//! Recompiled NES games on the Nintendo 3DS, built with Citronnade: one
//! program with a game selector, each game keeping its place while another
//! is played.
//!
//! The generated C (each game) and its runtime are compiled for the ARM11
//! by `build.rs`, every game with its own copy of the runtime, and called
//! through `c/game.c`'s functions (`games.rs`, generated, lists them). Each
//! VBlank runs one NES frame of the game being played; the APU's samples go
//! to the DSP.
//!
//! The picture is split into layers (codegen/lib/layers.c: the background,
//! each pixel on a plane, and the sprites), drawn as two textures that the
//! GPU shows at different depths when the 3D slider is up. In SMB the level
//! data places the hills, clouds and bushes behind the screen, and the
//! blocks, pipes and ground on it with the characters; in the other games
//! the background and the characters are on the screen. Status bars float
//! in front. Frames the layers
//! cannot draw (split mid-screen, switched tiles) are drawn flat by the CPU,
//! as when R turns the layers off.
//!
//! Controls: the Circle Pad or the D-pad, A, B (or Y), START, SELECT. The
//! bottom screen's buttons save and load a state on the SD card, choose how
//! deep the 3D is and go back to the games.
#![no_std]
#![no_main]

extern crate alloc;

use alloc::format;
use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use citronnade::audio::{Audio, SampleFormat, WaveBuf, OUTPUT_RATE};
use citronnade::gfx::Canvas;
use citronnade::gpu::{
    self, shader, AttribType, Cull, Filter, Gpu, LinearBuffer, Primitive, RenderTarget, Shader, Source, TexEnv, TexFormat, TestFunc,
    Texture, VertexLayout, WriteMask,
};
use citronnade::math::Mat4;
use citronnade::prelude::*;
use citronnade::time::Instant;

const NES_WIDTH: u32 = 256;
const NES_HEIGHT: u32 = 240;
const PIXELS: usize = (NES_WIDTH * NES_HEIGHT) as usize;

/// One game's functions (c/game.c).
pub struct GameFns {
    init: unsafe extern "C" fn(data: *const u8, size: usize, sample_rate: usize) -> i32,
    controller: unsafe extern "C" fn(state: u8),
    frame: unsafe extern "C" fn(),
    end_frame: unsafe extern "C" fn(),
    audio: unsafe extern "C" fn(out: *mut f32, count: usize),
    render: unsafe extern "C" fn() -> *const u8,
    layers: unsafe extern "C" fn(target: *const LayerTarget) -> bool,
    materials: unsafe extern "C" fn() -> *const u8,
    backdrop: unsafe extern "C" fn() -> u8,
    state_size: unsafe extern "C" fn() -> u32,
    save: unsafe extern "C" fn(dest: *mut u8),
    load: unsafe extern "C" fn(state: *mut u8),
}

/// A game built in: its name, what it needs from its ROM (embedded at
/// build time), whether its level data places its background
/// (`layered_level`), and its functions.
pub struct GameInfo {
    id: &'static str,
    title: &'static str,
    data: &'static [u8],
    layered_level: bool,
    fns: GameFns,
}

include!(concat!(env!("OUT_DIR"), "/games.rs"));

/// layers.h's LayerTarget.
#[repr(C)]
pub struct LayerTarget {
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
/// negative is in front. The playfield (ground, blocks, pipes, coins) and
/// the characters on it share the screen's plane; the scenery is behind
/// it and the status bar floats in front, as an overlay.
const DEPTH_PLAYFIELD: f32 = 0.0;
const DEPTH_SPRITES: f32 = DEPTH_PLAYFIELD;
const DEPTH_BEHIND: f32 = 3.0;
const DEPTH_FIXED: f32 = -1.5;

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
    fn render(&mut self, game: &GameFns) -> bool {
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
        unsafe { (game.layers)(&target) }
    }

    /// Clears the sprites' texture, which each game clears only where it
    /// drew last: for a game played after another.
    fn forget_sprites(&mut self) {
        self.sprites.data_mut().fill(0);
    }

