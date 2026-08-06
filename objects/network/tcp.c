/*****************************************************************/
// Module:  tcp.c
//
// Purpose: the TCP object - objects/demo/TCPObject ported onto this
//          framework, keeping its shape: ONE message function switching on
//          the ids in tcp.h, all of its state in its own struct, and its
//          callbacks going to the {owner, base, port} it was handed at
//          creation. The owner picks the base, so an owner holding several
//          objects tells their replies apart.
//
//          tcp.h is the whole interface. Nothing else is reachable: no
//          configuration properties, no state properties, no second way in.
/*****************************************************************/

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
#include "tcp.h"

#define TCP_CHUNK_SIZE 2048
#define POLL_MS 10

enum { OFF, ON };

/* one connection - the reference's ring, minus its single-connection
   shortcut: the listening socket keeps listening and every accepted peer
   gets its own entry */
typedef struct Connection
{
	int    fd;
	buff   sendbuf;
	int    peerClosed;
	long   id;					/* 1..N; 0 is the server/client instance */
	SSL   *ssl;
	int    handshaking;
	struct Connection *next;
} Connection;

typedef struct InstanceData
{
	int         listenfd;
	int         connectfd;		/* a client connect in flight */
	Connection *conns;
	long        nextConnId;
	int         state;			/* OFF / ON */
	TaskObj     task;
	int         scheduled;		/* the poll is armed - arming a linked task
								   corrupts the scheduler's task list */

	/* the vars, private: reachable only through the ids in tcp.h */
	char        remoteHost[256];
	int         remotePort;
	int         localPort;
	long        currentConn;
	int         connCount;
	int         maxConns;
	int         mode;			/* TCP_CLIENT / TCP_SERVER */
	int         securityMode;
	char        cert[256];
	char        key[256];

	SSL_CTX    *ctx;

	/* the reference's New(class, msgID, owner): who to tell, the base id THEY
	   chose for these replies, and the port on them it lands on */
	NodeObj     owner;
	int         msgBase;
	char        callback[64];
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static int  Tcp_Poll(NodeObj instance, NodeObj taskdata, int reason);
static int  Tcp_Start(NodeObj instance, InstanceData * local);
static int  Tcp_Stop(NodeObj instance, InstanceData * local);
static void Tcp_SetNonBlocking(int fd);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;
	return rtrn_handled;
}

/* ---- the callbacks: base + the ordinal from tcp.h ------------------- */

static void Tcp_Callback(NodeObj instance, InstanceData * local, int ordinal,
						 long connId, char * payload, int length)
{
	NodeObj chunk;

	(void) instance;

	if (!local->owner || !local->callback[0])
		return;

	/* which connection this concerns - the reference's
	   TCP_CURRENT_CONNECTION_VAR, set before the callback goes out */
	local->currentConn = connId;

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	if (payload && length > 0)
		SetValueStrLen(chunk, payload, length);

	DeliverMsg(local->owner, local->callback, local->msgBase + ordinal, chunk);
	DelNode(chunk);		/* DeliverMsg is synchronous: ours to free */
}

static void Tcp_Error(NodeObj instance, InstanceData * local, char * what)
{
	DebugPrint(what, __FILE__, __LINE__, ERROR);
	Tcp_Callback(instance, local, TCP_ERROR_CALLBACK, local->currentConn,
				 what, (int) strlen(what));
}

/* ---- TLS ------------------------------------------------------------ */

static void Tcp_SslError(char *what)
{
	char buf[300];
	unsigned long e = ERR_get_error();

	snprintf(buf, sizeof(buf), "TCP TLS: %s - %s", what,
			 e ? ERR_reason_error_string(e) : "no detail");
	DebugPrint(buf, __FILE__, __LINE__, ERROR);
}

