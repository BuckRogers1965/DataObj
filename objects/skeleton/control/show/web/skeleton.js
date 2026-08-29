/* The Skeleton's browser half - the control's own presentation, shipped
   inside its .object and registered by class name. The client asks the
   engine what arrived and calls this; there is no list of control names
   anywhere in the browser to add yourself to.

   Two things every control's create() must do:
     - define `.value`, the ONE accessor the host reads and writes. Whatever
       your element's natural property is (textContent, .checked, a knob's
       angle), .value is what hides it.
     - call ctx.commit(v) if a person can CHANGE this control. That is the
       gesture going back to the engine; a read-only control just omits it.

   ctx.defaultValue is what the class published as the starting value. */
register('Skeleton', {
  create(ctx) {
    const el = document.createElement('input');
    el.type = 'text';
    el.className = 'widget-input';

    /* .value is already an input's own property - a control whose element
       has no natural .value defines one with Object.defineProperty, the way
       the Checkbox does over .checked and the TextOut does over textContent */
    el.value = (ctx && ctx.defaultValue) || '';

    /* the person changed it: tell the engine, do not just keep it here */
    if (ctx && ctx.commit) el.onchange = () => ctx.commit(el.value);

    return el;
  },
});
