#include "lib/common.h"
#include "lib/debug_chr.h"
#include "lib/rec.h"
#include <stdlib.h>
#include <string.h>

// Replays rec/warpless.rec headlessly and checks two hashes:
// - the framebuffer hash, which depends on the CHR tiles and the renderer;
// - the game-state hash, which covers everything the renderer consumes and
//   all live RAM, but no pixels. It is renderer-independent, so it stays
//   valid while the rendering backend changes.
//
// usage: ./hash [--debug-chr] [--trace FILE] [--write-trace FILE]
//   --debug-chr        use generated tiles instead of smb.nes (ROM-free)
//   --trace FILE       compare per-frame state hashes, report the first
//                      divergent frame
//   --write-trace FILE record per-frame state hashes
//   --dump N[,N...]    write those frames as frame_N.ppm
//   --audio-trace FILE compare per-frame audio hashes: the APU register
//                      writes the game made, and the samples rendered from
//                      them at 48 kHz (one frame's worth, 800 samples, as
//                      16-bit values)
//   --write-audio-trace FILE  record them
//   --dump-audio FILE  write the samples as raw 32-bit floats (48 kHz, mono)
//   --audio-chunk N    render in calls of N samples (the synthesizer must
//                      resume anywhere, as audio callbacks ask for)

#define ROM_PATH "smb.nes"
#define REC_FILE "rec/warpless.rec"
#define HASH_OFFSET_BASIS 2166136261u
#define HASH_PRIME 16777619u
#define RECORDING_FRAME_COUNT 7987
#define EXPECTED_ROM_FRAME_HASH 1633679932u
#define EXPECTED_DEBUG_FRAME_HASH 508916200u
#define EXPECTED_DEBUG_STATE_HASH 1794530440u
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_FRAME_SAMPLES (AUDIO_SAMPLE_RATE / 60)

// The guest stack grows down from $01FF and never reaches below $01E0 in the
// replay; SMB keeps variables in $0100-$013F. Between frames the stack is
// empty (the foreground idles with SP=$FF), so bytes above $013F hold only
// dead return addresses whose values depend on the call convention.
#define STACK_SCRATCH_START 0x140
#define STACK_SCRATCH_END 0x200

static NESInputRecording recording;

static uint32_t hash_bytes(uint32_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= HASH_PRIME;
    }
    return hash;
}

static uint32_t hash_byte(uint32_t hash, uint8_t value) {
    return hash_bytes(hash, &value, 1);
}

static uint32_t hash_state(void) {
    uint32_t hash = HASH_OFFSET_BASIS;
    hash = hash_bytes(hash, ram, STACK_SCRATCH_START);
    hash = hash_bytes(hash, ram + STACK_SCRATCH_END, RAM_SIZE - STACK_SCRATCH_END);
    hash = hash_bytes(hash, video.nametable, NAMETABLE_SIZE);
    hash = hash_bytes(hash, video.palette.u8, PALETTE_SIZE);
    hash = hash_bytes(hash, video.oam, OAM_SIZE);
    hash = hash_byte(hash, video.ctrl);
    hash = hash_byte(hash, video.mask);
    hash = hash_byte(hash, video.scroll_x);
    hash = hash_byte(hash, video.scroll_y);
    return hash;
}

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

static bool flag(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) return true;
    }
    return false;
}

