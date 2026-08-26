/* THE VIEW'S BROWSER HALF - what containment looks like.

   A View is the class that contains other instances, so everything about
   holding things is here and nothing about it is in the host: the icon that
   is a view's permanent presence, the panel it opens, its header and
   collapse, PanelX/PanelY, the resize handle, the stand-in dots it offers
   on each side, and where its members are placed.

   That last one is the point. "Members lay out by their own X/Y" stops
   being something the framework imposes and becomes THIS class's opinion,
   which a Row, a Grid or a table-of-records can subclass and replace
   without the host knowing anything happened.

   A Widget is a View (widget.c is Parent = Control, same as this), so a
   widget's panel is this rendering, inherited rather than reimplemented.

   These functions are the ones that were in app.js, moved unchanged: this
   is a relocation, and the palette shape test proves it byte for byte. */

function registerPanel(alias, panelEl, display, onToggle) {
  panelEl.style.position = 'absolute';
  panelEl.style.left = '240px';
  panelEl.style.top = '60px';
  panelEl.style.display = 'none';
  $('canvas').appendChild(panelEl);
  toTop(panelEl);


  const rec = {
    el: panelEl,
    openApplied: false,
    setOpen(open) {
      /* LOCKED OPEN: a panel that is drawn in place has no icon to bring
         it back, so closing it would just vanish it. Every caller comes
         through here - the header button, the host applying
         ReservedViewOpen, an alias opening it - so the guard belongs here
         and nowhere else. */
      if (rec.lockedOpen && !open) return;

      /* an explicit open/close IS a presentation decision - a late-      */
      /* arriving initial Open value must never override it               */
      rec.openApplied = true;
      panelEl.style.display = open ? display : 'none';
      /* opening puts it on top - always. The pointerdown handler raises
         whatever you click, so the panel you opened this one FROM is
         already above it, and a sub-view would come up behind its own
         parent looking like it never opened. */
      if (open) toTop(panelEl);
      /* tell the engine the panel opened/closed - an object may react to  */
      /* its own panel opening (e.g. load help on open). The engine        */
      /* DELIVERS this to any Open handler; it does not persist the state. */
      send({ cmd: 'set-property', instance: cur(alias), prop: 'ReservedViewOpen', value: open ? '1' : '0' });
      if (onToggle) onToggle(open);
      updateWiresFor(alias);
      /* opening reveals controls that were not drawable a moment ago, so the
         wires that could only be drawn against a dot can now also be drawn
         against the control itself. Redrawn from what the engine has already
         reported - asking it to re-list would re-walk every connection in the
         session on every single panel open. */
      if (open && currentMode === 'Connect') redrawKnownWires();
    },
  };
  panels[alias] = rec;

  /* the engine records a container's newest member here, so anything that
     appears inside this view - an import, a clone, an object building its
     own children - announces itself without the creator having to know
     who is watching. */
  send({ cmd: 'subscribe', instance: alias, port: 'LastMember' });
  send({ cmd: 'subscribe', instance: alias, port: 'ReservedViewOpen' });
  send({ cmd: 'subscribe', instance: alias, port: 'ReservedViewPanelX' });
  send({ cmd: 'subscribe', instance: alias, port: 'ReservedViewPanelY' });
  return rec;
}

/* dragging any panel by its titlebar moves the PANEL (PanelX/PanelY,     */
/* shared) - never the icon; two things, two independent lives            */
function startPanelDrag(ev, alias, panelEl) {
  const rect = panelEl.getBoundingClientRect();
  panelDrag = { alias, el: panelEl, offsetX: ev.clientX - rect.left, offsetY: ev.clientY - rect.top };
  ev.preventDefault();
}

function addStandInMark(icon, side, alias, spec) {
  const dot = document.createElement('div');
  dot.className = 'view-dot ' + side;
  dot.title = (side === 'in' ? 'wire in -> ' : 'wire out -> ') + spec;
  dot.addEventListener('click', (ev) => {
    ev.stopPropagation();
    onStandInClick(alias, side, spec, dot);
  });
  icon.appendChild(dot);
  standInDots.push({ el: dot, viewAlias: alias, spec });
}

