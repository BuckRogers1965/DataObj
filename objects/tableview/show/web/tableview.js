/* THE TABLEVIEW'S BROWSER HALF - the spreadsheet dividers, and nothing else.

   A TableView IS a View: it contains controls and they lay out by their own
   X/Y, so containment is not reimplemented here. registerView does that -
   the same call the View class makes - and this adds the one thing a grid
   has that a view does not: a line on every column edge and every row edge
   that you can drag.

   Dragging any of them sets the size of EVERY column, or every row. They
   are not per-column widths; there is one width, the way the engine stores
   it, and each line is just another handle onto it.

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
    LockCells: 0,
  };

  const lines = [];

  function clear() {
    while (lines.length) lines.pop().remove();
  }

  /* one gesture, one property: a drag moves the line under the pointer and
     the release commits the size. Where you let go IS the size. */
  function grab(el, prop, axis) {
    el.onpointerdown = (ev) => {
      if (Number(geom.LockCells)) return;
      ev.stopPropagation();
      ev.preventDefault();

      const start = axis === 'x' ? ev.clientX : ev.clientY;
      const was = Number(geom[prop]) || 0;
      const step = Math.max(1, Number(el.dataset.index) + 1);

      const move = (e2) => {
        const now = axis === 'x' ? e2.clientX : e2.clientY;
        /* the line sits after `step` cells, so moving it by N changes each
           of those cells by N/step - drag the third divider and the columns
           follow the pointer instead of racing ahead of it */
        geom[prop] = Math.max(16, Math.min(400, was + (now - start) / step));
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

  function place() {
    const x = Number(geom.GridX), y = Number(geom.GridY), g = Number(geom.GridGap);
    const cw = Number(geom.CellW), ch = Number(geom.CellH);
    const rows = Number(geom.ViewRows), cols = Number(geom.ViewCols);
    const gridW = cols * (cw + g) - g;
    const gridH = rows * (ch + g) - g;

    clear();
    if (Number(geom.LockCells)) return;		/* locked: no handles at all */

    /* one line following each column, spanning the rows */
    for (let c = 0; c < cols; c++) {
      const el = line('table-divider-col', 'CellW', 'x', c);
      el.style.left = (x + (c + 1) * (cw + g) - g) + 'px';
      el.style.top = y + 'px';
      el.style.height = gridH + 'px';
    }
    /* and one following each row, spanning the columns */
    for (let r = 0; r < rows; r++) {
      const el = line('table-divider-row', 'CellH', 'y', r);
      el.style.top = (y + (r + 1) * (ch + g) - g) + 'px';
      el.style.left = x + 'px';
      el.style.width = gridW + 'px';
    }
  }

  for (const p of Object.keys(geom)) {
    send({ cmd: 'subscribe', instance: alias, port: p });
  }

  /* the host hands a view class its own property updates, so the lines
     follow a size typed into Settings exactly as they follow a drag */
  view.onProp = (port, value) => {
    if (!(port in geom)) return;
    geom[port] = Number(value) || 0;
    place();
  };

  place();
}

register('TableView', {
  renderInstance: (s) => {
    /* a TableView is a View - containment inherited, not reimplemented */
    registerView(s.alias, s.props, s.pos, s.hidden, s.container, s.reservedIn, s.reservedOut);
    tableDividers(s.alias);
  },
});
