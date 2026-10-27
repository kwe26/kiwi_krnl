/*
 * Framebuffer: the Bochs/QEMU display adapter ("BGA", VBE DISPI ports).
 *
 * Switching to graphics clobbers the VGA registers and the text/font
 * planes, so the text-mode register file, DAC palette and BIOS font are
 * captured at boot and put back when we return to the console.  The font
 * is also exported to user space (KINFO_FONT) for the GUI.
 */
#include "kernel.h"

#define VBE_INDEX 0x1CE
#define VBE_DATA  0x1CF
enum { VBE_ID, VBE_XRES, VBE_YRES, VBE_BPP, VBE_ENABLE, VBE_BANK, VBE_VIRT_W,
       VBE_VIRT_H, VBE_XOFF, VBE_YOFF, VBE_VRAM64K };
#define VBE_ENABLED 0x01
#define VBE_LFB     0x40

uint8_t vga_font[4096];
static struct kiwi_fb_info fbi;

struct vga_state {
    uint8_t misc, seq[5], crtc[25], gc[9], ac[21], dac[768];
};
static struct vga_state text_state;

static uint16_t vbe_read(int idx)
{
    outw(VBE_INDEX, (uint16_t)idx);
    return inw(VBE_DATA);
}

static void vbe_write(int idx, uint16_t v)
{
    outw(VBE_INDEX, (uint16_t)idx);
    outw(VBE_DATA, v);
}

static void vga_save(struct vga_state *v)
{
    v->misc = inb(0x3CC);
    for (int i = 0; i < 5; i++) { outb(0x3C4, (uint8_t)i); v->seq[i] = inb(0x3C5); }
    for (int i = 0; i < 25; i++) { outb(0x3D4, (uint8_t)i); v->crtc[i] = inb(0x3D5); }
    for (int i = 0; i < 9; i++) { outb(0x3CE, (uint8_t)i); v->gc[i] = inb(0x3CF); }
    for (int i = 0; i < 21; i++) { inb(0x3DA); outb(0x3C0, (uint8_t)i); v->ac[i] = inb(0x3C1); }
    inb(0x3DA);
    outb(0x3C0, 0x20);
    outb(0x3C7, 0);
    for (int i = 0; i < 768; i++) v->dac[i] = inb(0x3C9);
}

static void vga_restore(const struct vga_state *v)
{
    outb(0x3C2, v->misc);
    for (int i = 0; i < 5; i++) { outb(0x3C4, (uint8_t)i); outb(0x3C5, v->seq[i]); }
    outb(0x3D4, 0x11);
    outb(0x3D5, v->crtc[0x11] & 0x7F);          /* unlock CRTC 0-7 */
    for (int i = 0; i < 25; i++) { outb(0x3D4, (uint8_t)i); outb(0x3D5, v->crtc[i]); }
    for (int i = 0; i < 9; i++) { outb(0x3CE, (uint8_t)i); outb(0x3CF, v->gc[i]); }
    for (int i = 0; i < 21; i++) { inb(0x3DA); outb(0x3C0, (uint8_t)i); outb(0x3C0, v->ac[i]); }
    inb(0x3DA);
    outb(0x3C0, 0x20);
    outb(0x3C8, 0);
    for (int i = 0; i < 768; i++) outb(0x3C9, v->dac[i]);
}

/* read or write the character generator (plane 2) */
static void vga_font_io(int write)
{
    uint8_t s2, s4, g4, g5, g6;
    outb(0x3C4, 2); s2 = inb(0x3C5);
    outb(0x3C4, 4); s4 = inb(0x3C5);
    outb(0x3CE, 4); g4 = inb(0x3CF);
    outb(0x3CE, 5); g5 = inb(0x3CF);
    outb(0x3CE, 6); g6 = inb(0x3CF);
    outb(0x3C4, 2); outb(0x3C5, 0x04);          /* write plane 2 only */
    outb(0x3C4, 4); outb(0x3C5, 0x07);          /* sequential addressing */
    outb(0x3CE, 4); outb(0x3CF, 0x02);          /* read plane 2 */
    outb(0x3CE, 5); outb(0x3CF, 0x00);          /* no odd/even */
    outb(0x3CE, 6); outb(0x3CF, 0x04);          /* map 0xA0000, 64KB */
    volatile uint8_t *m = (volatile uint8_t *)0xA0000;
    for (int c = 0; c < 256; c++)
        for (int r = 0; r < 16; r++) {
            if (write) m[c * 32 + r] = vga_font[c * 16 + r];
            else vga_font[c * 16 + r] = m[c * 32 + r];
        }
    outb(0x3C4, 2); outb(0x3C5, s2);
    outb(0x3C4, 4); outb(0x3C5, s4);
    outb(0x3CE, 4); outb(0x3CF, g4);
    outb(0x3CE, 5); outb(0x3CF, g5);
    outb(0x3CE, 6); outb(0x3CF, g6);
}

void fb_init(void)
{
    vga_save(&text_state);
    vga_font_io(0);
    uint16_t id = vbe_read(VBE_ID);
    if (id < 0xB0C0 || id > 0xB0CF) {
        kprintf("fb: no Bochs/QEMU display adapter\n");
        return;
    }
    fbi.bga_version = id;
    struct kiwi_pcidev *d = pci_find(0x1234, 0x1111);
    fbi.phys = d ? (d->bar[0] & ~0xFu) : 0xE0000000u;
    fbi.vram = id >= 0xB0C2 ? (uint32_t)vbe_read(VBE_VRAM64K) * 65536u : 0;
    if (!fbi.vram)
        fbi.vram = 16u << 20;
    kprintf("fb: Bochs VBE %x, LFB at %x, %d KB video memory\n", id, fbi.phys, fbi.vram / 1024);
}

int fb_set_mode(uint32_t w, uint32_t h, uint32_t bpp)
{
    if (!fbi.bga_version)
        return -ENODEV;
    if (bpp != 32 || w < 320 || h < 200 || w > 1920 || h > 1200 || w * h * 4 > fbi.vram)
        return -EINVAL;
    if (!fbi.graphics)
        console_shadow(1);
    vbe_write(VBE_ENABLE, 0);
    vbe_write(VBE_XRES, (uint16_t)w);
    vbe_write(VBE_YRES, (uint16_t)h);
    vbe_write(VBE_BPP, (uint16_t)bpp);
    vbe_write(VBE_VIRT_W, (uint16_t)w);
    vbe_write(VBE_XOFF, 0);
    vbe_write(VBE_YOFF, 0);
    vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB);
    fbi.width = w;
    fbi.height = h;
    fbi.bpp = bpp;
    fbi.pitch = w * 4;
    fbi.size = w * h * 4;
    fbi.graphics = 1;
    kprintf("fb: %dx%dx%d graphics mode\n", w, h, bpp);
    return 0;
}

void fb_text_mode(void)
{
    if (!fbi.graphics)
        return;
    vbe_write(VBE_ENABLE, 0);
    vga_restore(&text_state);
    vga_font_io(1);
    fbi.graphics = 0;
    console_shadow(0);
    kprintf("fb: back to text mode\n");
}

void fb_get_info(struct kiwi_fb_info *out)
{
    *out = fbi;
}
