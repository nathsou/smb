//! The recompiled game on the Nintendo 3DS, built with Citronnade.
//!
//! The generated C (the game) and its runtime are compiled for the ARM11 by
//! `build.rs` and called through the same ten functions the desktop and web
//! front ends use. Each VBlank runs one NES frame; the APU's samples go to
//! the DSP.
//!
//! The picture is split into layers (codegen/lib/layers.c: the background,
//! each pixel on a plane chosen from the level data, and the sprites), drawn
//! as two textures that the GPU shows at different depths when the 3D
//! slider is up: the sprites in front of the screen, the blocks, pipes and
//! ground on it, the hills, clouds and bushes behind it, the status bar
//! fixed on the screen. R toggles the layers off (the flat picture, drawn
//! by the CPU).
//!
//! Controls: the Circle Pad or the D-pad, A, B (or Y), START, SELECT.
#![no_std]
#![no_main]

extern crate alloc;

use alloc::format;
use alloc::sync::Arc;
use alloc::vec::Vec;
use citronnade::audio::{Audio, SampleFormat, WaveBuf, OUTPUT_RATE};
use citronnade::gpu::{
    self, shader, AttribType, Cull, Filter, Gpu, LinearBuffer, Primitive, RenderTarget, Shader, Source, TexEnv, TexFormat, TestFunc,
    Texture, VertexLayout, WriteMask,
};
use citronnade::math::Mat4;
use citronnade::prelude::*;
use citronnade::time::Instant;

const NES_WIDTH: u32 = 256;
const NES_HEIGHT: u32 = 240;
/// The CHR (tiles), extracted from your ROM by build.rs.
static CHR: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/chr.bin"));

unsafe extern "C" {
    fn cpu_init();
    fn apu_init(sample_rate: usize);
    fn video_init(chr: *const u8);
    fn render_init();
    fn cpu_reset_entry();
    fn update_controller1(state: u8);
    fn next_frame();
    fn render_frame();
    fn sound_end_frame();
    fn apu_render(out: *mut f32, count: usize);
    /// The picture render_frame draws: 256x240, RGB888.
    static frame: [u8; (NES_WIDTH * NES_HEIGHT * 3) as usize];
    /// The NES's 64 colours, RGB.
    static COLOR_PALETTE: [u8; 64 * 3];
    fn layers_smb_level() -> LayerLevel;
    fn layers_render(level: *const LayerLevel, target: *const LayerTarget) -> bool;
    static layer_background_material: [u8; PIXELS];
    static layer_backdrop: u8;
}

const PIXELS: usize = (NES_WIDTH * NES_HEIGHT) as usize;

/// layers.h's LayerLevel.
#[repr(C)]
struct LayerLevel {
    blocks: *const u8,
    area_kind: i32,
    climb_limits: [u8; 4],
}

/// layers.h's LayerTarget.
#[repr(C)]
struct LayerTarget {
    background: *mut u32,
    sprites: *mut u32,
    sprites_bytes: usize,
    colors: *const u32,
    plane_bits: [u32; 4],
    sprite_bits: u32,
    row_offset: *const u32,
    column_offset: *const u32,
}

/// Each plane's texel alpha in the background texture (layers.h's planes:
/// clear, behind, on, fixed), which the alpha test picks one at a time.
const PLANE_ALPHA: [u32; 4] = [0x00, 0x40, 0x80, 0xC0];

/// How far (in pixels at full slider) each layer sits behind the screen;
/// negative is in front.
const DEPTH_SPRITES: f32 = -1.5;
const DEPTH_ON: f32 = 0.0;
const DEPTH_BEHIND: f32 = 3.0;

/// The layers' textures are 256x256, the picture in their top 240 rows.
const TEX: u32 = 256;

const SHADER: &[u8] = shader!(
    r#"
    .fvec projection[4]
    .fvec offset            ; x: the layer's shift in pixels
    .constf consts(0.0, 1.0, 0.0, 0.0)
    .alias inpos v0         ; x, y in pixels
    .alias intc v1          ; u, v
    .out outpos position
    .out outtc texcoord0

    .proc main
        add r0.xy, offset, inpos
        mov r0.zw, consts.xxxy  ; z = 0, w = 1
        dp4 outpos.x, projection[0], r0
        dp4 outpos.y, projection[1], r0
        dp4 outpos.z, projection[2], r0
        dp4 outpos.w, projection[3], r0
        mov outtc, intc
        end
    .end
    "#
);

#[derive(Clone, Copy, Default)]
#[repr(C)]
struct Vertex {
    pos: [f32; 2],
    uv: [f32; 2],
}

