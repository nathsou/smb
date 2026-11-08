// The hand-written routines the game calls (routines.txt) and the generated
// routines they call, with the signatures --locals gives them. Generated
// with the game (`make native-interface`), do not edit.
//
// With MACHINE_CALLS (a game built without --locals, and the split
// WebAssembly build's), they are named for the adapters in machine_calls.c,
// which pass their values through the machine's registers.

#ifndef SMB_ROUTINES_H
#define SMB_ROUTINES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef MACHINE_CALLS
#define draw_title_screen locals_draw_title_screen
#define clear_screens locals_clear_screens
#define handle_pause locals_handle_pause
#define shuffle_sprite_slots locals_shuffle_sprite_slots
#define run_game_mode locals_run_game_mode
#define hide_all_sprites locals_hide_all_sprites
#define hide_sprites_but_zero locals_hide_sprites_but_zero
#define select_vram_list_b locals_select_vram_list_b
#define next_game_mode_task_b locals_next_game_mode_task_b
#define read_joypads locals_read_joypads
#define update_top_score locals_update_top_score
#define update_sound locals_update_sound
#endif

// hand-written
void reset(void); // with the machine's registers
void frame_interrupt(void); // with the machine's registers
void draw_title_screen(void);
uint8_t clear_screens(void); // returns y

// generated
void handle_pause(void);
uint8_t shuffle_sprite_slots(void); // returns x
void run_game_mode(uint8_t x);
uint8_t hide_all_sprites(void); // returns y
void hide_sprites_but_zero(void);
void select_vram_list_b(uint8_t a);
void next_game_mode_task_b(void);
void read_joypads(void);
void update_top_score(void);
void clear_memory(void); // with the machine's registers
void update_sound(uint8_t x);

#endif
