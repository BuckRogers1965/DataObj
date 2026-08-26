#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "widget.h"
#include "DebugPrint.h"
#include "callback.h"
#include "sched.h"
#include "namespace.h"

/*
 * Addressing (roadmap Phase 1.5): the ENGINE owns path -> instance.
 * Identity was always a path in this system (Container is a path string,
 * "there is no creation path, only a current path") - this is the one
 * resolver over that fact, a character trie (namespace.c, written for
 * exactly this) resolving in O(path length) regardless of session size.
 * Translators (the JSON bridge, script language hosts, the future MCP
 * server) all resolve the same names against the same index instead of
 * each keeping a private alias table; naming something is RegisterPath,
 * un-naming is UnregisterPath (a real delete - retired names reclaim
 * their keys), and the reverse direction needs no table at all:
 * PathOfInstance derives from the Name and Container properties the
 * instance already carries, verified by resolving back.
 */
static NSObj * PathIndex = NULL;

static NSObj * GetPathIndex(void)
{
	if (!PathIndex)
		PathIndex = NSCreate();
	return PathIndex;
}

/* CLASS NAME -> CLASS NODE, for the same reason the path index exists: a
   name lookup should cost the length of the name, not a walk over every
   class of every library. FindClass was that walk, and bring-up rebuilt a
   throwaway copy of this index on every sweep.

   It is maintained by RegisterClass/UnRegisterClass, so it is correct
   whatever shape the registry tree has - which is what lets a class move
   out of its library's child list without any lookup noticing. */
static NSObj * ClassIndex = NULL;

static NSObj * GetClassIndex(void)
{
	if (!ClassIndex)
		ClassIndex = NSCreate();
	return ClassIndex;
}

/* THE CONTAINER'S OWN LIST OF WHAT IS IN IT.

   "What is in this container" was answered by walking every instance in
   the session and string-comparing its Container - five copies of that
   scan, for the question everything asks most. The answer is known at the
   moment it becomes true: RegisterPath already resolves the container in
   order to tell it about the arrival. Keeping what it is told turns a walk
   of the whole session into a walk of a short list.

   POINTERS, NOT CHILDREN. An instance is already a child of its class and
   a sibling of that class's other instances - one parent, one nextSib,
   both spent on the type tree. So the list is entries under a Members
   property, each standing for one instance. Containment remains the
   Container property; this is an index of it, held where the announcement
   already arrives.

   The list node is LONG-typed so IsPortableProp refuses it: an index is
   not data, and neither a clone nor a flow file should carry one. */
static NodeObj MembersOf(NodeObj container, int create)
{
	NodeObj list;

	if (!container)
		return NULL;

	list = GetPropNode(container, "Members");
	if (!list && create)
	{
		list = NewNode(LONG);
		SetName(list, "Members");
		AddProp(container, list);
	}

	return list;
}

static void AddMember(NodeObj container, NodeObj inst)
{
	NodeObj list, entry;

	if (!container || !inst)
		return;

	list = MembersOf(container, 1);
	if (!list)
		return;

	/* named twice is not two members - RegisterPath is called again on
	   every rename, and the instance is already in here */
	for (entry = GetChild(list); entry; entry = GetNextSibling(entry))
		if ((NodeObj) GetPropLong(entry, "Instance") == inst)
			return;

	entry = NewNode(INTEGER);
	SetName(entry, "Member");
	SetPropLong(entry, "Instance", (long) inst);
	AppendChild(list, entry);
}

static void RemoveMember(NodeObj container, NodeObj inst)
{
	NodeObj list = MembersOf(container, 0);
	NodeObj entry;

	for (entry = list ? GetChild(list) : NULL; entry; entry = GetNextSibling(entry))
		if ((NodeObj) GetPropLong(entry, "Instance") == inst)
		{
			DelSibling(entry);
			DelNode(entry);
			return;
		}
}

/* walk it as:  for (e = FirstMember(c); e; e = GetNextSibling(e))
                    inst = MemberInstance(e);                        */
NodeObj FirstMember(NodeObj container)
{
	NodeObj list = MembersOf(container, 0);

	return list ? GetChild(list) : NULL;
}

NodeObj MemberInstance(NodeObj entry)
{
	return entry ? (NodeObj) GetPropLong(entry, "Instance") : NULL;
}

/* the container of a path, or NULL for a root - the same split
   RegisterPath and UnregisterPath both need */
static NodeObj ContainerOfPath(char *path)
{
	char cpath[300], *slash;

	if (!path || !path[0])
		return NULL;

	snprintf(cpath, sizeof(cpath), "%s", path);
	slash = strrchr(cpath, '/');
	if (!slash || slash == cpath)
		return NULL;
	*slash = '\0';

	return ResolvePath(cpath);
}

void RegisterPath(char * path, NodeObj inst)
{
	NodeObj container;
	char    cpath[300], *slash;

	if (!path || !path[0] || !inst)
		return;

	/* A SECOND NAME IS A MOVE, NOT AN EXTRA ONE. Naming something that is
	   already named has to retire the old name, or the index keeps a key
	   nobody will ever remove: the thing answers to two paths, only one of
	   which its own Name agrees with, and the other sits there forever
	   holding a name no mint will hand out again. The two rename paths in
	   the bridge do this by hand, in the right order, because they knew;
	   doing it here is what lets anything else register a name over an
	   older one and be right. Their own UnregisterPath still runs first,
	   which leaves nothing here to find - a no-op, not a double.
	   Chrome's short well-known keys are deliberately a SECOND way to
	   reach one instance, so they are not this: they name no location,
	   and the leading slash is the difference. */
	if (path[0] == '/')
	{
		char was[300];

		if (PathOfInstance(inst, was, sizeof(was)) && strcmp(was, path) != 0)
		{
			char dbg[400];

			snprintf(dbg, sizeof(dbg), "RENAMED '%s' -> '%s'", was, path);
			DebugPrint(dbg, __FILE__, __LINE__, REGISTER);
			UnregisterPath(was);
		}
	}

	NSInsert(GetPathIndex(), path, (long) inst);

	/* THE LAST SEGMENT IS THE NAME. It was already in the caller's hand and
	   thrown away here, so every caller set it again on the next line -
	   RegisterPath then Bridge_SetNameFromAlias, twice in the bridge's
	   creates, twice more in import. Worse than duplication: the two could
	   disagree, and a Name that disagrees with the path it was registered
	   under is exactly the failure PathOfInstance reports as "calls itself
	   X but that path is not in the index" - it derives from the Name, so
	   the thing goes unaddressable while sitting in the index the whole
	   time. Setting it HERE is what makes the two unable to differ.
	   Set before the container is told, so whoever reacts to the arrival
	   reads the name it arrived under rather than the one it had a moment
	   ago. Only for a real path: the chrome's short well-known aliases
	   (bridge.c) are lookup keys, not locations, and name nothing. */
	if (path[0] == '/')
	{
		char *base = strrchr(path, '/') + 1;
		char *had  = GetPropStr(inst, "Name");
		char  prev[160];

		/* COPIED, not held: `had` points into the Name property's own
		   DataObj, and the write below replaces it - reading it afterwards
		   to say what the name used to be is reading freed memory. */
		snprintf(prev, sizeof(prev), "%s", (had && had[0]) ? had : "");

		if (base[0] && strcmp(prev, base) != 0)
		{
			char dbg[400];

			SetPropStr(inst, "Name", base);
			snprintf(dbg, sizeof(dbg), "NAMED '%s' from its path '%s'%s%s",
					 base, path,
					 prev[0] ? ", was " : " (had no name)",
					 prev[0] ? prev : "");
			DebugPrint(dbg, __FILE__, __LINE__, REGISTER);
		}
	}

	/* the container just gained a member, and that is otherwise invisible:
	   containment is a property on the CHILD, so its fan-out reaches
	   whoever was already watching that child - nobody, for something that
	   did not exist a moment ago. Recording the new path ON THE CONTAINER
	   makes it an ordinary property write, which fans out to whoever is
	   watching the container. This is the one place every creator meets:
	   import, clone, a bridge create, an object building its own panel.
	   A root has no container to tell. */
	snprintf(cpath, sizeof(cpath), "%s", path);
	slash = strrchr(cpath, '/');
	if (!slash || slash == cpath)
		return;
	*slash = '\0';
	container = ResolvePath(cpath);
	if (container)
	{
		/* WHERE IT WAS BUILT IS WHAT IT IS. The path already says the
		   location - this function splits it off to find the container -
		   so writing it back onto the instance is the same fact, not a
		   second one a caller has to remember to keep in step. */
		SetPropStr(inst, "Container", cpath);

		/* the event, for whoever is watching */
		SetPropStr(container, "LastMember", path);
		/* and the record, for whoever asks later */
		AddMember(container, inst);
	}
}

/* Arrival was announced and departure was silent, which is why nothing
   could keep a list: it would go stale on the first delete. This is the
   other edge - resolve first, un-name, then take it out of its
   container's list. */
void UnregisterPath(char * path)
{
	NodeObj inst, container;

	if (!path || !path[0])
		return;

	inst = ResolvePath(path);
	container = ContainerOfPath(path);

	NSDelete(GetPathIndex(), path);

	if (inst && container)
		RemoveMember(container, inst);
}

NodeObj ResolvePath(char * path)
{
	if (!path || !path[0])
		return NULL;
	return (NodeObj) NSSearch(GetPathIndex(), path);
}

/* the derived reverse lookup: an instance's path is its Container plus  */
/* its Name (empty Container means the top-level canvas, /Root). Only a  */
/* path that resolves back to the same instance is returned - anything   */
/* unnamed, engine-internal, or mid-rename simply has no path, the same  */
/* answer a missing alias-table entry used to give.                       */
/* A ROOT IS A VIEW. Nothing about it is special: it is an ordinary View
   instance, made the ordinary way, and everything a session shows lives
   in it exactly as anything lives in any other view. The one and only
   difference is that it has no container - it is the top, so there is
   nothing above it to be in. That is why it is made here rather than
   with CreateObject, which requires a location.

   There can be as many as you like - eventually one per login. Each
   anchors its own namespace, and a Bridge is handed the root of the
   session it serves and walks it like any other view.  */
NodeObj FindClass(char * classname);	/* defined below */
static int NameTakenIn(char *name, char *containerPath);	/* defined below */

