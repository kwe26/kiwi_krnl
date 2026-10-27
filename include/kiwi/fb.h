/*
 * /dev/fb0 - linear framebuffer (Bochs/QEMU "BGA" display adapter).
 *
 *   int fd = open("/dev/fb0", O_RDWR);
 *   struct kiwi_fb_mode m = { 800, 600, 32 };
 *   ioctl(fd, FB_SET_MODE, &m);             // switch to graphics
 *   ioctl(fd, FB_GET_INFO, &info);
 *   uint32_t *px = mmap(0, info.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
 *   ...
 *   ioctl(fd, FB_TEXT_MODE, 0);             // back to the text console
 *
 * Closing the descriptor that switched to graphics restores text mode.
 */
#ifndef _KIWI_FB_H
#define _KIWI_FB_H

#include <stdint.h>

#define FB_GET_INFO   0x4600     /* arg: struct kiwi_fb_info * */
#define FB_SET_MODE   0x4601     /* arg: struct kiwi_fb_mode * */
#define FB_TEXT_MODE  0x4602

struct kiwi_fb_mode {
    uint32_t width, height, bpp;
};

struct kiwi_fb_info {
    uint32_t width, height, bpp;
    uint32_t pitch;              /* bytes per scanline */
    uint32_t size;               /* bytes of the visible framebuffer */
    uint32_t phys;               /* physical address of the LFB */
    uint32_t vram;               /* adapter memory in bytes */
    int32_t  graphics;           /* 1 while a graphics mode is active */
    uint32_t bga_version;        /* 0xB0C0..0xB0C5, 0 if absent */
};

#endif
