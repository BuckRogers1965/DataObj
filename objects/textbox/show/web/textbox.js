/* The Textbox's browser half - the ONE text control, whatever size its
   text is. NOT a textarea (there is no textarea in this system): an
   editable div that answers .value like every other control, so every
   read/write path works on it unchanged.

   The default box is for a property that declares no size (an options or
   dissection row); a placed control overrides it with its own W/H. Boxes
   never resize by content. */
register('Textbox', {
  create(ctx) {
    const el = document.createElement('div');
    el.className = 'textbox';
    el.contentEditable = 'plaintext-only';
    Object.defineProperty(el, 'value', {
      get() { return this.innerText.replace(/\n$/, ''); },
      set(v) { this.innerText = v || ''; },
    });
    el.value = (ctx && ctx.defaultValue) || '';
    if (ctx && ctx.commit) {
      el.onchange = () => ctx.commit(el.value);
      el.addEventListener('blur', () => ctx.commit(el.value));
    }
    el.style.height = '2lh';
    el.style.width = '20ch';
    return el;
  },
});
