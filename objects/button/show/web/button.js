/* The Button's browser half. A Button is a MoButton that only sends the 1 -
   it is not a different kind of thing, and "Activate" is a property name a
   panel may happen to use, never a caption. That one difference is the
   whole of this file: the release clears the pressed look and reports
   nothing. */
register('Button', {
  create(ctx) {
    const btn = document.createElement('button');
    btn.className = 'mo-button';
    btn.textContent = 'Press';

    let held = false;
    btn.addEventListener('pointerdown', (ev) => {
      ev.stopPropagation();
      held = true;
      btn.classList.add('pressed');
      ctx.set('Value', '1');
    });
    const done = () => { held = false; btn.classList.remove('pressed'); };
    btn.addEventListener('pointerup', done);
    btn.addEventListener('pointerleave', done);

    ctx.watch('Label', (v) => { btn.textContent = v; });
    return btn;
  },
});
