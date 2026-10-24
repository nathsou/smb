// The scene for the themes: the frame as background and sprite layers with a
// material each (common.glsl), including SCENE_MARGIN columns on each side.
//
// Materials come from the game's block buffer, the metatiles of the two
// screens around the player, which holds only what the player can touch:
// the level parser leaves scenery out (block_buffer_limits). A metatile is
// solid unless it is climbable (climb_metatile_limits), a coin, or one of the
// invisible ones (vine stem, hidden blocks).

uniform sampler2D u_blocks; // 16x26 R8: block_buffer1 rows 0-12, block_buffer2 rows 13-25
uniform int u_climb_limits[4];
uniform int u_area_kind;    // area_kind: 3 is a castle

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
    int x = int(gl_FragCoord.x) - SCENE_MARGIN;
    int y = int(gl_FragCoord.y);
    o_background = vec4(color(u_palette[0]), float(M_BACKDROP) / 255.0);
    o_sprite = vec4(0.0);
    if (y >= 240) return;

    bool background_opaque = false;
    if ((u_mask & 0x08) == 0) {
        o_background.rgb = vec3(0.0);
    } else if (u_status_bar != 0 && y < 32) {
        o_background.a = float(M_HUD) / 255.0;
        if (x >= 0 && x < 256) {
            ivec3 bg = background(0, x, y);
            o_background.rgb = color(bg.x);
            background_opaque = bg.y != 0;
        }
    } else {
        int base = (u_ctrl & 0x01) != 0 ? 1 : 0;
        int wx = x + u_scroll_x + 512;
        int screen = ((wx / 256) & 1) == 0 ? base : 1 - base;
        int nx = wx % 256;
        ivec3 bg = background(screen, nx, y);
        background_opaque = bg.y != 0;
        if (background_opaque) {
            o_background = vec4(color(bg.x), float(background_material(screen, nx, y, bg.z)) / 255.0);
        }
    }

    if ((u_mask & 0x10) != 0 && x >= 0) {
        ivec2 s = sprite(x, y, background_opaque);
        if (s.y >= 0) {
            int material = is_fire(u_oam[s.y * 4 + 1]) ? S_FIRE : S_BODY;
            o_sprite = vec4(color(s.x), float(material) / 255.0);
        }
    }
}
