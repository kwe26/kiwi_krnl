// demo.js - a JavaScript program using Kiwi's system libraries.
// Run it with:  kjs demo.js
import * as sys from 'sys';
import fs from 'fs';
import fmt from 'fmt';
import { color } from 'tty';

console.log(color.bold('Files in /bin, largest first (stored compressed in RAM):'));
const bins = fs.readdir('/bin').sort((a, b) => b.size - a.size).slice(0, 5);
console.log(fmt.table(bins.map(e => [e.name, fmt.bytes(e.size), fmt.bytes(e.csize), fmt.ratio(e.size, e.csize)]),
    { header: ['name', 'size', 'in RAM', 'ratio'], align: 'lrrr' }));

const me = () => sys.ps().find(p => p.pid === sys.getpid());
const show = (when) => {
    const p = me();
    console.log(`${fmt.pad(when, 22)} resident ${fmt.lpad(fmt.bytes(p.resident * 4096), 7)}   ` +
        `compressed ${fmt.lpad(fmt.bytes(p.compressed * 4096), 7)}`);
};

console.log(color.bold('\nThis process\'s own memory:'));
show('at start');
const rows = Array.from({ length: 150000 }, (_, i) => ({ id: i, name: `user${i % 500}`, ok: i % 3 === 0 }));
show(`after ${rows.length} objects`);

console.log(color.gray('sleeping 5 s: the idle thread compresses pages nobody touches...'));
await new Promise(r => setTimeout(r, 5000));
show('after sleeping');

const sum = rows.reduce((a, r) => a + (r.ok ? r.id : 0), 0);    // touch everything again
show('after reading it all');
console.log(`(checksum ${sum}; every page came back intact from zram)`);
