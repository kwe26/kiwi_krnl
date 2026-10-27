#!/bin/kjs
// cp - copy files.  -r: copy directories recursively
import * as sys from 'sys';
import fs from 'fs';
import { main } from 'proc';
import { parse } from 'args';

main((argv) => {
    const o = parse(argv, { boolean: 'rR' });
    if (o._.length < 2) throw new Error('usage: cp [-r] SRC... DEST');
    const dest = o._[o._.length - 1];
    for (const s of o._.slice(0, -1)) fs.copy(s, dest, { recursive: o.r || o.R });
});
