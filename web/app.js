/*

GrokThink web client: palette + canvas + widgets, talking to a Bridge
object over WebSocket. Vanilla JS, no build step - matches the
framework's own "no external dependencies" discipline.

The whole app is a thin veneer over the Bridge's verbs (create-instance,
connect, disconnect, set-property, activate, subscribe), same as the
Bridge itself is a veneer over the C API. This file never talks to the
framework directly - only ever through send()/JSON messages.

Recursion, not raw HTML: every control rendered on a node card is backed
by a REAL registered object instance of that control's class, wired to the node it
decorates with genuine Connect() calls (connect reaches any property or
an Activate directly now - the bind-* verbs are retired) - never a bare
<input> firing set-property straight at the target. A composite object
(a Reader, say) is instantiated as a COLLECTION of those widget
objects, not drawn as chrome.

The recursion has to bottom out somewhere: a widget class's OWN Value/
State/activate is rendered directly against itself (that's the base
case - a Textbox's value control can't itself be another Textbox).
Every other class gets each control wrapped in a fresh widget instance.

Widget numbers below mirror the PropertyType enum in object.h, for the
classes this file still renders itself:
  1 TEXTBOX  3 BUTTON  4 CHECKBOX  5 SLIDER
  6 VUMETER  7 TEXTOUT  8 KNOB  9 LABEL  10 NULL (a port, not a widget)
A number missing from that list belongs to a class that brings its own
presentation - it says so itself and the Bridge passes it on, so nothing
here needs to hold it.

*/

/* Router puts HTTP and WebSocket on the same TCP port (the whole point   */
/* being: only one hole needs to be open in a firewall) - so the socket   */
/* connects back to whatever port this page itself was loaded from,       */
/* rather than a second hardcoded one.                                    */
const ROOT_VIEW = '/Root';   /* the canvas IS this view - see placeInContainer */

const WS_PORT = location.port || (location.protocol === 'https:' ? 443 : 80);

/* The only widget numbers left here are the ones the HOST itself acts on:
   a doorway it opens, a menu row it points a control at, and the default it
   falls back to for an atom with no Value. Which class RENDERS a given
   number is not in this file at all - each class says so itself
   (PublishShow) and the Bridge passes it on as GTWidgetTypes. */
const PROP_TEXTOUT = 7;


/* WHAT THE CONTROLS THEMSELVES BROUGHT. widgets.js is assembled by the
   Bridge out of each class's own Show/web and loaded before this file, so
   by the time anything renders the registrations are here. The host holds
   no list of them, learns nothing about any one of them, and asks these
   two questions instead of knowing the answers.

   The maps above are what has not moved yet. As each control brings its
   own half, its entry leaves them; when the last one goes, so do they. */
function widgetModule(name) {
  return (window.GTWidgets && window.GTWidgets[name]) || null;
}


/* the class that renders a property of this widget type - the class said so
   itself (its own Value is declared as that type), the Bridge passed it on */
function widgetClassForType(widget) {
  return (window.GTWidgetTypes && window.GTWidgetTypes[widget]) || null;
}

let ws = null;
let classes = {};          // className -> [{Name,Widget,Default}, ...]
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
let instances = {};        // alias -> {className, el, ports: {name: dotEl}}
let selfDisplays = {};     // "alias.propName" -> {el,widgetClass}, for a widget class's own State shown on itself
let liveControls = {};     // "alias.propName" -> {el,widgetClass}, an editable control synced from its own property-changed events
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
let portDisplays = {};     // "alias.portName" -> {el,widgetClass}, a readout painted from the port's message-flowed traffic
let wires = [];            // {fromAlias, fromPort, toAlias, toPort, lineEl}
let cardBodies = {};       // cardAlias -> {addMemberRow}, a card panel waiting to grow rows from its internals view's members
let internalsOwner = {};   // internals view alias -> the instance it dissects (learned from the internals event)
let internalsAskMode = {}; // instance alias -> 'card'|'options', which gesture asked for internals (purely which PANEL this window then shows)
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
let pendingPort = null;    // {alias, port, dotEl} - first end of a wire being drawn
let dragState = null;      // {alias, offsetX, offsetY}
let gestureDrag = null;    // {kind:'clone'|'alias', data, ghost} - a Clone/Alias carry in progress (the ghost, not the source)
let panelDrag = null;      // {alias, el, offsetX, offsetY} - a view PANEL being moved by its titlebar (PanelX/PanelY, not X/Y)
let livePositions = {};    // alias -> {el}, a card whose X/Y are real properties kept in sync like anything else
let menuButtons = {};      // alias -> {label,items,selected,btn,dropdown}, any MenuButton (topbar chrome or dropped-in)
let propertyValues = {};   // "alias.propName" -> last known value, from property-changed - what Clone reads to copy a source's configuration
let loadedContainers = {}; // view alias -> 1, containers whose members this client has already fetched (lazy - on first open)
let panels = {};           // alias -> {el, setOpen, openApplied} - EVERY thing's open panel, view or card alike: icons nest in the hierarchy, panels are all peers at the root
let views = {};             // alias -> {el, innerEl, mode}, a real View instance's own rendering
let pendingContainer = {};  // containerAlias -> [el, el, ...], elements waiting for a View that hasn't rendered yet

/* the session's own current interaction mode - a real property (Chrome's  */
/* ModeMenu instance, "Selected") kept in sync the exact same way anything */
/* else is, so switching mode is visible to every connected window, not     */
/* just this one - see onPropertyChanged's ModeMenu special case below and  */
/* applyMode(). "Operate" matches BuildChrome's own default (object.c).     */
let currentMode = 'Operate';

/* armed by File > Export: the next thing clicked is the view to export - a
   client-local one-shot (like the file dialog, it collects intent, then sends
   ONE verb, export-flow). Cleared by the click that consumes it or by Escape. */
let pendingExport = false;
let pendingWholeLoad = false;  // a whole-session load is in flight - rebuild the page when it lands

function $(id) { return document.getElementById(id); }

/* what a thing is CALLED on screen is just its name - the path is where   */
/* it lives, and you can see that from the view you found it in. The full  */
/* path exists for scripting (it IS the alias every command uses); it's    */
/* just not smeared across every label.                                     */
function baseName(alias) {
  const i = alias.lastIndexOf('/');
  return i < 0 ? alias : alias.slice(i + 1);
}

/* gestures were built with the alias a thing was BORN with - but moving   */
/* into a view (or editing Name) RENAMES it, and a command sent to the old  */
/* name is a command sent to nothing. Resolve the element's CURRENT alias   */
/* at gesture time, so a thing can be renamed and moved forever.             */
function aliasOfEl(el, fallback) {
  for (const k in instances) if (instances[k].el === el) return k;
  return fallback;
}

/* mode governs how the canvas responds to the mouse - a body class drives */
/* the CSS side (port dots/rows only in mode-connect, inline controls only */
/* in mode-operate) while the JS side (startDrag, onPortClick, the delete   */
/* gesture) reads currentMode directly. Leaving Connect mode is when         */
/* "connections go away" - the wires drawn for it are Connect-mode-scoped,   */
/* not a permanent overlay, so they're cleared here rather than left to      */
/* accumulate stale lines across mode switches.                              */
function applyMode(mode) {
  const prevMode = currentMode;
  currentMode = mode;

  document.body.className = document.body.className
    .split(' ')
    .filter((c) => c && !c.startsWith('mode-'))
    .concat('mode-' + mode.toLowerCase())
    .join(' ');

  if (prevMode === 'Connect' && mode !== 'Connect') {
    for (const w of wires) removeWire(w);
    wires = [];
    if (pendingPort) {
      pendingPort.el.classList.remove('armed');
      pendingPort = null;
    }
  }

  if (mode === 'Connect' && prevMode !== 'Connect') {
    send({ cmd: 'list-connections' });
  }
}

/* where something renders is Container, an ordinary property (object.h/   */
/* object.c) - "" means the top-level canvas, anything else names a View's  */
/* own alias. Container arrives the same asynchronous way X/Y does (a       */
/* subscribe, corrected once the real value echoes back), so an element     */
/* may need to be re-parented after it already rendered once, and the       */
/* View it names may not have rendered yet at all - pendingContainer is      */
/* the queue for that second case, flushed by registerView once the real    */
/* View shows up.                                                           */
function placeInContainer(el, containerAlias) {
  /* the root view IS the canvas - it is an ordinary View like any other,
     it just happens to be the one this window is showing */
  if (!containerAlias || containerAlias === ROOT_VIEW) {
    $('canvas').appendChild(el);
    return;
  }

  const view = views[containerAlias];
  if (view) {
    view.innerEl.appendChild(el);
    return;
  }

  $('canvas').appendChild(el);
  (pendingContainer[containerAlias] = pendingContainer[containerAlias] || []).push(el);
}

function flushPendingContainer(viewAlias) {
  const pending = pendingContainer[viewAlias];
  if (!pending) return;
  delete pendingContainer[viewAlias];
  for (const el of pending) views[viewAlias].innerEl.appendChild(el);
}

/* every thing works identically: its icon lives in the containment       */
/* hierarchy, and its open panel is a peer of every other panel at the     */
/* ROOT - never nested. One mechanism for views and cards alike: PanelX/   */
/* PanelY are shared properties (where the panel sits, for everyone),      */
/* Open's stored value is only the INITIAL presentation, and whether this   */
/* window currently shows the panel is its own business after that.         */
/* Two rules, one counter: anything NEW goes on top, anything CLICKED goes
   on top. Starts above the CSS baselines (panels 5, menus 20) and below the
   modal/ghost layers (300+), and only ever counts up. */
let topZ = 100;

function toTop(el) {
  if (el) el.style.zIndex = String(++topZ);
}

