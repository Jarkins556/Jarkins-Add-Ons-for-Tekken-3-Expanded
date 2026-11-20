/* Jarkins Lab: getting the overlay's picture onto the screen (jarkins_video.h).
 *
 * The add-on may not change the runtime, so the picture is drawn the way a
 * mod's picture is drawn: into the upper VRAM half, by a GPU packet of the
 * add-on's own, spliced into the frame's display list. It is the recipe TMST's
 * overlay uses (addons/tmst/src/core/tmst_overlay.c); both run on a stock
 * release, and nothing of the game's 1024x512 texture space is touched.
 *
 * What this file is careful about, each of which broke the game before:
 *
 *   - It never emits GP0(E1). E1 is the GPU's *drawing mode*, not a property
 *     of one primitive: bit 11 there is the mask bit (set on every pixel drawn
 *     afterwards) and bit 10 is "may draw to the display area". A packet that
 *     set those left the game's own primitives stamping the mask bit into its
 *     textures, which is what "the textures completely break in a match"
 *     looked like. The texture page goes in the quad's own word instead.
 *   - It uploads only the ink, into a place the pack itself reports free
 *     (its stage, hit-effect, wolf and PS2-weapon reservations), never a blind
 *     block over the half.
 *   - It never writes the chain into a list that already contains it: Practice
 *     and its menus do not rebuild every list each frame, and splicing twice
 *     pointed the chain's tail at its own head, so the DMA walk never ended.
 *   - It retracts the chain when there is nothing to show (retract below): a
 *     list that is walked again still draws the chain it was given, so the
 *     frame that stops showing has to end that chain itself.
 *   - It lays the whole picture as ONE ordering-table node (build_chain), the
 *     way TMST's overlay lays its picture (tmst_overlay.c: prims_begin, then
 *     every quad, then prims_finish). A node per tile - which is what a chain
 *     of them was - lost every quad after the first in practice.
 *   - It keeps every texel it draws inside ONE texture page (find_pieces).
 *     The half's free run is a single 256-texel page wide: the page this
 *     overlay's text has always been drawn from. The page alongside it is not
 *     ours - another add-on's overlay keeps its font sheet and atlas there and
 *     uploads them after this one does, so texels placed in it are overwritten
 *     before the frame is drawn and never appear. The readouts are spread over
 *     more than a page across the screen, but most of that is the empty middle:
 *     each side is a piece of its own, and the pieces are packed side by side
 *     into the one page. A wide picture is therefore still one page of texels,
 *     one quad per piece, all in the one node.
 */
#include "jarkins_video.h"
#include "jarkins_osd.h"
#include "mod_plugins.h"
#include "gpu.h"
#include <stdarg.h>
#include <stdio.h>

/* A 15-bit texture page holds 256x256 texels and a packet's U/V are 8 bits, so
 * no piece may be wider than one page, and a whole screen's worth still fits in
 * one block of packet memory (16 quads x 9 words, under one tag). */
enum { TILE = 256 };
/* The upper half starts here: 512 lines, as on Namco System 12. */
enum { HALF_Y = 512 };
/* The chain is one node: its tag, then nine words a quad, and the two pairs of
 * drawing-area words (E3/E4) that widen the clip for those quads and give the
 * game's own back after them. */
enum { MAX_TILES = 16, AREA_WORDS = 4,
       NODE_WORDS = 1 + AREA_WORDS + MAX_TILES * 9, BUF_BYTES = NODE_WORDS * 4 };
/* Empty picture columns up to this wide stay inside one piece (letters of one
 * readout are two columns apart); the gap between player one's readouts and
 * player two's is over a hundred, so the two sides are two pieces. */
enum { PIECE_GAP = 16, MAX_PIECES = 8 };

static uint32_t packet;                 /* our chain's first order-table node */
static int prepared;
static int usable;                      /* the half and the packet memory exist */

static const uint32_t *picture;         /* the canvas being shown */
static int pic_w = 368, pic_h = 480;
static int want;                        /* there is something to show */
static int linked;                      /* our chain is in this frame's list */

