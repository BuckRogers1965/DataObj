/* THE CONTROL CLASS'S BROWSER HALF - what a control IS on a screen.

   Every class whose Parent is Control renders through this unless it says
   otherwise: an element the class itself builds, a caption, and where the
   caption sits. The host does not know any of it - it asks the class chain
   to render an instance and this answers for the whole Control branch.

   What is here is the FURNITURE and the BINDING, not any one control's
   look: the widget-atom wrapper, the label and its position, the size the
   instance declares, the click that arms a wire, and the context a control
   is handed (its value, how to commit, how to write and watch its own
   properties). The look of a Checkbox is in the Checkbox.

   controlContext is here rather than in the host because the context IS
   this class's offer to its subclasses - what a Control can do. A base
   class that renders differently offers a different one.

   Moved unchanged from app.js: a relocation, proven by the palette shape
   test rather than by reading. */

/* WHAT A CONTROL IS HANDED. Four things: the value it starts with, how to
   commit an edit, how to write one of its own properties, and how to watch
   one. A control can therefore TRANSLATE a gesture into a property write
   and reflect a property into a look - and nothing else. It cannot decide
   anything about the session, which is the line that keeps a surface a
   surface. */
function controlContext(alias, defaultValue, commit, points) {
  /* A control names its own properties ("Value", "Items"). Where those
     actually live is the context's business, which is what lets the SAME
     control serve an alias: the control is unchanged, it is just pointed
     somewhere else. Identity when nothing is pointed. */
  const at = (prop) => (points && points[prop]) || { instance: alias, prop: prop };
  return {
    defaultValue: defaultValue,
    commit: commit,
    alias: alias,
    set(prop, value) {
      const t = at(prop);
      send({ cmd: 'set-property', instance: cur(t.instance), prop: t.prop, value: value });
    },
    watch(prop, apply) {
      const t = at(prop);
      const key = t.instance + '.' + t.prop;
      (liveControls[key] = liveControls[key] || []).push({ apply: apply, widgetClass: 'own' });
      send({ cmd: 'subscribe', instance: t.instance, port: t.prop });
    },
  };
}

/* one shared builder for the raw input element a Value control renders  */
/* as - used both for a widget instance's own Value (base case) and for  */
/* the sub-widget instances wrapping some other object's property        */
function buildValueControl(widgetClass, defaultValue, onCommit, alias) {
  /* Every control builds itself. The host does not know what any of them
     are, does not keep a list, and has no fallback that quietly renders the
     wrong thing - a class with no browser half is a bug to see, not a
     textbox to squint at. */
  const own = widgetModule(widgetClass);
  if (own && own.create) return own.create(controlContext(alias, defaultValue, onCommit));
  return missingControlEl(widgetClass);
}

/* the loud failure. A missing presentation used to be silence - the lookup
   missed, the switch fell through, and it rendered as a textbox with no
   error anywhere. That silence is the thing this whole change exists to
   end, so it is visible on the page AND in the console. */
function missingControlEl(widgetClass) {
  console.error('no browser half registered for control class:', widgetClass);
  const el = document.createElement('span');
  el.className = 'widget-readout control-missing';
  el.textContent = '?' + (widgetClass || '');
  return el;
}

function makeReadoutEl(widgetClass) {
  const own = widgetModule(widgetClass);
  if (own && own.create) return own.create({});
  return missingControlEl(widgetClass);
}

