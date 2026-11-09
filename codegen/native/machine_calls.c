// Adapters between the hand-written routines, which have locals, and a game
// that keeps the machine's registers (built without --locals, and the split
// WebAssembly build's). Generated with the game (`make native-interface`),
// do not edit; built with MACHINE_CALLS.

#include "../lib/cpu.h"

// the game's, with the machine's registers
void handle_pause(void);
void shuffle_sprite_slots(void);
void run_game_mode(void);
void hide_all_sprites(void);
void hide_sprites_but_zero(void);
void select_vram_list_b(void);
void next_game_mode_task_b(void);
void read_joypads(void);
void update_top_score(void);
void clear_memory(void);
void update_sound(void);

// the hand-written ones, with locals
void locals_draw_title_screen(void);
uint8_t locals_clear_screens(void);

// a hand-written routine as the game calls it
void draw_title_screen(void) {
  locals_draw_title_screen();
}
void clear_screens(void) {
  y = locals_clear_screens();
}

// a generated routine as the hand-written ones call it
void locals_handle_pause(void) {
  handle_pause();
}
uint8_t locals_shuffle_sprite_slots(void) {
  shuffle_sprite_slots();
  return x;
}
void locals_run_game_mode(uint8_t v_x) {
  x = v_x;
  run_game_mode();
}
uint8_t locals_hide_all_sprites(void) {
  hide_all_sprites();
  return y;
}
void locals_hide_sprites_but_zero(void) {
  hide_sprites_but_zero();
}
void locals_select_vram_list_b(uint8_t v_a) {
  a = v_a;
  select_vram_list_b();
}
void locals_next_game_mode_task_b(void) {
  next_game_mode_task_b();
}
void locals_read_joypads(void) {
  read_joypads();
}
void locals_update_top_score(void) {
  update_top_score();
}
void locals_update_sound(uint8_t v_x) {
  x = v_x;
  update_sound();
}
