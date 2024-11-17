const SCREEN_WIDTH = 256;
const SCREEN_HEIGHT = 240;
const AUDIO_CHUNK_SIZE = 1024;
const SAVE_STATE_SIZE = 4384; // RAM_SIZE + NAMETABLE_SIZE + PALETTE_SIZE + OAM_SIZE
const SAVE_STATE_LOCAL_STORAGE_KEY = 'save_state';

function createCanvas() {
    const canvas = document.createElement('canvas');

    if (canvas === null) {
        throw new Error('Failed to create canvas');
    }

    const updateSize = () => {
        const scalingFactor = Math.max(
            Math.min(
                Math.floor(window.innerWidth / SCREEN_WIDTH),
                Math.floor(window.innerHeight / SCREEN_HEIGHT),
            ), 1);

        canvas.style.width = `${SCREEN_WIDTH * scalingFactor}px`;
        canvas.style.height = `${SCREEN_HEIGHT * scalingFactor}px`;
    };

    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;
    updateSize();

    const context = canvas.getContext('2d');

    if (context === null) {
        throw new Error('Failed to get 2d context');
    }

    return { canvas, context, updateSize };
}

function createController(onStart, saveState) {
    let state = 0;

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

    window.addEventListener('keydown', (event) => {
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

    return {
        getState: () => state,
    };
}

// Rest of the utility functions remain the same
function createUploadButton() {
    const button = document.createElement('button');
    button.textContent = 'Upload ROM or CHR';
    button.style.fontSize = '24px';
    button.style.padding = '10px 20px';
    button.style.marginTop = '20px';
    return button;
}

function displayMessage(message) {
    const msg = document.createElement('div');
    msg.textContent = message;
    msg.style.fontSize = '18px';
    msg.style.marginTop = '10px';
    return msg;
}

const CHR_ROM_LOCAL_STORAGE_KEY = 'chr_rom';

async function getCHRROM() {
    const chrRomData = localStorage.getItem(CHR_ROM_LOCAL_STORAGE_KEY);

    if (chrRomData) {
        const byteString = atob(chrRomData);
        const array = new Uint8Array(byteString.length);
        for (let i = 0; i < byteString.length; i++) {
            array[i] = byteString.charCodeAt(i);
        }
        return array;
    } else {
        const container = document.createElement('div');
        container.style.textAlign = 'center';
        const uploadButton = createUploadButton();
        const instruction = displayMessage('Please upload a NES ROM or CHR ROM dump to extract graphics data from.');

        container.appendChild(instruction);
        container.appendChild(uploadButton);
        document.body.appendChild(container);

        return new Promise((resolve, reject) => {
            uploadButton.addEventListener('click', () => {
                const input = document.createElement('input');
                input.type = 'file';
                input.accept = '.nes,.chr';

                input.onchange = () => {
                    const file = input.files[0];

                    if (!file) {
                        reject(new Error('No file selected'));
                        return;
                    }

                    const reader = new FileReader();
                    reader.onload = () => {
                        const arrayBuffer = reader.result;
                        const rom = new Uint8Array(arrayBuffer);
                        if (rom.length < 8192) {
                            reject(new Error('ROM file is too small'));
                            return;
                        }
                        const chrRom = rom.slice(-8192);
                        let binary = '';
                        const len = chrRom.byteLength;
                        for (let i = 0; i < len; i++) {
                            binary += String.fromCharCode(chrRom[i]);
                        }
                        const base64String = btoa(binary);
                        localStorage.setItem(CHR_ROM_LOCAL_STORAGE_KEY, base64String);
                        document.body.removeChild(container);
                        resolve(chrRom);
                    };
                    reader.onerror = () => {
                        reject(new Error('Failed to read file'));
                    };
                    reader.readAsArrayBuffer(file);
                };

                input.click();
            });
        });
    }
}

function readUint16(buffer, ptr) {
    return buffer[ptr] + buffer[ptr + 1] * 256;
}

async function main() {
    const memory = new WebAssembly.Memory({ initial: 10 });
    const uint8View = new Uint8Array(memory.buffer);

    const { instance } = await WebAssembly.instantiateStreaming(fetch('smb.wasm'), {
        env: {
            memory,
            abort(_msg, _file, line, column) {
                console.error(`[abort] at ${line}:${column} in ${_file}: ${_msg}`);
            },
            memset: (ptr, value, num) => {
                uint8View.fill(value, ptr, ptr + num);
            },
            memcpy: (dest, src, num) => {
                uint8View.copyWithin(dest, src, src + num);
            },
        },
    });

    const {
        cpu_init: initCPU,
        ppu_init: initPPU,
        apu_init: initAPU,
        Start: start,
        ppu_render: renderPPU,
        frame: framePtr,
        update_controller1: updateController1,
        apu_fill_buffer: fillAPUBuffer,
        apu_step_frame: stepAPUFrame,
        NonMaskableInterrupt: nmi,
        load_state: loadState,
        save_state: saveState,
    } = instance.exports;

    const chrRomPtr = instance.exports.chr_rom.value;
    const audioBufferSizePtr = instance.exports.audio_buffer_size.value;
    const audioBufferSize = readUint16(uint8View, audioBufferSizePtr);
    const webAudioBufferPtr = instance.exports.web_audio_buffer.value;
    const samples = new Uint8Array(uint8View.buffer, webAudioBufferPtr, audioBufferSize);
    const lastSaveStatePtr = instance.exports.last_save_state.value;
    const lastSaveState = new Uint8Array(uint8View.buffer, lastSaveStatePtr, SAVE_STATE_SIZE);
    let saveStateReady = false;

    const chrRom = await getCHRROM();
    uint8View.set(chrRom, chrRomPtr);

    const { canvas, context: ctx, updateSize } = createCanvas();
    document.body.appendChild(canvas);

    const imageData = ctx.createImageData(SCREEN_WIDTH, SCREEN_HEIGHT);

    const renderFrame = () => {
        for (let i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            imageData.data[i * 4] = uint8View[framePtr + i * 3];
            imageData.data[i * 4 + 1] = uint8View[framePtr + i * 3 + 1];
            imageData.data[i * 4 + 2] = uint8View[framePtr + i * 3 + 2];
            imageData.data[i * 4 + 3] = 255;
        }

        ctx.putImageData(imageData, 0, 0);
    };

    window.addEventListener('resize', updateSize);

    // silence the audio when the tab is not active
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            audioContext.suspend();
        } else {
            audioContext.resume();
        }
    });

    const audioContext = new AudioContext({
        latencyHint: 'interactive',
    });

    let audioInitialized = false;
    let audioWorklet = null;
    let lastAudioTime = 0;

    const initAudio = async () => {
        await audioContext.audioWorklet.addModule('audio-worklet.js');
        audioWorklet = new AudioWorkletNode(audioContext, 'apu-audio-processor', {
            outputChannelCount: [1],
            processorOptions: {
                sampleRate: audioContext.sampleRate
            }
        });

        audioWorklet.port.onmessage = (event) => {
            if (event.data.type === 'bufferLow') {
                processAudio();
            }
        };

        audioWorklet.connect(audioContext.destination);
    };

    const normalizedSamples = new Float32Array(AUDIO_CHUNK_SIZE);

    const processAudio = () => {
        if (!audioInitialized) return;

        // Calculate how many samples we need based on time elapsed
        const currentTime = audioContext.currentTime;
        const timeDelta = currentTime - lastAudioTime;
        const samplesNeeded = Math.floor(timeDelta * audioContext.sampleRate);
        const chunkSize = Math.min(AUDIO_CHUNK_SIZE, samplesNeeded);

        fillAPUBuffer(samples.byteOffset, chunkSize);

        for (let i = 0; i < chunkSize; i++) {
            normalizedSamples[i] = (samples[i] / 255) * 2 - 1;
        }

        audioWorklet.port.postMessage({
            type: 'samples',
            samples: normalizedSamples,
            chunkSize,
        });

        lastAudioTime = currentTime;
    };

    let shouldSaveState = false;
    let shouldLoadState = false;

    const joypad1 = createController(
        async () => {
            if (!audioInitialized) {
                await audioContext.resume();
                await initAudio();
                lastAudioTime = audioContext.currentTime;
                audioInitialized = true;
            }
        },
        {
            save() {
                shouldSaveState = true;
            },
            load() {
                shouldLoadState = true;
            },
        },
    );

    initCPU();
    initPPU(chrRomPtr);
    initAPU(audioContext.sampleRate);

    start();

    let lastFrameTime = performance.now();
    let currentFrameTime = lastFrameTime;
    let lastUpdateTime = lastFrameTime;
    const targetFrameTime = 1000 / 60;

    const update = () => {
        const currentTime = performance.now();
        const delta = currentTime - lastUpdateTime;
        currentFrameTime += delta;
        lastUpdateTime = currentTime;

        if (currentFrameTime >= targetFrameTime) {
            updateController1(joypad1.getState());
            nmi();
            stepAPUFrame();

            if (audioInitialized) {
                processAudio();
            }

            renderPPU();
            renderFrame();
            lastFrameTime = currentTime;
            currentFrameTime = currentFrameTime - targetFrameTime;

            if (shouldSaveState) {
                saveState(lastSaveState.byteOffset);
                localStorage.setItem(SAVE_STATE_LOCAL_STORAGE_KEY, JSON.stringify(Array.from(lastSaveState)));
                shouldSaveState = false;
                saveStateReady = true;
            } else if (shouldLoadState) {
                if (saveStateReady) {
                    loadState(lastSaveState.byteOffset);
                } else {
                    const saveStateData = localStorage.getItem(SAVE_STATE_LOCAL_STORAGE_KEY);

                    if (saveStateData) {
                        const saveStateArray = JSON.parse(saveStateData);
                        lastSaveState.set(saveStateArray);
                        saveStateReady = true;
                    }
                }

                shouldLoadState = false;
            }
        }

        requestAnimationFrame(update);
    };

    update();
}

main();
