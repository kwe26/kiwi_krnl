// Task Manager: processes, memory graph and compression controls.
import * as sys from 'sys';
import kz from 'kz';
import fmt from 'fmt';
import { Window, ListView, Button, Label, Graph, Progress, messageBox, C } from 'gui';

export default {
    id: 'taskmgr', title: 'Task Manager', icon: 'chart', single: true,
    create(desktop) {
        const win = new Window({ title: 'Task Manager', x: 200, y: 80, w: 520, h: 440, icon: 'chart', minW: 400, minH: 320 });
        const graph = win.add(new Graph(8, 8, 300, 110, {
            series: [{ values: [], color: C.green }, { values: [], color: C.yellow }],
            label: 'resident (green) / zram (yellow)',
        }));
        const stats = win.add(new Label(0, 8, ''));
        const budget = win.add(new Progress(0, 0, 180, 16));
        const list = win.add(new ListView(8, 126, 300, 200, {
            columns: [{ title: 'PID', w: 46, align: 'r' }, { title: 'Name', w: 110 }, { title: 'State', w: 76 },
                { title: 'Resident', w: 80, align: 'r' }, { title: 'In zram', w: 80, align: 'r' }, { title: 'CPU s', w: 56, align: 'r' }],
        }));
        const kill = win.add(new Button(0, 0, 110, 24, 'End Process', () => {
            const r = list.selectedRow;
            if (!r) return;
            if (r.pid <= 1 || r.pid === sys.getpid()) { messageBox(desktop, 'Task Manager', `Process ${r.pid} is protected.`); return; }
            try { sys.kill(r.pid, sys.SIGKILL); } catch (e) { messageBox(desktop, 'Task Manager', e.message); }
            update();
        }));
        const squeeze = win.add(new Button(0, 0, 130, 24, 'Compress Now', () => {
            const n = kz.compressAll();
            messageBox(desktop, 'Memory compression', `Compressed ${n} idle pages of other processes.`);
            update();
        }));

        function update() {
            const z = kz.stats();
            graph.push([z.resident, z.zOrig]);
            graph.max = Math.max(z.resident, z.zOrig, 1);
            stats.set([
                `RAM       ${fmt.bytes(z.ram)}`,
                `free      ${fmt.bytes(z.free)}`,
                `resident  ${fmt.bytes(z.resident)}`,
                `zram      ${fmt.bytes(z.zOrig)}`,
                `  stored  ${fmt.bytes(z.z_bytes)} (${fmt.ratio(z.zOrig, z.z_bytes)})`,
                `ramfs     ${fmt.ratio(z.fs_bytes, z.fs_zbytes)} smaller`,
                `budget:`,
            ].join('\n'));
            budget.set(z.resident / z.budgetBytes);
            list.setRows(sys.ps().map(p => ({
                pid: p.pid,
                cells: [p.pid, p.name, { R: 'running', S: 'sleeping', Z: 'zombie' }[p.state] || p.state,
                    fmt.bytes(p.resident * 4096), fmt.bytes(p.compressed * 4096), (p.ticks / 100).toFixed(1)],
            })));
        }
        win.layout = (cw, ch) => {
            graph.move(8, 8, cw - 210, 120);
            stats.move(cw - 194, 8);
            budget.move(cw - 194, 8 + 7 * 16 - 14, 186, 16);
            list.move(8, 136, cw - 16, ch - 136 - 40);
            kill.move(cw - 118, ch - 32);
            squeeze.move(8, ch - 32);
        };
        win.every(1000, update);
        win.onOpen = update;
        return win;
    },
};
