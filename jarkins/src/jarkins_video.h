/* Jarkins Lab: getting the overlay's picture onto the screen (jarkins_video.h).
 *
 * Why this exists
 *   The canvas in jarkins_osd.c is the add-on's own ARGB picture. Something has
 *   to put it over the game's frame, and an add-on may not change the runtime
 *   to get a host-side overlay: it may only add sources to the game's build
 *   and wrap symbols the game does not wrap (addons/README.md, add-on API 1).
 *   So the picture goes the way a mod's picture goes: into VRAM the game does
 *   not use, drawn by a GPU packet of the add-on's own, spliced into the
 *   frame's display list. The recipe is the one TMST's overlay uses.
 *
 * Where the picture goes
 *   The upper VRAM half (y 512..1023, as on Namco System 12) is absent by
 *   default and a plugin asks for it at activation; the game's own 1024x512
 *   texture space is never touched. The canvas is cut into 256x256 tiles (a
 *   15-bit texture page is 256x256 and a packet's U/V are 8 bits), each tile
 *   uploaded into the half once a frame, and each drawn back with one
 *   textured quad that the GPU reads from there.
 *
 * Transparency
 *   A texel of 0x0000 reads as see-through to the GPU - that is how the canvas'
 *   empty pixels stay empty - and one with bit 15 set is half see-through. The
 *   quads are semi-transparent textured quads, so those texels blend with the
 *   frame under them. A black pixel is not 0x0000 but the darkest blue there
 *   is, else black ink would read as a hole.
 *
 * How it gets drawn
 *   gpu_ws_prepass_linked_list(start) is the runtime's "the GPU is about to
 *   walk this frame's ordering table": the add-on wraps it, and before the
 *   real one runs, appends its packet chain to the end of the frame's main
 *   list (the one the game's primitives fill, not a set-up list), where it is
 *   drawn with the rest of the frame. The chain sets its own texture page,
 *   draws the tiles, and is skipped entirely when the canvas is empty.
 *
 * One frame behind
 *   The canvas is built during a frame and shown from the next one, the same
 *   frame the canvas itself is presented on.
 */
#ifndef JARKINS_VIDEO_H
#define JARKINS_VIDEO_H

#include <stdint.h>

/* From activation: asks for the upper VRAM half and the packet memory. Answers
 * 1 when the picture can be shown; nothing is drawn (and nothing breaks) on a
 * release that cannot give the half. */
int jarkins_video_prepare(void);

/* A line of the add-on's own log: jarkins.log beside the game (the working
 * directory the game runs in), and stderr as well. Written immediately, so a
 * crash does not take the last lines with it. */
void jarkins_log(const char *fmt, ...);

/* Shows `argb` (w x h, the game's 368x480 coordinates) on the next frame, or
 * stops showing anything when the ink box is empty. x0/y0/x1/y1 are the drawn
 * pixels of the picture, inclusive; dx0..dy1 the same picture's ink box. */
void jarkins_video_show(const uint32_t *argb, int w, int h,
                        int x0, int y0, int x1, int y1);

#endif /* JARKINS_VIDEO_H */
