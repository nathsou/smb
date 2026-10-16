// Buffers through which JavaScript drives the synthesizer (apu.h): the web
// page's AudioWorklet runs it as its own WebAssembly module (web/apu.wasm),
// and the WebAssembly replay test renders from the runtime module.
#include "apu.h"

uint8_t apu_input[APU_FRAME_EVENTS_MAX * 2];
float apu_output[4096];
