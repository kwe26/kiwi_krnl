/*
 * Console: VGA text mode with an ANSI escape interpreter, mirrored to
 * COM1, plus the tty line discipline fed by the PS/2 keyboard and serial.
 */
#include "kernel.h"

#define VGA_W 80
#define VGA_H 25
#define COM1  0x3F8

#define VGA_MEM ((volatile uint16_t *)0xB8000)
static volatile uint16_t *vga = VGA_MEM;
static uint16_t shadow[80 * 25];     /* text screen while graphics is active */
static int cx, cy, saved_x, saved_y;
static uint8_t attr = 0x07, base_attr = 0x07;
static int inverse, bright;

/* ANSI parser state */
static int esc_state;          /* 0 none, 1 got ESC, 2 in CSI */
static int esc_args[8], esc_n, esc_priv;

static void vga_cursor(void)
{
    if (vga != VGA_MEM)
        return;
    uint16_t pos = (uint16_t)(cy * VGA_W + cx);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)pos);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

static uint8_t cur_attr(void)
{
    uint8_t a = attr;
    if (bright) a |= 0x08;
    if (inverse) a = (uint8_t)(((a & 0x0F) << 4) | ((a >> 4) & 0x0F));
    return a;
}

static void clear_cells(int from, int to)
{
    uint16_t blank = (uint16_t)(' ' | (cur_attr() & 0xF0 ? cur_attr() : base_attr) << 8);
    for (int i = from; i < to; i++)
        vga[i] = blank;
}

static void scroll(void)
{
    memmove((void *)vga, (void *)(vga + VGA_W), (VGA_H - 1) * VGA_W * 2);
    clear_cells((VGA_H - 1) * VGA_W, VGA_H * VGA_W);
    cy = VGA_H - 1;
}

static void sgr(int code)
{
    static const uint8_t ansi2vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (code == 0) { attr = base_attr; inverse = bright = 0; }
    else if (code == 1) bright = 1;
    else if (code == 22) bright = 0;
    else if (code == 7) inverse = 1;
    else if (code == 27) inverse = 0;
    else if (code >= 30 && code <= 37) attr = (uint8_t)((attr & 0xF0) | ansi2vga[code - 30]);
    else if (code == 39) attr = (uint8_t)((attr & 0xF0) | (base_attr & 0x0F));
    else if (code >= 40 && code <= 47) attr = (uint8_t)((attr & 0x0F) | ansi2vga[code - 40] << 4);
    else if (code == 49) attr = (uint8_t)((attr & 0x0F) | (base_attr & 0xF0));
    else if (code >= 90 && code <= 97) attr = (uint8_t)((attr & 0xF0) | ansi2vga[code - 90] | 8);
}

static void csi(char cmd)
{
    int a0 = esc_n > 0 ? esc_args[0] : 0;
    int a1 = esc_n > 1 ? esc_args[1] : 0;
    int n = a0 ? a0 : 1;
    if (esc_priv)
        return;                 /* ?25l / ?25h etc: ignore */
    switch (cmd) {
    case 'A': cy -= n; break;
    case 'B': cy += n; break;
    case 'C': cx += n; break;
    case 'D': cx -= n; break;
    case 'G': cx = n - 1; break;
    case 'd': cy = n - 1; break;
    case 'H': case 'f':
        cy = (a0 ? a0 : 1) - 1;
        cx = (a1 ? a1 : 1) - 1;
        break;
    case 'J':
        if (a0 == 2 || a0 == 3) { clear_cells(0, VGA_W * VGA_H); }
        else if (a0 == 0) clear_cells(cy * VGA_W + cx, VGA_W * VGA_H);
        else clear_cells(0, cy * VGA_W + cx + 1);
        break;
    case 'K':
        if (a0 == 0) clear_cells(cy * VGA_W + cx, (cy + 1) * VGA_W);
        else if (a0 == 1) clear_cells(cy * VGA_W, cy * VGA_W + cx + 1);
        else clear_cells(cy * VGA_W, (cy + 1) * VGA_W);
        break;
    case 'm':
        if (esc_n == 0) sgr(0);
        for (int i = 0; i < esc_n; i++) sgr(esc_args[i]);
        break;
    case 's': saved_x = cx; saved_y = cy; break;
    case 'u': cx = saved_x; cy = saved_y; break;
    }
    if (cx < 0) cx = 0;
    if (cx >= VGA_W) cx = VGA_W - 1;
    if (cy < 0) cy = 0;
    if (cy >= VGA_H) cy = VGA_H - 1;
}

