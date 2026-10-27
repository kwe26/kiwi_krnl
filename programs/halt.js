#!/bin/kjs
// halt - power off the machine.
import * as sys from 'sys';
sys.write(1, 'halting...\n');
sys.poweroff();
