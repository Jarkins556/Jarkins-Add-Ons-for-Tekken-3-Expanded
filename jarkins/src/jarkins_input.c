/*
 * Jarkins Lab: the Input Display.
 *
 * What it does
 *   Once a frame it reads the pad the player is using and remembers every
 *   change. In Practice it draws those changes as a strip in a corner, in the
 *   move list's own notation: a direction arrow, then the attack buttons that
 *   were held with it (square 1, triangle 2, cross 3, circle 4), oldest on the
 *   left, newest on the right.
 *
 * Where the input comes from
 *   sio_get_pad_buttons_slot(), the runtime's own pad state (sio.h): the same
 *   active-low bits the game reads, after the host keymap and any other mod's
 *   filter. Reading it changes nothing, so this mod never wraps input and
 *   cannot interfere with TMST's filters.
 *
 * Where it shows
 *   Practice only: the fight screen (SCREEN 8) with the game's mode word at
 *   PRACTICE (5) — the same test TMST's pause row makes. Arcade, VS, Team
 *   Battle, Survival, Ball and Force are left alone.
 *
 * How it draws
 *   Into a picture of its own, never into the game's frame. The strip is
 *   rendered into an ARGB bitmap and handed to the runtime's host overlay,
 *   which is composited when the frame is presented: Practice's menus freeze
 *   the frame they open on and use it as their background, so a strip drawn
 *   into VRAM gets baked into every menu after it and stays there. Nothing of
 *   this strip is ever in VRAM, so nothing of it can be left behind, on any
 *   screen, and no drawing state the game relies on is touched.
 *
 *   The icons are ours: eight arrows and the four PlayStation buttons, drawn
 *   by tools/make_icons.py and baked into jarkins_icons.h as 16 x 16 pictures
 *   with a transparent background. Each entry in the strip is a direction
 *   arrow (or a dot for none) followed by one glyph per button held with it.
 *
 * Debugging
 *   JARKINS_INPUT_LOG=1        what it sampled, and every link, on stderr.
 *   JARKINS_INPUT_ANY_SCREEN=1 draws outside Practice too (checking the
 *                              drawing without playing).
 */
#include "mod_plugins.h"
#include "jarkins_video.h"
#include "jarkins_osd.h"
#include "sio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKAGE "jarkins.lab"
#define FEATURE "input-display"

/* The game's screen and mode words, as TMST reads them (tmst_pause.c). */
#include "jarkins_icons.h"

#define SCREEN        0x800ae204u
#define PHASE         0x800ae224u
#define MODE          0x800afa88u
#define SCREEN_FIGHT  8
#define MODE_PRACTICE 5
/* The fight's own phase: below it the state is still set-up (the loading
 * screen, the way in), which draws for a while after STATE has already read 8.
 * Only a lower bound: within one live match the word has read both 6 and 8, so
 * it cannot tell the match from a menu. That is what PAUSED is for. */
enum { PHASE_FIGHT = 6 };
/* The fight's pause: 0 while the match runs, 1 or 2 for the player who paused.
 * Practice's own panels (1P FREESTYLE and the rest) are that pause, drawn over
 * the match by its overlay - TMST reads the same word for the same reason -
 * so this is what keeps the strip off the menu. */
enum { PAUSED = 0x80095494 };
/* Set every frame by input_vblank: START or SELECT is down, or went down. */
static int panel_key;
/* JARKINS_INPUT_ANY_SCREEN: for checking the drawing itself, the strip is not
 * kept to Practice. The keys still keep it off; the inputs are still its own. */
static int any_screen;
/* Set by START+SELECT, the way out of Practice: keeps the strip off until the
 * match is running again, so the way out and the menus it leads to are clear. */
static int leaving;

