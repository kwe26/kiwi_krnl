/* Classic 3D desktop widgets (see include/gfx/ui.h). */
#include <gfx/ui.h>
#include <string.h>

static void edge(gfx_surface *s, int x, int y, int w, int h, gfx_color tl, gfx_color br)
{
    gfx_hline(s, x, y, w - 1, tl);
    gfx_vline(s, x, y, h - 1, tl);
    gfx_hline(s, x, y + h - 1, w, br);
    gfx_vline(s, x + w - 1, y, h, br);
}

void ui_bevel(gfx_surface *s, int x, int y, int w, int h, int style)
{
    switch (style) {
    case UI_RAISED:
        edge(s, x, y, w, h, UI_HILIGHT, UI_SHADOW);
        break;
    case UI_SUNKEN:
        edge(s, x, y, w, h, UI_SHADOW, UI_HILIGHT);
        break;
    case UI_BUTTON_UP:
        edge(s, x, y, w, h, UI_HILIGHT, UI_DKSHADOW);
        edge(s, x + 1, y + 1, w - 2, h - 2, UI_LIGHT, UI_SHADOW);
        break;
    case UI_BUTTON_DOWN:
        edge(s, x, y, w, h, UI_DKSHADOW, UI_HILIGHT);
        edge(s, x + 1, y + 1, w - 2, h - 2, UI_SHADOW, UI_LIGHT);
        break;
    case UI_FIELD_EDGE:
        edge(s, x, y, w, h, UI_SHADOW, UI_HILIGHT);
        edge(s, x + 1, y + 1, w - 2, h - 2, UI_DKSHADOW, UI_LIGHT);
        break;
    case UI_WINDOW_EDGE:
        edge(s, x, y, w, h, UI_LIGHT, UI_DKSHADOW);
        edge(s, x + 1, y + 1, w - 2, h - 2, UI_HILIGHT, UI_SHADOW);
        break;
    case UI_GROOVE:
        gfx_frame(s, x, y, w - 1, h - 1, UI_SHADOW);
        gfx_frame(s, x + 1, y + 1, w - 1, h - 1, UI_HILIGHT);
        break;
    }
}

/* 8x8 glyphs for caption buttons, scroll arrows and check marks */
static const char *const glyphs[] = {
    [UI_GLYPH_CLOSE] = "11....11" ".11..11." "..1111.." "...11..." "..1111.." ".11..11." "11....11" "........",
    [UI_GLYPH_MIN]   = "........" "........" "........" "........" "........" "........" "111111.." "111111..",
    [UI_GLYPH_MAX]   = "11111111" "11111111" "1......1" "1......1" "1......1" "1......1" "11111111" "........",
    [UI_GLYPH_UP]    = "........" "...1...." "..111..." ".11111.." "1111111." "........" "........" "........",
    [UI_GLYPH_DOWN]  = "........" "1111111." ".11111.." "..111..." "...1...." "........" "........" "........",
    [UI_GLYPH_LEFT]  = "....1..." "...11..." "..111..." ".1111..." "..111..." "...11..." "....1..." "........",
    [UI_GLYPH_RIGHT] = ".1......" ".11....." ".111...." ".1111..." ".111...." ".11....." ".1......" "........",
    [UI_GLYPH_CHECK] = "......1." ".....11." "1...111." "11.111.." "11111..." ".111...." "..1....." "........",
};

void ui_glyph(gfx_surface *s, int x, int y, int glyph, gfx_color c)
{
    gfx_color pal[2] = { 0, c };
    gfx_bitmap(s, x, y, 8, 8, glyphs[glyph], pal);
}

static void caption_button(gfx_surface *s, int x, int y, int glyph)
{
    gfx_fill(s, x, y, 16, 14, UI_FACE);
    ui_bevel(s, x, y, 16, 14, UI_BUTTON_UP);
    ui_glyph(s, x + 4, y + 3, glyph, UI_TEXT);
}

void ui_client(int x, int y, int w, int h, gfx_rect *c)
{
    c->x = x + UI_BORDER;
    c->y = y + UI_BORDER + UI_TITLE_H + 1;
    c->w = w - 2 * UI_BORDER;
    c->h = h - 2 * UI_BORDER - UI_TITLE_H - 1;
}