    /// Draws one eye's picture (`eye` -1 for the left, 1 for the right)
    /// into the render target: the backdrop, then the planes back to front,
    /// each shifted by its depth.
    fn draw(&mut self, game: &GameFns, eye: f32, slider: f32) -> Result<()> {
        let shift = |depth: f32| citronnade::math::round(depth * slider * eye);
        let backdrop = unsafe { (game.backdrop)() } as usize * 3;
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
            (&self.background, DEPTH_PLAYFIELD, TestFunc::Equal, PLANE_ALPHA[2]),
            (&self.background, DEPTH_FIXED, TestFunc::Equal, PLANE_ALPHA[3]),
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

/// Streams the playing game's APU samples to a DSP channel.
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
    fn pump(&mut self, game: &GameFns) {
        let Some(a) = &self.audio else { return };
        let ch = a.channel(0);
        for buf in self.buffers.iter_mut() {
            if let Some(b) = Arc::get_mut(buf) {
                if b.is_free() {
                    unsafe { (game.audio)(self.scratch.as_mut_ptr(), SOUND_FRAMES) };
                    for (d, s) in b.samples_mut().iter_mut().zip(&self.scratch) {
                        *d = (s.clamp(-1.0, 1.0) * 32000.0) as i16;
                    }
                    ch.queue(buf);
                }
            }
        }
    }
}

const SAVE_DIR: &str = "/3ds/smb-3ds";

/// The 3D presets: how far the planes move, times their depths, at full
/// slider.
const DEPTHS: [(&str, f32); 4] = [("flat", 0.0), ("subtle", 0.5), ("normal", 1.0), ("deep", 1.6)];

#[derive(Clone, Copy, PartialEq)]
enum Action {
    Save,
    Load,
    Depth,
    Layers,
    Games,
}

/// The bottom screen's buttons while playing: what they do and where.
const BUTTONS: [(Action, i32, i32, u32); 5] = [
    (Action::Save, 12, 48, 144),
    (Action::Load, 164, 48, 144),
    (Action::Depth, 12, 98, 144),
    (Action::Layers, 164, 98, 144),
    (Action::Games, 12, 148, 296),
];
const BUTTON_H: u32 = 42;

fn button_at((x, y): (u16, u16)) -> Option<Action> {
    let (x, y) = (x as i32, y as i32);
    BUTTONS
        .iter()
        .find(|&&(_, bx, by, w)| (bx..bx + w as i32).contains(&x) && (by..by + BUTTON_H as i32).contains(&y))
        .map(|&(a, ..)| a)
}

/// The selector's rows on the bottom screen.
const ROW_Y: i32 = 40;
const ROW_H: i32 = 30;

fn row_at((x, y): (u16, u16), rows: usize) -> Option<usize> {
    let (x, y) = (x as i32, y as i32);
    if !(12..308).contains(&x) || y < ROW_Y {
        return None;
    }
    let row = ((y - ROW_Y) / ROW_H) as usize;
    (row < rows && (y - ROW_Y) % ROW_H < ROW_H - 4).then_some(row)
}

fn state_file(game: &GameInfo) -> String {
    format!("{SAVE_DIR}/{}.state", game.id)
}

/// Saves a game's state to the SD card.
fn save(game: &GameInfo) -> core::result::Result<(), &'static str> {
    let mut state = alloc::vec![0u8; unsafe { (game.fns.state_size)() } as usize];
    unsafe { (game.fns.save)(state.as_mut_ptr()) };
    citronnade::fs::create_dir_all(SAVE_DIR).map_err(|_| "Could not make /3ds/smb-3ds")?;
    citronnade::fs::write(&state_file(game), &state).map_err(|_| "Could not write the state")
}

/// Loads the state saved on the SD card.
fn load(game: &GameInfo) -> core::result::Result<(), &'static str> {
    let mut state = citronnade::fs::read(&state_file(game)).map_err(|_| "No saved state yet")?;
    if state.len() != unsafe { (game.fns.state_size)() } as usize {
        return Err("The saved state is not from this version");
    }
    unsafe { (game.fns.load)(state.as_mut_ptr()) };
    Ok(())
}

/// Whether each game has started, and how its frames go.
#[derive(Clone, Copy, Default)]
struct Progress {
    started: bool,
    frames: u64,
    total_us: u64,
    /// The game, the picture (layers or flat), the sound, the drawing.
    parts: [u64; 4],
}

/// What the bottom screen shows.
struct Menu {
    depth: usize,
    layered: bool,
    message: Option<(&'static str, u64)>,
    us: u64,
}

const TEXT: Color = Color::rgb(180, 190, 210);
const BACKGROUND: Color = Color::rgb(20, 20, 28);

fn button(b: &mut Canvas, x: i32, y: i32, w: u32, h: u32, label: &str, highlight: bool) {
    b.fill_rect(x, y, w, h, if highlight { Color::rgb(70, 82, 124) } else { Color::rgb(46, 52, 74) });
    b.rect(x, y, w, h, if highlight { Color::rgb(255, 214, 120) } else { Color::rgb(110, 122, 166) });
    let (tw, th) = Canvas::text_size(label, 1);
    b.text(x + (w as i32 - tw as i32) / 2, y + (h as i32 - th as i32) / 2, label, Color::WHITE, 1);
}

impl Menu {
    fn label(&self, action: Action) -> String {
        match action {
            Action::Save => "Save state".into(),
            Action::Load => "Load state".into(),
            Action::Depth => format!("3D: {}", DEPTHS[self.depth].0),
            Action::Layers => format!("Layers: {}", if self.layered { "on" } else { "off" }),
            Action::Games => "Games".into(),
        }
    }

