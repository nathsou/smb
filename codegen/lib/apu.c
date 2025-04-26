#include "apu.h"
#include "external.h"
#define PI 3.14159265358979323846

#define CPU_FREQUENCY 1789773
#define FRAME_RATE 60

// Length Counter

const uint8_t LENGTH_LOOKUP[] = {
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

// Timer

typedef struct {
    uint16_t counter;
    uint16_t period;
} Timer;

void timer_init(Timer* self) {
    self->counter = 0;
    self->period = 0;
}

inline bool timer_step(Timer* self) {
    if (self->counter == 0) {
        self->counter = self->period;
        return true;
    } else {
        self->counter -= 1;
        return false;
    }
}

// Envelope

typedef struct {
    bool constant_mode;
    bool looping;
    bool start;
    uint8_t constant_volume;
    uint8_t period;
    uint8_t divider;
    uint8_t decay;
} Envelope;

void envelope_init(Envelope* self) {
    self->constant_mode = false;
    self->looping = false;
    self->start = false;
    self->constant_volume = 0;
    self->period = 0;
    self->divider = 0;
    self->decay = 0;
}

void envelope_step(Envelope* self) {
    if (self->start) {
        self->start = false;
        self->decay = 15;
        self->divider = self->period;
    } else if (self->divider == 0) {
        if (self->decay > 0) {
            self->decay -= 1;
        } else if (self->looping) {
            self->decay = 15;
        }
        self->divider = self->period;
    } else {
        self->divider -= 1;
    }
}

inline uint8_t envelope_output(const Envelope* self) {
    return self->constant_mode ? self->constant_volume : self->decay;
}

// Pulse

typedef struct {
    bool enabled;
    uint8_t duty_mode;
    uint8_t duty_cycle;
    Timer timer;
    LengthCounter length_counter;
    Envelope envelope;
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
    timer_init(&self->timer);
    length_counter_init(&self->length_counter);
    envelope_init(&self->envelope);
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
    if (timer_step(&self->timer)) {
        self->duty_cycle = (self->duty_cycle + 1) & 7;
    }
}

inline void pulse_step_length_counter(Pulse* self) {
    length_counter_step(&self->length_counter);
}

inline void pulse_step_envelope(Pulse* self) {
    envelope_step(&self->envelope);
}

void pulse_write_control(Pulse* self, uint8_t value) {
    self->duty_mode = (value >> 6) & 0b11;
    bool halt_length_counter = (value & 0b00100000) != 0;
    length_counter_set_enabled(&self->length_counter, !halt_length_counter);
    self->envelope.looping = halt_length_counter;
    self->envelope.constant_mode = (value & 0b00010000) != 0;
    self->envelope.period = value & 0b1111;
    self->envelope.constant_volume = value & 0b1111;
    self->envelope.start = true;
}

void pulse_write_reload_low(Pulse* self, uint8_t value) {
    self->timer.period = (uint16_t)((self->timer.period & 0xff00) | ((uint16_t)value));
}

void pulse_write_reload_high(Pulse* self, uint8_t value) {
    self->timer.period = (uint16_t)((self->timer.period & 0x00ff) | (((uint16_t)(value & 7)) << 8));
    self->duty_cycle = 0;
    self->envelope.start = true;
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
    uint16_t change_amount = self->timer.period >> self->sweep_shift;

    if (self->sweep_negate) {
        if (change_amount > self->timer.period) {
            return 0;
        } else {
            return self->timer.period - change_amount;
        }
    } else {
        return self->timer.period + change_amount;
    }
}

void pulse_step_sweep(Pulse* self) {
    uint16_t target_period = pulse_sweep_target_period(self);
    self->sweep_mute = self->timer.period < 8 || target_period > 0x7ff;

    if (self->sweep_divider == 0 && self->sweep_enabled && !self->sweep_mute) {
        self->timer.period = target_period;
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

    return envelope_output(&self->envelope);
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
    Timer timer;
    LengthCounter length_counter;
    uint8_t linear_counter;
    bool linear_counter_reload;
    uint8_t duty_cycle;
} Triangle;

void triangle_init(Triangle* tc) {
    tc->enabled = false;
    tc->control_flag = false;
    tc->counter_reload = 0;
    timer_init(&tc->timer);
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
    tc->timer.period = (tc->timer.period & 0xFF00) | val;
}

void triangle_write_timer_high(Triangle* tc, uint8_t val) {
    tc->timer.period = (tc->timer.period & 0x00FF) | (uint16_t)((uint16_t)(val & 0x07) << 8);
    tc->timer.counter = tc->timer.period;
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
    if (timer_step(&tc->timer) && tc->linear_counter > 0 && !length_counter_is_zero(&tc->length_counter)) {
        tc->duty_cycle = (tc->duty_cycle + 1) & 31;
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
        tc->timer.period <= 2) {
        return 0;
    }
    return SEQUENCER_LOOKUP[tc->duty_cycle & 0x1F];
}

// Noise

static const uint16_t NOISE_PERIOD_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

typedef struct {
    bool enabled;
    LengthCounter length_counter;
    Envelope envelope;
    Timer timer;
    uint16_t shift_register;
    bool mode;
} Noise;

void noise_init(Noise* self) {
    self->enabled = false;
    self->length_counter.counter = 0;
    self->envelope.constant_mode = false;
    self->envelope.looping = false;
    self->envelope.start = false;
    self->envelope.constant_volume = 0;
    self->envelope.period = 0;
    self->envelope.divider = 0;
    self->envelope.decay = 0;
    self->timer.counter = 0;
    self->timer.period = 0;
    self->shift_register = 1;
    self->mode = false;
}

void noise_set_enabled(Noise* self, bool enabled) {
    self->enabled = enabled;
    if (!enabled) {
        self->length_counter.counter = 0;
    }
}

void noise_step_timer(Noise* self) {
    if (timer_step(&self->timer)) {
        uint8_t bit = self->mode ? 6 : 1;
        uint16_t bit0 = self->shift_register & 1;
        uint16_t other_bit = (self->shift_register >> bit) & 1;
        uint16_t feedback = bit0 ^ other_bit;
        self->shift_register >>= 1;
        self->shift_register |= feedback << 14;
    }
}

void noise_step_length_counter(Noise* self) {
    if (self->length_counter.counter > 0) {
        self->length_counter.counter--;
    }
}

void noise_step_envelope(Noise* self) {
    envelope_step(&self->envelope);
}

void noise_write_control(Noise* self, uint8_t val) {
    bool halt_length_counter = (val & 0x20) != 0;
    self->length_counter.counter = halt_length_counter ? 0 : self->length_counter.counter;
    self->envelope.looping = halt_length_counter;
    self->envelope.constant_mode = (val & 0x10) != 0;
    self->envelope.period = val & 0x0F;
    self->envelope.constant_volume = val & 0x0F;
}

void noise_write_period(Noise* self, uint8_t val) {
    self->mode = (val & 0x80) != 0;
    self->timer.period = NOISE_PERIOD_TABLE[val & 0x0F];
}

void noise_write_length(Noise* self, uint8_t val) {
    self->length_counter.counter = val >> 3;
    self->envelope.start = true;
}

uint8_t noise_output(const Noise* self) {
    if ((self->shift_register & 1) == 1 || self->length_counter.counter == 0) {
        return 0;
    } else {
        return envelope_output(&self->envelope);
    }
}

// DMC

static const uint16_t DELTA_MODULATION_RATES[] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54,
};

typedef struct {
    bool enabled;
    bool interrupt_flag;
    bool loop_flag;
    Timer timer;
    uint8_t output_level;
    uint16_t sample_addr;
    uint16_t sample_len;
    uint16_t current_addr;
    uint16_t bytes_remaining;
    uint8_t shift_register;
    bool silence_flag;
    uint8_t output_bits_remaining;
    bool irq_enabled;
    uint32_t cpu_stall;
    uint16_t memory_read_request;
    bool has_memory_request;
} DeltaModulationChannel;

void dmc_init(DeltaModulationChannel* dmc) {
    dmc->enabled = false;
    dmc->interrupt_flag = false;
    dmc->loop_flag = false;
    timer_init(&dmc->timer);
    dmc->output_level = 0;
    dmc->sample_addr = 0;
    dmc->sample_len = 0;
    dmc->current_addr = 0;
    dmc->bytes_remaining = 0;
    dmc->shift_register = 0;
    dmc->silence_flag = false;
    dmc->output_bits_remaining = 0;
    dmc->irq_enabled = false;
    dmc->cpu_stall = 0;
    dmc->has_memory_request = false;
}

void dmc_restart(DeltaModulationChannel* dmc) {
    dmc->current_addr = dmc->sample_addr;
    dmc->bytes_remaining = dmc->sample_len;
}

void dmc_step_shifter(DeltaModulationChannel* dmc) {
    if (dmc->output_bits_remaining != 0) {
        if (!dmc->silence_flag) {
            if (dmc->shift_register & 1) {
                if (dmc->output_level <= 125) {
                    dmc->output_level += 2;
                }
            } else {
                if (dmc->output_level >= 2) {
                    dmc->output_level -= 2;
                }
            }
        }
        dmc->shift_register >>= 1;
        dmc->output_bits_remaining--;
    }
}

void dmc_step_reader(DeltaModulationChannel* dmc) {
    if (dmc->output_bits_remaining == 0 && dmc->bytes_remaining > 0) {
        dmc->cpu_stall += 4;
        dmc->memory_read_request = dmc->current_addr;
        dmc->has_memory_request = true;
        dmc->output_bits_remaining = 8;

        dmc->current_addr = (dmc->current_addr == 0xFFFF) ? 0x8000 : dmc->current_addr + 1;
        dmc->bytes_remaining--;

        if (dmc->bytes_remaining == 0) {
            if (dmc->loop_flag) {
                dmc_restart(dmc);
            } else if (dmc->irq_enabled) {
                dmc->interrupt_flag = true;
            }
        }
    }
}

void dmc_step_timer(DeltaModulationChannel* dmc) {
    if (dmc->enabled) {
        dmc_step_reader(dmc);

        if (!dmc->has_memory_request && timer_step(&dmc->timer)) {
            dmc_step_shifter(dmc);
        }
    }
}

void dmc_write_control(DeltaModulationChannel* dmc, uint8_t val) {
    dmc->irq_enabled = (val & 0x80) != 0;
    dmc->loop_flag = (val & 0x40) != 0;
    dmc->timer.period = DELTA_MODULATION_RATES[val & 0x0F];
}

inline void dmc_write_output(DeltaModulationChannel* dmc, uint8_t val) {
    dmc->output_level = val & 0x7F;
}

inline void dmc_write_sample_addr(DeltaModulationChannel* dmc, uint8_t val) {
    dmc->sample_addr = 0xC000 | (uint16_t)((uint16_t)val << 6);
}

inline void dmc_write_sample_len(DeltaModulationChannel* dmc, uint8_t val) {
    dmc->sample_len = (uint16_t)((uint16_t)val << 4) | 1;
}

inline void dmc_set_memory_read_response(DeltaModulationChannel* dmc, uint8_t val) {
    dmc->shift_register = val;
    dmc->has_memory_request = false;
    if (timer_step(&dmc->timer)) {
        dmc_step_shifter(dmc);
    }
}

inline bool dmc_is_active(const DeltaModulationChannel* dmc) {
    return dmc->bytes_remaining > 0;
}

inline void dmc_clear_interrupt_flag(DeltaModulationChannel* dmc) {
    dmc->interrupt_flag = false;
}

void dmc_set_enabled(DeltaModulationChannel* dmc, bool enabled) {
    dmc->enabled = enabled;
    if (!enabled) {
        dmc->bytes_remaining = 0;
    } else if (dmc->bytes_remaining == 0) {
        dmc_restart(dmc);
    }
}

inline uint8_t dmc_output(const DeltaModulationChannel* dmc) {
    return dmc->output_level;
}

// Filters

typedef struct {
    double b0;
    double b1;
    double a1;
    double prev_x;
    double prev_y;
} Filter;

void filter_init_low_pass(Filter* f, size_t sample_rate, double cutoff) {
    double c = (double)sample_rate / (cutoff * PI);
    double a0 = 1.0 / (1.0 + c);

    f->b0 = a0;
    f->b1 = a0;
    f->a1 = (1.0 - c) * a0;
    f->prev_x = 0.0;
    f->prev_y = 0.0;
}

void filter_init_high_pass(Filter* f, size_t sample_rate, double cutoff) {
    double c = (double)sample_rate / (cutoff * PI);
    double a0 = 1.0 / (1.0 + c);

    f->b0 = c * a0;
    f->b1 = -c * a0;
    f->a1 = (1.0 - c) * a0;
    f->prev_x = 0.0;
    f->prev_y = 0.0;
}

double filter_output(Filter* f, double x) {
    double y = f->b0 * x + f->b1 * f->prev_x - f->a1 * f->prev_y;
    f->prev_x = x;
    f->prev_y = y;

    return y;
}

// APU

size_t sample_rate;
uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
uint8_t web_audio_buffer[AUDIO_BUFFER_SIZE];
uint16_t audio_buffer_index;
Pulse pulse1, pulse2;
Triangle triangle;
Noise noise;
DeltaModulationChannel dmc;
Filter filter1, filter2, filter3;
size_t frame_counter;
uint16_t audio_buffer_size = AUDIO_BUFFER_SIZE;

void apu_init(size_t frequency) {
    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);
    pulse_init(&pulse1);
    pulse_init(&pulse2);
    triangle_init(&triangle);
    noise_init(&noise);
    dmc_init(&dmc);

    sample_rate = frequency;
    audio_buffer_index = 0;
    frame_counter = 0;

    filter_init_high_pass(&filter1, sample_rate, 90.0);
    filter_init_high_pass(&filter2, sample_rate, 440.0);
    filter_init_low_pass(&filter3, sample_rate, 14000.0);
}

