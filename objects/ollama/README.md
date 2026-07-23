# Ollama

Sends a chat request to an OpenAI-format endpoint (Ollama's
`/v1/chat/completions`) and puts the reply text in **Output**. The request runs
on a non-blocking socket driven by a poll task, so a long generation never
freezes anything else.

## Controls
- **Server** / **Port** - where the server listens (default `127.0.0.1:11434`).
- **Model** - the model name to use. Injected into the payload's `"model"`
  field at send, overriding whatever the payload has.
- **Models** - a dropdown of the models the server reports (`GET /v1/models`).
  Picking one fills the **Model** box, which you can then edit. The list is
  fetched when the widget opens and whenever you press **Refresh**.
- **Refresh** - re-fetch the model list.
- **Path** - the request path (default `/v1/chat/completions`).
- **Payload** - the prompt text you are sending. The widget wraps it into the
  OpenAI chat request `{"model":...,"stream":false,"messages":[{"role":"user",
  "content":<prompt>}]}`, JSON-escaping the prompt for you.
- **Send** - fire the request.
- **Timeout** - seconds to wait before giving up (default `3600`, i.e. one hour).
- **Status** - `connecting` / `sending` / `waiting` / the HTTP status line, or an
  error (`timed out`, `connect refused`, ...).
- **Output** - the assistant's text (`choices[0].message.content`). If the reply
  has no `content`, the raw body is shown instead.
- **Enable** - unchecked, Send and Refresh do nothing; unchecking mid-request
  cancels it.

## Notes
Plain HTTP only (no TLS). Both **Send** and **Refresh** are ordinary ports, so a
Pulse or a script can drive them like the on-screen buttons. `Model`, `Payload`,
and the rest are ordinary properties - wire them from a flow to feed the request.
