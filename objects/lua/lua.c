#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "script.h"

#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
#include "control.h"	/* PROP_* - what a published property presents as */

/*
Lua host.

It is an OPAQUE object: no properties, no ports, no name, no path. Its whole
interface is script.h - a driver creates one through this class's own
InstanceStart, hands over {Owner, MsgBase, Port}, then sends it messages and
catches answers. Nothing can find it, wire it, save it or reach into it.

The source a user typed lives on the WIDGET that owns this host (a ScriptBox's
Source property, an MCPAgent's), which is the thing that gets serialized and
cloned; the host is rebuilt from it on every start. That is what lets a
language swap keep the code and a clone come up working.

What a script sees is NOT defined here. script.object owns the verb table and
implements every verb once, in C, over DataObj; this file walks that table and
registers each entry through one trampoline. Add a verb there and Lua has it
without this file changing - which is the point, because Lua and JSScript had
already drifted apart (this host had sibget/sibset and no print, the other had
print and no siblings, and only the other had a runaway guard).

The one thing still spelled here is oninput(fn), because "the function to run
when data arrives" is the host's own lifecycle rather than a verb.

Lua 5.4.7 is vendored in ./lua (MIT license, lua.org) so the module stays a
single self-contained .object.
*/

typedef struct InstanceData
{
	int        enabled;
	lua_State *L;
	int        onin_ref;	/* registry ref of the oninput callback */
	NodeObj    instance;
	char      *source;		/* our own copy - the owner holds the real one */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static InstanceData *Script_Local(lua_State *L)
{
	InstanceData *local;

	lua_getfield(L, LUA_REGISTRYINDEX, "script_local");
	local = (InstanceData *) lua_touserdata(L, -1);
	lua_pop(L, 1);
	return local;
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

/* ---- the one trampoline -------------------------------------------------
   Every verb in script.object's table is registered through this, with the
   ScriptVerb* as an upvalue. Marshal Lua values to DataObj, call, marshal the
   result back. A verb added over there needs no code here. */
static int Script_VerbCall(lua_State *L)
{
	ScriptVerb   *v     = (ScriptVerb *) lua_touserdata(L, lua_upvalueindex(1));
	InstanceData *local = Script_Local(L);
	DataObj       argv[8];
	DataObj       result;
	long          cb = 0;
	int           i, n;

	if (!v || !local)
		return 0;

	n = v->argc < 8 ? v->argc : 8;
	for (i = 0; i < n; i++)
	{
		argv[i] = NewData(STRING);
		/* lua_tostring converts numbers in place, which is exactly the
		   DataObj bargain: hand over what you have, conversion is not the
		   caller's problem */
		SetStr(argv[i], (char *) (lua_isnoneornil(L, i + 1) ? "" : luaL_tolstring(L, i + 1, NULL)));
		if (!lua_isnoneornil(L, i + 1))
			lua_pop(L, 1);				/* luaL_tolstring pushed a copy */
	}

	/* a trailing function argument becomes an opaque handle: a registry ref
	   only this host knows how to call back through */
	if (v->takesCallback && lua_isfunction(L, n + 1))
	{
		lua_pushvalue(L, n + 1);
		cb = (long) luaL_ref(L, LUA_REGISTRYINDEX);
	}

	result = v->fn(local->instance, argv, cb);

	for (i = 0; i < n; i++)
		DelData(argv[i]);

	if (!result)
		return 0;

	lua_pushstring(L, GetStr(result));
	DelData(result);
	return 1;
}

/* script.object calls this when a connect()ed source fires: turn the handle
   back into a Lua function and call it. Only this file knows how. */
static void Script_Invoke(NodeObj self, long cbHandle, DataObj value)
{
	InstanceData *local = (InstanceData *) GetPropLong(self, "local");

	if (!local || !local->L || !cbHandle)
		return;

	lua_rawgeti(local->L, LUA_REGISTRYINDEX, (int) cbHandle);
	lua_pushstring(local->L, value ? GetStr(value) : "");
	if (lua_pcall(local->L, 1, 0, 0) != LUA_OK)
	{
		ScriptReport(self, SCRIPT_ERROR, (char *) lua_tostring(local->L, -1));
		lua_pop(local->L, 1);
	}
}

/* the runaway guard: Lua had none at all. The budget lives in script.object;
   this is just the hook that asks. */
static void Script_Hook(lua_State *L, lua_Debug *ar)
{
	InstanceData *local = Script_Local(L);

	(void) ar;

	if (local && ScriptOverBudget(local->instance))
		luaL_error(L, "script exceeded its time budget");
}

/* the one thing that is genuinely this host's own: which function to run when
   data arrives. Not a verb - a lifecycle hook. */
static int l_oninput(lua_State *L)
{
	InstanceData *local = Script_Local(L);

	if (!local)
		return 0;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (local->onin_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, local->onin_ref);
	lua_pushvalue(L, 1);
	local->onin_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* ---- running ------------------------------------------------------------ */

static void Script_Teardown(InstanceData *local)
{
	if (local->L)
	{
		lua_close(local->L);
		local->L = NULL;
	}
	local->onin_ref = LUA_NOREF;
}

static void Script_Run(NodeObj instance)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	ScriptVerb   *v;

	if (!local)
		return;

	Script_Teardown(local);

	local->L = luaL_newstate();
	if (!local->L)
	{
		ScriptReport(instance, SCRIPT_ERROR, "could not create a Lua state");
		return;
	}
	luaL_openlibs(local->L);

	lua_pushlightuserdata(local->L, local);
	lua_setfield(local->L, LUA_REGISTRYINDEX, "script_local");

	/* the whole binding: walk the table, one trampoline per entry */
	for (v = ScriptVerbs(); v && v->name; v++)
	{
		lua_pushlightuserdata(local->L, v);
		lua_pushcclosure(local->L, Script_VerbCall, 1);
		lua_setglobal(local->L, v->name);
	}
	lua_register(local->L, "oninput", l_oninput);

	/* ask the guard every 10000 VM instructions */
	lua_sethook(local->L, Script_Hook, LUA_MASKCOUNT, 10000);

	if (!local->source || !local->source[0])
		return;

	ScriptStartRun(instance);
	if (luaL_loadstring(local->L, local->source) != LUA_OK
		|| lua_pcall(local->L, 0, 0, 0) != LUA_OK)
	{
		ScriptReport(instance, SCRIPT_ERROR, (char *) lua_tostring(local->L, -1));
		lua_pop(local->L, 1);
	}
}

static void Script_Deliver(NodeObj instance, NodeObj data, char *kind)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	if (!local || !local->enabled || !local->L || local->onin_ref == LUA_NOREF)
		return;

	ScriptStartRun(instance);
	lua_rawgeti(local->L, LUA_REGISTRYINDEX, local->onin_ref);
	lua_pushstring(local->L, data ? GetValueStr(data) : "");
	lua_pushstring(local->L, kind);
	if (lua_pcall(local->L, 2, 0, 0) != LUA_OK)
	{
		ScriptReport(instance, SCRIPT_ERROR, (char *) lua_tostring(local->L, -1));
		lua_pop(local->L, 1);
	}
}

/* ---- the whole driver-facing surface: one message function -------------- */
static int Script_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
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
			Script_Run(instance);
			return rtrn_handled;

		case SCRIPT_IN_MSG:
			Script_Deliver(instance, data, "send");
			return rtrn_handled;

		case SCRIPT_STOP_MSG:
			local->enabled = 0;
			Script_Teardown(local);
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
	local->onin_ref = LUA_NOREF;
	local->enabled  = 1;
	local->instance = instance;

	SetName(instance, "Lua");
	SetPropLong(instance, "local", (long) local);

	/* ONE entry node. Every macro in script.h delivers to "Msg" - that is
	   the whole surface. No Source, no In, no Out, no Enable, no State:
	   nothing here is presented, addressed, wired or saved. */
	SetPropStr(instance, "Msg", "");
	entry = GetPropNode(instance, "Msg");
	SetPropLong(entry, "OnMsg", (long) Script_MessageFunc);

	/* where to report back to, chosen by whoever created us */
	if (data)
		ScriptAttach(instance, (NodeObj) GetPropLong(data, "Owner"),
					 (MsgId) GetPropLong(data, "MsgBase"), GetPropStr(data, "Port"));
	else
		ScriptAttach(instance, NULL, 0, NULL);

	ScriptSetInvoke(instance, Script_Invoke);

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
		Script_Teardown(local);
		if (local->source)
			free(local->source);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Lua");
	SetPropLong(class, "InstanceStart", (long) InstanceStart);
	SetPropLong(class, "InstanceEnd", (long) InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Script");

	/* NOTHING is published, and there is no ScriptHost marker: the interface
	   is script.h, and "which classes are language hosts" is now a question
	   about this class's parent rather than a flag each host has to remember
	   to set. */

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

	SetName(temp, "Lua");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b3a4f0e2-6c1d-4b8e-9f27-51d0aa4c9e63");
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