    fn draw(&self, b: &mut Canvas, game: &GameInfo, frames: u64) {
        b.clear(BACKGROUND);
        b.text(12, 12, game.title, Color::WHITE, 1);
        b.text(12, 26, &format!("Recompiled, not emulated   {:5.2} ms per frame", self.us as f32 / 1000.0), TEXT, 1);
        for &(action, x, y, w) in BUTTONS.iter() {
            button(b, x, y, w, BUTTON_H, &self.label(action), false);
        }
        if let Some((message, until)) = self.message {
            if frames < until {
                b.text(12, 198, message, Color::rgb(255, 214, 120), 1);
            }
        }
        b.text(12, 212, "R: layers   START + SELECT + L/R: quit", TEXT, 1);
    }
}

/// The game selector: the list on the bottom screen, the chosen game's
/// name on the top.
fn draw_selector(screens: &mut Screens, chosen: usize, progress: &[Progress], message: Option<&str>) {
    let (mut l, mut r) = screens.top_stereo();
    for top in [&mut l, &mut r] {
        top.clear(Color::rgb(12, 14, 24));
        top.text(24, 24, "Recompiled NES games", Color::rgb(255, 214, 120), 2);
        top.text(24, 52, "Statically recompiled to C from your own ROMs,", TEXT, 1);
        top.text(24, 66, "not emulated.", TEXT, 1);
        top.text(24, 110, GAMES[chosen].title, Color::WHITE, 2);
        let status = if progress[chosen].started { "A: resume (it kept its place)" } else { "A: start" };
        top.text(24, 140, status, TEXT, 1);
        top.text(24, 210, "Up/Down or the stylus: choose   START: quit", TEXT, 1);
    }
    drop((l, r));
    let mut b = screens.bottom();
    b.clear(BACKGROUND);
    b.text(12, 14, "Choose a game", Color::WHITE, 1);
    for (i, game) in GAMES.iter().enumerate() {
        let y = ROW_Y + i as i32 * ROW_H;
        let label = if progress[i].started { format!("{}  (paused)", game.title) } else { String::from(game.title) };
        button(&mut b, 12, y, 296, (ROW_H - 4) as u32, &label, i == chosen);
    }
    if let Some(message) = message {
        b.text(12, 212, message, Color::rgb(255, 214, 120), 1);
    }
}

#[citronnade::main]
fn main(sys: System) -> Result<()> {
    let System { apt, gsp, hid, .. } = sys;
    let _ = apt.set_speedup(true);
    let mut screens = gsp.screens()?;
    let mut input = hid.input()?;
    let mut sound = Sound::new();
    log!(
        "smb-3ds: {} games ({}), sound {}",
        GAMES.len(),
        GAMES.iter().map(|g| g.id).collect::<Vec<_>>().join(", "),
        if sound.audio.is_some() { "on" } else { "off (no DSP firmware)" }
    );

    screens.set_top_mode(TopMode::Stereo);
    let mut layers = LayerRenderer::new()?;
    let mut menu = Menu { depth: 2, layered: true, message: None, us: 0 };
    let mut progress = alloc::vec![Progress::default(); GAMES.len()];
    // The game being played, or the selector; the selector's choice.
    let mut playing: Option<usize> = None;
    let mut chosen = 0;
    let mut selector_message: Option<&'static str> = None;
    // The bottom screen is drawn only when it changes: into both of its
    // buffers, so two frames in a row.
    let mut redraw = 2;
    let mut was_touching = false;
    let mut frames = 0u64;
    while apt.main_loop() {
        let state = input.scan();
        let (held, down) = (state.held(), state.down());
        let touch = state.touch();
        let tap = touch.filter(|_| !was_touching);
        was_touching = touch.is_some();
        frames += 1;

        let Some(index) = playing else {
            // The selector.
            if down.contains(Buttons::START) {
                break;
            }
            let rows = GAMES.len();
            let mut pick = tap.and_then(|t| row_at(t, rows));
            if let Some(row) = pick {
                chosen = row;
            }
            if down.intersects(Buttons::UP) {
                chosen = (chosen + rows - 1) % rows;
            }
            if down.intersects(Buttons::DOWN) {
                chosen = (chosen + 1) % rows;
            }
            if down.contains(Buttons::A) {
                pick = Some(chosen);
            }
            if let Some(i) = pick {
                let game = &GAMES[i];
                if !progress[i].started {
                    let ok = unsafe { (game.fns.init)(game.data.as_ptr(), game.data.len(), OUTPUT_RATE as usize) } == 0;
                    log!("smb-3ds: starting {}: {}", game.id, if ok { "ok" } else { "failed" });
                    if ok {
                        progress[i].started = true;
                    } else {
                        selector_message = Some("That game could not start: is its ROM right?");
                    }
                }
                if progress[i].started {
                    playing = Some(i);
                    layers.forget_sprites();
                    menu.message = None;
                    redraw = 2;
                    continue;
                }
            }
            draw_selector(&mut screens, chosen, &progress, selector_message);
            screens.present();
            continue;
        };

        // A game.
        let game = &GAMES[index];
        if held.contains(Buttons::START | Buttons::SELECT) && down.intersects(Buttons::L | Buttons::R) {
            break; // START + SELECT + L or R quits
        }
        let action = if down.contains(Buttons::R) { Some(Action::Layers) } else { tap.and_then(button_at) };
        if let Some(action) = action {
            // Between frames, where states are taken.
            let result = match action {
                Action::Save => save(game).map(|_| "State saved"),
                Action::Load => load(game).map(|_| "State loaded"),
                Action::Depth => {
                    menu.depth = (menu.depth + 1) % DEPTHS.len();
                    Ok("")
                }
                Action::Layers => {
                    menu.layered = !menu.layered;
                    Ok("")
                }
                Action::Games => {
                    playing = None;
                    chosen = index;
                    selector_message = None;
                    continue;
                }
            };
            let message = result.unwrap_or_else(|e| e);
            if !message.is_empty() {
                log!("smb-3ds: {}: {}", game.id, message);
                menu.message = Some((message, frames + 120));
            }
            redraw = 2;
        }

        let start = Instant::now();
        let depth = citronnade::slider_3d() * DEPTHS[menu.depth].1;
        unsafe {
            (game.fns.controller)(controller(held, state.circle_pad()));
            (game.fns.frame)();
        }
        let t0 = start.elapsed().as_micros() as u64;
        let split = menu.layered && layers.render(&game.fns);
        let flat = if split { None } else { Some(unsafe { core::slice::from_raw_parts((game.fns.render)(), PIXELS * 3) }) };
        let t1 = start.elapsed().as_micros() as u64;
        unsafe { (game.fns.end_frame)() };
        sound.pump(&game.fns);
        let t2 = start.elapsed().as_micros() as u64;
        if let Some(picture) = flat {
            let x0 = ((400 - NES_WIDTH) / 2) as i32;
            let (mut l, mut r) = screens.top_stereo();
            for eye in [&mut l, &mut r] {
                eye.clear(Color::BLACK);
                eye.draw_rgb888(x0, 0, NES_WIDTH, NES_HEIGHT, 1, picture);
            }
        } else {
            layers.draw(&game.fns, -1.0, depth)?;
            screens.show_target(&mut layers.gpu, &layers.target, Screen::Top)?;
            if depth > 0.0 {
                layers.draw(&game.fns, 1.0, depth)?;
            }
            screens.show_target_right(&mut layers.gpu, &layers.target)?;
        }
        let us = start.elapsed().as_micros() as u64;
        let p = &mut progress[index];
        p.frames += 1;
        p.total_us += us;
        for (part, t) in p.parts.iter_mut().zip([t0, t1 - t0, t2 - t1, us - t2]) {
            *part += t;
        }
        if p.frames == 120 {
            let a = p.parts.map(|t| t / p.frames);
            log!("smb-3ds: {}: {} us per frame on average (game {}, picture {}, sound {}, drawing {})", game.id, p.total_us / p.frames, a[0], a[1], a[2], a[3]);
        }
        if p.frames == 400 && split && game.layered_level {
            // How the level data splits this frame (once, for the log).
            let materials = unsafe { core::slice::from_raw_parts((game.fns.materials)(), PIXELS) };
            let mut count = [0u32; 8];
            for &m in materials {
                count[(m & 7) as usize] += 1;
            }
            log!("smb-3ds: {}: layers: backdrop {} scenery {} solid {} climb {} coin {} question {} hud {}", game.id, count[0], count[1], count[2], count[3], count[4], count[5], count[7]);
        }

        // The frame time twice a second, and the message's end.
        if frames % 30 == 0 || menu.message.is_some_and(|(_, until)| frames == until) {
            menu.us = us;
            redraw = 2;
        }
        if redraw > 0 {
            menu.draw(&mut screens.bottom(), game, frames);
            redraw -= 1;
        }
        screens.present();
    }
    log!("smb-3ds: bye");
    Ok(())
}