static int box_x, box_y, box_w, box_h;  /* the ink box, in picture coordinates */
static int home_x = -1, home_y;         /* where that ink lives in the half */
static int home_w, home_h;
static int need_w, need_h;              /* what this frame's ink needs of the half */

/* The pieces of the picture: the ink box cut at the empty columns between
 * readouts, each a rectangle of the picture that is uploaded and drawn on its
 * own. Side by side in the order they appear, so player one's comes first. */
typedef struct {
    int x, y, w, h;
} piece_t;
static piece_t pieces[MAX_PIECES];
static int piece_count;
static int piece_hx[MAX_PIECES];        /* where each piece's texels start, from the home */

/* --- the drawing area the game draws with ----------------------------------
 *
 * E3 (top-left) and E4 (bottom-right): the clip rectangle every primitive is
 * drawn inside, x in bits 0-9 and y from bit 10. It is the game's own screen,
 * and the overlay's ink runs past it - the readouts reach the widescreen HUD's
 * ends, which the 4:3 screen does not. A primitive outside the area is simply
 * not drawn, so the area's right edge is widened for this add-on's own quads
 * and the game's own words are written straight back after them: the clip is
 * the game's again before the next list is walked, and nothing of the game's is
 * drawn through our change.
 *
 * The values are read out of the frame's own lists, the way TMST's overlay does
 * it (tmst_overlay.c: command_words, follow, gpu_e3/gpu_e4), because the game
 * changes them as the screen it is drawing changes - menus, the round, the
 * results. Until one is heard, the whole screen is assumed, which is what
 * TMST's overlay assumes too. */
static uint32_t area_e3 = 0xE3000000u;
static uint32_t area_e4 = 0xE4000000u | 479u << 10 | 367u;

static uint16_t row_texels[TILE + 1];   /* one row of a piece, uploaded at a time */
static uint32_t at;                     /* where the next packet word goes */
static int quads;                       /* the quads laid in this frame's node */

/* --- the add-on's log ----------------------------------------------------- */

void jarkins_log(const char *fmt, ...) {
    static FILE *f;
    static int opened;
    if (!opened) {
        opened = 1;
        f = fopen("jarkins.log", "w");
    }
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    if (f) {
        vfprintf(f, fmt, ap);
        fflush(f);
    }
    vfprintf(stderr, fmt, ap2);
    va_end(ap2);
    va_end(ap);
}

/* --- what the pack has already claimed in the half ------------------------ */

/* Each answers 1 when the part of the half it uses covers the rectangle. They
 * are the pack's own reservations (src/tekken3_*_high_vram_used), absent on a
 * stock Expanded - where nothing else uses the half at all - so every test is
 * guarded by the pointer. */
extern int tekken3_stage_high_vram_used(int x, int y, int w, int h) __attribute__((weak));
extern int tekken3_hit_effect_high_vram_used(int x, int y, int w, int h) __attribute__((weak));
extern int tekken3_ttt1_wolf_high_vram_used(int x, int y, int w, int h) __attribute__((weak));
extern int tekken3_ps2_laser_high_vram_used(int x, int y, int w, int h) __attribute__((weak));
extern int tekken3_ps2_weapons_high_vram_used(int x, int y, int w, int h) __attribute__((weak));

/* One probe, in the shape the pack itself asks in: its own callers query a
 * 16-wide, 64-tall column (tekken3_ttt1_ps2_laser.c, tekken3_ttt1_combat.c), so
 * these functions are not asked about a whole screen-sized rectangle at once.
 * Asking for a big one was what made the game fault the moment a combo first
 * appeared - the frame the ink box grew and the search ran. */
