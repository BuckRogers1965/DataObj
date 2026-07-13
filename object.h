#ifndef Object_H_
#define Object_H_



typedef int MsgId;

typedef int(*msgobj)(NodeObj instance, MsgId message, NodeObj data);

enum {Stopping=0, Starting, Running};


/* create a container to hold object instances */
/* all containers must be part of another container */
NodeObj
CreateContainer(NodeObj node, char * name);

/* users as nodes: Main/Users/<name>, each with its own Canvas container */
NodeObj CreateUser(NodeObj main, char * name, char * token);
NodeObj FindUser(NodeObj main, char * name);
/* the user node on success, NULL on an unknown user or a token mismatch */
NodeObj AuthenticateUser(NodeObj main, char * name, char * token);

/* create an object instance of named class in the given container */
NodeObj
CreateObject(NodeObj container, char * classname);

/* the registry root - RegObjList -> libraries -> classes - walk every  */
/* class (a palette) with GetChild/GetNextSibling at both levels        */
NodeObj GetRegObjList(void);

/* the palette is a real View instance (see BuildPalette's own doc         */
/* comment, object.c) holding one inert (never Activated) bootstrap         */
/* instance per registered class as ordinary Container'd children. Built    */
/* once, after all classes are loaded (see main.c). GetPalette() is the     */
/* class-name -> instance lookup table (Bridge's seeding source);           */
/* GetPaletteView() is the View itself.                                     */
NodeObj GetPalette(void);
NodeObj GetPaletteView(void);
void BuildPalette(void);

/* the app's own chrome, addressed and broadcast the exact same way the   */
/* Palette is - a handful of well-known, always-present instances (the    */
/* topbar's File/Mode menus) rather than a separate client-side concept.  */
/* Built once, after BuildPalette (its classes, e.g. MenuButton, must     */
/* already be registered). GetChrome() holds one long property per        */
/* well-known name ("FileMenu","ModeMenu"), same shape as GetPalette().   */
NodeObj GetChrome(void);
void BuildChrome(void);

/* remove an instance for good - UnRegisterInstance plus DelNode. Callers */
/* that also track the instance by alias (Bridge) must drop their own      */
/* reference too; this only unwinds the registry/tree side.                */
void DeleteInstance(NodeObj instance);

/* Connect two properties between two object instances */
/* the sink's "to" port subscribes to the source's "from" port */
int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to);

/* Send a message out a named port of an instance. */
/* The message is routed to every subscriber of that port. */
/* Returns the number of subscribers it was delivered to.  */
int
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data);

/* Deliver straight to one named port's own handler, bypassing whatever  */
/* Subscriber list is attached to it - for something that decides at     */
/* delivery time which single target gets a message (Router), rather    */
/* than a fixed Connect()'d wire. Returns 1 if delivered, 0 if the       */
/* target has no such port or no handler on it.                         */
int
DeliverMsg(NodeObj target, char * port, MsgId message, NodeObj data);

/* Call the Activate function an instance registered on itself */
int
ActivateInstance(NodeObj instance);

/* Write a named property on target the way something OUTSIDE the object */
/* has to: if the name resolves to a port (an OnMsg handler is present,  */
/* e.g. Enable), deliver a message so the port's own handler actually    */
/* runs, exactly like genuine Connect()'d traffic would - a bare         */
/* SetPropStr would only overwrite the port's raw text and change        */
/* nothing the object is actually gated on. Otherwise it's a plain data  */
/* property and a direct SetPropStr is correct.                          */
void
SetOrDeliverProp(NodeObj target, char * propname, char * value);

/* Resolve (instance, propname) through any alias link chain to the pair */
/* that actually owns the property - *instp is rewritten to the owning   */
/* instance when the name is a link. Plain properties behave exactly     */
/* like GetPropNode. Every port-resolution choke point uses this, which  */
/* is the entire alias mechanism.                                         */
NodeObj ResolvePort(NodeObj * instp, char * name);

/* Expose targetInst's property on owner as a link - value, subscribers, */
/* and wiring all stay on the original. Chains collapse to the final     */
/* original at creation. Returns 0 if targetInst has no such property.   */
/* The ...As form names the local slot: an Alias keeps its link in its    */
/* own "Value" slot so its own Name/Container/X/Y stay its own.           */
int LinkPropertyAs(NodeObj owner, char * slot, NodeObj targetInst, char * propname);
int LinkProperty(NodeObj owner, NodeObj targetInst, char * propname);

