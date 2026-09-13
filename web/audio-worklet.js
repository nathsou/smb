
class APUAudioProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        // Create a circular buffer to prevent underruns
        this.bufferSize = 8192;
        this.bufferMask = this.bufferSize - 1;
        this.buffer = new Float32Array(this.bufferSize);
        this.writeIndex = 0;
        this.readIndex = 0;
        this.lastSample = 0;
        this.bufferLowThreshold = this.bufferSize / 4;

        this.port.onmessage = (event) => {
            if (event.data.type === 'samples') {
                const newSamples = event.data.samples;
                const chunkSize = event.data.chunkSize;

                for (let i = 0; i < chunkSize; i++) {
                    const nextWriteIndex = (this.writeIndex + 1) & this.bufferMask;
                    // Preserve queued samples if the producer gets ahead. An
                    // overwrite would splice unrelated waveform sections and
                    // produce a click.
                    if (nextWriteIndex === this.readIndex) break;
                    this.buffer[this.writeIndex] = newSamples[i];
                    this.writeIndex = nextWriteIndex;
                }
            }
        };
    }

    getBufferFill() {
        if (this.writeIndex >= this.readIndex) {
            return this.writeIndex - this.readIndex;
        }

        return this.bufferSize - (this.readIndex - this.writeIndex);
    }

    process(_inputs, outputs) {
        const channel = outputs[0][0];

        for (let i = 0; i < channel.length; i++) {
            if (this.readIndex !== this.writeIndex) {
                this.lastSample = this.buffer[this.readIndex];
                this.readIndex = (this.readIndex + 1) & this.bufferMask;
            }
            channel[i] = this.lastSample;
        }

        // Notify main thread if the buffer is running low
        if (this.getBufferFill() < this.bufferLowThreshold) {
            this.port.postMessage({ type: 'bufferLow' });
        }

        return true;
    }
}

registerProcessor('apu-audio-processor', APUAudioProcessor);
