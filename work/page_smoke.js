const fs = require('fs');

// --- minimal DOM/canvas mocks ---
const drawn = [];
function mockCtx() {
  return {
    fillStyle: null,
    clearRect() {}, fillRect(x, y, w, h) { drawn.push([x, y, w, h]); },
  };
}
function mockCanvas() { return { width: 0, height: 0, getContext: () => mockCtx() }; }
const elements = {};
function mockEl(tag) {
  return {
    children: [], style: {},
    set className(v) {}, get className() { return ''; },
    set textContent(v) { this._t = v; }, get textContent() { return this._t; },
    set innerHTML(v) { this._h = v; }, get innerHTML() { return this._h; },
    appendChild(c) { this.children.push(c); },
    getContext: tag === 'canvas' ? () => mockCtx() : undefined,
    width: 0, height: 0,
  };
}
global.document = {
  getElementById: (id) => (elements[id] = elements[id] || mockEl('div')),
  createElement: (tag) => mockEl(tag),
};

// load data + page script
const data = fs.readFileSync('/workspace/work/report_data.js', 'utf8');
let html = fs.readFileSync('/workspace/work/index.html', 'utf8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { console.log('NO INLINE SCRIPT FOUND'); process.exit(1); }
eval(data + m[1]);
console.log('page script ran OK, fillRect calls:', drawn.length);
console.log('stats html set:', (elements['stats'] && elements['stats']._h || '').slice(0, 80));
