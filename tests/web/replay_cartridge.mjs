#!/usr/bin/env node
// Replays a recording with the split WebAssembly build of a program that
// drives the hardware registers (Metroid, SMB3): the runtime module for such
// cartridges (web/runtime-cartridge.wasm) and the game module the recompiler
// emits from the ROM, built with the runtime's layout. Compares each frame's
// state hash (codegen/lib/state_hash.c) with a trace of the native build or
// the reference interpreter.
//
//   node tests/web/replay_cartridge.mjs runtime.wasm game.wasm ROM.nes RECORDING TRACE
import fs from 'fs';
import { link } from '../../web/link.js';
import { splitDump } from '../../web/cartridge.js';

const [runtimePath, gamePath, romPath, recordingPath, tracePath] = process.argv.slice(2);
const rom = new Uint8Array(fs.readFileSync(romPath));
const { memory, runtime: wasm, game } = await link(fs.readFileSync(runtimePath), fs.readFileSync(gamePath), rom);

const inputs = [];
for (const line of fs.readFileSync(recordingPath, 'utf8').split('\n')) {
    const match = /^(\d+):(\d+)$/.exec(line.trim());
    if (match) inputs.push([Number(match[1]), Number(match[2])]);
}
const expected = fs.readFileSync(tracePath, 'utf8').trim().split('\n');

wasm.cpu_init();
wasm.apu_init(48000);
const { chr, mapper } = splitDump(rom);
const bytes = new Uint8Array(memory.buffer);
bytes.fill(0, wasm.chr_rom.value, wasm.chr_rom.value + 0x2000);
bytes.set(chr, wasm.chr_rom.value);
bytes[wasm.chr_ram.value] = chr.length === 0 ? 1 : 0;
wasm.video_init_chr(Math.max(chr.length, 0x2000));
wasm.render_init();
wasm.mapper_init(mapper);
game.cpu_reset_entry();

let state = 0;
let next = 0;
const start = performance.now();
for (let frame = 0; frame < expected.length; frame++) {
    while (next < inputs.length && inputs[next][0] <= frame) state = inputs[next++][1];
    wasm.update_controller1(state);
    wasm.next_frame();
    wasm.sound_end_frame();
    const hash = (wasm.hash_state() >>> 0).toString(16).padStart(8, '0');
    if (hash !== expected[frame]) {
        console.log(`The WebAssembly build diverges from ${tracePath} at frame ${frame}`);
        process.exit(1);
    }
}
const ms = performance.now() - start;
console.log(`The WebAssembly build matches ${tracePath} on all ${expected.length} frames (${ms.toFixed(0)} ms)`);
