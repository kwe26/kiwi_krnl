#!/bin/kjs
// cat - concatenate files to standard output (byte exact).  -n numbers lines.
import * as sys from 'sys';
import fs from 'fs';

const args = sys.argv.slice(1);
const number = args[0] === '-n';
const files = number ? args.slice(1) : args;
let rc = 0, n = 1;
for (const f of files.length ? files : ['-']) {
    let fd;
    try { fd = fs.openRead(f); } catch (e) { sys.write(2, `cat: ${e.message}\n`); rc = 1; continue; }
    if (number) {
        for (const line of fs.lines(fd)) sys.write(1, `${String(n++).padStart(6)}  ${line}\n`);
    } else {
        for (let b; (b = sys.read(fd, 16384, true)).byteLength;) sys.write(1, b);
    }
    if (fd) sys.close(fd);
}
sys.exit(rc);
