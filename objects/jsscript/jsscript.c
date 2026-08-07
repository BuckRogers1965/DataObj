#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "script.h"

#include "quickjs/quickjs.h"
#include "control.h"	/* PROP_* - what a published property presents as */

/*
JavaScript (QuickJS) host.

It is an OPAQUE object: no properties, no ports, no name, no path. Its whole
interface is script.h - a driver creates one through this class's own
InstanceStart, hands over {Owner, MsgBase, Port}, then sends it messages and
catches answers. Nothing can find it, wire it, save it or reach into it.

The source lives on the WIDGET that owns this host, which is the thing that
gets serialized and cloned; the host is rebuilt from it on every start.

What a script sees is NOT defined here. script.object owns the verb table and
implements every verb once, in C, over DataObj; this file walks that table and
registers each entry through one trampoline. Add a verb there and JavaScript
has it without this file changing - which is the point, because this host and
the Lua one had already drifted apart (this one had print and cmd and no
sibling access, the other had siblings and no print).

Two things are still spelled here, because they are this host's own lifecycle
rather than verbs: oninput(fn) and onevent(fn).

A runaway script cannot freeze the single-threaded fabric: QuickJS's interrupt
handler asks script.object's shared budget. That budget is real wall clock, not
the framework's cached time, precisely because a script that never yields also
never lets the main loop refresh the cache.

QuickJS 2024-01-13 is vendored in ./quickjs (MIT, bellard.org) so the module
stays a single self-contained .object.
*/

typedef struct InstanceData
{
	int        enabled;
	JSRuntime *rt;
	JSContext *ctx;
	JSValue    onin;	/* oninput callback, JS_UNDEFINED when unset */
	JSValue    onevt;	/* onevent callback                          */
	NodeObj    instance;
	char      *source;	/* our own copy - the owner holds the real one */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static InstanceData *JS_Local(JSContext *ctx)
{
	return (InstanceData *) JS_GetContextOpaque(ctx);
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

/* the runaway guard: the deadline is script.object's, so both languages get
   the same one and neither can forget to have it */
static int JS_InterruptCheck(JSRuntime *rt, void *opaque)
{
	InstanceData *local = (InstanceData *) opaque;

	(void) rt;
	return local && ScriptOverBudget(local->instance);
}

static void JS_ReportException(InstanceData *local)
{
	JSValue     e = JS_GetException(local->ctx);
	const char *s = JS_ToCString(local->ctx, e);

	ScriptReport(local->instance, SCRIPT_ERROR, (char *) (s ? s : "script error"));
	if (s)
		JS_FreeCString(local->ctx, s);
	JS_FreeValue(local->ctx, e);
}

/* ---- the one trampoline -------------------------------------------------
   Every verb in script.object's table is registered through this, with the
   table index as magic. Marshal JS values to DataObj, call, marshal back. */
static JSValue JS_VerbCall(JSContext *ctx, JSValueConst this_val,
						   int argc, JSValueConst *argv, int magic)
{
	InstanceData *local = JS_Local(ctx);
	ScriptVerb   *table = ScriptVerbs();
	ScriptVerb   *v     = table ? &table[magic] : NULL;
	DataObj       args[8];
	DataObj       result;
	JSValue       out;
	long          cb = 0;
	const char   *s;
	int           i, n;

	(void) this_val;

	if (!v || !local)
		return JS_UNDEFINED;

	n = v->argc < 8 ? v->argc : 8;
	for (i = 0; i < n; i++)
	{
		args[i] = NewData(STRING);
		s = (i < argc) ? JS_ToCString(ctx, argv[i]) : NULL;
		SetStr(args[i], (char *) (s ? s : ""));
		if (s)
			JS_FreeCString(ctx, s);
	}

	/* a trailing function argument becomes an opaque handle only this host
	   knows how to call back through */
	if (v->takesCallback && n < argc && JS_IsFunction(ctx, argv[n]))
		cb = (long) JS_VALUE_GET_PTR(JS_DupValue(ctx, argv[n]));

	result = v->fn(local->instance, args, cb);

	for (i = 0; i < n; i++)
		DelData(args[i]);

	if (!result)
		return JS_UNDEFINED;

	out = JS_NewString(ctx, GetStr(result));
	DelData(result);
	return out;
}

/* script.object calls this when a connect()ed source fires */
static void JSHost_Invoke(NodeObj self, long cbHandle, DataObj value)
{
	InstanceData *local = (InstanceData *) GetPropLong(self, "local");
	JSValue       fn, arg, r;

	if (!local || !local->ctx || !cbHandle)
		return;

	fn  = JS_MKPTR(JS_TAG_OBJECT, (void *) cbHandle);
	arg = JS_NewString(local->ctx, value ? GetStr(value) : "");
	r   = JS_Call(local->ctx, fn, JS_UNDEFINED, 1, (JSValueConst *) &arg);
	if (JS_IsException(r))
		JS_ReportException(local);
	JS_FreeValue(local->ctx, r);
	JS_FreeValue(local->ctx, arg);
}

/* this host's own lifecycle hooks, not verbs */
static JSValue js_oninput(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);

	(void) this_val;
	if (local && argc > 0 && JS_IsFunction(ctx, argv[0]))
	{
		JS_FreeValue(ctx, local->onin);
		local->onin = JS_DupValue(ctx, argv[0]);
	}
	return JS_UNDEFINED;
}

static JSValue js_onevent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);

	(void) this_val;
	if (local && argc > 0 && JS_IsFunction(ctx, argv[0]))
	{
		JS_FreeValue(ctx, local->onevt);
		local->onevt = JS_DupValue(ctx, argv[0]);
	}
	return JS_UNDEFINED;
}

