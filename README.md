# Kiwi

An experimental Unix-like kernel written from scratch for 32-bit x86. It has
heavy memory compression, its own libc and Unix headers, a userland written
mostly in JavaScript (on QuickJS), and a classic-era desktop GUI. The kernel's
low-level internals are open to JavaScript. It boots in QEMU.

```
Kiwi 0.1.0 (i686) - init is JavaScript (QuickJS), pid 1
root@kiwi:~# kjs demo.js
after 150000 objects   resident   19 MB   compressed     0 B
after sleeping         resident  212 KB   compressed   18 MB
after reading it all   resident   18 MB   compressed  764 KB
```

## Quick start

```bash
sudo apt-get install gcc gcc-multilib make qemu-system-x86
make            # kernel, libc, QuickJS, programs, initrd
make run-nox    # boot with the serial console in this terminal (Ctrl-A X quits QEMU)
make run        # VGA window, plus serial on this terminal (type `desktop` for the GUI)
make check      # syntax-check every JS file with a host build of kjs
```

You can pass kernel options with `APPEND`, for example
`make run-nox APPEND="zbudget=8 zproactive=3"`.

| option         | meaning                                                       |
|----------------|---------------------------------------------------------------|
| `zbudget=MB`   | maximum resident user memory before pages are compressed      |
| `zproactive=N` | idle thread compresses pages untouched for N scans (0 = off)  |
| `init=/path`   | first program to run (default `/bin/init`)                    |

`tools/kiwitest.py` boots the system and types commands over the serial
port. It is used for testing:

```bash
tools/kiwitest.py 'ls -l /bin' 'memhog 16' 'memstat'
tools/kiwitest.py --append zbudget=4 --screenshot vga.png 'kfetch'
tools/kiwitest.py --window              # QEMU window that stays open
tools/kiwitest.py --window 'kedit demo.js'   # run commands, then keep the window
```

With `--window`, QEMU keeps running after the scripted commands. The
serial console streams to your terminal, and what you type there goes to
Kiwi too (or type in the window itself). It stops when you close the
window, run `halt`, or press Ctrl-] in the terminal. `--display sdl` picks
a different QEMU backend.

## Memory compression

There are three layers, all in the kernel:

1. **zram: compressed user pages** ([kernel/zram.c](kernel/zram.c))
   - Every user frame is tracked in a reverse map. Resident user memory is
     capped by a budget. When a process needs a frame beyond the budget, a
     clock sweep evicts a page whose accessed bit is clear by compressing it
     into a pool. The PTE then holds a slot number (`PTE_Z`), and the next
     touch page-faults it back in.
   - Pool objects are:
     - **same-filled** pages (zero pages and the like), which store no data;
     - **LZ** pages, compressed by fast KLZ on the allocation path;
     - **LZ+Huffman** pages, compressed by the idle thread, which has CPU
       to spare;
     - **raw** pages that don't compress.
   - Objects are **deduplicated** by content hash with reference counts, so
     identical pages are stored once. That covers several processes
     running the same binary, and `fork()` children, which share compressed
     pages without copying.
   - **Proactive compression**: when nothing is runnable, the idle thread
     ages pages and compresses any that nobody touched for a few scans.
     Sleeping processes shrink on their own. `kjs demo.js` shows a JS heap
     going from 19 MB resident to 200 KB.
2. **Compressed ramfs** ([kernel/ramfs.c](kernel/ramfs.c))
   - A file at rest is only its packed blob, whichever is smallest of
     KLZ+Huffman, KLZ, or raw.
   - Opening a file inflates it into a cache shared by all descriptors. The
     last close re-compresses it if it was modified.
   - `ls -l` shows both the logical size and the bytes actually held in RAM.
3. **Compressed initrd** ([tools/mkinitrd.c](tools/mkinitrd.c))
   - Files are packed once on the host with the same code
     ([common/lz.c](common/lz.c), [common/huff.c](common/huff.c)).
   - The kernel adopts the blobs as they are, so nothing is re-compressed
     at boot.

`memstat` shows all of it, and can tune it at runtime (`-b MB`, `-p N`,
`-c`, `-w`).

## Layout

```
kernel/       the kernel: boot, GDT/IDT, paging, zram, ramfs, processes, syscalls, console,
              /dev, framebuffer, PS/2 mouse, PCI, mmap, kinfo
common/       code shared by kernel, libc and host tools: KLZ, Huffman, heap allocator
include/      the custom headers (listed below)
lib/          libc: crt0, syscalls, stdio (printf with floats), stdlib, math (x87), time,
              plus the graphics library (gfx.c, gfxui.c)
programs/     every program; the Makefile builds whatever is here:
  *.c           one C file = one program        (hello, memhog, gfxdemo)
  <dir>/*.c     multi-file C program            (kjs = QuickJS host)
  *.js          JavaScript program, #!/bin/kjs  (init, kbash, kedit, ls, cat, grep, ...)
jslib/        JavaScript system libraries, installed in /lib/js (apps/: GUI apps)
rootfs/       files copied into the root filesystem (/etc, /home, /tmp)
tools/        mkinitrd (host) and kiwitest.py (QEMU driver)
third_party/quickjs/   QuickJS 2026-06-04 (one small patch, see KIWI_PATCHES.md)
```

