#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "node.h"
#include "list.h"
#include "sched.h"
#include "deamon.h"
#include "dirscan.h"

#include "object.h"
#include "timer.h"
#include "libload.h"
#include "dyn/bufftest.h"
#include "DebugPrint.h"
#include "namespace.h"

#include "version.h"

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

Hard coded test flow: emulate cat.

The Reader reads test.txt a chunk at a time and sends each chunk as a
message out its Out port.  The Writer's In port subscribes to that Out,
buffers what arrives, and drains it to test.txt.back.

At end of file the Reader sends EOF out the same port and deactivates.
When the Writer has the EOF and its write buffers are done it closes
the file and deactivates too.  Nobody calls exit: with both objects
quiet the task list empties and the main loop shuts the program down.

*/
void CreateTestApp(NodeObj Main){

	NodeObj Reader, Writer, Probe, StateProbe;

	/* recorded as a flow script so this cat flow can be saved and       */
	/* reloaded instead of hard-coded here - see default.flow after a    */
	/* run. Flow* wrappers have the exact same live effect as the plain  */
	/* CreateObject/SetPropStr/Connect/ActivateInstance calls they wrap. */
	NodeObj flow = NewFlow("cat");

	Reader = FlowCreateObject(flow, Main, "Reader");
	Writer = FlowCreateObject(flow, Main, "Writer");

	if (!Reader || !Writer) {
		DebugPrint ( "Test flow needs the Reader and Writer classes, skipping.", __FILE__, __LINE__, ERROR);
		return;
	}

	FlowSetProp(flow, Reader, "Filename", "test.txt");
	FlowSetProp(flow, Writer, "Filename", "test.txt.back");

	/* the sink subscribes to the source */
	FlowConnect(flow, Reader, "Out", Writer, "In");

	/* debug probe: a second subscriber on the same port, it prints    */
	/* every message that passes and proves the fan out routing works  */
	Probe = FlowCreateObject(flow, Main, "Out");
	if (Probe) {
		FlowSetProp(flow, Probe, "Label", "reader.Out");
		FlowConnect(flow, Reader, "Out", Probe, "In");
		FlowActivateInstance(flow, Probe);
	}

	/* proving Phase 2.4's skin works: State is a plain property, not a  */
	/* port, but WatchableProp made it Connect()-able exactly like one - */
	/* this probe should print Reader's Starting/Running/Stopping        */
	/* transitions live, the same way reader.Out prints its data         */
	StateProbe = FlowCreateObject(flow, Main, "Out");
	if (StateProbe) {
		FlowSetProp(flow, StateProbe, "Label", "reader.State");
		FlowConnect(flow, Reader, "State", StateProbe, "In");
		FlowActivateInstance(flow, StateProbe);
	}

	/* open the sinks first so they never miss a chunk, then start the source */
	FlowActivateInstance(flow, Writer);
	FlowActivateInstance(flow, Reader);

	SaveFlow(flow, "default.flow");

	/* second flow: a pulse train fanned out to a probe and to a filter,   */
	/* with a second pulse driving the filter's Enable line                */
	/*                                                                     */
	/*   Pulse (200ms, 3) --+--> probe "pulse.Out"                         */
	/*                      +--> Filter (ones) --> probe "filter.Out"      */
	/*   Gate (450ms, 1) -------> Filter.Enable                            */
	/*                                                                     */
	/* pulse ones arrive at 200, 600, 1000 ms                              */
	/* the gate sends 1 at 450 (already enabled) and 0 at 900 (disable)    */
	/* so filter.Out should show the ones from 200 and 600, drop the one   */
	/* from 1000, and still pass the EOF at 1200: "after 2 messages"       */
	/* this is the same wiring a 30 second timer will use to shut down     */
	/* the TCP object later: anything can drive anything's Enable          */
	/* the queue rides the same pulse: edges are pushed as they arrive   */
	/* and a slower clock pulse pops them back out one per tick          */
	/*                                                                   */
	/*   Pulse ----------+--> Queue.In                                   */
	/*   Clock (350ms,4) ----> Queue.Clock                               */
	/*   Queue.Out ----------> probe "queue.Out"                         */
	/*                                                                   */
	/* pushes land at 200..1200ms, pops at 350,700,...  so queue.Out     */
	/* replays 1 0 1 0 1 0 at the clock's pace and pops the in band      */
	/* EOF at about 2450ms: "after 6 messages, 6 bytes"                  */
	{
		NodeObj Pulse, PulseProbe, Filter, FilterProbe, Gate;
		NodeObj Queue, QueueProbe, Clock;
		NodeObj FeedProbe, Stack, StackProbe;

		Pulse       = CreateObject(Main, "Pulse");
		PulseProbe  = CreateObject(Main, "Out");
		Filter      = CreateObject(Main, "Filter");
		FilterProbe = CreateObject(Main, "Out");
		Gate        = CreateObject(Main, "Pulse");
		Queue       = CreateObject(Main, "Queue");
		QueueProbe  = CreateObject(Main, "Out");
		Clock       = CreateObject(Main, "Pulse");
		FeedProbe   = CreateObject(Main, "Out");
		Stack       = CreateObject(Main, "Stack");
		StackProbe  = CreateObject(Main, "Out");

		if (Pulse && PulseProbe && Filter && FilterProbe && Gate
			&& Queue && QueueProbe && Clock
			&& FeedProbe && Stack && StackProbe) {

			SetPropInt(Pulse, "Interval", 200);
			SetPropInt(Pulse, "Count", 3);
			SetPropStr(PulseProbe, "Label", "pulse.Out");

			SetPropStr(Filter, "Mode", "ones");
			SetPropStr(FilterProbe, "Label", "filter.Out");

			SetPropInt(Gate, "Interval", 450);
			SetPropInt(Gate, "Count", 1);

			SetPropStr(QueueProbe, "Label", "queue.Out");
			SetPropInt(Clock, "Interval", 350);
			SetPropInt(Clock, "Count", 4);

			/* the same pulse feeds the queue and the stack, this  */
			/* probe shows exactly what both of them receive       */
			SetPropStr(FeedProbe, "Label", "queue.In");
			SetPropStr(StackProbe, "Label", "stack.Out");

			Connect(Pulse, "Out", PulseProbe, "In");
			Connect(Pulse, "Out", Filter, "In");
			Connect(Filter, "Out", FilterProbe, "In");
			Connect(Gate, "Out", Filter, "Enable");

			Connect(Pulse, "Out", FeedProbe, "In");
			Connect(Pulse, "Out", Queue, "In");
			Connect(Clock, "Out", Queue, "Clock");
			Connect(Queue, "Out", QueueProbe, "In");

			/* the stack rides the same feed and the same clock,   */
			/* it pops newest first, so expect the stream reversed */
			/* piecewise, EOF included: that is what stacks do     */
			Connect(Pulse, "Out", Stack, "In");
			Connect(Clock, "Out", Stack, "Clock");
			Connect(Stack, "Out", StackProbe, "In");

			/* sinks and middle objects first, sources last */
			ActivateInstance(PulseProbe);
			ActivateInstance(FilterProbe);
			ActivateInstance(QueueProbe);
			ActivateInstance(FeedProbe);
			ActivateInstance(StackProbe);
			ActivateInstance(Filter);
			ActivateInstance(Queue);
			ActivateInstance(Stack);
			ActivateInstance(Gate);
			ActivateInstance(Clock);
			ActivateInstance(Pulse);
		}
	}

	/* third flow: a TCP echo server on port 8094 with a 30 second life   */
	/*                                                                    */
	/*   TCP (8094) --+--> probe "tcp.Out"                                */
	/*                +--> back into TCP.In (echo to the peer)            */
	/*   Timer (15s, 1) --> TCP.Enable                                    */
	/*                                                                    */
	/* everything a peer sends prints on the probe and echoes back        */
	/* the timer's rising edge at 15s is a no-op enable, its falling      */
	/* edge lands at 30 seconds and shuts the server down completely,     */
	/* so the program keeps itself alive exactly 30 seconds and then      */
	/* quiesces like every other flow                                     */
	{
		NodeObj Tcp, TcpProbe, Timer;

		Tcp      = CreateObject(Main, "TCP");
		TcpProbe = CreateObject(Main, "Out");
		Timer    = CreateObject(Main, "Pulse");

		if (Tcp && TcpProbe && Timer) {

			SetPropInt(Tcp, "LocalPort", 8094);
			SetPropStr(TcpProbe, "Label", "tcp.Out");

			SetPropInt(Timer, "Interval", 15000);
			SetPropInt(Timer, "Count", 1);

			Connect(Tcp, "Out", TcpProbe, "In");
			Connect(Tcp, "Out", Tcp, "In");		/* echo everything back */
			Connect(Timer, "Out", Tcp, "Enable");

			ActivateInstance(TcpProbe);
			ActivateInstance(Tcp);
			ActivateInstance(Timer);
		}
	}

	/* fourth flow: the web GUI, HTTP and WebSocket sharing one TCP port - */
	/* Phases 3.1/3.2/3.3 together, and what a browser (or a firewall     */
	/* with exactly one hole punched in it) actually talks to. Router     */
	/* sniffs each connection's first message for the WebSocket upgrade   */
	/* header and sends it to Http or to WebSocket->Bridge accordingly;   */
	/* neither of those two is aware the other, or Router, exists. No     */
	/* shutdown timer - this is meant to stay up for as long as someone   */
	/* is actually browsing it; Ctrl+C stops it, same as any dev server   */
	/*                                                                    */
	/*   TCP (-ip/-port, default 0.0.0.0:8083) --> Router                 */
	/*                           +--> Http (Root="web") --> TCP.In        */
	/*                           +--> WebSocket.Wire                      */
	/*                                 WebSocket.Send --> TCP.In          */
	/*                                 WebSocket.Out --> Bridge.In        */
	/*                                 Bridge.Out --> WebSocket.In        */
	/*                                              +--> probe "web.Out"  */
	{
		NodeObj WebTcp, Router, Http, Ws, WebBridge, WebProbe, FileMenu;

		WebTcp    = CreateObject(Main, "TCP");
		Router    = CreateObject(Main, "Router");
		Http      = CreateObject(Main, "Http");
		Ws        = CreateObject(Main, "WebSocket");
		WebBridge = CreateObject(Main, "Bridge");
		WebProbe  = CreateObject(Main, "Out");

		if (WebTcp && Router && Http && Ws && WebBridge && WebProbe) {

			SetPropStr(WebTcp, "LocalAddr", GetPropStr(Main, "ip"));
			SetPropInt(WebTcp, "LocalPort", GetPropInt(Main, "port"));
			SetPropStr(Http, "Root", "web");
			SetPropStr(WebProbe, "Label", "web.Out");
			/* off by default: list-instances replays every existing   */
			/* instance (including every hidden helper widget) to each */
			/* freshly-connecting client, and a grown session.flow      */
			/* makes that dump big enough to flood stdout on every page */
			/* load/reconnect. Still a real Echo checkbox in the UI -   */
			/* flip it on from there when actually debugging this wire. */
			SetPropInt(WebProbe, "Echo", 0);

			SetPropLong(Router, "HttpTarget", (long) Http);
			SetPropLong(Router, "WsTarget", (long) Ws);

			Connect(WebTcp, "Out", Router, "Wire");
			Connect(Http, "Out", WebTcp, "In");
			Connect(Ws, "Send", WebTcp, "In");
			Connect(Ws, "Out", WebBridge, "In");
			Connect(WebBridge, "Out", Ws, "In");
			Connect(WebBridge, "Out", WebProbe, "In");

			/* the File menu's Selected drives Bridge's own Save/Load/  */
			/* Import handling - an ordinary property wired to an       */
			/* ordinary port, same as anything else (see                */
			/* Bridge_OnFileCmd's doc comment, bridge.c)                 */
			FileMenu = (NodeObj) GetPropLong(GetChrome(), "FileMenu");
			if (FileMenu) {
				SetPropLong(WebBridge, "FileMenu", (long) FileMenu);
				Connect(FileMenu, "Selected", WebBridge, "FileCmd");
			}

			ActivateInstance(WebProbe);
			ActivateInstance(WebBridge);
			ActivateInstance(Ws);
			ActivateInstance(Http);
			ActivateInstance(Router);
			ActivateInstance(WebTcp);
		}
	}

	/* fifth flow: the Bridge control protocol, also on top of TCP - the  */
	/* roadmap's Phase 3.3. A remote client sends JSON commands and gets  */
	/* JSON events back; every command is a direct CreateObject/Connect/  */
	/* SetPropStr/ActivateInstance call, same as everywhere else in this  */
	/* file - the protocol is a thin veneer, not a second implementation  */
	/*                                                                    */
	/*   TCP (8091) --> Bridge --+--> back into TCP.In (events to client) */
	/*                           +--> probe "bridge.Out" (events on the   */
	/*                                server's own log, same fan-out      */
	/*                                reader.Out already proved)          */
	/*   Timer (20s, 1) --> TCP.Enable                                    */
	{
		NodeObj BridgeTcp, Bridge, BridgeProbe, BridgeTimer;

		BridgeTcp   = CreateObject(Main, "TCP");
		Bridge      = CreateObject(Main, "Bridge");
		BridgeProbe = CreateObject(Main, "Out");
		BridgeTimer = CreateObject(Main, "Pulse");

		if (BridgeTcp && Bridge && BridgeProbe && BridgeTimer) {

			SetPropInt(BridgeTcp, "LocalPort", 8091);
			SetPropStr(BridgeProbe, "Label", "bridge.Out");
			SetPropInt(BridgeProbe, "Echo", 0);	/* see web.Out comment above */

			SetPropInt(BridgeTimer, "Interval", 20000);
			SetPropInt(BridgeTimer, "Count", 1);

			Connect(BridgeTcp, "Out", Bridge, "In");
			Connect(Bridge, "Out", BridgeTcp, "In");
			Connect(Bridge, "Out", BridgeProbe, "In");
			Connect(BridgeTimer, "Out", BridgeTcp, "Enable");

			ActivateInstance(BridgeProbe);
			ActivateInstance(Bridge);
			ActivateInstance(BridgeTcp);
			ActivateInstance(BridgeTimer);
		}
	}

	/* sixth flow: the Bridge with RequireAuth on - the roadmap's         */
	/* Phase 3.5. One registered test user, Main/Users/jim, token         */
	/* "secret". Every command but login gets rejected until it succeeds. */
	/*                                                                    */
	/*   TCP (8093) --> Bridge (RequireAuth=1, Main=Main) --+--> TCP.In   */
	/*                                                       +--> probe   */
	/*   Timer (20s, 1) --> TCP.Enable                                    */
	CreateUser(Main, "jim", "secret");
	{
		NodeObj AuthTcp, AuthBridge, AuthProbe, AuthTimer;

		AuthTcp    = CreateObject(Main, "TCP");
		AuthBridge = CreateObject(Main, "Bridge");
		AuthProbe  = CreateObject(Main, "Out");
		AuthTimer  = CreateObject(Main, "Pulse");

		if (AuthTcp && AuthBridge && AuthProbe && AuthTimer) {

			SetPropInt(AuthTcp, "LocalPort", 8093);
			SetPropStr(AuthBridge, "RequireAuth", "1");
			SetPropLong(AuthBridge, "Main", (long) Main);
			SetPropStr(AuthProbe, "Label", "authbridge.Out");
			SetPropInt(AuthProbe, "Echo", 0);	/* see web.Out comment above */

			SetPropInt(AuthTimer, "Interval", 20000);
			SetPropInt(AuthTimer, "Count", 1);

			Connect(AuthTcp, "Out", AuthBridge, "In");
			Connect(AuthBridge, "Out", AuthTcp, "In");
			Connect(AuthBridge, "Out", AuthProbe, "In");
			Connect(AuthTimer, "Out", AuthTcp, "Enable");

			ActivateInstance(AuthProbe);
			ActivateInstance(AuthBridge);
			ActivateInstance(AuthTcp);
			ActivateInstance(AuthTimer);
		}
	}
}

