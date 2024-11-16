#include "common.h"
#include "raylib.h"

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

void handle_inputs(void) {
    uint8_t state = 0;

    if (IsKeyDown(CONTROLLER1_UP_KEY)) state |= CONTROLLER_UP;
    if (IsKeyDown(CONTROLLER1_LEFT_KEY)) state |= CONTROLLER_LEFT;
    if (IsKeyDown(CONTROLLER1_DOWN_KEY)) state |= CONTROLLER_DOWN;
    if (IsKeyDown(CONTROLLER1_RIGHT_KEY)) state |= CONTROLLER_RIGHT;
    if (IsKeyDown(CONTROLLER1_A_KEY)) state |= CONTROLLER_A;
    if (IsKeyDown(CONTROLLER1_B_KEY)) state |= CONTROLLER_B;
    if (IsKeyDown(CONTROLLER1_START_KEY)) state |= CONTROLLER_START;
    if (IsKeyDown(CONTROLLER1_SELECT_KEY)) state |= CONTROLLER_SELECT;

    update_controller1(state);
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

    Start();

    SetTargetFPS(60);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "SMB");

    SetAudioStreamBufferSizeDefault(128);

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
        next_frame();
        ppu_render();
        apu_step_frame();
        UpdateTexture(texture, frame);

        BeginDrawing();
            ClearBackground(WHITE);
            DrawTexturePro(texture, source, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();
    }

    StopAudioStream(stream);
    UnloadAudioStream(stream);
    UnloadTexture(texture);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
