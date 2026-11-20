/* Jarkins Lab: the shared overlay canvas (jarkins_osd.h). */
#include "jarkins_osd.h"
#include "jarkins_video.h"

#include <string.h>

static uint32_t cv[JARKINS_OSD_W * JARKINS_OSD_H];
static unsigned cur_frame;          /* the frame the canvas is being built for */
static int have_frame;              /* cur_frame holds a frame number */
static int dirty;                   /* something drew into the canvas */
/* The box the canvas drew in, so the picture path uploads only what it
 * needs (an empty box is a frame with nothing to show). */
static int ink_x0 = JARKINS_OSD_W, ink_y0 = JARKINS_OSD_H, ink_x1 = -1, ink_y1 = -1;
static int blend;                   /* the fill about to be made blends */

/* GP0 colour (0x00BBGGRR) to the ARGB the host overlay takes (0xAARRGGBB). */
static uint32_t argb_of(uint32_t gp0) {
    return 0xFF000000u | ((gp0 & 0xFFu) << 16) | (gp0 & 0xFF00u) |
           ((gp0 >> 16) & 0xFFu);
}

void jarkins_osd_frame(unsigned frame_no) {
    /* Only a *newer* number opens a frame. Features number the vblank
     * themselves, and if one is a frame behind it must neither open the frame
     * again (presenting the canvas mid-frame) nor present an empty picture over
     * one another feature has just drawn: it just draws into the frame that is
     * already open. */
    if (have_frame && frame_no <= cur_frame)
        return;
    /* Present last frame's picture. The host overlay keeps a picture until it is
     * replaced or cleared, so a frame in which nothing drew has to take the last
     * picture away: that is how a feature whose gate has closed disappears
     * instead of staying on screen for the rest of the session. */
    jarkins_video_show(dirty ? cv : NULL, JARKINS_OSD_W, JARKINS_OSD_H,
                       ink_x0, ink_y0, ink_x1, ink_y1);
    memset(cv, 0, sizeof cv);
    blend = 0;
    dirty = 0;
    ink_x0 = JARKINS_OSD_W; ink_y0 = JARKINS_OSD_H; ink_x1 = -1; ink_y1 = -1;
    cur_frame = frame_no;
    have_frame = 1;
}

void jarkins_osd_put(int x, int y, uint32_t argb) {
    if (x < 0 || y < 0 || x >= JARKINS_OSD_W || y >= JARKINS_OSD_H)
        return;
    uint32_t *d = &cv[y * JARKINS_OSD_W + x];
    dirty = 1;
    if (x < ink_x0) ink_x0 = x;
    if (y < ink_y0) ink_y0 = y;
    if (x > ink_x1) ink_x1 = x;
    if (y > ink_y1) ink_y1 = y;
    if (!blend) {
        *d = argb;
        return;
    }
    const unsigned sa = (argb >> 24) & 0xFFu, ia = 255u - sa;
    const unsigned sr = (argb >> 16) & 0xFFu, sg = (argb >> 8) & 0xFFu, sb = argb & 0xFFu;
    const unsigned dr = (*d >> 16) & 0xFFu, dg = (*d >> 8) & 0xFFu, db = *d & 0xFFu;
    const unsigned da = (*d >> 24) & 0xFFu;
    const unsigned a = sa + da * ia / 255u;
    *d = (a << 24) | (((sr * sa + dr * ia) / 255u) << 16) |
         (((sg * sa + dg * ia) / 255u) << 8) | ((sb * sa + db * ia) / 255u);
}

void jarkins_osd_quad_half(int x, int y, int w, int h, uint32_t gp0) {
    const uint32_t c = (0x80u << 24) | (argb_of(gp0) & 0x00FFFFFFu);
    blend = 1;
    for (int r = 0; r < h; r++)
        for (int i = 0; i < w; i++)
            jarkins_osd_put(x + i, y + r, c);
    blend = 0;
}

