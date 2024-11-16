#include "common.h"
#include "rec.h"

#define REC_FILE "rec/warpless.rec"
#define HASH_OFFSET_BASIS 2166136261
#define HASH_PRIME 16777619
#define RECORDING_FRAME_COUNT 7987
#define EXPECTED_CUMULATIVE_HASH 788985578

static NESInputRecording recording;
static uint32_t frame_counter = 0;

void handle_inputs(void) {
    uint8_t state;

    // Get input from recording
    state = rec_state_at_frame(&recording, frame_counter);
    
    // If we've reached the end of the recording, switch to recording mode
    if (!recording.has_next && frame_counter >= recording.current.frame) {
        // Reopen the file in write mode
        rec_close(&recording);
        if (rec_open(REC_FILE, "a", &recording) != 0) {
            fprintf(stderr, "Error: Could not open recording file for writing\n");
            exit(1);
        }
    }

    update_controller1(state);
}

int rec_init(void) {
    if (rec_open(REC_FILE, "r", &recording) != 0) {
        // If file doesn't exist or can't be opened for reading, create new recording
        if (rec_open(REC_FILE, "w", &recording) != 0) {
            fprintf(stderr, "Error: Could not create recording file\n");
            return 1;
        }
    }

    return 0;
}

uint32_t hash_frame(const uint8_t* data, size_t size) {
    uint32_t hash = HASH_OFFSET_BASIS;

    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= HASH_PRIME;
    }

    return hash;
}

int main(void) {
    cpu_init();
    rec_init();

    if (read_chr_rom()) {
        rec_close(&recording);
        return 1;
    }

    Start();

    uint32_t cumulative_hash = HASH_OFFSET_BASIS;

    for (size_t i = 0; i < RECORDING_FRAME_COUNT; i++) {
        handle_inputs();
        next_frame();
        ppu_render();

        // update the cumulative hash
        uint32_t frame_hash = hash_frame(frame, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
        cumulative_hash ^= frame_hash;
        cumulative_hash *= HASH_PRIME;
        frame_counter++;
    }

    bool matches = cumulative_hash == EXPECTED_CUMULATIVE_HASH;

    printf("Cumulative hash: %u, expected: %u, matches: %s\n", cumulative_hash, EXPECTED_CUMULATIVE_HASH, matches ? "yes" : "no");

    return matches ? 0 : 1;
}
