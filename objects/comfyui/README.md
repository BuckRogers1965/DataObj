# ComfyUI

Renders an image with a ComfyUI server. A **Prompt** is spliced into a
**Workflow** (a ComfyUI API-format graph) wherever the tag `%%prompt%%` appears,
the workflow is queued, and when the render finishes the `/view` **Url** of the
image is published.

## Flow
1. `POST /prompt` with `{"prompt": <workflow>}` - `%%prompt%%` and
   `%%negative%%` are spliced in first, and seeds are randomized if asked.
2. Poll `GET /history/<id>` once a second until the image appears.
3. Build `http://<Server>:<Port>/view?filename=...` (plus `&subfolder=` only
   when the image is in one) and put it in **Url**.

The widget never fetches the image bytes - **Url** is the address, ready for an
image control (or a browser) to load straight from ComfyUI.

## Controls
The main panel holds the everyday controls (Prompt, Negative, Generate, Status,
Url, Enable); the **Settings** sub-view (open its icon) holds the wiring you set
once - Server, Port, Timeout, Randomize, Workflow.

- **Server** / **Port** - the ComfyUI address (default `192.168.4.253:8188`).
- **Prompt** - the text spliced in for `%%prompt%%`.
- **Negative** - the text spliced in for `%%negative%%`. Empty means an empty
  negative (no effect).
- **Randomize** - checked, every `seed` / `noise_seed` in the workflow is set
  to a fresh random number on each render; unchecked, the workflow's seeds are
  used as written.
- **In** - a prompt arriving on this port sets Prompt and starts a render, so a
  flow (e.g. an Ollama Output) can drive image generation.
- **Generate** - render from the current Prompt.
- **Workflow** - the ComfyUI **API-format** JSON (Save > API Format in ComfyUI).
  Put `%%prompt%%` / `%%negative%%` in your CLIP text nodes. The default is the
  standard SD1.5 text-to-image graph; set `ckpt_name` to a checkpoint you have.
- **Url** - the resulting image URL (also the wire-out; a render publishes it).
  Only ever an image URL, never anything else.
- **Status** - `submitting` / `rendering...` / `done`, or an error.
- **Timeout** - seconds before giving up (default `600`).
- **Enable** - unchecked, Generate/In do nothing; unchecking mid-render cancels.

## Notes
Plain HTTP only. **In** and **Generate** are ordinary ports, so a Pulse or
script can trigger a render like the button. Every `%%prompt%%` / `%%negative%%`
occurrence is replaced. The image URL omits `&type=` (ComfyUI defaults to
`output`, which is what a browser can fetch - a workflow's own type such as
`flux2` would 404).
