#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "data.h"


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

typedef struct node * NodeObj;

struct node {
	int type;
	DataObj name;
	DataObj value;
	NodeObj props;
	NodeObj parent;
	NodeObj class;
	NodeObj child;
	NodeObj nextSib;
	NodeObj prevSib;
} node;

#include "callback.h"

void 
PrintNodePrivate(NodeObj node, int depth){

	NodeObj current = node;
	int i;

//	printf("depth=%d\n", depth);

//	printf("%s %s\n", GetStr(node->name), GetStr(node->value));


//	node = node->nextSib;
	while(current){

		for (i=0; i<depth; i++)
			printf(" ");

		printf("%s %s\n", GetStr(current->name), GetStr(current->value));

		if(current->props)
			PrintNodePrivate(current->props, depth);

		if(current->child)
			PrintNodePrivate(current->child, depth+1);
		
		current = current->nextSib;

	}

	printf("\n");
}

void
PrintNode(NodeObj node){

	PrintNodePrivate(node, 0);
}


void
DelNode(NodeObj node){

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

        free(node);
}


NodeObj
NewNode(){
	NodeObj temp = malloc (sizeof(node));
	if (temp) {
		temp->name = NewData(STRING);
		temp->value = NewData(INTEGER);
		temp->props = NULL;
		temp->parent = NULL;
		temp->child = NULL;
		temp->nextSib = NULL;
		temp->prevSib = NULL;
	}
	return temp;
}

void
SetName(NodeObj node, char * name){

	if(!node || !name)
		return;

	SetStr(node->name, name);
}

int
CmpName(NodeObj node, char * name){

	int length;

	if(!node || !name)
		return 0;

	length = strlen(name)+1;
	
	return (strncmp( GetStr(node->name), name, length ) == 0);
}

void
SetValueStr(NodeObj node, char * value){

	if (!node || !value)
		return;

	SetStr(node->value, value);
}


char *
GetValueStr(NodeObj node){

	if (!node)
		return NULL;

	return GetStr(node->value);
}


char *
GetNameStr(NodeObj node){

	if (!node)
		return NULL;

	return GetStr(node->name);
}

int
GetValueInt(NodeObj node){

	if (!node)
		return 0;

	return GetInt(node->value);
}

DataObj
GetValueNode(NodeObj node){
	if (!node)
		return 0;

	return node->value;
}

void
SetParent (NodeObj parent, NodeObj child){
	if(!parent || !child)
		return;
	child->parent=parent;
}

void
SetChild (NodeObj node, NodeObj child){
	if(!node || !child)
		return;
	DelNode(node->child);
	node->child=child;
	child->parent=node;
}

void
DelSibling (NodeObj sib){
	if(!sib)
		return;
	if (sib->nextSib){
	}
}

void
AddSibling (NodeObj node, NodeObj sib){
	if(!node || !sib)
		return;

	sib->nextSib=node->nextSib;	
	if (node->nextSib){
		node->nextSib->prevSib=sib;
	}
	node->nextSib=sib;
	sib->prevSib=node;
	sib->parent=node->parent;
}

void
AddChild(NodeObj parent, NodeObj child){
	if(!parent || !child)
		return;

	if (parent->child)
		AddSibling(parent->child, child);
	else
		parent->child = child;

	child->parent=parent;
}

NodeObj
GetPropNode(NodeObj node, char * name){
	if (!node || !name)
		return NULL;
	NodeObj current = node->props;
	while (current){
		if (CmpName(current, name))
			return current;
		current=current->nextSib;
	}
	return NULL;
}

void
SetPropInt(NodeObj node, char * name, int value){
	NodeObj propnode;
	if (!node || !name)
		return;

	/* If property exists */
	if ((propnode=GetPropNode(node, name))){

		NodeObj intercept = GetPropNode(propnode, "Intercept");
		if(intercept){

			NodeObj newpropnode;
			newpropnode = NewNode();
			SetStr(newpropnode->name, name);
			SetInt(newpropnode->value, value);

			FuncPtr callback = (FuncPtr)GetValueInt(GetPropNode(intercept, "Callback"));
			NodeObj current = GetPropNode(intercept, "Current");

			if (!callback || ! current){
				SetInt(propnode->value, value);
			} else {
				(*callback)(current, newpropnode, msg_change);
			}

		} else {
			SetInt(propnode->value, value);
		}

	/* otherwise create the property */
	} else {
		propnode = NewNode();
		SetStr(propnode->name, name);
		SetInt(propnode->value, value);
		if (node->props){
			AddSibling(node->props, propnode);
		} else {
			node->props = propnode;
			propnode->parent = node;
		}
	}
}

void
SetPropStr(NodeObj node, char * name, char * value){
	NodeObj propnode;
	if (!node || !name)
		return;

	/* If property exists */
	if ((propnode=GetPropNode(node, name))){

		NodeObj intercept = GetPropNode(propnode, "Intercept");
		if(intercept){
			
		} else
		SetStr(propnode->value, value);

	/* otherwise create the property */
	} else {
		propnode = NewNode();
		SetStr(propnode->name, name);
		SetStr(propnode->value, value);
		if (node->props){
			AddSibling(node->props, propnode);
		} else {
			node->props = propnode;
			propnode->parent = node;
		}
	}
}

void
TestFunc(NodeObj node){

	printf("\n\nRunning tests\n\n");

	printf("Set name of root to Test.\n");
	SetName(node, "Test");

	printf("Is name of root Test?  %d\n", CmpName(node, "Test"));
	printf("Is name of root Banana?  %d\n", CmpName(node, "Banana"));

	printf("Set property on root named Banana, set value to 5.\n");
	SetPropInt(node, "Banana", 5);

	printf("The value of Banana is %d\n", GetValueInt(GetPropNode(node, "Banana")));

	printf("Check root for non exisitant property Fred.\n");
	if (GetPropNode(node, "Fred")) 
		printf("Fred Exists.\n");
	else
		printf("Fred property does not exist.\n");

	printf("Set another property on root to be Fred with value of 6.\n");
	SetPropInt(node, "Fred", 6);

	if (GetPropNode(node, "Fred")) 
		printf("Fred Exists.\n");
	else
		printf("Fred property does not exist.\n");

	PrintNode(node);

}

void
NodeTest (){

	NodeObj root = NULL;
	int i;

	for (i=0; i < 1; i++){
		root = NewNode();

		TestFunc(root);

		DelNode(root);
		root=NULL;

		TestFunc(root);
	}

}

