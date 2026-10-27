#!/bin/kjs
// touch - create empty files (existing files are left alone).
import * as sys from 'sys';

for (const f of sys.argv.slice(1)) {
    try { sys.close(sys.open(f, sys.O_WRONLY | sys.O_CREAT)); }
    catch (e) { sys.write(2, `touch: ${e.message}\n`); }
}
