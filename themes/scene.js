// What the themes share: GL helpers, and the scene (scene.frag): the frame as
// background and sprite layers with materials, from the display state and the
// game's level data (the map names the variables it reads).

export const SCENE_WIDTH = 320;
export const SCENE_HEIGHT = 288;

export function createTexture(gl, width, height, internalFormat, format, type) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return texture;
}

// A 320x288 half-float target (light, distances); needs EXT_color_buffer_float.
export function createFloatTexture(gl, width = SCENE_WIDTH, height = SCENE_HEIGHT) {
    return createTexture(gl, width, height, gl.RGBA16F, gl.RGBA, gl.HALF_FLOAT);
}

export function requireFloatTargets(gl) {
    if (gl.getExtension('EXT_color_buffer_float') === null) {
        throw new Error('This theme needs float render targets (EXT_color_buffer_float)');
    }
}

export function createFramebuffer(gl, textures) {
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    textures.forEach((texture, i) => {
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0 + i, gl.TEXTURE_2D, texture, 0);
    });
    gl.drawBuffers(textures.map((_, i) => gl.COLOR_ATTACHMENT0 + i));
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
        throw new Error('Incomplete framebuffer');
    }
    return framebuffer;
}

// Binds textures to units from 3 (0 to 2 are video.frag's) and points the
// program's samplers at them: { u_name: texture }.
export function bindTextures(gl, program, textures) {
    Object.entries(textures).forEach(([name, texture], i) => {
        gl.activeTexture(gl.TEXTURE3 + i);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.uniform1i(gl.getUniformLocation(program, name), 3 + i);
    });
}

// Setting a canvas's size clears it, even to the same size.
export function sizeCanvas(canvas, width, height) {
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
}

// Draws a program into a framebuffer (null: the canvas) of the given size.
export function pass(context, program, framebuffer, width, height) {
    const { gl } = context;
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.viewport(0, 0, width, height);
    gl.useProgram(program);
    context.drawFullscreen();
}

// Whether the map names SMB's level data (the block buffer): its materials,
// fire and lava, and its dark areas lit by them.
export function levelKnown(context) {
    return ['block_buffer1', 'block_buffer2'].every(name => context.symbols.has(name));
}

// The linear backdrop colour, and whether it is dark (SMB's underground,
// castles, night): dark areas have no sky light. Other games, whose lights
// the themes do not know, are lit as by day (Metroid's black backdrop is
// space and caverns: the sky light shades what is under something).
export function backdrop(context) {
    const entry = context.frame.palette[0] & 63;
    const rgb = Array.from(context.colors.subarray(entry * 3, entry * 3 + 3), c => c ** 2.2);
    const luma = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    return { rgb, dark: levelKnown(context) && luma < 0.02 };
}

// The playfield's point in the 512x480 plane of the nametables at the
// screen's top-left corner, from the first scanline that is not a status
// bar (render.h's video_lines).
function planePosition(lines) {
    for (let y = 0; y < 240; y++) {
        const at = y * 16;
        if (lines[at + 6] !== 0) continue;
        return [lines[at + 2] | (lines[at + 3] << 8), (lines[at + 4] | (lines[at + 5] << 8)) - y];
    }
    return [0, 0];
}

export async function createScene(context) {
    const { gl, symbols, frame } = context;
    const program = await context.compile('scene.frag');
    const background = createTexture(gl, SCENE_WIDTH, SCENE_HEIGHT, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE);
    const sprite = createTexture(gl, SCENE_WIDTH, SCENE_HEIGHT, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE);
    const metatile = createTexture(gl, SCENE_WIDTH, SCENE_HEIGHT, gl.R8, gl.RED, gl.UNSIGNED_BYTE);
    const framebuffer = createFramebuffer(gl, [background, sprite, metatile]);
    const blocks = createTexture(gl, 16, 26, gl.R8, gl.RED, gl.UNSIGNED_BYTE);
    const blockBytes = new Uint8Array(16 * 26);

    // Without the map's names nothing is solid: every opaque pixel is scenery.
    const blockBuffers = ['block_buffer1', 'block_buffer2'].map(name => symbols.get(name));
    const areaKind = symbols.get('area_kind');
    const climbTable = symbols.get('climb_metatile_limits');
    const climbLimits = climbTable === undefined ? new Int32Array([256, 256, 256, 256]) :
        Int32Array.from(context.prg().subarray(climbTable - 0x8000, climbTable - 0x8000 + 4));

    gl.useProgram(program);
    gl.uniform1iv(gl.getUniformLocation(program, 'u_climb_limits'), climbLimits);
    gl.uniform1i(gl.getUniformLocation(program, 'u_level_known'), levelKnown(context) ? 1 : 0);
    const areaKindLocation = gl.getUniformLocation(program, 'u_area_kind');

    const mod = (a, n) => ((a % n) + n) % n;
    let scrollPosition = null;
    const scene = {
        background,
        sprite,
        metatile,
        origin: [16, 0], // scene point of the screen's top-left corner (common.glsl)
        shift: [0, 0],   // where last frame had this frame's scene point, minus the point
        // Sets u_origin on a program that uses the scene.
        bindOrigin(target) {
            gl.useProgram(target);
            gl.uniform2i(gl.getUniformLocation(target, 'u_origin'), scene.origin[0], scene.origin[1]);
        },
        draw() {
            const position = planePosition(frame.lines);
            const origin = [16 + mod(position[0] - 16, 32), mod(position[1], 32)];
            const sizes = [512, 480];
            scene.shift = position.map((p, i) => {
                const scrolled = scrollPosition === null ? 0 : mod(p - scrollPosition[i] + sizes[i] / 2, sizes[i]) - sizes[i] / 2;
                return scrolled - (origin[i] - scene.origin[i]);
            });
            scene.origin = origin;
            scrollPosition = position;

            const ram = context.ram();
            if (blockBuffers.every(address => address !== undefined)) {
                blockBytes.set(ram.subarray(blockBuffers[0], blockBuffers[0] + 208), 0);
                blockBytes.set(ram.subarray(blockBuffers[1], blockBuffers[1] + 208), 208);
            }
            context.bindVideo(program);
            scene.bindOrigin(program);
            gl.uniform1i(areaKindLocation, areaKind === undefined ? -1 : ram[areaKind]);
            gl.activeTexture(gl.TEXTURE3);
            gl.bindTexture(gl.TEXTURE_2D, blocks);
            gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 16, 26, gl.RED, gl.UNSIGNED_BYTE, blockBytes);
            gl.uniform1i(gl.getUniformLocation(program, 'u_blocks'), 3);
            pass(context, program, framebuffer, SCENE_WIDTH, SCENE_HEIGHT);
        },
        dispose() {
            [background, sprite, metatile, blocks].forEach(texture => gl.deleteTexture(texture));
            gl.deleteFramebuffer(framebuffer);
            gl.deleteProgram(program);
        },
    };
    return scene;
}
