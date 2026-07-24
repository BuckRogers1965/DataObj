
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

/* ComfyUI: submit a workflow (with %%prompt%% replaced by the prompt) to a
   ComfyUI server, poll until the image is rendered, and publish the /view URL
   of the result in Url. Sockets are non-blocking, driven by a poll task, so a
   long render never stalls the core; Timeout (seconds) bounds the wait. */

enum { PH_IDLE, PH_CONN, PH_SEND, PH_RECV };
enum { ST_QUEUE, ST_HISTORY };

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
	int     step;
	char    promptid[80];
	char   *req;
	int     reqlen, reqsent;
	SB      resp;
	time_t  deadline;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static void Comfy_BuildPanel(NodeObj instance);
static WidgetItem ComfyPanel[];
static int  Comfy_BuildTask(NodeObj instance, NodeObj data, int msgid);
static int  Comfy_Poll(NodeObj instance, NodeObj taskdata, int reason);
static int  Comfy_Retry(NodeObj instance, NodeObj taskdata, int reason);
static void Comfy_StartHistory(NodeObj instance, InstanceData *local);

int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint("ComfyUI handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
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
			utf8_put(&o, cp);
			break;
		}
		default:
			if (*q) sbputc(&o, *q++);
		}
	}
	return o.b ? o.b : dupstr("");
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
		p += strlen(pat);
	}
	return NULL;
}

/* case-insensitive substring search */
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

/* de-chunk a Transfer-Encoding: chunked body (malloc'd; caller frees) */
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

/* percent-encode a query value (malloc'd) */
static char *urlenc(const char *s)
{
	SB o;
	o.b = NULL; o.n = o.cap = 0;
	for (; s && *s; s++)
	{
		unsigned char c = *s;
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z') || c == '-' || c == '_'
			|| c == '.' || c == '~')
			sbputc(&o, (char)c);
		else
		{
			char h[4];
			snprintf(h, sizeof(h), "%%%02X", c);
			sbputs(&o, h);
		}
	}
	return o.b ? o.b : dupstr("");
}

/* replace every occurrence of tag in hay with rep (malloc'd) */
static char *replace_all(const char *hay, const char *tag, const char *rep)
{
	SB o;
	size_t tl = strlen(tag);
	o.b = NULL; o.n = o.cap = 0;

	while (*hay)
	{
		if (!strncmp(hay, tag, tl))
		{
			sbputs(&o, rep);
			hay += tl;
		}
		else
			sbputc(&o, *hay++);
	}
	return o.b ? o.b : dupstr("");
}

/* a JSON string's value with the surrounding quotes stripped, for splicing
   into an existing "..." in the workflow */
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

/* a fresh large positive seed */
static long long new_seed(void)
{
	long long s = ((long long)rand() << 33) ^ ((long long)rand() << 12) ^ rand();
	if (s < 0) s = -s;
	return s % 1000000000000000LL;
}

/* is p at a "seed" / "noise_seed" key? sets its length */
static int seed_key_at(const char *p, int *kl)
{
	if (!strncmp(p, "\"seed\"", 6))        { *kl = 6;  return 1; }
	if (!strncmp(p, "\"noise_seed\"", 12)) { *kl = 12; return 1; }
	return 0;
}

/* every "seed": N in the workflow gets a new random N - a loop, since a flow
   can hold several samplers */
static char *replace_seeds(const char *wf)
{
	SB o;
	o.b = NULL; o.n = o.cap = 0;

	while (*wf)
	{
		int kl;
		if (seed_key_at(wf, &kl))
		{
			const char *v = wf + kl;
			while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
			if (*v == ':')
			{
				const char *num = v + 1;
				while (*num == ' ' || *num == '\t' || *num == '\n' || *num == '\r') num++;
				if (*num == '-' || (*num >= '0' && *num <= '9'))
				{
					const char *end = num;
					char sb[24];
					if (*end == '-') end++;
					while (*end >= '0' && *end <= '9') end++;
					sbput(&o, wf, num - wf);		/* key, colon, spaces */
					snprintf(sb, sizeof(sb), "%lld", new_seed());
					sbputs(&o, sb);
					wf = end;
					continue;
				}
			}
		}
		sbputc(&o, *wf++);
	}
	return o.b ? o.b : dupstr("");
}

