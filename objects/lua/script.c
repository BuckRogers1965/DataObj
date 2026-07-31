#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"

/*

Script object: a Lua interpreter as an ordinary dataflow object - the
roadmap's "languages as extensions", first step.

The script text lives in the Source property (edit it on the dissection
table like any other property; it saves in session.flow, clones with
the instance). Activate (re)runs it. The instance has ordinary In/Out
ports, so a script IS a flow object: wire anything's Out to the
script's In and the script's callback runs on every message - this is
the "subscribe to updates directly and call a function on change"
mechanism, and it is nothing more than Connect() reaching a Lua
function through the same OnMsg trampoline every compiled handler uses
(function pointers in node properties don't care what they point at).

What a script sees (registered as globals in its Lua state):

    oninput(fn)      fn(value, msgkind) runs for every message arriving
                     on In - msgkind is "send", "change", or "eof"
    send(value)      send a message out the Out port
    getprop(name)    read one of this instance's own properties
    setprop(n, v)    write one of this instance's own properties
    sibget(name)     read a sibling's Value by name (Container + "/" +
                     name, resolved fresh every call - a sibling control
                     dragged in beside this script, or generated
                     alongside it, e.g. by a container-widget host)
    sibset(n, v)     write a sibling's Value the same way
    log(text)        a line on the server's stdout, tagged

Callbacks run synchronously inside message delivery, exactly like a
compiled handler - keep them short, never busy-wait (the fabric is
single-threaded and polled; a blocking script blocks everything).
Cooperative sleep/wait via coroutines parked on scheduler tasks is the
planned next step.

Lua 5.4.7 is vendored in ./lua (MIT license, lua.org) so the module
stays a single self-contained .object.

*/

