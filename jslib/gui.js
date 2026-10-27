// gui: a classic-era desktop toolkit for Kiwi, written in JavaScript.
//
// Drawing goes through the native 'gfx' module (framebuffer, 3D bevels,
// VGA font).  This file has the window manager (Desktop), windows, and
// widgets: Label Button CheckBox TextField ListView TextArea Tabs
// Progress Graph Canvas.
//
//   const d = new Desktop();
//   d.register({ id: 'hello', title: 'Hello', icon: 'info', create: (d) => new Window({...}) });
//   d.run();
import * as gfx from 'gfx';
import * as sys from 'sys';

export const FONT_W = gfx.FONT_W, FONT_H = gfx.FONT_H;
export const C = {
    desktop: gfx.DESKTOP, face: gfx.FACE, hilight: gfx.HILIGHT, light: 0xDFDFDF,
    shadow: gfx.SHADOW, dark: gfx.DKSHADOW, text: gfx.TEXT, select: gfx.SELECT,
    selectText: gfx.SELECT_TEXT, field: gfx.FIELD, disabled: gfx.DISABLED, title: gfx.TITLE,
    black: 0x000000, white: 0xFFFFFF, navy: 0x000080, teal: 0x008080, green: 0x00FF00,
    dkgreen: 0x008000, red: 0xFF0000, maroon: 0x800000, yellow: 0xFFFF00, blue: 0x0000FF,
    gray: 0x808080, silver: 0xC0C0C0, purple: 0x800080, olive: 0x808000,
};
export const KEY = {
    ESC: gfx.KEY_ESC, BACKSPACE: gfx.KEY_BACKSPACE, TAB: gfx.KEY_TAB, ENTER: gfx.KEY_ENTER,
    UP: gfx.KEY_UP, DOWN: gfx.KEY_DOWN, LEFT: gfx.KEY_LEFT, RIGHT: gfx.KEY_RIGHT,
    HOME: gfx.KEY_HOME, END: gfx.KEY_END, PGUP: gfx.KEY_PGUP, PGDN: gfx.KEY_PGDN,
    DELETE: gfx.KEY_DELETE, F1: gfx.KEY_F1, F4: gfx.KEY_F1 + 3,
};

// ------------------------------------------------------------------ geometry
export function intersect(a, b) {
    const x = Math.max(a.x, b.x), y = Math.max(a.y, b.y);
    const x1 = Math.min(a.x + a.w, b.x + b.w), y1 = Math.min(a.y + a.h, b.y + b.h);
    return { x, y, w: Math.max(0, x1 - x), h: Math.max(0, y1 - y) };
}

export function union(a, b) {
    if (!a) return { ...b };
    const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y);
    return { x, y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y };
}

const inside = (r, x, y) => x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;

// ------------------------------------------------------------------ graphics context
// Origin + clip rectangle on top of the native, global gfx clip.
let activeG = null;

export class G {
    constructor(ox, oy, clip) {
        this.ox = ox;
        this.oy = oy;
        this.clipRect = clip;
    }
    use() {
        if (activeG !== this) {
            const c = this.clipRect;
            gfx.clip(c.x, c.y, c.w, c.h);
            activeG = this;
        }
        return this;
    }
    sub(x, y, w, h) {
        return new G(this.ox + x, this.oy + y, intersect(this.clipRect, { x: this.ox + x, y: this.oy + y, w, h }));
    }
    get empty() { return this.clipRect.w <= 0 || this.clipRect.h <= 0; }
    fill(x, y, w, h, c) { this.use(); gfx.fill(this.ox + x, this.oy + y, w, h, c); }
    frame(x, y, w, h, c) { this.use(); gfx.frame(this.ox + x, this.oy + y, w, h, c); }
    line(x0, y0, x1, y1, c) { this.use(); gfx.line(this.ox + x0, this.oy + y0, this.ox + x1, this.oy + y1, c); }
    pixel(x, y, c) { this.use(); gfx.pixel(this.ox + x, this.oy + y, c); }
    text(x, y, s, fg = C.text, bg = -1, flags = 0) { this.use(); gfx.text(this.ox + x, this.oy + y, String(s), fg, bg, flags); }
    gradient(x, y, w, h, a, b) { this.use(); gfx.gradient(this.ox + x, this.oy + y, w, h, a, b); }
    dither(x, y, w, h, a, b) { this.use(); gfx.dither(this.ox + x, this.oy + y, w, h, a, b); }
    bevel(x, y, w, h, style) { this.use(); gfx.bevel(this.ox + x, this.oy + y, w, h, style); }
    glyph(x, y, gl, c = C.text) { this.use(); gfx.glyph(this.ox + x, this.oy + y, gl, c); }
    button(x, y, w, h, label, pressed = false, focused = false) { this.use(); gfx.button(this.ox + x, this.oy + y, w, h, label, pressed, focused); }
    field(x, y, w, h) { this.use(); gfx.field(this.ox + x, this.oy + y, w, h); }
    checkbox(x, y, label, checked) { this.use(); gfx.checkbox(this.ox + x, this.oy + y, label, checked); }
    scrollbar(x, y, len, vertical, total, visible, pos) { this.use(); gfx.scrollbar(this.ox + x, this.oy + y, len, vertical, total, visible, pos); }
    progress(x, y, w, h, frac) { this.use(); gfx.progress(this.ox + x, this.oy + y, w, h, Math.round(frac * 1000)); }
    groupbox(x, y, w, h, label) { this.use(); gfx.groupbox(this.ox + x, this.oy + y, w, h, label); }
    bitmap(x, y, w, h, art, pal) { this.use(); gfx.bitmap(this.ox + x, this.oy + y, w, h, art, pal); }
    window(x, y, w, h, title, active) { this.use(); gfx.window(this.ox + x, this.oy + y, w, h, title, active); }
    circle(cx, cy, r, c) {
        for (let dy = -r; dy <= r; dy++) {
            const w = Math.floor(Math.sqrt(r * r - dy * dy));
            this.fill(cx - w, cy + dy, 2 * w + 1, 1, c);
        }
    }
    textCenter(x, y, w, s, fg = C.text, flags = 0) {
        this.text(x + Math.floor((w - gfx.textWidth(String(s))) / 2), y, s, fg, -1, flags);
    }
}

export const textWidth = (s) => gfx.textWidth(String(s));

