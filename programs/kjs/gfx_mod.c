/*
 * The 'gfx' module: framebuffer drawing, classic widgets and input for
 * JavaScript, on top of <gfx/gfx.h> and <gfx/ui.h>.
 *
 *   import * as gfx from 'gfx';
 *   gfx.open(800, 600);
 *   gfx.fill(0, 0, 800, 600, gfx.DESKTOP);
 *   gfx.window(100, 100, 300, 200, 'Hello', true);
 *   gfx.flush();
 *   const ev = gfx.poll(1000);   // { type: 'mouse', dx, dy, buttons } | { type: 'key', ... } | null
 */
#include "quickjs.h"

#ifdef KJS_HOST
/* host build (syntax checks only): an empty module so imports resolve */
static int host_init(JSContext *ctx, JSModuleDef *m) { return 0; }
JSModuleDef *kjs_module_gfx(JSContext *ctx, const char *name)
{
    return JS_NewCModule(ctx, name, host_init);
}
#else
#include <stdlib.h>
#include <string.h>
#include <gfx/gfx.h>
#include <gfx/ui.h>

#define S gfx_screen()

static int I(JSContext *ctx, int argc, JSValueConst *argv, int i, int def)
{
    int32_t v = def;
    if (i < argc && !JS_IsUndefined(argv[i]))
        JS_ToInt32(ctx, &v, argv[i]);
    return v;
}

static int B(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
    return i < argc && JS_ToBool(ctx, argv[i]) > 0;
}

/* run fn with a C string argument */
#define WITH_STR(idx, var, body)                                      \
    do {                                                              \
        const char *var = JS_ToCString(ctx, idx < argc ? argv[idx] : JS_UNDEFINED); \
        if (!var)                                                     \
            return JS_EXCEPTION;                                      \
        body;                                                         \
        JS_FreeCString(ctx, var);                                     \
    } while (0)

#define A(i) I(ctx, argc, argv, i, 0)

static JSValue g_open(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewBool(ctx, gfx_open(I(ctx, argc, argv, 0, 800), I(ctx, argc, argv, 1, 600)) == 0);
}

static JSValue g_close(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_close();
    return JS_UNDEFINED;
}

static JSValue g_size(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "width", JS_NewInt32(ctx, S->w));
    JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, S->h));
    return o;
}

static JSValue g_fill(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_fill(S, A(0), A(1), A(2), A(3), (gfx_color)A(4));
    return JS_UNDEFINED;
}

static JSValue g_frame(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_frame(S, A(0), A(1), A(2), A(3), (gfx_color)A(4));
    return JS_UNDEFINED;
}

static JSValue g_line(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_line(S, A(0), A(1), A(2), A(3), (gfx_color)A(4));
    return JS_UNDEFINED;
}

static JSValue g_pixel(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_pixel(S, A(0), A(1), (gfx_color)A(2));
    return JS_UNDEFINED;
}

static JSValue g_gradient(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_gradient(S, A(0), A(1), A(2), A(3), (gfx_color)A(4), (gfx_color)A(5));
    return JS_UNDEFINED;
}

static JSValue g_dither(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_dither(S, A(0), A(1), A(2), A(3), (gfx_color)A(4), (gfx_color)A(5));
    return JS_UNDEFINED;
}

static JSValue g_text(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    WITH_STR(2, s, gfx_text(S, A(0), A(1), s, (gfx_color)A(3), I(ctx, argc, argv, 4, -1), A(5)));
    return JS_UNDEFINED;
}

static JSValue g_textWidth(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int w = 0;
    WITH_STR(0, s, w = gfx_text_width(s));
    return JS_NewInt32(ctx, w);
}

/* bitmap(x, y, w, h, art, [palette...]) */
static JSValue g_bitmap(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_color pal[16] = { 0 };
    if (argc > 5) {
        for (uint32_t i = 0; i < 16; i++) {
            JSValue v = JS_GetPropertyUint32(ctx, argv[5], i);
            int32_t c = 0;
            if (!JS_IsUndefined(v))
                JS_ToInt32(ctx, &c, v);
            pal[i] = (gfx_color)c;
            JS_FreeValue(ctx, v);
        }
    }
    WITH_STR(4, art, gfx_bitmap(S, A(0), A(1), A(2), A(3), art, pal));
    return JS_UNDEFINED;
}

