import { splitDump } from './cartridge.js';

// Links the two WebAssembly modules of the game into one memory:
//
// - the runtime, compiled from the C runtime (video, audio, the native display
//   routines); it calls game routines, which it imports from `env`;
// - the game, recompiled from the ROM; it imports the runtime's memory,
//   the addresses of its machine state and the runtime functions it uses.
//
// The runtime is instantiated first; its imports of game routines are
// trampolines to the game module's exports, instantiated right after.
// The ROM's PRG banks are copied into the runtime's PRG buffer.
// A runtime built for cartridges with a mapper (web/runtime-mmc1.wasm) also
// has the cartridge's RAM and the bank mapped.

// `gameBytes` may be a function of the runtime's layout, to build the game
// module once the runtime's addresses are known. `hostImports` provides
// runtime imports other than game routines.
export async function link(runtimeBytes, gameBytes, rom, { pages = 20, hostImports = {} } = {}) {
    const memory = new WebAssembly.Memory({ initial: pages });
    const bytes = () => new Uint8Array(memory.buffer);
    let game = null;
    const runtimeModule = await WebAssembly.compile(runtimeBytes);
    const env = { memory };
    for (const { name, kind } of WebAssembly.Module.imports(runtimeModule)) {
        if (kind !== 'function') continue;
        if (name in hostImports) {
            env[name] = hostImports[name];
        } else if (name === 'memset') {
            env.memset = (ptr, value, num) => (bytes().fill(value, ptr, ptr + num), ptr);
        } else if (name === 'memcpy') {
            env.memcpy = (dest, src, num) => (bytes().copyWithin(dest, src, src + num), dest);
        } else {
            env[name] = (...args) => game[name](...args);
        }
    }
    const runtime = (await WebAssembly.instantiate(runtimeModule, { env })).exports;
    if (gameBytes === null) return { memory, runtime, game: null };

    const bytes_ = typeof gameBytes === 'function' ? await gameBytes(runtimeLayout(runtime)) : gameBytes;
    const gameModule = await WebAssembly.compile(bytes_);
    const gameEnv = { memory };
    for (const { name, kind } of WebAssembly.Module.imports(gameModule)) {
        if (kind === 'memory') continue;
        if (!(name in runtime)) throw new Error(`The runtime does not provide ${name}`);
        gameEnv[name] = runtime[name];
    }
    game = (await WebAssembly.instantiate(gameModule, { env: gameEnv })).exports;

    if (rom) loadPrg(memory, runtime.data.value, rom, runtime);
    return { memory, runtime, game };
}

// Addresses of the objects the game module reads and writes directly: the
// machine state, and (with a mapper) the cartridge's RAM and the bank mapped,
// and the runtime state that suspension and exact returns read.
export const MACHINE_STATE = [
    'ram', 'data', 'a', 'x', 'y', 'sp', 'carry_flag', 'zero_flag', 'neg_flag',
    'overflow_flag', 'interrupt_disabled', 'decimal_flag',
    'wram', 'prg_window', 'prg_bank', 'cpu_return', 'cpu_suspended', 'cpu_resume_depth', 'cpu_resume_point',
];

export function runtimeLayout(runtime) {
    return Object.fromEntries(MACHINE_STATE.filter(name => name in runtime).map(name => [name, runtime[name].value]));
}

// The PRG banks of an iNES file or raw dump: a 16 KiB image appears twice;
// with a mapper every bank, and the runtime learns how many.
export function loadPrg(memory, address, rom, runtime = null) {
    const { prg } = splitDump(rom);
    const view = new Uint8Array(memory.buffer, address, Math.max(prg.length, 0x8000));
    view.set(prg);
    if (prg.length === 0x4000) view.set(prg, 0x4000);
    if (runtime?.prg_image_size) {
        new DataView(memory.buffer).setUint32(runtime.prg_image_size.value, Math.max(prg.length, 0x8000), true);
    }
}
