// hw: low-level x86 / PC hardware access written in JavaScript.
//
// Raw port I/O comes from the 'kernel' module (the process gets IOPL 3);
// everything else - PCI enumeration, CMOS, PIT, CPUID decoding, GDT/IDT
// and page-table decoding - is plain JavaScript.
import * as kernel from 'kernel';

export const hex = (v, w = 8) => '0x' + (v >>> 0).toString(16).padStart(w, '0');

// ---------------------------------------------------------------- PCI
const VENDORS = {
    0x8086: 'Intel', 0x1234: 'QEMU/Bochs', 0x1af4: 'Red Hat (virtio)', 0x10ec: 'Realtek',
    0x1022: 'AMD', 0x10de: 'NVIDIA', 0x1002: 'ATI/AMD', 0x15ad: 'VMware', 0x80ee: 'VirtualBox',
    0x1b36: 'Red Hat (QEMU)', 0x106b: 'Apple', 0x1013: 'Cirrus Logic',
};
const DEVICES = {
    '8086:1237': '440FX host bridge', '8086:7000': 'PIIX3 ISA bridge', '8086:7010': 'PIIX3 IDE',
    '8086:7113': 'PIIX4 ACPI', '8086:100e': '82540EM gigabit ethernet', '1234:1111': 'Bochs VGA (BGA)',
    '8086:29c0': 'Q35 host bridge', '8086:2918': 'ICH9 LPC', '8086:2922': 'ICH9 AHCI',
    '1af4:1000': 'virtio net', '1af4:1001': 'virtio block', '10ec:8139': 'RTL8139 ethernet',
};
const CLASSES = ['unclassified', 'storage', 'network', 'display', 'multimedia', 'memory', 'bridge',
    'communication', 'system peripheral', 'input', 'docking', 'processor', 'serial bus'];

export function pciRead(bus, dev, fn, off) {
    kernel.outl(0xCF8, (0x80000000 | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC)) >>> 0);
    return kernel.inl(0xCFC) >>> 0;
}

// enumerate the PCI bus by banging configuration ports 0xCF8/0xCFC
export function pciScan() {
    const out = [];
    const probe = (bus, dev, fn) => {
        const id = pciRead(bus, dev, fn, 0);
        if ((id & 0xFFFF) === 0xFFFF) return false;
        const cls = pciRead(bus, dev, fn, 8);
        const vendor = id & 0xFFFF, device = id >>> 16;
        const key = `${vendor.toString(16).padStart(4, '0')}:${device.toString(16).padStart(4, '0')}`;
        out.push({
            bus, dev, fn, vendor, device, id: key,
            class: cls >>> 24, subclass: (cls >>> 16) & 0xFF, rev: cls & 0xFF,
            irq: pciRead(bus, dev, fn, 0x3C) & 0xFF,
            bars: [0, 1, 2, 3, 4, 5].map(i => pciRead(bus, dev, fn, 0x10 + 4 * i)),
            vendorName: VENDORS[vendor] || 'unknown',
            name: DEVICES[key] || CLASSES[cls >>> 24] || 'device',
            className: CLASSES[cls >>> 24] || `class ${cls >>> 24}`,
        });
        return true;
    };
    for (let bus = 0; bus < 8; bus++) {
        for (let dev = 0; dev < 32; dev++) {
            if (!probe(bus, dev, 0)) continue;
            if (pciRead(bus, dev, 0, 0x0C) & 0x00800000)
                for (let fn = 1; fn < 8; fn++) probe(bus, dev, fn);
        }
    }
    return out;
}

// ---------------------------------------------------------------- CMOS / RTC / PIT
export function cmos(reg) {
    kernel.outb(0x70, reg);
    return kernel.inb(0x71);
}

export function rtc() {
    while (cmos(0x0A) & 0x80);                     // update in progress
    const bcd = !(cmos(0x0B) & 4);
    const v = (r) => { const x = cmos(r); return bcd ? (x & 15) + (x >> 4) * 10 : x; };
    return { year: 2000 + v(9), month: v(8), day: v(7), hour: v(4), minute: v(2), second: v(0) };
}

// latch and read the 16-bit counter of PIT channel 0
export function pitCounter() {
    kernel.outb(0x43, 0x00);
    const lo = kernel.inb(0x40), hi = kernel.inb(0x40);
    return lo | (hi << 8);
}

