
/*

DNS lookup is a blocking call. 

If your machine is having network problems a program 
trying to lookup a dns entry can take minutes to respond.

This example demonstrates how to do a non blocking dns lookup. 

Since dns itself is not thread safe you can only do one lookup at a time.

The rest of the requests should be queued up to be processed one at a time.  

As the result comes back it can be checked in the main loop of the program
and the return value for that lookup delivered and the next lookup started.


Another way to speed everything up is to cache the value. 
This is the place to add DNS caching as needed.

The calling routines can also just keep their copy of the entry and use the
values found to recall the 

How to build stand alone test

gcc -g -Wall dnslookup.c -o dnslookup -lpthread -DTESTBUILD


The method used for communicating with the thread is to set
a sentinel value in the data structure that is passed to the
thread, occassionally come back and check the value to see if 
it has been marked completed. 

Once completed join wih the thread, collect it's values, call
the given callback to report the status and if found, the ip 
address.

*/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#include "../node.h"

//#include "pthread.h"


typedef struct dns_entry * dns;

typedef void (*dnscallback)(dns lookup);

enum {dns_processing=0, dns_found, dns_notfound };

struct dns_entry {
	int state;
	char * hostname;
	struct sockaddr_in client_addr;
	dns next;
	dnscallback callback;
	NodeObj data;
} dns_entry; 


/* Lookup dns request in seperate thread so the main thread does not block. */
void* mylookupdns (void* arg)
{

	dns lookup = (dns) arg;
		
	if (isalpha (lookup->hostname[0])) {
		struct hostent *host;

		/*  Get the ip number from the hostname */
		if ((host = gethostbyname (lookup->hostname)) == NULL)
		  {
			lookup->state = dns_notfound;
			return NULL;
		  }		// if by name

		/* copy network order ip number to sinaddr data structure */
		memcpy (&lookup->client_addr.sin_addr.s_addr,
			host->h_addr_list[0], host->h_length);
	} else if (isdigit (lookup->hostname[0]))
	      {
		/* if the hostname begins with a digit, then it is an IP number in dot code */
		lookup->client_addr.sin_addr.s_addr = inet_addr (lookup->hostname);
	} else {
		/* otherwise we did not resolve the hostname. */
		lookup->state = dns_notfound;
		return NULL;
	}

	lookup->state = dns_found;
	return NULL;
}


pthread_t thread;
pthread_attr_t attr;

dns lookup;

dns dnshead = NULL;
dns dnstail = NULL;

void
DnsProcess(){

	/* check current request
	if it has completed, handle it */
	if (lookup && lookup->state) {
		/* Wait for the  dns lookup thread to complete, and get the result. */
		pthread_join (thread, NULL);


		(*lookup->callback)(lookup);
		//if (lookup->state == dns_found)
		//	printf("%s\n\n", inet_ntoa (lookup->client_addr.sin_addr));
		//else printf("\n%s not found.\n\n", lookup->hostname);

		free(lookup->hostname);
		free(lookup);

		lookup = NULL;
	}


	/* if no request is being processed
	check buffer for another request to start processing */

	if(!lookup && dnshead){
		lookup = dnshead;
		dnshead=lookup->next;

		if (!dnshead)
			dnstail = NULL;

//		printf("Started dns lookup for %s\n", lookup->hostname);

		/* Start the lookup thread */
		pthread_create (&thread, NULL, &mylookupdns, lookup);
	}

}

char * mystrdup (char * val){
	int length;
	char * ret_val = NULL;
	if (val == NULL)
		return NULL;
	length = strlen (val);
	ret_val = malloc (length+1);
	if (!ret_val)
		return NULL;
	strncpy(ret_val, val, length+1);
	return ret_val;
}


dns
DnsAddLookup(char * hostname, dnscallback callback, NodeObj data ){

	dns temp = malloc (sizeof(dns_entry));

	if (!temp  || !callback )
		return NULL;

	temp->state = 0;
	temp->hostname = mystrdup(hostname);
	temp->next = NULL;
	temp->callback = callback;
	temp->data = data;

	if (!dnstail) {
		dnshead = dnstail = temp;
	} else {
		dnstail->next = temp;
		dnstail = temp;
	}

	return temp;
}

