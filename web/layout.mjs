#!/usr/bin/env node
// Prints the runtime module's addresses of the machine state, the layout
// the recompiler bakes into the game module (--wasm-layout).
//   node web/layout.mjs web/runtime.wasm > web/layout.txt
import fs from 'fs';
import { link, runtimeLayout } from './link.js';

const { runtime } = await link(fs.readFileSync(process.argv[2]), null, null);
for (const [name, address] of Object.entries(runtimeLayout(runtime))) console.log(name, address);