// ---------------------------------------------------------------- CPU
const EDX_FEATURES = ['fpu', 'vme', 'de', 'pse', 'tsc', 'msr', 'pae', 'mce', 'cx8', 'apic', '', 'sep', 'mtrr',
    'pge', 'mca', 'cmov', 'pat', 'pse36', 'psn', 'clflush', '', 'ds', 'acpi', 'mmx', 'fxsr', 'sse', 'sse2',
    'ss', 'htt', 'tm', 'ia64', 'pbe'];
const ECX_FEATURES = ['sse3', 'pclmul', 'dtes64', 'monitor', 'ds_cpl', 'vmx', 'smx', 'est', 'tm2', 'ssse3',
    'cid', 'sdbg', 'fma', 'cx16', 'xtpr', 'pdcm', '', 'pcid', 'dca', 'sse4.1', 'sse4.2', 'x2apic', 'movbe',
    'popcnt', 'tsc_deadline', 'aes', 'xsave', 'osxsave', 'avx', 'f16c', 'rdrand', 'hypervisor'];

const bits = (v, names) => names.filter((n, i) => n && (v >>> i) & 1);

export function cpuInfo() {
    const { vendor, brand } = kernel.cpu();
    const [eax, , ecx, edx] = kernel.cpuid(1);
    let family = (eax >>> 8) & 0xF, model = (eax >>> 4) & 0xF;
    if (family === 0xF) family += (eax >>> 20) & 0xFF;
    if (family === 6 || family >= 0xF) model += ((eax >>> 16) & 0xF) << 4;
    return { vendor, brand, family, model, stepping: eax & 0xF, features: [...bits(edx, EDX_FEATURES), ...bits(ecx, ECX_FEATURES)] };
}

export const CR0 = ['PE', 'MP', 'EM', 'TS', 'ET', 'NE', ...Array(10).fill(''), 'WP', '', 'AM', ...Array(10).fill(''), 'NW', 'CD', 'PG'];
export const CR4 = ['VME', 'PVI', 'TSD', 'DE', 'PSE', 'PAE', 'MCE', 'PGE', 'PCE', 'OSFXSR', 'OSXMMEXCPT'];
export const EFLAGS = ['CF', '', 'PF', '', 'AF', '', 'ZF', 'SF', 'TF', 'IF', 'DF', 'OF', '', '', 'NT', '', 'RF', 'VM', 'AC', 'VIF', 'VIP', 'ID'];
export const flagNames = (v, names) => bits(v, names).join(' ');

// measure the TSC frequency against the PIT-driven uptime clock
export function tscMHz(sys, ms = 100) {
    const t0 = kernel.rdtsc(), u0 = sys.uptime();
    sys.sleep(ms);
    const dt = kernel.rdtsc() - t0, du = sys.uptime() - u0;
    return du ? Math.round(dt / du / 1000) : 0;
}

// ---------------------------------------------------------------- descriptor tables
export function decodeGdt(buf = kernel.gdt()) {
    const d = new DataView(buf);
    const out = [];
    for (let i = 0; i * 8 < buf.byteLength; i++) {
        const lo = d.getUint32(i * 8, true), hi = d.getUint32(i * 8 + 4, true);
        const base = ((lo >>> 16) | ((hi & 0xFF) << 16) | (hi & 0xFF000000)) >>> 0;
        let limit = (lo & 0xFFFF) | (hi & 0xF0000);
        const access = (hi >>> 8) & 0xFF, gran = (hi >>> 23) & 1;
        if (gran) limit = (limit * 4096 + 4095) >>> 0;
        const present = !!(access & 0x80), dpl = (access >>> 5) & 3, system = !(access & 0x10);
        let type = 'null';
        if (present) type = system ? ((access & 0xF) === 9 || (access & 0xF) === 0xB ? 'TSS' : 'system')
            : (access & 8 ? 'code' : 'data');
        out.push({ index: i, selector: i * 8, base, limit, access, dpl, present, type, raw: [lo, hi] });
    }
    return out;
}

