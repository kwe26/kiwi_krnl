/*
 * Kiwi graphics: a double-buffered framebuffer with clipped 2D drawing,
 * the VGA BIOS 8x16 font, a software mouse cursor and input events.
 *
 *   gfx_open(800, 600);
 *   gfx_surface *s = gfx_screen();
 *   gfx_fill(s, 0, 0, s->w, s->h, UI_DESKTOP);
 *   gfx_text(s, 10, 10, "Hello", GFX_WHITE, GFX_TRANSPARENT, 0);
 *   gfx_flush_all();
 *   gfx_event ev;
 *   while (gfx_poll(&ev, 1000) >= 0) ...
 *   gfx_close();
 */
#ifndef _GFX_GFX_H
#define _GFX_GFX_H

#include <stdint.h>
#include <kiwi/input.h>
#include <gfx/colors.h>

typedef uint32_t gfx_color;              /* 0xRRGGBB */
#define GFX_TRANSPARENT (-1)

typedef struct { int x, y, w, h; } gfx_rect;

typedef struct gfx_surface {
    int w, h;
    int stride;                          /* pixels per row */
    uint32_t *px;
    gfx_rect clip;
} gfx_surface;

typedef struct kiwi_input_event gfx_event;

/* ---- display ---- */
int          gfx_open(int width, int height);   /* 0 on success */
void         gfx_close(void);                   /* back to the text console */
gfx_surface *gfx_screen(void);                  /* the back buffer */
void         gfx_flush(int x, int y, int w, int h);   /* back buffer -> screen */
void         gfx_flush_all(void);

/* ---- surfaces and clipping ---- */
gfx_surface *gfx_surface_new(int w, int h);
void         gfx_surface_free(gfx_surface *s);
void         gfx_clip(gfx_surface *s, int x, int y, int w, int h);
void         gfx_noclip(gfx_surface *s);

/* ---- primitives ---- */
void gfx_pixel(gfx_surface *s, int x, int y, gfx_color c);
void gfx_fill(gfx_surface *s, int x, int y, int w, int h, gfx_color c);
void gfx_hline(gfx_surface *s, int x, int y, int w, gfx_color c);
void gfx_vline(gfx_surface *s, int x, int y, int h, gfx_color c);
void gfx_frame(gfx_surface *s, int x, int y, int w, int h, gfx_color c);   /* outline */
void gfx_line(gfx_surface *s, int x0, int y0, int x1, int y1, gfx_color c);
void gfx_gradient(gfx_surface *s, int x, int y, int w, int h, gfx_color a, gfx_color b);
void gfx_dither(gfx_surface *s, int x, int y, int w, int h, gfx_color a, gfx_color b);
void gfx_blit(gfx_surface *dst, int dx, int dy, const gfx_surface *src,
              int sx, int sy, int w, int h);
/* ASCII-art bitmap: '0'-'9','a'-'f' index the palette, '.' / ' ' are transparent */
void gfx_bitmap(gfx_surface *s, int x, int y, int w, int h, const char *art,
                const gfx_color *palette);

/* ---- text (VGA 8x16 font) ---- */
#define GFX_FONT_W 8
#define GFX_FONT_H 16
#define GFX_BOLD      1
#define GFX_UNDERLINE 2
void           gfx_text(gfx_surface *s, int x, int y, const char *str, gfx_color fg,
                        int32_t bg, int flags);
int            gfx_text_width(const char *str);
const uint8_t *gfx_font(void);

/* ---- mouse cursor (drawn on the screen, never in the back buffer) ---- */
void gfx_cursor_move(int x, int y);
void gfx_cursor_show(int visible);

/* ---- input ---- */
int gfx_poll(gfx_event *ev, int timeout_ms);    /* 1 = event, 0 = timeout, -1 = error */

#endif