void
DnsDelLookup(dns entry){

	dns current = dnshead;
	dns previous = dnshead;

	/* return if entry is null */
	if (!entry )
		return;

	if (lookup == entry) {
		/* the item being cancelled is being looked up right now */

		int res = 0;

		//lookup->state = dns_notfound;

		//res = pthread_cancel (thread);

		if (res != 0) {
//			printf("Thread cancelation failed");
		}

	}

	/* return if the lookup list is empty */
	if (!dnshead)
		return;

	while (current) {

		if (current == entry)
			break;

		previous = current;
		current = current->next;
	}

	if (current) {
		/* found an item */

		/* detach the item from the list */
		if (current == dnshead ) {
			/* found item is at the head of the list */
			dnshead = current->next;

			/* if that was the only item in the list, mark the tail too */
			if (!dnshead)
				dnstail = NULL;

		} else if (current == dnstail) {
			/*list is more than one item long, 
			  removing item on the end */
			dnstail = previous;
			dnstail->next = NULL;
			
		} else {
			/* removing an item in the middle */
			previous->next = current->next;
		}

		/* free the memory for the item */
		current->next = NULL;
		free(current->hostname);
		free(current);
		current = NULL;
	}
	
	return;
}


NodeObj
DnsGetData(dns entry){

	return entry->data;
}


int
DnsGetState(dns entry){

	return entry->state;
}


char *
DnsGetHostName(dns entry){

	return entry->hostname;
}


char *
DnsGetIPAddr(dns entry){

	return inet_ntoa (entry->client_addr.sin_addr);
}


struct sockaddr_in 
DnsGetSocketInfo(dns entry){

	return entry->client_addr;
}


/* Testing stuff below this point */

void
PrintStatus(dns entry){

	if (DnsGetState(entry) == dns_found)
		printf("\n%s found at %s\n\n", DnsGetHostName(entry), DnsGetIPAddr(entry));
	else printf("\n%s not found.\n\n", DnsGetHostName(entry));

}

dns
TestDnsAddLookup(char * hostname, dnscallback callback, NodeObj data ){

	printf("Adding lookup for %s\n", hostname);
	return DnsAddLookup(hostname, callback, data );
}


void
TestDnsDelLookup(dns entry){

	printf("Removing lookup for %s\n", entry->hostname);
	DnsDelLookup(entry);
}

void
DnsTest ()
{
	dns entry;

	/* test of adding and removing single entry from list */
	TestDnsDelLookup ( TestDnsAddLookup ( "www.google1.com", &PrintStatus, NULL ) );

	/* test of adding and removing single entry from list if it is being looked up */
	entry = TestDnsAddLookup ( "www.google2.com", &PrintStatus, NULL );
	DnsProcess();
	TestDnsDelLookup(entry);
	
	/* test of adding and removing last of two entries from list */
	entry = TestDnsAddLookup ( "localhost1", &PrintStatus, NULL );
	TestDnsDelLookup ( TestDnsAddLookup ( "localhost2", &PrintStatus, NULL ) );

	/*test of removing the first element from a list of two items */
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsDelLookup(entry);
	
	/* test of removing element from middle of list */
	entry = TestDnsAddLookup ( "localhost3", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsDelLookup(entry);

	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );

	/* test of removing item from end of list */
	TestDnsDelLookup ( TestDnsAddLookup ( "www.google3.com", &PrintStatus, NULL ) );

	TestDnsAddLookup ( "www.google.com", &PrintStatus, NULL );
	entry = TestDnsAddLookup ( "localhost4", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );

	/* testing removing item from middle of bigger list */
	TestDnsDelLookup(entry);

	/* process entries until they are all finished */
	while (dnshead || lookup ) {
		DnsProcess();
		printf(".");
		usleep(1000);
		fflush(stdout);
	}

	printf("\nFinished DNS test.\n");
}
 
#ifdef TESTBUILD
int main (){

DnsTest();

return 0;
}

#endif

/*****************************************************************/
/* The object half. Everything above is the engine - the queue,   */
/* the worker, and DnsProcess to collect answers as they are      */
/* found. What follows wraps it the way udp.c wraps a socket:     */
/* ONE message function switching on the ids in dns.h, its state  */
/* in its own struct, and its replies going to the {owner,        */
/* callback} it was handed at creation.                           */
/*                                                                */
/* dns.h is the whole interface. There are no properties to write */
/* and none to read.                                              */
/*****************************************************************/

#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "dns.h"

#define POLL_MS 20

typedef struct Pending
{
	dns             handle;
	char            host[MAX_HOST_SIZE];
	struct Pending *next;
} Pending;

typedef struct InstanceData
{
	/* the reference's {owner, msgID} from New(class, msgID, owner): who to
	   tell, the id THEY chose for these answers, and the port on them the
	   message lands on */
	NodeObj owner;
	MsgId   msgID;
	char    callback[64];

	TaskObj task;
	int     scheduled;		/* the pump is armed - arming a linked task
							   corrupts the scheduler's list */
	int     outstanding;	/* answers still owed to us */

	Pending *pending;		/* what we asked for, so it can be cancelled by
							   name and dropped if we are deleted */

	/* the answer being delivered right now - what DNS_HOSTNAME_VAR and
	   DNS_IPADDR_VAR read, so a caller that asked for several names knows
	   which one this is */
	char host[MAX_HOST_SIZE];
	char addr[64];
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static int Dns_Poll(NodeObj instance, NodeObj taskdata, int reason);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;
	return rtrn_handled;
}

