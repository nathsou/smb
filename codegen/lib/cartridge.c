#include "cartridge.h"
#include "video.h"
#include "render.h"
#include "external.h"
#ifndef CARTRIDGE_NO_STDIO
#include <stdio.h>
#include <string.h>
#endif

uint8_t cartridge_mapper;
uint8_t cartridge_vertical;

// The iNES header: the mapper and mirroring, and where the CHR ROM is and
// how long. Returns 0 when the header is usable.
static int read_header(const uint8_t header[16], long *chr_start, long *chr_size) {
    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1a) return 1;
    long prg_size = header[4] * 0x4000L;
    *chr_size = header[5] * 0x2000L;
    *chr_start = 16 + ((header[6] & 4) ? 512 : 0) + prg_size;
    // old dumping tools left text in bytes 12-15, where byte 7 is not to be trusted either
    bool archaic = (header[7] & 0x0c) != 8 && (header[12] | header[13] | header[14] | header[15]) != 0;
    cartridge_mapper = (uint8_t)((header[6] >> 4) | (archaic ? 0 : header[7] & 0xf0));
    cartridge_vertical = header[6] & 1;
    return *chr_size > CHR_MAX_SIZE ? 2 : 0;
}

static void finish(long chr_size) {
    video_init_chr(chr_ram ? CHR_ROM_SIZE : (uint32_t)chr_size);
    render_init();
}

int load_cartridge_image(const uint8_t *rom, size_t size) {
    long chr_start, chr_size;
    if (size < 16 || read_header(rom, &chr_start, &chr_size) != 0) return 1;
    memset(chr_rom, 0, CHR_MAX_SIZE);
    chr_ram = chr_size == 0;
    if (!chr_ram) {
        if ((size_t)(chr_start + chr_size) > size) return 1;
        memcpy(chr_rom, rom + chr_start, (size_t)chr_size);
    }
    finish(chr_size);
    return 0;
}

#ifndef CARTRIDGE_NO_STDIO
int load_cartridge(const char *rom_path) {
    FILE *file = fopen(rom_path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open rom file: %s\n", rom_path);
        return 1;
    }
    uint8_t header[16];
    long chr_start, chr_size;
    int bad = fread(header, 1, sizeof(header), file) != sizeof(header) ? 1 : read_header(header, &chr_start, &chr_size);
    if (bad == 1) {
        fprintf(stderr, "Error: %s is not an iNES file\n", rom_path);
        fclose(file);
        return 1;
    }
    if (bad == 2) {
        fprintf(stderr, "Error: %s has more CHR ROM than supported\n", rom_path);
        fclose(file);
        return 1;
    }
    memset(chr_rom, 0, CHR_MAX_SIZE);
    chr_ram = chr_size == 0;
    if (!chr_ram && (fseek(file, chr_start, SEEK_SET) != 0 ||
                     fread(chr_rom, 1, (size_t)chr_size, file) != (size_t)chr_size)) {
        fprintf(stderr, "Error: Could not read the CHR ROM of %s\n", rom_path);
        fclose(file);
        return 1;
    }
    fclose(file);
    finish(chr_size);
    return 0;
}
#endif