/* A brand-new instance of source's class carrying a snapshot of its     */
/* published data - the engine-level clone. Naming/containment/eventing  */
/* are the caller's business.                                              */
NodeObj CloneObject(NodeObj source);

/* Wire fromInst's fromPort to an arbitrary, chosen-at-runtime property */
/* on targetInst (something Connect() alone can't do - see the comment */
/* above its definition in object.c). Returns the adapter node (keep   */
/* it alive for the life of the wiring) or NULL if fromPort is bad.    */
NodeObj ConnectToProperty(NodeObj fromInst, char * fromPort, NodeObj targetInst, char * targetProp);

/* Same, but for reaching targetInst's ActivateInstance instead of a   */
/* property - what a Button's Out wires to.                            */
NodeObj ConnectToActivate(NodeObj fromInst, char * fromPort, NodeObj targetInst);

/* One row of a settings panel: which control class represents a named  */
/* property/port on the object that owns this table, and where it sits. */
/* The C-side answer to "make objects control their own presentation" -  */
/* the shape is the VNOS panel-builder pattern (objects/demo/            */
/* pulsegenerator/pulsepb.c's ControlInfo[]: control class, bound        */
/* variable, X, Y, W, H per row) brought inward: an object's own         */
/* ClassStart/InstanceStart declares this table directly instead of a    */
/* client inferring a layout from a Widget-type constant. property is    */
/* unused for a Button row - it always reaches the target's Activate,    */
/* never a named property.                                               */
typedef struct ControlSpec
{
	char *controlClass;
	char *property;
	int   x, y, w, h;
} ControlSpec;

/* Builds target's settings panel, populated per specs: creates each     */
/* control, positions it (a plain X/Y/W/H write - see InitPosition) and   */
/* wires it the way its own kind implies - Button reaches target's        */
/* Activate (ConnectToActivate); a display kind (LED/TextOut/VUMeter/     */
/* Label) reflects target's property/port (Connect(target, prop,          */
/* control, "In")); anything else (an input kind: Checkbox/Textbox/       */
/* Slider/Knob) edits it (ConnectToProperty(control, "Value", target,     */
/* prop)). No container groups these controls - each is independently     */
/* placed and wired, discoverable by their shared Connect() to target,    */
/* the same as anything else in the tree. Returns target.                 */
NodeObj BuildSettingsView(NodeObj target, ControlSpec *specs, int count);

/* Small per-connection scalar state, keyed by a Conn id (see tcp.c's   */
/* multi-connection support) - a table node holds one long-typed prop   */
/* per connection, named by its decimal id. Used by anything sitting     */
/* between multiple simultaneous TCP peers and otherwise-shared app      */
/* state that still has to stay separate per peer (Router's sniffed      */
/* HTTP-vs-WebSocket mode, WebSocket's handshake-done flag). Conn ids    */
/* are handed out once per accepted connection and never reused, so a    */
/* closed connection's entry is simply left in the table rather than     */
/* removed - the same pragmatic non-cleanup every alias table and flow-  */
/* recording node in this codebase already accepts.                      */
long GetConnState(NodeObj table, long connId);
void SetConnState(NodeObj table, long connId, long value);

/* Retired - SetPropInt/SetPropStr/SetPropLong (node.c) fan out to a      */
/* property's subscribers unconditionally now, on every write, with no   */
/* opt-in step: a property is watchable simply by existing, exactly like */
/* a port already was. Kept as a no-op only so existing call sites across */
/* the object tree keep compiling; stripping the calls out is cleanup,   */
/* not a fix, and can happen opportunistically.                          */
void
WatchableProp(NodeObj instance, char * propname);

/* Position (and containment - see Container below) is not core knowledge -*/
/* RegisterClass/RegisterInstance stay exactly as agnostic about X/Y/W/H as */
/* they are about Filename or State. These are ordinary opt-in helpers any  */
/* class that wants its instances placeable calls from its own              */
/* ClassStart/InstanceStart, exactly the way it already publishes its own   */
/* properties. Moving something is then just set-property on X/Y - the      */
/* identical command, and the identical subscribe/property-changed fan-out  */
/* (unconditional, see node.c), that already syncs Count or Filename        */
/* across every connected window. Nothing new to build.                     */
/*                                                                          */
/* Also carries Container (which View's own alias this instance lives      */
/* inside, "" for the top-level canvas) and Deletable ("0" refuses           */
/* Bridge_Delete) - the same reasoning: an instance's membership in a View   */
/* is exactly as ordinary a property as where it sits, not a Slot/           */
/* membership structure, and BuildPalette (this file) is what actually       */
/* uses both to make the Palette "just a view" with no special handling      */
/* beyond two property values.                                              */
void PublishPosition(NodeObj class);
void InitPosition(NodeObj instance);