function registerView(alias, props, pos, hidden, container, reservedIn, reservedOut) {
  /* the icon IS the view's permanent presence - it never goes away.        */
  /* Opening shows the panel as a separate element with its own position    */
  /* (PanelX/PanelY, real shared properties independent of the icon's        */
  /* X/Y) - two placements of one thing, not two things. A HIDDEN view is    */
  /* a panel with no icon at all: an object's internals view, whose          */
  /* presence on the canvas is the object's own icon.                        */
  const wrap = document.createElement('div');
  wrap.className = 'instance-wrap';
  toTop(wrap);
  wrap.style.left = pos.x + 'px';
  wrap.style.top = pos.y + 'px';
  if (hidden) wrap.style.display = 'none';

  const icon = document.createElement('div');
  icon.className = 'instance-icon';
  const iconLabel = document.createElement('span');
  iconLabel.className = 'instance-icon-label';
  iconLabel.textContent = baseName(alias);
  /* A ONE-CHARACTER NAME IS A SYMBOL, not a word: draw it as a small glyph
     button rather than a labelled box. Purely how it is drawn - the name is
     still the name, and the engine is not consulted about pixels. */
  if ([...baseName(alias)].length === 1) icon.classList.add('instance-icon-glyph');
  iconLabel.title = alias;
  icon.appendChild(iconLabel);
  wrap.appendChild(icon);

  /* the panel floats at the ROOT wherever PanelX/PanelY say, whatever    */
  /* container the icon itself lives in - registerPanel, the exact same    */
  /* mechanism every other thing's panel uses                              */
  const panel = document.createElement('div');
  panel.className = 'view-panel';
  panel.style.width = '190px';
  panel.style.height = '220px';

  const header = document.createElement('div');
  header.className = 'view-header';
  const headerTitle = document.createElement('span');
  headerTitle.textContent = baseName(alias);
  headerTitle.title = alias;
  header.appendChild(headerTitle);
  const collapseBtn = document.createElement('span');
  collapseBtn.className = 'node-collapse';
  collapseBtn.textContent = '−';
  collapseBtn.title = 'Close';
  collapseBtn.addEventListener('click', (ev) => {
    ev.stopPropagation();
    /* close over the panel record, not the name - the thing may have    */
    /* been renamed since this button was built                           */
    p.setOpen(false);
  });
  header.appendChild(collapseBtn);
  panel.appendChild(header);

  const innerEl = document.createElement('div');
  innerEl.className = 'view-inner';
  innerEl.dataset.viewAlias = alias;
  panel.appendChild(innerEl);

  const resizeHandle = document.createElement('div');
  resizeHandle.className = 'view-resize-handle';
  resizeHandle.style.display = 'none'; /* shown once Resizeable arrives as "1" */
  /* the CURRENT name, not the birth name - renaming re-keys every map    */
  /* this gesture is about to look in (see aliasOfEl)                       */
  resizeHandle.onpointerdown = (ev) => startResize(ev, aliasOfEl(wrap, alias));
  panel.appendChild(resizeHandle);

  /* a view's only extra behavior on open: a closed view's contents were  */
  /* never sent here - the GUI only holds what it can see - so first open  */
  /* fetches the members; live events keep them current after that.        */
  /* Resolved to the CURRENT alias at open time (aliasOfEl), never the      */
  /* birth name captured here - a rename between birth and first open       */
  /* otherwise lists (and marks this window as viewing) a container that    */
  /* no longer exists, and every event about the real one is then           */
  /* correctly scoped away from us.                                          */
  const p = registerPanel(alias, panel, 'flex', (open) => {
    const cur = aliasOfEl(wrap, alias);
    if (open && !loadedContainers[cur]) {
      loadedContainers[cur] = 1;
      send({ cmd: 'list-instances', container: cur });
    }
  });

  icon.addEventListener('click', () => {
    if (effectiveMode(icon) === 'Operate') p.setOpen(true);
  });

  /* aliasing a view aliases its Open - the alias renders as another icon */
  /* that opens this same panel (see renderAliasControl)                   */
  /* ON THE WRAP, which is where every control binds it (control.js): the
     chrome drags, and anything inside stops propagation and wins. Bound to
     the icon it went missing the moment the icon did, and an embedded view
     could not be moved or cloned. Same binding serves both presentations -
     the icon is inside the wrap, and so are the contents. */
  wrap.onpointerdown = (ev) => {
    /* ONLY THE CHROME DRAGS - control.js's rule, and a View is the one
       control that has contents, so the rule has teeth here. Its chrome is
       the icon when it is an icon, and the header or the frame itself when
       it is drawn in place. A member gets its own gesture and this must
       not steal it: bound with no test at all, a pointerdown on a checkbox
       inside started a second drag that never ended. */
    if (ev.target !== icon && ev.target !== iconLabel
        && ev.target !== header && ev.target !== headerTitle
        && ev.target !== panel && ev.target !== wrap)
      return;
    startDrag(ev, wrap, alias, 'View', 'ReservedViewOpen');
  };
  /* the header floats a FLOATING panel to a new spot. An embedded view is
     not floating - it lives where its container puts it - and leaving this
     bound meant one pointerdown on the header started a panel drag AND the
     view drag: pointerup finishes the panel drag and returns, so the view
     drag never completed and could not be put down. */
  header.onpointerdown = (ev) => {
    if (view.embedded || ev.target === collapseBtn) return;
    startPanelDrag(ev, aliasOfEl(wrap, alias), panel);
  };
  attachDeleteGesture(wrap, alias);
  attachOptionsGesture(wrap, alias);
  /* same as a card: the panel is a root-level peer, so it carries the      */
  /* view's own Options gesture (members' gestures stopPropagation and win)  */
  attachOptionsGesture(panel, alias);

  /* THE OTHER PRESENTATION OF THE SAME OBJECT: drawn in place instead of
     as an icon you open. It is an ordinary control - the wrap IS the
     control, placed in its container at its own X/Y, carrying the same
     drag, clone, alias, delete and options gestures every control carries.
     Nothing here touches those. The only thing that changes is what the
     wrap draws: its contents instead of an icon. */
  function setEmbedded(on) {
    if (on === view.embedded) return;
    view.embedded = on;

    if (on) {
      icon.style.display = 'none';
      panel.classList.add('view-embedded');
      panel.style.position = 'static';	/* it fills the control, not the page */
      panel.style.display = 'flex';
      panel.style.left = '';
      panel.style.top = '';
      panel.style.zIndex = '';
      panel.style.borderRadius = '6px';
      wrap.appendChild(panel);			/* the control's contents */

      /* IT IS ALWAYS OPEN, so it must do what opening does. A view's
         members are fetched by the first open (registerPanel's toggle),
         and that fetch is also what registers this window as VIEWING the
         container - which is what scopes instance-created events to it.
         Without it a drop lands in the engine and the browser is never
         told, and a reload shows nothing either. */
      /* NO CLOSE. Closing hides the panel, and an embedded view has no icon
         to bring it back - the gesture would simply vanish it. It is not
         "open", it is drawn: there is nothing to shut. */
      collapseBtn.style.display = 'none';
      p.lockedOpen = true;

      const curAlias = aliasOfEl(wrap, alias);
      if (!loadedContainers[curAlias]) {
        loadedContainers[curAlias] = 1;
        send({ cmd: 'list-instances', container: curAlias });
      }
    } else {
      icon.style.display = '';
      panel.classList.remove('view-embedded');
      panel.style.position = 'absolute';
      collapseBtn.style.display = '';
      p.lockedOpen = false;
      panel.style.display = 'none';
      $('canvas').appendChild(panel);
    }
  }

  const view = { el: wrap, panel, innerEl, header, resizeHandle, icon, setOpen: p.setOpen,
                 embedded: false, setEmbedded };
  views[alias] = view;
  /* instance-created carries Container inline - see registerWidgetAtom's   */
  /* matching comment                                                       */
  placeInContainer(wrap, container || '');
  flushPendingContainer(alias);

  /* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
  instances[alias] = { className: 'View', el: wrap, ports: {} };
  livePositions[alias] = { el: wrap };

  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'W' });
  send({ cmd: 'subscribe', instance: alias, port: 'H' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });
  send({ cmd: 'subscribe', instance: alias, port: 'ReservedViewResizeable' });
  send({ cmd: 'subscribe', instance: alias, port: 'ReservedViewEmbedded' });

  /* purely a visual cue, shown only in Connect mode: this view names a
     control that stands in for it on that side. Not clickable, not a port,
     nothing subscribes to it - just a mark so you can see it is wireable. */
  if (reservedIn)  addStandInMark(icon, 'in', alias, reservedIn);
  if (reservedOut) addStandInMark(icon, 'out', alias, reservedOut);

  log('created ' + alias + ' (View)', 'event');
}

