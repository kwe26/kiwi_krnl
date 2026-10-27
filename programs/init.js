#!/bin/kjs
// init - PID 1, written in JavaScript: greets, runs /etc/rc, keeps a shell alive.
import * as sys from 'sys';
import { color } from 'tty';
import fmt from 'fmt';
import kz from 'kz';

const u = sys.uname();
const z = kz.stats();
sys.write(1, `\n${color.bgreen('Kiwi')} ${u.release} (${u.machine}) - init is JavaScript (QuickJS), pid ${sys.getpid()}\n` +
    `${fmt.bytes(z.ram)} RAM, ${z.fs_files} files: ${fmt.bytes(z.fs_bytes)} held compressed in ` +
    `${fmt.bytes(z.fs_zbytes)} (${fmt.ratio(z.fs_bytes, z.fs_zbytes)})\n\n`);

try { sys.write(1, sys.readFile('/etc/motd')); } catch {}

function reapUntil(pid) {
    for (;;) {
        const r = sys.wait(-1);
        if (!r || r.pid === pid) return r;
    }
}

if (sys.stat('/etc/rc')) reapUntil(sys.spawn('/bin/kbash', ['kbash', '/etc/rc']));

for (;;) {
    let pid;
    try {
        pid = sys.spawn('/bin/kbash', ['kbash'], { pgid: -1 });
    } catch (e) {
        sys.write(2, `init: cannot start shell: ${e.message}\n`);
        sys.sleep(1000);
        continue;
    }
    const r = reapUntil(pid);
    sys.ttyRaw(false);
    sys.write(1, `\ninit: shell exited (${r ? r.code : '?'}), starting a new one\n\n`);
}