## Kernel

- Multiboot boot (QEMU `-kernel`).
- Identity-mapped 4 MB kernel pages. User space is 1 GB to 3 GB, with
  demand-zero heap and stack.
- Preemptive round-robin scheduling. The kernel itself is non-preemptible.
- FPU/SSE state saved per process, because JavaScript numbers are doubles.
- System calls through `int $0x80`, listed in
  [include/kiwi/abi.h](include/kiwi/abi.h):
  - files: `open read write lseek close stat fstat readdir mkdir unlink rename ftruncate`
  - processes: `fork execve spawn waitpid exit kill setpgid getpid getppid`
  - fds: `pipe dup dup2`
  - memory: `sbrk mmap munmap`
  - other: `sleep time uname ioctl ps poweroff`
  - compression: `kzstat kzctl`
  - low level: `kinfo peek portio iopl`
- `execve` runs ELF binaries and `#!` scripts. JS programs are just files
  starting with `#!/bin/kjs`.
- `spawn` creates a process without copying the caller's address space
  (posix_spawn style). The JS shell uses it, since forking a JS heap would
  be wasteful.
- Console: VGA text mode with an ANSI escape interpreter, mirrored to COM1.
  Input comes from the PS/2 keyboard and the serial port, through a tty
  line discipline.
  - cooked mode handles echo, backspace and `^C`, which kills the
    foreground process group;
  - raw mode passes keys through unchanged.
- Devices in `/dev`:

  | device       | what it is                                                     |
  |--------------|----------------------------------------------------------------|
  | `null`       | discards writes, reads nothing                                 |
  | `zero`       | reads zeros                                                    |
  | `kmsg`       | the kernel log                                                 |
  | `tty`        | the console                                                    |
  | `fb0`        | Bochs/QEMU linear framebuffer: ioctl to set the mode, then mmap |
  | `input`      | keyboard and mouse events; opening it grabs the keyboard        |

- Graphics: the text mode's VGA registers, palette and BIOS font are saved
  at boot. Leaving graphics mode puts them back, so the console returns
  intact.

## Headers

All headers are in `include/`. User programs compile with `-nostdinc`
against this directory only.

- **C / POSIX:** `stdio.h stdlib.h string.h ctype.h math.h time.h errno.h
  assert.h setjmp.h limits.h float.h stdint.h stddef.h stdarg.h stdbool.h
  inttypes.h unistd.h fcntl.h dirent.h signal.h termios.h alloca.h
  malloc.h fenv.h`
- **sys/:** `types.h stat.h wait.h time.h ioctl.h utsname.h mman.h`, and
  `io.h` for Linux-style `iopl()` with `inb`/`outb`
- **Low level:** `cpuid.h`; `kiwi/cpu.h` (rdtsc, cpu_vendor, cpu_brand);
  `kiwi/kiwi.h` (compression, ps, spawn, `kinfo`, `kiwi_peek`)
- **Kernel ABI:** `kiwi/abi.h` (syscalls, errno, structs);
  `kiwi/kinfo.h` (introspection: GDT, IDT, IRQs, page tables, PCI, zram,
  font); `kiwi/fb.h` (framebuffer); `kiwi/input.h` (key and mouse events)
- **GUI:**
  - `gfx/gfx.h`: double-buffered drawing, clipping, lines, gradients,
    ASCII-art bitmaps, VGA-font text, software cursor, input events;
  - `gfx/ui.h`: the classic look, with 3D bevels, windows with gradient
    title bars, buttons, check boxes, fields, scroll bars, progress bars and
    group boxes;
  - `gfx/colors.h`: the 16-color palette and the UI scheme.

`gfxdemo` is a C program that uses the GUI headers directly.

## Classic desktop

Run `desktop` (or its alias `startx`). It is JavaScript: [jslib/gui.js](jslib/gui.js)
is the window manager and widget toolkit, and the apps live in
[jslib/apps/](jslib/apps).

- **Desktop:** a teal background with icons (double-click to open), a
  taskbar with Start button, task buttons and clock, and a Start menu with
  a side banner.
- **Windows:** you can drag, raise, minimize to the taskbar, resize with an
  outline, and close them. Keyboard shortcuts: Alt+Tab, Alt+F4, Ctrl+Esc.
- **Widgets:** labels, buttons, check boxes, text fields, list views with
  columns, icons and scroll bars, text areas, tabs, progress bars, graphs
  and canvases.
- **Kernel Inspector** has these tabs:
  - System: kernel image, heap, RAM, uptime, RTC (read through CMOS
    ports), PIT counter;
  - CPU: CPUID, TSC clock, CR0/CR2/CR3/CR4 and EFLAGS decoded;
  - Tables: the live GDT and IDT;
  - IRQs: live interrupt counters;
  - PCI: enumerated from JavaScript through ports 0xCF8/0xCFC;
  - Memory: the E820 map and a zram object-size histogram;
  - Pages: each process's page tables, showing which ranges are resident,
    compressed or device memory;
  - Log: the kernel log.
