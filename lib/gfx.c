/*
 * gfx: double-buffered drawing on /dev/fb0 (see include/gfx/gfx.h).
 *
 * Everything draws into a back buffer (anonymous mmap, so the kernel may
 * compress it like any other memory); gfx_flush copies rectangles to the
 * framebuffer and repaints the software cursor on top.
 */
#include <gfx/gfx.h>
#include <kiwi/kiwi.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int fb_fd = -1, in_fd = -1;
static struct kiwi_fb_info info;
static uint32_t *front;
static gfx_surface screen;
static uint8_t font[4096];
static int font_loaded;
static int cur_x, cur_y, cur_visible = 1;
static int in_timeout = 1 << 30;

#define CUR_W 12
#define CUR_H 19
static const char *const cursor_art =
    "X..........."
    "XX.........."
    "XWX........."
    "XWWX........"
    "XWWWX......."
    "XWWWWX......"
    "XWWWWWX....."
    "XWWWWWWX...."
    "XWWWWWWWX..."
    "XWWWWWWWWX.."
    "XWWWWWWWWWX."
    "XWWWWWWXXXXX"
    "XWWWXWWX...."
    "XWWX.XWWX..."
    "XWX..XWWX..."
    "XX....XWWX.."
    "X.....XWWX.."
    ".......XWWX."
    ".......XXX..";

const uint8_t *gfx_font(void)
{
    if (!font_loaded) {
        if (kinfo(KINFO_FONT, 0, font, sizeof font) == (int)sizeof font)
            font_loaded = 1;
    }
    return font;
}

gfx_surface *gfx_screen(void)
{
    return &screen;
}

int gfx_open(int width, int height)
{
    if (front)
        return 0;
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0)
        return -1;
    struct kiwi_fb_mode m = { (uint32_t)width, (uint32_t)height, 32 };
    if (ioctl(fb_fd, FB_SET_MODE, &m) < 0 || ioctl(fb_fd, FB_GET_INFO, &info) < 0)
        goto fail;
    front = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (front == MAP_FAILED)
        goto fail;
    uint32_t *back = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (back == MAP_FAILED)
        goto fail;
    screen.w = (int)info.width;
    screen.h = (int)info.height;
    screen.stride = (int)(info.pitch / 4);
    screen.px = back;
    gfx_noclip(&screen);
    in_fd = open("/dev/input", O_RDONLY);
    gfx_font();
    cur_x = screen.w / 2;
    cur_y = screen.h / 2;
    return 0;
fail:
    ioctl(fb_fd, FB_TEXT_MODE, 0);
    close(fb_fd);
    fb_fd = -1;
    front = 0;
    return -1;
}

void gfx_close(void)
{
    if (fb_fd < 0)
        return;
    ioctl(fb_fd, FB_TEXT_MODE, 0);
    munmap(front, info.size);
    munmap(screen.px, info.size);
    close(fb_fd);
    if (in_fd >= 0)
        close(in_fd);
    fb_fd = in_fd = -1;
    front = 0;
    screen.px = 0;
}

/* ---- surfaces and clipping ---- */
gfx_surface *gfx_surface_new(int w, int h)
{
    gfx_surface *s = malloc(sizeof *s);
    if (!s)
        return 0;
    s->px = calloc((size_t)w * (size_t)h, 4);
    if (!s->px) {
        free(s);
        return 0;
    }
    s->w = w;
    s->h = h;
    s->stride = w;
    gfx_noclip(s);
    return s;
}

void gfx_surface_free(gfx_surface *s)
{
    if (s) {
        free(s->px);
        free(s);
    }
}

void gfx_noclip(gfx_surface *s)
{
    s->clip = (gfx_rect){ 0, 0, s->w, s->h };
}

void gfx_clip(gfx_surface *s, int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    s->clip = (gfx_rect){ x, y, x1 > x ? x1 - x : 0, y1 > y ? y1 - y : 0 };
}

