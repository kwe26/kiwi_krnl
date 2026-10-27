#!/bin/kjs
// cpuinfo - CPU identification, control registers and descriptor tables.
//   -t  also dump the GDT and IDT
import * as sys from 'sys';
import * as kernel from 'kernel';
import hw from 'hw';
import fmt from 'fmt';
import { color } from 'tty';

const c = hw.cpuInfo();
const s = kernel.system();
const L = (k) => color.cyan(k.padEnd(12));
console.log(`${L('cpu')}${c.brand} (${c.vendor})`);
console.log(`${L('signature')}family ${c.family} model ${c.model} stepping ${c.stepping}, ~${hw.tscMHz(sys)} MHz TSC`);
console.log(`${L('features')}${c.features.join(' ')}`);
console.log(`${L('cr0')}${hw.hex(s.cr0)}  ${hw.flagNames(s.cr0, hw.CR0)}`);
console.log(`${L('cr3')}${hw.hex(s.cr3)}  page directory of this process`);
console.log(`${L('cr4')}${hw.hex(s.cr4)}  ${hw.flagNames(s.cr4, hw.CR4)}`);
console.log(`${L('eflags')}${hw.hex(kernel.eflags())}  ${hw.flagNames(kernel.eflags(), hw.EFLAGS)} (user mode, IOPL ${(kernel.eflags() >> 12) & 3})`);
console.log(`${L('gdtr')}${hw.hex(s.gdtBase)} limit ${s.gdtLimit}    ${color.cyan('idtr')} ${hw.hex(s.idtBase)} limit ${s.idtLimit}`);
console.log(`${L('tss.esp0')}${hw.hex(s.tssEsp0)}  kernel stack for ring 3 -> 0 transitions`);
console.log(`${L('kernel')}${hw.hex(s.kernelStart)}-${hw.hex(s.kernelEnd)}, heap ${fmt.bytes(s.heapSize)} at ${hw.hex(s.heapStart)}`);

if (sys.argv.includes('-t')) {
    console.log('\n' + color.bold('GDT'));
    console.log(fmt.table(hw.decodeGdt().map(d => [hw.hex(d.selector, 2), d.type, hw.hex(d.base), hw.hex(d.limit), d.dpl, hw.hex(d.access, 2)]),
        { header: ['SEL', 'TYPE', 'BASE', 'LIMIT', 'DPL', 'ACCESS'] }));
    console.log('\n' + color.bold('IDT') + ' (present gates)');
    const idt = hw.decodeIdt();
    console.log(fmt.table(idt.map(g => [g.vector, hw.hex(g.handler), g.dpl, g.type, hw.vectorName(g.vector === 0x80 ? 48 : g.vector)]),
        { header: ['VEC', 'HANDLER', 'DPL', 'TYPE', 'NAME'], align: 'r' }));
}
