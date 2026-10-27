/*
 * Input events: PS/2 mouse driver plus a queue of key and mouse events
 * read through /dev/input.  While /dev/input is open the keyboard is
 * grabbed (see keyboard_irq in console.c).
 */
#include "kernel.h"

#define QSIZE 256
int input_grabbed;
static int grab_count;
static struct kiwi_input_event q[QSIZE];
static uint32_t qhead, qtail;
static int input_chan;
static uint8_t pkt[3];
static int pkt_len;

static void push(const struct kiwi_input_event *e)
{
    if (qhead - qtail >= QSIZE)
        qtail++;                                /* drop the oldest */
    q[qhead++ % QSIZE] = *e;
    wakeup(&input_chan);
}

void input_grab(int on)
{
    grab_count += on ? 1 : -1;
    if (grab_count < 0)
        grab_count = 0;
    input_grabbed = grab_count > 0;
    if (!input_grabbed)
        qhead = qtail = 0;
}

void input_key(uint16_t code, int down, uint32_t mods, uint32_t ch)
{
    struct kiwi_input_event e = { ticks * (1000 / HZ), EV_KEY, code, down, 0, 0, mods, ch };
    push(&e);
}

static void mouse_event(int dx, int dy, uint32_t buttons)
{
    if (!input_grabbed)
        return;
    if (qhead != qtail) {                       /* merge pure motion */
        struct kiwi_input_event *last = &q[(qhead - 1) % QSIZE];
        if (last->type == EV_MOUSE && last->buttons == buttons) {
            last->dx += dx;
            last->dy += dy;
            wakeup(&input_chan);
            return;
        }
    }
    struct kiwi_input_event e = { ticks * (1000 / HZ), EV_MOUSE, 0, 0, dx, dy, buttons, 0 };
    push(&e);
}

void mouse_irq(void)
{
    uint8_t b = inb(0x60);
    if (pkt_len == 0 && !(b & 0x08))
        return;                                 /* resynchronise */
    pkt[pkt_len++] = b;
    if (pkt_len < 3)
        return;
    pkt_len = 0;
    if (pkt[0] & 0xC0)
        return;                                 /* overflow */
    int dx = (int)pkt[1] - (int)((pkt[0] << 4) & 0x100);
    int dy = (int)pkt[2] - (int)((pkt[0] << 3) & 0x100);
    mouse_event(dx, -dy, pkt[0] & 7);
}

static void ps2_wait_write(void)
{
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
}

static void ps2_wait_read(void)
{
    for (int i = 0; i < 100000 && !(inb(0x64) & 1); i++)
        ;
}

static void mouse_cmd(uint8_t v)
{
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, v);
    ps2_wait_read();
    inb(0x60);                                  /* ACK */
}

void input_init(void)
{
    ps2_wait_write();
    outb(0x64, 0xA8);                           /* enable the aux port */
    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t cfg = inb(0x60);
    cfg |= 0x02;                                /* aux interrupt */
    cfg &= (uint8_t)~0x20;                      /* aux clock on */
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, cfg);
    mouse_cmd(0xF6);                            /* defaults */
    mouse_cmd(0xF4);                            /* start streaming */
    kprintf("input: PS/2 keyboard and mouse\n");
}

/* ubuf is a user pointer; timeout_ms: 0 blocks, < 0 never blocks */
int input_read(uint8_t *ubuf, uint32_t n, int timeout_ms)
{
    uint32_t max = n / sizeof(struct kiwi_input_event);
    if (!max)
        return -EINVAL;
    if (qhead == qtail) {
        if (timeout_ms < 0)
            return 0;
        current->wake_tick = timeout_ms ? ticks + (uint32_t)(timeout_ms + 9) / (1000 / HZ) + 1 : 0;
        while (qhead == qtail) {
            sleep_on(&input_chan);
            if (current->killed) {
                current->wake_tick = 0;
                return -EINTR;
            }
            if (timeout_ms && ticks >= current->wake_tick)
                break;
        }
        current->wake_tick = 0;
    }
    uint32_t got = 0;
    while (got < max && qtail != qhead) {
        if (copy_to_user(ubuf + got * sizeof(struct kiwi_input_event), &q[qtail % QSIZE],
                         sizeof(struct kiwi_input_event)) < 0)
            return -EFAULT;
        qtail++;
        got++;
    }
    return (int)(got * sizeof(struct kiwi_input_event));
}
