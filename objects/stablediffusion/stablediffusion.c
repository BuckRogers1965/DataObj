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
#include "widget.h"

enum { PH_IDLE, PH_CONN, PH_SEND, PH_RECV };
enum { REQ_TXT2IMG, REQ_MODELS };

typedef struct { char *b; size_t n, cap; } SB;

typedef struct InstanceData
{
	int     enabled;
	int     panelBuilt;
	int     ready;			/* startup settled - only then honor Generate/In */
	TaskObj buildTask;
	TaskObj poll;
	TaskObj retry;
	int     fd;
	int     phase;
	int     reqkind;
	char    promptid[80];
	char   *req;
	int     reqlen, reqsent;
	SB      resp;
	time_t  deadline;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void SD_BuildPanel(NodeObj instance);
static WidgetItem SDPanel[];
static int  SD_BuildTask(NodeObj instance, NodeObj data, int msgid);
static int  SD_Poll(NodeObj instance, NodeObj taskdata, int reason);
static int  SD_Retry(NodeObj instance, NodeObj taskdata, int reason);
static void SD_StartModels(NodeObj instance, InstanceData *local);

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("StableDiffusion handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
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

static char *dupstr(const char *s)
{
	char *b = malloc(strlen(s) + 1);
	strcpy(b, s);
	return b;
}

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
			utf8_put(&o, cp);
			break;
		}
		default:
			if (*q) sbputc(&o, *q++);
		}
	}
	return o.b ? o.b : dupstr("");
}

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

static double json_double(const char *json, const char *key)
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
		return strtod(q, NULL);
	}
	return 0.0;
}

static int ci_find(const char *hay, const char *needle)
{
	size_t nl = strlen(needle);
	for (; *hay; hay++)
	{
		size_t i = 0;
		while (i < nl && hay[i] && (hay[i] | 0x20) == (needle[i] | 0x20))
			i++;
		if (i == nl)
			return 1;
	}
	return 0;
}

static char *dechunk(const char *body)
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
	return o.b ? o.b : dupstr("");
}

static char *escaped_inner(char *s)
{
	char  *esc = JsonEscapeStr(s ? s : "");
	size_t L = strlen(esc);
	char  *inner = malloc(L > 1 ? L - 1 : 1);
	if (L >= 2) { memcpy(inner, esc + 1, L - 2); inner[L - 2] = 0; }
	else inner[0] = 0;
	free(esc);
	return inner;
}

static char *sd_queue_body(NodeObj instance)
{
	char *prompt   = GetPropStr(instance, "Prompt");
	char *negative = GetPropStr(instance, "Negative");
	char *model    = GetPropStr(instance, "Model");
	int   steps    = GetPropInt(instance, "Steps");
	int   width    = GetPropInt(instance, "Width");
	int   height   = GetPropInt(instance, "Height");
	int   cfg      = GetPropInt(instance, "CfgScale");

	if (steps <= 0) steps = 20;
	if (width <= 0) width = 512;
	if (height <= 0) height = 512;
	if (cfg <= 0)   cfg = 7;

	char *pin = escaped_inner(prompt);
	char *nin = escaped_inner(negative);
	char *min = escaped_inner(model);

	SB o;
	o.b = NULL; o.n = o.cap = 0;
	sbputs(&o, "{\"prompt\":\"");
	sbputs(&o, pin);
	sbputs(&o, "\",\"negative_prompt\":\"");
	sbputs(&o, nin);

	char buf[384];
	if (min && min[0])
	{
		snprintf(buf, sizeof(buf),
				 "\",\"steps\":%d,\"width\":%d,\"height\":%d,\"cfg_scale\":%d,\"save_images\":true,"
				 "\"override_settings\":{\"sd_model_checkpoint\":\"%s\"}}",
				 steps, width, height, cfg, min);
	}
	else
	{
		snprintf(buf, sizeof(buf),
				 "\",\"steps\":%d,\"width\":%d,\"height\":%d,\"cfg_scale\":%d,\"save_images\":true}",
				 steps, width, height, cfg);
	}
	sbputs(&o, buf);

	free(pin); free(nin); free(min);
	return o.b ? o.b : dupstr("{}");
}

/* ---- request / socket ---- */

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
						   const char *path, const char *body, int *outlen)
{
	int   plen = body ? (int)strlen(body) : 0;
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
				  method, (path && path[0]) ? path : "/", host, port, plen);

	req = malloc(hl + plen);
	memcpy(req, hdr, hl);
	if (plen)
		memcpy(req + hl, body, plen);
	*outlen = hl + plen;
	return req;
}

