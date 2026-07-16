/*

GrokThink web client: palette + canvas + widgets, talking to a Bridge
object over WebSocket. Vanilla JS, no build step - matches the
framework's own "no external dependencies" discipline.

The whole app is a thin veneer over the Bridge's verbs (create-instance,
connect, bind-property, bind-activate, set-property, activate,
subscribe), same as the Bridge itself is a veneer over the C API. This
file never talks to the framework directly - only ever through
send()/JSON messages.

Recursion, not raw HTML: every LED, textbox, checkbox, slider, knob and
activate button rendered on a node card is backed by a REAL registered
object instance (LED/Textbox/Checkbox/.../Button), wired to the node it
decorates with genuine Connect()/bind-property/bind-activate calls -
never a bare <input> firing set-property straight at the target. A
composite object (a Reader, say) is instantiated as a COLLECTION of
those widget objects, not drawn as chrome.

The recursion has to bottom out somewhere: a widget class's OWN Value/
State/activate is rendered directly against itself (that's the base
case - a Textbox's value control can't itself be another Textbox).
Every other class gets each control wrapped in a fresh widget instance.

Widget numbers below mirror the PropertyType enum in object.h:
  1 TEXTBOX  2 LED  3 BUTTON  4 CHECKBOX  5 SLIDER
  6 VUMETER  7 TEXTOUT  8 KNOB  9 LABEL  10 NULL (a port, not a widget)

*/

/* Router puts HTTP and WebSocket on the same TCP port (the whole point   */
/* being: only one hole needs to be open in a firewall) - so the socket   */
/* connects back to whatever port this page itself was loaded from,       */
/* rather than a second hardcoded one.                                    */
const WS_PORT = location.port || (location.protocol === 'https:' ? 443 : 80);

const PROP_TEXTBOX = 1, PROP_LED = 2, PROP_BUTTON = 3, PROP_CHECKBOX = 4, PROP_SLIDER = 5,
      PROP_VUMETER = 6, PROP_TEXTOUT = 7, PROP_KNOB = 8, PROP_LABEL = 9, PROP_NULL = 10, PROP_MENU = 11,
      PROP_ICON = 12;   /* a doorway: renders as the target's icon, opens its one panel */

/* palette classes that ARE widgets - the base case of the recursion */
const WIDGET_CLASSES = new Set(['Checkbox', 'Textbox', 'Slider', 'Knob', 'Label', 'LED', 'TextOut', 'VUMeter', 'Button', 'MenuButton']);

/* which widget class backs which property widget-type, split by whether  */
/* the widget is something the user edits or something that only displays */
const INPUT_WIDGET_CLASS   = { [PROP_TEXTBOX]: 'Textbox', [PROP_CHECKBOX]: 'Checkbox', [PROP_SLIDER]: 'Slider', [PROP_KNOB]: 'Knob' };
const DISPLAY_WIDGET_CLASS = { [PROP_LED]: 'LED', [PROP_TEXTOUT]: 'TextOut', [PROP_VUMETER]: 'VUMeter', [PROP_LABEL]: 'Label' };

