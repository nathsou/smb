#ifndef SMB_RENDER_H
#define SMB_RENDER_H

#include <stdint.h>
#include "video.h"

#define FRAME_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 3)

// The last rendered frame, 3 bytes per pixel (RGB).
extern uint8_t frame[FRAME_BUFFER_SIZE];

// The NES master palette: 64 RGB triples.
extern uint8_t COLOR_PALETTE[];

void render_init(void);
void render_frame(void);

// Each scanline's settings, for the GPU renderers (video.frag's u_lines):
// VIDEO_LINE_SIZE bytes a line, the control and mask registers, the
// scanline's position in the 512x480 plane of the nametables (x and row, two
// bytes each, low first), whether it is part of a status bar, whether the
// sprites are the general path's (the hardware's priority, a sprite behind
// the background hiding those after it, and left masking by pixel; else
// those after it show through, and masking drops sprites at x 0) and the 8
// CHR banks (1 KiB each) mapped at $0000-$1FFF. video_prepare_lines
// fills them from the display state.
#define VIDEO_LINE_SIZE 16
extern uint8_t video_lines[];
void video_prepare_lines(void);

#endif
