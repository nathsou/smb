#!/usr/bin/env node
// Checks the page's audio path against tests/golden/warpless.audio: the
// recording replayed with the split WebAssembly build, each frame's APU
// writes taken as web/index.js takes them and posted to the AudioWorklet
// processor (web/audio-worklet.js, running web/apu.wasm), which renders in
// the 128-sample quanta browsers use. The page posts a frame each frame; here
// the game runs one frame ahead of the audio so that every frame is queued
// by its boundary, as when the audio thread keeps a little latency.
//
//   node tests/web/worklet.mjs [runtime.wasm] [game.wasm] [smb.nes]
import fs from 'fs';
import path from 'path';
import vm from 'vm';
import { fileURLToPath } from 'url';
import { link } from '../../web/link.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const [runtimePath, gamePath, romPath] = [
    process.argv[2] ?? path.join(root, 'web/runtime.wasm'),
    process.argv[3] ?? path.join(root, 'web/game.wasm'),
    process.argv[4] ?? path.join(root, 'smb.nes'),
];
const FRAMES = 7987;
const RATE = 48000;
const FRAME_SAMPLES = RATE / 60;
const QUANTUM = 128;

// AudioWorkletGlobalScope, as far as the processor uses it.
let Processor = null;
const scope = {
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
    registerProcessor: (_name, cls) => { Processor = cls; },
    sampleRate: RATE,
    WebAssembly,
    Uint8Array,
    Float32Array,
};
vm.runInNewContext(fs.readFileSync(path.join(root, 'web/audio-worklet.js'), 'utf8'), scope);
const processor = new Processor({
    processorOptions: { wasm: fs.readFileSync(path.join(root, 'web/apu.wasm')), registers: new Array(0x18).fill(-1) },
});

const { memory, runtime, game } = await link(
    fs.readFileSync(runtimePath), fs.readFileSync(gamePath), new Uint8Array(fs.readFileSync(romPath)));
const bytes = () => new Uint8Array(memory.buffer);
const data = () => new DataView(memory.buffer);
runtime.cpu_init();
runtime.debug_chr_generate(runtime.chr_rom.value);
runtime.video_init(runtime.chr_rom.value);
runtime.render_init();
game.cpu_reset_entry();

const inputs = [];
for (const line of fs.readFileSync(path.join(root, 'rec/warpless.rec'), 'utf8').split('\n')) {
    const match = /^(\d+):(\d+)$/.exec(line.trim());
    if (match) inputs.push([Number(match[1]), Number(match[2])]);
}
const expected = fs.readFileSync(path.join(root, 'tests/golden/warpless.audio'), 'utf8').trim().split('\n');

let state = 0;
let next = 0;
function gameFrame(frame) {
    while (next < inputs.length && inputs[next][0] <= frame) state = inputs[next++][1];
    runtime.update_controller1(state);
    runtime.next_frame();
    // as web/index.js sends them
    const count = data().getUint16(runtime.sound_event_count.value, true);
    const events = bytes().slice(runtime.sound_events.value, runtime.sound_events.value + 2 * count);
    data().setUint16(runtime.sound_event_count.value, 0, true);
    processor.port.onmessage({ data: { type: 'frame', events } });
}

function fnv(hash, values) {
    for (const value of values) hash = Math.imul(hash ^ value, 16777619) >>> 0;
    return hash;
}

const samples = new Int16Array(FRAMES * FRAME_SAMPLES + QUANTUM);
let rendered = 0;
const output = [[new Float32Array(QUANTUM)]];
gameFrame(0);
for (let frame = 0; frame < FRAMES; frame++) {
    if (frame + 1 < FRAMES) gameFrame(frame + 1);
    while (rendered < (frame + 1) * FRAME_SAMPLES) {
        processor.process([], output);
        // as 16-bit values, like ./hash
        for (const value of output[0][0]) samples[rendered++] = Math.floor(value * 32767 + 0.5);
    }
    const hash = fnv(2166136261, new Uint8Array(samples.buffer, frame * FRAME_SAMPLES * 2, FRAME_SAMPLES * 2));
    if (hash.toString(16).padStart(8, '0') !== expected[frame].split(' ')[1]) {
        console.log(`AudioWorklet samples diverge from tests/golden/warpless.audio at frame ${frame}`);
        process.exit(1);
    }
}
console.log(`AudioWorklet samples match tests/golden/warpless.audio on all ${FRAMES} frames`);