/* {"prompt": <workflow, tags spliced, seeds randomized if asked>} */
static char *comfy_queue_body(char *workflow, char *prompt, char *negative, int randomize)
{
	char *pin = escaped_inner(prompt);
	char *nin = escaped_inner(negative);
	char *w1 = replace_all(workflow ? workflow : "{}", "%%prompt%%", pin);
	char *w2 = replace_all(w1, "%%negative%%", nin);
	char *w3 = randomize ? replace_seeds(w2) : dupstr(w2);
	SB    o;

	free(pin); free(nin); free(w1); free(w2);

	o.b = NULL; o.n = o.cap = 0;
	sbputs(&o, "{\"prompt\":");
	sbputs(&o, w3);
	sbputc(&o, '}');
	free(w3);
	return o.b ? o.b : dupstr("{\"prompt\":{}}");
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

/* drop the socket and buffers, back to idle (pipeline state is left alone) */
static void Comfy_Reset(InstanceData *local)
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

/* end the whole pipeline with a status message */
static void Comfy_Fail(NodeObj instance, InstanceData *local, char *why)
{
	SetPropStr(instance, "Status", why);
	if (local->poll)  RemoveTask(local->poll);	/* kill any pending poll/retry */
	if (local->retry) RemoveTask(local->retry);
	Comfy_Reset(local);
	local->promptid[0] = 0;
}

/* one request finished (peer closed): act on the reply for the current step */
static void Comfy_Complete(NodeObj instance, InstanceData *local)
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

	if (local->step == ST_QUEUE)
	{
		char *pid = json_string(body, "prompt_id");
		if (pid)
		{
			snprintf(local->promptid, sizeof(local->promptid), "%s", pid);
			free(pid);
			if (decoded) free(decoded);
			Comfy_Reset(local);
			SetPropStr(instance, "Status", "queued");
			Comfy_StartHistory(instance, local);
		}
		else
		{
			/* no prompt_id: ComfyUI refused it - the reason goes to Status
			   (the specific node_errors detail if any, else the message). */
			char *ne  = strstr(body, "node_errors");
			char *det = ne ? json_string(ne, "details") : NULL;
			char *msg = json_string(body, "message");
			Comfy_Fail(instance, local,
					   (det && det[0]) ? det : (msg ? msg : "server rejected the workflow"));
			free(det); free(msg);
			if (decoded) free(decoded);
		}
		return;
	}

	/* ST_HISTORY: the image is ready once a filename shows up */
	{
		char *fn = json_string(body, "filename");
		char *sf = json_string(body, "subfolder");
		char *ty = json_string(body, "type");
		if (decoded) free(decoded);
		Comfy_Reset(local);

		if (fn)
		{
			char *ef = urlenc(fn);
			char *host = GetPropStr(instance, "Server");
			int   port = GetPropInt(instance, "Port");
			SB    u;

			u.b = NULL; u.n = u.cap = 0;
			sbputs(&u, "http://");
			sbputs(&u, host ? host : "127.0.0.1");
			sbputc(&u, ':');
			{
				char pb[16];
				snprintf(pb, sizeof(pb), "%d", port ? port : 8188);
				sbputs(&u, pb);
			}
			sbputs(&u, "/view?filename=");
			sbputs(&u, ef);
			if (sf && sf[0])			/* only when the image is in a subfolder */
			{
				char *es = urlenc(sf);
				sbputs(&u, "&subfolder=");
				sbputs(&u, es);
				free(es);
			}
			/* no &type=: ComfyUI defaults to "output" (what a browser can
			   fetch); a workflow's own type such as flux2 would 404 */

			SetPropStr(instance, "Url", u.b);
			SetPropStr(instance, "Status", "done");

			free(u.b); free(ef);
			free(fn); free(sf); free(ty);
			local->promptid[0] = 0;
		}
		else
		{
			free(fn); free(sf); free(ty);
			SetPropStr(instance, "Status", "rendering...");
			AddTaskMilli(local->retry, 1000, (FuncPtr)Comfy_Retry, msg_send, instance);
		}
	}
}