static void SD_Reset(InstanceData *local)
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

static void SD_Fail(NodeObj instance, InstanceData *local, char *why)
{
	SetPropStr(instance, "Status", why);
	if (local->poll)  RemoveTask(local->poll);
	if (local->retry) RemoveTask(local->retry);
	SD_Reset(local);
	local->promptid[0] = 0;
}

static void SD_Complete(NodeObj instance, InstanceData *local)
{
	char *r = local->resp.b ? local->resp.b : (char *)"";
	char *body, *decoded = NULL;
	int   chunked = 0;

	body = strstr(r, "\r\n\r\n");
	if (body)
	{
		*body = 0;
		chunked = ci_find(r, "transfer-encoding") && ci_find(r, "chunked");
		*body = '\r';
		body += 4;
	}
	else
		body = r;

	if (chunked)
	{
		decoded = dechunk(body);
		body = decoded;
	}

	if (local->reqkind == REQ_MODELS)
	{
		char *list = json_string_all(body, "title");
		if (!list || !list[0])
		{
			if (list) free(list);
			list = json_string_all(body, "model_name");
		}
		SetPropStr(instance, "Status", "done");
		SetPropStr(instance, "ModelsList", list ? list : "");
		if (list) free(list);
		if (decoded) free(decoded);
		SD_Reset(local);
		return;
	}

	{
		char *host = GetPropStr(instance, "Server");
		int   port = GetPropInt(instance, "Port");
		if (!host || !host[0]) host = "192.168.4.253";
		if (port <= 0) port = 7860;

		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		char date_folder[32];
		strftime(date_folder, sizeof(date_folder), "%Y-%m-%d", tm);

		SB u;
		u.b = NULL; u.n = u.cap = 0;
		sbputs(&u, "http://");
		sbputs(&u, host);
		sbputc(&u, ':');
		{
			char pb[16];
			snprintf(pb, sizeof(pb), "%d", port);
			sbputs(&u, pb);
		}
		
		sbputs(&u, "/file=outputs/txt2img-images/");
		sbputs(&u, date_folder);

		SetPropStr(instance, "Url", u.b);
		SetPropStr(instance, "Status", "done (100%)");

		if (decoded) free(decoded);
		free(u.b);
		SD_Reset(local);
	}
}

static void SD_Begin(NodeObj instance, InstanceData *local, int kind,
                     const char *method, const char *path, const char *body)
{
	struct sockaddr_in addr;
	char *host = GetPropStr(instance, "Server");
	int   port = GetPropInt(instance, "Port");

	if (local->phase != PH_IDLE)
		SD_Reset(local);

	if (!host || !host[0]) host = "192.168.4.253";
	if (port <= 0) port = 7860;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	if (!resolve_into(host, &addr))
	{
		SD_Fail(instance, local, "cannot resolve server");
		return;
	}

	local->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (local->fd < 0)
	{
		SD_Fail(instance, local, "no socket");
		return;
	}
	fcntl(local->fd, F_SETFL, O_NONBLOCK);

	if (connect(local->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
		&& errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		SD_Fail(instance, local, "connect refused");
		return;
	}

	local->reqkind = kind;
	local->req = build_request(method, host, port, path, body, &local->reqlen);
	local->reqsent = 0;
	local->phase = PH_CONN;

	AddTaskMilli(local->poll, 40, (FuncPtr)SD_Poll, msg_send, instance);
}

static int SD_Retry(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	(void) taskdata;

	if (!local || reason == task_deactivate || local->phase == PH_IDLE)
		return rtrn_handled;

	if (time(NULL) >= local->deadline)
	{
		SD_Fail(instance, local, "timed out");
		return rtrn_handled;
	}

	char *host = GetPropStr(instance, "Server");
	int   port = GetPropInt(instance, "Port");
	if (!host || !host[0]) host = "192.168.4.253";
	if (port <= 0) port = 7860;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);

	if (resolve_into(host, &addr))
	{
		int pfd = socket(AF_INET, SOCK_STREAM, 0);
		if (pfd >= 0)
		{
			fcntl(pfd, F_SETFL, O_NONBLOCK);
			if (connect(pfd, (struct sockaddr *)&addr, sizeof(addr)) == 0 || errno == EINPROGRESS)
			{
				int reqlen = 0;
				char *req = build_request("GET", host, port, "/sdapi/v1/progress?skip_current_image=true", "", &reqlen);
				
				fd_set w, r;
				struct timeval tv;
				tv.tv_sec = 0; tv.tv_usec = 100000;
				
				FD_ZERO(&w); FD_SET(pfd, &w);
				if (select(pfd + 1, NULL, &w, NULL, &tv) > 0)
				{
					send(pfd, req, reqlen, 0);
					FD_ZERO(&r); FD_SET(pfd, &r);
					tv.tv_sec = 0; tv.tv_usec = 100000;
					if (select(pfd + 1, &r, NULL, NULL, &tv) > 0)
					{
						char pbuf[2048];
						int k = recv(pfd, pbuf, sizeof(pbuf) - 1, 0);
						if (k > 0)
						{
							pbuf[k] = 0;
							double prog = json_double(pbuf, "progress");
							int pct = (int)(prog * 100.0);
							if (pct < 0) pct = 0;
							if (pct > 100) pct = 100;

							char stbuf[64];
							snprintf(stbuf, sizeof(stbuf), "rendering... %d%%", pct);
							SetPropStr(instance, "Status", stbuf);
						}
					}
				}
				free(req);
			}
			close(pfd);
		}
	}

	if (local->phase != PH_IDLE)
	{
		AddTaskMilli(local->retry, 1000, (FuncPtr)SD_Retry, msg_send, instance);
	}
	return rtrn_handled;
}

