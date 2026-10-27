// repl: interactive JavaScript shell (run `kjs` with no arguments).
import * as sys from 'sys';
import { LineEditor } from 'readline';
import { color } from 'tty';
import { bytes } from 'fmt';

const globals = () => Object.getOwnPropertyNames(globalThis);

const rl = new LineEditor({
    complete(line) {
        const m = /([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*\.?)$/.exec(line);
        if (!m) return null;
        const expr = m[1];
        const dot = expr.lastIndexOf('.');
        let obj = globalThis, prefix = expr, base = '';
        if (dot >= 0) {
            try { obj = sys.evalScript(expr.slice(0, dot)); } catch { return null; }
            prefix = expr.slice(dot + 1);
            base = expr.slice(0, dot + 1);
        }
        const names = new Set();
        for (let o = obj; o != null; o = Object.getPrototypeOf(o)) {
            for (const n of Object.getOwnPropertyNames(o)) names.add(n);
        }
        const items = [...names].filter(n => n.startsWith(prefix)).sort().map(n => base + n);
        return { start: line.length - expr.length, items };
    },
});

const u = sys.uname();
console.log(color.bold(`kjs`) + ` - QuickJS on ${u.sysname} ${u.release}. ` +
    `Type .help for commands; the ${color.cyan('sys')} object is the kernel API.`);

let buffer = '';
for (;;) {
    const line = rl.read(buffer ? color.gray('... ') : color.green('js> '));
    if (line === null) break;
    if (!buffer && line.startsWith('.')) {
        const cmd = line.trim();
        if (cmd === '.exit' || cmd === '.quit') break;
        if (cmd === '.help') {
            console.log('.exit       leave the REPL\n.mem        engine memory usage\n' +
                '.gc         run the garbage collector\n.clear      reset a multi-line entry\n' +
                'sys.*       kernel API, e.g. sys.ps(), sys.kzstat(), sys.readdir("/")');
        } else if (cmd === '.mem') {
            const m = sys.memoryUsage();
            console.log(`${bytes(m.malloc)} allocated, ${m.objects} objects, ${m.strings} strings, ${m.functions} functions`);
        } else if (cmd === '.gc') {
            sys.gc();
        } else if (cmd === '.clear') {
            buffer = '';
        } else {
            console.log(`unknown command ${cmd}`);
        }
        continue;
    }
    rl.addHistory(line);
    buffer += line + '\n';
    try {
        const v = sys.evalScript(buffer, '<repl>');
        buffer = '';
        if (v !== undefined) console.log(typeof v === 'string' ? color.green(JSON.stringify(v)) : sys.inspect(v));
    } catch (e) {
        // keep reading when the input is merely incomplete
        if (e instanceof SyntaxError && /unexpected end|expecting/.test(e.message) && line.trim() !== '') continue;
        buffer = '';
        console.log(color.red(String(e)));
    }
}