// ------------------------------------------------------------------ icons
// Drawn with primitives on a 32x32 grid, scaled for 16px use.
export function drawIcon(g, name, x, y, size = 32) {
    const k = size / 32;
    const s = g.sub(x, y, size, size);
    const f = (a, b, w, h, c) => s.fill(Math.round(a * k), Math.round(b * k), Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)), c);
    const r = (a, b, w, h, c) => s.frame(Math.round(a * k), Math.round(b * k), Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)), c);
    const circ = (cx, cy, rr, c) => s.circle(Math.round(cx * k), Math.round(cy * k), Math.max(1, Math.round(rr * k)), c);
    switch (name) {
        case 'computer':
            f(4, 2, 24, 19, C.face); r(4, 2, 24, 19, C.black); f(7, 5, 18, 13, C.teal); f(8, 6, 6, 2, C.white);
            f(12, 21, 8, 3, C.shadow); f(3, 24, 26, 6, C.face); r(3, 24, 26, 6, C.black);
            for (let i = 0; i < 5; i++) f(5 + i * 5, 26, 3, 1, C.shadow);
            break;
        case 'chip':
            f(8, 8, 16, 16, C.black);
            for (let i = 0; i < 4; i++) { f(4, 10 + i * 4, 4, 2, C.silver); f(24, 10 + i * 4, 4, 2, C.silver); f(10 + i * 4, 4, 2, 4, C.silver); f(10 + i * 4, 24, 2, 4, C.silver); }
            f(11, 11, 10, 10, 0x404040); f(13, 13, 6, 6, C.green);
            break;
        case 'folder':
            f(3, 6, 11, 4, 0xC0C000); r(3, 6, 11, 4, C.black); f(3, 9, 26, 18, C.yellow); r(3, 9, 26, 18, C.black);
            f(4, 10, 24, 2, 0xFFFFC0);
            break;
        case 'file':
            f(7, 2, 18, 28, C.white); r(7, 2, 18, 28, C.black); f(19, 2, 6, 6, C.silver); r(19, 2, 6, 6, C.black);
            for (let i = 0; i < 6; i++) f(10, 11 + i * 3, 12 - (i % 3) * 2, 1, C.gray);
            break;
        case 'notepad':
            f(6, 4, 20, 26, C.white); r(6, 4, 20, 26, C.black); f(6, 4, 20, 4, C.blue);
            for (let i = 0; i < 6; i++) f(9, 11 + i * 3, 14, 1, C.navy);
            for (let i = 0; i < 4; i++) f(9 + i * 5, 2, 2, 4, C.gray);
            break;
        case 'chart':
            f(3, 3, 26, 22, C.face); r(3, 3, 26, 22, C.black); f(6, 6, 20, 16, C.black);
            for (let i = 0; i < 4; i++) f(6, 9 + i * 4, 20, 1, C.dkgreen);
            s.line(Math.round(7 * k), Math.round(19 * k), Math.round(12 * k), Math.round(12 * k), C.green);
            s.line(Math.round(12 * k), Math.round(12 * k), Math.round(17 * k), Math.round(16 * k), C.green);
            s.line(Math.round(17 * k), Math.round(16 * k), Math.round(24 * k), Math.round(8 * k), C.green);
            f(10, 25, 12, 3, C.shadow); f(6, 28, 20, 2, C.face);
            break;
        case 'calc':
            f(7, 2, 18, 28, C.face); r(7, 2, 18, 28, C.black); f(9, 4, 14, 6, 0x90B090); r(9, 4, 14, 6, C.shadow);
            for (let j = 0; j < 4; j++) for (let i = 0; i < 3; i++) f(9 + i * 5, 12 + j * 4, 4, 3, i === 2 && j === 3 ? C.red : C.white);
            break;
        case 'run':
            f(3, 5, 26, 22, C.black); r(3, 5, 26, 22, C.face); f(4, 6, 24, 4, C.navy);
            f(6, 13, 2, 2, C.silver); f(8, 15, 2, 2, C.silver); f(6, 17, 2, 2, C.silver); f(12, 18, 7, 2, C.silver);
            break;
        case 'info':
            circ(16, 16, 13, C.navy); circ(16, 16, 11, C.blue); f(14, 13, 4, 11, C.white); f(14, 7, 4, 4, C.white);
            break;
        case 'power':
            circ(16, 16, 13, C.maroon); circ(16, 16, 10, C.red); circ(16, 17, 6, C.white); circ(16, 17, 4, C.red);
            f(15, 6, 3, 10, C.white);
            break;
        case 'kiwi':
            circ(16, 16, 14, 0x6B4423); circ(16, 16, 12, 0x6CB33F); circ(16, 16, 7, 0x9ED36A); circ(16, 16, 3, 0xF0F5D8);
            for (let i = 0; i < 10; i++) {
                const a = i * Math.PI / 5;
                f(16 + Math.cos(a) * 5.5 - 0.5, 16 + Math.sin(a) * 5.5 - 0.5, 1.5, 1.5, C.black);
            }
            break;
        case 'memory':
            f(2, 10, 28, 12, C.dkgreen); r(2, 10, 28, 12, C.black);
            for (let i = 0; i < 4; i++) f(4 + i * 7, 12, 5, 6, C.black);
            for (let i = 0; i < 9; i++) f(3 + i * 3, 22, 2, 3, 0xD4AF37);
            break;
        default:
            f(6, 6, 20, 20, C.face); r(6, 6, 20, 20, C.black);
    }
}

// ------------------------------------------------------------------ widgets
export class Widget {
    constructor(x = 0, y = 0, w = 0, h = 0) {
        this.x = x; this.y = y; this.w = w; this.h = h;
        this.visible = true;
        this.focusable = false;
        this.window = null;
    }
    contains(px, py) { return this.visible && px >= this.x && py >= this.y && px < this.x + this.w && py < this.y + this.h; }
    move(x, y, w = this.w, h = this.h) { Object.assign(this, { x, y, w, h }); return this; }
    get focused() { return this.window?.focus === this; }
    invalidate() { this.window?.invalidate(); }
    paint(g) {}
    mouseDown(x, y, ev) {}
    mouseMove(x, y, ev) {}
    mouseUp(x, y, ev) {}
    key(ev) { return false; }
}

export class Label extends Widget {
    constructor(x, y, text, { color = C.text, bold = false, bg = -1, maxW = Infinity } = {}) {
        super(x, y, 0, FONT_H);
        Object.assign(this, { text, color, bold, bg, maxW });
        this.fit();
    }
    fit() {
        const lines = String(this.text).split('\n');
        this.w = Math.min(this.maxW, Math.max(...lines.map(textWidth)));
        this.h = lines.length * FONT_H;
    }
    set(text) { this.text = text; this.fit(); this.invalidate(); }
    paint(g) {
        String(this.text).split('\n').forEach((l, i) => g.text(0, i * FONT_H, l, this.color, this.bg, this.bold ? gfx.BOLD : 0));
    }
}

export class Button extends Widget {
    constructor(x, y, w, h, label, onClick) {
        super(x, y, w, h);
        Object.assign(this, { label, onClick, pressed: false, armed: false, focusable: true });
    }
    paint(g) { g.button(0, 0, this.w, this.h, this.label, this.pressed, this.focused); }
    mouseDown() { this.pressed = this.armed = true; this.invalidate(); }
    mouseMove(x, y) {
        const p = this.armed && x >= 0 && y >= 0 && x < this.w && y < this.h;
        if (p !== this.pressed) { this.pressed = p; this.invalidate(); }
    }
    mouseUp(x, y) {
        const hit = this.pressed;
        this.pressed = this.armed = false;
        this.invalidate();
        if (hit) this.onClick?.(this);
    }
    key(ev) {
        if (ev.code === KEY.ENTER || ev.ch === ' ') { this.onClick?.(this); return true; }
        return false;
    }
}

