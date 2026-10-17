#!/usr/bin/env node
// Replays rec/warpless.rec with the split WebAssembly build: the runtime
// module compiled from C (web/runtime.wasm) and the game module the
// recompiler emits from the ROM (--wasm), sharing one memory. Checks every
// frame's game-state hash against tests/golden/warpless.trace, with the
// generated debug tiles, and every frame's audio hashes (the APU writes and
// the samples rendered at 48 kHz) against tests/golden/warpless.audio.
//
//   node tests/web/replay_split.mjs [runtime.wasm] [game.wasm] [smb.nes]
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { link } from '../../web/link.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const [runtimePath, gamePath, romPath] = [
    process.argv[2] ?? path.join(root, 'web/runtime.wasm'),
    process.argv[3] ?? path.join(root, 'web/game.wasm'),
    process.argv[4] ?? path.join(root, 'smb.nes'),
];
const FRAMES = 7987;
const STACK_SCRATCH_START = 0x140; // see codegen/hash.c
const STACK_SCRATCH_END = 0x200;

const { memory, runtime: wasm, game } = await link(
    fs.readFileSync(runtimePath), fs.readFileSync(gamePath), new Uint8Array(fs.readFileSync(romPath)));
const view = () => new Uint8Array(memory.buffer);

const inputs = [];
for (const line of fs.readFileSync(path.join(root, 'rec/warpless.rec'), 'utf8').split('\n')) {
    const match = /^(\d+):(\d+)$/.exec(line.trim());
    if (match) inputs.push([Number(match[1]), Number(match[2])]);
}
const expected = fs.readFileSync(path.join(root, 'tests/golden/warpless.trace'), 'utf8').trim().split('\n');
const expectedAudio = fs.readFileSync(path.join(root, 'tests/golden/warpless.audio'), 'utf8').trim().split('\n');
const AUDIO_SAMPLE_RATE = 48000;
const WRITE_HASH_RESET = 2166136261;

const [nametable, palette, oam, ctrl, mask, scrollX] =
    Array.from(new Uint32Array(memory.buffer, wasm.video_layout.value, 7), offset => wasm.video.value + offset);
const ram = wasm.ram.value;

function fnv(hash, bytes) {
    for (const byte of bytes) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
    return hash;
}

function stateHash() {
    const bytes = view();
    let hash = 2166136261;
    hash = fnv(hash, bytes.subarray(ram, ram + STACK_SCRATCH_START));
    hash = fnv(hash, bytes.subarray(ram + STACK_SCRATCH_END, ram + 2048));
    hash = fnv(hash, bytes.subarray(nametable, nametable + 2048));
    hash = fnv(hash, bytes.subarray(palette, palette + 32));
    hash = fnv(hash, bytes.subarray(oam, oam + 256));
    return fnv(hash, [bytes[ctrl], bytes[mask], bytes[scrollX], bytes[scrollX + 1]]);
}

wasm.cpu_init();
wasm.debug_chr_generate(wasm.chr_rom.value);
wasm.video_init(wasm.chr_rom.value);
wasm.render_init();
wasm.apu_init(AUDIO_SAMPLE_RATE);
game.cpu_reset_entry();

const u32 = address => new DataView(memory.buffer).getUint32(address, true);
const FRAME_SAMPLES = AUDIO_SAMPLE_RATE / 60;
function audioHashes() {
    const writes = u32(wasm.apu_write_hash.value);
    wasm.sound_end_frame();
    wasm.apu_render(wasm.apu_output.value, FRAME_SAMPLES);
    const quantised = Int16Array.from(new Float32Array(memory.buffer, wasm.apu_output.value, FRAME_SAMPLES),
        value => Math.floor(value * 32767 + 0.5));
    const samples = fnv(2166136261, new Uint8Array(quantised.buffer));
    return `${writes.toString(16).padStart(8, '0')} ${samples.toString(16).padStart(8, '0')}`;
}

let state = 0;
let next = 0;
const start = performance.now();
for (let frame = 0; frame < FRAMES; frame++) {
    while (next < inputs.length && inputs[next][0] <= frame) state = inputs[next++][1];
    wasm.update_controller1(state);
    new DataView(memory.buffer).setUint32(wasm.apu_write_hash.value, WRITE_HASH_RESET, true);
    wasm.next_frame();
    const audio = audioHashes();
    if (audio !== expectedAudio[frame]) {
        console.log(`Split WebAssembly build's audio diverges from tests/golden/warpless.audio at frame ${frame}`);
        process.exit(1);
    }
    const hash = stateHash().toString(16).padStart(8, '0');
    if (hash !== expected[frame]) {
        console.log(`Split WebAssembly build diverges from tests/golden/warpless.trace at frame ${frame}`);
        process.exit(1);
    }
}
const ms = performance.now() - start;
console.log(`Split WebAssembly build matches tests/golden/warpless.trace and .audio on all ${FRAMES} frames (${ms.toFixed(0)} ms)`);