/* Pad bits: 1 = released (sio.h's PS1 layout). */
enum {
    PAD_SELECT   = 0x0001,
    PAD_START    = 0x0008,
    PAD_UP       = 0x0010,
    PAD_RIGHT    = 0x0020,
    PAD_DOWN     = 0x0040,
    PAD_LEFT     = 0x0080,
    PAD_TRIANGLE = 0x1000,
    PAD_CIRCLE   = 0x2000,
    PAD_CROSS    = 0x4000,
    PAD_SQUARE   = 0x8000
};

enum { MAX_ENTRIES = 16, DEFAULT_ENTRIES = 8 };

/* A symbol's box, the gap after it, and the wider gap between inputs. */
enum { SYM = 16, SYM_GAP = 2, ENTRY_GAP = 6 };
enum { SCREEN_W = 368, SCREEN_H = 480, MARGIN = 6 };

/* The strip draws into the add-on's shared overlay canvas (jarkins_osd.h), in
 * the game's own coordinates, so it can share the screen with the readouts
 * instead of the two replacing each other's picture every frame. */

/* GP0 colours: 0x00BBGGRR, the move list's own notation colours. */
#define COLOR(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))
#define C_NONE   COLOR(0x50, 0x50, 0x58)   /* nothing held */
#define C_PANEL  COLOR(0x00, 0x00, 0x00)   /* the panel behind the strip */

/* Where the strip sits (the "corner" option). */
enum { CORNER_BOTTOM_CENTRE, CORNER_BOTTOM_LEFT, CORNER_BOTTOM_RIGHT, CORNER_TOP_LEFT, CORNER_TOP_RIGHT };

/* --- what the player chose ------------------------------------------------- */

static int option_int(const char *id, int fallback, int lo, int hi) {
    char text[16] = "";
    if (!psx_mod_option_value(PACKAGE, FEATURE, id, text, sizeof text) || !text[0])
        return fallback;
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value < lo || value > hi)
        return fallback;
    return (int)value;
}

static int option_corner(void) {
    char text[32] = "";
    if (!psx_mod_option_value(PACKAGE, FEATURE, "corner", text, sizeof text))
        return CORNER_BOTTOM_CENTRE;
    if (strcmp(text, "bottom-left") == 0) return CORNER_BOTTOM_LEFT;
    if (strcmp(text, "bottom-right") == 0) return CORNER_BOTTOM_RIGHT;
    if (strcmp(text, "top-left") == 0) return CORNER_TOP_LEFT;
    if (strcmp(text, "top-right") == 0) return CORNER_TOP_RIGHT;
    return CORNER_BOTTOM_CENTRE;
}

static int logging(void) {
    static int on = -1;
    if (on < 0) {
        const char *env = getenv("JARKINS_INPUT_LOG");
        on = env && *env && *env != '0';
    }
    return on;
}

/* --- state ---------------------------------------------------------------- */

static int armed;                   /* this launch's plan switched it on */
static int entries = DEFAULT_ENTRIES;
static int player = 1;
static int corner = CORNER_BOTTOM_CENTRE;

static uint16_t hist[MAX_ENTRIES];   /* the states, oldest first, newest last */
static int hist_n;

static unsigned frame;
static unsigned last_log;

/* --- the strip's picture --------------------------------------------------- */

/* The strip is drawn into a picture of its own and handed to the runtime's
 * host overlay, which is composited when the frame is presented. Nothing of it
 * ever enters VRAM, where Practice's menus would freeze it into their
 * background and keep showing it on every screen after. The picture is ARGB
 * (0xAARRGGBB, as host_osd takes it); the colours below are GP0's 0x00BBGGRR. */
static uint32_t cv_argb(uint32_t colour) {
    return 0xFF000000u | ((colour & 0xFFu) << 16) | (colour & 0xFF00u) |
           ((colour >> 16) & 0xFFu);
}

static void cv_put(int x, int y, uint32_t argb) {
    jarkins_osd_put(x, y, argb);
}