enum { PROBE_W = 16, PROBE_H = 64 };
static int cell_used(int x, int y) {
    if (tekken3_stage_high_vram_used && tekken3_stage_high_vram_used(x, y, PROBE_W, PROBE_H)) return 1;
    if (tekken3_hit_effect_high_vram_used && tekken3_hit_effect_high_vram_used(x, y, PROBE_W, PROBE_H)) return 1;
    if (tekken3_ttt1_wolf_high_vram_used && tekken3_ttt1_wolf_high_vram_used(x, y, PROBE_W, PROBE_H)) return 1;
    if (tekken3_ps2_laser_high_vram_used && tekken3_ps2_laser_high_vram_used(x, y, PROBE_W, PROBE_H)) return 1;
    if (tekken3_ps2_weapons_high_vram_used && tekken3_ps2_weapons_high_vram_used(x, y, PROBE_W, PROBE_H)) return 1;
    return 0;
}

/* Past the half's own bounds? */
static int beyond(int x, int y, int w, int h) {
    return x < 0 || y < HALF_Y || x + w > 1024 || y + h > 1024;
}

/* Is any of the rectangle claimed? Walked as a grid of 16x64 cells, so no single
 * call carries more than the pack expects (see cell_used). */
static int taken(int x, int y, int w, int h) {
    if (beyond(x, y, w, h)) return 1;
    for (int cy = y; cy < y + h; cy += PROBE_H)
        for (int cx = x; cx < x + w; cx += PROBE_W)
            if (cell_used(cx, cy)) return 1;
    return 0;
}

/* A home for the frame's ink: the lowest free place, searched upwards from
 * y 512, so the overlay sits below the pictures TMST's overlay places (it
 * searches from the top of the half downwards). One is kept for as long as it
 * stays free; the stage changing under it moves it. 0 when the half has no room
 * - the picture is then not shown at all, rather than written over something of
 * the game's. */
static int place(void) {
    /* The home only ever grows. The ink box changes size as the picture does
     * (one entry in the strip, then two; a combo line appearing), and a home
     * that moved with it would re-upload the picture into a different place
     * every few frames - stale texels left behind and a picture that shivers.
     * A slot big enough for the largest box seen is kept until the stage claims
     * it, and smaller boxes reuse it where they are. */
    if (home_x >= 0 && home_w >= need_w && home_h >= need_h &&
        !taken(home_x, home_y, home_w, home_h))
        return 1;
    const int near_w = need_w > home_w ? need_w : home_w;
    const int near_h = need_h > home_h ? need_h : home_h;
    /* Small steps first, so a home that only just stopped fitting is moved a
     * little rather than being thrown across the half. */
    for (int y = HALF_Y; y + near_h <= 1024; y += 8) {
        for (int x = 0; x + near_w <= 1024; x += 64) {
            if (taken(x, y, near_w, near_h)) continue;
            home_x = x; home_y = y; home_w = near_w; home_h = near_h;
            return 1;
        }
    }
    home_x = -1;
    home_w = home_h = 0;
    return 0;
}

/* --- start-up -------------------------------------------------------------- */

int jarkins_video_prepare(void) {
    if (prepared) return usable;
    prepared = 1;
    /* The half exists only if a plugin asks before the renderer starts
     * sampling it, which is what an activation callback is for. */
    if (!psx_mod_gpu_high_vram_enable()) {
        jarkins_log( "jarkins: no upper VRAM half - overlays off\n");
        return 0;
    }
    /* ... and only the renderers that can sample it show anything: the
     * Vulkan/Android path cannot, so say so rather than working invisibly. */
    if (!psx_mod_gpu_high_vram_ready()) {
        jarkins_log( "jarkins: this renderer cannot sample the upper half - "
                        "overlays off\n");
        return 0;
    }
    packet = psx_mod_alloc_gpu_dma_memory(BUF_BYTES, 16);
    if (!packet || !psx_mod_gpu_high_vram_source(packet, BUF_BYTES)) {
        jarkins_log( "jarkins: no packet memory in the GPU arena - overlays off\n");
        return 0;
    }
    usable = 1;
    jarkins_log( "jarkins: overlay ready\n");
    return 1;
}

/* An ARGB pixel as the GPU's 15-bit texel: 0x0000 reads as see-through (that
 * is how the canvas' empty pixels stay empty), bit 15 makes it half
 * see-through, the rest is BGR555. Black cannot be 0x0000 - it would be a hole
 * - so it takes the darkest blue there is, as TMST's overlay does. */
