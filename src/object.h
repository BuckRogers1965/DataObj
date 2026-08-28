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

/* Create an object instance of a named class IN A CONTAINER. The
 * container is required: everything is created somewhere, and a missing
 * location is an error (logged, and the instance comes back unplaced and
 * therefore unaddressable). Only a root has no location - see CreateRoot.
 *
 * `name` is what it will be called, and it is kept: a caller that knows the
 * name must not have it changed underneath. NULL/empty means the caller
 * genuinely has none - a palette drop - and only then is one minted.
 */
NodeObj
CreateObject(NodeObj container, char * classname, char * name);

/* The same creation, deliberately UNNAMED: a private handle, placed like
 * anything else but reachable only through the pointer its owner keeps.
 * The socket inside a port widget and the language host inside a ScriptBox
 * are these. Nothing addresses one, so nothing lists, saves or wires it by
 * path - and that is stated here rather than achieved by not calling
 * RegisterPath, which is how it used to be told apart from an oversight:
 * it wasn't.
 */
NodeObj
CreatePrivate(NodeObj container, char * classname, char * name);

/* A root IS A VIEW - an ordinary one. The only difference is that it has
 * no container, because it is the top; that is why it is made here rather
 * than with CreateObject, which requires a location. As many as you like
 * (eventually one per login); each anchors its own namespace, and a
 * Bridge is handed the root of the session it serves.
 */
NodeObj CreateRoot(char * name);

/* the registry root - RegObjList -> libraries -> classes - walk every  */
/* class (a palette) with GetChild/GetNextSibling at both levels        */
NodeObj GetRegObjList(void);

/* WALK THE REGISTRY WITH THESE, not with a nested loop of your own.

     for (cls  = FirstClass();    cls;  cls  = NextClass(cls))
     for (inst = FirstInstance(); inst; inst = NextInstance(inst))

   Spelling out RegObjList -> library -> class -> instance at a call site
   copies the registry's shape into that file, and every copy has to be
   found and edited the day the shape changes. These four know the shape;
   nobody else needs to. Early exit as usual - the cursor resumes from any
   node, because a node knows where it sits.

   ClassOfInstance/LibraryOfClass give back the level above when a walk
   needs it, so nothing has to carry it in a local. */
NodeObj FirstClass(void);
NodeObj NextClass(NodeObj class);
NodeObj FirstInstance(void);
NodeObj NextInstance(NodeObj inst);
NodeObj ClassOfInstance(NodeObj inst);

/* WHAT IS IN THIS CONTAINER, without walking every instance in the
   session. The list is maintained where naming happens - RegisterPath
   adds, UnregisterPath removes - so it is an index of the Container
   property rather than a second truth about it.

     for (e = FirstMember(view); e; e = GetNextSibling(e))
         inst = MemberInstance(e);                                     */
NodeObj FirstMember(NodeObj container);
NodeObj MemberInstance(NodeObj entry);

/* A handler that returned rtrn_dropped said "not mine". This offers the
   message up the class chain - instance -> its class -> its parent -> ...
   -> Object, which handles it or drops it for real. A class opts in with a
   ClassMsg on its class node; one without is transparent.

   Walked live on every call, never cached, so a class spliced in between
   an existing class and its parent takes effect on the next message. */
int PuntToClass(NodeObj instance, MsgId message, NodeObj data);
NodeObj LibraryOfClass(NodeObj class);

/* The palette, the topbar chrome, and what it takes to be placed at all
   (a name, a place, a size) live in control.object - the palette exists
   only to show controls. Include control.h to reach them. */

/* remove an instance for good - UnRegisterInstance plus DelNode. Callers */
/* that also track the instance by alias (Bridge) must drop their own      */
/* reference too; this only unwinds the registry/tree side.                */
void DeleteInstance(NodeObj instance);

/* Connect two properties between two object instances */
/* the sink's "to" port subscribes to the source's "from" port. Works    */
/* against ANY property name on any instance: a port with a compiled     */
/* OnMsg handler gets its handler called, a plain property receives the  */
/* universal default delivery (store what arrived - DeliverToSubscriber, */
/* node.c). Either way the subscription names the real sink and its      */
/* port, so the live graph IS the connection list.                       */
int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to);

