// The light on each scene pixel: the irradiance of cascade 0 (its four rays,
// between the four nearest probes). An occluder takes the light of its
// nearest free pixel, dimming with depth, so that surfaces are lit from the
// side light comes from. u_floor is light that reaches everywhere, enclosed
// or not.

uniform sampler2D u_cascade0;
uniform sampler2D u_seeds;
uniform vec3 u_floor;

out vec4 o_light;

vec3 probe_irradiance(ivec2 probe) {
    probe = clamp(probe, ivec2(0), ivec2(SCENE_WIDTH, SCENE_HEIGHT) / 2 - 1);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; i++) sum += texelFetch(u_cascade0, probe * 2 + ivec2(i & 1, i >> 1), 0).rgb;
    return sum * 0.25;
}

vec3 irradiance(vec2 p) {
    vec2 at = p / 2.0 - 0.5;
    ivec2 probe = ivec2(floor(at));
    vec2 f = fract(at);
    return mix(
        mix(probe_irradiance(probe), probe_irradiance(probe + ivec2(1, 0)), f.x),
        mix(probe_irradiance(probe + ivec2(0, 1)), probe_irradiance(probe + ivec2(1, 1)), f.x),
        f.y);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 here = vec2(p);
    vec4 seeds = texelFetch(u_seeds, p, 0);
    if (distance(here, seeds.xy) >= 0.5) {
        o_light = vec4(irradiance(here + 0.5) + u_floor, 1.0);
        return;
    }
    float depth = distance(here, seeds.zw);
    o_light = vec4(irradiance(seeds.zw + 0.5) * (0.6 + 0.4 * exp(-max(depth - 1.0, 0.0) / 12.0)) + u_floor, 1.0);
}
