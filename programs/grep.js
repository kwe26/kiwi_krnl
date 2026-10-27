#!/bin/kjs
// grep - search for a regular expression.  -i -v -n -c -l, -r recursive
import * as sys from 'sys';
import fs from 'fs';
import path from 'path';
import { color, isatty } from 'tty';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { boolean: 'ivnclrF' });
if (!o._.length) { sys.write(2, 'usage: grep [-ivnclrF] PATTERN [FILE...]\n'); sys.exit(2); }
const pat = o._[0];
const src = o.F ? pat.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') : pat;
const re = new RegExp(src, o.i ? 'i' : '');
const hl = new RegExp(src, o.i ? 'gi' : 'g');
let files = o._.slice(1);
if (o.r) {
    const expanded = [];
    for (const f of files.length ? files : ['.']) {
        if (fs.isDir(f)) fs.walk(f, (p, e) => { if (e.type === 'file') expanded.push(p); });
        else expanded.push(f);
    }
    files = expanded;
}
const many = files.length > 1;
const paint = isatty(1);
let found = false;
for (const f of files.length ? files : ['-']) {
    let fd;
    try { fd = fs.openRead(f); } catch (e) { sys.write(2, `grep: ${e.message}\n`); continue; }
    let n = 0, count = 0;
    for (const line of fs.lines(fd)) {
        n++;
        if (re.test(line) === !!o.v) continue;
        count++;
        found = true;
        if (o.l) break;
        if (o.c) continue;
        const text = paint && !o.v ? line.replace(hl, (m) => color.bred(m)) : line;
        const prefix = (many ? (paint ? color.magenta(f) : f) + ':' : '') + (o.n ? (paint ? color.green(n) : n) + ':' : '');
        sys.write(1, prefix + text + '\n');
    }
    if (o.l && count) console.log(f);
    if (o.c) console.log((many ? f + ':' : '') + count);
    if (fd) sys.close(fd);
}
sys.exit(found ? 0 : 1);
