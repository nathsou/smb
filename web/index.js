import { createGpuRenderer } from './gpu.js';
import { link } from './link.js';
import { createViewer } from './viewer.js';
import * as store from './store.js';

const SCREEN_WIDTH = 256;
const SCREEN_HEIGHT = 240;
const AUDIO_CHUNK_SIZE = 1024;
const SAVE_STATE_SIZE = 4384; // RAM_SIZE + NAMETABLE_SIZE + PALETTE_SIZE + OAM_SIZE
const SAVE_STATE_LOCAL_STORAGE_KEY = 'save_state';

function createCanvas() {
    const canvas = document.createElement('canvas');
    const screen = document.getElementById('screen');

    // the largest whole multiple of the NES resolution that fits
    const updateSize = () => {
        const width = screen.clientWidth - 16;
        const height = screen.clientHeight - 48;
        const scalingFactor = Math.max(
            Math.min(Math.floor(width / SCREEN_WIDTH), Math.floor(height / SCREEN_HEIGHT)), 1);
        canvas.style.width = `${SCREEN_WIDTH * scalingFactor}px`;
        canvas.style.height = `${SCREEN_HEIGHT * scalingFactor}px`;
    };

    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;
    new ResizeObserver(updateSize).observe(screen);

    return { canvas, updateSize };
}

// Software fallback: copy the C renderer's RGB frame into a 2D canvas.
function createSoftwareRenderer(canvas, renderVideo, uint8View, framePtr) {
    const ctx = canvas.getContext('2d');

    if (ctx === null) {
        throw new Error('Failed to get 2d context');
    }

    const imageData = ctx.createImageData(SCREEN_WIDTH, SCREEN_HEIGHT);

    return {
        draw() {
            renderVideo();

            for (let i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                imageData.data[i * 4] = uint8View[framePtr + i * 3];
                imageData.data[i * 4 + 1] = uint8View[framePtr + i * 3 + 1];
                imageData.data[i * 4 + 2] = uint8View[framePtr + i * 3 + 2];
                imageData.data[i * 4 + 3] = 255;
            }

            ctx.putImageData(imageData, 0, 0);
        },
    };
}

const SUPPORTED_GAMEPAD_MAPPINGS = {
    'SNES Controller (Vendor: 057e Product: 2017)': {
        a: gamepad => gamepad.buttons[1].pressed,
        b: gamepad => gamepad.buttons[0].pressed || gamepad.buttons[3].pressed,
        select: gamepad => gamepad.buttons[8].pressed,
        start: gamepad => gamepad.buttons[9].pressed,
        load: gamepad => gamepad.buttons[4].pressed,
        save: gamepad => gamepad.buttons[5].pressed,
        up: gamepad => Math.abs(gamepad.axes[9] - -1) < 0.01,
        down: gamepad => Math.abs(gamepad.axes[9] - 0.14285719394683838) < 0.01,
        left: gamepad => Math.abs(gamepad.axes[9] - 0.7142857313156128) < 0.01,
        right: gamepad => Math.abs(gamepad.axes[9] - -0.4285714030265808) < 0.01,
    },
};

function getGamepad() {
    for (const gamepad of navigator.getGamepads()) {
        if (gamepad && gamepad.connected && SUPPORTED_GAMEPAD_MAPPINGS[gamepad.id]) {
            return { gamepad, mapping: SUPPORTED_GAMEPAD_MAPPINGS[gamepad.id] };
        }
    }

    return null;
}

