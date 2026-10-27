// tty: ANSI terminal output and raw keyboard input.
import * as sys from 'sys';

export const CSI = '\x1b[';
export const clearScreen = '\x1b[2J\x1b[H';
export const clearEol = '\x1b[K';
export const clearLine = '\x1b[2K\r';
export const hideCursor = '\x1b[?25l';
export const showCursor = '\x1b[?25h';
export const reset = '\x1b[0m';
export const moveTo = (row, col) => `\x1b[${row};${col}H`;
export const up = (n = 1) => (n > 0 ? `\x1b[${n}A` : '');
export const down = (n = 1) => (n > 0 ? `\x1b[${n}B` : '');
export const right = (n = 1) => (n > 0 ? `\x1b[${n}C` : '');
export const left = (n = 1) => (n > 0 ? `\x1b[${n}D` : '');

const sgr = (code) => (s) => `\x1b[${code}m${s}\x1b[0m`;
export const color = {
    bold: sgr(1), inverse: sgr(7),
    red: sgr(31), green: sgr(32), yellow: sgr(33), blue: sgr(34),
    magenta: sgr(35), cyan: sgr(36), white: sgr(37), gray: sgr(90),
    bred: sgr('1;31'), bgreen: sgr('1;32'), byellow: sgr('1;33'),
    bblue: sgr('1;34'), bcyan: sgr('1;36'), bmagenta: sgr('1;35'),
};

// strip ANSI sequences (for measuring visible width)
export const strip = (s) => s.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '');
export const width = (s) => strip(s).length;

export const write = (s) => sys.write(1, s);
export const size = () => sys.ttySize();
export const isatty = (fd = 1) => sys.isatty(fd);

let rawDepth = 0;
export function raw(on) {
    sys.ttyRaw(!!on);
    rawDepth = on ? 1 : 0;
}
export const isRaw = () => rawDepth > 0;

// ---- key decoding ----
let pending = '';

function nextByte() {
    while (pending === '') {
        const s = sys.read(0, 64);
        if (s === '') return null;          // EOF
        pending += s;
    }
    const c = pending[0];
    pending = pending.slice(1);
    return c;
}

const CSI_KEYS = { A: 'up', B: 'down', C: 'right', D: 'left', H: 'home', F: 'end' };
const TILDE_KEYS = { 1: 'home', 2: 'insert', 3: 'delete', 4: 'end', 5: 'pageup', 6: 'pagedown', 7: 'home', 8: 'end' };

// Read one key in raw mode.  Returns a printable character, or a name
// such as 'up', 'enter', 'backspace', 'ctrl-s', or null on EOF.
export function readKey() {
    const c = nextByte();
    if (c === null) return null;
    const code = c.charCodeAt(0);
    if (c === '\x1b') {
        const c2 = nextByte();
        if (c2 === '[' || c2 === 'O') {
            let seq = '';
            for (;;) {
                const c3 = nextByte();
                if (c3 === null) return 'escape';
                if (/[0-9;]/.test(c3)) { seq += c3; continue; }
                if (c3 === '~') return TILDE_KEYS[seq.split(';')[0]] || 'unknown';
                return CSI_KEYS[c3] || 'unknown';
            }
        }
        return c2 === '\x1b' ? 'escape' : 'alt-' + c2;
    }
    if (c === '\r' || c === '\n') return 'enter';
    if (code === 127 || code === 8) return 'backspace';
    if (c === '\t') return 'tab';
    if (code < 32) return 'ctrl-' + String.fromCharCode(code + 96);
    return c;
}

export default {
    CSI, clearScreen, clearEol, clearLine, hideCursor, showCursor, reset, moveTo,
    up, down, right, left, color, strip, width, write, size, isatty, raw, isRaw, readKey,
};
