#!/bin/kjs
// peek - hexdump physical memory.  peek ADDR [LEN]
//   peek 0xB8000 160     the VGA text buffer
//   peek 0x400 64        the BIOS data area
import * as sys from 'sys';
import * as kernel from 'kernel';
import hw from 'hw';

const addr = Number(sys.argv[1] ?? 0x400), len = Number(sys.argv[2] ?? 128);
console.log(hw.hexdump(kernel.peek(addr, len), addr));
