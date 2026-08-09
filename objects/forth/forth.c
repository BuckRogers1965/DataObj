#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "script.h"

#include "atlast/atldef.h"		/* the stack macros a primitive needs */
#include "atlast/atlast.h"

/*
Forth host.

Opaque, exactly like the other hosts: no properties, no name, no path, no
ports. The whole interface is script.h - a driver hands over
{Owner, MsgBase, Port} at InstanceStart and then sends messages.

ONE VM, SHARED, ON PURPOSE. Every other host makes a private context per
instance; this one does not, because in Forth the dictionary IS the system.
Words defined by one script are available to the next, so a script becomes a
vocabulary that other scripts use. That is the language working as intended
rather than a limitation being tolerated.

What follows from sharing:

  - the verb table is bound ONCE, at first use, not per instance. Rebinding
    would redefine the same words and grow the dictionary every time.
  - a running word needs to know WHICH host it is running for, since sibget
    and sibset resolve against the owner. CurHost is set around every eval,
    saved and restored - the same discipline node.c uses for MsgFromNode,
    and sufficient for the same reason: the fabric is single-threaded and an
    eval is synchronous.
  - the stack is reset before each run. Junk left by one script is not the
    next script's problem, and a non-empty stack afterwards is reported as a
    diagnostic rather than silently carried.
  - SET_SOURCE marks the dictionary and RUN unwinds to that mark before
    compiling, so re-running the same source does not append a second copy of
    its definitions. Words a script defined for others to use survive until
    that script's source is loaded again.

The runaway guard needs no patch to the vendored source: atlast's inner loop
already polls Keybreak() once per word when built -DBREAK, so the Makefile
points that at Forth_PollBudget and the budget in script.object is asked
there.

Atlast 1.2 is vendored in ./atlast (public domain, John Walker - see
./atlast/COPYING and this object's README.md).
*/