/* resolve, non-blocking connect, arm the poll for one request */
static void Comfy_Begin(NodeObj instance, InstanceData *local,
						const char *method, const char *path, const char *body)
{
	struct sockaddr_in addr;
	char *host = GetPropStr(instance, "Server");
	int   port = GetPropInt(instance, "Port");

	if (local->phase != PH_IDLE)
		Comfy_Reset(local);

	if (!host || !host[0]) host = "127.0.0.1";
	if (port <= 0) port = 8188;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	if (!resolve_into(host, &addr))
	{
		Comfy_Fail(instance, local, "cannot resolve server");
		return;
	}

	local->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (local->fd < 0)
	{
		Comfy_Fail(instance, local, "no socket");
		return;
	}
	fcntl(local->fd, F_SETFL, O_NONBLOCK);

	if (connect(local->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
		&& errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		Comfy_Fail(instance, local, "connect refused");
		return;
	}

	local->req = build_request(method, host, port, path, body, &local->reqlen);
	local->reqsent = 0;
	local->phase = PH_CONN;

	AddTaskMilli(local->poll, 40, (FuncPtr)Comfy_Poll, msg_send, instance);
}

/* GET /history/<id> to see whether the render has finished */
static void Comfy_StartHistory(NodeObj instance, InstanceData *local)
{
	char path[128];
	snprintf(path, sizeof(path), "/history/%s", local->promptid);
	local->step = ST_HISTORY;
	Comfy_Begin(instance, local, "GET", path, "");
}

/* retry task: after the 1s gap, poll /history again (or give up on timeout) */
static int Comfy_Retry(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) taskdata;

	if (!local || reason == task_deactivate || !local->promptid[0])
		return rtrn_handled;
	if (time(NULL) >= local->deadline)
	{
		SetPropStr(instance, "Status", "timed out");
		local->promptid[0] = 0;
		return rtrn_handled;
	}
	Comfy_StartHistory(instance, local);
	return rtrn_handled;
}

static int Comfy_Poll(NodeObj instance, NodeObj taskdata, int reason)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char buf[4096];

	(void) taskdata;

	if (!local || local->phase == PH_IDLE || reason == task_deactivate)
		return rtrn_handled;

	if (time(NULL) >= local->deadline)
	{
		Comfy_Fail(instance, local, "timed out");
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
				Comfy_Fail(instance, local, "connect failed");
				return rtrn_handled;
			}
			local->phase = PH_SEND;
			SetPropStr(instance, "Status",
					   local->step == ST_QUEUE ? "submitting" : "checking");
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
				Comfy_Fail(instance, local, "send failed");
				return rtrn_handled;
			}
		}
		if (local->reqsent == local->reqlen)
			local->phase = PH_RECV;
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
				Comfy_Complete(instance, local);
				return rtrn_handled;
			}
			else if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				Comfy_Fail(instance, local, "read failed");
				return rtrn_handled;
			}
		}
	}

	AddTaskMilli(local->poll, 40, (FuncPtr)Comfy_Poll, msg_send, instance);
	return rtrn_handled;
}

/* start the pipeline: queue the workflow, then poll history */
static void Comfy_Generate(NodeObj instance, InstanceData *local)
{
	char *body = comfy_queue_body(GetPropStr(instance, "Workflow"),
								  GetPropStr(instance, "Prompt"),
								  GetPropStr(instance, "Negative"),
								  GetPropInt(instance, "Randomize"));
	int   timeout = GetPropInt(instance, "Timeout");

	if (timeout <= 0) timeout = 36000;
	local->promptid[0] = 0;
	local->step = ST_QUEUE;
	local->deadline = time(NULL) + timeout;
	/* leave Url alone - it feeds the image, which should keep showing the
	   previous result until the new render replaces it */
	SetPropStr(instance, "Status", "connecting");
	Comfy_Begin(instance, local, "POST", "/prompt", body);
	free(body);
}

