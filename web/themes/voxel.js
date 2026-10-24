// 2.5D ray tracing: the scene as voxels (voxel.frag), drawn at the page's
// resolution, lit by the sun and by fire with shadow rays and by the 2D
// global illumination (light.js) as ambient light.

import { createScene, bindTextures, pass, backdrop, sizeCanvas } from './scene.js';
import { createGlobalIllumination } from './light.js';

const SUN = normalize([-0.45, -0.8, -0.55]); // up and to the left, towards the viewer
const SUN_COLOR = [0.85, 0.82, 0.74];
const DAY_AMBIENT = 0.62;
// Dark areas: a faint cool key light from the same side, so that tops and
// sides still read.
const DARK_KEY_COLOR = [0.14, 0.15, 0.2];
const DARK_AMBIENT = 0.85;
const FIRE_COLOR = [2.5, 1.25, 0.4];
const FIRE_RADIUS = 40;
const FIRE_DEPTH = -10; // in front of the blocks, so that fire lights their faces
const MAX_LIGHTS = 8;
const positions = new Float32Array(MAX_LIGHTS * 4);
const colors = new Float32Array(MAX_LIGHTS * 3);

function normalize(v) {
    const length = Math.hypot(...v);
    return v.map(c => c / length);
}

export function isFire(tile) {
    return (tile >= 0x64 && tile <= 0x68) || (tile >= 0x51 && tile <= 0x53);
}

// Fire sprites as point lights; the sprites of one flame (Bowser's is three)
// make one light.
export function fireLights(oam) {
    const lights = [];
    for (let i = 0; i < 64; i++) {
        const y = oam[i * 4];
        if (y >= 0xef || !isFire(oam[i * 4 + 1])) continue;
        const x = oam[i * 4 + 3] + 4;
        const center = y + 5;
        const near = lights.find(light => Math.abs(light.x - x) < 12 && Math.abs(light.y - center) < 12);
        if (near) {
            near.x = (near.x * near.count + x) / (near.count + 1);
            near.y = (near.y * near.count + center) / (near.count + 1);
            near.count++;
        } else if (lights.length < MAX_LIGHTS) {
            lights.push({ x, y: center, count: 1 });
        }
    }
    return lights;
}

// Sets the light uniforms of voxel.frag and model.frag: the sun (a faint key
// light in dark areas), the 2D light as ambient, and fire as point lights at
// `fireDepth`, in front of what they should light.
export function bindLighting(context, program, illumination, fireDepth = FIRE_DEPTH) {
    const { gl, frame } = context;
    const uniform = name => gl.getUniformLocation(program, name);
    const { dark } = backdrop(context);
    const lights = fireLights(frame.oam);
    lights.forEach((light, i) => {
        positions.set([light.x, light.y, fireDepth, FIRE_RADIUS * Math.sqrt(light.count)], i * 4);
        colors.set(FIRE_COLOR, i * 3);
    });
    gl.useProgram(program);
    gl.uniform3fv(uniform('u_floor'), illumination.floor);
    gl.uniform3fv(uniform('u_sun'), SUN);
    gl.uniform3fv(uniform('u_sun_color'), dark ? DARK_KEY_COLOR : SUN_COLOR);
    gl.uniform1f(uniform('u_ambient'), dark ? DARK_AMBIENT : DAY_AMBIENT);
    gl.uniform1i(uniform('u_light_count'), lights.length);
    gl.uniform4fv(uniform('u_lights'), positions);
    gl.uniform3fv(uniform('u_light_colors'), colors);
}

// Canvas pixels per NES pixel for the page's scale, at most 4.
export function canvasScale(displayScale) {
    return Math.min(4, Math.max(1, Math.round(displayScale)));
}

export async function create(context) {
    const { gl, canvas } = context;
    const scene = await createScene(context);
    const illumination = await createGlobalIllumination(context, scene, { sky: true });
    const program = await context.compile('voxel.frag');
    let scale = 2;

    return {
        resize(displayScale) {
            scale = canvasScale(displayScale);
        },
        draw() {
            sizeCanvas(canvas, 256 * scale, 240 * scale);
            scene.draw();
            illumination.draw();

            context.bindVideo(program);
            scene.bindOrigin(program);
            bindTextures(gl, program, {
                u_background: scene.background,
                u_sprite: scene.sprite,
                u_light: illumination.light,
            });
            bindLighting(context, program, illumination);
            gl.uniform1f(gl.getUniformLocation(program, 'u_scale'), scale);
            pass(context, program, null, 256 * scale, 240 * scale);
        },
        dispose() {
            scene.dispose();
            illumination.dispose();
            gl.deleteProgram(program);
        },
    };
}
