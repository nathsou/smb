// Hand-written replacements for SMB's display routines (see routines.txt).
//
// The original routines drove the PPU through its registers: a shared write
// latch, an auto-incrementing VRAM address, a buffered data port, and status
// polling for vblank and the sprite 0 hit. Here the same work is expressed as
// direct updates of the Video state, which the renderer draws from. Timing
// waits disappear because a frame is rendered from the finished state.
//
// RAM side effects (pointers left in $00/$01, mirrors of the display bytes,
// buffer resets) are kept exactly, and each routine leaves the registers its
// callers read as printed in the generated code.c. The replay in codegen/hash.c
// checks this frame by frame against tests/golden/warpless.trace.

#include "smb.h"
#include "../lib/cpu.h"
#include "../lib/sound.h"
#include "../lib/video.h"
#include "../lib/external.h"

#define TITLE_SCREEN_DATA_SIZE 0x13a
#define NAMETABLE_TILES 960
#define ATTRIBUTE_BYTES 64
#define BLANK_TILE 0x24
#define TITLE_SCREEN_LIST 0x1ec0 // in CHR ROM
// clear_memory clears RAM below $0700 + Y; a warm boot keeps the top score.
#define COLD_BOOT_OFFSET 0xfe
#define WARM_BOOT_OFFSET 0xd6

static uint8_t rom(uint16_t addr) {
    return data[addr - 0x8000];
}

static bool game_paused(void) {
    return ram[PAUSE_STATE] & 1;
}

// set_ppu_ctrl: set the display control byte and the game's mirror of it.
static void write_control(uint8_t value) {
    video.ctrl = value;
    ram[PPU_CTRL_COPY] = value;
}

// set_scroll: both scroll registers receive the same value.
static void write_scroll(uint8_t value) {
    video.scroll_x = value;
    video.scroll_y = value;
}

// apply_vram_list: apply the VRAM update list pointed to by $00/$01.
// Each entry is a big-endian VRAM address, a control byte and data:
//   bit 7: advance by 32 (write a column) instead of 1 (a row)
//   bit 6: repeat the single data byte instead of copying `count` bytes
//   bits 0-5: count (0 wraps to 256)
// A zero byte where an address would start ends the list.
static void apply_list(void) {
    for (;;) {
        uint16_t list = (uint16_t)(ram[0] | (ram[1] << 8));
        uint8_t high = read_byte(list);
        if (high == 0) break;

        uint16_t addr = (uint16_t)((high << 8) | read_byte((uint16_t)(list + 1)));
        uint8_t control = read_byte((uint16_t)(list + 2));
        bool column = control & 0x80;
        bool repeat = control & 0x40;
        uint8_t count = control & 0x3f;

        uint8_t ctrl = ram[PPU_CTRL_COPY] | VIDEO_CTRL_INCREMENT_32;
        if (!column) ctrl &= (uint8_t)~VIDEO_CTRL_INCREMENT_32;
        write_control(ctrl);

        uint8_t offset = repeat ? 3 : 2; // index of the last byte read
        uint8_t remaining = count;
        do {
            if (!repeat) offset++;
            video_write(addr, read_byte((uint16_t)(list + offset)));
            addr = (uint16_t)(addr + (column ? 32 : 1));
            remaining--;
        } while (remaining != 0);

        // advance the list pointer past this entry
        uint16_t next = (uint16_t)(list + offset + 1);
        ram[0] = (uint8_t)next;
        ram[1] = (uint8_t)(next >> 8);
    }

    write_scroll(0);
    a = 0;
    y = 0;
}

// clear_screen: fill one screen with blank tiles and clear its attributes.
static void blank_screen(uint16_t base) {
    for (uint16_t i = 0; i < NAMETABLE_TILES; i++) {
        video_write((uint16_t)(base + i), BLANK_TILE);
    }
    for (uint16_t i = 0; i < ATTRIBUTE_BYTES; i++) {
        video_write((uint16_t)(base + NAMETABLE_TILES + i), 0);
    }
    ram[VRAM_LIST1_LENGTH] = 0;
    ram[VRAM_LIST1] = 0;
    ram[SCROLL_X] = 0;
    ram[SCROLL_Y] = 0;
    write_scroll(0);
}

// Clear both screens, with sprites in pattern table $0000 and the background
// in $1000.
void clear_screens(void) {
    uint8_t ctrl = (ram[PPU_CTRL_COPY] | VIDEO_CTRL_BACKGROUND_BANK) & 0xf0;
    write_control(ctrl);
    blank_screen(0x2400);
    blank_screen(0x2000);
    a = 0;
    y = 0;
}

// The title screen is a VRAM update list stored in CHR ROM. Copy it to the
// buffer at $0300 and queue it for the next frame.
void draw_title_screen(void) {
    if (ram[GAME_MODE] != 0) {
        next_game_mode_task_b();
        return;
    }

    for (uint16_t i = 0; i < TITLE_SCREEN_DATA_SIZE; i++) {
        ram[VRAM_LIST1_LENGTH + i] = video_read((uint16_t)(TITLE_SCREEN_LIST + i));
    }
    ram[0] = 0; // the copy loop leaves its pointer at $0400
    ram[1] = 4;
    y = TITLE_SCREEN_DATA_SIZE & 0xff;

    a = 5; // draw the buffer at $0300 next frame, then the next screen task
    select_vram_list_b();
}

