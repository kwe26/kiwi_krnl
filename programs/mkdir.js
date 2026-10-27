#!/bin/kjs
// mkdir - create directories.  -p: create parents as needed
import * as sys from 'sys';
import fs from 'fs';
import { main } from 'proc';
import { parse } from 'args';

main((argv) => {
    const o = parse(argv, { boolean: 'p' });
    if (!o._.length) throw new Error('usage: mkdir [-p] DIR...');
    for (const d of o._) fs.mkdir(d, { recursive: o.p });
});
