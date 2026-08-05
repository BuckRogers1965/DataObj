/*****************************************************************/
// Module:  udp.c
//
// Purpose: the UDP object - a port of objects/demo/UDPObject onto this
//          framework, keeping its shape: ONE message function switching on
//          the ids in udp.h, its state in its own struct, and its replies
//          going to the {owner, callback} it was handed at creation.
//
//          udp.h is the whole interface. Nothing else about this object is
//          reachable: there are no configuration properties to write, no
//          state properties to read, and no second way in.
/*****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "udp.h"

#define POLL_MS 10

enum { OFF, ON };

typedef struct InstanceData
{
	int     sockfd;
	int     state;			/* OFF / ON, as the reference's dev->state */
	TaskObj task;
	int     scheduled;		/* the poll is armed - arming a linked task
							   corrupts the scheduler's list */

	struct sockaddr_in server_addr,		/* where we listen */
					   client_addr;		/* where we send, and who last sent
										   to us - the reference reuses this
										   the same way */

	/* the reference's {owner, msgID} from New(class, msgID, owner): who to
	   tell, the id THEY chose for these replies (so an owner driving several
	   objects tells them apart - their bases differ, as the reference's
	   UDP_CALLBACK 0x5001 does), and the port on them the message lands on */
	NodeObj owner;
	MsgId   msgID;
	char    callback[64];
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static int Udp_Poll(NodeObj instance, NodeObj taskdata, int reason);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;
	return rtrn_handled;
}

/* a dotted quad never blocks; a name does, and says so */
static int Udp_ResolveInto(char * addr, struct in_addr * out)
{
	struct hostent * host;

	if (!addr || !addr[0])
		return 0;

	if (isdigit((unsigned char)addr[0]))
	{
		out->s_addr = inet_addr(addr);
		return out->s_addr != INADDR_NONE;
	}

	DebugPrint("UDP: resolving a HOSTNAME blocks until it answers - use an IP",
			   __FILE__, __LINE__, ERROR);

	host = gethostbyname(addr);
	if (!host)
		return 0;

	memcpy(&out->s_addr, host->h_addr_list[0], host->h_length);
	return 1;
}

