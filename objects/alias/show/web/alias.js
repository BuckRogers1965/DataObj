/* THE ALIAS'S BROWSER HALF - a control that points somewhere else.

   An Alias is a Control (alias.c is Parent = Control), so it renders as an
   atom like any other; what makes it an alias is only WHERE its value
   lives. It reads its own Target/TargetProp/Widget as they arrive and puts
   up the control that Widget names, bound to the target's property.

   Moved from the host unchanged. The next step is smaller than this file
   looks: a control pointed elsewhere is exactly what controlContext's
   `points` argument already expresses, so most of what is here collapses
   into "create the class my Widget names, with my context pointed at my
   target" - which is why it had to leave the host before it could shrink. */

/* (re)build the alias atom's control once Target/TargetProp are known (or  */
/* after its Widget/Label presentation properties change). Reads subscribe  */
/* to the TARGET - events speak the original's name, one tap serves every    */
/* alias of it - while edits write through the ALIAS, exercising the link.   */
/*                                                                            */
/* The Widget property is stamped by the ENGINE at the alias's birth          */
/* (create-alias / the internals builder, bridge.c) from what the target's    */
/* class published - this function renders it and deduces nothing.             */
function renderAliasControl(alias) {
  const rec = aliasAtoms[alias];
  if (!rec || !rec.target || !rec.targetProp) return;

  /* PROP_ICON (what Open publishes): another icon for the same thing -    */
  /* clicking it opens the ONE panel, whether the target is a view or a    */
  /* card; twelve icons anywhere are twelve doorways to one window         */
  if (Number(rec.widget) === PROP_ICON) {
    rec.slot.textContent = '';
    const ic = document.createElement('div');
    ic.className = 'instance-icon';
    const lb = document.createElement('span');
    lb.className = 'instance-icon-label';
    lb.textContent = rec.label || baseName(rec.target);
    lb.title = rec.target;
    ic.appendChild(lb);
    ic.addEventListener('click', () => {
      const p = panels[rec.target];
      if (effectiveMode(ic) === 'Operate' && p) p.setOpen(true);
    });
    rec.slot.appendChild(ic);
    rec.control = ic;
    rec.labelEl.textContent = '';
    /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
    instances[alias] = instances[alias] || { className: 'Alias', el: rec.el, ports: {} };
    instances[alias].ports['Value'] = rec.el;
    return;
  }

  /* The row is labelled with the PROPERTY name alone. The object it belongs
     to is already the title of the panel the row is sitting in, so prefixing
     every row with it just repeats a long generated name twenty times. */

  /* PROP_MENU: a dropdown. Its selectable options come from a companion   */
  /* property on the target named "<prop>List" (e.g. Language -> the        */
  /* discovered LanguageList) - the same one-property-carries-the-options   */
  /* convention a MenuButton uses with its Items, applied to any menu prop. */
  if (Number(rec.widget) === PROP_MENU) {
    rec.slot.textContent = '';
    /* the Dropdown control, pointed at someone else's property - the same
       class the palette holds, told where its Value and Items live. Nothing
       here builds a select or knows what one looks like. */
    const menu = widgetModule('Dropdown');
    const sel = menu.create(controlContext(alias, null, null, {
      Value: { instance: rec.target, prop: rec.targetProp },
      Items: { instance: rec.target, prop: rec.targetProp + 'List' },
    }));
    sel.classList.add('widget-atom-control');
    rec.slot.appendChild(sel);
    rec.control = sel;
    rec.labelEl.textContent = rec.label || rec.targetProp;
    /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
    instances[alias] = instances[alias] || { className: 'Alias', el: rec.el, ports: {} };
    instances[alias].ports['Value'] = rec.el;
    return;
  }

  /* presentation is the alias's own Widget property: the stamped numeric  */
  /* widget type maps to a control class; a widget CLASS NAME typed in by   */
  /* hand on the dissection table is honored as-is; anything else (an       */
  /* unpublished property, PROP_NULL plumbing) is a plain textbox            */
  const widgetClass = widgetClassForType(rec.widget) ||
                      (widgetModule(rec.widget) ? rec.widget : 'Textbox');

  rec.slot.textContent = '';
  /* reads subscribe to the target (events speak the original's name);    */
  /* writes go through the alias's own "Value" slot - the doorway - so     */
  /* the alias's own Name/Container/X/Y are never touched                  */
  const el = bindLiveControl(rec.target, rec.targetProp, widgetClass, propertyValues[rec.target + '.' + rec.targetProp],
    (v) => send({ cmd: 'set-property', instance: cur(alias), prop: 'Value', value: v }), alias);
  el.classList && el.classList.add('widget-atom-control');
  rec.slot.appendChild(el);
  rec.control = el;

  rec.labelEl.textContent = rec.label || rec.targetProp;

  /* wiring through the alias is wiring to the original (ResolvePort,       */
  /* object.c) - the atom arms a wire on its doorway slot                    */
  /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
  instances[alias] = instances[alias] || { className: 'Alias', el: rec.el, ports: {} };
  instances[alias].ports['Value'] = rec.el;
}

