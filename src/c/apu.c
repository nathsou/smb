#include "apu.h"
#include "external.h"

#define BUFFER_SIZE 4096
#define BUFFER_MASK (BUFFER_SIZE - 1)
#define CPU_FREQUENCY 1789773
#define FRAME_RATE 60

typedef struct {
    bool enabled;
    uint8_t duty_mode;
    uint8_t duty_cycle;
    uint16_t timer;
    uint16_t timer_period;
    uint8_t length_counter;
    bool halt_length_counter;
    bool envelope_constant_mode;
    uint8_t envelope_constant_volume;
    bool envelope_loop;
    bool envelope_start;
    uint8_t envelope_period;
    uint8_t envelope_divider;
    uint8_t envelope_decay;
} Pulse;

size_t sample_rate;
uint8_t audio_buffer[BUFFER_SIZE];
size_t buffer_index;
Pulse pulse1, pulse2;
size_t frame_counter;

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

const uint8_t LENGTH_LOOKUP[] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30,
};

void pulse_init(Pulse* self) {
    self->enabled = false;
    self->duty_mode = 0;
    self->duty_cycle = 0;
    self->timer = 0;
    self->timer_period = 0;
    self->length_counter = 0;
    self->halt_length_counter = false;
    self->envelope_constant_mode = false;
    self->envelope_constant_volume = 0;
    self->envelope_loop = false;
    self->envelope_start = false;
    self->envelope_period = 0;
    self->envelope_divider = 0;
    self->envelope_decay = 0;
}

void pulse_step_timer(Pulse* self) {
    if (self->timer == 0) {
        self->timer = self->timer_period;
        self->duty_cycle = (self->duty_cycle + 1) & 7;
    } else {
        self->timer--;
    }
}

void pulse_step_length_counter(Pulse* self) {
    if (self->length_counter > 0 && !self->halt_length_counter) {
        self->length_counter--;
    }
}

void pulse_step_envelope(Pulse* self) {
    if (self->envelope_start) {
        self->envelope_start = false;
        self->envelope_decay = 15;
        self->envelope_divider = self->envelope_period;
    } else if (self->envelope_divider == 0) {
        if (self->envelope_decay > 0) {
            self->envelope_decay--;
        } else if (self->envelope_loop) {
            self->envelope_decay = 15;
        }

        self->envelope_divider = self->envelope_period;
    } else {
        self->envelope_divider--;
    }
}

void pulse_write_control(Pulse* self, uint8_t value) {
    self->duty_mode = (value >> 6) & 0b11;
    self->halt_length_counter = (value & 0b00100000) != 0;
    self->envelope_loop = self->halt_length_counter;
    self->envelope_constant_mode = (value & 0b00010000) != 0;
    self->envelope_period = value & 0b1111;
    self->envelope_constant_volume = value & 0b1111;
    self->envelope_start = true;
}

void pulse_write_reload_low(Pulse* self, uint8_t value) {
    self->timer_period = (uint16_t)((self->timer_period & 0xff00) | ((uint16_t)value));
}

void pulse_write_reload_high(Pulse* self, uint8_t value) {
    self->timer_period = (uint16_t)((self->timer_period & 0x00ff) | (((uint16_t)(value & 7)) << 8));
    self->duty_cycle = 0;
    self->envelope_start = true;
    self->length_counter = LENGTH_LOOKUP[value >> 3];
}

uint8_t pulse_output(Pulse* self) {
    if (
        !self->enabled ||
        self->timer_period < 8 ||
        self->timer_period > 0x7ff ||
        self->length_counter == 0 ||
        PULSE_DUTY_TABLE[self->duty_mode][self->duty_cycle] == 0
    ) {
        return 0;
    }

    if (self->envelope_constant_mode) {
        return self->envelope_constant_volume;
    } else {
        return self->envelope_decay;
    }
}

void apu_init(size_t frequency) {
    memset(audio_buffer, 0, BUFFER_SIZE);
    pulse_init(&pulse1);
    pulse_init(&pulse2);

    sample_rate = frequency;
    buffer_index = 0;
    frame_counter = 0;
}

uint8_t apu_get_sample(void) {
    // https://www.nesdev.org/wiki/APU_Mixer
    uint8_t p1 = pulse_output(&pulse1);
    uint8_t p2 = pulse_output(&pulse2);
    double pulse_out = PULSE_MIXER_LOOKUP[p1 + p2];

    return (uint8_t)(255.0 * pulse_out);
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

            if (!pulse1.enabled) {
                pulse1.length_counter = 0;
            }

            if (!pulse2.enabled) {
                pulse2.length_counter = 0;
            }
            break;
        }
        // Frame counter
        case 0x4017: {
            break;
        }
    }
}

void apu_step_timer(void) {
    pulse_step_timer(&pulse1);
    pulse_step_timer(&pulse2);
}

void apu_step_envelope(void) {
    pulse_step_envelope(&pulse1);
    pulse_step_envelope(&pulse2);
}

void apu_step_length_counter(void) {
    pulse_step_length_counter(&pulse1);
    pulse_step_length_counter(&pulse2);
}

const size_t CYCLES_PER_FRAME = CPU_FREQUENCY / FRAME_RATE;

void apu_step_frame(void) {
    // step the frame sequencer 4 times per frame
    // https://www.nesdev.org/wiki/APU_Frame_Counter
    size_t samples_per_quarter_frame = sample_rate / (4 * FRAME_RATE);

    for (size_t i = 0; i < 4; i++) {
        frame_counter = (frame_counter + 1) % 5; // TODO: confirm that 4-step sequence is never used

        apu_step_envelope();

        if (frame_counter & 1) {
            apu_step_length_counter();
        }

        size_t samples_to_write = samples_per_quarter_frame;
        size_t samples_written = 0;

        // on the final step of the sequencer, output the remaining samples for this frame
        if (i == 3) {
            samples_to_write = (sample_rate / FRAME_RATE) - 3 * samples_per_quarter_frame;
        }

        for (size_t i = 0; i < (CYCLES_PER_FRAME / 4); i++) {
            double progress_counter = (double)i / (double)(CYCLES_PER_FRAME / 4);
            double progress_samples = (double)samples_written / (double)samples_to_write;

            if (samples_written < samples_to_write && progress_counter > progress_samples) {
                buffer_index = (buffer_index + 1) & BUFFER_MASK;
                audio_buffer[buffer_index] = apu_get_sample();
                samples_written++;
            }

            apu_step_timer();
        }
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