let ws = null;
let classes = {};          // className -> [{Name,Direction,Widget,Default}, ...]
let instances = {};        // alias -> {className, el, ports: {name: dotEl}}
let selfDisplays = {};     // "alias.propName" -> {el,widgetClass}, for a widget class's own State shown on itself
let liveControls = {};     // "alias.propName" -> {el,widgetClass}, an editable control synced from its own property-changed events
let portDisplays = {};     // "alias.portName" -> {el,widgetClass}, a readout painted from the port's message-flowed traffic (an out-port LED)
let wires = [];            // {fromAlias, fromPort, toAlias, toPort, lineEl}
let cardBodies = {};       // cardAlias -> {addMemberRow}, a card panel waiting to grow rows from its internals view's members
let internalsOwner = {};   // internals view alias -> the instance it dissects (learned from the internals event)
let internalsAskMode = {}; // instance alias -> 'card'|'options', which gesture asked for internals (purely which PANEL this window then shows)
let pendingPort = null;    // {alias, port, dotEl} - first end of a wire being drawn
let dragState = null;      // {alias, offsetX, offsetY}
let gestureDrag = null;    // {kind:'clone'|'alias', data, ghost} - a Clone/Alias carry in progress (the ghost, not the source)
let panelDrag = null;      // {alias, el, offsetX, offsetY} - a view PANEL being moved by its titlebar (PanelX/PanelY, not X/Y)
let livePositions = {};    // alias -> {el}, a card whose X/Y are real properties kept in sync like anything else
let menuButtons = {};      // alias -> {label,items,selected,btn,dropdown}, any MenuButton (topbar chrome or dropped-in)
let propertyValues = {};   // "alias.propName" -> last known value, from property-changed - what Clone reads to copy a source's configuration
let aliasAtoms = {};       // alias -> {el, slot, labelEl, target, targetProp, widget, label, control}, a real Alias instance's rendering
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
    for (const w of wires) w.line.remove();
    wires = [];
    if (pendingPort) {
      pendingPort.dotEl.classList.remove('armed');
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
  if (!containerAlias) {
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
function registerPanel(alias, panelEl, display, onToggle) {
  panelEl.style.position = 'absolute';
  panelEl.style.left = '240px';
  panelEl.style.top = '60px';
  panelEl.style.display = 'none';
  panelEl.style.zIndex = '40';
  $('canvas').appendChild(panelEl);

  const rec = {
    el: panelEl,
    openApplied: false,
    setOpen(open) {
      /* an explicit open/close IS a presentation decision - a late-      */
      /* arriving initial Open value must never override it               */
      rec.openApplied = true;
      panelEl.style.display = open ? display : 'none';
      if (onToggle) onToggle(open);
      updateWiresFor(alias);
    },
  };
  panels[alias] = rec;

  send({ cmd: 'subscribe', instance: alias, port: 'Open' });
  send({ cmd: 'subscribe', instance: alias, port: 'PanelX' });
  send({ cmd: 'subscribe', instance: alias, port: 'PanelY' });
  return rec;
}

/* dragging any panel by its titlebar moves the PANEL (PanelX/PanelY,     */
/* shared) - never the icon; two things, two independent lives            */
function startPanelDrag(ev, alias, panelEl) {
  const rect = panelEl.getBoundingClientRect();
  panelDrag = { alias, el: panelEl, offsetX: ev.clientX - rect.left, offsetY: ev.clientY - rect.top };
  ev.preventDefault();
}

/* one session mode, everywhere - a View used to be able to override the   */
/* mode for its contents (the old "the Palette is a permanent Clone         */
/* station" mechanism), but there are no special views anymore: the         */
/* palette is just a View, Root is just a View, and every gesture works     */
/* the same inside all of them. Kept as a function because every gesture    */
/* already calls it.                                                         */
function effectiveMode(el) {
  return currentMode;
}

function setStatus(text, cls) {
  const el = $('status');
  el.textContent = text;
  el.className = 'status-' + cls;
}

/* the hardcoded Activity panel this used to render into has been removed - */
/* a dev-console breadcrumb for now, since plenty of call sites still want   */
/* a low-level trace of client actions (clone, rename, errors) that have no  */
/* source port to wire from.                                                 */
function log(text, cls) {
  console.log((cls ? '[' + cls + '] ' : '') + text);
}

function send(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(cmd));
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
      break;
    case 'instance-created':
      onInstanceCreated(msg.instance, msg.class, msg.parent, msg.interface, msg.hidden, msg.container);
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
      log('loaded ' + msg.file, 'event');
      closeFlowDialog();
      /* a load renames, re-paths and deletes instances mid-replay; a
         client that renders incrementally through that burst races its
         own subscribes - a subscribe to /Root/View_1's X goes out, the
         load renames View_1 to slider before it arrives, and the
         subscribe resolves to nothing (the "unknown instance" storm in
         the server log). Rather than chase every racy event, re-project
         from scratch: the engine holds the final state, so reload and
         re-list it clean - every name resolved, no subscribe outrunning
         a rename. A load is a bulk state change; a projector re-projects. */
      location.reload();
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
    Direction: nodeProp(p, 'Direction'),
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

/* one shared builder for the raw input element a Value control renders  */
/* as - used both for a widget instance's own Value (base case) and for  */
/* the sub-widget instances wrapping some other object's property        */
function buildValueControl(widgetClass, defaultValue, onCommit) {
  let el;
  switch (widgetClass) {
    case 'Checkbox':
      el = document.createElement('input');
      el.type = 'checkbox';
      el.checked = defaultValue === '1';
      el.onchange = () => onCommit(el.checked ? '1' : '0');
      break;
    case 'Slider':
      el = document.createElement('input');
      el.type = 'range';
      el.value = defaultValue || '0';
      el.oninput = () => onCommit(el.value);
      break;
    case 'Knob':
      el = document.createElement('input');
      el.type = 'number';
      el.value = defaultValue || '0';
      el.onchange = () => onCommit(el.value);
      break;
    default: /* Textbox */
      el = document.createElement('input');
      el.type = 'text';
      el.value = defaultValue || '';
      el.onchange = () => onCommit(el.value);
  }
  return el;
}

function makeReadoutEl(widgetClass) {
  const el = document.createElement('span');
  el.className = widgetClass === 'LED' ? 'node-led state-0' : 'widget-readout';
  return el;
}

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
        if (alias === 'FileMenu' && (item === 'Save' || item === 'Load' || item === 'Import')) {
          openFlowDialog(item);
          return;
        }
        send({ cmd: 'set-property', instance: alias, prop: 'Selected', value: item });
      };
      dropdown.appendChild(row);
    }
  }

  btn.onclick = (ev) => {
    ev.stopPropagation();
    dropdown.style.display = dropdown.style.display === 'none' ? 'block' : 'none';
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

function openFlowDialog(kind) {
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
  input.value = 'session';
  body.appendChild(input);
  box.appendChild(body);

  const doIt = () => {
    const name = input.value.trim();
    if (!name) return;
    const verb = kind === 'Save' ? 'save-flow' : kind === 'Import' ? 'import-flow' : 'load-flow';
    send({ cmd: verb, file: name });
    /* stays open until flow-saved/flow-loaded confirms (handleEvent) -   */
    /* the engine is the one that knows whether it happened               */
  };

  const footer = document.createElement('div');
  footer.className = 'node-footer';
  const go = document.createElement('button');
  go.className = 'activate-btn';
  go.textContent = kind;
  go.onclick = doIt;
  const cancel = document.createElement('button');
  cancel.className = 'activate-btn';
  cancel.textContent = 'Cancel';
  cancel.onclick = closeFlowDialog;
  footer.appendChild(go);
  footer.appendChild(cancel);
  box.appendChild(footer);

  input.onkeydown = (ev) => { if (ev.key === 'Enter') doIt(); };

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
  for (const alias in menuButtons) for (const m of menuButtons[alias]) m.dropdown.style.display = 'none';
});

