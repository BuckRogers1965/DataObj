#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "data.h"

/*

Implement new data type.
Manage memory for the data type.

Implement a property list for the object that allows multiple properties, make these properties be the same data objects as the data object.  So that way properties can have properties.


Allow functions to be called if a node is changed.

*/

/*

Tasks needed done:

O  add to xml and from xml conversions to allow node trees to be imported and exported

O

*/

typedef struct node *NodeObj;

struct node
{
	int type;
	DataObj name;
	DataObj value;
	NodeObj props;
	NodeObj parent;
	NodeObj class;
	NodeObj child;
	NodeObj nextSib;
} node;

#include "callback.h"

void PrintNodePrivate(NodeObj node, int depth)
{

	NodeObj current = node;
	int i;

	while (current)
	{

		// Print indentation based on the depth of the node
		for (i = 0; i < depth; i++)
			printf("  "); // Two spaces for each level of depth

		// Print the node's name and value
		printf("%s: %s\n", GetStr(current->name), GetStr(current->value));

		// Recursively print properties
		if (current->props)
			PrintNodePrivate(current->props, depth + 1);

		// Recursively print children, increasing the depth
		if (current->child)
			PrintNodePrivate(current->child, depth + 1);

		// Move to the next sibling
		current = current->nextSib;
	}
}

void PrintNodePrivateold(NodeObj node, int depth)
{

	NodeObj current = node;
	int i;

	//	printf("depth=%d\n", depth);

	//	printf("%s %s\n", GetStr(node->name), GetStr(node->value));

	//	node = node->nextSib;
	while (current)
	{

		for (i = 0; i < depth; i++)
			printf(" ");

		printf("%s %s\n", GetStr(current->name), GetStr(current->value));

		if (current->props)
			PrintNodePrivate(current->props, depth);

		if (current->child)
			PrintNodePrivate(current->child, depth + 1);

		current = current->nextSib;
	}

	printf("\n");
}

void PrintNode(NodeObj node)
{

	PrintNodePrivate(node, 0);
}


void DelNode(NodeObj node)
{

	if (node == NULL)
		return;

	DelNode(node->nextSib);
	node->nextSib = NULL;

	DelNode(node->props);
	node->props = NULL;

	DelNode(node->child);
	node->child = NULL;

	if (node->parent && node->parent->child == node)
		node->parent = NULL;

	/* NewNode allocates both of these; nothing else ever frees them -  */
	/* every node deleted without this leaked two DataObj structs       */
	DelData(node->name);
	DelData(node->value);

	free(node);
}

NodeObj
NewNode(int type)
{
	NodeObj temp = malloc(sizeof(node));
	if (temp)
	{
		temp->name = NewData(STRING);
		temp->value = NewData(type);
		temp->props = NULL;
		temp->parent = NULL;
		temp->child = NULL;
		temp->nextSib = NULL;
	}
	return temp;
}

void SetName(NodeObj node, char *name)
{

	if (!node || !name)
		return;

	SetStr(node->name, name);
}

int CmpName(NodeObj node, char *name)
{

	int length;

	if (!node || !name)
		return 0;

	length = strlen(name) + 1;

	return (strncmp(GetStr(node->name), name, length) == 0);
}

void SetValueStr(NodeObj node, char *value)
{

	if (!node || !value)
		return;

	SetStr(node->value, value);
}

/* binary-safe: copies exactly `length` bytes, embedded NULs included */
void SetValueStrLen(NodeObj node, char *value, int length)
{

	if (!node || !value)
		return;

	SetStrLen(node->value, value, length);
}

int GetValueLen(NodeObj node)
{

	if (!node)
		return 0;

	return GetStrLen(node->value);
}

void SetValueInt(NodeObj node, int value)
{

	if (!node || !value)
		return;

	SetInt(node->value, value);
}

void SetValueLong(NodeObj node, long value)
{

	if (!node || !value)
		return;

	SetLong(node->value, value);
}

char *
GetValueStr(NodeObj node)
{

	if (!node)
		return NULL;

	return GetStr(node->value);
}

