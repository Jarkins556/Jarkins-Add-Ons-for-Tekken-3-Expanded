/*
 * Jarkins Lab: damage readouts.
 *
 * What it shows, and where
 *   Two things, each where the game itself puts the information (and each shown
 *   only while the game is drawing the health bars). Each of the two has its own
 *   switch - "Combo counter" and "Health numbers" - so either can be drawn on
 *   its own:
 *     - The combo counter, in the middle of the screen, laid out the way the
 *       game's own orange combo popup reads: the count and the word on the first
 *       line ("8 COMBO"), the string's total on the second ("60 DAMAGE"), and
 *       the newest hits on a third ("12+9+7") so a juggle's scaling is visible
 *       while the count is what makes the string read as one combo. All three
 *       lines one colour. It pops up on the hit that makes a combo and goes away
 *       when the string stops, like the game's own popup.
 *     - The health number under each health bar: the exact life left, as a
 *       number. The "Health number" setting puts it at the bar's left end, its
 *       middle or its right end, so it can sit wherever the bars leave room.
 *
 * Where the numbers come from
 *   The fighter records TMST reads (its tournament source uses the same table):
 *     RECORD + 0x3f4   the fighter's life, 16.16 fixed point
 *     RECORD + 0x188c  the second fighter's record
 *   Nothing here writes to the game. Every read is a psx_mod_read_word and every
 *   pixel goes to the add-on's own overlay canvas (jarkins_osd.h), so a wrong
 *   readout is a readout to correct, never a changed fight.
 *
 * How a combo is counted, and when it ends
 *   There is no hit event to subscribe to. Life is sampled every vblank and a
 *   drop is one hit, worth exactly the drop; the hits since the string last
 *   ended are the combo. It ends when the life goes back up (a new round or a
 *   heal) or when nothing has landed for COMBO_LINGER frames - the same thing
 *   that makes the game's own popup disappear.
 *
 *   Each fighter's block is on his own side of the frame, mirrored about the
 *   middle the way the health bars are (see draw_combos). A string is counted
 *   from the life that was lost, so the record whose life dropped is the fighter
 *   who LANDED the string - record 0's combo goes on player two's side.
 *
 *   Each fighter's string is counted on its own: its own hits, its own total and
 *   its own idle count, so a hit on player one neither extends nor clears player
 *   two's string (and the other way round). Two fighters mid-string show two
 *   blocks; one fighter's string ending leaves the other's alone. The count only shows from
 *   `combo_min` hits up (two by default), which is what tells a poke apart from
 *   a combo. Because it counts life drops rather than move data it works for
 *   every character and every mode without a move table.
 *
 * When the readouts go away
 *   Two gates, both the game's own words:
 *     - the HUD routine has to have run within the last couple of frames, so a
 *       screen the bars are not drawn on (the results, the VS screen, the next
 *       match's loading) carries nothing of ours;
 *     - the round has to still be on. Both fighters' rounds-won byte (the one the
 *       game counts a round with) is read every frame: when it goes up, the round
 *       has just been decided - a KO or the time running out - and the readouts
 *       stop there, over the winner's pose, rather than hanging on the finished
 *       round until the next one starts. A life climbing back up, or both lives
 *       standing full, says the next round is on and the numbers come back.
 *
 *   Practice is left alone in both cases - see below.
 *
 * Where Practice is concerned
 *   The mode is not touched: no bars there to hang a health number under, and
 *   its own combo popup already covers it. The layout here copies that popup so
 *   the readout reads as part of the game, not because this runs in Practice.
 *
 * Not here yet
 *   Damage scaling as a percentage of a move's base, and per-move frame data,
 *   both need the frames-as-data work in the roadmap; they are not faked here.
 */
#include "mod_plugins.h"
#include "jarkins_osd.h"
#include "jarkins_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKAGE "jarkins.lab"
#define FEATURE "damage-readouts"

/* --- where things are ----------------------------------------------------- */

/* Two excluded places, by name: Practice, which is not touched, and the pause
 * menu, whose own frame calls the HUD routine too. Whether a match is being
 * played at all is the HUD routine's entry hook ("the game's HUD routine"), not
 * anything read from these words. */
enum {
    PAUSED        = 0x80095494,
    MODE          = 0x800afa88,
    MODE_PRACTICE = 5
};

