
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "script.h"

/*

MCPSource: a bridge from an "MCP-style" agent service (raw TCP,
newline-delimited JSON, {"command":"LIST_AGENTS"} / {"command":
"EXECUTE_AGENT", ...}) into the palette. It holds no socket of its own
for discovery either - it contains a TCP object, in client mode, and
drives it, the same shell/engine split TCPPort uses for its own
contained TCP object.

Connect asks the service for its agent list and, for each agent
returned, builds one View inside a group named ViewName under
/Root/Palette - one input Textbox per declared input, one TextOut per
declared output, the agent's own help text, and a Submit button. That
View is an ordinary palette member from then on: cloneable into a flow
like anything else. No new compiled class registers per agent - the
schema varies per agent (a different number of inputs/outputs each
time), and this framework's widget tables are fixed at compile time, so
a per-agent shape has to be built at runtime instead. Widget_Create
registers the generated View into the namespace exactly the same way
any compiled class's InstanceStart does - the mechanism doesn't care
whether the caller is a ClassStart or, as here, a runtime loop.

Each agent View also holds a Lua instance (objects/lua) whose generated
Source knows its own inputs/outputs (baked in as Lua literals at
generation time). Submit sends the gathered inputs, as one flat JSON
object, out the script's own Out port. That's wired to a small handler
this file registers directly on the agent's View (an ordinary port -
message delivery doesn't care what class owns it) which does its OWN
TCP object call: creates a fresh TCP client instance right there, dials
the host/port it reads back from the connector at that moment (never
copied in - always a live read), sends one EXECUTE_AGENT command,
receives one reply, and tears the connection back down - one shot per
request, same pattern TCPPort/TPLink use for their own single
connection. So every agent widget drives its own connection
independently; the connector is only ever asked for where to dial, not
asked to do the dialing.

Because the per-agent unit is an ordinary View containing ordinary Lua
and TCP instances - all existing classes - nothing new needs excluding
from the automatic palette seed: Lua already has no X/Y of its own
(same reason it never appears in the palette on its own,
objects/lua/script.c), so this needed no change anywhere outside this
file.

Wire protocol: one line in, one line out, newline-delimited JSON - not
the TP-Link style length-prefixed binary framing, so the
accumulate-until step here watches for '\n' instead of a byte count.

*/

#define MCP_TIMEOUT_MS 8000

enum { MCP_IDLE, MCP_LIST };

#define NS_IDLE      "Idle"
#define NS_DISABLED  "Disabled"
#define NS_CONNECT   "Connecting"
#define NS_WAIT      "Waiting for reply"

