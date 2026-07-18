#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

#include "quickjs/quickjs.h"

/*

JSScript object: a QuickJS interpreter as an ordinary dataflow object -
the SECOND language host, built deliberately as a BRIDGE CLIENT: wire
its Cmd port to a Bridge's In and the Bridge's Out to its Evt port,
and a script speaks the same JSON protocol the browser does - create,
connect, set-property, subscribe, the whole verb set, with session
naming, events and flow recording for free. A script is a peer of the
GUI, not a special API. (The Lua host predates this shape and is
untouched until this one proves the pattern.)

Ports and properties:

    Source     the JavaScript text (edit like any property; clones, saves)
    In     ->  oninput(fn) runs fn(value, kind) per arriving message
    Out        send(value) emits a dataflow message here
    Print      print(value) emits here - the ScriptBox shell wires this
               to its Output textarea, and errors land here too, loud
    Cmd        cmd(objOrString) emits one JSON protocol command here
    Evt    ->  onevent(fn) runs fn(jsonText) per arriving Bridge event
    Enable     1/0 gate, the standard convention
    State      Starting/Running/Stopping

Script globals: send, print, cmd, oninput, onevent, getprop, setprop,
log. Values cross the boundary as strings (the intelligent-data-object
rule extended into script space); cmd() accepts an object and
JSON.stringifies it, or a preformed string.

Activate (re)runs Source in a FRESH JSContext. Callbacks run
synchronously inside message delivery, like any compiled handler - and
a runaway script cannot freeze the single-threaded fabric: an
interrupt handler enforces a per-entry wall-clock budget (500ms);
overrunning it kills the script loudly (State, Print, DebugPrint).

QuickJS 2024-01-13 is vendored in ./quickjs (MIT, bellard.org) so the
module stays a single self-contained .object.

*/

#define JS_ENTRY_BUDGET_USEC 500000

typedef struct InstanceData
{
	int        active;
	int        enabled;
	JSRuntime *rt;
	JSContext *ctx;
	JSValue    onin;	/* oninput callback, JS_UNDEFINED when unset */
	JSValue    onevt;	/* onevent callback                           */
	NodeObj    instance;
	long       entryDeadline;	/* usec-of-day the current entry must yield by */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static long JS_NowUsec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000L + tv.tv_usec;
}

/* the runaway guard: called periodically by the engine mid-execution */
static int JS_InterruptCheck(JSRuntime *rt, void *opaque)
{
	InstanceData *local = (InstanceData *) opaque;
	return local && local->entryDeadline && JS_NowUsec() > local->entryDeadline;
}

static InstanceData *JS_Local(JSContext *ctx)
{
	return (InstanceData *) JS_GetContextOpaque(ctx);
}

/* one message out a named port, payload as a string */
static void JS_EmitPort(InstanceData *local, char *port, const char *text)
{
	NodeObj chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStr(chunk, (char *) (text ? text : ""));
	SndMsg(local->instance, port, msg_send, chunk);
}

/* a script failure is never silent: State stops, the text goes out     */
/* Print (the Output box), and the server log gets it at ERROR          */
static void JS_Fail(InstanceData *local, const char *what)
{
	char buf[900];

	snprintf(buf, sizeof(buf), "JSScript error: %s", what ? what : "unknown");
	DebugPrint(buf, __FILE__, __LINE__, ERROR);
	JS_EmitPort(local, "Print", buf);
	SetPropInt(local->instance, "State", Stopping);
}

static void JS_ReportException(InstanceData *local)
{
	JSValue exc = JS_GetException(local->ctx);
	const char *msg = JS_ToCString(local->ctx, exc);

	JS_Fail(local, msg ? msg : "exception");

	if (msg)
		JS_FreeCString(local->ctx, msg);
	JS_FreeValue(local->ctx, exc);
}

/* ---- the script-visible globals ---- */

static JSValue js_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;

	if (local && s)
		JS_EmitPort(local, "Out", s);
	if (s)
		JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;

	if (local && s)
		JS_EmitPort(local, "Print", s);
	if (s)
		JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

