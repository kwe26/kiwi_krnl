#!/bin/kjs
// free - memory usage summary (sizes in KB, -m for MB).
import * as sys from 'sys';
import kz from 'kz';
import fmt from 'fmt';

const div = sys.argv.includes('-m') ? 1024 * 1024 : 1024;
const z = kz.stats();
const n = (b) => Math.round(b / div);
console.log(fmt.table([
    ['Mem:', n(z.ram), n(z.ram - z.free), n(z.free), n(z.resident), n(z.heap_used)],
    ['Zram:', n(z.zOrig), n(z.z_bytes), n(z.zSaved), '', ''],
    ['Ramfs:', n(z.fs_bytes), n(z.fs_zbytes), n(z.fsSaved), '', ''],
], { header: ['', 'total', 'used', 'free/saved', 'user', 'kheap'], align: 'lrrrrr' }));
