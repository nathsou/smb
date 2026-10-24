// 3D: the shapes of shape.frag seen through a perspective camera, slightly
// from above, ray traced. Each scene pixel is a column covering the depths of
// its background shape and of its sprite shape; rays walk the pixel grid
// (Amanatides & Woo) and check the depths they cross each column at.
// Inflated shapes (scenery, sprites) are refined between pixels: their
// thickness is interpolated where the ray meets them, for the depth and the
// normal, so they shade smoothly while their outlines stay pixel art. Blocks
// and pipes keep their exact shapes. Lit as the 2.5D theme: the sun and fire
// with shadow rays, ambient occlusion, and the 2D global illumination, with
// more of it from above than from below, a soft highlight where the sun
// glints, and the backdrop's colour a little over what is far back.

uniform sampler2D u_background;
uniform sampler2D u_sprite;
uniform sampler2D u_shape;
uniform sampler2D u_metatile;    // scene.frag's: metatile / 255
uniform sampler2D u_light;
uniform vec3 u_floor;           // light to add to u_light everywhere
uniform float u_scale;          // canvas pixels per NES pixel
uniform vec3 u_camera;          // position
uniform vec3 u_forward;         // unit vector to the screen's centre
uniform vec3 u_down;            // unit vector down the screen
uniform float u_focal;          // distance at which a pixel spans a NES pixel
uniform vec3 u_sun;             // towards the sun
uniform vec3 u_sun_color;
uniform int u_jitter;           // 1: each pixel of a 2x2 square sees the sun from its own spot
uniform float u_ambient;        // how much of the 2D light to use
uniform int u_light_count;
uniform vec4 u_lights[8];       // position, radius
uniform vec3 u_light_colors[8];

out vec4 o_color;

const float NEAR = -20.0; // in front of every shape
const float HAZE_START = 18.0; // depth from which far shapes fade towards the backdrop
const float HAZE_MAX = 0.12;
const float FAR = 44.0;   // behind every shape

const int NONE = 0;
const int BACKGROUND = 1;
const int SPRITE = 2;

struct Hit {
    float t;
    float t_enter; // where the ray entered the column
    float t_exit;  // and left it
    vec3 side;     // the face of the column the ray came in by, 0 when its front or back
    ivec2 cell;
    int layer;
};

const ivec2 NO_CELL = ivec2(-100000);

// The scene pixel of a column in screen coordinates. Rows below the screen
// repeat the bottom row of blocks, so that the ground goes on down.
ivec2 scene_at(ivec2 cell) {
    return ivec2(cell.x + u_origin, cell.y < 240 ? cell.y : 224 + (cell.y - 240) % 16);
}

vec4 shape_at(ivec2 cell) {
    ivec2 at = scene_at(cell);
    if (at.x < 0 || at.x >= SCENE_WIDTH || cell.y < 0) return vec4(0.0);
    vec4 shape = texelFetch(u_shape, at, 0);
    if (cell.y >= 240) {
        int material = material_of(texelFetch(u_background, at, 0));
        return material == M_SOLID ? vec4(shape.xy, 0.0, 0.0) : vec4(0.0);
    }
    return shape;
}

vec4 layer_color(ivec2 cell, int layer) {
    ivec2 at = scene_at(cell);
    return layer == SPRITE ? texelFetch(u_sprite, at, 0) : texelFetch(u_background, at, 0);
}

// Where a ray inside a column over [t0, t1] first has a depth within
// [z0, z1], or -1; by_side is whether it came in by the column's side.
float enter(vec3 origin, vec3 direction, float t0, float t1, float z0, float z1, out bool by_side) {
    float z = origin.z + direction.z * t0;
    by_side = z >= z0 && z <= z1;
    if (by_side) return t0;
    float t = -1.0;
    if (direction.z > 0.0 && z < z0) t = (z0 - origin.z) / direction.z;
    if (direction.z < 0.0 && z > z1) t = (z1 - origin.z) / direction.z;
    return t >= 0.0 && t <= t1 ? t : -1.0;
}

