#!/bin/kjs
// head - print the first lines of files.  -n N (default 10)
import * as sys from 'sys';
import fs from 'fs';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { string: 'n', numeric: 'n' });
const max = Number(o.n ?? 10);
for (const f of o._.length ? o._ : ['-']) {
    const fd = fs.openRead(f);
    let i = 0;
    for (const line of fs.lines(fd)) {
        if (i++ >= max) break;
        sys.write(1, line + '\n');
    }
    if (fd) sys.close(fd);
}
