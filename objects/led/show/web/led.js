/* The LED's browser half, owned by the LED. The host knows nothing about
   this class - not its name, not what it looks like, not how it updates.

   The element answers .value, so nothing outside needs a branch to update
   one: whatever arrives is assigned and the control decides what that
   means. Here it means which state class it wears. */
register('LED', {
  create(ctx) {
    const el = document.createElement('span');
    Object.defineProperty(el, 'value', {
      get() { return this._v === undefined ? '0' : this._v; },
      set(v) {
        this._v = v;
        this.className = 'node-led state-' + (v === undefined || v === null || v === '' ? '0' : v);
      },
    });
    el.value = (ctx && ctx.defaultValue) || '0';
    return el;
  },
});
