
const SCREEN_WIDTH = 256;
const SCREEN_HEIGHT = 240;

function createCanvas() {
    const canvas = document.createElement('canvas');

    if (canvas === null) {
        throw new Error('Failed to create canvas');
    }

    canvas.width = SCREEN_WIDTH;
    canvas.height = 240;

    const scalingFactor = 2;
    canvas.style.width = `${SCREEN_WIDTH * scalingFactor}px`;
    canvas.style.height = `${SCREEN_HEIGHT * scalingFactor}px`;
    canvas.style.imageRendering = 'pixelated';

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
        select: 'Space',
    };
    
    window.addEventListener('keydown', (event) => {
        switch (event.key) {
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
                break;
            case keyMap.select:
                state |= CONTROLLER_SELECT;
                break;
        }
    });

    window.addEventListener('keyup', (event) => {
        switch (event.key) {
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

async function main() {
    const memory = new WebAssembly.Memory({ initial: 256 });
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

    window.nes = instance.exports;

    const rom = new Uint8Array(await (await fetch('Super Mario Bros.nes')).arrayBuffer());
    const chrRom = rom.slice(-8192); // last 8KB of ROM
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
