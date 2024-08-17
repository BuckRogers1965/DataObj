
#include "data.h"

/*

Implement new data type.
Manage memory for the data type.

Implement a property list for the object that allows multiple properties, make these properties be the same data objects as the data object.  So that way properties can have properties.  


Allow functions to be called if a node is changed.

*/

typedef void * NodeObj;

void
DelNode(NodeObj node);

NodeObj
NewNode();

void
PrintNode(NodeObj node);

void
SetName(NodeObj node, char * name);

int
CmpName(NodeObj node, char * name);

int
GetValue(NodeObj node);

int
GetValueInt(NodeObj node);

char *
GetValueStr(NodeObj node);

DataObj
GetValueNode(NodeObj node);

char *
GetNameStr(NodeObj node);

void
SetValue(NodeObj node, int value);

void
SetValueStr(NodeObj node, char * Value);

void
SetPropInt(NodeObj node, char * Name, int Value);

void
SetPropStr(NodeObj node, char * Name, char * Value);

void
SetChild (NodeObj node, NodeObj child);

void
DelSibling (NodeObj sib);

void
AddChild(NodeObj parent, NodeObj child);

void
AddSibling (NodeObj node, NodeObj sib);

NodeObj
GetPropNode(NodeObj node, char * name);

void
SetProp(NodeObj node, char * name, int value);

void
NodeTest ();


