#include "gpu.h"
#include "lib/common.h"
#include "lib/debug_chr.h"
#include "lib/rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Replays rec/warpless.rec and compares the GPU renderer with the software
// renderer pixel for pixel. Needs an OpenGL 3.3 context (Xvfb works).
//
// usage: ./gpu_check [--debug-chr] [--every N]

#define ROM_PATH "smb.nes"
#define REC_FILE "rec/warpless.rec"
#define SHADER_PATH "web/video.frag"
#define RECORDING_FRAME_COUNT 7987

static void write_ppm(const char *path, const uint8_t *rgb, bool bottom_up) {
    FILE *file = fopen(path, "wb");
    if (!file) return;
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int row = bottom_up ? SCREEN_HEIGHT - 1 - y : y;
        fwrite(&rgb[row * SCREEN_WIDTH * 3], 1, SCREEN_WIDTH * 3, file);
    }
    fclose(file);
}

int main(int argc, char **argv) {
    bool debug_chr = false;
    int every = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug-chr") == 0) debug_chr = true;
        if (strcmp(argv[i], "--every") == 0 && i + 1 < argc) every = atoi(argv[++i]);
    }

    cpu_init();
    if (debug_chr) {
        uint8_t chr[DEBUG_CHR_SIZE];
        debug_chr_generate(chr);
        video_init(chr);
    } else if (read_chr_rom(ROM_PATH)) {
        return 1;
    }
    render_init();

    NESInputRecording recording;
    if (rec_open(REC_FILE, "r", &recording) != 0) {
        fprintf(stderr, "Could not open %s\n", REC_FILE);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "gpu check");
    GpuRenderer gpu;
    if (!gpu_renderer_init(&gpu, SHADER_PATH)) {
        fprintf(stderr, "Could not load the GPU renderer\n");
        return 1;
    }

    cpu_reset_entry();
    int compared = 0, mismatched_frames = 0;
    long first_mismatch = -1;

    for (uint32_t i = 0; i < RECORDING_FRAME_COUNT; i++) {
        update_controller1(rec_state_at_frame(&recording, i));
        next_frame();
        if (i % (uint32_t)every != 0) continue;

        render_frame();
        gpu_renderer_draw(&gpu);
        Image image = LoadImageFromTexture(gpu.target.texture);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        const uint8_t *pixels = image.data;

        int differences = 0;
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            // The render target's first row is the bottom of the screen.
            const uint8_t *gpu_row = &pixels[(SCREEN_HEIGHT - 1 - y) * SCREEN_WIDTH * 3];
            if (memcmp(gpu_row, &frame[y * SCREEN_WIDTH * 3], SCREEN_WIDTH * 3) != 0) differences++;
        }
        compared++;
        if (differences) {
            mismatched_frames++;
            if (first_mismatch < 0) {
                first_mismatch = (long)i;
                printf("frame %u: %d rows differ (written to gpu_mismatch.ppm, cpu_mismatch.ppm)\n", i, differences);
                write_ppm("gpu_mismatch.ppm", pixels, true);
                write_ppm("cpu_mismatch.ppm", frame, false);
            }
        }
        UnloadImage(image);
    }

    gpu_renderer_unload(&gpu);
    CloseWindow();
    rec_close(&recording);

    printf("Compared %d frames, %d differ\n", compared, mismatched_frames);
    return mismatched_frames == 0 ? 0 : 1;
}