static JSValue g_clip(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    if (argc == 0)
        gfx_noclip(S);
    else
        gfx_clip(S, A(0), A(1), A(2), A(3));
    return JS_UNDEFINED;
}

static JSValue g_flush(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    if (argc < 4)
        gfx_flush_all();
    else
        gfx_flush(A(0), A(1), A(2), A(3));
    return JS_UNDEFINED;
}

static JSValue g_cursor(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_cursor_move(A(0), A(1));
    return JS_UNDEFINED;
}

static JSValue g_cursorVisible(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_cursor_show(B(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

static JSValue g_poll(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_event ev;
    int r = gfx_poll(&ev, I(ctx, argc, argv, 0, -1));
    if (r <= 0)
        return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "time", JS_NewUint32(ctx, ev.time_ms));
    if (ev.type == EV_MOUSE) {
        JS_SetPropertyStr(ctx, o, "type", JS_NewString(ctx, "mouse"));
        JS_SetPropertyStr(ctx, o, "dx", JS_NewInt32(ctx, ev.dx));
        JS_SetPropertyStr(ctx, o, "dy", JS_NewInt32(ctx, ev.dy));
        JS_SetPropertyStr(ctx, o, "buttons", JS_NewInt32(ctx, (int)ev.buttons));
    } else {
        JS_SetPropertyStr(ctx, o, "type", JS_NewString(ctx, "key"));
        JS_SetPropertyStr(ctx, o, "code", JS_NewInt32(ctx, ev.code));
        JS_SetPropertyStr(ctx, o, "down", JS_NewBool(ctx, ev.value));
        JS_SetPropertyStr(ctx, o, "mods", JS_NewInt32(ctx, (int)ev.buttons));
        char ch[2] = { (char)ev.ch, 0 };
        JS_SetPropertyStr(ctx, o, "ch", JS_NewString(ctx, ev.ch ? ch : ""));
    }
    return o;
}

/* ---- classic widgets ---- */
static JSValue g_bevel(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    ui_bevel(S, A(0), A(1), A(2), A(3), A(4));
    return JS_UNDEFINED;
}

static JSValue g_glyph(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    ui_glyph(S, A(0), A(1), A(2), (gfx_color)A(3));
    return JS_UNDEFINED;
}

static JSValue g_window(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    WITH_STR(4, t, ui_window(S, A(0), A(1), A(2), A(3), t, B(ctx, argc, argv, 5)));
    return JS_UNDEFINED;
}

static JSValue g_client(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    gfx_rect c;
    ui_client(A(0), A(1), A(2), A(3), &c);
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "x", JS_NewInt32(ctx, c.x));
    JS_SetPropertyStr(ctx, o, "y", JS_NewInt32(ctx, c.y));
    JS_SetPropertyStr(ctx, o, "w", JS_NewInt32(ctx, c.w));
    JS_SetPropertyStr(ctx, o, "h", JS_NewInt32(ctx, c.h));
    return o;
}

static JSValue g_hit(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    static const char *const names[] = { "none", "title", "close", "client", "frame", "resize", "minimize" };
    return JS_NewString(ctx, names[ui_window_hit(A(0), A(1), A(2), A(3), A(4), A(5))]);
}

static JSValue g_button(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    WITH_STR(4, l, ui_button(S, A(0), A(1), A(2), A(3), l, B(ctx, argc, argv, 5), B(ctx, argc, argv, 6)));
    return JS_UNDEFINED;
}

static JSValue g_checkbox(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    WITH_STR(2, l, ui_checkbox(S, A(0), A(1), l, B(ctx, argc, argv, 3)));
    return JS_UNDEFINED;
}

static JSValue g_field(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    ui_field(S, A(0), A(1), A(2), A(3));
    return JS_UNDEFINED;
}

static JSValue g_scrollbar(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    ui_scrollbar(S, A(0), A(1), A(2), B(ctx, argc, argv, 3), A(4), A(5), A(6));
    return JS_UNDEFINED;
}

static JSValue g_progress(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    ui_progress(S, A(0), A(1), A(2), A(3), A(4));
    return JS_UNDEFINED;
}

static JSValue g_groupbox(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    WITH_STR(4, l, ui_groupbox(S, A(0), A(1), A(2), A(3), l));
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry gfx_funcs[] = {
    JS_CFUNC_DEF("open", 2, g_open),
    JS_CFUNC_DEF("close", 0, g_close),
    JS_CFUNC_DEF("size", 0, g_size),
    JS_CFUNC_DEF("fill", 5, g_fill),
    JS_CFUNC_DEF("frame", 5, g_frame),
    JS_CFUNC_DEF("line", 5, g_line),
    JS_CFUNC_DEF("pixel", 3, g_pixel),
    JS_CFUNC_DEF("gradient", 6, g_gradient),
    JS_CFUNC_DEF("dither", 6, g_dither),
    JS_CFUNC_DEF("text", 6, g_text),
    JS_CFUNC_DEF("textWidth", 1, g_textWidth),
    JS_CFUNC_DEF("bitmap", 6, g_bitmap),
    JS_CFUNC_DEF("clip", 4, g_clip),
    JS_CFUNC_DEF("flush", 4, g_flush),
    JS_CFUNC_DEF("cursor", 2, g_cursor),
    JS_CFUNC_DEF("cursorVisible", 1, g_cursorVisible),
    JS_CFUNC_DEF("poll", 1, g_poll),
    JS_CFUNC_DEF("bevel", 5, g_bevel),
    JS_CFUNC_DEF("glyph", 4, g_glyph),
    JS_CFUNC_DEF("window", 6, g_window),
    JS_CFUNC_DEF("client", 4, g_client),
    JS_CFUNC_DEF("hit", 6, g_hit),
    JS_CFUNC_DEF("button", 7, g_button),
    JS_CFUNC_DEF("checkbox", 4, g_checkbox),
    JS_CFUNC_DEF("field", 4, g_field),
    JS_CFUNC_DEF("scrollbar", 7, g_scrollbar),
    JS_CFUNC_DEF("progress", 5, g_progress),
    JS_CFUNC_DEF("groupbox", 5, g_groupbox),
    JS_PROP_INT32_DEF("FONT_W", GFX_FONT_W, 0),
    JS_PROP_INT32_DEF("FONT_H", GFX_FONT_H, 0),
    JS_PROP_INT32_DEF("BOLD", GFX_BOLD, 0),
    JS_PROP_INT32_DEF("UNDERLINE", GFX_UNDERLINE, 0),
    JS_PROP_INT32_DEF("TITLE_H", UI_TITLE_H, 0),
    JS_PROP_INT32_DEF("BORDER", UI_BORDER, 0),
    JS_PROP_INT32_DEF("SCROLL_W", UI_SCROLL_W, 0),
    JS_PROP_INT32_DEF("RAISED", UI_RAISED, 0),
    JS_PROP_INT32_DEF("SUNKEN", UI_SUNKEN, 0),
    JS_PROP_INT32_DEF("BUTTON_UP", UI_BUTTON_UP, 0),
    JS_PROP_INT32_DEF("BUTTON_DOWN", UI_BUTTON_DOWN, 0),
    JS_PROP_INT32_DEF("FIELD_EDGE", UI_FIELD_EDGE, 0),
    JS_PROP_INT32_DEF("WINDOW_EDGE", UI_WINDOW_EDGE, 0),
    JS_PROP_INT32_DEF("GROOVE", UI_GROOVE, 0),
    JS_PROP_INT32_DEF("GLYPH_CLOSE", UI_GLYPH_CLOSE, 0),
    JS_PROP_INT32_DEF("GLYPH_MIN", UI_GLYPH_MIN, 0),
    JS_PROP_INT32_DEF("GLYPH_MAX", UI_GLYPH_MAX, 0),
    JS_PROP_INT32_DEF("GLYPH_UP", UI_GLYPH_UP, 0),
    JS_PROP_INT32_DEF("GLYPH_DOWN", UI_GLYPH_DOWN, 0),
    JS_PROP_INT32_DEF("GLYPH_LEFT", UI_GLYPH_LEFT, 0),
    JS_PROP_INT32_DEF("GLYPH_RIGHT", UI_GLYPH_RIGHT, 0),
    JS_PROP_INT32_DEF("GLYPH_CHECK", UI_GLYPH_CHECK, 0),
    JS_PROP_INT32_DEF("DESKTOP", UI_DESKTOP, 0),
    JS_PROP_INT32_DEF("FACE", UI_FACE, 0),
    JS_PROP_INT32_DEF("HILIGHT", UI_HILIGHT, 0),
    JS_PROP_INT32_DEF("SHADOW", UI_SHADOW, 0),
    JS_PROP_INT32_DEF("DKSHADOW", UI_DKSHADOW, 0),
    JS_PROP_INT32_DEF("TITLE", UI_TITLE, 0),
    JS_PROP_INT32_DEF("TEXT", UI_TEXT, 0),
    JS_PROP_INT32_DEF("SELECT", UI_SELECT, 0),
    JS_PROP_INT32_DEF("SELECT_TEXT", UI_SELECT_TEXT, 0),
    JS_PROP_INT32_DEF("FIELD", UI_FIELD, 0),
    JS_PROP_INT32_DEF("DISABLED", UI_DISABLED, 0),
    JS_PROP_INT32_DEF("KEY_ESC", KEY_ESC, 0),
    JS_PROP_INT32_DEF("KEY_BACKSPACE", KEY_BACKSPACE, 0),
    JS_PROP_INT32_DEF("KEY_TAB", KEY_TAB, 0),
    JS_PROP_INT32_DEF("KEY_ENTER", KEY_ENTER, 0),
    JS_PROP_INT32_DEF("KEY_UP", KEY_UP, 0),
    JS_PROP_INT32_DEF("KEY_DOWN", KEY_DOWN, 0),
    JS_PROP_INT32_DEF("KEY_LEFT", KEY_LEFT, 0),
    JS_PROP_INT32_DEF("KEY_RIGHT", KEY_RIGHT, 0),
    JS_PROP_INT32_DEF("KEY_HOME", KEY_HOME, 0),
    JS_PROP_INT32_DEF("KEY_END", KEY_END, 0),
    JS_PROP_INT32_DEF("KEY_PGUP", KEY_PGUP, 0),
    JS_PROP_INT32_DEF("KEY_PGDN", KEY_PGDN, 0),
    JS_PROP_INT32_DEF("KEY_DELETE", KEY_DELETE, 0),
    JS_PROP_INT32_DEF("KEY_F1", KEY_F1, 0),
    JS_PROP_INT32_DEF("MOD_SHIFT", MOD_SHIFT, 0),
    JS_PROP_INT32_DEF("MOD_CTRL", MOD_CTRL, 0),
    JS_PROP_INT32_DEF("MOD_ALT", MOD_ALT, 0),
    JS_PROP_INT32_DEF("BTN_LEFT", BTN_LEFT, 0),
    JS_PROP_INT32_DEF("BTN_RIGHT", BTN_RIGHT, 0),
};

static int gfx_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, gfx_funcs, sizeof gfx_funcs / sizeof gfx_funcs[0]);
}

JSModuleDef *kjs_module_gfx(JSContext *ctx, const char *name)
{
    JSModuleDef *m = JS_NewCModule(ctx, name, gfx_init);
    if (m)
        JS_AddModuleExportList(ctx, m, gfx_funcs, sizeof gfx_funcs / sizeof gfx_funcs[0]);
    return m;
}
#endif
