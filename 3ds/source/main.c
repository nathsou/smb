#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include "../../codegen/lib/cpu.h"
#include "../../codegen/lib/ppu.h"
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

void update_top_screen(u8* buffer) {
    // rotate the frame buffer 90 degrees
    for (size_t y = 0; y < NES_SCREEN_HEIGHT; y++) {
        for (size_t x = 0; x < NES_SCREEN_WIDTH; x++) {
            size_t src_index = (y * NES_SCREEN_WIDTH + x) * 3;
            size_t dst_index = ((LEFT_X_OFFSET_TOP + x) * NES_SCREEN_HEIGHT + (NES_SCREEN_HEIGHT - y - 1)) * 3;

            // RGB -> BGR
            buffer[dst_index] = frame[src_index + 2];
            buffer[dst_index + 1] = frame[src_index + 1];
            buffer[dst_index + 2] = frame[src_index];
        }
    }
}

inline bool is_key_active(u32 keys, u32 kDown, u32 kHeld) {
	return (keys & kDown) || (keys & kHeld);
}

bool handle_inputs(void) {
	u32 kDown = hidKeysDown();
	u32 kHeld = hidKeysHeld();
    u8 state = 0;

	if (is_key_active(KEY_DLEFT, kDown, kHeld) || is_key_active(KEY_CPAD_LEFT, kDown, kHeld)) state |= CONTROLLER_LEFT;
	if (is_key_active(KEY_DRIGHT, kDown, kHeld) || is_key_active(KEY_CPAD_RIGHT, kDown, kHeld)) state |= CONTROLLER_RIGHT;
	if (is_key_active(KEY_DDOWN, kDown, kHeld) || is_key_active(KEY_CPAD_DOWN, kDown, kHeld)) state |= CONTROLLER_DOWN;
	if (is_key_active(KEY_DUP, kDown, kHeld) || is_key_active(KEY_CPAD_UP, kDown, kHeld)) state |= CONTROLLER_UP;
	if (is_key_active(KEY_START, kDown, kHeld)) state |= CONTROLLER_START;
	if (is_key_active(KEY_SELECT, kDown, kHeld)) state |= CONTROLLER_SELECT;
	if (is_key_active(KEY_B, kDown, kHeld) || is_key_active(KEY_X, kDown, kHeld)) state |= CONTROLLER_B;
	if (is_key_active(KEY_A, kDown, kHeld) || is_key_active(KEY_Y, kDown, kHeld)) state |= CONTROLLER_A;
	if (is_key_active(KEY_L, kDown, kHeld) && is_key_active(KEY_R, kDown, kHeld)) return true;

    update_controller1(state);
    return false;
}

int main(int argc, char* argv[]) {
    gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);
    consoleInit(GFX_BOTTOM, NULL);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSwapBuffers();

    cpu_init();
    ppu_init(______smb_nes);

    Start();

    while (aptMainLoop()) {
        hidScanInput();

        // Measure NonMaskableInterrupt duration
        u64 start = svcGetSystemTick();
        NonMaskableInterrupt();
        u64 end = svcGetSystemTick();
        u64 nmi_duration = (end - start) * 1000 / SYSCLOCK_ARM11;

        // Measure ppu_render duration
        start = svcGetSystemTick();
        u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        ppu_render();
        end = svcGetSystemTick();
        u64 ppu_duration = (end - start) * 1000 / SYSCLOCK_ARM11;

        // Measure update_top_screen duration
        start = svcGetSystemTick();
        update_top_screen(fb);
        end = svcGetSystemTick();
        u64 screen_update_duration = (end - start) * 1000 / SYSCLOCK_ARM11;

        // Update console
        printf("\x1b[2;1H"); // Move cursor to row 2, column 1
		printf("Press L + R to exit\n");
        printf("NonMaskableInterrupt: %llu ms   \n", nmi_duration);
        printf("ppu_render:           %llu ms   \n", ppu_duration);
        printf("update_top_screen:    %llu ms   \n", screen_update_duration);

        // Handle inputs and exit condition
        if (handle_inputs()) {
            break;
        }

        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}

