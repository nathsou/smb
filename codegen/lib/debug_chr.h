#ifndef SMB_DEBUG_CHR_H
#define SMB_DEBUG_CHR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_CHR_SIZE 8192

// Fills chr with generated placeholder tiles; no ROM required.
void debug_chr_generate(uint8_t chr[DEBUG_CHR_SIZE]);

#endif