static int Tcp_SslContext(InstanceData * local)
{
	if (local->mode == TCP_CLIENT && !local->cert[0])
	{
		local->ctx = SSL_CTX_new(TLS_client_method());
		if (!local->ctx)
		{
			Tcp_SslError("could not create the TLS client context");
			return 0;
		}
		return 1;
	}

	if (!local->cert[0] || !local->key[0])
	{
		DebugPrint("TCP TLS: security is on but the cert/key are not set",
				   __FILE__, __LINE__, ERROR);
		return 0;
	}

	local->ctx = SSL_CTX_new(local->mode == TCP_CLIENT ? TLS_client_method()
													  : TLS_server_method());
	if (!local->ctx)
	{
		Tcp_SslError("could not create the TLS context");
		return 0;
	}

	if (SSL_CTX_use_certificate_file(local->ctx, local->cert, SSL_FILETYPE_PEM) <= 0)
	{
		Tcp_SslError("certificate file rejected");
		SSL_CTX_free(local->ctx);
		local->ctx = NULL;
		return 0;
	}

	if (SSL_CTX_use_PrivateKey_file(local->ctx, local->key, SSL_FILETYPE_PEM) <= 0)
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

static int Tcp_SslHandshake(Connection *conn)
{
	int rc = SSL_do_handshake(conn->ssl), err;

	if (rc == 1)
	{
		conn->handshaking = 0;
		return 1;
	}

	err = SSL_get_error(conn->ssl, rc);
	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
		return 0;

	Tcp_SslError("handshake failed");
	conn->peerClosed = 1;
	return 0;
}

static void Tcp_SslClose(Connection *conn)
{
	if (!conn->ssl)
		return;
	SSL_shutdown(conn->ssl);
	SSL_free(conn->ssl);
	conn->ssl = NULL;
}

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
				return 0;
			errno = EIO;
			return -1;
		}
		return rc;
	}

	return (int) recv(conn->fd, buffer, len, 0);
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

	return (int) send(conn->fd, block, len, 0);
}

/* ---- client mode ---------------------------------------------------- */

static int Tcp_ResolveInto(char *addr, struct sockaddr_in *out)
{
	struct addrinfo hints, *res = NULL;

	if (inet_aton(addr, &out->sin_addr))
		return 1;

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

static int Tcp_ConnectStart(NodeObj instance, InstanceData * local)
{
	struct sockaddr_in addr;
	int fd, port = local->remotePort ? local->remotePort : local->localPort;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short) port);

	if (!local->remoteHost[0] || !Tcp_ResolveInto(local->remoteHost, &addr))
	{
		Tcp_Error(instance, local, "TCP client: the remote host is empty or will not resolve");
		return 0;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		Tcp_Error(instance, local, "TCP client: could not make a socket");
		return 0;
	}

	Tcp_SetNonBlocking(fd);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
		&& errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		close(fd);
		Tcp_Error(instance, local, "TCP client: connect refused outright");
		return 0;
	}

	local->connectfd = fd;
	return 1;
}

/* the connecting state machine the reference sketched: ask the socket each
   tick whether it finished, so a dead host costs one getsockopt per tick */
