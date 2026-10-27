#!/bin/kjs
// kill - terminate processes.  kill [-9] PID...
import * as sys from 'sys';

let args = sys.argv.slice(1), sig = sys.SIGTERM, rc = 0;
if (args[0] && /^-\d+$/.test(args[0])) { sig = -Number(args[0]); args = args.slice(1); }
for (const a of args) {
    try { sys.kill(Number(a), sig); } catch (e) { sys.write(2, `kill: ${a}: ${e.message}\n`); rc = 1; }
}
sys.exit(rc);