document.addEventListener('pointerdown', (ev) => {
  for (let el = ev.target; el && el.classList; el = el.parentElement) {
    if (el.classList.contains('view-panel') || el.classList.contains('instance-wrap')
        || el.classList.contains('widget-atom') || el.classList.contains('menu-dropdown')
        || el.classList.contains('menu-button-wrap'))
      toTop(el);
  }
}, true);



/* one session mode, everywhere - a View used to be able to override the   */
/* mode for its contents (the old "the Palette is a permanent Clone         */
/* station" mechanism), but there are no special views anymore: the         */
/* palette is just a View, Root is just a View, and every gesture works     */
/* the same inside all of them. Kept as a function because every gesture    */
/* already calls it.                                                         */
/* The mode menu is an ordinary object - moveable, clonable, deletable
   like anything else. What keeps the session escapable is not an
   exemption but a rule: TOUCHING the mode menu returns you to Operate
   (makeMenuButtonEl), and its own click runs before the mode gesture
   that is bubbling up behind it. Esc does the same from anywhere. */
function modeMenuAlias() {
  /* by NAME, not by a hardcoded path - the menu is an ordinary instance
     and can live anywhere a session puts it */
  for (const k in instances) if (baseName(k) === 'ModeMenu') return k;
  return null;
}

/* The mode is the session's mode, full stop. There is one. */
function effectiveMode(el) {
  return currentMode;
}

/* Connection state is the whole screen, not a word in a corner: anything
   other than a live session greys the canvas out and says why in the
   middle of it, so a dead session cannot be fiddled with by accident. */
function setStatus(text, cls) {
  const live = (cls === 'ready');
  document.body.classList.toggle('offline', !live);
  const msg = $('offline-msg');
  if (msg && !live) msg.textContent = text;
}

/* the hardcoded Activity panel this used to render into has been removed - */
/* a dev-console breadcrumb for now, since plenty of call sites still want   */
/* a low-level trace of client actions (clone, rename, errors) that have no  */
/* source port to wire from.                                                 */
function log(text, cls) {
  console.log((cls ? '[' + cls + '] ' : '') + text);
}

function send(cmd) {
  /* every command this client issues, traced. Without this a gesture that
     silently fails to send is indistinguishable from one the engine
     ignored, which cost a whole debugging session. */
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    console.warn('[send DROPPED - socket not open]', cmd);
    return;
  }
  console.log('[send]', cmd);
  ws.send(JSON.stringify(cmd));
}

/* the "interface" field is a full node-tree (NodeToText's own shape),
   not a flat object - {name,type,value,props,children} - this walks
   a node's props to pull out one by name */
function nodeProp(node, name) {
  if (!node.props) return undefined;
  for (const p of node.props) {
    if (p.name === name) return p.value;
  }
  return undefined;
}

function connectSocket() {
  ws = new WebSocket('ws://' + location.hostname + ':' + WS_PORT);

  ws.onopen = () => {
    /* there is no separate "what classes exist" step - list-instances   */
    /* IS the whole view: Palette (one real instance per class, doubling */
    /* as the palette panel) and Root (the shared session, however many  */
    /* other clients have been building it) arrive the same way, each    */
    /* instance-created event self-contained (class, parent group, and   */
    /* its class's full Interface inline) - see bridge.c's doc comment   */
    setStatus('loading view…', 'connecting');
    send({ cmd: 'list-instances' });
  };

  ws.onclose = () => setStatus('disconnected', 'error');
  ws.onerror = () => setStatus('connection error', 'error');

  ws.onmessage = (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch (e) { return; }
    handleEvent(msg);
  };
}

function handleEvent(msg) {
  switch (msg.event) {
    case 'instances-done':
      setStatus('ready', 'ready');
      /* members that rendered after the Connect-mode listing (a view      */
      /* opened mid-mode) have no wires yet - re-list; drawWire dedupes    */
      if (currentMode === 'Connect') send({ cmd: 'list-connections' });
      break;
    case 'instance-created':
      /* client-only GUI_ annotations ride the birth event, because a control
         on a canvas has no panel open and nothing subscribes to them. They
         land in propertyValues like any other value, so everything that
         reads them (guiAnnotation) works the same whether they arrived here
         or from an options panel's own subscriptions. */
      if (msg.gui) for (const k in msg.gui) propertyValues[msg.instance + '.' + k] = msg.gui[k];
      onInstanceCreated(msg.instance, msg.class, msg.parent, msg.interface, msg.hidden, msg.container,
                        msg.reservedIn, msg.reservedOut, msg.classParent);
      break;
    case 'property-changed':
      onPropertyChanged(msg.instance, msg.port, msg.value);
      break;
    case 'message-flowed':
      onMessageFlowed(msg.instance, msg.port, msg.value);
      break;
    case 'connected':
      onConnected(msg.from, msg.fromPort, msg.to, msg.toPort);
      break;
    case 'disconnected':
      onDisconnected(msg.from, msg.fromPort, msg.to, msg.toPort);
      break;
    case 'connections-done':
      break;
    case 'instance-removed':
      onInstanceRemoved(msg.instance);
      break;
    case 'instance-renamed':
      onInstanceRenamed(msg.from, msg.to);
      break;
    case 'internals':
      onInternals(msg.instance, msg.view);
      break;
    case 'flow-file':
      onFlowFile(msg.file);
      break;
    case 'flows-done':
      break;
    case 'flow-saved':
      log('saved ' + msg.file, 'event');
      closeFlowDialog();
      break;
    case 'flow-loaded':
      /* Re-list, never reload. A reload destroys the whole session - every
         panel, every position in the stack - and rebuilds it in whatever
         order the bridge announces, so an imported view came back buried.
         Re-listing adds only what is new: registerView bails on aliases it
         already has, so existing panels are untouched and the new ones
         arrive as ordinary instance-created events and land on top. */
      log('loaded ' + msg.file, 'event');
      closeFlowDialog();
      if (pendingWholeLoad) {
        /* the session it was showing no longer exists - start clean */
        pendingWholeLoad = false;
        window.location.reload();
        break;
      }
      /* an import only ADDS: re-list so the new content appears, and leave
         everything already on screen exactly where it is */
      send({ cmd: 'list-instances' });
      if (currentMode === 'Connect') send({ cmd: 'list-connections' });
      break;
    case 'error':
      log('error (' + msg.cmd + '): ' + msg.message, 'error');
      break;
    default:
      break;
  }
}

function parseInterface(interfaceNode) {
  return (interfaceNode && interfaceNode.children || []).map((p) => ({
    Name: nodeProp(p, 'Name'),
    Widget: parseInt(nodeProp(p, 'Widget'), 10),
    Default: nodeProp(p, 'Default'),
  }));
}

/* an alias is always the instance's CURRENT full path (/Root/... here,     */
/* /Root/Palette/... for BuildPalette's bootstrap instances - object.c).     */
/* Moving an instance to a different View changes both Container AND this    */
/* path together (Bridge_Rename, bridge.c) - a client re-keys everything it  */
/* has stored under the old alias to the new one (onInstanceRenamed).         */
/* Creation itself is one verb: create-instance carries class/container/x/y   */
/* and the SERVER names the result - the client learns the name from the      */
/* instance-created event, never mints one.                                    */




/* the frame's own base look, injected ahead of the value - the panel      */
/* theme carried inside the sandbox, since outside styles can't reach in   */

/* a MenuButton, wherever it appears - the topbar's File/Mode chrome and a  */
/* dropped-in MenuButton instance are the same object rendered the same     */
/* way (registerWidgetAtom below reuses this too). Label/Items/Selected are */
/* just ordinary properties, subscribed like anything else - the button's   */
/* own text and its dropdown's contents only become correct once the        */
/* subscribe echoes their current values back (Bridge_Subscribe pushes the   */
/* current value immediately, so this is near-instant, not a visible wait).  */
function makeMenuButtonEl(alias) {
  const wrap = document.createElement('div');
  wrap.className = 'menu-button-wrap';

  const btn = document.createElement('button');
  btn.className = 'menu-button';
  btn.textContent = 'Menu';

  const dropdown = document.createElement('div');
  dropdown.className = 'menu-dropdown';
  dropdown.style.display = 'none';

  const state = { label: 'Menu', items: [], selected: '' };

  function renderLabel() {
    btn.textContent = state.label + (state.selected ? ': ' + state.selected : '');
  }

  function renderItems() {
    dropdown.innerHTML = '';
    for (const item of state.items) {
      if (!item) continue;
      const row = document.createElement('div');
      row.className = 'menu-item';
      row.textContent = item;
      row.onclick = (ev) => {
        ev.stopPropagation();
        dropdown.style.display = 'none';
        /* a file action needs a filename before it becomes ONE verb -    */
        /* the dialog collects it (user input, the client's legitimate    */
        /* job) and sends save-flow/load-flow/import-flow itself. Every    */
        /* other menu selection is an ordinary property write.             */
        /* by NAME, not by a bare alias: these menus have real paths now
           ('/Root/FileMenu'), so an equality test against the short name
           silently stopped matching and Save fell through to a plain
           property write with no dialog. */
        if (baseName(alias) === 'FileMenu' && (item === 'Save' || item === 'Load' || item === 'Import')) {
          openFlowDialog(item);
          return;
        }
        /* Export is not a whole-session file action - it arms "click the view
           to export", and the next click opens the dialog named after it */
        if (baseName(alias) === 'FileMenu' && item === 'Export') {
          pendingExport = true;
          document.body.classList.add('mode-export');
          return;
        }
        send({ cmd: 'set-property', instance: cur(alias), prop: 'Selected', value: item });
      };
      dropdown.appendChild(row);
    }
  }

  btn.onclick = (ev) => {
    /* the pointerdown that exited a mode already opened this - do not
       toggle it straight back shut */
    const self = (menuButtons[alias] || [])[0];
    if (self && self.justOpened) {
      self.justOpened = false;
      ev.stopPropagation();
      return;
    }

    ev.stopPropagation();
    dropdown.style.display = dropdown.style.display === 'none' ? 'block' : 'none';
    /* an open menu belongs on top of whatever it hangs over */
  };

  wrap.appendChild(btn);
  wrap.appendChild(dropdown);

  /* list, not a single slot - same reasoning as bindLiveControl/            */
  /* makeSelfDisplay above, in case this MenuButton is ever Copy'd too       */
  (menuButtons[alias] = menuButtons[alias] || []).push({ state, renderLabel, renderItems, dropdown });

  send({ cmd: 'subscribe', instance: alias, port: 'Label' });
  send({ cmd: 'subscribe', instance: alias, port: 'Items' });
  send({ cmd: 'subscribe', instance: alias, port: 'Selected' });

  return wrap;
}