/* Addressing (roadmap Phase 1.5): the engine owns path -> instance, one */
/* trie-backed index every translator resolves against. RegisterPath     */
/* names, UnregisterPath un-names (a real delete), ResolvePath finds in  */
/* O(path length). PathOfInstance derives the reverse from Name +        */
/* Container and verifies by resolving back - returns 0 for anything     */
/* unnamed or inconsistent (treat as "has no path").                     */
void    RegisterPath(char * path, NodeObj inst);
void    UnregisterPath(char * path);
NodeObj ResolvePath(char * path);
int     PathOfInstance(NodeObj inst, char * out, int outlen);

/* THE SAME LOOKUP, ASKED AS AN ASSERTION. "Not found" is the wanted answer
   for a mint or an exists-check and a fault for anything that was holding
   the thing or was handed the name by a user - one NULL, two facts, so the
   call site says which by which one it calls. These report at ERROR against
   the CALLER's file and line, then return what the plain form returns.
   Rule of thumb: walking FirstMember means every one of them was named on
   purpose, so use the Require form; walking FirstInstance reaches private
   handles that are unnamed deliberately, so use the plain one.            */
NodeObj RequirePathAt(char * path, char * file, int line);
int     RequirePathOfAt(NodeObj inst, char * out, int outlen, char * file, int line);
#define RequirePath(path)               RequirePathAt((path), __FILE__, __LINE__)
#define RequirePathOf(inst, out, len)   RequirePathOfAt((inst), (out), (len), __FILE__, __LINE__)

/* allocation accounting: message envelopes currently queued between     */
/* SndMsg and DispatchMsg - reads 0 at rest; a climb means messages are   */
/* being lost undelivered. See NodeCount (node.h).                         */
long
EnvelopeCount(void);

/* the inverse: remove exactly the one wire Connect() would have made    */
/* between these four names (aliases resolve the same way). Returns 1    */
/* if a wire was removed, 0 if none matched.                             */
int
Disconnect(NodeObj fromNode, char * from, NodeObj toNode, char * to);

/* Send a message out a named port of an instance. */
/* The message is routed to every subscriber of that port. */
/* Returns the number of subscribers it was delivered to.  */
int
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data);

/* the same send, given the resolved property node rather than a name -  */
/* node.c's property fan-out queues through this.                        */
int
SndMsgNode(NodeObj instance, NodeObj outPort, MsgId message, NodeObj data);

/* Deliver straight to one named port's own handler, bypassing whatever  */
/* Subscriber list is attached to it - for something that decides at     */
/* delivery time which single target gets a message (Router), rather    */
/* than a fixed Connect()'d wire. Returns 1 if delivered, 0 if the       */
/* target has no such port or no handler on it.                         */
int
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
DeliverMsg(NodeObj target, char * port, MsgId message, NodeObj data);

/* Call the Activate function an instance registered on itself */
int
ActivateInstance(NodeObj instance);

/* Export/import/load a view: serializer.object owns the format, both
   directions - include serializer.h to reach ExportView / ImportView /
   LoadViewAsync. The core neither reads nor writes it. */

/* THE rule for a name the server mints: the base with any trailing _N
   stripped, then the lowest free _k in the container. Shared by import and
   by clone, which is why it stays here - naming is addressing. */
void MintFreshName(char *base, char *containerPath, char *out, int outlen);

/* Write a named property on target the way something OUTSIDE the object */
/* has to: if the name resolves to a port (an OnMsg handler is present,  */
/* e.g. Enable), deliver a message so the port's own handler actually    */
/* runs, exactly like genuine Connect()'d traffic would - a bare         */
/* SetPropStr would only overwrite the port's raw text and change        */
/* nothing the object is actually gated on. Otherwise it's a plain data  */
/* property and a direct SetPropStr is correct.                          */
void
SetOrDeliverProp(NodeObj target, char * propname, char * value);

/* Placement is part of birth (and of a move): Container, X, Y in one    */
/* call, so every verb that puts a thing somewhere - create, clone, move, */
/* from any translator (Bridge, Script, ...) - places it the same way.   */
/* NULL container means the top-level canvas; NULL/empty x or y means    */
/* "leave it where it is".                                                 */
void
PlaceInstance(NodeObj inst, char * container, char * x, char * y);

/* The one-verb move: refuse a containment cycle (a view can never enter  */
/* itself or a descendant - instPath is the mover's session path,          */
/* container the destination's; paths are the containment chain, so the    */
/* rule is a prefix test, see ContainmentCycle), then PlaceInstance.        */
/* Returns 1 moved, 0 refused. Renaming/eventing stay the translator's     */
/* business, same split as CloneInstance.                                     */
int
MoveInstance(NodeObj inst, char * instPath, char * container, char * x, char * y);
int
ContainmentCycle(char * instPath, char * container);

