
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "dyn/buff.h"

/*

TCP object, ported from the VNOS TCPObject (see TCPObject.c in this
directory for the reference implementation this descends from).

Server mode, any number of simultaneous connections:

	LocalPort	the port to listen on
	Out		bytes received from a peer go out here as messages,
			each carrying a Conn property identifying which one
	In		messages arriving here are buffered and sent to a peer -
			a Conn property picks which one, or broadcasts to
			every connected peer if it's absent/0
	Enable		1 enables, 0 shuts the server down completely

TCPObject.c's own ring is the model for this - a linked list of active
connections serviced by one shared polling task - but the reference
takes a shortcut its own comment admits: when a listening socket accepts,
it repurposes ITS OWN ring entry for the new client ("for now we can only
service one connection at a time") instead of giving the accepted
connection a ring entry of its own. That shortcut is exactly what's fixed
here: the listening socket keeps listening, and every accepted connection
gets its own entry in local->conns, so a second, third, thousandth peer
connecting never displaces the ones already being serviced.

A polling task runs every POLL_MS milliseconds: it accepts every waiting
connection (not just one - a listening socket can hand out several in a
single tick), then for each connection in local->conns: receives what
that peer sent (each recv becomes one message out Out, tagged with that
connection's Conn id), and drains that connection's own send buffer to
it. When a peer closes we send msg_eof out Out tagged with just that
Conn id and remove it from the list - the only signal anything sitting
on top of TCP gets that THIS ONE peer is gone (as opposed to the server
itself shutting down, below, which sends an untagged msg_eof meaning
everyone is gone). Objects that carry per-connection state across a
sequence of peers (WebSocket's handshake flag, Router's sniffed mode,
a Bridge's login) key it off Conn and reset just that Conn's entry on
this, not the Enable-driven shutdown below.

Enable=0 is a full shutdown, not a pause: every connection's socket
closes, the listening socket closes, an untagged (Conn 0, meaning
"everyone") msg_eof goes out Out, and the polling task is not re-armed,
so the object stops holding the program open. This is the wiring for a
timed server: connect a pulse to Enable and the falling edge shuts it
down.

Payloads carry a Length property beside the data (SetValueStrLen/
GetValueLen in node.c) so embedded NULs survive - needed once anything
binary sits on top of this, a WebSocket frame's masking key being the
first example. A plain text payload with no Length property falls back
to strlen(), same as before.

Client mode is still to come, the reference TCPObject.c has the
connecting state machine for it.

*/

#define TCP_CHUNK_SIZE 2048
#define POLL_MS 10

/* one accepted peer - local->conns is the head of a linked list of      */
/* these, the "ring" TCPObject.c's own comments describe, minus the      */
/* single-connection shortcut its accept path took                       */
typedef struct Connection
{
	int    fd;
	buff   sendbuf;
	int    peerClosed;	/* peer half-closed (recv==0); finish draining sendbuf, don't discard it */
	long   id;		/* never 0 and never reused - 0 is reserved to mean "every connection" */
	SSL   *ssl;		/* the TLS session, NULL on a plain connection */
	int    handshaking;	/* SSL_accept has not completed yet (non-blocking) */
	struct Connection *next;
} Connection;

