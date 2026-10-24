// 2.5D: the scene as voxels, ray traced. Every opaque pixel is a column of
// voxels over a range of depths its material gives: solid metatiles deep,
// sprites thinner, scenery behind. The view is a cabinet projection: parallel
// rays that go into the screen and a little down and right, so the play
// plane (depth 0) stays where the game draws it while tops and sides show.
// Surfaces are lit by the sun and by fire, with shadow rays through the
// voxels, and by the 2D global illumination (light.js) as the light from
// everywhere else, dimmed by ambient occlusion.

uniform sampler2D u_background;
uniform sampler2D u_sprite;
uniform sampler2D u_light;
uniform float u_scale;          // canvas pixels per NES pixel
uniform vec3 u_sun;             // towards the sun
uniform vec3 u_sun_color;       // faint in dark areas
uniform float u_ambient;        // how much of the 2D light to use
uniform int u_light_count;
uniform vec4 u_lights[8];       // position, radius
uniform vec3 u_light_colors[8];

out vec4 o_color;

const vec3 VIEW = vec3(0.22, 0.38, 1.0); // a primary ray's step per unit of depth
const float NEAR = -12.0;                // in front of every voxel
const float BACK = 11.0;                 // behind every voxel
const float FRONT = -8.0;

const int NONE = 0;
const int BACKGROUND = 1;
const int SPRITE = 2;

struct Hit {
    float t;
    vec3 normal;
    ivec2 cell;
    int layer;
};

// The depths of a background material's voxels.
bool background_slab(int material, out vec2 range) {
    if (material == M_SOLID || material == M_QUESTION || material == M_LAVA) range = vec2(-8.0, 8.0);
    else if (material == M_COIN) range = vec2(-2.0, 2.0);
    else if (material == M_CLIMB) range = vec2(-1.0, 1.0);
    else if (material == M_SCENERY) range = vec2(8.0, 11.0);
    else return false;
    return true;
}

// Where a ray inside a cell over [t0, t1] first has a depth within range, or
// -1; the normal is the face it enters by (`side` when it enters the cell
// already within range).
float enter(vec3 origin, vec3 direction, float t0, float t1, vec2 range, vec3 side, out vec3 normal) {
    float z = origin.z + direction.z * t0;
    if (z >= range.x && z <= range.y) {
        normal = side;
        return t0;
    }
    if (direction.z > 0.0 && z < range.x) {
        float t = (range.x - origin.z) / direction.z;
        normal = vec3(0.0, 0.0, -1.0);
        return t <= t1 ? t : -1.0;
    }
    if (direction.z < 0.0 && z > range.y) {
        float t = (range.y - origin.z) / direction.z;
        normal = vec3(0.0, 0.0, 1.0);
        return t <= t1 ? t : -1.0;
    }
    return -1.0;
}

// The first voxel along a ray, up to t_max: a walk over the pixel grid
// (Amanatides & Woo), each cell checked over the depths the ray crosses it at.
Hit trace(vec3 origin, vec3 direction, float t_max) {
    Hit hit;
    hit.layer = NONE;
    ivec2 cell = ivec2(floor(origin.xy));
    vec2 step = vec2(direction.x >= 0.0 ? 1.0 : -1.0, direction.y >= 0.0 ? 1.0 : -1.0);
    vec2 delta = 1.0 / max(abs(direction.xy), vec2(1e-6));
    vec2 next = (step * (vec2(cell) - origin.xy) + max(step, 0.0)) * delta;
    float t = 0.0;
    vec3 side = vec3(0.0);
    for (int i = 0; i < 80; i++) {
        float z = origin.z + direction.z * t;
        if ((direction.z >= 0.0 && z > BACK) || (direction.z <= 0.0 && z < FRONT)) break;
        float t_exit = min(min(next.x, next.y), t_max);
        bool inside = cell.x >= -SCENE_MARGIN && cell.x < 256 + SCENE_MARGIN && cell.y >= 0 && cell.y < 240;
        if (inside) {
            ivec2 at = cell + ivec2(SCENE_MARGIN, 0);
            int background = material_of(texelFetch(u_background, at, 0));
            if (background != M_HUD) {
                vec2 range;
                vec3 normal;
                float best = 1e9;
                if (background_slab(background, range)) {
                    float t_hit = enter(origin, direction, t, t_exit, range, side, normal);
                    if (t_hit >= 0.0) {
                        best = t_hit;
                        hit.normal = normal;
                        hit.layer = BACKGROUND;
                    }
                }
                if (material_of(texelFetch(u_sprite, at, 0)) != S_NONE) {
                    float t_hit = enter(origin, direction, t, t_exit, vec2(-3.0, 3.0), side, normal);
                    if (t_hit >= 0.0 && t_hit < best) {
                        best = t_hit;
                        hit.normal = normal;
                        hit.layer = SPRITE;
                    }
                }
                if (hit.layer != NONE) {
                    hit.t = best;
                    hit.cell = cell;
                    return hit;
                }
            }
        } else if ((cell.y < 0 && step.y < 0.0) || (cell.y >= 240 && step.y > 0.0) ||
                   (cell.x < -SCENE_MARGIN && step.x < 0.0) || (cell.x >= 256 + SCENE_MARGIN && step.x > 0.0)) {
            break;
        }
        if (t_exit >= t_max) break;
        if (next.x < next.y) {
            cell.x += int(step.x);
            t = next.x;
            next.x += delta.x;
            side = vec3(-step.x, 0.0, 0.0);
        } else {
            cell.y += int(step.y);
            t = next.y;
            next.y += delta.y;
            side = vec3(0.0, -step.y, 0.0);
        }
    }
    return hit;
}