typedef struct InstanceData
{
	int     enabled;
	NodeObj instance;
	char   *source;			/* our own copy - the owner holds the real one */
	char   *onin;			/* the word to run when data arrives, and the
							   entry point a step executes */
	int     marked;			/* this instance has a dictionary mark */
	atl_statemark mark;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static int     VmReady;			/* atl_init + verbs bound, once per process */
static NodeObj CurHost;			/* whose eval is running, for the verbs */
static long    PollCount;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

/* ---- the budget poll ----------------------------------------------------
   atlast calls this once per word. Asking a clock that often would cost more
   than the interpreter, so only every so often. */
void Forth_PollBudget(void)
{
	if (++PollCount < 10000)
		return;
	PollCount = 0;

	if (CurHost && ScriptOverBudget(CurHost))
		atl_break();
}

/* ---- strings ------------------------------------------------------------
   A verb's arguments and result are text. Forth passes a string as the
   address of its bytes, and atlast's own temporary string buffers are how a
   transient string is handed back - so a result goes where s" would have put
   it, and behaves the way a Forth programmer already expects. */
static char *Forth_TempStr(char *text)
{
	char *buf;

	if (!strbuf || atl_ntempstr <= 0)
		return NULL;

	buf = strbuf[cstrbuf];
	cstrbuf = (cstrbuf + 1) % ((int) atl_ntempstr);

	strncpy(buf, text ? text : "", (size_t) atl_ltempstr - 1);
	buf[atl_ltempstr - 1] = 0;

	return buf;
}

/* ---- the one trampoline -------------------------------------------------
   An atlast primitive takes no arguments, so a shared trampoline cannot be
   told which verb invoked it. One thin function per slot carries the index
   and everything else is shared: a verb added to script.object's table
   appears here with no code written. */
static ScriptVerb *VerbSlot(int i)
{
	ScriptVerb *v = ScriptVerbs();
	int         n = 0;

	for (; v && v->name; v++, n++)
		if (n == i)
			return v;

	return NULL;
}

static void Forth_VerbCall(int slot)
{
	ScriptVerb *v = VerbSlot(slot);
	DataObj     argv[8];
	DataObj     result;
	int         i, n;

	if (!v)
	{
		atl_error("verb slot is not bound");
		return;
	}

	n = v->argc < 8 ? v->argc : 8;

	/* arguments were pushed in order, so the last one is on top */
	for (i = n - 1; i >= 0; i--)
	{
		argv[i] = NewData(STRING);
		SetStr(argv[i], (char *) S0);
		Pop;
	}

	/* takesCallback verbs (connect) want a function, and a Forth word is not
	   a value this binding can carry across - so they are unavailable here
	   rather than half-working. A script subscribes by naming its handler
	   word to oninput instead. */
	result = v->fn(CurHost, argv, 0);

	for (i = 0; i < n; i++)
		DelData(argv[i]);

	/* A verb that returns nothing pushes NOTHING. Pushing a phantom empty
	   string here made every no-result verb leave an address on the stack, so
	   `dup N>S print` silently grew the stack by one and the next arithmetic
	   added a heap pointer to a number. In Forth a word's stack effect is the
	   contract: print consumes its argument and returns, getprop consumes a
	   name and leaves a string. */
	if (result)
	{
		char *tmp = Forth_TempStr(GetStr(result));

		DelData(result);
		Push = (stackitem) tmp;
	}
}

#define VERB(n)  static void Forth_V##n(void) { Forth_VerbCall(n); }
VERB(0)  VERB(1)  VERB(2)  VERB(3)  VERB(4)  VERB(5)  VERB(6)  VERB(7)
VERB(8)  VERB(9)  VERB(10) VERB(11) VERB(12) VERB(13) VERB(14) VERB(15)
VERB(16) VERB(17) VERB(18) VERB(19) VERB(20) VERB(21) VERB(22) VERB(23)
VERB(24) VERB(25) VERB(26) VERB(27) VERB(28) VERB(29) VERB(30) VERB(31)
#undef VERB

static codeptr VerbFn[] = {
	Forth_V0,  Forth_V1,  Forth_V2,  Forth_V3,  Forth_V4,  Forth_V5,
	Forth_V6,  Forth_V7,  Forth_V8,  Forth_V9,  Forth_V10, Forth_V11,
	Forth_V12, Forth_V13, Forth_V14, Forth_V15, Forth_V16, Forth_V17,
	Forth_V18, Forth_V19, Forth_V20, Forth_V21, Forth_V22, Forth_V23,
	Forth_V24, Forth_V25, Forth_V26, Forth_V27, Forth_V28, Forth_V29,
	Forth_V30, Forth_V31
};
#define VERBSLOTS ((int)(sizeof(VerbFn) / sizeof(VerbFn[0])))

/* oninput NAME - the word to run when data arrives. Not a verb: which word
   handles input is this host's own lifecycle, the same as elsewhere. */
static void Forth_OnInput(void)
{
	InstanceData *local = CurHost ? (InstanceData *) GetPropLong(CurHost, "local") : NULL;

	if (!local)
		return;

	if (local->onin)
		free(local->onin);
	local->onin = strdup((char *) S0);
	Pop;
}

/* ---- the VM, once ------------------------------------------------------- */
/* A primitive's name as atlast stores it: the FIRST BYTE IS A FLAG BYTE, not
   part of the name - lookup() compares wname + 1 - and lookup() upper-cases
   the token it is given, so the stored name must be upper case too. Both are
   conventions the release's own primdeftest.c demonstrates ("0TIME"), and
   getting either wrong yields nothing but "undefined word". A script still
   types whatever case it likes, because the token is upper-cased on the way
   in. */
static char *Forth_WordName(char *dst, int len, char *name)
{
	int i;

	dst[0] = '0';			/* the flag byte, overwritten by atlast */
	for (i = 0; name[i] && i < len - 2; i++)
		dst[i + 1] = (char) (name[i] >= 'a' && name[i] <= 'z'
							 ? name[i] - ('a' - 'A') : name[i]);
	dst[i + 1] = 0;

	return dst;
}

static void Forth_VmStart(void)
{
	static struct primfcn prims[VERBSLOTS + 2];
	static char           names[VERBSLOTS + 2][40];
	ScriptVerb           *v;
	int                   n = 0;

	if (VmReady)
		return;

	/* Sized generously ON PURPOSE. Every activate re-evaluates the whole
	   source, so each run allocates its buffers and definitions again; with
	   atlast's default heap that overflows after a handful of presses, in the
	   middle of `string`. Five megabytes is nothing on a host that measures
	   its instances in megabytes, and it turns "dies on the thirteenth press"
	   into "runs until something is actually wrong".

	   ltempstr matters too: it is the width of the buffers Forth_TempStr
	   hands back, so it is the longest string a verb can return - the default
	   would quietly truncate a Source or an Output. */
	atl_stklen   = 2048;
	atl_rstklen  = 1024;
	atl_heaplen  = 16384;			/* stackitems - a Forth's worth, not a heap */
	atl_ltempstr = 4096;			/* the widest string a verb can return */
	atl_ntempstr = 8;

	atl_init();
	atl_redef = 1;		/* re-running source redefines its own words quietly */

	for (v = ScriptVerbs(); v && v->name; v++)
	{
		if (n >= VERBSLOTS)
		{
			char msg[160];

			snprintf(msg, sizeof(msg),
					 "forth: the verb table has more than %d entries - '%s' and "
					 "anything after it is unavailable; add more VERB() slots",
					 VERBSLOTS, v->name);
			DebugPrint(msg, __FILE__, __LINE__, ERROR);
			break;
		}
		prims[n].pname = Forth_WordName(names[n], (int) sizeof(names[0]), v->name);
		prims[n].pcode = VerbFn[n];
		n++;
	}

	prims[n].pname = Forth_WordName(names[n], (int) sizeof(names[0]), "oninput");
	prims[n].pcode = (codeptr) Forth_OnInput;
	n++;
	prims[n].pname = NULL;
	prims[n].pcode = (codeptr) 0;

	atl_primdef(prims);

	VmReady = 1;
}

/* ---- running ------------------------------------------------------------ */

/* every entry into the interpreter goes through here: whose host is running,
   the budget clock, and the stack reset all belong together */
static int Forth_Eval(NodeObj instance, char *text)
{
	NodeObj prev = CurHost;
	int     stat;

	CurHost   = instance;
	PollCount = 0;

	ScriptStartRun(instance);
	stat = atl_eval(text);
	CurHost = prev;

	if (stat != ATL_SNORM)
	{
		char msg[200];

		snprintf(msg, sizeof(msg), "forth: eval failed, status %d%s", stat,
				 stat == ATL_BREAK ? " (over its time budget)" : "");
		ScriptReport(instance, SCRIPT_ERROR, msg);
	}

	return stat;
}

static void Forth_Run(NodeObj instance)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	if (!local)
		return;

	Forth_VmStart();

	/* No dictionary rewind between runs. Re-running the same source simply
	   redefines its own words (atl_redef is on, so quietly), which is how
	   Forth behaves; the mark/unwind that used to be here was the only thing
	   that made a second run take a different path from the first, and a
	   second click behaved nothing like the first because of it. */

	if (!local->source || !local->source[0])
		return;

	/* RUN IS COMPILE. The driver's Activate hands the text over and asks for
	   this; stepping is the In line, which executes the entry word against
	   what was compiled (Forth_Deliver).

	   Rewind this instance's own definitions first, so compiling twice does
	   not stack a second copy of everything the source defines. Without it
	   each compile permanently consumed dictionary and heap - another buffer,
	   another definition - and the interpreter overflowed after a dozen
	   presses. That is what the mark is for. */
	if (local->marked)
		atl_unwind(&local->mark);
	atl_mark(&local->mark);
	local->marked = 1;

	Forth_Eval(instance, local->source);
}

