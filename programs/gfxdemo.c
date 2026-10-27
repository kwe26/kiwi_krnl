/*
 * gfxdemo: the C GUI headers in action - a classic window with widgets.
 * Move the mouse, click the buttons; Esc or the close box exits.
 */
#include <stdio.h>
#include <gfx/gfx.h>
#include <gfx/ui.h>
#include <kiwi/kiwi.h>

static int mx = 400, my = 300, pressed, checked = 1, clicks;

static void paint(void)
{
    gfx_surface *s = gfx_screen();
    gfx_fill(s, 0, 0, s->w, s->h, UI_DESKTOP);
    int x = 180, y = 120, w = 440, h = 300;
    ui_window(s, x, y, w, h, "gfxdemo - C GUI headers", 1);
    gfx_rect c;
    ui_client(x, y, w, h, &c);
    gfx_text(s, c.x + 12, c.y + 10, "Drawn with <gfx/gfx.h> and <gfx/ui.h>", UI_TEXT, GFX_TRANSPARENT, 0);
    ui_groupbox(s, c.x + 10, c.y + 36, 250, 120, "Widgets");
    ui_checkbox(s, c.x + 24, c.y + 60, "A check box", checked);
    ui_field(s, c.x + 24, c.y + 86, 200, 22);
    char buf[64];
    snprintf(buf, sizeof buf, "button clicks: %d", clicks);
    gfx_text(s, c.x + 30, c.y + 89, buf, UI_TEXT, GFX_TRANSPARENT, 0);
    ui_progress(s, c.x + 24, c.y + 120, 200, 20, (int)(kiwi_uptime_ms() / 10 % 1000));
    ui_scrollbar(s, c.x + c.w - 26, c.y + 36, 160, 1, 100, 25, 40);
    ui_button(s, c.x + 280, c.y + 60, 100, 26, "Click me", pressed, 1);
    ui_button(s, c.x + 280, c.y + 96, 100, 26, "Quit", 0, 0);
    gfx_flush_all();
}

static int in(int x, int y, int w, int h) { return mx >= x && my >= y && mx < x + w && my < y + h; }

int main(void)
{
    if (gfx_open(800, 600) < 0) {
        printf("gfxdemo: no framebuffer\n");
        return 1;
    }
    gfx_rect c;
    ui_client(180, 120, 440, 300, &c);
    paint();
    gfx_cursor_move(mx, my);
    int buttons = 0;
    for (;;) {
        gfx_event ev;
        int r = gfx_poll(&ev, 50);
        if (r < 0)
            break;
        if (r == 0) {                   /* animate the progress bar */
            paint();
            continue;
        }
        if (ev.type == EV_KEY && ev.value && ev.code == KEY_ESC)
            break;
        if (ev.type != EV_MOUSE)
            continue;
        mx += ev.dx;
        my += ev.dy;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx > 799) mx = 799;
        if (my > 599) my = 599;
        gfx_cursor_move(mx, my);
        int down = (ev.buttons & BTN_LEFT) && !(buttons & BTN_LEFT);
        int up = !(ev.buttons & BTN_LEFT) && (buttons & BTN_LEFT);
        buttons = (int)ev.buttons;
        if (down && in(c.x + 280, c.y + 60, 100, 26)) { pressed = 1; paint(); }
        if (up && pressed) { pressed = 0; clicks++; paint(); }
        if (down && in(c.x + 24, c.y + 60, 13, 14)) { checked = !checked; paint(); }
        if (down && (in(c.x + 280, c.y + 96, 100, 26) || ui_window_hit(180, 120, 440, 300, mx, my) == 2))
            break;
    }
    gfx_close();
    printf("gfxdemo: %d clicks\n", clicks);
    return 0;
}