/* Resolve (instance, propname) through any alias link chain to the pair */
/* that actually owns the property - *instp is rewritten to the owning   */
/* instance when the name is a link. Plain properties behave exactly     */
/* like GetPropNode. Every port-resolution choke point uses this, which  */
/* is the entire alias mechanism.                                         */
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
NodeObj ResolvePort(NodeObj * instp, char * name);

/* Expose targetInst's property on owner as a link - value, subscribers, */
/* and wiring all stay on the original. Chains collapse to the final     */
/* original at creation. Returns 0 if targetInst has no such property.   */
/* The ...As form names the local slot: an Alias keeps its link in its    */
/* own "Value" slot so its own Name/Container/X/Y stay its own.           */
int LinkPropertyAs(NodeObj owner, char * slot, NodeObj targetInst, char * propname);

/* Make a doorway onto one property of another instance: a control whose   */
/* own "Value" slot links to it, carrying Target/TargetProp/Widget so a    */
/* client can see what it stands for. There is no alias class - CreateAlias */
/* makes the control that says it Renders that kind of property, the way    */
/* CloneInstance needs no clone class. AliasProperty applies the link to an */
/* instance that already exists; CreateAlias makes one first.               */
int     AliasProperty(NodeObj aliasInst, NodeObj targetInst, char * propname);
NodeObj CreateAlias(NodeObj container, NodeObj targetInst, char * propname);

/* Does this instance stand for somebody else's property - is it an alias. */
/* Asks the thing (is its Value a link) rather than what class it is, which */
/* is the only question with an answer now that any control can be one.    */
/* targetInst/targetProp are filled when given; either may be NULL.        */
int IsAlias(NodeObj inst, NodeObj * targetInst, char ** targetProp);

/* which loaded class says it renders this kind of property (Renders, on   */
/* the class node). The registry walk FindClass does, asking what instead   */
/* of who - a control declares its own type, nothing is inferred.           */
NodeObj FindClassRendering(int widget);
int LinkProperty(NodeObj owner, NodeObj targetInst, char * propname);

/* Clone the wires INSIDE a group being copied: for each of srcInst's    */
/* outgoing subscriptions whose sink is also being cloned (map holds     */
/* sink -> sink's clone, keyed by pointer - see GetConnState), wire      */
/* cloneInst's matching port to that sink's CLONE. This is what keeps a  */
/* copied group wired to itself instead of arriving dead - the exact     */
/* rule alias members already follow when a view is deep-cloned.         */
/* Subscriptions leaving the group are deliberately NOT copied: from     */
/* here a client's own subscribe tap looks identical to a wire, and      */
/* copying one would make the clone's traffic report under the           */
/* original's name.                                                       */
void CloneConnections(NodeObj srcInst, NodeObj cloneInst, NodeObj map);

/* Clone a THING into containerPath - ANY instance, in the engine,        */
/* identical whoever asked (a script, or the html through the bridge).     */
/* There is NO separate call for a view: you clone the thing. If it holds  */
/* members (a view, or a view nested in a view) those are copied too -     */
/* re-homed, alias members re-pointed at the clones, wires re-made between  */
/* them - recursing to any depth; a leaf control just copies its node.     */
/* The ENGINE names the clone (unique in its container). Every instance    */
/* made is recorded in `map` (src -> clone) for the caller to walk out.    */
/* Returns the top clone.                                                  */
NodeObj CloneInstance(NodeObj source, char * containerPath, NodeObj map);

/* Retired: ConnectToProperty/ConnectToActivate and their adapter nodes  */
/* are gone - plain Connect() reaches any property (universal default    */
/* delivery) and any instance's Activate port (ActivateOnMsg, stamped by */
/* RegisterInstance), so there is no second or third way to wire.        */

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
/* membership structure, and BuildPalette (control.object) is what actually  */
/* uses both to make the Palette "just a view" with no special handling      */
/* beyond two property values.                                              */
/* PublishPosition/InitPosition are in control.object too (control.h). */


/* A flow is the recorded sequence of composition calls that built it -   */
/* Create/Set/Connect/Activate instructions, not a dump of live instance  */
/* state (pointers, ports) that InstanceStart re-establishes every time   */
/* flow scripts live in flow.object now - include flow.h to reach them */

