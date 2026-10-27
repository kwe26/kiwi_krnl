#!/bin/kjs
// tree - show a directory tree with file and compressed sizes.
import * as sys from 'sys';
import fs from 'fs';
import path from 'path';
import fmt from 'fmt';
import { color, isatty } from 'tty';

const root = sys.argv[1] || '.';
const paint = isatty(1);
let dirs = 0, files = 0, bytes = 0, held = 0;
console.log(paint ? color.bblue(root) : root);
function walk(dir, prefix) {
    const entries = fs.readdir(dir).filter(e => !e.name.startsWith('.'));
    entries.forEach((e, i) => {
        const last = i === entries.length - 1;
        const name = e.type === 'dir' ? (paint ? color.bblue(e.name) : e.name) :
            `${e.name} ${color.gray(`(${fmt.bytes(e.size)} -> ${fmt.bytes(e.csize)})`)}`;
        console.log(prefix + (last ? '`-- ' : '|-- ') + name);
        if (e.type === 'dir') { dirs++; walk(path.join(dir, e.name), prefix + (last ? '    ' : '|   ')); }
        else { files++; bytes += e.size; held += e.csize; }
    });
}
walk(root, '');
console.log(`\n${dirs} directories, ${files} files, ${fmt.bytes(bytes)} stored in ${fmt.bytes(held)} (${fmt.ratio(bytes, held)})`);
