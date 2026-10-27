// Notepad: edit text files (stored compressed in the ramfs).
import * as sys from 'sys';
import fmt from 'fmt';
import path from 'path';
import { Window, TextArea, Button, Label, TextField, messageBox, KEY } from 'gui';

export default {
    id: 'notepad', title: 'Notepad', icon: 'notepad',
    create(desktop, args = {}) {
        let file = args.file || null;
        let dirty = false;
        const win = new Window({ title: 'Notepad', x: 140 + Math.random() * 60 | 0, y: 70, w: 520, h: 380, icon: 'notepad' });
        const area = win.add(new TextArea(4, 4, 300, 200, { onChange: () => { if (!dirty) { dirty = true; title(); } } }));
        const name = win.add(new TextField(8, 0, 260, file || '/home/untitled.txt'));
        const save = win.add(new Button(0, 0, 70, 24, 'Save', () => doSave()));
        const status = win.add(new Label(0, 0, ''));
        const title = () => win.setTitle(`${dirty ? '*' : ''}${file ? path.basename(file) : 'Untitled'} - Notepad`);

        function doSave() {
            const target = name.value.trim();
            if (!target) return;
            try {
                sys.writeFile(target, area.text);
                file = target;
                dirty = false;
                const st = sys.stat(target);
                status.set(`saved ${fmt.bytes(st.size)} -> ${fmt.bytes(st.csize)} in RAM`);
                title();
            } catch (e) {
                messageBox(desktop, 'Notepad', e.message);
            }
        }

        if (file) {
            try { area.setText(sys.readFile(file)); } catch (e) { area.setText(''); status.set(e.message); }
        }
        win.focus = area;
        win.onKey = (ev) => {
            if ((ev.mods & 2) && ev.ch === '\x13') { doSave(); return true; }   // Ctrl+S
            return false;
        };
        win.layout = (cw, ch) => {
            area.move(4, 4, cw - 8, ch - 40);
            name.move(8, ch - 31, Math.min(300, cw - 260));
            save.move(name.x + name.w + 6, ch - 32);
            status.move(save.x + 78, ch - 28);
        };
        title();
        return win;
    },
};
