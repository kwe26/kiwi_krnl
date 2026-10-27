#!/bin/kjs
// memstat - show and tune Kiwi's memory compression.
//   memstat            report        memstat -w        refresh every second
//   memstat -b MB      set the resident user-page budget (0 = auto)
//   memstat -c         compress every idle page now
//   memstat -p N       proactive idle compression after N scans (0 = off)
import * as sys from 'sys';
import kz from 'kz';
import fmt from 'fmt';
import { color, clearScreen } from 'tty';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { boolean: 'cwh', string: 'bp' });
if (o.h) {
    console.log('usage: memstat [-w] [-c] [-b MB] [-p N]');
    sys.exit(0);
}
if (o.b !== undefined) console.log(`budget set to ${fmt.bytes(kz.setBudget(Number(o.b)) * 4096)}`);
if (o.p !== undefined) { kz.setProactive(Number(o.p)); console.log(`proactive threshold ${o.p}`); }
if (o.c) console.log(`compressed ${kz.compressAll()} pages`);

function report() {
    const z = kz.stats();
    const L = (k) => color.cyan(fmt.pad(k, 13));
    const out = [];
    out.push(color.bold('Kiwi memory compression'));
    out.push(`${L('RAM')}${fmt.bytes(z.ram)} total, ${fmt.bytes(z.free)} free`);
    out.push(`${L('user pages')}${fmt.bytes(z.resident)} resident of ${fmt.bytes(z.budgetBytes)} budget ` +
        `[${fmt.bar(z.resident / z.budgetBytes, 20)}]`);
    out.push(`${L('zram')}${fmt.bytes(z.zOrig)} of pages held in ${fmt.bytes(z.z_bytes)} ` +
        `(${color.bgreen(fmt.ratio(z.zOrig, z.z_bytes))}), saving ${fmt.bytes(z.zSaved)}`);
    out.push(`${L('')}same-filled ${z.z_same}  lz ${z.z_lz}  lz+huff ${z.z_lzh}  raw ${z.z_raw}  unique ${z.z_slots}  dedup hits ${z.z_dedup}`);
    out.push(`${L('')}page-outs ${z.z_pageouts}  page-ins ${z.z_pageins}  proactive ${z.proactive ? 'after ' + z.proactive + ' idle scans' : 'off'}`);
    out.push(`${L('ramfs')}${z.fs_files} files, ${fmt.bytes(z.fs_bytes)} held in ${fmt.bytes(z.fs_zbytes)} ` +
        `(${color.bgreen(fmt.ratio(z.fs_bytes, z.fs_zbytes))})`);
    out.push(`${L('kernel heap')}${fmt.bytes(z.heap_used)} of ${fmt.bytes(z.heap_total)}`);
    out.push('');
    const procs = sys.ps().filter(p => p.pid > 0);
    out.push(fmt.table(procs.map(p => [p.pid, p.name, fmt.bytes(p.resident * 4096), fmt.bytes(p.compressed * 4096),
        fmt.percent(p.compressed, p.resident + p.compressed, 0)]),
        { header: ['PID', 'NAME', 'RESIDENT', 'IN ZRAM', 'ZRAM%'], align: 'rlrrr' }));
    return out.join('\n');
}

if (o.w) {
    for (;;) {
        sys.write(1, clearScreen + report() + '\n\n' + color.gray('refreshing every second, ^C to stop') + '\n');
        sys.sleep(1000);
    }
}
console.log(report());
