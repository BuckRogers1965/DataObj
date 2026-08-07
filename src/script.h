#ifndef SCRIPT_H
#define SCRIPT_H

/*
 * script.h - what a scripting language host is, from both sides.
 *
 * TWO AUDIENCES, one file:
 *
 *   A DRIVER (ScriptBox, MCPAgent, anything that owns a script) sees only the
 *   message ids below. It creates a host through that class's own
 *   InstanceStart, hands over {Owner, MsgBase, Port}, and from then on sends
 *   messages and catches answers. The host publishes NOTHING - no Source
 *   property, no In/Out ports, no name, no path. It cannot be found, wired,
 *   saved or reached into.
 *
 *   A HOST (Lua, JSScript) additionally sees the verb table and the helpers
 *   below. Its whole job is: make an interpreter context, walk the verb table
 *   registering each entry under its own name, marshal between its language's
 *   values and DataObj, and run source. Everything a script can DO is
 *   implemented once, in C, in script.object.
 *
 * WHY THE HOST IS OPAQUE: the source a user typed lives on the WIDGET (a
 * ScriptBox's Source property, an MCPAgent's), which is the thing that gets
 * serialized and cloned. The host is rebuilt from it on every start. That is
 * what lets a language swap keep the code, a clone come up working, and a
 * saved flow reload - none of which depend on the host being addressable.
 */

#include <stdio.h>

#include "node.h"
#include "data.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"

/* ---- what a DRIVER sends ------------------------------------------------ */
/* Order is the ABI: append only, and bump the class's Minor when you do. */
enum {
	SCRIPT_SET_SOURCE_MSG = USER_MESSAGE_BASE,	/* the code, as text        */
	SCRIPT_RUN_MSG,								/* fresh context, run it    */
	SCRIPT_IN_MSG,								/* dataflow in              */
	SCRIPT_STOP_MSG,							/* stop, drop the context   */
	SCRIPT_BUDGET_MSG							/* wall-clock ms, 0 = none  */
};

/* ---- what comes BACK, as base + ordinal on the owner's chosen port ------ */
enum {
	SCRIPT_PRINT = 0,		/* output, and errors, loudly */
	SCRIPT_OUT,				/* dataflow out */
	SCRIPT_ERROR			/* it died, or ran past its budget - why */
};

#define ScriptSetSource(pS, text) DeliverMsg((pS), "Msg", SCRIPT_SET_SOURCE_MSG, (text))
#define ScriptRun(pS)             DeliverMsg((pS), "Msg", SCRIPT_RUN_MSG, 0L)
#define ScriptIn(pS, data)        DeliverMsg((pS), "Msg", SCRIPT_IN_MSG, (data))
#define ScriptStop(pS)            DeliverMsg((pS), "Msg", SCRIPT_STOP_MSG, 0L)
#define ScriptBudget(pS, ms)      DeliverMsg((pS), "Msg", SCRIPT_BUDGET_MSG, (ms))

/* ---- what a HOST binds -------------------------------------------------- */

/* One verb. A host never names verbs individually - it walks the table and
   registers every entry through one trampoline, so a verb added here appears
   in every language the day it is written and no host is touched.

   Arguments and the result are DataObjs, so a script that hands over a number
   where a string is wanted simply works: conversion is the data object's job,
   not the binding's, and not the script author's.

   takesCallback: the verb's LAST argument is a function in the script rather
   than a value - connect() is the one that matters. The host passes an opaque
   handle it can later turn back into a callable; script.object stores it with
   the subscription and hands it back when the message arrives. */
/* The set is deliberately not final - see the bottom of the table in
   script.object for what is left out and why. Adding one is a line there and
   every language gets it; what needs deciding first is what a verb that
   returns a COLLECTION hands back, since DataObj is a scalar. */
typedef struct ScriptVerb
{
	char    *name;
	int      argc;				/* DataObj arguments, before any callback */
	int      takesCallback;
	DataObj (*fn)(NodeObj self, DataObj *argv, long cbHandle);
} ScriptVerb;

