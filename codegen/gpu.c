#include "gpu.h"
#include "lib/render.h"
#include "lib/video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATTERN_ATLAS_WIDTH 128  // 16 tiles per row
#define PATTERN_ATLAS_HEIGHT 256 // 32 rows: bank 0 then bank 1

static Texture2D load_bytes(const void *data, int width, int height, int format) {
    Image image = {
        .data = (void *)data,
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = format,
    };
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}

// Decodes the 2-bit planar tiles into one byte (colour index 0-3) per pixel.
static void decode_patterns(uint8_t *atlas) {
    for (int index = 0; index < 512; index++) {
        const uint8_t *tile = &chr_rom[index * 16];
        int left = (index % 16) * 8;
        int top = (index / 16) * 8;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                int bit = 7 - col;
                uint8_t low = (uint8_t)((tile[row] >> bit) & 1);
                uint8_t high = (uint8_t)((tile[row + 8] >> bit) & 1);
                atlas[(top + row) * PATTERN_ATLAS_WIDTH + left + col] = (uint8_t)(low | (high << 1));
            }
        }
    }
}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        text = NULL;
    }
    if (text) text[size] = '\0';
    fclose(file);
    return text;
}

bool gpu_renderer_init(GpuRenderer *gpu, const char *shader_path) {
    char *body = read_text(shader_path);
    if (!body) {
        fprintf(stderr, "Could not read %s\n", shader_path);
        return false;
    }
    static const char version[] = "#version 330\n";
    char *source = malloc(sizeof(version) + strlen(body));
    strcpy(source, version);
    strcat(source, body);
    free(body);

    gpu->shader = LoadShaderFromMemory(NULL, source);
    free(source);
    if (!IsShaderValid(gpu->shader)) return false;

    static uint8_t atlas[PATTERN_ATLAS_WIDTH * PATTERN_ATLAS_HEIGHT];
    decode_patterns(atlas);
    gpu->patterns = load_bytes(atlas, PATTERN_ATLAS_WIDTH, PATTERN_ATLAS_HEIGHT, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    gpu->nametable = load_bytes(video.nametable, 32, NAMETABLE_SIZE / 32, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    for (int i = 0; i < 64 * 3; i++) gpu->colors[i] = COLOR_PALETTE[i] / 255.0f;
    gpu->target = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    SetTextureFilter(gpu->target.texture, TEXTURE_FILTER_POINT);

    gpu->ctrl_loc = GetShaderLocation(gpu->shader, "u_ctrl");
    gpu->mask_loc = GetShaderLocation(gpu->shader, "u_mask");
    gpu->scroll_x_loc = GetShaderLocation(gpu->shader, "u_scroll_x");
    gpu->status_bar_loc = GetShaderLocation(gpu->shader, "u_status_bar");
    gpu->flip_y_loc = GetShaderLocation(gpu->shader, "u_flip_y");
    gpu->patterns_loc = GetShaderLocation(gpu->shader, "u_patterns");
    gpu->nametable_loc = GetShaderLocation(gpu->shader, "u_nametable");
    gpu->palette_loc = GetShaderLocation(gpu->shader, "u_palette");
    gpu->oam_loc = GetShaderLocation(gpu->shader, "u_oam");
    gpu->colors_loc = GetShaderLocation(gpu->shader, "u_colors");
    return true;
}

static void set_int(Shader shader, int loc, int value) {
    SetShaderValue(shader, loc, &value, SHADER_UNIFORM_INT);
}

static void set_bytes(Shader shader, int loc, const uint8_t *bytes, int count) {
    int values[OAM_SIZE];
    for (int i = 0; i < count; i++) values[i] = bytes[i];
    SetShaderValueV(shader, loc, values, SHADER_UNIFORM_INT, count);
}

void gpu_renderer_draw(GpuRenderer *gpu) {
    UpdateTexture(gpu->nametable, video.nametable);

    BeginTextureMode(gpu->target);
    BeginShaderMode(gpu->shader);
    set_int(gpu->shader, gpu->ctrl_loc, video.ctrl);
    set_int(gpu->shader, gpu->mask_loc, video.mask);
    set_int(gpu->shader, gpu->scroll_x_loc, video.scroll_x);
    set_int(gpu->shader, gpu->status_bar_loc, video.status_bar ? 1 : 0);
    // Render textures store their first row at the bottom.
    set_int(gpu->shader, gpu->flip_y_loc, 1);
    SetShaderValueTexture(gpu->shader, gpu->patterns_loc, gpu->patterns);
    SetShaderValueTexture(gpu->shader, gpu->nametable_loc, gpu->nametable);
    set_bytes(gpu->shader, gpu->palette_loc, video.palette.u8, PALETTE_SIZE);
    set_bytes(gpu->shader, gpu->oam_loc, video.oam, OAM_SIZE);
    SetShaderValueV(gpu->shader, gpu->colors_loc, gpu->colors, SHADER_UNIFORM_VEC3, 64);
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void gpu_renderer_unload(GpuRenderer *gpu) {
    UnloadRenderTexture(gpu->target);
    UnloadTexture(gpu->patterns);
    UnloadTexture(gpu->nametable);
    UnloadShader(gpu->shader);
}
