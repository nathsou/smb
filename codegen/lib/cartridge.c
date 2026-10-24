#include "cartridge.h"
#include "video.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

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
    uint8_t chr[CHR_ROM_SIZE];
    memset(chr, 0, sizeof(chr));
    chr_ram = chr_size == 0;
    if (!chr_ram && (fseek(file, start + prg_size, SEEK_SET) != 0 ||
                     fread(chr, 1, CHR_ROM_SIZE, file) != CHR_ROM_SIZE)) {
        fprintf(stderr, "Error: Could not read the CHR ROM of %s\n", rom_path);
        fclose(file);
        return 1;
    }
    fclose(file);
    video_init(chr);
    render_init();
    return 0;
}
