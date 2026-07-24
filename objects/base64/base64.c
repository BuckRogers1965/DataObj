
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Base64 widget: an instrument panel that encodes or decodes text. A pure
transform, no clock: text written to Input is (en|de)coded straight into
Output whenever Input, Decode, or Enable changes.

Everything is a property; there are no in/out ports. Input and Output are
plain properties named that. Default input connection is Input, default
output connection is Output - so a flow can pipe text through the codec by
wiring to those, exactly as a user types into the panel.

*/

typedef struct InstanceData
{
	int     enabled;	/* the Enable checkbox - gates the transform */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem Base64Panel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Base64 handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- the codec (self-contained) ----------------------------------- */

static const char EncodeTable[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* one base64 digit -> its 6-bit value, or -1 for anything not in the
   alphabet (whitespace, line breaks, '=' padding, junk) - so decode ignores
   them exactly as RFC 1341 requires. */
static int Base64Val(int c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

/* encode: 3 octets -> 4 digits, '=' padding for a short final group.
   returns a malloc'd NUL-terminated string; caller frees. */
static char *Base64Encode(const char *in)
{
	size_t len = in ? strlen(in) : 0;
	size_t olen = 4 * ((len + 2) / 3);
	char  *out = malloc(olen + 1);
	size_t i, o = 0;

	if (!out)
		return NULL;

	for (i = 0; i < len; i += 3)
	{
		unsigned a = (unsigned char)in[i];
		unsigned b = (i + 1 < len) ? (unsigned char)in[i + 1] : 0;
		unsigned c = (i + 2 < len) ? (unsigned char)in[i + 2] : 0;
		unsigned triple = (a << 16) | (b << 8) | c;

		out[o++] = EncodeTable[(triple >> 18) & 0x3f];
		out[o++] = EncodeTable[(triple >> 12) & 0x3f];
		out[o++] = (i + 1 < len) ? EncodeTable[(triple >> 6) & 0x3f] : '=';
		out[o++] = (i + 2 < len) ? EncodeTable[triple & 0x3f] : '=';
	}
	out[o] = '\0';
	return out;
}

/* decode: gather 4 alphabet digits at a time (skipping everything else) and
   emit the octets they carry. A trailing 2- or 3-digit group yields 1 or 2
   bytes. returns a malloc'd NUL-terminated string; caller frees.
   (text payloads only, matching the rest of the fabric - a NUL byte in the
   decoded output would end the string early, as it would through any port.) */
static char *Base64Decode(const char *in)
{
	size_t len = in ? strlen(in) : 0;
	char  *out = malloc(len + 1);		/* decoded is always smaller */
	size_t i, o = 0;
	int    quad[4], qn = 0;

	if (!out)
		return NULL;

	for (i = 0; i < len; i++)
	{
		int v = Base64Val((unsigned char)in[i]);
		if (v < 0)
			continue;			/* skip whitespace / '=' / junk */
		quad[qn++] = v;
		if (qn == 4)
		{
			out[o++] = (quad[0] << 2) | (quad[1] >> 4);
			out[o++] = ((quad[1] & 0x0f) << 4) | (quad[2] >> 2);
			out[o++] = ((quad[2] & 0x03) << 6) | quad[3];
			qn = 0;
		}
	}
	/* a partial final group carries 1 byte (2 digits) or 2 bytes (3 digits) */
	if (qn >= 2)
	{
		out[o++] = (quad[0] << 2) | (quad[1] >> 4);
		if (qn >= 3)
			out[o++] = ((quad[1] & 0x0f) << 4) | (quad[2] >> 2);
	}
	out[o] = '\0';
	return out;
}

/* the transform: read Input live and, if Enabled, write Output - Decode
   picks the direction. Output is a plain property; setting it fans out to
   the Output textbox and anything a flow wired to it. A disabled codec
   leaves Output untouched. */
static void Base64_Recompute(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *in, *out;

	if (!local || !local->enabled)
		return;

	in = GetPropStr(instance, "Input");
	if (GetPropInt(instance, "Decode"))
		out = Base64Decode(in ? in : "");
	else
		out = Base64Encode(in ? in : "");

	SetPropStr(instance, "Output", out ? out : "");
	if (out)
		free(out);
}

/* ---- action handlers (a property with an OnMsg handler acts on write) - */

/* Input: new text arrived (typed, or wired from a source). Mirror it onto
   the port WITHOUT re-firing, then transform. */
int Base64_OnInput(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;

	SetValueStr(GetPropNode(instance, "Input"), GetValueStr(data));
	Base64_Recompute(instance);
	return rtrn_handled;
}

/* Decode: 1 = decode, 0 = encode. Retransform the current Input on toggle. */
int Base64_OnDecode(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;

	SetValueStr(GetPropNode(instance, "Decode"), GetValueInt(data) ? "1" : "0");
	Base64_Recompute(instance);
	return rtrn_handled;
}

/* Enable: 1 allows the transform, 0 gates it off. */
int Base64_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	Base64_Recompute(instance);		/* re-enabling refreshes Output */
	return rtrn_handled;
}

/* Placement setup - the framework's Activate hook, run once by the build
   task so the panel comes up live and Output reflects the initial Input. */
int Base64_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, Base64Panel);
	Base64_Recompute(instance);
	return rtrn_handled;
}

/* ---- lifecycle ------------------------------------------------------ */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "Base64");

	/* every control's initial value and handler, from the table: Input /
	   Decode / Enable carry a handler (a write acts), Output is plain data. */
	Widget_Init(instance, Base64Panel);

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Base64_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, Base64Panel);		/* main size before any subscribe */
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, Base64Panel);	/* panel built one tick from now */

	return rtrn_handled;
}

/* The whole widget in one table: the main view, the Help sub-view, and every
   control (with its initial value and, for the ports, its handler). */
static WidgetItem Base64Panel[] = {
	/* cls        prop      def  panel   x    y    w   h  label       [handler] */
	{ "View",     "Base64", "",  0,   0,   0, 380, 400, 0 },			/* 0: main */
	{ "Help",     "objects/base64/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable", "1", 0, 348,  14,   9,  9, LABEL_LEFT, (void *)Base64_OnEnable },
	{ "Textbox",  "Input",  "",  0,  15,  40, 350, 110, LABEL_TOP,  (void *)Base64_OnInput },
	{ "Checkbox", "Decode", "0", 0, 348, 165,   9,  9, LABEL_LEFT, (void *)Base64_OnDecode },
	{ "Textbox",  "Output", "",  0,  15, 190, 350, 110, LABEL_TOP },

	{ NULL }
};

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) message; (void) data;

	Widget_CancelBuild(instance);		/* drop a still-pending deferred build */
	if (local)
		free(local);

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Base64");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);		/* X/Y/W/H + the reserved view props */

	/* every control, straight from the table (widget type from the class) */
	Widget_Publish(ClassSelf, Base64Panel);
	PublishProp(ClassSelf, "State", "data", PROP_LED, "1");	/* lifecycle, no control */

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

	SetName(temp, "Base64");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "a2c8ce45-58e8-43aa-813b-7727d66368e9");
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