void ui_window(gfx_surface *s, int x, int y, int w, int h, const char *title, int active)
{
    gfx_fill(s, x, y, w, h, UI_FACE);
    ui_bevel(s, x, y, w, h, UI_WINDOW_EDGE);
    int tx = x + 3, ty = y + 3, tw = w - 6;
    if (active)
        gfx_gradient(s, tx, ty, tw, UI_TITLE_H, UI_TITLE, UI_TITLE2);
    else
        gfx_gradient(s, tx, ty, tw, UI_TITLE_H, UI_TITLE_INACT, UI_TITLE_INACT2);
    gfx_rect saved = s->clip;
    gfx_clip(s, tx, ty, tw - 40, UI_TITLE_H);
    gfx_rect inter = s->clip;
    /* respect the caller's clip as well */
    int x0 = inter.x > saved.x ? inter.x : saved.x, y0 = inter.y > saved.y ? inter.y : saved.y;
    int x1 = inter.x + inter.w < saved.x + saved.w ? inter.x + inter.w : saved.x + saved.w;
    int y1 = inter.y + inter.h < saved.y + saved.h ? inter.y + inter.h : saved.y + saved.h;
    s->clip = (gfx_rect){ x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
    gfx_text(s, tx + 4, ty + 1, title, active ? UI_TITLE_TEXT : GFX_SILVER, GFX_TRANSPARENT, GFX_BOLD);
    s->clip = saved;
    caption_button(s, x + w - 5 - 16, ty + 2, UI_GLYPH_CLOSE);
    caption_button(s, x + w - 5 - 16 - 18, ty + 2, UI_GLYPH_MIN);
}

int ui_window_hit(int x, int y, int w, int h, int px, int py)
{
    if (px < x || py < y || px >= x + w || py >= y + h)
        return 0;
    int ty = y + 3;
    if (py >= ty + 2 && py < ty + 16) {
        if (px >= x + w - 5 - 16 && px < x + w - 5)
            return 2;                               /* close */
        if (px >= x + w - 5 - 34 && px < x + w - 5 - 18)
            return 6;                               /* minimize */
    }
    if (px >= x + w - 12 && py >= y + h - 12)
        return 5;                                   /* resize grip */
    if (py >= ty && py < ty + UI_TITLE_H && px >= x + 3 && px < x + w - 3)
        return 1;                                   /* title bar */
    gfx_rect c;
    ui_client(x, y, w, h, &c);
    if (px >= c.x && py >= c.y && px < c.x + c.w && py < c.y + c.h)
        return 3;
    return 4;
}

void ui_button(gfx_surface *s, int x, int y, int w, int h, const char *label, int pressed, int focused)
{
    gfx_fill(s, x, y, w, h, UI_FACE);
    ui_bevel(s, x, y, w, h, pressed ? UI_BUTTON_DOWN : UI_BUTTON_UP);
    int tw = gfx_text_width(label);
    int tx = x + (w - tw) / 2 + (pressed ? 1 : 0), ty = y + (h - GFX_FONT_H) / 2 + (pressed ? 1 : 0);
    gfx_text(s, tx, ty, label, UI_TEXT, GFX_TRANSPARENT, 0);
    if (focused) {
        for (int i = x + 4; i < x + w - 4; i += 2) {
            gfx_pixel(s, i, y + 4, UI_TEXT);
            gfx_pixel(s, i, y + h - 5, UI_TEXT);
        }
        for (int j = y + 4; j < y + h - 4; j += 2) {
            gfx_pixel(s, x + 4, j, UI_TEXT);
            gfx_pixel(s, x + w - 5, j, UI_TEXT);
        }
    }
}

void ui_field(gfx_surface *s, int x, int y, int w, int h)
{
    gfx_fill(s, x, y, w, h, UI_FIELD);
    ui_bevel(s, x, y, w, h, UI_FIELD_EDGE);
}

void ui_checkbox(gfx_surface *s, int x, int y, const char *label, int checked)
{
    ui_field(s, x, y + 1, 13, 13);
    if (checked)
        ui_glyph(s, x + 3, y + 4, UI_GLYPH_CHECK, UI_TEXT);
    gfx_text(s, x + 19, y, label, UI_TEXT, GFX_TRANSPARENT, 0);
}

void ui_scrollbar(gfx_surface *s, int x, int y, int len, int vertical, int total, int visible, int pos)
{
    int b = UI_SCROLL_W;
    int bx2 = vertical ? x : x + len - b, by2 = vertical ? y + len - b : y;
    if (vertical)
        gfx_dither(s, x, y + b, b, len - 2 * b, UI_FACE, UI_HILIGHT);
    else
        gfx_dither(s, x + b, y, len - 2 * b, b, UI_FACE, UI_HILIGHT);
    gfx_fill(s, x, y, b, b, UI_FACE);
    ui_bevel(s, x, y, b, b, UI_BUTTON_UP);
    ui_glyph(s, x + 4, y + 4, vertical ? UI_GLYPH_UP : UI_GLYPH_LEFT, UI_TEXT);
    gfx_fill(s, bx2, by2, b, b, UI_FACE);
    ui_bevel(s, bx2, by2, b, b, UI_BUTTON_UP);
    ui_glyph(s, bx2 + 4, by2 + 4, vertical ? UI_GLYPH_DOWN : UI_GLYPH_RIGHT, UI_TEXT);
    int track = len - 2 * b;
    if (total <= visible || track < 8)
        return;
    int th = track * visible / total;
    if (th < 8) th = 8;
    int maxpos = total - visible;
    if (pos > maxpos) pos = maxpos;
    if (pos < 0) pos = 0;
    int off = (track - th) * pos / maxpos;
    if (vertical) {
        gfx_fill(s, x, y + b + off, b, th, UI_FACE);
        ui_bevel(s, x, y + b + off, b, th, UI_BUTTON_UP);
    } else {
        gfx_fill(s, x + b + off, y, th, b, UI_FACE);
        ui_bevel(s, x + b + off, y, th, b, UI_BUTTON_UP);
    }
}

void ui_progress(gfx_surface *s, int x, int y, int w, int h, int permille)
{
    gfx_fill(s, x, y, w, h, UI_FACE);
    ui_bevel(s, x, y, w, h, UI_SUNKEN);
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    int inner = w - 4, filled = inner * permille / 1000;
    for (int bx = 0; bx + 2 < filled; bx += 10) {
        int bw = filled - bx < 8 ? filled - bx : 8;
        gfx_fill(s, x + 2 + bx, y + 2, bw, h - 4, UI_SELECT);
    }
}

void ui_groupbox(gfx_surface *s, int x, int y, int w, int h, const char *label)
{
    ui_bevel(s, x, y + 7, w, h - 7, UI_GROOVE);
    if (label && *label) {
        int tw = gfx_text_width(label);
        gfx_fill(s, x + 8, y, tw + 4, GFX_FONT_H, UI_FACE);
        gfx_text(s, x + 10, y, label, UI_TEXT, GFX_TRANSPARENT, 0);
    }
}
