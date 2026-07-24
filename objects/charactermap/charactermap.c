
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/* CharacterMap: rewrite Input into Output a byte at a time through a 256-entry
   table built from Map. Each Map line is "from to" (remap) or "from" (delete);
   a token is a literal char, decimal, 0x/%/\x hex, \0 octal, or a \-escape. */

typedef struct InstanceData
{
	int     enabled;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem CharMapPanel[];

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("CharacterMap handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- token parsing ---- */

/* parse len digits of s in base (8/10/16); -1 on any non-digit for the base */
static int CharMap_Base(const char *s, int len, int base)
{
	int i, result = 0;

	for (i = 0; i < len; i++)
	{
		int c = (unsigned char)s[i], d;

		if (c >= '0' && c <= '9')      d = c - '0';
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else return -1;

		if (d >= base)
			return -1;
		result = result * base + d;
	}
	return result;
}

/* one Map token -> its byte value, or -1 if unparseable */
static int CharMap_Token(const char *s, int len)
{
	char buf[5];

	if (len <= 0)
		return -1;
	if (len > 4)
		len = 4;
	memcpy(buf, s, len);
	buf[len] = 0;

	if (len == 1)					/* a lone char is that char */
		return (unsigned char)buf[0];

	if (buf[0] == '%')
		return CharMap_Base(buf + 1, len - 1, 16);
	if (buf[0] == '0' && buf[1] == 'x')
		return CharMap_Base(buf + 2, len - 2, 16);
	if (buf[0] == '\\')
	{
		switch (buf[1])
		{
		case 'a': return '\a';
		case 'b': return '\b';
		case 'f': return '\f';
		case 'n': return '\n';
		case 'r': return '\r';
		case 's': return ' ';
		case 't': return '\t';
		case 'v': return '\v';
		case 'x': return CharMap_Base(buf + 2, len - 2, 16);
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
			return CharMap_Base(buf + 1, len - 1, 8);
		default:  return '\\';
		}
	}
	return atoi(buf);				/* decimal */
}

/* fill map[256] from the Map text: identity, then apply each line */
static void CharMap_Build(const char *spec, int *map)
{
	int i;

	for (i = 0; i < 256; i++)
		map[i] = i;
	if (!spec)
		return;

	while (*spec)
	{
		const char *tok[2];
		int len[2], nt = 0, av;

		while (*spec && *spec != '\n' && *spec != '\r')	/* tokens of one line */
		{
			const char *start;

			if (*spec == ' ' || *spec == '\t')
			{
				spec++;
				continue;
			}
			start = spec;
			while (*spec && *spec != ' ' && *spec != '\t'
				   && *spec != '\n' && *spec != '\r')
				spec++;
			if (nt < 2)				/* first two tokens; extras ignored */
			{
				tok[nt] = start;
				len[nt] = (int)(spec - start);
				nt++;
			}
		}
		if (*spec == '\r' && *(spec + 1) == '\n')
			spec += 2;
		else if (*spec)
			spec++;

		if (nt >= 1)
		{
			av = CharMap_Token(tok[0], len[0]);
			if (av >= 0 && av < 256)
				map[av] = (nt >= 2) ? CharMap_Token(tok[1], len[1]) : -1;
		}
	}
}

/* apply the map to Input and publish Output (a mapped-to -1 drops the byte) */
static void CharMap_Recompute(NodeObj instance)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *in, *out;
	int   map[256], len, i, j, k;

	if (!local || !local->enabled)
		return;

	in  = GetPropStr(instance, "Input");
	CharMap_Build(GetPropStr(instance, "Map"), map);

	len = in ? (int)strlen(in) : 0;
	out = malloc(len + 1);
	if (!out)
		return;

	for (i = 0, j = 0; i < len; i++)
	{
		k = map[(unsigned char)in[i]];
		if (k >= 0)
			out[j++] = (char)k;
	}
	out[j] = 0;

	SetPropStr(instance, "Output", out);
	free(out);
}

/* ---- presets: fill the Map box with a ready-made mapping ---- */

static const unsigned char ascii2ebcdic[256] = {
	0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
	0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
	0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
	0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
	0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
	0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0x9A,0x6D,
	0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
	0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0x5F,0x07,
	0x20,0x21,0x22,0x23,0x24,0x15,0x06,0x17,0x28,0x29,0x2A,0x2B,0x2C,0x09,0x0A,0x1B,
	0x30,0x31,0x1A,0x33,0x34,0x35,0x36,0x08,0x38,0x39,0x3A,0x3B,0x04,0x14,0x3E,0xE1,
	0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
	0x58,0x59,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x70,0x71,0x72,0x73,0x74,0x75,
	0x76,0x77,0x78,0x80,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x6A,0x9B,0x9C,0x9D,0x9E,
	0x9F,0xA0,0xAA,0xAB,0xAC,0x4A,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,
	0xB8,0xB9,0xBA,0xBB,0xBC,0xA1,0xBE,0xBF,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xDA,0xDB,
	0xDC,0xDD,0xDE,0xDF,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};

static const unsigned char ebcdic2ascii[256] = {
	0x00,0x01,0x02,0x03,0x9C,0x09,0x86,0x7F,0x97,0x8D,0x8E,0x0B,0x0C,0x0D,0x0E,0x0F,
	0x10,0x11,0x12,0x13,0x9D,0x85,0x08,0x87,0x18,0x19,0x92,0x8F,0x1C,0x1D,0x1E,0x1F,
	0x80,0x81,0x82,0x83,0x84,0x0A,0x17,0x1B,0x88,0x89,0x8A,0x8B,0x8C,0x05,0x06,0x07,
	0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9A,0x9B,0x14,0x15,0x9E,0x1A,
	0x20,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0x5B,0x2E,0x3C,0x28,0x2B,0x21,
	0x26,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0x5D,0x24,0x2A,0x29,0x3B,0x5E,
	0x2D,0x2F,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0x7C,0x2C,0x25,0x5F,0x3E,0x3F,
	0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,0xC0,0xC1,0xC2,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
	0xC3,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,
	0xCA,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,
	0xD1,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,
	0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,
	0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,
	0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,
	0x5C,0x9F,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};

static char *CharMap_Dup(const char *s)
{
	char *b = malloc(strlen(s) + 1);
	if (b)
		strcpy(b, s);
	return b;
}

/* build the Map text a preset name stands for (caller frees); NULL if blank */
static char *CharMap_PresetText(const char *name)
{
	char *buf;
	int   i, n = 0;

	if (!name || !name[0])
		return NULL;

	/* short fixed maps ("\\r" is the two chars \ and r, i.e. the CR token) */
	if (!strcmp(name, "Strip CR (Win->Unix)")) return CharMap_Dup("\\r");
	if (!strcmp(name, "CR->LF (Mac->Unix)"))   return CharMap_Dup("\\r \\n");
	if (!strcmp(name, "LF->CR (Unix->Mac)"))   return CharMap_Dup("\\n \\r");
	if (!strcmp(name, "Backslash->Slash"))     return CharMap_Dup("0x5C 0x2F");
	if (!strcmp(name, "Slash->Backslash"))     return CharMap_Dup("0x2F 0x5C");
	if (!strcmp(name, "Comma->Tab (CSV->TSV)")) return CharMap_Dup(", \\t");
	if (!strcmp(name, "Tab->Comma (TSV->CSV)")) return CharMap_Dup("\\t ,");
	if (!strcmp(name, "Strip Tabs"))           return CharMap_Dup("\\t");
	if (!strcmp(name, "Swap Quotes"))          return CharMap_Dup("\" '\n' \"");
	if (!strcmp(name, "Swap Brackets"))        return CharMap_Dup("{ [\n} ]\n[ {\n] }");

	if (!strcmp(name, "Upper Case") || !strcmp(name, "Lower Case"))
	{
		int up = !strcmp(name, "Upper Case");

		buf = malloc(26 * 5 + 1);		/* "x X\n" per letter */
		if (!buf)
			return NULL;
		for (i = 0; i < 26; i++)
			n += sprintf(buf + n, "%c %c\n", up ? 'a' + i : 'A' + i,
						 up ? 'A' + i : 'a' + i);
		buf[n] = 0;
		return buf;
	}

	if (!strcmp(name, "ROT13"))			/* each letter -> the one 13 places on */
	{
		buf = malloc(52 * 5 + 1);
		if (!buf)
			return NULL;
		for (i = 0; i < 26; i++)
		{
			n += sprintf(buf + n, "%c %c\n", 'A' + i, 'A' + (i + 13) % 26);
			n += sprintf(buf + n, "%c %c\n", 'a' + i, 'a' + (i + 13) % 26);
		}
		buf[n] = 0;
		return buf;
	}

	if (!strcmp(name, "ASCII to EBCDIC") || !strcmp(name, "EBCDIC to ASCII"))
	{
		const unsigned char *tbl = !strcmp(name, "ASCII to EBCDIC")
								   ? ascii2ebcdic : ebcdic2ascii;

		buf = malloc(256 * 11 + 1);		/* "0xII 0xEE\n" per changed byte */
		if (!buf)
			return NULL;
		for (i = 0; i < 256; i++)
			if (tbl[i] != i)
				n += sprintf(buf + n, "0x%02X 0x%02X\n", i, tbl[i]);
		buf[n] = 0;
		return buf;
	}

	return NULL;
}

/* ---- handlers ---- */

int CharMap_OnInput(NodeObj instance, MsgId message, NodeObj data)
{
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "Input"), GetValueStr(data));
	CharMap_Recompute(instance);
	return rtrn_handled;
}

