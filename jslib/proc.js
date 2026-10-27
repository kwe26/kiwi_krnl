// proc: processes, arguments and exit helpers.
import * as sys from 'sys';
import path from 'path';

export const argv = sys.argv;                 // argv[0] is the script path
export const name = path.basename(argv[0] || 'kjs').replace(/\.js$/, '');
export const args = argv.slice(1);
export const pid = sys.getpid();
export const PATH = ['/bin'];

export function which(cmd) {
    if (cmd.includes('/')) return sys.stat(cmd)?.isFile ? cmd : null;
    for (const dir of PATH) {
        const p = `${dir}/${cmd}`;
        if (sys.stat(p)?.isFile) return p;
    }
    return null;
}

// start a program; opts: { stdin, stdout, stderr, pgid }
export function spawn(cmd, args = [], opts = {}) {
    const exe = which(cmd);
    if (!exe) {
        const e = new Error(`${cmd}: command not found`);
        e.code = 127;
        throw e;
    }
    return sys.spawn(exe, [cmd, ...args], opts);
}

export const wait = (p = -1, nohang = false) => sys.wait(p, nohang);

// spawn and wait; returns the exit code
export function run(cmd, args = [], opts = {}) {
    const p = spawn(cmd, args, opts);
    let r;
    do { r = sys.wait(p); } while (r && r.pid !== p);
    return r ? r.code : 0;
}

// run a command and capture its standard output as a string
export function capture(cmd, args = []) {
    const [r, w] = sys.pipe();
    let p;
    try {
        p = spawn(cmd, args, { stdout: w });
    } finally {
        sys.close(w);
    }
    let out = '';
    for (let s; (s = sys.read(r, 4096)) !== '';) out += s;
    sys.close(r);
    const st = sys.wait(p);
    return { code: st ? st.code : 0, out };
}

export const exit = (code = 0) => sys.exit(code);
export const sleep = (ms) => sys.sleep(ms);
export const delay = (ms) => new Promise((res) => setTimeout(res, ms));
export const ps = () => sys.ps();
export const kill = (p, sig = sys.SIGTERM) => sys.kill(p, sig);
export const uptime = () => sys.uptime();

export function die(msg, code = 1) {
    sys.write(2, `${name}: ${msg}\n`);
    sys.exit(code);
}

// wrap a program's main so errors print nicely and set the exit code
export function main(fn) {
    try {
        const r = fn(args);
        if (r instanceof Promise) {
            r.then((c) => sys.exit(typeof c === 'number' ? c : 0),
                   (e) => die(e?.message ?? String(e)));
        } else {
            sys.exit(typeof r === 'number' ? r : 0);
        }
    } catch (e) {
        die(e?.message ?? String(e));
    }
}

export default { argv, name, args, pid, PATH, which, spawn, wait, run, capture, exit, sleep, delay, ps, kill, uptime, die, main };
