// Routine-level differential test: every generated routine is run twice
// from the same machine state, once as currently generated and once as the
// reference build (renamed ref_*, see check_routines.py), and the results are
// compared. States come from the replay (real RAM and display contents) with
// randomised registers and flags, and every other state also randomises some
// RAM, so paths the replay never takes are exercised too.
#include "lib/common.h"
#include "lib/debug_chr.h"
#include "lib/rec.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define REC_FILE "rec/warpless.rec"
#define RECORDING_FRAME_COUNT 7987
#define SNAPSHOT_INTERVAL 160
#define REGISTER_STATES 6
#define PERTURBED_BYTES 48 // RAM bytes randomised in every other state
#define TIMEOUT_USEC 20000

typedef struct {
    const char *name;
    void (*current)(void);
    void (*reference)(void);
    bool carry, zero, negative, overflow; // flags callers read afterwards
} Routine;

#include "routines.h" // generated: const Routine ROUTINES[]; ROUTINE_COUNT

extern bool controller1_strobe;
extern uint8_t controller1_btn_index;

typedef struct {
    uint8_t ram[RAM_SIZE];
    Video video;
    uint8_t a, x, y, sp;
    bool carry, zero, negative, overflow, interrupt, decimal;
    uint8_t controller_state, controller_index;
    bool controller_strobe;
    int outcome; // 0 returned, 1 trapped, 2 timed out
} Machine;

static void save(Machine *m) {
    memcpy(m->ram, ram, RAM_SIZE);
    m->video = video;
    m->a = a; m->x = x; m->y = y; m->sp = sp;
    m->carry = carry_flag; m->zero = zero_flag; m->negative = neg_flag;
    m->overflow = overflow_flag; m->interrupt = interrupt_disabled; m->decimal = decimal_flag;
    m->controller_state = controller1_state;
    m->controller_index = controller1_btn_index;
    m->controller_strobe = controller1_strobe;
}

static void restore(const Machine *m) {
    memcpy(ram, m->ram, RAM_SIZE);
    video = m->video;
    a = m->a; x = m->x; y = m->y; sp = m->sp;
    carry_flag = m->carry; zero_flag = m->zero; neg_flag = m->negative;
    overflow_flag = m->overflow; interrupt_disabled = m->interrupt; decimal_flag = m->decimal;
    controller1_state = m->controller_state;
    controller1_btn_index = m->controller_index;
    controller1_strobe = m->controller_strobe;
}

static sigjmp_buf escape;

static void on_signal(int signal) {
    siglongjmp(escape, signal == SIGALRM ? 2 : 1);
}

static void run(void (*routine)(void), Machine *out) {
    struct itimerval timer = { .it_value = { .tv_sec = 0, .tv_usec = TIMEOUT_USEC } };
    struct itimerval off = { 0 };
    int outcome = sigsetjmp(escape, 1);
    if (outcome == 0) {
        setitimer(ITIMER_REAL, &timer, NULL);
        routine();
    }
    setitimer(ITIMER_REAL, &off, NULL);
    save(out);
    out->outcome = outcome;
}

static uint32_t rng = 0x12345678;
static uint8_t random_byte(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (uint8_t)rng;
}

static const char *difference(const Routine *r, const Machine *ref, const Machine *cur) {
    if (ref->outcome != cur->outcome) return "outcome (return/trap/timeout)";
    if (ref->outcome != 0) return NULL; // both stopped; state is partial
    if (ref->a != cur->a) return "a";
    if (ref->x != cur->x) return "x";
    if (ref->y != cur->y) return "y";
    if (ref->sp != cur->sp) return "sp";
    if (memcmp(ref->ram, cur->ram, RAM_SIZE) != 0) return "ram";
    if (memcmp(&ref->video, &cur->video, sizeof(Video)) != 0) return "video";
    if (r->carry && ref->carry != cur->carry) return "carry";
    if (r->zero && ref->zero != cur->zero) return "zero";
    if (r->negative && ref->negative != cur->negative) return "negative";
    if (r->overflow && ref->overflow != cur->overflow) return "overflow";
    if (ref->interrupt != cur->interrupt || ref->decimal != cur->decimal) return "status";
    return NULL;
}

int main(void) {
    signal(SIGILL, on_signal);
    signal(SIGTRAP, on_signal);
    signal(SIGSEGV, on_signal);
    signal(SIGALRM, on_signal);

    cpu_init();
    uint8_t chr[DEBUG_CHR_SIZE];
    debug_chr_generate(chr);
    video_init(chr);
    render_init();

    NESInputRecording recording;
    if (rec_open(REC_FILE, "r", &recording) != 0) {
        fprintf(stderr, "Could not open %s\n", REC_FILE);
        return 1;
    }

    static Machine snapshots[RECORDING_FRAME_COUNT / SNAPSHOT_INTERVAL + 1];
    int snapshot_count = 0;
    cpu_reset_entry();
    for (uint32_t i = 0; i < RECORDING_FRAME_COUNT; i++) {
        update_controller1(rec_state_at_frame(&recording, i));
        next_frame();
        if (i % SNAPSHOT_INTERVAL == 0) save(&snapshots[snapshot_count++]);
    }
    rec_close(&recording);

    int failures = 0, completed = 0, exercised = 0;
    for (int r = 0; r < ROUTINE_COUNT; r++) {
        const Routine *routine = &ROUTINES[r];
        int routine_completed = 0;
        const char *first = NULL;
        for (int s = 0; s < snapshot_count; s++) {
            for (int k = 0; k < REGISTER_STATES; k++) {
                Machine start = snapshots[s];
                start.a = random_byte();
                start.x = random_byte();
                start.y = random_byte();
                start.sp = (uint8_t)(0xc0 + (random_byte() & 0x2f));
                uint8_t status = random_byte();
                start.carry = status & 1;
                start.zero = status & 2;
                start.negative = status & 4;
                start.overflow = status & 8;
                if (k & 1) {
                    // reach paths the replay's RAM never takes (stack page excluded)
                    for (int b = 0; b < PERTURBED_BYTES; b++) {
                        uint16_t addr = (uint16_t)(((random_byte() << 8) | random_byte()) & 0x7ff);
                        if (addr < 0x100 || addr >= 0x200) start.ram[addr] = random_byte();
                    }
                }

                Machine ref, cur;
                restore(&start);
                run(routine->reference, &ref);
                restore(&start);
                run(routine->current, &cur);

                const char *what = difference(routine, &ref, &cur);
                if (what && !first) first = what;
                if (ref.outcome == 0 && cur.outcome == 0) routine_completed++;
            }
        }
        completed += routine_completed;
        if (routine_completed > 0) exercised++;
        if (first) {
            failures++;
            printf("MISMATCH %s: %s\n", routine->name, first);
        }
    }

    printf("Routines: %d, returned normally in some state: %d, runs completed: %d, mismatches: %d\n",
        ROUTINE_COUNT, exercised, completed, failures);
    return failures == 0 ? 0 : 1;
}
