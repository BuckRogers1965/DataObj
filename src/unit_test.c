#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>

#include "node.h"
#include "list.h"
#include "sched.h"
#include "deamon.h"
#include "dirscan.h"

#include "object.h"
#include "widget.h"
#include "control.h"	/* the palette, the chrome, and placement */
#include "timer.h"
#include "libload.h"
#include "dyn/bufftest.h"
#include "DebugPrint.h"
#include "namespace.h"

#include "version.h"
#include "dyn/buff.h"
#include "dyn/queue.h"
#include "skin.h"		/* GetClassSkin/LoadSkin moved to skin.object */
#include "flow.h"		/* the Flow* script API moved to flow.object */
#include <stdarg.h>
#include <stdlib.h>

TaskList Tasks;

/*

The rest of the code modules are built into a single library.

This main code module is built on top of the library as a 
seperate executable linked to the library.

The reason for this is so that we can have multiple different
executables, all tiny, that each dynamically load the same library
but that can each have radically different behaviors.

This seperates the function of the library from the form of the program.

The main executable defines the behavior of:

	where to look for dynamically loaded modules
		Right now we dynamically scan down from the execution path.

	where to load in a default application
		The default application can define many further 
		operations to control the entire environment.

	what actions to take based on command line options
		Become a service?
		Where to log debug info
		How much debug info to generate
		Run unit tests for sub libraries?

	Collect all the unit tests of all the sublibraries into 
	a single function.
		Should we have an init funtion in each library 
		that calls back a function to call for testing 
		with standard return values?

REGISTERING WITH CODE MODULES	

The Tasklist is called back into the schedule function
	This should be a member of a container 
	So that there can be seperate multiple task lists executing functions

Once the Tasklist is empty, clean up and exit.

The List of registered objects is called back into the object code module so
that loaded modules can be added to the list as they register themselves.

This list of items is used later to call the activation funtions of each
loaded module. 

DYNAMICALLY LOADABLE MODULES

The dynamically loaded modules also link against the framework library.

This dramatically reduces their size into the 10-20 KB range, assuming 
they themselves don't load too big of libraries.

These modules register themselves on library load and unregister on Library exit.

ToDo:

X Create a pulse generator that outputs a 1 followed by a 0 at specified millisecond intervals.
Create a timer object that outputs the time between events it receives on it's input.
Create a random object that will output a random value between a high and low value.
Create a filter object that only passes items when the value changes.
Create a filter object that only passes 1's
Create a filter object that only passes a 0
X Create a file object that can read and write files.



improvement:

Allow objects to subclass from other objects, making them depend on those
other objects.  

X Load objects in proper order, so that their dependencies are 
all satisfied or they will be unloaded.

improvement:

X The objects will publish their own interface.

Later when I add dynamically loaded language objects the objects will 
work with the language modules to automatically generate the 
necesary include or object definition files in order to properly 
program them in any embedded languages.

If we add a new object it should work with existing languages to 
export it's interface to all available languages.

If we add a new language it should generate proper header files 
for it's object definition for all existing objects.

improvement:

Be able to save and load xml files that describe the state of a container and subcontainers.
Compress these files transparently using zlib for bonus points.  Can be added later.


improvement:

At some point make a graphical interface to describe everything.
Make skins for objects load from an xml property file.

Containers can also be skinned to make them look good.

Be able to load background images for the containers 
and objects and position their 


*/

void MainLoop(NodeObj Main){
	unsigned long offset;
	int CountOfScheduledTasks = 0;
	offset = (unsigned long) TimeUpdate();
	if ( offset > 0 )
		AdjustDelayedTasks (Tasks, offset);
	CountOfScheduledTasks = ExecTasks(Tasks);

	/* if we have no scheduled tasks, then begin stopping */
	if (CountOfScheduledTasks == 0)
		SetPropInt(Main, "State", Stopping);	
}

/*

The default application: nothing but the surfaces a user (or a script)
actually talks to - the web GUI flow, the raw JSON bridge, and the
authenticated bridge. The app IS objects plus wiring, built with the
same CreateObject/Connect/ActivateInstance calls anything else uses;
eventually this function is a flow file, not code.

The dataflow test flows that used to boot here (the Reader->Writer
"cat", the pulse/filter/gate and queue/stack chains, the 30-second TCP
echo server) live in the test harness now - testharness/flowtest.py
builds the identical wiring over the raw protocol and ASSERTS on
subscribed events instead of printing probes for eyeballing. Same
engine mechanisms, exercised the way any client exercises them. The
harness even composes its own raw TCP transport through the web
bridge first (ensure_raw_bridge, rawtest.py) - a transport is just
objects plus wiring, so the disabled flows below stay disabled.

*/

/* return the current status of the Main execution thread */
int IsRunning(NodeObj Main){
	return (GetInt((DataObj)GetValueNode(GetPropNode(Main, "State"))));
}


/* anything still on the task list when the tests are done is a test that armed
   something and never let it drain - name it and get out, rather than spinning
   in the main loop forever */
void UT_PerformTesting(void);
void UT_InterceptTest(void);
void UT_LinkTest(void);

static int UT_ReportLeftoverTasks(TaskList list)
{
	TaskObj t;
	Dl_info info;
	FuncPtr cb;
	int     n = 0;
	char   *where;

	for (where = "", t = GetTaskListHead(list); ; t = GetTaskNext(t))
	{
		if (!t)
		{
			if (where[0]) break;
			where = " (runnow)";
			/* the HEAD of the runnow bucket, not the bucket itself:
			   GetTaskListRunnow returns the LIST, and both a list and a task
			   entry are typedef'd void *, so handing the list straight to
			   GetTaskNext walks the list header as though it were a task and
			   reports whatever those bytes happen to be (seen live:
			   callback=0x31, data=ASCII). The compiler cannot catch it. */
			t = GetTaskListHead(GetTaskListRunnow(list));
			if (!t) break;
		}
		cb = GetTaskCallback(t);
		n++;
		if (cb && dladdr((void *) cb, &info) && info.dli_sname)
			printf("LEFTOVER TASK%s: %s()  data=%p\n", where, info.dli_sname, (void *) GetTaskData(t));
		else
			printf("LEFTOVER TASK%s: callback=%p  data=%p\n", where, (void *) cb, (void *) GetTaskData(t));
	}
	return n;
}

void PerformTesting(){
	DebugPrint ( "Entering Perform Testing function.", __FILE__, __LINE__, PROG_FLOW);
	DataTest();
	NodeTest();
	/* PropertyWatchTest moved to the harness: UT_PropertyWatchTest */
	BuffTest();
	//NameSpaceTest();
	SchedTest();
}

