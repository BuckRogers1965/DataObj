#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "data.h"
#include "DebugPrint.h"

/*

Implement new data type.
Manage memory for the data type.

Implement a property list for the object that allows multiple properties, make these properties be the same data objects as the data object.  So that way properties can have properties.


Allow functions to be called if a node is changed.

*/

/*

Tasks needed done:

O  add to xml and from xml conversions to allow node trees to be imported and exported

O

*/

typedef struct node *NodeObj;

struct node
{
	int type;
	DataObj name;
	DataObj value;
	NodeObj props;
	NodeObj parent;
	NodeObj class;
	NodeObj child;
	NodeObj nextSib;

	/* symlink: when set, this node stands for another node - an alias's  */
	/* control property links to the original's, so value, subscribers,   */
	/* and wiring all live in exactly one place. Non-owning pointer;      */
	/* resolution happens at the port-resolution choke points (object.c), */
	/* and DeleteInstance scrubs links aimed at a dying instance.          */
	NodeObj link;
} node;

#include "callback.h"

void PrintNodePrivate(NodeObj node, int depth)
{

	NodeObj current = node;
	int i;

	while (current)
	{

		// Print indentation based on the depth of the node
		for (i = 0; i < depth; i++)
			printf("  "); // Two spaces for each level of depth

		// Print the node's name and value
		printf("%s: %s\n", GetStr(current->name), GetStr(current->value));

		// Recursively print properties
		if (current->props)
			PrintNodePrivate(current->props, depth + 1);

		// Recursively print children, increasing the depth
		if (current->child)
			PrintNodePrivate(current->child, depth + 1);

		// Move to the next sibling
		current = current->nextSib;
	}
}

void PrintNodePrivateold(NodeObj node, int depth)
{

	NodeObj current = node;
	int i;

	//	printf("depth=%d\n", depth);

	//	printf("%s %s\n", GetStr(node->name), GetStr(node->value));

	//	node = node->nextSib;
	while (current)
	{

		for (i = 0; i < depth; i++)
			printf(" ");

		printf("%s %s\n", GetStr(current->name), GetStr(current->value));

		if (current->props)
			PrintNodePrivate(current->props, depth);

		if (current->child)
			PrintNodePrivate(current->child, depth + 1);

		current = current->nextSib;
	}

	printf("\n");
}

void PrintNode(NodeObj node)
{

	PrintNodePrivate(node, 0);
}


/* allocation accounting: every NewNode counts up, every freed struct     */
/* counts down - a count that grows and never shrinks across a create/    */
/* destroy cycle IS a leak, named by its type. The counter is a plain     */
/* static (never a node property - counting the counter would recurse);   */
/* publishing it into the tree is an object's job (objects/stats), the    */
/* same mechanism/behavior split as everything else in the core.          */
static long nodesAlive = 0;

long NodeCount(void)
{
	return nodesAlive;
}

void DelNode(NodeObj node)
{

	if (node == NULL)
		return;

	DelNode(node->nextSib);
	node->nextSib = NULL;

	DelNode(node->props);
	node->props = NULL;

	DelNode(node->child);
	node->child = NULL;

	if (node->parent && node->parent->child == node)
		node->parent = NULL;

	/* NewNode allocates both of these; nothing else ever frees them -  */
	/* every node deleted without this leaked two DataObj structs       */
	DelData(node->name);
	DelData(node->value);

	nodesAlive--;
	free(node);
}

NodeObj
NewNode(int type)
{
	NodeObj temp = malloc(sizeof(node));
	if (temp)
	{
		nodesAlive++;
		temp->name = NewData(STRING);
		temp->value = NewData(type);
		temp->props = NULL;
		temp->parent = NULL;
		temp->child = NULL;
		temp->nextSib = NULL;
		temp->link = NULL;
	}
	return temp;
}

/* make node a symlink standing for target (NULL unlinks). The link is  */
/* non-owning: deleting the link node never touches the target, and     */
/* whoever deletes the target is responsible for scrubbing links aimed  */
/* at it (ScrubLinks, object.c)                                          */
void LinkNode(NodeObj node, NodeObj target)
{
	if (!node)
		return;

	node->link = target;
}

NodeObj GetNodeLink(NodeObj node)
{
	if (!node)
		return NULL;

	return node->link;
}

/* follow a link chain to the node it ultimately stands for - depth-     */
/* capped so a cycle degrades into "stops resolving" instead of hanging  */
/* the fabric. A node with no link resolves to itself.                    */
NodeObj ResolveNode(NodeObj node)
{
	int depth = 8;

	while (node && node->link && depth--)
		node = node->link;

	return node;
}

