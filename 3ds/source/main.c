#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include "../../codegen/lib/cpu.h"
#include "../../codegen/lib/ppu.h"
#include "../../codegen/lib/apu.h"
#include "../../codegen/lib/code.h"
#include "../../codegen/lib/common.h"
#include "chr_rom.h"

#define NES_SCREEN_WIDTH 256 // px
#define NES_SCREEN_HEIGHT 240 // px
#define TOP_SCREEN_WIDTH 400 // px
#define TOP_SCREEN_HEIGHT 240 // px
#define BOTTOM_SCREEN_WIDTH 320 // px
#define BOTTOM_SCREEN_HEIGHT 240 // px
#define TOP_SCREEN_BUFFER_SIZE (TOP_SCREEN_WIDTH * TOP_SCREEN_HEIGHT * 3)
#define LEFT_X_OFFSET_TOP ((TOP_SCREEN_WIDTH - NES_SCREEN_WIDTH) / 2)

#define CONTROLLER_RIGHT 0b10000000
#define CONTROLLER_LEFT 0b01000000
#define CONTROLLER_DOWN 0b00100000
#define CONTROLLER_UP 0b00010000
#define CONTROLLER_START 0b00001000
#define CONTROLLER_SELECT 0b00000100
#define CONTROLLER_B 0b00000010
#define CONTROLLER_A 0b00000001

#define AUDIO_SAMPLE_RATE 32728
#define AUDIO_FRAMES_PER_SEC 60
#define SAMPLES_PER_BUF (AUDIO_SAMPLE_RATE / AUDIO_FRAMES_PER_SEC)
#define AUDIO_CHANNELS 2
#define BYTES_PER_SAMPLE_PER_CHANNEL 2
#define AUDIO_BUFFER_SIZE_3DS (SAMPLES_PER_BUF * AUDIO_CHANNELS * BYTES_PER_SAMPLE_PER_CHANNEL)

static ndspWaveBuf waveBuf[2];
static u8* audioBuffer = NULL;
static bool fillBlock = false;
static u8* apu_temp_buffer = NULL;
static bool audio_muted = true;

void update_top_screen(u8* buffer) {
    // rotate the frame buffer 90 degrees and handle BGR format
    for (size_t y = 0; y < NES_SCREEN_HEIGHT; y++) {
        for (size_t x = 0; x < NES_SCREEN_WIDTH; x++) {
            size_t src_index = (y * NES_SCREEN_WIDTH + x) * 3;
            // Calculate destination index for 90-degree rotation and centering
            size_t dst_index = ((LEFT_X_OFFSET_TOP + x) * TOP_SCREEN_HEIGHT + (NES_SCREEN_HEIGHT - 1 - y)) * 3;

            // RGB -> BGR conversion
            buffer[dst_index] = frame[src_index + 2];
            buffer[dst_index + 1] = frame[src_index + 1];
            buffer[dst_index + 2] = frame[src_index];
        }
    }
}

inline bool is_key_active(u32 keys, u32 kDown, u32 kHeld) {
	return (keys & kDown) || (keys & kHeld);
}

// Returns true if exit combination is pressed
bool handle_inputs(void) {
	hidScanInput();
	u32 kDown = hidKeysDown();
	u32 kHeld = hidKeysHeld();
    u8 state = 0;

	if (is_key_active(KEY_DLEFT | KEY_CPAD_LEFT, kDown, kHeld)) state |= CONTROLLER_LEFT;
	if (is_key_active(KEY_DRIGHT | KEY_CPAD_RIGHT, kDown, kHeld)) state |= CONTROLLER_RIGHT;
	if (is_key_active(KEY_DDOWN | KEY_CPAD_DOWN, kDown, kHeld)) state |= CONTROLLER_DOWN;
	if (is_key_active(KEY_DUP | KEY_CPAD_UP, kDown, kHeld)) state |= CONTROLLER_UP;
	if (is_key_active(KEY_START, kDown, kHeld)) state |= CONTROLLER_START;
	if (is_key_active(KEY_SELECT, kDown, kHeld)) state |= CONTROLLER_SELECT;
	if (is_key_active(KEY_B | KEY_X, kDown, kHeld)) state |= CONTROLLER_B;
	if (is_key_active(KEY_A | KEY_Y, kDown, kHeld)) state |= CONTROLLER_A;

    // Check for mute toggle press (SELECT button)
    if (kDown & KEY_SELECT) {
        audio_muted = !audio_muted;
        if (audio_muted) {
            // Clear existing buffers when muting
            ndspChnWaveBufClear(0);
            // Ensure buffers are marked DONE so they can be refilled if unmuted
             waveBuf[0].status = NDSP_WBUF_DONE;
             waveBuf[1].status = NDSP_WBUF_DONE;
             fillBlock = 0; // Reset fill block index
        }
    }

    // Exit condition: Press L + R together
	if ((kHeld & KEY_L) && (kHeld & KEY_R)) return true;

    update_controller1(state);
    return false;
}

