// A small browser for QuickJS: DOM tree, Canvas2D over native.blit/fill, Image, fetch, localStorage,
// timers and keyboard events from the 3DS buttons. Only what PocketFarmWeb touches.
const N = globalThis.native;
const g = globalThis;
g.window = g; g.self = g;
g.TextEncoder ??= class { encode(s) { return new Uint8Array([...unescape(encodeURIComponent(s))].map(c => c.charCodeAt(0))); } };
g.TextDecoder ??= class { decode(b) { let s = ''; for (const c of new Uint8Array(b?.buffer ?? b)) s += String.fromCharCode(c); try { return decodeURIComponent(escape(s)); } catch { return s; } } };

// ---------- console ----------
const fmt = a => a.map(x => typeof x === 'string' ? x : x instanceof Error ? x + '\n' + x.stack : (() => { try { return JSON.stringify(x); } catch { return String(x); } })()).join(' ');
g.console = { log: (...a) => N.log(fmt(a)), info: (...a) => N.log(fmt(a)), debug() {}, warn: (...a) => N.log('WARN ' + fmt(a)), error: (...a) => N.log('ERROR ' + fmt(a)) };

// ---------- timers ----------
let timerId = 1, now = N.now();
const timers = new Map();
let rafQ = [];
g.performance = { now: () => N.now() };
g.setTimeout = (fn, ms = 0, ...args) => { const id = timerId++; timers.set(id, { fn, at: now + ms, args }); return id; };
g.setInterval = (fn, ms = 0, ...args) => { const id = timerId++; timers.set(id, { fn, at: now + ms, every: Math.max(1, ms), args }); return id; };
g.clearTimeout = g.clearInterval = id => timers.delete(id);
g.requestAnimationFrame = fn => { const id = timerId++; rafQ.push({ id, fn }); return id; };
g.cancelAnimationFrame = id => { rafQ = rafQ.filter(r => r.id !== id); };
g.queueMicrotask ??= fn => Promise.resolve().then(fn);
const safe = (fn, ...a) => { try { fn(...a); } catch (e) { console.error(e); } };

// ---------- events ----------
class EventTarget {
  addEventListener(type, fn, opts) {
    if (!fn) return;
    ((this._ev ??= {})[type] ??= []).push({ fn, once: !!opts?.once });
  }
  removeEventListener(type, fn) {
    const l = this._ev?.[type]; if (l) this._ev[type] = l.filter(x => x.fn !== fn);
  }
  dispatchEvent(e) {
    e.target ??= this; e.currentTarget = this;
    for (const l of [...(this._ev?.[e.type] || [])]) {
      if (l.once) this.removeEventListener(e.type, l.fn);
      safe(l.fn.bind(this), e);
      if (e._stop) return;
    }
    const on = this['on' + e.type];
    if (typeof on === 'function') safe(on.bind(this), e);
    if (e.bubbles && !e._stop && this.parentNode) this.parentNode.dispatchEvent(e);
    else if (e.bubbles && !e._stop && this !== g && this !== document) g.dispatchEvent(e);
  }
}
class Event {
  constructor(type, init = {}) { Object.assign(this, init); this.type = type; this.defaultPrevented = false; }
  preventDefault() { this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; }
}
g.Event = Event;
for (const k of Object.getOwnPropertyNames(EventTarget.prototype)) if (k !== 'constructor') g[k] = EventTarget.prototype[k].bind(g);

