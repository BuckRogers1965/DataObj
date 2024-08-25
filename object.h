#ifndef Object_H_
#define Object_H_



typedef int MsgId;

typedef int(*msgobj)(NodeObj instance, MsgId message, NodeObj data);

enum {Stopping=0, Starting, Running};


/* create a container to hold object instances */
/* all containers must be part of another container */
NodeObj
CreateContainer(NodeObj node, char * name);

/* create an object instance of named class in the given container */
NodeObj
CreateObject(NodeObj container, char * classname);

/* Connect two properties between two object instances */
int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to);


/* Call backs from dynamically loaded objects to register and unregister themselves. */
NodeObj
RegisterLibrary(NodeObj node);

void
UnregisterLibrary(NodeObj node);

NodeObj
RegisterClass(NodeObj obj, NodeObj class);
void
UnregisterClass(NodeObj obj, NodeObj class);

NodeObj
RegisterInstance(NodeObj class, NodeObj inst);
void
UnregisterInstance(NodeObj class, NodeObj inst);


/* The main funtion must sent a property node of it's main to accept the register list */
void
ObjSetRegObjList(NodeObj node);


typedef enum {
    PROP_TEXTBOX=1,
    PROP_LED,
    PROP_BUTTON,
    PROP_CHECKBOX,
    PROP_NULL
} PropertyType;

#endif