export class CheckBox extends Widget {
    constructor(x, y, label, checked = false, onChange) {
        super(x, y, 19 + textWidth(label), 16);
        Object.assign(this, { label, checked, onChange, focusable: true });
    }
    paint(g) { g.checkbox(0, 0, this.label, this.checked); }
    mouseUp(x, y) {
        if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.toggle();
    }
    toggle() { this.checked = !this.checked; this.invalidate(); this.onChange?.(this.checked); }
    key(ev) { if (ev.ch === ' ') { this.toggle(); return true; } return false; }
}

export class TextField extends Widget {
    constructor(x, y, w, text = '', { onEnter, onChange } = {}) {
        super(x, y, w, 22);
        Object.assign(this, { text, pos: text.length, scroll: 0, onEnter, onChange, focusable: true });
    }
    set value(t) { this.text = t; this.pos = t.length; this.invalidate(); }
    get value() { return this.text; }
    paint(g) {
        g.field(0, 0, this.w, this.h);
        const cols = Math.floor((this.w - 8) / FONT_W);
        if (this.pos < this.scroll) this.scroll = this.pos;
        if (this.pos > this.scroll + cols) this.scroll = this.pos - cols;
        const inner = g.sub(3, 2, this.w - 6, this.h - 4);
        inner.text(1, 1, this.text.slice(this.scroll, this.scroll + cols + 1));
        if (this.focused) inner.fill(1 + (this.pos - this.scroll) * FONT_W, 1, 1, FONT_H, C.text);
    }
    mouseDown(x) {
        this.pos = Math.max(0, Math.min(this.text.length, this.scroll + Math.round((x - 4) / FONT_W)));
        this.invalidate();
    }
    key(ev) {
        const t = this.text, p = this.pos;
        switch (ev.code) {
            case KEY.LEFT: this.pos = Math.max(0, p - 1); break;
            case KEY.RIGHT: this.pos = Math.min(t.length, p + 1); break;
            case KEY.HOME: this.pos = 0; break;
            case KEY.END: this.pos = t.length; break;
            case KEY.BACKSPACE: if (p > 0) { this.text = t.slice(0, p - 1) + t.slice(p); this.pos--; this.onChange?.(this.text); } break;
            case KEY.DELETE: this.text = t.slice(0, p) + t.slice(p + 1); this.onChange?.(this.text); break;
            case KEY.ENTER: this.onEnter?.(this.text); break;
            default:
                if (ev.ch && ev.ch >= ' ' && !(ev.mods & gfx.MOD_CTRL)) {
                    this.text = t.slice(0, p) + ev.ch + t.slice(p);
                    this.pos++;
                    this.onChange?.(this.text);
                } else return false;
        }
        this.invalidate();
        return true;
    }
}

// shared scroll bar arithmetic (mirrors ui_scrollbar in lib/gfxui.c)
function scrollHit(len, total, visible, pos, p) {
    const b = gfx.SCROLL_W;
    if (p < b) return { part: 'up' };
    if (p >= len - b) return { part: 'down' };
    const track = len - 2 * b;
    if (total <= visible) return { part: 'none' };
    const th = Math.max(8, Math.floor(track * visible / total));
    const maxpos = total - visible;
    const off = Math.floor((track - th) * Math.min(pos, maxpos) / maxpos);
    if (p < b + off) return { part: 'pageup' };
    if (p >= b + off + th) return { part: 'pagedown' };
    return { part: 'thumb', grab: p - b - off, track, th, maxpos };
}

export class ListView extends Widget {
    constructor(x, y, w, h, { columns = [{ title: '', w: 200 }], rows = [], onSelect, onActivate, header = true } = {}) {
        super(x, y, w, h);
        Object.assign(this, { columns, rows, onSelect, onActivate, header, selected: -1, top: 0, focusable: true, rowH: 18 });
    }
    setRows(rows, keepSelection = true) {
        this.rows = rows;
        if (!keepSelection || this.selected >= rows.length) this.selected = rows.length ? Math.min(this.selected, rows.length - 1) : -1;
        this.top = Math.max(0, Math.min(this.top, rows.length - this.pageSize));
        this.invalidate();
    }
    get headerH() { return this.header ? 20 : 0; }
    get pageSize() { return Math.max(1, Math.floor((this.h - 4 - this.headerH) / this.rowH)); }
    get needScroll() { return this.rows.length > this.pageSize; }
    get selectedRow() { return this.rows[this.selected]; }
    select(i) {
        if (!this.rows.length) return;
        i = Math.max(0, Math.min(this.rows.length - 1, i));
        this.selected = i;
        if (i < this.top) this.top = i;
        if (i >= this.top + this.pageSize) this.top = i - this.pageSize + 1;
        this.invalidate();
        this.onSelect?.(this.rows[i], i);
    }
    paint(g) {
        g.field(0, 0, this.w, this.h);
        const sw = this.needScroll ? gfx.SCROLL_W : 0;
        const inner = g.sub(2, 2, this.w - 4 - sw, this.h - 4);
        let x = 0;
        if (this.header) {
            for (const col of this.columns) {
                inner.button(x, 0, col.w, 20, '');
                inner.sub(x, 0, col.w - 4, 20).text(6, 2, col.title);
                x += col.w;
            }
            inner.button(x, 0, Math.max(2, this.w - x), 20, '');
        }
        const body = inner.sub(0, this.headerH, this.w - 4 - sw, this.h - 4 - this.headerH);
        const end = Math.min(this.rows.length, this.top + this.pageSize + 1);
        for (let i = this.top; i < end; i++) {
            const row = this.rows[i];
            const cells = Array.isArray(row) ? row : row.cells;
            const y = (i - this.top) * this.rowH;
            const sel = i === this.selected;
            const fg = sel ? C.selectText : (row.color ?? C.text);
            let cx = 0;
            this.columns.forEach((col, ci) => {
                const cell = body.sub(cx, y, col.w, this.rowH);
                let tx = 4;
                if (ci === 0 && row.icon) {
                    drawIcon(cell, row.icon, 2, 1, 16);
                    tx = 22;
                }
                const s = String(cells[ci] ?? '');
                if (sel && ci === 0) cell.fill(tx - 2, 0, Math.min(col.w - tx + 2, textWidth(s) + 4), this.rowH, C.select);
                else if (sel) cell.fill(0, 0, col.w, this.rowH, C.select);
                const w = textWidth(s);
                cell.text(col.align === 'r' ? col.w - w - 6 : tx, 1, s, fg);
                cx += col.w;
            });
            if (sel && this.focused) {
                for (let px = 0; px < this.w - 4 - sw; px += 2) { body.pixel(px, y, C.yellow); body.pixel(px, y + this.rowH - 1, C.yellow); }
            }
        }
        if (sw) g.scrollbar(this.w - 2 - sw, 2, this.h - 4, true, this.rows.length, this.pageSize, this.top);
    }
    scrollTo(top) {
        this.top = Math.max(0, Math.min(this.rows.length - this.pageSize, top));
        this.invalidate();
    }
    mouseDown(x, y, ev) {
        const sw = this.needScroll ? gfx.SCROLL_W : 0;
        if (sw && x >= this.w - 2 - sw) {
            const h = scrollHit(this.h - 4, this.rows.length, this.pageSize, this.top, y - 2);
            this.drag = h.part === 'thumb' ? h : null;
            const step = { up: -1, down: 1, pageup: -this.pageSize, pagedown: this.pageSize }[h.part];
            if (step) this.scrollTo(this.top + step);
            return;
        }
        if (y < 2 + this.headerH) return;
        const i = this.top + Math.floor((y - 2 - this.headerH) / this.rowH);
        if (i < this.rows.length) {
            this.select(i);
            if (ev.double) this.onActivate?.(this.rows[i], i);
        }
    }
    mouseMove(x, y, ev) {
        if (!this.drag || !(ev.buttons & 1)) return;
        const d = this.drag;
        const off = y - 2 - gfx.SCROLL_W - d.grab;
        this.scrollTo(Math.round(off * d.maxpos / Math.max(1, d.track - d.th)));
    }
    mouseUp() { this.drag = null; }
    key(ev) {
        const map = { [KEY.UP]: -1, [KEY.DOWN]: 1, [KEY.PGUP]: -this.pageSize, [KEY.PGDN]: this.pageSize };
        if (map[ev.code] !== undefined) { this.select(this.selected + map[ev.code]); return true; }
        if (ev.code === KEY.HOME) { this.select(0); return true; }
        if (ev.code === KEY.END) { this.select(this.rows.length - 1); return true; }
        if (ev.code === KEY.ENTER && this.selected >= 0) { this.onActivate?.(this.rows[this.selected], this.selected); return true; }
        return false;
    }
}

