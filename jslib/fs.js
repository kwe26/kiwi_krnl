// fs: file system helpers on top of the sys module.
import * as sys from 'sys';
import path from 'path';

export const readFile = (p) => sys.readFile(p);
export const readBytes = (p) => new Uint8Array(sys.readFile(p, true));
export const writeFile = (p, data) => { sys.writeFile(p, data); };
export const appendFile = (p, data) => { sys.writeFile(p, data, true); };
export const stat = (p) => sys.stat(p);
export const exists = (p) => sys.stat(p) !== null;
export const isDir = (p) => !!sys.stat(p)?.isDir;
export const isFile = (p) => !!sys.stat(p)?.isFile;
export const rename = (a, b) => sys.rename(a, b);
export const cwd = () => sys.getcwd();
export const chdir = (p) => sys.chdir(p);

export function readdir(p = '.') {
    return sys.readdir(p).sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
}

export function mkdir(p, { recursive = false } = {}) {
    if (!recursive) return sys.mkdir(p);
    let cur = p.startsWith('/') ? '/' : '';
    for (const part of p.split('/').filter(Boolean)) {
        cur = cur ? path.join(cur, part) : part;
        if (!exists(cur)) sys.mkdir(cur);
        else if (!isDir(cur)) throw new Error(`mkdir ${cur}: Not a directory`);
    }
}

export function rm(p, { recursive = false } = {}) {
    const st = sys.stat(p);
    if (!st) throw new Error(`rm ${p}: No such file or directory`);
    if (st.isDir) {
        if (!recursive) throw new Error(`rm ${p}: Is a directory`);
        for (const e of sys.readdir(p)) rm(path.join(p, e.name), { recursive });
        return sys.rmdir(p);
    }
    sys.unlink(p);
}

export function copy(src, dst, { recursive = false } = {}) {
    const st = sys.stat(src);
    if (!st) throw new Error(`cp ${src}: No such file or directory`);
    if (isDir(dst)) dst = path.join(dst, path.basename(src));
    if (st.isDir) {
        if (!recursive) throw new Error(`cp ${src}: Is a directory (use -r)`);
        if (!exists(dst)) sys.mkdir(dst);
        for (const e of sys.readdir(src)) copy(path.join(src, e.name), path.join(dst, e.name), { recursive });
        return;
    }
    sys.writeFile(dst, sys.readFile(src, true));
}

// depth-first walk: fn(path, entry, depth)
export function walk(dir, fn, depth = 0) {
    for (const e of readdir(dir)) {
        const p = path.join(dir, e.name);
        if (fn(p, e, depth) === false) continue;
        if (e.type === 'dir') walk(p, fn, depth + 1);
    }
}

// read everything from a file descriptor
export function readAll(fd = 0) {
    let out = '';
    for (;;) {
        const s = sys.read(fd, 8192);
        if (s === '') return out;
        out += s;
    }
}

// iterate lines of a descriptor without loading it all
export function* lines(fd = 0) {
    let buf = '';
    for (;;) {
        const s = sys.read(fd, 4096);
        if (s === '') break;
        buf += s;
        let i;
        while ((i = buf.indexOf('\n')) >= 0) {
            yield buf.slice(0, i);
            buf = buf.slice(i + 1);
        }
    }
    if (buf) yield buf;
}

// open a named file (or '-' for stdin) for reading, returns fd
export function openRead(p) {
    return p === '-' ? 0 : sys.open(p, sys.O_RDONLY);
}

export default {
    readFile, readBytes, writeFile, appendFile, stat, exists, isDir, isFile, rename,
    cwd, chdir, readdir, mkdir, rm, copy, walk, readAll, lines, openRead,
};
