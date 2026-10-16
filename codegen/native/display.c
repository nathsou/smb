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
// callers read as printed in codegen/lib/code.c. The replay in codegen/hash.c
// checks this frame by frame against tests/golden/warpless.trace.

#include "../lib/code.h"
#include "../lib/video.h"
#include "../lib/external.h"

#define TITLE_SCREEN_DATA_SIZE 0x13a
#define NAMETABLE_TILES 960
#define ATTRIBUTE_BYTES 64
#define BLANK_TILE 0x24

static uint8_t rom(uint16_t addr) {
    return data[addr - 0x8000];
}

static bool game_paused(void) {
    return ram[GamePauseStatus] & 1;
}

// WritePPUReg1: set the display control byte and the game's mirror of it.
static void set_control(uint8_t value) {
    video.ctrl = value;
    ram[Mirror_PPU_CTRL_REG1] = value;
}

// InitScroll: both scroll registers receive the same value.
static void set_scroll(uint8_t value) {
    video.scroll_x = value;
    video.scroll_y = value;
}

// UpdateScreen: apply the VRAM update list pointed to by $00/$01.
// Each entry is a big-endian VRAM address, a control byte and data:
//   bit 7: advance by 32 (write a column) instead of 1 (a row)
//   bit 6: repeat the single data byte instead of copying `count` bytes
//   bits 0-5: count (0 wraps to 256)
// A zero byte where an address would start ends the list.
static void update_screen(void) {
    for (;;) {
        uint16_t list = (uint16_t)(ram[0] | (ram[1] << 8));
        uint8_t high = read_byte(list);
        if (high == 0) break;

        uint16_t addr = (uint16_t)((high << 8) | read_byte((uint16_t)(list + 1)));
        uint8_t control = read_byte((uint16_t)(list + 2));
        bool column = control & 0x80;
        bool repeat = control & 0x40;
        uint8_t count = control & 0x3f;

        uint8_t ctrl = ram[Mirror_PPU_CTRL_REG1] | VIDEO_CTRL_INCREMENT_32;
        if (!column) ctrl &= (uint8_t)~VIDEO_CTRL_INCREMENT_32;
        set_control(ctrl);

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

    set_scroll(0);
    a = 0;
    y = 0;
}

// WriteNTAddr: fill one screen with blank tiles and clear its attributes.
static void clear_screen(uint16_t base) {
    for (uint16_t i = 0; i < NAMETABLE_TILES; i++) {
        video_write((uint16_t)(base + i), BLANK_TILE);
    }
    for (uint16_t i = 0; i < ATTRIBUTE_BYTES; i++) {
        video_write((uint16_t)(base + NAMETABLE_TILES + i), 0);
    }
    ram[VRAM_Buffer1_Offset] = 0;
    ram[VRAM_Buffer1] = 0;
    ram[HorizontalScroll] = 0;
    ram[VerticalScroll] = 0;
    set_scroll(0);
}

// Clear both screens, with sprites in pattern table $0000 and the background
// in $1000.
void InitializeNameTables(void) {
    uint8_t ctrl = (ram[Mirror_PPU_CTRL_REG1] | VIDEO_CTRL_BACKGROUND_BANK) & 0xf0;
    set_control(ctrl);
    clear_screen(0x2400);
    clear_screen(0x2000);
    a = 0;
    y = 0;
}

// The title screen is a VRAM update list stored in CHR ROM. Copy it to the
// buffer at $0300 and queue it for the next frame.
void DrawTitleScreen(void) {
    if (ram[OperMode] != 0) {
        IncModeTask_B();
        return;
    }

    for (uint16_t i = 0; i < TITLE_SCREEN_DATA_SIZE; i++) {
        ram[VRAM_Buffer1_Offset + i] = video_read((uint16_t)(TitleScreenDataOffset + i));
    }
    ram[0] = 0; // the copy loop leaves its pointer at $0400
    ram[1] = 4;
    y = TITLE_SCREEN_DATA_SIZE & 0xff;

    a = 5; // draw the buffer at $0300 next frame, then the next screen task
    SetVRAMAddr_B();
}

static bool top_score_digits_valid(void) {
    for (int i = 5; i >= 0; i--) {
        if (ram[TopScoreDisplay + i] >= 10) return false;
    }
    return true;
}

// Reset vector. The vblank waits that let the PPU warm up are not needed.
void Start(void) {
    interrupt_disabled = true;
    decimal_flag = false;
    video.ctrl = VIDEO_CTRL_BACKGROUND_BANK;
    sp = 0xff;

    // A valid top score and validation byte mean a warm boot: keep the top
    // score when clearing memory.
    y = ColdBootOffset;
    if (top_score_digits_valid() && ram[WarmBootValidation] == 0xa5) {
        y = WarmBootOffset;
    }
    InitializeMemory(); // clears RAM below the offset in Y, leaves A = 0

    apu_write(SND_DELTA_REG + 1, a);
    ram[OperMode] = a;
    ram[WarmBootValidation] = 0xa5;
    ram[PseudoRandomBitReg] = 0xa5; // random seed
    apu_write(SND_MASTERCTRL_REG, 0x0f); // enable all channels except DMC
    video.mask = VIDEO_MASK_BACKGROUND_LEFT | VIDEO_MASK_SPRITES_LEFT;

    MoveAllSpritesOffscreen();
    InitializeNameTables();
    ram[DisableScreenFlag]++;
    set_control(ram[Mirror_PPU_CTRL_REG1] | VIDEO_CTRL_NMI);

    cpu_yield(0x8057); // idle until the next frame
}

static void decrement_timers(void) {
    // TimerControl pauses all timers while it counts down.
    if (ram[TimerControl] != 0 && --ram[TimerControl] != 0) return;

    // Frame timers tick every frame; interval timers every 21st frame.
    int last = 0x14;
    if ((int8_t)--ram[IntervalTimerControl] < 0) {
        ram[IntervalTimerControl] = 0x14;
        last = 0x23;
    }
    for (int i = last; i >= 0; i--) {
        if (ram[Timers + i] != 0) ram[Timers + i]--;
    }
}

// 7-byte linear feedback shift register, fed by bit 1 of its first two bytes.
static void advance_random(void) {
    ram[0] = ram[PseudoRandomBitReg] & 0x02;
    bool carry = ((ram[PseudoRandomBitReg + 1] & 0x02) ^ ram[0]) != 0;
    for (int i = 0; i < 7; i++) {
        uint8_t value = ram[PseudoRandomBitReg + i];
        bool out = value & 1;
        ram[PseudoRandomBitReg + i] = (uint8_t)((value >> 1) | (carry ? 0x80 : 0));
        carry = out;
    }
}

// Once per frame (NMI): present the previous frame's updates, then run one
// frame of game logic.
void NonMaskableInterrupt(void) {
    ram[Mirror_PPU_CTRL_REG1] &= (uint8_t)~VIDEO_CTRL_NMI;
    video.ctrl = ram[Mirror_PPU_CTRL_REG1] & 0x7e;

    uint8_t mask = ram[DisableScreenFlag]
        ? ram[Mirror_PPU_CTRL_REG2] & 0xe6
        : ram[Mirror_PPU_CTRL_REG2] | 0x1e;
    ram[Mirror_PPU_CTRL_REG2] = mask;
    video.mask = mask & 0xe7; // display off during the update

    set_scroll(0);
    memcpy(video.oam, &ram[Sprite_Data], OAM_SIZE);

    uint8_t buffer = ram[VRAM_Buffer_AddrCtrl];
    ram[0] = rom((uint16_t)(VRAM_AddrTable_Low + buffer));
    ram[1] = rom((uint16_t)(VRAM_AddrTable_High + buffer));
    update_screen();

    // Reset the buffer just drawn ($0341 when the control selected it).
    uint8_t offset = rom((uint16_t)(VRAM_Buffer_Offset + (buffer == 6 ? 1 : 0)));
    ram[VRAM_Buffer1_Offset + offset] = 0;
    ram[VRAM_Buffer1 + offset] = 0;
    ram[VRAM_Buffer_AddrCtrl] = 0;
    video.mask = ram[Mirror_PPU_CTRL_REG2];

    SoundEngine();
    ReadJoypads();
    PauseRoutine();
    UpdateTopScore();

    if (!game_paused()) {
        decrement_timers();
        ram[FrameCounter]++;
    }
    advance_random();

    // The status bar is split off at the sprite 0 hit; sprites are shuffled
    // while waiting for it.
    if (ram[Sprite0HitDetectFlag] && !game_paused()) {
        MoveSpritesOffscreen();
        SpriteShuffler();
    }

    video.scroll_x = ram[HorizontalScroll];
    video.scroll_y = ram[VerticalScroll];
    uint8_t ctrl = ram[Mirror_PPU_CTRL_REG1];
    video.ctrl = ctrl;

    if (!game_paused()) {
        OperModeExecutionTree();
    }

    video.ctrl = ctrl | VIDEO_CTRL_NMI;
    video.status_bar = ram[Sprite0HitDetectFlag] != 0;
}