// ---------- DOM ----------
const VOID = new Set(['br', 'img', 'input', 'meta', 'link', 'hr', 'source']);
const missing = new Set();
class ClassList {
  constructor(el) { this.el = el; }
  get _s() { return new Set(this.el.className.split(/\s+/).filter(Boolean)); }
  _w(s) { this.el.className = [...s].join(' '); }
  add(...c) { const s = this._s; c.forEach(x => s.add(x)); this._w(s); }
  remove(...c) { const s = this._s; c.forEach(x => s.delete(x)); this._w(s); }
  contains(c) { return this._s.has(c); }
  toggle(c, force) { const s = this._s, on = force ?? !s.has(c); on ? s.add(c) : s.delete(c); this._w(s); return on; }
}
class Style {
  setProperty(k, v) { this[k] = v; }
  removeProperty(k) { delete this[k]; }
  getPropertyValue(k) { return this[k] ?? ''; }
}
class Node extends EventTarget {
  constructor() { super(); this.parentNode = null; this.childNodes = []; }
  get parentElement() { return this.parentNode instanceof Element ? this.parentNode : null; }
  get isConnected() { let n = this; while (n.parentNode) n = n.parentNode; return n === document; }
  get textContent() { return this.childNodes.map(c => c.textContent).join(''); }
  set textContent(v) { this.replaceChildren(); if (v !== '' && v != null) this.append(String(v)); }
  _adopt(c) { if (typeof c === 'string' || typeof c === 'number') c = new Text(String(c)); c.remove?.(); c.parentNode = this; return c; }
  append(...cs) { for (const c of cs) this.childNodes.push(this._adopt(c)); }
  appendChild(c) { this.append(c); return c; }
  prepend(...cs) { this.childNodes.unshift(...cs.map(c => this._adopt(c))); }
  insertBefore(c, ref) { c = this._adopt(c); const i = ref ? this.childNodes.indexOf(ref) : -1; i < 0 ? this.childNodes.push(c) : this.childNodes.splice(i, 0, c); return c; }
  removeChild(c) { c.remove(); return c; }
  replaceChildren(...cs) { for (const c of this.childNodes) c.parentNode = null; this.childNodes = []; this.append(...cs); }
  remove() { const p = this.parentNode; if (p) { p.childNodes.splice(p.childNodes.indexOf(this), 1); this.parentNode = null; } }
  after(...cs) { const p = this.parentNode; if (!p) return; let i = p.childNodes.indexOf(this) + 1; for (let c of cs) { c = p._adopt(c); p.childNodes.splice(i++, 0, c); } }
  before(...cs) { const p = this.parentNode; if (!p) return; let i = p.childNodes.indexOf(this); for (let c of cs) { c = p._adopt(c); p.childNodes.splice(i++, 0, c); } }
  replaceWith(...cs) { this.before(...cs); this.remove(); }
  contains(n) { for (; n; n = n.parentNode) if (n === this) return true; return false; }
  get children() { return this.childNodes.filter(c => c instanceof Element); }
  get firstElementChild() { return this.children[0] ?? null; }
  get lastElementChild() { return this.children.at(-1) ?? null; }
  get firstChild() { return this.childNodes[0] ?? null; }
  *_walk() { for (const c of this.children) { yield c; yield* c._walk(); } }
  querySelectorAll(sel) { const m = compile(sel); return [...this._walk()].filter(m); }
  querySelector(sel) { const m = compile(sel); for (const e of this._walk()) if (m(e)) return e; return null; }
  getElementById(id) { for (const e of this._walk()) if (e.id === id) return e; return null; }
}
class Text extends Node {
  constructor(t) { super(); this.data = t; }
  get textContent() { return this.data; }
  set textContent(v) { this.data = String(v); }
  get nodeValue() { return this.data; }
}
const RECTS = { app: [0, 0, 400, 240] };   // the page is the top screen; everything else is measured as empty
class Element extends Node {
  constructor(tag) {
    super();
    Object.assign(this, { tagName: tag.toUpperCase(), localName: tag, id: '', className: '', hidden: false, attrs: {}, dataset: {}, style: new Style(),
      onclick: null, value: '', checked: false, disabled: false, title: '', scrollTop: 0, scrollLeft: 0, tabIndex: 0, type: '' });
    this.classList = new ClassList(this);
    // unknown methods/props become logged no-ops so DOM-bound code keeps running
    return new Proxy(this, {
      get(t, k, r) {
        if (k in t || typeof k !== 'string' || k === 'then' || k.startsWith('on') || k.startsWith('_')) return Reflect.get(t, k, r);
        if (!missing.has(k)) { missing.add(k); console.log('dom: missing ' + k); }
        return () => {};
      },
    });
  }
  get nodeName() { return this.tagName; }
  setAttribute(k, v) {
    v = String(v); this.attrs[k] = v;
    if (k === 'id') this.id = v; else if (k === 'class') this.className = v; else if (k === 'hidden') this.hidden = true;
    else if (k.startsWith('data-')) this.dataset[k.slice(5).replace(/-(\w)/g, (_, c) => c.toUpperCase())] = v;
  }
  getAttribute(k) { return k === 'id' ? this.id || null : k === 'class' ? this.className : this.attrs[k] ?? null; }
  hasAttribute(k) { return k === 'hidden' ? this.hidden : k in this.attrs || (k.startsWith('data-') && this.getAttribute(k) != null); }
  removeAttribute(k) { delete this.attrs[k]; if (k === 'hidden') this.hidden = false; }
  toggleAttribute(k, f) { const on = f ?? !this.hasAttribute(k); on ? this.setAttribute(k, '') : this.removeAttribute(k); return on; }
  get innerHTML() { return ''; }
  set innerHTML(html) { this.replaceChildren(...parseHTML(String(html))); }
  get outerHTML() { return ''; }
  matches(sel) { return compile(sel)(this); }
  closest(sel) { const m = compile(sel); for (let e = this; e instanceof Element; e = e.parentNode) if (m(e)) return e; return null; }
  getBoundingClientRect() {
    const [x, y, w, h] = RECTS[this.id] ?? (this.tagName === 'CANVAS' && this.id === 'game' ? RECTS.app : [0, 0, 0, 0]);
    return { x, y, left: x, top: y, width: w, height: h, right: x + w, bottom: y + h };
  }
  get offsetWidth() { return this.getBoundingClientRect().width; }
  get offsetHeight() { return this.getBoundingClientRect().height; }
  get clientWidth() { return this.offsetWidth; }
  get clientHeight() { return this.offsetHeight; }
  get scrollHeight() { return this.offsetHeight; }
  click() { this.dispatchEvent(new Event('click', { bubbles: true })); }
  focus() {} blur() {} scrollIntoView() {} scrollTo() {} setPointerCapture() {} releasePointerCapture() {}
  animate() { const a = { finished: Promise.resolve(), cancel() {}, onfinish: null }; setTimeout(() => a.onfinish?.(), 0); return a; }
  cloneNode(deep) {
    const c = document.createElement(this.localName);
    Object.assign(c, { id: this.id, className: this.className, hidden: this.hidden });
    Object.assign(c.attrs, this.attrs); Object.assign(c.dataset, this.dataset); Object.assign(c.style, this.style);
    if (deep) for (const n of this.childNodes) c.append(n instanceof Text ? n.data : n.cloneNode(true));
    return c;
  }
}