/* a control that subscribes to its own backing property so it reflects  */
/* the authoritative server-side value, not just its own optimistic      */
/* write - anything else driving this same property (another client, a  */
/* Connect()ed source) shows up here too, the same way selfDisplays does */
function bindLiveControl(subscribeAlias, subscribeProp, widgetClass, defaultValue, onCommit) {
  const el = buildValueControl(widgetClass, defaultValue, onCommit);
  send({ cmd: 'subscribe', instance: subscribeAlias, port: subscribeProp });
  /* more than one rendering can subscribe to the same alias.prop (an       */
  /* Alias atom binds to its target's prop alongside the target's own       */
  /* rendering) - every one has to keep getting updated, not just            */
  /* whichever registered last, so this is a list, not a single slot        */
  const key = subscribeAlias + '.' + subscribeProp;
  (liveControls[key] = liveControls[key] || []).push({ el, widgetClass });
  return el;
}

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

function updateLiveControl(entry, value) {
  if (entry.widgetClass === 'Checkbox') entry.el.checked = value === '1';
  else entry.el.value = value;
}

/* --- base case: a widget class's own Value/State/activate, rendered against itself --- */

function makeSelfControl(alias, propName, widget, defaultValue) {
  const widgetClass = INPUT_WIDGET_CLASS[widget] || 'Textbox';
  return bindLiveControl(alias, propName, widgetClass, defaultValue,
    (v) => send({ cmd: 'set-property', instance: alias, prop: propName, value: v }));
}

function makeSelfDisplay(alias, propName, widget) {
  const widgetClass = DISPLAY_WIDGET_CLASS[widget] || 'TextOut';
  const el = makeReadoutEl(widgetClass);
  send({ cmd: 'subscribe', instance: alias, port: propName });
  /* same list-not-single-slot reasoning as bindLiveControl above - Copy    */
  /* means more than one of these can exist for the same alias.prop        */
  const key = alias + '.' + propName;
  (selfDisplays[key] = selfDisplays[key] || []).push({ el, widgetClass });
  return el;
}

function makeSelfActivateButton(alias) {
  const btn = document.createElement('button');
  btn.className = 'activate-btn';
  btn.textContent = 'Activate';
  btn.onclick = () => send({ cmd: 'activate', instance: alias });
  return btn;
}

/* The hidden helper widgets (makeInputWidget/makeDisplayWidget/            */
/* makeButtonWidget) that used to wrap a composite object's properties in    */
/* invisible per-page widget instances are GONE (readmefirst repair #3):     */
/* a card's rows are the engine's own internals-view members now (see        */
/* registerCard/addMemberRow), its icon LED is a plain subscribed readout,   */
/* and its Activate button sends the activate verb straight at the object.   */
/* Nothing on this side of the bridge creates, names, or owns anything.      */

/* a standalone widget instance: its own natural control (or dot) plus a  */
/* label, positioned and draggable exactly like any other card - no        */
/* header, no property rows, no footer, and its wiring dots (below) only    */
/* ever show up in Connect mode, same as everything else's. (The old Copy   */
/* mode's client-local second renderings are gone - a second doorway to     */
/* the same object is now a real Alias instance, shared and savable; see    */
/* registerAliasAtom.)                                                      */
function registerWidgetAtom(alias, className, props, pos, isCopy, container) {
  const el = document.createElement('div');
  el.className = 'widget-atom';
  el.style.left = pos.x + 'px';
  el.style.top = pos.y + 'px';

  let control, primaryProp;
  if (className === 'MenuButton') {
    control = makeMenuButtonEl(alias);
    primaryProp = 'Selected';
  } else if (className === 'Button') {
    control = makeSelfActivateButton(alias);
    primaryProp = 'Out';
  } else {
    const valueProp = props.find((p) => p.Name === 'Value');
    const widget = valueProp ? valueProp.Widget : PROP_TEXTOUT;
    control = INPUT_WIDGET_CLASS[widget]
      ? makeSelfControl(alias, 'Value', widget, valueProp && valueProp.Default)
      : makeSelfDisplay(alias, 'Value', widget);
    primaryProp = 'Value';
  }
  control.classList.add('widget-atom-control');
  el.appendChild(control);

  /* a MenuButton's own button text already says what it is (Label,        */
  /* plus Selected once something's been picked) - a second "alias" label  */
  /* under it would just be noise                                          */
  if (className !== 'MenuButton') {
    const label = document.createElement('span');
    label.className = 'widget-atom-label';
    label.textContent = baseName(alias);
    label.title = alias;
    el.appendChild(label);
  }

  /* the whole atom IS the property, the same way clicking a card's row is  */
  /* that row's property - no separate dot, no in/out distinction, click    */
  /* it in Connect mode and it arms/completes a wire on primaryProp         */
  el.addEventListener('click', () => onPortClick(alias, primaryProp, el));

  /* dragging the atom: Move moves it, Clone ghosts a new independent      */
  /* instance, Alias ghosts an Alias of its primary control - one           */
  /* mousedown, mode decides (startDrag). The control itself keeps its own  */
  /* gesture (a click, a slider drag) - only the chrome around it drags.    */
  el.onmousedown = (ev) => { if (ev.target !== control) startDrag(ev, el, alias, className, primaryProp); };

  attachDeleteGesture(el, alias);
  attachOptionsGesture(el, alias);

  /* instance-created already carries Container inline (Bridge_InstanceEvent, */
  /* bridge.c) so this places straight into the real parent on first paint -  */
  /* no default-to-canvas-then-correct, which is what let a dropped/delayed   */
  /* Container reply strand an element in the root looking like it belonged   */
  /* there. The Container subscribe below still exists for any later move.   */
  placeInContainer(el, container || '');
  instances[alias] = { className, el, ports: { [primaryProp]: el } };

  livePositions[alias] = { el };
  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });

  log('created ' + alias + ' (' + className + ')', 'event');
}