function createController(onStart, saveState) {
    let state = 0;
    let gamepadConnected = false;

    const CONTROLLER_RIGHT = 0b10000000;
    const CONTROLLER_LEFT = 0b01000000;
    const CONTROLLER_DOWN = 0b00100000;
    const CONTROLLER_UP = 0b00010000;
    const CONTROLLER_START = 0b00001000;
    const CONTROLLER_SELECT = 0b00000100;
    const CONTROLLER_B = 0b00000010;
    const CONTROLLER_A = 0b00000001;

    const keyMap = {
        up: 'w',
        left: 'a',
        down: 's',
        right: 'd',
        a: 'l',
        b: 'k',
        start: 'Enter',
        select: ' ',
        saveState: 'z',
        loadState: 'x',
    };

    window.addEventListener('gamepadconnected', () => {
        gamepadConnected = true;
    });

    window.addEventListener('gamepaddisconnected', () => {
        for (const gamepad of navigator.getGamepads()) {
            if (gamepad && gamepad.connected) {
                gamepadConnected = true;
                return;
            }
        }

        gamepadConnected = false;
    });

    const typing = event => event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement;

    window.addEventListener('keydown', (event) => {
        if (typing(event)) return;
        const key = event.key;

        switch (key) {
            case keyMap.up:
                state |= CONTROLLER_UP;
                break;
            case keyMap.left:
                state |= CONTROLLER_LEFT;
                break;
            case keyMap.down:
                state |= CONTROLLER_DOWN;
                break;
            case keyMap.right:
                state |= CONTROLLER_RIGHT;
                break;
            case keyMap.a:
                state |= CONTROLLER_A;
                break;
            case keyMap.b:
                state |= CONTROLLER_B;
                break;
            case keyMap.start:
                state |= CONTROLLER_START;
                onStart();
                break;
            case keyMap.select:
                state |= CONTROLLER_SELECT;
                break;
            case keyMap.saveState:
                saveState.save();
                break;
            case keyMap.loadState:
                onStart();
                saveState.load();
                break;
        }
    });

    window.addEventListener('keyup', (event) => {
        if (typing(event)) return;
        const key = event.key;

        switch (key) {
            case keyMap.up:
                state &= ~CONTROLLER_UP;
                break;
            case keyMap.left:
                state &= ~CONTROLLER_LEFT;
                break;
            case keyMap.down:
                state &= ~CONTROLLER_DOWN;
                break;
            case keyMap.right:
                state &= ~CONTROLLER_RIGHT;
                break;
            case keyMap.a:
                state &= ~CONTROLLER_A;
                break;
            case keyMap.b:
                state &= ~CONTROLLER_B;
                break;
            case keyMap.start:
                state &= ~CONTROLLER_START;
                break;
            case keyMap.select:
                state &= ~CONTROLLER_SELECT;
                break;
        }
    });

    function pollGamepad() {
        if (gamepadConnected) {
            // we need to call navigator.getGamepads to get the latest gamepad state
            const gamepadInfo = getGamepad();
            if (gamepadInfo) {
                const { gamepad, mapping } = gamepadInfo;
                let controllerState = 0;

                if (mapping.a(gamepad)) {
                    controllerState |= CONTROLLER_A;
                }

                if (mapping.b(gamepad)) {
                    controllerState |= CONTROLLER_B;
                }

                if (mapping.up(gamepad)) {
                    controllerState |= CONTROLLER_UP;
                }

                if (mapping.down(gamepad)) {
                    controllerState |= CONTROLLER_DOWN;
                }

                if (mapping.left(gamepad)) {
                    controllerState |= CONTROLLER_LEFT;
                }

                if (mapping.right(gamepad)) {
                    controllerState |= CONTROLLER_RIGHT;
                }

                if (mapping.start(gamepad)) {
                    controllerState |= CONTROLLER_START;
                    onStart();
                }

                if (mapping.select(gamepad)) {
                    controllerState |= CONTROLLER_SELECT;
                }

                if (mapping.save(gamepad)) {
                    saveState.save();
                }

                if (mapping.load(gamepad)) {
                    onStart();
                    saveState.load();
                }

                return controllerState;
            }
        }

        return 0;
    }

    return {
        getState: () => state | pollGamepad(),
    };
}

function readUint16(buffer, ptr) {
    return buffer[ptr] + buffer[ptr + 1] * 256;
}

// iNES: CHR ROM follows the PRG banks.
function chrOf(rom) {
    const start = 16 + (rom[6] & 4 ? 512 : 0) + rom[4] * 0x4000;
    return rom.subarray(start, start + 8192);
}

function checkRom(rom) {
    if (rom.length < 16 || rom[0] !== 0x4e || rom[1] !== 0x45 || rom[2] !== 0x53 || rom[3] !== 0x1a) {
        throw new Error('This is not an iNES file.');
    }
    if ((rom[6] >> 4 | (rom[7] & 0xf0)) !== 0 || rom[4] < 1 || rom[4] > 2) {
        throw new Error('Only mapper 0 (NROM) games are supported.');
    }
}

// The ROM: from storage, or asked for (file picker or drop).
function askForRom(error = '') {
    const dialog = document.getElementById('dialog');
    const message = document.getElementById('dialog-error');
    message.textContent = error;
    dialog.hidden = false;
    return new Promise(resolve => {
        const accept = async file => {
            const rom = new Uint8Array(await file.arrayBuffer());
            try {
                checkRom(rom);
            } catch (e) {
                message.textContent = e.message;
                return;
            }
            await store.put('rom', 'rom', rom);
            dialog.hidden = true;
            resolve(rom);
        };
        document.getElementById('dialog-load').onclick = () => pickFile().then(accept);
        dialog.ondragover = event => { event.preventDefault(); dialog.classList.add('dragging'); };
        dialog.ondragleave = () => dialog.classList.remove('dragging');
        dialog.ondrop = event => {
            event.preventDefault();
            dialog.classList.remove('dragging');
            if (event.dataTransfer.files[0]) accept(event.dataTransfer.files[0]);
        };
    });
}

