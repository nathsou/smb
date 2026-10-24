// 3D: the level and its characters as shapes made at runtime from the ROM's
// own tiles and the game's level data (shape.frag): bevelled blocks, pipes as
// cylinders, and scenery and sprites inflated from their silhouettes. No
// models are drawn by hand. Seen through a perspective camera (model.frag),
// lit as the 2.5D theme, and drawn at up to 4 times the NES resolution, then
// scaled to the canvas: twice the canvas's resolution at the usual 2x, which
// smooths edges and the edges of shadows.

import {
    SCENE_WIDTH, SCENE_HEIGHT, createScene, createTexture, createFloatTexture, createFramebuffer, bindTextures,
    pass, sizeCanvas,
} from './scene.js';
import { createGlobalIllumination, JUMP_FLOOD_STEPS } from './light.js';
import { bindLighting, canvasScale } from './voxel.js';

// The camera looks at the middle of the play plane from FOCAL away, tilted
// down by PITCH: at that distance a canvas pixel spans a NES pixel, so the
// play plane keeps about the game's framing.
const PITCH = 16 * Math.PI / 180;
const FOCAL = 440;
const TARGET = [128, 120, 0];
const FIRE_DEPTH = -18; // in front of the pipes
const MAX_RENDER_SCALE = 4; // NES pixels are drawn at most 4x4 before scaling to the canvas

export async function create(context) {
    const { gl, canvas } = context;
    const scene = await createScene(context);
    const illumination = await createGlobalIllumination(context, scene, { sky: true });
    const [edges, jumpFlood, shapes, smoothing] = await Promise.all(
        ['edges.frag', 'jfa.frag', 'shape.frag', 'smooth.frag'].map(path => context.compile(path)));
    // Some GPUs' compilers give up on the full shader: then without ambient
    // occlusion and smooth shapes.
    let program;
    try {
        program = await context.compile('model.frag');
    } catch (error) {
        console.warn('3D: the full shader failed, using the lighter one:', error);
        program = await context.compile('model.frag', '#define MODEL_LITE\n');
    }

    const seeds = [createFloatTexture(gl), createFloatTexture(gl)];
    const seedTargets = seeds.map(texture => createFramebuffer(gl, [texture]));
    const shapeTextures = [createFloatTexture(gl), createFloatTexture(gl)]; // as made, smoothed
    const shapeTargets = shapeTextures.map(texture => createFramebuffer(gl, [texture]));
    // the picture before scaling to the canvas, resized with it
    let image = null;
    let imageTarget = null;
    let imageScale = 0;
    const resizeImage = renderScale => {
        if (renderScale === imageScale) return;
        if (image !== null) {
            gl.deleteTexture(image);
            gl.deleteFramebuffer(imageTarget);
        }
        image = createTexture(gl, 256 * renderScale, 240 * renderScale, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE);
        imageTarget = createFramebuffer(gl, [image]);
        imageScale = renderScale;
    };

    const forward = [0, Math.sin(PITCH), Math.cos(PITCH)];
    const down = [0, Math.cos(PITCH), -Math.sin(PITCH)];
    const camera = TARGET.map((t, i) => t - forward[i] * FOCAL);
    let scale = 2;

    return {
        resize(displayScale) {
            scale = canvasScale(displayScale);
        },
        draw() {
            sizeCanvas(canvas, 256 * scale, 240 * scale);
            scene.draw();
            illumination.draw();

            // each scenery and sprite pixel's distance to the edge of its shape
            gl.useProgram(edges);
            bindTextures(gl, edges, { u_background: scene.background, u_sprite: scene.sprite });
            pass(context, edges, seedTargets[0], SCENE_WIDTH, SCENE_HEIGHT);
            let source = 0;
            for (const step of JUMP_FLOOD_STEPS) {
                gl.useProgram(jumpFlood);
                bindTextures(gl, jumpFlood, { u_seeds: seeds[source] });
                gl.uniform1i(gl.getUniformLocation(jumpFlood, 'u_step'), step);
                pass(context, jumpFlood, seedTargets[1 - source], SCENE_WIDTH, SCENE_HEIGHT);
                source = 1 - source;
            }

            gl.useProgram(shapes);
            bindTextures(gl, shapes, {
                u_background: scene.background,
                u_sprite: scene.sprite,
                u_metatile: scene.metatile,
                u_edges: seeds[source],
            });
            pass(context, shapes, shapeTargets[0], SCENE_WIDTH, SCENE_HEIGHT);
            gl.useProgram(smoothing);
            bindTextures(gl, smoothing, { u_shape: shapeTextures[0], u_background: scene.background });
            pass(context, smoothing, shapeTargets[1], SCENE_WIDTH, SCENE_HEIGHT);

            const renderScale = Math.max(scale, Math.min(2 * scale, MAX_RENDER_SCALE));
            resizeImage(renderScale);

            context.bindVideo(program);
            scene.bindOrigin(program);
            bindTextures(gl, program, {
                u_background: scene.background,
                u_sprite: scene.sprite,
                u_shape: shapeTextures[1],
                u_metatile: scene.metatile,
                u_light: illumination.light,
            });
            bindLighting(context, program, illumination, FIRE_DEPTH);
            const uniform = name => gl.getUniformLocation(program, name);
            gl.uniform1f(uniform('u_scale'), renderScale);
            gl.uniform1i(uniform('u_jitter'), renderScale > scale ? 1 : 0);
            gl.uniform3fv(uniform('u_camera'), camera);
            gl.uniform3fv(uniform('u_forward'), forward);
            gl.uniform3fv(uniform('u_down'), down);
            gl.uniform1f(uniform('u_focal'), FOCAL);
            pass(context, program, imageTarget, 256 * renderScale, 240 * renderScale);

            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, imageTarget);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
            gl.blitFramebuffer(0, 0, 256 * renderScale, 240 * renderScale, 0, 0, 256 * scale, 240 * scale,
                gl.COLOR_BUFFER_BIT, gl.LINEAR);
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
        },
        dispose() {
            scene.dispose();
            illumination.dispose();
            [...seeds, ...shapeTextures, image].forEach(texture => gl.deleteTexture(texture));
            [...seedTargets, ...shapeTargets, imageTarget].forEach(target => gl.deleteFramebuffer(target));
            [edges, jumpFlood, shapes, smoothing, program].forEach(item => gl.deleteProgram(item));
        },
    };
}
