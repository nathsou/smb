// Synthesises the game's sound on the audio thread.
//
// The synthesizer (codegen/lib/apu.c, built to apu.wasm) runs here, in its
// own WebAssembly instance. The page posts each frame's APU register writes;
// the synthesizer applies them at the next frame boundary in sample time and
// renders whatever the audio device asks for, so the sound never waits on the
// main thread's timing.
//
// processorOptions: { wasm: ArrayBuffer of apu.wasm, registers: the last
// value written to each APU register ($4000-$4017), to start from }
// messages: { type: 'frame', events: Uint8Array of (register, value) pairs }

class APUProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const { wasm, registers } = options.processorOptions;
        let memory = null;
        const bytes = () => new Uint8Array(memory.buffer);
        const instance = new WebAssembly.Instance(new WebAssembly.Module(wasm), {
            env: {
                memcpy: (dest, src, num) => (bytes().copyWithin(dest, src, src + num), dest),
            },
        });
        this.apu = instance.exports;
        memory = this.apu.memory;
        this.memory = memory;
        this.apu.apu_init(sampleRate);

        // Start from the registers as the game left them, channel enables
        // last, applied with the first frame (not as a frame of their own,
        // which would delay every later frame by one).
        const initial = [];
        registers.forEach((value, register) => {
            if (value >= 0 && register !== 0x15) initial.push(register, value);
        });
        if (registers[0x15] >= 0) initial.push(0x15, registers[0x15]);
        this.initial = new Uint8Array(initial);

        this.port.onmessage = ({ data }) => {
            if (data.type !== 'frame') return;
            if (this.initial !== null) {
                const first = new Uint8Array(this.initial.length + data.events.length);
                first.set(this.initial);
                first.set(data.events, this.initial.length);
                this.initial = null;
                this.push(first);
            } else {
                this.push(data.events);
            }
        };
    }

    push(events) {
        new Uint8Array(this.memory.buffer).set(events, this.apu.apu_input.value);
        this.apu.apu_push_frame(this.apu.apu_input.value, events.length / 2);
    }

    process(_inputs, outputs) {
        const channel = outputs[0][0];
        const address = this.apu.apu_output.value;
        this.apu.apu_render(address, channel.length);
        const samples = new Uint8Array(this.memory.buffer, address, channel.length);
        for (let i = 0; i < channel.length; i++) channel[i] = (samples[i] / 255) * 2 - 1;
        return true;
    }
}

registerProcessor('apu-processor', APUProcessor);
