
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
Register(char * classname, char * company, char * uuid, msgobj objhndl);

void
Unregister(NodeObj node);


/* The main funtion must sent a property node of it's main to accept the register list */
void
ObjSetRegObjList(NodeObj node);