int main(int argc, char **argv) {
    bool debug_chr = flag(argc, argv, "--debug-chr");
    const char *trace_path = option(argc, argv, "--trace");
    const char *write_trace_path = option(argc, argv, "--write-trace");
    const char *dump = option(argc, argv, "--dump");
    const char *audio_trace_path = option(argc, argv, "--audio-trace");
    const char *write_audio_trace_path = option(argc, argv, "--write-audio-trace");
    const char *chunk_option = option(argc, argv, "--audio-chunk");
    const char *dump_audio_path = option(argc, argv, "--dump-audio");
    FILE *dump_audio = dump_audio_path ? fopen(dump_audio_path, "wb") : NULL;
    size_t audio_chunk = chunk_option ? (size_t)strtoul(chunk_option, NULL, 10) : AUDIO_FRAME_SAMPLES;
    if (audio_chunk == 0) audio_chunk = AUDIO_FRAME_SAMPLES;

    cpu_init();
    apu_init(AUDIO_SAMPLE_RATE);

    if (debug_chr) {
        uint8_t chr[DEBUG_CHR_SIZE];
        debug_chr_generate(chr);
        video_init(chr);
        render_init();
    } else if (read_chr_rom(ROM_PATH)) {
        return 1;
    }

    if (rec_open(REC_FILE, "r", &recording) != 0) {
        fprintf(stderr, "Error: Could not open %s\n", REC_FILE);
        return 1;
    }

    FILE *trace = trace_path ? fopen(trace_path, "r") : NULL;
    FILE *write_trace = write_trace_path ? fopen(write_trace_path, "w") : NULL;
    FILE *audio_trace = audio_trace_path ? fopen(audio_trace_path, "r") : NULL;
    FILE *write_audio_trace = write_audio_trace_path ? fopen(write_audio_trace_path, "w") : NULL;
    if ((trace_path && !trace) || (write_trace_path && !write_trace) ||
        (audio_trace_path && !audio_trace) || (write_audio_trace_path && !write_audio_trace)) {
        fprintf(stderr, "Error: Could not open trace file\n");
        return 1;
    }

    cpu_reset_entry();

    uint32_t frame_hash = HASH_OFFSET_BASIS;
    uint32_t state_hash = HASH_OFFSET_BASIS;
    long first_divergence = -1;
    long first_audio_divergence = -1;

    for (uint32_t i = 0; i < RECORDING_FRAME_COUNT; i++) {
        update_controller1(rec_state_at_frame(&recording, i));
        apu_write_hash = APU_WRITE_HASH_RESET;
        next_frame();
        render_frame();
        uint32_t writes = apu_write_hash;
        sound_end_frame();
        float audio[AUDIO_FRAME_SAMPLES];
        for (size_t done = 0; done < AUDIO_FRAME_SAMPLES; done += audio_chunk) {
            size_t left = AUDIO_FRAME_SAMPLES - done;
            apu_render(audio + done, left < audio_chunk ? left : audio_chunk);
        }
        if (dump_audio) fwrite(audio, sizeof(float), AUDIO_FRAME_SAMPLES, dump_audio);
        // as 16-bit samples (computed in double, the same in JavaScript)
        uint32_t samples = HASH_OFFSET_BASIS;
        for (size_t s = 0; s < AUDIO_FRAME_SAMPLES; s++) {
            double scaled = (double)audio[s] * 32767.0 + 0.5;
            int value = (int)scaled;
            if ((double)value > scaled) value--; // floor

            samples = hash_byte(hash_byte(samples, (uint8_t)(value & 0xff)), (uint8_t)((value >> 8) & 0xff));
        }
        if (write_audio_trace) fprintf(write_audio_trace, "%08x %08x\n", writes, samples);
        if (audio_trace && first_audio_divergence < 0) {
            unsigned expected_writes, expected_samples;
            if (fscanf(audio_trace, "%x %x", &expected_writes, &expected_samples) != 2 ||
                expected_writes != writes || expected_samples != samples) {
                first_audio_divergence = (long)i;
                printf("Audio diverges at frame %u: writes %s, samples %s\n", i,
                       expected_writes == writes ? "match" : "differ",
                       expected_samples == samples ? "match" : "differ");
            }
        }

        uint32_t frame_state = hash_state();
        frame_hash = (frame_hash ^ hash_bytes(HASH_OFFSET_BASIS, frame, SCREEN_WIDTH * SCREEN_HEIGHT * 3)) * HASH_PRIME;
        state_hash = (state_hash ^ frame_state) * HASH_PRIME;

        if (listed(dump, i)) dump_frame(i);
        if (write_trace) fprintf(write_trace, "%08x\n", frame_state);
        if (trace && first_divergence < 0) {
            unsigned expected;
            if (fscanf(trace, "%x", &expected) != 1 || expected != frame_state) {
                first_divergence = (long)i;
            }
        }
    }

    rec_close(&recording);
    if (trace) fclose(trace);
    if (write_trace) fclose(write_trace);
    if (audio_trace) fclose(audio_trace);
    if (write_audio_trace) fclose(write_audio_trace);
    if (dump_audio) fclose(dump_audio);

    bool ok = true;
    if (debug_chr) {
        printf("State hash: %u, expected: %u\n", state_hash, EXPECTED_DEBUG_STATE_HASH);
        printf("Frame hash (debug tiles): %u, expected: %u\n", frame_hash, EXPECTED_DEBUG_FRAME_HASH);
        ok = state_hash == EXPECTED_DEBUG_STATE_HASH && frame_hash == EXPECTED_DEBUG_FRAME_HASH;
    } else {
        printf("Frame hash (ROM tiles): %u, expected: %u\n", frame_hash, EXPECTED_ROM_FRAME_HASH);
        ok = frame_hash == EXPECTED_ROM_FRAME_HASH;
    }

    if (trace_path) {
        if (first_divergence >= 0) {
            printf("State diverges from %s at frame %ld\n", trace_path, first_divergence);
            ok = false;
        } else {
            printf("Per-frame state matches %s\n", trace_path);
        }
    }

    if (audio_trace_path) {
        if (first_audio_divergence >= 0) {
            ok = false;
        } else {
            printf("Per-frame audio matches %s\n", audio_trace_path);
        }
    }

    printf("%s\n", ok ? "OK" : "MISMATCH");
    return ok ? 0 : 1;
}