/* ---- handlers ---- */

/* Generate: a 1 fires a render from the current Prompt */
int Comfy_OnGenerate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof || !GetValueInt(data))
		return rtrn_handled;
	if (!local->ready)
	{
		DebugPrint("ComfyUI: ignored a Generate before settle (startup)",
				   __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}
	if (!local->enabled)
		return rtrn_handled;
	Comfy_Generate(instance, local);
	return rtrn_handled;
}

/* In: a prompt arrived from a wire - show it, then render */
int Comfy_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
	char *p, vpath[256], boxpath[320];
	NodeObj box;

	if (!local || message == msg_eof)
		return rtrn_handled;
	if (!local->ready)
	{
		DebugPrint("ComfyUI: ignored an In before settle (startup)",
				   __FILE__, __LINE__, OBJMSGHANDLING);
		return rtrn_handled;
	}
	if (!local->enabled)
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
	Comfy_Generate(instance, local);
	return rtrn_handled;
}

int Comfy_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message == msg_eof)		/* accept the toggle however it arrives */
		return rtrn_handled;
	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");
	if (!local->enabled)					/* disabling always stops and clears */
		Comfy_Fail(instance, local, "cancelled");
	return rtrn_handled;
}

int Comfy_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		Comfy_BuildPanel(instance);
	}
	return rtrn_handled;
}

/* ---- lifecycle ---- */

static char DEFAULT_WORKFLOW[] =
	"{\n"
	"  \"3\": {\"class_type\": \"KSampler\", \"inputs\": {\"seed\": 42, \"steps\": 20,\n"
	"    \"cfg\": 7, \"sampler_name\": \"euler\", \"scheduler\": \"normal\", \"denoise\": 1,\n"
	"    \"model\": [\"4\", 0], \"positive\": [\"6\", 0], \"negative\": [\"7\", 0],\n"
	"    \"latent_image\": [\"5\", 0]}},\n"
	"  \"4\": {\"class_type\": \"CheckpointLoaderSimple\",\n"
	"    \"inputs\": {\"ckpt_name\": \"zavychromaxl_v21.safetensors\"}},\n"
	"  \"5\": {\"class_type\": \"EmptyLatentImage\",\n"
	"    \"inputs\": {\"width\": 512, \"height\": 512, \"batch_size\": 1}},\n"
	"  \"6\": {\"class_type\": \"CLIPTextEncode\",\n"
	"    \"inputs\": {\"text\": \"%%prompt%%\", \"clip\": [\"4\", 1]}},\n"
	"  \"7\": {\"class_type\": \"CLIPTextEncode\", \"inputs\": {\"text\": \"%%negative%%\", \"clip\": [\"4\", 1]}},\n"
	"  \"8\": {\"class_type\": \"VAEDecode\", \"inputs\": {\"samples\": [\"3\", 0], \"vae\": [\"4\", 2]}},\n"
	"  \"9\": {\"class_type\": \"SaveImage\",\n"
	"    \"inputs\": {\"filename_prefix\": \"ComfyUI\", \"images\": [\"8\", 0]}}\n"
	"}";

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
	local->step = ST_QUEUE;
	local->promptid[0] = 0;
	local->req = NULL;
	local->reqlen = local->reqsent = 0;
	local->resp.b = NULL;
	local->resp.n = local->resp.cap = 0;
	local->deadline = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "ComfyUI");

	/* every control's value + handler from the table (Enable/Generate carry a
	   handler; Server/Port/Timeout/Prompt/Negative/Randomize/Workflow/Url/Status
	   are plain data) */
	Widget_Init(instance, ComfyPanel);

	SetPropInt(instance, "State", Starting);
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Comfy_Activate);

	Widget_Port(instance, "In", "", (void *)Comfy_OnIn);	/* wire input, no control */

	InitPosition(instance);
	Widget_MainSize(instance, ComfyPanel);		/* main size before any subscribe */

	RegisterInstance(class, instance);

	local->poll = CreateTask(ObjGetTaskList());
	local->retry = CreateTask(ObjGetTaskList());
	local->buildTask = CreateTask(ObjGetTaskList());
	AddTaskMilli(local->buildTask, 1, (FuncPtr)Comfy_BuildTask, msg_send, instance);

	return rtrn_handled;
}

