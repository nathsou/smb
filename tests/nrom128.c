#include "lib/code.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    cpu_init();
    cpu_reset_entry();
    assert(ram[0] == 0x77 && ram[1] == 2 && ram[2] == 3);
    assert(ram[0x7ff] == 0);
    assert(ram[0x10] == 0 && ram[0x11] == 1);
    assert(ram[0x12] == 8 && ram[0x13] == 5);
    assert(oam[0x21] == 0x42 && oam_addr == 0x22);
    assert(sp == 0xff);
    assert(read_byte(0x8000) == read_byte(0xc000));
    assert(read_byte(0xbffc) == read_byte(0xfffc));
    next_frame();
    assert(ram[0x14] == 1 && sp == 0xff);
    puts("Independent NROM-128 program passed");
}
