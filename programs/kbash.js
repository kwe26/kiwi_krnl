#!/bin/kjs
// kbash - the Kiwi shell, written in JavaScript.
//
//   pipelines  a | b | c          lists     a ; b   a && b   a || b   a &
//   redirects  > file  >> file  < file  2> file  2>&1
//   expansion  $VAR ${VAR} $? $$ $(command) ~ * ?   quoting '...' "..." \x
//   builtins   cd pwd echo exit export set unset alias unalias source .
//              history jobs wait type help clear time true false = (JS eval)
import * as sys from 'sys';
import fs from 'fs';
import path from 'path';
import tty, { color } from 'tty';
import { LineEditor } from 'readline';
import { columns } from 'fmt';

const vars = {
    HOME: '/home', PATH: '/bin', SHELL: '/bin/kbash', USER: 'root', HOSTNAME: 'kiwi',
    PS1: '\\e[1;32m\\u@\\h\\e[0m:\\e[1;34m\\w\\e[0m\\$ ',
};
const aliases = {};
const jobs = new Map();         // pid -> { id, cmd }
let nextJob = 1;
let status = 0;
const HISTFILE = '/home/.kbash_history';

class ShellError extends Error {}

// ------------------------------------------------------------------ lexer
const OPS = ['2>&1', '2>>', '&&', '||', '>>', '2>', '|', '&', ';', '<', '>'];

// A word is a list of parts: literal strings, or functions evaluated when
// the command runs (so `N=1; echo $N` and `false; echo $?` work).
function readVar(src, i) {
    // src[i] === '$'; returns [part, nextIndex]
    const c = src[i + 1];
    if (c === '?') return [() => String(status), i + 2];
    if (c === '$') return [() => String(sys.getpid()), i + 2];
    if (c === '#') return [() => String(Number(vars['#'] || 0)), i + 2];
    if (c === '{') {
        const j = src.indexOf('}', i + 2);
        if (j < 0) throw new ShellError('bad substitution');
        const name = src.slice(i + 2, j);
        return [() => vars[name] ?? '', j + 1];
    }
    if (c === '(' && src[i + 2] === '(') {
        // $(( arithmetic )), evaluated as JavaScript with shell variables
        const j = src.indexOf('))', i + 3);
        if (j < 0) throw new ShellError('unterminated $((');
        const raw = src.slice(i + 3, j);
        return [() => {
            const expr = raw.replace(/\$?\b([A-Za-z_]\w*)\b/g, (m, name) => String(Number(vars[name] ?? 0)));
            try {
                return String(sys.evalScript(`(${expr})`, '<arith>'));
            } catch (e) {
                throw new ShellError(`arithmetic: ${e.message}`);
            }
        }, j + 2];
    }
    if (c === '(') {
        let depth = 1, j = i + 2;
        for (; j < src.length && depth; j++) {
            if (src[j] === '(') depth++;
            else if (src[j] === ')') depth--;
        }
        if (depth) throw new ShellError('unterminated $(');
        const inner = src.slice(i + 2, j - 1);
        return [() => captureOutput(inner), j];
    }
    const m = /^[A-Za-z_][A-Za-z0-9_]*|^[0-9]/.exec(src.slice(i + 1));
    if (!m) return ['$', i + 1];
    return [() => vars[m[0]] ?? '', i + 1 + m[0].length];
}

const wordValue = (w) => w.parts.map(p => (typeof p === 'function' ? p() : p)).join('');
const wordLiteral = (w) => (w.parts.every(p => typeof p === 'string') ? w.parts.join('') : null);

