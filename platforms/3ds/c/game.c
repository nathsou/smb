// One game's interface for the 3DS front end. build.rs compiles it with
// each game's generated code and runtime, then prefixes every symbol they
// define with the game's name (`smb_game_init`, `metroid_game_init`, ...),
// so that several games, each with its own copy of the runtime and its
// state, link into one program.

#include "cpu.h"
#include "video.h"
#include "render.h"
#include "apu.h"
#include "sound.h"
#include "state.h"
#include "layers.h"
#ifdef MAPPER
#include "cartridge.h"
#include "mapper.h"
#endif

// Starts the game. `data` is the CHR (8 KiB) for SMB, or for a cartridge
// the iNES image with its PRG banks left out (the recompiled program has
// them). Returns 0 on success.
int game_init(const uint8_t *data, size_t size, size_t sample_rate) {
    cpu_init();
    apu_init(sample_rate);
#ifdef MAPPER
    if (load_cartridge_image(data, size)) return 1;
    mapper_init(cartridge_mapper);
    mapper_mirroring(cartridge_vertical);
#else
    if (size < 0x2000) return 1;
    video_init(data);
    render_init();
#endif
    cpu_reset_entry();
    return 0;
}

void game_controller(uint8_t state) { update_controller1(state); }
void game_frame(void) { next_frame(); }
void game_end_frame(void) { sound_end_frame(); }
void game_audio(float *out, size_t count) { apu_render(out, count); }

// The flat picture: 256x240, RGB888.
const uint8_t *game_render(void) {
    render_frame();
    return frame;
}

// The picture in layers (layers.h); false when the frame needs the flat
// picture. SMB places its background from its level data; the others keep
// their background on the screen and their sprites in front.
bool game_layers(const LayerTarget *target) {
#ifdef SMB_LEVEL
    LayerLevel level = layers_smb_level();
#else
    LayerLevel level = { 0 };
#endif
    return layers_render(&level, target);
}

const uint8_t *game_materials(void) { return layer_background_material; }
uint8_t game_backdrop(void) { return layer_backdrop; }

uint32_t game_state_size(void) { return SAVE_STATE_SIZE; }
void game_save(uint8_t *dest) { save_state(dest); }
void game_load(uint8_t *state) { load_state(state); }