// The first column along a ray, up to t_max, other than `skip`.
Hit trace(vec3 origin, vec3 direction, float t_max, ivec2 skip) {
    Hit hit;
    hit.layer = NONE;
    ivec2 cell = ivec2(floor(origin.xy));
    vec2 step = vec2(direction.x >= 0.0 ? 1.0 : -1.0, direction.y >= 0.0 ? 1.0 : -1.0);
    vec2 delta = 1.0 / max(abs(direction.xy), vec2(1e-6));
    vec2 next = (step * (vec2(cell) - origin.xy) + max(step, 0.0)) * delta;
    float t = 0.0;
    vec3 side = vec3(0.0);
    for (int i = 0; i < 128; i++) {
        float z = origin.z + direction.z * t;
        if ((direction.z >= 0.0 && z > FAR) || (direction.z <= 0.0 && z < NEAR)) break;
        if ((cell.y < 0 && step.y < 0.0) || (cell.x + u_origin < 0 && step.x < 0.0) ||
            (cell.x + u_origin >= SCENE_WIDTH && step.x > 0.0)) break;
        float t_exit = min(min(next.x, next.y), t_max);
        vec4 shape = cell == skip ? vec4(0.0) : shape_at(cell);
        float best = 1e9;
        bool by_side;
        if (shape.y > 0.0) {
            float t_hit = enter(origin, direction, t, t_exit, shape.x - shape.y, shape.x + shape.y, by_side);
            if (t_hit >= 0.0) {
                best = t_hit;
                hit.layer = BACKGROUND;
                hit.side = by_side ? side : vec3(0.0);
            }
        }
        if (shape.w > 0.0) {
            float t_hit = enter(origin, direction, t, t_exit, shape.z - shape.w, shape.z + shape.w, by_side);
            if (t_hit >= 0.0 && t_hit < best) {
                best = t_hit;
                hit.layer = SPRITE;
                hit.side = by_side ? side : vec3(0.0);
            }
        }
        if (hit.layer != NONE) {
            hit.t = best;
            hit.t_enter = t;
            hit.t_exit = t_exit;
            hit.cell = cell;
            return hit;
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

bool blocked(vec3 origin, vec3 direction, float t_max, ivec2 skip) {
    return trace(origin, direction, t_max, skip).layer != NONE;
}

float half_thickness(ivec2 cell, int layer) {
    vec4 shape = shape_at(cell);
    return layer == SPRITE ? shape.w : shape.y;
}

// The half thickness of one shape at a column: 0 where another shape is
// (another layer, or a background at another depth).
float shape_half(ivec2 cell, int layer, float centre) {
    vec4 shape = shape_at(cell);
    if (layer == SPRITE) return shape.w;
    return abs(shape.x - centre) < 0.5 ? shape.y : 0.0;
}

// The half thickness of a shape at any point, between the centres of the
// four nearest columns.
float smooth_half(vec2 p, int layer, float centre) {
    vec2 a = p - 0.5;
    ivec2 i = ivec2(floor(a));
    vec2 f = a - floor(a);
    return mix(
        mix(shape_half(i, layer, centre), shape_half(i + ivec2(1, 0), layer, centre), f.x),
        mix(shape_half(i + ivec2(0, 1), layer, centre), shape_half(i + ivec2(1, 1), layer, centre), f.x),
        f.y);
}

bool inflated(Hit hit) {
    return hit.layer == SPRITE || material_of(layer_color(hit.cell, BACKGROUND)) == M_SCENERY;
}

float centre_of(Hit hit) {
    vec4 shape = shape_at(hit.cell);
    return hit.layer == SPRITE ? shape.z : shape.x;
}

// Where a ray that reached an inflated shape's column meets its smooth
// surface, within the column.
float refine(Hit hit, vec3 origin, vec3 direction) {
    float centre = centre_of(hit);
    float t = hit.t;
    for (int i = 0; i < 4; i++) {
        vec3 p = origin + direction * t;
        float z = centre - smooth_half(p.xy, hit.layer, centre);
        t = clamp((z - origin.z) / direction.z, hit.t_enter, hit.t_exit);
    }
    return t;
}

// The normal of a column's surface: from how its half thickness changes
// across neighbouring columns (none: the edge, at the centre), facing the
// viewer on the front half and away on the back half; a ray that came in by
// a side leans it towards that side. Inflated shapes take it from their
// smooth surface at the point itself, whichever way the ray came in.
vec3 surface_normal(Hit hit, vec3 p) {
    float centre = centre_of(hit);
    float facing = p.z < centre ? -1.0 : 1.0;
    if (inflated(hit)) {
        float dx = smooth_half(p.xy + vec2(0.5, 0.0), hit.layer, centre) -
            smooth_half(p.xy - vec2(0.5, 0.0), hit.layer, centre);
        float dy = smooth_half(p.xy + vec2(0.0, 0.5), hit.layer, centre) -
            smooth_half(p.xy - vec2(0.0, 0.5), hit.layer, centre);
        return normalize(vec3(-dx, -dy, facing));
    }
    float dx = half_thickness(hit.cell + ivec2(1, 0), hit.layer) - half_thickness(hit.cell - ivec2(1, 0), hit.layer);
    float dy = half_thickness(hit.cell + ivec2(0, 1), hit.layer) - half_thickness(hit.cell - ivec2(0, 1), hit.layer);
    vec3 n = normalize(vec3(-dx * 0.5, -dy * 0.5, facing));
    return normalize(n + 2.0 * hit.side);
}

// A side takes the brightest of the edge pixel and the two inside it: tiles
// are outlined in black.
vec4 side_color(ivec2 cell, ivec2 inward, int layer) {
    vec4 best = layer_color(cell, layer);
    float brightest = dot(best.rgb, vec3(0.3, 0.6, 0.1));
    for (int k = 1; k <= 2; k++) {
        vec4 c = layer_color(cell + inward * k, layer);
        if (material_of(c) == 0) break;
        float brightness = dot(c.rgb, vec3(0.3, 0.6, 0.1));
        if (brightness > brightest) {
            best = c;
            brightest = brightness;
        }
    }
    return best;
}

// A block's top, bottom and sides show its own tile, as a cube's faces
// would: depth from its front picks the row (top and bottom) or the column
// (sides) within its 16x16 metatile, every 16 pixels.
vec4 block_face_color(Hit hit, float z) {
    vec4 shape = shape_at(hit.cell);
    int depth = int(mod(z - (shape.x - shape.y), 16.0));
    ivec2 at = scene_at(hit.cell);
    ivec2 corner = hit.cell - ivec2(at.x % 16, at.y % 16);
    ivec2 cell = hit.side.y != 0.0 ? ivec2(hit.cell.x, corner.y + depth) : ivec2(corner.x + depth, hit.cell.y);
    return layer_color(cell, BACKGROUND);
}

int metatile_at(ivec2 cell) {
    return int(texelFetch(u_metatile, scene_at(cell), 0).r * 255.0 + 0.5);
}

bool upright_pipe(int metatile) {
    return metatile >= 0x10 && metatile <= 0x15;
}

bool sideways_pipe(int metatile) {
    return metatile >= 0x1c && metatile <= 0x21;
}

const float PIPE_RADIUS = 16.0; // as shape.frag's
const float PIPE_HOLE = 11.0;

// The open end of a pipe, for a ray that came in by the end of a pipe's
// column (the top of an upright pipe, the side of a sideways one): a dark
// hole inside a rim of the pipe's colour, which a dark edge outlines as the
// front's outline does. False for any other face.
bool pipe_end(Hit hit, vec3 p, int metatile, out vec3 albedo) {
    ivec2 at = scene_at(hit.cell);
    ivec2 corner = hit.cell - ivec2(at.x % 16, at.y % 16);
    float r;
    vec4 rim;
    if (upright_pipe(metatile) && hit.side.y != 0.0) {
        float axis = float(corner.x) + (metatile % 2 == 0 ? 16.0 : 0.0); // left halves are even
        r = length(vec2(p.x - axis, p.z));
        rim = layer_color(ivec2(hit.cell.x, corner.y + 4), BACKGROUND); // below the outline
    } else if (sideways_pipe(metatile) && hit.side.x != 0.0) {
        float axis = float(corner.y) + (metatile <= 0x1e ? 16.0 : 0.0); // the top row is $1c-$1e
        r = length(vec2(p.y - axis, p.z));
        rim = layer_color(ivec2(corner.x + 4, hit.cell.y), BACKGROUND);
    } else {
        return false;
    }
    vec3 colour = to_linear(rim.rgb);
    if (r > PIPE_RADIUS - 1.25) albedo = vec3(0.004);
    else if (r > PIPE_HOLE) albedo = colour;
    else albedo = colour * mix(0.02, 0.12, smoothstep(PIPE_HOLE - 4.0, PIPE_HOLE, r)); // darker deeper in
    return true;
}

// The share of five short rays around the normal that get out.
float ambient_occlusion(vec3 origin, vec3 normal, ivec2 skip) {
    vec3 u = normalize(abs(normal.z) > 0.7 ? cross(normal, vec3(0.0, 1.0, 0.0)) : cross(normal, vec3(0.0, 0.0, 1.0)));
    vec3 v = cross(normal, u);
    float open = 0.0;
    open += blocked(origin, normal, 6.0, skip) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal + u), 6.0, skip) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal - u), 6.0, skip) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal + v), 6.0, skip) ? 0.0 : 1.0;
    open += blocked(origin, normalize(normal - v), 6.0, skip) ? 0.0 : 1.0;
    return 0.3 + 0.7 * open / 5.0;
}

