// Runs the recompiler (src/web, built to recompiler.js with
// `moon build --target js`) off the main thread.
//
// Message in:  { rom: Uint8Array, map, native, layout } (strings)
// Messages out: { progress, took } as each stage starts (its key,
//          src/pipeline; the last stage's milliseconds), then
//          { wasm: Uint8Array, files: [{ name, contents }], warning, ms, took }
//          or { error }
import * as recompiler from './recompiler.js';

self.onmessage = ({ data: { rom, map, native, layout } }) => {
    const start = performance.now();
    let stageStart = start;
    const took = () => {
        const now = performance.now();
        const ms = now - stageStart;
        stageStart = now;
        return ms;
    };
    const error = recompiler.recompile(rom, map, native, layout, stage => {
        self.postMessage({ progress: stage, took: took() });
    });
    if (error !== '') {
        self.postMessage({ error });
        return;
    }
    const files = [];
    for (let i = 0; i < recompiler.file_count(); i++) {
        files.push({ name: recompiler.file_name(i), contents: recompiler.file_contents(i) });
    }
    const wasm = recompiler.wasm().slice();
    const warning = recompiler.warning();
    self.postMessage({ wasm, files, warning, ms: performance.now() - start, took: took() }, [wasm.buffer]);
};