/* a control that subscribes to its own backing property so it reflects  */
/* the authoritative server-side value, not just its own optimistic      */
/* write - anything else driving this same property (another client, a  */
/* Connect()ed source) shows up here too, the same way selfDisplays does */
function bindLiveControl(subscribeAlias, subscribeProp, widgetClass, defaultValue, onCommit, sizeAlias) {
  /* a Textbox edit is gated by this alias's GUI_Pattern before it leaves the
     browser - see guiValidate. Every other class commits unchanged. */
  const commit = (widgetClass !== 'Textbox' || subscribeProp !== 'Value') ? onCommit : (v) => {
    const raw = guiMaskStrip(guiAnnotation(subscribeAlias, 'Format'), v);
    if (!guiValidate(subscribeAlias, el, raw)) return;
    onCommit(raw);
  };
  const el = buildValueControl(widgetClass, defaultValue, commit, subscribeAlias);
  /* a Textbox is a pixel box: it takes the W/H of the instance it IS - its   */
  /* own when it renders its own Value, the Alias member's when it stands in  */
  /* for another object's property. Never the TARGET's: that is the whole     */
  /* widget's size, not this box's.                                            */
  if (widgetClass === 'Textbox') {
    /* the annotation belongs to the DATA the control carries, not to every
       box that happens to be bound to this instance. In an options panel
       every row binds to the same instance - masking on the instance alone
       put the phone format on X, Y, W, H, Name and on the GUI_Format row
       itself. An instance-level GUI_Format describes its Value; a row for
       any other property is not that value and is left alone. */
    if (subscribeProp === 'Value') {
      el.guiAlias = subscribeAlias;
      el.addEventListener('input', () => {
        guiReformat(el);
        /* live, both ways, once the box has been good once - the outline
           goes the instant the last digit lands and comes back the instant
           one is deleted. An untouched box that has never been complete is
           only half-typed, not wrong, so it stays plain until it commits. */
        const raw = guiMaskStrip(guiAnnotation(subscribeAlias, 'Format'), el.value);
        if (guiOk(subscribeAlias, raw)) {
          el.guiArmed = true;
          el.classList.remove('gui-invalid');
        } else if (el.guiArmed) {
          el.classList.add('gui-invalid');
        }
      });
    }
    const owner = sizeAlias || subscribeAlias;
    (liveControls[owner + '.W'] = liveControls[owner + '.W'] || []).push({ el, widgetClass: 'AtomW' });
    (liveControls[owner + '.H'] = liveControls[owner + '.H'] || []).push({ el, widgetClass: 'AtomH' });
    send({ cmd: 'subscribe', instance: owner, port: 'W' });
    send({ cmd: 'subscribe', instance: owner, port: 'H' });
  }
  send({ cmd: 'subscribe', instance: subscribeAlias, port: subscribeProp });
  /* more than one rendering can subscribe to the same alias.prop (an       */
  /* Alias atom binds to its target's prop alongside the target's own       */
  /* rendering) - every one has to keep getting updated, not just            */
  /* whichever registered last, so this is a list, not a single slot        */
  const key = subscribeAlias + '.' + subscribeProp;
  (liveControls[key] = liveControls[key] || []).push({ el, widgetClass });
  return el;
}

function makeSelfControl(alias, propName, widget, defaultValue) {
  const widgetClass = widgetClassForType(widget) || 'Textbox';
  return bindLiveControl(alias, propName, widgetClass, defaultValue,
    (v) => send({ cmd: 'set-property', instance: cur(alias), prop: propName, value: v }));
}

function makeSelfDisplay(alias, propName, widget) {
  const widgetClass = widgetClassForType(widget) || 'TextOut';
  const el = makeReadoutEl(widgetClass);
  send({ cmd: 'subscribe', instance: alias, port: propName });
  /* same list-not-single-slot reasoning as bindLiveControl above - Copy    */
  /* means more than one of these can exist for the same alias.prop        */
  const key = alias + '.' + propName;
  (selfDisplays[key] = selfDisplays[key] || []).push({ el, widgetClass });
  return el;
}

