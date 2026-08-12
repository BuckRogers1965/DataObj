/* The Markdown control's browser half - including its renderer, which used
   to sit in the host. A dependency-free renderer (the framework's own "no
   external dependencies" discipline): headings, bold/italic, inline code,
   fenced blocks, bullets, paragraphs. Input is escaped before any markup is
   applied, so arbitrary flow data is safe to display. */
register('Markdown', {
  create(ctx) {
    const el = document.createElement('div');
    el.className = 'markdown-view';
    Object.defineProperty(el, 'value', {
      get() { return this._v || ''; },
      set(v) { this._v = v; this.innerHTML = renderMarkdown(v); },
    });
    if (ctx && ctx.defaultValue) el.value = ctx.defaultValue;
    return el;
  },
});

function renderMarkdown(md) {
  const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const inline = (s) => esc(s)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\*([^*]+)\*/g, '<em>$1</em>');
  let html = '', inCode = false, inList = false;
  for (const line of String(md || '').split('\n')) {
    if (line.trim().startsWith('```')) {
      if (inList) { html += '</ul>'; inList = false; }
      html += inCode ? '</code></pre>' : '<pre><code>';
      inCode = !inCode;
      continue;
    }
    if (inCode) { html += esc(line) + '\n'; continue; }
    const li = line.match(/^\s*[-*]\s+(.*)/);
    if (inList && !li) { html += '</ul>'; inList = false; }
    if (li) {
      if (!inList) { html += '<ul>'; inList = true; }
      html += '<li>' + inline(li[1]) + '</li>';
      continue;
    }
    /* a heading is hashes then the text: the space after them is optional  */
    /* ("#Title" is a heading), leading indentation is tolerated (a pasted   */
    /* block carries the indent it was copied from), and all six levels      */
    /* exist. Nothing here requires column zero or a perfectly typed space.  */
    const h = line.match(/^\s*(#{1,6})\s*(.*)/);
    if (h) { html += '<h' + h[1].length + '>' + inline(h[2]) + '</h' + h[1].length + '>'; continue; }
    if (line.trim()) html += '<p>' + inline(line) + '</p>';
  }
  if (inList) html += '</ul>';
  if (inCode) html += '</code></pre>';
  return html;
}

/* a MenuButton, wherever it appears - the topbar's File/Mode chrome and a  */
/* dropped-in MenuButton instance are the same object rendered the same     */
/* way (registerWidgetAtom below reuses this too). Label/Items/Selected are */
/* just ordinary properties, subscribed like anything else - the button's   */
/* own text and its dropdown's contents only become correct once the        */
/* subscribe echoes their current values back (Bridge_Subscribe pushes the   */
/* current value immediately, so this is near-instant, not a visible wait).  */