// selectors: comma lists of descendant chains of tag#id.class[attr][attr=v]
const selCache = new Map();
function compile(sel) {
  let f = selCache.get(sel);
  if (f) return f;
  const simple = s => {
    const parts = s.match(/(^[a-z*][\w-]*)|#[\w-]+|\.[\w-]+|\[[^\]]+\]|:not\([^)]*\)/gi) || [];
    const tests = parts.map(p => {
      if (p[0] === '#') return e => e.id === p.slice(1);
      if (p[0] === '.') return e => e.classList.contains(p.slice(1));
      if (p[0] === '[') {
        const [, k, v] = p.match(/^\[([\w-]+)(?:=["']?([^"'\]]*)["']?)?\]$/) || [];
        return v === undefined ? e => e.hasAttribute(k) : e => e.getAttribute(k) === v;
      }
      if (p.startsWith(':not(')) { const inner = simple(p.slice(5, -1)); return e => !inner(e); }
      return p === '*' ? () => true : e => e.localName === p.toLowerCase();
    });
    return e => tests.every(t => t(e));
  };
  const alts = sel.split(',').map(a => a.trim().split(/\s*>\s*|\s+/).map(simple));
  f = e => alts.some(chain => {
    if (!chain.at(-1)(e)) return false;
    let i = chain.length - 2;
    for (let p = e.parentNode; i >= 0 && p instanceof Element; p = p.parentNode) if (chain[i](p)) i--;
    return i < 0;
  });
  selCache.set(sel, f);
  return f;
}

// tiny HTML parser: enough for index.html and the few innerHTML templates
const ENT = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ' };
const unent = s => s.replace(/&(#\d+|\w+);/g, (m, e) => e[0] === '#' ? String.fromCharCode(+e.slice(1)) : ENT[e] ?? m);
function parseHTML(html) {
  const root = new Element('template'), stack = [root];
  const re = /<!--[\s\S]*?-->|<\/([\w-]+)\s*>|<([\w-]+)((?:\s+[\w-:]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))?)*)\s*\/?>|([^<]+)/g;
  for (let m; (m = re.exec(html));) {
    const top = stack.at(-1);
    if (m[1]) { const i = stack.findLastIndex(e => e.localName === m[1].toLowerCase()); if (i > 0) stack.length = i; }
    else if (m[2]) {
      const el = document.createElement(m[2].toLowerCase());
      for (const a of m[3].matchAll(/([\w-:]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g)) el.setAttribute(a[1], unent(a[2] ?? a[3] ?? a[4] ?? ''));
      top.append(el);
      if (!VOID.has(el.localName) && !m[0].endsWith('/>')) stack.push(el);
    } else if (m[4] && m[4].trim()) top.append(unent(m[4].replace(/\s+/g, ' ')));
  }
  return root.childNodes.slice();
}

// ---------- canvas ----------
const colorCache = new Map();
const NAMED = { black: '#000', white: '#fff', red: '#f00', cyan: '#0ff', yellow: '#ff0', lime: '#0f0', blue: '#00f', magenta: '#f0f', gray: '#808080', grey: '#808080', transparent: 'rgba(0,0,0,0)' };
function parseColor(s) {
  s = String(s).trim().toLowerCase();
  let c = colorCache.get(s);
  if (c !== undefined) return c;
  const v = NAMED[s] ?? s;
  let r = 0, gg = 0, b = 0, a = 255, m;
  if (v[0] === '#') {
    const h = v.slice(1), x = h.length <= 4 ? [...h].map(d => parseInt(d + d, 16)) : h.match(/../g).map(d => parseInt(d, 16));
    [r, gg, b] = x; if (x[3] !== undefined) a = x[3];
  } else if ((m = v.match(/^rgba?\(([^)]+)\)/))) {
    const p = m[1].split(/[\s,\/]+/).filter(Boolean).map(parseFloat);
    [r, gg, b] = p; if (p[3] !== undefined) a = Math.round(p[3] * 255);
  } else if ((m = v.match(/^hsla?\(([^)]+)\)/))) {
    const [hh, ss, ll, aa] = m[1].split(/[\s,\/]+/).filter(Boolean).map(parseFloat);
    const S = ss / 100, L = ll / 100, k = n => (n + hh / 30) % 12, A = S * Math.min(L, 1 - L);
    const f = n => Math.round(255 * (L - A * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1))));
    [r, gg, b] = [f(0), f(8), f(4)]; if (aa !== undefined) a = Math.round(aa * 255);
  }
  c = ((r & 255) << 24 | (gg & 255) << 16 | (b & 255) << 8 | (a & 255)) >>> 0;
  colorCache.set(s, c);
  return c;
}
const OPS = { 'source-over': 0, lighter: 1, multiply: 2, 'source-atop': 3, 'destination-in': 4 };

