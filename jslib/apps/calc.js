// Calculator: a classic four-function calculator.
import { Window, Button, Canvas, C } from 'gui';

export default {
    id: 'calc', title: 'Calculator', icon: 'calc',
    create() {
        const win = new Window({ title: 'Calculator', x: 420, y: 120, w: 236, h: 290, icon: 'calc', resizable: false });
        let display = '0', acc = null, op = null, fresh = true;
        const show = (v) => {
            display = Number.isFinite(v) ? String(+v.toPrecision(12)) : 'Error';
            win.invalidate();
        };
        win.add(new Canvas(8, 8, 212, 30, (g, w, h) => {
            g.field(0, 0, w, h);
            g.text(w - 8 - display.length * 8, 7, display, C.text, -1, 1);
        }));
        const apply = () => {
            const x = parseFloat(display);
            if (op && acc !== null) {
                acc = { '+': acc + x, '-': acc - x, '*': acc * x, '/': acc / x }[op];
                show(acc);
            } else {
                acc = x;
            }
        };
        const press = (k) => {
            if (/[0-9]/.test(k)) {
                display = fresh || display === '0' ? k : display + k;
                fresh = false;
            } else if (k === '.') {
                if (fresh) display = '0';
                if (!display.includes('.')) display += '.';
                fresh = false;
            } else if ('+-*/'.includes(k)) {
                if (!fresh) apply();
                op = k;
                fresh = true;
            } else if (k === '=') {
                apply();
                op = null;
                fresh = true;
            } else if (k === 'C') {
                display = '0'; acc = null; op = null; fresh = true;
            } else if (k === '±') {
                display = String(-parseFloat(display));
            } else if (k === '√') {
                show(Math.sqrt(parseFloat(display)));
                fresh = true;
            }
            win.invalidate();
        };
        const keys = [['7', '8', '9', '/'], ['4', '5', '6', '*'], ['1', '2', '3', '-'], ['0', '.', '=', '+'], ['C', '+/-', 'sqrt', '']];
        keys.forEach((row, j) => row.forEach((k, i) => {
            if (!k) return;
            const key = k === '+/-' ? '±' : k === 'sqrt' ? '√' : k;
            win.add(new Button(8 + i * 54, 48 + j * 40, 48, 32, k, () => press(key)));
        }));
        win.onKey = (ev) => {
            if (!ev.ch) return false;
            const k = ev.ch === '\n' ? '=' : ev.ch.toUpperCase() === 'C' ? 'C' : ev.ch;
            if (/[0-9.+\-*/=C]/.test(k)) { press(k); return true; }
            return false;
        };
        return win;
    },
};
