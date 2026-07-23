
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/* RegExp: line-oriented POSIX-ERE match/replace. Each line of Input is tested
   against Expression; Action decides what reaches Output. Replace expands
   $0..$9 (whole match and capture groups) from Substitution. */

typedef struct InstanceData
{
	int     enabled;
	int     panelBuilt;
	TaskObj buildTask;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void RegExp_BuildPanel(NodeObj instance);
static int  RegExp_BuildTask(NodeObj instance, NodeObj data, int msgid);

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("RegExp handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- growable string ---- */

typedef struct { char *b; size_t n, cap; } SB;

static void sbput(SB *s, const char *p, size_t k)
{
	if (s->n + k + 1 > s->cap)
	{
		size_t nc = (s->n + k + 1) * 2;
		char  *nb = realloc(s->b, nc);
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

/* ---- match / replace ---- */

/* copy sub into out, expanding $0..$9 to the matched group text ($$ -> $) */
static void RegExp_Expand(SB *out, const char *sub, const char *base,
						  regmatch_t *m, int nsub)
{
	for (; *sub; sub++)
	{
		if (*sub == '$' && sub[1] == '$')
		{
			sbputc(out, '$');
			sub++;
		}
		else if (*sub == '$' && sub[1] >= '0' && sub[1] <= '9')
		{
			int g = sub[1] - '0';
			sub++;
			if (g <= nsub && m[g].rm_so >= 0)
				sbput(out, base + m[g].rm_so, m[g].rm_eo - m[g].rm_so);
		}
		else
			sbputc(out, *sub);
	}
}

/* replace every match in line with the expanded substitution */
static void RegExp_ReplaceAll(SB *out, regex_t *re, const char *line,
							  const char *sub, int nsub)
{
	const char *p = line;
	int         flags = 0;
	regmatch_t  m[10];

	for (;;)
	{
		if (regexec(re, p, 10, m, flags) != 0)
		{
			sbputs(out, p);
			return;
		}
		sbput(out, p, m[0].rm_so);				/* text before the match */
		RegExp_Expand(out, sub, p, m, nsub);
		p += m[0].rm_eo;
		if (m[0].rm_so == m[0].rm_eo)			/* zero-width: consume one char */
		{
			if (!*p)
				return;
			sbputc(out, *p);
			p++;
		}
		if (!*p)
			return;
		flags = REG_NOTBOL;
	}
}

/* the whole transform: compile Expression, run each Input line through it */
static void RegExp_Recompute(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *input, *expr, *sub, *action;
	int   cs, cflags, rc, rep, repline, reject, first = 1;
	regex_t re;
	SB out;
	const char *p;

	if (!local || !local->enabled)
		return;

	input  = GetPropStr(instance, "Input");
	expr   = GetPropStr(instance, "Expression");
	sub    = GetPropStr(instance, "Substitution");
	action = GetPropStr(instance, "Action");
	cs     = GetPropInt(instance, "CaseSensitive");
	if (!input)  input  = "";
	if (!sub)    sub    = "";
	if (!action) action = "Match";

	if (!expr || !expr[0])					/* no pattern: pass through */
	{
		SetPropStr(instance, "Error", "");
		SetPropStr(instance, "Output", input);
		return;
	}

	cflags = REG_EXTENDED | (cs ? 0 : REG_ICASE);
	rc = regcomp(&re, expr, cflags);
	if (rc)
	{
		char err[128];
		regerror(rc, &re, err, sizeof(err));
		regfree(&re);
		SetPropStr(instance, "Error", err);	/* bad pattern: leave Output be */
		return;
	}
	SetPropStr(instance, "Error", "");

	rep     = !strcmp(action, "Replace");
	repline = !strcmp(action, "Replace Line");
	reject  = !strcmp(action, "Reject");

	out.b = NULL;
	out.n = out.cap = 0;
	p = input;

	while (*p)
	{
		const char *nl = strchr(p, '\n');
		size_t      L  = nl ? (size_t)(nl - p) : strlen(p);
		char       *line;
		regmatch_t  m[10];
		int         matched;

		if (L && p[L - 1] == '\r')			/* strip the CR of a CRLF */
			L--;
		line = malloc(L + 1);
		memcpy(line, p, L);
		line[L] = 0;

		matched = (regexec(&re, line, 10, m, 0) == 0);

		if (rep || repline)					/* every line reaches Output */
		{
			if (!first)
				sbputc(&out, '\n');
			first = 0;
			if (!matched)
				sbputs(&out, line);
			else if (repline)
				RegExp_Expand(&out, sub, line, m, (int)re.re_nsub);
			else
				RegExp_ReplaceAll(&out, &re, line, sub, (int)re.re_nsub);
		}
		else								/* filter: Match keeps hits, Reject misses */
		{
			if (reject ? !matched : matched)
			{
				if (!first)
					sbputc(&out, '\n');
				first = 0;
				sbputs(&out, line);
			}
		}

		free(line);
		if (!nl)
			break;
		p = nl + 1;
	}

	regfree(&re);
	SetPropStr(instance, "Output", out.b ? out.b : "");
	free(out.b);
}

/* ---- handlers ---- */

int RegExp_OnInput(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "Input"), GetValueStr(data));
	RegExp_Recompute(instance);
	return rtrn_handled;
}

int RegExp_OnExpression(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "Expression"), GetValueStr(data));
	RegExp_Recompute(instance);
	return rtrn_handled;
}