class CanvasGradient {
  constructor(args) { this.args = args; this.stops = []; }
  addColorStop(o, col) { this.stops.push([o, parseColor(col)]); this._buf = null; }
  get buf() {
    if (this._buf) return this._buf;
    const s = this.stops.sort((a, b) => a[0] - b[0]);
    const f = new Float64Array(6 + s.length * 5);
    f.set(this.args);
    s.forEach(([o, c], i) => f.set([o, c >>> 24, c >> 16 & 255, c >> 8 & 255, c & 255], 6 + i * 5));
    return (this._buf = f);
  }
}
class ImageData {
  constructor(a, w, h) {
    if (typeof a === 'number') { h = w; w = a; a = new Uint8ClampedArray(w * h * 4); }
    this.data = a; this.width = w; this.height = h;
  }
}
g.ImageData = ImageData;

const srcOf = img => img?._px ? img : img?.canvas?._px ? img.canvas : null;
class Context2D {
  constructor(canvas) {
    this.canvas = canvas;
    this._s = { m: [1, 0, 0, 1, 0, 0], globalAlpha: 1, op: 0, opName: 'source-over', fillStyle: '#000', strokeStyle: '#000', lineWidth: 1, font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', imageSmoothingEnabled: true };
    this._stack = []; this._path = [];
  }
  get globalAlpha() { return this._s.globalAlpha; } set globalAlpha(v) { if (v >= 0 && v <= 1) this._s.globalAlpha = +v; }
  get globalCompositeOperation() { return this._s.opName; }
  set globalCompositeOperation(v) { if (v in OPS) { this._s.op = OPS[v]; this._s.opName = v; } }
  get fillStyle() { return this._s.fillStyle; } set fillStyle(v) { this._s.fillStyle = v; }
  get strokeStyle() { return this._s.strokeStyle; } set strokeStyle(v) { this._s.strokeStyle = v; }
  get lineWidth() { return this._s.lineWidth; } set lineWidth(v) { this._s.lineWidth = v; }
  get font() { return this._s.font; } set font(v) { this._s.font = v; }
  get textAlign() { return this._s.textAlign; } set textAlign(v) { this._s.textAlign = v; }
  get textBaseline() { return this._s.textBaseline; } set textBaseline(v) { this._s.textBaseline = v; }
  get imageSmoothingEnabled() { return this._s.imageSmoothingEnabled; } set imageSmoothingEnabled(v) { this._s.imageSmoothingEnabled = !!v; }
  save() { this._stack.push({ ...this._s, m: this._s.m.slice() }); }
  restore() { const s = this._stack.pop(); if (s) this._s = s; }
  setTransform(a, b, c, d, e, f) {
    if (typeof a === 'object' && a) ({ a, b, c, d, e, f } = a);
    this._s.m = a === undefined ? [1, 0, 0, 1, 0, 0] : [a, b, c, d, e, f];
  }
  resetTransform() { this._s.m = [1, 0, 0, 1, 0, 0]; }
  getTransform() { const [a, b, c, d, e, f] = this._s.m; return { a, b, c, d, e, f }; }
  transform(a2, b2, c2, d2, e2, f2) {
    const [a, b, c, d, e, f] = this._s.m;
    this._s.m = [a * a2 + c * b2, b * a2 + d * b2, a * c2 + c * d2, b * c2 + d * d2, a * e2 + c * f2 + e, b * e2 + d * f2 + f];
  }
  translate(x, y) { const m = this._s.m; m[4] += m[0] * x + m[2] * y; m[5] += m[1] * x + m[3] * y; }
  scale(x, y) { const m = this._s.m; m[0] *= x; m[1] *= x; m[2] *= y; m[3] *= y; }
  rotate(t) { const c = Math.cos(t), s = Math.sin(t); this.transform(c, s, -s, c, 0, 0); }
  _paint(style) {
    if (typeof style === 'string') return parseColor(style);
    if (style instanceof CanvasGradient) return style.buf;
    return style?.data ? style : 0;
  }
  _fill(x, y, w, h, style) {
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    const c = this.canvas;
    N.fill(c._px.buffer, c.width, c.height, x, y, w, h, this._s.m, this._s.globalAlpha, this._s.op, this._paint(style));
  }
  fillRect(x, y, w, h) { this._fill(x, y, w, h, this._s.fillStyle); }
  clearRect(x, y, w, h) {
    const c = this.canvas;
    N.fill(c._px.buffer, c.width, c.height, x, y, w, h, this._s.m, 1, 5, 0);
  }
  strokeRect(x, y, w, h) {
    const l = this._s.lineWidth, s = this._s.strokeStyle, k = l / 2;
    this._fill(x - k, y - k, w + l, l, s); this._fill(x - k, y + h - k, w + l, l, s);
    this._fill(x - k, y + k, l, h - l, s); this._fill(x + w - k, y + k, l, h - l, s);
  }
  drawImage(img, ...a) {
    const src = srcOf(img);
    if (!src || !src.width || !src.height) return;
    let sx = 0, sy = 0, sw = src.width, sh = src.height, dx, dy, dw, dh;
    if (a.length === 2) [dx, dy] = a, dw = sw, dh = sh;
    else if (a.length === 4) [dx, dy, dw, dh] = a;
    else [sx, sy, sw, sh, dx, dy, dw, dh] = a;
    const c = this.canvas;
    N.blit(c._px.buffer, c.width, c.height, src._px.buffer, src.width, src.height, sx, sy, sw, sh, dx, dy, dw, dh, this._s.m, this._s.globalAlpha, this._s.op);
  }
  getImageData(x, y, w, h) {
    const c = this.canvas, out = new ImageData(w, h), W = c.width;
    for (let r = 0; r < h; r++) {
      const sy = y + r; if (sy < 0 || sy >= c.height) continue;
      const x0 = Math.max(0, x), x1 = Math.min(W, x + w);
      if (x1 > x0) out.data.set(c._px.subarray((sy * W + x0) * 4, (sy * W + x1) * 4), (r * w + x0 - x) * 4);
    }
    return out;
  }
  putImageData(d, x, y) {
    const c = this.canvas, W = c.width;
    for (let r = 0; r < d.height; r++) {
      const ty = y + r; if (ty < 0 || ty >= c.height) continue;
      const x0 = Math.max(0, x), x1 = Math.min(W, x + d.width);
      if (x1 > x0) c._px.set(d.data.subarray((r * d.width + x0 - x) * 4, (r * d.width + x1 - x) * 4), (ty * W + x0) * 4);
    }
  }
  createImageData(w, h) { return typeof w === 'object' ? new ImageData(w.width, w.height) : new ImageData(w, h); }
  createRadialGradient(x0, y0, r0, x1, y1, r1) { return new CanvasGradient([x0, y0, r0, x1, y1, r1]); }
  createLinearGradient(x0, y0, x1, y1) { return new CanvasGradient([x0, y0, 0, x0, y0, Math.hypot(x1 - x0, y1 - y0)]); }   // ponytail: unused by the game, radial stand-in
  createPattern(img) { const s = srcOf(img); return s ? { data: s._px.buffer, width: s.width, height: s.height } : null; }
  // paths: the game only draws rectangles and straight 1px lines
  beginPath() { this._path = []; }
  closePath() {}
  moveTo(x, y) { this._path.push({ move: true, x, y }); }
  lineTo(x, y) { this._path.push({ x, y }); }
  rect(x, y, w, h) { this._path.push({ rect: [x, y, w, h] }); }
  fill() { for (const p of this._path) if (p.rect) this._fill(...p.rect, this._s.fillStyle); }
  stroke() {
    const l = this._s.lineWidth, s = this._s.strokeStyle;
    let prev = null;
    for (const p of this._path) {
      if (p.rect) { const [x, y, w, h] = p.rect; this.save(); this.strokeRect(x, y, w, h); this.restore(); prev = null; continue; }
      if (!p.move && prev) {
        const x0 = Math.min(prev.x, p.x), y0 = Math.min(prev.y, p.y);
        if (prev.x === p.x) this._fill(p.x - l / 2, y0, l, Math.abs(p.y - prev.y), s);
        else if (prev.y === p.y) this._fill(x0, p.y - l / 2, Math.abs(p.x - prev.x), l, s);
        else { const n = Math.ceil(Math.hypot(p.x - prev.x, p.y - prev.y)); for (let i = 0; i <= n; i++) this._fill(prev.x + (p.x - prev.x) * i / n - l / 2, prev.y + (p.y - prev.y) * i / n - l / 2, l, l, s); }
      }
      prev = p;
    }
  }
  clip() {}
  // text: 5x7-ish blocks until the bitmap font lands (only the title logo uses canvas text)
  measureText(t) { const px = parseFloat(this._s.font.match(/(\d+(?:\.\d+)?)px/)?.[1] ?? 10); return { width: String(t).length * px * 0.6 }; }
  fillText(t, x, y) {
    const px = parseFloat(this._s.font.match(/(\d+(?:\.\d+)?)px/)?.[1] ?? 10), w = px * 0.6;
    for (let i = 0; i < t.length; i++) if (t[i] !== ' ') this._fill(x + i * w + w * 0.1, y + px * 0.15, w * 0.8, px * 0.75, this._s.fillStyle);
  }
}

class HTMLCanvasElement extends Element {
  constructor() { super('canvas'); this._w = 300; this._h = 150; this._px = new Uint8Array(300 * 150 * 4); this._ctx = null; }
  get width() { return this._w; } set width(v) { this._w = Math.max(0, v | 0); this._px = new Uint8Array(this._w * this._h * 4); this._ctx && (this._ctx._s = new Context2D(this)._s); }
  get height() { return this._h; } set height(v) { this._h = Math.max(0, v | 0); this._px = new Uint8Array(this._w * this._h * 4); this._ctx && (this._ctx._s = new Context2D(this)._s); }
  getContext() { return (this._ctx ??= new Context2D(this)); }
  toDataURL() { return ''; }
  toBlob(cb) { cb?.(null); }
}

class Image extends EventTarget {
  constructor(w, h) { super(); this.width = w || 0; this.height = h || 0; this._px = null; this.complete = false; this.onload = this.onerror = null; this._src = ''; }
  get naturalWidth() { return this.width; } get naturalHeight() { return this.height; }
  get src() { return this._src; }
  set src(url) {
    this._src = url;
    const d = N.readFile(romPath(url)), img = d && N.decodePNG(d);
    setTimeout(() => {
      if (!img) { console.warn('img: ' + url); this.onerror?.(new Event('error')); return; }
      this.width = img.width; this.height = img.height; this._px = new Uint8Array(img.data); this.complete = true;
      this.onload?.(new Event('load'));
    }, 0);
  }
  decode() { return new Promise((ok, no) => { if (this.complete) ok(); else { this.onload = ok; this.onerror = no; } }); }
}
g.Image = Image;
g.HTMLCanvasElement = HTMLCanvasElement;
g.HTMLElement = g.Element = Element;
g.Node = Node;

// ---------- document ----------
class Document extends Node {
  constructor() {
    super();
    this.hidden = false; this.visibilityState = 'visible';
    this.fonts = { ready: Promise.resolve(), load: () => Promise.resolve([]), check: () => true };
  }
  createElement(tag) { tag = tag.toLowerCase(); return tag === 'canvas' ? new HTMLCanvasElement() : new Element(tag); }
  createElementNS(ns, tag) { return this.createElement(tag); }
  createTextNode(t) { return new Text(String(t)); }
  createDocumentFragment() { return new Element('fragment'); }
  get body() { return this.documentElement.querySelector('body'); }
  get head() { return this.documentElement.querySelector('head'); }
  get activeElement() { return this.body; }
}
const document = g.document = new Document();
const romPath = url => 'romfs:/' + decodeURI(String(url)).replace(/^\.?\//, '').replace(/[?#].*$/, '');
{
  const html = new TextDecoder().decode(N.readFile('romfs:/index.html'));
  const [root] = parseHTML(html.replace(/<!DOCTYPE[^>]*>/i, '').replace(/<script[\s\S]*?<\/script>/gi, ''));
  root.parentNode = document;
  document.childNodes = [root];
  document.documentElement = root;
}

// ---------- the rest of the window ----------
g.location = { search: '', hash: '', pathname: '/', href: 'romfs:/index.html', origin: 'romfs:', reload() {} };
g.history = { replaceState() {}, pushState() {} };
g.navigator = { userAgent: 'PocketFarm3DS', language: 'pt-BR', maxTouchPoints: 1, vibrate() {}, clipboard: { writeText: () => Promise.reject(new Error('sem clipboard')) } };
g.screen = { orientation: { type: 'landscape-primary', lock: () => Promise.reject(new Error('no')) } };
g.innerWidth = 400; g.innerHeight = 240; g.devicePixelRatio = 1;
g.matchMedia = q => ({ matches: false, media: q, addEventListener() {}, removeEventListener() {} });
g.getComputedStyle = el => el.style;
g.URLSearchParams ??= class { constructor(s = '') { this.m = new Map(String(s).replace(/^\?/, '').split('&').filter(Boolean).map(p => p.split('=').map(decodeURIComponent))); } get(k) { return this.m.get(k) ?? null; } has(k) { return this.m.has(k); } };
g.URL ??= { createObjectURL: () => '', revokeObjectURL() {} };
g.Blob ??= class { constructor(parts) { this.parts = parts; } };

g.fetch = async url => {
  const d = N.readFile(romPath(url));
  if (!d) return { ok: false, status: 404, text: async () => '', json: async () => { throw new Error('404 ' + url); } };
  const text = () => new TextDecoder().decode(d);
  return { ok: true, status: 200, text: async () => text(), json: async () => JSON.parse(text()), arrayBuffer: async () => d };
};

// localStorage: one JSON file on the SD card
const STORE = 'sdmc:/3ds/PocketFarm/storage.json';
const store = (() => { try { return JSON.parse(new TextDecoder().decode(N.readFile(STORE))) || {}; } catch { return {}; } })();
const flush = () => N.writeFile(STORE, JSON.stringify(store));
g.localStorage = {
  getItem: k => (k in store ? store[k] : null),
  setItem: (k, v) => { store[k] = String(v); flush(); },
  removeItem: k => { delete store[k]; flush(); },
  clear: () => { for (const k in store) delete store[k]; flush(); },
  key: i => Object.keys(store)[i] ?? null,
  get length() { return Object.keys(store).length; },
};

// Web Audio: silent stand-in until the ndsp synth lands; every call chains and returns harmlessly
const hush = new Proxy(function () {}, {
  get: (t, k) => k === 'then' ? undefined : k === Symbol.toPrimitive ? () => 0 : k === 'currentTime' ? N.now() / 1000 : k === 'state' ? 'running' : hush,
  apply: () => hush, construct: () => hush, set: () => true,
});
g.AudioContext = hush;

console.log('shim ready');

// ---------- the frame, called by main.c every vblank ----------
const KEY = { A: 1, B: 2, SELECT: 4, START: 8, DRIGHT: 16, DLEFT: 32, DUP: 64, DDOWN: 128, R: 256, L: 512, X: 1024, Y: 2048, CRIGHT: 1 << 28, CLEFT: 1 << 29, CUP: 1 << 30, CDOWN: 1 << 31 };
const MAP = [   // 3DS button -> the PC keyboard key the game already reads (input.js)
  [KEY.DUP | KEY.CUP, 'ArrowUp'], [KEY.DDOWN | KEY.CDOWN, 'ArrowDown'], [KEY.DLEFT | KEY.CLEFT, 'ArrowLeft'], [KEY.DRIGHT | KEY.CRIGHT, 'ArrowRight'],
  [KEY.A, 'KeyZ'], [KEY.B, 'KeyX'], [KEY.START, 'Escape'],
  [KEY.Y, 'Digit1'], [KEY.X, 'Digit2'], [KEY.L, 'Digit3'], [KEY.R, 'Digit4'],
];
let wasHeld = 0, frameNo = 0, lastT = 0;
const DEBUG = !!N.readFile('sdmc:/3ds/PocketFarm/debug');   // touch this file on the SD for heartbeat logs + screenshots
const key = (type, code) => g.dispatchEvent(new Event(type, { code, key: code, repeat: false, target: document.body }));

g.__frame = (t, down, up, held) => {
  now = t;
  // directions are combined (d-pad OR circle pad), so derive edges from the combined mask
  for (const [mask, code] of MAP) {
    const on = !!(held & mask), was = !!(wasHeld & mask);
    if (on && !was) key('keydown', code);
    else if (!on && was) key('keyup', code);
  }
  wasHeld = held;
  for (const [id, tm] of [...timers]) {
    if (tm.at > now || !timers.has(id)) continue;
    if (tm.every) tm.at += Math.max(tm.every, Math.ceil((now - tm.at) / tm.every) * tm.every); else timers.delete(id);
    safe(tm.fn, ...tm.args);
  }
  const q = rafQ; rafQ = [];
  for (const r of q) safe(r.fn, now);
  const game = document.getElementById('game');
  if (game) N.present(game._px.buffer, game.width, game.height, 0, 0x2b3a2a);
  if (DEBUG) {
    frameNo++;
    const ms = N.now() - t;
    if (frameNo <= 5 || frameNo % 60 === 0) console.log(`frame ${frameNo}: ${ms.toFixed(1)}ms (gap ${(t - lastT).toFixed(1)}ms)`);
    if (frameNo % 300 === 0 && game) N.savePPM(game._px.buffer, game.width, game.height, 'sdmc:/3ds/PocketFarm/shot.ppm');
  }
  lastT = t;
};
g.__quit = () => g.dispatchEvent(new Event('pagehide'));