/* The fighter records, as TMST reads them. ROUNDS_WON is the byte the game
 * itself counts a round with: it goes up the frame a round is decided, which is
 * what lets the readouts stop at the KO instead of running on into the winner's
 * pose. */
enum { RECORD = 0x800a9228, RECORD_STRIDE = 0x188c, LIFE = 0x3f4, LIFE_MAX = 0x3f8,
       ROUNDS_WON = 0x44 };

/* --- where the readouts sit -----------------------------------------------
 * The bars are the game's own, at the top of the frame: player one's from the
 * left edge inwards, player two's mirrored. The spans are measured off a match;
 * if a bar ever moves, they are the only numbers to change. */
enum {
    /* The bars' fill, measured off a match at 368 wide (the white frame sits half
     * a texel outside these): each is 115 texels long, starting 54 in from its
     * edge, and the two are exact mirrors about the middle of the frame. */
    BAR_P1_X0 = 54, BAR_P1_X1 = 169,      /* player one's bar, left to right */
    BAR_P2_X0 = 199, BAR_P2_X1 = 314,     /* player two's, the same span mirrored */
    HEALTH_Y  = 44,                       /* on the bar's own line: the bar band is rows 44-59 */
    COMBO_Y   = 164,                      /* the popup band's middle, measured off the game's */
    COMBO_X   = 13,                       /* the popup's left edge; player two's mirrors it */
    COMBO_LINE_H = 18,                    /* line pitch inside a combo block */
    MAX_HITS  = 8,                        /* room for a whole juggle */
    TEXT_SCALE = 1,
    /* The readouts are set in the game's own HUD font, drawn 1:1 here: ten
     * texels a character, sixteen tall, the same weight and pitch as the
     * the STAGE line and its timer over the bars, so the numbers read as part of
     * the HUD rather than as an overlay on top of it. */
    COMBO_SCALE = 1,
    EDGE_MARGIN = 8,                      /* bar left showing past an end-placed number */
    COMBO_LINGER = 60                     /* frames without a hit before the combo is over */
};

#define COLOR(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))
/* One colour for the whole combo block, the game's popup orange. Each piece of
 * the overlay keeps its own constant so a per-piece colour setting can take them
 * over one at a time. */
#define C_COMBO COLOR(0xff, 0xac, 0x00)   /* the combo block's default (the popup's orange) */
#define C_P1    COLOR(0x60, 0xa8, 0xff)   /* the first player's health number */
#define C_P2    COLOR(0xff, 0x70, 0x60)   /* the second's */
/* The shades themselves, and the edge rule that keeps dark ink readable, are the
 * overlay's own table (jarkins_osd_colour_of / jarkins_osd_edge_of): one list for
 * every feature in the pack that colours something. */

/* Where the health number sits along its bar (its orientation). */
enum { HEALTH_LEFT, HEALTH_MIDDLE, HEALTH_RIGHT };

/* --- what the player chose ------------------------------------------------ */

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

/* A switch: on unless it was turned off. */
static int option_flag(const char *id, int fallback) {
    char text[16] = "";
    if (!psx_mod_option_value(PACKAGE, FEATURE, id, text, sizeof text) || !text[0])
        return fallback;
    return strcmp(text, "false") != 0 && strcmp(text, "0") != 0;
}

/* A colour setting, by the name the manifest lists. */
static uint32_t option_colour(const char *id, uint32_t fallback) {
    char text[16] = "";
    if (!psx_mod_option_value(PACKAGE, FEATURE, id, text, sizeof text))
        return fallback;
    return jarkins_osd_colour_of(text, fallback);
}

/* "Health alignment": the bar's outer end, middle or inner end. */
static int option_health_orientation(void) {
    char text[16] = "";
    if (!psx_mod_option_value(PACKAGE, FEATURE, "health_pos", text, sizeof text))
        return HEALTH_MIDDLE;
    if (strcmp(text, "left") == 0) return HEALTH_LEFT;
    if (strcmp(text, "right") == 0) return HEALTH_RIGHT;
    return HEALTH_MIDDLE;
}

/* "Number style": where the colour goes on each glyph. */
static int option_glyph_fill(void) {
    char text[16] = "";
    if (!psx_mod_option_value(PACKAGE, FEATURE, "glyph_fill", text, sizeof text))
        return JARKINS_OSD_FILL_OUTER;
    if (strcmp(text, "whole") == 0) return JARKINS_OSD_FILL_WHOLE;
    if (strcmp(text, "inner") == 0) return JARKINS_OSD_FILL_INNER;
    return JARKINS_OSD_FILL_OUTER;
}

