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

export async function link(runtimeBytes, gameBytes, rom, pages = 20) {
    const memory = new WebAssembly.Memory({ initial: pages });
    const bytes = () => new Uint8Array(memory.buffer);
    let game = null;
    const runtimeModule = await WebAssembly.compile(runtimeBytes);
    const env = { memory };
    for (const { name, kind } of WebAssembly.Module.imports(runtimeModule)) {
        if (kind !== 'function') continue;
        if (name === 'memset') {
            env.memset = (ptr, value, num) => (bytes().fill(value, ptr, ptr + num), ptr);
        } else if (name === 'memcpy') {
            env.memcpy = (dest, src, num) => (bytes().copyWithin(dest, src, src + num), dest);
        } else {
            env[name] = (...args) => game[name](...args);
        }
    }
    const runtime = (await WebAssembly.instantiate(runtimeModule, { env })).exports;

    const gameModule = await WebAssembly.compile(gameBytes);
    const gameEnv = { memory };
    for (const { name, kind } of WebAssembly.Module.imports(gameModule)) {
        if (kind === 'memory') continue;
        if (!(name in runtime)) throw new Error(`The runtime does not provide ${name}`);
        gameEnv[name] = runtime[name];
    }
    game = (await WebAssembly.instantiate(gameModule, { env: gameEnv })).exports;

    if (rom) loadPrg(memory, runtime.data.value, rom);
    return { memory, runtime, game };
}

// iNES with mapper 0: 16 or 32 KiB of PRG ROM after the header (and a
// trainer, if any). A 16 KiB image appears twice.
export function loadPrg(memory, address, rom) {
    if (rom[0] !== 0x4e || rom[1] !== 0x45 || rom[2] !== 0x53 || rom[3] !== 0x1a) {
        throw new Error('Not an iNES file');
    }
    const banks = rom[4];
    const start = 16 + (rom[6] & 4 ? 512 : 0);
    const prg = rom.subarray(start, start + banks * 0x4000);
    const view = new Uint8Array(memory.buffer, address, 0x8000);
    view.set(prg);
    if (banks === 1) view.set(prg, 0x4000);
}