typedef struct InstanceData
{
	int        listenfd;
	Connection *conns;
	long       nextConnId;
	TaskObj    task;
	int        active;
	int        enabled;
	int        scheduled;
	SSL_CTX   *ctx;		/* the TLS context, NULL when insecure */
	int        secure;	/* Secure was on at activation */
	int        clientMode;	/* RemoteAddr was set: connect out, don't listen */
	int        connectfd;	/* the socket a client connect is in flight on */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "TCP handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

void Tcp_SetNonBlocking(int fd);	/* defined below, used by the client path */

/* ---- TLS ------------------------------------------------------------ */
/* Ported from the VNOS reference (objects/demo/TCPObject/TCPObject.c),   */
/* which was a SECURE cross-platform TCP object: a server context built   */
/* from a PEM certificate and key, an SSL session per accepted peer, and  */
/* SSL_read/SSL_write in place of recv/send. Brought up to OpenSSL 3:     */
/* TLS_server_method for SSLv23_server_method, and the library's own      */
/* implicit init instead of SSL_load_error_strings/SSLeay_add_*.          */
/* The handshake is completed across poll ticks rather than in one call,  */
/* because these sockets are non-blocking (the original's single          */
/* SSL_accept assumed it would finish immediately).                        */

/* the last OpenSSL error, as a line for the log */
static void Tcp_SslError(char *what)
{
	char buf[300];
	unsigned long e = ERR_get_error();

	snprintf(buf, sizeof(buf), "TCP TLS: %s - %s", what,
			 e ? ERR_reason_error_string(e) : "no detail");
	DebugPrint(buf, __FILE__, __LINE__, ERROR);
}

/* build the server context from the instance's Cert/Key. Returns 0 and   */
/* logs loudly on any failure - a secure server that quietly serves in    */
/* the clear is the one outcome worse than not starting.                   */
static int Tcp_SslContext(NodeObj instance, InstanceData *local)
{
	char *cert = GetPropStr(instance, "SslCert");
	char *key  = GetPropStr(instance, "SslKey");
	char *pass = GetPropStr(instance, "SslPass");

	if (local->clientMode && (!cert || !cert[0]))
	{
		/* a client presents no certificate of its own - just make the
		   context and let the handshake verify the server's */
		local->ctx = SSL_CTX_new(TLS_client_method());
		if (!local->ctx)
		{
			Tcp_SslError("could not create the TLS client context");
			return 0;
		}
		return 1;
	}

	if (!cert || !cert[0] || !key || !key[0])
	{
		DebugPrint("TCP TLS: Secure is on but SslCert/SslKey are not set",
				   __FILE__, __LINE__, ERROR);
		return 0;
	}

	local->ctx = SSL_CTX_new(local->clientMode ? TLS_client_method()
												: TLS_server_method());
	if (!local->ctx)
	{
		Tcp_SslError("could not create the TLS context");
		return 0;
	}

	/* a key file may be encrypted - the panel's SslPass unlocks it */
	if (pass && pass[0])
		SSL_CTX_set_default_passwd_cb_userdata(local->ctx, pass);

	if (SSL_CTX_use_certificate_file(local->ctx, cert, SSL_FILETYPE_PEM) <= 0)
	{
		Tcp_SslError("certificate file rejected");
		SSL_CTX_free(local->ctx);
		local->ctx = NULL;
		return 0;
	}

	if (SSL_CTX_use_PrivateKey_file(local->ctx, key, SSL_FILETYPE_PEM) <= 0)
	{
		Tcp_SslError("private key file rejected");
		SSL_CTX_free(local->ctx);
		local->ctx = NULL;
		return 0;
	}

	if (!SSL_CTX_check_private_key(local->ctx))
	{
		DebugPrint("TCP TLS: the private key does not match the certificate",
				   __FILE__, __LINE__, ERROR);
		SSL_CTX_free(local->ctx);
		local->ctx = NULL;
		return 0;
	}

	return 1;
}

/* drive a non-blocking handshake forward; 1 when it has completed */
static int Tcp_SslHandshake(Connection *conn)
{
	int rc, err;

	rc = SSL_do_handshake(conn->ssl);	/* accept or connect - the side was set already */
	if (rc == 1)
	{
		conn->handshaking = 0;
		DebugPrint("TCP TLS: handshake complete.", __FILE__, __LINE__, OBJMSGHANDLING);
		return 1;
	}

	err = SSL_get_error(conn->ssl, rc);
	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
		return 0;			/* not finished; try again next tick */

	Tcp_SslError("handshake failed");
	conn->peerClosed = 1;
	return 0;
}

/* read/write that don't care whether the connection is secure */
static int Tcp_ConnRecv(Connection *conn, char *buffer, int len)
{
	if (conn->ssl)
	{
		int rc = SSL_read(conn->ssl, buffer, len);
		if (rc <= 0)
		{
			int err = SSL_get_error(conn->ssl, rc);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
			{
				errno = EAGAIN;
				return -1;
			}
			if (err == SSL_ERROR_ZERO_RETURN)
				return 0;		/* peer closed the TLS session cleanly */
			errno = EIO;
			return -1;
		}
		return rc;
	}

	return (int)recv(conn->fd, buffer, len, 0);
}

static int Tcp_ConnSend(Connection *conn, char *block, int len)
{
	if (conn->ssl)
	{
		int rc = SSL_write(conn->ssl, block, len);
		if (rc <= 0)
		{
			int err = SSL_get_error(conn->ssl, rc);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
			{
				errno = EAGAIN;
				return -1;
			}
			errno = EIO;
			return -1;
		}
		return rc;
	}

	return (int)send(conn->fd, block, len, 0);
}

/* ---- client mode ---------------------------------------------------- */
/* The connecting state machine TCPObject.c sketched but never finished -
   its own comment on the EINPROGRESS case reads "once we allow this to be
   non blocking, we will need to check for this in it's own loop". This is
   that loop: connect() returns immediately, and the poll task asks the
   socket each tick whether it has finished, so a connect to a dead host
   costs one getsockopt per tick instead of stalling the whole core.

   Hostname resolution is the one blocking call left (getaddrinfo), so a
   NUMERIC address never blocks at all, and a name says loudly that it is
   about to block. That is what async-dns/ exists to retire, and it should
   land with this. */

static int Tcp_ResolveInto(char *addr, struct sockaddr_in *out)
{
	struct addrinfo hints, *res = NULL;

	if (inet_aton(addr, &out->sin_addr))
		return 1;			/* numeric: no lookup, no blocking */

	DebugPrint("TCP client: resolving a HOSTNAME blocks the core until it "
			   "answers - use an IP, or wire async-dns", __FILE__, __LINE__, ERROR);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(addr, NULL, &hints, &res) != 0 || !res)
		return 0;

	out->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);
	return 1;
}

