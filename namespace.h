/* Given a set of strings, create a search tree of values */

#ifndef IN_NAMESPACE
typedef void * NSObj; 
#endif

NSObj *
NSCreate ();

void
NSRelease (NSObj *);

int
NSInsert (NSObj *, char *, long);

long
NSSearch (NSObj *, char *);

int
NSDelete (NSObj *, char *);

int
NameSpaceTest ();
