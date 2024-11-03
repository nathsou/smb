
class APUAudioProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        // Create a circular buffer to prevent underruns
        this.bufferSize = 8192;
        this.bufferMask = this.bufferSize - 1;
        this.buffer = new Float32Array(this.bufferSize);
        this.writeIndex = 0;
        this.readIndex = 0;
        this.bufferFilled = false;
        this.bufferLowThreshold = this.bufferSize / 4;

        this.port.onmessage = (event) => {
            if (event.data.type === 'samples') {
                const newSamples = event.data.samples;
                const chunkSize = event.data.chunkSize;

                for (let i = 0; i < chunkSize; i++) {
                    this.buffer[this.writeIndex] = newSamples[i];
                    this.writeIndex = (this.writeIndex + 1) & this.bufferMask;
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
            channel[i] = this.buffer[this.readIndex];
            this.readIndex = (this.readIndex + 1) & this.bufferMask;
        }

        // Notify main thread if the buffer is running low
        if (this.getBufferFill() < this.bufferLowThreshold) {
            this.port.postMessage({ type: 'bufferLow' });
        }

        return true;
    }
}

registerProcessor('apu-audio-processor', APUAudioProcessor);
