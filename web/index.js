import { createGpuRenderer, THEMES } from './gpu.js';
import { link } from './link.js';
import { createViewer } from './viewer.js';
import * as store from './store.js';
import { splitDump } from './cartridge.js';
import { createProgress, RECOMPILER_STEPS } from './progress.js';

const SCREEN_WIDTH = 256;
const SCREEN_HEIGHT = 240;

// The games the page recompiles, told apart by their board: Super Mario
// Bros. (NROM), with its hand-written display routines, and Metroid (MMC1),
// which drives the PPU through its registers and switches banks, with the
// runtime module for such cartridges. The themes read SMB's variables, and
// Metroid writes its own tiles (CHR RAM): both are SMB's only.
const GAMES = {
    smb: {
        title: 'SUPER MARIO BROS.',
        pageTitle: 'SMB Recompiled',
        map: 'smb.map',
        runtime: 'runtime.wasm',
        native: 'routines.txt',
        display: 'display.c',
        extras: true, // themes and debug tiles
        mapKey: 'map',
        saveKey: 'save_state',
    },
    metroid: {
        title: 'METROID',
        pageTitle: 'Metroid Recompiled',
        map: 'metroid.map',
        runtime: 'runtime-mmc1.wasm',
        native: null,
        display: null,
        extras: false,
        mapKey: 'map:metroid',
        saveKey: 'save_state:metroid',
    },
};

function gameOf(rom) {
    return splitDump(rom).mapper === 1 ? GAMES.metroid : GAMES.smb;
}

// A game's files, fetched once.
const gameFiles = new Map();
function filesOf(game) {
    if (!gameFiles.has(game)) {
        const text = name => name ? fetch(name).then(r => r.text()) : Promise.resolve('');
        gameFiles.set(game, Promise.all([
            text(game.map),
            text(game.native),
            text(game.display),
            fetch(game.runtime).then(r => r.arrayBuffer()),
        ]).then(([map, native, display, runtime]) => ({ map, native, display, runtime })));
    }
    return gameFiles.get(game);
}

// onScale gets the device pixels per NES pixel the canvas is shown at.
function createCanvas(onScale) {
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
        onScale(scalingFactor * window.devicePixelRatio);
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

function checkRom(rom) {
    splitDump(rom); // throws with the reason
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
        input.accept = '.nes,.bin,.rom';
        input.onchange = () => input.files[0] && resolve(input.files[0]);
        input.click();
    });
}