function startResize(ev, alias) {
  const view = views[alias];
  if (!view || view.resizeHandle.style.display === 'none') return;
  ev.stopPropagation();
  ev.preventDefault();
  const rect = view.panel.getBoundingClientRect();
  resizeState = { alias, el: view.panel, startW: rect.width, startH: rect.height, startX: ev.clientX, startY: ev.clientY };
}

/* Move all "drag and drop into any view" with no view special-cased.       */
function dropTargetAt(ev, ignoreEl) {
  let restore = null;
  if (ignoreEl) {
    restore = ignoreEl.style.pointerEvents;
    ignoreEl.style.pointerEvents = 'none';
  }
  const hit = document.elementFromPoint(ev.clientX, ev.clientY);
  if (ignoreEl) ignoreEl.style.pointerEvents = restore || '';

  const inner = hit && hit.closest('.view-inner');
  if (inner && inner.dataset.viewAlias && (!ignoreEl || !ignoreEl.contains(inner))) {
    const rect = inner.getBoundingClientRect();
    return {
      container: inner.dataset.viewAlias,
      x: Math.max(0, ev.clientX - rect.left + inner.scrollLeft),
      y: Math.max(0, ev.clientY - rect.top + inner.scrollTop),
    };
  }

  const canvas = $('canvas');
  const rect = canvas.getBoundingClientRect();
  return {
    container: ROOT_VIEW,	/* the canvas is the root view, not "nowhere" */
    x: Math.max(0, ev.clientX - rect.left),
    y: Math.max(0, ev.clientY - rect.top),
  };
}

/* the class's entry point: render one instance of me */
register('View', {
  renderInstance: (s) => registerView(s.alias, s.props, s.pos, s.hidden,
                                      s.container, s.reservedIn, s.reservedOut),
});