/* The whole widget in one table: main view, Help, the Settings sub-view, and
   every control (value +, for the ports, handler). In and State are apart. */
static WidgetItem ComfyPanel[] = {
	/* cls        prop        def   panel   x    y    w    h  label       [handler] */
	{ "View", "ComfyUI",  "", 0,   0,   0, 560, 420, 0 },					/* 0: main */
	{ "Help", "objects/comfyui/README.md", "", 0, 0, 0, 0, 0, 0 },			/* 1: help */
	{ "View", "Settings", "", 0,  95, 356, 560, 440, 0 },					/* 2: settings */

	/* --- main (0) --- */
	{ "Checkbox", "Enable",   "1",   0, 490,  12,   9,  9, LABEL_LEFT, (void *)Comfy_OnEnable },
	{ "Textbox",  "Prompt",   "a cat wearing a wizard hat, digital art", 0, 15, 36, 490, 70, LABEL_NONE },
	{ "Textbox",  "Negative", "",    0,  15, 114, 490, 70, LABEL_NONE },
	{ "MoButton", "Generate", "0",   0,  15, 192,  90, 24, LABEL_NONE, (void *)Comfy_OnGenerate },
	{ "TextOut",  "Status",   "idle",0, 115, 196, 390, 16, LABEL_NONE },
	{ "Textbox",  "Url",      "",    0,  15, 226, 490, 70, LABEL_NONE },

	/* --- settings (2) --- */
	{ "Textbox",  "Server",    "192.168.4.251", 2,  15, 15, 200, 22, LABEL_NONE },
	{ "Textbox",  "Port",      "8188",          2, 225, 15,  70, 22, LABEL_NONE },
	{ "Textbox",  "Timeout",   "36000",         2, 305, 15, 150, 22, LABEL_NONE },
	{ "Checkbox", "Randomize", "1",             2,  15, 52,   9,  9, LABEL_NONE },
	{ "Textbox",  "Workflow",  DEFAULT_WORKFLOW,2,  15, 82, 490, 300, LABEL_NONE },

	{ NULL }
};

static void Comfy_BuildPanel(NodeObj instance)
{
	/* main view, Help, the Settings sub-view, and every control - all from the
	   one table, walked by Widget_BuildTable */
	Widget_BuildTable(instance, ComfyPanel);
}

static int Comfy_BuildTask(NodeObj instance, NodeObj data, int msgid)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	(void) data;
	(void) msgid;

	if (!local)
		return rtrn_handled;
	if (!local->panelBuilt)
	{
		local->panelBuilt = 1;
		Comfy_BuildPanel(instance);
		Comfy_Activate(instance, msg_initialize, NULL);
		/* settle: honor Generate/In only AFTER any startup deliveries have
		   drained, so the widget never renders on its own at creation */
		AddTaskMilli(local->buildTask, 300, (FuncPtr)Comfy_BuildTask, msg_send, instance);
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

	SetName(class, "ComfyUI");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (widget type from each control's class) */
	Widget_Publish(ClassSelf, ComfyPanel);

	/* the wire input and the lifecycle state - no on-screen control */
	PublishProp(ClassSelf, "In",    "data", PROP_NULL, "");
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

	srand((unsigned)time(NULL));		/* seed the seed randomizer */
	SetName(temp, "ComfyUI");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "81f05bfb-5677-4254-a9a0-bb35d388f892");
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
