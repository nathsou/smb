//! Builds the recompiled games for the 3DS's ARM11 with clang (no C library:
//! the runtime only needs memcpy and memset, which Rust provides), and
//! takes what each needs from your ROM: SMB's CHR (its tiles), or a
//! cartridge's header and CHR (the PRG is the recompiled program's own).
//! Everything comes from your own files at build time and never enters the
//! repository:
//!
//! - `make codegen GAME=...` (in the repository root) generates each game,
//!   in `codegen/game` (SMB) or `codegen/NAME`;
//! - the ROMs are `NAME.nes` in the repository root, or the paths in
//!   `SMB_ROM`, `METROID_ROM`, `SMB3_ROM`, `ZELDA_ROM` and `TETRIS_ROM`.
//!
//! Every game whose code and ROM are there is built in; the others are left
//! out with a warning. Each game links with its own copy of the runtime:
//! its objects are linked into one (`ld.lld -r`) and every symbol it
//! defines gets the game's name as a prefix (`llvm-objcopy`), so the games'
//! `cpu_init`s, RAMs and frames do not collide. `c/game.c` gives each the
//! same interface, which `games.rs` (generated) binds.
//!
//! `CC`, `AR`, `LD` and `OBJCOPY` choose the tools (clang, llvm-ar, ld.lld,
//! llvm-objcopy and llvm-nm by default; `NM` too).

use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

struct Game {
    id: &'static str,
    title: &'static str,
    dir: &'static str,
    /// Drives the PPU through its registers, with a mapper: an iNES cartridge.
    cartridge: bool,
}

const GAMES: &[Game] = &[
    Game { id: "smb", title: "Super Mario Bros.", dir: "codegen/game", cartridge: false },
    Game { id: "smb3", title: "Super Mario Bros. 3", dir: "codegen/smb3", cartridge: true },
    Game { id: "metroid", title: "Metroid", dir: "codegen/metroid", cartridge: true },
    Game { id: "zelda", title: "The Legend of Zelda", dir: "codegen/zelda", cartridge: true },
    Game { id: "tetris", title: "Tetris", dir: "codegen/tetris", cartridge: true },
];

/// The runtime every game links with.
const RUNTIME: &[&str] = &[
    "codegen/lib/cpu.c",
    "codegen/lib/video.c",
    "codegen/lib/render.c",
    "codegen/lib/apu.c",
    "codegen/lib/apu_buffers.c",
    "codegen/lib/sound.c",
    "codegen/lib/state.c",
    "codegen/lib/layers.c",
    "platforms/3ds/c/game.c",
];
const SMB_RUNTIME: &[&str] = &["codegen/native/display.c", "codegen/lib/debug_chr.c"];
const CARTRIDGE_RUNTIME: &[&str] = &["codegen/lib/ppu_bus.c", "codegen/lib/mapper.c", "codegen/lib/cartridge.c"];

/// The functions of c/game.c: name, Rust signature.
const FUNCTIONS: &[(&str, &str)] = &[
    ("game_init", "(data: *const u8, size: usize, sample_rate: usize) -> i32"),
    ("game_controller", "(state: u8)"),
    ("game_frame", "()"),
    ("game_end_frame", "()"),
    ("game_audio", "(out: *mut f32, count: usize)"),
    ("game_render", "() -> *const u8"),
    ("game_layers", "(target: *const crate::LayerTarget) -> bool"),
    ("game_materials", "() -> *const u8"),
    ("game_backdrop", "() -> u8"),
    ("game_state_size", "() -> u32"),
    ("game_save", "(dest: *mut u8)"),
    ("game_load", "(state: *mut u8)"),
];

fn find_tool(var: &str, candidates: &[&str]) -> String {
    if let Ok(v) = env::var(var) {
        return v;
    }
    for c in candidates {
        if Command::new(c).arg("--version").output().is_ok_and(|o| o.status.success()) {
            return c.to_string();
        }
    }
    panic!("no {var} found (tried {candidates:?}); set {var}");
}