/* A flow is the recorded sequence of composition calls that built it -   */
/* Create/Set/Connect/Activate instructions, not a dump of live instance  */
/* state (pointers, ports) that InstanceStart re-establishes every time   */
/* an object loads anyway. Build one with NewFlow(), then use the         */
/* Flow* wrappers in place of the plain calls above: each one performs    */
/* the live effect exactly like its plain counterpart and also appends   */
/* the instruction that reproduces it. NodeToText/TextToNode save and     */
/* load the resulting script; RunFlow replays it into a container.        */

NodeObj NewFlow(char * name);

NodeObj FlowCreateObject(NodeObj flow, NodeObj container, char * classname);
void    FlowSetProp     (NodeObj flow, NodeObj instance, char * prop, char * value);
int     FlowConnect     (NodeObj flow, NodeObj fromInst, char * fromPort, NodeObj toInst, char * toPort);
int     FlowActivateInstance(NodeObj flow, NodeObj instance);

/* replay a recorded (or loaded) flow script into a container */
NodeObj RunFlow(NodeObj container, NodeObj flow);

/* save/load a flow script as text */
int     SaveFlow(NodeObj flow, char * filename);
NodeObj LoadFlow(NodeObj container, char * filename);

/* needs real loaded classes - run after InstallObjects(), not from PerformTesting() */
void    FlowTest(NodeObj container);
void    InterfaceTest(void);
void    SkinTest(void);

/* pure mechanism, no registered classes needed - fine to run from PerformTesting() */
void    PropertyWatchTest(void);


/* Call backs from dynamically loaded objects to register and unregister    */
/* themselves. The node passed in is conventionally built with Name,        */
/* Company, UUID, ClassStart/ClassEnd/ClassMsg (function pointers, as long   */
/* properties), and State - every _init() already does this. Two more       */
/* properties, both optional (missing/empty means "none"):                  */
/*   Version      - a plain string, "1.0" for everything right now; nothing  */
/*                  reads or compares it yet, it exists so a library says    */
/*                  what it is without a separate registry.                  */
/*   Dependencies - a comma-separated list of other libraries' Name that     */
/*                  must have their own ClassStart already run first -       */
/*                  loadClasses (object.c) topologically sorts on this       */
/*                  before calling ClassStart on anything, so a class that   */
/*                  needs another one already registered (subclassing, a     */
/*                  runtime lookup by name) can just declare it instead of    */
/*                  hoping scan order happens to cooperate.                  */
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
    PROP_SLIDER,
    PROP_VUMETER,
    PROP_TEXTOUT,
    PROP_KNOB,
    PROP_LABEL,
    PROP_NULL,
    PROP_MENU
} PropertyType;

/* Published interface: what a palette (or anything else outside the     */
/* object) needs to know about a class without creating an instance -    */
/* its properties and ports, direction, widget, and default value. Each  */
/* class declares this itself in ClassStart, right after RegisterClass.  */
/* direction is "data" for a plain property, "in"/"out" for a port.      */
NodeObj PublishProp(NodeObj class, char * name, char * direction, int widget, char * defaultValue);

/* the published interface for a registered class, or NULL if it hasn't  */
/* published one - walk it with GetChild/GetNextSibling                  */
NodeObj GetClassInterface(NodeObj class);

/* per-class layout (position, label, look) for each published property, */
/* one Layout entry per Property in the class's Interface. GetClassSkin  */
/* returns whatever was loaded, or generates and attaches a default from */
/* the interface the first time it's asked. Save/Load round-trip it as   */
/* text through a file, the same pattern as SaveFlow/LoadFlow.           */
NodeObj GenerateSkin(NodeObj class);
NodeObj GetClassSkin (NodeObj class);
int     SaveSkin(NodeObj class, char * filename);
NodeObj LoadSkin(NodeObj class, char * filename);

#endif