NodeObj CreateRoot(char * name)
{
	NodeObj class, root;
	msgobj InstanceStart;
	char path[160];

	if (!name || !name[0])
		return NULL;

	class = FindClass("View");
	if (!class) {
		DebugPrint("CreateRoot needs the View class", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	if (!InstanceStart)
		return NULL;

	InstanceStart(class, msg_initialize, NULL);
	root = (NodeObj)GetPropLong(class, "LastInstance");
	if (!root)
		return NULL;

	SetPropStr(root, "Name", name);
	SetPropStr(root, "Container", "");	/* the top: nothing above it */
	SetPropStr(root, "ReservedViewOpen", "1");

	snprintf(path, sizeof(path), "/%s", name);
	RegisterPath(path, root);

	return root;
}

int PathOfInstance(NodeObj inst, char * out, int outlen)
{
	char * name, * cont;

	if (!inst || !out || outlen < 2)
		return 0;

	name = GetPropStr(inst, "Name");
	if (!name || !name[0]){
		/* No name means no path - and that is a legitimate answer now, not a
		   fault. A PRIVATE HANDLE is deliberately unnamed so that nothing can
		   address it: a language host inside a ScriptBox, a socket inside a
		   port widget. This was an ERROR back when everything was supposed to
		   be addressable, and it fired - with a full node dump - on every
		   registry-wide walk, which is most of them.
		   Reported as what it is - a placement trace - and NOTHING MORE. It used
		   to dump the whole node at -v 3, which was wrong twice over: every
		   registry-wide walk calls this on every instance, so one unnamed
		   handle meant dumping its entire subtree on every walk (a firehose,
		   not a diagnostic); and some of those walks run inside
		   DeleteInstance, where the scrub is freeing the very nodes the dump
		   would be reading. That crashed the -O0 builds outright and got past
		   release and asan on luck. An expected condition does not get a
		   node dump. */
		DebugPrint ( "instance has no name, so it has no path", __FILE__, __LINE__, PLACE);
		return 0;
	}

	/* Container + name, and nothing invented. Only a ROOT has no
	   container (CreateRoot), and its path is just its own name - every
	   other instance was created somewhere and says so. */
	cont = GetPropStr(inst, "Container");
	if (cont && cont[0])
		snprintf(out, outlen, "%s/%s", cont, name);
	else
		snprintf(out, outlen, "/%s", name);

	return ResolvePath(out) == inst;
}

/* THE SAME LOOKUP, ASKED AS A DIFFERENT QUESTION.

   ResolvePath and PathOfInstance both answer "not found" with NULL/0, and
   two completely different facts arrive that way:

     - the name is free, or this handle was never meant to be addressable.
       That is the answer the caller WANTED. Minting asks it, an adopt check
       asks it, a script's `exists` asks it. Saying anything at all about it
       would be noise on the success path.

     - the name was supposed to be there. A script writing a property by
       path, a translator moving something it is already holding, a save
       walking a container's own members. Absent here is a fault, and every
       one of those sites swallowed it - `if (!PathOfInstance(...)) continue;`
       - so a save quietly omitted a member and a mistyped path in a script
       did nothing whatever, without a word.

   Same trie, same lookup, same answer. What the call site knows and the
   function cannot is which of the two it is asking, so it says so by which
   one it calls. The Require forms are the second question: they report it,
   naming the CALLER's file and line rather than this one, and then return
   exactly what the plain form returns. They change nothing about what
   happens - only about what is known when it does. */
NodeObj RequirePathAt(char * path, char * file, int line)
{
	NodeObj inst = ResolvePath(path);
	char dbg[400];

	if (inst)
		return inst;

	snprintf(dbg, sizeof(dbg), "nothing is named '%s'",
			 (path && path[0]) ? path : "");
	DebugPrint(dbg, file, line, ERROR);

	return NULL;
}

/* The reverse, for a caller that is HOLDING the instance. It is alive, so
   a missing path is a fault in the naming rather than in the question.
   Which fault matters, because the two have different causes and looked
   identical for as long as both were 0: no Name at all (never named, or a
   private handle somewhere that expects an addressable one), against a
   Name whose path is not in the index (a sibling already holds it, or a
   re-key was skipped and the thing is buried under its old name). */
int RequirePathOfAt(NodeObj inst, char * out, int outlen, char * file, int line)
{
	char * name, * cont;
	char dbg[500];

	if (PathOfInstance(inst, out, outlen))
		return 1;

	if (!inst)
	{
		DebugPrint("asked where nothing lives", file, line, ERROR);
		return 0;
	}

	name = GetPropStr(inst, "Name");
	cont = GetPropStr(inst, "Container");

	if (!name || !name[0])
		snprintf(dbg, sizeof(dbg),
				 "a live %s in '%s' has no Name, so nothing can reach it",
				 GetNameStr(inst) ? GetNameStr(inst) : "instance",
				 (cont && cont[0]) ? cont : "/Root");
	else
		snprintf(dbg, sizeof(dbg),
				 "'%s' calls itself '%s' but that path is not in the index - "
				 "a sibling holds the name, or a rename never re-keyed",
				 (cont && cont[0]) ? cont : "/Root", name);

	DebugPrint(dbg, file, line, ERROR);

	return 0;
}

NodeObj RegObjList;

void
ObjSetRegObjList(NodeObj node){
	RegObjList = node;

	/* the core's own classes, the moment there is somewhere to put them.
	   NOT the host's job: every host has its own copy of InstallObjects, so a
	   host that forgot this call got a registry where nothing could start -
	   every module declares Object, and an unmet parent means no class at all.
	   Registering here means any embedder gets it by handing over a registry. */
	RegisterCoreClasses();
}

/* The scheduler task list lives in main, but loaded objects need to */
/* schedule work.  This lives here in the shared library so main and */
/* every loaded object see the same list, just like RegObjList.      */

void * ObjTaskList;

void
ObjSetTaskList(void * list){
	ObjTaskList = list;
}

void *
ObjGetTaskList(void){
	return ObjTaskList;
}


/* the file a library came from - libload.c stamps it after dlopen, because  */
/* the library node is built inside the module's own _init(), which never    */
/* sees the path. Basename only: a dependency names the file you drop in the  */
/* scan path, not where it happens to sit. (Two directories holding the same  */
/* basename is the double-load bug of 2026-08-05; one name, one module.)      */
static NodeObj FindLibraryByFile(char *file)
{
	NodeObj library;
	char *have;

	if (!file || !file[0])
		return NULL;

	for (library = GetChild(RegObjList); library; library = GetNextSibling(library)) {
		have = GetPropStr(library, "File");
		if (have && strcmp(have, file) == 0)
			return library;
	}
	return NULL;
}

/* A version is a TUPLE, not a number: "1.10" sorts below "1.9" as a string  */
/* and converts to 1.1 as a REAL, so major and minor stay separate values     */
/* (same shape as version.h's RELEASEMAJOR/RELEASEMINOR). Compatible means    */
/* the major matches exactly and the minor is at least what was asked for.    */
/* Asking for nothing accepts anything, so an undeclared dependency still     */
/* resolves by name alone.                                                    */
int ClassVersionOk(NodeObj class, char *wantMajor, char *wantMinor)
{
	if (!class)
		return 0;
	if (!wantMajor || !wantMajor[0])
		return 1;

	if (GetPropInt(class, "Major") != atoi(wantMajor))
		return 0;

	if (!wantMinor || !wantMinor[0])
		return 1;

	return GetPropInt(class, "Minor") >= atoi(wantMinor);
}

/* Declare a class this module actually uses. One entry NODE per dependency   */
/* under the library's own "Dependencies" property - not a packed string:      */
/* file + class + version in a comma list needs a second separator and a       */
/* fixed buffer, and truncation there silently drops enforcement of whatever    */
/* fell off the end. Nodes have no length to overrun.                           */
/* Both names are needed: the FILE is what the loader can act on now, while a   */
/* CLASS cannot be looked up until its own ClassStart has run - which is the    */
/* very thing being ordered. The class is then verified once the file is up.    */
void AddDependency(NodeObj library, char *file, char *classname,
				   char *major, char *minor)
{
	NodeObj deps, entry;

	if (!library || !file || !classname)
		return;

	deps = GetPropNode(library, "Dependencies");
	if (!deps) {
		deps = NewNode(INTEGER);
		SetName(deps, "Dependencies");
		AddProp(library, deps);
	}

	entry = NewNode(INTEGER);
	SetName(entry, "Dependency");
	SetPropStr(entry, "File", file);
	SetPropStr(entry, "Class", classname);
	SetPropStr(entry, "Major", major ? major : "");
	SetPropStr(entry, "Minor", minor ? minor : "");
	AddChild(deps, entry);
}

/* the symmetric half of AddDependency, for a module's _fini(): DelNode walks
   siblings, props and children, so removing the Dependencies property frees
   every entry under it. Properties carry no parent back-pointer, which is why
   RemoveProp needs the owner. */
void ClearDependencies(NodeObj library)
{
	NodeObj deps;

	if (!library)
		return;

	deps = GetPropNode(library, "Dependencies");
	if (!deps)
		return;

	RemoveProp(library, deps);
	DelNode(deps);
}

/* what a module publishes about a class it registers - the other half of the */
/* version gate: a widget asking for Object 1 0 will not load into a core     */
/* that has moved to 2 0.                                                     */
void SetClassVersion(NodeObj class, char *major, char *minor)
{
	if (!class)
		return;
	SetPropStr(class, "Major", major ? major : "0");
	SetPropStr(class, "Minor", minor ? minor : "0");
}

/* "I am one of these", which is a different claim from "load me after this":  */
/* a Widget CONTAINS controls (a dependency) but IS a Presentation (a parent). */
/* The parent's existence is already guaranteed by the matching dependency     */
/* entry, so an unresolvable name here means the module declared one without    */
/* the other - loud, because a class with no parent is broken, not degraded.    */
void SetClassParent(NodeObj class, char *parentname)
{
	NodeObj parent;
	char msg[200];

	if (!class || !parentname || !parentname[0])
		return;

	parent = FindClass(parentname);
	if (!parent) {
		snprintf(msg, sizeof(msg),
				 "class '%s' names parent '%s', which is not registered",
				 GetNameStr(class), parentname);
		DebugPrint(msg, __FILE__, __LINE__, ERROR);
		return;
	}

	SetPropStr(class, "Parent", parentname);
	SetPropLong(class, "ParentClass", (long) parent);

	/* AND MOVE IT. The parent link is the walkable relation, so it is the
	   one that gets the tree: a class becomes a child of its parent class,
	   and GetParent walks instance -> class -> ... -> Object with no
	   lookup at all.

	   Re-pointable on purpose. Calling this again moves the class again,
	   which is what lets a class be spliced in between an existing class
	   and its parent - a module whose whole content is one overridden
	   handler, dropped in the scan path, with neither end recompiled or
	   even aware. Nothing caches the resolved chain, so a splice takes
	   effect on the next message. */
	if (GetParent(class) != parent)
	{
		DelSibling(class);			/* out of the library, or the old parent */
		AddChild(parent, class);
	}
}

/* true once every entry's file is Loaded AND the class it named exists at an */
/* acceptable version. Anything short of that leaves the library unstarted -   */
/* see ReportUnmet for why that is not best-effort any more.                   */
/* The two indexes bring-up resolves against: file name -> library node, and
   class name -> class node. Both are the trie namespace.c already provides
   (the same structure the path index uses), so a lookup costs the length of
   the key instead of a walk over every library, or every class of every
   library. That is the difference between "fine for fifty modules" and
   "fine for ten thousand": FindLibraryByFile and FindClass are linear, and
   they were being called once per dependency per pass. */
typedef struct {
	NSObj *files;		/* "widget.object"  -> NodeObj library */
} DepIndex;

/* a library and how many of its dependencies were unmet when the working
   list was built - the sort key, taken once */
typedef struct {
	NodeObj library;
	int     unmet;
} DepRank;

static int DepRankCmp(const void *a, const void *b)
{
	return ((const DepRank *) a)->unmet - ((const DepRank *) b)->unmet;
}

/* every class a library registered becomes findable the moment it starts -
   nothing can depend on a class before its library has run ClassStart */
/* ONE dependency entry. Once satisfied it is stamped Met and never asked
   about again - so the total work is one resolution per declared
   dependency, no matter how many times its library is revisited. */
static int DependencyMet(DepIndex *ix, NodeObj entry)
{
	NodeObj depLib, depClass;
	char   *file, *classname;

	if (GetPropInt(entry, "Met"))
		return 1;

	file = GetPropStr(entry, "File");
	depLib = file ? (NodeObj) NSSearch(ix->files, file) : NULL;
	if (!depLib || !GetPropInt(depLib, "Loaded"))
		return 0;

	/* the class index is permanent and maintained by RegisterClass, so a
	   class is findable the instant its module registers it - no per-sweep
	   copy to build and nothing to keep in step */
	classname = GetPropStr(entry, "Class");
	depClass = FindClass(classname);
	if (!ClassVersionOk(depClass, GetPropStr(entry, "Major"),
						GetPropStr(entry, "Minor")))
		return 0;

	SetPropInt(entry, "Met", 1);
	return 1;
}

/* how many of a library's dependencies are still unmet - and the ones that
   became met since last time are struck off on the way past */
static int UnmetCount(DepIndex *ix, NodeObj library)
{
	NodeObj deps, entry;
	int unmet = 0;

	deps = GetPropNode(library, "Dependencies");
	for (entry = deps ? GetChild(deps) : NULL; entry; entry = GetNextSibling(entry))
		if (!DependencyMet(ix, entry))
			unmet++;

	return unmet;
}


/* Name what is actually missing, per entry. Reached when a sweep makes no    */
/* progress: the library never starts, so a widget whose control module is    */
/* absent is MISSING from the palette rather than quietly built without it.   */
static void ReportUnmet(NodeObj library)
{
	NodeObj deps, entry, depLib, depClass;
	char msg[300];
	char *file, *classname;

	deps = GetPropNode(library, "Dependencies");
	for (entry = deps ? GetChild(deps) : NULL; entry; entry = GetNextSibling(entry)) {
		file      = GetPropStr(entry, "File");
		classname = GetPropStr(entry, "Class");
		depLib    = FindLibraryByFile(file);

		if (!depLib)
			snprintf(msg, sizeof(msg), "'%s' needs file '%s' - not loaded",
					 GetNameStr(library), file ? file : "");
		else if (!GetPropInt(depLib, "Loaded"))
			snprintf(msg, sizeof(msg),
					 "'%s' needs '%s' from '%s' - that file's own dependencies are unmet",
					 GetNameStr(library), classname ? classname : "", file ? file : "");
		else if (!(depClass = FindClass(classname)))
			snprintf(msg, sizeof(msg),
					 "'%s' needs class '%s' - '%s' loaded but never registered it",
					 GetNameStr(library), classname ? classname : "", file ? file : "");
		else if (!ClassVersionOk(depClass, GetPropStr(entry, "Major"),
								 GetPropStr(entry, "Minor")))
			snprintf(msg, sizeof(msg),
					 "'%s' needs '%s' version %s.%s, found %s.%s",
					 GetNameStr(library), classname ? classname : "",
					 GetPropStr(entry, "Major"), GetPropStr(entry, "Minor"),
					 GetPropStr(depClass, "Major"), GetPropStr(depClass, "Minor"));
		else
			continue;

		DebugPrint(msg, __FILE__, __LINE__, ERROR);
	}
}

/*
 * Dependency-ordered class bring-up: every library scanned/dlopen'd by
 * InstallObjects() is sitting in RegObjList by now (see main.c), each
 * carrying the Dependencies its own _init() declared (AddDependency).
 * This sweeps repeatedly, calling ClassStart on whichever libraries have
 * every dependency satisfied, until nothing is left.
 *
 * A pass that makes no progress used to load the rest in registration order
 * with one warning. It does not any more: a module whose dependency is
 * missing or the wrong version is precisely the thing the version gate
 * exists to stop - an old widget must not come up inside a newer core. Those
 * libraries stay unstarted and ReportUnmet names, per entry, exactly what
 * was wanted and what was found.
 */
/* One line for what was found, in the order it was loaded, instead of a
   line per file. RegObjList holds them in reverse (AddChild prepends), so
   the names are built back to front. A module whose registered class name
   is just its filename says nothing worth reading; only the ones that
   differ are named, and that is the whole point of the second line. */
static void ReportLoadedObjects(void)
{
	NodeObj library;
	char list[3000], renamed[1500], one[200], msg[3200];
	char *file, *name, *dot, *core = NULL;
	int  n = 0, r = 0;
	size_t len;

	list[0] = renamed[0] = '\0';

	for (library = GetChild(RegObjList); library; library = GetNextSibling(library))
	{
		file = GetPropStr(library, "File");
		name = GetNameStr(library);
		if (!file || !file[0])
			continue;				/* nothing on disk to name */

		/* the core is not something the scan FOUND - it carries a File only
		   because dependencies name it, and its classes are registered
		   before any module is loaded. Counting it here is what makes the
		   totals read as one short. */
		if (strcmp(file, CORE_LIBRARY_FILE) == 0)
		{
			core = name;
			continue;
		}

		/* prepend: the list is newest-first, the load order is the reverse */
		snprintf(one, sizeof(one), "%s%s", file, list[0] ? ", " : "");
		len = strlen(one) + strlen(list);
		if (len < sizeof(list) - 1)
		{
			memmove(list + strlen(one), list, strlen(list) + 1);
			memcpy(list, one, strlen(one));
		}
		n++;

		/* file "slider.object" vs class name "Slider" is not news - case is
		   not a change. A name that is genuinely something else is. */
		dot = strrchr(file, '.');
		if (name && name[0] &&
			(dot ? strncasecmp(file, name, (size_t)(dot - file)) != 0 ||
				   strlen(name) != (size_t)(dot - file)
				 : strcasecmp(file, name) != 0))
		{
			snprintf(one, sizeof(one), "%s%s -> %s", renamed[0] ? ", " : "", file, name);
			if (strlen(renamed) + strlen(one) < sizeof(renamed) - 1)
				strcat(renamed, one);
			r++;
		}
	}

	snprintf(msg, sizeof(msg), "list of objs (%d, in load order): %s%s%s", n, list,
			 core ? " -- plus the core, " : "", core ? core : "");
	DebugPrint(msg, __FILE__, __LINE__, PROG_FLOW);

	if (r)
	{
		snprintf(msg, sizeof(msg), "objs whose class name is not their filename (%d): %s",
				 r, renamed);
		DebugPrint(msg, __FILE__, __LINE__, PROG_FLOW);
	}
}

void
loadClasses(void){
	NodeObj library;
	int madeProgress, remaining, sweep = 0, started, marked = 0;
	msgobj ClassStart;
	char dbg[400], order[3000];

	ReportLoadedObjects();

	remaining = 0;
	library = GetChild(RegObjList);
	while (library) {
		/* the core's own classes are registered before any module and are
		   always available - never re-run, never counted as pending */
		if (!GetPropInt(library, "Loaded"))
			remaining++;
		library = GetNextSibling(library);
	}

	started = 0;
	order[0] = '\0';

	if (remaining > 0) {
		NodeObj *work = malloc((size_t) remaining * sizeof(NodeObj));
		int  head = 0, queued = 0, cap = remaining, i, j, stalled = 0, thisPass;
		DepIndex ix;

		if (!work) {
			DebugPrint("loadClasses: out of memory building the working list",
					   __FILE__, __LINE__, ERROR);
			return;
		}

		/* index every library by the file it came from. Classes need no
		   pass of their own any more: RegisterClass keeps the permanent
		   class index, so the core's own classes are already in it and
		   every module's appear the instant its ClassStart registers
		   them. */
		ix.files   = NSCreate();
		for (library = GetChild(RegObjList); library; library = GetNextSibling(library)) {
			char *file = GetPropStr(library, "File");

			if (file && file[0])
				NSInsert(ix.files, file, (long) library);
		}

		/* the working list: every unstarted library, FEWEST DEPENDENCIES
		   FIRST, so the ones that can go immediately are not queued behind
		   the ones that cannot. The count is taken ONCE per library and
		   sorted on - computing it inside the comparison is how a sort of
		   ten thousand becomes a hundred million dependency walks. */
		{
			DepRank *rank = malloc((size_t) remaining * sizeof(DepRank));

			if (!rank) {
				DebugPrint("loadClasses: out of memory ranking the working list",
						   __FILE__, __LINE__, ERROR);
				NSRelease(ix.files);
				free(work);
				return;
			}

			for (library = GetChild(RegObjList); library; library = GetNextSibling(library)) {
				if (GetPropInt(library, "Loaded"))
					continue;
				rank[queued].library = library;
				rank[queued].unmet   = UnmetCount(&ix, library);
				queued++;
			}

			qsort(rank, (size_t) queued, sizeof(DepRank), DepRankCmp);

			for (i = 0; i < queued; i++)
				work[i] = rank[i].library;

			free(rank);
		}

		/* Take from the front; a library whose dependencies are all met
		   starts and leaves the list, and one still waiting goes to the
		   BACK to be revisited after the others have had their turn. Two
		   full traversals with nothing leaving means nothing can ever
		   leave - the remainder is a cycle or an unmet requirement, and it
		   gets dumped rather than retried forever. */
		thisPass = queued;
		while (queued > 0) {
			library = work[head];
			head = (head + 1) % cap;
			queued--;

			if (UnmetCount(&ix, library) == 0) {
				ClassStart = (msgobj) GetPropLong(library, "ClassStart");
				if (ClassStart) ClassStart(library, 0, NULL);
				SetPropInt(library, "Loaded", 1);
				remaining--;
				started++;
				stalled = 0;

				/* the order, one line, with the passes marked in it: the
				   pass a library came up in IS the dependency depth it sat
				   at, so [1] is everything that waited for nothing. */
				if (marked != sweep) {
					snprintf(dbg, sizeof(dbg), "%s[%d] ", order[0] ? " | " : "", sweep + 1);
					marked = sweep;
				}
				else
					snprintf(dbg, sizeof(dbg), ", ");

				if (strlen(order) + strlen(dbg) + strlen(GetNameStr(library))
					< sizeof(order) - 1) {
					strcat(order, dbg);
					strcat(order, GetNameStr(library));
				}
			}
			else {
				work[(head + queued) % cap] = library;	/* to the back */
				queued++;
				stalled++;
			}

			if (--thisPass <= 0) {			/* one traversal of the list */
				thisPass = queued;
				sweep++;
				marked = sweep - 1;
			}

			if (queued > 0 && stalled >= 2 * queued) {
				snprintf(dbg, sizeof(dbg),
						 "CLASS START stalled: %d librar(ies) went two full passes "
						 "without one starting - a cycle, or a requirement nothing "
						 "provides. The ring, and what each one is waiting for:",
						 queued);
				DebugPrint(dbg, __FILE__, __LINE__, ERROR);

				for (i = 0, j = head; i < queued; i++, j = (j + 1) % cap) {
					snprintf(dbg, sizeof(dbg), "  stalled: %s", GetNameStr(work[j]));
					DebugPrint(dbg, __FILE__, __LINE__, ERROR);
					ReportUnmet(work[j]);
				}
				break;
			}
		}

		NSRelease(ix.files);
		free(work);
		madeProgress = started > 0;
		(void) madeProgress;
	}

	{
		char line[3300];

		snprintf(line, sizeof(line),
				 "classes started: %d in %d sweep(s), %d left unstarted: %s",
				 started, sweep, remaining, order);
		DebugPrint(line, __FILE__, __LINE__, PROG_FLOW);
	}
}

void
UnloadClasses(void){
	NodeObj library = GetChild( RegObjList );
	while (library) {
		msgobj ClassEnd = (msgobj)GetPropLong(library, "ClassEnd");
		if (ClassEnd) ClassEnd(library, 0, NULL);
		library = GetNextSibling(library);
	}
}


		//printf ("In core:     Class callbacks: %lu, %lu, %lu\n", (long)ClassStart, (long)ClassEnd, (long)ClassMsg);
		//msgobj ClassEnd   = (msgobj)GetPropLong(library, "ClassEnd");
		//msgobj ClassMsg   = (msgobj)GetPropLong(library, "ClassMsg");
		//PrintNode(library);

NodeObj
CreateContainer(NodeObj container, char * name){

        // these containers just exist in our nodes to organize groups of objects together.
        // evolve into an application grouping with a schedule? 

        // these could be functional organizations
        // later we could also have these same objects in multiple views in logical organizations


        // need to check to see if name already exists
	NodeObj temp = NewNode(INTEGER);
	SetName(temp, name);

	AddChild(container, temp);

	return temp;
}

/*
 * Users as nodes: Main/Users/<name>, each with its own Canvas container
 * (via CreateContainer - it does not mean anything more than any other
 * container yet, ready for Phase 5's container ports to make it real).
 * Token auth only for now - the roadmap explicitly sequences TLS after
 * this, and TLS means linking OpenSSL, a real new dependency this build
 * does not have; the VNOS reference TCPObject.c has the SSL_* call
 * shape to port when that happens.
 */
NodeObj CreateUser(NodeObj main, char *name, char *token){

	NodeObj users, user;

	if (!main || !name)
		return NULL;

	users = GetPropNode(main, "Users");
	if (!users) {
		users = NewNode(INTEGER);
		SetName(users, "Users");
		AddProp(main, users);
	}

	user = NewNode(INTEGER);
	SetName(user, name);
	SetPropStr(user, "Token", token ? token : "");
	AppendChild(users, user);

	CreateContainer(user, "Canvas");

	return user;
}

NodeObj FindUser(NodeObj main, char *name){

	NodeObj users, user;

	users = main ? GetPropNode(main, "Users") : NULL;
	if (!users || !name)
		return NULL;

	user = GetChild(users);
	while (user) {
		if (CmpName(user, name))
			return user;
		user = GetNextSibling(user);
	}
	return NULL;
}

/* the user node on success, NULL on an unknown user or a token mismatch */
NodeObj AuthenticateUser(NodeObj main, char *name, char *token){

	NodeObj user;
	char *stored;

	user = FindUser(main, name);
	if (!user)
		return NULL;

	stored = GetPropStr(user, "Token");
	if (!stored || !token || strcmp(stored, token) != 0)
		return NULL;

	return user;
}

/* WALKING THE REGISTRY WITHOUT KNOWING ITS SHAPE.

   Every caller that wanted "every class" or "every instance" wrote the
   nested loop itself - seventeen of them, each spelling out
   RegObjList -> library -> class -> instance. That is a private copy of the
   registry's shape in seventeen places, and every one of them has to be
   found and edited the day the shape changes.

   A cursor instead of a nested loop. `for (c = FirstClass(); c; c =
   NextClass(c))` reads the same, keeps early exits working, and asks the
   registry where to go next instead of assuming. What a class's parent is,
   and where instances hang, becomes these four functions' business and
   nobody else's.

   GetParent is what makes the cursor possible: a node knows where it sits,
   so a walk can resume from any node without carrying the levels above it
   in local variables. */

/* A class node's children are its subclasses AND its instances, so every
   step of both walks asks which it is looking at. IsClass is stamped by
   RegisterClass and by nothing else. */
static int IsClassNode(NodeObj n)
{
	return n && GetPropInt(n, "IsClass");
}

static NodeObj FirstClassChild(NodeObj n)
{
	NodeObj c;

	for (c = n ? GetChild(n) : NULL; c; c = GetNextSibling(c))
		if (IsClassNode(c))
			return c;

	return NULL;
}

static NodeObj NextClassSibling(NodeObj n)
{
	for (n = n ? GetNextSibling(n) : NULL; n; n = GetNextSibling(n))
		if (IsClassNode(n))
			return n;

	return NULL;
}

/* the first class of this library or, failing that, of any library after
   it - a library that registered nothing, or whose classes have all moved
   under their parents, is skipped rather than ending the walk */
static NodeObj FirstClassFrom(NodeObj library)
{
	NodeObj class;

	for (; library; library = GetNextSibling(library))
		if ((class = FirstClassChild(library)))
			return class;

	return NULL;
}

NodeObj FirstClass(void)
{
	NodeObj root = GetRegObjList();

	return FirstClassFrom(root ? GetChild(root) : NULL);
}

/* depth first, because the type tree is a tree: down into subclasses,
   then across, then out to the next root class and eventually the next
   library. Every class is reached exactly once whether it sits under its
   library (a root, like Object) or under its parent class. */
NodeObj NextClass(NodeObj class)
{
	NodeObj n, p;

	if (!class)
		return NULL;

	if ((n = FirstClassChild(class)))
		return n;

	for (p = class; IsClassNode(p); p = GetParent(p))
		if ((n = NextClassSibling(p)))
			return n;

	/* climbed past the last root class: p is the library that held it */
	return FirstClassFrom(p ? GetNextSibling(p) : NULL);
}

static NodeObj FirstInstanceChild(NodeObj class)
{
	NodeObj c;

	for (c = class ? GetChild(class) : NULL; c; c = GetNextSibling(c))
		if (!IsClassNode(c))
			return c;

	return NULL;
}

static NodeObj NextInstanceSibling(NodeObj inst)
{
	for (inst = inst ? GetNextSibling(inst) : NULL; inst; inst = GetNextSibling(inst))
		if (!IsClassNode(inst))
			return inst;

	return NULL;
}

/* the first instance of this class or of any class after it */
static NodeObj FirstInstanceFrom(NodeObj class)
{
	NodeObj inst;

	for (; class; class = NextClass(class))
		if ((inst = FirstInstanceChild(class)))
			return inst;

	return NULL;
}

NodeObj FirstInstance(void)
{
	return FirstInstanceFrom(FirstClass());
}

NodeObj NextInstance(NodeObj inst)
{
	NodeObj next;

	if (!inst)
		return NULL;

	if ((next = NextInstanceSibling(inst)))
		return next;

	return FirstInstanceFrom(NextClass(GetParent(inst)));
}

/* THE DISPATCH LADDER.

   A handler that returns rtrn_dropped has said "not mine". Until now that
   ended there. It is the punt signal - already specified, already returned
   by every handler that declines - and this gives it somewhere to go:

     an instance that drops    -> its class
     a class that drops        -> its parent class
     Object                    -> handles it or drops it, and a drop there
                                  is a real drop

   A class opts in by putting a ClassMsg on its class node; a class with
   none is simply transparent and the message carries on up. So this
   changes nothing until a class implements one.

   WALKED LIVE, EVERY TIME. Nothing here caches the chain, because a class
   can be spliced in between an existing class and its parent at runtime -
   a module whose whole content is one overridden handler, dropped in the
   scan path, with neither end recompiled or aware. A cached chain is
   exactly what would make such a splice not take effect. The cost is a
   pointer hop per level on a message that was already refused.

   The property the message was about is the data node's name, same
   convention as every other handler. */
int PuntToClass(NodeObj instance, MsgId message, NodeObj data)
{
	NodeObj class;
	msgobj  handler;
	int     verdict;
	char    dbg[300];

	for (class = ClassOfInstance(instance); IsClassNode(class); class = GetParent(class))
	{
		handler = (msgobj) GetPropLong(class, "ClassMsg");
		if (!handler)
			continue;

		verdict = handler(instance, message, data);

		snprintf(dbg, sizeof(dbg), "PUNT '%s'.%s -> class '%s' said %s",
				 GetPropStr(instance, "Name") ? GetPropStr(instance, "Name") : "?",
				 data && GetNameStr(data) ? GetNameStr(data) : "?",
				 GetNameStr(class) ? GetNameStr(class) : "?",
				 verdict == rtrn_handled   ? "HANDLED" :
				 verdict == rtrn_propagate ? "PROPAGATE" :
				 verdict == rtrn_dropped   ? "dropped, punting on" : "an unknown code");
		DebugPrint(dbg, __FILE__, __LINE__, WIRE);

		if (verdict != rtrn_dropped)
			return verdict;
	}

	return rtrn_dropped;
}

/* the class an instance is one of, and the library a class came from -
   asked rather than remembered, so a caller that has one end of the walk
   can get the other without having carried it */
NodeObj ClassOfInstance(NodeObj inst)
{
	return inst ? GetParent(inst) : NULL;
}

/* the tree above a class is its PARENT CLASS now, so the library it came
   from is the property RegisterClass stamped */
NodeObj LibraryOfClass(NodeObj class)
{
	return class ? (NodeObj) GetPropLong(class, "Library") : NULL;
}

/* the registry root - RegObjList -> libraries -> classes - for anything */
/* that needs to walk every class (a palette) rather than find one by    */
/* name; GetChild/GetNextSibling at both levels is all that takes        */
NodeObj GetRegObjList(void){
	return RegObjList;
}


/* walk the registry looking for a registered class by name */
/* the registry is RegObjList -> libraries -> classes        */
NodeObj
FindClass(char * classname){

	if (!classname || !classname[0])
		return NULL;

	return (NodeObj) NSSearch(GetClassIndex(), classname);
}

/* The same walk, a different question: which class SHOWS a property of     */
/* this kind. A class states it (Renders, on the class node) rather than    */
/* anyone inferring it, so a control that ships tomorrow answers for its    */
/* own type the moment it loads, with no table to edit anywhere. This is    */
/* what lets the engine turn "alias that property" into a real control      */
/* without naming one.                                                      */
NodeObj FindClassRendering(int widget)
{
	NodeObj class;

	if (!widget)
		return NULL;

	for (class = FirstClass(); class; class = NextClass(class))
		if (GetPropInt(class, "Renders") == widget)
			return class;

	return NULL;
}

/* MADE SOMEWHERE, ANSWERING TO NOBODY. A private handle is a real instance
   its owner keeps a pointer to and nothing else can reach: the socket
   inside a port widget, the language host inside a ScriptBox. It is placed
   - it has to live somewhere, like everything else - but it is never named,
   so no path resolves to it, no listing announces it and no save records
   it.

   That state was reached by OMISSION: create it and then just don't name
   it. Which works, and is indistinguishable from forgetting to, and left
   the engine unable to tell a private part from a member whose naming had
   not happened yet - the same ambiguity that made "no path" unloggable.
   Saying it is what makes the difference real. */
NodeObj
CreatePrivate(NodeObj container, char * classname, char * wanted){

	NodeObj class, inst, place;
	msgobj InstanceStart;
	char cpath[300];

	class = FindClass(classname);
	if (!class) {
		char miss[200];
		snprintf(miss, sizeof(miss),
				 "CreatePrivate('%s'): no such registered class - its module is not"
				 " loaded, or its own dependencies were unmet",
				 classname ? classname : "");
		DebugPrint ( miss, __FILE__, __LINE__, ERROR);
		return NULL;
	}

	InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	if (!InstanceStart) {
		char miss[200];
		snprintf(miss, sizeof(miss),
				 "CreatePrivate('%s'): that class has no InstanceStart, so nothing"
				 " can instantiate it", classname ? classname : "");
		DebugPrint ( miss, __FILE__, __LINE__, ERROR);
		return NULL;
	}

	/* EVERYTHING IS ATTACHED WHERE IT LIVES, AT CREATION - so the place is
	   checked BEFORE anything is made. A missing or unaddressable container
	   is not quietly turned into "the root": nothing is created at all, the
	   reason is printed, and the caller gets NULL. Half-making something and
	   dropping it somewhere convenient helps nobody - it just moves the
	   failure to whoever finds it loose on a canvas later. */
	{
		char dbg[400];

		if (!container) {
			snprintf(dbg, sizeof(dbg),
					 "CreatePrivate('%s') REFUSED: no container given. Every object "
					 "is created somewhere - pass the view it belongs in.", classname);
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
			return NULL;
		}

		if (!PathOfInstance(container, cpath, sizeof(cpath))) {
			snprintf(dbg, sizeof(dbg),
					 "CreatePrivate('%s') REFUSED: the container has no path of its "
					 "own, so it cannot hold anything yet. container path: '%s'", classname, GetPropStr(container, "Container"));
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
			PrintNode(container);
			return NULL;
		}
	}

	/* A THING IS BUILT IN A LOCATION, UNDER A NAME - so the constructor is
	   handed both. Neither arrived before, which is why a widget could not
	   build its own panel: the controls go INSIDE it, so it must already be
	   somewhere, and under its FINAL name or its children are created under
	   a path that stops existing the moment it is renamed. That is the
	   whole reason panel construction was ever deferred to a task and
	   needed a flag to remember it had happened.
	   The place is a plain node, freed here - the constructor reads what it
	   needs and the engine keeps nothing. */
	place = NewNode(INTEGER);
	SetPropLong(place, "Container", (long) container);
	if (wanted && wanted[0])
		SetPropStr(place, "Name", wanted);

	InstanceStart(class, msg_initialize, place);
	DelNode(place);

	inst = (NodeObj)GetPropLong(class, "LastInstance");
	if (!inst) {
		DebugPrint("CreatePrivate: the class made no instance", __FILE__, __LINE__, ERROR);
		return NULL;
	}

	SetPropStr(inst, "Container", cpath);

	/* TEMPORARILY SILENCED 2026-08-12: one line per object created, which
	   at boot is every control of every panel. Commented out, NOT removed:
	   restore it when tracing placement. */
	/*
	{
		char dbg[400];
		snprintf(dbg, sizeof(dbg), "CreatePrivate %s -> %s", classname, cpath);
		DebugPrint(dbg, __FILE__, __LINE__, PLACE);
	}
	*/

	return inst;
}

/* An ordinary member of its container: placed the same way, and NAMED, so
   it can be addressed, listed, saved and wired by path from the moment it
   exists. The difference from a private handle is the name, and it is the
   only difference.

   NAMING IS PART OF CREATION. It used to be the caller's next line, which
   left a live, placed, class-registered instance that nothing could reach
   for as long as the caller took to get round to it - and every caller
   closed that window by hand, with the same three lines, re-resolving the
   container path this function had resolved and thrown away. Worse than
   the duplication: an object could not build its own panel in its own
   constructor, because a panel is made of objects created IN it and
   creating something in it needs its path. That is the only reason widget
   construction is in two halves.

   The name minted here is a placeholder in the sense that anyone may name
   it something better on the next line - RegisterPath treats a second name
   as a move and retires this one. It is not a placeholder in any sense
   that matters to the fabric: the thing is addressable now, and there is
   no moment when it is not. */
NodeObj
CreateObject(NodeObj container, char * classname, char * wanted){

	NodeObj inst;
	char cpath[300], name[192], path[512];

	/* the name is settled BEFORE the thing is made, so the constructor can
	   be told it and place its own contents under the path it will keep */
	if (!RequirePathOf(container, cpath, sizeof(cpath)))
		return NULL;

	if (wanted && wanted[0])
		snprintf(name, sizeof(name), "%s", wanted);
	else
		MintFreshName(classname, cpath, name, sizeof(name));

	inst = CreatePrivate(container, classname, name);
	if (!inst)
		return NULL;

	/* THE NAME IT WAS GIVEN IS THE NAME IT GETS. A caller almost always
	   knows: Widget_Create is told "Enable", import carries the saved name,
	   a client sends one. Minting over the top of that named every object
	   twice - Checkbox_1 then Enable - and the first name is the one a
	   widget's own children were created under, so renaming the parent
	   afterwards left them addressed somewhere that no longer existed.
	   Minting is for the caller with genuinely no name (a palette drop). */
	/* a constructor that placed itself under the name it was handed is
	   already right where it belongs; this is for everything that did not */
	if (!PathOfInstance(inst, path, sizeof(path)))
	{
		snprintf(path, sizeof(path), "%s/%s", cpath, name);
		RegisterPath(path, inst);
	}

	return inst;
}


/* THE rule for a name the server mints: the base with any trailing _N
   stripped, then the lowest free _k in the container. A clone and an import
   both use it, so View_2 becomes View_3 and never View_2_1.
   Why it matters beyond tidiness: View_2 is a PREFIX of View_2_1, and every
   path remap in an import is a prefix rewrite - so a suffixed name makes the
   copy's members indistinguishable from the source's, and the copy comes up
   wired to the original. Re-numbering keeps the two paths disjoint. */
void MintFreshName(char *base, char *containerPath, char *out, int outlen)
{
	char stem[200], full[400];
	int len, k;

	snprintf(stem, sizeof(stem), "%s", (base && base[0]) ? base : "Thing");

	len = (int) strlen(stem);
	while (len > 0 && stem[len - 1] >= '0' && stem[len - 1] <= '9')
		len--;
	if (len > 0 && len < (int) strlen(stem) && stem[len - 1] == '_')
		stem[len - 1] = 0;

	for (k = 1; k < 100000; k++)
	{
		snprintf(out, outlen, "%s_%d", stem, k);
		snprintf(full, sizeof(full), "%s/%s",
				 (containerPath && containerPath[0]) ? containerPath : "/Root", out);
		if (!NameTakenIn(out, containerPath) && !ResolvePath(full))
			return;
	}
}


/*
 * The alias mechanism's one moving part: resolve (instance, propname)
 * through any link chain to the pair that actually owns the property.
 * A linked prop node (LinkNode, node.c) carries a LinkInst prop naming
 * the owning instance (set by LinkProperty below, which collapses
 * chains at creation), so the caller gets back both the real prop node
 * AND the real instance - handlers need the owning instance to find
 * their "local" state, so resolving the node alone is not enough.
 * For a plain unlinked property this is exactly GetPropNode.
 */
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
NodeObj ResolvePort(NodeObj * instp, char * name)
{
	NodeObj raw, owner;

	if (!instp || !*instp || !name)
		return NULL;

	raw = GetPropNode(*instp, name);
	if (!raw)
		return NULL;

	if (GetNodeLink(raw))
	{
		owner = (NodeObj) GetPropLong(raw, "LinkInst");
		if (owner)
			*instp = owner;
		return ResolveNode(raw);
	}

	return raw;
}

/*
 * Expose targetInst's property on owner under `slot`, as a link.
 * Everything that resolves ports (Connect, SndMsg, SetOrDeliverProp,
 * the Bridge's subscribe) lands on the original: one value, one
 * subscriber list, no forwarding. Aliasing an alias collapses to the
 * final original at creation, so chains never grow at use time.
 *
 * The slot matters: an Alias instance keeps the link in its own "Value"
 * slot precisely so its OWN Name/Container/X/Y stay its own - linking a
 * target's Name under the name "Name" would hijack the alias's identity
 * (renaming the member renamed the target, and vice versa).
 */
int LinkPropertyAs(NodeObj owner, char * slot, NodeObj targetInst, char * propname)
{
	NodeObj targetProp, linknode, realOwner;

	if (!owner || !slot || !targetInst || !propname)
		return 0;

	realOwner = targetInst;
	targetProp = ResolvePort(&realOwner, propname);
	if (!targetProp)
		return 0;

	SetPropStrPrivate(owner, slot, "");		/* never through an existing link */
	linknode = GetPropNode(owner, slot);
	if (!linknode)
		return 0;

	SetPropLong(linknode, "LinkInst", (long) realOwner);
	LinkNode(linknode, targetProp);

	return 1;
}

/* Turn an existing Alias instance into a doorway onto one property of    */
/* another instance. The link lives in the alias's own "Value" slot, so    */
/* the value, the subscribers, and anything wired to it stay on the        */
/* original - a doorway, not a copy that has to be kept in step.           */
/* Target/TargetProp name the FINAL original (aliasing an alias collapses  */
/* at the link level, and events always carry the original's name), and    */
/* Widget is whatever presentation was declared for the property - by the  */
/* owner's class, or by the property itself. Safe to re-apply: a link is a */
/* pointer, which a saved flow cannot carry, so a restored alias needs it  */
/* put back.                                                               */
/* This is the ONE place an alias is made. Every caller - the bridge's     */
/* create-alias command, the panel a bridge builds out of a thing's own    */
/* properties, the import pass restoring a saved one - goes through here,  */
/* the way every caller that copies a thing goes through CloneInstance.    */
int AliasProperty(NodeObj aliasInst, NodeObj targetInst, char * propname)
{
	NodeObj owner, node, pub;
	char path[256];
	int widget;

	if (!aliasInst || !targetInst || !propname || !propname[0])
		return 0;

	if (!LinkPropertyAs(aliasInst, "Value", targetInst, propname))
		return 0;

	owner = targetInst;
	node = ResolvePort(&owner, propname);
	if (node)
		propname = GetNameStr(node);

	/* How the doorway draws: what the owner's class published for this      */
	/* property, else what the property itself says - a Widget sub-property   */
	/* on the property node, the same stamp one level down, which is how a    */
	/* property nobody published still knows what shows it. Left unstamped    */
	/* when neither has an answer: the core carries the number, it does not   */
	/* pick a control, so whoever renders decides what nothing means.         */
	pub = InterfacePropForInstance(owner, propname);
	widget = pub ? GetPropInt(pub, "Widget") : 0;
	if (!widget && node)
		widget = GetPropInt(node, "Widget");
	if (widget)
		SetPropInt(aliasInst, "Widget", widget);

	/* A menu's options travel with it. The convention is a companion
	   property named <prop>List on the same owner (Language -> LanguageList),
	   which is what Widget_Ctl already reflects into a Dropdown's Items when
	   a widget builds its own panel - so a control made HERE has to honour it
	   too, or the same control comes up with a value and no options and shows
	   blank. Linked, not copied: the list stays live, survives a rename, and
	   needs nothing re-made on a clone.
	   Not menu-only by test: if a companion list exists, the control gets it,
	   and a control that has no use for Items ignores the property. */
	{
		char listprop[128];

		snprintf(listprop, sizeof(listprop), "%sList", propname);
		if (GetPropNode(owner, listprop))
			LinkPropertyAs(aliasInst, "Items", owner, listprop);
	}

	if (PathOfInstance(owner, path, sizeof(path)))
		SetPropStr(aliasInst, "Target", path);
	SetPropStr(aliasInst, "TargetProp", propname);

	return 1;
}

/*
 * The same, making the instance too - the caller places and names it.
 *
 * THERE IS NO ALIAS OBJECT. What gets made is an ordinary control - the
 * class that says it renders this kind of property - whose own Value slot
 * links to the target's. Aliasing is a relationship between two things that
 * already exist, which is the same category as Connect and Move: mechanism
 * every app needs whatever it does. Cloning needs no clone object and moving
 * needs no move object, and for exactly the same reason neither does this.
 * A class was only ever invented here because this is the one gesture that
 * makes a NEW visible thing, and "it needs an instance" got read as "it needs
 * a class" - when the instance it needed was a control.
 *
 * Which control: what the owner's class published for the property, else what
 * the property itself declares, else text. That last step is not a give-up -
 * it is what makes late binding work. A property a script grew, or one that
 * nothing has described yet, is still readable and writable as text, so you
 * can alias it and it becomes something better the moment somebody says what
 * it is.
 */
NodeObj CreateAlias(NodeObj container, NodeObj targetInst, char * propname)
{
	NodeObj inst, owner, node, pub, class;
	char dbg[300];
	int widget;

	if (!container || !targetInst || !propname || !propname[0])
		return NULL;

	owner = targetInst;
	node = ResolvePort(&owner, propname);
	if (!node)
	{
		snprintf(dbg, sizeof(dbg),
				 "CreateAlias: '%s' has no property '%s' to stand for",
				 GetPropStr(targetInst, "Name"), propname);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return NULL;
	}

	pub = InterfacePropForInstance(owner, GetNameStr(node));
	widget = pub ? GetPropInt(pub, "Widget") : 0;

	/* PROP_NULL is the published way of saying "no particular control" - it
	   is the absence, not a kind. Asking which class renders no-control is a
	   category error, and treating it as a type is why X/Y/W/H/Container
	   (control.c publishes all of them PROP_NULL) had nothing to make and
	   went missing from every panel. It falls through to the same place a
	   property nobody described falls through to: text. */
	if (widget == PROP_NULL)
		widget = 0;
	if (!widget)
		widget = GetPropInt(node, "Widget");
	if (!widget)
		widget = PROP_TEXTBOX;

	class = FindClassRendering(widget);
	if (!class)
	{
		snprintf(dbg, sizeof(dbg),
				 "CreateAlias: nothing loaded says it renders property type %d, "
				 "so '%s'.%s cannot be shown - the control that draws it is "
				 "missing", widget, GetPropStr(targetInst, "Name"), propname);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return NULL;
	}

	inst = CreateObject(container, GetNameStr(class), NULL);
	if (!inst)
		return NULL;

	if (!AliasProperty(inst, targetInst, propname))
	{
		DeleteInstance(inst);
		return NULL;
	}

	/* NAMED FOR WHAT IT STANDS FOR - the instance it aliases and the
	   property: bob_1's Value is bob_1_Value. CreateObject names a new
	   thing after its class, which for an alias says only what draws it
	   and nothing about which thing it is a doorway onto. Minted in the
	   container so two aliases of the same property are still distinct. */
	{
		char base[192], newname[192], cpath[300];
		char *tn = GetPropStr(targetInst, "Name");

		snprintf(base, sizeof(base), "%s_%s",
				 (tn && tn[0]) ? tn : GetNameStr(class), propname);

		/* REGISTERED, not just renamed. It arrived with a minted name of
		   its own (CreateObject), so writing the Name alone would leave
		   that path in the index pointing at something that no longer
		   answers to it - registering the new name is what retires the
		   old one. */
		if (RequirePathOf(container, cpath, sizeof(cpath)))
		{
			char path[512];

			MintFreshName(base, cpath, newname, sizeof(newname));
			snprintf(path, sizeof(path), "%s/%s", cpath, newname);
			RegisterPath(path, inst);
		}
		else
			SetOrDeliverProp(inst, "Name", base);
	}

	snprintf(dbg, sizeof(dbg), "CreateAlias: '%s'.%s -> a %s pointing at it",
			 GetPropStr(targetInst, "Name"), propname, GetNameStr(class));
	DebugPrint(dbg, __FILE__, __LINE__, WIRE);

	return inst;
}

/*
 * Is this instance an alias - does it stand for somebody else's property?
 *
 * It asks the thing itself instead of asking what class it is, which is the
 * only question that still has an answer: an alias is any control whose Value
 * is a link, and the control can be a Knob, a Textbox, anything that renders
 * the property it points at. Aliasing never was a kind of object, so "what
 * class is it" was always answering a different question than the one being
 * asked - it just happened to agree while exactly one class did this.
 *
 * Fills targetInst/targetProp when asked (either may be NULL).
 */
int IsAlias(NodeObj inst, NodeObj * targetInst, char ** targetProp)
{
	NodeObj slot, owner;

	if (!inst)
		return 0;

	slot = GetPropNode(inst, "Value");
	if (!slot || !GetNodeLink(slot))
		return 0;

	owner = (NodeObj) GetPropLong(slot, "LinkInst");
	if (!owner)
		return 0;

	if (targetInst)
		*targetInst = owner;
	if (targetProp)
		*targetProp = GetNameStr(ResolveNode(slot));

	return 1;
}

/* same, exposed under the target property's own name */
int LinkProperty(NodeObj owner, NodeObj targetInst, char * propname)
{
	return LinkPropertyAs(owner, propname, targetInst, propname);
}

/*
 * The engine-level clone: a brand-new instance of source's class
 * carrying a snapshot of source's published data properties - its own
 * separate copy, nothing shared, nothing linked. Reading each value
 * through ResolvePort means cloning THROUGH an alias never copies the
 * alias's plumbing - it snapshots the original's live values. Naming,
 * containment, registration in a session, and eventing are the
 * caller's business (the Bridge's clone-instance verb does all of
 * that); this is just the node operation.
 */
/* copy ONE node's data - the private building block CloneInstance uses
   for the top instance and for each concrete member it walks. Not a
   public entry: you clone a THING (CloneInstance), which copies the node
   AND whatever the node contains. */
/* THE rule for "is this property portable data" - the one both directions of
   copying have to agree on, which is why it lives here rather than in either
   of them.

   Two tests. A LONG-valued property is a runtime pointer (local, Activate,
   OnMsg, a task handle) and means nothing to anyone else. An older same-named
   property is a stale shadow, since SetProp prepends and the newest is the
   one that counts.

   They disagreed before this existed: the serializer walked the instance's
   real properties while CloneObject walked the CLASS's published Interface,
   so an unpublished property survived export/import and was silently lost by
   clone - which is exactly how a generated MCP agent came back from a clone
   with no logic in it at all. */
int IsPortableProp(NodeObj inst, NodeObj prop)
{
	NodeObj q;
	char   *name;

	if (!inst || !prop)
		return 0;

	if (GetDataType(GetValueNode(prop)) == LONG)
		return 0;

	name = GetNameStr(prop);
	if (!name || !name[0])
		return 0;

	for (q = GetNextProp(inst); q && q != prop; q = GetNextSibling(q))
		if (strcmp(GetNameStr(q), name) == 0)
			return 0;

	return 1;
}


/* Copy the source's DATA onto an instance that already exists - and only
   its data. Structure is not copied: a widget's panel is built when the
   instance is created, from the table its class declares, exactly as the
   one in the palette was. This writes values onto what is already there. */
static void CloneData(NodeObj source, NodeObj inst)
{
	/* THE OBJECT HANDLES ITS OWN SERIALIZING, on this path too. Anything a
	   class keeps where the property walk cannot see it - a private object
	   reached by a LONG, which IsPortableProp refuses - is carried by the
	   class itself or not at all. Same two function pointers the serializer
	   asks for, so a class writes itself once and both paths work. */
	{
		NodeObj  cls = ClassOfInstance(source);
		char   *(*writeSelf)(NodeObj) =
			(char *(*)(NodeObj)) (cls ? GetPropLong(cls, "Serialize") : 0);
		void   (*readSelf)(NodeObj, char *) =
			(void (*)(NodeObj, char *)) (cls ? GetPropLong(cls, "Deserialize") : 0);

		if (writeSelf && readSelf)
		{
			char *own = writeSelf(source);

			if (own)
			{
				readSelf(inst, own);
				free(own);
			}
		}
	}

	NodeObj prop, valnode, owner;
	char *name, *val;

	if (!source || !inst)
		return;

	/* Copy what the SOURCE actually carries, not what its class published.
	   Those are different sets: a property added to one instance - an agent's
	   generated Source, anything a user annotates a single object with - is
	   real data that is simply not in the class Interface. Walking the
	   interface silently dropped all of it, so a clone came back missing
	   things an export/import of the same thing kept.
	   IsPortableProp is the shared rule; the serializer walks with the same
	   one, which is what makes the two paths agree. */
	for (prop = GetNextProp(source); prop; prop = GetNextSibling(prop))
	{
		name = GetNameStr(prop);
		if (!name || !IsPortableProp(source, prop))
			continue;
		if (strcmp(name, "State") == 0)		/* lifecycle, not data */
			continue;

		/* IDENTITY IS NOT DATA. The clone was created in its own place and
		   registered under its own name; writing the source's Name over it
		   would leave the path it answers to pointing at a name it no
		   longer has. The caller names it, the same way any creator does. */
		if (strcmp(name, "Name") == 0 || strcmp(name, "Container") == 0)
			continue;

		/* A LINK IS NOT A VALUE. The slot points at another instance's
		   property; copying the string it currently reads leaves the clone
		   holding a dead snapshot where the original had a live pointer.
		   Links are re-made in pass 1, when every member of the group
		   exists and the map can say which copy each should point at. */
		if (GetNodeLink(prop))
			continue;

		owner = source;
		valnode = ResolvePort(&owner, name);
		val = valnode ? GetValueStr(valnode) : NULL;
		if (!val)
			continue;

		/* Write it under THE NAME IT WAS READ UNDER. This used to call
		   SetOrDeliverProp, which resolves the name on the way in and then
		   rewrites it to whatever the resolution landed on - by design, so a
		   write through an alias reaches the original's real property. That is
		   wrong here: a fresh clone has nothing to write through, so the
		   resolution redirected the value into the target slot's name instead
		   of its own. A linked slot is called "Value", which is how a cloned
		   box ended up with its properties written one level down into Value.

		   The handler still gets its chance, but looked up on the CLONE's own
		   property of that name - never on whatever the name resolves to.

		   IF THIS EVER HAS TO BE REVISITED, here is the case it can break. The
		   old call had a second purpose besides reaching a handler: writing
		   THROUGH a link. If an instance's own property is legitimately a link
		   - a composite whose bind-port exposes an inner member's property as
		   its own - then copying it with SetPropStr puts a plain value in the
		   link slot instead of reaching the target, and the clone comes up
		   with a dead value where the original had a live link. Symptom: a
		   cloned composite's bound property stops tracking. widgettest and
		   scriptedwidgettest build bind-port composites, so a sweep should
		   catch it.
		   The repair then is NOT to go back to SetOrDeliverProp - that
		   reintroduces the rename this fixed. It is to notice that the
		   SOURCE's property is a link and reproduce the LINK on the clone
		   (LinkPropertyAs against the mapped target), rather than copying a
		   value at all. A link is not a value and should not be cloned as
		   one. */
		{
			NodeObj dst   = GetPropNode(inst, name);
			msgobj  onmsg = dst ? (msgobj) GetPropLong(dst, "OnMsg") : NULL;

			if (onmsg)
			{
				NodeObj chunk = NewNode(STRING);

				SetName(chunk, name);
				SetValueStr(chunk, val);
				onmsg(inst, msg_send, chunk);
				DelNode(chunk);
			}
			else
				SetPropStr(inst, name, val);
		}
	}
}

/* A CLONE IS AN INSTANCE OF THE SAME CLASS, MADE THE ONE WAY INSTANCES ARE
   MADE, with the source's data copied onto it. There is no second creation
   path here and there must not be: this used to call the class's
   InstanceStart directly and take LastInstance, which skipped everything
   CreateObject does - the name, the registration, and the class-chain
   initialize that builds a widget's declared panel. That is why a cloned
   widget came up with a panel full of copied values and nothing behind
   them. */
static NodeObj CloneObject(NodeObj source, NodeObj container, char * name)
{
	NodeObj class, inst;

	if (!source || !container)
		return NULL;

	class = GetParent(source);
	if (!class)
		return NULL;

	/* under the name it will KEEP: a widget builds its controls inside
	   itself as it is constructed, so renaming it afterwards would leave
	   them under a path that no longer exists */
	inst = CreateObject(container, GetNameStr(class), name);
	if (!inst)
		return NULL;

	CloneData(source, inst);

	return inst;
}

/* record a subscription on a source port. Each Subscriber carries the   */
/* sink instance, the NAME of the sink port/property the wire lands on,  */
/* and the handler the sink registered as OnMsg there - or 0 for a plain */
/* property, in which case delivery applies the universal default        */
/* (DeliverToSubscriber, node.c: store what arrived). Recording the port */
/* name is what makes the record self-describing: list-connections,     */
/* CloneConnections, Disconnect and the delete scrub all read the wire   */
/* straight off it, no adapter, no reverse handler lookup.               */
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
void AddSubscription(NodeObj fromPort, NodeObj toNode, char * toPort, long handler){

	NodeObj sub;

	/* ONE wire, not two: the same sink and port recorded twice on one
	   source delivers every message twice. This is reached with the record
	   already present whenever a widget's build re-makes wiring that a load
	   already restored - and a hand cannot draw the same wire twice either.
	   A later call carrying a real handler upgrades the record: that is how
	   a restored wire (Callback 0, the universal default) gets its compiled
	   handler back, since the pointer itself is never saved. */
	for (sub = GetNextProp(fromPort); sub; sub = GetNextSibling(sub))
	{
		char *p;

		if (!CmpName(sub, "Subscriber"))
			continue;
		if ((NodeObj) GetPropLong(sub, "Instance") != toNode)
			continue;
		/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
		p = GetPropStr(sub, "Port");
		if ((!p && !toPort) || (p && toPort && strcmp(p, toPort) == 0))
		{
			if (handler)
				SetPropLong(sub, "Callback", handler);
			return;
		}
	}

	sub = NewNode(INTEGER);
	SetName(sub, "Subscriber");
	SetPropLong(sub, "Instance", (long)toNode);
	if (toPort)
		/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
		SetPropStr(sub, "Port", toPort);
	SetPropLong(sub, "Callback", handler);
	AddProp(fromPort, sub);
}

/* see the comment in object.h - a copied group has to arrive wired to  */
/* itself, the same rule a deep-cloned view's aliases already follow    */
void CloneConnections(NodeObj srcInst, NodeObj cloneInst, NodeObj map){

	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	NodeObj port, sub, sink, sinkClone;
	char *portName, *toPort;

	char dbg[256];

	if (!srcInst || !cloneInst || !map)
		return;

	snprintf(dbg, sizeof(dbg), "CloneConnections: wiring clone of '%s' (its clone is '%s')",
			 GetPropStr(srcInst, "Name"), GetPropStr(cloneInst, "Name"));
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	/* every property is a potential source port - the ones that were    */
	/* actually wired are exactly the ones carrying Subscriber entries    */
	/* (AddSubscription, above), so no separate "is this a port" test is  */
	/* needed or wanted                                                    */
	for (port = GetNextProp(srcInst); port; port = GetNextSibling(port)) {

		portName = GetNameStr(port);
		if (!portName)
			continue;

		for (sub = GetNextProp(port); sub; sub = GetNextSibling(sub)) {

			if (!CmpName(sub, "Subscriber"))
				continue;

			/* every wire is one uniform record naming its REAL sink and    */
			/* the port it lands on ({Instance, Port} - AddSubscription).   */
			/* A record with no Port is not a user wire (a Bridge tap       */
			/* predating a reconnect, a test stub) and is left alone; a     */
			/* sink outside this cloned group (including every tap - taps   */
			/* are never in the map) is left alone too.                     */
			sink = (NodeObj) GetPropLong(sub, "Instance");
			/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
			toPort = GetPropStr(sub, "Port");
			if (!sink || !toPort)
				continue;

			sinkClone = (NodeObj) GetConnState(map, (long) sink);

			snprintf(dbg, sizeof(dbg),
					 "CloneConnections:   found wire %s.%s -> '%s'.%s; that sink %s cloned in this group",
					 GetPropStr(srcInst, "Name"), portName, GetPropStr(sink, "Name"), toPort,
					 sinkClone ? "WAS" : "was NOT");
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);

			if (!sinkClone)
				continue;

			/* re-make the wire between the copies with the same call a    */
			/* live one uses - the sink's clone is the same class, so       */
			/* Connect() finds the same handler (or the same absence of     */
			/* one) on the same port name                                    */
			Connect(cloneInst, portName, sinkClone, toPort);

			snprintf(dbg, sizeof(dbg),
					 "CloneConnections:   ADDED wire on clone: '%s'.%s -> '%s'.%s",
					 GetPropStr(cloneInst, "Name"), portName, GetPropStr(sinkClone, "Name"), toPort);
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
		}
	}
}

/*
 * Deep-clone a container and everything in it - the WHOLE clone, in the
 * engine, so it happens the same whoever asked (a script, or the html
 * through the bridge). CloneObject copies one node's data; this copies a
 * group and keeps it self-contained:
 *
 *   - every member (an instance whose Container is srcPath, skipping
 *     hidden plumbing) is cloned and re-homed under clonePath, recursively
 *     into nested containers;
 *   - an alias member is re-pointed at the CLONE of whatever it aliased
 *     inside the group; an alias to something OUTSIDE the group is left
 *     pointing where it did;
 *   - the wires between members are re-made between the clones.
 *
 * Naming the result and telling anyone about it are NOT here - that is the
 * translator's job (the bridge turns objects into html paths and events).
 * Every instance made is recorded in `map` (src pointer -> clone pointer,
 * GetConnState/SetConnState) so the caller can walk the clones out and
 * name/announce them. srcPath/clonePath are the container-path strings the
 * members' Container properties use; the caller owns that convention, the
 * engine only rewrites old -> new. Members keep their own Name (unique
 * already inside a fresh copy of their container); only the top is named
 * by the caller, via clonePath's basename.
 */
/* a thing's session path = its Container plus its Name (root-level things  */
/* have an empty Container and live under /Root). The engine owns this      */
/* now, because the engine owns naming.                                      */
static void InstancePath(NodeObj inst, char *out, int outlen)
{
	char *cont = GetPropStr(inst, "Container");
	char *nm   = GetPropStr(inst, "Name");

	if (cont && cont[0])
		snprintf(out, outlen, "%s/%s", cont, nm ? nm : "");
	else
		snprintf(out, outlen, "/Root/%s", nm ? nm : "");
}

/* is `name` already the Name of some instance sitting in containerPath?    */
/* the engine's own registry walk - names are unique within a container     */
static int NameTakenIn(char *name, char *containerPath)
{
	NodeObj inst;
	char *cont, *nm;

	/* THE SCAN, DELIBERATELY, not the Members index. The index holds
	   NAMED members - RegisterPath is what puts them there - and naming is
	   the translator's job, done after the engine returns (see
	   Bridge_RegisterClone). This question is asked WHILE a clone or a
	   load is placing siblings: their Container is set, their name is
	   being minted right now, and none of them are in any index yet.
	   Asking the Container property is the only thing that sees them, and
	   minting against a stale answer hands out a name already taken. */
	for (inst = FirstInstance(); inst; inst = NextInstance(inst))
	{
		cont = GetPropStr(inst, "Container");
		if (strcmp(cont ? cont : "", containerPath ? containerPath : "") != 0)
			continue;
		nm = GetPropStr(inst, "Name");
		if (nm && strcmp(nm, name) == 0)
			return 1;
	}

	return 0;
}

/* the engine names a clone: after what the user calls the source, with     */
/* any trailing _N stripped, then the lowest free _k in the target          */
/* container - so a "Slider_1" cloned beside itself becomes Slider_2 (not    */
/* Slider_1_1), and a view "CloneAliasTest" becomes CloneAliasTest_1.        */
static void CloneMintName(NodeObj source, char *containerPath, char *out, int outlen)
{
	char *nm = GetPropStr(source, "Name");
	char *b  = (nm && nm[0]) ? nm : GetNameStr(GetParent(source));

	MintFreshName(b, containerPath, out, outlen);
}

/* Re-make the links an instance carried, on its clone: same slot, pointing
   at the CLONE of whatever it pointed at - or at the original, when that
   target was outside the group being copied.

   There is no alias species and no second clone path. A control that shows
   another object's property is an ordinary instance whose slot happens to
   be a link, so it clones like everything else; this is the one thing a
   value copy cannot do, and it waits until every member exists. It runs
   over EVERY property, not just Value, so a composite whose own property
   is bound to an inner member's comes across bound too. */
static void CloneRelinkProps(NodeObj src, NodeObj clone, NodeObj map)
{
	NodeObj prop, target, mapped, resolved;
	char dbg[300];

	if (!src || !clone || !map)
		return;

	for (prop = GetNextProp(src); prop; prop = GetNextSibling(prop))
	{
		char *name = GetNameStr(prop);

		if (!name || !GetNodeLink(prop))
			continue;

		target = (NodeObj) GetPropLong(prop, "LinkInst");
		resolved = ResolveNode(prop);
		if (!target || !resolved)
			continue;

		mapped = (NodeObj) GetConnState(map, (long) target);
		if (mapped)
			target = mapped;
		else
		{
			char probe[256];

			/* NOT IN THE GROUP AND NOT ADDRESSABLE = THE ORIGINAL'S PRIVATE
			   STATE. A link out of the group is normally kept, because the
			   thing on the other end is shared and the copy should point at
			   it too. But an instance with no path is a class's own private
			   object - nothing else in the session can reach it - and
			   pointing the COPY at it wires the new widget's controls into
			   the original's data: typing in one changed the other. The copy
			   owns its own, made by its own InstanceStart, so leave the link
			   its build already set. */
			if (!PathOfInstance(target, probe, sizeof(probe)))
			{
				snprintf(dbg, sizeof(dbg),
						 "CLONE relink: '%s'.%s points at private state - left as the copy's own",
						 GetPropStr(clone, "Name") ? GetPropStr(clone, "Name") : "?", name);
				DebugPrint(dbg, __FILE__, __LINE__, CLONE);
				continue;
			}
		}

		if (LinkPropertyAs(clone, name, target, GetNameStr(resolved)))
			snprintf(dbg, sizeof(dbg), "CLONE relink: '%s'.%s -> '%s'.%s",
					 GetPropStr(clone, "Name"), name,
					 GetPropStr(target, "Name"), GetNameStr(resolved));
		else
			snprintf(dbg, sizeof(dbg),
					 "CLONE relink FAILED: '%s'.%s cannot point at '%s'.%s",
					 GetPropStr(clone, "Name"), name,
					 GetPropStr(target, "Name"), GetNameStr(resolved));
		DebugPrint(dbg, __FILE__, __LINE__, CLONE);
	}
}

/* one pass over a container's direct members (0: clone every member,      */
/* 1: re-make the links they carried, 2: clone the wires between them).    */
/* Every member goes through the same pass 0 - there is no second kind of  */
/* thing here. Later passes                                                */
/* need every clone to already exist, so it's three sweeps, recursing into */
/* nested views on each. The matching members are snapshotted into `list`  */
/* first, because cloning ADDS instances to the same registry this walks.  */
static void CloneGroupPass(char *srcPath, char *clonePath, NodeObj map, int pass)
{
	NodeObj list, inst, entry, clone;
	char *cont, *classname, *nm;
	char childSrc[256], childClone[256], key[24];
	char dbg[300];
	int n = 0;

	/* the scan, for the same reason as NameTakenIn: a clone of a clone
	   reads members that the engine placed but has not yet named, and the
	   index only knows named ones */
	list = NewNode(INTEGER);
	for (inst = FirstInstance(); inst; inst = NextInstance(inst))
	{
		cont = GetPropStr(inst, "Container");
		if (!cont || strcmp(cont, srcPath) != 0)
			continue;
		if (GetPropInt(inst, "_Hidden"))
			continue;	/* plumbing is not content */
		snprintf(key, sizeof(key), "%d", n++);
		SetPropLong(list, key, (long) inst);
	}

	snprintf(dbg, sizeof(dbg), "CLONE pass %d: %d member(s) in '%s' -> '%s'",
			 pass, n, srcPath, clonePath);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	for (entry = GetNextProp(list); entry; entry = GetNextSibling(entry))
	{
		inst = (NodeObj) GetValueLong(entry);
		classname = GetNameStr(GetParent(inst));
		nm = GetPropStr(inst, "Name");

		if (pass == 0)
		{
			/* IT IS PROBABLY ALREADY THERE. Creating the container built
			   whatever its class declares - a widget's panel and every
			   control in it - so the member with this name exists already
			   and only wants the source's values written onto it. Making a
			   second one would leave the declared control inert beside a
			   copy nothing is wired to. Anything the class does NOT declare
			   (something dropped into a plain view) is not there, and that
			   is the case that still creates. */
			char  memberPath[512];
			char *what;

			snprintf(memberPath, sizeof(memberPath), "%s/%s",
					 clonePath, (nm && nm[0]) ? nm : "");
			clone = (nm && nm[0]) ? ResolvePath(memberPath) : NULL;

			if (clone)
			{
				CloneData(inst, clone);
				what = "already declared - values copied onto it";
			}
			else
			{
				clone = CloneObject(inst, ResolvePath(clonePath), nm);
				what = clone ? "not declared by the class - created" : "FAILED";
			}

			if (clone)
				SetConnState(map, (long) inst, (long) clone);

			snprintf(dbg, sizeof(dbg), "CLONE pass 0: member '%s' (%s) %s",
					 nm ? nm : "?", classname, what);
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
		}
		else if (pass == 1)
		{
			clone = (NodeObj) GetConnState(map, (long) inst);
			if (clone)
				CloneRelinkProps(inst, clone, map);
		}
		else if (pass == 2)
		{
			clone = (NodeObj) GetConnState(map, (long) inst);
			snprintf(dbg, sizeof(dbg), "CLONE pass 2: member '%s' -> %s",
					 nm ? nm : "?", clone ? "cloning its wires" : "NO CLONE in map (skipped)");
			DebugPrint(dbg, __FILE__, __LINE__, CLONE);
			if (clone)
				CloneConnections(inst, clone, map);
		}

		/* a nested view's members live under the nested view's own path */
		if (strcmp(classname, "View") == 0 && nm && nm[0])
		{
			snprintf(childSrc, sizeof(childSrc), "%s/%s", srcPath, nm);
			snprintf(childClone, sizeof(childClone), "%s/%s", clonePath, nm);
			CloneGroupPass(childSrc, childClone, map, pass);
		}
	}

	DelNode(list);
}

/* Clone a THING - any instance - into containerPath. There is no separate
   call for a view: you clone the thing, and if the thing contains members
   (a view, or a view inside a view) this copies them too, aliases
   re-pointed and wires re-made, recursing to any depth. A leaf control
   just copies its node and finds no members. One entry, whatever it is. */
NodeObj CloneInstance(NodeObj source, char *containerPath, NodeObj map)
{
	NodeObj top;
	char name[200], srcPath[256], clonePath[256];
	char dbg[1024];

	if (!source || !map)
		return NULL;

	/* the engine names it (this is the core's job, not the caller's) and  */
	/* works out both paths itself: where the source's members point        */
	/* (srcPath) and where the clone's will (clonePath)                     */
	InstancePath(source, srcPath, sizeof(srcPath));
	CloneMintName(source, containerPath ? containerPath : "", name, sizeof(name));

	snprintf(dbg, sizeof(dbg), "CLONE start: source '%s' -> new name '%s' into container '%s' (source path '%s')",
			 GetPropStr(source, "Name"), name, containerPath ? containerPath : "(root)", srcPath);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	if (containerPath && containerPath[0])
		snprintf(clonePath, sizeof(clonePath), "%s/%s", containerPath, name);
	else
		snprintf(clonePath, sizeof(clonePath), "/Root/%s", name);

	/* made in its container the ordinary way - so it is named, registered,
	   and its class has built whatever it declares - then renamed to the
	   name minted for it (RegisterPath retires the one it arrived with) */
	top = CloneObject(source, ContainerOfPath(clonePath), name);
	if (!top)
		return NULL;
	SetConnState(map, (long) source, (long) top);

	/* everything inside it - members, then the links they carried, then wires */
	CloneGroupPass(srcPath, clonePath, map, 0);
	CloneGroupPass(srcPath, clonePath, map, 1);
	CloneRelinkProps(source, top, map);	/* the container's own links, same step */
	CloneGroupPass(srcPath, clonePath, map, 2);

	/* and the CONTAINER'S OWN wires. Pass 2 walks members only, so every wire
	   whose source is the group's root was silently dropped - which is every
	   wire a widget's panel makes from the widget's property to its control
	   (Output -> the Output box's Value, and its siblings). A clone came up
	   with its controls disconnected from the thing they display: the value
	   was there, nothing carried it. Done after the passes so every member is
	   in the map; CloneConnections ignores sinks that are not (a client's own
	   subscribe tap, anything outside the group), which is what keeps the
	   copy's traffic out of the original's name. */
	CloneConnections(source, top, map);

	snprintf(dbg, sizeof(dbg), "CLONE done: '%s' cloned into '%s'", GetPropStr(source, "Name"), clonePath);
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);

	return top;
}