static NodeObj LibrarySelf;
static NodeObj SourceClass;
static WidgetItem MCPSourcePanel[];

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("MCPSource handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- growable string buffer (same shape as ollama.c's SB) ---- */

typedef struct { char *b; size_t n, cap; } SB;

static void sbput(SB *s, const char *p, size_t k)
{
	if (s->n + k + 1 > s->cap)
	{
		size_t nc = (s->n + k + 1) * 2;
		char *nb = realloc(s->b, nc);
		if (!nb)
			return;
		s->b = nb;
		s->cap = nc;
	}
	memcpy(s->b + s->n, p, k);
	s->n += k;
	s->b[s->n] = 0;
}
static void sbputc(SB *s, char c)        { sbput(s, &c, 1); }
static void sbputs(SB *s, const char *p) { sbput(s, p, strlen(p)); }

/* ---- minimal JSON reader: parses straight into the existing NodeObj  ---- */
/* tree (an object's keys/an array's elements become AppendChild'd          */
/* children, in order; a leaf's text is its Value) - reusing the tree the   */
/* rest of this framework already has, instead of a second data structure.  */

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

static void utf8_put(SB *o, unsigned cp)
{
	if (cp < 0x80) sbputc(o, (char)cp);
	else if (cp < 0x800)
	{
		sbputc(o, (char)(0xC0 | (cp >> 6)));
		sbputc(o, (char)(0x80 | (cp & 0x3F)));
	}
	else if (cp < 0x10000)
	{
		sbputc(o, (char)(0xE0 | (cp >> 12)));
		sbputc(o, (char)(0x80 | ((cp >> 6) & 0x3F)));
		sbputc(o, (char)(0x80 | (cp & 0x3F)));
	}
	else
	{
		sbputc(o, (char)(0xF0 | (cp >> 18)));
		sbputc(o, (char)(0x80 | ((cp >> 12) & 0x3F)));
		sbputc(o, (char)(0x80 | ((cp >> 6) & 0x3F)));
		sbputc(o, (char)(0x80 | (cp & 0x3F)));
	}
}

static void JSON_SkipWS(const char **p)
{
	while (**p && isspace((unsigned char)**p))
		(*p)++;
}

static NodeObj JSON_ParseValue(const char **p);

static NodeObj JSON_ParseString(const char **p)
{
	NodeObj n = NewNode(STRING);
	SB o;
	const char *q = *p + 1;	/* skip opening quote */

	o.b = NULL; o.n = o.cap = 0;
	while (*q && *q != '"')
	{
		if (*q != '\\')
		{
			sbputc(&o, *q++);
			continue;
		}
		q++;
		switch (*q)
		{
		case 'n':  sbputc(&o, '\n'); q++; break;
		case 't':  sbputc(&o, '\t'); q++; break;
		case 'r':  sbputc(&o, '\r'); q++; break;
		case 'b':  sbputc(&o, '\b'); q++; break;
		case 'f':  sbputc(&o, '\f'); q++; break;
		case '/':  sbputc(&o, '/');  q++; break;
		case '"':  sbputc(&o, '"');  q++; break;
		case '\\': sbputc(&o, '\\'); q++; break;
		case 'u':
		{
			unsigned cp = 0;
			int i;
			for (i = 0; i < 4 && q[1]; i++)
				cp = cp * 16 + (unsigned)hexval((unsigned char)*++q);
			q++;
			utf8_put(&o, cp);
			break;
		}
		default:
			if (*q) sbputc(&o, *q++);
		}
	}
	if (*q == '"')
		q++;
	*p = q;
	SetValueStr(n, o.b ? o.b : "");
	if (o.b) free(o.b);
	return n;
}

static NodeObj JSON_ParseLiteral(const char **p)
{
	NodeObj n = NewNode(STRING);
	char buf[64];
	int i = 0;

	while (**p && !strchr(",]}\t\r\n ", **p) && i < (int)sizeof(buf) - 1)
		buf[i++] = *(*p)++;
	buf[i] = 0;
	SetValueStr(n, buf);
	return n;
}

static NodeObj JSON_ParseArray(const char **p)
{
	NodeObj arr = NewNode(STRING);
	int idx = 0;

	(*p)++;	/* skip '[' */
	JSON_SkipWS(p);
	if (**p == ']')
	{
		(*p)++;
		return arr;
	}
	for (;;)
	{
		NodeObj val;
		char nbuf[16];

		JSON_SkipWS(p);
		val = JSON_ParseValue(p);
		if (!val)
			break;
		snprintf(nbuf, sizeof(nbuf), "%d", idx++);
		SetName(val, nbuf);
		AppendChild(arr, val);
		JSON_SkipWS(p);
		if (**p == ',')
		{
			(*p)++;
			continue;
		}
		break;
	}
	JSON_SkipWS(p);
	if (**p == ']')
		(*p)++;
	return arr;
}

static NodeObj JSON_ParseObject(const char **p)
{
	NodeObj obj = NewNode(STRING);

	(*p)++;	/* skip '{' */
	JSON_SkipWS(p);
	if (**p == '}')
	{
		(*p)++;
		return obj;
	}
	for (;;)
	{
		NodeObj keyNode, val;
		char keybuf[160];

		JSON_SkipWS(p);
		if (**p != '"')
			break;	/* malformed - stop rather than loop forever */
		keyNode = JSON_ParseString(p);
		snprintf(keybuf, sizeof(keybuf), "%s", GetValueStr(keyNode) ? GetValueStr(keyNode) : "");
		DelNode(keyNode);
		JSON_SkipWS(p);
		if (**p == ':')
			(*p)++;
		JSON_SkipWS(p);
		val = JSON_ParseValue(p);
		if (!val)
			break;
		SetName(val, keybuf);
		AppendChild(obj, val);
		JSON_SkipWS(p);
		if (**p == ',')
		{
			(*p)++;
			continue;
		}
		break;
	}
	JSON_SkipWS(p);
	if (**p == '}')
		(*p)++;
	return obj;
}

static NodeObj JSON_ParseValue(const char **p)
{
	JSON_SkipWS(p);
	if (**p == '{') return JSON_ParseObject(p);
	if (**p == '[') return JSON_ParseArray(p);
	if (**p == '"') return JSON_ParseString(p);
	if (**p)        return JSON_ParseLiteral(p);
	return NULL;
}

static NodeObj JSON_Parse(const char *text)
{
	const char *p = text;
	return JSON_ParseValue(&p);
}

/* no separate JSON_Free: DelNode (node.c) already recursively frees
   nextSib, props, AND child before freeing the node itself - a second,
   hand-rolled recursive walk on top of that was freeing every child a
   second time DelNode's own recursion had already freed. Just DelNode(root)
   at the call site. */

static NodeObj JSON_Get(NodeObj obj, const char *key)
{
	NodeObj c;

	if (!obj || !key)
		return NULL;
	for (c = GetChild(obj); c; c = GetNextSibling(c))
		if (CmpName(c, (char *)key))
			return c;
	return NULL;
}

static char *JSON_GetStr(NodeObj obj, const char *key)
{
	NodeObj v = JSON_Get(obj, key);
	return v ? GetValueStr(v) : NULL;
}

static void JSON_AppendEscaped(SB *o, const char *s)
{
	sbputc(o, '"');
	for (; s && *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		switch (c)
		{
		case '"':  sbputs(o, "\\\""); break;
		case '\\': sbputs(o, "\\\\"); break;
		case '\n': sbputs(o, "\\n");  break;
		case '\r': sbputs(o, "\\r");  break;
		case '\t': sbputs(o, "\\t");  break;
		default:
			if (c < 0x20)
			{
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				sbputs(o, buf);
			}
			else
				sbputc(o, (char)c);
		}
	}
	sbputc(o, '"');
}

/* ---- a name safe to use as a node/path segment - MCP agent names carry   */
/* slashes, dots, spaces ("/text/edit/append_text/v1.0", "sqlite test")     */

static void MCP_SanitizeName(const char *raw, char *out, int outlen)
{
	int i = 0;

	if (!raw)
	{
		snprintf(out, outlen, "Agent");
		return;
	}
	for (; *raw && i < outlen - 1; raw++)
	{
		char c = *raw;
		out[i++] = (isalnum((unsigned char)c) || c == '_') ? c : '_';
	}
	if (i == 0)
		snprintf(out, outlen, "Agent");
	else
		out[i] = 0;
}

/* mint a name guaranteed free in containerPath - base, then base_1,
   base_2... whichever is first unclaimed. The same generic "just
   create it, let the name dedupe" idiom the engine's own clone naming
   uses (CloneMintName, object.c) - not reachable from here (private to
   that file), so reimplemented locally rather than special-casing a
   check-then-destroy-the-old-one dance for what is really just a name
   collision. RegisterPath (object.c/namespace.c) does a blind overwrite
   on a colliding key with no dedup of its own, so minting up front is
   what actually avoids orphaning the previous instance. */
static void MCP_MintChildName(char *containerPath, char *base, char *out, int outlen)
{
	char testpath[600];
	int n;

	snprintf(out, outlen, "%s", base);
	snprintf(testpath, sizeof(testpath), "%s/%s", containerPath, out);
	if (!ResolvePath(testpath))
		return;
	for (n = 1; n < 10000; n++)
	{
		snprintf(out, outlen, "%s_%d", base, n);
		snprintf(testpath, sizeof(testpath), "%s/%s", containerPath, out);
		if (!ResolvePath(testpath))
			return;
	}
}

/* escape a C string into a Lua single-quoted literal's body (embedded in
   the generated script below) */
static void Lua_AppendEscaped(SB *o, const char *s)
{
	sbputc(o, '\'');
	for (; s && *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '\'' || c == '\\')
		{
			sbputc(o, '\\');
			sbputc(o, (char)c);
		}
		else if (c == '\n')
			sbputs(o, "\\n");
		else
			sbputc(o, (char)c);
	}
	sbputc(o, '\'');
}

/* ================================================================ */
/* MCPSource - discovery only. Executing an agent is each generated   */
/* View's own doing (below); the connector is never asked to dial.    */
/* ================================================================ */

typedef struct SourceData
{
	int      enabled;
	NodeObj  inner;
	TaskObj  timeoutTask;
	int      pending;		/* MCP_IDLE / MCP_LIST */
	char    *rxbuf;
	size_t   rxlen, rxcap;
} SourceData;

/* returns the View's own H, so callers can stack the next one below it */
static int MCPSource_BuildAgentView(NodeObj connector, NodeObj group, char *safe,
									 char *agentName, char *help,
									 char *inputsCsv, char *outputsCsv, int startY);

static void MCPSource_SetNet(NodeObj instance, char *s)
{
	SetPropStr(instance, "NetStatus", s);
}

static void MCPSource_TearDown(SourceData *local)
{
	if (local->timeoutTask)
	{
		DeleteTask(local->timeoutTask);
		local->timeoutTask = NULL;
	}
	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}
	local->pending = MCP_IDLE;
	local->rxlen = 0;
}

static void MCPSource_SendListAgents(NodeObj instance, SourceData *local)
{
	static const char *req = "{\"command\":\"LIST_AGENTS\"}\n";
	NodeObj chunk = NewNode(STRING);

	SetName(chunk, "Data");
	SetValueStrLen(chunk, (char *)req, (int)strlen(req));
	SetPropInt(chunk, "Length", (int)strlen(req));
	DeliverMsg(local->inner, "In", msg_send, chunk);
	MCPSource_SetNet(instance, NS_WAIT);
}

int MCPSource_OnInnerUp(NodeObj instance, MsgId message, NodeObj data)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;

	if (GetValueInt(data))
	{
		if (local->pending == MCP_LIST)
			MCPSource_SendListAgents(instance, local);
	}
	else
	{
		MCPSource_SetNet(instance, "Error: connect failed");
		local->pending = MCP_IDLE;
	}
	return rtrn_handled;
}

