
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "dyn/buff.h"

/*

WebSocket object: handshake + framing on top of the TCP object, so a
plain browser WebSocket client can talk to whatever is wired to its
app-facing side (the Bridge, most naturally).

Four ports instead of the usual two, because unlike Filter/Http (one
request in triggers one response out) a WebSocket has to relay two
independent streams at once - what TCP hands it and what the app hands
it. TCP itself still only ever sees bytes in, bytes out:

    Wire (in)   raw bytes from TCP - the handshake request, then framed
                client messages
    Send (out)  raw bytes to TCP - the handshake response, then framed
                server messages
    In   (in)   plain text from the app, to be framed and sent to Send
    Out  (out)  plain text unframed from Wire, handed to the app

Wire it as:

    Connect(Tcp, "Out", WebSocket, "Wire")
    Connect(WebSocket, "Send", Tcp, "In")
    Connect(WebSocket, "Out", Bridge, "In")
    Connect(Bridge, "Out", WebSocket, "In")

and the Bridge is none the wiser whether it is being driven by raw TCP
or a browser's WebSocket - same "In subscribes, Out replies" shape it
already uses.

Handshake: the first message on Wire is expected to be the browser's
HTTP upgrade request. The Sec-WebSocket-Key header is pulled out,
combined with the RFC 6455 magic GUID, SHA-1 hashed and base64 encoded
into Sec-WebSocket-Accept - no external crypto library, both are
implemented below and checked against the RFC's own worked example.

Frames: unfragmented (FIN=1) frames only, 7-bit or 16-bit extended length
(up to 65535 bytes) on both receive and send - a palette's published
interfaces routinely run well past the 125-byte 7-bit limit, caught by
testing the real palette load, not a synthetic small message. All complete
frames in one Wire message are processed, not just the first - a client firing
off several small sends back to back routinely lands as multiple frames
in one TCP recv, and only handling the first one silently dropped the rest.

A frame split across two recvs is buffered and completed, not dropped -
each Conn gets its own accumulating buff (connRecvBufs, WS_GetConnBuf):
every Wire message's bytes are appended, as many complete frames as are
available get parsed, and whatever incomplete tail remains (a frame that
straddles the boundary between this recv and the next) stays in the
buffer for the following Wire message to finish. This was assumed "rare
enough for small JSON payloads" until View-based rendering (object.c)
made a fresh connection's own replay send 200+ subscribe commands back
to back - routine at that volume, not an edge case, and it was silently
dropping some of them (a connecting client's own Palette contents
"falling out" onto the top-level canvas - the subscribe for their
Container property never arrived intact). Ping/pong/close and 64-bit
extended length are still not implemented; a close frame is just dropped
(TCP owns the socket and closes on its own when the peer does). This
covers what a plain browser client sends for small JSON commands and
events - exactly the Bridge's traffic - not a general WebSocket stack.

This is also the first object that actually needs Phase 1.2's
binary-safe payloads: a masking key is 4 essentially random bytes, so
about 1.5% of frames would have silently truncated at an embedded NUL
without SetValueStrLen/the Length property carrying the real byte count.

TCP now services any number of simultaneous connections (see tcp.c),
each message tagged with a Conn id, so the handshake-done flag has to be
tracked per Conn rather than one flag for the whole object - two
browsers connecting at once must not have the second one's still-pending
handshake read as already done because the first one's was. Every send
back out - the handshake response, a framed reply - carries the same
Conn tag the triggering Wire message arrived with, so it reaches only
that one peer; Conn 0 (or absent, the same "everyone" sentinel tcp.c
uses) broadcasts, which is what a Bridge's shared-view event traffic
wants on the app-facing side. One narrow known gap: a broadcast that
lands in the same ~10ms poll tick as a brand new connection's own
handshake could queue ahead of that connection's handshake response in
its send buffer, corrupting its upgrade - rare enough (a connection has
to be accepted and framed data broadcast to everyone inside the same
tick) to accept as a reconnect-and-retry edge case rather than engineer
around here.

*/

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct InstanceData
{
	int active;
	int enabled;
	NodeObj connHandshakes;	/* Conn id -> handshake done (0/1), see GetConnState/SetConnState */
	NodeObj connRecvBufs;	/* Conn id -> buff* (cast through GetConnState/SetConnState's long), see WS_GetConnBuf */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "WebSocket handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* ---- SHA-1 (single-shot, no streaming) and Base64, no external library ---- */
/* verified against the RFC 6455 worked example: key "dGhlIHNhbXBsZSBub25jZQ==" */
/* -> accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="                                     */

void WS_SHA1(const unsigned char *input, size_t inputLen, unsigned char digest[20])
{
	unsigned int h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
	unsigned char *msg;
	unsigned long long ml = (unsigned long long)inputLen * 8;
	size_t newLen, i, chunk;

	newLen = inputLen + 1;
	while (newLen % 64 != 56)
		newLen++;
	newLen += 8;

	msg = calloc(newLen, 1);
	memcpy(msg, input, inputLen);
	msg[inputLen] = 0x80;

	for (i = 0; i < 8; i++)
		msg[newLen - 1 - i] = (unsigned char)(ml >> (8 * i));

	for (chunk = 0; chunk < newLen; chunk += 64)
	{
		unsigned int w[80];
		unsigned int a, b, c, d, e, f, k, temp;
		int t;

		for (t = 0; t < 16; t++)
			w[t] = (msg[chunk + t*4] << 24) | (msg[chunk + t*4 + 1] << 16) |
				   (msg[chunk + t*4 + 2] << 8) | (msg[chunk + t*4 + 3]);
		for (t = 16; t < 80; t++)
		{
			temp = w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16];
			w[t] = (temp << 1) | (temp >> 31);
		}

		a = h0; b = h1; c = h2; d = h3; e = h4;

		for (t = 0; t < 80; t++)
		{
			if (t < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
			else if (t < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
			else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
			else { f = b ^ c ^ d; k = 0xCA62C1D6; }

			temp = ((a << 5) | (a >> 27)) + f + e + k + w[t];
			e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
		}

		h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
	}

	free(msg);

	digest[0]=(h0>>24)&0xFF; digest[1]=(h0>>16)&0xFF; digest[2]=(h0>>8)&0xFF; digest[3]=h0&0xFF;
	digest[4]=(h1>>24)&0xFF; digest[5]=(h1>>16)&0xFF; digest[6]=(h1>>8)&0xFF; digest[7]=h1&0xFF;
	digest[8]=(h2>>24)&0xFF; digest[9]=(h2>>16)&0xFF; digest[10]=(h2>>8)&0xFF; digest[11]=h2&0xFF;
	digest[12]=(h3>>24)&0xFF; digest[13]=(h3>>16)&0xFF; digest[14]=(h3>>8)&0xFF; digest[15]=h3&0xFF;
	digest[16]=(h4>>24)&0xFF; digest[17]=(h4>>16)&0xFF; digest[18]=(h4>>8)&0xFF; digest[19]=h4&0xFF;
}

char *WS_Base64Encode(unsigned char *data, size_t len)
{
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t outLen = 4 * ((len + 2) / 3);
	char *out = malloc(outLen + 1);
	size_t i, j, mod;

	for (i = 0, j = 0; i < len;)
	{
		unsigned int octet_a = i < len ? data[i++] : 0;
		unsigned int octet_b = i < len ? data[i++] : 0;
		unsigned int octet_c = i < len ? data[i++] : 0;
		unsigned int triple = (octet_a << 16) + (octet_b << 8) + octet_c;

		out[j++] = table[(triple >> 18) & 0x3F];
		out[j++] = table[(triple >> 12) & 0x3F];
		out[j++] = table[(triple >> 6) & 0x3F];
		out[j++] = table[triple & 0x3F];
	}

	mod = len % 3;
	if (mod == 1) { out[outLen-1] = '='; out[outLen-2] = '='; }
	else if (mod == 2) { out[outLen-1] = '='; }

	out[outLen] = 0;
	return out;
}

/* ---- handshake ---- */

void WS_SendRaw(NodeObj instance, char *port, char *text, int length, long connId)
{
	NodeObj chunk = NewNode(STRING);
	SetName(chunk, "Frame");
	SetValueStrLen(chunk, text, length);
	SetPropInt(chunk, "Length", length);
	SetPropLong(chunk, "Conn", connId);
	SndMsg(instance, port, msg_send, chunk);
}

int WS_TryHandshake(NodeObj instance, InstanceData *local, char *request, long connId)
{
	char *keyStart, *keyEnd, *response;
	char key[128], combined[200], accept[64];
	unsigned char digest[20];
	char *b64;
	int keyLen, responseLen;

	keyStart = strstr(request, "Sec-WebSocket-Key:");
	if (!keyStart)
		return 0;

	keyStart += strlen("Sec-WebSocket-Key:");
	while (*keyStart == ' ')
		keyStart++;
	keyEnd = strpbrk(keyStart, "\r\n");
	keyLen = keyEnd ? (int)(keyEnd - keyStart) : (int)strlen(keyStart);
	if (keyLen <= 0 || keyLen >= (int)sizeof(key))
		return 0;
	memcpy(key, keyStart, keyLen);
	key[keyLen] = 0;

	snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID);
	WS_SHA1((unsigned char *)combined, strlen(combined), digest);
	b64 = WS_Base64Encode(digest, 20);
	snprintf(accept, sizeof(accept), "%s", b64);
	free(b64);

	response = malloc(256);
	responseLen = snprintf(response, 256,
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n\r\n", accept);

	WS_SendRaw(instance, "Send", response, responseLen, connId);
	free(response);

	SetConnState(local->connHandshakes, connId, 1);
	return 1;
}

/* ---- frames ---- */

/*
 * A TCP recv is one message here, but a client sending several small
 * frames back to back (completely ordinary - e.g. a browser firing off
 * a handful of bridge commands in response to one user action) very
 * commonly lands as multiple complete frames in that same recv. Parsing
 * only the first one and dropping the rest is a real bug, not just an
 * edge case - caught by testing rapid successive sends. The caller
 * (WS_OnWire) is what handles a frame split across two recvs, by
 * buffering per-Conn and only ever calling this once a frame's worth of
 * bytes might be available - this function itself just needs to keep
 * reporting "not enough yet" (0) accurately.
 *
 * Client -> server frames are always masked. Returns the number of
 * bytes the frame occupied so the caller can advance to the next one,
 * 0 if buf doesn't hold a complete frame yet, -1 if it's a shape this
 * parser doesn't support (64-bit extended length).
 */
int WS_HandleOneFrame(NodeObj instance, unsigned char *buf, int len, long connId)
{
	int opcode, masked, payloadLen, offset, i;
	unsigned char mask[4];
	char *payload;

	if (len < 2)
		return 0;

	opcode = buf[0] & 0x0F;
	masked = (buf[1] & 0x80) != 0;
	payloadLen = buf[1] & 0x7F;
	offset = 2;

	if (payloadLen == 126)
	{
		if (len < 4)
			return 0;
		payloadLen = (buf[2] << 8) | buf[3];
		offset = 4;
	}
	else if (payloadLen == 127)
	{
		return -1;	/* 64-bit extended length not supported - out of scope here */
	}

	if (masked)
	{
		if (len < offset + 4)
			return 0;
		memcpy(mask, buf + offset, 4);
		offset += 4;
	}

	if (len < offset + payloadLen)
		return 0;

	if (opcode == 0x1)
	{
		payload = malloc(payloadLen + 1);
		for (i = 0; i < payloadLen; i++)
			payload[i] = masked ? (buf[offset + i] ^ mask[i % 4]) : buf[offset + i];
		payload[payloadLen] = 0;

		WS_SendRaw(instance, "Out", payload, payloadLen, connId);

		free(payload);
	}
	/* close/ping/pong/binary: not implemented, silently skipped */

	return offset + payloadLen;
}

/* returns the number of bytes at the front of buf that were consumed by  */
/* complete frames - always < len unless every byte was used, since a      */
/* trailing partial frame (or a shape WS_HandleOneFrame won't parse) stops */
/* the walk without consuming it, exactly like it always has              */
int WS_HandleFrames(NodeObj instance, unsigned char *buf, int len, long connId)
{
	int pos = 0, consumed;

	while (pos < len)
	{
		consumed = WS_HandleOneFrame(instance, buf + pos, len - pos, connId);
		if (consumed <= 0)
			break;
		pos += consumed;
	}

	return pos;
}

/* one accumulating byte buffer per Conn, created the first time that Conn */
/* sends anything past its handshake - what makes a frame split across two */
/* recvs (WS_OnWire) survive instead of being silently dropped.            */
buff WS_GetConnBuf(InstanceData *local, long connId)
{
	buff b = (buff) GetConnState(local->connRecvBufs, connId);

	if (!b)
	{
		b = buffCreate(4096);
		SetConnState(local->connRecvBufs, connId, (long) b);
	}

	return b;
}

/* subscription callback: raw bytes from TCP - handshake, then frames */
int WS_OnWire(NodeObj instance, MsgId message, NodeObj data)
{
	char *str;
	int len;
	long connId;
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	connId = GetPropLong(data, "Conn");

	if (message == msg_eof)
	{
		/* just this one peer is gone (or, Conn 0, every peer is - the   */
		/* server shut down) - either way the next connection to use a   */
		/* given Conn id (never reused) still needs its own handshake    */
		buff b = (buff) GetConnState(local->connRecvBufs, connId);
		if (b)
		{
			/* Conn ids are never reused, so this buff will never be   */
			/* looked up again - without this every connection left    */
			/* its accumulating recv buffer behind for the life of the */
			/* instance (InstanceEnd was the only thing freeing them)  */
			buffDestroy(b);
			SetConnState(local->connRecvBufs, connId, 0);
		}
		SetConnState(local->connHandshakes, connId, 0);

		/* forward the close out Out tagged with its Conn - eof is just  */
		/* another message, and downstream owns per-connection state too  */
		/* (the Bridge deletes the dead connection's hidden helper        */
		/* widgets when it sees this - see Bridge_ConnClosed, bridge.c)   */
		{
			NodeObj chunk = NewNode(STRING);
			SetName(chunk, "Frame");
			SetPropLong(chunk, "Conn", connId);
			SndMsg(instance, "Out", msg_eof, chunk);
		}
		return rtrn_handled;
	}

	if (message != msg_send)
		return rtrn_dropped;

	str = GetValueStr(data);
	if (!str)
		return rtrn_dropped;

	if (!GetConnState(local->connHandshakes, connId))
	{
		WS_TryHandshake(instance, local, str, connId);
		return rtrn_handled;
	}

	len = GetPropInt(data, "Length");
	if (!len)
		len = strlen(str);

	/* accumulate this Conn's bytes, parse everything complete out of the */
	/* front, and keep only whatever incomplete tail is left (a frame       */
	/* straddling this recv and the next) for next time - see WS_GetConnBuf */
	/* and the doc comment on this object for why this replaced parsing      */
	/* the Wire message in isolation.                                        */
	{
		buff connBuf = WS_GetConnBuf(local, connId);
		char *bufPtr, *leftover;
		unsigned int bufLen;
		int pos;

		buffAdd(connBuf, str, len);

		bufLen = buffGetBuffer(connBuf, &bufPtr);
		pos = bufPtr ? WS_HandleFrames(instance, (unsigned char *) bufPtr, (int) bufLen, connId) : 0;

		if (pos < (int) bufLen)
		{
			unsigned int leftoverLen = bufLen - pos;
			leftover = malloc(leftoverLen);
			memcpy(leftover, bufPtr + pos, leftoverLen);
			buffClear(connBuf);
			buffAdd(connBuf, leftover, leftoverLen);
			free(leftover);
		}
		else
		{
			buffClear(connBuf);
		}
	}

	return rtrn_handled;
}

/* length of the well-formed UTF-8 sequence starting at s (1..4), or 0 if the
   byte at s does not begin a valid one that fits in `avail` bytes. RFC 3629:
   rejects stray continuation bytes, overlong forms, UTF-16 surrogates and
   anything past U+10FFFF. */
static int WS_Utf8Len(const unsigned char *s, int avail)
{
	unsigned char c = s[0];

	if (c < 0x80)
		return 1;
	if (c < 0xC2)
		return 0;					/* 0x80..0xBF stray; 0xC0/0xC1 overlong */
	if (c < 0xE0)					/* 2-byte: C2..DF */
	{
		if (avail < 2) return 0;
		return (s[1] >= 0x80 && s[1] <= 0xBF) ? 2 : 0;
	}
	if (c < 0xF0)					/* 3-byte: E0..EF */
	{
		unsigned char lo = (c == 0xE0) ? 0xA0 : 0x80;	/* E0: no overlong */
		unsigned char hi = (c == 0xED) ? 0x9F : 0xBF;	/* ED: no surrogates */
		if (avail < 3) return 0;
		if (s[1] < lo || s[1] > hi) return 0;
		if (s[2] < 0x80 || s[2] > 0xBF) return 0;
		return 3;
	}
	if (c <= 0xF4)					/* 4-byte: F0..F4 */
	{
		unsigned char lo = (c == 0xF0) ? 0x90 : 0x80;	/* F0: no overlong */
		unsigned char hi = (c == 0xF4) ? 0x8F : 0xBF;	/* F4: <= U+10FFFF */
		if (avail < 4) return 0;
		if (s[1] < lo || s[1] > hi) return 0;
		if (s[2] < 0x80 || s[2] > 0xBF) return 0;
		if (s[3] < 0x80 || s[3] > 0xBF) return 0;
		return 4;
	}
	return 0;						/* F5..FF */
}

/* Build a DISPLAY copy of a frame payload guaranteed to be valid UTF-8. A
   WebSocket text frame carrying an invalid byte MUST close the connection
   (RFC 6455), so a raw byte in a property value - e.g. the binary a Base64
   decode legitimately produces - would drop the whole GUI. Any invalid byte
   becomes U+FFFD here. This copies FOR THE WIRE ONLY: the data model,
   downstream wires, and saved flows keep the exact original bytes - only what
   the browser DISPLAYS is made safe. Caller frees. Worst case is 3x (each bad
   byte -> a 3-byte replacement char). */
static char *WS_Utf8Display(const char *in, int inlen, int *outlen)
{
	const unsigned char *s = (const unsigned char *)in;
	char *out = malloc((size_t)inlen * 3 + 1);
	int i = 0, o = 0, n;

	if (!out)
	{
		*outlen = 0;
		return NULL;
	}
	while (i < inlen)
	{
		n = WS_Utf8Len(s + i, inlen - i);
		if (n > 0)
			while (n--)
				out[o++] = (char)s[i++];
		else
		{
			out[o++] = (char)0xEF;		/* U+FFFD replacement character */
			out[o++] = (char)0xBF;
			out[o++] = (char)0xBD;
			i++;
		}
	}
	out[o] = 0;
	*outlen = o;
	return out;
}

/* subscription callback: plain text from the app - frame it, send it.   */
/* A Conn property on data targets one peer (a request-scoped reply -    */
/* an error, a login ack, the palette dump); absent/0 broadcasts to      */
/* every peer that has finished its handshake, which is what a shared-   */
/* view event (instance-created, property-changed, ...) wants.           */
int WS_OnAppIn(NodeObj instance, MsgId message, NodeObj data)
{
	char *str, *frame, *disp;
	int len, headerLen;
	unsigned char header[4];
	long connId;
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active || !local->enabled)
		return rtrn_dropped;

	if (message != msg_send)
		return rtrn_dropped;

	connId = GetPropLong(data, "Conn");
	if (connId && !GetConnState(local->connHandshakes, connId))
		return rtrn_dropped;	/* that specific peer hasn't finished its handshake yet */

	str = GetValueStr(data);
	if (!str)
		return rtrn_dropped;

	len = GetPropInt(data, "Length");
	if (!len)
		len = strlen(str);

	/* sanitize FOR THE WIRE ONLY - the browser needs a valid-UTF-8 text frame
	   or it drops the connection; the original bytes stay in the data model.
	   The safe copy can be longer, so every length check below is on it. */
	disp = WS_Utf8Display(str, len, &len);
	if (!disp)
		return rtrn_dropped;

	if (len > 65535)
	{
		free(disp);			/* 64-bit extended length not implemented, same as receive */
		return rtrn_dropped;
	}

	header[0] = 0x81;		/* FIN=1, opcode=1 (text) */

	if (len <= 125)
	{
		header[1] = (unsigned char) len;	/* no mask bit: server frames are never masked */
		headerLen = 2;
	}
	else
	{
		header[1] = 126;
		header[2] = (unsigned char) (len >> 8);
		header[3] = (unsigned char) (len & 0xFF);
		headerLen = 4;
	}

	frame = malloc(headerLen + len);
	memcpy(frame, header, headerLen);
	memcpy(frame + headerLen, disp, len);

	WS_SendRaw(instance, "Send", frame, headerLen + len, connId);

	free(frame);
	free(disp);

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int WS_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	local->enabled = GetValueInt(data) ? 1 : 0;
	SetValueStr(GetPropNode(instance, "Enable"), local->enabled ? "1" : "0");

	return rtrn_handled;
}

/* no socket of its own, nothing to schedule - Activate just goes live */
int WS_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || local->active)
		return rtrn_dropped;

	local->active = 1;
	SetPropInt(instance, "State", Running);

	return rtrn_handled;
}