int RegExp_OnSubstitution(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "Substitution"), GetValueStr(data));
	RegExp_Recompute(instance);
	return rtrn_handled;
}

int RegExp_OnAction(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "Action"), GetValueStr(data));
	RegExp_Recompute(instance);
	return rtrn_handled;
}

int RegExp_OnCaseSensitive(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "CaseSensitive"), GetValueInt(data) ? "1" : "0");
	RegExp_Recompute(instance);
	return rtrn_handled;
}

int RegExp_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;
	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	RegExp_Recompute(instance);
	return rtrn_handled;
}

int RegExp_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		RegExp_BuildPanel(instance);
	}
	RegExp_Recompute(instance);
	return rtrn_handled;
}

/* ---- lifecycle ---- */

static void RegExp_Port(NodeObj instance, char *name, char *initial, void *handler)
{
	NodeObj port;

	SetPropStr(instance, name, initial);
	port = GetPropNode(instance, name);
	SetPropLong(port, "OnMsg", (long)handler);
}

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->enabled = 1;
	local->panelBuilt = 0;
	local->buildTask = NULL;

	instance = NewNode(INTEGER);
	SetName(instance, "RegExp");

	SetPropStr(instance, "Output", "");
	SetPropStr(instance, "Error", "");
	SetPropStr(instance, "ActionList", "Match,Reject,Replace,Replace Line");

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)RegExp_Activate);

	RegExp_Port(instance, "Input",         "",      (void *)RegExp_OnInput);
	RegExp_Port(instance, "Expression",    "",      (void *)RegExp_OnExpression);
	RegExp_Port(instance, "Substitution",  "",      (void *)RegExp_OnSubstitution);
	RegExp_Port(instance, "Action",        "Match", (void *)RegExp_OnAction);
	RegExp_Port(instance, "CaseSensitive", "1",     (void *)RegExp_OnCaseSensitive);
	RegExp_Port(instance, "Enable",        "1",     (void *)RegExp_OnEnable);

	InitPosition(instance);

	/* set here, before any client subscribes */
	SetPropInt(instance, "W", 490);
	SetPropInt(instance, "H", 460);

	RegisterInstance(class, instance);

	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)RegExp_BuildTask, msg_send, instance);

	return rtrn_handled;
}

static void RegExp_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

static char *RegExp_ReadFile(char *path)
{
	FILE *f = fopen(path, "rb");
	long  n;
	char *buf;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0)
	{
		fclose(f);
		return NULL;
	}
	buf = malloc(n + 1);
	if (!buf)
	{
		fclose(f);
		return NULL;
	}
	n = (long)fread(buf, 1, n, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void RegExp_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
					   int x, int y, int w, int h, int rows, int cols)
{
	char cpath[256], path[300];
	NodeObj c = CreateObject(container, cls);
	if (!c)
		return;

	if (PathOfInstance(container, cpath, sizeof(cpath)))
	{
		SetPropStr(c, "Name", prop && prop[0] ? prop : cls);
		snprintf(path, sizeof(path), "%s/%s", cpath, prop && prop[0] ? prop : cls);
		RegisterPath(path, c);
	}

	SetPropInt(c, "X", x);
	SetPropInt(c, "Y", y);
	SetPropInt(c, "W", w);
	SetPropInt(c, "H", h);
	if (prop && prop[0])
		SetPropStr(c, "Label", prop);

	if (strcmp(cls, "Textbox") == 0 && rows > 0 && cols > 0)
	{
		SetPropInt(c, "Rows", rows);
		SetPropInt(c, "Cols", cols);
	}

	if (strcmp(cls, "MoButton") == 0)
		Connect(c, "Out", target, prop);
	else if (strcmp(cls, "Button") == 0)
		Connect(c, "Out", target, "Activate");
	else if (strcmp(cls, "Markdown") == 0)
		;							/* Help box loads its README on open */
	else if (strcmp(cls, "LED") == 0 || strcmp(cls, "TextOut") == 0
			 || strcmp(cls, "Label") == 0)
		RegExp_Reflect(target, prop, c, "Value");
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		Connect(c, "Value", target, prop);
		RegExp_Reflect(target, listprop, c, "Items");
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop));
	}
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);
		RegExp_Reflect(target, prop, c, "In");
	}
}

