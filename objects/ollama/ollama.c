
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"

/* Ollama: POST a chat payload to an OpenAI-format endpoint (Ollama's
   /v1/chat/completions) and put the reply text in Output. The socket is
   non-blocking, driven by a poll task, so a long generation never stalls the
   core; Timeout (seconds) bounds the wait. Plain HTTP only. */

enum { PH_IDLE, PH_CONN, PH_SEND, PH_RECV };
enum { REQ_CHAT, REQ_MODELS };

typedef struct { char *b; size_t n, cap; } SB;

typedef struct InstanceData
{
	int     enabled;
	int     panelBuilt;
	TaskObj buildTask;
	TaskObj poll;
	int     fd;
	int     phase;
	int     reqkind;
	char   *req;
	int     reqlen, reqsent;
	SB      resp;
	time_t  deadline;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void Ollama_BuildPanel(NodeObj instance);
static int  Ollama_BuildTask(NodeObj instance, NodeObj data, int msgid);
static int  Ollama_Poll(NodeObj instance, NodeObj taskdata, int reason);

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("Ollama handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- growable string ---- */

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

/* ---- tiny JSON reply reader ---- */

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

static void utf8_put(SB *o, unsigned cp)
{
	if (cp < 0x80)
		sbputc(o, (char)cp);
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

/* q points at the opening quote of a JSON string; return it unescaped */
static char *json_unescape(const char *q)
{
	SB o;
	o.b = NULL; o.n = o.cap = 0;

	q++;
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
		case 'n': sbputc(&o, '\n'); q++; break;
		case 't': sbputc(&o, '\t'); q++; break;
		case 'r': sbputc(&o, '\r'); q++; break;
		case 'b': sbputc(&o, '\b'); q++; break;
		case 'f': sbputc(&o, '\f'); q++; break;
		case '/': sbputc(&o, '/');  q++; break;
		case '"': sbputc(&o, '"');  q++; break;
		case '\\': sbputc(&o, '\\'); q++; break;
		case 'u':
		{
			unsigned cp = 0;
			int i;
			for (i = 0; i < 4 && q[1]; i++)
				cp = cp * 16 + hexval((unsigned char)*++q);
			q++;
			if (cp >= 0xD800 && cp <= 0xDBFF && q[0] == '\\' && q[1] == 'u')
			{
				unsigned lo = 0;
				const char *s = q + 1;
				for (i = 0; i < 4 && s[1]; i++)
					lo = lo * 16 + hexval((unsigned char)*++s);
				if (lo >= 0xDC00 && lo <= 0xDFFF)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					q = s + 1;
				}
			}
			utf8_put(&o, cp);
			break;
		}
		default:
			if (*q) sbputc(&o, *q++);
		}
	}
	if (!o.b)
		return strcpy(malloc(1), "");
	return o.b;
}

/* value of the first string-valued occurrence of "key" (malloc'd or NULL) */
static char *json_string(const char *json, const char *key)
{
	char pat[80];
	const char *p = json;

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	while ((p = strstr(p, pat)))
	{
		const char *q = p + strlen(pat);
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
		if (*q != ':') { p += strlen(pat); continue; }
		q++;
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
		if (*q == '"')
			return json_unescape(q);
		p += strlen(pat);			/* not a string value; keep looking */
	}
	return NULL;
}

static char *dupstr(const char *s)
{
	char *b = malloc(strlen(s) + 1);
	strcpy(b, s);
	return b;
}

/* comma-joined list of every string-valued occurrence of "key" */
static char *json_string_all(const char *json, const char *key)
{
	char pat[80];
	const char *p = json;
	SB o;
	int first = 1;

	o.b = NULL; o.n = o.cap = 0;
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	while ((p = strstr(p, pat)))
	{
		const char *q = p + strlen(pat);
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
		if (*q == ':')
		{
			q++;
			while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
			if (*q == '"')
			{
				char *v = json_unescape(q);
				if (v)
				{
					if (!first) sbputc(&o, ',');
					first = 0;
					sbput(&o, v, strlen(v));
					free(v);
				}
			}
		}
		p += strlen(pat);
	}
	return o.b ? o.b : dupstr("");
}

