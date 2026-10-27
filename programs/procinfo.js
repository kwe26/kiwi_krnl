#!/bin/kjs
// procinfo - the kernel's view of a process.  procinfo [PID]
import * as sys from 'sys';
import * as kernel from 'kernel';
import hw from 'hw';
import fmt from 'fmt';
import { color } from 'tty';

const p = kernel.proc(Number(sys.argv[1] || sys.getpid()));
const L = (k) => color.cyan(k.padEnd(12));
console.log(`${L('process')}${p.name} pid ${p.pid} ppid ${p.ppid} pgid ${p.pgid}, ${p.state}, cwd ${p.cwd}`);
console.log(`${L('kernel')}stack ${hw.hex(p.kstack)} esp ${hw.hex(p.kesp)}, page directory ${hw.hex(p.pageDir)}`);
console.log(`${L('memory')}heap ${hw.hex(p.heapStart)}-${hw.hex(p.brk)}, next mmap ${hw.hex(p.mmapNext)}`);
console.log(`${L('pages')}${fmt.bytes(p.resident * 4096)} resident, ${fmt.bytes(p.compressed * 4096)} in zram`);
console.log(`${L('cpu')}${(p.ticks / 100).toFixed(2)} s, IOPL ${p.iopl}`);
for (const v of p.vmas) console.log(`${L('vma')}${hw.hex(v.start)}-${hw.hex(v.end)} ${v.type} (${fmt.bytes(v.end - v.start)})`);
console.log(`${L('fds')}${p.fds.map((t, i) => (t ? `${i}:${t}` : '')).filter(Boolean).join('  ')}`);
