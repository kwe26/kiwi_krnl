#!/bin/kjs
// uname - print system information.  -a for everything
import * as sys from 'sys';

const u = sys.uname();
const a = sys.argv.includes('-a');
console.log(a ? `${u.sysname} ${u.nodename} ${u.release} ${u.version} ${u.machine}` : u.sysname);