export class TextArea extends Widget {
    constructor(x, y, w, h, { text = '', readonly = false, onChange, color = C.text, bg = C.field } = {}) {
        super(x, y, w, h);
        Object.assign(this, { readonly, onChange, color, bg, focusable: true, cx: 0, cy: 0, top: 0, left: 0 });
        this.setText(text);
    }
    setText(t) { this.lines = String(t).split('\n'); this.cx = this.cy = this.top = this.left = 0; this.invalidate(); }
    get text() { return this.lines.join('\n'); }
    append(t) {
        const parts = String(t).split('\n');
        this.lines[this.lines.length - 1] += parts[0];
        this.lines.push(...parts.slice(1));
        if (this.lines.length > 2000) this.lines.splice(0, this.lines.length - 2000);
        this.cy = this.lines.length - 1;
        this.cx = this.lines[this.cy].length;
        this.ensure();
        this.invalidate();
    }
    get rowsVisible() { return Math.max(1, Math.floor((this.h - 4) / FONT_H)); }
    get colsVisible() { return Math.max(1, Math.floor((this.w - 8 - gfx.SCROLL_W) / FONT_W)); }
    ensure() {
        if (this.cy < this.top) this.top = this.cy;
        if (this.cy >= this.top + this.rowsVisible) this.top = this.cy - this.rowsVisible + 1;
        if (this.cx < this.left) this.left = this.cx;
        if (this.cx >= this.left + this.colsVisible) this.left = this.cx - this.colsVisible + 1;
    }
    paint(g) {
        g.fill(0, 0, this.w, this.h, this.bg);
        g.bevel(0, 0, this.w, this.h, gfx.FIELD_EDGE);
        const body = g.sub(4, 2, this.w - 6 - gfx.SCROLL_W, this.h - 4);
        for (let r = 0; r < this.rowsVisible; r++) {
            const l = this.lines[this.top + r];
            if (l === undefined) break;
            body.text(0, r * FONT_H, l.slice(this.left, this.left + this.colsVisible + 1), this.color);
        }
        if (this.focused && !this.readonly)
            body.fill((this.cx - this.left) * FONT_W, (this.cy - this.top) * FONT_H, 2, FONT_H, this.color);
        g.scrollbar(this.w - 2 - gfx.SCROLL_W, 2, this.h - 4, true, this.lines.length, this.rowsVisible, this.top);
    }
    mouseDown(x, y) {
        if (x >= this.w - 2 - gfx.SCROLL_W) {
            const h = scrollHit(this.h - 4, this.lines.length, this.rowsVisible, this.top, y - 2);
            const step = { up: -1, down: 1, pageup: -this.rowsVisible, pagedown: this.rowsVisible }[h.part];
            if (step) this.top = Math.max(0, Math.min(this.lines.length - this.rowsVisible, this.top + step));
            this.drag = h.part === 'thumb' ? h : null;
            this.invalidate();
            return;
        }
        this.cy = Math.min(this.lines.length - 1, this.top + Math.floor((y - 2) / FONT_H));
        this.cx = Math.min(this.lines[this.cy].length, this.left + Math.round((x - 4) / FONT_W));
        this.invalidate();
    }
    mouseMove(x, y, ev) {
        if (!this.drag || !(ev.buttons & 1)) return;
        const d = this.drag;
        const off = y - 2 - gfx.SCROLL_W - d.grab;
        this.top = Math.max(0, Math.min(d.maxpos, Math.round(off * d.maxpos / Math.max(1, d.track - d.th))));
        this.invalidate();
    }
    mouseUp() { this.drag = null; }
    key(ev) {
        const L = this.lines;
        let { cx, cy } = this;
        const edit = (fn) => { if (this.readonly) return; fn(); this.onChange?.(); };
        switch (ev.code) {
            case KEY.UP: cy = Math.max(0, cy - 1); break;
            case KEY.DOWN: cy = Math.min(L.length - 1, cy + 1); break;
            case KEY.PGUP: cy = Math.max(0, cy - this.rowsVisible); break;
            case KEY.PGDN: cy = Math.min(L.length - 1, cy + this.rowsVisible); break;
            case KEY.LEFT: if (cx > 0) cx--; else if (cy > 0) { cy--; cx = L[cy].length; } break;
            case KEY.RIGHT: if (cx < L[cy].length) cx++; else if (cy < L.length - 1) { cy++; cx = 0; } break;
            case KEY.HOME: cx = 0; break;
            case KEY.END: cx = L[cy].length; break;
            case KEY.ENTER: edit(() => { L.splice(cy + 1, 0, L[cy].slice(cx)); L[cy] = L[cy].slice(0, cx); cy++; cx = 0; }); break;
            case KEY.BACKSPACE:
                edit(() => {
                    if (cx > 0) { L[cy] = L[cy].slice(0, cx - 1) + L[cy].slice(cx); cx--; }
                    else if (cy > 0) { cx = L[cy - 1].length; L[cy - 1] += L[cy]; L.splice(cy, 1); cy--; }
                });
                break;
            case KEY.DELETE:
                edit(() => {
                    if (cx < L[cy].length) L[cy] = L[cy].slice(0, cx) + L[cy].slice(cx + 1);
                    else if (cy < L.length - 1) { L[cy] += L[cy + 1]; L.splice(cy + 1, 1); }
                });
                break;
            case KEY.TAB: edit(() => { L[cy] = L[cy].slice(0, cx) + '    ' + L[cy].slice(cx); cx += 4; }); break;
            default:
                if (ev.ch && ev.ch >= ' ' && !(ev.mods & gfx.MOD_CTRL)) {
                    edit(() => { L[cy] = L[cy].slice(0, cx) + ev.ch + L[cy].slice(cx); cx++; });
                } else return false;
        }
        this.cy = cy;
        this.cx = Math.min(cx, L[cy].length);
        this.ensure();
        this.invalidate();
        return true;
    }
}

