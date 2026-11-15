// Checks codegen/lib/layers.c against render.c over a recording: on every
// frame, the layers flattened the way the 3DS draws them with the 3D slider
// down (the sprites, else the background's planes, else the backdrop) must
// be render_frame's picture, pixel for pixel.
//
// usage: ./layers_check ROM.nes --rec FILE [--frames N] [--dump N]
//   --dump N   write the first N mismatching frames as
//              layers_N.ppm (render.c's picture left, the layers' right)
//
// Build: make layers-check GAME=zelda (or any game; SMB with its level data)

#include "cpu.h"
#include "video.h"
#include "render.h"
#include "sound.h"
#include "apu.h"
#include "rec.h"
#include "layers.h"
#ifdef MAPPER
#include "cartridge.h"
#include "mapper.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// render.c's palette, for the layers' colours
extern uint8_t COLOR_PALETTE[];

static uint32_t background[256 * 256];
static uint32_t sprites[256 * 256];
static uint32_t colors[64];
static uint32_t row_offset[SCREEN_HEIGHT];
static uint32_t column_offset[SCREEN_WIDTH];
static uint8_t flat[SCREEN_WIDTH * SCREEN_HEIGHT * 3];

static const char *option(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return NULL;
}

static void dump(uint32_t frame_index, const uint8_t *reference, const uint8_t *layered) {
    char path[48];
    snprintf(path, sizeof path, "layers_%u.ppm", frame_index);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH * 2, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        fwrite(&reference[y * SCREEN_WIDTH * 3], 1, SCREEN_WIDTH * 3, f);
        fwrite(&layered[y * SCREEN_WIDTH * 3], 1, SCREEN_WIDTH * 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM.nes --rec FILE [--frames N] [--dump N]\n", argv[0]);
        return 2;
    }
    const char *rec_path = option(argc, argv, "--rec");
    const char *frames_option = option(argc, argv, "--frames");
    const char *dump_option = option(argc, argv, "--dump");
    int dumps_left = dump_option ? atoi(dump_option) : 0;

    cpu_init();
    apu_init(48000);
#ifdef MAPPER
    if (load_cartridge(argv[1])) return 1;
    mapper_init(cartridge_mapper);
    mapper_mirroring(cartridge_vertical);
#else
    {
        FILE *rom = fopen(argv[1], "rb");
        static uint8_t image[0x10000];
        if (!rom) return 1;
        size_t n = fread(image, 1, sizeof image, rom);
        fclose(rom);
        size_t prg = (size_t)image[4] * 0x4000;
        if (n < 16 + prg + 0x2000) return 1;
        video_init(&image[16 + prg]);
        render_init();
    }
#endif

    NESInputRecording recording = {0};
    if (rec_path && rec_open(rec_path, "r", &recording) != 0) {
        fprintf(stderr, "Error: Could not open %s\n", rec_path);
        return 1;
    }
    uint32_t frames = frames_option ? (uint32_t)strtoul(frames_option, NULL, 10) : rec_path ? rec_length(rec_path) : 600;

    // a linear layout: plane in the top byte, then RGB
    for (int i = 0; i < 64; i++) {
        colors[i] = (uint32_t)COLOR_PALETTE[i * 3] << 16 | (uint32_t)COLOR_PALETTE[i * 3 + 1] << 8 | COLOR_PALETTE[i * 3 + 2];
    }
    for (int y = 0; y < SCREEN_HEIGHT; y++) row_offset[y] = (uint32_t)y * 256;
    for (int x = 0; x < SCREEN_WIDTH; x++) column_offset[x] = (uint32_t)x;
    LayerTarget target = {
        .background = background, .sprites = sprites, .sprites_bytes = sizeof sprites, .colors = colors,
        .plane_bits = { 0, 1u << 24, 2u << 24, 3u << 24 }, .sprite_bits = 0xFFu << 24,
        .row_offset = row_offset, .column_offset = column_offset,
    };

    cpu_reset_entry();
    uint32_t bad_frames = 0, first_bad = 0;
    for (uint32_t i = 0; i < frames; i++) {
        update_controller1(rec_path ? rec_state_at_frame(&recording, i) : 0);
        next_frame();
        sound_end_frame();
#ifdef SMB_LEVEL
        LayerLevel level = layers_smb_level();
#else
        LayerLevel level = { 0 };
#endif
        layers_render(&level, &target);
        uint32_t backdrop = colors[layer_backdrop];
        for (int p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; p++) {
            uint32_t s = sprites[p], b = background[p];
            uint32_t c = s ? s : (b >> 24) ? b : backdrop;
            flat[p * 3] = (uint8_t)(c >> 16);
            flat[p * 3 + 1] = (uint8_t)(c >> 8);
            flat[p * 3 + 2] = (uint8_t)c;
        }
        render_frame();
        if (memcmp(flat, frame, sizeof flat) != 0) {
            if (bad_frames++ == 0) first_bad = i;
            if (dumps_left > 0) {
                dumps_left--;
                dump(i, frame, flat);
            }
        }
    }
    if (rec_path) rec_close(&recording);
    if (bad_frames) {
        printf("%u of %u frames differ (the first: %u)\n", bad_frames, frames, first_bad);
        return 1;
    }
    printf("All %u frames match\n", frames);
    return 0;
}
