// Command Prompt: run kbash command lines and show their output.
import * as sys from 'sys';
import { Window, TextArea, TextField, Button, C } from 'gui';

function capture(cmd) {
    const [r, w] = sys.pipe();
    let pid;
    try {
        pid = sys.spawn('/bin/kbash', ['kbash', '-c', cmd], { stdout: w, stderr: w, stdin: sys.open('/dev/null', 0) });
    } finally {
        sys.close(w);
    }
    let out = '';
    for (let s; (s = sys.read(r, 4096)) !== '';) out += s;
    sys.close(r);
    const st = sys.wait(pid);
    return { out: out.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, ''), code: st ? st.code : 0 };
}

export default {
    id: 'run', title: 'Command Prompt', icon: 'run',
    create(desktop, args = {}) {
        const win = new Window({ title: 'Command Prompt', x: 100, y: 90, w: 580, h: 360, icon: 'run' });
        const out = win.add(new TextArea(4, 4, 300, 200, { readonly: true, color: C.silver, bg: C.black }));
        const cmd = win.add(new TextField(4, 0, 300, args.command || '', { onEnter: () => go() }));
        const btn = win.add(new Button(0, 0, 60, 24, 'Run', () => go()));
        out.setText(`Kiwi command prompt - commands run in kbash, output appears here.\n`);
        const prompt = () => `${sys.getcwd()}> `;
        function go() {
            const line = cmd.value.trim();
            if (!line) return;
            out.append(`\n${prompt()}${line}\n`);
            const m = /^cd\s*(.*)$/.exec(line);
            if (m) {
                try { sys.chdir(m[1] || '/home'); } catch (e) { out.append(e.message + '\n'); }
            } else if (line === 'cls' || line === 'clear') {
                out.setText('');
            } else {
                const r = capture(line);
                out.append(r.out + (r.code ? `[exit ${r.code}]\n` : ''));
            }
            cmd.value = '';
            win.invalidate();
        }
        win.focus = cmd;
        win.layout = (cw, ch) => {
            out.move(4, 4, cw - 8, ch - 38);
            cmd.move(4, ch - 30, cw - 76);
            btn.move(cw - 66, ch - 31);
        };
        if (args.command) win.onOpen = go;
        return win;
    },
};
