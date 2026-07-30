# Stable Diffusion

Renders an image via a Stable Diffusion WebUI (AUTOMATIC1111 v1.6 / SD Next) server using its REST API. A **Prompt** and **Negative** prompt are sent directly to the `/sdapi/v1/txt2img` endpoint along with render parameters (Steps, Width, Height, CFG Scale). During generation, progress is polled in real time to display `% done`, and once finished, the `/file=` **Url** of the image is published.

## Flow
1. `POST /sdapi/v1/txt2img` with JSON options containing `prompt`, `negative_prompt`, `steps`, `width`, `height`, `cfg_scale`, and `save_images: true`.
2. Poll `GET /sdapi/v1/progress` every 500ms during rendering to update real-time progress (`% done`).
3. Once the render completes, build `http://<Server>:<Port>/file=<filepath>` and publish it in **Url**.

The widget never fetches raw image bytes into memory — **Url** provides the HTTP address ready for an image control or web browser to render directly from the server.

## Controls
The main panel holds everyday controls (Prompt, Negative, Generate, Progress, Status, Url, Enable); the **Settings** sub-view (accessible via its view icon) holds server and model configuration parameters set once — Server, Port, Steps, Width, Height, CfgScale, Timeout.

### Main View
- **Enable** — checked by default. Unchecked, Generate/In trigger nothing; unchecking mid-render cancels operation.
- **Prompt** — text prompt describing the desired image generation.
- **Negative** — unwanted elements/concepts to exclude from generation.
- **Generate** — triggers an image render using the current prompt and settings.
- **Progress** — displays real-time render percentage next to the generate button (`0% done` to `100% done`).
- **Status** — current state (`idle`, `connecting`, `generating... X% done`, `done (100%)`, or an error).
- **Url** — the resulting image HTTP URL (also the wire-out; a finished render publishes it).

### Settings View
- **Server** / **Port** — the WebUI server address and API port (default `127.0.0.1:7860`).
- **Steps** — number of sampling steps (default `20`).
- **Width** / **Height** — image dimensions in pixels (default `512x512`).
- **CfgScale** — Classifier Free Guidance scale intensity (default `7`).
- **Timeout** — seconds before giving up on a render task (default `36000`).

### Ports
- **In** — a wire input port. Receiving a string prompt here sets **Prompt** and immediately starts a render, allowing automated workflows (e.g. LLM/Ollama output nodes) to drive image generation.

## Notes
- Requires the Stable Diffusion WebUI server to be started with the `--api` flag enabled (e.g. `./webui.sh --api`).
- Plain HTTP only. **In** and **Generate** act as trigger points, allowing timers or scripts to automate generation.
- Real-time progress monitoring occurs via lightweight background requests to `/sdapi/v1/progress?skip_current_image=true` so main frame execution remains smooth and responsive.
