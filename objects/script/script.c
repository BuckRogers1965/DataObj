/*
 * Script - the class every scripting language host is one of.
 *
 * A host runs source in a fresh context on Activate, carries In/Out for
 * dataflow, Print for output and loud errors, and Cmd/Evt so a script is a
 * peer of the browser on the same protocol. It is a plain Object: nothing
 * puts it on a canvas - ScriptBox creates one and drives it.
 *
 * What belongs here is whatever every language does the same way, starting
 * with the runaway guard: a wall-clock budget the interpreter's interrupt
 * hook checks. QuickJS has one of its own and Lua has none, which is the
 * kind of gap a shared class closes for both at once.
 *
 * A stand-in for now - it registers the class and takes its place in the
 * chain. What this class does not answer falls through to Object.
 */

#include <stdio.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	/* nothing of its own yet: dropped, so the walk continues to Object */
	return rtrn_dropped;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Script");

	/* no InstanceStart: you instantiate a Lua or a JSScript, never a Script.
	   Which language hosts exist is now a question about this class's
	   children, so nothing needs a ScriptHost marker property. */

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Script");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "045bb6bc-d9e4-47f6-acf3-27c6553c3ff0");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)Handle_Message);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
