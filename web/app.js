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
      PROP_VUMETER = 6, PROP_TEXTOUT = 7, PROP_KNOB = 8, PROP_LABEL = 9, PROP_NULL = 10, PROP_MENU = 11;

/* palette classes that ARE widgets - the base case of the recursion */
const WIDGET_CLASSES = new Set(['Checkbox', 'Textbox', 'Slider', 'Knob', 'Label', 'LED', 'TextOut', 'VUMeter', 'Button', 'MenuButton']);

/* which widget class backs which property widget-type, split by whether  */
/* the widget is something the user edits or something that only displays */
const INPUT_WIDGET_CLASS   = { [PROP_TEXTBOX]: 'Textbox', [PROP_CHECKBOX]: 'Checkbox', [PROP_SLIDER]: 'Slider', [PROP_KNOB]: 'Knob' };
const DISPLAY_WIDGET_CLASS = { [PROP_LED]: 'LED', [PROP_TEXTOUT]: 'TextOut', [PROP_VUMETER]: 'VUMeter', [PROP_LABEL]: 'Label' };

let ws = null;
let classes = {};          // className -> [{Name,Direction,Widget,Default}, ...]
let instances = {};        // alias -> {className, el, ports: {name: dotEl}}
let widgets = {};          // widgetAlias -> {kind:'input'|'display'|'action', widgetClass, targetAlias, targetProp, el}
let selfDisplays = {};     // "alias.propName" -> {el,widgetClass}, for a widget class's own State shown on itself
let liveControls = {};     // "alias.propName" -> {el,widgetClass}, an editable control synced from its own property-changed events
let wires = [];            // {fromAlias, fromPort, toAlias, toPort, lineEl}
let aliasCounters = {};    // className -> next number, for user-placed palette instances
let widgetAliasCounters = {}; // widgetClass -> next number, for internally-wired widget instances
let pendingPort = null;    // {alias, port, dotEl} - first end of a wire being drawn
let dragState = null;      // {alias, offsetX, offsetY}
let nextPos = { x: 250, y: 30 }; /* clear of the palette panel's own top-left corner */
let pendingPositions = {}; // alias -> {x,y}, staged by createInstance() for an instance this client is about to create
let livePositions = {};    // alias -> {el}, a card whose X/Y are real properties kept in sync like anything else
let menuButtons = {};      // alias -> {label,items,selected,btn,dropdown}, any MenuButton (topbar chrome or dropped-in)
let propertyValues = {};   // "alias.propName" -> last known value, from property-changed - what Clone reads to copy a source's configuration
let copyRenderings = {};   // alias -> [el, el, ...], every extra rendering Copy has made of an alias, beyond the one tracked in instances{}
let views = {};             // alias -> {el, innerEl, mode}, a real View instance's own rendering
let pendingContainer = {};  // containerAlias -> [el, el, ...], elements waiting for a View that hasn't rendered yet

/* the session's own current interaction mode - a real property (Chrome's  */
/* ModeMenu instance, "Selected") kept in sync the exact same way anything */
/* else is, so switching mode is visible to every connected window, not     */
/* just this one - see onPropertyChanged's ModeMenu special case below and  */
/* applyMode(). "Operate" matches BuildChrome's own default (object.c).     */
let currentMode = 'Operate';

function $(id) { return document.getElementById(id); }

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

