// The 2D global illumination theme's picture: the frame's colours times the
// light (surface.frag, plus the floor), fire and lava glowing. The backdrop
// is not lit: it shows its colour, and over it the light that emitters add,
// so that fire glows in the dark. The status bar is unlit.

uniform sampler2D u_background;
uniform sampler2D u_sprite;
uniform sampler2D u_light;
uniform vec3 u_open;  // the light in open space, without emitters
uniform vec3 u_floor; // light everywhere

out vec4 o_color;

void main() {
    ivec2 p = ivec2(int(gl_FragCoord.x), 239 - int(gl_FragCoord.y)) + u_origin;
    vec4 background = texelFetch(u_background, p, 0);
    vec4 sprite = texelFetch(u_sprite, p, 0);
    int material = material_of(background);
    int sprite_material = material_of(sprite);
    vec3 base = sprite_material != S_NONE ? sprite.rgb : background.rgb;
    if (material == M_HUD) {
        o_color = vec4(base, 1.0);
        return;
    }

    vec3 albedo = to_linear(base);
    vec3 light = texelFetch(u_light, p, 0).rgb;
    vec3 c = albedo * (light + u_floor);
    if (sprite_material == S_NONE && material == M_BACKDROP) {
        c = albedo + 0.08 * max(light - u_open, 0.0);
    } else if (sprite_material == S_FIRE) {
        c = albedo * 2.5;
    } else if (sprite_material == S_NONE) {
        if (material == M_LAVA) c = albedo * max(light + u_floor, vec3(1.6));
        else if (material == M_COIN) c += albedo * 0.4;
    }
    o_color = vec4(to_display(c), 1.0);
}
