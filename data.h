
/* 

This code module is the opaque interface to the Data object module.

Create data objects, set their value, retrieve the value in a number of types with automatic conversion of the data between types.


*/

#define STRING 0
#define INTEGER 1
#define HEX 2
#define REAL 3


typedef  void * DataObj;


/* Create a new data object to hold data */
DataObj
NewData(int type);


/* Set the value of the object as a string */
int
SetStr(DataObj this, char * value);

/* Get the value of the object as a string */
char *
GetStr(DataObj this);


/* Set the value of the object as an integer */
int
SetInt(DataObj this, int value);

/* Get the value of the object as an integer */
int
GetInt(DataObj this);


/* Set the value of the object as hexidecimal */
int
SetHex(DataObj this, char * value);

/* Get the value of the object as hexidecimal */
char *
GetHex(DataObj this);


/* Set the value of the object as a Real number */
int
SetReal(DataObj this, double value);

/* Get the value of the object as a Real number */
double
GetReal(DataObj this);

void
DataTest ();
