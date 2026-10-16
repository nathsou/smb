#include "code.h"

// APU registers, indexed by channel
// object slot being processed
// A and B, as joypad bits
// which player routine runs this frame
// level layout being decoded
// enemy list being decoded
// floating score numbers
// digits added to a score
// bounding boxes: left, top, right, bottom
// metatiles of the two screens around the player
// first OAM entry of each object's sprites
// 0 small, 1 big, 2 fiery
// lives, halfway page, level, hidden 1-up, coins, world, area
// title, playing, victory, game over
// power-on and reset: clear memory, set up the display, then idle

// Inline-table dispatch proven by evaluating dispatch; its effects run here unchanged.
static uint16_t dispatch(uint16_t return_address) {
  ram[0x100 + sp] = HIGH_BYTE(return_address);
  sp--;
  ram[0x100 + sp] = LOW_BYTE(return_address);
  carry_flag = (a & 0x80) != 0;
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  ram[4] = ram[0x100 + sp];
  sp++;
  ram[5] = ram[0x100 + sp];
  y = (uint8_t)((uint8_t)(a << 1) + 1);
  ram[6] = read_byte(indirect_y_addr(4));
  y++;
  ram[7] = read_byte(indirect_y_addr(4));
  return cpu_dispatch_target = (uint16_t)(ram[6] | (ram[7] << 8));
}

// reset: native implementation, not generated.
// It may read: none.
// It must produce: none.

// frame_interrupt: native implementation, not generated.
// It may read: none.
// It must produce: none.

// start toggles pause while playing
void handle_pause(void) {
  a = ram[GAME_MODE];
  if (a != 2) {
    if (a != 1) {
      return;
    }
    if (ram[GAME_MODE_TASK] != 3) {
      return;
    }
  }
  if (ram[PAUSE_TIMER] != 0) {
    ram[PAUSE_TIMER]--;
    return;
  }
  if ((ram[JOYPAD1] & 0x10) != 0) {
    if ((ram[PAUSE_STATE] & 0x80) != 0) {
      return;
    }
    ram[PAUSE_TIMER] = 0x2b;
    a = ram[PAUSE_STATE];
    ram[PAUSE_SOUND_REQUEST] = (uint8_t)(a + 1);
    a = (a ^ 1) | 0x80;
    if (a != 0) { goto loc_81c2; }
  }
  a = ram[PAUSE_STATE] & 0x7f;

loc_81c2:
  ram[PAUSE_STATE] = a;
  return;
}

// rotate OAM slots so that dropped sprites flicker instead of vanishing
// in: none; out: x
void shuffle_sprite_slots(void) {
  ram[0] = 0x28;
  x = 0xe;
  do {
    a = ram[SPRITE_SLOTS + x];
    if (a >= ram[0]) {
      uint16_t sum = (uint16_t)(a + ram[SHUFFLE_STEPS + ram[SHUFFLE_POS]]);
      a = (uint8_t)sum;
      if (sum >= 0x100) {
        a = (uint8_t)(a + ram[0]);
      }
      ram[SPRITE_SLOTS + x] = a;
    }
    x--;
  } while ((x & 0x80) == 0);
  x = (uint8_t)(ram[SHUFFLE_POS] + 1);
  if (x == 3) {
    x = 0;
  }
  ram[SHUFFLE_POS] = x;
  x = 8;
  y = 2;
  do {
    a = ram[ENEMY_SPRITE_SLOT + 4 + y];
    ram[FIREBALL_SPRITE_SLOT + x] = a;
    a = (uint8_t)(a + 8);
    ram[FIREBALL_SPRITE_SLOT + 1 + x] = a;
    ram[MISC_SPRITE_SLOT + x] = (uint8_t)(a + 8);
    x = (uint8_t)((uint8_t)(x - 2) - 1);
    y--;
  } while ((y & 0x80) == 0);
  return;
}

// dispatch on the game mode (title, playing, victory, game over)
void run_game_mode(void) {
  a = ram[GAME_MODE];
  switch (dispatch(0x8217)) {
    case 0x8231: title_mode(); return;
    case 0xaedc: playing_mode(); return;
    case 0x838b: victory_mode(); return;
    case 0x9218: game_over_mode(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: none; out: y
void hide_all_sprites(void) {
  y = 0;
  hide_sprites_from(); return;
}

void hide_sprites_but_zero(void) {
  y = 4;
  hide_sprites_from(); return;
}

// in: y; out: y
void hide_sprites_from(void) {
  do {
    ram[SPRITE_Y + y] = 0xf8;
    y = (uint8_t)((uint8_t)(y + 3) + 1);
  } while (y != 0);
  return;
}

void title_mode(void) {
  a = ram[GAME_MODE_TASK];
  switch (dispatch(0x8236)) {
    case 0x8fcf: start_game(); return;
    case 0x8567: run_screen_task(); return;
    case 0x9061: setup_new_game(); return;
    case 0x8245: title_menu(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void title_menu(void) {
  y = 0;
  a = ram[JOYPAD1] | ram[JOYPAD2];
  if (a == 0x10) { goto loc_82d8; }
  if (a == 0x90) { goto loc_82d8; }
  if (a != 0x20) {
    if (ram[DEMO_TIMER] == 0) {
      ram[SELECT_TIMER] = a;
      play_demo();
      if (carry_flag) { goto loc_82c9; }
      goto loc_82c0;
    }
    if (ram[WORLD_SELECT_UNLOCKED] == 0) { goto loc_82bb; }
    if (a != 0x40) { goto loc_82bb; }
    y = 1;
  }
  if (ram[DEMO_TIMER] == 0) { goto loc_82c9; }
  ram[DEMO_TIMER] = 0x18;
  if (ram[SELECT_TIMER] == 0) {
    ram[SELECT_TIMER] = 0x10;
    if (y != 1) {
      ram[PLAYER_COUNT] ^= 1;
      draw_player_select_icon();
    } else {
      a = (uint8_t)(ram[WORLD_SELECT] + 1) & 7;
      ram[WORLD_SELECT] = a;
      start_continue();
      do {
        ram[VRAM_LIST1_LENGTH + x] = ROM(WORLD_SELECT_TEMPLATE + x);
        x++;
      } while (((x - 6) & 0x80) != 0);
      ram[VRAM_LIST1 + 3] = (uint8_t)(ram[WORLD_NUMBER] + 1);
    }
  }

loc_82bb:
  ram[JOYPAD1] = 0;

loc_82c0:
  run_game_frame();
  if (ram[PLAYER_TASK] != 6) {
    return;
  }

loc_82c9:
  ram[GAME_MODE] = 0;
  ram[GAME_MODE_TASK] = 0;
  ram[STATUS_BAR_SPLIT] = 0;
  ram[SCREEN_OFF]++;
  return;

loc_82d8:
  if (ram[DEMO_TIMER] == 0) { goto loc_82c9; }
  if ((a & 0x80) != 0) {
    a = ram[CONTINUE_WORLD];
    start_continue();
  }
  load_area_pointer();
  ram[HIDDEN_1UP]++;
  ram[OTHER_HIDDEN_1UP]++;
  ram[RELOAD_TIME_LIMIT]++;
  ram[GAME_MODE]++;
  ram[HARD_MODE] = ram[WORLD_SELECT_UNLOCKED];
  ram[GAME_MODE_TASK] = 0;
  ram[DEMO_TIMER] = 0;
  x = 0x17;
  do {
    ram[(PLAYER_SCORE + x) & 0x7ff] = 0;
    x--;
  } while ((x & 0x80) == 0);
  return;
}

// in: a; out: x
void start_continue(void) {
  ram[WORLD_NUMBER] = a;
  ram[OTHER_WORLD_NUMBER] = a;
  x = 0;
  ram[AREA_NUMBER] = 0;
  ram[OTHER_AREA_NUMBER] = 0;
  return;
}

void draw_player_select_icon(void) {
  y = 7;
  do {
    ram[VRAM_LIST1_LENGTH + y] = ROM(PLAYER_SELECT_ICON_DATA + y);
    y--;
  } while ((y & 0x80) == 0);
  if (ram[PLAYER_COUNT] == 0) {
    return;
  }
  ram[VRAM_LIST1 + 3] = 0x24;
  ram[VRAM_LIST1 + 5] = 0xce;
  return;
}

// in: none; out: carry
void play_demo(void) {
  x = ram[DEMO_ACTION];
  if (ram[DEMO_ACTION_TIMER] == 0) {
    x++;
    ram[DEMO_ACTION]++;
    carry_flag = true;
    a = ROM(DEMO_BUTTONS + 0x14 + x);
    ram[DEMO_ACTION_TIMER] = a;
    if (a == 0) {
      return;
    }
  }
  ram[JOYPAD1] = ROM(DEMO_BUTTONS - 1 + x);
  ram[DEMO_ACTION_TIMER]--;
  carry_flag = false;
  return;
}

void victory_mode(void) {
  run_victory_task();
  if (ram[GAME_MODE_TASK] != 0) {
    x = 0;
    ram[SLOT] = 0;
    update_enemies_and_loops();
  }
  player_screen_position();
  draw_player(); return;
}

void run_victory_task(void) {
  a = ram[GAME_MODE_TASK];
  switch (dispatch(0x83a5)) {
    case 0xcfec: collapse_bridge(); return;
    case 0x83b0: start_victory(); return;
    case 0x83bd: victory_walk(); return;
    case 0x83f6: show_victory_messages(); return;
    case 0x8461: finish_world(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void start_victory(void) {
  ram[TARGET_PAGE] = (uint8_t)(ram[SCREEN_RIGHT_PAGE] + 1);
  ram[EVENT_MUSIC_REQUEST] = 8;
  next_game_mode_task_b(); return;
}

void victory_walk(void) {
  y = 0;
  ram[VICTORY_WALK_STEP] = 0;
  if (ram[PLAYER_PAGE] == ram[TARGET_PAGE]) {
    if (ram[PLAYER_X] >= 0x60) { goto loc_83d0; }
  }
  ram[VICTORY_WALK_STEP]++;
  y = 1;

loc_83d0:
  a = y;
  drive_player();
  if (ram[SCREEN_LEFT_PAGE] != ram[TARGET_PAGE]) {
    uint16_t sum = (uint16_t)(ram[SCROLL_FRACTION] + 0x80);
    ram[SCROLL_FRACTION] = (uint8_t)sum;
    y = (uint8_t)(1 + (sum >= 0x100));
    scroll_screen();
    update_scroll_column();
    ram[VICTORY_WALK_STEP]++;
  }
  if (ram[VICTORY_WALK_STEP] == 0) { next_game_mode_task(); return; }
  return;
}

void show_victory_messages(void) {
  if (ram[MESSAGE_STEP2] == 0) {
    a = ram[MESSAGE_STEP];
    if (a != 0) {
      if (a >= 9) { goto loc_8443; }
      if (ram[WORLD_NUMBER] == 7) {
        carry_flag = a >= 3;
        if (!carry_flag) { goto loc_8443; }
        a = (uint8_t)(a - 1 - !carry_flag);
      } else {
        if (a < 2) { goto loc_8443; }
      }
    }
    y = a;
    if (y == 0) {
      if (ram[CURRENT_PLAYER] == 0) { goto loc_8434; }
      y++;
      if (y != 0) { goto loc_8434; }
    }
    y++;
    if (ram[WORLD_NUMBER] != 7) {
      y--;
      carry_flag = y >= 4;
      if (carry_flag) { goto loc_8456; }
      if (y >= 3) { goto loc_8443; }
    }

loc_8434:
    if (y == 3) {
      ram[EVENT_MUSIC_REQUEST] = 4;
    }
    ram[VRAM_LIST_SELECT] = (uint8_t)(y + 0xc);
  }

loc_8443:;
  uint16_t sum2 = (uint16_t)(ram[MESSAGE_STEP2] + 4);
  ram[MESSAGE_STEP2] = (uint8_t)sum2;
  a = (uint8_t)(ram[MESSAGE_STEP] + (sum2 >= 0x100));
  ram[MESSAGE_STEP] = a;
  carry_flag = a >= 7;

loc_8456:
  if (!carry_flag) {
    return;
  }
  ram[WORLD_END_TIMER] = 6;
  next_game_mode_task(); return;
}

void next_game_mode_task(void) {
  ram[GAME_MODE_TASK]++;
  return;
}

void finish_world(void) {
  if (ram[WORLD_END_TIMER] != 0) {
    return;
  }
  if (ram[WORLD_NUMBER] < 7) {
    ram[AREA_NUMBER] = 0;
    ram[LEVEL_NUMBER] = 0;
    ram[GAME_MODE_TASK] = 0;
    ram[WORLD_NUMBER]++;
    load_area_pointer();
    ram[RELOAD_TIME_LIMIT]++;
    ram[GAME_MODE] = 1;
    return;
  }
  if (((ram[JOYPAD1] | ram[JOYPAD2]) & 0x40) == 0) {
    return;
  }
  ram[WORLD_SELECT_UNLOCKED] = 1;
  ram[LIVES] = 0xff;
  end_game();
  return;
}

// in: x; out: x
void update_score_popups(void) {
  a = ram[POPUP_KIND + x];
  if (a == 0) {
    return;
  }
  if (a >= 0xb) {
    a = 0xb;
    ram[POPUP_KIND + x] = 0xb;
  }
  y = a;
  a = ram[POPUP_TIMER + x];
  if (a == 0) {
    ram[POPUP_KIND + x] = a;
    return;
  }
  ram[POPUP_TIMER + x]--;
  if (a == 0x2b) {
    if (y == 0xb) {
      ram[LIVES]++;
      ram[PULSE2_SFX_REQUEST] = 0x40;
    }
    x = ROM(POPUP_SCORE_DIGITS + y) >> 1 >> 1 >> 1 >> 1;
    ram[SCORE_ADD + x] = ROM(POPUP_SCORE_DIGITS + y) & 0xf;
    add_score();
  }
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x12) {
    if (a != 0xd) {
      if (a != 5) {
        if (a == 0xa) { goto loc_852b; }
        if (a == 0xb) { goto loc_852b; }
        if (a < 9) {
          if (ram[(uint8_t)(ENEMY_STATE + x)] >= 2) { goto loc_852b; }
        }
      }
      y = ram[BLOCK_SPRITE_SLOT + ram[SPRITE_SLOT_CTRL]];
      x = ram[SLOT];
    }
  }

loc_852b:
  a = ram[POPUP_Y + x];
  carry_flag = a >= 0x18;
  if (carry_flag) {
    int diff = a - 1 - !carry_flag;
    carry_flag = diff >= 0;
    ram[POPUP_Y + x] = (uint8_t)diff;
  }
  int diff2 = ram[POPUP_Y + x] - 8 - !carry_flag;
  carry_flag = diff2 >= 0;
  a = (uint8_t)diff2;
  fill_two_sprites();
  a = ram[POPUP_X + x];
  ram[SPRITE_X + y] = a;
  ram[SPRITE_X + 4 + y] = (uint8_t)(a + 8);
  ram[SPRITE_ATTR + y] = 2;
  ram[SPRITE_X + 3 + y] = 2;
  x = (uint8_t)(ram[POPUP_KIND + x] << 1);
  ram[SPRITE_TILE + y] = ROM(POPUP_TILES + x);
  ram[SPRITE_X + 2 + y] = ROM(POPUP_TILES + 1 + x);
  x = ram[SLOT];
  return;
}

void run_screen_task(void) {
  a = ram[SCREEN_TASK];
  switch (dispatch(0x856c)) {
    case 0x858b: clear_screen_task(); return;
    case 0x859b: intermission_setup(); return;
    case 0x8652: draw_status_bar_top(); return;
    case 0x865a: draw_status_bar_bottom(); return;
    case 0x8693: show_time_up(); return;
    case 0x889d: wait_screen_timer(); return;
    case 0x86a8: show_intermission(); return;
    case 0x86e6: render_area_columns(); return;
    case 0x85bf: queue_area_palette(); return;
    case 0x85e3: queue_background_color(); return;
    case 0x8643: queue_mushroom_palette(); return;
    case 0x86ff: draw_title_screen(); return;
    case 0x8732: clear_lists_draw_icon(); return;
    case 0x8749: draw_top_score(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void clear_screen_task(void) {
  hide_all_sprites();
  clear_screens();
  if (ram[GAME_MODE] == 0) {
    next_screen_task(); return;
  }
  x = 3;
  select_vram_list(); return;
}

void intermission_setup(void) {
  ram[0x100 + sp] = ram[BACKGROUND_COLOR];
  sp--;
  ram[0x100 + sp] = ram[PLAYER_POWER];
  sp--;
  ram[PLAYER_POWER] = 0;
  ram[BACKGROUND_COLOR] = 2;
  queue_player_colors();
  sp++;
  ram[PLAYER_POWER] = ram[0x100 + sp];
  sp++;
  ram[BACKGROUND_COLOR] = ram[0x100 + sp];
  next_screen_task_b(); return;
}

void queue_area_palette(void) {
  x = ROM(AREA_PALETTE_LISTS + ram[AREA_KIND]);
  select_vram_list(); return;
}

// in: x; out: none
void select_vram_list(void) {
  ram[VRAM_LIST_SELECT] = x;
  next_screen_task(); return;
}

void next_screen_task(void) {
  next_screen_task_b(); return;
}

void queue_background_color(void) {
  y = ram[BACKGROUND_COLOR];
  if (y != 0) {
    ram[VRAM_LIST_SELECT] = ROM(BACKGROUND_COLOR_LISTS - 4 + y);
  }
  ram[SCREEN_TASK]++;
  queue_player_colors(); return;
}

// in: none; out: x
void queue_player_colors(void) {
  x = ram[VRAM_LIST1_LENGTH];
  y = 0;
  if (ram[CURRENT_PLAYER] != 0) {
    y = 4;
  }
  if (ram[PLAYER_POWER] == 2) {
    y = 8;
  }
  ram[0] = 3;
  do {
    ram[VRAM_LIST1 + 3 + x] = ROM(PLAYER_COLORS + y);
    y++;
    x++;
    ram[0]--;
  } while ((ram[0] & 0x80) == 0);
  x = ram[VRAM_LIST1_LENGTH];
  y = ram[BACKGROUND_COLOR];
  if (y == 0) {
    y = ram[AREA_KIND];
  }
  ram[VRAM_LIST1 + 3 + x] = ROM(BACKGROUND_COLORS + y);
  ram[VRAM_LIST1 + x] = 0x3f;
  ram[VRAM_LIST1 + 1 + x] = 0x10;
  ram[VRAM_LIST1 + 2 + x] = 4;
  ram[VRAM_LIST1 + 7 + x] = 0;
  a = (uint8_t)(x + 7);
  set_vram_list1_length(); return;
}

// in: a; out: none
void set_vram_list1_length(void) {
  ram[VRAM_LIST1_LENGTH] = a;
  return;
}

void queue_mushroom_palette(void) {
  if (ram[AREA_STYLE] != 1) {
    skip_mushroom_palette(); return;
  }
  a = 0xb;
  select_vram_list_b(); return;
}

// in: a; out: none
void select_vram_list_b(void) {
  ram[VRAM_LIST_SELECT] = a;
  skip_mushroom_palette(); return;
}

void skip_mushroom_palette(void) {
  next_screen_task_b(); return;
}

void draw_status_bar_top(void) {
  a = 0;
  queue_text();
  next_screen_task_b(); return;
}

void draw_status_bar_bottom(void) {
  queue_counters_for_player();
  x = ram[VRAM_LIST1_LENGTH];
  ram[VRAM_LIST1 + x] = 0x20;
  ram[VRAM_LIST1 + 1 + x] = 0x73;
  ram[VRAM_LIST1 + 2 + x] = 3;
  ram[VRAM_LIST1 + 3 + x] = (uint8_t)(ram[WORLD_NUMBER] + 1);
  ram[VRAM_LIST1 + 4 + x] = 0x28;
  ram[VRAM_LIST1 + 5 + x] = (uint8_t)(ram[LEVEL_NUMBER] + 1);
  ram[VRAM_LIST1 + 6 + x] = 0;
  ram[VRAM_LIST1_LENGTH] = (uint8_t)(x + 6);
  next_screen_task_b(); return;
}

void show_time_up(void) {
  if (ram[TIME_UP] != 0) {
    ram[TIME_UP] = 0;
    a = 2;
    start_intermission(); return;
  }
  ram[SCREEN_TASK]++;
  next_screen_task_b(); return;
}

void show_intermission(void) {
  a = ram[GAME_MODE];
  if (a != 0) {
    if (a != 3) {
      if (ram[ALT_ENTRANCE] != 0) { goto loc_86e0; }
      if (ram[AREA_KIND] != 3) {
        if (ram[SKIP_INTERMISSION] != 0) { goto loc_86e0; }
      }
      draw_lives_screen_player();
      a = 1;
      start_intermission(); return;
    }
    ram[SCREEN_TIMER] = 0x12;
    a = 3;
    queue_text();
    next_game_mode_task_b(); return;
  }

loc_86e0:
  ram[SCREEN_TASK] = 8;
  return;
}

// in: a; out: none
void start_intermission(void) {
  queue_text();
  restart_screen_timer();
  ram[SCREEN_OFF] = 0;
  return;
}

// build the screen column by column while the display is off
void render_area_columns(void) {
  ram[SCREEN_OFF]++;
  do {
    run_parser_task();
  } while (ram[PARSER_TASK] != 0);
  ram[COLUMN_SETS]--;
  if ((ram[COLUMN_SETS] & 0x80) != 0) {
    ram[SCREEN_TASK]++;
  }
  ram[VRAM_LIST_SELECT] = 6;
  return;
}

// draw_title_screen: native implementation, not generated.
// It may read: none.
// It must produce: none.

void clear_lists_draw_icon(void) {
  a = ram[GAME_MODE];
  if (a != 0) {
    next_game_mode_task_b(); return;
  }
  x = 0;
  do {
    ram[VRAM_LIST1_LENGTH + x] = a;
    ram[OBJ_X_FORCE + x] = a;
    x--;
  } while (x != 0);
  draw_player_select_icon();
  next_screen_task_b(); return;
}

void next_screen_task_b(void) {
  ram[SCREEN_TASK]++;
  return;
}

void draw_top_score(void) {
  a = 0xfa;
  queue_counters();
  next_game_mode_task_b(); return;
}

void next_game_mode_task_b(void) {
  ram[GAME_MODE_TASK]++;
  return;
}

// in: a; out: none
void queue_text(void) {
  ram[0x100 + sp] = a;
  sp--;
  y = (uint8_t)(a << 1);
  if (y >= 4) {
    if (y >= 8) {
      y = 8;
    }
    if (ram[PLAYER_COUNT] == 0) {
      y++;
    }
  }
  x = ROM(TEXT_OFFSETS + y);
  y = 0;
  do {
    a = ROM(TEXT_STATUS_BAR + x);
    if (a == 0xff) { goto loc_882e; }
    ram[VRAM_LIST1 + y] = a;
    x++;
    y++;
  } while (y != 0);

loc_882e:
  ram[VRAM_LIST1 + y] = 0;
  sp++;
  a = ram[0x100 + sp];
  x = a;
  carry_flag = a >= 4;
  if (!carry_flag) {
    x--;
    if (x == 0) {
      a = (uint8_t)(ram[LIVES] + 1);
      carry_flag = a >= 0xa;
      if (carry_flag) {
        a = (uint8_t)(a - 0xa - !carry_flag);
        ram[VRAM_LIST1 + 7] = 0x9f;
      }
      ram[VRAM_LIST1 + 8] = a;
      ram[VRAM_LIST1 + 0x13] = (uint8_t)(ram[WORLD_NUMBER] + 1);
      ram[VRAM_LIST1 + 0x15] = (uint8_t)(ram[LEVEL_NUMBER] + 1);
      return;
    }
    if (ram[PLAYER_COUNT] == 0) {
      return;
    }
    a = ram[CURRENT_PLAYER];
    if ((uint8_t)(x - 1) == 0) {
      if (ram[GAME_MODE] != 3) {
        a ^= 1;
      }
    }
    if ((a & 1) == 0) {
      return;
    }
    y = 4;
    do {
      ram[VRAM_LIST1 + 3 + y] = ROM(TEXT_LUIGI + y);
      y--;
    } while ((y & 0x80) == 0);
    return;
  }
  x = (uint8_t)((uint8_t)((uint8_t)(a - 4 - !carry_flag) << 1) << 1);
  y = 0;
  do {
    ram[VRAM_LIST1 + 0x1b + y] = ROM(TEXT_WARP_NUMBERS + x);
    x++;
    y = (uint8_t)((uint8_t)(y + 3) + 1);
  } while (y < 0xc);
  a = 0x2c;
  set_vram_list1_length(); return;
}

void wait_screen_timer(void) {
  if (ram[SCREEN_TIMER] != 0) {
    return;
  }
  hide_all_sprites();
  restart_screen_timer(); return;
}

void restart_screen_timer(void) {
  ram[SCREEN_TIMER] = 7;
  ram[SCREEN_TASK]++;
  return;
}

// turn one column of metatiles into a VRAM update
// in: none; out: x
void render_column(void) {
  ram[5] = ram[PARSER_COLUMN] & 1;
  y = ram[VRAM_LIST2_LENGTH];
  ram[0] = y;
  ram[VRAM_LIST2 + 1 + y] = ram[NT_ADDRESS_LOW];
  ram[VRAM_LIST2 + y] = ram[NT_ADDRESS_HIGH];
  ram[VRAM_LIST2 + 2 + y] = 0x9a;
  ram[4] = 0;
  x = 0;
  do {
    ram[1] = x;
    a = ram[METATILE_COLUMN + x] & 0xc0;
    ram[3] = a;
    carry_flag = (a & 0x80) != 0;
    a = (uint8_t)(a << 1);
    bool carry = (a & 0x80) != 0;
    y = (uint8_t)(((uint8_t)((a << 1) | carry_flag) << 1) | carry);
    ram[6] = ROM(METATILE_SETS_LOW + y);
    ram[7] = ROM(METATILE_SETS_HIGH + y);
    ram[2] = (uint8_t)((uint8_t)(ram[METATILE_COLUMN + x] << 1) << 1);
    a = (ram[PARSER_TASK] & 1) ^ 1;
    carry_flag = (a & 0x80) != 0;
    y = (uint8_t)((uint8_t)(a << 1) + ram[2] + carry_flag);
    x = ram[0];
    a = read_byte(indirect_y_addr(6));
    ram[VRAM_LIST2 + 3 + x] = a;
    a = read_byte((uint16_t)((ram[6] | (ram[7] << 8)) + (uint8_t)(y + 1)));
    ram[VRAM_LIST2 + 4 + x] = a;
    y = ram[4];
    if (ram[5] == 0) {
      carry_flag = (ram[1] & 1) != 0;
      if (carry_flag) { goto loc_892a; }
      bool carry3 = (ram[3] & 0x80) != 0;
      ram[3] = (uint8_t)((ram[3] << 1) | carry_flag);
      bool carry4 = (ram[3] & 0x80) != 0;
      ram[3] = (uint8_t)((ram[3] << 1) | carry3);
      ram[3] = (uint8_t)((ram[3] << 1) | carry4);
      goto loc_8930;
    }
    if ((ram[1] & 1) != 0) { goto loc_892e; }
    ram[3] = ram[3] >> 1;
    ram[3] = ram[3] >> 1;
    ram[3] = ram[3] >> 1;
    ram[3] = ram[3] >> 1;
    goto loc_8930;

loc_892a:
    ram[3] = ram[3] >> 1;
    ram[3] = ram[3] >> 1;

loc_892e:
    ram[4]++;

loc_8930:
    ram[ATTRIBUTE_BUFFER + y] |= ram[3];
    ram[0]++;
    ram[0]++;
    x = (uint8_t)(ram[1] + 1);
  } while (x < 0xd);
  y = (uint8_t)((uint8_t)(ram[0] + 2) + 1);
  ram[VRAM_LIST2 + y] = 0;
  ram[VRAM_LIST2_LENGTH] = y;
  ram[NT_ADDRESS_LOW]++;
  if ((ram[NT_ADDRESS_LOW] & 0x1f) != 0) {
    select_vram_list2(); return;
  }
  ram[NT_ADDRESS_LOW] = 0x80;
  ram[NT_ADDRESS_HIGH] ^= 4;
  select_vram_list2(); return;
}

// in: none; out: x
void render_attributes(void) {
  int diff = (ram[NT_ADDRESS_LOW] & 0x1f) - 4;
  ram[1] = (uint8_t)diff & 0x1f;
  a = ram[NT_ADDRESS_HIGH];
  if (diff < 0) {
    a ^= 4;
  }
  ram[0] = (a & 4) | 0x23;
  a = ram[1] >> 1;
  ram[1] = (uint8_t)((a >> 1) + 0xc0 + ((a & 1) != 0));
  x = 0;
  y = ram[VRAM_LIST2_LENGTH];
  do {
    ram[VRAM_LIST2 + y] = ram[0];
    a = (uint8_t)(ram[1] + 8);
    ram[VRAM_LIST2 + 1 + y] = a;
    ram[1] = a;
    ram[VRAM_LIST2 + 3 + y] = ram[ATTRIBUTE_BUFFER + x];
    ram[VRAM_LIST2 + 2 + y] = 1;
    ram[ATTRIBUTE_BUFFER + x] = 0;
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x++;
  } while (x < 7);
  ram[VRAM_LIST2 + y] = 0;
  ram[VRAM_LIST2_LENGTH] = y;
  select_vram_list2(); return;
}

void select_vram_list2(void) {
  ram[VRAM_LIST_SELECT] = 6;
  return;
}

// in: none; out: x
void cycle_palette(void) {
  a = ram[FRAME_COUNT] & 7;
  if (a != 0) {
    return;
  }
  x = ram[VRAM_LIST1_LENGTH];
  if (x >= 0x31) {
    return;
  }
  y = a;
  do {
    ram[VRAM_LIST1 + x] = ROM(BLANK_PALETTE + y);
    x++;
    y++;
  } while (y < 8);
  x = ram[VRAM_LIST1_LENGTH];
  ram[0] = 3;
  y = (uint8_t)((uint8_t)(ram[AREA_KIND] << 1) << 1);
  do {
    ram[VRAM_LIST1 + 3 + x] = ROM(CYCLING_PALETTES + y);
    y++;
    x++;
    ram[0]--;
  } while ((ram[0] & 0x80) == 0);
  x = ram[VRAM_LIST1_LENGTH];
  ram[VRAM_LIST1 + 4 + x] = ROM(CYCLING_PALETTE_IDS + ram[PALETTE_CYCLE_POS]);
  ram[VRAM_LIST1_LENGTH] = (uint8_t)(ram[VRAM_LIST1_LENGTH] + 7);
  ram[PALETTE_CYCLE_POS]++;
  if (ram[PALETTE_CYCLE_POS] < 6) {
    return;
  }
  ram[PALETTE_CYCLE_POS] = 0;
  return;
}

// in: none; out: carry
void erase_coin_or_axe(void) {
  y = 0x41;
  a = 3;
  x = ram[AREA_KIND];
  if (x == 0) {
    a = 4;
  }
  queue_metatile();
  ram[VRAM_LIST_SELECT] = 6;
  return;
}

// in: a, x; out: x
void redraw_block(void) {
  queue_block_tiles();
  ram[BLOCK_RESIDUE]++;
  ram[BLOCK_REPLACE + x]--;
  return;
}

// in: x; out: none
void redraw_block_blank(void) {
  a = 0;
  queue_block_tiles(); return;
}

// in: a, x; out: x
void queue_block_tiles(void) {
  y = 3;
  if (a != 0) {
    y = 0;
    if (a != 0x58) {
      if (a != 0x51) {
        y = 1;
        if (a != 0x5d) {
          if (a != 0x52) {
            y = 2;
          }
        }
      }
    }
  }
  a = y;
  y = (uint8_t)(ram[VRAM_LIST1_LENGTH] + 1);
  queue_metatile();
  advance_vram_list(); return;
}

// in: y; out: y
void advance_vram_list(void) {
  y--;
  a = (uint8_t)(y + 0xa);
  set_vram_list1_length(); return;
}

// in: a, x, y; out: x, y, carry
void queue_metatile(void) {
  ram[0] = x;
  ram[1] = y;
  x = (uint8_t)((uint8_t)(a << 1) << 1);
  y = 0x20;
  a = ram[6];
  if (a >= 0xd0) {
    y = 0x24;
  }
  ram[3] = y;
  ram[4] = (uint8_t)((a & 0xf) << 1);
  ram[5] = 0;
  a = (uint8_t)(ram[2] + 0x20);
  carry_flag = (a & 0x80) != 0;
  a = (uint8_t)(a << 1);
  ram[5] = (uint8_t)((ram[5] << 1) | carry_flag);
  bool carry2 = (ram[5] & 0x80) != 0;
  ram[5] = (uint8_t)((ram[5] << 1) | ((a & 0x80) != 0));
  uint16_t sum2 = (uint16_t)((uint8_t)(a << 1) + ram[4] + carry2);
  ram[4] = (uint8_t)sum2;
  ram[5] = (uint8_t)(ram[5] + (sum2 >= 0x100) + ram[3]);
  y = ram[1];
  queue_metatile_tiles(); return;
}

// in: x, y; out: x, carry
void queue_metatile_tiles(void) {
  ram[VRAM_LIST1 + 2 + y] = ROM(BLOCK_TILE_OFFSETS + x);
  ram[VRAM_LIST1 + 3 + y] = ROM(BLOCK_TILE_OFFSETS + 1 + x);
  ram[VRAM_LIST1 + 7 + y] = ROM(BLOCK_TILE_OFFSETS + 2 + x);
  ram[VRAM_LIST1 + 8 + y] = ROM(BLOCK_TILE_OFFSETS + 3 + x);
  a = ram[4];
  ram[VRAM_LIST1 + y] = a;
  uint16_t sum = (uint16_t)(a + 0x20);
  carry_flag = sum >= 0x100;
  ram[VRAM_LIST1 + 5 + y] = (uint8_t)sum;
  a = ram[5];
  ram[VRAM_LIST1_LENGTH + y] = a;
  ram[VRAM_LIST1 + 4 + y] = a;
  ram[VRAM_LIST1 + 1 + y] = 2;
  ram[VRAM_LIST1 + 6 + y] = 2;
  ram[VRAM_LIST1 + 9 + y] = 0;
  x = ram[0];
  return;
}

// clear_screens: native implementation, not generated.
// It may read: none.
// It must produce: y.

// clear_screen: native implementation, not generated.
// It may read: a.
// It must produce: y.

void read_joypads(void) {
  write_joypad1(1); write_joypad2(1);
  a = 0;
  x = 0;
  write_joypad1(0); write_joypad2(0);
  read_joypad();
  x = 1;
  read_joypad(); return;
}

// in: a, x; out: a
void read_joypad(void) {
  y = 8;
  do {
    ram[0x100 + sp] = a;
    a = read_byte((uint16_t)(JOYPAD_PORTS + x));
    ram[0] = a;
    carry_flag = (((a >> 1) | ram[0]) & 1) != 0;
    sp = (uint8_t)((uint8_t)(sp - 1) + 1);
    a = (uint8_t)((ram[0x100 + sp] << 1) | carry_flag);
    y--;
  } while (y != 0);
  ram[JOYPADS + x] = a;
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 0x30 & ram[(JOYPAD_MASKS + x) & 0x7ff]) != 0) {
    sp++;
    a = ram[0x100 + sp] & 0xcf;
    ram[JOYPADS + x] = a;
    return;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[(JOYPAD_MASKS + x) & 0x7ff] = a;
  return;
}

// apply_vram_list: native implementation, not generated.
// It may read: none.
// It must produce: none.

// set_scroll: native implementation, not generated.
// It may read: a.
// It must produce: none.

// set_ppu_ctrl: native implementation, not generated.
// It may read: a.
// It must produce: none.

// in: a; out: x
void queue_player_counters(void) {
  ram[0] = a;
  queue_counter_digits();
  a = ram[0] >> 1 >> 1 >> 1 >> 1;
  queue_counter_digits(); return;
}

// write a score, coin count or time to the VRAM update list
// in: a; out: x
void queue_counter_digits(void) {
  a = (uint8_t)(a + 1) & 0xf;
  if (a >= 6) {
    return;
  }
  ram[0x100 + sp] = a;
  sp--;
  y = (uint8_t)(a << 1);
  x = ram[VRAM_LIST1_LENGTH];
  a = 0x20;
  if (y == 0) {
    a = 0x22;
  }
  ram[VRAM_LIST1 + x] = a;
  ram[VRAM_LIST1 + 1 + x] = ROM(COUNTER_POSITIONS + y);
  a = ROM(COUNTER_POSITIONS + 1 + y);
  ram[VRAM_LIST1 + 2 + x] = a;
  ram[3] = a;
  ram[2] = x;
  sp++;
  a = ROM(COUNTER_OFFSETS + ram[0x100 + sp]);
  y = (uint8_t)(a - ROM(COUNTER_POSITIONS + 1 + y));
  x = ram[2];
  do {
    ram[VRAM_LIST1 + 3 + x] = ram[(DIGITS + y) & 0x7ff];
    x++;
    y++;
    ram[3]--;
  } while (ram[3] != 0);
  ram[VRAM_LIST1 + 3 + x] = 0;
  x = (uint8_t)((uint8_t)(x + 2) + 1);
  ram[VRAM_LIST1_LENGTH] = x;
  return;
}

// decimal add with carry across score or time digits
// in: y; out: x
void add_digits(void) {
  if (ram[GAME_MODE] == 0) { goto loc_8f7c; }
  x = 5;

loc_8f68:
  a = (uint8_t)(ram[SCORE_ADD + x] + ram[(DIGITS + y) & 0x7ff]);
  if ((a & 0x80) != 0) { goto loc_8f87; }
  if (a >= 0xa) { goto loc_8f8e; }

loc_8f75:
  ram[(DIGITS + y) & 0x7ff] = a;
  y--;
  x--;
  if ((x & 0x80) == 0) { goto loc_8f68; }

loc_8f7c:
  x = 6;
  do {
    ram[POPUP_TIMER + 7 + x] = 0;
    x--;
  } while ((x & 0x80) == 0);
  return;

loc_8f87:
  ram[POPUP_TIMER + 7 + x]--;
  a = 9;
  goto loc_8f75;

loc_8f8e:
  a = (uint8_t)(a - 0xa);
  ram[POPUP_TIMER + 7 + x]++;
  goto loc_8f75;
}

void update_top_score(void) {
  x = 5;
  check_top_score();
  x = 0xb;
  check_top_score(); return;
}

// in: x; out: none
void check_top_score(void) {
  y = 5;
  carry_flag = true;
  do {
    a = ram[(PLAYER_SCORE + x) & 0x7ff];
    carry_flag = (a - ram[(DIGITS + y) & 0x7ff] - !carry_flag) >= 0;
    x--;
    y--;
  } while ((y & 0x80) == 0);
  if (!carry_flag) {
    return;
  }
  x++;
  y++;
  do {
    ram[(DIGITS + y) & 0x7ff] = ram[(PLAYER_SCORE + x) & 0x7ff];
    x++;
    y++;
  } while (y < 6);
  return;
}

void start_game(void) {
  y = 0x6f;
  clear_memory();
  y = 0x1f;
  do {
    ram[(NOISE_MUSIC_POS + y) & 0x7ff] = a;
    y--;
  } while ((y & 0x80) == 0);
  ram[DEMO_TIMER] = 0x18;
  load_area_pointer();
  start_area(); return;
}

// reset area state and position the player at the entrance
void start_area(void) {
  y = 0x4b;
  clear_memory();
  x = 0x21;
  do {
    ram[(TIMERS + x) & 0x7ff] = 0;
    x--;
  } while ((x & 0x80) == 0);
  a = ram[HALFWAY_PAGE];
  if (ram[ALT_ENTRANCE] != 0) {
    a = ram[ENTRANCE_PAGE];
  }
  ram[SCREEN_LEFT_PAGE] = a;
  ram[PARSER_PAGE] = a;
  ram[BACKLOADING] = a;
  update_screen_edges();
  y = 0x20;
  a &= 1;
  if (a != 0) {
    y = 0x24;
  }
  ram[NT_ADDRESS_HIGH] = y;
  ram[NT_ADDRESS_LOW] = 0x80;
  a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(a << 1) << 1) << 1) << 1);
  ram[BLOCK_COLUMN_POS] = a;
  ram[AREA_OBJ_LENGTH]--;
  ram[AREA_OBJ_LENGTH + 1]--;
  ram[AREA_OBJ_LENGTH + 2]--;
  ram[COLUMN_SETS] = 0xb;
  load_area_data();
  if (ram[HARD_MODE] == 0) {
    a = ram[WORLD_NUMBER];
    zero_flag = a == 4;
    if (a < 4) { goto loc_904a; }
    if (zero_flag) {
      if (ram[LEVEL_NUMBER] < 2) { goto loc_904a; }
    }
  }
  ram[HARD_MODE_LATE]++;

loc_904a:
  if (ram[HALFWAY_PAGE] != 0) {
    ram[ENTRANCE_KIND] = 2;
  }
  ram[AREA_MUSIC_REQUEST] = 0x80;
  ram[SCREEN_OFF] = 1;
  ram[GAME_MODE_TASK]++;
  return;
}

void setup_new_game(void) {
  ram[RELOAD_TIME_LIMIT] = 1;
  ram[PLAYER_SMALL] = 1;
  ram[LIVES] = 2;
  ram[OTHER_LIVES] = 2;
  setup_area_display(); return;
}

void setup_area_display(void) {
  ram[SCREEN_OFF] = 0;
  y = 0;
  do {
    ram[VRAM_LIST1_LENGTH + y] = 0;
    y++;
  } while (y != 0);
  ram[TIME_UP] = 0;
  ram[SKIP_INTERMISSION] = 0;
  ram[BACKLOADING] = 0;
  ram[BALANCE_ALIGNMENT] = 0xff;
  a = ram[SCREEN_LEFT_PAGE];
  ram[PPU_CTRL_COPY] = ram[PPU_CTRL_COPY] >> 1;
  ram[PPU_CTRL_COPY] = (uint8_t)((ram[PPU_CTRL_COPY] << 1) | ((a & 1 & 1) != 0));
  queue_area_music();
  ram[SHUFFLE_STEPS + 2] = 0x38;
  ram[SHUFFLE_STEPS + 1] = 0x48;
  ram[SHUFFLE_STEPS] = 0x58;
  x = 0xe;
  do {
    ram[SPRITE_SLOTS + x] = ROM(DEFAULT_SPRITE_SLOTS + x);
    x--;
  } while ((x & 0x80) == 0);
  y = 3;
  do {
    ram[SPRITE_Y + y] = ROM(SPRITE_ZERO + y);
    y--;
  } while ((y & 0x80) == 0);
  do_nothing();
  set_unused_flag();
  ram[STATUS_BAR_SPLIT]++;
  ram[GAME_MODE_TASK]++;
  return;
}

// clear RAM below the offset in Y, sparing the stack in use
// in: y; out: a
void clear_memory(void) {
  x = 7;
  a = 0;
  ram[6] = 0;
  do {
    ram[7] = x;
    do {
      if (x == 1) {
        if (y >= 0x60) { goto loc_90de; }
      }
      dynamic_ram_write(indirect_y_addr(6), 0);

loc_90de:
      y--;
    } while (y != 0xff);
    x--;
  } while ((x & 0x80) == 0);
  return;
}

void queue_area_music(void) {
  if (ram[GAME_MODE] == 0) {
    return;
  }
  if (ram[ALT_ENTRANCE] != 2) {
    y = 5;
    a = ram[ENTRANCE_KIND];
    if (a == 6) { goto loc_9110; }
    if (a == 7) { goto loc_9110; }
  }
  y = ram[AREA_KIND];
  if (ram[CLOUD_OVERRIDE] != 0) {
    y = 4;
  }

loc_9110:
  ram[AREA_MUSIC_REQUEST] = ROM(AREA_MUSIC_KINDS + y);
  return;
}

void place_player(void) {
  ram[PLAYER_PAGE] = ram[SCREEN_LEFT_PAGE];
  ram[GRAVITY_FALLING] = 0x28;
  ram[PLAYER_FACING] = 1;
  ram[PLAYER_Y_PAGE] = 1;
  ram[PLAYER_STATE] = 0;
  ram[PLAYER_COLLISION]--;
  y = 0;
  ram[HALFWAY_PAGE] = 0;
  if (ram[AREA_KIND] == 0) {
    y = 1;
  }
  ram[SWIMMING] = y;
  x = ram[ENTRANCE_KIND];
  y = ram[ALT_ENTRANCE];
  if (y != 0) {
    if (y != 1) {
      x = ROM(ENTRANCE_X_POSITIONS + 2 + y);
    }
  }
  ram[PLAYER_X] = ROM(ENTRANCE_X_POSITIONS + y);
  ram[PLAYER_Y] = ROM(ENTRANCE_Y_POSITIONS + x);
  ram[PLAYER_SPRITE_ATTR] = ROM(ENTRANCE_PRIORITIES + x);
  queue_player_colors();
  y = ram[TIME_LIMIT];
  if (y != 0) {
    if (ram[RELOAD_TIME_LIMIT] != 0) {
      ram[TIME_DIGITS] = ROM(TIME_LIMITS + y);
      ram[TIME_DIGITS + 2] = 1;
      ram[TIME_DIGITS + 1] = 0;
      ram[RELOAD_TIME_LIMIT] = 0;
      ram[STAR_TIMER] = 0;
    }
  }
  if (ram[JOYPAD_OVERRIDE] != 0) {
    ram[PLAYER_STATE] = 3;
    x = 0;
    place_block_object();
    ram[BLOCK_Y] = 0xf0;
    x = 5;
    y = 0;
    spawn_vine();
  }
  if (ram[AREA_KIND] == 0) {
    spawn_bubble();
  }
  ram[PLAYER_TASK] = 7;
  return;
}

void lose_life(void) {
  ram[SCREEN_OFF]++;
  ram[STATUS_BAR_SPLIT] = 0;
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  ram[LIVES]--;
  if ((ram[LIVES] & 0x80) != 0) {
    ram[GAME_MODE_TASK] = 0;
    ram[GAME_MODE] = 3;
    return;
  }
  x = (uint8_t)(ram[WORLD_NUMBER] << 1);
  if ((ram[LEVEL_NUMBER] & 2) != 0) {
    x++;
  }
  a = ROM(HALFWAY_PAGES + x);
  if ((ram[LEVEL_NUMBER] & 1) == 0) {
    a = a >> 1 >> 1 >> 1 >> 1;
  }
  a &= 0xf;
  carry_flag = a >= ram[SCREEN_LEFT_PAGE];
  if (a != ram[SCREEN_LEFT_PAGE]) {
    if (carry_flag) {
      a = 0;
    }
  }
  ram[HALFWAY_PAGE] = a;
  switch_players();
  continue_game(); return;
}

void game_over_mode(void) {
  a = ram[GAME_MODE_TASK];
  switch (dispatch(0x921d)) {
    case 0x9224: start_game_over(); return;
    case 0x8567: run_screen_task(); return;
    case 0x9237: game_over_screen(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void start_game_over(void) {
  ram[SCREEN_TASK] = 0;
  ram[STATUS_BAR_SPLIT] = 0;
  ram[EVENT_MUSIC_REQUEST] = 2;
  ram[SCREEN_OFF]++;
  ram[GAME_MODE_TASK]++;
  return;
}

void game_over_screen(void) {
  ram[SCREEN_OFF] = 0;
  if ((ram[JOYPAD1] & 0x10) != 0) {
    end_game(); return;
  }
  if (ram[SCREEN_TIMER] != 0) { return; }
  end_game(); return;
}

void end_game(void) {
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  switch_players();
  if (!carry_flag) {
    continue_game(); return;
  }
  ram[CONTINUE_WORLD] = ram[WORLD_NUMBER];
  ram[GAME_MODE_TASK] = 0;
  ram[SCREEN_TIMER] = 0;
  ram[GAME_MODE] = 0;
  return;
}

void continue_game(void) {
  load_area_pointer();
  ram[PLAYER_SMALL] = 1;
  ram[RELOAD_TIME_LIMIT]++;
  ram[TIMERS_PAUSED] = 0;
  ram[PLAYER_POWER] = 0;
  ram[PLAYER_TASK] = 0;
  ram[GAME_MODE_TASK] = 0;
  ram[GAME_MODE] = 1;
  return;
}

// in: none; out: carry
void switch_players(void) {
  carry_flag = true;
  if (ram[PLAYER_COUNT] == 0) {
    return;
  }
  if ((ram[OTHER_LIVES] & 0x80) != 0) {
    return;
  }
  ram[CURRENT_PLAYER] ^= 1;
  x = 6;
  do {
    ram[0x100 + sp] = ram[(PLAYER_INFO + x) & 0x7ff];
    ram[(PLAYER_INFO + x) & 0x7ff] = ram[(OTHER_PLAYER_INFO + x) & 0x7ff];
    sp = (uint8_t)((uint8_t)(sp - 1) + 1);
    ram[(OTHER_PLAYER_INFO + x) & 0x7ff] = ram[0x100 + sp];
    x--;
  } while ((x & 0x80) == 0);
  carry_flag = false;
  return;
}

void set_unused_flag(void) {
  ram[MISC_CONTACT + 0xb] = 0xff;
  do_nothing(); return;
}

void do_nothing(void) {
  return;
}

// in: none; out: x
void run_parser_task(void) {
  y = ram[PARSER_TASK];
  if (y == 0) {
    y = 8;
    ram[PARSER_TASK] = 8;
  }
  a = (uint8_t)(y - 1);
  run_parser_subtask();
  ram[PARSER_TASK]--;
  if (ram[PARSER_TASK] != 0) {
    return;
  }
  render_attributes();
  return;
}

// in: a; out: x
void run_parser_subtask(void) {
  switch (dispatch(0x92ca)) {
    case 0x92db: advance_column(); return;
    case 0x88ae: render_column(); return;
    case 0x93fc: parse_area_column(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void advance_column(void) {
  ram[PARSER_COLUMN]++;
  a = ram[PARSER_COLUMN] & 0xf;
  if (a == 0) {
    ram[PARSER_COLUMN] = a;
    ram[PARSER_PAGE]++;
  }
  ram[BLOCK_COLUMN_POS]++;
  ram[BLOCK_COLUMN_POS] &= 0x1f;
  return;
}

// in: none; out: x
void parse_area_column(void) {
  if (ram[BACKLOADING] != 0) {
    process_area_objects();
  }
  x = 0xc;
  do {
    ram[METATILE_COLUMN + x] = 0;
    x--;
  } while ((x & 0x80) == 0);
  y = ram[BACKGROUND_SCENERY];
  if (y != 0) {
    a = ram[PARSER_PAGE];
    do {
      if (((a - 3) & 0x80) != 0) { goto loc_941f; }
      a = (uint8_t)(a - 3);
    } while ((a & 0x80) == 0);

loc_941f:
    a = (uint8_t)((uint8_t)((uint8_t)(a << 1) << 1) << 1);
    carry_flag = (a & 0x80) != 0;
    uint16_t sum = (uint16_t)((uint8_t)(a << 1) + ROM(BACKGROUND_SCENE_OFFSETS - 1 + y) + carry_flag);
    a = ROM(BACKGROUND_SCENES + (uint8_t)((uint8_t)sum + ram[PARSER_COLUMN] + (sum >= 0x100)));
    if (a != 0) {
      ram[0x100 + sp] = a;
      a = (uint8_t)((a & 0xf) - 1);
      ram[0] = a;
      carry_flag = (a & 0x80) != 0;
      x = (uint8_t)((uint8_t)(a << 1) + ram[0] + carry_flag);
      sp = (uint8_t)((uint8_t)(sp - 1) + 1);
      y = ram[0x100 + sp] >> 1 >> 1 >> 1 >> 1;
      ram[0] = 3;
      do {
        ram[METATILE_COLUMN + y] = ROM(BACKGROUND_SCENE_METATILES + x);
        x++;
        y++;
        if (y == 0xb) { goto loc_9455; }
        ram[0]--;
      } while (ram[0] != 0);
    }
  }

loc_9455:
  x = ram[FOREGROUND_SCENERY];
  if (x != 0) {
    y = ROM(BACKGROUND_SCENE_METATILES + 0x23 + x);
    x = 0;
    do {
      a = ROM(FOREGROUND_SCENES + y);
      if (a != 0) {
        ram[METATILE_COLUMN + x] = a;
      }
      y++;
      x++;
    } while (x != 0xd);
  }
  y = ram[AREA_KIND];
  if (y == 0) {
    if (ram[WORLD_NUMBER] != 7) { goto loc_947e; }
    a = 0x62;
  } else {

loc_947e:
    a = ROM(TERRAIN_METATILES + y);
    if (ram[CLOUD_OVERRIDE] != 0) {
      a = 0x88;
    }
  }
  ram[7] = a;
  x = 0;
  y = (uint8_t)(ram[TERRAIN_KIND] << 1);
  do {
    ram[0] = ROM(TERRAIN_PATTERNS + y);
    ram[1] = (uint8_t)(y + 1);
    if (ram[CLOUD_OVERRIDE] != 0) {
      if (x != 0) {
        ram[0] &= 8;
      }
    }
    y = 0;
    do {
      if ((ROM(BIT_MASKS + y) & ram[0]) != 0) {
        ram[METATILE_COLUMN + x] = ram[7];
      }
      x++;
      if (x == 0xd) { goto loc_94d3; }
      if (ram[AREA_KIND] == 2) {
        if (x == 0xb) {
          ram[7] = 0x54;
        }
      }
      y++;
    } while (y != 8);
    y = ram[1];
  } while (y != 0);

loc_94d3:
  process_area_objects();
  a = ram[BLOCK_COLUMN_POS];
  get_block_buffer_address();
  x = 0;
  y = 0;
  do {
    ram[0] = y;
    a = ram[METATILE_COLUMN + x] & 0xc0;
    carry_flag = (a & 0x80) != 0;
    a = (uint8_t)(a << 1);
    bool carry = (a & 0x80) != 0;
    y = (uint8_t)(((uint8_t)((a << 1) | carry_flag) << 1) | carry);
    a = ram[METATILE_COLUMN + x];
    if (a < ROM(BLOCK_BUFFER_LIMITS + y)) {
      a = 0;
    }
    y = ram[0];
    dynamic_ram_write(indirect_y_addr(6), a);
    y = (uint8_t)(y + 0x10);
    x++;
  } while (x < 0xd);
  return;
}

// in: none; out: x
void process_area_objects(void) {
  do {

process_area_objects:
    x = 2;
    do {
      ram[SLOT] = x;
      ram[PARSER_BEHIND] = 0;
      y = ram[AREA_DATA_POS];
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      if (a != 0xfd) {
        if ((ram[(AREA_OBJ_LENGTH + x) & 0x7ff] & 0x80) != 0) {
          y++;
          a = read_byte(indirect_y_addr(AREA_DATA_PTR));
          if ((a & 0x80) != 0) {
            if (ram[AREA_OBJ_PAGE_SET] == 0) {
              ram[AREA_OBJ_PAGE_SET]++;
              ram[AREA_OBJ_PAGE]++;
            }
          }
          y--;
          a = read_byte(indirect_y_addr(AREA_DATA_PTR));
          a &= 0xf;
          if (a == 0xd) {
            y++;
            a = read_byte(indirect_y_addr(AREA_DATA_PTR));
            y--;
            if ((a & 0x40) != 0) { goto loc_955d; }
            if (ram[AREA_OBJ_PAGE_SET] != 0) { goto loc_955d; }
            y++;
            a = read_byte(indirect_y_addr(AREA_DATA_PTR));
            ram[AREA_OBJ_PAGE] = a & 0x1f;
            ram[AREA_OBJ_PAGE_SET]++;
            goto loc_956e;
          }
          if (a == 0xe) {
            if (ram[BACKLOADING] != 0) { goto loc_9565; }
          }

loc_955d:
          if (ram[AREA_OBJ_PAGE] < ram[PARSER_PAGE]) { goto loc_956b; }
        }
      }

loc_9565:
      decode_area_object();
      goto loc_9571;

loc_956b:
      ram[PARSER_BEHIND]++;

loc_956e:
      next_area_object();

loc_9571:
      x = ram[SLOT];
      if ((ram[(AREA_OBJ_LENGTH + x) & 0x7ff] & 0x80) == 0) {
        ram[(AREA_OBJ_LENGTH + x) & 0x7ff]--;
      }
      x--;
    } while ((x & 0x80) == 0);
    if (ram[PARSER_BEHIND] != 0) { goto process_area_objects; }
  } while (ram[BACKLOADING] != 0);
  return;
}

void next_area_object(void) {
  ram[AREA_DATA_POS]++;
  ram[AREA_DATA_POS]++;
  ram[AREA_OBJ_PAGE_SET] = 0;
  return;
}

// in: x, y; out: none
void decode_area_object(void) {
  if ((ram[(AREA_OBJ_LENGTH + x) & 0x7ff] & 0x80) == 0) {
    y = ram[(AREA_OBJ_POS + x) & 0x7ff];
  }
  x = 0x10;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  if (a == 0xfd) {
    return;
  }
  a &= 0xf;
  if (a != 0xf) {
    x = 8;
    if (a != 0xc) {
      x = 0;
    }
  }
  ram[7] = x;
  x = ram[SLOT];
  if (a == 0xe) {
    ram[7] = 0;
    a = 0x2e;
  } else {
    if (a == 0xd) {
      ram[7] = 0x22;
      y++;
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      if ((a & 0x40) == 0) {
        return;
      }
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      a &= 0x7f;
      if (a == 0x4b) {
        ram[LOOP_COMMAND]++;
      }
      a &= 0x3f;
    } else {
      if (a < 0xc) {
        y++;
        a = read_byte(indirect_y_addr(AREA_DATA_PTR));
        a &= 0x70;
        if (a == 0) {
          ram[7] = 0x16;
          a = read_byte(indirect_y_addr(AREA_DATA_PTR));
          a &= 0xf;
          goto loc_9616;
        }
        ram[0] = a;
        if (a == 0x70) {
          a = read_byte(indirect_y_addr(AREA_DATA_PTR));
          if ((a & 8) != 0) {
            ram[0] = 0;
          }
        }
        a = ram[0];
      } else {
        y++;
        a = read_byte(indirect_y_addr(AREA_DATA_PTR));
        a &= 0x70;
      }
      a = a >> 1 >> 1 >> 1 >> 1;
    }
  }

loc_9616:
  ram[0] = a;
  if ((ram[(AREA_OBJ_LENGTH + x) & 0x7ff] & 0x80) != 0) {
    if (ram[AREA_OBJ_PAGE] != ram[PARSER_PAGE]) {
      a = read_byte((uint16_t)((ram[AREA_DATA_PTR] | (ram[AREA_DATA_PTR + 1] << 8)) + ram[AREA_DATA_POS]));
      if ((a & 0xf) != 0xe) {
        return;
      }
      if (ram[BACKLOADING] != 0) { goto loc_9656; }
      return;
    }
    if (ram[BACKLOADING] != 0) {
      ram[BACKLOADING] = 0;
      ram[PARSER_BEHIND] = 0;
      ram[SLOT] = 0;
      check_loop_object(); return;
    }
    a = read_byte((uint16_t)((ram[AREA_DATA_PTR] | (ram[AREA_DATA_PTR + 1] << 8)) + ram[AREA_DATA_POS]));
    a = (a & 0xf0) >> 1 >> 1 >> 1 >> 1;
    if (a != ram[PARSER_COLUMN]) {
      return;
    }

loc_9656:
    ram[(AREA_OBJ_POS + x) & 0x7ff] = ram[AREA_DATA_POS];
    next_area_object();
  }
  a = (uint8_t)(ram[0] + ram[7]);
  switch (dispatch(0x9666)) {
    case 0x98e5: draw_vertical_pipe(); return;
    case 0x9740: draw_area_style_object(); return;
    case 0x9a2e: draw_brick_row(); return;
    case 0x9a3e: draw_solid_row(); return;
    case 0x99f2: draw_coin_row(); return;
    case 0x9a50: draw_brick_column(); return;
    case 0x9a59: draw_solid_column(); return;
    case 0x9b41: draw_hole(); return;
    case 0x97ba: draw_pulley_rope(); return;
    case 0x9979: draw_bridge_high(); return;
    case 0x997c: draw_bridge_middle(); return;
    case 0x997f: draw_bridge_low(); return;
    case 0x9957: draw_water_hole(); return;
    case 0x9968: draw_question_row_high(); return;
    case 0x996b: draw_question_row_low(); return;
    case 0x99d0: draw_endless_rope(); return;
    case 0x99d7: draw_balance_rope(); return;
    case 0x9806: draw_castle(); return;
    case 0x9ab7: draw_staircase(); return;
    case 0x98ab: draw_exit_pipe(); return;
    case 0x9994: draw_flag_balls(); return;
    case 0x9b0e: draw_question_block(); return;
    case 0x9b01: draw_hidden_1up(); return;
    case 0x9b19: draw_item_brick(); return;
    case 0x9b14: draw_coin_brick(); return;
    case 0x986f: draw_water_pipe(); return;
    case 0x9a19: draw_empty_block(); return;
    case 0x9ad3: place_spring(); return;
    case 0x9882: draw_intro_pipe(); return;
    case 0x999e: draw_flagpole(); return;
    case 0x9a09: draw_axe(); return;
    case 0x9a0e: draw_small_object(); return;
    case 0x9a01: draw_castle_bridge(); return;
    case 0x96f2: warp_zone_lock(); return;
    case 0x970d: lock_scroll(); return;
    case 0x972b: start_frenzy(); return;
    case 0x9645: check_loop_object(); return;
    case 0x96c5: change_area_attributes(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void check_loop_object(void) {
  return;
}

// in: x; out: none
void change_area_attributes(void) {
  y = (uint8_t)(ram[(AREA_OBJ_POS + x) & 0x7ff] + 1);
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 0x40) == 0) {
    sp++;
    a = ram[0x100 + sp];
    ram[0x100 + sp] = a;
    ram[TERRAIN_KIND] = a & 0xf;
    sp = (uint8_t)((uint8_t)(sp - 1) + 1);
    a = (ram[0x100 + sp] & 0x30) >> 1 >> 1 >> 1;
    ram[BACKGROUND_SCENERY] = a >> 1;
    return;
  }
  sp++;
  a = ram[0x100 + sp] & 7;
  if (a >= 4) {
    ram[BACKGROUND_COLOR] = a;
    a = 0;
  }
  ram[FOREGROUND_SCENERY] = a;
  return;
}

void warp_zone_lock(void) {
  x = 4;
  if (ram[WORLD_NUMBER] != 0) {
    x = 5;
    if ((uint8_t)(ram[AREA_KIND] - 1) == 0) {
      x = 6;
    }
  }
  a = x;
  ram[WARP_ZONE] = a;
  queue_text();
  a = 0xd;
  remove_enemies();
  lock_scroll(); return;
}

void lock_scroll(void) {
  ram[SCROLL_LOCKED] ^= 1;
  return;
}

// in: a; out: none
void remove_enemies(void) {
  ram[0] = a;
  x = 4;
  do {
    if (ram[(uint8_t)(ENEMY_KIND + x)] == ram[0]) {
      ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0;
    }
    x--;
  } while ((x & 0x80) == 0);
  return;
}

void start_frenzy(void) {
  a = ROM(FRENZY_KINDS - 8 + ram[0]);
  y = 5;
  do {
    y--;
    if ((y & 0x80) != 0) { goto loc_973c; }
  } while (a != ram[ENEMY_KIND + y]);
  a = 0;

loc_973c:
  ram[FRENZY_QUEUE] = a;
  return;
}

// in: x; out: none
void draw_area_style_object(void) {
  a = ram[AREA_STYLE];
  switch (dispatch(0x9745)) {
    case 0x974c: draw_tree_ledge(); return;
    case 0x9778: draw_mushroom_ledge(); return;
    case 0x9a69: draw_cannon(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: none
void draw_tree_ledge(void) {
  get_object_row_size();
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  neg_flag = (a & 0x80) != 0;
  if (a != 0) {
    if (neg_flag) {
      ram[(AREA_OBJ_LENGTH + x) & 0x7ff] = y;
      if ((ram[PARSER_PAGE] | ram[PARSER_COLUMN]) != 0) {
        a = 0x16;
        draw_stem_none(); return;
      }
    }
    x = ram[7];
    ram[METATILE_COLUMN + x] = 0x17;
    a = 0x4c;
    draw_stem_full(); return;
  }
  a = 0x18;
  draw_stem_none(); return;
}

// in: x; out: none
void draw_mushroom_ledge(void) {
  get_object_length();
  ram[6] = y;
  if (carry_flag) {
    ram[(LEDGE_HALF_LENGTH + x) & 0x7ff] = ram[(AREA_OBJ_LENGTH + x) & 0x7ff] >> 1;
    a = 0x19;
    draw_stem_none(); return;
  }
  a = 0x1b;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  if (y == 0) {
    draw_stem_none(); return;
  }
  ram[6] = ram[(LEDGE_HALF_LENGTH + x) & 0x7ff];
  x = ram[7];
  ram[METATILE_COLUMN + x] = 0x1a;
  if (y != ram[6]) {
    return;
  }
  x++;
  ram[METATILE_COLUMN + x] = 0x4f;
  a = 0x50;
  draw_stem_full(); return;
}

// in: a, x; out: none
void draw_stem_full(void) {
  x++;
  y = 0xf;
  fill_column_down(); return;
}

// in: a; out: none
void draw_stem_none(void) {
  x = ram[7];
  y = 0;
  fill_column_down(); return;
}

// in: x; out: none
void draw_pulley_rope(void) {
  get_object_length();
  y = 0;
  if (!carry_flag) {
    y = 1;
    if (ram[(AREA_OBJ_LENGTH + x) & 0x7ff] == 0) {
      y = 2;
    }
  }
  ram[METATILE_COLUMN] = ROM(PULLEY_ROPE_METATILES + y);
  return;
}

// in: x; out: none
void draw_castle(void) {
  get_object_row_size();
  ram[7] = y;
  y = 4;
  get_object_fixed_length();
  ram[0x100 + sp] = x;
  sp--;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  x = ram[7];
  ram[6] = 0xb;
  do {
    ram[METATILE_COLUMN + x] = ROM(CASTLE_METATILES + y);
    x++;
    if (ram[6] != 0) {
      y = (uint8_t)((uint8_t)(y + 4) + 1);
      ram[6]--;
    }
  } while (x != 0xb);
  sp++;
  x = ram[0x100 + sp];
  if (ram[PARSER_PAGE] == 0) {
    return;
  }
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  if (a != 1) {
    y = ram[7];
    if (y == 0) {
      if (a == 3) { goto loc_9869; }
    }
    if (a != 2) {
      return;
    }
    get_object_x();
    ram[0x100 + sp] = a;
    sp--;
    find_free_enemy_slot();
    sp++;
    ram[(uint8_t)(ENEMY_X + x)] = ram[0x100 + sp];
    ram[(uint8_t)(ENEMY_PAGE + x)] = ram[PARSER_PAGE];
    ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
    ram[(uint8_t)(ENEMY_Y + x)] = 0x90;
    ram[(uint8_t)(ENEMY_KIND + x)] = 0x31;
    return;
  }

loc_9869:
  ram[METATILE_COLUMN + 0xa] = 0x52;
  return;
}

// in: x; out: none
void draw_water_pipe(void) {
  get_object_row_size();
  x = ram[7];
  ram[METATILE_COLUMN + x] = 0x6b;
  ram[METATILE_COLUMN + 1 + x] = 0x6c;
  return;
}

// in: x; out: none
void draw_intro_pipe(void) {
  y = 3;
  get_object_fixed_length();
  y = 0xa;
  draw_side_pipe();
  if (carry_flag) {
    return;
  }
  x = 6;
  do {
    ram[METATILE_COLUMN + x] = 0;
    x--;
  } while ((x & 0x80) == 0);
  ram[METATILE_COLUMN + 7] = ROM(VERTICAL_PIPE_METATILES + y);
  return;
}

// in: x; out: none
void draw_exit_pipe(void) {
  y = 3;
  get_object_fixed_length();
  get_object_row_size();
  draw_side_pipe(); return;
}

// in: x, y; out: y, carry
void draw_side_pipe(void) {
  ram[5] = (uint8_t)(y - 2);
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  ram[6] = y;
  x = (uint8_t)(ram[5] + 1);
  a = ROM(SIDE_PIPE_SHAFTS + y);
  carry_flag = a >= 0;
  if (a != 0) {
    x = 0;
    y = ram[5];
    fill_column_down();
    carry_flag = false;
  }
  y = ram[6];
  ram[METATILE_COLUMN + x] = ROM(SIDE_PIPE_TOPS + y);
  ram[METATILE_COLUMN + 1 + x] = ROM(SIDE_PIPE_BOTTOMS + y);
  return;
}

// in: x; out: none
void draw_vertical_pipe(void) {
  get_pipe_height();
  if (ram[0] != 0) {
    y = (uint8_t)((uint8_t)(y + 3) + 1);
  }
  ram[0x100 + sp] = y;
  sp--;
  if ((ram[AREA_NUMBER] | ram[WORLD_NUMBER]) != 0) {
    y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
    if (y != 0) {
      find_free_enemy_slot();
      if (!carry_flag) {
        get_object_x();
        uint16_t sum = (uint16_t)(a + 8);
        ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum;
        ram[(uint8_t)(ENEMY_PAGE + x)] = (uint8_t)(ram[PARSER_PAGE] + (sum >= 0x100));
        ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
        ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
        get_object_y();
        ram[(uint8_t)(ENEMY_Y + x)] = a;
        ram[(uint8_t)(ENEMY_KIND + x)] = 0xd;
        init_piranha();
      }
    }
  }
  sp++;
  y = ram[0x100 + sp];
  x = ram[7];
  ram[METATILE_COLUMN + x] = ROM(VERTICAL_PIPE_METATILES + y);
  x++;
  a = ROM(VERTICAL_PIPE_METATILES + 2 + y);
  y = (uint8_t)(ram[6] - 1);
  fill_column_down(); return;
}

// in: x; out: y
void get_pipe_height(void) {
  y = 1;
  get_object_fixed_length();
  get_object_row_size();
  ram[6] = y & 7;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  return;
}

// in: none; out: x, carry
void find_free_enemy_slot(void) {
  x = 0;
  do {
    carry_flag = false;
    if (ram[(uint8_t)(ENEMY_ACTIVE + x)] == 0) {
      return;
    }
    x++;
    carry_flag = x >= 5;
  } while (x != 5);
  return;
}

// in: x; out: none
void draw_water_hole(void) {
  get_object_length();
  ram[METATILE_COLUMN + 0xa] = 0x86;
  x = 0xb;
  y = 1;
  a = 0x87;
  fill_column_down(); return;
}

// in: x; out: none
void draw_question_row_high(void) {
  a = 3;
  draw_question_row(); return;
}

// in: x; out: none
void draw_question_row_low(void) {
  a = 7;
  draw_question_row(); return;
}

// in: a, x; out: none
void draw_question_row(void) {
  ram[0x100 + sp] = a;
  sp--;
  get_object_length();
  sp++;
  ram[METATILE_COLUMN + ram[0x100 + sp]] = 0xc0;
  return;
}

// in: x; out: none
void draw_bridge_high(void) {
  a = 6;
  draw_bridge_at_row_middle(); return;
}

// in: x; out: none
void draw_bridge_middle(void) {
  a = 7;
  draw_bridge_at_row_middle(); return;
}

// in: a, x; out: none
void draw_bridge_at_row_middle(void) {
  draw_bridge_at_row(); return;
}

// in: x; out: none
void draw_bridge_low(void) {
  a = 9;
  draw_bridge_at_row(); return;
}

// in: a, x; out: none
void draw_bridge_at_row(void) {
  ram[0x100 + sp] = a;
  sp--;
  get_object_length();
  sp++;
  x = ram[0x100 + sp];
  ram[METATILE_COLUMN + x] = 0xb;
  x++;
  y = 0;
  a = 0x63;
  fill_column_down(); return;
}

// in: x; out: none
void draw_flag_balls(void) {
  get_object_row_size();
  x = 2;
  a = 0x6d;
  fill_column_down(); return;
}

void draw_flagpole(void) {
  ram[METATILE_COLUMN] = 0x24;
  x = 1;
  y = 8;
  a = 0x25;
  fill_column_down();
  ram[METATILE_COLUMN + 0xa] = 0x61;
  get_object_x();
  int diff = a - 8;
  ram[ENEMY_X + 5] = (uint8_t)diff;
  ram[ENEMY_PAGE + 5] = (uint8_t)(ram[PARSER_PAGE] - (diff < 0));
  ram[ENEMY_Y + 5] = 0x30;
  ram[FLAG_SCORE_Y] = 0xb0;
  ram[ENEMY_KIND + 5] = 0x30;
  ram[ENEMY_ACTIVE + 5]++;
  return;
}

void draw_endless_rope(void) {
  x = 0;
  y = 0xf;
  draw_rope(); return;
}

// in: x; out: none
void draw_balance_rope(void) {
  ram[0x100 + sp] = x;
  sp--;
  x = 1;
  y = 0xf;
  a = 0x44;
  fill_column_down();
  sp++;
  x = ram[0x100 + sp];
  get_object_row_size();
  x = 1;
  draw_rope(); return;
}

// in: x, y; out: none
void draw_rope(void) {
  a = 0x40;
  fill_column_down(); return;
}

// in: x; out: none
void draw_coin_row(void) {
  a = ROM(COIN_METATILES + ram[AREA_KIND]);
  draw_row_of(); return;
}

// in: x; out: none
void draw_castle_bridge(void) {
  y = 0xc;
  get_object_fixed_length();
  draw_small_object(); return;
}

void draw_axe(void) {
  ram[VRAM_LIST_SELECT] = 8;
  draw_small_object(); return;
}

void draw_small_object(void) {
  y = ram[0];
  x = ROM(SMALL_OBJECT_ROWS - 2 + y);
  a = ROM(SMALL_OBJECT_ROWS + 1 + y);
  draw_single_column(); return;
}

// in: x; out: none
void draw_empty_block(void) {
  get_object_row_size();
  x = ram[7];
  a = 0xc4;
  draw_single_column(); return;
}

// in: a, x; out: none
void draw_single_column(void) {
  y = 0;
  fill_column_down(); return;
}

// in: x; out: none
void draw_brick_row(void) {
  y = ram[AREA_KIND];
  if (ram[CLOUD_OVERRIDE] != 0) {
    y = 4;
  }
  a = ROM(BRICK_METATILES + y);
  draw_row_of(); return;
}

// in: x; out: none
void draw_solid_row(void) {
  a = ROM(SOLID_BLOCK_METATILES + ram[AREA_KIND]);
  draw_row_of(); return;
}

// in: a, x; out: none
void draw_row_of(void) {
  ram[0x100 + sp] = a;
  sp--;
  get_object_length();
  draw_row_height1(); return;
}

void draw_row_height1(void) {
  x = ram[7];
  y = 0;
  sp++;
  a = ram[0x100 + sp];
  fill_column_down(); return;
}

// in: x; out: none
void draw_brick_column(void) {
  a = ROM(BRICK_METATILES + ram[AREA_KIND]);
  draw_column_of(); return;
}

// in: x; out: none
void draw_solid_column(void) {
  a = ROM(SOLID_BLOCK_METATILES + ram[AREA_KIND]);
  draw_column_of(); return;
}

// in: a, x; out: none
void draw_column_of(void) {
  ram[0x100 + sp] = a;
  sp--;
  get_object_row_size();
  sp++;
  a = ram[0x100 + sp];
  x = ram[7];
  fill_column_down(); return;
}

// in: x; out: none
void draw_cannon(void) {
  get_object_row_size();
  x = ram[7];
  ram[METATILE_COLUMN + x] = 0x64;
  x++;
  y--;
  if ((y & 0x80) == 0) {
    ram[METATILE_COLUMN + x] = 0x65;
    x++;
    y--;
    if ((y & 0x80) == 0) {
      a = 0x66;
      fill_column_down();
    }
  }
  x = ram[CANNON_SLOT];
  get_object_y();
  ram[CANNON_Y + x] = a;
  ram[CANNON_PAGE + x] = ram[PARSER_PAGE];
  get_object_x();
  ram[CANNON_X + x] = a;
  x++;
  if (x >= 6) {
    x = 0;
  }
  ram[CANNON_SLOT] = x;
  return;
}

// in: x; out: none
void draw_staircase(void) {
  get_object_length();
  if (carry_flag) {
    ram[STAIRCASE_STEP] = 9;
  }
  ram[STAIRCASE_STEP]--;
  y = ram[STAIRCASE_STEP];
  x = ROM(STAIRCASE_ROWS + y);
  y = ROM(STAIRCASE_HEIGHTS + y);
  a = 0x61;
  fill_column_down(); return;
}

// in: x; out: none
void place_spring(void) {
  get_object_row_size();
  find_free_enemy_slot();
  get_object_x();
  ram[(uint8_t)(ENEMY_X + x)] = a;
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[PARSER_PAGE];
  get_object_y();
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x32;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
  ram[(uint8_t)(ENEMY_ACTIVE + x)]++;
  x = ram[7];
  ram[METATILE_COLUMN + x] = 0x67;
  ram[METATILE_COLUMN + 1 + x] = 0x68;
  return;
}

// in: x; out: none
void draw_hidden_1up(void) {
  if (ram[HIDDEN_1UP] == 0) {
    return;
  }
  ram[HIDDEN_1UP] = 0;
  draw_item_brick(); return;
}

// in: x; out: none
void draw_question_block(void) {
  get_area_object_kind();
  draw_block_metatile(); return;
}

// in: x; out: none
void draw_coin_brick(void) {
  ram[BRICK_COIN_ACTIVE] = 0;
  draw_item_brick(); return;
}

// in: x; out: none
void draw_item_brick(void) {
  get_area_object_kind();
  ram[7] = y;
  a = 0;
  if ((uint8_t)(ram[AREA_KIND] - 1) != 0) {
    a = 5;
  }
  y = (uint8_t)(a + ram[7]);
  draw_block_metatile(); return;
}

// in: x, y; out: none
void draw_block_metatile(void) {
  ram[0x100 + sp] = ROM(BUMPABLE_METATILES + y);
  sp--;
  get_object_row_size();
  draw_row_height1(); return;
}

// in: none; out: y
void get_area_object_kind(void) {
  y = ram[0];
  return;
}

// in: x; out: none
void draw_hole(void) {
  get_object_length();
  if (carry_flag) {
    if (ram[AREA_KIND] == 0) {
      x = ram[CANNON_SLOT];
      get_object_x();
      int diff = a - 0x10;
      ram[CANNON_X + x] = (uint8_t)diff;
      ram[CANNON_PAGE + x] = (uint8_t)(ram[PARSER_PAGE] - (diff < 0));
      a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(y + 2) << 1) << 1) << 1);
      ram[CANNON_Y + x] = (uint8_t)(a << 1);
      x++;
      if (x >= 5) {
        x = 0;
      }
      ram[CANNON_SLOT] = x;
    }
  }
  a = ROM(HOLE_METATILES + ram[AREA_KIND]);
  x = 8;
  y = 0xf;
  fill_column_down(); return;
}

// in: a, x, y; out: x, y
void fill_column_down(void) {
  do {
    ram[AREA_OBJ_HEIGHT] = y;
    y = ram[METATILE_COLUMN + x];
    if (y != 0) {
      if (y == 0x17) { goto loc_9ba0; }
      if (y == 0x1a) { goto loc_9ba0; }
      if (y != 0xc0) {
        if (y >= 0xc0) { goto loc_9ba0; }
        if (y == 0x54) {
          if (a == 0x50) { goto loc_9ba0; }
        }
      }
    }
    ram[METATILE_COLUMN + x] = a;

loc_9ba0:
    x++;
    if (x >= 0xd) {
      return;
    }
    y = (uint8_t)(ram[AREA_OBJ_HEIGHT] - 1);
  } while ((y & 0x80) == 0);
  return;
}

// in: x; out: y, carry
void get_object_length(void) {
  get_object_row_size();
  get_object_fixed_length(); return;
}

// in: x, y; out: carry
void get_object_fixed_length(void) {
  carry_flag = false;
  if ((ram[(AREA_OBJ_LENGTH + x) & 0x7ff] & 0x80) == 0) {
    return;
  }
  ram[(AREA_OBJ_LENGTH + x) & 0x7ff] = y;
  carry_flag = true;
  return;
}

// in: x; out: y
void get_object_row_size(void) {
  y = ram[(AREA_OBJ_POS + x) & 0x7ff];
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  ram[7] = a & 0xf;
  y++;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  y = a & 0xf;
  return;
}

// in: none; out: a
void get_object_x(void) {
  a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(ram[PARSER_COLUMN] << 1) << 1) << 1) << 1);
  return;
}

// in: none; out: a
void get_object_y(void) {
  a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(ram[7] << 1) << 1) << 1) << 1);
  a = (uint8_t)(a + 0x20);
  return;
}

// in: a; out: none
void get_block_buffer_address(void) {
  ram[0x100 + sp] = a;
  y = a >> 1 >> 1 >> 1 >> 1;
  ram[7] = ROM(BLOCK_BUFFER_ADDRESSES + 2 + y);
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  ram[6] = (uint8_t)((ram[0x100 + sp] & 0xf) + ROM(BLOCK_BUFFER_ADDRESSES + y));
  return;
}

void load_area_pointer(void) {
  find_area_pointer();
  ram[AREA_POINTER] = a;
  get_area_kind(); return;
}

// in: a; out: a
void get_area_kind(void) {
  a &= 0x60;
  carry_flag = (a & 0x80) != 0;
  a = (uint8_t)(a << 1);
  bool carry = (a & 0x80) != 0;
  a = (uint8_t)((a << 1) | carry_flag);
  bool carry2 = (a & 0x80) != 0;
  a = (uint8_t)(((uint8_t)((a << 1) | carry) << 1) | carry2);
  ram[AREA_KIND] = a;
  return;
}

// in: none; out: a
void find_area_pointer(void) {
  a = ROM(AREAS_WORLD1 + (uint8_t)(ROM(WORLD_AREA_OFFSETS + ram[WORLD_NUMBER]) + ram[AREA_NUMBER]));
  return;
}

// level layout and enemy list pointers, header attributes
void load_area_data(void) {
  a = ram[AREA_POINTER];
  get_area_kind();
  ram[AREA_POINTER_OFFSET] = ram[AREA_POINTER] & 0x1f;
  y = (uint8_t)(ROM(ENEMY_LIST_KIND_OFFSETS + a) + ram[AREA_POINTER_OFFSET]);
  ram[ENEMY_DATA_PTR] = ROM(ENEMY_LISTS_LOW + y);
  ram[ENEMY_DATA_PTR + 1] = ROM(ENEMY_LISTS_HIGH + y);
  y = (uint8_t)(ROM(AREA_LAYOUT_KIND_OFFSETS + ram[AREA_KIND]) + ram[AREA_POINTER_OFFSET]);
  ram[AREA_DATA_PTR] = ROM(AREA_LAYOUTS_LOW + y);
  ram[AREA_DATA_PTR + 1] = ROM(AREA_LAYOUTS_HIGH + y);
  a = read_byte((uint16_t)(ram[AREA_DATA_PTR] | (ram[AREA_DATA_PTR + 1] << 8)));
  ram[0x100 + sp] = a;
  sp--;
  a &= 7;
  if (a >= 4) {
    ram[BACKGROUND_COLOR] = a;
    a = 0;
  }
  ram[FOREGROUND_SCENERY] = a;
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a;
  ram[ENTRANCE_KIND] = (a & 0x38) >> 1 >> 1 >> 1;
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  a = ram[0x100 + sp] & 0xc0;
  bool carry = (a & 0x80) != 0;
  a = (uint8_t)(a << 1);
  bool carry2 = (a & 0x80) != 0;
  ram[TIME_LIMIT] = (uint8_t)(((uint8_t)((a << 1) | carry) << 1) | carry2);
  a = read_byte((uint16_t)((ram[AREA_DATA_PTR] | (ram[AREA_DATA_PTR + 1] << 8)) + 1));
  ram[0x100 + sp] = a;
  ram[TERRAIN_KIND] = a & 0xf;
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a;
  ram[BACKGROUND_SCENERY] = (a & 0x30) >> 1 >> 1 >> 1 >> 1;
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  a = ram[0x100 + sp] & 0xc0;
  bool carry4 = (a & 0x80) != 0;
  a = (uint8_t)(a << 1);
  bool carry5 = (a & 0x80) != 0;
  a = (uint8_t)(((uint8_t)((a << 1) | carry4) << 1) | carry5);
  if (a == 3) {
    ram[CLOUD_OVERRIDE] = a;
    a = 0;
  }
  ram[AREA_STYLE] = a;
  uint16_t sum3 = (uint16_t)(ram[AREA_DATA_PTR] + 2);
  ram[AREA_DATA_PTR] = (uint8_t)sum3;
  ram[AREA_DATA_PTR + 1] = (uint8_t)(ram[AREA_DATA_PTR + 1] + (sum3 >= 0x100));
  return;
}

void playing_mode(void) {
  a = ram[GAME_MODE_TASK];
  switch (dispatch(0xaee1)) {
    case 0x8fe4: start_area(); return;
    case 0x8567: run_screen_task(); return;
    case 0x9071: setup_area_display(); return;
    case 0xaeea: run_game_frame(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void run_game_frame(void) {
  x = ram[CURRENT_PLAYER];
  ram[JOYPAD1] = ram[JOYPADS + x];
  run_player_task();
  if (ram[GAME_MODE_TASK] < 3) {
    return;
  }
  update_fireballs_and_bubbles();
  x = 0;
  do {
    ram[SLOT] = x;
    update_enemies_and_loops();
    update_score_popups();
    x++;
  } while (x != 6);
  player_offscreen_bits();
  player_screen_position();
  draw_player();
  update_block_metatiles();
  x = 1;
  ram[SLOT] = 1;
  update_block_objects();
  x--;
  ram[SLOT] = x;
  update_block_objects();
  update_misc_objects();
  update_cannons();
  update_whirlpools();
  update_flagpole();
  tick_time_limit();
  cycle_palette();
  if (((ram[PLAYER_Y_PAGE] - 2) & 0x80) != 0) {
    a = ram[STAR_TIMER];
    if (a == 0) { goto loc_af64; }
    if (a == 4) {
      if (ram[INTERVAL_TIMER_PERIOD] == 0) {
        queue_area_music();
      }
    }
  }
  a = ram[FRAME_COUNT];
  if (ram[STAR_TIMER] < 8) {
    a = a >> 1 >> 1;
  }
  a = a >> 1;
  cycle_player_palette();
  goto loc_af67;

loc_af64:
  reset_player_palette();

loc_af67:
  ram[BUTTONS_AB_PREVIOUS] = ram[BUTTONS_AB];
  ram[BUTTONS_HORIZONTAL] = 0;
  update_scroll_column(); return;
}

void update_scroll_column(void) {
  if (ram[VRAM_LIST_SELECT] == 6) {
    return;
  }
  if (ram[PARSER_TASK] == 0) {
    a = ram[SCROLLED_32];
    carry_flag = a >= 0x20;
    if (((a - 0x20) & 0x80) != 0) {
      return;
    }
    ram[SCROLLED_32] = (uint8_t)(ram[SCROLLED_32] - 0x20 - !carry_flag);
    ram[VRAM_LIST2_LENGTH] = 0;
  }
  run_parser_task();
  return;
}

void scroll_with_player(void) {
  ram[PLAYER_SCROLL_X] = (uint8_t)(ram[PLAYER_SCROLL_X] + ram[PLATFORM_X_SCROLL]);
  if (ram[SCROLL_LOCKED] == 0) {
    if (ram[PLAYER_SCROLL_POS] >= 0x50) {
      if (ram[SIDE_CONTACT_TIMER] == 0) {
        y = (uint8_t)(ram[PLAYER_SCROLL_X] - 1);
        if ((y & 0x80) == 0) {
          y++;
          if (y >= 2) {
            y--;
          }
          if (ram[PLAYER_SCROLL_POS] < 0x70) {
            scroll_screen(); return;
          }
          y = ram[PLAYER_SCROLL_X];
          scroll_screen(); return;
        }
      }
    }
  }
  ram[SCROLL_AMOUNT] = 0;
  check_player_offscreen(); return;
}

// in: y; out: x
void scroll_screen(void) {
  a = y;
  ram[SCROLL_AMOUNT] = a;
  ram[SCROLLED_32] = (uint8_t)(a + ram[SCROLLED_32]);
  uint16_t sum2 = (uint16_t)(y + ram[SCREEN_LEFT_X]);
  a = (uint8_t)sum2;
  ram[SCREEN_LEFT_X] = a;
  ram[SCROLL_X] = a;
  a = (uint8_t)(ram[SCREEN_LEFT_PAGE] + (sum2 >= 0x100));
  ram[SCREEN_LEFT_PAGE] = a;
  ram[0] = a & 1;
  ram[PPU_CTRL_COPY] = (ram[PPU_CTRL_COPY] & 0xfe) | ram[0];
  update_screen_edges();
  ram[SCROLL_TIMER] = 8;
  check_player_offscreen(); return;
}

// in: none; out: x
void check_player_offscreen(void) {
  x = 0;
  compute_x_offscreen_bits();
  ram[0] = a;
  y = 0;
  if ((a & 0x80) == 0) {
    y = 1;
    if ((ram[0] & 0x20) == 0) { goto loc_b02e; }
  }
  int diff = ram[(SCREEN_LEFT_X + y) & 0x7ff] - ROM(SCREEN_EDGE_OFFSETS + y);
  ram[PLAYER_X] = (uint8_t)diff;
  ram[PLAYER_PAGE] = (uint8_t)(ram[(SCREEN_LEFT_PAGE + y) & 0x7ff] - (diff < 0));
  if (ram[BUTTONS_HORIZONTAL] != ROM(OFFSCREEN_JOYPAD_MASKS + y)) {
    ram[PLAYER_X_SPEED] = 0;
  }

loc_b02e:
  ram[PLATFORM_X_SCROLL] = 0;
  return;
}

// in: none; out: a
void update_screen_edges(void) {
  uint16_t sum = (uint16_t)(ram[SCREEN_LEFT_X] + 0xff);
  ram[SCREEN_RIGHT_X] = (uint8_t)sum;
  a = (uint8_t)(ram[SCREEN_LEFT_PAGE] + (sum >= 0x100));
  ram[SCREEN_RIGHT_PAGE] = a;
  return;
}

void run_player_task(void) {
  a = ram[PLAYER_TASK];
  switch (dispatch(0xb04e)) {
    case 0x9131: place_player(); return;
    case 0xb1c7: vine_climb_in(); return;
    case 0xb206: enter_side_pipe_exit(); return;
    case 0xb1e5: enter_down_pipe(); return;
    case 0xb2a4: slide_down_flagpole(); return;
    case 0xb2ca: walk_to_castle(); return;
    case 0x91cd: lose_life(); return;
    case 0xb069: enter_area(); return;
    case 0xb0e9: control_player(); return;
    case 0xb233: grow_or_shrink(); return;
    case 0xb245: injury_blink(); return;
    case 0xb269: player_dying(); return;
    case 0xb27d: get_fire_flower(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void enter_area(void) {
  if (ram[ALT_ENTRANCE] != 2) {
    a = 0;
    if (ram[PLAYER_Y] < 0x30) {
      drive_player(); return;
    }
    a = ram[ENTRANCE_KIND];
    if (a != 6) {
      if (a != 7) { goto loc_b0d3; }
    }
    if (ram[PLAYER_SPRITE_ATTR] == 0) {
      a = 1;
      drive_player(); return;
    }
    walk_into_side_pipe();
    ram[CHANGE_AREA_TIMER]--;
    if (ram[CHANGE_AREA_TIMER] != 0) {
      return;
    }
    ram[SKIP_INTERMISSION]++;
    next_area(); return;
  }
  if (ram[JOYPAD_OVERRIDE] == 0) {
    a = 0xff;
    move_player_down();
    if (ram[PLAYER_Y] < 0x91) { goto loc_b0d3; }
    return;
  }
  if (ram[VINE_HEIGHT] != 0x60) {
    return;
  }
  y = 0;
  a = 1;
  if (ram[PLAYER_Y] >= 0x99) {
    ram[PLAYER_STATE] = 3;
    y = 1;
    a = 8;
    ram[BLOCK_BUFFER1 + 0xb4] = 8;
  }
  ram[COLLISIONS_OFF] = y;
  drive_player();
  if (ram[PLAYER_X] < 0x48) {
    return;
  }

loc_b0d3:
  ram[PLAYER_TASK] = 8;
  ram[PLAYER_FACING] = 1;
  ram[ALT_ENTRANCE] = 0;
  ram[COLLISIONS_OFF] = 0;
  ram[JOYPAD_OVERRIDE] = 0;
  return;
}

// in: a; out: none
void drive_player(void) {
  ram[JOYPAD1] = a;
  control_player(); return;
}

// joypad to movement, collisions, animation
void control_player(void) {
  if (ram[PLAYER_TASK] != 0xb) {
    if (ram[AREA_KIND] == 0) {
      if ((uint8_t)(ram[PLAYER_Y_PAGE] - 1) == 0) {
        if (ram[PLAYER_Y] < 0xd0) { goto loc_b104; }
      }
      ram[JOYPAD1] = 0;
    }

loc_b104:
    ram[BUTTONS_AB] = ram[JOYPAD1] & 0xc0;
    ram[BUTTONS_HORIZONTAL] = ram[JOYPAD1] & 3;
    a = ram[JOYPAD1] & 0xc;
    ram[BUTTONS_VERTICAL] = a;
    if ((a & 4) != 0) {
      if (ram[PLAYER_STATE] == 0) {
        if (ram[BUTTONS_HORIZONTAL] != 0) {
          ram[BUTTONS_HORIZONTAL] = 0;
          ram[BUTTONS_VERTICAL] = 0;
        }
      }
    }
  }
  move_player();
  y = 1;
  if (ram[PLAYER_SMALL] == 0) {
    y = 0;
    if (ram[CROUCHING] != 0) {
      y = 2;
    }
  }
  ram[PLAYER_BOX_KIND] = y;
  a = 1;
  y = ram[PLAYER_X_SPEED];
  neg_flag = (y & 0x80) != 0;
  if (y != 0) {
    if (neg_flag) {
      a = 2;
    }
    ram[PLAYER_DIR] = a;
  }
  scroll_with_player();
  player_offscreen_bits();
  player_screen_position();
  x = 0;
  compute_box();
  player_bg_collision();
  if (ram[PLAYER_Y] >= 0x40) {
    a = ram[PLAYER_TASK];
    if (a != 5) {
      if (a != 7) {
        if (a >= 4) {
          ram[PLAYER_SPRITE_ATTR] &= 0xdf;
        }
      }
    }
  }
  a = ram[PLAYER_Y_PAGE];
  if (((a - 2) & 0x80) != 0) {
    return;
  }
  ram[SCROLL_LOCKED] = 1;
  ram[7] = 4;
  x = 0;
  if (ram[TIME_UP] == 0) {
    if (ram[CLOUD_OVERRIDE] != 0) { goto loc_b1aa; }
  }
  x = 1;
  if (ram[PLAYER_TASK] != 0xb) {
    y = ram[DEATH_MUSIC_QUEUED];
    if (y == 0) {
      y++;
      ram[EVENT_MUSIC_REQUEST] = y;
      ram[DEATH_MUSIC_QUEUED] = y;
    }
    ram[7] = 6;
  }

loc_b1aa:
  if (((a - ram[7]) & 0x80) != 0) {
    return;
  }
  if (((uint8_t)(x - 1) & 0x80) == 0) {
    if (ram[EVENT_MUSIC_PLAYING] != 0) {
      return;
    }
    ram[PLAYER_TASK] = 6;
    return;
  }
  ram[JOYPAD_OVERRIDE] = 0;
  set_alt_entrance();
  ram[ALT_ENTRANCE]++;
  return;
}

void vine_climb_in(void) {
  if (ram[PLAYER_Y_PAGE] == 0) {
    if (ram[PLAYER_Y] < 0xe4) {
      set_alt_entrance(); return;
    }
  }
  a = 8;
  ram[JOYPAD_OVERRIDE] = 8;
  ram[PLAYER_STATE] = 3;
  drive_player(); return;
}

void set_alt_entrance(void) {
  ram[ALT_ENTRANCE] = 2;
  change_area(); return;
}

void enter_down_pipe(void) {
  a = 1;
  move_player_down();
  scroll_with_player();
  y = 0;
  if (ram[WARP_ZONE] != 0) {
    change_area_after_pipe(); return;
  }
  y = 1;
  if (ram[AREA_KIND] != 3) {
    change_area_after_pipe(); return;
  }
  y = 2;
  change_area_after_pipe(); return;
}

// in: a; out: none
void move_player_down(void) {
  ram[PLAYER_Y] = (uint8_t)(a + ram[PLAYER_Y]);
  return;
}

void enter_side_pipe_exit(void) {
  walk_into_side_pipe();
  y = 2;
  change_area_after_pipe(); return;
}

// in: y; out: none
void change_area_after_pipe(void) {
  ram[CHANGE_AREA_TIMER]--;
  if (ram[CHANGE_AREA_TIMER] != 0) {
    return;
  }
  ram[ALT_ENTRANCE] = y;
  change_area(); return;
}

// in: none; out: a
void change_area(void) {
  ram[SCREEN_OFF]++;
  a = 0;
  ram[GAME_MODE_TASK] = 0;
  ram[STATUS_BAR_SPLIT] = 0;
  return;
}

void walk_into_side_pipe(void) {
  ram[PLAYER_X_SPEED] = 8;
  y = 1;
  a = ram[PLAYER_X] & 0xf;
  if (a == 0) {
    ram[PLAYER_X_SPEED] = a;
    y = a;
  }
  a = y;
  drive_player();
  return;
}

void grow_or_shrink(void) {
  a = ram[TIMERS_PAUSED];
  if (a == 0xf8) {
    start_size_change(); return;
  }
  if (a != 0xc4) {
    return;
  }
  player_task_done();
  return;
}

void injury_blink(void) {
  a = ram[TIMERS_PAUSED];
  zero_flag = a == 0xf0;
  if (a < 0xf0) {
    if (a == 0xc8) { player_task_done(); return; }
    control_player(); return;
  }
  if (!zero_flag) { return; }
  start_size_change(); return;
}

void start_size_change(void) {
  y = ram[PLAYER_CHANGING_SIZE];
  if (y != 0) {
    return;
  }
  ram[PLAYER_ANIM_FRAME] = y;
  ram[PLAYER_CHANGING_SIZE]++;
  ram[PLAYER_SMALL] ^= 1;
  return;
}

void player_dying(void) {
  if (ram[TIMERS_PAUSED] >= 0xf0) { return; }
  control_player(); return;
}

void player_task_done(void) {
  ram[TIMERS_PAUSED] = 0;
  ram[PLAYER_TASK] = 8;
  return;
}

void get_fire_flower(void) {
  if (ram[TIMERS_PAUSED] != 0xc0) {
    a = ram[FRAME_COUNT] >> 1 >> 1;
    cycle_player_palette(); return;
  }
  player_task_done();
  reset_player_palette(); return;
}

// in: a; out: none
void cycle_player_palette(void) {
  ram[0] = a & 3;
  ram[PLAYER_SPRITE_ATTR] = (ram[PLAYER_SPRITE_ATTR] & 0xfc) | ram[0];
  return;
}

void reset_player_palette(void) {
  ram[PLAYER_SPRITE_ATTR] &= 0xfc;
  return;
}

void slide_down_flagpole(void) {
  if (ram[ENEMY_KIND + 5] == 0x30) {
    ram[PULSE1_SFX_REQUEST] = ram[FLAGPOLE_SOUND];
    a = 0;
    ram[FLAGPOLE_SOUND] = 0;
    if (ram[PLAYER_Y] >= 0x9e) {
      drive_player(); return;
    }
    a = 4;
    drive_player(); return;
  }
  ram[PLAYER_TASK]++;
  return;
}

void walk_to_castle(void) {
  a = 1;
  drive_player();
  if (ram[PLAYER_Y] >= 0xae) {
    if (ram[SCROLL_LOCKED] != 0) {
      ram[EVENT_MUSIC_REQUEST] = 0x20;
      ram[SCROLL_LOCKED] = 0;
    }
  }
  if ((ram[PLAYER_COLLISION] & 1) == 0) {
    if (ram[STAR_FLAG_TASK] == 0) {
      ram[STAR_FLAG_TASK]++;
    }
    ram[PLAYER_SPRITE_ATTR] = 0x20;
  }
  if (ram[STAR_FLAG_TASK] != 5) {
    return;
  }
  ram[LEVEL_NUMBER]++;
  if (ram[LEVEL_NUMBER] != 3) {
    next_area(); return;
  }
  if (ram[COINS_FOR_1UPS] < ROM(HIDDEN_1UP_COIN_COUNTS + ram[WORLD_NUMBER])) {
    next_area(); return;
  }
  ram[HIDDEN_1UP]++;
  next_area(); return;
}

void next_area(void) {
  ram[AREA_NUMBER]++;
  load_area_pointer();
  ram[RELOAD_TIME_LIMIT]++;
  change_area();
  ram[HALFWAY_PAGE] = a;
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  return;
}

void move_player(void) {
  a = 0;
  if (ram[PLAYER_SMALL] == 0) {
    if (ram[PLAYER_STATE] != 0) { goto loc_b33b; }
    a = ram[BUTTONS_VERTICAL] & 4;
  }
  ram[CROUCHING] = a;

loc_b33b:
  player_physics();
  if (ram[PLAYER_CHANGING_SIZE] != 0) {
    return;
  }
  a = ram[PLAYER_STATE];
  if (a != 3) {
    ram[CLIMB_SIDE_TIMER] = 0x18;
  }
  switch (dispatch(0xb350)) {
    case 0xb35a: move_on_ground(); return;
    case 0xb376: move_jumping_or_swimming(); return;
    case 0xb36d: move_falling(); return;
    case 0xb3cf: move_climbing(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void move_on_ground(void) {
  set_player_anim_speed();
  a = ram[BUTTONS_HORIZONTAL];
  if (a != 0) {
    ram[PLAYER_FACING] = a;
  }
  apply_friction();
  move_player_x();
  ram[PLAYER_SCROLL_X] = a;
  return;
}

void move_falling(void) {
  ram[GRAVITY] = ram[GRAVITY_FALLING];
  move_in_air(); return;
}

void move_jumping_or_swimming(void) {
  if ((ram[PLAYER_Y_SPEED] & 0x80) != 0) {
    if ((ram[BUTTONS_AB] & 0x80 & ram[BUTTONS_AB_PREVIOUS]) != 0) { goto loc_b393; }
    if ((uint8_t)(ram[JUMP_ORIGIN_Y] - ram[PLAYER_Y]) < ram[JUMP_CUT_SPEED]) { goto loc_b393; }
  }
  ram[GRAVITY] = ram[GRAVITY_FALLING];

loc_b393:
  if (ram[SWIMMING] == 0) {
    move_in_air(); return;
  }
  set_player_anim_speed();
  if (ram[PLAYER_Y] < 0x14) {
    ram[GRAVITY] = 0x18;
  }
  a = ram[BUTTONS_HORIZONTAL];
  if (a == 0) {
    move_in_air(); return;
  }
  ram[PLAYER_FACING] = a;
  move_in_air(); return;
}

void move_in_air(void) {
  a = ram[BUTTONS_HORIZONTAL];
  if (a != 0) {
    apply_friction();
  }
  move_player_x();
  ram[PLAYER_SCROLL_X] = a;
  if (ram[PLAYER_TASK] == 0xb) {
    ram[GRAVITY] = 0x28;
  }
  x = 0;
  if (ram[TIMERS_PAUSED] == 0) {
    if (ram[SPRING_ANIM_STEP] != 0) {
      return;
    }
  }
  ram[0] = ram[GRAVITY];
  a = 4;
  apply_gravity_object(); return;
}

void move_climbing(void) {
  uint16_t sum = (uint16_t)(ram[PLAYER_Y_FRACTION] + ram[PLAYER_Y_FORCE]);
  carry_flag = sum >= 0x100;
  ram[PLAYER_Y_FRACTION] = (uint8_t)sum;
  y = 0;
  a = ram[PLAYER_Y_SPEED];
  if ((a & 0x80) != 0) {
    y = (uint8_t)-0x1;
  }
  ram[0] = y;
  uint16_t sum2 = (uint16_t)(a + ram[PLAYER_Y] + carry_flag);
  ram[PLAYER_Y] = (uint8_t)sum2;
  ram[PLAYER_Y_PAGE] = (uint8_t)(ram[PLAYER_Y_PAGE] + ram[0] + (sum2 >= 0x100));
  a = ram[BUTTONS_HORIZONTAL] & ram[PLAYER_COLLISION];
  if (a != 0) {
    if (ram[CLIMB_SIDE_TIMER] != 0) {
      return;
    }
    ram[CLIMB_SIDE_TIMER] = 0x18;
    x = 0;
    y = ram[PLAYER_FACING];
    if ((a & 1) == 0) {
      x = 2;
    }
    if ((uint8_t)(y - 1) != 0) {
      x++;
    }
    uint16_t sum4 = (uint16_t)(ram[PLAYER_X] + ROM(CLIMB_STEPS_LOW + x));
    ram[PLAYER_X] = (uint8_t)sum4;
    ram[PLAYER_PAGE] = (uint8_t)(ram[PLAYER_PAGE] + ROM(CLIMB_STEPS_HIGH + x) + (sum4 >= 0x100));
    ram[PLAYER_FACING] = ram[BUTTONS_HORIZONTAL] ^ 3;
    return;
  }
  ram[CLIMB_SIDE_TIMER] = a;
  return;
}

// in: none; out: x
void player_physics(void) {
  if (ram[PLAYER_STATE] == 3) {
    y = 0;
    a = ram[BUTTONS_VERTICAL] & ram[PLAYER_COLLISION];
    if (a != 0) {
      y = 1;
      if ((a & 8) == 0) {
        y = 2;
      }
    }
    ram[PLAYER_Y_FORCE] = ROM(CLIMB_FORCES + y);
    a = 8;
    x = ROM(CLIMB_SPEEDS + y);
    ram[PLAYER_Y_SPEED] = x;
    if ((x & 0x80) == 0) {
      a = 4;
    }
    ram[PLAYER_ANIM_PERIOD] = a;
    return;
  }
  if (ram[SPRING_ANIM_STEP] == 0) {
    a = ram[BUTTONS_AB] & 0x80;
    if (a != 0) {
      if ((a & ram[BUTTONS_AB_PREVIOUS]) == 0) {
        if (ram[PLAYER_STATE] != 0) {
          if (ram[SWIMMING] == 0) { goto loc_b51c; }
          if (ram[JUMP_SWIM_TIMER] == 0) {
            if ((ram[PLAYER_Y_SPEED] & 0x80) != 0) { goto loc_b51c; }
          }
        }
        ram[JUMP_SWIM_TIMER] = 0x20;
        y = 0;
        ram[PLAYER_Y_FRACTION] = 0;
        ram[PLAYER_Y_FORCE] = 0;
        ram[JUMP_ORIGIN_Y_PAGE] = ram[PLAYER_Y_PAGE];
        ram[JUMP_ORIGIN_Y] = ram[PLAYER_Y];
        ram[PLAYER_STATE] = 1;
        a = ram[PLAYER_ABS_SPEED];
        if (a >= 9) {
          y = 1;
          if (a >= 0x10) {
            y = 2;
            if (a >= 0x19) {
              y = 3;
              if (a >= 0x1c) {
                y = 4;
              }
            }
          }
        }
        ram[JUMP_CUT_SPEED] = 1;
        if (ram[SWIMMING] != 0) {
          y = 5;
          if (ram[CANNON_TIMER] != 0) {
            y = 6;
          }
        }
        ram[GRAVITY] = ROM(JUMP_GRAVITY + y);
        ram[GRAVITY_FALLING] = ROM(FALL_GRAVITY + y);
        ram[PLAYER_Y_FORCE] = ROM(JUMP_INITIAL_FORCE + y);
        ram[PLAYER_Y_SPEED] = ROM(JUMP_SPEEDS + y);
        if (ram[SWIMMING] != 0) {
          ram[PULSE1_SFX_REQUEST] = 4;
          if (ram[PLAYER_Y] < 0x14) {
            ram[PLAYER_Y_SPEED] = 0;
          }
        } else {
          a = 1;
          if (ram[PLAYER_SMALL] != 0) {
            a = 0x80;
          }
          ram[PULSE1_SFX_REQUEST] = a;
        }
      }
    }
  }

loc_b51c:
  y = 0;
  ram[0] = 0;
  if (ram[PLAYER_STATE] != 0) {
    if (ram[PLAYER_ABS_SPEED] >= 0x19) { goto loc_b55e; }
    goto loc_b545;
  }
  y = 1;
  if (ram[AREA_KIND] != 0) {
    y = 0;
    if (ram[BUTTONS_HORIZONTAL] == ram[PLAYER_DIR]) {
      if ((ram[BUTTONS_AB] & 0x40) != 0) { goto loc_b559; }
      if (ram[RUNNING_TIMER] != 0) { goto loc_b55e; }
    }
  }

loc_b545:
  y++;
  ram[0]++;
  if (ram[RUNNING_SPEED] == 0) {
    if (ram[PLAYER_ABS_SPEED] < 0x21) { goto loc_b55e; }
  }
  ram[0]++;
  goto loc_b55e;

loc_b559:
  ram[RUNNING_TIMER] = 0xa;

loc_b55e:
  ram[MAX_LEFT_SPEED] = ROM(MAX_LEFT_SPEEDS + y);
  if (ram[PLAYER_TASK] == 7) {
    y = 3;
  }
  ram[MAX_RIGHT_SPEED] = ROM(MAX_RIGHT_SPEEDS + y);
  ram[FRICTION_LOW] = ROM(FRICTION_AMOUNTS + ram[0]);
  ram[FRICTION_HIGH] = 0;
  if (ram[PLAYER_FACING] == ram[PLAYER_DIR]) {
    return;
  }
  carry_flag = (ram[FRICTION_LOW] & 0x80) != 0;
  ram[FRICTION_LOW] = (uint8_t)(ram[FRICTION_LOW] << 1);
  ram[FRICTION_HIGH] = (uint8_t)((ram[FRICTION_HIGH] << 1) | carry_flag);
  return;
}

void set_player_anim_speed(void) {
  y = 0;
  a = ram[PLAYER_ABS_SPEED];
  if (a < 0x1c) {
    y = 1;
    if (a < 0xe) {
      y = 2;
    }
    a = ram[JOYPAD1] & 0x7f;
    if (a == 0) { goto loc_b5c5; }
    if ((a & 3) != ram[PLAYER_DIR]) { goto loc_b5b3; }
    a = 0;
  }
  ram[RUNNING_SPEED] = a;
  goto loc_b5c5;

loc_b5b3:
  if (ram[PLAYER_ABS_SPEED] < 0xb) {
    ram[PLAYER_DIR] = ram[PLAYER_FACING];
    ram[PLAYER_X_SPEED] = 0;
    ram[PLAYER_X_FORCE] = 0;
  }

loc_b5c5:
  ram[PLAYER_ANIM_PERIOD] = ROM(PLAYER_ANIM_PERIODS + y);
  return;
}

// in: a; out: none
void apply_friction(void) {
  a &= ram[PLAYER_COLLISION];
  if (a == 0) {
    a = ram[PLAYER_X_SPEED];
    neg_flag = (a & 0x80) != 0;
    if (a == 0) { goto loc_b620; }
    if (!neg_flag) { goto loc_b5fc; }
    goto loc_b5de;
  }
  if ((a & 1) != 0) {

loc_b5de:;
    uint16_t sum = (uint16_t)(ram[PLAYER_X_FORCE] + ram[FRICTION_LOW]);
    ram[PLAYER_X_FORCE] = (uint8_t)sum;
    a = (uint8_t)(ram[PLAYER_X_SPEED] + ram[FRICTION_HIGH] + (sum >= 0x100));
    ram[PLAYER_X_SPEED] = a;
    if (((a - ram[MAX_RIGHT_SPEED]) & 0x80) != 0) { goto loc_b617; }
    a = ram[MAX_RIGHT_SPEED];
    ram[PLAYER_X_SPEED] = a;
    goto loc_b620;
  }

loc_b5fc:;
  int diff = ram[PLAYER_X_FORCE] - ram[FRICTION_LOW];
  ram[PLAYER_X_FORCE] = (uint8_t)diff;
  a = (uint8_t)(ram[PLAYER_X_SPEED] - ram[FRICTION_HIGH] - (diff < 0));
  ram[PLAYER_X_SPEED] = a;
  if (((a - ram[MAX_LEFT_SPEED]) & 0x80) != 0) {
    a = ram[MAX_LEFT_SPEED];
    ram[PLAYER_X_SPEED] = a;
  }

loc_b617:
  if ((a & 0x80) != 0) {
    a = (uint8_t)((a ^ 0xff) + 1);
  }

loc_b620:
  ram[PLAYER_ABS_SPEED] = a;
  return;
}

void update_fireballs_and_bubbles(void) {
  if (ram[PLAYER_POWER] >= 2) {
    a = ram[BUTTONS_AB] & 0x40;
    if (a != 0) {
      if ((a & ram[BUTTONS_AB_PREVIOUS]) == 0) {
        x = ram[FIREBALL_COUNT] & 1;
        if (ram[(uint8_t)(FIREBALL_STATE + x)] == 0) {
          if ((uint8_t)(ram[PLAYER_Y_PAGE] - 1) == 0) {
            if (ram[CROUCHING] == 0) {
              if (ram[PLAYER_STATE] != 3) {
                ram[PULSE1_SFX_REQUEST] = 0x20;
                ram[(uint8_t)(FIREBALL_STATE + x)] = 2;
                y = ram[PLAYER_ANIM_PERIOD];
                ram[FIREBALL_THROW_TIMER] = y;
                ram[PLAYER_ANIM_TIMER] = (uint8_t)(y - 1);
                ram[FIREBALL_COUNT]++;
              }
            }
          }
        }
      }
    }
    x = 0;
    update_fireball();
    x = 1;
    update_fireball();
  }
  if (ram[AREA_KIND] != 0) {
    return;
  }
  x = 2;
  do {
    ram[SLOT] = x;
    update_bubble();
    bubble_screen_position();
    bubble_offscreen_bits();
    draw_bubble();
    x--;
  } while ((x & 0x80) == 0);
  return;
}

// in: x; out: none
void update_fireball(void) {
  ram[SLOT] = x;
  carry_flag = (ram[(uint8_t)(FIREBALL_STATE + x)] & 0x80) != 0;
  if (!carry_flag) {
    y = ram[(uint8_t)(FIREBALL_STATE + x)];
    if (y == 0) {
      return;
    }
    if ((uint8_t)(y - 1) != 0) {
      uint16_t sum = (uint16_t)(ram[PLAYER_X] + 4 + carry_flag);
      ram[(uint8_t)(FIREBALL_X + x)] = (uint8_t)sum;
      ram[(uint8_t)(FIREBALL_PAGE + x)] = (uint8_t)(ram[PLAYER_PAGE] + (sum >= 0x100));
      ram[(uint8_t)(FIREBALL_Y + x)] = ram[PLAYER_Y];
      ram[(uint8_t)(FIREBALL_Y_PAGE + x)] = 1;
      ram[(uint8_t)(FIREBALL_X_SPEED + x)] = ROM(FIREBALL_SPEEDS + (uint8_t)(ram[PLAYER_FACING] - 1));
      ram[(uint8_t)(FIREBALL_Y_SPEED + x)] = 4;
      ram[FIREBALL_BOX_KIND + x] = 7;
      ram[(uint8_t)(FIREBALL_STATE + x)]--;
    }
    x = (uint8_t)(x + 7);
    ram[0] = 0x50;
    ram[2] = 3;
    a = 0;
    apply_gravity();
    move_object_x();
    x = ram[SLOT];
    fireball_screen_position();
    fireball_offscreen_bits();
    fireball_box();
    fireball_bg_collision();
    if ((ram[FIREBALL_OFFSCREEN] & 0xcc) == 0) {
      fireball_enemy_collision();
      goto loc_ecde;
    }
    ram[(uint8_t)(FIREBALL_STATE + x)] = 0;
    return;
  }
  fireball_screen_position();
  goto loc_ed09;

loc_ecde:
  y = ram[FIREBALL_SPRITE_SLOT + x];
  ram[SPRITE_Y + y] = ram[FIREBALL_SCREEN_Y];
  ram[SPRITE_X + y] = ram[FIREBALL_SCREEN_X];
  draw_firebar_tile(); return;

loc_ed09:
  y = ram[BLOCK_SPRITE_SLOT + x];
  a = ram[(uint8_t)(FIREBALL_STATE + x)];
  ram[(uint8_t)(FIREBALL_STATE + x)]++;
  a = (a >> 1) & 7;
  carry_flag = a >= 3;
  if (!carry_flag) {
    draw_explosion(); return;
  }
  ram[(uint8_t)(FIREBALL_STATE + x)] = 0;
  return;
}

// in: x; out: none
void update_bubble(void) {
  ram[7] = ram[(RANDOM + 1 + x) & 0x7ff] & 1;
  if (ram[(uint8_t)(BUBBLE_Y + x)] != 0xf8) {
    move_bubble(); return;
  }
  if (ram[AIR_BUBBLE_TIMER] != 0) { return; }
  spawn_bubble(); return;
}

// in: x; out: none
void spawn_bubble(void) {
  y = 0;
  carry_flag = (ram[PLAYER_FACING] & 1) != 0;
  if (carry_flag) {
    y = 8;
  }
  uint16_t sum = (uint16_t)(y + ram[PLAYER_X] + carry_flag);
  ram[(uint8_t)(BUBBLE_X + x)] = (uint8_t)sum;
  ram[(uint8_t)(BUBBLE_PAGE + x)] = (uint8_t)(ram[PLAYER_PAGE] + (sum >= 0x100));
  ram[(uint8_t)(BUBBLE_Y + x)] = (uint8_t)(ram[PLAYER_Y] + 8);
  ram[(uint8_t)(BUBBLE_Y_PAGE + x)] = 1;
  ram[AIR_BUBBLE_TIMER] = ROM(BUBBLE_INTERVALS + ram[7]);
  move_bubble(); return;
}

// in: x; out: none
void move_bubble(void) {
  int diff = ram[BUBBLE_Y_FRACTION + x] - ROM(BUBBLE_FORCES + ram[7]);
  ram[BUBBLE_Y_FRACTION + x] = (uint8_t)diff;
  a = (uint8_t)(ram[(uint8_t)(BUBBLE_Y + x)] - (diff < 0));
  if (a < 0x20) {
    a = 0xf8;
  }
  ram[(uint8_t)(BUBBLE_Y + x)] = a;
  return;
}

// in: none; out: x
void tick_time_limit(void) {
  if (ram[GAME_MODE] == 0) {
    return;
  }
  a = ram[PLAYER_TASK];
  if (a < 8) {
    return;
  }
  if (a == 0xb) {
    return;
  }
  if (ram[PLAYER_Y_PAGE] >= 2) {
    return;
  }
  if (ram[TICK_TIMER] != 0) {
    return;
  }
  a = ram[TIME_DIGITS] | ram[TIME_DIGITS + 1] | ram[TIME_DIGITS + 2];
  if (a != 0) {
    if ((uint8_t)(ram[TIME_DIGITS] - 1) == 0) {
      if ((ram[TIME_DIGITS + 1] | ram[TIME_DIGITS + 2]) == 0) {
        ram[EVENT_MUSIC_REQUEST] = 0x40;
      }
    }
    ram[TICK_TIMER] = 0x18;
    y = 0x23;
    ram[SCORE_ADD + 5] = 0xff;
    add_digits();
    a = 0xa4;
    queue_player_counters(); return;
  }
  ram[PLAYER_POWER] = a;
  force_injury();
  ram[TIME_UP]++;
  return;
}

// in: x; out: none
void warp_zone_trigger(void) {
  if (ram[SCROLL_LOCKED] == 0) {
    return;
  }
  a = ram[PLAYER_Y] & ram[PLAYER_Y_PAGE];
  if (a != 0) {
    return;
  }
  ram[SCROLL_LOCKED] = a;
  ram[WARP_ZONE]++;
  erase_enemy(); return;
}

void update_whirlpools(void) {
  a = ram[AREA_KIND];
  if (a != 0) {
    return;
  }
  ram[CANNON_TIMER] = a;
  if (ram[TIMERS_PAUSED] != 0) {
    return;
  }
  y = 4;
  do {
    uint16_t sum = (uint16_t)(ram[CANNON_X + y] + ram[CANNON_Y + y]);
    carry_flag = sum >= 0x100;
    ram[2] = (uint8_t)sum;
    a = ram[CANNON_PAGE + y];
    if (a != 0) {
      ram[1] = (uint8_t)(a + carry_flag);
      carry_flag = (ram[PLAYER_X] - ram[CANNON_X + y]) >= 0;
      a = (uint8_t)(ram[PLAYER_PAGE] - ram[CANNON_PAGE + y] - !carry_flag);
      if ((a & 0x80) == 0) {
        a = (uint8_t)(ram[1] - ram[PLAYER_PAGE] - ((ram[2] - ram[PLAYER_X]) < 0));
        if ((a & 0x80) == 0) { goto loc_b7f5; }
      }
    }
    y--;
  } while ((y & 0x80) == 0);
  return;

loc_b7f5:
  ram[0] = ram[CANNON_Y + y] >> 1;
  uint16_t sum3 = (uint16_t)(ram[CANNON_X + y] + ram[0]);
  ram[1] = (uint8_t)sum3;
  ram[0] = (uint8_t)(ram[CANNON_PAGE + y] + (sum3 >= 0x100));
  if ((ram[FRAME_COUNT] & 1) != 0) {
    a = (uint8_t)(ram[0] - ram[PLAYER_PAGE] - ((ram[1] - ram[PLAYER_X]) < 0));
    if ((a & 0x80) != 0) {
      int diff7 = ram[PLAYER_X] - 1;
      ram[PLAYER_X] = (uint8_t)diff7;
      a = (uint8_t)(ram[PLAYER_PAGE] - (diff7 < 0));
    } else {
      if ((ram[PLAYER_COLLISION] & 1) == 0) { goto loc_b83b; }
      uint16_t sum5 = (uint16_t)(ram[PLAYER_X] + 1);
      ram[PLAYER_X] = (uint8_t)sum5;
      a = (uint8_t)(ram[PLAYER_PAGE] + (sum5 >= 0x100));
    }
    ram[PLAYER_PAGE] = a;
  }

loc_b83b:
  ram[0] = 0x10;
  ram[CANNON_TIMER] = 1;
  ram[2] = 1;
  a = 0;
  x = 0;
  apply_gravity(); return;
}

// in: none; out: x
void update_flagpole(void) {
  x = 5;
  ram[SLOT] = 5;
  if (ram[ENEMY_KIND + 5] != 0x30) {
    return;
  }
  if (ram[PLAYER_TASK] == 4) {
    if (ram[PLAYER_STATE] == 3) {
      if (ram[ENEMY_Y + 5] < 0xaa) {
        carry_flag = ram[PLAYER_Y] >= 0xa2;
        if (carry_flag) { goto loc_b899; }
        uint16_t sum = (uint16_t)(ram[ENEMY_Y_FRACTION + 5] + 0xff + carry_flag);
        ram[ENEMY_Y_FRACTION + 5] = (uint8_t)sum;
        a = (uint8_t)(ram[ENEMY_Y + 5] + 1 + (sum >= 0x100));
        ram[ENEMY_Y + 5] = a;
        int diff = ram[FLAG_SCORE_Y_FRACTION] - 0xff;
        ram[FLAG_SCORE_Y_FRACTION] = (uint8_t)diff;
        ram[FLAG_SCORE_Y] = (uint8_t)(ram[FLAG_SCORE_Y] - 1 - (diff < 0));
      } else {

loc_b899:
        y = ram[FLAG_SCORE_KIND];
        ram[SCORE_ADD + ROM(FLAGPOLE_SCORE_DIGITS + y)] = ROM(FLAGPOLE_SCORE_BOUNDS + y);
        add_score();
        ram[PLAYER_TASK] = 5;
      }
    }
  }
  enemy_offscreen_bits();
  enemy_screen_position();
  draw_flagpole_flag();
  return;
}

// in: x; out: x
void update_spring(void) {
  enemy_offscreen_bits();
  if (ram[TIMERS_PAUSED] == 0) {
    a = ram[SPRING_ANIM_STEP];
    if (a != 0) {
      y = (uint8_t)(a - 1);
      if ((y & 2) == 0) {
        ram[PLAYER_Y]++;
        ram[PLAYER_Y]++;
      } else {
        ram[PLAYER_Y]--;
        ram[PLAYER_Y]--;
      }
      a = (uint8_t)(ram[(uint8_t)(ENEMY_X_SPEED + x)] + ROM(SPRING_OFFSETS + y));
      ram[(uint8_t)(ENEMY_Y + x)] = a;
      if (y >= 1) {
        a = ram[BUTTONS_AB] & 0x80;
        if (a != 0) {
          if ((a & ram[BUTTONS_AB_PREVIOUS]) == 0) {
            ram[SPRING_FORCE] = 0xf4;
          }
        }
      }
      if (y == 3) {
        ram[PLAYER_Y_SPEED] = ram[SPRING_FORCE];
        ram[SPRING_ANIM_STEP] = 0;
      }
    }
  }
  enemy_screen_position();
  draw_enemy();
  check_enemy_offscreen();
  if (ram[SPRING_ANIM_STEP] == 0) {
    return;
  }
  if (ram[SPRING_TIMER] != 0) {
    return;
  }
  ram[SPRING_TIMER] = 4;
  ram[SPRING_ANIM_STEP]++;
  return;
}

// in: x, y; out: none
void spawn_vine(void) {
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x2f;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[BLOCK_PAGE + y];
  ram[(uint8_t)(ENEMY_X + x)] = ram[BLOCK_X + y];
  a = ram[BLOCK_Y + y];
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  y = ram[VINE_FLAG_SLOT];
  if (y == 0) {
    ram[VINE_START_Y] = a;
  }
  ram[VINE_SLOTS + y] = x;
  ram[VINE_FLAG_SLOT]++;
  ram[PULSE2_SFX_REQUEST] = 4;
  return;
}

// in: x; out: x
void update_vine(void) {
  if (x == 5) {
    if (ram[VINE_HEIGHT] != ROM(VINE_HEIGHTS + (uint8_t)(ram[VINE_FLAG_SLOT] - 1))) {
      carry_flag = ((ram[FRAME_COUNT] >> 1) & 1) != 0;
      if (carry_flag) {
        ram[ENEMY_Y + 5] = (uint8_t)(ram[ENEMY_Y + 5] - 1 - !carry_flag);
        ram[VINE_HEIGHT]++;
      }
    }
    if (ram[VINE_HEIGHT] >= 8) {
      enemy_screen_position();
      enemy_offscreen_bits();
      y = 0;
      do {
        draw_vine();
        y++;
      } while (y != ram[VINE_FLAG_SLOT]);
      if ((ram[ENEMY_OFFSCREEN] & 0xc) != 0) {
        y--;
        do {
          x = ram[VINE_SLOTS + y];
          erase_enemy();
          y--;
        } while ((y & 0x80) == 0);
        ram[VINE_FLAG_SLOT] = a;
        ram[VINE_HEIGHT] = a;
      }
      if (ram[VINE_HEIGHT] >= 0x20) {
        x = 6;
        a = 1;
        y = 0x1b;
        block_probe();
        y = ram[2];
        if (y < 0xd0) {
          if (read_byte(indirect_y_addr(6)) == 0) {
            dynamic_ram_write(indirect_y_addr(6), 0x26);
          }
        }
      }
    }
  }
  x = ram[SLOT];
  return;
}

void update_cannons(void) {
  if (ram[AREA_KIND] == 0) {
    return;
  }
  x = 2;
  do {
    ram[SLOT] = x;
    if (ram[(uint8_t)(ENEMY_ACTIVE + x)] == 0) {
      y = ram[HARD_MODE_LATE];
      a = ram[(RANDOM + 1 + x) & 0x7ff] & ROM(CANNON_MASKS + y);
      carry_flag = a >= 6;
      if (carry_flag) { goto loc_ba1a; }
      y = a;
      if (ram[CANNON_PAGE + y] == 0) { goto loc_ba1a; }
      a = ram[CANNON_TIMER + y];
      if (a != 0) {
        ram[CANNON_TIMER + y] = (uint8_t)(a - !carry_flag);
        goto loc_ba1a;
      }
      if (ram[TIMERS_PAUSED] != 0) { goto loc_ba1a; }
      ram[CANNON_TIMER + y] = 0xe;
      ram[(uint8_t)(ENEMY_PAGE + x)] = ram[CANNON_PAGE + y];
      ram[(uint8_t)(ENEMY_X + x)] = ram[CANNON_X + y];
      ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(ram[CANNON_Y + y] - 8);
      ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
      ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
      ram[(uint8_t)(ENEMY_STATE + x)] = 0;
      ram[ENEMY_BOX_KIND + x] = 9;
      ram[(uint8_t)(ENEMY_KIND + x)] = 0x33;
    } else {

loc_ba1a:
      if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x33) {
        check_enemy_offscreen();
        if (ram[(uint8_t)(ENEMY_ACTIVE + x)] != 0) {
          enemy_offscreen_bits();
          update_bullet();
        }
      }
    }
    x--;
  } while ((x & 0x80) == 0);
  return;
}

// in: x; out: x
void update_bullet(void) {
  if (ram[TIMERS_PAUSED] == 0) {
    if (ram[(uint8_t)(ENEMY_STATE + x)] == 0) {
      if ((ram[ENEMY_OFFSCREEN] & 0xc) == 0xc) { goto loc_ba85; }
      y = 1;
      player_enemy_distance();
      if (!neg_flag) {
        y = 2;
      }
      ram[(uint8_t)(ENEMY_DIR + x)] = y;
      y--;
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(BULLET_SPEEDS + y);
      if ((uint8_t)(ram[0] + 0x28 + carry_flag) < 0x50) { goto loc_ba85; }
      ram[(uint8_t)(ENEMY_STATE + x)] = 1;
      ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0xa;
      ram[PULSE2_SFX_REQUEST] = 8;
    }
    if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
      move_enemy_down_fast();
    }
    move_enemy_x();
  }
  enemy_offscreen_bits();
  enemy_screen_position();
  enemy_box();
  player_enemy_collision();
  draw_enemy(); return;

loc_ba85:
  erase_enemy();
  return;
}

// in: none; out: x, carry
void spawn_hammer(void) {
  a = ram[RANDOM + 1] & 7;
  if (a == 0) {
    a = ram[RANDOM + 1] & 8;
  }
  y = a;
  if (ram[MISC_STATE + y] == 0) {
    if (ram[(uint8_t)(ENEMY_ACTIVE + ROM(HAMMER_SLOTS + y))] == 0) {
      x = ram[SLOT];
      ram[HAMMER_SLOT + y] = x;
      ram[MISC_STATE + y] = 0x90;
      ram[MISC_BOX_KIND + y] = 7;
      carry_flag = true;
      return;
    }
  }
  x = ram[SLOT];
  carry_flag = false;
  return;
}

// in: x; out: x
void update_hammer(void) {
  if (ram[TIMERS_PAUSED] == 0) {
    a = ram[(uint8_t)(MISC_STATE + x)] & 0x7f;
    y = ram[HAMMER_SLOT + x];
    carry_flag = a >= 2;
    if (a != 2) {
      if (carry_flag) { goto loc_bb09; }
      x = (uint8_t)(x + 0xd);
      ram[0] = 0x10;
      ram[1] = 0xf;
      ram[2] = 4;
      a = 0;
      apply_gravity();
      move_object_x();
      x = ram[SLOT];
      goto loc_bb28;
    }
    ram[(uint8_t)(MISC_Y_SPEED + x)] = 0xfe;
    ram[ENEMY_STATE + y] &= 0xf7;
    a = ROM(HAMMER_SPEEDS + (uint8_t)(ram[(uint8_t)(ENEMY_DIR + y)] - 1));
    x = ram[SLOT];
    ram[(uint8_t)(MISC_X_SPEED + x)] = a;

loc_bb09:
    ram[(uint8_t)(MISC_STATE + x)]--;
    uint16_t sum2 = (uint16_t)(ram[ENEMY_X + y] + 2);
    ram[(uint8_t)(MISC_X + x)] = (uint8_t)sum2;
    ram[(uint8_t)(MISC_PAGE + x)] = (uint8_t)(ram[ENEMY_PAGE + y] + (sum2 >= 0x100));
    ram[(uint8_t)(MISC_Y + x)] = (uint8_t)(ram[ENEMY_Y + y] - 0xa);
    ram[(uint8_t)(MISC_Y_PAGE + x)] = 1;
    goto loc_bb2b;

loc_bb28:
    player_hammer_collision();
  }

loc_bb2b:
  misc_offscreen_bits();
  misc_screen_position();
  misc_box();
  draw_hammer();
  return;
}

// in: x; out: none
void spawn_block_coin(void) {
  find_free_misc_slot();
  ram[MISC_PAGE + y] = ram[(uint8_t)(BLOCK_PAGE + x)];
  ram[MISC_X + y] = ram[(uint8_t)(BLOCK_X + x)] | 5;
  a = (uint8_t)(ram[(uint8_t)(BLOCK_Y + x)] - 0x10 - !carry_flag);
  ram[MISC_Y + y] = a;
  launch_coin(); return;
}

// in: x; out: x
void spawn_jumping_coin(void) {
  find_free_misc_slot();
  ram[MISC_PAGE + y] = ram[BLOCK_PAGE2 + x];
  a = (uint8_t)((uint8_t)((uint8_t)(ram[6] << 1) << 1) << 1);
  carry_flag = (a & 0x80) != 0;
  ram[MISC_X + y] = (uint8_t)(a << 1) | 5;
  ram[MISC_Y + y] = (uint8_t)(ram[2] + 0x20 + carry_flag);
  launch_coin(); return;
}

// in: x, y; out: x
void launch_coin(void) {
  ram[MISC_Y_SPEED + y] = 0xfb;
  ram[MISC_Y_PAGE + y] = 1;
  ram[MISC_STATE + y] = 1;
  ram[PULSE2_SFX_REQUEST] = 1;
  ram[SLOT] = x;
  give_coin();
  ram[COINS_FOR_1UPS]++;
  return;
}

// in: none; out: y, carry
void find_free_misc_slot(void) {
  y = 8;
  do {
    if (ram[MISC_STATE + y] == 0) { goto loc_bb92; }
    y--;
    carry_flag = y >= 5;
  } while (y != 5);
  y = 8;

loc_bb92:
  ram[COIN_JUMP_SLOT] = y;
  return;
}

void update_misc_objects(void) {
  x = 8;
  do {
    ram[SLOT] = x;
    a = ram[(uint8_t)(MISC_STATE + x)];
    if (a != 0) {
      if ((a & 0x80) != 0) {
        update_hammer();
      } else {
        if ((uint8_t)(ram[(uint8_t)(MISC_STATE + x)] - 1) != 0) {
          ram[(uint8_t)(MISC_STATE + x)]++;
          uint16_t sum = (uint16_t)(ram[(uint8_t)(MISC_X + x)] + ram[SCROLL_AMOUNT]);
          ram[(uint8_t)(MISC_X + x)] = (uint8_t)sum;
          a = (uint8_t)(ram[(uint8_t)(MISC_PAGE + x)] + (sum >= 0x100));
          ram[(uint8_t)(MISC_PAGE + x)] = a;
          if (ram[(uint8_t)(MISC_STATE + x)] != 0x30) { goto loc_bbe8; }
          ram[(uint8_t)(MISC_STATE + x)] = 0;
          goto loc_bbf4;
        }
        x = (uint8_t)(x + 0xd);
        ram[0] = 0x50;
        ram[2] = 6;
        ram[1] = 3;
        a = 0;
        apply_gravity();
        x = ram[SLOT];
        if (ram[(uint8_t)(MISC_Y_SPEED + x)] == 5) {
          ram[(uint8_t)(MISC_STATE + x)]++;
        }

loc_bbe8:
        misc_screen_position();
        misc_offscreen_bits();
        misc_box();
        draw_coin_or_score();
      }
    }

loc_bbf4:
    x--;
  } while ((x & 0x80) == 0);
  return;
}

// in: none; out: x
void give_coin(void) {
  ram[SCORE_ADD + 5] = 1;
  y = ROM(COIN_DIGIT_OFFSETS + ram[CURRENT_PLAYER]);
  add_digits();
  ram[COINS]++;
  if (ram[COINS] == 0x64) {
    ram[COINS] = 0;
    ram[LIVES]++;
    ram[PULSE2_SFX_REQUEST] = 0x40;
  }
  ram[SCORE_ADD + 4] = 2;
  add_score(); return;
}

// in: none; out: x
void add_score(void) {
  y = ROM(SCORE_DIGIT_OFFSETS + ram[CURRENT_PLAYER]);
  add_digits();
  queue_counters_for_player(); return;
}

// in: none; out: x
void queue_counters_for_player(void) {
  a = ROM(COUNTER_SELECTORS + ram[CURRENT_PLAYER]);
  queue_counters(); return;
}

// in: a; out: x
void queue_counters(void) {
  queue_player_counters();
  y = ram[VRAM_LIST1_LENGTH];
  if (ram[SPRITE_X + 0xf8 + y] == 0) {
    ram[SPRITE_X + 0xf8 + y] = 0x24;
  }
  x = ram[SLOT];
  return;
}

void spawn_powerup(void) {
  ram[ENEMY_STATE + 5] = 1;
  ram[ENEMY_ACTIVE + 5] = 1;
  ram[ENEMY_BOX_KIND + 5] = 3;
  if (ram[POWERUP_KIND] < 2) {
    a = ram[PLAYER_POWER];
    if (a >= 2) {
      a = a >> 1;
    }
    ram[POWERUP_KIND] = a;
  }
  ram[ENEMY_SPRITE_ATTR + 5] = 0x20;
  ram[PULSE2_SFX_REQUEST] = 2;
  return;
}

// in: none; out: x
void update_powerup(void) {
  x = 5;
  ram[SLOT] = 5;
  a = ram[ENEMY_STATE + 5];
  if (a == 0) {
    return;
  }
  if ((a & 0x80) != 0) {
    if (ram[TIMERS_PAUSED] == 0) {
      a = ram[POWERUP_KIND];
      if (a != 0) {
        if (a == 3) { goto loc_bcaa; }
        if (a == 2) {
          move_hopping_paratroopa();
          enemy_jump_check();
        }
      } else {

loc_bcaa:
        move_walker();
        enemy_bg_collision();
      }
    }
  } else {
    if ((ram[FRAME_COUNT] & 3) == 0) {
      ram[ENEMY_Y + 5]--;
      a = ram[ENEMY_STATE + 5];
      ram[ENEMY_STATE + 5]++;
      if (a >= 0x11) {
        ram[ENEMY_X_SPEED + 5] = 0x10;
        ram[ENEMY_STATE + 5] = 0x80;
        ram[ENEMY_SPRITE_ATTR + 5] = 0;
        ram[ENEMY_DIR + 5] = 1;
      }
    }
    if (ram[ENEMY_STATE + 5] < 6) {
      return;
    }
  }
  enemy_screen_position();
  enemy_offscreen_bits();
  enemy_box();
  draw_powerup();
  player_enemy_collision();
  check_enemy_offscreen();
  return;
}

// in: a; out: none
void bump_head(void) {
  ram[0x100 + sp] = a;
  sp--;
  a = 0x11;
  x = ram[SPRITE_SLOT_CTRL];
  if (ram[PLAYER_SMALL] == 0) {
    a = 0x12;
  }
  ram[(uint8_t)(BLOCK_STATE + x)] = a;
  redraw_block_blank();
  x = ram[SPRITE_SLOT_CTRL];
  a = ram[2];
  ram[BLOCK_ORIGIN_Y + x] = a;
  ram[BLOCK_BUFFER_LOW + x] = ram[6];
  a = read_byte((uint16_t)((ram[6] | (ram[7] << 8)) + a));
  find_bumped_metatile();
  ram[0] = a;
  y = ram[PLAYER_SMALL];
  if (y == 0) {
    a = y;
  }
  if (carry_flag) {
    ram[(uint8_t)(BLOCK_STATE + x)] = 0x11;
    a = 0xc4;
    y = ram[0];
    if (y != 0x58) {
      if (y != 0x5d) { goto loc_bd41; }
    }
    if (ram[BRICK_COIN_ACTIVE] == 0) {
      ram[BRICK_COIN_TIMER] = 0xb;
      ram[BRICK_COIN_ACTIVE]++;
    }
    if (ram[BRICK_COIN_TIMER] == 0) {
      y = 0xc4;
    }
    a = y;
  }

loc_bd41:
  ram[BLOCK_METATILE + x] = a;
  place_block_object();
  dynamic_ram_write((uint16_t)((ram[6] | (ram[7] << 8)) + ram[2]), 0x23);
  ram[BLOCK_BOUNCE_TIMER] = 0x10;
  sp++;
  ram[5] = ram[0x100 + sp];
  y = 0;
  if (ram[CROUCHING] == 0) {
    if (ram[PLAYER_SMALL] == 0) { goto loc_bd62; }
  }
  y = 1;

loc_bd62:
  a = (uint8_t)(ram[PLAYER_Y] + ROM(BLOCK_Y_OFFSETS + y)) & 0xf0;
  ram[(uint8_t)(BLOCK_Y + x)] = a;
  if (ram[(uint8_t)(BLOCK_STATE + x)] != 0x11) {
    shatter_brick();
  } else {
    bump_block();
  }
  ram[SPRITE_SLOT_CTRL] ^= 1;
  return;
}

// in: x; out: none
void place_block_object(void) {
  uint16_t sum = (uint16_t)(ram[PLAYER_X] + 8);
  ram[(uint8_t)(BLOCK_X + x)] = (uint8_t)sum & 0xf0;
  a = (uint8_t)(ram[PLAYER_PAGE] + (sum >= 0x100));
  ram[(uint8_t)(BLOCK_PAGE + x)] = a;
  ram[BLOCK_PAGE2 + x] = a;
  ram[(uint8_t)(BLOCK_Y_PAGE + x)] = ram[PLAYER_Y_PAGE];
  return;
}

void bump_block(void) {
  collect_coin_above();
  ram[PULSE1_SFX_REQUEST] = 2;
  ram[(uint8_t)(BLOCK_X_SPEED + x)] = 0;
  ram[BLOCK_Y_FORCE + x] = 0;
  ram[PLAYER_Y_SPEED] = 0;
  ram[(uint8_t)(BLOCK_Y_SPEED + x)] = 0xfe;
  a = ram[5];
  find_bumped_metatile();
  if (!carry_flag) {
    return;
  }
  a = y;
  carry_flag = a >= 9;
  if (carry_flag) {
    a = (uint8_t)(a - 5 - !carry_flag);
  }
  switch (dispatch(0xbdbf)) {
    case 0xbdd2: block_gives_mushroom(); return;
    case 0xbb38: spawn_block_coin(); return;
    case 0xbdd8: block_gives_1up(); return;
    case 0xbddf: block_gives_vine(); return;
    case 0xbdd5: block_gives_star(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: none
void block_gives_mushroom(void) {
  a = 0;
  block_gives_kind_star(); return;
}

// in: x; out: none
void block_gives_star(void) {
  a = 2;
  block_gives_kind_star(); return;
}

// in: a, x; out: none
void block_gives_kind_star(void) {
  block_gives_kind(); return;
}

// in: x; out: none
void block_gives_1up(void) {
  a = 3;
  block_gives_kind(); return;
}

// in: a, x; out: none
void block_gives_kind(void) {
  goto block_gives_kind;

loc_bc49:
  ram[ENEMY_KIND + 5] = 0x2e;
  ram[ENEMY_PAGE + 5] = ram[(uint8_t)(BLOCK_PAGE + x)];
  ram[ENEMY_X + 5] = ram[(uint8_t)(BLOCK_X + x)];
  ram[ENEMY_Y_PAGE + 5] = 1;
  ram[ENEMY_Y + 5] = (uint8_t)(ram[(uint8_t)(BLOCK_Y + x)] - 8);
  spawn_powerup(); return;

block_gives_kind:
  ram[POWERUP_KIND] = a;
  goto loc_bc49;
}

void block_gives_vine(void) {
  x = 5;
  y = ram[SPRITE_SLOT_CTRL];
  spawn_vine();
  return;
}

// in: a; out: y, carry
void find_bumped_metatile(void) {
  y = 0xd;
  do {
    carry_flag = a >= ROM(BUMPABLE_METATILES + y);
    if (a == ROM(BUMPABLE_METATILES + y)) {
      return;
    }
    y--;
  } while ((y & 0x80) == 0);
  carry_flag = false;
  return;
}

void shatter_brick(void) {
  collect_coin_above();
  ram[BLOCK_REPLACE + x] = 1;
  ram[NOISE_SFX_REQUEST] = 1;
  spawn_brick_chunks();
  ram[PLAYER_Y_SPEED] = 0xfe;
  ram[SCORE_ADD + 5] = 5;
  add_score();
  return;
}

// in: none; out: x
void collect_coin_above(void) {
  x = ram[SPRITE_SLOT_CTRL];
  y = ram[2];
  if (y == 0) {
    return;
  }
  a = (uint8_t)(y - 0x10);
  ram[2] = a;
  y = a;
  if (read_byte(indirect_y_addr(6)) != 0xc2) {
    return;
  }
  dynamic_ram_write(indirect_y_addr(6), 0);
  erase_coin_or_axe();
  x = ram[SPRITE_SLOT_CTRL];
  spawn_jumping_coin();
  return;
}

// in: x; out: none
void spawn_brick_chunks(void) {
  ram[BLOCK_ORIGIN_X + x] = ram[(uint8_t)(BLOCK_X + x)];
  ram[(uint8_t)(BLOCK_X_SPEED + x)] = 0xf0;
  ram[(uint8_t)(BLOCK_X_SPEED + 2 + x)] = 0xf0;
  ram[(uint8_t)(BLOCK_Y_SPEED + x)] = 0xfa;
  ram[(uint8_t)(BLOCK_Y_SPEED + 2 + x)] = 0xfc;
  ram[BLOCK_Y_FORCE + x] = 0;
  ram[BLOCK_Y_FORCE + 2 + x] = 0;
  ram[(uint8_t)(BLOCK_PAGE + 2 + x)] = ram[(uint8_t)(BLOCK_PAGE + x)];
  ram[(uint8_t)(BLOCK_X + 2 + x)] = ram[(uint8_t)(BLOCK_X + x)];
  a = (uint8_t)(ram[(uint8_t)(BLOCK_Y + x)] + 8);
  ram[(uint8_t)(BLOCK_Y + 2 + x)] = a;
  ram[(uint8_t)(BLOCK_Y_SPEED + x)] = 0xfa;
  return;
}

// in: x; out: x
void update_block_objects(void) {
  a = ram[(uint8_t)(BLOCK_STATE + x)];
  if (a != 0) {
    a &= 0xf;
    ram[0x100 + sp] = a;
    sp--;
    x = (uint8_t)(x + 9);
    if ((uint8_t)(a - 1) != 0) {
      apply_gravity_block();
      move_object_x();
      x = (uint8_t)(x + 2);
      apply_gravity_block();
      move_object_x();
      x = ram[SLOT];
      block_screen_position();
      block_offscreen_bits();
      draw_brick_chunks();
      sp++;
      a = ram[0x100 + sp];
      if (ram[(uint8_t)(BLOCK_Y_PAGE + x)] == 0) { goto loc_bed1; }
      ram[0x100 + sp] = a;
      sp--;
      if (0xf0 < ram[(uint8_t)(BLOCK_Y + 2 + x)]) {
        ram[(uint8_t)(BLOCK_Y + 2 + x)] = 0xf0;
      }
      sp++;
      a = ram[0x100 + sp];
      if (ram[(uint8_t)(BLOCK_Y + x)] < 0xf0) { goto loc_bed1; }
      goto loc_becf;
    }
    apply_gravity_block();
    x = ram[SLOT];
    block_screen_position();
    block_offscreen_bits();
    draw_block();
    sp++;
    a = ram[0x100 + sp];
    if ((ram[(uint8_t)(BLOCK_Y + x)] & 0xf) >= 5) { goto loc_bed1; }
    ram[BLOCK_REPLACE + x] = 1;

loc_becf:
    a = 0;
  }

loc_bed1:
  ram[(uint8_t)(BLOCK_STATE + x)] = a;
  return;
}

void update_block_metatiles(void) {
  x = 1;
  do {
    ram[SLOT] = x;
    if (ram[VRAM_LIST1] == 0) {
      if (ram[BLOCK_REPLACE + x] != 0) {
        ram[6] = ram[BLOCK_BUFFER_LOW + x];
        ram[7] = 5;
        a = ram[BLOCK_ORIGIN_Y + x];
        ram[2] = a;
        y = a;
        a = ram[BLOCK_METATILE + x];
        dynamic_ram_write(indirect_y_addr(6), a);
        redraw_block();
        ram[BLOCK_REPLACE + x] = 0;
      }
    }
    x--;
  } while ((x & 0x80) == 0);
  return;
}

// in: x; out: a, x, y
void move_enemy_x(void) {
  x++;
  move_object_x();
  x = ram[SLOT];
  return;
}

// in: none; out: a
void move_player_x(void) {
  a = ram[SPRING_ANIM_STEP];
  if (a != 0) {
    return;
  }
  x = a;
  move_object_x(); return;
}

// add speed (4.4 fixed point) to position and page
// in: x; out: a, y
void move_object_x(void) {
  a = (uint8_t)((uint8_t)((uint8_t)(ram[(uint8_t)(OBJ_X_SPEED + x)] << 1) << 1) << 1);
  ram[1] = (uint8_t)(a << 1);
  a = ram[(uint8_t)(OBJ_X_SPEED + x)] >> 1 >> 1 >> 1 >> 1;
  if (a >= 8) {
    a |= 0xf0;
  }
  ram[0] = a;
  y = 0;
  if ((a & 0x80) != 0) {
    y = (uint8_t)-0x1;
  }
  ram[2] = y;
  uint16_t sum = (uint16_t)(ram[OBJ_X_FORCE + x] + ram[1]);
  ram[OBJ_X_FORCE + x] = (uint8_t)sum;
  a = sum >= 0x100;
  ram[0x100 + sp] = a;
  carry_flag = (a & 1) != 0;
  uint16_t sum2 = (uint16_t)(ram[(uint8_t)(OBJ_X + x)] + ram[0] + carry_flag);
  ram[(uint8_t)(OBJ_X + x)] = (uint8_t)sum2;
  a = (uint8_t)(ram[(uint8_t)(OBJ_PAGE + x)] + ram[2] + (sum2 >= 0x100));
  ram[(uint8_t)(OBJ_PAGE + x)] = a;
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  a = (uint8_t)(ram[0x100 + sp] + ram[0]);
  return;
}

// in: x; out: x
void move_enemy_down_fast(void) {
  y = 0x3d;
  if (ram[(uint8_t)(ENEMY_STATE + x)] != 5) { move_vertically(); return; }
  move_falling_platform(); return;
}

// in: x; out: x
void move_falling_platform(void) {
  y = 0x20;
  move_vertically(); return;
}

// in: x, y; out: x
void move_vertically(void) {
  set_max_speed_high(); return;
}

// in: x; out: x
void move_drop_platform(void) {
  y = 0x7f;
  set_max_speed_mid(); return;
}

// in: x; out: x
void move_enemy_down_slow(void) {
  y = 0xf;
  set_max_speed_mid(); return;
}

// in: x, y; out: x
void set_max_speed_mid(void) {
  a = 2;
  apply_gravity_enemy(); return;
}

// in: x; out: x
void move_enemy_jumping(void) {
  y = 0x1c;
  set_max_speed_high(); return;
}

// in: x, y; out: x
void set_max_speed_high(void) {
  a = 3;
  apply_gravity_enemy(); return;
}

// in: a, x, y; out: x
void apply_gravity_enemy(void) {
  ram[0] = y;
  x++;
  apply_gravity_object();
  x = ram[SLOT];
  return;
}

// in: x; out: none
void apply_gravity_block(void) {
  ram[0] = 0x50;
  a = 8;
  apply_gravity_object(); return;
}

// in: a, x; out: none
void apply_gravity_object(void) {
  ram[2] = a;
  a = 0;
  apply_gravity(); return;
}

// in: x; out: x
void move_platform_down(void) {
  a = 0;
  move_platform(); return;
}

// in: x; out: x
void move_platform_up(void) {
  a = 1;
  move_platform(); return;
}

// in: a, x; out: x
void move_platform(void) {
  ram[0x100 + sp] = a;
  sp--;
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  x++;
  a = 5;
  if (y == 0x29) {
    a = 9;
  }
  ram[0] = a;
  ram[1] = 0xa;
  ram[2] = 3;
  sp++;
  a = ram[0x100 + sp];
  move_paratroopa_vertically(); return;
}

// in: a, x; out: x
void move_paratroopa_vertically(void) {
  apply_gravity();
  x = ram[SLOT];
  return;
}

// vertical speed, sub-pixel force and position with a speed cap
// in: a, x; out: none
void apply_gravity(void) {
  ram[0x100 + sp] = a;
  sp--;
  uint16_t sum = (uint16_t)(ram[OBJ_Y_FRACTION + x] + ram[OBJ_Y_FORCE + x]);
  carry_flag = sum >= 0x100;
  ram[OBJ_Y_FRACTION + x] = (uint8_t)sum;
  y = 0;
  a = ram[(uint8_t)(OBJ_Y_SPEED + x)];
  if ((a & 0x80) != 0) {
    y = (uint8_t)-0x1;
  }
  ram[7] = y;
  uint16_t sum2 = (uint16_t)(a + ram[(uint8_t)(OBJ_Y + x)] + carry_flag);
  ram[(uint8_t)(OBJ_Y + x)] = (uint8_t)sum2;
  a = (uint8_t)(ram[(uint8_t)(OBJ_Y_PAGE + x)] + ram[7] + (sum2 >= 0x100));
  ram[(uint8_t)(OBJ_Y_PAGE + x)] = a;
  uint16_t sum4 = (uint16_t)(ram[OBJ_Y_FORCE + x] + ram[0]);
  ram[OBJ_Y_FORCE + x] = (uint8_t)sum4;
  a = (uint8_t)(ram[(uint8_t)(OBJ_Y_SPEED + x)] + (sum4 >= 0x100));
  ram[(uint8_t)(OBJ_Y_SPEED + x)] = a;
  if (((a - ram[2]) & 0x80) == 0) {
    if (ram[OBJ_Y_FORCE + x] >= 0x80) {
      ram[(uint8_t)(OBJ_Y_SPEED + x)] = ram[2];
      ram[OBJ_Y_FORCE + x] = 0;
    }
  }
  sp++;
  if (ram[0x100 + sp] == 0) {
    return;
  }
  ram[7] = (uint8_t)((ram[2] ^ 0xff) + 1);
  int diff = ram[OBJ_Y_FORCE + x] - ram[1];
  ram[OBJ_Y_FORCE + x] = (uint8_t)diff;
  a = (uint8_t)(ram[(uint8_t)(OBJ_Y_SPEED + x)] - (diff < 0));
  ram[(uint8_t)(OBJ_Y_SPEED + x)] = a;
  if (((a - ram[7]) & 0x80) == 0) {
    return;
  }
  if (ram[OBJ_Y_FORCE + x] >= 0x80) {
    return;
  }
  ram[(uint8_t)(OBJ_Y_SPEED + x)] = ram[7];
  ram[OBJ_Y_FORCE + x] = 0xff;
  return;
}

// in: x; out: x
void update_enemies_and_loops(void) {
  a = ram[(uint8_t)(ENEMY_ACTIVE + x)];
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 0x80) == 0) {
    sp++;
    if (ram[0x100 + sp] != 0) { goto loc_c882; }
    if ((ram[PARSER_TASK] & 7) == 7) { return; }
    goto loc_c0cc;
  }
  sp++;
  a = ram[ENEMY_ACTIVE + (ram[0x100 + sp] & 0xf)];
  if (a != 0) {
    return;
  }
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  return;

loc_c0cc:
  if (ram[LOOP_COMMAND] != 0) {
    if (ram[PARSER_COLUMN] == 0) {
      y = 0xb;
      do {

loc_c0d8:
        y--;
        if ((y & 0x80) != 0) { goto loc_c12f; }
        if (ram[WORLD_NUMBER] != ROM(LOOP_WORLDS + y)) { goto loc_c0d8; }
      } while (ram[PARSER_PAGE] != ROM(LOOP_PAGES + y));
      if (ram[PLAYER_Y] != ROM(LOOP_Y_POSITIONS + y)) { goto loc_c115; }
      if (ram[PLAYER_STATE] != 0) { goto loc_c115; }
      if (ram[WORLD_NUMBER] != 6) { goto loc_c122; }
      ram[LOOP_CHECK_COUNT]++;

loc_c102:
      ram[LOOP_PASS_COUNT]++;
      if (ram[LOOP_PASS_COUNT] != 3) { goto loc_c12a; }
      if (ram[LOOP_CHECK_COUNT] == 3) { goto loc_c122; }
      goto loc_c11c;

loc_c115:
      if (ram[WORLD_NUMBER] == 6) { goto loc_c102; }

loc_c11c:
      load_enemies_and_loops();
      kill_all_enemies();

loc_c122:
      ram[LOOP_PASS_COUNT] = 0;
      ram[LOOP_CHECK_COUNT] = 0;

loc_c12a:
      ram[LOOP_COMMAND] = 0;
    }
  }

loc_c12f:
  a = ram[FRENZY_QUEUE];
  if (a != 0) {
    ram[(uint8_t)(ENEMY_KIND + x)] = a;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
    ram[(uint8_t)(ENEMY_STATE + x)] = 0;
    ram[FRENZY_QUEUE] = 0;
    spawn_enemy(); return;
  }
  y = ram[ENEMY_DATA_POS];
  a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  if (a != 0xff) {
    if ((a & 0xf) != 0xe) {
      if (x >= 5) {
        y++;
        a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
        if ((a & 0x3f) != 0x2e) {
          return;
        }
      }
    }
    uint16_t sum = (uint16_t)(ram[SCREEN_RIGHT_X] + 0x30);
    ram[7] = (uint8_t)sum & 0xf0;
    ram[6] = (uint8_t)(ram[SCREEN_RIGHT_PAGE] + (sum >= 0x100));
    y = (uint8_t)(ram[ENEMY_DATA_POS] + 1);
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    if ((a & 0x80) != 0) {
      if (ram[ENEMY_OBJ_PAGE_SET] == 0) {
        ram[ENEMY_OBJ_PAGE_SET]++;
        ram[ENEMY_OBJ_PAGE]++;
      }
    }
    y--;
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    if ((a & 0xf) == 0xf) {
      if (ram[ENEMY_OBJ_PAGE_SET] == 0) {
        y++;
        a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
        ram[ENEMY_OBJ_PAGE] = a & 0x3f;
        ram[ENEMY_DATA_POS]++;
        ram[ENEMY_DATA_POS]++;
        ram[ENEMY_OBJ_PAGE_SET]++;
        goto loc_c0cc;
      }
    }
    ram[(uint8_t)(ENEMY_PAGE + x)] = ram[ENEMY_OBJ_PAGE];
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a &= 0xf0;
    ram[(uint8_t)(ENEMY_X + x)] = a;
    carry_flag = a >= ram[SCREEN_RIGHT_X];
    if ((ram[(uint8_t)(ENEMY_PAGE + x)] - ram[SCREEN_RIGHT_PAGE] - !carry_flag) < 0) {
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      if ((a & 0xf) == 0xe) { goto loc_c231; }
      goto loc_c250;
    }
    carry_flag = ram[7] >= ram[(uint8_t)(ENEMY_X + x)];
    carry_flag = (ram[6] - ram[(uint8_t)(ENEMY_PAGE + x)] - !carry_flag) >= 0;
    if (carry_flag) {
      ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(a << 1) << 1) << 1) << 1);
      ram[(uint8_t)(ENEMY_Y + x)] = a;
      if (a == 0xe0) { goto loc_c231; }
      y++;
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      if ((a & 0x40) != 0) {
        if (ram[HARD_MODE_LATE] == 0) { goto loc_c25e; }
      }
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      a &= 0x3f;
      if (a >= 0x37) {
        if (a < 0x3f) { goto loc_c71b; }
      }
      if (a == 6) {
        if (ram[HARD_MODE] != 0) {
          a = 2;
        }
      }
      ram[(uint8_t)(ENEMY_KIND + x)] = a;
      ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
      spawn_enemy();
      if (ram[(uint8_t)(ENEMY_ACTIVE + x)] != 0) { goto loc_c25e; }
      return;
    }
  }
  a = ram[FRENZY_ENEMY];
  if (a == 0) {
    if (ram[VINE_FLAG_SLOT] != 1) {
      return;
    }
    a = 0x2f;
  }
  ram[(uint8_t)(ENEMY_KIND + x)] = a;
  spawn_enemy(); return;

loc_c231:
  y = (uint8_t)((uint8_t)(y + 1) + 1);
  a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  a = a >> 1 >> 1 >> 1 >> 1 >> 1;
  if (a != ram[WORLD_NUMBER]) { goto loc_c25b; }
  y--;
  ram[AREA_POINTER] = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  y++;
  a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  ram[ENTRANCE_PAGE] = a & 0x1f;
  goto loc_c25b;

loc_c250:
  a = read_byte((uint16_t)((ram[ENEMY_DATA_PTR] | (ram[ENEMY_DATA_PTR + 1] << 8)) + ram[ENEMY_DATA_POS]));
  if ((a & 0xf) == 0xe) {

loc_c25b:
    ram[ENEMY_DATA_POS]++;
  }

loc_c25e:
  ram[ENEMY_DATA_POS]++;
  ram[ENEMY_DATA_POS]++;
  ram[ENEMY_OBJ_PAGE_SET] = 0;
  x = ram[SLOT];
  return;

loc_c71b:
  y = 0;
  a = (uint8_t)(a - 0x37);
  ram[0x100 + sp] = a;
  sp--;
  if (a < 4) {
    ram[0x100 + sp] = a;
    sp--;
    y = 6;
    if (ram[HARD_MODE] != 0) {
      y = 2;
    }
    sp++;
    a = ram[0x100 + sp];
  }
  ram[1] = y;
  y = 0xb0;
  if ((a & 2) != 0) {
    y = 0x70;
  }
  ram[0] = y;
  ram[2] = ram[SCREEN_RIGHT_PAGE];
  ram[3] = ram[SCREEN_RIGHT_X];
  y = 2;
  sp++;
  if ((ram[0x100 + sp] & 1) != 0) {
    y = 3;
  }
  ram[GROUP_ENEMY_COUNT] = y;
  do {
    x = 0xff;
    do {
      x++;
      if (x >= 5) { goto loc_c25e; }
    } while (ram[(uint8_t)(ENEMY_ACTIVE + x)] != 0);
    ram[(uint8_t)(ENEMY_KIND + x)] = ram[1];
    ram[(uint8_t)(ENEMY_PAGE + x)] = ram[2];
    a = ram[3];
    ram[(uint8_t)(ENEMY_X + x)] = a;
    uint16_t sum3 = (uint16_t)(a + 0x18);
    ram[3] = (uint8_t)sum3;
    ram[2] = (uint8_t)(ram[2] + (sum3 >= 0x100));
    ram[(uint8_t)(ENEMY_Y + x)] = ram[0];
    ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
    init_enemy();
    ram[GROUP_ENEMY_COUNT]--;
  } while (ram[GROUP_ENEMY_COUNT] != 0);
  goto loc_c25e;

loc_c882:
  x = ram[SLOT];
  a = 0;
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = y >= 0x15;
  if (carry_flag) {
    a = (uint8_t)(y - 0x14 - !carry_flag);
  }
  switch (dispatch(0xc891)) {
    case 0xc8e0: run_normal_enemy(); return;
    case 0xc935: run_bowser_flame(); return;
    case 0xd295: run_firework(); return;
    case 0xc8d6: run_nothing(); return;
    case 0xc947: run_firebar(); return;
    case 0xc965: run_large_platform(); return;
    case 0xc94d: run_small_platform(); return;
    case 0xd065: run_bowser(); return;
    case 0xbc85: update_powerup(); return;
    case 0xb94b: update_vine(); return;
    case 0xd2d9: run_star_flag(); return;
    case 0xb8ba: update_spring(); return;
    case 0xb7a4: warp_zone_trigger(); return;
    case 0xc8d7: run_retainer(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: y; out: none
void load_enemies_and_loops(void) {
  ram[PLAYER_PAGE] = (uint8_t)(ram[PLAYER_PAGE] - 4);
  ram[PARSER_PAGE] = (uint8_t)(ram[PARSER_PAGE] - 4);
  ram[SCREEN_LEFT_PAGE] = (uint8_t)(ram[SCREEN_LEFT_PAGE] - 4);
  ram[SCREEN_RIGHT_PAGE] = (uint8_t)(ram[SCREEN_RIGHT_PAGE] - 4);
  ram[AREA_OBJ_PAGE] = (uint8_t)(ram[AREA_OBJ_PAGE] - 4);
  ram[ENEMY_OBJ_PAGE_SET] = 0;
  ram[AREA_OBJ_PAGE_SET] = 0;
  ram[ENEMY_DATA_POS] = 0;
  ram[ENEMY_OBJ_PAGE] = 0;
  ram[AREA_DATA_POS] = ROM(LOOP_AREA_OFFSETS + y);
  return;
}

// in: x; out: x
void spawn_enemy(void) {
  ram[(uint8_t)(ENEMY_STATE + x)] = 0;
  init_enemy();
  return;
}

// in: x; out: x
void init_enemy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = a >= 0x15;
  if (!carry_flag) {
    y = a;
    a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + 8 + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    ram[ENEMY_OFFSCREEN_MASKED + x] = 1;
    a = y;
  }
  switch (dispatch(0xc281)) {
    case 0xc30e: init_walker(); return;
    case 0xc31e: init_red_koopa(); return;
    case 0xc2f0: init_nothing(); return;
    case 0xc328: init_hammer_bro(); return;
    case 0xc2f1: init_goomba(); return;
    case 0xc342: init_blooper(); return;
    case 0xc36b: init_bullet(); return;
    case 0xc375: init_cheep_cheep(); return;
    case 0xc2f7: init_podoboo(); return;
    case 0xc787: init_piranha(); return;
    case 0xc7d1: init_bouncing_paratroopa(); return;
    case 0xc34a: init_red_paratroopa(); return;
    case 0xc33d: init_flyer_or_swimmer(); return;
    case 0xc385: init_lakitu(); return;
    case 0xc7a0: init_frenzy(); return;
    case 0xc7b8: end_frenzy(); return;
    case 0xc45c: init_firebar(); return;
    case 0xc459: init_long_firebar(); return;
    case 0xc7df: init_balance_platform(); return;
    case 0xc812: init_vertical_platform(); return;
    case 0xc83f: large_lift_up(); return;
    case 0xc845: large_lift_down(); return;
    case 0xc80b: init_side_platform(); return;
    case 0xc803: init_drop_platform(); return;
    case 0xc84b: small_lift_up(); return;
    case 0xc857: small_lift_down(); return;
    case 0xc549: init_bowser(); return;
    case 0xbc60: spawn_powerup(); return;
    case 0xb91e: spawn_vine(); return;
    case 0xc307: init_retainer(); return;
    case 0xc881: finish_enemy_init(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void init_nothing(void) {
  return;
}

// in: x; out: none
void init_goomba(void) {
  init_walker();
  set_small_box(); return;
}

// in: x; out: none
void init_podoboo(void) {
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 2;
  ram[(uint8_t)(ENEMY_Y + x)] = 2;
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = 1;
  ram[(uint8_t)(ENEMY_STATE + x)] = 0;
  set_small_box(); return;
}

// in: x; out: none
void init_retainer(void) {
  ram[(uint8_t)(ENEMY_Y + x)] = 0xb8;
  return;
}

// in: x; out: y
void init_walker(void) {
  y = 1;
  if (ram[HARD_MODE] == 0) {
    y = 0;
  }
  a = ROM(WALK_SPEEDS + y);
  set_enemy_speed(); return;
}

// in: a, x; out: none
void set_enemy_speed(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  set_tall_box(); return;
}

// in: x; out: none
void init_red_koopa(void) {
  init_walker();
  ram[(uint8_t)(ENEMY_STATE + x)] = 1;
  return;
}

// in: x; out: none
void init_hammer_bro(void) {
  ram[PLATFORM_CONTACT + x] = 0;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0;
  y = ram[HARD_MODE_LATE];
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = ROM(HAMMER_BRO_WALK_TIMES + y);
  a = 0xb;
  set_box_facing_left(); return;
}

// in: x; out: none
void init_flyer_or_swimmer(void) {
  a = 0;
  set_enemy_speed(); return;
}

// in: x; out: none
void init_blooper(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0;
  set_small_box(); return;
}

// in: x; out: a
void set_small_box(void) {
  a = 9;
  set_box_facing_left(); return;
}

// in: x; out: none
void init_red_paratroopa(void) {
  y = 0x30;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  ram[ENEMY_X_FORCE + x] = a;
  if ((a & 0x80) != 0) {
    y = 0xe0;
  }
  a = (uint8_t)(y + ram[(uint8_t)(ENEMY_Y + x)] + carry_flag);
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  set_tall_box(); return;
}

// in: x; out: none
void set_tall_box(void) {
  a = 3;
  set_box_facing_left(); return;
}

// in: a, x; out: a
void set_box_facing_left(void) {
  ram[ENEMY_BOX_KIND + x] = a;
  ram[(uint8_t)(ENEMY_DIR + x)] = 2;
  clear_vertical_motion(); return;
}

// in: x; out: a
void clear_vertical_motion(void) {
  a = 0;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0;
  ram[ENEMY_Y_FORCE + x] = 0;
  return;
}

// in: x; out: none
void init_bullet(void) {
  ram[(uint8_t)(ENEMY_DIR + x)] = 2;
  ram[ENEMY_BOX_KIND + x] = 9;
  return;
}

// in: x; out: none
void init_cheep_cheep(void) {
  set_small_box();
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ram[(RANDOM + x) & 0x7ff] & 0x10;
  ram[ENEMY_Y_FORCE + x] = ram[(uint8_t)(ENEMY_Y + x)];
  return;
}

// in: x; out: none
void init_lakitu(void) {
  if (ram[FRENZY_ENEMY] != 0) { erase_enemy(); return; }
  setup_lakitu(); return;
}

// in: x; out: none
void setup_lakitu(void) {
  ram[LAKITU_RETURN_TIMER] = 0;
  init_flyer_or_swimmer();
  set_tall_box_b(); return;
}

// in: x; out: x
void update_lakitu_spawner(void) {
  if (ram[FRENZY_TIMER] != 0) {
    return;
  }
  if (x >= 5) {
    return;
  }
  ram[FRENZY_TIMER] = 0x80;
  y = 4;
  do {
    if (ram[ENEMY_KIND + y] == 0x11) { goto loc_c3e6; }
    y--;
  } while ((y & 0x80) == 0);
  ram[LAKITU_RETURN_TIMER]++;
  if (ram[LAKITU_RETURN_TIMER] < 7) {
    return;
  }
  x = 4;
  do {
    if (ram[(uint8_t)(ENEMY_ACTIVE + x)] == 0) { goto loc_c3d3; }
    x--;
  } while ((x & 0x80) == 0);
  goto loc_c3e3;

loc_c3d3:
  ram[(uint8_t)(ENEMY_STATE + x)] = 0;
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x11;
  setup_lakitu();
  a = 0x20;
  place_flame();

loc_c3e3:
  x = ram[SLOT];
  return;

loc_c3e6:
  if (ram[PLAYER_Y] < 0x2c) {
    return;
  }
  if (ram[ENEMY_STATE + y] != 0) {
    return;
  }
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[ENEMY_PAGE + y];
  ram[(uint8_t)(ENEMY_X + x)] = ram[ENEMY_X + y];
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
  ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(ram[ENEMY_Y + y] - 8);
  y = ram[(RANDOM + x) & 0x7ff] & 3;
  x = 2;
  do {
    ram[(uint8_t)(1 + x)] = ROM(LAKITU_AIM_OFFSETS + y);
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x--;
  } while ((x & 0x80) == 0);
  x = ram[SLOT];
  lakitu_chase();
  y = ram[PLAYER_X_SPEED];
  if (y < 8) {
    y = a;
    if ((ram[(RANDOM + 1 + x) & 0x7ff] & 3) != 0) {
      y = (uint8_t)((y ^ 0xff) + 1);
    }
  }
  set_small_box();
  y = 2;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  if ((a & 0x80) == 0) {
    y = 1;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfd;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
  ram[(uint8_t)(ENEMY_STATE + x)] = 5;
  return;
}

// in: x; out: none
void init_long_firebar(void) {
  copy_enemy();
  init_firebar(); return;
}

// in: x; out: none
void init_firebar(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0;
  y = (uint8_t)(ram[(uint8_t)(ENEMY_KIND + x)] - 0x1b);
  ram[FIREBAR_SPEED + x] = ROM(FIREBAR_SPEEDS + y);
  ram[(uint8_t)(TARGET_PAGE + x)] = ROM(FIREBAR_DIRECTIONS + y);
  ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + 4);
  uint16_t sum2 = (uint16_t)(ram[(uint8_t)(ENEMY_X + x)] + 4);
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum2;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] + (sum2 >= 0x100));
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  set_tall_box_b(); return;
}

// in: x; out: none
void init_flying_fish(void) {
  if (ram[FRENZY_TIMER] != 0) {
    return;
  }
  set_small_box();
  a = ROM(FLYING_FISH_INTERVALS + (ram[(RANDOM + 1 + x) & 0x7ff] & 3));
  ram[FRENZY_TIMER] = a;
  y = 3;
  if (ram[HARD_MODE_LATE] != 0) {
    y = 4;
  }
  ram[0] = y;
  if (x >= ram[0]) {
    return;
  }
  a = ram[(RANDOM + x) & 0x7ff] & 3;
  ram[0] = a;
  ram[1] = a;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfb;
  a = 0;
  y = ram[PLAYER_X_SPEED];
  if (y != 0) {
    a = 4;
    if (y >= 0x19) {
      a = 8;
    }
  }
  ram[0x100 + sp] = a;
  sp--;
  ram[0] = (uint8_t)(a + ram[0]);
  if ((ram[(RANDOM + 1 + x) & 0x7ff] & 3) != 0) {
    ram[0] = ram[(RANDOM + 2 + x) & 0x7ff] & 0xf;
  }
  sp++;
  y = (uint8_t)(ram[0x100 + sp] + ram[1]);
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(FLYING_FISH_SPEEDS + y);
  ram[(uint8_t)(ENEMY_DIR + x)] = 1;
  if (ram[PLAYER_X_SPEED] == 0) {
    y = ram[0];
    if ((y & 2) != 0) {
      a = (uint8_t)((ram[(uint8_t)(ENEMY_X_SPEED + x)] ^ 0xff) + 1);
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
      ram[(uint8_t)(ENEMY_DIR + x)]++;
    }
  }
  if ((y & 2) != 0) {
    uint16_t sum4 = (uint16_t)(ram[PLAYER_X] + ROM(FLYING_FISH_X_OFFSETS + y));
    ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum4;
    a = (uint8_t)(ram[PLAYER_PAGE] + (sum4 >= 0x100));
  } else {
    int diff = ram[PLAYER_X] - ROM(FLYING_FISH_X_OFFSETS + y);
    ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)diff;
    a = (uint8_t)(ram[PLAYER_PAGE] - (diff < 0));
  }
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
  ram[(uint8_t)(ENEMY_Y + x)] = 0xf8;
  return;
}

// in: x; out: none
void init_bowser(void) {
  copy_enemy();
  ram[BOWSER_SLOT] = x;
  ram[BOWSER_BODY] = 0;
  ram[BRIDGE_COLLAPSE_POS] = 0;
  ram[BOWSER_HOME_X] = ram[(uint8_t)(ENEMY_X + x)];
  ram[BOWSER_BREATH_TIMER] = 0xdf;
  ram[(uint8_t)(ENEMY_DIR + x)] = 0xdf;
  ram[BOWSER_FEET] = 0x20;
  ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0x20;
  ram[BOWSER_HEALTH] = 5;
  ram[BOWSER_SPEED] = 2;
  return;
}

// in: x; out: none
void copy_enemy(void) {
  y = 0xff;
  do {
    y++;
  } while (ram[ENEMY_ACTIVE + y] != 0);
  ram[COPY_SLOT] = y;
  ram[ENEMY_ACTIVE + y] = x | 0x80;
  ram[ENEMY_PAGE + y] = ram[(uint8_t)(ENEMY_PAGE + x)];
  ram[ENEMY_X + y] = ram[(uint8_t)(ENEMY_X + x)];
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
  ram[ENEMY_Y_PAGE + y] = 1;
  ram[ENEMY_Y + y] = ram[(uint8_t)(ENEMY_Y + x)];
  return;
}

// in: x; out: none
void init_bowser_flame(void) {
  a = ram[FRENZY_TIMER];
  if (a != 0) {
    return;
  }
  ram[ENEMY_Y_FORCE + x] = a;
  ram[NOISE_SFX_REQUEST] |= 2;
  y = ram[BOWSER_SLOT];
  if (ram[ENEMY_KIND + y] != 0x2d) {
    next_flame_timer();
    a = (uint8_t)(a + 0x20);
    if (ram[HARD_MODE_LATE] != 0) {
      a = (uint8_t)(a - 0x10);
    }
    ram[FRENZY_TIMER] = a;
    a = ram[(RANDOM + x) & 0x7ff] & 3;
    ram[ENEMY_Y_FRACTION + x] = a;
    a = ROM(FLAME_HEIGHTS + a);
    place_flame(); return;
  }
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)(ram[ENEMY_X + y] - 0xe);
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[ENEMY_PAGE + y];
  ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(ram[ENEMY_Y + y] + 8);
  a = ram[(RANDOM + x) & 0x7ff] & 3;
  ram[ENEMY_Y_FRACTION + x] = a;
  y = 0;
  if (ROM(FLAME_HEIGHTS + a) >= ram[(uint8_t)(ENEMY_Y + x)]) {
    y = 1;
  }
  ram[ENEMY_Y_FORCE + x] = ROM(FLAME_FORCE_OFFSETS + y);
  ram[FRENZY_ENEMY] = 0;
  finish_flame(); return;
}

// in: a, x; out: a
void place_flame(void) {
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  uint16_t sum = (uint16_t)(ram[SCREEN_RIGHT_X] + 0x20);
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum;
  ram[(uint8_t)(ENEMY_PAGE + x)] = (uint8_t)(ram[SCREEN_RIGHT_PAGE] + (sum >= 0x100));
  finish_flame(); return;
}

// in: x; out: a
void finish_flame(void) {
  ram[ENEMY_BOX_KIND + x] = 8;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
  a = 0;
  ram[ENEMY_X_FORCE + x] = 0;
  ram[(uint8_t)(ENEMY_STATE + x)] = 0;
  return;
}

// in: x; out: none
void init_firework(void) {
  if (ram[FRENZY_TIMER] != 0) {
    return;
  }
  ram[FRENZY_TIMER] = 0x20;
  ram[FIREWORKS_COUNT]--;
  y = 6;
  do {
    y--;
  } while (ram[ENEMY_KIND + y] != 0x31);
  int diff = ram[ENEMY_X + y] - 0x30;
  ram[0x100 + sp] = (uint8_t)diff;
  ram[0] = (uint8_t)(ram[ENEMY_PAGE + y] - (diff < 0));
  y = (uint8_t)(ram[FIREWORKS_COUNT] + ram[ENEMY_STATE + y]);
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  uint16_t sum2 = (uint16_t)(ram[0x100 + sp] + ROM(FIREWORK_X_OFFSETS + y));
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum2;
  ram[(uint8_t)(ENEMY_PAGE + x)] = (uint8_t)(ram[0] + (sum2 >= 0x100));
  ram[(uint8_t)(ENEMY_Y + x)] = ROM(FIREWORK_Y_OFFSETS + y);
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 1;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 1;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 8;
  return;
}

// in: x; out: x
void spawn_frenzy_enemy(void) {
  if (ram[FRENZY_TIMER] != 0) {
    return;
  }
  if (ram[AREA_KIND] != 0) { goto loc_c6fd; }
  if (x >= 3) {
    return;
  }
  y = 0;
  if (ram[(RANDOM + x) & 0x7ff] >= 0xaa) {
    y = 1;
  }
  if (ram[WORLD_NUMBER] != 1) {
    y++;
  }
  a = ROM(SWIMMING_FISH_KINDS + (y & 1));

loc_c6c3:
  ram[(uint8_t)(ENEMY_KIND + x)] = a;
  if (ram[MOVE_BIT_FILTER] == 0xff) {
    ram[MOVE_BIT_FILTER] = 0;
  }
  a = ram[(RANDOM + x) & 0x7ff] & 7;

loc_c6d6:
  y = a;
  a = ROM(BIT_MASKS + y);
  if ((a & ram[MOVE_BIT_FILTER]) != 0) {
    a = (uint8_t)(y + 1) & 7;
    goto loc_c6d6;
  }
  ram[MOVE_BIT_FILTER] = a | ram[MOVE_BIT_FILTER];
  a = ROM(FRENZY_Y_POSITIONS + y);
  place_flame();
  ram[ENEMY_Y_FRACTION + x] = a;
  ram[FRENZY_TIMER] = 0x20;
  init_enemy(); return;

loc_c6fd:
  y = 0xff;
  do {

loc_c6ff:
    y++;
    if (y >= 5) { goto loc_c711; }
    if (ram[ENEMY_ACTIVE + y] == 0) { goto loc_c6ff; }
  } while (ram[ENEMY_KIND + y] != 8);
  return;

loc_c711:
  ram[PULSE2_SFX_REQUEST] |= 8;
  a = 8;
  goto loc_c6c3;
}

// in: x; out: none
void init_piranha(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 1;
  ram[(uint8_t)(ENEMY_STATE + x)] = 0;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  ram[ENEMY_Y_FORCE + x] = a;
  ram[ENEMY_Y_FRACTION + x] = (uint8_t)(a - 0x18);
  a = 9;
  set_box_kind(); return;
}

// in: x; out: x
void init_frenzy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  ram[FRENZY_ENEMY] = a;
  a = (uint8_t)(a - 0x12);
  switch (dispatch(0xc7aa)) {
    case 0xc3a4: update_lakitu_spawner(); return;
    case 0xc7b7: init_no_frenzy(); return;
    case 0xc4a8: init_flying_fish(); return;
    case 0xc5a3: init_bowser_flame(); return;
    case 0xc63d: init_firework(); return;
    case 0xc69c: spawn_frenzy_enemy(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void init_no_frenzy(void) {
  return;
}

// in: x; out: none
void end_frenzy(void) {
  y = 5;
  do {
    if (ram[ENEMY_KIND + y] == 0x11) {
      ram[ENEMY_STATE + y] = 1;
    }
    y--;
  } while ((y & 0x80) == 0);
  ram[FRENZY_ENEMY] = 0;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0;
  return;
}

// in: x; out: none
void init_bouncing_paratroopa(void) {
  ram[(uint8_t)(ENEMY_DIR + x)] = 2;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0xf8;
  set_tall_box_b(); return;
}

// in: x; out: none
void set_tall_box_b(void) {
  a = 3;
  set_box_kind(); return;
}

// in: a, x; out: none
void set_box_kind(void) {
  ram[ENEMY_BOX_KIND + x] = a;
  return;
}

// in: x; out: none
void init_balance_platform(void) {
  ram[(uint8_t)(ENEMY_Y + x)]--;
  ram[(uint8_t)(ENEMY_Y + x)]--;
  if (ram[HARD_MODE_LATE] == 0) {
    y = 2;
    offset_platform();
  }
  y = 0xff;
  a = ram[BALANCE_ALIGNMENT];
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  if ((a & 0x80) != 0) {
    y = x;
  }
  ram[BALANCE_ALIGNMENT] = y;
  ram[(uint8_t)(ENEMY_DIR + x)] = 0;
  y = 0;
  offset_platform();
  init_drop_platform(); return;
}

// in: x; out: none
void init_drop_platform(void) {
  ram[PLATFORM_CONTACT + x] = 0xff;
  init_platform(); return;
}

// in: x; out: none
void init_side_platform(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0;
  init_platform(); return;
}

// in: x; out: none
void init_vertical_platform(void) {
  y = 0x40;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  if ((a & 0x80) != 0) {
    a = (uint8_t)((a ^ 0xff) + 1);
    y = 0xc0;
  }
  ram[ENEMY_X_FORCE + x] = a;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = (uint8_t)(y + ram[(uint8_t)(ENEMY_Y + x)]);
  init_platform(); return;
}

// in: x; out: none
void init_platform(void) {
  clear_vertical_motion();
  set_platform_box(); return;
}

// in: x; out: none
void set_platform_box(void) {
  a = 5;
  if (ram[AREA_KIND] != 3) {
    if (ram[HARD_MODE_LATE] == 0) {
      a = 6;
    }
  }
  ram[ENEMY_BOX_KIND + x] = a;
  return;
}

// in: x; out: none
void large_lift_up(void) {
  small_lift_up();
  large_lift_box(); return;
}

// in: x; out: none
void large_lift_down(void) {
  small_lift_down();
  large_lift_box(); return;
}

// in: x; out: none
void large_lift_box(void) {
  set_platform_box(); return;
}

// in: x; out: none
void small_lift_up(void) {
  ram[ENEMY_Y_FORCE + x] = 0x10;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xff;
  small_lift_common(); return;
}

// in: x; out: none
void small_lift_down(void) {
  ram[ENEMY_Y_FORCE + x] = 0xf0;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0;
  small_lift_common(); return;
}

// in: x; out: none
void small_lift_common(void) {
  y = 1;
  offset_platform();
  ram[ENEMY_BOX_KIND + x] = 4;
  return;
}

// in: x, y; out: none
void offset_platform(void) {
  uint16_t sum = (uint16_t)(ram[(uint8_t)(ENEMY_X + x)] + ROM(PLATFORM_OFFSET_LOW + y));
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum;
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  a = (uint8_t)(a + ROM(PLATFORM_OFFSET_HIGH + y) + (sum >= 0x100));
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  return;
}

void finish_enemy_init(void) {
  return;
}

void run_nothing(void) {
  return;
}

// in: x; out: x
void run_retainer(void) {
  enemy_offscreen_bits();
  enemy_screen_position();
  draw_enemy(); return;
}

// in: x; out: x
void run_normal_enemy(void) {
  ram[ENEMY_SPRITE_ATTR + x] = 0;
  enemy_offscreen_bits();
  enemy_screen_position();
  draw_enemy();
  enemy_box();
  enemy_bg_collision();
  enemy_enemy_collisions();
  player_enemy_collision();
  if (ram[TIMERS_PAUSED] != 0) {
    check_enemy_offscreen(); return;
  }
  move_enemy();
  check_enemy_offscreen(); return;
}

// in: x; out: x
void move_enemy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  switch (dispatch(0xc909)) {
    case 0xca77: move_walker(); return;
    case 0xc9d8: update_hammer_bro(); return;
    case 0xcb89: move_blooper(); return;
    case 0xcc36: move_bullet(); return;
    case 0xc934: move_nothing(); return;
    case 0xcc4a: move_swimming_fish(); return;
    case 0xc9b0: move_podoboo(); return;
    case 0xd3b0: move_piranha(); return;
    case 0xcaf9: move_hopping_paratroopa(); return;
    case 0xcaff: move_red_paratroopa(); return;
    case 0xcb25: move_green_paratroopa(); return;
    case 0xcf28: move_lakitu(); return;
    case 0xcedf: move_flying_fish(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void move_nothing(void) {
  return;
}

// in: x; out: x
void run_bowser_flame(void) {
  update_bowser_flame();
  enemy_offscreen_bits();
  enemy_screen_position();
  enemy_box();
  player_enemy_collision();
  check_enemy_offscreen(); return;
}

// in: x; out: x
void run_firebar(void) {
  update_firebar();
  check_enemy_offscreen(); return;
}

// in: x; out: x
void run_small_platform(void) {
  enemy_offscreen_bits();
  enemy_screen_position();
  small_platform_box();
  small_platform_collision();
  enemy_screen_position();
  draw_small_platform();
  move_small_lift();
  check_enemy_offscreen(); return;
}

// in: x; out: x
void run_large_platform(void) {
  enemy_offscreen_bits();
  enemy_screen_position();
  large_platform_box();
  large_platform_collision();
  if (ram[TIMERS_PAUSED] == 0) {
    move_large_platform();
  }
  enemy_screen_position();
  draw_large_platform();
  check_enemy_offscreen(); return;
}

// in: x; out: x
void move_large_platform(void) {
  a = (uint8_t)(ram[(uint8_t)(ENEMY_KIND + x)] - 0x24);
  switch (dispatch(0xc989)) {
    case 0xd432: update_balance_platform(); return;
    case 0xd5d3: move_vertical_platform(); return;
    case 0xd64f: move_large_lift(); return;
    case 0xd607: move_side_platform(); return;
    case 0xd631: drop_platform(); return;
    case 0xd63d: right_platform(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: a
void erase_enemy(void) {
  a = 0;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0;
  ram[(uint8_t)(ENEMY_KIND + x)] = 0;
  ram[(uint8_t)(ENEMY_STATE + x)] = 0;
  ram[POPUP_KIND + x] = 0;
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = 0;
  ram[SHELL_CHAIN + x] = 0;
  ram[ENEMY_SPRITE_ATTR + x] = 0;
  ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0;
  return;
}

// in: x; out: x
void move_podoboo(void) {
  if (ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] != 0) {
    move_enemy_jumping(); return;
  }
  init_podoboo();
  a = ram[(RANDOM + 1 + x) & 0x7ff] | 0x80;
  ram[ENEMY_Y_FORCE + x] = a;
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = (a & 0xf) | 6;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xf9;
  move_enemy_jumping(); return;
}

// in: x; out: x
void update_hammer_bro(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    move_defeated_enemy(); return;
  }
  if (ram[(uint8_t)(HAMMER_JUMP_TIMER + x)] != 0) {
    ram[(uint8_t)(HAMMER_JUMP_TIMER + x)]--;
    if ((ram[ENEMY_OFFSCREEN] & 0xc) != 0) {
      hammer_bro_walk(); return;
    }
    if (ram[PLATFORM_CONTACT + x] == 0) {
      ram[PLATFORM_CONTACT + x] = ROM(HAMMER_THROW_MASKS + ram[HARD_MODE_LATE]);
      spawn_hammer();
      if (carry_flag) {
        ram[(uint8_t)(ENEMY_STATE + x)] |= 8;
        hammer_bro_walk(); return;
      }
    }
    ram[PLATFORM_CONTACT + x]--;
    hammer_bro_walk(); return;
  }
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 7) == 1) {
    hammer_bro_walk(); return;
  }
  ram[0] = 0;
  y = 0xfa;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  if ((a & 0x80) != 0) {
    hammer_bro_jump(); return;
  }
  y = 0xfd;
  ram[0]++;
  if (a < 0x70) {
    hammer_bro_jump(); return;
  }
  ram[0]--;
  if ((ram[(RANDOM + 1 + x) & 0x7ff] & 1) != 0) {
    hammer_bro_jump(); return;
  }
  y = 0xfa;
  hammer_bro_jump(); return;
}

// in: x, y; out: x, y
void hammer_bro_jump(void) {
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = y;
  ram[(uint8_t)(ENEMY_STATE + x)] |= 1;
  y = ram[0] & ram[(RANDOM + 2 + x) & 0x7ff];
  a = ram[HARD_MODE_LATE];
  if (a == 0) {
    y = a;
  }
  ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = ROM(HAMMER_BRO_JUMP_LENGTHS + y);
  a = ram[(RANDOM + 1 + x) & 0x7ff] | 0xc0;
  ram[(uint8_t)(HAMMER_JUMP_TIMER + x)] = a;
  hammer_bro_walk(); return;
}

// in: x; out: x, y
void hammer_bro_walk(void) {
  y = 0xfc;
  if ((ram[FRAME_COUNT] & 0x40) == 0) {
    y = 4;
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = y;
  y = 1;
  player_enemy_distance();
  if (!neg_flag) {
    y = 2;
    if (ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] == 0) {
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0xf8;
    }
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  move_walker(); return;
}

// in: x; out: x, y
void move_walker(void) {
  y = 0;
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x40) == 0) {
    if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x80) != 0) { goto loc_cab4; }
    if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
      move_defeated_enemy(); return;
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)] & 7;
    if (a == 0) { goto loc_cab4; }
    if (a != 5) {
      if (a >= 3) { goto loc_cac8; }
    }
  }
  move_enemy_down_fast();
  y = 0;
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  if (a == 2) {
    move_enemy_x(); return;
  }
  if ((a & 0x40) != 0) {
    if (ram[(uint8_t)(ENEMY_KIND + x)] != 0x2e) {
      y = 1;
    }
  }

loc_cab4:
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 0x80) != 0) {
    y = (uint8_t)((uint8_t)(y + 1) + 1);
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = (uint8_t)(a + ROM(WALK_SPEED_ADDERS + y));
  move_enemy_x();
  sp++;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ram[0x100 + sp];
  return;

loc_cac8:
  a = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff];
  if (a == 0) {
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    y = (uint8_t)((ram[FRAME_COUNT] & 1) + 1);
    ram[(uint8_t)(ENEMY_DIR + x)] = y;
    y--;
    if (ram[HARD_MODE] != 0) {
      y = (uint8_t)((uint8_t)(y + 1) + 1);
    }
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(REVIVED_SPEEDS + y);
    return;
  }
  if (a != 0xe) {
    return;
  }
  if (ram[(uint8_t)(ENEMY_KIND + x)] != 6) {
    return;
  }
  erase_enemy();
  return;
}

// in: x; out: x, y
void move_defeated_enemy(void) {
  move_enemy_down_fast();
  move_enemy_x(); return;
}

// in: x; out: x
void move_hopping_paratroopa(void) {
  move_enemy_jumping();
  move_enemy_x(); return;
}

// in: x; out: x
void move_red_paratroopa(void) {
  goto move_red_paratroopa;
  do {
    y = 0;
    goto loc_bf77;

loc_bf75:
    y = 1;

loc_bf77:
    x++;
    ram[0] = 3;
    ram[1] = 6;
    ram[2] = 2;
    a = y;
    move_paratroopa_vertically(); return;

move_red_paratroopa:
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)] | ram[ENEMY_Y_FORCE + x];
    if (a == 0) {
      ram[ENEMY_Y_FRACTION + x] = a;
      if (ram[(uint8_t)(ENEMY_Y + x)] < ram[ENEMY_X_FORCE + x]) {
        if ((ram[FRAME_COUNT] & 7) != 0) {
          return;
        }
        ram[(uint8_t)(ENEMY_Y + x)]++;
        return;
      }
    }
  } while (ram[(uint8_t)(ENEMY_Y + x)] < ram[(uint8_t)(ENEMY_X_SPEED + x)]);
  goto loc_bf75;
}

// in: x; out: x
void move_green_paratroopa(void) {
  sway_green_paratroopa();
  sway_offset();
  y = 1;
  if ((ram[FRAME_COUNT] & 3) != 0) {
    return;
  }
  if ((ram[FRAME_COUNT] & 0x40) == 0) {
    y = 0xff;
  }
  ram[0] = y;
  ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + ram[0]);
  return;
}

// in: x; out: none
void sway_green_paratroopa(void) {
  a = 0x13;
  sway_counters(); return;
}

// oscillating primary/secondary counters for back-and-forth motion
// in: a, x; out: none
void sway_counters(void) {
  ram[1] = a;
  if ((ram[FRAME_COUNT] & 3) != 0) {
    return;
  }
  y = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  if ((ram[(uint8_t)(ENEMY_Y_SPEED + x)] & 1) != 0) { goto loc_cb60; }
  if (y != ram[1]) {
    ram[(uint8_t)(ENEMY_X_SPEED + x)]++;
    return;
  }

loc_cb5d:
  ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
  return;

loc_cb60:
  if (y == 0) { goto loc_cb5d; }
  ram[(uint8_t)(ENEMY_X_SPEED + x)]--;
  return;
}

// in: x; out: x
void sway_offset(void) {
  ram[0x100 + sp] = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  sp--;
  y = 1;
  if ((ram[(uint8_t)(ENEMY_Y_SPEED + x)] & 2) == 0) {
    a = (uint8_t)((ram[(uint8_t)(ENEMY_X_SPEED + x)] ^ 0xff) + 1);
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    y = 2;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  move_enemy_x();
  ram[0] = a;
  sp++;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ram[0x100 + sp];
  return;
}

// in: x; out: x
void move_blooper(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    move_enemy_down_slow(); return;
  }
  a = ram[(RANDOM + 1 + x) & 0x7ff];
  if ((a & ROM(BLOOPER_MASKS + ram[HARD_MODE_LATE])) == 0) {
    carry_flag = (x & 1) != 0;
    if (carry_flag) {
      y = ram[PLAYER_DIR];
    } else {
      y = 2;
      player_enemy_distance();
      if (neg_flag) {
        y = 1;
      }
    }
    ram[(uint8_t)(ENEMY_DIR + x)] = y;
  }
  blooper_swim();
  a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] - ram[ENEMY_Y_FORCE + x]);
  if (a >= 0x20) {
    ram[(uint8_t)(ENEMY_Y + x)] = a;
  }
  if ((uint8_t)(ram[(uint8_t)(ENEMY_DIR + x)] - 1) == 0) {
    uint16_t sum = (uint16_t)(ram[(uint8_t)(ENEMY_X + x)] + ram[(uint8_t)(ENEMY_X_SPEED + x)]);
    ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)sum;
    a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] + (sum >= 0x100));
    ram[(uint8_t)(ENEMY_PAGE + x)] = a;
    return;
  }
  int diff2 = ram[(uint8_t)(ENEMY_X + x)] - ram[(uint8_t)(ENEMY_X_SPEED + x)];
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)diff2;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] - (diff2 < 0));
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  return;
}

// in: x; out: none
void blooper_swim(void) {
  if ((ram[(uint8_t)(ENEMY_Y_SPEED + x)] & 2) == 0) {
    ram[0x100 + sp] = ram[FRAME_COUNT] & 7;
    sp--;
    if ((ram[(uint8_t)(ENEMY_Y_SPEED + x)] & 1) == 0) {
      sp++;
      if (ram[0x100 + sp] != 0) {
        return;
      }
      a = (uint8_t)(ram[ENEMY_Y_FORCE + x] + 1);
      ram[ENEMY_Y_FORCE + x] = a;
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
      if (a != 2) {
        return;
      }
      ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
      return;
    }
    sp++;
    if (ram[0x100 + sp] != 0) {
      return;
    }
    a = (uint8_t)(ram[ENEMY_Y_FORCE + x] - 1);
    ram[ENEMY_Y_FORCE + x] = a;
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    if (a != 0) {
      return;
    }
    ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
    ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = 2;
    return;
  }
  if (ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] != 0) {

loc_cc21:
    if ((ram[FRAME_COUNT] & 1) != 0) {
      return;
    }
    ram[(uint8_t)(ENEMY_Y + x)]++;
    return;
  }
  carry_flag = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + 0x10 + carry_flag) >= ram[PLAYER_Y];
  if (!carry_flag) { goto loc_cc21; }
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0;
  return;
}

// in: x; out: x
void move_bullet(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    move_enemy_jumping(); return;
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0xe8;
  move_enemy_x(); return;
}

// in: x; out: x
void move_swimming_fish(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)] & 0x20;
  if (a != 0) {
    move_enemy_down_slow(); return;
  }
  ram[3] = a;
  ram[2] = ROM(SWIMMING_FISH_SPEEDS + (uint8_t)(ram[(uint8_t)(ENEMY_KIND + x)] - 0xa));
  int diff2 = ram[ENEMY_X_FORCE + x] - ram[2];
  ram[ENEMY_X_FORCE + x] = (uint8_t)diff2;
  int diff3 = ram[(uint8_t)(ENEMY_X + x)] - (diff2 < 0);
  ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)diff3;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] - (diff3 < 0));
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  ram[2] = 0x20;
  if (x < 2) {
    return;
  }
  if (ram[(uint8_t)(ENEMY_X_SPEED + x)] >= 0x10) {
    uint16_t sum = (uint16_t)(ram[ENEMY_Y_FRACTION + x] + ram[2]);
    ram[ENEMY_Y_FRACTION + x] = (uint8_t)sum;
    uint16_t sum2 = (uint16_t)(ram[(uint8_t)(ENEMY_Y + x)] + ram[3] + (sum >= 0x100));
    ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)sum2;
    a = (uint8_t)(ram[(uint8_t)(ENEMY_Y_PAGE + x)] + (sum2 >= 0x100));
  } else {
    int diff5 = ram[ENEMY_Y_FRACTION + x] - ram[2];
    ram[ENEMY_Y_FRACTION + x] = (uint8_t)diff5;
    int diff6 = ram[(uint8_t)(ENEMY_Y + x)] - ram[3] - (diff5 < 0);
    ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)diff6;
    a = (uint8_t)(ram[(uint8_t)(ENEMY_Y_PAGE + x)] - (diff6 < 0));
  }
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
  y = 0;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] - ram[ENEMY_Y_FORCE + x]);
  if ((a & 0x80) != 0) {
    y = 0x10;
    a = (uint8_t)((a ^ 0xff) + 1);
  }
  if (a < 0xf) {
    return;
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = y;
  return;
}

// in: x; out: x
void update_firebar(void) {
  enemy_offscreen_bits();
  if ((ram[ENEMY_OFFSCREEN] & 8) != 0) {
    return;
  }
  if (ram[TIMERS_PAUSED] == 0) {
    a = ram[FIREBAR_SPEED + x];
    spin_firebar();
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a & 0x1f;
  }
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  if (ram[(uint8_t)(ENEMY_KIND + x)] >= 0x1f) {
    if (a != 8) {
      if (a != 0x18) { goto loc_cd6a; }
    }
    a++;
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  }

loc_cd6a:
  ram[ENEMY_DATA_PTR + 6] = a;
  enemy_screen_position();
  firebar_ball_position();
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_SCREEN_Y];
  ram[SPRITE_Y + y] = a;
  ram[7] = a;
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[6] = a;
  ram[0] = 1;
  firebar_ball_collision();
  y = 5;
  if (ram[(uint8_t)(ENEMY_KIND + x)] >= 0x1f) {
    y = 0xb;
  }
  ram[ENEMY_DATA_PTR + 4] = y;
  ram[0] = 0;
  do {
    a = ram[ENEMY_DATA_PTR + 6];
    firebar_ball_position();
    draw_firebar_ball();
    if (ram[0] == 4) {
      ram[6] = ram[ENEMY_SPRITE_SLOT + ram[COPY_SLOT]];
    }
    ram[0]++;
  } while (ram[0] < ram[ENEMY_DATA_PTR + 4]);
  return;
}

// in: none; out: x
void draw_firebar_ball(void) {
  ram[5] = ram[3];
  y = ram[6];
  a = ram[1];
  carry_flag = (ram[5] & 1) != 0;
  ram[5] = ram[5] >> 1;
  if (!carry_flag) {
    a = (uint8_t)((a ^ 0xff) + 1 + carry_flag);
  }
  a = (uint8_t)(a + ram[ENEMY_SCREEN_X]);
  ram[SPRITE_X + y] = a;
  ram[6] = a;
  if (a < ram[ENEMY_SCREEN_X]) {
    a = (uint8_t)(ram[ENEMY_SCREEN_X] - ram[6]);
  } else {
    a = (uint8_t)(a - ram[ENEMY_SCREEN_X]);
  }
  if (a >= 0x59) {
    a = 0xf8;
  } else {
    a = ram[ENEMY_SCREEN_Y];
    if (a != 0xf8) {
      a = ram[2];
      carry_flag = (ram[5] & 1) != 0;
      ram[5] = ram[5] >> 1;
      if (!carry_flag) {
        a = (uint8_t)((a ^ 0xff) + 1 + carry_flag);
      }
      a = (uint8_t)(a + ram[ENEMY_SCREEN_Y]);
    }
  }
  ram[SPRITE_Y + y] = a;
  ram[7] = a;
  firebar_ball_collision(); return;
}

// in: y; out: x
void firebar_ball_collision(void) {
  draw_firebar_tile();
  ram[0x100 + sp] = y;
  sp--;
  a = ram[STAR_TIMER] | ram[TIMERS_PAUSED];
  if (a == 0) {
    ram[5] = a;
    if ((uint8_t)(ram[PLAYER_Y_PAGE] - 1) == 0) {
      y = ram[PLAYER_Y];
      if (ram[PLAYER_SMALL] == 0) {
        if (ram[CROUCHING] == 0) { goto loc_ce31; }
      }
      ram[5]++;
      ram[5]++;
      y = (uint8_t)(y + 0x18);

loc_ce31:
      a = y;

loc_ce32:
      a = (uint8_t)(a - ram[7]);
      if ((a & 0x80) != 0) {
        a = (uint8_t)((a ^ 0xff) + 1);
      }
      if (a < 8) {
        if (ram[6] < 0xf0) {
          a = (uint8_t)(ram[SPRITE_X + 4] + 4);
          ram[4] = a;
          a = (uint8_t)(a - ram[6]);
          if ((a & 0x80) != 0) {
            a = (uint8_t)((a ^ 0xff) + 1);
          }
          if (a < 8) { goto loc_ce6f; }
        }
      }
      if (ram[5] == 2) { goto loc_ce85; }
      a = (uint8_t)(ram[PLAYER_Y] + ROM(FIREBAR_Y_POSITIONS + ram[5]));
      ram[5]++;
      goto loc_ce32;

loc_ce6f:
      x = 1;
      if (ram[4] < ram[6]) {
        x = 2;
      }
      ram[ENEMY_DIR] = x;
      ram[0x100 + sp] = ram[0];
      sp--;
      injure_player();
      sp++;
      ram[0] = ram[0x100 + sp];
    }
  }

loc_ce85:
  sp++;
  ram[6] = (uint8_t)(ram[0x100 + sp] + 4);
  x = ram[SLOT];
  return;
}

// in: a; out: none
void firebar_ball_position(void) {
  ram[0x100 + sp] = a;
  sp--;
  a &= 0xf;
  if (a >= 9) {
    a = (uint8_t)((a ^ 0xf) + 1);
  }
  ram[1] = a;
  ram[1] = ROM(FIREBAR_OFFSETS + (uint8_t)(ROM(FIREBAR_TABLE_OFFSETS + ram[0]) + ram[1]));
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a;
  sp--;
  a = (uint8_t)(a + 8) & 0xf;
  if (a >= 9) {
    a = (uint8_t)((a ^ 0xf) + 1);
  }
  ram[2] = a;
  ram[2] = ROM(FIREBAR_OFFSETS + (uint8_t)(ROM(FIREBAR_TABLE_OFFSETS + ram[0]) + ram[2]));
  sp++;
  a = ROM(FIREBAR_MIRRORS + (ram[0x100 + sp] >> 1 >> 1 >> 1));
  ram[3] = a;
  return;
}

// in: x; out: x
void move_flying_fish(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    ram[ENEMY_SPRITE_ATTR + x] = 0;
    move_enemy_jumping(); return;
  }
  move_enemy_x();
  y = 0xd;
  a = 5;
  apply_gravity_enemy();
  y = ram[ENEMY_Y_FORCE + x] >> 1 >> 1 >> 1 >> 1;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] - ROM(RANDOM_Y_RANGES + y));
  if ((a & 0x80) != 0) {
    a = (uint8_t)((a ^ 0xff) + 1);
  }
  if (a < 8) {
    a = (uint8_t)(ram[ENEMY_Y_FORCE + x] + 0x10);
    ram[ENEMY_Y_FORCE + x] = a;
    y = a >> 1 >> 1 >> 1 >> 1;
  }
  ram[ENEMY_SPRITE_ATTR + x] = ROM(FLYING_FISH_PRIORITIES + y);
  return;
}

// in: x; out: x
void move_lakitu(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    move_enemy_down_fast(); return;
  }
  if (ram[(uint8_t)(ENEMY_STATE + x)] != 0) {
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0;
    ram[FRENZY_ENEMY] = 0;
    a = 0x10;
  } else {
    ram[FRENZY_ENEMY] = 0x12;
    y = 2;
    do {
      ram[1 + y] = ROM(LAKITU_SPEED_ADJUST + y);
      y--;
    } while ((y & 0x80) == 0);
    lakitu_chase();
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  y = 1;
  if ((ram[(uint8_t)(ENEMY_Y_SPEED + x)] & 1) == 0) {
    a = (uint8_t)((ram[(uint8_t)(ENEMY_X_SPEED + x)] ^ 0xff) + 1);
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    y = 2;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  move_enemy_x(); return;
}

// in: x; out: a
void lakitu_chase(void) {
  y = 0;
  player_enemy_distance();
  if (neg_flag) {
    y = 1;
    ram[0] = (uint8_t)((ram[0] ^ 0xff) + 1);
  }
  if (ram[0] >= 0x3c) {
    ram[0] = 0x3c;
    if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x11) {
      if (y != ram[(uint8_t)(ENEMY_Y_SPEED + x)]) {
        if (ram[(uint8_t)(ENEMY_Y_SPEED + x)] != 0) {
          ram[(uint8_t)(ENEMY_X_SPEED + x)]--;
          a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
          if (a != 0) {
            return;
          }
        }
        ram[(uint8_t)(ENEMY_Y_SPEED + x)] = y;
      }
    }
  }
  ram[0] = (ram[0] & 0x3c) >> 1 >> 1;
  y = 0;
  if (ram[PLAYER_X_SPEED] != 0) {
    if (ram[SCROLL_AMOUNT] != 0) {
      y = 1;
      if (ram[PLAYER_X_SPEED] >= 0x19) {
        if (ram[SCROLL_AMOUNT] >= 2) {
          y = 2;
        }
      }
      if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x12) {
        if (ram[PLAYER_X_SPEED] != 0) { goto loc_cfd1; }
      }
      if (ram[(uint8_t)(ENEMY_Y_SPEED + x)] == 0) {
        y = 0;
      }
    }
  }

loc_cfd1:
  a = ram[1 + y];
  y = ram[0];
  do {
    a--;
    y--;
  } while ((y & 0x80) == 0);
  return;
}

void collapse_bridge(void) {
  x = ram[BOWSER_SLOT];
  if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x2d) {
    ram[SLOT] = x;
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    if (a == 0) { goto loc_d015; }
    if ((a & 0x40) != 0) {
      if (ram[(uint8_t)(ENEMY_Y + x)] < 0xe0) {
        move_dying_bowser(); return;
      }
    }
  }
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  ram[GAME_MODE_TASK]++;
  kill_all_enemies(); return;

loc_d015:
  ram[BOWSER_FEET]--;
  if (ram[BOWSER_FEET] != 0) {
    draw_bowser(); return;
  }
  ram[BOWSER_FEET] = 4;
  ram[BOWSER_BODY] ^= 1;
  ram[5] = 0x22;
  ram[4] = ROM(BRIDGE_TILE_COLUMNS + ram[BRIDGE_COLLAPSE_POS]);
  y = (uint8_t)(ram[VRAM_LIST1_LENGTH] + 1);
  x = 0xc;
  queue_metatile_tiles();
  x = ram[SLOT];
  advance_vram_list();
  ram[PULSE2_SFX_REQUEST] = 8;
  ram[NOISE_SFX_REQUEST] = 1;
  ram[BRIDGE_COLLAPSE_POS]++;
  if (ram[BRIDGE_COLLAPSE_POS] != 0xf) {
    draw_bowser(); return;
  }
  clear_vertical_motion();
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x40;
  ram[PULSE2_SFX_REQUEST] = 0x80;
  draw_bowser(); return;
}

// in: x; out: x
void move_dying_bowser(void) {
  move_enemy_down_slow();
  draw_bowser(); return;
}

// in: x; out: x
void run_bowser(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    if (ram[(uint8_t)(ENEMY_Y + x)] < 0xe0) { move_dying_bowser(); return; }
    kill_all_enemies(); return;
  }
  ram[FRENZY_ENEMY] = 0;
  if (ram[TIMERS_PAUSED] == 0) {
    if ((ram[BOWSER_BODY] & 0x80) == 0) {
      ram[BOWSER_FEET]--;
      if (ram[BOWSER_FEET] == 0) {
        ram[BOWSER_FEET] = 0x20;
        ram[BOWSER_BODY] ^= 1;
      }
      if ((ram[FRAME_COUNT] & 0xf) == 0) {
        ram[(uint8_t)(ENEMY_DIR + x)] = 2;
      }
      if (ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] != 0) {
        player_enemy_distance();
        if (neg_flag) {
          ram[(uint8_t)(ENEMY_DIR + x)] = 1;
          ram[BOWSER_SPEED] = 2;
          ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0x20;
          ram[BOWSER_BREATH_TIMER] = 0x20;
          if (ram[(uint8_t)(ENEMY_X + x)] >= 0xc8) { goto loc_d10f; }
        }
      }
      if ((ram[FRAME_COUNT] & 3) == 0) {
        if (ram[(uint8_t)(ENEMY_X + x)] == ram[BOWSER_HOME_X]) {
          ram[MAX_RANGE] = ROM(BOWSER_MOVE_RANGES + (ram[(RANDOM + x) & 0x7ff] & 3));
        }
        a = (uint8_t)(ram[(uint8_t)(ENEMY_X + x)] + ram[BOWSER_SPEED]);
        ram[(uint8_t)(ENEMY_X + x)] = a;
        y = ram[(uint8_t)(ENEMY_DIR + x)];
        if (y != 1) {
          y = 0xff;
          a = (uint8_t)(a - ram[BOWSER_HOME_X]);
          if ((a & 0x80) != 0) {
            a = (uint8_t)((a ^ 0xff) + 1);
            y = 1;
          }
          if (a >= ram[MAX_RANGE]) {
            ram[BOWSER_SPEED] = y;
          }
        }
      }
    }

loc_d10f:
    a = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff];
    if (a == 0) {
      move_enemy_down_slow();
      if (ram[WORLD_NUMBER] >= 5) {
        if ((ram[FRAME_COUNT] & 3) == 0) {
          spawn_hammer();
        }
      }
      if (ram[(uint8_t)(ENEMY_Y + x)] >= 0x80) {
        a = ROM(BOWSER_MOVE_RANGES + (ram[(RANDOM + x) & 0x7ff] & 3));
        ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = a;
      }
    } else {
      if (a == 1) {
        ram[(uint8_t)(ENEMY_Y + x)]--;
        clear_vertical_motion();
        ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfe;
      }
    }
  }
  do {
    a = ram[WORLD_NUMBER];
    if (a != 7) {
      if (a >= 5) {
        draw_bowser(); return;
      }
    }
    if (ram[BOWSER_BREATH_TIMER] != 0) {
      draw_bowser(); return;
    }
    ram[BOWSER_BREATH_TIMER] = 0x20;
    a = ram[BOWSER_BODY] ^ 0x80;
    ram[BOWSER_BODY] = a;
  } while ((a & 0x80) != 0);
  next_flame_timer();
  if (ram[HARD_MODE_LATE] != 0) {
    a = (uint8_t)(a - 0x10);
  }
  ram[BOWSER_BREATH_TIMER] = a;
  ram[FRENZY_ENEMY] = 0x15;
  draw_bowser(); return;
}

// in: none; out: x
void kill_all_enemies(void) {
  x = 4;
  do {
    erase_enemy();
    x--;
  } while ((x & 0x80) == 0);
  ram[FRENZY_ENEMY] = a;
  x = ram[SLOT];
  return;
}

// in: x; out: x
void draw_bowser(void) {
  bowser_half();
  y = 0x10;
  if ((ram[(uint8_t)(ENEMY_DIR + x)] & 1) != 0) {
    y = 0xf0;
  }
  a = (uint8_t)(y + ram[(uint8_t)(ENEMY_X + x)]);
  y = ram[COPY_SLOT];
  ram[ENEMY_X + y] = a;
  ram[ENEMY_Y + y] = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + 8);
  ram[ENEMY_STATE + y] = ram[(uint8_t)(ENEMY_STATE + x)];
  ram[ENEMY_DIR + y] = ram[(uint8_t)(ENEMY_DIR + x)];
  ram[0x100 + sp] = ram[SLOT];
  sp--;
  x = ram[COPY_SLOT];
  ram[SLOT] = x;
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x2d;
  bowser_half();
  sp++;
  a = ram[0x100 + sp];
  ram[SLOT] = a;
  x = a;
  ram[BOWSER_GFX_FLAG] = 0;
  return;
}

// in: x; out: x
void bowser_half(void) {
  ram[BOWSER_GFX_FLAG]++;
  run_retainer();
  if (ram[(uint8_t)(ENEMY_STATE + x)] != 0) {
    return;
  }
  ram[ENEMY_BOX_KIND + x] = 0xa;
  enemy_box();
  player_enemy_collision(); return;
}

// in: none; out: a
void next_flame_timer(void) {
  y = ram[BOWSER_FLAME_TIMER];
  ram[BOWSER_FLAME_TIMER]++;
  ram[BOWSER_FLAME_TIMER] &= 7;
  a = ROM(FLAME_TIMERS + y);
  return;
}

// in: x; out: x
void update_bowser_flame(void) {
  if (ram[TIMERS_PAUSED] == 0) {
    a = 0x40;
    if (ram[HARD_MODE_LATE] != 0) {
      a = 0x60;
    }
    ram[0] = a;
    int diff = ram[ENEMY_X_FORCE + x] - ram[0];
    ram[ENEMY_X_FORCE + x] = (uint8_t)diff;
    int diff2 = ram[(uint8_t)(ENEMY_X + x)] - 1 - (diff < 0);
    ram[(uint8_t)(ENEMY_X + x)] = (uint8_t)diff2;
    a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] - (diff2 < 0));
    ram[(uint8_t)(ENEMY_PAGE + x)] = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    if (a != ROM(FLAME_HEIGHTS + ram[ENEMY_Y_FRACTION + x])) {
      ram[(uint8_t)(ENEMY_Y + x)] = (uint8_t)(a + ram[ENEMY_Y_FORCE + x]);
    }
  }
  enemy_screen_position();
  if (ram[(uint8_t)(ENEMY_STATE + x)] != 0) {
    return;
  }
  ram[0] = 0x51;
  y = 2;
  if ((ram[FRAME_COUNT] & 2) != 0) {
    y = 0x82;
  }
  ram[1] = y;
  y = ram[ENEMY_SPRITE_SLOT + x];
  x = 0;
  do {
    ram[SPRITE_Y + y] = ram[ENEMY_SCREEN_Y];
    ram[SPRITE_TILE + y] = ram[0];
    ram[0]++;
    ram[SPRITE_ATTR + y] = ram[1];
    a = ram[ENEMY_SCREEN_X];
    ram[SPRITE_X + y] = a;
    ram[ENEMY_SCREEN_X] = (uint8_t)(a + 8);
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x++;
  } while (x < 3);
  x = ram[SLOT];
  enemy_offscreen_bits();
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_OFFSCREEN];
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    ram[SPRITE_X + 9 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    ram[SPRITE_X + 5 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    ram[SPRITE_X + 1 + y] = 0xf8;
  }
  sp++;
  if ((ram[0x100 + sp] & 1) == 0) {
    return;
  }
  ram[SPRITE_Y + y] = 0xf8;
  return;
}

// in: x; out: x
void run_firework(void) {
  ram[(uint8_t)(ENEMY_Y_SPEED + x)]--;
  if (ram[(uint8_t)(ENEMY_Y_SPEED + x)] == 0) {
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 8;
    ram[(uint8_t)(ENEMY_X_SPEED + x)]++;
    if (ram[(uint8_t)(ENEMY_X_SPEED + x)] >= 3) { goto loc_d2bd; }
  }
  enemy_screen_position();
  ram[FIREBALL_SCREEN_Y] = ram[ENEMY_SCREEN_Y];
  ram[FIREBALL_SCREEN_X] = ram[ENEMY_SCREEN_X];
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  draw_explosion();
  return;

loc_d2bd:
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0;
  ram[PULSE2_SFX_REQUEST] = 8;
  ram[SCORE_ADD + 4] = 5;
  award_area_points(); return;
}

// in: x; out: x
void run_star_flag(void) {
  ram[FRENZY_ENEMY] = 0;
  a = ram[STAR_FLAG_TASK];
  if (a >= 5) {
    return;
  }
  switch (dispatch(0xd2e7)) {
    case 0xd311: star_flag_done(); return;
    case 0xd2f2: count_fireworks(); return;
    case 0xd312: award_time_points(); return;
    case 0xd34e: raise_star_flag(); return;
    case 0xd3a2: wait_area_end(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: none
void count_fireworks(void) {
  y = 5;
  a = ram[TIME_DIGITS + 2];
  if (a != 1) {
    y = 3;
    if (a != 3) {
      y = 0;
      if (a != 6) {
        a = 0xff;
      }
    }
  }
  ram[FIREWORKS_COUNT] = a;
  ram[(uint8_t)(ENEMY_STATE + x)] = y;
  next_star_flag_task(); return;
}

void next_star_flag_task(void) {
  ram[STAR_FLAG_TASK]++;
  star_flag_done(); return;
}

void star_flag_done(void) {
  return;
}

// in: none; out: x
void award_time_points(void) {
  a = ram[TIME_DIGITS] | ram[TIME_DIGITS + 1] | ram[TIME_DIGITS + 2];
  if (a == 0) {
    next_star_flag_task(); return;
  }
  if ((ram[FRAME_COUNT] & 4) != 0) {
    ram[PULSE2_SFX_REQUEST] = 0x10;
  }
  y = 0x23;
  ram[SCORE_ADD + 5] = 0xff;
  add_digits();
  ram[SCORE_ADD + 5] = 5;
  award_area_points(); return;
}

// in: none; out: x
void award_area_points(void) {
  y = 0xb;
  if (ram[CURRENT_PLAYER] != 0) {
    y = 0x11;
  }
  add_digits();
  a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(ram[CURRENT_PLAYER] << 1) << 1) << 1) << 1);
  a |= 4;
  queue_counters(); return;
}

// in: x; out: x
void raise_star_flag(void) {
  if (ram[(uint8_t)(ENEMY_Y + x)] >= 0x72) {
    ram[(uint8_t)(ENEMY_Y + x)]--;
    draw_star_flag(); return;
  }
  a = ram[FIREWORKS_COUNT];
  neg_flag = (a & 0x80) != 0;
  if (a != 0) {
    if (!neg_flag) {
      ram[FRENZY_ENEMY] = 0x16;
      draw_star_flag(); return;
    }
  }
  draw_star_flag();
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = 6;
  next_star_flag_task_b(); return;
}

// in: x; out: x
void draw_star_flag(void) {
  enemy_screen_position();
  y = ram[ENEMY_SPRITE_SLOT + x];
  x = 3;
  do {
    ram[SPRITE_Y + y] = (uint8_t)(ram[ENEMY_SCREEN_Y] + ROM(STAR_FLAG_Y_OFFSETS + x));
    ram[SPRITE_TILE + y] = ROM(STAR_FLAG_TILES + x);
    ram[SPRITE_ATTR + y] = 0x22;
    ram[SPRITE_X + y] = (uint8_t)(ram[ENEMY_SCREEN_X] + ROM(STAR_FLAG_X_OFFSETS + x));
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x--;
  } while ((x & 0x80) == 0);
  x = ram[SLOT];
  return;
}

void next_star_flag_task_b(void) {
  ram[STAR_FLAG_TASK]++;
  return;
}

// in: x; out: x
void wait_area_end(void) {
  draw_star_flag();
  if (ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] != 0) {
    return;
  }
  if (ram[EVENT_MUSIC_PLAYING] == 0) { next_star_flag_task_b(); return; }
  return;
}

// in: x; out: none
void move_piranha(void) {
  if (ram[(uint8_t)(ENEMY_STATE + x)] == 0) {
    if (ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] == 0) {
      if (ram[(uint8_t)(ENEMY_Y_SPEED + x)] == 0) {
        if ((ram[(uint8_t)(ENEMY_X_SPEED + x)] & 0x80) == 0) {
          player_enemy_distance();
          if (neg_flag) {
            ram[0] = (uint8_t)((ram[0] ^ 0xff) + 1);
          }
          if (ram[0] < 0x21) { goto loc_d40a; }
        }
        a = (uint8_t)((ram[(uint8_t)(ENEMY_X_SPEED + x)] ^ 0xff) + 1);
        ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
        ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
      }
      a = ram[ENEMY_Y_FORCE + x];
      if ((ram[(uint8_t)(ENEMY_X_SPEED + x)] & 0x80) != 0) {
        a = ram[ENEMY_Y_FRACTION + x];
      }
      ram[0] = a;
      if ((ram[FRAME_COUNT] & 1) != 0) {
        if (ram[TIMERS_PAUSED] == 0) {
          a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + ram[(uint8_t)(ENEMY_X_SPEED + x)]);
          ram[(uint8_t)(ENEMY_Y + x)] = a;
          if (a == ram[0]) {
            ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0;
            ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0x40;
          }
        }
      }
    }
  }

loc_d40a:
  ram[ENEMY_SPRITE_ATTR + x] = 0x20;
  return;
}

// in: a, x; out: a
void spin_firebar(void) {
  ram[7] = a;
  if (ram[(uint8_t)(TARGET_PAGE + x)] == 0) {
    uint16_t sum = (uint16_t)(ram[(uint8_t)(ENEMY_X_SPEED + x)] + ram[7]);
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = (uint8_t)sum;
    a = (uint8_t)(ram[(uint8_t)(ENEMY_Y_SPEED + x)] + (sum >= 0x100));
    return;
  }
  int diff = ram[(uint8_t)(ENEMY_X_SPEED + x)] - ram[7];
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = (uint8_t)diff;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_Y_SPEED + x)] - (diff < 0));
  return;
}

// in: x; out: x
void update_balance_platform(void) {
  if (ram[(uint8_t)(ENEMY_Y_PAGE + x)] == 3) {
    erase_enemy(); return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  if ((a & 0x80) != 0) {
    return;
  }
  y = a;
  ram[0] = ram[PLATFORM_CONTACT + x];
  if (ram[(uint8_t)(ENEMY_DIR + x)] == 0) {
    if (0x2d >= ram[(uint8_t)(ENEMY_Y + x)]) {
      if (y == ram[0]) { goto loc_d598; }
      ram[(uint8_t)(ENEMY_Y + x)] = 0x2f;
      stop_balance_platforms(); return;
    }
    if (0x2d >= ram[ENEMY_Y + y]) {
      if (x == ram[0]) { goto loc_d598; }
      ram[ENEMY_Y + y] = 0x2f;
      stop_balance_platforms(); return;
    }
    ram[0x100 + sp] = ram[(uint8_t)(ENEMY_Y + x)];
    sp--;
    a = ram[PLATFORM_CONTACT + x];
    if ((a & 0x80) != 0) {
      uint16_t sum3 = (uint16_t)(ram[ENEMY_Y_FORCE + x] + 5);
      ram[0] = (uint8_t)sum3;
      a = (uint8_t)(ram[(uint8_t)(ENEMY_Y_SPEED + x)] + (sum3 >= 0x100));
      zero_flag = a == 0;
      if ((a & 0x80) != 0) { goto loc_d4a4; }
      if (!zero_flag) { goto loc_d498; }
      if (ram[0] < 0xb) { goto loc_d49e; }
      goto loc_d498;
    }
    if (a == ram[SLOT]) { goto loc_d4a4; }

loc_d498:
    move_platform_up();
    goto loc_d4a7;

loc_d49e:
    stop_balance_platforms();
    goto loc_d4a7;

loc_d4a4:
    move_platform_down();

loc_d4a7:
    y = ram[(uint8_t)(ENEMY_STATE + x)];
    sp++;
    a = (uint8_t)(ram[0x100 + sp] - ram[(uint8_t)(ENEMY_Y + x)]);
    ram[ENEMY_Y + y] = (uint8_t)(a + ram[ENEMY_Y + y]);
    a = ram[PLATFORM_CONTACT + x];
    if ((a & 0x80) == 0) {
      x = a;
      place_player_on_vertical_platform();
    }
    y = ram[SLOT];
    if ((ram[ENEMY_Y_SPEED + y] | ram[ENEMY_Y_FORCE + y]) != 0) {
      if (ram[VRAM_LIST1_LENGTH] < 0x20) {
        a = ram[ENEMY_Y_SPEED + y];
        ram[0x100 + sp] = a;
        sp--;
        ram[0x100 + sp] = a;
        sp--;
        draw_platform_rope();
        ram[VRAM_LIST1 + x] = ram[1];
        ram[VRAM_LIST1 + 1 + x] = ram[0];
        ram[VRAM_LIST1 + 2 + x] = 2;
        if ((ram[ENEMY_Y_SPEED + y] & 0x80) == 0) {
          ram[VRAM_LIST1 + 3 + x] = 0xa2;
          ram[VRAM_LIST1 + 4 + x] = 0xa3;
        } else {
          ram[VRAM_LIST1 + 3 + x] = 0x24;
          ram[VRAM_LIST1 + 4 + x] = 0x24;
        }
        y = ram[ENEMY_STATE + y];
        sp++;
        a = ram[0x100 + sp] ^ 0xff;
        draw_platform_rope();
        ram[VRAM_LIST1 + 5 + x] = ram[1];
        ram[VRAM_LIST1 + 6 + x] = ram[0];
        ram[VRAM_LIST1 + 7 + x] = 2;
        sp++;
        if ((ram[0x100 + sp] & 0x80) != 0) {
          ram[VRAM_LIST1 + 8 + x] = 0xa2;
          ram[VRAM_LIST1 + 9 + x] = 0xa3;
        } else {
          ram[VRAM_LIST1 + 8 + x] = 0x24;
          ram[VRAM_LIST1 + 9 + x] = 0x24;
        }
        ram[VRAM_LIST1 + 0xa + x] = 0;
        ram[VRAM_LIST1_LENGTH] = (uint8_t)(ram[VRAM_LIST1_LENGTH] + 0xa);
      }
    }
    x = ram[SLOT];
    return;

loc_d598:
    x = y;
    enemy_offscreen_bits();
    a = 6;
    spawn_floating_score();
    ram[POPUP_X + x] = ram[PLAYER_SCREEN_X];
    ram[POPUP_Y + x] = ram[PLAYER_Y];
    ram[(uint8_t)(ENEMY_DIR + x)] = 1;
    stop_balance_platforms(); return;
  }
  ram[0x100 + sp] = y;
  sp--;
  move_falling_platform();
  sp++;
  x = ram[0x100 + sp];
  move_falling_platform();
  a = ram[PLATFORM_CONTACT + ram[SLOT]];
  if ((a & 0x80) == 0) {
    x = a;
    place_player_on_vertical_platform();
  }
  x = ram[SLOT];
  return;
}

// in: a, y; out: x
void draw_platform_rope(void) {
  ram[0x100 + sp] = a;
  sp--;
  uint16_t sum = (uint16_t)(ram[ENEMY_X + y] + 8);
  carry_flag = sum >= 0x100;
  a = (uint8_t)sum;
  if (ram[HARD_MODE_LATE] == 0) {
    uint16_t sum2 = (uint16_t)(a + 0x10);
    carry_flag = sum2 >= 0x100;
    a = (uint8_t)sum2;
  }
  ram[0x100 + sp] = a;
  ram[2] = (uint8_t)(ram[ENEMY_PAGE + y] + carry_flag);
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  ram[0] = (ram[0x100 + sp] & 0xf0) >> 1 >> 1 >> 1;
  x = ram[(uint8_t)(ENEMY_Y + y)];
  sp++;
  if ((ram[0x100 + sp] & 0x80) != 0) {
    x = (uint8_t)(x + 8);
  }
  a = x;
  x = ram[VRAM_LIST1_LENGTH];
  carry_flag = (a & 0x80) != 0;
  a = (uint8_t)(a << 1);
  bool carry = (a & 0x80) != 0;
  a = (uint8_t)((a << 1) | carry_flag);
  ram[0x100 + sp] = a;
  ram[1] = ((uint8_t)((a << 1) | carry) & 3) | 0x20;
  ram[1] = (uint8_t)(((ram[2] & 1) << 1 << 1) | ram[1]);
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  ram[0] = (uint8_t)((ram[0x100 + sp] & 0xe0) + ram[0]);
  if (ram[ENEMY_Y + y] < 0xe8) {
    return;
  }
  ram[0] &= 0xbf;
  return;
}

// in: x, y; out: none
void stop_balance_platforms(void) {
  clear_vertical_motion();
  ram[ENEMY_Y_SPEED + y] = a;
  ram[ENEMY_Y_FORCE + y] = a;
  return;
}

// in: x; out: x
void move_vertical_platform(void) {
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)] | ram[ENEMY_Y_FORCE + x];
  if (a == 0) {
    ram[ENEMY_Y_FRACTION + x] = a;
    if (ram[(uint8_t)(ENEMY_Y + x)] < ram[ENEMY_X_FORCE + x]) {
      if ((ram[FRAME_COUNT] & 7) != 0) {
        carry_player_vertically(); return;
      }
      ram[(uint8_t)(ENEMY_Y + x)]++;
      carry_player_vertically(); return;
    }
  }
  if (ram[(uint8_t)(ENEMY_Y + x)] >= ram[(uint8_t)(ENEMY_X_SPEED + x)]) {
    move_platform_up();
    carry_player_vertically(); return;
  }
  move_platform_down();
  carry_player_vertically(); return;
}

// in: x; out: none
void carry_player_vertically(void) {
  if ((ram[PLATFORM_CONTACT + x] & 0x80) != 0) {
    return;
  }
  place_player_on_vertical_platform();
  return;
}

// in: x; out: x
void move_side_platform(void) {
  a = 0xe;
  sway_counters();
  sway_offset();
  if ((ram[PLATFORM_CONTACT + x] & 0x80) != 0) { return; }
  carry_player_horizontally(); return;
}

// in: x; out: none
void carry_player_horizontally(void) {
  uint16_t sum = (uint16_t)(ram[PLAYER_X] + ram[0]);
  carry_flag = sum >= 0x100;
  ram[PLAYER_X] = (uint8_t)sum;
  a = ram[PLAYER_PAGE];
  y = ram[0];
  if ((y & 0x80) == 0) {
    a = (uint8_t)(a + carry_flag);
  } else {
    a = (uint8_t)(a - !carry_flag);
  }
  ram[PLAYER_PAGE] = a;
  ram[PLATFORM_X_SCROLL] = y;
  place_player_on_vertical_platform();
  return;
}

// in: x; out: x
void drop_platform(void) {
  if ((ram[PLATFORM_CONTACT + x] & 0x80) != 0) {
    return;
  }
  move_drop_platform();
  place_player_on_vertical_platform();
  return;
}

// in: x; out: x
void right_platform(void) {
  move_enemy_x();
  ram[0] = a;
  if ((ram[PLATFORM_CONTACT + x] & 0x80) != 0) {
    return;
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0x10;
  carry_player_horizontally();
  return;
}

// in: x; out: none
void move_large_lift(void) {
  move_lift();
  carry_player_vertically(); return;
}

// in: x; out: none
void move_small_lift(void) {
  move_lift();
  a = ram[PLATFORM_CONTACT + x];
  if (a == 0) {
    return;
  }
  place_player_on_small_platform();
  return;
}

// in: x; out: none
void move_lift(void) {
  if (ram[TIMERS_PAUSED] != 0) {
    return;
  }
  uint16_t sum = (uint16_t)(ram[ENEMY_Y_FRACTION + x] + ram[ENEMY_Y_FORCE + x]);
  ram[ENEMY_Y_FRACTION + x] = (uint8_t)sum;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  a = (uint8_t)(a + ram[(uint8_t)(ENEMY_Y_SPEED + x)] + (sum >= 0x100));
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  return;
}

// in: x; out: none
void check_enemy_offscreen(void) {
  if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x14) {
    return;
  }
  a = ram[SCREEN_LEFT_X];
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = y >= 5;
  if (y != 5) {
    carry_flag = y >= 0xd;
    if (y != 0xd) { goto loc_d68f; }
  }
  uint16_t sum = (uint16_t)(a + 0x38 + carry_flag);
  carry_flag = sum >= 0x100;
  a = (uint8_t)sum;

loc_d68f:;
  int diff = a - 0x48 - !carry_flag;
  ram[1] = (uint8_t)diff;
  int diff2 = ram[SCREEN_LEFT_PAGE] - (diff < 0);
  ram[0] = (uint8_t)diff2;
  uint16_t sum2 = (uint16_t)(ram[SCREEN_RIGHT_X] + 0x48 + (diff2 >= 0));
  ram[3] = (uint8_t)sum2;
  ram[2] = (uint8_t)(ram[SCREEN_RIGHT_PAGE] + (sum2 >= 0x100));
  carry_flag = ram[(uint8_t)(ENEMY_X + x)] >= ram[1];
  a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] - ram[0] - !carry_flag);
  if ((a & 0x80) == 0) {
    carry_flag = ram[(uint8_t)(ENEMY_X + x)] >= ram[3];
    a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] - ram[2] - !carry_flag);
    if ((a & 0x80) != 0) {
      return;
    }
    if (ram[(uint8_t)(ENEMY_STATE + x)] == 5) {
      return;
    }
    if (y == 0xd) {
      return;
    }
    if (y == 0x30) {
      return;
    }
    if (y == 0x31) {
      return;
    }
    if (y == 0x32) {
      return;
    }
  }
  erase_enemy();
  return;
}

// in: x; out: x
void fireball_enemy_collision(void) {
  a = ram[(uint8_t)(FIREBALL_STATE + x)];
  if (a != 0) {
    if ((a & 0x80) == 0) {
      if ((ram[FRAME_COUNT] & 1) == 0) {
        y = (uint8_t)(((uint8_t)(x << 1) << 1) + 0x1c);
        x = 4;
        do {
          ram[1] = x;
          ram[0x100 + sp] = y;
          sp--;
          if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) == 0) {
            if (ram[(uint8_t)(ENEMY_ACTIVE + x)] != 0) {
              a = ram[(uint8_t)(ENEMY_KIND + x)];
              if (a >= 0x24) {
                if (a < 0x2b) { goto loc_d72c; }
              }
              if (a == 6) {
                if (ram[(uint8_t)(ENEMY_STATE + x)] >= 2) { goto loc_d72c; }
              }
              if (ram[ENEMY_OFFSCREEN_MASKED + x] == 0) {
                x = (uint8_t)(((uint8_t)(x << 1) << 1) + 4);
                objects_collide();
                x = ram[SLOT];
                if (carry_flag) {
                  ram[(uint8_t)(FIREBALL_STATE + x)] = 0x80;
                  x = ram[1];
                  fireball_hit_enemy();
                }
              }
            }
          }

loc_d72c:
          sp++;
          y = ram[0x100 + sp];
          x = (uint8_t)(ram[1] - 1);
        } while ((x & 0x80) == 0);
      }
    }
  }
  x = ram[SLOT];
  return;
}

// in: x; out: none
void fireball_hit_enemy(void) {
  enemy_screen_position();
  x = ram[1];
  a = ram[(uint8_t)(ENEMY_ACTIVE + x)];
  if ((a & 0x80) != 0) {
    x = a & 0xf;
    if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x2d) { goto loc_d75c; }
    x = ram[1];
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 2) {
    return;
  }
  if (a == 0x2d) {

loc_d75c:
    ram[BOWSER_HEALTH]--;
    if (ram[BOWSER_HEALTH] != 0) {
      return;
    }
    clear_vertical_motion();
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    ram[FRENZY_ENEMY] = a;
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfe;
    y = ram[WORLD_NUMBER];
    ram[(uint8_t)(ENEMY_KIND + x)] = ROM(BOWSER_LOOKALIKES + y);
    a = 0x20;
    if (y < 3) {
      a = 0x23;
    }
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    ram[PULSE2_SFX_REQUEST] = 0x80;
    x = ram[1];
    a = 9;
    smack_enemy_score(); return;
  }
  if (a == 8) {
    return;
  }
  if (a == 0xc) {
    return;
  }
  if (a >= 0x15) { return; }
  defeat_enemy(); return;
}

// in: x; out: y
void defeat_enemy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = a >= 0xd;
  if (a == 0xd) {
    a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + 0x18 + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
  }
  check_stun_enemy();
  ram[(uint8_t)(ENEMY_STATE + x)] = (ram[(uint8_t)(ENEMY_STATE + x)] & 0x1f) | 0x20;
  a = 2;
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  if (y == 5) {
    a = 6;
  }
  if (y != 6) {
    smack_enemy_score(); return;
  }
  a = 1;
  smack_enemy_score(); return;
}

// in: a, x; out: none
void smack_enemy_score(void) {
  spawn_floating_score();
  ram[PULSE1_SFX_REQUEST] = 8;
  return;
}

// in: x; out: x
void player_hammer_collision(void) {
  if ((ram[FRAME_COUNT] & 1) == 0) {
    return;
  }
  if ((ram[TIMERS_PAUSED] | ram[MISC_OFFSCREEN]) != 0) {
    return;
  }
  y = (uint8_t)(((uint8_t)(x << 1) << 1) + 0x24);
  player_collides();
  x = ram[SLOT];
  if (carry_flag) {
    if (ram[MISC_CONTACT + x] != 0) {
      return;
    }
    ram[MISC_CONTACT + x] = 1;
    a = (uint8_t)((ram[(uint8_t)(MISC_X_SPEED + x)] ^ 0xff) + 1);
    ram[(uint8_t)(MISC_X_SPEED + x)] = a;
    if (ram[STAR_TIMER] != 0) { return; }
    injure_player(); return;
  }
  ram[MISC_CONTACT + x] = 0;
  return;
}

// in: x; out: x
void player_enemy_collision(void) {
  goto player_enemy_collision;
  do {
    erase_enemy();
    a = 6;
    spawn_floating_score();
    ram[PULSE2_SFX_REQUEST] = 0x20;
    a = ram[POWERUP_KIND];
    if (a >= 2) {
      if (a == 3) { goto loc_d83a; }
      ram[STAR_TIMER] = 0x23;
      ram[AREA_MUSIC_REQUEST] = 0x40;
      return;
    }
    a = ram[PLAYER_POWER];
    if (a == 0) { goto loc_d840; }
    if (a != 1) {
      return;
    }
    ram[PLAYER_POWER] = 2;
    queue_player_colors();
    a = 0xc;
    goto loc_d847;

loc_d83a:
    ram[POPUP_KIND + x] = 0xb;
    return;

loc_d840:
    ram[PLAYER_POWER] = 1;
    a = 9;

loc_d847:
    y = 0;
    set_player_routine();
    return;

player_enemy_collision:
    if ((ram[FRAME_COUNT] & 1) != 0) {
      return;
    }
    check_player_vertical();
    if (carry_flag) {
      return;
    }
    if (ram[ENEMY_OFFSCREEN_MASKED + x] != 0) {
      return;
    }
    if (ram[PLAYER_TASK] != 8) {
      return;
    }
    if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
      return;
    }
    enemy_box_offset();
    player_collides();
    x = ram[SLOT];
    if (!carry_flag) {
      ram[ENEMY_COLLISION + x] &= 0xfe;
      return;
    }
    y = ram[(uint8_t)(ENEMY_KIND + x)];
  } while (y == 0x2e);
  if (ram[STAR_TIMER] != 0) {
    defeat_enemy(); return;
  }
  zero_flag = ((ram[ENEMY_COLLISION + x] & 1) | ram[ENEMY_OFFSCREEN_MASKED + x]) == 0;
  if (!zero_flag) {
    return;
  }
  ram[ENEMY_COLLISION + x] = 1 | ram[ENEMY_COLLISION + x];
  if (y != 0x12) {
    if (y == 0xd) {
      injure_player(); return;
    }
    if (y == 0xc) {
      injure_player(); return;
    }
    if (y != 0x33) {
      if (y >= 0x15) {
        injure_player(); return;
      }
      if (ram[AREA_KIND] == 0) {
        injure_player(); return;
      }
      if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x80) == 0) {
        if ((ram[(uint8_t)(ENEMY_STATE + x)] & 7) >= 2) {
          if (ram[(uint8_t)(ENEMY_KIND + x)] == 6) {
            return;
          }
          ram[PULSE1_SFX_REQUEST] = 8;
          ram[(uint8_t)(ENEMY_STATE + x)] |= 0x80;
          face_player();
          ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(KICKED_SHELL_SPEEDS + y);
          a = (uint8_t)(3 + ram[STOMP_CHAIN]);
          y = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff];
          if (y < 3) {
            a = ROM(KICKED_SHELL_POINTS + y);
          }
          spawn_floating_score();
          return;
        }
      }
    }
  }
  a = ram[PLAYER_Y_SPEED];
  zero_flag = a == 0;
  if ((a & 0x80) == 0) {
    if (!zero_flag) { goto loc_d969; }
  }
  if (ram[(uint8_t)(ENEMY_KIND + x)] >= 7) {
    if ((uint8_t)(ram[PLAYER_Y] + 0xc) < ram[(uint8_t)(ENEMY_Y + x)]) { goto loc_d969; }
  }
  if (ram[STOMP_TIMER] == 0) {
    if (ram[INJURY_TIMER] != 0) {
      end_injury_check(); return;
    }
    if (ram[PLAYER_SCREEN_X] >= ram[ENEMY_SCREEN_X]) { goto loc_d9f6; }
    if (ram[(uint8_t)(ENEMY_DIR + x)] != 1) { injure_player(); return; }
    goto loc_d9ff;
  }

loc_d969:
  if (ram[(uint8_t)(ENEMY_KIND + x)] == 0x12) {
    injure_player(); return;
  }
  ram[PULSE1_SFX_REQUEST] = 4;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  y = 0;
  if (a != 0x14) {
    if (a != 8) {
      if (a != 0x33) {
        if (a != 0xc) {
          y = 1;
          if (a != 5) {
            y = 2;
            if (a != 0x11) {
              y = 3;
              if (a != 7) { goto loc_d9b3; }
            }
          }
        }
      }
    }
  }
  a = ROM(STOMP_POINTS + y);
  spawn_floating_score();
  ram[0x100 + sp] = ram[(uint8_t)(ENEMY_DIR + x)];
  sp--;
  stun_enemy();
  sp++;
  ram[(uint8_t)(ENEMY_DIR + x)] = ram[0x100 + sp];
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x20;
  clear_vertical_motion();
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  ram[PLAYER_Y_SPEED] = 0xfd;
  return;

loc_d9b3:
  if (a >= 9) {
    ram[(uint8_t)(ENEMY_KIND + x)] = a & 1;
    y = 0;
    ram[(uint8_t)(ENEMY_STATE + x)] = 0;
    a = 3;
    spawn_floating_score();
    clear_vertical_motion();
    face_player();
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(DEMOTED_KOOPA_SPEEDS + y);
  } else {
    ram[(uint8_t)(ENEMY_STATE + x)] = 4;
    ram[STOMP_CHAIN]++;
    a = (uint8_t)(ram[STOMP_CHAIN] + ram[STOMP_TIMER]);
    spawn_floating_score();
    ram[STOMP_TIMER]++;
    ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = ROM(REVIVAL_RATES + ram[HARD_MODE]);
  }
  ram[PLAYER_Y_SPEED] = 0xfc;
  return;

loc_d9f6:
  if (ram[(uint8_t)(ENEMY_DIR + x)] == 1) {
    injure_player(); return;
  }

loc_d9ff:
  turn_enemy_around();
  injure_player(); return;
}

// in: none; out: x
void injure_player(void) {
  a = ram[INJURY_TIMER];
  if (a != 0) { end_injury_check(); return; }
  force_injury(); return;
}

// in: a; out: x
void force_injury(void) {
  x = ram[PLAYER_POWER];
  if (x == 0) { goto loc_d958; }
  ram[PLAYER_POWER] = a;
  ram[INJURY_TIMER] = 8;
  ram[PULSE1_SFX_REQUEST] = 0x10;
  queue_player_colors();
  a = 0xa;

loc_d946:
  y = 1;
  set_player_routine(); return;

loc_d958:
  ram[PLAYER_X_SPEED] = x;
  ram[EVENT_MUSIC_REQUEST] = (uint8_t)(x + 1);
  ram[PLAYER_Y_SPEED] = 0xfc;
  a = 0xb;
  goto loc_d946;
}

// in: a, y; out: x
void set_player_routine(void) {
  ram[PLAYER_TASK] = a;
  ram[PLAYER_STATE] = y;
  ram[TIMERS_PAUSED] = 0xff;
  ram[SCROLL_AMOUNT] = 0;
  end_injury_check(); return;
}

// in: none; out: x
void end_injury_check(void) {
  x = ram[SLOT];
  return;
}

// in: x; out: y
void face_player(void) {
  y = 1;
  player_enemy_distance();
  if (neg_flag) {
    y = 2;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  y--;
  return;
}

// in: a, x; out: a
void spawn_floating_score(void) {
  ram[POPUP_KIND + x] = a;
  ram[POPUP_TIMER + x] = 0x30;
  ram[POPUP_Y + x] = ram[(uint8_t)(ENEMY_Y + x)];
  a = ram[ENEMY_SCREEN_X];
  ram[POPUP_X + x] = a;
  return;
}

// in: x; out: x
void enemy_enemy_collisions(void) {
  if ((ram[FRAME_COUNT] & 1) == 0) {
    return;
  }
  if (ram[AREA_KIND] == 0) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a < 0x15) {
    if (a != 0x11) {
      if (a != 0xd) {
        if (ram[ENEMY_OFFSCREEN_MASKED + x] == 0) {
          enemy_box_offset();
          x--;
          if ((x & 0x80) == 0) {
            do {
              ram[1] = x;
              ram[0x100 + sp] = y;
              sp--;
              if (ram[(uint8_t)(ENEMY_ACTIVE + x)] != 0) {
                a = ram[(uint8_t)(ENEMY_KIND + x)];
                if (a < 0x15) {
                  if (a != 0x11) {
                    if (a != 0xd) {
                      if (ram[ENEMY_OFFSCREEN_MASKED + x] == 0) {
                        x = (uint8_t)(((uint8_t)(x << 1) << 1) + 4);
                        objects_collide();
                        x = ram[SLOT];
                        y = ram[1];
                        if (carry_flag) {
                          a = (ram[(uint8_t)(ENEMY_STATE + x)] | ram[ENEMY_STATE + y]) & 0x80;
                          if (a == 0) {
                            if ((ram[ENEMY_COLLISION + y] & ROM(SET_BIT_MASKS + x)) != 0) { goto loc_daaa; }
                            ram[ENEMY_COLLISION + y] |= ROM(SET_BIT_MASKS + x);
                          }
                          enemy_pair_collision();
                        } else {
                          ram[ENEMY_COLLISION + y] &= ROM(CLEAR_BIT_MASKS + x);
                        }
                      }
                    }
                  }
                }
              }

loc_daaa:
              sp++;
              y = ram[0x100 + sp];
              x = (uint8_t)(ram[1] - 1);
            } while ((x & 0x80) == 0);
          }
        }
      }
    }
  }
  x = ram[SLOT];
  return;
}

// in: x, y; out: none
void enemy_pair_collision(void) {
  a = (ram[ENEMY_STATE + y] | ram[(uint8_t)(ENEMY_STATE + x)]) & 0x20;
  if (a != 0) {
    return;
  }
  if (ram[(uint8_t)(ENEMY_STATE + x)] >= 6) {
    if (ram[(uint8_t)(ENEMY_KIND + x)] == 5) {
      return;
    }
    if ((ram[ENEMY_STATE + y] & 0x80) != 0) {
      a = 6;
      spawn_floating_score();
      defeat_enemy();
      y = ram[1];
    }
    x = y;
    defeat_enemy();
    a = (uint8_t)(ram[SHELL_CHAIN + ram[SLOT]] + 4);
    x = ram[1];
    spawn_floating_score();
    x = ram[SLOT];
    ram[SHELL_CHAIN + x]++;
    return;
  }
  if (ram[ENEMY_STATE + y] >= 6) {
    if (ram[ENEMY_KIND + y] == 5) {
      return;
    }
    defeat_enemy();
    y = ram[1];
    a = (uint8_t)(ram[SHELL_CHAIN + y] + 4);
    x = ram[SLOT];
    spawn_floating_score();
    x = ram[1];
    ram[SHELL_CHAIN + x]++;
    return;
  }
  x = y;
  turn_enemy_around();
  x = ram[SLOT];
  turn_enemy_around(); return;
}

// in: x; out: none
void turn_enemy_around(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0xd) {
    return;
  }
  if (a == 0x11) {
    return;
  }
  if (a == 5) {
    return;
  }
  if (a == 0x12) {
    reverse_enemy(); return;
  }
  if (a == 0xe) {
    reverse_enemy(); return;
  }
  if (a >= 7) { return; }
  reverse_enemy(); return;
}

// in: x; out: y
void reverse_enemy(void) {
  y = (uint8_t)((ram[(uint8_t)(ENEMY_X_SPEED + x)] ^ 0xff) + 1);
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = y;
  ram[(uint8_t)(ENEMY_DIR + x)] ^= 3;
  return;
}

// in: x; out: x
void large_platform_collision(void) {
  ram[PLATFORM_CONTACT + x] = 0xff;
  if (ram[TIMERS_PAUSED] != 0) {
    large_platform_collision_done(); return;
  }
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x80) != 0) {
    large_platform_collision_done(); return;
  }
  if (ram[(uint8_t)(ENEMY_KIND + x)] != 0x24) {
    player_on_large_platform(); return;
  }
  x = ram[(uint8_t)(ENEMY_STATE + x)];
  player_on_large_platform();
  player_on_large_platform(); return;
}

// in: x; out: x
void player_on_large_platform(void) {
  check_player_vertical();
  if (carry_flag) {
    large_platform_collision_done(); return;
  }
  a = x;
  enemy_box_offset_for();
  ram[0] = ram[(uint8_t)(ENEMY_Y + x)];
  ram[0x100 + sp] = x;
  sp--;
  player_collides();
  sp++;
  x = ram[0x100 + sp];
  if (!carry_flag) {
    large_platform_collision_done(); return;
  }
  platform_landing();
  large_platform_collision_done(); return;
}

// in: none; out: x
void large_platform_collision_done(void) {
  x = ram[SLOT];
  return;
}

// in: x; out: x
void small_platform_collision(void) {
  a = ram[TIMERS_PAUSED];
  if (a == 0) {
    ram[PLATFORM_CONTACT + x] = a;
    check_player_vertical();
    if (!carry_flag) {
      ram[0] = 2;
      do {
        x = ram[SLOT];
        enemy_box_offset();
        if ((a & 2) != 0) { goto loc_dbb7; }
        if (ram[BOX_TOP + y] >= 0x20) {
          player_collides();
          if (carry_flag) { goto loc_dbba; }
        }
        ram[BOX_TOP + y] = (uint8_t)(ram[BOX_TOP + y] + 0x80);
        ram[BOX_BOTTOM + y] = (uint8_t)(ram[BOX_BOTTOM + y] + 0x80);
        ram[0]--;
      } while (ram[0] != 0);
    }
  }

loc_dbb7:
  x = ram[SLOT];
  return;

loc_dbba:
  x = ram[SLOT];
  platform_landing(); return;
}

// in: x, y; out: x
void platform_landing(void) {
  if ((uint8_t)(ram[BOX_BOTTOM + y] - ram[BOX_TOP]) < 4) {
    if ((ram[PLAYER_Y_SPEED] & 0x80) != 0) {
      ram[PLAYER_Y_SPEED] = 1;
    }
  }
  if ((uint8_t)(ram[BOX_BOTTOM] - ram[BOX_TOP + y]) < 6) {
    if ((ram[PLAYER_Y_SPEED] & 0x80) == 0) {
      a = ram[0];
      y = ram[(uint8_t)(ENEMY_KIND + x)];
      if (y != 0x2b) {
        if (y != 0x2c) {
          a = x;
        }
      }
      x = ram[SLOT];
      ram[PLATFORM_CONTACT + x] = a;
      ram[PLAYER_STATE] = 0;
      return;
    }
  }
  ram[0] = 1;
  if ((uint8_t)(ram[BOX_RIGHT] - ram[BOXES + y]) >= 8) {
    ram[0]++;
    if ((uint8_t)(ram[BOX_RIGHT + y] - ram[BOX_LEFT] - 1) >= 9) { goto loc_dc14; }
  }
  block_player_side();

loc_dc14:
  x = ram[SLOT];
  return;
}

// in: a, x; out: none
void place_player_on_small_platform(void) {
  a = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + ROM(SMALL_PLATFORM_Y_OFFSETS - 1 + a));
  place_player_on_platform_y(); return;
}

// in: x; out: none
void place_player_on_vertical_platform(void) {
  a = ram[(uint8_t)(ENEMY_Y + x)];
  place_player_on_platform_y(); return;
}

// in: a, x; out: none
void place_player_on_platform_y(void) {
  if (ram[PLAYER_TASK] == 0xb) {
    return;
  }
  y = ram[(uint8_t)(ENEMY_Y_PAGE + x)];
  if (y != 1) {
    return;
  }
  int diff = a - 0x20;
  ram[PLAYER_Y] = (uint8_t)diff;
  ram[PLAYER_Y_PAGE] = (uint8_t)(y - (diff < 0));
  ram[PLAYER_Y_SPEED] = 0;
  ram[PLAYER_Y_FORCE] = 0;
  return;
}

// in: none; out: carry
void check_player_vertical(void) {
  carry_flag = ram[PLAYER_OFFSCREEN] >= 0xf0;
  if (carry_flag) {
    return;
  }
  if ((uint8_t)(ram[PLAYER_Y_PAGE] - 1) != 0) {
    return;
  }
  carry_flag = ram[PLAYER_Y] >= 0xd0;
  return;
}

// in: none; out: a, y
void enemy_box_offset(void) {
  a = ram[SLOT];
  enemy_box_offset_for(); return;
}

// in: a; out: a, y
void enemy_box_offset_for(void) {
  y = (uint8_t)(((uint8_t)(a << 1) << 1) + 4);
  a = ram[ENEMY_OFFSCREEN] & 0xf;
  return;
}

void player_bg_collision(void) {
  if (ram[COLLISIONS_OFF] != 0) {
    return;
  }
  a = ram[PLAYER_TASK];
  if (a == 0xb) {
    return;
  }
  if (a < 4) {
    return;
  }
  a = 1;
  if (ram[SWIMMING] == 0) {
    a = ram[PLAYER_STATE];
    if (a != 0) {
      if (a != 3) { goto loc_dc86; }
    }
    a = 2;
  }
  ram[PLAYER_STATE] = a;

loc_dc86:
  if (ram[PLAYER_Y_PAGE] != 1) {
    return;
  }
  ram[PLAYER_COLLISION] = 0xff;
  if (ram[PLAYER_Y] >= 0xcf) {
    return;
  }
  y = 2;
  if (ram[CROUCHING] == 0) {
    if (ram[PLAYER_SMALL] == 0) {
      y = 1;
      if (ram[SWIMMING] == 0) {
        y = 0;
      }
    }
  }
  a = ROM(BLOCK_PROBE_STARTS + y);
  ram[ENEMY_DATA_PTR + 2] = a;
  y = a;
  x = ram[PLAYER_SMALL];
  if (ram[CROUCHING] != 0) {
    x++;
  }
  if (ram[PLAYER_Y] >= ROM(PLAYER_BG_TOP_EXTENTS + x)) {
    block_probe_head();
    if (!zero_flag) {
      is_coin_metatile();
      if (carry_flag) { goto loc_de05; }
      if ((ram[PLAYER_Y_SPEED] & 0x80) != 0) {
        if (ram[4] >= 4) {
          is_solid_metatile();
          if (!carry_flag) {
            if (ram[AREA_KIND] == 0) { goto loc_dcf2; }
            if (ram[BLOCK_BOUNCE_TIMER] != 0) { goto loc_dcf2; }
            bump_head();
            goto loc_dcf6;
          }
          if (a != 0x26) {
            ram[PULSE1_SFX_REQUEST] = 2;
          }

loc_dcf2:
          ram[PLAYER_Y_SPEED] = 1;
        }
      }
    }
  }

loc_dcf6:
  y = ram[ENEMY_DATA_PTR + 2];
  if (ram[PLAYER_Y] < 0xcf) {
    block_probe_feet();
    is_coin_metatile();
    if (carry_flag) { goto loc_de05; }
    ram[0x100 + sp] = a;
    sp--;
    block_probe_feet();
    ram[0] = a;
    sp++;
    a = ram[0x100 + sp];
    ram[1] = a;
    if (a == 0) {
      a = ram[0];
      if (a == 0) { goto loc_dd5e; }
      is_coin_metatile();
      if (carry_flag) { goto loc_de05; }
    }
    is_climb_metatile();
    if (!carry_flag) {
      if ((ram[PLAYER_Y_SPEED] & 0x80) == 0) {
        if (a == 0xc5) { goto loc_de0e; }
        is_hidden_block();
        if (!zero_flag) {
          if (ram[SPRING_ANIM_STEP] == 0) {
            if (ram[4] >= 5) {
              ram[0] = ram[PLAYER_DIR];
              block_player_side(); return;
            }
            check_spring_landing();
            ram[PLAYER_Y] = 0xf0 & ram[PLAYER_Y];
            enter_pipe();
            ram[PLAYER_Y_SPEED] = 0;
            ram[PLAYER_Y_FORCE] = 0;
            ram[STOMP_CHAIN] = 0;
          }
          ram[PLAYER_STATE] = 0;
        }
      }
    }
  }

loc_dd5e:
  y = (uint8_t)((uint8_t)(ram[ENEMY_DATA_PTR + 2] + 1) + 1);
  ram[0] = 2;
  do {
    y++;
    ram[ENEMY_DATA_PTR + 2] = y;
    a = ram[PLAYER_Y];
    if (a >= 0x20) {
      if (a >= 0xe4) {
        return;
      }
      block_probe_side();
      if (!zero_flag) {
        if (a != 0x1c) {
          if (a != 0x6b) {
            is_climb_metatile();
            if (!carry_flag) { goto loc_dd9c; }
          }
        }
      }
    }
    y = (uint8_t)(ram[ENEMY_DATA_PTR + 2] + 1);
    a = ram[PLAYER_Y];
    if (a < 8) {
      return;
    }
    if (a >= 0xd0) {
      return;
    }
    block_probe_side();
    if (!zero_flag) { goto loc_dd9c; }
    ram[0]--;
  } while (ram[0] != 0);
  return;

loc_dd9c:
  is_hidden_block();
  if (zero_flag) {
    return;
  }
  is_climb_metatile();
  if (carry_flag) { goto loc_de2e; }
  is_coin_metatile();
  if (!carry_flag) {
    is_spring_metatile();
    if (carry_flag) {
      if (ram[SPRING_ANIM_STEP] != 0) { return; }
      goto loc_ddff;
    }
    if (ram[PLAYER_STATE] == 0) {
      if ((uint8_t)(ram[PLAYER_FACING] - 1) == 0) {
        if (a != 0x6c) {
          if (a != 0x1f) { goto loc_ddff; }
        }
        a = ram[PLAYER_SPRITE_ATTR];
        if (a == 0) {
          ram[PULSE1_SFX_REQUEST] = 0x10;
        }
        ram[PLAYER_SPRITE_ATTR] = a | 0x20;
        if ((ram[PLAYER_X] & 0xf) != 0) {
          y = 0;
          if (ram[SCREEN_LEFT_PAGE] != 0) {
            y = 1;
          }
          ram[CHANGE_AREA_TIMER] = ROM(AREA_CHANGE_TIMERS + y);
        }
        a = ram[PLAYER_TASK];
        if (a == 7) {
          return;
        }
        if (a != 8) {
          return;
        }
        ram[PLAYER_TASK] = 2;
        return;
      }
    }

loc_ddff:
    block_player_side();
    return;
  }

loc_de05:
  erase_metatile();
  ram[COINS_FOR_1UPS]++;
  give_coin(); return;

loc_de0e:
  ram[GAME_MODE_TASK] = 0;
  ram[GAME_MODE] = 2;
  ram[PLAYER_X_SPEED] = 0x18;
  erase_metatile(); return;

loc_de2e:
  y = ram[4];
  if (y < 6) {
    return;
  }
  if (y >= 0xa) {
    return;
  }
  if (a != 0x24) {
    if (a != 0x25) { goto loc_de7a; }
  }
  if (ram[PLAYER_TASK] == 5) { goto loc_de88; }
  ram[PLAYER_FACING] = 1;
  ram[SCROLL_LOCKED]++;
  if (ram[PLAYER_TASK] != 4) {
    a = 0x33;
    remove_enemies();
    ram[EVENT_MUSIC_REQUEST] = 0x80;
    ram[FLAGPOLE_SOUND] = 0x40;
    x = 4;
    a = ram[PLAYER_Y];
    ram[FLAGPOLE_CONTACT_Y] = a;
    do {
      if (a >= ROM(FLAGPOLE_HEIGHTS + x)) { goto loc_de70; }
      x--;
    } while (x != 0);

loc_de70:
    ram[FLAG_SCORE_KIND] = x;
  }
  ram[PLAYER_TASK] = 4;
  goto loc_de88;

loc_de7a:
  if (a == 0x26) {
    if (ram[PLAYER_Y] < 0x20) {
      ram[PLAYER_TASK] = 1;
    }
  }

loc_de88:
  ram[PLAYER_STATE] = 3;
  ram[PLAYER_X_SPEED] = 0;
  ram[PLAYER_X_FORCE] = 0;
  if ((uint8_t)(ram[PLAYER_X] - ram[SCREEN_LEFT_X]) < 0x10) {
    ram[PLAYER_FACING] = 2;
  }
  y = ram[PLAYER_FACING];
  a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(ram[6] << 1) << 1) << 1) << 1);
  ram[PLAYER_X] = (uint8_t)(a + ROM(CLIMB_X_OFFSETS - 1 + y));
  if (ram[6] != 0) {
    return;
  }
  ram[PLAYER_PAGE] = (uint8_t)(ram[SCREEN_RIGHT_PAGE] + ROM(CLIMB_X_OFFSETS + 1 + y));
  return;
}

void erase_metatile(void) {
  dynamic_ram_write((uint16_t)((ram[6] | (ram[7] << 8)) + ram[2]), 0);
  erase_coin_or_axe(); return;
}

// in: a; out: zero
void is_hidden_block(void) {
  zero_flag = a == 0x5f;
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0x60;
  return;
}

// in: a; out: none
void check_spring_landing(void) {
  is_spring_metatile();
  if (!carry_flag) {
    return;
  }
  ram[GRAVITY] = 0x70;
  ram[SPRING_FORCE] = 0xf9;
  ram[SPRING_TIMER] = 3;
  ram[SPRING_ANIM_STEP] = 1;
  return;
}

// in: a; out: carry
void is_spring_metatile(void) {
  if (a != 0x67) {
    carry_flag = false;
    if (a != 0x68) {
      return;
    }
  }
  carry_flag = true;
  return;
}

void enter_pipe(void) {
  if ((ram[BUTTONS_VERTICAL] & 4) == 0) {
    return;
  }
  if (ram[0] != 0x11) {
    return;
  }
  if (ram[1] != 0x10) {
    return;
  }
  ram[CHANGE_AREA_TIMER] = 0x30;
  ram[PLAYER_TASK] = 3;
  ram[PULSE1_SFX_REQUEST] = 0x10;
  ram[PLAYER_SPRITE_ATTR] = 0x20;
  a = ram[WARP_ZONE];
  if (a == 0) {
    return;
  }
  x = (uint8_t)((a & 3) << 1 << 1);
  a = ram[PLAYER_X];
  if (a >= 0x60) {
    x++;
    if (a >= 0xa0) {
      x++;
    }
  }
  y = (uint8_t)(ROM(TEXT_WARP_NUMBERS + x) - 1);
  ram[WORLD_NUMBER] = y;
  ram[AREA_POINTER] = ROM(AREAS_WORLD1 + ROM(WORLD_AREA_OFFSETS + y));
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  ram[ENTRANCE_PAGE] = 0;
  ram[AREA_NUMBER] = 0;
  ram[LEVEL_NUMBER] = 0;
  ram[ALT_ENTRANCE] = 0;
  ram[HIDDEN_1UP]++;
  ram[RELOAD_TIME_LIMIT]++;
  return;
}

void block_player_side(void) {
  y = ram[PLAYER_X_SPEED];
  x = (uint8_t)(ram[0] - 1);
  if (x == 0) {
    x++;
    if ((y & 0x80) != 0) { goto loc_df81; }
    a = 0xff;
  } else {
    x = 2;
    if (((y - 1) & 0x80) == 0) { goto loc_df81; }
    a = 1;
  }
  ram[SIDE_CONTACT_TIMER] = 0x10;
  y = 0;
  ram[PLAYER_X_SPEED] = 0;
  if ((a & 0x80) != 0) {
    y = (uint8_t)-0x1;
  }
  ram[0] = y;
  uint16_t sum = (uint16_t)(a + ram[PLAYER_X]);
  ram[PLAYER_X] = (uint8_t)sum;
  ram[PLAYER_PAGE] = (uint8_t)(ram[PLAYER_PAGE] + ram[0] + (sum >= 0x100));

loc_df81:
  ram[PLAYER_COLLISION] = (x ^ 0xff) & ram[PLAYER_COLLISION];
  return;
}

// in: a; out: a, carry
void is_solid_metatile(void) {
  metatile_group();
  carry_flag = a >= ROM(SOLID_METATILE_LIMITS + x);
  return;
}

// in: a; out: a, y, carry
void is_climb_metatile(void) {
  metatile_group();
  carry_flag = a >= ROM(CLIMB_METATILE_LIMITS + x);
  return;
}

// in: a; out: a, carry
void is_coin_metatile(void) {
  carry_flag = a >= 0xc2;
  if (a != 0xc2) {
    carry_flag = a >= 0xc3;
    if (a != 0xc3) {
      carry_flag = false;
      return;
    }
  }
  a = 1;
  ram[PULSE2_SFX_REQUEST] = 1;
  return;
}

// in: a; out: a, x, y
void metatile_group(void) {
  y = a;
  a &= 0xc0;
  carry_flag = (a & 0x80) != 0;
  a = (uint8_t)(a << 1);
  bool carry = (a & 0x80) != 0;
  x = (uint8_t)(((uint8_t)((a << 1) | carry_flag) << 1) | carry);
  a = y;
  return;
}

// in: x; out: x
void enemy_bg_collision(void) {
  if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x20) != 0) {
    return;
  }
  enemy_height_check();
  if (!carry_flag) {
    return;
  }
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  if (y == 0x12) {
    if (ram[(uint8_t)(ENEMY_Y + x)] < 0x25) {
      return;
    }
  }
  if (y == 0xe) {
    enemy_jump_check(); return;
  }
  if (y != 5) {
    if (y != 0x12) {
      if (y != 0x2e) {
        if (y >= 7) {
          return;
        }
      }
    }
    check_under_enemy();
    if (!zero_flag) {
      is_passable_metatile();
      if (!zero_flag) {
        if (a == 0x23) {
          y = ram[2];
          dynamic_ram_write(indirect_y_addr(6), 0);
          a = ram[(uint8_t)(ENEMY_KIND + x)];
          if (a >= 0x15) {
            check_stun_enemy(); return;
          }
          if (a == 6) {
            kill_enemy_on_block();
          }
          a = 1;
          spawn_floating_score();
          check_stun_enemy(); return;
        }
        if ((uint8_t)(ram[4] - 8) < 5) {
          if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x40) == 0) {
            if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x80) != 0) {
              enemy_side_check(); return;
            }
            a = ram[(uint8_t)(ENEMY_STATE + x)];
            if (a == 0) {
              enemy_side_check(); return;
            }
            if (a != 5) {
              if (a >= 3) {
                return;
              }
              if (ram[(uint8_t)(ENEMY_STATE + x)] == 2) {
                a = 0x10;
                y = ram[(uint8_t)(ENEMY_KIND + x)];
                if (y == 0x12) {
                  a = 0;
                }
                ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = a;
                ram[(uint8_t)(ENEMY_STATE + x)] = 3;
                land_enemy();
                return;
              }
            }
            a = ram[(uint8_t)(ENEMY_KIND + x)];
            if (a != 6) {
              if (a == 0x12) {
                ram[(uint8_t)(ENEMY_DIR + x)] = 1;
                ram[(uint8_t)(ENEMY_X_SPEED + x)] = 8;
                if ((ram[FRAME_COUNT] & 7) == 0) { goto loc_e0cd; }
              }
              y = 1;
              player_enemy_distance();
              if (neg_flag) {
                y = 2;
              }
              if (y == ram[(uint8_t)(ENEMY_DIR + x)]) {
                enemy_bump();
              }
            }
          }

loc_e0cd:
          land_enemy();
          if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x80) == 0) {
            ram[(uint8_t)(ENEMY_STATE + x)] = 0;
            return;
          }
          ram[(uint8_t)(ENEMY_STATE + x)] &= 0xbf;
          return;
        }
      }
    }
    if (ram[(uint8_t)(ENEMY_KIND + x)] == 3) {
      if (ram[(uint8_t)(ENEMY_STATE + x)] == 0) {
        enemy_bump(); return;
      }
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    y = a;
    if ((a & 0x80) != 0) {
      a = ram[(uint8_t)(ENEMY_STATE + x)] | 0x40;
    } else {
      a = ROM(ENEMY_BG_STATES + y);
    }
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    enemy_side_check(); return;
  }
  check_under_enemy();
  if (!zero_flag) {
    if (a == 0x23) {
      kill_enemy_on_block(); return;
    }
    if (ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] == 0) {
      ram[(uint8_t)(ENEMY_STATE + x)] &= 0x88;
      land_enemy();
      enemy_side_check(); return;
    }
  }
  ram[(uint8_t)(ENEMY_STATE + x)] |= 1;
  return;
}

// in: a, x; out: none
void check_stun_enemy(void) {
  if (a < 9) {
    stun_enemy(); return;
  }
  if (a >= 0x11) {
    stun_enemy(); return;
  }
  if (a >= 0xa) {
    if (a < 0xd) {
      stun_enemy(); return;
    }
  }
  ram[(uint8_t)(ENEMY_KIND + x)] = a & 1;
  stun_enemy(); return;
}

// in: x; out: y
void stun_enemy(void) {
  ram[(uint8_t)(ENEMY_STATE + x)] = (ram[(uint8_t)(ENEMY_STATE + x)] & 0xf0) | 2;
  ram[(uint8_t)(ENEMY_Y + x)]--;
  ram[(uint8_t)(ENEMY_Y + x)]--;
  if (ram[(uint8_t)(ENEMY_KIND + x)] != 7) {
    a = 0xfd;
    if (ram[AREA_KIND] != 0) { goto loc_e04a; }
  }
  a = 0xff;

loc_e04a:
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  y = 1;
  player_enemy_distance();
  if (neg_flag) {
    y = 2;
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x33) {
    if (a != 8) {
      ram[(uint8_t)(ENEMY_DIR + x)] = y;
    }
  }
  y--;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(ENEMY_BG_SPEEDS + y);
  return;
}

// in: x; out: x
void enemy_side_check(void) {
  if (ram[(uint8_t)(ENEMY_Y + x)] < 0x20) {
    return;
  }
  y = 0x16;
  ram[ENEMY_DATA_PTR + 2] = 2;
  do {
    if (ram[ENEMY_DATA_PTR + 2] == ram[(uint8_t)(ENEMY_DIR + x)]) {
      a = 1;
      enemy_block_check();
      if (!zero_flag) {
        is_passable_metatile();
        if (!zero_flag) {
          enemy_bump(); return;
        }
      }
    }
    ram[ENEMY_DATA_PTR + 2]--;
    y++;
  } while (y < 0x18);
  return;
}

// in: x; out: x, y
void enemy_bump(void) {
  if (x != 5) {
    if ((ram[(uint8_t)(ENEMY_STATE + x)] & 0x80) != 0) {
      ram[PULSE1_SFX_REQUEST] = 2;
    }
  }
  if (ram[(uint8_t)(ENEMY_KIND + x)] != 5) {
    reverse_enemy(); return;
  }
  ram[0] = 0;
  y = 0xfa;
  hammer_bro_jump(); return;
}

// in: x; out: carry, negative
void player_enemy_distance(void) {
  int diff = ram[(uint8_t)(ENEMY_X + x)] - ram[PLAYER_X];
  ram[0] = (uint8_t)diff;
  int diff2 = ram[(uint8_t)(ENEMY_PAGE + x)] - ram[PLAYER_PAGE] - (diff < 0);
  carry_flag = diff2 >= 0;
  neg_flag = ((uint8_t)diff2 & 0x80) != 0;
  return;
}

// in: x; out: none
void land_enemy(void) {
  clear_vertical_motion();
  ram[(uint8_t)(ENEMY_Y + x)] = (ram[(uint8_t)(ENEMY_Y + x)] & 0xf0) | 8;
  return;
}

// in: x; out: carry
void enemy_height_check(void) {
  carry_flag = (uint8_t)(ram[(uint8_t)(ENEMY_Y + x)] + 0x3e) >= 0x44;
  return;
}

// in: x; out: x
void enemy_jump_check(void) {
  enemy_height_check();
  if (!carry_flag) {
    enemy_side_check(); return;
  }
  if ((uint8_t)(ram[(uint8_t)(ENEMY_Y_SPEED + x)] + 2) < 3) {
    enemy_side_check(); return;
  }
  check_under_enemy();
  if (zero_flag) {
    enemy_side_check(); return;
  }
  is_passable_metatile();
  if (zero_flag) {
    enemy_side_check(); return;
  }
  land_enemy();
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfd;
  enemy_side_check(); return;
}

// in: x; out: y
void kill_enemy_on_block(void) {
  defeat_enemy();
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfc;
  return;
}

// in: x; out: a, x, y, zero
void check_under_enemy(void) {
  a = 0;
  y = 0x15;
  enemy_block_check(); return;
}

// in: a; out: zero
void is_passable_metatile(void) {
  zero_flag = a == 0x26;
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0xc2;
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0xc3;
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0x5f;
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0x60;
  return;
}

// in: x; out: x
void fireball_bg_collision(void) {
  if (ram[(uint8_t)(FIREBALL_Y + x)] >= 0x18) {
    fireball_block_check();
    if (!zero_flag) {
      is_passable_metatile();
      if (!zero_flag) {
        if ((ram[(uint8_t)(FIREBALL_Y_SPEED + x)] & 0x80) != 0) { goto loc_e1f4; }
        if (ram[(uint8_t)(FIREBALL_BOUNCING + x)] != 0) { goto loc_e1f4; }
        ram[(uint8_t)(FIREBALL_Y_SPEED + x)] = 0xfd;
        ram[(uint8_t)(FIREBALL_BOUNCING + x)] = 1;
        ram[(uint8_t)(FIREBALL_Y + x)] &= 0xf8;
        return;
      }
    }
  }
  ram[(uint8_t)(FIREBALL_BOUNCING + x)] = 0;
  return;

loc_e1f4:
  ram[(uint8_t)(FIREBALL_STATE + x)] = 0x80;
  ram[PULSE1_SFX_REQUEST] = 2;
  return;
}

// left, top, right and bottom offsets by box kind
// in: x; out: x
void fireball_box(void) {
  x = (uint8_t)(x + 7);
  y = 2;
  fireball_box_common(); return;
}

// in: x; out: x
void misc_box(void) {
  x = (uint8_t)(x + 9);
  y = 6;
  fireball_box_common(); return;
}

// in: x, y; out: x
void fireball_box_common(void) {
  compute_box();
  clip_box_to_screen(); return;
}

// in: x; out: x
void enemy_box(void) {
  ram[0] = 0x48;
  y = 0x44;
  enemy_box_offscreen_bits(); return;
}

// in: x; out: x
void small_platform_box(void) {
  ram[0] = 8;
  y = 4;
  enemy_box_offscreen_bits(); return;
}

// in: x, y; out: x
void enemy_box_offscreen_bits(void) {
  int diff = ram[(uint8_t)(ENEMY_X + x)] - ram[SCREEN_LEFT_X];
  ram[1] = (uint8_t)diff;
  a = (uint8_t)(ram[(uint8_t)(ENEMY_PAGE + x)] - ram[SCREEN_LEFT_PAGE] - (diff < 0));
  if ((a & 0x80) == 0) {
    if ((a | ram[1]) != 0) {
      y = ram[0];
    }
  }
  a = y & ram[ENEMY_OFFSCREEN];
  ram[ENEMY_OFFSCREEN_MASKED + x] = a;
  if (a != 0) { hide_box(); return; }
  enemy_box_core(); return;
}

// in: x; out: x
void large_platform_box(void) {
  x++;
  compute_x_offscreen_bits();
  x--;
  if (a >= 0xfe) { hide_box(); return; }
  enemy_box_core(); return;
}

// in: x; out: x
void enemy_box_core(void) {
  x = (uint8_t)(x + 1);
  y = 1;
  compute_box();
  clip_box_to_screen(); return;
}

// in: x; out: none
void hide_box(void) {
  y = (uint8_t)((uint8_t)(x << 1) << 1);
  ram[ENEMY_BOXES + y] = 0xff;
  ram[ENEMY_BOXES + 1 + y] = 0xff;
  ram[ENEMY_BOXES + 2 + y] = 0xff;
  ram[ENEMY_BOXES + 3 + y] = 0xff;
  return;
}

// in: x, y; out: x, y
void compute_box(void) {
  ram[0] = x;
  ram[2] = ram[OBJ_SCREEN_Y + y];
  ram[1] = ram[OBJ_SCREEN_X + y];
  a = (uint8_t)((uint8_t)(x << 1) << 1);
  ram[0x100 + sp] = a;
  y = a;
  x = (uint8_t)((uint8_t)(ram[OBJ_BOX_KIND + x] << 1) << 1);
  ram[BOXES + y] = (uint8_t)(ram[1] + ROM(BOX_SIZES + x));
  ram[BOX_RIGHT + y] = (uint8_t)(ram[1] + ROM(BOX_SIZES + 2 + x));
  x++;
  y++;
  ram[BOXES + y] = (uint8_t)(ram[2] + ROM(BOX_SIZES + x));
  ram[BOX_RIGHT + y] = (uint8_t)(ram[2] + ROM(BOX_SIZES + 2 + x));
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  y = ram[0x100 + sp];
  x = ram[0];
  return;
}

// in: x, y; out: x
void clip_box_to_screen(void) {
  uint16_t sum = (uint16_t)(ram[SCREEN_LEFT_X] + 0x80);
  ram[2] = (uint8_t)sum;
  ram[1] = (uint8_t)(ram[SCREEN_LEFT_PAGE] + (sum >= 0x100));
  carry_flag = ram[(uint8_t)(OBJ_X + x)] >= ram[2];
  carry_flag = (ram[(uint8_t)(OBJ_PAGE + x)] - ram[1] - !carry_flag) >= 0;
  if (carry_flag) {
    if ((ram[BOX_RIGHT + y] & 0x80) == 0) {
      if ((ram[BOXES + y] & 0x80) == 0) {
        ram[BOXES + y] = 0xff;
      }
      ram[BOX_RIGHT + y] = 0xff;
    }
    x = ram[SLOT];
    return;
  }
  a = ram[BOXES + y];
  if ((a & 0x80) != 0) {
    if (a >= 0xa0) {
      if ((ram[BOX_RIGHT + y] & 0x80) != 0) {
        ram[BOX_RIGHT + y] = 0;
      }
      ram[BOXES + y] = 0;
    }
  }
  x = ram[SLOT];
  return;
}

// in: y; out: y, carry
void player_collides(void) {
  x = 0;
  objects_collide(); return;
}

// bounding box overlap test, carry set on collision
// in: x, y; out: y, carry
void objects_collide(void) {
  ram[6] = y;
  ram[7] = 1;
  do {
    a = ram[BOXES + y];
    if (a < ram[BOXES + x]) {
      zero_flag = a == ram[BOX_RIGHT + x];
      if (a >= ram[BOX_RIGHT + x]) {
        if (zero_flag) { goto loc_e37e; }
        a = ram[BOX_RIGHT + y];
        if (a < ram[BOXES + y]) { goto loc_e37e; }
        carry_flag = a >= ram[BOXES + x];
        if (carry_flag) { goto loc_e37e; }
        y = ram[6];
        return;
      }
      if (ram[BOX_RIGHT + x] < ram[BOXES + x]) { goto loc_e37e; }
      carry_flag = ram[BOX_RIGHT + y] >= ram[BOXES + x];
      if (carry_flag) { goto loc_e37e; }
      y = ram[6];
      return;
    }
    if (a != ram[BOXES + x]) {
      zero_flag = a == ram[BOX_RIGHT + x];
      if (a >= ram[BOX_RIGHT + x]) {
        if (!zero_flag) {
          zero_flag = a == ram[BOX_RIGHT + y];
          if (a >= ram[BOX_RIGHT + y]) {
            if (!zero_flag) {
              if (ram[BOX_RIGHT + y] >= ram[BOXES + x]) { goto loc_e37e; }
            }
          }
          carry_flag = false;
          y = ram[6];
          return;
        }
      }
    }

loc_e37e:
    x++;
    y++;
    ram[7]--;
  } while ((ram[7] & 0x80) == 0);
  carry_flag = true;
  y = ram[6];
  return;
}

// in: a, x, y; out: a, x, y, zero
void enemy_block_check(void) {
  ram[0x100 + sp] = a;
  x = (uint8_t)(x + 1);
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  a = ram[0x100 + sp];
  block_check_common(); return;
}

// in: x; out: a, x, y, zero
void fireball_block_check(void) {
  y = 0x1a;
  x = (uint8_t)(x + 7);
  a = 0;
  block_check_common(); return;
}

// in: a, x, y; out: a, x, y, zero
void block_check_common(void) {
  block_probe();
  x = ram[SLOT];
  zero_flag = a == 0;
  return;
}

// in: y; out: a, y
void block_probe_feet(void) {
  y++;
  block_probe_head(); return;
}

// in: y; out: a, y, zero
void block_probe_head(void) {
  a = 0;
  block_probe_player(); return;
}

// in: y; out: a, y, zero
void block_probe_side(void) {
  a = 1;
  block_probe_player(); return;
}

// in: a, y; out: a, y, zero
void block_probe_player(void) {
  x = 0;
  block_probe(); return;
}

// metatile at an object's probe point
// in: a, x, y; out: a, y, zero
void block_probe(void) {
  ram[0x100 + sp] = a;
  sp--;
  ram[4] = y;
  uint16_t sum = (uint16_t)(ROM(BLOCK_PROBE_X + y) + ram[(uint8_t)(OBJ_X + x)]);
  ram[5] = (uint8_t)sum;
  a = (uint8_t)(ram[(uint8_t)(OBJ_PAGE + x)] + (sum >= 0x100)) & 1;
  carry_flag = (a & 1) != 0;
  a = (a >> 1) | ram[5];
  a = (uint8_t)(((a >> 1) | (carry_flag << 7)) >> 1 >> 1 >> 1);
  get_block_buffer_address();
  a = (uint8_t)(ram[(uint8_t)(OBJ_Y + x)] + ROM(BLOCK_PROBE_Y + ram[4]));
  a = (uint8_t)((a & 0xf0) - 0x20);
  ram[2] = a;
  ram[3] = read_byte((uint16_t)((ram[6] | (ram[7] << 8)) + a));
  y = ram[4];
  sp++;
  if (ram[0x100 + sp] == 0) {
    a = ram[(uint8_t)(OBJ_Y + x)];
  } else {
    a = ram[(uint8_t)(OBJ_X + x)];
  }
  ram[4] = a & 0xf;
  a = ram[3];
  zero_flag = a == 0;
  return;
}

// in: y; out: y
void draw_vine(void) {
  ram[0] = y;
  a = (uint8_t)(ram[ENEMY_SCREEN_Y] + ROM(VINE_Y_OFFSETS + y));
  y = ram[ENEMY_SPRITE_SLOT + ram[VINE_SLOTS + y]];
  ram[2] = y;
  stack_six_sprites();
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[SPRITE_X + 8 + y] = a;
  ram[SPRITE_X + 0x10 + y] = a;
  a = (uint8_t)(a + 6);
  ram[SPRITE_X + 4 + y] = a;
  ram[SPRITE_X + 0xc + y] = a;
  ram[SPRITE_X + 0x14 + y] = a;
  ram[SPRITE_ATTR + y] = 0x21;
  ram[SPRITE_X + 7 + y] = 0x21;
  ram[SPRITE_X + 0xf + y] = 0x21;
  ram[SPRITE_X + 3 + y] = 0x61;
  ram[SPRITE_X + 0xb + y] = 0x61;
  ram[SPRITE_X + 0x13 + y] = 0x61;
  x = 5;
  do {
    ram[SPRITE_TILE + y] = 0xe1;
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x--;
  } while ((x & 0x80) == 0);
  y = ram[2];
  if (ram[0] == 0) {
    ram[SPRITE_TILE + y] = 0xe0;
  }
  x = 0;
  do {
    if ((uint8_t)(ram[VINE_START_Y] - ram[SPRITE_Y + y]) >= 0x64) {
      ram[SPRITE_Y + y] = 0xf8;
    }
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x++;
  } while (x != 6);
  y = ram[0];
  return;
}

// in: a, y; out: y, carry
void stack_six_sprites(void) {
  x = 6;
  do {
    ram[SPRITE_Y + y] = a;
    uint16_t sum = (uint16_t)(a + 8);
    carry_flag = sum >= 0x100;
    a = (uint8_t)sum;
    y = (uint8_t)((uint8_t)(y + 3) + 1);
    x--;
  } while (x != 0);
  y = ram[2];
  return;
}

// in: x; out: x
void draw_hammer(void) {
  y = ram[MISC_SPRITE_SLOT + x];
  if (ram[TIMERS_PAUSED] == 0) {
    if ((ram[(uint8_t)(MISC_STATE + x)] & 0x7f) == 1) { goto loc_e4f0; }
  }
  x = 0;
  goto loc_e4f7;

loc_e4f0:
  x = (ram[FRAME_COUNT] >> 1 >> 1) & 3;

loc_e4f7:
  a = (uint8_t)(ram[MISC_SCREEN_Y] + ROM(HAMMER_FIRST_Y + x));
  ram[SPRITE_Y + y] = a;
  ram[SPRITE_X + 1 + y] = (uint8_t)(a + ROM(HAMMER_SECOND_Y + x));
  a = (uint8_t)(ram[MISC_SCREEN_X] + ROM(HAMMER_FIRST_X + x));
  ram[SPRITE_X + y] = a;
  uint16_t sum4 = (uint16_t)(a + ROM(HAMMER_SECOND_X + x));
  carry_flag = sum4 >= 0x100;
  ram[SPRITE_X + 4 + y] = (uint8_t)sum4;
  ram[SPRITE_TILE + y] = ROM(HAMMER_FIRST_TILES + x);
  ram[SPRITE_X + 2 + y] = ROM(HAMMER_SECOND_TILES + x);
  a = ROM(HAMMER_ATTRIBUTES + x);
  ram[SPRITE_ATTR + y] = a;
  ram[SPRITE_X + 3 + y] = a;
  x = ram[SLOT];
  if ((ram[MISC_OFFSCREEN] & 0xfc) == 0) {
    return;
  }
  ram[(uint8_t)(MISC_STATE + x)] = 0;
  a = 0xf8;
  fill_two_sprites();
  return;
}

// in: x; out: x
void draw_flagpole_flag(void) {
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  a = (uint8_t)(a + 8);
  ram[SPRITE_X + 4 + y] = a;
  ram[SPRITE_X + 8 + y] = a;
  uint16_t sum2 = (uint16_t)(a + 0xc);
  carry_flag = sum2 >= 0x100;
  ram[5] = (uint8_t)sum2;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  fill_two_sprites();
  uint16_t sum3 = (uint16_t)(a + 8 + carry_flag);
  carry_flag = sum3 >= 0x100;
  ram[SPRITE_X + 5 + y] = (uint8_t)sum3;
  ram[2] = ram[FLAG_SCORE_Y];
  ram[3] = 1;
  ram[4] = 1;
  ram[SPRITE_ATTR + y] = 1;
  ram[SPRITE_X + 3 + y] = 1;
  ram[SPRITE_X + 7 + y] = 1;
  ram[SPRITE_TILE + y] = 0x7e;
  ram[SPRITE_X + 6 + y] = 0x7e;
  ram[SPRITE_X + 2 + y] = 0x7f;
  if (ram[FLAGPOLE_CONTACT_Y] != 0) {
    y = (uint8_t)(y + 0xc);
    x = (uint8_t)(ram[FLAG_SCORE_KIND] << 1);
    ram[0] = ROM(FLAGPOLE_SCORE_TILES + x);
    a = ROM(FLAGPOLE_SCORE_TILES + 1 + x);
    draw_sprite_row();
  }
  x = ram[SLOT];
  y = ram[ENEMY_SPRITE_SLOT + x];
  if ((ram[ENEMY_OFFSCREEN] & 0xe) == 0) { return; }
  hide_six_sprites(); return;
}

// in: y; out: none
void hide_six_sprites(void) {
  a = 0xf8;
  fill_six_sprites(); return;
}

// in: a, y; out: none
void fill_six_sprites(void) {
  ram[SPRITE_X + 0x11 + y] = a;
  ram[SPRITE_X + 0xd + y] = a;
  fill_four_sprites(); return;
}

// in: a, y; out: none
void fill_four_sprites(void) {
  ram[SPRITE_X + 9 + y] = a;
  fill_three_sprites(); return;
}

// in: a, y; out: none
void fill_three_sprites(void) {
  ram[SPRITE_X + 5 + y] = a;
  fill_two_sprites(); return;
}

// in: a, y; out: none
void fill_two_sprites(void) {
  ram[SPRITE_X + 1 + y] = a;
  ram[SPRITE_Y + y] = a;
  return;
}

// in: x; out: x
void draw_large_platform(void) {
  y = ram[ENEMY_SPRITE_SLOT + x];
  ram[2] = y;
  y = (uint8_t)((uint8_t)(y + 2) + 1);
  a = ram[ENEMY_SCREEN_X];
  stack_six_sprites();
  x = ram[SLOT];
  a = ram[(uint8_t)(ENEMY_Y + x)];
  fill_four_sprites();
  y = ram[AREA_KIND];
  carry_flag = y >= 3;
  if (y != 3) {
    if (ram[HARD_MODE_LATE] == 0) { goto loc_e5eb; }
  }
  a = 0xf8;

loc_e5eb:
  y = ram[ENEMY_SPRITE_SLOT + x];
  ram[SPRITE_X + 0xd + y] = a;
  ram[SPRITE_X + 0x11 + y] = a;
  a = 0x5b;
  if (ram[CLOUD_OVERRIDE] != 0) {
    a = 0x75;
  }
  x = ram[SLOT];
  y++;
  fill_six_sprites();
  a = 2;
  y++;
  fill_six_sprites();
  x++;
  compute_x_offscreen_bits();
  x--;
  y = ram[ENEMY_SPRITE_SLOT + x];
  ram[0x100 + sp] = (uint8_t)(a << 1);
  sp--;
  if ((a & 0x80) != 0) {
    ram[SPRITE_Y + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = (uint8_t)(a << 1);
  sp--;
  if ((a & 0x80) != 0) {
    ram[SPRITE_X + 1 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = (uint8_t)(a << 1);
  sp--;
  if ((a & 0x80) != 0) {
    ram[SPRITE_X + 5 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = (uint8_t)(a << 1);
  sp--;
  if ((a & 0x80) != 0) {
    ram[SPRITE_X + 9 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = (uint8_t)(a << 1);
  sp--;
  if ((a & 0x80) != 0) {
    ram[SPRITE_X + 0xd + y] = 0xf8;
  }
  sp++;
  if ((ram[0x100 + sp] & 0x80) != 0) {
    ram[SPRITE_X + 0x11 + y] = 0xf8;
  }
  carry_flag = (ram[ENEMY_OFFSCREEN] & 0x80) != 0;
  if (!carry_flag) {
    return;
  }
  hide_six_sprites();
  return;
}

// in: x; out: x
void draw_coin_or_score(void) {
  goto draw_coin_or_score;
  do {
    carry_flag = (ram[FRAME_COUNT] & 1) != 0;
    if (!carry_flag) {
      ram[(uint8_t)(MISC_Y + x)]--;
    }
    a = ram[(uint8_t)(MISC_Y + x)];
    fill_two_sprites();
    a = ram[MISC_SCREEN_X];
    ram[SPRITE_X + y] = a;
    ram[SPRITE_X + 4 + y] = (uint8_t)(a + 8);
    ram[SPRITE_ATTR + y] = 2;
    ram[SPRITE_X + 3 + y] = 2;
    ram[SPRITE_TILE + y] = 0xf7;
    ram[SPRITE_X + 2 + y] = 0xfb;
    return;

draw_coin_or_score:
    y = ram[MISC_SPRITE_SLOT + x];
  } while (ram[(uint8_t)(MISC_STATE + x)] >= 2);
  a = ram[(uint8_t)(MISC_Y + x)];
  ram[SPRITE_Y + y] = a;
  ram[SPRITE_X + 1 + y] = (uint8_t)(a + 8);
  a = ram[MISC_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[SPRITE_X + 4 + y] = a;
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0;
  x = (a >> 1) & 3;
  a = ROM(JUMPING_COIN_TILES + x);
  y++;
  fill_two_sprites();
  y--;
  ram[SPRITE_ATTR + y] = 2;
  ram[SPRITE_X + 3 + y] = 0x82;
  x = ram[SLOT];
  return;
}

// in: none; out: x
void draw_powerup(void) {
  y = ram[ENEMY_SPRITE_SLOT + 5];
  ram[2] = (uint8_t)(ram[ENEMY_SCREEN_Y] + 8);
  ram[5] = ram[ENEMY_SCREEN_X];
  x = ram[POWERUP_KIND];
  ram[4] = ROM(POWERUP_ATTRIBUTES + x) | ram[ENEMY_SPRITE_ATTR + 5];
  a = x;
  ram[0x100 + sp] = a;
  sp--;
  x = (uint8_t)((uint8_t)(a << 1) << 1);
  ram[7] = 1;
  ram[3] = 1;
  do {
    ram[0] = ROM(POWERUP_TILES + x);
    a = ROM(POWERUP_TILES + 1 + x);
    draw_sprite_row();
    ram[7]--;
  } while ((ram[7] & 0x80) == 0);
  y = ram[ENEMY_SPRITE_SLOT + 5];
  sp++;
  a = ram[0x100 + sp];
  if (a == 0) {
    clip_enemy_sprites(); return;
  }
  if (a == 3) {
    clip_enemy_sprites(); return;
  }
  ram[0] = a;
  a = ((ram[FRAME_COUNT] >> 1) & 3) | ram[ENEMY_SPRITE_ATTR + 5];
  ram[SPRITE_ATTR + y] = a;
  ram[SPRITE_X + 3 + y] = a;
  if ((uint8_t)(ram[0] - 1) != 0) {
    ram[SPRITE_X + 7 + y] = a;
    ram[SPRITE_X + 0xb + y] = a;
  }
  ram[SPRITE_X + 3 + y] |= 0x40;
  ram[SPRITE_X + 0xb + y] |= 0x40;
  clip_enemy_sprites(); return;
}

// in: x; out: x
void draw_enemy(void) {
  ram[2] = ram[(uint8_t)(ENEMY_Y + x)];
  ram[5] = ram[ENEMY_SCREEN_X];
  ram[ENEMY_DATA_PTR + 2] = ram[ENEMY_SPRITE_SLOT + x];
  ram[FLIP_VERTICALLY] = 0;
  ram[3] = ram[(uint8_t)(ENEMY_DIR + x)];
  ram[4] = ram[ENEMY_SPRITE_ATTR + x];
  if (ram[(uint8_t)(ENEMY_KIND + x)] == 0xd) {
    if ((ram[(uint8_t)(ENEMY_X_SPEED + x)] & 0x80) == 0) {
      if (ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] != 0) {
        return;
      }
    }
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  ram[ENEMY_DATA_PTR + 4] = a;
  y = a & 0x1f;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x35) {
    y = 0;
    ram[3] = 1;
    a = 0x15;
  }
  if (a == 0x33) {
    ram[2]--;
    a = 3;
    if (ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] != 0) {
      a = 0x23;
    }
    ram[4] = a;
    y = 0;
    ram[ENEMY_DATA_PTR + 4] = 0;
    a = 8;
  }
  if (a == 0x32) {
    y = 3;
    a = ROM(SPRING_TILE_OFFSETS + ram[SPRING_ANIM_STEP]);
  }
  ram[ENEMY_DATA_PTR + 6] = a;
  ram[ENEMY_DATA_PTR + 3] = y;
  x = ram[SLOT];
  if (a == 0xc) {
    if ((ram[(uint8_t)(ENEMY_Y_SPEED + x)] & 0x80) == 0) {
      ram[FLIP_VERTICALLY]++;
    }
  }
  a = ram[BOWSER_GFX_FLAG];
  if (a != 0) {
    y = 0x16;
    if (a != 1) {
      y = 0x17;
    }
    ram[ENEMY_DATA_PTR + 6] = y;
  }
  y = ram[ENEMY_DATA_PTR + 6];
  if (y == 6) {
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    if (a >= 2) {
      ram[ENEMY_DATA_PTR + 3] = 4;
    }
    if (((a & 0x20) | ram[TIMERS_PAUSED]) == 0) {
      if ((ram[FRAME_COUNT] & 8) == 0) {
        ram[3] ^= 3;
      }
    }
  }
  ram[4] = ROM(ENEMY_ATTRIBUTES + y) | ram[4];
  x = ROM(ENEMY_TILE_OFFSETS + y);
  y = ram[ENEMY_DATA_PTR + 3];
  a = ram[BOWSER_GFX_FLAG];
  if (a != 0) {
    if (a != 1) { goto loc_e94c; }
    if ((ram[BOWSER_BODY] & 0x80) != 0) {
      x = 0xde;
    }
    if ((ram[ENEMY_DATA_PTR + 4] & 0x20) == 0) { goto loc_ea4b; }

loc_e946:
    ram[FLIP_VERTICALLY] = x;
    goto loc_ea4b;

loc_e94c:
    if ((ram[BOWSER_BODY] & 1) != 0) {
      x = 0xe4;
    }
    if ((ram[ENEMY_DATA_PTR + 4] & 0x20) == 0) { goto loc_ea4b; }
    ram[2] = (uint8_t)(ram[2] - 0x10);
    goto loc_e946;
  }
  if (x == 0x24) {
    if (y == 5) {
      x = 0x30;
      ram[3] = 2;
      ram[ENEMY_DATA_PTR + 3] = 5;
    }
  } else {
    if (x == 0x90) {
      if ((ram[ENEMY_DATA_PTR + 4] & 0x20) != 0) { goto loc_ea37; }
      if (ram[FRENZY_TIMER] >= 0x10) { goto loc_ea37; }
      x = 0x96;
      goto loc_ea37;
    }
    if (ram[ENEMY_DATA_PTR + 6] < 4) {
      if (y >= 2) {
        x = 0x5a;
        if (ram[ENEMY_DATA_PTR + 6] == 2) {
          x = 0x7e;
          ram[2]++;
        }
      }
    }
    if (ram[ENEMY_DATA_PTR + 3] == 4) {
      x = 0x72;
      ram[2]++;
      y = ram[ENEMY_DATA_PTR + 6];
      if (y != 2) {
        x = 0x66;
        ram[2]++;
      }
      if (y == 6) {
        x = 0x54;
        if ((ram[ENEMY_DATA_PTR + 4] & 0x20) == 0) {
          x = 0x8a;
          ram[2]--;
        }
      }
    }
  }
  y = ram[SLOT];
  if (ram[ENEMY_DATA_PTR + 6] == 5) {
    a = ram[ENEMY_DATA_PTR + 4];
    if (a != 0) {
      if ((a & 8) == 0) { goto loc_ea37; }
      x = 0xb4;
    }
  } else {
    if (x != 0x48) {
      a = ram[(ENEMY_INTERVAL_TIMER + y) & 0x7ff];
      if (a >= 5) { goto loc_ea37; }
      if (x == 0x3c) {
        if (a == 1) { goto loc_ea37; }
        ram[2]++;
        ram[2]++;
        ram[2]++;
        goto loc_ea29;
      }
    }
  }
  a = ram[ENEMY_DATA_PTR + 6];
  if (a == 6) { goto loc_ea37; }
  if (a == 8) { goto loc_ea37; }
  if (a == 0xc) { goto loc_ea37; }
  if (a >= 0x18) { goto loc_ea37; }
  if (a == 0x15) {
    if (ram[WORLD_NUMBER] >= 7) { goto loc_ea37; }
    x = 0xa2;
    ram[ENEMY_DATA_PTR + 3] = 3;
    goto loc_ea37;
  }
  if ((ram[FRAME_COUNT] & 8) == 0) {

loc_ea29:
    if (((ram[ENEMY_DATA_PTR + 4] & 0xa0) | ram[TIMERS_PAUSED]) == 0) {
      x = (uint8_t)(x + 6);
    }
  }

loc_ea37:
  if ((ram[ENEMY_DATA_PTR + 4] & 0x20) != 0) {
    if (ram[ENEMY_DATA_PTR + 6] >= 4) {
      ram[FLIP_VERTICALLY] = 1;
      ram[ENEMY_DATA_PTR + 3] = 0;
    }
  }

loc_ea4b:
  y = ram[ENEMY_DATA_PTR + 2];
  draw_enemy_row();
  draw_enemy_row();
  draw_enemy_row();
  x = ram[SLOT];
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_DATA_PTR + 6];
  carry_flag = a >= 8;
  if (a == 8) {
    clip_enemy_sprites(); return;
  }
  if (ram[FLIP_VERTICALLY] != 0) {
    a = ram[SPRITE_ATTR + y] | 0x80;
    y = (uint8_t)((uint8_t)(y + 1) + 1);
    fill_six_sprites();
    y = (uint8_t)((uint8_t)(y - 1) - 1);
    x = y;
    a = ram[ENEMY_DATA_PTR + 6];
    if (a != 5) {
      if (a != 0x11) {
        if (a < 0x15) {
          x = (uint8_t)(x + 8);
        }
      }
    }
    ram[0x100 + sp] = ram[SPRITE_TILE + x];
    sp--;
    ram[0x100 + sp] = ram[SPRITE_X + 2 + x];
    ram[SPRITE_TILE + x] = ram[SPRITE_X + 0xe + y];
    ram[SPRITE_X + 2 + x] = ram[SPRITE_X + 0x12 + y];
    sp = (uint8_t)((uint8_t)(sp - 1) + 1);
    ram[SPRITE_X + 0x12 + y] = ram[0x100 + sp];
    sp++;
    ram[SPRITE_X + 0xe + y] = ram[0x100 + sp];
  }
  if (ram[BOWSER_GFX_FLAG] != 0) {
    clip_enemy_sprites(); return;
  }
  a = ram[ENEMY_DATA_PTR + 6];
  x = ram[ENEMY_DATA_PTR + 3];
  if (a == 5) {
    clip_enemy_sprites(); return;
  }
  if (a != 7) {
    if (a != 0xd) {
      if (a != 0xc) {
        if (a == 0x12) {
          if (x != 5) { goto loc_eb12; }
        }
        if (a == 0x15) {
          ram[SPRITE_X + 0x13 + y] = 0x42;
        }
        if (x < 2) { goto loc_eb12; }
      }
    }
  }
  if (ram[BOWSER_GFX_FLAG] == 0) {
    a = ram[SPRITE_ATTR + y] & 0xa3;
    ram[SPRITE_ATTR + y] = a;
    ram[SPRITE_X + 7 + y] = a;
    ram[SPRITE_X + 0xf + y] = a;
    a |= 0x40;
    if (x == 5) {
      a |= 0x80;
    }
    ram[SPRITE_X + 3 + y] = a;
    ram[SPRITE_X + 0xb + y] = a;
    ram[SPRITE_X + 0x13 + y] = a;
    if (x == 4) {
      a = ram[SPRITE_X + 7 + y] | 0x80;
      ram[SPRITE_X + 7 + y] = a;
      ram[SPRITE_X + 0xf + y] = a;
      a |= 0x40;
      ram[SPRITE_X + 0xb + y] = a;
      ram[SPRITE_X + 0x13 + y] = a;
    }
  }

loc_eb12:
  if (ram[ENEMY_DATA_PTR + 6] == 0x11) {
    if (ram[FLIP_VERTICALLY] == 0) {
      ram[SPRITE_X + 0xf + y] &= 0x81;
      a = ram[SPRITE_X + 0x13 + y] | 0x41;
      ram[SPRITE_X + 0x13 + y] = a;
      if (ram[FRENZY_TIMER] >= 0x10) {
        clip_enemy_sprites(); return;
      }
      ram[SPRITE_X + 0xb + y] = a;
      ram[SPRITE_X + 7 + y] = a & 0x81;
      clip_enemy_sprites(); return;
    }
    ram[SPRITE_ATTR + y] &= 0x81;
    ram[SPRITE_X + 3 + y] |= 0x41;
  }
  if (ram[ENEMY_DATA_PTR + 6] < 0x18) {
    clip_enemy_sprites(); return;
  }
  ram[SPRITE_X + 7 + y] = 0x82;
  ram[SPRITE_X + 0xf + y] = 0x82;
  ram[SPRITE_X + 0xb + y] = 0xc2;
  ram[SPRITE_X + 0x13 + y] = 0xc2;
  clip_enemy_sprites(); return;
}

// in: none; out: x
void clip_enemy_sprites(void) {
  x = ram[SLOT];
  a = ram[ENEMY_OFFSCREEN] >> 1 >> 1;
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    a = 4;
    hide_enemy_column();
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    a = 0;
    hide_enemy_column();
  }
  sp++;
  a = ram[0x100 + sp] >> 1;
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    a = 0x10;
    hide_enemy_row();
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a >> 1;
  sp--;
  if ((a & 1) != 0) {
    a = 8;
    hide_enemy_row();
  }
  sp++;
  a = ram[0x100 + sp];
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (!carry_flag) {
    return;
  }
  hide_enemy_row();
  if (ram[(uint8_t)(ENEMY_KIND + x)] == 0xc) {
    return;
  }
  if (ram[(uint8_t)(ENEMY_Y_PAGE + x)] != 2) {
    return;
  }
  erase_enemy();
  return;
}

// in: x, y; out: x, y
void draw_enemy_row(void) {
  ram[0] = ROM(ENEMY_TILES + x);
  a = ROM(ENEMY_TILES + 1 + x);
  draw_sprite_row(); return;
}

// in: a, x, y; out: x, y, carry
void draw_sprite_row(void) {
  ram[1] = a;
  a = ram[0];
  if (((ram[3] >> 1) & 1) != 0) {
    ram[SPRITE_X + 2 + y] = a;
    ram[SPRITE_TILE + y] = ram[1];
    a = 0x40;
  } else {
    ram[SPRITE_TILE + y] = a;
    ram[SPRITE_X + 2 + y] = ram[1];
    a = 0;
  }
  a |= ram[4];
  ram[SPRITE_ATTR + y] = a;
  ram[SPRITE_X + 3 + y] = a;
  a = ram[2];
  ram[SPRITE_Y + y] = a;
  ram[SPRITE_X + 1 + y] = a;
  a = ram[5];
  ram[SPRITE_X + y] = a;
  ram[SPRITE_X + 4 + y] = (uint8_t)(a + 8);
  ram[2] = (uint8_t)(ram[2] + 8);
  uint16_t sum3 = (uint16_t)(y + 8);
  carry_flag = sum3 >= 0x100;
  y = (uint8_t)sum3;
  x = (uint8_t)((uint8_t)(x + 1) + 1);
  return;
}

// in: a, x; out: y
void hide_enemy_row(void) {
  uint16_t sum = (uint16_t)(a + ram[ENEMY_SPRITE_SLOT + x]);
  carry_flag = sum >= 0x100;
  y = (uint8_t)sum;
  a = 0xf8;
  fill_two_sprites(); return;
}

// in: a, x; out: none
void hide_enemy_column(void) {
  y = (uint8_t)(a + ram[ENEMY_SPRITE_SLOT + x]);
  hide_column();
  ram[SPRITE_X + 0xd + y] = a;
  return;
}

// in: x; out: x
void draw_block(void) {
  ram[2] = ram[BLOCK_SCREEN_Y];
  ram[5] = ram[BLOCK_SCREEN_X];
  ram[4] = 3;
  ram[3] = 1;
  y = ram[BLOCK_SPRITE_SLOT + x];
  x = 0;
  do {
    ram[0] = ROM(BLOCK_TILES + x);
    a = ROM(BLOCK_TILES + 1 + x);
    draw_sprite_row();
  } while (x != 4);
  x = ram[SLOT];
  y = ram[BLOCK_SPRITE_SLOT + x];
  if (ram[AREA_KIND] != 1) {
    ram[SPRITE_TILE + y] = 0x86;
    ram[SPRITE_X + 2 + y] = 0x86;
  }
  a = ram[BLOCK_METATILE + x];
  carry_flag = a >= 0xc4;
  if (a == 0xc4) {
    a = 0x87;
    y++;
    fill_four_sprites();
    y--;
    a = 3;
    if ((uint8_t)(ram[AREA_KIND] - 1) != 0) {
      a = 1;
    }
    x = ram[SLOT];
    ram[SPRITE_ATTR + y] = a;
    a |= 0x40;
    ram[SPRITE_X + 3 + y] = a;
    a |= 0x80;
    ram[SPRITE_X + 0xb + y] = a;
    ram[SPRITE_X + 7 + y] = a & 0x83;
  }
  a = ram[BLOCK_OFFSCREEN];
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 4) != 0) {
    ram[SPRITE_X + 1 + y] = 0xf8;
    ram[SPRITE_X + 9 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  check_left_column(); return;
}

// in: a, y; out: none
void check_left_column(void) {
  if ((a & 8) == 0) { return; }
  hide_column(); return;
}

// in: y; out: a
void hide_column(void) {
  a = 0xf8;
  ram[SPRITE_Y + y] = 0xf8;
  ram[SPRITE_X + 5 + y] = 0xf8;
  return;
}

// in: x; out: none
void draw_brick_chunks(void) {
  ram[0] = 2;
  a = 0x75;
  y = ram[PLAYER_TASK];
  carry_flag = y >= 5;
  if (y != 5) {
    ram[0] = 3;
    a = 0x84;
  }
  y = (uint8_t)(ram[BLOCK_SPRITE_SLOT + x] + 1);
  fill_four_sprites();
  a = (uint8_t)((uint8_t)((uint8_t)(ram[FRAME_COUNT] << 1) << 1) << 1);
  carry_flag = (a & 0x80) != 0;
  a = ((uint8_t)(a << 1) & 0xc0) | ram[0];
  y++;
  fill_four_sprites();
  y = (uint8_t)((uint8_t)(y - 1) - 1);
  a = ram[BLOCK_SCREEN_Y];
  fill_two_sprites();
  ram[SPRITE_X + y] = ram[BLOCK_SCREEN_X];
  a = (uint8_t)(ram[BLOCK_ORIGIN_X + x] - ram[SCREEN_LEFT_X]);
  ram[0] = a;
  int diff2 = a - ram[BLOCK_SCREEN_X];
  uint16_t sum = (uint16_t)((uint8_t)diff2 + ram[0] + (diff2 >= 0));
  a = (uint8_t)((uint8_t)sum + 6 + (sum >= 0x100));
  ram[SPRITE_X + 4 + y] = a;
  a = ram[BLOCK_SCREEN_Y + 1];
  ram[SPRITE_X + 5 + y] = a;
  ram[SPRITE_X + 9 + y] = a;
  ram[SPRITE_X + 8 + y] = ram[BLOCK_SCREEN_X + 1];
  int diff3 = ram[0] - ram[BLOCK_SCREEN_X + 1];
  uint16_t sum3 = (uint16_t)((uint8_t)diff3 + ram[0] + (diff3 >= 0));
  a = (uint8_t)((uint8_t)sum3 + 6 + (sum3 >= 0x100));
  ram[SPRITE_X + 0xc + y] = a;
  a = ram[BLOCK_OFFSCREEN];
  check_left_column();
  carry_flag = (ram[BLOCK_OFFSCREEN] & 0x80) != 0;
  if (carry_flag) {
    a = 0xf8;
    fill_two_sprites();
  }
  if ((ram[0] & 0x80) == 0) {
    return;
  }
  if (ram[SPRITE_X + y] < ram[SPRITE_X + 4 + y]) {
    return;
  }
  ram[SPRITE_X + 1 + y] = 0xf8;
  ram[SPRITE_X + 9 + y] = 0xf8;
  return;
}

// in: y; out: none
void draw_firebar_tile(void) {
  a = ram[FRAME_COUNT] >> 1 >> 1;
  ram[0x100 + sp] = a;
  ram[SPRITE_TILE + y] = (a & 1) ^ 0x64;
  sp = (uint8_t)((uint8_t)(sp - 1) + 1);
  a = 2;
  if (((ram[0x100 + sp] >> 1) & 1) != 0) {
    a = 0xc2;
  }
  ram[SPRITE_ATTR + y] = a;
  return;
}

// in: a, y; out: x
void draw_explosion(void) {
  x = a;
  a = ROM(EXPLOSION_TILES + x);
  y++;
  fill_four_sprites();
  y--;
  x = ram[SLOT];
  a = (uint8_t)(ram[FIREBALL_SCREEN_Y] - 4);
  ram[SPRITE_Y + y] = a;
  ram[SPRITE_X + 5 + y] = a;
  a = (uint8_t)(a + 8);
  ram[SPRITE_X + 1 + y] = a;
  ram[SPRITE_X + 9 + y] = a;
  a = (uint8_t)(ram[FIREBALL_SCREEN_X] - 4);
  ram[SPRITE_X + y] = a;
  ram[SPRITE_X + 4 + y] = a;
  a = (uint8_t)(a + 8);
  ram[SPRITE_X + 8 + y] = a;
  ram[SPRITE_X + 0xc + y] = a;
  ram[SPRITE_ATTR + y] = 2;
  ram[SPRITE_X + 3 + y] = 0x82;
  ram[SPRITE_X + 7 + y] = 0x42;
  ram[SPRITE_X + 0xb + y] = 0xc2;
  return;
}

// in: x; out: x
void draw_small_platform(void) {
  a = 0x5b;
  y = (uint8_t)(ram[ENEMY_SPRITE_SLOT + x] + 1);
  fill_six_sprites();
  y++;
  a = 2;
  fill_six_sprites();
  y = (uint8_t)((uint8_t)(y - 1) - 1);
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[SPRITE_X + 0xc + y] = a;
  a = (uint8_t)(a + 8);
  ram[SPRITE_X + 4 + y] = a;
  ram[SPRITE_X + 0x10 + y] = a;
  a = (uint8_t)(a + 8);
  ram[SPRITE_X + 8 + y] = a;
  ram[SPRITE_X + 0x14 + y] = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  x = a;
  ram[0x100 + sp] = a;
  sp--;
  carry_flag = x >= 0x20;
  if (!carry_flag) {
    a = 0xf8;
  }
  fill_three_sprites();
  sp++;
  a = (uint8_t)(ram[0x100 + sp] + 0x80);
  if (a < 0x20) {
    a = 0xf8;
  }
  ram[SPRITE_X + 9 + y] = a;
  ram[SPRITE_X + 0xd + y] = a;
  ram[SPRITE_X + 0x11 + y] = a;
  a = ram[ENEMY_OFFSCREEN];
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 8) != 0) {
    ram[SPRITE_Y + y] = 0xf8;
    ram[SPRITE_X + 9 + y] = 0xf8;
  }
  sp++;
  a = ram[0x100 + sp];
  ram[0x100 + sp] = a;
  sp--;
  if ((a & 4) != 0) {
    ram[SPRITE_X + 1 + y] = 0xf8;
    ram[SPRITE_X + 0xd + y] = 0xf8;
  }
  sp++;
  if ((ram[0x100 + sp] & 2) != 0) {
    ram[SPRITE_X + 5 + y] = 0xf8;
    ram[SPRITE_X + 0x11 + y] = 0xf8;
  }
  x = ram[SLOT];
  return;
}

// in: x; out: none
void draw_bubble(void) {
  if ((uint8_t)(ram[PLAYER_Y_PAGE] - 1) != 0) {
    return;
  }
  if ((ram[BUBBLE_OFFSCREEN] & 8) != 0) {
    return;
  }
  y = ram[BUBBLE_SPRITE_SLOT + x];
  ram[SPRITE_X + y] = ram[BUBBLE_SCREEN_X];
  ram[SPRITE_Y + y] = ram[BUBBLE_SCREEN_Y];
  ram[SPRITE_TILE + y] = 0x74;
  ram[SPRITE_ATTR + y] = 2;
  return;
}

void draw_player(void) {
  if (ram[INJURY_TIMER] != 0) {
    if ((ram[FRAME_COUNT] & 1) != 0) {
      return;
    }
  }
  if (ram[PLAYER_TASK] != 0xb) {
    if (ram[PLAYER_CHANGING_SIZE] == 0) {
      if (ram[SWIMMING] == 0) {
        draw_player_action(); return;
      }
      if (ram[PLAYER_STATE] == 0) {
        draw_player_action(); return;
      }
      draw_player_action();
      a = ram[FRAME_COUNT] & 4;
      if (a != 0) {
        return;
      }
      x = a;
      y = ram[PLAYER_SPRITE_SLOT];
      if ((ram[PLAYER_FACING] & 1) == 0) {
        y = (uint8_t)((uint8_t)(y + 3) + 1);
      }
      if (ram[PLAYER_SMALL] != 0) {
        if (ram[SPRITE_X + 0x16 + y] == 0x48) {
          return;
        }
        x++;
      }
      ram[SPRITE_X + 0x16 + y] = ROM(SWIM_KICK_TILES + x);
      return;
    }
    animate_size_change();
    draw_player_with_offset(); return;
  }
  a = 0xb0;
  draw_player_with_offset(); return;
}

void draw_player_action(void) {
  player_action_offset();
  draw_player_with_offset(); return;
}

// in: a; out: none
void draw_player_with_offset(void) {
  ram[PLAYER_GFX_OFFSET] = a;
  a = 4;
  render_player();
  fix_player_attributes();
  if (ram[FIREBALL_THROW_TIMER] != 0) {
    a = ram[PLAYER_ANIM_TIMER];
    carry_flag = a >= ram[FIREBALL_THROW_TIMER];
    ram[FIREBALL_THROW_TIMER] = 0;
    if (!carry_flag) {
      ram[FIREBALL_THROW_TIMER] = a;
      ram[PLAYER_GFX_OFFSET] = 0x58;
      y = 4;
      if ((ram[PLAYER_X_SPEED] | ram[BUTTONS_HORIZONTAL]) != 0) {
        y = 3;
      }
      a = y;
      render_player();
    }
  }
  ram[0] = ram[PLAYER_OFFSCREEN] >> 1 >> 1 >> 1 >> 1;
  x = 3;
  y = (uint8_t)(ram[PLAYER_SPRITE_SLOT] + 0x18);
  do {
    a = 0xf8;
    carry_flag = (ram[0] & 1) != 0;
    ram[0] = ram[0] >> 1;
    if (carry_flag) {
      fill_two_sprites();
    }
    y = (uint8_t)(y - 8);
    x--;
  } while ((x & 0x80) == 0);
  return;
}

void draw_lives_screen_player(void) {
  x = 5;
  do {
    ram[(uint8_t)(2 + x)] = ROM(LIVES_SCREEN_PLAYER + x);
    x--;
  } while ((x & 0x80) == 0);
  x = 0xb8;
  y = 4;
  draw_player_rows();
  ram[SPRITE_X + 0x1f] = ram[SPRITE_X + 0x23] | 0x40;
  return;
}

// in: a; out: none
void render_player(void) {
  ram[7] = a;
  a = ram[PLAYER_SCREEN_X];
  ram[PLAYER_SCROLL_POS] = a;
  ram[5] = a;
  ram[2] = ram[PLAYER_SCREEN_Y];
  ram[3] = ram[PLAYER_FACING];
  ram[4] = ram[PLAYER_SPRITE_ATTR];
  x = ram[PLAYER_GFX_OFFSET];
  y = ram[PLAYER_SPRITE_SLOT];
  draw_player_rows(); return;
}

// in: x, y; out: none
void draw_player_rows(void) {
  do {
    ram[0] = ROM(PLAYER_TILES + x);
    a = ROM(PLAYER_TILES + 1 + x);
    draw_sprite_row();
    ram[7]--;
  } while (ram[7] != 0);
  return;
}

// in: none; out: a
void player_action_offset(void) {
  a = ram[PLAYER_STATE];
  if (a == 3) { goto loc_f044; }
  if (a == 2) { goto loc_f034; }
  if (a == 1) {
    if (ram[SWIMMING] != 0) { goto loc_f050; }
    y = 6;
    if (ram[CROUCHING] == 0) {
      y = 0;
    }
  } else {
    y = 6;
    if (ram[CROUCHING] == 0) {
      y = 2;
      if ((ram[PLAYER_X_SPEED] | ram[BUTTONS_HORIZONTAL]) != 0) {
        if (ram[PLAYER_ABS_SPEED] < 9) { goto loc_f03c; }
        if ((ram[PLAYER_DIR] & ram[PLAYER_FACING]) != 0) { goto loc_f03c; }
        y = 3;
      }
    }
  }

loc_f028:
  size_offset();
  ram[PLAYER_ANIM_FRAME] = 0;
  a = ROM(PLAYER_TILE_OFFSETS + y);
  return;

loc_f034:
  y = 4;
  size_offset();
  animate_player(); return;

loc_f03c:
  y = 4;
  size_offset();
  goto loc_f068;

loc_f044:
  y = 5;
  if (ram[PLAYER_Y_SPEED] == 0) { goto loc_f028; }
  size_offset();
  goto loc_f06d;

loc_f050:
  y = 1;
  size_offset();
  if ((ram[JUMP_SWIM_TIMER] | ram[PLAYER_ANIM_FRAME]) == 0) {
    if ((ram[BUTTONS_AB] & 0x80) == 0) {
      animate_player(); return;
    }
  }

loc_f068:
  a = 3;
  goto loc_f06f;

loc_f06d:
  a = 2;

loc_f06f:
  ram[0] = a;
  animate_player();
  ram[0x100 + sp] = a;
  sp--;
  if (ram[PLAYER_ANIM_TIMER] == 0) {
    ram[PLAYER_ANIM_TIMER] = ram[PLAYER_ANIM_PERIOD];
    a = (uint8_t)(ram[PLAYER_ANIM_FRAME] + 1);
    if (a >= ram[0]) {
      a = 0;
    }
    ram[PLAYER_ANIM_FRAME] = a;
  }
  sp++;
  a = ram[0x100 + sp];
  return;
}

// in: y; out: a
void animate_player(void) {
  a = ram[PLAYER_ANIM_FRAME];
  animation_offset(); return;
}

// in: y; out: y
void size_offset(void) {
  if (ram[PLAYER_SMALL] == 0) {
    return;
  }
  y = (uint8_t)(y + 8);
  return;
}

// in: none; out: a
void animate_size_change(void) {
  y = ram[PLAYER_ANIM_FRAME];
  if ((ram[FRAME_COUNT] & 3) == 0) {
    y++;
    if (y >= 0xa) {
      y = 0;
      ram[PLAYER_CHANGING_SIZE] = 0;
    }
    ram[PLAYER_ANIM_FRAME] = y;
  }
  if (ram[PLAYER_SMALL] == 0) {
    a = ROM(CHANGE_SIZE_FRAMES + y);
    y = 0xf;
    animation_offset(); return;
  }
  x = (uint8_t)(y + 0xa);
  y = 9;
  if (ROM(CHANGE_SIZE_FRAMES + x) == 0) {
    y = 1;
  }
  a = ROM(PLAYER_TILE_OFFSETS + y);
  return;
}

// in: a, y; out: a
void animation_offset(void) {
  a = (uint8_t)((uint8_t)(a << 1) << 1);
  carry_flag = (a & 0x80) != 0;
  a = (uint8_t)((uint8_t)(a << 1) + ROM(PLAYER_TILE_OFFSETS + y) + carry_flag);
  return;
}

void fix_player_attributes(void) {
  y = ram[PLAYER_SPRITE_SLOT];
  if (ram[PLAYER_TASK] != 0xb) {
    a = ram[PLAYER_GFX_OFFSET];
    if (a == 0x50) { goto loc_f117; }
    if (a == 0xb8) { goto loc_f117; }
    if (a == 0xc0) { goto loc_f117; }
    if (a != 0xc8) {
      return;
    }
  }
  ram[SPRITE_X + 0xf + y] &= 0x3f;
  a = (ram[SPRITE_X + 0x13 + y] & 0x3f) | 0x40;
  ram[SPRITE_X + 0x13 + y] = a;

loc_f117:
  ram[SPRITE_X + 0x17 + y] &= 0x3f;
  a = (ram[SPRITE_X + 0x1b + y] & 0x3f) | 0x40;
  ram[SPRITE_X + 0x1b + y] = a;
  return;
}

// in: none; out: y
void player_screen_position(void) {
  x = 0;
  y = 0;
  screen_position_restore(); return;
}

// in: x; out: x
void bubble_screen_position(void) {
  y = 1;
  object_slot();
  y = 3;
  screen_position_restore(); return;
}

// in: x; out: x
void fireball_screen_position(void) {
  y = 0;
  object_slot();
  y = 2;
  screen_position_restore(); return;
}

// in: x, y; out: x
void screen_position_restore(void) {
  screen_position();
  x = ram[SLOT];
  return;
}

// in: x; out: x
void misc_screen_position(void) {
  y = 2;
  object_slot();
  y = 6;
  screen_position_restore(); return;
}

// in: x; out: a, x, y, carry
void enemy_screen_position(void) {
  a = 1;
  y = 1;
  screen_position_at(); return;
}

// in: x; out: x
void block_screen_position(void) {
  a = 9;
  y = 4;
  screen_position_at();
  x = (uint8_t)((uint8_t)(x + 1) + 1);
  a = 9;
  y = 5;
  screen_position_at(); return;
}

// in: a, x, y; out: a, x, carry
void screen_position_at(void) {
  ram[0] = x;
  x = (uint8_t)(a + ram[0]);
  screen_position();
  x = ram[SLOT];
  return;
}

// coordinates relative to the left edge of the screen
// in: x, y; out: a, carry
void screen_position(void) {
  ram[OBJ_SCREEN_Y + y] = ram[(uint8_t)(OBJ_Y + x)];
  int diff = ram[(uint8_t)(OBJ_X + x)] - ram[SCREEN_LEFT_X];
  carry_flag = diff >= 0;
  a = (uint8_t)diff;
  ram[OBJ_SCREEN_X + y] = a;
  return;
}

void player_offscreen_bits(void) {
  x = 0;
  y = 0;
  offscreen_bits(); return;
}

// in: x; out: x
void fireball_offscreen_bits(void) {
  y = 0;
  object_slot();
  y = 2;
  offscreen_bits(); return;
}

// in: x; out: x
void bubble_offscreen_bits(void) {
  y = 1;
  object_slot();
  y = 3;
  offscreen_bits(); return;
}

// in: x; out: x
void misc_offscreen_bits(void) {
  y = 2;
  object_slot();
  y = 6;
  offscreen_bits(); return;
}

// in: x, y; out: x
void object_slot(void) {
  x = (uint8_t)(x + ROM(OBJECT_SLOT_OFFSETS + y));
  return;
}

// in: x; out: x, y
void enemy_offscreen_bits(void) {
  a = 1;
  y = 1;
  offset_slot(); return;
}

// in: x; out: x
void block_offscreen_bits(void) {
  a = 9;
  y = 4;
  offset_slot(); return;
}

// in: a, x, y; out: x, y
void offset_slot(void) {
  ram[0] = x;
  x = (uint8_t)(a + ram[0]);
  offscreen_bits(); return;
}

// in: x, y; out: x, y
void offscreen_bits(void) {
  ram[0x100 + sp] = y;
  sp--;
  offscreen_bits_parts();
  a = (uint8_t)((uint8_t)((uint8_t)((uint8_t)(a << 1) << 1) << 1) << 1);
  ram[0] = a | ram[0];
  sp++;
  y = ram[0x100 + sp];
  ram[OBJ_OFFSCREEN + y] = ram[0];
  x = ram[SLOT];
  return;
}

// in: x; out: a
void offscreen_bits_parts(void) {
  compute_x_offscreen_bits();
  ram[0] = a >> 1 >> 1 >> 1 >> 1;
  ram[4] = x;
  y = 1;
  do {
    int diff = ROM(HIGH_POSITION_UNITS + y) - ram[(uint8_t)(OBJ_Y + x)];
    ram[7] = (uint8_t)diff;
    a = (uint8_t)(1 - ram[(uint8_t)(OBJ_Y_PAGE + x)] - (diff < 0));
    x = ROM(Y_EDGE_DEFAULTS + y);
    if ((a & 0x80) == 0) {
      x = ROM(Y_EDGE_DEFAULTS + 1 + y);
      if (((a - 1) & 0x80) != 0) {
        ram[6] = 0x20;
        a = 4;
        edge_distance_bits();
      }
    }
    a = ROM(Y_OFFSCREEN_BITS + x);
    x = ram[4];
    if (a != 0) {
      return;
    }
    y--;
  } while ((y & 0x80) == 0);
  return;
}

// in: x; out: a, x
void compute_x_offscreen_bits(void) {
  ram[4] = x;
  y = 1;
  do {
    int diff = ram[(SCREEN_LEFT_X + y) & 0x7ff] - ram[(uint8_t)(OBJ_X + x)];
    ram[7] = (uint8_t)diff;
    a = ram[(SCREEN_LEFT_PAGE + y) & 0x7ff];
    a = (uint8_t)(a - ram[(uint8_t)(OBJ_PAGE + x)] - (diff < 0));
    x = ROM(X_EDGE_DEFAULTS + y);
    if ((a & 0x80) == 0) {
      x = ROM(X_EDGE_DEFAULTS + 1 + y);
      if (((a - 1) & 0x80) != 0) {
        ram[6] = 0x38;
        a = 8;
        edge_distance_bits();
      }
    }
    a = ROM(X_OFFSCREEN_BITS + x);
    x = ram[4];
    if (a != 0) {
      return;
    }
    y--;
  } while ((y & 0x80) == 0);
  return;
}

// in: a, y; out: x
void edge_distance_bits(void) {
  ram[5] = a;
  a = ram[7];
  if (a >= ram[6]) {
    return;
  }
  a = (a >> 1 >> 1 >> 1) & 7;
  carry_flag = y >= 1;
  if (!carry_flag) {
    a = (uint8_t)(a + ram[5] + carry_flag);
  }
  x = a;
  return;
}

// per-frame sound engine entry point
void update_sound(void) {
  a = ram[GAME_MODE];
  if (a == 0) {
    apu_write(APU_STATUS, a);
    return;
  }
  apu_write(JOYPAD2_PORT, 0xff);
  apu_write(APU_STATUS, 0xf);
  if (ram[SOUND_PAUSED] == 0) {
    if (ram[PAUSE_SOUND_REQUEST] != 1) { goto loc_f34b; }
  }
  if (ram[PAUSE_SOUND_PLAYING] != 0) { goto loc_f316; }
  a = ram[PAUSE_SOUND_REQUEST];
  if (a == 0) { goto loc_f35d; }
  ram[PAUSE_SOUND_PLAYING] = a;
  ram[SOUND_PAUSED] = a;
  apu_write(APU_STATUS, 0);
  ram[PULSE1_SFX_PLAYING] = 0;
  ram[PULSE2_SFX_PLAYING] = 0;
  ram[NOISE_SFX_PLAYING] = 0;
  apu_write(APU_STATUS, 0xf);
  ram[PULSE1_SFX_COUNTER] = 0x2a;

loc_f312:
  a = 0x44;
  goto loc_f327;

loc_f316:
  a = ram[PULSE1_SFX_COUNTER];
  if (a != 0x24) {
    if (a == 0x1e) { goto loc_f312; }
    if (a != 0x18) { goto loc_f32e; }
  }
  a = 0x64;

loc_f327:
  x = 0x84;
  y = 0x7f;
  play_square1();

loc_f32e:
  ram[PULSE1_SFX_COUNTER]--;
  if (ram[PULSE1_SFX_COUNTER] != 0) { goto loc_f35d; }
  apu_write(APU_STATUS, 0);
  if (ram[PAUSE_SOUND_PLAYING] == 2) {
    ram[SOUND_PAUSED] = 0;
  }
  ram[PAUSE_SOUND_PLAYING] = 0;
  goto loc_f35d;

loc_f34b:
  square1_sfx();
  square2_sfx();
  noise_sfx();
  update_music();
  ram[AREA_MUSIC_REQUEST] = 0;
  ram[EVENT_MUSIC_REQUEST] = 0;

loc_f35d:
  ram[PULSE1_SFX_REQUEST] = 0;
  ram[PULSE2_SFX_REQUEST] = 0;
  ram[NOISE_SFX_REQUEST] = 0;
  ram[PAUSE_SOUND_REQUEST] = 0;
  y = ram[DMC_COUNTER];
  if ((ram[AREA_MUSIC_PLAYING] & 3) != 0) {
    ram[DMC_COUNTER]++;
    if (y < 0x30) { goto loc_f37d; }
  }
  if (y != 0) {
    ram[DMC_COUNTER]--;
  }

loc_f37d:
  apu_write((uint16_t)(APU + 0x11), y);
  return;
}

// in: x, y; out: none
void set_square1_regs(void) {
  apu_write((uint16_t)(APU + 1), y);
  apu_write(APU_PULSE1, x);
  return;
}

// in: a, x, y; out: a, x, zero
void play_square1(void) {
  set_square1_regs();
  set_square1_freq(); return;
}

// in: a; out: a, x, y, zero
void set_square1_freq(void) {
  x = 0;
  set_freq(); return;
}

// in: a, x; out: a, y, zero
void set_freq(void) {
  y = a;
  a = read_byte((uint16_t)(NOTE_PERIODS + 1 + y));
  zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  dynamic_ram_write((uint16_t)(APU + 2 + x), a);
  a = ROM(NOTE_PERIODS + y) | 8;
  zero_flag = a == 0;
  dynamic_ram_write((uint16_t)(APU + 3 + x), a);
  return;
}

// in: x, y; out: none
void set_square2_regs(void) {
  apu_write(APU_PULSE2, x);
  apu_write((uint16_t)(APU + 5), y);
  return;
}

// in: a, x, y; out: a, x
void play_square2(void) {
  set_square2_regs();
  set_square2_freq(); return;
}

// in: a; out: a, x, y, zero
void set_square2_freq(void) {
  x = 4;
  set_freq(); return;
}

// in: a; out: none
void set_triangle_freq(void) {
  x = 8;
  set_freq(); return;
}

// in: none; out: x
void square1_sfx(void) {
  goto square1_sfx;

loc_f3bf:
  ram[PULSE1_SFX_COUNTER] = 0x40;
  a = 0x62;
  set_square1_freq();
  x = 0x99;
  goto loc_f3f2;

loc_f3cd:
  a = 0x26;
  goto loc_f3d3;

loc_f3d1:
  a = 0x18;

loc_f3d3:
  x = 0x82;
  y = 0xa7;
  play_square1();
  ram[PULSE1_SFX_COUNTER] = 0x28;

loc_f3df:
  a = ram[PULSE1_SFX_COUNTER];
  if (a == 0x25) {
    x = 0x5f;
    y = 0xf6;
    goto loc_f3f4;
  }
  if (a != 0x20) { goto loc_f4a2; }
  x = 0x48;

loc_f3f2:
  y = 0xbc;

loc_f3f4:
  set_square1_regs();
  goto loc_f4a2;

loc_f3f9:
  a = 5;
  y = 0x99;
  goto loc_f403;

loc_f3ff:
  a = 0xa;
  y = 0x93;

loc_f403:
  x = 0x9e;
  ram[PULSE1_SFX_COUNTER] = a;
  a = 0xc;
  play_square1();
  do {

loc_f40d:
    a = ram[PULSE1_SFX_COUNTER];
    if (a != 6) { goto loc_f4a2; }
    a = 0xbb;
    apu_write((uint16_t)(APU + 1), 0xbb);
    goto loc_f4a2;

square1_sfx:
    y = ram[PULSE1_SFX_REQUEST];
    neg_flag = (y & 0x80) != 0;
    if (y != 0) {
      ram[PULSE1_SFX_PLAYING] = y;
      if (neg_flag) { goto loc_f3cd; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f3d1; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f3ff; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f45b; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f47d; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f4b6; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f3f9; }
      carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0;
      ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
      if (carry_flag) { goto loc_f3bf; }
    }
    a = ram[PULSE1_SFX_PLAYING];
    neg_flag = (a & 0x80) != 0;
    if (a == 0) {
      return;
    }
    if (neg_flag) { goto loc_f3df; }
    carry_flag = (a & 1) != 0;
    a = a >> 1;
    if (carry_flag) { goto loc_f3df; }
    carry_flag = (a & 1) != 0;
    a = a >> 1;
    if (carry_flag) { goto loc_f40d; }
    carry_flag = (a & 1) != 0;
    a = a >> 1;
    if (carry_flag) { goto loc_f469; }
    carry_flag = (a & 1) != 0;
    a = a >> 1;
    if (carry_flag) { goto loc_f48d; }
    carry_flag = (a & 1) != 0;
    a = a >> 1;
    if (carry_flag) { goto loc_f4bb; }
    carry_flag = (a & 1) != 0;
    a = a >> 1;
  } while (carry_flag);
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f4a2; }
  return;

loc_f45b:
  ram[PULSE1_SFX_COUNTER] = 0xe;
  y = 0x9c;
  x = 0x9e;
  a = 0x26;
  play_square1();

loc_f469:
  y = ram[PULSE1_SFX_COUNTER];
  a = ROM(SWIM_STOMP_ENVELOPE - 1 + y);
  apu_write(APU_PULSE1, a);
  if (y != 6) { goto loc_f4a2; }
  a = 0x9e;
  apu_write((uint16_t)(APU + 2), 0x9e);
  goto loc_f4a2;

loc_f47d:
  y = 0xcb;
  x = 0x9f;
  ram[PULSE1_SFX_COUNTER] = 0xe;
  a = 0x28;
  play_square1();
  if (zero_flag) {

loc_f48d:
    if (ram[PULSE1_SFX_COUNTER] == 8) {
      apu_write((uint16_t)(APU + 2), 0xa0);
      a = 0x9f;
    } else {
      a = 0x90;
    }
    apu_write(APU_PULSE1, a);
  }

loc_f4a2:
  ram[PULSE1_SFX_COUNTER]--;
  if (ram[PULSE1_SFX_COUNTER] != 0) { return; }
  stop_square1_sfx(); return;

loc_f4b6:
  ram[PULSE1_SFX_COUNTER] = 0x2f;

loc_f4bb:
  a = ram[PULSE1_SFX_COUNTER];
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f4a2; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f4a2; }
  a &= 2;
  if (a == 0) { goto loc_f4a2; }
  y = 0x91;
  x = 0x9a;
  a = 0x44;
  play_square1();
  goto loc_f4a2;
}

// in: none; out: x
void stop_square1_sfx(void) {
  ram[PULSE1_SFX_PLAYING] = 0;
  apu_write(APU_STATUS, 0xe);
  x = 0xf;
  apu_write(APU_STATUS, 0xf);
  return;
}

// in: none; out: x
void stop_square2_sfx(void) {
  apu_write(APU_STATUS, 0xd);
  x = 0xf;
  apu_write(APU_STATUS, 0xf);
  return;
}

// in: none; out: x
void square2_sfx(void) {
  goto square2_sfx;

loc_f518:
  a = 0x35;
  x = 0x8d;
  goto loc_f522;

loc_f51e:
  a = 6;
  x = 0x98;

loc_f522:
  ram[PULSE2_SFX_COUNTER] = a;
  y = 0x7f;
  a = 0x42;
  play_square2();

loc_f52c:
  a = ram[PULSE2_SFX_COUNTER];
  if (a != 0x30) { goto loc_f568; }
  a = 0x54;
  apu_write((uint16_t)(APU + 6), 0x54);
  goto loc_f568;

loc_f53a:
  ram[PULSE2_SFX_COUNTER] = 0x20;
  y = 0x94;
  a = 0x5e;
  goto loc_f5de;

loc_f545:
  a = ram[PULSE2_SFX_COUNTER];
  if (a != 0x18) { goto loc_f568; }
  y = 0x93;
  a = 0x18;
  goto loc_f5de;

loc_f552:
  ram[PULSE2_SFX_COUNTER] = 0x36;

loc_f557:
  a = ram[PULSE2_SFX_COUNTER];
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f568; }
  a = ROM(EXTRA_LIFE_NOTES + 5 + a);
  x = 0x5d;
  y = 0x7f;

loc_f565:
  play_square2();

loc_f568:
  ram[PULSE2_SFX_COUNTER]--;
  if (ram[PULSE2_SFX_COUNTER] != 0) {
    return;
  }

loc_f56d:
  ram[PULSE2_SFX_PLAYING] = 0;
  stop_square2_sfx(); return;

square2_sfx:
  if ((ram[PULSE2_SFX_PLAYING] & 0x40) != 0) { goto loc_f5e7; }
  y = ram[PULSE2_SFX_REQUEST];
  neg_flag = (y & 0x80) != 0;
  if (y != 0) {
    ram[PULSE2_SFX_PLAYING] = y;
    if (neg_flag) { goto loc_f5c8; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f518; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f5fc; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f600; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f53a; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f51e; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f552; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0;
    ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f5e2; }
  }
  a = ram[PULSE2_SFX_PLAYING];
  neg_flag = (a & 0x80) != 0;
  if (a == 0) {
    return;
  }
  if (neg_flag) { goto loc_f5d3; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f52c; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f60f; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f60f; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f545; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f52c; }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f557; }
  if ((a & 1) != 0) { goto loc_f5e7; }
  return;

loc_f5c8:
  ram[PULSE2_SFX_COUNTER] = 0x38;
  y = 0xc4;
  a = 0x18;
  goto loc_f5de;

loc_f5d3:
  a = ram[PULSE2_SFX_COUNTER];
  if (a != 8) { goto loc_f568; }
  y = 0xa4;
  a = 0x5a;

loc_f5de:
  x = 0x9f;
  goto loc_f565;

loc_f5e2:
  ram[PULSE2_SFX_COUNTER] = 0x30;

loc_f5e7:
  a = ram[PULSE2_SFX_COUNTER];
  x = 3;
  do {
    carry_flag = (a & 1) != 0;
    a = a >> 1;
    if (carry_flag) { goto loc_f568; }
    x--;
  } while (x != 0);
  a = ROM(EXTRA_LIFE_NOTES - 1 + a);
  x = 0x82;
  y = 0x7f;
  goto loc_f565;

loc_f5fc:
  a = 0x10;
  goto loc_f602;

loc_f600:
  a = 0x20;

loc_f602:
  ram[PULSE2_SFX_COUNTER] = a;
  apu_write((uint16_t)(APU + 5), 0x7f);
  ram[SFX_STEP] = 0;

loc_f60f:
  ram[SFX_STEP]++;
  a = ram[SFX_STEP] >> 1;
  y = a;
  if (y == ram[PULSE2_SFX_COUNTER]) { goto loc_f56d; }
  apu_write(APU_PULSE2, 0x9d);
  a = ROM(POWERUP_REVEAL_NOTES + y);
  set_square2_freq();
  return;
}

// in: none; out: x
void noise_sfx(void) {
  goto noise_sfx;

loc_f63b:
  ram[NOISE_SFX_COUNTER] = 0x20;

loc_f640:
  a = ram[NOISE_SFX_COUNTER];
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (!carry_flag) { goto loc_f658; }
  y = a;
  x = ROM(BRICK_SHATTER_NOTES + y);
  a = read_byte((uint16_t)(BRICK_SHATTER_ENVELOPE + y));

loc_f64d:
  apu_write(APU_NOISE, a);
  apu_write((uint16_t)(APU + 0xe), x);
  apu_write((uint16_t)(APU + 0xf), 0x18);

loc_f658:
  ram[NOISE_SFX_COUNTER]--;
  if (ram[NOISE_SFX_COUNTER] != 0) {
    return;
  }
  apu_write(APU_NOISE, 0xf0);
  ram[NOISE_SFX_PLAYING] = 0;
  return;

noise_sfx:
  y = ram[NOISE_SFX_REQUEST];
  if (y != 0) {
    ram[NOISE_SFX_PLAYING] = y;
    carry_flag = (ram[NOISE_SFX_REQUEST] & 1) != 0;
    ram[NOISE_SFX_REQUEST] = ram[NOISE_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f63b; }
    carry_flag = (ram[NOISE_SFX_REQUEST] & 1) != 0;
    ram[NOISE_SFX_REQUEST] = ram[NOISE_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f680; }
  }
  a = ram[NOISE_SFX_PLAYING];
  if (a == 0) {
    return;
  }
  carry_flag = (a & 1) != 0;
  a = a >> 1;
  if (carry_flag) { goto loc_f640; }
  if ((a & 1) != 0) { goto loc_f685; }
  return;

loc_f680:
  ram[NOISE_SFX_COUNTER] = 0x40;

loc_f685:
  x = 0xf;
  a = read_byte((uint16_t)(WATER_MUSIC_ENVELOPE + 0x27 + (ram[NOISE_SFX_COUNTER] >> 1)));
  if (a != 0) { goto loc_f64d; }
  continue_music(); return;
}

// in: none; out: x
void continue_music(void) {
  goto loc_f73a;

loc_f6a4:
  ram[EVENT_MUSIC_PLAYING] = a;
  if (a == 1) {
    stop_square1_sfx();
    stop_square2_sfx();
  }
  x = ram[AREA_MUSIC_PLAYING];
  ram[AREA_MUSIC_SAVED] = x;
  y = 0;
  ram[NOTE_LENGTH_ADD] = 0;
  ram[AREA_MUSIC_PLAYING] = 0;
  if (a != 0x40) { goto loc_f6f1; }
  x = 8;
  ram[NOTE_LENGTH_ADD] = 8;
  goto loc_f6f1;

loc_f6d1:
  ram[GROUND_MUSIC_HEADER] = 0x11;

loc_f6d4:
  ram[EVENT_MUSIC_PLAYING] = 0;
  ram[AREA_MUSIC_PLAYING] = a;
  if (a == 1) {
    ram[GROUND_MUSIC_HEADER]++;
    y = ram[GROUND_MUSIC_HEADER];
    if (y != 0x32) { goto loc_f6f5; }
    goto loc_f6d1;
  }
  y = 8;
  ram[PULSE2_MUSIC_POS] = 8;
  do {

loc_f6f1:
    y++;
    carry_flag = (a & 1) != 0;
    a = a >> 1;
  } while (!carry_flag);

loc_f6f5:
  y = ROM(SONG_HEADER_OFFSETS - 1 + y);
  ram[NOTE_LENGTH_OFFSET] = ROM(SONG_HEADER_OFFSETS + y);
  ram[MUSIC_PTR] = ROM(SONG_HEADER_OFFSETS + 1 + y);
  ram[MUSIC_PTR + 1] = ROM(SONG_HEADER_OFFSETS + 2 + y);
  ram[TRIANGLE_MUSIC_POS] = ROM(SONG_HEADER_OFFSETS + 3 + y);
  ram[PULSE1_MUSIC_POS] = ROM(SONG_HEADER_OFFSETS + 4 + y);
  a = ROM(SONG_HEADER_OFFSETS + 5 + y);
  ram[NOISE_MUSIC_POS] = a;
  ram[NOISE_LOOP_POS] = a;
  ram[PULSE2_NOTE_COUNTER] = 1;
  ram[PULSE1_NOTE_COUNTER] = 1;
  ram[TRIANGLE_NOTE_COUNTER] = 1;
  ram[NOISE_BEAT_COUNTER] = 1;
  ram[PULSE2_MUSIC_POS] = 0;
  ram[ALT_REGISTERS] = 0;
  apu_write(APU_STATUS, 0xb);
  apu_write(APU_STATUS, 0xf);

loc_f73a:
  ram[PULSE2_NOTE_COUNTER]--;
  if (ram[PULSE2_NOTE_COUNTER] == 0) {
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    neg_flag = (a & 0x80) != 0;
    if (a != 0) {
      if (!neg_flag) { goto loc_f786; }
      goto loc_f77a;
    }
    a = ram[EVENT_MUSIC_PLAYING];
    if (a == 0x40) {
      a = ram[AREA_MUSIC_SAVED];
      if (a != 0) { goto loc_f6d4; }
    }
    a &= 4;
    if (a != 0) { goto loc_f6a4; }
    a = ram[AREA_MUSIC_PLAYING] & 0x5f;
    if (a != 0) { goto loc_f6d4; }
    ram[AREA_MUSIC_PLAYING] = 0;
    ram[EVENT_MUSIC_PLAYING] = 0;
    apu_write(APU_TRIANGLE, 0);
    apu_write(APU_PULSE1, 0x90);
    apu_write(APU_PULSE2, 0x90);
    return;

loc_f77a:
    note_length();
    ram[PULSE2_NOTE_LENGTH] = a;
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));

loc_f786:
    x = ram[PULSE2_SFX_PLAYING];
    if (x == 0) {
      set_square2_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE2_ENVELOPE] = a;
      set_square2_regs();
    }
    ram[PULSE2_NOTE_COUNTER] = ram[PULSE2_NOTE_LENGTH];
  }
  if (ram[PULSE2_SFX_PLAYING] == 0) {
    if ((ram[EVENT_MUSIC_PLAYING] & 0x91) == 0) {
      y = ram[PULSE2_ENVELOPE];
      if (y != 0) {
        ram[PULSE2_ENVELOPE]--;
      }
      load_envelope_entry();
      apu_write(APU_PULSE2, a);
      x = 0x7f;
      apu_write((uint16_t)(APU + 5), 0x7f);
    }
  }
  if (ram[PULSE1_MUSIC_POS] != 0) {
    ram[PULSE1_NOTE_COUNTER]--;
    if (ram[PULSE1_NOTE_COUNTER] == 0) {

loc_f7c5:
      y = ram[PULSE1_MUSIC_POS];
      ram[PULSE1_MUSIC_POS]++;
      a = read_byte(indirect_y_addr(MUSIC_PTR));
      if (a == 0) {
        apu_write(APU_PULSE1, 0x83);
        apu_write((uint16_t)(APU + 1), 0x94);
        ram[ALT_REGISTERS] = 0x94;
        goto loc_f7c5;
      }
      alternate_length();
      ram[PULSE1_NOTE_COUNTER] = a;
      if (ram[PULSE1_SFX_PLAYING] != 0) { goto loc_f81a; }
      a = x & 0x3e;
      set_square1_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE1_ENVELOPE] = a;
      set_square1_regs();
    }
    if (ram[PULSE1_SFX_PLAYING] == 0) {
      if ((ram[EVENT_MUSIC_PLAYING] & 0x91) == 0) {
        y = ram[PULSE1_ENVELOPE];
        if (y != 0) {
          ram[PULSE1_ENVELOPE]--;
        }
        load_envelope_entry();
        apu_write(APU_PULSE1, a);
      }
      a = ram[ALT_REGISTERS];
      if (a == 0) {
        a = 0x7f;
      }
      apu_write((uint16_t)(APU + 1), a);
    }
  }

loc_f81a:
  ram[TRIANGLE_NOTE_COUNTER]--;
  if (ram[TRIANGLE_NOTE_COUNTER] == 0) {
    y = ram[TRIANGLE_MUSIC_POS];
    ram[TRIANGLE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    neg_flag = (a & 0x80) != 0;
    if (a != 0) {
      if (neg_flag) {
        note_length();
        ram[TRIANGLE_NOTE_LENGTH] = a;
        apu_write(APU_TRIANGLE, 0x1f);
        y = ram[TRIANGLE_MUSIC_POS];
        ram[TRIANGLE_MUSIC_POS]++;
        a = read_byte(indirect_y_addr(MUSIC_PTR));
        if (a == 0) { goto loc_f86a; }
      }
      set_triangle_freq();
      x = ram[TRIANGLE_NOTE_LENGTH];
      ram[TRIANGLE_NOTE_COUNTER] = x;
      if ((ram[EVENT_MUSIC_PLAYING] & 0x6e) == 0) {
        if ((ram[AREA_MUSIC_PLAYING] & 0xa) == 0) { goto loc_f86d; }
      }
      if (x < 0x12) {
        if ((ram[EVENT_MUSIC_PLAYING] & 8) != 0) {
          a = 0xf;
        } else {
          a = 0x1f;
        }
      } else {
        a = 0xff;
      }
    }

loc_f86a:
    apu_write(APU_TRIANGLE, a);
  }

loc_f86d:
  if ((ram[AREA_MUSIC_PLAYING] & 0xf3) == 0) {
    return;
  }
  ram[NOISE_BEAT_COUNTER]--;
  if (ram[NOISE_BEAT_COUNTER] != 0) {
    return;
  }
  do {
    y = ram[NOISE_MUSIC_POS];
    ram[NOISE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    if (a != 0) { goto loc_f88a; }
    a = ram[NOISE_LOOP_POS];
    ram[NOISE_MUSIC_POS] = a;
  } while (a != 0);

loc_f88a:
  alternate_length();
  ram[NOISE_BEAT_COUNTER] = a;
  a = x & 0x3e;
  if (a != 0) {
    if (a != 0x30) {
      if (a != 0x20) {
        if ((a & 0x10) == 0) { goto loc_f8b9; }
        a = 0x1c;
        x = 3;
        y = 0x18;
      } else {
        a = 0x1c;
        x = 0xc;
        y = 0x18;
      }
    } else {
      a = 0x1c;
      x = 3;
      y = 0x58;
    }
  } else {

loc_f8b9:
    a = 0x10;
  }
  apu_write(APU_NOISE, a);
  apu_write((uint16_t)(APU + 0xe), x);
  apu_write((uint16_t)(APU + 0xf), y);
  return;
}

void update_music(void) {
  a = ram[EVENT_MUSIC_REQUEST];
  if (a == 0) {
    a = ram[AREA_MUSIC_REQUEST];
    if (a != 0) { goto loc_f6c8; }
    if ((ram[EVENT_MUSIC_PLAYING] | ram[AREA_MUSIC_PLAYING]) != 0) { continue_music(); return; }
    return;
  }

loc_f6a4:
  ram[EVENT_MUSIC_PLAYING] = a;
  if (a == 1) {
    stop_square1_sfx();
    stop_square2_sfx();
  }
  x = ram[AREA_MUSIC_PLAYING];
  ram[AREA_MUSIC_SAVED] = x;
  y = 0;
  ram[NOTE_LENGTH_ADD] = 0;
  ram[AREA_MUSIC_PLAYING] = 0;
  if (a != 0x40) { goto loc_f6f1; }
  x = 8;
  ram[NOTE_LENGTH_ADD] = 8;
  goto loc_f6f1;

loc_f6c8:
  if (a == 4) {
    stop_square1_sfx();
  }
  y = 0x10;

loc_f6d1:
  ram[GROUND_MUSIC_HEADER] = y;

loc_f6d4:
  ram[EVENT_MUSIC_PLAYING] = 0;
  ram[AREA_MUSIC_PLAYING] = a;
  if (a == 1) {
    ram[GROUND_MUSIC_HEADER]++;
    y = ram[GROUND_MUSIC_HEADER];
    if (y != 0x32) { goto loc_f6f5; }
    y = 0x11;
    goto loc_f6d1;
  }
  y = 8;
  ram[PULSE2_MUSIC_POS] = 8;
  do {

loc_f6f1:
    y++;
    carry_flag = (a & 1) != 0;
    a = a >> 1;
  } while (!carry_flag);

loc_f6f5:
  y = ROM(SONG_HEADER_OFFSETS - 1 + y);
  ram[NOTE_LENGTH_OFFSET] = ROM(SONG_HEADER_OFFSETS + y);
  ram[MUSIC_PTR] = ROM(SONG_HEADER_OFFSETS + 1 + y);
  ram[MUSIC_PTR + 1] = ROM(SONG_HEADER_OFFSETS + 2 + y);
  ram[TRIANGLE_MUSIC_POS] = ROM(SONG_HEADER_OFFSETS + 3 + y);
  ram[PULSE1_MUSIC_POS] = ROM(SONG_HEADER_OFFSETS + 4 + y);
  a = ROM(SONG_HEADER_OFFSETS + 5 + y);
  ram[NOISE_MUSIC_POS] = a;
  ram[NOISE_LOOP_POS] = a;
  ram[PULSE2_NOTE_COUNTER] = 1;
  ram[PULSE1_NOTE_COUNTER] = 1;
  ram[TRIANGLE_NOTE_COUNTER] = 1;
  ram[NOISE_BEAT_COUNTER] = 1;
  ram[PULSE2_MUSIC_POS] = 0;
  ram[ALT_REGISTERS] = 0;
  apu_write(APU_STATUS, 0xb);
  apu_write(APU_STATUS, 0xf);
  ram[PULSE2_NOTE_COUNTER]--;
  if (ram[PULSE2_NOTE_COUNTER] == 0) {
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    neg_flag = (a & 0x80) != 0;
    if (a != 0) {
      if (!neg_flag) { goto loc_f786; }
      goto loc_f77a;
    }
    a = ram[EVENT_MUSIC_PLAYING];
    if (a == 0x40) {
      a = ram[AREA_MUSIC_SAVED];
      if (a != 0) { goto loc_f6d4; }
    }
    a &= 4;
    if (a != 0) { goto loc_f6a4; }
    a = ram[AREA_MUSIC_PLAYING] & 0x5f;
    if (a != 0) { goto loc_f6d4; }
    ram[AREA_MUSIC_PLAYING] = 0;
    ram[EVENT_MUSIC_PLAYING] = 0;
    apu_write(APU_TRIANGLE, 0);
    apu_write(APU_PULSE1, 0x90);
    apu_write(APU_PULSE2, 0x90);
    return;

loc_f77a:
    note_length();
    ram[PULSE2_NOTE_LENGTH] = a;
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));

loc_f786:
    x = ram[PULSE2_SFX_PLAYING];
    if (x == 0) {
      set_square2_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE2_ENVELOPE] = a;
      set_square2_regs();
    }
    ram[PULSE2_NOTE_COUNTER] = ram[PULSE2_NOTE_LENGTH];
  }
  if (ram[PULSE2_SFX_PLAYING] == 0) {
    if ((ram[EVENT_MUSIC_PLAYING] & 0x91) == 0) {
      y = ram[PULSE2_ENVELOPE];
      if (y != 0) {
        ram[PULSE2_ENVELOPE]--;
      }
      load_envelope_entry();
      apu_write(APU_PULSE2, a);
      x = 0x7f;
      apu_write((uint16_t)(APU + 5), 0x7f);
    }
  }
  if (ram[PULSE1_MUSIC_POS] != 0) {
    ram[PULSE1_NOTE_COUNTER]--;
    if (ram[PULSE1_NOTE_COUNTER] == 0) {

loc_f7c5:
      y = ram[PULSE1_MUSIC_POS];
      ram[PULSE1_MUSIC_POS]++;
      a = read_byte(indirect_y_addr(MUSIC_PTR));
      if (a == 0) {
        apu_write(APU_PULSE1, 0x83);
        apu_write((uint16_t)(APU + 1), 0x94);
        ram[ALT_REGISTERS] = 0x94;
        goto loc_f7c5;
      }
      alternate_length();
      ram[PULSE1_NOTE_COUNTER] = a;
      if (ram[PULSE1_SFX_PLAYING] != 0) { goto loc_f81a; }
      a = x & 0x3e;
      set_square1_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE1_ENVELOPE] = a;
      set_square1_regs();
    }
    if (ram[PULSE1_SFX_PLAYING] == 0) {
      if ((ram[EVENT_MUSIC_PLAYING] & 0x91) == 0) {
        y = ram[PULSE1_ENVELOPE];
        if (y != 0) {
          ram[PULSE1_ENVELOPE]--;
        }
        load_envelope_entry();
        apu_write(APU_PULSE1, a);
      }
      a = ram[ALT_REGISTERS];
      if (a == 0) {
        a = 0x7f;
      }
      apu_write((uint16_t)(APU + 1), a);
    }
  }

loc_f81a:
  ram[TRIANGLE_NOTE_COUNTER]--;
  if (ram[TRIANGLE_NOTE_COUNTER] == 0) {
    y = ram[TRIANGLE_MUSIC_POS];
    ram[TRIANGLE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    neg_flag = (a & 0x80) != 0;
    if (a != 0) {
      if (neg_flag) {
        note_length();
        ram[TRIANGLE_NOTE_LENGTH] = a;
        apu_write(APU_TRIANGLE, 0x1f);
        y = ram[TRIANGLE_MUSIC_POS];
        ram[TRIANGLE_MUSIC_POS]++;
        a = read_byte(indirect_y_addr(MUSIC_PTR));
        if (a == 0) { goto loc_f86a; }
      }
      set_triangle_freq();
      x = ram[TRIANGLE_NOTE_LENGTH];
      ram[TRIANGLE_NOTE_COUNTER] = x;
      if ((ram[EVENT_MUSIC_PLAYING] & 0x6e) == 0) {
        if ((ram[AREA_MUSIC_PLAYING] & 0xa) == 0) { goto loc_f86d; }
      }
      if (x < 0x12) {
        if ((ram[EVENT_MUSIC_PLAYING] & 8) != 0) {
          a = 0xf;
        } else {
          a = 0x1f;
        }
      } else {
        a = 0xff;
      }
    }

loc_f86a:
    apu_write(APU_TRIANGLE, a);
  }

loc_f86d:
  if ((ram[AREA_MUSIC_PLAYING] & 0xf3) == 0) {
    return;
  }
  ram[NOISE_BEAT_COUNTER]--;
  if (ram[NOISE_BEAT_COUNTER] != 0) {
    return;
  }
  do {
    y = ram[NOISE_MUSIC_POS];
    ram[NOISE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    if (a != 0) { goto loc_f88a; }
    a = ram[NOISE_LOOP_POS];
    ram[NOISE_MUSIC_POS] = a;
  } while (a != 0);

loc_f88a:
  alternate_length();
  ram[NOISE_BEAT_COUNTER] = a;
  a = x & 0x3e;
  if (a != 0) {
    if (a != 0x30) {
      if (a != 0x20) {
        if ((a & 0x10) == 0) { goto loc_f8b9; }
        a = 0x1c;
        x = 3;
        y = 0x18;
      } else {
        a = 0x1c;
        x = 0xc;
        y = 0x18;
      }
    } else {
      a = 0x1c;
      x = 3;
      y = 0x58;
    }
  } else {

loc_f8b9:
    a = 0x10;
  }
  apu_write(APU_NOISE, a);
  apu_write((uint16_t)(APU + 0xe), x);
  apu_write((uint16_t)(APU + 0xf), y);
  return;
}

// in: a; out: a, x, y
void alternate_length(void) {
  x = a;
  carry_flag = (a & 1) != 0;
  a = x;
  bool carry2 = (a & 0x80) != 0;
  a = (uint8_t)((a << 1) | carry_flag);
  bool carry3 = (a & 0x80) != 0;
  a = (uint8_t)(((uint8_t)((a << 1) | carry2) << 1) | carry3);
  note_length(); return;
}

// in: a; out: a, y
void note_length(void) {
  uint16_t sum = (uint16_t)((a & 7) + ram[NOTE_LENGTH_OFFSET]);
  y = (uint8_t)((uint8_t)sum + ram[NOTE_LENGTH_ADD] + (sum >= 0x100));
  a = read_byte((uint16_t)(NOTE_LENGTHS + y));
  return;
}

// in: none; out: a, x, y
void load_square_envelope(void) {
  if ((ram[EVENT_MUSIC_PLAYING] & 8) != 0) {
    a = 4;
  } else {
    if ((ram[AREA_MUSIC_PLAYING] & 0x7d) != 0) {
      a = 8;
    } else {
      a = 0x28;
    }
  }
  x = 0x82;
  y = 0x7f;
  return;
}

// in: y; out: a
void load_envelope_entry(void) {
  if ((ram[EVENT_MUSIC_PLAYING] & 8) != 0) {
    a = read_byte((uint16_t)(CASTLE_CLEAR_ENVELOPE + y));
    return;
  }
  if ((ram[AREA_MUSIC_PLAYING] & 0x7d) != 0) {
    a = read_byte((uint16_t)(AREA_MUSIC_ENVELOPE + y));
    return;
  }
  a = read_byte((uint16_t)(WATER_MUSIC_ENVELOPE + y));
  return;
}

void cpu_reset_entry(void) { reset(); }
void cpu_nmi_entry(void) { frame_interrupt(); }