int
Connect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	NodeObj fromPort, toPort, fromOwner, toOwner;
	long handler;

	if (!fromNode || !toNode)
		return 0;

	/* An unnamed end connects to Value - the one thing a control speaks
	   through. ReservedOut/ReservedIn are the OVERRIDE: an object that
	   answers somewhere else (TCPPort's TxData/RxData, a Filter's In/Out,
	   a View naming an inner property) says so, and most things do not
	   carry them at all. Resolved here so every caller - the GUI dot, a
	   script, a REST call - gets the same answer without knowing the rule. */
	if (!from || !from[0])
		from = GetPropStr(fromNode, "ReservedOut");
	if (!from || !from[0])
		from = "Value";
	if (!to || !to[0])
		to = GetPropStr(toNode, "ReservedIn");
	if (!to || !to[0])
		to = "Value";

	/* both ends resolve through links: wiring to or from an alias IS   */
	/* wiring to or from the original - the Subscriber entry lands on    */
	/* the original's port and carries the original instance, so the     */
	/* alias adds zero cost (and zero code) to every later message       */
	fromOwner = fromNode;
	fromPort = ResolvePort(&fromOwner, from);
	if (!fromPort) {
		/* ResolvePort says NULL for two different things: the property is
		   absent, or it is present but its link dangles. Only the first is
		   safe to invent - writing 0 over the second destroys both the link
		   and whatever value it was carrying. */
		if (GetPropNode(fromNode, from))
		{
			char dbg[400], fpath[300];
			snprintf(dbg, sizeof(dbg), "Connect: '%s' has property '%s' but it does not "
					 "resolve (dangling link) - refusing to overwrite it",
					 PathOfInstance(fromNode, fpath, sizeof(fpath)) ? fpath
						: (GetPropStr(fromNode, "Name") ? GetPropStr(fromNode, "Name") : "(unnamed)"),
					 from);
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
			return 0;
		}
		/* genuinely absent - so it comes into being here, and that is the
		   MECHANISM, not a mistake: a property exists because something
		   referred to it. That is how an instance is annotated
		   (GUI_Format), how a default is overridden per instance, and how
		   a script adds state to an object the class never declared. A
		   class publishes what it ships with; an instance grows the rest.

		   It is logged because it is indistinguishable from a typo or a
		   renamed property, and THAT silence is what let six harness tests
		   subscribe to a property removed two commits earlier and report
		   nothing for weeks. Seeing it is the fix; refusing it would break
		   late binding. */
		{
			char dbg[400], fpath[300];

			snprintf(dbg, sizeof(dbg),
					 "Connect: '%s' grew property '%s' by being wired to it "
					 "(late binding - normal). Worth a look only if that name "
					 "was meant to already exist.",
					 PathOfInstance(fromNode, fpath, sizeof(fpath)) ? fpath
						: (GetPropStr(fromNode, "Name") ? GetPropStr(fromNode, "Name") : "(unnamed)"),
					 from);
			DebugPrint(dbg, __FILE__, __LINE__, WIRE);
		}
		SetPropInt(fromNode, from, 0);
		fromPort = GetPropNode(fromNode, from);
	}

	/* the target property must already exist */
	toOwner = toNode;
	toPort = ResolvePort(&toOwner, to);
	if (!toPort) {
		char dbg[400], tpath[300];

		snprintf(dbg, sizeof(dbg), "Connect: '%s' has no property '%s'",
				 PathOfInstance(toNode, tpath, sizeof(tpath)) ? tpath
					: (GetPropStr(toNode, "Name") ? GetPropStr(toNode, "Name") : "(unnamed)"),
				 to);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return 0;
	}

	/* one record either way - a port with a compiled handler records it,  */
	/* a plain property records Callback 0 and delivery applies the        */
	/* universal default (store what arrived - DeliverToSubscriber,        */
	/* node.c). Every property is both a source and a sink: node.c's       */
	/* unconditional write fan-out is the source half, the default         */
	/* delivery is the sink half, and the Subscriber always names the      */
	/* REAL sink - no adapter standing in between for the graph walkers    */
	/* (list-connections, clone, scrub) to trip over. The recorded name    */
	/* is the RESOLVED node's own - wiring to an alias records what the    */
	/* alias stands for, same rule as SetOrDeliverProp.                    */
	handler = GetPropLong(toPort, "OnMsg");
	AddSubscription(fromPort, toOwner, GetNameStr(toPort), handler);

	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "Connect: '%s'.%s -> '%s'.%s (%s)",
				 GetPropStr(fromOwner, "Name"), from, GetPropStr(toOwner, "Name"),
				 GetNameStr(toPort), handler ? "handler" : "default delivery");
		DebugPrint(dbg, __FILE__, __LINE__, WIRE);
	}

	return 1;
}

