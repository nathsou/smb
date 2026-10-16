#include "sound.h"

uint8_t sound_events[APU_FRAME_EVENTS_MAX * 2];
uint16_t sound_event_count;
uint32_t apu_write_hash = APU_WRITE_HASH_RESET;

void apu_write(uint16_t addr, uint8_t value) {
    apu_write_hash = (apu_write_hash ^ (addr & 0xff)) * 16777619u;
    apu_write_hash = (apu_write_hash ^ (addr >> 8)) * 16777619u;
    apu_write_hash = (apu_write_hash ^ value) * 16777619u;
    if (addr < 0x4000 || addr > 0x4017 || sound_event_count >= APU_FRAME_EVENTS_MAX) return;
    sound_events[2 * sound_event_count] = (uint8_t)(addr - 0x4000);
    sound_events[2 * sound_event_count + 1] = value;
    sound_event_count++;
}

void sound_end_frame(void) {
    apu_push_frame(sound_events, sound_event_count);
    sound_event_count = 0;
}