/* --- state ---------------------------------------------------------------- */

static int armed;
static int recent = 3;                 /* how many hits are listed under the count */
static int combo_min = 2;              /* hits before the popup shows */
static int health_pos = HEALTH_MIDDLE;     /* orientation along the bar */
static int show_combo = 1;             /* the combo counters are drawn */
static int show_health = 1;            /* the health numbers are drawn */
static uint32_t colour_combo = C_COMBO;
static uint32_t colour_p1 = C_P1;
static uint32_t colour_p2 = C_P2;
static int glyph_fill = JARKINS_OSD_FILL_OUTER;
static unsigned frame;

/* The round, and where it stands. Both fighters' rounds won is read every frame
 * the readouts are allowed to draw: when it goes up a round has just been
 * decided (the KO or the time out), and from that frame until a life climbs
 * again - the next round - nothing of ours is drawn. The counter is not the
 * life, so it says "the round is over" on a time out too, and life climbs back
 * only when the game starts the next one. */
static unsigned rounds_won;            /* both records' rounds won, as last seen */
static int have_rounds;
static int round_over;                 /* the round is decided: draw nothing */
static int life_rose;                  /* a life went back up this frame */

/* One fighter's string. */
typedef struct {
    int hits[MAX_HITS];                /* damage of each hit, oldest first */
    int n;
    int total;
    int idle;                          /* this fighter's frames since its last hit */
    uint32_t last_life;
    int have_life;
} damage_t;

static damage_t dmg[2];

static void forget(damage_t *d) {
    d->n = 0;
    d->total = 0;
    d->idle = 0;
}

static void forget_all(void) {
    forget(&dmg[0]);
    forget(&dmg[1]);
    dmg[0].have_life = 0;
    dmg[1].have_life = 0;
}

/* --- reading the game ----------------------------------------------------- */

/* A drop in this fighter's life is one hit; a rise means a new round. Answers
 * whether this frame landed a hit. */
static int sample(int player) {
    damage_t *d = &dmg[player];
    const uint32_t now =
        psx_mod_read_word(RECORD + (uint32_t)player * RECORD_STRIDE + LIFE);
    int landed = 0;

    if (!d->have_life) {
        d->last_life = now;
        d->have_life = 1;
        return 0;
    }
    if (now < d->last_life) {
        const int damage = (int)(((d->last_life - now) + 0x8000u) >> 16);
        if (damage > 0) {
            if (d->n == MAX_HITS) {     /* keep the newest */
                memmove(d->hits, d->hits + 1, sizeof d->hits[0] * (MAX_HITS - 1));
                d->n--;
            }
            d->hits[d->n++] = damage;
            d->total += damage;
            landed = 1;
            jarkins_log( "jarkins: hit P%u -%d (hits %d, total %d)\n",
                    (unsigned)player + 1, damage, d->n, d->total);
        }
    } else if (now > d->last_life) {
        forget(d);
        life_rose = 1;
    }
    d->last_life = now;
    return landed;
}

static int life_of(int player) {
    return (int)(psx_mod_read_word(RECORD + (uint32_t)player * RECORD_STRIDE + LIFE) >> 16);
}

/* --- drawing -------------------------------------------------------------- */

/* Where the game's own HUD appears. The bars are the game's, drawn into the
 * 4:3 frame; the runtime's native-wide presentation then stretches the game's
 * HUD out to fill the screen, so the bar the player looks at is the bar's own
 * span pushed through that stretch. A number placed against an end follows it,
 * or it sits inside the bar it belongs to. The reveal is zero at 4:3, where
 * this is the identity and the bars are where they are drawn. */
extern int32_t psx_mod_widescreen_x_margin(void) __attribute__((weak));

static int presented_x(int x) {
    const int margin = psx_mod_widescreen_x_margin ? psx_mod_widescreen_x_margin() : 0;
    if (margin <= 0)
        return x;
    return (x * (JARKINS_OSD_W + 2 * margin) + JARKINS_OSD_W / 2) / JARKINS_OSD_W - margin;
}

