#!/usr/bin/env node
// Runs a C fixture harness (tests/semantics.c, tests/nrom128.c) compiled
// into a runtime module against the WebAssembly game module the recompiler
// emits for the fixture, linked like the page links the game.
//   node tests/web/fixture.mjs runtime.wasm game.wasm fixture.nes
import fs from 'fs';
import { link } from '../../web/link.js';

const [runtimePath, gamePath, romPath] = process.argv.slice(2);
let memory = null;
const text = pointer => {
    const bytes = new Uint8Array(memory.buffer);
    let end = pointer;
    while (bytes[end] !== 0) end++;
    return new TextDecoder().decode(bytes.subarray(pointer, end));
};
const linked = await link(fs.readFileSync(runtimePath), fs.readFileSync(gamePath),
    new Uint8Array(fs.readFileSync(romPath)), {
        hostImports: {
            fixture_fail: line => { throw new Error(`assertion failed at line ${line}`); },
            fixture_pass: message => console.log(`${text(message)} (WebAssembly backend)`),
        },
    });
memory = linked.memory;
(linked.runtime.main ?? linked.runtime.__main_void)();
