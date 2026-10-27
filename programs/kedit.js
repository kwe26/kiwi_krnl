#!/bin/kjs
// kedit - a full-screen text editor for Kiwi, written in JavaScript.
//
//   ^S save   ^Q quit   ^F find   ^N find next   ^G go to line
//   ^K cut line   ^U paste   ^D duplicate line   ^Z undo   ^L redraw
//   arrows, Home/End, PgUp/PgDn, Tab inserts 4 spaces
import * as sys from 'sys';
import tty from 'tty';
import path from 'path';
import { bytes } from 'fmt';

const file = sys.argv[1] || null;
let lines = [''];
let cx = 0, cy = 0;             // cursor (column, line) in the buffer
let top = 0, left = 0;          // scroll offsets
let dirty = false, quitArmed = false;
let message = '';
let clipboard = [];
let lastSearch = '';
const undo = [];
const { rows, cols } = sys.ttySize();
const GUTTER = 5;
const textRows = rows - 2;
const textCols = cols - GUTTER;
let screen = [];                // last frame, row -> string (to redraw only what changed)

if (file && sys.stat(file)) {
    try {
        const text = sys.readFile(file);
        lines = text.split('\n');
        if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
        message = `"${file}" ${lines.length} lines, ${bytes(text.length)}`;
    } catch (e) {
        message = e.message;
    }
} else if (file) {
    message = `"${file}" [new file]`;
}

// ---- syntax highlighting (JS / C / shell) ----
const ext = file ? path.extname(file) : '';
const KEYWORDS = new Set(('break case catch class const continue default delete do else export extends ' +
    'finally for function if import in instanceof let new return switch this throw try typeof var void ' +
    'while yield async await of static struct int char void unsigned long short double float sizeof ' +
    'typedef enum union uint8_t uint16_t uint32_t uint64_t size_t include define then fi elif done esac').split(' '));
const LITERALS = new Set(['true', 'false', 'null', 'undefined', 'NULL']);
const highlight = ['.js', '.c', '.h', '.sh', '.S'].includes(ext) || (file && file.startsWith('/bin/'));
const C = { kw: '\x1b[36m', str: '\x1b[32m', num: '\x1b[35m', com: '\x1b[90m', lit: '\x1b[33m', off: '\x1b[0m' };
const commentStart = ext === '.sh' ? '#' : '//';

function colorize(s) {
    if (!highlight) return s;
    let out = '', i = 0;
    while (i < s.length) {
        const ch = s[i];
        if (s.startsWith(commentStart, i)) { out += C.com + s.slice(i) + C.off; break; }
        if (ch === '"' || ch === "'" || ch === '`') {
            let j = i + 1;
            while (j < s.length && s[j] !== ch) j += s[j] === '\\' ? 2 : 1;
            out += C.str + s.slice(i, j + 1) + C.off;
            i = j + 1;
            continue;
        }
        const m = /^[A-Za-z_$][\w$]*/.exec(s.slice(i)) || /^\d[\w.]*/.exec(s.slice(i));
        if (m) {
            const w = m[0];
            if (/^\d/.test(w)) out += C.num + w + C.off;
            else if (KEYWORDS.has(w)) out += C.kw + w + C.off;
            else if (LITERALS.has(w)) out += C.lit + w + C.off;
            else out += w;
            i += w.length;
            continue;
        }
        out += ch;
        i++;
    }
    return out;
}

// ---- rendering ----
const write = (s) => sys.write(1, s);

function scroll() {
    if (cy < top) top = cy;
    if (cy >= top + textRows) top = cy - textRows + 1;
    if (cx < left) left = cx;
    if (cx >= left + textCols) left = cx - textCols + 1;
}

function render(full = false) {
    scroll();
    if (full) { screen = []; write(tty.clearScreen); }
    let frame = tty.hideCursor;
    for (let r = 0; r < textRows; r++) {
        const n = top + r;
        let row;
        if (n < lines.length) {
            const num = `\x1b[90m${String(n + 1).padStart(GUTTER - 1)} \x1b[0m`;
            row = num + colorize(lines[n].slice(left, left + textCols));
        } else {
            row = '\x1b[34m~\x1b[0m';
        }
        if (screen[r] !== row) {
            frame += tty.moveTo(r + 1, 1) + row + '\x1b[0m' + tty.clearEol;
            screen[r] = row;
        }
    }
    const name = file || '[no name]';
    const left_ = ` kedit  ${name}${dirty ? ' [+]' : ''}`;
    const right_ = `Ln ${cy + 1}/${lines.length}  Col ${cx + 1} `;
    const status = '\x1b[7m' + left_ + ' '.repeat(Math.max(1, cols - left_.length - right_.length)) + right_ + '\x1b[0m';
    const help = message || '\x1b[90m^S save  ^Q quit  ^F find  ^N next  ^G goto  ^K cut  ^U paste  ^Z undo\x1b[0m';
    const bottom = [status, help.slice(0, cols + 20)];
    for (let i = 0; i < 2; i++) {
        const r = textRows + i;
        if (screen[r] !== bottom[i]) {
            frame += tty.moveTo(r + 1, 1) + bottom[i] + tty.clearEol;
            screen[r] = bottom[i];
        }
    }
    frame += tty.moveTo(cy - top + 1, cx - left + GUTTER + 1) + tty.showCursor;
    write(frame);
}

// ---- editing ----
function snapshot() {
    undo.push({ lines: lines.slice(), cx, cy });
    if (undo.length > 100) undo.shift();
}

function change() {
    dirty = true;
    quitArmed = false;
}

function insert(text) {
    snapshot();
    const l = lines[cy];
    lines[cy] = l.slice(0, cx) + text + l.slice(cx);
    cx += text.length;
    change();
}