- **Task Manager:** a process list with resident and compressed memory, a
  live graph of resident memory against zram, End Process and Compress Now.
- **Other apps:** File Manager (browses the ramfs showing compressed
  sizes), Notepad, Calculator, Command Prompt (runs kbash command lines)
  and About.
- **Shut Down:** exits to the shell or powers off.

The mouse is a QEMU PS/2 mouse. Click into the QEMU window to grab it, and
press Ctrl+Alt+G to release it. While the GUI runs, typing on the serial
console also arrives as key events.

## Low-level access from JavaScript

The `kernel` module exposes the machine. `hw` ([jslib/hw.js](jslib/hw.js))
builds on it in plain JavaScript.

```js
import * as kernel from 'kernel';
import hw from 'hw';
kernel.outb(0x70, 0x0A); kernel.inb(0x71);      // raw port I/O (the process gets IOPL 3)
kernel.cpuid(1); kernel.rdtsc(); kernel.eflags();
kernel.system();                                // CR0-CR4, GDTR/IDTR, TSS, kernel layout
kernel.gdt(); kernel.idt();                     // raw descriptor tables -> hw.decodeGdt/decodeIdt
kernel.irqs(); kernel.memoryMap(); kernel.pci(); kernel.zram();
kernel.pagemap(pid);                            // PTEs of any process -> hw.pagemap / hw.pageRanges
kernel.proc(pid);                               // kernel stack, page directory, VMAs, fds
kernel.peek(0xB8000, 160);                      // physical memory
hw.pciScan(); hw.rtc(); hw.pitCounter(); hw.cpuInfo(); hw.tscMHz(sys);
```

Command-line tools built on these:

| tool               | what it shows                                                  |
|--------------------|----------------------------------------------------------------|
| `lspci`            | PCI devices, scanned in JavaScript                             |
| `cpuinfo [-t]`     | CPU details, control registers, and (with `-t`) the GDT and IDT |
| `lsirq`            | interrupt counters                                             |
| `pagemap [pid]`    | a process's page-table ranges                                  |
| `procinfo [pid]`   | the kernel's view of a process                                 |
| `peek ADDR [LEN]`  | a hexdump of physical memory                                   |
| `dmesg`            | the kernel log                                                 |

## The JavaScript userland

`kjs` ([programs/kjs/main.c](programs/kjs/main.c)) embeds QuickJS:

- It runs ES modules and has an event loop for `setTimeout` and promises.
- It exposes the kernel as the native module `sys`.
- Bare imports resolve to the system library: `import fs from 'fs'` loads
  `/lib/js/fs.js`.

```js
import * as sys from 'sys';
import fs from 'fs';
import kz from 'kz';
console.log(fs.readdir('/bin').length, 'programs;', kz.stats().zRatio.toFixed(1) + 'x zram ratio');
const pid = sys.spawn('/bin/ls', ['ls', '-l'], { stdout: 1 });
sys.wait(pid);
```

System libraries ([jslib/](jslib)):

| library    | contents                                                |
|------------|---------------------------------------------------------|
| `fs`       | file system helpers                                     |
| `path`     | path manipulation                                       |
| `tty`      | ANSI output and key decoding                            |
| `fmt`      | tables, sizes, bars, sprintf                            |
| `proc`     | which, spawn, run, capture                              |
| `args`     | getopt-style option parsing                             |
| `readline` | line editor with history and completion                 |
| `kz`       | memory compression statistics and controls              |
| `repl`     | the interactive JavaScript prompt                       |
| `hw`       | PCI, CMOS, PIT, CPUID, and descriptor/page-table decoders, all in JS |
| `gui`      | window manager, widgets and icons for the classic desktop |

Programs:

- `init` is PID 1, written in JS.
- **kbash** is the shell:
  - pipes, `> >> < 2> 2>&1`, `&& || ; &`;
  - `$VAR`, `$(cmd)`, and `$((expr))` evaluated as JavaScript;
  - globbing, quoting, aliases, jobs, history, tab completion;
  - scripts, and `= expr` to evaluate JavaScript inline.
- **kedit** is a full-screen editor with syntax highlighting, search,
  go-to-line, cut and paste, and undo.
- Utilities: `ls cat echo grep wc head tail cp mv rm mkdir touch tree
  hexdump ps kill free memstat uptime uname date sleep clear halt kfetch`.
- `kjs` starts a JavaScript REPL.

The C programs `hello` and `memhog` show the libc side. `memhog 32`
allocates and verifies 32 MB while the kernel compresses it underneath.

## Adding a program

Drop `programs/foo.js` (or `programs/foo.c`) in and run `make`. It becomes
`/bin/foo`. JS files are syntax-checked at build time. C programs compile
with `-nostdinc` against `include/` and link with `lib/`.
