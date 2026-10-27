#!/bin/kjs
// ls - list directory contents.  -l long (shows compressed size), -a all, -1 one per line
import * as sys from 'sys';
import fs from 'fs';
import path from 'path';
import fmt from 'fmt';
import { color, isatty } from 'tty';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { boolean: 'la1hR' });
const targets = o._.length ? o._ : ['.'];
const tty = isatty(1);
const paint = (e, dir) => {
    if (!tty) return e.name;
    if (e.type === 'dir') return color.bblue(e.name) + '/';
    if (path.resolve(dir).endsWith('/bin')) return color.bgreen(e.name);
    return e.name;
};
let rc = 0;

// entries: [{ name, type, size, csize, full }] shown as one listing
function show(entries, base) {
    if (o.l) {
        const rows = entries.map(e => {
            const s = sys.stat(e.full) || e;
            const t = new Date((s.mtime || 0) * 1000).toISOString().slice(0, 16).replace('T', ' ');
            const ratio = e.type === 'dir' || !e.size ? '' : fmt.percent(e.csize, e.size, 0);
            return [e.type === 'dir' ? 'd' : '-', e.size, e.type === 'dir' ? '' : e.csize, ratio, t, paint(e, base)];
        });
        if (rows.length) console.log(fmt.table(rows, { header: ['T', 'SIZE', 'IN RAM', 'RATIO', 'MODIFIED', 'NAME'], align: 'lrrrll' }));
    } else if (o['1'] || !tty) {
        for (const e of entries) console.log(paint(e, base));
    } else if (entries.length) {
        console.log(fmt.columns(entries.map(e => paint(e, base)), sys.ttySize().cols));
    }
}

function listDir(dir, header) {
    let entries = fs.readdir(dir).map(e => ({ ...e, full: path.join(dir, e.name) }));
    if (!o.a) entries = entries.filter(e => !e.name.startsWith('.'));
    if (header) console.log(`${dir}:`);
    show(entries, dir);
    if (o.R) for (const e of entries) if (e.type === 'dir') { console.log(''); listDir(e.full, true); }
}

const files = [], dirs = [];
for (const t of targets) {
    const st = sys.stat(t);
    if (!st) { sys.write(2, `ls: ${t}: No such file or directory\n`); rc = 1; }
    else if (st.isDir) dirs.push(t);
    else files.push({ name: t, type: 'file', size: st.size, csize: st.csize, full: t });
}
if (files.length) show(files, path.dirname(files[0].full));
dirs.forEach((d, i) => {
    if (files.length || i) console.log('');
    listDir(d, files.length + dirs.length > 1 || o.R);
});
sys.exit(rc);
