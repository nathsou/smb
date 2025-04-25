#include "rec.h"

int rec_open(const char* filename, const char* mode, NESInputRecording* rec) {
    if (!rec || !filename || !mode) {
        return -1;
    }

    rec->file = fopen(filename, mode);
    if (!rec->file) {
        return -1;
    }

    rec->has_next = false;
    rec->current.frame = 0;
    rec->current.state = 0;

    // If opening for reading, try to read the first input
    if (mode[0] == 'r') {
        if (fscanf(rec->file, "%u:%hhu\n", &rec->next.frame, &rec->next.state) == 2) {
            rec->has_next = true;
        }
    }

    return 0;
}

void rec_close(NESInputRecording* rec) {
    if (rec && rec->file) {
        fclose(rec->file);
        rec->file = NULL;
        rec->has_next = false;
    }
}

int rec_write(NESInputRecording* rec, uint32_t frame, uint8_t state) {
    if (!rec || !rec->file) {
        return -1;
    }
    
    if (fprintf(rec->file, "%u:%u\n", frame, state) < 0) {
        return -1;
    }
    
    return 0;
}

uint8_t rec_state_at_frame(NESInputRecording* rec, uint32_t frame) {
    if (!rec || !rec->file) {
        return 0;
    }

    while (rec->has_next && rec->next.frame <= frame) {
        rec->current = rec->next;
        
        rec->has_next = (fscanf(rec->file, "%u:%hhu\n", 
                               &rec->next.frame, 
                               &rec->next.state) == 2);
    }

    return rec->current.state;
}