char *
GetNameStr(NodeObj node)
{

	if (!node)
		return NULL;

	return GetStr(node->name);
}

int GetValueInt(NodeObj node)
{

	if (!node)
		return 0;

	return GetInt(node->value);
}

long GetValueLong(NodeObj node)
{

	if (!node)
		return 0;

	return GetLong(node->value);
}

DataObj
GetValueNode(NodeObj node)
{
	if (!node)
		return 0;

	return node->value;
}

void SetParent(NodeObj parent, NodeObj child)
{
	if (!parent || !child)
		return;
	child->parent = parent;
}

void SetChild(NodeObj node, NodeObj child)
{
	if (!node || !child)
		return;
	DelNode(node->child);
	node->child = child;
	child->parent = node;
}

NodeObj GetChild(NodeObj node){
	return node->child;
}

NodeObj GetParent(NodeObj node){
	if (!node)
		return NULL;
	return node->parent;
}

/* unlink sib from its parent's CHILD chain, relinking around it - does   */
/* NOT free it. Singly-linked (nextSib only, no "previous" pointer), so   */
/* removing anything but the head means walking from the head to find     */
/* whoever points at sib. Leaves sib's own nextSib/parent cleared so a    */
/* caller can safely DelNode() it afterward without cascading into what   */
/* used to be its later siblings - DelNode(node->nextSib) deletes a whole */
/* chain, which is correct for tearing down a subtree wholesale but wrong */
/* for removing one node out of the middle of one. Child-chain only: a    */
/* property's nextSib chain (AddProp) never sets ->parent, so this can't  */
/* (yet) unlink a single property the same way - not needed for instance  */
/* removal, the one thing that currently calls this.                      */
void DelSibling(NodeObj sib)
{
	NodeObj parent, prev;

	if (!sib)
		return;

	parent = sib->parent;
	if (!parent)
		return;

	if (parent->child == sib)
	{
		parent->child = sib->nextSib;
	}
	else
	{
		prev = parent->child;
		while (prev && prev->nextSib != sib)
			prev = prev->nextSib;
		if (prev)
			prev->nextSib = sib->nextSib;
	}

	sib->nextSib = NULL;
	sib->parent = NULL;
}

void AddProp(NodeObj node, NodeObj prop)
{
	if (!node || !prop)
		return;
	if (node->props)
	{
		NodeObj temp = node->props;
		node->props = prop;
		prop->nextSib = temp;
	}
	else
	{
		node->props = prop;
	}
}

/* unlink prop from owner's property chain, relinking around it - does   */
/* NOT free it. Properties don't carry a parent back-pointer the way      */
/* children do (AddProp never sets ->parent), so unlike DelSibling this   */
/* needs the owner passed in explicitly. Leaves prop's own nextSib        */
/* cleared so a caller can safely DelNode() it afterward without          */
/* cascading into whatever property used to follow it in the chain.       */
void RemoveProp(NodeObj owner, NodeObj prop)
{
	NodeObj current;

	if (!owner || !prop)
		return;

	if (owner->props == prop)
	{
		owner->props = prop->nextSib;
	}
	else
	{
		current = owner->props;
		while (current && current->nextSib != prop)
			current = current->nextSib;
		if (current)
			current->nextSib = prop->nextSib;
	}

	prop->nextSib = NULL;
}

void AddSibling(NodeObj node, NodeObj sib)
{
	if (!node || !sib)
		return;

	NodeObj temp = node->nextSib;
	node->nextSib = sib;
	sib->nextSib = temp;
	/*
		sib->nextSib=node->nextSib;
		if (node->nextSib){
			node->nextSib->prevSib=sib;
		}
		node->nextSib=sib;
		sib->prevSib=node;
		sib->parent=node->parent;
		*/
}
NodeObj GetNextSibling(NodeObj node){
	return node->nextSib;
}

void AddChild(NodeObj parent, NodeObj child)
{
	if (!parent || !child)
		return;

	if (parent->child)
		AddSibling(parent->child, child);
	else
		parent->child = child;

	child->parent = parent;
}

