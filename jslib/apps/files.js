// File Manager: browse the compressed ramfs.
import * as sys from 'sys';
import path from 'path';
import fmt from 'fmt';
import { Window, ListView, Button, TextField, Label, messageBox } from 'gui';

const TEXT = /\.(txt|js|sh|md|c|h)$|^(motd|kbashrc|\.kbash_history)$/;

export default {
    id: 'files', title: 'File Manager', icon: 'folder',
    create(desktop, args = {}) {
        let cwd = args.dir || '/';
        const win = new Window({ title: 'File Manager', x: 160, y: 50, w: 540, h: 400, icon: 'folder' });
        const up = win.add(new Button(8, 8, 50, 22, 'Up', () => go(path.dirname(cwd))));
        const loc = win.add(new TextField(64, 8, 300, cwd, { onEnter: (t) => go(t) }));
        const list = win.add(new ListView(8, 38, 300, 200, {
            columns: [{ title: 'Name', w: 200 }, { title: 'Size', w: 80, align: 'r' }, { title: 'In RAM', w: 80, align: 'r' }, { title: 'Ratio', w: 60, align: 'r' }],
            onActivate: (row) => open(row),
            onSelect: (row) => status.set(row.full),
        }));
        const status = win.add(new Label(8, 0, '', { maxW: 400 }));
        const openBtn = win.add(new Button(0, 0, 80, 24, 'Open', () => list.selectedRow && open(list.selectedRow)));

        function go(dir) {
            dir = path.resolve(cwd, dir);
            let entries;
            try { entries = sys.readdir(dir); } catch (e) { messageBox(desktop, 'File Manager', e.message); return; }
            cwd = dir;
            loc.value = dir;
            entries.sort((a, b) => (a.type === b.type ? (a.name < b.name ? -1 : 1) : a.type === 'dir' ? -1 : 1));
            let total = 0, held = 0;
            list.setRows(entries.map(e => {
                if (e.type === 'file') { total += e.size; held += e.csize; }
                return {
                    icon: e.type === 'dir' ? 'folder' : dir === '/bin' ? 'run' : TEXT.test(e.name) ? 'notepad' : 'file',
                    cells: [e.name, e.type === 'dir' ? '' : fmt.bytes(e.size), e.type === 'dir' ? '' : fmt.bytes(e.csize),
                        e.type === 'dir' || !e.size ? '' : fmt.ratio(e.size, e.csize)],
                    entry: e, full: path.join(dir, e.name),
                };
            }), false);
            if (list.rows.length) list.select(0);
            status.set(`${entries.length} items, ${fmt.bytes(total)} stored in ${fmt.bytes(held)} of RAM`);
            win.setTitle(`File Manager - ${dir}`);
        }

        function open(row) {
            const e = row.entry;
            if (e.type === 'dir') return go(row.full);
            if (e.type !== 'file') return messageBox(desktop, 'File Manager', `${row.full} is a device.`);
            if (cwd === '/bin') return desktop.launch('run', { command: e.name });
            desktop.launch('notepad', { file: row.full });
        }

        win.layout = (cw, ch) => {
            loc.move(64, 8, cw - 72);
            list.move(8, 38, cw - 16, ch - 38 - 36);
            status.move(8, ch - 26);
            openBtn.move(cw - 88, ch - 30);
        };
        win.onOpen = () => go(cwd);
        return win;
    },
};
