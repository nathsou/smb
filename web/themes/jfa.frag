// One step of the jump flood: each pixel keeps the nearest occluder (.xy)
// and the nearest free pixel (.zw) among its own seeds and those u_step
// pixels away. Steps of 256, 128, ..., 1 give every pixel both.

uniform sampler2D u_seeds;
uniform int u_step;

out vec4 o_seed;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 here = vec2(p);
    vec4 best = texelFetch(u_seeds, p, 0);
    float occluder = distance(here, best.xy);
    float free = distance(here, best.zw);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 q = p + ivec2(dx, dy) * u_step;
            if (q.x < 0 || q.y < 0 || q.x >= SCENE_WIDTH || q.y >= SCENE_HEIGHT) continue;
            vec4 seeds = texelFetch(u_seeds, q, 0);
            float d = distance(here, seeds.xy);
            if (d < occluder) {
                occluder = d;
                best.xy = seeds.xy;
            }
            d = distance(here, seeds.zw);
            if (d < free) {
                free = d;
                best.zw = seeds.zw;
            }
        }
    }
    o_seed = best;
}