/* AddChild inserts right after the head (see AddSibling), which scrambles */
/* order - fine for the registry, wrong for anything whose children must   */
/* replay in the sequence they were added, like a flow script.             */
void AppendChild(NodeObj parent, NodeObj child)
{
	NodeObj last;

	if (!parent || !child)
		return;

	if (!parent->child)
	{
		parent->child = child;
	}
	else
	{
		last = parent->child;
		while (last->nextSib)
			last = last->nextSib;
		last->nextSib = child;
	}

	child->parent = parent;
}

NodeObj
GetPropNode(NodeObj node, char *name)
{
#ifndef S_SPLINT_S

	if (!node || !name)
		return NULL;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return current;
		current = current->nextSib;
	}
#endif
	return NULL;
}

long GetPropLong(NodeObj node, char *name)
{
#ifndef S_SPLINT_S

	if (!node || !name)
		return NULL;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return GetValueLong(current);
		current = current->nextSib;
	}
#endif
	return 0;
}

int GetPropInt(NodeObj node, char *name)
{
	if (!node || !name)
		return 0;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return GetValueInt(current);
		current = current->nextSib;
	}
	return 0;
}

char *GetPropStr(NodeObj node, char *name)
{
	if (!node || !name)
		return NULL;
	NodeObj current = node->props;
	while (current)
	{
		if (CmpName(current, name))
			return GetValueStr(current);
		current = current->nextSib;
	}
	return NULL;
}

/* returns the first property of a node so callers can iterate */
/* the property list, walk the rest with GetNextSibling         */
NodeObj GetNextProp(NodeObj node)
{
	if (!node)
		return NULL;
	return node->props;
}

/* every property write fans out, unconditionally, to whatever has       */
/* subscribed to it - Connect() (object.c) already leaves "Subscriber"   */
/* children on whatever node it targets, port or plain property alike    */
/* (see AddSubscription/SndMsg); this walks the same shape natively so   */
/* node.c does not have to call up into object.c. There is no opt-in     */
/* step - a property is watchable simply by existing, same as a port.    */
static void FanOutSubscribers(NodeObj propnode)
{
	NodeObj sub, toInstance;
	int (*callback)(NodeObj, int, NodeObj);

	sub = GetNextProp(propnode);
	while (sub)
	{
		if (CmpName(sub, "Subscriber"))
		{
			callback   = (int (*)(NodeObj, int, NodeObj)) GetPropLong(sub, "Callback");
			toInstance = (NodeObj) GetPropLong(sub, "Instance");
			if (callback)
				callback(toInstance, msg_change, propnode);
		}
		sub = GetNextSibling(sub);
	}
}