/* --- the file dialog: pure presentation over two engine facts ---------- */
/* list-flows says what exists in saved/; save-flow/load-flow/import-flow  */
/* do the work. The filename is genuine user input - collecting it here    */
/* is the client's legitimate job; the action is still ONE verb carrying   */
/* the whole intent.                                                        */

let flowDialog = null;   // {el, listEl, input, kind} while a dialog is open

function closeFlowDialog() {
  if (flowDialog) {
    flowDialog.el.remove();
    flowDialog = null;
  }
}

function openFlowDialog(kind, opts) {
  opts = opts || {};
  closeFlowDialog();

  const overlay = document.createElement('div');
  overlay.className = 'flow-dialog-overlay';
  overlay.onclick = (ev) => { if (ev.target === overlay) closeFlowDialog(); };

  const box = document.createElement('div');
  box.className = 'node-box flow-dialog';

  const header = document.createElement('div');
  header.className = 'node-header';
  const title = document.createElement('span');
  title.className = 'node-title';
  title.textContent = kind + ' flow';
  const hint = document.createElement('span');
  hint.className = 'node-class';
  hint.textContent = 'saved/';
  header.appendChild(title);
  header.appendChild(hint);
  box.appendChild(header);

  const body = document.createElement('div');
  body.className = 'node-body';
  const listEl = document.createElement('div');
  listEl.className = 'flow-dialog-list';
  body.appendChild(listEl);
  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'flow-dialog-name';
  input.placeholder = 'flow name';
  input.value = opts.name || 'session';
  body.appendChild(input);
  box.appendChild(body);

  const doIt = (ev) => {
    const name = input.value.trim();
    if (!name) return;
    if (kind === 'Export') {
      /* export just the clicked view's subtree (Serializer -> Writer) */
      send({ cmd: 'export-flow', file: name, of: opts.of });
      return;
    }
    if (kind === 'Import') {
      /* import is a DROP, identical to dropping a clone: pick the file
         here, then the next click places the view where it lands (its
         internal links are relative, so it parents anywhere). */
      closeFlowDialog();
      startGestureDrag(ev, 'import', { file: name }, 'import: ' + name);
      return;
    }
    const verb = kind === 'Save' ? 'save-flow' : 'load-flow';
    /* a whole-session LOAD replaces everything that is on screen, so the
       page is rebuilt from scratch when it lands. An IMPORT never gets
       here (it is a drop gesture above) and must NOT reload - it only
       adds, and reloading would throw away the session around it. */
    if (verb === 'load-flow') pendingWholeLoad = true;
    send({ cmd: verb, file: name });
    /* stays open until flow-saved/flow-loaded confirms (handleEvent) -   */
    /* the engine is the one that knows whether it happened               */
  };

  const footer = document.createElement('div');
  footer.className = 'node-footer';
  const go = document.createElement('button');
  go.className = 'activate-btn';
  go.textContent = kind;
  go.onclick = (ev) => doIt(ev);
  const cancel = document.createElement('button');
  cancel.className = 'activate-btn';
  cancel.textContent = 'Cancel';
  cancel.onclick = closeFlowDialog;
  footer.appendChild(go);
  footer.appendChild(cancel);
  box.appendChild(footer);

  input.onkeydown = (ev) => { if (ev.key === 'Enter') doIt(ev); };

  overlay.appendChild(box);
  document.body.appendChild(overlay);
  flowDialog = { el: overlay, listEl, input, kind };
  input.focus();
  input.select();

  /* what exists is the ENGINE's fact - ask, then render what comes back */
  send({ cmd: 'list-flows' });
}

function onFlowFile(file) {
  if (!flowDialog || !file) return;
  const row = document.createElement('div');
  row.className = 'menu-item flow-dialog-file';
  const name = file.replace(/\.flow$/, '');
  row.textContent = name;
  row.onclick = () => { flowDialog.input.value = name; };
  flowDialog.listEl.appendChild(row);
}

/* clicking anywhere outside an open dropdown closes it - ordinary menu    */
/* behavior, nothing property-related about it                            */
document.addEventListener('click', () => {
  for (const alias in menuButtons) for (const m of menuButtons[alias]) {
    /* except one just opened by the touch that exited a mode - that click
       is the same gesture, not a click "outside" it */
    if (m.justOpened) { m.justOpened = false; continue; }
    m.dropdown.style.display = 'none';
  }
});


/* a control's wire anchor: the same .wireable wrapper every control has   */
/* always had (the control inside loses pointer-events outside Operate      */
/* mode, see the CSS, so the click falls through to this wrapper), but the   */
/* endpoint it arms is the REAL instance and property/port name - there is   */
/* no separate object standing in for a control anymore                      */
function wireSlot(alias, propName, controlEl) {
  const wrap = document.createElement('span');
  wrap.className = 'wireable';
  wrap.appendChild(controlEl);
  wrap.addEventListener('click', () => onPortClick(alias, propName, wrap));
  return wrap;
}

/* GUI_* properties: client-only annotations. The engine stores them like any
   other property and never reads them - the prefix IS the rule, so no list of
   blessed names lives here or in the bridge. They arrive through the ordinary
   property-changed path and sit in propertyValues like everything else.
   For now only Textbox honors them.

   GUI_Format is a MASK, not the name of a format - "(###) ###-####" for a
   phone number, and the same mechanism does a date or a zip+4 without another
   line of code here. A '#' takes one digit from the value; every other
   character is punctuation the box supplies for you, so you type 5551234567
   and read (555) 123-4567.

   The mask is display only: the ENGINE holds the digits. Formatting that
   changed the stored value would not be formatting, it would be a second
   version of the data - and two clients with different masks would disagree
   about what the property is. So the box shows masked text, and what leaves
   the browser is always the raw digits.

   GUI_Pattern is a regular expression the RAW value must match. A value that
   fails it gets a red outline and is not sent - the propagation is gated in
   the browser, which is why this is presentation and not truth: nothing else
   writing that same property is bound by it. If a rule has to hold for
   everyone, it belongs on the wire as a Filter, not here. */
function guiAnnotation(alias, name) {
  const v = propertyValues[cur(alias) + '.GUI_' + name];
  return v ? v : null;
}

/* mask -> text. Punctuation is emitted only while digits remain, so a
   half-typed number reads "(555) 12" and not "(555) 12)-    ". */
function guiMaskApply(mask, raw) {
  const digits = (raw || '').replace(/\D/g, '');
  let out = '', d = 0;
  for (const ch of mask) {
    if (d >= digits.length) break;
    if (ch === '#') out += digits[d++];
    else out += ch;
  }
  return out;
}

/* text -> what the engine stores. The mask's punctuation is the browser's,
   never the value's. */
function guiMaskStrip(mask, text) {
  return mask ? (text || '').replace(/\D/g, '') : text;
}

/* a mask is a rule, not decoration: "(###) ###-####" says ten digits, so a
   value that does not fill it is not a phone number. The box takes whatever
   string it is handed - typed, pasted, or written by the engine - and holds
   it to that shape. Anything short (or long) is outlined and never sent. */