function registerAliasAtom(alias, pos, container) {
  const el = document.createElement('div');
  el.className = 'widget-atom alias-atom';
  el.style.left = pos.x + 'px';
  el.style.top = pos.y + 'px';

  const slot = document.createElement('span');
  slot.className = 'alias-slot';
  slot.textContent = '…';
  el.appendChild(slot);

  const labelEl = document.createElement('span');
  labelEl.className = 'widget-atom-label';
  labelEl.textContent = baseName(alias);
  labelEl.title = alias;
  el.appendChild(labelEl);

  aliasAtoms[alias] = { el, slot, labelEl, target: '', targetProp: '', widget: '', direction: '', label: '', control: null, container: container || '' };

  el.addEventListener('click', () => {
    const rec = aliasAtoms[alias];
    if (rec && rec.targetProp) onPortClick(alias, 'Value', el);
  });

  /* an alias is as alias-able and clone-able as anything else. Alias      */
  /* makes another alias of the same target (chains collapse to the        */
  /* original); Clone goes through the alias to the THING and snapshots    */
  /* it - a new independent instance of the target's class with a copy of  */
  /* its current data, exactly what cloning the thing itself gives you.    */
  el.onpointerdown = (ev) => {
    const cur = aliasOfEl(el, alias);   /* survives renames/moves */
    const rec = aliasAtoms[cur];
    if (!rec || ev.target === rec.control) return;
    const mode = effectiveMode(el);
    if (mode === 'Alias' && rec.target && rec.targetProp) {
      /* through the doorway slot - the server resolves and records the  */
      /* real target and property name                                    */
      startGestureDrag(ev, 'alias', { of: cur, prop: 'Value' },
        'alias: ' + baseName(rec.target) + '.' + rec.targetProp);
      return;
    }
    if (mode === 'Clone' && rec.target) {
      /* the server resolves through the alias and snapshots the thing */
      startGestureDrag(ev, 'clone', { sourceAlias: cur, className: 'Alias' },
        'clone: ' + baseName(rec.target));
      return;
    }
    startDrag(ev, el, cur, 'Alias');
  };
  attachDeleteGesture(el, alias);
  attachOptionsGesture(el, alias);

  placeInContainer(el, container || '');
  /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
  instances[alias] = { className: 'Alias', el, ports: {} };
  livePositions[alias] = { el };
  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });

  /* what it stands for + its own presentation - the atom assembles itself  */
  /* as these arrive (onPropertyChanged's aliasAtoms branch). Widget and     */
  /* Widget is the engine's stamp (create-alias/internals, bridge.c).        */
  send({ cmd: 'subscribe', instance: alias, port: 'Target' });
  send({ cmd: 'subscribe', instance: alias, port: 'TargetProp' });
  send({ cmd: 'subscribe', instance: alias, port: 'Widget' });
  send({ cmd: 'subscribe', instance: alias, port: 'Label' });

  log('created ' + alias + ' (Alias)', 'event');
}

/* the class's entry point: render one instance of me */
register('Alias', {
  renderInstance: (s) => registerAliasAtom(s.alias, s.pos, s.container),
});
