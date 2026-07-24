
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

LogicGate object: a logic-gate instrument panel. Built on the same panel
mechanics as PulseGenerator (deferred build, sub-panel, reflect-and-seed,
help-on-open). It combines its In by the chosen Mode (OR / AND / XOR /
Parity) with an optional Invert and publishes the result on Out.

As a single-input OR with Invert on, it is a NOT gate - which turns a Pulse
Generator's Out into its inverse: feed pulse Out into In, wire Out into a
second Stopwatch's Run, and that stopwatch times the LOW phase while the
first (on the pulse directly) times the HIGH phase - both duty cycles.

Everything is a property. In/Interpret/Enable carry a handler so a write
acts; Mode/Invert/ChangesOnly/AutoInterpret are plain data read live; Out
is a plain property whose write fans out to whatever is wired to it. In and
Out are ordinary properties named In and Out - not a special "port" kind.

NOTE on multiple inputs: this framework delivers one value at a time and does
not yet expose a port's sources, so OR/AND/XOR here operate on the single
arriving value (identity for one input) and Parity toggles per event -
faithful for the inverter/buffer use. True N-input combination waits on the
source-enumeration primitive (ROADMAP.md, Phase 8).

*/

typedef struct InstanceData
{
	int     enabled;	/* the Enable checkbox - gates the gate       */
	int     lastInput;	/* the most recent value seen on In           */
	int     parity;		/* running parity state for Parity mode       */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem LogicGatePanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("LogicGate handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the logic ------------------------------------------------------ */

static int LogicGate_Is(NodeObj instance, char *prop, char *val)
{
	char *cur = GetPropStr(instance, prop);
	return cur && strcmp(cur, val) == 0;
}

/* single-input case: OR/AND/XOR of one
   value is that value; Parity toggles on each event; Invert flips. */
static int LogicGate_Compute(NodeObj instance, int in)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int r;

	if (LogicGate_Is(instance, "GateMode", "Parity Gate"))
	{
		local->parity = !local->parity;
		r = local->parity;
	}
	else
	{
		r = in ? 1 : 0;
	}

	if (GetPropInt(instance, "InvertOp"))
		r = !r;

	return r;
}

/* Out is just a property. Setting it fans out to everything subscribed -
   the Out LED, and anything a flow wired to it - honoring Changes Only. */
static void LogicGate_Emit(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	int r = LogicGate_Compute(instance, local->lastInput);

	if (GetPropInt(instance, "ChangesOnly") && GetPropInt(instance, "Out") == r)
		return;

	SetPropStr(instance, "Out", r ? "1" : "0");
}

/* ---- action handlers ------------------------------------------------ */

/* In: a value arrived from a wired source. Remember it, and (enabled and
   Auto Interpret on) recompute and publish. */
int LogicGate_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	local->lastInput = GetValueInt(data) ? 1 : 0;

	if (!local->enabled)
		return rtrn_handled;
	if (!GetPropInt(instance, "AutoInterpret"))
		return rtrn_handled;		/* wait for Interpret */

	LogicGate_Emit(instance);
	return rtrn_handled;
}

/* Interpret: recompute now from the last input (for use when Auto Interpret
   is off) */
int LogicGate_OnInterpret(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local || !local->enabled)
		return rtrn_handled;

	LogicGate_Emit(instance);
	return rtrn_handled;
}

/* Enable: gates the gate */
int LogicGate_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* Placement setup - run once by the build task so the panel comes up live
   (gated only by Enable, which defaults on). */
int LogicGate_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, LogicGatePanel);
	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->lastInput = 0;
	local->parity = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "LogicGate");

	/* every control's value + handler from the table (Enable/Interpret carry
	   a handler; GateMode/the checkboxes/Out are plain data) */
	Widget_Init(instance, LogicGatePanel);

	/* the wire input (has a handler, no control), the dropdown's backing list,
	   and the lifecycle state */
	Widget_Port(instance, "In", "0", (void *)LogicGate_OnIn);
	SetPropStr(instance, "GateModeList", "OR Gate,AND Gate,XOR Gate,Parity Gate");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)LogicGate_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, LogicGatePanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, LogicGatePanel);

	return rtrn_handled;
}

/* The whole widget in one table: main view, Help, every control (value +, for
   the ports, handler). In (a wire input), GateModeList and State are apart. */
static WidgetItem LogicGatePanel[] = {
	/* cls        prop            def       panel   x    y    w    h  label       [handler] */
	{ "View",     "LogicGate",    "",       0,   0,   0, 300, 250, 0 },			/* 0: main */
	{ "Help",     "objects/logicgate/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	{ "Checkbox", "Enable",        "1",      0, 148,  13,   8,  8, LABEL_LEFT, (void *)LogicGate_OnEnable },
	{ "Dropdown", "GateMode",      "OR Gate",0,  15,  57, 178, 15, LABEL_NONE },
	{ "Checkbox", "InvertOp",      "0",      0,  16,  89,   8,  8, LABEL_NONE },
	{ "Checkbox", "ChangesOnly",   "1",      0, 108,  89,   8,  8, LABEL_NONE },
	{ "Checkbox", "AutoInterpret", "1",      0, 108, 119,   8,  8, LABEL_NONE },
	{ "MoButton", "Interpret",     "0",      0, 108, 141,  60, 20, LABEL_NONE, (void *)LogicGate_OnInterpret },
	{ "LED",      "Out",           "0",      0,  14, 118,  12, 12, LABEL_NONE },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);
	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "LogicGate");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (widget type from each control's class) */
	Widget_Publish(ClassSelf, LogicGatePanel);

	/* the wire input, the dropdown's backing list, and the lifecycle state */
	PublishProp(ClassSelf, "In",           "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "GateModeList", "data", PROP_NULL, "OR Gate,AND Gate,XOR Gate,Parity Gate");
	PublishProp(ClassSelf, "State",        "data", PROP_LED, "1");

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

	SetName(temp, "LogicGate");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "b6027f4a-3c81-49e5-a2d0-71f4c8e5309b");
	SetPropStr(temp, "Version", "1.0");
	SetPropStr(temp, "Dependencies", "");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
