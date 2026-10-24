#ifndef SMB_STATE_HASH_H
#define SMB_STATE_HASH_H

#include <stdint.h>

// A hash of the machine state after a frame, for programs that drive the
// hardware registers: RAM, the cartridge's RAM and the bank mapped (with a
// mapper), the pattern tables, and everything the renderer draws from.
// The recompiled program and the reference interpreter must agree on it.
uint32_t hash_state(void);

#endif
