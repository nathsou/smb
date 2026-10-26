#include "state.h"
#ifdef MAPPER
#include "mapper.h"
#include "ppu_bus.h"
#endif

uint8_t last_save_state[SAVE_STATE_SIZE];
const uint32_t save_state_size = SAVE_STATE_SIZE;

#ifdef MAPPER
// The bytes after the display state, in order (see state.h).
static void cartridge_state(uint8_t *state, bool save) {
    uint8_t *p = state + SAVE_STATE_DISPLAY_SIZE;
    #define BYTES(object, size) do { if (save) memcpy(p, (object), (size)); else memcpy((object), p, (size)); p += (size); } while (0)
    #define BYTE(value, type) do { if (save) *p = (uint8_t)(value); else (value) = (type)*p; p++; } while (0)
    // CHR RAM (CHR ROM never changes: its place stays unused)
    if (chr_ram) BYTES(chr_rom, CHR_ROM_SIZE); else p += CHR_ROM_SIZE;
    BYTES(wram, WRAM_SIZE);
    BYTE(mmc1.shift, uint8_t); BYTE(mmc1.count, uint8_t); BYTE(mmc1.control, uint8_t);
    BYTE(mmc1.chr0, uint8_t); BYTE(mmc1.chr1, uint8_t); BYTE(mmc1.prg, uint8_t);
    BYTE(mmc3.select, uint8_t);
    for (int i = 0; i < 8; i++) BYTE(mmc3.banks[i], uint8_t);
    BYTE(mmc3.mirroring, uint8_t); BYTE(mmc3.irq_latch, uint8_t); BYTE(mmc3.irq_counter, uint8_t);
    BYTE(mmc3.irq_reload, uint8_t); BYTE(mmc3.irq_enabled, uint8_t); BYTE(mmc3.irq_pending, uint8_t);
    BYTE(mmc3.prg_mode, uint8_t);
    BYTE(video.ctrl, uint8_t); BYTE(video.mask, uint8_t); BYTE(video.scroll_x, uint8_t);
    BYTE(video.scroll_y, uint8_t); BYTE(video.status_bar, bool); BYTE(video.mirroring, uint8_t);
    uint8_t vram_low = (uint8_t)ppu_vram_addr, vram_high = (uint8_t)(ppu_vram_addr >> 8);
    BYTE(oam_addr, uint8_t); BYTE(ppu_write_toggle, uint8_t); BYTE(vram_low, uint8_t);
    BYTE(vram_high, uint8_t); BYTE(ppu_read_buffer, uint8_t); BYTE(ppu_status_clear, bool);
    // the scroll the IRQ handler's splits start from
    uint8_t temp_low = (uint8_t)ppu_temp_addr, temp_high = (uint8_t)(ppu_temp_addr >> 8);
    BYTE(temp_low, uint8_t); BYTE(temp_high, uint8_t); BYTE(ppu_fine_x, uint8_t);
    BYTE(a, uint8_t); BYTE(x, uint8_t); BYTE(y, uint8_t); BYTE(sp, uint8_t);
    BYTE(carry_flag, bool); BYTE(zero_flag, bool); BYTE(neg_flag, bool);
    BYTE(overflow_flag, bool); BYTE(interrupt_disabled, bool); BYTE(decimal_flag, bool);
    uint8_t return_low = (uint8_t)cpu_return, return_high = (uint8_t)(cpu_return >> 8);
    BYTE(return_low, uint8_t); BYTE(return_high, uint8_t);
    BYTE(cpu_resume_depth, uint8_t);
    BYTES(cpu_resume_stack, CPU_RESUME_STACK);
    // the coroutines: the one running, and the others' resume points
    BYTE(cpu_thread, uint8_t);
    for (int k = 0; k < CPU_THREADS; k++) {
        CpuThread *t = &cpu_threads[k];
        uint8_t awaited_low = (uint8_t)t->awaited, awaited_high = (uint8_t)(t->awaited >> 8);
        BYTE(t->parked, uint8_t); BYTE(t->depth, uint8_t);
        BYTE(awaited_low, uint8_t); BYTE(awaited_high, uint8_t);
        BYTES(t->points, CPU_RESUME_STACK);
        if (!save) t->awaited = (uint16_t)(awaited_low | (awaited_high << 8));
    }
    if (!save) {
        ppu_vram_addr = (uint16_t)(vram_low | (vram_high << 8));
        ppu_temp_addr = (uint16_t)(temp_low | (temp_high << 8));
        cpu_return = (uint16_t)(return_low | (return_high << 8));
        mapper_apply();
        video.chr_version++; // the renderers decode the tiles again
    }
    #undef BYTES
    #undef BYTE
}
#endif

void save_state(uint8_t *dest) {
    // TODO: investigate why memcpy has strange behavior in the web target
    for (size_t i = 0; i < RAM_SIZE; i++) {
        dest[i] = ram[i];
    }

    for (size_t i = 0; i < NAMETABLE_SIZE; i++) {
        dest[SAVE_STATE_NAMETABLE_OFFSET + i] = video.nametable[i];
    }

    for (size_t i = 0; i < PALETTE_SIZE; i++) {
        dest[SAVE_STATE_PALETTE_OFFSET + i] = video.palette.u8[i];
    }

    for (size_t i = 0; i < OAM_SIZE; i++) {
        dest[SAVE_STATE_OAM_OFFSET + i] = video.oam[i];
    }
#ifdef MAPPER
    cartridge_state(dest, true);
#endif
}

void load_state(uint8_t *state) {
    memcpy(ram, state, RAM_SIZE);
    memcpy(video.nametable, state + SAVE_STATE_NAMETABLE_OFFSET, NAMETABLE_SIZE);
    memcpy(video.palette.u8, state + SAVE_STATE_PALETTE_OFFSET, PALETTE_SIZE);
    memcpy(video.oam, state + SAVE_STATE_OAM_OFFSET, OAM_SIZE);
#ifdef MAPPER
    cartridge_state(state, false);
#endif
}