/* the inverse of Connect() - remove exactly the one wire that matches,   */
/* by the same resolution rules (either end may be an alias). Returns 1   */
/* if a wire was removed, 0 if no such wire existed. Taps and handlers    */
/* are all the same record shape, so "which wire" is just {Instance,      */
/* Port} equality on the resolved sink.                                    */
int
Disconnect(NodeObj fromNode, char * from, NodeObj toNode, char * to){

	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	NodeObj fromPort, toPort, fromOwner, toOwner, sub;
	char * toName;

	if (!fromNode || !from || !toNode || !to)
		return 0;

	fromOwner = fromNode;
	fromPort = ResolvePort(&fromOwner, from);
	if (!fromPort)
		return 0;

	toOwner = toNode;
	toPort = ResolvePort(&toOwner, to);
	if (!toPort)
		return 0;
	toName = GetNameStr(toPort);

	for (sub = GetNextProp(fromPort); sub; sub = GetNextSibling(sub))
	{
		if (!CmpName(sub, "Subscriber"))
			continue;
		if ((NodeObj) GetPropLong(sub, "Instance") != toOwner)
			continue;
		/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
		if (!GetPropStr(sub, "Port") || !toName
			|| strcmp(GetPropStr(sub, "Port"), toName) != 0)
			continue;

		RemoveProp(fromPort, sub);
		DelNode(sub);

		{
			char dbg[256];
			snprintf(dbg, sizeof(dbg), "Disconnect: '%s'.%s -/-> '%s'.%s",
					 GetPropStr(fromOwner, "Name"), from, GetPropStr(toOwner, "Name"), toName);
			DebugPrint(dbg, __FILE__, __LINE__, WIRE);
		}

		return 1;
	}

	return 0;
}

