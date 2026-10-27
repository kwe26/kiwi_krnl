#!/bin/kjs
// kfetch - system summary with the Kiwi logo.
import * as sys from 'sys';
import kz from 'kz';
import fmt from 'fmt';
import { color } from 'tty';

const u = sys.uname();
const z = kz.stats();
const m = sys.memoryUsage();
const logo = [
    '     .--.     ',
    '   .\'_\\/_\'.   ',
    '   \'. /\\ .\'   ',
    '     "||"     ',
    '      || /\\   ',
    '   /\\ ||//\\)  ',
    '  (/\\\\||/     ',
    '     \\||      ',
];
const info = [
    color.bgreen(`root@${u.nodename}`),
    '-'.repeat(14),
    `${color.bgreen('OS')}: ${u.sysname} ${u.release} ${u.machine}`,
    `${color.bgreen('Kernel')}: ${u.version}`,
    `${color.bgreen('Uptime')}: ${fmt.duration(sys.uptime())}`,
    `${color.bgreen('Shell')}: kbash (JavaScript)`,
    `${color.bgreen('Userland')}: QuickJS, ${sys.readdir('/lib/js').length} JS system libraries`,
    `${color.bgreen('Memory')}: ${fmt.bytes(z.ram - z.free)} / ${fmt.bytes(z.ram)}`,
    `${color.bgreen('zram')}: ${fmt.bytes(z.zOrig)} -> ${fmt.bytes(z.z_bytes)} (${fmt.ratio(z.zOrig, z.z_bytes)})`,
    `${color.bgreen('ramfs')}: ${fmt.bytes(z.fs_bytes)} -> ${fmt.bytes(z.fs_zbytes)} (${fmt.ratio(z.fs_bytes, z.fs_zbytes)})`,
    `${color.bgreen('JS heap')}: ${fmt.bytes(m.malloc)} in this process`,
];
const n = Math.max(logo.length, info.length);
for (let i = 0; i < n; i++) console.log(color.green(logo[i] || ' '.repeat(14)) + '  ' + (info[i] || ''));
