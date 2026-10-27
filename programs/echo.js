#!/bin/kjs
// echo - print arguments.  -n: no newline, -e: interpret \n \t \e
import * as sys from 'sys';

let args = sys.argv.slice(1), nl = true, esc = false;
while (args[0] === '-n' || args[0] === '-e') { if (args[0] === '-n') nl = false; else esc = true; args = args.slice(1); }
let s = args.join(' ');
if (esc) s = s.replace(/\\n/g, '\n').replace(/\\t/g, '\t').replace(/\\e/g, '\x1b');
sys.write(1, s + (nl ? '\n' : ''));
