// WebGL2 renderer: draws the Video state (codegen/lib/video.h) from wasm
// memory with the fragment shader in video.frag, which the desktop build
// shares. Pattern tables and nametables are textures; palette, sprites and
// the master palette are uniforms.

const GPU_SCREEN_WIDTH = 256;
const GPU_SCREEN_HEIGHT = 240;

const VERTEX_SHADER = `#version 300 es
out vec2 fragTexCoord;
void main() {
    // one triangle covering the viewport
    vec2 position = vec2(float((gl_VertexID & 1) * 4 - 1), float((gl_VertexID & 2) * 2 - 1));
    fragTexCoord = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}`;

const FRAGMENT_PREFIX = `#version 300 es
precision highp float;
precision highp int;
`;

function compileShader(gl, type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        throw new Error(gl.getShaderInfoLog(shader));
    }
    return shader;
}

function createByteTexture(gl, width, height) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, width, height, 0, gl.RED, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    return texture;
}

// 2-bit planar tiles to one colour index (0-3) per pixel: 16 tiles per row,
// bank 0 in rows 0-15, bank 1 in rows 16-31.
function decodePatterns(chr) {
    const atlas = new Uint8Array(128 * 256);
    for (let index = 0; index < 512; index++) {
        const left = (index % 16) * 8;
        const top = Math.floor(index / 16) * 8;
        for (let row = 0; row < 8; row++) {
            const low = chr[index * 16 + row];
            const high = chr[index * 16 + row + 8];
            for (let col = 0; col < 8; col++) {
                const bit = 7 - col;
                atlas[(top + row) * 128 + left + col] = ((low >> bit) & 1) | (((high >> bit) & 1) << 1);
            }
        }
    }
    return atlas;
}

// Returns null when WebGL2 is unavailable. Call after the pattern tables are
// loaded into chr_rom.
export async function createGpuRenderer(canvas, exports, memory, shaderUrl = 'video.frag') {
    const gl = canvas.getContext('webgl2', { antialias: false, depth: false });
    if (gl === null) return null;

    const fragmentSource = await (await fetch(shaderUrl)).text();
    const program = gl.createProgram();
    gl.attachShader(program, compileShader(gl, gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(program, compileShader(gl, gl.FRAGMENT_SHADER, FRAGMENT_PREFIX + fragmentSource));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        throw new Error(gl.getProgramInfoLog(program));
    }
    gl.useProgram(program);

    const bytes = () => new Uint8Array(memory.buffer);
    const words = new Uint32Array(memory.buffer, exports.video_layout.value, 7);
    const videoPtr = exports.video.value;
    const [nametable, palette, oam, ctrl, mask, scrollX, statusBar] =
        Array.from(words, offset => videoPtr + offset);

    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    const patterns = createByteTexture(gl, 128, 256);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 128, 256, gl.RED, gl.UNSIGNED_BYTE,
        decodePatterns(bytes().subarray(exports.chr_rom.value, exports.chr_rom.value + 8192)));
    const nametableTexture = createByteTexture(gl, 32, 64);

    const location = name => gl.getUniformLocation(program, name);
    const uniforms = {
        patterns: location('u_patterns'),
        nametable: location('u_nametable'),
        palette: location('u_palette'),
        oam: location('u_oam'),
        colors: location('u_colors'),
        ctrl: location('u_ctrl'),
        mask: location('u_mask'),
        scrollX: location('u_scroll_x'),
        statusBar: location('u_status_bar'),
        flipY: location('u_flip_y'),
    };

    const colorBytes = bytes().subarray(exports.COLOR_PALETTE.value, exports.COLOR_PALETTE.value + 64 * 3);
    gl.uniform3fv(uniforms.colors, Float32Array.from(colorBytes, value => value / 255));
    gl.uniform1i(uniforms.patterns, 0);
    gl.uniform1i(uniforms.nametable, 1);
    gl.uniform1i(uniforms.flipY, 1); // the canvas's first GL row is its bottom

    canvas.width = GPU_SCREEN_WIDTH;
    canvas.height = GPU_SCREEN_HEIGHT;
    gl.viewport(0, 0, GPU_SCREEN_WIDTH, GPU_SCREEN_HEIGHT);
    const paletteValues = new Int32Array(32);
    const oamValues = new Int32Array(256);

    return {
        gl,
        draw() {
            const view = bytes();
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, patterns);
            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, nametableTexture);
            gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 32, 64, gl.RED, gl.UNSIGNED_BYTE,
                view.subarray(nametable, nametable + 2048));

            paletteValues.set(view.subarray(palette, palette + 32));
            oamValues.set(view.subarray(oam, oam + 256));
            gl.uniform1iv(uniforms.palette, paletteValues);
            gl.uniform1iv(uniforms.oam, oamValues);
            gl.uniform1i(uniforms.ctrl, view[ctrl]);
            gl.uniform1i(uniforms.mask, view[mask]);
            gl.uniform1i(uniforms.scrollX, view[scrollX]);
            gl.uniform1i(uniforms.statusBar, view[statusBar]);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        },
    };
}