/* The health number, under the bar, at the end the setting asks for. */
static void draw_health(int player) {
    char text[16];
    snprintf(text, sizeof text, "%d", life_of(player));

    const int w = jarkins_osd_text_width(text, TEXT_SCALE);
    /* The bar as it appears, so an end-placed number lands on that end. */
    const int x0 = presented_x(player == 0 ? BAR_P1_X0 : BAR_P2_X0);
    const int x1 = presented_x(player == 0 ? BAR_P1_X1 : BAR_P2_X1);

    /* Player two's bar is player one's mirror image, so the setting mirrors with
     * it: "left" is the bar's left end for player one and its right end for
     * player two - the same edge of each bar as that player sees it.
     *
     * An end placement leaves the same EDGE_MARGIN of bar showing on both sides: at
     * the bar's start the first drawn column begins that far in (the string's own
     * blank lead - "1" starts a column in - is taken off), and at the bar's end
     * last drawn column stops that far short, so the bar shows the same margin
     * on that side too. */
    int lead = 0, trail = 0;
    jarkins_osd_text_ink_edges(text, TEXT_SCALE, &lead, &trail);

    const int at_end = health_pos != HEALTH_MIDDLE;
    const int at_left_end = (health_pos == HEALTH_LEFT) != (player == 1);
    int x = (x0 + x1) / 2 - w / 2;              /* middle by default: centred on the bar */
    if (at_end && at_left_end)
        x = x0 + EDGE_MARGIN - lead;
    else if (at_end)
        x = x1 - EDGE_MARGIN - w + trail;
    /* Whatever the setting asks for, the number stays on the bar, and on the
     * picture that reaches the screen: the bars run wider than the frame the
     * overlay is composited from, so an outer-end placement past that frame's
     * last column is pulled back onto it rather than lost. */
    if (x < x0)
        x = x0;
    else if (x > x1 - w)
        x = x1 - w;
    if (x + w > JARKINS_OSD_DRAW_W)
        x = JARKINS_OSD_DRAW_W - w;
    if (x < 0)
        x = 0;

    const int y = HEALTH_Y;
    const uint32_t ink = player == 0 ? colour_p1 : colour_p2;
    jarkins_osd_text_fill(x, y, text, ink, TEXT_SCALE, glyph_fill);
    /* Once per player per run: which number went where. */
    static int told[2];
    if (!told[player]) {
        told[player] = 1;
        jarkins_log( "jarkins: health P%u \"%s\" at x %d, y %d, %d wide (bar %d..%d as shown)\n",
                (unsigned)player + 1, text, x, y, w, x0, x1);
    }
}

/* "8 COMBO": the count and the word, the way the game's popup reads. */
static void count_line(int player, char *buf, size_t size) {
    const damage_t *d = &dmg[player];
    snprintf(buf, size, "%d COMBO", d->n);
}

/* "60 DAMAGE": the string's total on its own line, under the count. */
static void damage_line(int player, char *buf, size_t size) {
    const damage_t *d = &dmg[player];
    snprintf(buf, size, "%d DAMAGE", d->total);
}

/* The newest hits in the order they landed: "12+9+7". */
static void hits_line(int player, char *buf, size_t size) {
    const damage_t *d = &dmg[player];
    buf[0] = '\0';
    if (recent < 1)
        return;
    int len = 0;
    const int first = d->n > recent ? d->n - recent : 0;
    for (int i = first; i < d->n; i++)
        len += snprintf(buf + len, size - (size_t)len, "%s%d",
                        i == first ? "" : "+", d->hits[i]);
}

/* The side a string belongs to. A string is counted from the life a fighter
 * lost, so record 0 (player one's life) is the string player TWO landed: the
 * owner is always the other record. */
static int owner_of(int record) {
    return record == 0 ? 1 : 0;
}

/* One line of a block, in the block's colour. Player one's
 * block is set left, where the game's own popup sits; player two's is the mirror
 * image of it on his own side of the frame, so each fighter's string reads under
 * his own half of the screen. */
static void block_line(int y, const char *text, int owner) {
    if (!text[0])
        return;
    const int w = jarkins_osd_text_width(text, COMBO_SCALE);
    /* Player one's block is set at the frame's own left margin and player two's
     * the same distance in from the frame's right column - the widest column
     * the picture is shown at, not the canvas' - so the last line of a long
     * string is drawn rather than clipped. */
    const int x = owner == 0 ? COMBO_X : (JARKINS_OSD_DRAW_W - COMBO_X) - w;
    jarkins_osd_text_fill(x, y, text, colour_combo, COMBO_SCALE, glyph_fill);
}