/* An opaque filled box. */
static void quad(int x, int y, int w, int h, uint32_t colour) {
    const uint32_t c = cv_argb(colour);
    for (int r = 0; r < h; r++)
        for (int c2 = 0; c2 < w; c2++)
            cv_put(x + c2, y + r, c);
}

/* A box blended half and half with what is already in the picture: the panel
 * the strip sits on, darkening what is under it the way the game's own hint
 * bars do, instead of covering it. */
static void quad_half(int x, int y, int w, int h, uint32_t colour) {
    jarkins_osd_quad_half(x, y, w, h, colour);
}

/* The canvas is cleared and presented once per frame by jarkins_osd_frame, so
 * these two are no-ops: they stay where they are because the build reads better
 * with them than with nothing at all. */
static void build_begin(void) {}

/* Hand the picture to the overlay: it is drawn over the presented frame, in
 * the game's own 368x480 coordinates. */
static void build_end(void) {}

/* One tile, scaled to SYM x SYM with its nearest pixel, flipped as asked. A
 * zero pixel is see-through, so the panel shows round the arrow's and the
 * lobes' edges. */
static void icon_draw(int x, int y, int id, int flip_h, int flip_v) {
    if (id < 0 || id >= ICON_COUNT)
        return;
    const jarkins_icon *ic = &jarkins_icons[id];
    for (int ry = 0; ry < SYM; ry++) {
        int sy = ry * (int)ic->h / SYM;
        if (flip_v)
            sy = (int)ic->h - 1 - sy;
        for (int rx = 0; rx < SYM; rx++) {
            int sx = rx * (int)ic->w / SYM;
            if (flip_h)
                sx = (int)ic->w - 1 - sx;
            const uint32_t c = ic->px[sy * (int)ic->w + sx];
            if (c)
                cv_put(x + rx, y + ry, 0xFF000000u | c);
        }
    }
}

/* The pad is active low: a clear bit is a pressed button. */
static int held(uint16_t b, uint16_t bit) { return (b & bit) == 0; }

/* The three arrow tiles cover all eight directions: the diagonal points
 * down-left and the others down and left, so each flip turns one into the
 * direction asked for - the diagonal mirrors right (dx > 0) and up
 * (dy < 0), the down arrow only up, the left arrow only right. */
static void draw_arrow(int x, int y, int dx, int dy) {
    if (dx && dy)
        icon_draw(x, y, ICON_ARROW_DIAG, dx > 0, dy < 0);
    else if (dy)
        icon_draw(x, y, ICON_ARROW_DOWN, 0, dy < 0);
    else
        icon_draw(x, y, ICON_ARROW_LEFT, dx > 0, 0);
}

/* The four discs, lit for every button held at once: the cluster underneath,
 * then the tile that is those discs and nothing else. */
static int buttons_mask(uint16_t b) {
    return (held(b, PAD_SQUARE) ? 1 : 0) | (held(b, PAD_TRIANGLE) ? 2 : 0) |
           (held(b, PAD_CROSS) ? 4 : 0) | (held(b, PAD_CIRCLE) ? 8 : 0);
}

static int pressed_buttons(uint16_t b) {
    const int m = buttons_mask(b);
    return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1);
}

/* One input's width: the direction, then the cluster of buttons held with it. */
static int entry_width(uint16_t b) {
    return (pressed_buttons(b) ? 2 : 1) * (SYM + SYM_GAP) - SYM_GAP;
}

static void draw_entry(uint16_t b, int x, int y) {
    const int dx = (held(b, PAD_LEFT) ? -1 : 0) + (held(b, PAD_RIGHT) ? 1 : 0);
    const int dy = (held(b, PAD_UP) ? -1 : 0) + (held(b, PAD_DOWN) ? 1 : 0);
    int cx = x;
    if (dx || dy)
        draw_arrow(cx, y, dx, dy);
    else
        quad(cx + SYM / 2 - 1, y + SYM / 2 - 1, 2, 2, C_NONE);   /* nothing held */
    cx += SYM + SYM_GAP;
    const int mask = buttons_mask(b);
    if (mask) {
        icon_draw(cx, y, ICON_CLUSTER, 0, 0);
        icon_draw(cx, y, jarkins_dots_icon[mask], 0, 0);
    }
}