/* Which instances are alive. An answer arrives long after it was asked for,
   and the asker may be gone by then - the engine would hand its callback a
   freed node. Checked here rather than guarded in the engine, because the
   engine is not ours to change. */
typedef struct Live { NodeObj instance; struct Live *next; } Live;
static Live *LiveRing;

static void Dns_Born(NodeObj instance)
{
	Live *l = malloc(sizeof(Live));

	if (!l)
		return;
	l->instance = instance;
	l->next = LiveRing;
	LiveRing = l;
}

static void Dns_Died(NodeObj instance)
{
	Live *cur = LiveRing, *prev = NULL;

	while (cur)
	{
		if (cur->instance == instance)
		{
			if (prev) prev->next = cur->next;
			else      LiveRing = cur->next;
			free(cur);
			return;
		}
		prev = cur;
		cur = cur->next;
	}
}

static int Dns_IsLive(NodeObj instance)
{
	Live *cur;

	for (cur = LiveRing; cur; cur = cur->next)
		if (cur->instance == instance)
			return 1;
	return 0;
}

/* remember what we asked for */
static void Dns_Remember(InstanceData *local, dns handle, char *host)
{
	Pending *p = malloc(sizeof(Pending));

	if (!p)
		return;
	p->handle = handle;
	snprintf(p->host, sizeof(p->host), "%s", host ? host : "");
	p->next = local->pending;
	local->pending = p;
}

/* and forget it - 1 if it was still ours, 0 if it was cancelled meanwhile,
   which is what makes a cancelled answer land nowhere */
static int Dns_Forget(InstanceData *local, dns handle)
{
	Pending *cur = local->pending, *prev = NULL;

	while (cur)
	{
		if (cur->handle == handle)
		{
			if (prev) prev->next = cur->next;
			else      local->pending = cur->next;
			free(cur);
			return 1;
		}
		prev = cur;
		cur = cur->next;
	}
	return 0;
}

/* the engine calling back, on the main thread, from DnsProcess */
static void Dns_Answer(dns entry)
{
	NodeObj instance = DnsGetData(entry);
	InstanceData *local;
	NodeObj chunk;
	char *name;

	if (!instance || !Dns_IsLive(instance))
		return;				/* whoever asked is gone; this answer is nobody's */

	local = (InstanceData *) GetPropLong(instance, "local");
	if (!local)
		return;

	if (local->outstanding > 0)
		local->outstanding--;

	if (!Dns_Forget(local, entry))
		return;				/* cancelled while it was in flight */

	name = DnsGetHostName(entry);
	snprintf(local->host, sizeof(local->host), "%s", name ? name : "");

	if (DnsGetState(entry) == dns_found)
		snprintf(local->addr, sizeof(local->addr), "%s", DnsGetIPAddr(entry));
	else
		local->addr[0] = '\0';

	if (local->owner && local->callback[0])
	{
		chunk = NewNode(STRING);
		SetName(chunk, "Address");
		SetValueStr(chunk, local->addr);
		/* the owner's own id, not one of ours */
		DeliverMsg(local->owner, local->callback, local->msgID, chunk);
		DelNode(chunk);		/* DeliverMsg is synchronous: ours to free */
	}
}

/* the pump. Armed when something is asked for, re-armed while answers are
   owed, and NOT re-armed once they have all arrived - an engine nobody is
   waiting on schedules nothing and holds no program open. */
static int Dns_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	(void) taskdata;

	if (reason == task_deactivate)
		return rtrn_handled;
	if (!local)
		return rtrn_dropped;

	local->scheduled = 0;

	DnsProcess();			/* collects a finished lookup, calls Dns_Answer,
							   starts the next one waiting */

	if (local->outstanding > 0)
	{
		AddTaskMilli(local->task, POLL_MS, (FuncPtr)Dns_Poll, msg_send, instance);
		local->scheduled = 1;
	}

	return rtrn_handled;
}

static void Dns_Arm(NodeObj instance, InstanceData *local)
{
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	if (!local->scheduled)
	{
		AddTaskMilli(local->task, POLL_MS, (FuncPtr)Dns_Poll, msg_send, instance);
		local->scheduled = 1;
	}
}