export class Tabs extends Widget {
    constructor(x, y, w, h, tabs, onChange) {
        super(x, y, w, h);
        Object.assign(this, { tabs, onChange, index: 0, focusable: true });
    }
    tabRects() {
        let x = 2;
        return this.tabs.map(t => { const w = textWidth(t) + 16; const r = { x, w }; x += w; return r; });
    }
    get panel() { return { x: this.x + 2, y: this.y + 24, w: this.w - 4, h: this.h - 26 }; }
    paint(g) {
        g.fill(0, 21, this.w, this.h - 21, C.face);
        g.bevel(0, 21, this.w, this.h - 21, gfx.WINDOW_EDGE);
        this.tabRects().forEach((r, i) => {
            const sel = i === this.index;
            const y = sel ? 0 : 2;
            g.fill(r.x - (sel ? 2 : 0), y, r.w + (sel ? 4 : 0), 23 - y, C.face);
            const x0 = r.x - (sel ? 2 : 0), w = r.w + (sel ? 4 : 0);
            g.fill(x0, y + 2, 1, 21 - y, C.hilight);
            g.fill(x0 + 2, y, w - 4, 1, C.hilight);
            g.pixel(x0 + 1, y + 1, C.hilight);
            g.fill(x0 + w - 1, y + 2, 1, 19 - y, C.dark);
            g.fill(x0 + w - 2, y + 1, 1, 20 - y, C.shadow);
            g.text(r.x + 8, y + 3, this.tabs[i], C.text, -1, sel && this.focused ? gfx.UNDERLINE : 0);
            if (sel) g.fill(x0 + 1, 21, w - 2, 2, C.face);
        });
    }
    mouseDown(x, y) {
        this.tabRects().forEach((r, i) => { if (y < 22 && x >= r.x && x < r.x + r.w) this.set(i); });
    }
    set(i) {
        if (i === this.index) return;
        this.index = (i + this.tabs.length) % this.tabs.length;
        this.invalidate();
        this.onChange?.(this.index);
    }
    key(ev) {
        if (ev.code === KEY.LEFT) { this.set(this.index - 1); return true; }
        if (ev.code === KEY.RIGHT) { this.set(this.index + 1); return true; }
        return false;
    }
}

export class Progress extends Widget {
    constructor(x, y, w, h, value = 0) { super(x, y, w, h); this.value = value; }
    set(v) { if (v !== this.value) { this.value = v; this.invalidate(); } }
    paint(g) { g.progress(0, 0, this.w, this.h, this.value); }
}

// a black "oscilloscope" graph with a green grid, like old task managers
export class Graph extends Widget {
    constructor(x, y, w, h, { series = [], max = 1, samples = 60, label = '' } = {}) {
        super(x, y, w, h);
        Object.assign(this, { series, max, samples, label, scrollPhase: 0 });
    }
    push(values) {
        values.forEach((v, i) => { const s = this.series[i]; s.values.push(v); if (s.values.length > this.samples) s.values.shift(); });
        this.scrollPhase = (this.scrollPhase + 1) % 4;
        this.invalidate();
    }
    paint(g) {
        g.fill(0, 0, this.w, this.h, C.black);
        g.bevel(0, 0, this.w, this.h, gfx.SUNKEN);
        const step = (this.w - 4) / (this.samples - 1);
        for (let x = this.w - 3 - this.scrollPhase * step * 3; x > 2; x -= step * 12) g.fill(Math.round(x), 2, 1, this.h - 4, C.dkgreen);
        for (let i = 1; i < 4; i++) g.fill(2, 2 + Math.round((this.h - 4) * i / 4), this.w - 4, 1, C.dkgreen);
        const max = Math.max(this.max, ...this.series.flatMap(s => s.values)) || 1;
        for (const s of this.series) {
            const off = this.samples - s.values.length;
            for (let i = 1; i < s.values.length; i++) {
                const x0 = 2 + Math.round((off + i - 1) * step), x1 = 2 + Math.round((off + i) * step);
                const y0 = this.h - 3 - Math.round((this.h - 6) * s.values[i - 1] / max);
                const y1 = this.h - 3 - Math.round((this.h - 6) * s.values[i] / max);
                g.line(x0, y0, x1, y1, s.color);
            }
        }
        if (this.label) g.text(6, 4, this.label, C.green);
    }
}

export class Canvas extends Widget {
    constructor(x, y, w, h, paintFn, { onMouse } = {}) {
        super(x, y, w, h);
        Object.assign(this, { paintFn, onMouse });
    }
    paint(g) { this.paintFn(g, this.w, this.h); }
    mouseDown(x, y, ev) { this.onMouse?.('down', x, y, ev); }
    mouseMove(x, y, ev) { this.onMouse?.('move', x, y, ev); }
    mouseUp(x, y, ev) { this.onMouse?.('up', x, y, ev); }
}