static uint16_t texel_of(uint32_t argb) {
    const unsigned a = argb >> 24;
    if (a < 128) return 0;
    const unsigned r = (argb >> 19) & 31u;
    const unsigned g = (argb >> 11) & 31u;
    const unsigned b = (argb >> 3) & 31u;
    const uint16_t c = (uint16_t)(b << 10 | g << 5 | r);
    if (a < 255) return (uint16_t)(0x8000u | c);
    return c ? c : 0x0421u;
}

static uint32_t xy(int x, int y) {
    return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF);
}

static void word(uint32_t w) {
    psx_mod_write_word(at, w);
    at += 4;
}

/* --- the pieces of the picture -------------------------------------------- */

/* Does this picture column hold any ink? The pieces are cut where there is
 * none, so a readout is never split across two pieces (and so never across two
 * texture pages). */
static int ink_in_column(int x) {
    for (int y = box_y; y < box_y + box_h; y++)
        if ((picture[(size_t)y * (size_t)pic_w + (size_t)x] >> 24) >= 128)
            return 1;
    return 0;
}

/* One piece of ink, cut into page-wide chunks: a rectangle of the picture that
 * is uploaded on its own and drawn by its own quad. It keeps the box's height,
 * so all the pieces of the frame are the same band of the half. */
static void add_piece(int x0, int x1) {
    for (int a = x0; a <= x1 && piece_count < MAX_PIECES; a += TILE) {
        int w = x1 - a + 1;
        if (w > TILE) w = TILE;
        pieces[piece_count].x = a;
        pieces[piece_count].y = box_y;
        pieces[piece_count].w = w;
        pieces[piece_count].h = box_h > TILE ? TILE : box_h;
        piece_count++;
    }
}

/* The picture's ink, cut at every empty stretch wider than PIECE_GAP. The
 * readouts sit on opposite sides of the screen with a wide empty middle, so
 * this normally finds two pieces - one per player - each of them well inside a
 * page, which is what lets the layout keep its full width while every texel it
 * draws stays in the one page the half gives us. */
static void find_pieces(void) {
    piece_count = 0;
    const int end = box_x + box_w;
    int run = -1, last_ink = -1;
    for (int x = box_x; x < end; x++) {
        if (ink_in_column(x)) {
            if (run < 0) run = x;
            last_ink = x;
        } else if (run >= 0 && x - last_ink > PIECE_GAP) {
            add_piece(run, last_ink);
            run = -1;
        }
    }
    if (run >= 0)
        add_piece(run, last_ink);
    /* Nothing at all on the canvas: there is then nothing to draw, but the box
     * the canvas reported says something was; fall back to the whole box, so a
     * blank frame still ends in a picture rather than a lost chain. */
    if (piece_count == 0)
        add_piece(box_x, box_x + box_w - 1);
}

/* Uploads one piece into its place in the half and lays the quad that draws it:
 * one textured quad, over the same rectangle in the game's 368x480 coordinates.
 * The quad's words go straight after the chain's tag, the way TMST's overlay
 * adds its quads to one node. */
static void add_tile(const piece_t *p, int hx) {
    const int px = p->x, py = p->y, tw = p->w, th = p->h;

    for (int r = 0; r < th; r++) {
        const uint32_t *src = picture + (size_t)(py + r) * (size_t)pic_w + (size_t)px;
        for (int i = 0; i < tw; i++) row_texels[i] = texel_of(src[i]);
        if (tw & 1) row_texels[tw] = row_texels[tw - 1];   /* even, as the GPU likes */
        (void)psx_mod_gpu_high_vram_upload(hx, home_y + r, tw + (tw & 1), 1, row_texels);
    }

    /* The piece's page: x in 64-texel blocks, y in 256-line rows above 512, and
     * bit 11 - the System 12 bit that means "the upper half" for a primitive
     * drawn from this add-on's own packet memory. It rides in the quad's fourth
     * word (bits 16-31), which is where a polygon's texture page belongs; the
     * GPU's drawing mode is left exactly as the game set it. */
    const uint32_t page = (uint32_t)((hx / 64) & 15) |
                          (uint32_t)(((home_y - HALF_Y) / 256) & 1) << 4 |
                          2u << 7 /* 15-bit texels */ |
                          1u << 11;
    const uint32_t v0 = (uint32_t)((home_y - HALF_Y) % 256);
    const uint32_t u1 = (uint32_t)tw > 255u ? 255u : (uint32_t)tw;
    uint32_t v1 = v0 + (uint32_t)th;
    if (v1 > 255u) v1 = 255u;

    /* Textured quad at 1:1, raw texels, the half see-through ones blended. */
    word(0x2E808080u);
    word(xy(px, py));            word(v0 << 8);
    word(xy(px + tw, py));       word(u1 | v0 << 8 | page << 16);
    word(xy(px, py + th));       word(v1 << 8);
    word(xy(px + tw, py + th));  word(u1 | v1 << 8);
    quads++;
}