/* the container primitive - a real, resizable panel with an inner area    */
/* holding whatever has Container==this View's own alias (placeInContainer/ */
/* flushPendingContainer). The Palette is nothing more than one of these,    */
/* built by the server with Mode="Clone" and Deletable="0" already set        */
/* (BuildPalette, object.c) - nothing here knows or cares that any            */
/* particular View happens to be "the palette".                              */
function registerView(alias, props, pos, hidden, container) {
  /* the icon IS the view's permanent presence - it never goes away.        */
  /* Opening shows the panel as a separate element with its own position    */
  /* (PanelX/PanelY, real shared properties independent of the icon's        */
  /* X/Y) - two placements of one thing, not two things. A HIDDEN view is    */
  /* a panel with no icon at all: an object's internals view, whose          */
  /* presence on the canvas is the object's own icon.                        */
  const wrap = document.createElement('div');
  wrap.className = 'instance-wrap';
  wrap.style.left = pos.x + 'px';
  wrap.style.top = pos.y + 'px';
  if (hidden) wrap.style.display = 'none';

  const icon = document.createElement('div');
  icon.className = 'instance-icon';
  const iconLabel = document.createElement('span');
  iconLabel.className = 'instance-icon-label';
  iconLabel.textContent = baseName(alias);
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
  resizeHandle.onmousedown = (ev) => startResize(ev, aliasOfEl(wrap, alias));
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
  icon.onmousedown = (ev) => { if (ev.target === icon || ev.target === iconLabel) startDrag(ev, wrap, alias, 'View', 'Open'); };
  header.onmousedown = (ev) => { if (ev.target !== collapseBtn) startPanelDrag(ev, aliasOfEl(wrap, alias), panel); };
  attachDeleteGesture(wrap, alias);
  attachOptionsGesture(wrap, alias);

  const view = { el: wrap, panel, innerEl, header, resizeHandle, icon, setOpen: p.setOpen };
  views[alias] = view;
  /* instance-created carries Container inline - see registerWidgetAtom's   */
  /* matching comment                                                       */
  placeInContainer(wrap, container || '');
  flushPendingContainer(alias);

  instances[alias] = { className: 'View', el: wrap, ports: {} };
  livePositions[alias] = { el: wrap };

  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'W' });
  send({ cmd: 'subscribe', instance: alias, port: 'H' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });
  send({ cmd: 'subscribe', instance: alias, port: 'Resizeable' });

  log('created ' + alias + ' (View)', 'event');
}

let resizeState = null; // {alias, el, startW, startH, startX, startY}

function startResize(ev, alias) {
  const view = views[alias];
  if (!view || view.resizeHandle.style.display === 'none') return;
  ev.stopPropagation();
  ev.preventDefault();
  const rect = view.panel.getBoundingClientRect();
  resizeState = { alias, el: view.panel, startW: rect.width, startH: rect.height, startX: ev.clientX, startY: ev.clientY };
}

document.addEventListener('mousemove', (ev) => {
  if (!resizeState) return;
  const w = Math.max(80, resizeState.startW + (ev.clientX - resizeState.startX));
  const h = Math.max(60, resizeState.startH + (ev.clientY - resizeState.startY));
  resizeState.el.style.width = w + 'px';
  resizeState.el.style.height = h + 'px';
});

