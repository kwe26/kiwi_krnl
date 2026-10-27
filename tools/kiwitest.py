#!/usr/bin/env python3
"""Boot Kiwi in QEMU and drive the serial console.

    tools/kiwitest.py [--append ARGS] [--timeout S] 'cmd1' 'cmd2' ...

Each command is typed at the kbash prompt after the previous one finished.
A command starting with 'keys:' sends raw keystrokes instead (escapes like
\\x11 are decoded), useful for full-screen programs such as kedit.
'mon:CMD' sends a QEMU monitor command (e.g. 'mon:mouse_move 20 10',
'mon:mouse_button 1', 'mon:sendkey esc'); 'shot:FILE.png' saves the screen.
'click:X,Y' / 'dclick:X,Y' / 'move:X,Y' put the mouse at an absolute screen
position (by first pushing it into the top-left corner) for GUI tests.
Prints the console transcript with ANSI sequences stripped.

    tools/kiwitest.py --window ['cmd' ...]

Opens QEMU in its own VGA window and keeps it running after the commands:
serial output streams to this terminal, and what you type here goes to the
serial console.  Close the window, run `halt`, or press Ctrl-] to quit.
"""
import argparse
import os
import re
import select
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANSI = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]')
PROMPT = re.compile(r'root@kiwi[^\n]*[#$] ')


def monitor(sock_path, command):
    import socket
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    s.sendall(command.encode() + b'\n')
    time.sleep(0.2)
    s.close()


def screenshot(sock_path, png_path):
    import struct, zlib
    ppm = os.path.abspath(png_path) + '.ppm'
    monitor(sock_path, f'screendump {ppm}')
    for _ in range(50):
        if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
            break
        time.sleep(0.1)
    time.sleep(0.3)
    data = open(ppm, 'rb').read()
    # parse binary PPM (P6) header
    parts, i = [], 0
    while len(parts) < 4:
        while data[i:i + 1].isspace():
            i += 1
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    w, h = int(parts[1]), int(parts[2])
    pix = data[i + 1:]
    raw = b''.join(b'\0' + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(png_path, 'wb').write(png)
    os.unlink(ppm)


def interact(p):
    """Keep QEMU running: stream serial output, forward terminal keystrokes."""
    import termios, tty
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd) if os.isatty(fd) else None
    if saved:
        tty.setraw(fd)
        os.write(1, b'\r\n[kiwitest] window mode: type here or in the QEMU window; Ctrl-] quits\r\n')
    try:
        while p.poll() is None:
            r, _, _ = select.select([p.stdout, fd] if saved else [p.stdout], [], [], 0.1)
            if p.stdout in r:
                chunk = os.read(p.stdout.fileno(), 65536)
                if not chunk:
                    break
                os.write(1, chunk)
            if saved and fd in r:
                data = os.read(fd, 1024)
                if b'\x1d' in data:            # Ctrl-]
                    p.kill()
                    break
                p.stdin.write(data)
                p.stdin.flush()
    finally:
        if saved:
            termios.tcsetattr(fd, termios.TCSADRAIN, saved)
    p.wait()
    print(f'\n[kiwitest] QEMU exited ({p.returncode})')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--append', default='')
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('--mem', default='128M')
    ap.add_argument('--raw', action='store_true', help='keep ANSI sequences')
    ap.add_argument('--screenshot', help='save the VGA screen as PNG at the end')
    ap.add_argument('--window', action='store_true',
                    help='show the QEMU window and keep running until it is closed')
    ap.add_argument('--display', default=None, help='QEMU display backend for --window (gtk, sdl, ...)')
    ap.add_argument('cmds', nargs='*')
    a = ap.parse_args()

    qemu = ['qemu-system-i386', '-m', a.mem, '-kernel', f'{ROOT}/build/kiwi.elf',
            '-initrd', f'{ROOT}/build/initrd.img', '-serial', 'stdio',
            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04', '-no-reboot',
            '-append', a.append]
    if os.access('/dev/kvm', os.W_OK):
        qemu.insert(1, '-enable-kvm')
    if not a.window:
        qemu += ['-display', 'none']
    elif a.display:
        qemu += ['-display', a.display]
    mon = None
    if a.screenshot or any(c.startswith(('mon:', 'shot:')) for c in a.cmds):
        mon = f'/tmp/kiwitest-{os.getpid()}.sock'
        qemu += ['-monitor', f'unix:{mon},server,nowait']
    p = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = b''

    def pump(until, timeout):
        nonlocal out
        end = time.time() + timeout
        while time.time() < end:
            r, _, _ = select.select([p.stdout], [], [], 0.05)
            if r:
                chunk = os.read(p.stdout.fileno(), 65536)
                if not chunk:
                    return False
                out += chunk
                if a.window:
                    os.write(1, chunk)
                text = ANSI.sub('', out.decode('utf-8', 'replace').replace('\r', ''))
                if until and until(text):
                    return True
            elif p.poll() is not None:
                return False
        return False

    ok = True
    def prompts():
        return len(PROMPT.findall(ANSI.sub('', out.decode('utf-8', 'replace'))))

    if not pump(lambda t: prompts() >= 1, a.timeout):
        ok = False
    for cmd in a.cmds:
        if not ok:
            break
        n = prompts()
        if cmd.startswith('keys:'):
            data = cmd[5:].encode().decode('unicode_escape').encode('latin1')
            for b in data:
                p.stdin.write(bytes([b]))
                p.stdin.flush()
                time.sleep(0.01)
            pump(None, 0.8)
            continue
        if cmd.startswith('mon:'):
            monitor(mon, cmd[4:])
            pump(None, 0.3)
            continue
        if cmd.split(':')[0] in ('click', 'dclick', 'move'):
            kind, pos = cmd.split(':')
            x, y = (int(v) for v in pos.split(','))
            for _ in range(10):
                monitor(mon, 'mouse_move -100 -100')
            while x or y:
                sx, sy = max(-100, min(100, x)), max(-100, min(100, y))
                monitor(mon, f'mouse_move {sx} {sy}')
                x, y = x - sx, y - sy
            for _ in range({'click': 1, 'dclick': 2, 'move': 0}[kind]):
                monitor(mon, 'mouse_button 1')
                monitor(mon, 'mouse_button 0')
            pump(None, 0.6)
            continue
        if cmd.startswith('shot:'):
            screenshot(mon, cmd[5:])
            continue
        if cmd.startswith('sleep:'):
            pump(None, float(cmd[6:]))
            continue
        for ch in (cmd + '\r').encode():
            p.stdin.write(bytes([ch]))
            p.stdin.flush()
            time.sleep(0.002)
        if not pump(lambda t: prompts() > n, a.timeout):
            ok = False
    if a.screenshot:
        screenshot(mon, a.screenshot)
    if a.window:
        return interact(p)
    p.kill()
    text = out.decode('utf-8', 'replace').replace('\r', '')
    print(text if a.raw else ANSI.sub('', text))
    if not ok:
        print('\n[kiwitest] timed out or QEMU exited', file=sys.stderr)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