/* Packs the pieces side by side from the home's left edge, each starting on a
 * page boundary: a quad reads its texels from its page's base plus U, so a
 * piece whose texels did not start there would be drawn from whatever column
 * its page begins at. Answers how much of the half the packed frame needs, from
 * the home. The pieces are narrow (a readout each), so a two-sided layout still
 * fits in one page's worth of columns - 223 of them, home at 640. */
static int pack_pieces(void) {
    int offset = 0;
    for (int i = 0; i < piece_count; i++) {
        offset = (offset + 63) & ~63;
        piece_hx[i] = offset;
        offset += pieces[i].w;
    }
    return offset;
}

/* The whole chain, rebuilt each frame the picture is shown: one quad per piece,
 * all of them in the one node the tag at `packet` opens. */
static void build_chain(void) {
    at = packet + 4;                 /* the tag is written once the words are counted */
    quads = 0;
    /* The clip widened to the picture's right edge for our quads: the game's own
     * area for everything else (x0 and both y values are the game's; only x1 is
     * ours). */
    word(area_e3);
    word((area_e4 & ~0x3FFu) | (uint32_t)(JARKINS_OSD_DRAW_W - 1));
    for (int i = 0; i < piece_count && quads < MAX_TILES; i++)
        add_tile(&pieces[i], home_x + piece_hx[i]);
    /* ... and the game's own area back, so whatever is walked next - the frame's
     * other lists, the next frame's - is clipped exactly as the game asked. */
    word(area_e3);
    word(area_e4);
    /* The tag's top byte is the number of words that follow it (the tag itself
     * is not counted - the DMA reads it that way), the rest is the list's own
     * terminator: nothing is chained after this picture. */
    psx_mod_write_word(packet, ((uint32_t)(quads * 9 + AREA_WORDS)) << 24 | 0x00FFFFFFu);
}

/* Takes the picture away.
 *
 * Not splicing is not enough. Some screens do not rebuild every list each
 * frame (Practice, the pause menu, the round's own transition - which is what
 * the "strip still in this list" line in the log says), so a chain spliced into
 * one of them is walked again and again, still drawing the picture of the frame
 * it was last laid for. That is how the round's health numbers stayed on screen
 * over the winner's pose and on past the results, and how the bar under them
 * turned into whatever the texels beneath it held once the box or its place in
 * the half changed under a frame that was no longer rebuilt.
 *
 * So the frame that stops showing ends the chain itself: a tag with no words
 * after it and the list's own terminator as its next, written over the chain's
 * head. Whatever still points at us now walks into that and draws nothing, and
 * a picture that comes back is laid over the same head again (or found already
 * in a list, as the splice below finds it). */
static void retract(void) {
    if (!usable)
        return;
    psx_mod_write_word(packet, 0x00FFFFFFu);   /* no GP0 words; the walk ends */
}

