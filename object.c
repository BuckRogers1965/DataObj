#include <stdio.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"

NodeObj RegObjList;

void
ObjSetRegObjList(NodeObj node){
	RegObjList = node;
}

loadClasses(){
	NodeObj library = GetChild( RegObjList );
	while (library) {
		msgobj ClassStart = (msgobj)GetPropLong(library, "ClassStart");
		if (ClassStart) ClassStart(library, 0, NULL);
		library = GetNextSibling(library);
	}
		//printf ("In core:     Class callbacks: %lu, %lu, %lu\n", (long)ClassStart, (long)ClassEnd, (long)ClassMsg);
		//msgobj ClassEnd   = (msgobj)GetPropLong(library, "ClassEnd");
		//msgobj ClassMsg   = (msgobj)GetPropLong(library, "ClassMsg");
		//PrintNode(library);
}

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

NodeObj
CreateObject(NodeObj container, char * classname){

        // has the class name been registered?
        // how do we tie these actions into class actions?

        //does this node name exist?

	NodeObj temp = NewNode(INTEGER);


        // decorate the node with class functions.

          
	SetName(temp, classname);
        // set object handle into us here.

	AddChild(container, temp);
        // place us into the container

	return temp;
}

void AddSubscription(NodeObj fromNode, NodeObj toNode){
    // Add our name to subscription list.
	
}

int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to){
	NodeObj nfrom = GetPropNode(fromNode, from);
	NodeObj nto= GetPropNode(toNode, to);
	//AddSubcription(nfrom, toNode);
	return 0;
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
	//PrintNode(Instance);
	return Instance;
}

void
UnRegisterInstance(NodeObj class, NodeObj Instance){
    PrintRegInfo("Unregistering instance of '%s'", class);
	//DelNode(node);
}