/* --- the game's own HUD font ----------------------------------------------
 * The readouts are set in the font the game itself draws its HUD in - the
 * glyphs behind the STAGE line and its timer: the 4bpp sheet the text routine at
 * 0x8004ce74 draws from (glyph u/v table at 0x80021b98, texture page 13), ten
 * texels wide and sixteen tall, one texel of cell per character. Each row below
 * is one scanline of a cell, ten bits wide, most significant bit on the left;
 * ink is every palette index the glyph covers (the game's own cells carry a
 * one-texel index of dark rim that would merge neighbouring glyphs, so the
 * masks start at index 2). Only the glyphs the overlays draw: the digits, the
 * letters of "COMBO" and "DAMAGE", '+' between a string's hits, and space.
 * Anything else draws as a blank. */
/* The 0, and the O with it (both draw from these cells): the middle of the
 * counter - cols 3 to 5 of the sides - is ink and the body is the ring around
 * it, so the glyph reads as a ring rather than a block in one colour. */
static const uint16_t F_0[16] = {0x0FC, 0x1FE, 0x386, 0x376, 0x376, 0x376, 0x376, 0x376, 0x376, 0x376, 0x376, 0x376, 0x376, 0x30E, 0x3FC, 0x1F8};
static const uint16_t F_1[16] = {0x000, 0x078, 0x088, 0x108, 0x188, 0x048, 0x048, 0x048, 0x048, 0x048, 0x048, 0x048, 0x186, 0x102, 0x1FE, 0x000};
static const uint16_t F_2[16] = {0x078, 0x0CC, 0x1B6, 0x17A, 0x34A, 0x2CA, 0x39A, 0x036, 0x06C, 0x058, 0x0D0, 0x1B0, 0x360, 0x2FF, 0x201, 0x3FF};
static const uint16_t F_3[16] = {0x078, 0x184, 0x132, 0x24A, 0x18A, 0x00A, 0x034, 0x024, 0x032, 0x009, 0x005, 0x385, 0x249, 0x132, 0x186, 0x078};
static const uint16_t F_4[16] = {0x007, 0x00D, 0x019, 0x035, 0x02D, 0x06D, 0x0DD, 0x0B5, 0x1A5, 0x365, 0x2FD, 0x201, 0x3FD, 0x005, 0x005, 0x007};
static const uint16_t F_5[16] = {0x3FF, 0x201, 0x2FF, 0x280, 0x280, 0x280, 0x280, 0x2FC, 0x206, 0x3FB, 0x00D, 0x005, 0x00D, 0x3FB, 0x206, 0x3FC};
static const uint16_t F_6[16] = {0x00E, 0x01A, 0x036, 0x06C, 0x0D8, 0x1B0, 0x360, 0x2FE, 0x203, 0x2FD, 0x285, 0x285, 0x285, 0x2FD, 0x303, 0x1FE};
static const uint16_t F_7[16] = {0x3FF, 0x201, 0x3FD, 0x00B, 0x00A, 0x01A, 0x016, 0x014, 0x034, 0x02C, 0x028, 0x068, 0x058, 0x050, 0x050, 0x070};
static const uint16_t F_8[16] = {0x0FC, 0x303, 0x2FD, 0x285, 0x285, 0x285, 0x2FD, 0x303, 0x2FD, 0x285, 0x285, 0x285, 0x285, 0x2FD, 0x303, 0x0FC};
static const uint16_t F_9[16] = {0x0FC, 0x303, 0x2FD, 0x285, 0x285, 0x285, 0x2FD, 0x303, 0x1F6, 0x02C, 0x068, 0x058, 0x0D0, 0x0B0, 0x0A0, 0x0E0};
static const uint16_t F_M[16] = {0x3CF, 0x249, 0x249, 0x279, 0x2B5, 0x2B5, 0x2B5, 0x2B5, 0x2B5, 0x2B5, 0x2CD, 0x2CD, 0x2CD, 0x2CD, 0x2CD, 0x3FF};
static const uint16_t F_C[16] = {0x0FC, 0x186, 0x37B, 0x2CD, 0x285, 0x287, 0x280, 0x280, 0x280, 0x280, 0x287, 0x285, 0x2CD, 0x37B, 0x186, 0x0FC};
static const uint16_t F_B[16] = {0x3FC, 0x203, 0x2FD, 0x285, 0x285, 0x285, 0x2FD, 0x203, 0x2FD, 0x285, 0x285, 0x285, 0x285, 0x2FD, 0x203, 0x3FC};
static const uint16_t F_A[16] = {0x078, 0x048, 0x048, 0x0CC, 0x0B4, 0x0B4, 0x0B4, 0x1B6, 0x17A, 0x17A, 0x102, 0x37B, 0x2CD, 0x285, 0x285, 0x387};
static const uint16_t F_D[16] = {0x3FC, 0x203, 0x2FD, 0x285, 0x285, 0x285, 0x285, 0x285, 0x285, 0x285, 0x285, 0x285, 0x285, 0x2FD, 0x203, 0x3FC};
static const uint16_t F_E[16] = {0x3FC, 0x202, 0x2FE, 0x280, 0x280, 0x280, 0x2FC, 0x204, 0x2FC, 0x280, 0x280, 0x280, 0x280, 0x2FE, 0x202, 0x3FC};
static const uint16_t F_G[16] = {0x0FC, 0x186, 0x37B, 0x2CD, 0x287, 0x280, 0x280, 0x280, 0x2BF, 0x2A1, 0x2BD, 0x285, 0x2CD, 0x37B, 0x186, 0x0FC};
static const uint16_t F_PLUS[16] = {0x000, 0x000, 0x000, 0x000, 0x070, 0x050, 0x050, 0x3DE, 0x202, 0x3DE, 0x050, 0x050, 0x070, 0x000, 0x000, 0x000};
static const uint16_t F_BLANK[16] = {0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000};
/* The cells the whole-glyph colour draws instead. Both parts take the one colour
 * there, so a filled counter would turn the glyph into a block: the A's and the
 * M's counters are drawn in the cell itself (two-texel strokes, the weight of a
 * rim and its body) and the 0's and the 8's are left open - holes the stage
 * shows through, the ring round each being what makes it read as a 0 or an 8. */