void Init(NodeObj Main){

	char * logname;
	NodeObj RegObjList;

	/* just hum along, add in the parts to initialize base object as I find we need them. */

	/* apply the verbose level the command line parser stored, */
	/* from here on DebugPrint filters by message type          */
	DebugPrintSetLevel(GetValueInt(GetPropNode(Main, "loglevel")));

	DebugPrint ( "Entering Init function.", __FILE__, __LINE__, PROG_FLOW);

	/* Set the name of the main object */
	SetName (Main, "Main");

	/* Create a place to store registered Objects */
	SetPropInt(Main, "RegObjList", 1);
	RegObjList = GetPropNode(Main, "RegObjList");
	ObjSetRegObjList(RegObjList);

	/* activate the main object */
	SetPropInt(Main, "State", Running);

	/* Insert release info into the Main node properties */
	SetPropStr(Main, "ReleaseMajor", RELEASEMAJOR);
	SetPropStr(Main, "ReleaseName",  RELEASENAME);
	SetPropStr(Main, "ReleaseMinor", RELEASEMINOR);
	SetPropStr(Main, "ReleaseLevel", RELEASELEVEL);
	SetPropStr(Main, "Copyright",    COPYRIGHT);
	SetPropStr(Main, "Author",       AUTHOR);
	SetPropStr(Main, "ReleaseTag",   RELEASETAG);

	/* process the command line */

	/* print out the help text if printhelp is turned on */
	if (GetValueInt(GetPropNode(Main, "printhelp"))) {
		printf ("%s %s.%s %s - (C) %s %s\n%s\nhttp://grokthink.org\n\n  Usage: framework <options>\n\n  Options:\n\n       -h              : This help screen\n       -d              : Become a server process\n       -ip   <address> : Address to bind the web GUI to, e.g. 127.0.0.1 or 0.0.0.0 (default 0.0.0.0)\n       -l    <logfile> : logfile to output debug info\n       -p              : Print Main Nodes on exit\n       -port <number>  : Port to serve the web GUI on (default 8083)\n       -t              : Perform Unit Testing of library functions\n       -v     <number> : Verbose level from 0 to 9, inclusive\n\n", RELEASENAME, RELEASEMAJOR, RELEASEMINOR, RELEASELEVEL, COPYRIGHT, AUTHOR, RELEASETAG);
	}

	/* if -t command line argument is set, perform unit test */
	if (GetValueInt(GetPropNode(Main, "UnitTest"))) {
		PerformTesting();		/* the copies still inside libframework.so */
	} else {
		UT_PerformTesting();		/* the copies in this file */
	}


	/* if deamon option was turned on, become a deamon */
	if (GetValueInt(GetPropNode(Main, "deamon"))) {
		// also turn off logging in debug print.
		// because part of becoming a deamon is eliminating stdout

		DebugPrint ( "Becoming a Deamon.", __FILE__, __LINE__, PROG_FLOW);
		become_deamon ();
	}

	/* if logname is given, set the debug print to use the logfile */
	logname = GetValueStr(GetPropNode(Main, "logname"));
	if (logname && strlen(logname)) {
		// set up the debug print to output to logfile
		// turn on normal debug printing
		DebugPrint ( "Verbose Logging Enabled.", __FILE__, __LINE__, PROG_FLOW);
		;
	}
	DebugPrint ( "Logging Level Set.", __FILE__, __LINE__, PROG_FLOW);
	Tasks = CreateList();

	/* hand the task list to the object layer so that */
	/* loaded objects can schedule their own tasks    */
	ObjSetTaskList(Tasks);

}

void InstallObjects(void)
{
	DebugPrint ( "Entering Install Objects function.", __FILE__, __LINE__, PROG_FLOW);
        ScanDir (".", ".object", (void *) LoadObject, 8, 0, PreOrder);

	// once the objects are found and loaded then initialize them after this.

	loadClasses();
}

enum { STORE_FILENAME=0, STORE_LOGNAME, STORE_OPTION, STORE_LOGLEVEL, STORE_IP, STORE_PORT };
void ProcessCmdLine(NodeObj Main, int argc, char * argv[]){

	/* skip the process name */
	int i=0;
	int state=STORE_FILENAME;

	DebugPrint ( "Entering Process Command Line Function.", __FILE__, __LINE__, PROG_FLOW);

	DebugPrint ( "Store default verbose logging level of 1.", __FILE__, __LINE__, CMDLINEOPTS);
	SetPropInt(Main, "loglevel", 1);

	DebugPrint ( "Store default ip address of 0.0.0.0.", __FILE__, __LINE__, CMDLINEOPTS);
	SetPropStr(Main, "ip", "0.0.0.0");

	DebugPrint ( "Store default port of 8083.", __FILE__, __LINE__, CMDLINEOPTS);
	SetPropStr(Main, "port", "8083");

	while(i < argc){

		//printf("%d %s\n", i, argv[i]);
		DebugPrint ( argv[i], __FILE__, __LINE__, CMDLINEOPTS);
		switch (state){

		case STORE_LOGLEVEL:
			if ( argv[i][0]=='-' ) {
				DebugPrint ( "Option found instead of loglevel.", __FILE__, __LINE__, ERROR);	
				SetPropInt(Main, "printhelp", 1);
				return;
			} else {

				if (strlen (argv[i]) > 1 || argv[i][0]-'0' < 0 || argv[i][0]-'0' > 9) {
					DebugPrint ( "Log level not between 0 to 9, inclusive.", __FILE__, __LINE__, ERROR);
					SetPropInt(Main, "printhelp", 1);
					return;
				}
		
				DebugPrint ( "Store log level.", __FILE__, __LINE__, CMDLINEOPTS);
				SetPropInt(Main, "loglevel", argv[i][0]-'0');
			}
			state=STORE_OPTION;
			break;

		case STORE_FILENAME:
			DebugPrint ( "Store file name.", __FILE__, __LINE__, CMDLINEOPTS);
			SetPropStr(Main, "filename", argv[i]);

			// improvement:
			// need to seperate the file name from the path and store them seperate
			// That way the path to the executable can be used in searching for 
			// loadable objects in the install Objects routine later.
			state=STORE_OPTION;
			break;
			
		case STORE_LOGNAME:
			if ( argv[i][0]=='-' ) {
				DebugPrint ( "Option found instead of filename.", __FILE__, __LINE__, ERROR);
				SetPropInt(Main, "printhelp", 1);
				return;
			} else {
				DebugPrint ( "Store log name.", __FILE__, __LINE__, CMDLINEOPTS);
				SetPropStr(Main, "logname", argv[i]);
			}
			state=STORE_OPTION;
			break;

		case STORE_IP:
			if ( argv[i][0]=='-' ) {
				DebugPrint ( "Option found instead of ip address.", __FILE__, __LINE__, ERROR);
				SetPropInt(Main, "printhelp", 1);
				return;
			} else {
				DebugPrint ( "Store ip address.", __FILE__, __LINE__, CMDLINEOPTS);
				SetPropStr(Main, "ip", argv[i]);
			}
			state=STORE_OPTION;
			break;

		case STORE_PORT:
			if ( argv[i][0]=='-' ) {
				DebugPrint ( "Option found instead of port.", __FILE__, __LINE__, ERROR);
				SetPropInt(Main, "printhelp", 1);
				return;
			} else {
				DebugPrint ( "Store port.", __FILE__, __LINE__, CMDLINEOPTS);
				SetPropStr(Main, "port", argv[i]);
			}
			state=STORE_OPTION;
			break;

		case STORE_OPTION:

			if (  argv[i][0]!='-' ) {
				DebugPrint ( "Option not found.", __FILE__, __LINE__, ERROR);
				SetPropInt(Main, "printhelp", 1);
				break;
			} else {

				if ( strcmp ( argv[i], "-l" ) == 0 ) {
					state=STORE_LOGNAME;
					break;
				}

				if ( strcmp ( argv[i], "-ip" ) == 0 ) {
					state=STORE_IP;
					break;
				}

				if ( strcmp ( argv[i], "-port" ) == 0 ) {
					state=STORE_PORT;
					break;
				}

				if (strcmp ( argv[i], "-h" ) == 0 ) {
					DebugPrint ( "Store print help file.", __FILE__, __LINE__, CMDLINEOPTS);
					SetPropInt(Main, "printhelp", 1);
					break;
				}

				if (strcmp ( argv[i], "-t" ) == 0 ) {
					DebugPrint ( "Store perform unit tests.", __FILE__, __LINE__, CMDLINEOPTS);
					SetPropInt(Main, "UnitTest", 1);
					break;
				}

				if (strcmp ( argv[i], "-v" ) == 0 ) {
					state = STORE_LOGLEVEL;
					break;
				}

				if (strcmp ( argv[i], "-d" ) == 0 ) {
					DebugPrint ( "Store become deamon.", __FILE__, __LINE__, CMDLINEOPTS);
					SetPropInt(Main, "deamon", 1);
					break;
				}

				if (strcmp ( argv[i], "-p" ) == 0 ) {
					DebugPrint ( "Store print nodes on exit.", __FILE__, __LINE__, CMDLINEOPTS);
					SetPropInt(Main, "PrintNodes", 1);
					break;
				}

				DebugPrint ( "Unknown Option.", __FILE__, __LINE__, ERROR);
				SetPropInt(Main, "printhelp", 1);
				return;
			}

			break;

		default:
			DebugPrint ( "Option Not Found.", __FILE__, __LINE__, ERROR);
			SetPropInt(Main, "printhelp", 1);
		}
		i++;
	}

	if (state == STORE_LOGNAME) {
		DebugPrint ( "Log filename not given.", __FILE__, __LINE__, ERROR);
		SetPropInt(Main, "printhelp", 1);
		return;
	}

	if (state == STORE_LOGLEVEL) {
		DebugPrint ( "Verbose log level not given.", __FILE__, __LINE__, ERROR);
		SetPropInt(Main, "printhelp", 1);
		return;
	}

}