/* Where the strip's row starts, by corner. */
static int strip_y0(void) {
    const int bottom = corner != CORNER_TOP_LEFT && corner != CORNER_TOP_RIGHT;
    return bottom ? 458 - SYM : 24;         /* the game draws rows 20..467 */
}

/* The strip: the newest inputs that fit, left to right. */
static void build_strip(void) {
    const int right = corner == CORNER_BOTTOM_RIGHT || corner == CORNER_TOP_RIGHT;
    const int centred = corner == CORNER_BOTTOM_CENTRE;
    const int room = SCREEN_W - 2 * MARGIN;

    int first = hist_n, used = 0;
    while (first > 0) {                     /* newest first: drop what overflows */
        const int w = entry_width(hist[first - 1]);
        const int add = first == hist_n ? w : w + ENTRY_GAP;
        if (used + add > room) break;
        used += add;
        first--;
    }
    const int y0 = strip_y0();
    if (first == hist_n)                    /* not even the newest fits */
        return;
    const int x0 = centred ? (SCREEN_W - used) / 2 : right ? SCREEN_W - MARGIN - used : MARGIN;

    build_begin();
    /* The panel first, so the icons sit on it: black, half see-through, with a
     * little room around them. */
    quad_half(x0 - 5, y0 - 4, used + 10, SYM + 8, C_PANEL);
    int x = x0;
    for (int i = first; i < hist_n; i++) {
        draw_entry(hist[i], x, y0);
        x += entry_width(hist[i]) + (i + 1 < hist_n ? ENTRY_GAP : 0);
    }
    build_end();
}

/* --- the frame's ordering table ------------------------------------------- */

/* Ours is drawn when the strip has something to say and the game is in
 * Practice. Anything else leaves the frame exactly as the game built it. */
/* Whether the strip belongs on this screen at all; the inputs are what it has
 * to show. */
static int strip_live(void) {
    if (!psx_mod_game_started())
        return 0;
    if (any_screen)                     /* the words, but not the keys */
        return !panel_key;
    /* Practice only, the match itself, and nothing over it: STATE reads 8 from
     * the loading screen on and Practice's menu does not move it, so the phase
     * keeps out the set-up and the pause word keeps out the menu. The panel's
     * own key is the last of it: see panel_key in input_vblank. */
    return psx_mod_read_half(SCREEN) == SCREEN_FIGHT &&
           psx_mod_read_half(PHASE) >= PHASE_FIGHT &&
           psx_mod_read_word(MODE) == MODE_PRACTICE &&
           !psx_mod_read_word(PAUSED) && !panel_key;
}


/* --- plugins -------------------------------------------------------------- */

static void input_activate(void) {
    /* The upper VRAM half must exist before the renderer starts sampling
     * it, and activation is the only moment before that. */
    (void)jarkins_video_prepare();
    {
        const char *env = getenv("JARKINS_INPUT_ANY_SCREEN");
        any_screen = env && *env && *env != '0';
    }
    entries = option_int("entries", DEFAULT_ENTRIES, 4, MAX_ENTRIES);
    player  = option_int("player", 1, 1, 2);
    corner  = option_corner();
    hist_n  = 0;

    armed = 1;
    fprintf(stderr, "jarkins: input display armed (%d entries, pad %d, corner %d, practice only, "
                    "its own icons, drawn over the presented frame)\n",
            entries, player, corner);
}