/* A host calls ScriptAttach in its InstanceStart with whatever its creator
   handed over, and ScriptDetach in InstanceEnd. Everything else below finds
   that state from `self`.

   Note what "sibling" means for sibget/sibset: a private host has no path of
   its own, so siblings are resolved against the OWNER - a ScriptBox's script
   reaches the ScriptBox's own panel controls, an MCPAgent's reaches the
   agent view's. That is both the only thing that can work and the thing you
   actually want. */
#ifdef SCRIPT_IMPL
void        ScriptAttach(NodeObj self, NodeObj owner, MsgId msgBase, char *port);
void        ScriptDetach(NodeObj self);
ScriptVerb *ScriptVerbs(void);
void        ScriptSetInvoke(NodeObj self, void (*invoke)(NodeObj, long, DataObj));
int         ScriptOverBudget(NodeObj self);
void        ScriptStartRun(NodeObj self);
void        ScriptSetBudget(NodeObj self, long ms);
void        ScriptReport(NodeObj self, int ordinal, char *text);
#else
static inline long ScriptEntry(char *name)
{
	static NodeObj cls;
	char msg[140];

	if (!cls)
		cls = FindClass("Script");
	if (!cls) {
		snprintf(msg, sizeof(msg),
				 "script.h: the Script class is not loaded - '%s' unreachable", name);
		DebugPrint(msg, __FILE__, __LINE__, ERROR);
		return 0;
	}
	return GetPropLong(cls, name);
}

static inline void ScriptAttach(NodeObj self, NodeObj owner, MsgId msgBase, char *port)
{
	void (*fn)(NodeObj, NodeObj, MsgId, char *) =
		(void (*)(NodeObj, NodeObj, MsgId, char *)) ScriptEntry("Attach");

	if (fn)
		fn(self, owner, msgBase, port);
}

static inline void ScriptDetach(NodeObj self)
{
	void (*fn)(NodeObj) = (void (*)(NodeObj)) ScriptEntry("Detach");

	if (fn)
		fn(self);
}

/* the interpreter is about to run: start the wall-clock budget */
static inline void ScriptStartRun(NodeObj self)
{
	void (*fn)(NodeObj) = (void (*)(NodeObj)) ScriptEntry("StartRun");

	if (fn)
		fn(self);
}


/* the NULL-terminated verb table - walk it once per context */
static inline ScriptVerb *ScriptVerbs(void)
{
	ScriptVerb *(*fn)(void) = (ScriptVerb *(*)(void)) ScriptEntry("Verbs");

	return fn ? fn() : NULL;
}

/* how script.object calls back INTO this language: it hands over the opaque
   handle the host gave it, plus the value, and the host makes the call */
static inline void ScriptSetInvoke(NodeObj self, void (*invoke)(NodeObj, long, DataObj))
{
	void (*fn)(NodeObj, void (*)(NodeObj, long, DataObj)) =
		(void (*)(NodeObj, void (*)(NodeObj, long, DataObj))) ScriptEntry("SetInvoke");

	if (fn)
		fn(self, invoke);
}

/* the interpreter's own interrupt hook calls this: has this run gone past the
   wall-clock budget it was given? One implementation, so a host cannot forget
   to have one (Lua had none). */
static inline int ScriptOverBudget(NodeObj self)
{
	int (*fn)(NodeObj) = (int (*)(NodeObj)) ScriptEntry("OverBudget");

	return fn ? fn(self) : 0;
}

/* the wall-clock a run is allowed, in ms; 0 means unlimited */
static inline void ScriptSetBudget(NodeObj self, long ms)
{
	void (*fn)(NodeObj, long) = (void (*)(NodeObj, long)) ScriptEntry("SetBudget");

	if (fn)
		fn(self, ms);
}

/* Print / Out / Error back to whoever owns this host */
static inline void ScriptReport(NodeObj self, int ordinal, char *text)
{
	void (*fn)(NodeObj, int, char *) =
		(void (*)(NodeObj, int, char *)) ScriptEntry("Report");

	if (fn)
		fn(self, ordinal, text);
}
#endif

#endif