/* start a non-blocking connect; 1 if it is under way (or already done) */
static int Tcp_ConnectStart(NodeObj instance, InstanceData *local)
{
	struct sockaddr_in addr;
	char *host = GetPropStr(instance, "RemoteAddr");
	int port = GetPropInt(instance, "RemotePort");
	int fd;

	if (!port)
		port = GetPropInt(instance, "LocalPort");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);

	if (!host || !host[0] || !Tcp_ResolveInto(host, &addr))
	{
		DebugPrint("TCP client: RemoteAddr is empty or will not resolve",
				   __FILE__, __LINE__, ERROR);
		return 0;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		DebugPrint("TCP client: could not make a socket", __FILE__, __LINE__, ERROR);
		return 0;
	}

	Tcp_SetNonBlocking(fd);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
		&& errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		close(fd);
		DebugPrint("TCP client: connect refused outright", __FILE__, __LINE__, ERROR);
		return 0;
	}

	local->connectfd = fd;		/* EINPROGRESS is the normal path - finish in the poll */
	DebugPrint("TCP client: connecting.", __FILE__, __LINE__, OBJMSGHANDLING);
	return 1;
}

/* has the in-flight connect finished? adopt it as an ordinary Connection
   when it has, so every read/write/close path below works unchanged */
static void Tcp_ConnectPoll(NodeObj instance, InstanceData *local)
{
	Connection *conn;
	socklen_t len = sizeof(int);
	int err = 0;
	fd_set wfds;
	struct timeval tv;

	FD_ZERO(&wfds);
	FD_SET(local->connectfd, &wfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;			/* poll, never wait - this is the shared core */

	if (select(local->connectfd + 1, NULL, &wfds, NULL, &tv) <= 0)
		return;			/* still connecting; try again next tick */

	if (getsockopt(local->connectfd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err)
	{
		close(local->connectfd);
		local->connectfd = -1;
		SetPropStr(instance, "Connected", "0");
		DebugPrint("TCP client: connect failed.", __FILE__, __LINE__, ERROR);
		return;
	}

	conn = malloc(sizeof(Connection));
	conn->fd = local->connectfd;
	conn->sendbuf = buffCreate(4 * TCP_CHUNK_SIZE);
	conn->peerClosed = 0;
	conn->id = ++local->nextConnId;
	conn->ssl = NULL;
	conn->handshaking = 0;
	conn->next = local->conns;
	local->conns = conn;
	local->connectfd = -1;

	/* a secure client speaks TLS from its side of the wire */
	if (local->ctx)
	{
		conn->ssl = SSL_new(local->ctx);
		if (conn->ssl)
		{
			SSL_set_fd(conn->ssl, conn->fd);
			SSL_set_connect_state(conn->ssl);
			conn->handshaking = 1;
		}
	}

	SetPropStr(instance, "Connected", "1");
	DebugPrint("TCP client: connected.", __FILE__, __LINE__, OBJMSGHANDLING);
}

/* tear down one connection's TLS session */
static void Tcp_SslClose(Connection *conn)
{
	if (!conn->ssl)
		return;

	SSL_shutdown(conn->ssl);
	SSL_free(conn->ssl);
	conn->ssl = NULL;
}

void Tcp_SetNonBlocking(int fd)
{
	fcntl(fd, F_SETFL, O_NONBLOCK);
}

/* scheduler callback: accept everyone waiting, service every connection, re-arm */
int Tcp_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	char buffer[TCP_CHUNK_SIZE + 1];
	char * block;
	unsigned int length;
	int bytes, sent, fd;
	NodeObj chunk;
	struct sockaddr_in peer;
	socklen_t peerlen;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");
	Connection *conn, **link;

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->active)
		return rtrn_dropped;

	local->scheduled = 0;

	/* a client connect in flight finishes here, never in a blocking wait */
	if (local->connectfd >= 0)
		Tcp_ConnectPoll(instance, local);

	/* accept every connection currently waiting, not just one - several  */
	/* can show up between poll ticks, and the listening socket itself    */
	/* never stops listening to service them                              */
	for (; local->listenfd >= 0; )
	{
		peerlen = sizeof(peer);
		fd = accept(local->listenfd, (struct sockaddr *)&peer, &peerlen);
		if (fd < 0)
			break;

		Tcp_SetNonBlocking(fd);

		conn = malloc(sizeof(Connection));
		conn->fd = fd;
		conn->sendbuf = buffCreate(4 * TCP_CHUNK_SIZE);
		conn->peerClosed = 0;
		conn->id = ++local->nextConnId;
		conn->ssl = NULL;
		conn->handshaking = 0;
		conn->next = local->conns;
		local->conns = conn;

		/* a secure server wraps every accepted peer in a TLS session -    */
		/* the handshake finishes across ticks, since the fd is non-       */
		/* blocking (TCPObject.c's SSL_new/set_fd/accept, done right)      */
		if (local->ctx)
		{
			conn->ssl = SSL_new(local->ctx);
			if (!conn->ssl)
			{
				Tcp_SslError("could not create the TLS session");
				conn->peerClosed = 1;
			}
			else
			{
				SSL_set_fd(conn->ssl, fd);
				SSL_set_accept_state(conn->ssl);
				conn->handshaking = 1;
				Tcp_SslHandshake(conn);
			}
		}

		DebugPrint ( "TCP accepted a connection.", __FILE__, __LINE__, OBJMSGHANDLING);
	}

	/* service every accepted connection */
	link = &local->conns;
	while (*link)
	{
		conn = *link;

		/* receive: each recv becomes one message out the Out port,   */
		/* tagged with which connection it came from                  */
		/* a session still shaking hands can carry no application data  */
		/* yet - drive it forward and leave the rest of this tick alone */
		if (conn->fd >= 0 && conn->handshaking)
		{
			Tcp_SslHandshake(conn);
			if (conn->handshaking && !conn->peerClosed)
			{
				link = &conn->next;
				continue;
			}
		}

		if (conn->fd >= 0)
		{
			bytes = Tcp_ConnRecv(conn, buffer, TCP_CHUNK_SIZE);

			if (bytes > 0)
			{
				/* binary-safe: recv'd bytes may contain embedded NULs (a  */
				/* WebSocket masking key, for instance) - the exact count  */
				/* travels as a Length property beside the data itself     */
				chunk = NewNode(STRING);
				SetName(chunk, "Data");
				SetValueStrLen(chunk, buffer, bytes);
				SetPropInt(chunk, "Length", bytes);
				SetPropLong(chunk, "Conn", conn->id);
				SndMsg(instance, "Out", msg_send, chunk);
			}
			else if (bytes == 0)
			{
				/* the peer half-closed (their write side; recv==0 is EOF on   */
				/* OUR read side, nothing more about their read side) - a      */
				/* client that finishes sending before reading its response    */
				/* (nc -q does exactly this) is completely ordinary, not gone. */
				/* Closing outright here would abandon whatever is still       */
				/* queued in sendbuf mid-transfer. Only the drain step below,  */
				/* once sendbuf is actually empty, closes for real.            */
				conn->peerClosed = 1;
			}
			else if (errno != EAGAIN && errno != EWOULDBLOCK)
			{
				Tcp_SslClose(conn);
				close(conn->fd);
				conn->fd = -1;
				conn->peerClosed = 1;	/* fall through: close-and-remove below, sendbuf is moot with fd gone */
				DebugPrint ( "TCP receive error, connection dropped.", __FILE__, __LINE__, ERROR);
			}
		}

		/* drain this connection's own send buffer to its peer */
		if (conn->fd >= 0 && buffGetLength(conn->sendbuf) > 0)
		{
			length = buffGetBlockFromTail(conn->sendbuf, &block, TCP_CHUNK_SIZE);
			if (length)
			{
				sent = Tcp_ConnSend(conn, block, length);

				if (sent < 0)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						buffGetUndoTail(conn->sendbuf, length);
					else
					{
						Tcp_SslClose(conn);
						close(conn->fd);
						conn->fd = -1;
						conn->peerClosed = 1;
						DebugPrint ( "TCP send error, connection dropped.", __FILE__, __LINE__, ERROR);
					}
				}
				else if ((unsigned int)sent < length)
				{
					/* put the unsent part back at the front */
					buffGetUndoTail(conn->sendbuf, length - sent);
				}
			}
		}

		/* the peer half-closed (or errored) and everything queued for it */
		/* has now gone out - safe to actually close, tell downstream just */
		/* this one connection is done, and drop it from the list          */
		if (conn->peerClosed && (conn->fd < 0 || buffGetLength(conn->sendbuf) == 0))
		{
			Tcp_SslClose(conn);
			if (conn->fd >= 0)
				close(conn->fd);

			chunk = NewNode(STRING);
			SetName(chunk, "Data");
			SetPropLong(chunk, "Conn", conn->id);
			SndMsg(instance, "Out", msg_eof, chunk);

			*link = conn->next;
			buffDestroy(conn->sendbuf);
			free(conn);
			DebugPrint ( "TCP peer closed the connection.", __FILE__, __LINE__, OBJMSGHANDLING);
			continue;
		}

		link = &conn->next;
	}

	AddTaskMilli(local->task, POLL_MS, (FuncPtr)Tcp_Poll, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* subscription callback: buffer whatever arrives, the poll task sends it. */
/* A Conn property picks one connection; absent or 0 broadcasts to every   */
/* connected peer - what a Bridge's own shared-view event traffic wants,   */
/* so every viewer sees the same thing, while a targeted reply (an HTTP    */
/* response, a WebSocket handshake) reaches only the peer that asked.      */
int Tcp_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char * str;
	int length;
	long connId;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");
	Connection *conn;

	if (!local || !local->active)
		return rtrn_dropped;

	if (message != msg_send)
		return rtrn_dropped;

	/* a Length property means the sender already knows its exact byte  */
	/* count (raw bytes that may hold embedded NULs, a WebSocket frame  */
	/* for instance) - fall back to strlen for a plain text message     */
	str = GetValueStr(data);
	length = GetPropInt(data, "Length");
	if (!length && str)
		length = strlen(str);

	if (!str || length <= 0)
		return rtrn_handled;

	connId = GetPropLong(data, "Conn");

	if (connId)
	{
		for (conn = local->conns; conn; conn = conn->next)
			if (conn->id == connId)
			{
				buffAdd(conn->sendbuf, str, length);
				break;
			}
	}
	else
	{
		for (conn = local->conns; conn; conn = conn->next)
			buffAdd(conn->sendbuf, str, length);
	}

	return rtrn_handled;
}