document.addEventListener('mouseup', () => {
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

function onInstanceCreated(alias, className, parent, interfaceNode, hidden, container) {
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
    registerView(alias, props, pos, hidden, container);
    return;
  }

  /* a real Alias instance: a doorway to one property of another object -   */
  /* rendered from its own Target/TargetProp/Widget/Label properties as     */
  /* they arrive, bound to the original's value through the node-level link */
  if (className === 'Alias') {
    registerAliasAtom(alias, pos, container);
    return;
  }

  /* a widget primitive has no panel - it's the same base-case rendering   */
  /* the recursion already bottoms out to (makeSelfControl/makeSelfDisplay/ */
  /* makeSelfActivateButton), just standing on its own instead of sitting   */
  /* inside a composite object's card: a control (or, for LED, a colored    */
  /* dot) and a label, nothing else. It's meant to be light enough to drag   */
  /* around freely and, eventually, clone or copy (see registerWidgetAtom's  */
  /* doc comment) - a node-box's header/body/footer chrome is the opposite   */
  /* of that.                                                                */
  if (WIDGET_CLASSES.has(className)) {
    registerWidgetAtom(alias, className, props, pos, false, container);
    return;
  }

  registerCard(alias, className, props, pos, false, container);
}

/* a composite object is an icon that opens into a panel - the icon's own   */
/* in/out anchors are the exact same aliases as the panel's, just a second   */
/* place to click them (see the iconDots/panelRows swap in setExpanded      */
/* below): collapsed or expanded is a display choice, not a different        */
/* object or a different wire.                                               */
/*                                                                            */
/* The panel's rows are the ENGINE's answer to "what is this object's        */
/* panel": its internals view's member aliases (Bridge_Internals). The       */
/* shell (header/body/footer) renders immediately; the first open asks       */
/* `internals`, the members stream in (the same lazy-contents path the       */
/* dissection panel uses), and each member becomes one row (addMemberRow     */
/* below), its control chosen by the member's engine-stamped Widget/          */
/* Direction and its edits written through the member's own Value link.       */
/* This window shows the node-box styling; Options mode shows the same        */
/* members as the free-form dissection table - one panel, two projections.    */
function registerCard(alias, className, props, pos, isCopy, container) {
  const wrap = document.createElement('div');
  wrap.className = 'instance-wrap';
  wrap.style.left = pos.x + 'px';
  wrap.style.top = pos.y + 'px';

  const icon = document.createElement('div');
  icon.className = 'instance-icon';
  const iconLabel = document.createElement('span');
  iconLabel.className = 'instance-icon-label';
  iconLabel.textContent = className;
  icon.appendChild(iconLabel);
  /* the status LED is a plain painted readout of the object's own State - */
  /* subscribe and render, no helper instance standing in for it            */
  icon.appendChild(wireSlot(alias, 'State', makeSelfDisplay(alias, 'State', PROP_LED)));
  wrap.appendChild(icon);

  /* the panel is the icon's control panel - a separate thing with its    */
  /* own independent life, handled by the exact same registerPanel every   */
  /* view uses: it opens at the ROOT (panels are all peers, never nested),  */
  /* sits at its own shared PanelX/PanelY, and neither opening nor moving   */
  /* it ever touches the icon                                               */
  const panel = document.createElement('div');
  panel.className = 'node-box';

  const header = document.createElement('div');
  header.className = 'node-header';
  const title = document.createElement('span');
  title.className = 'node-title';
  title.textContent = baseName(alias);
  title.title = alias;
  const cls = document.createElement('span');
  cls.className = 'node-class';
  cls.textContent = className;
  const collapseBtn = document.createElement('span');
  collapseBtn.className = 'node-collapse';
  collapseBtn.textContent = '−';
  collapseBtn.title = 'Collapse';
  header.appendChild(title);
  header.appendChild(cls);
  header.appendChild(collapseBtn);
  panel.appendChild(header);

  const body = document.createElement('div');
  body.className = 'node-body';

  const ports = {};
  const panelRows = {};
  const iconDots = {};
  const outPorts = [];
  const builtRows = {};
  let panelOpen = false;
  let rowsAsked = false;

  /* the icon's port dots render straight from the class's published        */
  /* Interface (it rides on instance-created for exactly this): a bare       */
  /* port - one carrying no widget metadata - gets a clickable dot. The       */
  /* panel's ROWS are not built here at all: they are the internals view's    */
  /* members, streamed in on first open (addMemberRow below).                 */
  for (const p of props) {
    if (p.Direction !== 'in' && p.Direction !== 'out') continue;
    if (p.Direction === 'out') outPorts.push(p.Name);
    if (DISPLAY_WIDGET_CLASS[p.Widget] || INPUT_WIDGET_CLASS[p.Widget]) continue;

    const dot = document.createElement('span');
    dot.className = 'port-dot icon-dot ' + p.Direction;
    dot.title = p.Name;
    dot.addEventListener('click', ((name) => (ev) => { ev.stopPropagation(); onPortClick(alias, name, ports[name]); })(p.Name));
    icon.appendChild(dot);
    iconDots[p.Name] = dot;
    ports[p.Name] = dot;
  }

  /* one row per internals-view member: which control, and whether it's a   */
  /* prop row or a port row, comes from the member's ENGINE-stamped Widget/  */
  /* Direction. Edits write through the member's own Value - the node-level  */
  /* link lands them on the object - and reads subscribe to the object       */
  /* itself (events speak the original's name; one tap serves everything).   */
  function addMemberRow(memberAlias, rec) {
    const propName = rec.targetProp;
    if (builtRows[propName]) return;
    builtRows[propName] = true;

    /* position/size are the card's own drag; State is the icon's LED */
    if (propName === 'State') return;

    const inputClass = INPUT_WIDGET_CLASS[rec.widget];
    const displayClass = DISPLAY_WIDGET_CLASS[rec.widget];
    /* the published Default seeds a control before its first live value    */
    /* arrives - same engine-published Interface fact the old rows used     */
    const pub = props.find((q) => q.Name === propName) || {};

    if (rec.direction === 'data') {
      if (!inputClass && !displayClass) return; /* plumbing (PROP_NULL, PROP_ICON): no row */

      let controlEl;
      if (inputClass) {
        controlEl = bindLiveControl(alias, propName, inputClass,
          propertyValues[alias + '.' + propName] || pub.Default,
          (v) => send({ cmd: 'set-property', instance: memberAlias, prop: 'Value', value: v }));
      } else {
        controlEl = makeSelfDisplay(alias, propName, rec.widget);
      }

      const row = document.createElement('div');
      row.className = 'prop-row';
      const label = document.createElement('label');
      label.textContent = propName;
      row.appendChild(label);
      row.appendChild(wireSlot(alias, propName, controlEl));
      /* members replay newest-first (the session table prepends), so     */
      /* prepending each row restores the class's published order - the    */
      /* same top-to-bottom the Interface declares                          */
      body.insertBefore(row, body.firstChild);

      /* "copying out a control": in Alias mode, drag this row into any     */
      /* view and drop it - a real Alias instance of THIS object's property  */
      /* lands there (create-alias, bridge.c), reading and writing the one   */
      /* true value through the link. That's how instrument panels are       */
      /* built.                                                               */
      row.onmousedown = (ev) => {
        if (effectiveMode(row) !== 'Alias') return;
        startGestureDrag(ev, 'alias', { of: alias, prop: propName }, 'alias: ' + alias + '.' + propName);
      };
    } else if (rec.direction === 'in' || rec.direction === 'out') {
      const hasControl = displayClass || inputClass;

      const row = document.createElement('div');
      row.className = 'port-row ' + rec.direction + (hasControl ? ' has-control' : '');

      const label = document.createElement('span');
      label.textContent = propName;

      if (rec.direction === 'out') {
        row.appendChild(label);
        /* a port can carry the same display-widget metadata a property     */
        /* does (Pulse's Out is Widget=PROP_LED) - painted straight from     */
        /* the port's own message-flowed traffic                             */
        if (displayClass) {
          const el = makeReadoutEl(displayClass);
          const key = alias + '.' + propName;
          (portDisplays[key] = portDisplays[key] || []).push({ el, widgetClass: displayClass });
          row.appendChild(wireSlot(alias, propName, el));
        }
      } else {
        /* likewise an in-port can carry input-widget metadata (Enable is   */
        /* Widget=PROP_CHECKBOX) - a directly-clickable control writing      */
        /* through the member's link, so the port's own handler runs          */
        if (inputClass) {
          const el = bindLiveControl(alias, propName, inputClass, pub.Default,
            (v) => send({ cmd: 'set-property', instance: memberAlias, prop: 'Value', value: v }));
          row.appendChild(wireSlot(alias, propName, el));
        }
        row.appendChild(label);
      }

      /* same published-order restoration as the prop rows above */
      body.insertBefore(row, body.firstChild);

      /* a bare port's row is itself the wire click-target while the panel   */
      /* is open (the icon's dot when it isn't - see the swap below)          */
      if (!hasControl) {
        row.addEventListener('click', () => onPortClick(alias, propName, ports[propName]));
        panelRows[propName] = row;
        if (panelOpen) ports[propName] = row;
      }
    }
  }
  cardBodies[alias] = { addMemberRow };

  panel.appendChild(body);

  const footer = document.createElement('div');
  footer.className = 'node-footer';
  /* Activate is one verb aimed at the object itself */
  const activateBtn = document.createElement('button');
  activateBtn.className = 'activate-btn';
  activateBtn.textContent = 'Activate';
  activateBtn.onclick = () => { if (effectiveMode(activateBtn) === 'Operate') send({ cmd: 'activate', instance: aliasOfEl(wrap, alias) }); };
  footer.appendChild(wireSlot(alias, 'Activate', activateBtn));
  panel.appendChild(footer);

  /* wires re-anchor to whichever representation shows the port best:      */
  /* the panel's rows while it's open here, the icon's dots when not.      */
  /* First open asks the engine for the internals view - the one answer     */
  /* to "what is this object's panel" - and the member rows stream in.      */
  const p = registerPanel(alias, panel, 'block', (open) => {
    panelOpen = open;
    for (const name in ports) ports[name] = (open && panelRows[name]) || iconDots[name] || ports[name];
    if (open && !rowsAsked) {
      rowsAsked = true;
      const cur = aliasOfEl(wrap, alias);
      internalsAskMode[cur] = 'card';
      send({ cmd: 'internals', instance: cur });
    }
  });

  icon.addEventListener('click', () => { if (effectiveMode(icon) === 'Operate') p.setOpen(true); });
  collapseBtn.addEventListener('click', (ev) => { ev.stopPropagation(); p.setOpen(false); });

  /* aliasing a thing is aliasing its icon - the alias opens this same    */
  /* panel (Open rides the link), exactly like aliasing a view            */
  icon.onmousedown = (ev) => { if (ev.target === icon || ev.target === iconLabel) startDrag(ev, wrap, alias, className, 'Open'); };
  header.onmousedown = (ev) => { if (ev.target !== collapseBtn) startPanelDrag(ev, aliasOfEl(wrap, alias), panel); };
  attachDeleteGesture(wrap, alias);
  attachOptionsGesture(wrap, alias);

  /* an out port still needs its own subscribe wherever this rendering      */
  /* lives - message-flowed log lines aren't state, there's nothing to       */
  /* double up on the way a data property's live value would be              */
  for (const portName of outPorts) send({ cmd: 'subscribe', instance: alias, port: portName });

  /* instance-created carries Container inline - see registerWidgetAtom's   */
  /* matching comment                                                       */
  placeInContainer(wrap, container || '');
  instances[alias] = { className, el: wrap, ports };

  /* moving this card is exactly the same as editing any other property -  */
  /* a set-property on drag-end (see the mouseup handler) and a subscribe   */
  /* here so every window, including this one on a future reconnect, shows */
  /* wherever it actually is, not wherever it happened to first render     */
  if (props.some((p) => p.Name === 'X') && props.some((p) => p.Name === 'Y')) {
    livePositions[alias] = { el: wrap };
    send({ cmd: 'subscribe', instance: alias, port: 'X' });
    send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  }
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });

  log('created ' + alias + ' (' + className + ')', 'event');
}