void jarkins_video_show(const uint32_t *argb, int w, int h,
                        int x0, int y0, int x1, int y1) {
    linked = 0;
    picture = argb;
    if (w > 0) pic_w = w;
    if (h > 0) pic_h = h;
    want = 0;
    /* NULL is the canvas' "nothing was drawn this frame": nothing to show, so
     * the chain is taken out of the picture rather than left to draw on. */
    if (!argb) {
        retract();
        return;
    }
    if (!usable) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > pic_w - 1) x1 = pic_w - 1;
    if (y1 > pic_h - 1) y1 = pic_h - 1;
    if (x1 < x0 || y1 < y0) {
        retract();
        return;
    }
    box_x = x0; box_y = y0; box_w = x1 - x0 + 1; box_h = y1 - y0 + 1;
    find_pieces();
    /* What the half has to hold: the pieces side by side, in one band as tall as
     * the box. The empty middle of the screen is not uploaded at all, which is
     * what keeps a full-width layout inside one page. */
    need_w = pack_pieces();
    need_h = box_h > TILE ? TILE : box_h;
    if (!place()) {
        static int told;
        if (!told) {
            told = 1;
            jarkins_log( "jarkins: no free place in the half for %d piece(s), "
                            "%dx%d - nothing drawn\n", piece_count, need_w, need_h);
        }
        retract();
        return;
    }
    want = 1;
    build_chain();
    /* What the overlay was given and where it put it: once, then again only when
     * the picture changes shape - the pieces, and the width they are packed
     * into, are what say whether something went missing or moved. */
    static int told_q = -1, told_w = -1, told_pc = -1;
    if (quads != told_q || need_w != told_w || piece_count != told_pc) {
        told_q = quads; told_w = need_w; told_pc = piece_count;
        jarkins_log( "jarkins: canvas box x %d..%d y %d..%d in %d piece(s) %d wide, "
                        "placed at %d,%d (page %d) in %d quad(s), drawn x %d..%d\n",
                box_x, box_x + box_w - 1, box_y, box_y + box_h - 1,
                piece_count, need_w, home_x, home_y, (home_x / 64) & 15, quads,
                (int)(area_e3 & 0x3FFu), (int)(area_e4 & 0x3FFu));
    }
    /* The area the game's own primitives are clipped to, so a readout that still
     * stops short can be told apart from one the clip cut. */
    static int told_area = -1;
    if ((int)(area_e4 & 0x3FFu) != told_area) {
        told_area = (int)(area_e4 & 0x3FFu);
        jarkins_log( "jarkins: the game draws inside x %d..%d - the overlay widens it to %d\n",
                (int)(area_e3 & 0x3FFu), (int)(area_e4 & 0x3FFu), JARKINS_OSD_DRAW_W - 1);
    }
}

/* --- what the game clips itself to ---------------------------------------- */

/* How many words the GP0 command at `i` takes, read the way the GPU reads it:
 * the same walk TMST's overlay does over the frame's own lists
 * (tmst_overlay.c: command_words), so the two add-ons agree on where a command
 * ends - a polyline runs to its terminator, an image transfer to its size. */
static int command_words(uint32_t node, unsigned i, unsigned n) {
    const uint32_t w = psx_mod_read_word(node + 4 + i * 4);
    const unsigned op = w >> 24;
    if (op >= 0x20 && op < 0x40) {                      /* polygons */
        const unsigned verts = (op & 8) ? 4u : 3u, tex = (op & 4) ? 1u : 0u;
        const unsigned shaded = (op & 0x10) ? 1u : 0u;
        return (int)(1u + verts * (1u + tex) + shaded * (verts - 1u));
    }
    if (op >= 0x40 && op < 0x60) {                      /* lines */
        const unsigned shaded = (op & 0x10) ? 1u : 0u;
        if (!(op & 8)) return 3 + (int)shaded;
        unsigned k = i + 1;                             /* polyline: to its terminator */
        while (k < n && (psx_mod_read_word(node + 4 + k * 4) & 0xF000F000u) != 0x50005000u) k++;
        return (int)(k - i + 1);
    }
    if (op >= 0x60 && op < 0x80)                        /* rectangles */
        return 2 + ((op & 4) ? 1 : 0) + (((op & 0x18) == 0) ? 1 : 0);
    if (op >= 0x80 && op < 0xA0) return 4;              /* VRAM to VRAM copy */
    if (op >= 0xA0 && op < 0xC0) {                      /* CPU to VRAM */
        const uint32_t size = i + 2 < n ? psx_mod_read_word(node + 4 + (i + 2) * 4) : 0;
        return 3 + (int)((((size & 0xFFFFu) * (size >> 16)) + 1u) / 2u);
    }
    if (op >= 0xC0 && op < 0xE0) return 3;              /* CPU to VRAM, V0 */
    if (op == 0x02) return 3;                           /* fill */
    return 1;                                           /* the E commands, NOP */
}

