#include "common.h"
#include "rec.h"

#define REC_FILE "rec/warpless.rec"
#define TARGET_FPS (60 * 1)

static NESInputRecording recording;
static uint32_t frame_counter = 0;

void handle_inputs(void) {
    uint8_t state;

    // Get input from recording
    state = rec_state_at_frame(&recording, frame_counter);
    
    // If we've reached the end of the recording, switch to recording mode
    if (!recording.has_next && frame_counter >= recording.current.frame) {
        // Reopen the file in write mode
        rec_close(&recording);
        if (rec_open(REC_FILE, "a", &recording) != 0) {
            fprintf(stderr, "Error: Could not open recording file for writing\n");
            exit(1);
        }
    }

    update_controller1(state);
}

int rec_init(void) {
    if (rec_open(REC_FILE, "r", &recording) != 0) {
        // If file doesn't exist or can't be opened for reading, create new recording
        if (rec_open(REC_FILE, "w", &recording) != 0) {
            fprintf(stderr, "Error: Could not create recording file\n");
            return 1;
        }
    }

    return 0;
}

int main(void) {
    cpu_init();
    rec_init();

    if (read_chr_rom()) {
        rec_close(&recording);
        return 1;
    }

    smb(RUN_STATE_RESET);

    SetTargetFPS(TARGET_FPS);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "SMB");

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

        frame_counter++;
    }

    UnloadTexture(texture);
    CloseAudioDevice();
    CloseWindow();
    rec_close(&recording);

    return 0;
}
