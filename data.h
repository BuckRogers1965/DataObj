
/* 

This code module is the opaque interface to the Data object module.

Create data objects, set their value, retrieve the value in a number of types with automatic conversion of the data between types.


*/

#define STRING 0
#define INTEGER 1
#define HEX 2
#define REAL 3
#define LONG 4

typedef  void * DataObj;

/* Create a new data object to hold data */
DataObj NewData(int type);

/* Set and Get the value of the object as a string */
int    SetStr(DataObj this, char * value);
char * GetStr(DataObj this);

/* Set the value of the object as an integer */
int SetInt(DataObj this, int value);
int GetInt(DataObj this);

/* Set and Get the value of the object as hexidecimal */
int    SetHex(DataObj this, char * value);
char * GetHex(DataObj this);

/* Set and Get the value of the object as a Real number */
int    SetReal(DataObj this, double value);
double GetReal(DataObj this);

/* Set and Get the value of the object as a long number */
int  SetLong(DataObj this, long value);
long GetLong(DataObj this);

void
DataTest ();
