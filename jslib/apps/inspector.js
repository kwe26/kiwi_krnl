// Kernel Inspector: every low-level corner of Kiwi in one window.
import * as sys from 'sys';
import * as kernel from 'kernel';
import hw from 'hw';
import fmt from 'fmt';
import kz from 'kz';
import { Window, Tabs, ListView, TextArea, Button, Label, C } from 'gui';

const TABS = ['System', 'CPU', 'Tables', 'IRQs', 'PCI', 'Memory', 'Pages', 'Log'];
const KV = [{ title: 'Property', w: 170 }, { title: 'Value', w: 400 }];
let mhz = 0;

function systemRows() {
    const s = kernel.system(), u = sys.uname(), z = kz.stats();
    return [
        ['Kernel', `${u.sysname} ${u.release} (${u.version})`],
        ['Command line', s.cmdline || '(none)'],
        ['Uptime', fmt.duration(s.ticks * 1000 / s.hz) + ` (${s.ticks} ticks at ${s.hz} Hz)`],
        ['Processes', s.procs],
        ['System calls', s.syscalls],
        ['Kernel image', `${hw.hex(s.kernelStart)} - ${hw.hex(s.kernelEnd)} (${fmt.bytes(s.kernelEnd - s.kernelStart)})`],
        ['Kernel heap', `${hw.hex(s.heapStart)}, ${fmt.bytes(z.heap_used)} of ${fmt.bytes(s.heapSize)} used`],
        ['Physical RAM', `${fmt.bytes(s.totalFrames * 4096)}, ${fmt.bytes(s.freeFrames * 4096)} free (${s.maxFrame} frames)`],
        ['User pages', `${fmt.bytes(z.resident)} resident, budget ${fmt.bytes(z.budgetBytes)}`],
        ['zram', `${fmt.bytes(z.zOrig)} held in ${fmt.bytes(z.z_bytes)} (${fmt.ratio(z.zOrig, z.z_bytes)})`],
        ['ramfs', `${z.fs_files} files, ${fmt.bytes(z.fs_bytes)} held in ${fmt.bytes(z.fs_zbytes)}`],
        ['Boot time', new Date(s.bootEpoch * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC'],
        ['RTC (CMOS ports)', (() => { const r = hw.rtc(); return `${r.year}-${r.month}-${r.day} ${r.hour}:${r.minute}:${r.second}`; })()],
        ['PIT counter', `${hw.pitCounter()} (latched via port 0x43)`],
    ];
}

function cpuRows() {
    const c = hw.cpuInfo(), s = kernel.system();
    if (!mhz) mhz = hw.tscMHz(sys, 50);
    const ef = kernel.eflags();
    return [
        ['Processor', c.brand], ['Vendor', c.vendor],
        ['Signature', `family ${c.family}, model ${c.model}, stepping ${c.stepping}`],
        ['TSC clock', `~${mhz} MHz, now ${kernel.rdtsc().toExponential(4)} cycles`],
        ['Features', c.features.slice(0, 12).join(' ')], ['', c.features.slice(12).join(' ')],
        ['CR0', `${hw.hex(s.cr0)}  ${hw.flagNames(s.cr0, hw.CR0)}`],
        ['CR2', `${hw.hex(s.cr2)}  last page-fault address`],
        ['CR3', `${hw.hex(s.cr3)}  page directory (this process)`],
        ['CR4', `${hw.hex(s.cr4)}  ${hw.flagNames(s.cr4, hw.CR4)}`],
        ['EFLAGS (user)', `${hw.hex(ef)}  ${hw.flagNames(ef, hw.EFLAGS)}  IOPL=${(ef >> 12) & 3}`],
        ['GDTR', `${hw.hex(s.gdtBase)} limit ${s.gdtLimit}`],
        ['IDTR', `${hw.hex(s.idtBase)} limit ${s.idtLimit}`],
        ['TSS.ESP0', hw.hex(s.tssEsp0)],
    ];
}

function tableRows() {
    const rows = hw.decodeGdt().map(d => [`GDT ${hw.hex(d.selector, 2)}`, `${d.type.padEnd(5)} base ${hw.hex(d.base)} limit ${hw.hex(d.limit)} DPL ${d.dpl} access ${hw.hex(d.access, 2)}`]);
    for (const g of hw.decodeIdt())
        rows.push([`IDT ${g.vector}`, `${hw.hex(g.handler)} sel ${hw.hex(g.selector, 2)} DPL ${g.dpl} ${g.type}  ${hw.vectorName(g.vector === 0x80 ? 48 : g.vector)}`]);
    return rows;
}

function irqRows() {
    const counts = kernel.irqs();
    const max = Math.max(1, ...counts);
    return counts.map((n, v) => (n ? [v === 48 ? 'int 0x80' : `vector ${v}`, `${String(n).padStart(8)}  ${fmt.bar(n / max, 12, '|', ' ')}  ${hw.vectorName(v)}`] : null)).filter(Boolean);
}

function pciRows() {
    return hw.pciScan().map(d => [`${d.bus}:${d.dev}.${d.fn}  ${d.id}`, `${d.vendorName} ${d.name}` +
        (d.irq && d.irq !== 0xFF ? `, IRQ ${d.irq}` : '') + (d.bars[0] ? `, BAR0 ${hw.hex(d.bars[0])}` : '')]);
}

function memoryRows() {
    const rows = kernel.memoryMap().map(r => [`E820 ${hw.hex(r.addr)}`, `${fmt.bytes(r.len).padStart(8)}  ${hw.MEMTYPES[r.type] || r.type}`]);
    const z = kernel.zram();
    rows.push(['zram slots', `${z.slotsUsed} used of ${z.slotsTotal}; ${z.sharedObjects} shared, most shared x${z.maxRefs}`]);
    rows.push(['zram objects', `same ${z.byType.same}  lz ${z.byType.lz}  lz+huff ${z.byType.lzh}  raw ${z.byType.raw}`]);
    rows.push(['clock hands', `evict ${z.clockHand}, idle ${z.idleHand}`]);
    const hist = z.sizeHistogram;
    const max = Math.max(1, ...hist);
    hist.forEach((n, i) => { if (n) rows.push([i === 0 ? 'size 0 (same)' : `size < ${(i + 1) * 256} B`, `${String(n).padStart(6)}  ${fmt.bar(n / max, 30, '|', ' ')}`]); });
    return rows;
}

export default {
    id: 'inspector', title: 'Kernel Inspector', icon: 'chip',
    create(desktop) {
        const win = new Window({ title: 'Kernel Inspector', x: 120, y: 30, w: 640, h: 470, icon: 'chip', minW: 420, minH: 260 });
        const tabs = win.add(new Tabs(6, 6, 100, 100, TABS, () => refresh()));
        const list = win.add(new ListView(0, 0, 100, 100, { columns: KV }));
        const procs = win.add(new ListView(0, 0, 100, 100, { columns: [{ title: 'Process', w: 140 }], onSelect: () => refreshPages() }));
        const log = win.add(new TextArea(0, 0, 100, 100, { readonly: true }));
        const status = win.add(new Label(8, 0, '', { maxW: 500 }));
        const refreshBtn = win.add(new Button(0, 0, 90, 24, 'Refresh', () => refresh()));

        function refreshPages() {
            const row = procs.selectedRow;
            if (!row) return;
            const pid = row.pid;
            const p = kernel.proc(pid);
            const { pages, summary } = hw.pagemap(pid);
            const rows = hw.pageRanges(pages).map(r => ({
                cells: [`${hw.hex(r.start)}-${hw.hex(r.end)}`, `${String(r.count).padStart(5)} ${r.state.padEnd(9)} ${hw.region(r.start, p)}`],
                color: r.state === 'zram' ? C.purple : r.state === 'device' ? C.olive : C.text,
            }));
            list.columns = [{ title: 'Range', w: 190 }, { title: 'Pages / state / region', w: 300 }];
            list.setRows(rows, false);
            status.set(`${p.name}: ${summary.resident} resident, ${summary.compressed} in zram, ${summary.device} device pages, page dir ${hw.hex(p.pageDir)}`);
        }

        function refresh() {
            const t = TABS[tabs.index];
            procs.visible = t === 'Pages';
            log.visible = t === 'Log';
            list.visible = t !== 'Log';
            win.resized();
            status.set('');
            try {
                if (t === 'Log') {
                    log.setText(kernel.dmesg().trimEnd());
                    log.cy = log.lines.length - 1;
                    log.ensure();
                } else if (t === 'Pages') {
                    procs.setRows(sys.ps().filter(p => p.pid > 0).map(p => ({ cells: [`${p.pid} ${p.name}`], pid: p.pid })));
                    if (procs.selected < 0) procs.select(0); else refreshPages();
                } else {
                    list.columns = KV;
                    const rows = { System: systemRows, CPU: cpuRows, Tables: tableRows, IRQs: irqRows, PCI: pciRows, Memory: memoryRows }[t]();
                    list.setRows(rows, false);
                    if (t === 'PCI') status.set('enumerated from JavaScript through ports 0xCF8/0xCFC');
                    if (t === 'IRQs') status.set('live: refreshes every second');
                }
            } catch (e) {
                status.set(`error: ${e.message}`);
            }
            win.invalidate();
        }

        win.layout = (cw, ch) => {
            tabs.move(6, 6, cw - 12, ch - 42);
            const p = tabs.panel;
            const pages = TABS[tabs.index] === 'Pages';
            procs.move(p.x + 4, p.y + 6, 150, p.h - 12);
            list.move(p.x + 4 + (pages ? 156 : 0), p.y + 6, p.w - 8 - (pages ? 156 : 0), p.h - 12);
            log.move(p.x + 4, p.y + 6, p.w - 8, p.h - 12);
            refreshBtn.move(cw - 96, ch - 30);
            status.move(8, ch - 26);
            status.maxW = cw - 116;
        };
        win.every(1000, () => { if (TABS[tabs.index] === 'IRQs') refresh(); });
        win.onOpen = refresh;
        return win;
    },
};