/* Reads the drawing area out of one node of the game's own list: every E3/E4 it
 * sets is kept, so the last one in the list - the area the frame is drawn
 * inside - is what our quads widen and give back. */
static void note_area(uint32_t node) {
    const unsigned n = psx_mod_read_word(node) >> 24;
    for (unsigned i = 0; i < n;) {
        const uint32_t w = psx_mod_read_word(node + 4 + i * 4);
        if ((w >> 24) == 0xE3u) area_e3 = w;
        else if ((w >> 24) == 0xE4u) area_e4 = w;
        const int len = command_words(node, i, n);
        if (len <= 0) break;
        i += (unsigned)len;
    }
}

/* The runtime is about to walk this frame's ordering table. Its main list is
 * the long one (the set-up lists the GPU is given are short); our chain goes at
 * the end of it, so it is drawn with, and over, the game's own primitives. */
/* Two add-ons cannot both define __wrap_gpu_ws_prepass_linked_list, and TMST's
 * overlay wraps the same routine: with both folders in, the link failed on the
 * duplicate and the Expanded dropped both add-ons. When TMST is in the build,
 * addon.cmake has its wrapper compiled as jarkins_tmst_prepass and it is called
 * from here instead - TMST splices its own chain and walks the list - so both
 * overlays draw. JARKINS_CHAINS_TMST is set only when that file is there; on its
 * own the add-on calls the runtime's routine, and TMST is not needed. */
#ifdef JARKINS_CHAINS_TMST
extern void jarkins_tmst_prepass(uint32_t start_addr);
#else
extern void __real_gpu_ws_prepass_linked_list(uint32_t start_addr);
#endif
void __wrap_gpu_ws_prepass_linked_list(uint32_t start_addr) {
    if (want && usable && !linked) {
        uint32_t walk = start_addr & 0x00FFFFFFu, last = 0, node = 0;
        const uint32_t mine = packet & 0x00FFFFFFu;
        int n = 0, already = 0;
        while (walk != 0x00FFFFFFu && n < 8192) {
            /* Practice and its menus do not rebuild every list each frame, so
             * the chain spliced into one last frame can still be there. Splicing
             * it in again at the new tail would point the chain's last node at
             * its own first: the DMA walk then never ends, and the game either
             * drops frames until the watchdog fires or lands on its error path
             * (which halts at PC 0). If it is already in this list, leave it. */
            if (walk == mine) { already = 1; break; }
            node = 0x80000000u | walk;
            /* What the game clips itself to, before our chain is walked: our
             * quads widen it and give it back (note_area, build_chain). */
            note_area(node);
            last = walk;
            walk = psx_mod_read_word(node) & 0x00FFFFFFu;
            n++;
        }
        if (already) {
            static int told;
            linked = 1;
            if (!told) {
                told = 1;
                jarkins_log( "jarkins: strip still in this list - not spliced again\n");
            }
        }
        /* A list we walked to its end, long enough to be the frame's own. */
        else if (last && n >= 64 && walk == 0x00FFFFFFu) {
            const uint32_t tag = psx_mod_read_word(0x80000000u | last);
            psx_mod_write_word(0x80000000u | last,
                               (tag & 0xFF000000u) | mine);
            linked = 1;
        }
    }
#ifdef JARKINS_CHAINS_TMST
    jarkins_tmst_prepass(start_addr);
#else
    __real_gpu_ws_prepass_linked_list(start_addr);
#endif
}
