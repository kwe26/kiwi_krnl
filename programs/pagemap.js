#!/bin/kjs
// pagemap - walk a process's page tables: which pages are resident,
// compressed in zram, or device memory.   pagemap [PID] [-a for every range]
import * as sys from 'sys';
import * as kernel from 'kernel';
import hw from 'hw';
import fmt from 'fmt';
import { color } from 'tty';

const args = sys.argv.slice(1);
const pid = Number(args.find(a => /^\d+$/.test(a)) || sys.getpid());
const all = args.includes('-a');
const p = kernel.proc(pid);
const { pages, summary } = hw.pagemap(pid);
console.log(`${color.bold(p.name)} pid ${pid}: page directory ${hw.hex(p.pageDir)}, heap ${hw.hex(p.heapStart)}-${hw.hex(p.brk)}`);
console.log(`${summary.resident} resident, ${summary.compressed} in zram, ${summary.device} device, ` +
    `${summary.accessed} accessed, ${summary.dirty} dirty  (${fmt.bytes((summary.resident + summary.compressed) * 4096)} mapped)`);
const ranges = hw.pageRanges(pages);
const paint = { resident: color.green, zram: color.magenta, device: color.yellow };
const shown = all ? ranges : ranges.slice(0, 40);
console.log(fmt.table(shown.map(r => [hw.hex(r.start), hw.hex(r.end), r.count, paint[r.state](r.state), hw.region(r.start, p)]),
    { header: ['START', 'END', 'PAGES', 'STATE', 'REGION'], align: 'llr' }));
if (shown.length < ranges.length) console.log(color.gray(`... ${ranges.length - shown.length} more ranges (use -a)`));