static const uint16_t F_A_SOLID[16] = {0x030, 0x030, 0x078, 0x078, 0x0CC, 0x0CC, 0x186, 0x186, 0x186, 0x387, 0x3FF, 0x3FF, 0x303, 0x303, 0x303, 0x387};
static const uint16_t F_M_SOLID[16] = {0x303, 0x387, 0x34B, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x303, 0x303, 0x303, 0x303, 0x303, 0x303, 0x303};
static const uint16_t F_0_SOLID[16] = {0x0FC, 0x1FE, 0x386, 0x306, 0x306, 0x306, 0x306, 0x306, 0x306, 0x306, 0x306, 0x306, 0x306, 0x30E, 0x3FC, 0x1F8};
/* The 8: two counters the same way, split by a waist two texels deep. */
static const uint16_t F_8_SOLID[16] = {0x0FC, 0x1FE, 0x386, 0x306, 0x306, 0x306, 0x306, 0x1FE, 0x1FE, 0x306, 0x306, 0x306, 0x306, 0x386, 0x1FE, 0x0FC};
static const uint16_t BODY_PLUS[16] = {0x000, 0x000, 0x000, 0x000, 0x000, 0x020, 0x020, 0x020, 0x1FC, 0x020, 0x020, 0x020, 0x000, 0x000, 0x000, 0x000};
static const uint16_t BODY_G[16] = {0x000, 0x078, 0x084, 0x102, 0x100, 0x100, 0x100, 0x100, 0x100, 0x11E, 0x102, 0x102, 0x102, 0x084, 0x078, 0x000};
static const uint16_t BODY_D[16] = {0x000, 0x1FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x1FC, 0x000};
static const uint16_t BODY_A[16] = {0x000, 0x030, 0x030, 0x030, 0x048, 0x048, 0x048, 0x048, 0x084, 0x084, 0x0FC, 0x084, 0x102, 0x102, 0x102, 0x000};
static const uint16_t BODY_B[16] = {0x000, 0x1FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x1FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x1FC, 0x000};
static const uint16_t BODY_E[16] = {0x000, 0x1FC, 0x100, 0x100, 0x100, 0x100, 0x100, 0x1F8, 0x100, 0x100, 0x100, 0x100, 0x100, 0x100, 0x1FC, 0x000};