/* ---- the copied tests, renamed UT_* ---- */

/* ---- copied verbatim from data.c ---- */
int UT_DataTest(){

	char * str_str;
	char * str_int;
	char * str_hex;
	char * str_real;

	int int_str;
	int int_int;
	int int_hex;
	int int_real;

	char * hex_str;
	char * hex_int;
	char * hex_hex;
	char * hex_real;

	double real_str;
	double real_int;
	double real_hex;
	double real_real;

	DataObj str_do  = NewData(STRING);
	DataObj int_do  = NewData(INTEGER);
	DataObj hex_do	= NewData(HEX);
	DataObj real_do = NewData(REAL);

	//int i = 0;

	SetStr(str_do, "   1000  test me ");
	SetInt(int_do, 67676);
	SetHex(hex_do, "BEEF");
	SetReal(real_do, 12344.56 );

	str_str  = GetStr(str_do);
	int_str  = GetInt(str_do);
	hex_str  = GetHex(str_do);
	real_str = GetReal(str_do);

	str_int  = GetStr(int_do);
	int_int  = GetInt(int_do);
	hex_int  = GetHex(int_do);
	real_int = GetReal(int_do);

	str_hex  = GetStr(hex_do);
	int_hex  = GetInt(hex_do);
	hex_hex  = GetHex(hex_do);
	real_hex = GetReal(hex_do);

	str_real  = GetStr(real_do);
	int_real  = GetInt(real_do);
	hex_real  = GetHex(real_do);
	real_real = GetReal(real_do);

	printf("      str\t\t\tint\t\t\thex\t\t\treal\n");
	printf("str  >%s<\t %s \t\t\t %s \t\t\t %s \n", str_str, str_int, str_hex, str_real);
	printf("int   %d \t\t\t>%d<\t\t\t %d \t\t\t %d \n", int_str, int_int, int_hex, int_real);
	printf("hex   %s \t\t\t %s \t\t\t>%s<\t\t\t %s \n", hex_str, hex_int, hex_hex, hex_real);
	printf("real  %e \t\t %e \t\t %e \t\t>%e<\n\n", real_str, real_int, real_hex, real_real);


	DataObj long_do  = NewData(LONG);
	SetLong(long_do,  140224278132965);
	printf("\nLong to string check This long %lu to string %s \n\n", GetLong(long_do), GetStr(long_do));

	/* the test owns these five - DelData takes their converted strings with
	   them, so the whole conversion matrix it just walked is accounted for */
	DelData(str_do);
	DelData(int_do);
	DelData(hex_do);
	DelData(real_do);
	DelData(long_do);

	
	




	return 0;
}

/* ---- copied verbatim from node.c ---- */
void UT_TestFunc(NodeObj node)
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

/* ---- copied verbatim from node.c ---- */
void UT_SerializationTest()
{
	NodeObj root, child, roundtrip;
	char *json;

	printf("\n\nRunning serialization tests\n\n");

	root = NewNode(STRING);
	SetName(root, "Root");
	SetValueStr(root, "hello \"world\"\nline two");
	SetPropInt(root, "Count", 5);
	SetPropLong(root, "Handle", 140224278132965);

	child = NewNode(INTEGER);
	SetName(child, "Child1");
	SetValueInt(child, 42);
	AddChild(root, child);

	json = NodeToText(root);
	printf("Encoded: %s\n", json);

	roundtrip = TextToNode(json);
	printf("Decoded name=%s value=%s Count=%d Handle=%ld child name=%s child value=%d\n",
		   GetNameStr(roundtrip),
		   GetValueStr(roundtrip),
		   GetPropInt(roundtrip, "Count"),
		   GetPropLong(roundtrip, "Handle"),
		   GetNameStr(GetChild(roundtrip)),
		   GetValueInt(GetChild(roundtrip)));

	free(json);
	DelNode(root);
	DelNode(roundtrip);
}

/* ---- copied verbatim from node.c ---- */
static int UT_SubscriberTestSeen;
static int UT_SubscriberTestMessage;

/* signature matches every other Subscriber callback in the system -     */
/* (instance, message, data), the convention SndMsg (object.c) already   */
/* uses to invoke a port's subscribers. There is nothing property-       */
/* specific about it, which is the point: a plain property and a port    */
/* are fanned out to identically.                                        */
int UT_SubscriberTestCallback(NodeObj instance, int message, NodeObj data)
{
	(void) instance;

	UT_SubscriberTestMessage = message;
	UT_SubscriberTestSeen = GetValueInt(data);
	return rtrn_handled;
}

