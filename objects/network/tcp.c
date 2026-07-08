
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

Server mode, one connection at a time:

	LocalPort	the port to listen on
	Out		bytes received from the peer go out here as messages
	In		messages arriving here are buffered and sent to the peer
	Enable		1 enables, 0 shuts the server down completely

A polling task runs every POLL_MS milliseconds: it accepts a waiting
connection if we have none, receives what the peer sent (each recv
becomes one message out the Out port), and drains the send buffer to
the peer.  When the peer closes we go back to listening for the next
connection.

Enable=0 is a full shutdown, not a pause: sockets close, msg_eof goes
out the Out port, and the polling task is not re-armed, so the object
stops holding the program open.  This is the wiring for a timed
server: connect a pulse to Enable and the falling edge shuts it down.

Payloads are null terminated strings for now, like the rest of the
flow: fine for text protocols, a binary payload needs a length carried
beside the data node.

Client mode and multiple connections are still to come, the reference
TCPObject.c has the connecting state machine and the ring pattern.

*/

#define TCP_CHUNK_SIZE 2048
#define POLL_MS 10

typedef struct InstanceData
{
	int     listenfd;
	int     clientfd;
	buff    sendbuf;
	TaskObj task;
	int     active;
	int     enabled;
	int     scheduled;
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

/* scheduler callback: accept, receive, send, re-arm */
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

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->active)
		return rtrn_dropped;

	local->scheduled = 0;

	/* accept a waiting connection if we have none */
	if (local->clientfd < 0)
	{
		peerlen = sizeof(peer);
		fd = accept(local->listenfd, (struct sockaddr *)&peer, &peerlen);
		if (fd >= 0)
		{
			Tcp_SetNonBlocking(fd);
			local->clientfd = fd;
			DebugPrint ( "TCP accepted a connection.", __FILE__, __LINE__, OBJMSGHANDLING);
		}
	}

	/* receive: each recv becomes one message out the Out port */
	if (local->clientfd >= 0)
	{
		bytes = recv(local->clientfd, buffer, TCP_CHUNK_SIZE, 0);

		if (bytes > 0)
		{
			buffer[bytes] = 0;

			chunk = NewNode(STRING);
			SetName(chunk, "Data");
			SetValueStr(chunk, buffer);
			SndMsg(instance, "Out", msg_send, chunk);
			DelNode(chunk);
		}
		else if (bytes == 0)
		{
			/* peer closed, go back to listening */
			close(local->clientfd);
			local->clientfd = -1;
			DebugPrint ( "TCP peer closed the connection.", __FILE__, __LINE__, OBJMSGHANDLING);
		}
		else if (errno != EAGAIN && errno != EWOULDBLOCK)
		{
			close(local->clientfd);
			local->clientfd = -1;
			DebugPrint ( "TCP receive error, connection dropped.", __FILE__, __LINE__, ERROR);
		}
	}

	/* drain the send buffer to the peer */
	if (local->clientfd >= 0 && buffGetLength(local->sendbuf) > 0)
	{
		length = buffGetBlockFromTail(local->sendbuf, &block, TCP_CHUNK_SIZE);
		if (length)
		{
			sent = send(local->clientfd, block, length, 0);

			if (sent < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					buffGetUndoTail(local->sendbuf, length);
				else
				{
					close(local->clientfd);
					local->clientfd = -1;
					DebugPrint ( "TCP send error, connection dropped.", __FILE__, __LINE__, ERROR);
				}
			}
			else if ((unsigned int)sent < length)
			{
				/* put the unsent part back at the front */
				buffGetUndoTail(local->sendbuf, length - sent);
			}
		}
	}

	AddTaskMilli(local->task, POLL_MS, (FuncPtr)Tcp_Poll, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* subscription callback: buffer whatever arrives, the poll task sends it */
int Tcp_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char * str;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	if (message != msg_send)
		return rtrn_dropped;

	str = GetValueStr(data);
	if (str && str[0])
		buffAdd(local->sendbuf, str, strlen(str));

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
			if (local->clientfd >= 0)
			{
				close(local->clientfd);
				local->clientfd = -1;
			}
			if (local->listenfd >= 0)
			{
				close(local->listenfd);
				local->listenfd = -1;
			}

			local->active = 0;
			SetPropInt(instance, "State", Stopping);

			/* the stream is over for everyone downstream, and the */
			/* poll task, already armed, sees active 0 and stops    */
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
	local->clientfd = -1;
	local->sendbuf = buffCreate(4 * TCP_CHUNK_SIZE);
	local->task = NULL;
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "TCP");
	SetPropInt(instance, "LocalPort", 8080);
	SetPropInt(instance, "State", Starting);
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
		if (local->clientfd >= 0)
			close(local->clientfd);
		if (local->listenfd >= 0)
			close(local->listenfd);
		if (local->sendbuf)
			buffDestroy(local->sendbuf);
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