/// Draws the layers on the GPU, one picture per eye.
struct LayerRenderer {
    gpu: Gpu,
    target: RenderTarget,
    shader: Shader,
    projection: gpu::UniformLocation,
    offset: gpu::UniformLocation,
    layout: VertexLayout,
    quad: LinearBuffer<Vertex>,
    background: Texture,
    sprites: Texture,
    colors: [u32; 64],
    row_offset: [u32; NES_HEIGHT as usize],
    column_offset: [u32; NES_WIDTH as usize],
}

impl LayerRenderer {
    fn new() -> Result<LayerRenderer> {
        let shader = Shader::new(SHADER).expect("shader");
        let projection = shader.uniform("projection").unwrap();
        let offset = shader.uniform("offset").unwrap();
        // The picture centred on the 400x240 screen; the texture's top row is v = 1.
        let (x0, x1) = (((400 - NES_WIDTH) / 2) as f32, ((400 + NES_WIDTH) / 2) as f32);
        let v1 = 1.0 - NES_HEIGHT as f32 / TEX as f32;
        let quad = LinearBuffer::from_slice(&[
            Vertex { pos: [x0, 0.0], uv: [0.0, 1.0] },
            Vertex { pos: [x1, 0.0], uv: [1.0, 1.0] },
            Vertex { pos: [x0, NES_HEIGHT as f32], uv: [0.0, v1] },
            Vertex { pos: [x1, NES_HEIGHT as f32], uv: [1.0, v1] },
        ])?;
        let texture = || -> Result<Texture> {
            let mut t = Texture::new(TEX, TEX, TexFormat::Rgba8)?;
            t.set_filter(Filter::Nearest, Filter::Nearest);
            Ok(t)
        };
        // RGBA8 texels are 0xRRGGBBAA; the alpha holds the plane.
        let colors = core::array::from_fn(|i| {
            let c = unsafe { &COLOR_PALETTE[i * 3..i * 3 + 3] };
            (c[0] as u32) << 24 | (c[1] as u32) << 16 | (c[2] as u32) << 8
        });
        Ok(LayerRenderer {
            gpu: Gpu::new()?,
            target: RenderTarget::top()?,
            projection,
            offset,
            shader,
            layout: VertexLayout::new().attrib(0, AttribType::Float, 2).attrib(1, AttribType::Float, 2),
            quad,
            background: texture()?,
            sprites: texture()?,
            colors,
            row_offset: core::array::from_fn(|y| gpu::tiled_index(0, y as u32, TEX, TEX)),
            column_offset: core::array::from_fn(|x| gpu::tiled_index(x as u32, 0, TEX, TEX)),
        })
    }

    /// Draws this frame's layers into the textures; false when the frame
    /// needs the flat picture.
    fn render(&mut self) -> bool {
        let level = unsafe { layers_smb_level() };
        let sprites = self.sprites.data_mut();
        let target = LayerTarget {
            background: self.background.data_mut().as_mut_ptr() as *mut u32,
            sprites_bytes: sprites.len(),
            sprites: sprites.as_mut_ptr() as *mut u32,
            colors: self.colors.as_ptr(),
            plane_bits: PLANE_ALPHA,
            sprite_bits: 0xFF,
            row_offset: self.row_offset.as_ptr(),
            column_offset: self.column_offset.as_ptr(),
        };
        unsafe { layers_render(&level, &target) }
    }