function updateReadout(el, widgetClass, value) {
  if (widgetClass === 'LED') el.className = 'node-led state-' + value;
  else el.textContent = value;
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
    /* Open's stored value is the initial presentation only - after       */
    /* first paint, open/closed is this window's own business              */
    if (port === 'Open' && !pnl.openApplied) {
      pnl.openApplied = true;
      pnl.setOpen(value === '1');
    }
    /* same "our own in-flight gesture wins" reasoning as X/Y above */
    else if (port === 'PanelX' && (!panelDrag || panelDrag.alias !== alias)) {
      pnl.el.style.left = (parseInt(value, 10) || 0) + 'px';
      updateWiresFor(alias);
    } else if (port === 'PanelY' && (!panelDrag || panelDrag.alias !== alias)) {
      pnl.el.style.top = (parseInt(value, 10) || 0) + 'px';
      updateWiresFor(alias);
    }
  }

  const view = views[alias];
  if (view) {
    if (port === 'Resizeable') view.resizeHandle.style.display = value === '0' ? 'none' : 'block';
    else if (port === 'W' && (!resizeState || resizeState.alias !== alias)) {
      view.panel.style.width = (parseInt(value, 10) || 190) + 'px';
      updateWiresFor(alias);
    } else if (port === 'H' && (!resizeState || resizeState.alias !== alias)) {
      view.panel.style.height = (parseInt(value, 10) || 220) + 'px';
      updateWiresFor(alias);
    }
  }

  /* an Alias atom assembles itself from its own properties as they arrive  */
  /* (Target/TargetProp say what it stands for, Widget/Direction are the    */
  /* engine's stamped presentation, Label its own) - see registerAliasAtom.  */
  /* An internals-view member additionally becomes a row on its owner's      */
  /* card panel (maybeBuildCardRow) - same member, second projection.         */
  const aliasAtom = aliasAtoms[alias];
  if (aliasAtom) {
    if (port === 'Target' || port === 'TargetProp' || port === 'Widget' || port === 'Direction' || port === 'Label') {
      aliasAtom[port.charAt(0).toLowerCase() + port.slice(1)] = value;
      renderAliasControl(alias);
      maybeBuildCardRow(alias);
    } else if (port === 'Container') {
      aliasAtom.container = value;
    }
  }

  /* every rendering subscribed to this alias.prop gets the update - this   */
  /* is the "load the new values in without propagating anything" contract: */
  /* updateReadout/updateLiveControl only ever assign the DOM value/checked  */
  /* directly, they never simulate a user edit, so hydrating N renderings   */
  /* here can never turn into N set-property echoes back out.               */
  const key = alias + '.' + port;
  for (const entry of selfDisplays[key] || []) updateReadout(entry.el, entry.widgetClass, value);
  for (const entry of liveControls[key] || []) updateLiveControl(entry, value);

  for (const menu of menuButtons[alias] || []) {
    if (port === 'Label') { menu.state.label = value; menu.renderLabel(); }
    else if (port === 'Items') { menu.state.items = value.split(','); menu.renderItems(); }
    else if (port === 'Selected') {
      menu.state.selected = value;
      menu.renderLabel();
      /* the Mode menu's Selected IS the session's current mode - a real,   */
      /* synced property, not client-local state, so this is the one place  */
      /* a mode change (from any window) actually takes effect              */
      if (alias === 'ModeMenu' && value) applyMode(value);
    }
  }

  log(alias + '.' + port + ' = ' + value, 'event');
}