void SetName(NodeObj node, char *name)
{

	if (!node || !name)
		return;

	SetStr(node->name, name);
}

int CmpName(NodeObj node, char *name)
{

	int length;

	if (!node || !name)
		return 0;

	length = strlen(name) + 1;

	return (strncmp(GetStr(node->name), name, length) == 0);
}

void SetValueStr(NodeObj node, char *value)
{

	if (!node || !value)
		return;

	SetStr(node->value, value);
}

/* binary-safe: copies exactly `length` bytes, embedded NULs included */
void SetValueStrLen(NodeObj node, char *value, int length)
{

	if (!node || !value)
		return;

	SetStrLen(node->value, value, length);
}

int GetValueLen(NodeObj node)
{

	if (!node)
		return 0;

	return GetStrLen(node->value);
}

void SetValueInt(NodeObj node, int value)
{

	/* ZERO IS A VALUE. This used to read `if (!node || !value)`, so storing 0
	   was silently a no-op and anything that turned something OFF through it
	   simply did not happen - an LED that could be lit and never darkened, an
	   Enable that could not go false. Only the node is checked now. */
	if (!node)
		return;

	SetInt(node->value, value);
}

void SetValueLong(NodeObj node, long value)
{

	/* zero is a value - see SetValueInt */
	if (!node)
		return;

	SetLong(node->value, value);
}

char *
GetValueStr(NodeObj node)
{

	if (!node)
		return NULL;

	return GetStr(node->value);
}

char *
GetNameStr(NodeObj node)
{

	if (!node)
		return NULL;

	return GetStr(node->name);
}

int GetValueInt(NodeObj node)
{

	if (!node)
		return 0;

	return GetInt(node->value);
}

long GetValueLong(NodeObj node)
{

	if (!node)
		return 0;

	return GetLong(node->value);
}

DataObj
GetValueNode(NodeObj node)
{
	if (!node)
		return 0;

	return node->value;
}

void SetParent(NodeObj parent, NodeObj child)
{
	if (!parent || !child)
		return;
	child->parent = parent;
}

void SetChild(NodeObj node, NodeObj child)
{
	if (!node || !child)
		return;
	DelNode(node->child);
	node->child = child;
	child->parent = node;
}

NodeObj GetChild(NodeObj node){
	return node->child;
}

NodeObj GetParent(NodeObj node){
	if (!node)
		return NULL;
	return node->parent;
}

/* unlink sib from its parent's CHILD chain, relinking around it - does   */
/* NOT free it. Singly-linked (nextSib only, no "previous" pointer), so   */
/* removing anything but the head means walking from the head to find     */
/* whoever points at sib. Leaves sib's own nextSib/parent cleared so a    */
/* caller can safely DelNode() it afterward without cascading into what   */
/* used to be its later siblings - DelNode(node->nextSib) deletes a whole */
/* chain, which is correct for tearing down a subtree wholesale but wrong */
/* for removing one node out of the middle of one. Child-chain only: a    */
/* property's nextSib chain (AddProp) never sets ->parent, so this can't  */
/* (yet) unlink a single property the same way - not needed for instance  */
/* removal, the one thing that currently calls this.                      */
void DelSibling(NodeObj sib)
{
	NodeObj parent, prev;

	if (!sib)
		return;

	parent = sib->parent;
	if (!parent)
		return;

	if (parent->child == sib)
	{
		parent->child = sib->nextSib;
	}
	else
	{
		prev = parent->child;
		while (prev && prev->nextSib != sib)
			prev = prev->nextSib;
		if (prev)
			prev->nextSib = sib->nextSib;
	}

	sib->nextSib = NULL;
	sib->parent = NULL;
}

void AddProp(NodeObj node, NodeObj prop)
{
	if (!node || !prop)
		return;
	if (node->props)
	{
		NodeObj temp = node->props;
		node->props = prop;
		prop->nextSib = temp;
	}
	else
	{
		node->props = prop;
	}
}

/* unlink prop from owner's property chain, relinking around it - does   */
/* NOT free it. Properties don't carry a parent back-pointer the way      */
/* children do (AddProp never sets ->parent), so unlike DelSibling this   */
/* needs the owner passed in explicitly. Leaves prop's own nextSib        */
/* cleared so a caller can safely DelNode() it afterward without          */
/* cascading into whatever property used to follow it in the chain.       */
void RemoveProp(NodeObj owner, NodeObj prop)
{
	NodeObj current;

	if (!owner || !prop)
		return;

	if (owner->props == prop)
	{
		owner->props = prop->nextSib;
	}
	else
	{
		current = owner->props;
		while (current && current->nextSib != prop)
			current = current->nextSib;
		if (current)
			current->nextSib = prop->nextSib;
	}

	prop->nextSib = NULL;
}

