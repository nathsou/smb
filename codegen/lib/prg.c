// PRG ROM buffer for builds that load the game at run time (the split
// WebAssembly build, the reference interpreter): the loader copies the ROM's
// PRG banks here, a 16 KiB image twice, and with a mapper every bank (and
// sets the size). Native builds link the generated data.c instead.
#include <stdint.h>

uint8_t data[0x40000];
unsigned prg_image_size = 0x8000;

// Copies a PRG ROM of `size` bytes into the buffer (a 16 KiB image twice).
void prg_load(const uint8_t *bytes, unsigned size) {
    for (unsigned i = 0; i < size; i++) data[i] = bytes[i];
    if (size == 0x4000) {
        for (unsigned i = 0; i < size; i++) data[0x4000 + i] = bytes[i];
        size = 0x8000;
    }
    prg_image_size = size;
}
