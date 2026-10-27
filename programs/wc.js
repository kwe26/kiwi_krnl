#!/bin/kjs
// wc - count lines, words and bytes.  -l -w -c select columns
import * as sys from 'sys';
import fs from 'fs';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { boolean: 'lwc' });
const all = !o.l && !o.w && !o.c;
const tot = [0, 0, 0];
const show = (c, name) => {
    const cols = [];
    if (all || o.l) cols.push(String(c[0]).padStart(7));
    if (all || o.w) cols.push(String(c[1]).padStart(7));
    if (all || o.c) cols.push(String(c[2]).padStart(7));
    console.log(cols.join(' ') + (name ? ' ' + name : ''));
};

function count(fd) {
    const c = [0, 0, 0];
    let inWord = false;
    for (let b; (b = new Uint8Array(sys.read(fd, 16384, true))).length;) {
        c[2] += b.length;
        for (const x of b) {
            if (x === 10) c[0]++;
            const space = x === 32 || (x >= 9 && x <= 13);
            if (!space && !inWord) c[1]++;
            inWord = !space;
        }
    }
    return c;
}

const files = o._.length ? o._ : ['-'];
let rc = 0;
for (const f of files) {
    let fd;
    try { fd = fs.openRead(f); } catch (e) { sys.write(2, `wc: ${e.message}\n`); rc = 1; continue; }
    const c = count(fd);
    if (fd) sys.close(fd);
    c.forEach((v, i) => { tot[i] += v; });
    show(c, f === '-' ? '' : f);
}
if (files.length > 1) show(tot, 'total');
sys.exit(rc);
