#!/bin/kjs
// uptime - how long the system has been running.
import * as sys from 'sys';
import fmt from 'fmt';

const procs = sys.ps().filter(p => p.pid > 0).length;
console.log(`up ${fmt.duration(sys.uptime())}, ${procs} processes`);
