/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "litest.h"
#include "litest-int.h"

static void
litest_ms_surface_cover_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_MS_SURFACE_COVER);
	litest_set_current_device(d);
}

static struct input_event down[] = {
	{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct input_event move[] = {
	{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

/* We define down/move so that we can emulate fake touches on this device,
   to make sure nothing crashes. */
static struct litest_device_interface interface = {
	.touch_down_events = down,
	.touch_move_events = move,
};

static struct input_absinfo absinfo[] = {
	{ ABS_VOLUME, 0, 1023, 0, 0, 0 },
	{ ABS_MISC, 0, 255, 0, 0, 0 },
	{ 41, 0, 255, 0, 0, 0 },
	{ 42, -127, 127, 0, 0, 0 },
	{ 43, -127, 127, 0, 0, 0 },
	{ 44, -127, 127, 0, 0, 0 },
	{ 45, -127, 127, 0, 0, 0 },
	{ 46, -127, 127, 0, 0, 0 },
	{ 47, -127, 127, 0, 0, 0 },
	/* ABS_MT range overlap starts here */
	{ 48, -127, 127, 0, 0, 0 }, /* ABS_MT_SLOT */
	{ 49, -127, 127, 0, 0, 0 },
	{ 50, -127, 127, 0, 0, 0 },
	{ 51, -127, 127, 0, 0, 0 },
	{ 52, -127, 127, 0, 0, 0 },
	{ 53, -127, 127, 0, 0, 0 },
	{ 54, -127, 127, 0, 0, 0 },
	{ 55, -127, 127, 0, 0, 0 },
	{ 56, -127, 127, 0, 0, 0 },
	{ 57, -127, 127, 0, 0, 0 },
	{ 58, -127, 127, 0, 0, 0 },
	{ 59, -127, 127, 0, 0, 0 },
	{ 60, -127, 127, 0, 0, 0 },
	{ 61, -127, 127, 0, 0, 0 }, /* ABS_MT_TOOL_Y */
	{ 62, -127, 127, 0, 0, 0 },
	{ .value = -1 },
};

static struct input_id input_id = {
	.bustype = 0x3,
	.vendor = 0x45e,
	.product = 0x7a9,
};

static int events[] = {
	EV_REL, REL_X,
	EV_REL, REL_Y,
	EV_REL, REL_HWHEEL,
	EV_REL, REL_DIAL,
	EV_REL, REL_WHEEL,
	EV_KEY, KEY_ESC,
	EV_KEY, KEY_1,
	EV_KEY, KEY_2,
	EV_KEY, KEY_3,
	EV_KEY, KEY_4,
	EV_KEY, KEY_5,
	EV_KEY, KEY_6,
	EV_KEY, KEY_7,
	EV_KEY, KEY_8,
	EV_KEY, KEY_9,
	EV_KEY, KEY_0,
	EV_KEY, KEY_MINUS,
	EV_KEY, KEY_EQUAL,
	EV_KEY, KEY_BACKSPACE,
	EV_KEY, KEY_TAB,
	EV_KEY, KEY_Q,
	EV_KEY, KEY_W,
	EV_KEY, KEY_E,
	EV_KEY, KEY_R,
	EV_KEY, KEY_T,
	EV_KEY, KEY_Y,
	EV_KEY, KEY_U,
	EV_KEY, KEY_I,
	EV_KEY, KEY_O,
	EV_KEY, KEY_P,
	EV_KEY, KEY_LEFTBRACE,
	EV_KEY, KEY_RIGHTBRACE,
	EV_KEY, KEY_ENTER,
	EV_KEY, KEY_LEFTCTRL,
	EV_KEY, KEY_A,
	EV_KEY, KEY_S,
	EV_KEY, KEY_D,
	EV_KEY, KEY_F,
	EV_KEY, KEY_G,
	EV_KEY, KEY_H,
	EV_KEY, KEY_J,
	EV_KEY, KEY_K,
	EV_KEY, KEY_L,
	EV_KEY, KEY_SEMICOLON,
	EV_KEY, KEY_APOSTROPHE,
	EV_KEY, KEY_GRAVE,
	EV_KEY, KEY_LEFTSHIFT,
	EV_KEY, KEY_BACKSLASH,
	EV_KEY, KEY_Z,
	EV_KEY, KEY_X,
	EV_KEY, KEY_C,
	EV_KEY, KEY_V,
	EV_KEY, KEY_B,
	EV_KEY, KEY_N,
	EV_KEY, KEY_M,
	EV_KEY, KEY_COMMA,
	EV_KEY, KEY_DOT,
	EV_KEY, KEY_SLASH,
	EV_KEY, KEY_RIGHTSHIFT,
	EV_KEY, KEY_KPASTERISK,
	EV_KEY, KEY_LEFTALT,
	EV_KEY, KEY_SPACE,
	EV_KEY, KEY_CAPSLOCK,
	EV_KEY, KEY_F1,
	EV_KEY, KEY_F2,
	EV_KEY, KEY_F3,
	EV_KEY, KEY_F4,
	EV_KEY, KEY_F5,
	EV_KEY, KEY_F6,
	EV_KEY, KEY_F7,
	EV_KEY, KEY_F8,
	EV_KEY, KEY_F9,
	EV_KEY, KEY_F10,
	EV_KEY, KEY_NUMLOCK,
	EV_KEY, KEY_SCROLLLOCK,
	EV_KEY, KEY_KP7,
	EV_KEY, KEY_KP8,
	EV_KEY, KEY_KP9,
	EV_KEY, KEY_KPMINUS,
	EV_KEY, KEY_KP4,
	EV_KEY, KEY_KP5,
	EV_KEY, KEY_KP6,
	EV_KEY, KEY_KPPLUS,
	EV_KEY, KEY_KP1,
	EV_KEY, KEY_KP2,
	EV_KEY, KEY_KP3,
	EV_KEY, KEY_KP0,
	EV_KEY, KEY_KPDOT,
	EV_KEY, KEY_102ND,
	EV_KEY, KEY_F11,
	EV_KEY, KEY_F12,
	EV_KEY, KEY_RO,
	EV_KEY, KEY_HENKAN,
	EV_KEY, KEY_KATAKANAHIRAGANA,
	EV_KEY, KEY_MUHENKAN,
	EV_KEY, KEY_KPJPCOMMA,
	EV_KEY, KEY_KPENTER,
	EV_KEY, KEY_RIGHTCTRL,
	EV_KEY, KEY_KPSLASH,
	EV_KEY, KEY_SYSRQ,
	EV_KEY, KEY_RIGHTALT,
	EV_KEY, KEY_HOME,
	EV_KEY, KEY_UP,
	EV_KEY, KEY_PAGEUP,
	EV_KEY, KEY_LEFT,
	EV_KEY, KEY_RIGHT,
	EV_KEY, KEY_END,
	EV_KEY, KEY_DOWN,
	EV_KEY, KEY_PAGEDOWN,
	EV_KEY, KEY_INSERT,
	EV_KEY, KEY_DELETE,
	EV_KEY, KEY_MUTE,
	EV_KEY, KEY_VOLUMEDOWN,
	EV_KEY, KEY_VOLUMEUP,
	EV_KEY, KEY_POWER,
	EV_KEY, KEY_KPEQUAL,
	EV_KEY, KEY_PAUSE,
	EV_KEY, KEY_KPCOMMA,
	EV_KEY, KEY_HANGEUL,
	EV_KEY, KEY_HANJA,
	EV_KEY, KEY_YEN,
	EV_KEY, KEY_LEFTMETA,
	EV_KEY, KEY_RIGHTMETA,
	EV_KEY, KEY_COMPOSE,
	EV_KEY, KEY_STOP,
	EV_KEY, KEY_AGAIN,
	EV_KEY, KEY_PROPS,
	EV_KEY, KEY_UNDO,
	EV_KEY, KEY_FRONT,
	EV_KEY, KEY_COPY,
	EV_KEY, KEY_OPEN,
	EV_KEY, KEY_PASTE,
	EV_KEY, KEY_FIND,
	EV_KEY, KEY_CUT,
	EV_KEY, KEY_HELP,
	EV_KEY, KEY_MENU,
	EV_KEY, KEY_CALC,
	EV_KEY, KEY_SLEEP,
	EV_KEY, KEY_FILE,
	EV_KEY, KEY_WWW,
	EV_KEY, KEY_COFFEE,
	EV_KEY, KEY_MAIL,
	EV_KEY, KEY_BOOKMARKS,
	EV_KEY, KEY_BACK,
	EV_KEY, KEY_FORWARD,
	EV_KEY, KEY_EJECTCD,
	EV_KEY, KEY_NEXTSONG,
	EV_KEY, KEY_PLAYPAUSE,
	EV_KEY, KEY_PREVIOUSSONG,
	EV_KEY, KEY_STOPCD,
	EV_KEY, KEY_RECORD,
	EV_KEY, KEY_REWIND,
	EV_KEY, KEY_PHONE,
	EV_KEY, KEY_CONFIG,
	EV_KEY, KEY_HOMEPAGE,
	EV_KEY, KEY_REFRESH,
	EV_KEY, KEY_EXIT,
	EV_KEY, KEY_EDIT,
	EV_KEY, KEY_SCROLLUP,
	EV_KEY, KEY_SCROLLDOWN,
	EV_KEY, KEY_NEW,
	EV_KEY, KEY_REDO,
	EV_KEY, KEY_F13,
	EV_KEY, KEY_F14,
	EV_KEY, KEY_F15,
	EV_KEY, KEY_F16,
	EV_KEY, KEY_F17,
	EV_KEY, KEY_F18,
	EV_KEY, KEY_F19,
	EV_KEY, KEY_F20,
	EV_KEY, KEY_F21,
	EV_KEY, KEY_F22,
	EV_KEY, KEY_F23,
	EV_KEY, KEY_F24,
	EV_KEY, KEY_CLOSE,
	EV_KEY, KEY_PLAY,
	EV_KEY, KEY_FASTFORWARD,
	EV_KEY, KEY_BASSBOOST,
	EV_KEY, KEY_PRINT,
	EV_KEY, KEY_CAMERA,
	EV_KEY, KEY_CHAT,
	EV_KEY, KEY_SEARCH,
	EV_KEY, KEY_FINANCE,
	EV_KEY, KEY_CANCEL,
	EV_KEY, KEY_BRIGHTNESSDOWN,
	EV_KEY, KEY_BRIGHTNESSUP,
	EV_KEY, KEY_KBDILLUMTOGGLE,
	EV_KEY, KEY_SEND,
	EV_KEY, KEY_REPLY,
	EV_KEY, KEY_FORWARDMAIL,
	EV_KEY, KEY_SAVE,
	EV_KEY, KEY_DOCUMENTS,
	EV_KEY, KEY_UNKNOWN,
	EV_KEY, KEY_VIDEO_NEXT,
	EV_KEY, KEY_BRIGHTNESS_ZERO,
	EV_KEY, BTN_0,
	EV_KEY, BTN_LEFT,
	EV_KEY, BTN_RIGHT,
	EV_KEY, BTN_MIDDLE,
	EV_KEY, BTN_SIDE,
	EV_KEY, BTN_EXTRA,
	EV_KEY, KEY_SELECT,
	EV_KEY, KEY_GOTO,
	EV_KEY, KEY_INFO,
	EV_KEY, KEY_PROGRAM,
	EV_KEY, KEY_PVR,
	EV_KEY, KEY_SUBTITLE,
	EV_KEY, KEY_ZOOM,
	EV_KEY, KEY_KEYBOARD,
	EV_KEY, KEY_PC,
	EV_KEY, KEY_TV,
	EV_KEY, KEY_TV2,
	EV_KEY, KEY_VCR,
	EV_KEY, KEY_VCR2,
	EV_KEY, KEY_SAT,
	EV_KEY, KEY_CD,
	EV_KEY, KEY_TAPE,
	EV_KEY, KEY_TUNER,
	EV_KEY, KEY_PLAYER,
	EV_KEY, KEY_DVD,
	EV_KEY, KEY_AUDIO,
	EV_KEY, KEY_VIDEO,
	EV_KEY, KEY_MEMO,
	EV_KEY, KEY_CALENDAR,
	EV_KEY, KEY_RED,
	EV_KEY, KEY_GREEN,
	EV_KEY, KEY_YELLOW,
	EV_KEY, KEY_BLUE,
	EV_KEY, KEY_CHANNELUP,
	EV_KEY, KEY_CHANNELDOWN,
	EV_KEY, KEY_LAST,
	EV_KEY, KEY_NEXT,
	EV_KEY, KEY_RESTART,
	EV_KEY, KEY_SLOW,
	EV_KEY, KEY_SHUFFLE,
	EV_KEY, KEY_PREVIOUS,
	EV_KEY, KEY_VIDEOPHONE,
	EV_KEY, KEY_GAMES,
	EV_KEY, KEY_ZOOMIN,
	EV_KEY, KEY_ZOOMOUT,
	EV_KEY, KEY_ZOOMRESET,
	EV_KEY, KEY_WORDPROCESSOR,
	EV_KEY, KEY_EDITOR,
	EV_KEY, KEY_SPREADSHEET,
	EV_KEY, KEY_GRAPHICSEDITOR,
	EV_KEY, KEY_PRESENTATION,
	EV_KEY, KEY_DATABASE,
	EV_KEY, KEY_NEWS,
	EV_KEY, KEY_VOICEMAIL,
	EV_KEY, KEY_ADDRESSBOOK,
	EV_KEY, KEY_MESSENGER,
	EV_KEY, KEY_DISPLAYTOGGLE,
	EV_KEY, KEY_SPELLCHECK,
	EV_KEY, KEY_LOGOFF,
	EV_KEY, KEY_MEDIA_REPEAT,
	EV_KEY, KEY_IMAGES,
	EV_KEY, 576,
	EV_KEY, 577,
	EV_KEY, 578,
	EV_KEY, 579,
	EV_KEY, 580,
	EV_KEY, 581,
	EV_KEY, 582,
	EV_KEY, 592,
	EV_KEY, 593,
	EV_KEY, 608,
	EV_KEY, 609,
	EV_KEY, 610,
	EV_KEY, 611,
	EV_KEY, 612,
	EV_KEY, 613,
	EV_LED, LED_NUML,
	EV_LED, LED_CAPSL,
	EV_LED, LED_SCROLLL,
	-1, -1,
};

struct litest_test_device litest_ms_surface_cover_device = {
	.type = LITEST_MS_SURFACE_COVER,
	.features = LITEST_KEYBOARD | LITEST_RELATIVE | LITEST_FAKE_MT,
	.shortname = "MS surface cover",
	.setup = litest_ms_surface_cover_setup,
	.interface = &interface,

	.name = "MICROSOFT SAM",
	.id = &input_id,
	.events = events,
	.absinfo = absinfo,
};
