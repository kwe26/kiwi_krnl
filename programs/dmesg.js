#!/bin/kjs
// dmesg - print the kernel log (read from the /dev/kmsg device).
import * as sys from 'sys';
sys.write(1, sys.readFile('/dev/kmsg'));
