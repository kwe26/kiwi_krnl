#!/bin/kjs
// desktop - the classic Kiwi desktop: taskbar, Start menu, icons and windows.
// Everything here is JavaScript on top of the native 'gfx' module.
//   desktop [-r WIDTHxHEIGHT]      Start > Shut Down to return to the shell
import * as sys from 'sys';
import { Desktop } from 'gui';
import inspector from 'apps/inspector';
import taskmgr from 'apps/taskmgr';
import files from 'apps/files';
import notepad from 'apps/notepad';
import calc from 'apps/calc';
import run from 'apps/run';
import about from 'apps/about';

const ri = sys.argv.indexOf('-r');
const [w, h] = ri > 0 ? sys.argv[ri + 1].split('x').map(Number) : [800, 600];
let d;
try {
    d = new Desktop(w, h);
} catch (e) {
    sys.write(2, `desktop: ${e.message}\n`);
    sys.exit(1);
}
for (const app of [inspector, taskmgr, files, notepad, calc, run, about]) d.register(app);
d.launch('about');
try {
    d.run();
} catch (e) {
    try { (await import('gfx')).close(); } catch {}
    sys.write(2, `desktop crashed: ${e.message}\n${e.stack || ''}\n`);
    sys.exit(1);
}