// ------------------------------------------------------------------ windows
export class Window {
    constructor({ title = 'Window', x = 80, y = 60, w = 400, h = 300, icon = 'file', resizable = true, minW = 180, minH = 120 } = {}) {
        Object.assign(this, { title, x, y, w, h, icon, resizable, minW, minH });
        this.widgets = [];
        this.focus = null;
        this.capture = null;
        this.minimized = false;
        this.timers = [];
        this.desktop = null;
    }
    add(w) { w.window = this; this.widgets.push(w); return w; }
    get client() { return gfx.client(this.x, this.y, this.w, this.h); }
    get rect() { return { x: this.x, y: this.y, w: this.w, h: this.h }; }
    setTitle(t) { this.title = t; this.invalidate(); this.desktop?.invalidateTaskbar(); }
    invalidate() { if (!this.minimized) this.desktop?.invalidate(this.rect); }
    every(ms, fn) { this.timers.push({ ms, fn, next: sys.uptime() + ms }); }
    layout(cw, ch) {}
    paintClient(g, cw, ch) {}
    paint(g, active) {
        g.window(this.x, this.y, this.w, this.h, this.title, active);
        const c = this.client;
        const cg = g.sub(c.x, c.y, c.w, c.h);
        if (cg.empty) return;
        cg.fill(0, 0, c.w, c.h, C.face);
        this.paintClient(cg, c.w, c.h);
        for (const w of this.widgets) {
            if (!w.visible) continue;
            const wg = cg.sub(w.x, w.y, w.w, w.h);
            if (!wg.empty) w.paint(wg);
        }
        if (this.resizable) {
            const gg = g.sub(this.x + this.w - 16, this.y + this.h - 16, 14, 14);
            for (let i = 0; i < 3; i++) {
                gg.line(13 - i * 4, 13, 13, 13 - i * 4, C.hilight);
                gg.line(12 - i * 4, 13, 13, 12 - i * 4, C.shadow);
                gg.line(11 - i * 4, 13, 13, 11 - i * 4, C.shadow);
            }
        }
    }
    widgetAt(x, y) {
        for (let i = this.widgets.length - 1; i >= 0; i--) if (this.widgets[i].contains(x, y)) return this.widgets[i];
        return null;
    }
    mouseDown(x, y, ev) {
        const w = this.widgetAt(x, y);
        if (w) {
            if (w.focusable && this.focus !== w) { this.focus = w; this.invalidate(); }
            this.capture = w;
            w.mouseDown(x - w.x, y - w.y, ev);
        }
        this.onMouseDown?.(x, y, ev);
    }
    mouseMove(x, y, ev) { if (this.capture) this.capture.mouseMove(x - this.capture.x, y - this.capture.y, ev); }
    mouseUp(x, y, ev) {
        const c = this.capture;
        this.capture = null;
        if (c) c.mouseUp(x - c.x, y - c.y, ev);
    }
    key(ev) {
        if (ev.code === KEY.TAB && !(this.focus instanceof TextArea && !this.focus.readonly)) {
            const f = this.widgets.filter(w => w.focusable && w.visible);
            if (f.length) {
                const i = f.indexOf(this.focus);
                this.focus = f[(i + ((ev.mods & gfx.MOD_SHIFT) ? f.length - 1 : 1)) % f.length];
                this.invalidate();
            }
            return true;
        }
        if (this.focus?.key(ev)) return true;
        return this.onKey?.(ev) ?? false;
    }
    resized() {
        const c = this.client;
        this.layout(c.w, c.h);
    }
    close() { this.desktop?.close(this); }
}

// a small modal-looking message box
export function messageBox(desktop, title, text, buttons = ['OK'], onChoice) {
    const lines = String(text).split('\n');
    const bw = Math.max(80, ...buttons.map(b => textWidth(b) + 24));
    const w = Math.max(240, Math.max(...lines.map(textWidth)) + 80, buttons.length * (bw + 10) + 20);
    const h = lines.length * FONT_H + 100;
    const win = new Window({ title, w, h, x: (desktop.width - w) >> 1, y: (desktop.height - h) >> 1, icon: 'info', resizable: false });
    win.paintClient = (g) => {
        drawIcon(g, 'info', 14, 12);
        lines.forEach((l, i) => g.text(58, 18 + i * FONT_H, l));
    };
    const bx0 = (w - 8 - buttons.length * (bw + 10) + 10) >> 1;
    buttons.forEach((b, i) => {
        const btn = win.add(new Button(bx0 + i * (bw + 10), h - 64, bw, 24, b, () => { win.close(); onChoice?.(b); }));
        if (i === 0) win.focus = btn;
    });
    desktop.open(win);
    return win;
}

// ------------------------------------------------------------------ desktop
const TASKBAR_H = 28;

export class Desktop {
    constructor(width = 800, height = 600) {
        if (!gfx.open(width, height)) throw new Error('cannot open /dev/fb0 (is this QEMU with -vga std?)');
        const s = gfx.size();
        this.width = s.width;
        this.height = s.height;
        this.windows = [];
        this.apps = [];
        this.icons = [];
        this.mx = this.width >> 1;
        this.my = this.height >> 1;
        this.buttons = 0;
        this.dirty = null;
        this.menu = null;
        this.drag = null;
        this.lastClick = { t: 0, x: -99, y: -99 };
        this.quit = false;
        this.clockText = '';
        gfx.cursor(this.mx, this.my);
    }

    // ---- apps and icons
    register(app) {
        this.apps.push(app);
        if (app.desktopIcon !== false) {
            const i = this.icons.length;
            this.icons.push({ app, x: 8, y: 10 + i * 74, selected: false });
        }
    }
    launch(id, args) {
        const app = this.apps.find(a => a.id === id);
        if (!app) return null;
        if (app.single) {
            const w = this.windows.find(x => x.app === app);
            if (w) { w.minimized = false; this.raise(w); return w; }
        }
        let win;
        try {
            win = app.create(this, args);
        } catch (e) {
            messageBox(this, app.title, `Could not start ${app.title}:\n${e.message}`);
            return null;
        }
        win.app = app;
        win.icon ??= app.icon;
        this.open(win);
        return win;
    }

    // ---- window management
    open(win) {
        win.desktop = this;
        const n = this.windows.length;
        if (win.x + win.w > this.width) win.x = Math.max(0, this.width - win.w - 8 - n * 4);
        if (win.y + win.h > this.height - TASKBAR_H) win.y = Math.max(0, this.height - TASKBAR_H - win.h - n * 4);
        this.windows.push(win);
        win.resized();
        if (!win.focus) win.focus = win.widgets.find(w => w.focusable) || null;
        this.invalidate(win.rect);
        this.invalidateTaskbar();
        win.onOpen?.();
    }
    close(win) {
        const i = this.windows.indexOf(win);
        if (i < 0) return;
        win.onClose?.();
        this.windows.splice(i, 1);
        this.invalidate(win.rect);
        this.invalidateTaskbar();
        const top = this.top;
        if (top) this.invalidate(top.rect);
    }
    get top() {
        for (let i = this.windows.length - 1; i >= 0; i--) if (!this.windows[i].minimized) return this.windows[i];
        return null;
    }
    raise(win) {
        const old = this.top;
        const i = this.windows.indexOf(win);
        if (i >= 0) this.windows.splice(i, 1);
        this.windows.push(win);
        win.minimized = false;
        this.invalidate(win.rect);
        if (old && old !== win) this.invalidate(old.rect);
        this.invalidateTaskbar();
    }
    minimize(win) {
        win.minimized = true;
        this.invalidate(win.rect);
        this.invalidateTaskbar();
        const t = this.top;
        if (t) this.invalidate(t.rect);
    }

    // ---- damage and painting
    invalidate(r = { x: 0, y: 0, w: this.width, h: this.height }) {
        this.dirty = union(this.dirty, r);
    }
    invalidateTaskbar() { this.invalidate({ x: 0, y: this.height - TASKBAR_H, w: this.width, h: TASKBAR_H }); }