int MCPSource_OnInnerRx(NodeObj instance, MsgId message, NodeObj data)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");
	char *str;
	int length;
	char *nl;

	if (!local)
		return rtrn_dropped;

	if (message == msg_eof)
	{
		if (local->pending != MCP_IDLE && local->rxlen == 0)
			MCPSource_SetNet(instance, "Error: connection closed early");
		return rtrn_handled;
	}

	str = GetValueStr(data);
	length = GetPropInt(data, "Length");
	if (!length && str)
		length = (int)strlen(str);
	if (!str || length <= 0)
		return rtrn_handled;

	if (local->rxlen + (size_t)length + 1 > local->rxcap)
	{
		size_t ncap = (local->rxlen + (size_t)length + 1) * 2;
		char *nb = realloc(local->rxbuf, ncap);
		if (!nb)
			return rtrn_handled;
		local->rxbuf = nb;
		local->rxcap = ncap;
	}
	memcpy(local->rxbuf + local->rxlen, str, (size_t)length);
	local->rxlen += (size_t)length;
	local->rxbuf[local->rxlen] = 0;

	nl = memchr(local->rxbuf, '\n', local->rxlen);
	if (!nl)
		return rtrn_handled;	/* one line not fully here yet */

	*nl = 0;

	{
		NodeObj root = JSON_Parse(local->rxbuf);
		char *status = JSON_GetStr(root, "status");

		if (!status || strcmp(status, "SUCCESS") != 0)
		{
			char *msg = JSON_GetStr(root, "message");
			char buf[300];
			snprintf(buf, sizeof(buf), "Error: %s", msg ? msg : "request failed");
			MCPSource_SetNet(instance, buf);
		}
		else
		{
			NodeObj payload = JSON_Get(root, "payload");
			NodeObj agent;
			char *viewNameProp = GetPropStr(instance, "ViewName");
			char viewNameBase[128], viewName[128], groupPath[512];
			NodeObj group, paletteView;
			int count = 0;
			int nextY = 8;	/* walked down the list, bumped by each agent's own H + 2 */

			snprintf(viewNameBase, sizeof(viewNameBase), "%s", (viewNameProp && viewNameProp[0]) ? viewNameProp : "Agents");
			/* every Connect press gets its OWN fresh group - a different
			   main directory each time (viewNameBase, then _1, _2...),
			   never reused/grown - so one press's result set is never
			   mixed with an earlier one's under the same name */
			MCP_MintChildName("/Root/Palette", viewNameBase, viewName, sizeof(viewName));
			snprintf(groupPath, sizeof(groupPath), "/Root/Palette/%s", viewName);

			paletteView = ResolvePath("/Root/Palette");
			if (!paletteView)
				DebugPrint("MCPSource: /Root/Palette did not resolve - cannot build the agent group.", __FILE__, __LINE__, ERROR);
			group = paletteView ? Widget_Create(paletteView, "View", viewName) : NULL;
			if (!group)
				DebugPrint("MCPSource: Widget_Create('View') for the agent group failed.", __FILE__, __LINE__, ERROR);
			else
				SetPropInt(group, "H", 220);

			for (agent = payload ? GetChild(payload) : NULL; agent; agent = GetNextSibling(agent))
			{
				char *name = JSON_GetStr(agent, "name");
				char *help = JSON_GetStr(agent, "help");
				NodeObj inputsArr  = JSON_Get(agent, "inputs");
				NodeObj optArr     = JSON_Get(agent, "optional_inputs");
				NodeObj outputsArr = JSON_Get(agent, "outputs");
				char safe[128];
				char inputsCsv[512], outputsCsv[512];
				NodeObj it;
				int n;

				if (!name || !group)
					continue;
				count++;

				/* the group is freshly minted above, guaranteed empty -
				   no collision possible within it, so plain sanitizing
				   is enough here (unlike the group name itself, which
				   has to dodge every EARLIER press's group) */
				MCP_SanitizeName(name, safe, sizeof(safe));

				inputsCsv[0] = 0;
				for (it = inputsArr ? GetChild(inputsArr) : NULL; it; it = GetNextSibling(it))
				{
					n = (int)strlen(inputsCsv);
					snprintf(inputsCsv + n, sizeof(inputsCsv) - (size_t)n, "%s%s", n ? "," : "", GetValueStr(it));
				}
				for (it = optArr ? GetChild(optArr) : NULL; it; it = GetNextSibling(it))
				{
					n = (int)strlen(inputsCsv);
					snprintf(inputsCsv + n, sizeof(inputsCsv) - (size_t)n, "%s%s", n ? "," : "", GetValueStr(it));
				}
				outputsCsv[0] = 0;
				for (it = outputsArr ? GetChild(outputsArr) : NULL; it; it = GetNextSibling(it))
				{
					n = (int)strlen(outputsCsv);
					snprintf(outputsCsv + n, sizeof(outputsCsv) - (size_t)n, "%s%s", n ? "," : "", GetValueStr(it));
				}

				/* the collapsed row height (one button + a 2px gap), not
				   the agent's own expanded internal height - a closed
				   View is a small icon in this list, not its full panel */
				MCPSource_BuildAgentView(instance, group, safe, name, help ? help : "",
										  inputsCsv, outputsCsv, nextY);
				nextY += 34;
			}

			if (group)
				SetPropInt(group, "H", nextY + 40);

			{
				char buf[64];
				snprintf(buf, sizeof(buf), "Idle - %d agent(s)", count);
				MCPSource_SetNet(instance, buf);
			}
		}
		DelNode(root);
	}

	if (local->timeoutTask)
	{
		DeleteTask(local->timeoutTask);
		local->timeoutTask = NULL;
	}
	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}
	local->pending = MCP_IDLE;
	local->rxlen = 0;
	return rtrn_handled;
}

static int MCPSource_OnTimeout(NodeObj instance, NodeObj taskdata, int reason)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");

	(void) taskdata;
	if (reason == task_deactivate || !local)
		return rtrn_handled;

	if (local->pending != MCP_IDLE)
	{
		MCPSource_SetNet(instance, "Error: timed out");
		MCPSource_TearDown(local);
	}
	return rtrn_handled;
}

int MCPSource_OnConnect(NodeObj instance, MsgId message, NodeObj data)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");
	char *host, *port, *viewName;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local)
		return rtrn_handled;
	if (!local->enabled)
	{
		MCPSource_SetNet(instance, "Ignored - not enabled");
		return rtrn_handled;
	}
	if (local->pending != MCP_IDLE)
	{
		MCPSource_SetNet(instance, "Busy - try again shortly");
		return rtrn_handled;
	}
	host = GetPropStr(instance, "HostName");
	port = GetPropStr(instance, "Port");
	viewName = GetPropStr(instance, "ViewName");
	if (!host || !host[0] || !port || !port[0])
	{
		MCPSource_SetNet(instance, "Error: HostName/Port not set (see Settings)");
		return rtrn_handled;
	}
	if (!viewName || !viewName[0])
	{
		MCPSource_SetNet(instance, "Error: ViewName is empty (see Settings)");
		return rtrn_handled;
	}

	if (local->inner)
	{
		DeleteInstance(local->inner);
		local->inner = NULL;
	}
	local->rxlen = 0;
	local->pending = MCP_LIST;

	local->inner = CreateObject(instance, "TCP");
	if (!local->inner)
	{
		MCPSource_SetNet(instance, "Error: TCP class is not loaded");
		local->pending = MCP_IDLE;
		return rtrn_handled;
	}
	Connect(local->inner, "Out", instance, "InnerRx");
	Connect(local->inner, "Connected", instance, "InnerUp");
	SetOrDeliverProp(local->inner, "RemoteAddr", host);
	SetOrDeliverProp(local->inner, "RemotePort", port);
	ActivateInstance(local->inner);
	MCPSource_SetNet(instance, NS_CONNECT);

	if (local->timeoutTask)
		DeleteTask(local->timeoutTask);
	local->timeoutTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->timeoutTask, MCP_TIMEOUT_MS, (FuncPtr)MCPSource_OnTimeout, msg_send, instance);
	return rtrn_handled;
}