inline double clamp(double d, double min, double max) {
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

uint8_t apu_get_sample(void) {
    // https://www.nesdev.org/wiki/APU_Mixer
    uint8_t p1 = pulse_output(&pulse1);
    uint8_t p2 = pulse_output(&pulse2);
    uint8_t t = triangle_output(&triangle);
    uint8_t n = noise_output(&noise);
    uint8_t d = dmc_output(&dmc);

    double pulse_out = PULSE_MIXER_LOOKUP[p1 + p2];
    double triangle_out = TRIANGLE_MIXER_LOOKUP[3 * t + 2 * n + d];
    double sample = pulse_out + triangle_out;

    sample = filter_output(&filter1, sample);
    sample = filter_output(&filter2, sample);
    sample = filter_output(&filter3, sample);

    // normalize to [0, 1] (constants found by running the game and measuring the output)
    sample = clamp((sample + 0.325022) * 1.5792998016399449, 0.0, 1.0);

    return (uint8_t)(255.0 * sample);
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
        // Noise
        case 0x400C: {
            noise_write_control(&noise, value);
            break;
        }
        case 0x400E: {
            noise_write_period(&noise, value);
            break;
        }
        case 0x400F: {
            noise_write_length(&noise, value);
            break;
        }
        case 0x400D: {
            break;
        }
        // DMC
        case 0x4010: {
            dmc_write_control(&dmc, value);
            break;
        }
        case 0x4011: {
            dmc_write_output(&dmc, value);
            break;
        }
        case 0x4012: {
            dmc_write_sample_addr(&dmc, value);
            break;
        }
        case 0x4013: {
            dmc_write_sample_len(&dmc, value);
            break;
        }
        // Control
        case 0x4015: {
            pulse_set_enabled(&pulse1, (value & 1) != 0);
            pulse_set_enabled(&pulse2, (value & 2) != 0);
            triangle_set_enabled(&triangle, (value & 4) != 0);
            noise_set_enabled(&noise, (value & 8) != 0);
            dmc_set_enabled(&dmc, (value & 16) != 0);
            dmc_clear_interrupt_flag(&dmc);
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
    noise_step_timer(&noise);
    dmc_step_timer(&dmc);
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
    noise_step_length_counter(&noise);
}

void apu_step_sweep(void) {
    pulse_step_sweep(&pulse1);
    pulse_step_sweep(&pulse2);
}

const size_t CYCLES_PER_FRAME = CPU_FREQUENCY / FRAME_RATE;

void apu_step_quarter_frame(void) {
    static size_t quarter_frame_counter = 0;
    size_t samples_per_quarter_frame = sample_rate / (4 * FRAME_RATE);
    double cycles_per_quarter_frame = (double)CYCLES_PER_FRAME / 4.0;
    frame_counter = (frame_counter + 1) % 5; // TODO: confirm that 4-step sequence is never used

    if (frame_counter & 1) {
        apu_step_envelope();
    } else {
        apu_step_length_counter();
        apu_step_envelope();
        apu_step_sweep();
    }

    size_t samples_written = 0;
    size_t samples_to_write = samples_per_quarter_frame;
    // on the final step of the sequencer, output the remaining samples for this frame
    if (quarter_frame_counter == 3) {
        samples_to_write = (sample_rate / FRAME_RATE) - 3 * samples_per_quarter_frame;
    }

    for (size_t i = 0; i < (size_t)cycles_per_quarter_frame; i++) {
        double progress_counter = (double)i / cycles_per_quarter_frame;
        double progress_samples = (double)samples_written / (double)samples_to_write;

        if (samples_written < samples_to_write && progress_counter > progress_samples) {
            if (audio_buffer_index < AUDIO_BUFFER_SIZE) {
                audio_buffer[audio_buffer_index++] = apu_get_sample();
                samples_written++;
            }
        }

        apu_step_timer();
    }

    quarter_frame_counter = (quarter_frame_counter + 1) & 3;
}

void apu_step_frame(void) {
    // step the frame sequencer 4 times per frame
    // https://www.nesdev.org/wiki/APU_Frame_Counter

    
    for (size_t i = 0; i < 4; i++) {
        apu_step_quarter_frame();
    }
}

void apu_fill_buffer(uint8_t* cb_buffer, size_t size) {
    while (audio_buffer_index < size) {
        apu_step_quarter_frame();
    }

    size_t len = size > audio_buffer_index ? audio_buffer_index : size;

    memcpy(cb_buffer, audio_buffer, len);
    audio_buffer_index -= len;
    memcpy(audio_buffer, audio_buffer + len, audio_buffer_index);
}
