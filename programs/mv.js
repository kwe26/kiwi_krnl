#!/bin/kjs
// mv - move or rename files.
import * as sys from 'sys';
import fs from 'fs';
import path from 'path';
import { main } from 'proc';

main((argv) => {
    if (argv.length < 2) throw new Error('usage: mv SRC... DEST');
    const dest = argv[argv.length - 1];
    const srcs = argv.slice(0, -1);
    if (srcs.length > 1 && !fs.isDir(dest)) throw new Error(`${dest}: not a directory`);
    for (const s of srcs) sys.rename(s, fs.isDir(dest) ? path.join(dest, path.basename(s)) : dest);
});