int MCPSource_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	if (!local->enabled)
	{
		MCPSource_TearDown(local);
		MCPSource_SetNet(instance, NS_DISABLED);
	}
	else
		MCPSource_SetNet(instance, NS_IDLE);
	return rtrn_handled;
}

int MCPSource_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");

	(void) message; (void) data;
	if (!local)
		return rtrn_dropped;
	Widget_BuildOnce(instance, MCPSourcePanel);
	MCPSource_SetNet(instance, local->enabled ? NS_IDLE : NS_DISABLED);
	return rtrn_handled;
}

static WidgetItem MCPSourcePanel[] = {
	/* cls          prop          def              panel x    y   w    h  label        [handler] */
	{ "View",     "MCPSource",    "",               0,   0,   0, 300, 210, 0 },
	{ "Help",     "objects/mcpsource/README.md", "", 0,   0,   0,   0,   0, 0 },
	{ "View",     "Settings",     "",               0,  85, 130, 300, 170, 0 },	/* Y matches Help's own H-80 convention (H=210) */

	{ "Checkbox", "Enable",     "1",   0, 200,  12,  8,  8, LABEL_LEFT, (void *)MCPSource_OnEnable },
	{ "TextOut",  "NetStatus",  NS_IDLE, 0, 15,  14, 220, 15, LABEL_LEFT },
	{ "MoButton", "Connect",    "0",   0,  15,  50,  70, 22, LABEL_NONE, (void *)MCPSource_OnConnect },

	{ "Textbox",  "HostName",   "127.0.0.1", 2, 15, 20, 230, 24, LABEL_TOP },
	{ "Textbox",  "Port",       "8081",      2, 15, 54, 100, 24, LABEL_TOP },
	{ "Textbox",  "ViewName",   "MCPAgents", 2, 15, 88, 230, 24, LABEL_TOP },

	{ NULL }
};

int SourceInstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	SourceData *local = malloc(sizeof(SourceData));

	(void) message; (void) data;

	local->enabled = 1;
	local->inner = NULL;
	local->timeoutTask = NULL;
	local->pending = MCP_IDLE;
	local->rxbuf = NULL;
	local->rxlen = 0;
	local->rxcap = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "MCPSource");
	Widget_Init(instance, MCPSourcePanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)MCPSource_Activate);
	Widget_Port(instance, "InnerRx", "", (void *)MCPSource_OnInnerRx);
	Widget_Port(instance, "InnerUp", "", (void *)MCPSource_OnInnerUp);

	InitPosition(instance);
	Widget_MainSize(instance, MCPSourcePanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, MCPSourcePanel);
	return rtrn_handled;
}

int SourceInstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	SourceData *local = (SourceData *)GetPropLong(instance, "local");

	(void) message; (void) data;
	Widget_CancelBuild(instance);
	if (local)
	{
		if (local->timeoutTask) DeleteTask(local->timeoutTask);
		if (local->inner) DeleteInstance(local->inner);
		if (local->rxbuf) free(local->rxbuf);
		free(local);
	}
	return rtrn_handled;
}

/* ================================================================ */
/* one generated agent View: plain Textbox controls for inputs/outputs,   */
/* a Lua "Runner" that reaches them directly by path (sibget/sibset,      */
/* objects/lua/script.c) and hands the flat params off to this View's own */
/* "Exec" port on Submit, and a small handler (below) that does that      */
/* agent's own TCP object call - dial, send EXECUTE_AGENT, read one       */
/* reply, teardown. Host/Port are read back from the connector at that    */
/* moment, never copied in.                                                */
/* ================================================================ */

/* ephemeral runtime state only - identity (AgentName, ConnectorPath) and
   the Runner child are never cached here; they are read live (published
   properties / a path lookup) each time, which is what makes clone and
   move work: this struct is rebuilt fresh by MCPAgent's own InstanceStart
   on every instantiation, including a clone's, but a cached NodeObj
   pointer to the ORIGINAL connector or ORIGINAL Runner would not be. */
/* our own callback base: the script host answers on our "ScriptEvt" port at
   base + SCRIPT_PRINT / SCRIPT_OUT / SCRIPT_ERROR */
#define MCPAGENT_CALLBACK 0x6200

typedef struct AgentData
{
	NodeObj  inner;
	NodeObj  host;		/* the agent's logic - a PRIVATE script handle. Not a
						   child called "Runner": unnamed, unaddressable, never
						   cloned and never serialized. The SOURCE lives on the
						   agent view itself, which IS cloned and serialized, so
						   a clone rebuilds its own host from its own copy. */
	TaskObj  timeoutTask;
	int      pending;
	char    *rxbuf;
	size_t   rxlen, rxcap;
	char    *txbuf;		/* the built request, held until Connected fires */
} AgentData;

static NodeObj MCPAgentClass;

int MCPSource_AgentOnRequest(NodeObj agentView, MsgId message, NodeObj data);

/* every class whose parent is Script, comma-joined - the options a Language
   menu offers. Same question ScriptBox asks; being a language host is a fact
   about the class tree, not a marker anyone has to remember to set. */
static void MCPAgent_HostList(char *out, int outlen)
{
	NodeObj lib, cls;
	int     used, first = 1;
	char   *par, *nm;

	out[0] = '\0';
	for (lib = GetChild(GetRegObjList()); lib; lib = GetNextSibling(lib))
		for (cls = GetChild(lib); cls; cls = GetNextSibling(cls))
		{
			par = GetPropStr(cls, "Parent");
			if (!par || strcmp(par, "Script"))
				continue;
			nm = GetNameStr(cls);
			if (!nm)
				continue;
			used = (int) strlen(out);
			snprintf(out + used, outlen - used, "%s%s", first ? "" : ",", nm);
			first = 0;
		}
}

/* The agent's script host, made on demand from the agent's OWN Source
   property. Lazy deliberately: a clone's MCPAgent_InstanceStart runs before
   its properties are copied, and nothing calls Activate on a clone, so the
   host cannot be built at start - it is built the first time anything needs
   it, by which point Source is there. That is also what makes this survive a
   load: the file carries Source, not a host. */