/* Each fighter's counter on his own side of the frame, mirrored about the middle
 * like the health bars: player one's at the popup's left edge, player two's the
 * same distance in from the right edge. Each block is three lines - "8 COMBO",
 * "60 DAMAGE", then the newest hits - in one orange: text over
 * the fight, no panel. Both blocks sit in the popup band, centred on COMBO_Y, so
 * they appear at the same height on opposite sides instead of stacking. */
static void draw_combos(void) {
    const int block_h = 3 * COMBO_LINE_H;
    const int y = COMBO_Y - block_h / 2;
    for (int p = 0; p < 2; p++) {
        if (dmg[p].n < combo_min || dmg[p].n < 2)
            continue;

        char count[40], total[40], hits[64];
        count_line(p, count, sizeof count);
        damage_line(p, total, sizeof total);
        hits_line(p, hits, sizeof hits);

        const int owner = owner_of(p);
        block_line(y, count, owner);
        block_line(y + COMBO_LINE_H, total, owner);
        block_line(y + 2 * COMBO_LINE_H, hits, owner);
    }
}

/* --- the game's HUD routine ----------------------------------------------- */

/* The health bars are drawn by the game's HUD routine at 0x8003D6B0, so its
 * entry is the one honest answer to "is a match on screen": whenever the
 * routine runs, the bars are being drawn, and when it does not - the K.O., the
 * replay, the next match's VS screen - they are not. The runtime only emits the
 * call into generated code for functions listed in game.toml's [recompiler]
 * mod_function_entry_funcs, which is where this address is named.
 *
 * Two frames of slack: our vblank and the HUD's own drawing do not belong to the
 * same frame boundary, so the routine may have run just before or just after the
 * last vblank we looked at. */
enum { HUD_ROUTINE = 0x8003D6B0, HUD_GRACE = 2 };

/* Written by the entry hook below, read by the vblank gate. */
static unsigned hud_frame;             /* the frame the HUD routine last ran on */
static int hud_seen;                   /* it has run at least once */

static void hud_entered(struct CPUState *cpu, uint32_t address) {
    (void)cpu;
    (void)address;
    hud_frame = frame;
    hud_seen = 1;
}

/* --- plugins -------------------------------------------------------------- */

static void damage_activate(void) {
    (void)jarkins_video_prepare();
    recent = option_int("recent", 3, 1, MAX_HITS);
    combo_min = option_int("combo_min", 2, 1, 10);
    health_pos = option_health_orientation();
    show_combo = option_flag("show_combo", 1);
    show_health = option_flag("show_health", 1);
    colour_combo = option_colour("combo_color", C_COMBO);
    colour_p1 = option_colour("health_p1_color", C_P1);
    colour_p2 = option_colour("health_p2_color", C_P2);
    glyph_fill = option_glyph_fill();
    forget_all();
    armed = 1;
    jarkins_log( "jarkins: damage readouts armed (health numbers %s at %s; "
                    "combo counter %s from %d hits, newest %d listed; "
                    "matches only, Practice untouched)\n",
            show_health ? "on" : "off",
            health_pos == HEALTH_LEFT ? "Outer end" : health_pos == HEALTH_RIGHT ? "Inner end" : "Middle",
            show_combo ? "on" : "off", combo_min, recent);
    jarkins_log( "jarkins: number style %s\n",
            glyph_fill == JARKINS_OSD_FILL_WHOLE ? "whole glyph"
          : glyph_fill == JARKINS_OSD_FILL_INNER ? "inner (colour inside, white rim)"
          : "outer (colour on the rim, white inside)");
}

/* The pack's table of which button picked each fighter; absent on a stock copy,
 * where nothing but the game's own choice exists. Read for the log only. */
extern int tekken3_native_pick_key(unsigned slot, unsigned id) __attribute__((weak));
/* ... and the model variant the pack picks for that fighter, so the log shows
 * whether a key actually reached a costume (7 is Xiaoyu's secret P8). */
extern int tekken3_native_model_variant(unsigned slot, unsigned id, unsigned costume)
    __attribute__((weak));