function newline() {
    snapshot();
    const l = lines[cy];
    const indent = /^\s*/.exec(l)[0];
    lines.splice(cy + 1, 0, indent + l.slice(cx));
    lines[cy] = l.slice(0, cx);
    cy++;
    cx = indent.length;
    change();
}

function backspace() {
    if (cx > 0) {
        snapshot();
        const l = lines[cy];
        lines[cy] = l.slice(0, cx - 1) + l.slice(cx);
        cx--;
        change();
    } else if (cy > 0) {
        snapshot();
        cx = lines[cy - 1].length;
        lines[cy - 1] += lines[cy];
        lines.splice(cy, 1);
        cy--;
        change();
    }
}

function del() {
    const l = lines[cy];
    if (cx < l.length) {
        snapshot();
        lines[cy] = l.slice(0, cx) + l.slice(cx + 1);
        change();
    } else if (cy < lines.length - 1) {
        snapshot();
        lines[cy] += lines[cy + 1];
        lines.splice(cy + 1, 1);
        change();
    }
}

function clampX() {
    cx = Math.min(cx, lines[cy].length);
}

// read a line of input on the message row
function ask(q, initial = '') {
    let ans = initial;
    for (;;) {
        message = `\x1b[1m${q}\x1b[0m ${ans}`;
        render();
        write(tty.moveTo(rows, q.length + 2 + ans.length));
        const k = tty.readKey();
        if (k === 'enter') break;
        if (k === 'escape' || k === 'ctrl-c' || k === 'ctrl-q' || k === null) { message = ''; return null; }
        if (k === 'backspace') ans = ans.slice(0, -1);
        else if (k.length === 1) ans += k;
    }
    message = '';
    return ans;
}

function save() {
    let name = file;
    if (!name) {
        name = ask('Save as:');
        if (!name) return;
    }
    try {
        const text = lines.join('\n') + '\n';
        sys.writeFile(name, text);
        const st = sys.stat(name);
        dirty = false;
        message = `wrote ${lines.length} lines, ${bytes(text.length)} -> ${bytes(st ? st.csize : text.length)} compressed in RAM`;
        return name;
    } catch (e) {
        message = `\x1b[31m${e.message}\x1b[0m`;
    }
}

function find(term, from = { x: cx + 1, y: cy }) {
    if (!term) return;
    for (let i = 0; i <= lines.length; i++) {
        const y = (from.y + i) % lines.length;
        const start = i === 0 ? from.x : 0;
        const x = lines[y].indexOf(term, start);
        if (x >= 0) {
            cy = y;
            cx = x;
            message = '';
            return;
        }
    }
    message = `not found: ${term}`;
}

// ---- main loop ----
tty.raw(true);
render(true);
try {
    for (;;) {
        const k = tty.readKey();
        if (k === null) break;
        if (k !== 'ctrl-q') quitArmed = false;
        if (message && k !== 'ctrl-q') message = '';
        switch (k) {
            case 'up': if (cy > 0) cy--; clampX(); break;
            case 'down': if (cy < lines.length - 1) cy++; clampX(); break;
            case 'left':
                if (cx > 0) cx--;
                else if (cy > 0) { cy--; cx = lines[cy].length; }
                break;
            case 'right':
                if (cx < lines[cy].length) cx++;
                else if (cy < lines.length - 1) { cy++; cx = 0; }
                break;
            case 'home': case 'ctrl-a': cx = 0; break;
            case 'end': case 'ctrl-e': cx = lines[cy].length; break;
            case 'pageup': cy = Math.max(0, cy - textRows); clampX(); break;
            case 'pagedown': cy = Math.min(lines.length - 1, cy + textRows); clampX(); break;
            case 'enter': newline(); break;
            case 'backspace': backspace(); break;
            case 'delete': del(); break;
            case 'tab': insert('    '); break;
            case 'ctrl-s': save(); break;
            case 'ctrl-q':
                if (dirty && !quitArmed) {
                    message = '\x1b[33munsaved changes - press ^Q again to quit without saving\x1b[0m';
                    quitArmed = true;
                    break;
                }
                throw 'quit';
            case 'ctrl-f': {
                const t = ask('Find:', lastSearch);
                if (t) { lastSearch = t; find(t, { x: cx, y: cy }); }
                break;
            }
            case 'ctrl-n': find(lastSearch); break;
            case 'ctrl-g': {
                const n = parseInt(ask('Go to line:'), 10);
                if (n > 0) { cy = Math.min(lines.length, n) - 1; cx = 0; }
                break;
            }
            case 'ctrl-k':
                snapshot();
                clipboard = [lines[cy]];
                if (lines.length > 1) lines.splice(cy, 1); else lines[0] = '';
                if (cy >= lines.length) cy = lines.length - 1;
                clampX();
                change();
                break;
            case 'ctrl-u':
                if (clipboard.length) {
                    snapshot();
                    lines.splice(cy, 0, ...clipboard);
                    change();
                }
                break;
            case 'ctrl-d':
                snapshot();
                lines.splice(cy + 1, 0, lines[cy]);
                cy++;
                change();
                break;
            case 'ctrl-z': {
                const u = undo.pop();
                if (u) { ({ lines, cx, cy } = u); dirty = true; } else message = 'nothing to undo';
                break;
            }
            case 'ctrl-l': render(true); continue;
            default:
                if (k.length === 1) insert(k);
        }
        render();
    }
} catch (e) {
    if (e !== 'quit') {
        tty.raw(false);
        write(tty.clearScreen);
        throw e;
    }
} finally {
    tty.raw(false);
}
write('\x1b[0m' + tty.clearScreen);
