const SCREEN_WIDTH = 256;
const SCREEN_HEIGHT = 240;

function createCanvas() {
    const canvas = document.createElement('canvas');

    if (canvas === null) {
        throw new Error('Failed to create canvas');
    }

    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;

    const scalingFactor = Math.min(
        Math.floor(window.innerWidth / SCREEN_WIDTH),
        Math.floor(window.innerHeight / SCREEN_HEIGHT),
    );

    canvas.style.width = `${SCREEN_WIDTH * scalingFactor}px`;
    canvas.style.height = `${SCREEN_HEIGHT * scalingFactor}px`;

    const context = canvas.getContext('2d');

    if (context === null) {
        throw new Error('Failed to get 2d context');
    }

    return { canvas, context };
}

function createController() {
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
    };

    window.addEventListener('keydown', (event) => {
        const key = event.key.toLowerCase();
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
            case keyMap.start.toLowerCase():
                state |= CONTROLLER_START;
                break;
            case keyMap.select.toLowerCase():
                state |= CONTROLLER_SELECT;
                break;
        }
    });

    window.addEventListener('keyup', (event) => {
        const key = event.key.toLowerCase();
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
            case keyMap.start.toLowerCase():
                state &= ~CONTROLLER_START;
                break;
            case keyMap.select.toLowerCase():
                state &= ~CONTROLLER_SELECT;
                break;
        }
    });

    return {
        getState: () => state,
    };
}

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
    // Check if CHR ROM is in localStorage
    const chrRomData = localStorage.getItem(CHR_ROM_LOCAL_STORAGE_KEY);
    
    if (chrRomData) {
        // Decode from base64 and convert to Uint8Array
        const byteString = atob(chrRomData);
        const array = new Uint8Array(byteString.length);
        for (let i = 0; i < byteString.length; i++) {
            array[i] = byteString.charCodeAt(i);
        }
        return array;
    } else {
        // Create upload button and message
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
                        const chrRom = rom.slice(-8192); // last 8KB of ROM
                        // Convert Uint8Array to base64 string
                        let binary = '';
                        const len = chrRom.byteLength;
                        for (let i = 0; i < len; i++) {
                            binary += String.fromCharCode(chrRom[i]);
                        }
                        const base64String = btoa(binary);
                        localStorage.setItem(CHR_ROM_LOCAL_STORAGE_KEY, base64String);
                        // Remove upload UI
                        document.body.removeChild(container);
                        resolve(chrRom);
                    };
                    reader.onerror = () => {
                        reject(new Error('Failed to read file'));
                    };
                    reader.readAsArrayBuffer(file);
                };

                // Trigger the file input dialog
                input.click();
            });
        });
    }
}

async function main() {
    const memory = new WebAssembly.Memory({ initial: 6, maximum: 6 });
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
        init_cpu: initCPU,
        init_ppu: initPPU,
        smb,
        render_ppu: renderPPU,
        frame: framePtr,
        chr_rom: chrRomPtr,
        update_controller1: updateController1,
    } = instance.exports;

    const chrRom = await getCHRROM();
    uint8View.set(chrRom, chrRomPtr);

    const { canvas, context: ctx } = createCanvas();
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

    const joypad1 = createController();

    initCPU();
    initPPU(chrRomPtr);

    smb(0);

    const update = () => {
        updateController1(joypad1.getState());
        smb(1);
        renderPPU();
        renderFrame();
        requestAnimationFrame(update);
    };

    update();
}

main();
