/* The Slider's browser half. A range input already answers .value, so
   there is nothing to shim - it commits while it is dragged. */
register('Slider', {
  create(ctx) {
    const el = document.createElement('input');
    el.type = 'range';
    el.value = (ctx && ctx.defaultValue) || '0';
    if (ctx && ctx.commit) el.oninput = () => ctx.commit(el.value);
    return el;
  },
});
