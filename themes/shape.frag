// The 3D theme's shapes, made from the level data and the pixels alone: for
// each scene pixel, the depths its background and its sprite cover, as a
// centre and a half thickness (0: nothing). Depth 0 is the play plane,
// positive is away from the viewer.
//
// - Blocks (each solid metatile) are cubes 16 deep with bevelled edges; the
//   floor's (the bottom two rows) reach back under the scenery, as a stage.
// - Pipes are cylinders: the axis runs between the two halves of a pipe,
//   vertical ($10-$15) or sideways ($1c-$21).
// - Coins and climbables (vines, the flagpole) are thin.
// - Scenery (hills, bushes, clouds, castles) and sprites are inflated from
//   their silhouettes: thicker the farther a pixel is from the edge.
//
// Without SMB's level data (other games) every opaque background pixel is a
// block, on the 16-pixel grid of the level, set back so that the sprites
// stand in front of the walls they overlap, and there is no floor.

uniform sampler2D u_background;
uniform sampler2D u_sprite;
uniform sampler2D u_metatile;
uniform sampler2D u_edges; // the jump flood of edges.frag
uniform bool u_level_known;

out vec4 o_shape; // background centre, half thickness; sprite centre, half thickness

const float BLOCK = 8.0;
const float BEVEL = 3.0;
const float PIPE = 16.0;
const float FLOOR_BACK = 40.0;
const float SCENERY_DEPTH = 20.0;
const float SCENERY_HALF = 14.0;
const float SCENERY_ROUND = 32.0;
const float SPRITE_HALF = 5.0;
const float SPRITE_ROUND = 6.0;
const float SET_BACK = 8.0; // blocks without level data

// A quarter circle: 0 at the edge, 1 from `radius` in.
float round_profile(float d, float radius) {
    float t = clamp(d / radius, 0.0, 1.0);
    return sqrt(t * (2.0 - t));
}

vec2 background_shape(ivec2 p, int material, int metatile) {
    if (material == M_BACKDROP || material == M_HUD) return vec2(0.0);
    float lx = float(p.x % 16) + 0.5;
    float ly = float(p.y % 16) + 0.5;
    if (metatile >= 0x10 && metatile <= 0x15) {
        float dx = metatile % 2 == 0 ? 16.0 - lx : lx; // left halves are even
        return vec2(0.0, sqrt(max(PIPE * PIPE - dx * dx, 4.0)));
    }
    if (metatile >= 0x1c && metatile <= 0x21) {
        float dy = metatile <= 0x1e ? 16.0 - ly : ly; // the top row is $1c-$1e
        return vec2(0.0, sqrt(max(PIPE * PIPE - dy * dy, 4.0)));
    }
    if (material == M_SOLID || material == M_QUESTION) {
        float d = min(min(lx, 16.0 - lx), min(ly, 16.0 - ly));
        float front = BLOCK - BEVEL + BEVEL * round_profile(d, BEVEL);
        if (!u_level_known) return vec2(SET_BACK, front);
        if (p.y - u_origin.y >= 208) return vec2((FLOOR_BACK - front) * 0.5, (FLOOR_BACK + front) * 0.5);
        return vec2(0.0, front);
    }
    if (material == M_LAVA) return vec2(0.0, BLOCK);
    if (material == M_COIN) return vec2(0.0, 1.5);
    if (material == M_CLIMB) return vec2(0.0, 1.5);
    float d = distance(vec2(p), texelFetch(u_edges, p, 0).xy) - 0.5;
    return vec2(SCENERY_DEPTH, max(SCENERY_HALF * round_profile(d, SCENERY_ROUND), 1.0));
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int material = material_of(texelFetch(u_background, p, 0));
    int metatile = int(texelFetch(u_metatile, p, 0).r * 255.0 + 0.5);
    vec2 background = background_shape(p, material, metatile);
    vec2 sprite = vec2(0.0);
    if (material != M_HUD && material_of(texelFetch(u_sprite, p, 0)) != S_NONE) {
        float d = distance(vec2(p), texelFetch(u_edges, p, 0).zw) - 0.5;
        sprite = vec2(0.0, max(SPRITE_HALF * round_profile(d, SPRITE_ROUND), 1.0));
    }
    o_shape = vec4(background, sprite);
}
