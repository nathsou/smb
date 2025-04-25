#include "common.h"
#include <stdio.h>

int read_chr_rom(char *rom_path) {
    FILE *input_file, *output_file;
    uint8_t chr_rom[CHR_ROM_SIZE];
    uint8_t header[NES_HEADER_SIZE];
    
    // Open input file
    input_file = fopen(rom_path, "rb");
    if (input_file == NULL) {
        fprintf(stderr, "Error: Could not open rom file: %s\n", rom_path);
        return 1;
    }
    
    // Read and verify header
    if (fread(header, 1, NES_HEADER_SIZE, input_file) != NES_HEADER_SIZE) {
        fprintf(stderr, "Error: Could not read NES header\n");
        fclose(input_file);
        return 1;
    }
    
    // Verify NES magic number ("NES" followed by MS-DOS EOF)
    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        fprintf(stderr, "Error: Invalid NES ROM format\n");
        fclose(input_file);
        return 1;
    }
    
    // Skip PRG ROM to reach CHR ROM
    if (fseek(input_file, NES_HEADER_SIZE + PRG_ROM_SIZE, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Could not seek to CHR ROM\n");
        fclose(input_file);
        return 1;
    }
    
    // Read CHR ROM data
    if (fread(chr_rom, 1, CHR_ROM_SIZE, input_file) != CHR_ROM_SIZE) {
        fprintf(stderr, "Error: Could not read CHR ROM data\n");
        fclose(input_file);
        return 1;
    }

    ppu_init(chr_rom);
    
    fclose(input_file);

    return 0;
}