/* ---- copied verbatim from node.c ---- */
void UT_NodeTest()
{

	NodeObj root = NULL;
	int i;

	for (i = 0; i < 1; i++)
	{
		root = NewNode(INTEGER);

		UT_TestFunc(root);

		DelNode(root);
		root = NULL;

		UT_TestFunc(root);
	}

	UT_SerializationTest();
	UT_InterceptTest();
	UT_LinkTest();
}

/* ---- copied verbatim from namespace.c ---- */
int UT_NameSpaceTest()
{

	NSObj *NameSpace;
	int i, j;
	char string[1024];

	for (j = 0; j <= 10; j++)
	{

		NameSpace = NSCreate();

		for (i = 0; i <= 10000; i++)
		{

			sprintf(string, "lksdfhjklsdfjklsdfhjklsdfhjklsdfh%d/test0/test1/test2/test3/test4/%d", i / 10, j);
			NSInsert(NameSpace, string, i);
			printf("%ld\n", NSSearch(NameSpace, string));

			NSInsert(NameSpace, "james", 1);
			NSInsert(NameSpace, "rogers", 2);
			NSInsert(NameSpace, "bob", 3);
			NSInsert(NameSpace, "aaa", 3);
			NSInsert(NameSpace, "bobabcdefghijklmnopqrstuv", 4);
			NSInsert(NameSpace, "bobabcdefghij", 5);
			NSInsert(NameSpace, "fred", 6);
			NSInsert(NameSpace, "This is a very/long/test of /multiple dirs/", 7);
			NSInsert(NameSpace, "This is a very/long/test of /multiple dirs/me too", 8);
			NSInsert(NameSpace, "harry", 9);
			NSInsert(NameSpace, "retriever", 10);

			printf("%ld ", NSSearch(NameSpace, "james"));
			printf("%ld ", NSSearch(NameSpace, "rogers"));
			printf("%ld ", NSSearch(NameSpace, "bob"));
			printf("%ld ", NSSearch(NameSpace, "aaa"));
			printf("%ld ", NSSearch(NameSpace, "bobabcdefghijklmnopqrstuv"));
			printf("%ld ", NSSearch(NameSpace, "bobabcdefghij"));
			printf("%ld ", NSSearch(NameSpace, "fred"));
			printf("%ld ", NSSearch(NameSpace, "This is a very/long/test of /multiple dirs/"));
			printf("%ld ", NSSearch(NameSpace, "This is a very/long/test of /multiple dirs/me too"));
			printf("%ld ", NSSearch(NameSpace, "harry"));
			printf("%ld  ", NSSearch(NameSpace, "retriever"));

			printf("%ld ", NSSearch(NameSpace, "I don't exist"));

			printf("%d ", NSDelete(NameSpace, "bobabcdefghij"));
			printf("%d ", NSDelete(NameSpace, "bobabcdefghij"));
			printf("%d  ", NSDelete(NameSpace, "I don't exist"));

			printf("%ld ", NSSearch(NameSpace, "bob"));
			printf("%ld ", NSSearch(NameSpace, "bobabcdefghijklmnopqrstuv"));
			printf("%ld ", NSSearch(NameSpace, "bobabcdefghij"));

			NSInsert(NameSpace, "james", 1000);
			printf("%ld\n", NSSearch(NameSpace, "james"));
		}

		NSRelease(NameSpace);

		usleep(0);
		fprintf(stderr, ".");
	}
	fprintf(stderr, "\n");
	return 0;
}

/* ---- copied verbatim from sched.c ---- */
/* NOT COMPILED - UT_SchedTest: uses sched.c's internal TaskPtr type, its static testcallback, and an internal AddTaskDelay signature.
   #if 0 rather than a block comment because the body contains comments. */
#if 0
UT_SchedTest (){

	int CountOfScheduledTasks = 1;

	TimeUpdate();

   # ifndef S_SPLINT_S

	TaskList testlist = CreateList();
	
	TaskPtr   testtask1 =  CreateTask(testlist);
	TaskPtr   testtask2 =  CreateTask(testlist);
	TaskPtr   testtask3 =  CreateTask(testlist);
	TaskPtr   testtask4 =  CreateTask(testlist);

	NodeObj testdata = NewNode(INTEGER);
	SetPropInt(testdata, "TestData", 1);


	AddTaskDelay(testtask1, 5, 500000, &testcallback, 1000, testdata);  /* 5.5s: 500000us */
	AddTaskNow(testtask2, &testcallback, 1001, testdata);
	AddTaskMilli(testtask3, 100, &testcallback, 1002, testdata);
	AddTaskSec(testtask4, 10, &testcallback, 1003, testdata);

	printf("Schedtest\n");

	while(CountOfScheduledTasks){
		//long offset;

		TimeUpdate();

		CountOfScheduledTasks = ExecTasks(testlist);

		/* if we have no scheduled tasks, then begin stopping */

		printf(".");
		fflush(stdout);
		usleep(10000);
	}
   # endif

	printf("\n");

}
#endif		/* UT_SchedTest */

/* ---- copied verbatim from object.c ---- */
void UT_FlowTest(NodeObj container){

	NodeObj flow, reloaded, Pulse, Probe;
	char *original, *roundtrip;

	printf("\n\nRunning flow tests\n\n");

	flow = NewFlow("UT_FlowTest");

	Pulse = FlowCreateObject(flow, container, "Pulse");
	Probe = FlowCreateObject(flow, container, "Out");

	if (!Pulse || !Probe) {
		printf("Flow test needs the Pulse and Out classes, skipping.\n");
		return;
	}

	FlowSetProp(flow, Pulse, "Interval", "50");
	FlowSetProp(flow, Pulse, "Count", "1");
	FlowSetProp(flow, Probe, "Label", "flowtest");

	FlowConnect(flow, Pulse, "Out", Probe, "In");

	FlowActivateInstance(flow, Probe);
	FlowActivateInstance(flow, Pulse);

	original = NodeToText(flow);
	printf("Recorded flow: %s\n", original);

	SaveFlow(flow, "flowtest.flow");

	/* replay the saved script into a second, independent pair of instances - */
	/* its probe should print the same messages the original one does, once  */
	/* the scheduler gets to it                                              */
	reloaded = LoadFlow(container, "flowtest.flow");

	roundtrip = NodeToText(reloaded);
	printf("Reloaded script matches the recording: %d\n", strcmp(original, roundtrip) == 0);

	free(original);
	free(roundtrip);
}

/* ---- copied verbatim from object.c ---- */
void UT_InterfaceTest(){

	NodeObj library, class, interface, prop;
	char *text, *name;

	printf("\n\nRunning interface publication tests\n\n");

	/* every class the registry actually holds, rather than a list of names
	   that has to be kept in step with what exists */
	for (library = GetChild(GetRegObjList()); library; library = GetNextSibling(library))
	for (class = GetChild(library); class; class = GetNextSibling(class)) {

		name = GetNameStr(class);

		interface = GetClassInterface(class);
		if (!interface) {
			printf("%s: no published interface\n", name);
			continue;
		}

		text = NodeToText(interface);
		printf("%s interface: %s\n", name, text);
		free(text);

		prop = GetChild(interface);
		while (prop) {
			printf("  %-10s widget=%d default=%s\n",
				GetPropStr(prop, "Name"),
				GetPropInt(prop, "Widget"),
				GetPropStr(prop, "Default"));
			prop = GetNextSibling(prop);
		}
	}
}