/* DNS_LOOKUP_MSG - the name is the data node's value */
static int Dns_Lookup(NodeObj instance, InstanceData *local, NodeObj data)
{
	char *host;
	dns   handle;

	if (!data)
		return rtrn_dropped;

	host = GetValueStr(data);
	if (!host || !host[0])
		return rtrn_dropped;

	handle = DnsAddLookup(host, &Dns_Answer, instance);
	if (!handle)
	{
		DebugPrint("DNS could not queue a lookup.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	Dns_Remember(local, handle, host);
	local->outstanding++;
	Dns_Arm(instance, local);

	return rtrn_handled;
}

/* DNS_CANCEL_MSG - by name, because a name is what the caller has. One
   already running cannot be stopped; forgetting it is what makes its answer
   land nowhere. */
static int Dns_Cancel(NodeObj instance, InstanceData *local, NodeObj data)
{
	Pending *cur, *next;
	char *host;

	(void) instance;

	if (!data)
		return rtrn_dropped;

	host = GetValueStr(data);
	if (!host || !host[0])
		return rtrn_dropped;

	for (cur = local->pending; cur; cur = next)
	{
		next = cur->next;
		if (strcmp(cur->host, host) != 0)
			continue;

		DnsDelLookup(cur->handle);
		Dns_Forget(local, cur->handle);
		if (local->outstanding > 0)
			local->outstanding--;
	}

	return rtrn_handled;
}

/* a var id: an empty node is FILLED IN with the answer being delivered.
   These are facts about an answer, so there is nothing to set. */
static int Dns_Variable(NodeObj instance, InstanceData *local, MsgId var, NodeObj data)
{
	(void) instance;

	if (!data)
		return rtrn_dropped;

	switch (var)
	{
	case DNS_HOSTNAME_VAR:
		SetValueStr(data, local->host);
		return rtrn_handled;

	case DNS_IPADDR_VAR:
		SetValueStr(data, local->addr);
		return rtrn_handled;

	default:
		return rtrn_dropped;
	}
}

/* THE message function, switching on the ids in dns.h. This is the object's
   only entrance. */
int Dns_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *) GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	switch (message)
	{
	case DNS_LOOKUP_MSG:
		return Dns_Lookup(instance, local, data);

	case DNS_CANCEL_MSG:
		return Dns_Cancel(instance, local, data);

	case DNS_HOSTNAME_VAR:
	case DNS_IPADDR_VAR:
		return Dns_Variable(instance, local, message, data);

	default:
		return rtrn_dropped;
	}
}

/* `data` is the reference's New(class, msgID, owner): the creator's own node
   and the name of the port on it that answers should arrive at. Without it
   the object still resolves - it simply has nobody to tell. */
int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData * local = malloc(sizeof(InstanceData));
	char * cb;

	(void) message;

	if (!local)
		return rtrn_dropped;

	memset(local, 0, sizeof(InstanceData));

	if (data)
	{
		local->owner = (NodeObj) GetPropLong(data, "Owner");
		local->msgID = (MsgId) GetPropLong(data, "MsgId");
		cb = GetPropStr(data, "Callback");
		if (cb)
			strncpy(local->callback, cb, sizeof(local->callback) - 1);
	}

	instance = NewNode(INTEGER);
	SetName(instance, "DNS");
	/* the node name is not the Name PROPERTY - PathOfInstance reads the
	   property, and without it every registry walk logs an error and dumps
	   this node. A private handle still has a name; it just has no path. */
	SetPropStr(instance, "Name", "DNS");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);

	/* the object's one entrance: every id in dns.h arrives here */
	SetPropStr(instance, "Msg", "");
	port = GetPropNode(instance, "Msg");
	SetPropLong(port, "OnMsg", (long)Dns_MessageFunc);

	Dns_Born(instance);
	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *) GetPropLong(instance, "local");
	Pending *cur, *next;

	(void) message; (void) data;

	Dns_Died(instance);

	if (local)
	{
		/* queued lookups go; one already running answers into Dns_Answer,
		   which will find this instance is no longer live */
		for (cur = local->pending; cur; cur = next)
		{
			next = cur->next;
			DnsDelLookup(cur->handle);
			free(cur);
		}

		if (local->task)
			DeleteTask(local->task);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "DNS");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	/* what this class IS, and at what version - a dependent asking for
	   DNS 1 0 gets checked against these two */
	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	/* nothing is published: the interface is dns.h, not a set of properties */

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

	SetName(temp, "DNS");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "6c1e8a94-72d3-4bd7-9a05-1f3e6c840b27");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	/* a resolver is a plain Object: no name, no path, no position, never
	   serialized - so the core's Object class is the whole dependency */
	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