static NodeObj MCPAgent_Host(NodeObj agentView)
{
	AgentData *ad = (AgentData *)GetPropLong(agentView, "local");
	NodeObj    cls, args, text;
	msgobj     instanceStart;
	char      *lang, *src;

	if (!ad)
		return NULL;
	if (ad->host)
		return ad->host;

	/* No fallback. Guessing the language is guessing what the source IS, and
	   being right only because one generator happens to emit Lua is not being
	   right - it just hides that the agent never said. An agent that does not
	   say is incomplete, and that is the thing worth reporting. */
	lang = GetPropStr(agentView, "Language");
	if (!lang || !lang[0])
	{
		char dbg[300];

		snprintf(dbg, sizeof(dbg),
				 "MCPAgent '%s' has no Language, so nothing knows what to run its"
				 " Source with - not guessing. Regenerate it, or set Language.",
				 GetPropStr(agentView, "AgentName"));
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return NULL;
	}

	cls = FindClass(lang);
	instanceStart = cls ? (msgobj) GetPropLong(cls, "InstanceStart") : NULL;
	if (!instanceStart)
	{
		DebugPrint("MCPAgent: no script host class to run this agent's logic",
				   __FILE__, __LINE__, ERROR);
		return NULL;
	}

	args = NewNode(INTEGER);
	SetPropLong(args, "Owner", (long) agentView);
	SetPropLong(args, "MsgBase", (long) MCPAGENT_CALLBACK);
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	SetPropStr(args, "Port", "ScriptEvt");
	instanceStart(cls, msg_initialize, args);
	DelNode(args);

	ad->host = (NodeObj) GetPropLong(cls, "LastInstance");
	if (!ad->host)
		return NULL;

	src = GetPropStr(agentView, "Source");
	if (src && src[0])
	{
		text = NewNode(STRING);
		SetName(text, "Source");
		SetValueStr(text, src);
		ScriptSetSource(ad->host, text);
		DelNode(text);
		ScriptRun(ad->host);
	}

	return ad->host;
}

/* the Language menu moved: drop the old host so the next use builds one in
   the new language. Without this the dropdown is decorative - the host is
   made once and cached, so whatever it was first is what it stays. */
int MCPAgent_OnLanguage(NodeObj agentView, MsgId message, NodeObj data)
{
	AgentData *ad = (AgentData *)GetPropLong(agentView, "local");
	char      *lang = data ? GetValueStr(data) : NULL;
	char       dbg[200];

	if (!ad || message == msg_eof)
		return rtrn_dropped;

	if (!lang || !lang[0])
		return rtrn_dropped;

	/* STORE IT. A property carrying an OnMsg is a port: SetOrDeliverProp
	   delivers to this handler INSTEAD of writing the value, so unless the
	   handler writes it the property still says whatever it said before -
	   and the rebuild below quietly made a Lua host after the menu said
	   JSScript. SetValueStr, not SetPropStr: the latter would fan out and
	   re-enter this handler. */
	SetValueStr(GetPropNode(agentView, "Language"), lang);

	if (ad->host)
	{
		NodeObj dying = ad->host;

		ad->host = NULL;			/* clear FIRST - nothing of ours may hold it */
		DeleteInstance(dying);
	}

	snprintf(dbg, sizeof(dbg), "MCPAgent language is now '%s' - host dropped, rebuilds on next use",
			 lang);
	DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);

	/* the source is unchanged and is still whatever language it was written
	   in - switching to a host that cannot read it will fail loudly, which is
	   the honest outcome */
	return rtrn_handled;
}

/* Submit pressed: hand it to the agent's own script, which builds the
   request and send()s it back out */
int MCPAgent_OnSubmit(NodeObj agentView, MsgId message, NodeObj data)
{
	NodeObj host = MCPAgent_Host(agentView);

	(void) message;

	if (!host)
		return rtrn_dropped;

	ScriptIn(host, data);
	return rtrn_handled;
}

/* what the agent's script says back: send() drives the request, exactly as
   Connect(Runner, "Out", agentView, "Exec") used to */
int MCPAgent_OnScriptEvt(NodeObj agentView, MsgId message, NodeObj data)
{
	switch (message - MCPAGENT_CALLBACK)
	{
		case SCRIPT_OUT:
			return MCPSource_AgentOnRequest(agentView, msg_send, data);

		case SCRIPT_PRINT:
		case SCRIPT_ERROR:
		{
			char dbg[400];

			snprintf(dbg, sizeof(dbg), "MCPAgent script: %.300s",
					 data ? GetValueStr(data) : "");
			DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
			return rtrn_handled;
		}
	}
	return rtrn_dropped;
}

/* the connector this agent belongs to, resolved fresh from its own
   ConnectorPath property - never a cached pointer, so it survives the
   connector being anywhere it currently is (and simply resolves to
   nothing, safely, if the connector was deleted) */
static NodeObj MCPSource_FindConnector(NodeObj agentView)
{
	char *path = GetPropStr(agentView, "ConnectorPath");

	if (!path || !path[0])
		return NULL;
	return ResolvePath(path);
}

static void MCPSource_AgentTearDown(AgentData *ad)
{
	if (ad->timeoutTask)
	{
		DeleteTask(ad->timeoutTask);
		ad->timeoutTask = NULL;
	}
	if (ad->inner)
	{
		DeleteInstance(ad->inner);
		ad->inner = NULL;
	}
	if (ad->txbuf)
	{
		free(ad->txbuf);
		ad->txbuf = NULL;
	}
	ad->pending = 0;
	ad->rxlen = 0;
}

static int MCPSource_AgentOnTimeout(NodeObj agentView, NodeObj taskdata, int reason)
{
	AgentData *ad = (AgentData *)GetPropLong(agentView, "local");

	(void) taskdata;
	if (reason == task_deactivate || !ad)
		return rtrn_handled;
	if (ad->pending)
		MCPSource_AgentTearDown(ad);
	return rtrn_handled;
}

int MCPSource_AgentOnInnerUp(NodeObj agentView, MsgId message, NodeObj data)
{
	AgentData *ad = (AgentData *)GetPropLong(agentView, "local");

	if (!ad || message == msg_eof)
		return rtrn_handled;
	if (!GetValueInt(data))
	{
		DebugPrint("MCPSource agent connect failed", __FILE__, __LINE__, ERROR);
		MCPSource_AgentTearDown(ad);
		return rtrn_handled;
	}

	/* only now is the socket actually connected - sending any earlier
	   (e.g. right after ActivateInstance, before this fires) hands the
	   bytes to a connection that isn't up yet and they are simply lost,
	   same reason the connector's own LIST_AGENTS send waits here too */
	if (ad->txbuf && ad->inner)
	{
		NodeObj chunk = NewNode(STRING);

		SetName(chunk, "Data");
		SetValueStrLen(chunk, ad->txbuf, (int)strlen(ad->txbuf));
		SetPropInt(chunk, "Length", (int)strlen(ad->txbuf));
		DeliverMsg(ad->inner, "In", msg_send, chunk);
		free(ad->txbuf);
		ad->txbuf = NULL;
	}
	return rtrn_handled;
}

