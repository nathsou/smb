// The NES sound hardware (APU) as a synthesizer, following
// https://www.nesdev.org/wiki/APU: two pulse channels, a triangle and a
// noise channel (SMB uses only the DMC's output level). See apu.h for how it
// is fed.
//
// Channels are event-driven: each timer is scheduled at its next step in CPU
// cycles, so the cost follows the notes, not the clock. Whenever the mixed
// output changes, a band-limited step (a windowed sinc) is added at that
// exact time to a "blip" buffer; integrating it gives samples without the
// aliasing of point sampling. The NES's own output filters follow.
#include "apu.h"
#include "external.h"

#define CPU_HZ 1789773
#define FRAME_RATE 60
#define NEVER INT64_MAX

static const uint8_t LENGTH_TABLE[32] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30,
};

static const uint8_t PULSE_DUTY_TABLE[][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 },
};

static const uint8_t TRIANGLE_SEQUENCE[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

// in CPU cycles
static const uint16_t NOISE_PERIOD_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

// Envelope and length counter (https://www.nesdev.org/wiki/APU_Envelope,
// https://www.nesdev.org/wiki/APU_Length_Counter)

typedef struct {
    bool start, loop, constant;
    uint8_t volume;  // constant volume, or the divider period
    uint8_t divider, decay;
} Envelope;

static void envelope_clock(Envelope *e) {
    if (e->start) {
        e->start = false;
        e->decay = 15;
        e->divider = e->volume;
    } else if (e->divider == 0) {
        e->divider = e->volume;
        if (e->decay > 0) e->decay--;
        else if (e->loop) e->decay = 15;
    } else {
        e->divider--;
    }
}

static uint8_t envelope_level(const Envelope *e) {
    return e->constant ? e->volume : e->decay;
}

// Pulse (https://www.nesdev.org/wiki/APU_Pulse): the timer counts APU
// cycles, two CPU cycles each.

typedef struct {
    bool enabled, halt;
    uint8_t duty, step, length;
    uint16_t period;
    Envelope envelope;
    bool sweep_enabled, sweep_negate, sweep_reload;
    uint8_t sweep_period, sweep_shift, sweep_divider;
    bool ones_complement; // pulse 1 negates with one's complement
    int64_t next;         // CPU cycle of the next sequencer step
} Pulse;

static uint16_t sweep_target(const Pulse *p) {
    int change = p->period >> p->sweep_shift;
    if (p->sweep_negate) change = -change - (p->ones_complement ? 1 : 0);
    int target = p->period + change;
    return (uint16_t)(target < 0 ? 0 : target);
}

static bool pulse_muted(const Pulse *p) {
    return p->period < 8 || sweep_target(p) > 0x7ff;
}

static uint8_t pulse_level(const Pulse *p) {
    if (!p->enabled || p->length == 0 || pulse_muted(p) || !PULSE_DUTY_TABLE[p->duty][p->step]) return 0;
    return envelope_level(&p->envelope);
}

static void pulse_schedule(Pulse *p, int64_t now) {
    // Inaudible periods are parked rather than stepped (they are muted).
    p->next = p->period < 8 ? NEVER : now + 2 * ((int64_t)p->period + 1);
}

static void pulse_clock_sweep(Pulse *p) {
    if (p->sweep_divider == 0 && p->sweep_enabled && p->sweep_shift > 0 && !pulse_muted(p)) {
        p->period = sweep_target(p);
    }
    if (p->sweep_divider == 0 || p->sweep_reload) {
        p->sweep_divider = p->sweep_period;
        p->sweep_reload = false;
    } else {
        p->sweep_divider--;
    }
}

// Triangle (https://www.nesdev.org/wiki/APU_Triangle): the timer counts CPU
// cycles; when stopped the output holds its level.

typedef struct {
    bool enabled, control, reload;
    uint8_t linear_period, linear, length, step;
    uint16_t period;
    int64_t next;
} Triangle;

static void triangle_schedule(Triangle *t, int64_t now) {
    // Periods below 2 are ultrasonic: hold the level instead of stepping.
    t->next = t->period < 2 ? NEVER : now + (int64_t)t->period + 1;
}

// Noise (https://www.nesdev.org/wiki/APU_Noise)

typedef struct {
    bool enabled, halt, mode;
    uint8_t length;
    uint16_t period, shift;
    Envelope envelope;
    int64_t next;
} Noise;

static uint8_t noise_level(const Noise *n) {
    if (!n->enabled || n->length == 0 || (n->shift & 1)) return 0;
    return envelope_level(&n->envelope);
}

// Band-limited synthesis: a step of height d at time t (in samples) adds
// d * kernel[phase(t)][k] to sample floor(t) + k. The kernel is a
// Blackman-windowed sinc impulse with its cutoff just below the Nyquist
// frequency, so the integrated buffer holds band-limited steps, delayed by
// half the kernel.

#define BLIP_PHASES 64
#define BLIP_TAPS 16
#define BLIP_SIZE 1024 // a power of two, more than a render chunk plus the taps
#define PI 3.14159265358979323846

static float kernel[BLIP_PHASES][BLIP_TAPS];
static float blip[BLIP_SIZE];

// floor, sin and cos without libm (the WebAssembly build has none); sine is
// accurate to about 1e-9 after reduction to [-pi/2, pi/2].
static double floor_(double v) {
    double t = (double)(int64_t)v;
    return t > v ? t - 1 : t;
}

static double sine(double x) {
    double turns = floor_(x / (2 * PI) + 0.5);
    x -= turns * 2 * PI; // [-pi, pi]
    if (x > PI / 2) x = PI - x;
    if (x < -PI / 2) x = -PI - x;
    double x2 = x * x, term = x, sum = x;
    for (int k = 1; k <= 7; k++) {
        term *= -x2 / ((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sum;
}

static double cosine(double x) {
    return sine(x + PI / 2);
}

static void kernel_init(double cutoff) { // cutoff as a fraction of the sample rate
    for (int phase = 0; phase < BLIP_PHASES; phase++) {
        double total = 0;
        double taps[BLIP_TAPS];
        for (int k = 0; k < BLIP_TAPS; k++) {
            // distance from the step, in samples
            double x = k - BLIP_TAPS / 2 + 1 - (double)phase / BLIP_PHASES;
            double sinc = x == 0 ? 2 * cutoff : sine(2 * PI * cutoff * x) / (PI * x);
            double w = (x + BLIP_TAPS / 2.0) / BLIP_TAPS; // window position, 0..1
            double window = 0.42 - 0.5 * cosine(2 * PI * w) + 0.08 * cosine(4 * PI * w);
            taps[k] = sinc * window;
            total += taps[k];
        }
        for (int k = 0; k < BLIP_TAPS; k++) kernel[phase][k] = (float)(taps[k] / total);
    }
}

// Output filters (https://www.nesdev.org/wiki/APU_Mixer): first-order high
// passes at 90 Hz and 440 Hz and a low pass at 14 kHz.

typedef struct {
    float b0, b1, a1, x, y;
} Filter;

static void high_pass(Filter *f, double rate, double cutoff) {
    double c = rate / (cutoff * PI);
    double a0 = 1 / (1 + c);
    *f = (Filter){(float)(c * a0), (float)(-c * a0), (float)((1 - c) * a0), 0, 0};
}

static void low_pass(Filter *f, double rate, double cutoff) {
    double c = rate / (cutoff * PI);
    double a0 = 1 / (1 + c);
    *f = (Filter){(float)a0, (float)a0, (float)((1 - c) * a0), 0, 0};
}

static float filter(Filter *f, float x) {
    float y = f->b0 * x + f->b1 * f->x - f->a1 * f->y;
    f->x = x;
    f->y = y;
    return y;
}

// The APU

static Pulse pulse[2];
static Triangle triangle;
static Noise noise;
static uint8_t dmc_level;
static Filter filters[3];
static uint32_t rate;
static int64_t now;             // CPU cycles since apu_init
static int64_t sample_count;    // samples rendered since apu_init
static float mixed;             // the mixer's output at `now`
static float integrator;
// The frame sequencer (https://www.nesdev.org/wiki/APU_Frame_Counter).
static bool five_step;
static int64_t sequence_start;
static int sequence_step;
static int64_t next_sequence;
// Frames of register writes (apu.h) are taken every rate / 60 samples.
static int64_t frame_index;
static int64_t next_frame;

static const int64_t SEQUENCE_STEPS[5] = {7457, 14913, 22371, 29829, 37281};

static int64_t sample_cycle(int64_t sample) {
    return sample * CPU_HZ / rate;
}

static void schedule_sequence(void) {
    next_sequence = sequence_start + SEQUENCE_STEPS[sequence_step];
}

static void clock_quarter(void) {
    envelope_clock(&pulse[0].envelope);
    envelope_clock(&pulse[1].envelope);
    envelope_clock(&noise.envelope);
    if (triangle.reload) triangle.linear = triangle.linear_period;
    else if (triangle.linear > 0) triangle.linear--;
    if (!triangle.control) triangle.reload = false;
}

static void clock_half(void) {
    for (int i = 0; i < 2; i++) {
        if (!pulse[i].halt && pulse[i].length > 0) pulse[i].length--;
        pulse_clock_sweep(&pulse[i]);
    }
    if (!triangle.control && triangle.length > 0) triangle.length--;
    if (!noise.halt && noise.length > 0) noise.length--;
}

static void sequence_clock(void) {
    // 4-step: E, EL, E, EL; 5-step: E, EL, E, -, EL (E: envelopes and the
    // triangle's linear counter; L: lengths and sweeps)
    int last = five_step ? 4 : 3;
    bool quarter = sequence_step != 3 || !five_step;
    bool half = sequence_step == 1 || sequence_step == last;
    if (quarter) clock_quarter();
    if (half) clock_half();
    if (sequence_step == last) {
        sequence_start += SEQUENCE_STEPS[last] + 1;
        sequence_step = 0;
    } else {
        sequence_step++;
    }
    schedule_sequence();
}

static void write_register(uint8_t reg, uint8_t value) {
    if (reg < 8) {
        Pulse *p = &pulse[reg >> 2];
        switch (reg & 3) {
            case 0:
                p->duty = value >> 6;
                p->halt = p->envelope.loop = (value & 0x20) != 0;
                p->envelope.constant = (value & 0x10) != 0;
                p->envelope.volume = value & 0x0f;
                break;
            case 1:
                p->sweep_enabled = (value & 0x80) != 0;
                p->sweep_period = (value >> 4) & 7;
                p->sweep_negate = (value & 0x08) != 0;
                p->sweep_shift = value & 7;
                p->sweep_reload = true;
                break;
            case 2:
                p->period = (uint16_t)((p->period & 0x700) | value);
                if (p->next == NEVER) pulse_schedule(p, now);
                break;
            case 3:
                p->period = (uint16_t)((p->period & 0xff) | ((value & 7) << 8));
                if (p->enabled) p->length = LENGTH_TABLE[value >> 3];
                p->step = 0;
                p->envelope.start = true;
                if (p->next == NEVER) pulse_schedule(p, now);
                break;
        }
        return;
    }
    switch (reg) {
        case 0x08:
            triangle.control = (value & 0x80) != 0;
            triangle.linear_period = value & 0x7f;
            break;
        case 0x0a:
            triangle.period = (uint16_t)((triangle.period & 0x700) | value);
            if (triangle.next == NEVER) triangle_schedule(&triangle, now);
            break;
        case 0x0b:
            triangle.period = (uint16_t)((triangle.period & 0xff) | ((value & 7) << 8));
            if (triangle.enabled) triangle.length = LENGTH_TABLE[value >> 3];
            triangle.reload = true;
            if (triangle.next == NEVER) triangle_schedule(&triangle, now);
            break;
        case 0x0c:
            noise.halt = noise.envelope.loop = (value & 0x20) != 0;
            noise.envelope.constant = (value & 0x10) != 0;
            noise.envelope.volume = value & 0x0f;
            break;
        case 0x0e:
            noise.mode = (value & 0x80) != 0;
            noise.period = NOISE_PERIOD_TABLE[value & 0x0f];
            break;
        case 0x0f:
            if (noise.enabled) noise.length = LENGTH_TABLE[value >> 3];
            noise.envelope.start = true;
            break;
        case 0x11:
            dmc_level = value & 0x7f;
            break;
        case 0x15:
            pulse[0].enabled = (value & 1) != 0;
            pulse[1].enabled = (value & 2) != 0;
            triangle.enabled = (value & 4) != 0;
            noise.enabled = (value & 8) != 0;
            if (!pulse[0].enabled) pulse[0].length = 0;
            if (!pulse[1].enabled) pulse[1].length = 0;
            if (!triangle.enabled) triangle.length = 0;
            if (!noise.enabled) noise.length = 0;
            break;
        case 0x17:
            // resets the sequencer; 5-step mode clocks everything at once
            five_step = (value & 0x80) != 0;
            sequence_start = now;
            sequence_step = 0;
            schedule_sequence();
            if (five_step) {
                clock_quarter();
                clock_half();
            }
            break;
        default:
            break; // the DMC's sample playback ($4010, $4012-$4013) is unused
    }
}

// The non-linear mixer (https://www.nesdev.org/wiki/APU_Mixer).
static float mix(void) {
    float pulses = (float)(pulse_level(&pulse[0]) + pulse_level(&pulse[1]));
    float pulse_out = pulses == 0 ? 0 : 95.88f / (8128.0f / pulses + 100.0f);
    float t = TRIANGLE_SEQUENCE[triangle.step] / 8227.0f;
    float n = noise_level(&noise) / 12241.0f;
    float d = dmc_level / 22638.0f;
    float tnd = t + n + d;
    float tnd_out = tnd == 0 ? 0 : 159.79f / (1.0f / tnd + 100.0f);
    return pulse_out + tnd_out;
}

// After any change: a band-limited step to the new level at `now`.
static void update_output(void) {
    float level = mix();
    float delta = level - mixed;
    if (delta == 0) return;
    mixed = level;
    double position = (double)now * rate / CPU_HZ; // in samples
    int64_t index = (int64_t)floor_(position);
    int phase = (int)((position - (double)index) * BLIP_PHASES);
    const float *k = kernel[phase];
    // one sample later: a cycle at the start of a render can map to the
    // previous sample, which was already output
    for (int i = 0; i < BLIP_TAPS; i++) blip[(index + 1 + i) & (BLIP_SIZE - 1)] += delta * k[i];
}

// The queue of frames of register writes (apu.h): a single producer (the
// game, once per frame) and a single consumer (apu_render).
#define QUEUE_FRAMES 8
#define MAX_LATENCY_FRAMES 3

typedef struct {
    uint16_t count;
    uint8_t events[APU_FRAME_EVENTS_MAX * 2];
} QueuedFrame;

static QueuedFrame queue[QUEUE_FRAMES];
static uint32_t queue_head; // next slot the producer fills
static uint32_t queue_tail; // next slot the consumer reads
// Writes not yet published because the queue was full: they join the next
// frame that fits, so no write is ever dropped.
static QueuedFrame pending;

uint32_t apu_applied_writes; // for tests

void apu_push_frame(const uint8_t *events, size_t count) {
    for (size_t i = 0; i < count && pending.count < APU_FRAME_EVENTS_MAX; i++) {
        pending.events[2 * pending.count] = events[2 * i];
        pending.events[2 * pending.count + 1] = events[2 * i + 1];
        pending.count++;
    }
    uint32_t head = __atomic_load_n(&queue_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&queue_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= QUEUE_FRAMES) return; // full: publish with the next frame
    QueuedFrame *slot = &queue[head % QUEUE_FRAMES];
    memcpy(slot->events, pending.events, 2 * (size_t)pending.count);
    slot->count = pending.count;
    pending.count = 0;
    __atomic_store_n(&queue_head, head + 1, __ATOMIC_RELEASE);
}

static void apply_frame(const QueuedFrame *frame) {
    apu_applied_writes += frame->count;
    for (uint16_t i = 0; i < frame->count; i++) {
        write_register(frame->events[2 * i], frame->events[2 * i + 1]);
    }
}

// At a frame boundary: the next frame's writes. With nothing queued (the
// game is late) the channels keep their state; when the game is ahead, older
// frames are applied at once so latency stays bounded.
static void take_frame(void) {
    uint32_t tail = __atomic_load_n(&queue_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&queue_head, __ATOMIC_ACQUIRE);
    if (head == tail) return;
    while (head - tail > MAX_LATENCY_FRAMES) {
        apply_frame(&queue[tail % QUEUE_FRAMES]);
        tail++;
    }
    apply_frame(&queue[tail % QUEUE_FRAMES]);
    __atomic_store_n(&queue_tail, tail + 1, __ATOMIC_RELEASE);
}

void apu_init(size_t sample_rate) {
    memset(pulse, 0, sizeof pulse);
    memset(&triangle, 0, sizeof triangle);
    memset(&noise, 0, sizeof noise);
    memset(blip, 0, sizeof blip);
    pulse[0].ones_complement = true;
    pulse[0].next = pulse[1].next = triangle.next = noise.next = NEVER;
    noise.shift = 1;
    noise.period = NOISE_PERIOD_TABLE[0];
    dmc_level = 0;
    rate = (uint32_t)sample_rate;
    now = 0;
    sample_count = 0;
    mixed = 0;
    integrator = 0;
    five_step = false;
    sequence_start = 0;
    sequence_step = 0;
    schedule_sequence();
    frame_index = 0;
    next_frame = 0;
    queue_head = queue_tail = 0;
    pending.count = 0;
    kernel_init(0.45);
    high_pass(&filters[0], rate, 90);
    high_pass(&filters[1], rate, 440);
    low_pass(&filters[2], rate, 14000);
}

static void noise_schedule(int64_t t) {
    // The shift register only matters while the channel sounds.
    noise.next = noise.enabled && noise.length > 0 ? t + noise.period : NEVER;
}

// Everything that happens before CPU cycle `end`, in order.
static void run_until(int64_t end) {
    for (;;) {
        int64_t t = next_frame;
        if (next_sequence < t) t = next_sequence;
        if (pulse[0].next < t) t = pulse[0].next;
        if (pulse[1].next < t) t = pulse[1].next;
        if (triangle.next < t) t = triangle.next;
        if (noise.next < t) t = noise.next;
        if (t >= end) return;
        now = t;
        if (t == next_frame) {
            take_frame();
            frame_index++;
            next_frame = sample_cycle(frame_index * rate / FRAME_RATE);
        }
        if (t == next_sequence) sequence_clock();
        for (int i = 0; i < 2; i++) {
            if (pulse[i].next == t) {
                pulse[i].step = (pulse[i].step + 1) & 7;
                pulse_schedule(&pulse[i], t);
            }
        }
        if (triangle.next == t) {
            if (triangle.linear > 0 && triangle.length > 0) triangle.step = (triangle.step + 1) & 31;
            triangle_schedule(&triangle, t);
        }
        if (noise.next == t) {
            uint16_t feedback = (noise.shift ^ (noise.shift >> (noise.mode ? 6 : 1))) & 1;
            noise.shift = (uint16_t)((noise.shift >> 1) | (feedback << 14));
            noise_schedule(t);
        }
        if (noise.next == NEVER && noise.enabled && noise.length > 0) noise_schedule(t);
        update_output();
    }
}

#define CHUNK 512
// About the previous output's loudness; the replay peaks at 0.85.
#define GAIN 3.2f

void apu_render(float *out, size_t count) {
    while (count > 0) {
        size_t chunk = count < CHUNK ? count : CHUNK;
        run_until(sample_cycle(sample_count + (int64_t)chunk));
        for (size_t i = 0; i < chunk; i++) {
            size_t slot = (size_t)(sample_count + (int64_t)i) & (BLIP_SIZE - 1);
            integrator += blip[slot];
            blip[slot] = 0;
            float s = integrator;
            for (int f = 0; f < 3; f++) s = filter(&filters[f], s);
            s *= GAIN;
            out[i] = s > 1 ? 1 : s < -1 ? -1 : s;
        }
        sample_count += (int64_t)chunk;
        out += chunk;
        count -= chunk;
    }
}
