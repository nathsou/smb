#include "lib/common.h"
#include "raylib.h"
#include <string.h>
#include <stdio.h>
#include "lib/state.h"

#define ROM_PATH "smb.nes"

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
#define SAVE_FILE "smb.sav"

bool last_save_state_loaded = false;
bool should_save_state = false;
bool should_load_state = false;

typedef struct {
    char *name;
    uint8_t left;
    uint8_t right;
    uint8_t up;
    uint8_t down;
    uint8_t a;
    uint8_t b;
    uint8_t start;
    uint8_t select;
    uint8_t save;
    uint8_t load;
} GamepadMapping;

GamepadMapping default_gamepad_mapping = {
    .name = "<default>",
    .left = GAMEPAD_BUTTON_LEFT_FACE_LEFT,
    .right = GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
    .up = GAMEPAD_BUTTON_LEFT_FACE_UP,
    .down = GAMEPAD_BUTTON_LEFT_FACE_DOWN,
    .a = GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
    .b = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,
    .start = GAMEPAD_BUTTON_MIDDLE_LEFT,
    .select = GAMEPAD_BUTTON_MIDDLE_RIGHT,
    .save = GAMEPAD_BUTTON_LEFT_TRIGGER_1,
    .load = GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
};

GamepadMapping gamepad_mappings[] = {
    {
        // 8BitDo SN30 Pro+ (MacOS mode: START + A)
        .name = "DUALSHOCK 4 Wireless Controller",
        .left = 4,
        .right = 2,
        .up = 1,
        .down = 3,
        .a = 7,
        .b = 8,
        .start = 15,
        .select = 13,
        .save = 11,
        .load = 9,
    },
};

GamepadMapping *get_gamepad_mapping(const char *name) {
    size_t len  = sizeof(gamepad_mappings) / sizeof(GamepadMapping);

    for (size_t i = 0; i < len; i++) {
        if (strcmp(gamepad_mappings[i].name, name) == 0) {
            return &gamepad_mappings[i];
        }
    }

    return &default_gamepad_mapping;
}

void handle_inputs(int gamepad, GamepadMapping *mapping) {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool a = false;
    bool b = false;
    bool start = false;
    bool select = false;
    
    // Keyboard inputs
    if (IsKeyDown(CONTROLLER1_UP_KEY)) up = true;
    if (IsKeyDown(CONTROLLER1_LEFT_KEY)) left = true;
    if (IsKeyDown(CONTROLLER1_DOWN_KEY)) down = true;
    if (IsKeyDown(CONTROLLER1_RIGHT_KEY)) right = true;
    if (IsKeyDown(CONTROLLER1_A_KEY)) a = true;
    if (IsKeyDown(CONTROLLER1_B_KEY)) b = true;
    if (IsKeyDown(CONTROLLER1_START_KEY)) start = true;
    if (IsKeyDown(CONTROLLER1_SELECT_KEY)) select = true;
    if (IsKeyPressed(KEY_Z)) should_save_state = true;
    if (IsKeyPressed(KEY_X)) should_load_state = true;

    // Gamepad inputs
    if (mapping != NULL) {
        if (IsGamepadButtonDown(gamepad, mapping->left)) left = true;
        if (IsGamepadButtonDown(gamepad, mapping->right)) right = true;
        if (IsGamepadButtonDown(gamepad, mapping->up)) up = true;
        if (IsGamepadButtonDown(gamepad, mapping->down)) down = true;
        if (IsGamepadButtonDown(gamepad, mapping->a)) a = true;
        if (IsGamepadButtonDown(gamepad, mapping->b)) b = true;
        if (IsGamepadButtonDown(gamepad, mapping->start)) start = true;
        if (IsGamepadButtonDown(gamepad, mapping->select)) select = true;
        if (IsGamepadButtonDown(gamepad, mapping->save)) should_save_state = true;
        if (IsGamepadButtonDown(gamepad, mapping->load)) should_load_state = true;
    }

    uint8_t state = 0;
    if (up) state |= CONTROLLER_UP;
    if (down) state |= CONTROLLER_DOWN;
    if (left) state |= CONTROLLER_LEFT;
    if (right) state |= CONTROLLER_RIGHT;
    if (a) state |= CONTROLLER_A;
    if (b) state |= CONTROLLER_B;
    if (start) state |= CONTROLLER_START;
    if (select) state |= CONTROLLER_SELECT;

    update_controller1(state);
}

void audio_input_callback(void* output_buffer, unsigned int frames) {
    uint8_t *samples = (uint8_t*)output_buffer;
    apu_fill_buffer(samples, (size_t)frames);
}

void read_save_state(char *path, uint8_t *buffer) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }

    fread(buffer, 1, SAVE_STATE_SIZE, file);
    fclose(file);
}

void write_save_state(char *path, uint8_t *buffer) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }

    fwrite(buffer, 1, SAVE_STATE_SIZE, file);
    fclose(file);
}

int main(void) {
    cpu_init();
    apu_init(AUDIO_SAMPLE_RATE);

    if (read_chr_rom(ROM_PATH)) {
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
    
    int gamepad = 0;
    GamepadMapping *mapping = NULL;

    while (!WindowShouldClose()) {
        if (mapping == NULL && IsGamepadAvailable(gamepad)) {
            const char *name = GetGamepadName(gamepad);
            mapping = get_gamepad_mapping(name);
            printf("Gamepad %d name: %s\n detected", gamepad, name);
        }
        
        handle_inputs(gamepad, mapping);
        next_frame();
        ppu_render();
        apu_step_frame();
        UpdateTexture(texture, frame);

        BeginDrawing();
            ClearBackground(WHITE);
            DrawTexturePro(texture, source, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        if (should_save_state) {
            save_state(last_save_state);
            write_save_state(SAVE_FILE, last_save_state);
            should_save_state = false;
            last_save_state_loaded = true;
        } else if (should_load_state) {
            if (!last_save_state_loaded) {
                read_save_state(SAVE_FILE, last_save_state);
                last_save_state_loaded = true;
            }

            load_state(last_save_state);
            should_load_state = false;
        }
    }

    StopAudioStream(stream);
    UnloadAudioStream(stream);
    UnloadTexture(texture);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
