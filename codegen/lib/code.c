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
  cpu_push_return_address(return_address);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  y = a; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  a = ram[0x100 + ++sp]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  ram[0x4] = a;
  a = ram[0x100 + ++sp]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  ram[0x5] = a;
  y++; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  a = read_byte(indirect_y_addr(0x4)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  ram[0x6] = a;
  y++; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  a = read_byte(indirect_y_addr(0x4)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  ram[0x7] = a;
  return cpu_dispatch_target = read_indirect_word(0x6);
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
  if (a != 0x2) {
    if (a != 0x1) {
      return;
    }
    a = ram[GAME_MODE_TASK];
    if (a != 0x3) {
      return;
    }
  }
  a = ram[PAUSE_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    ram[PAUSE_TIMER]--;
    return;
  }
  a = ram[JOYPAD1];
  a &= 0x10; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[PAUSE_STATE];
    a &= 0x80; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
    ram[PAUSE_TIMER] = 0x2b;
    a = ram[PAUSE_STATE];
    y = a;
    y++;
    ram[PAUSE_SOUND_REQUEST] = y;
    a ^= 0x1;
    a |= 0x80; zero_flag = a == 0;
    if (!zero_flag) { goto loc_81c2; }
  }
  a = ram[PAUSE_STATE];
  a &= 0x7f;
  
loc_81c2:
  ram[PAUSE_STATE] = a;
  return;
}

// rotate OAM slots so that dropped sprites flicker instead of vanishing
void shuffle_sprite_slots(void) {
  y = ram[AREA_KIND];
  ram[0x0] = 0x28;
  x = 0xe;
  do {
    a = ram[SPRITE_SLOTS + x];
    carry_flag = a >= ram[0x0];
    if (carry_flag) {
      y = ram[SHUFFLE_POS];
      carry_flag = false;
      { uint16_t sum = a + ram[SHUFFLE_STEPS + y] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      if (carry_flag) {
        carry_flag = false;
        a = (uint8_t)(a + ram[0x0] + carry_flag);
      }
      ram[SPRITE_SLOTS + x] = a;
    }
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  x = ram[SHUFFLE_POS];
  x++;
  if (x == 0x3) {
    x = 0x0;
  }
  ram[SHUFFLE_POS] = x;
  x = 0x8;
  y = 0x2;
  do {
    a = ram[(ENEMY_SPRITE_SLOT + 4) + y];
    ram[FIREBALL_SPRITE_SLOT + x] = a;
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[(FIREBALL_SPRITE_SLOT + 1) + x] = a;
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[MISC_SPRITE_SLOT + x] = a;
    x--;
    x--;
    x--;
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
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
  y = 0x0;
  
  hide_sprites_from(); return;
}

void hide_sprites_but_zero(void) {
  y = 0x4;
  hide_sprites_from(); // fallthrough
  return;
}

// in: y; out: y
void hide_sprites_from(void) {
  a = 0xf8;
  do {
    ram[SPRITE_Y + y] = a;
    y++;
    y++;
    y++;
    y++; zero_flag = y == 0;
  } while (!zero_flag);
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
  y = 0x0;
  a = ram[JOYPAD1];
  a |= ram[JOYPAD2];
  if (a != 0x10) {
    zero_flag = a == 0x90;
    if (!zero_flag) { goto loc_8258; }
  }
  goto loc_82d8;
  
loc_8258:
  if (a != 0x20) {
    x = ram[DEMO_TIMER]; zero_flag = x == 0;
    if (zero_flag) {
      ram[SELECT_TIMER] = a;
      play_demo();
      if (carry_flag) { goto loc_82c9; }
      goto loc_82c0;
    }
    x = ram[WORLD_SELECT_UNLOCKED]; zero_flag = x == 0;
    if (zero_flag) { goto loc_82bb; }
    zero_flag = a == 0x40;
    if (!zero_flag) { goto loc_82bb; }
    y++;
  }
  a = ram[DEMO_TIMER]; zero_flag = a == 0;
  if (zero_flag) { goto loc_82c9; }
  ram[DEMO_TIMER] = 0x18;
  a = ram[SELECT_TIMER]; zero_flag = a == 0;
  if (zero_flag) {
    ram[SELECT_TIMER] = 0x10;
    if (y != 0x1) {
      a = ram[PLAYER_COUNT];
      a ^= 0x1;
      ram[PLAYER_COUNT] = a;
      draw_player_select_icon();
      goto loc_82bb;
    }
    x = ram[WORLD_SELECT];
    x++;
    a = x;
    a &= 0x7;
    ram[WORLD_SELECT] = a;
    start_continue();
    do {
      ram[VRAM_LIST1_LENGTH + x] = ROM(WORLD_SELECT_TEMPLATE + x);
      x++;
      neg_flag = ((x - 0x6) & 0x80) != 0;
    } while (neg_flag);
    y = ram[WORLD_NUMBER];
    y++;
    ram[(VRAM_LIST1 + 3)] = y;
  }
  
loc_82bb:
  ram[JOYPAD1] = 0x0;
  
loc_82c0:
  run_game_frame();
  a = ram[PLAYER_TASK];
  if (a != 0x6) {
    return;
  }
  
loc_82c9:
  a = 0x0;
  ram[GAME_MODE] = a;
  ram[GAME_MODE_TASK] = a;
  ram[STATUS_BAR_SPLIT] = a;
  ram[SCREEN_OFF]++;
  return;
  
loc_82d8:
  y = ram[DEMO_TIMER]; zero_flag = y == 0;
  if (zero_flag) { goto loc_82c9; }
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (carry_flag) {
    a = ram[CONTINUE_WORLD];
    start_continue();
  }
  load_area_pointer();
  ram[HIDDEN_1UP]++;
  ram[OTHER_HIDDEN_1UP]++;
  ram[RELOAD_TIME_LIMIT]++;
  ram[GAME_MODE]++;
  ram[HARD_MODE] = ram[WORLD_SELECT_UNLOCKED];
  a = 0x0;
  ram[GAME_MODE_TASK] = a;
  ram[DEMO_TIMER] = a;
  x = 0x17;
  a = 0x0;
  do {
    ram[(PLAYER_SCORE + x) & 0x7ff] = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: a; out: x
void start_continue(void) {
  ram[WORLD_NUMBER] = a;
  ram[OTHER_WORLD_NUMBER] = a;
  x = 0x0;
  ram[AREA_NUMBER] = x;
  ram[OTHER_AREA_NUMBER] = x;
  return;
}

void draw_player_select_icon(void) {
  y = 0x7;
  do {
    ram[VRAM_LIST1_LENGTH + y] = ROM(PLAYER_SELECT_ICON_DATA + y);
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  a = ram[PLAYER_COUNT]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  ram[(VRAM_LIST1 + 3)] = 0x24;
  ram[(VRAM_LIST1 + 5)] = 0xce;
  return;
}

// in: none; out: carry
void play_demo(void) {
  x = ram[DEMO_ACTION];
  a = ram[DEMO_ACTION_TIMER]; zero_flag = a == 0;
  if (zero_flag) {
    x++;
    ram[DEMO_ACTION]++;
    carry_flag = true;
    a = ROM((DEMO_BUTTONS + 20) + x); zero_flag = a == 0;
    ram[DEMO_ACTION_TIMER] = a;
    if (zero_flag) {
      return;
    }
  }
  ram[JOYPAD1] = ROM((DEMO_BUTTONS - 1) + x);
  ram[DEMO_ACTION_TIMER]--;
  carry_flag = false;
  return;
}

void victory_mode(void) {
  run_victory_task();
  a = ram[GAME_MODE_TASK]; zero_flag = a == 0;
  if (!zero_flag) {
    x = 0x0;
    ram[SLOT] = x;
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
  x = ram[SCREEN_RIGHT_PAGE];
  x++;
  ram[TARGET_PAGE] = x;
  ram[EVENT_MUSIC_REQUEST] = 0x8;
  next_game_mode_task_b(); return;
}

void victory_walk(void) {
  y = 0x0;
  ram[VICTORY_WALK_STEP] = y;
  a = ram[PLAYER_PAGE];
  zero_flag = a == ram[TARGET_PAGE];
  if (zero_flag) {
    a = ram[PLAYER_X];
    carry_flag = a >= 0x60;
    if (carry_flag) { goto loc_83d0; }
  }
  ram[VICTORY_WALK_STEP]++;
  y++;
  
loc_83d0:
  a = y;
  drive_player();
  a = ram[SCREEN_LEFT_PAGE];
  zero_flag = a == ram[TARGET_PAGE];
  if (!zero_flag) {
    a = ram[SCROLL_FRACTION];
    carry_flag = false;
    { uint16_t sum = a + 0x80 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[SCROLL_FRACTION] = a;
    a = 0x1;
    a = (uint8_t)(a + 0x0 + carry_flag);
    y = a;
    scroll_screen();
    update_scroll_column();
    ram[VICTORY_WALK_STEP]++;
  }
  a = ram[VICTORY_WALK_STEP]; zero_flag = a == 0;
  if (zero_flag) {
    next_game_mode_task();
    return;
  }
  return;
}

void show_victory_messages(void) {
  a = ram[MESSAGE_STEP2]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[MESSAGE_STEP]; zero_flag = a == 0;
    if (!zero_flag) {
      carry_flag = a >= 0x9;
      if (carry_flag) { goto loc_8443; }
      y = ram[WORLD_NUMBER];
      if (y == 0x7) {
        carry_flag = a >= 0x3;
        if (!carry_flag) { goto loc_8443; }
        a = (uint8_t)(a - 0x1 - !carry_flag);
        goto loc_8418;
      }
      carry_flag = a >= 0x2;
      if (!carry_flag) { goto loc_8443; }
    }
    
loc_8418:
    y = a; zero_flag = y == 0;
    if (zero_flag) {
      a = ram[CURRENT_PLAYER]; zero_flag = a == 0;
      if (zero_flag) { goto loc_8434; }
      y++; zero_flag = y == 0;
      if (!zero_flag) { goto loc_8434; }
    }
    y++;
    a = ram[WORLD_NUMBER];
    if (a != 0x7) {
      y--;
      carry_flag = y >= 0x4;
      if (carry_flag) { goto loc_8456; }
      carry_flag = y >= 0x3;
      if (carry_flag) { goto loc_8443; }
    }
    
loc_8434:
    if (y == 0x3) {
      ram[EVENT_MUSIC_REQUEST] = 0x4;
    }
    a = y;
    carry_flag = false;
    a = (uint8_t)(a + 0xc + carry_flag);
    ram[VRAM_LIST_SELECT] = a;
  }
  
loc_8443:
  a = ram[MESSAGE_STEP2];
  carry_flag = false;
  { uint16_t sum = a + 0x4 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[MESSAGE_STEP2] = a;
  a = ram[MESSAGE_STEP];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[MESSAGE_STEP] = a;
  carry_flag = a >= 0x7;
  
loc_8456:
  if (!carry_flag) {
    return;
  }
  ram[WORLD_END_TIMER] = 0x6;
  next_game_mode_task(); // fallthrough
  return;
}

void next_game_mode_task(void) {
  ram[GAME_MODE_TASK]++;
  return;
}

void finish_world(void) {
  a = ram[WORLD_END_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  y = ram[WORLD_NUMBER];
  if (y < 0x7) {
    a = 0x0;
    ram[AREA_NUMBER] = a;
    ram[LEVEL_NUMBER] = a;
    ram[GAME_MODE_TASK] = a;
    ram[WORLD_NUMBER]++;
    load_area_pointer();
    ram[RELOAD_TIME_LIMIT]++;
    ram[GAME_MODE] = 0x1;
    return;
  }
  a = ram[JOYPAD1];
  a |= ram[JOYPAD2];
  a &= 0x40; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  ram[WORLD_SELECT_UNLOCKED] = 0x1;
  ram[LIVES] = 0xff;
  end_game();
  return;
}

// in: x; out: x
void update_score_popups(void) {
  a = ram[POPUP_KIND + x]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  if (a >= 0xb) {
    a = 0xb;
    ram[POPUP_KIND + x] = a;
  }
  y = a;
  a = ram[POPUP_TIMER + x]; zero_flag = a == 0;
  if (zero_flag) {
    ram[POPUP_KIND + x] = a;
    return;
  }
  ram[POPUP_TIMER + x]--;
  if (a == 0x2b) {
    if (y == 0xb) {
      ram[LIVES]++;
      ram[PULSE2_SFX_REQUEST] = 0x40;
    }
    a = ROM(POPUP_SCORE_DIGITS + y);
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    x = a;
    a = ROM(POPUP_SCORE_DIGITS + y);
    a &= 0xf;
    ram[SCORE_ADD + x] = a;
    add_score();
  }
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x12) {
    zero_flag = a == 0xd;
    if (zero_flag) { goto loc_852b; }
    if (a != 0x5) {
      zero_flag = a == 0xa;
      if (zero_flag) { goto loc_852b; }
      zero_flag = a == 0xb;
      if (zero_flag) { goto loc_852b; }
      carry_flag = a >= 0x9;
      if (carry_flag) { goto loc_8523; }
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      carry_flag = a >= 0x2;
      if (carry_flag) { goto loc_852b; }
    }
    
loc_8523:
    x = ram[SPRITE_SLOT_CTRL];
    y = ram[BLOCK_SPRITE_SLOT + x];
    x = ram[SLOT];
  }
  
loc_852b:
  a = ram[POPUP_Y + x];
  carry_flag = a >= 0x18;
  if (carry_flag) {
    { uint16_t diff = (uint16_t)(a - 0x1 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[POPUP_Y + x] = a;
  }
  a = ram[POPUP_Y + x];
  { uint16_t diff = (uint16_t)(a - 0x8 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  fill_two_sprites();
  a = ram[POPUP_X + x];
  ram[SPRITE_X + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  a = 0x2;
  ram[SPRITE_ATTR + y] = a;
  ram[(SPRITE_X + 3) + y] = a;
  a = ram[POPUP_KIND + x];
  a = (uint8_t)(a << 1);
  x = a;
  ram[SPRITE_TILE + y] = ROM(POPUP_TILES + x);
  ram[(SPRITE_X + 2) + y] = ROM((POPUP_TILES + 1) + x);
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
  a = ram[GAME_MODE]; zero_flag = a == 0;
  if (zero_flag) {
    next_screen_task();
    return;
  }
  x = 0x3;
  select_vram_list(); return;
}

void intermission_setup(void) {
  a = ram[BACKGROUND_COLOR];
  ram[0x100 + sp--] = a;
  a = ram[PLAYER_POWER];
  ram[0x100 + sp--] = a;
  ram[PLAYER_POWER] = 0x0;
  ram[BACKGROUND_COLOR] = 0x2;
  queue_player_colors();
  a = ram[0x100 + ++sp];
  ram[PLAYER_POWER] = a;
  a = ram[0x100 + ++sp];
  ram[BACKGROUND_COLOR] = a;
  next_screen_task_b(); return;
}

void queue_area_palette(void) {
  y = ram[AREA_KIND];
  x = ROM(AREA_PALETTE_LISTS + y);
  select_vram_list(); // fallthrough
  return;
}

// in: x; out: none
void select_vram_list(void) {
  ram[VRAM_LIST_SELECT] = x;
  next_screen_task(); // fallthrough
  return;
}

void next_screen_task(void) {
  next_screen_task_b(); return;
}

void queue_background_color(void) {
  y = ram[BACKGROUND_COLOR]; zero_flag = y == 0;
  if (!zero_flag) {
    ram[VRAM_LIST_SELECT] = ROM((BACKGROUND_COLOR_LISTS - 4) + y);
  }
  ram[SCREEN_TASK]++;
  queue_player_colors(); // fallthrough
  return;
}

// in: none; out: x
void queue_player_colors(void) {
  x = ram[VRAM_LIST1_LENGTH];
  y = 0x0;
  a = ram[CURRENT_PLAYER]; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x4;
  }
  a = ram[PLAYER_POWER];
  if (a == 0x2) {
    y = 0x8;
  }
  ram[0x0] = 0x3;
  do {
    ram[(VRAM_LIST1 + 3) + x] = ROM(PLAYER_COLORS + y);
    y++;
    x++;
    ram[0x0]--; neg_flag = (ram[0x0] & 0x80) != 0;
  } while (!neg_flag);
  x = ram[VRAM_LIST1_LENGTH];
  y = ram[BACKGROUND_COLOR]; zero_flag = y == 0;
  if (zero_flag) {
    y = ram[AREA_KIND];
  }
  ram[(VRAM_LIST1 + 3) + x] = ROM(BACKGROUND_COLORS + y);
  ram[VRAM_LIST1 + x] = 0x3f;
  ram[(VRAM_LIST1 + 1) + x] = 0x10;
  ram[(VRAM_LIST1 + 2) + x] = 0x4;
  ram[(VRAM_LIST1 + 7) + x] = 0x0;
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x7 + carry_flag);
  set_vram_list1_length(); // fallthrough
  return;
}

// in: a; out: none
void set_vram_list1_length(void) {
  ram[VRAM_LIST1_LENGTH] = a;
  return;
}

void queue_mushroom_palette(void) {
  a = ram[AREA_STYLE];
  if (a != 0x1) {
    skip_mushroom_palette();
    return;
  }
  a = 0xb;
  select_vram_list_b(); // fallthrough
  return;
}

// in: a; out: none
void select_vram_list_b(void) {
  ram[VRAM_LIST_SELECT] = a;
  skip_mushroom_palette(); // fallthrough
  return;
}

void skip_mushroom_palette(void) {
  next_screen_task_b(); return;
}

void draw_status_bar_top(void) {
  a = 0x0;
  queue_text();
  next_screen_task_b(); return;
}

void draw_status_bar_bottom(void) {
  queue_counters_for_player();
  x = ram[VRAM_LIST1_LENGTH];
  ram[VRAM_LIST1 + x] = 0x20;
  ram[(VRAM_LIST1 + 1) + x] = 0x73;
  ram[(VRAM_LIST1 + 2) + x] = 0x3;
  y = ram[WORLD_NUMBER];
  y++;
  ram[(VRAM_LIST1 + 3) + x] = y;
  ram[(VRAM_LIST1 + 4) + x] = 0x28;
  y = ram[LEVEL_NUMBER];
  y++;
  ram[(VRAM_LIST1 + 5) + x] = y;
  ram[(VRAM_LIST1 + 6) + x] = 0x0;
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x6 + carry_flag);
  ram[VRAM_LIST1_LENGTH] = a;
  next_screen_task_b(); return;
}

void show_time_up(void) {
  a = ram[TIME_UP]; zero_flag = a == 0;
  if (!zero_flag) {
    ram[TIME_UP] = 0x0;
    a = 0x2;
    start_intermission(); return;
  }
  ram[SCREEN_TASK]++;
  next_screen_task_b(); return;
}

void show_intermission(void) {
  a = ram[GAME_MODE]; zero_flag = a == 0;
  if (!zero_flag) {
    if (a != 0x3) {
      a = ram[ALT_ENTRANCE]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_86e0; }
      y = ram[AREA_KIND];
      if (y != 0x3) {
        a = ram[SKIP_INTERMISSION]; zero_flag = a == 0;
        if (!zero_flag) { goto loc_86e0; }
      }
      draw_lives_screen_player();
      a = 0x1;
      start_intermission(); // fallthrough
      return;
    }
    ram[SCREEN_TIMER] = 0x12;
    a = 0x3;
    queue_text();
    next_game_mode_task_b(); return;
  }
  
loc_86e0:
  ram[SCREEN_TASK] = 0x8;
  return;
}

// in: a; out: none
void start_intermission(void) {
  queue_text();
  restart_screen_timer();
  ram[SCREEN_OFF] = 0x0;
  return;
}

// build the screen column by column while the display is off
void render_area_columns(void) {
  ram[SCREEN_OFF]++;
  do {
    run_parser_task();
    a = ram[PARSER_TASK]; zero_flag = a == 0;
  } while (!zero_flag);
  ram[COLUMN_SETS]--; neg_flag = (ram[COLUMN_SETS] & 0x80) != 0;
  if (neg_flag) {
    ram[SCREEN_TASK]++;
  }
  ram[VRAM_LIST_SELECT] = 0x6;
  return;
}

// draw_title_screen: native implementation, not generated.
// It may read: none.
// It must produce: none.

void clear_lists_draw_icon(void) {
  a = ram[GAME_MODE]; zero_flag = a == 0;
  if (!zero_flag) {
    next_game_mode_task_b();
    return;
  }
  x = 0x0;
  do {
    ram[VRAM_LIST1_LENGTH + x] = a;
    ram[OBJ_X_FORCE + x] = a;
    x--; zero_flag = x == 0;
  } while (!zero_flag);
  draw_player_select_icon();
  next_screen_task_b(); // fallthrough
  return;
}

void next_screen_task_b(void) {
  ram[SCREEN_TASK]++;
  return;
}

void draw_top_score(void) {
  a = 0xfa;
  queue_counters();
  next_game_mode_task_b(); // fallthrough
  return;
}

void next_game_mode_task_b(void) {
  ram[GAME_MODE_TASK]++;
  return;
}

// in: a; out: none
void queue_text(void) {
  ram[0x100 + sp--] = a;
  a = (uint8_t)(a << 1);
  y = a;
  if (y >= 0x4) {
    if (y >= 0x8) {
      y = 0x8;
    }
    a = ram[PLAYER_COUNT]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_881b; }
    y++;
  }
  
loc_881b:
  x = ROM(TEXT_OFFSETS + y);
  y = 0x0;
  do {
    a = ROM(TEXT_STATUS_BAR + x);
    zero_flag = a == 0xff;
    if (zero_flag) { goto loc_882e; }
    ram[VRAM_LIST1 + y] = a;
    x++;
    y++; zero_flag = y == 0;
  } while (!zero_flag);
  
loc_882e:
  ram[VRAM_LIST1 + y] = 0x0;
  a = ram[0x100 + ++sp];
  x = a;
  carry_flag = a >= 0x4;
  if (!carry_flag) {
    x--; zero_flag = x == 0;
    if (zero_flag) {
      a = ram[LIVES];
      carry_flag = false;
      a = (uint8_t)(a + 0x1 + carry_flag);
      carry_flag = a >= 0xa;
      if (carry_flag) {
        a = (uint8_t)(a - 0xa - !carry_flag);
        ram[(VRAM_LIST1 + 7)] = 0x9f;
      }
      ram[(VRAM_LIST1 + 8)] = a;
      y = ram[WORLD_NUMBER];
      y++;
      ram[(VRAM_LIST1 + 19)] = y;
      y = ram[LEVEL_NUMBER];
      y++;
      ram[(VRAM_LIST1 + 21)] = y;
      return;
    }
    a = ram[PLAYER_COUNT]; zero_flag = a == 0;
    if (zero_flag) {
      return;
    }
    a = ram[CURRENT_PLAYER];
    x--; zero_flag = x == 0;
    if (zero_flag) {
      y = ram[GAME_MODE];
      zero_flag = y == 0x3;
      if (zero_flag) { goto loc_8873; }
      a ^= 0x1;
    }
    
loc_8873:
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (!carry_flag) {
      return;
    }
    y = 0x4;
    do {
      ram[(VRAM_LIST1 + 3) + y] = ROM(TEXT_LUIGI + y);
      y--; neg_flag = (y & 0x80) != 0;
    } while (!neg_flag);
    return;
  }
  a = (uint8_t)(a - 0x4 - !carry_flag);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  x = a;
  y = 0x0;
  do {
    ram[(VRAM_LIST1 + 27) + y] = ROM(TEXT_WARP_NUMBERS + x);
    x++;
    y++;
    y++;
    y++;
    y++;
    carry_flag = y >= 0xc;
  } while (!carry_flag);
  a = 0x2c;
  set_vram_list1_length(); return;
}

void wait_screen_timer(void) {
  a = ram[SCREEN_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  hide_all_sprites();
  restart_screen_timer(); // fallthrough
  return;
}

void restart_screen_timer(void) {
  ram[SCREEN_TIMER] = 0x7;
  ram[SCREEN_TASK]++;
  return;
}

// turn one column of metatiles into a VRAM update
void render_column(void) {
  a = ram[PARSER_COLUMN];
  a &= 0x1;
  ram[0x5] = a;
  y = ram[VRAM_LIST2_LENGTH];
  ram[0x0] = y;
  ram[(VRAM_LIST2 + 1) + y] = ram[NT_ADDRESS_LOW];
  ram[VRAM_LIST2 + y] = ram[NT_ADDRESS_HIGH];
  ram[(VRAM_LIST2 + 2) + y] = 0x9a;
  a = 0x0;
  ram[0x4] = a;
  x = a;
  do {
    ram[0x1] = x;
    a = ram[METATILE_COLUMN + x];
    a &= 0xc0;
    ram[0x3] = a;
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
    a = (uint8_t)(a << 1 | carry_flag);
    y = a;
    ram[0x6] = ROM(METATILE_SETS_LOW + y);
    ram[0x7] = ROM(METATILE_SETS_HIGH + y);
    a = ram[METATILE_COLUMN + x];
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    ram[0x2] = a;
    a = ram[PARSER_TASK];
    a &= 0x1;
    a ^= 0x1;
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    a = (uint8_t)(a + ram[0x2] + carry_flag);
    y = a;
    x = ram[0x0];
    ram[(VRAM_LIST2 + 3) + x] = read_byte(indirect_y_addr(0x6));
    y++;
    ram[(VRAM_LIST2 + 4) + x] = read_byte(indirect_y_addr(0x6));
    y = ram[0x4];
    a = ram[0x5]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[0x1];
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (carry_flag) { goto loc_892a; }
      { bool carry = (ram[0x3] & 0x80) != 0; ram[0x3] = (uint8_t)(ram[0x3] << 1 | carry_flag); carry_flag = carry; }
      { bool carry = (ram[0x3] & 0x80) != 0; ram[0x3] = (uint8_t)(ram[0x3] << 1 | carry_flag); carry_flag = carry; }
      ram[0x3] = (uint8_t)(ram[0x3] << 1 | carry_flag);
    } else {
      a = ram[0x1];
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (carry_flag) { goto loc_892e; }
      ram[0x3] = ram[0x3] >> 1;
      ram[0x3] = ram[0x3] >> 1;
      ram[0x3] = ram[0x3] >> 1;
      ram[0x3] = ram[0x3] >> 1;
      goto loc_8930;
      
loc_892a:
      ram[0x3] = ram[0x3] >> 1;
      ram[0x3] = ram[0x3] >> 1;
      
loc_892e:
      ram[0x4]++;
    }
    
loc_8930:
    a = ram[ATTRIBUTE_BUFFER + y];
    a |= ram[0x3];
    ram[ATTRIBUTE_BUFFER + y] = a;
    ram[0x0]++;
    ram[0x0]++;
    x = ram[0x1];
    x++;
    carry_flag = x >= 0xd;
  } while (!carry_flag);
  y = ram[0x0];
  y++;
  y++;
  y++;
  ram[VRAM_LIST2 + y] = 0x0;
  ram[VRAM_LIST2_LENGTH] = y;
  ram[NT_ADDRESS_LOW]++;
  a = ram[NT_ADDRESS_LOW];
  a &= 0x1f; zero_flag = a == 0;
  if (zero_flag) {
    ram[NT_ADDRESS_LOW] = 0x80;
    a = ram[NT_ADDRESS_HIGH];
    a ^= 0x4;
    ram[NT_ADDRESS_HIGH] = a;
  }
  select_vram_list2(); return;
}

void render_attributes(void) {
  a = ram[NT_ADDRESS_LOW];
  a &= 0x1f;
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - 0x4 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  a &= 0x1f;
  ram[0x1] = a;
  a = ram[NT_ADDRESS_HIGH];
  if (!carry_flag) {
    a ^= 0x4;
  }
  a &= 0x4;
  a |= 0x23;
  ram[0x0] = a;
  a = ram[0x1];
  a = a >> 1;
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = (uint8_t)(a + 0xc0 + carry_flag);
  ram[0x1] = a;
  x = 0x0;
  y = ram[VRAM_LIST2_LENGTH];
  do {
    ram[VRAM_LIST2 + y] = ram[0x0];
    a = ram[0x1];
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[(VRAM_LIST2 + 1) + y] = a;
    ram[0x1] = a;
    ram[(VRAM_LIST2 + 3) + y] = ram[ATTRIBUTE_BUFFER + x];
    a = 0x1;
    ram[(VRAM_LIST2 + 2) + y] = a;
    a = a >> 1;
    ram[ATTRIBUTE_BUFFER + x] = a;
    y++;
    y++;
    y++;
    y++;
    x++;
    carry_flag = x >= 0x7;
  } while (!carry_flag);
  ram[VRAM_LIST2 + y] = a;
  ram[VRAM_LIST2_LENGTH] = y;
  select_vram_list2(); // fallthrough
  return;
}

void select_vram_list2(void) {
  ram[VRAM_LIST_SELECT] = 0x6;
  return;
}

void cycle_palette(void) {
  a = ram[FRAME_COUNT];
  a &= 0x7; zero_flag = a == 0;
  if (!zero_flag) {
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
    carry_flag = y >= 0x8;
  } while (!carry_flag);
  x = ram[VRAM_LIST1_LENGTH];
  ram[0x0] = 0x3;
  a = ram[AREA_KIND];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  y = a;
  do {
    ram[(VRAM_LIST1 + 3) + x] = ROM(CYCLING_PALETTES + y);
    y++;
    x++;
    ram[0x0]--; neg_flag = (ram[0x0] & 0x80) != 0;
  } while (!neg_flag);
  x = ram[VRAM_LIST1_LENGTH];
  y = ram[PALETTE_CYCLE_POS];
  ram[(VRAM_LIST1 + 4) + x] = ROM(CYCLING_PALETTE_IDS + y);
  a = ram[VRAM_LIST1_LENGTH];
  carry_flag = false;
  a = (uint8_t)(a + 0x7 + carry_flag);
  ram[VRAM_LIST1_LENGTH] = a;
  ram[PALETTE_CYCLE_POS]++;
  a = ram[PALETTE_CYCLE_POS];
  if (a < 0x6) {
    return;
  }
  ram[PALETTE_CYCLE_POS] = 0x0;
  return;
}

// in: none; out: carry
void erase_coin_or_axe(void) {
  y = 0x41;
  a = 0x3;
  x = ram[AREA_KIND]; zero_flag = x == 0;
  if (zero_flag) {
    a = 0x4;
  }
  queue_metatile();
  ram[VRAM_LIST_SELECT] = 0x6;
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
  a = 0x0;
  queue_block_tiles(); // fallthrough
  return;
}

// in: a, x; out: x
void queue_block_tiles(void) {
  y = 0x3;
  if (a != 0x0) {
    y = 0x0;
    zero_flag = a == 0x58;
    if (zero_flag) { goto loc_8a87; }
    zero_flag = a == 0x51;
    if (zero_flag) { goto loc_8a87; }
    y++;
    zero_flag = a == 0x5d;
    if (zero_flag) { goto loc_8a87; }
    zero_flag = a == 0x52;
    if (zero_flag) { goto loc_8a87; }
    y++;
  }
  
loc_8a87:
  a = y;
  y = ram[VRAM_LIST1_LENGTH];
  y++;
  queue_metatile();
  advance_vram_list(); // fallthrough
  return;
}

// in: y; out: y
void advance_vram_list(void) {
  y--;
  a = y;
  carry_flag = false;
  a = (uint8_t)(a + 0xa + carry_flag);
  set_vram_list1_length(); return;
}

// in: a, x, y; out: x, y, carry
void queue_metatile(void) {
  ram[0x0] = x;
  ram[0x1] = y;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  x = a;
  y = 0x20;
  a = ram[0x6];
  if (a >= 0xd0) {
    y = 0x24;
  }
  ram[0x3] = y;
  a &= 0xf;
  a = (uint8_t)(a << 1);
  ram[0x4] = a;
  ram[0x5] = 0x0;
  a = ram[0x2];
  carry_flag = false;
  a = (uint8_t)(a + 0x20 + carry_flag);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x5] = (uint8_t)(ram[0x5] << 1 | carry_flag);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (ram[0x5] & 0x80) != 0; ram[0x5] = (uint8_t)(ram[0x5] << 1 | carry_flag); carry_flag = carry; }
  { uint16_t sum = a + ram[0x4] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x4] = a;
  a = ram[0x5];
  a = (uint8_t)(a + 0x0 + carry_flag);
  carry_flag = false;
  a = (uint8_t)(a + ram[0x3] + carry_flag);
  ram[0x5] = a;
  y = ram[0x1];
  queue_metatile_tiles(); // fallthrough
  return;
}

// in: x, y; out: x, carry
void queue_metatile_tiles(void) {
  ram[(VRAM_LIST1 + 2) + y] = ROM(BLOCK_TILE_OFFSETS + x);
  ram[(VRAM_LIST1 + 3) + y] = ROM((BLOCK_TILE_OFFSETS + 1) + x);
  ram[(VRAM_LIST1 + 7) + y] = ROM((BLOCK_TILE_OFFSETS + 2) + x);
  ram[(VRAM_LIST1 + 8) + y] = ROM((BLOCK_TILE_OFFSETS + 3) + x);
  a = ram[0x4];
  ram[VRAM_LIST1 + y] = a;
  carry_flag = false;
  { uint16_t sum = a + 0x20 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(VRAM_LIST1 + 5) + y] = a;
  a = ram[0x5];
  ram[VRAM_LIST1_LENGTH + y] = a;
  ram[(VRAM_LIST1 + 4) + y] = a;
  a = 0x2;
  ram[(VRAM_LIST1 + 1) + y] = a;
  ram[(VRAM_LIST1 + 6) + y] = a;
  ram[(VRAM_LIST1 + 9) + y] = 0x0;
  x = ram[0x0];
  return;
}

// clear_screens: native implementation, not generated.
// It may read: none.
// It must produce: y.

// clear_screen: native implementation, not generated.
// It may read: a.
// It must produce: y.

void read_joypads(void) {
  a = 0x1;
  write_joypad1(a); write_joypad2(a);
  a = a >> 1;
  x = a;
  write_joypad1(a); write_joypad2(a);
  read_joypad();
  x++;
  read_joypad(); // fallthrough
  return;
}

// in: a, x; out: a
void read_joypad(void) {
  y = 0x8;
  do {
    ram[0x100 + sp--] = a;
    a = read_byte((uint16_t)(JOYPAD_PORTS + x));
    ram[0x0] = a;
    a = a >> 1;
    a |= ram[0x0];
    carry_flag = (a & 1) != 0; a = a >> 1;
    a = ram[0x100 + ++sp];
    a = (uint8_t)(a << 1 | carry_flag);
    y--; zero_flag = y == 0;
  } while (!zero_flag);
  ram[JOYPADS + x] = a;
  ram[0x100 + sp--] = a;
  a &= 0x30;
  a &= ram[(JOYPAD_MASKS + x) & 0x7ff]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[0x100 + ++sp];
    a &= 0xcf;
    ram[JOYPADS + x] = a;
    return;
  }
  a = ram[0x100 + ++sp];
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

// in: a; out: none
void queue_player_counters(void) {
  ram[0x0] = a;
  queue_counter_digits();
  a = ram[0x0];
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  queue_counter_digits(); // fallthrough
  return;
}

// write a score, coin count or time to the VRAM update list
// in: a; out: none
void queue_counter_digits(void) {
  carry_flag = false;
  a = (uint8_t)(a + 0x1 + carry_flag);
  a &= 0xf;
  if (a >= 0x6) {
    return;
  }
  ram[0x100 + sp--] = a;
  a = (uint8_t)(a << 1);
  y = a;
  x = ram[VRAM_LIST1_LENGTH];
  a = 0x20;
  if (y == 0x0) {
    a = 0x22;
  }
  ram[VRAM_LIST1 + x] = a;
  ram[(VRAM_LIST1 + 1) + x] = ROM(COUNTER_POSITIONS + y);
  a = ROM((COUNTER_POSITIONS + 1) + y);
  ram[(VRAM_LIST1 + 2) + x] = a;
  ram[0x3] = a;
  ram[0x2] = x;
  a = ram[0x100 + ++sp];
  x = a;
  a = ROM(COUNTER_OFFSETS + x);
  carry_flag = true;
  a = (uint8_t)(a - ROM((COUNTER_POSITIONS + 1) + y) - !carry_flag);
  y = a;
  x = ram[0x2];
  do {
    ram[(VRAM_LIST1 + 3) + x] = ram[(DIGITS + y) & 0x7ff];
    x++;
    y++;
    ram[0x3]--; zero_flag = ram[0x3] == 0;
  } while (!zero_flag);
  ram[(VRAM_LIST1 + 3) + x] = 0x0;
  x++;
  x++;
  x++;
  ram[VRAM_LIST1_LENGTH] = x;
  return;
}

// decimal add with carry across score or time digits
// in: y; out: none
void add_digits(void) {
  a = ram[GAME_MODE];
  zero_flag = a == 0x0;
  if (zero_flag) { goto loc_8f7c; }
  x = 0x5;
  
loc_8f68:
  a = ram[SCORE_ADD + x];
  carry_flag = false;
  a = (uint8_t)(a + ram[(DIGITS + y) & 0x7ff] + carry_flag); neg_flag = (a & 0x80) != 0;
  if (neg_flag) { goto loc_8f87; }
  carry_flag = a >= 0xa;
  if (carry_flag) { goto loc_8f8e; }
  
loc_8f75:
  ram[(DIGITS + y) & 0x7ff] = a;
  y--;
  x--; neg_flag = (x & 0x80) != 0;
  if (!neg_flag) { goto loc_8f68; }
  
loc_8f7c:
  a = 0x0;
  x = 0x6;
  do {
    ram[(POPUP_TIMER + 7) + x] = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
  
loc_8f87:
  ram[(POPUP_TIMER + 7) + x]--;
  a = 0x9;
  goto loc_8f75;
  
loc_8f8e:
  carry_flag = true;
  a = (uint8_t)(a - 0xa - !carry_flag);
  ram[(POPUP_TIMER + 7) + x]++;
  goto loc_8f75;
}

void update_top_score(void) {
  x = 0x5;
  check_top_score();
  x = 0xb;
  check_top_score(); // fallthrough
  return;
}

// in: x; out: none
void check_top_score(void) {
  y = 0x5;
  carry_flag = true;
  do {
    a = ram[(PLAYER_SCORE + x) & 0x7ff];
    { uint16_t diff = (uint16_t)(a - ram[(DIGITS + y) & 0x7ff] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    x--;
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  if (!carry_flag) {
    return;
  }
  x++;
  y++;
  do {
    ram[(DIGITS + y) & 0x7ff] = ram[(PLAYER_SCORE + x) & 0x7ff];
    x++;
    y++;
    carry_flag = y >= 0x6;
  } while (!carry_flag);
  return;
}

void start_game(void) {
  y = 0x6f;
  clear_memory();
  y = 0x1f;
  do {
    ram[(NOISE_MUSIC_POS + y) & 0x7ff] = a;
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  ram[DEMO_TIMER] = 0x18;
  load_area_pointer();
  start_area(); // fallthrough
  return;
}

// reset area state and position the player at the entrance
void start_area(void) {
  y = 0x4b;
  clear_memory();
  x = 0x21;
  a = 0x0;
  do {
    ram[(TIMERS + x) & 0x7ff] = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  a = ram[HALFWAY_PAGE];
  y = ram[ALT_ENTRANCE]; zero_flag = y == 0;
  if (!zero_flag) {
    a = ram[ENTRANCE_PAGE];
  }
  ram[SCREEN_LEFT_PAGE] = a;
  ram[PARSER_PAGE] = a;
  ram[BACKLOADING] = a;
  update_screen_edges();
  y = 0x20;
  a &= 0x1; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x24;
  }
  ram[NT_ADDRESS_HIGH] = y;
  ram[NT_ADDRESS_LOW] = 0x80;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  ram[BLOCK_COLUMN_POS] = a;
  ram[AREA_OBJ_LENGTH]--;
  ram[(AREA_OBJ_LENGTH + 1)]--;
  ram[(AREA_OBJ_LENGTH + 2)]--;
  ram[COLUMN_SETS] = 0xb;
  load_area_data();
  a = ram[HARD_MODE]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[WORLD_NUMBER];
    { uint8_t m = 0x4; carry_flag = a >= m; zero_flag = a == m; }
    if (!carry_flag) { goto loc_904a; }
    if (!zero_flag) { goto loc_9047; }
    a = ram[LEVEL_NUMBER];
    carry_flag = a >= 0x2;
    if (!carry_flag) { goto loc_904a; }
  }
  
loc_9047:
  ram[HARD_MODE_LATE]++;
  
loc_904a:
  a = ram[HALFWAY_PAGE]; zero_flag = a == 0;
  if (!zero_flag) {
    ram[ENTRANCE_KIND] = 0x2;
  }
  ram[AREA_MUSIC_REQUEST] = 0x80;
  ram[SCREEN_OFF] = 0x1;
  ram[GAME_MODE_TASK]++;
  return;
}

void setup_new_game(void) {
  a = 0x1;
  ram[RELOAD_TIME_LIMIT] = a;
  ram[PLAYER_SMALL] = a;
  a = 0x2;
  ram[LIVES] = a;
  ram[OTHER_LIVES] = a;
  setup_area_display(); // fallthrough
  return;
}

void setup_area_display(void) {
  a = 0x0;
  ram[SCREEN_OFF] = a;
  y = a;
  do {
    ram[VRAM_LIST1_LENGTH + y] = a;
    y++; zero_flag = y == 0;
  } while (!zero_flag);
  ram[TIME_UP] = a;
  ram[SKIP_INTERMISSION] = a;
  ram[BACKLOADING] = a;
  ram[BALANCE_ALIGNMENT] = 0xff;
  a = ram[SCREEN_LEFT_PAGE];
  carry_flag = (ram[PPU_CTRL_COPY] & 1) != 0; ram[PPU_CTRL_COPY] = ram[PPU_CTRL_COPY] >> 1;
  a &= 0x1;
  { bool carry = (a & 1) != 0; a = (uint8_t)(a >> 1 | carry_flag << 7); carry_flag = carry; }
  ram[PPU_CTRL_COPY] = (uint8_t)(ram[PPU_CTRL_COPY] << 1 | carry_flag);
  queue_area_music();
  ram[(SHUFFLE_STEPS + 2)] = 0x38;
  ram[(SHUFFLE_STEPS + 1)] = 0x48;
  ram[SHUFFLE_STEPS] = 0x58;
  x = 0xe;
  do {
    ram[SPRITE_SLOTS + x] = ROM(DEFAULT_SPRITE_SLOTS + x);
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  y = 0x3;
  do {
    ram[SPRITE_Y + y] = ROM(SPRITE_ZERO + y);
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  do_nothing();
  set_unused_flag();
  ram[STATUS_BAR_SPLIT]++;
  ram[GAME_MODE_TASK]++;
  return;
}

// clear RAM below the offset in Y, sparing the stack in use
// in: y; out: a
void clear_memory(void) {
  x = 0x7;
  a = 0x0;
  ram[0x6] = a;
  do {
    ram[0x7] = x;
    do {
      if (x == 0x1) {
        carry_flag = y >= 0x60;
        if (carry_flag) { goto loc_90de; }
      }
      dynamic_ram_write(indirect_y_addr(0x6), a);
      
loc_90de:
      y--;
      zero_flag = y == 0xff;
    } while (!zero_flag);
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

void queue_area_music(void) {
  a = ram[GAME_MODE]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[ALT_ENTRANCE];
  if (a != 0x2) {
    y = 0x5;
    a = ram[ENTRANCE_KIND];
    zero_flag = a == 0x6;
    if (zero_flag) { goto loc_9110; }
    zero_flag = a == 0x7;
    if (zero_flag) { goto loc_9110; }
  }
  y = ram[AREA_KIND];
  a = ram[CLOUD_OVERRIDE]; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x4;
  }
  
loc_9110:
  ram[AREA_MUSIC_REQUEST] = ROM(AREA_MUSIC_KINDS + y);
  return;
}

void place_player(void) {
  ram[PLAYER_PAGE] = ram[SCREEN_LEFT_PAGE];
  ram[GRAVITY_FALLING] = 0x28;
  a = 0x1;
  ram[PLAYER_FACING] = a;
  ram[PLAYER_Y_PAGE] = a;
  ram[PLAYER_STATE] = 0x0;
  ram[PLAYER_COLLISION]--;
  y = 0x0;
  ram[HALFWAY_PAGE] = y;
  a = ram[AREA_KIND]; zero_flag = a == 0;
  if (zero_flag) {
    y++;
  }
  ram[SWIMMING] = y;
  x = ram[ENTRANCE_KIND];
  y = ram[ALT_ENTRANCE]; zero_flag = y == 0;
  if (!zero_flag) {
    zero_flag = y == 0x1;
    if (zero_flag) { goto loc_9165; }
    x = ROM((ENTRANCE_X_POSITIONS + 2) + y);
  }
  
loc_9165:
  ram[PLAYER_X] = ROM(ENTRANCE_X_POSITIONS + y);
  ram[PLAYER_Y] = ROM(ENTRANCE_Y_POSITIONS + x);
  ram[PLAYER_SPRITE_ATTR] = ROM(ENTRANCE_PRIORITIES + x);
  queue_player_colors();
  y = ram[TIME_LIMIT]; zero_flag = y == 0;
  if (!zero_flag) {
    a = ram[RELOAD_TIME_LIMIT]; zero_flag = a == 0;
    if (zero_flag) { goto loc_9197; }
    ram[TIME_DIGITS] = ROM(TIME_LIMITS + y);
    a = 0x1;
    ram[(TIME_DIGITS + 2)] = a;
    a = a >> 1;
    ram[(TIME_DIGITS + 1)] = a;
    ram[RELOAD_TIME_LIMIT] = a;
    ram[STAR_TIMER] = a;
  }
  
loc_9197:
  y = ram[JOYPAD_OVERRIDE]; zero_flag = y == 0;
  if (!zero_flag) {
    ram[PLAYER_STATE] = 0x3;
    x = 0x0;
    place_block_object();
    ram[BLOCK_Y] = 0xf0;
    x = 0x5;
    y = 0x0;
    spawn_vine();
  }
  y = ram[AREA_KIND]; zero_flag = y == 0;
  if (zero_flag) {
    spawn_bubble();
  }
  ram[PLAYER_TASK] = 0x7;
  return;
}

void lose_life(void) {
  ram[SCREEN_OFF]++;
  ram[STATUS_BAR_SPLIT] = 0x0;
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  ram[LIVES]--; neg_flag = (ram[LIVES] & 0x80) != 0;
  if (neg_flag) {
    ram[GAME_MODE_TASK] = 0x0;
    ram[GAME_MODE] = 0x3;
    return;
  }
  a = ram[WORLD_NUMBER];
  a = (uint8_t)(a << 1);
  x = a;
  a = ram[LEVEL_NUMBER];
  a &= 0x2; zero_flag = a == 0;
  if (!zero_flag) {
    x++;
  }
  y = ROM(HALFWAY_PAGES + x);
  a = ram[LEVEL_NUMBER];
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = y;
  if (!carry_flag) {
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
  }
  a &= 0xf;
  { uint8_t m = ram[SCREEN_LEFT_PAGE]; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) {
    if (!carry_flag) { goto loc_920f; }
    a = 0x0;
  }
  
loc_920f:
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
  a = 0x0;
  ram[SCREEN_TASK] = a;
  ram[STATUS_BAR_SPLIT] = a;
  ram[EVENT_MUSIC_REQUEST] = 0x2;
  ram[SCREEN_OFF]++;
  ram[GAME_MODE_TASK]++;
  return;
}

void game_over_screen(void) {
  ram[SCREEN_OFF] = 0x0;
  a = ram[JOYPAD1];
  a &= 0x10; zero_flag = a == 0;
  if (!zero_flag) {
    end_game();
    return;
  }
  a = ram[SCREEN_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  end_game(); // fallthrough
  return;
}

void end_game(void) {
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  switch_players();
  if (!carry_flag) {
    continue_game();
    return;
  }
  ram[CONTINUE_WORLD] = ram[WORLD_NUMBER];
  a = 0x0;
  a = (uint8_t)(a << 1);
  ram[GAME_MODE_TASK] = a;
  ram[SCREEN_TIMER] = a;
  ram[GAME_MODE] = a;
  return;
}

void continue_game(void) {
  load_area_pointer();
  ram[PLAYER_SMALL] = 0x1;
  ram[RELOAD_TIME_LIMIT]++;
  a = 0x0;
  ram[TIMERS_PAUSED] = a;
  ram[PLAYER_POWER] = a;
  ram[PLAYER_TASK] = a;
  ram[GAME_MODE_TASK] = a;
  ram[GAME_MODE] = 0x1;
  return;
}

// in: none; out: carry
void switch_players(void) {
  carry_flag = true;
  a = ram[PLAYER_COUNT]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[OTHER_LIVES]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    return;
  }
  a = ram[CURRENT_PLAYER];
  a ^= 0x1;
  ram[CURRENT_PLAYER] = a;
  x = 0x6;
  do {
    a = ram[(PLAYER_INFO + x) & 0x7ff];
    ram[0x100 + sp--] = a;
    ram[(PLAYER_INFO + x) & 0x7ff] = ram[(OTHER_PLAYER_INFO + x) & 0x7ff];
    a = ram[0x100 + ++sp];
    ram[(OTHER_PLAYER_INFO + x) & 0x7ff] = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  carry_flag = false;
  return;
}

void set_unused_flag(void) {
  ram[(MISC_CONTACT + 11)] = 0xff;
  do_nothing(); // fallthrough
  return;
}

void do_nothing(void) {
  return;
}

void run_parser_task(void) {
  y = ram[PARSER_TASK]; zero_flag = y == 0;
  if (zero_flag) {
    y = 0x8;
    ram[PARSER_TASK] = y;
  }
  y--;
  a = y;
  run_parser_subtask();
  ram[PARSER_TASK]--; zero_flag = ram[PARSER_TASK] == 0;
  if (!zero_flag) {
    return;
  }
  render_attributes();
  return;
}

// in: a; out: none
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
  a = ram[PARSER_COLUMN];
  a &= 0xf; zero_flag = a == 0;
  if (zero_flag) {
    ram[PARSER_COLUMN] = a;
    ram[PARSER_PAGE]++;
  }
  ram[BLOCK_COLUMN_POS]++;
  a = ram[BLOCK_COLUMN_POS];
  a &= 0x1f;
  ram[BLOCK_COLUMN_POS] = a;
  return;
}

void parse_area_column(void) {
  a = ram[BACKLOADING]; zero_flag = a == 0;
  if (!zero_flag) {
    process_area_objects();
  }
  x = 0xc;
  a = 0x0;
  do {
    ram[METATILE_COLUMN + x] = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  y = ram[BACKGROUND_SCENERY]; zero_flag = y == 0;
  if (!zero_flag) {
    a = ram[PARSER_PAGE];
    do {
      neg_flag = ((a - 0x3) & 0x80) != 0;
      if (neg_flag) { goto loc_941f; }
      carry_flag = true;
      a = (uint8_t)(a - 0x3 - !carry_flag); neg_flag = (a & 0x80) != 0;
    } while (!neg_flag);
    
loc_941f:
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    { uint16_t sum = a + ROM((BACKGROUND_SCENE_OFFSETS - 1) + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    a = (uint8_t)(a + ram[PARSER_COLUMN] + carry_flag);
    x = a;
    a = ROM(BACKGROUND_SCENES + x); zero_flag = a == 0;
    if (zero_flag) { goto loc_9455; }
    ram[0x100 + sp--] = a;
    a &= 0xf;
    carry_flag = true;
    a = (uint8_t)(a - 0x1 - !carry_flag);
    ram[0x0] = a;
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    a = (uint8_t)(a + ram[0x0] + carry_flag);
    x = a;
    a = ram[0x100 + ++sp];
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    y = a;
    ram[0x0] = 0x3;
    do {
      ram[METATILE_COLUMN + y] = ROM(BACKGROUND_SCENE_METATILES + x);
      x++;
      y++;
      zero_flag = y == 0xb;
      if (zero_flag) { goto loc_9455; }
      ram[0x0]--; zero_flag = ram[0x0] == 0;
    } while (!zero_flag);
  }
  
loc_9455:
  x = ram[FOREGROUND_SCENERY]; zero_flag = x == 0;
  if (!zero_flag) {
    y = ROM((BACKGROUND_SCENE_METATILES + 35) + x);
    x = 0x0;
    do {
      a = ROM(FOREGROUND_SCENES + y); zero_flag = a == 0;
      if (!zero_flag) {
        ram[METATILE_COLUMN + x] = a;
      }
      y++;
      x++;
      zero_flag = x == 0xd;
    } while (!zero_flag);
  }
  y = ram[AREA_KIND]; zero_flag = y == 0;
  if (zero_flag) {
    a = ram[WORLD_NUMBER];
    zero_flag = a == 0x7;
    if (!zero_flag) { goto loc_947e; }
    a = 0x62;
  } else {
    
loc_947e:
    a = ROM(TERRAIN_METATILES + y);
    y = ram[CLOUD_OVERRIDE]; zero_flag = y == 0;
    if (zero_flag) { goto loc_9488; }
    a = 0x88;
  }
  
loc_9488:
  ram[0x7] = a;
  x = 0x0;
  a = ram[TERRAIN_KIND];
  a = (uint8_t)(a << 1);
  y = a;
  do {
    ram[0x0] = ROM(TERRAIN_PATTERNS + y);
    y++;
    ram[0x1] = y;
    a = ram[CLOUD_OVERRIDE]; zero_flag = a == 0;
    if (!zero_flag) {
      zero_flag = x == 0x0;
      if (zero_flag) { goto loc_94a8; }
      a = ram[0x0];
      a &= 0x8;
      ram[0x0] = a;
    }
    
loc_94a8:
    y = 0x0;
    do {
      a = ROM(BIT_MASKS + y);
      zero_flag = (a & ram[0x0]) == 0;
      if (!zero_flag) {
        ram[METATILE_COLUMN + x] = ram[0x7];
      }
      x++;
      zero_flag = x == 0xd;
      if (zero_flag) { goto loc_94d3; }
      a = ram[AREA_KIND];
      if (a == 0x2) {
        zero_flag = x == 0xb;
        if (!zero_flag) { goto loc_94ca; }
        ram[0x7] = 0x54;
      }
      
loc_94ca:
      y++;
      zero_flag = y == 0x8;
    } while (!zero_flag);
    y = ram[0x1]; zero_flag = y == 0;
  } while (!zero_flag);
  
loc_94d3:
  process_area_objects();
  a = ram[BLOCK_COLUMN_POS];
  get_block_buffer_address();
  x = 0x0;
  y = 0x0;
  do {
    ram[0x0] = y;
    a = ram[METATILE_COLUMN + x];
    a &= 0xc0;
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
    a = (uint8_t)(a << 1 | carry_flag);
    y = a;
    a = ram[METATILE_COLUMN + x];
    carry_flag = a >= ROM(BLOCK_BUFFER_LIMITS + y);
    if (!carry_flag) {
      a = 0x0;
    }
    y = ram[0x0];
    dynamic_ram_write(indirect_y_addr(0x6), a);
    a = y;
    carry_flag = false;
    a = (uint8_t)(a + 0x10 + carry_flag);
    y = a;
    x++;
    carry_flag = x >= 0xd;
  } while (!carry_flag);
  return;
}

// in: none; out: x
void process_area_objects(void) {
  do {
    
process_area_objects:
    x = 0x2;
    do {
      ram[SLOT] = x;
      ram[PARSER_BEHIND] = 0x0;
      y = ram[AREA_DATA_POS];
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      if (a != 0xfd) {
        a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; neg_flag = (a & 0x80) != 0;
        if (!neg_flag) { goto loc_9565; }
        y++;
        a = read_byte(indirect_y_addr(AREA_DATA_PTR));
        carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
        if (carry_flag) {
          a = ram[AREA_OBJ_PAGE_SET]; zero_flag = a == 0;
          if (!zero_flag) { goto loc_9530; }
          ram[AREA_OBJ_PAGE_SET]++;
          ram[AREA_OBJ_PAGE]++;
        }
        
loc_9530:
        y--;
        a = read_byte(indirect_y_addr(AREA_DATA_PTR));
        a &= 0xf;
        if (a == 0xd) {
          y++;
          a = read_byte(indirect_y_addr(AREA_DATA_PTR));
          y--;
          a &= 0x40; zero_flag = a == 0;
          if (!zero_flag) { goto loc_955d; }
          a = ram[AREA_OBJ_PAGE_SET]; zero_flag = a == 0;
          if (!zero_flag) { goto loc_955d; }
          y++;
          a = read_byte(indirect_y_addr(AREA_DATA_PTR));
          a &= 0x1f;
          ram[AREA_OBJ_PAGE] = a;
          ram[AREA_OBJ_PAGE_SET]++;
          goto loc_956e;
        }
        if (a == 0xe) {
          a = ram[BACKLOADING]; zero_flag = a == 0;
          if (!zero_flag) { goto loc_9565; }
        }
        
loc_955d:
        a = ram[AREA_OBJ_PAGE];
        carry_flag = a >= ram[PARSER_PAGE];
        if (!carry_flag) { goto loc_956b; }
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
      a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; neg_flag = (a & 0x80) != 0;
      if (!neg_flag) {
        ram[(AREA_OBJ_LENGTH + x) & 0x7ff]--;
      }
      x--; neg_flag = (x & 0x80) != 0;
    } while (!neg_flag);
    a = ram[PARSER_BEHIND]; zero_flag = a == 0;
    if (!zero_flag) { goto process_area_objects; }
    a = ram[BACKLOADING]; zero_flag = a == 0;
  } while (!zero_flag);
  return;
}

void next_area_object(void) {
  ram[AREA_DATA_POS]++;
  ram[AREA_DATA_POS]++;
  ram[AREA_OBJ_PAGE_SET] = 0x0;
  return;
}

// in: x, y; out: none
void decode_area_object(void) {
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; neg_flag = (a & 0x80) != 0;
  if (!neg_flag) {
    y = ram[(AREA_OBJ_POS + x) & 0x7ff];
  }
  x = 0x10;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  if (a == 0xfd) {
    return;
  }
  a &= 0xf;
  if (a != 0xf) {
    x = 0x8;
    zero_flag = a == 0xc;
    if (zero_flag) { goto loc_95b3; }
    x = 0x0;
  }
  
loc_95b3:
  ram[0x7] = x;
  x = ram[SLOT];
  if (a == 0xe) {
    ram[0x7] = 0x0;
    a = 0x2e;
  } else {
    if (a == 0xd) {
      ram[0x7] = 0x22;
      y++;
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      a &= 0x40; zero_flag = a == 0;
      if (zero_flag) {
        return;
      }
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      a &= 0x7f;
      if (a == 0x4b) {
        ram[LOOP_COMMAND]++;
      }
      a &= 0x3f;
      goto loc_9616;
    }
    if (a < 0xc) {
      y++;
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      a &= 0x70; zero_flag = a == 0;
      if (zero_flag) {
        ram[0x7] = 0x16;
        a = read_byte(indirect_y_addr(AREA_DATA_PTR));
        a &= 0xf;
        goto loc_9616;
      }
      ram[0x0] = a;
      if (a == 0x70) {
        a = read_byte(indirect_y_addr(AREA_DATA_PTR));
        a &= 0x8; zero_flag = a == 0;
        if (zero_flag) { goto loc_9608; }
        ram[0x0] = 0x0;
      }
      
loc_9608:
      a = ram[0x0];
    } else {
      y++;
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      a &= 0x70;
    }
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
  }
  
loc_9616:
  ram[0x0] = a;
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    a = ram[AREA_OBJ_PAGE];
    zero_flag = a == ram[PARSER_PAGE];
    if (!zero_flag) {
      y = ram[AREA_DATA_POS];
      a = read_byte(indirect_y_addr(AREA_DATA_PTR));
      a &= 0xf;
      if (a != 0xe) {
        return;
      }
      a = ram[BACKLOADING]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_9656; }
      return;
    }
    a = ram[BACKLOADING]; zero_flag = a == 0;
    if (!zero_flag) {
      a = 0x0;
      ram[BACKLOADING] = a;
      ram[PARSER_BEHIND] = a;
      ram[SLOT] = a;
      check_loop_object(); // fallthrough
      return;
    }
    y = ram[AREA_DATA_POS];
    a = read_byte(indirect_y_addr(AREA_DATA_PTR));
    a &= 0xf0;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    zero_flag = a == ram[PARSER_COLUMN];
    if (!zero_flag) {
      return;
    }
    
loc_9656:
    ram[(AREA_OBJ_POS + x) & 0x7ff] = ram[AREA_DATA_POS];
    next_area_object();
  }
  a = ram[0x0];
  carry_flag = false;
  a = (uint8_t)(a + ram[0x7] + carry_flag);
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
  y = ram[(AREA_OBJ_POS + x) & 0x7ff];
  y++;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  ram[0x100 + sp--] = a;
  a &= 0x40; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[0x100 + ++sp];
    ram[0x100 + sp--] = a;
    a &= 0xf;
    ram[TERRAIN_KIND] = a;
    a = ram[0x100 + ++sp];
    a &= 0x30;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    ram[BACKGROUND_SCENERY] = a;
    return;
  }
  a = ram[0x100 + ++sp];
  a &= 0x7;
  if (a >= 0x4) {
    ram[BACKGROUND_COLOR] = a;
    a = 0x0;
  }
  ram[FOREGROUND_SCENERY] = a;
  return;
}

void warp_zone_lock(void) {
  x = 0x4;
  a = ram[WORLD_NUMBER]; zero_flag = a == 0;
  if (!zero_flag) {
    x++;
    y = ram[AREA_KIND];
    y--; zero_flag = y == 0;
    if (!zero_flag) { goto loc_9701; }
    x++;
  }
  
loc_9701:
  a = x;
  ram[WARP_ZONE] = a;
  queue_text();
  a = 0xd;
  remove_enemies();
  lock_scroll(); // fallthrough
  return;
}

void lock_scroll(void) {
  a = ram[SCROLL_LOCKED];
  a ^= 0x1;
  ram[SCROLL_LOCKED] = a;
  return;
}

// in: a; out: none
void remove_enemies(void) {
  ram[0x0] = a;
  a = 0x0;
  x = 0x4;
  do {
    y = ram[(uint8_t)(ENEMY_KIND + x)];
    zero_flag = y == ram[0x0];
    if (zero_flag) {
      ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
    }
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

void start_frenzy(void) {
  x = ram[0x0];
  a = ROM((FRENZY_KINDS - 8) + x);
  y = 0x5;
  do {
    y--; neg_flag = (y & 0x80) != 0;
    if (neg_flag) { goto loc_973c; }
    zero_flag = a == ram[ENEMY_KIND + y];
  } while (!zero_flag);
  a = 0x0;
  
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
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  if (!zero_flag) {
    if (neg_flag) {
      ram[(AREA_OBJ_LENGTH + x) & 0x7ff] = y;
      a = ram[PARSER_PAGE];
      a |= ram[PARSER_COLUMN]; zero_flag = a == 0;
      if (zero_flag) { goto loc_9767; }
      a = 0x16;
      draw_stem_none(); return;
    }
    
loc_9767:
    x = ram[0x7];
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
  ram[0x6] = y;
  if (carry_flag) {
    a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
    a = a >> 1;
    ram[(LEDGE_HALF_LENGTH + x) & 0x7ff] = a;
    a = 0x19;
    draw_stem_none(); return;
  }
  a = 0x1b;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; zero_flag = y == 0;
  if (zero_flag) {
    draw_stem_none();
    return;
  }
  ram[0x6] = ram[(LEDGE_HALF_LENGTH + x) & 0x7ff];
  x = ram[0x7];
  ram[METATILE_COLUMN + x] = 0x1a;
  zero_flag = y == ram[0x6];
  if (!zero_flag) {
    return;
  }
  x++;
  ram[METATILE_COLUMN + x] = 0x4f;
  a = 0x50;
  draw_stem_full(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_stem_full(void) {
  x++;
  y = 0xf;
  fill_column_down(); return;
}

// in: a; out: none
void draw_stem_none(void) {
  x = ram[0x7];
  y = 0x0;
  fill_column_down(); return;
}

// in: x; out: none
void draw_pulley_rope(void) {
  get_object_length();
  y = 0x0;
  if (!carry_flag) {
    y++;
    a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_97c8; }
    y++;
  }
  
loc_97c8:
  ram[METATILE_COLUMN] = ROM(PULLEY_ROPE_METATILES + y);
  return;
}

// in: x; out: none
void draw_castle(void) {
  get_object_row_size();
  ram[0x7] = y;
  y = 0x4;
  get_object_fixed_length();
  a = x;
  ram[0x100 + sp--] = a;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  x = ram[0x7];
  ram[0x6] = 0xb;
  do {
    ram[METATILE_COLUMN + x] = ROM(CASTLE_METATILES + y);
    x++;
    a = ram[0x6]; zero_flag = a == 0;
    if (!zero_flag) {
      y++;
      y++;
      y++;
      y++;
      y++;
      ram[0x6]--;
    }
    zero_flag = x == 0xb;
  } while (!zero_flag);
  a = ram[0x100 + ++sp];
  x = a;
  a = ram[PARSER_PAGE]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  if (a != 0x1) {
    y = ram[0x7]; zero_flag = y == 0;
    if (zero_flag) {
      zero_flag = a == 0x3;
      if (zero_flag) { goto loc_9869; }
    }
    if (a != 0x2) {
      return;
    }
    get_object_x();
    ram[0x100 + sp--] = a;
    find_free_enemy_slot();
    a = ram[0x100 + ++sp];
    ram[(uint8_t)(ENEMY_X + x)] = a;
    ram[(uint8_t)(ENEMY_PAGE + x)] = ram[PARSER_PAGE];
    a = 0x1;
    ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
    ram[(uint8_t)(ENEMY_Y + x)] = 0x90;
    ram[(uint8_t)(ENEMY_KIND + x)] = 0x31;
    return;
  }
  
loc_9869:
  ram[(METATILE_COLUMN + 10)] = 0x52;
  return;
}

// in: x; out: none
void draw_water_pipe(void) {
  get_object_row_size();
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  x = ram[0x7];
  ram[METATILE_COLUMN + x] = 0x6b;
  ram[(METATILE_COLUMN + 1) + x] = 0x6c;
  return;
}

// in: x; out: none
void draw_intro_pipe(void) {
  y = 0x3;
  get_object_fixed_length();
  y = 0xa;
  draw_side_pipe();
  if (carry_flag) {
    return;
  }
  x = 0x6;
  do {
    ram[METATILE_COLUMN + x] = 0x0;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  ram[(METATILE_COLUMN + 7)] = ROM(VERTICAL_PIPE_METATILES + y);
  return;
}

// in: x; out: none
void draw_exit_pipe(void) {
  y = 0x3;
  get_object_fixed_length();
  get_object_row_size();
  draw_side_pipe(); // fallthrough
  return;
}

// in: x, y; out: y, carry
void draw_side_pipe(void) {
  y--;
  y--;
  ram[0x5] = y;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  ram[0x6] = y;
  x = ram[0x5];
  x++;
  a = ROM(SIDE_PIPE_SHAFTS + y);
  { uint8_t m = 0x0; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) {
    x = 0x0;
    y = ram[0x5];
    fill_column_down();
    carry_flag = false;
  }
  y = ram[0x6];
  ram[METATILE_COLUMN + x] = ROM(SIDE_PIPE_TOPS + y);
  ram[(METATILE_COLUMN + 1) + x] = ROM(SIDE_PIPE_BOTTOMS + y);
  return;
}

// in: x; out: none
void draw_vertical_pipe(void) {
  get_pipe_height();
  a = ram[0x0]; zero_flag = a == 0;
  if (!zero_flag) {
    y++;
    y++;
    y++;
    y++;
  }
  a = y;
  ram[0x100 + sp--] = a;
  a = ram[AREA_NUMBER];
  a |= ram[WORLD_NUMBER]; zero_flag = a == 0;
  if (!zero_flag) {
    y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; zero_flag = y == 0;
    if (zero_flag) { goto loc_9925; }
    find_free_enemy_slot();
    if (carry_flag) { goto loc_9925; }
    get_object_x();
    carry_flag = false;
    { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[(uint8_t)(ENEMY_X + x)] = a;
    a = ram[PARSER_PAGE];
    a = (uint8_t)(a + 0x0 + carry_flag);
    ram[(uint8_t)(ENEMY_PAGE + x)] = a;
    a = 0x1;
    ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
    get_object_y();
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    ram[(uint8_t)(ENEMY_KIND + x)] = 0xd;
    init_piranha();
  }
  
loc_9925:
  a = ram[0x100 + ++sp];
  y = a;
  x = ram[0x7];
  ram[METATILE_COLUMN + x] = ROM(VERTICAL_PIPE_METATILES + y);
  x++;
  a = ROM((VERTICAL_PIPE_METATILES + 2) + y);
  y = ram[0x6];
  y--;
  fill_column_down(); return;
}

// in: x; out: y
void get_pipe_height(void) {
  y = 0x1;
  get_object_fixed_length();
  get_object_row_size();
  a = y;
  a &= 0x7;
  ram[0x6] = a;
  y = ram[(AREA_OBJ_LENGTH + x) & 0x7ff];
  return;
}

// in: none; out: x, carry
void find_free_enemy_slot(void) {
  x = 0x0;
  do {
    carry_flag = false;
    a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
    if (zero_flag) {
      return;
    }
    x++;
    { uint8_t m = 0x5; carry_flag = x >= m; zero_flag = x == m; }
  } while (!zero_flag);
  return;
}

// in: x; out: none
void draw_water_hole(void) {
  get_object_length();
  ram[(METATILE_COLUMN + 10)] = 0x86;
  x = 0xb;
  y = 0x1;
  a = 0x87;
  fill_column_down(); return;
}

// in: x; out: none
void draw_question_row_high(void) {
  a = 0x3;
  
  draw_question_row(); return;
}

// in: x; out: none
void draw_question_row_low(void) {
  a = 0x7;
  draw_question_row(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_question_row(void) {
  ram[0x100 + sp--] = a;
  get_object_length();
  a = ram[0x100 + ++sp];
  x = a;
  ram[METATILE_COLUMN + x] = 0xc0;
  return;
}

// in: x; out: none
void draw_bridge_high(void) {
  a = 0x6;
  
  draw_bridge_at_row_middle(); return;
}

// in: x; out: none
void draw_bridge_middle(void) {
  a = 0x7;
  draw_bridge_at_row_middle(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_bridge_at_row_middle(void) {
  
  draw_bridge_at_row(); return;
}

// in: x; out: none
void draw_bridge_low(void) {
  a = 0x9;
  draw_bridge_at_row(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_bridge_at_row(void) {
  ram[0x100 + sp--] = a;
  get_object_length();
  a = ram[0x100 + ++sp];
  x = a;
  ram[METATILE_COLUMN + x] = 0xb;
  x++;
  y = 0x0;
  a = 0x63;
  fill_column_down(); return;
}

// in: x; out: none
void draw_flag_balls(void) {
  get_object_row_size();
  x = 0x2;
  a = 0x6d;
  fill_column_down(); return;
}

void draw_flagpole(void) {
  ram[METATILE_COLUMN] = 0x24;
  x = 0x1;
  y = 0x8;
  a = 0x25;
  fill_column_down();
  ram[(METATILE_COLUMN + 10)] = 0x61;
  get_object_x();
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - 0x8 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[(ENEMY_X + 5)] = a;
  a = ram[PARSER_PAGE];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  ram[(ENEMY_PAGE + 5)] = a;
  ram[(ENEMY_Y + 5)] = 0x30;
  ram[FLAG_SCORE_Y] = 0xb0;
  ram[(ENEMY_KIND + 5)] = 0x30;
  ram[(ENEMY_ACTIVE + 5)]++;
  return;
}

void draw_endless_rope(void) {
  x = 0x0;
  y = 0xf;
  draw_rope(); return;
}

// in: x; out: none
void draw_balance_rope(void) {
  a = x;
  ram[0x100 + sp--] = a;
  x = 0x1;
  y = 0xf;
  a = 0x44;
  fill_column_down();
  a = ram[0x100 + ++sp];
  x = a;
  get_object_row_size();
  x = 0x1;
  draw_rope(); // fallthrough
  return;
}

// in: x, y; out: none
void draw_rope(void) {
  a = 0x40;
  fill_column_down(); return;
}

// in: x; out: none
void draw_coin_row(void) {
  y = ram[AREA_KIND];
  a = ROM(COIN_METATILES + y);
  draw_row_of(); return;
}

// in: x; out: none
void draw_castle_bridge(void) {
  y = 0xc;
  get_object_fixed_length();
  draw_small_object(); return;
}

void draw_axe(void) {
  ram[VRAM_LIST_SELECT] = 0x8;
  draw_small_object(); // fallthrough
  return;
}

void draw_small_object(void) {
  y = ram[0x0];
  x = ROM((SMALL_OBJECT_ROWS - 2) + y);
  a = ROM((SMALL_OBJECT_ROWS + 1) + y);
  draw_single_column(); return;
}

// in: x; out: none
void draw_empty_block(void) {
  get_object_row_size();
  x = ram[0x7];
  a = 0xc4;
  draw_single_column(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_single_column(void) {
  y = 0x0;
  fill_column_down(); return;
}

// in: x; out: none
void draw_brick_row(void) {
  y = ram[AREA_KIND];
  a = ram[CLOUD_OVERRIDE]; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x4;
  }
  a = ROM(BRICK_METATILES + y);
  draw_row_of(); return;
}

// in: x; out: none
void draw_solid_row(void) {
  y = ram[AREA_KIND];
  a = ROM(SOLID_BLOCK_METATILES + y);
  draw_row_of(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_row_of(void) {
  ram[0x100 + sp--] = a;
  get_object_length();
  draw_row_height1(); // fallthrough
  return;
}

void draw_row_height1(void) {
  x = ram[0x7];
  y = 0x0;
  a = ram[0x100 + ++sp];
  fill_column_down(); return;
}

// in: x; out: none
void draw_brick_column(void) {
  y = ram[AREA_KIND];
  a = ROM(BRICK_METATILES + y);
  draw_column_of(); return;
}

// in: x; out: none
void draw_solid_column(void) {
  y = ram[AREA_KIND];
  a = ROM(SOLID_BLOCK_METATILES + y);
  draw_column_of(); // fallthrough
  return;
}

// in: a, x; out: none
void draw_column_of(void) {
  ram[0x100 + sp--] = a;
  get_object_row_size();
  a = ram[0x100 + ++sp];
  x = ram[0x7];
  fill_column_down(); return;
}

// in: x; out: none
void draw_cannon(void) {
  get_object_row_size();
  x = ram[0x7];
  ram[METATILE_COLUMN + x] = 0x64;
  x++;
  y--; neg_flag = (y & 0x80) != 0;
  if (!neg_flag) {
    ram[METATILE_COLUMN + x] = 0x65;
    x++;
    y--; neg_flag = (y & 0x80) != 0;
    if (neg_flag) { goto loc_9a85; }
    a = 0x66;
    fill_column_down();
  }
  
loc_9a85:
  x = ram[CANNON_SLOT];
  get_object_y();
  ram[CANNON_Y + x] = a;
  ram[CANNON_PAGE + x] = ram[PARSER_PAGE];
  get_object_x();
  ram[CANNON_X + x] = a;
  x++;
  if (x >= 0x6) {
    x = 0x0;
  }
  ram[CANNON_SLOT] = x;
  return;
}

// in: x; out: none
void draw_staircase(void) {
  get_object_length();
  if (carry_flag) {
    ram[STAIRCASE_STEP] = 0x9;
  }
  ram[STAIRCASE_STEP]--;
  y = ram[STAIRCASE_STEP];
  x = ROM(STAIRCASE_ROWS + y);
  a = ROM(STAIRCASE_HEIGHTS + y);
  y = a;
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
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 0x1;
  ram[(uint8_t)(ENEMY_ACTIVE + x)]++;
  x = ram[0x7];
  ram[METATILE_COLUMN + x] = 0x67;
  ram[(METATILE_COLUMN + 1) + x] = 0x68;
  return;
}

// in: x; out: none
void draw_hidden_1up(void) {
  a = ram[HIDDEN_1UP]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  ram[HIDDEN_1UP] = 0x0;
  draw_item_brick(); return;
}

// in: x; out: none
void draw_question_block(void) {
  get_area_object_kind();
  draw_block_metatile(); return;
}

// in: x; out: none
void draw_coin_brick(void) {
  ram[BRICK_COIN_ACTIVE] = 0x0;
  draw_item_brick(); // fallthrough
  return;
}

// in: x; out: none
void draw_item_brick(void) {
  get_area_object_kind();
  ram[0x7] = y;
  a = 0x0;
  y = ram[AREA_KIND];
  y--; zero_flag = y == 0;
  if (!zero_flag) {
    a = 0x5;
  }
  carry_flag = false;
  a = (uint8_t)(a + ram[0x7] + carry_flag);
  y = a;
  draw_block_metatile(); // fallthrough
  return;
}

// in: x, y; out: none
void draw_block_metatile(void) {
  a = ROM(BUMPABLE_METATILES + y);
  ram[0x100 + sp--] = a;
  get_object_row_size();
  draw_row_height1(); return;
}

// in: none; out: y
void get_area_object_kind(void) {
  a = ram[0x0];
  carry_flag = true;
  a = (uint8_t)(a - 0x0 - !carry_flag);
  y = a;
  return;
}

// in: x; out: none
void draw_hole(void) {
  get_object_length();
  if (carry_flag) {
    a = ram[AREA_KIND]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_9b73; }
    x = ram[CANNON_SLOT];
    get_object_x();
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - 0x10 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[CANNON_X + x] = a;
    a = ram[PARSER_PAGE];
    a = (uint8_t)(a - 0x0 - !carry_flag);
    ram[CANNON_PAGE + x] = a;
    y++;
    y++;
    a = y;
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    ram[CANNON_Y + x] = a;
    x++;
    if (x >= 0x5) {
      x = 0x0;
    }
    ram[CANNON_SLOT] = x;
  }
  
loc_9b73:
  x = ram[AREA_KIND];
  a = ROM(HOLE_METATILES + x);
  x = 0x8;
  y = 0xf;
  fill_column_down(); // fallthrough
  return;
}

// in: a, x, y; out: x, y
void fill_column_down(void) {
  do {
    ram[AREA_OBJ_HEIGHT] = y;
    y = ram[METATILE_COLUMN + x]; zero_flag = y == 0;
    if (!zero_flag) {
      zero_flag = y == 0x17;
      if (zero_flag) { goto loc_9ba0; }
      zero_flag = y == 0x1a;
      if (zero_flag) { goto loc_9ba0; }
      zero_flag = y == 0xc0;
      if (zero_flag) { goto loc_9b9d; }
      carry_flag = y >= 0xc0;
      if (carry_flag) { goto loc_9ba0; }
      zero_flag = y == 0x54;
      if (!zero_flag) { goto loc_9b9d; }
      zero_flag = a == 0x50;
      if (zero_flag) { goto loc_9ba0; }
    }
    
loc_9b9d:
    ram[METATILE_COLUMN + x] = a;
    
loc_9ba0:
    x++;
    if (x >= 0xd) {
      return;
    }
    y = ram[AREA_OBJ_HEIGHT];
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: x; out: y, carry
void get_object_length(void) {
  get_object_row_size();
  get_object_fixed_length(); // fallthrough
  return;
}

// in: x, y; out: carry
void get_object_fixed_length(void) {
  a = ram[(AREA_OBJ_LENGTH + x) & 0x7ff]; neg_flag = (a & 0x80) != 0;
  carry_flag = false;
  if (!neg_flag) {
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
  a &= 0xf;
  ram[0x7] = a;
  y++;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  a &= 0xf;
  y = a;
  return;
}

// in: none; out: a
void get_object_x(void) {
  a = ram[PARSER_COLUMN];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  return;
}

// in: none; out: a
void get_object_y(void) {
  a = ram[0x7];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + 0x20 + carry_flag);
  return;
}

// in: a; out: none
void get_block_buffer_address(void) {
  ram[0x100 + sp--] = a;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  y = a;
  ram[0x7] = ROM((BLOCK_BUFFER_ADDRESSES + 2) + y);
  a = ram[0x100 + ++sp];
  a &= 0xf;
  carry_flag = false;
  a = (uint8_t)(a + ROM(BLOCK_BUFFER_ADDRESSES + y) + carry_flag);
  ram[0x6] = a;
  return;
}

void load_area_pointer(void) {
  find_area_pointer();
  ram[AREA_POINTER] = a;
  get_area_kind(); // fallthrough
  return;
}

// in: a; out: a
void get_area_kind(void) {
  a &= 0x60;
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  ram[AREA_KIND] = a;
  return;
}

// in: none; out: a
void find_area_pointer(void) {
  y = ram[WORLD_NUMBER];
  a = ROM(WORLD_AREA_OFFSETS + y);
  carry_flag = false;
  a = (uint8_t)(a + ram[AREA_NUMBER] + carry_flag);
  y = a;
  a = ROM(AREAS_WORLD1 + y);
  return;
}

// level layout and enemy list pointers, header attributes
void load_area_data(void) {
  a = ram[AREA_POINTER];
  get_area_kind();
  y = a;
  a = ram[AREA_POINTER];
  a &= 0x1f;
  ram[AREA_POINTER_OFFSET] = a;
  a = ROM(ENEMY_LIST_KIND_OFFSETS + y);
  carry_flag = false;
  a = (uint8_t)(a + ram[AREA_POINTER_OFFSET] + carry_flag);
  y = a;
  ram[ENEMY_DATA_PTR] = ROM(ENEMY_LISTS_LOW + y);
  ram[(ENEMY_DATA_PTR + 1)] = ROM(ENEMY_LISTS_HIGH + y);
  y = ram[AREA_KIND];
  a = ROM(AREA_LAYOUT_KIND_OFFSETS + y);
  carry_flag = false;
  a = (uint8_t)(a + ram[AREA_POINTER_OFFSET] + carry_flag);
  y = a;
  ram[AREA_DATA_PTR] = ROM(AREA_LAYOUTS_LOW + y);
  ram[(AREA_DATA_PTR + 1)] = ROM(AREA_LAYOUTS_HIGH + y);
  y = 0x0;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  ram[0x100 + sp--] = a;
  a &= 0x7;
  if (a >= 0x4) {
    ram[BACKGROUND_COLOR] = a;
    a = 0x0;
  }
  ram[FOREGROUND_SCENERY] = a;
  a = ram[0x100 + ++sp];
  ram[0x100 + sp--] = a;
  a &= 0x38;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  ram[ENTRANCE_KIND] = a;
  a = ram[0x100 + ++sp];
  a &= 0xc0;
  carry_flag = false;
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  ram[TIME_LIMIT] = a;
  y++;
  a = read_byte(indirect_y_addr(AREA_DATA_PTR));
  ram[0x100 + sp--] = a;
  a &= 0xf;
  ram[TERRAIN_KIND] = a;
  a = ram[0x100 + ++sp];
  ram[0x100 + sp--] = a;
  a &= 0x30;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  ram[BACKGROUND_SCENERY] = a;
  a = ram[0x100 + ++sp];
  a &= 0xc0;
  carry_flag = false;
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  if (a == 0x3) {
    ram[CLOUD_OVERRIDE] = a;
    a = 0x0;
  }
  ram[AREA_STYLE] = a;
  a = ram[AREA_DATA_PTR];
  carry_flag = false;
  { uint16_t sum = a + 0x2 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[AREA_DATA_PTR] = a;
  a = ram[(AREA_DATA_PTR + 1)];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(AREA_DATA_PTR + 1)] = a;
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
  a = ram[GAME_MODE_TASK];
  if (a < 0x3) {
    return;
  }
  update_fireballs_and_bubbles();
  x = 0x0;
  do {
    ram[SLOT] = x;
    update_enemies_and_loops();
    update_score_popups();
    x++;
    zero_flag = x == 0x6;
  } while (!zero_flag);
  player_offscreen_bits();
  player_screen_position();
  draw_player();
  update_block_metatiles();
  x = 0x1;
  ram[SLOT] = x;
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
  a = ram[PLAYER_Y_PAGE];
  neg_flag = ((a - 0x2) & 0x80) != 0;
  if (neg_flag) {
    a = ram[STAR_TIMER]; zero_flag = a == 0;
    if (zero_flag) { goto loc_af64; }
    zero_flag = a == 0x4;
    if (!zero_flag) { goto loc_af52; }
    a = ram[INTERVAL_TIMER_PERIOD]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_af52; }
    queue_area_music();
  }
  
loc_af52:
  y = ram[STAR_TIMER];
  a = ram[FRAME_COUNT];
  if (y < 0x8) {
    a = a >> 1;
    a = a >> 1;
  }
  a = a >> 1;
  cycle_player_palette();
  goto loc_af67;
  
loc_af64:
  reset_player_palette();
  
loc_af67:
  ram[BUTTONS_AB_PREVIOUS] = ram[BUTTONS_AB];
  ram[BUTTONS_HORIZONTAL] = 0x0;
  update_scroll_column(); // fallthrough
  return;
}

void update_scroll_column(void) {
  a = ram[VRAM_LIST_SELECT];
  if (a == 0x6) {
    return;
  }
  a = ram[PARSER_TASK]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[SCROLLED_32];
    { uint8_t m = 0x20; carry_flag = a >= m; neg_flag = ((a - m) & 0x80) != 0; }
    if (neg_flag) {
      return;
    }
    a = ram[SCROLLED_32];
    a = (uint8_t)(a - 0x20 - !carry_flag);
    ram[SCROLLED_32] = a;
    ram[VRAM_LIST2_LENGTH] = 0x0;
  }
  run_parser_task();
  return;
}

void scroll_with_player(void) {
  a = ram[PLAYER_SCROLL_X];
  carry_flag = false;
  a = (uint8_t)(a + ram[PLATFORM_X_SCROLL] + carry_flag);
  ram[PLAYER_SCROLL_X] = a;
  a = ram[SCROLL_LOCKED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[PLAYER_SCROLL_POS];
    carry_flag = a >= 0x50;
    if (!carry_flag) { goto loc_affb; }
    a = ram[SIDE_CONTACT_TIMER]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_affb; }
    y = ram[PLAYER_SCROLL_X];
    y--; neg_flag = (y & 0x80) != 0;
    if (neg_flag) { goto loc_affb; }
    y++;
    if (y >= 0x2) {
      y--;
    }
    a = ram[PLAYER_SCROLL_POS];
    if (a < 0x70) {
      scroll_screen();
      return;
    }
    y = ram[PLAYER_SCROLL_X];
    scroll_screen(); // fallthrough
    return;
  }
  
loc_affb:
  ram[SCROLL_AMOUNT] = 0x0;
  check_player_offscreen(); // fallthrough
  return;
}

// in: y; out: none
void scroll_screen(void) {
  a = y;
  ram[SCROLL_AMOUNT] = a;
  carry_flag = false;
  a = (uint8_t)(a + ram[SCROLLED_32] + carry_flag);
  ram[SCROLLED_32] = a;
  a = y;
  carry_flag = false;
  { uint16_t sum = a + ram[SCREEN_LEFT_X] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[SCREEN_LEFT_X] = a;
  ram[SCROLL_X] = a;
  a = ram[SCREEN_LEFT_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[SCREEN_LEFT_PAGE] = a;
  a &= 0x1;
  ram[0x0] = a;
  a = ram[PPU_CTRL_COPY];
  a &= 0xfe;
  a |= ram[0x0];
  ram[PPU_CTRL_COPY] = a;
  update_screen_edges();
  ram[SCROLL_TIMER] = 0x8;
  check_player_offscreen(); return;
}

void check_player_offscreen(void) {
  x = 0x0;
  compute_x_offscreen_bits();
  ram[0x0] = a;
  y = 0x0;
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (!carry_flag) {
    y++;
    a = ram[0x0];
    a &= 0x20; zero_flag = a == 0;
    if (zero_flag) { goto loc_b02e; }
  }
  a = ram[(SCREEN_LEFT_X + y) & 0x7ff];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ROM(SCREEN_EDGE_OFFSETS + y) - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[PLAYER_X] = a;
  a = ram[(SCREEN_LEFT_PAGE + y) & 0x7ff];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  ram[PLAYER_PAGE] = a;
  a = ram[BUTTONS_HORIZONTAL];
  zero_flag = a == ROM(OFFSCREEN_JOYPAD_MASKS + y);
  if (!zero_flag) {
    ram[PLAYER_X_SPEED] = 0x0;
  }
  
loc_b02e:
  ram[PLATFORM_X_SCROLL] = 0x0;
  return;
}

// in: none; out: a
void update_screen_edges(void) {
  a = ram[SCREEN_LEFT_X];
  carry_flag = false;
  { uint16_t sum = a + 0xff + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[SCREEN_RIGHT_X] = a;
  a = ram[SCREEN_LEFT_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
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
  a = ram[ALT_ENTRANCE];
  if (a != 0x2) {
    a = 0x0;
    y = ram[PLAYER_Y];
    if (y < 0x30) {
      drive_player();
      return;
    }
    a = ram[ENTRANCE_KIND];
    if (a != 0x6) {
      zero_flag = a == 0x7;
      if (!zero_flag) { goto loc_b0d3; }
    }
    a = ram[PLAYER_SPRITE_ATTR]; zero_flag = a == 0;
    if (zero_flag) {
      a = 0x1;
      drive_player(); return;
    }
    walk_into_side_pipe();
    ram[CHANGE_AREA_TIMER]--; zero_flag = ram[CHANGE_AREA_TIMER] == 0;
    if (!zero_flag) {
      return;
    }
    ram[SKIP_INTERMISSION]++;
    next_area(); return;
  }
  a = ram[JOYPAD_OVERRIDE]; zero_flag = a == 0;
  if (zero_flag) {
    a = 0xff;
    move_player_down();
    a = ram[PLAYER_Y];
    carry_flag = a >= 0x91;
    if (!carry_flag) { goto loc_b0d3; }
    return;
  }
  a = ram[VINE_HEIGHT];
  if (a != 0x60) {
    return;
  }
  a = ram[PLAYER_Y];
  carry_flag = a >= 0x99;
  y = 0x0;
  a = 0x1;
  if (carry_flag) {
    ram[PLAYER_STATE] = 0x3;
    y++;
    a = 0x8;
    ram[(BLOCK_BUFFER1 + 180)] = a;
  }
  ram[COLLISIONS_OFF] = y;
  drive_player();
  a = ram[PLAYER_X];
  if (a < 0x48) {
    return;
  }
  
loc_b0d3:
  ram[PLAYER_TASK] = 0x8;
  a = 0x1;
  ram[PLAYER_FACING] = a;
  a = a >> 1;
  ram[ALT_ENTRANCE] = a;
  ram[COLLISIONS_OFF] = a;
  ram[JOYPAD_OVERRIDE] = a;
  return;
}

// in: a; out: none
void drive_player(void) {
  ram[JOYPAD1] = a;
  control_player(); // fallthrough
  return;
}

// joypad to movement, collisions, animation
void control_player(void) {
  a = ram[PLAYER_TASK];
  if (a != 0xb) {
    a = ram[AREA_KIND]; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[PLAYER_Y_PAGE];
      y--; zero_flag = y == 0;
      if (zero_flag) {
        a = ram[PLAYER_Y];
        carry_flag = a >= 0xd0;
        if (!carry_flag) { goto loc_b104; }
      }
      ram[JOYPAD1] = 0x0;
    }
    
loc_b104:
    a = ram[JOYPAD1];
    a &= 0xc0;
    ram[BUTTONS_AB] = a;
    a = ram[JOYPAD1];
    a &= 0x3;
    ram[BUTTONS_HORIZONTAL] = a;
    a = ram[JOYPAD1];
    a &= 0xc;
    ram[BUTTONS_VERTICAL] = a;
    a &= 0x4; zero_flag = a == 0;
    if (zero_flag) { goto loc_b12b; }
    a = ram[PLAYER_STATE]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_b12b; }
    y = ram[BUTTONS_HORIZONTAL]; zero_flag = y == 0;
    if (zero_flag) { goto loc_b12b; }
    a = 0x0;
    ram[BUTTONS_HORIZONTAL] = a;
    ram[BUTTONS_VERTICAL] = a;
  }
  
loc_b12b:
  move_player();
  y = 0x1;
  a = ram[PLAYER_SMALL]; zero_flag = a == 0;
  if (zero_flag) {
    y = 0x0;
    a = ram[CROUCHING]; zero_flag = a == 0;
    if (zero_flag) { goto loc_b13e; }
    y = 0x2;
  }
  
loc_b13e:
  ram[PLAYER_BOX_KIND] = y;
  a = 0x1;
  y = ram[PLAYER_X_SPEED]; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  if (!zero_flag) {
    if (neg_flag) {
      a = (uint8_t)(a << 1);
    }
    ram[PLAYER_DIR] = a;
  }
  scroll_with_player();
  player_offscreen_bits();
  player_screen_position();
  x = 0x0;
  compute_box();
  player_bg_collision();
  a = ram[PLAYER_Y];
  if (a >= 0x40) {
    a = ram[PLAYER_TASK];
    zero_flag = a == 0x5;
    if (zero_flag) { goto loc_b179; }
    zero_flag = a == 0x7;
    if (zero_flag) { goto loc_b179; }
    carry_flag = a >= 0x4;
    if (!carry_flag) { goto loc_b179; }
    a = ram[PLAYER_SPRITE_ATTR];
    a &= 0xdf;
    ram[PLAYER_SPRITE_ATTR] = a;
  }
  
loc_b179:
  a = ram[PLAYER_Y_PAGE];
  neg_flag = ((a - 0x2) & 0x80) != 0;
  if (neg_flag) {
    return;
  }
  ram[SCROLL_LOCKED] = 0x1;
  ram[0x7] = 0x4;
  x = 0x0;
  y = ram[TIME_UP]; zero_flag = y == 0;
  if (zero_flag) {
    y = ram[CLOUD_OVERRIDE]; zero_flag = y == 0;
    if (!zero_flag) { goto loc_b1aa; }
  }
  x++;
  y = ram[PLAYER_TASK];
  if (y != 0xb) {
    y = ram[DEATH_MUSIC_QUEUED]; zero_flag = y == 0;
    if (zero_flag) {
      y++;
      ram[EVENT_MUSIC_REQUEST] = y;
      ram[DEATH_MUSIC_QUEUED] = y;
    }
    ram[0x7] = 0x6;
  }
  
loc_b1aa:
  neg_flag = ((a - ram[0x7]) & 0x80) != 0;
  if (neg_flag) {
    return;
  }
  x--; neg_flag = (x & 0x80) != 0;
  if (!neg_flag) {
    y = ram[EVENT_MUSIC_PLAYING]; zero_flag = y == 0;
    if (!zero_flag) {
      return;
    }
    ram[PLAYER_TASK] = 0x6;
    return;
  }
  ram[JOYPAD_OVERRIDE] = 0x0;
  set_alt_entrance();
  ram[ALT_ENTRANCE]++;
  return;
}

void vine_climb_in(void) {
  a = ram[PLAYER_Y_PAGE]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[PLAYER_Y];
    if (a < 0xe4) {
      set_alt_entrance();
      return;
    }
  }
  a = 0x8;
  ram[JOYPAD_OVERRIDE] = a;
  ram[PLAYER_STATE] = 0x3;
  drive_player(); return;
}

void set_alt_entrance(void) {
  ram[ALT_ENTRANCE] = 0x2;
  change_area(); return;
}

void enter_down_pipe(void) {
  a = 0x1;
  move_player_down();
  scroll_with_player();
  y = 0x0;
  a = ram[WARP_ZONE]; zero_flag = a == 0;
  if (!zero_flag) {
    change_area_after_pipe();
    return;
  }
  y++;
  a = ram[AREA_KIND];
  if (a != 0x3) {
    change_area_after_pipe();
    return;
  }
  y++;
  change_area_after_pipe(); return;
}

// in: a; out: none
void move_player_down(void) {
  carry_flag = false;
  a = (uint8_t)(a + ram[PLAYER_Y] + carry_flag);
  ram[PLAYER_Y] = a;
  return;
}

void enter_side_pipe_exit(void) {
  walk_into_side_pipe();
  y = 0x2;
  change_area_after_pipe(); // fallthrough
  return;
}

// in: y; out: none
void change_area_after_pipe(void) {
  ram[CHANGE_AREA_TIMER]--; zero_flag = ram[CHANGE_AREA_TIMER] == 0;
  if (!zero_flag) {
    return;
  }
  ram[ALT_ENTRANCE] = y;
  change_area(); // fallthrough
  return;
}

// in: none; out: a
void change_area(void) {
  ram[SCREEN_OFF]++;
  a = 0x0;
  ram[GAME_MODE_TASK] = a;
  ram[STATUS_BAR_SPLIT] = a;
  return;
}

void walk_into_side_pipe(void) {
  ram[PLAYER_X_SPEED] = 0x8;
  y = 0x1;
  a = ram[PLAYER_X];
  a &= 0xf; zero_flag = a == 0;
  if (zero_flag) {
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
  { uint8_t m = 0xf0; carry_flag = a >= m; zero_flag = a == m; }
  if (!carry_flag) {
    if (a == 0xc8) {
      player_task_done();
      return;
    }
    control_player(); return;
  }
  if (!zero_flag) {
    return;
  }
  start_size_change(); // fallthrough
  return;
}

void start_size_change(void) {
  y = ram[PLAYER_CHANGING_SIZE]; zero_flag = y == 0;
  if (!zero_flag) {
    return;
  }
  ram[PLAYER_ANIM_FRAME] = y;
  ram[PLAYER_CHANGING_SIZE]++;
  a = ram[PLAYER_SMALL];
  a ^= 0x1;
  ram[PLAYER_SMALL] = a;
  return;
}

void player_dying(void) {
  a = ram[TIMERS_PAUSED];
  if (a >= 0xf0) {
    return;
  }
  control_player(); return;
}

void player_task_done(void) {
  ram[TIMERS_PAUSED] = 0x0;
  ram[PLAYER_TASK] = 0x8;
  return;
}

void get_fire_flower(void) {
  a = ram[TIMERS_PAUSED];
  if (a != 0xc0) {
    a = ram[FRAME_COUNT];
    a = a >> 1;
    a = a >> 1;
    cycle_player_palette(); // fallthrough
    return;
  }
  player_task_done();
  reset_player_palette(); // fallthrough
  return;
}

// in: a; out: none
void cycle_player_palette(void) {
  a &= 0x3;
  ram[0x0] = a;
  a = ram[PLAYER_SPRITE_ATTR];
  a &= 0xfc;
  a |= ram[0x0];
  ram[PLAYER_SPRITE_ATTR] = a;
  return;
}

void reset_player_palette(void) {
  a = ram[PLAYER_SPRITE_ATTR];
  a &= 0xfc;
  ram[PLAYER_SPRITE_ATTR] = a;
  return;
}

void slide_down_flagpole(void) {
  a = ram[(ENEMY_KIND + 5)];
  if (a == 0x30) {
    ram[PULSE1_SFX_REQUEST] = ram[FLAGPOLE_SOUND];
    a = 0x0;
    ram[FLAGPOLE_SOUND] = a;
    y = ram[PLAYER_Y];
    if (y < 0x9e) {
      a = 0x4;
    }
    drive_player(); return;
  }
  ram[PLAYER_TASK]++;
  return;
}

void walk_to_castle(void) {
  a = 0x1;
  drive_player();
  a = ram[PLAYER_Y];
  if (a >= 0xae) {
    a = ram[SCROLL_LOCKED]; zero_flag = a == 0;
    if (zero_flag) { goto loc_b2e3; }
    ram[EVENT_MUSIC_REQUEST] = 0x20;
    ram[SCROLL_LOCKED] = 0x0;
  }
  
loc_b2e3:
  a = ram[PLAYER_COLLISION];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    a = ram[STAR_FLAG_TASK]; zero_flag = a == 0;
    if (zero_flag) {
      ram[STAR_FLAG_TASK]++;
    }
    ram[PLAYER_SPRITE_ATTR] = 0x20;
  }
  a = ram[STAR_FLAG_TASK];
  if (a != 0x5) {
    return;
  }
  ram[LEVEL_NUMBER]++;
  a = ram[LEVEL_NUMBER];
  if (a != 0x3) {
    next_area();
    return;
  }
  y = ram[WORLD_NUMBER];
  a = ram[COINS_FOR_1UPS];
  carry_flag = a >= ROM(HIDDEN_1UP_COIN_COUNTS + y);
  if (!carry_flag) {
    next_area();
    return;
  }
  ram[HIDDEN_1UP]++;
  next_area(); // fallthrough
  return;
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
  a = 0x0;
  y = ram[PLAYER_SMALL]; zero_flag = y == 0;
  if (zero_flag) {
    a = ram[PLAYER_STATE]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_b33b; }
    a = ram[BUTTONS_VERTICAL];
    a &= 0x4;
  }
  ram[CROUCHING] = a;
  
loc_b33b:
  player_physics();
  a = ram[PLAYER_CHANGING_SIZE]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[PLAYER_STATE];
  if (a != 0x3) {
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
  a = ram[BUTTONS_HORIZONTAL]; zero_flag = a == 0;
  if (!zero_flag) {
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
  y = ram[PLAYER_Y_SPEED]; neg_flag = (y & 0x80) != 0;
  if (neg_flag) {
    a = ram[BUTTONS_AB];
    a &= 0x80;
    a &= ram[BUTTONS_AB_PREVIOUS]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_b393; }
    a = ram[JUMP_ORIGIN_Y];
    carry_flag = true;
    a = (uint8_t)(a - ram[PLAYER_Y] - !carry_flag);
    carry_flag = a >= ram[JUMP_CUT_SPEED];
    if (!carry_flag) { goto loc_b393; }
  }
  ram[GRAVITY] = ram[GRAVITY_FALLING];
  
loc_b393:
  a = ram[SWIMMING]; zero_flag = a == 0;
  if (zero_flag) {
    move_in_air();
    return;
  }
  set_player_anim_speed();
  a = ram[PLAYER_Y];
  if (a < 0x14) {
    ram[GRAVITY] = 0x18;
  }
  a = ram[BUTTONS_HORIZONTAL]; zero_flag = a == 0;
  if (zero_flag) {
    move_in_air();
    return;
  }
  ram[PLAYER_FACING] = a;
  move_in_air(); // fallthrough
  return;
}

void move_in_air(void) {
  a = ram[BUTTONS_HORIZONTAL]; zero_flag = a == 0;
  if (!zero_flag) {
    apply_friction();
  }
  move_player_x();
  ram[PLAYER_SCROLL_X] = a;
  a = ram[PLAYER_TASK];
  if (a == 0xb) {
    ram[GRAVITY] = 0x28;
  }
  x = 0x0;
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[SPRING_ANIM_STEP]; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
  }
  ram[0x0] = ram[GRAVITY];
  a = 0x4;
  apply_gravity_object(); return;
}

void move_climbing(void) {
  a = ram[PLAYER_Y_FRACTION];
  carry_flag = false;
  { uint16_t sum = a + ram[PLAYER_Y_FORCE] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[PLAYER_Y_FRACTION] = a;
  y = 0x0;
  a = ram[PLAYER_Y_SPEED]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    y--;
  }
  ram[0x0] = y;
  { uint16_t sum = a + ram[PLAYER_Y] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[PLAYER_Y] = a;
  a = ram[PLAYER_Y_PAGE];
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  ram[PLAYER_Y_PAGE] = a;
  a = ram[BUTTONS_HORIZONTAL];
  a &= ram[PLAYER_COLLISION]; zero_flag = a == 0;
  if (!zero_flag) {
    y = ram[CLIMB_SIDE_TIMER]; zero_flag = y == 0;
    if (!zero_flag) {
      return;
    }
    ram[CLIMB_SIDE_TIMER] = 0x18;
    x = 0x0;
    y = ram[PLAYER_FACING];
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (!carry_flag) {
      x++;
      x++;
    }
    y--; zero_flag = y == 0;
    if (!zero_flag) {
      x++;
    }
    a = ram[PLAYER_X];
    carry_flag = false;
    { uint16_t sum = a + ROM(CLIMB_STEPS_LOW + x) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[PLAYER_X] = a;
    a = ram[PLAYER_PAGE];
    a = (uint8_t)(a + ROM(CLIMB_STEPS_HIGH + x) + carry_flag);
    ram[PLAYER_PAGE] = a;
    a = ram[BUTTONS_HORIZONTAL];
    a ^= 0x3;
    ram[PLAYER_FACING] = a;
    return;
  }
  ram[CLIMB_SIDE_TIMER] = a;
  return;
}

void player_physics(void) {
  a = ram[PLAYER_STATE];
  if (a == 0x3) {
    y = 0x0;
    a = ram[BUTTONS_VERTICAL];
    a &= ram[PLAYER_COLLISION]; zero_flag = a == 0;
    if (!zero_flag) {
      y++;
      a &= 0x8; zero_flag = a == 0;
      if (!zero_flag) { goto loc_b465; }
      y++;
    }
    
loc_b465:
    ram[PLAYER_Y_FORCE] = ROM(CLIMB_FORCES + y);
    a = 0x8;
    x = ROM(CLIMB_SPEEDS + y); neg_flag = (x & 0x80) != 0;
    ram[PLAYER_Y_SPEED] = x;
    if (!neg_flag) {
      a = a >> 1;
    }
    ram[PLAYER_ANIM_PERIOD] = a;
    return;
  }
  a = ram[SPRING_ANIM_STEP]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[BUTTONS_AB];
    a &= 0x80; zero_flag = a == 0;
    if (zero_flag) { goto loc_b488; }
    a &= ram[BUTTONS_AB_PREVIOUS]; zero_flag = a == 0;
    if (zero_flag) { goto loc_b48b; }
  }
  
loc_b488:
  goto loc_b51c;
  
loc_b48b:
  a = ram[PLAYER_STATE]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[SWIMMING]; zero_flag = a == 0;
    if (zero_flag) { goto loc_b488; }
    a = ram[JUMP_SWIM_TIMER]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_b4a0; }
    a = ram[PLAYER_Y_SPEED]; neg_flag = (a & 0x80) != 0;
    if (!neg_flag) { goto loc_b4a0; }
  } else {
    
loc_b4a0:
    ram[JUMP_SWIM_TIMER] = 0x20;
    y = 0x0;
    ram[PLAYER_Y_FRACTION] = y;
    ram[PLAYER_Y_FORCE] = y;
    ram[JUMP_ORIGIN_Y_PAGE] = ram[PLAYER_Y_PAGE];
    ram[JUMP_ORIGIN_Y] = ram[PLAYER_Y];
    ram[PLAYER_STATE] = 0x1;
    a = ram[PLAYER_ABS_SPEED];
    if (a >= 0x9) {
      y++;
      carry_flag = a >= 0x10;
      if (!carry_flag) { goto loc_b4d2; }
      y++;
      carry_flag = a >= 0x19;
      if (!carry_flag) { goto loc_b4d2; }
      y++;
      carry_flag = a >= 0x1c;
      if (!carry_flag) { goto loc_b4d2; }
      y++;
    }
    
loc_b4d2:
    ram[JUMP_CUT_SPEED] = 0x1;
    a = ram[SWIMMING]; zero_flag = a == 0;
    if (!zero_flag) {
      y = 0x5;
      a = ram[CANNON_TIMER]; zero_flag = a == 0;
      if (zero_flag) { goto loc_b4e4; }
      y++;
    }
    
loc_b4e4:
    ram[GRAVITY] = ROM(JUMP_GRAVITY + y);
    ram[GRAVITY_FALLING] = ROM(FALL_GRAVITY + y);
    ram[PLAYER_Y_FORCE] = ROM(JUMP_INITIAL_FORCE + y);
    ram[PLAYER_Y_SPEED] = ROM(JUMP_SPEEDS + y);
    a = ram[SWIMMING]; zero_flag = a == 0;
    if (!zero_flag) {
      ram[PULSE1_SFX_REQUEST] = 0x4;
      a = ram[PLAYER_Y];
      carry_flag = a >= 0x14;
      if (carry_flag) { goto loc_b51c; }
      ram[PLAYER_Y_SPEED] = 0x0;
      goto loc_b51c;
    }
    a = 0x1;
    y = ram[PLAYER_SMALL]; zero_flag = y == 0;
    if (!zero_flag) {
      a = 0x80;
    }
    ram[PULSE1_SFX_REQUEST] = a;
  }
  
loc_b51c:
  y = 0x0;
  ram[0x0] = y;
  a = ram[PLAYER_STATE]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[PLAYER_ABS_SPEED];
    carry_flag = a >= 0x19;
    if (carry_flag) { goto loc_b55e; }
  } else {
    y++;
    a = ram[AREA_KIND]; zero_flag = a == 0;
    if (zero_flag) { goto loc_b545; }
    y--;
    a = ram[BUTTONS_HORIZONTAL];
    zero_flag = a == ram[PLAYER_DIR];
    if (!zero_flag) { goto loc_b545; }
    a = ram[BUTTONS_AB];
    a &= 0x40; zero_flag = a == 0;
    if (!zero_flag) { goto loc_b559; }
    a = ram[RUNNING_TIMER]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_b55e; }
  }
  
loc_b545:
  y++;
  ram[0x0]++;
  a = ram[RUNNING_SPEED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[PLAYER_ABS_SPEED];
    carry_flag = a >= 0x21;
    if (!carry_flag) { goto loc_b55e; }
  }
  ram[0x0]++;
  goto loc_b55e;
  
loc_b559:
  ram[RUNNING_TIMER] = 0xa;
  
loc_b55e:
  ram[MAX_LEFT_SPEED] = ROM(MAX_LEFT_SPEEDS + y);
  a = ram[PLAYER_TASK];
  if (a == 0x7) {
    y = 0x3;
  }
  ram[MAX_RIGHT_SPEED] = ROM(MAX_RIGHT_SPEEDS + y);
  y = ram[0x0];
  ram[FRICTION_LOW] = ROM(FRICTION_AMOUNTS + y);
  ram[FRICTION_HIGH] = 0x0;
  a = ram[PLAYER_FACING];
  zero_flag = a == ram[PLAYER_DIR];
  if (zero_flag) {
    return;
  }
  carry_flag = (ram[FRICTION_LOW] & 0x80) != 0; ram[FRICTION_LOW] = (uint8_t)(ram[FRICTION_LOW] << 1);
  ram[FRICTION_HIGH] = (uint8_t)(ram[FRICTION_HIGH] << 1 | carry_flag);
  return;
}

void set_player_anim_speed(void) {
  y = 0x0;
  a = ram[PLAYER_ABS_SPEED];
  if (a < 0x1c) {
    y++;
    if (a < 0xe) {
      y++;
    }
    a = ram[JOYPAD1];
    a &= 0x7f; zero_flag = a == 0;
    if (zero_flag) { goto loc_b5c5; }
    a &= 0x3;
    zero_flag = a == ram[PLAYER_DIR];
    if (!zero_flag) { goto loc_b5b3; }
    a = 0x0;
  }
  ram[RUNNING_SPEED] = a;
  goto loc_b5c5;
  
loc_b5b3:
  a = ram[PLAYER_ABS_SPEED];
  if (a < 0xb) {
    ram[PLAYER_DIR] = ram[PLAYER_FACING];
    a = 0x0;
    ram[PLAYER_X_SPEED] = a;
    ram[PLAYER_X_FORCE] = a;
  }
  
loc_b5c5:
  ram[PLAYER_ANIM_PERIOD] = ROM(PLAYER_ANIM_PERIODS + y);
  return;
}

// in: a; out: none
void apply_friction(void) {
  a &= ram[PLAYER_COLLISION];
  if (a == 0x0) {
    a = ram[PLAYER_X_SPEED]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    if (zero_flag) { goto loc_b620; }
    if (!neg_flag) { goto loc_b5fc; }
  } else {
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (!carry_flag) { goto loc_b5fc; }
  }
  a = ram[PLAYER_X_FORCE];
  carry_flag = false;
  { uint16_t sum = a + ram[FRICTION_LOW] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[PLAYER_X_FORCE] = a;
  a = ram[PLAYER_X_SPEED];
  a = (uint8_t)(a + ram[FRICTION_HIGH] + carry_flag);
  ram[PLAYER_X_SPEED] = a;
  neg_flag = ((a - ram[MAX_RIGHT_SPEED]) & 0x80) != 0;
  if (neg_flag) { goto loc_b617; }
  a = ram[MAX_RIGHT_SPEED];
  ram[PLAYER_X_SPEED] = a;
  goto loc_b620;
  
loc_b5fc:
  a = ram[PLAYER_X_FORCE];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[FRICTION_LOW] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[PLAYER_X_FORCE] = a;
  a = ram[PLAYER_X_SPEED];
  a = (uint8_t)(a - ram[FRICTION_HIGH] - !carry_flag);
  ram[PLAYER_X_SPEED] = a;
  neg_flag = ((a - ram[MAX_LEFT_SPEED]) & 0x80) != 0;
  if (neg_flag) {
    a = ram[MAX_LEFT_SPEED];
    ram[PLAYER_X_SPEED] = a;
  }
  
loc_b617:
  neg_flag = ((a - 0x0) & 0x80) != 0;
  if (neg_flag) {
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  
loc_b620:
  ram[PLAYER_ABS_SPEED] = a;
  return;
}

void update_fireballs_and_bubbles(void) {
  a = ram[PLAYER_POWER];
  if (a >= 0x2) {
    a = ram[BUTTONS_AB];
    a &= 0x40; zero_flag = a == 0;
    if (!zero_flag) {
      a &= ram[BUTTONS_AB_PREVIOUS]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_b664; }
      a = ram[FIREBALL_COUNT];
      a &= 0x1;
      x = a;
      a = ram[(uint8_t)(FIREBALL_STATE + x)]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_b664; }
      y = ram[PLAYER_Y_PAGE];
      y--; zero_flag = y == 0;
      if (!zero_flag) { goto loc_b664; }
      a = ram[CROUCHING]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_b664; }
      a = ram[PLAYER_STATE];
      zero_flag = a == 0x3;
      if (zero_flag) { goto loc_b664; }
      ram[PULSE1_SFX_REQUEST] = 0x20;
      ram[(uint8_t)(FIREBALL_STATE + x)] = 0x2;
      y = ram[PLAYER_ANIM_PERIOD];
      ram[FIREBALL_THROW_TIMER] = y;
      y--;
      ram[PLAYER_ANIM_TIMER] = y;
      ram[FIREBALL_COUNT]++;
    }
    
loc_b664:
    x = 0x0;
    update_fireball();
    x = 0x1;
    update_fireball();
  }
  a = ram[AREA_KIND]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  x = 0x2;
  do {
    ram[SLOT] = x;
    update_bubble();
    bubble_screen_position();
    bubble_offscreen_bits();
    draw_bubble();
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: x; out: none
void update_fireball(void) {
  ram[SLOT] = x;
  a = ram[(uint8_t)(FIREBALL_STATE + x)];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (!carry_flag) {
    y = ram[(uint8_t)(FIREBALL_STATE + x)]; zero_flag = y == 0;
    if (zero_flag) {
      return;
    }
    y--; zero_flag = y == 0;
    if (!zero_flag) {
      a = ram[PLAYER_X];
      { uint16_t sum = a + 0x4 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[(uint8_t)(FIREBALL_X + x)] = a;
      a = ram[PLAYER_PAGE];
      a = (uint8_t)(a + 0x0 + carry_flag);
      ram[(uint8_t)(FIREBALL_PAGE + x)] = a;
      ram[(uint8_t)(FIREBALL_Y + x)] = ram[PLAYER_Y];
      ram[(uint8_t)(FIREBALL_Y_PAGE + x)] = 0x1;
      y = ram[PLAYER_FACING];
      y--;
      ram[(uint8_t)(FIREBALL_X_SPEED + x)] = ROM(FIREBALL_SPEEDS + y);
      ram[(uint8_t)(FIREBALL_Y_SPEED + x)] = 0x4;
      ram[FIREBALL_BOX_KIND + x] = 0x7;
      ram[(uint8_t)(FIREBALL_STATE + x)]--;
    }
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x7 + carry_flag);
    x = a;
    ram[0x0] = 0x50;
    ram[0x2] = 0x3;
    a = 0x0;
    apply_gravity();
    move_object_x();
    x = ram[SLOT];
    fireball_screen_position();
    fireball_offscreen_bits();
    fireball_box();
    fireball_bg_collision();
    a = ram[FIREBALL_OFFSCREEN];
    a &= 0xcc; zero_flag = a == 0;
    if (zero_flag) {
      fireball_enemy_collision();
      goto loc_ecde;
    }
    ram[(uint8_t)(FIREBALL_STATE + x)] = 0x0;
    return;
  }
  fireball_screen_position();
  goto loc_ed09;
  
loc_ecde:
  y = ram[FIREBALL_SPRITE_SLOT + x];
  ram[SPRITE_Y + y] = ram[FIREBALL_SCREEN_Y];
  ram[SPRITE_X + y] = ram[FIREBALL_SCREEN_X];
  draw_firebar_tile(); // fallthrough
  return;
  
loc_ed09:
  y = ram[BLOCK_SPRITE_SLOT + x];
  a = ram[(uint8_t)(FIREBALL_STATE + x)];
  ram[(uint8_t)(FIREBALL_STATE + x)]++;
  a = a >> 1;
  a &= 0x7;
  carry_flag = a >= 0x3;
  if (!carry_flag) {
    draw_explosion(); // fallthrough
    return;
  }
  ram[(uint8_t)(FIREBALL_STATE + x)] = 0x0;
  return;
}

// in: x; out: none
void update_bubble(void) {
  a = ram[((RANDOM + 1) + x) & 0x7ff];
  a &= 0x1;
  ram[0x7] = a;
  a = ram[(uint8_t)(BUBBLE_Y + x)];
  if (a != 0xf8) {
    move_bubble();
    return;
  }
  a = ram[AIR_BUBBLE_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  spawn_bubble(); // fallthrough
  return;
}

// in: x; out: none
void spawn_bubble(void) {
  y = 0x0;
  a = ram[PLAYER_FACING];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) {
    y = 0x8;
  }
  a = y;
  { uint16_t sum = a + ram[PLAYER_X] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(BUBBLE_X + x)] = a;
  a = ram[PLAYER_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(BUBBLE_PAGE + x)] = a;
  a = ram[PLAYER_Y];
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(uint8_t)(BUBBLE_Y + x)] = a;
  ram[(uint8_t)(BUBBLE_Y_PAGE + x)] = 0x1;
  y = ram[0x7];
  ram[AIR_BUBBLE_TIMER] = ROM(BUBBLE_INTERVALS + y);
  move_bubble(); // fallthrough
  return;
}

// in: x; out: none
void move_bubble(void) {
  y = ram[0x7];
  a = ram[BUBBLE_Y_FRACTION + x];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ROM(BUBBLE_FORCES + y) - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[BUBBLE_Y_FRACTION + x] = a;
  a = ram[(uint8_t)(BUBBLE_Y + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  if (a < 0x20) {
    a = 0xf8;
  }
  ram[(uint8_t)(BUBBLE_Y + x)] = a;
  return;
}

void tick_time_limit(void) {
  a = ram[GAME_MODE]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[PLAYER_TASK];
  if (a < 0x8) {
    return;
  }
  if (a == 0xb) {
    return;
  }
  a = ram[PLAYER_Y_PAGE];
  if (a >= 0x2) {
    return;
  }
  a = ram[TICK_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[TIME_DIGITS];
  a |= ram[(TIME_DIGITS + 1)];
  a |= ram[(TIME_DIGITS + 2)]; zero_flag = a == 0;
  if (!zero_flag) {
    y = ram[TIME_DIGITS];
    y--; zero_flag = y == 0;
    if (zero_flag) {
      a = ram[(TIME_DIGITS + 1)];
      a |= ram[(TIME_DIGITS + 2)]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_b786; }
      ram[EVENT_MUSIC_REQUEST] = 0x40;
    }
    
loc_b786:
    ram[TICK_TIMER] = 0x18;
    y = 0x23;
    ram[(SCORE_ADD + 5)] = 0xff;
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
  a = ram[SCROLL_LOCKED]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[PLAYER_Y];
  a &= ram[PLAYER_Y_PAGE]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[SCROLL_LOCKED] = a;
  ram[WARP_ZONE]++;
  erase_enemy(); return;
}

void update_whirlpools(void) {
  a = ram[AREA_KIND]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[CANNON_TIMER] = a;
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  y = 0x4;
  do {
    a = ram[CANNON_X + y];
    carry_flag = false;
    { uint16_t sum = a + ram[CANNON_Y + y] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[0x2] = a;
    a = ram[CANNON_PAGE + y]; zero_flag = a == 0;
    if (!zero_flag) {
      a = (uint8_t)(a + 0x0 + carry_flag);
      ram[0x1] = a;
      a = ram[PLAYER_X];
      carry_flag = true;
      { uint16_t diff = (uint16_t)(a - ram[CANNON_X + y] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
      a = ram[PLAYER_PAGE];
      a = (uint8_t)(a - ram[CANNON_PAGE + y] - !carry_flag); neg_flag = (a & 0x80) != 0;
      if (neg_flag) { goto loc_b7f1; }
      a = ram[0x2];
      carry_flag = true;
      { uint16_t diff = (uint16_t)(a - ram[PLAYER_X] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
      a = ram[0x1];
      a = (uint8_t)(a - ram[PLAYER_PAGE] - !carry_flag); neg_flag = (a & 0x80) != 0;
      if (!neg_flag) { goto loc_b7f5; }
    }
    
loc_b7f1:
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  return;
  
loc_b7f5:
  a = ram[CANNON_Y + y];
  a = a >> 1;
  ram[0x0] = a;
  a = ram[CANNON_X + y];
  carry_flag = false;
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x1] = a;
  a = ram[CANNON_PAGE + y];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[0x0] = a;
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) {
    a = ram[0x1];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[PLAYER_X] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    a = ram[0x0];
    a = (uint8_t)(a - ram[PLAYER_PAGE] - !carry_flag); neg_flag = (a & 0x80) != 0;
    if (neg_flag) {
      a = ram[PLAYER_X];
      carry_flag = true;
      { uint16_t diff = (uint16_t)(a - 0x1 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
      ram[PLAYER_X] = a;
      a = ram[PLAYER_PAGE];
      a = (uint8_t)(a - 0x0 - !carry_flag);
    } else {
      a = ram[PLAYER_COLLISION];
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (!carry_flag) { goto loc_b83b; }
      a = ram[PLAYER_X];
      carry_flag = false;
      { uint16_t sum = a + 0x1 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[PLAYER_X] = a;
      a = ram[PLAYER_PAGE];
      a = (uint8_t)(a + 0x0 + carry_flag);
    }
    ram[PLAYER_PAGE] = a;
  }
  
loc_b83b:
  ram[0x0] = 0x10;
  a = 0x1;
  ram[CANNON_TIMER] = a;
  ram[0x2] = a;
  a = a >> 1;
  x = a;
  apply_gravity(); return;
}

void update_flagpole(void) {
  x = 0x5;
  ram[SLOT] = x;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x30) {
    return;
  }
  a = ram[PLAYER_TASK];
  if (a == 0x4) {
    a = ram[PLAYER_STATE];
    zero_flag = a == 0x3;
    if (!zero_flag) { goto loc_b896; }
    a = ram[(uint8_t)(ENEMY_Y + x)];
    carry_flag = a >= 0xaa;
    if (carry_flag) { goto loc_b899; }
    a = ram[PLAYER_Y];
    carry_flag = a >= 0xa2;
    if (carry_flag) { goto loc_b899; }
    a = ram[ENEMY_Y_FRACTION + x];
    { uint16_t sum = a + 0xff + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[ENEMY_Y_FRACTION + x] = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    a = ram[FLAG_SCORE_Y_FRACTION];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - 0xff - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[FLAG_SCORE_Y_FRACTION] = a;
    a = ram[FLAG_SCORE_Y];
    a = (uint8_t)(a - 0x1 - !carry_flag);
    ram[FLAG_SCORE_Y] = a;
  }
  
loc_b896:
  goto loc_b8ac;
  
loc_b899:
  y = ram[FLAG_SCORE_KIND];
  a = ROM(FLAGPOLE_SCORE_BOUNDS + y);
  x = ROM(FLAGPOLE_SCORE_DIGITS + y);
  ram[SCORE_ADD + x] = a;
  add_score();
  ram[PLAYER_TASK] = 0x5;
  
loc_b8ac:
  enemy_offscreen_bits();
  enemy_screen_position();
  draw_flagpole_flag();
  return;
}

// in: x; out: x
void update_spring(void) {
  enemy_offscreen_bits();
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[SPRING_ANIM_STEP]; zero_flag = a == 0;
    if (zero_flag) { goto loc_b902; }
    y = a;
    y--;
    a = y;
    a &= 0x2; zero_flag = a == 0;
    if (zero_flag) {
      ram[PLAYER_Y]++;
      ram[PLAYER_Y]++;
    } else {
      ram[PLAYER_Y]--;
      ram[PLAYER_Y]--;
    }
    a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
    carry_flag = false;
    a = (uint8_t)(a + ROM(SPRING_OFFSETS + y) + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    if (y >= 0x1) {
      a = ram[BUTTONS_AB];
      a &= 0x80; zero_flag = a == 0;
      if (zero_flag) { goto loc_b8f4; }
      a &= ram[BUTTONS_AB_PREVIOUS]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_b8f4; }
      ram[SPRING_FORCE] = 0xf4;
    }
    
loc_b8f4:
    zero_flag = y == 0x3;
    if (!zero_flag) { goto loc_b902; }
    ram[PLAYER_Y_SPEED] = ram[SPRING_FORCE];
    ram[SPRING_ANIM_STEP] = 0x0;
  }
  
loc_b902:
  enemy_screen_position();
  draw_enemy();
  check_enemy_offscreen();
  a = ram[SPRING_ANIM_STEP]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[SPRING_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[SPRING_TIMER] = 0x4;
  ram[SPRING_ANIM_STEP]++;
  return;
}

// in: x, y; out: none
void spawn_vine(void) {
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x2f;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0x1;
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[BLOCK_PAGE + y];
  ram[(uint8_t)(ENEMY_X + x)] = ram[BLOCK_X + y];
  a = ram[BLOCK_Y + y];
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  y = ram[VINE_FLAG_SLOT]; zero_flag = y == 0;
  if (zero_flag) {
    ram[VINE_START_Y] = a;
  }
  ram[VINE_SLOTS + y] = x;
  ram[VINE_FLAG_SLOT]++;
  ram[PULSE2_SFX_REQUEST] = 0x4;
  return;
}

// in: x; out: x
void update_vine(void) {
  if (x == 0x5) {
    y = ram[VINE_FLAG_SLOT];
    y--;
    a = ram[VINE_HEIGHT];
    zero_flag = a == ROM(VINE_HEIGHTS + y);
    if (!zero_flag) {
      a = ram[FRAME_COUNT];
      a = a >> 1;
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (!carry_flag) { goto loc_b96a; }
      a = ram[(ENEMY_Y + 5)];
      a = (uint8_t)(a - 0x1 - !carry_flag);
      ram[(ENEMY_Y + 5)] = a;
      ram[VINE_HEIGHT]++;
    }
    
loc_b96a:
    a = ram[VINE_HEIGHT];
    carry_flag = a >= 0x8;
    if (!carry_flag) { goto loc_b9b7; }
    enemy_screen_position();
    enemy_offscreen_bits();
    y = 0x0;
    do {
      draw_vine();
      y++;
      zero_flag = y == ram[VINE_FLAG_SLOT];
    } while (!zero_flag);
    a = ram[ENEMY_OFFSCREEN];
    a &= 0xc; zero_flag = a == 0;
    if (!zero_flag) {
      y--;
      do {
        x = ram[VINE_SLOTS + y];
        erase_enemy();
        y--; neg_flag = (y & 0x80) != 0;
      } while (!neg_flag);
      ram[VINE_FLAG_SLOT] = a;
      ram[VINE_HEIGHT] = a;
    }
    a = ram[VINE_HEIGHT];
    carry_flag = a >= 0x20;
    if (!carry_flag) { goto loc_b9b7; }
    x = 0x6;
    a = 0x1;
    y = 0x1b;
    block_probe();
    y = ram[0x2];
    carry_flag = y >= 0xd0;
    if (carry_flag) { goto loc_b9b7; }
    a = read_byte(indirect_y_addr(0x6)); zero_flag = a == 0;
    if (!zero_flag) { goto loc_b9b7; }
    dynamic_ram_write(indirect_y_addr(0x6), 0x26);
  }
  
loc_b9b7:
  x = ram[SLOT];
  return;
}

void update_cannons(void) {
  a = ram[AREA_KIND]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  x = 0x2;
  do {
    ram[SLOT] = x;
    a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[((RANDOM + 1) + x) & 0x7ff];
      y = ram[HARD_MODE_LATE];
      a &= ROM(CANNON_MASKS + y);
      carry_flag = a >= 0x6;
      if (carry_flag) { goto loc_ba1a; }
      y = a;
      a = ram[CANNON_PAGE + y]; zero_flag = a == 0;
      if (zero_flag) { goto loc_ba1a; }
      a = ram[CANNON_TIMER + y]; zero_flag = a == 0;
      if (!zero_flag) {
        a = (uint8_t)(a - 0x0 - !carry_flag);
        ram[CANNON_TIMER + y] = a;
        goto loc_ba1a;
      }
      a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_ba1a; }
      ram[CANNON_TIMER + y] = 0xe;
      ram[(uint8_t)(ENEMY_PAGE + x)] = ram[CANNON_PAGE + y];
      ram[(uint8_t)(ENEMY_X + x)] = ram[CANNON_X + y];
      a = ram[CANNON_Y + y];
      carry_flag = true;
      a = (uint8_t)(a - 0x8 - !carry_flag);
      ram[(uint8_t)(ENEMY_Y + x)] = a;
      a = 0x1;
      ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
      ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
      a = a >> 1;
      ram[(uint8_t)(ENEMY_STATE + x)] = a;
      ram[ENEMY_BOX_KIND + x] = 0x9;
      ram[(uint8_t)(ENEMY_KIND + x)] = 0x33;
    } else {
      
loc_ba1a:
      a = ram[(uint8_t)(ENEMY_KIND + x)];
      zero_flag = a == 0x33;
      if (!zero_flag) { goto loc_ba2d; }
      check_enemy_offscreen();
      a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
      if (zero_flag) { goto loc_ba2d; }
      enemy_offscreen_bits();
      update_bullet();
    }
    
loc_ba2d:
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: x; out: x
void update_bullet(void) {
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[ENEMY_OFFSCREEN];
      a &= 0xc;
      zero_flag = a == 0xc;
      if (zero_flag) { goto loc_ba85; }
      y = 0x1;
      player_enemy_distance();
      if (!neg_flag) {
        y++;
      }
      ram[(uint8_t)(ENEMY_DIR + x)] = y;
      y--;
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(BULLET_SPEEDS + y);
      a = ram[0x0];
      a = (uint8_t)(a + 0x28 + carry_flag);
      carry_flag = a >= 0x50;
      if (!carry_flag) { goto loc_ba85; }
      ram[(uint8_t)(ENEMY_STATE + x)] = 0x1;
      ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0xa;
      ram[PULSE2_SFX_REQUEST] = 0x8;
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    a &= 0x20; zero_flag = a == 0;
    if (!zero_flag) {
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
  a = ram[(RANDOM + 1)];
  a &= 0x7; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(RANDOM + 1)];
    a &= 0x8;
  }
  y = a;
  a = ram[MISC_STATE + y]; zero_flag = a == 0;
  if (zero_flag) {
    x = ROM(HAMMER_SLOTS + y);
    a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_babf; }
    x = ram[SLOT];
    ram[HAMMER_SLOT + y] = x;
    ram[MISC_STATE + y] = 0x90;
    ram[MISC_BOX_KIND + y] = 0x7;
    carry_flag = true;
    return;
  }
  
loc_babf:
  x = ram[SLOT];
  carry_flag = false;
  return;
}

// in: x; out: x
void update_hammer(void) {
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(MISC_STATE + x)];
    a &= 0x7f;
    y = ram[HAMMER_SLOT + x];
    { uint8_t m = 0x2; carry_flag = a >= m; zero_flag = a == m; }
    if (!zero_flag) {
      if (carry_flag) { goto loc_bb09; }
      a = x;
      carry_flag = false;
      a = (uint8_t)(a + 0xd + carry_flag);
      x = a;
      ram[0x0] = 0x10;
      ram[0x1] = 0xf;
      ram[0x2] = 0x4;
      a = 0x0;
      apply_gravity();
      move_object_x();
      x = ram[SLOT];
    } else {
      ram[(uint8_t)(MISC_Y_SPEED + x)] = 0xfe;
      a = ram[ENEMY_STATE + y];
      a &= 0xf7;
      ram[ENEMY_STATE + y] = a;
      x = ram[(uint8_t)(ENEMY_DIR + y)];
      x--;
      a = ROM(HAMMER_SPEEDS + x);
      x = ram[SLOT];
      ram[(uint8_t)(MISC_X_SPEED + x)] = a;
      
loc_bb09:
      ram[(uint8_t)(MISC_STATE + x)]--;
      a = ram[ENEMY_X + y];
      carry_flag = false;
      { uint16_t sum = a + 0x2 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[(uint8_t)(MISC_X + x)] = a;
      a = ram[ENEMY_PAGE + y];
      a = (uint8_t)(a + 0x0 + carry_flag);
      ram[(uint8_t)(MISC_PAGE + x)] = a;
      a = ram[ENEMY_Y + y];
      carry_flag = true;
      a = (uint8_t)(a - 0xa - !carry_flag);
      ram[(uint8_t)(MISC_Y + x)] = a;
      ram[(uint8_t)(MISC_Y_PAGE + x)] = 0x1;
      goto loc_bb2b;
    }
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
  a = ram[(uint8_t)(BLOCK_X + x)];
  a |= 0x5;
  ram[MISC_X + y] = a;
  a = ram[(uint8_t)(BLOCK_Y + x)];
  a = (uint8_t)(a - 0x10 - !carry_flag);
  ram[MISC_Y + y] = a;
  launch_coin(); return;
}

// in: x; out: x
void spawn_jumping_coin(void) {
  find_free_misc_slot();
  ram[MISC_PAGE + y] = ram[BLOCK_PAGE2 + x];
  a = ram[0x6];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  a |= 0x5;
  ram[MISC_X + y] = a;
  a = ram[0x2];
  a = (uint8_t)(a + 0x20 + carry_flag);
  ram[MISC_Y + y] = a;
  launch_coin(); // fallthrough
  return;
}

// in: x, y; out: x
void launch_coin(void) {
  ram[MISC_Y_SPEED + y] = 0xfb;
  a = 0x1;
  ram[MISC_Y_PAGE + y] = a;
  ram[MISC_STATE + y] = a;
  ram[PULSE2_SFX_REQUEST] = a;
  ram[SLOT] = x;
  give_coin();
  ram[COINS_FOR_1UPS]++;
  return;
}

// in: none; out: y, carry
void find_free_misc_slot(void) {
  y = 0x8;
  do {
    a = ram[MISC_STATE + y]; zero_flag = a == 0;
    if (zero_flag) { goto loc_bb92; }
    y--;
    { uint8_t m = 0x5; carry_flag = y >= m; zero_flag = y == m; }
  } while (!zero_flag);
  y = 0x8;
  
loc_bb92:
  ram[COIN_JUMP_SLOT] = y;
  return;
}

void update_misc_objects(void) {
  x = 0x8;
  do {
    ram[SLOT] = x;
    a = ram[(uint8_t)(MISC_STATE + x)]; zero_flag = a == 0;
    if (!zero_flag) {
      carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
      if (carry_flag) {
        update_hammer();
        goto loc_bbf4;
      }
      y = ram[(uint8_t)(MISC_STATE + x)];
      y--; zero_flag = y == 0;
      if (!zero_flag) {
        ram[(uint8_t)(MISC_STATE + x)]++;
        a = ram[(uint8_t)(MISC_X + x)];
        carry_flag = false;
        { uint16_t sum = a + ram[SCROLL_AMOUNT] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
        ram[(uint8_t)(MISC_X + x)] = a;
        a = ram[(uint8_t)(MISC_PAGE + x)];
        a = (uint8_t)(a + 0x0 + carry_flag);
        ram[(uint8_t)(MISC_PAGE + x)] = a;
        a = ram[(uint8_t)(MISC_STATE + x)];
        zero_flag = a == 0x30;
        if (!zero_flag) { goto loc_bbe8; }
        ram[(uint8_t)(MISC_STATE + x)] = 0x0;
        goto loc_bbf4;
      }
      a = x;
      carry_flag = false;
      a = (uint8_t)(a + 0xd + carry_flag);
      x = a;
      ram[0x0] = 0x50;
      a = 0x6;
      ram[0x2] = a;
      a = a >> 1;
      ram[0x1] = a;
      a = 0x0;
      apply_gravity();
      x = ram[SLOT];
      a = ram[(uint8_t)(MISC_Y_SPEED + x)];
      if (a == 0x5) {
        ram[(uint8_t)(MISC_STATE + x)]++;
      }
      
loc_bbe8:
      misc_screen_position();
      misc_offscreen_bits();
      misc_box();
      draw_coin_or_score();
    }
    
loc_bbf4:
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: none; out: x
void give_coin(void) {
  ram[(SCORE_ADD + 5)] = 0x1;
  x = ram[CURRENT_PLAYER];
  y = ROM(COIN_DIGIT_OFFSETS + x);
  add_digits();
  ram[COINS]++;
  a = ram[COINS];
  if (a == 0x64) {
    ram[COINS] = 0x0;
    ram[LIVES]++;
    ram[PULSE2_SFX_REQUEST] = 0x40;
  }
  ram[(SCORE_ADD + 4)] = 0x2;
  add_score(); // fallthrough
  return;
}

// in: none; out: x
void add_score(void) {
  x = ram[CURRENT_PLAYER];
  y = ROM(SCORE_DIGIT_OFFSETS + x);
  add_digits();
  queue_counters_for_player(); // fallthrough
  return;
}

// in: none; out: x
void queue_counters_for_player(void) {
  y = ram[CURRENT_PLAYER];
  a = ROM(COUNTER_SELECTORS + y);
  queue_counters(); // fallthrough
  return;
}

// in: a; out: x
void queue_counters(void) {
  queue_player_counters();
  y = ram[VRAM_LIST1_LENGTH];
  a = ram[(SPRITE_X + 248) + y]; zero_flag = a == 0;
  if (zero_flag) {
    ram[(SPRITE_X + 248) + y] = 0x24;
  }
  x = ram[SLOT];
  return;
}

void spawn_powerup(void) {
  a = 0x1;
  ram[(ENEMY_STATE + 5)] = a;
  ram[(ENEMY_ACTIVE + 5)] = a;
  ram[(ENEMY_BOX_KIND + 5)] = 0x3;
  a = ram[POWERUP_KIND];
  if (a < 0x2) {
    a = ram[PLAYER_POWER];
    if (a >= 0x2) {
      a = a >> 1;
    }
    ram[POWERUP_KIND] = a;
  }
  ram[(ENEMY_SPRITE_ATTR + 5)] = 0x20;
  ram[PULSE2_SFX_REQUEST] = 0x2;
  return;
}

// in: none; out: x
void update_powerup(void) {
  x = 0x5;
  ram[SLOT] = x;
  a = ram[(ENEMY_STATE + 5)]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (carry_flag) {
    a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_bcd8; }
    a = ram[POWERUP_KIND]; zero_flag = a == 0;
    if (!zero_flag) {
      zero_flag = a == 0x3;
      if (zero_flag) { goto loc_bcaa; }
      zero_flag = a == 0x2;
      if (!zero_flag) { goto loc_bcd8; }
      move_hopping_paratroopa();
      enemy_jump_check();
      goto loc_bcd8;
    }
    
loc_bcaa:
    move_walker();
    enemy_bg_collision();
  } else {
    a = ram[FRAME_COUNT];
    a &= 0x3; zero_flag = a == 0;
    if (zero_flag) {
      ram[(ENEMY_Y + 5)]--;
      a = ram[(ENEMY_STATE + 5)];
      ram[(ENEMY_STATE + 5)]++;
      carry_flag = a >= 0x11;
      if (!carry_flag) { goto loc_bcd2; }
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0x10;
      a = 0x80;
      ram[(ENEMY_STATE + 5)] = a;
      carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
      ram[(ENEMY_SPRITE_ATTR + 5)] = a;
      a = (uint8_t)(a << 1 | carry_flag);
      ram[(uint8_t)(ENEMY_DIR + x)] = a;
    }
    
loc_bcd2:
    a = ram[(ENEMY_STATE + 5)];
    if (a < 0x6) {
      return;
    }
  }
  
loc_bcd8:
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
  ram[0x100 + sp--] = a;
  a = 0x11;
  x = ram[SPRITE_SLOT_CTRL];
  y = ram[PLAYER_SMALL]; zero_flag = y == 0;
  if (zero_flag) {
    a = 0x12;
  }
  ram[(uint8_t)(BLOCK_STATE + x)] = a;
  redraw_block_blank();
  x = ram[SPRITE_SLOT_CTRL];
  a = ram[0x2];
  ram[BLOCK_ORIGIN_Y + x] = a;
  y = a;
  ram[BLOCK_BUFFER_LOW + x] = ram[0x6];
  a = read_byte(indirect_y_addr(0x6));
  find_bumped_metatile();
  ram[0x0] = a;
  y = ram[PLAYER_SMALL]; zero_flag = y == 0;
  if (zero_flag) {
    a = y;
  }
  if (carry_flag) {
    ram[(uint8_t)(BLOCK_STATE + x)] = 0x11;
    a = 0xc4;
    y = ram[0x0];
    if (y != 0x58) {
      zero_flag = y == 0x5d;
      if (!zero_flag) { goto loc_bd41; }
    }
    a = ram[BRICK_COIN_ACTIVE]; zero_flag = a == 0;
    if (zero_flag) {
      ram[BRICK_COIN_TIMER] = 0xb;
      ram[BRICK_COIN_ACTIVE]++;
    }
    a = ram[BRICK_COIN_TIMER]; zero_flag = a == 0;
    if (zero_flag) {
      y = 0xc4;
    }
    a = y;
  }
  
loc_bd41:
  ram[BLOCK_METATILE + x] = a;
  place_block_object();
  y = ram[0x2];
  dynamic_ram_write(indirect_y_addr(0x6), 0x23);
  ram[BLOCK_BOUNCE_TIMER] = 0x10;
  a = ram[0x100 + ++sp];
  ram[0x5] = a;
  y = 0x0;
  a = ram[CROUCHING]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[PLAYER_SMALL]; zero_flag = a == 0;
    if (zero_flag) { goto loc_bd62; }
  }
  y++;
  
loc_bd62:
  a = ram[PLAYER_Y];
  carry_flag = false;
  a = (uint8_t)(a + ROM(BLOCK_Y_OFFSETS + y) + carry_flag);
  a &= 0xf0;
  ram[(uint8_t)(BLOCK_Y + x)] = a;
  y = ram[(uint8_t)(BLOCK_STATE + x)];
  if (y != 0x11) {
    shatter_brick();
  } else {
    bump_block();
  }
  a = ram[SPRITE_SLOT_CTRL];
  a ^= 0x1;
  ram[SPRITE_SLOT_CTRL] = a;
  return;
}

// in: x; out: none
void place_block_object(void) {
  a = ram[PLAYER_X];
  carry_flag = false;
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  a &= 0xf0;
  ram[(uint8_t)(BLOCK_X + x)] = a;
  a = ram[PLAYER_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(BLOCK_PAGE + x)] = a;
  ram[BLOCK_PAGE2 + x] = a;
  ram[(uint8_t)(BLOCK_Y_PAGE + x)] = ram[PLAYER_Y_PAGE];
  return;
}

void bump_block(void) {
  collect_coin_above();
  ram[PULSE1_SFX_REQUEST] = 0x2;
  a = 0x0;
  ram[(uint8_t)(BLOCK_X_SPEED + x)] = a;
  ram[BLOCK_Y_FORCE + x] = a;
  ram[PLAYER_Y_SPEED] = a;
  ram[(uint8_t)(BLOCK_Y_SPEED + x)] = 0xfe;
  a = ram[0x5];
  find_bumped_metatile();
  if (!carry_flag) {
    return;
  }
  a = y;
  carry_flag = a >= 0x9;
  if (carry_flag) {
    a = (uint8_t)(a - 0x5 - !carry_flag);
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
  a = 0x0;
  
  block_gives_kind_star(); return;
}

// in: x; out: none
void block_gives_star(void) {
  a = 0x2;
  block_gives_kind_star(); // fallthrough
  return;
}

// in: a, x; out: none
void block_gives_kind_star(void) {
  
  block_gives_kind(); return;
}

// in: x; out: none
void block_gives_1up(void) {
  a = 0x3;
  block_gives_kind(); // fallthrough
  return;
}

// in: a, x; out: none
void block_gives_kind(void) {
  goto block_gives_kind;
  
loc_bc49:
  ram[(ENEMY_KIND + 5)] = 0x2e;
  ram[(ENEMY_PAGE + 5)] = ram[(uint8_t)(BLOCK_PAGE + x)];
  ram[(ENEMY_X + 5)] = ram[(uint8_t)(BLOCK_X + x)];
  ram[(ENEMY_Y_PAGE + 5)] = 0x1;
  a = ram[(uint8_t)(BLOCK_Y + x)];
  carry_flag = true;
  a = (uint8_t)(a - 0x8 - !carry_flag);
  ram[(ENEMY_Y + 5)] = a;
  spawn_powerup(); // fallthrough
  return;
  
block_gives_kind:
  ram[POWERUP_KIND] = a;
  goto loc_bc49;
}

void block_gives_vine(void) {
  x = 0x5;
  y = ram[SPRITE_SLOT_CTRL];
  spawn_vine();
  return;
}

// in: a; out: y, carry
void find_bumped_metatile(void) {
  y = 0xd;
  do {
    { uint8_t m = ROM(BUMPABLE_METATILES + y); carry_flag = a >= m; zero_flag = a == m; }
    if (zero_flag) {
      return;
    }
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  carry_flag = false;
  return;
}

void shatter_brick(void) {
  collect_coin_above();
  a = 0x1;
  ram[BLOCK_REPLACE + x] = a;
  ram[NOISE_SFX_REQUEST] = a;
  spawn_brick_chunks();
  ram[PLAYER_Y_SPEED] = 0xfe;
  ram[(SCORE_ADD + 5)] = 0x5;
  add_score();
  x = ram[SPRITE_SLOT_CTRL];
  return;
}

// in: none; out: x
void collect_coin_above(void) {
  x = ram[SPRITE_SLOT_CTRL];
  y = ram[0x2]; zero_flag = y == 0;
  if (zero_flag) {
    return;
  }
  a = y;
  carry_flag = true;
  a = (uint8_t)(a - 0x10 - !carry_flag);
  ram[0x2] = a;
  y = a;
  a = read_byte(indirect_y_addr(0x6));
  if (a != 0xc2) {
    return;
  }
  dynamic_ram_write(indirect_y_addr(0x6), 0x0);
  erase_coin_or_axe();
  x = ram[SPRITE_SLOT_CTRL];
  spawn_jumping_coin();
  return;
}

// in: x; out: none
void spawn_brick_chunks(void) {
  ram[BLOCK_ORIGIN_X + x] = ram[(uint8_t)(BLOCK_X + x)];
  a = 0xf0;
  ram[(uint8_t)(BLOCK_X_SPEED + x)] = a;
  ram[(uint8_t)((BLOCK_X_SPEED + 2) + x)] = a;
  ram[(uint8_t)(BLOCK_Y_SPEED + x)] = 0xfa;
  ram[(uint8_t)((BLOCK_Y_SPEED + 2) + x)] = 0xfc;
  a = 0x0;
  ram[BLOCK_Y_FORCE + x] = a;
  ram[(BLOCK_Y_FORCE + 2) + x] = a;
  ram[(uint8_t)((BLOCK_PAGE + 2) + x)] = ram[(uint8_t)(BLOCK_PAGE + x)];
  ram[(uint8_t)((BLOCK_X + 2) + x)] = ram[(uint8_t)(BLOCK_X + x)];
  a = ram[(uint8_t)(BLOCK_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(uint8_t)((BLOCK_Y + 2) + x)] = a;
  ram[(uint8_t)(BLOCK_Y_SPEED + x)] = 0xfa;
  return;
}

// in: x; out: x
void update_block_objects(void) {
  a = ram[(uint8_t)(BLOCK_STATE + x)]; zero_flag = a == 0;
  if (!zero_flag) {
    a &= 0xf;
    ram[0x100 + sp--] = a;
    y = a;
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x9 + carry_flag);
    x = a;
    y--; zero_flag = y == 0;
    if (!zero_flag) {
      apply_gravity_block();
      move_object_x();
      a = x;
      carry_flag = false;
      a = (uint8_t)(a + 0x2 + carry_flag);
      x = a;
      apply_gravity_block();
      move_object_x();
      x = ram[SLOT];
      block_screen_position();
      block_offscreen_bits();
      draw_brick_chunks();
      a = ram[0x100 + ++sp];
      y = ram[(uint8_t)(BLOCK_Y_PAGE + x)]; zero_flag = y == 0;
      if (zero_flag) { goto loc_bed1; }
      ram[0x100 + sp--] = a;
      a = 0xf0;
      carry_flag = a >= ram[(uint8_t)((BLOCK_Y + 2) + x)];
      if (!carry_flag) {
        ram[(uint8_t)((BLOCK_Y + 2) + x)] = a;
      }
      a = ram[(uint8_t)(BLOCK_Y + x)];
      carry_flag = a >= 0xf0;
      a = ram[0x100 + ++sp];
      if (!carry_flag) { goto loc_bed1; }
    } else {
      apply_gravity_block();
      x = ram[SLOT];
      block_screen_position();
      block_offscreen_bits();
      draw_block();
      a = ram[(uint8_t)(BLOCK_Y + x)];
      a &= 0xf;
      carry_flag = a >= 0x5;
      a = ram[0x100 + ++sp];
      if (carry_flag) { goto loc_bed1; }
      ram[BLOCK_REPLACE + x] = 0x1;
    }
    a = 0x0;
  }
  
loc_bed1:
  ram[(uint8_t)(BLOCK_STATE + x)] = a;
  return;
}

void update_block_metatiles(void) {
  x = 0x1;
  do {
    ram[SLOT] = x;
    a = ram[VRAM_LIST1]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[BLOCK_REPLACE + x]; zero_flag = a == 0;
      if (zero_flag) { goto loc_befe; }
      ram[0x6] = ram[BLOCK_BUFFER_LOW + x];
      ram[0x7] = 0x5;
      a = ram[BLOCK_ORIGIN_Y + x];
      ram[0x2] = a;
      y = a;
      a = ram[BLOCK_METATILE + x];
      dynamic_ram_write(indirect_y_addr(0x6), a);
      redraw_block();
      ram[BLOCK_REPLACE + x] = 0x0;
    }
    
loc_befe:
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
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
  a = ram[SPRING_ANIM_STEP]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  x = a;
  move_object_x(); // fallthrough
  return;
}

// add speed (4.4 fixed point) to position and page
// in: x; out: a, y
void move_object_x(void) {
  a = ram[(uint8_t)(OBJ_X_SPEED + x)];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  ram[0x1] = a;
  a = ram[(uint8_t)(OBJ_X_SPEED + x)];
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  if (a >= 0x8) {
    a |= 0xf0;
  }
  ram[0x0] = a;
  y = 0x0;
  neg_flag = ((a - 0x0) & 0x80) != 0;
  if (neg_flag) {
    y--;
  }
  ram[0x2] = y;
  a = ram[OBJ_X_FORCE + x];
  carry_flag = false;
  { uint16_t sum = a + ram[0x1] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[OBJ_X_FORCE + x] = a;
  a = 0x0;
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  ram[0x100 + sp--] = a;
  { bool carry = (a & 1) != 0; a = (uint8_t)(a >> 1 | carry_flag << 7); carry_flag = carry; }
  a = ram[(uint8_t)(OBJ_X + x)];
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(OBJ_X + x)] = a;
  a = ram[(uint8_t)(OBJ_PAGE + x)];
  a = (uint8_t)(a + ram[0x2] + carry_flag);
  ram[(uint8_t)(OBJ_PAGE + x)] = a;
  a = ram[0x100 + ++sp];
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  return;
}

// in: x; out: x
void move_enemy_down_fast(void) {
  y = 0x3d;
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  if (a != 0x5) {
    move_vertically();
    return;
  }
  move_falling_platform(); // fallthrough
  return;
}

// in: x; out: x
void move_falling_platform(void) {
  y = 0x20;
  move_vertically(); // fallthrough
  return;
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
  set_max_speed_mid(); // fallthrough
  return;
}

// in: x, y; out: x
void set_max_speed_mid(void) {
  a = 0x2;
  apply_gravity_enemy(); return;
}

// in: x; out: x
void move_enemy_jumping(void) {
  y = 0x1c;
  set_max_speed_high(); // fallthrough
  return;
}

// in: x, y; out: x
void set_max_speed_high(void) {
  a = 0x3;
  apply_gravity_enemy(); // fallthrough
  return;
}

// in: a, x, y; out: x
void apply_gravity_enemy(void) {
  ram[0x0] = y;
  x++;
  apply_gravity_object();
  x = ram[SLOT];
  return;
}

// in: x; out: none
void apply_gravity_block(void) {
  y = 0x1;
  ram[0x0] = 0x50;
  a = ROM(BLOCK_MAX_SPEEDS + y);
  apply_gravity_object(); // fallthrough
  return;
}

// in: a, x; out: none
void apply_gravity_object(void) {
  ram[0x2] = a;
  a = 0x0;
  apply_gravity(); return;
}

// in: x; out: x
void move_platform_down(void) {
  a = 0x0;
  
  move_platform(); return;
}

// in: x; out: x
void move_platform_up(void) {
  a = 0x1;
  move_platform(); // fallthrough
  return;
}

// in: a, x; out: x
void move_platform(void) {
  ram[0x100 + sp--] = a;
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  x++;
  a = 0x5;
  if (y == 0x29) {
    a = 0x9;
  }
  ram[0x0] = a;
  ram[0x1] = 0xa;
  ram[0x2] = 0x3;
  a = ram[0x100 + ++sp];
  y = a;
  move_paratroopa_vertically(); // fallthrough
  return;
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
  ram[0x100 + sp--] = a;
  a = ram[OBJ_Y_FRACTION + x];
  carry_flag = false;
  { uint16_t sum = a + ram[OBJ_Y_FORCE + x] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[OBJ_Y_FRACTION + x] = a;
  y = 0x0;
  a = ram[(uint8_t)(OBJ_Y_SPEED + x)]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    y--;
  }
  ram[0x7] = y;
  { uint16_t sum = a + ram[(uint8_t)(OBJ_Y + x)] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(OBJ_Y + x)] = a;
  a = ram[(uint8_t)(OBJ_Y_PAGE + x)];
  a = (uint8_t)(a + ram[0x7] + carry_flag);
  ram[(uint8_t)(OBJ_Y_PAGE + x)] = a;
  a = ram[OBJ_Y_FORCE + x];
  carry_flag = false;
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[OBJ_Y_FORCE + x] = a;
  a = ram[(uint8_t)(OBJ_Y_SPEED + x)];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(OBJ_Y_SPEED + x)] = a;
  neg_flag = ((a - ram[0x2]) & 0x80) != 0;
  if (!neg_flag) {
    a = ram[OBJ_Y_FORCE + x];
    carry_flag = a >= 0x80;
    if (!carry_flag) { goto loc_c018; }
    ram[(uint8_t)(OBJ_Y_SPEED + x)] = ram[0x2];
    ram[OBJ_Y_FORCE + x] = 0x0;
  }
  
loc_c018:
  a = ram[0x100 + ++sp]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[0x2];
  a ^= 0xff;
  y = a;
  y++;
  ram[0x7] = y;
  a = ram[OBJ_Y_FORCE + x];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[0x1] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[OBJ_Y_FORCE + x] = a;
  a = ram[(uint8_t)(OBJ_Y_SPEED + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  ram[(uint8_t)(OBJ_Y_SPEED + x)] = a;
  neg_flag = ((a - ram[0x7]) & 0x80) != 0;
  if (!neg_flag) {
    return;
  }
  a = ram[OBJ_Y_FORCE + x];
  if (a >= 0x80) {
    return;
  }
  ram[(uint8_t)(OBJ_Y_SPEED + x)] = ram[0x7];
  ram[OBJ_Y_FORCE + x] = 0xff;
  return;
}

// in: x; out: x
void update_enemies_and_loops(void) {
  a = ram[(uint8_t)(ENEMY_ACTIVE + x)];
  ram[0x100 + sp--] = a;
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (!carry_flag) {
    a = ram[0x100 + ++sp]; zero_flag = a == 0;
    if (!zero_flag) {
      goto loc_c882;
    }
    a = ram[PARSER_TASK];
    a &= 0x7;
    if (a == 0x7) {
      return;
    }
  } else {
    a = ram[0x100 + ++sp];
    a &= 0xf;
    y = a;
    a = ram[ENEMY_ACTIVE + y]; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
    return;
  }
  
loc_c0cc:
  a = ram[LOOP_COMMAND]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[PARSER_COLUMN]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_c12f; }
    y = 0xb;
    do {
      
loc_c0d8:
      y--; neg_flag = (y & 0x80) != 0;
      if (neg_flag) { goto loc_c12f; }
      a = ram[WORLD_NUMBER];
      zero_flag = a == ROM(LOOP_WORLDS + y);
      if (!zero_flag) { goto loc_c0d8; }
      a = ram[PARSER_PAGE];
      zero_flag = a == ROM(LOOP_PAGES + y);
    } while (!zero_flag);
    a = ram[PLAYER_Y];
    zero_flag = a == ROM(LOOP_Y_POSITIONS + y);
    if (!zero_flag) { goto loc_c115; }
    a = ram[PLAYER_STATE];
    zero_flag = a == 0x0;
    if (!zero_flag) { goto loc_c115; }
    a = ram[WORLD_NUMBER];
    zero_flag = a == 0x6;
    if (!zero_flag) { goto loc_c122; }
    ram[LOOP_CHECK_COUNT]++;
    
loc_c102:
    ram[LOOP_PASS_COUNT]++;
    a = ram[LOOP_PASS_COUNT];
    zero_flag = a == 0x3;
    if (!zero_flag) { goto loc_c12a; }
    a = ram[LOOP_CHECK_COUNT];
    zero_flag = a == 0x3;
    if (zero_flag) { goto loc_c122; }
    goto loc_c11c;
    
loc_c115:
    a = ram[WORLD_NUMBER];
    zero_flag = a == 0x6;
    if (zero_flag) { goto loc_c102; }
    
loc_c11c:
    load_enemies_and_loops();
    kill_all_enemies();
    
loc_c122:
    a = 0x0;
    ram[LOOP_PASS_COUNT] = a;
    ram[LOOP_CHECK_COUNT] = a;
    
loc_c12a:
    ram[LOOP_COMMAND] = 0x0;
  }
  
loc_c12f:
  a = ram[FRENZY_QUEUE]; zero_flag = a == 0;
  if (!zero_flag) {
    ram[(uint8_t)(ENEMY_KIND + x)] = a;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0x1;
    a = 0x0;
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    ram[FRENZY_QUEUE] = a;
    spawn_enemy(); return;
  }
  y = ram[ENEMY_DATA_POS];
  a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  if (a == 0xff) {
  } else {
    a &= 0xf;
    if (a != 0xe) {
      carry_flag = x >= 0x5;
      if (!carry_flag) { goto loc_c164; }
      y++;
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      a &= 0x3f;
      zero_flag = a == 0x2e;
      if (zero_flag) { goto loc_c164; }
      return;
    }
    
loc_c164:
    a = ram[SCREEN_RIGHT_X];
    carry_flag = false;
    { uint16_t sum = a + 0x30 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    a &= 0xf0;
    ram[0x7] = a;
    a = ram[SCREEN_RIGHT_PAGE];
    a = (uint8_t)(a + 0x0 + carry_flag);
    ram[0x6] = a;
    y = ram[ENEMY_DATA_POS];
    y++;
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) {
      a = ram[ENEMY_OBJ_PAGE_SET]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_c189; }
      ram[ENEMY_OBJ_PAGE_SET]++;
      ram[ENEMY_OBJ_PAGE]++;
    }
    
loc_c189:
    y--;
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a &= 0xf;
    if (a == 0xf) {
      a = ram[ENEMY_OBJ_PAGE_SET]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_c1ab; }
      y++;
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      a &= 0x3f;
      ram[ENEMY_OBJ_PAGE] = a;
      ram[ENEMY_DATA_POS]++;
      ram[ENEMY_DATA_POS]++;
      ram[ENEMY_OBJ_PAGE_SET]++;
      goto loc_c0cc;
    }
    
loc_c1ab:
    ram[(uint8_t)(ENEMY_PAGE + x)] = ram[ENEMY_OBJ_PAGE];
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a &= 0xf0;
    ram[(uint8_t)(ENEMY_X + x)] = a;
    carry_flag = a >= ram[SCREEN_RIGHT_X];
    a = ram[(uint8_t)(ENEMY_PAGE + x)];
    { uint16_t diff = (uint16_t)(a - ram[SCREEN_RIGHT_PAGE] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    if (!carry_flag) {
      a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
      a &= 0xf;
      zero_flag = a == 0xe;
      if (zero_flag) { goto loc_c231; }
      goto loc_c250;
    }
    a = ram[0x7];
    carry_flag = a >= ram[(uint8_t)(ENEMY_X + x)];
    a = ram[0x6];
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(ENEMY_PAGE + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    if (!carry_flag) { goto loc_c216; }
    ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 0x1;
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    zero_flag = a == 0xe0;
    if (zero_flag) { goto loc_c231; }
    y++;
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a &= 0x40; zero_flag = a == 0;
    if (!zero_flag) {
      a = ram[HARD_MODE_LATE]; zero_flag = a == 0;
      if (zero_flag) { goto loc_c25e; }
    }
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a &= 0x3f;
    if (a >= 0x37) {
      carry_flag = a >= 0x3f;
      if (!carry_flag) { goto loc_c22e; }
    }
    if (a == 0x6) {
      y = ram[HARD_MODE]; zero_flag = y == 0;
      if (zero_flag) { goto loc_c208; }
      a = 0x2;
    }
    
loc_c208:
    ram[(uint8_t)(ENEMY_KIND + x)] = a;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0x1;
    spawn_enemy();
    a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_c25e; }
    return;
  }
  
loc_c216:
  a = ram[FRENZY_ENEMY]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[VINE_FLAG_SLOT];
    if (a != 0x1) {
      return;
    }
    a = 0x2f;
  }
  ram[(uint8_t)(ENEMY_KIND + x)] = a;
  spawn_enemy(); // fallthrough
  return;
  
loc_c22e:
  goto loc_c71b;
  
loc_c231:
  y++;
  y++;
  a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  zero_flag = a == ram[WORLD_NUMBER];
  if (zero_flag) {
    y--;
    ram[AREA_POINTER] = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    y++;
    a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
    a &= 0x1f;
    ram[ENTRANCE_PAGE] = a;
  }
  goto loc_c25b;
  
loc_c250:
  y = ram[ENEMY_DATA_POS];
  a = read_byte(indirect_y_addr(ENEMY_DATA_PTR));
  a &= 0xf;
  zero_flag = a == 0xe;
  if (!zero_flag) { goto loc_c25e; }
  
loc_c25b:
  ram[ENEMY_DATA_POS]++;
  
loc_c25e:
  ram[ENEMY_DATA_POS]++;
  ram[ENEMY_DATA_POS]++;
  ram[ENEMY_OBJ_PAGE_SET] = 0x0;
  x = ram[SLOT];
  return;
  
loc_c71b:
  y = 0x0;
  carry_flag = true;
  a = (uint8_t)(a - 0x37 - !carry_flag);
  ram[0x100 + sp--] = a;
  if (a < 0x4) {
    ram[0x100 + sp--] = a;
    y = 0x6;
    a = ram[HARD_MODE]; zero_flag = a == 0;
    if (!zero_flag) {
      y = 0x2;
    }
    a = ram[0x100 + ++sp];
  }
  ram[0x1] = y;
  y = 0xb0;
  a &= 0x2; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x70;
  }
  ram[0x0] = y;
  ram[0x2] = ram[SCREEN_RIGHT_PAGE];
  ram[0x3] = ram[SCREEN_RIGHT_X];
  y = 0x2;
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) {
    y++;
  }
  ram[GROUP_ENEMY_COUNT] = y;
  do {
    x = 0xff;
    do {
      x++;
      carry_flag = x >= 0x5;
      if (carry_flag) { goto loc_c784; }
      a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
    } while (!zero_flag);
    ram[(uint8_t)(ENEMY_KIND + x)] = ram[0x1];
    ram[(uint8_t)(ENEMY_PAGE + x)] = ram[0x2];
    a = ram[0x3];
    ram[(uint8_t)(ENEMY_X + x)] = a;
    carry_flag = false;
    { uint16_t sum = a + 0x18 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[0x3] = a;
    a = ram[0x2];
    a = (uint8_t)(a + 0x0 + carry_flag);
    ram[0x2] = a;
    ram[(uint8_t)(ENEMY_Y + x)] = ram[0x0];
    a = 0x1;
    ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
    ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
    init_enemy();
    ram[GROUP_ENEMY_COUNT]--; zero_flag = ram[GROUP_ENEMY_COUNT] == 0;
  } while (!zero_flag);
  
loc_c784:
  goto loc_c25e;
  
loc_c882:
  x = ram[SLOT];
  a = 0x0;
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = y >= 0x15;
  if (carry_flag) {
    a = y;
    a = (uint8_t)(a - 0x14 - !carry_flag);
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
  a = ram[PLAYER_PAGE];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[PLAYER_PAGE] = a;
  a = ram[PARSER_PAGE];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[PARSER_PAGE] = a;
  a = ram[SCREEN_LEFT_PAGE];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[SCREEN_LEFT_PAGE] = a;
  a = ram[SCREEN_RIGHT_PAGE];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[SCREEN_RIGHT_PAGE] = a;
  a = ram[AREA_OBJ_PAGE];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[AREA_OBJ_PAGE] = a;
  a = 0x0;
  ram[ENEMY_OBJ_PAGE_SET] = a;
  ram[AREA_OBJ_PAGE_SET] = a;
  ram[ENEMY_DATA_POS] = a;
  ram[ENEMY_OBJ_PAGE] = a;
  ram[AREA_DATA_POS] = ROM(LOOP_AREA_OFFSETS + y);
  return;
}

// in: x; out: x
void spawn_enemy(void) {
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x0;
  init_enemy();
  return;
}

// in: x; out: x
void init_enemy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = a >= 0x15;
  if (!carry_flag) {
    y = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    ram[ENEMY_OFFSCREEN_MASKED + x] = 0x1;
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
  a = 0x2;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  a = a >> 1;
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = a;
  a = a >> 1;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  set_small_box(); return;
}

// in: x; out: none
void init_retainer(void) {
  ram[(uint8_t)(ENEMY_Y + x)] = 0xb8;
  return;
}

// in: x; out: y
void init_walker(void) {
  y = 0x1;
  a = ram[HARD_MODE]; zero_flag = a == 0;
  if (zero_flag) {
    y--;
  }
  a = ROM(WALK_SPEEDS + y);
  set_enemy_speed(); // fallthrough
  return;
}

// in: a, x; out: none
void set_enemy_speed(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  set_tall_box(); return;
}

// in: x; out: none
void init_red_koopa(void) {
  init_walker();
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x1;
  return;
}

// in: x; out: none
void init_hammer_bro(void) {
  a = 0x0;
  ram[PLATFORM_CONTACT + x] = a;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  y = ram[HARD_MODE_LATE];
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = ROM(HAMMER_BRO_WALK_TIMES + y);
  a = 0xb;
  set_box_facing_left(); return;
}

// in: x; out: none
void init_flyer_or_swimmer(void) {
  a = 0x0;
  set_enemy_speed(); return;
}

// in: x; out: none
void init_blooper(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0x0;
  set_small_box(); // fallthrough
  return;
}

// in: x; out: a
void set_small_box(void) {
  a = 0x9;
  set_box_facing_left(); return;
}

// in: x; out: none
void init_red_paratroopa(void) {
  y = 0x30;
  a = ram[(uint8_t)(ENEMY_Y + x)]; neg_flag = (a & 0x80) != 0;
  ram[ENEMY_X_FORCE + x] = a;
  if (neg_flag) {
    y = 0xe0;
  }
  a = y;
  a = (uint8_t)(a + ram[(uint8_t)(ENEMY_Y + x)] + carry_flag);
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  set_tall_box(); // fallthrough
  return;
}

// in: x; out: none
void set_tall_box(void) {
  a = 0x3;
  set_box_facing_left(); // fallthrough
  return;
}

// in: a, x; out: a
void set_box_facing_left(void) {
  ram[ENEMY_BOX_KIND + x] = a;
  ram[(uint8_t)(ENEMY_DIR + x)] = 0x2;
  clear_vertical_motion(); // fallthrough
  return;
}

// in: x; out: a
void clear_vertical_motion(void) {
  a = 0x0;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  ram[ENEMY_Y_FORCE + x] = a;
  return;
}

// in: x; out: none
void init_bullet(void) {
  ram[(uint8_t)(ENEMY_DIR + x)] = 0x2;
  ram[ENEMY_BOX_KIND + x] = 0x9;
  return;
}

// in: x; out: none
void init_cheep_cheep(void) {
  set_small_box();
  a = ram[(RANDOM + x) & 0x7ff];
  a &= 0x10;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  ram[ENEMY_Y_FORCE + x] = ram[(uint8_t)(ENEMY_Y + x)];
  return;
}

// in: x; out: none
void init_lakitu(void) {
  a = ram[FRENZY_ENEMY]; zero_flag = a == 0;
  if (zero_flag) {
    setup_lakitu(); // fallthrough
    return;
  }
  erase_enemy(); return;
}

// in: x; out: none
void setup_lakitu(void) {
  ram[LAKITU_RETURN_TIMER] = 0x0;
  init_flyer_or_swimmer();
  set_tall_box_b(); return;
}

// in: x; out: x
void update_lakitu_spawner(void) {
  a = ram[FRENZY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  if (x >= 0x5) {
    return;
  }
  ram[FRENZY_TIMER] = 0x80;
  y = 0x4;
  do {
    a = ram[ENEMY_KIND + y];
    zero_flag = a == 0x11;
    if (zero_flag) { goto loc_c3e6; }
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  ram[LAKITU_RETURN_TIMER]++;
  a = ram[LAKITU_RETURN_TIMER];
  if (a < 0x7) {
    return;
  }
  x = 0x4;
  do {
    a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
    if (zero_flag) { goto loc_c3d3; }
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  goto loc_c3e3;
  
loc_c3d3:
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x0;
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x11;
  setup_lakitu();
  a = 0x20;
  place_flame();
  
loc_c3e3:
  x = ram[SLOT];
  return;
  
loc_c3e6:
  a = ram[PLAYER_Y];
  if (a < 0x2c) {
    return;
  }
  a = ram[ENEMY_STATE + y]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[ENEMY_PAGE + y];
  ram[(uint8_t)(ENEMY_X + x)] = ram[ENEMY_X + y];
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = 0x1;
  a = ram[ENEMY_Y + y];
  carry_flag = true;
  a = (uint8_t)(a - 0x8 - !carry_flag);
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  a = ram[(RANDOM + x) & 0x7ff];
  a &= 0x3;
  y = a;
  x = 0x2;
  do {
    ram[(uint8_t)(0x1 + x)] = ROM(LAKITU_AIM_OFFSETS + y);
    y++;
    y++;
    y++;
    y++;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  x = ram[SLOT];
  lakitu_chase();
  y = ram[PLAYER_X_SPEED];
  if (y < 0x8) {
    y = a;
    a = ram[((RANDOM + 1) + x) & 0x7ff];
    a &= 0x3; zero_flag = a == 0;
    if (!zero_flag) {
      a = y;
      a ^= 0xff;
      y = a;
      y++;
    }
    a = y;
  }
  set_small_box();
  y = 0x2;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  neg_flag = ((a - 0x0) & 0x80) != 0;
  if (!neg_flag) {
    y--;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfd;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0x1;
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x5;
  return;
}

// in: x; out: none
void init_long_firebar(void) {
  copy_enemy();
  init_firebar(); // fallthrough
  return;
}

// in: x; out: none
void init_firebar(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0x0;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = true;
  a = (uint8_t)(a - 0x1b - !carry_flag);
  y = a;
  ram[FIREBAR_SPEED + x] = ROM(FIREBAR_SPEEDS + y);
  ram[(uint8_t)(TARGET_PAGE + x)] = ROM(FIREBAR_DIRECTIONS + y);
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + 0x4 + carry_flag);
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  a = ram[(uint8_t)(ENEMY_X + x)];
  carry_flag = false;
  { uint16_t sum = a + 0x4 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(ENEMY_X + x)] = a;
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  set_tall_box_b(); return;
}

// in: x; out: none
void init_flying_fish(void) {
  a = ram[FRENZY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  set_small_box();
  a = ram[((RANDOM + 1) + x) & 0x7ff];
  a &= 0x3;
  y = a;
  ram[FRENZY_TIMER] = ROM(FLYING_FISH_INTERVALS + y);
  y = 0x3;
  a = ram[HARD_MODE_LATE]; zero_flag = a == 0;
  if (!zero_flag) {
    y++;
  }
  ram[0x0] = y;
  carry_flag = x >= ram[0x0];
  if (carry_flag) {
    return;
  }
  a = ram[(RANDOM + x) & 0x7ff];
  a &= 0x3;
  ram[0x0] = a;
  ram[0x1] = a;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfb;
  a = 0x0;
  y = ram[PLAYER_X_SPEED]; zero_flag = y == 0;
  if (!zero_flag) {
    a = 0x4;
    carry_flag = y >= 0x19;
    if (!carry_flag) { goto loc_c4e4; }
    a = (uint8_t)(a << 1);
  }
  
loc_c4e4:
  ram[0x100 + sp--] = a;
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  ram[0x0] = a;
  a = ram[((RANDOM + 1) + x) & 0x7ff];
  a &= 0x3; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[((RANDOM + 2) + x) & 0x7ff];
    a &= 0xf;
    ram[0x0] = a;
  }
  a = ram[0x100 + ++sp];
  carry_flag = false;
  a = (uint8_t)(a + ram[0x1] + carry_flag);
  y = a;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(FLYING_FISH_SPEEDS + y);
  ram[(uint8_t)(ENEMY_DIR + x)] = 0x1;
  a = ram[PLAYER_X_SPEED]; zero_flag = a == 0;
  if (zero_flag) {
    y = ram[0x0];
    a = y;
    a &= 0x2; zero_flag = a == 0;
    if (zero_flag) { goto loc_c51c; }
    a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    ram[(uint8_t)(ENEMY_DIR + x)]++;
  }
  
loc_c51c:
  a = y;
  a &= 0x2; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[PLAYER_X];
    carry_flag = false;
    { uint16_t sum = a + ROM(FLYING_FISH_X_OFFSETS + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[(uint8_t)(ENEMY_X + x)] = a;
    a = ram[PLAYER_PAGE];
    a = (uint8_t)(a + 0x0 + carry_flag);
  } else {
    a = ram[PLAYER_X];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ROM(FLYING_FISH_X_OFFSETS + y) - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[(uint8_t)(ENEMY_X + x)] = a;
    a = ram[PLAYER_PAGE];
    a = (uint8_t)(a - 0x0 - !carry_flag);
  }
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  a = 0x1;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
  ram[(uint8_t)(ENEMY_Y + x)] = 0xf8;
  return;
}

// in: x; out: none
void init_bowser(void) {
  copy_enemy();
  ram[BOWSER_SLOT] = x;
  a = 0x0;
  ram[BOWSER_BODY] = a;
  ram[BRIDGE_COLLAPSE_POS] = a;
  ram[BOWSER_HOME_X] = ram[(uint8_t)(ENEMY_X + x)];
  a = 0xdf;
  ram[BOWSER_BREATH_TIMER] = a;
  ram[(uint8_t)(ENEMY_DIR + x)] = a;
  a = 0x20;
  ram[BOWSER_FEET] = a;
  ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = a;
  a = 0x5;
  ram[BOWSER_HEALTH] = a;
  a = a >> 1;
  ram[BOWSER_SPEED] = a;
  return;
}

// in: x; out: none
void copy_enemy(void) {
  y = 0xff;
  do {
    y++;
    a = ram[ENEMY_ACTIVE + y]; zero_flag = a == 0;
  } while (!zero_flag);
  ram[COPY_SLOT] = y;
  a = x;
  a |= 0x80;
  ram[ENEMY_ACTIVE + y] = a;
  ram[ENEMY_PAGE + y] = ram[(uint8_t)(ENEMY_PAGE + x)];
  ram[ENEMY_X + y] = ram[(uint8_t)(ENEMY_X + x)];
  a = 0x1;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  ram[ENEMY_Y_PAGE + y] = a;
  ram[ENEMY_Y + y] = ram[(uint8_t)(ENEMY_Y + x)];
  return;
}

// in: x; out: none
void init_bowser_flame(void) {
  a = ram[FRENZY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[ENEMY_Y_FORCE + x] = a;
  a = ram[NOISE_SFX_REQUEST];
  a |= 0x2;
  ram[NOISE_SFX_REQUEST] = a;
  y = ram[BOWSER_SLOT];
  a = ram[ENEMY_KIND + y];
  if (a != 0x2d) {
    next_flame_timer();
    carry_flag = false;
    a = (uint8_t)(a + 0x20 + carry_flag);
    y = ram[HARD_MODE_LATE]; zero_flag = y == 0;
    if (!zero_flag) {
      carry_flag = true;
      a = (uint8_t)(a - 0x10 - !carry_flag);
    }
    ram[FRENZY_TIMER] = a;
    a = ram[(RANDOM + x) & 0x7ff];
    a &= 0x3;
    ram[ENEMY_Y_FRACTION + x] = a;
    y = a;
    a = ROM(FLAME_HEIGHTS + y);
    place_flame(); // fallthrough
    return;
  }
  a = ram[ENEMY_X + y];
  carry_flag = true;
  a = (uint8_t)(a - 0xe - !carry_flag);
  ram[(uint8_t)(ENEMY_X + x)] = a;
  ram[(uint8_t)(ENEMY_PAGE + x)] = ram[ENEMY_PAGE + y];
  a = ram[ENEMY_Y + y];
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  a = ram[(RANDOM + x) & 0x7ff];
  a &= 0x3;
  ram[ENEMY_Y_FRACTION + x] = a;
  y = a;
  a = ROM(FLAME_HEIGHTS + y);
  y = 0x0;
  carry_flag = a >= ram[(uint8_t)(ENEMY_Y + x)];
  if (carry_flag) {
    y++;
  }
  ram[ENEMY_Y_FORCE + x] = ROM(FLAME_FORCE_OFFSETS + y);
  ram[FRENZY_ENEMY] = 0x0;
  finish_flame(); // fallthrough
  return;
}

// in: a, x; out: a
void place_flame(void) {
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  a = ram[SCREEN_RIGHT_X];
  carry_flag = false;
  { uint16_t sum = a + 0x20 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(ENEMY_X + x)] = a;
  a = ram[SCREEN_RIGHT_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  finish_flame(); return;
}

// in: x; out: a
void finish_flame(void) {
  ram[ENEMY_BOX_KIND + x] = 0x8;
  a = 0x1;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  a = a >> 1;
  ram[ENEMY_X_FORCE + x] = a;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  return;
}

// in: x; out: none
void init_firework(void) {
  a = ram[FRENZY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[FRENZY_TIMER] = 0x20;
  ram[FIREWORKS_COUNT]--;
  y = 0x6;
  do {
    y--;
    a = ram[ENEMY_KIND + y];
    zero_flag = a == 0x31;
  } while (!zero_flag);
  a = ram[ENEMY_X + y];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - 0x30 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[0x100 + sp--] = a;
  a = ram[ENEMY_PAGE + y];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  ram[0x0] = a;
  a = ram[FIREWORKS_COUNT];
  carry_flag = false;
  a = (uint8_t)(a + ram[ENEMY_STATE + y] + carry_flag);
  y = a;
  a = ram[0x100 + ++sp];
  carry_flag = false;
  { uint16_t sum = a + ROM(FIREWORK_X_OFFSETS + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(ENEMY_X + x)] = a;
  a = ram[0x0];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  ram[(uint8_t)(ENEMY_Y + x)] = ROM(FIREWORK_Y_OFFSETS + y);
  a = 0x1;
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  a = a >> 1;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0x8;
  return;
}

// in: x; out: x
void spawn_frenzy_enemy(void) {
  a = ram[FRENZY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[AREA_KIND]; zero_flag = a == 0;
  if (!zero_flag) { goto loc_c6fd; }
  if (x >= 0x3) {
    return;
  }
  y = 0x0;
  a = ram[(RANDOM + x) & 0x7ff];
  if (a >= 0xaa) {
    y++;
  }
  a = ram[WORLD_NUMBER];
  if (a != 0x1) {
    y++;
  }
  a = y;
  a &= 0x1;
  y = a;
  a = ROM(SWIMMING_FISH_KINDS + y);
  
loc_c6c3:
  ram[(uint8_t)(ENEMY_KIND + x)] = a;
  a = ram[MOVE_BIT_FILTER];
  if (a == 0xff) {
    ram[MOVE_BIT_FILTER] = 0x0;
  }
  a = ram[(RANDOM + x) & 0x7ff];
  a &= 0x7;
  
loc_c6d6:
  y = a;
  a = ROM(BIT_MASKS + y);
  zero_flag = (a & ram[MOVE_BIT_FILTER]) == 0;
  if (!zero_flag) {
    y++;
    a = y;
    a &= 0x7;
    goto loc_c6d6;
  }
  a |= ram[MOVE_BIT_FILTER];
  ram[MOVE_BIT_FILTER] = a;
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
    carry_flag = y >= 0x5;
    if (carry_flag) { goto loc_c711; }
    a = ram[ENEMY_ACTIVE + y]; zero_flag = a == 0;
    if (zero_flag) { goto loc_c6ff; }
    a = ram[ENEMY_KIND + y];
    zero_flag = a == 0x8;
  } while (!zero_flag);
  return;
  
loc_c711:
  a = ram[PULSE2_SFX_REQUEST];
  a |= 0x8;
  ram[PULSE2_SFX_REQUEST] = a;
  a = 0x8;
  goto loc_c6c3;
}

// in: x; out: none
void init_piranha(void) {
  a = 0x1;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  a = a >> 1;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  ram[ENEMY_Y_FORCE + x] = a;
  carry_flag = true;
  a = (uint8_t)(a - 0x18 - !carry_flag);
  ram[ENEMY_Y_FRACTION + x] = a;
  a = 0x9;
  set_box_kind(); return;
}

// in: x; out: x
void init_frenzy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  ram[FRENZY_ENEMY] = a;
  carry_flag = true;
  a = (uint8_t)(a - 0x12 - !carry_flag);
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
  y = 0x5;
  do {
    a = ram[ENEMY_KIND + y];
    if (a == 0x11) {
      ram[ENEMY_STATE + y] = 0x1;
    }
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  a = 0x0;
  ram[FRENZY_ENEMY] = a;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  return;
}

// in: x; out: none
void init_bouncing_paratroopa(void) {
  ram[(uint8_t)(ENEMY_DIR + x)] = 0x2;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0xf8;
  set_tall_box_b(); // fallthrough
  return;
}

// in: x; out: none
void set_tall_box_b(void) {
  a = 0x3;
  set_box_kind(); // fallthrough
  return;
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
  y = ram[HARD_MODE_LATE]; zero_flag = y == 0;
  if (zero_flag) {
    y = 0x2;
    offset_platform();
  }
  y = 0xff;
  a = ram[BALANCE_ALIGNMENT]; neg_flag = (a & 0x80) != 0;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  if (neg_flag) {
    a = x;
    y = a;
  }
  ram[BALANCE_ALIGNMENT] = y;
  a = 0x0;
  ram[(uint8_t)(ENEMY_DIR + x)] = a;
  y = a;
  offset_platform();
  init_drop_platform(); // fallthrough
  return;
}

// in: x; out: none
void init_drop_platform(void) {
  ram[PLATFORM_CONTACT + x] = 0xff;
  init_platform(); return;
}

// in: x; out: none
void init_side_platform(void) {
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0x0;
  init_platform(); return;
}

// in: x; out: none
void init_vertical_platform(void) {
  y = 0x40;
  a = ram[(uint8_t)(ENEMY_Y + x)]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    y = 0xc0;
  }
  ram[ENEMY_X_FORCE + x] = a;
  a = y;
  carry_flag = false;
  a = (uint8_t)(a + ram[(uint8_t)(ENEMY_Y + x)] + carry_flag);
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  init_platform(); // fallthrough
  return;
}

// in: x; out: none
void init_platform(void) {
  clear_vertical_motion();
  set_platform_box(); // fallthrough
  return;
}

// in: x; out: none
void set_platform_box(void) {
  a = 0x5;
  y = ram[AREA_KIND];
  if (y != 0x3) {
    y = ram[HARD_MODE_LATE]; zero_flag = y == 0;
    if (!zero_flag) { goto loc_c83b; }
    a = 0x6;
  }
  
loc_c83b:
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
  large_lift_box(); // fallthrough
  return;
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
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0x0;
  small_lift_common(); // fallthrough
  return;
}

// in: x; out: none
void small_lift_common(void) {
  y = 0x1;
  offset_platform();
  ram[ENEMY_BOX_KIND + x] = 0x4;
  return;
}

// in: x, y; out: none
void offset_platform(void) {
  a = ram[(uint8_t)(ENEMY_X + x)];
  carry_flag = false;
  { uint16_t sum = a + ROM(PLATFORM_OFFSET_LOW + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(ENEMY_X + x)] = a;
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  a = (uint8_t)(a + ROM(PLATFORM_OFFSET_HIGH + y) + carry_flag);
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
  ram[ENEMY_SPRITE_ATTR + x] = 0x0;
  enemy_offscreen_bits();
  enemy_screen_position();
  draw_enemy();
  enemy_box();
  enemy_bg_collision();
  enemy_enemy_collisions();
  player_enemy_collision();
  y = ram[TIMERS_PAUSED]; zero_flag = y == 0;
  if (zero_flag) {
    move_enemy();
  }
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
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    move_large_platform();
  }
  enemy_screen_position();
  draw_large_platform();
  check_enemy_offscreen(); return;
}

// in: x; out: x
void move_large_platform(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = true;
  a = (uint8_t)(a - 0x24 - !carry_flag);
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
  a = 0x0;
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = a;
  ram[(uint8_t)(ENEMY_KIND + x)] = a;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  ram[POPUP_KIND + x] = a;
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = a;
  ram[SHELL_CHAIN + x] = a;
  ram[ENEMY_SPRITE_ATTR + x] = a;
  ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = a;
  return;
}

// in: x; out: x
void move_podoboo(void) {
  a = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff]; zero_flag = a == 0;
  if (zero_flag) {
    init_podoboo();
    a = ram[((RANDOM + 1) + x) & 0x7ff];
    a |= 0x80;
    ram[ENEMY_Y_FORCE + x] = a;
    a &= 0xf;
    a |= 0x6;
    ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = a;
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xf9;
  }
  move_enemy_jumping(); return;
}

// in: x; out: x
void update_hammer_bro(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    move_defeated_enemy(); return;
  }
  a = ram[(uint8_t)(HAMMER_JUMP_TIMER + x)]; zero_flag = a == 0;
  if (!zero_flag) {
    ram[(uint8_t)(HAMMER_JUMP_TIMER + x)]--;
    a = ram[ENEMY_OFFSCREEN];
    a &= 0xc; zero_flag = a == 0;
    if (!zero_flag) {
      hammer_bro_walk();
      return;
    }
    a = ram[PLATFORM_CONTACT + x]; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[HARD_MODE_LATE];
      ram[PLATFORM_CONTACT + x] = ROM(HAMMER_THROW_MASKS + y);
      spawn_hammer();
      if (!carry_flag) { goto loc_ca0a; }
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      a |= 0x8;
      ram[(uint8_t)(ENEMY_STATE + x)] = a;
      hammer_bro_walk(); return;
    }
    
loc_ca0a:
    ram[PLATFORM_CONTACT + x]--;
    hammer_bro_walk(); return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x7;
  if (a == 0x1) {
    hammer_bro_walk();
    return;
  }
  ram[0x0] = 0x0;
  y = 0xfa;
  a = ram[(uint8_t)(ENEMY_Y + x)]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    hammer_bro_jump();
    return;
  }
  y = 0xfd;
  carry_flag = a >= 0x70;
  ram[0x0]++;
  if (!carry_flag) {
    hammer_bro_jump();
    return;
  }
  ram[0x0]--;
  a = ram[((RANDOM + 1) + x) & 0x7ff];
  a &= 0x1; zero_flag = a == 0;
  if (!zero_flag) {
    hammer_bro_jump();
    return;
  }
  y = 0xfa;
  hammer_bro_jump(); // fallthrough
  return;
}

// in: x, y; out: x, y
void hammer_bro_jump(void) {
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = y;
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a |= 0x1;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  a = ram[0x0];
  a &= ram[((RANDOM + 2) + x) & 0x7ff];
  y = a;
  a = ram[HARD_MODE_LATE]; zero_flag = a == 0;
  if (zero_flag) {
    y = a;
  }
  ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = ROM(HAMMER_BRO_JUMP_LENGTHS + y);
  a = ram[((RANDOM + 1) + x) & 0x7ff];
  a |= 0xc0;
  ram[(uint8_t)(HAMMER_JUMP_TIMER + x)] = a;
  hammer_bro_walk(); // fallthrough
  return;
}

// in: x; out: x, y
void hammer_bro_walk(void) {
  y = 0xfc;
  a = ram[FRAME_COUNT];
  a &= 0x40; zero_flag = a == 0;
  if (zero_flag) {
    y = 0x4;
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = y;
  y = 0x1;
  player_enemy_distance();
  if (!neg_flag) {
    y++;
    a = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_ca75; }
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0xf8;
  }
  
loc_ca75:
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  move_walker(); // fallthrough
  return;
}

// in: x; out: x, y
void move_walker(void) {
  y = 0x0;
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x40; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) { goto loc_cab4; }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    a &= 0x20; zero_flag = a == 0;
    if (!zero_flag) {
      move_defeated_enemy();
      return;
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    a &= 0x7; zero_flag = a == 0;
    if (zero_flag) { goto loc_cab4; }
    zero_flag = a == 0x5;
    if (zero_flag) { goto loc_ca98; }
    carry_flag = a >= 0x3;
    if (carry_flag) { goto loc_cac8; }
  }
  
loc_ca98:
  move_enemy_down_fast();
  y = 0x0;
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  if (a != 0x2) {
    a &= 0x40; zero_flag = a == 0;
    if (zero_flag) { goto loc_cab4; }
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    zero_flag = a == 0x2e;
    if (zero_flag) { goto loc_cab4; }
  } else {
    move_enemy_x(); return;
  }
  y = 0x1;
  
loc_cab4:
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)]; neg_flag = (a & 0x80) != 0;
  ram[0x100 + sp--] = a;
  if (neg_flag) {
    y++;
    y++;
  }
  carry_flag = false;
  a = (uint8_t)(a + ROM(WALK_SPEED_ADDERS + y) + carry_flag);
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  move_enemy_x();
  a = ram[0x100 + ++sp];
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  return;
  
loc_cac8:
  a = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff]; zero_flag = a == 0;
  if (zero_flag) {
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    a = ram[FRAME_COUNT];
    a &= 0x1;
    y = a;
    y++;
    ram[(uint8_t)(ENEMY_DIR + x)] = y;
    y--;
    a = ram[HARD_MODE]; zero_flag = a == 0;
    if (!zero_flag) {
      y++;
      y++;
    }
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(REVIVED_SPEEDS + y);
    return;
  }
  if (a != 0xe) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x6) {
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
  
loc_bf70:
  y = 0x0;
  goto loc_bf77;
  
loc_bf75:
  y = 0x1;
  
loc_bf77:
  x++;
  ram[0x0] = 0x3;
  ram[0x1] = 0x6;
  ram[0x2] = 0x2;
  a = y;
  move_paratroopa_vertically(); return;
  
move_red_paratroopa:
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  a |= ram[ENEMY_Y_FORCE + x]; zero_flag = a == 0;
  if (zero_flag) {
    ram[ENEMY_Y_FRACTION + x] = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    carry_flag = a >= ram[ENEMY_X_FORCE + x];
    if (carry_flag) { goto loc_cb19; }
    a = ram[FRAME_COUNT];
    a &= 0x7; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
    ram[(uint8_t)(ENEMY_Y + x)]++;
    return;
  }
  
loc_cb19:
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = a >= ram[(uint8_t)(ENEMY_X_SPEED + x)];
  if (carry_flag) {
    goto loc_bf75;
  }
  goto loc_bf70;
}

// in: x; out: x
void move_green_paratroopa(void) {
  sway_green_paratroopa();
  sway_offset();
  y = 0x1;
  a = ram[FRAME_COUNT];
  a &= 0x3; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[FRAME_COUNT];
  a &= 0x40; zero_flag = a == 0;
  if (zero_flag) {
    y = 0xff;
  }
  ram[0x0] = y;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  return;
}

// in: x; out: none
void sway_green_paratroopa(void) {
  a = 0x13;
  sway_counters(); // fallthrough
  return;
}

// oscillating primary/secondary counters for back-and-forth motion
// in: a, x; out: none
void sway_counters(void) {
  ram[0x1] = a;
  a = ram[FRAME_COUNT];
  a &= 0x3; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  y = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_cb60; }
  zero_flag = y == ram[0x1];
  if (!zero_flag) {
    ram[(uint8_t)(ENEMY_X_SPEED + x)]++;
    return;
  }
  
loc_cb5d:
  ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
  return;
  
loc_cb60:
  a = y; zero_flag = a == 0;
  if (zero_flag) { goto loc_cb5d; }
  ram[(uint8_t)(ENEMY_X_SPEED + x)]--;
  return;
}

// in: x; out: x
void sway_offset(void) {
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  ram[0x100 + sp--] = a;
  y = 0x1;
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  a &= 0x2; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    y = 0x2;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  move_enemy_x();
  ram[0x0] = a;
  a = ram[0x100 + ++sp];
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  return;
}

// in: x; out: x
void move_blooper(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (zero_flag) {
    y = ram[HARD_MODE_LATE];
    a = ram[((RANDOM + 1) + x) & 0x7ff];
    a &= ROM(BLOOPER_MASKS + y); zero_flag = a == 0;
    if (zero_flag) {
      a = x;
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (carry_flag) {
        y = ram[PLAYER_DIR];
      } else {
        y = 0x2;
        player_enemy_distance();
        if (!neg_flag) { goto loc_cbaa; }
        y--;
      }
      
loc_cbaa:
      ram[(uint8_t)(ENEMY_DIR + x)] = y;
    }
    blooper_swim();
    a = ram[(uint8_t)(ENEMY_Y + x)];
    carry_flag = true;
    a = (uint8_t)(a - ram[ENEMY_Y_FORCE + x] - !carry_flag);
    if (a >= 0x20) {
      ram[(uint8_t)(ENEMY_Y + x)] = a;
    }
    y = ram[(uint8_t)(ENEMY_DIR + x)];
    y--; zero_flag = y == 0;
    if (zero_flag) {
      a = ram[(uint8_t)(ENEMY_X + x)];
      carry_flag = false;
      { uint16_t sum = a + ram[(uint8_t)(ENEMY_X_SPEED + x)] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[(uint8_t)(ENEMY_X + x)] = a;
      a = ram[(uint8_t)(ENEMY_PAGE + x)];
      a = (uint8_t)(a + 0x0 + carry_flag);
      ram[(uint8_t)(ENEMY_PAGE + x)] = a;
      return;
    }
    a = ram[(uint8_t)(ENEMY_X + x)];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(ENEMY_X_SPEED + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[(uint8_t)(ENEMY_X + x)] = a;
    a = ram[(uint8_t)(ENEMY_PAGE + x)];
    a = (uint8_t)(a - 0x0 - !carry_flag);
    ram[(uint8_t)(ENEMY_PAGE + x)] = a;
    return;
  }
  move_enemy_down_slow(); return;
}

// in: x; out: none
void blooper_swim(void) {
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  a &= 0x2; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[FRAME_COUNT];
    a &= 0x7;
    ram[0x100 + sp--] = a;
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (!carry_flag) {
      a = ram[0x100 + ++sp]; zero_flag = a == 0;
      if (!zero_flag) {
        return;
      }
      a = ram[ENEMY_Y_FORCE + x];
      carry_flag = false;
      a = (uint8_t)(a + 0x1 + carry_flag);
      ram[ENEMY_Y_FORCE + x] = a;
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
      if (a != 0x2) {
        return;
      }
      ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
      return;
    }
    a = ram[0x100 + ++sp]; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
    a = ram[ENEMY_Y_FORCE + x];
    carry_flag = true;
    a = (uint8_t)(a - 0x1 - !carry_flag); zero_flag = a == 0;
    ram[ENEMY_Y_FORCE + x] = a;
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    if (!zero_flag) {
      return;
    }
    ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
    ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = 0x2;
    return;
  }
  a = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff]; zero_flag = a == 0;
  if (zero_flag) { goto loc_cc29; }
  
loc_cc21:
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) {
    return;
  }
  ram[(uint8_t)(ENEMY_Y + x)]++;
  return;
  
loc_cc29:
  a = ram[(uint8_t)(ENEMY_Y + x)];
  a = (uint8_t)(a + 0x10 + carry_flag);
  carry_flag = a >= ram[PLAYER_Y];
  if (!carry_flag) { goto loc_cc21; }
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0x0;
  return;
}

// in: x; out: x
void move_bullet(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    move_enemy_jumping(); return;
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0xe8;
  move_enemy_x(); return;
}

// in: x; out: x
void move_swimming_fish(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    move_enemy_down_slow(); return;
  }
  ram[0x3] = a;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  carry_flag = true;
  a = (uint8_t)(a - 0xa - !carry_flag);
  y = a;
  ram[0x2] = ROM(SWIMMING_FISH_SPEEDS + y);
  a = ram[ENEMY_X_FORCE + x];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[0x2] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[ENEMY_X_FORCE + x] = a;
  a = ram[(uint8_t)(ENEMY_X + x)];
  { uint16_t diff = (uint16_t)(a - 0x0 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[(uint8_t)(ENEMY_X + x)] = a;
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  ram[(uint8_t)(ENEMY_PAGE + x)] = a;
  ram[0x2] = 0x20;
  if (x < 0x2) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  if (a >= 0x10) {
    a = ram[ENEMY_Y_FRACTION + x];
    carry_flag = false;
    { uint16_t sum = a + ram[0x2] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[ENEMY_Y_FRACTION + x] = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    { uint16_t sum = a + ram[0x3] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    a = ram[(uint8_t)(ENEMY_Y_PAGE + x)];
    a = (uint8_t)(a + 0x0 + carry_flag);
  } else {
    a = ram[ENEMY_Y_FRACTION + x];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[0x2] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[ENEMY_Y_FRACTION + x] = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    { uint16_t diff = (uint16_t)(a - ram[0x3] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    a = ram[(uint8_t)(ENEMY_Y_PAGE + x)];
    a = (uint8_t)(a - 0x0 - !carry_flag);
  }
  ram[(uint8_t)(ENEMY_Y_PAGE + x)] = a;
  y = 0x0;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = true;
  a = (uint8_t)(a - ram[ENEMY_Y_FORCE + x] - !carry_flag); neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    y = 0x10;
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
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
  a = ram[ENEMY_OFFSCREEN];
  a &= 0x8; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[FIREBAR_SPEED + x];
    spin_firebar();
    a &= 0x1f;
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  }
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  if (y >= 0x1f) {
    if (a != 0x8) {
      zero_flag = a == 0x18;
      if (!zero_flag) { goto loc_cd6a; }
    }
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  }
  
loc_cd6a:
  ram[(ENEMY_DATA_PTR + 6)] = a;
  enemy_screen_position();
  firebar_ball_position();
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_SCREEN_Y];
  ram[SPRITE_Y + y] = a;
  ram[0x7] = a;
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[0x6] = a;
  ram[0x0] = 0x1;
  firebar_ball_collision();
  y = 0x5;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a >= 0x1f) {
    y = 0xb;
  }
  ram[(ENEMY_DATA_PTR + 4)] = y;
  ram[0x0] = 0x0;
  do {
    a = ram[(ENEMY_DATA_PTR + 6)];
    firebar_ball_position();
    draw_firebar_ball();
    a = ram[0x0];
    if (a == 0x4) {
      y = ram[COPY_SLOT];
      ram[0x6] = ram[ENEMY_SPRITE_SLOT + y];
    }
    ram[0x0]++;
    a = ram[0x0];
    carry_flag = a >= ram[(ENEMY_DATA_PTR + 4)];
  } while (!carry_flag);
  return;
}

// in: none; out: x
void draw_firebar_ball(void) {
  ram[0x5] = ram[0x3];
  y = ram[0x6];
  a = ram[0x1];
  carry_flag = (ram[0x5] & 1) != 0; ram[0x5] = ram[0x5] >> 1;
  if (!carry_flag) {
    a ^= 0xff;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  carry_flag = false;
  a = (uint8_t)(a + ram[ENEMY_SCREEN_X] + carry_flag);
  ram[SPRITE_X + y] = a;
  ram[0x6] = a;
  carry_flag = a >= ram[ENEMY_SCREEN_X];
  if (!carry_flag) {
    a = ram[ENEMY_SCREEN_X];
    carry_flag = true;
    a = (uint8_t)(a - ram[0x6] - !carry_flag);
  } else {
    carry_flag = true;
    a = (uint8_t)(a - ram[ENEMY_SCREEN_X] - !carry_flag);
  }
  if (a >= 0x59) {
    a = 0xf8;
  } else {
    a = ram[ENEMY_SCREEN_Y];
    zero_flag = a == 0xf8;
    if (zero_flag) { goto loc_ce03; }
    a = ram[0x2];
    carry_flag = (ram[0x5] & 1) != 0; ram[0x5] = ram[0x5] >> 1;
    if (!carry_flag) {
      a ^= 0xff;
      a = (uint8_t)(a + 0x1 + carry_flag);
    }
    carry_flag = false;
    a = (uint8_t)(a + ram[ENEMY_SCREEN_Y] + carry_flag);
  }
  
loc_ce03:
  ram[SPRITE_Y + y] = a;
  ram[0x7] = a;
  firebar_ball_collision(); // fallthrough
  return;
}

// in: y; out: x
void firebar_ball_collision(void) {
  draw_firebar_tile();
  a = y;
  ram[0x100 + sp--] = a;
  a = ram[STAR_TIMER];
  a |= ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    ram[0x5] = a;
    y = ram[PLAYER_Y_PAGE];
    y--; zero_flag = y == 0;
    if (!zero_flag) { goto loc_ce85; }
    y = ram[PLAYER_Y];
    a = ram[PLAYER_SMALL]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[CROUCHING]; zero_flag = a == 0;
      if (zero_flag) { goto loc_ce31; }
    }
    ram[0x5]++;
    ram[0x5]++;
    a = y;
    carry_flag = false;
    a = (uint8_t)(a + 0x18 + carry_flag);
    y = a;
    
loc_ce31:
    a = y;
    
loc_ce32:
    carry_flag = true;
    a = (uint8_t)(a - ram[0x7] - !carry_flag); neg_flag = (a & 0x80) != 0;
    if (neg_flag) {
      a ^= 0xff;
      carry_flag = false;
      a = (uint8_t)(a + 0x1 + carry_flag);
    }
    if (a < 0x8) {
      a = ram[0x6];
      carry_flag = a >= 0xf0;
      if (carry_flag) { goto loc_ce5c; }
      a = ram[(SPRITE_X + 4)];
      carry_flag = false;
      a = (uint8_t)(a + 0x4 + carry_flag);
      ram[0x4] = a;
      carry_flag = true;
      a = (uint8_t)(a - ram[0x6] - !carry_flag); neg_flag = (a & 0x80) != 0;
      if (neg_flag) {
        a ^= 0xff;
        carry_flag = false;
        a = (uint8_t)(a + 0x1 + carry_flag);
      }
      carry_flag = a >= 0x8;
      if (!carry_flag) { goto loc_ce6f; }
    }
    
loc_ce5c:
    a = ram[0x5];
    zero_flag = a == 0x2;
    if (zero_flag) { goto loc_ce85; }
    y = ram[0x5];
    a = ram[PLAYER_Y];
    carry_flag = false;
    a = (uint8_t)(a + ROM(FIREBAR_Y_POSITIONS + y) + carry_flag);
    ram[0x5]++;
    goto loc_ce32;
    
loc_ce6f:
    x = 0x1;
    a = ram[0x4];
    carry_flag = a >= ram[0x6];
    if (!carry_flag) {
      x++;
    }
    ram[ENEMY_DIR] = x;
    x = 0x0;
    a = ram[0x0];
    ram[0x100 + sp--] = a;
    injure_player();
    a = ram[0x100 + ++sp];
    ram[0x0] = a;
  }
  
loc_ce85:
  a = ram[0x100 + ++sp];
  carry_flag = false;
  a = (uint8_t)(a + 0x4 + carry_flag);
  ram[0x6] = a;
  x = ram[SLOT];
  return;
}

// in: a; out: none
void firebar_ball_position(void) {
  ram[0x100 + sp--] = a;
  a &= 0xf;
  if (a >= 0x9) {
    a ^= 0xf;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  ram[0x1] = a;
  y = ram[0x0];
  a = ROM(FIREBAR_TABLE_OFFSETS + y);
  carry_flag = false;
  a = (uint8_t)(a + ram[0x1] + carry_flag);
  y = a;
  ram[0x1] = ROM(FIREBAR_OFFSETS + y);
  a = ram[0x100 + ++sp];
  ram[0x100 + sp--] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  a &= 0xf;
  if (a >= 0x9) {
    a ^= 0xf;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  ram[0x2] = a;
  y = ram[0x0];
  a = ROM(FIREBAR_TABLE_OFFSETS + y);
  carry_flag = false;
  a = (uint8_t)(a + ram[0x2] + carry_flag);
  y = a;
  ram[0x2] = ROM(FIREBAR_OFFSETS + y);
  a = ram[0x100 + ++sp];
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  y = a;
  ram[0x3] = ROM(FIREBAR_MIRRORS + y);
  return;
}

// in: x; out: x
void move_flying_fish(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    ram[ENEMY_SPRITE_ATTR + x] = 0x0;
    move_enemy_jumping(); return;
  }
  move_enemy_x();
  y = 0xd;
  a = 0x5;
  apply_gravity_enemy();
  a = ram[ENEMY_Y_FORCE + x];
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  y = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = true;
  a = (uint8_t)(a - ROM(RANDOM_Y_RANGES + y) - !carry_flag); neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  if (a < 0x8) {
    a = ram[ENEMY_Y_FORCE + x];
    carry_flag = false;
    a = (uint8_t)(a + 0x10 + carry_flag);
    ram[ENEMY_Y_FORCE + x] = a;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    y = a;
  }
  ram[ENEMY_SPRITE_ATTR + x] = ROM(FLYING_FISH_PRIORITIES + y);
  return;
}

// in: x; out: x
void move_lakitu(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    move_enemy_down_fast(); return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0x0;
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
    ram[FRENZY_ENEMY] = a;
    a = 0x10;
  } else {
    ram[FRENZY_ENEMY] = 0x12;
    y = 0x2;
    do {
      ram[0x1 + y] = ROM(LAKITU_SPEED_ADJUST + y);
      y--; neg_flag = (y & 0x80) != 0;
    } while (!neg_flag);
    lakitu_chase();
  }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  y = 0x1;
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  a &= 0x1; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    y++;
  }
  ram[(uint8_t)(ENEMY_DIR + x)] = y;
  move_enemy_x(); return;
}

// in: x; out: a
void lakitu_chase(void) {
  y = 0x0;
  player_enemy_distance();
  if (neg_flag) {
    y++;
    a = ram[0x0];
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[0x0] = a;
  }
  a = ram[0x0];
  if (a >= 0x3c) {
    ram[0x0] = 0x3c;
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    zero_flag = a == 0x11;
    if (!zero_flag) { goto loc_cf9f; }
    a = y;
    zero_flag = a == ram[(uint8_t)(ENEMY_Y_SPEED + x)];
    if (zero_flag) { goto loc_cf9f; }
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)]; zero_flag = a == 0;
    if (!zero_flag) {
      ram[(uint8_t)(ENEMY_X_SPEED + x)]--;
      a = ram[(uint8_t)(ENEMY_X_SPEED + x)]; zero_flag = a == 0;
      if (!zero_flag) {
        return;
      }
    }
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = y;
  }
  
loc_cf9f:
  a = ram[0x0];
  a &= 0x3c;
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a;
  y = 0x0;
  a = ram[PLAYER_X_SPEED]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[SCROLL_AMOUNT]; zero_flag = a == 0;
    if (zero_flag) { goto loc_cfd1; }
    y++;
    a = ram[PLAYER_X_SPEED];
    if (a >= 0x19) {
      a = ram[SCROLL_AMOUNT];
      carry_flag = a >= 0x2;
      if (!carry_flag) { goto loc_cfc1; }
      y++;
    }
    
loc_cfc1:
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    if (a == 0x12) {
      a = ram[PLAYER_X_SPEED]; zero_flag = a == 0;
      if (!zero_flag) { goto loc_cfd1; }
    }
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_cfd1; }
    y = 0x0;
  }
  
loc_cfd1:
  a = ram[0x1 + y];
  y = ram[0x0];
  do {
    carry_flag = true;
    a = (uint8_t)(a - 0x1 - !carry_flag);
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  return;
}

void collapse_bridge(void) {
  x = ram[BOWSER_SLOT];
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x2d) {
    ram[SLOT] = x;
    a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
    if (zero_flag) { goto loc_d015; }
    a &= 0x40; zero_flag = a == 0;
    if (zero_flag) { goto loc_d005; }
    a = ram[(uint8_t)(ENEMY_Y + x)];
    if (a < 0xe0) {
      move_dying_bowser();
      return;
    }
  }
  
loc_d005:
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  ram[GAME_MODE_TASK]++;
  kill_all_enemies(); return;
  
loc_d015:
  ram[BOWSER_FEET]--; zero_flag = ram[BOWSER_FEET] == 0;
  if (zero_flag) {
    ram[BOWSER_FEET] = 0x4;
    a = ram[BOWSER_BODY];
    a ^= 0x1;
    ram[BOWSER_BODY] = a;
    ram[0x5] = 0x22;
    y = ram[BRIDGE_COLLAPSE_POS];
    ram[0x4] = ROM(BRIDGE_TILE_COLUMNS + y);
    y = ram[VRAM_LIST1_LENGTH];
    y++;
    x = 0xc;
    queue_metatile_tiles();
    x = ram[SLOT];
    advance_vram_list();
    ram[PULSE2_SFX_REQUEST] = 0x8;
    ram[NOISE_SFX_REQUEST] = 0x1;
    ram[BRIDGE_COLLAPSE_POS]++;
    a = ram[BRIDGE_COLLAPSE_POS];
    zero_flag = a == 0xf;
    if (!zero_flag) { goto loc_d05e; }
    clear_vertical_motion();
    ram[(uint8_t)(ENEMY_STATE + x)] = 0x40;
    ram[PULSE2_SFX_REQUEST] = 0x80;
  }
  
loc_d05e:
  draw_bowser(); return;
}

// in: x; out: x
void move_dying_bowser(void) {
  move_enemy_down_slow();
  draw_bowser(); return;
}

// in: x; out: x
void run_bowser(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[(uint8_t)(ENEMY_Y + x)];
    if (a < 0xe0) {
      move_dying_bowser();
      return;
    }
    kill_all_enemies(); // fallthrough
    return;
  }
  ram[FRENZY_ENEMY] = 0x0;
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (!zero_flag) {
  } else {
    a = ram[BOWSER_BODY]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) {
    } else {
      ram[BOWSER_FEET]--; zero_flag = ram[BOWSER_FEET] == 0;
      if (zero_flag) {
        ram[BOWSER_FEET] = 0x20;
        a = ram[BOWSER_BODY];
        a ^= 0x1;
        ram[BOWSER_BODY] = a;
      }
      a = ram[FRAME_COUNT];
      a &= 0xf; zero_flag = a == 0;
      if (zero_flag) {
        ram[(uint8_t)(ENEMY_DIR + x)] = 0x2;
      }
      a = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff]; zero_flag = a == 0;
      if (!zero_flag) {
        player_enemy_distance();
        if (!neg_flag) { goto loc_d0d1; }
        ram[(uint8_t)(ENEMY_DIR + x)] = 0x1;
        ram[BOWSER_SPEED] = 0x2;
        a = 0x20;
        ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = a;
        ram[BOWSER_BREATH_TIMER] = a;
        a = ram[(uint8_t)(ENEMY_X + x)];
        carry_flag = a >= 0xc8;
        if (carry_flag) { goto loc_d10f; }
      }
      
loc_d0d1:
      a = ram[FRAME_COUNT];
      a &= 0x3; zero_flag = a == 0;
      if (!zero_flag) { goto loc_d10f; }
      a = ram[(uint8_t)(ENEMY_X + x)];
      zero_flag = a == ram[BOWSER_HOME_X];
      if (zero_flag) {
        a = ram[(RANDOM + x) & 0x7ff];
        a &= 0x3;
        y = a;
        ram[MAX_RANGE] = ROM(BOWSER_MOVE_RANGES + y);
      }
      a = ram[(uint8_t)(ENEMY_X + x)];
      carry_flag = false;
      a = (uint8_t)(a + ram[BOWSER_SPEED] + carry_flag);
      ram[(uint8_t)(ENEMY_X + x)] = a;
      y = ram[(uint8_t)(ENEMY_DIR + x)];
      zero_flag = y == 0x1;
      if (zero_flag) { goto loc_d10f; }
      y = 0xff;
      carry_flag = true;
      a = (uint8_t)(a - ram[BOWSER_HOME_X] - !carry_flag); neg_flag = (a & 0x80) != 0;
      if (neg_flag) {
        a ^= 0xff;
        carry_flag = false;
        a = (uint8_t)(a + 0x1 + carry_flag);
        y = 0x1;
      }
      carry_flag = a >= ram[MAX_RANGE];
      if (!carry_flag) { goto loc_d10f; }
      ram[BOWSER_SPEED] = y;
    }
    
loc_d10f:
    a = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_d13c; }
    move_enemy_down_slow();
    a = ram[WORLD_NUMBER];
    if (a >= 0x5) {
      a = ram[FRAME_COUNT];
      a &= 0x3; zero_flag = a == 0;
      if (!zero_flag) { goto loc_d127; }
      spawn_hammer();
    }
    
loc_d127:
    a = ram[(uint8_t)(ENEMY_Y + x)];
    carry_flag = a >= 0x80;
    if (!carry_flag) { goto loc_d149; }
    a = ram[(RANDOM + x) & 0x7ff];
    a &= 0x3;
    y = a;
    ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = ROM(BOWSER_MOVE_RANGES + y);
  }
  goto loc_d149;
  
loc_d13c:
  if (a == 0x1) {
    ram[(uint8_t)(ENEMY_Y + x)]--;
    clear_vertical_motion();
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfe;
  }
  do {
    
loc_d149:
    a = ram[WORLD_NUMBER];
    if (a != 0x7) {
      if (a >= 0x5) {
        draw_bowser();
        return;
      }
    }
    a = ram[BOWSER_BREATH_TIMER]; zero_flag = a == 0;
    if (!zero_flag) {
      draw_bowser();
      return;
    }
    ram[BOWSER_BREATH_TIMER] = 0x20;
    a = ram[BOWSER_BODY];
    a ^= 0x80; neg_flag = (a & 0x80) != 0;
    ram[BOWSER_BODY] = a;
  } while (neg_flag);
  next_flame_timer();
  y = ram[HARD_MODE_LATE]; zero_flag = y == 0;
  if (!zero_flag) {
    carry_flag = true;
    a = (uint8_t)(a - 0x10 - !carry_flag);
  }
  ram[BOWSER_BREATH_TIMER] = a;
  ram[FRENZY_ENEMY] = 0x15;
  draw_bowser(); // fallthrough
  return;
}

// in: none; out: x
void kill_all_enemies(void) {
  x = 0x4;
  do {
    erase_enemy();
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  ram[FRENZY_ENEMY] = a;
  x = ram[SLOT];
  return;
}

// in: x; out: x
void draw_bowser(void) {
  bowser_half();
  y = 0x10;
  a = ram[(uint8_t)(ENEMY_DIR + x)];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) {
    y = 0xf0;
  }
  a = y;
  carry_flag = false;
  a = (uint8_t)(a + ram[(uint8_t)(ENEMY_X + x)] + carry_flag);
  y = ram[COPY_SLOT];
  ram[ENEMY_X + y] = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[ENEMY_Y + y] = a;
  ram[ENEMY_STATE + y] = ram[(uint8_t)(ENEMY_STATE + x)];
  ram[ENEMY_DIR + y] = ram[(uint8_t)(ENEMY_DIR + x)];
  a = ram[SLOT];
  ram[0x100 + sp--] = a;
  x = ram[COPY_SLOT];
  ram[SLOT] = x;
  ram[(uint8_t)(ENEMY_KIND + x)] = 0x2d;
  bowser_half();
  a = ram[0x100 + ++sp];
  ram[SLOT] = a;
  x = a;
  ram[BOWSER_GFX_FLAG] = 0x0;
  return;
}

// in: x; out: x
void bowser_half(void) {
  ram[BOWSER_GFX_FLAG]++;
  run_retainer();
  a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
  if (!zero_flag) {
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
  a = ram[BOWSER_FLAME_TIMER];
  a &= 0x7;
  ram[BOWSER_FLAME_TIMER] = a;
  a = ROM(FLAME_TIMERS + y);
  return;
}

// in: x; out: x
void update_bowser_flame(void) {
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = 0x40;
    y = ram[HARD_MODE_LATE]; zero_flag = y == 0;
    if (!zero_flag) {
      a = 0x60;
    }
    ram[0x0] = a;
    a = ram[ENEMY_X_FORCE + x];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[0x0] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[ENEMY_X_FORCE + x] = a;
    a = ram[(uint8_t)(ENEMY_X + x)];
    { uint16_t diff = (uint16_t)(a - 0x1 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[(uint8_t)(ENEMY_X + x)] = a;
    a = ram[(uint8_t)(ENEMY_PAGE + x)];
    a = (uint8_t)(a - 0x0 - !carry_flag);
    ram[(uint8_t)(ENEMY_PAGE + x)] = a;
    y = ram[ENEMY_Y_FRACTION + x];
    a = ram[(uint8_t)(ENEMY_Y + x)];
    zero_flag = a == ROM(FLAME_HEIGHTS + y);
    if (zero_flag) { goto loc_d220; }
    carry_flag = false;
    a = (uint8_t)(a + ram[ENEMY_Y_FORCE + x] + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
  }
  
loc_d220:
  enemy_screen_position();
  a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  ram[0x0] = 0x51;
  y = 0x2;
  a = ram[FRAME_COUNT];
  a &= 0x2; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x82;
  }
  ram[0x1] = y;
  y = ram[ENEMY_SPRITE_SLOT + x];
  x = 0x0;
  do {
    ram[SPRITE_Y + y] = ram[ENEMY_SCREEN_Y];
    ram[SPRITE_TILE + y] = ram[0x0];
    ram[0x0]++;
    ram[SPRITE_ATTR + y] = ram[0x1];
    a = ram[ENEMY_SCREEN_X];
    ram[SPRITE_X + y] = a;
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[ENEMY_SCREEN_X] = a;
    y++;
    y++;
    y++;
    y++;
    x++;
    carry_flag = x >= 0x3;
  } while (!carry_flag);
  x = ram[SLOT];
  enemy_offscreen_bits();
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_OFFSCREEN];
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 9) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 5) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 1) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    return;
  }
  ram[SPRITE_Y + y] = 0xf8;
  return;
}

// in: x; out: x
void run_firework(void) {
  ram[(uint8_t)(ENEMY_Y_SPEED + x)]--; zero_flag = ram[(uint8_t)(ENEMY_Y_SPEED + x)] == 0;
  if (zero_flag) {
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0x8;
    ram[(uint8_t)(ENEMY_X_SPEED + x)]++;
    a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
    carry_flag = a >= 0x3;
    if (carry_flag) { goto loc_d2bd; }
  }
  enemy_screen_position();
  ram[FIREBALL_SCREEN_Y] = ram[ENEMY_SCREEN_Y];
  ram[FIREBALL_SCREEN_X] = ram[ENEMY_SCREEN_X];
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  draw_explosion();
  return;
  
loc_d2bd:
  ram[(uint8_t)(ENEMY_ACTIVE + x)] = 0x0;
  ram[PULSE2_SFX_REQUEST] = 0x8;
  ram[(SCORE_ADD + 4)] = 0x5;
  award_area_points(); return;
}

// in: x; out: x
void run_star_flag(void) {
  ram[FRENZY_ENEMY] = 0x0;
  a = ram[STAR_FLAG_TASK];
  if (a >= 0x5) {
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
  y = 0x5;
  a = ram[(TIME_DIGITS + 2)];
  if (a != 0x1) {
    y = 0x3;
    zero_flag = a == 0x3;
    if (zero_flag) { goto loc_d309; }
    y = 0x0;
    zero_flag = a == 0x6;
    if (zero_flag) { goto loc_d309; }
    a = 0xff;
  }
  
loc_d309:
  ram[FIREWORKS_COUNT] = a;
  ram[(uint8_t)(ENEMY_STATE + x)] = y;
  next_star_flag_task(); // fallthrough
  return;
}

void next_star_flag_task(void) {
  ram[STAR_FLAG_TASK]++;
  star_flag_done(); // fallthrough
  return;
}

void star_flag_done(void) {
  return;
}

// in: none; out: x
void award_time_points(void) {
  a = ram[TIME_DIGITS];
  a |= ram[(TIME_DIGITS + 1)];
  a |= ram[(TIME_DIGITS + 2)]; zero_flag = a == 0;
  if (zero_flag) {
    next_star_flag_task();
    return;
  }
  a = ram[FRAME_COUNT];
  a &= 0x4; zero_flag = a == 0;
  if (!zero_flag) {
    ram[PULSE2_SFX_REQUEST] = 0x10;
  }
  y = 0x23;
  ram[(SCORE_ADD + 5)] = 0xff;
  add_digits();
  ram[(SCORE_ADD + 5)] = 0x5;
  award_area_points(); // fallthrough
  return;
}

// in: none; out: x
void award_area_points(void) {
  y = 0xb;
  a = ram[CURRENT_PLAYER]; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x11;
  }
  add_digits();
  a = ram[CURRENT_PLAYER];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a |= 0x4;
  queue_counters(); return;
}

// in: x; out: x
void raise_star_flag(void) {
  a = ram[(uint8_t)(ENEMY_Y + x)];
  if (a >= 0x72) {
    ram[(uint8_t)(ENEMY_Y + x)]--;
    draw_star_flag(); return;
  }
  a = ram[FIREWORKS_COUNT]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  if (!zero_flag) {
    if (neg_flag) { goto loc_d396; }
    ram[FRENZY_ENEMY] = 0x16;
    draw_star_flag(); // fallthrough
    return;
  }
  
loc_d396:
  draw_star_flag();
  ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = 0x6;
  next_star_flag_task_b(); // fallthrough
  return;
}

// in: x; out: x
void draw_star_flag(void) {
  enemy_screen_position();
  y = ram[ENEMY_SPRITE_SLOT + x];
  x = 0x3;
  do {
    a = ram[ENEMY_SCREEN_Y];
    carry_flag = false;
    a = (uint8_t)(a + ROM(STAR_FLAG_Y_OFFSETS + x) + carry_flag);
    ram[SPRITE_Y + y] = a;
    ram[SPRITE_TILE + y] = ROM(STAR_FLAG_TILES + x);
    ram[SPRITE_ATTR + y] = 0x22;
    a = ram[ENEMY_SCREEN_X];
    carry_flag = false;
    a = (uint8_t)(a + ROM(STAR_FLAG_X_OFFSETS + x) + carry_flag);
    ram[SPRITE_X + y] = a;
    y++;
    y++;
    y++;
    y++;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
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
  a = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[EVENT_MUSIC_PLAYING]; zero_flag = a == 0;
  if (zero_flag) {
    next_star_flag_task_b();
    return;
  }
  return;
}

// in: x; out: none
void move_piranha(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_d40a; }
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[(uint8_t)(ENEMY_X_SPEED + x)]; neg_flag = (a & 0x80) != 0;
      if (!neg_flag) {
        player_enemy_distance();
        if (neg_flag) {
          a = ram[0x0];
          a ^= 0xff;
          carry_flag = false;
          a = (uint8_t)(a + 0x1 + carry_flag);
          ram[0x0] = a;
        }
        a = ram[0x0];
        carry_flag = a >= 0x21;
        if (!carry_flag) { goto loc_d40a; }
      }
      a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
      a ^= 0xff;
      carry_flag = false;
      a = (uint8_t)(a + 0x1 + carry_flag);
      ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
      ram[(uint8_t)(ENEMY_Y_SPEED + x)]++;
    }
    a = ram[ENEMY_Y_FORCE + x];
    y = ram[(uint8_t)(ENEMY_X_SPEED + x)]; neg_flag = (y & 0x80) != 0;
    if (neg_flag) {
      a = ram[ENEMY_Y_FRACTION + x];
    }
    ram[0x0] = a;
    a = ram[FRAME_COUNT];
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (!carry_flag) { goto loc_d40a; }
    a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_d40a; }
    a = ram[(uint8_t)(ENEMY_Y + x)];
    carry_flag = false;
    a = (uint8_t)(a + ram[(uint8_t)(ENEMY_X_SPEED + x)] + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
    zero_flag = a == ram[0x0];
    if (!zero_flag) { goto loc_d40a; }
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0x0;
    ram[(ENEMY_FRAME_TIMER + x) & 0x7ff] = 0x40;
  }
  
loc_d40a:
  ram[ENEMY_SPRITE_ATTR + x] = 0x20;
  return;
}

// in: a, x; out: a
void spin_firebar(void) {
  ram[0x7] = a;
  a = ram[(uint8_t)(TARGET_PAGE + x)]; zero_flag = a == 0;
  if (zero_flag) {
    y = 0x18;
    a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
    carry_flag = false;
    { uint16_t sum = a + ram[0x7] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
    a = (uint8_t)(a + 0x0 + carry_flag);
    return;
  }
  y = 0x8;
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[0x7] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag);
  return;
}

// in: x; out: x
void update_balance_platform(void) {
  a = ram[(uint8_t)(ENEMY_Y_PAGE + x)];
  if (a == 0x3) {
    erase_enemy(); return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    return;
  }
  y = a;
  ram[0x0] = ram[PLATFORM_CONTACT + x];
  a = ram[(uint8_t)(ENEMY_DIR + x)]; zero_flag = a == 0;
  if (!zero_flag) {
  } else {
    a = 0x2d;
    carry_flag = a >= ram[(uint8_t)(ENEMY_Y + x)];
    if (!carry_flag) { goto loc_d462; }
    zero_flag = y == ram[0x0];
    if (!zero_flag) {
      carry_flag = false;
      a = (uint8_t)(a + 0x2 + carry_flag);
      ram[(uint8_t)(ENEMY_Y + x)] = a;
      stop_balance_platforms(); return;
    }
    
loc_d45f:
    goto loc_d598;
    
loc_d462:
    carry_flag = a >= ram[ENEMY_Y + y];
    if (carry_flag) {
      zero_flag = x == ram[0x0];
      if (zero_flag) { goto loc_d45f; }
      carry_flag = false;
      a = (uint8_t)(a + 0x2 + carry_flag);
      ram[ENEMY_Y + y] = a;
      stop_balance_platforms(); return;
    }
    a = ram[(uint8_t)(ENEMY_Y + x)];
    ram[0x100 + sp--] = a;
    a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) {
      a = ram[ENEMY_Y_FORCE + x];
      carry_flag = false;
      { uint16_t sum = a + 0x5 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[0x0] = a;
      a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
      a = (uint8_t)(a + 0x0 + carry_flag); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
      if (neg_flag) { goto loc_d4a4; }
      if (!zero_flag) { goto loc_d498; }
      a = ram[0x0];
      carry_flag = a >= 0xb;
      if (!carry_flag) { goto loc_d49e; }
    } else {
      zero_flag = a == ram[SLOT];
      if (zero_flag) { goto loc_d4a4; }
    }
    
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
    a = ram[0x100 + ++sp];
    carry_flag = true;
    a = (uint8_t)(a - ram[(uint8_t)(ENEMY_Y + x)] - !carry_flag);
    carry_flag = false;
    a = (uint8_t)(a + ram[ENEMY_Y + y] + carry_flag);
    ram[ENEMY_Y + y] = a;
    a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
    if (!neg_flag) {
      x = a;
      place_player_on_vertical_platform();
    }
    y = ram[SLOT];
    a = ram[ENEMY_Y_SPEED + y];
    a |= ram[ENEMY_Y_FORCE + y]; zero_flag = a == 0;
    if (!zero_flag) {
      x = ram[VRAM_LIST1_LENGTH];
      carry_flag = x >= 0x20;
      if (carry_flag) { goto loc_d53e; }
      a = ram[ENEMY_Y_SPEED + y];
      ram[0x100 + sp--] = a;
      ram[0x100 + sp--] = a;
      draw_platform_rope();
      ram[VRAM_LIST1 + x] = ram[0x1];
      ram[(VRAM_LIST1 + 1) + x] = ram[0x0];
      ram[(VRAM_LIST1 + 2) + x] = 0x2;
      a = ram[ENEMY_Y_SPEED + y]; neg_flag = (a & 0x80) != 0;
      if (!neg_flag) {
        ram[(VRAM_LIST1 + 3) + x] = 0xa2;
        ram[(VRAM_LIST1 + 4) + x] = 0xa3;
      } else {
        a = 0x24;
        ram[(VRAM_LIST1 + 3) + x] = a;
        ram[(VRAM_LIST1 + 4) + x] = a;
      }
      a = ram[ENEMY_STATE + y];
      y = a;
      a = ram[0x100 + ++sp];
      a ^= 0xff;
      draw_platform_rope();
      ram[(VRAM_LIST1 + 5) + x] = ram[0x1];
      ram[(VRAM_LIST1 + 6) + x] = ram[0x0];
      ram[(VRAM_LIST1 + 7) + x] = 0x2;
      a = ram[0x100 + ++sp]; neg_flag = (a & 0x80) != 0;
      if (neg_flag) {
        ram[(VRAM_LIST1 + 8) + x] = 0xa2;
        ram[(VRAM_LIST1 + 9) + x] = 0xa3;
      } else {
        a = 0x24;
        ram[(VRAM_LIST1 + 8) + x] = a;
        ram[(VRAM_LIST1 + 9) + x] = a;
      }
      ram[(VRAM_LIST1 + 10) + x] = 0x0;
      a = ram[VRAM_LIST1_LENGTH];
      carry_flag = false;
      a = (uint8_t)(a + 0xa + carry_flag);
      ram[VRAM_LIST1_LENGTH] = a;
    }
    
loc_d53e:
    x = ram[SLOT];
    return;
    
loc_d598:
    a = y;
    x = a;
    enemy_offscreen_bits();
    a = 0x6;
    spawn_floating_score();
    ram[POPUP_X + x] = ram[PLAYER_SCREEN_X];
    ram[POPUP_Y + x] = ram[PLAYER_Y];
    ram[(uint8_t)(ENEMY_DIR + x)] = 0x1;
    stop_balance_platforms(); // fallthrough
    return;
  }
  a = y;
  ram[0x100 + sp--] = a;
  move_falling_platform();
  a = ram[0x100 + ++sp];
  x = a;
  move_falling_platform();
  x = ram[SLOT];
  a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
  if (!neg_flag) {
    x = a;
    place_player_on_vertical_platform();
  }
  x = ram[SLOT];
  return;
}

// in: a, y; out: x
void draw_platform_rope(void) {
  ram[0x100 + sp--] = a;
  a = ram[ENEMY_X + y];
  carry_flag = false;
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  x = ram[HARD_MODE_LATE]; zero_flag = x == 0;
  if (zero_flag) {
    carry_flag = false;
    { uint16_t sum = a + 0x10 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  }
  ram[0x100 + sp--] = a;
  a = ram[ENEMY_PAGE + y];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[0x2] = a;
  a = ram[0x100 + ++sp];
  a &= 0xf0;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a;
  x = ram[(uint8_t)(ENEMY_Y + y)];
  a = ram[0x100 + ++sp]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag);
    x = a;
  }
  a = x;
  x = ram[VRAM_LIST1_LENGTH];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  ram[0x100 + sp--] = a;
  a = (uint8_t)(a << 1 | carry_flag);
  a &= 0x3;
  a |= 0x20;
  ram[0x1] = a;
  a = ram[0x2];
  a &= 0x1;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a |= ram[0x1];
  ram[0x1] = a;
  a = ram[0x100 + ++sp];
  a &= 0xe0;
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  ram[0x0] = a;
  a = ram[ENEMY_Y + y];
  if (a < 0xe8) {
    return;
  }
  a = ram[0x0];
  a &= 0xbf;
  ram[0x0] = a;
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
  a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
  a |= ram[ENEMY_Y_FORCE + x]; zero_flag = a == 0;
  if (zero_flag) {
    ram[ENEMY_Y_FRACTION + x] = a;
    a = ram[(uint8_t)(ENEMY_Y + x)];
    carry_flag = a >= ram[ENEMY_X_FORCE + x];
    if (carry_flag) { goto loc_d5ef; }
    a = ram[FRAME_COUNT];
    a &= 0x7; zero_flag = a == 0;
    if (zero_flag) {
      ram[(uint8_t)(ENEMY_Y + x)]++;
    }
    carry_player_vertically(); return;
  }
  
loc_d5ef:
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = a >= ram[(uint8_t)(ENEMY_X_SPEED + x)];
  if (carry_flag) {
    move_platform_up();
    carry_player_vertically(); return;
  }
  move_platform_down();
  carry_player_vertically(); // fallthrough
  return;
}

// in: x; out: none
void carry_player_vertically(void) {
  a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
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
  a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    return;
  }
  carry_player_horizontally(); // fallthrough
  return;
}

// in: x; out: none
void carry_player_horizontally(void) {
  a = ram[PLAYER_X];
  carry_flag = false;
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[PLAYER_X] = a;
  a = ram[PLAYER_PAGE];
  y = ram[0x0]; neg_flag = (y & 0x80) != 0;
  if (!neg_flag) {
    a = (uint8_t)(a + 0x0 + carry_flag);
  } else {
    a = (uint8_t)(a - 0x0 - !carry_flag);
  }
  ram[PLAYER_PAGE] = a;
  ram[PLATFORM_X_SCROLL] = y;
  place_player_on_vertical_platform();
  return;
}

// in: x; out: x
void drop_platform(void) {
  a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    return;
  }
  move_drop_platform();
  place_player_on_vertical_platform();
  return;
}

// in: x; out: x
void right_platform(void) {
  move_enemy_x();
  ram[0x0] = a;
  a = ram[PLATFORM_CONTACT + x]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
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
  a = ram[PLATFORM_CONTACT + x]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  place_player_on_small_platform();
  return;
}

// in: x; out: none
void move_lift(void) {
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[ENEMY_Y_FRACTION + x];
  carry_flag = false;
  { uint16_t sum = a + ram[ENEMY_Y_FORCE + x] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[ENEMY_Y_FRACTION + x] = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  a = (uint8_t)(a + ram[(uint8_t)(ENEMY_Y_SPEED + x)] + carry_flag);
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  return;
}

// in: x; out: none
void check_enemy_offscreen(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x14) {
    return;
  }
  a = ram[SCREEN_LEFT_X];
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  { uint8_t m = 0x5; carry_flag = y >= m; zero_flag = y == m; }
  if (!zero_flag) {
    { uint8_t m = 0xd; carry_flag = y >= m; zero_flag = y == m; }
    if (!zero_flag) { goto loc_d68f; }
  }
  { uint16_t sum = a + 0x38 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  
loc_d68f:
  { uint16_t diff = (uint16_t)(a - 0x48 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[0x1] = a;
  a = ram[SCREEN_LEFT_PAGE];
  { uint16_t diff = (uint16_t)(a - 0x0 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[0x0] = a;
  a = ram[SCREEN_RIGHT_X];
  { uint16_t sum = a + 0x48 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x3] = a;
  a = ram[SCREEN_RIGHT_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[0x2] = a;
  a = ram[(uint8_t)(ENEMY_X + x)];
  carry_flag = a >= ram[0x1];
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  a = (uint8_t)(a - ram[0x0] - !carry_flag); neg_flag = (a & 0x80) != 0;
  if (!neg_flag) {
    a = ram[(uint8_t)(ENEMY_X + x)];
    carry_flag = a >= ram[0x3];
    a = ram[(uint8_t)(ENEMY_PAGE + x)];
    a = (uint8_t)(a - ram[0x2] - !carry_flag); neg_flag = (a & 0x80) != 0;
    if (neg_flag) {
      return;
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    if (a == 0x5) {
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
  a = ram[(uint8_t)(FIREBALL_STATE + x)]; zero_flag = a == 0;
  if (!zero_flag) {
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) { goto loc_d733; }
    a = ram[FRAME_COUNT];
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_d733; }
    a = x;
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    carry_flag = false;
    a = (uint8_t)(a + 0x1c + carry_flag);
    y = a;
    x = 0x4;
    do {
      ram[0x1] = x;
      a = y;
      ram[0x100 + sp--] = a;
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      a &= 0x20; zero_flag = a == 0;
      if (zero_flag) {
        a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
        if (zero_flag) { goto loc_d72c; }
        a = ram[(uint8_t)(ENEMY_KIND + x)];
        if (a >= 0x24) {
          carry_flag = a >= 0x2b;
          if (!carry_flag) { goto loc_d72c; }
        }
        if (a == 0x6) {
          a = ram[(uint8_t)(ENEMY_STATE + x)];
          carry_flag = a >= 0x2;
          if (carry_flag) { goto loc_d72c; }
        }
        a = ram[ENEMY_OFFSCREEN_MASKED + x]; zero_flag = a == 0;
        if (!zero_flag) { goto loc_d72c; }
        a = x;
        a = (uint8_t)(a << 1);
        a = (uint8_t)(a << 1);
        carry_flag = false;
        a = (uint8_t)(a + 0x4 + carry_flag);
        x = a;
        objects_collide();
        x = ram[SLOT];
        if (!carry_flag) { goto loc_d72c; }
        ram[(uint8_t)(FIREBALL_STATE + x)] = 0x80;
        x = ram[0x1];
        fireball_hit_enemy();
      }
      
loc_d72c:
      a = ram[0x100 + ++sp];
      y = a;
      x = ram[0x1];
      x--; neg_flag = (x & 0x80) != 0;
    } while (!neg_flag);
  }
  
loc_d733:
  x = ram[SLOT];
  return;
}

// in: x; out: none
void fireball_hit_enemy(void) {
  enemy_screen_position();
  x = ram[0x1];
  a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    a &= 0xf;
    x = a;
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    zero_flag = a == 0x2d;
    if (zero_flag) { goto loc_d75c; }
    x = ram[0x1];
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x2) {
    return;
  }
  zero_flag = a == 0x2d;
  if (!zero_flag) { goto loc_d789; }
  
loc_d75c:
  ram[BOWSER_HEALTH]--; zero_flag = ram[BOWSER_HEALTH] == 0;
  if (!zero_flag) {
    return;
  }
  clear_vertical_motion();
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  ram[FRENZY_ENEMY] = a;
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfe;
  y = ram[WORLD_NUMBER];
  ram[(uint8_t)(ENEMY_KIND + x)] = ROM(BOWSER_LOOKALIKES + y);
  a = 0x20;
  if (y < 0x3) {
    a |= 0x3;
  }
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  ram[PULSE2_SFX_REQUEST] = 0x80;
  x = ram[0x1];
  a = 0x9;
  smack_enemy_score(); return;
  
loc_d789:
  if (a == 0x8) {
    return;
  }
  if (a == 0xc) {
    return;
  }
  if (a >= 0x15) {
    return;
  }
  defeat_enemy(); // fallthrough
  return;
}

// in: x; out: y
void defeat_enemy(void) {
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  { uint8_t m = 0xd; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    a = ram[(uint8_t)(ENEMY_Y + x)];
    a = (uint8_t)(a + 0x18 + carry_flag);
    ram[(uint8_t)(ENEMY_Y + x)] = a;
  }
  check_stun_enemy();
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x1f;
  a |= 0x20;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  a = 0x2;
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  if (y == 0x5) {
    a = 0x6;
  }
  if (y != 0x6) {
    smack_enemy_score();
    return;
  }
  a = 0x1;
  smack_enemy_score(); // fallthrough
  return;
}

// in: a, x; out: none
void smack_enemy_score(void) {
  spawn_floating_score();
  ram[PULSE1_SFX_REQUEST] = 0x8;
  return;
}

// in: x; out: x
void player_hammer_collision(void) {
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    return;
  }
  a = ram[TIMERS_PAUSED];
  a |= ram[MISC_OFFSCREEN]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = x;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + 0x24 + carry_flag);
  y = a;
  player_collides();
  x = ram[SLOT];
  if (carry_flag) {
    a = ram[MISC_CONTACT + x]; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
    ram[MISC_CONTACT + x] = 0x1;
    a = ram[(uint8_t)(MISC_X_SPEED + x)];
    a ^= 0xff;
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(MISC_X_SPEED + x)] = a;
    a = ram[STAR_TIMER]; zero_flag = a == 0;
    if (!zero_flag) {
      return;
    }
    injure_player(); return;
  }
  ram[MISC_CONTACT + x] = 0x0;
  return;
}

// in: x; out: x
void player_enemy_collision(void) {
  goto player_enemy_collision;
  
loc_d800:
  erase_enemy();
  a = 0x6;
  spawn_floating_score();
  ram[PULSE2_SFX_REQUEST] = 0x20;
  a = ram[POWERUP_KIND];
  if (a >= 0x2) {
    zero_flag = a == 0x3;
    if (zero_flag) { goto loc_d83a; }
    ram[STAR_TIMER] = 0x23;
    ram[AREA_MUSIC_REQUEST] = 0x40;
    return;
  }
  a = ram[PLAYER_POWER]; zero_flag = a == 0;
  if (zero_flag) { goto loc_d840; }
  if (a != 0x1) {
    return;
  }
  x = ram[SLOT];
  ram[PLAYER_POWER] = 0x2;
  queue_player_colors();
  x = ram[SLOT];
  a = 0xc;
  goto loc_d847;
  
loc_d83a:
  ram[POPUP_KIND + x] = 0xb;
  return;
  
loc_d840:
  ram[PLAYER_POWER] = 0x1;
  a = 0x9;
  
loc_d847:
  y = 0x0;
  set_player_routine();
  return;
  
player_enemy_collision:
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) {
    return;
  }
  check_player_vertical();
  if (carry_flag) {
    return;
  }
  a = ram[ENEMY_OFFSCREEN_MASKED + x]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[PLAYER_TASK];
  if (a != 0x8) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  enemy_box_offset();
  player_collides();
  x = ram[SLOT];
  if (!carry_flag) {
    a = ram[ENEMY_COLLISION + x];
    a &= 0xfe;
    ram[ENEMY_COLLISION + x] = a;
    return;
  }
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  if (y == 0x2e) {
    goto loc_d800;
  }
  a = ram[STAR_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    defeat_enemy(); return;
  }
  a = ram[ENEMY_COLLISION + x];
  a &= 0x1;
  a |= ram[ENEMY_OFFSCREEN_MASKED + x]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = 0x1;
  a |= ram[ENEMY_COLLISION + x];
  ram[ENEMY_COLLISION + x] = a;
  if (y != 0x12) {
    if (y == 0xd) {
      injure_player();
      return;
    }
    if (y == 0xc) {
      injure_player();
      return;
    }
    zero_flag = y == 0x33;
    if (zero_flag) { goto loc_d8f9; }
    if (y >= 0x15) {
      injure_player();
      return;
    }
    a = ram[AREA_KIND]; zero_flag = a == 0;
    if (zero_flag) {
      injure_player();
      return;
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) { goto loc_d8f9; }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    a &= 0x7;
    carry_flag = a >= 0x2;
    if (!carry_flag) { goto loc_d8f9; }
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    if (a == 0x6) {
      return;
    }
    ram[PULSE1_SFX_REQUEST] = 0x8;
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    a |= 0x80;
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    face_player();
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(KICKED_SHELL_SPEEDS + y);
    a = 0x3;
    carry_flag = false;
    a = (uint8_t)(a + ram[STOMP_CHAIN] + carry_flag);
    y = ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff];
    if (y < 0x3) {
      a = ROM(KICKED_SHELL_POINTS + y);
    }
    spawn_floating_score();
    return;
  }
  
loc_d8f9:
  a = ram[PLAYER_Y_SPEED]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  if (!neg_flag) {
    if (!zero_flag) { goto loc_d969; }
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a >= 0x7) {
    a = ram[PLAYER_Y];
    carry_flag = false;
    a = (uint8_t)(a + 0xc + carry_flag);
    carry_flag = a >= ram[(uint8_t)(ENEMY_Y + x)];
    if (!carry_flag) { goto loc_d969; }
  }
  a = ram[STOMP_TIMER]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[INJURY_TIMER]; zero_flag = a == 0;
    if (!zero_flag) {
      end_injury_check();
      return;
    }
    a = ram[PLAYER_SCREEN_X];
    carry_flag = a >= ram[ENEMY_SCREEN_X];
    if (carry_flag) {
      goto loc_d9f6;
    }
    a = ram[(uint8_t)(ENEMY_DIR + x)];
    if (a != 0x1) {
      injure_player();
      return;
    }
    goto loc_d9ff;
  }
  
loc_d969:
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x12) {
    injure_player();
    return;
  }
  ram[PULSE1_SFX_REQUEST] = 0x4;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  y = 0x0;
  if (a != 0x14) {
    zero_flag = a == 0x8;
    if (zero_flag) { goto loc_d996; }
    zero_flag = a == 0x33;
    if (zero_flag) { goto loc_d996; }
    zero_flag = a == 0xc;
    if (zero_flag) { goto loc_d996; }
    y++;
    zero_flag = a == 0x5;
    if (zero_flag) { goto loc_d996; }
    y++;
    zero_flag = a == 0x11;
    if (zero_flag) { goto loc_d996; }
    y++;
    zero_flag = a == 0x7;
    if (!zero_flag) { goto loc_d9b3; }
  }
  
loc_d996:
  a = ROM(STOMP_POINTS + y);
  spawn_floating_score();
  a = ram[(uint8_t)(ENEMY_DIR + x)];
  ram[0x100 + sp--] = a;
  stun_enemy();
  a = ram[0x100 + ++sp];
  ram[(uint8_t)(ENEMY_DIR + x)] = a;
  ram[(uint8_t)(ENEMY_STATE + x)] = 0x20;
  clear_vertical_motion();
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = a;
  ram[PLAYER_Y_SPEED] = 0xfd;
  return;
  
loc_d9b3:
  if (a >= 0x9) {
    a &= 0x1;
    ram[(uint8_t)(ENEMY_KIND + x)] = a;
    y = 0x0;
    ram[(uint8_t)(ENEMY_STATE + x)] = y;
    a = 0x3;
    spawn_floating_score();
    clear_vertical_motion();
    face_player();
    ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(DEMOTED_KOOPA_SPEEDS + y);
  } else {
    ram[(uint8_t)(ENEMY_STATE + x)] = 0x4;
    ram[STOMP_CHAIN]++;
    a = ram[STOMP_CHAIN];
    carry_flag = false;
    a = (uint8_t)(a + ram[STOMP_TIMER] + carry_flag);
    spawn_floating_score();
    ram[STOMP_TIMER]++;
    y = ram[HARD_MODE];
    ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = ROM(REVIVAL_RATES + y);
  }
  ram[PLAYER_Y_SPEED] = 0xfc;
  return;
  
loc_d9f6:
  a = ram[(uint8_t)(ENEMY_DIR + x)];
  if (a == 0x1) {
    injure_player(); return;
  }
  
loc_d9ff:
  turn_enemy_around();
  injure_player(); return;
}

// in: none; out: x
void injure_player(void) {
  a = ram[INJURY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    end_injury_check();
    return;
  }
  force_injury(); // fallthrough
  return;
}

// in: a; out: x
void force_injury(void) {
  x = ram[PLAYER_POWER]; zero_flag = x == 0;
  if (zero_flag) { goto loc_d958; }
  ram[PLAYER_POWER] = a;
  a = 0x8;
  ram[INJURY_TIMER] = a;
  a = (uint8_t)(a << 1);
  ram[PULSE1_SFX_REQUEST] = a;
  queue_player_colors();
  a = 0xa;
  
loc_d946:
  y = 0x1;
  set_player_routine(); // fallthrough
  return;
  
loc_d958:
  ram[PLAYER_X_SPEED] = x;
  x++;
  ram[EVENT_MUSIC_REQUEST] = x;
  ram[PLAYER_Y_SPEED] = 0xfc;
  a = 0xb;
  goto loc_d946;
}

// in: a, y; out: x
void set_player_routine(void) {
  ram[PLAYER_TASK] = a;
  ram[PLAYER_STATE] = y;
  y = 0xff;
  ram[TIMERS_PAUSED] = y;
  y++;
  ram[SCROLL_AMOUNT] = y;
  end_injury_check(); // fallthrough
  return;
}

// in: none; out: x
void end_injury_check(void) {
  x = ram[SLOT];
  return;
}

// in: x; out: y
void face_player(void) {
  y = 0x1;
  player_enemy_distance();
  if (neg_flag) {
    y++;
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
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    return;
  }
  a = ram[AREA_KIND]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a < 0x15) {
    zero_flag = a == 0x11;
    if (zero_flag) { goto loc_dab1; }
    zero_flag = a == 0xd;
    if (zero_flag) { goto loc_dab1; }
    a = ram[ENEMY_OFFSCREEN_MASKED + x]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_dab1; }
    enemy_box_offset();
    x--; neg_flag = (x & 0x80) != 0;
    if (neg_flag) { goto loc_dab1; }
    do {
      ram[0x1] = x;
      a = y;
      ram[0x100 + sp--] = a;
      a = ram[(uint8_t)(ENEMY_ACTIVE + x)]; zero_flag = a == 0;
      if (!zero_flag) {
        a = ram[(uint8_t)(ENEMY_KIND + x)];
        carry_flag = a >= 0x15;
        if (carry_flag) { goto loc_daaa; }
        zero_flag = a == 0x11;
        if (zero_flag) { goto loc_daaa; }
        zero_flag = a == 0xd;
        if (zero_flag) { goto loc_daaa; }
        a = ram[ENEMY_OFFSCREEN_MASKED + x]; zero_flag = a == 0;
        if (!zero_flag) { goto loc_daaa; }
        a = x;
        a = (uint8_t)(a << 1);
        a = (uint8_t)(a << 1);
        carry_flag = false;
        a = (uint8_t)(a + 0x4 + carry_flag);
        x = a;
        objects_collide();
        x = ram[SLOT];
        y = ram[0x1];
        if (carry_flag) {
          a = ram[(uint8_t)(ENEMY_STATE + x)];
          a |= ram[ENEMY_STATE + y];
          a &= 0x80; zero_flag = a == 0;
          if (zero_flag) {
            a = ram[ENEMY_COLLISION + y];
            a &= ROM(SET_BIT_MASKS + x); zero_flag = a == 0;
            if (!zero_flag) { goto loc_daaa; }
            a = ram[ENEMY_COLLISION + y];
            a |= ROM(SET_BIT_MASKS + x);
            ram[ENEMY_COLLISION + y] = a;
          }
          enemy_pair_collision();
          goto loc_daaa;
        }
        a = ram[ENEMY_COLLISION + y];
        a &= ROM(CLEAR_BIT_MASKS + x);
        ram[ENEMY_COLLISION + y] = a;
      }
      
loc_daaa:
      a = ram[0x100 + ++sp];
      y = a;
      x = ram[0x1];
      x--; neg_flag = (x & 0x80) != 0;
    } while (!neg_flag);
  }
  
loc_dab1:
  x = ram[SLOT];
  return;
}

// in: x, y; out: none
void enemy_pair_collision(void) {
  a = ram[ENEMY_STATE + y];
  a |= ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  if (a >= 0x6) {
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    if (a == 0x5) {
      return;
    }
    a = ram[ENEMY_STATE + y];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) {
      a = 0x6;
      spawn_floating_score();
      defeat_enemy();
      y = ram[0x1];
    }
    a = y;
    x = a;
    defeat_enemy();
    x = ram[SLOT];
    a = ram[SHELL_CHAIN + x];
    carry_flag = false;
    a = (uint8_t)(a + 0x4 + carry_flag);
    x = ram[0x1];
    spawn_floating_score();
    x = ram[SLOT];
    ram[SHELL_CHAIN + x]++;
    return;
  }
  a = ram[ENEMY_STATE + y];
  if (a >= 0x6) {
    a = ram[ENEMY_KIND + y];
    if (a == 0x5) {
      return;
    }
    defeat_enemy();
    y = ram[0x1];
    a = ram[SHELL_CHAIN + y];
    carry_flag = false;
    a = (uint8_t)(a + 0x4 + carry_flag);
    x = ram[SLOT];
    spawn_floating_score();
    x = ram[0x1];
    ram[SHELL_CHAIN + x]++;
    return;
  }
  a = y;
  x = a;
  turn_enemy_around();
  x = ram[SLOT];
  turn_enemy_around(); // fallthrough
  return;
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
  if (a == 0x5) {
    return;
  }
  if (a == 0x12) {
    reverse_enemy();
    return;
  }
  if (a == 0xe) {
    reverse_enemy();
    return;
  }
  if (a >= 0x7) {
    return;
  }
  reverse_enemy(); // fallthrough
  return;
}

// in: x; out: y
void reverse_enemy(void) {
  a = ram[(uint8_t)(ENEMY_X_SPEED + x)];
  a ^= 0xff;
  y = a;
  y++;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = y;
  a = ram[(uint8_t)(ENEMY_DIR + x)];
  a ^= 0x3;
  ram[(uint8_t)(ENEMY_DIR + x)] = a;
  return;
}

// in: x; out: x
void large_platform_collision(void) {
  ram[PLATFORM_CONTACT + x] = 0xff;
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (!zero_flag) {
    large_platform_collision_done();
    return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    large_platform_collision_done();
    return;
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x24) {
    player_on_large_platform();
    return;
  }
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  x = a;
  player_on_large_platform();
  player_on_large_platform(); // fallthrough
  return;
}

// in: x; out: x
void player_on_large_platform(void) {
  check_player_vertical();
  if (carry_flag) {
    large_platform_collision_done();
    return;
  }
  a = x;
  enemy_box_offset_for();
  ram[0x0] = ram[(uint8_t)(ENEMY_Y + x)];
  a = x;
  ram[0x100 + sp--] = a;
  player_collides();
  a = ram[0x100 + ++sp];
  x = a;
  if (!carry_flag) {
    large_platform_collision_done();
    return;
  }
  platform_landing();
  large_platform_collision_done(); // fallthrough
  return;
}

// in: none; out: x
void large_platform_collision_done(void) {
  x = ram[SLOT];
  return;
}

// in: x; out: x
void small_platform_collision(void) {
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    ram[PLATFORM_CONTACT + x] = a;
    check_player_vertical();
    if (carry_flag) { goto loc_dbb7; }
    ram[0x0] = 0x2;
    do {
      x = ram[SLOT];
      enemy_box_offset();
      a &= 0x2; zero_flag = a == 0;
      if (!zero_flag) { goto loc_dbb7; }
      a = ram[BOX_TOP + y];
      if (a >= 0x20) {
        player_collides();
        if (carry_flag) { goto loc_dbba; }
      }
      a = ram[BOX_TOP + y];
      carry_flag = false;
      a = (uint8_t)(a + 0x80 + carry_flag);
      ram[BOX_TOP + y] = a;
      a = ram[BOX_BOTTOM + y];
      carry_flag = false;
      a = (uint8_t)(a + 0x80 + carry_flag);
      ram[BOX_BOTTOM + y] = a;
      ram[0x0]--; zero_flag = ram[0x0] == 0;
    } while (!zero_flag);
  }
  
loc_dbb7:
  x = ram[SLOT];
  return;
  
loc_dbba:
  x = ram[SLOT];
  platform_landing(); // fallthrough
  return;
}

// in: x, y; out: x
void platform_landing(void) {
  a = ram[BOX_BOTTOM + y];
  carry_flag = true;
  a = (uint8_t)(a - ram[BOX_TOP] - !carry_flag);
  if (a < 0x4) {
    a = ram[PLAYER_Y_SPEED]; neg_flag = (a & 0x80) != 0;
    if (!neg_flag) { goto loc_dbcf; }
    ram[PLAYER_Y_SPEED] = 0x1;
  }
  
loc_dbcf:
  a = ram[BOX_BOTTOM];
  carry_flag = true;
  a = (uint8_t)(a - ram[BOX_TOP + y] - !carry_flag);
  if (a < 0x6) {
    a = ram[PLAYER_Y_SPEED]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) { goto loc_dbf5; }
    a = ram[0x0];
    y = ram[(uint8_t)(ENEMY_KIND + x)];
    if (y != 0x2b) {
      zero_flag = y == 0x2c;
      if (zero_flag) { goto loc_dbeb; }
      a = x;
    }
    
loc_dbeb:
    x = ram[SLOT];
    ram[PLATFORM_CONTACT + x] = a;
    ram[PLAYER_STATE] = 0x0;
    return;
  }
  
loc_dbf5:
  ram[0x0] = 0x1;
  a = ram[BOX_RIGHT];
  carry_flag = true;
  a = (uint8_t)(a - ram[BOXES + y] - !carry_flag);
  if (a >= 0x8) {
    ram[0x0]++;
    a = ram[BOX_RIGHT + y];
    carry_flag = false;
    a = (uint8_t)(a - ram[BOX_LEFT] - !carry_flag);
    carry_flag = a >= 0x9;
    if (carry_flag) { goto loc_dc14; }
  }
  block_player_side();
  
loc_dc14:
  x = ram[SLOT];
  return;
}

// in: a, x; out: none
void place_player_on_small_platform(void) {
  y = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + ROM((SMALL_PLATFORM_Y_OFFSETS - 1) + y) + carry_flag);
  
  place_player_on_platform_y(); return;
}

// in: x; out: none
void place_player_on_vertical_platform(void) {
  a = ram[(uint8_t)(ENEMY_Y + x)];
  place_player_on_platform_y(); // fallthrough
  return;
}

// in: a, x; out: none
void place_player_on_platform_y(void) {
  y = ram[PLAYER_TASK];
  if (y == 0xb) {
    return;
  }
  y = ram[(uint8_t)(ENEMY_Y_PAGE + x)];
  if (y != 0x1) {
    return;
  }
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - 0x20 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[PLAYER_Y] = a;
  a = y;
  a = (uint8_t)(a - 0x0 - !carry_flag);
  ram[PLAYER_Y_PAGE] = a;
  a = 0x0;
  ram[PLAYER_Y_SPEED] = a;
  ram[PLAYER_Y_FORCE] = a;
  return;
}

// in: none; out: carry
void check_player_vertical(void) {
  a = ram[PLAYER_OFFSCREEN];
  carry_flag = a >= 0xf0;
  if (carry_flag) {
    return;
  }
  y = ram[PLAYER_Y_PAGE];
  y--; zero_flag = y == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[PLAYER_Y];
  carry_flag = a >= 0xd0;
  return;
}

// in: none; out: a, y
void enemy_box_offset(void) {
  a = ram[SLOT];
  enemy_box_offset_for(); // fallthrough
  return;
}

// in: a; out: a, y
void enemy_box_offset_for(void) {
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + 0x4 + carry_flag);
  y = a;
  a = ram[ENEMY_OFFSCREEN];
  a &= 0xf;
  
  return;
}

void player_bg_collision(void) {
  a = ram[COLLISIONS_OFF]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[PLAYER_TASK];
  if (a == 0xb) {
    return;
  }
  if (a < 0x4) {
    return;
  }
  a = 0x1;
  y = ram[SWIMMING]; zero_flag = y == 0;
  if (zero_flag) {
    a = ram[PLAYER_STATE]; zero_flag = a == 0;
    if (!zero_flag) {
      zero_flag = a == 0x3;
      if (!zero_flag) { goto loc_dc86; }
    }
    a = 0x2;
  }
  ram[PLAYER_STATE] = a;
  
loc_dc86:
  a = ram[PLAYER_Y_PAGE];
  if (a != 0x1) {
    return;
  }
  ram[PLAYER_COLLISION] = 0xff;
  a = ram[PLAYER_Y];
  if (a >= 0xcf) {
    return;
  }
  y = 0x2;
  a = ram[CROUCHING]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[PLAYER_SMALL]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_dcab; }
    y--;
    a = ram[SWIMMING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_dcab; }
    y--;
  }
  
loc_dcab:
  a = ROM(BLOCK_PROBE_STARTS + y);
  ram[(ENEMY_DATA_PTR + 2)] = a;
  y = a;
  x = ram[PLAYER_SMALL];
  a = ram[CROUCHING]; zero_flag = a == 0;
  if (!zero_flag) {
    x++;
  }
  a = ram[PLAYER_Y];
  carry_flag = a >= ROM(PLAYER_BG_TOP_EXTENTS + x);
  if (carry_flag) {
    block_probe_head();
    if (zero_flag) { goto loc_dcf6; }
    is_coin_metatile();
    if (carry_flag) { goto loc_dd1a; }
    y = ram[PLAYER_Y_SPEED]; neg_flag = (y & 0x80) != 0;
    if (!neg_flag) { goto loc_dcf6; }
    y = ram[0x4];
    carry_flag = y >= 0x4;
    if (!carry_flag) { goto loc_dcf6; }
    is_solid_metatile();
    if (!carry_flag) {
      y = ram[AREA_KIND]; zero_flag = y == 0;
      if (zero_flag) { goto loc_dcf2; }
      y = ram[BLOCK_BOUNCE_TIMER]; zero_flag = y == 0;
      if (!zero_flag) { goto loc_dcf2; }
      bump_head();
      goto loc_dcf6;
    }
    if (a != 0x26) {
      ram[PULSE1_SFX_REQUEST] = 0x2;
    }
    
loc_dcf2:
    ram[PLAYER_Y_SPEED] = 0x1;
  }
  
loc_dcf6:
  y = ram[(ENEMY_DATA_PTR + 2)];
  a = ram[PLAYER_Y];
  carry_flag = a >= 0xcf;
  if (carry_flag) { goto loc_dd5e; }
  block_probe_feet();
  is_coin_metatile();
  if (!carry_flag) {
    ram[0x100 + sp--] = a;
    block_probe_feet();
    ram[0x0] = a;
    a = ram[0x100 + ++sp]; zero_flag = a == 0;
    ram[0x1] = a;
    if (!zero_flag) { goto loc_dd1d; }
    a = ram[0x0]; zero_flag = a == 0;
    if (zero_flag) { goto loc_dd5e; }
    is_coin_metatile();
    if (!carry_flag) { goto loc_dd1d; }
  }
  
loc_dd1a:
  goto loc_de05;
  
loc_dd1d:
  is_climb_metatile();
  if (!carry_flag) {
    y = ram[PLAYER_Y_SPEED]; neg_flag = (y & 0x80) != 0;
    if (neg_flag) { goto loc_dd5e; }
    if (a == 0xc5) {
      goto loc_de0e;
    }
    is_hidden_block();
    if (zero_flag) { goto loc_dd5e; }
    y = ram[SPRING_ANIM_STEP]; zero_flag = y == 0;
    if (zero_flag) {
      y = ram[0x4];
      if (y >= 0x5) {
        ram[0x0] = ram[PLAYER_DIR];
        block_player_side(); return;
      }
      check_spring_landing();
      a = 0xf0;
      a &= ram[PLAYER_Y];
      ram[PLAYER_Y] = a;
      enter_pipe();
      a = 0x0;
      ram[PLAYER_Y_SPEED] = a;
      ram[PLAYER_Y_FORCE] = a;
      ram[STOMP_CHAIN] = a;
    }
    ram[PLAYER_STATE] = 0x0;
  }
  
loc_dd5e:
  y = ram[(ENEMY_DATA_PTR + 2)];
  y++;
  y++;
  ram[0x0] = 0x2;
  do {
    y++;
    ram[(ENEMY_DATA_PTR + 2)] = y;
    a = ram[PLAYER_Y];
    if (a >= 0x20) {
      if (a >= 0xe4) {
        return;
      }
      block_probe_side();
      if (zero_flag) { goto loc_dd85; }
      zero_flag = a == 0x1c;
      if (zero_flag) { goto loc_dd85; }
      zero_flag = a == 0x6b;
      if (zero_flag) { goto loc_dd85; }
      is_climb_metatile();
      if (!carry_flag) { goto loc_dd9c; }
    }
    
loc_dd85:
    y = ram[(ENEMY_DATA_PTR + 2)];
    y++;
    a = ram[PLAYER_Y];
    if (a < 0x8) {
      return;
    }
    if (a >= 0xd0) {
      return;
    }
    block_probe_side();
    if (!zero_flag) { goto loc_dd9c; }
    ram[0x0]--; zero_flag = ram[0x0] == 0;
  } while (!zero_flag);
  return;
  
loc_dd9c:
  is_hidden_block();
  if (zero_flag) {
    return;
  }
  is_climb_metatile();
  if (carry_flag) {
    goto loc_de2e;
  }
  is_coin_metatile();
  if (!carry_flag) {
    is_spring_metatile();
    if (carry_flag) {
      a = ram[SPRING_ANIM_STEP]; zero_flag = a == 0;
      if (!zero_flag) {
        return;
      }
    } else {
      y = ram[PLAYER_STATE];
      zero_flag = y == 0x0;
      if (!zero_flag) { goto loc_ddff; }
      y = ram[PLAYER_FACING];
      y--; zero_flag = y == 0;
      if (!zero_flag) { goto loc_ddff; }
      if (a != 0x6c) {
        zero_flag = a == 0x1f;
        if (!zero_flag) { goto loc_ddff; }
      }
      a = ram[PLAYER_SPRITE_ATTR]; zero_flag = a == 0;
      if (zero_flag) {
        ram[PULSE1_SFX_REQUEST] = 0x10;
      }
      a |= 0x20;
      ram[PLAYER_SPRITE_ATTR] = a;
      a = ram[PLAYER_X];
      a &= 0xf; zero_flag = a == 0;
      if (!zero_flag) {
        y = 0x0;
        a = ram[SCREEN_LEFT_PAGE]; zero_flag = a == 0;
        if (!zero_flag) {
          y++;
        }
        ram[CHANGE_AREA_TIMER] = ROM(AREA_CHANGE_TIMERS + y);
      }
      a = ram[PLAYER_TASK];
      if (a == 0x7) {
        return;
      }
      if (a != 0x8) {
        return;
      }
      ram[PLAYER_TASK] = 0x2;
      return;
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
  ram[GAME_MODE_TASK] = 0x0;
  ram[GAME_MODE] = 0x2;
  ram[PLAYER_X_SPEED] = 0x18;
  erase_metatile(); // fallthrough
  return;
  
loc_de2e:
  y = ram[0x4];
  if (y < 0x6) {
    return;
  }
  if (y >= 0xa) {
    return;
  }
  if (a != 0x24) {
    zero_flag = a == 0x25;
    if (!zero_flag) { goto loc_de7a; }
  }
  a = ram[PLAYER_TASK];
  zero_flag = a == 0x5;
  if (zero_flag) { goto loc_de88; }
  ram[PLAYER_FACING] = 0x1;
  ram[SCROLL_LOCKED]++;
  a = ram[PLAYER_TASK];
  if (a != 0x4) {
    a = 0x33;
    remove_enemies();
    a = 0x80;
    ram[EVENT_MUSIC_REQUEST] = a;
    a = a >> 1;
    ram[FLAGPOLE_SOUND] = a;
    x = 0x4;
    a = ram[PLAYER_Y];
    ram[FLAGPOLE_CONTACT_Y] = a;
    do {
      carry_flag = a >= ROM(FLAGPOLE_HEIGHTS + x);
      if (carry_flag) { goto loc_de70; }
      x--; zero_flag = x == 0;
    } while (!zero_flag);
    
loc_de70:
    ram[FLAG_SCORE_KIND] = x;
  }
  ram[PLAYER_TASK] = 0x4;
  goto loc_de88;
  
loc_de7a:
  if (a == 0x26) {
    a = ram[PLAYER_Y];
    carry_flag = a >= 0x20;
    if (carry_flag) { goto loc_de88; }
    ram[PLAYER_TASK] = 0x1;
  }
  
loc_de88:
  ram[PLAYER_STATE] = 0x3;
  a = 0x0;
  ram[PLAYER_X_SPEED] = a;
  ram[PLAYER_X_FORCE] = a;
  a = ram[PLAYER_X];
  carry_flag = true;
  a = (uint8_t)(a - ram[SCREEN_LEFT_X] - !carry_flag);
  if (a < 0x10) {
    ram[PLAYER_FACING] = 0x2;
  }
  y = ram[PLAYER_FACING];
  a = ram[0x6];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + ROM((CLIMB_X_OFFSETS - 1) + y) + carry_flag);
  ram[PLAYER_X] = a;
  a = ram[0x6]; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[SCREEN_RIGHT_PAGE];
  carry_flag = false;
  a = (uint8_t)(a + ROM((CLIMB_X_OFFSETS + 1) + y) + carry_flag);
  ram[PLAYER_PAGE] = a;
  return;
}

void erase_metatile(void) {
  y = ram[0x2];
  dynamic_ram_write(indirect_y_addr(0x6), 0x0);
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
  a = 0x3;
  ram[SPRING_TIMER] = a;
  a = a >> 1;
  ram[SPRING_ANIM_STEP] = a;
  return;
}

// in: a; out: carry
void is_spring_metatile(void) {
  if (a != 0x67) {
    zero_flag = a == 0x68;
    carry_flag = false;
    if (!zero_flag) {
      return;
    }
  }
  carry_flag = true;
  return;
}

void enter_pipe(void) {
  a = ram[BUTTONS_VERTICAL];
  a &= 0x4; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = ram[0x0];
  if (a != 0x11) {
    return;
  }
  a = ram[0x1];
  if (a != 0x10) {
    return;
  }
  ram[CHANGE_AREA_TIMER] = 0x30;
  ram[PLAYER_TASK] = 0x3;
  ram[PULSE1_SFX_REQUEST] = 0x10;
  ram[PLAYER_SPRITE_ATTR] = 0x20;
  a = ram[WARP_ZONE]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a &= 0x3;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  x = a;
  a = ram[PLAYER_X];
  if (a >= 0x60) {
    x++;
    carry_flag = a >= 0xa0;
    if (!carry_flag) { goto loc_df22; }
    x++;
  }
  
loc_df22:
  y = ROM(TEXT_WARP_NUMBERS + x);
  y--;
  ram[WORLD_NUMBER] = y;
  x = ROM(WORLD_AREA_OFFSETS + y);
  ram[AREA_POINTER] = ROM(AREAS_WORLD1 + x);
  ram[EVENT_MUSIC_REQUEST] = 0x80;
  a = 0x0;
  ram[ENTRANCE_PAGE] = a;
  ram[AREA_NUMBER] = a;
  ram[LEVEL_NUMBER] = a;
  ram[ALT_ENTRANCE] = a;
  ram[HIDDEN_1UP]++;
  ram[RELOAD_TIME_LIMIT]++;
  return;
}

void block_player_side(void) {
  a = 0x0;
  y = ram[PLAYER_X_SPEED];
  x = ram[0x0];
  x--; zero_flag = x == 0;
  if (zero_flag) {
    x++;
    neg_flag = ((y - 0x0) & 0x80) != 0;
    if (neg_flag) { goto loc_df81; }
    a = 0xff;
  } else {
    x = 0x2;
    neg_flag = ((y - 0x1) & 0x80) != 0;
    if (!neg_flag) { goto loc_df81; }
    a = 0x1;
  }
  ram[SIDE_CONTACT_TIMER] = 0x10;
  y = 0x0;
  ram[PLAYER_X_SPEED] = y;
  neg_flag = ((a - 0x0) & 0x80) != 0;
  if (neg_flag) {
    y--;
  }
  ram[0x0] = y;
  carry_flag = false;
  { uint16_t sum = a + ram[PLAYER_X] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[PLAYER_X] = a;
  a = ram[PLAYER_PAGE];
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  ram[PLAYER_PAGE] = a;
  
loc_df81:
  a = x;
  a ^= 0xff;
  a &= ram[PLAYER_COLLISION];
  ram[PLAYER_COLLISION] = a;
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
  { uint8_t m = 0xc2; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) {
    { uint8_t m = 0xc3; carry_flag = a >= m; zero_flag = a == m; }
    if (zero_flag) { goto loc_dfab; }
    carry_flag = false;
    return;
  }
  
loc_dfab:
  a = 0x1;
  ram[PULSE2_SFX_REQUEST] = a;
  return;
}

// in: a; out: a, x, y
void metatile_group(void) {
  y = a;
  a &= 0xc0;
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  x = a;
  a = y;
  return;
}

// in: x; out: x
void enemy_bg_collision(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  enemy_height_check();
  if (!carry_flag) {
    return;
  }
  y = ram[(uint8_t)(ENEMY_KIND + x)];
  if (y == 0x12) {
    a = ram[(uint8_t)(ENEMY_Y + x)];
    if (a < 0x25) {
      return;
    }
  }
  if (y == 0xe) {
    enemy_jump_check(); return;
  }
  if (y == 0x5) {
  } else {
    if (y != 0x12) {
      zero_flag = y == 0x2e;
      if (zero_flag) { goto loc_dff2; }
      if (y >= 0x7) {
        return;
      }
    }
    
loc_dff2:
    check_under_enemy();
    if (!zero_flag) { goto loc_dffa; }
    
loc_dff7:
    goto loc_e0e2;
    
loc_dffa:
    is_passable_metatile();
    if (zero_flag) { goto loc_dff7; }
    if (a == 0x23) {
      y = ram[0x2];
      dynamic_ram_write(indirect_y_addr(0x6), 0x0);
      a = ram[(uint8_t)(ENEMY_KIND + x)];
      if (a >= 0x15) {
        check_stun_enemy();
        return;
      }
      if (a == 0x6) {
        kill_enemy_on_block();
      }
      a = 0x1;
      spawn_floating_score();
      check_stun_enemy(); // fallthrough
      return;
    }
    a = ram[0x4];
    carry_flag = true;
    a = (uint8_t)(a - 0x8 - !carry_flag);
    if (a < 0x5) {
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      a &= 0x40; zero_flag = a == 0;
      if (zero_flag) {
        a = ram[(uint8_t)(ENEMY_STATE + x)];
        carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
        if (!carry_flag) { goto loc_e07e; }
        
loc_e07b:
        enemy_side_check(); return;
        
loc_e07e:
        a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
        if (zero_flag) { goto loc_e07b; }
        if (a != 0x5) {
          if (a >= 0x3) {
            return;
          }
          a = ram[(uint8_t)(ENEMY_STATE + x)];
          zero_flag = a == 0x2;
          if (!zero_flag) { goto loc_e0a5; }
          a = 0x10;
          y = ram[(uint8_t)(ENEMY_KIND + x)];
          if (y == 0x12) {
            a = 0x0;
          }
          ram[(ENEMY_INTERVAL_TIMER + x) & 0x7ff] = a;
          ram[(uint8_t)(ENEMY_STATE + x)] = 0x3;
          land_enemy();
          return;
        }
        
loc_e0a5:
        a = ram[(uint8_t)(ENEMY_KIND + x)];
        zero_flag = a == 0x6;
        if (zero_flag) { goto loc_e0cd; }
        if (a == 0x12) {
          ram[(uint8_t)(ENEMY_DIR + x)] = 0x1;
          ram[(uint8_t)(ENEMY_X_SPEED + x)] = 0x8;
          a = ram[FRAME_COUNT];
          a &= 0x7; zero_flag = a == 0;
          if (zero_flag) { goto loc_e0cd; }
        }
        y = 0x1;
        player_enemy_distance();
        if (neg_flag) {
          y++;
        }
        a = y;
        zero_flag = a == ram[(uint8_t)(ENEMY_DIR + x)];
        if (!zero_flag) { goto loc_e0cd; }
        enemy_bump();
      }
      
loc_e0cd:
      land_enemy();
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      a &= 0x80; zero_flag = a == 0;
      if (zero_flag) {
        ram[(uint8_t)(ENEMY_STATE + x)] = 0x0;
        return;
      }
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      a &= 0xbf;
      ram[(uint8_t)(ENEMY_STATE + x)] = a;
      return;
    }
    
loc_e0e2:
    a = ram[(uint8_t)(ENEMY_KIND + x)];
    if (a == 0x3) {
      a = ram[(uint8_t)(ENEMY_STATE + x)]; zero_flag = a == 0;
      if (zero_flag) {
        enemy_bump();
        return;
      }
    }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    y = a;
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) {
      a = ram[(uint8_t)(ENEMY_STATE + x)];
      a |= 0x40;
    } else {
      a = ROM(ENEMY_BG_STATES + y);
    }
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    enemy_side_check(); // fallthrough
    return;
  }
  check_under_enemy();
  if (!zero_flag) {
    if (a == 0x23) {
      kill_enemy_on_block(); // fallthrough
      return;
    }
    a = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_e1a7; }
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    a &= 0x88;
    ram[(uint8_t)(ENEMY_STATE + x)] = a;
    land_enemy();
    enemy_side_check(); return;
  }
  
loc_e1a7:
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a |= 0x1;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  return;
}

// in: a, x; out: none
void check_stun_enemy(void) {
  if (a < 0x9) {
    stun_enemy();
    return;
  }
  if (a >= 0x11) {
    stun_enemy();
    return;
  }
  if (a >= 0xa) {
    if (a < 0xd) {
      stun_enemy();
      return;
    }
  }
  a &= 0x1;
  ram[(uint8_t)(ENEMY_KIND + x)] = a;
  stun_enemy(); // fallthrough
  return;
}

// in: x; out: y
void stun_enemy(void) {
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  a &= 0xf0;
  a |= 0x2;
  ram[(uint8_t)(ENEMY_STATE + x)] = a;
  ram[(uint8_t)(ENEMY_Y + x)]--;
  ram[(uint8_t)(ENEMY_Y + x)]--;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x7) {
    a = 0xfd;
    y = ram[AREA_KIND]; zero_flag = y == 0;
    if (!zero_flag) { goto loc_e04a; }
  }
  a = 0xff;
  
loc_e04a:
  ram[(uint8_t)(ENEMY_Y_SPEED + x)] = a;
  y = 0x1;
  player_enemy_distance();
  if (neg_flag) {
    y++;
  }
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a != 0x33) {
    zero_flag = a == 0x8;
    if (zero_flag) { goto loc_e060; }
    ram[(uint8_t)(ENEMY_DIR + x)] = y;
  }
  
loc_e060:
  y--;
  ram[(uint8_t)(ENEMY_X_SPEED + x)] = ROM(ENEMY_BG_SPEEDS + y);
  return;
}

// in: x; out: x
void enemy_side_check(void) {
  a = ram[(uint8_t)(ENEMY_Y + x)];
  if (a < 0x20) {
    return;
  }
  y = 0x16;
  ram[(ENEMY_DATA_PTR + 2)] = 0x2;
  do {
    a = ram[(ENEMY_DATA_PTR + 2)];
    zero_flag = a == ram[(uint8_t)(ENEMY_DIR + x)];
    if (zero_flag) {
      a = 0x1;
      enemy_block_check();
      if (zero_flag) { goto loc_e11c; }
      is_passable_metatile();
      if (!zero_flag) {
        enemy_bump();
        return;
      }
    }
    
loc_e11c:
    ram[(ENEMY_DATA_PTR + 2)]--;
    y++;
    carry_flag = y >= 0x18;
  } while (!carry_flag);
  return;
}

// in: x; out: x, y
void enemy_bump(void) {
  if (x != 0x5) {
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (!carry_flag) { goto loc_e131; }
    ram[PULSE1_SFX_REQUEST] = 0x2;
  }
  
loc_e131:
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x5) {
    ram[0x0] = 0x0;
    y = 0xfa;
    hammer_bro_jump(); return;
  }
  reverse_enemy(); return;
}

// in: x; out: carry, negative
void player_enemy_distance(void) {
  a = ram[(uint8_t)(ENEMY_X + x)];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[PLAYER_X] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[0x0] = a;
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  { uint16_t diff = (uint16_t)(a - ram[PLAYER_PAGE] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; neg_flag = (a & 0x80) != 0; }
  return;
}

// in: x; out: none
void land_enemy(void) {
  clear_vertical_motion();
  a = ram[(uint8_t)(ENEMY_Y + x)];
  a &= 0xf0;
  a |= 0x8;
  ram[(uint8_t)(ENEMY_Y + x)] = a;
  return;
}

// in: x; out: carry
void enemy_height_check(void) {
  a = ram[(uint8_t)(ENEMY_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + 0x3e + carry_flag);
  carry_flag = a >= 0x44;
  return;
}

// in: x; out: x
void enemy_jump_check(void) {
  enemy_height_check();
  if (carry_flag) {
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)];
    carry_flag = false;
    a = (uint8_t)(a + 0x2 + carry_flag);
    carry_flag = a >= 0x3;
    if (!carry_flag) { goto loc_e182; }
    check_under_enemy();
    if (zero_flag) { goto loc_e182; }
    is_passable_metatile();
    if (zero_flag) { goto loc_e182; }
    land_enemy();
    ram[(uint8_t)(ENEMY_Y_SPEED + x)] = 0xfd;
  }
  
loc_e182:
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
  a = 0x0;
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
  a = ram[(uint8_t)(FIREBALL_Y + x)];
  if (a >= 0x18) {
    fireball_block_check();
    if (zero_flag) { goto loc_e1ef; }
    is_passable_metatile();
    if (zero_flag) { goto loc_e1ef; }
    a = ram[(uint8_t)(FIREBALL_Y_SPEED + x)]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) { goto loc_e1f4; }
    a = ram[(uint8_t)(FIREBALL_BOUNCING + x)]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_e1f4; }
    ram[(uint8_t)(FIREBALL_Y_SPEED + x)] = 0xfd;
    ram[(uint8_t)(FIREBALL_BOUNCING + x)] = 0x1;
    a = ram[(uint8_t)(FIREBALL_Y + x)];
    a &= 0xf8;
    ram[(uint8_t)(FIREBALL_Y + x)] = a;
    return;
  }
  
loc_e1ef:
  ram[(uint8_t)(FIREBALL_BOUNCING + x)] = 0x0;
  return;
  
loc_e1f4:
  ram[(uint8_t)(FIREBALL_STATE + x)] = 0x80;
  ram[PULSE1_SFX_REQUEST] = 0x2;
  return;
}

// left, top, right and bottom offsets by box kind
// in: x; out: x
void fireball_box(void) {
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x7 + carry_flag);
  x = a;
  y = 0x2;
  fireball_box_common(); return;
}

// in: x; out: x
void misc_box(void) {
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x9 + carry_flag);
  x = a;
  y = 0x6;
  fireball_box_common(); // fallthrough
  return;
}

// in: x, y; out: x
void fireball_box_common(void) {
  compute_box();
  clip_box_to_screen(); return;
}

// in: x; out: x
void enemy_box(void) {
  ram[0x0] = 0x48;
  y = 0x44;
  enemy_box_offscreen_bits(); return;
}

// in: x; out: x
void small_platform_box(void) {
  ram[0x0] = 0x8;
  y = 0x4;
  enemy_box_offscreen_bits(); // fallthrough
  return;
}

// in: x, y; out: x
void enemy_box_offscreen_bits(void) {
  a = ram[(uint8_t)(ENEMY_X + x)];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[SCREEN_LEFT_X] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[0x1] = a;
  a = ram[(uint8_t)(ENEMY_PAGE + x)];
  a = (uint8_t)(a - ram[SCREEN_LEFT_PAGE] - !carry_flag); neg_flag = (a & 0x80) != 0;
  if (!neg_flag) {
    a |= ram[0x1]; zero_flag = a == 0;
    if (zero_flag) { goto loc_e267; }
    y = ram[0x0];
  }
  
loc_e267:
  a = y;
  a &= ram[ENEMY_OFFSCREEN]; zero_flag = a == 0;
  ram[ENEMY_OFFSCREEN_MASKED + x] = a;
  if (!zero_flag) {
    hide_box();
    return;
  }
  enemy_box_core(); return;
}

// in: x; out: x
void large_platform_box(void) {
  x++;
  compute_x_offscreen_bits();
  x--;
  if (a >= 0xfe) {
    hide_box();
    return;
  }
  enemy_box_core(); // fallthrough
  return;
}

// in: x; out: x
void enemy_box_core(void) {
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x1 + carry_flag);
  x = a;
  y = 0x1;
  compute_box();
  clip_box_to_screen(); return;
}

// in: x; out: none
void hide_box(void) {
  a = x;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  y = a;
  a = 0xff;
  ram[ENEMY_BOXES + y] = a;
  ram[(ENEMY_BOXES + 1) + y] = a;
  ram[(ENEMY_BOXES + 2) + y] = a;
  ram[(ENEMY_BOXES + 3) + y] = a;
  return;
}

// in: x, y; out: x, y
void compute_box(void) {
  ram[0x0] = x;
  ram[0x2] = ram[OBJ_SCREEN_Y + y];
  ram[0x1] = ram[OBJ_SCREEN_X + y];
  a = x;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  y = a;
  a = ram[OBJ_BOX_KIND + x];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  x = a;
  a = ram[0x1];
  carry_flag = false;
  a = (uint8_t)(a + ROM(BOX_SIZES + x) + carry_flag);
  ram[BOXES + y] = a;
  a = ram[0x1];
  carry_flag = false;
  a = (uint8_t)(a + ROM((BOX_SIZES + 2) + x) + carry_flag);
  ram[BOX_RIGHT + y] = a;
  x++;
  y++;
  a = ram[0x2];
  carry_flag = false;
  a = (uint8_t)(a + ROM(BOX_SIZES + x) + carry_flag);
  ram[BOXES + y] = a;
  a = ram[0x2];
  carry_flag = false;
  a = (uint8_t)(a + ROM((BOX_SIZES + 2) + x) + carry_flag);
  ram[BOX_RIGHT + y] = a;
  a = ram[0x100 + ++sp];
  y = a;
  x = ram[0x0];
  return;
}

// in: x, y; out: x
void clip_box_to_screen(void) {
  a = ram[SCREEN_LEFT_X];
  carry_flag = false;
  { uint16_t sum = a + 0x80 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x2] = a;
  a = ram[SCREEN_LEFT_PAGE];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[0x1] = a;
  a = ram[(uint8_t)(OBJ_X + x)];
  carry_flag = a >= ram[0x2];
  a = ram[(uint8_t)(OBJ_PAGE + x)];
  { uint16_t diff = (uint16_t)(a - ram[0x1] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  if (carry_flag) {
    a = ram[BOX_RIGHT + y]; neg_flag = (a & 0x80) != 0;
    if (!neg_flag) {
      a = 0xff;
      x = ram[BOXES + y]; neg_flag = (x & 0x80) != 0;
      if (!neg_flag) {
        ram[BOXES + y] = a;
      }
      ram[BOX_RIGHT + y] = a;
    }
    x = ram[SLOT];
    return;
  }
  a = ram[BOXES + y]; neg_flag = (a & 0x80) != 0;
  if (neg_flag) {
    carry_flag = a >= 0xa0;
    if (!carry_flag) { goto loc_e322; }
    a = 0x0;
    x = ram[BOX_RIGHT + y]; neg_flag = (x & 0x80) != 0;
    if (neg_flag) {
      ram[BOX_RIGHT + y] = a;
    }
    ram[BOXES + y] = a;
  }
  
loc_e322:
  x = ram[SLOT];
  return;
}

// in: y; out: y, carry
void player_collides(void) {
  x = 0x0;
  objects_collide(); // fallthrough
  return;
}

// bounding box overlap test, carry set on collision
// in: x, y; out: y, carry
void objects_collide(void) {
  ram[0x6] = y;
  ram[0x7] = 0x1;
  do {
    a = ram[BOXES + y];
    carry_flag = a >= ram[BOXES + x];
    if (!carry_flag) {
      { uint8_t m = ram[BOX_RIGHT + x]; carry_flag = a >= m; zero_flag = a == m; }
      if (carry_flag) {
        if (zero_flag) { goto loc_e37e; }
        a = ram[BOX_RIGHT + y];
        carry_flag = a >= ram[BOXES + y];
        if (!carry_flag) { goto loc_e37e; }
        carry_flag = a >= ram[BOXES + x];
        if (carry_flag) { goto loc_e37e; }
        y = ram[0x6];
        return;
      }
      a = ram[BOX_RIGHT + x];
      carry_flag = a >= ram[BOXES + x];
      if (!carry_flag) { goto loc_e37e; }
      a = ram[BOX_RIGHT + y];
      carry_flag = a >= ram[BOXES + x];
      if (carry_flag) { goto loc_e37e; }
      y = ram[0x6];
      return;
    }
    zero_flag = a == ram[BOXES + x];
    if (!zero_flag) {
      { uint8_t m = ram[BOX_RIGHT + x]; carry_flag = a >= m; zero_flag = a == m; }
      if (!carry_flag) { goto loc_e37e; }
      if (zero_flag) { goto loc_e37e; }
      { uint8_t m = ram[BOX_RIGHT + y]; carry_flag = a >= m; zero_flag = a == m; }
      if (carry_flag) {
        if (zero_flag) { goto loc_e37a; }
        a = ram[BOX_RIGHT + y];
        carry_flag = a >= ram[BOXES + x];
        if (carry_flag) { goto loc_e37e; }
      }
      
loc_e37a:
      carry_flag = false;
      y = ram[0x6];
      return;
    }
    
loc_e37e:
    x++;
    y++;
    ram[0x7]--; neg_flag = (ram[0x7] & 0x80) != 0;
  } while (!neg_flag);
  carry_flag = true;
  y = ram[0x6];
  return;
}

// in: a, x, y; out: a, x, y, zero
void enemy_block_check(void) {
  ram[0x100 + sp--] = a;
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x1 + carry_flag);
  x = a;
  a = ram[0x100 + ++sp];
  block_check_common(); return;
}

// in: x; out: a, x, y, zero
void fireball_block_check(void) {
  y = 0x1a;
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x7 + carry_flag);
  x = a;
  a = 0x0;
  block_check_common(); // fallthrough
  return;
}

// in: a, x, y; out: a, x, y, zero
void block_check_common(void) {
  block_probe();
  x = ram[SLOT];
  zero_flag = a == 0x0;
  return;
}

// in: y; out: a, y
void block_probe_feet(void) {
  y++;
  block_probe_head(); // fallthrough
  return;
}

// in: y; out: a, y, zero
void block_probe_head(void) {
  a = 0x0;
  
  block_probe_player(); return;
}

// in: y; out: a, y, zero
void block_probe_side(void) {
  a = 0x1;
  block_probe_player(); // fallthrough
  return;
}

// in: a, y; out: a, y, zero
void block_probe_player(void) {
  x = 0x0;
  block_probe(); // fallthrough
  return;
}

// metatile at an object's probe point
// in: a, x, y; out: a, y, zero
void block_probe(void) {
  ram[0x100 + sp--] = a;
  ram[0x4] = y;
  a = ROM(BLOCK_PROBE_X + y);
  carry_flag = false;
  { uint16_t sum = a + ram[(uint8_t)(OBJ_X + x)] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x5] = a;
  a = ram[(uint8_t)(OBJ_PAGE + x)];
  a = (uint8_t)(a + 0x0 + carry_flag);
  a &= 0x1;
  carry_flag = (a & 1) != 0; a = a >> 1;
  a |= ram[0x5];
  a = (uint8_t)(a >> 1 | carry_flag << 7);
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  get_block_buffer_address();
  y = ram[0x4];
  a = ram[(uint8_t)(OBJ_Y + x)];
  carry_flag = false;
  a = (uint8_t)(a + ROM(BLOCK_PROBE_Y + y) + carry_flag);
  a &= 0xf0;
  carry_flag = true;
  a = (uint8_t)(a - 0x20 - !carry_flag);
  ram[0x2] = a;
  y = a;
  ram[0x3] = read_byte(indirect_y_addr(0x6));
  y = ram[0x4];
  a = ram[0x100 + ++sp]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(OBJ_Y + x)];
  } else {
    a = ram[(uint8_t)(OBJ_X + x)];
  }
  a &= 0xf;
  ram[0x4] = a;
  a = ram[0x3]; zero_flag = a == 0;
  return;
}

// in: y; out: y
void draw_vine(void) {
  ram[0x0] = y;
  a = ram[ENEMY_SCREEN_Y];
  carry_flag = false;
  a = (uint8_t)(a + ROM(VINE_Y_OFFSETS + y) + carry_flag);
  x = ram[VINE_SLOTS + y];
  y = ram[ENEMY_SPRITE_SLOT + x];
  ram[0x2] = y;
  stack_six_sprites();
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[(SPRITE_X + 8) + y] = a;
  ram[(SPRITE_X + 16) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x6 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  ram[(SPRITE_X + 12) + y] = a;
  ram[(SPRITE_X + 20) + y] = a;
  a = 0x21;
  ram[SPRITE_ATTR + y] = a;
  ram[(SPRITE_X + 7) + y] = a;
  ram[(SPRITE_X + 15) + y] = a;
  a |= 0x40;
  ram[(SPRITE_X + 3) + y] = a;
  ram[(SPRITE_X + 11) + y] = a;
  ram[(SPRITE_X + 19) + y] = a;
  x = 0x5;
  do {
    ram[SPRITE_TILE + y] = 0xe1;
    y++;
    y++;
    y++;
    y++;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  y = ram[0x2];
  a = ram[0x0]; zero_flag = a == 0;
  if (zero_flag) {
    ram[SPRITE_TILE + y] = 0xe0;
  }
  x = 0x0;
  do {
    a = ram[VINE_START_Y];
    carry_flag = true;
    a = (uint8_t)(a - ram[SPRITE_Y + y] - !carry_flag);
    if (a >= 0x64) {
      ram[SPRITE_Y + y] = 0xf8;
    }
    y++;
    y++;
    y++;
    y++;
    x++;
    zero_flag = x == 0x6;
  } while (!zero_flag);
  y = ram[0x0];
  return;
}

// in: a, y; out: y, carry
void stack_six_sprites(void) {
  x = 0x6;
  do {
    ram[SPRITE_Y + y] = a;
    carry_flag = false;
    { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    y++;
    y++;
    y++;
    y++;
    x--; zero_flag = x == 0;
  } while (!zero_flag);
  y = ram[0x2];
  return;
}

// in: x; out: x
void draw_hammer(void) {
  y = ram[MISC_SPRITE_SLOT + x];
  a = ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[(uint8_t)(MISC_STATE + x)];
    a &= 0x7f;
    zero_flag = a == 0x1;
    if (zero_flag) { goto loc_e4f0; }
  }
  x = 0x0;
  goto loc_e4f7;
  
loc_e4f0:
  a = ram[FRAME_COUNT];
  a = a >> 1;
  a = a >> 1;
  a &= 0x3;
  x = a;
  
loc_e4f7:
  a = ram[MISC_SCREEN_Y];
  carry_flag = false;
  a = (uint8_t)(a + ROM(HAMMER_FIRST_Y + x) + carry_flag);
  ram[SPRITE_Y + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + ROM(HAMMER_SECOND_Y + x) + carry_flag);
  ram[(SPRITE_X + 1) + y] = a;
  a = ram[MISC_SCREEN_X];
  carry_flag = false;
  a = (uint8_t)(a + ROM(HAMMER_FIRST_X + x) + carry_flag);
  ram[SPRITE_X + y] = a;
  carry_flag = false;
  { uint16_t sum = a + ROM(HAMMER_SECOND_X + x) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(SPRITE_X + 4) + y] = a;
  ram[SPRITE_TILE + y] = ROM(HAMMER_FIRST_TILES + x);
  ram[(SPRITE_X + 2) + y] = ROM(HAMMER_SECOND_TILES + x);
  a = ROM(HAMMER_ATTRIBUTES + x);
  ram[SPRITE_ATTR + y] = a;
  ram[(SPRITE_X + 3) + y] = a;
  x = ram[SLOT];
  a = ram[MISC_OFFSCREEN];
  a &= 0xfc; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  ram[(uint8_t)(MISC_STATE + x)] = 0x0;
  a = 0xf8;
  fill_two_sprites();
  return;
}

// in: x; out: none
void draw_flagpole_flag(void) {
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  ram[(SPRITE_X + 8) + y] = a;
  carry_flag = false;
  { uint16_t sum = a + 0xc + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x5] = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  fill_two_sprites();
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(SPRITE_X + 5) + y] = a;
  ram[0x2] = ram[FLAG_SCORE_Y];
  a = 0x1;
  ram[0x3] = a;
  ram[0x4] = a;
  ram[SPRITE_ATTR + y] = a;
  ram[(SPRITE_X + 3) + y] = a;
  ram[(SPRITE_X + 7) + y] = a;
  a = 0x7e;
  ram[SPRITE_TILE + y] = a;
  ram[(SPRITE_X + 6) + y] = a;
  ram[(SPRITE_X + 2) + y] = 0x7f;
  a = ram[FLAGPOLE_CONTACT_Y]; zero_flag = a == 0;
  if (!zero_flag) {
    a = y;
    carry_flag = false;
    a = (uint8_t)(a + 0xc + carry_flag);
    y = a;
    a = ram[FLAG_SCORE_KIND];
    a = (uint8_t)(a << 1);
    x = a;
    ram[0x0] = ROM(FLAGPOLE_SCORE_TILES + x);
    a = ROM((FLAGPOLE_SCORE_TILES + 1) + x);
    draw_sprite_row();
  }
  x = ram[SLOT];
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[ENEMY_OFFSCREEN];
  a &= 0xe; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  hide_six_sprites(); // fallthrough
  return;
}

// in: y; out: none
void hide_six_sprites(void) {
  a = 0xf8;
  fill_six_sprites(); // fallthrough
  return;
}

// in: a, y; out: none
void fill_six_sprites(void) {
  ram[(SPRITE_X + 17) + y] = a;
  ram[(SPRITE_X + 13) + y] = a;
  fill_four_sprites(); // fallthrough
  return;
}

// in: a, y; out: none
void fill_four_sprites(void) {
  ram[(SPRITE_X + 9) + y] = a;
  fill_three_sprites(); // fallthrough
  return;
}

// in: a, y; out: none
void fill_three_sprites(void) {
  ram[(SPRITE_X + 5) + y] = a;
  fill_two_sprites(); // fallthrough
  return;
}

// in: a, y; out: none
void fill_two_sprites(void) {
  ram[(SPRITE_X + 1) + y] = a;
  ram[SPRITE_Y + y] = a;
  return;
}

// in: x; out: x
void draw_large_platform(void) {
  y = ram[ENEMY_SPRITE_SLOT + x];
  ram[0x2] = y;
  y++;
  y++;
  y++;
  a = ram[ENEMY_SCREEN_X];
  stack_six_sprites();
  x = ram[SLOT];
  a = ram[(uint8_t)(ENEMY_Y + x)];
  fill_four_sprites();
  y = ram[AREA_KIND];
  { uint8_t m = 0x3; carry_flag = y >= m; zero_flag = y == m; }
  if (!zero_flag) {
    y = ram[HARD_MODE_LATE]; zero_flag = y == 0;
    if (zero_flag) { goto loc_e5eb; }
  }
  a = 0xf8;
  
loc_e5eb:
  y = ram[ENEMY_SPRITE_SLOT + x];
  ram[(SPRITE_X + 13) + y] = a;
  ram[(SPRITE_X + 17) + y] = a;
  a = 0x5b;
  x = ram[CLOUD_OVERRIDE]; zero_flag = x == 0;
  if (!zero_flag) {
    a = 0x75;
  }
  x = ram[SLOT];
  y++;
  fill_six_sprites();
  a = 0x2;
  y++;
  fill_six_sprites();
  x++;
  compute_x_offscreen_bits();
  x--;
  y = ram[ENEMY_SPRITE_SLOT + x];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[SPRITE_Y + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 1) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 5) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 9) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    ram[(SPRITE_X + 13) + y] = 0xf8;
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (carry_flag) {
    ram[(SPRITE_X + 17) + y] = 0xf8;
  }
  a = ram[ENEMY_OFFSCREEN];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (!carry_flag) {
    return;
  }
  hide_six_sprites();
  return;
}

// in: x; out: x
void draw_coin_or_score(void) {
  goto draw_coin_or_score;
  
loc_e655:
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    ram[(uint8_t)(MISC_Y + x)]--;
  }
  a = ram[(uint8_t)(MISC_Y + x)];
  fill_two_sprites();
  a = ram[MISC_SCREEN_X];
  ram[SPRITE_X + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  a = 0x2;
  ram[SPRITE_ATTR + y] = a;
  ram[(SPRITE_X + 3) + y] = a;
  ram[SPRITE_TILE + y] = 0xf7;
  ram[(SPRITE_X + 2) + y] = 0xfb;
  return;
  
draw_coin_or_score:
  y = ram[MISC_SPRITE_SLOT + x];
  a = ram[(uint8_t)(MISC_STATE + x)];
  carry_flag = a >= 0x2;
  if (carry_flag) { goto loc_e655; }
  a = ram[(uint8_t)(MISC_Y + x)];
  ram[SPRITE_Y + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 1) + y] = a;
  a = ram[MISC_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[(SPRITE_X + 4) + y] = a;
  a = ram[FRAME_COUNT];
  carry_flag = (a & 1) != 0; a = a >> 1;
  a &= 0x3;
  x = a;
  a = ROM(JUMPING_COIN_TILES + x);
  y++;
  fill_two_sprites();
  y--;
  ram[SPRITE_ATTR + y] = 0x2;
  ram[(SPRITE_X + 3) + y] = 0x82;
  x = ram[SLOT];
  return;
}

// in: none; out: x
void draw_powerup(void) {
  y = ram[(ENEMY_SPRITE_SLOT + 5)];
  a = ram[ENEMY_SCREEN_Y];
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[0x2] = a;
  ram[0x5] = ram[ENEMY_SCREEN_X];
  x = ram[POWERUP_KIND];
  a = ROM(POWERUP_ATTRIBUTES + x);
  a |= ram[(ENEMY_SPRITE_ATTR + 5)];
  ram[0x4] = a;
  a = x;
  ram[0x100 + sp--] = a;
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  x = a;
  a = 0x1;
  ram[0x7] = a;
  ram[0x3] = a;
  do {
    ram[0x0] = ROM(POWERUP_TILES + x);
    a = ROM((POWERUP_TILES + 1) + x);
    draw_sprite_row();
    ram[0x7]--; neg_flag = (ram[0x7] & 0x80) != 0;
  } while (!neg_flag);
  y = ram[(ENEMY_SPRITE_SLOT + 5)];
  a = ram[0x100 + ++sp]; zero_flag = a == 0;
  if (!zero_flag) {
    zero_flag = a == 0x3;
    if (zero_flag) { goto loc_e73b; }
    ram[0x0] = a;
    a = ram[FRAME_COUNT];
    a = a >> 1;
    a &= 0x3;
    a |= ram[(ENEMY_SPRITE_ATTR + 5)];
    ram[SPRITE_ATTR + y] = a;
    ram[(SPRITE_X + 3) + y] = a;
    x = ram[0x0];
    x--; zero_flag = x == 0;
    if (!zero_flag) {
      ram[(SPRITE_X + 7) + y] = a;
      ram[(SPRITE_X + 11) + y] = a;
    }
    a = ram[(SPRITE_X + 3) + y];
    a |= 0x40;
    ram[(SPRITE_X + 3) + y] = a;
    a = ram[(SPRITE_X + 11) + y];
    a |= 0x40;
    ram[(SPRITE_X + 11) + y] = a;
  }
  
loc_e73b:
  clip_enemy_sprites(); return;
}

// in: x; out: x
void draw_enemy(void) {
  ram[0x2] = ram[(uint8_t)(ENEMY_Y + x)];
  ram[0x5] = ram[ENEMY_SCREEN_X];
  ram[(ENEMY_DATA_PTR + 2)] = ram[ENEMY_SPRITE_SLOT + x];
  ram[FLIP_VERTICALLY] = 0x0;
  ram[0x3] = ram[(uint8_t)(ENEMY_DIR + x)];
  ram[0x4] = ram[ENEMY_SPRITE_ATTR + x];
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0xd) {
    y = ram[(uint8_t)(ENEMY_X_SPEED + x)]; neg_flag = (y & 0x80) != 0;
    if (neg_flag) { goto loc_e8a9; }
    y = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff]; zero_flag = y == 0;
    if (zero_flag) { goto loc_e8a9; }
    return;
  }
  
loc_e8a9:
  a = ram[(uint8_t)(ENEMY_STATE + x)];
  ram[(ENEMY_DATA_PTR + 4)] = a;
  a &= 0x1f;
  y = a;
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0x35) {
    y = 0x0;
    ram[0x3] = 0x1;
    a = 0x15;
  }
  if (a == 0x33) {
    ram[0x2]--;
    a = 0x3;
    y = ram[(ENEMY_FRAME_TIMER + x) & 0x7ff]; zero_flag = y == 0;
    if (!zero_flag) {
      a |= 0x20;
    }
    ram[0x4] = a;
    y = 0x0;
    ram[(ENEMY_DATA_PTR + 4)] = y;
    a = 0x8;
  }
  if (a == 0x32) {
    y = 0x3;
    x = ram[SPRING_ANIM_STEP];
    a = ROM(SPRING_TILE_OFFSETS + x);
  }
  ram[(ENEMY_DATA_PTR + 6)] = a;
  ram[(ENEMY_DATA_PTR + 3)] = y;
  x = ram[SLOT];
  if (a == 0xc) {
    a = ram[(uint8_t)(ENEMY_Y_SPEED + x)]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) { goto loc_e8f2; }
    ram[FLIP_VERTICALLY]++;
  }
  
loc_e8f2:
  a = ram[BOWSER_GFX_FLAG]; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x16;
    if (a != 0x1) {
      y++;
    }
    ram[(ENEMY_DATA_PTR + 6)] = y;
  }
  y = ram[(ENEMY_DATA_PTR + 6)];
  if (y == 0x6) {
    a = ram[(uint8_t)(ENEMY_STATE + x)];
    if (a >= 0x2) {
      ram[(ENEMY_DATA_PTR + 3)] = 0x4;
    }
    a &= 0x20;
    a |= ram[TIMERS_PAUSED]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_e923; }
    a = ram[FRAME_COUNT];
    a &= 0x8; zero_flag = a == 0;
    if (!zero_flag) { goto loc_e923; }
    a = ram[0x3];
    a ^= 0x3;
    ram[0x3] = a;
  }
  
loc_e923:
  a = ROM(ENEMY_ATTRIBUTES + y);
  a |= ram[0x4];
  ram[0x4] = a;
  a = ROM(ENEMY_TILE_OFFSETS + y);
  x = a;
  y = ram[(ENEMY_DATA_PTR + 3)];
  a = ram[BOWSER_GFX_FLAG]; zero_flag = a == 0;
  if (!zero_flag) {
    zero_flag = a == 0x1;
    if (!zero_flag) { goto loc_e94c; }
    a = ram[BOWSER_BODY]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) {
      x = 0xde;
    }
    a = ram[(ENEMY_DATA_PTR + 4)];
    a &= 0x20; zero_flag = a == 0;
    if (zero_flag) { goto loc_e949; }
    
loc_e946:
    ram[FLIP_VERTICALLY] = x;
    
loc_e949:
    goto loc_ea4b;
    
loc_e94c:
    a = ram[BOWSER_BODY];
    a &= 0x1; zero_flag = a == 0;
    if (!zero_flag) {
      x = 0xe4;
    }
    a = ram[(ENEMY_DATA_PTR + 4)];
    a &= 0x20; zero_flag = a == 0;
    if (zero_flag) { goto loc_e949; }
    a = ram[0x2];
    carry_flag = true;
    a = (uint8_t)(a - 0x10 - !carry_flag);
    ram[0x2] = a;
    goto loc_e946;
  }
  if (x == 0x24) {
    if (y == 0x5) {
      x = 0x30;
      ram[0x3] = 0x2;
      ram[(ENEMY_DATA_PTR + 3)] = 0x5;
    }
  } else {
    if (x == 0x90) {
      a = ram[(ENEMY_DATA_PTR + 4)];
      a &= 0x20; zero_flag = a == 0;
      if (zero_flag) {
        a = ram[FRENZY_TIMER];
        carry_flag = a >= 0x10;
        if (carry_flag) { goto loc_e98d; }
        x = 0x96;
      }
      
loc_e98d:
      goto loc_ea37;
    }
    a = ram[(ENEMY_DATA_PTR + 6)];
    if (a < 0x4) {
      carry_flag = y >= 0x2;
      if (!carry_flag) { goto loc_e9a6; }
      x = 0x5a;
      y = ram[(ENEMY_DATA_PTR + 6)];
      zero_flag = y == 0x2;
      if (!zero_flag) { goto loc_e9a6; }
      x = 0x7e;
      ram[0x2]++;
    }
    
loc_e9a6:
    a = ram[(ENEMY_DATA_PTR + 3)];
    zero_flag = a == 0x4;
    if (!zero_flag) { goto loc_e9ca; }
    x = 0x72;
    ram[0x2]++;
    y = ram[(ENEMY_DATA_PTR + 6)];
    if (y != 0x2) {
      x = 0x66;
      ram[0x2]++;
    }
    zero_flag = y == 0x6;
    if (!zero_flag) { goto loc_e9ca; }
    x = 0x54;
    a = ram[(ENEMY_DATA_PTR + 4)];
    a &= 0x20; zero_flag = a == 0;
    if (!zero_flag) { goto loc_e9ca; }
    x = 0x8a;
    ram[0x2]--;
  }
  
loc_e9ca:
  y = ram[SLOT];
  a = ram[(ENEMY_DATA_PTR + 6)];
  if (a == 0x5) {
    a = ram[(ENEMY_DATA_PTR + 4)]; zero_flag = a == 0;
    if (zero_flag) { goto loc_e9fa; }
    a &= 0x8; zero_flag = a == 0;
    if (zero_flag) { goto loc_ea37; }
    x = 0xb4;
  } else {
    zero_flag = x == 0x48;
    if (zero_flag) { goto loc_e9fa; }
    a = ram[(ENEMY_INTERVAL_TIMER + y) & 0x7ff];
    carry_flag = a >= 0x5;
    if (carry_flag) { goto loc_ea37; }
    zero_flag = x == 0x3c;
    if (!zero_flag) { goto loc_e9fa; }
    zero_flag = a == 0x1;
    if (zero_flag) { goto loc_ea37; }
    ram[0x2]++;
    ram[0x2]++;
    ram[0x2]++;
    goto loc_ea29;
  }
  
loc_e9fa:
  a = ram[(ENEMY_DATA_PTR + 6)];
  zero_flag = a == 0x6;
  if (zero_flag) { goto loc_ea37; }
  zero_flag = a == 0x8;
  if (zero_flag) { goto loc_ea37; }
  zero_flag = a == 0xc;
  if (zero_flag) { goto loc_ea37; }
  carry_flag = a >= 0x18;
  if (carry_flag) { goto loc_ea37; }
  y = 0x0;
  if (a == 0x15) {
    y++;
    a = ram[WORLD_NUMBER];
    carry_flag = a >= 0x7;
    if (carry_flag) { goto loc_ea37; }
    x = 0xa2;
    ram[(ENEMY_DATA_PTR + 3)] = 0x3;
    goto loc_ea37;
  }
  a = ram[FRAME_COUNT];
  a &= ROM(ENEMY_ANIMATION_MASKS + y); zero_flag = a == 0;
  if (!zero_flag) { goto loc_ea37; }
  
loc_ea29:
  a = ram[(ENEMY_DATA_PTR + 4)];
  a &= 0xa0;
  a |= ram[TIMERS_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x6 + carry_flag);
    x = a;
  }
  
loc_ea37:
  a = ram[(ENEMY_DATA_PTR + 4)];
  a &= 0x20; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[(ENEMY_DATA_PTR + 6)];
    carry_flag = a >= 0x4;
    if (!carry_flag) { goto loc_ea4b; }
    y = 0x1;
    ram[FLIP_VERTICALLY] = y;
    y--;
    ram[(ENEMY_DATA_PTR + 3)] = y;
  }
  
loc_ea4b:
  y = ram[(ENEMY_DATA_PTR + 2)];
  draw_enemy_row();
  draw_enemy_row();
  draw_enemy_row();
  x = ram[SLOT];
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = ram[(ENEMY_DATA_PTR + 6)];
  { uint8_t m = 0x8; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) { goto loc_ea64; }
  
loc_ea61:
  clip_enemy_sprites(); return;
  
loc_ea64:
  a = ram[FLIP_VERTICALLY]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[SPRITE_ATTR + y];
    a |= 0x80;
    y++;
    y++;
    fill_six_sprites();
    y--;
    y--;
    a = y;
    x = a;
    a = ram[(ENEMY_DATA_PTR + 6)];
    if (a != 0x5) {
      zero_flag = a == 0x11;
      if (zero_flag) { goto loc_ea8a; }
      carry_flag = a >= 0x15;
      if (carry_flag) { goto loc_ea8a; }
      a = x;
      carry_flag = false;
      a = (uint8_t)(a + 0x8 + carry_flag);
      x = a;
    }
    
loc_ea8a:
    a = ram[SPRITE_TILE + x];
    ram[0x100 + sp--] = a;
    a = ram[(SPRITE_X + 2) + x];
    ram[0x100 + sp--] = a;
    ram[SPRITE_TILE + x] = ram[(SPRITE_X + 14) + y];
    ram[(SPRITE_X + 2) + x] = ram[(SPRITE_X + 18) + y];
    a = ram[0x100 + ++sp];
    ram[(SPRITE_X + 18) + y] = a;
    a = ram[0x100 + ++sp];
    ram[(SPRITE_X + 14) + y] = a;
  }
  a = ram[BOWSER_GFX_FLAG]; zero_flag = a == 0;
  if (!zero_flag) { goto loc_ea61; }
  a = ram[(ENEMY_DATA_PTR + 6)];
  x = ram[(ENEMY_DATA_PTR + 3)];
  if (a == 0x5) {
    clip_enemy_sprites(); return;
  }
  if (a != 0x7) {
    zero_flag = a == 0xd;
    if (zero_flag) { goto loc_ead7; }
    zero_flag = a == 0xc;
    if (zero_flag) { goto loc_ead7; }
    if (a == 0x12) {
      zero_flag = x == 0x5;
      if (!zero_flag) { goto loc_eb12; }
    }
    if (a == 0x15) {
      ram[(SPRITE_X + 19) + y] = 0x42;
    }
    carry_flag = x >= 0x2;
    if (!carry_flag) { goto loc_eb12; }
  }
  
loc_ead7:
  a = ram[BOWSER_GFX_FLAG]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[SPRITE_ATTR + y];
    a &= 0xa3;
    ram[SPRITE_ATTR + y] = a;
    ram[(SPRITE_X + 7) + y] = a;
    ram[(SPRITE_X + 15) + y] = a;
    a |= 0x40;
    if (x == 0x5) {
      a |= 0x80;
    }
    ram[(SPRITE_X + 3) + y] = a;
    ram[(SPRITE_X + 11) + y] = a;
    ram[(SPRITE_X + 19) + y] = a;
    zero_flag = x == 0x4;
    if (!zero_flag) { goto loc_eb12; }
    a = ram[(SPRITE_X + 7) + y];
    a |= 0x80;
    ram[(SPRITE_X + 7) + y] = a;
    ram[(SPRITE_X + 15) + y] = a;
    a |= 0x40;
    ram[(SPRITE_X + 11) + y] = a;
    ram[(SPRITE_X + 19) + y] = a;
  }
  
loc_eb12:
  a = ram[(ENEMY_DATA_PTR + 6)];
  if (a == 0x11) {
    a = ram[FLIP_VERTICALLY]; zero_flag = a == 0;
    if (zero_flag) {
      a = ram[(SPRITE_X + 15) + y];
      a &= 0x81;
      ram[(SPRITE_X + 15) + y] = a;
      a = ram[(SPRITE_X + 19) + y];
      a |= 0x41;
      ram[(SPRITE_X + 19) + y] = a;
      x = ram[FRENZY_TIMER];
      if (x >= 0x10) {
        clip_enemy_sprites();
        return;
      }
      ram[(SPRITE_X + 11) + y] = a;
      a &= 0x81;
      ram[(SPRITE_X + 7) + y] = a;
      clip_enemy_sprites(); return;
    }
    a = ram[SPRITE_ATTR + y];
    a &= 0x81;
    ram[SPRITE_ATTR + y] = a;
    a = ram[(SPRITE_X + 3) + y];
    a |= 0x41;
    ram[(SPRITE_X + 3) + y] = a;
  }
  a = ram[(ENEMY_DATA_PTR + 6)];
  if (a < 0x18) {
    clip_enemy_sprites();
    return;
  }
  a = 0x82;
  ram[(SPRITE_X + 7) + y] = a;
  ram[(SPRITE_X + 15) + y] = a;
  a |= 0x40;
  ram[(SPRITE_X + 11) + y] = a;
  ram[(SPRITE_X + 19) + y] = a;
  clip_enemy_sprites(); // fallthrough
  return;
}

// in: none; out: x
void clip_enemy_sprites(void) {
  x = ram[SLOT];
  a = ram[ENEMY_OFFSCREEN];
  a = a >> 1;
  a = a >> 1;
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    a = 0x4;
    hide_enemy_column();
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    a = 0x0;
    hide_enemy_column();
  }
  a = ram[0x100 + ++sp];
  a = a >> 1;
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    a = 0x10;
    hide_enemy_row();
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a;
  if (carry_flag) {
    a = 0x8;
    hide_enemy_row();
  }
  a = ram[0x100 + ++sp];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    return;
  }
  hide_enemy_row();
  a = ram[(uint8_t)(ENEMY_KIND + x)];
  if (a == 0xc) {
    return;
  }
  a = ram[(uint8_t)(ENEMY_Y_PAGE + x)];
  if (a != 0x2) {
    return;
  }
  erase_enemy();
  return;
}

// in: x, y; out: x, y
void draw_enemy_row(void) {
  ram[0x0] = ROM(ENEMY_TILES + x);
  a = ROM((ENEMY_TILES + 1) + x);
  draw_sprite_row(); // fallthrough
  return;
}

// in: a, x, y; out: x, y, carry
void draw_sprite_row(void) {
  ram[0x1] = a;
  a = ram[0x3];
  a = a >> 1;
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = ram[0x0];
  if (carry_flag) {
    ram[(SPRITE_X + 2) + y] = a;
    ram[SPRITE_TILE + y] = ram[0x1];
    a = 0x40;
  } else {
    ram[SPRITE_TILE + y] = a;
    ram[(SPRITE_X + 2) + y] = ram[0x1];
    a = 0x0;
  }
  a |= ram[0x4];
  ram[SPRITE_ATTR + y] = a;
  ram[(SPRITE_X + 3) + y] = a;
  a = ram[0x2];
  ram[SPRITE_Y + y] = a;
  ram[(SPRITE_X + 1) + y] = a;
  a = ram[0x5];
  ram[SPRITE_X + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  a = ram[0x2];
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[0x2] = a;
  a = y;
  carry_flag = false;
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  y = a;
  x++;
  x++;
  return;
}

// in: a, x; out: y
void hide_enemy_row(void) {
  carry_flag = false;
  { uint16_t sum = a + ram[ENEMY_SPRITE_SLOT + x] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  y = a;
  a = 0xf8;
  fill_two_sprites(); return;
}

// in: a, x; out: none
void hide_enemy_column(void) {
  carry_flag = false;
  a = (uint8_t)(a + ram[ENEMY_SPRITE_SLOT + x] + carry_flag);
  y = a;
  hide_column();
  ram[(SPRITE_X + 13) + y] = a;
  return;
}

// in: x; out: x
void draw_block(void) {
  ram[0x2] = ram[BLOCK_SCREEN_Y];
  ram[0x5] = ram[BLOCK_SCREEN_X];
  a = 0x3;
  ram[0x4] = a;
  a = a >> 1;
  ram[0x3] = a;
  y = ram[BLOCK_SPRITE_SLOT + x];
  x = 0x0;
  do {
    ram[0x0] = ROM(BLOCK_TILES + x);
    a = ROM((BLOCK_TILES + 1) + x);
    draw_sprite_row();
    zero_flag = x == 0x4;
  } while (!zero_flag);
  x = ram[SLOT];
  y = ram[BLOCK_SPRITE_SLOT + x];
  a = ram[AREA_KIND];
  if (a != 0x1) {
    a = 0x86;
    ram[SPRITE_TILE + y] = a;
    ram[(SPRITE_X + 2) + y] = a;
  }
  a = ram[BLOCK_METATILE + x];
  { uint8_t m = 0xc4; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    a = 0x87;
    y++;
    fill_four_sprites();
    y--;
    a = 0x3;
    x = ram[AREA_KIND];
    x--; zero_flag = x == 0;
    if (!zero_flag) {
      a = a >> 1;
    }
    x = ram[SLOT];
    ram[SPRITE_ATTR + y] = a;
    a |= 0x40;
    ram[(SPRITE_X + 3) + y] = a;
    a |= 0x80;
    ram[(SPRITE_X + 11) + y] = a;
    a &= 0x83;
    ram[(SPRITE_X + 7) + y] = a;
  }
  a = ram[BLOCK_OFFSCREEN];
  ram[0x100 + sp--] = a;
  a &= 0x4; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0xf8;
    ram[(SPRITE_X + 1) + y] = a;
    ram[(SPRITE_X + 9) + y] = a;
  }
  a = ram[0x100 + ++sp];
  check_left_column(); // fallthrough
  return;
}

// in: a, y; out: none
void check_left_column(void) {
  a &= 0x8; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  hide_column(); // fallthrough
  return;
}

// in: y; out: a
void hide_column(void) {
  a = 0xf8;
  ram[SPRITE_Y + y] = a;
  ram[(SPRITE_X + 5) + y] = a;
  return;
}

// in: x; out: none
void draw_brick_chunks(void) {
  ram[0x0] = 0x2;
  a = 0x75;
  y = ram[PLAYER_TASK];
  { uint8_t m = 0x5; carry_flag = y >= m; zero_flag = y == m; }
  if (!zero_flag) {
    ram[0x0] = 0x3;
    a = 0x84;
  }
  y = ram[BLOCK_SPRITE_SLOT + x];
  y++;
  fill_four_sprites();
  a = ram[FRAME_COUNT];
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  a &= 0xc0;
  a |= ram[0x0];
  y++;
  fill_four_sprites();
  y--;
  y--;
  a = ram[BLOCK_SCREEN_Y];
  fill_two_sprites();
  ram[SPRITE_X + y] = ram[BLOCK_SCREEN_X];
  a = ram[BLOCK_ORIGIN_X + x];
  carry_flag = true;
  a = (uint8_t)(a - ram[SCREEN_LEFT_X] - !carry_flag);
  ram[0x0] = a;
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[BLOCK_SCREEN_X] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  a = (uint8_t)(a + 0x6 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  a = ram[(BLOCK_SCREEN_Y + 1)];
  ram[(SPRITE_X + 5) + y] = a;
  ram[(SPRITE_X + 9) + y] = a;
  ram[(SPRITE_X + 8) + y] = ram[(BLOCK_SCREEN_X + 1)];
  a = ram[0x0];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[(BLOCK_SCREEN_X + 1)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  a = (uint8_t)(a + 0x6 + carry_flag);
  ram[(SPRITE_X + 12) + y] = a;
  a = ram[BLOCK_OFFSCREEN];
  check_left_column();
  a = ram[BLOCK_OFFSCREEN];
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  if (carry_flag) {
    a = 0xf8;
    fill_two_sprites();
  }
  a = ram[0x0]; neg_flag = (a & 0x80) != 0;
  if (!neg_flag) {
    return;
  }
  a = ram[SPRITE_X + y];
  carry_flag = a >= ram[(SPRITE_X + 4) + y];
  if (!carry_flag) {
    return;
  }
  a = 0xf8;
  ram[(SPRITE_X + 1) + y] = a;
  ram[(SPRITE_X + 9) + y] = a;
  return;
}

// in: y; out: none
void draw_firebar_tile(void) {
  a = ram[FRAME_COUNT];
  a = a >> 1;
  a = a >> 1;
  ram[0x100 + sp--] = a;
  a &= 0x1;
  a ^= 0x64;
  ram[SPRITE_TILE + y] = a;
  a = ram[0x100 + ++sp];
  a = a >> 1;
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = 0x2;
  if (carry_flag) {
    a |= 0xc0;
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
  a = ram[FIREBALL_SCREEN_Y];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[SPRITE_Y + y] = a;
  ram[(SPRITE_X + 5) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 1) + y] = a;
  ram[(SPRITE_X + 9) + y] = a;
  a = ram[FIREBALL_SCREEN_X];
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[SPRITE_X + y] = a;
  ram[(SPRITE_X + 4) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 8) + y] = a;
  ram[(SPRITE_X + 12) + y] = a;
  ram[SPRITE_ATTR + y] = 0x2;
  ram[(SPRITE_X + 3) + y] = 0x82;
  ram[(SPRITE_X + 7) + y] = 0x42;
  ram[(SPRITE_X + 11) + y] = 0xc2;
  return;
}

// in: x; out: x
void draw_small_platform(void) {
  y = ram[ENEMY_SPRITE_SLOT + x];
  a = 0x5b;
  y++;
  fill_six_sprites();
  y++;
  a = 0x2;
  fill_six_sprites();
  y--;
  y--;
  a = ram[ENEMY_SCREEN_X];
  ram[SPRITE_X + y] = a;
  ram[(SPRITE_X + 12) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 4) + y] = a;
  ram[(SPRITE_X + 16) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(SPRITE_X + 8) + y] = a;
  ram[(SPRITE_X + 20) + y] = a;
  a = ram[(uint8_t)(ENEMY_Y + x)];
  x = a;
  ram[0x100 + sp--] = a;
  carry_flag = x >= 0x20;
  if (!carry_flag) {
    a = 0xf8;
  }
  fill_three_sprites();
  a = ram[0x100 + ++sp];
  carry_flag = false;
  a = (uint8_t)(a + 0x80 + carry_flag);
  x = a;
  if (x < 0x20) {
    a = 0xf8;
  }
  ram[(SPRITE_X + 9) + y] = a;
  ram[(SPRITE_X + 13) + y] = a;
  ram[(SPRITE_X + 17) + y] = a;
  a = ram[ENEMY_OFFSCREEN];
  ram[0x100 + sp--] = a;
  a &= 0x8; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0xf8;
    ram[SPRITE_Y + y] = a;
    ram[(SPRITE_X + 9) + y] = a;
  }
  a = ram[0x100 + ++sp];
  ram[0x100 + sp--] = a;
  a &= 0x4; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0xf8;
    ram[(SPRITE_X + 1) + y] = a;
    ram[(SPRITE_X + 13) + y] = a;
  }
  a = ram[0x100 + ++sp];
  a &= 0x2; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0xf8;
    ram[(SPRITE_X + 5) + y] = a;
    ram[(SPRITE_X + 17) + y] = a;
  }
  x = ram[SLOT];
  return;
}

// in: x; out: none
void draw_bubble(void) {
  y = ram[PLAYER_Y_PAGE];
  y--; zero_flag = y == 0;
  if (!zero_flag) {
    return;
  }
  a = ram[BUBBLE_OFFSCREEN];
  a &= 0x8; zero_flag = a == 0;
  if (!zero_flag) {
    return;
  }
  y = ram[BUBBLE_SPRITE_SLOT + x];
  ram[SPRITE_X + y] = ram[BUBBLE_SCREEN_X];
  ram[SPRITE_Y + y] = ram[BUBBLE_SCREEN_Y];
  ram[SPRITE_TILE + y] = 0x74;
  ram[SPRITE_ATTR + y] = 0x2;
  return;
}

void draw_player(void) {
  a = ram[INJURY_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    a = ram[FRAME_COUNT];
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) {
      return;
    }
  }
  a = ram[PLAYER_TASK];
  if (a != 0xb) {
    a = ram[PLAYER_CHANGING_SIZE]; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[SWIMMING]; zero_flag = y == 0;
      if (zero_flag) {
        draw_player_action();
        return;
      }
      a = ram[PLAYER_STATE];
      if (a == 0x0) {
        draw_player_action();
        return;
      }
      draw_player_action();
      a = ram[FRAME_COUNT];
      a &= 0x4; zero_flag = a == 0;
      if (!zero_flag) {
        return;
      }
      x = a;
      y = ram[PLAYER_SPRITE_SLOT];
      a = ram[PLAYER_FACING];
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (!carry_flag) {
        y++;
        y++;
        y++;
        y++;
      }
      a = ram[PLAYER_SMALL]; zero_flag = a == 0;
      if (!zero_flag) {
        a = ram[(SPRITE_X + 22) + y];
        zero_flag = a == ROM((PLAYER_TILES + 158));
        if (zero_flag) {
          return;
        }
        x++;
      }
      ram[(SPRITE_X + 22) + y] = ROM(SWIM_KICK_TILES + x);
      return;
    }
    animate_size_change();
    draw_player_with_offset(); return;
  }
  y = 0xe;
  a = ROM(PLAYER_TILE_OFFSETS + y);
  draw_player_with_offset(); // fallthrough
  return;
}

void draw_player_action(void) {
  player_action_offset();
  draw_player_with_offset(); return;
}

// in: a; out: none
void draw_player_with_offset(void) {
  ram[PLAYER_GFX_OFFSET] = a;
  a = 0x4;
  render_player();
  fix_player_attributes();
  a = ram[FIREBALL_THROW_TIMER]; zero_flag = a == 0;
  if (!zero_flag) {
    y = 0x0;
    a = ram[PLAYER_ANIM_TIMER];
    carry_flag = a >= ram[FIREBALL_THROW_TIMER];
    ram[FIREBALL_THROW_TIMER] = y;
    if (carry_flag) { goto loc_ef7a; }
    ram[FIREBALL_THROW_TIMER] = a;
    y = 0x7;
    ram[PLAYER_GFX_OFFSET] = ROM(PLAYER_TILE_OFFSETS + y);
    y = 0x4;
    a = ram[PLAYER_X_SPEED];
    a |= ram[BUTTONS_HORIZONTAL]; zero_flag = a == 0;
    if (!zero_flag) {
      y--;
    }
    a = y;
    render_player();
  }
  
loc_ef7a:
  a = ram[PLAYER_OFFSCREEN];
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a;
  x = 0x3;
  a = ram[PLAYER_SPRITE_SLOT];
  carry_flag = false;
  a = (uint8_t)(a + 0x18 + carry_flag);
  y = a;
  do {
    a = 0xf8;
    carry_flag = (ram[0x0] & 1) != 0; ram[0x0] = ram[0x0] >> 1;
    if (carry_flag) {
      fill_two_sprites();
    }
    a = y;
    carry_flag = true;
    a = (uint8_t)(a - 0x8 - !carry_flag);
    y = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  return;
}

void draw_lives_screen_player(void) {
  x = 0x5;
  do {
    ram[(uint8_t)(0x2 + x)] = ROM(LIVES_SCREEN_PLAYER + x);
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  x = 0xb8;
  y = 0x4;
  draw_player_rows();
  a = ram[(SPRITE_X + 35)];
  a |= 0x40;
  ram[(SPRITE_X + 31)] = a;
  return;
}

// in: a; out: none
void render_player(void) {
  ram[0x7] = a;
  a = ram[PLAYER_SCREEN_X];
  ram[PLAYER_SCROLL_POS] = a;
  ram[0x5] = a;
  ram[0x2] = ram[PLAYER_SCREEN_Y];
  ram[0x3] = ram[PLAYER_FACING];
  ram[0x4] = ram[PLAYER_SPRITE_ATTR];
  x = ram[PLAYER_GFX_OFFSET];
  y = ram[PLAYER_SPRITE_SLOT];
  draw_player_rows(); // fallthrough
  return;
}

// in: x, y; out: none
void draw_player_rows(void) {
  do {
    ram[0x0] = ROM(PLAYER_TILES + x);
    a = ROM((PLAYER_TILES + 1) + x);
    draw_sprite_row();
    ram[0x7]--; zero_flag = ram[0x7] == 0;
  } while (!zero_flag);
  return;
}

// in: none; out: a
void player_action_offset(void) {
  a = ram[PLAYER_STATE];
  zero_flag = a == 0x3;
  if (zero_flag) { goto loc_f044; }
  zero_flag = a == 0x2;
  if (zero_flag) { goto loc_f034; }
  if (a == 0x1) {
    a = ram[SWIMMING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f050; }
    y = 0x6;
    a = ram[CROUCHING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f028; }
    y = 0x0;
  } else {
    y = 0x6;
    a = ram[CROUCHING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f028; }
    y = 0x2;
    a = ram[PLAYER_X_SPEED];
    a |= ram[BUTTONS_HORIZONTAL]; zero_flag = a == 0;
    if (zero_flag) { goto loc_f028; }
    a = ram[PLAYER_ABS_SPEED];
    carry_flag = a >= 0x9;
    if (!carry_flag) { goto loc_f03c; }
    a = ram[PLAYER_DIR];
    a &= ram[PLAYER_FACING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f03c; }
    y++;
  }
  
loc_f028:
  size_offset();
  ram[PLAYER_ANIM_FRAME] = 0x0;
  a = ROM(PLAYER_TILE_OFFSETS + y);
  return;
  
loc_f034:
  y = 0x4;
  size_offset();
  animate_player(); return;
  
loc_f03c:
  y = 0x4;
  size_offset();
  goto loc_f068;
  
loc_f044:
  y = 0x5;
  a = ram[PLAYER_Y_SPEED]; zero_flag = a == 0;
  if (zero_flag) { goto loc_f028; }
  size_offset();
  goto loc_f06d;
  
loc_f050:
  y = 0x1;
  size_offset();
  a = ram[JUMP_SWIM_TIMER];
  a |= ram[PLAYER_ANIM_FRAME]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[BUTTONS_AB];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) { goto loc_f068; }
    animate_player(); // fallthrough
    return;
  }
  
loc_f068:
  a = 0x3;
  goto loc_f06f;
  
loc_f06d:
  a = 0x2;
  
loc_f06f:
  ram[0x0] = a;
  animate_player();
  ram[0x100 + sp--] = a;
  a = ram[PLAYER_ANIM_TIMER]; zero_flag = a == 0;
  if (zero_flag) {
    ram[PLAYER_ANIM_TIMER] = ram[PLAYER_ANIM_PERIOD];
    a = ram[PLAYER_ANIM_FRAME];
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    carry_flag = a >= ram[0x0];
    if (carry_flag) {
      a = 0x0;
    }
    ram[PLAYER_ANIM_FRAME] = a;
  }
  a = ram[0x100 + ++sp];
  return;
}

// in: y; out: a
void animate_player(void) {
  a = ram[PLAYER_ANIM_FRAME];
  animation_offset(); return;
}

// in: y; out: y
void size_offset(void) {
  a = ram[PLAYER_SMALL]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  a = y;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag);
  y = a;
  return;
}

// in: none; out: a
void animate_size_change(void) {
  y = ram[PLAYER_ANIM_FRAME];
  a = ram[FRAME_COUNT];
  a &= 0x3; zero_flag = a == 0;
  if (zero_flag) {
    y++;
    if (y >= 0xa) {
      y = 0x0;
      ram[PLAYER_CHANGING_SIZE] = y;
    }
    ram[PLAYER_ANIM_FRAME] = y;
  }
  a = ram[PLAYER_SMALL]; zero_flag = a == 0;
  if (zero_flag) {
    a = ROM(CHANGE_SIZE_FRAMES + y);
    y = 0xf;
    animation_offset(); // fallthrough
    return;
  }
  a = y;
  carry_flag = false;
  a = (uint8_t)(a + 0xa + carry_flag);
  x = a;
  y = 0x9;
  a = ROM(CHANGE_SIZE_FRAMES + x); zero_flag = a == 0;
  if (zero_flag) {
    y = 0x1;
  }
  a = ROM(PLAYER_TILE_OFFSETS + y);
  return;
}

// in: a, y; out: a
void animation_offset(void) {
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  a = (uint8_t)(a + ROM(PLAYER_TILE_OFFSETS + y) + carry_flag);
  return;
}

void fix_player_attributes(void) {
  y = ram[PLAYER_SPRITE_SLOT];
  a = ram[PLAYER_TASK];
  if (a != 0xb) {
    a = ram[PLAYER_GFX_OFFSET];
    zero_flag = a == 0x50;
    if (zero_flag) { goto loc_f117; }
    zero_flag = a == 0xb8;
    if (zero_flag) { goto loc_f117; }
    zero_flag = a == 0xc0;
    if (zero_flag) { goto loc_f117; }
    if (a != 0xc8) {
      return;
    }
  }
  a = ram[(SPRITE_X + 15) + y];
  a &= 0x3f;
  ram[(SPRITE_X + 15) + y] = a;
  a = ram[(SPRITE_X + 19) + y];
  a &= 0x3f;
  a |= 0x40;
  ram[(SPRITE_X + 19) + y] = a;
  
loc_f117:
  a = ram[(SPRITE_X + 23) + y];
  a &= 0x3f;
  ram[(SPRITE_X + 23) + y] = a;
  a = ram[(SPRITE_X + 27) + y];
  a &= 0x3f;
  a |= 0x40;
  ram[(SPRITE_X + 27) + y] = a;
  return;
}

// in: none; out: y
void player_screen_position(void) {
  x = 0x0;
  y = 0x0;
  screen_position_restore(); return;
}

// in: x; out: x
void bubble_screen_position(void) {
  y = 0x1;
  object_slot();
  y = 0x3;
  screen_position_restore(); return;
}

// in: x; out: x
void fireball_screen_position(void) {
  y = 0x0;
  object_slot();
  y = 0x2;
  screen_position_restore(); // fallthrough
  return;
}

// in: x, y; out: x
void screen_position_restore(void) {
  screen_position();
  x = ram[SLOT];
  return;
}

// in: x; out: x
void misc_screen_position(void) {
  y = 0x2;
  object_slot();
  y = 0x6;
  screen_position_restore(); return;
}

// in: x; out: a, x, y, carry
void enemy_screen_position(void) {
  a = 0x1;
  y = 0x1;
  screen_position_at(); return;
}

// in: x; out: x
void block_screen_position(void) {
  a = 0x9;
  y = 0x4;
  screen_position_at();
  x++;
  x++;
  a = 0x9;
  y++;
  screen_position_at(); // fallthrough
  return;
}

// in: a, x, y; out: a, x, carry
void screen_position_at(void) {
  ram[0x0] = x;
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  x = a;
  screen_position();
  x = ram[SLOT];
  return;
}

// coordinates relative to the left edge of the screen
// in: x, y; out: a, carry
void screen_position(void) {
  ram[OBJ_SCREEN_Y + y] = ram[(uint8_t)(OBJ_Y + x)];
  a = ram[(uint8_t)(OBJ_X + x)];
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[SCREEN_LEFT_X] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[OBJ_SCREEN_X + y] = a;
  return;
}

void player_offscreen_bits(void) {
  x = 0x0;
  y = 0x0;
  offscreen_bits(); return;
}

// in: x; out: x
void fireball_offscreen_bits(void) {
  y = 0x0;
  object_slot();
  y = 0x2;
  offscreen_bits(); return;
}

// in: x; out: x
void bubble_offscreen_bits(void) {
  y = 0x1;
  object_slot();
  y = 0x3;
  offscreen_bits(); return;
}

// in: x; out: x
void misc_offscreen_bits(void) {
  y = 0x2;
  object_slot();
  y = 0x6;
  offscreen_bits(); return;
}

// in: x, y; out: x
void object_slot(void) {
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + ROM(OBJECT_SLOT_OFFSETS + y) + carry_flag);
  x = a;
  return;
}

// in: x; out: x, y
void enemy_offscreen_bits(void) {
  a = 0x1;
  y = 0x1;
  offset_slot(); return;
}

// in: x; out: x
void block_offscreen_bits(void) {
  a = 0x9;
  y = 0x4;
  offset_slot(); // fallthrough
  return;
}

// in: a, x, y; out: x, y
void offset_slot(void) {
  ram[0x0] = x;
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag);
  x = a;
  offscreen_bits(); // fallthrough
  return;
}

// in: x, y; out: x, y
void offscreen_bits(void) {
  a = y;
  ram[0x100 + sp--] = a;
  offscreen_bits_parts();
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a |= ram[0x0];
  ram[0x0] = a;
  a = ram[0x100 + ++sp];
  y = a;
  ram[OBJ_OFFSCREEN + y] = ram[0x0];
  x = ram[SLOT];
  return;
}

// in: x; out: a
void offscreen_bits_parts(void) {
  compute_x_offscreen_bits();
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a;
  ram[0x4] = x;
  y = 0x1;
  do {
    a = ROM(HIGH_POSITION_UNITS + y);
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(OBJ_Y + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[0x7] = a;
    a = 0x1;
    a = (uint8_t)(a - ram[(uint8_t)(OBJ_Y_PAGE + x)] - !carry_flag);
    x = ROM(Y_EDGE_DEFAULTS + y);
    neg_flag = ((a - 0x0) & 0x80) != 0;
    if (!neg_flag) {
      x = ROM((Y_EDGE_DEFAULTS + 1) + y);
      neg_flag = ((a - 0x1) & 0x80) != 0;
      if (!neg_flag) { goto loc_f260; }
      ram[0x6] = 0x20;
      a = 0x4;
      edge_distance_bits();
    }
    
loc_f260:
    a = ROM(Y_OFFSCREEN_BITS + x);
    x = ram[0x4];
    if (a != 0x0) {
      return;
    }
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: x; out: a, x
void compute_x_offscreen_bits(void) {
  ram[0x4] = x;
  y = 0x1;
  do {
    a = ram[(SCREEN_LEFT_X + y) & 0x7ff];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(OBJ_X + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[0x7] = a;
    a = ram[(SCREEN_LEFT_PAGE + y) & 0x7ff];
    a = (uint8_t)(a - ram[(uint8_t)(OBJ_PAGE + x)] - !carry_flag);
    x = ROM(X_EDGE_DEFAULTS + y);
    neg_flag = ((a - 0x0) & 0x80) != 0;
    if (!neg_flag) {
      x = ROM((X_EDGE_DEFAULTS + 1) + y);
      neg_flag = ((a - 0x1) & 0x80) != 0;
      if (!neg_flag) { goto loc_f21e; }
      ram[0x6] = 0x38;
      a = 0x8;
      edge_distance_bits();
    }
    
loc_f21e:
    a = ROM(X_OFFSCREEN_BITS + x);
    x = ram[0x4];
    if (a != 0x0) {
      return;
    }
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  return;
}

// in: a, y; out: x
void edge_distance_bits(void) {
  ram[0x5] = a;
  a = ram[0x7];
  carry_flag = a >= ram[0x6];
  if (carry_flag) {
    return;
  }
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  a &= 0x7;
  carry_flag = y >= 0x1;
  if (!carry_flag) {
    a = (uint8_t)(a + ram[0x5] + carry_flag);
  }
  x = a;
  return;
}

// per-frame sound engine entry point
void update_sound(void) {
  a = ram[GAME_MODE]; zero_flag = a == 0;
  if (zero_flag) {
    apu_write(APU_STATUS, a);
    return;
  }
  apu_write(JOYPAD2_PORT, 0xff);
  apu_write(APU_STATUS, 0xf);
  a = ram[SOUND_PAUSED]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[PAUSE_SOUND_REQUEST];
    { uint8_t m = 0x1; carry_flag = a >= m; zero_flag = a == m; }
    if (!zero_flag) { goto loc_f34b; }
  }
  a = ram[PAUSE_SOUND_PLAYING]; zero_flag = a == 0;
  if (!zero_flag) { goto loc_f316; }
  a = ram[PAUSE_SOUND_REQUEST]; zero_flag = a == 0;
  if (zero_flag) { goto loc_f35d; }
  ram[PAUSE_SOUND_PLAYING] = a;
  ram[SOUND_PAUSED] = a;
  a = 0x0;
  apu_write(APU_STATUS, a);
  ram[PULSE1_SFX_PLAYING] = a;
  ram[PULSE2_SFX_PLAYING] = a;
  ram[NOISE_SFX_PLAYING] = a;
  apu_write(APU_STATUS, 0xf);
  ram[PULSE1_SFX_COUNTER] = 0x2a;
  
loc_f312:
  a = 0x44;
  goto loc_f327;
  
loc_f316:
  a = ram[PULSE1_SFX_COUNTER];
  { uint8_t m = 0x24; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) {
    { uint8_t m = 0x1e; carry_flag = a >= m; zero_flag = a == m; }
    if (zero_flag) { goto loc_f312; }
    { uint8_t m = 0x18; carry_flag = a >= m; zero_flag = a == m; }
    if (!zero_flag) { goto loc_f32e; }
  }
  a = 0x64;
  
loc_f327:
  x = 0x84;
  y = 0x7f; zero_flag = y == 0;
  play_square1();
  
loc_f32e:
  ram[PULSE1_SFX_COUNTER]--; zero_flag = ram[PULSE1_SFX_COUNTER] == 0;
  if (!zero_flag) { goto loc_f35d; }
  apu_write(APU_STATUS, 0x0);
  a = ram[PAUSE_SOUND_PLAYING];
  if (a == 0x2) {
    ram[SOUND_PAUSED] = 0x0;
  }
  ram[PAUSE_SOUND_PLAYING] = 0x0;
  goto loc_f35d;
  
loc_f34b:
  square1_sfx();
  square2_sfx();
  noise_sfx();
  update_music();
  a = 0x0;
  ram[AREA_MUSIC_REQUEST] = a;
  ram[EVENT_MUSIC_REQUEST] = a;
  
loc_f35d:
  a = 0x0;
  ram[PULSE1_SFX_REQUEST] = a;
  ram[PULSE2_SFX_REQUEST] = a;
  ram[NOISE_SFX_REQUEST] = a;
  ram[PAUSE_SOUND_REQUEST] = a;
  y = ram[DMC_COUNTER];
  a = ram[AREA_MUSIC_PLAYING];
  a &= 0x3; zero_flag = a == 0;
  if (!zero_flag) {
    ram[DMC_COUNTER]++;
    carry_flag = y >= 0x30;
    if (!carry_flag) { goto loc_f37d; }
  }
  a = y; zero_flag = a == 0;
  if (!zero_flag) {
    ram[DMC_COUNTER]--;
  }
  
loc_f37d:
  apu_write((APU + 17), y);
  return;
}

// in: x, y; out: none
void set_square1_regs(void) {
  apu_write((APU + 1), y);
  apu_write(APU_PULSE1, x);
  return;
}

// in: a, x, y; out: a, x, zero
void play_square1(void) {
  set_square1_regs();
  set_square1_freq(); // fallthrough
  return;
}

// in: a; out: a, x, y, zero
void set_square1_freq(void) {
  x = 0x0;
  set_freq(); // fallthrough
  return;
}

// in: a, x; out: a, y, zero
void set_freq(void) {
  y = a;
  a = read_byte((uint16_t)((NOTE_PERIODS + 1) + y)); zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  dynamic_ram_write((uint16_t)((APU + 2) + x), a);
  a = ROM(NOTE_PERIODS + y);
  a |= 0x8; zero_flag = a == 0;
  dynamic_ram_write((uint16_t)((APU + 3) + x), a);
  return;
}

// in: x, y; out: none
void set_square2_regs(void) {
  apu_write(APU_PULSE2, x);
  apu_write((APU + 5), y);
  return;
}

// in: a, x, y; out: a, x
void play_square2(void) {
  set_square2_regs();
  set_square2_freq(); // fallthrough
  return;
}

// in: a; out: a, x, y, zero
void set_square2_freq(void) {
  x = 0x4;
  set_freq(); return;
}

// in: a; out: none
void set_triangle_freq(void) {
  x = 0x8;
  set_freq(); return;
}

// in: none; out: x, carry
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
  y = 0xa7; zero_flag = y == 0;
  play_square1();
  ram[PULSE1_SFX_COUNTER] = 0x28;
  
loc_f3df:
  a = ram[PULSE1_SFX_COUNTER];
  { uint8_t m = 0x25; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    x = 0x5f;
    y = 0xf6; zero_flag = y == 0;
    goto loc_f3f4;
  }
  { uint8_t m = 0x20; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) { goto loc_f419; }
  x = 0x48;
  
loc_f3f2:
  y = 0xbc; zero_flag = y == 0;
  
loc_f3f4:
  set_square1_regs();
  if (!zero_flag) { goto loc_f419; }
  
loc_f3f9:
  a = 0x5;
  y = 0x99;
  goto loc_f403;
  
loc_f3ff:
  a = 0xa;
  y = 0x93;
  
loc_f403:
  x = 0x9e;
  ram[PULSE1_SFX_COUNTER] = a;
  a = 0xc; zero_flag = a == 0;
  play_square1();
  
loc_f40d:
  a = ram[PULSE1_SFX_COUNTER];
  { uint8_t m = 0x6; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    a = 0xbb;
    apu_write((APU + 1), a);
  }
  
loc_f419:
  goto loc_f47b;
  
square1_sfx:
  y = ram[PULSE1_SFX_REQUEST]; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  if (!zero_flag) {
    ram[PULSE1_SFX_PLAYING] = y;
    if (neg_flag) { goto loc_f3cd; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f3d1; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f3ff; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f45b; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f47d; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f4b6; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f3f9; }
    carry_flag = (ram[PULSE1_SFX_REQUEST] & 1) != 0; ram[PULSE1_SFX_REQUEST] = ram[PULSE1_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f3bf; }
  }
  a = ram[PULSE1_SFX_PLAYING]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  if (zero_flag) {
    return;
  }
  if (neg_flag) { goto loc_f3df; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f3df; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f40d; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f469; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f48d; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f4bb; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f40d; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f4a2; }
  return;
  
loc_f45b:
  ram[PULSE1_SFX_COUNTER] = 0xe;
  y = 0x9c;
  x = 0x9e;
  a = 0x26; zero_flag = a == 0;
  play_square1();
  
loc_f469:
  y = ram[PULSE1_SFX_COUNTER];
  a = ROM((SWIM_STOMP_ENVELOPE - 1) + y);
  apu_write(APU_PULSE1, a);
  { uint8_t m = 0x6; carry_flag = y >= m; zero_flag = y == m; }
  if (zero_flag) {
    a = 0x9e;
    apu_write((APU + 2), a);
  }
  
loc_f47b:
  goto loc_f4a2;
  
loc_f47d:
  a = 0xe;
  y = 0xcb;
  x = 0x9f;
  ram[PULSE1_SFX_COUNTER] = a;
  a = 0x28; zero_flag = a == 0;
  play_square1();
  if (!zero_flag) { goto loc_f4a2; }
  
loc_f48d:
  y = ram[PULSE1_SFX_COUNTER];
  { uint8_t m = 0x8; carry_flag = y >= m; zero_flag = y == m; }
  if (zero_flag) {
    apu_write((APU + 2), 0xa0);
    a = 0x9f;
  } else {
    a = 0x90;
  }
  apu_write(APU_PULSE1, a);
  
loc_f4a2:
  ram[PULSE1_SFX_COUNTER]--; zero_flag = ram[PULSE1_SFX_COUNTER] == 0;
  if (!zero_flag) {
    return;
  }
  stop_square1_sfx(); // fallthrough
  return;
  
loc_f4b6:
  ram[PULSE1_SFX_COUNTER] = 0x2f;
  
loc_f4bb:
  a = ram[PULSE1_SFX_COUNTER];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f4d1; }
    a &= 0x2; zero_flag = a == 0;
    if (zero_flag) { goto loc_f4d1; }
    y = 0x91;
    x = 0x9a;
    a = 0x44; zero_flag = a == 0;
    play_square1();
  }
  
loc_f4d1:
  goto loc_f4a2;
}

// in: none; out: x
void stop_square1_sfx(void) {
  ram[PULSE1_SFX_PLAYING] = 0x0;
  apu_write(APU_STATUS, 0xe);
  x = 0xf;
  apu_write(APU_STATUS, x);
  return;
}

// in: none; out: x
void stop_square2_sfx(void) {
  apu_write(APU_STATUS, 0xd);
  x = 0xf;
  apu_write(APU_STATUS, x);
  return;
}

// in: none; out: x, carry
void square2_sfx(void) {
  goto square2_sfx;
  
loc_f518:
  a = 0x35;
  x = 0x8d;
  goto loc_f522;
  
loc_f51e:
  a = 0x6;
  x = 0x98;
  
loc_f522:
  ram[PULSE2_SFX_COUNTER] = a;
  y = 0x7f;
  a = 0x42;
  play_square2();
  
loc_f52c:
  a = ram[PULSE2_SFX_COUNTER];
  { uint8_t m = 0x30; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    a = 0x54;
    apu_write((APU + 6), a);
  }
  goto loc_f568;
  
loc_f53a:
  ram[PULSE2_SFX_COUNTER] = 0x20;
  y = 0x94;
  a = 0x5e;
  goto loc_f550;
  
loc_f545:
  a = ram[PULSE2_SFX_COUNTER];
  { uint8_t m = 0x18; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) { goto loc_f568; }
  y = 0x93;
  a = 0x18;
  
loc_f550:
  goto loc_f5d1;
  
loc_f552:
  ram[PULSE2_SFX_COUNTER] = 0x36;
  
loc_f557:
  a = ram[PULSE2_SFX_COUNTER];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f568; }
  y = a;
  a = ROM((EXTRA_LIFE_NOTES + 5) + y);
  x = 0x5d;
  y = 0x7f;
  
loc_f565:
  play_square2();
  
loc_f568:
  ram[PULSE2_SFX_COUNTER]--; zero_flag = ram[PULSE2_SFX_COUNTER] == 0;
  if (!zero_flag) {
    return;
  }
  
loc_f56d:
  ram[PULSE2_SFX_PLAYING] = 0x0;
  stop_square2_sfx(); // fallthrough
  return;
  
square2_sfx:
  a = ram[PULSE2_SFX_PLAYING];
  a &= 0x40; zero_flag = a == 0;
  if (!zero_flag) { goto loc_f5e7; }
  y = ram[PULSE2_SFX_REQUEST]; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  if (!zero_flag) {
    ram[PULSE2_SFX_PLAYING] = y;
    if (neg_flag) { goto loc_f5c8; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f518; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f5fc; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f600; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f53a; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f51e; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f552; }
    carry_flag = (ram[PULSE2_SFX_REQUEST] & 1) != 0; ram[PULSE2_SFX_REQUEST] = ram[PULSE2_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f5e2; }
  }
  a = ram[PULSE2_SFX_PLAYING]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  if (zero_flag) {
    return;
  }
  if (neg_flag) { goto loc_f5d3; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) {
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f60f; }
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f60f; }
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f545; }
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f5c2; }
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f557; }
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f5e7; }
    return;
  }
  
loc_f5c2:
  goto loc_f52c;
  
loc_f5c5:
  goto loc_f568;
  
loc_f5c8:
  ram[PULSE2_SFX_COUNTER] = 0x38;
  y = 0xc4;
  a = 0x18;
  
loc_f5d1:
  goto loc_f5de;
  
loc_f5d3:
  a = ram[PULSE2_SFX_COUNTER];
  { uint8_t m = 0x8; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) { goto loc_f568; }
  y = 0xa4;
  a = 0x5a;
  
loc_f5de:
  x = 0x9f;
  
loc_f5e0:
  goto loc_f565;
  
loc_f5e2:
  ram[PULSE2_SFX_COUNTER] = 0x30;
  
loc_f5e7:
  a = ram[PULSE2_SFX_COUNTER];
  x = 0x3;
  do {
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto loc_f5c5; }
    x--; zero_flag = x == 0;
  } while (!zero_flag);
  y = a;
  a = ROM((EXTRA_LIFE_NOTES - 1) + y);
  x = 0x82;
  y = 0x7f;
  goto loc_f5e0;
  
loc_f5fc:
  a = 0x10;
  goto loc_f602;
  
loc_f600:
  a = 0x20;
  
loc_f602:
  ram[PULSE2_SFX_COUNTER] = a;
  apu_write((APU + 5), 0x7f);
  ram[SFX_STEP] = 0x0;
  
loc_f60f:
  ram[SFX_STEP]++;
  a = ram[SFX_STEP];
  a = a >> 1;
  y = a;
  { uint8_t m = ram[PULSE2_SFX_COUNTER]; carry_flag = y >= m; zero_flag = y == m; }
  if (!zero_flag) {
    apu_write(APU_PULSE2, 0x9d);
    a = ROM(POWERUP_REVEAL_NOTES + y);
    set_square2_freq();
    return;
  }
  goto loc_f56d;
}

// in: none; out: x, carry
void noise_sfx(void) {
  goto noise_sfx;
  
loc_f63b:
  ram[NOISE_SFX_COUNTER] = 0x20;
  
loc_f640:
  a = ram[NOISE_SFX_COUNTER];
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (!carry_flag) { goto loc_f658; }
  y = a;
  x = ROM(BRICK_SHATTER_NOTES + y);
  a = read_byte((uint16_t)(BRICK_SHATTER_ENVELOPE + y));
  
loc_f64d:
  apu_write(APU_NOISE, a);
  apu_write((APU + 14), x);
  apu_write((APU + 15), 0x18);
  
loc_f658:
  ram[NOISE_SFX_COUNTER]--; zero_flag = ram[NOISE_SFX_COUNTER] == 0;
  if (!zero_flag) {
    return;
  }
  apu_write(APU_NOISE, 0xf0);
  ram[NOISE_SFX_PLAYING] = 0x0;
  return;
  
noise_sfx:
  y = ram[NOISE_SFX_REQUEST]; zero_flag = y == 0;
  if (!zero_flag) {
    ram[NOISE_SFX_PLAYING] = y;
    carry_flag = (ram[NOISE_SFX_REQUEST] & 1) != 0; ram[NOISE_SFX_REQUEST] = ram[NOISE_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f63b; }
    carry_flag = (ram[NOISE_SFX_REQUEST] & 1) != 0; ram[NOISE_SFX_REQUEST] = ram[NOISE_SFX_REQUEST] >> 1;
    if (carry_flag) { goto loc_f680; }
  }
  a = ram[NOISE_SFX_PLAYING]; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f640; }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto loc_f685; }
  return;
  
loc_f680:
  ram[NOISE_SFX_COUNTER] = 0x40;
  
loc_f685:
  a = ram[NOISE_SFX_COUNTER];
  carry_flag = (a & 1) != 0; a = a >> 1;
  y = a;
  x = 0xf;
  a = read_byte((uint16_t)((WATER_MUSIC_ENVELOPE + 39) + y)); zero_flag = a == 0;
  if (!zero_flag) { goto loc_f64d; }
  continue_music(); // fallthrough
  return;
}

// in: none; out: x, carry
void continue_music(void) {
  goto loc_f73a;
  
loc_f6a4:
  ram[EVENT_MUSIC_PLAYING] = a;
  { uint8_t m = 0x1; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    stop_square1_sfx();
    stop_square2_sfx();
  }
  x = ram[AREA_MUSIC_PLAYING];
  ram[AREA_MUSIC_SAVED] = x;
  y = 0x0;
  ram[NOTE_LENGTH_ADD] = y;
  ram[AREA_MUSIC_PLAYING] = y;
  zero_flag = a == 0x40;
  if (!zero_flag) { goto loc_f6f1; }
  x = 0x8;
  ram[NOTE_LENGTH_ADD] = x;
  goto loc_f6f1;
  
loc_f6d1:
  ram[GROUND_MUSIC_HEADER] = y;
  
loc_f6d4:
  ram[EVENT_MUSIC_PLAYING] = 0x0;
  ram[AREA_MUSIC_PLAYING] = a;
  if (a == 0x1) {
    ram[GROUND_MUSIC_HEADER]++;
    y = ram[GROUND_MUSIC_HEADER];
    { uint8_t m = 0x32; carry_flag = y >= m; zero_flag = y == m; }
    if (!zero_flag) { goto loc_f6f5; }
    y = 0x11;
    goto loc_f6d1;
  }
  y = 0x8;
  ram[PULSE2_MUSIC_POS] = y;
  do {
    
loc_f6f1:
    y++;
    carry_flag = (a & 1) != 0; a = a >> 1;
  } while (!carry_flag);
  
loc_f6f5:
  a = ROM((SONG_HEADER_OFFSETS - 1) + y);
  y = a;
  ram[NOTE_LENGTH_OFFSET] = ROM(SONG_HEADER_OFFSETS + y);
  ram[MUSIC_PTR] = ROM((SONG_HEADER_OFFSETS + 1) + y);
  ram[(MUSIC_PTR + 1)] = ROM((SONG_HEADER_OFFSETS + 2) + y);
  ram[TRIANGLE_MUSIC_POS] = ROM((SONG_HEADER_OFFSETS + 3) + y);
  ram[PULSE1_MUSIC_POS] = ROM((SONG_HEADER_OFFSETS + 4) + y);
  a = ROM((SONG_HEADER_OFFSETS + 5) + y);
  ram[NOISE_MUSIC_POS] = a;
  ram[NOISE_LOOP_POS] = a;
  a = 0x1;
  ram[PULSE2_NOTE_COUNTER] = a;
  ram[PULSE1_NOTE_COUNTER] = a;
  ram[TRIANGLE_NOTE_COUNTER] = a;
  ram[NOISE_BEAT_COUNTER] = a;
  a = 0x0;
  ram[PULSE2_MUSIC_POS] = a;
  ram[ALT_REGISTERS] = a;
  apu_write(APU_STATUS, 0xb);
  apu_write(APU_STATUS, 0xf);
  
loc_f73a:
  ram[PULSE2_NOTE_COUNTER]--; zero_flag = ram[PULSE2_NOTE_COUNTER] == 0;
  if (zero_flag) {
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    if (!zero_flag) {
      if (!neg_flag) { goto loc_f786; }
    } else {
      a = ram[EVENT_MUSIC_PLAYING];
      { uint8_t m = 0x40; carry_flag = a >= m; zero_flag = a == m; }
      if (zero_flag) {
        a = ram[AREA_MUSIC_SAVED]; zero_flag = a == 0;
        if (!zero_flag) { goto loc_f774; }
      }
      a &= 0x4; zero_flag = a == 0;
      if (!zero_flag) { goto loc_f777; }
      a = ram[AREA_MUSIC_PLAYING];
      a &= 0x5f; zero_flag = a == 0;
      if (zero_flag) {
        a = 0x0;
        ram[AREA_MUSIC_PLAYING] = a;
        ram[EVENT_MUSIC_PLAYING] = a;
        apu_write(APU_TRIANGLE, a);
        a = 0x90;
        apu_write(APU_PULSE1, a);
        apu_write(APU_PULSE2, a);
        return;
      }
      
loc_f774:
      goto loc_f6d4;
      
loc_f777:
      goto loc_f6a4;
    }
    note_length();
    ram[PULSE2_NOTE_LENGTH] = a;
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    
loc_f786:
    x = ram[PULSE2_SFX_PLAYING]; zero_flag = x == 0;
    if (zero_flag) {
      set_square2_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE2_ENVELOPE] = a;
      set_square2_regs();
    }
    ram[PULSE2_NOTE_COUNTER] = ram[PULSE2_NOTE_LENGTH];
  }
  a = ram[PULSE2_SFX_PLAYING]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[EVENT_MUSIC_PLAYING];
    a &= 0x91; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f7bc; }
    y = ram[PULSE2_ENVELOPE]; zero_flag = y == 0;
    if (!zero_flag) {
      ram[PULSE2_ENVELOPE]--;
    }
    load_envelope_entry();
    apu_write(APU_PULSE2, a);
    x = 0x7f;
    apu_write((APU + 5), x);
  }
  
loc_f7bc:
  y = ram[PULSE1_MUSIC_POS]; zero_flag = y == 0;
  if (!zero_flag) {
    ram[PULSE1_NOTE_COUNTER]--; zero_flag = ram[PULSE1_NOTE_COUNTER] == 0;
    if (zero_flag) {
      
loc_f7c5:
      y = ram[PULSE1_MUSIC_POS];
      ram[PULSE1_MUSIC_POS]++;
      a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0;
      if (zero_flag) {
        apu_write(APU_PULSE1, 0x83);
        a = 0x94;
        apu_write((APU + 1), a);
        ram[ALT_REGISTERS] = a;
        goto loc_f7c5;
      }
      alternate_length();
      ram[PULSE1_NOTE_COUNTER] = a;
      y = ram[PULSE1_SFX_PLAYING]; zero_flag = y == 0;
      if (!zero_flag) { goto loc_f81a; }
      a = x;
      a &= 0x3e;
      set_square1_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE1_ENVELOPE] = a;
      set_square1_regs();
    }
    a = ram[PULSE1_SFX_PLAYING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f81a; }
    a = ram[EVENT_MUSIC_PLAYING];
    a &= 0x91; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[PULSE1_ENVELOPE]; zero_flag = y == 0;
      if (!zero_flag) {
        ram[PULSE1_ENVELOPE]--;
      }
      load_envelope_entry();
      apu_write(APU_PULSE1, a);
    }
    a = ram[ALT_REGISTERS]; zero_flag = a == 0;
    if (zero_flag) {
      a = 0x7f;
    }
    apu_write((APU + 1), a);
  }
  
loc_f81a:
  a = ram[TRIANGLE_MUSIC_POS];
  ram[TRIANGLE_NOTE_COUNTER]--; zero_flag = ram[TRIANGLE_NOTE_COUNTER] == 0;
  if (zero_flag) {
    y = ram[TRIANGLE_MUSIC_POS];
    ram[TRIANGLE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    if (!zero_flag) {
      if (neg_flag) {
        note_length();
        ram[TRIANGLE_NOTE_LENGTH] = a;
        apu_write(APU_TRIANGLE, 0x1f);
        y = ram[TRIANGLE_MUSIC_POS];
        ram[TRIANGLE_MUSIC_POS]++;
        a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0;
        if (zero_flag) { goto loc_f86a; }
      }
      set_triangle_freq();
      x = ram[TRIANGLE_NOTE_LENGTH];
      ram[TRIANGLE_NOTE_COUNTER] = x;
      a = ram[EVENT_MUSIC_PLAYING];
      a &= 0x6e; zero_flag = a == 0;
      if (zero_flag) {
        a = ram[AREA_MUSIC_PLAYING];
        a &= 0xa; zero_flag = a == 0;
        if (zero_flag) { goto loc_f86d; }
      }
      a = x;
      carry_flag = a >= 0x12;
      if (!carry_flag) {
        a = ram[EVENT_MUSIC_PLAYING];
        a &= 0x8; zero_flag = a == 0;
        if (!zero_flag) {
          a = 0xf;
          goto loc_f86a;
        }
        a = 0x1f;
        goto loc_f86a;
      }
      a = 0xff;
    }
    
loc_f86a:
    apu_write(APU_TRIANGLE, a);
  }
  
loc_f86d:
  a = ram[AREA_MUSIC_PLAYING];
  a &= 0xf3; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  ram[NOISE_BEAT_COUNTER]--; zero_flag = ram[NOISE_BEAT_COUNTER] == 0;
  if (!zero_flag) {
    return;
  }
  do {
    y = ram[NOISE_MUSIC_POS];
    ram[NOISE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0;
    if (!zero_flag) { goto loc_f88a; }
    a = ram[NOISE_LOOP_POS]; zero_flag = a == 0;
    ram[NOISE_MUSIC_POS] = a;
  } while (!zero_flag);
  
loc_f88a:
  alternate_length();
  ram[NOISE_BEAT_COUNTER] = a;
  a = x;
  a &= 0x3e; zero_flag = a == 0;
  if (!zero_flag) {
    { uint8_t m = 0x30; carry_flag = a >= m; zero_flag = a == m; }
    if (!zero_flag) {
      { uint8_t m = 0x20; carry_flag = a >= m; zero_flag = a == m; }
      if (!zero_flag) {
        a &= 0x10; zero_flag = a == 0;
        if (zero_flag) { goto loc_f8b9; }
        a = 0x1c;
        x = 0x3;
        y = 0x18;
        goto loc_f8bb;
      }
      a = 0x1c;
      x = 0xc;
      y = 0x18;
      goto loc_f8bb;
    }
    a = 0x1c;
    x = 0x3;
    y = 0x58;
  } else {
    
loc_f8b9:
    a = 0x10;
  }
  
loc_f8bb:
  apu_write(APU_NOISE, a);
  apu_write((APU + 14), x);
  apu_write((APU + 15), y);
  return;
}

void update_music(void) {
  a = ram[EVENT_MUSIC_REQUEST]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[AREA_MUSIC_REQUEST]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f6c8; }
    a = ram[EVENT_MUSIC_PLAYING];
    a |= ram[AREA_MUSIC_PLAYING]; zero_flag = a == 0;
    if (!zero_flag) {
      continue_music();
      return;
    }
    return;
  }
  
loc_f6a4:
  ram[EVENT_MUSIC_PLAYING] = a;
  { uint8_t m = 0x1; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    stop_square1_sfx();
    stop_square2_sfx();
  }
  x = ram[AREA_MUSIC_PLAYING];
  ram[AREA_MUSIC_SAVED] = x;
  y = 0x0;
  ram[NOTE_LENGTH_ADD] = y;
  ram[AREA_MUSIC_PLAYING] = y;
  zero_flag = a == 0x40;
  if (!zero_flag) { goto loc_f6f1; }
  x = 0x8;
  ram[NOTE_LENGTH_ADD] = x;
  goto loc_f6f1;
  
loc_f6c8:
  { uint8_t m = 0x4; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    stop_square1_sfx();
  }
  y = 0x10;
  
loc_f6d1:
  ram[GROUND_MUSIC_HEADER] = y;
  
loc_f6d4:
  ram[EVENT_MUSIC_PLAYING] = 0x0;
  ram[AREA_MUSIC_PLAYING] = a;
  if (a == 0x1) {
    ram[GROUND_MUSIC_HEADER]++;
    y = ram[GROUND_MUSIC_HEADER];
    { uint8_t m = 0x32; carry_flag = y >= m; zero_flag = y == m; }
    if (!zero_flag) { goto loc_f6f5; }
    y = 0x11;
    goto loc_f6d1;
  }
  y = 0x8;
  ram[PULSE2_MUSIC_POS] = y;
  do {
    
loc_f6f1:
    y++;
    carry_flag = (a & 1) != 0; a = a >> 1;
  } while (!carry_flag);
  
loc_f6f5:
  a = ROM((SONG_HEADER_OFFSETS - 1) + y);
  y = a;
  ram[NOTE_LENGTH_OFFSET] = ROM(SONG_HEADER_OFFSETS + y);
  ram[MUSIC_PTR] = ROM((SONG_HEADER_OFFSETS + 1) + y);
  ram[(MUSIC_PTR + 1)] = ROM((SONG_HEADER_OFFSETS + 2) + y);
  ram[TRIANGLE_MUSIC_POS] = ROM((SONG_HEADER_OFFSETS + 3) + y);
  ram[PULSE1_MUSIC_POS] = ROM((SONG_HEADER_OFFSETS + 4) + y);
  a = ROM((SONG_HEADER_OFFSETS + 5) + y);
  ram[NOISE_MUSIC_POS] = a;
  ram[NOISE_LOOP_POS] = a;
  a = 0x1;
  ram[PULSE2_NOTE_COUNTER] = a;
  ram[PULSE1_NOTE_COUNTER] = a;
  ram[TRIANGLE_NOTE_COUNTER] = a;
  ram[NOISE_BEAT_COUNTER] = a;
  a = 0x0;
  ram[PULSE2_MUSIC_POS] = a;
  ram[ALT_REGISTERS] = a;
  apu_write(APU_STATUS, 0xb);
  apu_write(APU_STATUS, 0xf);
  ram[PULSE2_NOTE_COUNTER]--; zero_flag = ram[PULSE2_NOTE_COUNTER] == 0;
  if (zero_flag) {
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    if (!zero_flag) {
      if (!neg_flag) { goto loc_f786; }
    } else {
      a = ram[EVENT_MUSIC_PLAYING];
      { uint8_t m = 0x40; carry_flag = a >= m; zero_flag = a == m; }
      if (zero_flag) {
        a = ram[AREA_MUSIC_SAVED]; zero_flag = a == 0;
        if (!zero_flag) { goto loc_f774; }
      }
      a &= 0x4; zero_flag = a == 0;
      if (!zero_flag) { goto loc_f777; }
      a = ram[AREA_MUSIC_PLAYING];
      a &= 0x5f; zero_flag = a == 0;
      if (zero_flag) {
        a = 0x0;
        ram[AREA_MUSIC_PLAYING] = a;
        ram[EVENT_MUSIC_PLAYING] = a;
        apu_write(APU_TRIANGLE, a);
        a = 0x90;
        apu_write(APU_PULSE1, a);
        apu_write(APU_PULSE2, a);
        return;
      }
      
loc_f774:
      goto loc_f6d4;
      
loc_f777:
      goto loc_f6a4;
    }
    note_length();
    ram[PULSE2_NOTE_LENGTH] = a;
    y = ram[PULSE2_MUSIC_POS];
    ram[PULSE2_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR));
    
loc_f786:
    x = ram[PULSE2_SFX_PLAYING]; zero_flag = x == 0;
    if (zero_flag) {
      set_square2_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE2_ENVELOPE] = a;
      set_square2_regs();
    }
    ram[PULSE2_NOTE_COUNTER] = ram[PULSE2_NOTE_LENGTH];
  }
  a = ram[PULSE2_SFX_PLAYING]; zero_flag = a == 0;
  if (zero_flag) {
    a = ram[EVENT_MUSIC_PLAYING];
    a &= 0x91; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f7bc; }
    y = ram[PULSE2_ENVELOPE]; zero_flag = y == 0;
    if (!zero_flag) {
      ram[PULSE2_ENVELOPE]--;
    }
    load_envelope_entry();
    apu_write(APU_PULSE2, a);
    x = 0x7f;
    apu_write((APU + 5), x);
  }
  
loc_f7bc:
  y = ram[PULSE1_MUSIC_POS]; zero_flag = y == 0;
  if (!zero_flag) {
    ram[PULSE1_NOTE_COUNTER]--; zero_flag = ram[PULSE1_NOTE_COUNTER] == 0;
    if (zero_flag) {
      
loc_f7c5:
      y = ram[PULSE1_MUSIC_POS];
      ram[PULSE1_MUSIC_POS]++;
      a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0;
      if (zero_flag) {
        apu_write(APU_PULSE1, 0x83);
        a = 0x94;
        apu_write((APU + 1), a);
        ram[ALT_REGISTERS] = a;
        goto loc_f7c5;
      }
      alternate_length();
      ram[PULSE1_NOTE_COUNTER] = a;
      y = ram[PULSE1_SFX_PLAYING]; zero_flag = y == 0;
      if (!zero_flag) { goto loc_f81a; }
      a = x;
      a &= 0x3e;
      set_square1_freq();
      if (!zero_flag) {
        load_square_envelope();
      }
      ram[PULSE1_ENVELOPE] = a;
      set_square1_regs();
    }
    a = ram[PULSE1_SFX_PLAYING]; zero_flag = a == 0;
    if (!zero_flag) { goto loc_f81a; }
    a = ram[EVENT_MUSIC_PLAYING];
    a &= 0x91; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[PULSE1_ENVELOPE]; zero_flag = y == 0;
      if (!zero_flag) {
        ram[PULSE1_ENVELOPE]--;
      }
      load_envelope_entry();
      apu_write(APU_PULSE1, a);
    }
    a = ram[ALT_REGISTERS]; zero_flag = a == 0;
    if (zero_flag) {
      a = 0x7f;
    }
    apu_write((APU + 1), a);
  }
  
loc_f81a:
  a = ram[TRIANGLE_MUSIC_POS];
  ram[TRIANGLE_NOTE_COUNTER]--; zero_flag = ram[TRIANGLE_NOTE_COUNTER] == 0;
  if (zero_flag) {
    y = ram[TRIANGLE_MUSIC_POS];
    ram[TRIANGLE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    if (!zero_flag) {
      if (neg_flag) {
        note_length();
        ram[TRIANGLE_NOTE_LENGTH] = a;
        apu_write(APU_TRIANGLE, 0x1f);
        y = ram[TRIANGLE_MUSIC_POS];
        ram[TRIANGLE_MUSIC_POS]++;
        a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0;
        if (zero_flag) { goto loc_f86a; }
      }
      set_triangle_freq();
      x = ram[TRIANGLE_NOTE_LENGTH];
      ram[TRIANGLE_NOTE_COUNTER] = x;
      a = ram[EVENT_MUSIC_PLAYING];
      a &= 0x6e; zero_flag = a == 0;
      if (zero_flag) {
        a = ram[AREA_MUSIC_PLAYING];
        a &= 0xa; zero_flag = a == 0;
        if (zero_flag) { goto loc_f86d; }
      }
      a = x;
      carry_flag = a >= 0x12;
      if (!carry_flag) {
        a = ram[EVENT_MUSIC_PLAYING];
        a &= 0x8; zero_flag = a == 0;
        if (!zero_flag) {
          a = 0xf;
          goto loc_f86a;
        }
        a = 0x1f;
        goto loc_f86a;
      }
      a = 0xff;
    }
    
loc_f86a:
    apu_write(APU_TRIANGLE, a);
  }
  
loc_f86d:
  a = ram[AREA_MUSIC_PLAYING];
  a &= 0xf3; zero_flag = a == 0;
  if (zero_flag) {
    return;
  }
  ram[NOISE_BEAT_COUNTER]--; zero_flag = ram[NOISE_BEAT_COUNTER] == 0;
  if (!zero_flag) {
    return;
  }
  do {
    y = ram[NOISE_MUSIC_POS];
    ram[NOISE_MUSIC_POS]++;
    a = read_byte(indirect_y_addr(MUSIC_PTR)); zero_flag = a == 0;
    if (!zero_flag) { goto loc_f88a; }
    a = ram[NOISE_LOOP_POS]; zero_flag = a == 0;
    ram[NOISE_MUSIC_POS] = a;
  } while (!zero_flag);
  
loc_f88a:
  alternate_length();
  ram[NOISE_BEAT_COUNTER] = a;
  a = x;
  a &= 0x3e; zero_flag = a == 0;
  if (!zero_flag) {
    { uint8_t m = 0x30; carry_flag = a >= m; zero_flag = a == m; }
    if (!zero_flag) {
      { uint8_t m = 0x20; carry_flag = a >= m; zero_flag = a == m; }
      if (!zero_flag) {
        a &= 0x10; zero_flag = a == 0;
        if (zero_flag) { goto loc_f8b9; }
        a = 0x1c;
        x = 0x3;
        y = 0x18;
        goto loc_f8bb;
      }
      a = 0x1c;
      x = 0xc;
      y = 0x18;
      goto loc_f8bb;
    }
    a = 0x1c;
    x = 0x3;
    y = 0x58;
  } else {
    
loc_f8b9:
    a = 0x10;
  }
  
loc_f8bb:
  apu_write(APU_NOISE, a);
  apu_write((APU + 14), x);
  apu_write((APU + 15), y);
  return;
}

// in: a; out: a, x, y, carry
void alternate_length(void) {
  x = a;
  { bool carry = (a & 1) != 0; a = (uint8_t)(a >> 1 | carry_flag << 7); carry_flag = carry; }
  a = x;
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  note_length(); // fallthrough
  return;
}

// in: a; out: a, y, carry
void note_length(void) {
  a &= 0x7;
  carry_flag = false;
  { uint16_t sum = a + ram[NOTE_LENGTH_OFFSET] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  { uint16_t sum = a + ram[NOTE_LENGTH_ADD] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  y = a;
  a = read_byte((uint16_t)(NOTE_LENGTHS + y));
  return;
}

// in: none; out: a, x, y, zero
void load_square_envelope(void) {
  a = ram[EVENT_MUSIC_PLAYING];
  a &= 0x8; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0x4;
  } else {
    a = ram[AREA_MUSIC_PLAYING];
    a &= 0x7d; zero_flag = a == 0;
    if (!zero_flag) {
      a = 0x8;
      goto loc_f8ef;
    }
    a = 0x28;
  }
  
loc_f8ef:
  x = 0x82;
  y = 0x7f; zero_flag = y == 0;
  return;
}

// in: y; out: a
void load_envelope_entry(void) {
  a = ram[EVENT_MUSIC_PLAYING];
  a &= 0x8; zero_flag = a == 0;
  if (!zero_flag) {
    a = read_byte((uint16_t)(CASTLE_CLEAR_ENVELOPE + y));
    return;
  }
  a = ram[AREA_MUSIC_PLAYING];
  a &= 0x7d; zero_flag = a == 0;
  if (!zero_flag) {
    a = read_byte((uint16_t)(AREA_MUSIC_ENVELOPE + y));
    return;
  }
  a = read_byte((uint16_t)(WATER_MUSIC_ENVELOPE + y));
  return;
}

void cpu_reset_entry(void) { reset(); }
void cpu_nmi_entry(void) { frame_interrupt(); }
