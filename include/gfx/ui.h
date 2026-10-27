/*
 * Classic desktop look: 3D bevels, windows with gradient title bars,
 * push buttons, check boxes, scroll bars, edit fields, progress bars.
 * Everything draws into a gfx_surface; layout constants are below.
 */
#ifndef _GFX_UI_H
#define _GFX_UI_H

#include <gfx/gfx.h>

#define UI_BORDER     4        /* window frame thickness */
#define UI_TITLE_H    18       /* title bar height */
#define UI_SCROLL_W   16       /* scroll bar thickness */

enum {
    UI_RAISED,                 /* 1-pixel raised edge */
    UI_SUNKEN,                 /* 1-pixel sunken edge */
    UI_BUTTON_UP,              /* 2-pixel push button */
    UI_BUTTON_DOWN,
    UI_FIELD_EDGE,             /* 2-pixel sunken edit / list frame */
    UI_WINDOW_EDGE,            /* 2-pixel window frame */
    UI_GROOVE,                 /* etched separator box */
};

enum { UI_GLYPH_CLOSE, UI_GLYPH_MIN, UI_GLYPH_MAX, UI_GLYPH_UP, UI_GLYPH_DOWN,
       UI_GLYPH_LEFT, UI_GLYPH_RIGHT, UI_GLYPH_CHECK };

void ui_bevel(gfx_surface *s, int x, int y, int w, int h, int style);
void ui_glyph(gfx_surface *s, int x, int y, int glyph, gfx_color c);
/* window frame, title bar and caption buttons; client area via ui_client() */
void ui_window(gfx_surface *s, int x, int y, int w, int h, const char *title, int active);
void ui_client(int x, int y, int w, int h, gfx_rect *client);
/* hit-test a window: 0 none, 1 title bar, 2 close, 3 client, 4 frame, 5 resize grip */
int  ui_window_hit(int x, int y, int w, int h, int px, int py);
void ui_button(gfx_surface *s, int x, int y, int w, int h, const char *label,
               int pressed, int focused);
void ui_checkbox(gfx_surface *s, int x, int y, const char *label, int checked);
void ui_field(gfx_surface *s, int x, int y, int w, int h);
void ui_scrollbar(gfx_surface *s, int x, int y, int len, int vertical,
                  int total, int visible, int pos);
void ui_progress(gfx_surface *s, int x, int y, int w, int h, int permille);
void ui_groupbox(gfx_surface *s, int x, int y, int w, int h, const char *label);

#endif