/* one of these rides on a queued dispatch task between SndMsg (which   */
/* builds it) and DispatchMsg (which reads it back out when the task    */
/* comes due) - see the comment on SndMsg for why sends are queued.     */
/* Not to be confused with DeliverMsg below, which is a separate,       */
/* synchronous, single-target mechanism (bypasses the subscriber list   */
/* entirely) used by Router and SetOrDeliverProp.                       */
typedef struct {
	NodeObj instance;	/* the source instance that owns outPort - see  */
				/* DeleteInstance's CancelPendingSends, which    */
				/* matches on this to catch a queued send whose  */
				/* source is deleted before delivery fires        */
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	NodeObj outPort;
	MsgId   message;
	NodeObj data;
	TaskObj task;
} MsgEnvelope;

/* allocation accounting - see the twin counter in node.c for the idea.  */
/* An envelope lives only between SndMsg and its DispatchMsg firing, so   */
/* at rest this reads 0; a climb means queued messages are being lost     */
/* (dropped on the ground) instead of delivered-and-freed.                 */
static long envelopesAlive = 0;

long EnvelopeCount(void)
{
	return envelopesAlive;
}

/* scheduler callback: actually walk the subscriber list and deliver.   */
/* Runs from ExecTasks, so every hop is a flat call from the scheduler, */
/* never nested inside the sender's own call stack the way a direct    */
/* SndMsg call used to be                                               */
static int
DispatchMsg(NodeObj envArg, NodeObj unused, int reason){

	MsgEnvelope * env = (MsgEnvelope *) envArg;
	NodeObj sub;

	(void) unused;

	/* outPort is NULL if CancelPendingSends neutralized this envelope   */
	/* because its source instance was deleted before this fired - the   */
	/* port (and everything else the deleted instance owned) is already  */
	/* gone, so there is nothing left to walk, just the payload to free  */
	if (reason == task_callback && env->outPort) {
		sub = GetNextProp(env->outPort);
		while (sub) {
			if (CmpName(sub, "Subscriber"))
				/* one shared definition of delivery (node.c): a       */
				/* recorded handler is called; a plain-property wire   */
				/* gets the universal default - store what arrived     */
				DeliverToSubscriber(sub, env->message, env->data, env->outPort);
			sub = GetNextSibling(sub);
		}
	}

	if (reason == task_callback)
		/* fired normally through ExecTasks, which does not free this  */
		/* task itself - park it for GetTask to hand back out          */
		RemoveTask(env->task);
	/* else task_deactivate: the list was torn down with this message  */
	/* still queued - DeleteTask frees the task_entry itself right     */
	/* after this returns, so it must not go back on the reuse pool    */

	if (env->data)
		DelNode(env->data);

	envelopesAlive--;
	free(env);

	return rtrn_handled;
}

