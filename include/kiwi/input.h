/*
 * /dev/input - keyboard and mouse events.
 *
 * While /dev/input is open the keyboard is grabbed: key presses become
 * events here instead of tty input.  read() returns whole events and blocks
 * until at least one is available (see INPUT_SET_TIMEOUT).
 */
#ifndef _KIWI_INPUT_H
#define _KIWI_INPUT_H

#include <stdint.h>

#define INPUT_SET_TIMEOUT 0x6900   /* arg: ms; 0 = block forever, -1 = never block */

#define EV_KEY    1
#define EV_MOUSE  2

#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4

/* key codes are PS/2 set-1 scancodes; extended keys have 0x100 added */
#define KEY_ESC        0x01
#define KEY_BACKSPACE  0x0E
#define KEY_TAB        0x0F
#define KEY_ENTER      0x1C
#define KEY_LCTRL      0x1D
#define KEY_LSHIFT     0x2A
#define KEY_RSHIFT     0x36
#define KEY_LALT       0x38
#define KEY_SPACE      0x39
#define KEY_F1         0x3B
#define KEY_F10        0x44
#define KEY_HOME       0x147
#define KEY_UP         0x148
#define KEY_PGUP       0x149
#define KEY_LEFT       0x14B
#define KEY_RIGHT      0x14D
#define KEY_END        0x14F
#define KEY_DOWN       0x150
#define KEY_PGDN       0x151
#define KEY_INSERT     0x152
#define KEY_DELETE     0x153

#define BTN_LEFT   1
#define BTN_RIGHT  2
#define BTN_MIDDLE 4

struct kiwi_input_event {
    uint32_t time_ms;     /* uptime when the event happened */
    uint16_t type;        /* EV_KEY / EV_MOUSE */
    uint16_t code;        /* key code (EV_KEY) */
    int32_t  value;       /* 1 = press, 0 = release (EV_KEY) */
    int32_t  dx, dy;      /* relative motion, screen orientation (EV_MOUSE) */
    uint32_t buttons;     /* BTN_* (EV_MOUSE) or MOD_* (EV_KEY) */
    uint32_t ch;          /* translated character, 0 if none (EV_KEY) */
};

#endif