void AddSibling(NodeObj node, NodeObj sib)
{
	if (!node || !sib)
		return;

	NodeObj temp = node->nextSib;
	node->nextSib = sib;
	sib->nextSib = temp;
	/*
		sib->nextSib=node->nextSib;
		if (node->nextSib){
			node->nextSib->prevSib=sib;
		}
		node->nextSib=sib;
		sib->prevSib=node;
		sib->parent=node->parent;
		*/
}
NodeObj GetNextSibling(NodeObj node){
	return node->nextSib;
}

void AddChild(NodeObj parent, NodeObj child)
{
	if (!parent || !child)
		return;

	if (parent->child)
		AddSibling(parent->child, child);
	else
		parent->child = child;

	child->parent = parent;
}

/* AddChild inserts right after the head (see AddSibling), which scrambles */
/* order - fine for the registry, wrong for anything whose children must   */
/* replay in the sequence they were added, like a flow script.             */
void AppendChild(NodeObj parent, NodeObj child)
{
	NodeObj last;

	if (!parent || !child)
		return;

	if (!parent->child)
	{
		parent->child = child;
	}
	else
	{
		last = parent->child;
		while (last->nextSib)
			last = last->nextSib;
		last->nextSib = child;
	}

	child->parent = parent;
}

NodeObj
GetPropNode(NodeObj node, char *name)
{
#ifndef S_SPLINT_S

	if (!node || !name)
		return NULL;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return current;
		current = current->nextSib;
	}
#endif
	return NULL;
}

long GetPropLong(NodeObj node, char *name)
{
#ifndef S_SPLINT_S

	if (!node || !name)
		return 0;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return GetValueLong(current);
		current = current->nextSib;
	}
#endif
	return 0;
}

int GetPropInt(NodeObj node, char *name)
{
	if (!node || !name)
		return 0;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return GetValueInt(current);
		current = current->nextSib;
	}
	return 0;
}

char *GetPropStr(NodeObj node, char *name)
{
	if (!node || !name)
		return NULL;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return GetValueStr(current);
		current = current->nextSib;
	}
	return NULL;
}

/* returns the first property of a node so callers can iterate */
/* the property list, walk the rest with GetNextSibling         */
NodeObj GetNextProp(NodeObj node)
{
	if (!node)
		return NULL;
	return node->props;
}

/* EVERY HANDLER ON A PROPERTY, newest first, then the property's own OnMsg
   as the last link.

   A property can carry "Handler" records - the same species as a Subscriber,
   a node holding a function pointer - and they are consulted before the
   OnMsg the class installed. AddProp prepends, so the last one installed
   runs first and wraps the ones before it.

   rtrn_unhandled means "not mine": the walk continues to the next record and
   finally to OnMsg. So reaching the original handler is not a call anybody
   makes, it is what declining does. Any other verdict stops the walk and is
   the answer.

   `found` reports whether there was anything at all to call, which is what
   the callers used to learn from OnMsg being non-NULL. With no records
   installed this is exactly the old behaviour: one lookup, one call.

   NOTE for the chain work: a Subscriber record carrying its own Callback is
   still called directly by DeliverToSubscriber below, ahead of this - so a
   handler installed on a property is not yet seen by wires made before it.
   That is deliberate for now; changing it changes behaviour, and this step
   changes none. */
int DeliverToHandlers(NodeObj owner, NodeObj propNode, int message, NodeObj data, int *found)
{
	int (*fn)(NodeObj, int, NodeObj);
	NodeObj rec;
	int verdict;

	if (found)
		*found = 0;
	if (!propNode)
		return rtrn_unhandled;

	for (rec = GetNextProp(propNode); rec; rec = GetNextSibling(rec))
	{
		if (!CmpName(rec, "Handler"))
			continue;

		fn = (int (*)(NodeObj, int, NodeObj)) GetPropLong(rec, "OnMsg");
		if (!fn)
			continue;

		if (found)
			*found = 1;
		verdict = fn(owner, message, data);
		if (verdict != rtrn_unhandled)
			return verdict;
	}

	fn = (int (*)(NodeObj, int, NodeObj)) GetPropLong(propNode, "OnMsg");
	if (fn)
	{
		if (found)
			*found = 1;
		return fn(owner, message, data);
	}

	return rtrn_unhandled;
}