static void damage_vblank(void) {
    if (!armed)
        return;

    /* This feature's own count of vblanks, one a frame: it is what tells a new
     * frame from the last one, and what the HUD grace below is measured in. */
    frame++;

    /* Share the canvas with whatever else is on: the first overlay plugin in a
     * frame presents the last one's picture and clears it for this one. */
    jarkins_osd_frame(frame);

    /* Draw while the game is drawing its HUD - the health bars - and not
     * otherwise. Practice is left alone, and the pause menu (which calls the HUD
     * routine too, for the band and bars its own frame redraws) is not ours. */
    if (!hud_seen || frame - hud_frame > HUD_GRACE ||
        psx_mod_read_half(MODE) == MODE_PRACTICE ||
        psx_mod_read_half(PAUSED) != 0) {
        forget_all();
        /* Out of the fight: the next one starts its own round count. A round
         * decided before the bars left is how most rounds end up here - the
         * transition screen stops the HUD - so say so, or a round that ends
         * looks like readouts that never came back. */
        if (round_over)
            jarkins_log( "jarkins: bars gone after the round - readouts re-armed\n");
        have_rounds = 0;
        round_over = 0;
        return;
    }

    /* A round decided? The counter says so the frame it happens, and the bars
     * are still on screen for the winner's pose - so the numbers have to be
     * switched off here rather than waiting for the HUD to stop. */
    const unsigned won = psx_mod_read_byte(RECORD + ROUNDS_WON) +
                         psx_mod_read_byte(RECORD + RECORD_STRIDE + ROUNDS_WON);
    if (!have_rounds) {
        rounds_won = won;
        have_rounds = 1;
        round_over = 0;
    } else if (won != rounds_won) {
        rounds_won = won;
        round_over = 1;
        forget_all();
        jarkins_log( "jarkins: round decided (%u won) - readouts off until the next round\n",
                won);
    }

    /* Each fighter's string runs on its own clock: a hit resets only that
     * fighter's idle count, and only that fighter's block goes away when its own
     * string runs out. Sampling goes on with the round over, because a life
     * climbing back up is what says the next round has started. */
    life_rose = 0;
    for (int p = 0; p < 2; p++) {
        if (sample(p))
            dmg[p].idle = 0;
        else if (++dmg[p].idle > COMBO_LINGER)
            forget(&dmg[p]);           /* this string is over: its block goes away */
    }

    if (round_over) {
        /* The next round has started when a life climbs back up, or when both
         * stand at full - the same two signs TMST uses, and the full one alone
         * covers a counter that moves as the round refills. */
        const uint32_t max1 = psx_mod_read_word(RECORD + LIFE_MAX);
        const uint32_t max2 = psx_mod_read_word(RECORD + RECORD_STRIDE + LIFE_MAX);
        const int both_full = max1 && max2 &&
            psx_mod_read_word(RECORD + LIFE) == max1 &&
            psx_mod_read_word(RECORD + RECORD_STRIDE + LIFE) == max2;
        if (!life_rose && !both_full)
            return;                    /* the round is over: nothing on screen */
        round_over = 0;                /* a new round: the numbers come back */
        forget_all();
        jarkins_log( "jarkins: next round - readouts back on\n");
    }

    /* Once per match: who is fighting and which button picked them - the number
     * the name plates colour themselves by, and the one R1 has to write. */
    {
        static int told;
        if (!told) {
            told = 1;
            const unsigned id0 = psx_mod_read_half(0x800add5c);
            const unsigned id1 = psx_mod_read_half(0x800add5e);
            const unsigned c0 = psx_mod_read_half(0x800add98) & 3;
            const unsigned c1 = psx_mod_read_half(0x800add9a) & 3;
            jarkins_log("jarkins: fighters %u/%u, pick keys %d/%d, costumes %u/%u, "
                        "model variants %d/%d\n",
                        id0, id1,
                        tekken3_native_pick_key ? tekken3_native_pick_key(0, id0) : -2,
                        tekken3_native_pick_key ? tekken3_native_pick_key(1, id1) : -2,
                        c0, c1,
                        tekken3_native_model_variant ? tekken3_native_model_variant(0, id0, c0) : -2,
                        tekken3_native_model_variant ? tekken3_native_model_variant(1, id1, c1) : -2);
        }
    }

    if (show_health) {
        draw_health(0);
        draw_health(1);
    }
    if (show_combo)
        draw_combos();
}

PSX_MOD_CONSTRUCTOR(jarkins_register_damage) {
    (void)psx_mod_register_activation_plugin("jarkins.damage", damage_activate);
    (void)psx_mod_register_vblank_plugin("jarkins.damage", damage_vblank);
    /* The HUD routine's entry, for "the bars are being drawn right now". */
    (void)psx_mod_register_function_entry_plugin("jarkins.damage", HUD_ROUTINE, hud_entered);
}