/* a View can override the session's own mode for everything directly       */
/* inside it (view.c's own doc comment - Mode="" means "use the session's    */
/* current mode", anything else wins regardless) - this is the entire        */
/* mechanism that makes the Palette a permanent Clone station (BuildPalette, */
/* object.c sets its Mode to "Clone") with nothing view.c or app.js needs     */
/* to know about "palette" specifically. Every gesture (onPortClick,          */
/* attachDeleteGesture, attachCloneGesture, attachCopyGesture, startDrag)     */
/* reads this instead of currentMode directly.                                */
function effectiveMode(el) {
  const container = el.closest('.view-inner');
  if (container && container.dataset.viewAlias) {
    const view = views[container.dataset.viewAlias];
    if (view && view.mode) return view.mode;
  }
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
/* /Root/Palette/... for BuildPalette's bootstrap instances - object.c) -    */
/* there is no separate "creation path" concept distinct from this: /Root/   */
/* here is just what the current path happens to be at the moment of birth,  */
/* the same as it is at any later moment. Moving an instance to a different   */
/* View later changes both Container AND this path together (Bridge_Rename,   */
/* bridge.c) - a client re-keys everything it has stored under the old        */
/* alias to the new one (onInstanceRenamed) rather than the name staying      */
/* fixed while only Container moves underneath it.                            */
function createInstance(className) {
  aliasCounters[className] = (aliasCounters[className] || 0) + 1;
  const alias = '/Root/' + className + aliasCounters[className];
  send({ cmd: 'create-instance', class: className, as: alias });
  /* the box itself is built on the instance-created event that comes  */
  /* back, not here - the server is the source of truth for whether it */
  /* actually exists                                                   */

  /* position is a property like any other - a freshly created instance */
  /* is just as real an object as one dragged a minute ago, so where it   */
  /* first appears has to be written back immediately, not held only as   */
  /* local placement the server (and every other window) never learns.   */
  const pos = nextPos;
  nextPos = { x: (nextPos.x + 40) % 500 + 30, y: nextPos.y + (nextPos.x > 460 ? 130 : 0) + 0 };
  pendingPositions[alias] = pos;
  send({ cmd: 'set-property', instance: alias, prop: 'X', value: String(pos.x) });
  send({ cmd: 'set-property', instance: alias, prop: 'Y', value: String(pos.y) });
  return alias;
}

function nextWidgetAlias(widgetClass) {
  widgetAliasCounters[widgetClass] = (widgetAliasCounters[widgetClass] || 0) + 1;
  return '_' + widgetClass + widgetAliasCounters[widgetClass];
}

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
        send({ cmd: 'set-property', instance: alias, prop: 'Selected', value: item });
        dropdown.style.display = 'none';
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
  /* more than one rendering can subscribe to the same alias.prop (Copy -   */
  /* see renderCopy) - every one of them has to keep getting updated, not   */
  /* just whichever registered last, so this is a list, not a single slot   */
  const key = subscribeAlias + '.' + subscribeProp;
  (liveControls[key] = liveControls[key] || []).push({ el, widgetClass });
  return el;
}