function pickFile() {
    return new Promise(resolve => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.nes';
        input.onchange = () => input.files[0] && resolve(input.files[0]);
        input.click();
    });
}

// Recompiles in a worker, or reuses an earlier result for the same inputs.
async function build(inputs, viewer) {
    const key = await store.digest(inputs.rom, inputs.map, inputs.native, inputs.layout, inputs.version);
    const cached = await store.get('builds', key);
    if (cached) {
        viewer.setStatus('RECOMPILED EARLIER: LOADED FROM THIS BROWSER\'S CACHE');
        return cached;
    }
    viewer.setStatus('RECOMPILING THE ROM...', 'busy');
    const result = await new Promise((resolve, reject) => {
        const worker = new Worker('recompiler-worker.js', { type: 'module' });
        worker.onmessage = ({ data }) => {
            worker.terminate();
            if (data.error) reject(new Error(data.error));
            else resolve(data);
        };
        worker.onerror = event => {
            worker.terminate();
            reject(new Error(event.message || 'The recompiler failed to start'));
        };
        worker.postMessage({ rom: inputs.rom, map: inputs.map, native: inputs.native, layout: inputs.layout });
    });
    viewer.setStatus(`RECOMPILED IN ${(result.ms / 1000).toFixed(1)} S: ${(result.wasm.length / 1024).toFixed(0)} KB OF WEBASSEMBLY`);
    const entry = { wasm: result.wasm, files: result.files };
    await store.put('builds', key, entry);
    return entry;
}

// Generated files in reading order, then the hand-written display code.
const FILE_ORDER = ['code.c', 'code.h', 'constants.h', 'data.h', 'data.c'];
const MAP_OVERRIDE_KEY = 'map';
const DEBUG_TILES_KEY = 'debug_tiles';