int CharMap_OnMap(NodeObj instance, MsgId message, NodeObj data)
{
	/* store the map; it is read fresh on the next Input change, not now */
	if (message == msg_eof)
		return rtrn_handled;
	SetValueStr(GetPropNode(instance, "Map"), GetValueStr(data));
	return rtrn_handled;
}

/* a preset pick: drop its ready-made mapping into the Map box. Poke the box
   control's In directly - that sets its displayed Value (so the GUI updates)
   and fans back to the widget's Map through the box's own Connect. */
int CharMap_OnPreset(NodeObj instance, MsgId message, NodeObj data)
{
	char *name, *text, vpath[256], boxpath[320];
	NodeObj box;

	if (message == msg_eof)
		return rtrn_handled;

	name = GetValueStr(data);
	SetValueStr(GetPropNode(instance, "Preset"), name ? name : "");

	text = CharMap_PresetText(name);
	if (!text)
		return rtrn_handled;

	if (PathOfInstance(instance, vpath, sizeof(vpath)))
	{
		snprintf(boxpath, sizeof(boxpath), "%s/Map", vpath);
		box = ResolvePath(boxpath);
		if (box)
			SetOrDeliverProp(box, "In", text);
	}
	free(text);
	return rtrn_handled;
}

int CharMap_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;
	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	CharMap_Recompute(instance);
	return rtrn_handled;
}