/* wrap the prompt in an OpenAI chat request for the given model */
static char *build_body(char *model, char *prompt)
{
	char *em = JsonEscapeStr(model ? model : "");
	char *ep = JsonEscapeStr(prompt ? prompt : "");
	SB    o;

	o.b = NULL; o.n = o.cap = 0;
	sbputs(&o, "{\"model\":");
	sbputs(&o, em);
	sbputs(&o, ",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":");
	sbputs(&o, ep);
	sbputs(&o, "}]}");
	free(em);
	free(ep);
	return o.b ? o.b : dupstr("{}");
}

/* case-insensitive substring search */
static int ci_find(const char *hay, const char *needle)
{
	size_t nl = strlen(needle);
	for (; *hay; hay++)
	{
		size_t i = 0;
		while (i < nl && hay[i]
			   && (hay[i] | 0x20) == (needle[i] | 0x20))
			i++;
		if (i == nl)
			return 1;
	}
	return 0;
}

/* de-chunk a Transfer-Encoding: chunked body (malloc'd; caller frees) */
static char *dechunk(const char *body, size_t *outlen)
{
	SB o;
	const char *p = body;
	o.b = NULL; o.n = o.cap = 0;

	for (;;)
	{
		char *end;
		long len = strtol(p, &end, 16);
		const char *nl;
		if (end == p)
			break;
		nl = strstr(p, "\r\n");
		if (!nl)
			break;
		p = nl + 2;
		if (len <= 0)
			break;
		sbput(&o, p, (size_t)len);
		p += len;
		if (p[0] == '\r' && p[1] == '\n')
			p += 2;
	}
	*outlen = o.n;
	return o.b ? o.b : strcpy(malloc(1), "");
}

/* ---- request / socket ---- */

/* numeric first (no lookup); a hostname blocks briefly on getaddrinfo */
static int resolve_into(const char *host, struct sockaddr_in *out)
{
	struct addrinfo hints, *res = NULL;

	if (inet_aton(host, &out->sin_addr))
		return 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res)
		return 0;
	out->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);
	return 1;
}

static char *build_request(const char *method, const char *host, int port,
						   const char *path, const char *payload, int *outlen)
{
	int   plen = payload ? (int)strlen(payload) : 0;
	char  hdr[600];
	int   hl;
	char *req;

	hl = snprintf(hdr, sizeof(hdr),
				  "%s %s HTTP/1.1\r\n"
				  "Host: %s:%d\r\n"
				  "Content-Type: application/json\r\n"
				  "Accept: application/json\r\n"
				  "Content-Length: %d\r\n"
				  "Connection: close\r\n"
				  "\r\n",
				  method, (path && path[0]) ? path : "/",
				  host, port, plen);

	req = malloc(hl + plen);
	memcpy(req, hdr, hl);
	if (plen)
		memcpy(req + hl, payload, plen);
	*outlen = hl + plen;
	return req;
}

/* drop the socket and buffers, back to idle (leaves Output/Status alone) */
static void Ollama_Reset(InstanceData *local)
{
	if (local->fd >= 0)
		close(local->fd);
	local->fd = -1;
	if (local->req)
		free(local->req);
	local->req = NULL;
	free(local->resp.b);
	local->resp.b = NULL;
	local->resp.n = local->resp.cap = 0;
	local->reqlen = local->reqsent = 0;
	local->phase = PH_IDLE;
}

static void Ollama_Fail(NodeObj instance, InstanceData *local, char *why)
{
	SetPropStr(instance, "Status", why);
	Ollama_Reset(local);
}

/* the whole reply arrived (peer closed): pull out the text, publish it */
static void Ollama_Complete(NodeObj instance, InstanceData *local)
{
	char *r = local->resp.b ? local->resp.b : (char *)"";
	char *body, *content = NULL, *decoded = NULL;
	char  status[128] = "done";
	int   chunked = 0;

	if (!strncmp(r, "HTTP/", 5))			/* first line is the status */
	{
		char *eol = strstr(r, "\r\n");
		int   L = eol ? (int)(eol - r) : 0;
		if (L > 0 && L < (int)sizeof(status))
		{
			memcpy(status, r, L);
			status[L] = 0;
		}
	}

	body = strstr(r, "\r\n\r\n");
	if (body)
	{
		*body = 0;						/* headers only, for the chunked check */
		chunked = ci_find(r, "transfer-encoding") && ci_find(r, "chunked");
		*body = '\r';
		body += 4;
	}
	else
		body = r;

	if (chunked)
	{
		size_t dl;
		decoded = dechunk(body, &dl);
		body = decoded;
	}

	if (local->reqkind == REQ_MODELS)		/* a /v1/models reply: list the ids */
	{
		char *list = json_string_all(body, "id");
		SetPropStr(instance, "Status", status);
		SetPropStr(instance, "ModelsList", list);
		free(list);
	}
	else
	{
		content = json_string(body, "content");
		SetPropStr(instance, "Status", status);
		SetPropStr(instance, "Output", content ? content : body);
		if (content) free(content);
	}

	if (decoded) free(decoded);
	Ollama_Reset(local);
}