function lex(src) {
    const toks = [];
    let i = 0;
    while (i < src.length) {
        const c = src[i];
        if (c === ' ' || c === '\t' || c === '\n') { i++; continue; }
        if (c === '#') break;
        const op = OPS.find(o => src.startsWith(o, i));
        if (op) {
            toks.push({ t: 'op', v: op });
            i += op.length;
            continue;
        }
        const parts = [];
        let glob = false, quoted = false, fromVar = false;
        const lit = (s) => {
            if (typeof parts[parts.length - 1] === 'string') parts[parts.length - 1] += s;
            else parts.push(s);
        };
        while (i < src.length && !' \t\n|&;<>'.includes(src[i])) {
            const ch = src[i];
            if (ch === "'") {
                const j = src.indexOf("'", i + 1);
                if (j < 0) throw new ShellError('unterminated quote');
                lit(src.slice(i + 1, j));
                i = j + 1;
                quoted = true;
            } else if (ch === '"') {
                i++;
                while (i < src.length && src[i] !== '"') {
                    if (src[i] === '\\' && '"\\$`'.includes(src[i + 1])) { lit(src[i + 1]); i += 2; }
                    else if (src[i] === '$') { const [v, j] = readVar(src, i); parts.push(v); i = j; }
                    else lit(src[i++]);
                }
                if (i >= src.length) throw new ShellError('unterminated quote');
                i++;
                quoted = true;
            } else if (ch === '\\') {
                lit(src[i + 1] ?? '');
                i += 2;
                quoted = true;
            } else if (ch === '$') {
                const [v, j] = readVar(src, i);
                parts.push(v);
                i = j;
                fromVar = true;
            } else if (ch === '~' && !parts.length && (i + 1 >= src.length || '/ \t'.includes(src[i + 1]))) {
                parts.push(() => vars.HOME);
                i++;
            } else {
                if (ch === '*' || ch === '?') glob = true;
                lit(ch);
                i++;
            }
        }
        toks.push({ t: 'word', parts, glob: glob && !quoted, quoted, fromVar });
    }
    return toks;
}

// ------------------------------------------------------------------ parser
// list := pipeline ((';' | '&&' | '||' | '&') pipeline)*
function parse(toks) {
    const list = [];
    let cmds = [], cmd = newCmd();
    function newCmd() { return { words: [], redirs: [] }; }
    function endCmd() {
        if (!cmd.words.length && !cmd.redirs.length) throw new ShellError('syntax error near unexpected token');
        cmds.push(cmd);
        cmd = newCmd();
    }
    for (let i = 0; i < toks.length; i++) {
        const tk = toks[i];
        if (tk.t === 'word') { cmd.words.push(tk); continue; }
        switch (tk.v) {
            case '|':
                endCmd();
                break;
            case ';': case '&&': case '||': case '&':
                if (!cmd.words.length && !cmds.length) {
                    if (tk.v === ';') continue;
                    throw new ShellError(`syntax error near '${tk.v}'`);
                }
                endCmd();
                list.push({ cmds, op: tk.v });
                cmds = [];
                break;
            case '2>&1':
                cmd.redirs.push({ fd: 2, mode: 'dup', target: 1 });
                break;
            default: {
                const target = toks[++i];
                if (!target || target.t !== 'word') throw new ShellError(`missing file after '${tk.v}'`);
                const fd = tk.v.startsWith('2') ? 2 : tk.v === '<' ? 0 : 1;
                const mode = tk.v === '<' ? 'r' : tk.v.endsWith('>>') ? 'a' : 'w';
                cmd.redirs.push({ fd, mode, target });
            }
        }
    }
    if (cmd.words.length || cmd.redirs.length) endCmd();
    if (cmds.length) list.push({ cmds, op: ';' });
    return list;
}

