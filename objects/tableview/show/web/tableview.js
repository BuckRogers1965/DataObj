/* THE TABLEVIEW'S BROWSER HALF - the spreadsheet dividers, and nothing else.

   A TableView IS a View: it contains controls and they lay out by their own
   X/Y, so containment is not reimplemented here. registerView does that -
   the same call the View class makes - and this adds the one thing a grid
   has that a view does not: a line on every column edge and every row edge
   that you can drag.

   Dragging the line after B sizes B - that column and every cell in it -
   and leaves A and C where they are, the way a spreadsheet does. Each
   column's width is its own engine property (ColW_B); a column nobody has
   touched has none and takes the default.

   Every number here is the engine's - GridX/GridY/GridGap, CellW/CellH,
   ViewRows/ViewCols, LockCells are ordinary published properties, watched
   like anything else. Nothing in this file decides a layout. */

function tableDividers(alias) {
  const view = views[alias];
  if (!view) return;

  const geom = {
    GridX: 44, GridY: 30, GridGap: 6,
    CellW: 80, CellH: 24,
    ViewRows: 3, ViewCols: 3,
    Row: 0, Col: 0,			/* where the window sits - the sizes are absolute */
    LockCells: 0,
  };

  const lines = [];

  /* A..Z, AA.. - the same column names the engine uses, so the property
     this writes is the one the engine reads */
  function colName(c) {
    let n = '', x = c;
    do { n = String.fromCharCode(65 + (x % 26)) + n; x = Math.floor(x / 26) - 1; } while (x >= 0);
    return n;
  }
  /* A WIDTH BELONGS TO A COLUMN, so it is keyed by the ABSOLUTE column,
     not by which slot the column happens to be in. Scroll onto D and the
     line still asks about D. */
  const colProp = (slot) => 'ColW_' + colName(Number(geom.Col) + slot);
  const rowProp = (slot) => 'RowH_' + (Number(geom.Row) + slot + 1);

  const widthOf = (slot) => Number(geom[colProp(slot)]) || Number(geom.CellW) || 80;
  const heightOf = (slot) => Number(geom[rowProp(slot)]) || Number(geom.CellH) || 24;

  function clear() {
    while (lines.length) lines.pop().remove();
  }

  /* ONE LINE, ONE COLUMN. The line after B belongs to B: dragging it
     moves the pointer one-to-one with B's width and nothing else shifts
     size - the columns to the right just slide along. */
  function grab(el, prop, axis) {
    el.onpointerdown = (ev) => {
      if (Number(geom.LockCells)) return;
      ev.stopPropagation();
      ev.preventDefault();

      const start = axis === 'x' ? ev.clientX : ev.clientY;
      const was = Number(geom[prop]) || (axis === 'x' ? Number(geom.CellW) : Number(geom.CellH));

      const move = (e2) => {
        const now = axis === 'x' ? e2.clientX : e2.clientY;
        geom[prop] = Math.max(16, Math.min(400, was + (now - start)));
        place();
      };
      const up = () => {
        document.removeEventListener('pointermove', move);
        document.removeEventListener('pointerup', up);
        send({ cmd: 'set-property', instance: cur(alias), prop,
               value: String(Math.round(geom[prop])) });
      };
      document.addEventListener('pointermove', move);
      document.addEventListener('pointerup', up);
    };
  }

  function line(cls, prop, axis, index) {
    const el = document.createElement('div');

    el.className = 'table-divider ' + cls;
    el.dataset.index = String(index);
    el.title = axis === 'x' ? 'drag: width of every column'
                            : 'drag: height of every row';
    view.innerEl.appendChild(el);
    lines.push(el);
    grab(el, prop, axis);
    return el;
  }

  const WIDE = 5;		/* the grab strip, centred in the gap */
  const NUDGE_X = 5;	/* column lines sit 5 to the right of centre */
  const NUDGE_Y = 5;	/* row lines sit 5 below it */

  function place() {
    const x = Number(geom.GridX), y = Number(geom.GridY), g = Number(geom.GridGap);
    const rows = Number(geom.ViewRows), cols = Number(geom.ViewCols);
    let gridW = 0, gridH = 0, at;

    for (let c = 0; c < cols; c++) gridW += widthOf(c) + g;
    for (let r = 0; r < rows; r++) gridH += heightOf(r) + g;
    gridW -= g;
    gridH -= g;

    clear();
    if (Number(geom.LockCells)) return;		/* locked: no handles at all */

    /* CENTRED IN THE GAP between two cells, not flush against the one it
       follows - a divider sits between things, and against an edge it looks
       like part of that cell */
    at = x;
    for (let c = 0; c < cols; c++) {
      at += widthOf(c);
      const el = line('table-divider-col', colProp(c), 'x', c);
      el.style.left = Math.round(at + (g - WIDE) / 2) + NUDGE_X + 'px';
      el.style.top = y + 'px';
      el.style.height = gridH + 'px';
      at += g;
    }
    at = y;
    for (let r = 0; r < rows; r++) {
      at += heightOf(r);
      const el = line('table-divider-row', rowProp(r), 'y', r);
      el.style.top = Math.round(at + (g - WIDE) / 2) + NUDGE_Y + 'px';
      el.style.left = x + 'px';
      el.style.width = gridW + 'px';
      at += g;
    }
  }

  for (const p of Object.keys(geom)) {
    send({ cmd: 'subscribe', instance: alias, port: p });
  }
  /* and each visible column's and row's own size */
  function watchSizes() {
    for (let c = 0; c < Number(geom.ViewCols); c++)
      if (!(colProp(c) in geom)) {
        geom[colProp(c)] = 0;
        send({ cmd: 'subscribe', instance: cur(alias), port: colProp(c) });
      }
    for (let r = 0; r < Number(geom.ViewRows); r++)
      if (!(rowProp(r) in geom)) {
        geom[rowProp(r)] = 0;
        send({ cmd: 'subscribe', instance: cur(alias), port: rowProp(r) });
      }
  }

  /* the host hands a view class its own property updates, so the lines
     follow a size typed into Settings exactly as they follow a drag */
  view.onProp = (port, value) => {
    if (!(port in geom) && !/^(ColW_[A-Z]+|RowH_\d+)$/.test(port)) return;
    geom[port] = Number(value) || 0;
    /* the window moving or resizing changes WHICH columns the lines are
       about, so the subscriptions have to follow before they are drawn */
    if (port === 'ViewRows' || port === 'ViewCols'
        || port === 'Row' || port === 'Col') watchSizes();
    place();
  };

  watchSizes();
  place();
}

register('TableView', {
  renderInstance: (s) => {
    /* a TableView is a View - containment inherited, not reimplemented */
    registerView(s.alias, s.props, s.pos, s.hidden, s.container, s.reservedIn, s.reservedOut);
    tableDividers(s.alias);
  },
});
