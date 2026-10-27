#!/bin/kjs
// rm - remove files.  -r: recursive, -f: ignore missing files
import * as sys from 'sys';
import fs from 'fs';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { boolean: 'rfR' });
let rc = 0;
for (const f of o._) {
    try {
        if (!fs.exists(f) && o.f) continue;
        fs.rm(f, { recursive: o.r || o.R });
    } catch (e) {
        sys.write(2, `rm: ${e.message}\n`);
        rc = 1;
    }
}
sys.exit(rc);