/* one protocol command out Cmd: a string goes as-is, an object is       */
/* JSON.stringify'd - wire Cmd to a Bridge's In and this IS the wire     */
static JSValue js_cmd(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);
	JSValue jstr;
	const char *s;

	if (!local || argc < 1)
		return JS_UNDEFINED;

	if (JS_IsString(argv[0]))
		jstr = JS_DupValue(ctx, argv[0]);
	else
		jstr = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);

	s = JS_ToCString(ctx, jstr);
	if (s)
	{
		JS_EmitPort(local, "Cmd", s);
		JS_FreeCString(ctx, s);
	}
	JS_FreeValue(ctx, jstr);
	return JS_UNDEFINED;
}

static JSValue js_oninput(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);

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

	if (local && argc > 0 && JS_IsFunction(ctx, argv[0]))
	{
		JS_FreeValue(ctx, local->onevt);
		local->onevt = JS_DupValue(ctx, argv[0]);
	}
	return JS_UNDEFINED;
}

static JSValue js_getprop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);
	const char *n = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	char *v = NULL;
	JSValue ret;

	if (local && n)
		v = GetPropStr(local->instance, (char *) n);
	ret = JS_NewString(ctx, v ? v : "");
	if (n)
		JS_FreeCString(ctx, n);
	return ret;
}

static JSValue js_setprop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	InstanceData *local = JS_Local(ctx);
	const char *n = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	const char *v = argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL;

	if (local && n && v)
		SetOrDeliverProp(local->instance, (char *) n, (char *) v);
	if (n)
		JS_FreeCString(ctx, n);
	if (v)
		JS_FreeCString(ctx, v);
	return JS_UNDEFINED;
}

static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	char buf[600];

	if (s)
	{
		snprintf(buf, sizeof(buf), "JSScript: %s", s);
		DebugPrint(buf, __FILE__, __LINE__, OBJMSGHANDLING);
		JS_FreeCString(ctx, s);
	}
	return JS_UNDEFINED;
}

/* call one stored callback with string args, budgeted and loud */
static void JS_CallHandler(InstanceData *local, JSValue fn, const char *a, const char *b)
{
	JSValue args[2], ret;
	int argc = 0;

	if (!local->ctx || !JS_IsFunction(local->ctx, fn))
		return;

	if (a)
		args[argc++] = JS_NewString(local->ctx, a);
	if (b)
		args[argc++] = JS_NewString(local->ctx, b);

	local->entryDeadline = JS_NowUsec() + JS_ENTRY_BUDGET_USEC;
	ret = JS_Call(local->ctx, fn, JS_UNDEFINED, argc, args);
	local->entryDeadline = 0;

	while (argc > 0)
		JS_FreeValue(local->ctx, args[--argc]);

	if (JS_IsException(ret))
		JS_ReportException(local);
	JS_FreeValue(local->ctx, ret);
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "JSScript handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* dataflow in: fn(value, kind) with kind "send"/"change"/"eof" */
int JSScript_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	char *v;

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	v = data ? GetValueStr(data) : NULL;
	JS_CallHandler(local, local->onin, v ? v : "",
				   message == msg_eof ? "eof" : (message == msg_change ? "change" : "send"));
	return rtrn_handled;
}

/* protocol events in (the Bridge's Out wired here): fn(jsonText) */
int JSScript_OnEvt(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	char *v;

	if (!local || !local->active || !local->enabled || message == msg_eof)
		return rtrn_dropped;

	v = data ? GetValueStr(data) : NULL;
	if (v)
		JS_CallHandler(local, local->onevt, v, NULL);
	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int JSScript_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	return rtrn_handled;
}

static void JSScript_FreeEngine(InstanceData *local)
{
	if (local->ctx)
	{
		JS_FreeValue(local->ctx, local->onin);
		JS_FreeValue(local->ctx, local->onevt);
		local->onin = JS_UNDEFINED;
		local->onevt = JS_UNDEFINED;
		JS_FreeContext(local->ctx);
		local->ctx = NULL;
	}
	if (local->rt)
	{
		JS_FreeRuntime(local->rt);
		local->rt = NULL;
	}
}