typedef struct InstanceData
{
	int        active;
	int        enabled;
	lua_State *L;
	int        onin_ref;	/* registry ref of the oninput callback */
	NodeObj    instance;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

int Script_Activate(NodeObj instance, MsgId message, NodeObj data);

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Script handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the Lua-visible API ---------------------------------------------- */

static InstanceData *Script_Local(lua_State *L)
{
	InstanceData *local;

	lua_getfield(L, LUA_REGISTRYINDEX, "script_local");
	local = (InstanceData *) lua_touserdata(L, -1);
	lua_pop(L, 1);
	return local;
}

static int l_send(lua_State *L)
{
	InstanceData *local = Script_Local(L);
	const char *v = luaL_checkstring(L, 1);
	NodeObj chunk;

	if (!local)
		return 0;

	chunk = NewNode(STRING);
	SetName(chunk, "Data");
	SetValueStr(chunk, (char *) v);
	SndMsg(local->instance, "Out", msg_send, chunk);
	return 0;
}

static int l_oninput(lua_State *L)
{
	InstanceData *local = Script_Local(L);

	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (!local)
		return 0;

	if (local->onin_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, local->onin_ref);
	lua_pushvalue(L, 1);
	local->onin_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

static int l_getprop(lua_State *L)
{
	InstanceData *local = Script_Local(L);
	const char *name = luaL_checkstring(L, 1);
	char *v;
	char dbg[300];

	if (!local)
		return 0;

	v = GetPropStr(local->instance, (char *) name);
	snprintf(dbg, sizeof(dbg), "getprop('%s') on %s/%s (instance=%p) -> %s%s%s",
			 name, GetPropStr(local->instance, "Container"), GetPropStr(local->instance, "Name"),
			 (void *)local->instance, v ? "'" : "", v ? v : "nil", v ? "'" : "");
	DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	if (v)
		lua_pushstring(L, v);
	else
		lua_pushnil(L);
	return 1;
}

static int l_setprop(lua_State *L)
{
	InstanceData *local = Script_Local(L);
	const char *name = luaL_checkstring(L, 1);
	const char *v = luaL_checkstring(L, 2);
	char dbg[300];

	if (!local)
		return 0;
	snprintf(dbg, sizeof(dbg), "setprop('%s', '%s') on %s/%s (instance=%p)",
			 name, v, GetPropStr(local->instance, "Container"), GetPropStr(local->instance, "Name"),
			 (void *)local->instance);
	DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	SetOrDeliverProp(local->instance, (char *) name, (char *) v);
	return 0;
}

/* the sibling this script's own Container + name resolves to right now -
   never cached, so it is whatever currently sits there: the original,
   or a clone's own sibling with the same relative name (a clone's
   children keep their names, only the container path changes) */
static NodeObj Script_Sibling(NodeObj instance, const char *name)
{
	char cont[300], path[360];

	if (!GetPropStr(instance, "Container"))
		return NULL;
	snprintf(cont, sizeof(cont), "%s", GetPropStr(instance, "Container"));
	snprintf(path, sizeof(path), "%s/%s", cont, name);
	return ResolvePath(path);
}

/* read a sibling control's own Value by name - this is the real fix for
   "getprop/setprop don't survive clone": a sibling Textbox is a proper,
   independently cloned instance with its own published Value, already
   correctly copied by the engine's own clone mechanism. No Connect-based
   mirror onto this script's own properties is needed at all - the script
   just reaches over and reads/writes the control directly, by path,
   the same "reach a sibling by path" mechanism this whole roadmap phase
   (container ports, path addressing) is built on. */
static int l_sibget(lua_State *L)
{
	InstanceData *local = Script_Local(L);
	const char *name = luaL_checkstring(L, 1);
	NodeObj sib;
	char *v;
	char dbg[300];

	if (!local)
		return 0;
	sib = Script_Sibling(local->instance, name);
	v = sib ? GetPropStr(sib, "Value") : NULL;
	snprintf(dbg, sizeof(dbg), "sibget('%s') from %s -> %s%s%s",
			 name, sib ? "found" : "MISSING",
			 v ? "'" : "", v ? v : "nil", v ? "'" : "");
	DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	if (v)
		lua_pushstring(L, v);
	else
		lua_pushnil(L);
	return 1;
}

static int l_sibset(lua_State *L)
{
	InstanceData *local = Script_Local(L);
	const char *name = luaL_checkstring(L, 1);
	const char *v = luaL_checkstring(L, 2);
	NodeObj sib;
	char dbg[300];

	if (!local)
		return 0;
	sib = Script_Sibling(local->instance, name);
	snprintf(dbg, sizeof(dbg), "sibset('%s', '%s') -> %s", name, v, sib ? "found" : "MISSING");
	DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	if (sib)
		SetOrDeliverProp(sib, "Value", (char *) v);
	return 0;
}

static int l_log(lua_State *L)
{
	InstanceData *local = Script_Local(L);
	const char *msg = luaL_checkstring(L, 1);
	char *name = local ? GetPropStr(local->instance, "Name") : NULL;

	printf("[script %s] %s\n", name && name[0] ? name : "?", msg);
	fflush(stdout);
	return 0;
}

/* ---- the trampoline: messages on In become Lua calls ------------------- */

int Script_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	char *value;
	const char *kind;

	/* a clone (or anything else that never got an explicit "activate")
	   has Source copied (a published property) but no interpreter -
	   clone calls no class's Activate, only an explicit user command
	   does (Bridge_DoActivate). Rather than sit dead forever, run
	   Source now, on the first real message, same as a fresh instance's
	   own build-time Activate call already does. */
	if (local && !local->L)
		Script_Activate(instance, message, data);

	if (!local || !local->enabled || !local->L || local->onin_ref == LUA_NOREF)
	{
		char dbg[300];
		snprintf(dbg, sizeof(dbg),
				 "Script_OnIn dropped on '%s': local=%p enabled=%d L=%p onin_ref=%d",
				 GetPropStr(instance, "Name"), (void *)local,
				 local ? local->enabled : -1, local ? (void *)local->L : NULL,
				 local ? local->onin_ref : -999);
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
		return rtrn_dropped;
	}

	value = data ? GetValueStr(data) : NULL;
	kind = (message == msg_eof) ? "eof" : (message == msg_change) ? "change" : "send";

	lua_rawgeti(local->L, LUA_REGISTRYINDEX, local->onin_ref);
	lua_pushstring(local->L, value ? value : "");
	lua_pushstring(local->L, kind);
	if (lua_pcall(local->L, 2, 0, 0) != LUA_OK)
	{
		printf("[script error] %s\n", lua_tostring(local->L, -1));
		fflush(stdout);
		lua_pop(local->L, 1);
		DebugPrint ( "Script callback raised an error.", __FILE__, __LINE__, ERROR);
	}
	else
	{
		char dbg[200];
		snprintf(dbg, sizeof(dbg), "Script_OnIn ran oninput on '%s' (kind=%s)", GetPropStr(instance, "Name"), kind);
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Script_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* Activate (re)runs Source in a fresh interpreter - editing the script    */
/* and activating again is the whole development loop                       */
int Script_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	char *source;

	if (!local)
		return rtrn_dropped;

	if (local->L)
	{
		lua_close(local->L);
		local->L = NULL;
		local->onin_ref = LUA_NOREF;
	}

	local->L = luaL_newstate();
	if (!local->L)
	{
		DebugPrint ( "Script could not create a Lua state.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}
	luaL_openlibs(local->L);

	lua_pushlightuserdata(local->L, local);
	lua_setfield(local->L, LUA_REGISTRYINDEX, "script_local");

	lua_register(local->L, "send",    l_send);
	lua_register(local->L, "oninput", l_oninput);
	lua_register(local->L, "getprop", l_getprop);
	lua_register(local->L, "setprop", l_setprop);
	lua_register(local->L, "sibget",  l_sibget);
	lua_register(local->L, "sibset",  l_sibset);
	lua_register(local->L, "log",     l_log);

	source = GetPropStr(instance, "Source");
	if (source && source[0])
	{
		if (luaL_loadstring(local->L, source) != LUA_OK
			|| lua_pcall(local->L, 0, 0, 0) != LUA_OK)
		{
			printf("[script error] %s\n", lua_tostring(local->L, -1));
			fflush(stdout);
			lua_pop(local->L, 1);
			DebugPrint ( "Script Source failed to run.", __FILE__, __LINE__, ERROR);
		}
	}

	local->active = 1;
	SetPropInt(instance, "State", Running);

	{
		char dbg[200];
		snprintf(dbg, sizeof(dbg), "Script_Activate ran on '%s': onin_ref=%d (%s)",
				 GetPropStr(instance, "Name"), local->onin_ref,
				 local->onin_ref == LUA_NOREF ? "no oninput() registered" : "oninput() registered");
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}

	return rtrn_handled;
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;
	local->L = NULL;
	local->onin_ref = LUA_NOREF;

	instance = NewNode(INTEGER);
	SetName(instance, "Lua");
	local->instance = instance;

	SetPropStr(instance, "Source", "");
	WatchableProp(instance, "Source");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long) local);
	SetPropLong(instance, "Activate", (long) Script_Activate);

	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long) Script_OnIn);

	SetPropInt(instance, "Out", 0);

	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long) Script_OnEnable);

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
		if (local->L)
			lua_close(local->L);
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

	/* runtime-discovery marker so this Lua host shows up in the ScriptBox  */
	/* language dropdown alongside JSScript - a tag only, no behavior change */
	SetPropInt(class, "ScriptHost", 1);

	ClassSelf = RegisterClass(library, class);

	PublishProp(ClassSelf, "Source", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "In", PROP_NULL, "");
	PublishProp(ClassSelf, "Out", PROP_NULL, "");
	PublishProp(ClassSelf, "Enable", PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State", PROP_LED, "1");

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
