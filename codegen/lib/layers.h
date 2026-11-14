#ifndef LAYERS_H
#define LAYERS_H

// The frame as two layers for front ends that place them at different
// depths (the 3DS's stereoscopic screen): the background, each pixel with a
// material from the game's level data, as the web themes' scene.frag reads
// it, and a plane (behind the screen, on it, fixed), and the sprites. Drawn from the Video state, like render.c's
// whole-frame path (frames split mid-screen and tall sprites are drawn as a
// single solid background by render_frame instead).

#include <stddef.h>
#include "video.h"

#define LAYER_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)

// background materials (web/themes/common.glsl)
#define M_BACKDROP 0 // the backdrop colour shows
#define M_SCENERY 1  // hills, clouds, bushes, castles: not in the block buffer
#define M_SOLID 2    // ground, bricks, blocks, pipes (and everything, without level data)
#define M_CLIMB 3    // vines, the flagpole
#define M_COIN 4
#define M_QUESTION 5
#define M_LAVA 6
#define M_HUD 7      // the fixed status bar
#define M_OPAQUE 0x80 // with a material: the pixel is not the backdrop colour

// The plane each background pixel is drawn on.
#define PLANE_CLEAR 0  // nothing: the backdrop shows
#define PLANE_BEHIND 1 // the scenery
#define PLANE_ON 2     // the blocks, the ground, coins
#define PLANE_FIXED 3  // the status bar

// Each background pixel's material, row by row (M_OPAQUE set where the
// pattern is not transparent).
extern uint8_t layer_background_material[LAYER_PIXELS];
// The backdrop's NES colour (0-63).
extern uint8_t layer_backdrop;

// Where layers_render draws: two images of 32-bit texels in the front end's
// layout (the 3DS: tiled RGBA8 textures the GPU draws at several depths).
typedef struct {
    uint32_t *background;          // colors[c] | plane_bits[plane]
    uint32_t *sprites;             // colors[c] | sprite_bits, 0 where none
    size_t sprites_bytes;          // cleared before the sprites are drawn
    const uint32_t *colors;        // the NES's 64 colours as texels
    uint32_t plane_bits[4];        // by plane
    uint32_t sprite_bits;
    const uint32_t *row_offset;    // [SCREEN_HEIGHT]: where each row starts, in texels
    const uint32_t *column_offset; // [SCREEN_WIDTH]: each column's offset in a row
} LayerTarget;

// SMB's level data, from its RAM and PRG (maps/smb.map): with
// `blocks` NULL, every opaque background pixel is solid.
typedef struct {
    const uint8_t *blocks;   // block_buffer1 then block_buffer2: 2 x 13 rows x 16 metatiles
    int area_kind;           // 3: castle
    uint8_t climb_limits[4]; // climb_metatile_limits
} LayerLevel;

// SMB's level data read through the CPU's memory map.
LayerLevel layers_smb_level(void);

// Draws both layers into `target`. Returns false, drawing nothing, when the
// frame needs render.c's scanline path (render_frame).
bool layers_render(const LayerLevel *level, const LayerTarget *target);

#endif
