// PRG ROM buffer for builds that load the game at run time (the split
// WebAssembly build): the loader copies the ROM's PRG banks here, a 16 KiB
// image twice. Native builds link the generated data.c instead.
#include <stdint.h>

uint8_t data[0x8000];
const unsigned prg_image_size = 0x8000;
