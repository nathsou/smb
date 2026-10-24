#ifndef SMB_RECORDING_H
#define SMB_RECORDING_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t frame;
    uint8_t state;
} NESInput;

typedef struct {
    FILE* file;
    bool has_next;
    NESInput next;
    NESInput current;
} NESInputRecording;

int rec_open(const char* filename, const char* mode, NESInputRecording* rec);

void rec_close(NESInputRecording* rec);

int rec_write(NESInputRecording* rec, uint32_t frame, uint8_t state);

uint8_t rec_state_at_frame(NESInputRecording* rec, uint32_t frame);

// The frames up to the last one a recording names.
uint32_t rec_length(const char* filename);

// Applies the pokes of `list` due at `frame` ("FRAME:ADDR=VALUE,...", the
// address and value in hex) through `write`: to start a recording where a
// game would take long to reach.
void rec_pokes(const char* list, uint32_t frame, void (*write)(uint16_t addr, uint8_t value));

#endif
