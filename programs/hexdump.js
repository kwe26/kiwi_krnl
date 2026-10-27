#!/bin/kjs
// hexdump - canonical hex+ASCII dump.  -n N limits the byte count
import * as sys from 'sys';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { string: 'n' });
function readStdin() {
    const chunks = [];
    for (let b; (b = new Uint8Array(sys.read(0, 16384, true))).length;) chunks.push(b);
    const all = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
    let off = 0;
    for (const c of chunks) { all.set(c, off); off += c.length; }
    return all;
}
const data = o._.length ? new Uint8Array(sys.readFile(o._[0], true)) : readStdin();
const len = Math.min(data.length, o.n !== undefined ? Number(o.n) : data.length);
const hex = (v, w) => v.toString(16).padStart(w, '0');
const out = [];
for (let off = 0; off < len; off += 16) {
    const row = data.subarray(off, Math.min(off + 16, len));
    let h = '';
    for (let i = 0; i < 16; i++) h += (i < row.length ? hex(row[i], 2) : '  ') + (i === 7 ? '  ' : ' ');
    const a = Array.from(row, b => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.')).join('');
    out.push(`${hex(off, 8)}  ${h} |${a}|`);
}
out.push(hex(len, 8));
console.log(out.join('\n'));
