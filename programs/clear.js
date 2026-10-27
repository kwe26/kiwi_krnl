#!/bin/kjs
// clear - clear the terminal.
import * as sys from 'sys';
sys.write(1, '\x1b[2J\x1b[H');
