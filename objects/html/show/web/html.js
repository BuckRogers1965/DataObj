/* The HTML control's browser half. A SANDBOXED frame: display is display,
   never execution. The sandbox blocks all script (browser-enforced, not a
   sanitizer to maintain); allow-same-origin alone is safe without
   allow-scripts and is what lets the page (and the harness) read the
   rendering. */
const HTML_VIEW_BASE = '<style>body{margin:6px 10px;background:#1a1b20;' +
  'color:#cfd4dc;font:12px/1.5 system-ui,sans-serif}</style>';

register('HTML', {
  create(ctx) {
    const el = document.createElement('iframe');
    el.className = 'html-view';
    el.setAttribute('sandbox', 'allow-same-origin');
    Object.defineProperty(el, 'value', {
      get() { return this._v || ''; },
      set(v) { this._v = v; this.srcdoc = HTML_VIEW_BASE + (v || ''); },
    });
    if (ctx && ctx.defaultValue) el.value = ctx.defaultValue;
    return el;
  },
});
