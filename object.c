#include <stdio.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"

NodeObj RegObjList;

void
ObjSetRegObjList(NodeObj node){
	RegObjList = node;
}

NodeObj
CreateContainer(NodeObj container, char * name){

        // these containers just exist in our nodes to organize groups of objects together.
        // evolve into an application grouping with a schedule? 

        // these could be functional organizations
        // later we could also have these same objects in multiple views in logical organizations


        // need to check to see if name already exists
	NodeObj temp = NewNode();
	SetName(temp, name);

	AddChild(container, temp);

	return temp;
}

NodeObj
CreateObject(NodeObj container, char * classname){

        // has the class name been registered?
        // how do we tie these actions into class actions?

        //does this node name exist?

	NodeObj temp = NewNode();


        // decorate the node with class functions.

          
	SetName(temp, classname);
        // set object handle into us here.

	AddChild(container, temp);
        // place us into the container

	return temp;
}

void AddConnectTo(NodeObj fromNode, NodeObj toNode){


    // find our name in subscription list.
}

void AddConnectFrom(NodeObj toNode, NodeObj fromNode){
}

int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	NodeObj nfrom = GetPropNode(fromNode, from);
	NodeObj nto= GetPropNode(toNode, to);
	AddConnectTo(nfrom, toNode);
	AddConnectFrom(fromNode, nto);

	return 0;
}

NodeObj
Register(char * classname, char * company, char * uuid, msgobj objhndl){

	char buffer[255];

	NodeObj temp = NewNode();
	SetName(temp, classname);

	SetPropStr(temp, "Company", company);
	SetPropStr(temp, "UUID", uuid);

	SetPropInt(temp, "ObjectHandle", (int)objhndl);

	SetPropInt(temp, "State", 1);

	sprintf((char *)&buffer, "Registering object '%s' company '%s' uuid '%s' memhandle at %lu", classname, company, uuid, (unsigned long) objhndl);
	DebugPrint ((char *)&buffer, __FILE__, __LINE__, REGISTER);

	AddChild(RegObjList, temp);

	// need to add this item to a property which is a list.

	return temp;
}


void
Unregister(NodeObj node){

	char buffer[255];

	sprintf((char *)&buffer, "Unregistering object '%s'", GetNameStr(node));
	DebugPrint ((char *)&buffer, __FILE__, __LINE__, REGISTER);


	//Mark this node as gone.
	SetPropInt(node, "State", 0);

	//DelNode(node);
}