/* every rendered control, however it's nested, is backed by a real,       */
/* separately addressable object (makeInputWidget/makeDisplayWidget/        */
/* makeButtonWidget already create one) - that's what makes it exactly as   */
/* wireable as a top-level atom, just currently sitting inside its parent's  */
/* card instead of standing on the canvas by itself. Same pattern            */
/* registerWidgetAtom uses for itself: a wrapper carries the click-to-wire   */
/* listener (the control inside loses pointer-events outside Operate mode,   */
/* see the CSS, so the click falls through to this wrapper instead), and     */
/* instances[alias] is what onConnected/updateWiresFor need to find it.      */
function wireable(alias, className, propName, controlEl) {
  const wrap = document.createElement('span');
  wrap.className = 'wireable';
  wrap.appendChild(controlEl);
  wrap.addEventListener('click', () => onPortClick(alias, propName, wrap));
  instances[alias] = { className, el: wrap, ports: { [propName]: wrap } };
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

/* --- recursive case: wrap a composite object's property in a real widget instance --- */

/* hidden:'1' marks these as plumbing, not first-class session objects -  */
/* server-side state (see Bridge_InstanceEvent's doc comment), not just   */
/* client bookkeeping, so a client that replays the view later (a fresh   */
/* page load, a different browser entirely) still knows to skip them      */
/* instead of rendering a stray extra card for every wrapped control      */
function makeInputWidget(widgetClass, targetAlias, targetProp, defaultValue) {
  const widgetAlias = nextWidgetAlias(widgetClass);
  send({ cmd: 'create-instance', class: widgetClass, as: widgetAlias, hidden: '1' });
  if (defaultValue) send({ cmd: 'set-property', instance: widgetAlias, prop: 'Value', value: defaultValue });
  send({ cmd: 'bind-property', from: widgetAlias, fromPort: 'Value', to: targetAlias, toProp: targetProp });

  widgets[widgetAlias] = { kind: 'input', widgetClass, targetAlias, targetProp };

  const el = bindLiveControl(widgetAlias, 'Value', widgetClass, defaultValue,
    (v) => send({ cmd: 'set-property', instance: widgetAlias, prop: 'Value', value: v }));

  /* the write path above is deliberately one-way (widget.Value -> target */
  /* property, via bind-property) to avoid a feedback loop - but nothing  */
  /* was ever reading the target's CURRENT value back, so a change made   */
  /* from a different window (its own separate hidden widget, or any      */
  /* other source) never showed up here. Subscribing straight to the      */
  /* target property is the read side of the same "everything is a        */
  /* property, everything is subscribable" rule - it only ever updates    */
  /* what's displayed, never re-sends, so it can't loop back into the      */
  /* write path above.                                                     */
  send({ cmd: 'subscribe', instance: targetAlias, port: targetProp });
  /* list, not a single slot - same reasoning as bindLiveControl above:     */
  /* if targetAlias has been Copy'd, every rendering's hidden widget needs   */
  /* to keep reading the target's real value, not just whichever one        */
  /* registered here last                                                   */
  const targetKey = targetAlias + '.' + targetProp;
  (liveControls[targetKey] = liveControls[targetKey] || []).push({ el, widgetClass });

  return wireable(widgetAlias, widgetClass, 'Value', el);
}

function makeDisplayWidget(widgetClass, targetAlias, targetPort) {
  const widgetAlias = nextWidgetAlias(widgetClass);
  send({ cmd: 'create-instance', class: widgetClass, as: widgetAlias, hidden: '1' });
  send({ cmd: 'connect', from: targetAlias, fromPort: targetPort, to: widgetAlias, toPort: 'In' });
  send({ cmd: 'subscribe', instance: widgetAlias, port: 'Value' });

  const el = makeReadoutEl(widgetClass);
  widgets[widgetAlias] = { kind: 'display', widgetClass, targetAlias, targetPort, el };
  return wireable(widgetAlias, widgetClass, 'Value', el);
}

function makeButtonWidget(targetAlias) {
  const widgetAlias = nextWidgetAlias('Button');
  send({ cmd: 'create-instance', class: 'Button', as: widgetAlias, hidden: '1' });
  send({ cmd: 'bind-activate', from: widgetAlias, fromPort: 'Out', to: targetAlias });

  widgets[widgetAlias] = { kind: 'action', widgetClass: 'Button', targetAlias };

  const btn = document.createElement('button');
  btn.className = 'activate-btn';
  btn.textContent = 'Activate';
  btn.onclick = () => { if (effectiveMode(btn) === 'Operate') send({ cmd: 'activate', instance: widgetAlias }); };
  return wireable(widgetAlias, 'Button', 'Out', btn);
}

/* a standalone widget instance: its own natural control (or dot) plus a  */
/* label, positioned and draggable exactly like any other card - no        */
/* header, no property rows, no footer, and its wiring dots (below) only    */
/* ever show up in Connect mode, same as everything else's.                */
/*                                                                          */
/* isCopy is what makes this the SAME rendering path serve Copy mode too    */
/* (see renderCopy): a copy binds every control to the SAME alias (so       */
/* editing it edits the one real object, exactly like Clone doesn't but      */
/* Copy does - "still hooked to its original source") except position,      */
/* which is inherently per-rendering once more than one exists - a copy      */
/* drags locally (like the palette panel, startDrag's alias:null form)       */
/* instead of subscribing to/writing the shared instance's X/Y, and is       */
/* tracked in copyRenderings instead of instances so deleting the shared     */
/* object (from any rendering) can find and remove every copy of it too.     */
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
    label.textContent = alias + (isCopy ? ' (copy)' : '');
    el.appendChild(label);
  }

  /* the whole atom IS the property, the same way clicking a card's row is  */
  /* that row's property - no separate dot, no in/out distinction, click    */
  /* it in Connect mode and it arms/completes a wire on primaryProp         */
  el.addEventListener('click', () => onPortClick(alias, primaryProp, el));

  /* dragging the atom moves it; the control itself keeps its own gesture  */
  /* (a click, a slider drag) - only the chrome around it starts a move    */
  el.onmousedown = (ev) => { if (ev.target !== control) startDrag(ev, el, isCopy ? null : alias); };

  attachDeleteGesture(el, alias);
  attachCloneGesture(el, alias, className);
  attachCopyGesture(el, alias, className);

  if (isCopy) {
    /* a copy manages its own local position independent of the source     */
    /* (see registerCard's matching comment) - always top-level, never       */
    /* container-aware, for the same reason                                 */
    $('canvas').appendChild(el);
    (copyRenderings[alias] = copyRenderings[alias] || []).push(el);
    log('copied ' + alias, 'event');
    return;
  }

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
function registerView(alias, props, pos, isCopy, container) {
  const el = document.createElement('div');
  el.className = 'view-panel';
  el.style.left = pos.x + 'px';
  el.style.top = pos.y + 'px';
  el.style.width = '190px';
  el.style.height = '220px';

  const header = document.createElement('div');
  header.className = 'view-header';
  header.textContent = alias + (isCopy ? ' (copy)' : '');
  el.appendChild(header);

  const innerEl = document.createElement('div');
  innerEl.className = 'view-inner';
  innerEl.dataset.viewAlias = alias;
  innerEl.dataset.viewMode = '';
  el.appendChild(innerEl);

  const resizeHandle = document.createElement('div');
  resizeHandle.className = 'view-resize-handle';
  resizeHandle.style.display = 'none'; /* shown once Resizeable arrives as "1" */
  resizeHandle.onmousedown = (ev) => startResize(ev, alias);
  el.appendChild(resizeHandle);

  header.onmousedown = (ev) => startDrag(ev, el, isCopy ? null : alias);
  attachDeleteGesture(el, alias);
  attachCloneGesture(el, alias, 'View');
  attachCopyGesture(el, alias, 'View');

  if (isCopy) {
    /* a copied View is a real second rendering of the panel chrome, but    */
    /* not a second container - its children stay wherever the original     */
    /* View's own innerEl is (views{} is single-slot, deliberately not       */
    /* extended for this: nesting a live container inside two places at      */
    /* once is a bigger feature than "click to see another view of a         */
    /* value"). Scope decision, not an oversight.                            */
    $('canvas').appendChild(el);
    (copyRenderings[alias] = copyRenderings[alias] || []).push(el);
    log('copied ' + alias, 'event');
    return;
  }

  const view = { el, innerEl, header, resizeHandle, mode: '' };
  views[alias] = view;
  /* instance-created carries Container inline - see registerWidgetAtom's   */
  /* matching comment                                                       */
  placeInContainer(el, container || '');
  flushPendingContainer(alias);

  instances[alias] = { className: 'View', el, ports: {} };
  livePositions[alias] = { el };

  send({ cmd: 'subscribe', instance: alias, port: 'X' });
  send({ cmd: 'subscribe', instance: alias, port: 'Y' });
  send({ cmd: 'subscribe', instance: alias, port: 'W' });
  send({ cmd: 'subscribe', instance: alias, port: 'H' });
  send({ cmd: 'subscribe', instance: alias, port: 'Container' });
  send({ cmd: 'subscribe', instance: alias, port: 'Mode' });
  send({ cmd: 'subscribe', instance: alias, port: 'Resizeable' });

  log('created ' + alias + ' (View)', 'event');
}

