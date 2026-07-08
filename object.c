#include <stdio.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"
#include "callback.h"

NodeObj RegObjList;

void
ObjSetRegObjList(NodeObj node){
	RegObjList = node;
}

/* The scheduler task list lives in main, but loaded objects need to */
/* schedule work.  This lives here in the shared library so main and */
/* every loaded object see the same list, just like RegObjList.      */

void * ObjTaskList;

void
ObjSetTaskList(void * list){
	ObjTaskList = list;
}

void *
ObjGetTaskList(void){
	return ObjTaskList;
}


loadClasses(){
	NodeObj library = GetChild( RegObjList );
	while (library) {
		msgobj ClassStart = (msgobj)GetPropLong(library, "ClassStart");
		if (ClassStart) ClassStart(library, 0, NULL);
		library = GetNextSibling(library);
	}
}

UnloadClasses(){
	NodeObj library = GetChild( RegObjList );
	while (library) {
		msgobj ClassEnd = (msgobj)GetPropLong(library, "ClassEnd");
		if (ClassEnd) ClassEnd(library, 0, NULL);
		library = GetNextSibling(library);
	}
}



		//printf ("In core:     Class callbacks: %lu, %lu, %lu\n", (long)ClassStart, (long)ClassEnd, (long)ClassMsg);
		//msgobj ClassEnd   = (msgobj)GetPropLong(library, "ClassEnd");
		//msgobj ClassMsg   = (msgobj)GetPropLong(library, "ClassMsg");
		//PrintNode(library);

NodeObj
CreateContainer(NodeObj container, char * name){

        // these containers just exist in our nodes to organize groups of objects together.
        // evolve into an application grouping with a schedule? 

        // these could be functional organizations
        // later we could also have these same objects in multiple views in logical organizations


        // need to check to see if name already exists
	NodeObj temp = NewNode(INTEGER);
	SetName(temp, name);

	AddChild(container, temp);

	return temp;
}

/* walk the registry looking for a registered class by name */
/* the registry is RegObjList -> libraries -> classes        */
NodeObj
FindClass(char * classname){

	NodeObj library = GetChild(RegObjList);
	NodeObj class;

	while (library) {
		class = GetChild(library);
		while (class) {
			if (CmpName(class, classname))
				return class;
			class = GetNextSibling(class);
		}
		library = GetNextSibling(library);
	}
	return NULL;
}

NodeObj
CreateObject(NodeObj container, char * classname){

	NodeObj class;
	msgobj InstanceStart;

	class = FindClass(classname);
	if (!class) {
		DebugPrint ( "CreateObject could not find a registered class by that name.", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	if (!InstanceStart) {
		DebugPrint ( "CreateObject found a class with no InstanceStart.", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	/* the class creates and registers the instance itself, */
	/* RegisterInstance leaves it in LastInstance for us     */
	InstanceStart(class, msg_initialize, NULL);

	/* the instance lives in the registry under its class,      */
	/* the container is not a second parent in the node tree    */

	return (NodeObj)GetPropLong(class, "LastInstance");
}

/* record a subscription on a source port */
/* each Subscriber carries the sink instance and the handler */
/* the sink registered as OnMsg on its input port            */
void AddSubscription(NodeObj fromPort, NodeObj toNode, long handler){

	NodeObj sub = NewNode(INTEGER);
	SetName(sub, "Subscriber");
	SetPropLong(sub, "Instance", (long)toNode);
	SetPropLong(sub, "Callback", handler);
	AddProp(fromPort, sub);
}

int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	NodeObj fromPort, toPort;

	if (!fromNode || !from || !toNode || !to)
		return 0;

	/* make sure the output port exists on the source */
	fromPort = GetPropNode(fromNode, from);
	if (!fromPort) {
		SetPropInt(fromNode, from, 0);
		fromPort = GetPropNode(fromNode, from);
	}

	/* the input port must exist, it carries the sink's handler */
	toPort = GetPropNode(toNode, to);
	if (!toPort) {
		DebugPrint ( "Connect could not find the input port on the sink.", __FILE__, __LINE__, ERROR);
		return 0;
	}

	AddSubscription(fromPort, toNode, GetPropLong(toPort, "OnMsg"));

	return 1;
}

/* route one message out a port to every subscriber of that port */
int
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data){

	NodeObj outPort, sub, toInstance;
	msgobj handler;
	int delivered = 0;

	outPort = GetPropNode(instance, port);
	if (!outPort)
		return 0;

	sub = GetNextProp(outPort);
	while (sub) {
		if (CmpName(sub, "Subscriber")) {
			handler    = (msgobj)  GetPropLong(sub, "Callback");
			toInstance = (NodeObj) GetPropLong(sub, "Instance");
			if (handler) {
				handler(toInstance, message, data);
				delivered++;
			}
		}
		sub = GetNextSibling(sub);
	}

	return delivered;
}

/* call the Activate function pointer an instance carries on itself */
int
ActivateInstance(NodeObj instance){

	msgobj Activate;

	if (!instance)
		return rtrn_dropped;

	Activate = (msgobj)GetPropLong(instance, "Activate");
	if (!Activate) {
		DebugPrint ( "ActivateInstance found no Activate function on the instance.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	return Activate(instance, msg_initialize, NULL);
}



// Handle registration of objects, classes, and instances,
PrintRegInfo(char* message, NodeObj obj){
	char buffer[255];
	sprintf((char *)&buffer, message, GetNameStr(obj));
	DebugPrint ((char *)&buffer, __FILE__, __LINE__, REGISTER);
}

NodeObj RegisterLibrary(NodeObj library){
	PrintRegInfo("Registering object '%s'", library);
	AddChild(RegObjList, library);
	return library;
}

void UnregisterLibrary(NodeObj library){
	PrintRegInfo("Unregistering object '%s'", library);
	SetPropInt(library, "State", 0); 	//Mark this node as gone.

	/* the full node dump is only wanted at high verbose levels */
	if (DebugPrintGetLevel() >= 3)
		PrintNode(library);

	//DelNode(node);  // I stopped removing the node to see it dump out on exit
}

NodeObj RegisterClass(NodeObj library, NodeObj class){
	PrintRegInfo("Registering class '%s'", class);
	AddChild(library, class);
	//PrintNode(library);
	//msgobj InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	//if (InstanceStart) InstanceStart(class, 1, NULL);
	return class;
}

void UnRegisterClass(NodeObj library, NodeObj class){
    PrintRegInfo("Unregistering class '%s'", library);
	//DelNode(node);
}

NodeObj RegisterInstance(NodeObj class, NodeObj Instance){
	PrintRegInfo("Registering instance of '%s'", Instance);

	AddChild(class, Instance);

	/* leave the newest instance where CreateObject can find it */
	SetPropLong(class, "LastInstance", (long)Instance);

	return Instance;
}

void
UnRegisterInstance(NodeObj class, NodeObj Instance){
    PrintRegInfo("Unregistering instance of '%s'", class);
	//DelNode(node);
}