    /// Draws one eye's picture (`eye` -1 for the left, 1 for the right)
    /// into the render target: the backdrop, then the planes back to front,
    /// each shifted by its depth.
    fn draw(&mut self, eye: f32, slider: f32) -> Result<()> {
        let shift = |depth: f32| citronnade::math::round(depth * slider * eye);
        let backdrop = unsafe { layer_backdrop } as usize * 3;
        let c = unsafe { &COLOR_PALETTE[backdrop..backdrop + 3] };
        let gpu = &mut self.gpu;
        gpu.clear(&self.target, gpu::rgba(0, 0, 0, 255), 0)?;
        gpu.bind_target(&self.target);
        // Only the picture: the shifted layers do not spill past its edges.
        // (Framebuffer coordinates: the screen's x runs along the 400 side.)
        let x0 = (400 - NES_WIDTH) / 2;
        gpu.set_scissor(Some((0, x0, NES_HEIGHT, x0 + NES_WIDTH)), false);
        gpu.bind_shader(&self.shader);
        gpu.set_layout(&self.layout);
        gpu.bind_vertices(&self.quad);
        gpu.reset_texenv(1);
        gpu.set_depth_test(false, TestFunc::Always, WriteMask::COLOR);
        gpu.set_cull(Cull::None);
        gpu.set_blend(None);
        gpu.set_mat4(self.projection, &Mat4::screen_2d(400.0, 240.0));
        // The backdrop colour, then the planes.
        gpu.set_texenv(0, TexEnv::replace(Source::Constant).constant(gpu::rgba(c[0], c[1], c[2], 255)));
        gpu.set_alpha_test(false, TestFunc::Always, 0);
        gpu.set_vec4(self.offset, [0.0; 4]);
        gpu.draw_arrays(Primitive::TriangleStrip, 0, 4);
        gpu.set_texenv(0, TexEnv::replace(Source::Texture0));
        let layers = [
            (&self.background, DEPTH_BEHIND, TestFunc::Equal, PLANE_ALPHA[1]),
            (&self.background, DEPTH_ON, TestFunc::Equal, PLANE_ALPHA[2]),
            (&self.background, 0.0, TestFunc::Equal, PLANE_ALPHA[3]),
            (&self.sprites, DEPTH_SPRITES, TestFunc::NotEqual, 0),
        ];
        for (texture, depth, func, alpha) in layers {
            gpu.bind_texture(0, Some(texture));
            gpu.set_alpha_test(true, func, alpha as u8);
            gpu.set_vec4(self.offset, [shift(depth), 0.0, 0.0, 0.0]);
            gpu.draw_arrays(Primitive::TriangleStrip, 0, 4);
        }
        gpu.set_alpha_test(false, TestFunc::Always, 0);
        Ok(())
    }
}

/// NES controller bits.
mod pad {
    pub const A: u8 = 0x01;
    pub const B: u8 = 0x02;
    pub const SELECT: u8 = 0x04;
    pub const START: u8 = 0x08;
    pub const UP: u8 = 0x10;
    pub const DOWN: u8 = 0x20;
    pub const LEFT: u8 = 0x40;
    pub const RIGHT: u8 = 0x80;
}

fn controller(held: Buttons, stick: (i16, i16)) -> u8 {
    let mut s = 0;
    let dead = 40;
    if held.contains(Buttons::A) {
        s |= pad::A;
    }
    if held.contains(Buttons::B) || held.contains(Buttons::Y) {
        s |= pad::B;
    }
    if held.contains(Buttons::SELECT) {
        s |= pad::SELECT;
    }
    if held.contains(Buttons::START) {
        s |= pad::START;
    }
    if held.contains(Buttons::DPAD_UP) || stick.1 > dead {
        s |= pad::UP;
    }
    if held.contains(Buttons::DPAD_DOWN) || stick.1 < -dead {
        s |= pad::DOWN;
    }
    if held.contains(Buttons::DPAD_LEFT) || stick.0 < -dead {
        s |= pad::LEFT;
    }
    if held.contains(Buttons::DPAD_RIGHT) || stick.0 > dead {
        s |= pad::RIGHT;
    }
    // Opposite directions at once confuse the game.
    if s & (pad::LEFT | pad::RIGHT) == pad::LEFT | pad::RIGHT {
        s &= !(pad::LEFT | pad::RIGHT);
    }
    if s & (pad::UP | pad::DOWN) == pad::UP | pad::DOWN {
        s &= !(pad::UP | pad::DOWN);
    }
    s
}

/// Streams the APU's samples to a DSP channel.
struct Sound {
    audio: Option<Audio>,
    buffers: Vec<Arc<WaveBuf>>,
    scratch: Vec<f32>,
}

const SOUND_FRAMES: usize = 1024;

impl Sound {
    fn new() -> Sound {
        let audio = Audio::new().ok();
        if let Some(a) = &audio {
            let ch = a.channel(0);
            ch.set_format(SampleFormat::Pcm16Mono);
            ch.set_sample_rate(OUTPUT_RATE);
            ch.set_volume(0.9, 0.9);
        }
        Sound {
            audio,
            buffers: (0..4).map(|_| Arc::new(WaveBuf::new(SOUND_FRAMES, 1))).collect(),
            scratch: alloc::vec![0.0; SOUND_FRAMES],
        }
    }

    /// Refills the buffers the DSP has played.
    fn pump(&mut self) {
        let Some(a) = &self.audio else { return };
        let ch = a.channel(0);
        for buf in self.buffers.iter_mut() {
            if let Some(b) = Arc::get_mut(buf) {
                if b.is_free() {
                    unsafe { apu_render(self.scratch.as_mut_ptr(), SOUND_FRAMES) };
                    for (d, s) in b.samples_mut().iter_mut().zip(&self.scratch) {
                        *d = (s.clamp(-1.0, 1.0) * 32000.0) as i16;
                    }
                    ch.queue(buf);
                }
            }
        }
    }
}