bool blocked(vec3 origin, vec3 direction, float t_max) {
    return trace(origin, direction, t_max).layer != NONE;
}

// The share of five short rays around the normal that get out.
float ambient_occlusion(vec3 origin, vec3 normal) {
    vec3 u = abs(normal.z) > 0.5 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 v = cross(normal, u);
    float open = blocked(origin, normal, 5.0) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal + u), 5.0) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal - u), 5.0) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal + v), 5.0) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal - v), 5.0) ? 0.0 : 1.0;
    return 0.25 + 0.75 * open / 5.0;
}

vec4 layer_at(ivec2 at, int layer) {
    return layer == SPRITE ? texelFetch(u_sprite, at, 0) : texelFetch(u_background, at, 0);
}

// A side face takes the brightest colour of the edge pixel and the two inside
// it: tiles are outlined in black, which would make every side black.
vec4 side_color(ivec2 at, ivec2 inward, int layer) {
    vec4 best = layer_at(at, layer);
    float brightest = dot(best.rgb, vec3(0.3, 0.6, 0.1));
    for (int k = 1; k <= 2; k++) {
        vec4 c = layer_at(at + inward * k, layer);
        if (material_of(c) == 0) break;
        float brightness = dot(c.rgb, vec3(0.3, 0.6, 0.1));
        if (brightness > brightest) {
            best = c;
            brightest = brightness;
        }
    }
    return best;
}

void main() {
    vec2 screen = vec2(gl_FragCoord.x, 240.0 * u_scale - gl_FragCoord.y) / u_scale; // y down
    ivec2 pixel = ivec2(screen) + ivec2(SCENE_MARGIN, 0);
    vec4 status_bar = texelFetch(u_background, pixel, 0);
    if (material_of(status_bar) == M_HUD) {
        vec4 sprite = texelFetch(u_sprite, pixel, 0);
        o_color = vec4(material_of(sprite) != S_NONE ? sprite.rgb : status_bar.rgb, 1.0);
        return;
    }

    vec3 origin = vec3(screen + VIEW.xy * NEAR, NEAR);
    Hit hit = trace(origin, VIEW, BACK - NEAR);
    if (hit.layer == NONE) {
        o_color = vec4((u_mask & 0x08) != 0 ? color(u_palette[0]) : vec3(0.0), 1.0);
        return;
    }

    ivec2 at = hit.cell + ivec2(SCENE_MARGIN, 0);
    vec4 layer = layer_at(at, hit.layer);
    int material = material_of(layer);
    if (hit.normal.z == 0.0) layer = side_color(at, -ivec2(hit.normal.xy), hit.layer);
    vec3 albedo = to_linear(layer.rgb);
    vec3 p = origin + VIEW * hit.t;
    vec3 n = hit.normal;
    vec3 surface = p + n * 0.02;

    vec3 light = texelFetch(u_light, at, 0).rgb * u_ambient * ambient_occlusion(surface, n);
    float sun = dot(n, u_sun);
    if (sun > 0.0 && u_sun_color != vec3(0.0) && !blocked(surface, u_sun, 48.0)) light += u_sun_color * sun;
    for (int i = 0; i < 8; i++) {
        if (i >= u_light_count) break;
        vec3 to = u_lights[i].xyz - p;
        float d = length(to);
        vec3 l = to / d;
        float facing = dot(n, l);
        if (facing <= 0.0 || blocked(surface, l, d)) continue;
        float r = u_lights[i].w;
        light += u_light_colors[i] * facing / (1.0 + d * d / (r * r));
    }

    vec3 c = albedo * light;
    if (hit.layer == SPRITE && material == S_FIRE) c += albedo * 2.5;
    else if (hit.layer == BACKGROUND && material == M_LAVA) c += albedo * 1.2;
    else if (hit.layer == BACKGROUND && material == M_COIN) c += albedo * 0.3;
    o_color = vec4(to_display(c), 1.0);
}