/* called by DeleteInstance before it frees deadInstance: a message      */
/* deadInstance already sent may still be queued (SndMsg only costs an   */
/* AddTaskNow, delivery happens later), and that queued envelope holds a */
/* raw pointer to one of deadInstance's own ports - about to be freed    */
/* along with the rest of it. Walk every pending DispatchMsg task (both  */
/* the main list and the in-flight runnow bucket - a message can be      */
/* mid-delivery-batch, due this tick but not yet run, when a callback    */
/* earlier in the same batch deletes its source) and blank out outPort   */
/* on any envelope whose source is deadInstance, so DispatchMsg finds    */
/* nothing to walk instead of dereferencing freed memory.                */
static void CancelPendingSends(NodeObj deadInstance)
{
	TaskList tasks = ObjGetTaskList();
	TaskObj task;
	MsgEnvelope * env;

	for (task = GetTaskListHead(tasks); task; task = GetTaskNext(task)) {
		if (GetTaskCallback(task) != (FuncPtr)DispatchMsg)
			continue;
		env = (MsgEnvelope *) GetTaskData(task);
		if (env->instance == deadInstance)
			env->outPort = NULL;
	}

	for (task = GetTaskListHead(GetTaskListRunnow(tasks)); task; task = GetTaskNext(task)) {
		if (GetTaskCallback(task) != (FuncPtr)DispatchMsg)
			continue;
		env = (MsgEnvelope *) GetTaskData(task);
		if (env->instance == deadInstance)
			env->outPort = NULL;
	}
}

