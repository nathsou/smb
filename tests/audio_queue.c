// The synthesizer's frame queue (codegen/lib/apu.c) at its edges:
// - the game ahead of the audio output: no write is lost, and the backlog is
//   applied at once so latency stays bounded;
// - the game late: rendering goes on with the channels as they are.
#include "apu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000
#define FRAME (RATE / 60)

static uint8_t out[FRAME];

static void render_frames(int frames) {
    for (int i = 0; i < frames; i++) apu_render(out, FRAME);
}

int main(void) {
    // A note: pulse 1 at full volume, enabled.
    const uint8_t note[] = {0x15, 0x01, 0x00, 0xbf, 0x02, 0x80, 0x03, 0x01};
    apu_init(RATE);
    apu_push_frame(note, 4);
    render_frames(1);
    assert(apu_applied_writes == 4);

    // The game is late: frames without writes render, the note keeps sounding.
    render_frames(5);
    int changes = 0;
    for (int i = 1; i < FRAME; i++) changes += out[i] != out[i - 1];
    assert(changes > 0);

    // The game runs 20 frames ahead of the output (more than the queue holds).
    uint32_t before = apu_applied_writes;
    for (int i = 0; i < 20; i++) {
        uint8_t write[] = {0x02, (uint8_t)i};
        apu_push_frame(write, 1);
    }
    // Writes that did not fit wait for the game's next frame, which carries
    // them; everything arrives within a few frames of output, not 20.
    for (int i = 0; i < 4; i++) {
        render_frames(1);
        apu_push_frame(NULL, 0);
    }
    assert(apu_applied_writes - before == 20);

    // A frame with more writes than fit is cut at the maximum, not overflowed.
    static uint8_t many[2 * (APU_FRAME_EVENTS_MAX + 10)];
    memset(many, 0, sizeof many);
    before = apu_applied_writes;
    apu_push_frame(many, APU_FRAME_EVENTS_MAX + 10);
    render_frames(4); // behind the empty frames still queued
    assert(apu_applied_writes - before == APU_FRAME_EVENTS_MAX);

    puts("Audio queue checks passed");
}