/* resolve, non-blocking connect, arm the poll for one request */
static void Ollama_Begin(NodeObj instance, InstanceData *local, int kind,
						 const char *method, const char *path, const char *body)
{
	struct sockaddr_in addr;
	char *host = GetPropStr(instance, "Server");
	int   port = GetPropInt(instance, "Port");
	int   timeout = GetPropInt(instance, "Timeout");

	if (local->phase != PH_IDLE)		/* a request is running - replace it */
		Ollama_Reset(local);

	if (!host || !host[0]) host = "192.168.4.253";
	if (port <= 0) port = 11434;
	if (timeout <= 0) timeout = 3600;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	if (!resolve_into(host, &addr))
	{
		SetPropStr(instance, "Status", "cannot resolve server");
		return;
	}

	local->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (local->fd < 0)
	{
		SetPropStr(instance, "Status", "no socket");
		return;
	}
	fcntl(local->fd, F_SETFL, O_NONBLOCK);

	if (connect(local->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
		&& errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		SetPropStr(instance, "Status", "connect refused");
		close(local->fd);
		local->fd = -1;
		return;
	}

	local->reqkind = kind;
	local->req = build_request(method, host, port, path, body, &local->reqlen);
	local->reqsent = 0;
	local->deadline = time(NULL) + timeout;
	local->phase = PH_CONN;
	SetPropStr(instance, "Status", "connecting");

	AddTaskMilli(local->poll, 40, (FuncPtr)Ollama_Poll, msg_send, instance);
}

/* POST the payload (with Model injected) to the chat endpoint */
static void Ollama_Start(NodeObj instance, InstanceData *local)
{
	char *body = build_body(GetPropStr(instance, "Model"),
							GetPropStr(instance, "Payload"));
	SetPropStr(instance, "Output", "");
	Ollama_Begin(instance, local, REQ_CHAT, "POST", GetPropStr(instance, "Path"), body);
	free(body);
}

/* GET the model list so the dropdown can be filled */
static void Ollama_StartModels(NodeObj instance, InstanceData *local)
{
	Ollama_Begin(instance, local, REQ_MODELS, "GET", "/v1/models", "");
}

static int Ollama_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char buf[4096];

	(void) taskdata;

	if (!local || local->phase == PH_IDLE || reason == task_deactivate)
		return rtrn_handled;

	if (time(NULL) >= local->deadline)
	{
		Ollama_Fail(instance, local, "timed out");
		return rtrn_handled;
	}

	if (local->phase == PH_CONN)
	{
		fd_set w;
		struct timeval tv;
		int err = 0;
		socklen_t l = sizeof(err);

		FD_ZERO(&w);
		FD_SET(local->fd, &w);
		tv.tv_sec = tv.tv_usec = 0;
		if (select(local->fd + 1, NULL, &w, NULL, &tv) > 0)
		{
			if (getsockopt(local->fd, SOL_SOCKET, SO_ERROR, &err, &l) < 0 || err)
			{
				Ollama_Fail(instance, local, "connect failed");
				return rtrn_handled;
			}
			local->phase = PH_SEND;
			SetPropStr(instance, "Status", "sending");
		}
	}

	if (local->phase == PH_SEND)
	{
		while (local->reqsent < local->reqlen)
		{
			int k = send(local->fd, local->req + local->reqsent,
						 local->reqlen - local->reqsent, 0);
			if (k > 0)
				local->reqsent += k;
			else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				break;
			else
			{
				Ollama_Fail(instance, local, "send failed");
				return rtrn_handled;
			}
		}
		if (local->reqsent == local->reqlen)
		{
			local->phase = PH_RECV;
			SetPropStr(instance, "Status", "waiting for reply");
		}
	}

	if (local->phase == PH_RECV)
	{
		for (;;)
		{
			int k = recv(local->fd, buf, sizeof(buf), 0);
			if (k > 0)
				sbput(&local->resp, buf, (size_t)k);
			else if (k == 0)
			{
				Ollama_Complete(instance, local);
				return rtrn_handled;
			}
			else if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				Ollama_Fail(instance, local, "read failed");
				return rtrn_handled;
			}
		}
	}

	AddTaskMilli(local->poll, 40, (FuncPtr)Ollama_Poll, msg_send, instance);
	return rtrn_handled;
}

/* ---- handlers ---- */

/* Send: a 1 fires a request; ignored while disabled */
int Ollama_OnSend(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->enabled)
		return rtrn_handled;
	Ollama_Start(instance, local);
	return rtrn_handled;
}