/* ---- copied verbatim from object.c ---- */
void UT_SkinTest(){

	NodeObj readerClass, writerClass, skin, custom, layout;
	char *text;
	FILE *f;

	printf("\n\nRunning skin tests\n\n");

	readerClass = FindClass("Reader");
	writerClass = FindClass("Writer");
	if (!readerClass || !writerClass) {
		printf("Skin test needs the Reader and Writer classes, skipping\n");
		return;
	}

	/* nobody has skinned Reader yet - this should generate a default   */
	/* from the interface it already published, one Layout per property */
	skin = GetClassSkin(readerClass);
	text = NodeToText(skin);
	printf("Reader's generated default skin: %s\n", text);
	free(text);

	/* stand in for a hand-edited skin file */
	custom = NewNode(INTEGER);
	SetName(custom, "Skin");
	layout = NewNode(INTEGER);
	SetName(layout, "Layout");
	SetPropStr(layout, "Name", "Filename");
	SetPropStr(layout, "Label", "Input file");
	SetPropInt(layout, "X", 10);
	SetPropInt(layout, "Y", 20);
	SetPropStr(layout, "Style", "highlighted");
	AppendChild(custom, layout);

	text = NodeToText(custom);
	f = fopen("skintest.skin", "w");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
	free(text);
	DelNode(custom);

	/* loading should replace the generated default outright */
	skin = LoadSkin(readerClass, "skintest.skin");
	text = NodeToText(skin);
	printf("Reader's skin after loading a custom one: %s\n", text);
	free(text);

	printf("GetClassSkin now returns the loaded skin, not a fresh default: %d\n",
		   GetClassSkin(readerClass) == skin);

	/* a class nobody touched still gets its own independent default */
	skin = GetClassSkin(writerClass);
	text = NodeToText(skin);
	printf("Writer (untouched) still generates its own default: %s\n", text);
	free(text);
}

/* ---- copied verbatim from object.c ---- */
static int UT_PropertyWatchTestMessage;
static int UT_PropertyWatchTestValue;

/* the watcher's In handler - same msgobj shape as any real port handler, */
/* proving a plain property change reaches it exactly like a port would   */
int PropertyWatchTestOnIn(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance;

	UT_PropertyWatchTestMessage = message;
	UT_PropertyWatchTestValue = GetValueInt(data);	/* copy now - data is gone once this returns */
	return rtrn_handled;
}

/* ---- copied verbatim from dyn/bufftest.c ---- */


#define LOOP 10000

#define STRING_ENTRIES 5

