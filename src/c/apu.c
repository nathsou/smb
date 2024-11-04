#include "apu.h"
#include "external.h"

#define BUFFER_MASK (AUDIO_BUFFER_SIZE - 1)
#define CPU_FREQUENCY 1789773
#define FRAME_RATE 60

// Length Counter

static const uint8_t LENGTH_LOOKUP[] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30,
};

typedef struct {
    bool enabled;
    uint8_t counter;
} LengthCounter;

void length_counter_init(LengthCounter* lc) {
    lc->enabled = false;
    lc->counter = 0;
}

inline void length_counter_reset_to_zero(LengthCounter* lc) {
    lc->counter = 0;
}

inline void length_counter_step(LengthCounter* lc) {
    if (lc->enabled && lc->counter > 0) {
        lc->counter--;
    }
}

inline void length_counter_set(LengthCounter* lc, uint8_t val) {
    lc->counter = LENGTH_LOOKUP[val];
}

// Set enabled status
inline void length_counter_set_enabled(LengthCounter* lc, bool enabled) {
    lc->enabled = enabled;
}

// Check if counter is zero
inline bool length_counter_is_zero(const LengthCounter* lc) {
    return lc->counter == 0;
}

// Pulse

typedef struct {
    bool enabled;
    uint8_t duty_mode;
    uint8_t duty_cycle;
    uint16_t timer;
    uint16_t timer_period;
    LengthCounter length_counter;
    bool envelope_constant_mode;
    uint8_t envelope_constant_volume;
    bool envelope_loop;
    bool envelope_start;
    uint8_t envelope_period;
    uint8_t envelope_divider;
    uint8_t envelope_decay;
    bool sweep_enabled;
    uint8_t sweep_period;
    bool sweep_negate;
    uint8_t sweep_shift;
    bool sweep_reload;
    uint8_t sweep_divider;
    bool sweep_mute;
} Pulse;

static const double PULSE_MIXER_LOOKUP[] = {
    0.0, 0.011609139, 0.02293948, 0.034000948,
    0.044803, 0.05535466, 0.06566453, 0.07574082,
    0.0855914, 0.09522375, 0.10464504, 0.11386215,
    0.12288164, 0.1317098, 0.14035264, 0.14881596,
    0.15710525, 0.16522588, 0.17318292, 0.18098126,
    0.18862559, 0.19612046, 0.20347017, 0.21067894,
    0.21775076, 0.2246895, 0.23149887, 0.23818247,
    0.24474378, 0.25118607, 0.25751257, 0.26372638,
};

static const uint8_t PULSE_DUTY_TABLE[][8] = {
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
    length_counter_init(&self->length_counter);
    self->envelope_constant_mode = false;
    self->envelope_constant_volume = 0;
    self->envelope_loop = false;
    self->envelope_start = false;
    self->envelope_period = 0;
    self->envelope_divider = 0;
    self->envelope_decay = 0;
    self->sweep_enabled = false;
    self->sweep_period = 0;
    self->sweep_negate = false;
    self->sweep_shift = 0;
    self->sweep_reload = false;
    self->sweep_divider = 0;
    self->sweep_mute = false;
}

void pulse_set_enabled(Pulse* self, bool enabled) {
    self->enabled = enabled;

    if (!enabled) {
        length_counter_reset_to_zero(&self->length_counter);
    }
}

void pulse_step_timer(Pulse* self) {
    if (self->timer == 0) {
        self->timer = self->timer_period;
        self->duty_cycle = (self->duty_cycle + 1) & 7;
    } else {
        self->timer--;
    }
}

inline void pulse_step_length_counter(Pulse* self) {
    length_counter_step(&self->length_counter);
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
    bool halt_length_counter = (value & 0b00100000) != 0;
    length_counter_set_enabled(&self->length_counter, !halt_length_counter);
    self->envelope_loop = halt_length_counter;
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
    length_counter_set(&self->length_counter, value >> 3);
}

