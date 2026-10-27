#!/bin/kjs
// sleep - wait for N seconds (fractions allowed).
import * as sys from 'sys';
sys.sleep(Math.round(Number(sys.argv[1] || 1) * 1000));
