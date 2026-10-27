/* The classic 16-color palette and the "3D" UI scheme of early-90s desktops. */
#ifndef _GFX_COLORS_H
#define _GFX_COLORS_H

#define GFX_BLACK        0x000000
#define GFX_MAROON       0x800000
#define GFX_GREEN        0x008000
#define GFX_OLIVE        0x808000
#define GFX_NAVY         0x000080
#define GFX_PURPLE       0x800080
#define GFX_TEAL         0x008080
#define GFX_SILVER       0xC0C0C0
#define GFX_GRAY         0x808080
#define GFX_RED          0xFF0000
#define GFX_LIME         0x00FF00
#define GFX_YELLOW       0xFFFF00
#define GFX_BLUE         0x0000FF
#define GFX_FUCHSIA      0xFF00FF
#define GFX_AQUA         0x00FFFF
#define GFX_WHITE        0xFFFFFF

/* UI scheme */
#define UI_DESKTOP       GFX_TEAL
#define UI_FACE          GFX_SILVER       /* button / window face */
#define UI_HILIGHT       GFX_WHITE        /* top-left bevel */
#define UI_LIGHT         0xDFDFDF
#define UI_SHADOW        GFX_GRAY         /* bottom-right bevel */
#define UI_DKSHADOW      GFX_BLACK
#define UI_TITLE         GFX_NAVY         /* active title bar */
#define UI_TITLE2        0x1084D0         /* active title gradient end */
#define UI_TITLE_INACT   GFX_GRAY
#define UI_TITLE_INACT2  0xB5B5B5
#define UI_TITLE_TEXT    GFX_WHITE
#define UI_TEXT          GFX_BLACK
#define UI_SELECT        GFX_NAVY         /* selection background */
#define UI_SELECT_TEXT   GFX_WHITE
#define UI_FIELD         GFX_WHITE        /* edit / list background */
#define UI_DISABLED      GFX_GRAY

#endif