/* return the current status of the Main execution thread */
int IsRunning(NodeObj Main){
	return (GetInt((DataObj)GetValueNode(GetPropNode(Main, "State"))));
}

/* Load in a default application */
void LoadDefaultApp(NodeObj Main){
	DebugPrint ( "Entering Default Application function.", __FILE__, __LINE__, PROG_FLOW);
	CreateTestApp(Main);
}

void PerformTesting(){
	DebugPrint ( "Entering Perform Testing function.", __FILE__, __LINE__, PROG_FLOW);
	DataTest();
	NodeTest();
	PropertyWatchTest();
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
		PerformTesting();
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

int main ( int argc, char* argv[] ){

	NodeObj Main = NewNode(INTEGER);

	SetPropInt(Main, "State", Starting);

	TimeUpdate();

	DebugPrint ( "Entering Main", __FILE__, __LINE__, PROG_FLOW);

	ProcessCmdLine(Main, argc, argv);

	Init(Main);

	InstallObjects();

	/* one inert instance per registered class, so a connecting client's */
	/* palette is real instances to walk, not a class-description dump  */
	/* (see BuildPalette's own comment in object.c) - also needs every   */
	/* class already loaded, same reason FlowTest waits below            */
	BuildPalette();

	/* the topbar's own File/Mode menus - real instances too, discovered */
	/* the same way the Palette is (see BuildChrome's comment, object.c) */
	BuildChrome();

	/* needs the classes InstallObjects() just loaded, so it can't run   */
	/* from PerformTesting() inside Init() alongside the other -t tests  */
	if (GetValueInt(GetPropNode(Main, "UnitTest"))) {
		FlowTest(Main);
		InterfaceTest();
		SkinTest();
	}

	LoadDefaultApp(Main);

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

	if (GetValueInt(GetPropNode(Main, "PrintNodes")) !=0) {
		DebugPrint ( "Dumping Main Node on exit because -p was passed on command line.\n", __FILE__, __LINE__, PROG_FLOW);
		PrintNode(Main);
	}

	return 0;
}