static void Forth_Deliver(NodeObj instance, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");
	NodeObj       prev;
	dictword     *dw;
	char         *value;
	char          word[64];
	int           stat;

	if (!local || !local->enabled || !local->onin || !VmReady)
		return;

	value = data ? GetValueStr(data) : (char *) "";
	if (!value)
		value = (char *) "";

	/* The value is PUSHED and the handler EXECUTED directly, rather than
	   composing source text like "value" HANDLER and evaluating that: a value
	   containing a double quote would end the literal early and the rest of it
	   would be interpreted as code. Data arriving from a wire is exactly where
	   that happens, so there is no quoting to get wrong here at all.

	   atl_lookup upper-cases the name it is handed, in place, so it gets a
	   copy rather than our stored one. */
	snprintf(word, sizeof(word), "%s", local->onin);
	dw = atl_lookup(word);
	if (!dw)
	{
		char msg[160];

		snprintf(msg, sizeof(msg), "forth: no word named '%s' to receive input",
				 local->onin);
		ScriptReport(instance, SCRIPT_ERROR, msg);
		return;
	}

	prev      = CurHost;
	CurHost   = instance;
	PollCount = 0;

	ScriptStartRun(instance);
	Push = (stackitem) Forth_TempStr(value);
	stat = atl_exec(dw);
	CurHost = prev;

	if (stat != ATL_SNORM)
	{
		char msg[160];

		snprintf(msg, sizeof(msg), "forth: '%s' failed, status %d%s", local->onin,
				 stat, stat == ATL_BREAK ? " (over its time budget)" : "");
		ScriptReport(instance, SCRIPT_ERROR, msg);
	}
}

