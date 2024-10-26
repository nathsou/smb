#include "constants.h"
#include "stdio.h"
#include "cpu.h"

extern void smb();

int main(void) {
    init_cpu(NULL);
    printf("PPU_ADDRESS: %x\n", PPU_ADDRESS);
    return 0;
}
