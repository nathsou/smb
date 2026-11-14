//! Builds the recompiled game for the 3DS's ARM11 with clang (no C library:
//! the runtime only needs memcpy and memset, which Rust provides), and
//! extracts the CHR (the game's tiles) from your ROM. Both come from your
//! own files at build time and never enter the repository:
//!
//! - `make codegen` (in the repository root) generates `codegen/game`;
//! - the ROM is `smb.nes` in the repository root, or `SMB_ROM`.
//!
//! `CC` and `AR` choose the compiler and the archiver (clang and llvm-ar by
//! default).

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

const SOURCES: &[&str] = &[
    "codegen/game/code.c",
    "codegen/game/data.c",
    "codegen/lib/cpu.c",
    "codegen/lib/video.c",
    "codegen/lib/render.c",
    "codegen/native/display.c",
    "codegen/lib/apu.c",
    "codegen/lib/apu_buffers.c",
    "codegen/lib/sound.c",
    "codegen/lib/state.c",
    "codegen/lib/debug_chr.c",
    "codegen/lib/layers.c",
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

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = manifest.join("../..").canonicalize().unwrap();
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    println!("cargo:rerun-if-env-changed=SMB_ROM");
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=AR");

    if !root.join("codegen/game/code.c").exists() {
        panic!("codegen/game is missing: run `make codegen` in {} first (it needs your ROM)", root.display());
    }

    // The CHR from the ROM: after the 16-byte header and the PRG banks.
    let rom_path = env::var("SMB_ROM").map(PathBuf::from).unwrap_or_else(|_| root.join("smb.nes"));
    println!("cargo:rerun-if-changed={}", rom_path.display());
    let rom = fs::read(&rom_path).unwrap_or_else(|e| panic!("cannot read the ROM {}: {e} (set SMB_ROM)", rom_path.display()));
    assert!(rom.len() >= 16 && &rom[0..4] == b"NES\x1A", "{} is not an iNES ROM", rom_path.display());
    let (prg, chr) = (rom[4] as usize * 16384, rom[5] as usize * 8192);
    let chr_data = rom.get(16 + prg..16 + prg + chr).expect("the ROM is shorter than its header says");
    fs::write(out.join("chr.bin"), chr_data).unwrap();

    // The game and its runtime as a static library.
    let cc = find_tool("CC", &["clang", "clang-18", "clang-19", "clang-20"]);
    let ar = find_tool("AR", &["llvm-ar", "llvm-ar-18", "llvm-ar-19", "llvm-ar-20"]);
    let mut objects = Vec::new();
    for src in SOURCES {
        let path = root.join(src);
        println!("cargo:rerun-if-changed={}", path.display());
        let obj = out.join(format!("{}.o", Path::new(src).file_stem().unwrap().to_string_lossy()));
        let status = Command::new(&cc)
            .args(["--target=armv6k-none-eabihf", "-mcpu=mpcore", "-mfloat-abi=hard", "-mfpu=vfp2", "-mtp=soft"])
            .args(["-O2", "-std=c99", "-ffreestanding", "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing"])
            .args(["-Wno-everything"])
            .arg(format!("-I{}", root.join("codegen/game").display()))
            .arg(format!("-I{}", root.join("codegen/lib").display()))
            .arg("-c")
            .arg(&path)
            .arg("-o")
            .arg(&obj)
            .status()
            .unwrap_or_else(|e| panic!("cannot run {cc}: {e}"));
        assert!(status.success(), "{cc} failed on {src}");
        objects.push(obj);
    }
    let lib = out.join("libsmb.a");
    let _ = fs::remove_file(&lib);
    let status = Command::new(&ar).arg("crs").arg(&lib).args(&objects).status().unwrap_or_else(|e| panic!("cannot run {ar}: {e}"));
    assert!(status.success(), "{ar} failed");
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=smb");
}