/* The tests that used to live here are in the test harness now
   (src/unit_test.c): the library ships mechanism, the harness ships the
   measurements. */


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

/* the deferred second phase of InstallObjects (see main.c): after every  */
/* .object is scanned and loaded, start each library's classes in         */
/* dependency order; UnloadClasses is the symmetric teardown              */


/* --- class dependencies and versions -------------------------------------- */
/* A module declares, in its _init(), every class it actually uses, naming the
   FILE that provides it and the CLASS itself: the file is what the loader can
   act on before anything has started, the class is what gets verified once
   that file is up. Version is per class, major/minor separate (a version is a
   tuple - "1.10" sorts below "1.9" as a string, and converts to 1.1 as a
   REAL). Compatible = major equal, minor at least what was asked.
   The core's own classes come from CORE_LIBRARY_FILE, so a widget declares
   AddDependency(lib, CORE_LIBRARY_FILE, "Widget", "1", "0").               */
#define CORE_LIBRARY_FILE "libframework.so"
#define CORECLASS_MAJOR   "1"
#define CORECLASS_MINOR   "0"

void AddDependency(NodeObj library, char *file, char *classname,
				   char *major, char *minor);
void ClearDependencies(NodeObj library);   /* a module's _fini() frees its own */
void SetClassVersion(NodeObj class, char *major, char *minor);
void SetClassParent(NodeObj class, char *parentname);
int  ClassVersionOk(NodeObj class, char *wantMajor, char *wantMinor);

/* find a registered class by name: RegObjList -> libraries -> classes. The
   lookup widget.h's wrappers use to reach the Widget class's entry points. */
NodeObj FindClass(char * classname);

/* Object - the one class the core provides, registered before any module is
   scanned so every module's dependency on it resolves. Control and Widget are
   loadable classes of their own (control.object, widget.object). */
void RegisterCoreClasses(void);
void
loadClasses(void);
void
UnloadClasses(void);


/* The main funtion must sent a property node of it's main to accept the register list */
void
ObjSetRegObjList(NodeObj node);

/* The main function must send in its scheduler task list */
/* so that loaded objects can schedule their own tasks     */
void
ObjSetTaskList(void * list);

void *
ObjGetTaskList(void);


/* PropertyType - which control a published property presents as - is in
   control.h. The core stores that number and never interprets it; what
   the choices ARE is a question about controls. */

/* Published interface: what a palette (or anything else outside the     */
/* object) needs to know about a class without creating an instance -    */
/* its properties and ports, direction, widget, and default value. Each  */
/* class declares this itself in ClassStart, right after RegisterClass.  */
/* There is no direction and no port. Every published property is the    */
/* same kind of thing: a node. If it changes and something subscribed to */
/* it, that change is sent out - the same rule whether the property is   */
/* named In, Out, Enable or Value. Those are names, not kinds.           */
NodeObj PublishProp(NodeObj class, char * name, int widget, char * defaultValue);

/* is this property portable DATA, as opposed to a runtime pointer or a stale
   shadow? The single rule a clone and an export must agree on - both call
   this, so they cannot drift. */
int IsPortableProp(NodeObj inst, NodeObj prop);

/* WHAT THIS CLASS OFFERS A PERSON, beyond the session's own modes: a comma
   separated list of gesture names, published on the class node the same way
   its Interface and its Show are. Picking one sends the instance a
   msg_gesture naming it; the class answers or declines like any message.
   A name ending in "..." wants something typed first. */
void PublishGestures(NodeObj class, char *names);
char *ClassGestures(NodeObj inst);
int PuntGesture(NodeObj inst, NodeObj bag);
NodeObj ClassOfferingGesture(NodeObj inst, char *name);

/* One instance property's published metadata - the Interface "Property" */
/* entry (Name/Widget/Default) on the instance's class, NULL             */
/* if unpublished. What a translator stamping presentation defaults onto */
/* an alias (create-alias, internals) reads: the engine decides what a   */
/* property's control looks like, clients render it, never deduce it.    */
NodeObj InterfacePropForInstance(NodeObj inst, char * propname);

/* the published interface for a registered class, or NULL if it hasn't  */
/* published one - walk it with GetChild/GetNextSibling                  */
NodeObj GetClassInterface(NodeObj class);

/* per-class layout for each published property: skins live in skin.object
   now - include skin.h to reach them */


#endif