fn tool(name: &str, var: &str) -> String {
    let versions: Vec<String> = ["", "-18", "-19", "-20", "-21"].iter().map(|v| format!("{name}{v}")).collect();
    find_tool(var, &versions.iter().map(String::as_str).collect::<Vec<_>>())
}

fn run(cmd: &mut Command, what: &str) -> Vec<u8> {
    let out = cmd.output().unwrap_or_else(|e| panic!("cannot run {what}: {e}"));
    if !out.status.success() {
        panic!("{what} failed:\n{}", String::from_utf8_lossy(&out.stderr));
    }
    out.stdout
}

/// What a game needs from its ROM: SMB's CHR, or a cartridge's header (its
/// PRG count zeroed, without a trainer) and CHR.
fn rom_data(rom: &[u8], path: &Path, cartridge: bool) -> Vec<u8> {
    assert!(rom.len() >= 16 && &rom[0..4] == b"NES\x1A", "{} is not an iNES ROM", path.display());
    let trainer = if rom[6] & 4 != 0 { 512 } else { 0 };
    let (prg, chr) = (rom[4] as usize * 16384, rom[5] as usize * 8192);
    let start = 16 + trainer + prg;
    let chr_data = rom.get(start..start + chr).unwrap_or_else(|| panic!("{} is shorter than its header says", path.display()));
    if !cartridge {
        return chr_data.to_vec();
    }
    let mut header = rom[..16].to_vec();
    header[4] = 0;
    header[6] &= !4;
    header.extend_from_slice(chr_data);
    header
}

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = manifest.join("../..").canonicalize().unwrap();
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    for var in ["CC", "AR", "LD", "NM", "OBJCOPY"] {
        println!("cargo:rerun-if-env-changed={var}");
    }
    let cc = tool("clang", "CC");
    let ar = tool("llvm-ar", "AR");
    let ld = tool("ld.lld", "LD");
    let nm = tool("llvm-nm", "NM");
    let objcopy = tool("llvm-objcopy", "OBJCOPY");
    let jobs = std::thread::available_parallelism().map_or(4, |n| n.get());

    let mut bindings = String::from("// Generated by build.rs: the games built in.\n\n");
    let mut entries = String::new();
    let mut built = Vec::new();
    for game in GAMES {
        let env_var = format!("{}_ROM", game.id.to_uppercase());
        println!("cargo:rerun-if-env-changed={env_var}");
        let rom_path = env::var(&env_var).map(PathBuf::from).unwrap_or_else(|_| root.join(format!("{}.nes", game.id)));
        let code = root.join(game.dir).join("code.c");
        println!("cargo:rerun-if-changed={}", rom_path.display());
        println!("cargo:rerun-if-changed={}", code.display());
        if !code.exists() || !rom_path.exists() {
            let missing = if !code.exists() { format!("{} (make codegen GAME={})", game.dir, game.id) } else { rom_path.display().to_string() };
            println!("cargo:warning={} left out: no {missing}", game.title);
            continue;
        }
        let rom = fs::read(&rom_path).unwrap_or_else(|e| panic!("cannot read {}: {e}", rom_path.display()));
        fs::write(out.join(format!("{}.data", game.id)), rom_data(&rom, &rom_path, game.cartridge)).unwrap();

        // Compile, a few files at a time.
        let dir = out.join(game.id);
        fs::create_dir_all(&dir).unwrap();
        let sources: Vec<PathBuf> = [format!("{}/code.c", game.dir), format!("{}/data.c", game.dir)]
            .into_iter()
            .chain(RUNTIME.iter().map(|s| s.to_string()))
            .chain((if game.cartridge { CARTRIDGE_RUNTIME } else { SMB_RUNTIME }).iter().map(|s| s.to_string()))
            .map(|s| root.join(s))
            .collect();
        let flags: &[&str] = if game.cartridge { &["-DPPU_BUS", "-DMAPPER", "-DCARTRIDGE_NO_STDIO"] } else { &["-DSMB_LEVEL"] };
        let mut objects = Vec::new();
        for chunk in sources.chunks(jobs) {
            let children: Vec<_> = chunk
                .iter()
                .map(|src| {
                    println!("cargo:rerun-if-changed={}", src.display());
                    let obj = dir.join(format!("{}.o", src.file_stem().unwrap().to_string_lossy()));
                    let child = Command::new(&cc)
                        .args(["--target=armv6k-none-eabihf", "-mcpu=mpcore", "-mfloat-abi=hard", "-mfpu=vfp2", "-mtp=soft"])
                        .args(["-O2", "-std=c99", "-ffreestanding", "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing"])
                        .args(["-Wno-everything"])
                        .args(flags)
                        .arg(format!("-I{}", root.join(game.dir).display()))
                        .arg(format!("-I{}", root.join("codegen/lib").display()))
                        .arg("-c")
                        .arg(src)
                        .arg("-o")
                        .arg(&obj)
                        .spawn()
                        .unwrap_or_else(|e| panic!("cannot run {cc}: {e}"));
                    objects.push(obj);
                    (src.clone(), child)
                })
                .collect();
            for (src, mut child) in children {
                assert!(child.wait().unwrap().success(), "{cc} failed on {}", src.display());
            }
        }

        // One object for the game, its symbols prefixed with its name.
        let merged = dir.join("merged.o");
        run(Command::new(&ld).arg("-r").arg("-o").arg(&merged).args(&objects), &ld);
        let symbols = run(Command::new(&nm).args(["--defined-only", "--extern-only", "--format=just-symbols"]).arg(&merged), &nm);
        let mut renames = String::new();
        for symbol in String::from_utf8(symbols).unwrap().lines().filter(|s| !s.is_empty()) {
            writeln!(renames, "{symbol} {}_{symbol}", game.id).unwrap();
        }
        let renames_file = dir.join("renames.txt");
        fs::write(&renames_file, renames).unwrap();
        let renamed = dir.join(format!("{}.o", game.id));
        run(Command::new(&objcopy).arg(format!("--redefine-syms={}", renames_file.display())).arg(&merged).arg(&renamed), &objcopy);
        let lib = out.join(format!("lib{}.a", game.id));
        let _ = fs::remove_file(&lib);
        run(Command::new(&ar).arg("crs").arg(&lib).arg(&renamed), &ar);
        println!("cargo:rustc-link-lib=static={}", game.id);

        // Its bindings.
        writeln!(bindings, "mod {} {{\n    unsafe extern \"C\" {{", game.id).unwrap();
        for (name, signature) in FUNCTIONS {
            writeln!(bindings, "        #[link_name = \"{}_{name}\"]\n        pub fn {name}{signature};", game.id).unwrap();
        }
        if built.is_empty() {
            writeln!(bindings, "        /// The NES's 64 colours, RGB (the same in every game).\n        #[link_name = \"{}_COLOR_PALETTE\"]\n        pub static COLOR_PALETTE: [u8; 192];", game.id).unwrap();
        }
        writeln!(bindings, "    }}\n}}\n").unwrap();
        let mut fields = String::new();
        for (name, _) in FUNCTIONS {
            write!(fields, "{}: {}::{name}, ", name.trim_start_matches("game_"), game.id).unwrap();
        }
        writeln!(
            entries,
            "    GameInfo {{ id: \"{id}\", title: \"{title}\", data: include_bytes!(concat!(env!(\"OUT_DIR\"), \"/{id}.data\")), layered_level: {level}, fns: GameFns {{ {fields}}} }},",
            id = game.id,
            title = game.title,
            level = !game.cartridge,
        )
        .unwrap();
        built.push(game.id);
    }
    assert!(!built.is_empty(), "no game to build: recompile at least one (make codegen in {}) and put its ROM there", root.display());
    writeln!(bindings, "pub use {}::COLOR_PALETTE;\n", built[0]).unwrap();
    writeln!(bindings, "pub static GAMES: &[GameInfo] = &[\n{entries}];").unwrap();
    fs::write(out.join("games.rs"), bindings).unwrap();
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rerun-if-changed=build.rs");
}