/* intersect a rectangle with the clip; returns 0 if empty */
static int clip_rect(const gfx_surface *s, int *x, int *y, int *w, int *h)
{
    int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
    if (x0 < s->clip.x) x0 = s->clip.x;
    if (y0 < s->clip.y) y0 = s->clip.y;
    if (x1 > s->clip.x + s->clip.w) x1 = s->clip.x + s->clip.w;
    if (y1 > s->clip.y + s->clip.h) y1 = s->clip.y + s->clip.h;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

/* ---- primitives ---- */
void gfx_pixel(gfx_surface *s, int x, int y, gfx_color c)
{
    if (x >= s->clip.x && y >= s->clip.y && x < s->clip.x + s->clip.w && y < s->clip.y + s->clip.h)
        s->px[y * s->stride + x] = c;
}

void gfx_fill(gfx_surface *s, int x, int y, int w, int h, gfx_color c)
{
    if (!clip_rect(s, &x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++) {
        uint32_t *row = s->px + (y + j) * s->stride + x;
        for (int i = 0; i < w; i++)
            row[i] = c;
    }
}

void gfx_hline(gfx_surface *s, int x, int y, int w, gfx_color c) { gfx_fill(s, x, y, w, 1, c); }
void gfx_vline(gfx_surface *s, int x, int y, int h, gfx_color c) { gfx_fill(s, x, y, 1, h, c); }

void gfx_frame(gfx_surface *s, int x, int y, int w, int h, gfx_color c)
{
    gfx_hline(s, x, y, w, c);
    gfx_hline(s, x, y + h - 1, w, c);
    gfx_vline(s, x, y, h, c);
    gfx_vline(s, x + w - 1, y, h, c);
}

void gfx_line(gfx_surface *s, int x0, int y0, int x1, int y1, gfx_color c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static gfx_color mix(gfx_color a, gfx_color b, int t, int n)
{
    int r = (int)((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t) / n;
    int g = (int)((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t) / n;
    int bl = (int)(a & 255) + (((int)(b & 255) - (int)(a & 255)) * t) / n;
    return (gfx_color)(r << 16 | g << 8 | bl);
}

/* horizontal gradient a -> b */
void gfx_gradient(gfx_surface *s, int x, int y, int w, int h, gfx_color a, gfx_color b)
{
    for (int i = 0; i < w; i++)
        gfx_fill(s, x + i, y, 1, h, mix(a, b, i, w > 1 ? w - 1 : 1));
}

/* checkerboard of two colors (classic scroll track / disabled look) */
void gfx_dither(gfx_surface *s, int x, int y, int w, int h, gfx_color a, gfx_color b)
{
    int x0 = x, y0 = y;
    if (!clip_rect(s, &x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++) {
        uint32_t *row = s->px + (y + j) * s->stride + x;
        for (int i = 0; i < w; i++)
            row[i] = ((x + i - x0 + y + j - y0) & 1) ? b : a;
    }
}

void gfx_blit(gfx_surface *dst, int dx, int dy, const gfx_surface *src, int sx, int sy, int w, int h)
{
    int x = dx, y = dy;
    if (!clip_rect(dst, &x, &y, &w, &h))
        return;
    sx += x - dx;
    sy += y - dy;
    for (int j = 0; j < h; j++) {
        if (sy + j < 0 || sy + j >= src->h)
            continue;
        memcpy(dst->px + (y + j) * dst->stride + x, src->px + (sy + j) * src->stride + sx, (size_t)w * 4);
    }
}

void gfx_bitmap(gfx_surface *s, int x, int y, int w, int h, const char *art, const gfx_color *pal)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            char c = art[j * w + i];
            if (!c)
                return;
            int idx = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (idx >= 0)
                gfx_pixel(s, x + i, y + j, pal[idx]);
        }
    }
}

/* ---- text ---- */
static int next_glyph(const unsigned char **p)
{
    const unsigned char *s = *p;
    int c = *s++;
    if (c >= 0x80) {                    /* UTF-8: skip the sequence, show '?' */
        while ((*s & 0xC0) == 0x80)
            s++;
        c = '?';
    }
    *p = s;
    return c;
}

int gfx_text_width(const char *str)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        next_glyph(&p);
        n++;
    }
    return n * GFX_FONT_W;
}

void gfx_text(gfx_surface *s, int x, int y, const char *str, gfx_color fg, int32_t bg, int flags)
{
    const uint8_t *f = gfx_font();
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        int c = next_glyph(&p);
        if (bg >= 0)
            gfx_fill(s, x, y, GFX_FONT_W, GFX_FONT_H, (gfx_color)bg);
        if (x + GFX_FONT_W + 1 > s->clip.x && x < s->clip.x + s->clip.w &&
            y + GFX_FONT_H > s->clip.y && y < s->clip.y + s->clip.h) {
            for (int r = 0; r < GFX_FONT_H; r++) {
                uint8_t bits = f[c * 16 + r];
                if ((flags & GFX_UNDERLINE) && r == GFX_FONT_H - 2)
                    bits = 0xFF;
                for (int b = 0; b < 8; b++) {
                    if (bits & (0x80 >> b)) {
                        gfx_pixel(s, x + b, y + r, fg);
                        if (flags & GFX_BOLD)
                            gfx_pixel(s, x + b + 1, y + r, fg);
                    }
                }
            }
        }
        x += GFX_FONT_W;
    }
}