static void vga_putc(char c)
{
    if (esc_state == 1) {
        if (c == '[') {
            esc_state = 2;
            esc_n = 0;
            esc_priv = 0;
            for (int i = 0; i < 8; i++) esc_args[i] = 0;
        } else {
            esc_state = 0;
        }
        return;
    }
    if (esc_state == 2) {
        if (c == '?') { esc_priv = 1; return; }
        if (c >= '0' && c <= '9') {
            if (esc_n == 0) esc_n = 1;
            esc_args[esc_n - 1] = esc_args[esc_n - 1] * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (esc_n == 0) esc_n = 1;
            if (esc_n < 8) esc_n++;
            return;
        }
        esc_state = 0;
        csi(c);
        return;
    }
    switch (c) {
    case 033: esc_state = 1; return;
    case '\n': cx = 0; cy++; break;
    case '\r': cx = 0; break;
    case '\b': if (cx > 0) cx--; break;
    case '\t': cx = (cx + 8) & ~7; break;
    case '\a': break;
    default:
        if ((uint8_t)c < 32) break;
        if (cx >= VGA_W) { cx = 0; cy++; }
        if (cy >= VGA_H) scroll();
        vga[cy * VGA_W + cx] = (uint16_t)((uint8_t)c | cur_attr() << 8);
        cx++;
        return;
    }
    if (cx >= VGA_W) { cx = 0; cy++; }
    if (cy >= VGA_H) scroll();
}

/* ---- serial ---- */
void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);       /* DLAB */
    outb(COM1 + 0, 0x01);       /* 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);       /* 8N1 */
    outb(COM1 + 2, 0xC7);       /* FIFO */
    outb(COM1 + 4, 0x0B);       /* DTR RTS OUT2 (IRQ enable) */
    outb(COM1 + 1, 0x01);       /* interrupt on received data */
}

static void serial_putc(char c)
{
    int spin = 100000;
    while (!(inb(COM1 + 5) & 0x20) && spin--)
        ;
    outb(COM1, (uint8_t)c);
}

void console_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n')
            serial_putc('\r');
        serial_putc(c);
        vga_putc(c);
    }
    vga_cursor();
}

/* graphics mode owns VGA memory: keep drawing the text screen off-screen */
void console_shadow(int on)
{
    if (on && vga == VGA_MEM) {
        for (int i = 0; i < VGA_W * VGA_H; i++)
            shadow[i] = VGA_MEM[i];
        vga = shadow;
    } else if (!on && vga != VGA_MEM) {
        vga = VGA_MEM;
        for (int i = 0; i < VGA_W * VGA_H; i++)
            VGA_MEM[i] = shadow[i];
        vga_cursor();
    }
}

void console_init(void)
{
    cx = cy = 0;
    clear_cells(0, VGA_W * VGA_H);
    vga_cursor();
}

/* ---- tty line discipline ---- */
#define TTY_BUF 2048
static char ibuf[TTY_BUF];      /* bytes ready for read() */
static uint32_t ihead, itail;
static char line[512];
static int linelen;
static int tty_raw;
static int tty_eof;
static int tty_fg;
static int in_escape;           /* swallowing an escape sequence (cooked) */
static int tty_chan;

static void ipush(char c)
{
    if (ihead - itail < TTY_BUF)
        ibuf[ihead++ % TTY_BUF] = c;
}

static void echo(const char *s, size_t n)
{
    console_write(s, n);
}

void tty_input(uint8_t c)
{
    if (tty_raw) {
        ipush((char)c);
        wakeup(&tty_chan);
        return;
    }
    if (in_escape) {
        if (c != '[' && c != 'O' && !(c >= '0' && c <= '9') && c != ';')
            in_escape = 0;
        return;
    }
    switch (c) {
    case 033:
        in_escape = 1;
        return;
    case 3:                     /* ^C */
        echo("^C\n", 3);
        linelen = 0;
        if (tty_fg > 1)
            proc_kill_pgrp(tty_fg);
        return;
    case 4:                     /* ^D */
        for (int i = 0; i < linelen; i++)
            ipush(line[i]);
        if (linelen == 0)
            tty_eof = 1;
        linelen = 0;
        wakeup(&tty_chan);
        return;
    case 8: case 127:
        if (linelen > 0) {
            linelen--;
            echo("\b \b", 3);
        }
        return;
    case 21:                    /* ^U */
        while (linelen > 0) {
            linelen--;
            echo("\b \b", 3);
        }
        return;
    case '\r': case '\n':
        echo("\n", 1);
        for (int i = 0; i < linelen; i++)
            ipush(line[i]);
        ipush('\n');
        linelen = 0;
        wakeup(&tty_chan);
        return;
    default:
        if (c == '\t' || c >= 32) {
            if (linelen < (int)sizeof line - 1) {
                line[linelen++] = (char)c;
                echo((char *)&c, 1);
            }
        }
    }
}