function updateLiveControl(entry, value) {
  /* a control watching one of its own properties - it said what to do */
  if (entry.apply) { entry.apply(value); return; }

  if (entry.widgetClass === 'AtomLabelPos') {
    const pos = ['left', 'right', 'top', 'bottom', 'none'].indexOf(value) >= 0 ? value : 'bottom';
    entry.el.classList.remove('atom-label-left', 'atom-label-right', 'atom-label-top', 'atom-label-bottom', 'atom-label-none');
    entry.el.classList.add('atom-label-' + pos);
    return;
  }
  if (entry.widgetClass === 'MoLabel') { entry.el.textContent = value; return; }
  if (entry.widgetClass === 'AtomW') { if (parseInt(value, 10)) entry.el.style.width = parseInt(value, 10) + 'px'; return; }
  if (entry.widgetClass === 'AtomH') { if (parseInt(value, 10)) entry.el.style.height = parseInt(value, 10) + 'px'; return; }
  /* the GUI_Format mask is the HOST's, not the control's: it is an
     annotation on the DATA (see guiValidate), so it is applied before the
     control is handed the value. This must stay ahead of the generic
     assignment below, or a masked box would be written raw. */
  if (entry.widgetClass === 'Textbox' && entry.el.guiAlias) {
    const mask = guiAnnotation(entry.el.guiAlias, 'Format');
    entry.el.value = mask ? guiMaskApply(mask, value) : value;
    guiValidate(entry.el.guiAlias, entry.el, value);
    return;
  }
  /* a control that brought its own presentation was handed a .value that
     knows what to do with what arrives - there is nothing to decide here */
  if (widgetModule(entry.widgetClass)) { entry.el.value = value; return; }
  entry.el.value = value;
}