int CharMap_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;
	Widget_BuildOnce(instance, CharMapPanel);
	CharMap_Recompute(instance);
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
	SetName(instance, "CharacterMap");

	/* every control's value + handler from the table (Input/Map/Enable/Preset
	   carry a handler; Output is plain data) */
	Widget_Init(instance, CharMapPanel);

	/* the preset menu's backing list and the lifecycle state - no control */
	SetPropStr(instance, "PresetList",
			   "default,Upper Case,Lower Case,"
			   "Strip CR (Win->Unix),CR->LF (Mac->Unix),LF->CR (Unix->Mac),"
			   "Backslash->Slash,Slash->Backslash,ROT13,"
			   "Comma->Tab (CSV->TSV),Tab->Comma (TSV->CSV),"
			   "Swap Quotes,Swap Brackets,Strip Tabs,"
			   "ASCII to EBCDIC,EBCDIC to ASCII");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)CharMap_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, CharMapPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, CharMapPanel);

	return rtrn_handled;
}

/* The whole widget in one table: main view, Help, and every control (value +,
   for the ports, handler). PresetList and State are published apart. */
static WidgetItem CharMapPanel[] = {
	/* cls        prop           def        panel   x    y    w    h  label       [handler] */
	{ "View",     "CharacterMap", "",       0,   0,   0, 460, 410, 0 },			/* 0: main */
	{ "Help",     "objects/charactermap/README.md", "", 0, 0, 0, 0, 0, 0 },		/* 1: help */

	{ "Checkbox", "Enable", "1",            0, 430,  14,   9,   9, LABEL_LEFT, (void *)CharMap_OnEnable },
	{ "Textbox",  "Input",  "",             0,  14,  45, 135, 255, LABEL_TOP,  (void *)CharMap_OnInput },
	{ "Textbox",  "Map",    "",             0, 158,  45, 130, 255, LABEL_TOP,  (void *)CharMap_OnMap },
	{ "Textbox",  "Output", "",             0, 300,  45, 135, 255, LABEL_TOP },
	{ "Dropdown", "Preset", "default",      0,  55, 325, 150,  20, LABEL_NONE, (void *)CharMap_OnPreset },

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

	SetName(class, "CharacterMap");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (widget type from each control's class) */
	Widget_Publish(ClassSelf, CharMapPanel);

	/* the preset menu's backing list and the lifecycle state - no control */
	PublishProp(ClassSelf, "PresetList", "data", PROP_NULL,
				"default,Upper Case,Lower Case,"
				"Strip CR (Win->Unix),CR->LF (Mac->Unix),LF->CR (Unix->Mac),"
				"Backslash->Slash,Slash->Backslash,ROT13,"
				"Comma->Tab (CSV->TSV),Tab->Comma (TSV->CSV),"
				"Swap Quotes,Swap Brackets,Strip Tabs,"
				"ASCII to EBCDIC,EBCDIC to ASCII");
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

	SetName(temp, "CharacterMap");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "f8117ccf-44c8-4a96-8e4f-966d082f7e01");
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