void SetPropLong(NodeObj node, char *name, long value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		SetLong(propnode->value, value);
		FanOutSubscribers(propnode);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(LONG);
	SetStr(propnode->name, name);
	SetLong(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropInt(NodeObj node, char *name, int value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		SetInt(propnode->value, value);
		FanOutSubscribers(propnode);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(INTEGER);
	SetStr(propnode->name, name);
	SetInt(propnode->value, value);
	AddProp(node, propnode);
}

void SetPropStr(NodeObj node, char *name, char * value)
{
	NodeObj propnode;

	if (!node || !name)
		return;

	propnode = GetPropNode(node, name);
	if (propnode)
	{
		SetStr(propnode->value, value);
		FanOutSubscribers(propnode);
		return;
	}

	/* otherwise create the property */
	propnode = NewNode(STRING);
	SetStr(propnode->name, name);
	SetStr(propnode->value, value);
	AddProp(node, propnode);
}

/* ---- serialization: node tree as text (JSON), and back ---- */

int GetValueType(NodeObj node)
{
	if (!node)
		return STRING;
	return GetDataType(node->value);
}

static char *TypeName(int type)
{
	switch (type)
	{
	case INTEGER:
		return "integer";
	case HEX:
		return "hex";
	case REAL:
		return "real";
	case LONG:
		return "long";
	case STRING:
	default:
		return "string";
	}
}

static int TypeFromName(char *name)
{
	if (!name)
		return STRING;
	if (strcmp(name, "integer") == 0)
		return INTEGER;
	if (strcmp(name, "hex") == 0)
		return HEX;
	if (strcmp(name, "real") == 0)
		return REAL;
	if (strcmp(name, "long") == 0)
		return LONG;
	return STRING;
}

typedef struct
{
	char *buf;
	size_t len;
	size_t cap;
} StrBuf;

static void sbInit(StrBuf *sb)
{
	sb->cap = 256;
	sb->len = 0;
	sb->buf = malloc(sb->cap);
	sb->buf[0] = '\0';
}

static void sbAppendN(StrBuf *sb, char *s, size_t n)
{
	if (sb->len + n + 1 > sb->cap)
	{
		while (sb->len + n + 1 > sb->cap)
			sb->cap *= 2;
		sb->buf = realloc(sb->buf, sb->cap);
	}
	memcpy(sb->buf + sb->len, s, n);
	sb->len += n;
	sb->buf[sb->len] = '\0';
}

static void sbAppend(StrBuf *sb, char *s)
{
	sbAppendN(sb, s, strlen(s));
}

static void sbAppendChar(StrBuf *sb, char c)
{
	sbAppendN(sb, &c, 1);
}

static void sbAppendJsonString(StrBuf *sb, char *s)
{
	char hex[8];
	unsigned char c;

	sbAppendChar(sb, '"');
	if (s)
	{
		for (; *s; s++)
		{
			c = (unsigned char)*s;
			switch (c)
			{
			case '"':
				sbAppend(sb, "\\\"");
				break;
			case '\\':
				sbAppend(sb, "\\\\");
				break;
			case '\n':
				sbAppend(sb, "\\n");
				break;
			case '\r':
				sbAppend(sb, "\\r");
				break;
			case '\t':
				sbAppend(sb, "\\t");
				break;
			case '\b':
				sbAppend(sb, "\\b");
				break;
			case '\f':
				sbAppend(sb, "\\f");
				break;
			default:
				if (c < 0x20)
				{
					snprintf(hex, sizeof(hex), "\\u%04x", c);
					sbAppend(sb, hex);
				}
				else
				{
					sbAppendChar(sb, (char)c);
				}
			}
		}
	}
	sbAppendChar(sb, '"');
}

static void EncodeNode(StrBuf *sb, NodeObj n);

static void EncodeList(StrBuf *sb, NodeObj head)
{
	NodeObj current = head;
	int first = 1;

	sbAppendChar(sb, '[');
	while (current)
	{
		if (!first)
			sbAppendChar(sb, ',');
		first = 0;
		EncodeNode(sb, current);
		current = current->nextSib;
	}
	sbAppendChar(sb, ']');
}

static void EncodeNode(StrBuf *sb, NodeObj n)
{
	sbAppendChar(sb, '{');
	sbAppend(sb, "\"name\":");
	sbAppendJsonString(sb, GetStr(n->name));
	sbAppend(sb, ",\"type\":");
	sbAppendJsonString(sb, TypeName(GetDataType(n->value)));
	sbAppend(sb, ",\"value\":");
	sbAppendJsonString(sb, GetStr(n->value));
	sbAppend(sb, ",\"props\":");
	EncodeList(sb, n->props);
	sbAppend(sb, ",\"children\":");
	EncodeList(sb, n->child);
	sbAppendChar(sb, '}');
}

/* node tree -> JSON text; caller frees the returned buffer */
char *NodeToText(NodeObj node)
{
	StrBuf sb;

	if (!node)
		return NULL;

	sbInit(&sb);
	EncodeNode(&sb, node);
	return sb.buf;
}

/*
 * A quoted, escaped JSON string literal (the quotes are included), for
 * code hand-building JSON objects (the Bridge's events, live taps) that
 * would otherwise embed a field with a raw %s and break the moment the
 * value contains a '"' or a newline. Caller frees the returned buffer.
 */
char *JsonEscapeStr(char *s)
{
	StrBuf sb;

	sbInit(&sb);
	sbAppendJsonString(&sb, s);
	return sb.buf;
}

typedef struct
{
	char *p;
} Cursor;

static void SkipWs(Cursor *c)
{
	while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')
		c->p++;
}

static char *ParseJsonString(Cursor *c)
{
	StrBuf sb;
	unsigned int cp;
	int i;
	char h;

	sbInit(&sb);

	if (*c->p != '"')
		return sb.buf;
	c->p++;

	while (*c->p && *c->p != '"')
	{
		if (*c->p == '\\')
		{
			c->p++;
			switch (*c->p)
			{
			case '"':
				sbAppendChar(&sb, '"');
				break;
			case '\\':
				sbAppendChar(&sb, '\\');
				break;
			case '/':
				sbAppendChar(&sb, '/');
				break;
			case 'n':
				sbAppendChar(&sb, '\n');
				break;
			case 'r':
				sbAppendChar(&sb, '\r');
				break;
			case 't':
				sbAppendChar(&sb, '\t');
				break;
			case 'b':
				sbAppendChar(&sb, '\b');
				break;
			case 'f':
				sbAppendChar(&sb, '\f');
				break;
			case 'u':
				cp = 0;
				for (i = 0; i < 4; i++)
				{
					c->p++;
					h = *c->p;
					cp <<= 4;
					if (h >= '0' && h <= '9')
						cp |= (h - '0');
					else if (h >= 'a' && h <= 'f')
						cp |= (h - 'a' + 10);
					else if (h >= 'A' && h <= 'F')
						cp |= (h - 'A' + 10);
				}
				/* BMP-only UTF-8 encode; surrogate pairs not handled */
				if (cp < 0x80)
				{
					sbAppendChar(&sb, (char)cp);
				}
				else if (cp < 0x800)
				{
					sbAppendChar(&sb, (char)(0xC0 | (cp >> 6)));
					sbAppendChar(&sb, (char)(0x80 | (cp & 0x3F)));
				}
				else
				{
					sbAppendChar(&sb, (char)(0xE0 | (cp >> 12)));
					sbAppendChar(&sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
					sbAppendChar(&sb, (char)(0x80 | (cp & 0x3F)));
				}
				break;
			default:
				break;
			}
			c->p++;
		}
		else
		{
			sbAppendChar(&sb, *c->p);
			c->p++;
		}
	}
	if (*c->p == '"')
		c->p++;

	return sb.buf;
}

static NodeObj ParseNodeObject(Cursor *c);

static NodeObj ParseArray(Cursor *c)
{
	NodeObj head = NULL, tail = NULL, n;

	if (*c->p != '[')
		return NULL;
	c->p++;
	SkipWs(c);
	if (*c->p == ']')
	{
		c->p++;
		return NULL;
	}

	while (1)
	{
		SkipWs(c);
		n = ParseNodeObject(c);
		if (n)
		{
			if (tail)
				tail->nextSib = n;
			else
				head = n;
			tail = n;
		}
		SkipWs(c);
		if (*c->p == ',')
		{
			c->p++;
			continue;
		}
		if (*c->p == ']')
		{
			c->p++;
			break;
		}
		break;
	}
	return head;
}

/*
 * Parses exactly what EncodeNode produces: an object with string-valued
 * name/type/value and array-valued props/children. Not a general JSON
 * parser - numbers, booleans and null are not accepted as values.
 */
static NodeObj ParseNodeObject(Cursor *c)
{
	char *name = NULL, *typeName = NULL, *value = NULL, *key;
	NodeObj props = NULL, children = NULL, node, ch;
	int type;

	if (*c->p != '{')
		return NULL;
	c->p++;

	SkipWs(c);
	if (*c->p == '}')
	{
		c->p++;
	}
	else
	{
		while (1)
		{
			SkipWs(c);
			if (*c->p != '"')
				break;
			key = ParseJsonString(c);
			SkipWs(c);
			if (*c->p == ':')
				c->p++;
			SkipWs(c);

			if (strcmp(key, "name") == 0)
				name = ParseJsonString(c);
			else if (strcmp(key, "type") == 0)
				typeName = ParseJsonString(c);
			else if (strcmp(key, "value") == 0)
				value = ParseJsonString(c);
			else if (strcmp(key, "props") == 0)
				props = ParseArray(c);
			else if (strcmp(key, "children") == 0)
				children = ParseArray(c);
			else if (*c->p == '"')
				free(ParseJsonString(c));

			free(key);
			SkipWs(c);
			if (*c->p == ',')
			{
				c->p++;
				continue;
			}
			if (*c->p == '}')
			{
				c->p++;
				break;
			}
			break;
		}
	}

	type = TypeFromName(typeName);
	node = NewNode(type);
	if (name)
		SetName(node, name);
	if (value)
	{
		switch (type)
		{
		case INTEGER:
			SetValueInt(node, atoi(value));
			break;
		case LONG:
			SetValueLong(node, atol(value));
			break;
		case HEX:
			SetHex(node->value, value);
			break;
		case REAL:
			SetReal(node->value, atof(value));
			break;
		case STRING:
		default:
			SetValueStr(node, value);
			break;
		}
	}

	node->props = props;
	node->child = children;
	ch = children;
	while (ch)
	{
		ch->parent = node;
		ch = ch->nextSib;
	}

	free(name);
	free(typeName);
	free(value);

	return node;
}

/* JSON text (as produced by NodeToText) -> a new node tree */
NodeObj TextToNode(char *text)
{
	Cursor c;

	if (!text)
		return NULL;

	c.p = text;
	SkipWs(&c);
	if (*c.p != '{')
		return NULL;

	return ParseNodeObject(&c);
}

/*
 * A flat JSON object - {"key":"value","key2":"value2",...} - parsed into
 * a node whose PROPS are the key/value pairs, all as strings (whatever
 * reads them converts automatically, same convention flow scripts already
 * use for property values). No nesting, no arrays: this is for simple
 * flat commands like the bridge protocol, not general trees - use
 * TextToNode for anything with props/children of its own.
 */
NodeObj TextToProps(char *text)
{
	Cursor c;
	NodeObj node;
	char *key, *value;

	if (!text)
		return NULL;

	c.p = text;
	SkipWs(&c);
	if (*c.p != '{')
		return NULL;
	c.p++;

	node = NewNode(INTEGER);
	SetName(node, "Command");

	SkipWs(&c);
	if (*c.p == '}')
		return node;

	while (1)
	{
		SkipWs(&c);
		if (*c.p != '"')
			break;
		key = ParseJsonString(&c);

		SkipWs(&c);
		if (*c.p == ':')
			c.p++;
		SkipWs(&c);
		value = ParseJsonString(&c);

		SetPropStr(node, key, value);
		free(key);
		free(value);

		SkipWs(&c);
		if (*c.p == ',')
		{
			c.p++;
			continue;
		}
		if (*c.p == '}')
			break;
		break;
	}

	return node;
}

void TestFunc(NodeObj node)
{

	printf("\n\nRunning tests\n\n");

	printf("Set name of root to Test.\n");
	SetName(node, "Test");

	printf("Is name of root Test?  %d\n", CmpName(node, "Test"));
	printf("Is name of root Banana?  %d\n", CmpName(node, "Banana"));

	printf("Set property on root named Banana, set value to 5.\n");
	SetPropInt(node, "Banana", 5);

	printf("The value of Banana is %d\n", GetValueInt(GetPropNode(node, "Banana")));

	printf("Check root for non exisitant property Fred.\n");
	if (GetPropNode(node, "Fred"))
		printf("Fred Exists.\n");
	else
		printf("Fred property does not exist.\n");

	printf("Set another property on root to be Fred with value of 6.\n");
	SetPropInt(node, "Fred", 6);

	if (GetPropNode(node, "Fred"))
		printf("Fred Exists.\n");
	else
		printf("Fred property does not exist.\n");

	PrintNode(node);
}

void SerializationTest()
{
	NodeObj root, child, roundtrip;
	char *json;

	printf("\n\nRunning serialization tests\n\n");

	root = NewNode(STRING);
	SetName(root, "Root");
	SetValueStr(root, "hello \"world\"\nline two");
	SetPropInt(root, "Count", 5);
	SetPropLong(root, "Handle", 140224278132965);

	child = NewNode(INTEGER);
	SetName(child, "Child1");
	SetValueInt(child, 42);
	AddChild(root, child);

	json = NodeToText(root);
	printf("Encoded: %s\n", json);

	roundtrip = TextToNode(json);
	printf("Decoded name=%s value=%s Count=%d Handle=%ld child name=%s child value=%d\n",
		   GetNameStr(roundtrip),
		   GetValueStr(roundtrip),
		   GetPropInt(roundtrip, "Count"),
		   GetPropLong(roundtrip, "Handle"),
		   GetNameStr(GetChild(roundtrip)),
		   GetValueInt(GetChild(roundtrip)));

	free(json);
	DelNode(root);
	DelNode(roundtrip);
}

static int SubscriberTestSeen;
static int SubscriberTestMessage;

/* signature matches every other Subscriber callback in the system -     */
/* (instance, message, data), the convention SndMsg (object.c) already   */
/* uses to invoke a port's subscribers. There is nothing property-       */
/* specific about it, which is the point: a plain property and a port    */
/* are fanned out to identically.                                        */
int SubscriberTestCallback(NodeObj instance, int message, NodeObj data)
{
	SubscriberTestMessage = message;
	SubscriberTestSeen = GetValueInt(data);
	return rtrn_handled;
}

void InterceptTest()
{
	NodeObj node, propnode, sub, before, after;

	printf("\n\nRunning subscriber fan-out tests\n\n");

	/* a property is watchable simply by existing - no opt-in step needed. */
	/* This attaches the exact same "Subscriber" child AddSubscription      */
	/* (object.c) would, straight onto the property node, to prove          */
	/* SetProp* fans out on its own with nothing else involved.              */
	node = NewNode(INTEGER);
	SetName(node, "SubscriberHolder");
	SetPropInt(node, "Value", 10);

	propnode = GetPropNode(node, "Value");
	sub = NewNode(INTEGER);
	SetName(sub, "Subscriber");
	SetPropLong(sub, "Instance", (long) node);
	SetPropLong(sub, "Callback", (long) SubscriberTestCallback);
	AddProp(propnode, sub);

	SetPropInt(node, "Value", 5);
	printf("Plain write fans out to its subscriber unasked: %d (saw value %d, message %d)\n",
		   SubscriberTestSeen == 5 && SubscriberTestMessage == msg_change,
		   SubscriberTestSeen, SubscriberTestMessage);

	DelNode(node);

	/* same property node identity should survive a plain (non-intercepted) */
	/* write now too - this is the shadow-vs-update fix, not the intercept  */
	node = NewNode(INTEGER);
	SetName(node, "PlainHolder");
	SetPropInt(node, "Foo", 1);
	before = GetPropNode(node, "Foo");

	SetPropInt(node, "Foo", 2);
	after = GetPropNode(node, "Foo");

	printf("Plain write updates in place instead of shadowing: %d (value now %d)\n",
		   before == after, GetPropInt(node, "Foo"));

	DelNode(node);

	/* the trap the fix above walks into: FlowSetProp always calls        */
	/* SetPropStr regardless of the target's native type (relying on the  */
	/* "intelligent data object" auto-conversion), so an update-in-place  */
	/* that writes through the wrong type's setter must not corrupt the   */
	/* other representations - this is exactly what broke Pulse's Count   */
	/* the first time this went in: SetPropStr("Count","1") on an         */
	/* INTEGER-native property silently zeroed it back to "pulses forever"*/
	node = NewNode(INTEGER);
	SetName(node, "CrossTypeHolder");
	SetPropInt(node, "Count", 0);
	SetPropStr(node, "Count", "1");

	printf("Cross-type write (SetPropStr onto an INTEGER prop) reads back correctly: %d (value now %d)\n",
		   GetPropInt(node, "Count") == 1, GetPropInt(node, "Count"));

	DelNode(node);
}

void NodeTest()
{

	NodeObj root = NULL;
	int i;

	for (i = 0; i < 1; i++)
	{
		root = NewNode(INTEGER);

		TestFunc(root);

		DelNode(root);
		root = NULL;

		TestFunc(root);
	}

	SerializationTest();
	InterceptTest();
}