static int SD_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char buf[4096];

	(void) taskdata;

	if (!local || local->phase == PH_IDLE || reason == task_deactivate)
		return rtrn_handled;

	if (time(NULL) >= local->deadline)
	{
		SD_Fail(instance, local, "timed out");
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
				SD_Fail(instance, local, "connect failed");
				return rtrn_handled;
			}
			local->phase = PH_SEND;
			SetPropStr(instance, "Status",
					   local->reqkind == REQ_TXT2IMG ? "submitting" : "checking models");
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
				SD_Fail(instance, local, "send failed");
				return rtrn_handled;
			}
		}
		if (local->reqsent == local->reqlen)
		{
			local->phase = PH_RECV;
			SetPropStr(instance, "Status",
					   local->reqkind == REQ_TXT2IMG ? "rendering... 0%" : "fetching models");
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
				SD_Complete(instance, local);
				return rtrn_handled;
			}
			else if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				SD_Fail(instance, local, "read failed");
				return rtrn_handled;
			}
		}
	}

	AddTaskMilli(local->poll, 40, (FuncPtr)SD_Poll, msg_send, instance);
	return rtrn_handled;
}

static void SD_Generate(NodeObj instance, InstanceData *local)
{
	char *body = sd_queue_body(instance);
	int   timeout = GetPropInt(instance, "Timeout");

	if (timeout <= 0) timeout = 36000;
	local->promptid[0] = 0;
	local->reqkind = REQ_TXT2IMG;
	local->deadline = time(NULL) + timeout;

	SetPropStr(instance, "Status", "connecting");
	SD_Begin(instance, local, REQ_TXT2IMG, "POST", "/sdapi/v1/txt2img", body);
	free(body);

	AddTaskMilli(local->retry, 1000, (FuncPtr)SD_Retry, msg_send, instance);
}

static void SD_StartModels(NodeObj instance, InstanceData *local)
{
	SD_Begin(instance, local, REQ_MODELS, "GET", "/sdapi/v1/sd-models", "");
}

/* ---- handlers ---- */

int SD_OnGenerate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->ready || !local->enabled)
		return rtrn_handled;
	SD_Generate(instance, local);
	return rtrn_handled;
}

int SD_OnRefresh(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->enabled)
		return rtrn_handled;
	SD_StartModels(instance, local);
	return rtrn_handled;
}

int SD_OnModels(NodeObj instance, MsgId message, NodeObj data)
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

int SD_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *p, vpath[256], boxpath[320];
	NodeObj box;

	if (!local || message == msg_eof)
		return rtrn_handled;
	if (!local->ready || !local->enabled)
		return rtrn_handled;

	p = GetValueStr(data);
	SetValueStr(GetPropNode(instance, "Prompt"), p ? p : "");
	if (p && PathOfInstance(instance, vpath, sizeof(vpath)))
	{
		snprintf(boxpath, sizeof(boxpath), "%s/Prompt", vpath);
		box = ResolvePath(boxpath);
		if (box)
			SetOrDeliverProp(box, "In", p);
	}
	SD_Generate(instance, local);
	return rtrn_handled;
}

int SD_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)
		return rtrn_handled;
	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	if (!local->enabled)
		SD_Fail(instance, local, "cancelled");
	return rtrn_handled;
}