// ------------------------------------------------------------------ expansion
function globToRegex(pat) {
    return new RegExp('^' + pat.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.') + '$');
}

function expandGlob(word) {
    const dir = word.includes('/') ? path.dirname(word) : '.';
    const base = path.basename(word);
    if (/[*?]/.test(dir)) return [word];
    let entries;
    try { entries = sys.readdir(dir); } catch { return [word]; }
    const re = globToRegex(base);
    const hits = entries.map(e => e.name).filter(n => re.test(n) && (base.startsWith('.') || !n.startsWith('.')))
        .sort().map(n => (dir === '.' && !word.startsWith('./') ? n : path.join(dir, n)));
    return hits.length ? hits : [word];
}

function expandWords(words) {
    const out = [];
    for (const w of words) {
        const v = wordValue(w);
        if (v === '' && w.fromVar && !w.quoted) continue;     // empty $VAR vanishes
        if (w.glob) out.push(...expandGlob(v));
        else out.push(v);
    }
    return out;
}

function expandAlias(words, seen = new Set()) {
    if (!words.length) return words;
    const name = wordLiteral(words[0]);
    if (name === null || aliases[name] === undefined || seen.has(name)) return words;
    seen.add(name);
    const repl = lex(aliases[name]).filter(t => t.t === 'word');
    return expandAlias([...repl, ...words.slice(1)], seen);
}

function findCommand(name) {
    if (name.includes('/')) return sys.stat(name)?.isFile ? name : null;
    for (const dir of (vars.PATH || '/bin').split(':')) {
        const p = `${dir}/${name}`;
        if (sys.stat(p)?.isFile) return p;
    }
    return null;
}

// ------------------------------------------------------------------ builtins
const out = (io, s) => sys.write(io.out, s);
const err = (io, s) => sys.write(io.err, s);

const builtins = {
    cd(args, io) {
        const dir = args[0] === undefined ? vars.HOME : args[0] === '-' ? (vars.OLDPWD || '.') : args[0];
        const old = sys.getcwd();
        sys.chdir(dir);
        vars.OLDPWD = old;
        vars.PWD = sys.getcwd();
        return 0;
    },
    pwd(args, io) { out(io, sys.getcwd() + '\n'); return 0; },
    echo(args, io) {
        let nl = true, esc = false;
        while (args[0] === '-n' || args[0] === '-e') { if (args[0] === '-n') nl = false; else esc = true; args = args.slice(1); }
        let s = args.join(' ');
        if (esc) s = s.replace(/\\n/g, '\n').replace(/\\t/g, '\t').replace(/\\e/g, '\x1b').replace(/\\\\/g, '\\');
        out(io, s + (nl ? '\n' : ''));
        return 0;
    },
    exit(args) {
        saveHistory();
        sys.exit(args[0] !== undefined ? Number(args[0]) : status);
    },
    true: () => 0,
    false: () => 1,
    export(args, io) { return builtins.set(args, io); },
    set(args, io) {
        if (!args.length) {
            for (const k of Object.keys(vars).sort()) if (!/^\d+$|^#$/.test(k)) out(io, `${k}=${vars[k]}\n`);
            return 0;
        }
        for (const a of args) {
            const eq = a.indexOf('=');
            if (eq > 0) vars[a.slice(0, eq)] = a.slice(eq + 1);
        }
        return 0;
    },
    unset(args) { for (const a of args) delete vars[a]; return 0; },
    alias(args, io) {
        if (!args.length) {
            for (const [k, v] of Object.entries(aliases)) out(io, `alias ${k}='${v}'\n`);
            return 0;
        }
        for (const a of args) {
            const eq = a.indexOf('=');
            if (eq > 0) aliases[a.slice(0, eq)] = a.slice(eq + 1);
            else if (aliases[a] !== undefined) out(io, `alias ${a}='${aliases[a]}'\n`);
            else { err(io, `alias: ${a}: not found\n`); return 1; }
        }
        return 0;
    },
    unalias(args) { for (const a of args) delete aliases[a]; return 0; },
    source(args, io) {
        if (!args.length) { err(io, 'source: filename required\n'); return 2; }
        return runScript(args[0], args.slice(1));
    },
    history(args, io) {
        if (args[0] === '-c') { rl.history.length = 0; return 0; }
        if (args[0] === '-w') { saveHistory(); return 0; }
        rl.history.forEach((h, i) => out(io, `${String(i + 1).padStart(4)}  ${h}\n`));
        return 0;
    },
    jobs(args, io) {
        for (const [pid, j] of jobs) out(io, `[${j.id}] ${pid} running  ${j.cmd}\n`);
        return 0;
    },
    wait(args) {
        while (jobs.size) {
            const r = sys.wait(-1);
            if (!r) break;
            jobs.delete(r.pid);
        }
        return 0;
    },
    type(args, io) {
        let rc = 0;
        for (const a of args) {
            if (aliases[a] !== undefined) out(io, `${a} is aliased to '${aliases[a]}'\n`);
            else if (builtins[a]) out(io, `${a} is a shell builtin\n`);
            else if (findCommand(a)) out(io, `${a} is ${findCommand(a)}\n`);
            else { err(io, `type: ${a}: not found\n`); rc = 1; }
        }
        return rc;
    },
    clear(args, io) { out(io, tty.clearScreen); return 0; },
    help(args, io) {
        const progs = (() => { try { return sys.readdir('/bin').map(e => e.name).sort(); } catch { return []; } })();
        out(io, color.bold('kbash') + ' - the Kiwi shell (JavaScript on QuickJS)\n\n' +
            color.cyan('builtins: ') + Object.keys(builtins).sort().join(' ') + '\n\n' +
            color.cyan('programs in /bin:\n') + columns(progs, sys.ttySize().cols) + '\n\n' +
            'syntax: a | b, a > f, a >> f, a < f, 2>&1, a && b, a || b, a ; b, a &,\n' +
            '        $VAR $(cmd) ~ * ?, NAME=value, "= 2**10" evaluates JavaScript\n');
        return 0;
    },
    time(args, io) {
        const t0 = sys.uptime();
        const rc = args.length ? runLine(args.map(quote).join(' ')) : 0;
        err(io, `\nreal ${((sys.uptime() - t0) / 1000).toFixed(3)}s\n`);
        return rc;
    },
    '='(args, io) {
        try {
            const v = sys.evalScript(args.join(' '), '<kbash>');
            out(io, (typeof v === 'string' ? v : sys.inspect(v)) + '\n');
            return 0;
        } catch (e) {
            err(io, `${e}\n`);
            return 1;
        }
    },
};
builtins['.'] = builtins.source;

const quote = (s) => (/^[\w@%+=:,./-]+$/.test(s) ? s : `'${s.replace(/'/g, `'\\''`)}'`);

// ------------------------------------------------------------------ execution
function openRedirs(redirs, io, toClose) {
    for (const r of redirs) {
        if (r.mode === 'dup') { io[r.fd === 2 ? 'err' : 'out'] = r.target === 1 ? io.out : io.err; continue; }
        const flags = r.mode === 'r' ? sys.O_RDONLY
            : sys.O_WRONLY | sys.O_CREAT | (r.mode === 'a' ? sys.O_APPEND : sys.O_TRUNC);
        const fd = sys.open(wordValue(r.target), flags);
        toClose.push(fd);
        if (r.fd === 0) io.in = fd; else if (r.fd === 1) io.out = fd; else io.err = fd;
    }
}

function runPipeline(cmds, background, text) {
    cmds = cmds.map(c => ({ ...c, words: expandAlias(c.words) }));

    // a lone builtin (or assignment) runs inside the shell
    const first = cmds[0];
    if (cmds.length === 1) {
        const words = expandWords(first.words);
        let k = 0;
        while (k < words.length && /^[A-Za-z_]\w*=/.test(words[k])) k++;
        if (k === words.length) {
            builtins.set(words);
            return 0;
        }
        if (k > 0) words.splice(0, k);          // NAME=v cmd: no env to pass
        if (builtins[words[0]]) {
            const io = { in: 0, out: 1, err: 2 }, toClose = [];
            try {
                openRedirs(first.redirs, io, toClose);
                return builtins[words[0]](words.slice(1), io) ?? 0;
            } finally {
                toClose.forEach(fd => sys.close(fd));
            }
        }
        first.argv = words;
    }

    const pids = [];
    let pgid = 0, prevRead = -1, rc = 0;
    for (let i = 0; i < cmds.length; i++) {
        const c = cmds[i];
        const argv = c.argv || expandWords(c.words);
        const io = { in: prevRead >= 0 ? prevRead : 0, out: 1, err: 2 }, toClose = [];
        let pipeW = -1, nextRead = -1;
        if (i < cmds.length - 1) {
            [nextRead, pipeW] = sys.pipe();
            io.out = pipeW;
        }
        try {
            openRedirs(c.redirs, io, toClose);
            const name = argv[0];
            const exe = name === undefined ? null : builtins[name] ? null : findCommand(name);
            if (name !== undefined && builtins[name]) {
                rc = builtins[name](argv.slice(1), io) ?? 0;   // builtin inside a pipeline
            } else if (!exe) {
                if (name !== undefined) sys.write(2, `kbash: ${name}: command not found\n`);
                rc = 127;
            } else {
                const pid = sys.spawn(exe, argv, { stdin: io.in, stdout: io.out, stderr: io.err, pgid: pgid || -1 });
                if (!pgid) pgid = pid;
                pids.push(pid);
            }
        } catch (e) {
            sys.write(2, `kbash: ${e.message}\n`);
            rc = 1;
        } finally {
            toClose.forEach(fd => sys.close(fd));
            if (prevRead >= 0) sys.close(prevRead);
            if (pipeW >= 0) sys.close(pipeW);
        }
        prevRead = nextRead;
    }
    if (!pids.length) return rc;

    if (background) {
        const id = nextJob++;
        for (const pid of pids) jobs.set(pid, { id, cmd: text });
        sys.write(2, `[${id}] ${pids[pids.length - 1]}\n`);
        return 0;
    }
    sys.ttySetFg(pgid);
    try {
        for (const pid of pids) {
            const r = sys.wait(pid);
            if (pid === pids[pids.length - 1]) rc = r ? r.code : 0;
            if (r && r.signal && r.signal !== 2 && r.signal !== 13)
                sys.write(2, `kbash: process ${pid} terminated by signal ${r.signal}\n`);
        }
    } finally {
        sys.ttySetFg(0);
    }
    return rc;
}

function runList(list, text) {
    let rc = status, skip = false;
    for (let i = 0; i < list.length; i++) {
        const item = list[i];
        if (!skip) {
            rc = runPipeline(item.cmds, item.op === '&', text);
            status = rc;
        }
        // decide whether the next pipeline runs
        skip = (item.op === '&&' && rc !== 0) || (item.op === '||' && rc === 0);
    }
    return rc;
}

function runLine(line) {
    const js = /^\s*=\s(.*)$/.exec(line);
    if (js) return (status = builtins['=']([js[1]], { in: 0, out: 1, err: 2 }));
    try {
        const list = parse(lex(line));
        return list.length ? runList(list, line.trim()) : status;
    } catch (e) {
        sys.write(2, `kbash: ${e.message}\n`);
        return (status = e instanceof ShellError ? 2 : 1);
    }
}

function captureOutput(cmdline) {
    const [r, w] = sys.pipe();
    let pid;
    try {
        pid = sys.spawn('/bin/kbash', ['kbash', '-c', cmdline], { stdout: w });
    } finally {
        sys.close(w);
    }
    let s = '';
    for (let c; (c = sys.read(r, 4096)) !== '';) s += c;
    sys.close(r);
    sys.wait(pid);
    return s.replace(/\n+$/, '');
}

function runScript(file, args = []) {
    let text;
    try { text = sys.readFile(file); } catch (e) { sys.write(2, `kbash: ${e.message}\n`); return 127; }
    const saved = { ...vars };
    args.forEach((a, i) => { vars[String(i + 1)] = a; });
    vars['#'] = String(args.length);
    vars['0'] = file;
    let rc = 0;
    // join lines ending in a backslash
    for (const line of text.replace(/\\\n/g, ' ').split('\n')) {
        if (line.startsWith('#!')) continue;
        rc = runLine(line);
    }
    for (const k of ['#', '0', ...args.map((_, i) => String(i + 1))]) delete vars[k];
    Object.assign(vars, Object.fromEntries(Object.entries(saved).filter(([k]) => /^\d+$|^#$/.test(k))));
    return rc;
}

// ------------------------------------------------------------------ interactive
function prompt() {
    const cwd = sys.getcwd();
    const w = cwd === vars.HOME ? '~' : cwd.startsWith(vars.HOME + '/') ? '~' + cwd.slice(vars.HOME.length) : cwd;
    return (vars.PS1 || '$ ')
        .replace(/\\e/g, '\x1b').replace(/\\u/g, vars.USER).replace(/\\h/g, vars.HOSTNAME)
        .replace(/\\w/g, w).replace(/\\W/g, path.basename(cwd) || '/').replace(/\\\$/g, '#')
        .replace(/\\\?/g, String(status));
}

function complete(line) {
    const m = /(\S*)$/.exec(line);
    const word = m[1];
    const before = line.slice(0, line.length - word.length);
    const prev = before.trimEnd();
    const isCmd = prev === '' || /[|;&]$/.test(prev);
    const start = line.length - word.length;
    if (isCmd && !word.includes('/')) {
        const names = new Set([...Object.keys(builtins), ...Object.keys(aliases)]);
        for (const dir of (vars.PATH || '/bin').split(':')) {
            try { for (const e of sys.readdir(dir)) names.add(e.name); } catch {}
        }
        return { start, items: [...names].filter(n => n.startsWith(word)).sort() };
    }
    const expanded = word.startsWith('~') ? vars.HOME + word.slice(1) : word;
    const slash = expanded.lastIndexOf('/');
    const dir = slash >= 0 ? expanded.slice(0, slash + 1) : '';
    const prefix = expanded.slice(slash + 1);
    let entries = [];
    try { entries = sys.readdir(dir || '.'); } catch {}
    const items = entries.filter(e => e.name.startsWith(prefix) && (prefix.startsWith('.') || !e.name.startsWith('.')))
        .map(e => (word.startsWith('~') ? '~' + (dir + e.name).slice(vars.HOME.length) : dir + e.name) + (e.type === 'dir' ? '/' : ''))
        .sort();
    return { start, items };
}

const rl = new LineEditor({ complete });

function loadHistory() {
    try { rl.history.push(...sys.readFile(HISTFILE).split('\n').filter(Boolean).slice(-200)); } catch {}
}

function saveHistory() {
    try { sys.writeFile(HISTFILE, rl.history.slice(-200).join('\n') + '\n'); } catch {}
}

function reapJobs() {
    for (;;) {
        const r = sys.wait(-1, true);
        if (!r) break;
        const j = jobs.get(r.pid);
        if (j) {
            jobs.delete(r.pid);
            if (![...jobs.values()].some(x => x.id === j.id))
                sys.write(2, `[${j.id}] done (${r.code})  ${j.cmd}\n`);
        }
    }
}

// ------------------------------------------------------------------ main
const argv = sys.argv.slice(1);
if (argv[0] === '-c') {
    sys.exit(runLine(argv.slice(1).join(' ')));
} else if (argv.length) {
    sys.exit(runScript(argv[0], argv.slice(1)));
}

if (fs.exists('/etc/kbashrc')) runScript('/etc/kbashrc');
if (fs.exists(vars.HOME + '/.kbashrc')) runScript(vars.HOME + '/.kbashrc');
loadHistory();
try { sys.chdir(vars.HOME); } catch {}

for (;;) {
    reapJobs();
    const line = rl.read(prompt());
    if (line === null) {
        if (!sys.isatty(0)) break;
        sys.write(1, 'exit\n');
        break;
    }
    if (!line.trim()) continue;
    rl.addHistory(line);
    status = runLine(line);
}
saveHistory();
sys.exit(status);