int MCPSource_AgentOnInnerRx(NodeObj agentView, MsgId message, NodeObj data)
{
	AgentData *ad = (AgentData *)GetPropLong(agentView, "local");
	char *str;
	int length;
	char *nl;

	if (!ad)
		return rtrn_dropped;
	if (message == msg_eof)
		return rtrn_handled;

	str = GetValueStr(data);
	length = GetPropInt(data, "Length");
	if (!length && str)
		length = (int)strlen(str);
	if (!str || length <= 0)
		return rtrn_handled;

	if (ad->rxlen + (size_t)length + 1 > ad->rxcap)
	{
		size_t ncap = (ad->rxlen + (size_t)length + 1) * 2;
		char *nb = realloc(ad->rxbuf, ncap);
		if (!nb)
			return rtrn_handled;
		ad->rxbuf = nb;
		ad->rxcap = ncap;
	}
	memcpy(ad->rxbuf + ad->rxlen, str, (size_t)length);
	ad->rxlen += (size_t)length;
	ad->rxbuf[ad->rxlen] = 0;

	nl = memchr(ad->rxbuf, '\n', ad->rxlen);
	if (!nl)
		return rtrn_handled;
	*nl = 0;

	{
		char *agentName = GetPropStr(agentView, "AgentName");
		NodeObj luaInst = MCPAgent_Host(agentView);
		NodeObj root = JSON_Parse(ad->rxbuf);
		char *status = JSON_GetStr(root, "status");
		char dbg[512];

		snprintf(dbg, sizeof(dbg), "MCPSource agent '%s' reply raw: %.300s", agentName ? agentName : "?", ad->rxbuf);
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);

		if (status && strcmp(status, "SUCCESS") == 0 && luaInst)
		{
			NodeObj payload = JSON_Get(root, "payload");
			NodeObj result = JSON_Get(payload, "result");
			SB o;
			NodeObj p;
			int first = 1;
			NodeObj chunk;

			o.b = NULL; o.n = o.cap = 0;
			sbputc(&o, '{');
			for (p = result ? GetChild(result) : NULL; p; p = GetNextSibling(p))
			{
				if (!first) sbputc(&o, ',');
				first = 0;
				JSON_AppendEscaped(&o, GetNameStr(p));
				sbputc(&o, ':');
				JSON_AppendEscaped(&o, GetValueStr(p) ? GetValueStr(p) : "");
			}
			sbputc(&o, '}');

			snprintf(dbg, sizeof(dbg), "MCPSource agent '%s' delivering result to Runner: %s", agentName ? agentName : "?", o.b ? o.b : "{}");
			DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);

			chunk = NewNode(STRING);
			SetName(chunk, "Data");
			SetValueStr(chunk, o.b ? o.b : "{}");
			ScriptIn(luaInst, chunk);
			DelNode(chunk);
			if (o.b) free(o.b);
		}
		else
		{
			snprintf(dbg, sizeof(dbg),
					 "MCPSource agent '%s' NOT delivering: status='%s' luaInst=%p",
					 agentName ? agentName : "?", status ? status : "(none)", (void *)luaInst);
			DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		}
		DelNode(root);
	}

	MCPSource_AgentTearDown(ad);
	return rtrn_handled;
}

/* the request arriving from this agent's own Lua script: one flat JSON
   object of gathered inputs, {"paramName":"value",...} - the agent name
   is already known here, baked in at generation time, so nothing more
   needs to travel with it. */
