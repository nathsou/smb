#ifndef SMB_GPU_H
#define SMB_GPU_H

#include <stdbool.h>
#include "raylib.h"
#include <stdint.h>

// Draws the Video state with a fragment shader (web/video.frag): the CHR,
// nametables and each scanline's settings are textures, palette and sprites
// are uniforms, and
// every pixel is computed on the GPU. The software renderer (render.c) is
// the reference.
typedef struct {
    Shader shader;
    Texture2D patterns;
    Texture2D nametable;
    Texture2D lines; // video_lines
    RenderTexture2D target; // 256x240, drawn scaled to the window
    int mirroring_loc, flip_y_loc;
    int patterns_loc, nametable_loc, lines_loc, palette_loc, oam_loc, colors_loc;
    uint32_t chr_version; // of the pattern tables uploaded
    float colors[64 * 3];
} GpuRenderer;

// Loads the shader from shader_path; call after InitWindow and video_init.
bool gpu_renderer_init(GpuRenderer *gpu, const char *shader_path);

// Renders the current Video state into gpu->target.
void gpu_renderer_draw(GpuRenderer *gpu);

void gpu_renderer_unload(GpuRenderer *gpu);

#endif
