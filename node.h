
#include "data.h"

/*
Implement new data type.
Manage memory for the data type.

Implement a property list for the object that allows multiple properties, make these properties 
be the same data objects as the data object.  So that way properties can have properties.  

Allow functions to be called if a node is changed.
*/

typedef void * NodeObj;

NodeObj NewNode (int type);
void    DelNode (NodeObj node);
void PrintNode(NodeObj node);

// serialization - node tree as text (JSON), and back
int     GetValueType (NodeObj node);
char *  NodeToText   (NodeObj node);

/* a quoted, escaped JSON string literal (quotes included) - for code   */
/* hand-building JSON objects instead of going through NodeToText       */
char *  JsonEscapeStr(char * s);
NodeObj TextToNode   (char * text);

/* flat JSON object -> a node whose props are its key/value pairs; no  */
/* nesting/arrays - for simple flat commands, not general trees        */
NodeObj TextToProps  (char * text);

void   SetName    (NodeObj node, char * name);
char * GetNameStr (NodeObj node);
int    CmpName    (NodeObj node, char * name);

DataObj GetValueNode (NodeObj node);
int     GetValueInt  (NodeObj node);
int     GetValueInt  (NodeObj node);
void    SetValueInt  (NodeObj node, int value);
char *  GetValueStr  (NodeObj node);
void    SetValueStr  (NodeObj node, char * Value);

/* binary-safe: copies exactly `length` bytes, embedded NULs included */
void    SetValueStrLen (NodeObj node, char * value, int length);
int     GetValueLen    (NodeObj node);
long    GetValueLong (NodeObj node);
void    SetValueLong (NodeObj node, long value);

// properties 
NodeObj GetPropNode (NodeObj node, char * name);
NodeObj GetNextProp (NodeObj node);
void    SetProp     (NodeObj node, char * name, int value);
void    AddProp     (NodeObj node, NodeObj prop);
void    SetPropInt  (NodeObj node, char * Name, int Value);
int     GetPropInt  (NodeObj node, char * Name); 
void    SetPropStr  (NodeObj node, char * Name, char * Value); 
char *  GetPropStr  (NodeObj node, char * Name);
void    SetPropLong (NodeObj node, char * Name, long Value);
long    GetPropLong (NodeObj node, char * Name);

// need more child handling functions here
void    SetChild     (NodeObj node,   NodeObj child);
void    AddChild     (NodeObj parent, NodeObj child);
void    AppendChild  (NodeObj parent, NodeObj child);
NodeObj GetChild     (NodeObj node);
NodeObj GetParent    (NodeObj node);
NodeObj GetNextChild (NodeObj node);

// sibling handling
void    AddSibling     (NodeObj node, NodeObj sib);
void    DelSibling     (NodeObj sib);
NodeObj GetSibling     (NodeObj node);
NodeObj GetNextSibling (NodeObj node);

// Test
void NodeTest ();


