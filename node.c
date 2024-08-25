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

	free(node);
}

NodeObj
NewNode(int type)
{
	NodeObj temp = malloc(sizeof(node));
	if (temp)
	{
		temp->name = NewData(STRING);
		temp->value = NewData(type);
		temp->props = NULL;
		temp->parent = NULL;
		temp->child = NULL;
		temp->nextSib = NULL;
	}
	return temp;
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

void SetValueInt(NodeObj node, int value)
{

	if (!node || !value)
		return;

	SetInt(node->value, value);
}

void SetValueLong(NodeObj node, long value)
{

	if (!node || !value)
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

void DelSibling(NodeObj sib)
{
	if (!sib)
		return;
	if (sib->nextSib)
	{
	}
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
		return NULL;
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

void SetPropLong(NodeObj node, char *name, long value)
{
	NodeObj propnode;
	if (!node || !name)
		return;

	if (!node->props)
		if ((propnode = GetPropNode(node, name)))
		{
			printf ("updating property %llu\nyy", value);
			// if the node exists
			SetLong(propnode->value, value);
			return;
		}
	
	printf ("Adding property %llu\nyy", value);
	/* otherwise create the property */
	propnode = NewNode(LONG);
	SetStr(propnode->name, name);
	SetLong(propnode->value, value);
	NodeObj temp = node->props;
	node->props = propnode;
	propnode->nextSib = temp;
}

void SetPropInt(NodeObj node, char *name, int value)
{
	NodeObj propnode;
	if (!node || !name)
		return;

	if (!node->props)
		if ((propnode = GetPropNode(node, name)))
		{
			// if the node exists
			SetInt(propnode->value, value);
			return;
		}
		
	/* otherwise create the property */
	propnode = NewNode(INTEGER);
	SetStr(propnode->name, name);
	SetInt(propnode->value, value);
	NodeObj temp = node->props;
	node->props = propnode;
	propnode->nextSib = temp;
}

void SetPropStr(NodeObj node, char *name, char * value)
{
	NodeObj propnode;
	if (!node || !name)
		return;

	if (!node->props)
		if ((propnode = GetPropNode(node, name)))
		{
			// if the node exists
			SetStr(propnode->value, value);
			return;
		}
		
	/* otherwise create the property */
	propnode = NewNode(STRING);
	SetStr(propnode->name, name);
	SetStr(propnode->value, value);
	NodeObj temp = node->props;
	node->props = propnode;
	propnode->nextSib = temp;
}

void SetPropLongOLD(NodeObj node, char *name, long value)
{
	NodeObj propnode;
	if (!node || !name)
		return;

	/* If property exists */
	if ((propnode = GetPropNode(node, name)))
	{

		NodeObj intercept = GetPropNode(propnode, "Intercept");
		if (intercept)
		{

			NodeObj newpropnode;
			newpropnode = NewNode(LONG);
			SetStr(newpropnode->name, name);
			SetLong(newpropnode->value, value);

#ifndef S_SPLINT_S

			FuncPtr callback = (FuncPtr)GetValueLong(GetPropNode(intercept, "Callback"));
			NodeObj current = GetPropNode(intercept, "Current");

#endif
			if (!callback || !current)
			{
				SetLong(propnode->value, value);
			}
			else
			{
				(*callback)(current, newpropnode, msg_change);
			}
		}
		else
		{
			SetLong(propnode->value, value);
		}

		/* otherwise create the property */
	}
	else
	{
		propnode = NewNode(LONG);
		SetStr(propnode->name, name);
		SetLong(propnode->value, value);
		AddProp(node, propnode);
	}
}

void SetPropLongold(NodeObj node, char *name, long value)
{
	NodeObj propnode;
	if (!node || !name)
		return;

	/* If property exists */
	if ((propnode = GetPropNode(node, name)))
	{

		NodeObj intercept = GetPropNode(propnode, "Intercept");
		if (intercept)
		{

			NodeObj newpropnode;
			newpropnode = NewNode(LONG);
			SetStr(newpropnode->name, name);
			SetLong(newpropnode->value, value);

#ifndef S_SPLINT_S

			FuncPtr callback = (FuncPtr)GetValueLong(GetPropNode(intercept, "Callback"));
			NodeObj current = GetPropNode(intercept, "Current");

#endif
			if (!callback || !current)
			{
				SetLong(propnode->value, value);
			}
			else
			{
				(*callback)(current, newpropnode, msg_change);
			}
		}
		else
		{
			SetLong(propnode->value, value);
		}

		/* otherwise create the property */
	}
	else
	{
		propnode = NewNode(LONG);
		SetStr(propnode->name, name);
		SetLong(propnode->value, value);
		AddProp(node, propnode);
	}
}

void SetPropStrold(NodeObj node, char *name, char *value)
{
	NodeObj propnode;
	if (!node || !name)
		return;

	/* If property exists */
	if ((propnode = GetPropNode(node, name)))
	{

		NodeObj intercept = GetPropNode(propnode, "Intercept");
		if (intercept)
		{
		}
		else
			SetStr(propnode->value, value);

		/* otherwise create the property */
	}
	else
	{
		propnode = NewNode(STRING);
		SetStr(propnode->name, name);
		SetStr(propnode->value, value);
		AddProp(node, propnode);
	}
}

void TestFunc(NodeObj node)
{

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

void NodeTest()
{

	NodeObj root = NULL;
	int i;

	for (i = 0; i < 1; i++)
	{
		root = NewNode(INTEGER);

		TestFunc(root);

		DelNode(root);
		root = NULL;

		TestFunc(root);
	}
}