/* Activate: fresh runtime, register the globals, run Source */
int JSScript_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	char *src;
	JSValue global, ret;

	if (!local)
		return rtrn_dropped;

	JSScript_FreeEngine(local);

	local->rt = JS_NewRuntime();
	local->ctx = JS_NewContext(local->rt);
	JS_SetContextOpaque(local->ctx, local);
	JS_SetInterruptHandler(local->rt, JS_InterruptCheck, local);
	local->onin = JS_UNDEFINED;
	local->onevt = JS_UNDEFINED;

	global = JS_GetGlobalObject(local->ctx);
	JS_SetPropertyStr(local->ctx, global, "send",    JS_NewCFunction(local->ctx, js_send,    "send", 1));
	JS_SetPropertyStr(local->ctx, global, "print",   JS_NewCFunction(local->ctx, js_print,   "print", 1));
	JS_SetPropertyStr(local->ctx, global, "cmd",     JS_NewCFunction(local->ctx, js_cmd,     "cmd", 1));
	JS_SetPropertyStr(local->ctx, global, "oninput", JS_NewCFunction(local->ctx, js_oninput, "oninput", 1));
	JS_SetPropertyStr(local->ctx, global, "onevent", JS_NewCFunction(local->ctx, js_onevent, "onevent", 1));
	JS_SetPropertyStr(local->ctx, global, "getprop", JS_NewCFunction(local->ctx, js_getprop, "getprop", 1));
	JS_SetPropertyStr(local->ctx, global, "setprop", JS_NewCFunction(local->ctx, js_setprop, "setprop", 2));
	JS_SetPropertyStr(local->ctx, global, "log",     JS_NewCFunction(local->ctx, js_log,     "log", 1));
	JS_FreeValue(local->ctx, global);

	local->active = 1;
	SetPropInt(instance, "State", Running);

	src = GetPropStr(instance, "Source");
	if (src && src[0])
	{
		local->entryDeadline = JS_NowUsec() + JS_ENTRY_BUDGET_USEC;
		ret = JS_Eval(local->ctx, src, strlen(src), "Source", JS_EVAL_TYPE_GLOBAL);
		local->entryDeadline = 0;

		if (JS_IsException(ret))
			JS_ReportException(local);
		JS_FreeValue(local->ctx, ret);
	}

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;
	local->rt = NULL;
	local->ctx = NULL;
	local->onin = JS_UNDEFINED;
	local->onevt = JS_UNDEFINED;
	local->entryDeadline = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "JSScript");
	local->instance = instance;

	SetPropStr(instance, "Source", "");
	SetPropInt(instance, "Out", 0);
	SetPropInt(instance, "Print", 0);
	SetPropInt(instance, "Cmd", 0);
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long) local);
	SetPropLong(instance, "Activate", (long) JSScript_Activate);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long) JSScript_OnIn);

	SetPropInt(instance, "Evt", 0);
	port = GetPropNode(instance, "Evt");
	SetPropLong(port, "OnMsg", (long) JSScript_OnEvt);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long) JSScript_OnEnable);

	/* a pure object - no view: no InitPosition/PublishPosition, so it has  */
	/* no X/Y and never shows in the palette. It is used INSIDE a ScriptBox  */
	/* widget, never dragged out on its own.                                 */

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	if (local)
	{
		JSScript_FreeEngine(local);
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
	SetPropInt(class, "ScriptHost", 1);

	ClassSelf = RegisterClass(library, class);

	PublishProp(ClassSelf, "Source", "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "In",     "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Out",    "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Print",  "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Cmd",    "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Evt",    "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Enable", "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",  "data", PROP_LED, "1");

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
	SetPropStr(temp, "Version", "1.0");
	SetPropStr(temp, "Dependencies", "");
	SetPropLong(temp, "ClassStart", (long) ClassStart);
	SetPropLong(temp, "ClassEnd", (long) ClassEnd);
	SetPropLong(temp, "ClassMsg", (long) 0);
	SetPropInt(temp, "State", 1);

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