/* Refresh: a 1 re-fetches the model list */
int Ollama_OnRefresh(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->enabled)
		return rtrn_handled;
	Ollama_StartModels(instance, local);
	return rtrn_handled;
}

/* a model pick: drop the name into the Model box (which stays overridable) */
int Ollama_OnModels(NodeObj instance, MsgId message, NodeObj data)
{
	char *name, vpath[256], boxpath[320];
	NodeObj box;

	if (message == msg_eof)
		return rtrn_handled;
	name = GetValueStr(data);
	SetValueStr(GetPropNode(instance, "Models"), name ? name : "");
	if (name && name[0] && PathOfInstance(instance, vpath, sizeof(vpath)))
	{
		snprintf(boxpath, sizeof(boxpath), "%s/Model", vpath);
		box = ResolvePath(boxpath);
		if (box)
			SetOrDeliverProp(box, "In", name);
	}
	return rtrn_handled;
}

int Ollama_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;
	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	if (!local->enabled && local->phase != PH_IDLE)
		Ollama_Fail(instance, local, "cancelled");
	return rtrn_handled;
}

int Ollama_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		Ollama_BuildPanel(instance);
	}
	return rtrn_handled;
}

/* ---- lifecycle ---- */

static void Ollama_Port(NodeObj instance, char *name, char *initial, void *handler)
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
	local->poll = NULL;
	local->fd = -1;
	local->phase = PH_IDLE;
	local->req = NULL;
	local->reqlen = local->reqsent = 0;
	local->resp.b = NULL;
	local->resp.n = local->resp.cap = 0;
	local->deadline = 0;
	local->reqkind = REQ_CHAT;

	instance = NewNode(INTEGER);
	SetName(instance, "Ollama");

	SetPropStr(instance, "Server", "192.168.4.253");
	SetPropStr(instance, "Port", "11434");
	SetPropStr(instance, "Path", "/v1/chat/completions");
	SetPropStr(instance, "Timeout", "3600");
	SetPropStr(instance, "Model", "llama3.2");
	SetPropStr(instance, "ModelsList", "");
	SetPropStr(instance, "Payload", "Say hello in one sentence.");
	SetPropStr(instance, "Output", "");
	SetPropStr(instance, "Status", "idle");

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Ollama_Activate);

	Ollama_Port(instance, "Send",    "0", (void *)Ollama_OnSend);
	Ollama_Port(instance, "Refresh", "0", (void *)Ollama_OnRefresh);
	Ollama_Port(instance, "Models",  "", (void *)Ollama_OnModels);
	Ollama_Port(instance, "Enable",  "1", (void *)Ollama_OnEnable);

	InitPosition(instance);

	/* set here, before any client subscribes */
	SetPropInt(instance, "W", 490);
	SetPropInt(instance, "H", 510);

	RegisterInstance(class, instance);

	local->poll = CreateTask(ObjGetTaskList());
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)Ollama_BuildTask, msg_send, instance);

	return rtrn_handled;
}

static void Ollama_Reflect(NodeObj src, char *sp, NodeObj dst, char *dp)
{
	char *cur;

	Connect(src, sp, dst, dp);
	cur = GetPropStr(src, sp);
	if (cur)
		SetOrDeliverProp(dst, dp, cur);
}

static char *Ollama_ReadFile(char *path)
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

static void Ollama_Ctl(NodeObj container, NodeObj target, char *cls, char *prop,
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
		Ollama_Reflect(target, prop, c, "Value");
	else if (strcmp(cls, "Dropdown") == 0)
	{
		char listprop[64];
		snprintf(listprop, sizeof(listprop), "%sList", prop);
		Connect(c, "Value", target, prop);
		Ollama_Reflect(target, listprop, c, "Items");
		SetOrDeliverProp(c, "Value", GetPropStr(target, prop));
	}
	else						/* Checkbox / Textbox */
	{
		Connect(c, "Value", target, prop);
		Ollama_Reflect(target, prop, c, "In");
	}
}

static NodeObj Ollama_SubPanel(NodeObj panel, char *name, int x, int y, int w, int h)
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

int Ollama_OnHelpOpen(NodeObj view, MsgId message, NodeObj data)
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

	md = Ollama_ReadFile("objects/ollama/README.md");
	SetPropStr(box, "Value", md ? md : "");
	if (md)
		free(md);
	return rtrn_handled;
}

/* panel 0 = the widget's view, panel 1 = Help */
typedef struct { char *cls, *prop; int x, y, w, h, panel, rows, cols; } OLCtl;

