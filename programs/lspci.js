#!/bin/kjs
// lspci - list PCI devices.  The bus is scanned from JavaScript by writing
// the configuration address port 0xCF8 and reading 0xCFC (needs IOPL 3).
//   -v  show BARs      -k  use the kernel's boot-time list instead
import * as sys from 'sys';
import * as kernel from 'kernel';
import hw from 'hw';
import { parse } from 'args';

const o = parse(sys.argv.slice(1), { boolean: 'vk' });
const devs = o.k ? kernel.pci().map(d => ({ ...d, id: `${d.vendor.toString(16).padStart(4, '0')}:${d.device.toString(16).padStart(4, '0')}`, name: '', vendorName: '' }))
    : hw.pciScan();
for (const d of devs) {
    console.log(`${String(d.bus).padStart(2, '0')}:${String(d.dev).padStart(2, '0')}.${d.fn} ` +
        `[${d.id}] class ${d.class.toString(16).padStart(2, '0')}${d.subclass.toString(16).padStart(2, '0')} ` +
        `${d.vendorName} ${d.name}`.trimEnd() + (d.irq && d.irq !== 0xFF ? `  irq ${d.irq}` : ''));
    if (o.v) d.bars.forEach((b, i) => { if (b) console.log(`        BAR${i} ${hw.hex(b)} ${b & 1 ? 'I/O' : 'memory'}`); });
}