function onMessageFlowed(alias, port, value) {
  /* an out-port readout (a Pulse's Out LED) paints straight from the       */
  /* port's own traffic - display only, nothing standing in for the port    */
  const key = alias + '.' + port;
  for (const entry of portDisplays[key] || []) updateReadout(entry.el, entry.widgetClass, value);

  log(alias + '.' + port + ' → ' + value, 'event');
}

/* an internals-view member alias doubles as a row on its owner's card      */
/* panel - built once its engine-stamped facts (TargetProp/Widget/           */
/* Direction) have all arrived and its container is a view some card here    */
/* knows as its internals view                                                */
function maybeBuildCardRow(memberAlias) {
  const rec = aliasAtoms[memberAlias];
  if (!rec || !rec.targetProp || rec.widget === '' || rec.direction === '') return;
  const owner = internalsOwner[rec.container];
  const card = owner && cardBodies[owner];
  if (card) card.addMemberRow(memberAlias, rec);
}

/* wiring: click any property (a row, or a whole widget atom - see
   registerWidgetAtom), then click a second one, in either order - there is
   no source/sink distinction to get right first. Connect-mode-only; el is
   whatever was clicked, reused both as the pending-connection highlight
   and as the wire's anchor point (drawWire/updateWire just read its
   bounding box, they don't care what kind of element it is). */
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

  send({ cmd: 'connect', from: from.alias, fromPort: from.port, to: alias, toPort: port });
  drawWire(from.alias, from.port, from.el, alias, port, el);
}

function drawWire(fromAlias, fromPort, fromEl, toAlias, toPort, toEl) {
  const svgns = 'http://www.w3.org/2000/svg';
  const line = document.createElementNS(svgns, 'line');
  $('wires').appendChild(line);
  const wire = { fromAlias, fromPort, fromEl, toAlias, toPort, toEl, line };
  wires.push(wire);
  updateWire(wire);
  log(fromAlias + '.' + fromPort + ' → ' + toAlias + '.' + toPort + ' connected', 'event');
}

function updateWire(wire) {
  const wrap = $('canvas-wrap');
  const wrapRect = wrap.getBoundingClientRect();
  const a = wire.fromEl.getBoundingClientRect();
  const b = wire.toEl.getBoundingClientRect();
  wire.line.setAttribute('x1', a.left + a.width / 2 - wrapRect.left + wrap.scrollLeft);
  wire.line.setAttribute('y1', a.top + a.height / 2 - wrapRect.top + wrap.scrollTop);
  wire.line.setAttribute('x2', b.left + b.width / 2 - wrapRect.left + wrap.scrollLeft);
  wire.line.setAttribute('y2', b.top + b.height / 2 - wrapRect.top + wrap.scrollTop);
}

function updateWiresFor(alias) {
  for (const w of wires) {
    if (w.fromAlias === alias || w.toAlias === alias) updateWire(w);
  }
}