/* control callback: a 0 on the Enable line is a full shutdown */
int Tcp_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	if (GetValueInt(data))
	{
		local->enabled = 1;
		SetValueStr(GetPropNode(instance, "Enable"), "1");
		/* re-enabling a shut down server does not restart it, */
		/* activation is one shot for now                       */
	}
	else
	{
		local->enabled = 0;
		SetValueStr(GetPropNode(instance, "Enable"), "0");

		if (local->active)
		{
			Connection *conn, *next;

			for (conn = local->conns; conn; conn = next)
			{
				next = conn->next;
				Tcp_SslClose(conn);
				if (conn->fd >= 0)
					close(conn->fd);
				buffDestroy(conn->sendbuf);
				free(conn);
			}
			local->conns = NULL;

			if (local->listenfd >= 0)
			{
				close(local->listenfd);
				local->listenfd = -1;
			}

			/* the TLS context dies with the server it belonged to */
			if (local->ctx)
			{
				SSL_CTX_free(local->ctx);
				local->ctx = NULL;
			}
			SetPropStr(instance, "Secured", "0");

			local->active = 0;
			SetPropInt(instance, "State", Stopping);

			/* untagged (Conn 0): every connection is gone, not just one, */
			/* and the poll task, already armed, sees active 0 and stops  */
			SndMsg(instance, "Out", msg_eof, NULL);

			DebugPrint ( "TCP server shut down by its Enable line.", __FILE__, __LINE__, OBJMSGHANDLING);
		}
	}

	return rtrn_handled;
}