/* route one message out a port to every subscriber of that port.       */
/*                                                                       */
/* Delivery is queued through the scheduler rather than called          */
/* synchronously: SndMsg only ever costs one AddTaskNow, and actual     */
/* delivery (walking the subscriber list, calling each handler) happens */
/* later from ExecTasks.  This keeps causality flat - a message and     */
/* everything it causes downstream can never nest inside the call stack */
/* of the object that sent it, so a dense subscriber web (or a filter/  */
/* queue chain that re-sends what it receives) can't starve the         */
/* scheduler by monopolizing the stack; every hop gets its own turn     */
/* through ExecTasks, breadth-first, in the order it was caused.        */
/*                                                                       */
/* SndMsg takes ownership of `data`: DispatchMsg frees it once every     */
/* subscriber has had it, so callers must drop the DelNode they used to  */
/* call right after SndMsg under the old synchronous contract - and      */
/* anything that forwards a message it received (rather than a freshly   */
/* built one) must build its own copy to send onward, since the original */
/* is owned by the sender's own queued delivery, not by whoever received */
/* it (see Filter_OnIn for the pattern).                                 */
/*                                                                       */
/* Two entry points, one body. SndMsg takes a property NAME and resolves */
/* it; SndMsgNode takes the resolved node, which is what node.c's        */
/* property fan-out already holds - and is what lets a property write    */
/* queue a message instead of walking the subscriber list itself.        */
int
SndMsgNode(NodeObj instance, NodeObj outPort, MsgId message, NodeObj data){

	MsgEnvelope * env;
	TaskObj task;

	if (!outPort) {
		if (data)
			DelNode(data);
		return 0;
	}

	env = malloc(sizeof(MsgEnvelope));
	envelopesAlive++;
	env->instance = instance;
	env->outPort = outPort;
	env->message = message;
	env->data    = data;

	task = GetTask(ObjGetTaskList());
	env->task = task;

	/* env rides through the generic NodeObj slot - not a real node,   */
	/* DispatchMsg is the only thing that ever reads it back out       */
	AddTaskNow(task, (FuncPtr)DispatchMsg, message, (NodeObj)env);

	return 1;
}

int
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
SndMsg(NodeObj instance, char * port, MsgId message, NodeObj data){

	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	NodeObj outPort, owner;

	/* sending out an alias port sends out the original - the envelope   */
	/* records the original as its source so CancelPendingSends matches   */
	/* the instance whose port the envelope actually holds                 */
	owner = instance;
	outPort = ResolvePort(&owner, port);

	return SndMsgNode(owner, outPort, message, data);
}

/*
 * Deliver straight to one named port's own handler, bypassing whatever
 * Subscriber list is (or isn't) attached to it - for an object like
 * Router that decides AT DELIVERY TIME which single target gets a given
 * message, rather than a fixed Connect()'d wire. SndMsg fans out to
 * everyone subscribed to a port; this reaches exactly one target's port
 * directly, the same way SndMsg reaches each subscriber once it's found.
 */
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
int DeliverMsg(NodeObj target, char *port, MsgId message, NodeObj data){

	NodeObj portNode;
	msgobj handler;

	if (!target || !port)
		return 0;

	portNode = GetPropNode(target, port);
	if (!portNode)
		return 0;

	handler = (msgobj) GetPropLong(portNode, "OnMsg");
	if (!handler)
		return 0;

	handler(target, message, data);
	return 1;
}

/*
 * A property name can resolve to two different things on a target: a
 * plain data property (a direct write is correct - SetProp* fans out to
 * subscribers on its own), or a port carrying an OnMsg
 * handler (Enable, In, ...). A port only actually does anything when a
 * message is delivered to it the way genuine Connect()'d traffic would
 * arrive - writing its raw value with SetPropStr silently changes the
 * text and calls no handler, so Enable, say, would never flip the
 * instance data the object itself is actually gated on. Every place
 * that writes an instance property by name from outside the object
 * (the property-binding adapter below, Bridge's set-property command)
 * needs this distinction, not just SetPropStr.
 */
void SetOrDeliverProp(NodeObj target, char *propname, char *value)
{
	NodeObj propnode, chunk, owner;

	if (!target || !propname || !value)
		return;

	/* writing through an alias writes the original - and the original's */
	/* own fan-out (SetPropStr) or compiled handler (DeliverMsg with the  */
	/* owning instance, so it finds its own "local") does the rest.       */
	/* The write goes by the RESOLVED node's own name, not the name the   */
	/* caller used: an Alias keeps its link in a "Value" slot that may    */
	/* stand for the target's Name, Enable, anything - writing "Value"    */
	/* on the owner would hit the wrong property entirely.                 */
	{
		char *asked = propname;
		char  dbg[512];

		owner = target;
		propnode = ResolvePort(&owner, propname);
		if (propnode)
			propname = GetNameStr(propnode);

		snprintf(dbg, sizeof(dbg),
				 "SET '%s'.%s = '%.60s' -> %s '%s'.%s%s",
				 GetPropStr(target, "Name") ? GetPropStr(target, "Name") : "?",
				 asked ? asked : "?",
				 value ? value : "(null)",
				 (propnode && GetPropLong(propnode, "OnMsg")) ? "DELIVER" : "STORE",
				 GetPropStr(owner, "Name") ? GetPropStr(owner, "Name") : "?",
				 propname,
				 (owner != target) ? "  [crossed a link]" : "");
		DebugPrint(dbg, __FILE__, __LINE__, WIRE);
	}
	if (propnode && GetPropLong(propnode, "OnMsg"))
	{
		/* the handler answers whether the property still needs writing -
		   the same rule, and the same three codes, as DeliverToSubscriber
		   (node.c). The handler is called here rather than through
		   DeliverMsg because DeliverMsg reports whether a handler existed,
		   not what it decided. */
		msgobj handler = (msgobj) GetPropLong(propnode, "OnMsg");
		int    verdict;

		chunk = NewNode(STRING);
		SetName(chunk, propname);
		SetValueStr(chunk, value);
		verdict = handler(owner, msg_send, chunk);
		DelNode(chunk);

		/* the missing half of the SET line above: DELIVER says a handler ran,
		   this says what it decided, and therefore whether the value is now
		   on the property at all */
		{
			char dbg[400];

			snprintf(dbg, sizeof(dbg),
					 "  '%s'.%s handler said %s%s",
					 GetPropStr(owner, "Name") ? GetPropStr(owner, "Name") : "?",
					 propname,
					 verdict == rtrn_handled   ? "HANDLED (it stored it itself)" :
					 verdict == rtrn_propagate ? "PROPAGATE" :
					 verdict == rtrn_dropped   ? "DROPPED (refused it)" : "an unknown code",
					 verdict == rtrn_propagate ? " - storing it now" : " - NOT storing");
			DebugPrint(dbg, __FILE__, __LINE__, WIRE);
		}

		/* refused by the instance: offer it up the class chain before
		   giving up on it (PuntToClass) */
		if (verdict == rtrn_dropped)
		{
			chunk = NewNode(STRING);
			SetName(chunk, propname);
			SetValueStr(chunk, value);
			verdict = PuntToClass(owner, msg_send, chunk);
			DelNode(chunk);
		}

		if (verdict == rtrn_propagate)
			SetPropStr(owner, propname, value);
		return;
	}

	/* no handler on the instance is also "not mine" - the class chain gets
	   its turn before the universal default (see DeliverToSubscriber) */
	{
		int verdict;

		chunk = NewNode(STRING);
		SetName(chunk, propname);
		SetValueStr(chunk, value);
		verdict = PuntToClass(propnode ? owner : target, msg_send, chunk);
		DelNode(chunk);

		if (verdict == rtrn_handled)
			return;
	}

	SetPropStr(propnode ? owner : target, propname, value);
}

/* see the comment in object.h - the one shared placement call behind    */
/* create, clone, and move, whatever translator asked                     */
void PlaceInstance(NodeObj inst, char *container, char *x, char *y)
{
	char dbg[1024];

	if (!inst)
		return;

	SetOrDeliverProp(inst, "Container", (container && container[0]) ? container : "");
	if (x && x[0])
		SetOrDeliverProp(inst, "X", x);
	if (y && y[0])
		SetOrDeliverProp(inst, "Y", y);

	snprintf(dbg, sizeof(dbg), "PLACE: '%s' in '%s' at X=%s Y=%s (PanelX=%s PanelY=%s)",
			 GetPropStr(inst, "Name"), GetPropStr(inst, "Container"),
			 GetPropStr(inst, "X"), GetPropStr(inst, "Y"),
			 GetPropStr(inst, "ReservedViewPanelX"), GetPropStr(inst, "ReservedViewPanelY"));
	DebugPrint(dbg, __FILE__, __LINE__, CLONE);
}

/* Would placing the thing whose session path is instPath into container  */
/* put it inside itself? Session paths ARE the containment chain (a       */
/* member's path is its container's path plus its basename - see the      */
/* Bridge's rename machinery), so ancestor-or-self is exactly a prefix    */
/* test. "" is the top-level canvas and can never be inside anything.      */
int ContainmentCycle(char *instPath, char *container)
{
	int len;

	if (!instPath || !instPath[0] || !container || !container[0])
		return 0;

	len = (int) strlen(instPath);
	if (strncmp(container, instPath, len) != 0)
		return 0;

	return container[len] == 0 || container[len] == '/';
}

/* see the comment in object.h - the one-verb move every translator      */
/* shares: validate, then place. Returns 1 moved, 0 refused.              */
int MoveInstance(NodeObj inst, char *instPath, char *container, char *x, char *y)
{
	if (!inst)
		return 0;

	if (ContainmentCycle(instPath, container))
		return 0;

	PlaceInstance(inst, container, x, y);
	return 1;
}

/* see the comment above these in object.h */
long GetConnState(NodeObj table, long connId)
{
	char key[32];

	if (!table)
		return 0;

	snprintf(key, sizeof(key), "%ld", connId);
	return GetPropLong(table, key);
}

void SetConnState(NodeObj table, long connId, long value)
{
	char key[32];

	if (!table)
		return;

	snprintf(key, sizeof(key), "%ld", connId);
	SetPropLong(table, key, value);
}

/*
 * The generic handler behind every instance's Activate port. Modules
 * store their activation function as an "Activate" long property
 * (ActivateInstance calls it); RegisterInstance stamps this handler as
 * OnMsg on that same property node, which makes Activate an ordinary
 * port: Connect(button, "Out", anything, "Activate") is one plain,
 * listable, clonable, scrubbable wire - no separate bind-activate
 * species. It also protects the stored function pointer: without a
 * handler here, a wire into Activate would fall to the default
 * store-what-arrived delivery and overwrite the pointer with a string.
 * msg_eof is ignored, same convention as every Enable handler - an
 * upstream ending is not a click.
 */
int ActivateOnMsg(NodeObj instance, MsgId message, NodeObj data)
{
	/* A PRESS, not a release. A Button writes its Value "1" then "0" so that
	   every press is a real change and gets through (button.c); with the value
	   ignored here, both edges activated and one click ran the target twice.
	   "0" is the release, and an empty value is nothing happening. */
	{
		char *v = data ? GetValueStr(data) : NULL;

		if (message == msg_eof || !v || !v[0] || strcmp(v, "0") == 0)
			return rtrn_handled;
	}

	{
		char dbg[200];

		snprintf(dbg, sizeof(dbg), "ActivateOnMsg: '%s' activated by value '%s'",
				 GetPropStr(instance, "Name"), data ? GetValueStr(data) : "(none)");
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}

	ActivateInstance(instance);
	return rtrn_handled;
}