#[citronnade::main]
fn main(sys: System) -> Result<()> {
    let System { apt, gsp, hid, .. } = sys;
    let _ = apt.set_speedup(true);
    let mut screens = gsp.screens()?;
    let mut input = hid.input()?;
    let mut sound = Sound::new();
    log!("smb-3ds: {} KiB of CHR, sound {}", CHR.len() / 1024, if sound.audio.is_some() { "on" } else { "off (no DSP firmware)" });

    unsafe {
        cpu_init();
        apu_init(OUTPUT_RATE as usize);
        video_init(CHR.as_ptr());
        render_init();
        cpu_reset_entry();
    }

    screens.set_top_mode(TopMode::Stereo);
    let mut layers = LayerRenderer::new()?;
    let mut layered = true;
    let (mut frames, mut total_us, mut worst_us) = (0u64, 0u64, 0u64);
    // Where the time goes: the game, the layers (or the flat picture), the
    // sound (the APU's frame and the DSP's buffers), the GPU.
    let mut parts = [0u64; 4];
    while apt.main_loop() {
        let state = input.scan();
        if state.held().contains(Buttons::START | Buttons::SELECT) && state.down().intersects(Buttons::L | Buttons::R) {
            break; // START + SELECT + L or R quits
        }
        if state.down().contains(Buttons::R) {
            layered = !layered;
            log!("smb-3ds: layers {}", if layered { "on" } else { "off" });
        }
        let start = Instant::now();
        let slider = citronnade::slider_3d();
        unsafe {
            update_controller1(controller(state.held(), state.circle_pad()));
            next_frame();
        }
        let t0 = start.elapsed().as_micros() as u64;
        let split = layered && layers.render();
        if !split {
            unsafe { render_frame() };
        }
        let t1 = start.elapsed().as_micros() as u64;
        unsafe { sound_end_frame() };
        sound.pump();
        let t2 = start.elapsed().as_micros() as u64;

        if split {
            layers.draw(-1.0, slider)?;
            screens.show_target(&mut layers.gpu, &layers.target, Screen::Top)?;
            if slider > 0.0 {
                layers.draw(1.0, slider)?;
            }
            screens.show_target_right(&mut layers.gpu, &layers.target)?;
        } else {
            let x0 = ((400 - NES_WIDTH) / 2) as i32;
            let (mut l, mut r) = screens.top_stereo();
            l.clear(Color::BLACK);
            r.clear(Color::BLACK);
            l.draw_rgb888(x0, 0, NES_WIDTH, NES_HEIGHT, 1, unsafe { &frame });
            r.draw_rgb888(x0, 0, NES_WIDTH, NES_HEIGHT, 1, unsafe { &frame });
        }
        let us = start.elapsed().as_micros() as u64;
        for (p, t) in parts.iter_mut().zip([t0, t1 - t0, t2 - t1, us - t2]) {
            *p += t;
        }
        total_us += us;
        worst_us = worst_us.max(us);
        frames += 1;
        if frames == 120 {
            let p = parts.map(|t| t / frames);
            log!("smb-3ds: running, {} us per frame on average (game {}, picture {}, sound {}, GPU {})", total_us / frames, p[0], p[1], p[2], p[3]);
        }
        if frames == 400 && split {
            // How the level data splits this frame (once, for the log).
            let mut count = [0u32; 8];
            for &m in unsafe { &layer_background_material }.iter() {
                count[(m & 7) as usize] += 1;
            }
            log!("smb-3ds: layers: backdrop {} scenery {} solid {} climb {} coin {} question {} hud {}", count[0], count[1], count[2], count[3], count[4], count[5], count[7]);
        }

        let mut b = screens.bottom();
        b.clear(Color::rgb(20, 20, 28));
        b.text(10, 10, "Recompiled, not emulated", Color::WHITE, 1);
        b.text(10, 30, &format!("frame {:6}   {:4} us", frames, us), Color::rgb(180, 190, 210), 1);
        b.text(10, 46, &format!("layers {}   3D slider {:.2}", if layered { "on" } else { "off" }, slider), Color::rgb(180, 190, 210), 1);
        b.text(10, 180, "R: layers on/off", Color::rgb(180, 190, 210), 1);
        b.text(10, 196, "Circle Pad / D-pad, A, B or Y, START, SELECT", Color::rgb(180, 190, 210), 1);
        b.text(10, 212, "START + SELECT + L/R: quit", Color::rgb(180, 190, 210), 1);
        screens.present();
    }
    log!("smb-3ds: {} frames, {} us average, {} us worst", frames, total_us / frames.max(1), worst_us);
    Ok(())
}
