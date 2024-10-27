#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"
#include "code.h"
#include "ppu.h"

#define ROM_PATH "Super Mario Bros.nes"
#define CHR_ROM_SIZE 8192
#define NES_HEADER_SIZE 16
#define PRG_ROM_SIZE 32768  // Super Mario Bros has 32KB of PRG ROM
#define WINDOW_SCALE 2
#define WINDOW_WIDTH SCREEN_WIDTH * WINDOW_SCALE
#define WINDOW_HEIGHT SCREEN_HEIGHT * WINDOW_SCALE

int read_chr_rom() {
    FILE *input_file, *output_file;
    uint8_t chr_rom[CHR_ROM_SIZE];
    uint8_t header[NES_HEADER_SIZE];
    
    // Open input file
    input_file = fopen(ROM_PATH, "rb");
    if (input_file == NULL) {
        fprintf(stderr, "Error: Could not open rom file: %s\n", ROM_PATH);
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

    init_ppu(chr_rom);
    
    fclose(input_file);

    return 0;
}

int main(void) {
    init_cpu();
    smb(RUN_STATE_RESET);
    smb(RUN_STATE_NMI_HANDLER);

    if (read_chr_rom()) {
        return 1;
    }

    render_ppu();

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "SMB");
    SetTargetFPS(60);

    Image image = {
        .data = frame,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
        .mipmaps = 1,
    };

    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    while (!WindowShouldClose()) {
        UpdateTexture(texture, frame);

        BeginDrawing();
            ClearBackground(WHITE);

            Rectangle source = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
            Rectangle dest = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };

            DrawTexturePro(texture, source, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();
    }

    UnloadTexture(texture);
    CloseWindow();

    return 0;
}