/* a connection the server already knew about (list-connections, sent on   */
/* entering Connect mode - see applyMode) - draw it exactly like one made   */
/* by clicking two dots just now. Silently skipped if either end isn't a    */
/* card this client currently has on screen (deleted, or plumbing this      */
/* client never rendered) - see Bridge_Delete's own doc comment (bridge.c)  */
/* on why a stale reference can outlive the instance it pointed at.         */
function onConnected(fromAlias, fromPort, toAlias, toPort) {
  const fromInst = instances[fromAlias];
  const toInst = instances[toAlias];
  if (!fromInst || !toInst) return;

  const fromDot = fromInst.ports[fromPort];
  const toDot = toInst.ports[toPort];
  if (!fromDot || !toDot) return;

  drawWire(fromAlias, fromPort, fromDot, toAlias, toPort, toDot);
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

/* --- an Alias instance's rendering: its own presentation, the original's --- */
/* --- value (see registerAliasAtom / the alias branch of                  --- */
/* --- onPropertyChanged; the mechanism is the node-level link, object.c)  --- */

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
    instances[alias] = instances[alias] || { className: 'Alias', el: rec.el, ports: {} };
    instances[alias].ports['Value'] = rec.el;
    return;
  }

  /* presentation is the alias's own Widget property: the stamped numeric  */
  /* widget type maps to a control class; a widget CLASS NAME typed in by   */
  /* hand on the dissection table is honored as-is; anything else (an       */
  /* unpublished property, PROP_NULL plumbing) is a plain textbox            */
  const widgetClass = INPUT_WIDGET_CLASS[rec.widget] || DISPLAY_WIDGET_CLASS[rec.widget] ||
                      (WIDGET_CLASSES.has(rec.widget) ? rec.widget : 'Textbox');

  rec.slot.textContent = '';
  /* reads subscribe to the target (events speak the original's name);    */
  /* writes go through the alias's own "Value" slot - the doorway - so     */
  /* the alias's own Name/Container/X/Y are never touched                  */
  const el = bindLiveControl(rec.target, rec.targetProp, widgetClass, propertyValues[rec.target + '.' + rec.targetProp],
    (v) => send({ cmd: 'set-property', instance: alias, prop: 'Value', value: v }));
  el.classList && el.classList.add('widget-atom-control');
  rec.slot.appendChild(el);
  rec.control = el;

  rec.labelEl.textContent = rec.label || (baseName(rec.target) + '.' + rec.targetProp);

  /* wiring through the alias is wiring to the original (ResolvePort,       */
  /* object.c) - the atom arms a wire on its doorway slot                    */
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
  el.onmousedown = (ev) => {
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
  instances[alias] = { className: 'Alias', el, ports: {} };
  livePositions[alias] = { el };
  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });

  /* what it stands for + its own presentation - the atom assembles itself  */
  /* as these arrive (onPropertyChanged's aliasAtoms branch). Widget and     */
  /* Direction are the engine's stamps (create-alias/internals, bridge.c).   */
  send({ cmd: 'subscribe', instance: alias, port: 'Target' });
  send({ cmd: 'subscribe', instance: alias, port: 'TargetProp' });
  send({ cmd: 'subscribe', instance: alias, port: 'Widget' });
  send({ cmd: 'subscribe', instance: alias, port: 'Direction' });
  send({ cmd: 'subscribe', instance: alias, port: 'Label' });

  log('created ' + alias + ' (Alias)', 'event');
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
  delete aliasAtoms[alias];
  delete cardBodies[alias];
  delete internalsOwner[alias];

  wires = wires.filter((w) => {
    if (w.fromAlias !== alias && w.toAlias !== alias) return true;
    w.line.remove();
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
  moveKey(aliasAtoms);
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
  const atom = aliasAtoms[newAlias];
  if (atom && !atom.label) atom.labelEl.textContent = baseName(atom.target) + '.' + atom.targetProp;

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
    container: '',
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

/* the shared mousedown for every card/atom/view - one dispatch, no          */
/* per-view or per-kind modes: Move drags the thing itself, Clone drags a   */
/* ghost that creates an independent instance where dropped, Alias (on a    */
/* widget atom) drags a ghost that creates an Alias of its primary control  */
function startDrag(ev, el, alias, className, primaryProp) {
  const mode = effectiveMode(el);
  alias = aliasOfEl(el, alias);   /* the CURRENT name, not the birth name */

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
  dragState = {
    el,
    alias,
    offsetX: ev.clientX - rect.left,
    offsetY: ev.clientY - rect.top,
  };
  ev.preventDefault();
}

/* position is relative to whatever positioned ancestor the element        */
/* currently sits in (.view-inner is position:relative; the canvas is the   */
/* outermost case) - correct DOM nesting instead of coordinate math         */
document.addEventListener('mousemove', (ev) => {
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
  if (dragState.alias) updateWiresFor(dragState.alias);
});

/* the placing click - capture phase, so nothing under the cursor reacts   */
/* to it (the click means "put it here", not "press this"). The arming     */
/* click never reaches here: startGestureDrag runs from an element         */
/* handler after capture has already passed, and stops propagation.        */
document.addEventListener('mousedown', (ev) => {
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

/* Esc drops whatever is being carried, nothing happens anywhere - and    */
/* closes an open file dialog the same way                                */
document.addEventListener('keydown', (ev) => {
  if (ev.key === 'Escape' && gestureDrag) {
    cancelGestureDrag();
    log('cancelled', 'event');
  }
  if (ev.key === 'Escape' && flowDialog) closeFlowDialog();
});

document.addEventListener('mouseup', (ev) => {
  if (panelDrag) {
    /* commit the panel's place - the panel's own shared properties, the  */
    /* icon's X/Y untouched                                                */
    send({ cmd: 'set-property', instance: panelDrag.alias, prop: 'PanelX', value: String(parseInt(panelDrag.el.style.left, 10) || 0) });
    send({ cmd: 'set-property', instance: panelDrag.alias, prop: 'PanelY', value: String(parseInt(panelDrag.el.style.top, 10) || 0) });
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
    const currentContainer = (currentInner && currentInner.dataset && currentInner.dataset.viewAlias) || '';

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
  dragState = null;
});

applyMode('Operate');
connectSocket();
