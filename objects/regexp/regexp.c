
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/* RegExp: line-oriented POSIX-ERE match/replace. Each line of Input is tested
   against Expression; Action decides what reaches Output. Replace expands
   $0..$9 (whole match and capture groups) from Substitution. */

typedef struct InstanceData
{
	int     enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem RegExpPanel[];

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
	Widget_BuildOnce(instance, RegExpPanel);
	RegExp_Recompute(instance);
	return rtrn_handled;
}

/* ---- lifecycle ---- */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->enabled = 1;

	instance = NewNode(INTEGER);
	SetName(instance, "RegExp");

	/* every control's value + handler from the table (Input/Expression/
	   Substitution/Action/CaseSensitive/Enable carry a handler; Output/Error
	   are plain data) */
	Widget_Init(instance, RegExpPanel);

	/* the Action menu's backing list and the lifecycle state - no control */
	SetPropStr(instance, "ActionList", "Match,Reject,Replace,Replace Line");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)RegExp_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, RegExpPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, RegExpPanel);

	return rtrn_handled;
}

/* The whole widget in one table: main view, Help, and every control (value +,
   for the ports, handler). ActionList and State are published apart. */
static WidgetItem RegExpPanel[] = {
	/* cls        prop            def      panel   x    y    w    h  label       [handler] */
	{ "View",     "RegExp", "",            0,   0,   0, 490, 460, 0 },			/* 0: main */
	{ "Help",     "objects/regexp/README.md", "", 0, 0, 0, 0, 0, 0 },			/* 1: help */

	{ "Checkbox", "Enable",        "1",     0, 435,  12,   9,   9, LABEL_LEFT, (void *)RegExp_OnEnable },
	{ "Textbox",  "Expression",    "",      0,  15,  38, 460,  22, LABEL_NONE, (void *)RegExp_OnExpression },
	{ "Textbox",  "Substitution",  "",      0,  15,  86, 460,  22, LABEL_NONE, (void *)RegExp_OnSubstitution },
	{ "Dropdown", "Action",        "Match", 0,  15, 120, 150,  20, LABEL_NONE, (void *)RegExp_OnAction },
	{ "Checkbox", "CaseSensitive", "1",     0, 185, 122,   9,   9, LABEL_NONE, (void *)RegExp_OnCaseSensitive },
	{ "Textbox",  "Input",         "",      0,  15, 160, 215, 165, LABEL_NONE, (void *)RegExp_OnInput },
	{ "Textbox",  "Output",        "",      0, 240, 160, 215, 165, LABEL_NONE },
	{ "TextOut",  "Error",         "",      0,  15, 332, 440,  16, LABEL_NONE },

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

	SetName(class, "RegExp");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (widget type from each control's class) */
	Widget_Publish(ClassSelf, RegExpPanel);

	/* the Action menu's backing list and the lifecycle state - no control */
	PublishProp(ClassSelf, "ActionList", "data", PROP_NULL, "Match,Reject,Replace,Replace Line");
	PublishProp(ClassSelf, "State",      "data", PROP_LED, "1");

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