/* ---- the whole driver-facing surface: one message function -------------- */
static int Forth_MessageFunc(NodeObj instance, MsgId message, NodeObj data)
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
			Forth_Run(instance);
			return rtrn_handled;

		case SCRIPT_IN_MSG:
			Forth_Deliver(instance, data);
			return rtrn_handled;

		case SCRIPT_STOP_MSG:
			local->enabled = 0;
			/* the dictionary is shared and outlives us; only this instance's
			   own definitions go */
			if (local->marked)
			{
				atl_unwind(&local->mark);
				local->marked = 0;
			}
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

	SetName(instance, "Forth");
	SetPropLong(instance, "local", (long) local);

	/* ONE entry node, like every host: nothing here is presented, addressed,
	   wired or saved */
	SetPropStr(instance, "Msg", "");
	entry = GetPropNode(instance, "Msg");
	SetPropLong(entry, "OnMsg", (long) Forth_MessageFunc);

	if (data)
		ScriptAttach(instance, (NodeObj) GetPropLong(data, "Owner"),
					 (MsgId) GetPropLong(data, "MsgBase"), GetPropStr(data, "Port"));
	else
		ScriptAttach(instance, NULL, 0, NULL);

	/* no SetInvoke: a Forth word is not a value this binding can hand to
	   script.object and get back, so callback verbs are simply not offered
	   here - see Forth_VerbCall */

	RegisterInstance(class, instance);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *) GetPropLong(instance, "local");

	(void) message; (void) data;

	ScriptDetach(instance);

	if (CurHost == instance)
		CurHost = NULL;

	if (local)
	{
		if (local->marked)
			atl_unwind(&local->mark);
		if (local->source)
			free(local->source);
		if (local->onin)
			free(local->onin);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Forth");
	SetPropLong(class, "InstanceStart", (long) InstanceStart);
	SetPropLong(class, "InstanceEnd", (long) InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Script");

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

	SetName(temp, "Forth");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "7e41c9d8-0b52-4a36-8c1f-3d9a6e2b74f0");
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
