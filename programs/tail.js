#!/bin/kjs
// tail - print the last lines of files.  -n N (default 10)
import * as sys from 'sys';
import fs from 'fs';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { string: 'n', numeric: 'n' });
const max = Number(o.n ?? 10);
for (const f of o._.length ? o._ : ['-']) {
    const fd = fs.openRead(f);
    const keep = [];
    for (const line of fs.lines(fd)) {
        keep.push(line);
        if (keep.length > max) keep.shift();
    }
    if (keep.length) sys.write(1, keep.join('\n') + '\n');
    if (fd) sys.close(fd);
}
