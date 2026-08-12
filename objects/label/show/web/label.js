/* The label's browser half, owned by the label. It shows text: whatever
   arrives is what it reads. The shared .widget-readout look stays in the
   host's stylesheet because it is shared - a rule belongs to a control
   only when that control is the only one wearing it. */
register('Label', {
  create(ctx) {
    const el = document.createElement('span');
    el.className = 'widget-readout';
    Object.defineProperty(el, 'value', {
      get() { return this.textContent; },
      set(v) { this.textContent = v === undefined || v === null ? '' : v; },
    });
    if (ctx && ctx.defaultValue) el.value = ctx.defaultValue;
    return el;
  },
});