/* is there anything to deliver to - a record or the property's own OnMsg.
   This is what the old `GetPropLong(prop, "OnMsg")` existence tests meant. */
int HasHandler(NodeObj propNode)
{
	NodeObj rec;

	if (!propNode)
		return 0;
	if (GetPropLong(propNode, "OnMsg"))
		return 1;

	for (rec = GetNextProp(propNode); rec; rec = GetNextSibling(rec))
		if (CmpName(rec, "Handler") && GetPropLong(rec, "OnMsg"))
			return 1;

	return 0;
}

/* deliver one message to one Subscriber record - the single definition   */
/* of what a subscription MEANS, shared by both fan-out walkers            */
/* (FanOutSubscribers below for synchronous property writes, DispatchMsg  */
/* in object.c for queued port sends). A record carries {Instance, Port,  */
/* Callback}: with a Callback it is a wire into a compiled handler (or a  */
/* Bridge tap) and the handler is called as ever; with no Callback it is  */
/* a wire into a plain property, and the universal default applies -      */
/* store what arrived (write the payload onto Instance's Port, whose own  */
/* write then fans out in turn, so chains hop onward). This default is    */
/* what lets Connect() reach ANY property with no adapter standing in     */
/* between - the Subscriber names the real sink, so everything that walks */
/* the graph (list-connections, CloneConnections, the delete scrub) sees  */
/* the user's actual wire.                                                 */
/* defined below - default delivery recurses into it (SetPropStr's own    */
/* fan-out is what makes chained property wires hop onward)                */
void SetPropStr(NodeObj node, char *name, char *value);

/* WHERE THE CURRENT DELIVERY CAME FROM.

   The dispatcher knows the source - FanOutSubscribers has the property that
   changed, DispatchMsg has the envelope's outPort - and used to drop it one
   line before calling the handler. A handler could then only learn its source
   by owning a separate sink object per wire, which is exactly why the web
   bridge grew a "tap" node per subscription.

   Set here, around the call, so everything underneath inherits it: the
   handler, and anything the handler sends onward. Saved and restored rather
   than assigned, because property fan-out is SYNCHRONOUS - a handler that
   writes a property nests another delivery inside the one already running.
   Single-threaded fabric, so a stack discipline is enough. */
static NodeObj curFromNode = NULL;

NodeObj MsgFromNode(void)
{
	return curFromNode;
}

/* object.c, same library - declared here rather than including object.h,
   which redeclares the node types this file defines */
int PuntToClass(NodeObj instance, int message, NodeObj data);

void DeliverToSubscriber(NodeObj sub, int message, NodeObj data, NodeObj fromNode)
{
	NodeObj toInstance, portnode, chunk;
	NodeObj prevFrom;
	int (*callback)(NodeObj, int, NodeObj);
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	char *port, *value;

	callback   = (int (*)(NodeObj, int, NodeObj)) GetPropLong(sub, "Callback");
	toInstance = (NodeObj) GetPropLong(sub, "Instance");

	prevFrom    = curFromNode;
	curFromNode = fromNode;

	if (callback)
	{
		callback(toInstance, message, data);
		curFromNode = prevFrom;
		return;
	}

	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	port  = GetPropStr(sub, "Port");
	value = data ? GetValueStr(data) : NULL;
	if (!toInstance || !port || !value)
	{
		curFromNode = prevFrom;
		return;
	}

	/* a handler that appeared on the port after the wire was made still  */
	/* wins - deliver to it the way genuine port traffic arrives (the     */
	/* same distinction SetOrDeliverProp draws, natively so node.c never  */
	/* calls up into object.c)                                            */
	portnode = GetPropNode(toInstance, port);
	if (HasHandler(portnode))
	{
		/* THE VERDICT SAYS WHETHER THE VALUE STILL NEEDS STORING, and the
		   three existing codes already say it exactly:

		     rtrn_handled    the handler took it, and a control that takes a
		                     value stores it (one SetPropStr, which is what a
		                     control IS) - storing here too would write the
		                     same value twice for one arrival, and every write
		                     fans out, so every subscriber would hear it twice
		     rtrn_propagate  the handler watched it and did not consume it (a
		                     probe), so the universal default applies and the
		                     value lands on the property, whose write fans on
		     rtrn_dropped    nothing took it - a disabled control refuses the
		                     value rather than displaying it

		   So the handler runs first and answers, rather than being told. */
		int verdict, found;

		chunk = NewNode(STRING);
		SetName(chunk, port);
		SetValueStr(chunk, value);
		verdict = DeliverToHandlers(toInstance, portnode, message, chunk, &found);

		/* refused by the instance: offer it up the class chain before
		   giving up on it. rtrn_dropped means "not mine", and until this
		   existed that was the end of the road - see PuntToClass. */
		if (verdict == rtrn_dropped)
			verdict = PuntToClass(toInstance, message, chunk);

		DelNode(chunk);

		if (verdict == rtrn_propagate)
			SetPropStr(toInstance, port, value);

		curFromNode = prevFrom;
		return;
	}

	/* NO HANDLER ON THE INSTANCE IS ALSO "not mine". The instance has no
	   opinion about this property, so the class chain gets its turn before
	   the universal default applies - that is what lets a class answer for
	   a property none of its instances declared, which is most of what a
	   class is for. Handled means the chain took it; anything else falls
	   through to the store, same as it always did. */
	{
		int verdict;

		chunk = NewNode(STRING);
		SetName(chunk, port);
		SetValueStr(chunk, value);
		verdict = PuntToClass(toInstance, message, chunk);
		DelNode(chunk);

		if (verdict == rtrn_handled)
		{
			curFromNode = prevFrom;
			return;
		}
	}

	SetPropStr(toInstance, port, value);
	curFromNode = prevFrom;
}