static NodeObj RegExp_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
{
	char ppath[256], path[300];
	NodeObj v = CreateObject(panel, "View");
	if (!v)
		return NULL;
	SetPropStr(v, "Name", name);
	if (PathOfInstance(panel, ppath, sizeof(ppath)))
	{
		snprintf(path, sizeof(path), "%s/%s", ppath, name);
		RegisterPath(path, v);
	}
	SetPropInt(v, "X", x);
	SetPropInt(v, "Y", y);
	SetPropInt(v, "W", w);
	SetPropInt(v, "H", h);
	return v;
}

int RegExp_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
{
	char vpath[256], mpath[320];
	NodeObj box;
	char *md;

	if (message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!PathOfInstance(view, vpath, sizeof(vpath)))
		return rtrn_handled;
	snprintf(mpath, sizeof(mpath), "%s/HelpText", vpath);
	box = ResolvePath(mpath);
	if (!box)
		return rtrn_handled;

	md = RegExp_ReadFile("objects/regexp/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);
	return rtrn_handled;
}

/* panel 0 = the widget's view, panel 1 = Help */
typedef struct { char *cls, *prop; int x, y, w, h, panel, rows, cols; } RXCtl;

static RXCtl RegExpPanel[] = {
	{ "Checkbox", "Enable",         435,  12,   9,   9, 0,  0,  0 },
	{ "Textbox",  "Expression",      15,  38, 440,  22, 0,  1, 62 },
	{ "Textbox",  "Substitution",    15,  86, 440,  22, 0,  1, 62 },
	{ "Dropdown", "Action",          15, 120, 150,  20, 0,  0,  0 },
	{ "Checkbox", "CaseSensitive",  185, 122,   9,   9, 0,  0,  0 },
	{ "Textbox",  "Input",           15, 160, 215, 165, 0, 11, 28 },
	{ "Textbox",  "Output",         240, 160, 215, 165, 0, 11, 28 },
	{ "TextOut",  "Error",           15, 332, 440,  16, 0,  0,  0 },

	{ "Markdown", "HelpText",        10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1,  0,  0 },

	{ NULL, NULL, 0, 0, 0, 0, 0, 0, 0 }
};

static void RegExp_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = RegExp_SubPanel(instance, "Help", 15, 352, HELP_W, HELP_H);

	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)RegExp_OnHelpOpen);
	}

	for (i = 0; RegExpPanel[i].cls; i++)
	{
		RXCtl *t = &RegExpPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			RegExp_Ctl(container, instance, t->cls, t->prop,
					   t->x, t->y, t->w, t->h, t->rows, t->cols);
	}
}

static int RegExp_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		RegExp_BuildPanel(instance);
		RegExp_Activate(instance, msg_initialize, NULL);
	}
	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->buildTask)
			RemoveTask(local->buildTask);
		free(local);
	}
	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);
	NodeObj entry;

	SetName(class, "RegExp");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Enable", "data", PROP_CHECKBOX, "1");

	entry = PublishProp(ClassSelf, "Expression", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 62);
	entry = PublishProp(ClassSelf, "Substitution", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 62);

	PublishProp(ClassSelf, "Action", "data", PROP_MENU, "Match");
	PublishProp(ClassSelf, "ActionList", "data", PROP_NULL, "Match,Reject,Replace,Replace Line");
	PublishProp(ClassSelf, "CaseSensitive", "data", PROP_CHECKBOX, "1");

	entry = PublishProp(ClassSelf, "Input", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 11);
	SetPropInt(entry, "Cols", 28);
	entry = PublishProp(ClassSelf, "Output", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 11);
	SetPropInt(entry, "Cols", 28);

	PublishProp(ClassSelf, "Error", "data", PROP_TEXTBOX, "");
	PublishProp(ClassSelf, "State", "data", PROP_LED, "1");

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

	SetName(temp, "RegExp");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "f289c05b-b667-4238-8a3b-faacef4fde51");
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