static void Tcp_ConnectPoll(NodeObj instance, InstanceData * local)
{
	Connection *conn;
	socklen_t len = sizeof(int);
	int err = 0;
	fd_set wfds;
	struct timeval tv;

	FD_ZERO(&wfds);
	FD_SET(local->connectfd, &wfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(local->connectfd + 1, NULL, &wfds, NULL, &tv) <= 0)
		return;

	if (getsockopt(local->connectfd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err)
	{
		close(local->connectfd);
		local->connectfd = -1;
		Tcp_Error(instance, local, "TCP client: connect failed");
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

	Tcp_Callback(instance, local, TCP_NEW_CONNECTION_CALLBACK, conn->id, NULL, 0);
	DebugPrint("TCP client: connected.", __FILE__, __LINE__, OBJMSGHANDLING);
}

static void Tcp_SetNonBlocking(int fd)
{
	fcntl(fd, F_SETFL, O_NONBLOCK);
}

/* ---- the verbs ------------------------------------------------------ */

/* TCP_START_MSG */
static int Tcp_Start(NodeObj instance, InstanceData * local)
{
	struct sockaddr_in addr;
	int one = 1;

	if (local->state == ON)
		return rtrn_dropped;

	if (local->securityMode && !Tcp_SslContext(local))
	{
		Tcp_Error(instance, local, "TCP: security was asked for but TLS could not start");
		return rtrn_dropped;
	}

	if (local->mode == TCP_CLIENT)
	{
		if (!Tcp_ConnectStart(instance, local))
			return rtrn_dropped;
	}
	else
	{
		if (local->localPort < 1 || local->localPort > 65535)
		{
			Tcp_Error(instance, local, "TCP: no usable local port");
			return rtrn_dropped;
		}

		local->listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (local->listenfd < 0)
		{
			Tcp_Error(instance, local, "TCP: could not create a socket");
			return rtrn_dropped;
		}

		setsockopt(local->listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons((unsigned short) local->localPort);

		if (bind(local->listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		{
			close(local->listenfd);
			local->listenfd = -1;
			Tcp_Error(instance, local, "TCP: could not bind the port");
			return rtrn_dropped;
		}

		if (listen(local->listenfd, 25) < 0)
		{
			close(local->listenfd);
			local->listenfd = -1;
			Tcp_Error(instance, local, "TCP: could not listen on the port");
			return rtrn_dropped;
		}

		Tcp_SetNonBlocking(local->listenfd);
	}

	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	if (!local->scheduled)
	{
		AddTaskMilli(local->task, POLL_MS, (FuncPtr)Tcp_Poll, msg_send, instance);
		local->scheduled = 1;
	}

	local->state = ON;
	SetPropInt(instance, "State", Running);
	DebugPrint("TCP started.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* TCP_STOP_MSG */
static int Tcp_Stop(NodeObj instance, InstanceData * local)
{
	Connection *conn, *next;

	if (local->state == OFF)
		return rtrn_dropped;

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
		close(local->listenfd);
	local->listenfd = -1;

	if (local->connectfd >= 0)
		close(local->connectfd);
	local->connectfd = -1;

	if (local->ctx)
	{
		SSL_CTX_free(local->ctx);
		local->ctx = NULL;
	}

	local->connCount = 0;
	local->state = OFF;
	SetPropInt(instance, "State", Stopping);

	DebugPrint("TCP stopped.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* TCP_SEND_DATA_MSG - to TCP_CURRENT_CONNECTION_VAR, or to every open peer
   when that is 0. LENGTH FIRST: GetValueLen converts the value when it is not
   already a string (data.c), which reallocates it and leaves an earlier
   pointer stale. */
static int Tcp_SendData(NodeObj instance, InstanceData * local, NodeObj data)
{
	Connection *conn;
	char * str;
	int length;

	(void) instance;

	if (local->state == OFF || !data)
		return rtrn_dropped;

	length = GetValueLen(data);
	str = GetValueStr(data);
	if (!length && str)
		length = (int) strlen(str);

	if (!str || length <= 0)
		return rtrn_dropped;

	if (local->currentConn)
	{
		for (conn = local->conns; conn; conn = conn->next)
			if (conn->id == local->currentConn)
			{
				buffAdd(conn->sendbuf, str, length);
				return rtrn_handled;
			}
		return rtrn_dropped;
	}

	for (conn = local->conns; conn; conn = conn->next)
		buffAdd(conn->sendbuf, str, length);

	return rtrn_handled;
}

/* TCP_CLOSE_CONNECTION_MSG - one connection, server mode only */
static int Tcp_CloseConnection(InstanceData * local, long id)
{
	Connection *conn;

	for (conn = local->conns; conn; conn = conn->next)
		if (conn->id == id)
		{
			conn->peerClosed = 1;	/* drains what is queued, then closes */
			return rtrn_handled;
		}

	return rtrn_dropped;
}

/* a var id: a node carrying a value SETS it, an empty one is FILLED IN -
   the reference's SETVARIABLE/GETVARIABLE pair. Everything except
   TCP_CURRENT_CONNECTION_VAR is refused while the object is started. */
static int Tcp_Variable(InstanceData * local, MsgId var, NodeObj data)
{
	char * value;
	int    setting;

	if (!data)
		return rtrn_dropped;

	value = GetValueStr(data);
	setting = value && value[0];

	if (setting && local->state == ON && var != TCP_CURRENT_CONNECTION_VAR)
		return rtrn_dropped;

	switch (var)
	{
	case TCP_REMOTE_HOST_VAR:
		if (setting)
			snprintf(local->remoteHost, sizeof(local->remoteHost), "%s", value);
		else
			SetValueStr(data, local->remoteHost);
		return rtrn_handled;

	case TCP_REMOTE_PORT_VAR:
		if (setting)
			local->remotePort = GetValueInt(data);
		else
			SetValueInt(data, local->remotePort);
		return rtrn_handled;

	case TCP_LOCAL_PORT_VAR:
		if (setting)
			local->localPort = GetValueInt(data);
		else
			SetValueInt(data, local->localPort);
		return rtrn_handled;

	case TCP_CURRENT_CONNECTION_VAR:
		if (setting)
			local->currentConn = GetValueInt(data);
		else
			SetValueInt(data, (int) local->currentConn);
		return rtrn_handled;

	case TCP_CONNECTION_COUNT_VAR:		/* get only */
		if (setting)
			return rtrn_dropped;
		SetValueInt(data, local->connCount);
		return rtrn_handled;

	case TCP_MAX_CONNECTIONS_VAR:
		if (setting)
		{
			int n = GetValueInt(data);
			local->maxConns = (n > 0 && n <= MAX_CONNECTS) ? n : MAX_CONNECTS;
		}
		else
			SetValueInt(data, local->maxConns);
		return rtrn_handled;

	case TCP_CONNECTION_MODE_VAR:
		if (setting)
		{
			int m = GetValueInt(data);
			if (m != TCP_CLIENT && m != TCP_SERVER)
				return rtrn_dropped;
			local->mode = m;
		}
		else
			SetValueInt(data, local->mode);
		return rtrn_handled;

	case TCP_SECURITY_MODE_VAR:
		if (setting)
			local->securityMode = GetValueInt(data) ? 1 : 0;
		else
			SetValueInt(data, local->securityMode);
		return rtrn_handled;

	case TCP_SECURITY_CERT_VAR:
		if (setting)
			snprintf(local->cert, sizeof(local->cert), "%s", value);
		else
			SetValueStr(data, local->cert);
		return rtrn_handled;

	case TCP_SECURITY_KEY_VAR:
		if (setting)
			snprintf(local->key, sizeof(local->key), "%s", value);
		else
			SetValueStr(data, local->key);
		return rtrn_handled;

	default:
		return rtrn_dropped;
	}
}

/* THE message function - the reference's ObjectMessageFunc, switching on the
   ids in tcp.h. This is the object's only entrance. */
int Tcp_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	switch (message)
	{
	case TCP_SEND_DATA_MSG:
		return Tcp_SendData(instance, local, data);

	case TCP_START_MSG:
		return Tcp_Start(instance, local);

	case TCP_STOP_MSG:
		return Tcp_Stop(instance, local);

	case TCP_CLOSE_CONNECTION_MSG:
		return Tcp_CloseConnection(local, data ? GetValueInt(data) : 0);

	case TCP_REMOTE_HOST_VAR:
	case TCP_REMOTE_PORT_VAR:
	case TCP_LOCAL_PORT_VAR:
	case TCP_CURRENT_CONNECTION_VAR:
	case TCP_CONNECTION_COUNT_VAR:
	case TCP_MAX_CONNECTIONS_VAR:
	case TCP_CONNECTION_MODE_VAR:
	case TCP_SECURITY_MODE_VAR:
	case TCP_SECURITY_CERT_VAR:
	case TCP_SECURITY_KEY_VAR:
		return Tcp_Variable(local, message, data);

	default:
		return rtrn_dropped;
	}
}

/* ---- the poll ------------------------------------------------------- */

static int Tcp_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	char buffer[TCP_CHUNK_SIZE + 1];
	char * block;
	unsigned int length;
	int bytes, sent, fd;
	struct sockaddr_in peer;
	socklen_t peerlen;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");
	Connection *conn, **link;

	(void) taskdata;

	if (reason == task_deactivate)
		return rtrn_handled;
	if (!local)
		return rtrn_dropped;

	local->scheduled = 0;

	if (local->state == OFF)
		return rtrn_dropped;

	if (local->connectfd >= 0)
		Tcp_ConnectPoll(instance, local);

	/* accept everyone waiting, up to TCP_MAX_CONNECTIONS_VAR */
	for (; local->listenfd >= 0; )
	{
		peerlen = sizeof(peer);
		fd = accept(local->listenfd, (struct sockaddr *)&peer, &peerlen);
		if (fd < 0)
			break;

		if (local->connCount >= local->maxConns)
		{
			close(fd);
			Tcp_Error(instance, local, "TCP: refused a connection, at the maximum");
			continue;
		}

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
		local->connCount++;

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

		/* the reference lets an owner refuse by returning 0 here; a handler's
		   verdict does not come back through DeliverMsg, so an owner that does
		   not want this peer answers with TCPCloseConnection instead */
		Tcp_Callback(instance, local, TCP_NEW_CONNECTION_CALLBACK, conn->id, NULL, 0);
	}

	/* service every connection */
	link = &local->conns;
	while (*link)
	{
		conn = *link;

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
				Tcp_Callback(instance, local, TCP_RECEIVED_DATA_CALLBACK,
							 conn->id, buffer, bytes);
			else if (bytes == 0)
				conn->peerClosed = 1;	/* half-closed: drain, then close */
			else if (errno != EAGAIN && errno != EWOULDBLOCK)
			{
				Tcp_SslClose(conn);
				close(conn->fd);
				conn->fd = -1;
				conn->peerClosed = 1;
				Tcp_Error(instance, local, "TCP: receive error, connection dropped");
			}
		}

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
						Tcp_Error(instance, local, "TCP: send error, connection dropped");
					}
				}
				else if ((unsigned int) sent < length)
					buffGetUndoTail(conn->sendbuf, length - sent);
			}
		}

		if (conn->peerClosed && (conn->fd < 0 || buffGetLength(conn->sendbuf) == 0))
		{
			Tcp_SslClose(conn);
			if (conn->fd >= 0)
				close(conn->fd);

			Tcp_Callback(instance, local, TCP_REMOTE_CONNECTION_CLOSED_CALLBACK,
						 conn->id, NULL, 0);

			*link = conn->next;
			buffDestroy(conn->sendbuf);
			free(conn);
			if (local->connCount > 0)
				local->connCount--;
			continue;
		}

		link = &conn->next;
	}

	AddTaskMilli(local->task, POLL_MS, (FuncPtr)Tcp_Poll, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

/* `data` is the reference's New(class, msgID, owner): the creator's node, the
   BASE id it chose for these callbacks, and the port they arrive on. */
int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData * local = malloc(sizeof(InstanceData));
	char * cb;

	(void) message;

	memset(local, 0, sizeof(InstanceData));
	local->listenfd = -1;
	local->connectfd = -1;
	local->state = OFF;
	local->localPort = 8080;
	local->maxConns = MAX_CONNECTS;
	local->mode = TCP_SERVER;

	if (data)
	{
		local->owner = (NodeObj) GetPropLong(data, "Owner");
		local->msgBase = (int) GetPropLong(data, "MsgBase");
		cb = GetPropStr(data, "Callback");
		if (cb)
			strncpy(local->callback, cb, sizeof(local->callback) - 1);
	}

	instance = NewNode(INTEGER);
	SetName(instance, "TCPSocket");
	/* the node name is not the Name PROPERTY - PathOfInstance reads the
	   property, and without it every registry walk logs an error and dumps
	   this node. A private handle still has a name; it just has no path. */
	SetPropStr(instance, "Name", "TCPSocket");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);

	/* the object's one entrance: every id in tcp.h arrives here */
	SetPropStr(instance, "Msg", "");
	port = GetPropNode(instance, "Msg");
	SetPropLong(port, "OnMsg", (long)Tcp_MessageFunc);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	if (local)
	{
		if (local->task)
			DeleteTask(local->task);
		Tcp_Stop(instance, local);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);
	struct sigaction handle;

	(void) message; (void) data;

	/* a peer disappearing mid send must not kill the process */
	memset(&handle, 0, sizeof(handle));
	handle.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &handle, NULL);

	SetName(class, "TCPSocket");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	/* nothing is published: the interface is tcp.h, not a set of properties */

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "TCPSocket");
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
