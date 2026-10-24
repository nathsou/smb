#!/usr/bin/env node
// Replays rec/warpless.rec with the WebAssembly build (web/smb.wasm) and the
// generated debug tiles, and checks every frame's game-state hash against
// tests/golden/warpless.trace, like codegen/hash.c does for native builds.
// Build first with `make wasm`.
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const FRAMES = 7987;
const STACK_SCRATCH_START = 0x140; // see codegen/hash.c
const STACK_SCRATCH_END = 0x200;

const memory = new WebAssembly.Memory({ initial: 32 }); // room for the runtime to grow
const view = () => new Uint8Array(memory.buffer);
const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(root, 'web/smb.wasm')), {
    env: {
        memory,
        // same contract as web/index.js: return the destination like C does
        memset: (ptr, value, num) => (view().fill(value, ptr, ptr + num), ptr),
        memcpy: (dest, src, num) => (view().copyWithin(dest, src, src + num), dest),
    },
});
const wasm = instance.exports;

const inputs = [];
for (const line of fs.readFileSync(path.join(root, 'rec/warpless.rec'), 'utf8').split('\n')) {
    const match = /^(\d+):(\d+)$/.exec(line.trim());
    if (match) inputs.push([Number(match[1]), Number(match[2])]);
}
const expected = fs.readFileSync(path.join(root, 'tests/golden/warpless.trace'), 'utf8').trim().split('\n');

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
    // ctrl, mask, scroll_x and scroll_y are consecutive
    return fnv(hash, [bytes[ctrl], bytes[mask], bytes[scrollX], bytes[scrollX + 1]]);
}

wasm.cpu_init();
wasm.debug_chr_generate(wasm.chr_rom.value);
wasm.video_init(wasm.chr_rom.value);
wasm.render_init();
wasm.cpu_reset_entry();

let state = 0;
let next = 0;
for (let frame = 0; frame < FRAMES; frame++) {
    while (next < inputs.length && inputs[next][0] <= frame) state = inputs[next++][1];
    wasm.update_controller1(state);
    wasm.next_frame();
    const hash = stateHash().toString(16).padStart(8, '0');
    if (hash !== expected[frame]) {
        console.log(`WebAssembly state diverges from tests/golden/warpless.trace at frame ${frame}`);
        process.exit(1);
    }
}
console.log(`WebAssembly build matches tests/golden/warpless.trace on all ${FRAMES} frames`);