let resizeState = null; // {alias, el, startW, startH, startX, startY}

function startResize(ev, alias) {
  const view = views[alias];
  if (!view || view.resizeHandle.style.display === 'none') return;
  ev.stopPropagation();
  ev.preventDefault();
  const rect = view.el.getBoundingClientRect();
  resizeState = { alias, el: view.el, startW: rect.width, startH: rect.height, startX: ev.clientX, startY: ev.clientY };
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
  /* plumbing widget instances (created by makeInputWidget/makeDisplayWidget/  */
  /* makeButtonWidget to back some other node's control) are already fully    */
  /* wired and rendered into their parent's card by the time this event       */
  /* arrives - registered in widgets{} synchronously, before the round trip   */
  /* to the server and back. They never get a canvas card of their own.       */
  if (widgets[alias]) return;

  /* the same plumbing, but replayed to a client that never created it (a  */
  /* fresh page load, a different browser) and so has no local widgets{}   */
  /* record - hidden is real server-side state precisely for this case     */
  if (hidden) return;

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
  /* self-created: the position createInstance() already staged and told  */
  /* the server about. Replayed (list-instances, or another client's       */
  /* create-instance): no local stake in it yet - place it anywhere, the   */
  /* X/Y subscribe below corrects it to the real value almost immediately. */
  const pos = pendingPositions[alias] || { x: 30, y: 30 };
  delete pendingPositions[alias];

  /* a View is not a special client-side concept (the Palette included -   */
  /* it's just a View whose bootstrap children happen to have Container    */
  /* set already, see BuildPalette, object.c) - it gets its own rendering   */
  /* because it's the one class that actually contains other instances,    */
  /* not because it's a Palette.                                           */
  if (className === 'View') {
    registerView(alias, props, pos, false, container);
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
/* object or a different wire. isCopy: see registerWidgetAtom's doc          */
/* comment - the exact same reasoning applies here (bind every control to    */
/* the same alias, but position and instances[]/copyRenderings registration  */
/* diverge for a second rendering).                                          */
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
  icon.appendChild(makeDisplayWidget('LED', alias, 'State'));
  wrap.appendChild(icon);

  const panel = document.createElement('div');
  panel.className = 'node-box';
  panel.style.display = 'none';

  const header = document.createElement('div');
  header.className = 'node-header';
  const title = document.createElement('span');
  title.className = 'node-title';
  title.textContent = alias + (isCopy ? ' (copy)' : '');
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

  for (const p of props) {
    /* position isn't a settings-row control, it's the card's own drag       */
    /* position (see the livePositions wiring below); State is on the icon   */
    /* now, not a second time in the panel header                            */
    if (p.Name === 'X' || p.Name === 'Y' || p.Name === 'W' || p.Name === 'H' || p.Name === 'State') continue;

    if (p.Direction === 'data') {
      let controlEl;
      if (INPUT_WIDGET_CLASS[p.Widget]) {
        controlEl = makeInputWidget(INPUT_WIDGET_CLASS[p.Widget], alias, p.Name, p.Default);
      } else if (DISPLAY_WIDGET_CLASS[p.Widget]) {
        controlEl = makeDisplayWidget(DISPLAY_WIDGET_CLASS[p.Widget], alias, p.Name);
      } else {
        continue; /* no widget backs this widget-type (e.g. PROP_BUTTON on a data prop) */
      }

      const row = document.createElement('div');
      row.className = 'prop-row';
      const label = document.createElement('label');
      label.textContent = p.Name;
      row.appendChild(label);
      row.appendChild(controlEl);
      body.appendChild(row);

      /* controlEl is already wireable (makeInputWidget/makeDisplayWidget    */
      /* wrap it and register its own real, separately addressable instance  */
      /* - see wireable() above) - nothing more to do here, this row is       */
      /* just its layout, not a second thing to click                        */
    } else if (p.Direction === 'in' || p.Direction === 'out') {
      const hasControl = DISPLAY_WIDGET_CLASS[p.Widget] || INPUT_WIDGET_CLASS[p.Widget];

      const row = document.createElement('div');
      row.className = 'port-row ' + p.Direction + (hasControl ? ' has-control' : '');

      const label = document.createElement('span');
      label.textContent = p.Name;

      if (p.Direction === 'out') {
        row.appendChild(label);
        /* a port can carry the same display-widget metadata a property does */
        /* (Pulse's Out is Widget=PROP_LED)                                   */
        if (DISPLAY_WIDGET_CLASS[p.Widget]) row.appendChild(makeDisplayWidget(DISPLAY_WIDGET_CLASS[p.Widget], alias, p.Name));
      } else {
        /* likewise an in-port can carry input-widget metadata (Enable is    */
        /* Widget=PROP_CHECKBOX) - give it a real, directly-clickable control */
        if (INPUT_WIDGET_CLASS[p.Widget]) row.appendChild(makeInputWidget(INPUT_WIDGET_CLASS[p.Widget], alias, p.Name, p.Default));
        row.appendChild(label);
      }

      body.appendChild(row);

      /* a bare port (no control - Reader's "Out", say) has no separate      */
      /* object standing in for it, so the row (or the icon's own small       */
      /* dot, whichever is currently showing - see setExpanded) is what        */
      /* gets clicked; a port that DOES carry a control is already wireable    */
      /* through it (see wireable() above) - a dot would be a redundant        */
      /* second click target for that one, so only bare ports get one.        */
      if (!hasControl) {
        row.addEventListener('click', () => onPortClick(alias, p.Name, ports[p.Name]));
        panelRows[p.Name] = row;
        ports[p.Name] = row;

        const dot = document.createElement('span');
        dot.className = 'port-dot icon-dot ' + p.Direction;
        dot.title = p.Name;
        dot.addEventListener('click', (ev) => { ev.stopPropagation(); onPortClick(alias, p.Name, ports[p.Name]); });
        icon.appendChild(dot);
        iconDots[p.Name] = dot;
      }
      if (p.Direction === 'out') outPorts.push(p.Name);
    }
  }

  panel.appendChild(body);

  const footer = document.createElement('div');
  footer.className = 'node-footer';
  footer.appendChild(makeButtonWidget(alias));
  panel.appendChild(footer);

  wrap.appendChild(panel);

  /* collapsed <-> expanded is a display choice, not a mode-gated gesture -   */
  /* it's always available, the same as Activate always is - but the CLICK    */
  /* that opens it only fires in Operate mode (or whatever a containing        */
  /* View's own Mode override resolves to - effectiveMode), so Delete/Clone/   */
  /* Copy/Connect clicking the icon still does THEIR thing instead. Toggling   */
  /* re-anchors every bare port's wires to whichever representation is now     */
  /* visible - same alias, same prop, just a different element to draw to.    */
  function setExpanded(next) {
    icon.style.display = next ? 'none' : 'flex';
    panel.style.display = next ? 'block' : 'none';
    for (const name in ports) ports[name] = next ? panelRows[name] : iconDots[name];
    updateWiresFor(alias);
  }

  icon.addEventListener('click', () => { if (effectiveMode(icon) === 'Operate') setExpanded(true); });
  collapseBtn.addEventListener('click', (ev) => { ev.stopPropagation(); setExpanded(false); });

  icon.onmousedown = (ev) => { if (ev.target === icon || ev.target === iconLabel) startDrag(ev, wrap, isCopy ? null : alias); };
  header.onmousedown = (ev) => { if (ev.target !== collapseBtn) startDrag(ev, wrap, isCopy ? null : alias); };
  attachDeleteGesture(wrap, alias);
  attachCloneGesture(wrap, alias, className);
  attachCopyGesture(wrap, alias, className);

  /* an out port still needs its own subscribe wherever this rendering      */
  /* lives - message-flowed log lines aren't state, there's nothing to       */
  /* double up on the way a data property's live value would be              */
  for (const portName of outPorts) send({ cmd: 'subscribe', instance: alias, port: portName });

  if (isCopy) {
    $('canvas').appendChild(wrap);
    (copyRenderings[alias] = copyRenderings[alias] || []).push(wrap);
    log('copied ' + alias, 'event');
    return;
  }

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
  /* same "our own in-flight gesture wins" reasoning as X/Y above, just for  */
  /* W/H - only a View ever subscribes to these, so this is a no-op for      */
  /* everything else                                                        */
  if (livePos && (port === 'W' || port === 'H') && (!resizeState || resizeState.alias !== alias)) {
    const n = parseInt(value, 10) || 0;
    if (port === 'W') livePos.el.style.width = n + 'px';
    else livePos.el.style.height = n + 'px';
    updateWiresFor(alias);
  }

  /* where this instance renders - the top-level canvas ("") or a real       */
  /* View's own inner area (its alias) - arrives the same asynchronous way   */
  /* X/Y does, and can change later exactly like X/Y can (see                */
  /* placeInContainer's own doc comment)                                     */
  if (port === 'Container') {
    const inst = instances[alias];
    if (inst) placeInContainer(inst.el, value);
  }

  /* a View's own two special property VALUES (view.c's doc comment) -      */
  /* Mode drives effectiveMode() for everything rendered inside it, tagged   */
  /* onto the inner element too so the CSS pointer-events rule (style.css)   */
  /* can see it without a per-frame JS pass; Resizeable just shows/hides      */
  /* the corner handle.                                                      */
  const view = views[alias];
  if (view) {
    if (port === 'Mode') {
      view.mode = value || '';
      view.innerEl.dataset.viewMode = view.mode;
    } else if (port === 'Resizeable') {
      view.resizeHandle.style.display = value === '0' ? 'none' : 'block';
    }
  }

  const w = widgets[alias];
  if (w && w.kind === 'display' && port === 'Value') updateReadout(w.el, w.widgetClass, value);

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
  log(alias + '.' + port + ' → ' + value, 'event');
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
    send({ cmd: 'delete-instance', instance: alias });
    onInstanceRemoved(alias);
  });
}

