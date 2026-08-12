/* The Image control's browser half: a plain <img>, its value loaded
   straight from wherever it points (a ComfyUI /view, say). No script, so
   no sandbox needed. */
register('Image', {
  create(ctx) {
    const el = document.createElement('img');
    el.className = 'image-view';
    Object.defineProperty(el, 'value', {
      get() { return this.getAttribute('src') || ''; },
      set(v) { this.src = v || ''; },
    });
    if (ctx && ctx.defaultValue) el.value = ctx.defaultValue;
    return el;
  },
});