/* the settings panel: what WebSocket looks like, built once per instance */
static ControlSpec WebSocketControls[] = {
	{ "LED",    "State", 10, 10, 20, 20 },
	{ "Button", NULL,    10, 40, 60, 20 },
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance, port;
	InstanceData *local = malloc(sizeof(InstanceData));

	local->active = 0;
	local->enabled = 1;
	local->connHandshakes = NewNode(INTEGER);
	local->connRecvBufs = NewNode(INTEGER);

	instance = NewNode(INTEGER);
	SetName(instance, "WebSocket");
	SetPropInt(instance, "State", Starting);
	WatchableProp(instance, "State");
	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)WS_Activate);

	/* app-facing: same In/Out shape every other translator object uses */
	SetPropInt(instance, "Out", 0);
	SetPropInt(instance, "In", 0);
	port = GetPropNode(instance, "In");
	SetPropLong(port, "OnMsg", (long)WS_OnAppIn);

	/* TCP-facing */
	SetPropInt(instance, "Send", 0);
	SetPropInt(instance, "Wire", 0);
	port = GetPropNode(instance, "Wire");
	SetPropLong(port, "OnMsg", (long)WS_OnWire);

	/* enable port, the LED: 1 enables, 0 disables, any source can drive it */
	SetPropStr(instance, "Enable", "1");
	port = GetPropNode(instance, "Enable");
	SetPropLong(port, "OnMsg", (long)WS_OnEnable);

	InitPosition(instance);

	RegisterInstance(class, instance);

	BuildSettingsView(instance, WebSocketControls, sizeof(WebSocketControls) / sizeof(WebSocketControls[0]));

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

	if (local)
	{
		NodeObj entry;

		for (entry = GetNextProp(local->connRecvBufs); entry; entry = GetNextSibling(entry))
		{
			buff b = (buff) GetValueLong(entry);
			if (b)
				buffDestroy(b);
		}
		DelNode(local->connRecvBufs);

		DelNode(local->connHandshakes);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "WebSocket");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	PublishProp(ClassSelf, "Wire",   "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Send",   "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "In",     "in",   PROP_NULL, "");
	PublishProp(ClassSelf, "Out",    "out",  PROP_NULL, "");
	PublishProp(ClassSelf, "Enable", "in",   PROP_CHECKBOX, "1");
	PublishProp(ClassSelf, "State",  "data", PROP_LED, "1");

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

	SetName(temp, "WebSocket");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c700");
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