/* a new, independent instance of the source's class, starting from the    */
/* source's current configuration - not a blank instance of the same       */
/* class, and not the same object under a second name (that's Copy - a      */
/* *second subscribe* to the SAME alias, no server-side clone concept        */
/* needed for it at all). State/X/Y/W/H/Container/Deletable are excluded:   */
/* a clone gets its own fresh lifecycle, its own position, and its own       */
/* independence - inheriting the source's Container would silently place     */
/* the clone back wherever the source lives (cloning something out of the    */
/* Palette would otherwise put the clone right back in the Palette), and     */
/* Deletable="0" is specifically the source's own protection, not a          */
/* configuration choice to propagate. propertyValues already holds            */
/* everything else this client has ever been told about the source (see      */
/* onPropertyChanged) - createInstance already stages/broadcasts position     */
/* the same way a palette click does, so cloning is just that plus copying    */
/* the rest of what's known.                                                  */
function cloneInstance(sourceAlias, className) {
  const alias = createInstance(className);

  const props = classes[className] || [];
  for (const p of props) {
    if (p.Direction !== 'data' || ['State', 'X', 'Y', 'W', 'H', 'Container', 'Deletable'].includes(p.Name)) continue;
    const known = propertyValues[sourceAlias + '.' + p.Name];
    if (known !== undefined) send({ cmd: 'set-property', instance: alias, prop: p.Name, value: known });
  }

  log('cloned ' + sourceAlias + ' → ' + alias, 'event');
}