/* bind, listen, and start the polling task */
int Tcp_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	int port, one = 1;
	char * bindaddr;
	struct sockaddr_in addr;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	/* CLIENT MODE: a RemoteAddr means dial out rather than listen. The
	   connect is non-blocking and completes in the poll task, so the two
	   modes share every read/write/close path below - a connection is a
	   connection however it was made. */
	{
		char *remote = GetPropStr(instance, "RemoteAddr");
		local->clientMode = (remote && remote[0]) ? 1 : 0;
	}

	if (local->clientMode)
	{
		local->secure = GetPropInt(instance, "Secure") ? 1 : 0;
		if (local->secure && !Tcp_SslContext(instance, local))
		{
			DebugPrint("TCP client: secure requested but TLS could not start",
					   __FILE__, __LINE__, ERROR);
			return rtrn_dropped;
		}

		if (!Tcp_ConnectStart(instance, local))
			return rtrn_dropped;

		if (!local->task)
			local->task = CreateTask(ObjGetTaskList());
		local->active = 1;
		SetPropInt(instance, "State", Running);
		SetPropStr(instance, "Secured", local->ctx ? "1" : "0");

		AddTaskMilli(local->task, POLL_MS, (FuncPtr)Tcp_Poll, msg_send, instance);
		local->scheduled = 1;
		return rtrn_handled;
	}

	port = GetPropInt(instance, "LocalPort");
	if (port < 1 || port > 65535)
	{
		DebugPrint ( "TCP has no usable LocalPort.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	local->listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (local->listenfd < 0)
	{
		DebugPrint ( "TCP could not create a socket.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	/* let the port be reused right away between runs */
	setsockopt(local->listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* LocalAddr picks the interface to listen on (e.g. 127.0.0.1); */
	/* absent or empty means all interfaces, same as 0.0.0.0        */
	bindaddr = GetPropStr(instance, "LocalAddr");
	if (bindaddr && strlen(bindaddr))
	{
		addr.sin_addr.s_addr = inet_addr(bindaddr);
		if (addr.sin_addr.s_addr == INADDR_NONE)
		{
			DebugPrint ( "TCP has an unusable LocalAddr.", __FILE__, __LINE__, ERROR);
			close(local->listenfd);
			local->listenfd = -1;
			return rtrn_dropped;
		}
	}

	addr.sin_port = htons(port);

	if (bind(local->listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		close(local->listenfd);
		local->listenfd = -1;
		DebugPrint ( "TCP could not bind its port.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	if (listen(local->listenfd, 25) < 0)
	{
		close(local->listenfd);
		local->listenfd = -1;
		DebugPrint ( "TCP could not listen on its port.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	Tcp_SetNonBlocking(local->listenfd);

	/* a secure server builds its TLS context here, once, before the       */
	/* first peer arrives. If Secure is on and the context cannot be       */
	/* built, DO NOT fall back to serving in the clear - refuse to start.  */
	local->secure = GetPropInt(instance, "Secure") ? 1 : 0;
	if (local->secure && !Tcp_SslContext(instance, local))
	{
		close(local->listenfd);
		local->listenfd = -1;
		SetPropInt(instance, "State", Starting);
		DebugPrint("TCP: secure mode requested but TLS could not start - not listening",
				   __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	/* one task struct for the instance's whole life - see leaktest.py */
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);
	SetPropStr(instance, "Secured", local->ctx ? "1" : "0");

	AddTaskMilli(local->task, POLL_MS, (FuncPtr)Tcp_Poll, msg_send, instance);
	local->scheduled = 1;

	DebugPrint ( "TCP server is listening.", __FILE__, __LINE__, OBJMSGHANDLING);

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData * local = malloc(sizeof(InstanceData));

	local->listenfd = -1;
	local->conns = NULL;
	local->nextConnId = 0;
	local->task = NULL;
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;
	local->ctx = NULL;
	local->secure = 0;
	local->clientMode = 0;
	local->connectfd = -1;

	instance = NewNode(INTEGER);
	SetName(instance, "TCP");
	SetPropInt(instance, "LocalPort", 8080);
	WatchableProp(instance, "LocalPort");

	/* TLS: the VNOS reference's SecurityMode and its PEM cert/key. Off by */
	/* default; when on, the cert and key must load or the server refuses  */
	/* to listen at all rather than quietly serving in the clear.          */
	/* client mode: set RemoteAddr (and RemotePort) and this object dials
	   out instead of listening - the reference's TCP_CLIENT mode */
	SetPropStr(instance, "RemoteAddr", "");
	SetPropInt(instance, "RemotePort", 0);
	SetPropStr(instance, "Connected", "0");

	SetPropStr(instance, "Secure", "0");
	SetPropStr(instance, "SslCert", "");
	SetPropStr(instance, "SslKey", "");
	SetPropStr(instance, "SslPass", "");
	SetPropStr(instance, "Secured", "0");	/* readback: TLS actually running */
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropInt(instance, "Out", 0);		/* received bytes go out here */
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Tcp_Activate);

	/* input port: messages arriving here are sent to the peer */
	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)Tcp_OnIn);

	/* enable port: a 0 on this line shuts the server down */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)Tcp_OnEnable);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		Connection *conn, *next;

		/* stop the poll task before freeing local, or a still-scheduled */
		/* task fires later with a dangling instance pointer as its data */
		if (local->task)
			DeleteTask(local->task);

		for (conn = local->conns; conn; conn = next)
		{
			next = conn->next;
			Tcp_SslClose(conn);
			if (conn->fd >= 0)
				close(conn->fd);
			buffDestroy(conn->sendbuf);
			free(conn);
		}

		if (local->listenfd >= 0)
			close(local->listenfd);
		if (local->ctx)
			SSL_CTX_free(local->ctx);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class;
	struct sigaction handle;

	/* a peer disappearing mid send must not kill the process */
	memset(&handle, 0, sizeof(handle));
	handle.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &handle, NULL);

	class = NewNode(INTEGER);
	SetName(class, "TCP");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishProp(ClassSelf, "LocalPort", "data", PROP_TEXTBOX, "8080");
	PublishProp(ClassSelf, "RemoteAddr", "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "RemotePort", "data", PROP_TEXTBOX, "0");
	PublishProp(ClassSelf, "Connected",  "data", PROP_LED, "0");
	PublishProp(ClassSelf, "Secure",    "data", PROP_CHECKBOX, "0");
	PublishProp(ClassSelf, "SslCert",   "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "SslKey",    "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "SslPass",   "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "Secured",   "data", PROP_LED, "0");
	PublishProp(ClassSelf, "Enable",    "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "In",        "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Out",       "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "State",     "data", PROP_LED, "1");

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;

	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "TCP");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c660");
	SetPropStr(temp, "Version", "1.0");
	SetPropStr(temp, "Dependencies", "");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
