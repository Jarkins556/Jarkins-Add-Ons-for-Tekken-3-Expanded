/*
 * Jarkins Lab: the overlay every feature draws into.
 *
 * Why this exists
 *   Every feature draws into one picture of the add-on's own, in the game's
 *   368x480 coordinates: the strip its band in a corner, the readouts their
 *   numbers by the bars and their combo counter mid-screen. One picture, so two
 *   features switched on at once draw over each other instead of replacing one
 *   another's screen; jarkins_video.c puts it on the frame.
 *
 * How a frame runs
 *   Each enabled feature calls jarkins_osd_frame(frame_no) at the top of its own
 *   vblank, before drawing anything, passing its own count of vblanks (one
 *   increment per vblank, so every feature passes the same number: `frame++;`
 *   then the call). The first call of a new frame hands the picture built last
 *   frame to jarkins_video.c and clears the canvas. Calls that are not newer
 *   than the open frame are no-ops and the caller still draws into it, so a
 *   feature that is a frame out of step neither presents nor clears over
 *   another's work. Nothing has to agree on a presenter, and a feature
 *   that is switched off simply stops drawing - its pixels are gone from the
 *   next frame because the canvas starts empty every frame.
 *
 * One frame behind
 *   What the screen shows is last frame's picture, so a number is at most 16 ms
 *   old. That is intentional: it is what removes all ordering between features.
 *
 * Nothing to retract
 *   The picture is drawn again from scratch every frame, so a feature whose gate
 *   closes just stops drawing and its pixels are gone from the next frame.
 */
#ifndef JARKINS_OSD_OVERLAY_H
#define JARKINS_OSD_OVERLAY_H

#include <stdint.h>

/* The picture the overlay covers: the game's own video coordinates. */
enum { JARKINS_OSD_W = 368, JARKINS_OSD_H = 480 };

/* How far across the picture a readout's ink may go: the whole canvas. The
 * game's own screen is narrower than this - it clips its primitives to the
 * drawing area it sets (GP0 E3/E4) - so the overlay widens that area's right
 * edge for its own quads and writes the game's words back afterwards
 * (jarkins_video.c, build_chain). Without that, ink past the game's own edge is
 * clipped away and readouts have to stop short of where the stretched
 * widescreen HUD actually ends.
 *
 * This is not how much texture the picture takes either: the two players'
 * readouts are pieces of their own, packed into the one page the half gives us,
 * so the full width here costs no extra page. */
enum { JARKINS_OSD_DRAW_W = JARKINS_OSD_W };

/* Top of a feature's vblank: presents last frame's picture if this is the
 * first call of frame_no, then clears the canvas for this frame's drawing. */
void jarkins_osd_frame(unsigned frame_no);

/* One ARGB pixel (0xAARRGGBB), clipped to the picture. */
void jarkins_osd_put(int x, int y, uint32_t argb);

/* A box blended half and half with what is already there. */
void jarkins_osd_quad_half(int x, int y, int w, int h, uint32_t gp0);

/* The game's own HUD font (the glyphs behind the STAGE line and its timer): digits, the
 * letters of COMBO and DAMAGE, '+' and space: a character is ten texels of cell
 * and two of space - twelve - and sixteen tall at scale 1; scale 2 draws 24x32.
 * Anything else draws as a blank. */
void jarkins_osd_text(int x, int y, const char *text, uint32_t gp0, int scale);

/* How the two parts of a glyph are coloured: the setting's colour on the rim
 * with a white body (the plates' look, and what jarkins_osd_text draws), on the
 * whole glyph, or on the body with the rim white. As above, a body under white
 * ink goes black instead, so the glyph keeps an edge either way. */
enum { JARKINS_OSD_FILL_OUTER, JARKINS_OSD_FILL_WHOLE, JARKINS_OSD_FILL_INNER };
void jarkins_osd_text_fill(int x, int y, const char *text, uint32_t gp0, int scale, int fill);
int jarkins_osd_text_width(const char *text, int scale);

/* The blank texels before the first drawn column and after the last: text placed
 * by its ink edges instead of its box sits the same distance in whichever side
 * of a bar it is put ("1" starts a column in, so a box-aligned right end and
 * left end do not look alike). */
void jarkins_osd_text_ink_edges(const char *text, int scale, int *lead, int *trail);

/* The colours a colour setting may name, in the order the menus list them: the
 * rainbow as the game's own text uses it, then pink, brown, and black and white.
 *
 * One table for the whole pack. A setting's stored value is one of these names,
 * so "pink" means the same pink in every feature that colours something - the
 * damage readouts today, the HUD colour customizer and the rest as they land -
 * and a new shade only ever has to be added here. */
enum { JARKINS_OSD_COLOUR_COUNT = 10 };
const char *jarkins_osd_colour_name(int index);   /* the stored value, NULL past the end */
uint32_t jarkins_osd_colour(int index);           /* GP0, 0x00BBGGRR */
/* The colour a setting names; `fallback` if the value is empty or not a colour. */
uint32_t jarkins_osd_colour_of(const char *name, uint32_t fallback);
/* The edge drawn behind text in gp0: dark, or light behind dark ink, so black
 * still reads on a dark stage. */
uint32_t jarkins_osd_edge_of(uint32_t gp0);

#endif /* JARKINS_OSD_OVERLAY_H */
