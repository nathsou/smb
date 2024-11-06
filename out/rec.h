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

#endif
