/* The Dropdown's browser half: a native select whose options are the
   object's own Items and whose selection is its Value.

   Items and Value arrive in whatever order the engine sends them, so the
   wanted value is remembered and re-applied after a rebuild - a <select>
   silently discards a value with no matching option, which is how a
   dropdown came to display one language while the property said another. */
register('Dropdown', {
  create(ctx) {
    const sel = document.createElement('select');
    sel.className = 'widget-menu';

    function applyValue(v) {
      sel.wantedValue = v;
      sel.value = v === undefined || v === null ? '' : v;
    }

    ctx.watch('Items', (v) => {
      const keep = sel.wantedValue || sel.value;
      sel.textContent = '';
      for (const item of (v || '').split(',')) {
        if (!item) continue;
        const opt = document.createElement('option');
        opt.value = item;
        opt.textContent = item;
        sel.appendChild(opt);
      }
      applyValue(keep);
    });
    ctx.watch('Value', applyValue);

    sel.onchange = () => ctx.set('Value', sel.value);
    return sel;
  },
});
