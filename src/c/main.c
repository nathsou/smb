#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"
#include "code.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"

#define ROM_PATH "Super Mario Bros.nes"
#define CHR_ROM_SIZE 8192 // 8KB
#define NES_HEADER_SIZE 16
#define PRG_ROM_SIZE 32768  // 32KB
#define WINDOW_SCALE 3
#define WINDOW_WIDTH SCREEN_WIDTH * WINDOW_SCALE
#define WINDOW_HEIGHT SCREEN_HEIGHT * WINDOW_SCALE
#define CONTROLLER1_RAM_ADDR 0x4016

#define CONTROLLER_RIGHT 0b10000000
#define CONTROLLER_LEFT 0b01000000
#define CONTROLLER_DOWN 0b00100000
#define CONTROLLER_UP 0b00010000
#define CONTROLLER_START 0b00001000
#define CONTROLLER_SELECT 0b00000100
#define CONTROLLER_B 0b00000010
#define CONTROLLER_A 0b00000001

#define CONTROLLER1_UP_KEY KEY_W
#define CONTROLLER1_LEFT_KEY KEY_A
#define CONTROLLER1_DOWN_KEY KEY_S
#define CONTROLLER1_RIGHT_KEY KEY_D
#define CONTROLLER1_A_KEY KEY_L
#define CONTROLLER1_B_KEY KEY_K
#define CONTROLLER1_START_KEY KEY_ENTER
#define CONTROLLER1_SELECT_KEY KEY_SPACE

#define AUDIO_SAMPLE_RATE 48000

int read_chr_rom(void) {
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

    ppu_init(chr_rom);
    
    fclose(input_file);

    return 0;
}

void handle_inputs(void) {
    if (IsKeyDown(CONTROLLER1_UP_KEY)) {
        controller1_state |= CONTROLLER_UP;
    } else if (IsKeyReleased(CONTROLLER1_UP_KEY)) {
        controller1_state &= ~CONTROLLER_UP;
    }
    
    if (IsKeyDown(CONTROLLER1_LEFT_KEY)) {
        controller1_state |= CONTROLLER_LEFT;
    } else if (IsKeyReleased(CONTROLLER1_LEFT_KEY)) {
        controller1_state &= ~CONTROLLER_LEFT;
    }

    if (IsKeyDown(CONTROLLER1_DOWN_KEY)) {
        controller1_state |= CONTROLLER_DOWN;
    } else if (IsKeyReleased(CONTROLLER1_DOWN_KEY)) {
        controller1_state &= ~CONTROLLER_DOWN;
    }

    if (IsKeyDown(CONTROLLER1_RIGHT_KEY)) {
        controller1_state |= CONTROLLER_RIGHT;
    } else if (IsKeyReleased(CONTROLLER1_RIGHT_KEY)) {
        controller1_state &= ~CONTROLLER_RIGHT;
    }

    if (IsKeyDown(CONTROLLER1_A_KEY)) {
        controller1_state |= CONTROLLER_A;
    } else if (IsKeyReleased(CONTROLLER1_A_KEY)) {
        controller1_state &= ~CONTROLLER_A;
    }

    if (IsKeyDown(CONTROLLER1_B_KEY)) {
        controller1_state |= CONTROLLER_B;
    } else if (IsKeyReleased(CONTROLLER1_B_KEY)) {
        controller1_state &= ~CONTROLLER_B;
    }

    if (IsKeyDown(CONTROLLER1_START_KEY)) {
        controller1_state |= CONTROLLER_START;
    } else if (IsKeyReleased(CONTROLLER1_START_KEY)) {
        controller1_state &= ~CONTROLLER_START;
    }

    if (IsKeyDown(CONTROLLER1_SELECT_KEY)) {
        controller1_state |= CONTROLLER_SELECT;
    } else if (IsKeyReleased(CONTROLLER1_SELECT_KEY)) {
        controller1_state &= ~CONTROLLER_SELECT;
    }
}

void audio_input_callback(void* output_buffer, unsigned int frames) {
    uint8_t *samples = (uint8_t*)output_buffer;
    apu_fill_buffer(samples, (size_t)frames);
}

int main(void) {
    cpu_init();
    apu_init(AUDIO_SAMPLE_RATE);

    if (read_chr_rom()) {
        return 1;
    }

    smb(RUN_STATE_RESET);

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "SMB");
    SetTargetFPS(60);

    InitAudioDevice();

    AudioStream stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 8, 1);
    SetAudioStreamCallback(stream, audio_input_callback);
    PlayAudioStream(stream);

    Image image = {
        .data = frame,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
        .mipmaps = 1,
    };

    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    SetExitKey(KEY_NULL);
    SetConfigFlags(FLAG_VSYNC_HINT);
    SetWindowMaxSize(SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE);
    SetWindowMinSize(SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE);

    Rectangle source = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
    Rectangle dest = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };

    while (!WindowShouldClose()) {
        handle_inputs();
        smb(RUN_STATE_NMI_HANDLER);
        ppu_render();
        apu_step_frame();
        UpdateTexture(texture, frame);

        BeginDrawing();
            ClearBackground(WHITE);
            DrawTexturePro(texture, source, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();
    }

    UnloadAudioStream(stream);
    UnloadTexture(texture);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