/* object.c, same library. Declared here rather than including object.h,  */
/* which redeclares the node types this file defines.                     */
int   SndMsgNode(NodeObj instance, NodeObj outPort, int message, NodeObj data);
void *ObjGetTaskList(void);

/* WRITING A PROPERTY THAT POINTS SOMEWHERE ELSE.

   A control points its Value at the object's property, and an object writes
   its own properties with SetProp* - Button_Activate writing "1" onto its
   Value, say. Those two facts meet here: without this, such a write lands on
   the link slot and the data it points at never hears about it, so a button
   press reaches nothing. Everything that goes through object.c (Connect,
   SndMsg, SetOrDeliverProp) has always resolved; this is the same rule for
   the plain setters, so it holds no matter who does the writing.

   A property that owns its value resolves to itself, so this is a no-op for
   everything that does not point anywhere. The owner comes back too, because
   the fan-out envelope names the instance the property belongs to. */
static NodeObj ResolveOwned(NodeObj *ownerp, NodeObj propnode)
{
	NodeObj link;

	if (!propnode || !GetNodeLink(propnode))
		return propnode;

	link = (NodeObj) GetPropLong(propnode, "LinkInst");
	if (link && ownerp)
		*ownerp = link;

	return ResolveNode(propnode);
}

/* every property write fans out, unconditionally, to whatever has       */
/* subscribed to it - Connect() (object.c) already leaves "Subscriber"   */
/* children on whatever node it targets, port or plain property alike    */
/* (see AddSubscription/SndMsg). There is no opt-in step - a property is */
/* watchable simply by existing, same as a port.                          */
/*                                                                        */
/* The delivery is QUEUED, not done here: SndMsgNode envelopes the write  */
/* onto the scheduler and DispatchMsg walks the Subscriber list later,    */
/* from ExecTasks. A write costs one task insert and returns, so no       */
/* subscriber's handler ever runs inside the setter's own call stack and  */
/* a handler runs to completion before anything it causes is dispatched.  */
/*                                                                        */
/* The payload is a copy: DispatchMsg frees the envelope's data after the */
/* last subscriber, and propnode belongs to owner. The copy carries the   */
/* property's name and value, the shape port traffic already arrives in.  */
static void FanOutSubscribers(NodeObj owner, NodeObj propnode)
{
	NodeObj sub, chunk;

	/* main.c installs the task list after the core has already written  */
	/* properties; nothing is subscribed that early                       */
	if (!ObjGetTaskList())
		return;

	/* an unwired property costs the walk and no allocation - this fires */
	/* on every write in the system, and most properties are unwired      */
	sub = GetNextProp(propnode);
	while (sub)
	{
		if (CmpName(sub, "Subscriber"))
			break;
		sub = GetNextSibling(sub);
	}
	if (!sub)
		return;

	/* who is about to be told. A SET line in the log with no FANOUT line
	   after it means the value landed on a node nothing is listening to -
	   which is the difference between "the write went to the wrong place"
	   and "the write was right and the subscription is on another node". */
	{
		char dbg[400];
		int  n = 0;
		NodeObj s;

		for (s = GetNextProp(propnode); s; s = GetNextSibling(s))
			if (CmpName(s, "Subscriber"))
				n++;

		snprintf(dbg, sizeof(dbg), "FANOUT '%s'.%s = '%.40s' -> %d subscriber(s)",
				 GetPropStr(owner, "Name") ? GetPropStr(owner, "Name") : "?",
				 GetNameStr(propnode) ? GetNameStr(propnode) : "?",
				 GetValueStr(propnode) ? GetValueStr(propnode) : "",
				 n);
		DebugPrint(dbg, __FILE__, __LINE__, WIRE);
	}

	chunk = NewNode(STRING);
	SetName(chunk, GetNameStr(propnode));
	SetValueStr(chunk, GetValueStr(propnode));
	SndMsgNode(owner, propnode, msg_change, chunk);
}