// The sun's direction for this pixel: with u_jitter, each pixel of a 2x2
// square sees it from a corner of a small disc, so that averaging the square
// softens the edges of shadows.
vec3 sun_direction() {
    if (u_jitter == 0) return u_sun;
    ivec2 corner = ivec2(gl_FragCoord.xy) & 1;
    vec3 u = normalize(cross(u_sun, vec3(0.0, 0.0, 1.0)));
    vec3 v = cross(u_sun, u);
    vec2 offset = (vec2(corner) * 2.0 - 1.0) * 0.06;
    return normalize(u_sun + u * offset.x + v * offset.y);
}

void main() {
    vec2 screen = vec2(gl_FragCoord.x, 240.0 * u_scale - gl_FragCoord.y) / u_scale; // y down
    ivec2 pixel = ivec2(screen) + ivec2(u_origin, 0);
    vec4 status_bar = texelFetch(u_background, pixel, 0);
    if (material_of(status_bar) == M_HUD) {
        vec4 sprite = texelFetch(u_sprite, pixel, 0);
        o_color = vec4(material_of(sprite) != S_NONE ? sprite.rgb : status_bar.rgb, 1.0);
        return;
    }

    vec3 right = cross(u_down, u_forward);
    vec3 direction = normalize(u_forward * u_focal + right * (screen.x - 128.0) + u_down * (screen.y - 120.0));
    vec3 origin = u_camera + direction * ((NEAR - u_camera.z) / direction.z);
    Hit hit = trace(origin, direction, (FAR - NEAR) / direction.z, NO_CELL);
    if (hit.layer == NONE) {
        o_color = vec4((u_mask & 0x08) != 0 ? color(u_palette[0]) : vec3(0.0), 1.0);
        return;
    }

    // a smooth surface is lit as a whole: its own column does not shade it
    ivec2 skip = NO_CELL;
    if (inflated(hit)) {
        hit.t = refine(hit, origin, direction);
        skip = hit.cell;
    }
    vec3 p = origin + direction * hit.t;
    vec3 n = surface_normal(hit, p);
    vec4 layer = layer_color(hit.cell, hit.layer);
    int material = material_of(layer);
    int metatile = hit.layer == BACKGROUND ? metatile_at(hit.cell) : 0;
    bool pipe = upright_pipe(metatile) || sideways_pipe(metatile);
    vec3 albedo = to_linear(layer.rgb);
    if (pipe && hit.side != vec3(0.0) && pipe_end(hit, p, metatile, albedo)) {
        n = hit.side;
    } else if (hit.side != vec3(0.0) && !pipe) {
        // a pipe's curved side keeps its column's colour: the picture is its
        // front view
        bool block = hit.layer == BACKGROUND && (material == M_SOLID || material == M_QUESTION);
        layer = block ? block_face_color(hit, p.z) : side_color(hit.cell, -ivec2(hit.side.xy), hit.layer);
        albedo = to_linear(layer.rgb);
    }
    vec3 surface = p + n * 0.05;

    ivec2 at = scene_at(hit.cell);
    float from_above = 0.8 - 0.3 * n.y; // y is down
    vec3 light = (texelFetch(u_light, at, 0).rgb + u_floor) * u_ambient * from_above *
        ambient_occlusion(surface, n, skip);
    vec3 to_sun = sun_direction();
    float sun = dot(n, to_sun);
    vec3 glint = vec3(0.0);
    if (sun > 0.0 && !blocked(surface, to_sun, 64.0, skip)) {
        light += u_sun_color * sun;
        // blocks, pipes and characters are glossier than scenery
        float gloss = hit.layer == SPRITE ? 0.25 : inflated(hit) ? 0.06 : 0.2;
        glint = u_sun_color * gloss * pow(max(dot(n, normalize(to_sun - direction)), 0.0), 40.0);
    }
    for (int i = 0; i < 8; i++) {
        if (i >= u_light_count) break;
        vec3 to = u_lights[i].xyz - p;
        float d = length(to);
        vec3 l = to / d;
        float facing = dot(n, l);
        if (facing <= 0.0 || blocked(surface, l, d, skip)) continue;
        float r = u_lights[i].w;
        light += u_light_colors[i] * facing / (1.0 + d * d / (r * r));
    }

    vec3 c = albedo * light + glint;
    if (hit.layer == SPRITE && material == S_FIRE) c += albedo * 2.5;
    else if (hit.layer == BACKGROUND && material == M_LAVA) c += albedo * 1.2;
    else if (hit.layer == BACKGROUND && material == M_COIN) c += albedo * 0.3;
    vec3 backdrop = to_linear((u_mask & 0x08) != 0 ? color(u_palette[0]) : vec3(0.0));
    c = mix(c, backdrop, clamp((p.z - HAZE_START) / 80.0, 0.0, HAZE_MAX));
    o_color = vec4(to_display(c), 1.0);
}
