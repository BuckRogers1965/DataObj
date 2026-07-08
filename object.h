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
/* the sink's "to" port subscribes to the source's "from" port */
int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to);

/* Send a message out a named port of an instance. */
/* The message is routed to every subscriber of that port. */
/* Returns the number of subscribers it was delivered to.  */
int
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data);

/* Call the Activate function an instance registered on itself */
int
ActivateInstance(NodeObj instance);


/* Call backs from dynamically loaded objects to register and unregister themselves. */
NodeObj
RegisterLibrary(NodeObj node);

void
UnregisterLibrary(NodeObj node);

NodeObj
RegisterClass(NodeObj obj, NodeObj class);
void
UnRegisterClass(NodeObj obj, NodeObj class);

NodeObj
RegisterInstance(NodeObj class, NodeObj inst);
void
UnRegisterInstance(NodeObj class, NodeObj inst);


/* The main funtion must sent a property node of it's main to accept the register list */
void
ObjSetRegObjList(NodeObj node);

/* The main function must send in its scheduler task list */
/* so that loaded objects can schedule their own tasks     */
void
ObjSetTaskList(void * list);

void *
ObjGetTaskList(void);


typedef enum {
    PROP_TEXTBOX=1,
    PROP_LED,
    PROP_BUTTON,
    PROP_CHECKBOX,
    PROP_NULL
} PropertyType;

#endif