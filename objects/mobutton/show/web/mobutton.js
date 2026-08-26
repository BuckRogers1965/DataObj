/* The MoButton's browser half: pointerdown presses, pointerup/leave
   releases, and BOTH are ordinary writes to its own Value. The engine owns
   what an edge MEANS (its Out, its auto-repeat); this only reports what the
   hand did. Released outside the button still counts as a release.

   The caption is the object's Label - engine state, watched like any
   property, never a string this file decides. */
register('MoButton', {
  create(ctx) {
    const btn = document.createElement('button');
    btn.className = 'mo-button';
    btn.textContent = 'Press';

    let held = false;
    btn.addEventListener('pointerdown', (ev) => {
      ev.stopPropagation();

      /* A HALF-TYPED EDIT IS STILL AN EDIT, AND IT HAPPENED FIRST.
         pointerdown beats the focused field's blur, so pressing a button
         while a box holds an uncommitted value sent the press first and
         let the edit land afterwards - against whatever the press had by
         then changed the box to stand for. Blur commits it (textbox.js
         writes on blur), so the two arrive in the order the hand did
         them. */
      if (document.activeElement && document.activeElement !== btn
          && typeof document.activeElement.blur === 'function')
        document.activeElement.blur();

      held = true;
      btn.classList.add('pressed');
      ctx.set('Value', '1');
    });
    const release = () => {
      if (!held) return;
      held = false;
      btn.classList.remove('pressed');
      ctx.set('Value', '0');
    };
    btn.addEventListener('pointerup', release);
    btn.addEventListener('pointerleave', release);

    ctx.watch('Label', (v) => { btn.textContent = v; });
    return btn;
  },
});
