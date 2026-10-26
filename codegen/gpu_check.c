#include "gpu.h"
#include "lib/common.h"
#include "lib/debug_chr.h"
#include "lib/rec.h"
#ifdef MAPPER
#include "lib/cartridge.h"
#include "lib/mapper.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Replays rec/warpless.rec (rec/metroid.rec with GAME=metroid, rec/smb3.rec
// with GAME=smb3) and compares the GPU renderer with the software renderer
// pixel for pixel. Needs an OpenGL 3.3 context (Xvfb works).
//
// usage: ./gpu_check [--debug-chr] [--every N] [--variations] [--rec FILE]
//   --variations  also draw each compared frame with every mirroring, base
//                 nametable and a few vertical scrolls, which the recording
//                 may not reach

#ifndef ROM_PATH
#define ROM_PATH "smb.nes"
#endif
#ifndef REC_FILE
#ifdef MAPPER
#define REC_FILE "rec/metroid.rec"
#else
#define REC_FILE "rec/warpless.rec"
#endif
#endif
#define SHADER_PATH "web/video.frag"

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

// Draws the frame with both renderers; the number of rows that differ. The
// first mismatch is written out.
static int compare(GpuRenderer *gpu, uint32_t frame_number, const char *variation) {
    static bool written = false;
    render_frame();
    gpu_renderer_draw(gpu);
    Image image = LoadImageFromTexture(gpu->target.texture);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    const uint8_t *pixels = image.data;

    int differences = 0;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        // The render target's first row is the bottom of the screen.
        const uint8_t *gpu_row = &pixels[(SCREEN_HEIGHT - 1 - y) * SCREEN_WIDTH * 3];
        if (memcmp(gpu_row, &frame[y * SCREEN_WIDTH * 3], SCREEN_WIDTH * 3) != 0) differences++;
    }
    if (differences && !written) {
        written = true;
        printf("frame %u%s: %d rows differ (written to gpu_mismatch.ppm, cpu_mismatch.ppm)\n",
               frame_number, variation, differences);
        write_ppm("gpu_mismatch.ppm", pixels, true);
        write_ppm("cpu_mismatch.ppm", frame, false);
    }
    UnloadImage(image);
    return differences;
}

int main(int argc, char **argv) {
    bool debug_chr = false, variations = false;
    int every = 1;
    const char *rec_file = REC_FILE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rec") == 0 && i + 1 < argc) rec_file = argv[++i];
        if (strcmp(argv[i], "--debug-chr") == 0) debug_chr = true;
        if (strcmp(argv[i], "--variations") == 0) variations = true;
        if (strcmp(argv[i], "--every") == 0 && i + 1 < argc) every = atoi(argv[++i]);
    }

    cpu_init();
#ifdef MAPPER
    // the game writes its own tiles (CHR RAM): no debug tiles
    (void)debug_chr;
    if (load_cartridge(ROM_PATH)) return 1;
    mapper_init(cartridge_mapper);
    mapper_mirroring(cartridge_vertical);
#else
    if (debug_chr) {
        uint8_t chr[DEBUG_CHR_SIZE];
        debug_chr_generate(chr);
        video_init(chr);
    } else if (read_chr_rom(ROM_PATH)) {
        return 1;
    }
    render_init();
#endif

    NESInputRecording recording;
    if (rec_open(rec_file, "r", &recording) != 0) {
        fprintf(stderr, "Could not open %s\n", rec_file);
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
    uint32_t frames = rec_length(rec_file);

    for (uint32_t i = 0; i < frames; i++) {
        update_controller1(rec_state_at_frame(&recording, i));
        next_frame();
        if (i % (uint32_t)every != 0) continue;

        compared++;
        if (compare(&gpu, i, "")) mismatched_frames++;
        if (!variations) continue;
        Video saved = video;
        static const uint8_t scrolls[] = {0, 93, 211};
        for (uint8_t mirroring = MIRROR_VERTICAL; mirroring <= MIRROR_SINGLE_UPPER; mirroring++) {
            for (uint8_t base = 0; base < 4; base++) {
                for (size_t k = 0; k < sizeof scrolls; k++) {
                    char variation[64];
                    snprintf(variation, sizeof variation, " (mirroring %u, nametable %u, scroll y %u)",
                             mirroring, base, scrolls[k]);
                    video.mirroring = mirroring;
                    video.ctrl = (uint8_t)((saved.ctrl & ~0x03) | base);
                    video.scroll_y = scrolls[k];
                    compared++;
                    if (compare(&gpu, i, variation)) mismatched_frames++;
                }
            }
        }
        video = saved;
    }

    gpu_renderer_unload(&gpu);
    CloseWindow();
    rec_close(&recording);

    printf("Compared %d frames, %d differ\n", compared, mismatched_frames);
    return mismatched_frames == 0 ? 0 : 1;
}