int SD_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		SD_BuildPanel(instance);
	}
	return rtrn_handled;
}

/* ---- lifecycle ---- */

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->enabled = 1;
	local->panelBuilt = 0;
	local->ready = 0;
	local->buildTask = NULL;
	local->poll = NULL;
	local->retry = NULL;
	local->fd = -1;
	local->phase = PH_IDLE;
	local->reqkind = REQ_TXT2IMG;
	local->promptid[0] = 0;
	local->req = NULL;
	local->reqlen = local->reqsent = 0;
	local->resp.b = NULL;
	local->resp.n = local->resp.cap = 0;
	local->deadline = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "StableDiffusion");

	Widget_Init(instance, SDPanel);

	SetPropStr(instance, "ModelsList", "");
	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)SD_Activate);

	Widget_Port(instance, "In", "", (void *)SD_OnIn);

	InitPosition(instance);
	Widget_MainSize(instance, SDPanel);

	RegisterInstance(class, instance);

	local->poll = CreateTask(ObjGetTaskList());
	local->retry = CreateTask(ObjGetTaskList());
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)SD_BuildTask, msg_send, instance);

	return rtrn_handled;
}

static WidgetItem SDPanel[] = {
	/* cls        prop        def   panel   x    y    w    h  label       [handler] */
	{ "View", "StableDiffusion", "", 0, 0, 0, 560, 420, 0 },					/* 0: main */
	{ "Help", "objects/stablediffusion/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */
	{ "View", "Settings", "", 0,  95, 356, 560, 440, 0 },					/* 2: settings */

	/* --- main (0) --- */
	{ "Checkbox", "Enable",   "1",   0, 490,  12,   9,  9, LABEL_LEFT, (void *)SD_OnEnable },
	{ "Textbox",  "Prompt",   "a cat wearing a wizard hat, digital art", 0, 15, 36, 490, 70, LABEL_NONE },
	{ "Textbox",  "Negative", "",    0,  15, 114, 490, 70, LABEL_NONE },
	{ "MoButton", "Generate", "0",   0,  15, 192,  90, 24, LABEL_NONE, (void *)SD_OnGenerate },
	{ "TextOut",  "Status",   "idle",0, 115, 196, 390, 16, LABEL_NONE },
	{ "Textbox",  "Url",      "",    0,  15, 226, 490, 70, LABEL_NONE },

	/* --- settings (2) --- */
	{ "Textbox",  "Server",   "192.168.4.253", 2,  15, 15, 200, 22, LABEL_NONE },
	{ "Textbox",  "Port",     "7860",      2, 225, 15,  70, 22, LABEL_NONE },
	{ "Textbox",  "Timeout",  "36000",     2, 305, 15, 100, 22, LABEL_NONE },
	{ "Textbox",  "Model",    "",          2,  15, 50, 200, 22, LABEL_NONE },
	{ "Dropdown", "Models",   "",          2, 225, 50, 150, 20, LABEL_NONE, (void *)SD_OnModels },
	{ "MoButton", "Refresh",  "0",         2, 385, 50,  70, 22, LABEL_NONE, (void *)SD_OnRefresh },
	{ "Textbox",  "Steps",    "20",        2,  15, 85,  70, 22, LABEL_NONE },
	{ "Textbox",  "Width",    "512",       2,  95, 85,  70, 22, LABEL_NONE },
	{ "Textbox",  "Height",   "512",       2, 175, 85,  70, 22, LABEL_NONE },
	{ "Textbox",  "CfgScale", "7",         2, 255, 85,  70, 22, LABEL_NONE },

	{ NULL }
};

static void SD_BuildPanel(NodeObj instance)
{
	Widget_BuildTable(instance, SDPanel);
}

static int SD_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (!local)
		return rtrn_handled;
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		SD_BuildPanel(instance);
		SD_Activate(instance, msg_initialize, NULL);
		if (local->enabled)
			SD_StartModels(instance, local);
		AddTaskMilli(local->buildTask, 300, (FuncPtr)SD_BuildTask, msg_send, instance);
	}
	else
		local->ready = 1;
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
		if (local->retry)
			DeleteTask(local->retry);
		free(local);
	}
	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "StableDiffusion");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	Widget_Publish(ClassSelf, SDPanel);

	PublishProp(ClassSelf, "ModelsList", "data", PROP_NULL, "");
	PublishProp(ClassSelf, "In",         "data", PROP_NULL, "");
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

	srand((unsigned)time(NULL));
	SetName(temp, "StableDiffusion");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "72f05bfb-5677-4254-a9a0-bb35d388f999");
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