/* ---- presenting and the cursor ---- */
static void draw_cursor(void)
{
    if (!cur_visible || !front)
        return;
    for (int j = 0; j < CUR_H; j++) {
        int y = cur_y + j;
        if (y < 0 || y >= screen.h)
            continue;
        for (int i = 0; i < CUR_W; i++) {
            int x = cur_x + i;
            char c = cursor_art[j * CUR_W + i];
            if (x < 0 || x >= screen.w || c == '.')
                continue;
            front[y * (int)(info.pitch / 4) + x] = c == 'X' ? 0x000000 : 0xFFFFFF;
        }
    }
}

static void copy_out(int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen.w) w = screen.w - x;
    if (y + h > screen.h) h = screen.h - y;
    if (w <= 0 || h <= 0 || !front)
        return;
    int fstride = (int)(info.pitch / 4);
    for (int j = 0; j < h; j++)
        memcpy(front + (y + j) * fstride + x, screen.px + (y + j) * screen.stride + x, (size_t)w * 4);
}

void gfx_flush(int x, int y, int w, int h)
{
    copy_out(x, y, w, h);
    if (cur_x < x + w && cur_x + CUR_W > x && cur_y < y + h && cur_y + CUR_H > y)
        draw_cursor();
}

void gfx_flush_all(void)
{
    gfx_flush(0, 0, screen.w, screen.h);
}

void gfx_cursor_move(int x, int y)
{
    if (x == cur_x && y == cur_y)
        return;
    int ox = cur_x, oy = cur_y;
    cur_x = x;
    cur_y = y;
    copy_out(ox, oy, CUR_W, CUR_H);              /* erase: restore from the back buffer */
    draw_cursor();
}

void gfx_cursor_show(int visible)
{
    cur_visible = visible;
    if (visible)
        draw_cursor();
    else
        copy_out(cur_x, cur_y, CUR_W, CUR_H);
}

/* ---- input ---- */
int gfx_poll(gfx_event *ev, int timeout_ms)
{
    if (in_fd < 0)
        return -1;
    int kt = timeout_ms < 0 ? 0 : timeout_ms == 0 ? -1 : timeout_ms;
    if (kt != in_timeout) {
        ioctl(in_fd, INPUT_SET_TIMEOUT, kt);
        in_timeout = kt;
    }
    ssize_t r = read(in_fd, ev, sizeof *ev);
    if (r < 0)
        return -1;
    return r == (ssize_t)sizeof *ev;
}