async function main() {
    const viewer = createViewer(document.getElementById('code'), {
        onRecompile: text => {
            localStorage.setItem(MAP_OVERRIDE_KEY, text);
            boot();
        },
        onResetMap: () => {
            localStorage.removeItem(MAP_OVERRIDE_KEY);
            boot();
        },
    });
    const toggleCode = document.getElementById('toggle-code');
    toggleCode.addEventListener('click', () => {
        const hidden = document.body.classList.toggle('code-hidden');
        toggleCode.textContent = hidden ? 'SHOW CODE' : 'HIDE CODE';
        viewer.refresh();
        updateSize();
    });

    const debugTiles = document.getElementById('debug-tiles');
    debugTiles.checked = localStorage.getItem(DEBUG_TILES_KEY) === '1';
    debugTiles.addEventListener('change', () => {
        localStorage.setItem(DEBUG_TILES_KEY, debugTiles.checked ? '1' : '0');
        boot();
    });

    const [defaultMap, native, display, runtimeBytes, recompiler] = await Promise.all([
        fetch('smb.map').then(r => r.text()),
        fetch('routines.txt').then(r => r.text()),
        fetch('display.c').then(r => r.text()),
        fetch('runtime.wasm').then(r => r.arrayBuffer()),
        fetch('recompiler.js').then(r => r.arrayBuffer()),
    ]);
    const version = await store.digest(new Uint8Array(recompiler));

    let rom = await store.get('rom', 'rom');
    document.getElementById('load-rom').addEventListener('click', async () => {
        const file = await pickFile();
        const bytes = new Uint8Array(await file.arrayBuffer());
        try {
            checkRom(bytes);
        } catch (e) {
            viewer.setStatus(e.message.toUpperCase(), 'error');
            return;
        }
        await store.put('rom', 'rom', bytes);
        rom = bytes;
        boot();
    });

    let { canvas, updateSize } = createCanvas();
    document.getElementById('screen').prepend(canvas);

    const audioContext = new AudioContext({ latencyHint: 'interactive' });
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) audioContext.suspend();
        else audioContext.resume();
    });
    let audioWorklet = null;
    let lastAudioTime = 0;
    let session = null;

    const initAudio = async () => {
        if (audioWorklet !== null) return;
        await audioContext.resume();
        await audioContext.audioWorklet.addModule('audio-worklet.js');
        audioWorklet = new AudioWorkletNode(audioContext, 'apu-audio-processor', {
            outputChannelCount: [1],
            processorOptions: { sampleRate: audioContext.sampleRate },
        });
        audioWorklet.port.onmessage = event => {
            if (event.data.type === 'bufferLow') session?.processAudio();
        };
        audioWorklet.connect(audioContext.destination);
        lastAudioTime = audioContext.currentTime;
    };

    let shouldSaveState = false;
    let shouldLoadState = false;
    const joypad1 = createController(initAudio, {
        save() { shouldSaveState = true; },
        load() { shouldLoadState = true; },
    });

    // One run of the game: recompiled module, linked runtime, frame loop.
    async function start() {
        session?.stop();
        session = null;
        while (!rom) rom = await askForRom();
        const map = localStorage.getItem(MAP_OVERRIDE_KEY) ?? defaultMap;
        let files = null;
        const { memory, runtime, game } = await link(runtimeBytes, async layout => {
            const layoutText = Object.entries(layout).map(([name, address]) => `${name} ${address}`).join('\n');
            const result = await build({ rom, map, native, layout: layoutText, version }, viewer);
            files = result.files;
            return result.wasm;
        }, rom);

        const ordered = FILE_ORDER.map(name => files.find(f => f.name === name)).filter(Boolean);
        viewer.setFiles([
            ...ordered,
            { name: 'display.c', contents: display },
            { name: 'smb.map', contents: map, editable: true, default: defaultMap },
        ]);

        const uint8View = () => new Uint8Array(memory.buffer);
        const audioBufferSize = readUint16(uint8View(), runtime.audio_buffer_size.value);
        const samples = new Uint8Array(memory.buffer, runtime.web_audio_buffer.value, audioBufferSize);
        const lastSaveState = new Uint8Array(memory.buffer, runtime.last_save_state.value, SAVE_STATE_SIZE);
        let saveStateReady = false;

        const chrRomPtr = runtime.chr_rom.value;
        if (debugTiles.checked) runtime.debug_chr_generate(chrRomPtr);
        else uint8View().set(chrOf(rom), chrRomPtr);

        runtime.cpu_init();
        runtime.video_init(chrRomPtr);
        runtime.render_init();
        runtime.apu_init(audioContext.sampleRate);

        // Draw with WebGL2 unless unavailable or ?software is in the URL.
        let renderer = null;
        if (!new URLSearchParams(window.location.search).has('software')) {
            try {
                renderer = await createGpuRenderer(canvas, runtime, memory);
            } catch (error) {
                console.error('WebGL renderer unavailable:', error);
                // a canvas keeps its first context type: start over with a 2D one
                canvas.remove();
                ({ canvas, updateSize } = createCanvas());
                document.getElementById('screen').prepend(canvas);
            }
        }
        if (renderer === null) {
            renderer = createSoftwareRenderer(canvas, runtime.render_frame, uint8View(), runtime.frame.value);
        }
        updateSize();

        const normalizedSamples = new Float32Array(AUDIO_CHUNK_SIZE);
        let running = true;
        const current = {
            stop() { running = false; },
            processAudio() {
                if (audioWorklet === null || !running) return;
                const currentTime = audioContext.currentTime;
                const samplesNeeded = Math.floor((currentTime - lastAudioTime) * audioContext.sampleRate);
                const chunkSize = Math.min(AUDIO_CHUNK_SIZE, samplesNeeded);
                runtime.apu_fill_buffer(samples.byteOffset, chunkSize);
                for (let i = 0; i < chunkSize; i++) normalizedSamples[i] = (samples[i] / 255) * 2 - 1;
                audioWorklet.port.postMessage({ type: 'samples', samples: normalizedSamples, chunkSize });
                lastAudioTime = currentTime;
            },
        };
        session = current;

        game.cpu_reset_entry();

        let lastUpdateTime = performance.now();
        let currentFrameTime = lastUpdateTime;
        const targetFrameTime = 1000 / 60;

        const update = () => {
            if (!running) return;
            const currentTime = performance.now();
            currentFrameTime += currentTime - lastUpdateTime;
            lastUpdateTime = currentTime;

            if (currentFrameTime >= targetFrameTime) {
                runtime.update_controller1(joypad1.getState());
                runtime.next_frame();
                runtime.apu_step_frame();
                current.processAudio();
                renderer.draw();
                currentFrameTime -= targetFrameTime;

                if (shouldSaveState) {
                    runtime.save_state(lastSaveState.byteOffset);
                    localStorage.setItem(SAVE_STATE_LOCAL_STORAGE_KEY, JSON.stringify(Array.from(lastSaveState)));
                    shouldSaveState = false;
                    saveStateReady = true;
                } else if (shouldLoadState) {
                    if (!saveStateReady) {
                        const saveStateData = localStorage.getItem(SAVE_STATE_LOCAL_STORAGE_KEY);
                        if (saveStateData) {
                            lastSaveState.set(JSON.parse(saveStateData));
                            saveStateReady = true;
                        }
                    }
                    if (saveStateReady) runtime.load_state(lastSaveState.byteOffset);
                    shouldLoadState = false;
                }
            }

            requestAnimationFrame(update);
        };

        update();
    }

    async function boot() {
        try {
            await start();
        } catch (error) {
            console.error(error);
            viewer.setStatus(`ERROR: ${error.message}`.toUpperCase(), 'error');
            if (/iNES|mapper|ROM/.test(error.message)) {
                rom = await askForRom(error.message);
                boot();
            }
        }
    }

    boot();
}

main();