static const uint16_t *glyph(char c) {
    switch (c) {
    case '0': return F_0;   case '1': return F_1;   case '2': return F_2;
    case '3': return F_3;   case '4': return F_4;   case '5': return F_5;
    case '6': return F_6;   case '7': return F_7;   case '8': return F_8;
    case '9': return F_9;
    case 'M': return F_M;   case 'C': return F_C;   case 'O': return F_0;
    case 'B': return F_B;   case 'A': return F_A;   case 'D': return F_D;
    case 'E': return F_E;   case 'G': return F_G;   case '+': return F_PLUS;
    default: return F_BLANK;
    }
}

/* Ten texels of cell per glyph, plus one texel of space before the next. The
 * game's own cells are packed edge to edge, but each carries a one-texel index
 * of dark rim that the masks here leave out (they start at index 2), and that rim
 * is what kept the game's words apart. Set solid, the readouts run together -
 * "DAMAGE" has letters touching - so the gap stands in for the rims: one texel
 * from this cell and one from the next make the two texels the game's own
 * lettering leaves between neighbouring strokes. It applies to the width, the
 * ink bearings and the advance alike, because they all measure from here. */
enum { GLYPH_W = 10, GLYPH_H = 16, GLYPH_GAP = 2 };

/* The columns a glyph actually draws in: answers 0 for a blank one. */
static int glyph_ink_span(char c, int *first, int *last) {
    const uint16_t *rows = glyph(c);
    int lo = GLYPH_W, hi = -1;
    for (int r = 0; r < GLYPH_H; r++)
        for (int i = 0; i < GLYPH_W; i++)
            if (rows[r] & (uint16_t)(1u << (GLYPH_W - 1 - i))) {
                if (i < lo) lo = i;
                if (i > hi) hi = i;
            }
    *first = lo;
    *last = hi;
    return hi >= 0;
}

void jarkins_osd_text_ink_edges(const char *text, int scale, int *lead, int *trail) {
    if (scale < 1)
        scale = 1;
    const int adv = GLYPH_W + GLYPH_GAP;
    const int n = (int)strlen(text);
    int first_i = -1, first_col = 0, last_i = -1, last_col = 0;
    for (int i = 0; i < n; i++) {
        int lo, hi;
        if (!glyph_ink_span(text[i], &lo, &hi))
            continue;
        if (first_i < 0) {
            first_i = i;
            first_col = lo;
        }
        last_i = i;
        last_col = hi;
    }
    const int blank = first_i < 0;      /* nothing draws: treat as no bearing */
    if (lead)
        *lead = blank ? 0 : (first_i * adv + first_col) * scale;
    if (trail)
        *trail = blank ? 0 : ((n - 1 - last_i) * adv + (GLYPH_W - 1 - last_col)) * scale;
}

int jarkins_osd_text_width(const char *text, int scale) {
    int n = 0;
    while (text[n])
        n++;
    return n ? n * (GLYPH_W + GLYPH_GAP) * scale - GLYPH_GAP * scale : 0;
}

/* The cells set every stroke as an outline, so a glyph taken straight from them
 * draws hollow. The readouts fill the blanks the ink shuts in - the letter bodies -
 * the way the name plates are set: solid letters rather than outlines. A blank
 * texel is inside when it cannot reach the edge of the cell without crossing ink. */
/* Bodies set out by hand, for the glyphs the flood cannot work out: open
 * strokes, or gaps that leave a texel hanging off the core. The 0's body is the
 * ring inside its rim - cols 2 and 6 of the sides, and the bars above and below
 * the middle - because the middle itself is ink (its cell above). */