/* UDP_START_MSG */
static int Udp_Start(NodeObj instance, InstanceData * local)
{
	int one = 1;
	socklen_t len;

	if (local->state == ON)
		return rtrn_dropped;

	local->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	local->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (local->sockfd < 0)
	{
		DebugPrint("UDP could not create a socket.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	setsockopt(local->sockfd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
	setsockopt(local->sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	if (bind(local->sockfd, (struct sockaddr *)&local->server_addr,
			 sizeof(local->server_addr)) < 0)
	{
		close(local->sockfd);
		local->sockfd = -1;
		DebugPrint("UDP could not bind its port.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	/* the port actually taken, so UDP_LISTEN_PORT_VAR can be read back when
	   0 was asked for - the reference's getsockname */
	len = sizeof(local->server_addr);
	getsockname(local->sockfd, (struct sockaddr *)&local->server_addr, &len);

	fcntl(local->sockfd, F_SETFL, O_NONBLOCK);
	fcntl(local->sockfd, F_SETFD, FD_CLOEXEC);

	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	if (!local->scheduled)
	{
		AddTaskMilli(local->task, POLL_MS, (FuncPtr)Udp_Poll, msg_send, instance);
		local->scheduled = 1;
	}

	local->state = ON;
	SetPropInt(instance, "State", Running);
	DebugPrint("UDP is listening.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* UDP_STOP_MSG */
static int Udp_Stop(NodeObj instance, InstanceData * local)
{
	if (local->state == OFF)
		return rtrn_dropped;

	if (local->sockfd >= 0)
		close(local->sockfd);
	local->sockfd = -1;
	local->state = OFF;
	SetPropInt(instance, "State", Stopping);

	DebugPrint("UDP stopped.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* UDP_SEND_PACKET_MSG - the packet's size is the message node's own byte
   count. LENGTH FIRST: GetValueLen converts the value when it is not already
   a string (data.c), which reallocates it and leaves an earlier pointer
   stale. */
static int Udp_SendPacket(NodeObj instance, InstanceData * local, NodeObj data)
{
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

	if (sendto(local->sockfd, str, length, 0,
			   (struct sockaddr *)&local->client_addr,
			   sizeof(local->client_addr)) < 0)
	{
		DebugPrint("UDP could not send a packet.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	return rtrn_handled;
}

/* a var id: a data node carrying a value SETS it, an empty one is FILLED IN
   with the current value - the reference's SETVARIABLE/GETVARIABLE pair */
static int Udp_Variable(NodeObj instance, InstanceData * local, MsgId var, NodeObj data)
{
	char * value;
	int    setting;

	(void) instance;

	if (!data)
		return rtrn_dropped;

	value = GetValueStr(data);
	setting = value && value[0];

	switch (var)
	{
	case UDP_REMOTE_HOST_VAR:
		if (setting)
			return Udp_ResolveInto(value, &local->client_addr.sin_addr)
				   ? rtrn_handled : rtrn_dropped;
		SetValueStr(data, inet_ntoa(local->client_addr.sin_addr));
		return rtrn_handled;

	case UDP_REMOTE_PORT_VAR:
		if (setting)
		{
			int port = GetValueInt(data);
			if (port < 0 || port > 65535)
				return rtrn_dropped;
			local->client_addr.sin_port = htons((unsigned short)port);
			return rtrn_handled;
		}
		SetValueInt(data, ntohs(local->client_addr.sin_port));
		return rtrn_handled;

	case UDP_LISTEN_PORT_VAR:
		if (setting)
		{
			int port = GetValueInt(data);
			if (port < 0 || port > 65535)
				return rtrn_dropped;
			local->server_addr.sin_port = htons((unsigned short)port);
			return rtrn_handled;
		}
		SetValueInt(data, ntohs(local->server_addr.sin_port));
		return rtrn_handled;

	default:
		return rtrn_dropped;
	}
}

/* THE message function - the reference's ObjectMessageFunc, switching on the
   ids in udp.h. This is the object's only entrance. */
int Udp_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	switch (message)
	{
	case UDP_SEND_PACKET_MSG:
		return Udp_SendPacket(instance, local, data);

	case UDP_START_MSG:
		return Udp_Start(instance, local);

	case UDP_STOP_MSG:
		return Udp_Stop(instance, local);

	case UDP_REMOTE_HOST_VAR:
	case UDP_REMOTE_PORT_VAR:
	case UDP_LISTEN_PORT_VAR:
		return Udp_Variable(instance, local, message, data);

	default:
		return rtrn_dropped;
	}
}

/* the polling task: every datagram waiting goes to the owner, on the port it
   named at creation - the reference's SendOMessage(dev->owner, dev->msgID, ...).
   recvfrom fills client_addr, so a reply after a receive goes back to whoever
   sent it, exactly as the reference behaves. */
static int Udp_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	char buffer[MAX_MSG_SIZE + 1];
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");
	socklen_t clilen;
	NodeObj chunk;
	int n;

	(void) taskdata;

	if (reason == task_deactivate)
		return rtrn_handled;
	if (!local)
		return rtrn_dropped;

	local->scheduled = 0;

	if (local->state == OFF)
		return rtrn_dropped;

	for (;;)
	{
		clilen = sizeof(local->client_addr);
		n = recvfrom(local->sockfd, buffer, MAX_MSG_SIZE, 0,
					 (struct sockaddr *)&local->client_addr, &clilen);
		if (n < 0)
			break;

		if (local->owner && local->callback[0])
		{
			chunk = NewNode(STRING);
			SetName(chunk, "Data");
			SetValueStrLen(chunk, buffer, n);
			/* the owner's own id, not one of ours - the reference's
			   SendOMessage(dev->owner, dev->msgID, 0, string) */
			DeliverMsg(local->owner, local->callback, local->msgID, chunk);
			DelNode(chunk);		/* DeliverMsg is synchronous: ours to free */
		}
	}

	AddTaskMilli(local->task, POLL_MS, (FuncPtr)Udp_Poll, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* `data` is the reference's New(class, msgID, owner): the creator's own node
   and the name of the port on it that replies should arrive at. Without it the
   object still works - it simply has nobody to tell. */
int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData * local = malloc(sizeof(InstanceData));
	char * cb;

	(void) message;

	memset(local, 0, sizeof(InstanceData));
	local->sockfd = -1;
	local->state = OFF;
	local->server_addr.sin_family = AF_INET;
	local->client_addr.sin_family = AF_INET;

	if (data)
	{
		local->owner = (NodeObj) GetPropLong(data, "Owner");
		local->msgID = (MsgId) GetPropLong(data, "MsgId");
		cb = GetPropStr(data, "Callback");
		if (cb)
			strncpy(local->callback, cb, sizeof(local->callback) - 1);
	}

	instance = NewNode(INTEGER);
	SetName(instance, "UDP");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);

	/* the object's one entrance: every id in udp.h arrives here */
	SetPropStr(instance, "Msg", "");
	port = GetPropNode(instance, "Msg");
	SetPropLong(port, "OnMsg", (long)Udp_MessageFunc);

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
		if (local->sockfd >= 0)
			close(local->sockfd);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "UDP");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	/* nothing is published: the interface is udp.h, not a set of properties */

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

	SetName(temp, "UDP");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b4f2c7d1-3e58-4a0c-9d16-7f8ab52e9c34");
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