static void input_vblank(void) {
    frame++;
    if (!armed)
        return;
    /* Opens this frame's shared canvas: the first overlay feature to reach its
     * vblank presents last frame's picture and clears it for this one's drawing. */
    jarkins_osd_frame(frame);
    const uint16_t now = sio_get_pad_buttons_slot(player - 1);
    /* Practice's panel freezes the frame it opens on, and the menus after it use
     * that frame as their background: a strip drawn there stays baked into them
     * until a new session draws the background again (the icons sat at the
     * bottom of MODE SELECT, which is what this is for). START or SELECT opens
     * that panel, so the strip is not drawn while either is down, nor on the
     * frame they went down. */
    {
        static uint16_t keys_before;
        const uint16_t keys = PAD_START | PAD_SELECT;
        panel_key = (now & keys) != keys || (keys_before & keys) != keys;
        keys_before = now;
    }
    const int live = strip_live();
    /* START+SELECT is how Practice is left: from that press the strip is off
     * until the match is running again, whatever the menus on the way out say
     * (the one after it reads screen 4, phase 1, mode 5 - still Practice's
     * mode). The inputs go with it. */
    /* Bits are 1 for released, so both held reads as both bits clear. */
    if ((now & (PAD_START | PAD_SELECT)) == 0) {
        leaving = 1;
        hist_n = 0;
    } else if (live) {
        leaving = 0;
    }
    /* Remembered inputs belong to the session the strip is on screen for: when
     * Practice's own words go (a menu, another mode, the way in) they are
     * dropped, so a new session starts clean and nothing of the old one can be
     * drawn. A live match stalls the motion words for a moment at a round
     * change; that hides the strip but keeps its inputs, since the words stay. */
    if (!any_screen &&
        (psx_mod_read_half(SCREEN) != SCREEN_FIGHT ||
         psx_mod_read_word(MODE) != MODE_PRACTICE))
        hist_n = 0;
    const int strip_now = live && !leaving && hist_n > 0;
    /* Rebuilt every frame it belongs on screen. When it does not, nothing is
     * drawn: the shared canvas starts empty every frame, so the strip is gone
     * from the presented picture without anything having to erase it. */
    if (strip_now)
        build_strip();
    /* Where the strip comes and goes, with the words that decide it. A pad
     * change cannot be the trigger: what matters is the frame the gate flips. */
    if (logging()) {
        static int last_strip = -1;
        const int strip = strip_now;
        if (strip != last_strip) {
            last_strip = strip;
            fprintf(stderr, "jarkins: strip %s (frame %u, screen %u phase %u mode %u paused %u leaving %d)\n",
                    strip ? "on" : "off", frame, (unsigned)psx_mod_read_half(SCREEN),
                    (unsigned)psx_mod_read_half(PHASE), (unsigned)psx_mod_read_word(MODE),
                    (unsigned)psx_mod_read_word(PAUSED), leaving);
        }
    }
    /* A pad change is what the strip records. In a menu there is no strip to
     * record it in, but the words are worth a line for anyone reading the log,
     * so the change is logged either way - against the pad itself, not against
     * the history, which is empty off screen. */
    static uint16_t last_pad = 0xFFFF;
    if (now == last_pad)
        return;
    last_pad = now;
    if (logging())
        fprintf(stderr, "jarkins: input %04x (frame %u, screen %u phase %u mode %u paused %u)\n",
                (unsigned)now, frame, (unsigned)psx_mod_read_half(SCREEN),
                (unsigned)psx_mod_read_half(PHASE), (unsigned)psx_mod_read_word(MODE),
                (unsigned)psx_mod_read_word(PAUSED));
    if (!live)
        return;
    if (hist_n == entries) {              /* the strip is full: oldest out */
        memmove(hist, hist + 1, sizeof hist[0] * (size_t)(entries - 1));
        hist_n--;
    }
    hist[hist_n++] = now;
}

PSX_MOD_CONSTRUCTOR(jarkins_register_input) {
    (void)psx_mod_register_activation_plugin("jarkins.input", input_activate);
    (void)psx_mod_register_vblank_plugin("jarkins.input", input_vblank);
}