static const uint16_t BODY_0[16] = {0x000, 0x000, 0x078, 0x088, 0x088, 0x088, 0x088, 0x088, 0x088, 0x088, 0x088, 0x088, 0x088, 0x0F0, 0x000, 0x000};
static const uint16_t BODY_1[16] = {0x000, 0x000, 0x070, 0x0F0, 0x070, 0x030, 0x030, 0x030, 0x030, 0x030, 0x030, 0x030, 0x078, 0x0FC, 0x000, 0x000};
static const uint16_t BODY_2[16] = {0x000, 0x030, 0x048, 0x084, 0x084, 0x104, 0x004, 0x008, 0x010, 0x020, 0x020, 0x040, 0x080, 0x100, 0x1FE, 0x000};
static const uint16_t BODY_3[16] = {0x000, 0x078, 0x0CC, 0x184, 0x004, 0x004, 0x008, 0x018, 0x00C, 0x006, 0x002, 0x002, 0x186, 0x0CC, 0x078, 0x000};
static const uint16_t BODY_4[16] = {0x000, 0x002, 0x006, 0x00A, 0x012, 0x012, 0x022, 0x042, 0x042, 0x082, 0x102, 0x1FE, 0x002, 0x002, 0x002, 0x000};
static const uint16_t BODY_5[16] = {0x000, 0x1FE, 0x100, 0x100, 0x100, 0x100, 0x100, 0x100, 0x1F8, 0x004, 0x002, 0x002, 0x002, 0x004, 0x1F8, 0x000};
static const uint16_t BODY_6[16] = {0x000, 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x1FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x0FC, 0x000};
static const uint16_t BODY_7[16] = {0x000, 0x1FE, 0x002, 0x004, 0x004, 0x004, 0x008, 0x008, 0x008, 0x010, 0x010, 0x010, 0x020, 0x020, 0x020, 0x000};
static const uint16_t BODY_8[16] = {0x000, 0x0FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x0FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x0FC, 0x000};
static const uint16_t BODY_9[16] = {0x000, 0x0FC, 0x102, 0x102, 0x102, 0x102, 0x102, 0x0FC, 0x008, 0x010, 0x010, 0x020, 0x020, 0x040, 0x040, 0x000};
static const uint16_t BODY_M[16] = {0x000, 0x186, 0x186, 0x186, 0x14A, 0x14A, 0x14A, 0x14A, 0x14A, 0x14A, 0x132, 0x132, 0x132, 0x132, 0x132, 0x000};
static const uint16_t BODY_C[16] = {0x000, 0x078, 0x084, 0x102, 0x102, 0x100, 0x100, 0x100, 0x100, 0x100, 0x100, 0x102, 0x102, 0x084, 0x078, 0x000};

static const uint16_t *glyph_body_fixed(char c) {
    switch (c) {
    case '0': return BODY_0;
    case '1': return BODY_1;
    case '2': return BODY_2;
    case '3': return BODY_3;
    case '4': return BODY_4;
    case '5': return BODY_5;
    case '6': return BODY_6;
    case '7': return BODY_7;
    case '8': return BODY_8;
    case '9': return BODY_9;
    case 'M': return BODY_M;
    case 'C': return BODY_C;
    case 'O': return BODY_0;
    case '+': return BODY_PLUS;
    case 'G': return BODY_G;
    case 'D': return BODY_D;
    case 'A': return BODY_A;
    case 'B': return BODY_B;
    case 'E': return BODY_E;
    default: return NULL;
    }
}

/* The glyphs that are drawn from their own cell when the colour takes the whole
 * glyph: filling the A's and the M's body closes the counters the outline leaves,
 * and both come out a solid block - and the 0, the O and the 8 keep their
 * counters open, so they come out as rings. Everything else fills its own cell. */
static const uint16_t *glyph_solid(char c) {
    switch (c) {
    case 'A': return F_A_SOLID;
    case 'M': return F_M_SOLID;
    case '0': case 'O': return F_0_SOLID;
    case '8': return F_8_SOLID;
    default: return NULL;
    }
}

