#include "code.h"

void Start(void) {
  sei(); // pretty standard 6502 type init here
  cld();
  lda_imm(0b00010000); // init PPU control register 1 
  ppu_ctrl = a;
  ldx_imm(0xff); // reset stack pointer
  sp = x;
  
VBlank1:
  lda_abs_fn(PPU_STATUS); // wait two frames
  if (!neg_flag) { goto VBlank1; }
  
VBlank2:
  lda_abs_fn(PPU_STATUS);
  if (!neg_flag) { goto VBlank2; }
  ldy_imm(ColdBootOffset); // load default cold boot pointer
  ldx_imm(0x5); // this is where we check for a warm boot
  
WBootCheck:
  lda_absx(TopScoreDisplay); // check each score digit in the top score
  cmp_imm_fczn(10); // to see if we have a valid digit
  // if not, give up and proceed with cold boot
  if (!carry_flag) {
    dex_fn();
    if (!neg_flag) { goto WBootCheck; }
    lda_abs(WarmBootValidation); // second checkpoint, check to see if 
    cmp_imm_fczn(0xa5); // another location has a specific value
    if (zero_flag) {
      ldy_imm_fzn(WarmBootOffset); // if passed both, load warm boot pointer
    }
  }
  // ColdBoot:
  cpu_call_begin(0x802d); InitializeMemory(); cpu_call_end(); // clear memory using pointer in Y
  dynamic_ram_write(SND_DELTA_REG + 1, a); // reset delta counter load register
  ram[OperMode] = a; // reset primary mode of operation
  lda_imm(0xa5); // set warm boot flag
  ram[WarmBootValidation] = a;
  ram[PseudoRandomBitReg] = a; // set seed for pseudorandom register
  lda_imm(0b00001111);
  apu_write(SND_MASTERCTRL_REG, a); // enable all sound channels except dmc
  lda_imm_fzn(0b00000110);
  ppu_mask = a; // turn off clipping for OAM and background
  cpu_call_begin(0x8048); MoveAllSpritesOffscreen(); cpu_call_end();
  cpu_call_begin(0x804b); InitializeNameTables(); cpu_call_end(); // initialize both name tables
  inc_abs(DisableScreenFlag); // set flag to disable screen output
  lda_abs(Mirror_PPU_CTRL_REG1);
  ora_imm_fzn(0b10000000); // enable NMIs
  cpu_call_begin(0x8056); WritePPUReg1(); cpu_call_end();
  
EndlessLoop:
  cpu_yield(0x8057); return; // endless loop, need I say more?
  // -------------------------------------------------------------------------------------
  // $00 - vram buffer address table low, also used for pseudorandom bit
  // $01 - vram buffer address table high
}

void NonMaskableInterrupt(void) {
  // NonMaskableInterrupt:
  lda_abs(Mirror_PPU_CTRL_REG1); // disable NMIs in mirror reg
  and_imm(0b01111111); // save all other bits
  ram[Mirror_PPU_CTRL_REG1] = a;
  and_imm(0b01111110); // alter name table address to be $2800
  ppu_ctrl = a; // (essentially $2000) but save other bits
  lda_abs(Mirror_PPU_CTRL_REG2); // disable OAM and background display by default
  and_imm(0b11100110);
  ldy_abs_fz(DisableScreenFlag); // get screen disable flag
  if (!zero_flag) { goto ScreenOff; } // if set, used bits as-is
  lda_abs(Mirror_PPU_CTRL_REG2); // otherwise reenable bits and save them
  ora_imm(0b00011110);
  
ScreenOff:
  ram[Mirror_PPU_CTRL_REG2] = a; // save bits for later but not in register at the moment
  and_imm(0b11100111); // disable screen for now
  ppu_mask = a;
  ldx_abs(PPU_STATUS); // reset flip-flop and reset scroll registers to zero
  lda_imm_fzn(0x0);
  cpu_call_begin(0x80ad); InitScroll(); cpu_call_end();
  oam_addr = a; // reset spr-ram address register
  lda_imm(0x2); // perform spr-ram DMA access on $0200-$02ff
  ppu_transfer_oam((uint16_t)(a << 8));
  ldx_abs(VRAM_Buffer_AddrCtrl); // load control for pointer to buffer contents
  lda_absx(VRAM_AddrTable_Low); // set indirect at $00 to pointer
  ram[0x0] = a;
  lda_absx_fzn(VRAM_AddrTable_High);
  ram[0x1] = a;
  cpu_call_begin(0x80c5); UpdateScreen(); cpu_call_end(); // update screen with buffer contents
  ldy_imm(0x0);
  ldx_abs(VRAM_Buffer_AddrCtrl); // check for usage of $0341
  cpx_imm_fcz(0x6);
  if (!zero_flag) { goto InitBuffer; }
  iny(); // get offset based on usage
  
InitBuffer:
  ldx_absy(VRAM_Buffer_Offset);
  lda_imm(0x0); // clear buffer header at last location
  ram[VRAM_Buffer1_Offset + x] = a;
  ram[VRAM_Buffer1 + x] = a;
  ram[VRAM_Buffer_AddrCtrl] = a; // reinit address control to $0301
  lda_abs_fzn(Mirror_PPU_CTRL_REG2); // copy mirror of $2001 to register
  ppu_mask = a;
  cpu_call_begin(0x80e6); SoundEngine(); cpu_call_end(); // play sound
  cpu_call_begin(0x80e9); ReadJoypads(); cpu_call_end(); // read joypads
  cpu_call_begin(0x80ec); PauseRoutine(); cpu_call_end(); // handle pause
  cpu_call_begin(0x80ef); UpdateTopScore(); cpu_call_end();
  lda_abs(GamePauseStatus); // check for pause status
  lsr_acc_fc();
  if (carry_flag) { goto PauseSkip; }
  lda_abs_fz(TimerControl); // if master timer control not set, decrement
  if (zero_flag) { goto DecTimers; } // all frame and interval timers
  dec_abs_fz(TimerControl);
  if (!zero_flag) { goto NoDecTimers; }
  
DecTimers:
  ldx_imm(0x14); // load end offset for end of frame timers
  dec_abs_fn(IntervalTimerControl); // decrement interval timer control,
  if (!neg_flag) { goto DecTimersLoop; } // if not expired, only frame timers will decrement
  lda_imm(0x14);
  ram[IntervalTimerControl] = a; // if control for interval timers expired,
  ldx_imm(0x23); // interval timers will decrement along with frame timers
  
DecTimersLoop:
  lda_absx_fz(Timers); // check current timer
  if (zero_flag) { goto SkipExpTimer; } // if current timer expired, branch to skip,
  dec_absx(Timers); // otherwise decrement the current timer
  
SkipExpTimer:
  dex_fn(); // move onto next timer
  if (!neg_flag) { goto DecTimersLoop; } // do this until all timers are dealt with
  
NoDecTimers:
  inc_zp(FrameCounter); // increment frame counter
  
PauseSkip:
  ldx_imm(0x0);
  ldy_imm(0x7);
  lda_abs(PseudoRandomBitReg); // get first memory location of LSFR bytes
  and_imm(0b00000010); // mask out all but d1
  ram[0x0] = a; // save here
  lda_abs(PseudoRandomBitReg + 1); // get second memory location
  and_imm(0b00000010); // mask out all but d1
  eor_zp_fz(0x0); // perform exclusive-OR on d1 from first and second bytes
  carry_flag = false; // if neither or both are set, carry will be clear
  if (zero_flag) { goto RotPRandomBit; }
  carry_flag = true; // if one or the other is set, carry will be set
  
RotPRandomBit:
  ror_absx_fc(PseudoRandomBitReg); // rotate carry into d7, and rotate last bit into carry
  inx(); // increment to next byte
  dey_fz(); // decrement for loop
  if (!zero_flag) { goto RotPRandomBit; }
  lda_abs_fz(Sprite0HitDetectFlag); // check for flag here
  if (zero_flag) { goto SkipSprite0; }
  
Sprite0Clr:
  lda_abs(PPU_STATUS); // wait for sprite 0 flag to clear, which will
  and_imm_fz(0b01000000); // not happen until vblank has ended
  if (!zero_flag) { goto Sprite0Clr; }
  lda_abs(GamePauseStatus); // if in pause mode, do not bother with sprites at all
  lsr_acc_fczn();
  if (carry_flag) { goto Sprite0Hit; }
  cpu_call_begin(0x814c); MoveSpritesOffscreen(); cpu_call_end();
  cpu_call_begin(0x814f); SpriteShuffler(); cpu_call_end();
  
Sprite0Hit:
  lda_abs(PPU_STATUS); // do sprite #0 hit detection
  and_imm_fz(0b01000000);
  if (zero_flag) { goto Sprite0Hit; }
  ldy_imm(0x14); // small delay, to wait until we hit horizontal blank time
  
HBlankDelay:
  dey_fz();
  if (!zero_flag) { goto HBlankDelay; }
  
SkipSprite0:
  lda_abs(HorizontalScroll); // set scroll registers from variables
  ppu_write_scroll(a);
  lda_abs(VerticalScroll);
  ppu_write_scroll(a);
  lda_abs(Mirror_PPU_CTRL_REG1); // load saved mirror of $2000
  pha();
  ppu_ctrl = a;
  lda_abs(GamePauseStatus); // if in pause mode, do not perform operation mode stuff
  lsr_acc_fczn();
  if (carry_flag) { goto SkipMainOper; }
  cpu_call_begin(0x8177); OperModeExecutionTree(); cpu_call_end(); // otherwise do one of many, many possible subroutines
  
SkipMainOper:
  lda_abs(PPU_STATUS); // reset flip-flop
  pla();
  ora_imm_fzn(0b10000000); // reactivate NMIs
  ppu_ctrl = a;
  return; // <rti> // we are done until the next frame!
  // -------------------------------------------------------------------------------------
}

void PauseRoutine(void) {
  // PauseRoutine:
  lda_abs(OperMode); // are we in victory mode?
  cmp_imm_fcz(VictoryModeValue); // if so, go ahead
  if (zero_flag) { goto ChkPauseTimer; }
  cmp_imm_fczn(GameModeValue); // are we in game mode?
  if (!zero_flag) { return; } // if not, leave
  lda_abs(OperMode_Task); // if we are in game mode, are we running game engine?
  cmp_imm_fczn(0x3);
  if (!zero_flag) { return; } // if not, leave
  
ChkPauseTimer:
  lda_abs_fz(GamePauseTimer); // check if pause timer is still counting down
  if (zero_flag) { goto ChkStart; }
  dec_abs_fzn(GamePauseTimer); // if so, decrement and leave
  return;
  
ChkStart:
  lda_abs(SavedJoypad1Bits); // check to see if start is pressed
  and_imm_fz(Start_Button); // on controller 1
  if (zero_flag) { goto ClrPauseTimer; }
  lda_abs(GamePauseStatus); // check to see if timer flag is set
  and_imm_fzn(0b10000000); // and if so, do not reset timer (residual,
  if (!zero_flag) { return; } // joypad reading routine makes this unnecessary)
  lda_imm(0x2b); // set pause timer
  ram[GamePauseTimer] = a;
  lda_abs(GamePauseStatus);
  tay();
  iny(); // set pause sfx queue for next pause mode
  ram[PauseSoundQueue] = y;
  eor_imm(0b00000001); // invert d0 and set d7
  ora_imm_fzn(0b10000000);
  if (!zero_flag) { goto SetPause; } // unconditional branch
  
ClrPauseTimer:
  lda_abs(GamePauseStatus); // clear timer flag if timer is at zero and start button
  and_imm_fzn(0b01111111); // is not pressed
  
SetPause:
  ram[GamePauseStatus] = a;
  // ExitPause:
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used for preset value
}

void SpriteShuffler(void) {
  ldy_abs(AreaType); // load level type, likely residual code
  lda_imm(0x28); // load preset value which will put it at
  ram[0x0] = a; // sprite #10
  ldx_imm(0xe); // start at the end of OAM data offsets
  
ShuffleLoop:
  lda_absx(SprDataOffset); // check for offset value against
  cmp_zp_fc(0x0); // the preset value
  // if less, skip this part
  if (carry_flag) {
    ldy_abs(SprShuffleAmtOffset); // get current offset to preset value we want to add
    carry_flag = false;
    adc_absy_fc(SprShuffleAmt); // get shuffle amount, add to current sprite offset
    // if not exceeded $ff, skip second add
    if (carry_flag) {
      carry_flag = false;
      adc_zp(0x0); // otherwise add preset value $28 to offset
    }
    // StrSprOffset:
    ram[SprDataOffset + x] = a; // store new offset here or old one if branched to here
  }
  // NextSprOffset:
  dex_fn(); // move backwards to next one
  if (!neg_flag) { goto ShuffleLoop; }
  ldx_abs(SprShuffleAmtOffset); // load offset
  inx();
  cpx_imm_fz(0x3); // check if offset + 1 goes to 3
  // if offset + 1 not 3, store
  if (zero_flag) {
    ldx_imm(0x0); // otherwise, init to 0
  }
  // SetAmtOffset:
  ram[SprShuffleAmtOffset] = x;
  ldx_imm(0x8); // load offsets for values and storage
  ldy_imm(0x2);
  
SetMiscOffset:
  lda_absy(SprDataOffset + 5); // load one of three OAM data offsets
  ram[Misc_SprDataOffset - 2 + x] = a; // store first one unmodified, but
  carry_flag = false; // add eight to the second and eight
  adc_imm(0x8); // more to the third one
  ram[Misc_SprDataOffset - 1 + x] = a; // note that due to the way X is set up,
  carry_flag = false; // this code loads into the misc sprite offsets
  adc_imm_fc(0x8);
  ram[Misc_SprDataOffset + x] = a;
  dex();
  dex();
  dex();
  dey_fzn();
  if (!neg_flag) { goto SetMiscOffset; } // do this until all misc spr offsets are loaded
  return;
  // -------------------------------------------------------------------------------------
}

void OperModeExecutionTree(void) {
  lda_abs_fzn(OperMode); // this is the heart of the entire program,
  cpu_call_begin(0x8217);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x8231: TitleScreenMode(); return;
    case 0xaedc: GameMode(); return;
    case 0x838b: VictoryMode(); return;
    case 0x9218: GameOverMode(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void MoveAllSpritesOffscreen(void) {
  ldy_imm(0x0); // this routine moves all sprites off the screen
  // loc_33314:
  bit_abs(0x4a0);
  goto loc_33317; // BIT instruction opcode
  
loc_33317:
  lda_imm(0xf8); // off the screen
  
SprInitLoop:
  ram[Sprite_Y_Position + y] = a; // write 248 into OAM data's Y coordinate
  iny(); // which will move it off the screen
  iny();
  iny();
  iny_fzn();
  if (!zero_flag) { goto SprInitLoop; }
  return;
  // -------------------------------------------------------------------------------------
}

void MoveSpritesOffscreen(void) {
  ldy_imm(0x4); // this routine moves all but sprite 0
  // loc_33317:
  lda_imm(0xf8); // off the screen
  
SprInitLoop:
  ram[Sprite_Y_Position + y] = a; // write 248 into OAM data's Y coordinate
  iny(); // which will move it off the screen
  iny();
  iny();
  iny_fzn();
  if (!zero_flag) { goto SprInitLoop; }
  return;
  // -------------------------------------------------------------------------------------
}

void TitleScreenMode(void) {
  lda_abs_fzn(OperMode_Task);
  cpu_call_begin(0x8236);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x8fcf: InitializeGame(); return;
    case 0x8567: ScreenRoutines(); return;
    case 0x9061: PrimaryGameSetup(); return;
    case 0x8245: GameMenuRoutine(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void GameMenuRoutine(void) {
  // GameMenuRoutine:
  ldy_imm(0x0);
  lda_abs(SavedJoypad1Bits); // check to see if either player pressed
  ora_abs(SavedJoypad2Bits); // only the start button (either joypad)
  cmp_imm_fcz(Start_Button);
  if (zero_flag) { goto StartGame; }
  cmp_imm_fcz(A_Button + Start_Button); // check to see if A + start was pressed
  if (!zero_flag) { goto ChkSelect; } // if not, branch to check select button
  
StartGame:
  goto ChkContinue; // if either start or A + start, execute here
  
ChkSelect:
  cmp_imm_fcz(Select_Button); // check to see if the select button was pressed
  if (zero_flag) { goto SelectBLogic; } // if so, branch reset demo timer
  ldx_abs_fzn(DemoTimer); // otherwise check demo timer
  if (!zero_flag) { goto ChkWorldSel; } // if demo timer not expired, branch to check world selection
  ram[SelectTimer] = a; // set controller bits here if running demo
  cpu_call_begin(0x8266); DemoEngine(); cpu_call_end(); // run through the demo actions
  if (carry_flag) { goto ResetTitle; } // if carry flag set, demo over, thus branch
  goto RunDemo; // otherwise, run game engine for demo
  
ChkWorldSel:
  ldx_abs_fz(WorldSelectEnableFlag); // check to see if world selection has been enabled
  if (zero_flag) { goto NullJoypad; }
  cmp_imm_fcz(B_Button); // if so, check to see if the B button was pressed
  if (!zero_flag) { goto NullJoypad; }
  iny(); // if so, increment Y and execute same code as select
  
SelectBLogic:
  lda_abs_fz(DemoTimer); // if select or B pressed, check demo timer one last time
  if (zero_flag) { goto ResetTitle; } // if demo timer expired, branch to reset title screen mode
  lda_imm(0x18); // otherwise reset demo timer
  ram[DemoTimer] = a;
  lda_abs_fz(SelectTimer); // check select/B button timer
  if (!zero_flag) { goto NullJoypad; } // if not expired, branch
  lda_imm(0x10); // otherwise reset select button timer
  ram[SelectTimer] = a;
  cpy_imm_fcz(0x1); // was the B button pressed earlier?  if so, branch
  if (zero_flag) { goto IncWorldSel; } // note this will not be run if world selection is disabled
  lda_abs(NumberOfPlayers); // if no, must have been the select button, therefore
  eor_imm_fzn(0b00000001); // change number of players and draw icon accordingly
  ram[NumberOfPlayers] = a;
  cpu_call_begin(0x8298); DrawMushroomIcon(); cpu_call_end();
  goto NullJoypad;
  
IncWorldSel:
  ldx_abs(WorldSelectNumber); // increment world select number
  inx();
  txa();
  and_imm_fzn(0b00000111); // mask out higher bits
  ram[WorldSelectNumber] = a; // store as current world select number
  cpu_call_begin(0x82a8); GoContinue(); cpu_call_end();
  
UpdateShroom:
  lda_absx(WSelectBufferTemplate); // write template for world select in vram buffer
  ram[VRAM_Buffer1 - 1 + x] = a; // do this until all bytes are written
  inx();
  cpx_imm_fcn(0x6);
  if (neg_flag) { goto UpdateShroom; }
  ldy_abs(WorldNumber); // get world number from variable and increment for
  iny(); // proper display, and put in blank byte before
  ram[VRAM_Buffer1 + 3] = y; // null terminator
  
NullJoypad:
  lda_imm_fzn(0x0); // clear joypad bits for player 1
  ram[SavedJoypad1Bits] = a;
  
RunDemo:
  cpu_call_begin(0x82c2); GameCoreRoutine(); cpu_call_end(); // run game engine
  lda_zp(GameEngineSubroutine); // check to see if we're running lose life routine
  cmp_imm_fczn(0x6);
  if (!zero_flag) { return; } // if not, do not do all the resetting below
  
ResetTitle:
  lda_imm(0x0); // reset game modes, disable
  ram[OperMode] = a; // sprite 0 check and disable
  ram[OperMode_Task] = a; // screen output
  ram[Sprite0HitDetectFlag] = a;
  inc_abs_fzn(DisableScreenFlag);
  return;
  
ChkContinue:
  ldy_abs_fz(DemoTimer); // if timer for demo has expired, reset modes
  if (zero_flag) { goto ResetTitle; }
  asl_acc_fczn(); // check to see if A button was also pushed
  if (!carry_flag) { goto StartWorld1; } // if not, don't load continue function's world number
  lda_abs_fzn(ContinueWorld); // load previously saved world number for secret
  cpu_call_begin(0x82e5); GoContinue(); cpu_call_end(); // continue function when pressing A + start
  
StartWorld1:
  cpu_call_begin(0x82e8); LoadAreaPointer(); cpu_call_end();
  inc_abs(Hidden1UpFlag); // set 1-up box flag for both players
  inc_abs(OffScr_Hidden1UpFlag);
  inc_abs(FetchNewGameTimerFlag); // set fetch new game timer flag
  inc_abs(OperMode); // set next game mode
  lda_abs(WorldSelectEnableFlag); // if world select flag is on, then primary
  ram[PrimaryHardMode] = a; // hard mode must be on as well
  lda_imm(0x0);
  ram[OperMode_Task] = a; // set game mode here, and clear demo timer
  ram[DemoTimer] = a;
  ldx_imm(0x17);
  lda_imm(0x0);
  
InitScores:
  ram[ScoreAndCoinDisplay + x] = a; // clear player scores and coin displays
  dex_fzn();
  if (!neg_flag) { goto InitScores; }
  // ExitMenu:
  return;
}

void GoContinue(void) {
  ram[WorldNumber] = a; // start both players at the first area
  ram[OffScr_WorldNumber] = a; // of the previously saved world number
  ldx_imm_fzn(0x0); // note that on power-up using this function
  ram[AreaNumber] = x; // will make no difference
  ram[OffScr_AreaNumber] = x;
  return;
  // -------------------------------------------------------------------------------------
}

void DrawMushroomIcon(void) {
  ldy_imm(0x7); // read eight bytes to be read by transfer routine
  
IconDataRead:
  lda_absy(MushroomIconData); // note that the default position is set for a
  ram[VRAM_Buffer1 - 1 + y] = a; // 1-player game
  dey_fn();
  if (!neg_flag) { goto IconDataRead; }
  lda_abs_fzn(NumberOfPlayers); // check number of players
  if (!zero_flag) {
    lda_imm(0x24); // otherwise, load blank tile in 1-player position
    ram[VRAM_Buffer1 + 3] = a;
    lda_imm_fzn(0xce); // then load shroom icon tile in 2-player position
    ram[VRAM_Buffer1 + 5] = a;
    // ExitIcon:
    return;
    // -------------------------------------------------------------------------------------
  }
}

void DemoEngine(void) {
  // DemoEngine:
  ldx_abs(DemoAction); // load current demo action
  lda_abs_fz(DemoActionTimer); // load current action timer
  if (!zero_flag) { goto DoAction; } // if timer still counting down, skip
  inx();
  inc_abs(DemoAction); // if expired, increment action, X, and
  carry_flag = true; // set carry by default for demo over
  lda_absx_fzn(DemoTimingData - 1); // get next timer
  ram[DemoActionTimer] = a; // store as current timer
  if (zero_flag) { return; } // if timer already at zero, skip
  
DoAction:
  lda_absx(DemoActionData - 1); // get and perform action (current or next)
  ram[SavedJoypad1Bits] = a;
  dec_abs_fzn(DemoActionTimer); // decrement action timer
  carry_flag = false; // clear carry if demo still going
  // DemoOver:
  return;
  // -------------------------------------------------------------------------------------
}

void VictoryMode(void) {
  cpu_call_begin(0x838d); VictoryModeSubroutines(); cpu_call_end(); // run victory mode subroutines
  lda_abs_fzn(OperMode_Task); // get current task of victory mode
  // if on bridge collapse, skip enemy processing
  if (!zero_flag) {
    ldx_imm_fzn(0x0);
    ram[ObjectOffset] = x; // otherwise reset enemy object offset 
    cpu_call_begin(0x8399); EnemiesAndLoopsCore(); cpu_call_end(); // and run enemy code
  }
  // AutoPlayer:
  cpu_call_begin(0x839c); RelativePlayerPosition(); cpu_call_end(); // get player's relative coordinates
  PlayerGfxHandler(); return; // draw the player, then leave
}

void VictoryModeSubroutines(void) {
  lda_abs_fzn(OperMode_Task);
  cpu_call_begin(0x83a5);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xcfec: BridgeCollapse(); return;
    case 0x83b0: SetupVictoryMode(); return;
    case 0x83bd: PlayerVictoryWalk(); return;
    case 0x83f6: PrintVictoryMessages(); return;
    case 0x8461: PlayerEndWorld(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void SetupVictoryMode(void) {
  ldx_abs(ScreenRight_PageLoc); // get page location of right side of screen
  inx(); // increment to next page
  ram[DestinationPageLoc] = x; // store here
  lda_imm(EndOfCastleMusic);
  ram[EventMusicQueue] = a; // play win castle music
  goto IncModeTask_B; // jump to set next major task in victory mode
  // -------------------------------------------------------------------------------------
  
IncModeTask_B:
  inc_abs_fzn(OperMode_Task); // move onto next mode
  return;
  // -------------------------------------------------------------------------------------
}

void PlayerVictoryWalk(void) {
  // PlayerVictoryWalk:
  ldy_imm(0x0); // set value here to not walk player by default
  ram[VictoryWalkControl] = y;
  lda_zp(Player_PageLoc); // get player's page location
  cmp_zp_fcz(DestinationPageLoc); // compare with destination page location
  if (!zero_flag) { goto PerformWalk; } // if page locations don't match, branch
  lda_zp(Player_X_Position); // otherwise get player's horizontal position
  cmp_imm_fc(0x60); // compare with preset horizontal position
  if (carry_flag) { goto DontWalk; } // if still on other page, branch ahead
  
PerformWalk:
  inc_zp(VictoryWalkControl); // otherwise increment value and Y
  iny(); // note Y will be used to walk the player
  
DontWalk:
  tya_fzn(); // put contents of Y in A and
  cpu_call_begin(0x83d3); AutoControlPlayer(); cpu_call_end(); // use A to move player to the right or not
  lda_abs(ScreenLeft_PageLoc); // check page location of left side of screen
  cmp_zp_fcz(DestinationPageLoc); // against set value here
  if (zero_flag) { goto ExitVWalk; } // branch if equal to change modes if necessary
  lda_abs(ScrollFractional);
  carry_flag = false; // do fixed point math on fractional part of scroll
  adc_imm_fc(0x80);
  ram[ScrollFractional] = a; // save fractional movement amount
  lda_imm(0x1); // set 1 pixel per frame
  adc_imm_fc(0x0); // add carry from previous addition
  tay_fzn(); // use as scroll amount
  cpu_call_begin(0x83eb); ScrollScreen(); cpu_call_end(); // do sub to scroll the screen
  cpu_call_begin(0x83ee); UpdScrollVar(); cpu_call_end(); // do another sub to update screen and scroll variables
  inc_zp(VictoryWalkControl); // increment value to stay in this routine
  
ExitVWalk:
  lda_zp_fzn(VictoryWalkControl); // load value set here
  if (zero_flag) { goto IncModeTask_A; } // if zero, branch to change modes
  return; // otherwise leave
  // -------------------------------------------------------------------------------------
  
IncModeTask_A:
  inc_abs_fzn(OperMode_Task); // move onto next task in mode
  // ExitMsgs:
  return; // leave
  // -------------------------------------------------------------------------------------
}

void PrintVictoryMessages(void) {
  // PrintVictoryMessages:
  lda_abs_fz(SecondaryMsgCounter); // load secondary message counter
  if (!zero_flag) { goto IncMsgCounter; } // if set, branch to increment message counters
  lda_abs_fz(PrimaryMsgCounter); // otherwise load primary message counter
  if (zero_flag) { goto ThankPlayer; } // if set to zero, branch to print first message
  cmp_imm_fc(0x9); // if at 9 or above, branch elsewhere (this comparison
  if (carry_flag) { goto IncMsgCounter; } // is residual code, counter never reaches 9)
  ldy_abs(WorldNumber); // check world number
  cpy_imm_fz(World8);
  if (!zero_flag) { goto MRetainerMsg; } // if not at world 8, skip to next part
  cmp_imm_fc(0x3); // check primary message counter again
  if (!carry_flag) { goto IncMsgCounter; } // if not at 3 yet (world 8 only), branch to increment
  sbc_imm(0x1); // otherwise subtract one
  goto ThankPlayer; // and skip to next part
  
MRetainerMsg:
  cmp_imm_fc(0x2); // check primary message counter
  if (!carry_flag) { goto IncMsgCounter; } // if not at 2 yet (world 1-7 only), branch
  
ThankPlayer:
  tay_fz(); // put primary message counter into Y
  if (!zero_flag) { goto SecondPartMsg; } // if counter nonzero, skip this part, do not print first message
  lda_abs_fz(CurrentPlayer); // otherwise get player currently on the screen
  if (zero_flag) { goto EvalForMusic; } // if mario, branch
  iny_fz(); // otherwise increment Y once for luigi and
  if (!zero_flag) { goto EvalForMusic; } // do an unconditional branch to the same place
  
SecondPartMsg:
  iny(); // increment Y to do world 8's message
  lda_abs(WorldNumber);
  cmp_imm_fz(World8); // check world number
  if (zero_flag) { goto EvalForMusic; } // if at world 8, branch to next part
  dey(); // otherwise decrement Y for world 1-7's message
  cpy_imm_fczn(0x4); // if counter at 4 (world 1-7 only)
  if (carry_flag) { goto SetEndTimer; } // branch to set victory end timer
  cpy_imm_fc(0x3); // if counter at 3 (world 1-7 only)
  if (carry_flag) { goto IncMsgCounter; } // branch to keep counting
  
EvalForMusic:
  cpy_imm_fz(0x3); // if counter not yet at 3 (world 8 only), branch
  if (!zero_flag) { goto PrintMsg; } // to print message only (note world 1-7 will only
  lda_imm(VictoryMusic); // reach this code if counter = 0, and will always branch)
  ram[EventMusicQueue] = a; // otherwise load victory music first (world 8 only)
  
PrintMsg:
  tya(); // put primary message counter in A
  carry_flag = false; // add $0c or 12 to counter thus giving an appropriate value,
  adc_imm(0xc); // ($0c-$0d = first), ($0e = world 1-7's), ($0f-$12 = world 8's)
  ram[VRAM_Buffer_AddrCtrl] = a; // write message counter to vram address controller
  
IncMsgCounter:
  lda_abs(SecondaryMsgCounter);
  carry_flag = false;
  adc_imm_fc(0x4); // add four to secondary message counter
  ram[SecondaryMsgCounter] = a;
  lda_abs(PrimaryMsgCounter);
  adc_imm(0x0); // add carry to primary message counter
  ram[PrimaryMsgCounter] = a;
  cmp_imm_fczn(0x7); // check primary counter one more time
  
SetEndTimer:
  if (!carry_flag) { return; } // if not reached value yet, branch to leave
  lda_imm(0x6);
  ram[WorldEndTimer] = a; // otherwise set world end timer
  // IncModeTask_A:
  inc_abs_fzn(OperMode_Task); // move onto next task in mode
  // ExitMsgs:
  return; // leave
  // -------------------------------------------------------------------------------------
}

void PlayerEndWorld(void) {
  // PlayerEndWorld:
  lda_abs_fzn(WorldEndTimer); // check to see if world end timer expired
  if (!zero_flag) { return; } // branch to leave if not
  ldy_abs(WorldNumber); // check world number
  cpy_imm_fc(World8); // if on world 8, player is done with game, 
  if (carry_flag) { goto EndChkBButton; } // thus branch to read controller
  lda_imm(0x0);
  ram[AreaNumber] = a; // otherwise initialize area number used as offset
  ram[LevelNumber] = a; // and level number control to start at area 1
  ram[OperMode_Task] = a; // initialize secondary mode of operation
  inc_abs_fzn(WorldNumber); // increment world number to move onto the next world
  cpu_call_begin(0x847d); LoadAreaPointer(); cpu_call_end(); // get area address offset for the next area
  inc_abs(FetchNewGameTimerFlag); // set flag to load game timer from header
  lda_imm_fzn(GameModeValue);
  ram[OperMode] = a; // set mode of operation to game mode
  // EndExitOne:
  return; // and leave
  
EndChkBButton:
  lda_abs(SavedJoypad1Bits);
  ora_abs(SavedJoypad2Bits); // check to see if B button was pressed on
  and_imm_fzn(B_Button); // either controller
  if (zero_flag) { return; } // branch to leave if not
  lda_imm(0x1); // otherwise set world selection flag
  ram[WorldSelectEnableFlag] = a;
  lda_imm_fzn(0xff); // remove onscreen player's lives
  ram[NumberofLives] = a;
  cpu_call_begin(0x849d); TerminateGame(); cpu_call_end(); // do sub to continue other player or end game
  // EndExitTwo:
  return; // leave
  // -------------------------------------------------------------------------------------
  // data is used as tiles for numbers
  // that appear when you defeat enemies
}

void FloateyNumbersRoutine(void) {
  // FloateyNumbersRoutine:
  lda_absx_fzn(FloateyNum_Control); // load control for floatey number
  if (zero_flag) { return; } // if zero, branch to leave
  cmp_imm_fc(0xb); // if less than $0b, branch
  if (!carry_flag) { goto ChkNumTimer; }
  lda_imm(0xb); // otherwise set to $0b, thus keeping
  ram[FloateyNum_Control + x] = a; // it in range
  
ChkNumTimer:
  tay(); // use as Y
  lda_absx_fzn(FloateyNum_Timer); // check value here
  if (!zero_flag) { goto DecNumTimer; } // if nonzero, branch ahead
  ram[FloateyNum_Control + x] = a; // initialize floatey number control and leave
  return;
  
DecNumTimer:
  dec_absx(FloateyNum_Timer); // decrement value here
  cmp_imm_fz(0x2b); // if not reached a certain point, branch  
  if (!zero_flag) { goto ChkTallEnemy; }
  cpy_imm_fz(0xb); // check offset for $0b
  if (!zero_flag) { goto LoadNumTiles; } // branch ahead if not found
  inc_abs(NumberofLives); // give player one extra life (1-up)
  lda_imm(Sfx_ExtraLife);
  ram[Square2SoundQueue] = a; // and play the 1-up sound
  
LoadNumTiles:
  lda_absy(ScoreUpdateData); // load point value here
  lsr_acc(); // move high nybble to low
  lsr_acc();
  lsr_acc();
  lsr_acc_fc();
  tax(); // use as X offset, essentially the digit
  lda_absy(ScoreUpdateData); // load again and this time
  and_imm_fzn(0b00001111); // mask out the high nybble
  ram[DigitModifier + x] = a; // store as amount to add to the digit
  cpu_call_begin(0x84ff); AddToScore(); cpu_call_end(); // update the score accordingly
  
ChkTallEnemy:
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset for enemy object
  lda_zpx(Enemy_ID); // get enemy object identifier
  cmp_imm_fz(Spiny);
  if (zero_flag) { goto FloateyPart; } // branch if spiny
  cmp_imm_fz(PiranhaPlant);
  if (zero_flag) { goto FloateyPart; } // branch if piranha plant
  cmp_imm_fz(HammerBro);
  if (zero_flag) { goto GetAltOffset; } // branch elsewhere if hammer bro
  cmp_imm_fz(GreyCheepCheep);
  if (zero_flag) { goto FloateyPart; } // branch if cheep-cheep of either color
  cmp_imm_fz(RedCheepCheep);
  if (zero_flag) { goto FloateyPart; }
  cmp_imm_fc(TallEnemy);
  if (carry_flag) { goto GetAltOffset; } // branch elsewhere if enemy object => $09
  lda_zpx(Enemy_State);
  cmp_imm_fc(0x2); // if enemy state defeated or otherwise
  if (carry_flag) { goto FloateyPart; } // $02 or greater, branch beyond this part
  
GetAltOffset:
  ldx_abs(SprDataOffset_Ctrl); // load some kind of control bit
  ldy_absx(Alt_SprDataOffset); // get alternate OAM data offset
  ldx_zp(ObjectOffset); // get enemy object offset again
  
FloateyPart:
  lda_absx(FloateyNum_Y_Pos); // get vertical coordinate for
  cmp_imm_fc(0x18); // floatey number, if coordinate in the
  if (!carry_flag) { goto SetupNumSpr; } // status bar, branch
  sbc_imm_fc(0x1);
  ram[FloateyNum_Y_Pos + x] = a; // otherwise subtract one and store as new
  
SetupNumSpr:
  lda_absx(FloateyNum_Y_Pos); // get vertical coordinate
  sbc_imm_fczn(0x8); // subtract eight and dump into the
  cpu_call_begin(0x853e); DumpTwoSpr(); cpu_call_end(); // left and right sprite's Y coordinates
  lda_absx(FloateyNum_X_Pos); // get horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store into X coordinate of left sprite
  carry_flag = false;
  adc_imm(0x8); // add eight pixels and store into X
  ram[Sprite_X_Position + 4 + y] = a; // coordinate of right sprite
  lda_imm(0x2);
  ram[Sprite_Attributes + y] = a; // set palette control in attribute bytes
  ram[Sprite_Attributes + 4 + y] = a; // of left and right sprites
  lda_absx(FloateyNum_Control);
  asl_acc_fc(); // multiply our floatey number control by 2
  tax(); // and use as offset for look-up table
  lda_absx(FloateyNumTileData);
  ram[Sprite_Tilenumber + y] = a; // display first half of number of points
  lda_absx(FloateyNumTileData + 1);
  ram[Sprite_Tilenumber + 4 + y] = a; // display the second half
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
  // -------------------------------------------------------------------------------------
}

void ScreenRoutines(void) {
  lda_abs_fzn(ScreenRoutineTask); // run one of the following subroutines
  cpu_call_begin(0x856c);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x858b: InitScreen(); return;
    case 0x859b: SetupIntermediate(); return;
    case 0x8652: WriteTopStatusLine(); return;
    case 0x865a: WriteBottomStatusLine(); return;
    case 0x8693: DisplayTimeUp(); return;
    case 0x889d: ResetSpritesAndScreenTimer(); return;
    case 0x86a8: DisplayIntermediate(); return;
    case 0x86e6: AreaParserTaskControl(); return;
    case 0x85bf: GetAreaPalette(); return;
    case 0x85e3: GetBackgroundColor(); return;
    case 0x8643: GetAlternatePalette1(); return;
    case 0x86ff: DrawTitleScreen(); return;
    case 0x8732: ClearBuffersDrawIcon(); return;
    case 0x8749: WriteTopScore(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void InitScreen(void) {
  cpu_call_begin(0x858d); MoveAllSpritesOffscreen(); cpu_call_end(); // initialize all sprites including sprite #0
  cpu_call_begin(0x8590); InitializeNameTables(); cpu_call_end(); // and erase both name and attribute tables
  lda_abs_fz(OperMode);
  // if mode still 0, do not load
  if (!zero_flag) {
    ldx_imm(0x3); // into buffer pointer
    goto SetVRAMAddr_A;
    // -------------------------------------------------------------------------------------
    
SetVRAMAddr_A:
    ram[VRAM_Buffer_AddrCtrl] = x; // store offset into buffer control
  }
  // NextSubtask:
  goto IncSubtask; // move onto next task
  // -------------------------------------------------------------------------------------
  // $00 - used as temp counter in GetPlayerColors
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void SetupIntermediate(void) {
  lda_abs(BackgroundColorCtrl); // save current background color control
  pha(); // and player status to stack
  lda_abs(PlayerStatus);
  pha();
  lda_imm(0x0); // set background color to black
  ram[PlayerStatus] = a; // and player status to not fiery
  lda_imm_fzn(0x2); // this is the ONLY time background color control
  ram[BackgroundColorCtrl] = a; // is set to less than 4
  cpu_call_begin(0x85af); GetPlayerColors(); cpu_call_end();
  pla(); // we only execute this routine for
  ram[PlayerStatus] = a; // the intermediate lives display
  pla(); // and once we're done, we return bg
  ram[BackgroundColorCtrl] = a; // color ctrl and player status from stack
  goto IncSubtask; // then move onto the next task
  // -------------------------------------------------------------------------------------
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void GetAreaPalette(void) {
  ldy_abs(AreaType); // select appropriate palette to load
  ldx_absy(AreaPalette); // based on area type
  // SetVRAMAddr_A:
  ram[VRAM_Buffer_AddrCtrl] = x; // store offset into buffer control
  // NextSubtask:
  goto IncSubtask; // move onto next task
  // -------------------------------------------------------------------------------------
  // $00 - used as temp counter in GetPlayerColors
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void GetBackgroundColor(void) {
  ldy_abs_fz(BackgroundColorCtrl); // check background color control
  // if not set, increment task and fetch palette
  if (!zero_flag) {
    lda_absy(BGColorCtrl_Addr - 4); // put appropriate palette into vram
    ram[VRAM_Buffer_AddrCtrl] = a; // note that if set to 5-7, $0301 will not be read
  }
  // NoBGColor:
  inc_abs(ScreenRoutineTask); // increment to next subtask and plod on through
  GetPlayerColors(); // fallthrough
  return;
}

void GetPlayerColors(void) {
  ldx_abs(VRAM_Buffer1_Offset); // get current buffer offset
  ldy_imm(0x0);
  lda_abs_fz(CurrentPlayer); // check which player is on the screen
  if (!zero_flag) {
    ldy_imm(0x4); // load offset for luigi
  }
  // ChkFiery:
  lda_abs(PlayerStatus); // check player status
  cmp_imm_fz(0x2);
  // if fiery, load alternate offset for fiery player
  if (zero_flag) {
    ldy_imm(0x8);
  }
  // StartClrGet:
  lda_imm(0x3); // do four colors
  ram[0x0] = a;
  
ClrGetLoop:
  lda_absy(PlayerColors); // fetch player colors and store them
  ram[VRAM_Buffer1 + 3 + x] = a; // in the buffer
  iny();
  inx();
  dec_zp_fn(0x0);
  if (!neg_flag) { goto ClrGetLoop; }
  ldx_abs(VRAM_Buffer1_Offset); // load original offset from before
  ldy_abs_fz(BackgroundColorCtrl); // if this value is four or greater, it will be set
  // therefore use it as offset to background color
  if (zero_flag) {
    ldy_abs(AreaType); // otherwise use area type bits from area offset as offset
  }
  // SetBGColor:
  lda_absy(BackgroundColors); // to background color instead
  ram[VRAM_Buffer1 + 3 + x] = a;
  lda_imm(0x3f); // set for sprite palette address
  ram[VRAM_Buffer1 + x] = a; // save to buffer
  lda_imm(0x10);
  ram[VRAM_Buffer1 + 1 + x] = a;
  lda_imm(0x4); // write length byte to buffer
  ram[VRAM_Buffer1 + 2 + x] = a;
  lda_imm(0x0); // now the null terminator
  ram[VRAM_Buffer1 + 7 + x] = a;
  txa(); // move the buffer pointer ahead 7 bytes
  carry_flag = false; // in case we want to write anything else later
  adc_imm_fczn(0x7);
  // SetVRAMOffset:
  ram[VRAM_Buffer1_Offset] = a; // store as new vram buffer offset
  return;
  // -------------------------------------------------------------------------------------
}

void GetAlternatePalette1(void) {
  lda_abs(AreaStyle); // check for mushroom level style
  cmp_imm_fcz(0x1);
  if (zero_flag) {
    lda_imm(0xb); // if found, load appropriate palette
    // SetVRAMAddr_B:
    ram[VRAM_Buffer_AddrCtrl] = a;
  }
  // NoAltPal:
  goto IncSubtask; // now onto the next task
  // -------------------------------------------------------------------------------------
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void WriteTopStatusLine(void) {
  lda_imm_fzn(0x0); // select main status bar
  cpu_call_begin(0x8656); WriteGameText(); cpu_call_end(); // output it
  goto IncSubtask; // onto the next task
  // -------------------------------------------------------------------------------------
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void WriteBottomStatusLine(void) {
  cpu_call_begin(0x865c); GetSBNybbles(); cpu_call_end(); // write player's score and coin tally to screen
  ldx_abs(VRAM_Buffer1_Offset);
  lda_imm(0x20); // write address for world-area number on screen
  ram[VRAM_Buffer1 + x] = a;
  lda_imm(0x73);
  ram[VRAM_Buffer1 + 1 + x] = a;
  lda_imm(0x3); // write length for it
  ram[VRAM_Buffer1 + 2 + x] = a;
  ldy_abs(WorldNumber); // first the world number
  iny();
  tya();
  ram[VRAM_Buffer1 + 3 + x] = a;
  lda_imm(0x28); // next the dash
  ram[VRAM_Buffer1 + 4 + x] = a;
  ldy_abs(LevelNumber); // next the level number
  iny(); // increment for proper number display
  tya();
  ram[VRAM_Buffer1 + 5 + x] = a;
  lda_imm(0x0); // put null terminator on
  ram[VRAM_Buffer1 + 6 + x] = a;
  txa(); // move the buffer offset up by 6 bytes
  carry_flag = false;
  adc_imm_fc(0x6);
  ram[VRAM_Buffer1_Offset] = a;
  goto IncSubtask;
  // -------------------------------------------------------------------------------------
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void DisplayTimeUp(void) {
  lda_abs_fz(GameTimerExpiredFlag); // if game timer not expired, increment task
  // control 2 tasks forward, otherwise, stay here
  if (!zero_flag) {
    lda_imm(0x0);
    ram[GameTimerExpiredFlag] = a; // reset timer expiration flag
    lda_imm_fzn(0x2); // output time-up screen to buffer
    goto OutputInter;
  }
  // NoTimeUp:
  inc_abs(ScreenRoutineTask); // increment control task 2 tasks forward
  goto IncSubtask;
  // -------------------------------------------------------------------------------------
  
OutputInter:
  cpu_call_begin(0x86c9); WriteGameText(); cpu_call_end();
  cpu_call_begin(0x86cc); ResetScreenTimer(); cpu_call_end();
  lda_imm_fzn(0x0);
  ram[DisableScreenFlag] = a; // reenable screen output
  return;
  
IncSubtask:
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  return;
  // -------------------------------------------------------------------------------------
}

void DisplayIntermediate(void) {
  // DisplayIntermediate:
  lda_abs_fz(OperMode); // check primary mode of operation
  if (zero_flag) { goto NoInter; } // if in title screen mode, skip this
  cmp_imm_fcz(GameOverModeValue); // are we in game over mode?
  if (zero_flag) { goto GameOverInter; } // if so, proceed to display game over screen
  lda_abs_fz(AltEntranceControl); // otherwise check for mode of alternate entry
  if (!zero_flag) { goto NoInter; } // and branch if found
  ldy_abs(AreaType); // check if we are on castle level
  cpy_imm_fczn(0x3); // and if so, branch (possibly residual)
  if (zero_flag) { goto PlayerInter; }
  lda_abs_fzn(DisableIntermediate); // if this flag is set, skip intermediate lives display
  if (!zero_flag) { goto NoInter; } // and jump to specific task, otherwise
  
PlayerInter:
  cpu_call_begin(0x86c4); DrawPlayer_Intermediate(); cpu_call_end(); // put player in appropriate place for
  lda_imm_fzn(0x1); // lives display, then output lives display to buffer
  // OutputInter:
  cpu_call_begin(0x86c9); WriteGameText(); cpu_call_end();
  cpu_call_begin(0x86cc); ResetScreenTimer(); cpu_call_end();
  lda_imm_fzn(0x0);
  ram[DisableScreenFlag] = a; // reenable screen output
  return;
  
GameOverInter:
  lda_imm(0x12); // set screen timer
  ram[ScreenTimer] = a;
  lda_imm_fzn(0x3); // output game over screen to buffer
  cpu_call_begin(0x86dc); WriteGameText(); cpu_call_end();
  goto IncModeTask_B;
  
NoInter:
  lda_imm_fzn(0x8); // set for specific task and leave
  ram[ScreenRoutineTask] = a;
  return;
  // -------------------------------------------------------------------------------------
  
IncModeTask_B:
  inc_abs_fzn(OperMode_Task); // move onto next mode
  return;
  // -------------------------------------------------------------------------------------
}

void AreaParserTaskControl(void) {
  inc_abs_fzn(DisableScreenFlag); // turn off screen
  
TaskLoop:
  cpu_call_begin(0x86eb); AreaParserTaskHandler(); cpu_call_end(); // render column set of current area
  lda_abs_fzn(AreaParserTaskNum); // check number of tasks
  if (!zero_flag) { goto TaskLoop; } // if tasks still not all done, do another one
  dec_abs_fn(ColumnSets); // do we need to render more column sets?
  if (neg_flag) {
    inc_abs(ScreenRoutineTask); // if not, move on to the next task
  }
  // OutputCol:
  lda_imm_fzn(0x6); // set vram buffer to output rendered column set
  ram[VRAM_Buffer_AddrCtrl] = a; // on next NMI
  return;
  // -------------------------------------------------------------------------------------
  // $00 - vram buffer address table low
  // $01 - vram buffer address table high
}

void DrawTitleScreen(void) {
  goto DrawTitleScreen;
  
SetVRAMAddr_B:
  ram[VRAM_Buffer_AddrCtrl] = a;
  // NoAltPal:
  goto IncSubtask; // now onto the next task
  // -------------------------------------------------------------------------------------
  
DrawTitleScreen:
  lda_abs_fz(OperMode); // are we in title screen mode?
  // if not, exit
  if (zero_flag) {
    lda_imm(HIGH_BYTE(TitleScreenDataOffset)); // load address $1ec0 into
    ppu_write_address(a); // the vram address register
    lda_imm(LOW_BYTE(TitleScreenDataOffset));
    ppu_write_address(a);
    lda_imm(0x3); // put address $0300 into
    ram[0x1] = a; // the indirect at $00
    ldy_imm(0x0);
    ram[0x0] = y;
    lda_abs(PPU_DATA); // do one garbage read
    
OutputTScr:
    lda_abs(PPU_DATA); // get title screen from chr-rom
    dynamic_ram_write(read_word(0x0) + y, a); // store 256 bytes into buffer
    iny_fz();
    // if not past 256 bytes, do not increment
    if (zero_flag) {
      inc_zp(0x1); // otherwise increment high byte of indirect
    }
    // ChkHiByte:
    lda_zp(0x1); // check high byte?
    cmp_imm_fz(0x4); // at $0400?
    if (!zero_flag) { goto OutputTScr; } // if not, loop back and do another
    cpy_imm_fc(0x3a); // check if offset points past end of data
    if (!carry_flag) { goto OutputTScr; } // if not, loop back and do another
    lda_imm(0x5); // set buffer transfer control to $0300,
    goto SetVRAMAddr_B; // increment task and exit
    // -------------------------------------------------------------------------------------
    
IncSubtask:
    inc_abs_fzn(ScreenRoutineTask); // move onto next task
    return;
    // -------------------------------------------------------------------------------------
  }
  // IncModeTask_B:
  inc_abs_fzn(OperMode_Task); // move onto next mode
  return;
  // -------------------------------------------------------------------------------------
}

void ClearBuffersDrawIcon(void) {
  lda_abs_fz(OperMode); // check game mode
  // if not title screen mode, leave
  if (zero_flag) {
    ldx_imm(0x0); // otherwise, clear buffer space
    
TScrClear:
    ram[VRAM_Buffer1 - 1 + x] = a;
    ram[VRAM_Buffer1 - 1 + 0x100 + x] = a;
    dex_fzn();
    if (!zero_flag) { goto TScrClear; }
    cpu_call_begin(0x8744); DrawMushroomIcon(); cpu_call_end(); // draw player select icon
    // IncSubtask:
    inc_abs_fzn(ScreenRoutineTask); // move onto next task
    return;
    // -------------------------------------------------------------------------------------
  }
  // IncModeTask_B:
  inc_abs_fzn(OperMode_Task); // move onto next mode
  return;
  // -------------------------------------------------------------------------------------
}

void WriteTopScore(void) {
  lda_imm_fzn(0xfa); // run display routine to display top score on title
  cpu_call_begin(0x874d); UpdateNumber(); cpu_call_end();
  // IncModeTask_B:
  inc_abs_fzn(OperMode_Task); // move onto next mode
  return;
  // -------------------------------------------------------------------------------------
}

void WriteGameText(void) {
  goto WriteGameText;
  
SetVRAMOffset:
  ram[VRAM_Buffer1_Offset] = a; // store as new vram buffer offset
  return;
  // -------------------------------------------------------------------------------------
  
WriteGameText:
  pha(); // save text number to stack
  asl_acc();
  tay(); // multiply by 2 and use as offset
  cpy_imm_fc(0x4); // if set to do top status bar or world/lives display,
  if (!carry_flag) { goto LdGameText; } // branch to use current offset as-is
  cpy_imm_fc(0x8); // if set to do time-up or game over,
  if (!carry_flag) { goto Chk2Players; } // branch to check players
  ldy_imm(0x8); // otherwise warp zone, therefore set offset
  
Chk2Players:
  lda_abs_fz(NumberOfPlayers); // check for number of players
  if (!zero_flag) { goto LdGameText; } // if there are two, use current offset to also print name
  iny(); // otherwise increment offset by one to not print name
  
LdGameText:
  ldx_absy(GameTextOffsets); // get offset to message we want to print
  ldy_imm(0x0);
  
GameTextLoop:
  lda_absx(GameText); // load message data
  cmp_imm_fz(0xff); // check for terminator
  if (zero_flag) { goto EndGameText; } // branch to end text if found
  ram[VRAM_Buffer1 + y] = a; // otherwise write data to buffer
  inx(); // and increment increment
  iny_fz();
  if (!zero_flag) { goto GameTextLoop; } // do this for 256 bytes if no terminator found
  
EndGameText:
  lda_imm(0x0); // put null terminator at end
  ram[VRAM_Buffer1 + y] = a;
  pla(); // pull original text number from stack
  tax();
  cmp_imm_fc(0x4); // are we printing warp zone?
  if (carry_flag) { goto PrintWarpZoneNumbers; }
  dex_fz(); // are we printing the world/lives display?
  if (!zero_flag) { goto CheckPlayerName; } // if not, branch to check player's name
  lda_abs(NumberofLives); // otherwise, check number of lives
  carry_flag = false; // and increment by one for display
  adc_imm(0x1);
  cmp_imm_fc(10); // more than 9 lives?
  if (!carry_flag) { goto PutLives; }
  sbc_imm_fc(10); // if so, subtract 10 and put a crown tile
  ldy_imm(0x9f); // next to the difference...strange things happen if
  ram[VRAM_Buffer1 + 7] = y; // the number of lives exceeds 19
  
PutLives:
  ram[VRAM_Buffer1 + 8] = a;
  ldy_abs(WorldNumber); // write world and level numbers (incremented for display)
  iny(); // to the buffer in the spaces surrounding the dash
  ram[VRAM_Buffer1 + 19] = y;
  ldy_abs(LevelNumber);
  iny_fzn();
  ram[VRAM_Buffer1 + 21] = y; // we're done here
  return;
  
CheckPlayerName:
  lda_abs_fzn(NumberOfPlayers); // check number of players
  if (zero_flag) { return; } // if only 1 player, leave
  lda_abs(CurrentPlayer); // load current player
  dex_fz(); // check to see if current message number is for time up
  if (!zero_flag) { goto ChkLuigi; }
  ldy_abs(OperMode); // check for game over mode
  cpy_imm_fz(GameOverModeValue);
  if (zero_flag) { goto ChkLuigi; }
  eor_imm(0b00000001); // if not, must be time up, invert d0 to do other player
  
ChkLuigi:
  lsr_acc_fczn();
  if (!carry_flag) { return; } // if mario is current player, do not change the name
  ldy_imm(0x4);
  
NameLoop:
  lda_absy(LuigiName); // otherwise, replace "MARIO" with "LUIGI"
  ram[VRAM_Buffer1 + 3 + y] = a;
  dey_fzn();
  if (!neg_flag) { goto NameLoop; } // do this until each letter is replaced
  // ExitChkName:
  return;
  
PrintWarpZoneNumbers:
  sbc_imm(0x4); // subtract 4 and then shift to the left
  asl_acc(); // twice to get proper warp zone number
  asl_acc(); // offset
  tax();
  ldy_imm(0x0);
  
WarpNumLoop:
  lda_absx(WarpZoneNumbers); // print warp zone numbers into the
  ram[VRAM_Buffer1 + 27 + y] = a; // placeholders from earlier
  inx();
  iny(); // put a number in every fourth space
  iny();
  iny();
  iny();
  cpy_imm_fc(0xc);
  if (!carry_flag) { goto WarpNumLoop; }
  lda_imm_fzn(0x2c); // load new buffer pointer at end of message
  goto SetVRAMOffset;
  // -------------------------------------------------------------------------------------
}

void ResetSpritesAndScreenTimer(void) {
  lda_abs_fzn(ScreenTimer); // check if screen timer has expired
  if (zero_flag) {
    cpu_call_begin(0x88a4); MoveAllSpritesOffscreen(); cpu_call_end(); // otherwise reset sprites now
    ResetScreenTimer(); // fallthrough
    return;
  }
}

void ResetScreenTimer(void) {
  lda_imm(0x7); // reset timer again
  ram[ScreenTimer] = a;
  inc_abs_fzn(ScreenRoutineTask); // move onto next task
  // NoReset:
  return;
  // -------------------------------------------------------------------------------------
  // $00 - temp vram buffer offset
  // $01 - temp metatile buffer offset
  // $02 - temp metatile graphics table offset
  // $03 - used to store attribute bits
  // $04 - used to determine attribute table row
  // $05 - used to determine attribute table column
  // $06 - metatile graphics table address low
  // $07 - metatile graphics table address high
}

void RenderAreaGraphics(void) {
  // RenderAreaGraphics:
  lda_abs(CurrentColumnPos); // store LSB of where we're at
  and_imm(0x1);
  ram[0x5] = a;
  ldy_abs(VRAM_Buffer2_Offset); // store vram buffer offset
  ram[0x0] = y;
  lda_abs(CurrentNTAddr_Low); // get current name table address we're supposed to render
  ram[VRAM_Buffer2 + 1 + y] = a;
  lda_abs(CurrentNTAddr_High);
  ram[VRAM_Buffer2 + y] = a;
  lda_imm(0x9a); // store length byte of 26 here with d7 set
  ram[VRAM_Buffer2 + 2 + y] = a; // to increment by 32 (in columns)
  lda_imm(0x0); // init attribute row
  ram[0x4] = a;
  tax();
  
DrawMTLoop:
  ram[0x1] = x; // store init value of 0 or incremented offset for buffer
  lda_absx(MetatileBuffer); // get first metatile number, and mask out all but 2 MSB
  and_imm(0b11000000);
  ram[0x3] = a; // store attribute table bits here
  asl_acc_fc(); // note that metatile format is:
  rol_acc_fc(); // %xx000000 - attribute table bits, 
  rol_acc(); // %00xxxxxx - metatile number
  tay(); // rotate bits to d1-d0 and use as offset here
  lda_absy(MetatileGraphics_Low); // get address to graphics table from here
  ram[0x6] = a;
  lda_absy(MetatileGraphics_High);
  ram[0x7] = a;
  lda_absx(MetatileBuffer); // get metatile number again
  asl_acc(); // multiply by 4 and use as tile offset
  asl_acc();
  ram[0x2] = a;
  lda_abs(AreaParserTaskNum); // get current task number for level processing and
  and_imm(0b00000001); // mask out all but LSB, then invert LSB, multiply by 2
  eor_imm(0b00000001); // to get the correct column position in the metatile,
  asl_acc_fc(); // then add to the tile offset so we can draw either side
  adc_zp(0x2); // of the metatiles
  tay();
  ldx_zp(0x0); // use vram buffer offset from before as X
  lda_indy(0x6);
  ram[VRAM_Buffer2 + 3 + x] = a; // get first tile number (top left or top right) and store
  iny();
  lda_indy(0x6); // now get the second (bottom left or bottom right) and store
  ram[VRAM_Buffer2 + 4 + x] = a;
  ldy_zp(0x4); // get current attribute row
  lda_zp_fz(0x5); // get LSB of current column where we're at, and
  if (!zero_flag) { goto RightCheck; } // branch if set (clear = left attrib, set = right)
  lda_zp(0x1); // get current row we're rendering
  lsr_acc_fc(); // branch if LSB set (clear = top left, set = bottom left)
  if (carry_flag) { goto LLeft; }
  rol_zp_fc(0x3); // rotate attribute bits 3 to the left
  rol_zp_fc(0x3); // thus in d1-d0, for upper left square
  rol_zp(0x3);
  goto SetAttrib;
  
RightCheck:
  lda_zp(0x1); // get LSB of current row we're rendering
  lsr_acc_fc(); // branch if set (clear = top right, set = bottom right)
  if (carry_flag) { goto NextMTRow; }
  lsr_zp(0x3); // shift attribute bits 4 to the right
  lsr_zp(0x3); // thus in d3-d2, for upper right square
  lsr_zp(0x3);
  lsr_zp(0x3);
  goto SetAttrib;
  
LLeft:
  lsr_zp(0x3); // shift attribute bits 2 to the right
  lsr_zp(0x3); // thus in d5-d4 for lower left square
  
NextMTRow:
  inc_zp(0x4); // move onto next attribute row  
  
SetAttrib:
  lda_absy(AttributeBuffer); // get previously saved bits from before
  ora_zp(0x3); // if any, and put new bits, if any, onto
  ram[AttributeBuffer + y] = a; // the old, and store
  inc_zp(0x0); // increment vram buffer offset by 2
  inc_zp(0x0);
  ldx_zp(0x1); // get current gfx buffer row, and check for
  inx(); // the bottom of the screen
  cpx_imm_fc(0xd);
  if (!carry_flag) { goto DrawMTLoop; } // if not there yet, loop back
  ldy_zp(0x0); // get current vram buffer offset, increment by 3
  iny(); // (for name table address and length bytes)
  iny();
  iny();
  lda_imm(0x0);
  ram[VRAM_Buffer2 + y] = a; // put null terminator at end of data for name table
  ram[VRAM_Buffer2_Offset] = y; // store new buffer offset
  inc_abs(CurrentNTAddr_Low); // increment name table address low
  lda_abs(CurrentNTAddr_Low); // check current low byte
  and_imm_fz(0b00011111); // if no wraparound, just skip this part
  if (!zero_flag) { goto ExitDrawM; }
  lda_imm(0x80); // if wraparound occurs, make sure low byte stays
  ram[CurrentNTAddr_Low] = a; // just under the status bar
  lda_abs(CurrentNTAddr_High); // and then invert d2 of the name table address high
  eor_imm(0b00000100); // to move onto the next appropriate name table
  ram[CurrentNTAddr_High] = a;
  
ExitDrawM:
  goto SetVRAMCtrl; // jump to set buffer to $0341 and leave
  // -------------------------------------------------------------------------------------
  // $00 - temp attribute table address high (big endian order this time!)
  // $01 - temp attribute table address low
  
SetVRAMCtrl:
  lda_imm_fzn(0x6);
  ram[VRAM_Buffer_AddrCtrl] = a; // set buffer to $0341 and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used as temporary counter in ColorRotation
}

void RenderAttributeTables(void) {
  lda_abs(CurrentNTAddr_Low); // get low byte of next name table address
  and_imm(0b00011111); // to be written to, mask out all but 5 LSB,
  carry_flag = true; // subtract four 
  sbc_imm_fc(0x4);
  and_imm(0b00011111); // mask out bits again and store
  ram[0x1] = a;
  lda_abs(CurrentNTAddr_High); // get high byte and branch if borrow not set
  if (!carry_flag) {
    eor_imm(0b00000100); // otherwise invert d2
  }
  // SetATHigh:
  and_imm(0b00000100); // mask out all other bits
  ora_imm(0x23); // add $2300 to the high byte and store
  ram[0x0] = a;
  lda_zp(0x1); // get low byte - 4, divide by 4, add offset for
  lsr_acc(); // attribute table and store
  lsr_acc_fc();
  adc_imm(0xc0); // we should now have the appropriate block of
  ram[0x1] = a; // attribute table in our temp address
  ldx_imm(0x0);
  ldy_abs(VRAM_Buffer2_Offset); // get buffer offset
  
AttribLoop:
  lda_zp(0x0);
  ram[VRAM_Buffer2 + y] = a; // store high byte of attribute table address
  lda_zp(0x1);
  carry_flag = false; // get low byte, add 8 because we want to start
  adc_imm(0x8); // below the status bar, and store
  ram[VRAM_Buffer2 + 1 + y] = a;
  ram[0x1] = a; // also store in temp again
  lda_absx(AttributeBuffer); // fetch current attribute table byte and store
  ram[VRAM_Buffer2 + 3 + y] = a; // in the buffer
  lda_imm(0x1);
  ram[VRAM_Buffer2 + 2 + y] = a; // store length of 1 in buffer
  lsr_acc();
  ram[AttributeBuffer + x] = a; // clear current byte in attribute buffer
  iny(); // increment buffer offset by 4 bytes
  iny();
  iny();
  iny();
  inx(); // increment attribute offset and check to see
  cpx_imm_fc(0x7); // if we're at the end yet
  if (!carry_flag) { goto AttribLoop; }
  ram[VRAM_Buffer2 + y] = a; // put null terminator at the end
  ram[VRAM_Buffer2_Offset] = y; // store offset in case we want to do any more
  // SetVRAMCtrl:
  lda_imm_fzn(0x6);
  ram[VRAM_Buffer_AddrCtrl] = a; // set buffer to $0341 and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used as temporary counter in ColorRotation
}

void ColorRotation(void) {
  // ColorRotation:
  lda_zp(FrameCounter); // get frame counter
  and_imm_fzn(0x7); // mask out all but three LSB
  if (!zero_flag) { return; } // branch if not set to zero to do this every eighth frame
  ldx_abs(VRAM_Buffer1_Offset); // check vram buffer offset
  cpx_imm_fczn(0x31);
  if (carry_flag) { return; } // if offset over 48 bytes, branch to leave
  tay(); // otherwise use frame counter's 3 LSB as offset here
  
GetBlankPal:
  lda_absy(BlankPalette); // get blank palette for palette 3
  ram[VRAM_Buffer1 + x] = a; // store it in the vram buffer
  inx(); // increment offsets
  iny();
  cpy_imm_fc(0x8);
  if (!carry_flag) { goto GetBlankPal; } // do this until all bytes are copied
  ldx_abs(VRAM_Buffer1_Offset); // get current vram buffer offset
  lda_imm(0x3);
  ram[0x0] = a; // set counter here
  lda_abs(AreaType); // get area type
  asl_acc(); // multiply by 4 to get proper offset
  asl_acc();
  tay(); // save as offset here
  
GetAreaPal:
  lda_absy(Palette3Data); // fetch palette to be written based on area type
  ram[VRAM_Buffer1 + 3 + x] = a; // store it to overwrite blank palette in vram buffer
  iny();
  inx();
  dec_zp_fn(0x0); // decrement counter
  if (!neg_flag) { goto GetAreaPal; } // do this until the palette is all copied
  ldx_abs(VRAM_Buffer1_Offset); // get current vram buffer offset
  ldy_abs(ColorRotateOffset); // get color cycling offset
  lda_absy(ColorRotatePalette);
  ram[VRAM_Buffer1 + 4 + x] = a; // get and store current color in second slot of palette
  lda_abs(VRAM_Buffer1_Offset);
  carry_flag = false; // add seven bytes to vram buffer offset
  adc_imm(0x7);
  ram[VRAM_Buffer1_Offset] = a;
  inc_abs(ColorRotateOffset); // increment color cycling offset
  lda_abs(ColorRotateOffset);
  cmp_imm_fczn(0x6); // check to see if it's still in range
  if (!carry_flag) { return; } // if so, branch to leave
  lda_imm_fzn(0x0);
  ram[ColorRotateOffset] = a; // otherwise, init to keep it in range
  // ExitColorRot:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00 - temp store for offset control bit
  // $01 - temp vram buffer offset
  // $02 - temp store for vertical high nybble in block buffer routine
  // $03 - temp adder for high byte of name table address
  // $04, $05 - name table address low/high
  // $06, $07 - block buffer address low/high
}

void RemoveCoin_Axe(void) {
  ldy_imm(0x41); // set low byte so offset points to $0341
  lda_imm(0x3); // load offset for default blank metatile
  ldx_abs_fzn(AreaType); // check area type
  // if not water type, use offset
  if (zero_flag) {
    lda_imm_fzn(0x4); // otherwise load offset for blank metatile used in water
  }
  // WriteBlankMT:
  cpu_call_begin(0x8a5a); PutBlockMetatile(); cpu_call_end(); // do a sub to write blank metatile to vram buffer
  lda_imm_fzn(0x6);
  ram[VRAM_Buffer_AddrCtrl] = a; // set vram address controller to $0341 and leave
  return;
}

void ReplaceBlockMetatile(void) {
  cpu_call_begin(0x8a63); WriteBlockMetatile(); cpu_call_end(); // write metatile to vram buffer to replace block object
  inc_abs(Block_ResidualCounter); // increment unused counter (residual code)
  dec_absx_fzn(Block_RepFlag); // decrement flag (residual code)
  return; // leave
}

void DestroyBlockMetatile(void) {
  lda_imm(0x0); // force blank metatile if branched/jumped to this point
  WriteBlockMetatile(); // fallthrough
  return;
}

void WriteBlockMetatile(void) {
  ldy_imm(0x3); // load offset for blank metatile
  cmp_imm_fcz(0x0); // check contents of A for blank metatile
  // branch if found (unconditional if branched from 8a6b)
  if (!zero_flag) {
    ldy_imm(0x0); // load offset for brick metatile w/ line
    cmp_imm_fcz(0x58);
    // use offset if metatile is brick with coins (w/ line)
    if (!zero_flag) {
      cmp_imm_fcz(0x51);
      // use offset if metatile is breakable brick w/ line
      if (!zero_flag) {
        iny(); // increment offset for brick metatile w/o line
        cmp_imm_fcz(0x5d);
        // use offset if metatile is brick with coins (w/o line)
        if (!zero_flag) {
          cmp_imm_fcz(0x52);
          // use offset if metatile is breakable brick w/o line
          if (!zero_flag) {
            iny(); // if any other metatile, increment offset for empty block
          }
        }
      }
    }
  }
  // UseBOffset:
  tya(); // put Y in A
  ldy_abs(VRAM_Buffer1_Offset); // get vram buffer offset
  iny_fzn(); // move onto next byte
  cpu_call_begin(0x8a8e); PutBlockMetatile(); cpu_call_end(); // get appropriate block data and write to vram buffer
  MoveVOffset(); // fallthrough
  return;
}

void MoveVOffset(void) {
  goto MoveVOffset;
  
SetVRAMOffset:
  ram[VRAM_Buffer1_Offset] = a; // store as new vram buffer offset
  return;
  // -------------------------------------------------------------------------------------
  
MoveVOffset:
  dey(); // decrement vram buffer offset
  tya(); // add 10 bytes to it
  carry_flag = false;
  adc_imm_fczn(10);
  goto SetVRAMOffset; // branch to store as new vram buffer offset
}

void PutBlockMetatile(void) {
  ram[0x0] = x; // store control bit from SprDataOffset_Ctrl
  ram[0x1] = y; // store vram buffer offset for next byte
  asl_acc();
  asl_acc(); // multiply A by four and use as X
  tax();
  ldy_imm(0x20); // load high byte for name table 0
  lda_zp(0x6); // get low byte of block buffer pointer
  cmp_imm_fc(0xd0); // check to see if we're on odd-page block buffer
  // if not, use current high byte
  if (carry_flag) {
    ldy_imm(0x24); // otherwise load high byte for name table 1
  }
  // SaveHAdder:
  ram[0x3] = y; // save high byte here
  and_imm(0xf); // mask out high nybble of block buffer pointer
  asl_acc(); // multiply by 2 to get appropriate name table low byte
  ram[0x4] = a; // and then store it here
  lda_imm(0x0);
  ram[0x5] = a; // initialize temp high byte
  lda_zp(0x2); // get vertical high nybble offset used in block buffer routine
  carry_flag = false;
  adc_imm(0x20); // add 32 pixels for the status bar
  asl_acc_fc();
  rol_zp(0x5); // shift and rotate d7 onto d0 and d6 into carry
  asl_acc_fc();
  rol_zp_fc(0x5); // shift and rotate d6 onto d0 and d5 into carry
  adc_zp_fc(0x4); // add low byte of name table and carry to vertical high nybble
  ram[0x4] = a; // and store here
  lda_zp(0x5); // get whatever was in d7 and d6 of vertical high nybble
  adc_imm(0x0); // add carry
  carry_flag = false;
  adc_zp(0x3); // then add high byte of name table
  ram[0x5] = a; // store here
  ldy_zp(0x1); // get vram buffer offset to be used
  RemBridge(); // fallthrough
  return;
}

void RemBridge(void) {
  lda_absx(BlockGfxData); // write top left and top right
  ram[VRAM_Buffer1 + 2 + y] = a; // tile numbers into first spot
  lda_absx(BlockGfxData + 1);
  ram[VRAM_Buffer1 + 3 + y] = a;
  lda_absx(BlockGfxData + 2); // write bottom left and bottom
  ram[VRAM_Buffer1 + 7 + y] = a; // right tiles numbers into
  lda_absx(BlockGfxData + 3); // second spot
  ram[VRAM_Buffer1 + 8 + y] = a;
  lda_zp(0x4);
  ram[VRAM_Buffer1 + y] = a; // write low byte of name table
  carry_flag = false; // into first slot as read
  adc_imm_fc(0x20); // add 32 bytes to value
  ram[VRAM_Buffer1 + 5 + y] = a; // write low byte of name table
  lda_zp(0x5); // plus 32 bytes into second slot
  ram[VRAM_Buffer1 - 1 + y] = a; // write high byte of name
  ram[VRAM_Buffer1 + 4 + y] = a; // table address to both slots
  lda_imm(0x2);
  ram[VRAM_Buffer1 + 1 + y] = a; // put length of 2 in
  ram[VRAM_Buffer1 + 6 + y] = a; // both slots
  lda_imm(0x0);
  ram[VRAM_Buffer1 + 9 + y] = a; // put null terminator at end
  ldx_zp_fzn(0x0); // get offset control bit here
  return; // and leave
  // -------------------------------------------------------------------------------------
  // METATILE GRAPHICS TABLE
}

void InitializeNameTables(void) {
  lda_abs(PPU_STATUS); // reset flip-flop
  lda_abs(Mirror_PPU_CTRL_REG1); // load mirror of ppu reg $2000
  ora_imm(0b00010000); // set sprites for first 4k and background for second 4k
  and_imm_fzn(0b11110000); // clear rest of lower nybble, leave higher alone
  cpu_call_begin(0x8e25); WritePPUReg1(); cpu_call_end();
  lda_imm_fzn(0x24); // set vram address to start of name table 1
  cpu_call_begin(0x8e2a); WriteNTAddr(); cpu_call_end();
  lda_imm(0x20); // and then set it to name table 0
  WriteNTAddr(); // fallthrough
  return;
}

void WriteNTAddr(void) {
  ppu_write_address(a);
  lda_imm(0x0);
  ppu_write_address(a);
  ldx_imm(0x4); // clear name table with blank tile #24
  ldy_imm(0xc0);
  lda_imm(0x24);
  
InitNTLoop:
  ppu_write_data(a); // count out exactly 768 tiles
  dey_fz();
  if (!zero_flag) { goto InitNTLoop; }
  dex_fz();
  if (!zero_flag) { goto InitNTLoop; }
  ldy_imm(64); // now to clear the attribute table (with zero this time)
  txa();
  ram[VRAM_Buffer1_Offset] = a; // init vram buffer 1 offset
  ram[VRAM_Buffer1] = a; // init vram buffer 1
  
InitATLoop:
  ppu_write_data(a);
  dey_fzn();
  if (!zero_flag) { goto InitATLoop; }
  ram[HorizontalScroll] = a; // reset scroll variables
  ram[VerticalScroll] = a;
  InitScroll(); return; // initialize scroll registers to zero
  // -------------------------------------------------------------------------------------
  // $00 - temp joypad bit
}

void ReadJoypads(void) {
  lda_imm(0x1); // reset and clear strobe of joypad ports
  write_joypad1(a);
  lsr_acc_fc();
  tax_fzn(); // start with joypad 1's port
  write_joypad1(a);
  cpu_call_begin(0x8e68); ReadPortBits(); cpu_call_end();
  inx(); // increment for joypad 2's port
  ReadPortBits(); // fallthrough
  return;
}

void ReadPortBits(void) {
  ldy_imm(0x8);
  
PortLoop:
  pha(); // push previous bit onto stack
  lda_absx(JOYPAD_PORT); // read current bit on joypad port
  ram[0x0] = a; // check d1 and d0 of port output
  lsr_acc(); // this is necessary on the old
  ora_zp(0x0); // famicom systems in japan
  lsr_acc_fc();
  pla(); // read bits from stack
  rol_acc_fc(); // rotate bit from carry flag
  dey_fz();
  if (!zero_flag) { goto PortLoop; } // count down bits left
  ram[SavedJoypadBits + x] = a; // save controller status here always
  pha();
  and_imm(0b00110000); // check for select or start
  and_absx_fz(JoypadBitMask); // if neither saved state nor current state
  // have any of these two set, branch
  if (!zero_flag) {
    pla();
    and_imm_fzn(0b11001111); // otherwise store without select
    ram[SavedJoypadBits + x] = a; // or start bits and leave
    return;
  }
  // Save8Bits:
  pla_fzn();
  ram[JoypadBitMask + x] = a; // save with all bits in another place and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - vram buffer address table low
  // $01 - vram buffer address table high
}

void UpdateScreen(void) {
  goto UpdateScreen;
  
WriteBufferToScreen:
  ppu_write_address(a); // store high byte of vram address
  iny();
  lda_indy(0x0); // load next byte (second)
  ppu_write_address(a); // store low byte of vram address
  iny();
  lda_indy(0x0); // load next byte (third)
  asl_acc_fc(); // shift to left and save in stack
  pha();
  lda_abs(Mirror_PPU_CTRL_REG1); // load mirror of $2000,
  ora_imm_fzn(0b00000100); // set ppu to increment by 32 by default
  // if d7 of third byte was clear, ppu will
  if (!carry_flag) {
    and_imm_fzn(0b11111011); // only increment by 1
  }
  // SetupWrites:
  cpu_call_begin(0x8eab); WritePPUReg1(); cpu_call_end(); // write to register
  pla(); // pull from stack and shift to left again
  asl_acc_fc();
  // if d6 of third byte was clear, do not repeat byte
  if (carry_flag) {
    ora_imm(0b00000010); // otherwise set d1 and increment Y
    iny();
  }
  // GetLength:
  lsr_acc(); // shift back to the right to get proper length
  lsr_acc_fc(); // note that d1 will now be in carry
  tax();
  
OutputToVRAM:
  // if carry set, repeat loading the same byte
  if (!carry_flag) {
    iny(); // otherwise increment Y to load next byte
  }
  // RepeatByte:
  lda_indy(0x0); // load more data from buffer and write to vram
  ppu_write_data(a);
  dex_fz(); // done writing?
  if (!zero_flag) { goto OutputToVRAM; }
  carry_flag = true;
  tya();
  adc_zp_fc(0x0); // add end length plus one to the indirect at $00
  ram[0x0] = a; // to allow this routine to read another set of updates
  lda_imm(0x0);
  adc_zp_fc(0x1);
  ram[0x1] = a;
  lda_imm(0x3f); // sets vram address to $3f00
  ppu_write_address(a);
  lda_imm(0x0);
  ppu_write_address(a);
  ppu_write_address(a); // then reinitializes it for some reason
  ppu_write_address(a);
  
UpdateScreen:
  ldx_abs(PPU_STATUS); // reset flip-flop
  ldy_imm(0x0); // load first byte from indirect as a pointer
  lda_indy_fzn(0x0);
  if (!zero_flag) { goto WriteBufferToScreen; } // if byte is zero we have no further updates to make here
  InitScroll(); // fallthrough
  return;
}

void InitScroll(void) {
  ppu_write_scroll(a); // store contents of A into scroll registers
  ppu_write_scroll(a); // and end whatever subroutine led us here
  return;
  // -------------------------------------------------------------------------------------
}

void WritePPUReg1(void) {
  ppu_ctrl = a; // write contents of A to PPU register 1
  ram[Mirror_PPU_CTRL_REG1] = a; // and its mirror
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used to store status bar nybbles
  // $02 - used as temp vram offset
  // $03 - used to store length of status bar number
  // status bar name table offset and length data
}

void PrintStatusBarNumbers(void) {
  ram[0x0] = a; // store player-specific offset
  cpu_call_begin(0x8f0a); OutputNumbers(); cpu_call_end(); // use first nybble to print the coin display
  lda_zp(0x0); // move high nybble to low
  lsr_acc(); // and print to score display
  lsr_acc();
  lsr_acc();
  lsr_acc();
  OutputNumbers(); // fallthrough
  return;
}

void OutputNumbers(void) {
  carry_flag = false; // add 1 to low nybble
  adc_imm(0x1);
  and_imm(0b00001111); // mask out high nybble
  cmp_imm_fczn(0x6);
  if (!carry_flag) {
    pha(); // save incremented value to stack for now and
    asl_acc(); // shift to left and use as offset
    tay();
    ldx_abs(VRAM_Buffer1_Offset); // get current buffer pointer
    lda_imm(0x20); // put at top of screen by default
    cpy_imm_fz(0x0); // are we writing top score on title screen?
    if (zero_flag) {
      lda_imm(0x22); // if so, put further down on the screen
    }
    // SetupNums:
    ram[VRAM_Buffer1 + x] = a;
    lda_absy(StatusBarData); // write low vram address and length of thing
    ram[VRAM_Buffer1 + 1 + x] = a; // we're printing to the buffer
    lda_absy(StatusBarData + 1);
    ram[VRAM_Buffer1 + 2 + x] = a;
    ram[0x3] = a; // save length byte in counter
    ram[0x2] = x; // and buffer pointer elsewhere for now
    pla(); // pull original incremented value from stack
    tax();
    lda_absx(StatusBarOffset); // load offset to value we want to write
    carry_flag = true;
    sbc_absy_fc(StatusBarData + 1); // subtract from length byte we read before
    tay(); // use value as offset to display digits
    ldx_zp(0x2);
    
DigitPLoop:
    lda_absy(DisplayDigits); // write digits to the buffer
    ram[VRAM_Buffer1 + 3 + x] = a;
    inx();
    iny();
    dec_zp_fz(0x3); // do this until all the digits are written
    if (!zero_flag) { goto DigitPLoop; }
    lda_imm(0x0); // put null terminator at end
    ram[VRAM_Buffer1 + 3 + x] = a;
    inx(); // increment buffer pointer by 3
    inx();
    inx_fzn();
    ram[VRAM_Buffer1_Offset] = x; // store it in case we want to use it again
    // ExitOutputN:
    return;
    // -------------------------------------------------------------------------------------
  }
}

void DigitsMathRoutine(void) {
  // DigitsMathRoutine:
  lda_abs(OperMode); // check mode of operation
  cmp_imm_fcz(TitleScreenModeValue);
  if (zero_flag) { goto EraseDMods; } // if in title screen mode, branch to lock score
  ldx_imm(0x5);
  
AddModLoop:
  lda_absx(DigitModifier); // load digit amount to increment
  carry_flag = false;
  adc_absy_fcn(DisplayDigits); // add to current digit
  if (neg_flag) { goto BorrowOne; } // if result is a negative number, branch to subtract
  cmp_imm_fc(10);
  if (carry_flag) { goto CarryOne; } // if digit greater than $09, branch to add
  
StoreNewD:
  ram[DisplayDigits + y] = a; // store as new score or game timer digit
  dey(); // move onto next digits in score or game timer
  dex_fn(); // and digit amounts to increment
  if (!neg_flag) { goto AddModLoop; } // loop back if we're not done yet
  
EraseDMods:
  lda_imm(0x0); // store zero here
  ldx_imm(0x6); // start with the last digit
  
EraseMLoop:
  ram[DigitModifier - 1 + x] = a; // initialize the digit amounts to increment
  dex_fzn();
  if (!neg_flag) { goto EraseMLoop; } // do this until they're all reset, then leave
  return;
  
BorrowOne:
  dec_absx(DigitModifier - 1); // decrement the previous digit, then put $09 in
  lda_imm(0x9); // the game timer digit we're currently on to "borrow
  goto StoreNewD; // the one", then do an unconditional branch back
  
CarryOne:
  carry_flag = true; // subtract ten from our digit to make it a
  sbc_imm_fc(10); // proper BCD number, then increment the digit
  inc_absx(DigitModifier - 1); // preceding current digit to "carry the one" properly
  goto StoreNewD; // go back to just after we branched here
  // -------------------------------------------------------------------------------------
}

void UpdateTopScore(void) {
  ldx_imm_fzn(0x5); // start with mario's score
  cpu_call_begin(0x8f9b); TopScoreCheck(); cpu_call_end();
  ldx_imm(0xb); // now do luigi's score
  TopScoreCheck(); // fallthrough
  return;
}

void TopScoreCheck(void) {
  ldy_imm(0x5); // start with the lowest digit
  carry_flag = true;
  
GetScoreDiff:
  lda_absx(PlayerScoreDisplay); // subtract each player digit from each high score digit
  sbc_absy_fc(TopScoreDisplay); // from lowest to highest, if any top score digit exceeds
  dex(); // any player digit, borrow will be set until a subsequent
  dey_fzn(); // subtraction clears it (player digit is higher than top)
  if (!neg_flag) { goto GetScoreDiff; }
  if (carry_flag) {
    inx(); // increment X and Y once to the start of the score
    iny();
    
CopyScore:
    lda_absx(PlayerScoreDisplay); // store player's score digits into high score memory area
    ram[TopScoreDisplay + y] = a;
    inx();
    iny();
    cpy_imm_fczn(0x6); // do this until we have stored them all
    if (!carry_flag) { goto CopyScore; }
    // NoTopSc:
    return;
    // -------------------------------------------------------------------------------------
  }
}

void InitializeGame(void) {
  ldy_imm_fzn(0x6f); // clear all memory as in initialization procedure,
  cpu_call_begin(0x8fd3); InitializeMemory(); cpu_call_end(); // but this time, clear only as far as $076f
  ldy_imm(0x1f);
  
ClrSndLoop:
  ram[SoundMemory + y] = a; // clear out memory used
  dey_fn(); // by the sound engines
  if (!neg_flag) { goto ClrSndLoop; }
  lda_imm_fzn(0x18); // set demo timer
  ram[DemoTimer] = a;
  cpu_call_begin(0x8fe3); LoadAreaPointer(); cpu_call_end();
  InitializeArea(); // fallthrough
  return;
}

void InitializeArea(void) {
  // InitializeArea:
  ldy_imm_fzn(0x4b); // clear all memory again, only as far as $074b
  cpu_call_begin(0x8fe8); InitializeMemory(); cpu_call_end(); // this is only necessary if branching from
  ldx_imm(0x21);
  lda_imm(0x0);
  
ClrTimersLoop:
  ram[Timers + x] = a; // clear out memory between
  dex_fn(); // $0780 and $07a1
  if (!neg_flag) { goto ClrTimersLoop; }
  lda_abs(HalfwayPage);
  ldy_abs_fzn(AltEntranceControl); // if AltEntranceControl not set, use halfway page, if any found
  if (zero_flag) { goto StartPage; }
  lda_abs_fzn(EntrancePage); // otherwise use saved entry page number here
  
StartPage:
  ram[ScreenLeft_PageLoc] = a; // set as value here
  ram[CurrentPageLoc] = a; // also set as current page
  ram[BackloadingFlag] = a; // set flag here if halfway page or saved entry page number found
  cpu_call_begin(0x9009); GetScreenPosition(); cpu_call_end(); // get pixel coordinates for screen borders
  ldy_imm(0x20); // if on odd numbered page, use $2480 as start of rendering
  and_imm_fz(0b00000001); // otherwise use $2080, this address used later as name table
  if (zero_flag) { goto SetInitNTHigh; } // address for rendering of game area
  ldy_imm(0x24);
  
SetInitNTHigh:
  ram[CurrentNTAddr_High] = y; // store name table address
  ldy_imm(0x80);
  ram[CurrentNTAddr_Low] = y;
  asl_acc(); // store LSB of page number in high nybble
  asl_acc(); // of block buffer column position
  asl_acc();
  asl_acc_fc();
  ram[BlockBufferColumnPos] = a;
  dec_abs(AreaObjectLength); // set area object lengths for all empty
  dec_abs(AreaObjectLength + 1);
  dec_abs(AreaObjectLength + 2);
  lda_imm_fzn(0xb); // set value for renderer to update 12 column sets
  ram[ColumnSets] = a; // 12 column sets = 24 metatile columns = 1 1/2 screens
  cpu_call_begin(0x9031); GetAreaDataAddrs(); cpu_call_end(); // get enemy and level addresses and load header
  lda_abs_fz(PrimaryHardMode); // check to see if primary hard mode has been activated
  if (!zero_flag) { goto SetSecHard; } // if so, activate the secondary no matter where we're at
  lda_abs(WorldNumber); // otherwise check world number
  cmp_imm_fcz(World5); // if less than 5, do not activate secondary
  if (!carry_flag) { goto CheckHalfway; }
  if (!zero_flag) { goto SetSecHard; } // if not equal to, then world > 5, thus activate
  lda_abs(LevelNumber); // otherwise, world 5, so check level number
  cmp_imm_fc(Level3); // if 1 or 2, do not set secondary hard mode flag
  if (!carry_flag) { goto CheckHalfway; }
  
SetSecHard:
  inc_abs(SecondaryHardMode); // set secondary hard mode flag for areas 5-3 and beyond
  
CheckHalfway:
  lda_abs_fz(HalfwayPage);
  if (zero_flag) { goto DoneInitArea; }
  lda_imm(0x2); // if halfway page set, overwrite start position from header
  ram[PlayerEntranceCtrl] = a;
  
DoneInitArea:
  lda_imm(Silence); // silence music
  ram[AreaMusicQueue] = a;
  lda_imm(0x1); // disable screen output
  ram[DisableScreenFlag] = a;
  inc_abs_fzn(OperMode_Task); // increment one of the modes
  return;
  // -------------------------------------------------------------------------------------
}

void PrimaryGameSetup(void) {
  lda_imm(0x1);
  ram[FetchNewGameTimerFlag] = a; // set flag to load game timer from header
  ram[PlayerSize] = a; // set player's size to small
  lda_imm(0x2);
  ram[NumberofLives] = a; // give each player three lives
  ram[OffScr_NumberofLives] = a;
  SecondaryGameSetup(); // fallthrough
  return;
}

void SecondaryGameSetup(void) {
  lda_imm(0x0);
  ram[DisableScreenFlag] = a; // enable screen output
  tay();
  
ClearVRLoop:
  ram[VRAM_Buffer1 - 1 + y] = a; // clear buffer at $0300-$03ff
  iny_fz();
  if (!zero_flag) { goto ClearVRLoop; }
  ram[GameTimerExpiredFlag] = a; // clear game timer exp flag
  ram[DisableIntermediate] = a; // clear skip lives display flag
  ram[BackloadingFlag] = a; // clear value here
  lda_imm(0xff);
  ram[BalPlatformAlignment] = a; // initialize balance platform assignment flag
  lda_abs(ScreenLeft_PageLoc); // get left side page location
  lsr_abs_fc(Mirror_PPU_CTRL_REG1); // shift LSB of ppu register #1 mirror out
  and_imm(0x1); // mask out all but LSB of page location
  ror_acc_fc(); // rotate LSB of page location into carry then onto mirror
  rol_abs_fczn(Mirror_PPU_CTRL_REG1); // this is to set the proper PPU name table
  cpu_call_begin(0x9099); GetAreaMusic(); cpu_call_end(); // load proper music into queue
  lda_imm(0x38); // load sprite shuffle amounts to be used later
  ram[SprShuffleAmt + 2] = a;
  lda_imm(0x48);
  ram[SprShuffleAmt + 1] = a;
  lda_imm(0x58);
  ram[SprShuffleAmt] = a;
  ldx_imm(0xe); // load default OAM offsets into $06e4-$06f2
  
ShufAmtLoop:
  lda_absx(DefaultSprOffsets);
  ram[SprDataOffset + x] = a;
  dex_fn(); // do this until they're all set
  if (!neg_flag) { goto ShufAmtLoop; }
  ldy_imm(0x3); // set up sprite #0
  
ISpr0Loop:
  lda_absy(Sprite0Data);
  ram[Sprite_Data + y] = a;
  dey_fzn();
  if (!neg_flag) { goto ISpr0Loop; }
  cpu_call_begin(0x90c1); DoNothing2(); cpu_call_end(); // these jsrs doesn't do anything useful
  cpu_call_begin(0x90c4); DoNothing1(); cpu_call_end();
  inc_abs(Sprite0HitDetectFlag); // set sprite #0 check flag
  inc_abs_fzn(OperMode_Task); // increment to next task
  return;
  // -------------------------------------------------------------------------------------
  // $06 - RAM address low
  // $07 - RAM address high
}

void InitializeMemory(void) {
  // InitializeMemory:
  ldx_imm(0x7); // set initial high byte to $0700-$07ff
  lda_imm(0x0); // set initial low byte to start of page (at $00 of page)
  ram[0x6] = a;
  
InitPageLoop:
  ram[0x7] = x;
  
InitByteLoop:
  cpx_imm_fz(0x1); // check to see if we're on the stack ($0100-$01ff)
  if (!zero_flag) { goto InitByte; } // if not, go ahead anyway
  cpy_imm_fc(0x60); // otherwise, check to see if we're at $0160-$01ff
  if (carry_flag) { goto SkipByte; } // if so, skip write
  
InitByte:
  dynamic_ram_write(read_word(0x6) + y, a); // otherwise, initialize byte with current low byte in Y
  
SkipByte:
  dey();
  cpy_imm_fcz(0xff); // do this until all bytes in page have been erased
  if (!zero_flag) { goto InitByteLoop; }
  dex_fzn(); // go onto the next page
  if (!neg_flag) { goto InitPageLoop; } // do this until all pages of memory have been erased
  return;
  // -------------------------------------------------------------------------------------
}

void GetAreaMusic(void) {
  // GetAreaMusic:
  lda_abs_fzn(OperMode); // if in title screen mode, leave
  if (zero_flag) { return; }
  lda_abs(AltEntranceControl); // check for specific alternate mode of entry
  cmp_imm_fcz(0x2); // if found, branch without checking starting position
  if (zero_flag) { goto ChkAreaType; } // from area object data header
  ldy_imm(0x5); // select music for pipe intro scene by default
  lda_abs(PlayerEntranceCtrl); // check value from level header for certain values
  cmp_imm_fcz(0x6);
  if (zero_flag) { goto StoreMusic; } // load music for pipe intro scene if header
  cmp_imm_fcz(0x7); // start position either value $06 or $07
  if (zero_flag) { goto StoreMusic; }
  
ChkAreaType:
  ldy_abs(AreaType); // load area type as offset for music bit
  lda_abs_fz(CloudTypeOverride);
  if (zero_flag) { goto StoreMusic; } // check for cloud type override
  ldy_imm(0x4); // select music for cloud type level if found
  
StoreMusic:
  lda_absy_fzn(MusicSelectData); // otherwise select appropriate music for level type
  ram[AreaMusicQueue] = a; // store in queue and leave
  // ExitGetM:
  return;
  // -------------------------------------------------------------------------------------
}

void Entrance_GameTimerSetup(void) {
  lda_abs(ScreenLeft_PageLoc); // set current page for area objects
  ram[Player_PageLoc] = a; // as page location for player
  lda_imm(0x28); // store value here
  ram[VerticalForceDown] = a; // for fractional movement downwards if necessary
  lda_imm(0x1); // set high byte of player position and
  ram[PlayerFacingDir] = a; // set facing direction so that player faces right
  ram[Player_Y_HighPos] = a;
  lda_imm(0x0); // set player state to on the ground by default
  ram[Player_State] = a;
  dec_abs(Player_CollisionBits); // initialize player's collision bits
  ldy_imm(0x0); // initialize halfway page
  ram[HalfwayPage] = y;
  lda_abs_fz(AreaType); // check area type
  // if water type, set swimming flag, otherwise do not set
  if (zero_flag) {
    iny();
  }
  // ChkStPos:
  ram[SwimmingFlag] = y;
  ldx_abs(PlayerEntranceCtrl); // get starting position loaded from header
  ldy_abs_fz(AltEntranceControl); // check alternate mode of entry flag for 0 or 1
  if (!zero_flag) {
    cpy_imm_fcz(0x1);
    if (!zero_flag) {
      ldx_absy(AltYPosOffset - 2); // if not 0 or 1, override $0710 with new offset in X
    }
  }
  // SetStPos:
  lda_absy(PlayerStarting_X_Pos); // load appropriate horizontal position
  ram[Player_X_Position] = a; // and vertical positions for the player, using
  lda_absx(PlayerStarting_Y_Pos); // AltEntranceControl as offset for horizontal and either $0710
  ram[Player_Y_Position] = a; // or value that overwrote $0710 as offset for vertical
  lda_absx_fzn(PlayerBGPriorityData);
  ram[Player_SprAttrib] = a; // set player sprite attributes using offset in X
  cpu_call_begin(0x9177); GetPlayerColors(); cpu_call_end(); // get appropriate player palette
  ldy_abs_fz(GameTimerSetting); // get timer control value from header
  // if set to zero, branch (do not use dummy byte for this)
  if (!zero_flag) {
    lda_abs_fz(FetchNewGameTimerFlag); // do we need to set the game timer? if not, use 
    // old game timer setting
    if (!zero_flag) {
      lda_absy(GameTimerData); // if game timer is set and game timer flag is also set,
      ram[GameTimerDisplay] = a; // use value of game timer control for first digit of game timer
      lda_imm(0x1);
      ram[GameTimerDisplay + 2] = a; // set last digit of game timer to 1
      lsr_acc_fc();
      ram[GameTimerDisplay + 1] = a; // set second digit of game timer
      ram[FetchNewGameTimerFlag] = a; // clear flag for game timer reset
      ram[StarInvincibleTimer] = a; // clear star mario timer
    }
  }
  // ChkOverR:
  ldy_abs_fz(JoypadOverride); // if controller bits not set, branch to skip this part
  if (!zero_flag) {
    lda_imm(0x3); // set player state to climbing
    ram[Player_State] = a;
    ldx_imm_fzn(0x0); // set offset for first slot, for block object
    cpu_call_begin(0x91a4); InitBlock_XY_Pos(); cpu_call_end();
    lda_imm(0xf0); // set vertical coordinate for block object
    ram[Block_Y_Position] = a;
    ldx_imm(0x5); // set offset in X for last enemy object buffer slot
    ldy_imm_fzn(0x0); // set offset in Y for object coordinates used earlier
    cpu_call_begin(0x91af); Setup_Vine(); cpu_call_end(); // do a sub to grow vine
  }
  // ChkSwimE:
  ldy_abs_fzn(AreaType); // if level not water-type,
  // skip this subroutine
  if (zero_flag) {
    cpu_call_begin(0x91b7); SetupBubble(); cpu_call_end(); // otherwise, execute sub to set up air bubbles
  }
  // SetPESub:
  lda_imm_fzn(0x7); // set to run player entrance subroutine
  ram[GameEngineSubroutine] = a; // on the next frame of game engine
  return;
  // -------------------------------------------------------------------------------------
  // page numbers are in order from -1 to -4
}

void PlayerLoseLife(void) {
  inc_abs(DisableScreenFlag); // disable screen and sprite 0 check
  lda_imm(0x0);
  ram[Sprite0HitDetectFlag] = a;
  lda_imm(Silence); // silence music
  ram[EventMusicQueue] = a;
  dec_abs_fn(NumberofLives); // take one life from player
  // if player still has lives, branch
  if (neg_flag) {
    lda_imm(0x0);
    ram[OperMode_Task] = a; // initialize mode task,
    lda_imm_fzn(GameOverModeValue); // switch to game over mode
    ram[OperMode] = a; // and leave
    return;
  }
  // StillInGame:
  lda_abs(WorldNumber); // multiply world number by 2 and use
  asl_acc(); // as offset
  tax();
  lda_abs(LevelNumber); // if in area -3 or -4, increment
  and_imm_fz(0x2); // offset by one byte, otherwise
  // leave offset alone
  if (!zero_flag) {
    inx();
  }
  // GetHalfway:
  ldy_absx(HalfwayPageNybbles); // get halfway page number with offset
  lda_abs(LevelNumber); // check area number's LSB
  lsr_acc_fc();
  tya(); // if in area -2 or -4, use lower nybble
  if (!carry_flag) {
    lsr_acc(); // move higher nybble to lower if area
    lsr_acc(); // number is -1 or -3
    lsr_acc();
    lsr_acc();
  }
  // MaskHPNyb:
  and_imm(0b00001111); // mask out all but lower nybble
  cmp_abs_fczn(ScreenLeft_PageLoc);
  // left side of screen must be at the halfway page,
  if (!zero_flag) {
    // otherwise player must start at the
    if (carry_flag) {
      lda_imm_fzn(0x0); // beginning of the level
    }
  }
  // SetHalfway:
  ram[HalfwayPage] = a; // store as halfway page for player
  cpu_call_begin(0x9214); TransposePlayers(); cpu_call_end(); // switch players around if 2-player game
  goto ContinueGame; // continue the game
  // -------------------------------------------------------------------------------------
  
ContinueGame:
  cpu_call_begin(0x9266); LoadAreaPointer(); cpu_call_end(); // update level pointer with
  lda_imm(0x1); // actual world and area numbers, then
  ram[PlayerSize] = a; // reset player's size, status, and
  inc_abs(FetchNewGameTimerFlag); // set game timer flag to reload
  lda_imm(0x0); // game timer from header
  ram[TimerControl] = a; // also set flag for timers to count again
  ram[PlayerStatus] = a;
  ram[GameEngineSubroutine] = a; // reset task for game core
  ram[OperMode_Task] = a; // set modes and leave
  lda_imm_fzn(0x1); // if in game over mode, switch back to
  ram[OperMode] = a; // game mode, because game is still on
  // GameIsOn:
  return;
}

void GameOverMode(void) {
  lda_abs_fzn(OperMode_Task);
  cpu_call_begin(0x921d);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x9224: SetupGameOver(); return;
    case 0x8567: ScreenRoutines(); return;
    case 0x9237: RunGameOver(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void SetupGameOver(void) {
  lda_imm(0x0); // reset screen routine task control for title screen, game,
  ram[ScreenRoutineTask] = a; // and game over modes
  ram[Sprite0HitDetectFlag] = a; // disable sprite 0 check
  lda_imm(GameOverMusic);
  ram[EventMusicQueue] = a; // put game over music in secondary queue
  inc_abs(DisableScreenFlag); // disable screen output
  inc_abs_fzn(OperMode_Task); // set secondary mode to 1
  return;
  // -------------------------------------------------------------------------------------
}

void RunGameOver(void) {
  lda_imm(0x0); // reenable screen
  ram[DisableScreenFlag] = a;
  lda_abs(SavedJoypad1Bits); // check controller for start pressed
  and_imm_fz(Start_Button);
  if (!zero_flag) {
    TerminateGame();
    return;
  }
  lda_abs_fzn(ScreenTimer); // if not pressed, wait for
  if (zero_flag) {
    TerminateGame(); // fallthrough
    return;
  }
}

void TerminateGame(void) {
  lda_imm_fzn(Silence); // silence music
  ram[EventMusicQueue] = a;
  cpu_call_begin(0x924e); TransposePlayers(); cpu_call_end(); // check if other player can keep
  // going, and do so if possible
  if (carry_flag) {
    lda_abs(WorldNumber); // otherwise put world number of current
    ram[ContinueWorld] = a; // player into secret continue function variable
    lda_imm(0x0);
    asl_acc_fczn(); // residual ASL instruction
    ram[OperMode_Task] = a; // reset all modes to title screen and
    ram[ScreenTimer] = a; // leave
    ram[OperMode] = a;
    return;
  }
  // ContinueGame:
  cpu_call_begin(0x9266); LoadAreaPointer(); cpu_call_end(); // update level pointer with
  lda_imm(0x1); // actual world and area numbers, then
  ram[PlayerSize] = a; // reset player's size, status, and
  inc_abs(FetchNewGameTimerFlag); // set game timer flag to reload
  lda_imm(0x0); // game timer from header
  ram[TimerControl] = a; // also set flag for timers to count again
  ram[PlayerStatus] = a;
  ram[GameEngineSubroutine] = a; // reset task for game core
  ram[OperMode_Task] = a; // set modes and leave
  lda_imm_fzn(0x1); // if in game over mode, switch back to
  ram[OperMode] = a; // game mode, because game is still on
  // GameIsOn:
  return;
}

void TransposePlayers(void) {
  // TransposePlayers:
  carry_flag = true; // set carry flag by default to end game
  lda_abs_fzn(NumberOfPlayers); // if only a 1 player game, leave
  if (zero_flag) { return; }
  lda_abs_fzn(OffScr_NumberofLives); // does offscreen player have any lives left?
  if (neg_flag) { return; } // branch if not
  lda_abs(CurrentPlayer); // invert bit to update
  eor_imm(0b00000001); // which player is on the screen
  ram[CurrentPlayer] = a;
  ldx_imm(0x6);
  
TransLoop:
  lda_absx(OnscreenPlayerInfo); // transpose the information
  pha(); // of the onscreen player
  lda_absx(OffscreenPlayerInfo); // with that of the offscreen player
  ram[OnscreenPlayerInfo + x] = a;
  pla();
  ram[OffscreenPlayerInfo + x] = a;
  dex_fzn();
  if (!neg_flag) { goto TransLoop; }
  carry_flag = false; // clear carry flag to get game going
  // ExTrans:
  return;
  // -------------------------------------------------------------------------------------
}

void DoNothing1(void) {
  lda_imm_fzn(0xff); // this is residual code, this value is
  ram[0x6c9] = a; // not used anywhere in the program
  DoNothing2(); // fallthrough
  return;
}

void DoNothing2(void) {
  return;
  // -------------------------------------------------------------------------------------
}

void AreaParserTaskHandler(void) {
  ldy_abs_fz(AreaParserTaskNum); // check number of tasks here
  // if already set, go ahead
  if (zero_flag) {
    ldy_imm(0x8);
    ram[AreaParserTaskNum] = y; // otherwise, set eight by default
  }
  // DoAPTasks:
  dey();
  tya_fzn();
  cpu_call_begin(0x92be); AreaParserTasks(); cpu_call_end();
  dec_abs_fzn(AreaParserTaskNum); // if all tasks not complete do not
  if (zero_flag) {
    cpu_call_begin(0x92c6); RenderAttributeTables(); cpu_call_end();
    // SkipATRender:
    return;
  }
}

void AreaParserTasks(void) {
  cpu_call_begin(0x92ca);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x92db: IncrementColumnPos(); return;
    case 0x88ae: RenderAreaGraphics(); return;
    case 0x93fc: AreaParserCore(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void IncrementColumnPos(void) {
  inc_abs(CurrentColumnPos); // increment column where we're at
  lda_abs(CurrentColumnPos);
  and_imm_fz(0b00001111); // mask out higher nybble
  if (zero_flag) {
    ram[CurrentColumnPos] = a; // if no bits left set, wrap back to zero (0-f)
    inc_abs(CurrentPageLoc); // and increment page number where we're at
  }
  // NoColWrap:
  inc_abs(BlockBufferColumnPos); // increment column offset where we're at
  lda_abs(BlockBufferColumnPos);
  and_imm_fzn(0b00011111); // mask out all but 5 LSB (0-1f)
  ram[BlockBufferColumnPos] = a; // and save
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used as counter, store for low nybble for background, ceiling byte for terrain
  // $01 - used to store floor byte for terrain
  // $07 - used to store terrain metatile
  // $06-$07 - used to store block buffer address
}

void AreaParserCore(void) {
  lda_abs_fzn(BackloadingFlag); // check to see if we are starting right of start
  // if not, go ahead and render background, foreground and terrain
  if (!zero_flag) {
    cpu_call_begin(0x9403); ProcessAreaData(); cpu_call_end(); // otherwise skip ahead and load level data
  }
  // RenderSceneryTerrain:
  ldx_imm(0xc);
  lda_imm(0x0);
  
ClrMTBuf:
  ram[MetatileBuffer + x] = a; // clear out metatile buffer
  dex_fn();
  if (!neg_flag) { goto ClrMTBuf; }
  ldy_abs_fz(BackgroundScenery); // do we need to render the background scenery?
  // if not, skip to check the foreground
  if (!zero_flag) {
    lda_abs(CurrentPageLoc); // otherwise check for every third page
    
ThirdP:
    cmp_imm_fn(0x3);
    // if less than three we're there
    if (!neg_flag) {
      carry_flag = true;
      sbc_imm_fn(0x3); // if 3 or more, subtract 3 and 
      if (!neg_flag) { goto ThirdP; } // do an unconditional branch
    }
    // RendBack:
    asl_acc(); // move results to higher nybble
    asl_acc();
    asl_acc();
    asl_acc_fc();
    adc_absy_fc(BSceneDataOffsets - 1); // add to it offset loaded from here
    adc_abs(CurrentColumnPos); // add to the result our current column position
    tax();
    lda_absx_fz(BackSceneryData); // load data from sum of offsets
    // if zero, no scenery for that part
    if (!zero_flag) {
      pha();
      and_imm(0xf); // save to stack and clear high nybble
      carry_flag = true;
      sbc_imm(0x1); // subtract one (because low nybble is $01-$0c)
      ram[0x0] = a; // save low nybble
      asl_acc_fc(); // multiply by three (shift to left and add result to old one)
      adc_zp(0x0); // note that since d7 was nulled, the carry flag is always clear
      tax(); // save as offset for background scenery metatile data
      pla(); // get high nybble from stack, move low
      lsr_acc();
      lsr_acc();
      lsr_acc();
      lsr_acc();
      tay(); // use as second offset (used to determine height)
      lda_imm(0x3); // use previously saved memory location for counter
      ram[0x0] = a;
      
SceLoop1:
      lda_absx(BackSceneryMetatiles); // load metatile data from offset of (lsb - 1) * 3
      ram[MetatileBuffer + y] = a; // store into buffer from offset of (msb / 16)
      inx();
      iny();
      cpy_imm_fz(0xb); // if at this location, leave loop
      if (!zero_flag) {
        dec_zp_fz(0x0); // decrement until counter expires, barring exception
        if (!zero_flag) { goto SceLoop1; }
      }
    }
  }
  // RendFore:
  ldx_abs_fz(ForegroundScenery); // check for foreground data needed or not
  // if not, skip this part
  if (!zero_flag) {
    ldy_absx(FSceneDataOffsets - 1); // load offset from location offset by header value, then
    ldx_imm(0x0); // reinit X
    
SceLoop2:
    lda_absy_fz(ForeSceneryData); // load data until counter expires
    // do not store if zero found
    if (!zero_flag) {
      ram[MetatileBuffer + x] = a;
    }
    // NoFore:
    iny();
    inx();
    cpx_imm_fz(0xd); // store up to end of metatile buffer
    if (!zero_flag) { goto SceLoop2; }
  }
  // RendTerr:
  ldy_abs_fz(AreaType); // check world type for water level
  // if not water level, skip this part
  if (zero_flag) {
    lda_abs(WorldNumber); // check world number, if not world number eight
    cmp_imm_fz(World8); // then skip this part
    if (zero_flag) {
      lda_imm(0x62); // if set as water level and world number eight,
      goto StoreMT; // use castle wall metatile as terrain type
    }
  }
  // TerMTile:
  lda_absy(TerrainMetatiles); // otherwise get appropriate metatile for area type
  ldy_abs_fz(CloudTypeOverride); // check for cloud type override
  // if not set, keep value otherwise
  if (!zero_flag) {
    lda_imm(0x88); // use cloud block terrain
  }
  
StoreMT:
  ram[0x7] = a; // store value here
  ldx_imm(0x0); // initialize X, use as metatile buffer offset
  lda_abs(TerrainControl); // use yet another value from the header
  asl_acc(); // multiply by 2 and use as yet another offset
  tay();
  
TerrLoop:
  lda_absy(TerrainRenderBits); // get one of the terrain rendering bit data
  ram[0x0] = a;
  iny(); // increment Y and use as offset next time around
  ram[0x1] = y;
  lda_abs_fz(CloudTypeOverride); // skip if value here is zero
  if (!zero_flag) {
    cpx_imm_fz(0x0); // otherwise, check if we're doing the ceiling byte
    if (!zero_flag) {
      lda_zp(0x0); // if not, mask out all but d3
      and_imm(0b00001000);
      ram[0x0] = a;
    }
  }
  // NoCloud2:
  ldy_imm(0x0); // start at beginning of bitmasks
  
TerrBChk:
  lda_absy(Bitmasks); // load bitmask, then perform AND on contents of first byte
  bit_zp_fz(0x0);
  // if not set, skip this part (do not write terrain to buffer)
  if (!zero_flag) {
    lda_zp(0x7);
    ram[MetatileBuffer + x] = a; // load terrain type metatile number and store into buffer here
  }
  // NextTBit:
  inx(); // continue until end of buffer
  cpx_imm_fczn(0xd);
  // if we're at the end, break out of this loop
  if (!zero_flag) {
    lda_abs(AreaType); // check world type for underground area
    cmp_imm_fz(0x2);
    // if not underground, skip this part
    if (zero_flag) {
      cpx_imm_fz(0xb);
      // if we're at the bottom of the screen, override
      if (zero_flag) {
        lda_imm(0x54); // old terrain type with ground level terrain type
        ram[0x7] = a;
      }
    }
    // EndUChk:
    iny(); // increment bitmasks offset in Y
    cpy_imm_fcz(0x8);
    if (!zero_flag) { goto TerrBChk; } // if not all bits checked, loop back    
    ldy_zp_fzn(0x1);
    if (!zero_flag) { goto TerrLoop; } // unconditional branch, use Y to load next byte
  }
  // RendBBuf:
  cpu_call_begin(0x94d5); ProcessAreaData(); cpu_call_end(); // do the area data loading routine now
  lda_abs_fzn(BlockBufferColumnPos);
  cpu_call_begin(0x94db); GetBlockBufferAddr(); cpu_call_end(); // get block buffer address from where we're at
  ldx_imm(0x0);
  ldy_imm(0x0); // init index regs and start at beginning of smaller buffer
  
ChkMTLow:
  ram[0x0] = y;
  lda_absx(MetatileBuffer); // load stored metatile number
  and_imm(0b11000000); // mask out all but 2 MSB
  asl_acc_fc();
  rol_acc_fc(); // make %xx000000 into %000000xx
  rol_acc();
  tay(); // use as offset in Y
  lda_absx(MetatileBuffer); // reload original unmasked value here
  cmp_absy_fc(BlockBuffLowBounds); // check for certain values depending on bits set
  // if equal or greater, branch
  if (!carry_flag) {
    lda_imm(0x0); // if less, init value before storing
  }
  // StrBlock:
  ldy_zp(0x0); // get offset for block buffer
  dynamic_ram_write(read_word(0x6) + y, a); // store value into block buffer
  tya();
  carry_flag = false; // add 16 (move down one row) to offset
  adc_imm(0x10);
  tay();
  inx(); // increment column value
  cpx_imm_fczn(0xd);
  if (!carry_flag) { goto ChkMTLow; } // continue until we pass last row, then leave
  return;
  // numbers lower than these with the same attribute bits
  // will not be stored in the block buffer
}

void ProcessAreaData(void) {
  
ProcessAreaData:
  ldx_imm(0x2); // start at the end of area object buffer
  
ProcADLoop:
  ram[ObjectOffset] = x;
  lda_imm(0x0); // reset flag
  ram[BehindAreaParserFlag] = a;
  ldy_abs(AreaDataOffset); // get offset of area data pointer
  lda_indy(AreaData); // get first byte of area object
  cmp_imm_fczn(0xfd); // if end-of-area, skip all this crap
  if (zero_flag) { goto RdyDecode; }
  lda_absx_fzn(AreaObjectLength); // check area object buffer flag
  if (!neg_flag) { goto RdyDecode; } // if buffer not negative, branch, otherwise
  iny();
  lda_indy(AreaData); // get second byte of area object
  asl_acc_fc(); // check for page select bit (d7), branch if not set
  if (!carry_flag) { goto Chk1Row13; }
  lda_abs_fz(AreaObjectPageSel); // check page select
  if (!zero_flag) { goto Chk1Row13; }
  inc_abs(AreaObjectPageSel); // if not already set, set it now
  inc_abs(AreaObjectPageLoc); // and increment page location
  
Chk1Row13:
  dey();
  lda_indy(AreaData); // reread first byte of level object
  and_imm(0xf); // mask out high nybble
  cmp_imm_fcz(0xd); // row 13?
  if (!zero_flag) { goto Chk1Row14; }
  iny(); // if so, reread second byte of level object
  lda_indy(AreaData);
  dey(); // decrement to get ready to read first byte
  and_imm_fz(0b01000000); // check for d6 set (if not, object is page control)
  if (!zero_flag) { goto CheckRear; }
  lda_abs_fz(AreaObjectPageSel); // if page select is set, do not reread
  if (!zero_flag) { goto CheckRear; }
  iny(); // if d6 not set, reread second byte
  lda_indy(AreaData);
  and_imm(0b00011111); // mask out all but 5 LSB and store in page control
  ram[AreaObjectPageLoc] = a;
  inc_abs_fzn(AreaObjectPageSel); // increment page select
  goto NextAObj;
  
Chk1Row14:
  cmp_imm_fcz(0xe); // row 14?
  if (!zero_flag) { goto CheckRear; }
  lda_abs_fzn(BackloadingFlag); // check flag for saved page number and branch if set
  if (!zero_flag) { goto RdyDecode; } // to render the object (otherwise bg might not look right)
  
CheckRear:
  lda_abs(AreaObjectPageLoc); // check to see if current page of level object is
  cmp_abs_fczn(CurrentPageLoc); // behind current page of renderer
  if (!carry_flag) { goto SetBehind; } // if so branch
  
RdyDecode:
  cpu_call_begin(0x9567); DecodeAreaData(); cpu_call_end(); // do sub and do not turn on flag
  goto ChkLength;
  
SetBehind:
  inc_abs_fzn(BehindAreaParserFlag); // turn on flag if object is behind renderer
  
NextAObj:
  cpu_call_begin(0x9570); IncAreaObjOffset(); cpu_call_end(); // increment buffer offset and move on
  
ChkLength:
  ldx_zp(ObjectOffset); // get buffer offset
  lda_absx_fn(AreaObjectLength); // check object length for anything stored here
  if (neg_flag) { goto ProcLoopb; } // if not, branch to handle loopback
  dec_absx(AreaObjectLength); // otherwise decrement length or get rid of it
  
ProcLoopb:
  dex_fn(); // decrement buffer offset
  if (!neg_flag) { goto ProcADLoop; } // and loopback unless exceeded buffer
  lda_abs_fz(BehindAreaParserFlag); // check for flag set if objects were behind renderer
  if (!zero_flag) { goto ProcessAreaData; } // branch if true to load more level data, otherwise
  lda_abs_fzn(BackloadingFlag); // check for flag set if starting right of page $00
  if (!zero_flag) { goto ProcessAreaData; } // branch if true to load more level data, otherwise leave
  // EndAParse:
  return;
}

void IncAreaObjOffset(void) {
  inc_abs(AreaDataOffset); // increment offset of level pointer
  inc_abs(AreaDataOffset);
  lda_imm_fzn(0x0); // reset page select
  ram[AreaObjectPageSel] = a;
  return;
}

void DecodeAreaData(void) {
  // DecodeAreaData:
  lda_absx_fn(AreaObjectLength); // check current buffer flag
  if (neg_flag) { goto Chk1stB; }
  ldy_absx(AreaObjOffsetBuffer); // if not, get offset from buffer
  
Chk1stB:
  ldx_imm(0x10); // load offset of 16 for special row 15
  lda_indy(AreaData); // get first byte of level object again
  cmp_imm_fczn(0xfd);
  if (zero_flag) { return; } // if end of level, leave this routine
  and_imm(0xf); // otherwise, mask out low nybble
  cmp_imm_fz(0xf); // row 15?
  if (zero_flag) { goto ChkRow14; } // if so, keep the offset of 16
  ldx_imm(0x8); // otherwise load offset of 8 for special row 12
  cmp_imm_fz(0xc); // row 12?
  if (zero_flag) { goto ChkRow14; } // if so, keep the offset value of 8
  ldx_imm(0x0); // otherwise nullify value by default
  
ChkRow14:
  ram[0x7] = x; // store whatever value we just loaded here
  ldx_zp(ObjectOffset); // get object offset again
  cmp_imm_fz(0xe); // row 14?
  if (!zero_flag) { goto ChkRow13; }
  lda_imm(0x0); // if so, load offset with $00
  ram[0x7] = a;
  lda_imm(0x2e); // and load A with another value
  goto NormObj; // unconditional branch
  
ChkRow13:
  cmp_imm_fcz(0xd); // row 13?
  if (!zero_flag) { goto ChkSRows; }
  lda_imm(0x22); // if so, load offset with 34
  ram[0x7] = a;
  iny(); // get next byte
  lda_indy(AreaData);
  and_imm_fzn(0b01000000); // mask out all but d6 (page control obj bit)
  if (zero_flag) { return; } // if d6 clear, branch to leave (we handled this earlier)
  lda_indy(AreaData); // otherwise, get byte again
  and_imm(0b01111111); // mask out d7
  cmp_imm_fz(0x4b); // check for loop command in low nybble
  if (!zero_flag) { goto Mask2MSB; } // (plus d6 set for object other than page control)
  inc_abs(LoopCommand); // if loop command, set loop command flag
  
Mask2MSB:
  and_imm(0b00111111); // mask out d7 and d6
  goto NormObj; // and jump
  
ChkSRows:
  cmp_imm_fc(0xc); // row 12-15?
  if (carry_flag) { goto SpecObj; }
  iny(); // if not, get second byte of level object
  lda_indy(AreaData);
  and_imm_fz(0b01110000); // mask out all but d6-d4
  if (!zero_flag) { goto LrgObj; } // if any bits set, branch to handle large object
  lda_imm(0x16);
  ram[0x7] = a; // otherwise set offset of 24 for small object
  lda_indy(AreaData); // reload second byte of level object
  and_imm(0b00001111); // mask out higher nybble and jump
  goto NormObj;
  
LrgObj:
  ram[0x0] = a; // store value here (branch for large objects)
  cmp_imm_fz(0x70); // check for vertical pipe object
  if (!zero_flag) { goto NotWPipe; }
  lda_indy(AreaData); // if not, reload second byte
  and_imm_fz(0b00001000); // mask out all but d3 (usage control bit)
  if (zero_flag) { goto NotWPipe; } // if d3 clear, branch to get original value
  lda_imm(0x0); // otherwise, nullify value for warp pipe
  ram[0x0] = a;
  
NotWPipe:
  lda_zp(0x0); // get value and jump ahead
  goto MoveAOId;
  
SpecObj:
  iny(); // branch here for rows 12-15
  lda_indy(AreaData);
  and_imm(0b01110000); // get next byte and mask out all but d6-d4
  
MoveAOId:
  lsr_acc(); // move d6-d4 to lower nybble
  lsr_acc();
  lsr_acc();
  lsr_acc();
  
NormObj:
  ram[0x0] = a; // store value here (branch for small objects and rows 13 and 14)
  lda_absx_fn(AreaObjectLength); // is there something stored here already?
  if (!neg_flag) { goto RunAObj; } // if so, branch to do its particular sub
  lda_abs(AreaObjectPageLoc); // otherwise check to see if the object we've loaded is on the
  cmp_abs_fcz(CurrentPageLoc); // same page as the renderer, and if so, branch
  if (zero_flag) { goto InitRear; }
  ldy_abs(AreaDataOffset); // if not, get old offset of level pointer
  lda_indy(AreaData); // and reload first byte
  and_imm(0b00001111);
  cmp_imm_fczn(0xe); // row 14?
  if (!zero_flag) { return; }
  lda_abs_fzn(BackloadingFlag); // if so, check backloading flag
  if (!zero_flag) { goto StrAObj; } // if set, branch to render object, else leave
  // LeavePar:
  return;
  
InitRear:
  lda_abs_fz(BackloadingFlag); // check backloading flag to see if it's been initialized
  if (zero_flag) { goto BackColC; } // branch to column-wise check
  lda_imm_fzn(0x0); // if not, initialize both backloading and 
  ram[BackloadingFlag] = a; // behind-renderer flags and leave
  ram[BehindAreaParserFlag] = a;
  ram[ObjectOffset] = a;
  LoopCmdE(); // fallthrough
  return;
  
BackColC:
  ldy_abs(AreaDataOffset); // get first byte again
  lda_indy(AreaData);
  and_imm(0b11110000); // mask out low nybble and move high to low
  lsr_acc();
  lsr_acc();
  lsr_acc();
  lsr_acc();
  cmp_abs_fczn(CurrentColumnPos); // is this where we're at?
  if (!zero_flag) { return; } // if not, branch to leave
  
StrAObj:
  lda_abs_fzn(AreaDataOffset); // if so, load area obj offset and store in buffer
  ram[AreaObjOffsetBuffer + x] = a;
  cpu_call_begin(0x965e); IncAreaObjOffset(); cpu_call_end(); // do sub to increment to next object data
  
RunAObj:
  lda_zp(0x0); // get stored value and add offset to it
  carry_flag = false; // then use the jump engine with current contents of A
  adc_zp_fczn(0x7);
  cpu_call_begin(0x9666);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x98e5: VerticalPipe(); return;
    case 0x9740: AreaStyleObject(); return;
    case 0x9a2e: RowOfBricks(); return;
    case 0x9a3e: RowOfSolidBlocks(); return;
    case 0x99f2: RowOfCoins(); return;
    case 0x9a50: ColumnOfBricks(); return;
    case 0x9a59: ColumnOfSolidBlocks(); return;
    case 0x9b41: Hole_Empty(); return;
    case 0x97ba: PulleyRopeObject(); return;
    case 0x9979: Bridge_High(); return;
    case 0x997c: Bridge_Middle(); return;
    case 0x997f: Bridge_Low(); return;
    case 0x9957: Hole_Water(); return;
    case 0x9968: QuestionBlockRow_High(); return;
    case 0x996b: QuestionBlockRow_Low(); return;
    case 0x99d0: EndlessRope(); return;
    case 0x99d7: BalancePlatRope(); return;
    case 0x9806: CastleObject(); return;
    case 0x9ab7: StaircaseObject(); return;
    case 0x98ab: ExitPipe(); return;
    case 0x9994: FlagBalls_Residual(); return;
    case 0x9b0e: QuestionBlock(); return;
    case 0x9b01: Hidden1UpBlock(); return;
    case 0x9b19: BrickWithItem(); return;
    case 0x9b14: BrickWithCoins(); return;
    case 0x986f: WaterPipe(); return;
    case 0x9a19: EmptyBlock(); return;
    case 0x9ad3: Jumpspring(); return;
    case 0x9882: IntroPipe(); return;
    case 0x999e: FlagpoleObject(); return;
    case 0x9a09: AxeObj(); return;
    case 0x9a0e: ChainObj(); return;
    case 0x9a01: CastleBridgeObj(); return;
    case 0x96f2: ScrollLockObject_Warp(); return;
    case 0x970d: ScrollLockObject(); return;
    case 0x972b: AreaFrenzy(); return;
    case 0x9645: LoopCmdE(); return;
    case 0x96c5: AlterAreaAttributes(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void LoopCmdE(void) {
  return;
}

void AlterAreaAttributes(void) {
  ldy_absx(AreaObjOffsetBuffer); // load offset for level object data saved in buffer
  iny(); // load second byte
  lda_indy(AreaData);
  pha(); // save in stack for now
  and_imm_fz(0b01000000);
  // branch if d6 is set
  if (zero_flag) {
    pla();
    pha(); // pull and push offset to copy to A
    and_imm(0b00001111); // mask out high nybble and store as
    ram[TerrainControl] = a; // new terrain height type bits
    pla();
    and_imm(0b00110000); // pull and mask out all but d5 and d4
    lsr_acc(); // move bits to lower nybble and store
    lsr_acc(); // as new background scenery bits
    lsr_acc();
    lsr_acc_fczn();
    ram[BackgroundScenery] = a; // then leave
    return;
  }
  // Alter2:
  pla();
  and_imm(0b00000111); // mask out all but 3 LSB
  cmp_imm_fczn(0x4); // if four or greater, set color control bits
  // and nullify foreground scenery bits
  if (carry_flag) {
    ram[BackgroundColorCtrl] = a;
    lda_imm_fzn(0x0);
  }
  // SetFore:
  ram[ForegroundScenery] = a; // otherwise set new foreground scenery bits
  return;
  // --------------------------------
}

void ScrollLockObject_Warp(void) {
  ldx_imm(0x4); // load value of 4 for game text routine as default
  lda_abs_fz(WorldNumber); // warp zone (4-3-2), then check world number
  if (!zero_flag) {
    inx(); // if world number > 1, increment for next warp zone (5)
    ldy_abs(AreaType); // check area type
    dey_fz();
    // if ground area type, increment for last warp zone
    if (zero_flag) {
      inx(); // (8-7-6) and move on
    }
  }
  // WarpNum:
  txa_fzn();
  ram[WarpZoneControl] = a; // store number here to be used by warp zone routine
  cpu_call_begin(0x9707); WriteGameText(); cpu_call_end(); // print text and warp zone numbers
  lda_imm_fzn(PiranhaPlant);
  cpu_call_begin(0x970c); KillEnemies(); cpu_call_end(); // load identifier for piranha plants and do sub
  ScrollLockObject(); // fallthrough
  return;
}

void ScrollLockObject(void) {
  lda_abs(ScrollLock); // invert scroll lock to turn it on
  eor_imm_fzn(0b00000001);
  ram[ScrollLock] = a;
  return;
  // --------------------------------
  // $00 - used to store enemy identifier in KillEnemies
}

void KillEnemies(void) {
  ram[0x0] = a; // store identifier here
  lda_imm(0x0);
  ldx_imm(0x4); // check for identifier in enemy object buffer
  
KillELoop:
  ldy_zpx(Enemy_ID);
  cpy_zp_fcz(0x0); // if not found, branch
  if (zero_flag) {
    ram[Enemy_Flag + x] = a; // if found, deactivate enemy object flag
  }
  // NoKillE:
  dex_fzn(); // do this until all slots are checked
  if (!neg_flag) { goto KillELoop; }
  return;
  // --------------------------------
}

void AreaFrenzy(void) {
  ldx_zp(0x0); // use area object identifier bit as offset
  lda_absx(FrenzyIDData - 8); // note that it starts at 8, thus weird address here
  ldy_imm(0x5);
  
FreCompLoop:
  dey_fzn(); // check regular slots of enemy object buffer
  // if all slots checked and enemy object not found, branch to store
  if (!neg_flag) {
    cmp_zpy_fcz(Enemy_ID); // check for enemy object in buffer versus frenzy object
    if (!zero_flag) { goto FreCompLoop; }
    lda_imm_fzn(0x0); // if enemy object already present, nullify queue and leave
  }
  // ExitAFrenzy:
  ram[EnemyFrenzyQueue] = a; // store enemy into frenzy queue
  return;
  // --------------------------------
  // $06 - used by MushroomLedge to store length
}

void AreaStyleObject(void) {
  lda_abs_fzn(AreaStyle); // load level object style and jump to the right sub
  cpu_call_begin(0x9745);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x974c: TreeLedge(); return;
    case 0x9778: MushroomLedge(); return;
    case 0x9a69: BulletBillCannon(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void TreeLedge(void) {
  cpu_call_begin(0x974e); GetLrgObjAttrib(); cpu_call_end(); // get row and length of green ledge
  lda_absx_fzn(AreaObjectLength); // check length counter for expiration
  if (!zero_flag) {
    if (neg_flag) {
      tya();
      ram[AreaObjectLength + x] = a; // store lower nybble into buffer flag as length of ledge
      lda_abs(CurrentPageLoc);
      ora_abs_fz(CurrentColumnPos); // are we at the start of the level?
      if (!zero_flag) {
        lda_imm(0x16); // render start of tree ledge
        goto NoUnder;
      }
    }
    // MidTreeL:
    ldx_zp(0x7);
    lda_imm(0x17); // render middle of tree ledge
    ram[MetatileBuffer + x] = a; // note that this is also used if ledge position is
    lda_imm(0x4c); // at the start of level for continuous effect
    goto AllUnder; // now render the part underneath
  }
  // EndTreeL:
  lda_imm(0x18); // render end of tree ledge
  goto NoUnder;
  
AllUnder:
  inx();
  ldy_imm(0xf); // set $0f to render all way down
  RenderUnderPart(); return; // now render the stem of mushroom
  
NoUnder:
  ldx_zp(0x7); // load row of ledge
  ldy_imm(0x0); // set 0 for no bottom on this part
  RenderUnderPart(); return;
  // --------------------------------
  // tiles used by pulleys and rope object
}

void MushroomLedge(void) {
  // MushroomLedge:
  cpu_call_begin(0x977a); ChkLrgObjLength(); cpu_call_end(); // get shroom dimensions
  ram[0x6] = y; // store length here for now
  if (!carry_flag) { goto EndMushL; }
  lda_absx(AreaObjectLength); // divide length by 2 and store elsewhere
  lsr_acc();
  ram[MushroomLedgeHalfLen + x] = a;
  lda_imm(0x19); // render start of mushroom
  goto NoUnder;
  
EndMushL:
  lda_imm(0x1b); // if at the end, render end of mushroom
  ldy_absx_fz(AreaObjectLength);
  if (zero_flag) { goto NoUnder; }
  lda_absx(MushroomLedgeHalfLen); // get divided length and store where length
  ram[0x6] = a; // was stored originally
  ldx_zp(0x7);
  lda_imm(0x1a);
  ram[MetatileBuffer + x] = a; // render middle of mushroom
  cpy_zp_fczn(0x6); // are we smack dab in the center?
  if (!zero_flag) { return; } // if not, branch to leave
  inx();
  lda_imm(0x4f);
  ram[MetatileBuffer + x] = a; // render stem top of mushroom underneath the middle
  lda_imm(0x50);
  // AllUnder:
  inx();
  ldy_imm(0xf); // set $0f to render all way down
  RenderUnderPart(); return; // now render the stem of mushroom
  
NoUnder:
  ldx_zp(0x7); // load row of ledge
  ldy_imm(0x0); // set 0 for no bottom on this part
  RenderUnderPart(); return;
  // --------------------------------
  // tiles used by pulleys and rope object
}

void PulleyRopeObject(void) {
  cpu_call_begin(0x97bc); ChkLrgObjLength(); cpu_call_end(); // get length of pulley/rope object
  ldy_imm(0x0); // initialize metatile offset
  // if starting, render left pulley
  if (!carry_flag) {
    iny();
    lda_absx_fz(AreaObjectLength); // if not at the end, render rope
    if (zero_flag) {
      iny(); // otherwise render right pulley
    }
  }
  // RenderPul:
  lda_absy_fzn(PulleyRopeMetatiles);
  ram[MetatileBuffer] = a; // render at the top of the screen
  // MushLExit:
  return; // and leave
  // --------------------------------
  // $06 - used to store upper limit of rows for CastleObject
}

void CastleObject(void) {
  // CastleObject:
  cpu_call_begin(0x9808); GetLrgObjAttrib(); cpu_call_end(); // save lower nybble as starting row
  ram[0x7] = y; // if starting row is above $0a, game will crash!!!
  ldy_imm_fzn(0x4);
  cpu_call_begin(0x980f); ChkLrgObjFixedLength(); cpu_call_end(); // load length of castle if not already loaded
  txa();
  pha(); // save obj buffer offset to stack
  ldy_absx(AreaObjectLength); // use current length as offset for castle data
  ldx_zp(0x7); // begin at starting row
  lda_imm(0xb);
  ram[0x6] = a; // load upper limit of number of rows to print
  
CRendLoop:
  lda_absy(CastleMetatiles); // load current byte using offset
  ram[MetatileBuffer + x] = a;
  inx(); // store in buffer and increment buffer offset
  lda_zp_fz(0x6);
  if (zero_flag) { goto ChkCFloor; } // have we reached upper limit yet?
  iny(); // if not, increment column-wise
  iny(); // to byte in next row
  iny();
  iny();
  iny();
  dec_zp(0x6); // move closer to upper limit
  
ChkCFloor:
  cpx_imm_fcz(0xb); // have we reached the row just before floor?
  if (!zero_flag) { goto CRendLoop; } // if not, go back and do another row
  pla();
  tax(); // get obj buffer offset from before
  lda_abs_fzn(CurrentPageLoc);
  if (zero_flag) { return; } // if we're at page 0, we do not need to do anything else
  lda_absx(AreaObjectLength); // check length
  cmp_imm_fcz(0x1); // if length almost about to expire, put brick at floor
  if (zero_flag) { goto PlayerStop; }
  ldy_zp_fz(0x7); // check starting row for tall castle ($00)
  if (!zero_flag) { goto NotTall; }
  cmp_imm_fcz(0x3); // if found, then check to see if we're at the second column
  if (zero_flag) { goto PlayerStop; }
  
NotTall:
  cmp_imm_fczn(0x2); // if not tall castle, check to see if we're at the third column
  if (!zero_flag) { return; } // if we aren't and the castle is tall, don't create flag yet
  cpu_call_begin(0x984d); GetAreaObjXPosition(); cpu_call_end(); // otherwise, obtain and save horizontal pixel coordinate
  pha();
  cpu_call_begin(0x9851); FindEmptyEnemySlot(); cpu_call_end(); // find an empty place on the enemy object buffer
  pla();
  ram[Enemy_X_Position + x] = a; // then write horizontal coordinate for star flag
  lda_abs(CurrentPageLoc);
  ram[Enemy_PageLoc + x] = a; // set page location for star flag
  lda_imm(0x1);
  ram[Enemy_Y_HighPos + x] = a; // set vertical high byte
  ram[Enemy_Flag + x] = a; // set flag for buffer
  lda_imm(0x90);
  ram[Enemy_Y_Position + x] = a; // set vertical coordinate
  lda_imm_fzn(StarFlagObject); // set star flag value in buffer itself
  ram[Enemy_ID + x] = a;
  return;
  
PlayerStop:
  ldy_imm_fzn(0x52); // put brick at floor to stop player at end of level
  ram[MetatileBuffer + 10] = y; // this is only done if we're on the second column
  // ExitCastle:
  return;
  // --------------------------------
}

void WaterPipe(void) {
  cpu_call_begin(0x9871); GetLrgObjAttrib(); cpu_call_end(); // get row and lower nybble
  ldy_absx(AreaObjectLength); // get length (residual code, water pipe is 1 col thick)
  ldx_zp(0x7); // get row
  lda_imm(0x6b);
  ram[MetatileBuffer + x] = a; // draw something here and below it
  lda_imm_fzn(0x6c);
  ram[MetatileBuffer + 1 + x] = a;
  return;
  // --------------------------------
  // $05 - used to store length of vertical shaft in RenderSidewaysPipe
  // $06 - used to store leftover horizontal length in RenderSidewaysPipe
  //  and vertical length in VerticalPipe and GetPipeHeight
}

void IntroPipe(void) {
  ldy_imm_fzn(0x3); // check if length set, if not set, set it
  cpu_call_begin(0x9886); ChkLrgObjFixedLength(); cpu_call_end();
  ldy_imm_fzn(0xa); // set fixed value and render the sideways part
  cpu_call_begin(0x988b); RenderSidewaysPipe(); cpu_call_end();
  if (!carry_flag) {
    ldx_imm(0x6); // blank everything above the vertical pipe part
    
VPipeSectLoop:
    lda_imm(0x0); // all the way to the top of the screen
    ram[MetatileBuffer + x] = a; // because otherwise it will look like exit pipe
    dex_fn();
    if (!neg_flag) { goto VPipeSectLoop; }
    lda_absy_fzn(VerticalPipeData); // draw the end of the vertical pipe part
    ram[MetatileBuffer + 7] = a;
    // NoBlankP:
    return;
  }
}

void ExitPipe(void) {
  ldy_imm_fzn(0x3); // check if length set, if not set, set it
  cpu_call_begin(0x98af); ChkLrgObjFixedLength(); cpu_call_end();
  cpu_call_begin(0x98b2); GetLrgObjAttrib(); cpu_call_end(); // get vertical length, then plow on through RenderSidewaysPipe
  RenderSidewaysPipe(); // fallthrough
  return;
}

void RenderSidewaysPipe(void) {
  dey(); // decrement twice to make room for shaft at bottom
  dey(); // and store here for now as vertical length
  ram[0x5] = y;
  ldy_absx(AreaObjectLength); // get length left over and store here
  ram[0x6] = y;
  ldx_zp(0x5); // get vertical length plus one, use as buffer offset
  inx();
  lda_absy(SidePipeShaftData); // check for value $00 based on horizontal offset
  cmp_imm_fcz(0x0);
  // if found, do not draw the vertical pipe shaft
  if (!zero_flag) {
    ldx_imm(0x0);
    ldy_zp_fzn(0x5); // init buffer offset and get vertical length
    cpu_call_begin(0x98cc); RenderUnderPart(); cpu_call_end(); // and render vertical shaft using tile number in A
    carry_flag = false; // clear carry flag to be used by IntroPipe
  }
  // DrawSidePart:
  ldy_zp(0x6); // render side pipe part at the bottom
  lda_absy(SidePipeTopPart);
  ram[MetatileBuffer + x] = a; // note that the pipe parts are stored
  lda_absy_fzn(SidePipeBottomPart); // backwards horizontally
  ram[MetatileBuffer + 1 + x] = a;
  return;
}

void VerticalPipe(void) {
  cpu_call_begin(0x98e7); GetPipeHeight(); cpu_call_end();
  lda_zp_fz(0x0); // check to see if value was nullified earlier
  // (if d3, the usage control bit of second byte, was set)
  if (!zero_flag) {
    iny();
    iny();
    iny();
    iny(); // add four if usage control bit was not set
  }
  // WarpPipe:
  tya(); // save value in stack
  pha();
  lda_abs(AreaNumber);
  ora_abs_fz(WorldNumber); // if at world 1-1, do not add piranha plant ever
  if (!zero_flag) {
    ldy_absx_fzn(AreaObjectLength); // if on second column of pipe, branch
    // (because we only need to do this once)
    if (!zero_flag) {
      cpu_call_begin(0x9901); FindEmptyEnemySlot(); cpu_call_end(); // check for an empty moving data buffer space
      // if not found, too many enemies, thus skip
      if (!carry_flag) {
        cpu_call_begin(0x9906); GetAreaObjXPosition(); cpu_call_end(); // get horizontal pixel coordinate
        carry_flag = false;
        adc_imm_fc(0x8); // add eight to put the piranha plant in the center
        ram[Enemy_X_Position + x] = a; // store as enemy's horizontal coordinate
        lda_abs(CurrentPageLoc); // add carry to current page number
        adc_imm_fc(0x0);
        ram[Enemy_PageLoc + x] = a; // store as enemy's page coordinate
        lda_imm_fzn(0x1);
        ram[Enemy_Y_HighPos + x] = a;
        ram[Enemy_Flag + x] = a; // activate enemy flag
        cpu_call_begin(0x991b); GetAreaObjYPosition(); cpu_call_end(); // get piranha plant's vertical coordinate and store here
        ram[Enemy_Y_Position + x] = a;
        lda_imm_fzn(PiranhaPlant); // write piranha plant's value into buffer
        ram[Enemy_ID + x] = a;
        cpu_call_begin(0x9924); InitPiranhaPlant(); cpu_call_end();
      }
    }
  }
  // DrawPipe:
  pla(); // get value saved earlier and use as Y
  tay();
  ldx_zp(0x7); // get buffer offset
  lda_absy(VerticalPipeData); // draw the appropriate pipe with the Y we loaded earlier
  ram[MetatileBuffer + x] = a; // render the top of the pipe
  inx();
  lda_absy(VerticalPipeData + 2); // render the rest of the pipe
  ldy_zp(0x6); // subtract one from length and render the part underneath
  dey();
  RenderUnderPart(); return;
}

void GetPipeHeight(void) {
  ldy_imm_fzn(0x1); // check for length loaded, if not, load
  cpu_call_begin(0x993d); ChkLrgObjFixedLength(); cpu_call_end(); // pipe length of 2 (horizontal)
  cpu_call_begin(0x9940); GetLrgObjAttrib(); cpu_call_end();
  tya(); // get saved lower nybble as height
  and_imm(0x7); // save only the three lower bits as
  ram[0x6] = a; // vertical length, then load Y with
  ldy_absx_fzn(AreaObjectLength); // length left over
  return;
}

void FindEmptyEnemySlot(void) {
  ldx_imm(0x0); // start at first enemy slot
  
EmptyChkLoop:
  carry_flag = false; // clear carry flag by default
  lda_zpx_fzn(Enemy_Flag); // check enemy buffer for nonzero
  if (!zero_flag) {
    inx();
    cpx_imm_fczn(0x5); // if nonzero, check next value
    if (!zero_flag) { goto EmptyChkLoop; }
    // ExitEmptyChk:
    return; // if all values nonzero, carry flag is set
    // --------------------------------
  }
}

void Hole_Water(void) {
  cpu_call_begin(0x9959); ChkLrgObjLength(); cpu_call_end(); // get low nybble and save as length
  lda_imm(0x86); // render waves
  ram[MetatileBuffer + 10] = a;
  ldx_imm(0xb);
  ldy_imm(0x1); // now render the water underneath
  lda_imm(0x87);
  RenderUnderPart(); return;
  // --------------------------------
}

void QuestionBlockRow_High(void) {
  lda_imm(0x3); // start on the fourth row
  // loc_39274:
  bit_abs_fzn(0x7a9);
  goto loc_39277; // BIT instruction opcode
  
loc_39277:
  pha(); // save whatever row to the stack for now
  cpu_call_begin(0x9970); ChkLrgObjLength(); cpu_call_end(); // get low nybble and save as length
  pla();
  tax(); // render question boxes with coins
  lda_imm_fzn(0xc0);
  ram[MetatileBuffer + x] = a;
  return;
  // --------------------------------
}

void QuestionBlockRow_Low(void) {
  lda_imm_fzn(0x7); // start on the eighth row
  // loc_39277:
  pha(); // save whatever row to the stack for now
  cpu_call_begin(0x9970); ChkLrgObjLength(); cpu_call_end(); // get low nybble and save as length
  pla();
  tax(); // render question boxes with coins
  lda_imm_fzn(0xc0);
  ram[MetatileBuffer + x] = a;
  return;
  // --------------------------------
}

void Bridge_High(void) {
  lda_imm(0x6); // start on the seventh row from top of screen
  // loc_39291:
  bit_abs(0x7a9);
  goto loc_39294; // BIT instruction opcode
  
loc_39294:
  bit_abs_fzn(0x9a9);
  goto loc_39297; // BIT instruction opcode
  
loc_39297:
  pha(); // save whatever row to the stack for now
  cpu_call_begin(0x9984); ChkLrgObjLength(); cpu_call_end(); // get low nybble and save as length
  pla();
  tax(); // render bridge railing
  lda_imm(0xb);
  ram[MetatileBuffer + x] = a;
  inx();
  ldy_imm(0x0); // now render the bridge itself
  lda_imm(0x63);
  RenderUnderPart(); return;
  // --------------------------------
}

void Bridge_Middle(void) {
  lda_imm(0x7); // start on the eighth row
  // loc_39294:
  bit_abs_fzn(0x9a9);
  goto loc_39297; // BIT instruction opcode
  
loc_39297:
  pha(); // save whatever row to the stack for now
  cpu_call_begin(0x9984); ChkLrgObjLength(); cpu_call_end(); // get low nybble and save as length
  pla();
  tax(); // render bridge railing
  lda_imm(0xb);
  ram[MetatileBuffer + x] = a;
  inx();
  ldy_imm(0x0); // now render the bridge itself
  lda_imm(0x63);
  RenderUnderPart(); return;
  // --------------------------------
}

void Bridge_Low(void) {
  lda_imm_fzn(0x9); // start on the tenth row
  // loc_39297:
  pha(); // save whatever row to the stack for now
  cpu_call_begin(0x9984); ChkLrgObjLength(); cpu_call_end(); // get low nybble and save as length
  pla();
  tax(); // render bridge railing
  lda_imm(0xb);
  ram[MetatileBuffer + x] = a;
  inx();
  ldy_imm(0x0); // now render the bridge itself
  lda_imm(0x63);
  RenderUnderPart(); return;
  // --------------------------------
}

void FlagBalls_Residual(void) {
  cpu_call_begin(0x9996); GetLrgObjAttrib(); cpu_call_end(); // get low nybble from object byte
  ldx_imm(0x2); // render flag balls on third row from top
  lda_imm(0x6d); // of screen downwards based on low nybble
  RenderUnderPart(); return;
  // --------------------------------
}

void FlagpoleObject(void) {
  lda_imm(0x24); // render flagpole ball on top
  ram[MetatileBuffer] = a;
  ldx_imm(0x1); // now render the flagpole shaft
  ldy_imm(0x8);
  lda_imm_fzn(0x25);
  cpu_call_begin(0x99ab); RenderUnderPart(); cpu_call_end();
  lda_imm_fzn(0x61); // render solid block at the bottom
  ram[MetatileBuffer + 10] = a;
  cpu_call_begin(0x99b3); GetAreaObjXPosition(); cpu_call_end();
  carry_flag = true; // get pixel coordinate of where the flagpole is,
  sbc_imm_fc(0x8); // subtract eight pixels and use as horizontal
  ram[Enemy_X_Position + 5] = a; // coordinate for the flag
  lda_abs(CurrentPageLoc);
  sbc_imm_fc(0x0); // subtract borrow from page location and use as
  ram[Enemy_PageLoc + 5] = a; // page location for the flag
  lda_imm(0x30);
  ram[Enemy_Y_Position + 5] = a; // set vertical coordinate for flag
  lda_imm(0xb0);
  ram[FlagpoleFNum_Y_Pos] = a; // set initial vertical coordinate for flagpole's floatey number
  lda_imm(FlagpoleFlagObject);
  ram[Enemy_ID + 5] = a; // set flag identifier, note that identifier and coordinates
  inc_zp_fzn(Enemy_Flag + 5); // use last space in enemy object buffer
  return;
  // --------------------------------
}

void EndlessRope(void) {
  ldx_imm(0x0); // render rope from the top to the bottom of screen
  ldy_imm(0xf);
  goto DrawRope;
  
DrawRope:
  lda_imm(0x40); // render the actual rope
  RenderUnderPart(); return;
  // --------------------------------
}

void BalancePlatRope(void) {
  txa(); // save object buffer offset for now
  pha();
  ldx_imm(0x1); // blank out all from second row to the bottom
  ldy_imm(0xf); // with blank used for balance platform rope
  lda_imm_fzn(0x44);
  cpu_call_begin(0x99e1); RenderUnderPart(); cpu_call_end();
  pla(); // get back object buffer offset
  tax_fzn();
  cpu_call_begin(0x99e6); GetLrgObjAttrib(); cpu_call_end(); // get vertical length from lower nybble
  ldx_imm(0x1);
  // DrawRope:
  lda_imm(0x40); // render the actual rope
  RenderUnderPart(); return;
  // --------------------------------
}

void RowOfCoins(void) {
  ldy_abs(AreaType); // get area type
  lda_absy_fzn(CoinMetatileData); // load appropriate coin metatile
  goto GetRow;
  // --------------------------------
  
GetRow:
  pha(); // store metatile here
  cpu_call_begin(0x9a47); ChkLrgObjLength(); cpu_call_end(); // get row number, load length
  // DrawRow:
  ldx_zp(0x7);
  ldy_imm(0x0); // set vertical height of 1
  pla();
  RenderUnderPart(); return; // render object
}

void CastleBridgeObj(void) {
  ldy_imm_fzn(0xc); // load length of 13 columns
  cpu_call_begin(0x9a05); ChkLrgObjFixedLength(); cpu_call_end();
  ChainObj(); return;
}

void AxeObj(void) {
  lda_imm(0x8); // load bowser's palette into sprite portion of palette
  ram[VRAM_Buffer_AddrCtrl] = a;
  ChainObj(); // fallthrough
  return;
}

void ChainObj(void) {
  ldy_zp(0x0); // get value loaded earlier from decoder
  ldx_absy(C_ObjectRow - 2); // get appropriate row and metatile for object
  lda_absy(C_ObjectMetatile - 2);
  goto ColObj;
  
ColObj:
  ldy_imm(0x0); // column length of 1
  RenderUnderPart(); return;
  // --------------------------------
}

void EmptyBlock(void) {
  cpu_call_begin(0x9a1b); GetLrgObjAttrib(); cpu_call_end(); // get row location
  ldx_zp(0x7);
  lda_imm(0xc4);
  // ColObj:
  ldy_imm(0x0); // column length of 1
  RenderUnderPart(); return;
  // --------------------------------
}

void RowOfBricks(void) {
  ldy_abs(AreaType); // load area type obtained from area offset pointer
  lda_abs_fz(CloudTypeOverride); // check for cloud type override
  if (!zero_flag) {
    ldy_imm(0x4); // if cloud type, override area type
  }
  // DrawBricks:
  lda_absy_fzn(BrickMetatiles); // get appropriate metatile
  goto GetRow; // and go render it
  
GetRow:
  pha(); // store metatile here
  cpu_call_begin(0x9a47); ChkLrgObjLength(); cpu_call_end(); // get row number, load length
  // DrawRow:
  ldx_zp(0x7);
  ldy_imm(0x0); // set vertical height of 1
  pla();
  RenderUnderPart(); return; // render object
}

void RowOfSolidBlocks(void) {
  ldy_abs(AreaType); // load area type obtained from area offset pointer
  lda_absy_fzn(SolidBlockMetatiles); // get metatile
  // GetRow:
  pha(); // store metatile here
  cpu_call_begin(0x9a47); ChkLrgObjLength(); cpu_call_end(); // get row number, load length
  // DrawRow:
  ldx_zp(0x7);
  ldy_imm(0x0); // set vertical height of 1
  pla();
  RenderUnderPart(); return; // render object
}

void ColumnOfBricks(void) {
  ldy_abs(AreaType); // load area type obtained from area offset
  lda_absy_fzn(BrickMetatiles); // get metatile (no cloud override as for row)
  goto GetRow2;
  
GetRow2:
  pha(); // save metatile to stack for now
  cpu_call_begin(0x9a62); GetLrgObjAttrib(); cpu_call_end(); // get length and row
  pla(); // restore metatile
  ldx_zp(0x7); // get starting row
  RenderUnderPart(); return; // now render the column
  // --------------------------------
}

void ColumnOfSolidBlocks(void) {
  ldy_abs(AreaType); // load area type obtained from area offset
  lda_absy_fzn(SolidBlockMetatiles); // get metatile
  // GetRow2:
  pha(); // save metatile to stack for now
  cpu_call_begin(0x9a62); GetLrgObjAttrib(); cpu_call_end(); // get length and row
  pla(); // restore metatile
  ldx_zp(0x7); // get starting row
  RenderUnderPart(); return; // now render the column
  // --------------------------------
}

void BulletBillCannon(void) {
  cpu_call_begin(0x9a6b); GetLrgObjAttrib(); cpu_call_end(); // get row and length of bullet bill cannon
  ldx_zp(0x7); // start at first row
  lda_imm(0x64); // render bullet bill cannon
  ram[MetatileBuffer + x] = a;
  inx();
  dey_fn(); // done yet?
  if (!neg_flag) {
    lda_imm(0x65); // if not, render middle part
    ram[MetatileBuffer + x] = a;
    inx();
    dey_fn(); // done yet?
    if (!neg_flag) {
      lda_imm_fzn(0x66); // if not, render bottom until length expires
      cpu_call_begin(0x9a84); RenderUnderPart(); cpu_call_end();
    }
  }
  // SetupCannon:
  ldx_abs_fzn(Cannon_Offset); // get offset for data used by cannons and whirlpools
  cpu_call_begin(0x9a8a); GetAreaObjYPosition(); cpu_call_end(); // get proper vertical coordinate for cannon
  ram[Cannon_Y_Position + x] = a; // and store it here
  lda_abs_fzn(CurrentPageLoc);
  ram[Cannon_PageLoc + x] = a; // store page number for cannon here
  cpu_call_begin(0x9a96); GetAreaObjXPosition(); cpu_call_end(); // get proper horizontal coordinate for cannon
  ram[Cannon_X_Position + x] = a; // and store it here
  inx();
  cpx_imm_fczn(0x6); // increment and check offset
  // if not yet reached sixth cannon, branch to save offset
  if (carry_flag) {
    ldx_imm_fzn(0x0); // otherwise initialize it
  }
  // StrCOffset:
  ram[Cannon_Offset] = x; // save new offset and leave
  return;
  // --------------------------------
}

void StaircaseObject(void) {
  cpu_call_begin(0x9ab9); ChkLrgObjLength(); cpu_call_end(); // check and load length
  // if length already loaded, skip init part
  if (carry_flag) {
    lda_imm(0x9); // start past the end for the bottom
    ram[StaircaseControl] = a; // of the staircase
  }
  // NextStair:
  dec_abs(StaircaseControl); // move onto next step (or first if starting)
  ldy_abs(StaircaseControl);
  ldx_absy(StaircaseRowData); // get starting row and height to render
  lda_absy(StaircaseHeightData);
  tay();
  lda_imm(0x61); // now render solid block staircase
  RenderUnderPart(); return;
  // --------------------------------
}

void Jumpspring(void) {
  cpu_call_begin(0x9ad5); GetLrgObjAttrib(); cpu_call_end();
  cpu_call_begin(0x9ad8); FindEmptyEnemySlot(); cpu_call_end(); // find empty space in enemy object buffer
  cpu_call_begin(0x9adb); GetAreaObjXPosition(); cpu_call_end(); // get horizontal coordinate for jumpspring
  ram[Enemy_X_Position + x] = a; // and store
  lda_abs_fzn(CurrentPageLoc); // store page location of jumpspring
  ram[Enemy_PageLoc + x] = a;
  cpu_call_begin(0x9ae5); GetAreaObjYPosition(); cpu_call_end(); // get vertical coordinate for jumpspring
  ram[Enemy_Y_Position + x] = a; // and store
  ram[Jumpspring_FixedYPos + x] = a; // store as permanent coordinate here
  lda_imm(JumpspringObject);
  ram[Enemy_ID + x] = a; // write jumpspring object to enemy object buffer
  ldy_imm(0x1);
  ram[Enemy_Y_HighPos + x] = y; // store vertical high byte
  inc_zpx(Enemy_Flag); // set flag for enemy object buffer
  ldx_zp(0x7);
  lda_imm(0x67); // draw metatiles in two rows where jumpspring is
  ram[MetatileBuffer + x] = a;
  lda_imm_fzn(0x68);
  ram[MetatileBuffer + 1 + x] = a;
  return;
  // --------------------------------
  // $07 - used to save ID of brick object
}

void Hidden1UpBlock(void) {
  lda_abs_fzn(Hidden1UpFlag); // if flag not set, do not render object
  if (!zero_flag) {
    lda_imm_fzn(0x0); // if set, init for the next one
    ram[Hidden1UpFlag] = a;
    BrickWithItem(); return; // jump to code shared with unbreakable bricks
  }
}

void QuestionBlock(void) {
  goto QuestionBlock;
  
DrawRow:
  ldx_zp(0x7);
  ldy_imm(0x0); // set vertical height of 1
  pla();
  RenderUnderPart(); return; // render object
  
QuestionBlock:
  cpu_call_begin(0x9b10); GetAreaObjectID(); cpu_call_end(); // get value from level decoder routine
  goto DrawQBlk; // go to render it
  
DrawQBlk:
  lda_absy_fzn(BrickQBlockMetatiles); // get appropriate metatile for brick (question block
  pha(); // if branched to here from question block routine)
  cpu_call_begin(0x9b32); GetLrgObjAttrib(); cpu_call_end(); // get row from location byte
  goto DrawRow; // now render the object
}

void BrickWithCoins(void) {
  lda_imm_fzn(0x0); // initialize multi-coin timer flag
  ram[BrickCoinTimerFlag] = a;
  BrickWithItem(); // fallthrough
  return;
}

void BrickWithItem(void) {
  goto BrickWithItem;
  
DrawRow:
  ldx_zp(0x7);
  ldy_imm(0x0); // set vertical height of 1
  pla();
  RenderUnderPart(); return; // render object
  
BrickWithItem:
  cpu_call_begin(0x9b1b); GetAreaObjectID(); cpu_call_end(); // save area object ID
  ram[0x7] = y;
  lda_imm(0x0); // load default adder for bricks with lines
  ldy_abs(AreaType); // check level type for ground level
  dey_fz();
  // if ground type, do not start with 5
  if (!zero_flag) {
    lda_imm(0x5); // otherwise use adder for bricks without lines
  }
  // BWithL:
  carry_flag = false; // add object ID to adder
  adc_zp_fc(0x7);
  tay(); // use as offset for metatile
  // DrawQBlk:
  lda_absy_fzn(BrickQBlockMetatiles); // get appropriate metatile for brick (question block
  pha(); // if branched to here from question block routine)
  cpu_call_begin(0x9b32); GetLrgObjAttrib(); cpu_call_end(); // get row from location byte
  goto DrawRow; // now render the object
}

void GetAreaObjectID(void) {
  lda_zp(0x0); // get value saved from area parser routine
  carry_flag = true;
  sbc_imm_fc(0x0); // possibly residual code
  tay_fzn(); // save to Y
  // ExitDecBlock:
  return;
  // --------------------------------
}

void Hole_Empty(void) {
  cpu_call_begin(0x9b43); ChkLrgObjLength(); cpu_call_end(); // get lower nybble and save as length
  // skip this part if length already loaded
  if (carry_flag) {
    lda_abs_fz(AreaType); // check for water type level
    // if not water type, skip this part
    if (zero_flag) {
      ldx_abs_fzn(Whirlpool_Offset); // get offset for data used by cannons and whirlpools
      cpu_call_begin(0x9b50); GetAreaObjXPosition(); cpu_call_end(); // get proper vertical coordinate of where we're at
      carry_flag = true;
      sbc_imm_fc(0x10); // subtract 16 pixels
      ram[Whirlpool_LeftExtent + x] = a; // store as left extent of whirlpool
      lda_abs(CurrentPageLoc); // get page location of where we're at
      sbc_imm(0x0); // subtract borrow
      ram[Whirlpool_PageLoc + x] = a; // save as page location of whirlpool
      iny();
      iny(); // increment length by 2
      tya();
      asl_acc(); // multiply by 16 to get size of whirlpool
      asl_acc(); // note that whirlpool will always be
      asl_acc(); // two blocks bigger than actual size of hole
      asl_acc(); // and extend one block beyond each edge
      ram[Whirlpool_Length + x] = a; // save size of whirlpool here
      inx();
      cpx_imm_fc(0x5); // increment and check offset
      // if not yet reached fifth whirlpool, branch to save offset
      if (carry_flag) {
        ldx_imm(0x0); // otherwise initialize it
      }
      // StrWOffset:
      ram[Whirlpool_Offset] = x; // save new offset here
    }
  }
  // NoWhirlP:
  ldx_abs(AreaType); // get appropriate metatile, then
  lda_absx(HoleMetatiles); // render the hole proper
  ldx_imm(0x8);
  ldy_imm(0xf); // start at ninth row and go to bottom, run RenderUnderPart
  // --------------------------------
  RenderUnderPart(); // fallthrough
  return;
}

void RenderUnderPart(void) {
  
RenderUnderPart:
  ram[AreaObjectHeight] = y; // store vertical length to render
  ldy_absx_fz(MetatileBuffer); // check current spot to see if there's something
  if (zero_flag) { goto DrawThisRow; } // we need to keep, if nothing, go ahead
  cpy_imm_fz(0x17);
  if (zero_flag) { goto WaitOneRow; } // if middle part (tree ledge), wait until next row
  cpy_imm_fz(0x1a);
  if (zero_flag) { goto WaitOneRow; } // if middle part (mushroom ledge), wait until next row
  cpy_imm_fz(0xc0);
  if (zero_flag) { goto DrawThisRow; } // if question block w/ coin, overwrite
  cpy_imm_fc(0xc0);
  if (carry_flag) { goto WaitOneRow; } // if any other metatile with palette 3, wait until next row
  cpy_imm_fz(0x54);
  if (!zero_flag) { goto DrawThisRow; } // if cracked rock terrain, overwrite
  cmp_imm_fz(0x50);
  if (zero_flag) { goto WaitOneRow; } // if stem top of mushroom, wait until next row
  
DrawThisRow:
  ram[MetatileBuffer + x] = a; // render contents of A from routine that called this
  
WaitOneRow:
  inx();
  cpx_imm_fczn(0xd); // stop rendering if we're at the bottom of the screen
  if (carry_flag) { return; }
  ldy_abs(AreaObjectHeight); // decrement, and stop rendering if there is no more length
  dey_fzn();
  if (!neg_flag) { goto RenderUnderPart; }
  // ExitUPartR:
  return;
  // --------------------------------
}

void ChkLrgObjLength(void) {
  cpu_call_begin(0x9bae); GetLrgObjAttrib(); cpu_call_end(); // get row location and size (length if branched to from here)
  ChkLrgObjFixedLength(); // fallthrough
  return;
}

void ChkLrgObjFixedLength(void) {
  lda_absx_fzn(AreaObjectLength); // check for set length counter
  carry_flag = false; // clear carry flag for not just starting
  if (neg_flag) {
    tya_fzn(); // save length into length counter
    ram[AreaObjectLength + x] = a;
    carry_flag = true; // set carry flag if just starting
    // LenSet:
    return;
  }
}

void GetLrgObjAttrib(void) {
  ldy_absx(AreaObjOffsetBuffer); // get offset saved from area obj decoding routine
  lda_indy(AreaData); // get first byte of level object
  and_imm(0b00001111);
  ram[0x7] = a; // save row location
  iny();
  lda_indy(AreaData); // get next byte, save lower nybble (length or height)
  and_imm(0b00001111); // as Y, then leave
  tay_fzn();
  return;
  // --------------------------------
}

void GetAreaObjXPosition(void) {
  lda_abs(CurrentColumnPos); // multiply current offset where we're at by 16
  asl_acc(); // to obtain horizontal pixel coordinate
  asl_acc();
  asl_acc();
  asl_acc_fczn();
  return;
  // --------------------------------
}

void GetAreaObjYPosition(void) {
  lda_zp(0x7); // multiply value by 16
  asl_acc();
  asl_acc(); // this will give us the proper vertical pixel coordinate
  asl_acc();
  asl_acc();
  carry_flag = false;
  adc_imm_fczn(32); // add 32 pixels for the status bar
  return;
  // -------------------------------------------------------------------------------------
  // $06-$07 - used to store block buffer address used as indirect
}

void GetBlockBufferAddr(void) {
  pha(); // take value of A, save
  lsr_acc(); // move high nybble to low
  lsr_acc();
  lsr_acc();
  lsr_acc();
  tay(); // use nybble as pointer to high byte
  lda_absy(BlockBufferAddr + 2); // of indirect here
  ram[0x7] = a;
  pla();
  and_imm(0b00001111); // pull from stack, mask out high nybble
  carry_flag = false;
  adc_absy_fczn(BlockBufferAddr); // add to low byte
  ram[0x6] = a; // store here and leave
  return;
  // -------------------------------------------------------------------------------------
  // unused space
  // -------------------------------------------------------------------------------------
}

void LoadAreaPointer(void) {
  cpu_call_begin(0x9c05); FindAreaPointer(); cpu_call_end(); // find it and store it here
  ram[AreaPointer] = a;
  GetAreaType(); // fallthrough
  return;
}

void GetAreaType(void) {
  and_imm(0b01100000); // mask out all but d6 and d5
  asl_acc_fc();
  rol_acc_fc();
  rol_acc_fc();
  rol_acc_fczn(); // make %0xx00000 into %000000xx
  ram[AreaType] = a; // save 2 MSB as area type
  return;
}

void FindAreaPointer(void) {
  ldy_abs(WorldNumber); // load offset from world variable
  lda_absy(WorldAddrOffsets);
  carry_flag = false; // add area number used to find data
  adc_abs_fc(AreaNumber);
  tay();
  lda_absy_fzn(AreaAddrOffsets); // from there we have our area pointer
  return;
}

void GetAreaDataAddrs(void) {
  lda_abs_fzn(AreaPointer); // use 2 MSB for Y
  cpu_call_begin(0x9c27); GetAreaType(); cpu_call_end();
  tay();
  lda_abs(AreaPointer); // mask out all but 5 LSB
  and_imm(0b00011111);
  ram[AreaAddrsLOffset] = a; // save as low offset
  lda_absy(EnemyAddrHOffsets); // load base value with 2 altered MSB,
  carry_flag = false; // then add base value to 5 LSB, result
  adc_abs(AreaAddrsLOffset); // becomes offset for level data
  tay();
  lda_absy(EnemyDataAddrLow); // use offset to load pointer
  ram[EnemyDataLow] = a;
  lda_absy(EnemyDataAddrHigh);
  ram[EnemyDataHigh] = a;
  ldy_abs(AreaType); // use area type as offset
  lda_absy(AreaDataHOffsets); // do the same thing but with different base value
  carry_flag = false;
  adc_abs(AreaAddrsLOffset);
  tay();
  lda_absy(AreaDataAddrLow); // use this offset to load another pointer
  ram[AreaDataLow] = a;
  lda_absy(AreaDataAddrHigh);
  ram[AreaDataHigh] = a;
  ldy_imm(0x0); // load first byte of header
  lda_indy(AreaData);
  pha(); // save it to the stack for now
  and_imm(0b00000111); // save 3 LSB for foreground scenery or bg color control
  cmp_imm_fc(0x4);
  if (carry_flag) {
    ram[BackgroundColorCtrl] = a; // if 4 or greater, save value here as bg color control
    lda_imm(0x0);
  }
  // StoreFore:
  ram[ForegroundScenery] = a; // if less, save value here as foreground scenery
  pla(); // pull byte from stack and push it back
  pha();
  and_imm(0b00111000); // save player entrance control bits
  lsr_acc(); // shift bits over to LSBs
  lsr_acc();
  lsr_acc();
  ram[PlayerEntranceCtrl] = a; // save value here as player entrance control
  pla(); // pull byte again but do not push it back
  and_imm(0b11000000); // save 2 MSB for game timer setting
  carry_flag = false;
  rol_acc_fc(); // rotate bits over to LSBs
  rol_acc_fc();
  rol_acc();
  ram[GameTimerSetting] = a; // save value here as game timer setting
  iny();
  lda_indy(AreaData); // load second byte of header
  pha(); // save to stack
  and_imm(0b00001111); // mask out all but lower nybble
  ram[TerrainControl] = a;
  pla(); // pull and push byte to copy it to A
  pha();
  and_imm(0b00110000); // save 2 MSB for background scenery type
  lsr_acc();
  lsr_acc(); // shift bits to LSBs
  lsr_acc();
  lsr_acc();
  ram[BackgroundScenery] = a; // save as background scenery
  pla();
  and_imm(0b11000000);
  carry_flag = false;
  rol_acc_fc(); // rotate bits over to LSBs
  rol_acc_fc();
  rol_acc();
  cmp_imm_fz(0b00000011); // if set to 3, store here
  // and nullify other value
  if (zero_flag) {
    ram[CloudTypeOverride] = a; // otherwise store value in other place
    lda_imm(0x0);
  }
  // StoreStyle:
  ram[AreaStyle] = a;
  lda_zp(AreaDataLow); // increment area data address by 2 bytes
  carry_flag = false;
  adc_imm_fc(0x2);
  ram[AreaDataLow] = a;
  lda_zp(AreaDataHigh);
  adc_imm_fczn(0x0);
  ram[AreaDataHigh] = a;
  return;
  // -------------------------------------------------------------------------------------
  // GAME LEVELS DATA
}

void GameMode(void) {
  lda_abs_fzn(OperMode_Task);
  cpu_call_begin(0xaee1);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x8fe4: InitializeArea(); return;
    case 0x8567: ScreenRoutines(); return;
    case 0x9071: SecondaryGameSetup(); return;
    case 0xaeea: GameCoreRoutine(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void GameCoreRoutine(void) {
  // GameCoreRoutine:
  ldx_abs(CurrentPlayer); // get which player is on the screen
  lda_absx_fzn(SavedJoypadBits); // use appropriate player's controller bits
  ram[SavedJoypadBits] = a; // as the master controller bits
  cpu_call_begin(0xaef5); GameRoutines(); cpu_call_end(); // execute one of many possible subs
  lda_abs(OperMode_Task); // check major task of operating mode
  cmp_imm_fczn(0x3); // if we are supposed to be here,
  if (carry_flag) { goto GameEngine; } // branch to the game engine itself
  return;
  
GameEngine:
  cpu_call_begin(0xaf00); ProcFireball_Bubble(); cpu_call_end(); // process fireballs and air bubbles
  ldx_imm_fzn(0x0);
  
ProcELoop:
  ram[ObjectOffset] = x; // put incremented offset in X as enemy object offset
  cpu_call_begin(0xaf07); EnemiesAndLoopsCore(); cpu_call_end(); // process enemy objects
  cpu_call_begin(0xaf0a); FloateyNumbersRoutine(); cpu_call_end(); // process floatey numbers
  inx();
  cpx_imm_fczn(0x6); // do these two subroutines until the whole buffer is done
  if (!zero_flag) { goto ProcELoop; }
  cpu_call_begin(0xaf12); GetPlayerOffscreenBits(); cpu_call_end(); // get offscreen bits for player object
  cpu_call_begin(0xaf15); RelativePlayerPosition(); cpu_call_end(); // get relative coordinates for player object
  cpu_call_begin(0xaf18); PlayerGfxHandler(); cpu_call_end(); // draw the player
  cpu_call_begin(0xaf1b); BlockObjMT_Updater(); cpu_call_end(); // replace block objects with metatiles if necessary
  ldx_imm_fzn(0x1);
  ram[ObjectOffset] = x; // set offset for second
  cpu_call_begin(0xaf22); BlockObjectsCore(); cpu_call_end(); // process second block object
  dex_fzn();
  ram[ObjectOffset] = x; // set offset for first
  cpu_call_begin(0xaf28); BlockObjectsCore(); cpu_call_end(); // process first block object
  cpu_call_begin(0xaf2b); MiscObjectsCore(); cpu_call_end(); // process misc objects (hammer, jumping coins)
  cpu_call_begin(0xaf2e); ProcessCannons(); cpu_call_end(); // process bullet bill cannons
  cpu_call_begin(0xaf31); ProcessWhirlpools(); cpu_call_end(); // process whirlpools
  cpu_call_begin(0xaf34); FlagpoleRoutine(); cpu_call_end(); // process the flagpole
  cpu_call_begin(0xaf37); RunGameTimer(); cpu_call_end(); // count down the game timer
  cpu_call_begin(0xaf3a); ColorRotation(); cpu_call_end(); // cycle one of the background colors
  lda_zp(Player_Y_HighPos);
  cmp_imm_fcn(0x2); // if player is below the screen, don't bother with the music
  if (!neg_flag) { goto NoChgMus; }
  lda_abs_fzn(StarInvincibleTimer); // if star mario invincibility timer at zero,
  if (zero_flag) { goto ClrPlrPal; } // skip this part
  cmp_imm_fcz(0x4);
  if (!zero_flag) { goto NoChgMus; } // if not yet at a certain point, continue
  lda_abs_fzn(IntervalTimerControl); // if interval timer not yet expired,
  if (!zero_flag) { goto NoChgMus; } // branch ahead, don't bother with the music
  cpu_call_begin(0xaf51); GetAreaMusic(); cpu_call_end(); // to re-attain appropriate level music
  
NoChgMus:
  ldy_abs(StarInvincibleTimer); // get invincibility timer
  lda_zp(FrameCounter); // get frame counter
  cpy_imm_fc(0x8); // if timer still above certain point,
  if (carry_flag) { goto CycleTwo; } // branch to cycle player's palette quickly
  lsr_acc(); // otherwise, divide by 8 to cycle every eighth frame
  lsr_acc();
  
CycleTwo:
  lsr_acc_fczn(); // if branched here, divide by 2 to cycle every other frame
  cpu_call_begin(0xaf60); CyclePlayerPalette(); cpu_call_end(); // do sub to cycle the palette (note: shares fire flower code)
  goto SaveAB; // then skip this sub to finish up the game engine
  
ClrPlrPal:
  cpu_call_begin(0xaf66); ResetPalStar(); cpu_call_end(); // do sub to clear player's palette bits in attributes
  
SaveAB:
  lda_zp(A_B_Buttons); // save current A and B button
  ram[PreviousA_B_Buttons] = a; // into temp variable to be used on next frame
  lda_imm(0x0);
  ram[Left_Right_Buttons] = a; // nullify left and right buttons temp variable
  UpdScrollVar(); // fallthrough
  return;
}

void UpdScrollVar(void) {
  // UpdScrollVar:
  lda_abs(VRAM_Buffer_AddrCtrl);
  cmp_imm_fczn(0x6); // if vram address controller set to 6 (one of two $0341s)
  if (zero_flag) { return; } // then branch to leave
  lda_abs_fzn(AreaParserTaskNum); // otherwise check number of tasks
  if (!zero_flag) { goto RunParser; }
  lda_abs(ScrollThirtyTwo); // get horizontal scroll in 0-31 or $00-$20 range
  cmp_imm_fczn(0x20); // check to see if exceeded $21
  if (neg_flag) { return; } // branch to leave if not
  lda_abs(ScrollThirtyTwo);
  sbc_imm_fc(0x20); // otherwise subtract $20 to set appropriately
  ram[ScrollThirtyTwo] = a; // and store
  lda_imm_fzn(0x0); // reset vram buffer offset used in conjunction with
  ram[VRAM_Buffer2_Offset] = a; // level graphics buffer at $0341-$035f
  
RunParser:
  cpu_call_begin(0xaf91); AreaParserTaskHandler(); cpu_call_end(); // update the name table with more level graphics
  // ExitEng:
  return; // and after all that, we're finally done!
  // -------------------------------------------------------------------------------------
}

void ScrollHandler(void) {
  // ScrollHandler:
  lda_abs(Player_X_Scroll); // load value saved here
  carry_flag = false;
  adc_abs_fc(Platform_X_Scroll); // add value used by left/right platforms
  ram[Player_X_Scroll] = a; // save as new value here to impose force on scroll
  lda_abs_fz(ScrollLock); // check scroll lock flag
  if (!zero_flag) { goto InitScrlAmt; } // skip a bunch of code here if set
  lda_abs(Player_Pos_ForScroll);
  cmp_imm_fc(0x50); // check player's horizontal screen position
  if (!carry_flag) { goto InitScrlAmt; } // if less than 80 pixels to the right, branch
  lda_abs_fz(SideCollisionTimer); // if timer related to player's side collision
  if (!zero_flag) { goto InitScrlAmt; } // not expired, branch
  ldy_abs(Player_X_Scroll); // get value and decrement by one
  dey_fn(); // if value originally set to zero or otherwise
  if (neg_flag) { goto InitScrlAmt; } // negative for left movement, branch
  iny();
  cpy_imm_fc(0x2); // if value $01, branch and do not decrement
  if (!carry_flag) { goto ChkNearMid; }
  dey(); // otherwise decrement by one
  
ChkNearMid:
  lda_abs(Player_Pos_ForScroll);
  cmp_imm_fc(0x70); // check player's horizontal screen position
  if (!carry_flag) { ScrollScreen(); return; } // if less than 112 pixels to the right, branch
  ldy_abs(Player_X_Scroll); // otherwise get original value undecremented
  ScrollScreen(); // fallthrough
  return;
  
InitScrlAmt:
  lda_imm(0x0);
  ram[ScrollAmount] = a; // initialize value here
  // ChkPOffscr:
  ldx_imm_fzn(0x0); // set X for player offset
  cpu_call_begin(0xb004); GetXOffscreenBits(); cpu_call_end(); // get horizontal offscreen bits for player
  ram[0x0] = a; // save them here
  ldy_imm(0x0); // load default offset (left side)
  asl_acc_fc(); // if d7 of offscreen bits are set,
  if (carry_flag) { goto KeepOnscr; } // branch with default offset
  iny(); // otherwise use different offset (right side)
  lda_zp(0x0);
  and_imm_fz(0b00100000); // check offscreen bits for d5 set
  if (zero_flag) { goto InitPlatScrl; } // if not set, branch ahead of this part
  
KeepOnscr:
  lda_absy(ScreenEdge_X_Pos); // get left or right side coordinate based on offset
  carry_flag = true;
  sbc_absy_fc(X_SubtracterData); // subtract amount based on offset
  ram[Player_X_Position] = a; // store as player position to prevent movement further
  lda_absy(ScreenEdge_PageLoc); // get left or right page location based on offset
  sbc_imm(0x0); // subtract borrow
  ram[Player_PageLoc] = a; // save as player's page location
  lda_zp(Left_Right_Buttons); // check saved controller bits
  cmp_absy_fcz(OffscrJoypadBitsData); // against bits based on offset
  if (zero_flag) { goto InitPlatScrl; } // if not equal, branch
  lda_imm(0x0);
  ram[Player_X_Speed] = a; // otherwise nullify horizontal speed of player
  
InitPlatScrl:
  lda_imm_fzn(0x0); // nullify platform force imposed on scroll
  ram[Platform_X_Scroll] = a;
  return;
}

void ScrollScreen(void) {
  // ScrollScreen:
  tya();
  ram[ScrollAmount] = a; // save value here
  carry_flag = false;
  adc_abs(ScrollThirtyTwo); // add to value already set here
  ram[ScrollThirtyTwo] = a; // save as new value here
  tya();
  carry_flag = false;
  adc_abs_fc(ScreenLeft_X_Pos); // add to left side coordinate
  ram[ScreenLeft_X_Pos] = a; // save as new left side coordinate
  ram[HorizontalScroll] = a; // save here also
  lda_abs(ScreenLeft_PageLoc);
  adc_imm_fc(0x0); // add carry to page location for left
  ram[ScreenLeft_PageLoc] = a; // side of the screen
  and_imm(0x1); // get LSB of page location
  ram[0x0] = a; // save as temp variable for PPU register 1 mirror
  lda_abs(Mirror_PPU_CTRL_REG1); // get PPU register 1 mirror
  and_imm(0b11111110); // save all bits except d0
  ora_zp_fzn(0x0); // get saved bit here and save in PPU register 1
  ram[Mirror_PPU_CTRL_REG1] = a; // mirror to be used to set name table later
  cpu_call_begin(0xaff2); GetScreenPosition(); cpu_call_end(); // figure out where the right side is
  lda_imm(0x8);
  ram[ScrollIntervalTimer] = a; // set scroll timer (residual, not used elsewhere)
  goto ChkPOffscr; // skip this part
  
ChkPOffscr:
  ldx_imm_fzn(0x0); // set X for player offset
  cpu_call_begin(0xb004); GetXOffscreenBits(); cpu_call_end(); // get horizontal offscreen bits for player
  ram[0x0] = a; // save them here
  ldy_imm(0x0); // load default offset (left side)
  asl_acc_fc(); // if d7 of offscreen bits are set,
  if (carry_flag) { goto KeepOnscr; } // branch with default offset
  iny(); // otherwise use different offset (right side)
  lda_zp(0x0);
  and_imm_fz(0b00100000); // check offscreen bits for d5 set
  if (zero_flag) { goto InitPlatScrl; } // if not set, branch ahead of this part
  
KeepOnscr:
  lda_absy(ScreenEdge_X_Pos); // get left or right side coordinate based on offset
  carry_flag = true;
  sbc_absy_fc(X_SubtracterData); // subtract amount based on offset
  ram[Player_X_Position] = a; // store as player position to prevent movement further
  lda_absy(ScreenEdge_PageLoc); // get left or right page location based on offset
  sbc_imm(0x0); // subtract borrow
  ram[Player_PageLoc] = a; // save as player's page location
  lda_zp(Left_Right_Buttons); // check saved controller bits
  cmp_absy_fcz(OffscrJoypadBitsData); // against bits based on offset
  if (zero_flag) { goto InitPlatScrl; } // if not equal, branch
  lda_imm(0x0);
  ram[Player_X_Speed] = a; // otherwise nullify horizontal speed of player
  
InitPlatScrl:
  lda_imm_fzn(0x0); // nullify platform force imposed on scroll
  ram[Platform_X_Scroll] = a;
  return;
}

void GetScreenPosition(void) {
  lda_abs(ScreenLeft_X_Pos); // get coordinate of screen's left boundary
  carry_flag = false;
  adc_imm_fc(0xff); // add 255 pixels
  ram[ScreenRight_X_Pos] = a; // store as coordinate of screen's right boundary
  lda_abs(ScreenLeft_PageLoc); // get page number where left boundary is
  adc_imm_fczn(0x0); // add carry from before
  ram[ScreenRight_PageLoc] = a; // store as page number where right boundary is
  return;
  // -------------------------------------------------------------------------------------
}

void GameRoutines(void) {
  lda_zp_fzn(GameEngineSubroutine); // run routine based on number (a few of these routines are   
  cpu_call_begin(0xb04e);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0x9131: Entrance_GameTimerSetup(); return;
    case 0xb1c7: Vine_AutoClimb(); return;
    case 0xb206: SideExitPipeEntry(); return;
    case 0xb1e5: VerticalPipeEntry(); return;
    case 0xb2a4: FlagpoleSlide(); return;
    case 0xb2ca: PlayerEndLevel(); return;
    case 0x91cd: PlayerLoseLife(); return;
    case 0xb069: PlayerEntrance(); return;
    case 0xb0e9: PlayerCtrlRoutine(); return;
    case 0xb233: PlayerChangeSize(); return;
    case 0xb245: PlayerInjuryBlink(); return;
    case 0xb269: PlayerDeath(); return;
    case 0xb27d: PlayerFireFlower(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void PlayerEntrance(void) {
  // PlayerEntrance:
  lda_abs(AltEntranceControl); // check for mode of alternate entry
  cmp_imm_fcz(0x2);
  if (zero_flag) { goto EntrMode2; } // if found, branch to enter from pipe or with vine
  lda_imm(0x0);
  ldy_zp(Player_Y_Position); // if vertical position above a certain
  cpy_imm_fc(0x30); // point, nullify controller bits and continue
  if (!carry_flag) { AutoControlPlayer(); return; } // with player movement code, do not return
  lda_abs(PlayerEntranceCtrl); // check player entry bits from header
  cmp_imm_fcz(0x6);
  if (zero_flag) { goto ChkBehPipe; } // if set to 6 or 7, execute pipe intro code
  cmp_imm_fcz(0x7); // otherwise branch to normal entry
  if (!zero_flag) { goto PlayerRdy; }
  
ChkBehPipe:
  lda_abs_fzn(Player_SprAttrib); // check for sprite attributes
  if (!zero_flag) { goto IntroEntr; } // branch if found
  lda_imm(0x1);
  AutoControlPlayer(); return; // force player to walk to the right
  
IntroEntr:
  cpu_call_begin(0xb08f); EnterSidePipe(); cpu_call_end(); // execute sub to move player to the right
  dec_abs_fzn(ChangeAreaTimer); // decrement timer for change of area
  if (!zero_flag) { return; } // branch to exit if not yet expired
  inc_abs(DisableIntermediate); // set flag to skip world and lives display
  goto NextArea; // jump to increment to next area and set modes
  
EntrMode2:
  lda_abs_fz(JoypadOverride); // if controller override bits set here,
  if (!zero_flag) { goto VineEntr; } // branch to enter with vine
  lda_imm_fzn(0xff); // otherwise, set value here then execute sub
  cpu_call_begin(0xb0a4); MovePlayerYAxis(); cpu_call_end(); // to move player upwards (note $ff = -1)
  lda_zp(Player_Y_Position); // check to see if player is at a specific coordinate
  cmp_imm_fczn(0x91); // if player risen to a certain point (this requires pipes
  if (!carry_flag) { goto PlayerRdy; } // to be at specific height to look/function right) branch
  return; // to the last part, otherwise leave
  
VineEntr:
  lda_abs(VineHeight);
  cmp_imm_fczn(0x60); // check vine height
  if (!zero_flag) { return; } // if vine not yet reached maximum height, branch to leave
  lda_zp(Player_Y_Position); // get player's vertical coordinate
  cmp_imm_fc(0x99); // check player's vertical coordinate against preset value
  ldy_imm(0x0); // load default values to be written to 
  lda_imm_fzn(0x1); // this value moves player to the right off the vine
  if (!carry_flag) { goto OffVine; } // if vertical coordinate < preset value, use defaults
  lda_imm(0x3);
  ram[Player_State] = a; // otherwise set player state to climbing
  iny(); // increment value in Y
  lda_imm_fzn(0x8); // set block in block buffer to cover hole, then 
  ram[Block_Buffer_1 + 0xb4] = a; // use same value to force player to climb
  
OffVine:
  ram[DisableCollisionDet] = y; // set collision detection disable flag
  cpu_call_begin(0xb0cc); AutoControlPlayer(); cpu_call_end(); // use contents of A to move player up or right, execute sub
  lda_zp(Player_X_Position);
  cmp_imm_fczn(0x48); // check player's horizontal position
  if (!carry_flag) { return; } // if not far enough to the right, branch to leave
  
PlayerRdy:
  lda_imm(0x8); // set routine to be executed by game engine next frame
  ram[GameEngineSubroutine] = a;
  lda_imm(0x1); // set to face player to the right
  ram[PlayerFacingDir] = a;
  lsr_acc_fczn(); // init A
  ram[AltEntranceControl] = a; // init mode of entry
  ram[DisableCollisionDet] = a; // init collision detection disable flag
  ram[JoypadOverride] = a; // nullify controller override bits
  // ExitEntr:
  return; // leave!
  // -------------------------------------------------------------------------------------
  // $07 - used to hold upper limit of high byte when player falls down hole
  
NextArea:
  inc_abs_fzn(AreaNumber); // increment area number used for address loader
  cpu_call_begin(0xb31a); LoadAreaPointer(); cpu_call_end(); // get new level pointer
  inc_abs_fzn(FetchNewGameTimerFlag); // set flag to load new game timer
  cpu_call_begin(0xb320); ChgAreaMode(); cpu_call_end(); // do sub to set secondary mode, disable screen and sprite 0
  ram[HalfwayPage] = a; // reset halfway page to 0 (beginning)
  lda_imm_fzn(Silence);
  ram[EventMusicQueue] = a; // silence music and leave
  // ExitNA:
  return;
  // -------------------------------------------------------------------------------------
}

void AutoControlPlayer(void) {
  ram[SavedJoypadBits] = a; // override controller bits with contents of A if executing here
  PlayerCtrlRoutine(); // fallthrough
  return;
}

void PlayerCtrlRoutine(void) {
  // PlayerCtrlRoutine:
  lda_zp(GameEngineSubroutine); // check task here
  cmp_imm_fczn(0xb); // if certain value is set, branch to skip controller bit loading
  if (zero_flag) { goto SizeChk; }
  lda_abs_fz(AreaType); // are we in a water type area?
  if (!zero_flag) { goto SaveJoyp; } // if not, branch
  ldy_zp(Player_Y_HighPos);
  dey_fz(); // if not in vertical area between
  if (!zero_flag) { goto DisJoyp; } // status bar and bottom, branch
  lda_zp(Player_Y_Position);
  cmp_imm_fc(0xd0); // if nearing the bottom of the screen or
  if (!carry_flag) { goto SaveJoyp; } // not in the vertical area between status bar or bottom,
  
DisJoyp:
  lda_imm(0x0); // disable controller bits
  ram[SavedJoypadBits] = a;
  
SaveJoyp:
  lda_abs(SavedJoypadBits); // otherwise store A and B buttons in $0a
  and_imm(0b11000000);
  ram[A_B_Buttons] = a;
  lda_abs(SavedJoypadBits); // store left and right buttons in $0c
  and_imm(0b00000011);
  ram[Left_Right_Buttons] = a;
  lda_abs(SavedJoypadBits); // store up and down buttons in $0b
  and_imm(0b00001100);
  ram[Up_Down_Buttons] = a;
  and_imm_fzn(0b00000100); // check for pressing down
  if (zero_flag) { goto SizeChk; } // if not, branch
  lda_zp_fzn(Player_State); // check player's state
  if (!zero_flag) { goto SizeChk; } // if not on the ground, branch
  ldy_zp_fzn(Left_Right_Buttons); // check left and right
  if (zero_flag) { goto SizeChk; } // if neither pressed, branch
  lda_imm_fzn(0x0);
  ram[Left_Right_Buttons] = a; // if pressing down while on the ground,
  ram[Up_Down_Buttons] = a; // nullify directional bits
  
SizeChk:
  cpu_call_begin(0xb12d); PlayerMovementSubs(); cpu_call_end(); // run movement subroutines
  ldy_imm(0x1); // is player small?
  lda_abs_fz(PlayerSize);
  if (!zero_flag) { goto ChkMoveDir; }
  ldy_imm(0x0); // check for if crouching
  lda_abs_fz(CrouchingFlag);
  if (zero_flag) { goto ChkMoveDir; } // if not, branch ahead
  ldy_imm(0x2); // if big and crouching, load y with 2
  
ChkMoveDir:
  ram[Player_BoundBoxCtrl] = y; // set contents of Y as player's bounding box size control
  lda_imm(0x1); // set moving direction to right by default
  ldy_zp_fzn(Player_X_Speed); // check player's horizontal speed
  if (zero_flag) { goto PlayerSubs; } // if not moving at all horizontally, skip this part
  if (!neg_flag) { goto SetMoveDir; } // if moving to the right, use default moving direction
  asl_acc_fczn(); // otherwise change to move to the left
  
SetMoveDir:
  ram[Player_MovingDir] = a; // set moving direction
  
PlayerSubs:
  cpu_call_begin(0xb14e); ScrollHandler(); cpu_call_end(); // move the screen if necessary
  cpu_call_begin(0xb151); GetPlayerOffscreenBits(); cpu_call_end(); // get player's offscreen bits
  cpu_call_begin(0xb154); RelativePlayerPosition(); cpu_call_end(); // get coordinates relative to the screen
  ldx_imm_fzn(0x0); // set offset for player object
  cpu_call_begin(0xb159); BoundingBoxCore(); cpu_call_end(); // get player's bounding box coordinates
  cpu_call_begin(0xb15c); PlayerBGCollision(); cpu_call_end(); // do collision detection and process
  lda_zp(Player_Y_Position);
  cmp_imm_fc(0x40); // check to see if player is higher than 64th pixel
  if (!carry_flag) { goto PlayerHole; } // if so, branch ahead
  lda_zp(GameEngineSubroutine);
  cmp_imm_fz(0x5); // if running end-of-level routine, branch ahead
  if (zero_flag) { goto PlayerHole; }
  cmp_imm_fz(0x7); // if running player entrance routine, branch ahead
  if (zero_flag) { goto PlayerHole; }
  cmp_imm_fc(0x4); // if running routines $00-$03, branch ahead
  if (!carry_flag) { goto PlayerHole; }
  lda_abs(Player_SprAttrib);
  and_imm(0b11011111); // otherwise nullify player's
  ram[Player_SprAttrib] = a; // background priority flag
  
PlayerHole:
  lda_zp(Player_Y_HighPos); // check player's vertical high byte
  cmp_imm_fczn(0x2); // for below the screen
  if (neg_flag) { return; } // branch to leave if not that far down
  ldx_imm(0x1);
  ram[ScrollLock] = x; // set scroll lock
  ldy_imm(0x4);
  ram[0x7] = y; // set value here
  ldx_imm(0x0); // use X as flag, and clear for cloud level
  ldy_abs_fz(GameTimerExpiredFlag); // check game timer expiration flag
  if (!zero_flag) { goto HoleDie; } // if set, branch
  ldy_abs_fz(CloudTypeOverride); // check for cloud type override
  if (!zero_flag) { goto ChkHoleX; } // skip to last part if found
  
HoleDie:
  inx(); // set flag in X for player death
  ldy_zp(GameEngineSubroutine);
  cpy_imm_fz(0xb); // check for some other routine running
  if (zero_flag) { goto ChkHoleX; } // if so, branch ahead
  ldy_abs_fz(DeathMusicLoaded); // check value here
  if (!zero_flag) { goto HoleBottom; } // if already set, branch to next part
  iny();
  ram[EventMusicQueue] = y; // otherwise play death music
  ram[DeathMusicLoaded] = y; // and set value here
  
HoleBottom:
  ldy_imm(0x6);
  ram[0x7] = y; // change value here
  
ChkHoleX:
  cmp_zp_fczn(0x7); // compare vertical high byte with value set here
  if (neg_flag) { return; } // if less, branch to leave
  dex_fn(); // otherwise decrement flag in X
  if (neg_flag) { goto CloudExit; } // if flag was clear, branch to set modes and other values
  ldy_abs_fzn(EventMusicBuffer); // check to see if music is still playing
  if (!zero_flag) { return; } // branch to leave if so
  lda_imm_fzn(0x6); // otherwise set to run lose life routine
  ram[GameEngineSubroutine] = a; // on next frame
  // ExitCtrl:
  return; // leave
  
CloudExit:
  lda_imm_fzn(0x0);
  ram[JoypadOverride] = a; // clear controller override bits if any are set
  cpu_call_begin(0xb1c2); SetEntr(); cpu_call_end(); // do sub to set secondary mode
  inc_abs_fzn(AltEntranceControl); // set mode of entry to 3
  return;
  // -------------------------------------------------------------------------------------
}

void Vine_AutoClimb(void) {
  lda_zp_fz(Player_Y_HighPos); // check to see whether player reached position
  // above the status bar yet and if so, set modes
  if (zero_flag) {
    lda_zp(Player_Y_Position);
    cmp_imm_fc(0xe4);
    if (!carry_flag) {
      SetEntr();
      return;
    }
  }
  // AutoClimb:
  lda_imm(0b00001000); // set controller bits override to up
  ram[JoypadOverride] = a;
  ldy_imm(0x3); // set player state to climbing
  ram[Player_State] = y;
  AutoControlPlayer(); return;
}

void SetEntr(void) {
  lda_imm(0x2); // set starting position to override
  ram[AltEntranceControl] = a;
  ChgAreaMode(); return; // set modes
  // -------------------------------------------------------------------------------------
}

void VerticalPipeEntry(void) {
  lda_imm_fzn(0x1); // set 1 as movement amount
  cpu_call_begin(0xb1e9); MovePlayerYAxis(); cpu_call_end(); // do sub to move player downwards
  cpu_call_begin(0xb1ec); ScrollHandler(); cpu_call_end(); // do sub to scroll screen with saved force if necessary
  ldy_imm(0x0); // load default mode of entry
  lda_abs_fz(WarpZoneControl); // check warp zone control variable/flag
  // if set, branch to use mode 0
  if (zero_flag) {
    iny();
    lda_abs(AreaType); // check for castle level type
    cmp_imm_fcz(0x3);
    // if not castle type level, use mode 1
    if (zero_flag) {
      iny();
      goto ChgAreaPipe; // otherwise use mode 2
    }
  }
  
ChgAreaPipe:
  dec_abs_fzn(ChangeAreaTimer); // decrement timer for change of area
  if (zero_flag) {
    ram[AltEntranceControl] = y; // when timer expires set mode of alternate entry
    ChgAreaMode(); // fallthrough
    return;
  }
}

void MovePlayerYAxis(void) {
  carry_flag = false;
  adc_zp_fczn(Player_Y_Position); // add contents of A to player position
  ram[Player_Y_Position] = a;
  return;
  // -------------------------------------------------------------------------------------
}

void SideExitPipeEntry(void) {
  cpu_call_begin(0xb208); EnterSidePipe(); cpu_call_end(); // execute sub to move player to the right
  ldy_imm(0x2);
  // ChgAreaPipe:
  dec_abs_fzn(ChangeAreaTimer); // decrement timer for change of area
  if (zero_flag) {
    ram[AltEntranceControl] = y; // when timer expires set mode of alternate entry
    ChgAreaMode(); // fallthrough
    return;
  }
}

void ChgAreaMode(void) {
  inc_abs(DisableScreenFlag); // set flag to disable screen output
  lda_imm_fzn(0x0);
  ram[OperMode_Task] = a; // set secondary mode of operation
  ram[Sprite0HitDetectFlag] = a; // disable sprite 0 check
  // ExitCAPipe:
  return; // leave
}

void EnterSidePipe(void) {
  lda_imm(0x8); // set player's horizontal speed
  ram[Player_X_Speed] = a;
  ldy_imm(0x1); // set controller right button by default
  lda_zp(Player_X_Position); // mask out higher nybble of player's
  and_imm_fz(0b00001111); // horizontal position
  if (zero_flag) {
    ram[Player_X_Speed] = a; // if lower nybble = 0, set as horizontal speed
    tay(); // and nullify controller bit override here
  }
  // RightPipe:
  tya_fzn(); // use contents of Y to
  cpu_call_begin(0xb231); AutoControlPlayer(); cpu_call_end(); // execute player control routine with ctrl bits nulled
  return;
  // -------------------------------------------------------------------------------------
}

void PlayerChangeSize(void) {
  // PlayerChangeSize:
  lda_abs(TimerControl); // check master timer control
  cmp_imm_fcz(0xf8); // for specific moment in time
  if (!zero_flag) { goto EndChgSize; } // branch if before or after that point
  goto InitChangeSize; // otherwise run code to get growing/shrinking going
  
EndChgSize:
  cmp_imm_fczn(0xc4); // check again for another specific moment
  if (!zero_flag) { return; } // and branch to leave if before or after that point
  cpu_call_begin(0xb243); DonePlayerTask(); cpu_call_end(); // otherwise do sub to init timer control and set routine
  // ExitChgSize:
  return; // and then leave
  // -------------------------------------------------------------------------------------
  
InitChangeSize:
  ldy_abs_fzn(PlayerChangeSizeFlag); // if growing/shrinking flag already set
  if (!zero_flag) { return; } // then branch to leave
  ram[PlayerAnimCtrl] = y; // otherwise initialize player's animation frame control
  inc_abs(PlayerChangeSizeFlag); // set growing/shrinking flag
  lda_abs(PlayerSize);
  eor_imm_fzn(0x1); // invert player's size
  ram[PlayerSize] = a;
  // ExitBoth:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00 - used in CyclePlayerPalette to store current palette to cycle
}

void PlayerInjuryBlink(void) {
  // PlayerInjuryBlink:
  lda_abs(TimerControl); // check master timer control
  cmp_imm_fczn(0xf0); // for specific moment in time
  if (carry_flag) { goto ExitBlink; } // branch if before that point
  cmp_imm_fcz(0xc8); // check again for another specific point
  if (zero_flag) { DonePlayerTask(); return; } // branch if at that point, and not before or after
  PlayerCtrlRoutine(); return; // otherwise run player control routine
  
ExitBlink:
  if (!zero_flag) { return; } // do unconditional branch to leave
  // InitChangeSize:
  ldy_abs_fzn(PlayerChangeSizeFlag); // if growing/shrinking flag already set
  if (!zero_flag) { return; } // then branch to leave
  ram[PlayerAnimCtrl] = y; // otherwise initialize player's animation frame control
  inc_abs(PlayerChangeSizeFlag); // set growing/shrinking flag
  lda_abs(PlayerSize);
  eor_imm_fzn(0x1); // invert player's size
  ram[PlayerSize] = a;
  // ExitBoth:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00 - used in CyclePlayerPalette to store current palette to cycle
}

void PlayerDeath(void) {
  lda_abs(TimerControl); // check master timer control
  cmp_imm_fczn(0xf0); // for specific moment in time
  if (!carry_flag) {
    PlayerCtrlRoutine(); return; // otherwise run player control routine
  }
}

void DonePlayerTask(void) {
  lda_imm(0x0);
  ram[TimerControl] = a; // initialize master timer control to continue timers
  lda_imm_fzn(0x8);
  ram[GameEngineSubroutine] = a; // set player control routine to run next frame
  return; // leave
}

void PlayerFireFlower(void) {
  lda_abs(TimerControl); // check master timer control
  cmp_imm_fczn(0xc0); // for specific moment in time
  // branch if at moment, not before or after
  if (!zero_flag) {
    lda_zp(FrameCounter); // get frame counter
    lsr_acc();
    lsr_acc_fc(); // divide by four to change every four frames
    CyclePlayerPalette(); // fallthrough
    return;
  }
  // ResetPalFireFlower:
  cpu_call_begin(0xb299); DonePlayerTask(); cpu_call_end(); // do sub to init timer control and run player control routine
  ResetPalStar(); // fallthrough
  return;
}

void CyclePlayerPalette(void) {
  and_imm(0x3); // mask out all but d1-d0 (previously d3-d2)
  ram[0x0] = a; // store result here to use as palette bits
  lda_abs(Player_SprAttrib); // get player attributes
  and_imm(0b11111100); // save any other bits but palette bits
  ora_zp_fzn(0x0); // add palette bits
  ram[Player_SprAttrib] = a; // store as new player attributes
  return; // and leave
}

void ResetPalStar(void) {
  lda_abs(Player_SprAttrib); // get player attributes
  and_imm_fzn(0b11111100); // mask out palette bits to force palette 0
  ram[Player_SprAttrib] = a; // store as new player attributes
  return; // and leave
}

void FlagpoleSlide(void) {
  lda_zp(Enemy_ID + 5); // check special use enemy slot
  cmp_imm_fcz(FlagpoleFlagObject); // for flagpole flag object
  // if not found, branch to something residual
  if (zero_flag) {
    lda_abs(FlagpoleSoundQueue); // load flagpole sound
    ram[Square1SoundQueue] = a; // into square 1's sfx queue
    lda_imm(0x0);
    ram[FlagpoleSoundQueue] = a; // init flagpole sound queue
    ldy_zp(Player_Y_Position);
    cpy_imm_fc(0x9e); // check to see if player has slid down
    // far enough, and if so, branch with no controller bits set
    if (!carry_flag) {
      lda_imm(0x4); // otherwise force player to climb down (to slide)
    }
    // SlidePlayer:
    AutoControlPlayer(); return; // jump to player control routine
  }
  // NoFPObj:
  inc_zp_fzn(GameEngineSubroutine); // increment to next routine (this may
  return; // be residual code)
  // -------------------------------------------------------------------------------------
}

void PlayerEndLevel(void) {
  lda_imm_fzn(0x1); // force player to walk to the right
  cpu_call_begin(0xb2ce); AutoControlPlayer(); cpu_call_end();
  lda_zp(Player_Y_Position); // check player's vertical position
  cmp_imm_fc(0xae);
  // if player is not yet off the flagpole, skip this part
  if (carry_flag) {
    lda_abs_fz(ScrollLock); // if scroll lock not set, branch ahead to next part
    // because we only need to do this part once
    if (!zero_flag) {
      lda_imm(EndOfLevelMusic);
      ram[EventMusicQueue] = a; // load win level music in event music queue
      lda_imm(0x0);
      ram[ScrollLock] = a; // turn off scroll lock to skip this part later
    }
  }
  // ChkStop:
  lda_abs(Player_CollisionBits); // get player collision bits
  lsr_acc_fc(); // check for d0 set
  // if d0 set, skip to next part
  if (!carry_flag) {
    lda_abs_fz(StarFlagTaskControl); // if star flag task control already set,
    // go ahead with the rest of the code
    if (zero_flag) {
      inc_abs(StarFlagTaskControl); // otherwise set task control now (this gets ball rolling!)
    }
    // InCastle:
    lda_imm(0b00100000); // set player's background priority bit to
    ram[Player_SprAttrib] = a; // give illusion of being inside the castle
  }
  // RdyNextA:
  lda_abs(StarFlagTaskControl);
  cmp_imm_fczn(0x5); // if star flag task control not yet set
  if (zero_flag) {
    inc_abs(LevelNumber); // increment level number used for game logic
    lda_abs(LevelNumber);
    cmp_imm_fcz(0x3); // check to see if we have yet reached level -4
    // and skip this last part here if not
    if (zero_flag) {
      ldy_abs(WorldNumber); // get world number as offset
      lda_abs(CoinTallyFor1Ups); // check third area coin tally for bonus 1-ups
      cmp_absy_fc(Hidden1UpCoinAmts); // against minimum value, if player has not collected
      // at least this number of coins, leave flag clear
      if (carry_flag) {
        inc_abs(Hidden1UpFlag); // otherwise set hidden 1-up box control flag
      }
    }
    // NextArea:
    inc_abs_fzn(AreaNumber); // increment area number used for address loader
    cpu_call_begin(0xb31a); LoadAreaPointer(); cpu_call_end(); // get new level pointer
    inc_abs_fzn(FetchNewGameTimerFlag); // set flag to load new game timer
    cpu_call_begin(0xb320); ChgAreaMode(); cpu_call_end(); // do sub to set secondary mode, disable screen and sprite 0
    ram[HalfwayPage] = a; // reset halfway page to 0 (beginning)
    lda_imm_fzn(Silence);
    ram[EventMusicQueue] = a; // silence music and leave
    // ExitNA:
    return;
    // -------------------------------------------------------------------------------------
  }
}

void PlayerMovementSubs(void) {
  // PlayerMovementSubs:
  lda_imm(0x0); // set A to init crouch flag by default
  ldy_abs_fzn(PlayerSize); // is player small?
  if (!zero_flag) { goto SetCrouch; } // if so, branch
  lda_zp_fzn(Player_State); // check state of player
  if (!zero_flag) { goto ProcMove; } // if not on the ground, branch
  lda_zp(Up_Down_Buttons); // load controller bits for up and down
  and_imm_fzn(0b00000100); // single out bit for down button
  
SetCrouch:
  ram[CrouchingFlag] = a; // store value in crouch flag
  
ProcMove:
  cpu_call_begin(0xb33d); PlayerPhysicsSub(); cpu_call_end(); // run sub related to jumping and swimming
  lda_abs_fzn(PlayerChangeSizeFlag); // if growing/shrinking flag set,
  if (!zero_flag) { return; } // branch to leave
  lda_zp(Player_State);
  cmp_imm_fczn(0x3); // get player state
  if (zero_flag) { goto MoveSubs; } // if climbing, branch ahead, leave timer unset
  ldy_imm_fzn(0x18);
  ram[ClimbSideTimer] = y; // otherwise reset timer now
  
MoveSubs:
  cpu_call_begin(0xb350);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xb35a: OnGroundStateSub(); return;
    case 0xb376: JumpSwimSub(); return;
    case 0xb36d: FallingSub(); return;
    case 0xb3cf: ClimbingSub(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void OnGroundStateSub(void) {
  cpu_call_begin(0xb35c); GetPlayerAnimSpeed(); cpu_call_end(); // do a sub to set animation frame timing
  lda_zp_fzn(Left_Right_Buttons);
  // if left/right controller bits not set, skip instruction
  if (!zero_flag) {
    ram[PlayerFacingDir] = a; // otherwise set new facing direction
  }
  // GndMove:
  cpu_call_begin(0xb365); ImposeFriction(); cpu_call_end(); // do a sub to impose friction on player's walk/run
  cpu_call_begin(0xb368); MovePlayerHorizontally(); cpu_call_end(); // do another sub to move player horizontally
  ram[Player_X_Scroll] = a; // set returned value as player's movement speed for scroll
  return;
  // --------------------------------
}

void FallingSub(void) {
  // FallingSub:
  lda_abs(VerticalForceDown);
  ram[VerticalForce] = a; // dump vertical movement force for falling into main one
  goto LRAir; // movement force, then skip ahead to process left/right movement
  // --------------------------------
  
LRAir:
  lda_zp_fzn(Left_Right_Buttons); // check left/right controller bits (check for jumping/falling)
  if (zero_flag) { goto JSMove; } // if not pressing any, skip
  cpu_call_begin(0xb3b2); ImposeFriction(); cpu_call_end(); // otherwise process horizontal movement
  
JSMove:
  cpu_call_begin(0xb3b5); MovePlayerHorizontally(); cpu_call_end(); // do a sub to move player horizontally
  ram[Player_X_Scroll] = a; // set player's speed here, to be used for scroll later
  lda_zp(GameEngineSubroutine);
  cmp_imm_fcz(0xb); // check for specific routine selected
  if (!zero_flag) { goto ExitMov1; } // branch if not set to run
  lda_imm(0x28);
  ram[VerticalForce] = a; // otherwise set fractional
  
ExitMov1:
  goto MovePlayerVertically; // jump to move player vertically, then leave
  // --------------------------------
  
MovePlayerVertically:
  ldx_imm(0x0); // set X for player offset
  lda_abs_fz(TimerControl);
  if (!zero_flag) { goto NoJSChk; } // if master timer control set, branch ahead
  lda_abs_fzn(JumpspringAnimCtrl); // otherwise check to see if jumpspring is animating
  if (!zero_flag) { return; } // branch to leave if so
  
NoJSChk:
  lda_abs(VerticalForce); // dump vertical force 
  ram[0x0] = a;
  lda_imm(0x4); // set maximum vertical speed here
  ImposeGravitySprObj(); return; // then jump to move player vertically
  // --------------------------------
}

void JumpSwimSub(void) {
  // JumpSwimSub:
  ldy_zp_fn(Player_Y_Speed); // if player's vertical speed zero
  if (!neg_flag) { goto DumpFall; } // or moving downwards, branch to falling
  lda_zp(A_B_Buttons);
  and_imm(A_Button); // check to see if A button is being pressed
  and_zp_fz(PreviousA_B_Buttons); // and was pressed in previous frame
  if (!zero_flag) { goto ProcSwim; } // if so, branch elsewhere
  lda_abs(JumpOrigin_Y_Position); // get vertical position player jumped from
  carry_flag = true;
  sbc_zp(Player_Y_Position); // subtract current from original vertical coordinate
  cmp_abs_fc(DiffToHaltJump); // compare to value set here to see if player is in mid-jump
  if (!carry_flag) { goto ProcSwim; } // or just starting to jump, if just starting, skip ahead
  
DumpFall:
  lda_abs(VerticalForceDown); // otherwise dump falling into main fractional
  ram[VerticalForce] = a;
  
ProcSwim:
  lda_abs_fzn(SwimmingFlag); // if swimming flag not set,
  if (zero_flag) { goto LRAir; } // branch ahead to last part
  cpu_call_begin(0xb39a); GetPlayerAnimSpeed(); cpu_call_end(); // do a sub to get animation frame timing
  lda_zp(Player_Y_Position);
  cmp_imm_fc(0x14); // check vertical position against preset value
  if (carry_flag) { goto LRWater; } // if not yet reached a certain position, branch ahead
  lda_imm(0x18);
  ram[VerticalForce] = a; // otherwise set fractional
  
LRWater:
  lda_zp_fz(Left_Right_Buttons); // check left/right controller bits (check for swimming)
  if (zero_flag) { goto LRAir; } // if not pressing any, skip
  ram[PlayerFacingDir] = a; // otherwise set facing direction accordingly
  
LRAir:
  lda_zp_fzn(Left_Right_Buttons); // check left/right controller bits (check for jumping/falling)
  if (zero_flag) { goto JSMove; } // if not pressing any, skip
  cpu_call_begin(0xb3b2); ImposeFriction(); cpu_call_end(); // otherwise process horizontal movement
  
JSMove:
  cpu_call_begin(0xb3b5); MovePlayerHorizontally(); cpu_call_end(); // do a sub to move player horizontally
  ram[Player_X_Scroll] = a; // set player's speed here, to be used for scroll later
  lda_zp(GameEngineSubroutine);
  cmp_imm_fcz(0xb); // check for specific routine selected
  if (!zero_flag) { goto ExitMov1; } // branch if not set to run
  lda_imm(0x28);
  ram[VerticalForce] = a; // otherwise set fractional
  
ExitMov1:
  goto MovePlayerVertically; // jump to move player vertically, then leave
  // --------------------------------
  
MovePlayerVertically:
  ldx_imm(0x0); // set X for player offset
  lda_abs_fz(TimerControl);
  if (!zero_flag) { goto NoJSChk; } // if master timer control set, branch ahead
  lda_abs_fzn(JumpspringAnimCtrl); // otherwise check to see if jumpspring is animating
  if (!zero_flag) { return; } // branch to leave if so
  
NoJSChk:
  lda_abs(VerticalForce); // dump vertical force 
  ram[0x0] = a;
  lda_imm(0x4); // set maximum vertical speed here
  ImposeGravitySprObj(); return; // then jump to move player vertically
  // --------------------------------
}

void ClimbingSub(void) {
  // ClimbingSub:
  lda_abs(Player_YMF_Dummy);
  carry_flag = false; // add movement force to dummy variable
  adc_abs_fc(Player_Y_MoveForce); // save with carry
  ram[Player_YMF_Dummy] = a;
  ldy_imm(0x0); // set default adder here
  lda_zp_fn(Player_Y_Speed); // get player's vertical speed
  if (!neg_flag) { goto MoveOnVine; } // if not moving upwards, branch
  dey(); // otherwise set adder to $ff
  
MoveOnVine:
  ram[0x0] = y; // store adder here
  adc_zp_fc(Player_Y_Position); // add carry to player's vertical position
  ram[Player_Y_Position] = a; // and store to move player up or down
  lda_zp(Player_Y_HighPos);
  adc_zp_fc(0x0); // add carry to player's page location
  ram[Player_Y_HighPos] = a; // and store
  lda_zp(Left_Right_Buttons); // compare left/right controller bits
  and_abs_fzn(Player_CollisionBits); // to collision flag
  if (zero_flag) { goto InitCSTimer; } // if not set, skip to end
  ldy_abs_fzn(ClimbSideTimer); // otherwise check timer 
  if (!zero_flag) { return; } // if timer not expired, branch to leave
  ldy_imm(0x18);
  ram[ClimbSideTimer] = y; // otherwise set timer now
  ldx_imm(0x0); // set default offset here
  ldy_zp(PlayerFacingDir); // get facing direction
  lsr_acc_fc(); // move right button controller bit to carry
  if (carry_flag) { goto ClimbFD; } // if controller right pressed, branch ahead
  inx();
  inx(); // otherwise increment offset by 2 bytes
  
ClimbFD:
  dey_fz(); // check to see if facing right
  if (zero_flag) { goto CSetFDir; } // if so, branch, do not increment
  inx(); // otherwise increment by 1 byte
  
CSetFDir:
  lda_zp(Player_X_Position);
  carry_flag = false; // add or subtract from player's horizontal position
  adc_absx_fc(ClimbAdderLow); // using value here as adder and X as offset
  ram[Player_X_Position] = a;
  lda_zp(Player_PageLoc); // add or subtract carry or borrow using value here
  adc_absx_fc(ClimbAdderHigh); // from the player's page location
  ram[Player_PageLoc] = a;
  lda_zp(Left_Right_Buttons); // get left/right controller bits again
  eor_imm_fzn(0b00000011); // invert them and store them while player
  ram[PlayerFacingDir] = a; // is on vine to face player in opposite direction
  // ExitCSub:
  return; // then leave
  
InitCSTimer:
  ram[ClimbSideTimer] = a; // initialize timer here
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used to store offset to friction data
}

void PlayerPhysicsSub(void) {
  // PlayerPhysicsSub:
  lda_zp(Player_State); // check player state
  cmp_imm_fcz(0x3);
  if (!zero_flag) { goto CheckForJumping; } // if not climbing, branch
  ldy_imm(0x0);
  lda_zp(Up_Down_Buttons); // get controller bits for up/down
  and_abs_fz(Player_CollisionBits); // check against player's collision detection bits
  if (zero_flag) { goto ProcClimb; } // if not pressing up or down, branch
  iny();
  and_imm_fz(0b00001000); // check for pressing up
  if (!zero_flag) { goto ProcClimb; }
  iny();
  
ProcClimb:
  ldx_absy(Climb_Y_MForceData); // load value here
  ram[Player_Y_MoveForce] = x; // store as vertical movement force
  lda_imm(0x8); // load default animation timing
  ldx_absy_fzn(Climb_Y_SpeedData); // load some other value here
  ram[Player_Y_Speed] = x; // store as vertical speed
  if (neg_flag) { goto SetCAnim; } // if climbing down, use default animation timing value
  lsr_acc_fczn(); // otherwise divide timer setting by 2
  
SetCAnim:
  ram[PlayerAnimTimerSet] = a; // store animation timer setting and leave
  return;
  
CheckForJumping:
  lda_abs_fz(JumpspringAnimCtrl); // if jumpspring animating, 
  if (!zero_flag) { goto NoJump; } // skip ahead to something else
  lda_zp(A_B_Buttons); // check for A button press
  and_imm_fz(A_Button);
  if (zero_flag) { goto NoJump; } // if not, branch to something else
  and_zp_fz(PreviousA_B_Buttons); // if button not pressed in previous frame, branch
  if (zero_flag) { goto ProcJumping; }
  
NoJump:
  goto X_Physics; // otherwise, jump to something else
  
ProcJumping:
  lda_zp_fz(Player_State); // check player state
  if (zero_flag) { goto InitJS; } // if on the ground, branch
  lda_abs_fz(SwimmingFlag); // if swimming flag not set, jump to do something else
  if (zero_flag) { goto NoJump; } // to prevent midair jumping, otherwise continue
  lda_abs_fz(JumpSwimTimer); // if jump/swim timer nonzero, branch
  if (!zero_flag) { goto InitJS; }
  lda_zp_fn(Player_Y_Speed); // check player's vertical speed
  if (!neg_flag) { goto InitJS; } // if player's vertical speed motionless or down, branch
  goto X_Physics; // if timer at zero and player still rising, do not swim
  
InitJS:
  lda_imm(0x20); // set jump/swim timer
  ram[JumpSwimTimer] = a;
  ldy_imm(0x0); // initialize vertical force and dummy variable
  ram[Player_YMF_Dummy] = y;
  ram[Player_Y_MoveForce] = y;
  lda_zp(Player_Y_HighPos); // get vertical high and low bytes of jump origin
  ram[JumpOrigin_Y_HighPos] = a; // and store them next to each other here
  lda_zp(Player_Y_Position);
  ram[JumpOrigin_Y_Position] = a;
  lda_imm(0x1); // set player state to jumping/swimming
  ram[Player_State] = a;
  lda_abs(Player_XSpeedAbsolute); // check value related to walking/running speed
  cmp_imm_fc(0x9);
  if (!carry_flag) { goto ChkWtr; } // branch if below certain values, increment Y
  iny(); // for each amount equal or exceeded
  cmp_imm_fc(0x10);
  if (!carry_flag) { goto ChkWtr; }
  iny();
  cmp_imm_fc(0x19);
  if (!carry_flag) { goto ChkWtr; }
  iny();
  cmp_imm_fc(0x1c);
  if (!carry_flag) { goto ChkWtr; } // note that for jumping, range is 0-4 for Y
  iny();
  
ChkWtr:
  lda_imm(0x1); // set value here (apparently always set to 1)
  ram[DiffToHaltJump] = a;
  lda_abs_fz(SwimmingFlag); // if swimming flag disabled, branch
  if (zero_flag) { goto GetYPhy; }
  ldy_imm(0x5); // otherwise set Y to 5, range is 5-6
  lda_abs_fz(Whirlpool_Flag); // if whirlpool flag not set, branch
  if (zero_flag) { goto GetYPhy; }
  iny(); // otherwise increment to 6
  
GetYPhy:
  lda_absy(JumpMForceData); // store appropriate jump/swim
  ram[VerticalForce] = a; // data here
  lda_absy(FallMForceData);
  ram[VerticalForceDown] = a;
  lda_absy(InitMForceData);
  ram[Player_Y_MoveForce] = a;
  lda_absy(PlayerYSpdData);
  ram[Player_Y_Speed] = a;
  lda_abs_fz(SwimmingFlag); // if swimming flag disabled, branch
  if (zero_flag) { goto PJumpSnd; }
  lda_imm(Sfx_EnemyStomp); // load swim/goomba stomp sound into
  ram[Square1SoundQueue] = a; // square 1's sfx queue
  lda_zp(Player_Y_Position);
  cmp_imm_fc(0x14); // check vertical low byte of player position
  if (carry_flag) { goto X_Physics; } // if below a certain point, branch
  lda_imm(0x0); // otherwise reset player's vertical speed
  ram[Player_Y_Speed] = a; // and jump to something else to keep player
  goto X_Physics; // from swimming above water level
  
PJumpSnd:
  lda_imm(Sfx_BigJump); // load big mario's jump sound by default
  ldy_abs_fz(PlayerSize); // is mario big?
  if (zero_flag) { goto SJumpSnd; }
  lda_imm(Sfx_SmallJump); // if not, load small mario's jump sound
  
SJumpSnd:
  ram[Square1SoundQueue] = a; // store appropriate jump sound in square 1 sfx queue
  
X_Physics:
  ldy_imm(0x0);
  ram[0x0] = y; // init value here
  lda_zp_fz(Player_State); // if mario is on the ground, branch
  if (zero_flag) { goto ProcPRun; }
  lda_abs(Player_XSpeedAbsolute); // check something that seems to be related
  cmp_imm_fc(0x19); // to mario's speed
  if (carry_flag) { goto GetXPhy; } // if =>$19 branch here
  if (!carry_flag) { goto ChkRFast; } // if not branch elsewhere
  
ProcPRun:
  iny(); // if mario on the ground, increment Y
  lda_abs_fz(AreaType); // check area type
  if (zero_flag) { goto ChkRFast; } // if water type, branch
  dey(); // decrement Y by default for non-water type area
  lda_zp(Left_Right_Buttons); // get left/right controller bits
  cmp_zp_fz(Player_MovingDir); // check against moving direction
  if (!zero_flag) { goto ChkRFast; } // if controller bits <> moving direction, skip this part
  lda_zp(A_B_Buttons); // check for b button pressed
  and_imm_fz(B_Button);
  if (!zero_flag) { goto SetRTmr; } // if pressed, skip ahead to set timer
  lda_abs_fz(RunningTimer); // check for running timer set
  if (!zero_flag) { goto GetXPhy; } // if set, branch
  
ChkRFast:
  iny(); // if running timer not set or level type is water, 
  inc_zp(0x0); // increment Y again and temp variable in memory
  lda_abs_fz(RunningSpeed);
  if (!zero_flag) { goto FastXSp; } // if running speed set here, branch
  lda_abs(Player_XSpeedAbsolute);
  cmp_imm_fc(0x21); // otherwise check player's walking/running speed
  if (!carry_flag) { goto GetXPhy; } // if less than a certain amount, branch ahead
  
FastXSp:
  inc_zp(0x0); // if running speed set or speed => $21 increment $00
  goto GetXPhy; // and jump ahead
  
SetRTmr:
  lda_imm(0xa); // if b button pressed, set running timer
  ram[RunningTimer] = a;
  
GetXPhy:
  lda_absy(MaxLeftXSpdData); // get maximum speed to the left
  ram[MaximumLeftSpeed] = a;
  lda_zp(GameEngineSubroutine); // check for specific routine running
  cmp_imm_fz(0x7); // (player entrance)
  if (!zero_flag) { goto GetXPhy2; } // if not running, skip and use old value of Y
  ldy_imm(0x3); // otherwise set Y to 3
  
GetXPhy2:
  lda_absy(MaxRightXSpdData); // get maximum speed to the right
  ram[MaximumRightSpeed] = a;
  ldy_zp(0x0); // get other value in memory
  lda_absy(FrictionData); // get value using value in memory as offset
  ram[FrictionAdderLow] = a;
  lda_imm(0x0);
  ram[FrictionAdderHigh] = a; // init something here
  lda_zp(PlayerFacingDir);
  cmp_zp_fczn(Player_MovingDir); // check facing direction against moving direction
  if (zero_flag) { return; } // if the same, branch to leave
  asl_abs_fc(FrictionAdderLow); // otherwise shift d7 of friction adder low into carry
  rol_abs_fczn(FrictionAdderHigh); // then rotate carry onto d0 of friction adder high
  // ExitPhy:
  return; // and then leave
  // -------------------------------------------------------------------------------------
}

void GetPlayerAnimSpeed(void) {
  // GetPlayerAnimSpeed:
  ldy_imm(0x0); // initialize offset in Y
  lda_abs(Player_XSpeedAbsolute); // check player's walking/running speed
  cmp_imm_fc(0x1c); // against preset amount
  if (carry_flag) { goto SetRunSpd; } // if greater than a certain amount, branch ahead
  iny(); // otherwise increment Y
  cmp_imm_fc(0xe); // compare against lower amount
  if (carry_flag) { goto ChkSkid; } // if greater than this but not greater than first, skip increment
  iny(); // otherwise increment Y again
  
ChkSkid:
  lda_abs(SavedJoypadBits); // get controller bits
  and_imm_fz(0b01111111); // mask out A button
  if (zero_flag) { goto SetAnimSpd; } // if no other buttons pressed, branch ahead of all this
  and_imm(0x3); // mask out all others except left and right
  cmp_zp_fcz(Player_MovingDir); // check against moving direction
  if (!zero_flag) { goto ProcSkid; } // if left/right controller bits <> moving direction, branch
  lda_imm(0x0); // otherwise set zero value here
  
SetRunSpd:
  ram[RunningSpeed] = a; // store zero or running speed here
  goto SetAnimSpd;
  
ProcSkid:
  lda_abs(Player_XSpeedAbsolute); // check player's walking/running speed
  cmp_imm_fc(0xb); // against one last amount
  if (carry_flag) { goto SetAnimSpd; } // if greater than this amount, branch
  lda_zp(PlayerFacingDir);
  ram[Player_MovingDir] = a; // otherwise use facing direction to set moving direction
  lda_imm(0x0);
  ram[Player_X_Speed] = a; // nullify player's horizontal speed
  ram[Player_X_MoveForce] = a; // and dummy variable for player
  
SetAnimSpd:
  lda_absy_fzn(PlayerAnimTmrData); // get animation timer setting using Y as offset
  ram[PlayerAnimTimerSet] = a;
  return;
  // -------------------------------------------------------------------------------------
}

void ImposeFriction(void) {
  // ImposeFriction:
  and_abs(Player_CollisionBits); // perform AND between left/right controller bits and collision flag
  cmp_imm_fcz(0x0); // then compare to zero (this instruction is redundant)
  if (!zero_flag) { goto JoypFrict; } // if any bits set, branch to next part
  lda_zp_fzn(Player_X_Speed);
  if (zero_flag) { goto SetAbsSpd; } // if player has no horizontal speed, branch ahead to last part
  if (!neg_flag) { goto RghtFrict; } // if player moving to the right, branch to slow
  if (neg_flag) { goto LeftFrict; } // otherwise logic dictates player moving left, branch to slow
  
JoypFrict:
  lsr_acc_fc(); // put right controller bit into carry
  if (!carry_flag) { goto RghtFrict; } // if left button pressed, carry = 0, thus branch
  
LeftFrict:
  lda_abs(Player_X_MoveForce); // load value set here
  carry_flag = false;
  adc_abs_fc(FrictionAdderLow); // add to it another value set here
  ram[Player_X_MoveForce] = a; // store here
  lda_zp(Player_X_Speed);
  adc_abs(FrictionAdderHigh); // add value plus carry to horizontal speed
  ram[Player_X_Speed] = a; // set as new horizontal speed
  cmp_abs_fcn(MaximumRightSpeed); // compare against maximum value for right movement
  if (neg_flag) { goto XSpdSign; } // if horizontal speed greater negatively, branch
  lda_abs_fzn(MaximumRightSpeed); // otherwise set preset value as horizontal speed
  ram[Player_X_Speed] = a; // thus slowing the player's left movement down
  goto SetAbsSpd; // skip to the end
  
RghtFrict:
  lda_abs(Player_X_MoveForce); // load value set here
  carry_flag = true;
  sbc_abs_fc(FrictionAdderLow); // subtract from it another value set here
  ram[Player_X_MoveForce] = a; // store here
  lda_zp(Player_X_Speed);
  sbc_abs(FrictionAdderHigh); // subtract value plus borrow from horizontal speed
  ram[Player_X_Speed] = a; // set as new horizontal speed
  cmp_abs_fn(MaximumLeftSpeed); // compare against maximum value for left movement
  if (!neg_flag) { goto XSpdSign; } // if horizontal speed greater positively, branch
  lda_abs(MaximumLeftSpeed); // otherwise set preset value as horizontal speed
  ram[Player_X_Speed] = a; // thus slowing the player's right movement down
  
XSpdSign:
  cmp_imm_fczn(0x0); // if player not moving or moving to the right,
  if (!neg_flag) { goto SetAbsSpd; } // branch and leave horizontal speed value unmodified
  eor_imm(0xff);
  carry_flag = false; // otherwise get two's compliment to get absolute
  adc_imm_fczn(0x1); // unsigned walking/running speed
  
SetAbsSpd:
  ram[Player_XSpeedAbsolute] = a; // store walking/running speed here and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used to store downward movement force in FireballObjCore
  // $02 - used to store maximum vertical speed in FireballObjCore
  // $07 - used to store pseudorandom bit in BubbleCheck
}

void ProcFireball_Bubble(void) {
  lda_abs(PlayerStatus); // check player's status
  cmp_imm_fc(0x2);
  // if not fiery, branch
  if (carry_flag) {
    lda_zp(A_B_Buttons);
    and_imm_fz(B_Button); // check for b button pressed
    // branch if not pressed
    if (!zero_flag) {
      and_zp_fz(PreviousA_B_Buttons);
      // if button pressed in previous frame, branch
      if (zero_flag) {
        lda_abs(FireballCounter); // load fireball counter
        and_imm(0b00000001); // get LSB and use as offset for buffer
        tax();
        lda_zpx_fz(Fireball_State); // load fireball state
        // if not inactive, branch
        if (zero_flag) {
          ldy_zp(Player_Y_HighPos); // if player too high or too low, branch
          dey_fz();
          if (zero_flag) {
            lda_abs_fz(CrouchingFlag); // if player crouching, branch
            if (zero_flag) {
              lda_zp(Player_State); // if player's state = climbing, branch
              cmp_imm_fcz(0x3);
              if (!zero_flag) {
                lda_imm(Sfx_Fireball); // play fireball sound effect
                ram[Square1SoundQueue] = a;
                lda_imm(0x2); // load state
                ram[Fireball_State + x] = a;
                ldy_abs(PlayerAnimTimerSet); // copy animation frame timer setting
                ram[FireballThrowingTimer] = y; // into fireball throwing timer
                dey();
                ram[PlayerAnimTimer] = y; // decrement and store in player's animation timer
                inc_abs(FireballCounter); // increment fireball counter
              }
            }
          }
        }
      }
    }
    // ProcFireballs:
    ldx_imm_fzn(0x0);
    cpu_call_begin(0xb668); FireballObjCore(); cpu_call_end(); // process first fireball object
    ldx_imm_fzn(0x1);
    cpu_call_begin(0xb66d); FireballObjCore(); cpu_call_end(); // process second fireball object, then do air bubbles
  }
  // ProcAirBubbles:
  lda_abs_fzn(AreaType); // if not water type level, skip the rest of this
  if (zero_flag) {
    ldx_imm_fzn(0x2); // otherwise load counter and use as offset
    
BublLoop:
    ram[ObjectOffset] = x; // store offset
    cpu_call_begin(0xb679); BubbleCheck(); cpu_call_end(); // check timers and coordinates, create air bubble
    cpu_call_begin(0xb67c); RelativeBubblePosition(); cpu_call_end(); // get relative coordinates
    cpu_call_begin(0xb67f); GetBubbleOffscreenBits(); cpu_call_end(); // get offscreen information
    cpu_call_begin(0xb682); DrawBubble(); cpu_call_end(); // draw the air bubble
    dex_fzn();
    if (!neg_flag) { goto BublLoop; } // do this until all three are handled
    // BublExit:
    return; // then leave
  }
}

void FireballObjCore(void) {
  // FireballObjCore:
  ram[ObjectOffset] = x; // store offset as current object
  lda_zpx(Fireball_State); // check for d7 = 1
  asl_acc_fczn();
  if (carry_flag) { goto FireballExplosion; } // if so, branch to get relative coordinates and draw explosion
  ldy_zpx_fzn(Fireball_State); // if fireball inactive, branch to leave
  if (zero_flag) { return; }
  dey_fz(); // if fireball state set to 1, skip this part and just run it
  if (zero_flag) { goto RunFB; }
  lda_zp(Player_X_Position); // get player's horizontal position
  adc_imm_fc(0x4); // add four pixels and store as fireball's horizontal position
  ram[Fireball_X_Position + x] = a;
  lda_zp(Player_PageLoc); // get player's page location
  adc_imm(0x0); // add carry and store as fireball's page location
  ram[Fireball_PageLoc + x] = a;
  lda_zp(Player_Y_Position); // get player's vertical position and store
  ram[Fireball_Y_Position + x] = a;
  lda_imm(0x1); // set high byte of vertical position
  ram[Fireball_Y_HighPos + x] = a;
  ldy_zp(PlayerFacingDir); // get player's facing direction
  dey(); // decrement to use as offset here
  lda_absy(FireballXSpdData); // set horizontal speed of fireball accordingly
  ram[Fireball_X_Speed + x] = a;
  lda_imm(0x4); // set vertical speed of fireball
  ram[Fireball_Y_Speed + x] = a;
  lda_imm(0x7);
  ram[Fireball_BoundBoxCtrl + x] = a; // set bounding box size control for fireball
  dec_zpx(Fireball_State); // decrement state to 1 to skip this part from now on
  
RunFB:
  txa(); // add 7 to offset to use
  carry_flag = false; // as fireball offset for next routines
  adc_imm_fc(0x7);
  tax();
  lda_imm(0x50); // set downward movement force here
  ram[0x0] = a;
  lda_imm(0x3); // set maximum speed here
  ram[0x2] = a;
  lda_imm_fzn(0x0);
  cpu_call_begin(0xb6cf); ImposeGravity(); cpu_call_end(); // do sub here to impose gravity on fireball and move vertically
  cpu_call_begin(0xb6d2); MoveObjectHorizontally(); cpu_call_end(); // do another sub to move it horizontally
  ldx_zp_fzn(ObjectOffset); // return fireball offset to X
  cpu_call_begin(0xb6d7); RelativeFireballPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xb6da); GetFireballOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xb6dd); GetFireballBoundBox(); cpu_call_end(); // get bounding box coordinates
  cpu_call_begin(0xb6e0); FireballBGCollision(); cpu_call_end(); // do fireball to background collision detection
  lda_abs(FBall_OffscreenBits); // get fireball offscreen bits
  and_imm_fzn(0b11001100); // mask out certain bits
  if (!zero_flag) { goto EraseFB; } // if any bits still set, branch to kill fireball
  cpu_call_begin(0xb6ea); FireballEnemyCollision(); cpu_call_end(); // do fireball to enemy collision detection and deal with collisions
  goto DrawFireball; // draw fireball appropriately and leave
  
EraseFB:
  lda_imm_fzn(0x0); // erase fireball state
  ram[Fireball_State + x] = a;
  // NoFBall:
  return; // leave
  
FireballExplosion:
  cpu_call_begin(0xb6f5); RelativeFireballPosition(); cpu_call_end();
  goto DrawExplosion_Fireball;
  
DrawFireball:
  ldy_absx(FBall_SprDataOffset); // get fireball's sprite data offset
  lda_abs(Fireball_Rel_YPos); // get relative vertical coordinate
  ram[Sprite_Y_Position + y] = a; // store as sprite Y coordinate
  lda_abs(Fireball_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as sprite X coordinate, then do shared code
  DrawFirebar(); // fallthrough
  return;
  
DrawExplosion_Fireball:
  ldy_absx(Alt_SprDataOffset); // get OAM data offset of alternate sort for fireball's explosion
  lda_zpx(Fireball_State); // load fireball state
  inc_zpx(Fireball_State); // increment state for next frame
  lsr_acc(); // divide by 2
  and_imm(0b00000111); // mask out all but d3-d1
  cmp_imm_fc(0x3); // check to see if time to kill fireball
  if (carry_flag) { goto KillFireBall; } // branch if so, otherwise continue to draw explosion
  DrawExplosion_Fireworks(); // fallthrough
  return;
  
KillFireBall:
  lda_imm_fzn(0x0); // clear fireball state to kill it
  ram[Fireball_State + x] = a;
  return;
  // -------------------------------------------------------------------------------------
}

void BubbleCheck(void) {
  // BubbleCheck:
  lda_absx(PseudoRandomBitReg + 1); // get part of LSFR
  and_imm(0x1);
  ram[0x7] = a; // store pseudorandom bit here
  lda_zpx(Bubble_Y_Position); // get vertical coordinate for air bubble
  cmp_imm_fcz(0xf8); // if offscreen coordinate not set,
  if (!zero_flag) { goto MoveBubl; } // branch to move air bubble
  lda_abs_fzn(AirBubbleTimer); // if air bubble timer not expired,
  if (!zero_flag) { return; } // branch to leave, otherwise create new air bubble
  SetupBubble(); // fallthrough
  return;
  
MoveBubl:
  ldy_zp(0x7); // get pseudorandom bit again, use as offset
  lda_absx(Bubble_YMF_Dummy);
  carry_flag = true; // subtract pseudorandom amount from dummy variable
  sbc_absy_fc(Bubble_MForceData);
  ram[Bubble_YMF_Dummy + x] = a; // save dummy variable
  lda_zpx(Bubble_Y_Position);
  sbc_imm(0x0); // subtract borrow from airbubble's vertical coordinate
  cmp_imm_fczn(0x20); // if below the status bar,
  if (carry_flag) { goto Y_Bubl; } // branch to go ahead and use to move air bubble upwards
  lda_imm_fzn(0xf8); // otherwise set offscreen coordinate
  
Y_Bubl:
  ram[Bubble_Y_Position + x] = a; // store as new vertical coordinate for air bubble
  // ExitBubl:
  return; // leave
}

void SetupBubble(void) {
  ldy_imm(0x0); // load default value here
  lda_zp(PlayerFacingDir); // get player's facing direction
  lsr_acc_fc(); // move d0 to carry
  // branch to use default value if facing left
  if (carry_flag) {
    ldy_imm(0x8); // otherwise load alternate value here
  }
  // PosBubl:
  tya(); // use value loaded as adder
  adc_zp_fc(Player_X_Position); // add to player's horizontal position
  ram[Bubble_X_Position + x] = a; // save as horizontal position for airbubble
  lda_zp(Player_PageLoc);
  adc_imm(0x0); // add carry to player's page location
  ram[Bubble_PageLoc + x] = a; // save as page location for airbubble
  lda_zp(Player_Y_Position);
  carry_flag = false; // add eight pixels to player's vertical position
  adc_imm(0x8);
  ram[Bubble_Y_Position + x] = a; // save as vertical position for air bubble
  lda_imm(0x1);
  ram[Bubble_Y_HighPos + x] = a; // set vertical high byte for air bubble
  ldy_zp(0x7); // get pseudorandom bit, use as offset
  lda_absy(BubbleTimerData); // get data for air bubble timer
  ram[AirBubbleTimer] = a; // set air bubble timer
  // MoveBubl:
  ldy_zp(0x7); // get pseudorandom bit again, use as offset
  lda_absx(Bubble_YMF_Dummy);
  carry_flag = true; // subtract pseudorandom amount from dummy variable
  sbc_absy_fc(Bubble_MForceData);
  ram[Bubble_YMF_Dummy + x] = a; // save dummy variable
  lda_zpx(Bubble_Y_Position);
  sbc_imm(0x0); // subtract borrow from airbubble's vertical coordinate
  cmp_imm_fczn(0x20); // if below the status bar,
  // branch to go ahead and use to move air bubble upwards
  if (!carry_flag) {
    lda_imm_fzn(0xf8); // otherwise set offscreen coordinate
  }
  // Y_Bubl:
  ram[Bubble_Y_Position + x] = a; // store as new vertical coordinate for air bubble
  // ExitBubl:
  return; // leave
}

void RunGameTimer(void) {
  // RunGameTimer:
  lda_abs_fzn(OperMode); // get primary mode of operation
  if (zero_flag) { return; } // branch to leave if in title screen mode
  lda_zp(GameEngineSubroutine);
  cmp_imm_fczn(0x8); // if routine number less than eight running,
  if (!carry_flag) { return; } // branch to leave
  cmp_imm_fczn(0xb); // if running death routine,
  if (zero_flag) { return; } // branch to leave
  lda_zp(Player_Y_HighPos);
  cmp_imm_fczn(0x2); // if player below the screen,
  if (carry_flag) { return; } // branch to leave regardless of level type
  lda_abs_fzn(GameTimerCtrlTimer); // if game timer control not yet expired,
  if (!zero_flag) { return; } // branch to leave
  lda_abs(GameTimerDisplay);
  ora_abs(GameTimerDisplay + 1); // otherwise check game timer digits
  ora_abs_fzn(GameTimerDisplay + 2);
  if (zero_flag) { goto TimeUpOn; } // if game timer digits at 000, branch to time-up code
  ldy_abs(GameTimerDisplay); // otherwise check first digit
  dey_fz(); // if first digit not on 1,
  if (!zero_flag) { goto ResGTCtrl; } // branch to reset game timer control
  lda_abs(GameTimerDisplay + 1); // otherwise check second and third digits
  ora_abs_fz(GameTimerDisplay + 2);
  if (!zero_flag) { goto ResGTCtrl; } // if timer not at 100, branch to reset game timer control
  lda_imm(TimeRunningOutMusic);
  ram[EventMusicQueue] = a; // otherwise load time running out music
  
ResGTCtrl:
  lda_imm(0x18); // reset game timer control
  ram[GameTimerCtrlTimer] = a;
  ldy_imm(0x23); // set offset for last digit
  lda_imm_fzn(0xff); // set value to decrement game timer digit
  ram[DigitModifier + 5] = a;
  cpu_call_begin(0xb794); DigitsMathRoutine(); cpu_call_end(); // do sub to decrement game timer slowly
  lda_imm_fzn(0xa4); // set status nybbles to update game timer display
  PrintStatusBarNumbers(); return; // do sub to update the display
  
TimeUpOn:
  ram[PlayerStatus] = a; // init player status (note A will always be zero here)
  cpu_call_begin(0xb79f); ForceInjury(); cpu_call_end(); // do sub to kill the player (note player is small here)
  inc_abs_fzn(GameTimerExpiredFlag); // set game timer expiration flag
  // ExGTimer:
  return; // leave
  // -------------------------------------------------------------------------------------
}

void WarpZoneObject(void) {
  // WarpZoneObject:
  lda_abs_fzn(ScrollLock); // check for scroll lock flag
  if (zero_flag) { return; } // branch if not set to leave
  lda_zp(Player_Y_Position); // check to see if player's vertical coordinate has
  and_zp_fzn(Player_Y_HighPos); // same bits set as in vertical high byte (why?)
  if (!zero_flag) { return; } // if so, branch to leave
  ram[ScrollLock] = a; // otherwise nullify scroll lock flag
  inc_abs(WarpZoneControl); // increment warp zone flag to make warp pipes for warp zone
  EraseEnemyObject(); return; // kill this object
  // -------------------------------------------------------------------------------------
  // $00 - used in WhirlpoolActivate to store whirlpool length / 2, page location of center of whirlpool
  // and also to store movement force exerted on player
  // $01 - used in ProcessWhirlpools to store page location of right extent of whirlpool
  // and in WhirlpoolActivate to store center of whirlpool
  // $02 - used in ProcessWhirlpools to store right extent of whirlpool and in
  // WhirlpoolActivate to store maximum vertical speed
}

void ProcessWhirlpools(void) {
  // ProcessWhirlpools:
  lda_abs_fzn(AreaType); // check for water type level
  if (!zero_flag) { return; } // branch to leave if not found
  ram[Whirlpool_Flag] = a; // otherwise initialize whirlpool flag
  lda_abs_fzn(TimerControl); // if master timer control set,
  if (!zero_flag) { return; } // branch to leave
  ldy_imm(0x4); // otherwise start with last whirlpool data
  
WhLoop:
  lda_absy(Whirlpool_LeftExtent); // get left extent of whirlpool
  carry_flag = false;
  adc_absy_fc(Whirlpool_Length); // add length of whirlpool
  ram[0x2] = a; // store result as right extent here
  lda_absy_fz(Whirlpool_PageLoc); // get page location
  if (zero_flag) { goto NextWh; } // if none or page 0, branch to get next data
  adc_imm(0x0); // add carry
  ram[0x1] = a; // store result as page location of right extent here
  lda_zp(Player_X_Position); // get player's horizontal position
  carry_flag = true;
  sbc_absy_fc(Whirlpool_LeftExtent); // subtract left extent
  lda_zp(Player_PageLoc); // get player's page location
  sbc_absy_fcn(Whirlpool_PageLoc); // subtract borrow
  if (neg_flag) { goto NextWh; } // if player too far left, branch to get next data
  lda_zp(0x2); // otherwise get right extent
  carry_flag = true;
  sbc_zp_fc(Player_X_Position); // subtract player's horizontal coordinate
  lda_zp(0x1); // get right extent's page location
  sbc_zp_fcn(Player_PageLoc); // subtract borrow
  if (!neg_flag) { goto WhirlpoolActivate; } // if player within right extent, branch to whirlpool code
  
NextWh:
  dey_fzn(); // move onto next whirlpool data
  if (!neg_flag) { goto WhLoop; } // do this until all whirlpools are checked
  // ExitWh:
  return; // leave
  
WhirlpoolActivate:
  lda_absy(Whirlpool_Length); // get length of whirlpool
  lsr_acc(); // divide by 2
  ram[0x0] = a; // save here
  lda_absy(Whirlpool_LeftExtent); // get left extent of whirlpool
  carry_flag = false;
  adc_zp_fc(0x0); // add length divided by 2
  ram[0x1] = a; // save as center of whirlpool
  lda_absy(Whirlpool_PageLoc); // get page location
  adc_imm(0x0); // add carry
  ram[0x0] = a; // save as page location of whirlpool center
  lda_zp(FrameCounter); // get frame counter
  lsr_acc_fc(); // shift d0 into carry (to run on every other frame)
  if (!carry_flag) { goto WhPull; } // if d0 not set, branch to last part of code
  lda_zp(0x1); // get center
  carry_flag = true;
  sbc_zp_fc(Player_X_Position); // subtract player's horizontal coordinate
  lda_zp(0x0); // get page location of center
  sbc_zp_fn(Player_PageLoc); // subtract borrow
  if (!neg_flag) { goto LeftWh; } // if player to the left of center, branch
  lda_zp(Player_X_Position); // otherwise slowly pull player left, towards the center
  carry_flag = true;
  sbc_imm_fc(0x1); // subtract one pixel
  ram[Player_X_Position] = a; // set player's new horizontal coordinate
  lda_zp(Player_PageLoc);
  sbc_imm(0x0); // subtract borrow
  goto SetPWh; // jump to set player's new page location
  
LeftWh:
  lda_abs(Player_CollisionBits); // get player's collision bits
  lsr_acc_fc(); // shift d0 into carry
  if (!carry_flag) { goto WhPull; } // if d0 not set, branch
  lda_zp(Player_X_Position); // otherwise slowly pull player right, towards the center
  carry_flag = false;
  adc_imm_fc(0x1); // add one pixel
  ram[Player_X_Position] = a; // set player's new horizontal coordinate
  lda_zp(Player_PageLoc);
  adc_imm(0x0); // add carry
  
SetPWh:
  ram[Player_PageLoc] = a; // set player's new page location
  
WhPull:
  lda_imm(0x10);
  ram[0x0] = a; // set vertical movement force
  lda_imm(0x1);
  ram[Whirlpool_Flag] = a; // set whirlpool flag to be used later
  ram[0x2] = a; // also set maximum vertical speed
  lsr_acc();
  tax(); // set X for player offset
  ImposeGravity(); return; // jump to put whirlpool effect on player vertically, do not return
  // -------------------------------------------------------------------------------------
}

void FlagpoleRoutine(void) {
  // FlagpoleRoutine:
  ldx_imm(0x5); // set enemy object offset
  ram[ObjectOffset] = x; // to special use slot
  lda_zpx(Enemy_ID);
  cmp_imm_fczn(FlagpoleFlagObject); // if flagpole flag not found,
  if (!zero_flag) { return; } // branch to leave
  lda_zp(GameEngineSubroutine);
  cmp_imm_fczn(0x4); // if flagpole slide routine not running,
  if (!zero_flag) { goto SkipScore; } // branch to near the end of code
  lda_zp(Player_State);
  cmp_imm_fczn(0x3); // if player state not climbing,
  if (!zero_flag) { goto SkipScore; } // branch to near the end of code
  lda_zpx(Enemy_Y_Position); // check flagpole flag's vertical coordinate
  cmp_imm_fc(0xaa); // if flagpole flag down to a certain point,
  if (carry_flag) { goto GiveFPScr; } // branch to end the level
  lda_zp(Player_Y_Position); // check player's vertical coordinate
  cmp_imm_fc(0xa2); // if player down to a certain point,
  if (carry_flag) { goto GiveFPScr; } // branch to end the level
  lda_absx(Enemy_YMF_Dummy);
  adc_imm_fc(0xff); // add movement amount to dummy variable
  ram[Enemy_YMF_Dummy + x] = a; // save dummy variable
  lda_zpx(Enemy_Y_Position); // get flag's vertical coordinate
  adc_imm(0x1); // add 1 plus carry to move flag, and
  ram[Enemy_Y_Position + x] = a; // store vertical coordinate
  lda_abs(FlagpoleFNum_YMFDummy);
  carry_flag = true; // subtract movement amount from dummy variable
  sbc_imm_fc(0xff);
  ram[FlagpoleFNum_YMFDummy] = a; // save dummy variable
  lda_abs(FlagpoleFNum_Y_Pos);
  sbc_imm_fczn(0x1); // subtract one plus borrow to move floatey number,
  ram[FlagpoleFNum_Y_Pos] = a; // and store vertical coordinate here
  
SkipScore:
  goto FPGfx; // jump to skip ahead and draw flag and floatey number
  
GiveFPScr:
  ldy_abs(FlagpoleScore); // get score offset from earlier (when player touched flagpole)
  lda_absy(FlagpoleScoreMods); // get amount to award player points
  ldx_absy_fzn(FlagpoleScoreDigits); // get digit with which to award points
  ram[DigitModifier + x] = a; // store in digit modifier
  cpu_call_begin(0xb8a7); AddToScore(); cpu_call_end(); // do sub to award player points depending on height of collision
  lda_imm_fzn(0x5);
  ram[GameEngineSubroutine] = a; // set to run end-of-level subroutine on next frame
  
FPGfx:
  cpu_call_begin(0xb8ae); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xb8b1); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xb8b4); FlagpoleGfxHandler(); cpu_call_end(); // draw flagpole flag and floatey number
  // ExitFlagP:
  return;
  // -------------------------------------------------------------------------------------
}

void JumpspringHandler(void) {
  // JumpspringHandler:
  cpu_call_begin(0xb8bc); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen information
  lda_abs_fzn(TimerControl); // check master timer control
  if (!zero_flag) { goto DrawJSpr; } // branch to last section if set
  lda_abs_fzn(JumpspringAnimCtrl); // check jumpspring frame control
  if (zero_flag) { goto DrawJSpr; } // branch to last section if not set
  tay();
  dey(); // subtract one from frame control,
  tya(); // the only way a poor nmos 6502 can
  and_imm_fz(0b00000010); // mask out all but d1, original value still in Y
  if (!zero_flag) { goto DownJSpr; } // if set, branch to move player up
  inc_zp(Player_Y_Position);
  inc_zp(Player_Y_Position); // move player's vertical position down two pixels
  goto PosJSpr; // skip to next part
  
DownJSpr:
  dec_zp(Player_Y_Position); // move player's vertical position up two pixels
  dec_zp(Player_Y_Position);
  
PosJSpr:
  lda_zpx(Jumpspring_FixedYPos); // get permanent vertical position
  carry_flag = false;
  adc_absy(Jumpspring_Y_PosData); // add value using frame control as offset
  ram[Enemy_Y_Position + x] = a; // store as new vertical position
  cpy_imm_fc(0x1); // check frame control offset (second frame is $00)
  if (!carry_flag) { goto BounceJS; } // if offset not yet at third frame ($01), skip to next part
  lda_zp(A_B_Buttons);
  and_imm_fz(A_Button); // check saved controller bits for A button press
  if (zero_flag) { goto BounceJS; } // skip to next part if A not pressed
  and_zp_fz(PreviousA_B_Buttons); // check for A button pressed in previous frame
  if (!zero_flag) { goto BounceJS; } // skip to next part if so
  lda_imm(0xf4);
  ram[JumpspringForce] = a; // otherwise write new jumpspring force here
  
BounceJS:
  cpy_imm_fczn(0x3); // check frame control offset again
  if (!zero_flag) { goto DrawJSpr; } // skip to last part if not yet at fifth frame ($03)
  lda_abs(JumpspringForce);
  ram[Player_Y_Speed] = a; // store jumpspring force as player's new vertical speed
  lda_imm_fzn(0x0);
  ram[JumpspringAnimCtrl] = a; // initialize jumpspring frame control
  
DrawJSpr:
  cpu_call_begin(0xb904); RelativeEnemyPosition(); cpu_call_end(); // get jumpspring's relative coordinates
  cpu_call_begin(0xb907); EnemyGfxHandler(); cpu_call_end(); // draw jumpspring
  cpu_call_begin(0xb90a); OffscreenBoundsCheck(); cpu_call_end(); // check to see if we need to kill it
  lda_abs_fzn(JumpspringAnimCtrl); // if frame control at zero, don't bother
  if (zero_flag) { return; } // trying to animate it, just leave
  lda_abs_fzn(JumpspringTimer);
  if (!zero_flag) { return; } // if jumpspring timer not expired yet, leave
  lda_imm(0x4);
  ram[JumpspringTimer] = a; // otherwise initialize jumpspring timer
  inc_abs_fzn(JumpspringAnimCtrl); // increment frame control to animate jumpspring
  // ExJSpring:
  return; // leave
  // -------------------------------------------------------------------------------------
}

void Setup_Vine(void) {
  lda_imm(VineObject); // load identifier for vine object
  ram[Enemy_ID + x] = a; // store in buffer
  lda_imm(0x1);
  ram[Enemy_Flag + x] = a; // set flag for enemy object buffer
  lda_zpy(Block_PageLoc);
  ram[Enemy_PageLoc + x] = a; // copy page location from previous object
  lda_zpy(Block_X_Position);
  ram[Enemy_X_Position + x] = a; // copy horizontal coordinate from previous object
  lda_zpy(Block_Y_Position);
  ram[Enemy_Y_Position + x] = a; // copy vertical coordinate from previous object
  ldy_abs_fz(VineFlagOffset); // load vine flag/offset to next available vine slot
  // if set at all, don't bother to store vertical
  if (zero_flag) {
    ram[VineStart_Y_Position] = a; // otherwise store vertical coordinate here
  }
  // NextVO:
  txa(); // store object offset to next available vine slot
  ram[VineObjOffset + y] = a; // using vine flag as offset
  inc_abs(VineFlagOffset); // increment vine flag offset
  lda_imm_fzn(Sfx_GrowVine);
  ram[Square2SoundQueue] = a; // load vine grow sound
  return;
  // -------------------------------------------------------------------------------------
  // $06-$07 - used as address to block buffer data
  // $02 - used as vertical high nybble of block buffer offset
}

void VineObjectHandler(void) {
  cpx_imm_fcz(0x5); // check enemy offset for special use slot
  // if not in last slot, branch to leave
  if (zero_flag) {
    ldy_abs(VineFlagOffset);
    dey(); // decrement vine flag in Y, use as offset
    lda_abs(VineHeight);
    cmp_absy_fz(VineHeightData); // if vine has reached certain height,
    // branch ahead to skip this part
    if (!zero_flag) {
      lda_zp(FrameCounter); // get frame counter
      lsr_acc(); // shift d1 into carry
      lsr_acc_fc();
      // if d1 not set (2 frames every 4) skip this part
      if (carry_flag) {
        lda_zp(Enemy_Y_Position + 5);
        sbc_imm(0x1); // subtract vertical position of vine
        ram[Enemy_Y_Position + 5] = a; // one pixel every frame it's time
        inc_abs(VineHeight); // increment vine height
      }
    }
    // RunVSubs:
    lda_abs(VineHeight); // if vine still very small,
    cmp_imm_fczn(0x8); // branch to leave
    if (carry_flag) {
      cpu_call_begin(0xb973); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinates of vine,
      cpu_call_begin(0xb976); GetEnemyOffscreenBits(); cpu_call_end(); // and any offscreen bits
      ldy_imm_fzn(0x0); // initialize offset used in draw vine sub
      
VDrawLoop:
      cpu_call_begin(0xb97b); DrawVine(); cpu_call_end(); // draw vine
      iny(); // increment offset
      cpy_abs_fczn(VineFlagOffset); // if offset in Y and offset here
      if (!zero_flag) { goto VDrawLoop; } // do not yet match, loop back to draw more vine
      lda_abs(Enemy_OffscreenBits);
      and_imm_fz(0b00001100); // mask offscreen bits
      // if none of the saved offscreen bits set, skip ahead
      if (!zero_flag) {
        dey(); // otherwise decrement Y to get proper offset again
        
KillVine:
        ldx_absy_fzn(VineObjOffset); // get enemy object offset for this vine object
        cpu_call_begin(0xb98f); EraseEnemyObject(); cpu_call_end(); // kill this vine object
        dey_fn(); // decrement Y
        if (!neg_flag) { goto KillVine; } // if any vine objects left, loop back to kill it
        ram[VineFlagOffset] = a; // initialize vine flag/offset
        ram[VineHeight] = a; // initialize vine height
      }
      // WrCMTile:
      lda_abs(VineHeight); // check vine height
      cmp_imm_fc(0x20); // if vine small (less than 32 pixels tall)
      // then branch ahead to leave
      if (carry_flag) {
        ldx_imm(0x6); // set offset in X to last enemy slot
        lda_imm(0x1); // set A to obtain horizontal in $04, but we don't care
        ldy_imm_fzn(0x1b); // set Y to offset to get block at ($04, $10) of coordinates
        cpu_call_begin(0xb9a8); BlockBufferCollision(); cpu_call_end(); // do a sub to get block buffer address set, return contents
        ldy_zp(0x2);
        cpy_imm_fc(0xd0); // if vertical high nybble offset beyond extent of
        // current block buffer, branch to leave, do not write
        if (!carry_flag) {
          lda_indy_fz(0x6); // otherwise check contents of block buffer at 
          // current offset, if not empty, branch to leave
          if (zero_flag) {
            lda_imm(0x26);
            dynamic_ram_write(read_word(0x6) + y, a); // otherwise, write climbing metatile to block buffer
          }
        }
      }
    }
  }
  // ExitVH:
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
  // -------------------------------------------------------------------------------------
}

void ProcessCannons(void) {
  lda_abs_fzn(AreaType); // get area type
  if (!zero_flag) {
    ldx_imm(0x2);
    
ThreeSChk:
    ram[ObjectOffset] = x; // start at third enemy slot
    lda_zpx_fz(Enemy_Flag); // check enemy buffer flag
    // if set, branch to check enemy
    if (zero_flag) {
      lda_absx(PseudoRandomBitReg + 1); // otherwise get part of LSFR
      ldy_abs(SecondaryHardMode); // get secondary hard mode flag, use as offset
      and_absy(CannonBitmasks); // mask out bits of LSFR as decided by flag
      cmp_imm_fc(0x6); // check to see if lower nybble is above certain value
      // if so, branch to check enemy
      if (!carry_flag) {
        tay(); // transfer masked contents of LSFR to Y as pseudorandom offset
        lda_absy_fz(Cannon_PageLoc); // get page location
        // if not set or on page 0, branch to check enemy
        if (!zero_flag) {
          lda_absy_fz(Cannon_Timer); // get cannon timer
          // if expired, branch to fire cannon
          if (!zero_flag) {
            sbc_imm(0x0); // otherwise subtract borrow (note carry will always be clear here)
            ram[Cannon_Timer + y] = a; // to count timer down
            goto Chk_BB; // then jump ahead to check enemy
          }
          // FireCannon:
          lda_abs_fz(TimerControl); // if master timer control set,
          // branch to check enemy
          if (zero_flag) {
            lda_imm(0xe); // otherwise we start creating one
            ram[Cannon_Timer + y] = a; // first, reset cannon timer
            lda_absy(Cannon_PageLoc); // get page location of cannon
            ram[Enemy_PageLoc + x] = a; // save as page location of bullet bill
            lda_absy(Cannon_X_Position); // get horizontal coordinate of cannon
            ram[Enemy_X_Position + x] = a; // save as horizontal coordinate of bullet bill
            lda_absy(Cannon_Y_Position); // get vertical coordinate of cannon
            carry_flag = true;
            sbc_imm(0x8); // subtract eight pixels (because enemies are 24 pixels tall)
            ram[Enemy_Y_Position + x] = a; // save as vertical coordinate of bullet bill
            lda_imm(0x1);
            ram[Enemy_Y_HighPos + x] = a; // set vertical high byte of bullet bill
            ram[Enemy_Flag + x] = a; // set buffer flag
            lsr_acc_fc(); // shift right once to init A
            ram[Enemy_State + x] = a; // then initialize enemy's state
            lda_imm(0x9);
            ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control for bullet bill
            lda_imm(BulletBill_CannonVar);
            ram[Enemy_ID + x] = a; // load identifier for bullet bill (cannon variant)
            goto Next3Slt; // move onto next slot
          }
        }
      }
    }
    
Chk_BB:
    lda_zpx(Enemy_ID); // check enemy identifier for bullet bill (cannon variant)
    cmp_imm_fczn(BulletBill_CannonVar);
    // if not found, branch to get next slot
    if (zero_flag) {
      cpu_call_begin(0xba22); OffscreenBoundsCheck(); cpu_call_end(); // otherwise, check to see if it went offscreen
      lda_zpx_fzn(Enemy_Flag); // check enemy buffer flag
      // if not set, branch to get next slot
      if (!zero_flag) {
        cpu_call_begin(0xba29); GetEnemyOffscreenBits(); cpu_call_end(); // otherwise, get offscreen information
        cpu_call_begin(0xba2c); BulletBillHandler(); cpu_call_end(); // then do sub to handle bullet bill
      }
    }
    
Next3Slt:
    dex_fzn(); // move onto next slot
    if (!neg_flag) { goto ThreeSChk; } // do this until first three slots are checked
    // ExCannon:
    return; // then leave
    // --------------------------------
  }
}

void BulletBillHandler(void) {
  // BulletBillHandler:
  lda_abs_fzn(TimerControl); // if master timer control set,
  if (!zero_flag) { goto RunBBSubs; } // branch to run subroutines except movement sub
  lda_zpx_fz(Enemy_State);
  if (!zero_flag) { goto ChkDSte; } // if bullet bill's state set, branch to check defeated state
  lda_abs(Enemy_OffscreenBits); // otherwise load offscreen bits
  and_imm(0b00001100); // mask out bits
  cmp_imm_fczn(0b00001100); // check to see if all bits are set
  if (zero_flag) { goto KillBB; } // if so, branch to kill this object
  ldy_imm_fzn(0x1); // set to move right by default
  cpu_call_begin(0xba49); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and bullet bill
  if (neg_flag) { goto SetupBB; } // if enemy to the left of player, branch
  iny(); // otherwise increment to move left
  
SetupBB:
  ram[Enemy_MovingDir + x] = y; // set bullet bill's moving direction
  dey(); // decrement to use as offset
  lda_absy(BulletBillXSpdData); // get horizontal speed based on moving direction
  ram[Enemy_X_Speed + x] = a; // and store it
  lda_zp(0x0); // get horizontal difference
  adc_imm(0x28); // add 40 pixels
  cmp_imm_fczn(0x50); // if less than a certain amount, player is too close
  if (!carry_flag) { goto KillBB; } // to cannon either on left or right side, thus branch
  lda_imm(0x1);
  ram[Enemy_State + x] = a; // otherwise set bullet bill's state
  lda_imm(0xa);
  ram[EnemyFrameTimer + x] = a; // set enemy frame timer
  lda_imm(Sfx_Blast);
  ram[Square2SoundQueue] = a; // play fireworks/gunfire sound
  
ChkDSte:
  lda_zpx(Enemy_State); // check enemy state for d5 set
  and_imm_fzn(0b00100000);
  if (zero_flag) { goto BBFly; } // if not set, skip to move horizontally
  cpu_call_begin(0xba72); MoveD_EnemyVertically(); cpu_call_end(); // otherwise do sub to move bullet bill vertically
  
BBFly:
  cpu_call_begin(0xba75); MoveEnemyHorizontally(); cpu_call_end(); // do sub to move bullet bill horizontally
  
RunBBSubs:
  cpu_call_begin(0xba78); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xba7b); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xba7e); GetEnemyBoundBox(); cpu_call_end(); // get bounding box coordinates
  cpu_call_begin(0xba81); PlayerEnemyCollision(); cpu_call_end(); // handle player to enemy collisions
  EnemyGfxHandler(); return; // draw the bullet bill and leave
  
KillBB:
  cpu_call_begin(0xba87); EraseEnemyObject(); cpu_call_end(); // kill bullet bill and leave
  return;
  // -------------------------------------------------------------------------------------
}

void SpawnHammerObj(void) {
  lda_abs(PseudoRandomBitReg + 1); // get pseudorandom bits from
  and_imm_fz(0b00000111); // second part of LSFR
  // if any bits are set, branch and use as offset
  if (zero_flag) {
    lda_abs(PseudoRandomBitReg + 1);
    and_imm(0b00001000); // get d3 from same part of LSFR
  }
  // SetMOfs:
  tay(); // use either d3 or d2-d0 for offset here
  lda_zpy_fz(Misc_State); // if any values loaded in
  // $2a-$32 where offset is then leave with carry clear
  if (zero_flag) {
    ldx_absy(HammerEnemyOfsData); // get offset of enemy slot to check using Y as offset
    lda_zpx_fz(Enemy_Flag); // check enemy buffer flag at offset
    // if buffer flag set, branch to leave with carry clear
    if (zero_flag) {
      ldx_zp(ObjectOffset); // get original enemy object offset
      txa();
      ram[HammerEnemyOffset + y] = a; // save here
      lda_imm(0x90);
      ram[Misc_State + y] = a; // save hammer's state here
      lda_imm_fzn(0x7);
      ram[Misc_BoundBoxCtrl + y] = a; // set something else entirely, here
      carry_flag = true; // return with carry set
      return;
    }
  }
  // NoHammer:
  ldx_zp_fzn(ObjectOffset); // get original enemy object offset
  carry_flag = false; // return with carry clear
  return;
  // --------------------------------
  // $00 - used to set downward force
  // $01 - used to set upward force (residual)
  // $02 - used to set maximum speed
}

void ProcHammerObj(void) {
  // ProcHammerObj:
  lda_abs_fzn(TimerControl); // if master timer control set
  if (!zero_flag) { goto RunHSubs; } // skip all of this code and go to last subs at the end
  lda_zpx(Misc_State); // otherwise get hammer's state
  and_imm(0b01111111); // mask out d7
  ldy_absx(HammerEnemyOffset); // get enemy object offset that spawned this hammer
  cmp_imm_fcz(0x2); // check hammer's state
  if (zero_flag) { goto SetHSpd; } // if currently at 2, branch
  if (carry_flag) { goto SetHPos; } // if greater than 2, branch elsewhere
  txa();
  carry_flag = false; // add 13 bytes to use
  adc_imm_fc(0xd); // proper misc object
  tax(); // return offset to X
  lda_imm(0x10);
  ram[0x0] = a; // set downward movement force
  lda_imm(0xf);
  ram[0x1] = a; // set upward movement force (not used)
  lda_imm(0x4);
  ram[0x2] = a; // set maximum vertical speed
  lda_imm_fzn(0x0); // set A to impose gravity on hammer
  cpu_call_begin(0xbaea); ImposeGravity(); cpu_call_end(); // do sub to impose gravity on hammer and move vertically
  cpu_call_begin(0xbaed); MoveObjectHorizontally(); cpu_call_end(); // do sub to move it horizontally
  ldx_zp_fzn(ObjectOffset); // get original misc object offset
  goto RunAllH; // branch to essential subroutines
  
SetHSpd:
  lda_imm(0xfe);
  ram[Misc_Y_Speed + x] = a; // set hammer's vertical speed
  lda_zpy(Enemy_State); // get enemy object state
  and_imm(0b11110111); // mask out d3
  ram[Enemy_State + y] = a; // store new state
  ldx_zpy(Enemy_MovingDir); // get enemy's moving direction
  dex(); // decrement to use as offset
  lda_absx(HammerXSpdData); // get proper speed to use based on moving direction
  ldx_zp(ObjectOffset); // reobtain hammer's buffer offset
  ram[Misc_X_Speed + x] = a; // set hammer's horizontal speed
  
SetHPos:
  dec_zpx(Misc_State); // decrement hammer's state
  lda_zpy(Enemy_X_Position); // get enemy's horizontal position
  carry_flag = false;
  adc_imm_fc(0x2); // set position 2 pixels to the right
  ram[Misc_X_Position + x] = a; // store as hammer's horizontal position
  lda_zpy(Enemy_PageLoc); // get enemy's page location
  adc_imm(0x0); // add carry
  ram[Misc_PageLoc + x] = a; // store as hammer's page location
  lda_zpy(Enemy_Y_Position); // get enemy's vertical position
  carry_flag = true;
  sbc_imm_fc(0xa); // move position 10 pixels upward
  ram[Misc_Y_Position + x] = a; // store as hammer's vertical position
  lda_imm_fzn(0x1);
  ram[Misc_Y_HighPos + x] = a; // set hammer's vertical high byte
  goto RunHSubs; // unconditional branch to skip first routine
  
RunAllH:
  cpu_call_begin(0xbb2a); PlayerHammerCollision(); cpu_call_end(); // handle collisions
  
RunHSubs:
  cpu_call_begin(0xbb2d); GetMiscOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xbb30); RelativeMiscPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xbb33); GetMiscBoundBox(); cpu_call_end(); // get bounding box coordinates
  cpu_call_begin(0xbb36); DrawHammer(); cpu_call_end(); // draw the hammer
  return; // and we are done here
  // -------------------------------------------------------------------------------------
  // $02 - used to store vertical high nybble offset from block buffer routine
  // $06 - used to store low byte of block buffer address
}

void CoinBlock(void) {
  cpu_call_begin(0xbb3a); FindEmptyMiscSlot(); cpu_call_end(); // set offset for empty or last misc object buffer slot
  lda_zpx(Block_PageLoc); // get page location of block object
  ram[Misc_PageLoc + y] = a; // store as page location of misc object
  lda_zpx(Block_X_Position); // get horizontal coordinate of block object
  ora_imm(0x5); // add 5 pixels
  ram[Misc_X_Position + y] = a; // store as horizontal coordinate of misc object
  lda_zpx(Block_Y_Position); // get vertical coordinate of block object
  sbc_imm_fc(0x10); // subtract 16 pixels
  ram[Misc_Y_Position + y] = a; // store as vertical coordinate of misc object
  goto JCoinC; // jump to rest of code as applies to this misc object
  
JCoinC:
  lda_imm(0xfb);
  ram[Misc_Y_Speed + y] = a; // set vertical speed
  lda_imm_fzn(0x1);
  ram[Misc_Y_HighPos + y] = a; // set vertical high byte
  ram[Misc_State + y] = a; // set state for misc object
  ram[Square2SoundQueue] = a; // load coin grab sound
  ram[ObjectOffset] = x; // store current control bit as misc object offset 
  cpu_call_begin(0xbb7f); GiveOneCoin(); cpu_call_end(); // update coin tally on the screen and coin amount variable
  inc_abs_fzn(CoinTallyFor1Ups); // increment coin tally used to activate 1-up block flag
  return;
}

void SetupJumpCoin(void) {
  cpu_call_begin(0xbb53); FindEmptyMiscSlot(); cpu_call_end(); // set offset for empty or last misc object buffer slot
  lda_absx(Block_PageLoc2); // get page location saved earlier
  ram[Misc_PageLoc + y] = a; // and save as page location for misc object
  lda_zp(0x6); // get low byte of block buffer offset
  asl_acc();
  asl_acc(); // multiply by 16 to use lower nybble
  asl_acc();
  asl_acc_fc();
  ora_imm(0x5); // add five pixels
  ram[Misc_X_Position + y] = a; // save as horizontal coordinate for misc object
  lda_zp(0x2); // get vertical high nybble offset from earlier
  adc_imm_fc(0x20); // add 32 pixels for the status bar
  ram[Misc_Y_Position + y] = a; // store as vertical coordinate
  // JCoinC:
  lda_imm(0xfb);
  ram[Misc_Y_Speed + y] = a; // set vertical speed
  lda_imm_fzn(0x1);
  ram[Misc_Y_HighPos + y] = a; // set vertical high byte
  ram[Misc_State + y] = a; // set state for misc object
  ram[Square2SoundQueue] = a; // load coin grab sound
  ram[ObjectOffset] = x; // store current control bit as misc object offset 
  cpu_call_begin(0xbb7f); GiveOneCoin(); cpu_call_end(); // update coin tally on the screen and coin amount variable
  inc_abs_fzn(CoinTallyFor1Ups); // increment coin tally used to activate 1-up block flag
  return;
}

void FindEmptyMiscSlot(void) {
  ldy_imm(0x8); // start at end of misc objects buffer
  
FMiscLoop:
  lda_zpy_fzn(Misc_State); // get misc object state
  // branch if none found to use current offset
  if (!zero_flag) {
    dey(); // decrement offset
    cpy_imm_fcz(0x5); // do this for three slots
    if (!zero_flag) { goto FMiscLoop; } // do this until all slots are checked
    ldy_imm_fzn(0x8); // if no empty slots found, use last slot
  }
  // UseMiscS:
  ram[JumpCoinMiscOffset] = y; // store offset of misc object buffer here (residual)
  return;
  // -------------------------------------------------------------------------------------
}

void MiscObjectsCore(void) {
  // MiscObjectsCore:
  ldx_imm(0x8); // set at end of misc object buffer
  
MiscLoop:
  ram[ObjectOffset] = x; // store misc object offset here
  lda_zpx_fz(Misc_State); // check misc object state
  if (zero_flag) { goto MiscLoopBack; } // branch to check next slot
  asl_acc_fczn(); // otherwise shift d7 into carry
  if (!carry_flag) { goto ProcJumpCoin; } // if d7 not set, jumping coin, thus skip to rest of code here
  cpu_call_begin(0xbba3); ProcHammerObj(); cpu_call_end(); // otherwise go to process hammer,
  goto MiscLoopBack; // then check next slot
  // --------------------------------
  // $00 - used to set downward force
  // $01 - used to set upward force (residual)
  // $02 - used to set maximum speed
  
ProcJumpCoin:
  ldy_zpx(Misc_State); // check misc object state
  dey_fz(); // decrement to see if it's set to 1
  if (zero_flag) { goto JCoinRun; } // if so, branch to handle jumping coin
  inc_zpx(Misc_State); // otherwise increment state to either start off or as timer
  lda_zpx(Misc_X_Position); // get horizontal coordinate for misc object
  carry_flag = false; // whether its jumping coin (state 0 only) or floatey number
  adc_abs_fc(ScrollAmount); // add current scroll speed
  ram[Misc_X_Position + x] = a; // store as new horizontal coordinate
  lda_zpx(Misc_PageLoc); // get page location
  adc_imm(0x0); // add carry
  ram[Misc_PageLoc + x] = a; // store as new page location
  lda_zpx(Misc_State);
  cmp_imm_fczn(0x30); // check state of object for preset value
  if (!zero_flag) { goto RunJCSubs; } // if not yet reached, branch to subroutines
  lda_imm(0x0);
  ram[Misc_State + x] = a; // otherwise nullify object state
  goto MiscLoopBack; // and move onto next slot
  
JCoinRun:
  txa();
  carry_flag = false; // add 13 bytes to offset for next subroutine
  adc_imm(0xd);
  tax();
  lda_imm(0x50); // set downward movement amount
  ram[0x0] = a;
  lda_imm(0x6); // set maximum vertical speed
  ram[0x2] = a;
  lsr_acc_fc(); // divide by 2 and set
  ram[0x1] = a; // as upward movement amount (apparently residual)
  lda_imm_fzn(0x0); // set A to impose gravity on jumping coin
  cpu_call_begin(0xbbdd); ImposeGravity(); cpu_call_end(); // do sub to move coin vertically and impose gravity on it
  ldx_zp(ObjectOffset); // get original misc object offset
  lda_zpx(Misc_Y_Speed); // check vertical speed
  cmp_imm_fczn(0x5);
  if (!zero_flag) { goto RunJCSubs; } // if not moving downward fast enough, keep state as-is
  inc_zpx_fzn(Misc_State); // otherwise increment state to change to floatey number
  
RunJCSubs:
  cpu_call_begin(0xbbea); RelativeMiscPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xbbed); GetMiscOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xbbf0); GetMiscBoundBox(); cpu_call_end(); // get bounding box coordinates (why?)
  cpu_call_begin(0xbbf3); JCoinGfxHandler(); cpu_call_end(); // draw the coin or floatey number
  
MiscLoopBack:
  dex_fzn(); // decrement misc object offset
  if (!neg_flag) { goto MiscLoop; } // loop back until all misc objects handled
  return; // then leave
  // -------------------------------------------------------------------------------------
}

void GiveOneCoin(void) {
  lda_imm(0x1); // set digit modifier to add 1 coin
  ram[DigitModifier + 5] = a; // to the current player's coin tally
  ldx_abs(CurrentPlayer); // get current player on the screen
  ldy_absx_fzn(CoinTallyOffsets); // get offset for player's coin tally
  cpu_call_begin(0xbc0b); DigitsMathRoutine(); cpu_call_end(); // update the coin tally
  inc_abs(CoinTally); // increment onscreen player's coin amount
  lda_abs(CoinTally);
  cmp_imm_fcz(100); // does player have 100 coins yet?
  // if not, skip all of this
  if (zero_flag) {
    lda_imm(0x0);
    ram[CoinTally] = a; // otherwise, reinitialize coin amount
    inc_abs(NumberofLives); // give the player an extra life
    lda_imm(Sfx_ExtraLife);
    ram[Square2SoundQueue] = a; // play 1-up sound
  }
  // CoinPoints:
  lda_imm(0x2); // set digit modifier to award
  ram[DigitModifier + 4] = a; // 200 points to the player
  AddToScore(); // fallthrough
  return;
}

void AddToScore(void) {
  ldx_abs(CurrentPlayer); // get current player
  ldy_absx_fzn(ScoreOffsets); // get offset for player's score
  cpu_call_begin(0xbc2f); DigitsMathRoutine(); cpu_call_end(); // update the score internally with value in digit modifier
  GetSBNybbles(); // fallthrough
  return;
}

void GetSBNybbles(void) {
  ldy_abs(CurrentPlayer); // get current player
  lda_absy_fzn(StatusBarNybbles); // get nybbles based on player, use to update score and coins
  UpdateNumber(); // fallthrough
  return;
}

void UpdateNumber(void) {
  cpu_call_begin(0xbc38); PrintStatusBarNumbers(); cpu_call_end(); // print status bar numbers based on nybbles, whatever they be
  ldy_abs(VRAM_Buffer1_Offset);
  lda_absy_fz(VRAM_Buffer1 - 6); // check highest digit of score
  // if zero, overwrite with space tile for zero suppression
  if (zero_flag) {
    lda_imm(0x24);
    ram[VRAM_Buffer1 - 6 + y] = a;
  }
  // NoZSup:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset
  return;
  // -------------------------------------------------------------------------------------
}

void PwrUpJmp(void) {
  lda_imm(0x1); // this is a residual jump point in enemy object jump table
  ram[Enemy_State + 5] = a; // set power-up object's state
  ram[Enemy_Flag + 5] = a; // set buffer flag
  lda_imm(0x3);
  ram[Enemy_BoundBoxCtrl + 5] = a; // set bounding box size control for power-up object
  lda_zp(PowerUpType);
  cmp_imm_fc(0x2); // check currently loaded power-up type
  // if star or 1-up, branch ahead
  if (!carry_flag) {
    lda_abs(PlayerStatus); // otherwise check player's current status
    cmp_imm_fc(0x2);
    // if player not fiery, use status as power-up type
    if (carry_flag) {
      lsr_acc_fc(); // otherwise shift right to force fire flower type
    }
    // StrType:
    ram[PowerUpType] = a; // store type here
  }
  // PutBehind:
  lda_imm(0b00100000);
  ram[Enemy_SprAttrib + 5] = a; // set background priority bit
  lda_imm_fzn(Sfx_GrowPowerUp);
  ram[Square2SoundQueue] = a; // load power-up reveal sound and leave
  return;
  // -------------------------------------------------------------------------------------
}

void PowerUpObjHandler(void) {
  // PowerUpObjHandler:
  ldx_imm(0x5); // set object offset for last slot in enemy object buffer
  ram[ObjectOffset] = x;
  lda_zp_fzn(Enemy_State + 5); // check power-up object's state
  if (zero_flag) { return; } // if not set, branch to leave
  asl_acc_fc(); // shift to check if d7 was set in object state
  if (!carry_flag) { goto GrowThePowerUp; } // if not set, branch ahead to skip this part
  lda_abs_fzn(TimerControl); // if master timer control set,
  if (!zero_flag) { goto RunPUSubs; } // branch ahead to enemy object routines
  lda_zp_fzn(PowerUpType); // check power-up type
  if (zero_flag) { goto ShroomM; } // if normal mushroom, branch ahead to move it
  cmp_imm_fczn(0x3);
  if (zero_flag) { goto ShroomM; } // if 1-up mushroom, branch ahead to move it
  cmp_imm_fczn(0x2);
  if (!zero_flag) { goto RunPUSubs; } // if not star, branch elsewhere to skip movement
  cpu_call_begin(0xbca3); MoveJumpingEnemy(); cpu_call_end(); // otherwise impose gravity on star power-up and make it jump
  cpu_call_begin(0xbca6); EnemyJump(); cpu_call_end(); // note that green paratroopa shares the same code here 
  goto RunPUSubs; // then jump to other power-up subroutines
  
ShroomM:
  cpu_call_begin(0xbcac); MoveNormalEnemy(); cpu_call_end(); // do sub to make mushrooms move
  cpu_call_begin(0xbcaf); EnemyToBGCollisionDet(); cpu_call_end(); // deal with collisions
  goto RunPUSubs; // run the other subroutines
  
GrowThePowerUp:
  lda_zp(FrameCounter); // get frame counter
  and_imm_fz(0x3); // mask out all but 2 LSB
  if (!zero_flag) { goto ChkPUSte; } // if any bits set here, branch
  dec_zp(Enemy_Y_Position + 5); // otherwise decrement vertical coordinate slowly
  lda_zp(Enemy_State + 5); // load power-up object state
  inc_zp(Enemy_State + 5); // increment state for next frame (to make power-up rise)
  cmp_imm_fc(0x11); // if power-up object state not yet past 16th pixel,
  if (!carry_flag) { goto ChkPUSte; } // branch ahead to last part here
  lda_imm(0x10);
  ram[Enemy_X_Speed + x] = a; // otherwise set horizontal speed
  lda_imm(0b10000000);
  ram[Enemy_State + 5] = a; // and then set d7 in power-up object's state
  asl_acc_fc(); // shift once to init A
  ram[Enemy_SprAttrib + 5] = a; // initialize background priority bit set here
  rol_acc(); // rotate A to set right moving direction
  ram[Enemy_MovingDir + x] = a; // set moving direction
  
ChkPUSte:
  lda_zp(Enemy_State + 5); // check power-up object's state
  cmp_imm_fczn(0x6); // for if power-up has risen enough
  if (!carry_flag) { return; } // if not, don't even bother running these routines
  
RunPUSubs:
  cpu_call_begin(0xbcda); RelativeEnemyPosition(); cpu_call_end(); // get coordinates relative to screen
  cpu_call_begin(0xbcdd); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen bits
  cpu_call_begin(0xbce0); GetEnemyBoundBox(); cpu_call_end(); // get bounding box coordinates
  cpu_call_begin(0xbce3); DrawPowerUp(); cpu_call_end(); // draw the power-up object
  cpu_call_begin(0xbce6); PlayerEnemyCollision(); cpu_call_end(); // check for collision with player
  cpu_call_begin(0xbce9); OffscreenBoundsCheck(); cpu_call_end(); // check to see if it went offscreen
  // ExitPUp:
  return; // and we're done
  // -------------------------------------------------------------------------------------
  // These apply to all routines in this section unless otherwise noted:
  // $00 - used to store metatile from block buffer routine
  // $02 - used to store vertical high nybble offset from block buffer routine
  // $05 - used to store metatile stored in A at beginning of PlayerHeadCollision
  // $06-$07 - used as block buffer address indirect
}

void PlayerHeadCollision(void) {
  // PlayerHeadCollision:
  pha(); // store metatile number to stack
  lda_imm(0x11); // load unbreakable block object state by default
  ldx_abs(SprDataOffset_Ctrl); // load offset control bit here
  ldy_abs_fzn(PlayerSize); // check player's size
  if (!zero_flag) { goto DBlockSte; } // if small, branch
  lda_imm_fzn(0x12); // otherwise load breakable block object state
  
DBlockSte:
  ram[Block_State + x] = a; // store into block object buffer
  cpu_call_begin(0xbcfe); DestroyBlockMetatile(); cpu_call_end(); // store blank metatile in vram buffer to write to name table
  ldx_abs(SprDataOffset_Ctrl); // load offset control bit
  lda_zp(0x2); // get vertical high nybble offset used in block buffer routine
  ram[Block_Orig_YPos + x] = a; // set as vertical coordinate for block object
  tay();
  lda_zp(0x6); // get low byte of block buffer address used in same routine
  ram[Block_BBuf_Low + x] = a; // save as offset here to be used later
  lda_indy_fzn(0x6); // get contents of block buffer at old address at $06, $07
  cpu_call_begin(0xbd11); BlockBumpedChk(); cpu_call_end(); // do a sub to check which block player bumped head on
  ram[0x0] = a; // store metatile here
  ldy_abs_fzn(PlayerSize); // check player's size
  if (!zero_flag) { goto ChkBrick; } // if small, use metatile itself as contents of A
  tya_fzn(); // otherwise init A (note: big = 0)
  
ChkBrick:
  if (!carry_flag) { goto PutMTileB; } // if no match was found in previous sub, skip ahead
  ldy_imm(0x11); // otherwise load unbreakable state into block object buffer
  ram[Block_State + x] = y; // note this applies to both player sizes
  lda_imm(0xc4); // load empty block metatile into A for now
  ldy_zp(0x0); // get metatile from before
  cpy_imm_fcz(0x58); // is it brick with coins (with line)?
  if (zero_flag) { goto StartBTmr; } // if so, branch
  cpy_imm_fczn(0x5d); // is it brick with coins (without line)?
  if (!zero_flag) { goto PutMTileB; } // if not, branch ahead to store empty block metatile
  
StartBTmr:
  lda_abs_fz(BrickCoinTimerFlag); // check brick coin timer flag
  if (!zero_flag) { goto ContBTmr; } // if set, timer expired or counting down, thus branch
  lda_imm(0xb);
  ram[BrickCoinTimer] = a; // if not set, set brick coin timer
  inc_abs(BrickCoinTimerFlag); // and set flag linked to it
  
ContBTmr:
  lda_abs_fz(BrickCoinTimer); // check brick coin timer
  if (!zero_flag) { goto PutOldMT; } // if not yet expired, branch to use current metatile
  ldy_imm(0xc4); // otherwise use empty block metatile
  
PutOldMT:
  tya_fzn(); // put metatile into A
  
PutMTileB:
  ram[Block_Metatile + x] = a; // store whatever metatile be appropriate here
  cpu_call_begin(0xbd46); InitBlock_XY_Pos(); cpu_call_end(); // get block object horizontal coordinates saved
  ldy_zp(0x2); // get vertical high nybble offset
  lda_imm(0x23);
  dynamic_ram_write(read_word(0x6) + y, a); // write blank metatile $23 to block buffer
  lda_imm(0x10);
  ram[BlockBounceTimer] = a; // set block bounce timer
  pla(); // pull original metatile from stack
  ram[0x5] = a; // and save here
  ldy_imm(0x0); // set default offset
  lda_abs_fz(CrouchingFlag); // is player crouching?
  if (!zero_flag) { goto SmallBP; } // if so, branch to increment offset
  lda_abs_fz(PlayerSize); // is player big?
  if (zero_flag) { goto BigBP; } // if so, branch to use default offset
  
SmallBP:
  iny(); // increment for small or big and crouching
  
BigBP:
  lda_zp(Player_Y_Position); // get player's vertical coordinate
  carry_flag = false;
  adc_absy(BlockYPosAdderData); // add value determined by size
  and_imm(0xf0); // mask out low nybble to get 16-pixel correspondence
  ram[Block_Y_Position + x] = a; // save as vertical coordinate for block object
  ldy_zpx(Block_State); // get block object state
  cpy_imm_fczn(0x11);
  if (zero_flag) { goto Unbreak; } // if set to value loaded for unbreakable, branch
  cpu_call_begin(0xbd74); BrickShatter(); cpu_call_end(); // execute code for breakable brick
  goto InvOBit; // skip subroutine to do last part of code here
  
Unbreak:
  cpu_call_begin(0xbd7a); BumpBlock(); cpu_call_end(); // execute code for unbreakable brick or question block
  
InvOBit:
  lda_abs(SprDataOffset_Ctrl); // invert control bit used by block objects
  eor_imm_fzn(0x1); // and floatey numbers
  ram[SprDataOffset_Ctrl] = a;
  return; // leave!
  // --------------------------------
}

void InitBlock_XY_Pos(void) {
  lda_zp(Player_X_Position); // get player's horizontal coordinate
  carry_flag = false;
  adc_imm_fc(0x8); // add eight pixels
  and_imm(0xf0); // mask out low nybble to give 16-pixel correspondence
  ram[Block_X_Position + x] = a; // save as horizontal coordinate for block object
  lda_zp(Player_PageLoc);
  adc_imm_fc(0x0); // add carry to page location of player
  ram[Block_PageLoc + x] = a; // save as page location of block object
  ram[Block_PageLoc2 + x] = a; // save elsewhere to be used later
  lda_zp_fzn(Player_Y_HighPos);
  ram[Block_Y_HighPos + x] = a; // save vertical high byte of player into
  return; // vertical high byte of block object and leave
  // --------------------------------
}

void BumpBlock(void) {
  cpu_call_begin(0xbd9d); CheckTopOfBlock(); cpu_call_end(); // check to see if there's a coin directly above this block
  lda_imm(Sfx_Bump);
  ram[Square1SoundQueue] = a; // play bump sound
  lda_imm(0x0);
  ram[Block_X_Speed + x] = a; // initialize horizontal speed for block object
  ram[Block_Y_MoveForce + x] = a; // init fractional movement force
  ram[Player_Y_Speed] = a; // init player's vertical speed
  lda_imm(0xfe);
  ram[Block_Y_Speed + x] = a; // set vertical speed for block object
  lda_zp_fzn(0x5); // get original metatile from stack
  cpu_call_begin(0xbdb3); BlockBumpedChk(); cpu_call_end(); // do a sub to check which block player bumped head on
  if (carry_flag) {
    tya(); // move block number to A
    cmp_imm_fczn(0x9); // if block number was within 0-8 range,
    // branch to use current number
    if (carry_flag) {
      sbc_imm_fczn(0x5); // otherwise subtract 5 for second set to get proper number
    }
    // BlockCode:
    cpu_call_begin(0xbdbf);
    asl_acc_fczn();
    tay_fzn();
    pla_fzn();
    ram[0x4] = a;
    pla_fzn();
    ram[0x5] = a;
    iny_fzn();
    lda_indy_fzn(0x4);
    ram[0x6] = a;
    iny_fzn();
    lda_indy_fzn(0x4);
    ram[0x7] = a;
    switch (read_word(0x6)) {
      case 0xbdd2: MushFlowerBlock(); return;
      case 0xbb38: CoinBlock(); return;
      case 0xbdd8: ExtraLifeMushBlock(); return;
      case 0xbddf: VineBlock(); return;
      case 0xbdd5: StarBlock(); return;
      default: cpu_unresolved_jump(read_word(0x6)); return;
    }
  }
}

void MushFlowerBlock(void) {
  goto MushFlowerBlock;
  
SetupPowerUp:
  lda_imm(PowerUpObject); // load power-up identifier into
  ram[Enemy_ID + 5] = a; // special use slot of enemy object buffer
  lda_zpx(Block_PageLoc); // store page location of block object
  ram[Enemy_PageLoc + 5] = a; // as page location of power-up object
  lda_zpx(Block_X_Position); // store horizontal coordinate of block object
  ram[Enemy_X_Position + 5] = a; // as horizontal coordinate of power-up object
  lda_imm(0x1);
  ram[Enemy_Y_HighPos + 5] = a; // set vertical high byte of power-up object
  lda_zpx(Block_Y_Position); // get vertical coordinate of block object
  carry_flag = true;
  sbc_imm(0x8); // subtract 8 pixels
  ram[Enemy_Y_Position + 5] = a; // and use as vertical coordinate of power-up object
  PwrUpJmp(); // fallthrough
  return;
  
MushFlowerBlock:
  lda_imm(0x0); // load mushroom/fire flower into power-up type
  // loc_48596:
  bit_abs(0x2a9);
  goto loc_48599; // BIT instruction opcode
  
loc_48599:
  bit_abs(0x3a9);
  goto loc_48602; // BIT instruction opcode
  
loc_48602:
  ram[0x39] = a; // store correct power-up type
  goto SetupPowerUp;
}

void StarBlock(void) {
  goto StarBlock;
  
SetupPowerUp:
  lda_imm(PowerUpObject); // load power-up identifier into
  ram[Enemy_ID + 5] = a; // special use slot of enemy object buffer
  lda_zpx(Block_PageLoc); // store page location of block object
  ram[Enemy_PageLoc + 5] = a; // as page location of power-up object
  lda_zpx(Block_X_Position); // store horizontal coordinate of block object
  ram[Enemy_X_Position + 5] = a; // as horizontal coordinate of power-up object
  lda_imm(0x1);
  ram[Enemy_Y_HighPos + 5] = a; // set vertical high byte of power-up object
  lda_zpx(Block_Y_Position); // get vertical coordinate of block object
  carry_flag = true;
  sbc_imm(0x8); // subtract 8 pixels
  ram[Enemy_Y_Position + 5] = a; // and use as vertical coordinate of power-up object
  PwrUpJmp(); // fallthrough
  return;
  
StarBlock:
  lda_imm(0x2); // load star into power-up type
  // loc_48599:
  bit_abs(0x3a9);
  goto loc_48602; // BIT instruction opcode
  
loc_48602:
  ram[0x39] = a; // store correct power-up type
  goto SetupPowerUp;
}

void ExtraLifeMushBlock(void) {
  goto ExtraLifeMushBlock;
  
SetupPowerUp:
  lda_imm(PowerUpObject); // load power-up identifier into
  ram[Enemy_ID + 5] = a; // special use slot of enemy object buffer
  lda_zpx(Block_PageLoc); // store page location of block object
  ram[Enemy_PageLoc + 5] = a; // as page location of power-up object
  lda_zpx(Block_X_Position); // store horizontal coordinate of block object
  ram[Enemy_X_Position + 5] = a; // as horizontal coordinate of power-up object
  lda_imm(0x1);
  ram[Enemy_Y_HighPos + 5] = a; // set vertical high byte of power-up object
  lda_zpx(Block_Y_Position); // get vertical coordinate of block object
  carry_flag = true;
  sbc_imm(0x8); // subtract 8 pixels
  ram[Enemy_Y_Position + 5] = a; // and use as vertical coordinate of power-up object
  PwrUpJmp(); // fallthrough
  return;
  
ExtraLifeMushBlock:
  lda_imm(0x3); // load 1-up mushroom into power-up type
  // loc_48602:
  ram[0x39] = a; // store correct power-up type
  goto SetupPowerUp;
}

void VineBlock(void) {
  ldx_imm(0x5); // load last slot for enemy object buffer
  ldy_abs_fzn(SprDataOffset_Ctrl); // get control bit
  cpu_call_begin(0xbde6); Setup_Vine(); cpu_call_end(); // set up vine object
  // ExitBlockChk:
  return; // leave
  // --------------------------------
}

void BlockBumpedChk(void) {
  ldy_imm(0xd); // start at end of metatile data
  
BumpChkLoop:
  cmp_absy_fczn(BrickQBlockMetatiles); // check to see if current metatile matches
  if (!zero_flag) {
    dey_fzn(); // otherwise move onto next metatile
    if (!neg_flag) { goto BumpChkLoop; } // do this until all metatiles are checked
    carry_flag = false; // if none match, return with carry clear
    // MatchBump:
    return; // note carry is set if found match
    // --------------------------------
  }
}

void BrickShatter(void) {
  cpu_call_begin(0xbe04); CheckTopOfBlock(); cpu_call_end(); // check to see if there's a coin directly above this block
  lda_imm_fzn(Sfx_BrickShatter);
  ram[Block_RepFlag + x] = a; // set flag for block object to immediately replace metatile
  ram[NoiseSoundQueue] = a; // load brick shatter sound
  cpu_call_begin(0xbe0e); SpawnBrickChunks(); cpu_call_end(); // create brick chunk objects
  lda_imm(0xfe);
  ram[Player_Y_Speed] = a; // set vertical speed for player
  lda_imm_fzn(0x5);
  ram[DigitModifier + 5] = a; // set digit modifier to give player 50 points
  cpu_call_begin(0xbe1a); AddToScore(); cpu_call_end(); // do sub to update the score
  ldx_abs_fzn(SprDataOffset_Ctrl); // load control bit and leave
  return;
  // --------------------------------
}

void CheckTopOfBlock(void) {
  // CheckTopOfBlock:
  ldx_abs(SprDataOffset_Ctrl); // load control bit
  ldy_zp_fzn(0x2); // get vertical high nybble offset used in block buffer
  if (zero_flag) { return; } // branch to leave if set to zero, because we're at the top
  tya(); // otherwise set to A
  carry_flag = true;
  sbc_imm(0x10); // subtract $10 to move up one row in the block buffer
  ram[0x2] = a; // store as new vertical high nybble offset
  tay();
  lda_indy(0x6); // get contents of block buffer in same column, one row up
  cmp_imm_fczn(0xc2); // is it a coin? (not underwater)
  if (!zero_flag) { return; } // if not, branch to leave
  lda_imm_fzn(0x0);
  dynamic_ram_write(read_word(0x6) + y, a); // otherwise put blank metatile where coin was
  cpu_call_begin(0xbe39); RemoveCoin_Axe(); cpu_call_end(); // write blank metatile to vram buffer
  ldx_abs_fzn(SprDataOffset_Ctrl); // get control bit
  cpu_call_begin(0xbe3f); SetupJumpCoin(); cpu_call_end(); // create jumping coin object and update coin variables
  // TopEx:
  return; // leave!
  // --------------------------------
}

void SpawnBrickChunks(void) {
  lda_zpx(Block_X_Position); // set horizontal coordinate of block object
  ram[Block_Orig_XPos + x] = a; // as original horizontal coordinate here
  lda_imm(0xf0);
  ram[Block_X_Speed + x] = a; // set horizontal speed for brick chunk objects
  ram[Block_X_Speed + 2 + x] = a;
  lda_imm(0xfa);
  ram[Block_Y_Speed + x] = a; // set vertical speed for one
  lda_imm(0xfc);
  ram[Block_Y_Speed + 2 + x] = a; // set lower vertical speed for the other
  lda_imm(0x0);
  ram[Block_Y_MoveForce + x] = a; // init fractional movement force for both
  ram[Block_Y_MoveForce + 2 + x] = a;
  lda_zpx(Block_PageLoc);
  ram[Block_PageLoc + 2 + x] = a; // copy page location
  lda_zpx(Block_X_Position);
  ram[Block_X_Position + 2 + x] = a; // copy horizontal coordinate
  lda_zpx(Block_Y_Position);
  carry_flag = false; // add 8 pixels to vertical coordinate
  adc_imm_fc(0x8); // and save as vertical coordinate for one of them
  ram[Block_Y_Position + 2 + x] = a;
  lda_imm_fzn(0xfa);
  ram[Block_Y_Speed + x] = a; // set vertical speed...again??? (redundant)
  return;
  // -------------------------------------------------------------------------------------
}

void BlockObjectsCore(void) {
  // BlockObjectsCore:
  lda_zpx_fzn(Block_State); // get state of block object
  if (zero_flag) { goto UpdSte; } // if not set, branch to leave
  and_imm(0xf); // mask out high nybble
  pha(); // push to stack
  tay(); // put in Y for now
  txa();
  carry_flag = false;
  adc_imm_fc(0x9); // add 9 bytes to offset (note two block objects are created
  tax(); // when using brick chunks, but only one offset for both)
  dey_fzn(); // decrement Y to check for solid block state
  if (zero_flag) { goto BouncingBlockHandler; } // branch if found, otherwise continue for brick chunks
  cpu_call_begin(0xbe82); ImposeGravityBlock(); cpu_call_end(); // do sub to impose gravity on one block object object
  cpu_call_begin(0xbe85); MoveObjectHorizontally(); cpu_call_end(); // do another sub to move horizontally
  txa();
  carry_flag = false; // move onto next block object
  adc_imm_fc(0x2);
  tax_fzn();
  cpu_call_begin(0xbe8d); ImposeGravityBlock(); cpu_call_end(); // do sub to impose gravity on other block object
  cpu_call_begin(0xbe90); MoveObjectHorizontally(); cpu_call_end(); // do another sub to move horizontally
  ldx_zp_fzn(ObjectOffset); // get block object offset used for both
  cpu_call_begin(0xbe95); RelativeBlockPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xbe98); GetBlockOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xbe9b); DrawBrickChunks(); cpu_call_end(); // draw the brick chunks
  pla(); // get lower nybble of saved state
  ldy_zpx_fzn(Block_Y_HighPos); // check vertical high byte of block object
  if (zero_flag) { goto UpdSte; } // if above the screen, branch to kill it
  pha(); // otherwise save state back into stack
  lda_imm(0xf0);
  cmp_zpx_fc(Block_Y_Position + 2); // check to see if bottom block object went
  if (carry_flag) { goto ChkTop; } // to the bottom of the screen, and branch if not
  ram[Block_Y_Position + 2 + x] = a; // otherwise set offscreen coordinate
  
ChkTop:
  lda_zpx(Block_Y_Position); // get top block object's vertical coordinate
  cmp_imm_fc(0xf0); // see if it went to the bottom of the screen
  pla_fzn(); // pull block object state from stack
  if (!carry_flag) { goto UpdSte; } // if not, branch to save state
  if (carry_flag) { goto KillBlock; } // otherwise do unconditional branch to kill it
  
BouncingBlockHandler:
  cpu_call_begin(0xbeb5); ImposeGravityBlock(); cpu_call_end(); // do sub to impose gravity on block object
  ldx_zp_fzn(ObjectOffset); // get block object offset
  cpu_call_begin(0xbeba); RelativeBlockPosition(); cpu_call_end(); // get relative coordinates
  cpu_call_begin(0xbebd); GetBlockOffscreenBits(); cpu_call_end(); // get offscreen information
  cpu_call_begin(0xbec0); DrawBlock(); cpu_call_end(); // draw the block
  lda_zpx(Block_Y_Position); // get vertical coordinate
  and_imm(0xf); // mask out high nybble
  cmp_imm_fc(0x5); // check to see if low nybble wrapped around
  pla_fzn(); // pull state from stack
  if (carry_flag) { goto UpdSte; } // if still above amount, not time to kill block yet, thus branch
  lda_imm(0x1);
  ram[Block_RepFlag + x] = a; // otherwise set flag to replace metatile
  
KillBlock:
  lda_imm_fzn(0x0); // if branched here, nullify object state
  
UpdSte:
  ram[Block_State + x] = a; // store contents of A in block object state
  return;
  // -------------------------------------------------------------------------------------
  // $02 - used to store offset to block buffer
  // $06-$07 - used to store block buffer address
}

void BlockObjMT_Updater(void) {
  ldx_imm(0x1); // set offset to start with second block object
  
UpdateLoop:
  ram[ObjectOffset] = x; // set offset here
  lda_abs_fz(VRAM_Buffer1); // if vram buffer already being used here,
  // branch to move onto next block object
  if (zero_flag) {
    lda_absx_fz(Block_RepFlag); // if flag for block object already clear,
    // branch to move onto next block object
    if (!zero_flag) {
      lda_absx(Block_BBuf_Low); // get low byte of block buffer
      ram[0x6] = a; // store into block buffer address
      lda_imm(0x5);
      ram[0x7] = a; // set high byte of block buffer address
      lda_absx(Block_Orig_YPos); // get original vertical coordinate of block object
      ram[0x2] = a; // store here and use as offset to block buffer
      tay();
      lda_absx_fzn(Block_Metatile); // get metatile to be written
      dynamic_ram_write(read_word(0x6) + y, a); // write it to the block buffer
      cpu_call_begin(0xbef8); ReplaceBlockMetatile(); cpu_call_end(); // do sub to replace metatile where block object is
      lda_imm(0x0);
      ram[Block_RepFlag + x] = a; // clear block object flag
    }
  }
  // NextBUpd:
  dex_fzn(); // decrement block object offset
  if (!neg_flag) { goto UpdateLoop; } // do this until both block objects are dealt with
  return; // then leave
  // -------------------------------------------------------------------------------------
  // $00 - used to store high nybble of horizontal speed as adder
  // $01 - used to store low nybble of horizontal speed
  // $02 - used to store adder to page location
}

void MoveEnemyHorizontally(void) {
  inx_fzn(); // increment offset for enemy offset
  cpu_call_begin(0xbf05); MoveObjectHorizontally(); cpu_call_end(); // position object horizontally according to
  ldx_zp_fzn(ObjectOffset); // counters, return with saved value in A,
  return; // put enemy offset back in X and leave
}

void MovePlayerHorizontally(void) {
  lda_abs_fzn(JumpspringAnimCtrl); // if jumpspring currently animating,
  if (zero_flag) {
    tax(); // otherwise set zero for offset to use player's stuff
    MoveObjectHorizontally(); // fallthrough
    return;
  }
}

void MoveObjectHorizontally(void) {
  lda_zpx(SprObject_X_Speed); // get currently saved value (horizontal
  asl_acc(); // speed, secondary counter, whatever)
  asl_acc(); // and move low nybble to high
  asl_acc();
  asl_acc();
  ram[0x1] = a; // store result here
  lda_zpx(SprObject_X_Speed); // get saved value again
  lsr_acc(); // move high nybble to low
  lsr_acc();
  lsr_acc();
  lsr_acc();
  cmp_imm_fc(0x8); // if < 8, branch, do not change
  if (carry_flag) {
    ora_imm(0b11110000); // otherwise alter high nybble
  }
  // SaveXSpd:
  ram[0x0] = a; // save result here
  ldy_imm(0x0); // load default Y value here
  cmp_imm_fn(0x0); // if result positive, leave Y alone
  if (neg_flag) {
    dey(); // otherwise decrement Y
  }
  // UseAdder:
  ram[0x2] = y; // save Y here
  lda_absx(SprObject_X_MoveForce); // get whatever number's here
  carry_flag = false;
  adc_zp_fc(0x1); // add low nybble moved to high
  ram[SprObject_X_MoveForce + x] = a; // store result here
  lda_imm(0x0); // init A
  rol_acc_fc(); // rotate carry into d0
  pha(); // push onto stack
  ror_acc_fc(); // rotate d0 back onto carry
  lda_zpx(SprObject_X_Position);
  adc_zp_fc(0x0); // add carry plus saved value (high nybble moved to low
  ram[SprObject_X_Position + x] = a; // plus $f0 if necessary) to object's horizontal position
  lda_zpx(SprObject_PageLoc);
  adc_zp(0x2); // add carry plus other saved value to the
  ram[SprObject_PageLoc + x] = a; // object's page location and save
  pla();
  carry_flag = false; // pull old carry from stack and add
  adc_zp_fczn(0x0); // to high nybble moved to low
  // ExXMove:
  return; // and leave
  // -------------------------------------------------------------------------------------
  // $00 - used for downward force
  // $01 - used for upward force
  // $02 - used for maximum vertical speed
}

void MoveD_EnemyVertically(void) {
  ldy_imm(0x3d); // set quick movement amount downwards
  lda_zpx(Enemy_State); // then check enemy state
  cmp_imm_fcz(0x5); // if not set to unique state for spiny's egg, go ahead
  // and use, otherwise set different movement amount, continue on
  if (zero_flag) {
    MoveFallingPlatform(); // fallthrough
    return;
  }
  // ContVMove:
  goto SetHiMax; // jump to skip the rest of this
  // --------------------------------
  
SetHiMax:
  lda_imm(0x3); // set maximum speed in A
  SetXMoveAmt(); // fallthrough
  return;
}

void MoveFallingPlatform(void) {
  ldy_imm(0x20); // set movement amount
  // ContVMove:
  goto SetHiMax; // jump to skip the rest of this
  // --------------------------------
  
SetHiMax:
  lda_imm(0x3); // set maximum speed in A
  SetXMoveAmt(); // fallthrough
  return;
}

void MoveDropPlatform(void) {
  ldy_imm(0x7f); // set movement amount for drop platform
  goto SetMdMax; // skip ahead of other value set here
  
SetMdMax:
  lda_imm(0x2); // set maximum speed in A
  SetXMoveAmt(); return; // unconditional branch
  // --------------------------------
}

void MoveEnemySlowVert(void) {
  ldy_imm(0xf); // set movement amount for bowser/other objects
  // SetMdMax:
  lda_imm(0x2); // set maximum speed in A
  SetXMoveAmt(); return; // unconditional branch
  // --------------------------------
}

void MoveJ_EnemyVertically(void) {
  ldy_imm(0x1c); // set movement amount for podoboo/other objects
  // SetHiMax:
  lda_imm(0x3); // set maximum speed in A
  SetXMoveAmt(); // fallthrough
  return;
}

void SetXMoveAmt(void) {
  ram[0x0] = y; // set movement amount here
  inx_fzn(); // increment X for enemy offset
  cpu_call_begin(0xbf9b); ImposeGravitySprObj(); cpu_call_end(); // do a sub to move enemy object downwards
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset and leave
  return;
  // --------------------------------
}

void ImposeGravityBlock(void) {
  ldy_imm(0x1); // set offset for maximum speed
  lda_imm(0x50); // set movement amount here
  ram[0x0] = a;
  lda_absy(MaxSpdBlockData); // get maximum speed
  ImposeGravitySprObj(); // fallthrough
  return;
}

void ImposeGravitySprObj(void) {
  ram[0x2] = a; // set maximum speed here
  lda_imm(0x0); // set value to move downwards
  ImposeGravity(); return; // jump to the code that actually moves it
  // --------------------------------
}

void MovePlatformDown(void) {
  lda_imm(0x0); // save value to stack (if branching here, execute next
  // loc_49078:
  bit_abs(0x1a9);
  goto loc_49081; // part as BIT instruction)
  
loc_49081:
  pha();
  ldy_zpx(Enemy_ID); // get enemy object identifier
  inx(); // increment offset for enemy object
  lda_imm(0x5); // load default value here
  cpy_imm_fcz(0x29); // residual comparison, object #29 never executes
  // this code, thus unconditional branch here
  if (zero_flag) {
    lda_imm(0x9); // residual code
  }
  // SetDplSpd:
  ram[0x0] = a; // save downward movement amount here
  lda_imm(0xa); // save upward movement amount here
  ram[0x1] = a;
  lda_imm(0x3); // save maximum vertical speed here
  ram[0x2] = a;
  pla(); // get value from stack
  tay_fzn(); // use as Y, then move onto code shared by red koopa
  // RedPTroopaGrav:
  cpu_call_begin(0xbfd3); ImposeGravity(); cpu_call_end(); // do a sub to move object gradually
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used for downward force
  // $01 - used for upward force
  // $07 - used as adder for vertical position
}

void MovePlatformUp(void) {
  lda_imm(0x1); // save value to stack
  // loc_49081:
  pha();
  ldy_zpx(Enemy_ID); // get enemy object identifier
  inx(); // increment offset for enemy object
  lda_imm(0x5); // load default value here
  cpy_imm_fcz(0x29); // residual comparison, object #29 never executes
  // this code, thus unconditional branch here
  if (zero_flag) {
    lda_imm(0x9); // residual code
  }
  // SetDplSpd:
  ram[0x0] = a; // save downward movement amount here
  lda_imm(0xa); // save upward movement amount here
  ram[0x1] = a;
  lda_imm(0x3); // save maximum vertical speed here
  ram[0x2] = a;
  pla(); // get value from stack
  tay_fzn(); // use as Y, then move onto code shared by red koopa
  // RedPTroopaGrav:
  cpu_call_begin(0xbfd3); ImposeGravity(); cpu_call_end(); // do a sub to move object gradually
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used for downward force
  // $01 - used for upward force
  // $07 - used as adder for vertical position
}

void ImposeGravity(void) {
  // ImposeGravity:
  pha(); // push value to stack
  lda_absx(SprObject_YMF_Dummy);
  carry_flag = false; // add value in movement force to contents of dummy variable
  adc_absx_fc(SprObject_Y_MoveForce);
  ram[SprObject_YMF_Dummy + x] = a;
  ldy_imm(0x0); // set Y to zero by default
  lda_zpx_fn(SprObject_Y_Speed); // get current vertical speed
  if (!neg_flag) { goto AlterYP; } // if currently moving downwards, do not decrement Y
  dey(); // otherwise decrement Y
  
AlterYP:
  ram[0x7] = y; // store Y here
  adc_zpx_fc(SprObject_Y_Position); // add vertical position to vertical speed plus carry
  ram[SprObject_Y_Position + x] = a; // store as new vertical position
  lda_zpx(SprObject_Y_HighPos);
  adc_zp(0x7); // add carry plus contents of $07 to vertical high byte
  ram[SprObject_Y_HighPos + x] = a; // store as new vertical high byte
  lda_absx(SprObject_Y_MoveForce);
  carry_flag = false;
  adc_zp_fc(0x0); // add downward movement amount to contents of $0433
  ram[SprObject_Y_MoveForce + x] = a;
  lda_zpx(SprObject_Y_Speed); // add carry to vertical speed and store
  adc_imm(0x0);
  ram[SprObject_Y_Speed + x] = a;
  cmp_zp_fcn(0x2); // compare to maximum speed
  if (neg_flag) { goto ChkUpM; } // if less than preset value, skip this part
  lda_absx(SprObject_Y_MoveForce);
  cmp_imm_fc(0x80); // if less positively than preset maximum, skip this part
  if (!carry_flag) { goto ChkUpM; }
  lda_zp(0x2);
  ram[SprObject_Y_Speed + x] = a; // keep vertical speed within maximum value
  lda_imm(0x0);
  ram[SprObject_Y_MoveForce + x] = a; // clear fractional
  
ChkUpM:
  pla_fzn(); // get value from stack
  if (zero_flag) { return; } // if set to zero, branch to leave
  lda_zp(0x2);
  eor_imm(0b11111111); // otherwise get two's compliment of maximum speed
  tay();
  iny();
  ram[0x7] = y; // store two's compliment here
  lda_absx(SprObject_Y_MoveForce);
  carry_flag = true; // subtract upward movement amount from contents
  sbc_zp_fc(0x1); // of movement force, note that $01 is twice as large as $00,
  ram[SprObject_Y_MoveForce + x] = a; // thus it effectively undoes add we did earlier
  lda_zpx(SprObject_Y_Speed);
  sbc_imm(0x0); // subtract borrow from vertical speed and store
  ram[SprObject_Y_Speed + x] = a;
  cmp_zp_fczn(0x7); // compare vertical speed to two's compliment
  if (!neg_flag) { return; } // if less negatively than preset maximum, skip this part
  lda_absx(SprObject_Y_MoveForce);
  cmp_imm_fczn(0x80); // check if fractional part is above certain amount,
  if (carry_flag) { return; } // and if so, branch to leave
  lda_zp(0x7);
  ram[SprObject_Y_Speed + x] = a; // keep vertical speed within maximum value
  lda_imm_fzn(0xff);
  ram[SprObject_Y_MoveForce + x] = a; // clear fractional
  // ExVMove:
  return; // leave!
  // -------------------------------------------------------------------------------------
}

void EnemiesAndLoopsCore(void) {
  // EnemiesAndLoopsCore:
  lda_zpx(Enemy_Flag); // check data here for MSB set
  pha(); // save in stack
  asl_acc_fc();
  if (carry_flag) { goto ChkBowserF; } // if MSB set in enemy flag, branch ahead of jumps
  pla_fz(); // get from stack
  if (zero_flag) { goto ChkAreaTsk; } // if data zero, branch
  goto RunEnemyObjectsCore; // otherwise, jump to run enemy subroutines
  
ChkAreaTsk:
  lda_abs(AreaParserTaskNum); // check number of tasks to perform
  and_imm(0x7);
  cmp_imm_fczn(0x7); // if at a specific task, jump and leave
  if (zero_flag) { return; }
  goto ProcLoopCommand; // otherwise, jump to process loop command/load enemies
  
ChkBowserF:
  pla(); // get data from stack
  and_imm(0b00001111); // mask out high nybble
  tay();
  lda_zpy_fzn(Enemy_Flag); // use as pointer and load same place with different offset
  if (!zero_flag) { return; }
  ram[Enemy_Flag + x] = a; // if second enemy flag not set, also clear first one
  // ExitELCore:
  return;
  // --------------------------------
  // loop command data
  
ProcLoopCommand:
  lda_abs_fz(LoopCommand); // check if loop command was found
  if (zero_flag) { goto ChkEnemyFrenzy; }
  lda_abs_fz(CurrentColumnPos); // check to see if we're still on the first page
  if (!zero_flag) { goto ChkEnemyFrenzy; } // if not, do not loop yet
  ldy_imm(0xb); // start at the end of each set of loop data
  
FindLoop:
  dey_fn();
  if (neg_flag) { goto ChkEnemyFrenzy; } // if all data is checked and not match, do not loop
  lda_abs(WorldNumber); // check to see if one of the world numbers
  cmp_absy_fcz(LoopCmdWorldNumber); // matches our current world number
  if (!zero_flag) { goto FindLoop; }
  lda_abs(CurrentPageLoc); // check to see if one of the page numbers
  cmp_absy_fcz(LoopCmdPageNumber); // matches the page we're currently on
  if (!zero_flag) { goto FindLoop; }
  lda_zp(Player_Y_Position); // check to see if the player is at the correct position
  cmp_absy_fz(LoopCmdYPosition); // if not, branch to check for world 7
  if (!zero_flag) { goto WrongChk; }
  lda_zp(Player_State); // check to see if the player is
  cmp_imm_fz(0x0); // on solid ground (i.e. not jumping or falling)
  if (!zero_flag) { goto WrongChk; } // if not, player fails to pass loop, and loopback
  lda_abs(WorldNumber); // are we in world 7? (check performed on correct
  cmp_imm_fcz(World7); // vertical position and on solid ground)
  if (!zero_flag) { goto InitMLp; } // if not, initialize flags used there, otherwise
  inc_abs(MultiLoopCorrectCntr); // increment counter for correct progression
  
IncMLoop:
  inc_abs(MultiLoopPassCntr); // increment master multi-part counter
  lda_abs(MultiLoopPassCntr); // have we done all three parts?
  cmp_imm_fcz(0x3);
  if (!zero_flag) { goto InitLCmd; } // if not, skip this part
  lda_abs(MultiLoopCorrectCntr); // if so, have we done them all correctly?
  cmp_imm_fczn(0x3);
  if (zero_flag) { goto InitMLp; } // if so, branch past unnecessary check here
  if (!zero_flag) { goto DoLpBack; } // unconditional branch if previous branch fails
  
WrongChk:
  lda_abs(WorldNumber); // are we in world 7? (check performed on
  cmp_imm_fczn(World7); // incorrect vertical position or not on solid ground)
  if (zero_flag) { goto IncMLoop; }
  
DoLpBack:
  cpu_call_begin(0xc11e); ExecGameLoopback(); cpu_call_end(); // if player is not in right place, loop back
  cpu_call_begin(0xc121); KillAllEnemies(); cpu_call_end();
  
InitMLp:
  lda_imm(0x0); // initialize counters used for multi-part loop commands
  ram[MultiLoopPassCntr] = a;
  ram[MultiLoopCorrectCntr] = a;
  
InitLCmd:
  lda_imm(0x0); // initialize loop command flag
  ram[LoopCommand] = a;
  // --------------------------------
  
ChkEnemyFrenzy:
  lda_abs_fz(EnemyFrenzyQueue); // check for enemy object in frenzy queue
  if (zero_flag) { goto ProcessEnemyData; } // if not, skip this part
  ram[Enemy_ID + x] = a; // store as enemy object identifier here
  lda_imm(0x1);
  ram[Enemy_Flag + x] = a; // activate enemy object flag
  lda_imm(0x0);
  ram[Enemy_State + x] = a; // initialize state and frenzy queue
  ram[EnemyFrenzyQueue] = a;
  InitEnemyObject(); return; // and then jump to deal with this enemy
  // --------------------------------
  // $06 - used to hold page location of extended right boundary
  // $07 - used to hold high nybble of position of extended right boundary
  
ProcessEnemyData:
  ldy_abs(EnemyDataOffset); // get offset of enemy object data
  lda_indy(EnemyData); // load first byte
  cmp_imm_fcz(0xff); // check for EOD terminator
  if (!zero_flag) { goto CheckEndofBuffer; }
  goto CheckFrenzyBuffer; // if found, jump to check frenzy buffer, otherwise
  
CheckEndofBuffer:
  and_imm(0b00001111); // check for special row $0e
  cmp_imm_fz(0xe);
  if (zero_flag) { goto CheckRightBounds; } // if found, branch, otherwise
  cpx_imm_fc(0x5); // check for end of buffer
  if (!carry_flag) { goto CheckRightBounds; } // if not at end of buffer, branch
  iny();
  lda_indy(EnemyData); // check for specific value here
  and_imm(0b00111111); // not sure what this was intended for, exactly
  cmp_imm_fczn(0x2e); // this part is quite possibly residual code
  if (zero_flag) { goto CheckRightBounds; } // but it has the effect of keeping enemies out of
  return; // the sixth slot
  
CheckRightBounds:
  lda_abs(ScreenRight_X_Pos); // add 48 to pixel coordinate of right boundary
  carry_flag = false;
  adc_imm_fc(0x30);
  and_imm(0b11110000); // store high nybble
  ram[0x7] = a;
  lda_abs(ScreenRight_PageLoc); // add carry to page location of right boundary
  adc_imm(0x0);
  ram[0x6] = a; // store page location + carry
  ldy_abs(EnemyDataOffset);
  iny();
  lda_indy(EnemyData); // if MSB of enemy object is clear, branch to check for row $0f
  asl_acc_fc();
  if (!carry_flag) { goto CheckPageCtrlRow; }
  lda_abs_fz(EnemyObjectPageSel); // if page select already set, do not set again
  if (!zero_flag) { goto CheckPageCtrlRow; }
  inc_abs(EnemyObjectPageSel); // otherwise, if MSB is set, set page select 
  inc_abs(EnemyObjectPageLoc); // and increment page control
  
CheckPageCtrlRow:
  dey();
  lda_indy(EnemyData); // reread first byte
  and_imm(0xf);
  cmp_imm_fcz(0xf); // check for special row $0f
  if (!zero_flag) { goto PositionEnemyObj; } // if not found, branch to position enemy object
  lda_abs_fz(EnemyObjectPageSel); // if page select set,
  if (!zero_flag) { goto PositionEnemyObj; } // branch without reading second byte
  iny();
  lda_indy(EnemyData); // otherwise, get second byte, mask out 2 MSB
  and_imm(0b00111111);
  ram[EnemyObjectPageLoc] = a; // store as page control for enemy object data
  inc_abs(EnemyDataOffset); // increment enemy object data offset 2 bytes
  inc_abs(EnemyDataOffset);
  inc_abs(EnemyObjectPageSel); // set page select for enemy object data and 
  goto ProcLoopCommand; // jump back to process loop commands again
  
PositionEnemyObj:
  lda_abs(EnemyObjectPageLoc); // store page control as page location
  ram[Enemy_PageLoc + x] = a; // for enemy object
  lda_indy(EnemyData); // get first byte of enemy object
  and_imm(0b11110000);
  ram[Enemy_X_Position + x] = a; // store column position
  cmp_abs_fc(ScreenRight_X_Pos); // check column position against right boundary
  lda_zpx(Enemy_PageLoc); // without subtracting, then subtract borrow
  sbc_abs_fc(ScreenRight_PageLoc); // from page location
  if (carry_flag) { goto CheckRightExtBounds; } // if enemy object beyond or at boundary, branch
  lda_indy(EnemyData);
  and_imm(0b00001111); // check for special row $0e
  cmp_imm_fz(0xe); // if found, jump elsewhere
  if (zero_flag) { goto ParseRow0e; }
  goto CheckThreeBytes; // if not found, unconditional jump
  
CheckRightExtBounds:
  lda_zp(0x7); // check right boundary + 48 against
  cmp_zpx_fc(Enemy_X_Position); // column position without subtracting,
  lda_zp(0x6); // then subtract borrow from page control temp
  sbc_zpx_fc(Enemy_PageLoc); // plus carry
  if (!carry_flag) { goto CheckFrenzyBuffer; } // if enemy object beyond extended boundary, branch
  lda_imm(0x1); // store value in vertical high byte
  ram[Enemy_Y_HighPos + x] = a;
  lda_indy(EnemyData); // get first byte again
  asl_acc(); // multiply by four to get the vertical
  asl_acc(); // coordinate
  asl_acc();
  asl_acc();
  ram[Enemy_Y_Position + x] = a;
  cmp_imm_fcz(0xe0); // do one last check for special row $0e
  if (zero_flag) { goto ParseRow0e; } // (necessary if branched to $c1cb)
  iny();
  lda_indy(EnemyData); // get second byte of object
  and_imm_fz(0b01000000); // check to see if hard mode bit is set
  if (zero_flag) { goto CheckForEnemyGroup; } // if not, branch to check for group enemy objects
  lda_abs_fz(SecondaryHardMode); // if set, check to see if secondary hard mode flag
  if (zero_flag) { goto Inc2B; } // is on, and if not, branch to skip this object completely
  
CheckForEnemyGroup:
  lda_indy(EnemyData); // get second byte and mask out 2 MSB
  and_imm(0b00111111);
  cmp_imm_fc(0x37); // check for value below $37
  if (!carry_flag) { goto BuzzyBeetleMutate; }
  cmp_imm_fc(0x3f); // if $37 or greater, check for value
  if (!carry_flag) { goto DoGroup; } // below $3f, branch if below $3f
  
BuzzyBeetleMutate:
  cmp_imm_fcz(Goomba); // if below $37, check for goomba
  if (!zero_flag) { goto StrID; } // value ($3f or more always fails)
  ldy_abs_fz(PrimaryHardMode); // check if primary hard mode flag is set
  if (zero_flag) { goto StrID; } // and if so, change goomba to buzzy beetle
  lda_imm(BuzzyBeetle);
  
StrID:
  ram[Enemy_ID + x] = a; // store enemy object number into buffer
  lda_imm_fzn(0x1);
  ram[Enemy_Flag + x] = a; // set flag for enemy in buffer
  cpu_call_begin(0xc210); InitEnemyObject(); cpu_call_end();
  lda_zpx_fzn(Enemy_Flag); // check to see if flag is set
  if (!zero_flag) { goto Inc2B; } // if not, leave, otherwise branch
  return;
  
CheckFrenzyBuffer:
  lda_abs_fz(EnemyFrenzyBuffer); // if enemy object stored in frenzy buffer
  if (!zero_flag) { goto StrFre; } // then branch ahead to store in enemy object buffer
  lda_abs(VineFlagOffset); // otherwise check vine flag offset
  cmp_imm_fczn(0x1);
  if (!zero_flag) { return; } // if other value <> 1, leave
  lda_imm(VineObject); // otherwise put vine in enemy identifier
  
StrFre:
  ram[Enemy_ID + x] = a; // store contents of frenzy buffer into enemy identifier value
  InitEnemyObject(); // fallthrough
  return;
  
DoGroup:
  goto HandleGroupEnemies; // handle enemy group objects
  
ParseRow0e:
  iny(); // increment Y to load third byte of object
  iny();
  lda_indy(EnemyData);
  lsr_acc(); // move 3 MSB to the bottom, effectively
  lsr_acc(); // making %xxx00000 into %00000xxx
  lsr_acc();
  lsr_acc();
  lsr_acc();
  cmp_abs_fcz(WorldNumber); // is it the same world number as we're on?
  if (!zero_flag) { goto NotUse; } // if not, do not use (this allows multiple uses
  dey(); // of the same area, like the underground bonus areas)
  lda_indy(EnemyData); // otherwise, get second byte and use as offset
  ram[AreaPointer] = a; // to addresses for level and enemy object data
  iny();
  lda_indy(EnemyData); // get third byte again, and this time mask out
  and_imm(0b00011111); // the 3 MSB from before, save as page number to be
  ram[EntrancePage] = a; // used upon entry to area, if area is entered
  
NotUse:
  goto Inc3B;
  
CheckThreeBytes:
  ldy_abs(EnemyDataOffset); // load current offset for enemy object data
  lda_indy(EnemyData); // get first byte
  and_imm(0b00001111); // check for special row $0e
  cmp_imm_fcz(0xe);
  if (!zero_flag) { goto Inc2B; }
  
Inc3B:
  inc_abs(EnemyDataOffset); // if row = $0e, increment three bytes
  
Inc2B:
  inc_abs(EnemyDataOffset); // otherwise increment two bytes
  inc_abs(EnemyDataOffset);
  lda_imm(0x0); // init page select for enemy objects
  ram[EnemyObjectPageSel] = a;
  ldx_zp_fzn(ObjectOffset); // reload current offset in enemy buffers
  return; // and leave
  
HandleGroupEnemies:
  ldy_imm(0x0); // load value for green koopa troopa
  carry_flag = true;
  sbc_imm(0x37); // subtract $37 from second byte read
  pha(); // save result in stack for now
  cmp_imm_fc(0x4); // was byte in $3b-$3e range?
  if (carry_flag) { goto SnglID; } // if so, branch
  pha(); // save another copy to stack
  ldy_imm(Goomba); // load value for goomba enemy
  lda_abs_fz(PrimaryHardMode); // if primary hard mode flag not set,
  if (zero_flag) { goto PullID; } // branch, otherwise change to value
  ldy_imm(BuzzyBeetle); // for buzzy beetle
  
PullID:
  pla(); // get second copy from stack
  
SnglID:
  ram[0x1] = y; // save enemy id here
  ldy_imm(0xb0); // load default y coordinate
  and_imm_fz(0x2); // check to see if d1 was set
  if (zero_flag) { goto SetYGp; } // if so, move y coordinate up,
  ldy_imm(0x70); // otherwise branch and use default
  
SetYGp:
  ram[0x0] = y; // save y coordinate here
  lda_abs(ScreenRight_PageLoc); // get page number of right edge of screen
  ram[0x2] = a; // save here
  lda_abs(ScreenRight_X_Pos); // get pixel coordinate of right edge
  ram[0x3] = a; // save here
  ldy_imm(0x2); // load two enemies by default
  pla(); // get first copy from stack
  lsr_acc_fc(); // check to see if d0 was set
  if (!carry_flag) { goto CntGrp; } // if not, use default value
  iny(); // otherwise increment to three enemies
  
CntGrp:
  ram[NumberofGroupEnemies] = y; // save number of enemies here
  
GrLoop:
  ldx_imm(0xff); // start at beginning of enemy buffers
  
GSltLp:
  inx(); // increment and branch if past
  cpx_imm_fc(0x5); // end of buffers
  if (carry_flag) { goto NextED; }
  lda_zpx_fz(Enemy_Flag); // check to see if enemy is already
  if (!zero_flag) { goto GSltLp; } // stored in buffer, and branch if so
  lda_zp(0x1);
  ram[Enemy_ID + x] = a; // store enemy object identifier
  lda_zp(0x2);
  ram[Enemy_PageLoc + x] = a; // store page location for enemy object
  lda_zp(0x3);
  ram[Enemy_X_Position + x] = a; // store x coordinate for enemy object
  carry_flag = false;
  adc_imm_fc(0x18); // add 24 pixels for next enemy
  ram[0x3] = a;
  lda_zp(0x2); // add carry to page location for
  adc_imm_fc(0x0); // next enemy
  ram[0x2] = a;
  lda_zp(0x0); // store y coordinate for enemy object
  ram[Enemy_Y_Position + x] = a;
  lda_imm_fzn(0x1); // activate flag for buffer, and
  ram[Enemy_Y_HighPos + x] = a; // put enemy within the screen vertically
  ram[Enemy_Flag + x] = a;
  cpu_call_begin(0xc77e); CheckpointEnemyID(); cpu_call_end(); // process each enemy object separately
  dec_abs_fz(NumberofGroupEnemies); // do this until we run out of enemy objects
  if (!zero_flag) { goto GrLoop; }
  
NextED:
  goto Inc2B; // jump to increment data offset and leave
  // --------------------------------
  
RunEnemyObjectsCore:
  ldx_zp(ObjectOffset); // get offset for enemy object buffer
  lda_imm(0x0); // load value 0 for jump engine by default
  ldy_zpx(Enemy_ID);
  cpy_imm_fczn(0x15); // if enemy object < $15, use default value
  if (!carry_flag) { goto JmpEO; }
  tya(); // otherwise subtract $14 from the value and use
  sbc_imm_fczn(0x14); // as value for jump engine
  
JmpEO:
  cpu_call_begin(0xc891);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xc8e0: RunNormalEnemies(); return;
    case 0xc935: RunBowserFlame(); return;
    case 0xd295: RunFireworks(); return;
    case 0xc8d6: NoRunCode(); return;
    case 0xc947: RunFirebarObj(); return;
    case 0xc965: RunLargePlatform(); return;
    case 0xc94d: RunSmallPlatform(); return;
    case 0xd065: RunBowser(); return;
    case 0xbc85: PowerUpObjHandler(); return;
    case 0xb94b: VineObjectHandler(); return;
    case 0xd2d9: RunStarFlagObj(); return;
    case 0xb8ba: JumpspringHandler(); return;
    case 0xb7a4: WarpZoneObject(); return;
    case 0xc8d7: RunRetainerObj(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void ExecGameLoopback(void) {
  lda_zp(Player_PageLoc); // send player back four pages
  carry_flag = true;
  sbc_imm(0x4);
  ram[Player_PageLoc] = a;
  lda_abs(CurrentPageLoc); // send current page back four pages
  carry_flag = true;
  sbc_imm(0x4);
  ram[CurrentPageLoc] = a;
  lda_abs(ScreenLeft_PageLoc); // subtract four from page location
  carry_flag = true; // of screen's left border
  sbc_imm(0x4);
  ram[ScreenLeft_PageLoc] = a;
  lda_abs(ScreenRight_PageLoc); // do the same for the page location
  carry_flag = true; // of screen's right border
  sbc_imm(0x4);
  ram[ScreenRight_PageLoc] = a;
  lda_abs(AreaObjectPageLoc); // subtract four from page control
  carry_flag = true; // for area objects
  sbc_imm_fc(0x4);
  ram[AreaObjectPageLoc] = a;
  lda_imm(0x0); // initialize page select for both
  ram[EnemyObjectPageSel] = a; // area and enemy objects
  ram[AreaObjectPageSel] = a;
  ram[EnemyDataOffset] = a; // initialize enemy object data offset
  ram[EnemyObjectPageLoc] = a; // and enemy object page control
  lda_absy_fzn(AreaDataOfsLoopback); // adjust area object offset based on
  ram[AreaDataOffset] = a; // which loop command we encountered
  return;
}

void InitEnemyObject(void) {
  lda_imm_fzn(0x0); // initialize enemy state
  ram[Enemy_State + x] = a;
  cpu_call_begin(0xc22c); CheckpointEnemyID(); cpu_call_end(); // jump ahead to run jump engine and subroutines
  // ExEPar:
  return; // then leave
}

void CheckpointEnemyID(void) {
  lda_zpx(Enemy_ID);
  cmp_imm_fczn(0x15); // check enemy object identifier for $15 or greater
  // and branch straight to the jump engine if found
  if (!carry_flag) {
    tay(); // save identifier in Y register for now
    lda_zpx(Enemy_Y_Position);
    adc_imm_fc(0x8); // add eight pixels to what will eventually be the
    ram[Enemy_Y_Position + x] = a; // enemy object's vertical coordinate ($00-$14 only)
    lda_imm(0x1);
    ram[EnemyOffscrBitsMasked + x] = a; // set offscreen masked bit
    tya_fzn(); // get identifier back and use as offset for jump engine
  }
  // InitEnemyRoutines:
  cpu_call_begin(0xc281);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xc30e: InitNormalEnemy(); return;
    case 0xc31e: InitRedKoopa(); return;
    case 0xc2f0: NoInitCode(); return;
    case 0xc328: InitHammerBro(); return;
    case 0xc2f1: InitGoomba(); return;
    case 0xc342: InitBloober(); return;
    case 0xc36b: InitBulletBill(); return;
    case 0xc375: InitCheepCheep(); return;
    case 0xc2f7: InitPodoboo(); return;
    case 0xc787: InitPiranhaPlant(); return;
    case 0xc7d1: InitJumpGPTroopa(); return;
    case 0xc34a: InitRedPTroopa(); return;
    case 0xc33d: InitHorizFlySwimEnemy(); return;
    case 0xc385: InitLakitu(); return;
    case 0xc7a0: InitEnemyFrenzy(); return;
    case 0xc7b8: EndFrenzy(); return;
    case 0xc45c: InitShortFirebar(); return;
    case 0xc459: InitLongFirebar(); return;
    case 0xc7df: InitBalPlatform(); return;
    case 0xc812: InitVertPlatform(); return;
    case 0xc83f: LargeLiftUp(); return;
    case 0xc845: LargeLiftDown(); return;
    case 0xc80b: InitHoriPlatform(); return;
    case 0xc803: InitDropPlatform(); return;
    case 0xc84b: PlatLiftUp(); return;
    case 0xc857: PlatLiftDown(); return;
    case 0xc549: InitBowser(); return;
    case 0xbc60: PwrUpJmp(); return;
    case 0xb91e: Setup_Vine(); return;
    case 0xc307: InitRetainerObj(); return;
    case 0xc881: EndOfEnemyInitCode(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void NoInitCode(void) {
  return; // this executed when enemy object has no init code
  // --------------------------------
}

void InitGoomba(void) {
  cpu_call_begin(0xc2f3); InitNormalEnemy(); cpu_call_end(); // set appropriate horizontal speed
  SmallBBox(); return; // set $09 as bounding box control, set other values
  // --------------------------------
}

void InitPodoboo(void) {
  lda_imm(0x2); // set enemy position to below
  ram[Enemy_Y_HighPos + x] = a; // the bottom of the screen
  ram[Enemy_Y_Position + x] = a;
  lsr_acc();
  ram[EnemyIntervalTimer + x] = a; // set timer for enemy
  lsr_acc_fc();
  ram[Enemy_State + x] = a; // initialize enemy state, then jump to use
  SmallBBox(); return; // $09 as bounding box size and set other things
  // --------------------------------
}

void InitRetainerObj(void) {
  lda_imm_fzn(0xb8); // set fixed vertical position for
  ram[Enemy_Y_Position + x] = a; // princess/mushroom retainer object
  return;
  // --------------------------------
}

void InitNormalEnemy(void) {
  ldy_imm(0x1); // load offset of 1 by default
  lda_abs_fz(PrimaryHardMode); // check for primary hard mode flag set
  if (zero_flag) {
    dey(); // if not set, decrement offset
  }
  // GetESpd:
  lda_absy(NormalXSpdData); // get appropriate horizontal speed
  // SetESpd:
  ram[Enemy_X_Speed + x] = a; // store as speed for enemy object
  goto TallBBox; // branch to set bounding box control and other data
  // --------------------------------
  
TallBBox:
  lda_imm(0x3); // set specific bounding box size control
  // SetBBox:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control here
  lda_imm(0x2); // set moving direction for left
  ram[Enemy_MovingDir + x] = a;
  InitVStf(); // fallthrough
  return;
}

void InitRedKoopa(void) {
  cpu_call_begin(0xc320); InitNormalEnemy(); cpu_call_end(); // load appropriate horizontal speed
  lda_imm_fzn(0x1); // set enemy state for red koopa troopa $03
  ram[Enemy_State + x] = a;
  return;
  // --------------------------------
}

void InitHammerBro(void) {
  lda_imm(0x0); // init horizontal speed and timer used by hammer bro
  ram[HammerThrowingTimer + x] = a; // apparently to time hammer throwing
  ram[Enemy_X_Speed + x] = a;
  ldy_abs(SecondaryHardMode); // get secondary hard mode flag
  lda_absy(HBroWalkingTimerData);
  ram[EnemyIntervalTimer + x] = a; // set value as delay for hammer bro to walk left
  lda_imm(0xb); // set specific value for bounding box size control
  goto SetBBox;
  // --------------------------------
  
SetBBox:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control here
  lda_imm(0x2); // set moving direction for left
  ram[Enemy_MovingDir + x] = a;
  InitVStf(); // fallthrough
  return;
}

void InitHorizFlySwimEnemy(void) {
  goto InitHorizFlySwimEnemy;
  
SetESpd:
  ram[Enemy_X_Speed + x] = a; // store as speed for enemy object
  goto TallBBox; // branch to set bounding box control and other data
  // --------------------------------
  
InitHorizFlySwimEnemy:
  lda_imm(0x0); // initialize horizontal speed
  goto SetESpd;
  // --------------------------------
  
TallBBox:
  lda_imm(0x3); // set specific bounding box size control
  // SetBBox:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control here
  lda_imm(0x2); // set moving direction for left
  ram[Enemy_MovingDir + x] = a;
  InitVStf(); // fallthrough
  return;
}

void InitBloober(void) {
  lda_imm(0x0); // initialize horizontal speed
  ram[BlooperMoveSpeed + x] = a;
  SmallBBox(); // fallthrough
  return;
}

void SmallBBox(void) {
  lda_imm(0x9); // set specific bounding box size control
  goto SetBBox; // unconditional branch
  // --------------------------------
  
SetBBox:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control here
  lda_imm(0x2); // set moving direction for left
  ram[Enemy_MovingDir + x] = a;
  InitVStf(); // fallthrough
  return;
}

void InitRedPTroopa(void) {
  ldy_imm(0x30); // load central position adder for 48 pixels down
  lda_zpx_fn(Enemy_Y_Position); // set vertical coordinate into location to
  ram[RedPTroopaOrigXPos + x] = a; // be used as original vertical coordinate
  // if vertical coordinate < $80
  if (neg_flag) {
    ldy_imm(0xe0); // if => $80, load position adder for 32 pixels up
  }
  // GetCent:
  tya(); // send central position adder to A
  adc_zpx_fc(Enemy_Y_Position); // add to current vertical coordinate
  ram[RedPTroopaCenterYPos + x] = a; // store as central vertical coordinate
  // TallBBox:
  lda_imm(0x3); // set specific bounding box size control
  // SetBBox:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control here
  lda_imm(0x2); // set moving direction for left
  ram[Enemy_MovingDir + x] = a;
  InitVStf(); // fallthrough
  return;
}

void InitVStf(void) {
  lda_imm_fzn(0x0); // initialize vertical speed
  ram[Enemy_Y_Speed + x] = a; // and movement force
  ram[Enemy_Y_MoveForce + x] = a;
  return;
  // --------------------------------
}

void InitBulletBill(void) {
  lda_imm(0x2); // set moving direction for left
  ram[Enemy_MovingDir + x] = a;
  lda_imm_fzn(0x9); // set bounding box control for $09
  ram[Enemy_BoundBoxCtrl + x] = a;
  return;
  // --------------------------------
}

void InitCheepCheep(void) {
  cpu_call_begin(0xc377); SmallBBox(); cpu_call_end(); // set vertical bounding box, speed, init others
  lda_absx(PseudoRandomBitReg); // check one portion of LSFR
  and_imm(0b00010000); // get d4 from it
  ram[CheepCheepMoveMFlag + x] = a; // save as movement flag of some sort
  lda_zpx_fzn(Enemy_Y_Position);
  ram[CheepCheepOrigYPos + x] = a; // save original vertical coordinate here
  return;
  // --------------------------------
}

void InitLakitu(void) {
  lda_abs_fz(EnemyFrenzyBuffer); // check to see if an enemy is already in
  // the frenzy buffer, and branch to kill lakitu if so
  if (zero_flag) {
    SetupLakitu(); // fallthrough
    return;
  }
  // KillLakitu:
  EraseEnemyObject(); return;
  // --------------------------------
  // $01-$03 - used to hold pseudorandom difference adjusters
}

void SetupLakitu(void) {
  lda_imm_fzn(0x0); // erase counter for lakitu's reappearance
  ram[LakituReappearTimer] = a;
  cpu_call_begin(0xc391); InitHorizFlySwimEnemy(); cpu_call_end(); // set $03 as bounding box, set other attributes
  goto TallBBox2; // set $03 as bounding box again (not necessary) and leave
  
TallBBox2:
  lda_imm_fzn(0x3); // set specific value for bounding box control
  // SetBBox2:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control then leave
  return;
  // --------------------------------
}

void LakituAndSpinyHandler(void) {
  // LakituAndSpinyHandler:
  lda_abs_fzn(FrenzyEnemyTimer); // if timer here not expired, leave
  if (!zero_flag) { return; }
  cpx_imm_fczn(0x5); // if we are on the special use slot, leave
  if (carry_flag) { return; }
  lda_imm(0x80); // set timer
  ram[FrenzyEnemyTimer] = a;
  ldy_imm(0x4); // start with the last enemy slot
  
ChkLak:
  lda_zpy(Enemy_ID); // check all enemy slots to see
  cmp_imm_fz(Lakitu); // if lakitu is on one of them
  if (zero_flag) { goto CreateSpiny; } // if so, branch out of this loop
  dey_fn(); // otherwise check another slot
  if (!neg_flag) { goto ChkLak; } // loop until all slots are checked
  inc_abs(LakituReappearTimer); // increment reappearance timer
  lda_abs(LakituReappearTimer);
  cmp_imm_fczn(0x7); // check to see if we're up to a certain value yet
  if (!carry_flag) { return; } // if not, leave
  ldx_imm(0x4); // start with the last enemy slot again
  
ChkNoEn:
  lda_zpx_fz(Enemy_Flag); // check enemy buffer flag for non-active enemy slot
  if (zero_flag) { goto CreateL; } // branch out of loop if found
  dex_fn(); // otherwise check next slot
  if (!neg_flag) { goto ChkNoEn; } // branch until all slots are checked
  if (neg_flag) { goto RetEOfs; } // if no empty slots were found, branch to leave
  
CreateL:
  lda_imm(0x0); // initialize enemy state
  ram[Enemy_State + x] = a;
  lda_imm_fzn(Lakitu); // create lakitu enemy object
  ram[Enemy_ID + x] = a;
  cpu_call_begin(0xc3dd); SetupLakitu(); cpu_call_end(); // do a sub to set up lakitu
  lda_imm_fzn(0x20);
  cpu_call_begin(0xc3e2); PutAtRightExtent(); cpu_call_end(); // finish setting up lakitu
  
RetEOfs:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset again and leave
  // ExLSHand:
  return;
  // --------------------------------
  
CreateSpiny:
  lda_zp(Player_Y_Position); // if player above a certain point, branch to leave
  cmp_imm_fczn(0x2c);
  if (!carry_flag) { return; }
  lda_zpy_fzn(Enemy_State); // if lakitu is not in normal state, branch to leave
  if (!zero_flag) { return; }
  lda_zpy(Enemy_PageLoc); // store horizontal coordinates (high and low) of lakitu
  ram[Enemy_PageLoc + x] = a; // into the coordinates of the spiny we're going to create
  lda_zpy(Enemy_X_Position);
  ram[Enemy_X_Position + x] = a;
  lda_imm(0x1); // put spiny within vertical screen unit
  ram[Enemy_Y_HighPos + x] = a;
  lda_zpy(Enemy_Y_Position); // put spiny eight pixels above where lakitu is
  carry_flag = true;
  sbc_imm_fc(0x8);
  ram[Enemy_Y_Position + x] = a;
  lda_absx(PseudoRandomBitReg); // get 2 LSB of LSFR and save to Y
  and_imm(0b00000011);
  tay();
  ldx_imm(0x2);
  
DifLoop:
  lda_absy(PRDiffAdjustData); // get three values and save them
  ram[0x1 + x] = a; // to $01-$03
  iny();
  iny(); // increment Y four bytes for each value
  iny();
  iny();
  dex_fn(); // decrement X for each one
  if (!neg_flag) { goto DifLoop; } // loop until all three are written
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset
  cpu_call_begin(0xc41f); PlayerLakituDiff(); cpu_call_end(); // move enemy, change direction, get value - difference
  ldy_zp(Player_X_Speed); // check player's horizontal speed
  cpy_imm_fczn(0x8);
  if (carry_flag) { goto SetSpSpd; } // if moving faster than a certain amount, branch elsewhere
  tay(); // otherwise save value in A to Y for now
  lda_absx(PseudoRandomBitReg + 1);
  and_imm_fz(0b00000011); // get one of the LSFR parts and save the 2 LSB
  if (zero_flag) { goto UsePosv; } // branch if neither bits are set
  tya();
  eor_imm(0b11111111); // otherwise get two's compliment of Y
  tay();
  iny();
  
UsePosv:
  tya_fzn(); // put value from A in Y back to A (they will be lost anyway)
  
SetSpSpd:
  cpu_call_begin(0xc436); SmallBBox(); cpu_call_end(); // set bounding box control, init attributes, lose contents of A
  ldy_imm(0x2);
  ram[Enemy_X_Speed + x] = a; // set horizontal speed to zero because previous contents
  cmp_imm_fcn(0x0); // of A were lost...branch here will never be taken for
  if (neg_flag) { goto SpinyRte; } // the same reason
  dey();
  
SpinyRte:
  ram[Enemy_MovingDir + x] = y; // set moving direction to the right
  lda_imm(0xfd);
  ram[Enemy_Y_Speed + x] = a; // set vertical speed to move upwards
  lda_imm(0x1);
  ram[Enemy_Flag + x] = a; // enable enemy object by setting flag
  lda_imm_fzn(0x5);
  ram[Enemy_State + x] = a; // put spiny in egg state and leave
  // ChpChpEx:
  return;
  // --------------------------------
}

void InitLongFirebar(void) {
  cpu_call_begin(0xc45b); DuplicateEnemyObj(); cpu_call_end(); // create enemy object for long firebar
  InitShortFirebar(); // fallthrough
  return;
}

void InitShortFirebar(void) {
  lda_imm(0x0); // initialize low byte of spin state
  ram[FirebarSpinState_Low + x] = a;
  lda_zpx(Enemy_ID); // subtract $1b from enemy identifier
  carry_flag = true; // to get proper offset for firebar data
  sbc_imm(0x1b);
  tay();
  lda_absy(FirebarSpinSpdData); // get spinning speed of firebar
  ram[FirebarSpinSpeed + x] = a;
  lda_absy(FirebarSpinDirData); // get spinning direction of firebar
  ram[FirebarSpinDirection + x] = a;
  lda_zpx(Enemy_Y_Position);
  carry_flag = false; // add four pixels to vertical coordinate
  adc_imm(0x4);
  ram[Enemy_Y_Position + x] = a;
  lda_zpx(Enemy_X_Position);
  carry_flag = false; // add four pixels to horizontal coordinate
  adc_imm_fc(0x4);
  ram[Enemy_X_Position + x] = a;
  lda_zpx(Enemy_PageLoc);
  adc_imm_fc(0x0); // add carry to page location
  ram[Enemy_PageLoc + x] = a;
  goto TallBBox2; // set bounding box control (not used) and leave
  // --------------------------------
  // $00-$01 - used to hold pseudorandom bits
  
TallBBox2:
  lda_imm_fzn(0x3); // set specific value for bounding box control
  // SetBBox2:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control then leave
  return;
  // --------------------------------
}

void InitFlyingCheepCheep(void) {
  // InitFlyingCheepCheep:
  lda_abs_fzn(FrenzyEnemyTimer); // if timer here not expired yet, branch to leave
  if (!zero_flag) { return; }
  cpu_call_begin(0xc4af); SmallBBox(); cpu_call_end(); // jump to set bounding box size $09 and init other values
  lda_absx(PseudoRandomBitReg + 1);
  and_imm(0b00000011); // set pseudorandom offset here
  tay();
  lda_absy(FlyCCTimerData); // load timer with pseudorandom offset
  ram[FrenzyEnemyTimer] = a;
  ldy_imm(0x3); // load Y with default value
  lda_abs_fz(SecondaryHardMode);
  if (zero_flag) { goto MaxCC; } // if secondary hard mode flag not set, do not increment Y
  iny(); // otherwise, increment Y to allow as many as four onscreen
  
MaxCC:
  ram[0x0] = y; // store whatever pseudorandom bits are in Y
  cpx_zp_fczn(0x0); // compare enemy object buffer offset with Y
  if (carry_flag) { return; } // if X => Y, branch to leave
  lda_absx(PseudoRandomBitReg);
  and_imm(0b00000011); // get last two bits of LSFR, first part
  ram[0x0] = a; // and store in two places
  ram[0x1] = a;
  lda_imm(0xfb); // set vertical speed for cheep-cheep
  ram[Enemy_Y_Speed + x] = a;
  lda_imm(0x0); // load default value
  ldy_zp_fz(Player_X_Speed); // check player's horizontal speed
  if (zero_flag) { goto GSeed; } // if player not moving left or right, skip this part
  lda_imm(0x4);
  cpy_imm_fc(0x19); // if moving to the right but not very quickly,
  if (!carry_flag) { goto GSeed; } // do not change A
  asl_acc(); // otherwise, multiply A by 2
  
GSeed:
  pha(); // save to stack
  carry_flag = false;
  adc_zp(0x0); // add to last two bits of LSFR we saved earlier
  ram[0x0] = a; // save it there
  lda_absx(PseudoRandomBitReg + 1);
  and_imm_fz(0b00000011); // if neither of the last two bits of second LSFR set,
  if (zero_flag) { goto RSeed; } // skip this part and save contents of $00
  lda_absx(PseudoRandomBitReg + 2);
  and_imm(0b00001111); // otherwise overwrite with lower nybble of
  ram[0x0] = a; // third LSFR part
  
RSeed:
  pla(); // get value from stack we saved earlier
  carry_flag = false;
  adc_zp(0x1); // add to last two bits of LSFR we saved in other place
  tay(); // use as pseudorandom offset here
  lda_absy(FlyCCXSpeedData); // get horizontal speed using pseudorandom offset
  ram[Enemy_X_Speed + x] = a;
  lda_imm(0x1); // set to move towards the right
  ram[Enemy_MovingDir + x] = a;
  lda_zp_fz(Player_X_Speed); // if player moving left or right, branch ahead of this part
  if (!zero_flag) { goto D2XPos1; }
  ldy_zp(0x0); // get first LSFR or third LSFR lower nybble
  tya(); // and check for d1 set
  and_imm_fz(0b00000010);
  if (zero_flag) { goto D2XPos1; } // if d1 not set, branch
  lda_zpx(Enemy_X_Speed);
  eor_imm(0xff); // if d1 set, change horizontal speed
  carry_flag = false; // into two's compliment, thus moving in the opposite
  adc_imm(0x1); // direction
  ram[Enemy_X_Speed + x] = a;
  inc_zpx(Enemy_MovingDir); // increment to move towards the left
  
D2XPos1:
  tya(); // get first LSFR or third LSFR lower nybble again
  and_imm_fz(0b00000010);
  if (zero_flag) { goto D2XPos2; } // check for d1 set again, branch again if not set
  lda_zp(Player_X_Position); // get player's horizontal position
  carry_flag = false;
  adc_absy_fc(FlyCCXPositionData); // if d1 set, add value obtained from pseudorandom offset
  ram[Enemy_X_Position + x] = a; // and save as enemy's horizontal position
  lda_zp(Player_PageLoc); // get player's page location
  adc_imm_fc(0x0); // add carry and jump past this part
  goto FinCCSt;
  
D2XPos2:
  lda_zp(Player_X_Position); // get player's horizontal position
  carry_flag = true;
  sbc_absy_fc(FlyCCXPositionData); // if d1 not set, subtract value obtained from pseudorandom
  ram[Enemy_X_Position + x] = a; // offset and save as enemy's horizontal position
  lda_zp(Player_PageLoc); // get player's page location
  sbc_imm_fc(0x0); // subtract borrow
  
FinCCSt:
  ram[Enemy_PageLoc + x] = a; // save as enemy's page location
  lda_imm(0x1);
  ram[Enemy_Flag + x] = a; // set enemy's buffer flag
  ram[Enemy_Y_HighPos + x] = a; // set enemy's high vertical byte
  lda_imm_fzn(0xf8);
  ram[Enemy_Y_Position + x] = a; // put enemy below the screen, and we are done
  return;
  // --------------------------------
}

void InitBowser(void) {
  cpu_call_begin(0xc54b); DuplicateEnemyObj(); cpu_call_end(); // jump to create another bowser object
  ram[BowserFront_Offset] = x; // save offset of first here
  lda_imm(0x0);
  ram[BowserBodyControls] = a; // initialize bowser's body controls
  ram[BridgeCollapseOffset] = a; // and bridge collapse offset
  lda_zpx(Enemy_X_Position);
  ram[BowserOrigXPos] = a; // store original horizontal position here
  lda_imm(0xdf);
  ram[BowserFireBreathTimer] = a; // store something here
  ram[Enemy_MovingDir + x] = a; // and in moving direction
  lda_imm(0x20);
  ram[BowserFeetCounter] = a; // set bowser's feet timer and in enemy timer
  ram[EnemyFrameTimer + x] = a;
  lda_imm(0x5);
  ram[BowserHitPoints] = a; // give bowser 5 hit points
  lsr_acc_fczn();
  ram[BowserMovementSpeed] = a; // set default movement speed here
  return;
  // --------------------------------
}

void DuplicateEnemyObj(void) {
  ldy_imm(0xff); // start at beginning of enemy slots
  
FSLoop:
  iny(); // increment one slot
  lda_zpy_fz(Enemy_Flag); // check enemy buffer flag for empty slot
  if (!zero_flag) { goto FSLoop; } // if set, branch and keep checking
  ram[DuplicateObj_Offset] = y; // otherwise set offset here
  txa(); // transfer original enemy buffer offset
  ora_imm(0b10000000); // store with d7 set as flag in new enemy
  ram[Enemy_Flag + y] = a; // slot as well as enemy offset
  lda_zpx(Enemy_PageLoc);
  ram[Enemy_PageLoc + y] = a; // copy page location and horizontal coordinates
  lda_zpx(Enemy_X_Position); // from original enemy to new enemy
  ram[Enemy_X_Position + y] = a;
  lda_imm(0x1);
  ram[Enemy_Flag + x] = a; // set flag as normal for original enemy
  ram[Enemy_Y_HighPos + y] = a; // set high vertical byte for new enemy
  lda_zpx_fzn(Enemy_Y_Position);
  ram[Enemy_Y_Position + y] = a; // copy vertical coordinate from original to new
  // FlmEx:
  return; // and then leave
  // --------------------------------
}

void InitBowserFlame(void) {
  lda_abs_fzn(FrenzyEnemyTimer); // if timer not expired yet, branch to leave
  if (zero_flag) {
    ram[Enemy_Y_MoveForce + x] = a; // reset something here
    lda_zp(NoiseSoundQueue);
    ora_imm(Sfx_BowserFlame); // load bowser's flame sound into queue
    ram[NoiseSoundQueue] = a;
    ldy_abs(BowserFront_Offset); // get bowser's buffer offset
    lda_zpy(Enemy_ID); // check for bowser
    cmp_imm_fczn(Bowser);
    // branch if found
    if (!zero_flag) {
      cpu_call_begin(0xc5bd); SetFlameTimer(); cpu_call_end(); // get timer data based on flame counter
      carry_flag = false;
      adc_imm(0x20); // add 32 frames by default
      ldy_abs_fz(SecondaryHardMode);
      // if secondary mode flag not set, use as timer setting
      if (!zero_flag) {
        carry_flag = true;
        sbc_imm(0x10); // otherwise subtract 16 frames for secondary hard mode
      }
      // SetFrT:
      ram[FrenzyEnemyTimer] = a; // set timer accordingly
      lda_absx(PseudoRandomBitReg);
      and_imm(0b00000011); // get 2 LSB from first part of LSFR
      ram[BowserFlamePRandomOfs + x] = a; // set here
      tay(); // use as offset
      lda_absy(FlameYPosData); // load vertical position based on pseudorandom offset
      PutAtRightExtent(); // fallthrough
      return;
    }
    // SpawnFromMouth:
    lda_zpy(Enemy_X_Position); // get bowser's horizontal position
    carry_flag = true;
    sbc_imm(0xe); // subtract 14 pixels
    ram[Enemy_X_Position + x] = a; // save as flame's horizontal position
    lda_zpy(Enemy_PageLoc);
    ram[Enemy_PageLoc + x] = a; // copy page location from bowser to flame
    lda_zpy(Enemy_Y_Position);
    carry_flag = false; // add 8 pixels to bowser's vertical position
    adc_imm(0x8);
    ram[Enemy_Y_Position + x] = a; // save as flame's vertical position
    lda_absx(PseudoRandomBitReg);
    and_imm(0b00000011); // get 2 LSB from first part of LSFR
    ram[Enemy_YMF_Dummy + x] = a; // save here
    tay(); // use as offset
    lda_absy(FlameYPosData); // get value here using bits as offset
    ldy_imm(0x0); // load default offset
    cmp_zpx_fc(Enemy_Y_Position); // compare value to flame's current vertical position
    // if less, do not increment offset
    if (carry_flag) {
      iny(); // otherwise increment now
    }
    // SetMF:
    lda_absy(FlameYMFAdderData); // get value here and save
    ram[Enemy_Y_MoveForce + x] = a; // to vertical movement force
    lda_imm(0x0);
    ram[EnemyFrenzyBuffer] = a; // clear enemy frenzy buffer
    // FinishFlame:
    lda_imm(0x8); // set $08 for bounding box control
    ram[Enemy_BoundBoxCtrl + x] = a;
    lda_imm(0x1); // set high byte of vertical and
    ram[Enemy_Y_HighPos + x] = a; // enemy buffer flag
    ram[Enemy_Flag + x] = a;
    lsr_acc_fczn();
    ram[Enemy_X_MoveForce + x] = a; // initialize horizontal movement force, and
    ram[Enemy_State + x] = a; // enemy state
    return;
    // --------------------------------
  }
}

void PutAtRightExtent(void) {
  ram[Enemy_Y_Position + x] = a; // set vertical position
  lda_abs(ScreenRight_X_Pos);
  carry_flag = false;
  adc_imm_fc(0x20); // place enemy 32 pixels beyond right side of screen
  ram[Enemy_X_Position + x] = a;
  lda_abs(ScreenRight_PageLoc);
  adc_imm(0x0); // add carry
  ram[Enemy_PageLoc + x] = a;
  goto FinishFlame; // skip this part to finish setting values
  
FinishFlame:
  lda_imm(0x8); // set $08 for bounding box control
  ram[Enemy_BoundBoxCtrl + x] = a;
  lda_imm(0x1); // set high byte of vertical and
  ram[Enemy_Y_HighPos + x] = a; // enemy buffer flag
  ram[Enemy_Flag + x] = a;
  lsr_acc_fczn();
  ram[Enemy_X_MoveForce + x] = a; // initialize horizontal movement force, and
  ram[Enemy_State + x] = a; // enemy state
  return;
  // --------------------------------
}

void InitFireworks(void) {
  lda_abs_fzn(FrenzyEnemyTimer); // if timer not expired yet, branch to leave
  if (zero_flag) {
    lda_imm(0x20); // otherwise reset timer
    ram[FrenzyEnemyTimer] = a;
    dec_abs(FireworksCounter); // decrement for each explosion
    ldy_imm(0x6); // start at last slot
    
StarFChk:
    dey();
    lda_zpy(Enemy_ID); // check for presence of star flag object
    cmp_imm_fz(StarFlagObject); // if there isn't a star flag object,
    if (!zero_flag) { goto StarFChk; } // routine goes into infinite loop = crash
    lda_zpy(Enemy_X_Position);
    carry_flag = true; // get horizontal coordinate of star flag object, then
    sbc_imm_fc(0x30); // subtract 48 pixels from it and save to
    pha(); // the stack
    lda_zpy(Enemy_PageLoc);
    sbc_imm(0x0); // subtract the carry from the page location
    ram[0x0] = a; // of the star flag object
    lda_abs(FireworksCounter); // get fireworks counter
    carry_flag = false;
    adc_zpy(Enemy_State); // add state of star flag object (possibly not necessary)
    tay(); // use as offset
    pla(); // get saved horizontal coordinate of star flag - 48 pixels
    carry_flag = false;
    adc_absy_fc(FireworksXPosData); // add number based on offset of fireworks counter
    ram[Enemy_X_Position + x] = a; // store as the fireworks object horizontal coordinate
    lda_zp(0x0);
    adc_imm(0x0); // add carry and store as page location for
    ram[Enemy_PageLoc + x] = a; // the fireworks object
    lda_absy(FireworksYPosData); // get vertical position using same offset
    ram[Enemy_Y_Position + x] = a; // and store as vertical coordinate for fireworks object
    lda_imm(0x1);
    ram[Enemy_Y_HighPos + x] = a; // store in vertical high byte
    ram[Enemy_Flag + x] = a; // and activate enemy buffer flag
    lsr_acc_fc();
    ram[ExplosionGfxCounter + x] = a; // initialize explosion counter
    lda_imm_fzn(0x8);
    ram[ExplosionTimerCounter + x] = a; // set explosion timing counter
    // ExitFWk:
    return;
    // --------------------------------
  }
}

void BulletBillCheepCheep(void) {
  // BulletBillCheepCheep:
  lda_abs_fzn(FrenzyEnemyTimer); // if timer not expired yet, branch to leave
  if (!zero_flag) { return; }
  lda_abs_fz(AreaType); // are we in a water-type level?
  if (!zero_flag) { goto DoBulletBills; } // if not, branch elsewhere
  cpx_imm_fczn(0x3); // are we past third enemy slot?
  if (carry_flag) { return; } // if so, branch to leave
  ldy_imm(0x0); // load default offset
  lda_absx(PseudoRandomBitReg);
  cmp_imm_fc(0xaa); // check first part of LSFR against preset value
  if (!carry_flag) { goto ChkW2; } // if less than preset, do not increment offset
  iny(); // otherwise increment
  
ChkW2:
  lda_abs(WorldNumber); // check world number
  cmp_imm_fz(World2);
  if (zero_flag) { goto Get17ID; } // if we're on world 2, do not increment offset
  iny(); // otherwise increment
  
Get17ID:
  tya();
  and_imm(0b00000001); // mask out all but last bit of offset
  tay();
  lda_absy(SwimCC_IDData); // load identifier for cheep-cheeps
  
Set17ID:
  ram[Enemy_ID + x] = a; // store whatever's in A as enemy identifier
  lda_abs(BitMFilter);
  cmp_imm_fcz(0xff); // if not all bits set, skip init part and compare bits
  if (!zero_flag) { goto GetRBit; }
  lda_imm(0x0); // initialize vertical position filter
  ram[BitMFilter] = a;
  
GetRBit:
  lda_absx(PseudoRandomBitReg); // get first part of LSFR
  and_imm(0b00000111); // mask out all but 3 LSB
  
ChkRBit:
  tay(); // use as offset
  lda_absy(Bitmasks); // load bitmask
  bit_abs_fz(BitMFilter); // perform AND on filter without changing it
  if (zero_flag) { goto AddFBit; }
  iny(); // increment offset
  tya();
  and_imm(0b00000111); // mask out all but 3 LSB thus keeping it 0-7
  goto ChkRBit; // do another check
  
AddFBit:
  ora_abs(BitMFilter); // add bit to already set bits in filter
  ram[BitMFilter] = a; // and store
  lda_absy_fzn(Enemy17YPosData); // load vertical position using offset
  cpu_call_begin(0xc6f1); PutAtRightExtent(); cpu_call_end(); // set vertical position and other values
  ram[Enemy_YMF_Dummy + x] = a; // initialize dummy variable
  lda_imm(0x20); // set timer
  ram[FrenzyEnemyTimer] = a;
  CheckpointEnemyID(); return; // process our new enemy object
  
DoBulletBills:
  ldy_imm(0xff); // start at beginning of enemy slots
  
BB_SLoop:
  iny(); // move onto the next slot
  cpy_imm_fc(0x5); // branch to play sound if we've done all slots
  if (carry_flag) { goto FireBulletBill; }
  lda_zpy_fz(Enemy_Flag); // if enemy buffer flag not set,
  if (zero_flag) { goto BB_SLoop; } // loop back and check another slot
  lda_zpy(Enemy_ID);
  cmp_imm_fczn(BulletBill_FrenzyVar); // check enemy identifier for
  if (!zero_flag) { goto BB_SLoop; } // bullet bill object (frenzy variant)
  // ExF17:
  return; // if found, leave
  
FireBulletBill:
  lda_zp(Square2SoundQueue);
  ora_imm(Sfx_Blast); // play fireworks/gunfire sound
  ram[Square2SoundQueue] = a;
  lda_imm(BulletBill_FrenzyVar); // load identifier for bullet bill object
  goto Set17ID; // unconditional branch
  // --------------------------------
  // $00 - used to store Y position of group enemies
  // $01 - used to store enemy ID
  // $02 - used to store page location of right side of screen
  // $03 - used to store X position of right side of screen
}

void InitPiranhaPlant(void) {
  lda_imm(0x1); // set initial speed
  ram[PiranhaPlant_Y_Speed + x] = a;
  lsr_acc();
  ram[Enemy_State + x] = a; // initialize enemy state and what would normally
  ram[PiranhaPlant_MoveFlag + x] = a; // be used as vertical speed, but not in this case
  lda_zpx(Enemy_Y_Position);
  ram[PiranhaPlantDownYPos + x] = a; // save original vertical coordinate here
  carry_flag = true;
  sbc_imm_fc(0x18);
  ram[PiranhaPlantUpYPos + x] = a; // save original vertical coordinate - 24 pixels here
  lda_imm_fzn(0x9);
  goto SetBBox2; // set specific value for bounding box control
  // --------------------------------
  
SetBBox2:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control then leave
  return;
  // --------------------------------
}

void InitEnemyFrenzy(void) {
  lda_zpx(Enemy_ID); // load enemy identifier
  ram[EnemyFrenzyBuffer] = a; // save in enemy frenzy buffer
  carry_flag = true;
  sbc_imm_fczn(0x12); // subtract 12 and use as offset for jump engine
  cpu_call_begin(0xc7aa);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xc3a4: LakituAndSpinyHandler(); return;
    case 0xc7b7: NoFrenzyCode(); return;
    case 0xc4a8: InitFlyingCheepCheep(); return;
    case 0xc5a3: InitBowserFlame(); return;
    case 0xc63d: InitFireworks(); return;
    case 0xc69c: BulletBillCheepCheep(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void NoFrenzyCode(void) {
  return;
  // --------------------------------
}

void EndFrenzy(void) {
  ldy_imm(0x5); // start at last slot
  
LakituChk:
  lda_zpy(Enemy_ID); // check enemy identifiers
  cmp_imm_fcz(Lakitu); // for lakitu
  if (zero_flag) {
    lda_imm(0x1); // if found, set state
    ram[Enemy_State + y] = a;
  }
  // NextFSlot:
  dey_fn(); // move onto the next slot
  if (!neg_flag) { goto LakituChk; } // do this until all slots are checked
  lda_imm_fzn(0x0);
  ram[EnemyFrenzyBuffer] = a; // empty enemy frenzy buffer
  ram[Enemy_Flag + x] = a; // disable enemy buffer flag for this object
  return;
  // --------------------------------
}

void InitJumpGPTroopa(void) {
  lda_imm(0x2); // set for movement to the left
  ram[Enemy_MovingDir + x] = a;
  lda_imm(0xf8); // set horizontal speed
  ram[Enemy_X_Speed + x] = a;
  // TallBBox2:
  lda_imm_fzn(0x3); // set specific value for bounding box control
  // SetBBox2:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control then leave
  return;
  // --------------------------------
}

void InitBalPlatform(void) {
  dec_zpx(Enemy_Y_Position); // raise vertical position by two pixels
  dec_zpx(Enemy_Y_Position);
  ldy_abs_fz(SecondaryHardMode); // if secondary hard mode flag not set,
  // branch ahead
  if (zero_flag) {
    ldy_imm_fzn(0x2); // otherwise set value here
    cpu_call_begin(0xc7ec); PosPlatform(); cpu_call_end(); // do a sub to add or subtract pixels
  }
  // AlignP:
  ldy_imm(0xff); // set default value here for now
  lda_abs_fn(BalPlatformAlignment); // get current balance platform alignment
  ram[Enemy_State + x] = a; // set platform alignment to object state here
  // if old alignment $ff, put $ff as alignment for negative
  if (neg_flag) {
    txa(); // if old contents already $ff, put
    tay(); // object offset as alignment to make next positive
  }
  // SetBPA:
  ram[BalPlatformAlignment] = y; // store whatever value's in Y here
  lda_imm(0x0);
  ram[Enemy_MovingDir + x] = a; // init moving direction
  tay_fzn(); // init Y
  cpu_call_begin(0xc802); PosPlatform(); cpu_call_end(); // do a sub to add 8 pixels, then run shared code here
  // --------------------------------
  InitDropPlatform(); // fallthrough
  return;
}

void InitDropPlatform(void) {
  lda_imm_fzn(0xff);
  ram[PlatformCollisionFlag + x] = a; // set some value here
  goto CommonPlatCode; // then jump ahead to execute more code
  // --------------------------------
  
CommonPlatCode:
  cpu_call_begin(0xc82a); InitVStf(); cpu_call_end(); // do a sub to init certain other values 
  // SPBBox:
  lda_imm(0x5); // set default bounding box size control
  ldy_abs(AreaType);
  cpy_imm_fczn(0x3); // check for castle-type level
  // use default value if found
  if (!zero_flag) {
    ldy_abs_fzn(SecondaryHardMode); // otherwise check for secondary hard mode flag
    // if set, use default value
    if (zero_flag) {
      lda_imm_fzn(0x6); // use alternate value if not castle or secondary not set
    }
  }
  // CasPBB:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control here and leave
  return;
  // --------------------------------
}

void InitHoriPlatform(void) {
  lda_imm_fzn(0x0);
  ram[XMoveSecondaryCounter + x] = a; // init one of the moving counters
  goto CommonPlatCode; // jump ahead to execute more code
  // --------------------------------
  
CommonPlatCode:
  cpu_call_begin(0xc82a); InitVStf(); cpu_call_end(); // do a sub to init certain other values 
  // SPBBox:
  lda_imm(0x5); // set default bounding box size control
  ldy_abs(AreaType);
  cpy_imm_fczn(0x3); // check for castle-type level
  // use default value if found
  if (!zero_flag) {
    ldy_abs_fzn(SecondaryHardMode); // otherwise check for secondary hard mode flag
    // if set, use default value
    if (zero_flag) {
      lda_imm_fzn(0x6); // use alternate value if not castle or secondary not set
    }
  }
  // CasPBB:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control here and leave
  return;
  // --------------------------------
}

void InitVertPlatform(void) {
  ldy_imm(0x40); // set default value here
  lda_zpx_fn(Enemy_Y_Position); // check vertical position
  // if above a certain point, skip this part
  if (neg_flag) {
    eor_imm(0xff);
    carry_flag = false; // otherwise get two's compliment
    adc_imm(0x1);
    ldy_imm(0xc0); // get alternate value to add to vertical position
  }
  // SetYO:
  ram[YPlatformTopYPos + x] = a; // save as top vertical position
  tya();
  carry_flag = false; // load value from earlier, add number of pixels 
  adc_zpx_fczn(Enemy_Y_Position); // to vertical position
  ram[YPlatformCenterYPos + x] = a; // save result as central vertical position
  // --------------------------------
  // CommonPlatCode:
  cpu_call_begin(0xc82a); InitVStf(); cpu_call_end(); // do a sub to init certain other values 
  // SPBBox:
  lda_imm(0x5); // set default bounding box size control
  ldy_abs(AreaType);
  cpy_imm_fczn(0x3); // check for castle-type level
  // use default value if found
  if (!zero_flag) {
    ldy_abs_fzn(SecondaryHardMode); // otherwise check for secondary hard mode flag
    // if set, use default value
    if (zero_flag) {
      lda_imm_fzn(0x6); // use alternate value if not castle or secondary not set
    }
  }
  // CasPBB:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control here and leave
  return;
  // --------------------------------
}

void LargeLiftUp(void) {
  goto LargeLiftUp;
  
SPBBox:
  lda_imm(0x5); // set default bounding box size control
  ldy_abs(AreaType);
  cpy_imm_fczn(0x3); // check for castle-type level
  // use default value if found
  if (!zero_flag) {
    ldy_abs_fzn(SecondaryHardMode); // otherwise check for secondary hard mode flag
    // if set, use default value
    if (zero_flag) {
      lda_imm_fzn(0x6); // use alternate value if not castle or secondary not set
    }
  }
  // CasPBB:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control here and leave
  return;
  // --------------------------------
  
LargeLiftUp:
  cpu_call_begin(0xc841); PlatLiftUp(); cpu_call_end(); // execute code for platforms going up
  goto LargeLiftBBox; // overwrite bounding box for large platforms
  
LargeLiftBBox:
  goto SPBBox; // jump to overwrite bounding box size control
  // --------------------------------
}

void LargeLiftDown(void) {
  goto LargeLiftDown;
  
SPBBox:
  lda_imm(0x5); // set default bounding box size control
  ldy_abs(AreaType);
  cpy_imm_fczn(0x3); // check for castle-type level
  // use default value if found
  if (!zero_flag) {
    ldy_abs_fzn(SecondaryHardMode); // otherwise check for secondary hard mode flag
    // if set, use default value
    if (zero_flag) {
      lda_imm_fzn(0x6); // use alternate value if not castle or secondary not set
    }
  }
  // CasPBB:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control here and leave
  return;
  // --------------------------------
  
LargeLiftDown:
  cpu_call_begin(0xc847); PlatLiftDown(); cpu_call_end(); // execute code for platforms going down
  // LargeLiftBBox:
  goto SPBBox; // jump to overwrite bounding box size control
  // --------------------------------
}

void PlatLiftUp(void) {
  lda_imm(0x10); // set movement amount here
  ram[Enemy_Y_MoveForce + x] = a;
  lda_imm(0xff); // set moving speed for platforms going up
  ram[Enemy_Y_Speed + x] = a;
  goto CommonSmallLift; // skip ahead to part we should be executing
  // --------------------------------
  
CommonSmallLift:
  ldy_imm_fzn(0x1);
  cpu_call_begin(0xc864); PosPlatform(); cpu_call_end(); // do a sub to add 12 pixels due to preset value  
  lda_imm_fzn(0x4);
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control for small platforms
  return;
  // --------------------------------
}

void PlatLiftDown(void) {
  lda_imm(0xf0); // set movement amount here
  ram[Enemy_Y_MoveForce + x] = a;
  lda_imm(0x0); // set moving speed for platforms going down
  ram[Enemy_Y_Speed + x] = a;
  // --------------------------------
  // CommonSmallLift:
  ldy_imm_fzn(0x1);
  cpu_call_begin(0xc864); PosPlatform(); cpu_call_end(); // do a sub to add 12 pixels due to preset value  
  lda_imm_fzn(0x4);
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control for small platforms
  return;
  // --------------------------------
}

void PosPlatform(void) {
  lda_zpx(Enemy_X_Position); // get horizontal coordinate
  carry_flag = false;
  adc_absy_fc(PlatPosDataLow); // add or subtract pixels depending on offset
  ram[Enemy_X_Position + x] = a; // store as new horizontal coordinate
  lda_zpx(Enemy_PageLoc);
  adc_absy_fczn(PlatPosDataHigh); // add or subtract page location depending on offset
  ram[Enemy_PageLoc + x] = a; // store as new page location
  return; // and go back
  // --------------------------------
}

void EndOfEnemyInitCode(void) {
  return;
  // -------------------------------------------------------------------------------------
}

void NoRunCode(void) {
  return;
  // --------------------------------
}

void RunRetainerObj(void) {
  cpu_call_begin(0xc8d9); GetEnemyOffscreenBits(); cpu_call_end();
  cpu_call_begin(0xc8dc); RelativeEnemyPosition(); cpu_call_end();
  EnemyGfxHandler(); return;
  // --------------------------------
}

void RunNormalEnemies(void) {
  lda_imm_fzn(0x0); // init sprite attributes
  ram[Enemy_SprAttrib + x] = a;
  cpu_call_begin(0xc8e7); GetEnemyOffscreenBits(); cpu_call_end();
  cpu_call_begin(0xc8ea); RelativeEnemyPosition(); cpu_call_end();
  cpu_call_begin(0xc8ed); EnemyGfxHandler(); cpu_call_end();
  cpu_call_begin(0xc8f0); GetEnemyBoundBox(); cpu_call_end();
  cpu_call_begin(0xc8f3); EnemyToBGCollisionDet(); cpu_call_end();
  cpu_call_begin(0xc8f6); EnemiesCollision(); cpu_call_end();
  cpu_call_begin(0xc8f9); PlayerEnemyCollision(); cpu_call_end();
  ldy_abs_fzn(TimerControl); // if master timer control set, skip to last routine
  if (zero_flag) {
    cpu_call_begin(0xc901); EnemyMovementSubs(); cpu_call_end();
  }
  // SkipMove:
  OffscreenBoundsCheck(); return;
}

void EnemyMovementSubs(void) {
  lda_zpx_fzn(Enemy_ID);
  cpu_call_begin(0xc909);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xca77: MoveNormalEnemy(); return;
    case 0xc9d8: ProcHammerBro(); return;
    case 0xcb89: MoveBloober(); return;
    case 0xcc36: MoveBulletBill(); return;
    case 0xc934: NoMoveCode(); return;
    case 0xcc4a: MoveSwimmingCheepCheep(); return;
    case 0xc9b0: MovePodoboo(); return;
    case 0xd3b0: MovePiranhaPlant(); return;
    case 0xcaf9: MoveJumpingEnemy(); return;
    case 0xcaff: ProcMoveRedPTroopa(); return;
    case 0xcb25: MoveFlyGreenPTroopa(); return;
    case 0xcf28: MoveLakitu(); return;
    case 0xcedf: MoveFlyingCheepCheep(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void NoMoveCode(void) {
  return;
  // --------------------------------
}

void RunBowserFlame(void) {
  cpu_call_begin(0xc937); ProcBowserFlame(); cpu_call_end();
  cpu_call_begin(0xc93a); GetEnemyOffscreenBits(); cpu_call_end();
  cpu_call_begin(0xc93d); RelativeEnemyPosition(); cpu_call_end();
  cpu_call_begin(0xc940); GetEnemyBoundBox(); cpu_call_end();
  cpu_call_begin(0xc943); PlayerEnemyCollision(); cpu_call_end();
  OffscreenBoundsCheck(); return;
  // --------------------------------
}

void RunFirebarObj(void) {
  cpu_call_begin(0xc949); ProcFirebar(); cpu_call_end();
  OffscreenBoundsCheck(); return;
  // --------------------------------
}

void RunSmallPlatform(void) {
  cpu_call_begin(0xc94f); GetEnemyOffscreenBits(); cpu_call_end();
  cpu_call_begin(0xc952); RelativeEnemyPosition(); cpu_call_end();
  cpu_call_begin(0xc955); SmallPlatformBoundBox(); cpu_call_end();
  cpu_call_begin(0xc958); SmallPlatformCollision(); cpu_call_end();
  cpu_call_begin(0xc95b); RelativeEnemyPosition(); cpu_call_end();
  cpu_call_begin(0xc95e); DrawSmallPlatform(); cpu_call_end();
  cpu_call_begin(0xc961); MoveSmallPlatform(); cpu_call_end();
  OffscreenBoundsCheck(); return;
  // --------------------------------
}

void RunLargePlatform(void) {
  cpu_call_begin(0xc967); GetEnemyOffscreenBits(); cpu_call_end();
  cpu_call_begin(0xc96a); RelativeEnemyPosition(); cpu_call_end();
  cpu_call_begin(0xc96d); LargePlatformBoundBox(); cpu_call_end();
  cpu_call_begin(0xc970); LargePlatformCollision(); cpu_call_end();
  lda_abs_fzn(TimerControl); // if master timer control set,
  // skip subroutine tree
  if (zero_flag) {
    cpu_call_begin(0xc978); LargePlatformSubroutines(); cpu_call_end();
  }
  // SkipPT:
  cpu_call_begin(0xc97b); RelativeEnemyPosition(); cpu_call_end();
  cpu_call_begin(0xc97e); DrawLargePlatform(); cpu_call_end();
  OffscreenBoundsCheck(); return;
  // --------------------------------
}

void LargePlatformSubroutines(void) {
  lda_zpx(Enemy_ID); // subtract $24 to get proper offset for jump table
  carry_flag = true;
  sbc_imm_fczn(0x24);
  cpu_call_begin(0xc989);
  asl_acc_fczn();
  tay_fzn();
  pla_fzn();
  ram[0x4] = a;
  pla_fzn();
  ram[0x5] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x6] = a;
  iny_fzn();
  lda_indy_fzn(0x4);
  ram[0x7] = a;
  switch (read_word(0x6)) {
    case 0xd432: BalancePlatform(); return;
    case 0xd5d3: YMovingPlatform(); return;
    case 0xd64f: MoveLargeLiftPlat(); return;
    case 0xd607: XMovingPlatform(); return;
    case 0xd631: DropPlatform(); return;
    case 0xd63d: RightPlatform(); return;
    default: cpu_unresolved_jump(read_word(0x6)); return;
  }
}

void EraseEnemyObject(void) {
  lda_imm_fzn(0x0); // clear all enemy object variables
  ram[Enemy_Flag + x] = a;
  ram[Enemy_ID + x] = a;
  ram[Enemy_State + x] = a;
  ram[FloateyNum_Control + x] = a;
  ram[EnemyIntervalTimer + x] = a;
  ram[ShellChainCounter + x] = a;
  ram[Enemy_SprAttrib + x] = a;
  ram[EnemyFrameTimer + x] = a;
  return;
  // -------------------------------------------------------------------------------------
}

void MovePodoboo(void) {
  lda_absx_fzn(EnemyIntervalTimer); // check enemy timer
  // branch to move enemy if not expired
  if (zero_flag) {
    cpu_call_begin(0xc9b7); InitPodoboo(); cpu_call_end(); // otherwise set up podoboo again
    lda_absx(PseudoRandomBitReg + 1); // get part of LSFR
    ora_imm(0b10000000); // set d7
    ram[Enemy_Y_MoveForce + x] = a; // store as movement force
    and_imm(0b00001111); // mask out high nybble
    ora_imm(0x6); // set for at least six intervals
    ram[EnemyIntervalTimer + x] = a; // store as new enemy timer
    lda_imm(0xf9);
    ram[Enemy_Y_Speed + x] = a; // set vertical speed to move podoboo upwards
  }
  // PdbM:
  MoveJ_EnemyVertically(); return; // branch to impose gravity on podoboo
  // --------------------------------
  // $00 - used in HammerBroJumpCode as bitmask
}

void ProcHammerBro(void) {
  // ProcHammerBro:
  lda_zpx(Enemy_State); // check hammer bro's enemy state for d5 set
  and_imm_fzn(0b00100000);
  if (zero_flag) { goto ChkJH; } // if not set, go ahead with code
  goto MoveDefeatedEnemy; // otherwise jump to something else
  
ChkJH:
  lda_zpx_fz(HammerBroJumpTimer); // check jump timer
  if (zero_flag) { goto HammerBroJumpCode; } // if expired, branch to jump
  dec_zpx(HammerBroJumpTimer); // otherwise decrement jump timer
  lda_abs(Enemy_OffscreenBits);
  and_imm_fz(0b00001100); // check offscreen bits
  if (!zero_flag) { goto MoveHammerBroXDir; } // if hammer bro a little offscreen, skip to movement code
  lda_absx_fz(HammerThrowingTimer); // check hammer throwing timer
  if (!zero_flag) { goto DecHT; } // if not expired, skip ahead, do not throw hammer
  ldy_abs(SecondaryHardMode); // otherwise get secondary hard mode flag
  lda_absy_fzn(HammerThrowTmrData); // get timer data using flag as offset
  ram[HammerThrowingTimer + x] = a; // set as new timer
  cpu_call_begin(0xc9fe); SpawnHammerObj(); cpu_call_end(); // do a sub here to spawn hammer object
  if (!carry_flag) { goto DecHT; } // if carry clear, hammer not spawned, skip to decrement timer
  lda_zpx(Enemy_State);
  ora_imm(0b00001000); // set d3 in enemy state for hammer throw
  ram[Enemy_State + x] = a;
  goto MoveHammerBroXDir; // jump to move hammer bro
  
DecHT:
  dec_absx(HammerThrowingTimer); // decrement timer
  goto MoveHammerBroXDir; // jump to move hammer bro
  
HammerBroJumpCode:
  lda_zpx(Enemy_State); // get hammer bro's enemy state
  and_imm(0b00000111); // mask out all but 3 LSB
  cmp_imm_fcz(0x1); // check for d0 set (for jumping)
  if (zero_flag) { goto MoveHammerBroXDir; } // if set, branch ahead to moving code
  lda_imm(0x0); // load default value here
  ram[0x0] = a; // save into temp variable for now
  ldy_imm(0xfa); // set default vertical speed
  lda_zpx_fn(Enemy_Y_Position); // check hammer bro's vertical coordinate
  if (neg_flag) { goto SetHJ; } // if on the bottom half of the screen, use current speed
  ldy_imm(0xfd); // otherwise set alternate vertical speed
  cmp_imm_fc(0x70); // check to see if hammer bro is above the middle of screen
  inc_zp(0x0); // increment preset value to $01
  if (!carry_flag) { goto SetHJ; } // if above the middle of the screen, use current speed and $01
  dec_zp(0x0); // otherwise return value to $00
  lda_absx(PseudoRandomBitReg + 1); // get part of LSFR, mask out all but LSB
  and_imm_fz(0x1);
  if (!zero_flag) { goto SetHJ; } // if d0 of LSFR set, branch and use current speed and $00
  ldy_imm(0xfa); // otherwise reset to default vertical speed
  
SetHJ:
  ram[Enemy_Y_Speed + x] = y; // set vertical speed for jumping
  lda_zpx(Enemy_State); // set d0 in enemy state for jumping
  ora_imm(0x1);
  ram[Enemy_State + x] = a;
  lda_zp(0x0); // load preset value here to use as bitmask
  and_absx(PseudoRandomBitReg + 2); // and do bit-wise comparison with part of LSFR
  tay(); // then use as offset
  lda_abs_fz(SecondaryHardMode); // check secondary hard mode flag
  if (!zero_flag) { goto HJump; }
  tay(); // if secondary hard mode flag clear, set offset to 0
  
HJump:
  lda_absy(HammerBroJumpLData); // get jump length timer data using offset from before
  ram[EnemyFrameTimer + x] = a; // save in enemy timer
  lda_absx(PseudoRandomBitReg + 1);
  ora_imm(0b11000000); // get contents of part of LSFR, set d7 and d6, then
  ram[HammerBroJumpTimer + x] = a; // store in jump timer
  
MoveHammerBroXDir:
  ldy_imm(0xfc); // move hammer bro a little to the left
  lda_zp(FrameCounter);
  and_imm_fz(0b01000000); // change hammer bro's direction every 64 frames
  if (!zero_flag) { goto Shimmy; }
  ldy_imm(0x4); // if d6 set in counter, move him a little to the right
  
Shimmy:
  ram[Enemy_X_Speed + x] = y; // store horizontal speed
  ldy_imm_fzn(0x1); // set to face right by default
  cpu_call_begin(0xca68); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and hammer bro
  if (neg_flag) { goto SetShim; } // if enemy to the left of player, skip this part
  iny(); // set to face left
  lda_absx_fz(EnemyIntervalTimer); // check walking timer
  if (!zero_flag) { goto SetShim; } // if not yet expired, skip to set moving direction
  lda_imm(0xf8);
  ram[Enemy_X_Speed + x] = a; // otherwise, make the hammer bro walk left towards player
  
SetShim:
  ram[Enemy_MovingDir + x] = y; // set moving direction
  MoveNormalEnemy(); // fallthrough
  return;
  
MoveDefeatedEnemy:
  cpu_call_begin(0xcae7); MoveD_EnemyVertically(); cpu_call_end(); // execute sub to move defeated enemy downwards
  MoveEnemyHorizontally(); return; // now move defeated enemy horizontally
}

void MoveNormalEnemy(void) {
  // MoveNormalEnemy:
  ldy_imm(0x0); // init Y to leave horizontal movement as-is 
  lda_zpx(Enemy_State);
  and_imm_fzn(0b01000000); // check enemy state for d6 set, if set skip
  if (!zero_flag) { goto FallE; } // to move enemy vertically, then horizontally if necessary
  lda_zpx(Enemy_State);
  asl_acc_fc(); // check enemy state for d7 set
  if (carry_flag) { goto SteadM; } // if set, branch to move enemy horizontally
  lda_zpx(Enemy_State);
  and_imm_fzn(0b00100000); // check enemy state for d5 set
  if (!zero_flag) { goto MoveDefeatedEnemy; } // if set, branch to move defeated enemy object
  lda_zpx(Enemy_State);
  and_imm_fz(0b00000111); // check d2-d0 of enemy state for any set bits
  if (zero_flag) { goto SteadM; } // if enemy in normal state, branch to move enemy horizontally
  cmp_imm_fczn(0x5);
  if (zero_flag) { goto FallE; } // if enemy in state used by spiny's egg, go ahead here
  cmp_imm_fczn(0x3);
  if (carry_flag) { goto ReviveStunned; } // if enemy in states $03 or $04, skip ahead to yet another part
  
FallE:
  cpu_call_begin(0xca9a); MoveD_EnemyVertically(); cpu_call_end(); // do a sub here to move enemy downwards
  ldy_imm(0x0);
  lda_zpx(Enemy_State); // check for enemy state $02
  cmp_imm_fcz(0x2);
  if (zero_flag) { goto MEHor; } // if found, branch to move enemy horizontally
  and_imm_fz(0b01000000); // check for d6 set
  if (zero_flag) { goto SteadM; } // if not set, branch to something else
  lda_zpx(Enemy_ID);
  cmp_imm_fcz(PowerUpObject); // check for power-up object
  if (zero_flag) { goto SteadM; }
  if (!zero_flag) { goto SlowM; } // if any other object where d6 set, jump to set Y
  
MEHor:
  MoveEnemyHorizontally(); return; // jump here to move enemy horizontally for <> $2e and d6 set
  
SlowM:
  ldy_imm(0x1); // if branched here, increment Y to slow horizontal movement
  
SteadM:
  lda_zpx_fn(Enemy_X_Speed); // get current horizontal speed
  pha(); // save to stack
  if (!neg_flag) { goto AddHS; } // if not moving or moving right, skip, leave Y alone
  iny();
  iny(); // otherwise increment Y to next data
  
AddHS:
  carry_flag = false;
  adc_absy_fczn(XSpeedAdderData); // add value here to slow enemy down if necessary
  ram[Enemy_X_Speed + x] = a; // save as horizontal speed temporarily
  cpu_call_begin(0xcac3); MoveEnemyHorizontally(); cpu_call_end(); // then do a sub to move horizontally
  pla_fzn();
  ram[Enemy_X_Speed + x] = a; // get old horizontal speed from stack and return to
  return; // original memory location, then leave
  
ReviveStunned:
  lda_absx_fz(EnemyIntervalTimer); // if enemy timer not expired yet,
  if (!zero_flag) { goto ChkKillGoomba; } // skip ahead to something else
  ram[Enemy_State + x] = a; // otherwise initialize enemy state to normal
  lda_zp(FrameCounter);
  and_imm(0x1); // get d0 of frame counter
  tay(); // use as Y and increment for movement direction
  iny();
  ram[Enemy_MovingDir + x] = y; // store as pseudorandom movement direction
  dey(); // decrement for use as pointer
  lda_abs_fz(PrimaryHardMode); // check primary hard mode flag
  if (zero_flag) { goto SetRSpd; } // if not set, use pointer as-is
  iny();
  iny(); // otherwise increment 2 bytes to next data
  
SetRSpd:
  lda_absy_fzn(RevivedXSpeed); // load and store new horizontal speed
  ram[Enemy_X_Speed + x] = a; // and leave
  return;
  
MoveDefeatedEnemy:
  cpu_call_begin(0xcae7); MoveD_EnemyVertically(); cpu_call_end(); // execute sub to move defeated enemy downwards
  MoveEnemyHorizontally(); return; // now move defeated enemy horizontally
  
ChkKillGoomba:
  cmp_imm_fczn(0xe); // check to see if enemy timer has reached
  if (!zero_flag) { return; } // a certain point, and branch to leave if not
  lda_zpx(Enemy_ID);
  cmp_imm_fczn(Goomba); // check for goomba object
  if (!zero_flag) { return; } // branch if not found
  cpu_call_begin(0xcaf7); EraseEnemyObject(); cpu_call_end(); // otherwise, kill this goomba object
  // NKGmba:
  return; // leave!
  // --------------------------------
}

void MoveJumpingEnemy(void) {
  cpu_call_begin(0xcafb); MoveJ_EnemyVertically(); cpu_call_end(); // do a sub to impose gravity on green paratroopa
  MoveEnemyHorizontally(); return; // jump to move enemy horizontally
  // --------------------------------
}

void ProcMoveRedPTroopa(void) {
  goto ProcMoveRedPTroopa;
  
MoveRedPTroopaDown:
  ldy_imm(0x0); // set Y to move downwards
  goto MoveRedPTroopa; // skip to movement routine
  
MoveRedPTroopaUp:
  ldy_imm(0x1); // set Y to move upwards
  
MoveRedPTroopa:
  inx(); // increment X for enemy offset
  lda_imm(0x3);
  ram[0x0] = a; // set downward movement amount here
  lda_imm(0x6);
  ram[0x1] = a; // set upward movement amount here
  lda_imm(0x2);
  ram[0x2] = a; // set maximum speed here
  tya_fzn(); // set movement direction in A, and
  goto RedPTroopaGrav; // jump to move this thing
  // --------------------------------
  
RedPTroopaGrav:
  cpu_call_begin(0xbfd3); ImposeGravity(); cpu_call_end(); // do a sub to move object gradually
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used for downward force
  // $01 - used for upward force
  // $07 - used as adder for vertical position
  
ProcMoveRedPTroopa:
  lda_zpx(Enemy_Y_Speed);
  ora_absx_fz(Enemy_Y_MoveForce); // check for any vertical force or speed
  if (!zero_flag) { goto MoveRedPTUpOrDown; } // branch if any found
  ram[Enemy_YMF_Dummy + x] = a; // initialize something here
  lda_zpx(Enemy_Y_Position); // check current vs. original vertical coordinate
  cmp_absx_fc(RedPTroopaOrigXPos);
  if (carry_flag) { goto MoveRedPTUpOrDown; } // if current => original, skip ahead to more code
  lda_zp(FrameCounter); // get frame counter
  and_imm_fzn(0b00000111); // mask out all but 3 LSB
  if (!zero_flag) { return; } // if any bits set, branch to leave
  inc_zpx_fzn(Enemy_Y_Position); // otherwise increment red paratroopa's vertical position
  // NoIncPT:
  return; // leave
  
MoveRedPTUpOrDown:
  lda_zpx(Enemy_Y_Position); // check current vs. central vertical coordinate
  cmp_zpx_fc(RedPTroopaCenterYPos);
  if (!carry_flag) { goto MovPTDwn; } // if current < central, jump to move downwards
  goto MoveRedPTroopaUp; // otherwise jump to move upwards
  
MovPTDwn:
  goto MoveRedPTroopaDown; // move downwards
  // --------------------------------
  // $00 - used to store adder for movement, also used as adder for platform
  // $01 - used to store maximum value for secondary counter
}

void MoveFlyGreenPTroopa(void) {
  cpu_call_begin(0xcb27); XMoveCntr_GreenPTroopa(); cpu_call_end(); // do sub to increment primary and secondary counters
  cpu_call_begin(0xcb2a); MoveWithXMCntrs(); cpu_call_end(); // do sub to move green paratroopa accordingly, and horizontally
  ldy_imm(0x1); // set Y to move green paratroopa down
  lda_zp(FrameCounter);
  and_imm_fzn(0b00000011); // check frame counter 2 LSB for any bits set
  if (zero_flag) {
    lda_zp(FrameCounter);
    and_imm_fz(0b01000000); // check frame counter for d6 set
    // branch to move green paratroopa down if set
    if (zero_flag) {
      ldy_imm(0xff); // otherwise set Y to move green paratroopa up
    }
    // YSway:
    ram[0x0] = y; // store adder here
    lda_zpx(Enemy_Y_Position);
    carry_flag = false; // add or subtract from vertical position
    adc_zp_fczn(0x0); // to give green paratroopa a wavy flight
    ram[Enemy_Y_Position + x] = a;
    // NoMGPT:
    return; // leave!
  }
}

void XMoveCntr_GreenPTroopa(void) {
  lda_imm(0x13); // load preset maximum value for secondary counter
  XMoveCntr_Platform(); // fallthrough
  return;
}

void XMoveCntr_Platform(void) {
  ram[0x1] = a; // store value here
  lda_zp(FrameCounter);
  and_imm_fzn(0b00000011); // branch to leave if not on
  if (zero_flag) {
    ldy_zpx(XMoveSecondaryCounter); // get secondary counter
    lda_zpx(XMovePrimaryCounter); // get primary counter
    lsr_acc_fc();
    // if d0 of primary counter set, branch elsewhere
    if (!carry_flag) {
      cpy_zp_fcz(0x1); // compare secondary counter to preset maximum value
      if (zero_flag) { goto IncPXM; } // if equal, branch ahead of this part
      inc_zpx_fzn(XMoveSecondaryCounter); // increment secondary counter and leave
      // NoIncXM:
      return;
      
IncPXM:
      inc_zpx_fzn(XMovePrimaryCounter); // increment primary counter and leave
      return;
    }
    // DecSeXM:
    tya_fz(); // put secondary counter in A
    if (zero_flag) { goto IncPXM; } // if secondary counter at zero, branch back
    dec_zpx_fzn(XMoveSecondaryCounter); // otherwise decrement secondary counter and leave
    return;
  }
}

void MoveWithXMCntrs(void) {
  lda_zpx(XMoveSecondaryCounter); // save secondary counter to stack
  pha();
  ldy_imm(0x1); // set value here by default
  lda_zpx(XMovePrimaryCounter);
  and_imm_fzn(0b00000010); // if d1 of primary counter is
  // set, branch ahead of this part here
  if (zero_flag) {
    lda_zpx(XMoveSecondaryCounter);
    eor_imm(0xff); // otherwise change secondary
    carry_flag = false; // counter to two's compliment
    adc_imm_fc(0x1);
    ram[XMoveSecondaryCounter + x] = a;
    ldy_imm_fzn(0x2); // load alternate value here
  }
  // XMRight:
  ram[Enemy_MovingDir + x] = y; // store as moving direction
  cpu_call_begin(0xcb80); MoveEnemyHorizontally(); cpu_call_end();
  ram[0x0] = a; // save value obtained from sub here
  pla_fzn(); // get secondary counter from stack
  ram[XMoveSecondaryCounter + x] = a; // and return to original place
  return;
  // --------------------------------
}

void MoveBloober(void) {
  // MoveBloober:
  lda_zpx(Enemy_State);
  and_imm_fz(0b00100000); // check enemy state for d5 set
  if (!zero_flag) { goto MoveDefeatedBloober; } // branch if set to move defeated bloober
  ldy_abs(SecondaryHardMode); // use secondary hard mode flag as offset
  lda_absx(PseudoRandomBitReg + 1); // get LSFR
  and_absy_fzn(BlooberBitmasks); // mask out bits in LSFR using bitmask loaded with offset
  if (!zero_flag) { goto BlooberSwim; } // if any bits set, skip ahead to make swim
  txa();
  lsr_acc_fc(); // check to see if on second or fourth slot (1 or 3)
  if (!carry_flag) { goto FBLeft; } // if not, branch to figure out moving direction
  ldy_zp_fzn(Player_MovingDir); // otherwise, load player's moving direction and
  if (carry_flag) { goto SBMDir; } // do an unconditional branch to set
  
FBLeft:
  ldy_imm_fzn(0x2); // set left moving direction by default
  cpu_call_begin(0xcba6); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and bloober
  if (!neg_flag) { goto SBMDir; } // if enemy to the right of player, keep left
  dey_fzn(); // otherwise decrement to set right moving direction
  
SBMDir:
  ram[Enemy_MovingDir + x] = y; // set moving direction of bloober, then continue on here
  
BlooberSwim:
  cpu_call_begin(0xcbae); ProcSwimmingB(); cpu_call_end(); // execute sub to make bloober swim characteristically
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  carry_flag = true;
  sbc_absx(Enemy_Y_MoveForce); // subtract movement force
  cmp_imm_fc(0x20); // check to see if position is above edge of status bar
  if (!carry_flag) { goto SwimX; } // if so, don't do it
  ram[Enemy_Y_Position + x] = a; // otherwise, set new vertical position, make bloober swim
  
SwimX:
  ldy_zpx(Enemy_MovingDir); // check moving direction
  dey_fz();
  if (!zero_flag) { goto LeftSwim; } // if moving to the left, branch to second part
  lda_zpx(Enemy_X_Position);
  carry_flag = false; // add movement speed to horizontal coordinate
  adc_zpx_fc(BlooperMoveSpeed);
  ram[Enemy_X_Position + x] = a; // store result as new horizontal coordinate
  lda_zpx(Enemy_PageLoc);
  adc_imm_fczn(0x0); // add carry to page location
  ram[Enemy_PageLoc + x] = a; // store as new page location and leave
  return;
  
LeftSwim:
  lda_zpx(Enemy_X_Position);
  carry_flag = true; // subtract movement speed from horizontal coordinate
  sbc_zpx_fc(BlooperMoveSpeed);
  ram[Enemy_X_Position + x] = a; // store result as new horizontal coordinate
  lda_zpx(Enemy_PageLoc);
  sbc_imm_fczn(0x0); // subtract borrow from page location
  ram[Enemy_PageLoc + x] = a; // store as new page location and leave
  return;
  
MoveDefeatedBloober:
  MoveEnemySlowVert(); return; // jump to move defeated bloober downwards
}

void ProcSwimmingB(void) {
  // ProcSwimmingB:
  lda_zpx(BlooperMoveCounter); // get enemy's movement counter
  and_imm_fz(0b00000010); // check for d1 set
  if (!zero_flag) { goto ChkForFloatdown; } // branch if set
  lda_zp(FrameCounter);
  and_imm(0b00000111); // get 3 LSB of frame counter
  pha(); // and save it to the stack
  lda_zpx(BlooperMoveCounter); // get enemy's movement counter
  lsr_acc_fc(); // check for d0 set
  if (carry_flag) { goto SlowSwim; } // branch if set
  pla_fzn(); // pull 3 LSB of frame counter from the stack
  if (!zero_flag) { return; } // branch to leave, execute code only every eighth frame
  lda_absx(Enemy_Y_MoveForce);
  carry_flag = false; // add to movement force to speed up swim
  adc_imm(0x1);
  ram[Enemy_Y_MoveForce + x] = a; // set movement force
  ram[BlooperMoveSpeed + x] = a; // set as movement speed
  cmp_imm_fczn(0x2);
  if (!zero_flag) { return; } // if certain horizontal speed, branch to leave
  inc_zpx_fzn(BlooperMoveCounter); // otherwise increment movement counter
  // BSwimE:
  return;
  
SlowSwim:
  pla_fzn(); // pull 3 LSB of frame counter from the stack
  if (!zero_flag) { return; } // branch to leave, execute code only every eighth frame
  lda_absx(Enemy_Y_MoveForce);
  carry_flag = true; // subtract from movement force to slow swim
  sbc_imm_fczn(0x1);
  ram[Enemy_Y_MoveForce + x] = a; // set movement force
  ram[BlooperMoveSpeed + x] = a; // set as movement speed
  if (!zero_flag) { return; } // if any speed, branch to leave
  inc_zpx(BlooperMoveCounter); // otherwise increment movement counter
  lda_imm_fzn(0x2);
  ram[EnemyIntervalTimer + x] = a; // set enemy's timer
  // NoSSw:
  return; // leave
  
ChkForFloatdown:
  lda_absx_fz(EnemyIntervalTimer); // get enemy timer
  if (zero_flag) { goto ChkNearPlayer; } // branch if expired
  
Floatdown:
  lda_zp(FrameCounter); // get frame counter
  lsr_acc_fczn(); // check for d0 set
  if (carry_flag) { return; } // branch to leave on every other frame
  inc_zpx_fzn(Enemy_Y_Position); // otherwise increment vertical coordinate
  // NoFD:
  return; // leave
  
ChkNearPlayer:
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  adc_imm(0x10); // add sixteen pixels
  cmp_zp_fc(Player_Y_Position); // compare result with player's vertical coordinate
  if (!carry_flag) { goto Floatdown; } // if modified vertical less than player's, branch
  lda_imm_fzn(0x0);
  ram[BlooperMoveCounter + x] = a; // otherwise nullify movement counter
  return;
  // --------------------------------
}

void MoveBulletBill(void) {
  lda_zpx(Enemy_State); // check bullet bill's enemy object state for d5 set
  and_imm_fz(0b00100000);
  // if not set, continue with movement code
  if (!zero_flag) {
    MoveJ_EnemyVertically(); return; // otherwise jump to move defeated bullet bill downwards
  }
  // NotDefB:
  lda_imm(0xe8); // set bullet bill's horizontal speed
  ram[Enemy_X_Speed + x] = a; // and move it accordingly (note: this bullet bill
  MoveEnemyHorizontally(); return; // object occurs in frenzy object $17, not from cannons)
  // --------------------------------
  // $02 - used to hold preset values
  // $03 - used to hold enemy state
}

void MoveSwimmingCheepCheep(void) {
  // MoveSwimmingCheepCheep:
  lda_zpx(Enemy_State); // check cheep-cheep's enemy object state
  and_imm_fz(0b00100000); // for d5 set
  if (zero_flag) { goto CCSwim; } // if not set, continue with movement code
  MoveEnemySlowVert(); return; // otherwise jump to move defeated cheep-cheep downwards
  
CCSwim:
  ram[0x3] = a; // save enemy state in $03
  lda_zpx(Enemy_ID); // get enemy identifier
  carry_flag = true;
  sbc_imm(0xa); // subtract ten for cheep-cheep identifiers
  tay(); // use as offset
  lda_absy(SwimCCXMoveData); // load value here
  ram[0x2] = a;
  lda_absx(Enemy_X_MoveForce); // load horizontal force
  carry_flag = true;
  sbc_zp_fc(0x2); // subtract preset value from horizontal force
  ram[Enemy_X_MoveForce + x] = a; // store as new horizontal force
  lda_zpx(Enemy_X_Position); // get horizontal coordinate
  sbc_imm_fc(0x0); // subtract borrow (thus moving it slowly)
  ram[Enemy_X_Position + x] = a; // and save as new horizontal coordinate
  lda_zpx(Enemy_PageLoc);
  sbc_imm(0x0); // subtract borrow again, this time from the
  ram[Enemy_PageLoc + x] = a; // page location, then save
  lda_imm(0x20);
  ram[0x2] = a; // save new value here
  cpx_imm_fczn(0x2); // check enemy object offset
  if (!carry_flag) { return; } // if in first or second slot, branch to leave
  lda_zpx(CheepCheepMoveMFlag); // check movement flag
  cmp_imm_fc(0x10); // if movement speed set to $00,
  if (!carry_flag) { goto CCSwimUpwards; } // branch to move upwards
  lda_absx(Enemy_YMF_Dummy);
  carry_flag = false;
  adc_zp_fc(0x2); // add preset value to dummy variable to get carry
  ram[Enemy_YMF_Dummy + x] = a; // and save dummy
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  adc_zp_fc(0x3); // add carry to it plus enemy state to slowly move it downwards
  ram[Enemy_Y_Position + x] = a; // save as new vertical coordinate
  lda_zpx(Enemy_Y_HighPos);
  adc_imm(0x0); // add carry to page location and
  goto ChkSwimYPos; // jump to end of movement code
  
CCSwimUpwards:
  lda_absx(Enemy_YMF_Dummy);
  carry_flag = true;
  sbc_zp_fc(0x2); // subtract preset value to dummy variable to get borrow
  ram[Enemy_YMF_Dummy + x] = a; // and save dummy
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  sbc_zp_fc(0x3); // subtract borrow to it plus enemy state to slowly move it upwards
  ram[Enemy_Y_Position + x] = a; // save as new vertical coordinate
  lda_zpx(Enemy_Y_HighPos);
  sbc_imm(0x0); // subtract borrow from page location
  
ChkSwimYPos:
  ram[Enemy_Y_HighPos + x] = a; // save new page location here
  ldy_imm(0x0); // load movement speed to upwards by default
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  carry_flag = true;
  sbc_absx_fn(CheepCheepOrigYPos); // subtract original coordinate from current
  if (!neg_flag) { goto YPDiff; } // if result positive, skip to next part
  ldy_imm(0x10); // otherwise load movement speed to downwards
  eor_imm(0xff);
  carry_flag = false; // get two's compliment of result
  adc_imm(0x1); // to obtain total difference of original vs. current
  
YPDiff:
  cmp_imm_fczn(0xf); // if difference between original vs. current vertical
  if (!carry_flag) { return; } // coordinates < 15 pixels, leave movement speed alone
  tya_fzn();
  ram[CheepCheepMoveMFlag + x] = a; // otherwise change movement speed
  // ExSwCC:
  return; // leave
  // --------------------------------
  // $00 - used as counter for firebar parts
  // $01 - used for oscillated high byte of spin state or to hold horizontal adder
  // $02 - used for oscillated high byte of spin state or to hold vertical adder
  // $03 - used for mirror data
  // $04 - used to store player's sprite 1 X coordinate
  // $05 - used to evaluate mirror data
  // $06 - used to store either screen X coordinate or sprite data offset
  // $07 - used to store screen Y coordinate
  // $ed - used to hold maximum length of firebar
  // $ef - used to hold high byte of spinstate
  // horizontal adder is at first byte + high byte of spinstate,
  // vertical adder is same + 8 bytes, two's compliment
  // if greater than $08 for proper oscillation
}

void ProcFirebar(void) {
  // ProcFirebar:
  cpu_call_begin(0xcd3e); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen information
  lda_abs(Enemy_OffscreenBits); // check for d3 set
  and_imm_fzn(0b00001000); // if so, branch to leave
  if (!zero_flag) { return; }
  lda_abs_fz(TimerControl); // if master timer control set, branch
  if (!zero_flag) { goto SusFbar; } // ahead of this part
  lda_absx_fzn(FirebarSpinSpeed); // load spinning speed of firebar
  cpu_call_begin(0xcd50); FirebarSpin(); cpu_call_end(); // modify current spinstate
  and_imm(0b00011111); // mask out all but 5 LSB
  ram[FirebarSpinState_High + x] = a; // and store as new high byte of spinstate
  
SusFbar:
  lda_zpx(FirebarSpinState_High); // get high byte of spinstate
  ldy_zpx(Enemy_ID); // check enemy identifier
  cpy_imm_fczn(0x1f);
  if (!carry_flag) { goto SetupGFB; } // if < $1f (long firebar), branch
  cmp_imm_fz(0x8); // check high byte of spinstate
  if (zero_flag) { goto SkpFSte; } // if eight, branch to change
  cmp_imm_fczn(0x18);
  if (!zero_flag) { goto SetupGFB; } // if not at twenty-four branch to not change
  
SkpFSte:
  carry_flag = false;
  adc_imm_fczn(0x1); // add one to spinning thing to avoid horizontal state
  ram[FirebarSpinState_High + x] = a;
  
SetupGFB:
  ram[0xef] = a; // save high byte of spinning thing, modified or otherwise
  cpu_call_begin(0xcd6e); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinates to screen
  cpu_call_begin(0xcd71); GetFirebarPosition(); cpu_call_end(); // do a sub here (residual, too early to be used now)
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
  ram[Sprite_Y_Position + y] = a; // store as Y in OAM data
  ram[0x7] = a; // also save here
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X in OAM data
  ram[0x6] = a; // also save here
  lda_imm_fzn(0x1);
  ram[0x0] = a; // set $01 value here (not necessary)
  cpu_call_begin(0xcd8b); FirebarCollision(); cpu_call_end(); // draw fireball part and do collision detection
  ldy_imm(0x5); // load value for short firebars by default
  lda_zpx(Enemy_ID);
  cmp_imm_fc(0x1f); // are we doing a long firebar?
  if (!carry_flag) { goto SetMFbar; } // no, branch then
  ldy_imm(0xb); // otherwise load value for long firebars
  
SetMFbar:
  ram[0xed] = y; // store maximum value for length of firebars
  lda_imm(0x0);
  ram[0x0] = a; // initialize counter here
  
DrawFbar:
  lda_zp_fzn(0xef); // load high byte of spinstate
  cpu_call_begin(0xcda0); GetFirebarPosition(); cpu_call_end(); // get fireball position data depending on firebar part
  cpu_call_begin(0xcda3); DrawFirebar_Collision(); cpu_call_end(); // position it properly, draw it and do collision detection
  lda_zp(0x0); // check which firebar part
  cmp_imm_fz(0x4);
  if (!zero_flag) { goto NextFbar; }
  ldy_abs(DuplicateObj_Offset); // if we arrive at fifth firebar part,
  lda_absy(Enemy_SprDataOffset); // get offset from long firebar and load OAM data offset
  ram[0x6] = a; // using long firebar offset, then store as new one here
  
NextFbar:
  inc_zp(0x0); // move onto the next firebar part
  lda_zp(0x0);
  cmp_zp_fczn(0xed); // if we end up at the maximum part, go on and leave
  if (!carry_flag) { goto DrawFbar; } // otherwise go back and do another
  // SkipFBar:
  return;
}

void DrawFirebar_Collision(void) {
  lda_zp(0x3); // store mirror data elsewhere
  ram[0x5] = a;
  ldy_zp(0x6); // load OAM data offset for firebar
  lda_zp(0x1); // load horizontal adder we got from position loader
  lsr_zp_fc(0x5); // shift LSB of mirror data
  // if carry was set, skip this part
  if (!carry_flag) {
    eor_imm(0xff);
    adc_imm(0x1); // otherwise get two's compliment of horizontal adder
  }
  // AddHA:
  carry_flag = false; // add horizontal coordinate relative to screen to
  adc_abs(Enemy_Rel_XPos); // horizontal adder, modified or otherwise
  ram[Sprite_X_Position + y] = a; // store as X coordinate here
  ram[0x6] = a; // store here for now, note offset is saved in Y still
  cmp_abs_fc(Enemy_Rel_XPos); // compare X coordinate of sprite to original X of firebar
  // if sprite coordinate => original coordinate, branch
  if (!carry_flag) {
    lda_abs(Enemy_Rel_XPos);
    carry_flag = true; // otherwise subtract sprite X from the
    sbc_zp(0x6); // original one and skip this part
    goto ChkFOfs;
  }
  // SubtR1:
  carry_flag = true; // subtract original X from the
  sbc_abs(Enemy_Rel_XPos); // current sprite X
  
ChkFOfs:
  cmp_imm_fc(0x59); // if difference of coordinates within a certain range,
  // continue by handling vertical adder
  if (carry_flag) {
    lda_imm_fzn(0xf8); // otherwise, load offscreen Y coordinate
    goto SetVFbr; // and unconditionally branch to move sprite offscreen
  }
  // VAHandl:
  lda_abs(Enemy_Rel_YPos); // if vertical relative coordinate offscreen,
  cmp_imm_fczn(0xf8); // skip ahead of this part and write into sprite Y coordinate
  if (!zero_flag) {
    lda_zp(0x2); // load vertical adder we got from position loader
    lsr_zp_fc(0x5); // shift LSB of mirror data one more time
    // if carry was set, skip this part
    if (!carry_flag) {
      eor_imm(0xff);
      adc_imm(0x1); // otherwise get two's compliment of second part
    }
    // AddVA:
    carry_flag = false; // add vertical coordinate relative to screen to 
    adc_abs_fczn(Enemy_Rel_YPos); // the second data, modified or otherwise
  }
  
SetVFbr:
  ram[Sprite_Y_Position + y] = a; // store as Y coordinate here
  ram[0x7] = a; // also store here for now
  FirebarCollision(); // fallthrough
  return;
}

void FirebarCollision(void) {
  // FirebarCollision:
  cpu_call_begin(0xce0a); DrawFirebar(); cpu_call_end(); // run sub here to draw current tile of firebar
  tya(); // return OAM data offset and save
  pha(); // to the stack for now
  lda_abs(StarInvincibleTimer); // if star mario invincibility timer
  ora_abs_fz(TimerControl); // or master timer controls set
  if (!zero_flag) { goto NoColFB; } // then skip all of this
  ram[0x5] = a; // otherwise initialize counter
  ldy_zp(Player_Y_HighPos);
  dey_fz(); // if player's vertical high byte offscreen,
  if (!zero_flag) { goto NoColFB; } // skip all of this
  ldy_zp(Player_Y_Position); // get player's vertical position
  lda_abs_fz(PlayerSize); // get player's size
  if (!zero_flag) { goto AdjSm; } // if player small, branch to alter variables
  lda_abs_fz(CrouchingFlag);
  if (zero_flag) { goto BigJp; } // if player big and not crouching, jump ahead
  
AdjSm:
  inc_zp(0x5); // if small or big but crouching, execute this part
  inc_zp(0x5); // first increment our counter twice (setting $02 as flag)
  tya();
  carry_flag = false; // then add 24 pixels to the player's
  adc_imm(0x18); // vertical coordinate
  tay();
  
BigJp:
  tya(); // get vertical coordinate, altered or otherwise, from Y
  
FBCLoop:
  carry_flag = true; // subtract vertical position of firebar
  sbc_zp_fn(0x7); // from the vertical coordinate of the player
  if (!neg_flag) { goto ChkVFBD; } // if player lower on the screen than firebar, 
  eor_imm(0xff); // skip two's compliment part
  carry_flag = false; // otherwise get two's compliment
  adc_imm(0x1);
  
ChkVFBD:
  cmp_imm_fc(0x8); // if difference => 8 pixels, skip ahead of this part
  if (carry_flag) { goto Chk2Ofs; }
  lda_zp(0x6); // if firebar on far right on the screen, skip this,
  cmp_imm_fc(0xf0); // because, really, what's the point?
  if (carry_flag) { goto Chk2Ofs; }
  lda_abs(Sprite_X_Position + 4); // get OAM X coordinate for sprite #1
  carry_flag = false;
  adc_imm(0x4); // add four pixels
  ram[0x4] = a; // store here
  carry_flag = true; // subtract horizontal coordinate of firebar
  sbc_zp_fn(0x6); // from the X coordinate of player's sprite 1
  if (!neg_flag) { goto ChkFBCl; } // if modded X coordinate to the right of firebar
  eor_imm(0xff); // skip two's compliment part
  carry_flag = false; // otherwise get two's compliment
  adc_imm(0x1);
  
ChkFBCl:
  cmp_imm_fc(0x8); // if difference < 8 pixels, collision, thus branch
  if (!carry_flag) { goto ChgSDir; } // to process
  
Chk2Ofs:
  lda_zp(0x5); // if value of $02 was set earlier for whatever reason,
  cmp_imm_fz(0x2); // branch to increment OAM offset and leave, no collision
  if (zero_flag) { goto NoColFB; }
  ldy_zp(0x5); // otherwise get temp here and use as offset
  lda_zp(Player_Y_Position);
  carry_flag = false;
  adc_absy(FirebarYPos); // add value loaded with offset to player's vertical coordinate
  inc_zp(0x5); // then increment temp and jump back
  goto FBCLoop;
  
ChgSDir:
  ldx_imm(0x1); // set movement direction by default
  lda_zp(0x4); // if OAM X coordinate of player's sprite 1
  cmp_zp_fc(0x6); // is greater than horizontal coordinate of firebar
  if (carry_flag) { goto SetSDir; } // then do not alter movement direction
  inx(); // otherwise increment it
  
SetSDir:
  ram[Enemy_MovingDir] = x; // store movement direction here
  ldx_imm(0x0);
  lda_zp_fzn(0x0); // save value written to $00 to stack
  pha();
  cpu_call_begin(0xce81); InjurePlayer(); cpu_call_end(); // perform sub to hurt or kill player
  pla();
  ram[0x0] = a; // get value of $00 from stack
  
NoColFB:
  pla(); // get OAM data offset
  carry_flag = false; // add four to it and save
  adc_imm_fc(0x4);
  ram[0x6] = a;
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset and leave
  return;
}

void GetFirebarPosition(void) {
  pha(); // save high byte of spinstate to the stack
  and_imm(0b00001111); // mask out low nybble
  cmp_imm_fc(0x9);
  // if lower than $09, branch ahead
  if (carry_flag) {
    eor_imm(0b00001111); // otherwise get two's compliment to oscillate
    carry_flag = false;
    adc_imm(0x1);
  }
  // GetHAdder:
  ram[0x1] = a; // store result, modified or not, here
  ldy_zp(0x0); // load number of firebar ball where we're at
  lda_absy(FirebarTblOffsets); // load offset to firebar position data
  carry_flag = false;
  adc_zp(0x1); // add oscillated high byte of spinstate
  tay(); // to offset here and use as new offset
  lda_absy(FirebarPosLookupTbl); // get data here and store as horizontal adder
  ram[0x1] = a;
  pla(); // pull whatever was in A from the stack
  pha(); // save it again because we still need it
  carry_flag = false;
  adc_imm(0x8); // add eight this time, to get vertical adder
  and_imm(0b00001111); // mask out high nybble
  cmp_imm_fc(0x9); // if lower than $09, branch ahead
  if (carry_flag) {
    eor_imm(0b00001111); // otherwise get two's compliment
    carry_flag = false;
    adc_imm(0x1);
  }
  // GetVAdder:
  ram[0x2] = a; // store result here
  ldy_zp(0x0);
  lda_absy(FirebarTblOffsets); // load offset to firebar position data again
  carry_flag = false;
  adc_zp(0x2); // this time add value in $02 to offset here and use as offset
  tay();
  lda_absy(FirebarPosLookupTbl); // get data here and store as vertica adder
  ram[0x2] = a;
  pla(); // pull out whatever was in A one last time
  lsr_acc(); // divide by eight or shift three to the right
  lsr_acc();
  lsr_acc_fc();
  tay(); // use as offset
  lda_absy_fzn(FirebarMirrorData); // load mirroring data here
  ram[0x3] = a; // store
  return;
  // --------------------------------
}

void MoveFlyingCheepCheep(void) {
  lda_zpx(Enemy_State); // check cheep-cheep's enemy state
  and_imm_fzn(0b00100000); // for d5 set
  // branch to continue code if not set
  if (!zero_flag) {
    lda_imm(0x0);
    ram[Enemy_SprAttrib + x] = a; // otherwise clear sprite attributes
    MoveJ_EnemyVertically(); return; // and jump to move defeated cheep-cheep downwards
  }
  // FlyCC:
  cpu_call_begin(0xceef); MoveEnemyHorizontally(); cpu_call_end(); // move cheep-cheep horizontally based on speed and force
  ldy_imm(0xd); // set vertical movement amount
  lda_imm_fzn(0x5); // set maximum speed
  cpu_call_begin(0xcef6); SetXMoveAmt(); cpu_call_end(); // branch to impose gravity on flying cheep-cheep
  lda_absx(Enemy_Y_MoveForce);
  lsr_acc(); // get vertical movement force and
  lsr_acc(); // move high nybble to low
  lsr_acc();
  lsr_acc();
  tay(); // save as offset (note this tends to go into reach of code)
  lda_zpx(Enemy_Y_Position); // get vertical position
  carry_flag = true; // subtract pseudorandom value based on offset from position
  sbc_absy_fn(PRandomSubtracter);
  // if result within top half of screen, skip this part
  if (neg_flag) {
    eor_imm(0xff);
    carry_flag = false; // otherwise get two's compliment
    adc_imm(0x1);
  }
  // AddCCF:
  cmp_imm_fc(0x8); // if result or two's compliment greater than eight,
  // skip to the end without changing movement force
  if (!carry_flag) {
    lda_absx(Enemy_Y_MoveForce);
    carry_flag = false;
    adc_imm(0x10); // otherwise add to it
    ram[Enemy_Y_MoveForce + x] = a;
    lsr_acc(); // move high nybble to low again
    lsr_acc();
    lsr_acc();
    lsr_acc_fc();
    tay();
  }
  // BPGet:
  lda_absy_fzn(FlyCCBPriority); // load bg priority data and store (this is very likely
  ram[Enemy_SprAttrib + x] = a; // broken or residual code, value is overwritten before
  return; // drawing it next frame), then leave
  // --------------------------------
  // $00 - used to hold horizontal difference
  // $01-$03 - used to hold difference adjusters
}

void MoveLakitu(void) {
  lda_zpx(Enemy_State); // check lakitu's enemy state
  and_imm_fz(0b00100000); // for d5 set
  // if not set, continue with code
  if (!zero_flag) {
    MoveD_EnemyVertically(); return; // otherwise jump to move defeated lakitu downwards
  }
  // ChkLS:
  lda_zpx_fz(Enemy_State); // if lakitu's enemy state not set at all,
  // go ahead and continue with code
  if (!zero_flag) {
    lda_imm(0x0);
    ram[LakituMoveDirection + x] = a; // otherwise initialize moving direction to move to left
    ram[EnemyFrenzyBuffer] = a; // initialize frenzy buffer
    lda_imm(0x10);
    goto SetLSpd; // load horizontal speed and do unconditional branch
  }
  // Fr12S:
  lda_imm(Spiny);
  ram[EnemyFrenzyBuffer] = a; // set spiny identifier in frenzy buffer
  ldy_imm(0x2);
  
LdLDa:
  lda_absy(LakituDiffAdj); // load values
  ram[0x1 + y] = a; // store in zero page
  dey_fzn();
  if (!neg_flag) { goto LdLDa; } // do this until all values are stired
  cpu_call_begin(0xcf52); PlayerLakituDiff(); cpu_call_end(); // execute sub to set speed and create spinys
  
SetLSpd:
  ram[LakituMoveSpeed + x] = a; // set movement speed returned from sub
  ldy_imm(0x1); // set moving direction to right by default
  lda_zpx(LakituMoveDirection);
  and_imm_fz(0x1); // get LSB of moving direction
  // if set, branch to the end to use moving direction
  if (zero_flag) {
    lda_zpx(LakituMoveSpeed);
    eor_imm(0xff); // get two's compliment of moving speed
    carry_flag = false;
    adc_imm_fc(0x1);
    ram[LakituMoveSpeed + x] = a; // store as new moving speed
    iny(); // increment moving direction to left
  }
  // SetLMov:
  ram[Enemy_MovingDir + x] = y; // store moving direction
  MoveEnemyHorizontally(); return; // move lakitu horizontally
}

void PlayerLakituDiff(void) {
  // PlayerLakituDiff:
  ldy_imm_fzn(0x0); // set Y for default value
  cpu_call_begin(0xcf70); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between enemy and player
  if (!neg_flag) { goto ChkLakDif; } // branch if enemy is to the right of the player
  iny(); // increment Y for left of player
  lda_zp(0x0);
  eor_imm(0xff); // get two's compliment of low byte of horizontal difference
  carry_flag = false;
  adc_imm(0x1); // store two's compliment as horizontal difference
  ram[0x0] = a;
  
ChkLakDif:
  lda_zp(0x0); // get low byte of horizontal difference
  cmp_imm_fc(0x3c); // if within a certain distance of player, branch
  if (!carry_flag) { goto ChkPSpeed; }
  lda_imm(0x3c); // otherwise set maximum distance
  ram[0x0] = a;
  lda_zpx(Enemy_ID); // check if lakitu is in our current enemy slot
  cmp_imm_fz(Lakitu);
  if (!zero_flag) { goto ChkPSpeed; } // if not, branch elsewhere
  tya(); // compare contents of Y, now in A
  cmp_zpx_fcz(LakituMoveDirection); // to what is being used as horizontal movement direction
  if (zero_flag) { goto ChkPSpeed; } // if moving toward the player, branch, do not alter
  lda_zpx_fz(LakituMoveDirection); // if moving to the left beyond maximum distance,
  if (zero_flag) { goto SetLMovD; } // branch and alter without delay
  dec_zpx(LakituMoveSpeed); // decrement horizontal speed
  lda_zpx_fzn(LakituMoveSpeed); // if horizontal speed not yet at zero, branch to leave
  if (!zero_flag) { return; }
  
SetLMovD:
  tya(); // set horizontal direction depending on horizontal
  ram[LakituMoveDirection + x] = a; // difference between enemy and player if necessary
  
ChkPSpeed:
  lda_zp(0x0);
  and_imm(0b00111100); // mask out all but four bits in the middle
  lsr_acc(); // divide masked difference by four
  lsr_acc();
  ram[0x0] = a; // store as new value
  ldy_imm(0x0); // init offset
  lda_zp_fz(Player_X_Speed);
  if (zero_flag) { goto SubDifAdj; } // if player not moving horizontally, branch
  lda_abs_fz(ScrollAmount);
  if (zero_flag) { goto SubDifAdj; } // if scroll speed not set, branch to same place
  iny(); // otherwise increment offset
  lda_zp(Player_X_Speed);
  cmp_imm_fc(0x19); // if player not running, branch
  if (!carry_flag) { goto ChkSpinyO; }
  lda_abs(ScrollAmount);
  cmp_imm_fc(0x2); // if scroll speed below a certain amount, branch
  if (!carry_flag) { goto ChkSpinyO; } // to same place
  iny(); // otherwise increment once more
  
ChkSpinyO:
  lda_zpx(Enemy_ID); // check for spiny object
  cmp_imm_fz(Spiny);
  if (!zero_flag) { goto ChkEmySpd; } // branch if not found
  lda_zp_fz(Player_X_Speed); // if player not moving, skip this part
  if (!zero_flag) { goto SubDifAdj; }
  
ChkEmySpd:
  lda_zpx_fz(Enemy_Y_Speed); // check vertical speed
  if (!zero_flag) { goto SubDifAdj; } // branch if nonzero
  ldy_imm(0x0); // otherwise reinit offset
  
SubDifAdj:
  lda_absy(0x1); // get one of three saved values from earlier
  ldy_zp(0x0); // get saved horizontal difference
  
SPixelLak:
  carry_flag = true; // subtract one for each pixel of horizontal difference
  sbc_imm_fc(0x1); // from one of three saved values
  dey_fzn();
  if (!neg_flag) { goto SPixelLak; } // branch until all pixels are subtracted, to adjust difference
  // ExMoveLak:
  return; // leave!!!
  // -------------------------------------------------------------------------------------
  // $04-$05 - used to store name table address in little endian order
}

void BridgeCollapse(void) {
  // BridgeCollapse:
  ldx_abs(BowserFront_Offset); // get enemy offset for bowser
  lda_zpx(Enemy_ID); // check enemy object identifier for bowser
  cmp_imm_fcz(Bowser); // if not found, branch ahead,
  if (!zero_flag) { goto SetM2; } // metatile removal not necessary
  ram[ObjectOffset] = x; // store as enemy offset here
  lda_zpx_fz(Enemy_State); // if bowser in normal state, skip all of this
  if (zero_flag) { goto RemoveBridge; }
  and_imm_fz(0b01000000); // if bowser's state has d6 clear, skip to silence music
  if (zero_flag) { goto SetM2; }
  lda_zpx(Enemy_Y_Position); // check bowser's vertical coordinate
  cmp_imm_fczn(0xe0); // if bowser not yet low enough, skip this part ahead
  if (!carry_flag) { goto MoveD_Bowser; }
  
SetM2:
  lda_imm(Silence); // silence music
  ram[EventMusicQueue] = a;
  inc_abs(OperMode_Task); // move onto next secondary mode in autoctrl mode
  KillAllEnemies(); return; // jump to empty all enemy slots and then leave  
  
MoveD_Bowser:
  cpu_call_begin(0xd011); MoveEnemySlowVert(); cpu_call_end(); // do a sub to move bowser downwards
  goto BowserGfxHandler; // jump to draw bowser's front and rear, then leave
  
RemoveBridge:
  dec_abs_fzn(BowserFeetCounter); // decrement timer to control bowser's feet
  if (!zero_flag) { goto NoBFall; } // if not expired, skip all of this
  lda_imm(0x4);
  ram[BowserFeetCounter] = a; // otherwise, set timer now
  lda_abs(BowserBodyControls);
  eor_imm(0x1); // invert bit to control bowser's feet
  ram[BowserBodyControls] = a;
  lda_imm(0x22); // put high byte of name table address here for now
  ram[0x5] = a;
  ldy_abs(BridgeCollapseOffset); // get bridge collapse offset here
  lda_absy(BridgeCollapseData); // load low byte of name table address and store here
  ram[0x4] = a;
  ldy_abs(VRAM_Buffer1_Offset); // increment vram buffer offset
  iny();
  ldx_imm_fzn(0xc); // set offset for tile data for sub to draw blank metatile
  cpu_call_begin(0xd03b); RemBridge(); cpu_call_end(); // do sub here to remove bowser's bridge metatiles
  ldx_zp_fzn(ObjectOffset); // get enemy offset
  cpu_call_begin(0xd040); MoveVOffset(); cpu_call_end(); // set new vram buffer offset
  lda_imm(Sfx_Blast); // load the fireworks/gunfire sound into the square 2 sfx
  ram[Square2SoundQueue] = a; // queue while at the same time loading the brick
  lda_imm(Sfx_BrickShatter); // shatter sound into the noise sfx queue thus
  ram[NoiseSoundQueue] = a; // producing the unique sound of the bridge collapsing 
  inc_abs(BridgeCollapseOffset); // increment bridge collapse offset
  lda_abs(BridgeCollapseOffset);
  cmp_imm_fczn(0xf); // if bridge collapse offset has not yet reached
  if (!zero_flag) { goto NoBFall; } // the end, go ahead and skip this part
  cpu_call_begin(0xd055); InitVStf(); cpu_call_end(); // initialize whatever vertical speed bowser has
  lda_imm(0b01000000);
  ram[Enemy_State + x] = a; // set bowser's state to one of defeated states (d6 set)
  lda_imm_fzn(Sfx_BowserFall);
  ram[Square2SoundQueue] = a; // play bowser defeat sound
  
NoBFall:
  goto BowserGfxHandler; // jump to code that draws bowser
  // --------------------------------
  
BowserGfxHandler:
  cpu_call_begin(0xd17d); ProcessBowserHalf(); cpu_call_end(); // do a sub here to process bowser's front
  ldy_imm(0x10); // load default value here to position bowser's rear
  lda_zpx(Enemy_MovingDir); // check moving direction
  lsr_acc_fc();
  if (!carry_flag) { goto CopyFToR; } // if moving left, use default
  ldy_imm(0xf0); // otherwise load alternate positioning value here
  
CopyFToR:
  tya(); // move bowser's rear object position value to A
  carry_flag = false;
  adc_zpx(Enemy_X_Position); // add to bowser's front object horizontal coordinate
  ldy_abs(DuplicateObj_Offset); // get bowser's rear object offset
  ram[Enemy_X_Position + y] = a; // store A as bowser's rear horizontal coordinate
  lda_zpx(Enemy_Y_Position);
  carry_flag = false; // add eight pixels to bowser's front object
  adc_imm_fc(0x8); // vertical coordinate and store as vertical coordinate
  ram[Enemy_Y_Position + y] = a; // for bowser's rear
  lda_zpx(Enemy_State);
  ram[Enemy_State + y] = a; // copy enemy state directly from front to rear
  lda_zpx(Enemy_MovingDir);
  ram[Enemy_MovingDir + y] = a; // copy moving direction also
  lda_zp(ObjectOffset); // save enemy object offset of front to stack
  pha();
  ldx_abs(DuplicateObj_Offset); // put enemy object offset of rear as current
  ram[ObjectOffset] = x;
  lda_imm_fzn(Bowser); // set bowser's enemy identifier
  ram[Enemy_ID + x] = a; // store in bowser's rear object
  cpu_call_begin(0xd1b1); ProcessBowserHalf(); cpu_call_end(); // do a sub here to process bowser's rear
  pla();
  ram[ObjectOffset] = a; // get original enemy object offset
  tax();
  lda_imm_fzn(0x0); // nullify bowser's front/rear graphics flag
  ram[BowserGfxFlag] = a;
  // ExBGfxH:
  return; // leave!
}

void RunBowser(void) {
  goto RunBowser;
  
MoveD_Bowser:
  cpu_call_begin(0xd011); MoveEnemySlowVert(); cpu_call_end(); // do a sub to move bowser downwards
  goto BowserGfxHandler; // jump to draw bowser's front and rear, then leave
  
RunBowser:
  lda_zpx(Enemy_State); // if d5 in enemy state is not set
  and_imm_fz(0b00100000); // then branch elsewhere to run bowser
  if (zero_flag) { goto BowserControl; }
  lda_zpx(Enemy_Y_Position); // otherwise check vertical position
  cmp_imm_fczn(0xe0); // if above a certain point, branch to move defeated bowser
  if (!carry_flag) { goto MoveD_Bowser; } // otherwise proceed to KillAllEnemies
  KillAllEnemies(); // fallthrough
  return;
  
BowserControl:
  lda_imm(0x0);
  ram[EnemyFrenzyBuffer] = a; // empty frenzy buffer
  lda_abs_fz(TimerControl); // if master timer control not set,
  if (zero_flag) { goto ChkMouth; } // skip jump and execute code here
  goto SkipToFB; // otherwise, jump over a bunch of code
  
ChkMouth:
  lda_abs_fn(BowserBodyControls); // check bowser's mouth
  if (!neg_flag) { goto FeetTmr; } // if bit clear, go ahead with code here
  goto HammerChk; // otherwise skip a whole section starting here
  
FeetTmr:
  dec_abs_fz(BowserFeetCounter); // decrement timer to control bowser's feet
  if (!zero_flag) { goto ResetMDr; } // if not expired, skip this part
  lda_imm(0x20); // otherwise, reset timer
  ram[BowserFeetCounter] = a;
  lda_abs(BowserBodyControls); // and invert bit used
  eor_imm(0b00000001); // to control bowser's feet
  ram[BowserBodyControls] = a;
  
ResetMDr:
  lda_zp(FrameCounter); // check frame counter
  and_imm_fz(0b00001111); // if not on every sixteenth frame, skip
  if (!zero_flag) { goto B_FaceP; } // ahead to continue code
  lda_imm(0x2); // otherwise reset moving/facing direction every
  ram[Enemy_MovingDir + x] = a; // sixteen frames
  
B_FaceP:
  lda_absx_fzn(EnemyFrameTimer); // if timer set here expired,
  if (zero_flag) { goto GetPRCmp; } // branch to next section
  cpu_call_begin(0xd0b7); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and bowser,
  if (!neg_flag) { goto GetPRCmp; } // and branch if bowser to the right of the player
  lda_imm(0x1);
  ram[Enemy_MovingDir + x] = a; // set bowser to move and face to the right
  lda_imm(0x2);
  ram[BowserMovementSpeed] = a; // set movement speed
  lda_imm(0x20);
  ram[EnemyFrameTimer + x] = a; // set timer here
  ram[BowserFireBreathTimer] = a; // set timer used for bowser's flame
  lda_zpx(Enemy_X_Position);
  cmp_imm_fc(0xc8); // if bowser to the right past a certain point,
  if (carry_flag) { goto HammerChk; } // skip ahead to some other section
  
GetPRCmp:
  lda_zp(FrameCounter); // get frame counter
  and_imm_fz(0b00000011);
  if (!zero_flag) { goto HammerChk; } // execute this code every fourth frame, otherwise branch
  lda_zpx(Enemy_X_Position);
  cmp_abs_fz(BowserOrigXPos); // if bowser not at original horizontal position,
  if (!zero_flag) { goto GetDToO; } // branch to skip this part
  lda_absx(PseudoRandomBitReg);
  and_imm(0b00000011); // get pseudorandom offset
  tay();
  lda_absy(PRandomRange); // load value using pseudorandom offset
  ram[MaxRangeFromOrigin] = a; // and store here
  
GetDToO:
  lda_zpx(Enemy_X_Position);
  carry_flag = false; // add movement speed to bowser's horizontal
  adc_abs(BowserMovementSpeed); // coordinate and save as new horizontal position
  ram[Enemy_X_Position + x] = a;
  ldy_zpx(Enemy_MovingDir);
  cpy_imm_fcz(0x1); // if bowser moving and facing to the right, skip ahead
  if (zero_flag) { goto HammerChk; }
  ldy_imm(0xff); // set default movement speed here (move left)
  carry_flag = true; // get difference of current vs. original
  sbc_abs_fn(BowserOrigXPos); // horizontal position
  if (!neg_flag) { goto CompDToO; } // if current position to the right of original, skip ahead
  eor_imm(0xff);
  carry_flag = false; // get two's compliment
  adc_imm(0x1);
  ldy_imm(0x1); // set alternate movement speed here (move right)
  
CompDToO:
  cmp_abs_fc(MaxRangeFromOrigin); // compare difference with pseudorandom value
  if (!carry_flag) { goto HammerChk; } // if difference < pseudorandom value, leave speed alone
  ram[BowserMovementSpeed] = y; // otherwise change bowser's movement speed
  
HammerChk:
  lda_absx_fzn(EnemyFrameTimer); // if timer set here not expired yet, skip ahead to
  if (!zero_flag) { goto MakeBJump; } // some other section of code
  cpu_call_begin(0xd116); MoveEnemySlowVert(); cpu_call_end(); // otherwise start by moving bowser downwards
  lda_abs(WorldNumber); // check world number
  cmp_imm_fc(World6);
  if (!carry_flag) { goto SetHmrTmr; } // if world 1-5, skip this part (not time to throw hammers yet)
  lda_zp(FrameCounter);
  and_imm_fzn(0b00000011); // check to see if it's time to execute sub
  if (!zero_flag) { goto SetHmrTmr; } // if not, skip sub, otherwise
  cpu_call_begin(0xd126); SpawnHammerObj(); cpu_call_end(); // execute sub on every fourth frame to spawn misc object (hammer)
  
SetHmrTmr:
  lda_zpx(Enemy_Y_Position); // get current vertical position
  cmp_imm_fc(0x80); // if still above a certain point
  if (!carry_flag) { goto ChkFireB; } // then skip to world number check for flames
  lda_absx(PseudoRandomBitReg);
  and_imm(0b00000011); // get pseudorandom offset
  tay();
  lda_absy(PRandomRange); // get value using pseudorandom offset
  ram[EnemyFrameTimer + x] = a; // set for timer here
  
SkipToFB:
  goto ChkFireB; // jump to execute flames code
  
MakeBJump:
  cmp_imm_fcz(0x1); // if timer not yet about to expire,
  if (!zero_flag) { goto ChkFireB; } // skip ahead to next part
  dec_zpx_fzn(Enemy_Y_Position); // otherwise decrement vertical coordinate
  cpu_call_begin(0xd144); InitVStf(); cpu_call_end(); // initialize movement amount
  lda_imm(0xfe);
  ram[Enemy_Y_Speed + x] = a; // set vertical speed to move bowser upwards
  
ChkFireB:
  lda_abs(WorldNumber); // check world number here
  cmp_imm_fcz(World8); // world 8?
  if (zero_flag) { goto SpawnFBr; } // if so, execute this part here
  cmp_imm_fczn(World6); // world 6-7?
  if (carry_flag) { goto BowserGfxHandler; } // if so, skip this part here
  
SpawnFBr:
  lda_abs_fzn(BowserFireBreathTimer); // check timer here
  if (!zero_flag) { goto BowserGfxHandler; } // if not expired yet, skip all of this
  lda_imm(0x20);
  ram[BowserFireBreathTimer] = a; // set timer here
  lda_abs(BowserBodyControls);
  eor_imm_fzn(0b10000000); // invert bowser's mouth bit to open
  ram[BowserBodyControls] = a; // and close bowser's mouth
  if (neg_flag) { goto ChkFireB; } // if bowser's mouth open, loop back
  cpu_call_begin(0xd16a); SetFlameTimer(); cpu_call_end(); // get timing for bowser's flame
  ldy_abs_fz(SecondaryHardMode);
  if (zero_flag) { goto SetFBTmr; } // if secondary hard mode flag not set, skip this
  carry_flag = true;
  sbc_imm_fc(0x10); // otherwise subtract from value in A
  
SetFBTmr:
  ram[BowserFireBreathTimer] = a; // set value as timer here
  lda_imm_fzn(BowserFlame); // put bowser's flame identifier
  ram[EnemyFrenzyBuffer] = a; // in enemy frenzy buffer
  // --------------------------------
  
BowserGfxHandler:
  cpu_call_begin(0xd17d); ProcessBowserHalf(); cpu_call_end(); // do a sub here to process bowser's front
  ldy_imm(0x10); // load default value here to position bowser's rear
  lda_zpx(Enemy_MovingDir); // check moving direction
  lsr_acc_fc();
  if (!carry_flag) { goto CopyFToR; } // if moving left, use default
  ldy_imm(0xf0); // otherwise load alternate positioning value here
  
CopyFToR:
  tya(); // move bowser's rear object position value to A
  carry_flag = false;
  adc_zpx(Enemy_X_Position); // add to bowser's front object horizontal coordinate
  ldy_abs(DuplicateObj_Offset); // get bowser's rear object offset
  ram[Enemy_X_Position + y] = a; // store A as bowser's rear horizontal coordinate
  lda_zpx(Enemy_Y_Position);
  carry_flag = false; // add eight pixels to bowser's front object
  adc_imm_fc(0x8); // vertical coordinate and store as vertical coordinate
  ram[Enemy_Y_Position + y] = a; // for bowser's rear
  lda_zpx(Enemy_State);
  ram[Enemy_State + y] = a; // copy enemy state directly from front to rear
  lda_zpx(Enemy_MovingDir);
  ram[Enemy_MovingDir + y] = a; // copy moving direction also
  lda_zp(ObjectOffset); // save enemy object offset of front to stack
  pha();
  ldx_abs(DuplicateObj_Offset); // put enemy object offset of rear as current
  ram[ObjectOffset] = x;
  lda_imm_fzn(Bowser); // set bowser's enemy identifier
  ram[Enemy_ID + x] = a; // store in bowser's rear object
  cpu_call_begin(0xd1b1); ProcessBowserHalf(); cpu_call_end(); // do a sub here to process bowser's rear
  pla();
  ram[ObjectOffset] = a; // get original enemy object offset
  tax();
  lda_imm_fzn(0x0); // nullify bowser's front/rear graphics flag
  ram[BowserGfxFlag] = a;
  // ExBGfxH:
  return; // leave!
}

void KillAllEnemies(void) {
  ldx_imm_fzn(0x4); // start with last enemy slot
  
KillLoop:
  cpu_call_begin(0xd075); EraseEnemyObject(); cpu_call_end(); // branch to kill enemy objects
  dex_fzn(); // move onto next enemy slot
  if (!neg_flag) { goto KillLoop; } // do this until all slots are emptied
  ram[EnemyFrenzyBuffer] = a; // empty frenzy buffer
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
}

void ProcessBowserHalf(void) {
  inc_abs_fzn(BowserGfxFlag); // increment bowser's graphics flag, then run subroutines
  cpu_call_begin(0xd1c1); RunRetainerObj(); cpu_call_end(); // to get offscreen bits, relative position and draw bowser (finally!)
  lda_zpx_fzn(Enemy_State);
  if (zero_flag) {
    lda_imm_fzn(0xa);
    ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control
    cpu_call_begin(0xd1cd); GetEnemyBoundBox(); cpu_call_end(); // get bounding box coordinates
    PlayerEnemyCollision(); return; // do player-to-enemy collision detection
    // -------------------------------------------------------------------------------------
    // $00 - used to hold movement force and tile number
    // $01 - used to hold sprite attribute data
  }
}

void SetFlameTimer(void) {
  ldy_abs(BowserFlameTimerCtrl); // load counter as offset
  inc_abs(BowserFlameTimerCtrl); // increment
  lda_abs(BowserFlameTimerCtrl); // mask out all but 3 LSB
  and_imm(0b00000111); // to keep in range of 0-7
  ram[BowserFlameTimerCtrl] = a;
  lda_absy_fzn(FlameTimerData); // load value to be used then leave
  // ExFl:
  return;
}

void ProcBowserFlame(void) {
  // ProcBowserFlame:
  lda_abs_fzn(TimerControl); // if master timer control flag set,
  if (!zero_flag) { goto SetGfxF; } // skip all of this
  lda_imm(0x40); // load default movement force
  ldy_abs_fz(SecondaryHardMode);
  if (zero_flag) { goto SFlmX; } // if secondary hard mode flag not set, use default
  lda_imm(0x60); // otherwise load alternate movement force to go faster
  
SFlmX:
  ram[0x0] = a; // store value here
  lda_absx(Enemy_X_MoveForce);
  carry_flag = true; // subtract value from movement force
  sbc_zp_fc(0x0);
  ram[Enemy_X_MoveForce + x] = a; // save new value
  lda_zpx(Enemy_X_Position);
  sbc_imm_fc(0x1); // subtract one from horizontal position to move
  ram[Enemy_X_Position + x] = a; // to the left
  lda_zpx(Enemy_PageLoc);
  sbc_imm(0x0); // subtract borrow from page location
  ram[Enemy_PageLoc + x] = a;
  ldy_absx(BowserFlamePRandomOfs); // get some value here and use as offset
  lda_zpx(Enemy_Y_Position); // load vertical coordinate
  cmp_absy_fczn(FlameYPosData); // compare against coordinate data using $0417,x as offset
  if (zero_flag) { goto SetGfxF; } // if equal, branch and do not modify coordinate
  carry_flag = false;
  adc_absx_fczn(Enemy_Y_MoveForce); // otherwise add value here to coordinate and store
  ram[Enemy_Y_Position + x] = a; // as new vertical coordinate
  
SetGfxF:
  cpu_call_begin(0xd222); RelativeEnemyPosition(); cpu_call_end(); // get new relative coordinates
  lda_zpx_fzn(Enemy_State); // if bowser's flame not in normal state,
  if (!zero_flag) { return; } // branch to leave
  lda_imm(0x51); // otherwise, continue
  ram[0x0] = a; // write first tile number
  ldy_imm(0x2); // load attributes without vertical flip by default
  lda_zp(FrameCounter);
  and_imm_fz(0b00000010); // invert vertical flip bit every 2 frames
  if (zero_flag) { goto FlmeAt; } // if d1 not set, write default value
  ldy_imm(0x82); // otherwise write value with vertical flip bit set
  
FlmeAt:
  ram[0x1] = y; // set bowser's flame sprite attributes here
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  ldx_imm(0x0);
  
DrawFlameLoop:
  lda_abs(Enemy_Rel_YPos); // get Y relative coordinate of current enemy object
  ram[Sprite_Y_Position + y] = a; // write into Y coordinate of OAM data
  lda_zp(0x0);
  ram[Sprite_Tilenumber + y] = a; // write current tile number into OAM data
  inc_zp(0x0); // increment tile number to draw more bowser's flame
  lda_zp(0x1);
  ram[Sprite_Attributes + y] = a; // write saved attributes into OAM data
  lda_abs(Enemy_Rel_XPos);
  ram[Sprite_X_Position + y] = a; // write X relative coordinate of current enemy object
  carry_flag = false;
  adc_imm(0x8);
  ram[Enemy_Rel_XPos] = a; // then add eight to it and store
  iny();
  iny();
  iny();
  iny(); // increment Y four times to move onto the next OAM
  inx(); // move onto the next OAM, and branch if three
  cpx_imm_fc(0x3); // have not yet been done
  if (!carry_flag) { goto DrawFlameLoop; }
  ldx_zp_fzn(ObjectOffset); // reload original enemy offset
  cpu_call_begin(0xd267); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen information
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  lda_abs(Enemy_OffscreenBits); // get enemy object offscreen bits
  lsr_acc_fc(); // move d0 to carry and result to stack
  pha();
  if (!carry_flag) { goto M3FOfs; } // branch if carry not set
  lda_imm(0xf8); // otherwise move sprite offscreen, this part likely
  ram[Sprite_Y_Position + 12 + y] = a; // residual since flame is only made of three sprites
  
M3FOfs:
  pla(); // get bits from stack
  lsr_acc_fc(); // move d1 to carry and move bits back to stack
  pha();
  if (!carry_flag) { goto M2FOfs; } // branch if carry not set again
  lda_imm(0xf8); // otherwise move third sprite offscreen
  ram[Sprite_Y_Position + 8 + y] = a;
  
M2FOfs:
  pla(); // get bits from stack again
  lsr_acc_fc(); // move d2 to carry and move bits back to stack again
  pha();
  if (!carry_flag) { goto M1FOfs; } // branch if carry not set yet again
  lda_imm(0xf8); // otherwise move second sprite offscreen
  ram[Sprite_Y_Position + 4 + y] = a;
  
M1FOfs:
  pla(); // get bits from stack one last time
  lsr_acc_fczn(); // move d3 to carry
  if (!carry_flag) { return; } // branch if carry not set one last time
  lda_imm_fzn(0xf8);
  ram[Sprite_Y_Position + y] = a; // otherwise move first sprite offscreen
  // ExFlmeD:
  return; // leave
  // --------------------------------
}

void RunFireworks(void) {
  // RunFireworks:
  dec_zpx_fzn(ExplosionTimerCounter); // decrement explosion timing counter here
  if (!zero_flag) { goto SetupExpl; } // if not expired, skip this part
  lda_imm(0x8);
  ram[ExplosionTimerCounter + x] = a; // reset counter
  inc_zpx(ExplosionGfxCounter); // increment explosion graphics counter
  lda_zpx(ExplosionGfxCounter);
  cmp_imm_fczn(0x3); // check explosion graphics counter
  if (carry_flag) { goto FireworksSoundScore; } // if at a certain point, branch to kill this object
  
SetupExpl:
  cpu_call_begin(0xd2a7); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinates of explosion
  lda_abs(Enemy_Rel_YPos); // copy relative coordinates
  ram[Fireball_Rel_YPos] = a; // from the enemy object to the fireball object
  lda_abs(Enemy_Rel_XPos); // first vertical, then horizontal
  ram[Fireball_Rel_XPos] = a;
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  lda_zpx_fzn(ExplosionGfxCounter); // get explosion graphics counter
  cpu_call_begin(0xd2bb); DrawExplosion_Fireworks(); cpu_call_end(); // do a sub to draw the explosion then leave
  return;
  
FireworksSoundScore:
  lda_imm(0x0); // disable enemy buffer flag
  ram[Enemy_Flag + x] = a;
  lda_imm(Sfx_Blast); // play fireworks/gunfire sound
  ram[Square2SoundQueue] = a;
  lda_imm(0x5); // set part of score modifier for 500 points
  ram[DigitModifier + 4] = a;
  goto EndAreaPoints; // jump to award points accordingly then leave
  // --------------------------------
  
EndAreaPoints:
  ldy_imm(0xb); // load offset for mario's score by default
  lda_abs_fzn(CurrentPlayer); // check player on the screen
  if (zero_flag) { goto ELPGive; } // if mario, do not change
  ldy_imm_fzn(0x11); // otherwise load offset for luigi's score
  
ELPGive:
  cpu_call_begin(0xd341); DigitsMathRoutine(); cpu_call_end(); // award 50 points per game timer interval
  lda_abs(CurrentPlayer); // get player on the screen (or 500 points per
  asl_acc(); // fireworks explosion if branched here from there)
  asl_acc(); // shift to high nybble
  asl_acc();
  asl_acc_fc();
  ora_imm_fzn(0b00000100); // add four to set nybble for game timer
  UpdateNumber(); return; // jump to print the new score and game timer
}

void RunStarFlagObj(void) {
  lda_imm(0x0); // initialize enemy frenzy buffer
  ram[EnemyFrenzyBuffer] = a;
  lda_abs(StarFlagTaskControl); // check star flag object task number here
  cmp_imm_fczn(0x5); // if greater than 5, branch to exit
  if (!carry_flag) {
    cpu_call_begin(0xd2e7);
    asl_acc_fczn();
    tay_fzn();
    pla_fzn();
    ram[0x4] = a;
    pla_fzn();
    ram[0x5] = a;
    iny_fzn();
    lda_indy_fzn(0x4);
    ram[0x6] = a;
    iny_fzn();
    lda_indy_fzn(0x4);
    ram[0x7] = a;
    switch (read_word(0x6)) {
      case 0xd311: StarFlagExit(); return;
      case 0xd2f2: GameTimerFireworks(); return;
      case 0xd312: AwardGameTimerPoints(); return;
      case 0xd34e: RaiseFlagSetoffFWorks(); return;
      case 0xd3a2: DelayToAreaEnd(); return;
      default: cpu_unresolved_jump(read_word(0x6)); return;
    }
  }
}

void GameTimerFireworks(void) {
  ldy_imm(0x5); // set default state for star flag object
  lda_abs(GameTimerDisplay + 2); // get game timer's last digit
  cmp_imm_fcz(0x1);
  // if last digit of game timer set to 1, skip ahead
  if (!zero_flag) {
    ldy_imm(0x3); // otherwise load new value for state
    cmp_imm_fcz(0x3);
    // if last digit of game timer set to 3, skip ahead
    if (!zero_flag) {
      ldy_imm(0x0); // otherwise load one more potential value for state
      cmp_imm_fcz(0x6);
      // if last digit of game timer set to 6, skip ahead
      if (!zero_flag) {
        lda_imm(0xff); // otherwise set value for no fireworks
      }
    }
  }
  // SetFWC:
  ram[FireworksCounter] = a; // set fireworks counter here
  ram[Enemy_State + x] = y; // set whatever state we have in star flag object
  // IncrementSFTask1:
  inc_abs_fzn(StarFlagTaskControl); // increment star flag object task number
  StarFlagExit(); // fallthrough
  return;
}

void StarFlagExit(void) {
  return; // leave
}

void AwardGameTimerPoints(void) {
  goto AwardGameTimerPoints;
  
IncrementSFTask1:
  inc_abs_fzn(StarFlagTaskControl); // increment star flag object task number
  StarFlagExit(); // fallthrough
  return;
  
AwardGameTimerPoints:
  lda_abs(GameTimerDisplay); // check all game timer digits for any intervals left
  ora_abs(GameTimerDisplay + 1);
  ora_abs_fz(GameTimerDisplay + 2);
  if (zero_flag) { goto IncrementSFTask1; } // if no time left on game timer at all, branch to next task
  lda_zp(FrameCounter);
  and_imm_fz(0b00000100); // check frame counter for d2 set (skip ahead
  // for four frames every four frames) branch if not set
  if (!zero_flag) {
    lda_imm(Sfx_TimerTick);
    ram[Square2SoundQueue] = a; // load timer tick sound
  }
  // NoTTick:
  ldy_imm(0x23); // set offset here to subtract from game timer's last digit
  lda_imm_fzn(0xff); // set adder here to $ff, or -1, to subtract one
  ram[DigitModifier + 5] = a; // from the last digit of the game timer
  cpu_call_begin(0xd330); DigitsMathRoutine(); cpu_call_end(); // subtract digit
  lda_imm(0x5); // set now to add 50 points
  ram[DigitModifier + 5] = a; // per game timer interval subtracted
  // EndAreaPoints:
  ldy_imm(0xb); // load offset for mario's score by default
  lda_abs_fzn(CurrentPlayer); // check player on the screen
  // if mario, do not change
  if (!zero_flag) {
    ldy_imm_fzn(0x11); // otherwise load offset for luigi's score
  }
  // ELPGive:
  cpu_call_begin(0xd341); DigitsMathRoutine(); cpu_call_end(); // award 50 points per game timer interval
  lda_abs(CurrentPlayer); // get player on the screen (or 500 points per
  asl_acc(); // fireworks explosion if branched here from there)
  asl_acc(); // shift to high nybble
  asl_acc();
  asl_acc_fc();
  ora_imm_fzn(0b00000100); // add four to set nybble for game timer
  UpdateNumber(); return; // jump to print the new score and game timer
}

void RaiseFlagSetoffFWorks(void) {
  lda_zpx(Enemy_Y_Position); // check star flag's vertical position
  cmp_imm_fc(0x72); // against preset value
  // if star flag higher vertically, branch to other code
  if (carry_flag) {
    dec_zpx_fzn(Enemy_Y_Position); // otherwise, raise star flag by one pixel
    DrawStarFlag(); return; // and skip this part here
  }
  // SetoffF:
  lda_abs_fzn(FireworksCounter); // check fireworks counter
  // if no fireworks left to go off, skip this part
  if (!zero_flag) {
    // if no fireworks set to go off, skip this part
    if (!neg_flag) {
      lda_imm_fzn(Fireworks);
      ram[EnemyFrenzyBuffer] = a; // otherwise set fireworks object in frenzy queue
      DrawStarFlag(); // fallthrough
      return;
    }
  }
  // DrawFlagSetTimer:
  cpu_call_begin(0xd398); DrawStarFlag(); cpu_call_end(); // do sub to draw star flag
  lda_imm(0x6);
  ram[EnemyIntervalTimer + x] = a; // set interval timer here
  // IncrementSFTask2:
  inc_abs_fzn(StarFlagTaskControl); // move onto next task
  return;
}

void DrawStarFlag(void) {
  cpu_call_begin(0xd367); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinates of star flag
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  ldx_imm(0x3); // do four sprites
  
DSFLoop:
  lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
  carry_flag = false;
  adc_absx(StarFlagYPosAdder); // add Y coordinate adder data
  ram[Sprite_Y_Position + y] = a; // store as Y coordinate
  lda_absx(StarFlagTileData); // get tile number
  ram[Sprite_Tilenumber + y] = a; // store as tile number
  lda_imm(0x22); // set palette and background priority bits
  ram[Sprite_Attributes + y] = a; // store as attributes
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  carry_flag = false;
  adc_absx_fc(StarFlagXPosAdder); // add X coordinate adder data
  ram[Sprite_X_Position + y] = a; // store as X coordinate
  iny();
  iny(); // increment OAM data offset four bytes
  iny(); // for next sprite
  iny();
  dex_fn(); // move onto next sprite
  if (!neg_flag) { goto DSFLoop; } // do this until all sprites are done
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
}

void DelayToAreaEnd(void) {
  goto DelayToAreaEnd;
  
IncrementSFTask2:
  inc_abs_fzn(StarFlagTaskControl); // move onto next task
  return;
  
DelayToAreaEnd:
  cpu_call_begin(0xd3a4); DrawStarFlag(); cpu_call_end(); // do sub to draw star flag
  lda_absx_fzn(EnemyIntervalTimer); // if interval timer set in previous task
  if (zero_flag) {
    lda_abs_fzn(EventMusicBuffer); // if event music buffer empty,
    if (zero_flag) { goto IncrementSFTask2; } // branch to increment task
    // StarFlagExit2:
    return; // otherwise leave
    // --------------------------------
    // $00 - used to store horizontal difference between player and piranha plant
  }
}

void MovePiranhaPlant(void) {
  // MovePiranhaPlant:
  lda_zpx_fz(Enemy_State); // check enemy state
  if (!zero_flag) { goto PutinPipe; } // if set at all, branch to leave
  lda_absx_fz(EnemyFrameTimer); // check enemy's timer here
  if (!zero_flag) { goto PutinPipe; } // branch to end if not yet expired
  lda_zpx_fz(PiranhaPlant_MoveFlag); // check movement flag
  if (!zero_flag) { goto SetupToMovePPlant; } // if moving, skip to part ahead
  lda_zpx_fzn(PiranhaPlant_Y_Speed); // if currently rising, branch 
  if (neg_flag) { goto ReversePlantSpeed; } // to move enemy upwards out of pipe
  cpu_call_begin(0xd3c3); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and
  if (!neg_flag) { goto ChkPlayerNearPipe; } // piranha plant, and branch if enemy to right of player
  lda_zp(0x0); // otherwise get saved horizontal difference
  eor_imm(0xff);
  carry_flag = false; // and change to two's compliment
  adc_imm(0x1);
  ram[0x0] = a; // save as new horizontal difference
  
ChkPlayerNearPipe:
  lda_zp(0x0); // get saved horizontal difference
  cmp_imm_fc(0x21);
  if (!carry_flag) { goto PutinPipe; } // if player within a certain distance, branch to leave
  
ReversePlantSpeed:
  lda_zpx(PiranhaPlant_Y_Speed); // get vertical speed
  eor_imm(0xff);
  carry_flag = false; // change to two's compliment
  adc_imm(0x1);
  ram[PiranhaPlant_Y_Speed + x] = a; // save as new vertical speed
  inc_zpx(PiranhaPlant_MoveFlag); // increment to set movement flag
  
SetupToMovePPlant:
  lda_absx(PiranhaPlantDownYPos); // get original vertical coordinate (lowest point)
  ldy_zpx_fn(PiranhaPlant_Y_Speed); // get vertical speed
  if (!neg_flag) { goto RiseFallPiranhaPlant; } // branch if moving downwards
  lda_absx(PiranhaPlantUpYPos); // otherwise get other vertical coordinate (highest point)
  
RiseFallPiranhaPlant:
  ram[0x0] = a; // save vertical coordinate here
  lda_zp(FrameCounter); // get frame counter
  lsr_acc_fc();
  if (!carry_flag) { goto PutinPipe; } // branch to leave if d0 set (execute code every other frame)
  lda_abs_fz(TimerControl); // get master timer control
  if (!zero_flag) { goto PutinPipe; } // branch to leave if set (likely not necessary)
  lda_zpx(Enemy_Y_Position); // get current vertical coordinate
  carry_flag = false;
  adc_zpx(PiranhaPlant_Y_Speed); // add vertical speed to move up or down
  ram[Enemy_Y_Position + x] = a; // save as new vertical coordinate
  cmp_zp_fcz(0x0); // compare against low or high coordinate
  if (!zero_flag) { goto PutinPipe; } // branch to leave if not yet reached
  lda_imm(0x0);
  ram[PiranhaPlant_MoveFlag + x] = a; // otherwise clear movement flag
  lda_imm(0x40);
  ram[EnemyFrameTimer + x] = a; // set timer to delay piranha plant movement
  
PutinPipe:
  lda_imm_fzn(0b00100000); // set background priority bit in sprite
  ram[Enemy_SprAttrib + x] = a; // attributes to give illusion of being inside pipe
  return; // then leave
  // -------------------------------------------------------------------------------------
  // $07 - spinning speed
}

void FirebarSpin(void) {
  ram[0x7] = a; // save spinning speed here
  lda_zpx_fz(FirebarSpinDirection); // check spinning direction
  // if moving counter-clockwise, branch to other part
  if (zero_flag) {
    ldy_imm(0x18); // possibly residual ldy
    lda_zpx(FirebarSpinState_Low);
    carry_flag = false; // add spinning speed to what would normally be
    adc_zp_fc(0x7); // the horizontal speed
    ram[FirebarSpinState_Low + x] = a;
    lda_zpx(FirebarSpinState_High); // add carry to what would normally be the vertical speed
    adc_imm_fczn(0x0);
    return;
  }
  // SpinCounterClockwise:
  ldy_imm(0x8); // possibly residual ldy
  lda_zpx(FirebarSpinState_Low);
  carry_flag = true; // subtract spinning speed to what would normally be
  sbc_zp_fc(0x7); // the horizontal speed
  ram[FirebarSpinState_Low + x] = a;
  lda_zpx(FirebarSpinState_High); // add carry to what would normally be the vertical speed
  sbc_imm_fczn(0x0);
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used to hold collision flag, Y movement force + 5 or low byte of name table for rope
  // $01 - used to hold high byte of name table for rope
  // $02 - used to hold page location of rope
}

void BalancePlatform(void) {
  // BalancePlatform:
  lda_zpx(Enemy_Y_HighPos); // check high byte of vertical position
  cmp_imm_fcz(0x3);
  if (!zero_flag) { goto DoBPl; }
  EraseEnemyObject(); return; // if far below screen, kill the object
  
DoBPl:
  lda_zpx_fzn(Enemy_State); // get object's state (set to $ff or other platform offset)
  if (!neg_flag) { goto CheckBalPlatform; } // if doing other balance platform, branch to leave
  return;
  
CheckBalPlatform:
  tay(); // save offset from state as Y
  lda_absx(PlatformCollisionFlag); // get collision flag of platform
  ram[0x0] = a; // store here
  lda_zpx_fz(Enemy_MovingDir); // get moving direction
  if (zero_flag) { goto ChkForFall; }
  goto PlatformFall; // if set, jump here
  
ChkForFall:
  lda_imm(0x2d); // check if platform is above a certain point
  cmp_zpx_fc(Enemy_Y_Position);
  if (!carry_flag) { goto ChkOtherForFall; } // if not, branch elsewhere
  cpy_zp_fcz(0x0); // if collision flag is set to same value as
  if (zero_flag) { goto MakePlatformFall; } // enemy state, branch to make platforms fall
  carry_flag = false;
  adc_imm_fczn(0x2); // otherwise add 2 pixels to vertical position
  ram[Enemy_Y_Position + x] = a; // of current platform and branch elsewhere
  StopPlatforms(); return; // to make platforms stop
  
MakePlatformFall:
  goto InitPlatformFall; // make platforms fall
  
ChkOtherForFall:
  cmp_zpy_fc(Enemy_Y_Position); // check if other platform is above a certain point
  if (!carry_flag) { goto ChkToMoveBalPlat; } // if not, branch elsewhere
  cpx_zp_fcz(0x0); // if collision flag is set to same value as
  if (zero_flag) { goto MakePlatformFall; } // enemy state, branch to make platforms fall
  carry_flag = false;
  adc_imm_fczn(0x2); // otherwise add 2 pixels to vertical position
  ram[Enemy_Y_Position + y] = a; // of other platform and branch elsewhere
  StopPlatforms(); return; // jump to stop movement and do not return
  
ChkToMoveBalPlat:
  lda_zpx(Enemy_Y_Position); // save vertical position to stack
  pha();
  lda_absx_fn(PlatformCollisionFlag); // get collision flag
  if (!neg_flag) { goto ColFlg; } // branch if collision
  lda_absx(Enemy_Y_MoveForce);
  carry_flag = false; // add $05 to contents of moveforce, whatever they be
  adc_imm_fc(0x5);
  ram[0x0] = a; // store here
  lda_zpx(Enemy_Y_Speed);
  adc_imm_fczn(0x0); // add carry to vertical speed
  if (neg_flag) { goto PlatDn; } // branch if moving downwards
  if (!zero_flag) { goto PlatUp; } // branch elsewhere if moving upwards
  lda_zp(0x0);
  cmp_imm_fczn(0xb); // check if there's still a little force left
  if (!carry_flag) { goto PlatSt; } // if not enough, branch to stop movement
  if (carry_flag) { goto PlatUp; } // otherwise keep branch to move upwards
  
ColFlg:
  cmp_zp_fczn(ObjectOffset); // if collision flag matches
  if (zero_flag) { goto PlatDn; } // current enemy object offset, branch
  
PlatUp:
  cpu_call_begin(0xd49a); MovePlatformUp(); cpu_call_end(); // do a sub to move upwards
  goto DoOtherPlatform; // jump ahead to remaining code
  
PlatSt:
  cpu_call_begin(0xd4a0); StopPlatforms(); cpu_call_end(); // do a sub to stop movement
  goto DoOtherPlatform; // jump ahead to remaining code
  
PlatDn:
  cpu_call_begin(0xd4a6); MovePlatformDown(); cpu_call_end(); // do a sub to move downwards
  
DoOtherPlatform:
  ldy_zpx(Enemy_State); // get offset of other platform
  pla(); // get old vertical coordinate from stack
  carry_flag = true;
  sbc_zpx(Enemy_Y_Position); // get difference of old vs. new coordinate
  carry_flag = false;
  adc_zpy_fc(Enemy_Y_Position); // add difference to vertical coordinate of other
  ram[Enemy_Y_Position + y] = a; // platform to move it in the opposite direction
  lda_absx_fn(PlatformCollisionFlag); // if no collision, skip this part here
  if (neg_flag) { goto DrawEraseRope; }
  tax_fzn(); // put offset which collision occurred here
  cpu_call_begin(0xd4bc); PositionPlayerOnVPlat(); cpu_call_end(); // and use it to position player accordingly
  
DrawEraseRope:
  ldy_zp(ObjectOffset); // get enemy object offset
  lda_zpy(Enemy_Y_Speed); // check to see if current platform is
  ora_absy_fz(Enemy_Y_MoveForce); // moving at all
  if (zero_flag) { goto ExitRp; } // if not, skip all of this and branch to leave
  ldx_abs(VRAM_Buffer1_Offset); // get vram buffer offset
  cpx_imm_fc(0x20); // if offset beyond a certain point, go ahead
  if (carry_flag) { goto ExitRp; } // and skip this, branch to leave
  lda_zpy_fzn(Enemy_Y_Speed);
  pha(); // save two copies of vertical speed to stack
  pha();
  cpu_call_begin(0xd4d5); SetupPlatformRope(); cpu_call_end(); // do a sub to figure out where to put new bg tiles
  lda_zp(0x1); // write name table address to vram buffer
  ram[VRAM_Buffer1 + x] = a; // first the high byte, then the low
  lda_zp(0x0);
  ram[VRAM_Buffer1 + 1 + x] = a;
  lda_imm(0x2); // set length for 2 bytes
  ram[VRAM_Buffer1 + 2 + x] = a;
  lda_zpy_fn(Enemy_Y_Speed); // if platform moving upwards, branch 
  if (neg_flag) { goto EraseR1; } // to do something else
  lda_imm(0xa2);
  ram[VRAM_Buffer1 + 3 + x] = a; // otherwise put tile numbers for left
  lda_imm(0xa3); // and right sides of rope in vram buffer
  ram[VRAM_Buffer1 + 4 + x] = a;
  goto OtherRope; // jump to skip this part
  
EraseR1:
  lda_imm(0x24); // put blank tiles in vram buffer
  ram[VRAM_Buffer1 + 3 + x] = a; // to erase rope
  ram[VRAM_Buffer1 + 4 + x] = a;
  
OtherRope:
  lda_zpy(Enemy_State); // get offset of other platform from state
  tay(); // use as Y here
  pla(); // pull second copy of vertical speed from stack
  eor_imm_fzn(0xff); // invert bits to reverse speed
  cpu_call_begin(0xd508); SetupPlatformRope(); cpu_call_end(); // do sub again to figure out where to put bg tiles  
  lda_zp(0x1); // write name table address to vram buffer
  ram[VRAM_Buffer1 + 5 + x] = a; // this time we're doing putting tiles for
  lda_zp(0x0); // the other platform
  ram[VRAM_Buffer1 + 6 + x] = a;
  lda_imm(0x2);
  ram[VRAM_Buffer1 + 7 + x] = a; // set length again for 2 bytes
  pla_fn(); // pull first copy of vertical speed from stack
  if (!neg_flag) { goto EraseR2; } // if moving upwards (note inversion earlier), skip this
  lda_imm(0xa2);
  ram[VRAM_Buffer1 + 8 + x] = a; // otherwise put tile numbers for left
  lda_imm(0xa3); // and right sides of rope in vram
  ram[VRAM_Buffer1 + 9 + x] = a; // transfer buffer
  goto EndRp; // jump to skip this part
  
EraseR2:
  lda_imm(0x24); // put blank tiles in vram buffer
  ram[VRAM_Buffer1 + 8 + x] = a; // to erase rope
  ram[VRAM_Buffer1 + 9 + x] = a;
  
EndRp:
  lda_imm(0x0); // put null terminator at the end
  ram[VRAM_Buffer1 + 10 + x] = a;
  lda_abs(VRAM_Buffer1_Offset); // add ten bytes to the vram buffer offset
  carry_flag = false; // and store
  adc_imm_fc(10);
  ram[VRAM_Buffer1_Offset] = a;
  
ExitRp:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset and leave
  return;
  
InitPlatformFall:
  tya(); // move offset of other platform from Y to X
  tax_fzn();
  cpu_call_begin(0xd59c); GetEnemyOffscreenBits(); cpu_call_end(); // get offscreen bits
  lda_imm_fzn(0x6);
  cpu_call_begin(0xd5a1); SetupFloateyNumber(); cpu_call_end(); // award 1000 points to player
  lda_abs(Player_Rel_XPos);
  ram[FloateyNum_X_Pos + x] = a; // put floatey number coordinates where player is
  lda_zp(Player_Y_Position);
  ram[FloateyNum_Y_Pos + x] = a;
  lda_imm_fzn(0x1); // set moving direction as flag for
  ram[Enemy_MovingDir + x] = a; // falling platforms
  StopPlatforms(); // fallthrough
  return;
  
PlatformFall:
  tya_fzn(); // save offset for other platform to stack
  pha();
  cpu_call_begin(0xd5bf); MoveFallingPlatform(); cpu_call_end(); // make current platform fall
  pla();
  tax_fzn(); // pull offset from stack and save to X
  cpu_call_begin(0xd5c4); MoveFallingPlatform(); cpu_call_end(); // make other platform fall
  ldx_zp(ObjectOffset);
  lda_absx_fn(PlatformCollisionFlag); // if player not standing on either platform,
  if (neg_flag) { goto ExPF; } // skip this part
  tax_fzn(); // transfer collision flag offset as offset to X
  cpu_call_begin(0xd5cf); PositionPlayerOnVPlat(); cpu_call_end(); // and position player appropriately
  
ExPF:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset and leave
  return;
  // --------------------------------
}

void SetupPlatformRope(void) {
  pha(); // save second/third copy to stack
  lda_zpy(Enemy_X_Position); // get horizontal coordinate
  carry_flag = false;
  adc_imm_fc(0x8); // add eight pixels
  ldx_abs_fz(SecondaryHardMode); // if secondary hard mode flag set,
  // use coordinate as-is
  if (zero_flag) {
    carry_flag = false;
    adc_imm_fc(0x10); // otherwise add sixteen more pixels
  }
  // GetLRp:
  pha(); // save modified horizontal coordinate to stack
  lda_zpy(Enemy_PageLoc);
  adc_imm(0x0); // add carry to page location
  ram[0x2] = a; // and save here
  pla(); // pull modified horizontal coordinate
  and_imm(0b11110000); // from the stack, mask out low nybble
  lsr_acc(); // and shift three bits to the right
  lsr_acc();
  lsr_acc();
  ram[0x0] = a; // store result here as part of name table low byte
  ldx_zpy(Enemy_Y_Position); // get vertical coordinate
  pla_fn(); // get second/third copy of vertical speed from stack
  // skip this part if moving downwards or not at all
  if (neg_flag) {
    txa();
    carry_flag = false;
    adc_imm(0x8); // add eight to vertical coordinate and
    tax(); // save as X
  }
  // GetHRp:
  txa(); // move vertical coordinate to A
  ldx_abs(VRAM_Buffer1_Offset); // get vram buffer offset
  asl_acc_fc();
  rol_acc_fc(); // rotate d7 to d0 and d6 into carry
  pha(); // save modified vertical coordinate to stack
  rol_acc(); // rotate carry to d0, thus d7 and d6 are at 2 LSB
  and_imm(0b00000011); // mask out all bits but d7 and d6, then set
  ora_imm(0b00100000); // d5 to get appropriate high byte of name table
  ram[0x1] = a; // address, then store
  lda_zp(0x2); // get saved page location from earlier
  and_imm(0x1); // mask out all but LSB
  asl_acc();
  asl_acc(); // shift twice to the left and save with the
  ora_zp(0x1); // rest of the bits of the high byte, to get
  ram[0x1] = a; // the proper name table and the right place on it
  pla(); // get modified vertical coordinate from stack
  and_imm(0b11100000); // mask out low nybble and LSB of high nybble
  carry_flag = false;
  adc_zp(0x0); // add to horizontal part saved here
  ram[0x0] = a; // save as name table low byte
  lda_zpy(Enemy_Y_Position);
  cmp_imm_fczn(0xe8); // if vertical position not below the
  if (carry_flag) {
    lda_zp(0x0);
    and_imm_fzn(0b10111111); // mask out d6 of low byte of name table address
    ram[0x0] = a;
    // ExPRp:
    return; // leave!
  }
}

void StopPlatforms(void) {
  cpu_call_begin(0xd5b3); InitVStf(); cpu_call_end(); // initialize vertical speed and low byte
  ram[Enemy_Y_Speed + y] = a; // for both platforms and leave
  ram[Enemy_Y_MoveForce + y] = a;
  return;
}

void YMovingPlatform(void) {
  lda_zpx(Enemy_Y_Speed); // if platform moving up or down, skip ahead to
  ora_absx_fz(Enemy_Y_MoveForce); // check on other position
  if (zero_flag) {
    ram[Enemy_YMF_Dummy + x] = a; // initialize dummy variable
    lda_zpx(Enemy_Y_Position);
    cmp_absx_fc(YPlatformTopYPos); // if current vertical position => top position, branch
    // ahead of all this
    if (!carry_flag) {
      lda_zp(FrameCounter);
      and_imm_fz(0b00000111); // check for every eighth frame
      if (zero_flag) {
        inc_zpx(Enemy_Y_Position); // increase vertical position every eighth frame
      }
      // SkipIY:
      goto ChkYPCollision; // skip ahead to last part
    }
  }
  // ChkYCenterPos:
  lda_zpx(Enemy_Y_Position); // if current vertical position < central position, branch
  cmp_zpx_fczn(YPlatformCenterYPos); // to slow ascent/move downwards
  if (carry_flag) {
    cpu_call_begin(0xd5f7); MovePlatformUp(); cpu_call_end(); // otherwise start slowing descent/moving upwards
    goto ChkYPCollision;
  }
  // YMDown:
  cpu_call_begin(0xd5fd); MovePlatformDown(); cpu_call_end(); // start slowing ascent/moving downwards
  
ChkYPCollision:
  lda_absx_fzn(PlatformCollisionFlag); // if collision flag not set here, branch
  if (!neg_flag) {
    cpu_call_begin(0xd605); PositionPlayerOnVPlat(); cpu_call_end(); // otherwise position player appropriately
    // ExYPl:
    return; // leave
    // --------------------------------
    // $00 - used as adder to position player hotizontally
  }
}

void XMovingPlatform(void) {
  lda_imm_fzn(0xe); // load preset maximum value for secondary counter
  cpu_call_begin(0xd60b); XMoveCntr_Platform(); cpu_call_end(); // do a sub to increment counters for movement
  cpu_call_begin(0xd60e); MoveWithXMCntrs(); cpu_call_end(); // do a sub to move platform accordingly, and return value
  lda_absx_fzn(PlatformCollisionFlag); // if no collision with player,
  if (!neg_flag) {
    PositionPlayerOnHPlat(); // fallthrough
    return;
  }
}

void PositionPlayerOnHPlat(void) {
  lda_zp(Player_X_Position);
  carry_flag = false; // add saved value from second subroutine to
  adc_zp_fc(0x0); // current player's position to position
  ram[Player_X_Position] = a; // player accordingly in horizontal position
  lda_zp(Player_PageLoc); // get player's page location
  ldy_zp_fn(0x0); // check to see if saved value here is positive or negative
  // if negative, branch to subtract
  if (!neg_flag) {
    adc_imm_fczn(0x0); // otherwise add carry to page location
    goto SetPVar; // jump to skip subtraction
  }
  // PPHSubt:
  sbc_imm_fczn(0x0); // subtract borrow from page location
  
SetPVar:
  ram[Player_PageLoc] = a; // save result to player's page location
  ram[Platform_X_Scroll] = y; // put saved value from second sub here to be used later
  cpu_call_begin(0xd62f); PositionPlayerOnVPlat(); cpu_call_end(); // position player vertically and appropriately
  // ExXMP:
  return; // and we are done here
  // --------------------------------
}

void DropPlatform(void) {
  lda_absx_fzn(PlatformCollisionFlag); // if no collision between platform and player
  if (!neg_flag) {
    cpu_call_begin(0xd638); MoveDropPlatform(); cpu_call_end(); // otherwise do a sub to move platform down very quickly
    cpu_call_begin(0xd63b); PositionPlayerOnVPlat(); cpu_call_end(); // do a sub to position player appropriately
    // ExDPl:
    return; // leave
    // --------------------------------
    // $00 - residual value from sub
  }
}

void RightPlatform(void) {
  cpu_call_begin(0xd63f); MoveEnemyHorizontally(); cpu_call_end(); // move platform with current horizontal speed, if any
  ram[0x0] = a; // store saved value here (residual code)
  lda_absx_fzn(PlatformCollisionFlag); // check collision flag, if no collision between player
  if (!neg_flag) {
    lda_imm_fzn(0x10);
    ram[Enemy_X_Speed + x] = a; // otherwise set new speed (gets moving if motionless)
    cpu_call_begin(0xd64d); PositionPlayerOnHPlat(); cpu_call_end(); // use saved value from earlier sub to position player
    // ExRPl:
    return; // then leave
    // --------------------------------
  }
}

void MoveLargeLiftPlat(void) {
  goto MoveLargeLiftPlat;
  
ChkYPCollision:
  lda_absx_fzn(PlatformCollisionFlag); // if collision flag not set here, branch
  if (!neg_flag) {
    cpu_call_begin(0xd605); PositionPlayerOnVPlat(); cpu_call_end(); // otherwise position player appropriately
    // ExYPl:
    return; // leave
    // --------------------------------
    // $00 - used as adder to position player hotizontally
    
MoveLargeLiftPlat:
    cpu_call_begin(0xd651); MoveLiftPlatforms(); cpu_call_end(); // execute common to all large and small lift platforms
    goto ChkYPCollision; // branch to position player correctly
  }
}

void MoveSmallPlatform(void) {
  cpu_call_begin(0xd657); MoveLiftPlatforms(); cpu_call_end(); // execute common to all large and small lift platforms
  goto ChkSmallPlatCollision; // branch to position player correctly
  
ChkSmallPlatCollision:
  lda_absx_fzn(PlatformCollisionFlag); // get bounding box counter saved in collision flag
  if (!zero_flag) {
    cpu_call_begin(0xd678); PositionPlayerOnS_Plat(); cpu_call_end(); // use to position player correctly
    // ExLiftP:
    return; // then leave
    // -------------------------------------------------------------------------------------
    // $00 - page location of extended left boundary
    // $01 - extended left boundary position
    // $02 - page location of extended right boundary
    // $03 - extended right boundary position
  }
}

void MoveLiftPlatforms(void) {
  lda_abs_fzn(TimerControl); // if master timer control set, skip all of this
  if (zero_flag) {
    lda_absx(Enemy_YMF_Dummy);
    carry_flag = false; // add contents of movement amount to whatever's here
    adc_absx_fc(Enemy_Y_MoveForce);
    ram[Enemy_YMF_Dummy + x] = a;
    lda_zpx(Enemy_Y_Position); // add whatever vertical speed is set to current
    adc_zpx_fczn(Enemy_Y_Speed); // vertical position plus carry to move up or down
    ram[Enemy_Y_Position + x] = a; // and then leave
    return;
  }
}

void OffscreenBoundsCheck(void) {
  // OffscreenBoundsCheck:
  lda_zpx(Enemy_ID); // check for cheep-cheep object
  cmp_imm_fczn(FlyingCheepCheep); // branch to leave if found
  if (zero_flag) { return; }
  lda_abs(ScreenLeft_X_Pos); // get horizontal coordinate for left side of screen
  ldy_zpx(Enemy_ID);
  cpy_imm_fcz(HammerBro); // check for hammer bro object
  if (zero_flag) { goto LimitB; }
  cpy_imm_fcz(PiranhaPlant); // check for piranha plant object
  if (!zero_flag) { goto ExtendLB; } // these two will be erased sooner than others if too far left
  
LimitB:
  adc_imm_fc(0x38); // add 56 pixels to coordinate if hammer bro or piranha plant
  
ExtendLB:
  sbc_imm_fc(0x48); // subtract 72 pixels regardless of enemy object
  ram[0x1] = a; // store result here
  lda_abs(ScreenLeft_PageLoc);
  sbc_imm_fc(0x0); // subtract borrow from page location of left side
  ram[0x0] = a; // store result here
  lda_abs(ScreenRight_X_Pos); // add 72 pixels to the right side horizontal coordinate
  adc_imm_fc(0x48);
  ram[0x3] = a; // store result here
  lda_abs(ScreenRight_PageLoc);
  adc_imm(0x0); // then add the carry to the page location
  ram[0x2] = a; // and store result here
  lda_zpx(Enemy_X_Position); // compare horizontal coordinate of the enemy object
  cmp_zp_fc(0x1); // to modified horizontal left edge coordinate to get carry
  lda_zpx(Enemy_PageLoc);
  sbc_zp_fczn(0x0); // then subtract it from the page coordinate of the enemy object
  if (neg_flag) { goto TooFar; } // if enemy object is too far left, branch to erase it
  lda_zpx(Enemy_X_Position); // compare horizontal coordinate of the enemy object
  cmp_zp_fc(0x3); // to modified horizontal right edge coordinate to get carry
  lda_zpx(Enemy_PageLoc);
  sbc_zp_fczn(0x2); // then subtract it from the page coordinate of the enemy object
  if (neg_flag) { return; } // if enemy object is on the screen, leave, do not erase enemy
  lda_zpx(Enemy_State); // if at this point, enemy is offscreen to the right, so check
  cmp_imm_fczn(HammerBro); // if in state used by spiny's egg, do not erase
  if (zero_flag) { return; }
  cpy_imm_fczn(PiranhaPlant); // if piranha plant, do not erase
  if (zero_flag) { return; }
  cpy_imm_fczn(FlagpoleFlagObject); // if flagpole flag, do not erase
  if (zero_flag) { return; }
  cpy_imm_fczn(StarFlagObject); // if star flag, do not erase
  if (zero_flag) { return; }
  cpy_imm_fczn(JumpspringObject); // if jumpspring, do not erase
  if (zero_flag) { return; } // erase all others too far to the right
  
TooFar:
  cpu_call_begin(0xd6d4); EraseEnemyObject(); cpu_call_end(); // erase object if necessary
  // ExScrnBd:
  return; // leave
  // -------------------------------------------------------------------------------------
  // some unused space
  // -------------------------------------------------------------------------------------
  // $01 - enemy buffer offset
}

void FireballEnemyCollision(void) {
  // FireballEnemyCollision:
  lda_zpx_fz(Fireball_State); // check to see if fireball state is set at all
  if (zero_flag) { goto ExitFBallEnemy; } // branch to leave if not
  asl_acc_fc();
  if (carry_flag) { goto ExitFBallEnemy; } // branch to leave also if d7 in state is set
  lda_zp(FrameCounter);
  lsr_acc_fc(); // get LSB of frame counter
  if (carry_flag) { goto ExitFBallEnemy; } // branch to leave if set (do routine every other frame)
  txa();
  asl_acc(); // multiply fireball offset by four
  asl_acc();
  carry_flag = false;
  adc_imm_fc(0x1c); // then add $1c or 28 bytes to it
  tay(); // to use fireball's bounding box coordinates 
  ldx_imm(0x4);
  
FireballEnemyCDLoop:
  ram[0x1] = x; // store enemy object offset here
  tya();
  pha(); // push fireball offset to the stack
  lda_zpx(Enemy_State);
  and_imm_fz(0b00100000); // check to see if d5 is set in enemy state
  if (!zero_flag) { goto NoFToECol; } // if so, skip to next enemy slot
  lda_zpx_fz(Enemy_Flag); // check to see if buffer flag is set
  if (zero_flag) { goto NoFToECol; } // if not, skip to next enemy slot
  lda_zpx(Enemy_ID); // check enemy identifier
  cmp_imm_fc(0x24);
  if (!carry_flag) { goto GoombaDie; } // if < $24, branch to check further
  cmp_imm_fc(0x2b);
  if (!carry_flag) { goto NoFToECol; } // if in range $24-$2a, skip to next enemy slot
  
GoombaDie:
  cmp_imm_fcz(Goomba); // check for goomba identifier
  if (!zero_flag) { goto NotGoomba; } // if not found, continue with code
  lda_zpx(Enemy_State); // otherwise check for defeated state
  cmp_imm_fc(0x2); // if stomped or otherwise defeated,
  if (carry_flag) { goto NoFToECol; } // skip to next enemy slot
  
NotGoomba:
  lda_absx_fz(EnemyOffscrBitsMasked); // if any masked offscreen bits set,
  if (!zero_flag) { goto NoFToECol; } // skip to next enemy slot
  txa();
  asl_acc(); // otherwise multiply enemy offset by four
  asl_acc();
  carry_flag = false;
  adc_imm_fc(0x4); // add 4 bytes to it
  tax_fzn(); // to use enemy's bounding box coordinates
  cpu_call_begin(0xd71e); SprObjectCollisionCore(); cpu_call_end(); // do fireball-to-enemy collision detection
  ldx_zp(ObjectOffset); // return fireball's original offset
  if (!carry_flag) { goto NoFToECol; } // if carry clear, no collision, thus do next enemy slot
  lda_imm(0b10000000);
  ram[Fireball_State + x] = a; // set d7 in enemy state
  ldx_zp_fzn(0x1); // get enemy offset
  cpu_call_begin(0xd72b); HandleEnemyFBallCol(); cpu_call_end(); // jump to handle fireball to enemy collision
  
NoFToECol:
  pla(); // pull fireball offset from stack
  tay(); // put it in Y
  ldx_zp(0x1); // get enemy object offset
  dex_fn(); // decrement it
  if (!neg_flag) { goto FireballEnemyCDLoop; } // loop back until collision detection done on all enemies
  
ExitFBallEnemy:
  ldx_zp_fzn(ObjectOffset); // get original fireball offset and leave
  return;
}

void HandleEnemyFBallCol(void) {
  // HandleEnemyFBallCol:
  cpu_call_begin(0xd740); RelativeEnemyPosition(); cpu_call_end(); // get relative coordinate of enemy
  ldx_zp(0x1); // get current enemy object offset
  lda_zpx_fn(Enemy_Flag); // check buffer flag for d7 set
  if (!neg_flag) { goto ChkBuzzyBeetle; } // branch if not set to continue
  and_imm(0b00001111); // otherwise mask out high nybble and
  tax(); // use low nybble as enemy offset
  lda_zpx(Enemy_ID);
  cmp_imm_fcz(Bowser); // check enemy identifier for bowser
  if (zero_flag) { goto HurtBowser; } // branch if found
  ldx_zp(0x1); // otherwise retrieve current enemy offset
  
ChkBuzzyBeetle:
  lda_zpx(Enemy_ID);
  cmp_imm_fczn(BuzzyBeetle); // check for buzzy beetle
  if (zero_flag) { return; } // branch if found to leave (buzzy beetles fireproof)
  cmp_imm_fcz(Bowser); // check for bowser one more time (necessary if d7 of flag was clear)
  if (!zero_flag) { goto ChkOtherEnemies; } // if not found, branch to check other enemies
  
HurtBowser:
  dec_abs_fzn(BowserHitPoints); // decrement bowser's hit points
  if (!zero_flag) { return; } // if bowser still has hit points, branch to leave
  cpu_call_begin(0xd763); InitVStf(); cpu_call_end(); // otherwise do sub to init vertical speed and movement force
  ram[Enemy_X_Speed + x] = a; // initialize horizontal speed
  ram[EnemyFrenzyBuffer] = a; // init enemy frenzy buffer
  lda_imm(0xfe);
  ram[Enemy_Y_Speed + x] = a; // set vertical speed to make defeated bowser jump a little
  ldy_abs(WorldNumber); // use world number as offset
  lda_absy(BowserIdentities); // get enemy identifier to replace bowser with
  ram[Enemy_ID + x] = a; // set as new enemy identifier
  lda_imm(0x20); // set A to use starting value for state
  cpy_imm_fc(0x3); // check to see if using offset of 3 or more
  if (carry_flag) { goto SetDBSte; } // branch if so
  ora_imm(0x3); // otherwise add 3 to enemy state
  
SetDBSte:
  ram[Enemy_State + x] = a; // set defeated enemy state
  lda_imm(Sfx_BowserFall);
  ram[Square2SoundQueue] = a; // load bowser defeat sound
  ldx_zp(0x1); // get enemy offset
  lda_imm_fzn(0x9); // award 5000 points to player for defeating bowser
  goto EnemySmackScore; // unconditional branch to award points
  
ChkOtherEnemies:
  cmp_imm_fczn(BulletBill_FrenzyVar);
  if (zero_flag) { return; } // branch to leave if bullet bill (frenzy variant) 
  cmp_imm_fczn(Podoboo);
  if (zero_flag) { return; } // branch to leave if podoboo
  cmp_imm_fczn(0x15);
  if (carry_flag) { return; } // branch to leave if identifier => $15
  ShellOrBlockDefeat(); // fallthrough
  return;
  
EnemySmackScore:
  cpu_call_begin(0xd7be); SetupFloateyNumber(); cpu_call_end(); // update necessary score variables
  lda_imm_fzn(Sfx_EnemySmack); // play smack enemy sound
  ram[Square1SoundQueue] = a;
  // ExHCF:
  return; // and now let's leave
  // -------------------------------------------------------------------------------------
}

void ShellOrBlockDefeat(void) {
  lda_zpx(Enemy_ID); // check for piranha plant
  cmp_imm_fczn(PiranhaPlant);
  // branch if not found
  if (zero_flag) {
    lda_zpx(Enemy_Y_Position);
    adc_imm_fczn(0x18); // add 24 pixels to enemy object's vertical position
    ram[Enemy_Y_Position + x] = a;
  }
  // StnE:
  cpu_call_begin(0xd7a3); ChkToStunEnemies(); cpu_call_end(); // do yet another sub
  lda_zpx(Enemy_State);
  and_imm(0b00011111); // mask out 2 MSB of enemy object's state
  ora_imm(0b00100000); // set d5 to defeat enemy and save as new state
  ram[Enemy_State + x] = a;
  lda_imm(0x2); // award 200 points by default
  ldy_zpx(Enemy_ID); // check for hammer bro
  cpy_imm_fz(HammerBro);
  // branch if not found
  if (zero_flag) {
    lda_imm(0x6); // award 1000 points for hammer bro
  }
  // GoombaPoints:
  cpy_imm_fczn(Goomba); // check for goomba
  // branch if not found
  if (zero_flag) {
    lda_imm_fzn(0x1); // award 100 points for goomba
  }
  // EnemySmackScore:
  cpu_call_begin(0xd7be); SetupFloateyNumber(); cpu_call_end(); // update necessary score variables
  lda_imm_fzn(Sfx_EnemySmack); // play smack enemy sound
  ram[Square1SoundQueue] = a;
  // ExHCF:
  return; // and now let's leave
  // -------------------------------------------------------------------------------------
}

void PlayerHammerCollision(void) {
  // PlayerHammerCollision:
  lda_zp(FrameCounter); // get frame counter
  lsr_acc_fczn(); // shift d0 into carry
  if (!carry_flag) { return; } // branch to leave if d0 not set to execute every other frame
  lda_abs(TimerControl); // if either master timer control
  ora_abs_fzn(Misc_OffscreenBits); // or any offscreen bits for hammer are set,
  if (!zero_flag) { return; } // branch to leave
  txa();
  asl_acc(); // multiply misc object offset by four
  asl_acc();
  carry_flag = false;
  adc_imm_fc(0x24); // add 36 or $24 bytes to get proper offset
  tay_fzn(); // for misc object bounding box coordinates
  cpu_call_begin(0xd7da); PlayerCollisionCore(); cpu_call_end(); // do player-to-hammer collision detection
  ldx_zp(ObjectOffset); // get misc object offset
  if (!carry_flag) { goto ClHCol; } // if no collision, then branch
  lda_absx_fzn(Misc_Collision_Flag); // otherwise read collision flag
  if (!zero_flag) { return; } // if collision flag already set, branch to leave
  lda_imm(0x1);
  ram[Misc_Collision_Flag + x] = a; // otherwise set collision flag now
  lda_zpx(Misc_X_Speed);
  eor_imm(0xff); // get two's compliment of
  carry_flag = false; // hammer's horizontal speed
  adc_imm_fc(0x1);
  ram[Misc_X_Speed + x] = a; // set to send hammer flying the opposite direction
  lda_abs_fzn(StarInvincibleTimer); // if star mario invincibility timer set,
  if (!zero_flag) { return; } // branch to leave
  InjurePlayer(); return; // otherwise jump to hurt player, do not return
  
ClHCol:
  lda_imm_fzn(0x0); // clear collision flag
  ram[Misc_Collision_Flag + x] = a;
  // ExPHC:
  return;
  // -------------------------------------------------------------------------------------
}

void PlayerEnemyCollision(void) {
  goto PlayerEnemyCollision;
  
HandlePowerUpCollision:
  cpu_call_begin(0xd802); EraseEnemyObject(); cpu_call_end(); // erase the power-up object
  lda_imm_fzn(0x6);
  cpu_call_begin(0xd807); SetupFloateyNumber(); cpu_call_end(); // award 1000 points to player by default
  lda_imm(Sfx_PowerUpGrab);
  ram[Square2SoundQueue] = a; // play the power-up sound
  lda_zp(PowerUpType); // check power-up type
  cmp_imm_fc(0x2);
  if (!carry_flag) { goto Shroom_Flower_PUp; } // if mushroom or fire flower, branch
  cmp_imm_fcz(0x3);
  if (zero_flag) { goto SetFor1Up; } // if 1-up mushroom, branch
  lda_imm(0x23); // otherwise set star mario invincibility
  ram[StarInvincibleTimer] = a; // timer, and load the star mario music
  lda_imm_fzn(StarPowerMusic); // into the area music queue, then leave
  ram[AreaMusicQueue] = a;
  return;
  
Shroom_Flower_PUp:
  lda_abs_fz(PlayerStatus); // if player status = small, branch
  if (zero_flag) { goto UpToSuper; }
  cmp_imm_fczn(0x1); // if player status not super, leave
  if (!zero_flag) { return; }
  ldx_zp(ObjectOffset); // get enemy offset, not necessary
  lda_imm_fzn(0x2); // set player status to fiery
  ram[PlayerStatus] = a;
  cpu_call_begin(0xd832); GetPlayerColors(); cpu_call_end(); // run sub to change colors of player
  ldx_zp(ObjectOffset); // get enemy offset again, and again not necessary
  lda_imm(0xc); // set value to be used by subroutine tree (fiery)
  goto UpToFiery; // jump to set values accordingly
  
SetFor1Up:
  lda_imm_fzn(0xb); // change 1000 points into 1-up instead
  ram[FloateyNum_Control + x] = a; // and then leave
  return;
  
UpToSuper:
  lda_imm(0x1); // set player status to super
  ram[PlayerStatus] = a;
  lda_imm(0x9); // set value to be used by subroutine tree (super)
  
UpToFiery:
  ldy_imm_fzn(0x0); // set value to be used as new player state
  cpu_call_begin(0xd84b); SetPRout(); cpu_call_end(); // set values to stop certain things in motion
  // NoPUp:
  return;
  // --------------------------------
  
PlayerEnemyCollision:
  lda_zp(FrameCounter); // check counter for d0 set
  lsr_acc_fczn();
  if (carry_flag) { return; } // if set, branch to leave
  cpu_call_begin(0xd85a); CheckPlayerVertical(); cpu_call_end(); // if player object is completely offscreen or
  if (carry_flag) { return; } // if down past 224th pixel row, branch to leave
  lda_absx_fzn(EnemyOffscrBitsMasked); // if current enemy is offscreen by any amount,
  if (!zero_flag) { return; } // go ahead and branch to leave
  lda_zp(GameEngineSubroutine);
  cmp_imm_fczn(0x8); // if not set to run player control routine
  if (!zero_flag) { return; } // on next frame, branch to leave
  lda_zpx(Enemy_State);
  and_imm_fzn(0b00100000); // if enemy state has d5 set, branch to leave
  if (!zero_flag) { return; }
  cpu_call_begin(0xd870); GetEnemyBoundBoxOfs(); cpu_call_end(); // get bounding box offset for current enemy object
  cpu_call_begin(0xd873); PlayerCollisionCore(); cpu_call_end(); // do collision detection on player vs. enemy
  ldx_zp(ObjectOffset); // get enemy object buffer offset
  if (carry_flag) { goto CheckForPUpCollision; } // if collision, branch past this part here
  lda_absx(Enemy_CollisionBits);
  and_imm_fzn(0b11111110); // otherwise, clear d0 of current enemy object's
  ram[Enemy_CollisionBits + x] = a; // collision bit
  // NoPECol:
  return;
  
CheckForPUpCollision:
  ldy_zpx(Enemy_ID);
  cpy_imm_fczn(PowerUpObject); // check for power-up object
  if (!zero_flag) { goto EColl; } // if not found, branch to next part
  goto HandlePowerUpCollision; // otherwise, unconditional jump backwards
  
EColl:
  lda_abs_fz(StarInvincibleTimer); // if star mario invincibility timer expired,
  if (zero_flag) { goto HandlePECollisions; } // perform task here, otherwise kill enemy like
  ShellOrBlockDefeat(); return; // hit with a shell, or from beneath
  
HandlePECollisions:
  lda_absx(Enemy_CollisionBits); // check enemy collision bits for d0 set
  and_imm(0b00000001); // or for being offscreen at all
  ora_absx_fzn(EnemyOffscrBitsMasked);
  if (!zero_flag) { return; } // branch to leave if either is true
  lda_imm(0x1);
  ora_absx(Enemy_CollisionBits); // otherwise set d0 now
  ram[Enemy_CollisionBits + x] = a;
  cpy_imm_fz(Spiny); // branch if spiny
  if (zero_flag) { goto ChkForPlayerInjury; }
  cpy_imm_fcz(PiranhaPlant); // branch if piranha plant
  if (zero_flag) { InjurePlayer(); return; }
  cpy_imm_fcz(Podoboo); // branch if podoboo
  if (zero_flag) { InjurePlayer(); return; }
  cpy_imm_fz(BulletBill_CannonVar); // branch if bullet bill
  if (zero_flag) { goto ChkForPlayerInjury; }
  cpy_imm_fc(0x15); // branch if object => $15
  if (carry_flag) { InjurePlayer(); return; }
  lda_abs_fz(AreaType); // branch if water type level
  if (zero_flag) { InjurePlayer(); return; }
  lda_zpx(Enemy_State); // branch if d7 of enemy state was set
  asl_acc_fc();
  if (carry_flag) { goto ChkForPlayerInjury; }
  lda_zpx(Enemy_State); // mask out all but 3 LSB of enemy state
  and_imm(0b00000111);
  cmp_imm_fc(0x2); // branch if enemy is in normal or falling state
  if (!carry_flag) { goto ChkForPlayerInjury; }
  lda_zpx(Enemy_ID); // branch to leave if goomba in defeated state
  cmp_imm_fczn(Goomba);
  if (zero_flag) { return; }
  lda_imm(Sfx_EnemySmack); // play smack enemy sound
  ram[Square1SoundQueue] = a;
  lda_zpx(Enemy_State); // set d7 in enemy state, thus become moving shell
  ora_imm_fzn(0b10000000);
  ram[Enemy_State + x] = a;
  cpu_call_begin(0xd8df); EnemyFacePlayer(); cpu_call_end(); // set moving direction and get offset
  lda_absy(KickedShellXSpdData); // load and set horizontal speed data with offset
  ram[Enemy_X_Speed + x] = a;
  lda_imm(0x3); // add three to whatever the stomp counter contains
  carry_flag = false; // to give points for kicking the shell
  adc_abs(StompChainCounter);
  ldy_absx(EnemyIntervalTimer); // check shell enemy's timer
  cpy_imm_fczn(0x3); // if above a certain point, branch using the points
  if (carry_flag) { goto KSPts; } // data obtained from the stomp counter + 3
  lda_absy_fzn(KickedShellPtsData); // otherwise, set points based on proximity to timer expiration
  
KSPts:
  cpu_call_begin(0xd8f7); SetupFloateyNumber(); cpu_call_end(); // set values for floatey number now
  // ExPEC:
  return; // leave!!!
  
ChkForPlayerInjury:
  lda_zp_fzn(Player_Y_Speed); // check player's vertical speed
  if (neg_flag) { goto ChkInj; } // perform procedure below if player moving upwards
  if (!zero_flag) { goto EnemyStomped; } // or not at all, and branch elsewhere if moving downwards
  
ChkInj:
  lda_zpx(Enemy_ID); // branch if enemy object < $07
  cmp_imm_fc(Bloober);
  if (!carry_flag) { goto ChkETmrs; }
  lda_zp(Player_Y_Position); // add 12 pixels to player's vertical position
  carry_flag = false;
  adc_imm(0xc);
  cmp_zpx_fc(Enemy_Y_Position); // compare modified player's position to enemy's position
  if (!carry_flag) { goto EnemyStomped; } // branch if this player's position above (less than) enemy's
  
ChkETmrs:
  lda_abs_fz(StompTimer); // check stomp timer
  if (!zero_flag) { goto EnemyStomped; } // branch if set
  lda_abs_fz(InjuryTimer); // check to see if injured invincibility timer still
  if (!zero_flag) { goto ExInjColRoutines; } // counting down, and branch elsewhere to leave if so
  lda_abs(Player_Rel_XPos);
  cmp_abs_fc(Enemy_Rel_XPos); // if player's relative position to the left of enemy's
  if (!carry_flag) { goto TInjE; } // relative position, branch here
  goto ChkEnemyFaceRight; // otherwise do a jump here
  
TInjE:
  lda_zpx(Enemy_MovingDir); // if enemy moving towards the left,
  cmp_imm_fczn(0x1); // branch, otherwise do a jump here
  if (!zero_flag) { InjurePlayer(); return; } // to turn the enemy around
  goto LInj;
  
ExInjColRoutines:
  ldx_zp_fzn(ObjectOffset); // get enemy offset and leave
  return;
  
EnemyStomped:
  lda_zpx(Enemy_ID); // check for spiny, branch to hurt player
  cmp_imm_fcz(Spiny); // if found
  if (zero_flag) { InjurePlayer(); return; }
  lda_imm(Sfx_EnemyStomp); // otherwise play stomp/swim sound
  ram[Square1SoundQueue] = a;
  lda_zpx(Enemy_ID);
  ldy_imm(0x0); // initialize points data offset for stomped enemies
  cmp_imm_fcz(FlyingCheepCheep); // branch for cheep-cheep
  if (zero_flag) { goto EnemyStompedPts; }
  cmp_imm_fcz(BulletBill_FrenzyVar); // branch for either bullet bill object
  if (zero_flag) { goto EnemyStompedPts; }
  cmp_imm_fcz(BulletBill_CannonVar);
  if (zero_flag) { goto EnemyStompedPts; }
  cmp_imm_fcz(Podoboo); // branch for podoboo (this branch is logically impossible
  if (zero_flag) { goto EnemyStompedPts; } // for cpu to take due to earlier checking of podoboo)
  iny(); // increment points data offset
  cmp_imm_fcz(HammerBro); // branch for hammer bro
  if (zero_flag) { goto EnemyStompedPts; }
  iny(); // increment points data offset
  cmp_imm_fcz(Lakitu); // branch for lakitu
  if (zero_flag) { goto EnemyStompedPts; }
  iny(); // increment points data offset
  cmp_imm_fcz(Bloober); // branch if NOT bloober
  if (!zero_flag) { goto ChkForDemoteKoopa; }
  
EnemyStompedPts:
  lda_absy_fzn(StompedEnemyPtsData); // load points data using offset in Y
  cpu_call_begin(0xd99b); SetupFloateyNumber(); cpu_call_end(); // run sub to set floatey number controls
  lda_zpx_fzn(Enemy_MovingDir);
  pha(); // save enemy movement direction to stack
  cpu_call_begin(0xd9a1); SetStun(); cpu_call_end(); // run sub to kill enemy
  pla();
  ram[Enemy_MovingDir + x] = a; // return enemy movement direction from stack
  lda_imm_fzn(0b00100000);
  ram[Enemy_State + x] = a; // set d5 in enemy state
  cpu_call_begin(0xd9ab); InitVStf(); cpu_call_end(); // nullify vertical speed, physics-related thing,
  ram[Enemy_X_Speed + x] = a; // and horizontal speed
  lda_imm_fzn(0xfd); // set player's vertical speed, to give bounce
  ram[Player_Y_Speed] = a;
  return;
  
ChkForDemoteKoopa:
  cmp_imm_fc(0x9); // branch elsewhere if enemy object < $09
  if (!carry_flag) { goto HandleStompedShellE; }
  and_imm(0b00000001); // demote koopa paratroopas to ordinary troopas
  ram[Enemy_ID + x] = a;
  ldy_imm(0x0); // return enemy to normal state
  ram[Enemy_State + x] = y;
  lda_imm_fzn(0x3); // award 400 points to the player
  cpu_call_begin(0xd9c3); SetupFloateyNumber(); cpu_call_end();
  cpu_call_begin(0xd9c6); InitVStf(); cpu_call_end(); // nullify physics-related thing and vertical speed
  cpu_call_begin(0xd9c9); EnemyFacePlayer(); cpu_call_end(); // turn enemy around if necessary
  lda_absy(DemotedKoopaXSpdData);
  ram[Enemy_X_Speed + x] = a; // set appropriate moving speed based on direction
  goto SBnce; // then move onto something else
  
HandleStompedShellE:
  lda_imm(0x4); // set defeated state for enemy
  ram[Enemy_State + x] = a;
  inc_abs(StompChainCounter); // increment the stomp counter
  lda_abs(StompChainCounter); // add whatever is in the stomp counter
  carry_flag = false; // to whatever is in the stomp timer
  adc_abs_fczn(StompTimer);
  cpu_call_begin(0xd9e4); SetupFloateyNumber(); cpu_call_end(); // award points accordingly
  inc_abs(StompTimer); // increment stomp timer of some sort
  ldy_abs(PrimaryHardMode); // check primary hard mode flag
  lda_absy(RevivalRateData); // load timer setting according to flag
  ram[EnemyIntervalTimer + x] = a; // set as enemy timer to revive stomped enemy
  
SBnce:
  lda_imm_fzn(0xfc); // set player's vertical speed for bounce
  ram[Player_Y_Speed] = a; // and then leave!!!
  return;
  
ChkEnemyFaceRight:
  lda_zpx(Enemy_MovingDir); // check to see if enemy is moving to the right
  cmp_imm_fczn(0x1);
  if (!zero_flag) { goto LInj; } // if not, branch
  InjurePlayer(); return; // otherwise go back to hurt player
  
LInj:
  cpu_call_begin(0xda01); EnemyTurnAround(); cpu_call_end(); // turn the enemy around, if necessary
  InjurePlayer(); return; // go back to hurt player
}

void InjurePlayer(void) {
  lda_abs_fz(InjuryTimer); // check again to see if injured invincibility timer is
  // at zero, and branch to leave if so
  if (zero_flag) {
    ForceInjury(); // fallthrough
    return;
  }
  // ExInjColRoutines:
  ldx_zp_fzn(ObjectOffset); // get enemy offset and leave
  return;
}

void ForceInjury(void) {
  ldx_abs_fz(PlayerStatus); // check player's status
  // branch if small
  if (!zero_flag) {
    ram[PlayerStatus] = a; // otherwise set player's status to small
    lda_imm(0x8);
    ram[InjuryTimer] = a; // set injured invincibility timer
    asl_acc_fczn();
    ram[Square1SoundQueue] = a; // play pipedown/injury sound
    cpu_call_begin(0xd943); GetPlayerColors(); cpu_call_end(); // change player's palette if necessary
    lda_imm(0xa); // set subroutine to run on next frame
    
SetKRout:
    ldy_imm(0x1); // set new player state
    SetPRout(); // fallthrough
    return;
  }
  // KillPlayer:
  ram[Player_X_Speed] = x; // halt player's horizontal movement by initializing speed
  inx();
  ram[EventMusicQueue] = x; // set event music queue to death music
  lda_imm(0xfc);
  ram[Player_Y_Speed] = a; // set new vertical speed
  lda_imm(0xb); // set subroutine to run on next frame
  goto SetKRout; // branch to set player's state and other things
}

void SetPRout(void) {
  ram[GameEngineSubroutine] = a; // load new value to run subroutine on next frame
  ram[Player_State] = y; // store new player state
  ldy_imm(0xff);
  ram[TimerControl] = y; // set master timer control flag to halt timers
  iny();
  ram[ScrollAmount] = y; // initialize scroll speed
  // ExInjColRoutines:
  ldx_zp_fzn(ObjectOffset); // get enemy offset and leave
  return;
}

void EnemyFacePlayer(void) {
  ldy_imm_fzn(0x1); // set to move right by default
  cpu_call_begin(0xda09); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and enemy
  // if enemy is to the right of player, do not increment
  if (neg_flag) {
    iny(); // otherwise, increment to set to move to the left
  }
  // SFcRt:
  ram[Enemy_MovingDir + x] = y; // set moving direction here
  dey_fzn(); // then decrement to use as a proper offset
  return;
}

void SetupFloateyNumber(void) {
  ram[FloateyNum_Control + x] = a; // set number of points control for floatey numbers
  lda_imm(0x30);
  ram[FloateyNum_Timer + x] = a; // set timer for floatey numbers
  lda_zpx(Enemy_Y_Position);
  ram[FloateyNum_Y_Pos + x] = a; // set vertical coordinate
  lda_abs_fzn(Enemy_Rel_XPos);
  ram[FloateyNum_X_Pos + x] = a; // set horizontal coordinate and leave
  // ExSFN:
  return;
  // -------------------------------------------------------------------------------------
  // $01 - used to hold enemy offset for second enemy
}

void EnemiesCollision(void) {
  // EnemiesCollision:
  lda_zp(FrameCounter); // check counter for d0 set
  lsr_acc_fczn();
  if (!carry_flag) { return; } // if d0 not set, leave
  lda_abs_fzn(AreaType);
  if (zero_flag) { return; } // if water area type, leave
  lda_zpx(Enemy_ID);
  cmp_imm_fc(0x15); // if enemy object => $15, branch to leave
  if (carry_flag) { goto ExitECRoutine; }
  cmp_imm_fcz(Lakitu); // if lakitu, branch to leave
  if (zero_flag) { goto ExitECRoutine; }
  cmp_imm_fcz(PiranhaPlant); // if piranha plant, branch to leave
  if (zero_flag) { goto ExitECRoutine; }
  lda_absx_fzn(EnemyOffscrBitsMasked); // if masked offscreen bits nonzero, branch to leave
  if (!zero_flag) { goto ExitECRoutine; }
  cpu_call_begin(0xda52); GetEnemyBoundBoxOfs(); cpu_call_end(); // otherwise, do sub, get appropriate bounding box offset for
  dex_fn(); // first enemy we're going to compare, then decrement for second
  if (neg_flag) { goto ExitECRoutine; } // branch to leave if there are no other enemies
  
ECLoop:
  ram[0x1] = x; // save enemy object buffer offset for second enemy here
  tya(); // save first enemy's bounding box offset to stack
  pha();
  lda_zpx_fz(Enemy_Flag); // check enemy object enable flag
  if (zero_flag) { goto ReadyNextEnemy; } // branch if flag not set
  lda_zpx(Enemy_ID);
  cmp_imm_fc(0x15); // check for enemy object => $15
  if (carry_flag) { goto ReadyNextEnemy; } // branch if true
  cmp_imm_fcz(Lakitu);
  if (zero_flag) { goto ReadyNextEnemy; } // branch if enemy object is lakitu
  cmp_imm_fcz(PiranhaPlant);
  if (zero_flag) { goto ReadyNextEnemy; } // branch if enemy object is piranha plant
  lda_absx_fz(EnemyOffscrBitsMasked);
  if (!zero_flag) { goto ReadyNextEnemy; } // branch if masked offscreen bits set
  txa(); // get second enemy object's bounding box offset
  asl_acc(); // multiply by four, then add four
  asl_acc();
  carry_flag = false;
  adc_imm_fc(0x4);
  tax_fzn(); // use as new contents of X
  cpu_call_begin(0xda7a); SprObjectCollisionCore(); cpu_call_end(); // do collision detection using the two enemies here
  ldx_zp(ObjectOffset); // use first enemy offset for X
  ldy_zp(0x1); // use second enemy offset for Y
  if (!carry_flag) { goto NoEnemyCollision; } // if carry clear, no collision, branch ahead of this
  lda_zpx(Enemy_State);
  ora_zpy(Enemy_State); // check both enemy states for d7 set
  and_imm_fzn(0b10000000);
  if (!zero_flag) { goto YesEC; } // branch if at least one of them is set
  lda_absy(Enemy_CollisionBits); // load first enemy's collision-related bits
  and_absx_fz(SetBitsMask); // check to see if bit connected to second enemy is
  if (!zero_flag) { goto ReadyNextEnemy; } // already set, and move onto next enemy slot if set
  lda_absy(Enemy_CollisionBits);
  ora_absx_fzn(SetBitsMask); // if the bit is not set, set it now
  ram[Enemy_CollisionBits + y] = a;
  
YesEC:
  cpu_call_begin(0xda9d); ProcEnemyCollisions(); cpu_call_end(); // react according to the nature of collision
  goto ReadyNextEnemy; // move onto next enemy slot
  
NoEnemyCollision:
  lda_absy(Enemy_CollisionBits); // load first enemy's collision-related bits
  and_absx(ClearBitsMask); // clear bit connected to second enemy
  ram[Enemy_CollisionBits + y] = a; // then move onto next enemy slot
  
ReadyNextEnemy:
  pla(); // get first enemy's bounding box offset from the stack
  tay(); // use as Y again
  ldx_zp(0x1); // get and decrement second enemy's object buffer offset
  dex_fn();
  if (!neg_flag) { goto ECLoop; } // loop until all enemy slots have been checked
  
ExitECRoutine:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset
  return; // leave
}

void ProcEnemyCollisions(void) {
  // ProcEnemyCollisions:
  lda_zpy(Enemy_State); // check both enemy states for d5 set
  ora_zpx(Enemy_State);
  and_imm_fzn(0b00100000); // if d5 is set in either state, or both, branch
  if (!zero_flag) { return; } // to leave and do nothing else at this point
  lda_zpx(Enemy_State);
  cmp_imm_fc(0x6); // if second enemy state < $06, branch elsewhere
  if (!carry_flag) { goto ProcSecondEnemyColl; }
  lda_zpx(Enemy_ID); // check second enemy identifier for hammer bro
  cmp_imm_fczn(HammerBro); // if hammer bro found in alt state, branch to leave
  if (zero_flag) { return; }
  lda_zpy(Enemy_State); // check first enemy state for d7 set
  asl_acc_fc();
  if (!carry_flag) { goto ShellCollisions; } // branch if d7 is clear
  lda_imm_fzn(0x6);
  cpu_call_begin(0xdad3); SetupFloateyNumber(); cpu_call_end(); // award 1000 points for killing enemy
  cpu_call_begin(0xdad6); ShellOrBlockDefeat(); cpu_call_end(); // then kill enemy, then load
  ldy_zp(0x1); // original offset of second enemy
  
ShellCollisions:
  tya(); // move Y to X
  tax_fzn();
  cpu_call_begin(0xdadd); ShellOrBlockDefeat(); cpu_call_end(); // kill second enemy
  ldx_zp(ObjectOffset);
  lda_absx(ShellChainCounter); // get chain counter for shell
  carry_flag = false;
  adc_imm_fc(0x4); // add four to get appropriate point offset
  ldx_zp_fzn(0x1);
  cpu_call_begin(0xdaea); SetupFloateyNumber(); cpu_call_end(); // award appropriate number of points for second enemy
  ldx_zp(ObjectOffset); // load original offset of first enemy
  inc_absx_fzn(ShellChainCounter); // increment chain counter for additional enemies
  // ExitProcessEColl:
  return; // leave!!!
  
ProcSecondEnemyColl:
  lda_zpy(Enemy_State); // if first enemy state < $06, branch elsewhere
  cmp_imm_fc(0x6);
  if (!carry_flag) { goto MoveEOfs; }
  lda_zpy(Enemy_ID); // check first enemy identifier for hammer bro
  cmp_imm_fczn(HammerBro); // if hammer bro found in alt state, branch to leave
  if (zero_flag) { return; }
  cpu_call_begin(0xdb01); ShellOrBlockDefeat(); cpu_call_end(); // otherwise, kill first enemy
  ldy_zp(0x1);
  lda_absy(ShellChainCounter); // get chain counter for shell
  carry_flag = false;
  adc_imm_fc(0x4); // add four to get appropriate point offset
  ldx_zp_fzn(ObjectOffset);
  cpu_call_begin(0xdb0e); SetupFloateyNumber(); cpu_call_end(); // award appropriate number of points for first enemy
  ldx_zp(0x1); // load original offset of second enemy
  inc_absx_fzn(ShellChainCounter); // increment chain counter for additional enemies
  return; // leave!!!
  
MoveEOfs:
  tya(); // move Y ($01) to X
  tax_fzn();
  cpu_call_begin(0xdb19); EnemyTurnAround(); cpu_call_end(); // do the sub here using value from $01
  ldx_zp(ObjectOffset); // then do it again using value from $08
  EnemyTurnAround(); // fallthrough
  return;
}

void EnemyTurnAround(void) {
  // EnemyTurnAround:
  lda_zpx(Enemy_ID); // check for specific enemies
  cmp_imm_fczn(PiranhaPlant);
  if (zero_flag) { return; } // if piranha plant, leave
  cmp_imm_fczn(Lakitu);
  if (zero_flag) { return; } // if lakitu, leave
  cmp_imm_fczn(HammerBro);
  if (zero_flag) { return; } // if hammer bro, leave
  cmp_imm_fcz(Spiny);
  if (zero_flag) { goto RXSpd; } // if spiny, turn it around
  cmp_imm_fcz(GreenParatroopaJump);
  if (zero_flag) { goto RXSpd; } // if green paratroopa, turn it around
  cmp_imm_fczn(0x7);
  if (carry_flag) { return; } // if any OTHER enemy object => $07, leave
  
RXSpd:
  lda_zpx(Enemy_X_Speed); // load horizontal speed
  eor_imm(0xff); // get two's compliment for horizontal speed
  tay();
  iny();
  ram[Enemy_X_Speed + x] = y; // store as new horizontal speed
  lda_zpx(Enemy_MovingDir);
  eor_imm_fzn(0b00000011); // invert moving direction and store, then leave
  ram[Enemy_MovingDir + x] = a; // thus effectively turning the enemy around
  // ExTA:
  return; // leave!!!
  // -------------------------------------------------------------------------------------
  // $00 - vertical position of platform
}

void LargePlatformCollision(void) {
  lda_imm(0xff); // save value here
  ram[PlatformCollisionFlag + x] = a;
  lda_abs_fz(TimerControl); // check master timer control
  // if set, branch to leave
  if (zero_flag) {
    lda_zpx_fn(Enemy_State); // if d7 set in object state,
    // branch to leave
    if (!neg_flag) {
      lda_zpx(Enemy_ID);
      cmp_imm_fczn(0x24); // check enemy object identifier for
      // balance platform, branch if not found
      if (!zero_flag) {
        ChkForPlayerC_LargeP();
        return;
      }
      lda_zpx(Enemy_State);
      tax_fzn(); // set state as enemy offset here
      cpu_call_begin(0xdb5e); ChkForPlayerC_LargeP(); cpu_call_end(); // perform code with state offset, then original offset, in X
      ChkForPlayerC_LargeP(); // fallthrough
      return;
    }
  }
  // ExLPC:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset and leave
  return;
  // --------------------------------
  // $00 - counter for bounding boxes
}

void ChkForPlayerC_LargeP(void) {
  cpu_call_begin(0xdb61); CheckPlayerVertical(); cpu_call_end(); // figure out if player is below a certain point
  // or offscreen, branch to leave if true
  if (!carry_flag) {
    txa_fzn();
    cpu_call_begin(0xdb67); GetEnemyBoundBoxOfsArg(); cpu_call_end(); // get bounding box offset in Y
    lda_zpx(Enemy_Y_Position); // store vertical coordinate in
    ram[0x0] = a; // temp variable for now
    txa_fzn(); // send offset we're on to the stack
    pha();
    cpu_call_begin(0xdb70); PlayerCollisionCore(); cpu_call_end(); // do player-to-platform collision detection
    pla(); // retrieve offset from the stack
    tax_fzn();
    // if no collision, branch to leave
    if (carry_flag) {
      cpu_call_begin(0xdb77); ProcLPlatCollisions(); cpu_call_end(); // otherwise collision, perform sub
    }
  }
  // ExLPC:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset and leave
  return;
  // --------------------------------
  // $00 - counter for bounding boxes
}

void SmallPlatformCollision(void) {
  // SmallPlatformCollision:
  lda_abs_fzn(TimerControl); // if master timer control set,
  if (!zero_flag) { goto ExSPC; } // branch to leave
  ram[PlatformCollisionFlag + x] = a; // otherwise initialize collision flag
  cpu_call_begin(0xdb85); CheckPlayerVertical(); cpu_call_end(); // do a sub to see if player is below a certain point
  if (carry_flag) { goto ExSPC; } // or entirely offscreen, and branch to leave if true
  lda_imm(0x2);
  ram[0x0] = a; // load counter here for 2 bounding boxes
  
ChkSmallPlatLoop:
  ldx_zp_fzn(ObjectOffset); // get enemy object offset
  cpu_call_begin(0xdb90); GetEnemyBoundBoxOfs(); cpu_call_end(); // get bounding box offset in Y
  and_imm_fz(0b00000010); // if d1 of offscreen lower nybble bits was set
  if (!zero_flag) { goto ExSPC; } // then branch to leave
  lda_absy(BoundingBox_UL_YPos); // check top of platform's bounding box for being
  cmp_imm_fczn(0x20); // above a specific point
  if (!carry_flag) { goto MoveBoundBox; } // if so, branch, don't do collision detection
  cpu_call_begin(0xdb9e); PlayerCollisionCore(); cpu_call_end(); // otherwise, perform player-to-platform collision detection
  if (carry_flag) { goto ProcSPlatCollisions; } // skip ahead if collision
  
MoveBoundBox:
  lda_absy(BoundingBox_UL_YPos); // move bounding box vertical coordinates
  carry_flag = false; // 128 pixels downwards
  adc_imm(0x80);
  ram[BoundingBox_UL_YPos + y] = a;
  lda_absy(BoundingBox_DR_YPos);
  carry_flag = false;
  adc_imm_fc(0x80);
  ram[BoundingBox_DR_YPos + y] = a;
  dec_zp_fz(0x0); // decrement counter we set earlier
  if (!zero_flag) { goto ChkSmallPlatLoop; } // loop back until both bounding boxes are checked
  
ExSPC:
  ldx_zp_fzn(ObjectOffset); // get enemy object buffer offset, then leave
  return;
  // --------------------------------
  
ProcSPlatCollisions:
  ldx_zp(ObjectOffset); // return enemy object buffer offset to X, then continue
  ProcLPlatCollisions(); // fallthrough
  return;
}

void ProcLPlatCollisions(void) {
  // ProcLPlatCollisions:
  lda_absy(BoundingBox_DR_YPos); // get difference by subtracting the top
  carry_flag = true; // of the player's bounding box from the bottom
  sbc_abs(BoundingBox_UL_YPos); // of the platform's bounding box
  cmp_imm_fc(0x4); // if difference too large or negative,
  if (carry_flag) { goto ChkForTopCollision; } // branch, do not alter vertical speed of player
  lda_zp_fn(Player_Y_Speed); // check to see if player's vertical speed is moving down
  if (!neg_flag) { goto ChkForTopCollision; } // if so, don't mess with it
  lda_imm(0x1); // otherwise, set vertical
  ram[Player_Y_Speed] = a; // speed of player to kill jump
  
ChkForTopCollision:
  lda_abs(BoundingBox_DR_YPos); // get difference by subtracting the top
  carry_flag = true; // of the platform's bounding box from the bottom
  sbc_absy(BoundingBox_UL_YPos); // of the player's bounding box
  cmp_imm_fc(0x6);
  if (carry_flag) { goto PlatformSideCollisions; } // if difference not close enough, skip all of this
  lda_zp_fn(Player_Y_Speed);
  if (neg_flag) { goto PlatformSideCollisions; } // if player's vertical speed moving upwards, skip this
  lda_zp(0x0); // get saved bounding box counter from earlier
  ldy_zpx(Enemy_ID);
  cpy_imm_fcz(0x2b); // if either of the two small platform objects are found,
  if (zero_flag) { goto SetCollisionFlag; } // regardless of which one, branch to use bounding box counter
  cpy_imm_fcz(0x2c); // as contents of collision flag
  if (zero_flag) { goto SetCollisionFlag; }
  txa(); // otherwise use enemy object buffer offset
  
SetCollisionFlag:
  ldx_zp(ObjectOffset); // get enemy object buffer offset
  ram[PlatformCollisionFlag + x] = a; // save either bounding box counter or enemy offset here
  lda_imm_fzn(0x0);
  ram[Player_State] = a; // set player state to normal then leave
  return;
  
PlatformSideCollisions:
  lda_imm(0x1); // set value here to indicate possible horizontal
  ram[0x0] = a; // collision on left side of platform
  lda_abs(BoundingBox_DR_XPos); // get difference by subtracting platform's left edge
  carry_flag = true; // from player's right edge
  sbc_absy(BoundingBox_UL_XPos);
  cmp_imm_fczn(0x8); // if difference close enough, skip all of this
  if (!carry_flag) { goto SideC; }
  inc_zp(0x0); // otherwise increment value set here for right side collision
  lda_absy(BoundingBox_DR_XPos); // get difference by subtracting player's left edge
  carry_flag = false; // from platform's right edge
  sbc_abs(BoundingBox_UL_XPos);
  cmp_imm_fczn(0x9); // if difference not close enough, skip subroutine
  if (carry_flag) { goto NoSideC; } // and instead branch to leave (no collision)
  
SideC:
  cpu_call_begin(0xdc13); ImpedePlayerMove(); cpu_call_end(); // deal with horizontal collision
  
NoSideC:
  ldx_zp_fzn(ObjectOffset); // return with enemy object buffer offset
  return;
  // -------------------------------------------------------------------------------------
}

void PositionPlayerOnS_Plat(void) {
  // PositionPlayerOnS_Plat:
  tay(); // use bounding box counter saved in collision flag
  lda_zpx(Enemy_Y_Position); // for offset
  carry_flag = false; // add positioning data using offset to the vertical
  adc_absy(PlayerPosSPlatData - 1); // coordinate
  // loc_56352:
  bit_abs(0xcfb5);
  goto loc_56355; // BIT instruction opcode
  
loc_56355:
  ldy_zp(GameEngineSubroutine);
  cpy_imm_fczn(0xb); // if certain routine being executed on this frame,
  if (zero_flag) { return; } // skip all of this
  ldy_zpx(Enemy_Y_HighPos);
  cpy_imm_fczn(0x1); // if vertical high byte offscreen, skip this
  if (!zero_flag) { return; }
  carry_flag = true; // subtract 32 pixels from vertical coordinate
  sbc_imm_fc(0x20); // for the player object's height
  ram[Player_Y_Position] = a; // save as player's new vertical coordinate
  tya();
  sbc_imm_fc(0x0); // subtract borrow and store as player's
  ram[Player_Y_HighPos] = a; // new vertical high byte
  lda_imm_fzn(0x0);
  ram[Player_Y_Speed] = a; // initialize vertical speed and low byte of force
  ram[Player_Y_MoveForce] = a; // and then leave
  // ExPlPos:
  return;
  // -------------------------------------------------------------------------------------
}

void PositionPlayerOnVPlat(void) {
  // PositionPlayerOnVPlat:
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  // loc_56355:
  ldy_zp(GameEngineSubroutine);
  cpy_imm_fczn(0xb); // if certain routine being executed on this frame,
  if (zero_flag) { return; } // skip all of this
  ldy_zpx(Enemy_Y_HighPos);
  cpy_imm_fczn(0x1); // if vertical high byte offscreen, skip this
  if (!zero_flag) { return; }
  carry_flag = true; // subtract 32 pixels from vertical coordinate
  sbc_imm_fc(0x20); // for the player object's height
  ram[Player_Y_Position] = a; // save as player's new vertical coordinate
  tya();
  sbc_imm_fc(0x0); // subtract borrow and store as player's
  ram[Player_Y_HighPos] = a; // new vertical high byte
  lda_imm_fzn(0x0);
  ram[Player_Y_Speed] = a; // initialize vertical speed and low byte of force
  ram[Player_Y_MoveForce] = a; // and then leave
  // ExPlPos:
  return;
  // -------------------------------------------------------------------------------------
}

void CheckPlayerVertical(void) {
  // CheckPlayerVertical:
  lda_abs(Player_OffscreenBits); // if player object is completely offscreen
  cmp_imm_fczn(0xf0); // vertically, leave this routine
  if (carry_flag) { return; }
  ldy_zp(Player_Y_HighPos); // if player high vertical byte is not
  dey_fzn(); // within the screen, leave this routine
  if (!zero_flag) { return; }
  lda_zp(Player_Y_Position); // if on the screen, check to see how far down
  cmp_imm_fczn(0xd0); // the player is vertically
  // ExCPV:
  return;
  // -------------------------------------------------------------------------------------
}

void GetEnemyBoundBoxOfs(void) {
  lda_zp(ObjectOffset); // get enemy object buffer offset
  GetEnemyBoundBoxOfsArg(); // fallthrough
  return;
}

void GetEnemyBoundBoxOfsArg(void) {
  asl_acc(); // multiply A by four, then add four
  asl_acc(); // to skip player's bounding box
  carry_flag = false;
  adc_imm(0x4);
  tay(); // send to Y
  lda_abs(Enemy_OffscreenBits); // get offscreen bits for enemy object
  and_imm(0b00001111); // save low nybble
  cmp_imm_fczn(0b00001111); // check for all bits set
  return;
  // -------------------------------------------------------------------------------------
  // $00-$01 - used to hold many values, essentially temp variables
  // $04 - holds lower nybble of vertical coordinate from block buffer routine
  // $eb - used to hold block buffer adder
}

void PlayerBGCollision(void) {
  // PlayerBGCollision:
  lda_abs_fzn(DisableCollisionDet); // if collision detection disabled flag set,
  if (!zero_flag) { return; } // branch to leave
  lda_zp(GameEngineSubroutine);
  cmp_imm_fczn(0xb); // if running routine #11 or $0b
  if (zero_flag) { return; } // branch to leave
  cmp_imm_fczn(0x4);
  if (!carry_flag) { return; } // if running routines $00-$03 branch to leave
  lda_imm(0x1); // load default player state for swimming
  ldy_abs_fz(SwimmingFlag); // if swimming flag set,
  if (!zero_flag) { goto SetPSte; } // branch ahead to set default state
  lda_zp_fz(Player_State); // if player in normal state,
  if (zero_flag) { goto SetFallS; } // branch to set default state for falling
  cmp_imm_fz(0x3);
  if (!zero_flag) { goto ChkOnScr; } // if in any other state besides climbing, skip to next part
  
SetFallS:
  lda_imm(0x2); // load default player state for falling
  
SetPSte:
  ram[Player_State] = a; // set whatever player state is appropriate
  
ChkOnScr:
  lda_zp(Player_Y_HighPos);
  cmp_imm_fczn(0x1); // check player's vertical high byte for still on the screen
  if (!zero_flag) { return; } // branch to leave if not
  lda_imm(0xff);
  ram[Player_CollisionBits] = a; // initialize player's collision flag
  lda_zp(Player_Y_Position);
  cmp_imm_fczn(0xcf); // check player's vertical coordinate
  if (!carry_flag) { goto ChkCollSize; } // if not too close to the bottom of screen, continue
  // ExPBGCol:
  return; // otherwise leave
  
ChkCollSize:
  ldy_imm(0x2); // load default offset
  lda_abs_fz(CrouchingFlag);
  if (!zero_flag) { goto GBBAdr; } // if player crouching, skip ahead
  lda_abs_fz(PlayerSize);
  if (!zero_flag) { goto GBBAdr; } // if player small, skip ahead
  dey(); // otherwise decrement offset for big player not crouching
  lda_abs_fz(SwimmingFlag);
  if (!zero_flag) { goto GBBAdr; } // if swimming flag set, skip ahead
  dey(); // otherwise decrement offset
  
GBBAdr:
  lda_absy(BlockBufferAdderData); // get value using offset
  ram[0xeb] = a; // store value here
  tay(); // put value into Y, as offset for block buffer routine
  ldx_abs(PlayerSize); // get player's size as offset
  lda_abs_fz(CrouchingFlag);
  if (zero_flag) { goto HeadChk; } // if player not crouching, branch ahead
  inx(); // otherwise increment size as offset
  
HeadChk:
  lda_zp(Player_Y_Position); // get player's vertical coordinate
  cmp_absx_fczn(PlayerBGUpperExtent); // compare with upper extent value based on offset
  if (!carry_flag) { goto DoFootCheck; } // if player is too high, skip this part
  cpu_call_begin(0xdcc3); BlockBufferColli_Head(); cpu_call_end(); // do player-to-bg collision detection on top of
  if (zero_flag) { goto DoFootCheck; } // player, and branch if nothing above player's head
  cpu_call_begin(0xdcc8); CheckForCoinMTiles(); cpu_call_end(); // check to see if player touched coin with their head
  if (carry_flag) { goto AwardTouchedCoin; } // if so, branch to some other part of code
  ldy_zp_fn(Player_Y_Speed); // check player's vertical speed
  if (!neg_flag) { goto DoFootCheck; } // if player not moving upwards, branch elsewhere
  ldy_zp(0x4); // check lower nybble of vertical coordinate returned
  cpy_imm_fczn(0x4); // from collision detection routine
  if (!carry_flag) { goto DoFootCheck; } // if low nybble < 4, branch
  cpu_call_begin(0xdcd7); CheckForSolidMTiles(); cpu_call_end(); // check to see what player's head bumped on
  if (carry_flag) { goto SolidOrClimb; } // if player collided with solid metatile, branch
  ldy_abs_fz(AreaType); // otherwise check area type
  if (zero_flag) { goto NYSpd; } // if water level, branch ahead
  ldy_abs_fzn(BlockBounceTimer); // if block bounce timer not expired,
  if (!zero_flag) { goto NYSpd; } // branch ahead, do not process collision
  cpu_call_begin(0xdce6); PlayerHeadCollision(); cpu_call_end(); // otherwise do a sub to process collision
  goto DoFootCheck; // jump ahead to skip these other parts here
  
SolidOrClimb:
  cmp_imm_fz(0x26); // if climbing metatile,
  if (zero_flag) { goto NYSpd; } // branch ahead and do not play sound
  lda_imm(Sfx_Bump);
  ram[Square1SoundQueue] = a; // otherwise load bump sound
  
NYSpd:
  lda_imm(0x1); // set player's vertical speed to nullify
  ram[Player_Y_Speed] = a; // jump or swim
  
DoFootCheck:
  ldy_zp(0xeb); // get block buffer adder offset
  lda_zp(Player_Y_Position);
  cmp_imm_fczn(0xcf); // check to see how low player is
  if (carry_flag) { goto DoPlayerSideCheck; } // if player is too far down on screen, skip all of this
  cpu_call_begin(0xdd00); BlockBufferColli_Feet(); cpu_call_end(); // do player-to-bg collision detection on bottom left of player
  cpu_call_begin(0xdd03); CheckForCoinMTiles(); cpu_call_end(); // check to see if player touched coin with their left foot
  if (carry_flag) { goto AwardTouchedCoin; } // if so, branch to some other part of code
  pha(); // save bottom left metatile to stack
  cpu_call_begin(0xdd09); BlockBufferColli_Feet(); cpu_call_end(); // do player-to-bg collision detection on bottom right of player
  ram[0x0] = a; // save bottom right metatile here
  pla_fzn();
  ram[0x1] = a; // pull bottom left metatile and save here
  if (!zero_flag) { goto ChkFootMTile; } // if anything here, skip this part
  lda_zp_fzn(0x0); // otherwise check for anything in bottom right metatile
  if (zero_flag) { goto DoPlayerSideCheck; } // and skip ahead if not
  cpu_call_begin(0xdd17); CheckForCoinMTiles(); cpu_call_end(); // check to see if player touched coin with their right foot
  if (!carry_flag) { goto ChkFootMTile; } // if not, skip unconditional jump and continue code
  
AwardTouchedCoin:
  goto HandleCoinMetatile; // follow the code to erase coin and award to player 1 coin
  
ChkFootMTile:
  cpu_call_begin(0xdd1f); CheckForClimbMTiles(); cpu_call_end(); // check to see if player landed on climbable metatiles
  if (carry_flag) { goto DoPlayerSideCheck; } // if so, branch
  ldy_zp_fn(Player_Y_Speed); // check player's vertical speed
  if (neg_flag) { goto DoPlayerSideCheck; } // if player moving upwards, branch
  cmp_imm_fczn(0xc5);
  if (!zero_flag) { goto ContChk; } // if player did not touch axe, skip ahead
  goto HandleAxeMetatile; // otherwise jump to set modes of operation
  
ContChk:
  cpu_call_begin(0xdd2f); ChkInvisibleMTiles(); cpu_call_end(); // do sub to check for hidden coin or 1-up blocks
  if (zero_flag) { goto DoPlayerSideCheck; } // if either found, branch
  ldy_abs_fz(JumpspringAnimCtrl); // if jumpspring animating right now,
  if (!zero_flag) { goto InitSteP; } // branch ahead
  ldy_zp(0x4); // check lower nybble of vertical coordinate returned
  cpy_imm_fczn(0x5); // from collision detection routine
  if (!carry_flag) { goto LandPlyr; } // if lower nybble < 5, branch
  lda_zp(Player_MovingDir);
  ram[0x0] = a; // use player's moving direction as temp variable
  ImpedePlayerMove(); return; // jump to impede player's movement in that direction
  
LandPlyr:
  cpu_call_begin(0xdd46); ChkForLandJumpSpring(); cpu_call_end(); // do sub to check for jumpspring metatiles and deal with it
  lda_imm(0xf0);
  and_zp_fzn(Player_Y_Position); // mask out lower nybble of player's vertical position
  ram[Player_Y_Position] = a; // and store as new vertical position to land player properly
  cpu_call_begin(0xdd4f); HandlePipeEntry(); cpu_call_end(); // do sub to process potential pipe entry
  lda_imm(0x0);
  ram[Player_Y_Speed] = a; // initialize vertical speed and fractional
  ram[Player_Y_MoveForce] = a; // movement force to stop player's vertical movement
  ram[StompChainCounter] = a; // initialize enemy stomp counter
  
InitSteP:
  lda_imm(0x0);
  ram[Player_State] = a; // set player's state to normal
  
DoPlayerSideCheck:
  ldy_zp(0xeb); // get block buffer adder offset
  iny();
  iny(); // increment offset 2 bytes to use adders for side collisions
  lda_imm(0x2); // set value here to be used as counter
  ram[0x0] = a;
  
SideCheckLoop:
  iny(); // move onto the next one
  ram[0xeb] = y; // store it
  lda_zp(Player_Y_Position);
  cmp_imm_fc(0x20); // check player's vertical position
  if (!carry_flag) { goto BHalf; } // if player is in status bar area, branch ahead to skip this part
  cmp_imm_fczn(0xe4);
  if (carry_flag) { return; } // branch to leave if player is too far down
  cpu_call_begin(0xdd75); BlockBufferColli_Side(); cpu_call_end(); // do player-to-bg collision detection on one half of player
  if (zero_flag) { goto BHalf; } // branch ahead if nothing found
  cmp_imm_fz(0x1c); // otherwise check for pipe metatiles
  if (zero_flag) { goto BHalf; } // if collided with sideways pipe (top), branch ahead
  cmp_imm_fczn(0x6b);
  if (zero_flag) { goto BHalf; } // if collided with water pipe (top), branch ahead
  cpu_call_begin(0xdd82); CheckForClimbMTiles(); cpu_call_end(); // do sub to see if player bumped into anything climbable
  if (!carry_flag) { goto CheckSideMTiles; } // if not, branch to alternate section of code
  
BHalf:
  ldy_zp(0xeb); // load block adder offset
  iny(); // increment it
  lda_zp(Player_Y_Position); // get player's vertical position
  cmp_imm_fczn(0x8);
  if (!carry_flag) { return; } // if too high, branch to leave
  cmp_imm_fczn(0xd0);
  if (carry_flag) { return; } // if too low, branch to leave
  cpu_call_begin(0xdd94); BlockBufferColli_Side(); cpu_call_end(); // do player-to-bg collision detection on other half of player
  if (!zero_flag) { goto CheckSideMTiles; } // if something found, branch
  dec_zp_fzn(0x0); // otherwise decrement counter
  if (!zero_flag) { goto SideCheckLoop; } // run code until both sides of player are checked
  // ExSCH:
  return; // leave
  
CheckSideMTiles:
  cpu_call_begin(0xdd9e); ChkInvisibleMTiles(); cpu_call_end(); // check for hidden or coin 1-up blocks
  if (zero_flag) { return; } // branch to leave if either found
  cpu_call_begin(0xdda3); CheckForClimbMTiles(); cpu_call_end(); // check for climbable metatiles
  if (!carry_flag) { goto ContSChk; } // if not found, skip and continue with code
  goto HandleClimbing; // otherwise jump to handle climbing
  
ContSChk:
  cpu_call_begin(0xddab); CheckForCoinMTiles(); cpu_call_end(); // check to see if player touched coin
  if (carry_flag) { goto HandleCoinMetatile; } // if so, execute code to erase coin and award to player 1 coin
  cpu_call_begin(0xddb0); ChkJumpspringMetatiles(); cpu_call_end(); // check for jumpspring metatiles
  if (!carry_flag) { goto ChkPBtm; } // if not found, branch ahead to continue cude
  lda_abs_fzn(JumpspringAnimCtrl); // otherwise check jumpspring animation control
  if (!zero_flag) { return; } // branch to leave if set
  goto StopPlayerMove; // otherwise jump to impede player's movement
  
ChkPBtm:
  ldy_zp(Player_State); // get player's state
  cpy_imm_fczn(0x0); // check for player's state set to normal
  if (!zero_flag) { goto StopPlayerMove; } // if not, branch to impede player's movement
  ldy_zp(PlayerFacingDir); // get player's facing direction
  dey_fzn();
  if (!zero_flag) { goto StopPlayerMove; } // if facing left, branch to impede movement
  cmp_imm_fz(0x6c); // otherwise check for pipe metatiles
  if (zero_flag) { goto PipeDwnS; } // if collided with sideways pipe (bottom), branch
  cmp_imm_fczn(0x1f); // if collided with water pipe (bottom), continue
  if (!zero_flag) { goto StopPlayerMove; } // otherwise branch to impede player's movement
  
PipeDwnS:
  lda_abs_fz(Player_SprAttrib); // check player's attributes
  if (!zero_flag) { goto PlyrPipe; } // if already set, branch, do not play sound again
  ldy_imm(Sfx_PipeDown_Injury);
  ram[Square1SoundQueue] = y; // otherwise load pipedown/injury sound
  
PlyrPipe:
  ora_imm(0b00100000);
  ram[Player_SprAttrib] = a; // set background priority bit in player attributes
  lda_zp(Player_X_Position);
  and_imm_fz(0b00001111); // get lower nybble of player's horizontal coordinate
  if (zero_flag) { goto ChkGERtn; } // if at zero, branch ahead to skip this part
  ldy_imm(0x0); // set default offset for timer setting data
  lda_abs_fz(ScreenLeft_PageLoc); // load page location for left side of screen
  if (zero_flag) { goto SetCATmr; } // if at page zero, use default offset
  iny(); // otherwise increment offset
  
SetCATmr:
  lda_absy(AreaChangeTimerData); // set timer for change of area as appropriate
  ram[ChangeAreaTimer] = a;
  
ChkGERtn:
  lda_zp(GameEngineSubroutine); // get number of game engine routine running
  cmp_imm_fczn(0x7);
  if (zero_flag) { return; } // if running player entrance routine or
  cmp_imm_fczn(0x8); // player control routine, go ahead and branch to leave
  if (!zero_flag) { return; }
  lda_imm_fzn(0x2);
  ram[GameEngineSubroutine] = a; // otherwise set sideways pipe entry routine to run
  return; // and leave
  // --------------------------------
  // $02 - high nybble of vertical coordinate from block buffer
  // $04 - low nybble of horizontal coordinate from block buffer
  // $06-$07 - block buffer address
  
StopPlayerMove:
  cpu_call_begin(0xde01); ImpedePlayerMove(); cpu_call_end(); // stop player's movement
  // ExCSM:
  return; // leave
  
HandleCoinMetatile:
  cpu_call_begin(0xde07); ErACM(); cpu_call_end(); // do sub to erase coin metatile from block buffer
  inc_abs(CoinTallyFor1Ups); // increment coin tally used for 1-up blocks
  GiveOneCoin(); return; // update coin amount and tally on the screen
  
HandleAxeMetatile:
  lda_imm(0x0);
  ram[OperMode_Task] = a; // reset secondary mode
  lda_imm(0x2);
  ram[OperMode] = a; // set primary mode to autoctrl mode
  lda_imm(0x18);
  ram[Player_X_Speed] = a; // set horizontal speed and continue to erase axe metatile
  ErACM(); // fallthrough
  return;
  
HandleClimbing:
  ldy_zp(0x4); // check low nybble of horizontal coordinate returned from
  cpy_imm_fczn(0x6); // collision detection routine against certain values, this
  if (!carry_flag) { return; } // makes actual physical part of vine or flagpole thinner
  cpy_imm_fczn(0xa); // than 16 pixels
  if (!carry_flag) { goto ChkForFlagpole; }
  // ExHC:
  return; // leave if too far left or too far right
  
ChkForFlagpole:
  cmp_imm_fz(0x24); // check climbing metatiles
  if (zero_flag) { goto FlagpoleCollision; } // branch if flagpole ball found
  cmp_imm_fz(0x25);
  if (!zero_flag) { goto VineCollision; } // branch to alternate code if flagpole shaft not found
  
FlagpoleCollision:
  lda_zp(GameEngineSubroutine);
  cmp_imm_fz(0x5); // check for end-of-level routine running
  if (zero_flag) { goto PutPlayerOnVine; } // if running, branch to end of climbing code
  lda_imm(0x1);
  ram[PlayerFacingDir] = a; // set player's facing direction to right
  inc_abs(ScrollLock); // set scroll lock flag
  lda_zp(GameEngineSubroutine);
  cmp_imm_fcz(0x4); // check for flagpole slide routine running
  if (zero_flag) { goto RunFR; } // if running, branch to end of flagpole code here
  lda_imm_fzn(BulletBill_CannonVar); // load identifier for bullet bills (cannon variant)
  cpu_call_begin(0xde58); KillEnemies(); cpu_call_end(); // get rid of them
  lda_imm(Silence);
  ram[EventMusicQueue] = a; // silence music
  lsr_acc();
  ram[FlagpoleSoundQueue] = a; // load flagpole sound into flagpole sound queue
  ldx_imm(0x4); // start at end of vertical coordinate data
  lda_zp(Player_Y_Position);
  ram[FlagpoleCollisionYPos] = a; // store player's vertical coordinate here to be used later
  
ChkFlagpoleYPosLoop:
  cmp_absx_fc(FlagpoleYPosData); // compare with current vertical coordinate data
  if (carry_flag) { goto MtchF; } // if player's => current, branch to use current offset
  dex_fz(); // otherwise decrement offset to use 
  if (!zero_flag) { goto ChkFlagpoleYPosLoop; } // do this until all data is checked (use last one if all checked)
  
MtchF:
  ram[FlagpoleScore] = x; // store offset here to be used later
  
RunFR:
  lda_imm(0x4);
  ram[GameEngineSubroutine] = a; // set value to run flagpole slide routine
  goto PutPlayerOnVine; // jump to end of climbing code
  
VineCollision:
  cmp_imm_fz(0x26); // check for climbing metatile used on vines
  if (!zero_flag) { goto PutPlayerOnVine; }
  lda_zp(Player_Y_Position); // check player's vertical coordinate
  cmp_imm_fc(0x20); // for being in status bar area
  if (carry_flag) { goto PutPlayerOnVine; } // branch if not that far up
  lda_imm(0x1);
  ram[GameEngineSubroutine] = a; // otherwise set to run autoclimb routine next frame
  
PutPlayerOnVine:
  lda_imm(0x3); // set player state to climbing
  ram[Player_State] = a;
  lda_imm(0x0); // nullify player's horizontal speed
  ram[Player_X_Speed] = a; // and fractional horizontal movement force
  ram[Player_X_MoveForce] = a;
  lda_zp(Player_X_Position); // get player's horizontal coordinate
  carry_flag = true;
  sbc_abs(ScreenLeft_X_Pos); // subtract from left side horizontal coordinate
  cmp_imm_fc(0x10);
  if (carry_flag) { goto SetVXPl; } // if 16 or more pixels difference, do not alter facing direction
  lda_imm(0x2);
  ram[PlayerFacingDir] = a; // otherwise force player to face left
  
SetVXPl:
  ldy_zp(PlayerFacingDir); // get current facing direction, use as offset
  lda_zp(0x6); // get low byte of block buffer address
  asl_acc();
  asl_acc(); // move low nybble to high
  asl_acc();
  asl_acc();
  carry_flag = false;
  adc_absy_fc(ClimbXPosAdder - 1); // add pixels depending on facing direction
  ram[Player_X_Position] = a; // store as player's horizontal coordinate
  lda_zp_fzn(0x6); // get low byte of block buffer address again
  if (!zero_flag) { return; } // if not zero, branch
  lda_abs(ScreenRight_PageLoc); // load page location of right side of screen
  carry_flag = false;
  adc_absy_fczn(ClimbPLocAdder - 1); // add depending on facing location
  ram[Player_PageLoc] = a; // store as player's page location
  // ExPVne:
  return; // finally, we're done!
  // --------------------------------
}

void ErACM(void) {
  ldy_zp(0x2); // load vertical high nybble offset for block buffer
  lda_imm(0x0); // load blank metatile
  dynamic_ram_write(read_word(0x6) + y, a); // store to remove old contents from block buffer
  RemoveCoin_Axe(); return; // update the screen accordingly
  // --------------------------------
  // $02 - high nybble of vertical coordinate from block buffer
  // $04 - low nybble of horizontal coordinate from block buffer
  // $06-$07 - block buffer address
}

void ChkInvisibleMTiles(void) {
  cmp_imm_fczn(0x5f); // check for hidden coin block
  if (!zero_flag) {
    cmp_imm_fczn(0x60); // check for hidden 1-up block
    // ExCInvT:
    return; // leave with zero flag set if either found
    // --------------------------------
    // $00-$01 - used to hold bottom right and bottom left metatiles (in that order)
    // $00 - used as flag by ImpedePlayerMove to restrict specific movement
  }
}

void ChkForLandJumpSpring(void) {
  cpu_call_begin(0xdec6); ChkJumpspringMetatiles(); cpu_call_end(); // do sub to check if player landed on jumpspring
  if (carry_flag) {
    lda_imm(0x70);
    ram[VerticalForce] = a; // otherwise set vertical movement force for player
    lda_imm(0xf9);
    ram[JumpspringForce] = a; // set default jumpspring force
    lda_imm(0x3);
    ram[JumpspringTimer] = a; // set jumpspring timer to be used later
    lsr_acc_fczn();
    ram[JumpspringAnimCtrl] = a; // set jumpspring animation control to start animating
    // ExCJSp:
    return; // and leave
  }
}

void ChkJumpspringMetatiles(void) {
  // ChkJumpspringMetatiles:
  cmp_imm_fzn(0x67); // check for top jumpspring metatile
  if (zero_flag) { goto JSFnd; } // branch to set carry if found
  cmp_imm_fzn(0x68); // check for bottom jumpspring metatile
  carry_flag = false; // clear carry flag
  if (!zero_flag) { return; } // branch to use cleared carry if not found
  
JSFnd:
  carry_flag = true; // set carry if found
  // NoJSFnd:
  return; // leave
}

void HandlePipeEntry(void) {
  // HandlePipeEntry:
  lda_zp(Up_Down_Buttons); // check saved controller bits from earlier
  and_imm_fzn(0b00000100); // for pressing down
  if (zero_flag) { return; } // if not pressing down, branch to leave
  lda_zp(0x0);
  cmp_imm_fczn(0x11); // check right foot metatile for warp pipe right metatile
  if (!zero_flag) { return; } // branch to leave if not found
  lda_zp(0x1);
  cmp_imm_fczn(0x10); // check left foot metatile for warp pipe left metatile
  if (!zero_flag) { return; } // branch to leave if not found
  lda_imm(0x30);
  ram[ChangeAreaTimer] = a; // set timer for change of area
  lda_imm(0x3);
  ram[GameEngineSubroutine] = a; // set to run vertical pipe entry routine on next frame
  lda_imm(Sfx_PipeDown_Injury);
  ram[Square1SoundQueue] = a; // load pipedown/injury sound
  lda_imm(0b00100000);
  ram[Player_SprAttrib] = a; // set background priority bit in player's attributes
  lda_abs_fzn(WarpZoneControl); // check warp zone control
  if (zero_flag) { return; } // branch to leave if none found
  and_imm(0b00000011); // mask out all but 2 LSB
  asl_acc();
  asl_acc(); // multiply by four
  tax(); // save as offset to warp zone numbers (starts at left pipe)
  lda_zp(Player_X_Position); // get player's horizontal position
  cmp_imm_fc(0x60);
  if (!carry_flag) { goto GetWNum; } // if player at left, not near middle, use offset and skip ahead
  inx(); // otherwise increment for middle pipe
  cmp_imm_fc(0xa0);
  if (!carry_flag) { goto GetWNum; } // if player at middle, but not too far right, use offset and skip
  inx(); // otherwise increment for last pipe
  
GetWNum:
  ldy_absx(WarpZoneNumbers); // get warp zone numbers
  dey(); // decrement for use as world number
  ram[WorldNumber] = y; // store as world number and offset
  ldx_absy(WorldAddrOffsets); // get offset to where this world's area offsets are
  lda_absx(AreaAddrOffsets); // get area offset based on world offset
  ram[AreaPointer] = a; // store area offset here to be used to change areas
  lda_imm(Silence);
  ram[EventMusicQueue] = a; // silence music
  lda_imm(0x0);
  ram[EntrancePage] = a; // initialize starting page number
  ram[AreaNumber] = a; // initialize area number used for area address offset
  ram[LevelNumber] = a; // initialize level number used for world display
  ram[AltEntranceControl] = a; // initialize mode of entry
  inc_abs(Hidden1UpFlag); // set flag for hidden 1-up blocks
  inc_abs_fzn(FetchNewGameTimerFlag); // set flag to load new game timer
  // ExPipeE:
  return; // leave!!!
}

void ImpedePlayerMove(void) {
  // ImpedePlayerMove:
  lda_imm(0x0); // initialize value here
  ldy_zp(Player_X_Speed); // get player's horizontal speed
  ldx_zp(0x0); // check value set earlier for
  dex_fz(); // left side collision
  if (!zero_flag) { goto RImpd; } // if right side collision, skip this part
  inx(); // return value to X
  cpy_imm_fcn(0x0); // if player moving to the left,
  if (neg_flag) { goto ExIPM; } // branch to invert bit and leave
  lda_imm(0xff); // otherwise load A with value to be used later
  goto NXSpd; // and jump to affect movement
  
RImpd:
  ldx_imm(0x2); // return $02 to X
  cpy_imm_fcn(0x1); // if player moving to the right,
  if (!neg_flag) { goto ExIPM; } // branch to invert bit and leave
  lda_imm(0x1); // otherwise load A with value to be used here
  
NXSpd:
  ldy_imm(0x10);
  ram[SideCollisionTimer] = y; // set timer of some sort
  ldy_imm(0x0);
  ram[Player_X_Speed] = y; // nullify player's horizontal speed
  cmp_imm_fn(0x0); // if value set in A not set to $ff,
  if (!neg_flag) { goto PlatF; } // branch ahead, do not decrement Y
  dey(); // otherwise decrement Y now
  
PlatF:
  ram[0x0] = y; // store Y as high bits of horizontal adder
  carry_flag = false;
  adc_zp_fc(Player_X_Position); // add contents of A to player's horizontal
  ram[Player_X_Position] = a; // position to move player left or right
  lda_zp(Player_PageLoc);
  adc_zp_fc(0x0); // add high bits and carry to
  ram[Player_PageLoc] = a; // page location if necessary
  
ExIPM:
  txa(); // invert contents of X
  eor_imm(0xff);
  and_abs_fzn(Player_CollisionBits); // mask out bit that was set here
  ram[Player_CollisionBits] = a; // store to clear bit
  return;
  // --------------------------------
}

void CheckForSolidMTiles(void) {
  cpu_call_begin(0xdf91); GetMTileAttrib(); cpu_call_end(); // find appropriate offset based on metatile's 2 MSB
  cmp_absx_fczn(SolidMTileUpperExt); // compare current metatile with solid metatiles
  return;
}

void CheckForClimbMTiles(void) {
  cpu_call_begin(0xdf9c); GetMTileAttrib(); cpu_call_end(); // find appropriate offset based on metatile's 2 MSB
  cmp_absx_fczn(ClimbMTileUpperExt); // compare current metatile with climbable metatiles
  return;
}

void CheckForCoinMTiles(void) {
  cmp_imm_fcz(0xc2); // check for regular coin
  // branch if found
  if (!zero_flag) {
    cmp_imm_fczn(0xc3); // check for underwater coin
    // branch if found
    if (!zero_flag) {
      carry_flag = false; // otherwise clear carry and leave
      return;
    }
  }
  // CoinSd:
  lda_imm_fzn(Sfx_CoinGrab);
  ram[Square2SoundQueue] = a; // load coin grab sound and leave
  return;
}

void GetMTileAttrib(void) {
  tay(); // save metatile value into Y
  and_imm(0b11000000); // mask out all but 2 MSB
  asl_acc_fc();
  rol_acc_fc(); // shift and rotate d7-d6 to d1-d0
  rol_acc_fc();
  tax(); // use as offset for metatile data
  tya_fzn(); // get original metatile value back
  // ExEBG:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $06-$07 - address from block buffer routine
}

void EnemyToBGCollisionDet(void) {
  // EnemyToBGCollisionDet:
  lda_zpx(Enemy_State); // check enemy state for d6 set
  and_imm_fzn(0b00100000);
  if (!zero_flag) { return; } // if set, branch to leave
  cpu_call_begin(0xdfc9); SubtEnemyYPos(); cpu_call_end(); // otherwise, do a subroutine here
  if (!carry_flag) { return; } // if enemy vertical coord + 62 < 68, branch to leave
  ldy_zpx(Enemy_ID);
  cpy_imm_fz(Spiny); // if enemy object is not spiny, branch elsewhere
  if (!zero_flag) { goto DoIDCheckBGColl; }
  lda_zpx(Enemy_Y_Position);
  cmp_imm_fczn(0x25); // if enemy vertical coordinate < 36 branch to leave
  if (!carry_flag) { return; }
  
DoIDCheckBGColl:
  cpy_imm_fczn(GreenParatroopaJump); // check for some other enemy object
  if (!zero_flag) { goto HBChk; } // branch if not found
  EnemyJump(); return; // otherwise jump elsewhere
  
HBChk:
  cpy_imm_fczn(HammerBro); // check for hammer bro
  if (!zero_flag) { goto CInvu; } // branch if not found
  goto HammerBroBGColl; // otherwise jump elsewhere
  
CInvu:
  cpy_imm_fczn(Spiny); // if enemy object is spiny, branch
  if (zero_flag) { goto YesIn; }
  cpy_imm_fczn(PowerUpObject); // if special power-up object, branch
  if (zero_flag) { goto YesIn; }
  cpy_imm_fczn(0x7); // if enemy object =>$07, branch to leave
  if (carry_flag) { return; }
  
YesIn:
  cpu_call_begin(0xdff4); ChkUnderEnemy(); cpu_call_end(); // if enemy object < $07, or = $12 or $2e, do this sub
  if (!zero_flag) { goto HandleEToBGCollision; } // if block underneath enemy, branch
  
NoEToBGCollision:
  goto ChkForRedKoopa; // otherwise skip and do something else
  // --------------------------------
  // $02 - vertical coordinate from block buffer routine
  
HandleEToBGCollision:
  cpu_call_begin(0xdffc); ChkForNonSolids(); cpu_call_end(); // if something is underneath enemy, find out what
  if (zero_flag) { goto NoEToBGCollision; } // if blank $26, coins, or hidden blocks, jump, enemy falls through
  cmp_imm_fz(0x23);
  if (!zero_flag) { goto LandEnemyProperly; } // check for blank metatile $23 and branch if not found
  ldy_zp(0x2); // get vertical coordinate used to find block
  lda_imm(0x0); // store default blank metatile in that spot so we won't
  dynamic_ram_write(read_word(0x6) + y, a); // trigger this routine accidentally again
  lda_zpx(Enemy_ID);
  cmp_imm_fc(0x15); // if enemy object => $15, branch ahead
  if (carry_flag) { ChkToStunEnemies(); return; }
  cmp_imm_fczn(Goomba); // if enemy object not goomba, branch ahead of this routine
  if (!zero_flag) { goto GiveOEPoints; }
  cpu_call_begin(0xe015); KillEnemyAboveBlock(); cpu_call_end(); // if enemy object IS goomba, do this sub
  
GiveOEPoints:
  lda_imm_fzn(0x1); // award 100 points for hitting block beneath enemy
  cpu_call_begin(0xe01a); SetupFloateyNumber(); cpu_call_end();
  ChkToStunEnemies(); // fallthrough
  return;
  
LandEnemyProperly:
  lda_zp(0x4); // check lower nybble of vertical coordinate saved earlier
  carry_flag = true;
  sbc_imm(0x8); // subtract eight pixels
  cmp_imm_fc(0x5); // used to determine whether enemy landed from falling
  if (carry_flag) { goto ChkForRedKoopa; } // branch if lower nybble in range of $0d-$0f before subtract
  lda_zpx(Enemy_State);
  and_imm_fzn(0b01000000); // branch if d6 in enemy state is set
  if (!zero_flag) { goto LandEnemyInitState; }
  lda_zpx(Enemy_State);
  asl_acc_fc(); // branch if d7 in enemy state is not set
  if (!carry_flag) { goto ChkLandedEnemyState; }
  
SChkA:
  goto DoEnemySideCheck; // if lower nybble < $0d, d7 set but d6 not set, jump here
  
ChkLandedEnemyState:
  lda_zpx_fz(Enemy_State); // if enemy in normal state, branch back to jump here
  if (zero_flag) { goto SChkA; }
  cmp_imm_fz(0x5); // if in state used by spiny's egg
  if (zero_flag) { goto ProcEnemyDirection; } // then branch elsewhere
  cmp_imm_fczn(0x3); // if already in state used by koopas and buzzy beetles
  if (carry_flag) { return; } // or in higher numbered state, branch to leave
  lda_zpx(Enemy_State); // load enemy state again (why?)
  cmp_imm_fz(0x2); // if not in $02 state (used by koopas and buzzy beetles)
  if (!zero_flag) { goto ProcEnemyDirection; } // then branch elsewhere
  lda_imm(0x10); // load default timer here
  ldy_zpx(Enemy_ID); // check enemy identifier for spiny
  cpy_imm_fcz(Spiny);
  if (!zero_flag) { goto SetForStn; } // branch if not found
  lda_imm(0x0); // set timer for $00 if spiny
  
SetForStn:
  ram[EnemyIntervalTimer + x] = a; // set timer here
  lda_imm_fzn(0x3); // set state here, apparently used to render
  ram[Enemy_State + x] = a; // upside-down koopas and buzzy beetles
  cpu_call_begin(0xe0a3); EnemyLanding(); cpu_call_end(); // then land it properly
  // ExSteChk:
  return; // then leave
  
ProcEnemyDirection:
  lda_zpx(Enemy_ID); // check enemy identifier for goomba
  cmp_imm_fczn(Goomba); // branch if found
  if (zero_flag) { goto LandEnemyInitState; }
  cmp_imm_fcz(Spiny); // check for spiny
  if (!zero_flag) { goto InvtD; } // branch if not found
  lda_imm(0x1);
  ram[Enemy_MovingDir + x] = a; // send enemy moving to the right by default
  lda_imm(0x8);
  ram[Enemy_X_Speed + x] = a; // set horizontal speed accordingly
  lda_zp(FrameCounter);
  and_imm_fzn(0b00000111); // if timed appropriately, spiny will skip over
  if (zero_flag) { goto LandEnemyInitState; } // trying to face the player
  
InvtD:
  ldy_imm_fzn(0x1); // load 1 for enemy to face the left (inverted here)
  cpu_call_begin(0xe0c1); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and enemy
  if (!neg_flag) { goto CNwCDir; } // if enemy to the right of player, branch
  iny(); // if to the left, increment by one for enemy to face right (inverted)
  
CNwCDir:
  tya();
  cmp_zpx_fczn(Enemy_MovingDir); // compare direction in A with current direction in memory
  if (!zero_flag) { goto LandEnemyInitState; }
  cpu_call_begin(0xe0cc); ChkForBump_HammerBroJ(); cpu_call_end(); // if equal, not facing in correct dir, do sub to turn around
  
LandEnemyInitState:
  cpu_call_begin(0xe0cf); EnemyLanding(); cpu_call_end(); // land enemy properly
  lda_zpx(Enemy_State);
  and_imm_fz(0b10000000); // if d7 of enemy state is set, branch
  if (!zero_flag) { goto NMovShellFallBit; }
  lda_imm_fzn(0x0); // otherwise initialize enemy state and leave
  ram[Enemy_State + x] = a; // note this will also turn spiny's egg into spiny
  return;
  
NMovShellFallBit:
  lda_zpx(Enemy_State); // nullify d6 of enemy state, save other bits
  and_imm_fzn(0b10111111); // and store, then leave
  ram[Enemy_State + x] = a;
  return;
  // --------------------------------
  
ChkForRedKoopa:
  lda_zpx(Enemy_ID); // check for red koopa troopa $03
  cmp_imm_fz(RedKoopa);
  if (!zero_flag) { goto Chk2MSBSt; } // branch if not found
  lda_zpx_fz(Enemy_State);
  if (zero_flag) { ChkForBump_HammerBroJ(); return; } // if enemy found and in normal state, branch
  
Chk2MSBSt:
  lda_zpx(Enemy_State); // save enemy state into Y
  tay();
  asl_acc_fc(); // check for d7 set
  if (!carry_flag) { goto GetSteFromD; } // branch if not set
  lda_zpx(Enemy_State);
  ora_imm(0b01000000); // set d6
  goto SetD6Ste; // jump ahead of this part
  
GetSteFromD:
  lda_absy(EnemyBGCStateData); // load new enemy state with old as offset
  
SetD6Ste:
  ram[Enemy_State + x] = a; // set as new state
  // --------------------------------
  // $00 - used to store bitmask (not used but initialized here)
  // $eb - used in DoEnemySideCheck as counter and to compare moving directions
  
DoEnemySideCheck:
  lda_zpx(Enemy_Y_Position); // if enemy within status bar, branch to leave
  cmp_imm_fczn(0x20); // because there's nothing there that impedes movement
  if (!carry_flag) { return; }
  ldy_imm(0x16); // start by finding block to the left of enemy ($00,$14)
  lda_imm(0x2); // set value here in what is also used as
  ram[0xeb] = a; // OAM data offset
  
SdeCLoop:
  lda_zp(0xeb); // check value
  cmp_zpx_fcz(Enemy_MovingDir); // compare value against moving direction
  if (!zero_flag) { goto NextSdeC; } // branch if different and do not seek block there
  lda_imm_fzn(0x1); // set flag in A for save horizontal coordinate 
  cpu_call_begin(0xe114); BlockBufferChk_Enemy(); cpu_call_end(); // find block to left or right of enemy object
  if (zero_flag) { goto NextSdeC; } // if nothing found, branch
  cpu_call_begin(0xe119); ChkForNonSolids(); cpu_call_end(); // check for non-solid blocks
  if (!zero_flag) { ChkForBump_HammerBroJ(); return; } // branch if not found
  
NextSdeC:
  dec_zp(0xeb); // move to the next direction
  iny();
  cpy_imm_fczn(0x18); // increment Y, loop only if Y < $18, thus we check
  if (!carry_flag) { goto SdeCLoop; } // enemy ($00, $14) and ($10, $14) pixel coordinates
  // ExESdeC:
  return;
  
HammerBroBGColl:
  cpu_call_begin(0xe187); ChkUnderEnemy(); cpu_call_end(); // check to see if hammer bro is standing on anything
  if (zero_flag) { goto NoUnderHammerBro; }
  cmp_imm_fczn(0x23); // check for blank metatile $23 and branch if not found
  if (!zero_flag) { goto UnderHammerBro; }
  KillEnemyAboveBlock(); // fallthrough
  return;
  
UnderHammerBro:
  lda_absx_fz(EnemyFrameTimer); // check timer used by hammer bro
  if (!zero_flag) { goto NoUnderHammerBro; } // branch if not expired
  lda_zpx(Enemy_State);
  and_imm_fzn(0b10001000); // save d7 and d3 from enemy state, nullify other bits
  ram[Enemy_State + x] = a; // and store
  cpu_call_begin(0xe1a3); EnemyLanding(); cpu_call_end(); // modify vertical coordinate, speed and something else
  goto DoEnemySideCheck; // then check for horizontal blockage and leave
  
NoUnderHammerBro:
  lda_zpx(Enemy_State); // if hammer bro is not standing on anything, set d0
  ora_imm_fzn(0x1); // in the enemy state to indicate jumping or falling, then leave
  ram[Enemy_State + x] = a;
  return;
}

void ChkToStunEnemies(void) {
  cmp_imm_fc(0x9); // perform many comparisons on enemy object identifier
  if (!carry_flag) {
    SetStun();
    return;
  }
  cmp_imm_fc(0x11); // if the enemy object identifier is equal to the values
  // $09, $0e, $0f or $10, it will be modified, and not
  if (carry_flag) {
    SetStun();
    return;
  }
  cmp_imm_fc(0xa); // modified if not any of those values, note that piranha plant will
  // always fail this test because A will still have vertical
  if (carry_flag) {
    cmp_imm_fc(PiranhaPlant); // coordinate from previous addition, also these comparisons
    // are only necessary if branching from $d7a1
    if (!carry_flag) {
      SetStun();
      return;
    }
  }
  // Demote:
  and_imm(0b00000001); // erase all but LSB, essentially turning enemy object
  ram[Enemy_ID + x] = a; // into green or red koopa troopa to demote them
  SetStun(); // fallthrough
  return;
}

void SetStun(void) {
  // SetStun:
  lda_zpx(Enemy_State); // load enemy state
  and_imm(0b11110000); // save high nybble
  ora_imm(0b00000010);
  ram[Enemy_State + x] = a; // set d1 of enemy state
  dec_zpx(Enemy_Y_Position);
  dec_zpx(Enemy_Y_Position); // subtract two pixels from enemy's vertical position
  lda_zpx(Enemy_ID);
  cmp_imm_fcz(Bloober); // check for bloober object
  if (zero_flag) { goto SetWYSpd; }
  lda_imm(0xfd); // set default vertical speed
  ldy_abs_fz(AreaType);
  if (!zero_flag) { goto SetNotW; } // if area type not water, set as speed, otherwise
  
SetWYSpd:
  lda_imm(0xff); // change the vertical speed
  
SetNotW:
  ram[Enemy_Y_Speed + x] = a; // set vertical speed now
  ldy_imm_fzn(0x1);
  cpu_call_begin(0xe050); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and enemy object
  if (!neg_flag) { goto ChkBBill; } // branch if enemy is to the right of player
  iny(); // increment Y if not
  
ChkBBill:
  lda_zpx(Enemy_ID);
  cmp_imm_fcz(BulletBill_CannonVar); // check for bullet bill (cannon variant)
  if (zero_flag) { goto NoCDirF; }
  cmp_imm_fcz(BulletBill_FrenzyVar); // check for bullet bill (frenzy variant)
  if (zero_flag) { goto NoCDirF; } // branch if either found, direction does not change
  ram[Enemy_MovingDir + x] = y; // store as moving direction
  
NoCDirF:
  dey(); // decrement and use as offset
  lda_absy_fzn(EnemyBGCXSpdData); // get proper horizontal speed
  ram[Enemy_X_Speed + x] = a; // and store, then leave
  // ExEBGChk:
  return;
  // --------------------------------
  // $04 - low nybble of vertical coordinate from block buffer routine
}

void ChkForBump_HammerBroJ(void) {
  goto ChkForBump_HammerBroJ;
  
SetHJ:
  ram[Enemy_Y_Speed + x] = y; // set vertical speed for jumping
  lda_zpx(Enemy_State); // set d0 in enemy state for jumping
  ora_imm(0x1);
  ram[Enemy_State + x] = a;
  lda_zp(0x0); // load preset value here to use as bitmask
  and_absx(PseudoRandomBitReg + 2); // and do bit-wise comparison with part of LSFR
  tay(); // then use as offset
  lda_abs_fz(SecondaryHardMode); // check secondary hard mode flag
  if (zero_flag) {
    tay(); // if secondary hard mode flag clear, set offset to 0
  }
  // HJump:
  lda_absy(HammerBroJumpLData); // get jump length timer data using offset from before
  ram[EnemyFrameTimer + x] = a; // save in enemy timer
  lda_absx(PseudoRandomBitReg + 1);
  ora_imm(0b11000000); // get contents of part of LSFR, set d7 and d6, then
  ram[HammerBroJumpTimer + x] = a; // store in jump timer
  // MoveHammerBroXDir:
  ldy_imm(0xfc); // move hammer bro a little to the left
  lda_zp(FrameCounter);
  and_imm_fz(0b01000000); // change hammer bro's direction every 64 frames
  if (zero_flag) {
    ldy_imm(0x4); // if d6 set in counter, move him a little to the right
  }
  // Shimmy:
  ram[Enemy_X_Speed + x] = y; // store horizontal speed
  ldy_imm_fzn(0x1); // set to face right by default
  cpu_call_begin(0xca68); PlayerEnemyDiff(); cpu_call_end(); // get horizontal difference between player and hammer bro
  // if enemy to the left of player, skip this part
  if (!neg_flag) {
    iny(); // set to face left
    lda_absx_fz(EnemyIntervalTimer); // check walking timer
    // if not yet expired, skip to set moving direction
    if (zero_flag) {
      lda_imm(0xf8);
      ram[Enemy_X_Speed + x] = a; // otherwise, make the hammer bro walk left towards player
    }
  }
  // SetShim:
  ram[Enemy_MovingDir + x] = y; // set moving direction
  MoveNormalEnemy(); // fallthrough
  return;
  
RXSpd:
  lda_zpx(Enemy_X_Speed); // load horizontal speed
  eor_imm(0xff); // get two's compliment for horizontal speed
  tay();
  iny();
  ram[Enemy_X_Speed + x] = y; // store as new horizontal speed
  lda_zpx(Enemy_MovingDir);
  eor_imm_fzn(0b00000011); // invert moving direction and store, then leave
  ram[Enemy_MovingDir + x] = a; // thus effectively turning the enemy around
  // ExTA:
  return; // leave!!!
  // -------------------------------------------------------------------------------------
  // $00 - vertical position of platform
  
ChkForBump_HammerBroJ:
  cpx_imm_fz(0x5); // check if we're on the special use slot
  // and if so, branch ahead and do not play sound
  if (!zero_flag) {
    lda_zpx(Enemy_State); // if enemy state d7 not set, branch
    asl_acc_fc(); // ahead and do not play sound
    if (carry_flag) {
      lda_imm(Sfx_Bump); // otherwise, play bump sound
      ram[Square1SoundQueue] = a; // sound will never be played if branching from ChkForRedKoopa
    }
  }
  // NoBump:
  lda_zpx(Enemy_ID); // check for hammer bro
  cmp_imm_fcz(0x5);
  // branch if not found
  if (zero_flag) {
    lda_imm(0x0);
    ram[0x0] = a; // initialize value here for bitmask  
    ldy_imm(0xfa); // load default vertical speed for jumping
    goto SetHJ; // jump to code that makes hammer bro jump
  }
  // InvEnemyDir:
  goto RXSpd; // jump to turn the enemy around
  // --------------------------------
  // $00 - used to hold horizontal difference between player and enemy
}

void PlayerEnemyDiff(void) {
  lda_zpx(Enemy_X_Position); // get distance between enemy object's
  carry_flag = true; // horizontal coordinate and the player's
  sbc_zp_fc(Player_X_Position); // horizontal coordinate
  ram[0x0] = a; // and store here
  lda_zpx(Enemy_PageLoc);
  sbc_zp_fczn(Player_PageLoc); // subtract borrow, then leave
  return;
  // --------------------------------
}

void EnemyLanding(void) {
  cpu_call_begin(0xe151); InitVStf(); cpu_call_end(); // do something here to vertical speed and something else
  lda_zpx(Enemy_Y_Position);
  and_imm(0b11110000); // save high nybble of vertical coordinate, and
  ora_imm_fzn(0b00001000); // set d3, then store, probably used to set enemy object
  ram[Enemy_Y_Position + x] = a; // neatly on whatever it's landing on
  return;
}

void SubtEnemyYPos(void) {
  lda_zpx(Enemy_Y_Position); // add 62 pixels to enemy object's
  carry_flag = false; // vertical coordinate
  adc_imm(0x3e);
  cmp_imm_fczn(0x44); // compare against a certain range
  return; // and leave with flags set for conditional branch
}

void EnemyJump(void) {
  goto EnemyJump;
  
DoEnemySideCheck:
  lda_zpx(Enemy_Y_Position); // if enemy within status bar, branch to leave
  cmp_imm_fczn(0x20); // because there's nothing there that impedes movement
  if (carry_flag) {
    ldy_imm(0x16); // start by finding block to the left of enemy ($00,$14)
    lda_imm(0x2); // set value here in what is also used as
    ram[0xeb] = a; // OAM data offset
    
SdeCLoop:
    lda_zp(0xeb); // check value
    cmp_zpx_fcz(Enemy_MovingDir); // compare value against moving direction
    // branch if different and do not seek block there
    if (zero_flag) {
      lda_imm_fzn(0x1); // set flag in A for save horizontal coordinate 
      cpu_call_begin(0xe114); BlockBufferChk_Enemy(); cpu_call_end(); // find block to left or right of enemy object
      // if nothing found, branch
      if (!zero_flag) {
        cpu_call_begin(0xe119); ChkForNonSolids(); cpu_call_end(); // check for non-solid blocks
        // branch if not found
        if (!zero_flag) {
          ChkForBump_HammerBroJ();
          return;
        }
      }
    }
    // NextSdeC:
    dec_zp(0xeb); // move to the next direction
    iny();
    cpy_imm_fczn(0x18); // increment Y, loop only if Y < $18, thus we check
    if (!carry_flag) { goto SdeCLoop; } // enemy ($00, $14) and ($10, $14) pixel coordinates
    // ExESdeC:
    return;
    
EnemyJump:
    cpu_call_begin(0xe165); SubtEnemyYPos(); cpu_call_end(); // do a sub here
    // if enemy vertical coord + 62 < 68, branch to leave
    if (carry_flag) {
      lda_zpx(Enemy_Y_Speed);
      carry_flag = false; // add two to vertical speed
      adc_imm(0x2);
      cmp_imm_fczn(0x3); // if green paratroopa not falling, branch ahead
      if (carry_flag) {
        cpu_call_begin(0xe173); ChkUnderEnemy(); cpu_call_end(); // otherwise, check to see if green paratroopa is 
        // standing on anything, then branch to same place if not
        if (!zero_flag) {
          cpu_call_begin(0xe178); ChkForNonSolids(); cpu_call_end(); // check for non-solid blocks
          // branch if found
          if (!zero_flag) {
            cpu_call_begin(0xe17d); EnemyLanding(); cpu_call_end(); // change vertical coordinate and speed
            lda_imm(0xfd);
            ram[Enemy_Y_Speed + x] = a; // make the paratroopa jump again
          }
        }
      }
    }
    // DoSide:
    goto DoEnemySideCheck; // check for horizontal blockage, then leave
    // --------------------------------
  }
}

void KillEnemyAboveBlock(void) {
  cpu_call_begin(0xe190); ShellOrBlockDefeat(); cpu_call_end(); // do this sub to kill enemy
  lda_imm_fzn(0xfc); // alter vertical speed of enemy and leave
  ram[Enemy_Y_Speed + x] = a;
  return;
}

void ChkUnderEnemy(void) {
  lda_imm(0x0); // set flag in A for save vertical coordinate
  ldy_imm(0x15); // set Y to check the bottom middle (8,18) of enemy object
  BlockBufferChk_Enemy(); return; // hop to it!
}

void ChkForNonSolids(void) {
  // ChkForNonSolids:
  cmp_imm_fczn(0x26); // blank metatile used for vines?
  if (zero_flag) { return; }
  cmp_imm_fczn(0xc2); // regular coin?
  if (zero_flag) { return; }
  cmp_imm_fczn(0xc3); // underwater coin?
  if (zero_flag) { return; }
  cmp_imm_fczn(0x5f); // hidden coin block?
  if (zero_flag) { return; }
  cmp_imm_fczn(0x60); // hidden 1-up block?
  // NSFnd:
  return;
  // -------------------------------------------------------------------------------------
}

void FireballBGCollision(void) {
  // FireballBGCollision:
  lda_zpx(Fireball_Y_Position); // check fireball's vertical coordinate
  cmp_imm_fczn(0x18);
  if (!carry_flag) { goto ClearBounceFlag; } // if within the status bar area of the screen, branch ahead
  cpu_call_begin(0xe1d0); BlockBufferChk_FBall(); cpu_call_end(); // do fireball to background collision detection on bottom of it
  if (zero_flag) { goto ClearBounceFlag; } // if nothing underneath fireball, branch
  cpu_call_begin(0xe1d5); ChkForNonSolids(); cpu_call_end(); // check for non-solid metatiles
  if (zero_flag) { goto ClearBounceFlag; } // branch if any found
  lda_zpx_fn(Fireball_Y_Speed); // if fireball's vertical speed set to move upwards,
  if (neg_flag) { goto InitFireballExplode; } // branch to set exploding bit in fireball's state
  lda_zpx_fz(FireballBouncingFlag); // if bouncing flag already set,
  if (!zero_flag) { goto InitFireballExplode; } // branch to set exploding bit in fireball's state
  lda_imm(0xfd);
  ram[Fireball_Y_Speed + x] = a; // otherwise set vertical speed to move upwards (give it bounce)
  lda_imm(0x1);
  ram[FireballBouncingFlag + x] = a; // set bouncing flag
  lda_zpx(Fireball_Y_Position);
  and_imm_fzn(0xf8); // modify vertical coordinate to land it properly
  ram[Fireball_Y_Position + x] = a; // store as new vertical coordinate
  return; // leave
  
ClearBounceFlag:
  lda_imm_fzn(0x0);
  ram[FireballBouncingFlag + x] = a; // clear bouncing flag by default
  return; // leave
  
InitFireballExplode:
  lda_imm(0x80);
  ram[Fireball_State + x] = a; // set exploding flag in fireball's state
  lda_imm_fzn(Sfx_Bump);
  ram[Square1SoundQueue] = a; // load bump sound
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00 - used to hold one of bitmasks, or offset
  // $01 - used for relative X coordinate, also used to store middle screen page location
  // $02 - used for relative Y coordinate, also used to store middle screen coordinate
  // this data added to relative coordinates of sprite objects
  // stored in order: left edge, top edge, right edge, bottom edge
}

void GetFireballBoundBox(void) {
  txa(); // add seven bytes to offset
  carry_flag = false; // to use in routines as offset for fireball
  adc_imm_fc(0x7);
  tax();
  ldy_imm_fzn(0x2); // set offset for relative coordinates
  goto FBallB; // unconditional branch
  
FBallB:
  cpu_call_begin(0xe23f); BoundingBoxCore(); cpu_call_end(); // get bounding box coordinates
  goto CheckRightScreenBBox; // jump to handle any offscreen coordinates
  
CheckRightScreenBBox:
  lda_abs(ScreenLeft_X_Pos); // add 128 pixels to left side of screen
  carry_flag = false; // and store as horizontal coordinate of middle
  adc_imm_fc(0x80);
  ram[0x2] = a;
  lda_abs(ScreenLeft_PageLoc); // add carry to page location of left side of screen
  adc_imm(0x0); // and store as page location of middle
  ram[0x1] = a;
  lda_zpx(SprObject_X_Position); // get horizontal coordinate
  cmp_zp_fc(0x2); // compare against middle horizontal coordinate
  lda_zpx(SprObject_PageLoc); // get page location
  sbc_zp_fc(0x1); // subtract from middle page location
  // if object is on the left side of the screen, branch
  if (carry_flag) {
    lda_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    // coordinates, branch if still on the screen
    if (!neg_flag) {
      lda_imm(0xff); // load offscreen value here to use on one or both horizontal sides
      ldx_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
      // coordinates, and branch if still on the screen
      if (!neg_flag) {
        ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
      }
      // SORte:
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // NoOfs:
    ldx_zp_fzn(ObjectOffset); // get object offset and leave
    return;
  }
  // CheckLeftScreenBBox:
  lda_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
  // coordinates, and branch if still on the screen
  if (neg_flag) {
    cmp_imm_fc(0xa0); // check to see if left-side edge is in the middle of the
    // screen or really offscreen, and branch if still on
    if (carry_flag) {
      lda_imm(0x0);
      ldx_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
      // coordinates, branch if still onscreen
      if (neg_flag) {
        ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
      }
      // SOLft:
      ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
    }
  }
  // NoOfs2:
  ldx_zp_fzn(ObjectOffset); // get object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $06 - second object's offset
  // $07 - counter
}

void GetMiscBoundBox(void) {
  txa(); // add nine bytes to offset
  carry_flag = false; // to use in routines as offset for misc object
  adc_imm_fc(0x9);
  tax();
  ldy_imm_fzn(0x6); // set offset for relative coordinates
  // FBallB:
  cpu_call_begin(0xe23f); BoundingBoxCore(); cpu_call_end(); // get bounding box coordinates
  goto CheckRightScreenBBox; // jump to handle any offscreen coordinates
  
CheckRightScreenBBox:
  lda_abs(ScreenLeft_X_Pos); // add 128 pixels to left side of screen
  carry_flag = false; // and store as horizontal coordinate of middle
  adc_imm_fc(0x80);
  ram[0x2] = a;
  lda_abs(ScreenLeft_PageLoc); // add carry to page location of left side of screen
  adc_imm(0x0); // and store as page location of middle
  ram[0x1] = a;
  lda_zpx(SprObject_X_Position); // get horizontal coordinate
  cmp_zp_fc(0x2); // compare against middle horizontal coordinate
  lda_zpx(SprObject_PageLoc); // get page location
  sbc_zp_fc(0x1); // subtract from middle page location
  // if object is on the left side of the screen, branch
  if (carry_flag) {
    lda_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    // coordinates, branch if still on the screen
    if (!neg_flag) {
      lda_imm(0xff); // load offscreen value here to use on one or both horizontal sides
      ldx_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
      // coordinates, and branch if still on the screen
      if (!neg_flag) {
        ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
      }
      // SORte:
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // NoOfs:
    ldx_zp_fzn(ObjectOffset); // get object offset and leave
    return;
  }
  // CheckLeftScreenBBox:
  lda_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
  // coordinates, and branch if still on the screen
  if (neg_flag) {
    cmp_imm_fc(0xa0); // check to see if left-side edge is in the middle of the
    // screen or really offscreen, and branch if still on
    if (carry_flag) {
      lda_imm(0x0);
      ldx_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
      // coordinates, branch if still onscreen
      if (neg_flag) {
        ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
      }
      // SOLft:
      ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
    }
  }
  // NoOfs2:
  ldx_zp_fzn(ObjectOffset); // get object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $06 - second object's offset
  // $07 - counter
}

void GetEnemyBoundBox(void) {
  ldy_imm(0x48); // store bitmask here for now
  ram[0x0] = y;
  ldy_imm(0x44); // store another bitmask here for now and jump
  goto GetMaskedOffScrBits;
  
GetMaskedOffScrBits:
  lda_zpx(Enemy_X_Position); // get enemy object position relative
  carry_flag = true; // to the left side of the screen
  sbc_abs_fc(ScreenLeft_X_Pos);
  ram[0x1] = a; // store here
  lda_zpx(Enemy_PageLoc); // subtract borrow from current page location
  sbc_abs_fn(ScreenLeft_PageLoc); // of left side
  // if enemy object is beyond left edge, branch
  if (!neg_flag) {
    ora_zp_fz(0x1);
    // if precisely at the left edge, branch
    if (!zero_flag) {
      ldy_zp(0x0); // if to the right of left edge, use value in $00 for A
    }
  }
  // CMBits:
  tya(); // otherwise use contents of Y
  and_abs_fz(Enemy_OffscreenBits); // preserve bitwise whatever's in here
  ram[EnemyOffscrBitsMasked + x] = a; // save masked offscreen bits here
  // if anything set here, branch
  if (zero_flag) {
    goto SetupEOffsetFBBox; // otherwise, do something else
    
SetupEOffsetFBBox:
    txa(); // add 1 to offset to properly address
    carry_flag = false; // the enemy object memory locations
    adc_imm_fc(0x1);
    tax();
    ldy_imm_fzn(0x1); // load 1 as offset here, same reason
    cpu_call_begin(0xe285); BoundingBoxCore(); cpu_call_end(); // do a sub to get the coordinates of the bounding box
    goto CheckRightScreenBBox; // jump to handle offscreen coordinates of bounding box
  }
  // MoveBoundBoxOffscreen:
  txa(); // multiply offset by 4
  asl_acc();
  asl_acc_fc();
  tay(); // use as offset here
  lda_imm_fzn(0xff);
  ram[EnemyBoundingBoxCoord + y] = a; // load value into four locations here and leave
  ram[EnemyBoundingBoxCoord + 1 + y] = a;
  ram[EnemyBoundingBoxCoord + 2 + y] = a;
  ram[EnemyBoundingBoxCoord + 3 + y] = a;
  return;
  
CheckRightScreenBBox:
  lda_abs(ScreenLeft_X_Pos); // add 128 pixels to left side of screen
  carry_flag = false; // and store as horizontal coordinate of middle
  adc_imm_fc(0x80);
  ram[0x2] = a;
  lda_abs(ScreenLeft_PageLoc); // add carry to page location of left side of screen
  adc_imm(0x0); // and store as page location of middle
  ram[0x1] = a;
  lda_zpx(SprObject_X_Position); // get horizontal coordinate
  cmp_zp_fc(0x2); // compare against middle horizontal coordinate
  lda_zpx(SprObject_PageLoc); // get page location
  sbc_zp_fc(0x1); // subtract from middle page location
  // if object is on the left side of the screen, branch
  if (carry_flag) {
    lda_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    // coordinates, branch if still on the screen
    if (!neg_flag) {
      lda_imm(0xff); // load offscreen value here to use on one or both horizontal sides
      ldx_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
      // coordinates, and branch if still on the screen
      if (!neg_flag) {
        ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
      }
      // SORte:
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // NoOfs:
    ldx_zp_fzn(ObjectOffset); // get object offset and leave
    return;
  }
  // CheckLeftScreenBBox:
  lda_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
  // coordinates, and branch if still on the screen
  if (neg_flag) {
    cmp_imm_fc(0xa0); // check to see if left-side edge is in the middle of the
    // screen or really offscreen, and branch if still on
    if (carry_flag) {
      lda_imm(0x0);
      ldx_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
      // coordinates, branch if still onscreen
      if (neg_flag) {
        ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
      }
      // SOLft:
      ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
    }
  }
  // NoOfs2:
  ldx_zp_fzn(ObjectOffset); // get object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $06 - second object's offset
  // $07 - counter
}

void SmallPlatformBoundBox(void) {
  ldy_imm(0x8); // store bitmask here for now
  ram[0x0] = y;
  ldy_imm(0x4); // store another bitmask here for now
  // GetMaskedOffScrBits:
  lda_zpx(Enemy_X_Position); // get enemy object position relative
  carry_flag = true; // to the left side of the screen
  sbc_abs_fc(ScreenLeft_X_Pos);
  ram[0x1] = a; // store here
  lda_zpx(Enemy_PageLoc); // subtract borrow from current page location
  sbc_abs_fn(ScreenLeft_PageLoc); // of left side
  // if enemy object is beyond left edge, branch
  if (!neg_flag) {
    ora_zp_fz(0x1);
    // if precisely at the left edge, branch
    if (!zero_flag) {
      ldy_zp(0x0); // if to the right of left edge, use value in $00 for A
    }
  }
  // CMBits:
  tya(); // otherwise use contents of Y
  and_abs_fz(Enemy_OffscreenBits); // preserve bitwise whatever's in here
  ram[EnemyOffscrBitsMasked + x] = a; // save masked offscreen bits here
  // if anything set here, branch
  if (zero_flag) {
    goto SetupEOffsetFBBox; // otherwise, do something else
    
SetupEOffsetFBBox:
    txa(); // add 1 to offset to properly address
    carry_flag = false; // the enemy object memory locations
    adc_imm_fc(0x1);
    tax();
    ldy_imm_fzn(0x1); // load 1 as offset here, same reason
    cpu_call_begin(0xe285); BoundingBoxCore(); cpu_call_end(); // do a sub to get the coordinates of the bounding box
    goto CheckRightScreenBBox; // jump to handle offscreen coordinates of bounding box
  }
  // MoveBoundBoxOffscreen:
  txa(); // multiply offset by 4
  asl_acc();
  asl_acc_fc();
  tay(); // use as offset here
  lda_imm_fzn(0xff);
  ram[EnemyBoundingBoxCoord + y] = a; // load value into four locations here and leave
  ram[EnemyBoundingBoxCoord + 1 + y] = a;
  ram[EnemyBoundingBoxCoord + 2 + y] = a;
  ram[EnemyBoundingBoxCoord + 3 + y] = a;
  return;
  
CheckRightScreenBBox:
  lda_abs(ScreenLeft_X_Pos); // add 128 pixels to left side of screen
  carry_flag = false; // and store as horizontal coordinate of middle
  adc_imm_fc(0x80);
  ram[0x2] = a;
  lda_abs(ScreenLeft_PageLoc); // add carry to page location of left side of screen
  adc_imm(0x0); // and store as page location of middle
  ram[0x1] = a;
  lda_zpx(SprObject_X_Position); // get horizontal coordinate
  cmp_zp_fc(0x2); // compare against middle horizontal coordinate
  lda_zpx(SprObject_PageLoc); // get page location
  sbc_zp_fc(0x1); // subtract from middle page location
  // if object is on the left side of the screen, branch
  if (carry_flag) {
    lda_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    // coordinates, branch if still on the screen
    if (!neg_flag) {
      lda_imm(0xff); // load offscreen value here to use on one or both horizontal sides
      ldx_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
      // coordinates, and branch if still on the screen
      if (!neg_flag) {
        ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
      }
      // SORte:
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // NoOfs:
    ldx_zp_fzn(ObjectOffset); // get object offset and leave
    return;
  }
  // CheckLeftScreenBBox:
  lda_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
  // coordinates, and branch if still on the screen
  if (neg_flag) {
    cmp_imm_fc(0xa0); // check to see if left-side edge is in the middle of the
    // screen or really offscreen, and branch if still on
    if (carry_flag) {
      lda_imm(0x0);
      ldx_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
      // coordinates, branch if still onscreen
      if (neg_flag) {
        ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
      }
      // SOLft:
      ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
    }
  }
  // NoOfs2:
  ldx_zp_fzn(ObjectOffset); // get object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $06 - second object's offset
  // $07 - counter
}

void LargePlatformBoundBox(void) {
  inx_fzn(); // increment X to get the proper offset
  cpu_call_begin(0xe276); GetXOffscreenBits(); cpu_call_end(); // then jump directly to the sub for horizontal offscreen bits
  dex(); // decrement to return to original offset
  cmp_imm_fc(0xfe); // if completely offscreen, branch to put entire bounding
  // box offscreen, otherwise start getting coordinates
  if (!carry_flag) {
    // SetupEOffsetFBBox:
    txa(); // add 1 to offset to properly address
    carry_flag = false; // the enemy object memory locations
    adc_imm_fc(0x1);
    tax();
    ldy_imm_fzn(0x1); // load 1 as offset here, same reason
    cpu_call_begin(0xe285); BoundingBoxCore(); cpu_call_end(); // do a sub to get the coordinates of the bounding box
    goto CheckRightScreenBBox; // jump to handle offscreen coordinates of bounding box
  }
  // MoveBoundBoxOffscreen:
  txa(); // multiply offset by 4
  asl_acc();
  asl_acc_fc();
  tay(); // use as offset here
  lda_imm_fzn(0xff);
  ram[EnemyBoundingBoxCoord + y] = a; // load value into four locations here and leave
  ram[EnemyBoundingBoxCoord + 1 + y] = a;
  ram[EnemyBoundingBoxCoord + 2 + y] = a;
  ram[EnemyBoundingBoxCoord + 3 + y] = a;
  return;
  
CheckRightScreenBBox:
  lda_abs(ScreenLeft_X_Pos); // add 128 pixels to left side of screen
  carry_flag = false; // and store as horizontal coordinate of middle
  adc_imm_fc(0x80);
  ram[0x2] = a;
  lda_abs(ScreenLeft_PageLoc); // add carry to page location of left side of screen
  adc_imm(0x0); // and store as page location of middle
  ram[0x1] = a;
  lda_zpx(SprObject_X_Position); // get horizontal coordinate
  cmp_zp_fc(0x2); // compare against middle horizontal coordinate
  lda_zpx(SprObject_PageLoc); // get page location
  sbc_zp_fc(0x1); // subtract from middle page location
  // if object is on the left side of the screen, branch
  if (carry_flag) {
    lda_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    // coordinates, branch if still on the screen
    if (!neg_flag) {
      lda_imm(0xff); // load offscreen value here to use on one or both horizontal sides
      ldx_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
      // coordinates, and branch if still on the screen
      if (!neg_flag) {
        ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
      }
      // SORte:
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // NoOfs:
    ldx_zp_fzn(ObjectOffset); // get object offset and leave
    return;
  }
  // CheckLeftScreenBBox:
  lda_absy_fn(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
  // coordinates, and branch if still on the screen
  if (neg_flag) {
    cmp_imm_fc(0xa0); // check to see if left-side edge is in the middle of the
    // screen or really offscreen, and branch if still on
    if (carry_flag) {
      lda_imm(0x0);
      ldx_absy_fn(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
      // coordinates, branch if still onscreen
      if (neg_flag) {
        ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
      }
      // SOLft:
      ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
    }
  }
  // NoOfs2:
  ldx_zp_fzn(ObjectOffset); // get object offset and leave
  return;
  // -------------------------------------------------------------------------------------
  // $06 - second object's offset
  // $07 - counter
}

void BoundingBoxCore(void) {
  ram[0x0] = x; // save offset here
  lda_absy(SprObject_Rel_YPos); // store object coordinates relative to screen
  ram[0x2] = a; // vertically and horizontally, respectively
  lda_absy(SprObject_Rel_XPos);
  ram[0x1] = a;
  txa(); // multiply offset by four and save to stack
  asl_acc();
  asl_acc();
  pha();
  tay(); // use as offset for Y, X is left alone
  lda_absx(SprObj_BoundBoxCtrl); // load value here to be used as offset for X
  asl_acc(); // multiply that by four and use as X
  asl_acc();
  tax();
  lda_zp(0x1); // add the first number in the bounding box data to the
  carry_flag = false; // relative horizontal coordinate using enemy object offset
  adc_absx(BoundBoxCtrlData); // and store somewhere using same offset * 4
  ram[BoundingBox_UL_Corner + y] = a; // store here
  lda_zp(0x1);
  carry_flag = false;
  adc_absx(BoundBoxCtrlData + 2); // add the third number in the bounding box data to the
  ram[BoundingBox_LR_Corner + y] = a; // relative horizontal coordinate and store
  inx(); // increment both offsets
  iny();
  lda_zp(0x2); // add the second number to the relative vertical coordinate
  carry_flag = false; // using incremented offset and store using the other
  adc_absx(BoundBoxCtrlData); // incremented offset
  ram[BoundingBox_UL_Corner + y] = a;
  lda_zp(0x2);
  carry_flag = false;
  adc_absx_fc(BoundBoxCtrlData + 2); // add the fourth number to the relative vertical coordinate
  ram[BoundingBox_LR_Corner + y] = a; // and store
  pla(); // get original offset loaded into $00 * y from stack
  tay(); // use as Y
  ldx_zp_fzn(0x0); // get original offset and use as X again
  return;
}

void PlayerCollisionCore(void) {
  ldx_imm(0x0); // initialize X to use player's bounding box for comparison
  SprObjectCollisionCore(); // fallthrough
  return;
}

void SprObjectCollisionCore(void) {
  // SprObjectCollisionCore:
  ram[0x6] = y; // save contents of Y here
  lda_imm(0x1);
  ram[0x7] = a; // save value 1 here as counter, compare horizontal coordinates first
  
CollisionCoreLoop:
  lda_absy(BoundingBox_UL_Corner); // compare left/top coordinates
  cmp_absx_fc(BoundingBox_UL_Corner); // of first and second objects' bounding boxes
  if (carry_flag) { goto FirstBoxGreater; } // if first left/top => second, branch
  cmp_absx_fcz(BoundingBox_LR_Corner); // otherwise compare to right/bottom of second
  if (!carry_flag) { goto SecondBoxVerticalChk; } // if first left/top < second right/bottom, branch elsewhere
  if (zero_flag) { goto CollisionFound; } // if somehow equal, collision, thus branch
  lda_absy(BoundingBox_LR_Corner); // if somehow greater, check to see if bottom of
  cmp_absy_fc(BoundingBox_UL_Corner); // first object's bounding box is greater than its top
  if (!carry_flag) { goto CollisionFound; } // if somehow less, vertical wrap collision, thus branch
  cmp_absx_fc(BoundingBox_UL_Corner); // otherwise compare bottom of first bounding box to the top
  if (carry_flag) { goto CollisionFound; } // of second box, and if equal or greater, collision, thus branch
  ldy_zp_fzn(0x6); // otherwise return with carry clear and Y = $0006
  return; // note horizontal wrapping never occurs
  
SecondBoxVerticalChk:
  lda_absx(BoundingBox_LR_Corner); // check to see if the vertical bottom of the box
  cmp_absx_fc(BoundingBox_UL_Corner); // is greater than the vertical top
  if (!carry_flag) { goto CollisionFound; } // if somehow less, vertical wrap collision, thus branch
  lda_absy(BoundingBox_LR_Corner); // otherwise compare horizontal right or vertical bottom
  cmp_absx_fc(BoundingBox_UL_Corner); // of first box with horizontal left or vertical top of second box
  if (carry_flag) { goto CollisionFound; } // if equal or greater, collision, thus branch
  ldy_zp_fzn(0x6); // otherwise return with carry clear and Y = $0006
  return;
  
FirstBoxGreater:
  cmp_absx_fz(BoundingBox_UL_Corner); // compare first and second box horizontal left/vertical top again
  if (zero_flag) { goto CollisionFound; } // if first coordinate = second, collision, thus branch
  cmp_absx_fcz(BoundingBox_LR_Corner); // if not, compare with second object right or bottom edge
  if (!carry_flag) { goto CollisionFound; } // if left/top of first less than or equal to right/bottom of second
  if (zero_flag) { goto CollisionFound; } // then collision, thus branch
  cmp_absy_fcz(BoundingBox_LR_Corner); // otherwise check to see if top of first box is greater than bottom
  if (!carry_flag) { goto NoCollisionFound; } // if less than or equal, no collision, branch to end
  if (zero_flag) { goto NoCollisionFound; }
  lda_absy(BoundingBox_LR_Corner); // otherwise compare bottom of first to top of second
  cmp_absx_fc(BoundingBox_UL_Corner); // if bottom of first is greater than top of second, vertical wrap
  if (carry_flag) { goto CollisionFound; } // collision, and branch, otherwise, proceed onwards here
  
NoCollisionFound:
  carry_flag = false; // clear carry, then load value set earlier, then leave
  ldy_zp_fzn(0x6); // like previous ones, if horizontal coordinates do not collide, we do
  return; // not bother checking vertical ones, because what's the point?
  
CollisionFound:
  inx(); // increment offsets on both objects to check
  iny(); // the vertical coordinates
  dec_zp_fn(0x7); // decrement counter to reflect this
  if (!neg_flag) { goto CollisionCoreLoop; } // if counter not expired, branch to loop
  carry_flag = true; // otherwise we already did both sets, therefore collision, so set carry
  ldy_zp_fzn(0x6); // load original value set here earlier, then leave
  return;
  // -------------------------------------------------------------------------------------
  // $02 - modified y coordinate
  // $03 - stores metatile involved in block buffer collisions
  // $04 - comes in with offset to block buffer adder data, goes out with low nybble x/y coordinate
  // $05 - modified x coordinate
  // $06-$07 - block buffer address
}

void BlockBufferChk_Enemy(void) {
  pha(); // save contents of A to stack
  txa();
  carry_flag = false; // add 1 to X to run sub with enemy offset in mind
  adc_imm_fc(0x1);
  tax();
  pla_fzn(); // pull A from stack and jump elsewhere
  goto BBChk_E;
  
BBChk_E:
  cpu_call_begin(0xe3a7); BlockBufferCollision(); cpu_call_end(); // do collision detection subroutine for sprite object
  ldx_zp(ObjectOffset); // get object offset
  cmp_imm_fczn(0x0); // check to see if object bumped into anything
  return;
}

void BlockBufferChk_FBall(void) {
  ldy_imm(0x1a); // set offset for block buffer adder data
  txa();
  carry_flag = false;
  adc_imm_fc(0x7); // add seven bytes to use
  tax();
  // ResJmpM:
  lda_imm_fzn(0x0); // set A to return vertical coordinate
  // BBChk_E:
  cpu_call_begin(0xe3a7); BlockBufferCollision(); cpu_call_end(); // do collision detection subroutine for sprite object
  ldx_zp(ObjectOffset); // get object offset
  cmp_imm_fczn(0x0); // check to see if object bumped into anything
  return;
}

void BlockBufferColli_Feet(void) {
  iny(); // if branched here, increment to next set of adders
  BlockBufferColli_Head(); // fallthrough
  return;
}

void BlockBufferColli_Head(void) {
  lda_imm(0x0); // set flag to return vertical coordinate
  // loc_58347:
  bit_abs(0x1a9);
  goto loc_58350; // BIT instruction opcode
  
loc_58350:
  ldx_imm(0x0); // set offset for player object
  BlockBufferCollision(); // fallthrough
  return;
}

void BlockBufferColli_Side(void) {
  lda_imm(0x1); // set flag to return horizontal coordinate
  // loc_58350:
  ldx_imm(0x0); // set offset for player object
  BlockBufferCollision(); // fallthrough
  return;
}

void BlockBufferCollision(void) {
  pha(); // save contents of A to stack
  ram[0x4] = y; // save contents of Y here
  lda_absy(BlockBuffer_X_Adder); // add horizontal coordinate
  carry_flag = false; // of object to value obtained using Y as offset
  adc_zpx_fc(SprObject_X_Position);
  ram[0x5] = a; // store here
  lda_zpx(SprObject_PageLoc);
  adc_imm(0x0); // add carry to page location
  and_imm(0x1); // get LSB, mask out all other bits
  lsr_acc_fc(); // move to carry
  ora_zp(0x5); // get stored value
  ror_acc(); // rotate carry to MSB of A
  lsr_acc(); // and effectively move high nybble to
  lsr_acc(); // lower, LSB which became MSB will be
  lsr_acc_fczn(); // d4 at this point
  cpu_call_begin(0xe40a); GetBlockBufferAddr(); cpu_call_end(); // get address of block buffer into $06, $07
  ldy_zp(0x4); // get old contents of Y
  lda_zpx(SprObject_Y_Position); // get vertical coordinate of object
  carry_flag = false;
  adc_absy(BlockBuffer_Y_Adder); // add it to value obtained using Y as offset
  and_imm(0b11110000); // mask out low nybble
  carry_flag = true;
  sbc_imm_fc(0x20); // subtract 32 pixels for the status bar
  ram[0x2] = a; // store result here
  tay(); // use as offset for block buffer
  lda_indy(0x6); // check current content of block buffer
  ram[0x3] = a; // and store here
  ldy_zp(0x4); // get old contents of Y again
  pla_fz(); // pull A from stack
  // if A = 1, branch
  if (zero_flag) {
    lda_zpx(SprObject_Y_Position); // if A = 0, load vertical coordinate
    goto RetYC; // and jump
  }
  // RetXC:
  lda_zpx(SprObject_X_Position); // otherwise load horizontal coordinate
  
RetYC:
  and_imm(0b00001111); // and mask out high nybble
  ram[0x4] = a; // store masked out result here
  lda_zp_fzn(0x3); // get saved content of block buffer
  return; // and leave
  // -------------------------------------------------------------------------------------
  // unused byte
  // -------------------------------------------------------------------------------------
  // $00 - offset to vine Y coordinate adder
  // $02 - offset to sprite data
}

void DrawVine(void) {
  ram[0x0] = y; // save offset here
  lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
  carry_flag = false;
  adc_absy_fc(VineYPosAdder); // add value using offset in Y to get value
  ldx_absy(VineObjOffset); // get offset to vine
  ldy_absx_fzn(Enemy_SprDataOffset); // get sprite data offset
  ram[0x2] = y; // store sprite data offset here
  cpu_call_begin(0xe448); SixSpriteStacker(); cpu_call_end(); // stack six sprites on top of each other vertically
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store in first, third and fifth sprites
  ram[Sprite_X_Position + 8 + y] = a;
  ram[Sprite_X_Position + 16 + y] = a;
  carry_flag = false;
  adc_imm(0x6); // add six pixels to second, fourth and sixth sprites
  ram[Sprite_X_Position + 4 + y] = a; // to give characteristic staggered vine shape to
  ram[Sprite_X_Position + 12 + y] = a; // our vertical stack of sprites
  ram[Sprite_X_Position + 20 + y] = a;
  lda_imm(0b00100001); // set bg priority and palette attribute bits
  ram[Sprite_Attributes + y] = a; // set in first, third and fifth sprites
  ram[Sprite_Attributes + 8 + y] = a;
  ram[Sprite_Attributes + 16 + y] = a;
  ora_imm(0b01000000); // additionally, set horizontal flip bit
  ram[Sprite_Attributes + 4 + y] = a; // for second, fourth and sixth sprites
  ram[Sprite_Attributes + 12 + y] = a;
  ram[Sprite_Attributes + 20 + y] = a;
  ldx_imm(0x5); // set tiles for six sprites
  
VineTL:
  lda_imm(0xe1); // set tile number for sprite
  ram[Sprite_Tilenumber + y] = a;
  iny(); // move offset to next sprite data
  iny();
  iny();
  iny();
  dex_fn(); // move onto next sprite
  if (!neg_flag) { goto VineTL; } // loop until all sprites are done
  ldy_zp(0x2); // get original offset
  lda_zp_fz(0x0); // get offset to vine adding data
  // if offset not zero, skip this part
  if (zero_flag) {
    lda_imm(0xe0);
    ram[Sprite_Tilenumber + y] = a; // set other tile number for top of vine
  }
  // SkpVTop:
  ldx_imm(0x0); // start with the first sprite again
  
ChkFTop:
  lda_abs(VineStart_Y_Position); // get original starting vertical coordinate
  carry_flag = true;
  sbc_absy(Sprite_Y_Position); // subtract top-most sprite's Y coordinate
  cmp_imm_fc(0x64); // if two coordinates are less than 100/$64 pixels
  // apart, skip this to leave sprite alone
  if (carry_flag) {
    lda_imm(0xf8);
    ram[Sprite_Y_Position + y] = a; // otherwise move sprite offscreen
  }
  // NextVSp:
  iny(); // move offset to next OAM data
  iny();
  iny();
  iny();
  inx(); // move onto next sprite
  cpx_imm_fcz(0x6); // do this until all sprites are checked
  if (!zero_flag) { goto ChkFTop; }
  ldy_zp_fzn(0x0); // return offset set earlier
  return;
}

void SixSpriteStacker(void) {
  ldx_imm(0x6); // do six sprites
  
StkLp:
  ram[Sprite_Data + y] = a; // store X or Y coordinate into OAM data
  carry_flag = false;
  adc_imm_fc(0x8); // add eight pixels
  iny();
  iny(); // move offset four bytes forward
  iny();
  iny();
  dex_fz(); // do another sprite
  if (!zero_flag) { goto StkLp; } // do this until all sprites are done
  ldy_zp_fzn(0x2); // get saved OAM data offset and leave
  return;
  // -------------------------------------------------------------------------------------
}

void DrawHammer(void) {
  // DrawHammer:
  ldy_absx(Misc_SprDataOffset); // get misc object OAM data offset
  lda_abs_fz(TimerControl);
  if (!zero_flag) { goto ForceHPose; } // if master timer control set, skip this part
  lda_zpx(Misc_State); // otherwise get hammer's state
  and_imm(0b01111111); // mask out d7
  cmp_imm_fz(0x1); // check to see if set to 1 yet
  if (zero_flag) { goto GetHPose; } // if so, branch
  
ForceHPose:
  ldx_imm(0x0); // reset offset here
  goto RenderH; // do unconditional branch to rendering part
  
GetHPose:
  lda_zp(FrameCounter); // get frame counter
  lsr_acc(); // move d3-d2 to d1-d0
  lsr_acc();
  and_imm(0b00000011); // mask out all but d1-d0 (changes every four frames)
  tax(); // use as timing offset
  
RenderH:
  lda_abs(Misc_Rel_YPos); // get relative vertical coordinate
  carry_flag = false;
  adc_absx(FirstSprYPos); // add first sprite vertical adder based on offset
  ram[Sprite_Y_Position + y] = a; // store as sprite Y coordinate for first sprite
  carry_flag = false;
  adc_absx(SecondSprYPos); // add second sprite vertical adder based on offset
  ram[Sprite_Y_Position + 4 + y] = a; // store as sprite Y coordinate for second sprite
  lda_abs(Misc_Rel_XPos); // get relative horizontal coordinate
  carry_flag = false;
  adc_absx(FirstSprXPos); // add first sprite horizontal adder based on offset
  ram[Sprite_X_Position + y] = a; // store as sprite X coordinate for first sprite
  carry_flag = false;
  adc_absx_fc(SecondSprXPos); // add second sprite horizontal adder based on offset
  ram[Sprite_X_Position + 4 + y] = a; // store as sprite X coordinate for second sprite
  lda_absx(FirstSprTilenum);
  ram[Sprite_Tilenumber + y] = a; // get and store tile number of first sprite
  lda_absx(SecondSprTilenum);
  ram[Sprite_Tilenumber + 4 + y] = a; // get and store tile number of second sprite
  lda_absx(HammerSprAttrib);
  ram[Sprite_Attributes + y] = a; // get and store attribute bytes for both
  ram[Sprite_Attributes + 4 + y] = a; // note in this case they use the same data
  ldx_zp(ObjectOffset); // get misc object offset
  lda_abs(Misc_OffscreenBits);
  and_imm_fzn(0b11111100); // check offscreen bits
  if (zero_flag) { return; } // if all bits clear, leave object alone
  lda_imm(0x0);
  ram[Misc_State + x] = a; // otherwise nullify misc object state
  lda_imm_fzn(0xf8);
  cpu_call_begin(0xe53f); DumpTwoSpr(); cpu_call_end(); // do sub to move hammer sprites offscreen
  // NoHOffscr:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00-$01 - used to hold tile numbers ($01 addressed in draw floatey number part)
  // $02 - used to hold Y coordinate for floatey number
  // $03 - residual byte used for flip (but value set here affects nothing)
  // $04 - attribute byte for floatey number
  // $05 - used as X coordinate for floatey number
}

void FlagpoleGfxHandler(void) {
  ldy_absx(Enemy_SprDataOffset); // get sprite data offset for flagpole flag
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X coordinate for first sprite
  carry_flag = false;
  adc_imm(0x8); // add eight pixels and store
  ram[Sprite_X_Position + 4 + y] = a; // as X coordinate for second and third sprites
  ram[Sprite_X_Position + 8 + y] = a;
  carry_flag = false;
  adc_imm_fc(0xc); // add twelve more pixels and
  ram[0x5] = a; // store here to be used later by floatey number
  lda_zpx_fzn(Enemy_Y_Position); // get vertical coordinate
  cpu_call_begin(0xe566); DumpTwoSpr(); cpu_call_end(); // and do sub to dump into first and second sprites
  adc_imm_fc(0x8); // add eight pixels
  ram[Sprite_Y_Position + 8 + y] = a; // and store into third sprite
  lda_abs(FlagpoleFNum_Y_Pos); // get vertical coordinate for floatey number
  ram[0x2] = a; // store it here
  lda_imm(0x1);
  ram[0x3] = a; // set value for flip which will not be used, and
  ram[0x4] = a; // attribute byte for floatey number
  ram[Sprite_Attributes + y] = a; // set attribute bytes for all three sprites
  ram[Sprite_Attributes + 4 + y] = a;
  ram[Sprite_Attributes + 8 + y] = a;
  lda_imm(0x7e);
  ram[Sprite_Tilenumber + y] = a; // put triangle shaped tile
  ram[Sprite_Tilenumber + 8 + y] = a; // into first and third sprites
  lda_imm(0x7f);
  ram[Sprite_Tilenumber + 4 + y] = a; // put skull tile into second sprite
  lda_abs_fz(FlagpoleCollisionYPos); // get vertical coordinate at time of collision
  // if zero, branch ahead
  if (!zero_flag) {
    tya();
    carry_flag = false; // add 12 bytes to sprite data offset
    adc_imm(0xc);
    tay(); // put back in Y
    lda_abs(FlagpoleScore); // get offset used to award points for touching flagpole
    asl_acc_fc(); // multiply by 2 to get proper offset here
    tax();
    lda_absx(FlagpoleScoreNumTiles); // get appropriate tile data
    ram[0x0] = a;
    lda_absx_fzn(FlagpoleScoreNumTiles + 1);
    cpu_call_begin(0xe5a6); DrawOneSpriteRow(); cpu_call_end(); // use it to render floatey number
  }
  // ChkFlagOffscreen:
  ldx_zp(ObjectOffset); // get object offset for flag
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  lda_abs(Enemy_OffscreenBits); // get offscreen bits
  and_imm_fzn(0b00001110); // mask out all but d3-d1
  if (!zero_flag) {
    // -------------------------------------------------------------------------------------
    MoveSixSpritesOffscreen(); // fallthrough
    return;
  }
}

void MoveSixSpritesOffscreen(void) {
  lda_imm_fzn(0xf8); // set offscreen coordinate if jumping here
  DumpSixSpr(); // fallthrough
  return;
}

void DumpSixSpr(void) {
  ram[Sprite_Data + 20 + y] = a; // dump A contents
  ram[Sprite_Data + 16 + y] = a; // into third row sprites
  DumpFourSpr(); // fallthrough
  return;
}

void DumpFourSpr(void) {
  ram[Sprite_Data + 12 + y] = a; // into second row sprites
  DumpThreeSpr(); // fallthrough
  return;
}

void DumpThreeSpr(void) {
  ram[Sprite_Data + 8 + y] = a;
  DumpTwoSpr(); // fallthrough
  return;
}

void DumpTwoSpr(void) {
  ram[Sprite_Data + 4 + y] = a; // and into first row sprites
  ram[Sprite_Data + y] = a;
  // ExitDumpSpr:
  return;
  // -------------------------------------------------------------------------------------
}

void DrawLargePlatform(void) {
  // DrawLargePlatform:
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  ram[0x2] = y; // store here
  iny(); // add 3 to it for offset
  iny(); // to X coordinate
  iny();
  lda_abs_fzn(Enemy_Rel_XPos); // get horizontal relative coordinate
  cpu_call_begin(0xe5d5); SixSpriteStacker(); cpu_call_end(); // store X coordinates using A as base, stack horizontally
  ldx_zp(ObjectOffset);
  lda_zpx_fzn(Enemy_Y_Position); // get vertical coordinate
  cpu_call_begin(0xe5dc); DumpFourSpr(); cpu_call_end(); // dump into first four sprites as Y coordinate
  ldy_abs(AreaType);
  cpy_imm_fcz(0x3); // check for castle-type level
  if (zero_flag) { goto ShrinkPlatform; }
  ldy_abs_fz(SecondaryHardMode); // check for secondary hard mode flag set
  if (zero_flag) { goto SetLast2Platform; } // branch if not set elsewhere
  
ShrinkPlatform:
  lda_imm(0xf8); // load offscreen coordinate if flag set or castle-type level
  
SetLast2Platform:
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  ram[Sprite_Y_Position + 16 + y] = a; // store vertical coordinate or offscreen
  ram[Sprite_Y_Position + 20 + y] = a; // coordinate into last two sprites as Y coordinate
  lda_imm(0x5b); // load default tile for platform (girder)
  ldx_abs_fz(CloudTypeOverride);
  if (zero_flag) { goto SetPlatformTilenum; } // if cloud level override flag not set, use
  lda_imm(0x75); // otherwise load other tile for platform (puff)
  
SetPlatformTilenum:
  ldx_zp(ObjectOffset); // get enemy object buffer offset
  iny_fzn(); // increment Y for tile offset
  cpu_call_begin(0xe602); DumpSixSpr(); cpu_call_end(); // dump tile number into all six sprites
  lda_imm(0x2); // set palette controls
  iny_fzn(); // increment Y for sprite attributes
  cpu_call_begin(0xe608); DumpSixSpr(); cpu_call_end(); // dump attributes into all six sprites
  inx_fzn(); // increment X for enemy objects
  cpu_call_begin(0xe60c); GetXOffscreenBits(); cpu_call_end(); // get offscreen bits again
  dex();
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  asl_acc_fc(); // rotate d7 into carry, save remaining
  pha(); // bits to the stack
  if (!carry_flag) { goto SChk2; }
  lda_imm(0xf8); // if d7 was set, move first sprite offscreen
  ram[Sprite_Y_Position + y] = a;
  
SChk2:
  pla(); // get bits from stack
  asl_acc_fc(); // rotate d6 into carry
  pha(); // save to stack
  if (!carry_flag) { goto SChk3; }
  lda_imm(0xf8); // if d6 was set, move second sprite offscreen
  ram[Sprite_Y_Position + 4 + y] = a;
  
SChk3:
  pla(); // get bits from stack
  asl_acc_fc(); // rotate d5 into carry
  pha(); // save to stack
  if (!carry_flag) { goto SChk4; }
  lda_imm(0xf8); // if d5 was set, move third sprite offscreen
  ram[Sprite_Y_Position + 8 + y] = a;
  
SChk4:
  pla(); // get bits from stack
  asl_acc_fc(); // rotate d4 into carry
  pha(); // save to stack
  if (!carry_flag) { goto SChk5; }
  lda_imm(0xf8); // if d4 was set, move fourth sprite offscreen
  ram[Sprite_Y_Position + 12 + y] = a;
  
SChk5:
  pla(); // get bits from stack
  asl_acc_fc(); // rotate d3 into carry
  pha(); // save to stack
  if (!carry_flag) { goto SChk6; }
  lda_imm(0xf8); // if d3 was set, move fifth sprite offscreen
  ram[Sprite_Y_Position + 16 + y] = a;
  
SChk6:
  pla(); // get bits from stack
  asl_acc_fc(); // rotate d2 into carry
  if (!carry_flag) { goto SLChk; } // save to stack
  lda_imm(0xf8);
  ram[Sprite_Y_Position + 20 + y] = a; // if d2 was set, move sixth sprite offscreen
  
SLChk:
  lda_abs(Enemy_OffscreenBits); // check d7 of offscreen bits
  asl_acc_fczn(); // and if d7 is not set, skip sub
  if (!carry_flag) { return; }
  cpu_call_begin(0xe653); MoveSixSpritesOffscreen(); cpu_call_end(); // otherwise branch to move all sprites offscreen
  // ExDLPl:
  return;
  // -------------------------------------------------------------------------------------
}

void JCoinGfxHandler(void) {
  goto JCoinGfxHandler;
  
DrawFloateyNumber_Coin:
  lda_zp(FrameCounter); // get frame counter
  lsr_acc_fc(); // divide by 2
  // branch if d0 not set to raise number every other frame
  if (!carry_flag) {
    dec_zpx(Misc_Y_Position); // otherwise, decrement vertical coordinate
  }
  // NotRsNum:
  lda_zpx_fzn(Misc_Y_Position); // get vertical coordinate
  cpu_call_begin(0xe660); DumpTwoSpr(); cpu_call_end(); // dump into both sprites
  lda_abs(Misc_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X coordinate for first sprite
  carry_flag = false;
  adc_imm_fc(0x8); // add eight pixels
  ram[Sprite_X_Position + 4 + y] = a; // store as X coordinate for second sprite
  lda_imm(0x2);
  ram[Sprite_Attributes + y] = a; // store attribute byte in both sprites
  ram[Sprite_Attributes + 4 + y] = a;
  lda_imm(0xf7);
  ram[Sprite_Tilenumber + y] = a; // put tile numbers into both sprites
  lda_imm_fzn(0xfb); // that resemble "200"
  ram[Sprite_Tilenumber + 4 + y] = a;
  return; // then jump to leave (why not an rts here instead?)
  
JCoinGfxHandler:
  ldy_absx(Misc_SprDataOffset); // get coin/floatey number's OAM data offset
  lda_zpx(Misc_State); // get state of misc object
  cmp_imm_fc(0x2); // if 2 or greater, 
  if (carry_flag) { goto DrawFloateyNumber_Coin; } // branch to draw floatey number
  lda_zpx(Misc_Y_Position); // store vertical coordinate as
  ram[Sprite_Y_Position + y] = a; // Y coordinate for first sprite
  carry_flag = false;
  adc_imm(0x8); // add eight pixels
  ram[Sprite_Y_Position + 4 + y] = a; // store as Y coordinate for second sprite
  lda_abs(Misc_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a;
  ram[Sprite_X_Position + 4 + y] = a; // store as X coordinate for first and second sprites
  lda_zp(FrameCounter); // get frame counter
  lsr_acc_fc(); // divide by 2 to alter every other frame
  and_imm(0b00000011); // mask out d2-d1
  tax(); // use as graphical offset
  lda_absx(JumpingCoinTiles); // load tile number
  iny_fzn(); // increment OAM data offset to write tile numbers
  cpu_call_begin(0xe6af); DumpTwoSpr(); cpu_call_end(); // do sub to dump tile number into both sprites
  dey(); // decrement to get old offset
  lda_imm(0x2);
  ram[Sprite_Attributes + y] = a; // set attribute byte in first sprite
  lda_imm(0x82);
  ram[Sprite_Attributes + 4 + y] = a; // set attribute byte with vertical flip in second sprite
  ldx_zp_fzn(ObjectOffset); // get misc object offset
  // ExJCGfx:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00-$01 - used to hold tiles for drawing the power-up, $00 also used to hold power-up type
  // $02 - used to hold bottom row Y position
  // $03 - used to hold flip control (not used here)
  // $04 - used to hold sprite attributes
  // $05 - used to hold X position
  // $07 - counter
  // tiles arranged in top left, right, bottom left, right order
}

void DrawPowerUp(void) {
  // DrawPowerUp:
  ldy_abs(Enemy_SprDataOffset + 5); // get power-up's sprite data offset
  lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
  carry_flag = false;
  adc_imm(0x8); // add eight pixels
  ram[0x2] = a; // store result here
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  ram[0x5] = a; // store here
  ldx_zp(PowerUpType); // get power-up type
  lda_absx(PowerUpAttributes); // get attribute data for power-up type
  ora_abs(Enemy_SprAttrib + 5); // add background priority bit if set
  ram[0x4] = a; // store attributes here
  txa();
  pha(); // save power-up type to the stack
  asl_acc();
  asl_acc_fc(); // multiply by four to get proper offset
  tax(); // use as X
  lda_imm(0x1);
  ram[0x7] = a; // set counter here to draw two rows of sprite object
  ram[0x3] = a; // init d1 of flip control
  
PUpDrawLoop:
  lda_absx(PowerUpGfxTable); // load left tile of power-up object
  ram[0x0] = a;
  lda_absx_fzn(PowerUpGfxTable + 1); // load right tile
  cpu_call_begin(0xe701); DrawOneSpriteRow(); cpu_call_end(); // branch to draw one row of our power-up object
  dec_zp_fn(0x7); // decrement counter
  if (!neg_flag) { goto PUpDrawLoop; } // branch until two rows are drawn
  ldy_abs(Enemy_SprDataOffset + 5); // get sprite data offset again
  pla_fz(); // pull saved power-up type from the stack
  if (zero_flag) { goto PUpOfs; } // if regular mushroom, branch, do not change colors or flip
  cmp_imm_fz(0x3);
  if (zero_flag) { goto PUpOfs; } // if 1-up mushroom, branch, do not change colors or flip
  ram[0x0] = a; // store power-up type here now
  lda_zp(FrameCounter); // get frame counter
  lsr_acc(); // divide by 2 to change colors every two frames
  and_imm(0b00000011); // mask out all but d1 and d0 (previously d2 and d1)
  ora_abs(Enemy_SprAttrib + 5); // add background priority bit if any set
  ram[Sprite_Attributes + y] = a; // set as new palette bits for top left and
  ram[Sprite_Attributes + 4 + y] = a; // top right sprites for fire flower and star
  ldx_zp(0x0);
  dex_fz(); // check power-up type for fire flower
  if (zero_flag) { goto FlipPUpRightSide; } // if found, skip this part
  ram[Sprite_Attributes + 8 + y] = a; // otherwise set new palette bits  for bottom left
  ram[Sprite_Attributes + 12 + y] = a; // and bottom right sprites as well for star only
  
FlipPUpRightSide:
  lda_absy(Sprite_Attributes + 4);
  ora_imm(0b01000000); // set horizontal flip bit for top right sprite
  ram[Sprite_Attributes + 4 + y] = a;
  lda_absy(Sprite_Attributes + 12);
  ora_imm(0b01000000); // set horizontal flip bit for bottom right sprite
  ram[Sprite_Attributes + 12 + y] = a; // note these are only done for fire flower and star power-ups
  
PUpOfs:
  goto SprObjectOffscrChk; // jump to check to see if power-up is offscreen at all, then leave
  // -------------------------------------------------------------------------------------
  // $00-$01 - used in DrawEnemyObjRow to hold sprite tile numbers
  // $02 - used to store Y position
  // $03 - used to store moving direction, used to flip enemies horizontally
  // $04 - used to store enemy's sprite attributes
  // $05 - used to store X position
  // $eb - used to hold sprite data offset
  // $ec - used to hold either altered enemy state or special value used in gfx handler as condition
  // $ed - used to hold enemy state from buffer 
  // $ef - used to hold enemy code used in gfx handler (may or may not resemble Enemy_ID values)
  // tiles arranged in top left, right, middle left, right, bottom left, right order
  
SprObjectOffscrChk:
  ldx_zp(ObjectOffset); // get enemy buffer offset
  lda_abs(Enemy_OffscreenBits); // check offscreen information
  lsr_acc();
  lsr_acc(); // shift three times to the right
  lsr_acc_fc(); // which puts d2 into carry
  pha(); // save to stack
  if (!carry_flag) { goto LcChk; } // branch if not set
  lda_imm_fzn(0x4); // set for right column sprites
  cpu_call_begin(0xeb73); MoveESprColOffscreen(); cpu_call_end(); // and move them offscreen
  
LcChk:
  pla(); // get from stack
  lsr_acc_fc(); // move d3 to carry
  pha(); // save to stack
  if (!carry_flag) { goto Row3C; } // branch if not set
  lda_imm_fzn(0x0); // set for left column sprites,
  cpu_call_begin(0xeb7d); MoveESprColOffscreen(); cpu_call_end(); // move them offscreen
  
Row3C:
  pla(); // get from stack again
  lsr_acc(); // move d5 to carry this time
  lsr_acc_fc();
  pha(); // save to stack again
  if (!carry_flag) { goto Row23C; } // branch if carry not set
  lda_imm_fzn(0x10); // set for third row of sprites
  cpu_call_begin(0xeb88); MoveESprRowOffscreen(); cpu_call_end(); // and move them offscreen
  
Row23C:
  pla(); // get from stack
  lsr_acc_fc(); // move d6 into carry
  pha(); // save to stack
  if (!carry_flag) { goto AllRowC; }
  lda_imm_fzn(0x8); // set for second and third rows
  cpu_call_begin(0xeb92); MoveESprRowOffscreen(); cpu_call_end(); // move them offscreen
  
AllRowC:
  pla(); // get from stack once more
  lsr_acc_fczn(); // move d7 into carry
  if (!carry_flag) { return; }
  cpu_call_begin(0xeb99); MoveESprRowOffscreen(); cpu_call_end(); // move all sprites offscreen (A should be 0 by now)
  lda_zpx(Enemy_ID);
  cmp_imm_fczn(Podoboo); // check enemy identifier for podoboo
  if (zero_flag) { return; } // skip this part if found, we do not want to erase podoboo!
  lda_zpx(Enemy_Y_HighPos); // check high byte of vertical position
  cmp_imm_fczn(0x2); // if not yet past the bottom of the screen, branch
  if (!zero_flag) { return; }
  cpu_call_begin(0xeba8); EraseEnemyObject(); cpu_call_end(); // what it says
  // ExEGHandler:
  return;
}

void EnemyGfxHandler(void) {
  // EnemyGfxHandler:
  lda_zpx(Enemy_Y_Position); // get enemy object vertical position
  ram[0x2] = a;
  lda_abs(Enemy_Rel_XPos); // get enemy object horizontal position
  ram[0x5] = a; // relative to screen
  ldy_absx(Enemy_SprDataOffset);
  ram[0xeb] = y; // get sprite data offset
  lda_imm(0x0);
  ram[VerticalFlipFlag] = a; // initialize vertical flip flag by default
  lda_zpx(Enemy_MovingDir);
  ram[0x3] = a; // get enemy object moving direction
  lda_absx(Enemy_SprAttrib);
  ram[0x4] = a; // get enemy object sprite attributes
  lda_zpx(Enemy_ID);
  cmp_imm_fcz(PiranhaPlant); // is enemy object piranha plant?
  if (!zero_flag) { goto CheckForRetainerObj; } // if not, branch
  ldy_zpx_fn(PiranhaPlant_Y_Speed);
  if (neg_flag) { goto CheckForRetainerObj; } // if piranha plant moving upwards, branch
  ldy_absx_fzn(EnemyFrameTimer);
  if (zero_flag) { goto CheckForRetainerObj; } // if timer for movement expired, branch
  return; // if all conditions fail, leave
  
CheckForRetainerObj:
  lda_zpx(Enemy_State); // store enemy state
  ram[0xed] = a;
  and_imm(0b00011111); // nullify all but 5 LSB and use as Y
  tay();
  lda_zpx(Enemy_ID); // check for mushroom retainer/princess object
  cmp_imm_fz(RetainerObject);
  if (!zero_flag) { goto CheckForBulletBillCV; } // if not found, branch
  ldy_imm(0x0); // if found, nullify saved state in Y
  lda_imm(0x1); // set value that will not be used
  ram[0x3] = a;
  lda_imm(0x15); // set value $15 as code for mushroom retainer/princess object
  
CheckForBulletBillCV:
  cmp_imm_fz(BulletBill_CannonVar); // otherwise check for bullet bill object
  if (!zero_flag) { goto CheckForJumpspring; } // if not found, branch again
  dec_zp(0x2); // decrement saved vertical position
  lda_imm(0x3);
  ldy_absx_fz(EnemyFrameTimer); // get timer for enemy object
  if (zero_flag) { goto SBBAt; } // if expired, do not set priority bit
  ora_imm(0b00100000); // otherwise do so
  
SBBAt:
  ram[0x4] = a; // set new sprite attributes
  ldy_imm(0x0); // nullify saved enemy state both in Y and in
  ram[0xed] = y; // memory location here
  lda_imm(0x8); // set specific value to unconditionally branch once
  
CheckForJumpspring:
  cmp_imm_fz(JumpspringObject); // check for jumpspring object
  if (!zero_flag) { goto CheckForPodoboo; }
  ldy_imm(0x3); // set enemy state -2 MSB here for jumpspring object
  ldx_abs(JumpspringAnimCtrl); // get current frame number for jumpspring object
  lda_absx(JumpspringFrameOffsets); // load data using frame number as offset
  
CheckForPodoboo:
  ram[0xef] = a; // store saved enemy object value here
  ram[0xec] = y; // and Y here (enemy state -2 MSB if not changed)
  ldx_zp(ObjectOffset); // get enemy object offset
  cmp_imm_fz(0xc); // check for podoboo object
  if (!zero_flag) { goto CheckBowserGfxFlag; } // branch if not found
  lda_zpx_fn(Enemy_Y_Speed); // if moving upwards, branch
  if (neg_flag) { goto CheckBowserGfxFlag; }
  inc_abs(VerticalFlipFlag); // otherwise, set flag for vertical flip
  
CheckBowserGfxFlag:
  lda_abs_fz(BowserGfxFlag); // if not drawing bowser at all, skip to something else
  if (zero_flag) { goto CheckForGoomba; }
  ldy_imm(0x16); // if set to 1, draw bowser's front
  cmp_imm_fz(0x1);
  if (zero_flag) { goto SBwsrGfxOfs; }
  iny(); // otherwise draw bowser's rear
  
SBwsrGfxOfs:
  ram[0xef] = y;
  
CheckForGoomba:
  ldy_zp(0xef); // check value for goomba object
  cpy_imm_fz(Goomba);
  if (!zero_flag) { goto CheckBowserFront; } // branch if not found
  lda_zpx(Enemy_State);
  cmp_imm_fc(0x2); // check for defeated state
  if (!carry_flag) { goto GmbaAnim; } // if not defeated, go ahead and animate
  ldx_imm(0x4); // if defeated, write new value here
  ram[0xec] = x;
  
GmbaAnim:
  and_imm(0b00100000); // check for d5 set in enemy object state 
  ora_abs_fz(TimerControl); // or timer disable flag set
  if (!zero_flag) { goto CheckBowserFront; } // if either condition true, do not animate goomba
  lda_zp(FrameCounter);
  and_imm_fz(0b00001000); // check for every eighth frame
  if (!zero_flag) { goto CheckBowserFront; }
  lda_zp(0x3);
  eor_imm(0b00000011); // invert bits to flip horizontally every eight frames
  ram[0x3] = a; // leave alone otherwise
  
CheckBowserFront:
  lda_absy(EnemyAttributeData); // load sprite attribute using enemy object
  ora_zp(0x4); // as offset, and add to bits already loaded
  ram[0x4] = a;
  lda_absy(EnemyGfxTableOffsets); // load value based on enemy object as offset
  tax(); // save as X
  ldy_zp(0xec); // get previously saved value
  lda_abs_fz(BowserGfxFlag);
  if (zero_flag) { goto CheckForSpiny; } // if not drawing bowser object at all, skip all of this
  cmp_imm_fcz(0x1);
  if (!zero_flag) { goto CheckBowserRear; } // if not drawing front part, branch to draw the rear part
  lda_abs_fn(BowserBodyControls); // check bowser's body control bits
  if (!neg_flag) { goto ChkFrontSte; } // branch if d7 not set (control's bowser's mouth)      
  ldx_imm(0xde); // otherwise load offset for second frame
  
ChkFrontSte:
  lda_zp(0xed); // check saved enemy state
  and_imm_fz(0b00100000); // if bowser not defeated, do not set flag
  if (zero_flag) { goto DrawBowser; }
  
FlipBowserOver:
  ram[VerticalFlipFlag] = x; // set vertical flip flag to nonzero
  
DrawBowser:
  goto DrawEnemyObject; // draw bowser's graphics now
  
CheckBowserRear:
  lda_abs(BowserBodyControls); // check bowser's body control bits
  and_imm_fz(0x1);
  if (zero_flag) { goto ChkRearSte; } // branch if d0 not set (control's bowser's feet)
  ldx_imm(0xe4); // otherwise load offset for second frame
  
ChkRearSte:
  lda_zp(0xed); // check saved enemy state
  and_imm_fz(0b00100000); // if bowser not defeated, do not set flag
  if (zero_flag) { goto DrawBowser; }
  lda_zp(0x2); // subtract 16 pixels from
  carry_flag = true; // saved vertical coordinate
  sbc_imm_fc(0x10);
  ram[0x2] = a;
  goto FlipBowserOver; // jump to set vertical flip flag
  
CheckForSpiny:
  cpx_imm_fz(0x24); // check if value loaded is for spiny
  if (!zero_flag) { goto CheckForLakitu; } // if not found, branch
  cpy_imm_fz(0x5); // if enemy state set to $05, do this,
  if (!zero_flag) { goto NotEgg; } // otherwise branch
  ldx_imm(0x30); // set to spiny egg offset
  lda_imm(0x2);
  ram[0x3] = a; // set enemy direction to reverse sprites horizontally
  lda_imm(0x5);
  ram[0xec] = a; // set enemy state
  
NotEgg:
  goto CheckForHammerBro; // skip a big chunk of this if we found spiny but not in egg
  
CheckForLakitu:
  cpx_imm_fcz(0x90); // check value for lakitu's offset loaded
  if (!zero_flag) { goto CheckUpsideDownShell; } // branch if not loaded
  lda_zp(0xed);
  and_imm_fz(0b00100000); // check for d5 set in enemy state
  if (!zero_flag) { goto NoLAFr; } // branch if set
  lda_abs(FrenzyEnemyTimer);
  cmp_imm_fc(0x10); // check timer to see if we've reached a certain range
  if (carry_flag) { goto NoLAFr; } // branch if not
  ldx_imm(0x96); // if d6 not set and timer in range, load alt frame for lakitu
  
NoLAFr:
  goto CheckDefeatedState; // skip this next part if we found lakitu but alt frame not needed
  
CheckUpsideDownShell:
  lda_zp(0xef); // check for enemy object => $04
  cmp_imm_fc(0x4);
  if (carry_flag) { goto CheckRightSideUpShell; } // branch if true
  cpy_imm_fc(0x2);
  if (!carry_flag) { goto CheckRightSideUpShell; } // branch if enemy state < $02
  ldx_imm(0x5a); // set for upside-down koopa shell by default
  ldy_zp(0xef);
  cpy_imm_fz(BuzzyBeetle); // check for buzzy beetle object
  if (!zero_flag) { goto CheckRightSideUpShell; }
  ldx_imm(0x7e); // set for upside-down buzzy beetle shell if found
  inc_zp(0x2); // increment vertical position by one pixel
  
CheckRightSideUpShell:
  lda_zp(0xec); // check for value set here
  cmp_imm_fz(0x4); // if enemy state < $02, do not change to shell, if
  if (!zero_flag) { goto CheckForHammerBro; } // enemy state => $02 but not = $04, leave shell upside-down
  ldx_imm(0x72); // set right-side up buzzy beetle shell by default
  inc_zp(0x2); // increment saved vertical position by one pixel
  ldy_zp(0xef);
  cpy_imm_fz(BuzzyBeetle); // check for buzzy beetle object
  if (zero_flag) { goto CheckForDefdGoomba; } // branch if found
  ldx_imm(0x66); // change to right-side up koopa shell if not found
  inc_zp(0x2); // and increment saved vertical position again
  
CheckForDefdGoomba:
  cpy_imm_fz(Goomba); // check for goomba object (necessary if previously
  if (!zero_flag) { goto CheckForHammerBro; } // failed buzzy beetle object test)
  ldx_imm(0x54); // load for regular goomba
  lda_zp(0xed); // note that this only gets performed if enemy state => $02
  and_imm_fz(0b00100000); // check saved enemy state for d5 set
  if (!zero_flag) { goto CheckForHammerBro; } // branch if set
  ldx_imm(0x8a); // load offset for defeated goomba
  dec_zp(0x2); // set different value and decrement saved vertical position
  
CheckForHammerBro:
  ldy_zp(ObjectOffset);
  lda_zp(0xef); // check for hammer bro object
  cmp_imm_fcz(HammerBro);
  if (!zero_flag) { goto CheckForBloober; } // branch if not found
  lda_zp_fz(0xed);
  if (zero_flag) { goto CheckToAnimateEnemy; } // branch if not in normal enemy state
  and_imm_fz(0b00001000);
  if (zero_flag) { goto CheckDefeatedState; } // if d3 not set, branch further away
  ldx_imm(0xb4); // otherwise load offset for different frame
  goto CheckToAnimateEnemy; // unconditional branch
  
CheckForBloober:
  cpx_imm_fz(0x48); // check for cheep-cheep offset loaded
  if (zero_flag) { goto CheckToAnimateEnemy; } // branch if found
  lda_absy(EnemyIntervalTimer);
  cmp_imm_fc(0x5);
  if (carry_flag) { goto CheckDefeatedState; } // branch if some timer is above a certain point
  cpx_imm_fz(0x3c); // check for bloober offset loaded
  if (!zero_flag) { goto CheckToAnimateEnemy; } // branch if not found this time
  cmp_imm_fcz(0x1);
  if (zero_flag) { goto CheckDefeatedState; } // branch if timer is set to certain point
  inc_zp(0x2); // increment saved vertical coordinate three pixels
  inc_zp(0x2);
  inc_zp(0x2);
  goto CheckAnimationStop; // and do something else
  
CheckToAnimateEnemy:
  lda_zp(0xef); // check for specific enemy objects
  cmp_imm_fcz(Goomba);
  if (zero_flag) { goto CheckDefeatedState; } // branch if goomba
  cmp_imm_fcz(0x8);
  if (zero_flag) { goto CheckDefeatedState; } // branch if bullet bill (note both variants use $08 here)
  cmp_imm_fcz(Podoboo);
  if (zero_flag) { goto CheckDefeatedState; } // branch if podoboo
  cmp_imm_fc(0x18); // branch if => $18
  if (carry_flag) { goto CheckDefeatedState; }
  ldy_imm(0x0);
  cmp_imm_fcz(0x15); // check for mushroom retainer/princess object
  if (!zero_flag) { goto CheckForSecondFrame; } // which uses different code here, branch if not found
  iny(); // residual instruction
  lda_abs(WorldNumber); // are we on world 8?
  cmp_imm_fc(World8);
  if (carry_flag) { goto CheckDefeatedState; } // if so, leave the offset alone (use princess)
  ldx_imm(0xa2); // otherwise, set for mushroom retainer object instead
  lda_imm(0x3); // set alternate state here
  ram[0xec] = a;
  goto CheckDefeatedState; // unconditional branch
  
CheckForSecondFrame:
  lda_zp(FrameCounter); // load frame counter
  and_absy_fz(EnemyAnimTimingBMask); // mask it (partly residual, one byte not ever used)
  if (!zero_flag) { goto CheckDefeatedState; } // branch if timing is off
  
CheckAnimationStop:
  lda_zp(0xed); // check saved enemy state
  and_imm(0b10100000); // for d7 or d5, or check for timers stopped
  ora_abs_fz(TimerControl);
  if (!zero_flag) { goto CheckDefeatedState; } // if either condition true, branch
  txa();
  carry_flag = false;
  adc_imm_fc(0x6); // add $06 to current enemy offset
  tax(); // to animate various enemy objects
  
CheckDefeatedState:
  lda_zp(0xed); // check saved enemy state
  and_imm_fz(0b00100000); // for d5 set
  if (zero_flag) { goto DrawEnemyObject; } // branch if not set
  lda_zp(0xef);
  cmp_imm_fc(0x4); // check for saved enemy object => $04
  if (!carry_flag) { goto DrawEnemyObject; } // branch if less
  ldy_imm(0x1);
  ram[VerticalFlipFlag] = y; // set vertical flip flag
  dey();
  ram[0xec] = y; // init saved value here
  
DrawEnemyObject:
  ldy_zp_fzn(0xeb); // load sprite data offset
  cpu_call_begin(0xea4f); DrawEnemyObjRow(); cpu_call_end(); // draw six tiles of data
  cpu_call_begin(0xea52); DrawEnemyObjRow(); cpu_call_end(); // into sprite data
  cpu_call_begin(0xea55); DrawEnemyObjRow(); cpu_call_end();
  ldx_zp(ObjectOffset); // get enemy object offset
  ldy_absx(Enemy_SprDataOffset); // get sprite data offset
  lda_zp(0xef);
  cmp_imm_fcz(0x8); // get saved enemy object and check
  if (!zero_flag) { goto CheckForVerticalFlip; } // for bullet bill, branch if not found
  
SkipToOffScrChk:
  goto SprObjectOffscrChk; // jump if found
  
CheckForVerticalFlip:
  lda_abs_fz(VerticalFlipFlag); // check if vertical flip flag is set here
  if (zero_flag) { goto CheckForESymmetry; } // branch if not
  lda_absy(Sprite_Attributes); // get attributes of first sprite we dealt with
  ora_imm(0b10000000); // set bit for vertical flip
  iny();
  iny_fzn(); // increment two bytes so that we store the vertical flip
  cpu_call_begin(0xea72); DumpSixSpr(); cpu_call_end(); // in attribute bytes of enemy obj sprite data
  dey();
  dey(); // now go back to the Y coordinate offset
  tya();
  tax(); // give offset to X
  lda_zp(0xef);
  cmp_imm_fz(HammerBro); // check saved enemy object for hammer bro
  if (zero_flag) { goto FlipEnemyVertically; }
  cmp_imm_fz(Lakitu); // check saved enemy object for lakitu
  if (zero_flag) { goto FlipEnemyVertically; } // branch for hammer bro or lakitu
  cmp_imm_fc(0x15);
  if (carry_flag) { goto FlipEnemyVertically; } // also branch if enemy object => $15
  txa();
  carry_flag = false;
  adc_imm(0x8); // if not selected objects or => $15, set
  tax(); // offset in X for next row
  
FlipEnemyVertically:
  lda_absx(Sprite_Tilenumber); // load first or second row tiles
  pha(); // and save tiles to the stack
  lda_absx(Sprite_Tilenumber + 4);
  pha();
  lda_absy(Sprite_Tilenumber + 16); // exchange third row tiles
  ram[Sprite_Tilenumber + x] = a; // with first or second row tiles
  lda_absy(Sprite_Tilenumber + 20);
  ram[Sprite_Tilenumber + 4 + x] = a;
  pla(); // pull first or second row tiles from stack
  ram[Sprite_Tilenumber + 20 + y] = a; // and save in third row
  pla();
  ram[Sprite_Tilenumber + 16 + y] = a;
  
CheckForESymmetry:
  lda_abs_fz(BowserGfxFlag); // are we drawing bowser at all?
  if (!zero_flag) { goto SkipToOffScrChk; } // branch if so
  lda_zp(0xef);
  ldx_zp(0xec); // get alternate enemy state
  cmp_imm_fz(0x5); // check for hammer bro object
  if (!zero_flag) { goto ContES; }
  goto SprObjectOffscrChk; // jump if found
  
ContES:
  cmp_imm_fz(Bloober); // check for bloober object
  if (zero_flag) { goto MirrorEnemyGfx; }
  cmp_imm_fz(PiranhaPlant); // check for piranha plant object
  if (zero_flag) { goto MirrorEnemyGfx; }
  cmp_imm_fz(Podoboo); // check for podoboo object
  if (zero_flag) { goto MirrorEnemyGfx; } // branch if either of three are found
  cmp_imm_fz(Spiny); // check for spiny object
  if (!zero_flag) { goto ESRtnr; } // branch closer if not found
  cpx_imm_fz(0x5); // check spiny's state
  if (!zero_flag) { goto CheckToMirrorLakitu; } // branch if not an egg, otherwise
  
ESRtnr:
  cmp_imm_fz(0x15); // check for princess/mushroom retainer object
  if (!zero_flag) { goto SpnySC; }
  lda_imm(0x42); // set horizontal flip on bottom right sprite
  ram[Sprite_Attributes + 20 + y] = a; // note that palette bits were already set earlier
  
SpnySC:
  cpx_imm_fc(0x2); // if alternate enemy state set to 1 or 0, branch
  if (!carry_flag) { goto CheckToMirrorLakitu; }
  
MirrorEnemyGfx:
  lda_abs_fz(BowserGfxFlag); // if enemy object is bowser, skip all of this
  if (!zero_flag) { goto CheckToMirrorLakitu; }
  lda_absy(Sprite_Attributes); // load attribute bits of first sprite
  and_imm(0b10100011);
  ram[Sprite_Attributes + y] = a; // save vertical flip, priority, and palette bits
  ram[Sprite_Attributes + 8 + y] = a; // in left sprite column of enemy object OAM data
  ram[Sprite_Attributes + 16 + y] = a;
  ora_imm(0b01000000); // set horizontal flip
  cpx_imm_fz(0x5); // check for state used by spiny's egg
  if (!zero_flag) { goto EggExc; } // if alternate state not set to $05, branch
  ora_imm(0b10000000); // otherwise set vertical flip
  
EggExc:
  ram[Sprite_Attributes + 4 + y] = a; // set bits of right sprite column
  ram[Sprite_Attributes + 12 + y] = a; // of enemy object sprite data
  ram[Sprite_Attributes + 20 + y] = a;
  cpx_imm_fz(0x4); // check alternate enemy state
  if (!zero_flag) { goto CheckToMirrorLakitu; } // branch if not $04
  lda_absy(Sprite_Attributes + 8); // get second row left sprite attributes
  ora_imm(0b10000000);
  ram[Sprite_Attributes + 8 + y] = a; // store bits with vertical flip in
  ram[Sprite_Attributes + 16 + y] = a; // second and third row left sprites
  ora_imm(0b01000000);
  ram[Sprite_Attributes + 12 + y] = a; // store with horizontal and vertical flip in
  ram[Sprite_Attributes + 20 + y] = a; // second and third row right sprites
  
CheckToMirrorLakitu:
  lda_zp(0xef); // check for lakitu enemy object
  cmp_imm_fz(Lakitu);
  if (!zero_flag) { goto CheckToMirrorJSpring; } // branch if not found
  lda_abs_fz(VerticalFlipFlag);
  if (!zero_flag) { goto NVFLak; } // branch if vertical flip flag not set
  lda_absy(Sprite_Attributes + 16); // save vertical flip and palette bits
  and_imm(0b10000001); // in third row left sprite
  ram[Sprite_Attributes + 16 + y] = a;
  lda_absy(Sprite_Attributes + 20); // set horizontal flip and palette bits
  ora_imm(0b01000001); // in third row right sprite
  ram[Sprite_Attributes + 20 + y] = a;
  ldx_abs(FrenzyEnemyTimer); // check timer
  cpx_imm_fc(0x10);
  if (carry_flag) { goto SprObjectOffscrChk; } // branch if timer has not reached a certain range
  ram[Sprite_Attributes + 12 + y] = a; // otherwise set same for second row right sprite
  and_imm(0b10000001);
  ram[Sprite_Attributes + 8 + y] = a; // preserve vertical flip and palette bits for left sprite
  if (!carry_flag) { goto SprObjectOffscrChk; } // unconditional branch
  
NVFLak:
  lda_absy(Sprite_Attributes); // get first row left sprite attributes
  and_imm(0b10000001);
  ram[Sprite_Attributes + y] = a; // save vertical flip and palette bits
  lda_absy(Sprite_Attributes + 4); // get first row right sprite attributes
  ora_imm(0b01000001); // set horizontal flip and palette bits
  ram[Sprite_Attributes + 4 + y] = a; // note that vertical flip is left as-is
  
CheckToMirrorJSpring:
  lda_zp(0xef); // check for jumpspring object (any frame)
  cmp_imm_fc(0x18);
  if (!carry_flag) { goto SprObjectOffscrChk; } // branch if not jumpspring object at all
  lda_imm(0x82);
  ram[Sprite_Attributes + 8 + y] = a; // set vertical flip and palette bits of 
  ram[Sprite_Attributes + 16 + y] = a; // second and third row left sprites
  ora_imm(0b01000000);
  ram[Sprite_Attributes + 12 + y] = a; // set, in addition to those, horizontal flip
  ram[Sprite_Attributes + 20 + y] = a; // for second and third row right sprites
  
SprObjectOffscrChk:
  ldx_zp(ObjectOffset); // get enemy buffer offset
  lda_abs(Enemy_OffscreenBits); // check offscreen information
  lsr_acc();
  lsr_acc(); // shift three times to the right
  lsr_acc_fc(); // which puts d2 into carry
  pha(); // save to stack
  if (!carry_flag) { goto LcChk; } // branch if not set
  lda_imm_fzn(0x4); // set for right column sprites
  cpu_call_begin(0xeb73); MoveESprColOffscreen(); cpu_call_end(); // and move them offscreen
  
LcChk:
  pla(); // get from stack
  lsr_acc_fc(); // move d3 to carry
  pha(); // save to stack
  if (!carry_flag) { goto Row3C; } // branch if not set
  lda_imm_fzn(0x0); // set for left column sprites,
  cpu_call_begin(0xeb7d); MoveESprColOffscreen(); cpu_call_end(); // move them offscreen
  
Row3C:
  pla(); // get from stack again
  lsr_acc(); // move d5 to carry this time
  lsr_acc_fc();
  pha(); // save to stack again
  if (!carry_flag) { goto Row23C; } // branch if carry not set
  lda_imm_fzn(0x10); // set for third row of sprites
  cpu_call_begin(0xeb88); MoveESprRowOffscreen(); cpu_call_end(); // and move them offscreen
  
Row23C:
  pla(); // get from stack
  lsr_acc_fc(); // move d6 into carry
  pha(); // save to stack
  if (!carry_flag) { goto AllRowC; }
  lda_imm_fzn(0x8); // set for second and third rows
  cpu_call_begin(0xeb92); MoveESprRowOffscreen(); cpu_call_end(); // move them offscreen
  
AllRowC:
  pla(); // get from stack once more
  lsr_acc_fczn(); // move d7 into carry
  if (!carry_flag) { return; }
  cpu_call_begin(0xeb99); MoveESprRowOffscreen(); cpu_call_end(); // move all sprites offscreen (A should be 0 by now)
  lda_zpx(Enemy_ID);
  cmp_imm_fczn(Podoboo); // check enemy identifier for podoboo
  if (zero_flag) { return; } // skip this part if found, we do not want to erase podoboo!
  lda_zpx(Enemy_Y_HighPos); // check high byte of vertical position
  cmp_imm_fczn(0x2); // if not yet past the bottom of the screen, branch
  if (!zero_flag) { return; }
  cpu_call_begin(0xeba8); EraseEnemyObject(); cpu_call_end(); // what it says
  // ExEGHandler:
  return;
}

void DrawEnemyObjRow(void) {
  lda_absx(EnemyGraphicsTable); // load two tiles of enemy graphics
  ram[0x0] = a;
  lda_absx(EnemyGraphicsTable + 1);
  DrawOneSpriteRow(); // fallthrough
  return;
}

void DrawOneSpriteRow(void) {
  ram[0x1] = a;
  goto DrawSpriteObject; // draw them
  
DrawSpriteObject:
  lda_zp(0x3); // get saved flip control bits
  lsr_acc();
  lsr_acc_fc(); // move d1 into carry
  lda_zp(0x0);
  // if d1 not set, branch
  if (carry_flag) {
    ram[Sprite_Tilenumber + 4 + y] = a; // store first tile into second sprite
    lda_zp(0x1); // and second into first sprite
    ram[Sprite_Tilenumber + y] = a;
    lda_imm(0x40); // activate horizontal flip OAM attribute
    goto SetHFAt; // and unconditionally branch
  }
  // NoHFlip:
  ram[Sprite_Tilenumber + y] = a; // store first tile into first sprite
  lda_zp(0x1); // and second into second sprite
  ram[Sprite_Tilenumber + 4 + y] = a;
  lda_imm(0x0); // clear bit for horizontal flip
  
SetHFAt:
  ora_zp(0x4); // add other OAM attributes if necessary
  ram[Sprite_Attributes + y] = a; // store sprite attributes
  ram[Sprite_Attributes + 4 + y] = a;
  lda_zp(0x2); // now the y coordinates
  ram[Sprite_Y_Position + y] = a; // note because they are
  ram[Sprite_Y_Position + 4 + y] = a; // side by side, they are the same
  lda_zp(0x5);
  ram[Sprite_X_Position + y] = a; // store x coordinate, then
  carry_flag = false; // add 8 pixels and store another to
  adc_imm(0x8); // put them side by side
  ram[Sprite_X_Position + 4 + y] = a;
  lda_zp(0x2); // add eight pixels to the next y
  carry_flag = false; // coordinate
  adc_imm(0x8);
  ram[0x2] = a;
  tya(); // add eight to the offset in Y to
  carry_flag = false; // move to the next two sprites
  adc_imm_fc(0x8);
  tay();
  inx(); // increment offset to return it to the
  inx_fzn(); // routine that called this subroutine
  return;
  // -------------------------------------------------------------------------------------
  // unused space
  // -------------------------------------------------------------------------------------
}

void MoveESprRowOffscreen(void) {
  carry_flag = false; // add A to enemy object OAM data offset
  adc_absx_fc(Enemy_SprDataOffset);
  tay(); // use as offset
  lda_imm_fzn(0xf8);
  DumpTwoSpr(); return; // move first row of sprites offscreen
}

void MoveESprColOffscreen(void) {
  carry_flag = false; // add A to enemy object OAM data offset
  adc_absx_fc(Enemy_SprDataOffset);
  tay_fzn(); // use as offset
  cpu_call_begin(0xebc8); MoveColOffscreen(); cpu_call_end(); // move first and second row sprites in column offscreen
  ram[Sprite_Data + 16 + y] = a; // move third row sprite in column offscreen
  return;
  // -------------------------------------------------------------------------------------
  // $00-$01 - tile numbers
  // $02 - relative Y position
  // $03 - horizontal flip flag (not used here)
  // $04 - attributes
  // $05 - relative X position
}

void DrawBlock(void) {
  lda_abs(Block_Rel_YPos); // get relative vertical coordinate of block object
  ram[0x2] = a; // store here
  lda_abs(Block_Rel_XPos); // get relative horizontal coordinate of block object
  ram[0x5] = a; // store here
  lda_imm(0x3);
  ram[0x4] = a; // set attribute byte here
  lsr_acc_fc();
  ram[0x3] = a; // set horizontal flip bit here (will not be used)
  ldy_absx(Block_SprDataOffset); // get sprite data offset
  ldx_imm(0x0); // reset X for use as offset to tile data
  
DBlkLoop:
  lda_absx(DefaultBlockObjTiles); // get left tile number
  ram[0x0] = a; // set here
  lda_absx_fzn(DefaultBlockObjTiles + 1); // get right tile number
  cpu_call_begin(0xebf1); DrawOneSpriteRow(); cpu_call_end(); // do sub to write tile numbers to first row of sprites
  cpx_imm_fcz(0x4); // check incremented offset
  if (!zero_flag) { goto DBlkLoop; } // and loop back until all four sprites are done
  ldx_zp(ObjectOffset); // get block object offset
  ldy_absx(Block_SprDataOffset); // get sprite data offset
  lda_abs(AreaType);
  cmp_imm_fz(0x1); // check for ground level type area
  // if found, branch to next part
  if (!zero_flag) {
    lda_imm(0x86);
    ram[Sprite_Tilenumber + y] = a; // otherwise remove brick tiles with lines
    ram[Sprite_Tilenumber + 4 + y] = a; // and replace then with lineless brick tiles
  }
  // ChkRep:
  lda_absx(Block_Metatile); // check replacement metatile
  cmp_imm_fcz(0xc4); // if not used block metatile, then
  // branch ahead to use current graphics
  if (zero_flag) {
    lda_imm(0x87); // set A for used block tile
    iny_fzn(); // increment Y to write to tile bytes
    cpu_call_begin(0xec16); DumpFourSpr(); cpu_call_end(); // do sub to dump into all four sprites
    dey(); // return Y to original offset
    lda_imm(0x3); // set palette bits
    ldx_abs(AreaType);
    dex_fz(); // check for ground level type area again
    // if found, use current palette bits
    if (!zero_flag) {
      lsr_acc_fc(); // otherwise set to $01
    }
    // SetBFlip:
    ldx_zp(ObjectOffset); // put block object offset back in X
    ram[Sprite_Attributes + y] = a; // store attribute byte as-is in first sprite
    ora_imm(0b01000000);
    ram[Sprite_Attributes + 4 + y] = a; // set horizontal flip bit for second sprite
    ora_imm(0b10000000);
    ram[Sprite_Attributes + 12 + y] = a; // set both flip bits for fourth sprite
    and_imm(0b10000011);
    ram[Sprite_Attributes + 8 + y] = a; // set vertical flip bit for third sprite
  }
  // BlkOffscr:
  lda_abs(Block_OffscreenBits); // get offscreen bits for block object
  pha(); // save to stack
  and_imm_fz(0b00000100); // check to see if d2 in offscreen bits are set
  // if not set, branch, otherwise move sprites offscreen
  if (!zero_flag) {
    lda_imm(0xf8); // move offscreen two OAMs
    ram[Sprite_Y_Position + 4 + y] = a; // on the right side
    ram[Sprite_Y_Position + 12 + y] = a;
  }
  // PullOfsB:
  pla(); // pull offscreen bits from stack
  ChkLeftCo(); // fallthrough
  return;
}

void ChkLeftCo(void) {
  and_imm_fzn(0b00001000); // check to see if d3 in offscreen bits are set
  if (!zero_flag) {
    MoveColOffscreen(); // fallthrough
    return;
  }
}

void MoveColOffscreen(void) {
  lda_imm_fzn(0xf8); // move offscreen two OAMs
  ram[Sprite_Y_Position + y] = a; // on the left side (or two rows of enemy on either side
  ram[Sprite_Y_Position + 8 + y] = a; // if branched here from enemy graphics handler)
  // ExDBlk:
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used to hold palette bits for attribute byte or relative X position
}

void DrawBrickChunks(void) {
  // DrawBrickChunks:
  lda_imm(0x2); // set palette bits here
  ram[0x0] = a;
  lda_imm(0x75); // set tile number for ball (something residual, likely)
  ldy_zp(GameEngineSubroutine);
  cpy_imm_fcz(0x5); // if end-of-level routine running,
  if (zero_flag) { goto DChunks; } // use palette and tile number assigned
  lda_imm(0x3); // otherwise set different palette bits
  ram[0x0] = a;
  lda_imm(0x84); // and set tile number for brick chunks
  
DChunks:
  ldy_absx(Block_SprDataOffset); // get OAM data offset
  iny_fzn(); // increment to start with tile bytes in OAM
  cpu_call_begin(0xec6b); DumpFourSpr(); cpu_call_end(); // do sub to dump tile number into all four sprites
  lda_zp(FrameCounter); // get frame counter
  asl_acc();
  asl_acc();
  asl_acc(); // move low nybble to high
  asl_acc_fc();
  and_imm(0xc0); // get what was originally d3-d2 of low nybble
  ora_zp(0x0); // add palette bits
  iny_fzn(); // increment offset for attribute bytes
  cpu_call_begin(0xec79); DumpFourSpr(); cpu_call_end(); // do sub to dump attribute data into all four sprites
  dey();
  dey(); // decrement offset to Y coordinate
  lda_abs_fzn(Block_Rel_YPos); // get first block object's relative vertical coordinate
  cpu_call_begin(0xec81); DumpTwoSpr(); cpu_call_end(); // do sub to dump current Y coordinate into two sprites
  lda_abs(Block_Rel_XPos); // get first block object's relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // save into X coordinate of first sprite
  lda_absx(Block_Orig_XPos); // get original horizontal coordinate
  carry_flag = true;
  sbc_abs(ScreenLeft_X_Pos); // subtract coordinate of left side from original coordinate
  ram[0x0] = a; // store result as relative horizontal coordinate of original
  carry_flag = true;
  sbc_abs_fc(Block_Rel_XPos); // get difference of relative positions of original - current
  adc_zp_fc(0x0); // add original relative position to result
  adc_imm(0x6); // plus 6 pixels to position second brick chunk correctly
  ram[Sprite_X_Position + 4 + y] = a; // save into X coordinate of second sprite
  lda_abs(Block_Rel_YPos + 1); // get second block object's relative vertical coordinate
  ram[Sprite_Y_Position + 8 + y] = a;
  ram[Sprite_Y_Position + 12 + y] = a; // dump into Y coordinates of third and fourth sprites
  lda_abs(Block_Rel_XPos + 1); // get second block object's relative horizontal coordinate
  ram[Sprite_X_Position + 8 + y] = a; // save into X coordinate of third sprite
  lda_zp(0x0); // use original relative horizontal position
  carry_flag = true;
  sbc_abs_fc(Block_Rel_XPos + 1); // get difference of relative positions of original - current
  adc_zp_fc(0x0); // add original relative position to result
  adc_imm_fc(0x6); // plus 6 pixels to position fourth brick chunk correctly
  ram[Sprite_X_Position + 12 + y] = a; // save into X coordinate of fourth sprite
  lda_abs_fzn(Block_OffscreenBits); // get offscreen bits for block object
  cpu_call_begin(0xecbd); ChkLeftCo(); cpu_call_end(); // do sub to move left half of sprites offscreen if necessary
  lda_abs(Block_OffscreenBits); // get offscreen bits again
  asl_acc_fc(); // shift d7 into carry
  if (!carry_flag) { goto ChnkOfs; } // if d7 not set, branch to last part
  lda_imm_fzn(0xf8);
  cpu_call_begin(0xecc8); DumpTwoSpr(); cpu_call_end(); // otherwise move top sprites offscreen
  
ChnkOfs:
  lda_zp_fzn(0x0); // if relative position on left side of screen,
  if (!neg_flag) { return; } // go ahead and leave
  lda_absy(Sprite_X_Position); // otherwise compare left-side X coordinate
  cmp_absy_fczn(Sprite_X_Position + 4); // to right-side X coordinate
  if (!carry_flag) { return; } // branch to leave if less
  lda_imm_fzn(0xf8); // otherwise move right half of sprites offscreen
  ram[Sprite_Y_Position + 4 + y] = a;
  ram[Sprite_Y_Position + 12 + y] = a;
  // ExBCDr:
  return; // leave
  // -------------------------------------------------------------------------------------
}

void DrawFirebar(void) {
  lda_zp(FrameCounter); // get frame counter
  lsr_acc(); // divide by four
  lsr_acc();
  pha(); // save result to stack
  and_imm(0x1); // mask out all but last bit
  eor_imm(0x64); // set either tile $64 or $65 as fireball tile
  ram[Sprite_Tilenumber + y] = a; // thus tile changes every four frames
  pla(); // get from stack
  lsr_acc(); // divide by four again
  lsr_acc_fc();
  lda_imm_fzn(0x2); // load value $02 to set palette in attrib byte
  // if last bit shifted out was not set, skip this
  if (carry_flag) {
    ora_imm_fzn(0b11000000); // otherwise flip both ways every eight frames
  }
  // FireA:
  ram[Sprite_Attributes + y] = a; // store attribute byte and leave
  return;
  // -------------------------------------------------------------------------------------
}

void DrawExplosion_Fireworks(void) {
  tax(); // use whatever's in A for offset
  lda_absx(ExplosionTiles); // get tile number using offset
  iny_fzn(); // increment Y (contains sprite data offset)
  cpu_call_begin(0xed1e); DumpFourSpr(); cpu_call_end(); // and dump into tile number part of sprite data
  dey(); // decrement Y so we have the proper offset again
  ldx_zp(ObjectOffset); // return enemy object buffer offset to X
  lda_abs(Fireball_Rel_YPos); // get relative vertical coordinate
  carry_flag = true; // subtract four pixels vertically
  sbc_imm(0x4); // for first and third sprites
  ram[Sprite_Y_Position + y] = a;
  ram[Sprite_Y_Position + 8 + y] = a;
  carry_flag = false; // add eight pixels vertically
  adc_imm(0x8); // for second and fourth sprites
  ram[Sprite_Y_Position + 4 + y] = a;
  ram[Sprite_Y_Position + 12 + y] = a;
  lda_abs(Fireball_Rel_XPos); // get relative horizontal coordinate
  carry_flag = true; // subtract four pixels horizontally
  sbc_imm(0x4); // for first and second sprites
  ram[Sprite_X_Position + y] = a;
  ram[Sprite_X_Position + 4 + y] = a;
  carry_flag = false; // add eight pixels horizontally
  adc_imm_fc(0x8); // for third and fourth sprites
  ram[Sprite_X_Position + 8 + y] = a;
  ram[Sprite_X_Position + 12 + y] = a;
  lda_imm(0x2); // set palette attributes for all sprites, but
  ram[Sprite_Attributes + y] = a; // set no flip at all for first sprite
  lda_imm(0x82);
  ram[Sprite_Attributes + 4 + y] = a; // set vertical flip for second sprite
  lda_imm(0x42);
  ram[Sprite_Attributes + 8 + y] = a; // set horizontal flip for third sprite
  lda_imm_fzn(0xc2);
  ram[Sprite_Attributes + 12 + y] = a; // set both flips for fourth sprite
  return; // we are done
}

void DrawSmallPlatform(void) {
  ldy_absx(Enemy_SprDataOffset); // get OAM data offset
  lda_imm(0x5b); // load tile number for small platforms
  iny_fzn(); // increment offset for tile numbers
  cpu_call_begin(0xed6e); DumpSixSpr(); cpu_call_end(); // dump tile number into all six sprites
  iny(); // increment offset for attributes
  lda_imm_fzn(0x2); // load palette controls
  cpu_call_begin(0xed74); DumpSixSpr(); cpu_call_end(); // dump attributes into all six sprites
  dey(); // decrement for original offset
  dey();
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a;
  ram[Sprite_X_Position + 12 + y] = a; // dump as X coordinate into first and fourth sprites
  carry_flag = false;
  adc_imm(0x8); // add eight pixels
  ram[Sprite_X_Position + 4 + y] = a; // dump into second and fifth sprites
  ram[Sprite_X_Position + 16 + y] = a;
  carry_flag = false;
  adc_imm(0x8); // add eight more pixels
  ram[Sprite_X_Position + 8 + y] = a; // dump into third and sixth sprites
  ram[Sprite_X_Position + 20 + y] = a;
  lda_zpx(Enemy_Y_Position); // get vertical coordinate
  tax();
  pha(); // save to stack
  cpx_imm_fczn(0x20); // if vertical coordinate below status bar,
  // do not mess with it
  if (!carry_flag) {
    lda_imm_fzn(0xf8); // otherwise move first three sprites offscreen
  }
  // TopSP:
  cpu_call_begin(0xed9e); DumpThreeSpr(); cpu_call_end(); // dump vertical coordinate into Y coordinates
  pla(); // pull from stack
  carry_flag = false;
  adc_imm(0x80); // add 128 pixels
  tax();
  cpx_imm_fc(0x20); // if below status bar (taking wrap into account)
  // then do not change altered coordinate
  if (!carry_flag) {
    lda_imm(0xf8); // otherwise move last three sprites offscreen
  }
  // BotSP:
  ram[Sprite_Y_Position + 12 + y] = a; // dump vertical coordinate + 128 pixels
  ram[Sprite_Y_Position + 16 + y] = a; // into Y coordinates
  ram[Sprite_Y_Position + 20 + y] = a;
  lda_abs(Enemy_OffscreenBits); // get offscreen bits
  pha(); // save to stack
  and_imm_fz(0b00001000); // check d3
  if (!zero_flag) {
    lda_imm(0xf8); // if d3 was set, move first and
    ram[Sprite_Y_Position + y] = a; // fourth sprites offscreen
    ram[Sprite_Y_Position + 12 + y] = a;
  }
  // SOfs:
  pla(); // move out and back into stack
  pha();
  and_imm_fz(0b00000100); // check d2
  if (!zero_flag) {
    lda_imm(0xf8); // if d2 was set, move second and
    ram[Sprite_Y_Position + 4 + y] = a; // fifth sprites offscreen
    ram[Sprite_Y_Position + 16 + y] = a;
  }
  // SOfs2:
  pla(); // get from stack
  and_imm_fz(0b00000010); // check d1
  if (!zero_flag) {
    lda_imm(0xf8); // if d1 was set, move third and
    ram[Sprite_Y_Position + 8 + y] = a; // sixth sprites offscreen
    ram[Sprite_Y_Position + 20 + y] = a;
  }
  // ExSPl:
  ldx_zp_fzn(ObjectOffset); // get enemy object offset and leave
  return;
  // -------------------------------------------------------------------------------------
}

void DrawBubble(void) {
  // DrawBubble:
  ldy_zp(Player_Y_HighPos); // if player's vertical high position
  dey_fzn(); // not within screen, skip all of this
  if (!zero_flag) { return; }
  lda_abs(Bubble_OffscreenBits); // check air bubble's offscreen bits
  and_imm_fzn(0b00001000);
  if (!zero_flag) { return; } // if bit set, branch to leave
  ldy_absx(Bubble_SprDataOffset); // get air bubble's OAM data offset
  lda_abs(Bubble_Rel_XPos); // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X coordinate here
  lda_abs(Bubble_Rel_YPos); // get relative vertical coordinate
  ram[Sprite_Y_Position + y] = a; // store as Y coordinate here
  lda_imm(0x74);
  ram[Sprite_Tilenumber + y] = a; // put air bubble tile into OAM data
  lda_imm_fzn(0x2);
  ram[Sprite_Attributes + y] = a; // set attribute byte
  // ExDBub:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00 - used to store player's vertical offscreen bits
}

void PlayerGfxHandler(void) {
  // PlayerGfxHandler:
  lda_abs_fz(InjuryTimer); // if player's injured invincibility timer
  if (zero_flag) { goto CntPl; } // not set, skip checkpoint and continue code
  lda_zp(FrameCounter);
  lsr_acc_fczn(); // otherwise check frame counter and branch
  if (carry_flag) { return; } // to leave on every other frame (when d0 is set)
  
CntPl:
  lda_zp(GameEngineSubroutine); // if executing specific game engine routine,
  cmp_imm_fcz(0xb); // branch ahead to some other part
  if (zero_flag) { goto PlayerKilled; }
  lda_abs_fzn(PlayerChangeSizeFlag); // if grow/shrink flag set
  if (!zero_flag) { goto DoChangeSize; } // then branch to some other code
  ldy_abs_fzn(SwimmingFlag); // if swimming flag set, branch to
  if (zero_flag) { FindPlayerAction(); return; } // different part, do not return
  lda_zp(Player_State);
  cmp_imm_fczn(0x0); // if player status normal,
  if (zero_flag) { FindPlayerAction(); return; } // branch and do not return
  cpu_call_begin(0xef0b); FindPlayerAction(); cpu_call_end(); // otherwise jump and return
  lda_zp(FrameCounter);
  and_imm_fzn(0b00000100); // check frame counter for d2 set (8 frames every
  if (!zero_flag) { return; } // eighth frame), and branch if set to leave
  tax(); // initialize X to zero
  ldy_abs(Player_SprDataOffset); // get player sprite data offset
  lda_zp(PlayerFacingDir); // get player's facing direction
  lsr_acc_fc();
  if (carry_flag) { goto SwimKT; } // if player facing to the right, use current offset
  iny();
  iny(); // otherwise move to next OAM data
  iny();
  iny();
  
SwimKT:
  lda_abs_fz(PlayerSize); // check player's size
  if (zero_flag) { goto BigKTS; } // if big, use first tile
  lda_absy(Sprite_Tilenumber + 24); // check tile number of seventh/eighth sprite
  cmp_abs_fczn(SwimTileRepOffset); // against tile number in player graphics table
  if (zero_flag) { return; } // if spr7/spr8 tile number = value, branch to leave
  inx(); // otherwise increment X for second tile
  
BigKTS:
  lda_absx_fzn(SwimKickTileNum); // overwrite tile number in sprite 7/8
  ram[Sprite_Tilenumber + 24 + y] = a; // to animate player's feet when swimming
  // ExPGH:
  return; // then leave
  
DoChangeSize:
  cpu_call_begin(0xef3c); HandleChangeSize(); cpu_call_end(); // find proper offset to graphics table for grow/shrink
  goto PlayerGfxProcessing; // draw player, then process for fireball throwing
  
PlayerKilled:
  ldy_imm(0xe); // load offset for player killed
  lda_absy(PlayerGfxTblOffsets); // get offset to graphics table
  
PlayerGfxProcessing:
  ram[PlayerGfxOffset] = a; // store offset to graphics table here
  lda_imm_fzn(0x4);
  cpu_call_begin(0xef4c); RenderPlayerSub(); cpu_call_end(); // draw player based on offset loaded
  cpu_call_begin(0xef4f); ChkForPlayerAttrib(); cpu_call_end(); // set horizontal flip bits as necessary
  lda_abs_fz(FireballThrowingTimer);
  if (zero_flag) { goto PlayerOffscreenChk; } // if fireball throw timer not set, skip to the end
  ldy_imm(0x0); // set value to initialize by default
  lda_abs(PlayerAnimTimer); // get animation frame timer
  cmp_abs_fc(FireballThrowingTimer); // compare to fireball throw timer
  ram[FireballThrowingTimer] = y; // initialize fireball throw timer
  if (carry_flag) { goto PlayerOffscreenChk; } // if animation frame timer => fireball throw timer skip to end
  ram[FireballThrowingTimer] = a; // otherwise store animation timer into fireball throw timer
  ldy_imm(0x7); // load offset for throwing
  lda_absy(PlayerGfxTblOffsets); // get offset to graphics table
  ram[PlayerGfxOffset] = a; // store it for use later
  ldy_imm(0x4); // set to update four sprite rows by default
  lda_zp(Player_X_Speed);
  ora_zp_fz(Left_Right_Buttons); // check for horizontal speed or left/right button press
  if (zero_flag) { goto SUpdR; } // if no speed or button press, branch using set value in Y
  dey(); // otherwise set to update only three sprite rows
  
SUpdR:
  tya_fzn(); // save in A for use
  cpu_call_begin(0xef79); RenderPlayerSub(); cpu_call_end(); // in sub, draw player object again
  
PlayerOffscreenChk:
  lda_abs(Player_OffscreenBits); // get player's offscreen bits
  lsr_acc();
  lsr_acc(); // move vertical bits to low nybble
  lsr_acc();
  lsr_acc();
  ram[0x0] = a; // store here
  ldx_imm(0x3); // check all four rows of player sprites
  lda_abs(Player_SprDataOffset); // get player's sprite data offset
  carry_flag = false;
  adc_imm(0x18); // add 24 bytes to start at bottom row
  tay(); // set as offset here
  
PROfsLoop:
  lda_imm(0xf8); // load offscreen Y coordinate just in case
  lsr_zp_fczn(0x0); // shift bit into carry
  if (!carry_flag) { goto NPROffscr; } // if bit not set, skip, do not move sprites
  cpu_call_begin(0xef94); DumpTwoSpr(); cpu_call_end(); // otherwise dump offscreen Y coordinate into sprite data
  
NPROffscr:
  tya();
  carry_flag = true; // subtract eight bytes to do
  sbc_imm_fc(0x8); // next row up
  tay();
  dex_fzn(); // decrement row counter
  if (!neg_flag) { goto PROfsLoop; } // do this until all sprite rows are checked
  return; // then we are done!
  // -------------------------------------------------------------------------------------
}

void FindPlayerAction(void) {
  cpu_call_begin(0xef36); ProcessPlayerAction(); cpu_call_end(); // find proper offset to graphics table by player's actions
  goto PlayerGfxProcessing; // draw player, then process for fireball throwing
  
PlayerGfxProcessing:
  ram[PlayerGfxOffset] = a; // store offset to graphics table here
  lda_imm_fzn(0x4);
  cpu_call_begin(0xef4c); RenderPlayerSub(); cpu_call_end(); // draw player based on offset loaded
  cpu_call_begin(0xef4f); ChkForPlayerAttrib(); cpu_call_end(); // set horizontal flip bits as necessary
  lda_abs_fz(FireballThrowingTimer);
  // if fireball throw timer not set, skip to the end
  if (!zero_flag) {
    ldy_imm(0x0); // set value to initialize by default
    lda_abs(PlayerAnimTimer); // get animation frame timer
    cmp_abs_fc(FireballThrowingTimer); // compare to fireball throw timer
    ram[FireballThrowingTimer] = y; // initialize fireball throw timer
    // if animation frame timer => fireball throw timer skip to end
    if (!carry_flag) {
      ram[FireballThrowingTimer] = a; // otherwise store animation timer into fireball throw timer
      ldy_imm(0x7); // load offset for throwing
      lda_absy(PlayerGfxTblOffsets); // get offset to graphics table
      ram[PlayerGfxOffset] = a; // store it for use later
      ldy_imm(0x4); // set to update four sprite rows by default
      lda_zp(Player_X_Speed);
      ora_zp_fz(Left_Right_Buttons); // check for horizontal speed or left/right button press
      // if no speed or button press, branch using set value in Y
      if (!zero_flag) {
        dey(); // otherwise set to update only three sprite rows
      }
      // SUpdR:
      tya_fzn(); // save in A for use
      cpu_call_begin(0xef79); RenderPlayerSub(); cpu_call_end(); // in sub, draw player object again
    }
  }
  // PlayerOffscreenChk:
  lda_abs(Player_OffscreenBits); // get player's offscreen bits
  lsr_acc();
  lsr_acc(); // move vertical bits to low nybble
  lsr_acc();
  lsr_acc();
  ram[0x0] = a; // store here
  ldx_imm(0x3); // check all four rows of player sprites
  lda_abs(Player_SprDataOffset); // get player's sprite data offset
  carry_flag = false;
  adc_imm(0x18); // add 24 bytes to start at bottom row
  tay(); // set as offset here
  
PROfsLoop:
  lda_imm(0xf8); // load offscreen Y coordinate just in case
  lsr_zp_fczn(0x0); // shift bit into carry
  // if bit not set, skip, do not move sprites
  if (carry_flag) {
    cpu_call_begin(0xef94); DumpTwoSpr(); cpu_call_end(); // otherwise dump offscreen Y coordinate into sprite data
  }
  // NPROffscr:
  tya();
  carry_flag = true; // subtract eight bytes to do
  sbc_imm_fc(0x8); // next row up
  tay();
  dex_fzn(); // decrement row counter
  if (!neg_flag) { goto PROfsLoop; } // do this until all sprite rows are checked
  return; // then we are done!
  // -------------------------------------------------------------------------------------
}

void DrawPlayer_Intermediate(void) {
  ldx_imm(0x5); // store data into zero page memory
  
PIntLoop:
  lda_absx(IntermediatePlayerData); // load data to display player as he always
  ram[0x2 + x] = a; // appears on world/lives display
  dex_fn();
  if (!neg_flag) { goto PIntLoop; } // do this until all data is loaded
  ldx_imm(0xb8); // load offset for small standing
  ldy_imm_fzn(0x4); // load sprite data offset
  cpu_call_begin(0xefb4); DrawPlayerLoop(); cpu_call_end(); // draw player accordingly
  lda_abs(Sprite_Attributes + 36); // get empty sprite attributes
  ora_imm_fzn(0b01000000); // set horizontal flip bit for bottom-right sprite
  ram[Sprite_Attributes + 32] = a; // store and leave
  return;
  // -------------------------------------------------------------------------------------
  // $00-$01 - used to hold tile numbers, $00 also used to hold upper extent of animation frames
  // $02 - vertical position
  // $03 - facing direction, used as horizontal flip control
  // $04 - attributes
  // $05 - horizontal position
  // $07 - number of rows to draw
  // these also used in IntermediatePlayerData
}

void RenderPlayerSub(void) {
  ram[0x7] = a; // store number of rows of sprites to draw
  lda_abs(Player_Rel_XPos);
  ram[Player_Pos_ForScroll] = a; // store player's relative horizontal position
  ram[0x5] = a; // store it here also
  lda_abs(Player_Rel_YPos);
  ram[0x2] = a; // store player's vertical position
  lda_zp(PlayerFacingDir);
  ram[0x3] = a; // store player's facing direction
  lda_abs(Player_SprAttrib);
  ram[0x4] = a; // store player's sprite attributes
  ldx_abs(PlayerGfxOffset); // load graphics table offset
  ldy_abs(Player_SprDataOffset); // get player's sprite data offset
  DrawPlayerLoop(); // fallthrough
  return;
}

void DrawPlayerLoop(void) {
  
DrawPlayerLoop:
  lda_absx(PlayerGraphicsTable); // load player's left side
  ram[0x0] = a;
  lda_absx_fzn(PlayerGraphicsTable + 1); // now load right side
  cpu_call_begin(0xefe6); DrawOneSpriteRow(); cpu_call_end();
  dec_zp_fzn(0x7); // decrement rows of sprites to draw
  if (!zero_flag) { goto DrawPlayerLoop; } // do this until all rows are drawn
  return;
}

void ProcessPlayerAction(void) {
  // ProcessPlayerAction:
  lda_zp(Player_State); // get player's state
  cmp_imm_fcz(0x3);
  if (zero_flag) { goto ActionClimbing; } // if climbing, branch here
  cmp_imm_fcz(0x2);
  if (zero_flag) { goto ActionFalling; } // if falling, branch here
  cmp_imm_fcz(0x1);
  if (!zero_flag) { goto ProcOnGroundActs; } // if not jumping, branch here
  lda_abs_fz(SwimmingFlag);
  if (!zero_flag) { goto ActionSwimming; } // if swimming flag set, branch elsewhere
  ldy_imm(0x6); // load offset for crouching
  lda_abs_fzn(CrouchingFlag); // get crouching flag
  if (!zero_flag) { goto NonAnimatedActs; } // if set, branch to get offset for graphics table
  ldy_imm_fzn(0x0); // otherwise load offset for jumping
  goto NonAnimatedActs; // go to get offset to graphics table
  
ProcOnGroundActs:
  ldy_imm(0x6); // load offset for crouching
  lda_abs_fzn(CrouchingFlag); // get crouching flag
  if (!zero_flag) { goto NonAnimatedActs; } // if set, branch to get offset for graphics table
  ldy_imm(0x2); // load offset for standing
  lda_zp(Player_X_Speed); // check player's horizontal speed
  ora_zp_fzn(Left_Right_Buttons); // and left/right controller bits
  if (zero_flag) { goto NonAnimatedActs; } // if no speed or buttons pressed, use standing offset
  lda_abs(Player_XSpeedAbsolute); // load walking/running speed
  cmp_imm_fc(0x9);
  if (!carry_flag) { goto ActionWalkRun; } // if less than a certain amount, branch, too slow to skid
  lda_zp(Player_MovingDir); // otherwise check to see if moving direction
  and_zp_fz(PlayerFacingDir); // and facing direction are the same
  if (!zero_flag) { goto ActionWalkRun; } // if moving direction = facing direction, branch, don't skid
  iny_fzn(); // otherwise increment to skid offset ($03)
  
NonAnimatedActs:
  cpu_call_begin(0xf02a); GetGfxOffsetAdder(); cpu_call_end(); // do a sub here to get offset adder for graphics table
  lda_imm(0x0);
  ram[PlayerAnimCtrl] = a; // initialize animation frame control
  lda_absy_fzn(PlayerGfxTblOffsets); // load offset to graphics table using size as offset
  return;
  
ActionFalling:
  ldy_imm_fzn(0x4); // load offset for walking/running
  cpu_call_begin(0xf038); GetGfxOffsetAdder(); cpu_call_end(); // get offset to graphics table
  GetCurrentAnimOffset(); return; // execute instructions for falling state
  
ActionWalkRun:
  ldy_imm_fzn(0x4); // load offset for walking/running
  cpu_call_begin(0xf040); GetGfxOffsetAdder(); cpu_call_end(); // get offset to graphics table
  goto FourFrameExtent; // execute instructions for normal state
  
ActionClimbing:
  ldy_imm(0x5); // load offset for climbing
  lda_zp_fzn(Player_Y_Speed); // check player's vertical speed
  if (zero_flag) { goto NonAnimatedActs; } // if no speed, branch, use offset as-is
  cpu_call_begin(0xf04c); GetGfxOffsetAdder(); cpu_call_end(); // otherwise get offset for graphics table
  goto ThreeFrameExtent; // then skip ahead to more code
  
ActionSwimming:
  ldy_imm_fzn(0x1); // load offset for swimming
  cpu_call_begin(0xf054); GetGfxOffsetAdder(); cpu_call_end();
  lda_abs(JumpSwimTimer); // check jump/swim timer
  ora_abs_fz(PlayerAnimCtrl); // and animation frame control
  if (!zero_flag) { goto FourFrameExtent; } // if any one of these set, branch ahead
  lda_zp(A_B_Buttons);
  asl_acc_fc(); // check for A button pressed
  if (carry_flag) { goto FourFrameExtent; } // branch to same place if A button pressed
  GetCurrentAnimOffset(); // fallthrough
  return;
  
FourFrameExtent:
  lda_imm_fzn(0x3); // load upper extent for frame control
  goto AnimationControl; // jump to get offset and animate player object
  
ThreeFrameExtent:
  lda_imm_fzn(0x2); // load upper extent for frame control for climbing
  
AnimationControl:
  ram[0x0] = a; // store upper extent here
  cpu_call_begin(0xf073); GetCurrentAnimOffset(); cpu_call_end(); // get proper offset to graphics table
  pha(); // save offset to stack
  lda_abs_fz(PlayerAnimTimer); // load animation frame timer
  if (!zero_flag) { goto ExAnimC; } // branch if not expired
  lda_abs(PlayerAnimTimerSet); // get animation frame timer amount
  ram[PlayerAnimTimer] = a; // and set timer accordingly
  lda_abs(PlayerAnimCtrl);
  carry_flag = false; // add one to animation frame control
  adc_imm(0x1);
  cmp_zp_fc(0x0); // compare to upper extent
  if (!carry_flag) { goto SetAnimC; } // if frame control + 1 < upper extent, use as next
  lda_imm(0x0); // otherwise initialize frame control
  
SetAnimC:
  ram[PlayerAnimCtrl] = a; // store as new animation frame control
  
ExAnimC:
  pla_fzn(); // get offset to graphics table from stack and leave
  return;
}

void GetCurrentAnimOffset(void) {
  lda_abs(PlayerAnimCtrl); // get animation frame control
  goto GetOffsetFromAnimCtrl; // jump to get proper offset to graphics table
  
GetOffsetFromAnimCtrl:
  asl_acc(); // multiply animation frame control
  asl_acc(); // by eight to get proper amount
  asl_acc_fc(); // to add to our offset
  adc_absy_fczn(PlayerGfxTblOffsets); // add to offset to graphics table
  return; // and return with result in A
}

void GetGfxOffsetAdder(void) {
  lda_abs_fzn(PlayerSize); // get player's size
  if (!zero_flag) {
    tya(); // for big player
    carry_flag = false; // otherwise add eight bytes to offset
    adc_imm_fc(0x8); // for small player
    tay_fzn();
    // SzOfs:
    return; // go back
  }
}

void HandleChangeSize(void) {
  ldy_abs(PlayerAnimCtrl); // get animation frame control
  lda_zp(FrameCounter);
  and_imm_fz(0b00000011); // get frame counter and execute this code every
  // fourth frame, otherwise branch ahead
  if (zero_flag) {
    iny(); // increment frame control
    cpy_imm_fc(0xa); // check for preset upper extent
    // if not there yet, skip ahead to use
    if (carry_flag) {
      ldy_imm(0x0); // otherwise initialize both grow/shrink flag
      ram[PlayerChangeSizeFlag] = y; // and animation frame control
    }
    // CSzNext:
    ram[PlayerAnimCtrl] = y; // store proper frame control
  }
  // GorSLog:
  lda_abs_fz(PlayerSize); // get player's size
  // if player small, skip ahead to next part
  if (zero_flag) {
    lda_absy(ChangeSizeOffsetAdder); // get offset adder based on frame control as offset
    ldy_imm(0xf); // load offset for player growing
    // GetOffsetFromAnimCtrl:
    asl_acc(); // multiply animation frame control
    asl_acc(); // by eight to get proper amount
    asl_acc_fc(); // to add to our offset
    adc_absy_fczn(PlayerGfxTblOffsets); // add to offset to graphics table
    return; // and return with result in A
  }
  // ShrinkPlayer:
  tya(); // add ten bytes to frame control as offset
  carry_flag = false;
  adc_imm_fc(0xa); // this thing apparently uses two of the swimming frames
  tax(); // to draw the player shrinking
  ldy_imm(0x9); // load offset for small player swimming
  lda_absx_fz(ChangeSizeOffsetAdder); // get what would normally be offset adder
  // and branch to use offset if nonzero
  if (zero_flag) {
    ldy_imm(0x1); // otherwise load offset for big player swimming
  }
  // ShrPlF:
  lda_absy_fzn(PlayerGfxTblOffsets); // get offset to graphics table based on offset loaded
  return; // and leave
}

void ChkForPlayerAttrib(void) {
  // ChkForPlayerAttrib:
  ldy_abs(Player_SprDataOffset); // get sprite data offset
  lda_zp(GameEngineSubroutine);
  cmp_imm_fcz(0xb); // if executing specific game engine routine,
  if (zero_flag) { goto KilledAtt; } // branch to change third and fourth row OAM attributes
  lda_abs(PlayerGfxOffset); // get graphics table offset
  cmp_imm_fcz(0x50);
  if (zero_flag) { goto C_S_IGAtt; } // if crouch offset, either standing offset,
  cmp_imm_fcz(0xb8); // or intermediate growing offset,
  if (zero_flag) { goto C_S_IGAtt; } // go ahead and execute code to change 
  cmp_imm_fcz(0xc0); // fourth row OAM attributes only
  if (zero_flag) { goto C_S_IGAtt; }
  cmp_imm_fczn(0xc8);
  if (!zero_flag) { return; } // if none of these, branch to leave
  
KilledAtt:
  lda_absy(Sprite_Attributes + 16);
  and_imm(0b00111111); // mask out horizontal and vertical flip bits
  ram[Sprite_Attributes + 16 + y] = a; // for third row sprites and save
  lda_absy(Sprite_Attributes + 20);
  and_imm(0b00111111);
  ora_imm(0b01000000); // set horizontal flip bit for second
  ram[Sprite_Attributes + 20 + y] = a; // sprite in the third row
  
C_S_IGAtt:
  lda_absy(Sprite_Attributes + 24);
  and_imm(0b00111111); // mask out horizontal and vertical flip bits
  ram[Sprite_Attributes + 24 + y] = a; // for fourth row sprites and save
  lda_absy(Sprite_Attributes + 28);
  and_imm(0b00111111);
  ora_imm_fzn(0b01000000); // set horizontal flip bit for second
  ram[Sprite_Attributes + 28 + y] = a; // sprite in the fourth row
  // ExPlyrAt:
  return; // leave
  // -------------------------------------------------------------------------------------
  // $00 - used in adding to get proper offset
}

void RelativePlayerPosition(void) {
  ldx_imm(0x0); // set offsets for relative cooordinates
  ldy_imm_fzn(0x0); // routine to correspond to player object
  goto RelWOfs; // get the coordinates
  
RelWOfs:
  cpu_call_begin(0xf144); GetObjRelativePosition(); cpu_call_end(); // get the coordinates
  ldx_zp_fzn(ObjectOffset); // return original offset
  return; // leave
}

void RelativeBubblePosition(void) {
  ldy_imm_fzn(0x1); // set for air bubble offsets
  cpu_call_begin(0xf135); GetProperObjOffset(); cpu_call_end(); // modify X to get proper air bubble offset
  ldy_imm_fzn(0x3);
  goto RelWOfs; // get the coordinates
  
RelWOfs:
  cpu_call_begin(0xf144); GetObjRelativePosition(); cpu_call_end(); // get the coordinates
  ldx_zp_fzn(ObjectOffset); // return original offset
  return; // leave
}

void RelativeFireballPosition(void) {
  ldy_imm_fzn(0x0); // set for fireball offsets
  cpu_call_begin(0xf13f); GetProperObjOffset(); cpu_call_end(); // modify X to get proper fireball offset
  ldy_imm_fzn(0x2);
  // RelWOfs:
  cpu_call_begin(0xf144); GetObjRelativePosition(); cpu_call_end(); // get the coordinates
  ldx_zp_fzn(ObjectOffset); // return original offset
  return; // leave
}

void RelativeMiscPosition(void) {
  goto RelativeMiscPosition;
  
RelWOfs:
  cpu_call_begin(0xf144); GetObjRelativePosition(); cpu_call_end(); // get the coordinates
  ldx_zp_fzn(ObjectOffset); // return original offset
  return; // leave
  
RelativeMiscPosition:
  ldy_imm_fzn(0x2); // set for misc object offsets
  cpu_call_begin(0xf14c); GetProperObjOffset(); cpu_call_end(); // modify X to get proper misc object offset
  ldy_imm_fzn(0x6);
  goto RelWOfs; // get the coordinates
}

void RelativeEnemyPosition(void) {
  lda_imm(0x1); // get coordinates of enemy object 
  ldy_imm(0x1); // relative to the screen
  VariableObjOfsRelPos(); return;
}

void RelativeBlockPosition(void) {
  lda_imm(0x9); // get coordinates of one block object
  ldy_imm_fzn(0x4); // relative to the screen
  cpu_call_begin(0xf15f); VariableObjOfsRelPos(); cpu_call_end();
  inx(); // adjust offset for other block object if any
  inx();
  lda_imm(0x9);
  iny(); // adjust other and get coordinates for other one
  VariableObjOfsRelPos(); // fallthrough
  return;
}

void VariableObjOfsRelPos(void) {
  ram[0x0] = x; // store value to add to A here
  carry_flag = false;
  adc_zp_fc(0x0); // add A to value stored
  tax_fzn(); // use as enemy offset
  cpu_call_begin(0xf16d); GetObjRelativePosition(); cpu_call_end();
  ldx_zp_fzn(ObjectOffset); // reload old object offset and leave
  return;
}

void GetObjRelativePosition(void) {
  lda_zpx(SprObject_Y_Position); // load vertical coordinate low
  ram[SprObject_Rel_YPos + y] = a; // store here
  lda_zpx(SprObject_X_Position); // load horizontal coordinate
  carry_flag = true; // subtract left edge coordinate
  sbc_abs_fczn(ScreenLeft_X_Pos);
  ram[SprObject_Rel_XPos + y] = a; // store result here
  return;
  // -------------------------------------------------------------------------------------
  // $00 - used as temp variable to hold offscreen bits
}

void GetPlayerOffscreenBits(void) {
  ldx_imm(0x0); // set offsets for player-specific variables
  ldy_imm(0x0); // and get offscreen information about player
  goto GetOffScreenBitsSet;
  
GetOffScreenBitsSet:
  tya_fzn(); // save offscreen bits offset to stack for now
  pha();
  cpu_call_begin(0xf1c4); RunOffscrBitsSubs(); cpu_call_end();
  asl_acc(); // move low nybble to high nybble
  asl_acc();
  asl_acc();
  asl_acc_fc();
  ora_zp(0x0); // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  pla(); // get offscreen bits offset from stack
  tay();
  lda_zp(0x0); // get value here and store elsewhere
  ram[SprObject_OffscrBits + y] = a;
  ldx_zp_fzn(ObjectOffset);
  return;
}

void GetFireballOffscreenBits(void) {
  ldy_imm_fzn(0x0); // set for fireball offsets
  cpu_call_begin(0xf18b); GetProperObjOffset(); cpu_call_end(); // modify X to get proper fireball offset
  ldy_imm(0x2); // set other offset for fireball's offscreen bits
  goto GetOffScreenBitsSet; // and get offscreen information about fireball
  
GetOffScreenBitsSet:
  tya_fzn(); // save offscreen bits offset to stack for now
  pha();
  cpu_call_begin(0xf1c4); RunOffscrBitsSubs(); cpu_call_end();
  asl_acc(); // move low nybble to high nybble
  asl_acc();
  asl_acc();
  asl_acc_fc();
  ora_zp(0x0); // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  pla(); // get offscreen bits offset from stack
  tay();
  lda_zp(0x0); // get value here and store elsewhere
  ram[SprObject_OffscrBits + y] = a;
  ldx_zp_fzn(ObjectOffset);
  return;
}

void GetBubbleOffscreenBits(void) {
  ldy_imm_fzn(0x1); // set for air bubble offsets
  cpu_call_begin(0xf195); GetProperObjOffset(); cpu_call_end(); // modify X to get proper air bubble offset
  ldy_imm(0x3); // set other offset for airbubble's offscreen bits
  goto GetOffScreenBitsSet; // and get offscreen information about air bubble
  
GetOffScreenBitsSet:
  tya_fzn(); // save offscreen bits offset to stack for now
  pha();
  cpu_call_begin(0xf1c4); RunOffscrBitsSubs(); cpu_call_end();
  asl_acc(); // move low nybble to high nybble
  asl_acc();
  asl_acc();
  asl_acc_fc();
  ora_zp(0x0); // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  pla(); // get offscreen bits offset from stack
  tay();
  lda_zp(0x0); // get value here and store elsewhere
  ram[SprObject_OffscrBits + y] = a;
  ldx_zp_fzn(ObjectOffset);
  return;
}

void GetMiscOffscreenBits(void) {
  ldy_imm_fzn(0x2); // set for misc object offsets
  cpu_call_begin(0xf19f); GetProperObjOffset(); cpu_call_end(); // modify X to get proper misc object offset
  ldy_imm(0x6); // set other offset for misc object's offscreen bits
  goto GetOffScreenBitsSet; // and get offscreen information about misc object
  
GetOffScreenBitsSet:
  tya_fzn(); // save offscreen bits offset to stack for now
  pha();
  cpu_call_begin(0xf1c4); RunOffscrBitsSubs(); cpu_call_end();
  asl_acc(); // move low nybble to high nybble
  asl_acc();
  asl_acc();
  asl_acc_fc();
  ora_zp(0x0); // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  pla(); // get offscreen bits offset from stack
  tay();
  lda_zp(0x0); // get value here and store elsewhere
  ram[SprObject_OffscrBits + y] = a;
  ldx_zp_fzn(ObjectOffset);
  return;
}

void GetProperObjOffset(void) {
  txa(); // move offset to A
  carry_flag = false;
  adc_absy_fc(ObjOffsetData); // add amount of bytes to offset depending on setting in Y
  tax_fzn(); // put back in X and leave
  return;
}

void GetEnemyOffscreenBits(void) {
  lda_imm(0x1); // set A to add 1 byte in order to get enemy offset
  ldy_imm(0x1); // set Y to put offscreen bits in Enemy_OffscreenBits
  goto SetOffscrBitsOffset;
  
SetOffscrBitsOffset:
  ram[0x0] = x;
  carry_flag = false; // add contents of X to A to get
  adc_zp_fc(0x0); // appropriate offset, then give back to X
  tax();
  // GetOffScreenBitsSet:
  tya_fzn(); // save offscreen bits offset to stack for now
  pha();
  cpu_call_begin(0xf1c4); RunOffscrBitsSubs(); cpu_call_end();
  asl_acc(); // move low nybble to high nybble
  asl_acc();
  asl_acc();
  asl_acc_fc();
  ora_zp(0x0); // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  pla(); // get offscreen bits offset from stack
  tay();
  lda_zp(0x0); // get value here and store elsewhere
  ram[SprObject_OffscrBits + y] = a;
  ldx_zp_fzn(ObjectOffset);
  return;
}

void GetBlockOffscreenBits(void) {
  lda_imm(0x9); // set A to add 9 bytes in order to get block obj offset
  ldy_imm(0x4); // set Y to put offscreen bits in Block_OffscreenBits
  // SetOffscrBitsOffset:
  ram[0x0] = x;
  carry_flag = false; // add contents of X to A to get
  adc_zp_fc(0x0); // appropriate offset, then give back to X
  tax();
  // GetOffScreenBitsSet:
  tya_fzn(); // save offscreen bits offset to stack for now
  pha();
  cpu_call_begin(0xf1c4); RunOffscrBitsSubs(); cpu_call_end();
  asl_acc(); // move low nybble to high nybble
  asl_acc();
  asl_acc();
  asl_acc_fc();
  ora_zp(0x0); // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  pla(); // get offscreen bits offset from stack
  tay();
  lda_zp(0x0); // get value here and store elsewhere
  ram[SprObject_OffscrBits + y] = a;
  ldx_zp_fzn(ObjectOffset);
  return;
}

void RunOffscrBitsSubs(void) {
  cpu_call_begin(0xf1d9); GetXOffscreenBits(); cpu_call_end(); // do subroutine here
  lsr_acc(); // move high nybble to low
  lsr_acc();
  lsr_acc();
  lsr_acc();
  ram[0x0] = a; // store here
  goto GetYOffscreenBits;
  // --------------------------------
  // (these apply to these three subsections)
  // $04 - used to store proper offset
  // $05 - used as adder in DividePDiff
  // $06 - used to store preset value used to compare to pixel difference in $07
  // $07 - used to store difference between coordinates of object and screen edges
  
GetYOffscreenBits:
  ram[0x4] = x; // save position in buffer to here
  ldy_imm(0x1); // start with top of screen
  
YOfsLoop:
  lda_absy(HighPosUnitData); // load coordinate for edge of vertical unit
  carry_flag = true;
  sbc_zpx_fc(SprObject_Y_Position); // subtract from vertical coordinate of object
  ram[0x7] = a; // store here
  lda_imm(0x1); // subtract one from vertical high byte of object
  sbc_zpx(SprObject_Y_HighPos);
  ldx_absy(DefaultYOnscreenOfs); // load offset value here
  cmp_imm_fn(0x0);
  // if under top of the screen or beyond bottom, branch
  if (!neg_flag) {
    ldx_absy(DefaultYOnscreenOfs + 1); // if not, load alternate offset value here
    cmp_imm_fcn(0x1);
    // if one vertical unit or more above the screen, branch
    if (neg_flag) {
      lda_imm(0x20); // if no branching, load value here and store
      ram[0x6] = a;
      lda_imm_fzn(0x4); // load some other value and execute subroutine
      cpu_call_begin(0xf25f); DividePDiff(); cpu_call_end();
    }
  }
  // YLdBData:
  lda_absx(YOffscreenBitsData); // get offscreen data bits using offset
  ldx_zp(0x4); // reobtain position in buffer
  cmp_imm_fczn(0x0);
  if (zero_flag) {
    dey_fzn(); // otherwise, do bottom of the screen now
    if (!neg_flag) { goto YOfsLoop; }
    // ExYOfsBS:
    return;
    // --------------------------------
  }
}

void GetXOffscreenBits(void) {
  ram[0x4] = x; // save position in buffer to here
  ldy_imm(0x1); // start with right side of screen
  
XOfsLoop:
  lda_absy(ScreenEdge_X_Pos); // get pixel coordinate of edge
  carry_flag = true; // get difference between pixel coordinate of edge
  sbc_zpx_fc(SprObject_X_Position); // and pixel coordinate of object position
  ram[0x7] = a; // store here
  lda_absy(ScreenEdge_PageLoc); // get page location of edge
  sbc_zpx(SprObject_PageLoc); // subtract from page location of object position
  ldx_absy(DefaultXOnscreenOfs); // load offset value here
  cmp_imm_fn(0x0);
  // if beyond right edge or in front of left edge, branch
  if (!neg_flag) {
    ldx_absy(DefaultXOnscreenOfs + 1); // if not, load alternate offset value here
    cmp_imm_fcn(0x1);
    // if one page or more to the left of either edge, branch
    if (neg_flag) {
      lda_imm(0x38); // if no branching, load value here and store
      ram[0x6] = a;
      lda_imm_fzn(0x8); // load some other value and execute subroutine
      cpu_call_begin(0xf21d); DividePDiff(); cpu_call_end();
    }
  }
  // XLdBData:
  lda_absx(XOffscreenBitsData); // get bits here
  ldx_zp(0x4); // reobtain position in buffer
  cmp_imm_fczn(0x0); // if bits not zero, branch to leave
  if (zero_flag) {
    dey_fzn(); // otherwise, do left side of screen now
    if (!neg_flag) { goto XOfsLoop; } // branch if not already done with left side
    // ExXOfsBS:
    return;
    // --------------------------------
  }
}

void DividePDiff(void) {
  ram[0x5] = a; // store current value in A here
  lda_zp(0x7); // get pixel difference
  cmp_zp_fczn(0x6); // compare to preset value
  if (!carry_flag) {
    lsr_acc(); // divide by eight
    lsr_acc();
    lsr_acc();
    and_imm(0x7); // mask out all but 3 LSB
    cpy_imm_fc(0x1); // right side of the screen or top?
    // if so, branch, use difference / 8 as offset
    if (!carry_flag) {
      adc_zp_fc(0x5); // if not, add value to difference / 8
    }
    // SetOscrO:
    tax_fzn(); // use as offset
    // ExDivPD:
    return; // leave
    // -------------------------------------------------------------------------------------
    // $00-$01 - tile numbers
    // $02 - Y coordinate
    // $03 - flip control
    // $04 - sprite attributes
    // $05 - X coordinate
  }
}

void SoundEngine(void) {
  // SoundEngine:
  lda_abs_fzn(OperMode); // are we in title screen mode?
  if (!zero_flag) { goto SndOn; }
  apu_write(SND_MASTERCTRL_REG, a); // if so, disable sound and leave
  return;
  
SndOn:
  lda_imm(0xff);
  write_joypad2(a); // disable irqs and set frame counter mode???
  lda_imm(0xf);
  apu_write(SND_MASTERCTRL_REG, a); // enable first four channels
  lda_abs_fz(PauseModeFlag); // is sound already in pause mode?
  if (!zero_flag) { goto InPause; }
  lda_zp(PauseSoundQueue); // if not, check pause sfx queue    
  cmp_imm_fczn(0x1);
  if (!zero_flag) { goto RunSoundSubroutines; } // if queue is empty, skip pause mode routine
  
InPause:
  lda_abs_fz(PauseSoundBuffer); // check pause sfx buffer
  if (!zero_flag) { goto ContPau; }
  lda_zp_fz(PauseSoundQueue); // check pause queue
  if (zero_flag) { goto SkipSoundSubroutines; }
  ram[PauseSoundBuffer] = a; // if queue full, store in buffer and activate
  ram[PauseModeFlag] = a; // pause mode to interrupt game sounds
  lda_imm(0x0); // disable sound and clear sfx buffers
  apu_write(SND_MASTERCTRL_REG, a);
  ram[Square1SoundBuffer] = a;
  ram[Square2SoundBuffer] = a;
  ram[NoiseSoundBuffer] = a;
  lda_imm(0xf);
  apu_write(SND_MASTERCTRL_REG, a); // enable sound again
  lda_imm(0x2a); // store length of sound in pause counter
  ram[Squ1_SfxLenCounter] = a;
  
PTone1F:
  lda_imm(0x44); // play first tone
  goto PTRegC; // unconditional branch
  
ContPau:
  lda_abs(Squ1_SfxLenCounter); // check pause length left
  cmp_imm_fcz(0x24); // time to play second?
  if (zero_flag) { goto PTone2F; }
  cmp_imm_fcz(0x1e); // time to play first again?
  if (zero_flag) { goto PTone1F; }
  cmp_imm_fcz(0x18); // time to play second again?
  if (!zero_flag) { goto DecPauC; } // only load regs during times, otherwise skip
  
PTone2F:
  lda_imm(0x64); // store reg contents and play the pause sfx
  
PTRegC:
  ldx_imm(0x84);
  ldy_imm_fzn(0x7f);
  cpu_call_begin(0xf32d); PlaySqu1Sfx(); cpu_call_end();
  
DecPauC:
  dec_abs_fz(Squ1_SfxLenCounter); // decrement pause sfx counter
  if (!zero_flag) { goto SkipSoundSubroutines; }
  lda_imm(0x0); // disable sound if in pause mode and
  apu_write(SND_MASTERCTRL_REG, a); // not currently playing the pause sfx
  lda_abs(PauseSoundBuffer); // if no longer playing pause sfx, check to see
  cmp_imm_fcz(0x2); // if we need to be playing sound again
  if (!zero_flag) { goto SkipPIn; }
  lda_imm(0x0); // clear pause mode to allow game sounds again
  ram[PauseModeFlag] = a;
  
SkipPIn:
  lda_imm(0x0); // clear pause sfx buffer
  ram[PauseSoundBuffer] = a;
  goto SkipSoundSubroutines;
  
RunSoundSubroutines:
  cpu_call_begin(0xf34d); Square1SfxHandler(); cpu_call_end(); // play sfx on square channel 1
  cpu_call_begin(0xf350); Square2SfxHandler(); cpu_call_end(); //  ''  ''  '' square channel 2
  cpu_call_begin(0xf353); NoiseSfxHandler(); cpu_call_end(); //  ''  ''  '' noise channel
  cpu_call_begin(0xf356); MusicHandler(); cpu_call_end(); // play music on all channels
  lda_imm(0x0); // clear the music queues
  ram[AreaMusicQueue] = a;
  ram[EventMusicQueue] = a;
  
SkipSoundSubroutines:
  lda_imm(0x0); // clear the sound effects queues
  ram[Square1SoundQueue] = a;
  ram[Square2SoundQueue] = a;
  ram[NoiseSoundQueue] = a;
  ram[PauseSoundQueue] = a;
  ldy_abs(DAC_Counter); // load some sort of counter 
  lda_zp(AreaMusicBuffer);
  and_imm_fz(0b00000011); // check for specific music
  if (zero_flag) { goto NoIncDAC; }
  inc_abs(DAC_Counter); // increment and check counter
  cpy_imm_fczn(0x30);
  if (!carry_flag) { goto StrWave; } // if not there yet, just store it
  
NoIncDAC:
  tya_fzn();
  if (zero_flag) { goto StrWave; } // if we are at zero, do not decrement 
  dec_abs_fzn(DAC_Counter); // decrement counter
  
StrWave:
  dynamic_ram_write(SND_DELTA_REG + 1, y); // store into DMC load register (??)
  return; // we are done here
  // --------------------------------
}

void Dump_Squ1_Regs(void) {
  dynamic_ram_write(SND_SQUARE1_REG + 1, y); // dump the contents of X and Y into square 1's control regs
  apu_write(SND_SQUARE1_REG, x);
  return;
}

void PlaySqu1Sfx(void) {
  cpu_call_begin(0xf38a); Dump_Squ1_Regs(); cpu_call_end(); // do sub to set ctrl regs for square 1, then set frequency regs
  SetFreq_Squ1(); // fallthrough
  return;
}

void SetFreq_Squ1(void) {
  ldx_imm(0x0); // set frequency reg offset for square 1 sound channel
  // Dump_Freq_Regs:
  tay();
  lda_absy_fzn(FreqRegLookupTbl + 1); // use previous contents of A for sound reg offset
  if (!zero_flag) {
    dynamic_ram_write(SND_REGISTER + 2 + x, a); // first byte goes into LSB of frequency divider
    lda_absy(FreqRegLookupTbl); // second byte goes into 3 MSB plus extra bit for 
    ora_imm_fzn(0b00001000); // length counter
    dynamic_ram_write(SND_REGISTER + 3 + x, a);
    // NoTone:
    return;
  }
}

void Dump_Sq2_Regs(void) {
  apu_write(SND_SQUARE2_REG, x); // dump the contents of X and Y into square 2's control regs
  dynamic_ram_write(SND_SQUARE2_REG + 1, y);
  return;
}

void PlaySqu2Sfx(void) {
  cpu_call_begin(0xf3a8); Dump_Sq2_Regs(); cpu_call_end(); // do sub to set ctrl regs for square 2, then set frequency regs
  SetFreq_Squ2(); // fallthrough
  return;
}

void SetFreq_Squ2(void) {
  goto SetFreq_Squ2;
  
Dump_Freq_Regs:
  tay();
  lda_absy_fzn(FreqRegLookupTbl + 1); // use previous contents of A for sound reg offset
  if (!zero_flag) {
    dynamic_ram_write(SND_REGISTER + 2 + x, a); // first byte goes into LSB of frequency divider
    lda_absy(FreqRegLookupTbl); // second byte goes into 3 MSB plus extra bit for 
    ora_imm_fzn(0b00001000); // length counter
    dynamic_ram_write(SND_REGISTER + 3 + x, a);
    // NoTone:
    return;
    
SetFreq_Squ2:
    ldx_imm(0x4); // set frequency reg offset for square 2 sound channel
    goto Dump_Freq_Regs; // unconditional branch
  }
}

void SetFreq_Tri(void) {
  goto SetFreq_Tri;
  
Dump_Freq_Regs:
  tay();
  lda_absy_fzn(FreqRegLookupTbl + 1); // use previous contents of A for sound reg offset
  if (!zero_flag) {
    dynamic_ram_write(SND_REGISTER + 2 + x, a); // first byte goes into LSB of frequency divider
    lda_absy(FreqRegLookupTbl); // second byte goes into 3 MSB plus extra bit for 
    ora_imm_fzn(0b00001000); // length counter
    dynamic_ram_write(SND_REGISTER + 3 + x, a);
    // NoTone:
    return;
    
SetFreq_Tri:
    ldx_imm(0x8); // set frequency reg offset for triangle sound channel
    goto Dump_Freq_Regs; // unconditional branch
    // --------------------------------
  }
}

void Square1SfxHandler(void) {
  goto Square1SfxHandler;
  
PlayFlagpoleSlide:
  lda_imm(0x40); // store length of flagpole sound
  ram[Squ1_SfxLenCounter] = a;
  lda_imm_fzn(0x62); // load part of reg contents for flagpole sound
  cpu_call_begin(0xf3c8); SetFreq_Squ1(); cpu_call_end();
  ldx_imm(0x99); // now load the rest
  goto FPS2nd;
  
PlaySmallJump:
  lda_imm(0x26); // branch here for small mario jumping sound
  goto JumpRegContents;
  
PlayBigJump:
  lda_imm(0x18); // branch here for big mario jumping sound
  
JumpRegContents:
  ldx_imm(0x82); // note that small and big jump borrow each others' reg contents
  ldy_imm_fzn(0xa7); // anyway, this loads the first part of mario's jumping sound
  cpu_call_begin(0xf3d9); PlaySqu1Sfx(); cpu_call_end();
  lda_imm(0x28); // store length of sfx for both jumping sounds
  ram[Squ1_SfxLenCounter] = a; // then continue on here
  
ContinueSndJump:
  lda_abs(Squ1_SfxLenCounter); // jumping sounds seem to be composed of three parts
  cmp_imm_fcz(0x25); // check for time to play second part yet
  if (!zero_flag) { goto N2Prt; }
  ldx_imm(0x5f); // load second part
  ldy_imm_fzn(0xf6);
  goto DmpJpFPS; // unconditional branch
  
N2Prt:
  cmp_imm_fcz(0x20); // check for third part
  if (!zero_flag) { goto DecJpFPS; }
  ldx_imm(0x48); // load third part
  
FPS2nd:
  ldy_imm_fzn(0xbc); // the flagpole slide sound shares part of third part
  
DmpJpFPS:
  cpu_call_begin(0xf3f6); Dump_Squ1_Regs(); cpu_call_end();
  if (!zero_flag) { goto DecJpFPS; } // unconditional branch outta here
  
PlayFireballThrow:
  lda_imm(0x5);
  ldy_imm(0x99); // load reg contents for fireball throw sound
  goto Fthrow; // unconditional branch
  
PlayBump:
  lda_imm(0xa); // load length of sfx and reg contents for bump sound
  ldy_imm(0x93);
  
Fthrow:
  ldx_imm(0x9e); // the fireball sound shares reg contents with the bump sound
  ram[Squ1_SfxLenCounter] = a;
  lda_imm_fzn(0xc); // load offset for bump sound
  cpu_call_begin(0xf40c); PlaySqu1Sfx(); cpu_call_end();
  
ContinueBumpThrow:
  lda_abs(Squ1_SfxLenCounter); // check for second part of bump sound
  cmp_imm_fcz(0x6);
  if (!zero_flag) { goto DecJpFPS; }
  lda_imm_fz(0xbb); // load second part directly
  dynamic_ram_write(SND_SQUARE1_REG + 1, a);
  
DecJpFPS:
  if (!zero_flag) { goto BranchToDecLength1; } // unconditional branch
  
Square1SfxHandler:
  ldy_zp_fzn(Square1SoundQueue); // check for sfx in queue
  if (zero_flag) { goto CheckSfx1Buffer; }
  ram[Square1SoundBuffer] = y; // if found, put in buffer
  if (neg_flag) { goto PlaySmallJump; } // small jump
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlayBigJump; } // big jump
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlayBump; } // bump
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlaySwimStomp; } // swim/stomp
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlaySmackEnemy; } // smack enemy
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlayPipeDownInj; } // pipedown/injury
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlayFireballThrow; } // fireball throw
  lsr_zp_fc(Square1SoundQueue);
  if (carry_flag) { goto PlayFlagpoleSlide; } // slide flagpole
  
CheckSfx1Buffer:
  lda_zp_fzn(Square1SoundBuffer); // check for sfx in buffer 
  if (zero_flag) { return; } // if not found, exit sub
  if (neg_flag) { goto ContinueSndJump; } // small mario jump 
  lsr_acc_fc();
  if (carry_flag) { goto ContinueSndJump; } // big mario jump 
  lsr_acc_fc();
  if (carry_flag) { goto ContinueBumpThrow; } // bump
  lsr_acc_fc();
  if (carry_flag) { goto ContinueSwimStomp; } // swim/stomp
  lsr_acc_fc();
  if (carry_flag) { goto ContinueSmackEnemy; } // smack enemy
  lsr_acc_fc();
  if (carry_flag) { goto ContinuePipeDownInj; } // pipedown/injury
  lsr_acc_fc();
  if (carry_flag) { goto ContinueBumpThrow; } // fireball throw
  lsr_acc_fczn();
  if (carry_flag) { goto DecrementSfx1Length; } // slide flagpole
  // ExS1H:
  return;
  
PlaySwimStomp:
  lda_imm(0xe); // store length of swim/stomp sound
  ram[Squ1_SfxLenCounter] = a;
  ldy_imm(0x9c); // store reg contents for swim/stomp sound
  ldx_imm(0x9e);
  lda_imm_fzn(0x26);
  cpu_call_begin(0xf468); PlaySqu1Sfx(); cpu_call_end();
  
ContinueSwimStomp:
  ldy_abs(Squ1_SfxLenCounter); // look up reg contents in data section based on
  lda_absy(SwimStompEnvelopeData - 1); // length of sound left, used to control sound's
  apu_write(SND_SQUARE1_REG, a); // envelope
  cpy_imm_fcz(0x6);
  if (!zero_flag) { goto BranchToDecLength1; }
  lda_imm_fz(0x9e); // when the length counts down to a certain point, put this
  dynamic_ram_write(SND_SQUARE1_REG + 2, a); // directly into the LSB of square 1's frequency divider
  
BranchToDecLength1:
  if (!zero_flag) { goto DecrementSfx1Length; } // unconditional branch (regardless of how we got here)
  
PlaySmackEnemy:
  lda_imm(0xe); // store length of smack enemy sound
  ldy_imm(0xcb);
  ldx_imm(0x9f);
  ram[Squ1_SfxLenCounter] = a;
  lda_imm_fzn(0x28); // store reg contents for smack enemy sound
  cpu_call_begin(0xf48a); PlaySqu1Sfx(); cpu_call_end();
  if (!zero_flag) { goto DecrementSfx1Length; } // unconditional branch
  
ContinueSmackEnemy:
  ldy_abs(Squ1_SfxLenCounter); // check about halfway through
  cpy_imm_fcz(0x8);
  if (!zero_flag) { goto SmSpc; }
  lda_imm(0xa0); // if we're at the about-halfway point, make the second tone
  dynamic_ram_write(SND_SQUARE1_REG + 2, a); // in the smack enemy sound
  lda_imm(0x9f);
  goto SmTick;
  
SmSpc:
  lda_imm(0x90); // this creates spaces in the sound, giving it its distinct noise
  
SmTick:
  apu_write(SND_SQUARE1_REG, a);
  
DecrementSfx1Length:
  dec_abs_fzn(Squ1_SfxLenCounter); // decrement length of sfx
  if (!zero_flag) { return; }
  StopSquare1Sfx(); // fallthrough
  return;
  
PlayPipeDownInj:
  lda_imm(0x2f); // load length of pipedown sound
  ram[Squ1_SfxLenCounter] = a;
  
ContinuePipeDownInj:
  lda_abs(Squ1_SfxLenCounter); // some bitwise logic, forces the regs
  lsr_acc_fc(); // to be written to only during six specific times
  if (carry_flag) { goto NoPDwnL; } // during which d3 must be set and d1-0 must be clear
  lsr_acc_fc();
  if (carry_flag) { goto NoPDwnL; }
  and_imm_fz(0b00000010);
  if (zero_flag) { goto NoPDwnL; }
  ldy_imm(0x91); // and this is where it actually gets written in
  ldx_imm(0x9a);
  lda_imm_fzn(0x44);
  cpu_call_begin(0xf4d0); PlaySqu1Sfx(); cpu_call_end();
  
NoPDwnL:
  goto DecrementSfx1Length;
  // --------------------------------
}

void StopSquare1Sfx(void) {
  ldx_imm(0x0); // if end of sfx reached, clear buffer
  ram[0xf1] = x; // and stop making the sfx
  ldx_imm(0xe);
  apu_write(SND_MASTERCTRL_REG, x);
  ldx_imm_fzn(0xf);
  apu_write(SND_MASTERCTRL_REG, x);
  // ExSfx1:
  return;
}

void StopSquare2Sfx(void) {
  ldx_imm(0xd); // stop playing the sfx
  apu_write(SND_MASTERCTRL_REG, x);
  ldx_imm_fzn(0xf);
  apu_write(SND_MASTERCTRL_REG, x);
  // ExSfx2:
  return;
}

void Square2SfxHandler(void) {
  goto Square2SfxHandler;
  
PlayCoinGrab:
  lda_imm(0x35); // load length of coin grab sound
  ldx_imm(0x8d); // and part of reg contents
  goto CGrab_TTickRegL;
  
PlayTimerTick:
  lda_imm(0x6); // load length of timer tick sound
  ldx_imm(0x98); // and part of reg contents
  
CGrab_TTickRegL:
  ram[Squ2_SfxLenCounter] = a;
  ldy_imm(0x7f); // load the rest of reg contents 
  lda_imm_fzn(0x42); // of coin grab and timer tick sound
  cpu_call_begin(0xf52b); PlaySqu2Sfx(); cpu_call_end();
  
ContinueCGrabTTick:
  lda_abs(Squ2_SfxLenCounter); // check for time to play second tone yet
  cmp_imm_fcz(0x30); // timer tick sound also executes this, not sure why
  if (!zero_flag) { goto N2Tone; }
  lda_imm_fz(0x54); // if so, load the tone directly into the reg
  dynamic_ram_write(SND_SQUARE2_REG + 2, a);
  
N2Tone:
  if (!zero_flag) { goto DecrementSfx2Length; }
  
PlayBlast:
  lda_imm(0x20); // load length of fireworks/gunfire sound
  ram[Squ2_SfxLenCounter] = a;
  ldy_imm(0x94); // load reg contents of fireworks/gunfire sound
  lda_imm(0x5e);
  goto SBlasJ;
  
ContinueBlast:
  lda_abs(Squ2_SfxLenCounter); // check for time to play second part
  cmp_imm_fcz(0x18);
  if (!zero_flag) { goto DecrementSfx2Length; }
  ldy_imm(0x93); // load second part reg contents then
  lda_imm(0x18);
  
SBlasJ:
  goto BlstSJp; // unconditional branch to load rest of reg contents
  
PlayPowerUpGrab:
  lda_imm(0x36); // load length of power-up grab sound
  ram[Squ2_SfxLenCounter] = a;
  
ContinuePowerUpGrab:
  lda_abs(Squ2_SfxLenCounter); // load frequency reg based on length left over
  lsr_acc_fc(); // divide by 2
  if (carry_flag) { goto DecrementSfx2Length; } // alter frequency every other frame
  tay();
  lda_absy(PowerUpGrabFreqData - 1); // use length left over / 2 for frequency offset
  ldx_imm(0x5d); // store reg contents of power-up grab sound
  ldy_imm_fzn(0x7f);
  
LoadSqu2Regs:
  cpu_call_begin(0xf567); PlaySqu2Sfx(); cpu_call_end();
  
DecrementSfx2Length:
  dec_abs_fzn(Squ2_SfxLenCounter); // decrement length of sfx
  if (!zero_flag) { return; }
  
EmptySfx2Buffer:
  ldx_imm(0x0); // initialize square 2's sound effects buffer
  ram[Square2SoundBuffer] = x;
  StopSquare2Sfx(); // fallthrough
  return;
  
Square2SfxHandler:
  lda_zp(Square2SoundBuffer); // special handling for the 1-up sound to keep it
  and_imm_fz(Sfx_ExtraLife); // from being interrupted by other sounds on square 2
  if (!zero_flag) { goto ContinueExtraLife; }
  ldy_zp_fzn(Square2SoundQueue); // check for sfx in queue
  if (zero_flag) { goto CheckSfx2Buffer; }
  ram[Square2SoundBuffer] = y; // if found, put in buffer and check for the following
  if (neg_flag) { goto PlayBowserFall; } // bowser fall
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayCoinGrab; } // coin grab
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayGrowPowerUp; } // power-up reveal
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayGrowVine; } // vine grow
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayBlast; } // fireworks/gunfire
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayTimerTick; } // timer tick
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayPowerUpGrab; } // power-up grab
  lsr_zp_fc(Square2SoundQueue);
  if (carry_flag) { goto PlayExtraLife; } // 1-up
  
CheckSfx2Buffer:
  lda_zp_fzn(Square2SoundBuffer); // check for sfx in buffer
  if (zero_flag) { return; } // if not found, exit sub
  if (neg_flag) { goto ContinueBowserFall; } // bowser fall
  lsr_acc_fc();
  if (carry_flag) { goto Cont_CGrab_TTick; } // coin grab
  lsr_acc_fc();
  if (carry_flag) { goto ContinueGrowItems; } // power-up reveal
  lsr_acc_fc();
  if (carry_flag) { goto ContinueGrowItems; } // vine grow
  lsr_acc_fc();
  if (carry_flag) { goto ContinueBlast; } // fireworks/gunfire
  lsr_acc_fc();
  if (carry_flag) { goto Cont_CGrab_TTick; } // timer tick
  lsr_acc_fc();
  if (carry_flag) { goto ContinuePowerUpGrab; } // power-up grab
  lsr_acc_fczn();
  if (carry_flag) { goto ContinueExtraLife; } // 1-up
  // ExS2H:
  return;
  
Cont_CGrab_TTick:
  goto ContinueCGrabTTick;
  
JumpToDecLength2:
  goto DecrementSfx2Length;
  
PlayBowserFall:
  lda_imm(0x38); // load length of bowser defeat sound
  ram[Squ2_SfxLenCounter] = a;
  ldy_imm(0xc4); // load contents of reg for bowser defeat sound
  lda_imm(0x18);
  
BlstSJp:
  goto PBFRegs;
  
ContinueBowserFall:
  lda_abs(Squ2_SfxLenCounter); // check for almost near the end
  cmp_imm_fcz(0x8);
  if (!zero_flag) { goto DecrementSfx2Length; }
  ldy_imm(0xa4); // if so, load the rest of reg contents for bowser defeat sound
  lda_imm(0x5a);
  
PBFRegs:
  ldx_imm_fzn(0x9f); // the fireworks/gunfire sound shares part of reg contents here
  
EL_LRegs:
  goto LoadSqu2Regs; // this is an unconditional branch outta here
  
PlayExtraLife:
  lda_imm(0x30); // load length of 1-up sound
  ram[Squ2_SfxLenCounter] = a;
  
ContinueExtraLife:
  lda_abs(Squ2_SfxLenCounter);
  ldx_imm(0x3); // load new tones only every eight frames
  
DivLLoop:
  lsr_acc_fc();
  if (carry_flag) { goto JumpToDecLength2; } // if any bits set here, branch to dec the length
  dex_fz();
  if (!zero_flag) { goto DivLLoop; } // do this until all bits checked, if none set, continue
  tay();
  lda_absy(ExtraLifeFreqData - 1); // load our reg contents
  ldx_imm(0x82);
  ldy_imm_fzn(0x7f);
  goto EL_LRegs; // unconditional branch
  
PlayGrowPowerUp:
  lda_imm(0x10); // load length of power-up reveal sound
  goto GrowItemRegs;
  
PlayGrowVine:
  lda_imm(0x20); // load length of vine grow sound
  
GrowItemRegs:
  ram[Squ2_SfxLenCounter] = a;
  lda_imm(0x7f); // load contents of reg for both sounds directly
  dynamic_ram_write(SND_SQUARE2_REG + 1, a);
  lda_imm(0x0); // start secondary counter for both sounds
  ram[Sfx_SecondaryCounter] = a;
  
ContinueGrowItems:
  inc_abs(Sfx_SecondaryCounter); // increment secondary counter for both sounds
  lda_abs(Sfx_SecondaryCounter); // this sound doesn't decrement the usual counter
  lsr_acc(); // divide by 2 to get the offset
  tay();
  cpy_abs_fcz(Squ2_SfxLenCounter); // have we reached the end yet?
  if (zero_flag) { goto StopGrowItems; } // if so, branch to jump, and stop playing sounds
  lda_imm(0x9d); // load contents of other reg directly
  apu_write(SND_SQUARE2_REG, a);
  lda_absy_fzn(PUp_VGrow_FreqData); // use secondary counter / 2 as offset for frequency regs
  cpu_call_begin(0xf626); SetFreq_Squ2(); cpu_call_end();
  return;
  
StopGrowItems:
  goto EmptySfx2Buffer; // branch to stop playing sounds
  // --------------------------------
}

void NoiseSfxHandler(void) {
  goto NoiseSfxHandler;
  
PlayBrickShatter:
  lda_imm(0x20); // load length of brick shatter sound
  ram[Noise_SfxLenCounter] = a;
  
ContinueBrickShatter:
  lda_abs(Noise_SfxLenCounter);
  lsr_acc_fc(); // divide by 2 and check for bit set to use offset
  if (!carry_flag) { goto DecrementSfx3Length; }
  tay();
  ldx_absy(BrickShatterFreqData); // load reg contents of brick shatter sound
  lda_absy(BrickShatterEnvData);
  
PlayNoiseSfx:
  apu_write(SND_NOISE_REG, a); // play the sfx
  dynamic_ram_write(SND_NOISE_REG + 2, x);
  lda_imm(0x18);
  dynamic_ram_write(SND_NOISE_REG + 3, a);
  
DecrementSfx3Length:
  dec_abs_fzn(Noise_SfxLenCounter); // decrement length of sfx
  if (!zero_flag) { return; }
  lda_imm(0xf0); // if done, stop playing the sfx
  apu_write(SND_NOISE_REG, a);
  lda_imm_fzn(0x0);
  ram[NoiseSoundBuffer] = a;
  // ExSfx3:
  return;
  
NoiseSfxHandler:
  ldy_zp_fz(NoiseSoundQueue); // check for sfx in queue
  if (zero_flag) { goto CheckNoiseBuffer; }
  ram[NoiseSoundBuffer] = y; // if found, put in buffer
  lsr_zp_fc(NoiseSoundQueue);
  if (carry_flag) { goto PlayBrickShatter; } // brick shatter
  lsr_zp_fc(NoiseSoundQueue);
  if (carry_flag) { goto PlayBowserFlame; } // bowser flame
  
CheckNoiseBuffer:
  lda_zp_fzn(NoiseSoundBuffer); // check for sfx in buffer
  if (zero_flag) { return; } // if not found, exit sub
  lsr_acc_fc();
  if (carry_flag) { goto ContinueBrickShatter; } // brick shatter
  lsr_acc_fczn();
  if (carry_flag) { goto ContinueBowserFlame; } // bowser flame
  // ExNH:
  return;
  
PlayBowserFlame:
  lda_imm(0x40); // load length of bowser flame sound
  ram[Noise_SfxLenCounter] = a;
  
ContinueBowserFlame:
  lda_abs(Noise_SfxLenCounter);
  lsr_acc_fc();
  tay();
  ldx_imm(0xf); // load reg contents of bowser flame sound
  lda_absy_fz(BowserFlameEnvData - 1);
  if (!zero_flag) { goto PlayNoiseSfx; } // unconditional branch here
  // --------------------------------
  // ContinueMusic:
  goto HandleSquare2Music; // if we have music, start with square 2 channel
  
LoadEventMusic:
  ram[EventMusicBuffer] = a; // copy event music queue contents to buffer
  cmp_imm_fczn(DeathMusic); // is it death music?
  if (!zero_flag) { goto NoStopSfx; } // if not, jump elsewhere
  cpu_call_begin(0xf6ad); StopSquare1Sfx(); cpu_call_end(); // stop sfx in square 1 and 2
  cpu_call_begin(0xf6b0); StopSquare2Sfx(); cpu_call_end(); // but clear only square 1's sfx buffer
  
NoStopSfx:
  ldx_zp(AreaMusicBuffer);
  ram[AreaMusicBuffer_Alt] = x; // save current area music buffer to be re-obtained later
  ldy_imm(0x0);
  ram[NoteLengthTblAdder] = y; // default value for additional length byte offset
  ram[AreaMusicBuffer] = y; // clear area music buffer
  cmp_imm_fz(TimeRunningOutMusic); // is it time running out music?
  if (!zero_flag) { goto FindEventMusicHeader; }
  ldx_imm(0x8); // load offset to be added to length byte of header
  ram[NoteLengthTblAdder] = x;
  goto FindEventMusicHeader; // unconditional branch
  
GMLoopB:
  ram[GroundMusicHeaderOfs] = y;
  
HandleAreaMusicLoopB:
  ldy_imm(0x0); // clear event music buffer
  ram[EventMusicBuffer] = y;
  ram[AreaMusicBuffer] = a; // copy area music queue contents to buffer
  cmp_imm_fz(0x1); // is it ground level music?
  if (!zero_flag) { goto FindAreaMusicHeader; }
  inc_abs(GroundMusicHeaderOfs); // increment but only if playing ground level music
  ldy_abs(GroundMusicHeaderOfs); // is it time to loopback ground level music?
  cpy_imm_fcz(0x32);
  if (!zero_flag) { goto LoadHeader; } // branch ahead with alternate offset
  ldy_imm(0x11);
  goto GMLoopB; // unconditional branch
  
FindAreaMusicHeader:
  ldy_imm(0x8); // load Y for offset of area music
  ram[MusicOffset_Square2] = y; // residual instruction here
  
FindEventMusicHeader:
  iny(); // increment Y pointer based on previously loaded queue contents
  lsr_acc_fc(); // bit shift and increment until we find a set bit for music
  if (!carry_flag) { goto FindEventMusicHeader; }
  
LoadHeader:
  lda_absy(MusicHeaderOffsetData); // load offset for header
  tay();
  lda_absy(MusicHeaderData); // now load the header
  ram[NoteLenLookupTblOfs] = a;
  lda_absy(MusicHeaderData + 1);
  ram[MusicDataLow] = a;
  lda_absy(MusicHeaderData + 2);
  ram[MusicDataHigh] = a;
  lda_absy(MusicHeaderData + 3);
  ram[MusicOffset_Triangle] = a;
  lda_absy(MusicHeaderData + 4);
  ram[MusicOffset_Square1] = a;
  lda_absy(MusicHeaderData + 5);
  ram[MusicOffset_Noise] = a;
  ram[NoiseDataLoopbackOfs] = a;
  lda_imm(0x1); // initialize music note counters
  ram[Squ2_NoteLenCounter] = a;
  ram[Squ1_NoteLenCounter] = a;
  ram[Tri_NoteLenCounter] = a;
  ram[Noise_BeatLenCounter] = a;
  lda_imm(0x0); // initialize music data offset for square 2
  ram[MusicOffset_Square2] = a;
  ram[AltRegContentFlag] = a; // initialize alternate control reg data used by square 1
  lda_imm(0xb); // disable triangle channel and reenable it
  apu_write(SND_MASTERCTRL_REG, a);
  lda_imm(0xf);
  apu_write(SND_MASTERCTRL_REG, a);
  
HandleSquare2Music:
  dec_abs_fz(Squ2_NoteLenCounter); // decrement square 2 note length
  if (!zero_flag) { goto MiscSqu2MusicTasks; } // is it time for more data?  if not, branch to end tasks
  ldy_zp(MusicOffset_Square2); // increment square 2 music offset and fetch data
  inc_zp(MusicOffset_Square2);
  lda_indy_fzn(MusicData);
  if (zero_flag) { goto EndOfMusicData; } // if zero, the data is a null terminator
  if (!neg_flag) { goto Squ2NoteHandler; } // if non-negative, data is a note
  if (!zero_flag) { goto Squ2LengthHandler; } // otherwise it is length data
  
EndOfMusicData:
  lda_abs(EventMusicBuffer); // check secondary buffer for time running out music
  cmp_imm_fcz(TimeRunningOutMusic);
  if (!zero_flag) { goto NotTRO; }
  lda_abs_fz(AreaMusicBuffer_Alt); // load previously saved contents of primary buffer
  if (!zero_flag) { goto MusicLoopBack; } // and start playing the song again if there is one
  
NotTRO:
  and_imm_fz(VictoryMusic); // check for victory music (the only secondary that loops)
  if (!zero_flag) { goto VictoryMLoopBack; }
  lda_zp(AreaMusicBuffer); // check primary buffer for any music except pipe intro
  and_imm_fz(0b01011111);
  if (!zero_flag) { goto MusicLoopBack; } // if any area music except pipe intro, music loops
  lda_imm(0x0); // clear primary and secondary buffers and initialize
  ram[AreaMusicBuffer] = a; // control regs of square and triangle channels
  ram[EventMusicBuffer] = a;
  apu_write(SND_TRIANGLE_REG, a);
  lda_imm_fzn(0x90);
  apu_write(SND_SQUARE1_REG, a);
  apu_write(SND_SQUARE2_REG, a);
  return;
  
MusicLoopBack:
  goto HandleAreaMusicLoopB;
  
VictoryMLoopBack:
  goto LoadEventMusic;
  
Squ2LengthHandler:
  cpu_call_begin(0xf77c); ProcessLengthData(); cpu_call_end(); // store length of note
  ram[Squ2_NoteLenBuffer] = a;
  ldy_zp(MusicOffset_Square2); // fetch another byte (MUST NOT BE LENGTH BYTE!)
  inc_zp(MusicOffset_Square2);
  lda_indy(MusicData);
  
Squ2NoteHandler:
  ldx_zp_fzn(Square2SoundBuffer); // is there a sound playing on this channel?
  if (!zero_flag) { goto SkipFqL1; }
  cpu_call_begin(0xf78c); SetFreq_Squ2(); cpu_call_end(); // no, then play the note
  if (zero_flag) { goto Rest; } // check to see if note is rest
  cpu_call_begin(0xf791); LoadControlRegs(); cpu_call_end(); // if not, load control regs for square 2
  
Rest:
  ram[Squ2_EnvelopeDataCtrl] = a; // save contents of A
  cpu_call_begin(0xf797); Dump_Sq2_Regs(); cpu_call_end(); // dump X and Y into square 2 control regs
  
SkipFqL1:
  lda_abs(Squ2_NoteLenBuffer); // save length in square 2 note counter
  ram[Squ2_NoteLenCounter] = a;
  
MiscSqu2MusicTasks:
  lda_zp_fz(Square2SoundBuffer); // is there a sound playing on square 2?
  if (!zero_flag) { goto HandleSquare1Music; }
  lda_abs(EventMusicBuffer); // check for death music or d4 set on secondary buffer
  and_imm_fz(0b10010001); // note that regs for death music or d4 are loaded by default
  if (!zero_flag) { goto HandleSquare1Music; }
  ldy_abs_fzn(Squ2_EnvelopeDataCtrl); // check for contents saved from LoadControlRegs
  if (zero_flag) { goto NoDecEnv1; }
  dec_abs_fzn(Squ2_EnvelopeDataCtrl); // decrement unless already zero
  
NoDecEnv1:
  cpu_call_begin(0xf7b3); LoadEnvelopeData(); cpu_call_end(); // do a load of envelope data to replace default
  apu_write(SND_SQUARE2_REG, a); // based on offset set by first load unless playing
  ldx_imm(0x7f); // death music or d4 set on secondary buffer
  dynamic_ram_write(SND_SQUARE2_REG + 1, x);
  
HandleSquare1Music:
  ldy_zp_fz(MusicOffset_Square1); // is there a nonzero offset here?
  if (zero_flag) { goto HandleTriangleMusic; } // if not, skip ahead to the triangle channel
  dec_abs_fz(Squ1_NoteLenCounter); // decrement square 1 note length
  if (!zero_flag) { goto MiscSqu1MusicTasks; } // is it time for more data?
  
FetchSqu1MusicData:
  ldy_zp(MusicOffset_Square1); // increment square 1 music offset and fetch data
  inc_zp(MusicOffset_Square1);
  lda_indy_fzn(MusicData);
  if (!zero_flag) { goto Squ1NoteHandler; } // if nonzero, then skip this part
  lda_imm(0x83);
  apu_write(SND_SQUARE1_REG, a); // store some data into control regs for square 1
  lda_imm(0x94); // and fetch another byte of data, used to give
  dynamic_ram_write(SND_SQUARE1_REG + 1, a); // death music its unique sound
  ram[AltRegContentFlag] = a;
  goto FetchSqu1MusicData; // unconditional branch
  
Squ1NoteHandler:
  cpu_call_begin(0xf7de); AlternateLengthHandler(); cpu_call_end();
  ram[Squ1_NoteLenCounter] = a; // save contents of A in square 1 note counter
  ldy_zp_fz(Square1SoundBuffer); // is there a sound playing on square 1?
  if (!zero_flag) { goto HandleTriangleMusic; }
  txa();
  and_imm_fzn(0b00111110); // change saved data to appropriate note format
  cpu_call_begin(0xf7eb); SetFreq_Squ1(); cpu_call_end(); // play the note
  if (zero_flag) { goto SkipCtrlL; }
  cpu_call_begin(0xf7f0); LoadControlRegs(); cpu_call_end();
  
SkipCtrlL:
  ram[Squ1_EnvelopeDataCtrl] = a; // save envelope offset
  cpu_call_begin(0xf7f6); Dump_Squ1_Regs(); cpu_call_end();
  
MiscSqu1MusicTasks:
  lda_zp_fz(Square1SoundBuffer); // is there a sound playing on square 1?
  if (!zero_flag) { goto HandleTriangleMusic; }
  lda_abs(EventMusicBuffer); // check for death music or d4 set on secondary buffer
  and_imm_fz(0b10010001);
  if (!zero_flag) { goto DeathMAltReg; }
  ldy_abs_fzn(Squ1_EnvelopeDataCtrl); // check saved envelope offset
  if (zero_flag) { goto NoDecEnv2; }
  dec_abs_fzn(Squ1_EnvelopeDataCtrl); // decrement unless already zero
  
NoDecEnv2:
  cpu_call_begin(0xf80c); LoadEnvelopeData(); cpu_call_end(); // do a load of envelope data
  apu_write(SND_SQUARE1_REG, a); // based on offset set by first load
  
DeathMAltReg:
  lda_abs_fz(AltRegContentFlag); // check for alternate control reg data
  if (!zero_flag) { goto DoAltLoad; }
  lda_imm(0x7f); // load this value if zero, the alternate value
  
DoAltLoad:
  dynamic_ram_write(SND_SQUARE1_REG + 1, a); // if nonzero, and let's move on
  
HandleTriangleMusic:
  lda_zp(MusicOffset_Triangle);
  dec_abs_fz(Tri_NoteLenCounter); // decrement triangle note length
  if (!zero_flag) { goto HandleNoiseMusic; } // is it time for more data?
  ldy_zp(MusicOffset_Triangle); // increment square 1 music offset and fetch data
  inc_zp(MusicOffset_Triangle);
  lda_indy_fzn(MusicData);
  if (zero_flag) { goto LoadTriCtrlReg; } // if zero, skip all this and move on to noise 
  if (!neg_flag) { goto TriNoteHandler; } // if non-negative, data is note
  cpu_call_begin(0xf82d); ProcessLengthData(); cpu_call_end(); // otherwise, it is length data
  ram[Tri_NoteLenBuffer] = a; // save contents of A
  lda_imm(0x1f);
  apu_write(SND_TRIANGLE_REG, a); // load some default data for triangle control reg
  ldy_zp(MusicOffset_Triangle); // fetch another byte
  inc_zp(MusicOffset_Triangle);
  lda_indy_fzn(MusicData);
  if (zero_flag) { goto LoadTriCtrlReg; } // check once more for nonzero data
  
TriNoteHandler:
  cpu_call_begin(0xf840); SetFreq_Tri(); cpu_call_end();
  ldx_abs(Tri_NoteLenBuffer); // save length in triangle note counter
  ram[Tri_NoteLenCounter] = x;
  lda_abs(EventMusicBuffer);
  and_imm_fz(0b01101110); // check for death music or d4 set on secondary buffer
  if (!zero_flag) { goto NotDOrD4; } // if playing any other secondary, skip primary buffer check
  lda_zp(AreaMusicBuffer); // check primary buffer for water or castle level music
  and_imm_fz(0b00001010);
  if (zero_flag) { goto HandleNoiseMusic; } // if playing any other primary, or death or d4, go on to noise routine
  
NotDOrD4:
  txa(); // if playing water or castle music or any secondary
  cmp_imm_fc(0x12); // besides death music or d4 set, check length of note
  if (carry_flag) { goto LongN; }
  lda_abs(EventMusicBuffer); // check for win castle music again if not playing a long note
  and_imm_fz(EndOfCastleMusic);
  if (zero_flag) { goto MediN; }
  lda_imm(0xf); // load value $0f if playing the win castle music and playing a short
  goto LoadTriCtrlReg; // note, load value $1f if playing water or castle level music or any
  
MediN:
  lda_imm(0x1f); // secondary besides death and d4 except win castle or win castle and playing
  goto LoadTriCtrlReg; // a short note, and load value $ff if playing a long note on water, castle
  
LongN:
  lda_imm(0xff); // or any secondary (including win castle) except death and d4
  
LoadTriCtrlReg:
  apu_write(SND_TRIANGLE_REG, a); // save final contents of A into control reg for triangle
  
HandleNoiseMusic:
  lda_zp(AreaMusicBuffer); // check if playing underground or castle music
  and_imm_fzn(0b11110011);
  if (zero_flag) { return; } // if so, skip the noise routine
  dec_abs_fzn(Noise_BeatLenCounter); // decrement noise beat length
  if (!zero_flag) { return; } // is it time for more data?
  
FetchNoiseBeatData:
  ldy_abs(MusicOffset_Noise); // increment noise beat offset and fetch data
  inc_abs(MusicOffset_Noise);
  lda_indy_fzn(MusicData); // get noise beat data, if nonzero, branch to handle
  if (!zero_flag) { goto NoiseBeatHandler; }
  lda_abs_fzn(NoiseDataLoopbackOfs); // if data is zero, reload original noise beat offset
  ram[MusicOffset_Noise] = a; // and loopback next time around
  if (!zero_flag) { goto FetchNoiseBeatData; } // unconditional branch
  
NoiseBeatHandler:
  cpu_call_begin(0xf88c); AlternateLengthHandler(); cpu_call_end();
  ram[Noise_BeatLenCounter] = a; // store length in noise beat counter
  txa();
  and_imm_fz(0b00111110); // reload data and erase length bits
  if (zero_flag) { goto SilentBeat; } // if no beat data, silence
  cmp_imm_fcz(0x30); // check the beat data and play the appropriate
  if (zero_flag) { goto LongBeat; } // noise accordingly
  cmp_imm_fcz(0x20);
  if (zero_flag) { goto StrongBeat; }
  and_imm_fz(0b00010000);
  if (zero_flag) { goto SilentBeat; }
  lda_imm(0x1c); // short beat data
  ldx_imm(0x3);
  ldy_imm_fzn(0x18);
  goto PlayBeat;
  
StrongBeat:
  lda_imm(0x1c); // strong beat data
  ldx_imm(0xc);
  ldy_imm_fzn(0x18);
  goto PlayBeat;
  
LongBeat:
  lda_imm(0x1c); // long beat data
  ldx_imm(0x3);
  ldy_imm_fzn(0x58);
  goto PlayBeat;
  
SilentBeat:
  lda_imm_fzn(0x10); // silence
  
PlayBeat:
  apu_write(SND_NOISE_REG, a); // load beat data into noise regs
  dynamic_ram_write(SND_NOISE_REG + 2, x);
  dynamic_ram_write(SND_NOISE_REG + 3, y);
  // ExitMusicHandler:
  return;
}

void MusicHandler(void) {
  goto MusicHandler;
  
ContinueMusic:
  goto HandleSquare2Music; // if we have music, start with square 2 channel
  
MusicHandler:
  lda_zp_fz(EventMusicQueue); // check event music queue
  if (!zero_flag) { goto LoadEventMusic; }
  lda_zp_fz(AreaMusicQueue); // check area music queue
  if (!zero_flag) { goto LoadAreaMusic; }
  lda_abs(EventMusicBuffer); // check both buffers
  ora_zp_fzn(AreaMusicBuffer);
  if (!zero_flag) { goto ContinueMusic; }
  return; // no music, then leave
  
LoadEventMusic:
  ram[EventMusicBuffer] = a; // copy event music queue contents to buffer
  cmp_imm_fczn(DeathMusic); // is it death music?
  if (!zero_flag) { goto NoStopSfx; } // if not, jump elsewhere
  cpu_call_begin(0xf6ad); StopSquare1Sfx(); cpu_call_end(); // stop sfx in square 1 and 2
  cpu_call_begin(0xf6b0); StopSquare2Sfx(); cpu_call_end(); // but clear only square 1's sfx buffer
  
NoStopSfx:
  ldx_zp(AreaMusicBuffer);
  ram[AreaMusicBuffer_Alt] = x; // save current area music buffer to be re-obtained later
  ldy_imm(0x0);
  ram[NoteLengthTblAdder] = y; // default value for additional length byte offset
  ram[AreaMusicBuffer] = y; // clear area music buffer
  cmp_imm_fz(TimeRunningOutMusic); // is it time running out music?
  if (!zero_flag) { goto FindEventMusicHeader; }
  ldx_imm(0x8); // load offset to be added to length byte of header
  ram[NoteLengthTblAdder] = x;
  goto FindEventMusicHeader; // unconditional branch
  
LoadAreaMusic:
  cmp_imm_fczn(0x4); // is it underground music?
  if (!zero_flag) { goto NoStop1; } // no, do not stop square 1 sfx
  cpu_call_begin(0xf6ce); StopSquare1Sfx(); cpu_call_end();
  
NoStop1:
  ldy_imm(0x10); // start counter used only by ground level music
  
GMLoopB:
  ram[GroundMusicHeaderOfs] = y;
  
HandleAreaMusicLoopB:
  ldy_imm(0x0); // clear event music buffer
  ram[EventMusicBuffer] = y;
  ram[AreaMusicBuffer] = a; // copy area music queue contents to buffer
  cmp_imm_fz(0x1); // is it ground level music?
  if (!zero_flag) { goto FindAreaMusicHeader; }
  inc_abs(GroundMusicHeaderOfs); // increment but only if playing ground level music
  ldy_abs(GroundMusicHeaderOfs); // is it time to loopback ground level music?
  cpy_imm_fcz(0x32);
  if (!zero_flag) { goto LoadHeader; } // branch ahead with alternate offset
  ldy_imm(0x11);
  goto GMLoopB; // unconditional branch
  
FindAreaMusicHeader:
  ldy_imm(0x8); // load Y for offset of area music
  ram[MusicOffset_Square2] = y; // residual instruction here
  
FindEventMusicHeader:
  iny(); // increment Y pointer based on previously loaded queue contents
  lsr_acc_fc(); // bit shift and increment until we find a set bit for music
  if (!carry_flag) { goto FindEventMusicHeader; }
  
LoadHeader:
  lda_absy(MusicHeaderOffsetData); // load offset for header
  tay();
  lda_absy(MusicHeaderData); // now load the header
  ram[NoteLenLookupTblOfs] = a;
  lda_absy(MusicHeaderData + 1);
  ram[MusicDataLow] = a;
  lda_absy(MusicHeaderData + 2);
  ram[MusicDataHigh] = a;
  lda_absy(MusicHeaderData + 3);
  ram[MusicOffset_Triangle] = a;
  lda_absy(MusicHeaderData + 4);
  ram[MusicOffset_Square1] = a;
  lda_absy(MusicHeaderData + 5);
  ram[MusicOffset_Noise] = a;
  ram[NoiseDataLoopbackOfs] = a;
  lda_imm(0x1); // initialize music note counters
  ram[Squ2_NoteLenCounter] = a;
  ram[Squ1_NoteLenCounter] = a;
  ram[Tri_NoteLenCounter] = a;
  ram[Noise_BeatLenCounter] = a;
  lda_imm(0x0); // initialize music data offset for square 2
  ram[MusicOffset_Square2] = a;
  ram[AltRegContentFlag] = a; // initialize alternate control reg data used by square 1
  lda_imm(0xb); // disable triangle channel and reenable it
  apu_write(SND_MASTERCTRL_REG, a);
  lda_imm(0xf);
  apu_write(SND_MASTERCTRL_REG, a);
  
HandleSquare2Music:
  dec_abs_fz(Squ2_NoteLenCounter); // decrement square 2 note length
  if (!zero_flag) { goto MiscSqu2MusicTasks; } // is it time for more data?  if not, branch to end tasks
  ldy_zp(MusicOffset_Square2); // increment square 2 music offset and fetch data
  inc_zp(MusicOffset_Square2);
  lda_indy_fzn(MusicData);
  if (zero_flag) { goto EndOfMusicData; } // if zero, the data is a null terminator
  if (!neg_flag) { goto Squ2NoteHandler; } // if non-negative, data is a note
  if (!zero_flag) { goto Squ2LengthHandler; } // otherwise it is length data
  
EndOfMusicData:
  lda_abs(EventMusicBuffer); // check secondary buffer for time running out music
  cmp_imm_fcz(TimeRunningOutMusic);
  if (!zero_flag) { goto NotTRO; }
  lda_abs_fz(AreaMusicBuffer_Alt); // load previously saved contents of primary buffer
  if (!zero_flag) { goto MusicLoopBack; } // and start playing the song again if there is one
  
NotTRO:
  and_imm_fz(VictoryMusic); // check for victory music (the only secondary that loops)
  if (!zero_flag) { goto VictoryMLoopBack; }
  lda_zp(AreaMusicBuffer); // check primary buffer for any music except pipe intro
  and_imm_fz(0b01011111);
  if (!zero_flag) { goto MusicLoopBack; } // if any area music except pipe intro, music loops
  lda_imm(0x0); // clear primary and secondary buffers and initialize
  ram[AreaMusicBuffer] = a; // control regs of square and triangle channels
  ram[EventMusicBuffer] = a;
  apu_write(SND_TRIANGLE_REG, a);
  lda_imm_fzn(0x90);
  apu_write(SND_SQUARE1_REG, a);
  apu_write(SND_SQUARE2_REG, a);
  return;
  
MusicLoopBack:
  goto HandleAreaMusicLoopB;
  
VictoryMLoopBack:
  goto LoadEventMusic;
  
Squ2LengthHandler:
  cpu_call_begin(0xf77c); ProcessLengthData(); cpu_call_end(); // store length of note
  ram[Squ2_NoteLenBuffer] = a;
  ldy_zp(MusicOffset_Square2); // fetch another byte (MUST NOT BE LENGTH BYTE!)
  inc_zp(MusicOffset_Square2);
  lda_indy(MusicData);
  
Squ2NoteHandler:
  ldx_zp_fzn(Square2SoundBuffer); // is there a sound playing on this channel?
  if (!zero_flag) { goto SkipFqL1; }
  cpu_call_begin(0xf78c); SetFreq_Squ2(); cpu_call_end(); // no, then play the note
  if (zero_flag) { goto Rest; } // check to see if note is rest
  cpu_call_begin(0xf791); LoadControlRegs(); cpu_call_end(); // if not, load control regs for square 2
  
Rest:
  ram[Squ2_EnvelopeDataCtrl] = a; // save contents of A
  cpu_call_begin(0xf797); Dump_Sq2_Regs(); cpu_call_end(); // dump X and Y into square 2 control regs
  
SkipFqL1:
  lda_abs(Squ2_NoteLenBuffer); // save length in square 2 note counter
  ram[Squ2_NoteLenCounter] = a;
  
MiscSqu2MusicTasks:
  lda_zp_fz(Square2SoundBuffer); // is there a sound playing on square 2?
  if (!zero_flag) { goto HandleSquare1Music; }
  lda_abs(EventMusicBuffer); // check for death music or d4 set on secondary buffer
  and_imm_fz(0b10010001); // note that regs for death music or d4 are loaded by default
  if (!zero_flag) { goto HandleSquare1Music; }
  ldy_abs_fzn(Squ2_EnvelopeDataCtrl); // check for contents saved from LoadControlRegs
  if (zero_flag) { goto NoDecEnv1; }
  dec_abs_fzn(Squ2_EnvelopeDataCtrl); // decrement unless already zero
  
NoDecEnv1:
  cpu_call_begin(0xf7b3); LoadEnvelopeData(); cpu_call_end(); // do a load of envelope data to replace default
  apu_write(SND_SQUARE2_REG, a); // based on offset set by first load unless playing
  ldx_imm(0x7f); // death music or d4 set on secondary buffer
  dynamic_ram_write(SND_SQUARE2_REG + 1, x);
  
HandleSquare1Music:
  ldy_zp_fz(MusicOffset_Square1); // is there a nonzero offset here?
  if (zero_flag) { goto HandleTriangleMusic; } // if not, skip ahead to the triangle channel
  dec_abs_fz(Squ1_NoteLenCounter); // decrement square 1 note length
  if (!zero_flag) { goto MiscSqu1MusicTasks; } // is it time for more data?
  
FetchSqu1MusicData:
  ldy_zp(MusicOffset_Square1); // increment square 1 music offset and fetch data
  inc_zp(MusicOffset_Square1);
  lda_indy_fzn(MusicData);
  if (!zero_flag) { goto Squ1NoteHandler; } // if nonzero, then skip this part
  lda_imm(0x83);
  apu_write(SND_SQUARE1_REG, a); // store some data into control regs for square 1
  lda_imm(0x94); // and fetch another byte of data, used to give
  dynamic_ram_write(SND_SQUARE1_REG + 1, a); // death music its unique sound
  ram[AltRegContentFlag] = a;
  goto FetchSqu1MusicData; // unconditional branch
  
Squ1NoteHandler:
  cpu_call_begin(0xf7de); AlternateLengthHandler(); cpu_call_end();
  ram[Squ1_NoteLenCounter] = a; // save contents of A in square 1 note counter
  ldy_zp_fz(Square1SoundBuffer); // is there a sound playing on square 1?
  if (!zero_flag) { goto HandleTriangleMusic; }
  txa();
  and_imm_fzn(0b00111110); // change saved data to appropriate note format
  cpu_call_begin(0xf7eb); SetFreq_Squ1(); cpu_call_end(); // play the note
  if (zero_flag) { goto SkipCtrlL; }
  cpu_call_begin(0xf7f0); LoadControlRegs(); cpu_call_end();
  
SkipCtrlL:
  ram[Squ1_EnvelopeDataCtrl] = a; // save envelope offset
  cpu_call_begin(0xf7f6); Dump_Squ1_Regs(); cpu_call_end();
  
MiscSqu1MusicTasks:
  lda_zp_fz(Square1SoundBuffer); // is there a sound playing on square 1?
  if (!zero_flag) { goto HandleTriangleMusic; }
  lda_abs(EventMusicBuffer); // check for death music or d4 set on secondary buffer
  and_imm_fz(0b10010001);
  if (!zero_flag) { goto DeathMAltReg; }
  ldy_abs_fzn(Squ1_EnvelopeDataCtrl); // check saved envelope offset
  if (zero_flag) { goto NoDecEnv2; }
  dec_abs_fzn(Squ1_EnvelopeDataCtrl); // decrement unless already zero
  
NoDecEnv2:
  cpu_call_begin(0xf80c); LoadEnvelopeData(); cpu_call_end(); // do a load of envelope data
  apu_write(SND_SQUARE1_REG, a); // based on offset set by first load
  
DeathMAltReg:
  lda_abs_fz(AltRegContentFlag); // check for alternate control reg data
  if (!zero_flag) { goto DoAltLoad; }
  lda_imm(0x7f); // load this value if zero, the alternate value
  
DoAltLoad:
  dynamic_ram_write(SND_SQUARE1_REG + 1, a); // if nonzero, and let's move on
  
HandleTriangleMusic:
  lda_zp(MusicOffset_Triangle);
  dec_abs_fz(Tri_NoteLenCounter); // decrement triangle note length
  if (!zero_flag) { goto HandleNoiseMusic; } // is it time for more data?
  ldy_zp(MusicOffset_Triangle); // increment square 1 music offset and fetch data
  inc_zp(MusicOffset_Triangle);
  lda_indy_fzn(MusicData);
  if (zero_flag) { goto LoadTriCtrlReg; } // if zero, skip all this and move on to noise 
  if (!neg_flag) { goto TriNoteHandler; } // if non-negative, data is note
  cpu_call_begin(0xf82d); ProcessLengthData(); cpu_call_end(); // otherwise, it is length data
  ram[Tri_NoteLenBuffer] = a; // save contents of A
  lda_imm(0x1f);
  apu_write(SND_TRIANGLE_REG, a); // load some default data for triangle control reg
  ldy_zp(MusicOffset_Triangle); // fetch another byte
  inc_zp(MusicOffset_Triangle);
  lda_indy_fzn(MusicData);
  if (zero_flag) { goto LoadTriCtrlReg; } // check once more for nonzero data
  
TriNoteHandler:
  cpu_call_begin(0xf840); SetFreq_Tri(); cpu_call_end();
  ldx_abs(Tri_NoteLenBuffer); // save length in triangle note counter
  ram[Tri_NoteLenCounter] = x;
  lda_abs(EventMusicBuffer);
  and_imm_fz(0b01101110); // check for death music or d4 set on secondary buffer
  if (!zero_flag) { goto NotDOrD4; } // if playing any other secondary, skip primary buffer check
  lda_zp(AreaMusicBuffer); // check primary buffer for water or castle level music
  and_imm_fz(0b00001010);
  if (zero_flag) { goto HandleNoiseMusic; } // if playing any other primary, or death or d4, go on to noise routine
  
NotDOrD4:
  txa(); // if playing water or castle music or any secondary
  cmp_imm_fc(0x12); // besides death music or d4 set, check length of note
  if (carry_flag) { goto LongN; }
  lda_abs(EventMusicBuffer); // check for win castle music again if not playing a long note
  and_imm_fz(EndOfCastleMusic);
  if (zero_flag) { goto MediN; }
  lda_imm(0xf); // load value $0f if playing the win castle music and playing a short
  goto LoadTriCtrlReg; // note, load value $1f if playing water or castle level music or any
  
MediN:
  lda_imm(0x1f); // secondary besides death and d4 except win castle or win castle and playing
  goto LoadTriCtrlReg; // a short note, and load value $ff if playing a long note on water, castle
  
LongN:
  lda_imm(0xff); // or any secondary (including win castle) except death and d4
  
LoadTriCtrlReg:
  apu_write(SND_TRIANGLE_REG, a); // save final contents of A into control reg for triangle
  
HandleNoiseMusic:
  lda_zp(AreaMusicBuffer); // check if playing underground or castle music
  and_imm_fzn(0b11110011);
  if (zero_flag) { return; } // if so, skip the noise routine
  dec_abs_fzn(Noise_BeatLenCounter); // decrement noise beat length
  if (!zero_flag) { return; } // is it time for more data?
  
FetchNoiseBeatData:
  ldy_abs(MusicOffset_Noise); // increment noise beat offset and fetch data
  inc_abs(MusicOffset_Noise);
  lda_indy_fzn(MusicData); // get noise beat data, if nonzero, branch to handle
  if (!zero_flag) { goto NoiseBeatHandler; }
  lda_abs_fzn(NoiseDataLoopbackOfs); // if data is zero, reload original noise beat offset
  ram[MusicOffset_Noise] = a; // and loopback next time around
  if (!zero_flag) { goto FetchNoiseBeatData; } // unconditional branch
  
NoiseBeatHandler:
  cpu_call_begin(0xf88c); AlternateLengthHandler(); cpu_call_end();
  ram[Noise_BeatLenCounter] = a; // store length in noise beat counter
  txa();
  and_imm_fz(0b00111110); // reload data and erase length bits
  if (zero_flag) { goto SilentBeat; } // if no beat data, silence
  cmp_imm_fcz(0x30); // check the beat data and play the appropriate
  if (zero_flag) { goto LongBeat; } // noise accordingly
  cmp_imm_fcz(0x20);
  if (zero_flag) { goto StrongBeat; }
  and_imm_fz(0b00010000);
  if (zero_flag) { goto SilentBeat; }
  lda_imm(0x1c); // short beat data
  ldx_imm(0x3);
  ldy_imm_fzn(0x18);
  goto PlayBeat;
  
StrongBeat:
  lda_imm(0x1c); // strong beat data
  ldx_imm(0xc);
  ldy_imm_fzn(0x18);
  goto PlayBeat;
  
LongBeat:
  lda_imm(0x1c); // long beat data
  ldx_imm(0x3);
  ldy_imm_fzn(0x58);
  goto PlayBeat;
  
SilentBeat:
  lda_imm_fzn(0x10); // silence
  
PlayBeat:
  apu_write(SND_NOISE_REG, a); // load beat data into noise regs
  dynamic_ram_write(SND_NOISE_REG + 2, x);
  dynamic_ram_write(SND_NOISE_REG + 3, y);
  // ExitMusicHandler:
  return;
}

void AlternateLengthHandler(void) {
  tax(); // save a copy of original byte into X
  ror_acc_fc(); // save LSB from original byte into carry
  txa(); // reload original byte and rotate three times
  rol_acc_fc(); // turning xx00000x into 00000xxx, with the
  rol_acc_fc(); // bit in carry as the MSB here
  rol_acc();
  ProcessLengthData(); // fallthrough
  return;
}

void ProcessLengthData(void) {
  and_imm(0b00000111); // clear all but the three LSBs
  carry_flag = false;
  adc_zp_fc(0xf0); // add offset loaded from first header byte
  adc_abs_fc(NoteLengthTblAdder); // add extra if time running out music
  tay();
  lda_absy_fzn(MusicLengthLookupTbl); // load length
  return;
}

void LoadControlRegs(void) {
  lda_abs(EventMusicBuffer); // check secondary buffer for win castle music
  and_imm_fz(EndOfCastleMusic);
  if (!zero_flag) {
    lda_imm(0x4); // this value is only used for win castle music
    goto AllMus; // unconditional branch
  }
  // NotECstlM:
  lda_zp(AreaMusicBuffer);
  and_imm_fz(0b01111101); // check primary buffer for water music
  if (!zero_flag) {
    lda_imm(0x8); // this is the default value for all other music
    goto AllMus;
  }
  // WaterMus:
  lda_imm(0x28); // this value is used for water music and all other event music
  
AllMus:
  ldx_imm(0x82); // load contents of other sound regs for square 2
  ldy_imm_fzn(0x7f);
  return;
}

void LoadEnvelopeData(void) {
  lda_abs(EventMusicBuffer); // check secondary buffer for win castle music
  and_imm_fz(EndOfCastleMusic);
  if (!zero_flag) {
    lda_absy_fzn(EndOfCastleMusicEnvData); // load data from offset for win castle music
    return;
  }
  // LoadUsualEnvData:
  lda_zp(AreaMusicBuffer); // check primary buffer for water music
  and_imm_fz(0b01111101);
  if (!zero_flag) {
    lda_absy_fzn(AreaMusicEnvData); // load default data from offset for all other music
    return;
  }
  // LoadWaterEventMusEnvData:
  lda_absy_fzn(WaterEventMusEnvData); // load data from offset for water music and all other event music
  return;
  // --------------------------------
  // music header offsets
}