int tty_read(char *buf, size_t n)
{
    while (ihead == itail && !tty_eof) {
        sleep_on(&tty_chan);
        if (current->killed)
            return -EINTR;
    }
    if (ihead == itail && tty_eof) {
        tty_eof = 0;
        return 0;
    }
    size_t got = 0;
    while (got < n && itail != ihead) {
        char c = ibuf[itail++ % TTY_BUF];
        buf[got++] = c;
        if (!tty_raw && c == '\n')
            break;
    }
    return (int)got;
}

int tty_ioctl(int req, uint32_t arg)
{
    switch (req) {
    case TTY_SETRAW:
        tty_raw = arg ? 1 : 0;
        linelen = 0;
        in_escape = 0;
        return 0;
    case TTY_GETRAW:
        return tty_raw;
    case TTY_SETFG:
        tty_fg = (int)arg;
        return 0;
    case TTY_GETSIZE:
        return (VGA_H << 16) | VGA_W;
    case TTY_ISATTY:
        return 1;
    }
    return -EINVAL;
}

/* ---- PS/2 keyboard (scancode set 1) ---- */
static const char kmap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
    'b','n','m',',','.','/', 0, '*', 0, ' ',
};
static const char kmap_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
    'B','N','M','<','>','?', 0, '*', 0, ' ',
};
static int shift, ctrl, alt, caps, e0;

static void key_seq(const char *s)
{
    while (*s)
        tty_input((uint8_t)*s++);
}

/* character for a (non-extended) scancode under the current modifiers */
static uint32_t key_char(uint8_t sc)
{
    char c = shift ? kmap_shift[sc] : kmap[sc];
    if (!c)
        return 0;
    if (caps && c >= 'a' && c <= 'z') c -= 32;
    else if (caps && c >= 'A' && c <= 'Z' && shift) c += 32;
    if (ctrl) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
    }
    return (uint8_t)c;
}

void keyboard_irq(void)
{
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { e0 = 1; return; }
    int release = sc & 0x80;
    int ext = e0;
    sc &= 0x7F;
    e0 = 0;

    int modifier = 1;
    if (!ext && (sc == 0x2A || sc == 0x36)) shift = !release;
    else if (sc == 0x1D) ctrl = !release;
    else if (sc == 0x38) alt = !release;
    else if (!ext && sc == 0x3A) { if (!release) caps = !caps; }
    else modifier = 0;

    if (input_grabbed) {
        /* /dev/input owns the keyboard: deliver press and release events */
        uint32_t mods = (shift ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) | (alt ? MOD_ALT : 0);
        uint32_t ch = 0;
        if (!release && !modifier)
            ch = ext ? (sc == 0x1C ? '\n' : 0) : key_char(sc);
        input_key((uint16_t)(sc | (ext ? 0x100 : 0)), !release, mods, ch);
        return;
    }
    if (release || modifier)
        return;
    if (ext) {
        switch (sc) {
        case 0x48: key_seq("\033[A"); return;
        case 0x50: key_seq("\033[B"); return;
        case 0x4D: key_seq("\033[C"); return;
        case 0x4B: key_seq("\033[D"); return;
        case 0x47: key_seq("\033[H"); return;
        case 0x4F: key_seq("\033[F"); return;
        case 0x53: key_seq("\033[3~"); return;
        case 0x49: key_seq("\033[5~"); return;
        case 0x51: key_seq("\033[6~"); return;
        case 0x1C: tty_input('\n'); return;
        }
        return;
    }
    uint32_t c = key_char(sc);
    if (c)
        tty_input((uint8_t)c);
}

void serial_irq(void)
{
    while (inb(COM1 + 5) & 1) {
        uint8_t c = inb(COM1);
        if (c == '\r') c = '\n';
        if (input_grabbed) {
            /* serial typing reaches GUI programs as key events */
            uint16_t code = c == '\n' ? KEY_ENTER : c == 127 || c == 8 ? KEY_BACKSPACE :
                            c == '\t' ? KEY_TAB : c == 27 ? KEY_ESC : 0;
            uint32_t mods = c < 32 && !code ? MOD_CTRL : 0;
            input_key(code, 1, mods, code == KEY_BACKSPACE || code == KEY_ESC ? 0 : c);
            input_key(code, 0, mods, 0);
            continue;
        }
        tty_input(c);
    }
}