// Fills an NDSP audio buffer with data from the APU
void fill_audio_buffer(ndspWaveBuf* buf) {
    if (!apu_temp_buffer || !buf || !buf->data_pcm16) return; // Safety checks

    apu_fill_buffer(apu_temp_buffer, buf->nsamples);

    s16* dest = (s16*)buf->data_pcm16;

    for (size_t i = 0; i < buf->nsamples; i++) {
        s16 sample_16bit = (((s16)apu_temp_buffer[i]) - 128) * 256;
        dest[i * 2] = sample_16bit; // Left channel
        dest[i * 2 + 1] = sample_16bit; // Right channel
    }

    DSP_FlushDataCache(buf->data_vaddr, buf->nsamples * AUDIO_CHANNELS * BYTES_PER_SAMPLE_PER_CHANNEL);
}


int main(int argc, char* argv[]) {
    gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);
    consoleInit(GFX_BOTTOM, NULL);
    gfxSetDoubleBuffering(GFX_TOP, true);

    printf("Initializing Emulator...\n");

    cpu_init();
    ppu_init(______smb_nes);
    apu_init(AUDIO_SAMPLE_RATE);

    printf("Initializing Audio...\n");

    ndspInit();

    audioBuffer = (u8*)linearAlloc(AUDIO_BUFFER_SIZE_3DS * 2);
    if (!audioBuffer) {
        printf("Failed to allocate audio buffer!\n");
        goto cleanup_gfx;
    }
    apu_temp_buffer = (u8*)malloc(SAMPLES_PER_BUF);
     if (!apu_temp_buffer) {
        printf("Failed to allocate APU temp buffer!\n");
        goto cleanup_ndsp;
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, AUDIO_SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    memset(waveBuf, 0, sizeof(waveBuf));
    waveBuf[0].data_vaddr = audioBuffer;
    waveBuf[0].nsamples = SAMPLES_PER_BUF;
    waveBuf[0].status = NDSP_WBUF_DONE;

    waveBuf[1].data_vaddr = audioBuffer + AUDIO_BUFFER_SIZE_3DS;
    waveBuf[1].nsamples = SAMPLES_PER_BUF;
    waveBuf[1].status = NDSP_WBUF_DONE;

    // Initial fill is only needed if not starting muted
    if (!audio_muted) {
        printf("Filling initial audio buffers...\n");
        fill_audio_buffer(&waveBuf[0]);
        ndspChnWaveBufAdd(0, &waveBuf[0]);
        fill_audio_buffer(&waveBuf[1]);
        ndspChnWaveBufAdd(0, &waveBuf[1]);
        fillBlock = 0;
    } else {
         printf("Starting muted.\n");
         fillBlock = 0; // Still need to initialize fillBlock
    }

    printf("Starting Emulation...\n");

    Start();

    consoleClear();

    while (aptMainLoop()) {
        if (handle_inputs()) {
            break;
        }

        u64 start_nmi = svcGetSystemTick();
        NonMaskableInterrupt();
        u64 end_nmi = svcGetSystemTick();

        u64 start_ppu = svcGetSystemTick();
        ppu_render();
        u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        update_top_screen(fb);
        u64 end_ppu = svcGetSystemTick();

        u64 start_apu = 0, end_apu = 0;
        if (!audio_muted) {
            start_apu = svcGetSystemTick();
            apu_step_frame();

            // If a buffer is done, refill and queue it
            if (waveBuf[fillBlock].status == NDSP_WBUF_DONE) {
                fill_audio_buffer(&waveBuf[fillBlock]);
                ndspChnWaveBufAdd(0, &waveBuf[fillBlock]);
                fillBlock = !fillBlock;
            }
            end_apu = svcGetSystemTick();
        }

        printf("\x1b[1;1H"); // Move cursor top-left
        printf("Press L + R to exit    \n");
        printf("Press SELECT to %s audio\n", audio_muted ? "Unmute" : "  Mute");
        printf("----------------------\n");
        
        // Calculate times using floating point for better display formatting
        float nmi_time_ms = (float)(end_nmi - start_nmi) * 1000.0f / (float)SYSCLOCK_ARM11;
        float ppu_time_ms = (float)(end_ppu - start_ppu) * 1000.0f / (float)SYSCLOCK_ARM11;
        
        printf("NMI Frame Time:  %.2f ms\n", nmi_time_ms);
        printf("PPU+Update Time: %.2f ms\n", ppu_time_ms);
               
        if (!audio_muted) {
            float apu_time_ms = (float)(end_apu - start_apu) * 1000.0f / (float)SYSCLOCK_ARM11;
            printf("Audio Fill Time: %.2f ms\n", apu_time_ms);
        } else {
            printf("Audio Fill Time: MUTED \n");
        }
        printf("\x1b[J"); // Clear rest of screen

        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    printf("Exiting...\n");

    ndspChnWaveBufClear(0);
cleanup_ndsp:
    ndspExit();
    if (audioBuffer) linearFree(audioBuffer);
    if (apu_temp_buffer) free(apu_temp_buffer);

cleanup_gfx:
    gfxExit();
    return 0;
}
