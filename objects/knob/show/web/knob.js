/* The Knob's browser half. A number input answers .value already; it
   commits when the number is settled, not while it is being typed. */
register('Knob', {
  create(ctx) {
    const el = document.createElement('input');
    el.type = 'number';
    el.value = (ctx && ctx.defaultValue) || '0';
    if (ctx && ctx.commit) el.onchange = () => ctx.commit(el.value);
    return el;
  },
});
