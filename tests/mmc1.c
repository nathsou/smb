// Harness for tests/mmc1 (see tests/check_fixtures.py): the recompiled MMC1
// program, frame by frame. The reference interpreter checks every frame's
// state too; these are what the program is written to do.
#include "lib/code.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    cpu_init();
    chr_ram = true;
    uint8_t chr[CHR_ROM_SIZE] = {0};
    video_init(chr);
    mapper_init(1);

    // Reset runs bank 0's setup, then waits in a subroutine of the main loop.
    cpu_reset_entry();
    assert(wram[0] == 0x11 && wram[3] == 0x44);              // the cartridge's RAM
    assert(chr_rom[0x10] == 0x11 && chr_rom[0x13] == 0x44);  // the pattern tables (CHR RAM)
    assert(ram[0x30] == 1 && ram[0x32] == 1);                // bank 0's step, the odd case
    assert(video.mirroring == MIRROR_HORIZONTAL);
    assert(cpu_resume_depth == 2 && ram[0x14] == 0);          // suspended in WaitFrame

    // Each frame: the interrupt (bank 0's or 1's tick), then the main loop
    // switches to the other bank and runs its step.
    next_frame();
    assert(ram[0x14] == 1 && ram[0x40] == 1 && ram[0x41] == 0xa0);  // bank 0's tick
    assert(ram[0x13] == 1 && ram[0x50] == 1);                        // bank 1's step
    assert(ram[0x52] == 1 && ram[0x55] == 1);                        // the computed jumps
    assert(ram[0x56] == 1 && ram[0x16] == 1 && ram[0x53] == 1);      // returned past the caller
    next_frame();
    assert(ram[0x42] == 1 && ram[0x43] == 0xb1);                     // bank 1's tick
    assert(ram[0x30] == 2 && ram[0x31] == 0x22);                     // bank 0's even case
    next_frame();
    next_frame();
    // every fourth frame, bytes of bank 2 through the window
    assert(ram[0x300] == 0x5a && ram[0x303] == 0x8d);
    for (int i = 0; i < 12; i++) next_frame();
    assert(ram[0x14] == 16 && ram[0x51] == 4 && ram[0x52] == 4 && ram[0x54] == 4 && ram[0x55] == 4);
    // suspended in WaitFrame, below the main loop's return address
    assert(ram[0x13] == 0 && prg_bank == 0 && sp == 0xfd);
    puts("Independent MMC1 program passed");
}
