#include "apu.h"
#include "external.h"
#include <math.h>

#define BUFFER_SIZE 4096

typedef struct {
    bool enabled;
    uint8_t duty_mode;
    uint8_t duty_cycle;
    uint16_t timer;
    uint16_t timer_period;
} Pulse;

size_t sample_rate;
uint8_t audio_buffer[BUFFER_SIZE];
size_t buffer_index;
Pulse pulse1, pulse2;

const double PULSE_MIXER_LOOKUP[] = {
    0.0, 0.011609139, 0.02293948, 0.034000948,
    0.044803, 0.05535466, 0.06566453, 0.07574082,
    0.0855914, 0.09522375, 0.10464504, 0.11386215,
    0.12288164, 0.1317098, 0.14035264, 0.14881596,
    0.15710525, 0.16522588, 0.17318292, 0.18098126,
    0.18862559, 0.19612046, 0.20347017, 0.21067894,
    0.21775076, 0.2246895, 0.23149887, 0.23818247,
    0.24474378, 0.25118607, 0.25751257, 0.26372638,
};

const uint8_t PULSE_DUTY_TABLE[][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 },
};

void pulse_init(Pulse* self) {
    self->enabled = false;
    self->duty_mode = 0;
    self->duty_cycle = 0;
    self->timer = 0;
    self->timer_period = 0;
}

void pulse_step_timer(Pulse* self) {
    if (self->timer == 0) {
        self->timer = self->timer_period;
        self->duty_cycle = (self->duty_cycle + 1) & 7;
    } else {
        self->timer--;
    }
}

void pulse_write_control(Pulse* self, uint8_t value) {
    self->duty_mode = (value >> 6) & 0b11;
}

void pulse_write_reload_low(Pulse* self, uint8_t value) {
    self->timer_period = (uint16_t)((self->timer_period & 0xff00) | ((uint16_t)value));
}

void pulse_write_reload_high(Pulse* self, uint8_t value) {
    self->timer_period = (uint16_t)((self->timer_period & 0x00ff) | (((uint16_t)(value & 7)) << 8));
    self->duty_cycle = 0;
}

uint8_t pulse_output(Pulse* self) {
    if (!self->enabled) {
        return 0;
    }

    return PULSE_DUTY_TABLE[self->duty_mode][self->duty_cycle];
}

void apu_init(size_t frequency) {
    memset(audio_buffer, 0, BUFFER_SIZE);
    pulse_init(&pulse1);
    pulse_init(&pulse2);

    sample_rate = frequency;
    buffer_index = 0;
}

uint8_t apu_get_sample(void) {
    uint8_t p1 = pulse_output(&pulse1);
    uint8_t p2 = pulse_output(&pulse2);
    double pulse_out = PULSE_MIXER_LOOKUP[p1 + p2];

    return (uint8_t)(floor(255.0 * pulse_out));;
}

void apu_write(uint16_t addr, uint8_t value) {
    switch (addr) {
        // Pulse 1
        case 0x4000: {
            pulse_write_control(&pulse1, value);
            break;
        }
        case 0x4001: break;
        case 0x4002: {
            pulse_write_reload_low(&pulse1, value);
            break;
        }
        case 0x4003: {
            pulse_write_reload_high(&pulse1, value);
            break;
        }
        // Pulse 2
        case 0x4004: {
            pulse_write_control(&pulse2, value);
            break;
        }
        case 0x4005: break;
        case 0x4006: {
            pulse_write_reload_low(&pulse2, value);
            break;
        }
        case 0x4007: {
            pulse_write_reload_high(&pulse2, value);
            break;
        }
        // Control
        case 0x4015: {
            pulse1.enabled = (value & 1) != 0;
            pulse2.enabled = (value & 2) != 0;
            break;
        }
    }
}

const size_t CYCLES_PER_FRAME = 1789773 / 60;

void apu_step_frame(void) {
    size_t samples_per_frame = sample_rate / 60;
    size_t samples_written = 0;

    for (size_t i = 0; i < CYCLES_PER_FRAME; i++) {
        double progress_counter = (double)i / (double)CYCLES_PER_FRAME;
        double progress_samples = (double)samples_written / (double)samples_per_frame;

        if (samples_written < samples_per_frame && progress_counter > progress_samples) {
            if (buffer_index < BUFFER_SIZE)  {
                audio_buffer[buffer_index++] = apu_get_sample();
            } else {
                // printf("Buffer overflow\n");
            }

            samples_written++;
        }

        pulse_step_timer(&pulse1);
        pulse_step_timer(&pulse2);
    }
}

void apu_fill_buffer(uint8_t* cb_buffer, size_t size) {
    size_t len = size > buffer_index ? buffer_index : size;
    
    if (len > 0) {
        if (size > buffer_index) {
            memcpy(cb_buffer, audio_buffer, buffer_index);
            buffer_index = 0;
        } else {
            memcpy(cb_buffer, audio_buffer, len);
            // memmove(audio_buffer, audio_buffer + len, buffer_index - len);
            memcpy(audio_buffer, audio_buffer + len, buffer_index - len);
            buffer_index -= len;
        }
    }
}
