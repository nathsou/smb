#ifndef SMB_MAPPER_H
#define SMB_MAPPER_H

#include <stdint.h>

// MMC1 (iNES mapper 1): a 16 KiB bank of PRG ROM switched in at $8000, the
// last bank fixed at $C000, 8 KiB of RAM at $6000, and the nametable
// mirroring. The game configures it with serial writes to $8000-$FFFF.
// Generated code reads the window through `prg_window`; the bank mapped is
// `prg_bank`. Builds for such cartridges define MAPPER.

#define WRAM_SIZE 0x2000

extern uint8_t wram[WRAM_SIZE];
extern uint8_t prg_bank;
extern const uint8_t *prg_window;

// The mapper's registers, for save states.
typedef struct {
    uint8_t shift, count;
    uint8_t control, chr0, chr1, prg;
} Mmc1;
extern Mmc1 mmc1;

// The power-on state: control $0C (the last bank fixed at $C000), bank 0.
void mapper_init(void);
void mapper_write(uint16_t addr, uint8_t value);
// Applies the registers (after loading them).
void mapper_apply(void);

#endif
