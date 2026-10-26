// Shared by the theme shaders, after video.frag (as a library).
//
// The scene (scene.frag) is the frame split into a background and a sprite
// layer, 320x288, y down from the top, rows off the screen empty. Each
// layer's alpha is a material: what the game's level data says the pixel is
// (SMB's), or what the picture suggests. The screen's top-left corner is at
// scene u_origin, x 16 to 47 and y 0 to 31: the scene moves with the world
// in steps of 32 pixels, so that the light's probes (2 to 32 pixels apart)
// stay in place on the level as it scrolls.

const int SCENE_WIDTH = 320;
const int SCENE_HEIGHT = 288;

uniform ivec2 u_origin;

// background materials
const int M_BACKDROP = 0; // transparent: the backdrop colour shows
const int M_SCENERY = 1;  // hills, clouds, bushes, castles, text: not in the block buffer
const int M_SOLID = 2;    // ground, bricks, blocks, pipes (and, without level data, the background)
const int M_CLIMB = 3;    // vines, flagpole
const int M_COIN = 4;
const int M_QUESTION = 5;
const int M_LAVA = 6;     // castle lava
const int M_HUD = 7;      // a fixed status bar

// sprite materials
const int S_NONE = 0;
const int S_BODY = 1;
const int S_FIRE = 2;     // fireballs, firebars, Bowser's flame

int material_of(vec4 layer) {
    return int(layer.a * 255.0 + 0.5);
}

// Whether a scene row is below the screen.
bool below_screen(int y) {
    return y - u_origin.y >= 240;
}

// The NES palette is sRGB-like: light is added in linear space.
vec3 to_linear(vec3 c) {
    return pow(c, vec3(2.2));
}

// Linear to display, with a soft shoulder above 0.8 so lights do not clip
// (values up to 0.8 are unchanged: fully lit surfaces keep their colour).
vec3 to_display(vec3 c) {
    vec3 over = max(c - 0.8, 0.0);
    c = min(c, 0.8) + 0.2 * (1.0 - exp(-over / 0.2));
    return pow(c, vec3(1.0 / 2.2));
}
