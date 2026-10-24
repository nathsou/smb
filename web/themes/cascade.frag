// One cascade of radiance cascades (Alexander Sannikov's 2D global
// illumination). In cascade n, probes 2^(n+1) pixels apart each cast 4^(n+1)
// rays, one per texel: a probe's rays fill a 2^(n+1) square, so every cascade
// is the size of the scene. Each ray covers [start, end) of the distance from
// its probe, four times longer than the cascade below's, marching through the
// distance field of the jump flood. A ray that hits nothing there takes the
// radiance of the four rays of cascade n + 1 that split its direction, between
// the four nearest probes of that cascade; past the last cascade, the sky.

uniform sampler2D u_seeds;          // the jump flood: nearest occluder in .xy
uniform sampler2D u_background;
uniform sampler2D u_sprite;
uniform sampler2D u_previous_light; // surface.frag, last frame
uniform sampler2D u_upper;          // cascade n + 1
uniform int u_cascade;
uniform int u_cascades;
uniform int u_scroll_delta;         // pixels the screen scrolled since last frame
uniform vec3 u_sky;                 // sky light (none in dark areas)
uniform vec3 u_ambient;             // light from everywhere
uniform float u_bounce;

out vec4 o_radiance;

const float PI = 3.14159265;
const vec3 LAVA = vec3(1.0, 0.13, 0.012); // lava's light, whatever its pixels' colour (white foam)
const float INTERVAL = 2.0; // cascade 0's ray length

// Light from beyond the scene: brighter from above.
vec3 sky(vec2 direction) {
    return u_ambient + u_sky * (0.25 + 0.75 * smoothstep(-0.2, 0.9, -direction.y));
}

// What an occluder sends back: its own light, and last frame's light on it
// reflected once.
vec3 emission(ivec2 p) {
    if (p.y >= 240) return vec3(0.0);
    vec4 sprite = texelFetch(u_sprite, p, 0);
    if (material_of(sprite) == S_FIRE) return to_linear(sprite.rgb) * 3.0;
    vec4 background = texelFetch(u_background, p, 0);
    int material = material_of(background);
    vec3 albedo = to_linear(background.rgb);
    vec3 light = vec3(0.0);
    if (material == M_LAVA) light = LAVA * 1.6;
    else if (material == M_COIN) light = albedo * 1.5;
    else if (material == M_QUESTION) light = albedo * 0.5;
    ivec2 before = clamp(p + ivec2(u_scroll_delta, 0), ivec2(0), ivec2(SCENE_WIDTH - 1, SCENE_HEIGHT - 1));
    return light + albedo * texelFetch(u_previous_light, before, 0).rgb * u_bounce;
}

// The radiance along one ray over [start, end), and in .a, 1 when it hit
// nothing.
vec4 march(vec2 origin, vec2 direction, float start, float end) {
    float t = start;
    for (int i = 0; i < 32 && t < end; i++) {
        vec2 p = origin + direction * t;
        if (p.x < 0.0 || p.y < 0.0 || p.x >= float(SCENE_WIDTH) || p.y >= float(SCENE_HEIGHT)) {
            return vec4(sky(direction), 0.0);
        }
        ivec2 cell = ivec2(p);
        float d = distance(vec2(cell), texelFetch(u_seeds, cell, 0).xy);
        if (d < 0.5) return vec4(emission(cell), 0.0);
        t += max(d - 0.75, 0.5);
    }
    return vec4(0.0, 0.0, 0.0, 1.0);
}

// The average of the four rays of cascade n + 1 under ray `index` of a probe.
vec3 upper_rays(ivec2 probe, int index, int size) {
    probe = clamp(probe, ivec2(0), ivec2(SCENE_WIDTH, SCENE_HEIGHT) / size - 1);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        int ray = index * 4 + i;
        sum += texelFetch(u_upper, probe * size + ivec2(ray % size, ray / size), 0).rgb;
    }
    return sum * 0.25;
}

vec3 merge(vec2 origin, int index, int size) {
    int upper = size * 2;
    vec2 at = origin / float(upper) - 0.5;
    ivec2 probe = ivec2(floor(at));
    vec2 f = fract(at);
    return mix(
        mix(upper_rays(probe, index, upper), upper_rays(probe + ivec2(1, 0), index, upper), f.x),
        mix(upper_rays(probe + ivec2(0, 1), index, upper), upper_rays(probe + ivec2(1, 1), index, upper), f.x),
        f.y);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    int size = 2 << u_cascade; // probe spacing, and the square root of the ray count
    ivec2 probe = texel / size;
    ivec2 slot = texel % size;
    int index = slot.x + slot.y * size;
    vec2 origin = (vec2(probe) + 0.5) * float(size);

    float angle = (float(index) + 0.5) * 2.0 * PI / float(size * size);
    vec2 direction = vec2(cos(angle), sin(angle));
    float start = INTERVAL * (pow(4.0, float(u_cascade)) - 1.0) / 3.0;
    float end = INTERVAL * (pow(4.0, float(u_cascade + 1)) - 1.0) / 3.0;

    vec4 radiance = march(origin, direction, start, end);
    if (radiance.a > 0.5) {
        radiance.rgb = u_cascade == u_cascades - 1 ? sky(direction) : merge(origin, index, size);
    }
    o_radiance = vec4(radiance.rgb, 1.0);
}