static bool top_score_digits_valid(void) {
    for (int i = 5; i >= 0; i--) {
        if (ram[TOP_SCORE + i] >= 10) return false;
    }
    return true;
}

// Reset vector. The vblank waits that let the PPU warm up are not needed.
void reset(void) {
    interrupt_disabled = true;
    decimal_flag = false;
    video.ctrl = VIDEO_CTRL_BACKGROUND_BANK;
    sp = 0xff;

    // A valid top score and validation byte mean a warm boot: keep the top
    // score when clearing memory.
    y = COLD_BOOT_OFFSET;
    if (top_score_digits_valid() && ram[WARM_BOOT_MARKER] == 0xa5) {
        y = WARM_BOOT_OFFSET;
    }
    clear_memory(); // clears RAM below the offset in Y, leaves A = 0

    apu_write(APU_DMC_LEVEL, a);
    ram[GAME_MODE] = a;
    ram[WARM_BOOT_MARKER] = 0xa5;
    ram[RANDOM] = 0xa5; // random seed
    apu_write(APU_STATUS, 0x0f); // enable all channels except DMC
    video.mask = VIDEO_MASK_BACKGROUND_LEFT | VIDEO_MASK_SPRITES_LEFT;

    hide_all_sprites();
    clear_screens();
    ram[SCREEN_OFF]++;
    write_control(ram[PPU_CTRL_COPY] | VIDEO_CTRL_NMI);

    cpu_yield(0x8057); // idle until the next frame
}

static void decrement_timers(void) {
    // TIMERS_PAUSED holds all timers while it counts down.
    if (ram[TIMERS_PAUSED] != 0 && --ram[TIMERS_PAUSED] != 0) return;

    // Frame timers tick every frame; interval timers every 21st frame.
    int last = 0x14;
    if ((int8_t)--ram[INTERVAL_TIMER_PERIOD] < 0) {
        ram[INTERVAL_TIMER_PERIOD] = 0x14;
        last = 0x23;
    }
    for (int i = last; i >= 0; i--) {
        if (ram[TIMERS + i] != 0) ram[TIMERS + i]--;
    }
}

// 7-byte linear feedback shift register, fed by bit 1 of its first two bytes.
static void advance_random(void) {
    ram[0] = ram[RANDOM] & 0x02;
    bool carry = ((ram[RANDOM + 1] & 0x02) ^ ram[0]) != 0;
    for (int i = 0; i < 7; i++) {
        uint8_t value = ram[RANDOM + i];
        bool out = value & 1;
        ram[RANDOM + i] = (uint8_t)((value >> 1) | (carry ? 0x80 : 0));
        carry = out;
    }
}

// Once per frame (NMI): present the previous frame's updates, then run one
// frame of game logic.
void frame_interrupt(void) {
    ram[PPU_CTRL_COPY] &= (uint8_t)~VIDEO_CTRL_NMI;
    video.ctrl = ram[PPU_CTRL_COPY] & 0x7e;

    uint8_t mask = ram[SCREEN_OFF]
        ? ram[PPU_MASK_COPY] & 0xe6
        : ram[PPU_MASK_COPY] | 0x1e;
    ram[PPU_MASK_COPY] = mask;
    video.mask = mask & 0xe7; // display off during the update

    write_scroll(0);
    memcpy(video.oam, &ram[SPRITE_Y], OAM_SIZE);

    uint8_t buffer = ram[VRAM_LIST_SELECT];
    ram[0] = rom((uint16_t)(VRAM_LISTS_LOW + buffer));
    ram[1] = rom((uint16_t)(VRAM_LISTS_HIGH + buffer));
    apply_list();

    // Reset the buffer just drawn ($0341 when the control selected it).
    uint8_t offset = rom((uint16_t)(VRAM_LIST_RESET_OFFSETS + (buffer == 6 ? 1 : 0)));
    ram[VRAM_LIST1_LENGTH + offset] = 0;
    ram[VRAM_LIST1 + offset] = 0;
    ram[VRAM_LIST_SELECT] = 0;
    video.mask = ram[PPU_MASK_COPY];

    update_sound();
    read_joypads();
    handle_pause();
    update_top_score();

    if (!game_paused()) {
        decrement_timers();
        ram[FRAME_COUNT]++;
    }
    advance_random();

    // The status bar is split off at the sprite 0 hit; sprites are shuffled
    // while waiting for it.
    if (ram[STATUS_BAR_SPLIT] && !game_paused()) {
        hide_sprites_but_zero();
        shuffle_sprite_slots();
    }

    video.scroll_x = ram[SCROLL_X];
    video.scroll_y = ram[SCROLL_Y];
    uint8_t ctrl = ram[PPU_CTRL_COPY];
    video.ctrl = ctrl;

    if (!game_paused()) {
        run_game_mode();
    }

    video.ctrl = ctrl | VIDEO_CTRL_NMI;
    video.status_bar = ram[STATUS_BAR_SPLIT] != 0;
}
