// Seeds of a jump flood (jfa.frag) for the silhouettes the 3D theme inflates:
// in .xy every pixel that is not scenery, in .zw every pixel without a
// sprite, so that the flood gives scenery and sprite pixels their distance to
// the edge of their shape.

uniform sampler2D u_background;
uniform sampler2D u_sprite;

out vec4 o_seed;

const float NONE = -10000.0;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 here = vec2(p);
    bool scenery = material_of(texelFetch(u_background, p, 0)) == M_SCENERY;
    bool sprite = material_of(texelFetch(u_sprite, p, 0)) != S_NONE;
    o_seed = vec4(scenery ? vec2(NONE) : here, sprite ? vec2(NONE) : here);
}
