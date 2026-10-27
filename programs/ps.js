#!/bin/kjs
// ps - list processes with their resident and compressed memory.
import * as sys from 'sys';
import fmt from 'fmt';

const rows = sys.ps().map(p => [p.pid, p.ppid, p.pgid, p.state, fmt.bytes(p.resident * 4096),
    fmt.bytes(p.compressed * 4096), (p.ticks / 100).toFixed(2), p.name]);
console.log(fmt.table(rows, { header: ['PID', 'PPID', 'PGID', 'S', 'RES', 'ZRAM', 'CPU', 'NAME'], align: 'rrrlrrrl' }));
