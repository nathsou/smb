#ifndef SMB_APU_H
#define SMB_APU_H

// The NES sound hardware as a synthesizer, fed with the game's register
// writes one frame at a time.
//
// The game side (sound.c) collects each frame's writes; the frame is handed
// over with apu_push_frame, and apu_render, called by the audio output (the
// raylib callback, the web page's AudioWorklet, or the replay test),
// applies it at the next frame boundary in sample time (every sample_rate / 60
// samples) and synthesises. One producer and one consumer may run on
// different threads.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Most register writes a frame may carry (SMB makes a few dozen).
#define APU_FRAME_EVENTS_MAX 256

void apu_init(size_t sample_rate);
// events: (register - $4000, value) pairs
void apu_push_frame(const uint8_t *events, size_t count);
// Samples in [-1, 1].
void apu_render(float *out, size_t count);
// Register writes applied so far (tests).
extern uint32_t apu_applied_writes;

#endif