int UT_BuffTest (){

	buff buffer;
	int i, j, k, length;
	char * resultString;
	int resultLength;

	/**********************/

	char tStr[STRING_ENTRIES][10000] = {
		"test\n",
		"01234567890\n",
		"abcdefghijklmnopqrstuvwxyz\n",
		"This is a very very very very very very very very very very very long line.\n",
		"<event originatingID=\"12345\",UnityEventID=\"3434346347\",UnityOriginator=\"QA Server\",EventCategory=\"NA\",UnityEventCategory=\"NetworkEvent\",PhysicalSource=\"Unitytest.singlestep.com\",OriginatingTime=\"05/31/2002 00:44:02\",Message=\"\",Severity=\"3\",Priority=\"1\",Assignee=\"None\",Count=\"1\",Description=\"new event 7 Something very very very very very very very naughty happenned on the network.\",State=\"UP\",Status=\"3\",Path=\"/EventGen-1.1!DBI!\",GenericKey=\"UnityStartingPolicyOutput\"/>\n"
	};

	/* int, not unsigned: these are compared against buffGetLine's int    */
	/* return everywhere below                                             */
	int tStrLen[STRING_ENTRIES];

	for (k=0; k < STRING_ENTRIES; k++)
		tStrLen[k] = strlen(tStr[k]);

	/**********************/

	if ( buffTotalCount (NULL)) {
		printf("T01: Failed the total count should be equal to 0 \n");
	}

	if ( buffTotalSize (NULL)) {
		printf("T02: Failed the total size would be equal to 0 \n");
	}

	printf("Creating a buffer of 0 bytes (will really create a buffer 1 byte long\n");
	buffer = buffCreate (0);
	if (!buffer) {
		printf("T03: Could not buffCreate (0).\n");
		exit (1);
	}

	if ( buffTotalCount () != 1) {
		printf("T04: Failed should be 1 \n");
		//exit (1);
	}

	if ( buffTotalSize () != 1) {
		printf("T05: Failed Should be 1 \n");
		exit (1);
	}

	/**********************/

	if ( buffCountResetEmpty (buffer)) {
		printf("Y39: Failed Should be zero \n");
		exit (1);
	}

	if ( buffCountMovedForward (buffer)) {
		printf("Y37: Failed Should be zero \n");
		exit (1);
	}

	if ( buffCountReallocs (buffer)) {
		printf("Y41: Failed Should be zero \n");
		exit (1);
	}

	if ( buffCountInserts (buffer)) {
		printf("Y43: Failed  Should be zero\n");
		exit (1);
	}

	if ( buffCountRetrievals (buffer)) {
		printf("Y45: Failed  Should be zero\n");
		exit (1);
	}

	/**********************/

	if ( buffDestroy (NULL)) {
		printf("A02: Failed buffDestroy must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	/**********************/

	if ( buffAdd (NULL, "test", 5)) {
		printf("A03: Failed buffAdd must return a 0 if given a NULL pointer. \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 0)) {
		printf("A04: Failed  \n");
		exit (1);
	}

	if ( buffAdd (buffer, "test", -1)) {
		printf("A05: Failed  \n");
		exit (1);
	}

	/* no real idea how to detect this */
	/* was crashing me later */
/*	if (!buffAdd (buffer, "", 5)) {
		printf("A06: Failed buffAdd(buffer, "", 5) should not work but does. \n");
		exit (1);
	}
*/

	if ( buffAdd (buffer, NULL, 5)) {
		printf("A07: Failed  must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	if ( buffGetLine (NULL, &resultString)) {
		printf("A08: Failed  must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	if ( buffGetLine (buffer, NULL)) {
		printf("A09: Failed  must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	/**********************/

	buffClear (buffer);
	if (!buffAdd (buffer, "\ntesta\rtestbb\r\ntestccc", 22)) {
		printf("A10: Failed  buffAdd should return a 1 on successful insert.\n");
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 1) {
		printf("A11: Failed buffGetLine() should return a 1 and a \\n here.\n");
		exit (1);
	}

	if (!memcmp("\n", &resultString, length)) {
		printf("A11a: Failed should be '%x' here, found '%x'.\n", '\n', *resultString);
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 6) {
		printf("A12: Failed buffGetLine() should return a 6 and a 'testa\\r' here.\n");
		exit (1);
	}

	if (!memcmp("testa\r", &resultString, length)) {
		printf("A12a: Failed should be 'testa\\r' here, found '%s'.\n", resultString);
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 8 ) {
		printf("A13: Failed buffGetLine() should return an 8 and a 'testbb\\r\\n' here.\n");
		exit (1);
	}

	if (!memcmp("testbb\r\n", &resultString, length)) {
		printf("A13a: Failed should be 'testbb\r\n' here, found '%s'.\n", resultString);
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) ) {
		printf("A14: Failed buffGetLine() should return a 0 and a '' here. found %s of len %d\n", resultString, length);
		exit (1);
	}

	if ( memcmp("", &resultString, length)) {
		printf("A14a: Failed should be '' here found '%s'.\n", resultString);
		exit (1);
	}

	if (!buffAdd (buffer, "\n", 1)) {
		printf("A14b: Failed  buffAdd should return a 1 on successful insert.\n");
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 8 ) {
		printf("A14c: Failed buffGetLine() should return an 8 and a 'testccc\\n' here.\n");
		exit (1);
	}

	if (!memcmp("testccc\n", &resultString, length)) {
		printf("A14d: Failed should be 'testccc\n' here, found '%s'.\n", resultString);
		exit (1);
	}

	if ( buffGetLine (buffer, &resultString)) {
		printf("A15: Failed buffGetLine() should return a 0 in an empty buffer.\n");
		exit (1);
	}

	/**********************/

	if ( buffGetBlockFromTail(NULL, &resultString, 5)) {
		printf("A16: Failed  \n");
		exit (1);
	}

	if ( buffGetBlockFromTail(buffer, NULL, 5)) {
		printf("A17: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A18: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromTail(buffer, &resultString, 4)) {
		printf("A19: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A20: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromTail(buffer, &resultString, 3)) {
		printf("A21: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A22: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromTail(buffer, &resultString, 10)) {
		printf("A23: Failed  \n");
		exit (1);
	}

	/**********************/

	if ( buffGetBlockFromHead (NULL, &resultString, 5)) {
		printf("A16: Failed  \n");
		exit (1);
	}

	if ( buffGetBlockFromHead (buffer, NULL, 5)) {
		printf("A17: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A18: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromHead (buffer, &resultString, 4)) {
		printf("A19: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A20: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromHead (buffer, &resultString, 3)) {
		printf("A21: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A22: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromHead (buffer, &resultString, 10)) {
		printf("A23: Failed  \n");
		exit (1);
	}

	/**********************/

/*
int
buffResize (buff buffer, unsigned int length);
*/
	if ( buffResize (NULL, 50)) {
		printf("H01: Failed  \n");
		exit (1);
	}

	if ( buffResize (NULL, 2000000)) {
		printf("H03: Failed  \n");
		exit (1);
	}

	if (!buffResize (buffer, 16000)) {
		printf("H04: Failed  \n");
		exit (1);
	}


	/**********************/

/*
unsigned int
buffReallocAdjustment (buff buffer, unsigned int newSize);
*/
	if (buffReallocAdjustment (NULL, 16000)) {
		printf("J01: Failed  \n");
		exit (1);
	}

	if (!buffReallocAdjustment (buffer, 16000)) {
		printf("J02: Failed  \n");
		exit (1);
	}

	if (buffReallocAdjustment (NULL, 1600000)) {
		printf("J03: Failed  \n");
		exit (1);
	}

	if (!buffReallocAdjustment (buffer, 16000)) {
		printf("J04: Failed  \n");
		exit (1);
	}


	/**********************/

	if ( buffGetBuffer (NULL, &resultString)) {
		printf("A24: Failed  \n");
		exit (1);
	}

	if ( buffGetBuffer (buffer, NULL)) {
		printf("A25: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A26: Failed  \n");
		exit (1);
	}

	if (!buffGetBuffer (buffer, &resultString)) {
		printf("A27: Failed  \n");
		exit (1);
	}

	/*********************

	if ( buffGetUndoTail (NULL, 1)) {
		printf("A28: Failed  \n");
		exit (1);
	}

	if (!buffGetUndoTail (buffer, 0)) {
		printf("A29: Failed  \n");
		exit (1);
	}

	if ( buffGetUndoTail (buffer, -1)) {
		printf("A30: Failed  \n");
		exit (1);
	}

	// actually rewind a character
	if (!buffGetUndoTail (buffer, 1)) {
		printf("A31: Failed  \n");
		exit (1);
	}
 */

	/**********************/

	if ( buffGetSize (NULL)) {
		printf("A32: Failed  \n");
		exit (1);
	}

	if (!buffGetSize (buffer)) {
		printf("A33: Failed  \n");
		exit (1);
	}

	/**********************/

	if ( buffGetLength (NULL)) {
		printf("A34: Failed  \n");
		exit (1);
	}

	if (!buffGetLength (buffer)) {
		printf("A35: Failed  \n");
		//exit (1);
	}

	/**********************/

	if ( buffCountMovedForward (NULL)) {
		printf("A36: Failed  \n");
		exit (1);
	}

	if ( buffCountResetEmpty (NULL)) {
		printf("A38: Failed  \n");
		exit (1);
	}

	if ( buffCountReallocs (NULL)) {
		printf("A40: Failed  \n");
		exit (1);
	}

	if ( buffCountInserts (NULL)) {
		printf("A42: Failed  \n");
		exit (1);
	}

	if ( buffCountRetrievals (NULL)) {
		printf("A44: Failed  \n");
		exit (1);
	}

	/**********************/

	if ( buffClear (NULL)) {
		printf("A48: Failed  \n");
		exit (1);
	}

	/* clean everything up */
	if (!buffClear (buffer)) {
		printf("A49: Failed  \n");
		exit (1);
	}

	/**********************/

	for (k=0; k < STRING_ENTRIES; k++) {

		printf("\nTesting is using this string:%s", tStr[k]);

/*
		printf("Inserting and removing %d lines, 10 times to test buffer growing functionality.\n", LOOP);
		for (j=0; j<10; j++){
			for (i = 0; i < LOOP; i++) {
				if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
					printf("M01: Failed Could not add a string.\n");
					exit (1);
				}
			}
			printf("+");
			fflush(NULL);
		
			printf("-");
			fflush(NULL);
		}
		printf("\n");
		fflush(NULL);
*/

		printf("Inserting and removing %d lines, 10 times to test buffer growing functionality.\n", LOOP);
		for (j=0; j<10; j++){
			for (i = 0; i < LOOP; i++) {
				if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
					printf("M01: Failed Could not add a string.\n");
					exit (1);
				}
			}
			printf("+");
			fflush(NULL);
		
			for (i=0; i < LOOP; i++) {
				if ((resultLength = buffGetLine(buffer, &resultString)) != tStrLen[k]){
					printf("M03: Failed on buffGetLine on line %d expected '%s' of length %d, got '%s' of length %d\n",
						i, tStr[k], tStrLen[k], resultString, resultLength);
					exit (1);
				}
			}
			printf("-");
			fflush(NULL);
		}
		printf("\n");
		fflush(NULL);
	
		printf("Inserting and removing %d lines, 30 times to test buffer move to front functionality.\n", LOOP/3);
		for (i = 0; i < LOOP/3; i++) {
			if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
				printf("M01: Failed Could not add a string.\n");
				exit (1);
			}
		}
		printf("+");
		fflush(NULL);

		for (j=0; j<29; j++){
			for (i = 0; i < LOOP/3; i++) {
				if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
					printf("M02: Failed Could not add a string.\n");
					exit (1);
				}
			}
			printf("+");
			fflush(NULL);
		
			for (i=0; i < LOOP/3; i++) {
				if ((resultLength = buffGetLine(buffer, &resultString)) != tStrLen[k]){
					printf("C03: Failed on buffGetLine on line %d expected '%s' of length %d, got '%s' of length %d\n",
						i, tStr[k], tStrLen[k], resultString, resultLength);
					exit (1);
				}
			}
			printf("-");
			fflush(NULL);
		}
		
		for (i=0; i < LOOP/3; i++) {
			if ((resultLength = buffGetLine(buffer, &resultString)) != tStrLen[k]){
				printf("C03: Failed on buffGetLine on line %d expected '%s' of length %d, got '%s' of length %d\n",
					i, tStr[k], tStrLen[k], resultString, resultLength);
				exit (1);
			}
		}
		printf("-");
		printf("\n");
		fflush(NULL);
	}
	printf("\n");

	if (!buffCountMovedForward (buffer)) {
		printf("Q01: Failed  \n");
	}

	if (!buffCountReallocs (buffer)) {
		printf("Q02: Failed  \n");
	}

	if (!buffCountInserts (buffer)) {
		printf("Q03: Failed  \n");
	}

	if (!buffCountRetrievals (buffer)) {
		printf("Q04: Failed  \n");
	}

	printf("Destroying a buffer.\n");
	if (!buffDestroy(buffer)) {
		printf("Q05: Failed Could not buffDestroy\n");
	}

	if ( buffTotalCount ()) {
		printf("Q06: Failed the total count should be equal to 0 \n");
	}

	if ( buffTotalSize ()) {
		printf("Q07: Failed the total size would be equal to 0 \n");
	}

	printf("Completed tests.\n");
	return 0;
}

/* ---- copied verbatim from object.c (3339-3346) ---- */
int UT_PropertyWatchTestOnIn(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance;

	UT_PropertyWatchTestMessage = message;
	UT_PropertyWatchTestValue = GetValueInt(data);	/* copy now - data is gone once this returns */
	return rtrn_handled;
}

/* ---- copied verbatim from object.c (3349-3377) ---- */
void UT_PropertyWatchTest(){

	NodeObj source, watcher, port;

	printf("\n\nRunning property watch tests\n\n");

	source = NewNode(INTEGER);
	SetName(source, "Source");
	SetPropInt(source, "Level", 0);
	WatchableProp(source, "Level");

	watcher = NewNode(INTEGER);
	SetName(watcher, "Watcher");
	SetPropInt(watcher, "In", 0);
	port = GetPropNode(watcher, "In");
	SetPropLong(port, "OnMsg", (long) UT_PropertyWatchTestOnIn);

	Connect(source, "Level", watcher, "In");

	SetPropInt(source, "Level", 42);

	printf("Watcher saw the property change: message=%d value=%d\n",
		   UT_PropertyWatchTestMessage, UT_PropertyWatchTestValue);
	printf("Property still reads correctly after the watched write: %d\n",
		   GetPropInt(source, "Level"));

	DelNode(source);
	DelNode(watcher);
}

/* ---- copied verbatim from node.c (1332-1432) ---- */
void UT_InterceptTest(void)
{
	NodeObj node, propnode, sub, before, after;

	printf("\n\nRunning subscriber fan-out tests\n\n");

	/* a property is watchable simply by existing - no opt-in step needed. */
	/* This attaches the exact same "Subscriber" child AddSubscription      */
	/* (object.c) would, straight onto the property node, to prove          */
	/* SetProp* fans out on its own with nothing else involved.              */
	node = NewNode(INTEGER);
	SetName(node, "SubscriberHolder");
	SetPropInt(node, "Value", 10);

	propnode = GetPropNode(node, "Value");
	sub = NewNode(INTEGER);
	SetName(sub, "Subscriber");
	SetPropLong(sub, "Instance", (long) node);
	SetPropLong(sub, "Callback", (long) UT_SubscriberTestCallback);
	AddProp(propnode, sub);

	SetPropInt(node, "Value", 5);
	printf("Plain write fans out to its subscriber unasked: %d (saw value %d, message %d)\n",
		   UT_SubscriberTestSeen == 5 && UT_SubscriberTestMessage == msg_change,
		   UT_SubscriberTestSeen, UT_SubscriberTestMessage);

	DelNode(node);

	/* same property node identity should survive a plain (non-intercepted) */
	/* write now too - this is the shadow-vs-update fix, not the intercept  */
	node = NewNode(INTEGER);
	SetName(node, "PlainHolder");
	SetPropInt(node, "Foo", 1);
	before = GetPropNode(node, "Foo");

	SetPropInt(node, "Foo", 2);
	after = GetPropNode(node, "Foo");

	printf("Plain write updates in place instead of shadowing: %d (value now %d)\n",
		   before == after, GetPropInt(node, "Foo"));

	DelNode(node);

	/* the trap the fix above walks into: FlowSetProp always calls        */
	/* SetPropStr regardless of the target's native type (relying on the  */
	/* "intelligent data object" auto-conversion), so an update-in-place  */
	/* that writes through the wrong type's setter must not corrupt the   */
	/* other representations - this is exactly what broke Pulse's Count   */
	/* the first time this went in: SetPropStr("Count","1") on an         */
	/* INTEGER-native property silently zeroed it back to "pulses forever"*/
	node = NewNode(INTEGER);
	SetName(node, "CrossTypeHolder");
	SetPropInt(node, "Count", 0);
	SetPropStr(node, "Count", "1");

	printf("Cross-type write (SetPropStr onto an INTEGER prop) reads back correctly: %d (value now %d)\n",
		   GetPropInt(node, "Count") == 1, GetPropInt(node, "Count"));

	DelNode(node);
}

void UT_LinkTest(void)
{
	NodeObj original, aliasProp, second;

	printf("\n\nRunning node link (symlink) tests\n\n");

	original = NewNode(STRING);
	SetName(original, "Original");
	SetValueStr(original, "truth");

	aliasProp = NewNode(STRING);
	SetName(aliasProp, "Alias");
	LinkNode(aliasProp, original);

	printf("Plain node resolves to itself: %d\n", ResolveNode(original) == original);
	printf("Linked node resolves to its target: %d\n", ResolveNode(aliasProp) == original);
	printf("Resolved value reads the original: %d (%s)\n",
		strcmp(GetValueStr(ResolveNode(aliasProp)), "truth") == 0,
		GetValueStr(ResolveNode(aliasProp)));

	/* chains collapse: an alias of an alias reaches the final target */
	second = NewNode(STRING);
	SetName(second, "SecondAlias");
	LinkNode(second, aliasProp);
	printf("Chained link resolves to the final target: %d\n", ResolveNode(second) == original);

	/* a cycle degrades into 'stops resolving' instead of hanging */
	LinkNode(original, second);
	printf("Cycle survives resolution (depth cap): %d\n", ResolveNode(second) != NULL);
	LinkNode(original, NULL);

	printf("Unlink restores self-resolution: %d\n",
		(LinkNode(second, NULL), ResolveNode(second) == second));

	DelNode(second);
	DelNode(aliasProp);
	printf("Deleting a link never touches the target: %d (%s)\n",
		strcmp(GetValueStr(original), "truth") == 0, GetValueStr(original));
	DelNode(original);
}

/* the same list, in the same order, as PerformTesting() in libframework */
void UT_PerformTesting(){
	DebugPrint ( "Entering Perform Testing function.", __FILE__, __LINE__, PROG_FLOW);
	UT_DataTest();
	UT_NodeTest();
	UT_PropertyWatchTest();
	UT_BuffTest();
	//UT_NameSpaceTest();
	/* UT_SchedTest();  NOT COMPILED: needs sched.c internals (TaskPtr, static testcallback) */
}

int main ( int argc, char* argv[] ){

	NodeObj Main = NewNode(INTEGER);

	/* Main is a real place, not a null. The app's plumbing - the TCP,
	   Router, Http, WebSocket and Bridge the web flow is made of - is
	   created IN it, which is why it needs a name and a path like
	   anything else. It is not a view: nothing on a canvas lives here,
	   which is exactly the point - plumbing is somewhere, just not
	   somewhere anyone looks at. */
	SetName(Main, "Main");
	SetPropStr(Main, "Name", "Main");
	SetPropStr(Main, "Container", "");
	RegisterPath("/Main", Main);

	SetPropInt(Main, "State", Starting);

	TimeUpdate();

	DebugPrint ( "Entering Main", __FILE__, __LINE__, PROG_FLOW);

	ProcessCmdLine(Main, argc, argv);

	Init(Main);

	/* ------------------------------------------------------------------
	   The modules load here, same as any host.

	   Whether the framework CAN load modules is mechanism; WHICH ones, and
	   from where, is policy the host decides. That argument has been settled
	   in the core's favour twice today. The scan is `objects/` rather than a
	   recursive walk down from the working directory, so a build tree or a
	   test run holding copies of the modules no longer gets them loaded a
	   second time. And the core's own classes are registered by
	   ObjSetRegObjList - the moment the core is handed a registry - rather
	   than by whichever host remembered to call RegisterCoreClasses.

	   That second one is why this file exists. RegisterCoreClasses used to
	   live in main.c's InstallObjects, and THIS host has its own copy of
	   InstallObjects, which did not have the call. Result: modules loaded,
	   no Object class, every module's dependency on it unmet, and not one
	   class started - silently. A second host on the same library is the only
	   thing that could have found that, and finding things like it is the
	   whole point of building one.

	   What loading modules costs this harness: with instances alive, tasks
	   are armed, so "0 tasks left on the list" at exit no longer means only
	   the core behaved. Read a leftover task as a question about whichever
	   object armed it, not automatically as a core leak. The three tests that
	   need registered classes - UT_FlowTest, UT_InterfaceTest, UT_SkinTest -
	   can actually run now instead of skipping themselves.
	   ------------------------------------------------------------------ */
	InstallObjects();

	/* the palette build makes one inert instance per class, and each of
	   those parks its settings controls in a stash view - name it now */
	SetSettingsHome(CreateRoot("Initialize"));

	/* one inert instance per registered class, so a connecting client's */
	/* palette is real instances to walk, not a class-description dump  */
	/* (see BuildPalette's own comment in control.c) - also needs every   */
	/* class already loaded, same reason FlowTest waits below            */
	/* NO BuildPalette()/BuildChrome() here, unlike main.c.
	   Both exist to give a CONNECTING CLIENT something real to walk - one
	   inert instance per registered class, and the topbar's menus. This host
	   has no bridge and no web flow, so nothing can ever connect: the palette
	   build stood up fifty instances and armed twenty-eight deferred panel
	   builds for an audience of nobody, and every one of those tasks was then
	   counted against the core at exit.
	   They are app furniture. A host that is not the app does not want them,
	   which is the same reason they no longer live in object.c. */

	/* needs the classes InstallObjects() just loaded, so it can't run   */
	/* from PerformTesting() inside Init() alongside the other -t tests  */
	if (GetValueInt(GetPropNode(Main, "UnitTest"))) {
		UT_FlowTest(Main);
		UT_InterfaceTest();
		UT_SkinTest();
	}

	DebugPrint ( "Entering Main Loop.", __FILE__, __LINE__, PROG_FLOW);

	while(IsRunning(Main)>0){
		unsigned long wake;

		MainLoop(Main);

		/* sleep exactly until the next scheduled task is due instead of  */
		/* polling on a fixed interval - SchedNextWakeMicros reads the    */
		/* (already time-sorted) list's head, so this is 0 during any     */
		/* burst of due-now work and only actually sleeps once the list   */
		/* is caught up.                                                   */
		/*                                                                  */
		/* Capped at 1ms, and it has to be a small cap: nothing in this    */
		/* fabric is interrupt-driven, every I/O source (TCP included)     */
		/* is a polling task that only notices new data when its own turn */
		/* comes up. usleep() can't be woken early by a socket becoming    */
		/* readable, so however long this sleeps is a floor on input      */
		/* latency for anything arriving with nothing else already due -  */
		/* confirmed live: three bound sliders felt chunky against a bare */
		/* uncapped wake, silky smooth once something was due every 1ms   */
		/* (a fast Pulse). 1ms is that same ceiling applied unconditionally*/
		/* instead of relying on a coincidentally-busy scheduler.         */
		wake = SchedNextWakeMicros(Tasks);
		if (wake > 1000UL)
			wake = 1000UL;
		usleep(wake);
	}

	DebugPrint ( "No more tasks scheduled, cleaning up and exiting", __FILE__, __LINE__, PROG_FLOW);

	/* Count only AFTER the loop has drained. The framework's own shutdown is
	   the wait: MainLoop flips State to Stopping the moment ExecTasks finds
	   nothing due, so a flow a test started - UT_FlowTest arms a Pulse with
	   Count=1 - runs to completion first, and its tasks retire the way they
	   would in any other host. Counting before the loop meant every test that
	   started anything was guaranteed to "leak" it. What is still on the list
	   here is something that never finished, which is the thing worth
	   reporting. */
	{
		int left = UT_ReportLeftoverTasks(Tasks);

		printf("%s: %d task%s left on the list\n", left ? "FAIL" : "ok",
			   left, left == 1 ? "" : "s");
		if (left)
			return left > 254 ? 254 : left;		/* the return code IS the count */
	}


	if (GetValueInt(GetPropNode(Main, "PrintNodes")) !=0) {
		DebugPrint ( "Dumping Main Node on exit because -p was passed on command line.\n", __FILE__, __LINE__, PROG_FLOW);
		PrintNode(Main);
	}

	return 0;
}