void SetPropLongPrivate(NodeObj node, char *name, long value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		SetLong(propnode->value, value);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(LONG);
	SetStr(propnode->name, name);
	SetLong(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropLong(NodeObj node, char *name, long value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		propnode = ResolveOwned(&node, propnode);
		SetLong(propnode->value, value);
		FanOutSubscribers(node, propnode);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(LONG);
	SetStr(propnode->name, name);
	SetLong(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropIntPrivate(NodeObj node, char *name, int value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		SetInt(propnode->value, value);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(INTEGER);
	SetStr(propnode->name, name);
	SetInt(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropInt(NodeObj node, char *name, int value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		propnode = ResolveOwned(&node, propnode);
		SetInt(propnode->value, value);
		FanOutSubscribers(node, propnode);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(INTEGER);
	SetStr(propnode->name, name);
	SetInt(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropStrPrivate(NodeObj node, char *name, char * value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		SetStr(propnode->value, value);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(STRING);
	SetStr(propnode->name, name);
	SetStr(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropStr(NodeObj node, char *name, char * value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		/* every write fans out, whether or not the value differs: a     */
		/* write is an event, and a repeated value is a repeated event   */
		/* (a button pressed twice, a clock held at 1). Comparing the    */
		/* payload to decide whether anything happened inspects the one  */
		/* field an event is free to keep constant.                       */
		propnode = ResolveOwned(&node, propnode);
		SetStr(propnode->value, value);
		FanOutSubscribers(node, propnode);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(STRING);
	SetStr(propnode->name, name);
	SetStr(propnode->value, value);
	AddProp(node, propnode);
}

/* ---- serialization: node tree as text (JSON), and back ---- */

int GetValueType(NodeObj node)
{
	if (!node)
		return STRING;
	return GetDataType(node->value);
}

static char *TypeName(int type)
{
	switch (type)
	{
	case INTEGER:
		return "integer";
	case HEX:
		return "hex";
	case REAL:
		return "real";
	case LONG:
		return "long";
	case STRING:
	default:
		return "string";
	}
}

static int TypeFromName(char *name)
{
	if (!name)
		return STRING;
	if (strcmp(name, "integer") == 0)
		return INTEGER;
	if (strcmp(name, "hex") == 0)
		return HEX;
	if (strcmp(name, "real") == 0)
		return REAL;
	if (strcmp(name, "long") == 0)
		return LONG;
	return STRING;
}

typedef struct
{
	char *buf;
	size_t len;
	size_t cap;
} StrBuf;

static void sbInit(StrBuf *sb)
{
	sb->cap = 256;
	sb->len = 0;
	sb->buf = malloc(sb->cap);
	sb->buf[0] = '\0';
}

static void sbAppendN(StrBuf *sb, char *s, size_t n)
{
	if (sb->len + n + 1 > sb->cap)
	{
		while (sb->len + n + 1 > sb->cap)
			sb->cap *= 2;
		sb->buf = realloc(sb->buf, sb->cap);
	}
	memcpy(sb->buf + sb->len, s, n);
	sb->len += n;
	sb->buf[sb->len] = '\0';
}

static void sbAppend(StrBuf *sb, char *s)
{
	sbAppendN(sb, s, strlen(s));
}

static void sbAppendChar(StrBuf *sb, char c)
{
	sbAppendN(sb, &c, 1);
}

static void sbAppendJsonString(StrBuf *sb, char *s)
{
	char hex[8];
	unsigned char c;

	sbAppendChar(sb, '"');
	if (s)
	{
		for (; *s; s++)
		{
			c = (unsigned char)*s;
			switch (c)
			{
			case '"':
				sbAppend(sb, "\\\"");
				break;
			case '\\':
				sbAppend(sb, "\\\\");
				break;
			case '\n':
				sbAppend(sb, "\\n");
				break;
			case '\r':
				sbAppend(sb, "\\r");
				break;
			case '\t':
				sbAppend(sb, "\\t");
				break;
			case '\b':
				sbAppend(sb, "\\b");
				break;
			case '\f':
				sbAppend(sb, "\\f");
				break;
			default:
				if (c < 0x20)
				{
					snprintf(hex, sizeof(hex), "\\u%04x", c);
					sbAppend(sb, hex);
				}
				else
				{
					sbAppendChar(sb, (char)c);
				}
			}
		}
	}
	sbAppendChar(sb, '"');
}

static void EncodeNode(StrBuf *sb, NodeObj n);

static void EncodeList(StrBuf *sb, NodeObj head)
{
	NodeObj current = head;
	int first = 1;

	sbAppendChar(sb, '[');
	while (current)
	{
		if (!first)
			sbAppendChar(sb, ',');
		first = 0;
		EncodeNode(sb, current);
		current = current->nextSib;
	}
	sbAppendChar(sb, ']');
}

static void EncodeNode(StrBuf *sb, NodeObj n)
{
	sbAppendChar(sb, '{');
	sbAppend(sb, "\"name\":");
	sbAppendJsonString(sb, GetStr(n->name));
	sbAppend(sb, ",\"type\":");
	sbAppendJsonString(sb, TypeName(GetDataType(n->value)));
	sbAppend(sb, ",\"value\":");
	sbAppendJsonString(sb, GetStr(n->value));
	sbAppend(sb, ",\"props\":");
	EncodeList(sb, n->props);
	sbAppend(sb, ",\"children\":");
	EncodeList(sb, n->child);
	sbAppendChar(sb, '}');
}

/* node tree -> JSON text; caller frees the returned buffer */
char *NodeToText(NodeObj node)
{
	StrBuf sb;

	if (!node)
		return NULL;

	sbInit(&sb);
	EncodeNode(&sb, node);
	return sb.buf;
}

/*
 * A quoted, escaped JSON string literal (the quotes are included), for
 * code hand-building JSON objects (the Bridge's events, live taps) that
 * would otherwise embed a field with a raw %s and break the moment the
 * value contains a '"' or a newline. Caller frees the returned buffer.
 */
char *JsonEscapeStr(char *s)
{
	StrBuf sb;

	sbInit(&sb);
	sbAppendJsonString(&sb, s);
	return sb.buf;
}

typedef struct
{
	char *p;
} Cursor;

static void SkipWs(Cursor *c)
{
	while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')
		c->p++;
}

static char *ParseJsonString(Cursor *c)
{
	StrBuf sb;
	unsigned int cp;
	int i;
	char h;

	sbInit(&sb);

	if (*c->p != '"')
		return sb.buf;
	c->p++;

	while (*c->p && *c->p != '"')
	{
		if (*c->p == '\\')
		{
			c->p++;
			switch (*c->p)
			{
			case '"':
				sbAppendChar(&sb, '"');
				break;
			case '\\':
				sbAppendChar(&sb, '\\');
				break;
			case '/':
				sbAppendChar(&sb, '/');
				break;
			case 'n':
				sbAppendChar(&sb, '\n');
				break;
			case 'r':
				sbAppendChar(&sb, '\r');
				break;
			case 't':
				sbAppendChar(&sb, '\t');
				break;
			case 'b':
				sbAppendChar(&sb, '\b');
				break;
			case 'f':
				sbAppendChar(&sb, '\f');
				break;
			case 'u':
				cp = 0;
				for (i = 0; i < 4; i++)
				{
					c->p++;
					h = *c->p;
					cp <<= 4;
					if (h >= '0' && h <= '9')
						cp |= (h - '0');
					else if (h >= 'a' && h <= 'f')
						cp |= (h - 'a' + 10);
					else if (h >= 'A' && h <= 'F')
						cp |= (h - 'A' + 10);
				}
				/* BMP-only UTF-8 encode; surrogate pairs not handled */
				if (cp < 0x80)
				{
					sbAppendChar(&sb, (char)cp);
				}
				else if (cp < 0x800)
				{
					sbAppendChar(&sb, (char)(0xC0 | (cp >> 6)));
					sbAppendChar(&sb, (char)(0x80 | (cp & 0x3F)));
				}
				else
				{
					sbAppendChar(&sb, (char)(0xE0 | (cp >> 12)));
					sbAppendChar(&sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
					sbAppendChar(&sb, (char)(0x80 | (cp & 0x3F)));
				}
				break;
			default:
				break;
			}
			c->p++;
		}
		else
		{
			sbAppendChar(&sb, *c->p);
			c->p++;
		}
	}
	if (*c->p == '"')
		c->p++;

	return sb.buf;
}

static NodeObj ParseNodeObject(Cursor *c);

static NodeObj ParseArray(Cursor *c)
{
	NodeObj head = NULL, tail = NULL, n;

	if (*c->p != '[')
		return NULL;
	c->p++;
	SkipWs(c);
	if (*c->p == ']')
	{
		c->p++;
		return NULL;
	}

	while (1)
	{
		SkipWs(c);
		n = ParseNodeObject(c);
		if (n)
		{
			if (tail)
				tail->nextSib = n;
			else
				head = n;
			tail = n;
		}
		SkipWs(c);
		if (*c->p == ',')
		{
			c->p++;
			continue;
		}
		if (*c->p == ']')
		{
			c->p++;
			break;
		}
		break;
	}
	return head;
}

/*
 * Parses exactly what EncodeNode produces: an object with string-valued
 * name/type/value and array-valued props/children. Not a general JSON
 * parser - numbers, booleans and null are not accepted as values.
 */
static NodeObj ParseNodeObject(Cursor *c)
{
	char *name = NULL, *typeName = NULL, *value = NULL, *key;
	NodeObj props = NULL, children = NULL, node, ch;
	int type;

	if (*c->p != '{')
		return NULL;
	c->p++;

	SkipWs(c);
	if (*c->p == '}')
	{
		c->p++;
	}
	else
	{
		while (1)
		{
			SkipWs(c);
			if (*c->p != '"')
				break;
			key = ParseJsonString(c);
			SkipWs(c);
			if (*c->p == ':')
				c->p++;
			SkipWs(c);

			if (strcmp(key, "name") == 0)
				name = ParseJsonString(c);
			else if (strcmp(key, "type") == 0)
				typeName = ParseJsonString(c);
			else if (strcmp(key, "value") == 0)
				value = ParseJsonString(c);
			else if (strcmp(key, "props") == 0)
				props = ParseArray(c);
			else if (strcmp(key, "children") == 0)
				children = ParseArray(c);
			else if (*c->p == '"')
				free(ParseJsonString(c));

			free(key);
			SkipWs(c);
			if (*c->p == ',')
			{
				c->p++;
				continue;
			}
			if (*c->p == '}')
			{
				c->p++;
				break;
			}
			break;
		}
	}

	type = TypeFromName(typeName);
	node = NewNode(type);
	if (name)
		SetName(node, name);
	if (value)
	{
		switch (type)
		{
		case INTEGER:
			SetValueInt(node, atoi(value));
			break;
		case LONG:
			SetValueLong(node, atol(value));
			break;
		case HEX:
			SetHex(node->value, value);
			break;
		case REAL:
			SetReal(node->value, atof(value));
			break;
		case STRING:
		default:
			SetValueStr(node, value);
			break;
		}
	}

	node->props = props;
	node->child = children;
	ch = children;
	while (ch)
	{
		ch->parent = node;
		ch = ch->nextSib;
	}

	free(name);
	free(typeName);
	free(value);

	return node;
}

/* JSON text (as produced by NodeToText) -> a new node tree */
NodeObj TextToNode(char *text)
{
	Cursor c;

	if (!text)
		return NULL;

	c.p = text;
	SkipWs(&c);
	if (*c.p != '{')
		return NULL;

	return ParseNodeObject(&c);
}

/*
 * A flat JSON object - {"key":"value","key2":"value2",...} - parsed into
 * a node whose PROPS are the key/value pairs, all as strings (whatever
 * reads them converts automatically, same convention flow scripts already
 * use for property values). No nesting, no arrays: this is for simple
 * flat commands like the bridge protocol, not general trees - use
 * TextToNode for anything with props/children of its own.
 */
NodeObj TextToProps(char *text)
{
	Cursor c;
	NodeObj node;
	char *key, *value;

	if (!text)
		return NULL;

	c.p = text;
	SkipWs(&c);
	if (*c.p != '{')
		return NULL;
	c.p++;

	node = NewNode(INTEGER);
	SetName(node, "Command");

	SkipWs(&c);
	if (*c.p == '}')
		return node;

	while (1)
	{
		SkipWs(&c);
		if (*c.p != '"')
			break;
		key = ParseJsonString(&c);

		SkipWs(&c);
		if (*c.p == ':')
			c.p++;
		SkipWs(&c);
		value = ParseJsonString(&c);

		SetPropStr(node, key, value);
		free(key);
		free(value);

		SkipWs(&c);
		if (*c.p == ',')
		{
			c.p++;
			continue;
		}
		if (*c.p == '}')
			break;
		break;
	}

	return node;
}