/* ---- running ------------------------------------------------------------ */

static void JS_Teardown(InstanceData *local)
{
	if (local->ctx)
	{
		JS_FreeValue(local->ctx, local->onin);
		JS_FreeValue(local->ctx, local->onevt);
		JS_FreeContext(local->ctx);
		local->ctx = NULL;
	}
	if (local->rt)
	{
		JS_FreeRuntime(local->rt);
		local->rt = NULL;
	}
	local->onin  = JS_UNDEFINED;
	local->onevt = JS_UNDEFINED;
}

static void JS_Run(NodeObj instance)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	ScriptVerb   *table;
	JSValue       global, r;
	int           i;

	if (!local)
		return;

	JS_Teardown(local);

	local->rt  = JS_NewRuntime();
	local->ctx = JS_NewContext(local->rt);
	JS_SetContextOpaque(local->ctx, local);
	JS_SetInterruptHandler(local->rt, JS_InterruptCheck, local);
	local->onin  = JS_UNDEFINED;
	local->onevt = JS_UNDEFINED;

	global = JS_GetGlobalObject(local->ctx);

	/* the whole binding: walk the table, one trampoline per entry */
	table = ScriptVerbs();
	for (i = 0; table && table[i].name; i++)
		JS_SetPropertyStr(local->ctx, global, table[i].name,
						  JS_NewCFunctionMagic(local->ctx, JS_VerbCall, table[i].name,
											   table[i].argc, JS_CFUNC_generic_magic, i));

	JS_SetPropertyStr(local->ctx, global, "oninput",
					  JS_NewCFunction(local->ctx, js_oninput, "oninput", 1));
	JS_SetPropertyStr(local->ctx, global, "onevent",
					  JS_NewCFunction(local->ctx, js_onevent, "onevent", 1));
	JS_FreeValue(local->ctx, global);

	if (!local->source || !local->source[0])
		return;

	ScriptStartRun(instance);
	r = JS_Eval(local->ctx, local->source, strlen(local->source), "<source>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r))
		JS_ReportException(local);
	JS_FreeValue(local->ctx, r);
}

static void JS_Deliver(NodeObj instance, NodeObj data, const char *kind)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	JSValue       args[2], r;

	if (!local || !local->enabled || !local->ctx || !JS_IsFunction(local->ctx, local->onin))
		return;

	ScriptStartRun(instance);
	args[0] = JS_NewString(local->ctx, data ? GetValueStr(data) : "");
	args[1] = JS_NewString(local->ctx, kind);
	r = JS_Call(local->ctx, local->onin, JS_UNDEFINED, 2, (JSValueConst *) args);
	if (JS_IsException(r))
		JS_ReportException(local);
	JS_FreeValue(local->ctx, r);
	JS_FreeValue(local->ctx, args[0]);
	JS_FreeValue(local->ctx, args[1]);
}

/* ---- the whole driver-facing surface: one message function -------------- */
static int JS_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	switch (message)
	{
		case SCRIPT_SET_SOURCE_MSG:
			if (local->source)
				free(local->source);
			local->source = strdup(data ? GetValueStr(data) : "");
			return rtrn_handled;

		case SCRIPT_RUN_MSG:
			local->enabled = 1;
			JS_Run(instance);
			return rtrn_handled;

		case SCRIPT_IN_MSG:
			JS_Deliver(instance, data, "send");
			return rtrn_handled;

		case SCRIPT_STOP_MSG:
			local->enabled = 0;
			JS_Teardown(local);
			return rtrn_handled;

		case SCRIPT_BUDGET_MSG:
			ScriptSetBudget(instance, data ? GetValueLong(data) : 0);
			return rtrn_handled;
	}

	return rtrn_dropped;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj       instance = NewNode(INTEGER);
	NodeObj       entry;
	InstanceData *local;

	(void) message;

	local = malloc(sizeof(InstanceData));
	memset(local, 0, sizeof(InstanceData));
	local->enabled  = 1;
	local->instance = instance;
	local->onin     = JS_UNDEFINED;
	local->onevt    = JS_UNDEFINED;

	SetName(instance, "JSScript");
	SetPropLong(instance, "local", (long) local);

	/* ONE entry node - the whole surface. Nothing published, nothing wired. */
	SetPropStr(instance, "Msg", "");
	entry = GetPropNode(instance, "Msg");
	SetPropLong(entry, "OnMsg", (long) JS_MessageFunc);

	if (data)
		ScriptAttach(instance, (NodeObj) GetPropLong(data, "Owner"),
					 /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
					 (MsgId) GetPropLong(data, "MsgBase"), GetPropStr(data, "Port"));
	else
		ScriptAttach(instance, NULL, 0, NULL);

	ScriptSetInvoke(instance, JSHost_Invoke);

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	(void) message; (void) data;

	ScriptDetach(instance);

	if (local)
	{
		JS_Teardown(local);
		if (local->source)
			free(local->source);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "JSScript");
	SetPropLong(class, "InstanceStart", (long) InstanceStart);
	SetPropLong(class, "InstanceEnd", (long) InstanceEnd);

	/* the runtime-discovery marker: anything listing script languages    */
	/* (the ScriptBox shell's dropdown) walks the registry for classes    */
	/* carrying this - no list is maintained anywhere                     */

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Script");


	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "JSScript");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "c41d78a9-2f6e-4b31-9d52-8e07f6a4b2c9");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long) ClassStart);
	SetPropLong(temp, "ClassEnd", (long) ClassEnd);
	SetPropLong(temp, "ClassMsg", (long) 0);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "script.object", "Script", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
