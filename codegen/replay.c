#include "lib/cpu.h"
#include "lib/video.h"
#include "lib/render.h"
#include "lib/sound.h"
#include "lib/cartridge.h"
#include "lib/rec.h"
#include "lib/ppu_bus.h"
#include "lib/state_hash.h"
#include "lib/state.h"
#ifdef MAPPER
#include "lib/mapper.h"
#endif
#include <stdlib.h>
#include <string.h>

// Replays an input recording headlessly for programs that drive the PPU
// through its registers (Metroid), and hashes the state after each frame
// (state_hash.c).
//
// usage: ./replay ROM.nes [--rec FILE] [--frames N] [--trace FILE]
//                 [--write-trace FILE] [--dump N[,N...]]
//   --rec FILE          joypad 1 states by frame ("frame:state" lines)
//   --frames N          frames to run (default: to the recording's last)
//   --trace FILE        compare per-frame state hashes, report the first
//                       divergent frame
//   --write-trace FILE  record per-frame state hashes
//   --dump N[,N...]     write those frames as frame_N.ppm
//   --dump-every N      write every Nth frame
//   --check-states N    every N frames, save the state, run 60 frames, load
//                       it and run them again: the states must be the same

static void dump_frame(uint32_t index) {
    char path[32];
    snprintf(path, sizeof(path), "frame_%u.ppm", index);
    FILE *file = fopen(path, "wb");
    if (!file) return;
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    fwrite(frame, 1, SCREEN_WIDTH * SCREEN_HEIGHT * 3, file);
    fclose(file);
}

static bool listed(const char *list, uint32_t index) {
    while (list && *list) {
        char *end;
        unsigned long value = strtoul(list, &end, 10);
        if (end == list) return false;
        if (value == index) return true;
        list = *end == ',' ? end + 1 : end;
    }
    return false;
}

static const char *option(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM.nes [--rec FILE] [--frames N] [--trace FILE] [--write-trace FILE] [--dump N,...]\n", argv[0]);
        return 1;
    }
    const char *rec_path = option(argc, argv, "--rec");
    const char *frames_option = option(argc, argv, "--frames");
    const char *trace_path = option(argc, argv, "--trace");
    const char *write_trace_path = option(argc, argv, "--write-trace");
    const char *dump = option(argc, argv, "--dump");
    const char *every_option = option(argc, argv, "--dump-every");
    uint32_t every = every_option ? (uint32_t)strtoul(every_option, NULL, 10) : 0;
    const char *states_option = option(argc, argv, "--check-states");
    uint32_t states_every = states_option ? (uint32_t)strtoul(states_option, NULL, 10) : 0;

    cpu_init();
    apu_init(48000);
    if (load_cartridge(argv[1])) return 1;
#ifdef MAPPER
    mapper_init();
#endif

    NESInputRecording recording = {0};
    if (rec_path && rec_open(rec_path, "r", &recording) != 0) {
        fprintf(stderr, "Error: Could not open %s\n", rec_path);
        return 1;
    }
    uint32_t frames = frames_option ? (uint32_t)strtoul(frames_option, NULL, 10)
                    : rec_path ? rec_length(rec_path) : 600;

    FILE *trace = trace_path ? fopen(trace_path, "r") : NULL;
    FILE *write_trace = write_trace_path ? fopen(write_trace_path, "w") : NULL;
    if ((trace_path && !trace) || (write_trace_path && !write_trace)) {
        fprintf(stderr, "Error: Could not open trace file\n");
        return 1;
    }

    cpu_reset_entry();
    long first_divergence = -1;
    for (uint32_t i = 0; i < frames; i++) {
        if (states_every && i % states_every == 0 && i > 0 && rec_path) {
            static uint8_t saved[SAVE_STATE_SIZE];
            uint32_t hashes[60];
            save_state(saved);
            NESInputRecording again = {0};
            rec_open(rec_path, "r", &again);
            for (uint32_t k = 0; k < 60; k++) {
                update_controller1(rec_state_at_frame(&again, i + k));
                next_frame();
                sound_end_frame();
                hashes[k] = hash_state();
            }
            rec_close(&again);
            load_state(saved);
            rec_open(rec_path, "r", &again);
            for (uint32_t k = 0; k < 60; k++) {
                update_controller1(rec_state_at_frame(&again, i + k));
                next_frame();
                sound_end_frame();
                if (hash_state() != hashes[k]) {
                    printf("A state loaded at frame %u diverges %u frames later\n", i, k);
                    return 1;
                }
            }
            rec_close(&again);
            load_state(saved);
        }
        update_controller1(rec_path ? rec_state_at_frame(&recording, i) : 0);
        next_frame();
        sound_end_frame();
        uint32_t state = hash_state();
        if (write_trace) fprintf(write_trace, "%08x\n", state);
        if (trace && first_divergence < 0) {
            unsigned expected;
            if (fscanf(trace, "%x", &expected) != 1 || expected != state) first_divergence = (long)i;
        }
        if (listed(dump, i) || (every && i % every == 0)) {
            render_frame();
            dump_frame(i);
        }
    }
    if (rec_path) rec_close(&recording);
    if (write_trace) fclose(write_trace);
    if (trace) {
        fclose(trace);
        if (first_divergence >= 0) {
            printf("State diverges from %s at frame %ld\n", trace_path, first_divergence);
            return 1;
        }
        printf("Per-frame state matches %s\n", trace_path);
    }
    if (states_every) printf("States saved every %u frames load back to the same frames\n", states_every);
    printf("Ran %u frames\n", frames);
    return 0;
}