void pulse_write_sweep(Pulse* self, uint8_t value) {
    self->sweep_enabled = (value & 0b10000000) != 0;
    self->sweep_period = (value >> 4) & 0b111;
    self->sweep_negate = (value & 0b1000) != 0;
    self->sweep_shift = value & 0b111;
    self->sweep_reload = true;
}

uint16_t pulse_sweep_target_period(Pulse* self) {
    uint16_t change_amount = self->timer_period >> self->sweep_shift;

    if (self->sweep_negate) {
        if (change_amount > self->timer_period) {
            return 0;
        } else {
            return self->timer_period - change_amount;
        }
    } else {
        return self->timer_period + change_amount;
    }
}

void pulse_step_sweep(Pulse* self) {
    uint16_t target_period = pulse_sweep_target_period(self);
    self->sweep_mute = self->timer_period < 8 || target_period > 0x7ff;

    if (self->sweep_divider == 0 && self->sweep_enabled && !self->sweep_mute) {
        self->timer_period = target_period;
    }

    if (self->sweep_divider == 0 || self->sweep_reload) {
        self->sweep_divider = self->sweep_period;
        self->sweep_reload = false;
    } else {
        self->sweep_divider--;
    }
}

uint8_t pulse_output(Pulse* self) {
    if (
        !self->enabled ||
        self->sweep_mute ||
        length_counter_is_zero(&self->length_counter) ||
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

// Triangle

static const uint8_t SEQUENCER_LOOKUP[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

static const double TRIANGLE_MIXER_LOOKUP[] = {
    0.0, 0.006699824, 0.01334502, 0.019936256, 0.02647418, 0.032959443, 0.039392676, 0.0457745, 
    0.052105535, 0.05838638, 0.064617634, 0.07079987, 0.07693369, 0.08301962, 0.08905826, 0.095050134, 
    0.100995794, 0.10689577, 0.11275058, 0.118560754, 0.12432679, 0.13004918, 0.13572845, 0.14136505, 
    0.1469595, 0.15251222, 0.1580237, 0.1634944, 0.16892476, 0.17431524, 0.17966628, 0.1849783, 
    0.19025174, 0.19548698, 0.20068447, 0.20584463, 0.21096781, 0.21605444, 0.22110492, 0.2261196, 
    0.23109888, 0.23604311, 0.24095272, 0.245828, 0.25066936, 0.2554771, 0.26025164, 0.26499328, 
    0.26970237, 0.27437922, 0.27902418, 0.28363758, 0.28821972, 0.29277095, 0.29729152, 0.3017818, 
    0.3062421, 0.31067267, 0.31507385, 0.31944588, 0.32378912, 0.32810378, 0.3323902, 0.3366486, 
    0.3408793, 0.34508255, 0.34925863, 0.35340777, 0.35753027, 0.36162636, 0.36569634, 0.36974037, 
    0.37375876, 0.37775174, 0.38171956, 0.38566244, 0.38958064, 0.39347437, 0.39734384, 0.4011893, 
    0.405011, 0.40880907, 0.41258383, 0.41633546, 0.42006415, 0.42377013, 0.4274536, 0.43111476, 
    0.43475384, 0.43837097, 0.44196644, 0.4455404, 0.449093, 0.45262453, 0.45613506, 0.4596249, 
    0.46309412, 0.46654293, 0.46997157, 0.47338015, 0.47676894, 0.48013794, 0.48348752, 0.4868177, 
    0.49012873, 0.4934207, 0.49669388, 0.49994832, 0.50318426, 0.50640184, 0.5096012, 0.51278245, 
    0.51594585, 0.5190914, 0.5222195, 0.52533007, 0.52842325, 0.5314993, 0.53455836, 0.5376005, 
    0.54062593, 0.5436348, 0.54662704, 0.54960304, 0.55256283, 0.55550647, 0.5584343, 0.56134623, 
    0.5642425, 0.56712323, 0.5699885, 0.5728384, 0.5756732, 0.57849294, 0.5812977, 0.5840876, 
    0.5868628, 0.58962345, 0.59236956, 0.59510136, 0.5978189, 0.6005223, 0.6032116, 0.605887, 
    0.60854864, 0.6111966, 0.6138308, 0.61645156, 0.619059, 0.62165314, 0.624234, 0.62680185, 
    0.6293567, 0.63189864, 0.6344277, 0.6369442, 0.63944805, 0.64193934, 0.64441824, 0.64688486, 
    0.6493392, 0.6517814, 0.6542115, 0.65662974, 0.65903604, 0.6614306, 0.6638134, 0.66618466, 
    0.66854435, 0.6708926, 0.67322946, 0.67555505, 0.67786944, 0.68017274, 0.68246496, 0.6847462, 
    0.6870166, 0.6892762, 0.69152504, 0.6937633, 0.6959909, 0.69820803, 0.7004148, 0.7026111, 
    0.7047972, 0.7069731, 0.7091388, 0.7112945, 0.7134401, 0.7155759, 0.7177018, 0.7198179, 
    0.72192425, 0.72402096, 0.726108, 0.72818565, 0.7302538, 0.73231256, 0.73436195, 0.7364021, 
    0.7384331, 0.7404549, 0.7424676, 0.7444713, 
};

typedef struct {
    bool enabled;
    bool control_flag;
    uint8_t counter_reload;
    uint16_t timer_period;
    uint16_t timer;
    LengthCounter length_counter;
    uint8_t linear_counter;
    bool linear_counter_reload;
    uint8_t duty_cycle;
} Triangle;

void triangle_init(Triangle* tc) {
    tc->enabled = false;
    tc->control_flag = false;
    tc->counter_reload = 0;
    tc->timer_period = 0;
    tc->timer = 0;
    length_counter_init(&tc->length_counter);
    tc->linear_counter = 0;
    tc->linear_counter_reload = false;
    tc->duty_cycle = 0;
}

void triangle_write_setup(Triangle* tc, uint8_t val) {
    tc->control_flag = (val & 0x80) != 0;
    tc->counter_reload = val & 0x7F;
}

void triangle_write_timer_low(Triangle* tc, uint8_t val) {
    tc->timer_period = (tc->timer_period & 0xFF00) | val;
}

void triangle_write_timer_high(Triangle* tc, uint8_t val) {
    tc->timer_period = (tc->timer_period & 0x00FF) | (uint16_t)((uint16_t)(val & 0x07) << 8);
    tc->timer = tc->timer_period;
    length_counter_set(&tc->length_counter, val >> 3);
    tc->linear_counter_reload = true;
}

void triangle_step_linear_counter(Triangle* tc) {
    if (tc->linear_counter_reload) {
        tc->linear_counter = tc->counter_reload;
    } else if (tc->linear_counter > 0) {
        tc->linear_counter--;
    }
    
    if (!tc->control_flag) {
        tc->linear_counter_reload = false;
    }
}

inline void triangle_step_length_counter(Triangle* tc) {
    length_counter_step(&tc->length_counter);
}

void triangle_step_timer(Triangle* tc) {
    if (tc->timer == 0) {
        tc->timer = tc->timer_period;
        if (tc->linear_counter > 0 && !length_counter_is_zero(&tc->length_counter)) {
            tc->duty_cycle = (tc->duty_cycle + 1) & 31;
        }
    } else {
        tc->timer--;
    }
}

void triangle_set_enabled(Triangle* tc, bool enabled) {
    tc->enabled = enabled;
    if (!enabled) {
        length_counter_reset_to_zero(&tc->length_counter);
    }
}

uint8_t triangle_output(const Triangle* tc) {
    if (!tc->enabled || 
        length_counter_is_zero(&tc->length_counter) ||
        tc->linear_counter == 0 ||
        tc->timer_period <= 2) {
        return 0;
    }
    return SEQUENCER_LOOKUP[tc->duty_cycle & 0x1F];
}

// APU

size_t sample_rate;
uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
uint8_t web_audio_buffer[AUDIO_BUFFER_SIZE];
uint16_t audio_buffer_index;
Pulse pulse1, pulse2;
Triangle triangle;
size_t frame_counter;
uint16_t audio_buffer_size = AUDIO_BUFFER_SIZE;

void apu_init(size_t frequency) {
    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);
    pulse_init(&pulse1);
    pulse_init(&pulse2);
    triangle_init(&triangle);

    sample_rate = frequency;
    audio_buffer_index = 0;
    frame_counter = 0;
}

uint8_t apu_get_sample(void) {
    // https://www.nesdev.org/wiki/APU_Mixer
    uint8_t p1 = pulse_output(&pulse1);
    uint8_t p2 = pulse_output(&pulse2);
    uint8_t t = triangle_output(&triangle);
    double pulse_out = PULSE_MIXER_LOOKUP[p1 + p2];
    double triangle_out = TRIANGLE_MIXER_LOOKUP[t * 3];

    return (uint8_t)(255.0 * (pulse_out + triangle_out));
}

void apu_write(uint16_t addr, uint8_t value) {
    switch (addr) {
        // Pulse 1
        case 0x4000: {
            pulse_write_control(&pulse1, value);
            break;
        }
        case 0x4001: {
            pulse_write_sweep(&pulse1, value);
            break;
        }
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
        case 0x4005: {
            pulse_write_sweep(&pulse2, value);
            break;
        }
        case 0x4006: {
            pulse_write_reload_low(&pulse2, value);
            break;
        }
        case 0x4007: {
            pulse_write_reload_high(&pulse2, value);
            break;
        }
        // Triangle
        case 0x4008: {
            triangle_write_setup(&triangle, value);
            break;
        }
        case 0x4009: {
            break;
        }
        case 0x400A: {
            triangle_write_timer_low(&triangle, value);
            break;
        }
        case 0x400B: {
            triangle_write_timer_high(&triangle, value);
            break;
        }
        // Control
        case 0x4015: {
            pulse_set_enabled(&pulse1, (value & 1) != 0);
            pulse_set_enabled(&pulse2, (value & 2) != 0);
            triangle_set_enabled(&triangle, (value & 4) != 0);
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
    triangle_step_timer(&triangle);
}

void apu_step_envelope(void) {
    pulse_step_envelope(&pulse1);
    pulse_step_envelope(&pulse2);
    triangle_step_linear_counter(&triangle);
}

void apu_step_length_counter(void) {
    pulse_step_length_counter(&pulse1);
    pulse_step_length_counter(&pulse2);
    triangle_step_length_counter(&triangle);
}

void apu_step_sweep(void) {
    pulse_step_sweep(&pulse1);
    pulse_step_sweep(&pulse2);
}

const size_t CYCLES_PER_FRAME = CPU_FREQUENCY / FRAME_RATE;

void apu_step_frame(void) {
    // step the frame sequencer 4 times per frame
    // https://www.nesdev.org/wiki/APU_Frame_Counter
    size_t samples_per_quarter_frame = sample_rate / (4 * FRAME_RATE);

    for (size_t i = 0; i < 4; i++) {
        frame_counter = (frame_counter + 1) % 5; // TODO: confirm that 4-step sequence is never used


        if (frame_counter & 1) {
            apu_step_envelope();
        } else {
            apu_step_length_counter();
            apu_step_envelope();
            apu_step_sweep();
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
                audio_buffer_index = (audio_buffer_index + 1) & BUFFER_MASK;
                audio_buffer[audio_buffer_index] = apu_get_sample();
                samples_written++;
            }

            apu_step_timer();
        }
    }
}

void apu_fill_buffer(uint8_t* cb_buffer, size_t size) {
    size_t len = size > audio_buffer_index ? audio_buffer_index : size;

    if (size > audio_buffer_index) {
        memcpy(cb_buffer, audio_buffer, audio_buffer_index);
        audio_buffer_index = 0;
    } else {
        memcpy(cb_buffer, audio_buffer, len);
        memcpy(audio_buffer, audio_buffer + len, audio_buffer_index - len);
        audio_buffer_index -= len;
    }
}