function guiMaskComplete(mask, raw) {
  if (!mask) return true;
  const want = (mask.match(/#/g) || []).length;
  return (raw || '').replace(/\D/g, '').length === want;
}

function guiPattern(alias) {
  const src = guiAnnotation(alias, 'Pattern');
  if (!src) return null;
  try { return new RegExp(src); }
  catch (e) { return null; }		/* a malformed pattern gates nothing */
}

/* both annotations judge the RAW value, so a mask and a pattern can never
   contradict each other. Either one failing means no send. */
function guiOk(alias, rawValue) {
  const re = guiPattern(alias);
  return guiMaskComplete(guiAnnotation(alias, 'Format'), rawValue)
         && (!re || re.test(rawValue));
}

function guiValidate(alias, el, rawValue) {
  const ok = guiOk(alias, rawValue);
  /* once a box has held a good value it is ARMED: from then on it says so
     immediately when it stops being good. Before that first good value it
     is just half-typed, which is not an error yet. */
  if (ok) el.guiArmed = true;
  el.classList.toggle('gui-invalid', !ok);
  return ok;
}

/* re-mask what is in the box as it is typed. The caret goes to the end
   because the mask fills left to right as digits arrive - editing the
   middle of a masked value is not something this handles. */
function guiReformat(el) {
  const mask = el.guiAlias && guiAnnotation(el.guiAlias, 'Format');
  if (!mask) return;
  const shown = guiMaskApply(mask, el.value);
  if (shown === el.value) return;
  el.value = shown;
  const sel = window.getSelection();
  const range = document.createRange();
  range.selectNodeContents(el);
  range.collapse(false);
  sel.removeAllRanges();
  sel.addRange(range);
}


/* --- base case: a widget class's own Value/State/activate, rendered against itself --- */




/* the container primitive - a real, resizable panel with an inner area    */
/* holding whatever has Container==this View's own alias (placeInContainer/ */
/* flushPendingContainer). The Palette is nothing more than one of these,    */
/* built by the server with Mode="Clone" and Deletable="0" already set        */
/* (BuildPalette, object.c) - nothing here knows or cares that any            */
/* particular View happens to be "the palette".                              */
/* a dot on one side of an icon: in on the left, out on the right. Visual
   only - no handler, no state, nothing behind it. */
/* the real (instance, property) each stand-in dot speaks for, so a wire
   the engine reports against the inner control can also be drawn against
   the dot - the x-ray view of a shut panel */
let standInDots = [];   // {el, viewAlias, spec} - scanned, never keyed: the
                        // engine reports an endpoint in whichever spelling the
                        // spec used, so the match is a comparison, not a lookup
let knownConnections = {};  // wireKey -> {fromAlias,fromPort,toAlias,toPort} as last reported



let resizeState = null; // {alias, el, startW, startH, startX, startY}


document.addEventListener('pointermove', (ev) => {
  if (!resizeState) return;
  const w = Math.max(80, resizeState.startW + (ev.clientX - resizeState.startX));
  const h = Math.max(60, resizeState.startH + (ev.clientY - resizeState.startY));
  resizeState.el.style.width = w + 'px';
  resizeState.el.style.height = h + 'px';
});

document.addEventListener('pointerup', () => {
  if (resizeState) {
    /* the identical set-property-on-release pattern X/Y drag-end already   */
    /* uses (startDrag's own mouseup handler) - resizing is not a different  */
    /* kind of thing from moving, it's the same write, just on W/H instead   */
    const w = parseInt(resizeState.el.style.width, 10) || 120;
    const h = parseInt(resizeState.el.style.height, 10) || 60;
    send({ cmd: 'set-property', instance: resizeState.alias, prop: 'W', value: String(w) });
    send({ cmd: 'set-property', instance: resizeState.alias, prop: 'H', value: String(h) });
  }
  resizeState = null;
});

/* Containment is the View class's business, and the View class brought its
   own browser half. Anything composite renders through it - a Widget IS a
   View - so this is the one place the host hands rendering over rather than
   knowing how. A base class shipping a different View renders the whole
   session differently, and nothing here changes. */
/* WHO RENDERS AN INSTANCE. The class itself if it brought a renderer, else
   the class it descends from, else the class that holds things. The host
   knows the CHAIN, never the classes - a base class shipping its own
   renderer takes over its whole branch and nothing here is edited. */
function rendererFor(className, classParent) {
  for (const name of [className, classParent, 'View']) {
    const cls = name && widgetModule(name);
    if (cls && cls.renderInstance) return cls.renderInstance;
  }
  console.error('nothing in the class chain can render', className, '(parent', classParent + ')');
  return null;
}

/* one named descriptor, never positional arguments: renderers are written
   by different classes at different times and must not depend on the order
   the host happens to pass things in */
function renderInstanceOf(spec) {
  const render = rendererFor(spec.className, spec.classParent);
  if (render) render(spec);
}



function onInstanceCreated(alias, className, parent, interfaceNode, hidden, container,
                           reservedIn, reservedOut, classParent) {
  /* replays are idempotent - a container listed twice (or an instance     */
  /* that arrived live before its container's members were fetched) never  */
  /* renders twice                                                          */
  if (instances[alias] || views[alias]) return;

  /* hidden marks plumbing, not first-class session content - real          */
  /* server-side state (see Bridge_InstanceEvent's doc comment). A hidden   */
  /* VIEW is different: it is a panel with no icon of its own (an object's   */
  /* internals view - the object's icon is its presence), so it registers    */
  /* normally minus the canvas icon.                                          */
  if (hidden && className !== 'View') return;

  /* every instance-created event carries its class's Interface inline -  */
  /* cache it once per class (every instance of the same class has the    */
  /* identical shape) so later cards don't need to re-parse it            */
  if (!classes[className]) classes[className] = parseInterface(interfaceNode);

  /* the app's own chrome (File/Mode) - real instances too, rendered in    */
  /* the topbar with the same makeMenuButtonEl a dropped-in MenuButton      */
  /* anywhere else uses; no card, no position, ordinary document flow      */
  if (parent === 'Chrome') {
    $('topbar-menus').appendChild(makeMenuButtonEl(alias));
    return;
  }

  const props = classes[className] || [];
  /* birth position/container are the SERVER's facts (atomic birth - the   */
  /* create verb carried them): place it anywhere for now, the X/Y          */
  /* subscribe below corrects it almost immediately.                        */
  const pos = { x: 30, y: 30 };
  container = container || '';

  /* a View is not a special client-side concept (the Palette included -   */
  /* it's just a View whose bootstrap children happen to have Container    */
  /* set already, see BuildPalette, object.c) - it gets its own rendering   */
  /* because it's the one class that actually contains other instances,    */
  /* not because it's a Palette.                                           */
  if (className === 'View') {
    renderInstanceOf({ alias, className, classParent, props, pos, hidden,
                       container, reservedIn, reservedOut });
    return;
  }

  /* a widget primitive has no panel - it's the same base-case rendering   */
  /* An atom is anything whose CLASS descends from Control: a control and a
     label, nothing else - the same base case the recursion bottoms out to
     (makeSelfControl/makeSelfDisplay/makeSelfActivateButton), standing on its
     own instead of sitting inside a composite panel. The engine says what
     kind of thing this is (classParent), so a control written tomorrow
     renders correctly today - this used to be a hardcoded list of the fifteen
     control class names, and anything missing from it drew as a panel. */

  /* A WIDGET IS JUST A VIEW. Any composite object renders as a view -
     an icon that opens a panel whose members lay out by their own X/Y.
     The object does not lay anything out; the view does, exactly as it
     does for a hand-built View or the palette. No card, no stacked rows,
     no special case. */
  renderInstanceOf({ alias, className, classParent, props, pos, hidden: false,
                     container, reservedIn, reservedOut });
}


function onPropertyChanged(alias, port, value) {
  /* every property-changed event already carries the current value -      */
  /* caching it costs nothing and is what makes Clone (below) able to       */
  /* copy a source instance's configuration without a new query mechanism  */
  propertyValues[alias + '.' + port] = value;

  /* while this card is being actively dragged locally, its own outgoing   */
  /* motion is what's authoritative - an echo of the write this same drag  */
  /* is about to commit (or another window's stale view of a position      */
  /* we're already past) shouldn't fight the mouse mid-gesture             */
  const livePos = livePositions[alias];
  if (livePos && (port === 'X' || port === 'Y') && (!dragState || dragState.alias !== alias)) {
    const n = parseInt(value, 10) || 0;
    if (port === 'X') livePos.el.style.left = n + 'px';
    else livePos.el.style.top = n + 'px';
    updateWiresFor(alias);
  }
  /* W/H land on the view's panel in the views branch below - nothing else */
  /* subscribes to them                                                     */

  /* where this instance renders - the top-level canvas ("") or a real       */
  /* View's own inner area (its alias) - arrives the same asynchronous way   */
  /* X/Y does, and can change later exactly like X/Y can (see                */
  /* placeInContainer's own doc comment)                                     */
  if (port === 'Container') {
    const inst = instances[alias];
    if (inst) placeInContainer(inst.el, value);
  }

  /* a View's own rendering properties (the old per-View Mode override is  */
  /* gone - one session mode, every view equal). W/H size the panel, not   */
  /* the wrap, so the closed icon keeps its natural size.                   */
  /* every thing's panel, view and card alike, syncs the same way */
  const pnl = panels[alias];
  if (pnl) {
    /* something new is in this container: ask what is there now. Same
       re-list the client already does for root when a flow lands, but
       aimed at the container that actually changed. */
    if (port === 'LastMember') {
      /* only for a container we have actually opened. Subscribing is
         truth-on-demand - it pushes the CURRENT value straight back - so a
         fresh page load subscribing to a closed view's LastMember got a
         value and listed contents it is not supposed to hold yet. A view we
         have never opened lists itself on open (registerPanel), and that is
         the only time it should. */
      if (value && loadedContainers[alias]) send({ cmd: 'list-instances', container: alias });
      return;
    }
    /* Open's stored value is the initial presentation only - after       */
    /* first paint, open/closed is this window's own business              */
    if (port === 'ReservedViewOpen' && !pnl.openApplied) {
      pnl.openApplied = true;
      pnl.setOpen(value === '1');
    }
    /* same "our own in-flight gesture wins" reasoning as X/Y above */
    else if (port === 'ReservedViewPanelX' && (!panelDrag || panelDrag.alias !== alias)) {
      pnl.el.style.left = (parseInt(value, 10) || 0) + 'px';
      updateWiresFor(alias);
    } else if (port === 'ReservedViewPanelY' && (!panelDrag || panelDrag.alias !== alias)) {
      pnl.el.style.top = (parseInt(value, 10) || 0) + 'px';
      updateWiresFor(alias);
    }
  }

  const view = views[alias];
  if (view) {
    if (port === 'ReservedViewResizeable') view.resizeHandle.style.display = value === '0' ? 'none' : 'block';
    else if (port === 'W' && (!resizeState || resizeState.alias !== alias)) {
      view.panel.style.width = (parseInt(value, 10) || 190) + 'px';
      updateWiresFor(alias);
    } else if (port === 'H' && (!resizeState || resizeState.alias !== alias)) {
      view.panel.style.height = (parseInt(value, 10) || 220) + 'px';
      updateWiresFor(alias);
    }
  }

  /* every rendering subscribed to this alias.prop gets the update - this   */
  /* is the "load the new values in without propagating anything" contract: */
  /* updateReadout/updateLiveControl only ever assign the DOM value/checked  */
  /* directly, they never simulate a user edit, so hydrating N renderings   */
  /* here can never turn into N set-property echoes back out.               */
  const key = alias + '.' + port;
  for (const entry of selfDisplays[key] || []) entry.el.value = value;
  for (const entry of liveControls[key] || []) updateLiveControl(entry, value);

  /* the annotation itself changed - re-judge every box it governs, so
     editing GUI_Pattern in the options panel outlines the offenders now
     instead of at the next unrelated write */
  if (port.startsWith('GUI_')) {
    for (const k in liveControls)
      for (const entry of liveControls[k])
        if (entry.el.guiAlias === alias) {
          const m = guiAnnotation(alias, 'Format');
          const raw = guiMaskStrip(m, entry.el.value);
          entry.el.value = m ? guiMaskApply(m, raw) : raw;
          guiValidate(alias, entry.el, raw);
        }
  }

  for (const menu of menuButtons[alias] || []) {
    if (port === 'Label') { menu.state.label = value; menu.renderLabel(); }
    else if (port === 'Items') { menu.state.items = value.split(','); menu.renderItems(); }
    else if (port === 'Selected') {
      menu.state.selected = value;
      menu.renderLabel();
      /* the Mode menu's Selected IS the session's current mode - a real,   */
      /* synced property, not client-local state, so this is the one place  */
      /* a mode change (from any window) actually takes effect              */
      if (baseName(alias) === 'ModeMenu' && value) applyMode(value);
    }
  }

  log(alias + '.' + port + ' = ' + value, 'event');
}

function onMessageFlowed(alias, port, value) {
  /* an out-port readout (a Pulse's Out, say) paints straight from the      */
  /* port's own traffic - display only, nothing standing in for the port    */
  const key = alias + '.' + port;
  for (const entry of portDisplays[key] || []) entry.el.value = value;

  log(alias + '.' + port + ' → ' + value, 'event');
}


/* wiring: click any property (a row, or a whole widget atom - see
   registerWidgetAtom), then click a second one, in either order - there is
   no source/sink distinction to get right first. Connect-mode-only; el is
   whatever was clicked, reused both as the pending-connection highlight
   and as the wire's anchor point (drawWire/updateWire just read its
   bounding box, they don't care what kind of element it is). */
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
function onPortClick(alias, port, el) {
  if (effectiveMode(el) !== 'Connect') return;

  if (!pendingPort) {
    pendingPort = { alias, port, el };
    el.classList.add('armed');
    return;
  }

  if (pendingPort.alias === alias && pendingPort.port === port) {
    pendingPort.el.classList.remove('armed');
    pendingPort = null;
    return;
  }

  const from = pendingPort;
  pendingPort.el.classList.remove('armed');
  pendingPort = null;
  completeWire(from, { alias, port, el });
}

/* A stand-in dot is a DIRECTED end: the out dot on the right can only
   START a wire, the in dot on the left can only FINISH one. A view itself
   is not a target - only its dots are. */
function onStandInClick(alias, side, spec, el) {
  if (effectiveMode(el) !== 'Connect') return;

  const end = { alias, port: spec, el, standIn: side };

  if (side === 'out') {
    if (pendingPort) {
      console.warn('[connect] the out dot starts a wire, it cannot finish one -',
                   alias, '(' + spec + ')');
      return;
    }
    pendingPort = end;
    el.classList.add('armed');
    return;
  }

  if (!pendingPort) {
    console.warn('[connect] the in dot finishes a wire, it cannot start one -',
                 alias, '(' + spec + ')');
    return;
  }

  const from = pendingPort;
  pendingPort.el.classList.remove('armed');
  pendingPort = null;
  completeWire(from, end);
}

/* what a stand-in spec actually resolves to: bare "TxData" is a property
   on the view itself; "Slider_1/Value" is a property on a control inside
   it. Reported only - the client does not act on it. */
function resolveStandIn(alias, spec) {
  const cut = spec.lastIndexOf('/');
  if (cut < 0) return { instance: alias, prop: spec };
  return { instance: alias + '/' + spec.slice(0, cut), prop: spec.slice(cut + 1) };
}

/* the real (instance, property) an end names - x-ray through a stand-in
   dot to the control it stands for, even with the view shut */
function realEnd(e) {
  return e.standIn ? resolveStandIn(e.alias, e.port)
                   : { instance: e.alias, prop: e.port };
}

function describeEnd(e) {
  const r = realEnd(e);
  return {
    shown: e.standIn ? e.alias + '  [' + e.standIn + ' dot -> "' + e.port + '"]'
                     : e.alias + '.' + e.port,
    real: r.instance + '.' + r.prop,
  };
}

/* One wire, either end of which may be a stand-in dot. If a dot is
   involved we do NOT wire anything yet - we report exactly what would be
   sent and what it would really subscribe to. Everything else behaves as
   it always has. */
function completeWire(from, to) {
  /* A loopback is fine - a view's out into its own in is a real thing to
     want. What is never fine is a property wired to ITSELF: the write
     would deliver straight back into the thing that made it. Compared on
     what the ends RESOLVE to, so a dot and a directly-clicked property
     that land on the same place are caught as well. */
  const fr = describeEnd(from).real, tr = describeEnd(to).real;
  if (fr === tr) {
    console.warn('[connect] refused - both ends are the same property:', fr);
    return;
  }

  /* One ordinary connect between two REAL properties. A stand-in dot is
     resolved here, on the client, to the control it stands for - the engine
     is handed the same command it would get if you had opened the view and
     clicked that control directly, and knows nothing about dots. */
  const F = realEnd(from), T = realEnd(to);

  if (from.standIn || to.standIn) {
    const f = describeEnd(from), t = describeEnd(to);
    console.log(
      '[connect: stand-in]\n' +
      '  from : ' + f.shown + '\n' +
      '  to   : ' + t.shown + '\n' +
      '  subscribing: ' + t.real + '   to   ' + f.real + '\n' +
      '  sending: ' + JSON.stringify({ cmd: 'connect', from: F.instance, fromPort: F.prop,
                                       to: T.instance, toPort: T.prop }));
  }

  /* send the verb; the connected event is the ONLY wire-drawer - same    */
  /* law as delete (readmefirst repair #5): the GUI never invents a line  */
  /* the engine hasn't confirmed, and every other window sees the same    */
  /* event this one does                                                   */
  send({ cmd: 'connect', from: F.instance, fromPort: F.prop, to: T.instance, toPort: T.prop });
}

const SVGNS = 'http://www.w3.org/2000/svg';

/* the view-inner elements above el, innermost first - the containment    */
/* chain as the DOM already encodes it (placeInContainer nests members    */
/* inside their View's inner)                                              */
function innerChain(el) {
  const chain = [];
  for (let e = el; e; e = e.parentElement) {
    if (e.classList && e.classList.contains('view-inner')) chain.push(e);
  }
  return chain;
}

/* which layer a wire renders in: the DEEPEST view both endpoints live     */
/* inside gets it (so a wire between two members of a view sits IN that    */
/* view - moves when it moves, hides when it closes); anything spanning    */
/* containers falls back to the root overlay. Each view-inner grows its    */
/* own svg on first use.                                                    */
function wireLayerFor(fromEl, toEl) {
  const toInners = new Set(innerChain(toEl));
  for (const inner of innerChain(fromEl)) {
    if (!toInners.has(inner)) continue;
    let svg = inner.querySelector(':scope > svg.view-wires');
    if (!svg) {
      svg = document.createElementNS(SVGNS, 'svg');
      svg.setAttribute('class', 'view-wires');
      inner.appendChild(svg);
    }
    return svg;
  }
  return $('wires');
}

function wireKey(fromAlias, fromPort, toAlias, toPort, tag) {
  return fromAlias + '.' + fromPort + '>' + toAlias + '.' + toPort + (tag ? '#' + tag : '');
}

/* the arrowhead, defined once in the root overlay and shared by every wire
   layer - an url(#id) reference resolves document-wide, so the per-view svgs
   use this one rather than each growing their own */
function ensureWireArrow() {
  const root = $('wires');
  if (!root || root.querySelector('#wire-arrow')) return;

  const defs = document.createElementNS(SVGNS, 'defs');
  const m = document.createElementNS(SVGNS, 'marker');
  m.setAttribute('id', 'wire-arrow');
  m.setAttribute('viewBox', '0 0 10 10');
  m.setAttribute('refX', '9');        /* the tip sits at the line's end */
  m.setAttribute('refY', '5');
  m.setAttribute('markerWidth', '7');
  m.setAttribute('markerHeight', '7');
  m.setAttribute('orient', 'auto');   /* follows the line, so it always points at the target */
  const path = document.createElementNS(SVGNS, 'path');
  path.setAttribute('d', 'M 0 0 L 10 5 L 0 10 z');
  path.setAttribute('fill', '#6b9fe6');   /* matches the stroke in style.css */
  m.appendChild(path);
  defs.appendChild(m);

  /* the same head in the stand-in colour - a marker cannot inherit the
     stroke of the line it sits on, so a second colour needs a second one */
  const s = m.cloneNode(true);
  s.setAttribute('id', 'wire-arrow-standin');
  s.firstChild.setAttribute('fill', '#a06be6');
  defs.appendChild(s);
  root.appendChild(defs);
}

/* tag distinguishes the RENDERINGS of one connection - the same wire drawn
   against the inner controls and against the stand-in dots. Both carry the
   same endpoints, so the x removes the one connection and onDisconnected
   erases every rendering of it. */
function drawWire(fromAlias, fromPort, fromEl, toAlias, toPort, toEl, tag, viaDot) {
  const key = wireKey(fromAlias, fromPort, toAlias, toPort, tag);
  if (wires.some((w) => w.key === key)) return;   /* a wire spanning two views is announced once per view */

/* both ends on the SAME anchor is not a wire: it draws as a zero-length
   segment, and a zero-length line still renders its marker-end - a bare
   arrowhead sitting on a dot that has nothing connected to it */
   if (fromEl === toEl) {
      console.log('[wire] both ends on one anchor, not drawn: ' +
        fromAlias + '.' + fromPort + ' -> ' + toAlias + '.' + toPort + '  tag=' + tag);
      return;
    }

  ensureWireArrow();
  const svg = wireLayerFor(fromEl, toEl);
  /* a view wired to itself is drawn as a loop ARCING OVER the icon, not a
     straight segment through it - the shape says "this comes back to me" */
  const loop = fromAlias === toAlias;
  const line = document.createElementNS(SVGNS, loop ? 'path' : 'line');
  /* a rendering anchored on a stand-in dot is the x-ray view of a shut
     panel, not a wire between two things you can see - coloured apart */
  const standin = !!viaDot;
  if (standin) line.classList.add('standin');
  line.setAttribute('marker-end', standin ? 'url(#wire-arrow-standin)' : 'url(#wire-arrow)');
  svg.appendChild(line);

  /* the mid-wire "x": sends disconnect - the disconnected event is the   */
  /* only remover, exactly as connected was the only drawer               */
  const x = document.createElementNS(SVGNS, 'text');
  x.setAttribute('class', 'wire-x');
  x.textContent = '×';
  x.addEventListener('click', (ev) => {
    ev.stopPropagation();
    send({ cmd: 'disconnect', from: fromAlias, fromPort, to: toAlias, toPort });
  });
  svg.appendChild(x);

  const wire = { key, fromAlias, fromPort, fromEl, toAlias, toPort, toEl, line, x, svg, loop };
  wires.push(wire);
  updateWire(wire);
  log(fromAlias + '.' + fromPort + ' → ' + toAlias + '.' + toPort + ' connected', 'event');
}

function removeWire(wire) {
  wire.line.remove();
  wire.x.remove();
}

function updateWire(wire) {
  /* coordinates are relative to the wire's own layer: the view-inner it   */
  /* hangs in, or the root canvas-wrap - the same element that scrolls it  */
  const origin = wire.svg.id === 'wires' ? $('canvas-wrap') : wire.svg.parentElement;
  const oRect = origin.getBoundingClientRect();
  const a = wire.fromEl.getBoundingClientRect();
  const b = wire.toEl.getBoundingClientRect();
  const x1 = a.left + a.width / 2 - oRect.left + origin.scrollLeft;
  const y1 = a.top + a.height / 2 - oRect.top + origin.scrollTop;
  const x2 = b.left + b.width / 2 - oRect.left + origin.scrollLeft;
  const y2 = b.top + b.height / 2 - oRect.top + origin.scrollTop;
  if (wire.loop) {
    /* out on the right, in on the left: bow up and over the icon and come
       back down the other side. The rise scales with the gap so a wide
       widget gets a proportionally wide loop rather than a flat smear. */
    const rise = Math.max(34, Math.abs(x1 - x2) * 0.9);
    const cy = Math.min(y1, y2) - rise;
    wire.line.setAttribute('d',
      'M ' + x1 + ' ' + y1 +
      ' C ' + (x1 + rise * 0.5) + ' ' + cy +
      ' ' + (x2 - rise * 0.5) + ' ' + cy +
      ' ' + x2 + ' ' + y2);
    /* the x sits at the apex, clear of the icon it arcs over */
    wire.x.setAttribute('x', (x1 + x2) / 2);
    wire.x.setAttribute('y', cy + rise * 0.25);
    return;
  }

  /* neither end runs to a centre: pull each back to where the line crosses
     its own box and leave a gap, so the wire points AT both things instead
     of burying itself in the middle of them */
  const dx = x2 - x1, dy = y2 - y1;
  const len = Math.hypot(dx, dy);
  let sx = x1, sy = y1, ex = x2, ey = y2;
  if (len > 8) {
    const edge = (r) => len * Math.min(
      Math.abs(dx) > 0.01 ? (r.width / 2) / Math.abs(dx) : Infinity,
      Math.abs(dy) > 0.01 ? (r.height / 2) / Math.abs(dy) : Infinity) - 4;
    let ca = edge(a), cb = edge(b);
    if (ca + cb > len - 6) { const k = (len - 6) / (ca + cb); ca *= k; cb *= k; }
    sx = x1 + dx / len * ca; sy = y1 + dy / len * ca;
    ex = x2 - dx / len * cb; ey = y2 - dy / len * cb;
  }

  wire.line.setAttribute('x1', sx);
  wire.line.setAttribute('y1', sy);
  wire.line.setAttribute('x2', ex);
  wire.line.setAttribute('y2', ey);
  wire.x.setAttribute('x', (sx + ex) / 2);
  wire.x.setAttribute('y', (sy + ey) / 2);
}

function updateWiresFor(alias) {
  for (const w of wires) {
    if (w.fromAlias === alias || w.toAlias === alias) updateWire(w);
  }
}

/* a wire exists - a live connect (this window's or anyone's), or one      */
/* replayed by list-connections on entering Connect mode; both arrive       */
/* here and draw identically (drawWire dedupes the overlap). Wires are      */
/* Connect-mode presentation, so outside Connect mode nothing is drawn -    */
/* re-entering the mode re-lists. Silently skipped if either end isn't      */
/* rendered by this client (a closed view's member, hidden plumbing);       */
/* instances-done re-lists so late-rendered members get their wires.        */
/* an element EXISTS but is not drawable while it sits inside a closed
   panel (display:none) - its rect is all zeros, and anchoring a wire to it
   puts that end in the canvas corner. Existence is not renderedness. */
function isDrawable(el) {
  if (!el) return false;
  const r = el.getBoundingClientRect();
  return r.width > 0 || r.height > 0;
}

/* Every anchor this end can be drawn against: the control itself when its
   panel is open and it is actually rendered, plus any stand-in dot that
   speaks for it - so a connection into a shut view still shows, on the dot. */
function anchorsFor(alias, port) {
  const out = [];
  const inst = instances[alias];
  const own = inst && inst.ports[port];
  if (own && isDrawable(own)) out.push({ el: own, tag: 'ctl' });
  /* scan every dot and ask whether it speaks for THIS endpoint: a spec is
     accepted in more than one spelling and the engine reports whichever one
     it resolved to, so a computed key misses where a comparison does not */
  standInDots.forEach((d, i) => {
    if (!isDrawable(d.el)) return;
    const cut = d.spec.lastIndexOf('/');
    const hit = cut < 0
      ? (d.viewAlias === alias && d.spec === port)
        || (alias === d.viewAlias + '/' + d.spec)
      : (alias === d.viewAlias + '/' + d.spec.slice(0, cut)
         && port === d.spec.slice(cut + 1));
    /* byName: the dot's spec named this CONTROL, rather than a property
       on the view itself - only that is standing in for something you
       cannot see, and only that takes the stand-in colour */
    if (hit) out.push({ el: d.el, tag: 'dot' + i, byName: alias !== d.viewAlias });
  });
  return out;
}

/* re-attempt every connection already reported: drawWire dedupes by anchor,
   so this adds the lines that just became drawable and touches nothing else */
function redrawKnownWires() {
  for (const k in knownConnections) {
    const c = knownConnections[k];
    onConnected(c.fromAlias, c.fromPort, c.toAlias, c.toPort);
  }
}

function onConnected(fromAlias, fromPort, toAlias, toPort) {
  /* remembered whatever the mode is, so opening a panel later can redraw
     from here instead of asking the engine to re-list the whole session */
  knownConnections[wireKey(fromAlias, fromPort, toAlias, toPort)] =
    { fromAlias, fromPort, toAlias, toPort };

  if (currentMode !== 'Connect') return;

  const froms = anchorsFor(fromAlias, fromPort);
  const tos = anchorsFor(toAlias, toPort);

  const hasDot = (l) => l.some((a) => a.byName);

  /* Only a connection touching a stand-in dot is reported - that is a
     handful, not the whole session, so this stays readable. */
  const fk = fromAlias + '.' + fromPort, tk = toAlias + '.' + toPort;
  if (hasDot(froms) || hasDot(tos)) {
    const show = (list) => list.length
      ? list.map((a) => a.tag).join(', ')
      : 'nothing drawable (panel shut, no dot)';
    console.log(
      '[wire: stand-in]\n' +
      '  from : ' + fk + '   anchors: ' + show(froms) + '\n' +
      '  to   : ' + tk + '   anchors: ' + show(tos) + '\n' +
      '  lines: ' + (froms.length * tos.length));
  }

  /* the colour belongs to the CONNECTION, not to one drawing of it - a
     connection standing in for a named control is that colour in every
     rendering, and everything else stays normal */
  const viaDot = hasDot(froms) || hasDot(tos);

  if (!froms.length || !tos.length) return;

  /* one line per pair of anchors: control-to-control when both panels are
     open, dot-to-control, dot-to-dot - all the same one connection */
  for (const f of froms)
    for (const t of tos)
      drawWire(fromAlias, fromPort, f.el, toAlias, toPort, t.el,
               f.tag + '-' + t.tag, viaDot);
}

/* the one wire-remover, mirroring onConnected the drawer */
function onDisconnected(fromAlias, fromPort, toAlias, toPort) {
  delete knownConnections[wireKey(fromAlias, fromPort, toAlias, toPort)];

  /* matched on the connection, not on one drawing of it: the same wire can
     be rendered against the controls AND against the stand-in dots, and all
     of them go when it is disconnected */
  wires = wires.filter((w) => {
    if (!(w.fromAlias === fromAlias && w.fromPort === fromPort
          && w.toAlias === toAlias && w.toPort === toPort)) return true;
    removeWire(w);
    return false;
  });
  log(fromAlias + '.' + fromPort + ' → ' + toAlias + '.' + toPort + ' disconnected', 'event');
}

/* Delete mode's gesture: click anywhere on a card to remove it. Attached  */
/* to every card regardless of mode - effectiveMode is what actually gates  */
/* it, the same pattern as onPortClick - so nothing has to be re-wired      */
/* when the mode changes later, and a View with its own Mode override        */
/* (the Palette) never has to be special-cased here at all.                  */
function attachDeleteGesture(el, alias) {
  el.addEventListener('click', (ev) => {
    if (effectiveMode(el) !== 'Delete') return;
    ev.stopPropagation();
    /* send the verb; the instance-removed event is the ONLY remover -    */
    /* for a moment the GUI may show a thing the engine has deleted, but   */
    /* never the reverse: the GUI is never the source of truth about what   */
    /* exists (and a refused delete - Deletable="0" - removes nothing        */
    /* anywhere, instead of flashing a fake removal here)                     */
    send({ cmd: 'delete-instance', instance: aliasOfEl(el, alias) });
  });
}

/* Options mode: click a thing and its INTERNALS view opens - a real View  */
/* the server builds lazily (once, shared by everyone), holding one real    */
/* Alias per published property, each a live link into the object's data.   */
/* The controls in it are ordinary instances: clone them, alias them, move  */
/* them, rearrange the view - there is no second kind of control panel.     */
function attachOptionsGesture(el, alias) {
  el.addEventListener('click', (ev) => {
    if (effectiveMode(el) !== 'Options') return;
    ev.stopPropagation();
    send({ cmd: 'internals', instance: aliasOfEl(el, alias) });
  });
}

/* the server's answer to an internals ask: which view is this thing's     */
/* panel. The view and its members are the same engine facts either way -   */
/* which PANEL this window then shows is the only thing the asking gesture   */
/* decides: an Options click opens the free-form dissection table; a card's   */
/* first open keeps its node-box and grows rows from the same members.        */
function onInternals(instance, viewAlias) {
  internalsOwner[viewAlias] = instance;
  const mode = internalsAskMode[instance] || 'options';
  delete internalsAskMode[instance];

  /* a closed view's contents were never sent here - fetch the members     */
  /* (idempotent: the dissection panel's own open does the same thing)      */
  if (!loadedContainers[viewAlias]) {
    loadedContainers[viewAlias] = 1;
    send({ cmd: 'list-instances', container: viewAlias });
  }

  if (mode !== 'options') return;

  /* open the dissection panel, retrying briefly if the view's own          */
  /* instance-created is still in flight ahead of us                         */
  const openIt = (tries) => {
    const p = panels[viewAlias];
    if (p) { p.setOpen(true); return; }
    if (tries < 10) setTimeout(() => openIt(tries + 1), 200);
  };
  openIt(0);
}

/* a new, independent instance of the source's class, starting from the    */
/* source's current configuration - one verb carrying the whole intent      */
/* (of/container/x/y); the engine snapshots, names, and places it            */
/* (CloneObject + Bridge_CloneCmd), and every window just renders the        */
/* instance-created broadcast                                                 */
function cloneInstance(sourceAlias, className, container, pos) {
  /* the clone is one node operation inside the engine (Bridge_CloneCmd -> */
  /* CloneObject, deep for views with aliases remapped onto the clones) -  */
  /* this client, and every other one, just renders the instance-created   */
  /* broadcasts that come back                                             */
  send({ cmd: 'clone-instance', of: sourceAlias, container: container || '',
         x: String(Math.round(pos.x)), y: String(Math.round(pos.y)) });
  log('cloned ' + sourceAlias, 'event');
}

/* the ONLY remover: every window - including the one whose Delete click    */
/* asked - takes a rendering down when, and only when, the engine says the   */
/* instance is gone. The same "everyone watching reflects it" rule position   */
/* and mode already follow.                                                   */
function onInstanceRemoved(alias) {
  const inst = instances[alias];
  if (inst) {
    inst.el.remove();
    delete instances[alias];
  }
  /* every thing's panel is a separate element at the root - take it along */
  if (panels[alias]) {
    panels[alias].el.remove();
    delete panels[alias];
  }
  if (views[alias]) {
    delete views[alias];
    delete loadedContainers[alias];
  }
  delete livePositions[alias];
  delete cardBodies[alias];
  delete internalsOwner[alias];

  wires = wires.filter((w) => {
    if (w.fromAlias !== alias && w.toAlias !== alias) return true;
    removeWire(w);
    return false;
  });
}

/* an alias is a full path (Bridge_Rename, bridge.c) - it names where an   */
/* instance currently lives, not a permanent identity, so moving it to a    */
/* different Container really does mean every one of this client's own      */
/* alias-keyed maps has to follow under the new key. Live subscriptions      */
/* need no action here: Bridge_TapOnIn already resolves its alias fresh on   */
/* every delivery rather than caching it, so property-changed/message-       */
/* flowed events simply start arriving tagged with the new alias on their    */
/* own.                                                                       */
/*                                                                            */
/* Known gap: gesture handlers attached at creation (attachDeleteGesture/     */
/* startDrag/onPortClick) close over the alias they were built with rather     */
/* than re-reading it live, so a gesture issued against a card AFTER it has    */
/* been renamed (now reachable by dragging it between views) will fail until    */
/* the page reloads. Making every gesture closure re-resolve its alias live    */
/* is a real follow-up, not done here.                                         */
/* Names change under a running page: a move re-containers an instance, and
   its path IS its name. onInstanceRenamed re-keys every map, but it cannot
   reach inside a closure - and every deferred gesture (a commit, a button,
   a view toggle) closed over the alias it was drawn with, so after a move it
   addressed a path the engine no longer knows and got back "unknown
   instance". Deferred sends resolve through here instead of trusting what
   they captured. Chains collapse: A->B then B->C answers C for A. */
let renamedTo = {};

function cur(alias) {
  let a = alias, guard = 0;
  while (renamedTo[a] && guard++ < 32) a = renamedTo[a];
  return a;
}

function onInstanceRenamed(oldAlias, newAlias) {
  if (!oldAlias || !newAlias || oldAlias === newAlias) return;

  const moveKey = (map) => {
    if (Object.prototype.hasOwnProperty.call(map, oldAlias)) {
      map[newAlias] = map[oldAlias];
      delete map[oldAlias];
    }
  };
  moveKey(instances);
  moveKey(views);
  moveKey(panels);
  moveKey(livePositions);
  moveKey(menuButtons);
  moveKey(cardBodies);
  moveKey(internalsAskMode);
  moveKey(loadedContainers);

  /* the internals bookkeeping names things on both sides: the view (key)  */
  /* and its owner (value)                                                  */
  moveKey(internalsOwner);
  for (const v of Object.keys(internalsOwner)) {
    if (internalsOwner[v] === oldAlias) internalsOwner[v] = newAlias;
  }

  const prefix = oldAlias + '.';
  const rekeyProps = (map) => {
    for (const key of Object.keys(map)) {
      if (key.startsWith(prefix)) {
        map[newAlias + '.' + key.slice(prefix.length)] = map[key];
        delete map[key];
      }
    }
  };
  rekeyProps(propertyValues);
  rekeyProps(selfDisplays);
  rekeyProps(liveControls);
  rekeyProps(portDisplays);

  for (const w of wires) {
    if (w.fromAlias === oldAlias) w.fromAlias = newAlias;
    if (w.toAlias === oldAlias) w.toAlias = newAlias;
  }

  renamedTo[oldAlias] = newAlias;

  /* a control addresses the engine through a stamp on its own element, so
     that a rename can follow it into what would otherwise be a closed-over
     constant. guiAlias is the same story for the GUI_ annotations. */
  for (const k of Object.keys(liveControls)) {
    for (const entry of liveControls[k]) {
      /* a control watching one of its OWN properties holds a handler, not an
         element - it addresses the engine through cur() already, so there is
         no stamp on it to follow a rename */
      if (!entry.el) continue;
      if (entry.el.ctlAlias === oldAlias) entry.el.ctlAlias = newAlias;
      if (entry.el.guiAlias === oldAlias) entry.el.guiAlias = newAlias;
    }
  }

  if (views[newAlias]) views[newAlias].innerEl.dataset.viewAlias = newAlias;

  /* what's painted on the thing follows its name - icon labels, card and  */
  /* panel titles, atom labels, wherever this alias shows itself           */
  const newBase = baseName(newAlias);
  const relabel = (rootEl) => {
    if (!rootEl) return;
    for (const sel of ['.instance-icon-label', '.node-title', '.widget-atom-label']) {
      const el = rootEl.querySelector(sel);
      if (el) { el.textContent = newBase; el.title = newAlias; }
    }
  };
  if (instances[newAlias]) relabel(instances[newAlias].el);
  if (panels[newAlias]) relabel(panels[newAlias].el);
  if (views[newAlias]) {
    const s = views[newAlias].header.querySelector('span');
    if (s) { s.textContent = newBase; s.title = newAlias; }
  }
  log(oldAlias + ' → ' + newAlias, 'event');
}

/* --- the drop primitive: where you release IS where it lives ------------ */

/* what view is under the cursor (skipping ignoreEl, usually the thing      */
/* being dragged) and the cursor's position relative to that view's inner   */
/* area - Root is just the outermost view, reported as container "" with    */
/* canvas coordinates. This one function is what makes Clone, Alias, and    */
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

/* Clone and Alias are pick-then-place, deterministic: the first click     */
/* picks up a ghost (the source never moves), the ghost follows the         */
/* pointer, and the NEXT click places it - wherever you point is exactly    */
/* where it lands. Esc cancels the carry. No press-drag-release timing      */
/* to get right.                                                            */
function startGestureDrag(ev, kind, data, labelText) {
  cancelGestureDrag();
  const ghost = document.createElement('div');
  ghost.className = 'drag-ghost';
  ghost.textContent = labelText;
  document.body.appendChild(ghost);
  gestureDrag = { kind, data, ghost };
  ghost.style.left = (ev.clientX + 8) + 'px';
  ghost.style.top = (ev.clientY + 8) + 'px';
  ev.preventDefault();
  ev.stopPropagation();
}

function cancelGestureDrag() {
  if (!gestureDrag) return;
  gestureDrag.ghost.remove();
  gestureDrag = null;
}

/* the shared pointerdown for every card/atom/view - one dispatch, no        */
/* per-view or per-kind modes: Move drags the thing itself, Clone drags a   */
/* ghost that creates an independent instance where dropped, Alias (on a    */
/* widget atom) drags a ghost that creates an Alias of its primary control. */
/* Pointer events, not mouse events, everywhere: one API covers mouse,      */
/* touch and pen, so a finger drag on a pad IS the drag (with touch-action: */
/* none on the draggable chrome, style.css, so the browser doesn't claim    */
/* the gesture for scrolling).                                               */
function startDrag(ev, el, alias, className, primaryProp) {
  const mode = effectiveMode(el);
  alias = aliasOfEl(el, alias);   /* the CURRENT name, not the birth name */

  /* Export armed: this click names the view to export. Open the dialog
     pre-filled with its name; export-flow does the rest. */
  if (pendingExport && alias) {
    pendingExport = false;
    document.body.classList.remove('mode-export');
    ev.stopPropagation();
    openFlowDialog('Export', { name: baseName(alias), of: alias });
    return;
  }

  if (mode === 'Clone' && alias && className) {
    startGestureDrag(ev, 'clone', { sourceAlias: alias, className }, 'clone: ' + className);
    return;
  }

  if (mode === 'Alias' && alias && primaryProp) {
    startGestureDrag(ev, 'alias', { of: alias, prop: primaryProp }, 'alias: ' + baseName(alias) + '.' + primaryProp);
    return;
  }

  if (mode !== 'Move') return;

  const rect = el.getBoundingClientRect();

  /* One pointerdown can reach here twice (a card's icon handler and the
     card's own), and each pass used to leave a tag behind with only the
     last one remembered - so the earlier ones stayed on screen for good.
     Clear any tag still standing before making this one. */
  if (dragState && dragState.tag) dragState.tag.remove();
  for (const stale of document.querySelectorAll('.drag-ghost.move-tag')) stale.remove();

  /* say what is being moved - the full path, so there is no guessing
     about WHICH thing the drag has hold of */
  const tag = document.createElement('div');
  tag.className = 'drag-ghost move-tag';
  tag.textContent = alias;
  tag.style.left = (ev.clientX + 8) + 'px';
  tag.style.top = (ev.clientY + 8) + 'px';
  document.body.appendChild(tag);

  dragState = {
    el,
    alias,
    tag,
    offsetX: ev.clientX - rect.left,
    offsetY: ev.clientY - rect.top,
  };
  ev.preventDefault();
}

/* position is relative to whatever positioned ancestor the element        */
/* currently sits in (.view-inner is position:relative; the canvas is the   */
/* outermost case) - correct DOM nesting instead of coordinate math         */
document.addEventListener('pointermove', (ev) => {
  if (gestureDrag) {
    gestureDrag.ghost.style.left = (ev.clientX + 8) + 'px';
    gestureDrag.ghost.style.top = (ev.clientY + 8) + 'px';
    return;
  }
  if (panelDrag) {
    const canvas = $('canvas');
    const rect = canvas.getBoundingClientRect();
    panelDrag.el.style.left = Math.max(0, ev.clientX - rect.left - panelDrag.offsetX) + 'px';
    panelDrag.el.style.top = Math.max(0, ev.clientY - rect.top - panelDrag.offsetY) + 'px';
    return;
  }
  if (!dragState) return;
  const parentEl = dragState.el.offsetParent || $('canvas');
  const rect = parentEl.getBoundingClientRect();
  const x = ev.clientX - rect.left + parentEl.scrollLeft - dragState.offsetX;
  const y = ev.clientY - rect.top + parentEl.scrollTop - dragState.offsetY;
  dragState.el.style.left = Math.max(0, x) + 'px';
  dragState.el.style.top = Math.max(0, y) + 'px';
  if (dragState.tag) {
    dragState.tag.style.left = (ev.clientX + 8) + 'px';
    dragState.tag.style.top = (ev.clientY + 8) + 'px';
  }
  if (dragState.alias) updateWiresFor(dragState.alias);
});

/* the placing click - capture phase, so nothing under the cursor reacts   */
/* to it (the click means "put it here", not "press this"). The arming     */
/* click never reaches here: startGestureDrag runs from an element         */
/* handler after capture has already passed, and stops propagation.        */
document.addEventListener('pointerdown', (ev) => {
  if (!gestureDrag) return;
  ev.stopPropagation();
  ev.preventDefault();

  const g = gestureDrag;
  gestureDrag = null;
  g.ghost.remove();

  /* clicking off the canvas (the topbar, say) is a cancel, not a place */
  if (!ev.target.closest || !ev.target.closest('#canvas-wrap')) {
    log('cancelled ' + g.kind, 'event');
    return;
  }

  const drop = dropTargetAt(ev, null);

  {
    if (g.kind === 'clone') {
      cloneInstance(g.data.sourceAlias, g.data.className, drop.container, { x: drop.x, y: drop.y });
    } else if (g.kind === 'import') {
      /* drop the saved view here - the server rebuilds it in this container
         at this spot, internal links resolving under the new copy */
      send({ cmd: 'import-flow', file: g.data.file, into: drop.container,
             x: String(Math.round(drop.x)), y: String(Math.round(drop.y)) });
      log('imported ' + g.data.file, 'event');
    } else if (g.kind === 'alias') {
      /* one verb carrying the whole intent - the server names it and    */
      /* places it in a single atomic birth (see readmefirst.md); this    */
      /* client learns the name from the instance-created that comes back */
      send({ cmd: 'create-alias', of: g.data.of, prop: g.data.prop,
             container: drop.container, x: String(Math.round(drop.x)), y: String(Math.round(drop.y)) });
      log('aliased ' + g.data.of + '.' + g.data.prop, 'event');
    }
  }
}, true);

/* a cancelled pointer (the browser reclaimed the gesture - an edge      */
/* swipe, a palm) must not leave a drag armed forever: drop everything    */
/* in-flight without committing, the same outcome as Esc                  */
document.addEventListener('pointercancel', () => {
  if (dragState && dragState.tag) dragState.tag.remove();
  dragState = null;
  panelDrag = null;
  resizeState = null;
  if (gestureDrag) cancelGestureDrag();
});

/* Esc drops whatever is being carried, nothing happens anywhere - and    */
/* closes an open file dialog the same way                                */
document.addEventListener('keydown', (ev) => {
  if (ev.key !== 'Escape') return;

  /* Esc is the way out of anything: drop what is being carried, close a
     dialog, un-arm a half-drawn wire, and come back to Operate - so no
     mode can ever be a place you are stuck in.

     CAPTURE phase (see the true below): a control that stops keydown -
     a focused text box, an open dropdown - would otherwise swallow this
     before the document ever sees it, and the way out must not be
     something any widget can eat. Focus is dropped for the same reason. */
  if (document.activeElement && document.activeElement.blur)
    document.activeElement.blur();
  if (gestureDrag) {
    cancelGestureDrag();
    log('cancelled', 'event');
  }
  if (flowDialog) closeFlowDialog();
  if (pendingExport) {
    pendingExport = false;
    document.body.classList.remove('mode-export');
  }
  if (pendingPort) {
    pendingPort.el.classList.remove('armed');
    pendingPort = null;
  }
  if (currentMode !== 'Operate') {
    const mm = modeMenuAlias();
    /* apply locally first so the way out never depends on a round trip,
       then tell the engine so every other window follows */
    applyMode('Operate');
    if (mm) send({ cmd: 'set-property', instance: mm, prop: 'Selected', value: 'Operate' });
  }

  /* and shut any menu that was hanging open */
  for (const a in menuButtons) for (const m of menuButtons[a]) m.dropdown.style.display = 'none';
}, true);

document.addEventListener('pointerup', (ev) => {
  if (panelDrag) {
    /* commit the panel's place - the panel's own shared properties, the  */
    /* icon's X/Y untouched                                                */
    send({ cmd: 'set-property', instance: panelDrag.alias, prop: 'ReservedViewPanelX', value: String(parseInt(panelDrag.el.style.left, 10) || 0) });
    send({ cmd: 'set-property', instance: panelDrag.alias, prop: 'ReservedViewPanelY', value: String(parseInt(panelDrag.el.style.top, 10) || 0) });
    panelDrag = null;
    return;
  }
  if (dragState && dragState.alias) {
    /* ONE verb carrying the whole intent: where you release IS where it   */
    /* lives - container and position ride together (move-instance,         */
    /* Bridge_Move -> MoveInstance, object.c). The engine independently      */
    /* validates that a view never enters itself; the geometric check here    */
    /* is input interpretation - a drop onto a view's own interior stays a    */
    /* same-container reposition, exactly as it always has.                   */
    /* Resolved to the CURRENT name - the drag may span a rename ago.          */
    const alias = aliasOfEl(dragState.el, dragState.alias);
    const inst = instances[alias];
    const drop = dropTargetAt(ev, dragState.el);
    const view = views[alias];
    const escapesItself = view && (drop.container === alias);

    const currentInner = dragState.el.parentElement;
    const currentContainer = (currentInner && currentInner.dataset && currentInner.dataset.viewAlias) || ROOT_VIEW;

    if (!escapesItself && drop.container !== currentContainer) {
      send({ cmd: 'move-instance', of: alias, container: drop.container,
             x: String(drop.x - dragState.offsetX), y: String(drop.y - dragState.offsetY) });
      dragState.el.style.left = Math.max(0, drop.x - dragState.offsetX) + 'px';
      dragState.el.style.top = Math.max(0, drop.y - dragState.offsetY) + 'px';
      if (inst) placeInContainer(inst.el, drop.container);
    } else {
      send({ cmd: 'move-instance', of: alias, container: currentContainer,
             x: String(parseInt(dragState.el.style.left, 10) || 0),
             y: String(parseInt(dragState.el.style.top, 10) || 0) });
    }
  }
  if (dragState && dragState.tag) dragState.tag.remove();
  dragState = null;
});

applyMode('Operate');
connectSocket();