    paint() {
        if (!this.dirty) return;
        const d = intersect(this.dirty, { x: 0, y: 0, w: this.width, h: this.height });
        this.dirty = null;
        if (d.w <= 0 || d.h <= 0) return;
        const g = new G(0, 0, d);
        g.fill(d.x, d.y, d.w, d.h, C.desktop);
        this.paintIcons(g);
        const top = this.top;
        for (const w of this.windows) {
            if (!w.minimized && intersect(w.rect, d).w > 0) w.paint(g, w === top);
        }
        this.paintTaskbar(g);
        if (this.menu) this.paintMenu(g);
        if (this.drag?.mode === 'outline') this.paintOutline(g);
        activeG = null;
        gfx.clip();
        gfx.flush(d.x, d.y, d.w, d.h);
    }

    // icon labels wrap into at most two centred lines of 10 characters
    iconLabel(title) {
        const out = [];
        let line = '';
        for (const word of title.split(' ')) {
            if (line && (line + ' ' + word).length > 10) { out.push(line); line = word; }
            else line = line ? line + ' ' + word : word;
        }
        out.push(line);
        return out.slice(0, 2);
    }
    paintIcons(g) {
        for (const ic of this.icons) {
            drawIcon(g, ic.app.icon, ic.x + 26, ic.y);
            this.iconLabel(ic.app.title).forEach((l, i) => {
                const tw = textWidth(l);
                const lx = ic.x + 42 - (tw >> 1), ly = ic.y + 35 + i * FONT_H;
                if (ic.selected) g.fill(lx - 2, ly, tw + 4, FONT_H, C.navy);
                g.text(lx, ly, l, C.white);
            });
        }
    }

    taskButtons() {
        const vis = this.windows;
        const x0 = 72, avail = this.width - x0 - 80;
        const w = Math.min(150, Math.floor(avail / Math.max(1, vis.length)) - 4);
        return vis.map((win, i) => ({ win, x: x0 + i * (w + 4), y: this.height - TASKBAR_H + 4, w, h: 22 }));
    }

    paintTaskbar(g) {
        const y = this.height - TASKBAR_H;
        g.fill(0, y, this.width, TASKBAR_H, C.face);
        g.fill(0, y + 1, this.width, 1, C.hilight);
        g.button(2, y + 4, 66, 22, '', !!this.menu);
        const p = this.menu ? 1 : 0;
        drawIcon(g, 'kiwi', 6 + p, y + 7 + p, 16);
        g.text(24 + p, y + 7 + p, 'Start', C.text, -1, gfx.BOLD);
        const top = this.top;
        for (const b of this.taskButtons()) {
            const active = b.win === top;
            if (active) g.dither(b.x + 2, b.y + 2, b.w - 4, b.h - 4, C.face, C.hilight);
            g.bevel(b.x, b.y, b.w, b.h, active ? gfx.BUTTON_DOWN : gfx.BUTTON_UP);
            if (!active) g.fill(b.x + 2, b.y + 2, b.w - 4, b.h - 4, C.face);
            const sub = g.sub(b.x + 3, b.y + 2, b.w - 8, b.h - 4);
            drawIcon(sub, b.win.icon || 'file', 1 + (active ? 1 : 0), 1 + (active ? 1 : 0), 16);
            sub.text(21 + (active ? 1 : 0), 2 + (active ? 1 : 0), b.win.title, C.text, -1, active ? gfx.BOLD : 0);
        }
        const tw = 70;
        g.bevel(this.width - tw - 4, y + 4, tw, 22, gfx.SUNKEN);
        const d = new Date();
        this.clockText = `${String(d.getUTCHours()).padStart(2, '0')}:${String(d.getUTCMinutes()).padStart(2, '0')}:${String(d.getUTCSeconds()).padStart(2, '0')}`;
        g.text(this.width - tw + 2, y + 7, this.clockText);
    }

    // ---- start menu
    menuItems() {
        return [...this.apps.filter(a => a.menu !== false).map(a => ({ label: a.title, icon: a.icon, run: () => this.launch(a.id) })),
            null,
            { label: 'Shut Down...', icon: 'power', run: () => this.shutdownDialog() }];
    }
    menuRect() {
        const items = this.menuItems();
        const h = items.reduce((n, it) => n + (it ? 26 : 8), 0) + 6;
        return { x: 2, y: this.height - TASKBAR_H - h + 2, w: 200, h, items };
    }
    paintMenu(g) {
        const m = this.menuRect();
        g.fill(m.x, m.y, m.w, m.h, C.face);
        g.bevel(m.x, m.y, m.w, m.h, gfx.WINDOW_EDGE);
        g.fill(m.x + 3, m.y + 3, 22, m.h - 6, C.shadow);
        'Kiwi'.split('').forEach((ch, i, a) => g.text(m.x + 10, m.y + m.h - 8 - (a.length - i) * 16, ch, C.white, -1, gfx.BOLD));
        let y = m.y + 3;
        m.items.forEach((it, i) => {
            if (!it) { g.fill(m.x + 28, y + 3, m.w - 32, 1, C.shadow); g.fill(m.x + 28, y + 4, m.w - 32, 1, C.hilight); y += 8; return; }
            const hot = this.menu.hover === i;
            if (hot) g.fill(m.x + 26, y, m.w - 29, 26, C.navy);
            drawIcon(g, it.icon, m.x + 30, y + 5, 16);
            g.text(m.x + 52, y + 5, it.label, hot ? C.white : C.text);
            it.y = y;
            y += 26;
        });
    }
    menuHit(x, y) {
        const m = this.menuRect();
        if (!inside(m, x, y)) return -2;
        let yy = m.y + 3;
        for (let i = 0; i < m.items.length; i++) {
            const hgt = m.items[i] ? 26 : 8;
            if (y >= yy && y < yy + hgt && x >= m.x + 26) return m.items[i] ? i : -1;
            yy += hgt;
        }
        return -1;
    }
    toggleMenu(open = !this.menu) {
        if (this.menu) this.invalidate(this.menuRect());
        this.menu = open ? { hover: -1 } : null;
        if (this.menu) this.invalidate(this.menuRect());
        this.invalidateTaskbar();
    }

    shutdownDialog() {
        messageBox(this, 'Shut Down Kiwi', 'What do you want the computer to do?', ['Exit to shell', 'Power off', 'Cancel'], (c) => {
            if (c === 'Exit to shell') this.quit = true;
            if (c === 'Power off') { gfx.close(); sys.poweroff(); }
        });
    }

    paintOutline(g) {
        const r = this.drag.rect;
        for (let i = 0; i < 2; i++) {
            g.dither(r.x + i, r.y, 1, r.h, C.black, C.white);
            g.dither(r.x + r.w - 1 - i, r.y, 1, r.h, C.black, C.white);
            g.dither(r.x, r.y + i, r.w, 1, C.black, C.white);
            g.dither(r.x, r.y + r.h - 1 - i, r.w, 1, C.black, C.white);
        }
    }

    // ---- input
    windowAt(x, y) {
        for (let i = this.windows.length - 1; i >= 0; i--) {
            const w = this.windows[i];
            if (!w.minimized && inside(w.rect, x, y)) return w;
        }
        return null;
    }

