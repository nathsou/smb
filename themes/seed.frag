// Seeds of the jump flood (jfa.frag): an occluder's own coordinates in .xy,
// a free pixel's in .zw. Occluders are what light does not pass through:
// solid metatiles, lava, coins and fire (the emitters, so that rays find
// them), and the rows below the screen (the ground goes on).

uniform sampler2D u_background;
uniform sampler2D u_sprite;

out vec4 o_seed;

const float NONE = -10000.0;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int material = material_of(texelFetch(u_background, p, 0));
    bool occluder = below_screen(p.y) || material == M_SOLID || material == M_QUESTION || material == M_LAVA ||
        material == M_COIN || material_of(texelFetch(u_sprite, p, 0)) == S_FIRE;
    vec2 here = vec2(p);
    o_seed = occluder ? vec4(here, NONE, NONE) : vec4(NONE, NONE, here);
}
