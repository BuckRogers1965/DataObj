
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
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "TCP handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
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

	/* accept every connection currently waiting, not just one - several  */
	/* can show up between poll ticks, and the listening socket itself    */
	/* never stops listening to service them                              */
	for (;;)
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
		conn->next = local->conns;
		local->conns = conn;

		DebugPrint ( "TCP accepted a connection.", __FILE__, __LINE__, OBJMSGHANDLING);
	}

	/* service every accepted connection */
	link = &local->conns;
	while (*link)
	{
		conn = *link;

		/* receive: each recv becomes one message out the Out port,   */
		/* tagged with which connection it came from                  */
		if (conn->fd >= 0)
		{
			bytes = recv(conn->fd, buffer, TCP_CHUNK_SIZE, 0);

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
				DelNode(chunk);
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
				sent = send(conn->fd, block, length, 0);

				if (sent < 0)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						buffGetUndoTail(conn->sendbuf, length);
					else
					{
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
			if (conn->fd >= 0)
				close(conn->fd);

			chunk = NewNode(STRING);
			SetName(chunk, "Data");
			SetPropLong(chunk, "Conn", conn->id);
			SndMsg(instance, "Out", msg_eof, chunk);
			DelNode(chunk);

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
	struct sockaddr_in addr;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

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

	local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

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

	instance = NewNode(INTEGER);
	SetName(instance, "TCP");
	SetPropInt(instance, "LocalPort", 8080);
	WatchableProp(instance, "LocalPort");
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
			if (conn->fd >= 0)
				close(conn->fd);
			buffDestroy(conn->sendbuf);
			free(conn);
		}

		if (local->listenfd >= 0)
			close(local->listenfd);
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