// Recompiles in a worker, or reuses an earlier result for the same inputs.
async function build(inputs, viewer, progress) {
    progress.begin('cache');
    const key = await store.digest(inputs.rom, inputs.map, inputs.native, inputs.layout, inputs.version);
    const cached = await store.get('builds', key);
    if (cached) {
        viewer.setStatus('RECOMPILED EARLIER: LOADED FROM THIS BROWSER\'S CACHE');
        progress.skip([...RECOMPILER_STEPS.map(([stage]) => stage), 'store'], 'CACHED');
        return cached;
    }
    viewer.setStatus('RECOMPILING THE ROM...', 'busy');
    const result = await new Promise((resolve, reject) => {
        const worker = new Worker('recompiler-worker.js', { type: 'module' });
        worker.onmessage = ({ data }) => {
            if (data.progress) {
                progress.begin(data.progress, data.progress === 'map' ? undefined : data.took);
                return;
            }
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
    const entry = { wasm: result.wasm, files: result.files, warning: result.warning };
    progress.begin('store', result.took);
    await store.put('builds', key, entry);
    return entry;
}

// A ROM the map was not written for still runs, with a warning.
function showWarning(text) {
    const warning = document.getElementById('rom-warning');
    warning.textContent = text ? text.toUpperCase() : '';
    warning.hidden = !text;
}

// Generated files in reading order, then the hand-written display code.
const FILE_ORDER = ['code.c', 'code.h', 'vars.h', 'constants.h', 'data.h', 'data.c'];
const DEBUG_TILES_KEY = 'debug_tiles';
const THEME_KEY = 'theme';

async function main() {
    // the game of the current run (its map, its save state)
    let currentGame = GAMES.smb;
    // the steps to a running game, in place of the screen until it runs
    const progress = createProgress(document.getElementById('screen'));
    const viewer = createViewer(document.getElementById('code'), {
        onRecompile: text => {
            localStorage.setItem(currentGame.mapKey, text);
            boot();
        },
        onResetMap: () => {
            localStorage.removeItem(currentGame.mapKey);
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

    // The renderer of the current run, for the theme menu and the canvas size.
    let renderer = null;
    const themeMenu = document.getElementById('theme');
    for (const [name, label] of Object.entries(THEMES)) themeMenu.add(new Option(label, name));
    themeMenu.value = localStorage.getItem(THEME_KEY) ?? 'original';
    if (themeMenu.value === '') themeMenu.value = 'original';
    const applyTheme = async () => {
        if (!renderer?.setTheme) return;
        try {
            await renderer.setTheme(currentGame.extras ? themeMenu.value : 'original');
        } catch (error) {
            console.error(error);
            viewer.setStatus(`THEME UNAVAILABLE: ${error.message}`.toUpperCase(), 'error');
        }
        if (currentGame.extras) themeMenu.value = renderer.theme;
    };
    themeMenu.addEventListener('change', () => {
        localStorage.setItem(THEME_KEY, themeMenu.value);
        applyTheme();
    });
    const onScale = scale => renderer?.setDisplayScale?.(scale);

    const [recompiler, apuWasm] = await Promise.all([
        fetch('recompiler.js').then(r => r.arrayBuffer()),
        fetch('apu.wasm').then(r => r.arrayBuffer()),
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

    let { canvas, updateSize } = createCanvas(onScale);
    document.getElementById('screen').prepend(canvas);

    const audioContext = new AudioContext({ latencyHint: 'interactive' });
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) audioContext.suspend();
        else audioContext.resume();
    });
    let audioWorklet = null;
    let session = null;
    // The last value the game wrote to each APU register ($4000-$4017, -1
    // when never written): audio can only start after a key press, and the
    // synthesizer then starts from these instead of from silence.
    const registers = new Int16Array(0x18).fill(-1);

    const initAudio = async () => {
        if (audioWorklet !== null) return;
        await audioContext.resume();
        await audioContext.audioWorklet.addModule('audio-worklet.js');
        audioWorklet = new AudioWorkletNode(audioContext, 'apu-processor', {
            outputChannelCount: [1],
            processorOptions: { wasm: apuWasm.slice(0), registers: Array.from(registers) },
        });
        audioWorklet.connect(audioContext.destination);
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
        currentGame = gameOf(rom);
        document.title = currentGame.pageTitle;
        document.getElementById('game-title').textContent = currentGame.title;
        debugTiles.parentElement.hidden = !currentGame.extras;
        themeMenu.parentElement.hidden = !currentGame.extras;
        canvas.hidden = true;
        progress.show(currentGame.title);
        progress.begin('files');
        const { map: defaultMap, native, display, runtime: runtimeBytes } = await filesOf(currentGame);
        const map = localStorage.getItem(currentGame.mapKey) ?? defaultMap;
        let files = null;
        progress.begin('runtime');
        const { memory, runtime, game } = await link(runtimeBytes, async layout => {
            const layoutText = Object.entries(layout).map(([name, address]) => `${name} ${address}`).join('\n');
            const result = await build({ rom, map, native, layout: layoutText, version }, viewer, progress);
            files = result.files;
            showWarning(result.warning);
            progress.begin('link');
            return result.wasm;
        }, rom);
        progress.begin('start');

        const ordered = FILE_ORDER.map(name => files.find(f => f.name === name)).filter(Boolean);
        viewer.setFiles([
            ...ordered,
            ...(currentGame.display ? [{ name: currentGame.display, contents: display }] : []),
            { name: currentGame.map, contents: map, editable: true, default: defaultMap },
        ]);

        const uint8View = () => new Uint8Array(memory.buffer);
        // the runtime's size: cartridges also save their tiles, RAM and mapper
        const saveStateSize = new DataView(memory.buffer).getUint32(runtime.save_state_size.value, true);
        const lastSaveState = new Uint8Array(memory.buffer, runtime.last_save_state.value, saveStateSize);
        let saveStateReady = false;

        const chrRomPtr = runtime.chr_rom.value;
        const { chr } = splitDump(rom);
        if (debugTiles.checked && currentGame.extras) {
            runtime.debug_chr_generate(chrRomPtr);
        } else {
            // CHR RAM (no CHR in the dump) starts blank: the game writes its tiles
            uint8View().fill(0, chrRomPtr, chrRomPtr + 0x2000);
            uint8View().set(chr, chrRomPtr);
        }
        if (runtime.chr_ram) uint8View()[runtime.chr_ram.value] = chr.length === 0 ? 1 : 0;

        runtime.cpu_init();
        runtime.video_init(chrRomPtr);
        runtime.render_init();
        runtime.mapper_init?.();

        // Draw with WebGL2 unless unavailable or ?software is in the URL.
        await renderer?.setTheme?.('original'); // frees the last run's theme
        renderer = null;
        if (!new URLSearchParams(window.location.search).has('software')) {
            try {
                renderer = await createGpuRenderer(canvas, runtime, memory, 'video.frag', { map });
            } catch (error) {
                console.error('WebGL renderer unavailable:', error);
                // a canvas keeps its first context type: start over with a 2D one
                canvas.remove();
                ({ canvas, updateSize } = createCanvas(onScale));
                document.getElementById('screen').prepend(canvas);
            }
        }
        if (renderer === null) {
            renderer = createSoftwareRenderer(canvas, runtime.render_frame, uint8View(), runtime.frame.value);
        }
        // the themes are WebGL2 only
        themeMenu.disabled = renderer.setTheme === undefined;
        await applyTheme();
        progress.hide();
        canvas.hidden = false;
        updateSize();

        let running = true;
        const current = {
            stop() { running = false; },
            // This frame's APU writes go to the synthesizer on the audio thread.
            sendSound() {
                const count = readUint16(uint8View(), runtime.sound_event_count.value);
                const events = uint8View().slice(runtime.sound_events.value, runtime.sound_events.value + 2 * count);
                for (let i = 0; i < events.length; i += 2) registers[events[i]] = events[i + 1];
                // every frame, empty or not: each marks a frame boundary
                audioWorklet?.port.postMessage({ type: 'frame', events }, [events.buffer]);
                new DataView(memory.buffer).setUint16(runtime.sound_event_count.value, 0, true);
            },
        };
        session = current;

        game.cpu_reset_entry();

        // The game runs at 60 frames per second whatever the display's rate:
        // a frame when 1/60 s has built up. Past a few frames' worth (the tab
        // was hidden, the page stalled), the backlog is dropped rather than
        // caught up, which a faster display would do at its own rate.
        let lastUpdateTime = performance.now();
        let currentFrameTime = 0;
        const targetFrameTime = 1000 / 60;
        const maxBacklog = 3 * targetFrameTime;

        const update = () => {
            if (!running) return;
            const currentTime = performance.now();
            currentFrameTime += currentTime - lastUpdateTime;
            lastUpdateTime = currentTime;
            if (currentFrameTime > maxBacklog) currentFrameTime = targetFrameTime;

            if (currentFrameTime >= targetFrameTime) {
                runtime.update_controller1(joypad1.getState());
                runtime.next_frame();
                current.sendSound();
                renderer.draw();
                currentFrameTime -= targetFrameTime;

                if (shouldSaveState) {
                    runtime.save_state(lastSaveState.byteOffset);
                    localStorage.setItem(currentGame.saveKey, JSON.stringify(Array.from(lastSaveState)));
                    shouldSaveState = false;
                    saveStateReady = true;
                } else if (shouldLoadState) {
                    if (!saveStateReady) {
                        const saveStateData = localStorage.getItem(currentGame.saveKey);
                        const saved = saveStateData ? JSON.parse(saveStateData) : [];
                        // a state of an earlier runtime of another size is left alone
                        if (saved.length === saveStateSize) {
                            lastSaveState.set(saved);
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
            progress.fail(`ERROR: ${error.message}`);
            if (/iNES|mapper|ROM/.test(error.message)) {
                rom = await askForRom(error.message);
                boot();
            }
        }
    }

    boot();
}

main();
