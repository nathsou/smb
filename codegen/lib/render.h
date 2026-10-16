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

#endif