static void glyph_body(char c, const uint16_t *rows, uint16_t body[GLYPH_H]) {
    const uint16_t *fixed = glyph_body_fixed(c);
    if (fixed) {
        memcpy(body, fixed, sizeof(uint16_t) * GLYPH_H);
        return;
    }
    unsigned char reach[GLYPH_H][GLYPH_W];
    for (int r = 0; r < GLYPH_H; r++)
        for (int i = 0; i < GLYPH_W; i++)
            reach[r][i] = (rows[r] & (uint16_t)(1u << (GLYPH_W - 1 - i))) ? 0u : 1u;
    for (int r = 0; r < GLYPH_H; r++)
        for (int i = 0; i < GLYPH_W; i++)
            if (reach[r][i] == 1u &&
                (r == 0 || r == GLYPH_H - 1 || i == 0 || i == GLYPH_W - 1))
                reach[r][i] = 2u;
    for (int pass = 0; pass < GLYPH_W + GLYPH_H; pass++)
        for (int r = 0; r < GLYPH_H; r++)
            for (int i = 0; i < GLYPH_W; i++)
                if (reach[r][i] == 1u &&
                    ((r > 0 && reach[r - 1][i] == 2u) ||
                     (r < GLYPH_H - 1 && reach[r + 1][i] == 2u) ||
                     (i > 0 && reach[r][i - 1] == 2u) ||
                     (i < GLYPH_W - 1 && reach[r][i + 1] == 2u)))
                    reach[r][i] = 2u;
    /* A blank the ink shuts in is body - unless it is a sliver: the rounded
     * digits carry one-texel gaps between the rim and the counter, and filling
     * those leaves specks floating in the middle of the number. A body one texel
     * wide or tall all the way through is such a gap, so it stays open. */
    unsigned char seen[GLYPH_H][GLYPH_W];
    memset(seen, 0, sizeof seen);
    for (int r0 = 0; r0 < GLYPH_H; r0++)
        for (int i0 = 0; i0 < GLYPH_W; i0++) {
            if (reach[r0][i0] != 1u || seen[r0][i0])
                continue;
            int stack[GLYPH_W * GLYPH_H], n = 0;
            int lo_r = r0, hi_r = r0, lo_i = i0, hi_i = i0;
            stack[n++] = r0 * GLYPH_W + i0;
            seen[r0][i0] = 1u;
            while (n > 0) {
                const int at = stack[--n], r = at / GLYPH_W, i = at % GLYPH_W;
                if (r < lo_r) lo_r = r;
                if (r > hi_r) hi_r = r;
                if (i < lo_i) lo_i = i;
                if (i > hi_i) hi_i = i;
                for (int dr = -1; dr <= 1; dr++)
                    for (int di = -1; di <= 1; di++) {
                        const int nr = r + dr, ni = i + di;
                        if (nr < 0 || ni < 0 || nr >= GLYPH_H || ni >= GLYPH_W)
                            continue;
                        if (reach[nr][ni] != 1u || seen[nr][ni])
                            continue;
                        seen[nr][ni] = 1u;
                        stack[n++] = nr * GLYPH_W + ni;
                    }
            }
            if ((hi_r == lo_r || hi_i == lo_i) && (hi_r - lo_r) * (hi_i - lo_i) <= 16)
                for (int r = lo_r; r <= hi_r; r++)
                    for (int i = lo_i; i <= hi_i; i++)
                        if (reach[r][i] == 1u)
                            reach[r][i] = 2u;
        }
    for (int r = 0; r < GLYPH_H; r++) {
        uint16_t m = 0;
        for (int i = 0; i < GLYPH_W; i++)
            if (reach[r][i] == 1u)
                m |= (uint16_t)(1u << (GLYPH_W - 1 - i));
        body[r] = m;
    }
}

/* What a glyph's body is filled with: white like the plate lettering, except
 * under white ink, which fills black so the letters keep an edge of their own. */
static uint32_t glyph_body_colour(uint32_t argb) {
    return (argb & 0x00FFFFFFu) == 0x00FFFFFFu ? 0xFF000000u : 0xFFFFFFFFu;
}

/* Which of a glyph's two parts the setting's colour takes, and what the other
 * one gets: the plates' white body, or the dark fill white ink already carries. */
static void glyph_colours(uint32_t c, int fill, uint32_t *ink_c, uint32_t *body_c) {
    const uint32_t other = glyph_body_colour(c);
    *ink_c = fill == JARKINS_OSD_FILL_INNER ? other : c;
    *body_c = (fill == JARKINS_OSD_FILL_INNER || fill == JARKINS_OSD_FILL_WHOLE) ? c : other;
}