int MCPSource_AgentOnRequest(NodeObj agentView, MsgId message, NodeObj data)
{
	AgentData *ad = (AgentData *)GetPropLong(agentView, "local");
	char *host, *port, *paramsJson, *agentName;
	NodeObj connector;
	SB o;

	if (!ad || message == msg_eof)
	{
		if (!ad)
			DebugPrint("MCPSource_AgentOnRequest dropped: no 'local' AgentData on this instance", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}
	if (ad->pending)
	{
		DebugPrint("MCPSource agent request ignored: already busy", __FILE__, __LINE__, ERROR);
		return rtrn_handled;	/* one request in flight per agent at a time */
	}

	paramsJson = data ? GetValueStr(data) : NULL;
	if (!paramsJson)
	{
		DebugPrint("MCPSource agent request ignored: no params payload", __FILE__, __LINE__, ERROR);
		return rtrn_handled;
	}

	agentName = GetPropStr(agentView, "AgentName");
	connector = MCPSource_FindConnector(agentView);
	host = connector ? GetPropStr(connector, "HostName") : NULL;
	port = connector ? GetPropStr(connector, "Port") : NULL;
	if (!host || !host[0] || !port || !port[0])
	{
		DebugPrint("MCPSource agent request ignored: connector has no HostName/Port", __FILE__, __LINE__, ERROR);
		return rtrn_handled;	/* connector not configured, moved, or deleted */
	}

	o.b = NULL; o.n = o.cap = 0;
	sbputs(&o, "{\"command\":\"EXECUTE_AGENT\",\"agent\":");
	JSON_AppendEscaped(&o, agentName ? agentName : "");
	sbputs(&o, ",\"params\":");
	sbputs(&o, paramsJson);
	sbputs(&o, "}\n");

	{
		char dbg[512];
		snprintf(dbg, sizeof(dbg), "MCPSource agent '%s' sending: %.300s", agentName ? agentName : "?", o.b);
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}

	ad->inner = CreateObject(agentView, "TCP");
	if (!ad->inner)
	{
		DebugPrint("MCPSource agent request failed: CreateObject('TCP') returned NULL", __FILE__, __LINE__, ERROR);
		if (o.b) free(o.b);
		return rtrn_handled;
	}
	ad->rxlen = 0;
	ad->pending = 1;
	ad->txbuf = o.b;	/* held - sent from AgentOnInnerUp once Connected fires */
	Connect(ad->inner, "Out", agentView, "InnerRx");
	Connect(ad->inner, "Connected", agentView, "InnerUp");
	SetOrDeliverProp(ad->inner, "RemoteAddr", host);
	SetOrDeliverProp(ad->inner, "RemotePort", port);
	ActivateInstance(ad->inner);

	if (ad->timeoutTask)
		DeleteTask(ad->timeoutTask);
	ad->timeoutTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(ad->timeoutTask, MCP_TIMEOUT_MS, (FuncPtr)MCPSource_AgentOnTimeout, msg_send, agentView);
	return rtrn_handled;
}

/* the generated Lua source: on Submit gathers its own inputs and send()s
   them as one flat JSON object to this View's own Exec port (wired
   below); on a reply (any message starting with "{") pulls its own
   output names back out and writes them. A tiny string-match extractor
   stands in for a JSON parser here - the reply shape is always a flat
   object of known field names, never nested, so a general parser would
   be pure overhead inside the interpreter that runs this. */
static char *MCP_BuildAgentSource(char *inputsCsv, char *outputsCsv)
{
	SB o;
	char copy[512], *tok, *end;
	int first;

	o.b = NULL; o.n = o.cap = 0;

	sbputs(&o, "local inputs = {");
	snprintf(copy, sizeof(copy), "%s", inputsCsv ? inputsCsv : "");
	tok = copy; first = 1;
	while (tok && *tok)
	{
		end = strchr(tok, ',');
		if (end) *end = 0;
		if (*tok)
		{
			if (!first) sbputc(&o, ',');
			first = 0;
			Lua_AppendEscaped(&o, tok);
		}
		tok = end ? end + 1 : NULL;
	}
	sbputs(&o, "}\nlocal outputs = {");
	snprintf(copy, sizeof(copy), "%s", outputsCsv ? outputsCsv : "");
	tok = copy; first = 1;
	while (tok && *tok)
	{
		end = strchr(tok, ',');
		if (end) *end = 0;
		if (*tok)
		{
			if (!first) sbputc(&o, ',');
			first = 0;
			Lua_AppendEscaped(&o, tok);
		}
		tok = end ? end + 1 : NULL;
	}
	sbputs(&o, "}\n");

	sbputs(&o,
"local function extract(json, name)\n"
"  local s = json:find('\"' .. name .. '\":\"', 1, true)\n"
"  if not s then return '' end\n"
"  local rest = json:sub(s + #name + 4)\n"
"  local v = rest:match('^([^\"]*)\"')\n"
"  return v or ''\n"
"end\n"
"local function esc(s)\n"
"  s = tostring(s or '')\n"
"  s = s:gsub('\\\\', '\\\\\\\\')\n"
"  s = s:gsub('\"', '\\\\\"')\n"
"  s = s:gsub('\\n', '\\\\n')\n"
"  return s\n"
"end\n"
"oninput(function(value, kind)\n"
"  log('oninput fired: kind=' .. tostring(kind) .. ' value=' .. tostring(value))\n"
"  if kind == 'eof' then return end\n"
"  if value:sub(1,1) == '{' then\n"
"    for _, name in ipairs(outputs) do\n"
"      sibset(name, extract(value, name))\n"
"    end\n"
"    return\n"
"  end\n"
"  if value ~= '1' then return end\n"
"  local parts = {}\n"
"  for _, name in ipairs(inputs) do\n"
"    table.insert(parts, '\"' .. name .. '\":\"' .. esc(sibget(name)) .. '\"')\n"
"  end\n"
"  send('{' .. table.concat(parts, ',') .. '}')\n"
"end)\n");

	return o.b ? o.b : strdup("");
}

/* an accumulator-style agent can list the same name in both its inputs
   and outputs (seed it, each run overwrites it with the new value) - one
   row serves both roles; a second row under the same name would collide
   on the same registered path with the first (Widget_Create names by
   Container+Name) and silently clobber it. */
static int CsvContains(char *csv, char *name)
{
	char copy[512], *tok, *end;

	if (!csv || !csv[0] || !name)
		return 0;
	snprintf(copy, sizeof(copy), "%s", csv);
	tok = copy;
	while (tok && *tok)
	{
		end = strchr(tok, ',');
		if (end) *end = 0;
		if (!strcmp(tok, name))
			return 1;
		tok = end ? end + 1 : NULL;
	}
	return 0;
}

/* just the control - no wiring onto luaInst at all. The script reaches
   this control directly by path (sibget/sibset, objects/lua/script.c),
   the same "reach a sibling by path" mechanism the whole framework
   already uses - not a Connect-based mirror onto the script's own
   properties. That mirror needed the sink property to already exist
   (Connect's own rule) and needed a SECOND wire for anything that was
   both an input and an output (an accumulator); sibget/sibset need
   neither - one real control, read or written directly, survives clone
   for free since the control itself is an ordinary cloned instance. */
static void MCPSource_BuildRow(NodeObj agentView, char *name, int y)
{
	NodeObj ctl = Widget_Create(agentView, "Textbox", name);

	if (!ctl)
		return;
	SetPropInt(ctl, "X", 10);
	SetPropInt(ctl, "Y", y);
	SetPropInt(ctl, "W", 220);
	SetPropInt(ctl, "H", 48);
	SetPropStr(ctl, "Label", name);
	SetPropStr(ctl, "LabelPos", "top");
}

static int MCPSource_BuildAgentView(NodeObj connector, NodeObj group, char *safe,
									 char *agentName, char *help,
									 char *inputsCsv, char *outputsCsv, int startY)
{
	NodeObj agentView, helpCtl, submitCtl;
	char copy[512], *tok, *end;
	char *source;
	char connectorPath[300];
	int y, n;

	agentView = Widget_Create(group, "MCPAgent", safe);
	if (!agentView)
	{
		char dbg[300];
		snprintf(dbg, sizeof(dbg), "MCPSource: Widget_Create('MCPAgent', '%s') failed building an agent widget.", safe);
		DebugPrint(dbg, __FILE__, __LINE__, ERROR);
		return 0;
	}
	SetPropInt(agentView, "W", 320);
	SetPropInt(agentView, "Y", startY);
	SetPropStr(agentView, "AgentName", agentName ? agentName : "");
	if (PathOfInstance(connector, connectorPath, sizeof(connectorPath)))
		SetPropStr(agentView, "ConnectorPath", connectorPath);

	helpCtl = Widget_Create(agentView, "TextOut", "Help");
	if (helpCtl)
	{
		SetPropInt(helpCtl, "X", 10);
		SetPropInt(helpCtl, "Y", 8);
		SetPropInt(helpCtl, "W", 260);
		SetPropInt(helpCtl, "H", 30);
		SetPropStr(helpCtl, "Value", help ? help : "");
	}

	/* No "Runner" child any more. The agent's logic is its own Source
	   property - an ordinary data property on the agent view, so it clones
	   and serializes with the view, and the private host is rebuilt from it
	   on demand. That also settles two old warts: there is no stray box for
	   a Lua with no X/Y, and _Hidden no longer has to mean two things at
	   once ("do not render" versus "do not clone"). */

	y = 50;
	snprintf(copy, sizeof(copy), "%s", inputsCsv ? inputsCsv : "");
	tok = copy;
	while (tok && *tok)
	{
		end = strchr(tok, ',');
		if (end) *end = 0;
		if (*tok)
		{
			MCPSource_BuildRow(agentView, tok, y);
			y += 58;
		}
		tok = end ? end + 1 : NULL;
	}

	submitCtl = Widget_Create(agentView, "MoButton", "Submit");
	if (submitCtl)
	{
		SetPropInt(submitCtl, "X", 10);
		SetPropInt(submitCtl, "Y", y);
		SetPropInt(submitCtl, "W", 60);
		SetPropInt(submitCtl, "H", 20);
		SetPropStr(submitCtl, "Label", "Submit");
		/* the button reaches the AGENT, which forwards into its private host -
		   there is no host property to wire to, and the agent is the thing
		   that survives a clone anyway */
		Connect(submitCtl, "Value", agentView, "Submit");
	}
	y += 20 + 30;

	snprintf(copy, sizeof(copy), "%s", outputsCsv ? outputsCsv : "");
	tok = copy;
	while (tok && *tok)
	{
		end = strchr(tok, ',');
		if (end) *end = 0;
		if (*tok && !CsvContains(inputsCsv, tok))
		{
			MCPSource_BuildRow(agentView, tok, y);
			y += 58;
		}
		tok = end ? end + 1 : NULL;
	}

	n = y + 20 + 40;	/* +40 beyond the estimate, same as every panel here */
	SetPropInt(agentView, "H", n);

	/* Exec/InnerRx/InnerUp and a fresh AgentData are set up by MCPAgent's
	   own InstanceStart (this class's class-level function, called by
	   CreateObject above) - not here, so a clone gets its own too. */

	source = MCP_BuildAgentSource(inputsCsv, outputsCsv);
	SetPropStr(agentView, "Source", source);
	free(source);

	/* and WHAT IT IS, said at the one place that knows - MCP_BuildAgentSource
	   emits Lua. On the instance, so a clone and a saved flow both keep it and
	   nothing downstream has to assume anything. */
	SetPropStr(agentView, "Language", "Lua");

	/* ...and make it CHANGEABLE, or stating it just locks the agent to one
	   language forever. Three ordinary properties do the whole job:
	     Language      the choice, with an OnMsg that drops the old host
	     LanguageList  the options - <prop>List is the existing convention a
	                   menu control reads its Items from (Widget_Ctl)
	     Widget        on the Language property itself, saying it presents as
	                   a menu. The class Interface is where presentation
	                   normally comes from, and Language is not published -
	                   so the property carries its own. */
	{
		/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
		NodeObj port = GetPropNode(agentView, "Language");
		char    hosts[300];

		if (port)
		{
			SetPropLong(port, "OnMsg", (long)MCPAgent_OnLanguage);
			SetPropInt(port, "Widget", PROP_MENU);
		}

		MCPAgent_HostList(hosts, sizeof(hosts));
		SetPropStr(agentView, "LanguageList", hosts);

		port = GetPropNode(agentView, "Source");
		if (port)
			SetPropInt(port, "Widget", PROP_TEXTBOX);
	}

	/* build the host now that Source is set - and from here on it is rebuilt
	   the same way on any clone or load, with no wire to re-make: the script's
	   send() arrives on ScriptEvt as SCRIPT_OUT. */
	MCPAgent_Host(agentView);

	{
		char verify[300], dbg[400];

		if (!PathOfInstance(agentView, verify, sizeof(verify)))
			DebugPrint("MCPSource: agent widget built but its own path does not verify - it will not be resolvable.", __FILE__, __LINE__, ERROR);
		else
		{
			snprintf(dbg, sizeof(dbg), "MCPSource: agent widget registered at %s", verify);
			DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
		}
	}

	return n;
}

/* MCPAgent: the class every generated agent View is really registered
   under (not the plain "View" class) - this is the "intercept
   InstanceStart" fix for clone/move. CloneObject (object.c) calls
   GetParent(source)'s own "InstanceStart" for a fresh instance, then
   copies that class's PUBLISHED "data" properties on top - so a class
   with its own InstanceStart gets its ports/local state rebuilt on
   every clone, and AgentName/ConnectorPath (real published properties,
   not raw pointers cached in a struct) get carried over automatically
   by that same copy step. The frontend has no special case for this:
   "a widget is just a View" (web/app.js) - any class not literally
   "View"/"Alias"/a leaf control renders through the identical
   registerView() path, so this renders exactly as it did as a plain
   View, panel and all. */
static int MCPAgent_InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	AgentData *ad = malloc(sizeof(AgentData));

	(void) message; (void) data;

	ad->inner = NULL;
	ad->host = NULL;
	ad->timeoutTask = NULL;
	ad->pending = 0;
	ad->rxbuf = NULL;
	ad->rxlen = 0;
	ad->rxcap = 0;
	ad->txbuf = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "MCPAgent");
	InitPosition(instance);
	SetPropLong(instance, "local", (long)ad);
	Widget_Port(instance, "Exec", "", (void *)MCPSource_AgentOnRequest);
	Widget_Port(instance, "Submit", "", (void *)MCPAgent_OnSubmit);
	Widget_Port(instance, "ScriptEvt", "", (void *)MCPAgent_OnScriptEvt);
	Widget_Port(instance, "InnerRx", "", (void *)MCPSource_AgentOnInnerRx);
	Widget_Port(instance, "InnerUp", "", (void *)MCPSource_AgentOnInnerUp);

	RegisterInstance(class, instance);

	{
		char dbg[200];
		snprintf(dbg, sizeof(dbg), "MCPAgent_InstanceStart ran, fresh AgentData=%p (fires on every build AND every clone)", (void *)ad);
		DebugPrint(dbg, __FILE__, __LINE__, PROG_FLOW);
	}
	return rtrn_handled;
}

