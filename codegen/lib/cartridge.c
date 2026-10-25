#include "cartridge.h"
#include "video.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

uint8_t cartridge_mapper;
uint8_t cartridge_vertical;

int load_cartridge(const char *rom_path) {
    FILE *file = fopen(rom_path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open rom file: %s\n", rom_path);
        return 1;
    }
    uint8_t header[16];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "NES\x1a", 4) != 0) {
        fprintf(stderr, "Error: %s is not an iNES file\n", rom_path);
        fclose(file);
        return 1;
    }
    long prg_size = header[4] * 0x4000L;
    long chr_size = header[5] * 0x2000L;
    long start = 16 + ((header[6] & 4) ? 512 : 0);
    // old dumping tools left text in bytes 12-15, where byte 7 is not to be trusted either
    bool archaic = (header[7] & 0x0c) != 8 && (header[12] | header[13] | header[14] | header[15]) != 0;
    cartridge_mapper = (uint8_t)((header[6] >> 4) | (archaic ? 0 : header[7] & 0xf0));
    cartridge_vertical = header[6] & 1;
    if (chr_size > CHR_MAX_SIZE) {
        fprintf(stderr, "Error: %s has more CHR ROM than supported\n", rom_path);
        fclose(file);
        return 1;
    }
    memset(chr_rom, 0, CHR_MAX_SIZE);
    chr_ram = chr_size == 0;
    if (!chr_ram && (fseek(file, start + prg_size, SEEK_SET) != 0 ||
                     fread(chr_rom, 1, (size_t)chr_size, file) != (size_t)chr_size)) {
        fprintf(stderr, "Error: Could not read the CHR ROM of %s\n", rom_path);
        fclose(file);
        return 1;
    }
    fclose(file);
    video_init_chr(chr_ram ? CHR_ROM_SIZE : (uint32_t)chr_size);
    render_init();
    return 0;
}