export function decodeIdt(buf = kernel.idt()) {
    const d = new DataView(buf);
    const out = [];
    for (let i = 0; i * 8 < buf.byteLength; i++) {
        const lo = d.getUint32(i * 8, true), hi = d.getUint32(i * 8 + 4, true);
        const flags = (hi >>> 8) & 0xFF;
        if (!(flags & 0x80)) continue;
        out.push({
            vector: i, selector: lo >>> 16, handler: ((hi & 0xFFFF0000) | (lo & 0xFFFF)) >>> 0,
            dpl: (flags >>> 5) & 3, type: (flags & 0xF) === 0xE ? 'interrupt gate' : (flags & 0xF) === 0xF ? 'trap gate' : 'gate',
        });
    }
    return out;
}

export const VECTORS = ['#DE divide', '#DB debug', 'NMI', '#BP breakpoint', '#OF overflow', '#BR bound',
    '#UD invalid opcode', '#NM no FPU', '#DF double fault', 'coprocessor', '#TS bad TSS', '#NP not present',
    '#SS stack', '#GP protection', '#PF page fault', 'reserved', '#MF x87', '#AC alignment', '#MC machine check',
    '#XM SIMD'];
export const IRQS = ['PIT timer', 'keyboard', 'cascade', 'COM2', 'COM1', 'LPT2', 'floppy', 'LPT1',
    'RTC', 'free', 'free', 'free', 'PS/2 mouse', 'FPU', 'ATA primary', 'ATA secondary'];

export function vectorName(v) {
    if (v < 20) return VECTORS[v];
    if (v < 32) return 'reserved';
    if (v < 48) return `IRQ${v - 32} ${IRQS[v - 32]}`;
    if (v === 48 || v === 0x80) return 'int 0x80 syscall';
    return '';
}

// ---------------------------------------------------------------- page tables
export function pagemap(pid = 0) {
    const raw = new Uint32Array(kernel.pagemap(pid));
    const pages = [];
    const sum = { resident: 0, compressed: 0, device: 0, accessed: 0, dirty: 0 };
    for (let i = 0; i < raw.length; i += 2) {
        const va = raw[i], pte = raw[i + 1];
        let state;
        if (pte & kernel.PTE_PHYS) { state = 'device'; sum.device++; }
        else if (pte & kernel.PTE_PRESENT) { state = 'resident'; sum.resident++; }
        else if (pte & kernel.PTE_ZRAM) { state = 'zram'; sum.compressed++; }
        else continue;
        if (pte & kernel.PTE_ACCESSED) sum.accessed++;
        if (pte & kernel.PTE_DIRTY) sum.dirty++;
        pages.push({
            va, pte, state,
            frame: state === 'zram' ? null : (pte & 0xFFFFF000) >>> 0,
            slot: state === 'zram' ? pte >>> 12 : null,
            accessed: !!(pte & kernel.PTE_ACCESSED), dirty: !!(pte & kernel.PTE_DIRTY),
        });
    }
    return { pages, summary: sum };
}

// coalesce consecutive pages with the same state into ranges
export function pageRanges(pages) {
    const out = [];
    for (const p of pages) {
        const last = out[out.length - 1];
        if (last && last.state === p.state && last.end === p.va) last.end += 4096, last.count++;
        else out.push({ start: p.va, end: p.va + 4096, state: p.state, count: 1 });
    }
    return out;
}

export function region(va, proc) {
    if (proc && va >= proc.heapStart && va < proc.brk) return 'heap';
    if (va >= 0xBF800000) return 'stack';
    if (va >= 0x80000000) return 'mmap';
    if (proc && va < proc.heapStart) return 'image';
    return '';
}

// ---------------------------------------------------------------- misc
export const MEMTYPES = ['', 'usable', 'reserved', 'ACPI reclaimable', 'ACPI NVS', 'bad'];

export function hexdump(buf, base = 0) {
    const b = new Uint8Array(buf);
    const lines = [];
    for (let o = 0; o < b.length; o += 16) {
        const row = b.subarray(o, o + 16);
        const h = Array.from(row, x => x.toString(16).padStart(2, '0')).join(' ').padEnd(48);
        const a = Array.from(row, x => (x >= 32 && x < 127 ? String.fromCharCode(x) : '.')).join('');
        lines.push(`${(base + o).toString(16).padStart(8, '0')}  ${h} |${a}|`);
    }
    return lines.join('\n');
}

export default {
    hex, pciRead, pciScan, cmos, rtc, pitCounter, cpuInfo, CR0, CR4, EFLAGS, flagNames, tscMHz,
    decodeGdt, decodeIdt, VECTORS, IRQS, vectorName, pagemap, pageRanges, region, MEMTYPES, hexdump,
};
