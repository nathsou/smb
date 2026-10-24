// The scene for the themes: the frame as background and sprite layers with a
// material each (common.glsl), with at least 16 columns on each side.
//
// SMB's materials come from the game's block buffer, the metatiles of the
// two screens around the player, which holds only what the player can
// touch: the level parser leaves scenery out (block_buffer_limits). A
// metatile is solid unless it is climbable (climb_metatile_limits), a coin,
// or one of the invisible ones (vine stem, hidden blocks). Without such level
// data (other games), every opaque background pixel is solid.

uniform sampler2D u_blocks; // 16x26 R8: block_buffer1 rows 0-12, block_buffer2 rows 13-25
uniform int u_climb_limits[4];
uniform int u_area_kind;    // area_kind: 3 is a castle
uniform bool u_level_known; // the block buffer, fire and lava are SMB's

layout(location = 0) out vec4 o_background;
layout(location = 1) out vec4 o_sprite;

int background_material(int screen, int nx, int y, int tile) {
    int metatile = 0;
    if (y >= 32) metatile = fetch(u_blocks, nx / 16, screen * 13 + (y - 32) / 16);
    if (metatile == 0) {
        // lava is water drawn with the castle palette: tiles of metatiles $86-$87
        if (u_area_kind == 3 && (tile == 0x41 || tile == 0x26)) return M_LAVA;
        return M_SCENERY;
    }
    if (metatile == 0xc2 || metatile == 0xc3) return M_COIN;
    if (metatile == 0xc0 || metatile == 0xc1) return M_QUESTION;
    if (metatile == 0x26 || metatile == 0x5f || metatile == 0x60) return M_SCENERY;
    if (metatile >= u_climb_limits[metatile >> 6]) return M_CLIMB;
    return M_SOLID;
}

bool is_fire(int tile) {
    return (tile >= 0x64 && tile <= 0x68) || (tile >= 0x51 && tile <= 0x53);
}

void main() {
    int x = int(gl_FragCoord.x) - u_origin.x; // on the screen
    int y = int(gl_FragCoord.y) - u_origin.y;
    o_background = vec4(color(u_palette[0]), float(M_BACKDROP) / 255.0);
    o_sprite = vec4(0.0);
    if (y < 0 || y >= 240) return;
    select_line(y);

    bool background_opaque = false;
    if ((line_mask & 0x08) == 0) {
        o_background.rgb = vec3(0.0);
    } else if ((line_mask & 0x02) == 0 && x >= 0 && x < 8) {
        // the masked left column: the backdrop
    } else if (line_status) {
        o_background.a = float(M_HUD) / 255.0;
        if (x >= 0 && x < 256) {
            ivec3 bg = plane_background(plane_point(x));
            o_background.rgb = color(bg.x);
            background_opaque = bg.y != 0;
        }
    } else {
        ivec2 point = plane_point(x);
        ivec3 bg = plane_background(point);
        background_opaque = bg.y != 0;
        if (background_opaque) {
            int screen = screen_of((point.y >= 240 ? 2 : 0) + (point.x >= 256 ? 1 : 0));
            int material = u_level_known ? background_material(screen, point.x % 256, y, bg.z) : M_SOLID;
            o_background = vec4(color(bg.x), float(material) / 255.0);
        }
    }

    if ((line_mask & 0x10) != 0 && x >= 0) {
        ivec2 s = sprite(x, y, background_opaque);
        if (s.y >= 0) {
            int material = u_level_known && is_fire(u_oam[s.y * 4 + 1]) ? S_FIRE : S_BODY;
            o_sprite = vec4(color(s.x), float(material) / 255.0);
        }
    }
}