    mouse(ev) {
        this.mx = Math.max(0, Math.min(this.width - 1, this.mx + ev.dx));
        this.my = Math.max(0, Math.min(this.height - 1, this.my + ev.dy));
        gfx.cursor(this.mx, this.my);
        const was = this.buttons;
        this.buttons = ev.buttons;
        const x = this.mx, y = this.my;
        const e = { buttons: ev.buttons, double: false, mods: 0 };
        if ((ev.buttons & 1) && !(was & 1)) {
            const now = sys.uptime();
            e.double = now - this.lastClick.t < 450 && Math.abs(x - this.lastClick.x) < 5 && Math.abs(y - this.lastClick.y) < 5;
            this.lastClick = { t: e.double ? 0 : now, x, y };
            this.press(x, y, e);
        } else if (!(ev.buttons & 1) && (was & 1)) {
            this.release(x, y, e);
        } else {
            this.motion(x, y, e);
        }
    }

    press(x, y, e) {
        if (this.menu) {
            const i = this.menuHit(x, y);
            if (i !== -2) {
                const it = this.menuRect().items[i];
                this.toggleMenu(false);
                it?.run();
                return;
            }
            this.toggleMenu(false);
        }
        const ty = this.height - TASKBAR_H;
        if (y >= ty) {
            if (x < 70) { this.toggleMenu(); return; }
            const b = this.taskButtons().find(b => inside(b, x, y));
            if (b) {
                if (b.win === this.top) this.minimize(b.win);
                else this.raise(b.win);
            }
            return;
        }
        const win = this.windowAt(x, y);
        for (const ic of this.icons) {
            const was = ic.selected;
            ic.selected = !win && x >= ic.x && x < ic.x + 84 && y >= ic.y && y < ic.y + 68;
            if (was !== ic.selected) this.invalidate({ x: ic.x, y: ic.y, w: 84, h: 70 });
            if (ic.selected && e.double) this.launch(ic.app.id);
        }
        if (!win) return;
        if (win !== this.top) this.raise(win);
        const part = gfx.hit(win.x, win.y, win.w, win.h, x, y);
        if (part === 'close') { win.close(); return; }
        if (part === 'minimize') { this.minimize(win); return; }
        if (part === 'title') {
            this.drag = { win, mode: 'move', dx: x - win.x, dy: y - win.y };
            return;
        }
        if (part === 'resize' && win.resizable) {
            this.drag = { win, mode: 'outline', resize: true, ox: x, oy: y, rect: win.rect };
            this.invalidate(win.rect);
            return;
        }
        if (part === 'client') {
            const c = win.client;
            win.mouseDown(x - c.x, y - c.y, e);
        }
    }

    motion(x, y, e) {
        if (this.menu) {
            const i = this.menuHit(x, y);
            const hover = i >= 0 ? i : -1;
            if (hover !== this.menu.hover) { this.menu.hover = hover; this.invalidate(this.menuRect()); }
        }
        const d = this.drag;
        if (d?.mode === 'move') {
            const old = d.win.rect;
            d.win.x = Math.max(-d.win.w + 40, Math.min(this.width - 40, x - d.dx));
            d.win.y = Math.max(0, Math.min(this.height - TASKBAR_H - 20, y - d.dy));
            this.invalidate(old);
            this.invalidate(d.win.rect);
            return;
        }
        if (d?.mode === 'outline') {
            this.invalidate(d.rect);
            const w = d.win;
            d.rect = { x: w.x, y: w.y, w: Math.max(w.minW, w.w + x - d.ox), h: Math.max(w.minH, w.h + y - d.oy) };
            this.invalidate(d.rect);
            return;
        }
        const win = this.top;
        if (win?.capture) {
            const c = win.client;
            win.mouseMove(x - c.x, y - c.y, e);
        }
    }

    release(x, y, e) {
        const d = this.drag;
        this.drag = null;
        if (d?.mode === 'outline') {
            this.invalidate(d.rect);
            this.invalidate(d.win.rect);
            Object.assign(d.win, { w: d.rect.w, h: d.rect.h });
            d.win.resized();
            this.invalidate(d.win.rect);
            return;
        }
        if (d) return;
        const win = this.top;
        if (win?.capture) {
            const c = win.client;
            win.mouseUp(x - c.x, y - c.y, e);
        }
    }

    key(ev) {
        if (!ev.down) return;
        const alt = ev.mods & gfx.MOD_ALT, ctrl = ev.mods & gfx.MOD_CTRL;
        if ((ctrl && ev.code === KEY.ESC) || ev.code === 0x15B /* Windows key */) { this.toggleMenu(); return; }
        if (this.menu && ev.code === KEY.ESC) { this.toggleMenu(false); return; }
        if (alt && ev.code === KEY.TAB && this.windows.length) { this.raise(this.windows[0]); return; }
        if (alt && ev.code === KEY.F4) { this.top ? this.top.close() : this.shutdownDialog(); return; }
        if (this.menu) {
            const items = this.menuRect().items;
            const step = ev.code === KEY.DOWN ? 1 : ev.code === KEY.UP ? -1 : 0;
            if (step) {
                let i = this.menu.hover;
                do { i = (i + step + items.length) % items.length; } while (!items[i]);
                this.menu.hover = i;
                this.invalidate(this.menuRect());
            } else if (ev.code === KEY.ENTER && items[this.menu.hover]) {
                const it = items[this.menu.hover];
                this.toggleMenu(false);
                it.run();
            }
            return;
        }
        this.top?.key(ev);
    }

    // ---- main loop
    run() {
        this.invalidate();
        let lastClock = '';
        while (!this.quit) {
            const now = sys.uptime();
            let next = now + 1000 - (now % 1000);
            for (const w of this.windows) for (const t of w.timers) next = Math.min(next, t.next);
            const ev = gfx.poll(Math.max(1, next - now));
            if (ev) {
                if (ev.type === 'mouse') this.mouse(ev);
                else this.key(ev);
                // drain queued events before repainting
                for (let more; (more = gfx.poll(0));) {
                    if (more.type === 'mouse') this.mouse(more);
                    else this.key(more);
                }
            }
            const t = sys.uptime();
            for (const w of [...this.windows]) {
                for (const tm of w.timers) {
                    if (t >= tm.next) {
                        tm.next = t + tm.ms;
                        try { tm.fn(); } catch (e) { sys.write(2, `gui: timer: ${e.message}\n`); }
                    }
                }
            }
            const clock = new Date().toISOString().slice(11, 19);
            if (clock !== lastClock) {
                lastClock = clock;
                this.invalidate({ x: this.width - 78, y: this.height - TASKBAR_H + 4, w: 74, h: 22 });
            }
            this.paint();
        }
        gfx.close();
    }
}

export default {
    C, KEY, FONT_W, FONT_H, G, Widget, Label, Button, CheckBox, TextField, ListView, TextArea,
    Tabs, Progress, Graph, Canvas, Window, Desktop, messageBox, drawIcon, textWidth, intersect, union,
};
