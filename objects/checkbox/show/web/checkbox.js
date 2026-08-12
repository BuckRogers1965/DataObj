/* The Checkbox's browser half. .value is defined over .checked so the one
   uniform accessor works here too: '1' is ticked, anything else is not. */
register('Checkbox', {
  create(ctx) {
    const el = document.createElement('input');
    el.type = 'checkbox';
    Object.defineProperty(el, 'value', {
      get() { return this.checked ? '1' : '0'; },
      set(v) { this.checked = v === '1'; },
    });
    el.value = (ctx && ctx.defaultValue) || '0';
    if (ctx && ctx.commit) el.onchange = () => ctx.commit(el.value);
    return el;
  },
});