static int MCPAgent_InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	AgentData *ad = (AgentData *)GetPropLong(instance, "local");

	(void) message; (void) data;
	if (ad)
	{
		if (ad->timeoutTask) DeleteTask(ad->timeoutTask);
		if (ad->inner) DeleteInstance(ad->inner);
		/* the script host is ours alone - unnamed and unregistered, so no
		   subtree walk knows about it and nothing else can be holding it */
		if (ad->host)
		{
			NodeObj dying = ad->host;

			ad->host = NULL;
			DeleteInstance(dying);
		}
		if (ad->rxbuf) free(ad->rxbuf);
		if (ad->txbuf) free(ad->txbuf);
		free(ad);
	}
	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj sourceClass = NewNode(INTEGER);
	NodeObj agentClass = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(sourceClass, "MCPSource");
	SetPropLong(sourceClass, "InstanceStart", (long)SourceInstanceStart);
	SetPropLong(sourceClass, "InstanceEnd", (long)SourceInstanceEnd);
	SourceClass = RegisterClass(library, sourceClass);

	SetClassVersion(SourceClass, "1", "0");
	SetClassParent(SourceClass, "Widget");
	PublishPosition(SourceClass);
	Widget_Publish(SourceClass, MCPSourcePanel);
	PublishProp(SourceClass, "HostName", PROP_TEXTBOX, "127.0.0.1");
	PublishProp(SourceClass, "Port", PROP_TEXTBOX, "8081");
	PublishProp(SourceClass, "ViewName", PROP_TEXTBOX, "MCPAgents");

	SetName(agentClass, "MCPAgent");
	SetPropLong(agentClass, "InstanceStart", (long)MCPAgent_InstanceStart);
	SetPropLong(agentClass, "InstanceEnd", (long)MCPAgent_InstanceEnd);
	MCPAgentClass = RegisterClass(library, agentClass);

	SetClassVersion(MCPAgentClass, "1", "0");
	SetClassParent(MCPAgentClass, "Widget");
	PublishPosition(MCPAgentClass);
	PublishProp(MCPAgentClass, "AgentName", PROP_TEXTBOX, "");
	PublishProp(MCPAgentClass, "ConnectorPath", PROP_TEXTBOX, "");


	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;
	UnRegisterClass(library, SourceClass);
	SourceClass = NULL;
	UnRegisterClass(library, MCPAgentClass);
	MCPAgentClass = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "MCPSource");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "7b6f0a3c-4d5e-4a9a-9c1a-4a2e5f8d9c11");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	/* one file, two classes (MCPSource and the MCPAgent view it generates),
	   so the dependency list is the file's. TCP and Lua are created in code,
	   not from a layout table. */
	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
	AddDependency(temp, "widget.object", "Widget", "1", "0");
	AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
	AddDependency(temp, "mobutton.object", "MoButton", "1", "0");
	AddDependency(temp, "textbox.object", "Textbox", "1", "0");
	AddDependency(temp, "textout.object", "TextOut", "1", "0");
	AddDependency(temp, "view.object", "View", "1", "0");
	AddDependency(temp, "tcpshim.object", "TCP", "1", "0");
	AddDependency(temp, "lua.object", "Lua", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