/* a standalone widget instance: its own natural control (or dot) plus a  */
/* label, positioned and draggable exactly like any other card - no        */
/* header, no property rows, no footer, and its wiring dots (below) only    */
/* ever show up in Connect mode, same as everything else's. (The old Copy   */
/* mode's client-local second renderings are gone - a second doorway to     */
/* the same object is now a real Alias instance, shared and savable; see    */
/* registerAliasAtom.)                                                      */
function registerWidgetAtom(alias, className, props, pos, isCopy, container, reservedIn, reservedOut) {
  const el = document.createElement('div');
  el.className = 'widget-atom';
  toTop(el);
  el.style.left = pos.x + 'px';
  el.style.top = pos.y + 'px';

  let control, primaryProp;
  /* A control that brought its own presentation builds itself, gestures and
     all - it is handed its context and nothing here knows what class it is.
     MenuButton is the one exception and it is not about rendering: it makes
     SESSION decisions (the file dialog, arming export), which is the host's
     to own. See makeMenuButtonEl. */
  if (widgetModule(className)) {
    /* Bound to its OWN Value like any rendering of a property: the control
       is created through the same path a panel row uses, so it subscribes,
       it is updated, and it commits. A control that only displays simply
       never calls the commit it was handed - which is the whole of the old
       input/display split, and why it needed no replacement. */
    const valueProp = props.find((p) => p.Name === 'Value');
    control = bindLiveControl(alias, 'Value', className, valueProp && valueProp.Default,
      (v) => send({ cmd: 'set-property', instance: cur(alias), prop: 'Value', value: v }));
  } else if (className === 'MenuButton') {
    control = makeMenuButtonEl(alias);
  } else {
    const valueProp = props.find((p) => p.Name === 'Value');
    const widget = valueProp ? valueProp.Widget : PROP_TEXTOUT;
    control = widgetClassForType(widget)
      ? makeSelfControl(alias, 'Value', widget, valueProp && valueProp.Default)
      : makeSelfDisplay(alias, 'Value', widget);
  }
  /* what this control speaks for is the instance's own answer - the same
     ReservedIn/ReservedOut a shut view answers with, never a class name */
  primaryProp = reservedOut || reservedIn;
  control.classList.add('widget-atom-control');
  el.appendChild(control);

  /* a Markdown/HTML box takes the size its OBJECT declares (W/H) - the     */
  /* panel that placed it sets its size,                                     */
  /* so it fits whatever container it was built into. Subscribed like any    */
  /* value and pushed on subscribe.                                          */
  if (className === 'Markdown' || className === 'HTML' || className === 'Image') {
    (liveControls[alias + '.W'] = liveControls[alias + '.W'] || []).push({ el: control, widgetClass: 'AtomW' });
    (liveControls[alias + '.H'] = liveControls[alias + '.H'] || []).push({ el: control, widgetClass: 'AtomH' });
    send({ cmd: 'subscribe', instance: alias, port: 'W' });
    send({ cmd: 'subscribe', instance: alias, port: 'H' });
  }

  /* a MenuButton's own button text already says what it is (Label,        */
  /* plus Selected once something's been picked) - a second "alias" label  */
  /* under it would just be noise                                          */
  if (className !== 'MenuButton') {
    const label = document.createElement('span');
    label.className = 'widget-atom-label';
    label.textContent = baseName(alias);
    label.title = alias;
    el.appendChild(label);

    /* the layout table can place (or hide) the caption via a LabelPos
       property - left/right/top/bottom/none; default is bottom */
    (liveControls[alias + '.LabelPos'] = liveControls[alias + '.LabelPos'] || []).push({ el, widgetClass: 'AtomLabelPos' });
    send({ cmd: 'subscribe', instance: alias, port: 'LabelPos' });
  }

  /* the whole atom IS the property, the same way clicking a card's row is  */
  /* that row's property - no separate dot, no in/out distinction, click    */
  /* it in Connect mode and it arms/completes a wire on primaryProp         */
  el.addEventListener('click', () => onPortClick(alias, primaryProp, el));

  /* dragging the atom: Move moves it, Clone ghosts a new independent      */
  /* instance, Alias ghosts an Alias of its primary control - one           */
  /* mousedown, mode decides (startDrag). The control itself keeps its own  */
  /* gesture (a click, a slider drag) - only the chrome around it drags.    */
  el.onpointerdown = (ev) => {
    /* HARD RULE: touching the MODE MENU in any mode other than Operate
       puts the session back in Operate, and that touch does nothing else
       - no drag, no clone, no delete. It is the control you steer the GUI
       with, so it is never itself the target of a mode. */
    if (baseName(alias) === 'ModeMenu' && currentMode !== 'Operate') {
      applyMode('Operate');
      send({ cmd: 'set-property', instance: alias, prop: 'Selected', value: 'Operate' });
      /* and open it, here, rather than hoping the click that follows does
         it - that click also TOGGLES, so whichever way the ordering fell
         the menu ended up shut. Opened explicitly, flagged so the click
         behind it leaves it alone. */
      const mm = (menuButtons[alias] || [])[0];
      if (mm && mm.dropdown) {
        mm.dropdown.style.display = 'block';
        mm.justOpened = true;
      }
      ev.stopPropagation();
      return;
    }

    /* Operate: the control keeps its own gesture (click a menu, drag a
       slider). Any other mode: the whole atom is what you grabbed, even
       if the control fills it. */
    if (currentMode === 'Operate' && ev.target === control) return;
    startDrag(ev, el, alias, className, primaryProp);
  };

  attachDeleteGesture(el, alias);
  attachOptionsGesture(el, alias);

  /* instance-created already carries Container inline (Bridge_InstanceEvent, */
  /* bridge.c) so this places straight into the real parent on first paint -  */
  /* no default-to-canvas-then-correct, which is what let a dropped/delayed   */
  /* Container reply strand an element in the root looking like it belonged   */
  /* there. The Container subscribe below still exists for any later move.   */
  placeInContainer(el, container || '');
  /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
  instances[alias] = { className, el, ports: { [primaryProp]: el } };

  livePositions[alias] = { el };
  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });

  log('created ' + alias + ' (' + className + ')', 'event');
}

/* the class's entry point: render one instance of me */
register('Control', {
  renderInstance: (s) => registerWidgetAtom(s.alias, s.className, s.props, s.pos,
                                            false, s.container, s.reservedIn, s.reservedOut),
});