static OLCtl OllamaPanel[] = {
	{ "Checkbox", "Enable",     455,  12,   9,   9, 0,  0,  0 },
	{ "Textbox",  "Server",      15,  38, 200,  22, 0,  1, 26 },
	{ "Textbox",  "Port",       225,  38,  70,  22, 0,  1,  7 },
	{ "Textbox",  "Timeout",    305,  38, 120,  22, 0,  1, 11 },
	{ "Textbox",  "Model",       15,  72, 210,  22, 0,  1, 28 },
	{ "Dropdown", "Models",     235,  72, 150,  20, 0,  0,  0 },
	{ "MoButton", "Refresh",    395,  72,  70,  22, 0,  0,  0 },
	{ "Textbox",  "Path",        15, 106, 450,  22, 0,  1, 62 },
	{ "Textbox",  "Payload",     15, 140, 450, 120, 0,  7, 62 },
	{ "MoButton", "Send",        15, 272,  80,  24, 0,  0,  0 },
	{ "TextOut",  "Status",     105, 276, 360,  16, 0,  0,  0 },
	{ "Textbox",  "Output",      15, 305, 450, 150, 0,  9, 62 },

	{ "Markdown", "HelpText",    10,  10, HELP_W - HELP_W_OFF, HELP_H - HELP_H_OFF, 1,  0,  0 },

	{ NULL, NULL, 0, 0, 0, 0, 0, 0, 0 }
};

static void Ollama_BuildPanel(NodeObj instance)
{
	NodeObj sub[2];
	int i;

	sub[0] = instance;
	sub[1] = Ollama_SubPanel(instance, "Help", 15, 462, HELP_W, HELP_H);

	if (sub[1])
	{
		NodeObj openPort = GetPropNode(sub[1], "ReservedViewOpen");
		if (openPort)
			SetPropLong(openPort, "OnMsg", (long)Ollama_OnHelpOpen);
	}

	for (i = 0; OllamaPanel[i].cls; i++)
	{
		OLCtl *t = &OllamaPanel[i];
		NodeObj container = (t->panel >= 0 && t->panel < 2) ? sub[t->panel] : instance;
		if (container)
			Ollama_Ctl(container, instance, t->cls, t->prop,
					   t->x, t->y, t->w, t->h, t->rows, t->cols);
	}
}

static int Ollama_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (local && !local->panelBuilt)
	{
		local->panelBuilt = 1;
		Ollama_BuildPanel(instance);
		Ollama_Activate(instance, msg_initialize, NULL);
		if (local->enabled)			/* fill the model dropdown from the server */
			Ollama_StartModels(instance, local);
	}
	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		if (local->fd >= 0)
			close(local->fd);
		if (local->req)
			free(local->req);
		free(local->resp.b);
		if (local->buildTask)
			RemoveTask(local->buildTask);
		if (local->poll)
			DeleteTask(local->poll);
		free(local);
	}
	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);
	NodeObj entry;

	SetName(class, "Ollama");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Enable", "data", PROP_CHECKBOX, "1");

	entry = PublishProp(ClassSelf, "Server", "data", PROP_TEXTBOX, "192.168.4.253");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 28);
	entry = PublishProp(ClassSelf, "Port", "data", PROP_TEXTBOX, "11434");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 8);
	entry = PublishProp(ClassSelf, "Timeout", "data", PROP_TEXTBOX, "3600");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 12);
	entry = PublishProp(ClassSelf, "Model", "data", PROP_TEXTBOX, "llama3.2");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 28);
	PublishProp(ClassSelf, "Models", "data", PROP_MENU, "");
	PublishProp(ClassSelf, "ModelsList", "data", PROP_NULL, "");
	PublishProp(ClassSelf, "Refresh", "data", PROP_NULL, "0");
	entry = PublishProp(ClassSelf, "Path", "data", PROP_TEXTBOX, "/v1/chat/completions");
	SetPropInt(entry, "Rows", 1);
	SetPropInt(entry, "Cols", 60);
	entry = PublishProp(ClassSelf, "Payload", "data", PROP_TEXTBOX, "Say hello in one sentence.");
	SetPropInt(entry, "Rows", 7);
	SetPropInt(entry, "Cols", 60);

	PublishProp(ClassSelf, "Send", "data", PROP_NULL, "0");
	PublishProp(ClassSelf, "Status", "data", PROP_TEXTBOX, "idle");

	entry = PublishProp(ClassSelf, "Output", "data", PROP_TEXTBOX, "");
	SetPropInt(entry, "Rows", 9);
	SetPropInt(entry, "Cols", 60);

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

	SetName(temp, "Ollama");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "6d007e4d-c7a5-47e9-971b-1e75eb7ad229");
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
