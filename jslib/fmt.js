// fmt: text formatting helpers (padding, sizes, tables, bars, sprintf).
import { width } from 'tty';

export function pad(s, n, ch = ' ') {
    s = String(s);
    const w = width(s);
    return w >= n ? s : s + ch.repeat(n - w);
}

export function lpad(s, n, ch = ' ') {
    s = String(s);
    const w = width(s);
    return w >= n ? s : ch.repeat(n - w) + s;
}

export function bytes(n) {
    if (n < 1024) return `${n} B`;
    const units = ['KB', 'MB', 'GB'];
    let u = -1;
    do { n /= 1024; u++; } while (n >= 1024 && u < units.length - 1);
    return `${n < 10 ? n.toFixed(1) : Math.round(n)} ${units[u]}`;
}

export function percent(part, whole, digits = 1) {
    return whole ? `${((100 * part) / whole).toFixed(digits)}%` : '-';
}

export function ratio(orig, packed) {
    return packed ? `${(orig / packed).toFixed(1)}x` : '-';
}

export function duration(ms) {
    const s = Math.floor(ms / 1000);
    const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
    const parts = [];
    if (d) parts.push(`${d}d`);
    if (d || h) parts.push(`${h}h`);
    parts.push(`${m}m`, `${s % 60}s`);
    return parts.join(' ');
}

// [####......] progress bar, ASCII only so it renders on VGA and serial
export function bar(frac, w = 20, fill = '#', empty = '.') {
    frac = Math.max(0, Math.min(1, frac || 0));
    const n = Math.round(frac * w);
    return fill.repeat(n) + empty.repeat(w - n);
}

// rows: array of arrays; align: string like 'lrr' (l = left, r = right)
export function table(rows, { header, align = '', gap = 2 } = {}) {
    const all = header ? [header, ...rows] : rows;
    const cols = Math.max(0, ...all.map(r => r.length));
    const w = [];
    for (let c = 0; c < cols; c++) w[c] = Math.max(...all.map(r => width(String(r[c] ?? ''))));
    const line = (r) => r.map((cell, c) => {
        const s = String(cell ?? '');
        const last = c === r.length - 1;
        if (align[c] === 'r') return lpad(s, w[c]);
        return last ? s : pad(s, w[c]);
    }).join(' '.repeat(gap));
    const out = all.map(line);
    if (header) out.splice(1, 0, w.map(n => '-'.repeat(n)).join(' '.repeat(gap)));
    return out.join('\n');
}

// arrange words in columns that fit the terminal width (like ls)
export function columns(items, totalWidth = 80) {
    if (!items.length) return '';
    const w = Math.max(...items.map(width)) + 2;
    const ncol = Math.max(1, Math.floor(totalWidth / w));
    const nrow = Math.ceil(items.length / ncol);
    const lines = [];
    for (let r = 0; r < nrow; r++) {
        let line = '';
        for (let c = 0; c < ncol; c++) {
            const i = c * nrow + r;
            if (i < items.length) line += c === ncol - 1 || i + nrow >= items.length ? items[i] : pad(items[i], w);
        }
        lines.push(line.trimEnd());
    }
    return lines.join('\n');
}

// printf-style formatting: %s %d %i %f %x %o %j %% with flags -0 width .prec
export function sprintf(fmt, ...args) {
    let i = 0;
    return fmt.replace(/%([-0]*)(\d*)(?:\.(\d+))?([sdifxXojc%])/g, (m, flags, wid, prec, conv) => {
        if (conv === '%') return '%';
        let v = args[i++];
        let s;
        switch (conv) {
            case 'd': case 'i': s = String(Math.trunc(Number(v))); break;
            case 'f': s = Number(v).toFixed(prec === undefined ? 6 : +prec); break;
            case 'x': s = (Number(v) >>> 0).toString(16); break;
            case 'X': s = (Number(v) >>> 0).toString(16).toUpperCase(); break;
            case 'o': s = (Number(v) >>> 0).toString(8); break;
            case 'j': s = JSON.stringify(v); break;
            case 'c': s = typeof v === 'number' ? String.fromCharCode(v) : String(v)[0]; break;
            default: s = String(v); if (prec !== undefined) s = s.slice(0, +prec);
        }
        const n = +wid || 0;
        if (flags.includes('-')) return pad(s, n);
        if (flags.includes('0') && conv !== 's') {
            const neg = s.startsWith('-');
            return (neg ? '-' : '') + lpad(neg ? s.slice(1) : s, n - (neg ? 1 : 0), '0');
        }
        return lpad(s, n);
    });
}

export function date(t = Date.now()) {
    const d = new Date(t);
    const p2 = (n) => String(n).padStart(2, '0');
    const days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    return `${days[d.getUTCDay()]} ${months[d.getUTCMonth()]} ${p2(d.getUTCDate())} ` +
        `${p2(d.getUTCHours())}:${p2(d.getUTCMinutes())}:${p2(d.getUTCSeconds())} UTC ${d.getUTCFullYear()}`;
}

export default { pad, lpad, bytes, percent, ratio, duration, bar, table, columns, sprintf, date };
