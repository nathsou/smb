// WebGL2 renderer: draws the Video state (codegen/lib/video.h) from wasm
// memory with the fragment shader in video.frag, which the desktop build
// shares. Pattern tables and nametables are textures; palette, sprites and
// the master palette are uniforms.
//
// A theme (web/themes) draws the same state its own way; 'original' is
// video.frag alone.

const GPU_SCREEN_WIDTH = 256;
const GPU_SCREEN_HEIGHT = 240;

export const THEMES = {
    original: 'ORIGINAL',
    light: '2D GLOBAL ILLUMINATION',
    voxel: '2.5D RAY TRACING',
};

export const VERTEX_SHADER = `#version 300 es
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
precision highp sampler2D;
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

export function createProgram(gl, fragmentSource) {
    const program = gl.createProgram();
    gl.attachShader(program, compileShader(gl, gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(program, compileShader(gl, gl.FRAGMENT_SHADER, FRAGMENT_PREFIX + fragmentSource));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        throw new Error(gl.getProgramInfoLog(program));
    }
    return program;
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

// The addresses the map gives names to (vars and tables), for the themes.
export function parseSymbols(map) {
    const symbols = new Map();
    for (const line of map.split('\n')) {
        const match = /^\s*(?:var|table)\s+\$([0-9a-fA-F]+)\s+(\w+)/.exec(line);
        if (match && !symbols.has(match[2])) symbols.set(match[2], parseInt(match[1], 16));
    }
    return symbols;
}

// Returns null when WebGL2 is unavailable. Call after the pattern tables are
// loaded into chr_rom. `map` (the map's text) gives the themes the game's
// variables.
export async function createGpuRenderer(canvas, exports, memory, shaderUrl = 'video.frag', { map = null } = {}) {
    const gl = canvas.getContext('webgl2', { antialias: false, depth: false });
    if (gl === null) return null;

    const videoSource = await (await fetch(shaderUrl)).text();
    const program = createProgram(gl, videoSource);
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

    const colorBytes = bytes().subarray(exports.COLOR_PALETTE.value, exports.COLOR_PALETTE.value + 64 * 3);
    const colors = Float32Array.from(colorBytes, value => value / 255);

    // This frame's display state, read once and given to every program.
    const frame = {
        palette: new Int32Array(32),
        oam: new Int32Array(256),
        ctrl: 0,
        mask: 0,
        scrollX: 0,
        statusBar: 0,
    };

    const readFrame = () => {
        const view = bytes();
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, patterns);
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, nametableTexture);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 32, 64, gl.RED, gl.UNSIGNED_BYTE,
            view.subarray(nametable, nametable + 2048));
        frame.palette.set(view.subarray(palette, palette + 32));
        frame.oam.set(view.subarray(oam, oam + 256));
        frame.ctrl = view[ctrl];
        frame.mask = view[mask];
        frame.scrollX = view[scrollX];
        frame.statusBar = view[statusBar];
    };

    // Sets video.frag's uniforms on a program that includes it.
    const videoUniforms = new WeakMap();
    const bindVideo = (target, flipY = 0) => {
        let uniforms = videoUniforms.get(target);
        if (uniforms === undefined) {
            const location = name => gl.getUniformLocation(target, name);
            uniforms = {
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
            videoUniforms.set(target, uniforms);
            gl.useProgram(target);
            gl.uniform3fv(uniforms.colors, colors);
            gl.uniform1i(uniforms.patterns, 0);
            gl.uniform1i(uniforms.nametable, 1);
        }
        gl.useProgram(target);
        gl.uniform1iv(uniforms.palette, frame.palette);
        gl.uniform1iv(uniforms.oam, frame.oam);
        gl.uniform1i(uniforms.ctrl, frame.ctrl);
        gl.uniform1i(uniforms.mask, frame.mask);
        gl.uniform1i(uniforms.scrollX, frame.scrollX);
        gl.uniform1i(uniforms.statusBar, frame.statusBar);
        gl.uniform1i(uniforms.flipY, flipY);
    };

    const themeUrl = path => new URL(`themes/${path}`, import.meta.url);
    const fetchText = async path => (await fetch(themeUrl(path))).text();
    let commonSource = null;
    // What a theme module gets: the frame, the game's memory, and programs
    // compiled with video.frag and common.glsl as libraries.
    const context = {
        gl,
        canvas,
        frame,
        colors,
        bindVideo,
        fetchText,
        symbols: parseSymbols(map ?? ''),
        ram: () => bytes().subarray(exports.ram.value, exports.ram.value + 0x800),
        // the PRG ROM, $8000-$FFFF
        prg: () => bytes().subarray(exports.data.value, exports.data.value + 0x8000),
        async compile(path) {
            commonSource ??= await fetchText('common.glsl');
            return createProgram(gl, '#define NES_LIBRARY\n' + videoSource + '\n' + commonSource + '\n' +
                await fetchText(path));
        },
        drawFullscreen() {
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        },
    };

    const useOriginalSize = () => {
        canvas.width = GPU_SCREEN_WIDTH;
        canvas.height = GPU_SCREEN_HEIGHT;
    };
    useOriginalSize();

    let theme = null;
    let themeName = 'original';
    let displayScale = 1;

    return {
        gl,
        get theme() { return themeName; },
        // Switches to a theme; on failure (a missing WebGL feature) keeps the
        // current one and throws.
        async setTheme(name) {
            if (name === themeName) return;
            let next = null;
            if (name !== 'original') {
                if (!(name in THEMES)) throw new Error(`Unknown theme: ${name}`);
                const module = await import(themeUrl(`${name}.js`));
                next = await module.create(context);
            }
            theme?.dispose();
            theme = next;
            themeName = name;
            if (theme === null) useOriginalSize();
            theme?.resize?.(displayScale);
        },
        // Device pixels per NES pixel on the page, for themes drawn above the
        // NES resolution.
        setDisplayScale(scale) {
            displayScale = scale;
            theme?.resize?.(scale);
        },
        draw() {
            readFrame();
            if (theme !== null) {
                theme.draw();
                return;
            }
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            gl.viewport(0, 0, GPU_SCREEN_WIDTH, GPU_SCREEN_HEIGHT);
            bindVideo(program, 1); // the canvas's first GL row is its bottom
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        },
    };
}
