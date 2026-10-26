#ifndef SMB_MAPPER_H
#define SMB_MAPPER_H

#include <stdint.h>
#include <stdbool.h>

// Cartridge mappers, for builds that define MAPPER:
//
// - NROM (iNES mapper 0): no registers, the mirroring wired on the board
//   (`mapper_mirroring`), for games that drive the PPU registers;
// - MMC1 (iNES mapper 1): a 16 KiB bank of PRG ROM switched in at $8000,
//   the last bank fixed at $C000 (or 32 KiB switched at once, which for 32
//   KiB of PRG ROM is the same), CHR ROM in banks of 4 or 8 KiB, and the
//   nametable mirroring, configured with serial writes to $8000-$FFFF;
// - MMC3 (iNES mapper 4): 8 KiB banks of PRG ROM in two windows ($8000 and
//   $A000, or $A000 and $C000 in PRG mode 1), the last two banks fixed,
//   1 and 2 KiB banks of CHR, the mirroring, and a scanline counter that
//   raises the IRQ.
//
// Both have 8 KiB of RAM at $6000. Generated code reads PRG ROM through
// `prg_slots` (the offsets in `data` of the 8 KiB at $8000, $A000, $C000 and
// $E000) and dispatches on the bank in each window (`prg_bank`, `prg_bank1`).

#define WRAM_SIZE 0x2000

extern uint8_t wram[WRAM_SIZE];
extern uint32_t prg_slots[4];
extern uint8_t prg_bank, prg_bank1;
extern uint8_t mapper_number;

// The mappers' registers, for save states.
typedef struct {
    uint8_t shift, count;
    uint8_t control, chr0, chr1, prg;
} Mmc1;

typedef struct {
    uint8_t select; // bank select: register, PRG mode (bit 6), CHR inversion (bit 7)
    uint8_t banks[8]; // R0-R5: CHR, R6-R7: PRG
    uint8_t mirroring;
    uint8_t irq_latch, irq_counter, irq_reload, irq_enabled, irq_pending;
    uint8_t prg_mode; // what generated code was recompiled for, bit 7 once known
} Mmc3;

extern Mmc1 mmc1;
extern Mmc3 mmc3;

// The power-on state of mapper `mapper` (0, 1 or 4): MMC1's control $0C
// (the last bank fixed at $C000), bank 0; MMC3's registers cleared.
void mapper_init(uint8_t mapper);
// NROM: the mirroring of the board (the iNES header's), vertical or not.
void mapper_mirroring(uint8_t vertical);
void mapper_write(uint16_t addr, uint8_t value);
// Applies the registers (after loading them).
void mapper_apply(void);
// MMC3: the PRG mode the code was recompiled for, which generated code
// tells at reset. A game that selects the other mode traps: its fixed and
// switched banks would not be where the code expects them.
void mapper_prg_mode(uint8_t mode);
// MMC3: clocks the scanline counter once (a rendered scanline); whether the
// IRQ is raised.
bool mapper_scanline(void);
// The frame's scanlines, after the interrupt handler (the frame model:
// rendering follows the NMI, nothing runs during it): clocks MMC3's
// scanline counter on each rendered scanline, the pre-render one first,
// and where it raises the IRQ, runs `irq` when `enabled`: interrupts are
// enabled in the foreground, or the NMI handler enabled them (CLI) while it
// ran (games run their frame in the NMI handler, where the IRQ interrupts
// it on the hardware). What `irq` changes of the scroll, the control and mask
// registers and the CHR banks applies from the next scanline on: the frame
// is drawn in parts (`video.splits`). Its writes to $2006 set where the
// frame continues, as on the hardware.
void mapper_frame(void (*irq)(void), bool enabled);

#endif