/* Clone mode's gesture: click anywhere on a card/atom to spin off an       */
/* independent copy of it - same pattern as attachDeleteGesture. This is     */
/* also the ENTIRE mechanism behind "the Palette is a permanent Clone         */
/* station": its Mode="Clone" (BuildPalette, object.c) makes effectiveMode    */
/* return "Clone" for anything inside it no matter what the session's own    */
/* mode is, so clicking a palette icon always clones, full stop - nothing     */
/* palette-specific written here at all.                                     */
function attachCloneGesture(el, alias, className) {
  el.addEventListener('click', (ev) => {
    if (effectiveMode(el) !== 'Clone') return;
    ev.stopPropagation();
    cloneInstance(alias, className);
  });
}

/* another rendering of the SAME real object, not a new instance - reuses   */
/* the identical registerWidgetAtom/registerCard construction the original   */
/* went through (isCopy=true), just bound to sourceAlias again instead of a  */
/* freshly created one. No create-instance, no new alias: subscribing is    */
/* the entire mechanism, exactly as discussed - editing a copy's control     */
/* still writes back to sourceAlias (it's the one real object), and every    */
/* value this copy is hydrated with on arrival goes through updateReadout/   */
/* updateLiveControl, which only ever assign - they never re-send, so         */
/* nothing here can loop back out as a spurious set-property.                */
/*                                                                            */
/* KNOWN GAP (ROADMAP.md, Phase 8): a Copy has no path of its own. Every      */
/* other rendering now follows "no creation path, only a current path" -      */
/* this one doesn't - registerCard/registerWidgetAtom's isCopy branch always  */
/* drops it on the top-level canvas regardless of where sourceAlias currently  */
/* lives, and it is never reparented if the source later moves (Bridge_Rename  */
/* only re-keys instances{}/etc, isCopy renderings live in copyRenderings{}    */
/* and are deliberately never placeInContainer'd at all). Needs a real design   */
/* decision, not a patch here - see the roadmap note.                          */
function renderCopy(sourceAlias, className) {
  const props = classes[className] || [];
  const pos = nextPos;
  nextPos = { x: (nextPos.x + 40) % 500 + 30, y: nextPos.y + (nextPos.x > 460 ? 130 : 0) + 0 };

  if (WIDGET_CLASSES.has(className)) registerWidgetAtom(sourceAlias, className, props, pos, true);
  else registerCard(sourceAlias, className, props, pos, true);
}

