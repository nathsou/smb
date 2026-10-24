// 2D global illumination: the scene lit by what glows (fire, lava, coins,
// question blocks), with soft shadows and one bounce, by radiance cascades
// over the distance field of a jump flood. Daylight is even, as in the game.
// In games whose lights the theme does not know (without SMB's level data),
// half the light comes from the sky, beyond the scene, so that open space
// lights what faces it and what is enclosed is in shade, and half is
// everywhere, so that enclosed rooms (Metroid's caverns) stay lit.
// The 2.5D theme (voxel.js) uses the same light with the sky as a light
// source too, for shade under overhangs.

import {
    SCENE_WIDTH, SCENE_HEIGHT, createScene, createFloatTexture, createFramebuffer, requireFloatTargets,
    bindTextures, pass, backdrop, sizeCanvas, levelKnown,
} from './scene.js';

const CASCADES = 5; // enough for rays to cross the scene: 2 * (4^5 - 1) / 3 pixels
const JUMP_FLOOD_STEPS = [256, 128, 64, 32, 16, 8, 4, 2, 1];

// Sky light (cascade.frag) averages 0.819 of u_sky over the upper half of
// the directions and 0.536 over all of them: open ground, which sees half the
// sky and the ground's bounce, is lit about 0.95, keeping its colours, and
// open air about 1.1.
const SKY = [2.12, 2.1, 2.0];
// In dark areas, light from beyond the scene.
const DARK_AMBIENT = [0.12, 0.12, 0.15];
// Light everywhere, enclosed or not (not in the light texture: it is not
// reflected), with and without the sky.
const FLOOR = { day: [0.04, 0.04, 0.05], dark: [0.2, 0.2, 0.24] };
const FLOOR_WITHOUT_SKY = { day: [1, 1, 1], dark: [0.3, 0.3, 0.36] };
// Without SMB's level data (both themes).
const UNKNOWN_SKY_SHARE = 0.5;
const UNKNOWN_FLOOR = [0.5, 0.5, 0.5];
const BOUNCE = 0.5;

// The light texture of every pixel of the scene, refreshed by draw(), and
// the light to add to it everywhere (floor). With `sky`, light also comes
// from beyond the scene (the sky in daylight), which shades what is under
// something.
export async function createGlobalIllumination(context, scene, { sky: withSky }) {
    const { gl } = context;
    requireFloatTargets(gl);
    const [seed, jumpFlood, cascade, surface] = await Promise.all(
        ['seed.frag', 'jfa.frag', 'cascade.frag', 'surface.frag'].map(path => context.compile(path)));
    gl.useProgram(surface);
    gl.uniform1i(gl.getUniformLocation(surface, 'u_level_known'), levelKnown(context) ? 1 : 0);

    const seeds = [createFloatTexture(gl), createFloatTexture(gl)];
    const seedTargets = seeds.map(texture => createFramebuffer(gl, [texture]));
    const cascades = Array.from({ length: CASCADES }, () => createFloatTexture(gl));
    const cascadeTargets = cascades.map(texture => createFramebuffer(gl, [texture]));
    const lights = [createFloatTexture(gl), createFloatTexture(gl)];
    const lightTargets = lights.map(texture => createFramebuffer(gl, [texture]));
    lightTargets.forEach(target => {
        gl.bindFramebuffer(gl.FRAMEBUFFER, target);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
    });

    const uniform = (program, name) => gl.getUniformLocation(program, name);
    gl.useProgram(cascade);
    gl.uniform1i(uniform(cascade, 'u_cascades'), CASCADES);
    gl.uniform1f(uniform(cascade, 'u_bounce'), BOUNCE);

    const known = levelKnown(context);
    let current = 0; // the light being drawn; the other is last frame's
    const illumination = {
        get light() { return lights[current]; },
        // The light in open space, without emitters and the floor.
        open: [0, 0, 0],
        floor: [0, 0, 0],
        draw() {
            const { dark } = backdrop(context);
            let sky = dark || !withSky ? [0, 0, 0] : SKY;
            const ambient = dark && withSky ? DARK_AMBIENT : [0, 0, 0];
            illumination.floor = (withSky ? FLOOR : FLOOR_WITHOUT_SKY)[dark ? 'dark' : 'day'];
            if (!known) {
                sky = SKY.map(s => s * UNKNOWN_SKY_SHARE);
                illumination.floor = UNKNOWN_FLOOR;
            }
            illumination.open = sky.map((s, i) => s * 0.536 + ambient[i]);

            gl.useProgram(seed);
            bindTextures(gl, seed, { u_background: scene.background, u_sprite: scene.sprite });
            pass(context, seed, seedTargets[0], SCENE_WIDTH, SCENE_HEIGHT);
            let source = 0;
            for (const step of JUMP_FLOOD_STEPS) {
                gl.useProgram(jumpFlood);
                bindTextures(gl, jumpFlood, { u_seeds: seeds[source] });
                gl.uniform1i(uniform(jumpFlood, 'u_step'), step);
                pass(context, jumpFlood, seedTargets[1 - source], SCENE_WIDTH, SCENE_HEIGHT);
                source = 1 - source;
            }

            current = 1 - current;
            gl.useProgram(cascade);
            gl.uniform3fv(uniform(cascade, 'u_sky'), sky);
            gl.uniform3fv(uniform(cascade, 'u_ambient'), ambient);
            gl.uniform2i(uniform(cascade, 'u_shift'), scene.shift[0], scene.shift[1]);
            for (let n = CASCADES - 1; n >= 0; n--) {
                bindTextures(gl, cascade, {
                    u_seeds: seeds[source],
                    u_background: scene.background,
                    u_sprite: scene.sprite,
                    u_previous_light: lights[1 - current],
                    // the last cascade reads no upper cascade, and must not read itself
                    u_upper: cascades[n === CASCADES - 1 ? 0 : n + 1],
                });
                gl.uniform1i(uniform(cascade, 'u_cascade'), n);
                pass(context, cascade, cascadeTargets[n], SCENE_WIDTH, SCENE_HEIGHT);
            }

            gl.useProgram(surface);
            gl.uniform3fv(uniform(surface, 'u_open'), illumination.open);
            bindTextures(gl, surface, { u_cascade0: cascades[0], u_seeds: seeds[source] });
            pass(context, surface, lightTargets[current], SCENE_WIDTH, SCENE_HEIGHT);
        },
        dispose() {
            [...seeds, ...cascades, ...lights].forEach(texture => gl.deleteTexture(texture));
            [...seedTargets, ...cascadeTargets, ...lightTargets].forEach(target => gl.deleteFramebuffer(target));
            [seed, jumpFlood, cascade, surface].forEach(program => gl.deleteProgram(program));
        },
    };
    return illumination;
}

export async function create(context) {
    const { gl, canvas } = context;
    const scene = await createScene(context);
    const illumination = await createGlobalIllumination(context, scene, { sky: false });
    const program = await context.compile('light.frag');

    return {
        draw() {
            sizeCanvas(canvas, 256, 240);
            scene.draw();
            illumination.draw();
            scene.bindOrigin(program);
            bindTextures(gl, program, {
                u_background: scene.background,
                u_sprite: scene.sprite,
                u_light: illumination.light,
            });
            gl.uniform3fv(gl.getUniformLocation(program, 'u_open'), illumination.open);
            gl.uniform3fv(gl.getUniformLocation(program, 'u_floor'), illumination.floor);
            pass(context, program, null, 256, 240);
        },
        dispose() {
            scene.dispose();
            illumination.dispose();
            gl.deleteProgram(program);
        },
    };
}