/*
 * Retired: SetPropInt/SetPropStr/SetPropLong (node.c) now fan out to a
 * property's Subscriber children unconditionally, on every write, with
 * no opt-in step - a property is watchable simply by existing, exactly
 * like a port already was. WatchableProp used to be how a property got
 * that behavior (installing PropertyChanged as an Intercept); it is kept
 * only so the many existing call sites across the object tree keep
 * compiling. It does nothing now, on purpose - deleting the calls
 * themselves is cleanup, not a fix.
 */
void WatchableProp(NodeObj instance, char *propname)
{
	(void) instance;
	(void) propname;
}


/* call the Activate function pointer an instance carries on itself */
int
ActivateInstance(NodeObj instance){

	msgobj Activate;

	if (!instance)
		return rtrn_dropped;

	Activate = (msgobj)GetPropLong(instance, "Activate");
	if (!Activate) {
		DebugPrint ( "ActivateInstance found no Activate function on the instance.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	return Activate(instance, msg_initialize, NULL);
}


// Handle registration of objects, classes, and instances,
static void
PrintRegInfo(char* message, NodeObj obj){
	/* TEMPORARILY SILENCED 2026-08-12: one line per instance, and a boot
	   builds hundreds, which made the log useless for anything else.
	   Commented out, NOT removed: restore it when tracing registration. */
	char buffer[255];
	sprintf((char *)&buffer, message, GetNameStr(obj));
	(void) buffer;
	/* DebugPrint ((char *)&buffer, __FILE__, __LINE__, REGISTER); */
}

NodeObj RegisterLibrary(NodeObj library){
	PrintRegInfo("Registering object '%s'", library);
	AddChild(RegObjList, library);
	/* the loader stamps File on this from the path it dlopen'd, which the
	   module's own _init() never sees - same hand-back as LastInstance on a
	   class. LoadObject clears it first, so an unset value means the module
	   registered nothing. */
	SetPropLong(RegObjList, "LastLibrary", (long) library);
	return library;
}

/* THE END OF THE CHAIN, and it answers for everything.

   Storing a value is not a special case in the delivery code, it is the
   root class's behaviour - so it lives here, and a property write nobody
   claimed is HANDLED rather than unhandled. That is what keeps the log
   below meaningful: it only fires for a message that genuinely nothing
   wanted.

   The one thing it has to tell apart is "nobody had an opinion" from
   "somebody refused it", because a disabled control returning
   rtrn_dropped must not have its value stored anyway. It reads that off
   the property itself: an OnMsg means someone was asked and said no.

   Anything that reaches here and cannot be handled is logged and dropped.
   A message nothing wanted is not necessarily an error, but it must never
   be silent - "nothing happened and nobody said why" is the most
   expensive kind of bug there is. */
static int ObjectClassMsg(NodeObj instance, MsgId message, NodeObj data)
{
	char    dbg[400];
	char   *prop = (data && GetNameStr(data)) ? GetNameStr(data) : NULL;
	char   *val  = data ? GetValueStr(data) : NULL;
	NodeObj propnode;

	if (instance && prop && message != msg_eof)
	{
		propnode = GetPropNode(instance, prop);

		/* no handler anywhere below: nobody had an opinion, so the default
		   applies and that IS handling it */
		if (!propnode || !GetPropLong(propnode, "OnMsg"))
		{
			SetPropStr(instance, prop, val ? val : "");
			return rtrn_handled;
		}
	}

	/* WIRE, so the default log reads exactly as it did before this chain
	   existed. A refusal is already reported where it happened ("handler
	   said DROPPED"); saying it again at the top would be the same event
	   twice, and a disabled control refuses constantly. This line is the
	   end of the trace for anyone following one message, not news. */
	snprintf(dbg, sizeof(dbg),
			 "UNHANDLED at Object: '%s'.%s = '%.60s' (msg %d) - dropped",
			 (instance && GetPropStr(instance, "Name")) ? GetPropStr(instance, "Name") : "?",
			 prop ? prop : "?", val ? val : "", (int) message);
	DebugPrint(dbg, __FILE__, __LINE__, WIRE);

	return rtrn_dropped;
}

/* The one class the core itself provides: Object, the end of the chain and
   the ABI anchor - every module declares Object against CORE_LIBRARY_FILE, so
   its version is what keeps a module built for an older core out of this one.
   Everything else is a loadable class of its own: Control (the presentation
   layer - a name, a place, a size, being serialized) and Widget (a Control
   that contains other Controls). They are modules, not core, so fixing what
   is common to every control ships one file. */
static NodeObj CoreClass(NodeObj library, char *name, char *parent)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, name);
	SetClassVersion(class, CORECLASS_MAJOR, CORECLASS_MINOR);
	RegisterClass(library, class);
	if (parent)
		SetClassParent(class, parent);
	return class;
}

void RegisterCoreClasses(void)
{
	NodeObj library = NewNode(INTEGER);

	SetName(library, "Framework");
	SetPropStr(library, "Company", "GrokThink");
	SetPropStr(library, "File", CORE_LIBRARY_FILE);
	SetPropInt(library, "State", 1);
	RegisterLibrary(library);
	/* nothing to bring up later: these are not dlopen'd, so they are Loaded
	   the moment they exist and loadClasses never counts them as pending */
	SetPropInt(library, "Loaded", 1);

	/* the root class answers last, and answers for everything */
	SetPropLong(CoreClass(library, "Object", NULL), "ClassMsg", (long) ObjectClassMsg);
}

void UnregisterLibrary(NodeObj library){
	PrintRegInfo("Unregistering object '%s'", library);
	SetPropInt(library, "State", 0); 	//Mark this node as gone.

	/* the full node dump is only wanted at high verbose levels */
	if (DebugPrintGetLevel() >= 3)
		PrintNode(library);

	//DelNode(node);  // I stopped removing the node to see it dump out on exit
}

NodeObj RegisterClass(NodeObj library, NodeObj class){
	NodeObj existing;

	/* a class name is the whole address space for creating one - two of them
	   means CreateObject, the palette and every translator silently pick
	   whichever the walk hits first. It happens when the same module is
	   loaded from two paths (dlopen keys on the pathname, not the inode),
	   so say so loudly and keep the one already registered. */
	existing = FindClass(GetNameStr(class));
	if (existing)
	{
		char dup[200];
		snprintf(dup, sizeof(dup),
		         "class '%s' is already registered - refusing the second one",
		         GetNameStr(class));
		DebugPrint(dup, __FILE__, __LINE__, ERROR);
		/* NULL, not the incumbent: a module does
		     ClassSelf = RegisterClass(...); SetClassVersion(ClassSelf, ...)
		   so handing back the class already registered would have it stamp
		   its version and parent onto a class it does not own. Registering
		   takes ownership of the node either way, so the refused one is
		   freed here rather than leaked in fifty modules. */
		DelNode(class);
		return NULL;
	}

	PrintRegInfo("Registering class '%s'", class);

	/* A CLASS IS A NODE IN THE TYPE TREE, and this is what tells a walk
	   which kind of child it is looking at: a class node's children are
	   its subclasses AND its instances, and only the class says so. */
	SetPropInt(class, "IsClass", 1);

	/* which .object provided it. A class comes from exactly one file, so
	   that relation is single-valued and belongs in a property - the child
	   list is needed for the walkable relation, which is subtyping. */
	SetPropLong(class, "Library", (long) library);

	NSInsert(GetClassIndex(), GetNameStr(class), (long) class);

	/* it starts under its library and moves under its parent class the
	   moment the module declares one (SetClassParent). A class that never
	   declares a parent - Object - stays here, which is what roots the
	   type tree. */
	AddChild(library, class);
	//PrintNode(library);
	//msgobj InstanceStart = (msgobj)GetPropLong(class, "InstanceStart");
	//if (InstanceStart) InstanceStart(class, 1, NULL);
	return class;
}

void UnRegisterClass(NodeObj library, NodeObj class){
	if (class && GetNameStr(class))
		NSDelete(GetClassIndex(), GetNameStr(class));
    PrintRegInfo("Unregistering class '%s'", library);
	//DelNode(node);
}

/* the interface lives as a property on the class, not a child - a       */
/* class's children are its subclasses (SetClassParent) and its          */
/* instances (RegisterInstance), and both are walked; mixing interface   */
/* entries into that list would break anything that walks it             */
NodeObj GetClassInterface(NodeObj class){

	if (!class)
		return NULL;

	return GetPropNode(class, "Interface");
}

/* see the comment in object.h */
NodeObj InterfacePropForInstance(NodeObj inst, char *propname)
{
	NodeObj interface, prop;
	char *name;

	if (!inst || !propname)
		return NULL;

	interface = GetClassInterface(GetParent(inst));
	for (prop = interface ? GetChild(interface) : NULL; prop; prop = GetNextSibling(prop))
	{
		name = GetPropStr(prop, "Name");
		if (name && strcmp(name, propname) == 0)
			return prop;
	}

	return NULL;
}

NodeObj PublishProp(NodeObj class, char *name, int widget, char *defaultValue){

	NodeObj interface, entry;

	if (!class || !name)
		return NULL;

	interface = GetClassInterface(class);
	if (!interface) {
		interface = NewNode(INTEGER);
		SetName(interface, "Interface");
		AddProp(class, interface);
	}

	entry = NewNode(INTEGER);
	SetName(entry, "Property");
	SetPropStr(entry, "Name", name);
	SetPropInt(entry, "Widget", widget);
	SetPropStr(entry, "Default", defaultValue ? defaultValue : "");

	AppendChild(interface, entry);

	return entry;
}

NodeObj RegisterInstance(NodeObj class, NodeObj Instance){
	NodeObj activate;

	PrintRegInfo("Registering instance of '%s'", Instance);

	AddChild(class, Instance);

	/* Activate is an ordinary port on every instance that has an          */
	/* activation function: modules store the pointer as an "Activate"     */
	/* property before registering (every InstanceStart ends here), and    */
	/* the generic handler makes it wireable - see ActivateOnMsg. Stamped  */
	/* on the property NODE (never SetProp* by the port's name - the       */
	/* shadowing landmine).                                                 */
	activate = GetPropNode(Instance, "Activate");
	if (activate && !GetPropLong(activate, "OnMsg"))
		SetPropLong(activate, "OnMsg", (long) ActivateOnMsg);

	/* leave the newest instance where CreateObject can find it */
	SetPropLong(class, "LastInstance", (long)Instance);

	return Instance;
}

void
UnRegisterInstance(NodeObj class, NodeObj Instance){
	(void) Instance;
    PrintRegInfo("Unregistering instance of '%s'", class);
	//DelNode(node);
}

/* recursively remove any "Subscriber" property under `node` (properties   */
/* can themselves carry sub-properties, e.g. widget metadata) that targets */
/* deadInstance - see DeleteInstance for why                               */
static void ScrubSubscriberProps(NodeObj node, NodeObj deadInstance)
{
	NodeObj prop, next;

	if (!node)
		return;

	prop = GetNextProp(node);
	while (prop) {
		next = GetNextSibling(prop);

		if (CmpName(prop, "Subscriber") && (NodeObj)GetPropLong(prop, "Instance") == deadInstance) {
			{
				char dbg[256];
				snprintf(dbg, sizeof(dbg), "Scrub: removed a wire into dying '%s' (from port '%s')",
						 GetPropStr(deadInstance, "Name"), GetNameStr(node));
				DebugPrint(dbg, __FILE__, __LINE__, WIRE);
			}
			RemoveProp(node, prop);
			DelNode(prop);
		} else {
			ScrubSubscriberProps(prop, deadInstance);
		}

		prop = next;
	}
}

/* walk every live instance (library -> class -> instance, the same shape */
/* FindClass walks) scrubbing any Subscriber entry that targets           */
/* deadInstance - see DeleteInstance                                       */
static void ScrubRegistrySubscriptions(NodeObj deadInstance)
{
	NodeObj instance;

	for (instance = FirstInstance(); instance; instance = NextInstance(instance))
		ScrubSubscriberProps(instance, deadInstance);
}

/* blank every link (aliased property) under `node`'s props that points   */
/* at deadInstance - the alias survives as a dead control instead of a    */
/* dangling pointer, same policy as scrubbed subscriptions                 */
static void ScrubLinkProps(NodeObj node, NodeObj deadInstance)
{
	NodeObj prop;

	if (!node)
		return;

	for (prop = GetNextProp(node); prop; prop = GetNextSibling(prop))
	{
		if (GetNodeLink(prop) && (NodeObj) GetPropLong(prop, "LinkInst") == deadInstance)
		{
			LinkNode(prop, NULL);
			SetPropLong(prop, "LinkInst", 0);
		}
		ScrubLinkProps(prop, deadInstance);
	}
}

/* walk every live instance (same shape as ScrubRegistrySubscriptions)   */
/* neutralizing links aimed at deadInstance - see DeleteInstance          */
static void ScrubRegistryLinks(NodeObj deadInstance)
{
	NodeObj instance;

	for (instance = FirstInstance(); instance; instance = NextInstance(instance))
		ScrubLinkProps(instance, deadInstance);
}

/* the actual, working removal UnRegisterInstance's own stub never did -  */
/* DelSibling first (unlinks Instance from its class's child chain without */
/* touching the instances after it), then DelNode (frees just this one,   */
/* its own properties and children, now that it's isolated).              */
/* InstanceEnd runs first - every object registers one (RegisterClass),   */
/* but until now nothing ever called it back, so local structs and their  */
/* scheduled tasks outlived the node that owned them: DelNode would free  */
/* the instance out from under a still-armed task, and the next time it   */
/* fired it would hand the callback a dangling NodeObj as its data        */
/*                                                                         */
/* ScrubRegistrySubscriptions and CancelPendingSends close two related    */
/* holes from the same root cause: messages are queued (SndMsg/           */
/* DispatchMsg in this file), not delivered synchronously, so a message   */
/* can still be in flight - already queued but not yet delivered - when   */
/* either end of it is deleted out from under it. DispatchMsg re-reads    */
/* its outPort's live Subscriber list at delivery time rather than a      */
/* frozen snapshot, so stripping every Subscriber entry that points at    */
/* this instance is enough to make an already-queued delivery TO it find  */
/* nothing and safely skip it; CancelPendingSends handles the other       */
/* direction, a message this instance already sent whose envelope still   */
/* points at one of its own (about to be freed) ports. Both are confirmed */
/* against real use-after-frees ASan caught in exactly these scenarios    */
/* before this existed.                                                   */
void DeleteInstance(NodeObj instance)
{
	NodeObj class;
	msgobj instanceEnd;

	if (!instance)
		return;

	/* instances are registered as children of their class (RegisterInstance) */
	class = GetParent(instance);
	if (class) {
		instanceEnd = (msgobj)GetPropLong(class, "InstanceEnd");
		if (instanceEnd)
			instanceEnd(instance, msg_update, NULL);
	}

	/* UN-NAME IT FIRST. DeleteInstance never did, so the path trie kept
	   pointing at freed memory for anything deleted through the engine
	   rather than through the bridge - and any index built on those names
	   would inherit the same dangling entries. Captured before anything is
	   torn down, because PathOfInstance derives from Name and Container. */
	{
		char gone[300];

		if (PathOfInstance(instance, gone, sizeof(gone)))
			UnregisterPath(gone);
	}

	ScrubRegistrySubscriptions(instance);
	ScrubRegistryLinks(instance);
	CancelPendingSends(instance);

	DelSibling(instance);
	DelNode(instance);
}