/* Copy mode's gesture: click anywhere on a card/atom to open another live  */
/* view onto the same object - same pattern as attachDeleteGesture/          */
/* attachCloneGesture                                                        */
function attachCopyGesture(el, alias, className) {
  el.addEventListener('click', (ev) => {
    if (effectiveMode(el) !== 'Copy') return;
    ev.stopPropagation();
    renderCopy(alias, className);
  });
}

/* Delete mode's own gesture (above) already removes the card locally      */
/* before the round trip - this is what makes the SAME removal happen on    */
/* every other window watching, the same "everyone watching reflects it"    */
/* rule position and mode already follow.                                   */
function onInstanceRemoved(alias) {
  const inst = instances[alias];
  if (inst) {
    inst.el.remove();
    delete instances[alias];
  }
  delete livePositions[alias];

  /* the object is gone - every copy of it (see renderCopy) is a rendering  */
  /* of that same now-nonexistent object, not a separate thing that can       */
  /* survive on its own                                                       */
  for (const el of copyRenderings[alias] || []) el.remove();
  delete copyRenderings[alias];

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
/* attachCloneGesture/attachCopyGesture/startDrag/onPortClick) close over      */
/* the alias they were built with rather than re-reading it live, so a        */
/* delete/clone/copy/drag/wire issued against a card AFTER it has been         */
/* renamed (today, only reachable by editing its Container property row       */
/* directly - there is no drag-between-Views gesture yet) will fail until      */
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
  moveKey(livePositions);
  moveKey(menuButtons);
  moveKey(copyRenderings);
  moveKey(widgets);

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

  for (const w of wires) {
    if (w.fromAlias === oldAlias) w.fromAlias = newAlias;
    if (w.toAlias === oldAlias) w.toAlias = newAlias;
  }

  if (views[newAlias]) views[newAlias].innerEl.dataset.viewAlias = newAlias;

  log(oldAlias + ' → ' + newAlias, 'event');
}

