// path: POSIX path manipulation.
import * as sys from 'sys';

export const sep = '/';

export function normalize(p) {
    const abs = p.startsWith('/');
    const out = [];
    for (const part of p.split('/')) {
        if (part === '' || part === '.') continue;
        if (part === '..') {
            if (out.length && out[out.length - 1] !== '..') out.pop();
            else if (!abs) out.push('..');
        } else {
            out.push(part);
        }
    }
    const s = out.join('/');
    return abs ? '/' + s : (s || '.');
}

export function join(...parts) {
    return normalize(parts.filter(p => p !== '').join('/'));
}

export function resolve(...parts) {
    let p = '';
    for (const part of parts) p = part.startsWith('/') ? part : (p ? p + '/' + part : part);
    if (!p.startsWith('/')) p = sys.getcwd() + '/' + p;
    return normalize(p);
}

export function dirname(p) {
    if (p === '/') return '/';
    p = p.replace(/\/+$/, '');
    const i = p.lastIndexOf('/');
    if (i < 0) return '.';
    return i === 0 ? '/' : p.slice(0, i);
}

export function basename(p, ext) {
    p = p.replace(/\/+$/, '');
    let b = p.slice(p.lastIndexOf('/') + 1) || (p.startsWith('/') ? '/' : '');
    if (ext && b.endsWith(ext) && b !== ext) b = b.slice(0, -ext.length);
    return b;
}

export function extname(p) {
    const b = basename(p);
    const i = b.lastIndexOf('.');
    return i > 0 ? b.slice(i) : '';
}

export function relative(from, to) {
    const a = resolve(from).split('/').filter(Boolean);
    const b = resolve(to).split('/').filter(Boolean);
    let i = 0;
    while (i < a.length && i < b.length && a[i] === b[i]) i++;
    return [...Array(a.length - i).fill('..'), ...b.slice(i)].join('/') || '.';
}

export default { sep, normalize, join, resolve, dirname, basename, extname, relative };