void jarkins_osd_text_fill(int x, int y, const char *text, uint32_t gp0, int scale, int fill) {
    if (scale < 1)
        scale = 1;
    const uint32_t c = argb_of(gp0);
    uint32_t ink_c, body_c;
    glyph_colours(c, fill, &ink_c, &body_c);
    int cx = x;
    for (const char *p = text; *p; p++) {
        const uint16_t *solid = fill == JARKINS_OSD_FILL_WHOLE ? glyph_solid(*p) : NULL;
        const uint16_t *rows = solid ? solid : glyph(*p);
        uint16_t body[GLYPH_H];
        glyph_body(*p, rows, body);
        const int passes = solid ? 1 : 2;
        for (int pass = 0; pass < passes; pass++) {
            const uint16_t *m = (solid || pass) ? rows : body;
            const uint32_t col = (solid || pass) ? ink_c : body_c;
            for (int r = 0; r < GLYPH_H; r++)
                for (int i = 0; i < GLYPH_W; i++)
                    if (m[r] & (uint16_t)(1u << (GLYPH_W - 1 - i)))
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++)
                                jarkins_osd_put(cx + i * scale + sx, y + r * scale + sy, col);
        }
        cx += (GLYPH_W + GLYPH_GAP) * scale;
    }
}

void jarkins_osd_text(int x, int y, const char *text, uint32_t gp0, int scale) {
    jarkins_osd_text_fill(x, y, text, gp0, scale, JARKINS_OSD_FILL_OUTER);
}

/* ---- Colour settings: one table for the whole pack (jarkins_osd.h) ---- */

#define JGP0(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

static const struct { const char *name; uint32_t gp0; } COLOURS[] = {
    { "red",    JGP0(0xe0, 0x30, 0x30) },
    { "orange", JGP0(0xff, 0xac, 0x00) },
    { "yellow", JGP0(0xff, 0xe0, 0x30) },
    { "green",  JGP0(0x30, 0xe0, 0x70) },
    { "blue",   JGP0(0x60, 0xa8, 0xff) },
    { "purple", JGP0(0xb0, 0x60, 0xff) },
    { "pink",   JGP0(0xff, 0x70, 0xb0) },
    { "brown",  JGP0(0xa0, 0x5a, 0x20) },
    { "black",  JGP0(0x10, 0x10, 0x10) },
    { "white",  JGP0(0xff, 0xff, 0xff) },
};

/* The header's count and the table above must not drift apart. */
typedef char jarkins_osd_colours_match[
    JARKINS_OSD_COLOUR_COUNT == (int)(sizeof COLOURS / sizeof COLOURS[0]) ? 1 : -1];

const char *jarkins_osd_colour_name(int index) {
    if (index < 0 || index >= JARKINS_OSD_COLOUR_COUNT)
        return NULL;
    return COLOURS[index].name;
}

uint32_t jarkins_osd_colour(int index) {
    if (index < 0 || index >= JARKINS_OSD_COLOUR_COUNT)
        return COLOURS[0].gp0;
    return COLOURS[index].gp0;
}

uint32_t jarkins_osd_colour_of(const char *name, uint32_t fallback) {
    if (!name || !name[0])
        return fallback;
    for (size_t i = 0; i < sizeof COLOURS / sizeof COLOURS[0]; i++)
        if (strcmp(name, COLOURS[i].name) == 0)
            return COLOURS[i].gp0;
    return fallback;
}

uint32_t jarkins_osd_edge_of(uint32_t gp0) {
    const int r = (int)(gp0 & 0xFF), g = (int)((gp0 >> 8) & 0xFF), b = (int)((gp0 >> 16) & 0xFF);
    /* Rec. 601 luma: dark ink wants a light edge to stand off a dark stage. */
    return (r * 77 + g * 151 + b * 28) / 256 < 48 ? JGP0(0xd8, 0xd8, 0xd8) : JGP0(0x10, 0x08, 0x00);
}