/* alias is null for something draggable that isn't a registered instance  */
/* (the palette panel itself, see below) - it moves locally same as ever,  */
/* it just has no server-side X/Y to write back on release                 */
function startDrag(ev, el, alias) {
  if (effectiveMode(el) !== 'Move') return;

  const rect = el.getBoundingClientRect();
  dragState = {
    el,
    alias,
    offsetX: ev.clientX - rect.left,
    offsetY: ev.clientY - rect.top,
  };
  ev.preventDefault();
}

document.addEventListener('mousemove', (ev) => {
  if (!dragState) return;
  const wrap = $('canvas-wrap');
  const x = ev.clientX - wrap.getBoundingClientRect().left + wrap.scrollLeft - dragState.offsetX;
  const y = ev.clientY - wrap.getBoundingClientRect().top + wrap.scrollTop - dragState.offsetY;
  dragState.el.style.left = Math.max(0, x) + 'px';
  dragState.el.style.top = Math.max(0, y) + 'px';
  if (dragState.alias) updateWiresFor(dragState.alias);
});

document.addEventListener('mouseup', () => {
  if (dragState && dragState.alias) {
    /* the identical command an edited textbox sends - moving a card is    */
    /* not a different kind of thing from changing a value, it's the same  */
    /* set-property on X/Y, so it reflects back to every connected window   */
    /* through the same subscribe/property-changed path as anything else    */
    const x = parseInt(dragState.el.style.left, 10) || 0;
    const y = parseInt(dragState.el.style.top, 10) || 0;
    send({ cmd: 'set-property', instance: dragState.alias, prop: 'X', value: String(x) });
    send({ cmd: 'set-property', instance: dragState.alias, prop: 'Y', value: String(y) });
  }
  dragState = null;
});

applyMode('Operate');
connectSocket();
