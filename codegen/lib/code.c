#include "code.h"

// SMBDIS.ASM - A COMPREHENSIVE SUPER MARIO BROS. DISASSEMBLY
// by doppelganger (doppelheathen@gmail.com)
// This file is provided for your own use as-is.  It will require the character rom data
// and an iNES file header to get it to work.
// There are so many people I have to thank for this, that taking all the credit for
// myself would be an unforgivable act of arrogance. Without their help this would
// probably not be possible.  So I thank all the peeps in the nesdev scene whose insight into
// the 6502 and the NES helped me learn how it works (you guys know who you are, there's no 
// way I could have done this without your help), as well as the authors of x816 and SMB 
// Utility, and the reverse-engineers who did the original Super Mario Bros. Hacking Project, 
// which I compared notes with but did not copy from.  Last but certainly not least, I thank
// Nintendo for creating this game and the NES, without which this disassembly would
// only be theory.
// Assembles with x816.
// -------------------------------------------------------------------------------------
// DEFINES
// NES specific hardware defines
//  GAME SPECIFIC DEFINES
// sound related defines
// -------------------------------------------------------------------------------------
// CONSTANTS
// sound effects constants
// music constants
// enemy object constants 
// other constants
// -------------------------------------------------------------------------------------
// DIRECTIVES
// -------------------------------------------------------------------------------------

// Inline-table dispatch proven by evaluating JumpEngine; its effects run here unchanged.
static uint16_t JumpEngine(uint16_t return_address) {
  cpu_call_begin(return_address);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // shift bit from contents of A
  y = a; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  a = ram[0x100 + ++sp]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // pull saved return address from stack
  ram[0x4] = a; // save to indirect
  a = ram[0x100 + ++sp]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
  ram[0x5] = a;
  y++; zero_flag = y == 0; neg_flag = (y & 0x80) != 0;
  a = read_byte(indirect_y_addr(0x4)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // load pointer from indirect
  ram[0x6] = a; // note that if an RTS is performed in next routine
  y++; zero_flag = y == 0; neg_flag = (y & 0x80) != 0; // it will return to the execution before the sub
  a = read_byte(indirect_y_addr(0x4)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // that called this routine
  ram[0x7] = a;
  return cpu_dispatch_target = read_indirect_word(0x6);
}

// Start: native implementation, not generated.
// It may read: none.
// It must produce: none.

// NonMaskableInterrupt: native implementation, not generated.
// It may read: none.
// It must produce: none.

// -------------------------------------------------------------------------------------
void PauseRoutine(void) {
  a = ram[OperMode]; // are we in victory mode?
  // if so, go ahead
  if (a != VictoryModeValue) {
    // are we in game mode?
    // if not, leave
    if (a != GameModeValue) {
      return;
    }
    a = ram[OperMode_Task]; // if we are in game mode, are we running game engine?
    // if not, leave
    if (a != 0x3) {
      return;
    }
  }
  // ChkPauseTimer:
  a = ram[GamePauseTimer]; zero_flag = a == 0; // check if pause timer is still counting down
  if (!zero_flag) {
    ram[GamePauseTimer]--; // if so, decrement and leave
    return;
  }
  // ChkStart:
  a = ram[SavedJoypad1Bits]; // check to see if start is pressed
  a &= Start_Button; zero_flag = a == 0; // on controller 1
  if (!zero_flag) {
    a = ram[GamePauseStatus]; // check to see if timer flag is set
    a &= 0b10000000; zero_flag = a == 0; // and if so, do not reset timer (residual,
    // joypad reading routine makes this unnecessary)
    if (!zero_flag) {
      return;
    }
    ram[GamePauseTimer] = 0x2b; // set pause timer
    a = ram[GamePauseStatus];
    y = a;
    y++; // set pause sfx queue for next pause mode
    ram[PauseSoundQueue] = y;
    a ^= 0b00000001; // invert d0 and set d7
    a |= 0b10000000; zero_flag = a == 0;
    if (!zero_flag) { goto SetPause; } // unconditional branch
  }
  // ClrPauseTimer:
  a = ram[GamePauseStatus]; // clear timer flag if timer is at zero and start button
  a &= 0b01111111; // is not pressed
  
SetPause:
  ram[GamePauseStatus] = a;
  // ExitPause:
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used for preset value
void SpriteShuffler(void) {
  y = ram[AreaType]; // load level type, likely residual code
  ram[0x0] = 0x28; // load preset value which will put it at; sprite #10
  x = 0xe; // start at the end of OAM data offsets
  do {
    // ShuffleLoop:
    a = ram[SprDataOffset + x]; // check for offset value against
    carry_flag = a >= ram[0x0]; // the preset value
    // Original branch: if less, skip this part
    if (carry_flag) {
      y = ram[SprShuffleAmtOffset]; // get current offset to preset value we want to add
      carry_flag = false;
      { uint16_t sum = a + ram[SprShuffleAmt + y] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // get shuffle amount, add to current sprite offset
      // Original branch: if not exceeded $ff, skip second add
      if (carry_flag) {
        carry_flag = false;
        a = (uint8_t)(a + ram[0x0] + carry_flag); // otherwise add preset value $28 to offset
      }
      // StrSprOffset:
      ram[SprDataOffset + x] = a; // store new offset here or old one if branched to here
    }
    // NextSprOffset:
    x--; neg_flag = (x & 0x80) != 0; // move backwards to next one
  } while (!neg_flag);
  x = ram[SprShuffleAmtOffset]; // load offset
  x++;
  // check if offset + 1 goes to 3
  // Original branch: if offset + 1 not 3, store
  if (x == 0x3) {
    x = 0x0; // otherwise, init to 0
  }
  // SetAmtOffset:
  ram[SprShuffleAmtOffset] = x;
  x = 0x8; // load offsets for values and storage
  y = 0x2;
  do {
    // SetMiscOffset:
    a = ram[(SprDataOffset + 5) + y]; // load one of three OAM data offsets
    ram[(Misc_SprDataOffset - 2) + x] = a; // store first one unmodified, but
    carry_flag = false; // add eight to the second and eight
    a = (uint8_t)(a + 0x8 + carry_flag); // more to the third one
    ram[(Misc_SprDataOffset - 1) + x] = a; // note that due to the way X is set up,
    carry_flag = false; // this code loads into the misc sprite offsets
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[Misc_SprDataOffset + x] = a;
    x--;
    x--;
    x--;
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag); // do this until all misc spr offsets are loaded
  return;
}

// -------------------------------------------------------------------------------------
void OperModeExecutionTree(void) {
  a = ram[OperMode]; // this is the heart of the entire program,
  switch (JumpEngine(0x8217)) {
    case 0x8231: TitleScreenMode(); return;
    case 0xaedc: GameMode(); return;
    case 0x838b: VictoryMode(); return;
    case 0x9218: GameOverMode(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: none; out: y
void MoveAllSpritesOffscreen(void) {
  y = 0x0; // this routine moves all sprites off the screen
  
  loc_33317(); return; // BIT instruction opcode
}

void MoveSpritesOffscreen(void) {
  y = 0x4; // this routine moves all but sprite 0
  loc_33317(); // fallthrough
  return;
}

// in: y; out: y
void loc_33317(void) {
  a = 0xf8; // off the screen
  do {
    // SprInitLoop:
    ram[Sprite_Y_Position + y] = a; // write 248 into OAM data's Y coordinate
    y++; // which will move it off the screen
    y++;
    y++;
    y++; zero_flag = y == 0;
  } while (!zero_flag);
  return;
}

// -------------------------------------------------------------------------------------
void TitleScreenMode(void) {
  a = ram[OperMode_Task];
  switch (JumpEngine(0x8236)) {
    case 0x8fcf: InitializeGame(); return;
    case 0x8567: ScreenRoutines(); return;
    case 0x9061: PrimaryGameSetup(); return;
    case 0x8245: GameMenuRoutine(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void GameMenuRoutine(void) {
  y = 0x0;
  a = ram[SavedJoypad1Bits]; // check to see if either player pressed
  a |= ram[SavedJoypad2Bits]; // only the start button (either joypad)
  if (a != Start_Button) {
    zero_flag = a == (A_Button + Start_Button); // check to see if A + start was pressed
    if (!zero_flag) { goto ChkSelect; } // if not, branch to check select button
  }
  // StartGame:
  goto ChkContinue; // if either start or A + start, execute here
  
ChkSelect:
  // check to see if the select button was pressed
  // Original branch: if so, branch reset demo timer
  if (a != Select_Button) {
    x = ram[DemoTimer]; zero_flag = x == 0; // otherwise check demo timer
    // Original branch: if demo timer not expired, branch to check world selection
    if (zero_flag) {
      ram[SelectTimer] = a; // set controller bits here if running demo
      CALL(DemoEngine, 0x8266); // run through the demo actions
      if (carry_flag) { goto ResetTitle; } // if carry flag set, demo over, thus branch
      goto RunDemo; // otherwise, run game engine for demo
    }
    // ChkWorldSel:
    x = ram[WorldSelectEnableFlag]; zero_flag = x == 0; // check to see if world selection has been enabled
    if (zero_flag) { goto NullJoypad; }
    zero_flag = a == B_Button; // if so, check to see if the B button was pressed
    if (!zero_flag) { goto NullJoypad; }
    y++; // if so, increment Y and execute same code as select
  }
  // SelectBLogic:
  a = ram[DemoTimer]; zero_flag = a == 0; // if select or B pressed, check demo timer one last time
  if (zero_flag) { goto ResetTitle; } // if demo timer expired, branch to reset title screen mode
  ram[DemoTimer] = 0x18; // otherwise reset demo timer
  a = ram[SelectTimer]; zero_flag = a == 0; // check select/B button timer
  // Original branch: if not expired, branch
  if (zero_flag) {
    ram[SelectTimer] = 0x10; // otherwise reset select button timer
    // was the B button pressed earlier?  if so, branch
    // Original branch: note this will not be run if world selection is disabled
    if (y != 0x1) {
      a = ram[NumberOfPlayers]; // if no, must have been the select button, therefore
      a ^= 0b00000001; // change number of players and draw icon accordingly
      ram[NumberOfPlayers] = a;
      CALL(DrawMushroomIcon, 0x8298);
      goto NullJoypad;
    }
    // IncWorldSel:
    x = ram[WorldSelectNumber]; // increment world select number
    x++;
    a = x;
    a &= 0b00000111; // mask out higher bits
    ram[WorldSelectNumber] = a; // store as current world select number
    CALL(GoContinue, 0x82a8);
    do {
      // UpdateShroom:
      ram[(VRAM_Buffer1 - 1) + x] = ROM(WSelectBufferTemplate + x); // write template for world select in vram buffer; do this until all bytes are written
      x++;
      neg_flag = ((x - 0x6) & 0x80) != 0;
    } while (neg_flag);
    y = ram[WorldNumber]; // get world number from variable and increment for
    y++; // proper display, and put in blank byte before
    ram[(VRAM_Buffer1 + 3)] = y; // null terminator
  }
  
NullJoypad:
  ram[SavedJoypad1Bits] = 0x0; // clear joypad bits for player 1
  
RunDemo:
  CALL(GameCoreRoutine, 0x82c2); // run game engine
  a = ram[GameEngineSubroutine]; // check to see if we're running lose life routine
  // if not, do not do all the resetting below
  if (a != 0x6) {
    return;
  }
  
ResetTitle:
  a = 0x0; // reset game modes, disable
  ram[OperMode] = a; // sprite 0 check and disable
  ram[OperMode_Task] = a; // screen output
  ram[Sprite0HitDetectFlag] = a;
  ram[DisableScreenFlag]++;
  return;
  
ChkContinue:
  y = ram[DemoTimer]; zero_flag = y == 0; // if timer for demo has expired, reset modes
  if (zero_flag) { goto ResetTitle; }
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // check to see if A button was also pushed
  // Original branch: if not, don't load continue function's world number
  if (carry_flag) {
    a = ram[ContinueWorld]; // load previously saved world number for secret
    CALL(GoContinue, 0x82e5); // continue function when pressing A + start
  }
  // StartWorld1:
  CALL(LoadAreaPointer, 0x82e8);
  ram[Hidden1UpFlag]++; // set 1-up box flag for both players
  ram[OffScr_Hidden1UpFlag]++;
  ram[FetchNewGameTimerFlag]++; // set fetch new game timer flag
  ram[OperMode]++; // set next game mode
  ram[PrimaryHardMode] = ram[WorldSelectEnableFlag]; // if world select flag is on, then primary; hard mode must be on as well
  a = 0x0;
  ram[OperMode_Task] = a; // set game mode here, and clear demo timer
  ram[DemoTimer] = a;
  x = 0x17;
  a = 0x0;
  do {
    // InitScores:
    ram[(ScoreAndCoinDisplay + x) & 0x7ff] = a; // clear player scores and coin displays
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  // ExitMenu:
  return;
}

// in: a; out: x
void GoContinue(void) {
  ram[WorldNumber] = a; // start both players at the first area
  ram[OffScr_WorldNumber] = a; // of the previously saved world number
  x = 0x0; // note that on power-up using this function
  ram[AreaNumber] = x; // will make no difference
  ram[OffScr_AreaNumber] = x;
  return;
}

// -------------------------------------------------------------------------------------
void DrawMushroomIcon(void) {
  y = 0x7; // read eight bytes to be read by transfer routine
  do {
    // IconDataRead:
    ram[(VRAM_Buffer1 - 1) + y] = ROM(MushroomIconData + y); // note that the default position is set for a; 1-player game
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  a = ram[NumberOfPlayers]; zero_flag = a == 0; // check number of players
  // if set to 1-player game, we're done
  if (zero_flag) {
    return;
  }
  ram[(VRAM_Buffer1 + 3)] = 0x24; // otherwise, load blank tile in 1-player position
  ram[(VRAM_Buffer1 + 5)] = 0xce; // then load shroom icon tile in 2-player position
  // ExitIcon:
  return;
}

// -------------------------------------------------------------------------------------
// in: none; out: carry
void DemoEngine(void) {
  x = ram[DemoAction]; // load current demo action
  a = ram[DemoActionTimer]; zero_flag = a == 0; // load current action timer
  // Original branch: if timer still counting down, skip
  if (zero_flag) {
    x++;
    ram[DemoAction]++; // if expired, increment action, X, and
    carry_flag = true; // set carry by default for demo over
    a = ROM((DemoTimingData - 1) + x); zero_flag = a == 0; // get next timer
    ram[DemoActionTimer] = a; // store as current timer
    // if timer already at zero, skip
    if (zero_flag) {
      return;
    }
  }
  // DoAction:
  ram[SavedJoypad1Bits] = ROM((DemoActionData - 1) + x); // get and perform action (current or next)
  ram[DemoActionTimer]--; // decrement action timer
  carry_flag = false; // clear carry if demo still going
  // DemoOver:
  return;
}

// -------------------------------------------------------------------------------------
void VictoryMode(void) {
  CALL(VictoryModeSubroutines, 0x838d); // run victory mode subroutines
  a = ram[OperMode_Task]; zero_flag = a == 0; // get current task of victory mode
  // Original branch: if on bridge collapse, skip enemy processing
  if (!zero_flag) {
    x = 0x0;
    ram[ObjectOffset] = x; // otherwise reset enemy object offset 
    CALL(EnemiesAndLoopsCore, 0x8399); // and run enemy code
  }
  // AutoPlayer:
  CALL(RelativePlayerPosition, 0x839c); // get player's relative coordinates
  PlayerGfxHandler(); return; // draw the player, then leave
}

void VictoryModeSubroutines(void) {
  a = ram[OperMode_Task];
  switch (JumpEngine(0x83a5)) {
    case 0xcfec: BridgeCollapse(); return;
    case 0x83b0: SetupVictoryMode(); return;
    case 0x83bd: PlayerVictoryWalk(); return;
    case 0x83f6: PrintVictoryMessages(); return;
    case 0x8461: PlayerEndWorld(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void SetupVictoryMode(void) {
  x = ram[ScreenRight_PageLoc]; // get page location of right side of screen
  x++; // increment to next page
  ram[DestinationPageLoc] = x; // store here
  ram[EventMusicQueue] = EndOfCastleMusic; // play win castle music
  IncModeTask_B(); return; // jump to set next major task in victory mode
}

// -------------------------------------------------------------------------------------
void PlayerVictoryWalk(void) {
  y = 0x0; // set value here to not walk player by default
  ram[VictoryWalkControl] = y;
  a = ram[Player_PageLoc]; // get player's page location
  zero_flag = a == ram[DestinationPageLoc]; // compare with destination page location
  // Original branch: if page locations don't match, branch
  if (zero_flag) {
    a = ram[Player_X_Position]; // otherwise get player's horizontal position
    carry_flag = a >= 0x60; // compare with preset horizontal position
    if (carry_flag) { goto DontWalk; } // if still on other page, branch ahead
  }
  // PerformWalk:
  ram[VictoryWalkControl]++; // otherwise increment value and Y
  y++; // note Y will be used to walk the player
  
DontWalk:
  a = y; // put contents of Y in A and
  CALL(AutoControlPlayer, 0x83d3); // use A to move player to the right or not
  a = ram[ScreenLeft_PageLoc]; // check page location of left side of screen
  zero_flag = a == ram[DestinationPageLoc]; // against set value here
  // Original branch: branch if equal to change modes if necessary
  if (!zero_flag) {
    a = ram[ScrollFractional];
    carry_flag = false; // do fixed point math on fractional part of scroll
    { uint16_t sum = a + 0x80 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    ram[ScrollFractional] = a; // save fractional movement amount
    a = 0x1; // set 1 pixel per frame
    a = (uint8_t)(a + 0x0 + carry_flag); // add carry from previous addition
    y = a; // use as scroll amount
    CALL(ScrollScreen, 0x83eb); // do sub to scroll the screen
    CALL(UpdScrollVar, 0x83ee); // do another sub to update screen and scroll variables
    ram[VictoryWalkControl]++; // increment value to stay in this routine
  }
  // ExitVWalk:
  a = ram[VictoryWalkControl]; zero_flag = a == 0; // load value set here
  // if zero, branch to change modes
  if (zero_flag) {
    IncModeTask_A();
    return;
  }
  return; // otherwise leave
}

// -------------------------------------------------------------------------------------
void PrintVictoryMessages(void) {
  a = ram[SecondaryMsgCounter]; zero_flag = a == 0; // load secondary message counter
  // Original branch: if set, branch to increment message counters
  if (zero_flag) {
    a = ram[PrimaryMsgCounter]; zero_flag = a == 0; // otherwise load primary message counter
    // Original branch: if set to zero, branch to print first message
    if (!zero_flag) {
      carry_flag = a >= 0x9; // if at 9 or above, branch elsewhere (this comparison
      if (carry_flag) { goto IncMsgCounter; } // is residual code, counter never reaches 9)
      y = ram[WorldNumber]; // check world number
      // Original branch: if not at world 8, skip to next part
      if (y == World8) {
        carry_flag = a >= 0x3; // check primary message counter again
        if (!carry_flag) { goto IncMsgCounter; } // if not at 3 yet (world 8 only), branch to increment
        a = (uint8_t)(a - 0x1 - !carry_flag); // otherwise subtract one
        goto ThankPlayer; // and skip to next part
      }
      // MRetainerMsg:
      carry_flag = a >= 0x2; // check primary message counter
      if (!carry_flag) { goto IncMsgCounter; } // if not at 2 yet (world 1-7 only), branch
    }
    
ThankPlayer:
    y = a; zero_flag = y == 0; // put primary message counter into Y
    // Original branch: if counter nonzero, skip this part, do not print first message
    if (zero_flag) {
      a = ram[CurrentPlayer]; zero_flag = a == 0; // otherwise get player currently on the screen
      if (zero_flag) { goto EvalForMusic; } // if mario, branch
      y++; zero_flag = y == 0; // otherwise increment Y once for luigi and
      if (!zero_flag) { goto EvalForMusic; } // do an unconditional branch to the same place
    }
    // SecondPartMsg:
    y++; // increment Y to do world 8's message
    a = ram[WorldNumber];
    // check world number
    // Original branch: if at world 8, branch to next part
    if (a != World8) {
      y--; // otherwise decrement Y for world 1-7's message
      carry_flag = y >= 0x4; // if counter at 4 (world 1-7 only)
      if (carry_flag) { goto SetEndTimer; } // branch to set victory end timer
      carry_flag = y >= 0x3; // if counter at 3 (world 1-7 only)
      if (carry_flag) { goto IncMsgCounter; } // branch to keep counting
    }
    
EvalForMusic:
    // if counter not yet at 3 (world 8 only), branch
    // Original branch: to print message only (note world 1-7 will only
    if (y == 0x3) {
      ram[EventMusicQueue] = VictoryMusic; // reach this code if counter = 0, and will always branch); otherwise load victory music first (world 8 only)
    }
    // PrintMsg:
    a = y; // put primary message counter in A
    carry_flag = false; // add $0c or 12 to counter thus giving an appropriate value,
    a = (uint8_t)(a + 0xc + carry_flag); // ($0c-$0d = first), ($0e = world 1-7's), ($0f-$12 = world 8's)
    ram[VRAM_Buffer_AddrCtrl] = a; // write message counter to vram address controller
  }
  
IncMsgCounter:
  a = ram[SecondaryMsgCounter];
  carry_flag = false;
  { uint16_t sum = a + 0x4 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add four to secondary message counter
  ram[SecondaryMsgCounter] = a;
  a = ram[PrimaryMsgCounter];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to primary message counter
  ram[PrimaryMsgCounter] = a;
  carry_flag = a >= 0x7; // check primary counter one more time
  
SetEndTimer:
  // if not reached value yet, branch to leave
  if (!carry_flag) {
    return;
  }
  ram[WorldEndTimer] = 0x6; // otherwise set world end timer
  IncModeTask_A(); // fallthrough
  return;
}

void IncModeTask_A(void) {
  ram[OperMode_Task]++; // move onto next task in mode
  // ExitMsgs:
  return; // leave
}

// -------------------------------------------------------------------------------------
void PlayerEndWorld(void) {
  a = ram[WorldEndTimer]; zero_flag = a == 0; // check to see if world end timer expired
  // branch to leave if not
  if (!zero_flag) {
    return;
  }
  y = ram[WorldNumber]; // check world number
  // if on world 8, player is done with game, 
  // Original branch: thus branch to read controller
  if (y < World8) {
    a = 0x0;
    ram[AreaNumber] = a; // otherwise initialize area number used as offset
    ram[LevelNumber] = a; // and level number control to start at area 1
    ram[OperMode_Task] = a; // initialize secondary mode of operation
    ram[WorldNumber]++; // increment world number to move onto the next world
    CALL(LoadAreaPointer, 0x847d); // get area address offset for the next area
    ram[FetchNewGameTimerFlag]++; // set flag to load game timer from header
    ram[OperMode] = GameModeValue; // set mode of operation to game mode
    // EndExitOne:
    return; // and leave
  }
  // EndChkBButton:
  a = ram[SavedJoypad1Bits];
  a |= ram[SavedJoypad2Bits]; // check to see if B button was pressed on
  a &= B_Button; zero_flag = a == 0; // either controller
  // branch to leave if not
  if (zero_flag) {
    return;
  }
  ram[WorldSelectEnableFlag] = 0x1; // otherwise set world selection flag
  ram[NumberofLives] = 0xff; // remove onscreen player's lives
  CALL(TerminateGame, 0x849d); // do sub to continue other player or end game
  // EndExitTwo:
  return; // leave
}

// -------------------------------------------------------------------------------------
// data is used as tiles for numbers
// that appear when you defeat enemies
// in: x; out: x
void FloateyNumbersRoutine(void) {
  a = ram[FloateyNum_Control + x]; zero_flag = a == 0; // load control for floatey number
  // if zero, branch to leave
  if (zero_flag) {
    return;
  }
  // if less than $0b, branch
  if (a >= 0xb) {
    a = 0xb; // otherwise set to $0b, thus keeping
    ram[FloateyNum_Control + x] = a; // it in range
  }
  // ChkNumTimer:
  y = a; // use as Y
  a = ram[FloateyNum_Timer + x]; zero_flag = a == 0; // check value here
  // Original branch: if nonzero, branch ahead
  if (zero_flag) {
    ram[FloateyNum_Control + x] = a; // initialize floatey number control and leave
    return;
  }
  // DecNumTimer:
  ram[FloateyNum_Timer + x]--; // decrement value here
  // if not reached a certain point, branch  
  if (a == 0x2b) {
    // check offset for $0b
    // Original branch: branch ahead if not found
    if (y == 0xb) {
      ram[NumberofLives]++; // give player one extra life (1-up)
      ram[Square2SoundQueue] = Sfx_ExtraLife; // and play the 1-up sound
    }
    // LoadNumTiles:
    a = ROM(ScoreUpdateData + y); // load point value here
    a = a >> 1; // move high nybble to low
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    x = a; // use as X offset, essentially the digit
    a = ROM(ScoreUpdateData + y); // load again and this time
    a &= 0b00001111; // mask out the high nybble
    ram[DigitModifier + x] = a; // store as amount to add to the digit
    CALL(AddToScore, 0x84ff); // update the score accordingly
  }
  // ChkTallEnemy:
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset for enemy object
  a = ram[(uint8_t)(Enemy_ID + x)]; // get enemy object identifier
  // Original branch: branch if spiny
  if (a != Spiny) {
    zero_flag = a == PiranhaPlant;
    if (zero_flag) { goto FloateyPart; } // branch if piranha plant
    // Original branch: branch elsewhere if hammer bro
    if (a != HammerBro) {
      zero_flag = a == GreyCheepCheep;
      if (zero_flag) { goto FloateyPart; } // branch if cheep-cheep of either color
      zero_flag = a == RedCheepCheep;
      if (zero_flag) { goto FloateyPart; }
      carry_flag = a >= TallEnemy;
      if (carry_flag) { goto GetAltOffset; } // branch elsewhere if enemy object => $09
      a = ram[(uint8_t)(Enemy_State + x)];
      carry_flag = a >= 0x2; // if enemy state defeated or otherwise
      if (carry_flag) { goto FloateyPart; } // $02 or greater, branch beyond this part
    }
    
GetAltOffset:
    x = ram[SprDataOffset_Ctrl]; // load some kind of control bit
    y = ram[Alt_SprDataOffset + x]; // get alternate OAM data offset
    x = ram[ObjectOffset]; // get enemy object offset again
  }
  
FloateyPart:
  a = ram[FloateyNum_Y_Pos + x]; // get vertical coordinate for
  carry_flag = a >= 0x18; // floatey number, if coordinate in the
  // Original branch: status bar, branch
  if (carry_flag) {
    { uint16_t diff = (uint16_t)(a - 0x1 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[FloateyNum_Y_Pos + x] = a; // otherwise subtract one and store as new
  }
  // SetupNumSpr:
  a = ram[FloateyNum_Y_Pos + x]; // get vertical coordinate
  { uint16_t diff = (uint16_t)(a - 0x8 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract eight and dump into the
  CALL(DumpTwoSpr, 0x853e); // left and right sprite's Y coordinates
  a = ram[FloateyNum_X_Pos + x]; // get horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store into X coordinate of left sprite
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels and store into X
  ram[(Sprite_X_Position + 4) + y] = a; // coordinate of right sprite
  a = 0x2;
  ram[Sprite_Attributes + y] = a; // set palette control in attribute bytes
  ram[(Sprite_Attributes + 4) + y] = a; // of left and right sprites
  a = ram[FloateyNum_Control + x];
  a = (uint8_t)(a << 1); // multiply our floatey number control by 2
  x = a; // and use as offset for look-up table
  ram[Sprite_Tilenumber + y] = ROM(FloateyNumTileData + x); // display first half of number of points
  ram[(Sprite_Tilenumber + 4) + y] = ROM((FloateyNumTileData + 1) + x); // display the second half
  x = ram[ObjectOffset]; // get enemy object offset and leave
  return;
}

// -------------------------------------------------------------------------------------
void ScreenRoutines(void) {
  a = ram[ScreenRoutineTask]; // run one of the following subroutines
  switch (JumpEngine(0x856c)) {
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
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void InitScreen(void) {
  CALL(MoveAllSpritesOffscreen, 0x858d); // initialize all sprites including sprite #0
  CALL(InitializeNameTables, 0x8590); // and erase both name and attribute tables
  a = ram[OperMode]; zero_flag = a == 0;
  // if mode still 0, do not load
  if (zero_flag) {
    NextSubtask();
    return;
  }
  x = 0x3; // into buffer pointer
  SetVRAMAddr_A(); return;
}

// -------------------------------------------------------------------------------------
void SetupIntermediate(void) {
  a = ram[BackgroundColorCtrl]; // save current background color control
  ram[0x100 + sp--] = a; // and player status to stack
  a = ram[PlayerStatus];
  ram[0x100 + sp--] = a;
  ram[PlayerStatus] = 0x0; // set background color to black; and player status to not fiery
  ram[BackgroundColorCtrl] = 0x2; // this is the ONLY time background color control; is set to less than 4
  CALL(GetPlayerColors, 0x85af);
  a = ram[0x100 + ++sp]; // we only execute this routine for
  ram[PlayerStatus] = a; // the intermediate lives display
  a = ram[0x100 + ++sp]; // and once we're done, we return bg
  ram[BackgroundColorCtrl] = a; // color ctrl and player status from stack
  IncSubtask(); return; // then move onto the next task
}

// -------------------------------------------------------------------------------------
void GetAreaPalette(void) {
  y = ram[AreaType]; // select appropriate palette to load
  x = ROM(AreaPalette + y); // based on area type
  SetVRAMAddr_A(); // fallthrough
  return;
}

// in: x; out: none
void SetVRAMAddr_A(void) {
  ram[VRAM_Buffer_AddrCtrl] = x; // store offset into buffer control
  NextSubtask(); // fallthrough
  return;
}

void NextSubtask(void) {
  IncSubtask(); return; // move onto next task
}

// -------------------------------------------------------------------------------------
// $00 - used as temp counter in GetPlayerColors
void GetBackgroundColor(void) {
  y = ram[BackgroundColorCtrl]; zero_flag = y == 0; // check background color control
  // Original branch: if not set, increment task and fetch palette
  if (!zero_flag) {
    ram[VRAM_Buffer_AddrCtrl] = ROM((BGColorCtrl_Addr - 4) + y); // put appropriate palette into vram; note that if set to 5-7, $0301 will not be read
  }
  // NoBGColor:
  ram[ScreenRoutineTask]++; // increment to next subtask and plod on through
  GetPlayerColors(); // fallthrough
  return;
}

// in: none; out: x
void GetPlayerColors(void) {
  x = ram[VRAM_Buffer1_Offset]; // get current buffer offset
  y = 0x0;
  a = ram[CurrentPlayer]; zero_flag = a == 0; // check which player is on the screen
  if (!zero_flag) {
    y = 0x4; // load offset for luigi
  }
  // ChkFiery:
  a = ram[PlayerStatus]; // check player status
  // Original branch: if fiery, load alternate offset for fiery player
  if (a == 0x2) {
    y = 0x8;
  }
  // StartClrGet:
  ram[0x0] = 0x3; // do four colors
  do {
    // ClrGetLoop:
    ram[(VRAM_Buffer1 + 3) + x] = ROM(PlayerColors + y); // fetch player colors and store them; in the buffer
    y++;
    x++;
    ram[0x0]--; neg_flag = (ram[0x0] & 0x80) != 0;
  } while (!neg_flag);
  x = ram[VRAM_Buffer1_Offset]; // load original offset from before
  y = ram[BackgroundColorCtrl]; zero_flag = y == 0; // if this value is four or greater, it will be set
  // Original branch: therefore use it as offset to background color
  if (zero_flag) {
    y = ram[AreaType]; // otherwise use area type bits from area offset as offset
  }
  // SetBGColor:
  ram[(VRAM_Buffer1 + 3) + x] = ROM(BackgroundColors + y); // to background color instead
  ram[VRAM_Buffer1 + x] = 0x3f; // set for sprite palette address; save to buffer
  ram[(VRAM_Buffer1 + 1) + x] = 0x10;
  ram[(VRAM_Buffer1 + 2) + x] = 0x4; // write length byte to buffer
  ram[(VRAM_Buffer1 + 7) + x] = 0x0; // now the null terminator
  a = x; // move the buffer pointer ahead 7 bytes
  carry_flag = false; // in case we want to write anything else later
  a = (uint8_t)(a + 0x7 + carry_flag);
  SetVRAMOffset(); // fallthrough
  return;
}

// in: a; out: none
void SetVRAMOffset(void) {
  ram[VRAM_Buffer1_Offset] = a; // store as new vram buffer offset
  return;
}

// -------------------------------------------------------------------------------------
void GetAlternatePalette1(void) {
  a = ram[AreaStyle]; // check for mushroom level style
  if (a != 0x1) {
    NoAltPal();
    return;
  }
  a = 0xb; // if found, load appropriate palette
  SetVRAMAddr_B(); // fallthrough
  return;
}

// in: a; out: none
void SetVRAMAddr_B(void) {
  ram[VRAM_Buffer_AddrCtrl] = a;
  NoAltPal(); // fallthrough
  return;
}

void NoAltPal(void) {
  IncSubtask(); return; // now onto the next task
}

// -------------------------------------------------------------------------------------
void WriteTopStatusLine(void) {
  a = 0x0; // select main status bar
  CALL(WriteGameText, 0x8656); // output it
  IncSubtask(); return; // onto the next task
}

// -------------------------------------------------------------------------------------
void WriteBottomStatusLine(void) {
  CALL(GetSBNybbles, 0x865c); // write player's score and coin tally to screen
  x = ram[VRAM_Buffer1_Offset];
  ram[VRAM_Buffer1 + x] = 0x20; // write address for world-area number on screen
  ram[(VRAM_Buffer1 + 1) + x] = 0x73;
  ram[(VRAM_Buffer1 + 2) + x] = 0x3; // write length for it
  y = ram[WorldNumber]; // first the world number
  y++;
  ram[(VRAM_Buffer1 + 3) + x] = y;
  ram[(VRAM_Buffer1 + 4) + x] = 0x28; // next the dash
  y = ram[LevelNumber]; // next the level number
  y++; // increment for proper number display
  ram[(VRAM_Buffer1 + 5) + x] = y;
  ram[(VRAM_Buffer1 + 6) + x] = 0x0; // put null terminator on
  a = x; // move the buffer offset up by 6 bytes
  carry_flag = false;
  a = (uint8_t)(a + 0x6 + carry_flag);
  ram[VRAM_Buffer1_Offset] = a;
  IncSubtask(); return;
}

// -------------------------------------------------------------------------------------
void DisplayTimeUp(void) {
  a = ram[GameTimerExpiredFlag]; zero_flag = a == 0; // if game timer not expired, increment task
  // Original branch: control 2 tasks forward, otherwise, stay here
  if (!zero_flag) {
    ram[GameTimerExpiredFlag] = 0x0; // reset timer expiration flag
    a = 0x2; // output time-up screen to buffer
    OutputInter(); return;
  }
  // NoTimeUp:
  ram[ScreenRoutineTask]++; // increment control task 2 tasks forward
  IncSubtask(); return;
}

// -------------------------------------------------------------------------------------
void DisplayIntermediate(void) {
  a = ram[OperMode]; zero_flag = a == 0; // check primary mode of operation
  // Original branch: if in title screen mode, skip this
  if (!zero_flag) {
    // are we in game over mode?
    // Original branch: if so, proceed to display game over screen
    if (a != GameOverModeValue) {
      a = ram[AltEntranceControl]; zero_flag = a == 0; // otherwise check for mode of alternate entry
      if (!zero_flag) { goto NoInter; } // and branch if found
      y = ram[AreaType]; // check if we are on castle level
      // and if so, branch (possibly residual)
      if (y != 0x3) {
        a = ram[DisableIntermediate]; zero_flag = a == 0; // if this flag is set, skip intermediate lives display
        if (!zero_flag) { goto NoInter; } // and jump to specific task, otherwise
      }
      // PlayerInter:
      CALL(DrawPlayer_Intermediate, 0x86c4); // put player in appropriate place for
      a = 0x1; // lives display, then output lives display to buffer
      OutputInter(); // fallthrough
      return;
    }
    // GameOverInter:
    ram[ScreenTimer] = 0x12; // set screen timer
    a = 0x3; // output game over screen to buffer
    CALL(WriteGameText, 0x86dc);
    IncModeTask_B(); return;
  }
  
NoInter:
  ram[ScreenRoutineTask] = 0x8; // set for specific task and leave
  return;
}

// in: a; out: none
void OutputInter(void) {
  CALL(WriteGameText, 0x86c9);
  CALL(ResetScreenTimer, 0x86cc);
  ram[DisableScreenFlag] = 0x0; // reenable screen output
  return;
}

// -------------------------------------------------------------------------------------
void AreaParserTaskControl(void) {
  ram[DisableScreenFlag]++; // turn off screen
  do {
    // TaskLoop:
    CALL(AreaParserTaskHandler, 0x86eb); // render column set of current area
    a = ram[AreaParserTaskNum]; zero_flag = a == 0; // check number of tasks
  } while (!zero_flag); // if tasks still not all done, do another one
  ram[ColumnSets]--; neg_flag = (ram[ColumnSets] & 0x80) != 0; // do we need to render more column sets?
  if (neg_flag) {
    ram[ScreenRoutineTask]++; // if not, move on to the next task
  }
  // OutputCol:
  ram[VRAM_Buffer_AddrCtrl] = 0x6; // set vram buffer to output rendered column set; on next NMI
  return;
}

// DrawTitleScreen: native implementation, not generated.
// It may read: none.
// It must produce: none.

// -------------------------------------------------------------------------------------
void ClearBuffersDrawIcon(void) {
  a = ram[OperMode]; zero_flag = a == 0; // check game mode
  // if not title screen mode, leave
  if (!zero_flag) {
    IncModeTask_B();
    return;
  }
  x = 0x0; // otherwise, clear buffer space
  do {
    // TScrClear:
    ram[(VRAM_Buffer1 - 1) + x] = a;
    ram[((VRAM_Buffer1 - 1) + 0x100) + x] = a;
    x--; zero_flag = x == 0;
  } while (!zero_flag);
  CALL(DrawMushroomIcon, 0x8744); // draw player select icon
  IncSubtask(); // fallthrough
  return;
}

void IncSubtask(void) {
  ram[ScreenRoutineTask]++; // move onto next task
  return;
}

// -------------------------------------------------------------------------------------
void WriteTopScore(void) {
  a = 0xfa; // run display routine to display top score on title
  CALL(UpdateNumber, 0x874d);
  IncModeTask_B(); // fallthrough
  return;
}

void IncModeTask_B(void) {
  ram[OperMode_Task]++; // move onto next mode
  return;
}

// -------------------------------------------------------------------------------------
// in: a; out: none
void WriteGameText(void) {
  ram[0x100 + sp--] = a; // save text number to stack
  a = (uint8_t)(a << 1);
  y = a; // multiply by 2 and use as offset
  // if set to do top status bar or world/lives display,
  // Original branch: branch to use current offset as-is
  if (y >= 0x4) {
    // if set to do time-up or game over,
    // Original branch: branch to check players
    if (y >= 0x8) {
      y = 0x8; // otherwise warp zone, therefore set offset
    }
    // Chk2Players:
    a = ram[NumberOfPlayers]; zero_flag = a == 0; // check for number of players
    if (!zero_flag) { goto LdGameText; } // if there are two, use current offset to also print name
    y++; // otherwise increment offset by one to not print name
  }
  
LdGameText:
  x = ROM(GameTextOffsets + y); // get offset to message we want to print
  y = 0x0;
  do {
    // GameTextLoop:
    a = ROM(GameText + x); // load message data
    zero_flag = a == 0xff; // check for terminator
    if (zero_flag) { goto EndGameText; } // branch to end text if found
    ram[VRAM_Buffer1 + y] = a; // otherwise write data to buffer
    x++; // and increment increment
    y++; zero_flag = y == 0;
  } while (!zero_flag); // do this for 256 bytes if no terminator found
  
EndGameText:
  ram[VRAM_Buffer1 + y] = 0x0; // put null terminator at end
  a = ram[0x100 + ++sp]; // pull original text number from stack
  x = a;
  carry_flag = a >= 0x4; // are we printing warp zone?
  if (!carry_flag) {
    x--; zero_flag = x == 0; // are we printing the world/lives display?
    // Original branch: if not, branch to check player's name
    if (zero_flag) {
      a = ram[NumberofLives]; // otherwise, check number of lives
      carry_flag = false; // and increment by one for display
      a = (uint8_t)(a + 0x1 + carry_flag);
      carry_flag = a >= 10; // more than 9 lives?
      if (carry_flag) {
        a = (uint8_t)(a - 10 - !carry_flag); // if so, subtract 10 and put a crown tile
        ram[(VRAM_Buffer1 + 7)] = 0x9f; // next to the difference...strange things happen if; the number of lives exceeds 19
      }
      // PutLives:
      ram[(VRAM_Buffer1 + 8)] = a;
      y = ram[WorldNumber]; // write world and level numbers (incremented for display)
      y++; // to the buffer in the spaces surrounding the dash
      ram[(VRAM_Buffer1 + 19)] = y;
      y = ram[LevelNumber];
      y++;
      ram[(VRAM_Buffer1 + 21)] = y; // we're done here
      return;
    }
    // CheckPlayerName:
    a = ram[NumberOfPlayers]; zero_flag = a == 0; // check number of players
    // if only 1 player, leave
    if (zero_flag) {
      return;
    }
    a = ram[CurrentPlayer]; // load current player
    x--; zero_flag = x == 0; // check to see if current message number is for time up
    if (zero_flag) {
      y = ram[OperMode]; // check for game over mode
      zero_flag = y == GameOverModeValue;
      if (zero_flag) { goto ChkLuigi; }
      a ^= 0b00000001; // if not, must be time up, invert d0 to do other player
    }
    
ChkLuigi:
    carry_flag = (a & 1) != 0; a = a >> 1;
    // if mario is current player, do not change the name
    if (!carry_flag) {
      return;
    }
    y = 0x4;
    do {
      // NameLoop:
      ram[(VRAM_Buffer1 + 3) + y] = ROM(LuigiName + y); // otherwise, replace "MARIO" with "LUIGI"
      y--; neg_flag = (y & 0x80) != 0;
    } while (!neg_flag); // do this until each letter is replaced
    // ExitChkName:
    return;
  }
  // PrintWarpZoneNumbers:
  a = (uint8_t)(a - 0x4 - !carry_flag); // subtract 4 and then shift to the left
  a = (uint8_t)(a << 1); // twice to get proper warp zone number
  a = (uint8_t)(a << 1); // offset
  x = a;
  y = 0x0;
  do {
    // WarpNumLoop:
    ram[(VRAM_Buffer1 + 27) + y] = ROM(WarpZoneNumbers + x); // print warp zone numbers into the; placeholders from earlier
    x++;
    y++; // put a number in every fourth space
    y++;
    y++;
    y++;
    carry_flag = y >= 0xc;
  } while (!carry_flag);
  a = 0x2c; // load new buffer pointer at end of message
  SetVRAMOffset(); return;
}

// -------------------------------------------------------------------------------------
void ResetSpritesAndScreenTimer(void) {
  a = ram[ScreenTimer]; zero_flag = a == 0; // check if screen timer has expired
  // if not, branch to leave
  if (!zero_flag) {
    return;
  }
  CALL(MoveAllSpritesOffscreen, 0x88a4); // otherwise reset sprites now
  ResetScreenTimer(); // fallthrough
  return;
}

void ResetScreenTimer(void) {
  ram[ScreenTimer] = 0x7; // reset timer again
  ram[ScreenRoutineTask]++; // move onto next task
  // NoReset:
  return;
}

// -------------------------------------------------------------------------------------
// $00 - temp vram buffer offset
// $01 - temp metatile buffer offset
// $02 - temp metatile graphics table offset
// $03 - used to store attribute bits
// $04 - used to determine attribute table row
// $05 - used to determine attribute table column
// $06 - metatile graphics table address low
// $07 - metatile graphics table address high
void RenderAreaGraphics(void) {
  a = ram[CurrentColumnPos]; // store LSB of where we're at
  a &= 0x1;
  ram[0x5] = a;
  y = ram[VRAM_Buffer2_Offset]; // store vram buffer offset
  ram[0x0] = y;
  ram[(VRAM_Buffer2 + 1) + y] = ram[CurrentNTAddr_Low]; // get current name table address we're supposed to render
  ram[VRAM_Buffer2 + y] = ram[CurrentNTAddr_High];
  ram[(VRAM_Buffer2 + 2) + y] = 0x9a; // store length byte of 26 here with d7 set; to increment by 32 (in columns)
  a = 0x0; // init attribute row
  ram[0x4] = a;
  x = a;
  do {
    // DrawMTLoop:
    ram[0x1] = x; // store init value of 0 or incremented offset for buffer
    a = ram[MetatileBuffer + x]; // get first metatile number, and mask out all but 2 MSB
    a &= 0b11000000;
    ram[0x3] = a; // store attribute table bits here
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // note that metatile format is:
    { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // %xx000000 - attribute table bits, 
    a = (uint8_t)(a << 1 | carry_flag); // %00xxxxxx - metatile number
    y = a; // rotate bits to d1-d0 and use as offset here
    ram[0x6] = ROM(MetatileGraphics_Low + y); // get address to graphics table from here
    ram[0x7] = ROM(MetatileGraphics_High + y);
    a = ram[MetatileBuffer + x]; // get metatile number again
    a = (uint8_t)(a << 1); // multiply by 4 and use as tile offset
    a = (uint8_t)(a << 1);
    ram[0x2] = a;
    a = ram[AreaParserTaskNum]; // get current task number for level processing and
    a &= 0b00000001; // mask out all but LSB, then invert LSB, multiply by 2
    a ^= 0b00000001; // to get the correct column position in the metatile,
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // then add to the tile offset so we can draw either side
    a = (uint8_t)(a + ram[0x2] + carry_flag); // of the metatiles
    y = a;
    x = ram[0x0]; // use vram buffer offset from before as X
    ram[(VRAM_Buffer2 + 3) + x] = read_byte(indirect_y_addr(0x6)); // get first tile number (top left or top right) and store
    y++;
    ram[(VRAM_Buffer2 + 4) + x] = read_byte(indirect_y_addr(0x6)); // now get the second (bottom left or bottom right) and store
    y = ram[0x4]; // get current attribute row
    a = ram[0x5]; zero_flag = a == 0; // get LSB of current column where we're at, and
    // Original branch: branch if set (clear = left attrib, set = right)
    if (zero_flag) {
      a = ram[0x1]; // get current row we're rendering
      carry_flag = (a & 1) != 0; a = a >> 1; // branch if LSB set (clear = top left, set = bottom left)
      if (carry_flag) { goto LLeft; }
      { bool carry = (ram[0x3] & 0x80) != 0; ram[0x3] = (uint8_t)(ram[0x3] << 1 | carry_flag); carry_flag = carry; } // rotate attribute bits 3 to the left
      { bool carry = (ram[0x3] & 0x80) != 0; ram[0x3] = (uint8_t)(ram[0x3] << 1 | carry_flag); carry_flag = carry; } // thus in d1-d0, for upper left square
      ram[0x3] = (uint8_t)(ram[0x3] << 1 | carry_flag);
    } else {
      // RightCheck:
      a = ram[0x1]; // get LSB of current row we're rendering
      carry_flag = (a & 1) != 0; a = a >> 1; // branch if set (clear = top right, set = bottom right)
      if (carry_flag) { goto NextMTRow; }
      ram[0x3] = ram[0x3] >> 1; // shift attribute bits 4 to the right
      ram[0x3] = ram[0x3] >> 1; // thus in d3-d2, for upper right square
      ram[0x3] = ram[0x3] >> 1;
      ram[0x3] = ram[0x3] >> 1;
      goto SetAttrib;
      
LLeft:
      ram[0x3] = ram[0x3] >> 1; // shift attribute bits 2 to the right
      ram[0x3] = ram[0x3] >> 1; // thus in d5-d4 for lower left square
      
NextMTRow:
      ram[0x4]++; // move onto next attribute row  
    }
    
SetAttrib:
    a = ram[AttributeBuffer + y]; // get previously saved bits from before
    a |= ram[0x3]; // if any, and put new bits, if any, onto
    ram[AttributeBuffer + y] = a; // the old, and store
    ram[0x0]++; // increment vram buffer offset by 2
    ram[0x0]++;
    x = ram[0x1]; // get current gfx buffer row, and check for
    x++; // the bottom of the screen
    carry_flag = x >= 0xd;
  } while (!carry_flag); // if not there yet, loop back
  y = ram[0x0]; // get current vram buffer offset, increment by 3
  y++; // (for name table address and length bytes)
  y++;
  y++;
  ram[VRAM_Buffer2 + y] = 0x0; // put null terminator at end of data for name table
  ram[VRAM_Buffer2_Offset] = y; // store new buffer offset
  ram[CurrentNTAddr_Low]++; // increment name table address low
  a = ram[CurrentNTAddr_Low]; // check current low byte
  a &= 0b00011111; zero_flag = a == 0; // if no wraparound, just skip this part
  if (zero_flag) {
    ram[CurrentNTAddr_Low] = 0x80; // if wraparound occurs, make sure low byte stays; just under the status bar
    a = ram[CurrentNTAddr_High]; // and then invert d2 of the name table address high
    a ^= 0b00000100; // to move onto the next appropriate name table
    ram[CurrentNTAddr_High] = a;
  }
  // ExitDrawM:
  SetVRAMCtrl(); return; // jump to set buffer to $0341 and leave
}

// -------------------------------------------------------------------------------------
// $00 - temp attribute table address high (big endian order this time!)
// $01 - temp attribute table address low
void RenderAttributeTables(void) {
  a = ram[CurrentNTAddr_Low]; // get low byte of next name table address
  a &= 0b00011111; // to be written to, mask out all but 5 LSB,
  carry_flag = true; // subtract four 
  { uint16_t diff = (uint16_t)(a - 0x4 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  a &= 0b00011111; // mask out bits again and store
  ram[0x1] = a;
  a = ram[CurrentNTAddr_High]; // get high byte and branch if borrow not set
  if (!carry_flag) {
    a ^= 0b00000100; // otherwise invert d2
  }
  // SetATHigh:
  a &= 0b00000100; // mask out all other bits
  a |= 0x23; // add $2300 to the high byte and store
  ram[0x0] = a;
  a = ram[0x1]; // get low byte - 4, divide by 4, add offset for
  a = a >> 1; // attribute table and store
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = (uint8_t)(a + 0xc0 + carry_flag); // we should now have the appropriate block of
  ram[0x1] = a; // attribute table in our temp address
  x = 0x0;
  y = ram[VRAM_Buffer2_Offset]; // get buffer offset
  do {
    // AttribLoop:
    ram[VRAM_Buffer2 + y] = ram[0x0]; // store high byte of attribute table address
    a = ram[0x1];
    carry_flag = false; // get low byte, add 8 because we want to start
    a = (uint8_t)(a + 0x8 + carry_flag); // below the status bar, and store
    ram[(VRAM_Buffer2 + 1) + y] = a;
    ram[0x1] = a; // also store in temp again
    ram[(VRAM_Buffer2 + 3) + y] = ram[AttributeBuffer + x]; // fetch current attribute table byte and store; in the buffer
    a = 0x1;
    ram[(VRAM_Buffer2 + 2) + y] = a; // store length of 1 in buffer
    a = a >> 1;
    ram[AttributeBuffer + x] = a; // clear current byte in attribute buffer
    y++; // increment buffer offset by 4 bytes
    y++;
    y++;
    y++;
    x++; // increment attribute offset and check to see
    carry_flag = x >= 0x7; // if we're at the end yet
  } while (!carry_flag);
  ram[VRAM_Buffer2 + y] = a; // put null terminator at the end
  ram[VRAM_Buffer2_Offset] = y; // store offset in case we want to do any more
  SetVRAMCtrl(); // fallthrough
  return;
}

void SetVRAMCtrl(void) {
  ram[VRAM_Buffer_AddrCtrl] = 0x6; // set buffer to $0341 and leave
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used as temporary counter in ColorRotation
void ColorRotation(void) {
  a = ram[FrameCounter]; // get frame counter
  a &= 0x7; zero_flag = a == 0; // mask out all but three LSB
  // branch if not set to zero to do this every eighth frame
  if (!zero_flag) {
    return;
  }
  x = ram[VRAM_Buffer1_Offset]; // check vram buffer offset
  // if offset over 48 bytes, branch to leave
  if (x >= 0x31) {
    return;
  }
  y = a; // otherwise use frame counter's 3 LSB as offset here
  do {
    // GetBlankPal:
    ram[VRAM_Buffer1 + x] = ROM(BlankPalette + y); // get blank palette for palette 3; store it in the vram buffer
    x++; // increment offsets
    y++;
    carry_flag = y >= 0x8;
  } while (!carry_flag); // do this until all bytes are copied
  x = ram[VRAM_Buffer1_Offset]; // get current vram buffer offset
  ram[0x0] = 0x3; // set counter here
  a = ram[AreaType]; // get area type
  a = (uint8_t)(a << 1); // multiply by 4 to get proper offset
  a = (uint8_t)(a << 1);
  y = a; // save as offset here
  do {
    // GetAreaPal:
    ram[(VRAM_Buffer1 + 3) + x] = ROM(Palette3Data + y); // fetch palette to be written based on area type; store it to overwrite blank palette in vram buffer
    y++;
    x++;
    ram[0x0]--; neg_flag = (ram[0x0] & 0x80) != 0; // decrement counter
  } while (!neg_flag); // do this until the palette is all copied
  x = ram[VRAM_Buffer1_Offset]; // get current vram buffer offset
  y = ram[ColorRotateOffset]; // get color cycling offset
  ram[(VRAM_Buffer1 + 4) + x] = ROM(ColorRotatePalette + y); // get and store current color in second slot of palette
  a = ram[VRAM_Buffer1_Offset];
  carry_flag = false; // add seven bytes to vram buffer offset
  a = (uint8_t)(a + 0x7 + carry_flag);
  ram[VRAM_Buffer1_Offset] = a;
  ram[ColorRotateOffset]++; // increment color cycling offset
  a = ram[ColorRotateOffset];
  // check to see if it's still in range
  // if so, branch to leave
  if (a < 0x6) {
    return;
  }
  ram[ColorRotateOffset] = 0x0; // otherwise, init to keep it in range
  // ExitColorRot:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00 - temp store for offset control bit
// $01 - temp vram buffer offset
// $02 - temp store for vertical high nybble in block buffer routine
// $03 - temp adder for high byte of name table address
// $04, $05 - name table address low/high
// $06, $07 - block buffer address low/high
// in: none; out: carry
void RemoveCoin_Axe(void) {
  y = 0x41; // set low byte so offset points to $0341
  a = 0x3; // load offset for default blank metatile
  x = ram[AreaType]; zero_flag = x == 0; // check area type
  // Original branch: if not water type, use offset
  if (zero_flag) {
    a = 0x4; // otherwise load offset for blank metatile used in water
  }
  // WriteBlankMT:
  CALL(PutBlockMetatile, 0x8a5a); // do a sub to write blank metatile to vram buffer
  ram[VRAM_Buffer_AddrCtrl] = 0x6; // set vram address controller to $0341 and leave
  return;
}

// in: a, x; out: x
void ReplaceBlockMetatile(void) {
  CALL(WriteBlockMetatile, 0x8a63); // write metatile to vram buffer to replace block object
  ram[Block_ResidualCounter]++; // increment unused counter (residual code)
  ram[Block_RepFlag + x]--; // decrement flag (residual code)
  return; // leave
}

// in: x; out: none
void DestroyBlockMetatile(void) {
  a = 0x0; // force blank metatile if branched/jumped to this point
  WriteBlockMetatile(); // fallthrough
  return;
}

// in: a, x; out: x
void WriteBlockMetatile(void) {
  y = 0x3; // load offset for blank metatile
  // check contents of A for blank metatile
  // Original branch: branch if found (unconditional if branched from 8a6b)
  if (a != 0x0) {
    y = 0x0; // load offset for brick metatile w/ line
    zero_flag = a == 0x58;
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is brick with coins (w/ line)
    zero_flag = a == 0x51;
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is breakable brick w/ line
    y++; // increment offset for brick metatile w/o line
    zero_flag = a == 0x5d;
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is brick with coins (w/o line)
    zero_flag = a == 0x52;
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is breakable brick w/o line
    y++; // if any other metatile, increment offset for empty block
  }
  
UseBOffset:
  a = y; // put Y in A
  y = ram[VRAM_Buffer1_Offset]; // get vram buffer offset
  y++; // move onto next byte
  CALL(PutBlockMetatile, 0x8a8e); // get appropriate block data and write to vram buffer
  MoveVOffset(); // fallthrough
  return;
}

// in: y; out: y
void MoveVOffset(void) {
  y--; // decrement vram buffer offset
  a = y; // add 10 bytes to it
  carry_flag = false;
  a = (uint8_t)(a + 10 + carry_flag);
  SetVRAMOffset(); return; // branch to store as new vram buffer offset
}

// in: a, x, y; out: x, y, carry
void PutBlockMetatile(void) {
  ram[0x0] = x; // store control bit from SprDataOffset_Ctrl
  ram[0x1] = y; // store vram buffer offset for next byte
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // multiply A by four and use as X
  x = a;
  y = 0x20; // load high byte for name table 0
  a = ram[0x6]; // get low byte of block buffer pointer
  // check to see if we're on odd-page block buffer
  // Original branch: if not, use current high byte
  if (a >= 0xd0) {
    y = 0x24; // otherwise load high byte for name table 1
  }
  // SaveHAdder:
  ram[0x3] = y; // save high byte here
  a &= 0xf; // mask out high nybble of block buffer pointer
  a = (uint8_t)(a << 1); // multiply by 2 to get appropriate name table low byte
  ram[0x4] = a; // and then store it here
  ram[0x5] = 0x0; // initialize temp high byte
  a = ram[0x2]; // get vertical high nybble offset used in block buffer routine
  carry_flag = false;
  a = (uint8_t)(a + 0x20 + carry_flag); // add 32 pixels for the status bar
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  ram[0x5] = (uint8_t)(ram[0x5] << 1 | carry_flag); // shift and rotate d7 onto d0 and d6 into carry
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (ram[0x5] & 0x80) != 0; ram[0x5] = (uint8_t)(ram[0x5] << 1 | carry_flag); carry_flag = carry; } // shift and rotate d6 onto d0 and d5 into carry
  { uint16_t sum = a + ram[0x4] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add low byte of name table and carry to vertical high nybble
  ram[0x4] = a; // and store here
  a = ram[0x5]; // get whatever was in d7 and d6 of vertical high nybble
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry
  carry_flag = false;
  a = (uint8_t)(a + ram[0x3] + carry_flag); // then add high byte of name table
  ram[0x5] = a; // store here
  y = ram[0x1]; // get vram buffer offset to be used
  RemBridge(); // fallthrough
  return;
}

// in: x, y; out: x, carry
void RemBridge(void) {
  ram[(VRAM_Buffer1 + 2) + y] = ROM(BlockGfxData + x); // write top left and top right; tile numbers into first spot
  ram[(VRAM_Buffer1 + 3) + y] = ROM((BlockGfxData + 1) + x);
  ram[(VRAM_Buffer1 + 7) + y] = ROM((BlockGfxData + 2) + x); // write bottom left and bottom; right tiles numbers into
  ram[(VRAM_Buffer1 + 8) + y] = ROM((BlockGfxData + 3) + x); // second spot
  a = ram[0x4];
  ram[VRAM_Buffer1 + y] = a; // write low byte of name table
  carry_flag = false; // into first slot as read
  { uint16_t sum = a + 0x20 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add 32 bytes to value
  ram[(VRAM_Buffer1 + 5) + y] = a; // write low byte of name table
  a = ram[0x5]; // plus 32 bytes into second slot
  ram[(VRAM_Buffer1 - 1) + y] = a; // write high byte of name
  ram[(VRAM_Buffer1 + 4) + y] = a; // table address to both slots
  a = 0x2;
  ram[(VRAM_Buffer1 + 1) + y] = a; // put length of 2 in
  ram[(VRAM_Buffer1 + 6) + y] = a; // both slots
  ram[(VRAM_Buffer1 + 9) + y] = 0x0; // put null terminator at end
  x = ram[0x0]; // get offset control bit here
  return; // and leave
}

// InitializeNameTables: native implementation, not generated.
// It may read: none.
// It must produce: y.

// WriteNTAddr: native implementation, not generated.
// It may read: a.
// It must produce: y.

// -------------------------------------------------------------------------------------
// $00 - temp joypad bit
void ReadJoypads(void) {
  a = 0x1; // reset and clear strobe of joypad ports
  write_joypad1(a); write_joypad2(a);
  a = a >> 1;
  x = a; // start with joypad 1's port
  write_joypad1(a); write_joypad2(a);
  CALL(ReadPortBits, 0x8e68);
  x++; // increment for joypad 2's port
  ReadPortBits(); // fallthrough
  return;
}

// in: a, x; out: a
void ReadPortBits(void) {
  y = 0x8;
  do {
    // PortLoop:
    ram[0x100 + sp--] = a; // push previous bit onto stack
    a = read_byte((uint16_t)(JOYPAD_PORT + x)); // read current bit on joypad port
    ram[0x0] = a; // check d1 and d0 of port output
    a = a >> 1; // this is necessary on the old
    a |= ram[0x0]; // famicom systems in japan
    carry_flag = (a & 1) != 0; a = a >> 1;
    a = ram[0x100 + ++sp]; // read bits from stack
    a = (uint8_t)(a << 1 | carry_flag); // rotate bit from carry flag
    y--; zero_flag = y == 0;
  } while (!zero_flag); // count down bits left
  ram[SavedJoypadBits + x] = a; // save controller status here always
  ram[0x100 + sp--] = a;
  a &= 0b00110000; // check for select or start
  a &= ram[(JoypadBitMask + x) & 0x7ff]; zero_flag = a == 0; // if neither saved state nor current state
  // Original branch: have any of these two set, branch
  if (!zero_flag) {
    a = ram[0x100 + ++sp];
    a &= 0b11001111; // otherwise store without select
    ram[SavedJoypadBits + x] = a; // or start bits and leave
    return;
  }
  // Save8Bits:
  a = ram[0x100 + ++sp];
  ram[(JoypadBitMask + x) & 0x7ff] = a; // save with all bits in another place and leave
  return;
}

// UpdateScreen: native implementation, not generated.
// It may read: none.
// It must produce: none.

// InitScroll: native implementation, not generated.
// It may read: a.
// It must produce: none.

// WritePPUReg1: native implementation, not generated.
// It may read: a.
// It must produce: none.

// -------------------------------------------------------------------------------------
// $00 - used to store status bar nybbles
// $02 - used as temp vram offset
// $03 - used to store length of status bar number
// status bar name table offset and length data
// in: a; out: none
void PrintStatusBarNumbers(void) {
  ram[0x0] = a; // store player-specific offset
  CALL(OutputNumbers, 0x8f0a); // use first nybble to print the coin display
  a = ram[0x0]; // move high nybble to low
  a = a >> 1; // and print to score display
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  OutputNumbers(); // fallthrough
  return;
}

// in: a; out: none
void OutputNumbers(void) {
  carry_flag = false; // add 1 to low nybble
  a = (uint8_t)(a + 0x1 + carry_flag);
  a &= 0b00001111; // mask out high nybble
  if (a >= 0x6) {
    return;
  }
  ram[0x100 + sp--] = a; // save incremented value to stack for now and
  a = (uint8_t)(a << 1); // shift to left and use as offset
  y = a;
  x = ram[VRAM_Buffer1_Offset]; // get current buffer pointer
  a = 0x20; // put at top of screen by default
  // are we writing top score on title screen?
  if (y == 0x0) {
    a = 0x22; // if so, put further down on the screen
  }
  // SetupNums:
  ram[VRAM_Buffer1 + x] = a;
  ram[(VRAM_Buffer1 + 1) + x] = ROM(StatusBarData + y); // write low vram address and length of thing; we're printing to the buffer
  a = ROM((StatusBarData + 1) + y);
  ram[(VRAM_Buffer1 + 2) + x] = a;
  ram[0x3] = a; // save length byte in counter
  ram[0x2] = x; // and buffer pointer elsewhere for now
  a = ram[0x100 + ++sp]; // pull original incremented value from stack
  x = a;
  a = ROM(StatusBarOffset + x); // load offset to value we want to write
  carry_flag = true;
  a = (uint8_t)(a - ROM((StatusBarData + 1) + y) - !carry_flag); // subtract from length byte we read before
  y = a; // use value as offset to display digits
  x = ram[0x2];
  do {
    // DigitPLoop:
    ram[(VRAM_Buffer1 + 3) + x] = ram[(DisplayDigits + y) & 0x7ff]; // write digits to the buffer
    x++;
    y++;
    ram[0x3]--; zero_flag = ram[0x3] == 0; // do this until all the digits are written
  } while (!zero_flag);
  ram[(VRAM_Buffer1 + 3) + x] = 0x0; // put null terminator at end
  x++; // increment buffer pointer by 3
  x++;
  x++;
  ram[VRAM_Buffer1_Offset] = x; // store it in case we want to use it again
  // ExitOutputN:
  return;
}

// -------------------------------------------------------------------------------------
// in: y; out: none
void DigitsMathRoutine(void) {
  a = ram[OperMode]; // check mode of operation
  zero_flag = a == TitleScreenModeValue;
  if (zero_flag) { goto EraseDMods; } // if in title screen mode, branch to lock score
  x = 0x5;
  
AddModLoop:
  a = ram[DigitModifier + x]; // load digit amount to increment
  carry_flag = false;
  a = (uint8_t)(a + ram[(DisplayDigits + y) & 0x7ff] + carry_flag); neg_flag = (a & 0x80) != 0; // add to current digit
  if (neg_flag) { goto BorrowOne; } // if result is a negative number, branch to subtract
  carry_flag = a >= 10;
  if (carry_flag) { goto CarryOne; } // if digit greater than $09, branch to add
  
StoreNewD:
  ram[(DisplayDigits + y) & 0x7ff] = a; // store as new score or game timer digit
  y--; // move onto next digits in score or game timer
  x--; neg_flag = (x & 0x80) != 0; // and digit amounts to increment
  if (!neg_flag) { goto AddModLoop; } // loop back if we're not done yet
  
EraseDMods:
  a = 0x0; // store zero here
  x = 0x6; // start with the last digit
  do {
    // EraseMLoop:
    ram[(DigitModifier - 1) + x] = a; // initialize the digit amounts to increment
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag); // do this until they're all reset, then leave
  return;
  
BorrowOne:
  ram[(DigitModifier - 1) + x]--; // decrement the previous digit, then put $09 in
  a = 0x9; // the game timer digit we're currently on to "borrow
  goto StoreNewD; // the one", then do an unconditional branch back
  
CarryOne:
  carry_flag = true; // subtract ten from our digit to make it a
  a = (uint8_t)(a - 10 - !carry_flag); // proper BCD number, then increment the digit
  ram[(DigitModifier - 1) + x]++; // preceding current digit to "carry the one" properly
  goto StoreNewD; // go back to just after we branched here
}

// -------------------------------------------------------------------------------------
void UpdateTopScore(void) {
  x = 0x5; // start with mario's score
  CALL(TopScoreCheck, 0x8f9b);
  x = 0xb; // now do luigi's score
  TopScoreCheck(); // fallthrough
  return;
}

// in: x; out: none
void TopScoreCheck(void) {
  y = 0x5; // start with the lowest digit
  carry_flag = true;
  do {
    // GetScoreDiff:
    a = ram[(PlayerScoreDisplay + x) & 0x7ff]; // subtract each player digit from each high score digit
    { uint16_t diff = (uint16_t)(a - ram[(TopScoreDisplay + y) & 0x7ff] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // from lowest to highest, if any top score digit exceeds
    x--; // any player digit, borrow will be set until a subsequent
    y--; neg_flag = (y & 0x80) != 0; // subtraction clears it (player digit is higher than top)
  } while (!neg_flag);
  // check to see if borrow is still set, if so, no new high score
  if (!carry_flag) {
    return;
  }
  x++; // increment X and Y once to the start of the score
  y++;
  do {
    // CopyScore:
    ram[(TopScoreDisplay + y) & 0x7ff] = ram[(PlayerScoreDisplay + x) & 0x7ff]; // store player's score digits into high score memory area
    x++;
    y++;
    carry_flag = y >= 0x6; // do this until we have stored them all
  } while (!carry_flag);
  // NoTopSc:
  return;
}

// -------------------------------------------------------------------------------------
void InitializeGame(void) {
  y = 0x6f; // clear all memory as in initialization procedure,
  CALL(InitializeMemory, 0x8fd3); // but this time, clear only as far as $076f
  y = 0x1f;
  do {
    // ClrSndLoop:
    ram[(SoundMemory + y) & 0x7ff] = a; // clear out memory used
    y--; neg_flag = (y & 0x80) != 0; // by the sound engines
  } while (!neg_flag);
  ram[DemoTimer] = 0x18; // set demo timer
  CALL(LoadAreaPointer, 0x8fe3);
  InitializeArea(); // fallthrough
  return;
}

void InitializeArea(void) {
  y = 0x4b; // clear all memory again, only as far as $074b
  CALL(InitializeMemory, 0x8fe8); // this is only necessary if branching from
  x = 0x21;
  a = 0x0;
  do {
    // ClrTimersLoop:
    ram[(Timers + x) & 0x7ff] = a; // clear out memory between
    x--; neg_flag = (x & 0x80) != 0; // $0780 and $07a1
  } while (!neg_flag);
  a = ram[HalfwayPage];
  y = ram[AltEntranceControl]; zero_flag = y == 0; // if AltEntranceControl not set, use halfway page, if any found
  if (!zero_flag) {
    a = ram[EntrancePage]; // otherwise use saved entry page number here
  }
  // StartPage:
  ram[ScreenLeft_PageLoc] = a; // set as value here
  ram[CurrentPageLoc] = a; // also set as current page
  ram[BackloadingFlag] = a; // set flag here if halfway page or saved entry page number found
  CALL(GetScreenPosition, 0x9009); // get pixel coordinates for screen borders
  y = 0x20; // if on odd numbered page, use $2480 as start of rendering
  a &= 0b00000001; zero_flag = a == 0; // otherwise use $2080, this address used later as name table
  // Original branch: address for rendering of game area
  if (!zero_flag) {
    y = 0x24;
  }
  // SetInitNTHigh:
  ram[CurrentNTAddr_High] = y; // store name table address
  ram[CurrentNTAddr_Low] = 0x80;
  a = (uint8_t)(a << 1); // store LSB of page number in high nybble
  a = (uint8_t)(a << 1); // of block buffer column position
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  ram[BlockBufferColumnPos] = a;
  ram[AreaObjectLength]--; // set area object lengths for all empty
  ram[(AreaObjectLength + 1)]--;
  ram[(AreaObjectLength + 2)]--;
  ram[ColumnSets] = 0xb; // set value for renderer to update 12 column sets; 12 column sets = 24 metatile columns = 1 1/2 screens
  CALL(GetAreaDataAddrs, 0x9031); // get enemy and level addresses and load header
  a = ram[PrimaryHardMode]; zero_flag = a == 0; // check to see if primary hard mode has been activated
  // Original branch: if so, activate the secondary no matter where we're at
  if (zero_flag) {
    a = ram[WorldNumber]; // otherwise check world number
    { uint8_t m = World5; carry_flag = a >= m; zero_flag = a == m; } // if less than 5, do not activate secondary
    if (!carry_flag) { goto CheckHalfway; }
    if (!zero_flag) { goto SetSecHard; } // if not equal to, then world > 5, thus activate
    a = ram[LevelNumber]; // otherwise, world 5, so check level number
    carry_flag = a >= Level3; // if 1 or 2, do not set secondary hard mode flag
    if (!carry_flag) { goto CheckHalfway; }
  }
  
SetSecHard:
  ram[SecondaryHardMode]++; // set secondary hard mode flag for areas 5-3 and beyond
  
CheckHalfway:
  a = ram[HalfwayPage]; zero_flag = a == 0;
  if (!zero_flag) {
    ram[PlayerEntranceCtrl] = 0x2; // if halfway page set, overwrite start position from header
  }
  // DoneInitArea:
  ram[AreaMusicQueue] = Silence; // silence music
  ram[DisableScreenFlag] = 0x1; // disable screen output
  ram[OperMode_Task]++; // increment one of the modes
  return;
}

// -------------------------------------------------------------------------------------
void PrimaryGameSetup(void) {
  a = 0x1;
  ram[FetchNewGameTimerFlag] = a; // set flag to load game timer from header
  ram[PlayerSize] = a; // set player's size to small
  a = 0x2;
  ram[NumberofLives] = a; // give each player three lives
  ram[OffScr_NumberofLives] = a;
  SecondaryGameSetup(); // fallthrough
  return;
}

void SecondaryGameSetup(void) {
  a = 0x0;
  ram[DisableScreenFlag] = a; // enable screen output
  y = a;
  do {
    // ClearVRLoop:
    ram[(VRAM_Buffer1 - 1) + y] = a; // clear buffer at $0300-$03ff
    y++; zero_flag = y == 0;
  } while (!zero_flag);
  ram[GameTimerExpiredFlag] = a; // clear game timer exp flag
  ram[DisableIntermediate] = a; // clear skip lives display flag
  ram[BackloadingFlag] = a; // clear value here
  ram[BalPlatformAlignment] = 0xff; // initialize balance platform assignment flag
  a = ram[ScreenLeft_PageLoc]; // get left side page location
  carry_flag = (ram[Mirror_PPU_CTRL_REG1] & 1) != 0; ram[Mirror_PPU_CTRL_REG1] = ram[Mirror_PPU_CTRL_REG1] >> 1; // shift LSB of ppu register #1 mirror out
  a &= 0x1; // mask out all but LSB of page location
  { bool carry = (a & 1) != 0; a = (uint8_t)(a >> 1 | carry_flag << 7); carry_flag = carry; } // rotate LSB of page location into carry then onto mirror
  ram[Mirror_PPU_CTRL_REG1] = (uint8_t)(ram[Mirror_PPU_CTRL_REG1] << 1 | carry_flag); // this is to set the proper PPU name table
  CALL(GetAreaMusic, 0x9099); // load proper music into queue
  ram[(SprShuffleAmt + 2)] = 0x38; // load sprite shuffle amounts to be used later
  ram[(SprShuffleAmt + 1)] = 0x48;
  ram[SprShuffleAmt] = 0x58;
  x = 0xe; // load default OAM offsets into $06e4-$06f2
  do {
    // ShufAmtLoop:
    ram[SprDataOffset + x] = ROM(DefaultSprOffsets + x);
    x--; neg_flag = (x & 0x80) != 0; // do this until they're all set
  } while (!neg_flag);
  y = 0x3; // set up sprite #0
  do {
    // ISpr0Loop:
    ram[Sprite_Data + y] = ROM(Sprite0Data + y);
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  CALL(DoNothing2, 0x90c1); // these jsrs doesn't do anything useful
  CALL(DoNothing1, 0x90c4);
  ram[Sprite0HitDetectFlag]++; // set sprite #0 check flag
  ram[OperMode_Task]++; // increment to next task
  return;
}

// -------------------------------------------------------------------------------------
// $06 - RAM address low
// $07 - RAM address high
// in: y; out: a
void InitializeMemory(void) {
  x = 0x7; // set initial high byte to $0700-$07ff
  a = 0x0; // set initial low byte to start of page (at $00 of page)
  ram[0x6] = a;
  do {
    // InitPageLoop:
    ram[0x7] = x;
    do {
      // InitByteLoop:
      // check to see if we're on the stack ($0100-$01ff)
      // Original branch: if not, go ahead anyway
      if (x == 0x1) {
        carry_flag = y >= 0x60; // otherwise, check to see if we're at $0160-$01ff
        if (carry_flag) { goto SkipByte; } // if so, skip write
      }
      // InitByte:
      dynamic_ram_write(indirect_y_addr(0x6), a); // otherwise, initialize byte with current low byte in Y
      
SkipByte:
      y--;
      zero_flag = y == 0xff; // do this until all bytes in page have been erased
    } while (!zero_flag);
    x--; neg_flag = (x & 0x80) != 0; // go onto the next page
  } while (!neg_flag); // do this until all pages of memory have been erased
  return;
}

// -------------------------------------------------------------------------------------
void GetAreaMusic(void) {
  a = ram[OperMode]; zero_flag = a == 0; // if in title screen mode, leave
  if (zero_flag) {
    return;
  }
  a = ram[AltEntranceControl]; // check for specific alternate mode of entry
  // if found, branch without checking starting position
  // Original branch: from area object data header
  if (a != 0x2) {
    y = 0x5; // select music for pipe intro scene by default
    a = ram[PlayerEntranceCtrl]; // check value from level header for certain values
    zero_flag = a == 0x6;
    if (zero_flag) { goto StoreMusic; } // load music for pipe intro scene if header
    zero_flag = a == 0x7; // start position either value $06 or $07
    if (zero_flag) { goto StoreMusic; }
  }
  // ChkAreaType:
  y = ram[AreaType]; // load area type as offset for music bit
  a = ram[CloudTypeOverride]; zero_flag = a == 0;
  // Original branch: check for cloud type override
  if (!zero_flag) {
    y = 0x4; // select music for cloud type level if found
  }
  
StoreMusic:
  ram[AreaMusicQueue] = ROM(MusicSelectData + y); // otherwise select appropriate music for level type; store in queue and leave
  // ExitGetM:
  return;
}

// -------------------------------------------------------------------------------------
void Entrance_GameTimerSetup(void) {
  ram[Player_PageLoc] = ram[ScreenLeft_PageLoc]; // set current page for area objects; as page location for player
  ram[VerticalForceDown] = 0x28; // store value here; for fractional movement downwards if necessary
  a = 0x1; // set high byte of player position and
  ram[PlayerFacingDir] = a; // set facing direction so that player faces right
  ram[Player_Y_HighPos] = a;
  ram[Player_State] = 0x0; // set player state to on the ground by default
  ram[Player_CollisionBits]--; // initialize player's collision bits
  y = 0x0; // initialize halfway page
  ram[HalfwayPage] = y;
  a = ram[AreaType]; zero_flag = a == 0; // check area type
  // Original branch: if water type, set swimming flag, otherwise do not set
  if (zero_flag) {
    y++;
  }
  // ChkStPos:
  ram[SwimmingFlag] = y;
  x = ram[PlayerEntranceCtrl]; // get starting position loaded from header
  y = ram[AltEntranceControl]; zero_flag = y == 0; // check alternate mode of entry flag for 0 or 1
  if (!zero_flag) {
    zero_flag = y == 0x1;
    if (zero_flag) { goto SetStPos; }
    x = ROM((AltYPosOffset - 2) + y); // if not 0 or 1, override $0710 with new offset in X
  }
  
SetStPos:
  ram[Player_X_Position] = ROM(PlayerStarting_X_Pos + y); // load appropriate horizontal position; and vertical positions for the player, using
  ram[Player_Y_Position] = ROM(PlayerStarting_Y_Pos + x); // AltEntranceControl as offset for horizontal and either $0710; or value that overwrote $0710 as offset for vertical
  ram[Player_SprAttrib] = ROM(PlayerBGPriorityData + x); // set player sprite attributes using offset in X
  CALL(GetPlayerColors, 0x9177); // get appropriate player palette
  y = ram[GameTimerSetting]; zero_flag = y == 0; // get timer control value from header
  // Original branch: if set to zero, branch (do not use dummy byte for this)
  if (!zero_flag) {
    a = ram[FetchNewGameTimerFlag]; zero_flag = a == 0; // do we need to set the game timer? if not, use 
    if (zero_flag) { goto ChkOverR; } // old game timer setting
    ram[GameTimerDisplay] = ROM(GameTimerData + y); // if game timer is set and game timer flag is also set,; use value of game timer control for first digit of game timer
    a = 0x1;
    ram[(GameTimerDisplay + 2)] = a; // set last digit of game timer to 1
    a = a >> 1;
    ram[(GameTimerDisplay + 1)] = a; // set second digit of game timer
    ram[FetchNewGameTimerFlag] = a; // clear flag for game timer reset
    ram[StarInvincibleTimer] = a; // clear star mario timer
  }
  
ChkOverR:
  y = ram[JoypadOverride]; zero_flag = y == 0; // if controller bits not set, branch to skip this part
  if (!zero_flag) {
    ram[Player_State] = 0x3; // set player state to climbing
    x = 0x0; // set offset for first slot, for block object
    CALL(InitBlock_XY_Pos, 0x91a4);
    ram[Block_Y_Position] = 0xf0; // set vertical coordinate for block object
    x = 0x5; // set offset in X for last enemy object buffer slot
    y = 0x0; // set offset in Y for object coordinates used earlier
    CALL(Setup_Vine, 0x91af); // do a sub to grow vine
  }
  // ChkSwimE:
  y = ram[AreaType]; zero_flag = y == 0; // if level not water-type,
  // Original branch: skip this subroutine
  if (zero_flag) {
    CALL(SetupBubble, 0x91b7); // otherwise, execute sub to set up air bubbles
  }
  // SetPESub:
  ram[GameEngineSubroutine] = 0x7; // set to run player entrance subroutine; on the next frame of game engine
  return;
}

// -------------------------------------------------------------------------------------
// page numbers are in order from -1 to -4
void PlayerLoseLife(void) {
  ram[DisableScreenFlag]++; // disable screen and sprite 0 check
  ram[Sprite0HitDetectFlag] = 0x0;
  ram[EventMusicQueue] = Silence; // silence music
  ram[NumberofLives]--; neg_flag = (ram[NumberofLives] & 0x80) != 0; // take one life from player
  // Original branch: if player still has lives, branch
  if (neg_flag) {
    ram[OperMode_Task] = 0x0; // initialize mode task,
    ram[OperMode] = GameOverModeValue; // switch to game over mode; and leave
    return;
  }
  // StillInGame:
  a = ram[WorldNumber]; // multiply world number by 2 and use
  a = (uint8_t)(a << 1); // as offset
  x = a;
  a = ram[LevelNumber]; // if in area -3 or -4, increment
  a &= 0x2; zero_flag = a == 0; // offset by one byte, otherwise
  // Original branch: leave offset alone
  if (!zero_flag) {
    x++;
  }
  // GetHalfway:
  y = ROM(HalfwayPageNybbles + x); // get halfway page number with offset
  a = ram[LevelNumber]; // check area number's LSB
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = y; // if in area -2 or -4, use lower nybble
  if (!carry_flag) {
    a = a >> 1; // move higher nybble to lower if area
    a = a >> 1; // number is -1 or -3
    a = a >> 1;
    a = a >> 1;
  }
  // MaskHPNyb:
  a &= 0b00001111; // mask out all but lower nybble
  { uint8_t m = ram[ScreenLeft_PageLoc]; carry_flag = a >= m; zero_flag = a == m; }
  // Original branch: left side of screen must be at the halfway page,
  if (!zero_flag) {
    if (!carry_flag) { goto SetHalfway; } // otherwise player must start at the
    a = 0x0; // beginning of the level
  }
  
SetHalfway:
  ram[HalfwayPage] = a; // store as halfway page for player
  CALL(TransposePlayers, 0x9214); // switch players around if 2-player game
  ContinueGame(); return; // continue the game
}

// -------------------------------------------------------------------------------------
void GameOverMode(void) {
  a = ram[OperMode_Task];
  switch (JumpEngine(0x921d)) {
    case 0x9224: SetupGameOver(); return;
    case 0x8567: ScreenRoutines(); return;
    case 0x9237: RunGameOver(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void SetupGameOver(void) {
  a = 0x0; // reset screen routine task control for title screen, game,
  ram[ScreenRoutineTask] = a; // and game over modes
  ram[Sprite0HitDetectFlag] = a; // disable sprite 0 check
  ram[EventMusicQueue] = GameOverMusic; // put game over music in secondary queue
  ram[DisableScreenFlag]++; // disable screen output
  ram[OperMode_Task]++; // set secondary mode to 1
  return;
}

// -------------------------------------------------------------------------------------
void RunGameOver(void) {
  ram[DisableScreenFlag] = 0x0; // reenable screen
  a = ram[SavedJoypad1Bits]; // check controller for start pressed
  a &= Start_Button; zero_flag = a == 0;
  if (!zero_flag) {
    TerminateGame();
    return;
  }
  a = ram[ScreenTimer]; zero_flag = a == 0; // if not pressed, wait for
  // screen timer to expire
  if (!zero_flag) {
    return;
  }
  TerminateGame(); // fallthrough
  return;
}

void TerminateGame(void) {
  ram[EventMusicQueue] = Silence; // silence music
  CALL(TransposePlayers, 0x924e); // check if other player can keep
  // going, and do so if possible
  if (!carry_flag) {
    ContinueGame();
    return;
  }
  ram[ContinueWorld] = ram[WorldNumber]; // otherwise put world number of current; player into secret continue function variable
  a = 0x0;
  a = (uint8_t)(a << 1); // residual ASL instruction
  ram[OperMode_Task] = a; // reset all modes to title screen and
  ram[ScreenTimer] = a; // leave
  ram[OperMode] = a;
  return;
}

void ContinueGame(void) {
  CALL(LoadAreaPointer, 0x9266); // update level pointer with
  ram[PlayerSize] = 0x1; // actual world and area numbers, then; reset player's size, status, and
  ram[FetchNewGameTimerFlag]++; // set game timer flag to reload
  a = 0x0; // game timer from header
  ram[TimerControl] = a; // also set flag for timers to count again
  ram[PlayerStatus] = a;
  ram[GameEngineSubroutine] = a; // reset task for game core
  ram[OperMode_Task] = a; // set modes and leave
  ram[OperMode] = 0x1; // if in game over mode, switch back to; game mode, because game is still on
  // GameIsOn:
  return;
}

// in: none; out: carry
void TransposePlayers(void) {
  carry_flag = true; // set carry flag by default to end game
  a = ram[NumberOfPlayers]; zero_flag = a == 0; // if only a 1 player game, leave
  if (zero_flag) {
    return;
  }
  a = ram[OffScr_NumberofLives]; neg_flag = (a & 0x80) != 0; // does offscreen player have any lives left?
  // branch if not
  if (neg_flag) {
    return;
  }
  a = ram[CurrentPlayer]; // invert bit to update
  a ^= 0b00000001; // which player is on the screen
  ram[CurrentPlayer] = a;
  x = 0x6;
  do {
    // TransLoop:
    a = ram[(OnscreenPlayerInfo + x) & 0x7ff]; // transpose the information
    ram[0x100 + sp--] = a; // of the onscreen player
    ram[(OnscreenPlayerInfo + x) & 0x7ff] = ram[(OffscreenPlayerInfo + x) & 0x7ff]; // with that of the offscreen player
    a = ram[0x100 + ++sp];
    ram[(OffscreenPlayerInfo + x) & 0x7ff] = a;
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  carry_flag = false; // clear carry flag to get game going
  // ExTrans:
  return;
}

// -------------------------------------------------------------------------------------
void DoNothing1(void) {
  ram[0x6c9] = 0xff; // this is residual code, this value is; not used anywhere in the program
  DoNothing2(); // fallthrough
  return;
}

void DoNothing2(void) {
  return;
}

// -------------------------------------------------------------------------------------
void AreaParserTaskHandler(void) {
  y = ram[AreaParserTaskNum]; zero_flag = y == 0; // check number of tasks here
  // Original branch: if already set, go ahead
  if (zero_flag) {
    y = 0x8;
    ram[AreaParserTaskNum] = y; // otherwise, set eight by default
  }
  // DoAPTasks:
  y--;
  a = y;
  CALL(AreaParserTasks, 0x92be);
  ram[AreaParserTaskNum]--; zero_flag = ram[AreaParserTaskNum] == 0; // if all tasks not complete do not
  // render attribute table yet
  if (!zero_flag) {
    return;
  }
  CALL(RenderAttributeTables, 0x92c6);
  // SkipATRender:
  return;
}

// in: a; out: none
void AreaParserTasks(void) {
  switch (JumpEngine(0x92ca)) {
    case 0x92db: IncrementColumnPos(); return;
    case 0x88ae: RenderAreaGraphics(); return;
    case 0x93fc: AreaParserCore(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void IncrementColumnPos(void) {
  ram[CurrentColumnPos]++; // increment column where we're at
  a = ram[CurrentColumnPos];
  a &= 0b00001111; zero_flag = a == 0; // mask out higher nybble
  if (zero_flag) {
    ram[CurrentColumnPos] = a; // if no bits left set, wrap back to zero (0-f)
    ram[CurrentPageLoc]++; // and increment page number where we're at
  }
  // NoColWrap:
  ram[BlockBufferColumnPos]++; // increment column offset where we're at
  a = ram[BlockBufferColumnPos];
  a &= 0b00011111; // mask out all but 5 LSB (0-1f)
  ram[BlockBufferColumnPos] = a; // and save
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used as counter, store for low nybble for background, ceiling byte for terrain
// $01 - used to store floor byte for terrain
// $07 - used to store terrain metatile
// $06-$07 - used to store block buffer address
void AreaParserCore(void) {
  a = ram[BackloadingFlag]; zero_flag = a == 0; // check to see if we are starting right of start
  // Original branch: if not, go ahead and render background, foreground and terrain
  if (!zero_flag) {
    CALL(ProcessAreaData, 0x9403); // otherwise skip ahead and load level data
  }
  // RenderSceneryTerrain:
  x = 0xc;
  a = 0x0;
  do {
    // ClrMTBuf:
    ram[MetatileBuffer + x] = a; // clear out metatile buffer
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  y = ram[BackgroundScenery]; zero_flag = y == 0; // do we need to render the background scenery?
  // Original branch: if not, skip to check the foreground
  if (!zero_flag) {
    a = ram[CurrentPageLoc]; // otherwise check for every third page
    do {
      // ThirdP:
      neg_flag = ((a - 0x3) & 0x80) != 0;
      if (neg_flag) { goto RendBack; } // if less than three we're there
      carry_flag = true;
      a = (uint8_t)(a - 0x3 - !carry_flag); neg_flag = (a & 0x80) != 0; // if 3 or more, subtract 3 and 
    } while (!neg_flag); // do an unconditional branch
    
RendBack:
    a = (uint8_t)(a << 1); // move results to higher nybble
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    { uint16_t sum = a + ROM((BSceneDataOffsets - 1) + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add to it offset loaded from here
    a = (uint8_t)(a + ram[CurrentColumnPos] + carry_flag); // add to the result our current column position
    x = a;
    a = ROM(BackSceneryData + x); zero_flag = a == 0; // load data from sum of offsets
    if (zero_flag) { goto RendFore; } // if zero, no scenery for that part
    ram[0x100 + sp--] = a;
    a &= 0xf; // save to stack and clear high nybble
    carry_flag = true;
    a = (uint8_t)(a - 0x1 - !carry_flag); // subtract one (because low nybble is $01-$0c)
    ram[0x0] = a; // save low nybble
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // multiply by three (shift to left and add result to old one)
    a = (uint8_t)(a + ram[0x0] + carry_flag); // note that since d7 was nulled, the carry flag is always clear
    x = a; // save as offset for background scenery metatile data
    a = ram[0x100 + ++sp]; // get high nybble from stack, move low
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    y = a; // use as second offset (used to determine height)
    ram[0x0] = 0x3; // use previously saved memory location for counter
    do {
      // SceLoop1:
      ram[MetatileBuffer + y] = ROM(BackSceneryMetatiles + x); // load metatile data from offset of (lsb - 1) * 3; store into buffer from offset of (msb / 16)
      x++;
      y++;
      zero_flag = y == 0xb; // if at this location, leave loop
      if (zero_flag) { goto RendFore; }
      ram[0x0]--; zero_flag = ram[0x0] == 0; // decrement until counter expires, barring exception
    } while (!zero_flag);
  }
  
RendFore:
  x = ram[ForegroundScenery]; zero_flag = x == 0; // check for foreground data needed or not
  // Original branch: if not, skip this part
  if (!zero_flag) {
    y = ROM((FSceneDataOffsets - 1) + x); // load offset from location offset by header value, then
    x = 0x0; // reinit X
    do {
      // SceLoop2:
      a = ROM(ForeSceneryData + y); zero_flag = a == 0; // load data until counter expires
      // Original branch: do not store if zero found
      if (!zero_flag) {
        ram[MetatileBuffer + x] = a;
      }
      // NoFore:
      y++;
      x++;
      zero_flag = x == 0xd; // store up to end of metatile buffer
    } while (!zero_flag);
  }
  // RendTerr:
  y = ram[AreaType]; zero_flag = y == 0; // check world type for water level
  // Original branch: if not water level, skip this part
  if (zero_flag) {
    a = ram[WorldNumber]; // check world number, if not world number eight
    zero_flag = a == World8; // then skip this part
    if (!zero_flag) { goto TerMTile; }
    a = 0x62; // if set as water level and world number eight,
  } else {
    
TerMTile:
    a = ROM(TerrainMetatiles + y); // otherwise get appropriate metatile for area type
    y = ram[CloudTypeOverride]; zero_flag = y == 0; // check for cloud type override
    if (zero_flag) { goto StoreMT; } // if not set, keep value otherwise
    a = 0x88; // use cloud block terrain
  }
  
StoreMT:
  ram[0x7] = a; // store value here
  x = 0x0; // initialize X, use as metatile buffer offset
  a = ram[TerrainControl]; // use yet another value from the header
  a = (uint8_t)(a << 1); // multiply by 2 and use as yet another offset
  y = a;
  do {
    // TerrLoop:
    ram[0x0] = ROM(TerrainRenderBits + y); // get one of the terrain rendering bit data
    y++; // increment Y and use as offset next time around
    ram[0x1] = y;
    a = ram[CloudTypeOverride]; zero_flag = a == 0; // skip if value here is zero
    if (!zero_flag) {
      zero_flag = x == 0x0; // otherwise, check if we're doing the ceiling byte
      if (zero_flag) { goto NoCloud2; }
      a = ram[0x0]; // if not, mask out all but d3
      a &= 0b00001000;
      ram[0x0] = a;
    }
    
NoCloud2:
    y = 0x0; // start at beginning of bitmasks
    do {
      // TerrBChk:
      a = ROM(Bitmasks + y); // load bitmask, then perform AND on contents of first byte
      zero_flag = (a & ram[0x0]) == 0;
      // Original branch: if not set, skip this part (do not write terrain to buffer)
      if (!zero_flag) {
        ram[MetatileBuffer + x] = ram[0x7]; // load terrain type metatile number and store into buffer here
      }
      // NextTBit:
      x++; // continue until end of buffer
      zero_flag = x == 0xd;
      if (zero_flag) { goto RendBBuf; } // if we're at the end, break out of this loop
      a = ram[AreaType]; // check world type for underground area
      // Original branch: if not underground, skip this part
      if (a == 0x2) {
        zero_flag = x == 0xb;
        if (!zero_flag) { goto EndUChk; } // if we're at the bottom of the screen, override
        ram[0x7] = 0x54; // old terrain type with ground level terrain type
      }
      
EndUChk:
      y++; // increment bitmasks offset in Y
      zero_flag = y == 0x8;
    } while (!zero_flag); // if not all bits checked, loop back    
    y = ram[0x1]; zero_flag = y == 0;
  } while (!zero_flag); // unconditional branch, use Y to load next byte
  
RendBBuf:
  CALL(ProcessAreaData, 0x94d5); // do the area data loading routine now
  a = ram[BlockBufferColumnPos];
  CALL(GetBlockBufferAddr, 0x94db); // get block buffer address from where we're at
  x = 0x0;
  y = 0x0; // init index regs and start at beginning of smaller buffer
  do {
    // ChkMTLow:
    ram[0x0] = y;
    a = ram[MetatileBuffer + x]; // load stored metatile number
    a &= 0b11000000; // mask out all but 2 MSB
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // make %xx000000 into %000000xx
    a = (uint8_t)(a << 1 | carry_flag);
    y = a; // use as offset in Y
    a = ram[MetatileBuffer + x]; // reload original unmasked value here
    carry_flag = a >= ROM(BlockBuffLowBounds + y); // check for certain values depending on bits set
    // Original branch: if equal or greater, branch
    if (!carry_flag) {
      a = 0x0; // if less, init value before storing
    }
    // StrBlock:
    y = ram[0x0]; // get offset for block buffer
    dynamic_ram_write(indirect_y_addr(0x6), a); // store value into block buffer
    a = y;
    carry_flag = false; // add 16 (move down one row) to offset
    a = (uint8_t)(a + 0x10 + carry_flag);
    y = a;
    x++; // increment column value
    carry_flag = x >= 0xd;
  } while (!carry_flag); // continue until we pass last row, then leave
  return;
}

// numbers lower than these with the same attribute bits
// will not be stored in the block buffer
// in: none; out: x
void ProcessAreaData(void) {
  do {
    
ProcessAreaData:
    x = 0x2; // start at the end of area object buffer
    do {
      // ProcADLoop:
      ram[ObjectOffset] = x;
      ram[BehindAreaParserFlag] = 0x0; // reset flag
      y = ram[AreaDataOffset]; // get offset of area data pointer
      a = read_byte(indirect_y_addr(AreaData)); // get first byte of area object
      // if end-of-area, skip all this crap
      if (a != 0xfd) {
        a = ram[(AreaObjectLength + x) & 0x7ff]; neg_flag = (a & 0x80) != 0; // check area object buffer flag
        if (!neg_flag) { goto RdyDecode; } // if buffer not negative, branch, otherwise
        y++;
        a = read_byte(indirect_y_addr(AreaData)); // get second byte of area object
        carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // check for page select bit (d7), branch if not set
        if (carry_flag) {
          a = ram[AreaObjectPageSel]; zero_flag = a == 0; // check page select
          if (!zero_flag) { goto Chk1Row13; }
          ram[AreaObjectPageSel]++; // if not already set, set it now
          ram[AreaObjectPageLoc]++; // and increment page location
        }
        
Chk1Row13:
        y--;
        a = read_byte(indirect_y_addr(AreaData)); // reread first byte of level object
        a &= 0xf; // mask out high nybble
        // row 13?
        if (a == 0xd) {
          y++; // if so, reread second byte of level object
          a = read_byte(indirect_y_addr(AreaData));
          y--; // decrement to get ready to read first byte
          a &= 0b01000000; zero_flag = a == 0; // check for d6 set (if not, object is page control)
          if (!zero_flag) { goto CheckRear; }
          a = ram[AreaObjectPageSel]; zero_flag = a == 0; // if page select is set, do not reread
          if (!zero_flag) { goto CheckRear; }
          y++; // if d6 not set, reread second byte
          a = read_byte(indirect_y_addr(AreaData));
          a &= 0b00011111; // mask out all but 5 LSB and store in page control
          ram[AreaObjectPageLoc] = a;
          ram[AreaObjectPageSel]++; // increment page select
          goto NextAObj;
        }
        // Chk1Row14:
        // row 14?
        if (a == 0xe) {
          a = ram[BackloadingFlag]; zero_flag = a == 0; // check flag for saved page number and branch if set
          if (!zero_flag) { goto RdyDecode; } // to render the object (otherwise bg might not look right)
        }
        
CheckRear:
        a = ram[AreaObjectPageLoc]; // check to see if current page of level object is
        carry_flag = a >= ram[CurrentPageLoc]; // behind current page of renderer
        if (!carry_flag) { goto SetBehind; } // if so branch
      }
      
RdyDecode:
      CALL(DecodeAreaData, 0x9567); // do sub and do not turn on flag
      goto ChkLength;
      
SetBehind:
      ram[BehindAreaParserFlag]++; // turn on flag if object is behind renderer
      
NextAObj:
      CALL(IncAreaObjOffset, 0x9570); // increment buffer offset and move on
      
ChkLength:
      x = ram[ObjectOffset]; // get buffer offset
      a = ram[(AreaObjectLength + x) & 0x7ff]; neg_flag = (a & 0x80) != 0; // check object length for anything stored here
      // Original branch: if not, branch to handle loopback
      if (!neg_flag) {
        ram[(AreaObjectLength + x) & 0x7ff]--; // otherwise decrement length or get rid of it
      }
      // ProcLoopb:
      x--; neg_flag = (x & 0x80) != 0; // decrement buffer offset
    } while (!neg_flag); // and loopback unless exceeded buffer
    a = ram[BehindAreaParserFlag]; zero_flag = a == 0; // check for flag set if objects were behind renderer
    if (!zero_flag) { goto ProcessAreaData; } // branch if true to load more level data, otherwise
    a = ram[BackloadingFlag]; zero_flag = a == 0; // check for flag set if starting right of page $00
  } while (!zero_flag); // branch if true to load more level data, otherwise leave
  // EndAParse:
  return;
}

void IncAreaObjOffset(void) {
  ram[AreaDataOffset]++; // increment offset of level pointer
  ram[AreaDataOffset]++;
  ram[AreaObjectPageSel] = 0x0; // reset page select
  return;
}

// in: x, y; out: none
void DecodeAreaData(void) {
  a = ram[(AreaObjectLength + x) & 0x7ff]; neg_flag = (a & 0x80) != 0; // check current buffer flag
  if (!neg_flag) {
    y = ram[(AreaObjOffsetBuffer + x) & 0x7ff]; // if not, get offset from buffer
  }
  // Chk1stB:
  x = 0x10; // load offset of 16 for special row 15
  a = read_byte(indirect_y_addr(AreaData)); // get first byte of level object again
  // if end of level, leave this routine
  if (a == 0xfd) {
    return;
  }
  a &= 0xf; // otherwise, mask out low nybble
  // row 15?
  // Original branch: if so, keep the offset of 16
  if (a != 0xf) {
    x = 0x8; // otherwise load offset of 8 for special row 12
    zero_flag = a == 0xc; // row 12?
    if (zero_flag) { goto ChkRow14; } // if so, keep the offset value of 8
    x = 0x0; // otherwise nullify value by default
  }
  
ChkRow14:
  ram[0x7] = x; // store whatever value we just loaded here
  x = ram[ObjectOffset]; // get object offset again
  // row 14?
  if (a == 0xe) {
    ram[0x7] = 0x0; // if so, load offset with $00
    a = 0x2e; // and load A with another value
  } else {
    // ChkRow13:
    // row 13?
    if (a == 0xd) {
      ram[0x7] = 0x22; // if so, load offset with 34
      y++; // get next byte
      a = read_byte(indirect_y_addr(AreaData));
      a &= 0b01000000; zero_flag = a == 0; // mask out all but d6 (page control obj bit)
      // if d6 clear, branch to leave (we handled this earlier)
      if (zero_flag) {
        return;
      }
      a = read_byte(indirect_y_addr(AreaData)); // otherwise, get byte again
      a &= 0b01111111; // mask out d7
      // check for loop command in low nybble
      // Original branch: (plus d6 set for object other than page control)
      if (a == 0x4b) {
        ram[LoopCommand]++; // if loop command, set loop command flag
      }
      // Mask2MSB:
      a &= 0b00111111; // mask out d7 and d6
      goto NormObj; // and jump
    }
    // ChkSRows:
    // row 12-15?
    if (a < 0xc) {
      y++; // if not, get second byte of level object
      a = read_byte(indirect_y_addr(AreaData));
      a &= 0b01110000; zero_flag = a == 0; // mask out all but d6-d4
      // Original branch: if any bits set, branch to handle large object
      if (zero_flag) {
        ram[0x7] = 0x16; // otherwise set offset of 24 for small object
        a = read_byte(indirect_y_addr(AreaData)); // reload second byte of level object
        a &= 0b00001111; // mask out higher nybble and jump
        goto NormObj;
      }
      // LrgObj:
      ram[0x0] = a; // store value here (branch for large objects)
      // check for vertical pipe object
      if (a == 0x70) {
        a = read_byte(indirect_y_addr(AreaData)); // if not, reload second byte
        a &= 0b00001000; zero_flag = a == 0; // mask out all but d3 (usage control bit)
        if (zero_flag) { goto NotWPipe; } // if d3 clear, branch to get original value
        ram[0x0] = 0x0; // otherwise, nullify value for warp pipe
      }
      
NotWPipe:
      a = ram[0x0]; // get value and jump ahead
    } else {
      // SpecObj:
      y++; // branch here for rows 12-15
      a = read_byte(indirect_y_addr(AreaData));
      a &= 0b01110000; // get next byte and mask out all but d6-d4
    }
    // MoveAOId:
    a = a >> 1; // move d6-d4 to lower nybble
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
  }
  
NormObj:
  ram[0x0] = a; // store value here (branch for small objects and rows 13 and 14)
  a = ram[(AreaObjectLength + x) & 0x7ff]; neg_flag = (a & 0x80) != 0; // is there something stored here already?
  // Original branch: if so, branch to do its particular sub
  if (neg_flag) {
    a = ram[AreaObjectPageLoc]; // otherwise check to see if the object we've loaded is on the
    zero_flag = a == ram[CurrentPageLoc]; // same page as the renderer, and if so, branch
    if (!zero_flag) {
      y = ram[AreaDataOffset]; // if not, get old offset of level pointer
      a = read_byte(indirect_y_addr(AreaData)); // and reload first byte
      a &= 0b00001111;
      // row 14?
      if (a != 0xe) {
        return;
      }
      a = ram[BackloadingFlag]; zero_flag = a == 0; // if so, check backloading flag
      if (!zero_flag) { goto StrAObj; } // if set, branch to render object, else leave
      // LeavePar:
      return;
    }
    // InitRear:
    a = ram[BackloadingFlag]; zero_flag = a == 0; // check backloading flag to see if it's been initialized
    // Original branch: branch to column-wise check
    if (!zero_flag) {
      a = 0x0; // if not, initialize both backloading and 
      ram[BackloadingFlag] = a; // behind-renderer flags and leave
      ram[BehindAreaParserFlag] = a;
      ram[ObjectOffset] = a;
      LoopCmdE(); // fallthrough
      return;
    }
    // BackColC:
    y = ram[AreaDataOffset]; // get first byte again
    a = read_byte(indirect_y_addr(AreaData));
    a &= 0b11110000; // mask out low nybble and move high to low
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    zero_flag = a == ram[CurrentColumnPos]; // is this where we're at?
    // if not, branch to leave
    if (!zero_flag) {
      return;
    }
    
StrAObj:
    ram[(AreaObjOffsetBuffer + x) & 0x7ff] = ram[AreaDataOffset]; // if so, load area obj offset and store in buffer
    CALL(IncAreaObjOffset, 0x965e); // do sub to increment to next object data
  }
  // RunAObj:
  a = ram[0x0]; // get stored value and add offset to it
  carry_flag = false; // then use the jump engine with current contents of A
  a = (uint8_t)(a + ram[0x7] + carry_flag);
  switch (JumpEngine(0x9666)) {
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
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void LoopCmdE(void) {
  return;
}

// in: x; out: none
void AlterAreaAttributes(void) {
  y = ram[(AreaObjOffsetBuffer + x) & 0x7ff]; // load offset for level object data saved in buffer
  y++; // load second byte
  a = read_byte(indirect_y_addr(AreaData));
  ram[0x100 + sp--] = a; // save in stack for now
  a &= 0b01000000; zero_flag = a == 0;
  // Original branch: branch if d6 is set
  if (zero_flag) {
    a = ram[0x100 + ++sp];
    ram[0x100 + sp--] = a; // pull and push offset to copy to A
    a &= 0b00001111; // mask out high nybble and store as
    ram[TerrainControl] = a; // new terrain height type bits
    a = ram[0x100 + ++sp];
    a &= 0b00110000; // pull and mask out all but d5 and d4
    a = a >> 1; // move bits to lower nybble and store
    a = a >> 1; // as new background scenery bits
    a = a >> 1;
    a = a >> 1;
    ram[BackgroundScenery] = a; // then leave
    return;
  }
  // Alter2:
  a = ram[0x100 + ++sp];
  a &= 0b00000111; // mask out all but 3 LSB
  // if four or greater, set color control bits
  // Original branch: and nullify foreground scenery bits
  if (a >= 0x4) {
    ram[BackgroundColorCtrl] = a;
    a = 0x0;
  }
  // SetFore:
  ram[ForegroundScenery] = a; // otherwise set new foreground scenery bits
  return;
}

// --------------------------------
void ScrollLockObject_Warp(void) {
  x = 0x4; // load value of 4 for game text routine as default
  a = ram[WorldNumber]; zero_flag = a == 0; // warp zone (4-3-2), then check world number
  if (!zero_flag) {
    x++; // if world number > 1, increment for next warp zone (5)
    y = ram[AreaType]; // check area type
    y--; zero_flag = y == 0;
    if (!zero_flag) { goto WarpNum; } // if ground area type, increment for last warp zone
    x++; // (8-7-6) and move on
  }
  
WarpNum:
  a = x;
  ram[WarpZoneControl] = a; // store number here to be used by warp zone routine
  CALL(WriteGameText, 0x9707); // print text and warp zone numbers
  a = PiranhaPlant;
  CALL(KillEnemies, 0x970c); // load identifier for piranha plants and do sub
  ScrollLockObject(); // fallthrough
  return;
}

void ScrollLockObject(void) {
  a = ram[ScrollLock]; // invert scroll lock to turn it on
  a ^= 0b00000001;
  ram[ScrollLock] = a;
  return;
}

// --------------------------------
// $00 - used to store enemy identifier in KillEnemies
// in: a; out: none
void KillEnemies(void) {
  ram[0x0] = a; // store identifier here
  a = 0x0;
  x = 0x4; // check for identifier in enemy object buffer
  do {
    // KillELoop:
    y = ram[(uint8_t)(Enemy_ID + x)];
    zero_flag = y == ram[0x0]; // if not found, branch
    if (zero_flag) {
      ram[(uint8_t)(Enemy_Flag + x)] = a; // if found, deactivate enemy object flag
    }
    // NoKillE:
    x--; neg_flag = (x & 0x80) != 0; // do this until all slots are checked
  } while (!neg_flag);
  return;
}

// --------------------------------
void AreaFrenzy(void) {
  x = ram[0x0]; // use area object identifier bit as offset
  a = ROM((FrenzyIDData - 8) + x); // note that it starts at 8, thus weird address here
  y = 0x5;
  do {
    // FreCompLoop:
    y--; neg_flag = (y & 0x80) != 0; // check regular slots of enemy object buffer
    if (neg_flag) { goto ExitAFrenzy; } // if all slots checked and enemy object not found, branch to store
    zero_flag = a == ram[Enemy_ID + y]; // check for enemy object in buffer versus frenzy object
  } while (!zero_flag);
  a = 0x0; // if enemy object already present, nullify queue and leave
  
ExitAFrenzy:
  ram[EnemyFrenzyQueue] = a; // store enemy into frenzy queue
  return;
}

// --------------------------------
// $06 - used by MushroomLedge to store length
// in: x; out: none
void AreaStyleObject(void) {
  a = ram[AreaStyle]; // load level object style and jump to the right sub
  switch (JumpEngine(0x9745)) {
    case 0x974c: TreeLedge(); return;
    case 0x9778: MushroomLedge(); return;
    case 0x9a69: BulletBillCannon(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: none
void TreeLedge(void) {
  CALL(GetLrgObjAttrib, 0x974e); // get row and length of green ledge
  a = ram[(AreaObjectLength + x) & 0x7ff]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // check length counter for expiration
  if (!zero_flag) {
    if (neg_flag) {
      ram[(AreaObjectLength + x) & 0x7ff] = y; // store lower nybble into buffer flag as length of ledge
      a = ram[CurrentPageLoc];
      a |= ram[CurrentColumnPos]; zero_flag = a == 0; // are we at the start of the level?
      if (zero_flag) { goto MidTreeL; }
      a = 0x16; // render start of tree ledge
      NoUnder(); return;
    }
    
MidTreeL:
    x = ram[0x7];
    ram[MetatileBuffer + x] = 0x17; // render middle of tree ledge; note that this is also used if ledge position is
    a = 0x4c; // at the start of level for continuous effect
    AllUnder(); return; // now render the part underneath
  }
  // EndTreeL:
  a = 0x18; // render end of tree ledge
  NoUnder(); return;
}

// in: x; out: none
void MushroomLedge(void) {
  CALL(ChkLrgObjLength, 0x977a); // get shroom dimensions
  ram[0x6] = y; // store length here for now
  if (carry_flag) {
    a = ram[(AreaObjectLength + x) & 0x7ff]; // divide length by 2 and store elsewhere
    a = a >> 1;
    ram[(MushroomLedgeHalfLen + x) & 0x7ff] = a;
    a = 0x19; // render start of mushroom
    NoUnder(); return;
  }
  // EndMushL:
  a = 0x1b; // if at the end, render end of mushroom
  y = ram[(AreaObjectLength + x) & 0x7ff]; zero_flag = y == 0;
  if (zero_flag) {
    NoUnder();
    return;
  }
  ram[0x6] = ram[(MushroomLedgeHalfLen + x) & 0x7ff]; // get divided length and store where length; was stored originally
  x = ram[0x7];
  ram[MetatileBuffer + x] = 0x1a; // render middle of mushroom
  zero_flag = y == ram[0x6]; // are we smack dab in the center?
  // if not, branch to leave
  if (!zero_flag) {
    return;
  }
  x++;
  ram[MetatileBuffer + x] = 0x4f; // render stem top of mushroom underneath the middle
  a = 0x50;
  AllUnder(); // fallthrough
  return;
}

// in: a, x; out: none
void AllUnder(void) {
  x++;
  y = 0xf; // set $0f to render all way down
  RenderUnderPart(); return; // now render the stem of mushroom
}

// in: a; out: none
void NoUnder(void) {
  x = ram[0x7]; // load row of ledge
  y = 0x0; // set 0 for no bottom on this part
  RenderUnderPart(); return;
}

// --------------------------------
// tiles used by pulleys and rope object
// in: x; out: none
void PulleyRopeObject(void) {
  CALL(ChkLrgObjLength, 0x97bc); // get length of pulley/rope object
  y = 0x0; // initialize metatile offset
  // Original branch: if starting, render left pulley
  if (!carry_flag) {
    y++;
    a = ram[(AreaObjectLength + x) & 0x7ff]; zero_flag = a == 0; // if not at the end, render rope
    if (!zero_flag) { goto RenderPul; }
    y++; // otherwise render right pulley
  }
  
RenderPul:
  ram[MetatileBuffer] = ROM(PulleyRopeMetatiles + y); // render at the top of the screen
  // MushLExit:
  return; // and leave
}

// --------------------------------
// $06 - used to store upper limit of rows for CastleObject
// in: x; out: none
void CastleObject(void) {
  CALL(GetLrgObjAttrib, 0x9808); // save lower nybble as starting row
  ram[0x7] = y; // if starting row is above $0a, game will crash!!!
  y = 0x4;
  CALL(ChkLrgObjFixedLength, 0x980f); // load length of castle if not already loaded
  a = x;
  ram[0x100 + sp--] = a; // save obj buffer offset to stack
  y = ram[(AreaObjectLength + x) & 0x7ff]; // use current length as offset for castle data
  x = ram[0x7]; // begin at starting row
  ram[0x6] = 0xb; // load upper limit of number of rows to print
  do {
    // CRendLoop:
    ram[MetatileBuffer + x] = ROM(CastleMetatiles + y); // load current byte using offset
    x++; // store in buffer and increment buffer offset
    a = ram[0x6]; zero_flag = a == 0;
    // Original branch: have we reached upper limit yet?
    if (!zero_flag) {
      y++; // if not, increment column-wise
      y++; // to byte in next row
      y++;
      y++;
      y++;
      ram[0x6]--; // move closer to upper limit
    }
    // ChkCFloor:
    zero_flag = x == 0xb; // have we reached the row just before floor?
  } while (!zero_flag); // if not, go back and do another row
  a = ram[0x100 + ++sp];
  x = a; // get obj buffer offset from before
  a = ram[CurrentPageLoc]; zero_flag = a == 0;
  // if we're at page 0, we do not need to do anything else
  if (zero_flag) {
    return;
  }
  a = ram[(AreaObjectLength + x) & 0x7ff]; // check length
  // if length almost about to expire, put brick at floor
  if (a != 0x1) {
    y = ram[0x7]; zero_flag = y == 0; // check starting row for tall castle ($00)
    if (zero_flag) {
      zero_flag = a == 0x3; // if found, then check to see if we're at the second column
      if (zero_flag) { goto PlayerStop; }
    }
    // NotTall:
    // if not tall castle, check to see if we're at the third column
    // if we aren't and the castle is tall, don't create flag yet
    if (a != 0x2) {
      return;
    }
    CALL(GetAreaObjXPosition, 0x984d); // otherwise, obtain and save horizontal pixel coordinate
    ram[0x100 + sp--] = a;
    CALL(FindEmptyEnemySlot, 0x9851); // find an empty place on the enemy object buffer
    a = ram[0x100 + ++sp];
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // then write horizontal coordinate for star flag
    ram[(uint8_t)(Enemy_PageLoc + x)] = ram[CurrentPageLoc]; // set page location for star flag
    a = 0x1;
    ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // set vertical high byte
    ram[(uint8_t)(Enemy_Flag + x)] = a; // set flag for buffer
    ram[(uint8_t)(Enemy_Y_Position + x)] = 0x90; // set vertical coordinate
    ram[(uint8_t)(Enemy_ID + x)] = StarFlagObject; // set star flag value in buffer itself
    return;
  }
  
PlayerStop:
  ram[(MetatileBuffer + 10)] = 0x52; // put brick at floor to stop player at end of level; this is only done if we're on the second column
  // ExitCastle:
  return;
}

// --------------------------------
// in: x; out: none
void WaterPipe(void) {
  CALL(GetLrgObjAttrib, 0x9871); // get row and lower nybble
  y = ram[(AreaObjectLength + x) & 0x7ff]; // get length (residual code, water pipe is 1 col thick)
  x = ram[0x7]; // get row
  ram[MetatileBuffer + x] = 0x6b; // draw something here and below it
  ram[(MetatileBuffer + 1) + x] = 0x6c;
  return;
}

// --------------------------------
// $05 - used to store length of vertical shaft in RenderSidewaysPipe
// $06 - used to store leftover horizontal length in RenderSidewaysPipe
//  and vertical length in VerticalPipe and GetPipeHeight
// in: x; out: none
void IntroPipe(void) {
  y = 0x3; // check if length set, if not set, set it
  CALL(ChkLrgObjFixedLength, 0x9886);
  y = 0xa; // set fixed value and render the sideways part
  CALL(RenderSidewaysPipe, 0x988b);
  // if carry flag set, not time to draw vertical pipe part
  if (carry_flag) {
    return;
  }
  x = 0x6; // blank everything above the vertical pipe part
  do {
    // VPipeSectLoop:
    ram[MetatileBuffer + x] = 0x0; // all the way to the top of the screen; because otherwise it will look like exit pipe
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag);
  ram[(MetatileBuffer + 7)] = ROM(VerticalPipeData + y); // draw the end of the vertical pipe part
  // NoBlankP:
  return;
}

// in: x; out: none
void ExitPipe(void) {
  y = 0x3; // check if length set, if not set, set it
  CALL(ChkLrgObjFixedLength, 0x98af);
  CALL(GetLrgObjAttrib, 0x98b2); // get vertical length, then plow on through RenderSidewaysPipe
  RenderSidewaysPipe(); // fallthrough
  return;
}

// in: x, y; out: y, carry
void RenderSidewaysPipe(void) {
  y--; // decrement twice to make room for shaft at bottom
  y--; // and store here for now as vertical length
  ram[0x5] = y;
  y = ram[(AreaObjectLength + x) & 0x7ff]; // get length left over and store here
  ram[0x6] = y;
  x = ram[0x5]; // get vertical length plus one, use as buffer offset
  x++;
  a = ROM(SidePipeShaftData + y); // check for value $00 based on horizontal offset
  { uint8_t m = 0x0; carry_flag = a >= m; zero_flag = a == m; }
  // Original branch: if found, do not draw the vertical pipe shaft
  if (!zero_flag) {
    x = 0x0;
    y = ram[0x5]; // init buffer offset and get vertical length
    CALL(RenderUnderPart, 0x98cc); // and render vertical shaft using tile number in A
    carry_flag = false; // clear carry flag to be used by IntroPipe
  }
  // DrawSidePart:
  y = ram[0x6]; // render side pipe part at the bottom
  ram[MetatileBuffer + x] = ROM(SidePipeTopPart + y); // note that the pipe parts are stored
  ram[(MetatileBuffer + 1) + x] = ROM(SidePipeBottomPart + y); // backwards horizontally
  return;
}

// in: x; out: none
void VerticalPipe(void) {
  CALL(GetPipeHeight, 0x98e7);
  a = ram[0x0]; zero_flag = a == 0; // check to see if value was nullified earlier
  // Original branch: (if d3, the usage control bit of second byte, was set)
  if (!zero_flag) {
    y++;
    y++;
    y++;
    y++; // add four if usage control bit was not set
  }
  // WarpPipe:
  a = y; // save value in stack
  ram[0x100 + sp--] = a;
  a = ram[AreaNumber];
  a |= ram[WorldNumber]; zero_flag = a == 0; // if at world 1-1, do not add piranha plant ever
  if (!zero_flag) {
    y = ram[(AreaObjectLength + x) & 0x7ff]; zero_flag = y == 0; // if on second column of pipe, branch
    if (zero_flag) { goto DrawPipe; } // (because we only need to do this once)
    CALL(FindEmptyEnemySlot, 0x9901); // check for an empty moving data buffer space
    if (carry_flag) { goto DrawPipe; } // if not found, too many enemies, thus skip
    CALL(GetAreaObjXPosition, 0x9906); // get horizontal pixel coordinate
    carry_flag = false;
    { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add eight to put the piranha plant in the center
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // store as enemy's horizontal coordinate
    a = ram[CurrentPageLoc]; // add carry to current page number
    a = (uint8_t)(a + 0x0 + carry_flag);
    ram[(uint8_t)(Enemy_PageLoc + x)] = a; // store as enemy's page coordinate
    a = 0x1;
    ram[(uint8_t)(Enemy_Y_HighPos + x)] = a;
    ram[(uint8_t)(Enemy_Flag + x)] = a; // activate enemy flag
    CALL(GetAreaObjYPosition, 0x991b); // get piranha plant's vertical coordinate and store here
    ram[(uint8_t)(Enemy_Y_Position + x)] = a;
    ram[(uint8_t)(Enemy_ID + x)] = PiranhaPlant; // write piranha plant's value into buffer
    CALL(InitPiranhaPlant, 0x9924);
  }
  
DrawPipe:
  a = ram[0x100 + ++sp]; // get value saved earlier and use as Y
  y = a;
  x = ram[0x7]; // get buffer offset
  ram[MetatileBuffer + x] = ROM(VerticalPipeData + y); // draw the appropriate pipe with the Y we loaded earlier; render the top of the pipe
  x++;
  a = ROM((VerticalPipeData + 2) + y); // render the rest of the pipe
  y = ram[0x6]; // subtract one from length and render the part underneath
  y--;
  RenderUnderPart(); return;
}

// in: x; out: y
void GetPipeHeight(void) {
  y = 0x1; // check for length loaded, if not, load
  CALL(ChkLrgObjFixedLength, 0x993d); // pipe length of 2 (horizontal)
  CALL(GetLrgObjAttrib, 0x9940);
  a = y; // get saved lower nybble as height
  a &= 0x7; // save only the three lower bits as
  ram[0x6] = a; // vertical length, then load Y with
  y = ram[(AreaObjectLength + x) & 0x7ff]; // length left over
  return;
}

// in: none; out: x, carry
void FindEmptyEnemySlot(void) {
  x = 0x0; // start at first enemy slot
  do {
    // EmptyChkLoop:
    carry_flag = false; // clear carry flag by default
    a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check enemy buffer for nonzero
    // if zero, leave
    if (zero_flag) {
      return;
    }
    x++;
    { uint8_t m = 0x5; carry_flag = x >= m; zero_flag = x == m; } // if nonzero, check next value
  } while (!zero_flag);
  // ExitEmptyChk:
  return; // if all values nonzero, carry flag is set
}

// --------------------------------
// in: x; out: none
void Hole_Water(void) {
  CALL(ChkLrgObjLength, 0x9959); // get low nybble and save as length
  ram[(MetatileBuffer + 10)] = 0x86; // render waves
  x = 0xb;
  y = 0x1; // now render the water underneath
  a = 0x87;
  RenderUnderPart(); return;
}

// --------------------------------
// in: x; out: none
void QuestionBlockRow_High(void) {
  a = 0x3; // start on the fourth row
  
  loc_39277(); return; // BIT instruction opcode
}

// in: x; out: none
void QuestionBlockRow_Low(void) {
  a = 0x7; // start on the eighth row
  loc_39277(); // fallthrough
  return;
}

// in: a, x; out: none
void loc_39277(void) {
  ram[0x100 + sp--] = a; // save whatever row to the stack for now
  CALL(ChkLrgObjLength, 0x9970); // get low nybble and save as length
  a = ram[0x100 + ++sp];
  x = a; // render question boxes with coins
  ram[MetatileBuffer + x] = 0xc0;
  return;
}

// --------------------------------
// in: x; out: none
void Bridge_High(void) {
  a = 0x6; // start on the seventh row from top of screen
  
  loc_39294(); return; // BIT instruction opcode
}

// in: x; out: none
void Bridge_Middle(void) {
  a = 0x7; // start on the eighth row
  loc_39294(); // fallthrough
  return;
}

// in: a, x; out: none
void loc_39294(void) {
  
  loc_39297(); return; // BIT instruction opcode
}

// in: x; out: none
void Bridge_Low(void) {
  a = 0x9; // start on the tenth row
  loc_39297(); // fallthrough
  return;
}

// in: a, x; out: none
void loc_39297(void) {
  ram[0x100 + sp--] = a; // save whatever row to the stack for now
  CALL(ChkLrgObjLength, 0x9984); // get low nybble and save as length
  a = ram[0x100 + ++sp];
  x = a; // render bridge railing
  ram[MetatileBuffer + x] = 0xb;
  x++;
  y = 0x0; // now render the bridge itself
  a = 0x63;
  RenderUnderPart(); return;
}

// --------------------------------
// in: x; out: none
void FlagBalls_Residual(void) {
  CALL(GetLrgObjAttrib, 0x9996); // get low nybble from object byte
  x = 0x2; // render flag balls on third row from top
  a = 0x6d; // of screen downwards based on low nybble
  RenderUnderPart(); return;
}

// --------------------------------
void FlagpoleObject(void) {
  ram[MetatileBuffer] = 0x24; // render flagpole ball on top
  x = 0x1; // now render the flagpole shaft
  y = 0x8;
  a = 0x25;
  CALL(RenderUnderPart, 0x99ab);
  ram[(MetatileBuffer + 10)] = 0x61; // render solid block at the bottom
  CALL(GetAreaObjXPosition, 0x99b3);
  carry_flag = true; // get pixel coordinate of where the flagpole is,
  { uint16_t diff = (uint16_t)(a - 0x8 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract eight pixels and use as horizontal
  ram[(Enemy_X_Position + 5)] = a; // coordinate for the flag
  a = ram[CurrentPageLoc];
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from page location and use as
  ram[(Enemy_PageLoc + 5)] = a; // page location for the flag
  ram[(Enemy_Y_Position + 5)] = 0x30; // set vertical coordinate for flag
  ram[FlagpoleFNum_Y_Pos] = 0xb0; // set initial vertical coordinate for flagpole's floatey number
  ram[(Enemy_ID + 5)] = FlagpoleFlagObject; // set flag identifier, note that identifier and coordinates
  ram[(Enemy_Flag + 5)]++; // use last space in enemy object buffer
  return;
}

// --------------------------------
void EndlessRope(void) {
  x = 0x0; // render rope from the top to the bottom of screen
  y = 0xf;
  DrawRope(); return;
}

// in: x; out: none
void BalancePlatRope(void) {
  a = x; // save object buffer offset for now
  ram[0x100 + sp--] = a;
  x = 0x1; // blank out all from second row to the bottom
  y = 0xf; // with blank used for balance platform rope
  a = 0x44;
  CALL(RenderUnderPart, 0x99e1);
  a = ram[0x100 + ++sp]; // get back object buffer offset
  x = a;
  CALL(GetLrgObjAttrib, 0x99e6); // get vertical length from lower nybble
  x = 0x1;
  DrawRope(); // fallthrough
  return;
}

// in: x, y; out: none
void DrawRope(void) {
  a = 0x40; // render the actual rope
  RenderUnderPart(); return;
}

// --------------------------------
// in: x; out: none
void RowOfCoins(void) {
  y = ram[AreaType]; // get area type
  a = ROM(CoinMetatileData + y); // load appropriate coin metatile
  GetRow(); return;
}

// --------------------------------
// in: x; out: none
void CastleBridgeObj(void) {
  y = 0xc; // load length of 13 columns
  CALL(ChkLrgObjFixedLength, 0x9a05);
  ChainObj(); return;
}

void AxeObj(void) {
  ram[VRAM_Buffer_AddrCtrl] = 0x8; // load bowser's palette into sprite portion of palette
  ChainObj(); // fallthrough
  return;
}

void ChainObj(void) {
  y = ram[0x0]; // get value loaded earlier from decoder
  x = ROM((C_ObjectRow - 2) + y); // get appropriate row and metatile for object
  a = ROM((C_ObjectMetatile - 2) + y);
  ColObj(); return;
}

// in: x; out: none
void EmptyBlock(void) {
  CALL(GetLrgObjAttrib, 0x9a1b); // get row location
  x = ram[0x7];
  a = 0xc4;
  ColObj(); // fallthrough
  return;
}

// in: a, x; out: none
void ColObj(void) {
  y = 0x0; // column length of 1
  RenderUnderPart(); return;
}

// --------------------------------
// in: x; out: none
void RowOfBricks(void) {
  y = ram[AreaType]; // load area type obtained from area offset pointer
  a = ram[CloudTypeOverride]; zero_flag = a == 0; // check for cloud type override
  if (!zero_flag) {
    y = 0x4; // if cloud type, override area type
  }
  // DrawBricks:
  a = ROM(BrickMetatiles + y); // get appropriate metatile
  GetRow(); return; // and go render it
}

// in: x; out: none
void RowOfSolidBlocks(void) {
  y = ram[AreaType]; // load area type obtained from area offset pointer
  a = ROM(SolidBlockMetatiles + y); // get metatile
  GetRow(); // fallthrough
  return;
}

// in: a, x; out: none
void GetRow(void) {
  ram[0x100 + sp--] = a; // store metatile here
  CALL(ChkLrgObjLength, 0x9a47); // get row number, load length
  DrawRow(); // fallthrough
  return;
}

void DrawRow(void) {
  x = ram[0x7];
  y = 0x0; // set vertical height of 1
  a = ram[0x100 + ++sp];
  RenderUnderPart(); return; // render object
}

// in: x; out: none
void ColumnOfBricks(void) {
  y = ram[AreaType]; // load area type obtained from area offset
  a = ROM(BrickMetatiles + y); // get metatile (no cloud override as for row)
  GetRow2(); return;
}

// in: x; out: none
void ColumnOfSolidBlocks(void) {
  y = ram[AreaType]; // load area type obtained from area offset
  a = ROM(SolidBlockMetatiles + y); // get metatile
  GetRow2(); // fallthrough
  return;
}

// in: a, x; out: none
void GetRow2(void) {
  ram[0x100 + sp--] = a; // save metatile to stack for now
  CALL(GetLrgObjAttrib, 0x9a62); // get length and row
  a = ram[0x100 + ++sp]; // restore metatile
  x = ram[0x7]; // get starting row
  RenderUnderPart(); return; // now render the column
}

// --------------------------------
// in: x; out: none
void BulletBillCannon(void) {
  CALL(GetLrgObjAttrib, 0x9a6b); // get row and length of bullet bill cannon
  x = ram[0x7]; // start at first row
  ram[MetatileBuffer + x] = 0x64; // render bullet bill cannon
  x++;
  y--; neg_flag = (y & 0x80) != 0; // done yet?
  if (!neg_flag) {
    ram[MetatileBuffer + x] = 0x65; // if not, render middle part
    x++;
    y--; neg_flag = (y & 0x80) != 0; // done yet?
    if (neg_flag) { goto SetupCannon; }
    a = 0x66; // if not, render bottom until length expires
    CALL(RenderUnderPart, 0x9a84);
  }
  
SetupCannon:
  x = ram[Cannon_Offset]; // get offset for data used by cannons and whirlpools
  CALL(GetAreaObjYPosition, 0x9a8a); // get proper vertical coordinate for cannon
  ram[Cannon_Y_Position + x] = a; // and store it here
  ram[Cannon_PageLoc + x] = ram[CurrentPageLoc]; // store page number for cannon here
  CALL(GetAreaObjXPosition, 0x9a96); // get proper horizontal coordinate for cannon
  ram[Cannon_X_Position + x] = a; // and store it here
  x++;
  // increment and check offset
  // Original branch: if not yet reached sixth cannon, branch to save offset
  if (x >= 0x6) {
    x = 0x0; // otherwise initialize it
  }
  // StrCOffset:
  ram[Cannon_Offset] = x; // save new offset and leave
  return;
}

// --------------------------------
// in: x; out: none
void StaircaseObject(void) {
  CALL(ChkLrgObjLength, 0x9ab9); // check and load length
  // Original branch: if length already loaded, skip init part
  if (carry_flag) {
    ram[StaircaseControl] = 0x9; // start past the end for the bottom; of the staircase
  }
  // NextStair:
  ram[StaircaseControl]--; // move onto next step (or first if starting)
  y = ram[StaircaseControl];
  x = ROM(StaircaseRowData + y); // get starting row and height to render
  a = ROM(StaircaseHeightData + y);
  y = a;
  a = 0x61; // now render solid block staircase
  RenderUnderPart(); return;
}

// --------------------------------
// in: x; out: none
void Jumpspring(void) {
  CALL(GetLrgObjAttrib, 0x9ad5);
  CALL(FindEmptyEnemySlot, 0x9ad8); // find empty space in enemy object buffer
  CALL(GetAreaObjXPosition, 0x9adb); // get horizontal coordinate for jumpspring
  ram[(uint8_t)(Enemy_X_Position + x)] = a; // and store
  ram[(uint8_t)(Enemy_PageLoc + x)] = ram[CurrentPageLoc]; // store page location of jumpspring
  CALL(GetAreaObjYPosition, 0x9ae5); // get vertical coordinate for jumpspring
  ram[(uint8_t)(Enemy_Y_Position + x)] = a; // and store
  ram[(uint8_t)(Jumpspring_FixedYPos + x)] = a; // store as permanent coordinate here
  ram[(uint8_t)(Enemy_ID + x)] = JumpspringObject; // write jumpspring object to enemy object buffer
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = 0x1; // store vertical high byte
  ram[(uint8_t)(Enemy_Flag + x)]++; // set flag for enemy object buffer
  x = ram[0x7];
  ram[MetatileBuffer + x] = 0x67; // draw metatiles in two rows where jumpspring is
  ram[(MetatileBuffer + 1) + x] = 0x68;
  return;
}

// --------------------------------
// $07 - used to save ID of brick object
// in: x; out: none
void Hidden1UpBlock(void) {
  a = ram[Hidden1UpFlag]; zero_flag = a == 0; // if flag not set, do not render object
  if (zero_flag) {
    return;
  }
  ram[Hidden1UpFlag] = 0x0; // if set, init for the next one
  BrickWithItem(); return; // jump to code shared with unbreakable bricks
}

// in: x; out: none
void QuestionBlock(void) {
  CALL(GetAreaObjectID, 0x9b10); // get value from level decoder routine
  DrawQBlk(); return; // go to render it
}

// in: x; out: none
void BrickWithCoins(void) {
  ram[BrickCoinTimerFlag] = 0x0; // initialize multi-coin timer flag
  BrickWithItem(); // fallthrough
  return;
}

// in: x; out: none
void BrickWithItem(void) {
  CALL(GetAreaObjectID, 0x9b1b); // save area object ID
  ram[0x7] = y;
  a = 0x0; // load default adder for bricks with lines
  y = ram[AreaType]; // check level type for ground level
  y--; zero_flag = y == 0;
  // Original branch: if ground type, do not start with 5
  if (!zero_flag) {
    a = 0x5; // otherwise use adder for bricks without lines
  }
  // BWithL:
  carry_flag = false; // add object ID to adder
  a = (uint8_t)(a + ram[0x7] + carry_flag);
  y = a; // use as offset for metatile
  DrawQBlk(); // fallthrough
  return;
}

// in: x, y; out: none
void DrawQBlk(void) {
  a = ROM(BrickQBlockMetatiles + y); // get appropriate metatile for brick (question block
  ram[0x100 + sp--] = a; // if branched to here from question block routine)
  CALL(GetLrgObjAttrib, 0x9b32); // get row from location byte
  DrawRow(); return; // now render the object
}

// in: none; out: y
void GetAreaObjectID(void) {
  a = ram[0x0]; // get value saved from area parser routine
  carry_flag = true;
  a = (uint8_t)(a - 0x0 - !carry_flag); // possibly residual code
  y = a; // save to Y
  // ExitDecBlock:
  return;
}

// --------------------------------
// in: x; out: none
void Hole_Empty(void) {
  CALL(ChkLrgObjLength, 0x9b43); // get lower nybble and save as length
  // Original branch: skip this part if length already loaded
  if (carry_flag) {
    a = ram[AreaType]; zero_flag = a == 0; // check for water type level
    if (!zero_flag) { goto NoWhirlP; } // if not water type, skip this part
    x = ram[Whirlpool_Offset]; // get offset for data used by cannons and whirlpools
    CALL(GetAreaObjXPosition, 0x9b50); // get proper vertical coordinate of where we're at
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - 0x10 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract 16 pixels
    ram[Whirlpool_LeftExtent + x] = a; // store as left extent of whirlpool
    a = ram[CurrentPageLoc]; // get page location of where we're at
    a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow
    ram[Whirlpool_PageLoc + x] = a; // save as page location of whirlpool
    y++;
    y++; // increment length by 2
    a = y;
    a = (uint8_t)(a << 1); // multiply by 16 to get size of whirlpool
    a = (uint8_t)(a << 1); // note that whirlpool will always be
    a = (uint8_t)(a << 1); // two blocks bigger than actual size of hole
    a = (uint8_t)(a << 1); // and extend one block beyond each edge
    ram[Whirlpool_Length + x] = a; // save size of whirlpool here
    x++;
    // increment and check offset
    // Original branch: if not yet reached fifth whirlpool, branch to save offset
    if (x >= 0x5) {
      x = 0x0; // otherwise initialize it
    }
    // StrWOffset:
    ram[Whirlpool_Offset] = x; // save new offset here
  }
  
NoWhirlP:
  x = ram[AreaType]; // get appropriate metatile, then
  a = ROM(HoleMetatiles + x); // render the hole proper
  x = 0x8;
  y = 0xf; // start at ninth row and go to bottom, run RenderUnderPart
  RenderUnderPart(); // fallthrough
  return;
}

// --------------------------------
// in: a, x, y; out: x, y
void RenderUnderPart(void) {
  do {
    ram[AreaObjectHeight] = y; // store vertical length to render
    y = ram[MetatileBuffer + x]; zero_flag = y == 0; // check current spot to see if there's something
    // Original branch: we need to keep, if nothing, go ahead
    if (!zero_flag) {
      zero_flag = y == 0x17;
      if (zero_flag) { goto WaitOneRow; } // if middle part (tree ledge), wait until next row
      zero_flag = y == 0x1a;
      if (zero_flag) { goto WaitOneRow; } // if middle part (mushroom ledge), wait until next row
      zero_flag = y == 0xc0;
      if (zero_flag) { goto DrawThisRow; } // if question block w/ coin, overwrite
      carry_flag = y >= 0xc0;
      if (carry_flag) { goto WaitOneRow; } // if any other metatile with palette 3, wait until next row
      zero_flag = y == 0x54;
      if (!zero_flag) { goto DrawThisRow; } // if cracked rock terrain, overwrite
      zero_flag = a == 0x50;
      if (zero_flag) { goto WaitOneRow; } // if stem top of mushroom, wait until next row
    }
    
DrawThisRow:
    ram[MetatileBuffer + x] = a; // render contents of A from routine that called this
    
WaitOneRow:
    x++;
    // stop rendering if we're at the bottom of the screen
    if (x >= 0xd) {
      return;
    }
    y = ram[AreaObjectHeight]; // decrement, and stop rendering if there is no more length
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag);
  // ExitUPartR:
  return;
}

// --------------------------------
// in: x; out: y, carry
void ChkLrgObjLength(void) {
  CALL(GetLrgObjAttrib, 0x9bae); // get row location and size (length if branched to from here)
  ChkLrgObjFixedLength(); // fallthrough
  return;
}

// in: x, y; out: carry
void ChkLrgObjFixedLength(void) {
  a = ram[(AreaObjectLength + x) & 0x7ff]; neg_flag = (a & 0x80) != 0; // check for set length counter
  carry_flag = false; // clear carry flag for not just starting
  // if counter not set, load it, otherwise leave alone
  if (!neg_flag) {
    return;
  }
  ram[(AreaObjectLength + x) & 0x7ff] = y; // save length into length counter
  carry_flag = true; // set carry flag if just starting
  // LenSet:
  return;
}

// in: x; out: y
void GetLrgObjAttrib(void) {
  y = ram[(AreaObjOffsetBuffer + x) & 0x7ff]; // get offset saved from area obj decoding routine
  a = read_byte(indirect_y_addr(AreaData)); // get first byte of level object
  a &= 0b00001111;
  ram[0x7] = a; // save row location
  y++;
  a = read_byte(indirect_y_addr(AreaData)); // get next byte, save lower nybble (length or height)
  a &= 0b00001111; // as Y, then leave
  y = a;
  return;
}

// --------------------------------
// in: none; out: a
void GetAreaObjXPosition(void) {
  a = ram[CurrentColumnPos]; // multiply current offset where we're at by 16
  a = (uint8_t)(a << 1); // to obtain horizontal pixel coordinate
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  return;
}

// --------------------------------
// in: none; out: a
void GetAreaObjYPosition(void) {
  a = ram[0x7]; // multiply value by 16
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // this will give us the proper vertical pixel coordinate
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + 32 + carry_flag); // add 32 pixels for the status bar
  return;
}

// -------------------------------------------------------------------------------------
// $06-$07 - used to store block buffer address used as indirect
// in: a; out: none
void GetBlockBufferAddr(void) {
  ram[0x100 + sp--] = a; // take value of A, save
  a = a >> 1; // move high nybble to low
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  y = a; // use nybble as pointer to high byte
  ram[0x7] = ROM((BlockBufferAddr + 2) + y); // of indirect here
  a = ram[0x100 + ++sp];
  a &= 0b00001111; // pull from stack, mask out high nybble
  carry_flag = false;
  a = (uint8_t)(a + ROM(BlockBufferAddr + y) + carry_flag); // add to low byte
  ram[0x6] = a; // store here and leave
  return;
}

// -------------------------------------------------------------------------------------
// unused space
// -------------------------------------------------------------------------------------
void LoadAreaPointer(void) {
  CALL(FindAreaPointer, 0x9c05); // find it and store it here
  ram[AreaPointer] = a;
  GetAreaType(); // fallthrough
  return;
}

// in: a; out: a
void GetAreaType(void) {
  a &= 0b01100000; // mask out all but d6 and d5
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag); // make %0xx00000 into %000000xx
  ram[AreaType] = a; // save 2 MSB as area type
  return;
}

// in: none; out: a
void FindAreaPointer(void) {
  y = ram[WorldNumber]; // load offset from world variable
  a = ROM(WorldAddrOffsets + y);
  carry_flag = false; // add area number used to find data
  a = (uint8_t)(a + ram[AreaNumber] + carry_flag);
  y = a;
  a = ROM(AreaAddrOffsets + y); // from there we have our area pointer
  return;
}

void GetAreaDataAddrs(void) {
  a = ram[AreaPointer]; // use 2 MSB for Y
  CALL(GetAreaType, 0x9c27);
  y = a;
  a = ram[AreaPointer]; // mask out all but 5 LSB
  a &= 0b00011111;
  ram[AreaAddrsLOffset] = a; // save as low offset
  a = ROM(EnemyAddrHOffsets + y); // load base value with 2 altered MSB,
  carry_flag = false; // then add base value to 5 LSB, result
  a = (uint8_t)(a + ram[AreaAddrsLOffset] + carry_flag); // becomes offset for level data
  y = a;
  ram[EnemyDataLow] = ROM(EnemyDataAddrLow + y); // use offset to load pointer
  ram[EnemyDataHigh] = ROM(EnemyDataAddrHigh + y);
  y = ram[AreaType]; // use area type as offset
  a = ROM(AreaDataHOffsets + y); // do the same thing but with different base value
  carry_flag = false;
  a = (uint8_t)(a + ram[AreaAddrsLOffset] + carry_flag);
  y = a;
  ram[AreaDataLow] = ROM(AreaDataAddrLow + y); // use this offset to load another pointer
  ram[AreaDataHigh] = ROM(AreaDataAddrHigh + y);
  y = 0x0; // load first byte of header
  a = read_byte(indirect_y_addr(AreaData));
  ram[0x100 + sp--] = a; // save it to the stack for now
  a &= 0b00000111; // save 3 LSB for foreground scenery or bg color control
  if (a >= 0x4) {
    ram[BackgroundColorCtrl] = a; // if 4 or greater, save value here as bg color control
    a = 0x0;
  }
  // StoreFore:
  ram[ForegroundScenery] = a; // if less, save value here as foreground scenery
  a = ram[0x100 + ++sp]; // pull byte from stack and push it back
  ram[0x100 + sp--] = a;
  a &= 0b00111000; // save player entrance control bits
  a = a >> 1; // shift bits over to LSBs
  a = a >> 1;
  a = a >> 1;
  ram[PlayerEntranceCtrl] = a; // save value here as player entrance control
  a = ram[0x100 + ++sp]; // pull byte again but do not push it back
  a &= 0b11000000; // save 2 MSB for game timer setting
  carry_flag = false;
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // rotate bits over to LSBs
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  ram[GameTimerSetting] = a; // save value here as game timer setting
  y++;
  a = read_byte(indirect_y_addr(AreaData)); // load second byte of header
  ram[0x100 + sp--] = a; // save to stack
  a &= 0b00001111; // mask out all but lower nybble
  ram[TerrainControl] = a;
  a = ram[0x100 + ++sp]; // pull and push byte to copy it to A
  ram[0x100 + sp--] = a;
  a &= 0b00110000; // save 2 MSB for background scenery type
  a = a >> 1;
  a = a >> 1; // shift bits to LSBs
  a = a >> 1;
  a = a >> 1;
  ram[BackgroundScenery] = a; // save as background scenery
  a = ram[0x100 + ++sp];
  a &= 0b11000000;
  carry_flag = false;
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // rotate bits over to LSBs
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; }
  a = (uint8_t)(a << 1 | carry_flag);
  // if set to 3, store here
  // Original branch: and nullify other value
  if (a == 0b00000011) {
    ram[CloudTypeOverride] = a; // otherwise store value in other place
    a = 0x0;
  }
  // StoreStyle:
  ram[AreaStyle] = a;
  a = ram[AreaDataLow]; // increment area data address by 2 bytes
  carry_flag = false;
  { uint16_t sum = a + 0x2 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[AreaDataLow] = a;
  a = ram[AreaDataHigh];
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[AreaDataHigh] = a;
  return;
}

// -------------------------------------------------------------------------------------
// GAME LEVELS DATA
void GameMode(void) {
  a = ram[OperMode_Task];
  switch (JumpEngine(0xaee1)) {
    case 0x8fe4: InitializeArea(); return;
    case 0x8567: ScreenRoutines(); return;
    case 0x9071: SecondaryGameSetup(); return;
    case 0xaeea: GameCoreRoutine(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void GameCoreRoutine(void) {
  x = ram[CurrentPlayer]; // get which player is on the screen
  ram[SavedJoypadBits] = ram[SavedJoypadBits + x]; // use appropriate player's controller bits; as the master controller bits
  CALL(GameRoutines, 0xaef5); // execute one of many possible subs
  a = ram[OperMode_Task]; // check major task of operating mode
  // if we are supposed to be here,
  // Original branch: branch to the game engine itself
  if (a < 0x3) {
    return;
  }
  // GameEngine:
  CALL(ProcFireball_Bubble, 0xaf00); // process fireballs and air bubbles
  x = 0x0;
  do {
    // ProcELoop:
    ram[ObjectOffset] = x; // put incremented offset in X as enemy object offset
    CALL(EnemiesAndLoopsCore, 0xaf07); // process enemy objects
    CALL(FloateyNumbersRoutine, 0xaf0a); // process floatey numbers
    x++;
    zero_flag = x == 0x6; // do these two subroutines until the whole buffer is done
  } while (!zero_flag);
  CALL(GetPlayerOffscreenBits, 0xaf12); // get offscreen bits for player object
  CALL(RelativePlayerPosition, 0xaf15); // get relative coordinates for player object
  CALL(PlayerGfxHandler, 0xaf18); // draw the player
  CALL(BlockObjMT_Updater, 0xaf1b); // replace block objects with metatiles if necessary
  x = 0x1;
  ram[ObjectOffset] = x; // set offset for second
  CALL(BlockObjectsCore, 0xaf22); // process second block object
  x--;
  ram[ObjectOffset] = x; // set offset for first
  CALL(BlockObjectsCore, 0xaf28); // process first block object
  CALL(MiscObjectsCore, 0xaf2b); // process misc objects (hammer, jumping coins)
  CALL(ProcessCannons, 0xaf2e); // process bullet bill cannons
  CALL(ProcessWhirlpools, 0xaf31); // process whirlpools
  CALL(FlagpoleRoutine, 0xaf34); // process the flagpole
  CALL(RunGameTimer, 0xaf37); // count down the game timer
  CALL(ColorRotation, 0xaf3a); // cycle one of the background colors
  a = ram[Player_Y_HighPos];
  neg_flag = ((a - 0x2) & 0x80) != 0; // if player is below the screen, don't bother with the music
  if (neg_flag) {
    a = ram[StarInvincibleTimer]; zero_flag = a == 0; // if star mario invincibility timer at zero,
    if (zero_flag) { goto ClrPlrPal; } // skip this part
    zero_flag = a == 0x4;
    if (!zero_flag) { goto NoChgMus; } // if not yet at a certain point, continue
    a = ram[IntervalTimerControl]; zero_flag = a == 0; // if interval timer not yet expired,
    if (!zero_flag) { goto NoChgMus; } // branch ahead, don't bother with the music
    CALL(GetAreaMusic, 0xaf51); // to re-attain appropriate level music
  }
  
NoChgMus:
  y = ram[StarInvincibleTimer]; // get invincibility timer
  a = ram[FrameCounter]; // get frame counter
  // if timer still above certain point,
  // Original branch: branch to cycle player's palette quickly
  if (y < 0x8) {
    a = a >> 1; // otherwise, divide by 8 to cycle every eighth frame
    a = a >> 1;
  }
  // CycleTwo:
  a = a >> 1; // if branched here, divide by 2 to cycle every other frame
  CALL(CyclePlayerPalette, 0xaf60); // do sub to cycle the palette (note: shares fire flower code)
  goto SaveAB; // then skip this sub to finish up the game engine
  
ClrPlrPal:
  CALL(ResetPalStar, 0xaf66); // do sub to clear player's palette bits in attributes
  
SaveAB:
  ram[PreviousA_B_Buttons] = ram[A_B_Buttons]; // save current A and B button; into temp variable to be used on next frame
  ram[Left_Right_Buttons] = 0x0; // nullify left and right buttons temp variable
  UpdScrollVar(); // fallthrough
  return;
}

void UpdScrollVar(void) {
  a = ram[VRAM_Buffer_AddrCtrl];
  // if vram address controller set to 6 (one of two $0341s)
  // then branch to leave
  if (a == 0x6) {
    return;
  }
  a = ram[AreaParserTaskNum]; zero_flag = a == 0; // otherwise check number of tasks
  if (zero_flag) {
    a = ram[ScrollThirtyTwo]; // get horizontal scroll in 0-31 or $00-$20 range
    { uint8_t m = 0x20; carry_flag = a >= m; neg_flag = ((a - m) & 0x80) != 0; } // check to see if exceeded $21
    // branch to leave if not
    if (neg_flag) {
      return;
    }
    a = ram[ScrollThirtyTwo];
    a = (uint8_t)(a - 0x20 - !carry_flag); // otherwise subtract $20 to set appropriately
    ram[ScrollThirtyTwo] = a; // and store
    ram[VRAM_Buffer2_Offset] = 0x0; // reset vram buffer offset used in conjunction with; level graphics buffer at $0341-$035f
  }
  // RunParser:
  CALL(AreaParserTaskHandler, 0xaf91); // update the name table with more level graphics
  // ExitEng:
  return; // and after all that, we're finally done!
}

// -------------------------------------------------------------------------------------
void ScrollHandler(void) {
  a = ram[Player_X_Scroll]; // load value saved here
  carry_flag = false;
  a = (uint8_t)(a + ram[Platform_X_Scroll] + carry_flag); // add value used by left/right platforms
  ram[Player_X_Scroll] = a; // save as new value here to impose force on scroll
  a = ram[ScrollLock]; zero_flag = a == 0; // check scroll lock flag
  // Original branch: skip a bunch of code here if set
  if (zero_flag) {
    a = ram[Player_Pos_ForScroll];
    carry_flag = a >= 0x50; // check player's horizontal screen position
    if (!carry_flag) { goto InitScrlAmt; } // if less than 80 pixels to the right, branch
    a = ram[SideCollisionTimer]; zero_flag = a == 0; // if timer related to player's side collision
    if (!zero_flag) { goto InitScrlAmt; } // not expired, branch
    y = ram[Player_X_Scroll]; // get value and decrement by one
    y--; neg_flag = (y & 0x80) != 0; // if value originally set to zero or otherwise
    if (neg_flag) { goto InitScrlAmt; } // negative for left movement, branch
    y++;
    // if value $01, branch and do not decrement
    if (y >= 0x2) {
      y--; // otherwise decrement by one
    }
    // ChkNearMid:
    a = ram[Player_Pos_ForScroll];
    // check player's horizontal screen position
    // if less than 112 pixels to the right, branch
    if (a < 0x70) {
      ScrollScreen();
      return;
    }
    y = ram[Player_X_Scroll]; // otherwise get original value undecremented
    ScrollScreen(); // fallthrough
    return;
  }
  
InitScrlAmt:
  ram[ScrollAmount] = 0x0; // initialize value here
  ChkPOffscr(); // fallthrough
  return;
}

// in: y; out: none
void ScrollScreen(void) {
  a = y;
  ram[ScrollAmount] = a; // save value here
  carry_flag = false;
  a = (uint8_t)(a + ram[ScrollThirtyTwo] + carry_flag); // add to value already set here
  ram[ScrollThirtyTwo] = a; // save as new value here
  a = y;
  carry_flag = false;
  { uint16_t sum = a + ram[ScreenLeft_X_Pos] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add to left side coordinate
  ram[ScreenLeft_X_Pos] = a; // save as new left side coordinate
  ram[HorizontalScroll] = a; // save here also
  a = ram[ScreenLeft_PageLoc];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location for left
  ram[ScreenLeft_PageLoc] = a; // side of the screen
  a &= 0x1; // get LSB of page location
  ram[0x0] = a; // save as temp variable for PPU register 1 mirror
  a = ram[Mirror_PPU_CTRL_REG1]; // get PPU register 1 mirror
  a &= 0b11111110; // save all bits except d0
  a |= ram[0x0]; // get saved bit here and save in PPU register 1
  ram[Mirror_PPU_CTRL_REG1] = a; // mirror to be used to set name table later
  CALL(GetScreenPosition, 0xaff2); // figure out where the right side is
  ram[ScrollIntervalTimer] = 0x8; // set scroll timer (residual, not used elsewhere)
  ChkPOffscr(); return; // skip this part
}

void ChkPOffscr(void) {
  x = 0x0; // set X for player offset
  CALL(GetXOffscreenBits, 0xb004); // get horizontal offscreen bits for player
  ram[0x0] = a; // save them here
  y = 0x0; // load default offset (left side)
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // if d7 of offscreen bits are set,
  // Original branch: branch with default offset
  if (!carry_flag) {
    y++; // otherwise use different offset (right side)
    a = ram[0x0];
    a &= 0b00100000; zero_flag = a == 0; // check offscreen bits for d5 set
    if (zero_flag) { goto InitPlatScrl; } // if not set, branch ahead of this part
  }
  // KeepOnscr:
  a = ram[(ScreenEdge_X_Pos + y) & 0x7ff]; // get left or right side coordinate based on offset
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ROM(X_SubtracterData + y) - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract amount based on offset
  ram[Player_X_Position] = a; // store as player position to prevent movement further
  a = ram[(ScreenEdge_PageLoc + y) & 0x7ff]; // get left or right page location based on offset
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow
  ram[Player_PageLoc] = a; // save as player's page location
  a = ram[Left_Right_Buttons]; // check saved controller bits
  zero_flag = a == ROM(OffscrJoypadBitsData + y); // against bits based on offset
  // Original branch: if not equal, branch
  if (!zero_flag) {
    ram[Player_X_Speed] = 0x0; // otherwise nullify horizontal speed of player
  }
  
InitPlatScrl:
  ram[Platform_X_Scroll] = 0x0; // nullify platform force imposed on scroll
  return;
}

// in: none; out: a
void GetScreenPosition(void) {
  a = ram[ScreenLeft_X_Pos]; // get coordinate of screen's left boundary
  carry_flag = false;
  { uint16_t sum = a + 0xff + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add 255 pixels
  ram[ScreenRight_X_Pos] = a; // store as coordinate of screen's right boundary
  a = ram[ScreenLeft_PageLoc]; // get page number where left boundary is
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry from before
  ram[ScreenRight_PageLoc] = a; // store as page number where right boundary is
  return;
}

// -------------------------------------------------------------------------------------
void GameRoutines(void) {
  a = ram[GameEngineSubroutine]; // run routine based on number (a few of these routines are   
  switch (JumpEngine(0xb04e)) {
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
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void PlayerEntrance(void) {
  a = ram[AltEntranceControl]; // check for mode of alternate entry
  // Original branch: if found, branch to enter from pipe or with vine
  if (a != 0x2) {
    a = 0x0;
    y = ram[Player_Y_Position]; // if vertical position above a certain
    // point, nullify controller bits and continue
    // with player movement code, do not return
    if (y < 0x30) {
      AutoControlPlayer();
      return;
    }
    a = ram[PlayerEntranceCtrl]; // check player entry bits from header
    // Original branch: if set to 6 or 7, execute pipe intro code
    if (a != 0x6) {
      zero_flag = a == 0x7; // otherwise branch to normal entry
      if (!zero_flag) { goto PlayerRdy; }
    }
    // ChkBehPipe:
    a = ram[Player_SprAttrib]; zero_flag = a == 0; // check for sprite attributes
    // Original branch: branch if found
    if (zero_flag) {
      a = 0x1;
      AutoControlPlayer(); return; // force player to walk to the right
    }
    // IntroEntr:
    CALL(EnterSidePipe, 0xb08f); // execute sub to move player to the right
    ram[ChangeAreaTimer]--; zero_flag = ram[ChangeAreaTimer] == 0; // decrement timer for change of area
    // branch to exit if not yet expired
    if (!zero_flag) {
      return;
    }
    ram[DisableIntermediate]++; // set flag to skip world and lives display
    NextArea(); return; // jump to increment to next area and set modes
  }
  // EntrMode2:
  a = ram[JoypadOverride]; zero_flag = a == 0; // if controller override bits set here,
  // Original branch: branch to enter with vine
  if (zero_flag) {
    a = 0xff; // otherwise, set value here then execute sub
    CALL(MovePlayerYAxis, 0xb0a4); // to move player upwards (note $ff = -1)
    a = ram[Player_Y_Position]; // check to see if player is at a specific coordinate
    carry_flag = a >= 0x91; // if player risen to a certain point (this requires pipes
    if (!carry_flag) { goto PlayerRdy; } // to be at specific height to look/function right) branch
    return; // to the last part, otherwise leave
  }
  // VineEntr:
  a = ram[VineHeight];
  // check vine height
  // if vine not yet reached maximum height, branch to leave
  if (a != 0x60) {
    return;
  }
  a = ram[Player_Y_Position]; // get player's vertical coordinate
  carry_flag = a >= 0x99; // check player's vertical coordinate against preset value
  y = 0x0; // load default values to be written to 
  a = 0x1; // this value moves player to the right off the vine
  // Original branch: if vertical coordinate < preset value, use defaults
  if (carry_flag) {
    ram[Player_State] = 0x3; // otherwise set player state to climbing
    y++; // increment value in Y
    a = 0x8; // set block in block buffer to cover hole, then 
    ram[(Block_Buffer_1 + 0xb4)] = a; // use same value to force player to climb
  }
  // OffVine:
  ram[DisableCollisionDet] = y; // set collision detection disable flag
  CALL(AutoControlPlayer, 0xb0cc); // use contents of A to move player up or right, execute sub
  a = ram[Player_X_Position];
  // check player's horizontal position
  // if not far enough to the right, branch to leave
  if (a < 0x48) {
    return;
  }
  
PlayerRdy:
  ram[GameEngineSubroutine] = 0x8; // set routine to be executed by game engine next frame
  a = 0x1; // set to face player to the right
  ram[PlayerFacingDir] = a;
  a = a >> 1; // init A
  ram[AltEntranceControl] = a; // init mode of entry
  ram[DisableCollisionDet] = a; // init collision detection disable flag
  ram[JoypadOverride] = a; // nullify controller override bits
  // ExitEntr:
  return; // leave!
}

// -------------------------------------------------------------------------------------
// $07 - used to hold upper limit of high byte when player falls down hole
// in: a; out: none
void AutoControlPlayer(void) {
  ram[SavedJoypadBits] = a; // override controller bits with contents of A if executing here
  PlayerCtrlRoutine(); // fallthrough
  return;
}

void PlayerCtrlRoutine(void) {
  a = ram[GameEngineSubroutine]; // check task here
  // if certain value is set, branch to skip controller bit loading
  if (a != 0xb) {
    a = ram[AreaType]; zero_flag = a == 0; // are we in a water type area?
    // Original branch: if not, branch
    if (zero_flag) {
      y = ram[Player_Y_HighPos];
      y--; zero_flag = y == 0; // if not in vertical area between
      // Original branch: status bar and bottom, branch
      if (zero_flag) {
        a = ram[Player_Y_Position];
        carry_flag = a >= 0xd0; // if nearing the bottom of the screen or
        if (!carry_flag) { goto SaveJoyp; } // not in the vertical area between status bar or bottom,
      }
      // DisJoyp:
      ram[SavedJoypadBits] = 0x0; // disable controller bits
    }
    
SaveJoyp:
    a = ram[SavedJoypadBits]; // otherwise store A and B buttons in $0a
    a &= 0b11000000;
    ram[A_B_Buttons] = a;
    a = ram[SavedJoypadBits]; // store left and right buttons in $0c
    a &= 0b00000011;
    ram[Left_Right_Buttons] = a;
    a = ram[SavedJoypadBits]; // store up and down buttons in $0b
    a &= 0b00001100;
    ram[Up_Down_Buttons] = a;
    a &= 0b00000100; zero_flag = a == 0; // check for pressing down
    if (zero_flag) { goto SizeChk; } // if not, branch
    a = ram[Player_State]; zero_flag = a == 0; // check player's state
    if (!zero_flag) { goto SizeChk; } // if not on the ground, branch
    y = ram[Left_Right_Buttons]; zero_flag = y == 0; // check left and right
    if (zero_flag) { goto SizeChk; } // if neither pressed, branch
    a = 0x0;
    ram[Left_Right_Buttons] = a; // if pressing down while on the ground,
    ram[Up_Down_Buttons] = a; // nullify directional bits
  }
  
SizeChk:
  CALL(PlayerMovementSubs, 0xb12d); // run movement subroutines
  y = 0x1; // is player small?
  a = ram[PlayerSize]; zero_flag = a == 0;
  if (zero_flag) {
    y = 0x0; // check for if crouching
    a = ram[CrouchingFlag]; zero_flag = a == 0;
    if (zero_flag) { goto ChkMoveDir; } // if not, branch ahead
    y = 0x2; // if big and crouching, load y with 2
  }
  
ChkMoveDir:
  ram[Player_BoundBoxCtrl] = y; // set contents of Y as player's bounding box size control
  a = 0x1; // set moving direction to right by default
  y = ram[Player_X_Speed]; zero_flag = y == 0; neg_flag = (y & 0x80) != 0; // check player's horizontal speed
  // Original branch: if not moving at all horizontally, skip this part
  if (!zero_flag) {
    // Original branch: if moving to the right, use default moving direction
    if (neg_flag) {
      a = (uint8_t)(a << 1); // otherwise change to move to the left
    }
    // SetMoveDir:
    ram[Player_MovingDir] = a; // set moving direction
  }
  // PlayerSubs:
  CALL(ScrollHandler, 0xb14e); // move the screen if necessary
  CALL(GetPlayerOffscreenBits, 0xb151); // get player's offscreen bits
  CALL(RelativePlayerPosition, 0xb154); // get coordinates relative to the screen
  x = 0x0; // set offset for player object
  CALL(BoundingBoxCore, 0xb159); // get player's bounding box coordinates
  CALL(PlayerBGCollision, 0xb15c); // do collision detection and process
  a = ram[Player_Y_Position];
  // check to see if player is higher than 64th pixel
  // Original branch: if so, branch ahead
  if (a >= 0x40) {
    a = ram[GameEngineSubroutine];
    zero_flag = a == 0x5; // if running end-of-level routine, branch ahead
    if (zero_flag) { goto PlayerHole; }
    zero_flag = a == 0x7; // if running player entrance routine, branch ahead
    if (zero_flag) { goto PlayerHole; }
    carry_flag = a >= 0x4; // if running routines $00-$03, branch ahead
    if (!carry_flag) { goto PlayerHole; }
    a = ram[Player_SprAttrib];
    a &= 0b11011111; // otherwise nullify player's
    ram[Player_SprAttrib] = a; // background priority flag
  }
  
PlayerHole:
  a = ram[Player_Y_HighPos]; // check player's vertical high byte
  neg_flag = ((a - 0x2) & 0x80) != 0; // for below the screen
  // branch to leave if not that far down
  if (neg_flag) {
    return;
  }
  ram[ScrollLock] = 0x1; // set scroll lock
  ram[0x7] = 0x4; // set value here
  x = 0x0; // use X as flag, and clear for cloud level
  y = ram[GameTimerExpiredFlag]; zero_flag = y == 0; // check game timer expiration flag
  // Original branch: if set, branch
  if (zero_flag) {
    y = ram[CloudTypeOverride]; zero_flag = y == 0; // check for cloud type override
    if (!zero_flag) { goto ChkHoleX; } // skip to last part if found
  }
  // HoleDie:
  x++; // set flag in X for player death
  y = ram[GameEngineSubroutine];
  // check for some other routine running
  // Original branch: if so, branch ahead
  if (y != 0xb) {
    y = ram[DeathMusicLoaded]; zero_flag = y == 0; // check value here
    // Original branch: if already set, branch to next part
    if (zero_flag) {
      y++;
      ram[EventMusicQueue] = y; // otherwise play death music
      ram[DeathMusicLoaded] = y; // and set value here
    }
    // HoleBottom:
    ram[0x7] = 0x6; // change value here
  }
  
ChkHoleX:
  neg_flag = ((a - ram[0x7]) & 0x80) != 0; // compare vertical high byte with value set here
  // if less, branch to leave
  if (neg_flag) {
    return;
  }
  x--; neg_flag = (x & 0x80) != 0; // otherwise decrement flag in X
  // Original branch: if flag was clear, branch to set modes and other values
  if (!neg_flag) {
    y = ram[EventMusicBuffer]; zero_flag = y == 0; // check to see if music is still playing
    // branch to leave if so
    if (!zero_flag) {
      return;
    }
    ram[GameEngineSubroutine] = 0x6; // otherwise set to run lose life routine; on next frame
    // ExitCtrl:
    return; // leave
  }
  // CloudExit:
  ram[JoypadOverride] = 0x0; // clear controller override bits if any are set
  CALL(SetEntr, 0xb1c2); // do sub to set secondary mode
  ram[AltEntranceControl]++; // set mode of entry to 3
  return;
}

// -------------------------------------------------------------------------------------
void Vine_AutoClimb(void) {
  a = ram[Player_Y_HighPos]; zero_flag = a == 0; // check to see whether player reached position
  // Original branch: above the status bar yet and if so, set modes
  if (zero_flag) {
    a = ram[Player_Y_Position];
    if (a < 0xe4) {
      SetEntr();
      return;
    }
  }
  // AutoClimb:
  a = 0b00001000; // set controller bits override to up
  ram[JoypadOverride] = a;
  ram[Player_State] = 0x3; // set player state to climbing
  AutoControlPlayer(); return;
}

void SetEntr(void) {
  ram[AltEntranceControl] = 0x2; // set starting position to override
  ChgAreaMode(); return; // set modes
}

// -------------------------------------------------------------------------------------
void VerticalPipeEntry(void) {
  a = 0x1; // set 1 as movement amount
  CALL(MovePlayerYAxis, 0xb1e9); // do sub to move player downwards
  CALL(ScrollHandler, 0xb1ec); // do sub to scroll screen with saved force if necessary
  y = 0x0; // load default mode of entry
  a = ram[WarpZoneControl]; zero_flag = a == 0; // check warp zone control variable/flag
  // if set, branch to use mode 0
  if (!zero_flag) {
    ChgAreaPipe();
    return;
  }
  y++;
  a = ram[AreaType]; // check for castle level type
  // if not castle type level, use mode 1
  if (a != 0x3) {
    ChgAreaPipe();
    return;
  }
  y++;
  ChgAreaPipe(); return; // otherwise use mode 2
}

// in: a; out: none
void MovePlayerYAxis(void) {
  carry_flag = false;
  a = (uint8_t)(a + ram[Player_Y_Position] + carry_flag); // add contents of A to player position
  ram[Player_Y_Position] = a;
  return;
}

// -------------------------------------------------------------------------------------
void SideExitPipeEntry(void) {
  CALL(EnterSidePipe, 0xb208); // execute sub to move player to the right
  y = 0x2;
  ChgAreaPipe(); // fallthrough
  return;
}

// in: y; out: none
void ChgAreaPipe(void) {
  ram[ChangeAreaTimer]--; zero_flag = ram[ChangeAreaTimer] == 0; // decrement timer for change of area
  if (!zero_flag) {
    return;
  }
  ram[AltEntranceControl] = y; // when timer expires set mode of alternate entry
  ChgAreaMode(); // fallthrough
  return;
}

// in: none; out: a
void ChgAreaMode(void) {
  ram[DisableScreenFlag]++; // set flag to disable screen output
  a = 0x0;
  ram[OperMode_Task] = a; // set secondary mode of operation
  ram[Sprite0HitDetectFlag] = a; // disable sprite 0 check
  // ExitCAPipe:
  return; // leave
}

void EnterSidePipe(void) {
  ram[Player_X_Speed] = 0x8; // set player's horizontal speed
  y = 0x1; // set controller right button by default
  a = ram[Player_X_Position]; // mask out higher nybble of player's
  a &= 0b00001111; zero_flag = a == 0; // horizontal position
  if (zero_flag) {
    ram[Player_X_Speed] = a; // if lower nybble = 0, set as horizontal speed
    y = a; // and nullify controller bit override here
  }
  // RightPipe:
  a = y; // use contents of Y to
  CALL(AutoControlPlayer, 0xb231); // execute player control routine with ctrl bits nulled
  return;
}

// -------------------------------------------------------------------------------------
void PlayerChangeSize(void) {
  a = ram[TimerControl]; // check master timer control
  // for specific moment in time
  // Original branch: branch if before or after that point
  if (a == 0xf8) {
    InitChangeSize(); return; // otherwise run code to get growing/shrinking going
  }
  // EndChgSize:
  // check again for another specific moment
  // and branch to leave if before or after that point
  if (a != 0xc4) {
    return;
  }
  CALL(DonePlayerTask, 0xb243); // otherwise do sub to init timer control and set routine
  // ExitChgSize:
  return; // and then leave
}

// -------------------------------------------------------------------------------------
void PlayerInjuryBlink(void) {
  a = ram[TimerControl]; // check master timer control
  { uint8_t m = 0xf0; carry_flag = a >= m; zero_flag = a == m; } // for specific moment in time
  // Original branch: branch if before that point
  if (!carry_flag) {
    // check again for another specific point
    // branch if at that point, and not before or after
    if (a == 0xc8) {
      DonePlayerTask();
      return;
    }
    PlayerCtrlRoutine(); return; // otherwise run player control routine
  }
  // ExitBlink:
  // do unconditional branch to leave
  if (!zero_flag) {
    return;
  }
  InitChangeSize(); // fallthrough
  return;
}

void InitChangeSize(void) {
  y = ram[PlayerChangeSizeFlag]; zero_flag = y == 0; // if growing/shrinking flag already set
  // then branch to leave
  if (!zero_flag) {
    return;
  }
  ram[PlayerAnimCtrl] = y; // otherwise initialize player's animation frame control
  ram[PlayerChangeSizeFlag]++; // set growing/shrinking flag
  a = ram[PlayerSize];
  a ^= 0x1; // invert player's size
  ram[PlayerSize] = a;
  // ExitBoth:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00 - used in CyclePlayerPalette to store current palette to cycle
void PlayerDeath(void) {
  a = ram[TimerControl]; // check master timer control
  // for specific moment in time
  // branch to leave if before that point
  if (a >= 0xf0) {
    return;
  }
  PlayerCtrlRoutine(); return; // otherwise run player control routine
}

void DonePlayerTask(void) {
  ram[TimerControl] = 0x0; // initialize master timer control to continue timers
  ram[GameEngineSubroutine] = 0x8; // set player control routine to run next frame
  return; // leave
}

void PlayerFireFlower(void) {
  a = ram[TimerControl]; // check master timer control
  // for specific moment in time
  // Original branch: branch if at moment, not before or after
  if (a != 0xc0) {
    a = ram[FrameCounter]; // get frame counter
    a = a >> 1;
    a = a >> 1; // divide by four to change every four frames
    CyclePlayerPalette(); // fallthrough
    return;
  }
  // ResetPalFireFlower:
  CALL(DonePlayerTask, 0xb299); // do sub to init timer control and run player control routine
  ResetPalStar(); // fallthrough
  return;
}

// in: a; out: none
void CyclePlayerPalette(void) {
  a &= 0x3; // mask out all but d1-d0 (previously d3-d2)
  ram[0x0] = a; // store result here to use as palette bits
  a = ram[Player_SprAttrib]; // get player attributes
  a &= 0b11111100; // save any other bits but palette bits
  a |= ram[0x0]; // add palette bits
  ram[Player_SprAttrib] = a; // store as new player attributes
  return; // and leave
}

void ResetPalStar(void) {
  a = ram[Player_SprAttrib]; // get player attributes
  a &= 0b11111100; // mask out palette bits to force palette 0
  ram[Player_SprAttrib] = a; // store as new player attributes
  return; // and leave
}

// -------------------------------------------------------------------------------------
void FlagpoleSlide(void) {
  a = ram[(Enemy_ID + 5)]; // check special use enemy slot
  // for flagpole flag object
  // Original branch: if not found, branch to something residual
  if (a == FlagpoleFlagObject) {
    ram[Square1SoundQueue] = ram[FlagpoleSoundQueue]; // load flagpole sound; into square 1's sfx queue
    a = 0x0;
    ram[FlagpoleSoundQueue] = a; // init flagpole sound queue
    y = ram[Player_Y_Position];
    // check to see if player has slid down
    // Original branch: far enough, and if so, branch with no controller bits set
    if (y < 0x9e) {
      a = 0x4; // otherwise force player to climb down (to slide)
    }
    // SlidePlayer:
    AutoControlPlayer(); return; // jump to player control routine
  }
  // NoFPObj:
  ram[GameEngineSubroutine]++; // increment to next routine (this may
  return; // be residual code)
}

// -------------------------------------------------------------------------------------
void PlayerEndLevel(void) {
  a = 0x1; // force player to walk to the right
  CALL(AutoControlPlayer, 0xb2ce);
  a = ram[Player_Y_Position]; // check player's vertical position
  // Original branch: if player is not yet off the flagpole, skip this part
  if (a >= 0xae) {
    a = ram[ScrollLock]; zero_flag = a == 0; // if scroll lock not set, branch ahead to next part
    if (zero_flag) { goto ChkStop; } // because we only need to do this part once
    ram[EventMusicQueue] = EndOfLevelMusic; // load win level music in event music queue
    ram[ScrollLock] = 0x0; // turn off scroll lock to skip this part later
  }
  
ChkStop:
  a = ram[Player_CollisionBits]; // get player collision bits
  carry_flag = (a & 1) != 0; a = a >> 1; // check for d0 set
  // Original branch: if d0 set, skip to next part
  if (!carry_flag) {
    a = ram[StarFlagTaskControl]; zero_flag = a == 0; // if star flag task control already set,
    // Original branch: go ahead with the rest of the code
    if (zero_flag) {
      ram[StarFlagTaskControl]++; // otherwise set task control now (this gets ball rolling!)
    }
    // InCastle:
    ram[Player_SprAttrib] = 0b00100000; // set player's background priority bit to; give illusion of being inside the castle
  }
  // RdyNextA:
  a = ram[StarFlagTaskControl];
  // if star flag task control not yet set
  // beyond last valid task number, branch to leave
  if (a != 0x5) {
    return;
  }
  ram[LevelNumber]++; // increment level number used for game logic
  a = ram[LevelNumber];
  // check to see if we have yet reached level -4
  // and skip this last part here if not
  if (a != 0x3) {
    NextArea();
    return;
  }
  y = ram[WorldNumber]; // get world number as offset
  a = ram[CoinTallyFor1Ups]; // check third area coin tally for bonus 1-ups
  carry_flag = a >= ROM(Hidden1UpCoinAmts + y); // against minimum value, if player has not collected
  // at least this number of coins, leave flag clear
  if (!carry_flag) {
    NextArea();
    return;
  }
  ram[Hidden1UpFlag]++; // otherwise set hidden 1-up box control flag
  NextArea(); // fallthrough
  return;
}

void NextArea(void) {
  ram[AreaNumber]++; // increment area number used for address loader
  CALL(LoadAreaPointer, 0xb31a); // get new level pointer
  ram[FetchNewGameTimerFlag]++; // set flag to load new game timer
  CALL(ChgAreaMode, 0xb320); // do sub to set secondary mode, disable screen and sprite 0
  ram[HalfwayPage] = a; // reset halfway page to 0 (beginning)
  ram[EventMusicQueue] = Silence; // silence music and leave
  // ExitNA:
  return;
}

// -------------------------------------------------------------------------------------
void PlayerMovementSubs(void) {
  a = 0x0; // set A to init crouch flag by default
  y = ram[PlayerSize]; zero_flag = y == 0; // is player small?
  // Original branch: if so, branch
  if (zero_flag) {
    a = ram[Player_State]; zero_flag = a == 0; // check state of player
    if (!zero_flag) { goto ProcMove; } // if not on the ground, branch
    a = ram[Up_Down_Buttons]; // load controller bits for up and down
    a &= 0b00000100; // single out bit for down button
  }
  // SetCrouch:
  ram[CrouchingFlag] = a; // store value in crouch flag
  
ProcMove:
  CALL(PlayerPhysicsSub, 0xb33d); // run sub related to jumping and swimming
  a = ram[PlayerChangeSizeFlag]; zero_flag = a == 0; // if growing/shrinking flag set,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[Player_State];
  // get player state
  // Original branch: if climbing, branch ahead, leave timer unset
  if (a != 0x3) {
    ram[ClimbSideTimer] = 0x18; // otherwise reset timer now
  }
  // MoveSubs:
  switch (JumpEngine(0xb350)) {
    case 0xb35a: OnGroundStateSub(); return;
    case 0xb376: JumpSwimSub(); return;
    case 0xb36d: FallingSub(); return;
    case 0xb3cf: ClimbingSub(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// -------------------------------------------------------------------------------------
// $00 - used by ClimbingSub to store high vertical adder
void OnGroundStateSub(void) {
  CALL(GetPlayerAnimSpeed, 0xb35c); // do a sub to set animation frame timing
  a = ram[Left_Right_Buttons]; zero_flag = a == 0;
  // Original branch: if left/right controller bits not set, skip instruction
  if (!zero_flag) {
    ram[PlayerFacingDir] = a; // otherwise set new facing direction
  }
  // GndMove:
  CALL(ImposeFriction, 0xb365); // do a sub to impose friction on player's walk/run
  CALL(MovePlayerHorizontally, 0xb368); // do another sub to move player horizontally
  ram[Player_X_Scroll] = a; // set returned value as player's movement speed for scroll
  return;
}

// --------------------------------
void FallingSub(void) {
  ram[VerticalForce] = ram[VerticalForceDown]; // dump vertical movement force for falling into main one
  LRAir(); return; // movement force, then skip ahead to process left/right movement
}

// --------------------------------
void JumpSwimSub(void) {
  y = ram[Player_Y_Speed]; neg_flag = (y & 0x80) != 0; // if player's vertical speed zero
  // Original branch: or moving downwards, branch to falling
  if (neg_flag) {
    a = ram[A_B_Buttons];
    a &= A_Button; // check to see if A button is being pressed
    a &= ram[PreviousA_B_Buttons]; zero_flag = a == 0; // and was pressed in previous frame
    if (!zero_flag) { goto ProcSwim; } // if so, branch elsewhere
    a = ram[JumpOrigin_Y_Position]; // get vertical position player jumped from
    carry_flag = true;
    a = (uint8_t)(a - ram[Player_Y_Position] - !carry_flag); // subtract current from original vertical coordinate
    carry_flag = a >= ram[DiffToHaltJump]; // compare to value set here to see if player is in mid-jump
    if (!carry_flag) { goto ProcSwim; } // or just starting to jump, if just starting, skip ahead
  }
  // DumpFall:
  ram[VerticalForce] = ram[VerticalForceDown]; // otherwise dump falling into main fractional
  
ProcSwim:
  a = ram[SwimmingFlag]; zero_flag = a == 0; // if swimming flag not set,
  // branch ahead to last part
  if (zero_flag) {
    LRAir();
    return;
  }
  CALL(GetPlayerAnimSpeed, 0xb39a); // do a sub to get animation frame timing
  a = ram[Player_Y_Position];
  // check vertical position against preset value
  // Original branch: if not yet reached a certain position, branch ahead
  if (a < 0x14) {
    ram[VerticalForce] = 0x18; // otherwise set fractional
  }
  // LRWater:
  a = ram[Left_Right_Buttons]; zero_flag = a == 0; // check left/right controller bits (check for swimming)
  // if not pressing any, skip
  if (zero_flag) {
    LRAir();
    return;
  }
  ram[PlayerFacingDir] = a; // otherwise set facing direction accordingly
  LRAir(); // fallthrough
  return;
}

void LRAir(void) {
  a = ram[Left_Right_Buttons]; zero_flag = a == 0; // check left/right controller bits (check for jumping/falling)
  // Original branch: if not pressing any, skip
  if (!zero_flag) {
    CALL(ImposeFriction, 0xb3b2); // otherwise process horizontal movement
  }
  // JSMove:
  CALL(MovePlayerHorizontally, 0xb3b5); // do a sub to move player horizontally
  ram[Player_X_Scroll] = a; // set player's speed here, to be used for scroll later
  a = ram[GameEngineSubroutine];
  // check for specific routine selected
  // Original branch: branch if not set to run
  if (a == 0xb) {
    ram[VerticalForce] = 0x28; // otherwise set fractional
  }
  // ExitMov1:
  // jump to move player vertically, then leave
  // -------------------------------------------------------------------------------------
  // $00 - used for downward force
  // $01 - used for upward force
  // $02 - used for maximum vertical speed
  // MovePlayerVertically:
  x = 0x0; // set X for player offset
  a = ram[TimerControl]; zero_flag = a == 0;
  // Original branch: if master timer control set, branch ahead
  if (zero_flag) {
    a = ram[JumpspringAnimCtrl]; zero_flag = a == 0; // otherwise check to see if jumpspring is animating
    // branch to leave if so
    if (!zero_flag) {
      return;
    }
  }
  // NoJSChk:
  ram[0x0] = ram[VerticalForce]; // dump vertical force 
  a = 0x4; // set maximum vertical speed here
  ImposeGravitySprObj(); return; // then jump to move player vertically
}

// --------------------------------
void ClimbingSub(void) {
  a = ram[Player_YMF_Dummy];
  carry_flag = false; // add movement force to dummy variable
  { uint16_t sum = a + ram[Player_Y_MoveForce] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // save with carry
  ram[Player_YMF_Dummy] = a;
  y = 0x0; // set default adder here
  a = ram[Player_Y_Speed]; neg_flag = (a & 0x80) != 0; // get player's vertical speed
  // Original branch: if not moving upwards, branch
  if (neg_flag) {
    y--; // otherwise set adder to $ff
  }
  // MoveOnVine:
  ram[0x0] = y; // store adder here
  { uint16_t sum = a + ram[Player_Y_Position] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add carry to player's vertical position
  ram[Player_Y_Position] = a; // and store to move player up or down
  a = ram[Player_Y_HighPos];
  a = (uint8_t)(a + ram[0x0] + carry_flag); // add carry to player's page location
  ram[Player_Y_HighPos] = a; // and store
  a = ram[Left_Right_Buttons]; // compare left/right controller bits
  a &= ram[Player_CollisionBits]; zero_flag = a == 0; // to collision flag
  // Original branch: if not set, skip to end
  if (!zero_flag) {
    y = ram[ClimbSideTimer]; zero_flag = y == 0; // otherwise check timer 
    // if timer not expired, branch to leave
    if (!zero_flag) {
      return;
    }
    ram[ClimbSideTimer] = 0x18; // otherwise set timer now
    x = 0x0; // set default offset here
    y = ram[PlayerFacingDir]; // get facing direction
    carry_flag = (a & 1) != 0; a = a >> 1; // move right button controller bit to carry
    // Original branch: if controller right pressed, branch ahead
    if (!carry_flag) {
      x++;
      x++; // otherwise increment offset by 2 bytes
    }
    // ClimbFD:
    y--; zero_flag = y == 0; // check to see if facing right
    // Original branch: if so, branch, do not increment
    if (!zero_flag) {
      x++; // otherwise increment by 1 byte
    }
    // CSetFDir:
    a = ram[Player_X_Position];
    carry_flag = false; // add or subtract from player's horizontal position
    { uint16_t sum = a + ROM(ClimbAdderLow + x) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // using value here as adder and X as offset
    ram[Player_X_Position] = a;
    a = ram[Player_PageLoc]; // add or subtract carry or borrow using value here
    a = (uint8_t)(a + ROM(ClimbAdderHigh + x) + carry_flag); // from the player's page location
    ram[Player_PageLoc] = a;
    a = ram[Left_Right_Buttons]; // get left/right controller bits again
    a ^= 0b00000011; // invert them and store them while player
    ram[PlayerFacingDir] = a; // is on vine to face player in opposite direction
    // ExitCSub:
    return; // then leave
  }
  // InitCSTimer:
  ram[ClimbSideTimer] = a; // initialize timer here
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used to store offset to friction data
void PlayerPhysicsSub(void) {
  a = ram[Player_State]; // check player state
  // Original branch: if not climbing, branch
  if (a == 0x3) {
    y = 0x0;
    a = ram[Up_Down_Buttons]; // get controller bits for up/down
    a &= ram[Player_CollisionBits]; zero_flag = a == 0; // check against player's collision detection bits
    // Original branch: if not pressing up or down, branch
    if (!zero_flag) {
      y++;
      a &= 0b00001000; zero_flag = a == 0; // check for pressing up
      if (!zero_flag) { goto ProcClimb; }
      y++;
    }
    
ProcClimb:
    ram[Player_Y_MoveForce] = ROM(Climb_Y_MForceData + y); // load value here; store as vertical movement force
    a = 0x8; // load default animation timing
    x = ROM(Climb_Y_SpeedData + y); neg_flag = (x & 0x80) != 0; // load some other value here
    ram[Player_Y_Speed] = x; // store as vertical speed
    // Original branch: if climbing down, use default animation timing value
    if (!neg_flag) {
      a = a >> 1; // otherwise divide timer setting by 2
    }
    // SetCAnim:
    ram[PlayerAnimTimerSet] = a; // store animation timer setting and leave
    return;
  }
  // CheckForJumping:
  a = ram[JumpspringAnimCtrl]; zero_flag = a == 0; // if jumpspring animating, 
  // Original branch: skip ahead to something else
  if (zero_flag) {
    a = ram[A_B_Buttons]; // check for A button press
    a &= A_Button; zero_flag = a == 0;
    if (zero_flag) { goto NoJump; } // if not, branch to something else
    a &= ram[PreviousA_B_Buttons]; zero_flag = a == 0; // if button not pressed in previous frame, branch
    if (zero_flag) { goto ProcJumping; }
  }
  
NoJump:
  goto X_Physics; // otherwise, jump to something else
  
ProcJumping:
  a = ram[Player_State]; zero_flag = a == 0; // check player state
  // Original branch: if on the ground, branch
  if (!zero_flag) {
    a = ram[SwimmingFlag]; zero_flag = a == 0; // if swimming flag not set, jump to do something else
    if (zero_flag) { goto NoJump; } // to prevent midair jumping, otherwise continue
    a = ram[JumpSwimTimer]; zero_flag = a == 0; // if jump/swim timer nonzero, branch
    if (!zero_flag) { goto InitJS; }
    a = ram[Player_Y_Speed]; neg_flag = (a & 0x80) != 0; // check player's vertical speed
    if (!neg_flag) { goto InitJS; } // if player's vertical speed motionless or down, branch
  } else {
    
InitJS:
    ram[JumpSwimTimer] = 0x20; // set jump/swim timer
    y = 0x0; // initialize vertical force and dummy variable
    ram[Player_YMF_Dummy] = y;
    ram[Player_Y_MoveForce] = y;
    ram[JumpOrigin_Y_HighPos] = ram[Player_Y_HighPos]; // get vertical high and low bytes of jump origin; and store them next to each other here
    ram[JumpOrigin_Y_Position] = ram[Player_Y_Position];
    ram[Player_State] = 0x1; // set player state to jumping/swimming
    a = ram[Player_XSpeedAbsolute]; // check value related to walking/running speed
    // Original branch: branch if below certain values, increment Y
    if (a >= 0x9) {
      y++; // for each amount equal or exceeded
      carry_flag = a >= 0x10;
      if (!carry_flag) { goto ChkWtr; }
      y++;
      carry_flag = a >= 0x19;
      if (!carry_flag) { goto ChkWtr; }
      y++;
      carry_flag = a >= 0x1c;
      if (!carry_flag) { goto ChkWtr; } // note that for jumping, range is 0-4 for Y
      y++;
    }
    
ChkWtr:
    ram[DiffToHaltJump] = 0x1; // set value here (apparently always set to 1)
    a = ram[SwimmingFlag]; zero_flag = a == 0; // if swimming flag disabled, branch
    if (!zero_flag) {
      y = 0x5; // otherwise set Y to 5, range is 5-6
      a = ram[Whirlpool_Flag]; zero_flag = a == 0; // if whirlpool flag not set, branch
      if (zero_flag) { goto GetYPhy; }
      y++; // otherwise increment to 6
    }
    
GetYPhy:
    ram[VerticalForce] = ROM(JumpMForceData + y); // store appropriate jump/swim; data here
    ram[VerticalForceDown] = ROM(FallMForceData + y);
    ram[Player_Y_MoveForce] = ROM(InitMForceData + y);
    ram[Player_Y_Speed] = ROM(PlayerYSpdData + y);
    a = ram[SwimmingFlag]; zero_flag = a == 0; // if swimming flag disabled, branch
    if (!zero_flag) {
      ram[Square1SoundQueue] = Sfx_EnemyStomp; // load swim/goomba stomp sound into; square 1's sfx queue
      a = ram[Player_Y_Position];
      carry_flag = a >= 0x14; // check vertical low byte of player position
      if (carry_flag) { goto X_Physics; } // if below a certain point, branch
      ram[Player_Y_Speed] = 0x0; // otherwise reset player's vertical speed; and jump to something else to keep player
      goto X_Physics; // from swimming above water level
    }
    // PJumpSnd:
    a = Sfx_BigJump; // load big mario's jump sound by default
    y = ram[PlayerSize]; zero_flag = y == 0; // is mario big?
    if (!zero_flag) {
      a = Sfx_SmallJump; // if not, load small mario's jump sound
    }
    // SJumpSnd:
    ram[Square1SoundQueue] = a; // store appropriate jump sound in square 1 sfx queue
  }
  
X_Physics:
  y = 0x0;
  ram[0x0] = y; // init value here
  a = ram[Player_State]; zero_flag = a == 0; // if mario is on the ground, branch
  if (!zero_flag) {
    a = ram[Player_XSpeedAbsolute]; // check something that seems to be related
    carry_flag = a >= 0x19; // to mario's speed
    if (carry_flag) { goto GetXPhy; } // if =>$19 branch here
  } else {
    // ProcPRun:
    y++; // if mario on the ground, increment Y
    a = ram[AreaType]; zero_flag = a == 0; // check area type
    if (zero_flag) { goto ChkRFast; } // if water type, branch
    y--; // decrement Y by default for non-water type area
    a = ram[Left_Right_Buttons]; // get left/right controller bits
    zero_flag = a == ram[Player_MovingDir]; // check against moving direction
    if (!zero_flag) { goto ChkRFast; } // if controller bits <> moving direction, skip this part
    a = ram[A_B_Buttons]; // check for b button pressed
    a &= B_Button; zero_flag = a == 0;
    if (!zero_flag) { goto SetRTmr; } // if pressed, skip ahead to set timer
    a = ram[RunningTimer]; zero_flag = a == 0; // check for running timer set
    if (!zero_flag) { goto GetXPhy; } // if set, branch
  }
  
ChkRFast:
  y++; // if running timer not set or level type is water, 
  ram[0x0]++; // increment Y again and temp variable in memory
  a = ram[RunningSpeed]; zero_flag = a == 0;
  // Original branch: if running speed set here, branch
  if (zero_flag) {
    a = ram[Player_XSpeedAbsolute];
    carry_flag = a >= 0x21; // otherwise check player's walking/running speed
    if (!carry_flag) { goto GetXPhy; } // if less than a certain amount, branch ahead
  }
  // FastXSp:
  ram[0x0]++; // if running speed set or speed => $21 increment $00
  goto GetXPhy; // and jump ahead
  
SetRTmr:
  ram[RunningTimer] = 0xa; // if b button pressed, set running timer
  
GetXPhy:
  ram[MaximumLeftSpeed] = ROM(MaxLeftXSpdData + y); // get maximum speed to the left
  a = ram[GameEngineSubroutine]; // check for specific routine running
  // (player entrance)
  // Original branch: if not running, skip and use old value of Y
  if (a == 0x7) {
    y = 0x3; // otherwise set Y to 3
  }
  // GetXPhy2:
  ram[MaximumRightSpeed] = ROM(MaxRightXSpdData + y); // get maximum speed to the right
  y = ram[0x0]; // get other value in memory
  ram[FrictionAdderLow] = ROM(FrictionData + y); // get value using value in memory as offset
  ram[FrictionAdderHigh] = 0x0; // init something here
  a = ram[PlayerFacingDir];
  zero_flag = a == ram[Player_MovingDir]; // check facing direction against moving direction
  // if the same, branch to leave
  if (zero_flag) {
    return;
  }
  carry_flag = (ram[FrictionAdderLow] & 0x80) != 0; ram[FrictionAdderLow] = (uint8_t)(ram[FrictionAdderLow] << 1); // otherwise shift d7 of friction adder low into carry
  ram[FrictionAdderHigh] = (uint8_t)(ram[FrictionAdderHigh] << 1 | carry_flag); // then rotate carry onto d0 of friction adder high
  // ExitPhy:
  return; // and then leave
}

// -------------------------------------------------------------------------------------
void GetPlayerAnimSpeed(void) {
  y = 0x0; // initialize offset in Y
  a = ram[Player_XSpeedAbsolute]; // check player's walking/running speed
  // against preset amount
  // Original branch: if greater than a certain amount, branch ahead
  if (a < 0x1c) {
    y++; // otherwise increment Y
    // compare against lower amount
    // Original branch: if greater than this but not greater than first, skip increment
    if (a < 0xe) {
      y++; // otherwise increment Y again
    }
    // ChkSkid:
    a = ram[SavedJoypadBits]; // get controller bits
    a &= 0b01111111; zero_flag = a == 0; // mask out A button
    if (zero_flag) { goto SetAnimSpd; } // if no other buttons pressed, branch ahead of all this
    a &= 0x3; // mask out all others except left and right
    zero_flag = a == ram[Player_MovingDir]; // check against moving direction
    if (!zero_flag) { goto ProcSkid; } // if left/right controller bits <> moving direction, branch
    a = 0x0; // otherwise set zero value here
  }
  // SetRunSpd:
  ram[RunningSpeed] = a; // store zero or running speed here
  goto SetAnimSpd;
  
ProcSkid:
  a = ram[Player_XSpeedAbsolute]; // check player's walking/running speed
  // against one last amount
  // Original branch: if greater than this amount, branch
  if (a < 0xb) {
    ram[Player_MovingDir] = ram[PlayerFacingDir]; // otherwise use facing direction to set moving direction
    a = 0x0;
    ram[Player_X_Speed] = a; // nullify player's horizontal speed
    ram[Player_X_MoveForce] = a; // and dummy variable for player
  }
  
SetAnimSpd:
  ram[PlayerAnimTimerSet] = ROM(PlayerAnimTmrData + y); // get animation timer setting using Y as offset
  return;
}

// -------------------------------------------------------------------------------------
// in: a; out: none
void ImposeFriction(void) {
  a &= ram[Player_CollisionBits]; // perform AND between left/right controller bits and collision flag
  // then compare to zero (this instruction is redundant)
  // Original branch: if any bits set, branch to next part
  if (a == 0x0) {
    a = ram[Player_X_Speed]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    if (zero_flag) { goto SetAbsSpd; } // if player has no horizontal speed, branch ahead to last part
    if (!neg_flag) { goto RghtFrict; } // if player moving to the right, branch to slow
  } else {
    // JoypFrict:
    carry_flag = (a & 1) != 0; a = a >> 1; // put right controller bit into carry
    if (!carry_flag) { goto RghtFrict; } // if left button pressed, carry = 0, thus branch
  }
  // LeftFrict:
  a = ram[Player_X_MoveForce]; // load value set here
  carry_flag = false;
  { uint16_t sum = a + ram[FrictionAdderLow] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add to it another value set here
  ram[Player_X_MoveForce] = a; // store here
  a = ram[Player_X_Speed];
  a = (uint8_t)(a + ram[FrictionAdderHigh] + carry_flag); // add value plus carry to horizontal speed
  ram[Player_X_Speed] = a; // set as new horizontal speed
  neg_flag = ((a - ram[MaximumRightSpeed]) & 0x80) != 0; // compare against maximum value for right movement
  if (neg_flag) { goto XSpdSign; } // if horizontal speed greater negatively, branch
  a = ram[MaximumRightSpeed]; // otherwise set preset value as horizontal speed
  ram[Player_X_Speed] = a; // thus slowing the player's left movement down
  goto SetAbsSpd; // skip to the end
  
RghtFrict:
  a = ram[Player_X_MoveForce]; // load value set here
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[FrictionAdderLow] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract from it another value set here
  ram[Player_X_MoveForce] = a; // store here
  a = ram[Player_X_Speed];
  a = (uint8_t)(a - ram[FrictionAdderHigh] - !carry_flag); // subtract value plus borrow from horizontal speed
  ram[Player_X_Speed] = a; // set as new horizontal speed
  neg_flag = ((a - ram[MaximumLeftSpeed]) & 0x80) != 0; // compare against maximum value for left movement
  // Original branch: if horizontal speed greater positively, branch
  if (neg_flag) {
    a = ram[MaximumLeftSpeed]; // otherwise set preset value as horizontal speed
    ram[Player_X_Speed] = a; // thus slowing the player's right movement down
  }
  
XSpdSign:
  neg_flag = ((a - 0x0) & 0x80) != 0; // if player not moving or moving to the right,
  // Original branch: branch and leave horizontal speed value unmodified
  if (neg_flag) {
    a ^= 0xff;
    carry_flag = false; // otherwise get two's compliment to get absolute
    a = (uint8_t)(a + 0x1 + carry_flag); // unsigned walking/running speed
  }
  
SetAbsSpd:
  ram[Player_XSpeedAbsolute] = a; // store walking/running speed here and leave
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used to store downward movement force in FireballObjCore
// $02 - used to store maximum vertical speed in FireballObjCore
// $07 - used to store pseudorandom bit in BubbleCheck
void ProcFireball_Bubble(void) {
  a = ram[PlayerStatus]; // check player's status
  // Original branch: if not fiery, branch
  if (a >= 0x2) {
    a = ram[A_B_Buttons];
    a &= B_Button; zero_flag = a == 0; // check for b button pressed
    // Original branch: branch if not pressed
    if (!zero_flag) {
      a &= ram[PreviousA_B_Buttons]; zero_flag = a == 0;
      if (!zero_flag) { goto ProcFireballs; } // if button pressed in previous frame, branch
      a = ram[FireballCounter]; // load fireball counter
      a &= 0b00000001; // get LSB and use as offset for buffer
      x = a;
      a = ram[(uint8_t)(Fireball_State + x)]; zero_flag = a == 0; // load fireball state
      if (!zero_flag) { goto ProcFireballs; } // if not inactive, branch
      y = ram[Player_Y_HighPos]; // if player too high or too low, branch
      y--; zero_flag = y == 0;
      if (!zero_flag) { goto ProcFireballs; }
      a = ram[CrouchingFlag]; zero_flag = a == 0; // if player crouching, branch
      if (!zero_flag) { goto ProcFireballs; }
      a = ram[Player_State]; // if player's state = climbing, branch
      zero_flag = a == 0x3;
      if (zero_flag) { goto ProcFireballs; }
      ram[Square1SoundQueue] = Sfx_Fireball; // play fireball sound effect
      ram[(uint8_t)(Fireball_State + x)] = 0x2; // load state
      y = ram[PlayerAnimTimerSet]; // copy animation frame timer setting
      ram[FireballThrowingTimer] = y; // into fireball throwing timer
      y--;
      ram[PlayerAnimTimer] = y; // decrement and store in player's animation timer
      ram[FireballCounter]++; // increment fireball counter
    }
    
ProcFireballs:
    x = 0x0;
    CALL(FireballObjCore, 0xb668); // process first fireball object
    x = 0x1;
    CALL(FireballObjCore, 0xb66d); // process second fireball object, then do air bubbles
  }
  // ProcAirBubbles:
  a = ram[AreaType]; zero_flag = a == 0; // if not water type level, skip the rest of this
  if (!zero_flag) {
    return;
  }
  x = 0x2; // otherwise load counter and use as offset
  do {
    // BublLoop:
    ram[ObjectOffset] = x; // store offset
    CALL(BubbleCheck, 0xb679); // check timers and coordinates, create air bubble
    CALL(RelativeBubblePosition, 0xb67c); // get relative coordinates
    CALL(GetBubbleOffscreenBits, 0xb67f); // get offscreen information
    CALL(DrawBubble, 0xb682); // draw the air bubble
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag); // do this until all three are handled
  // BublExit:
  return; // then leave
}

// in: x; out: none
void FireballObjCore(void) {
  ram[ObjectOffset] = x; // store offset as current object
  a = ram[(uint8_t)(Fireball_State + x)]; // check for d7 = 1
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  // Original branch: if so, branch to get relative coordinates and draw explosion
  if (!carry_flag) {
    y = ram[(uint8_t)(Fireball_State + x)]; zero_flag = y == 0; // if fireball inactive, branch to leave
    if (zero_flag) {
      return;
    }
    y--; zero_flag = y == 0; // if fireball state set to 1, skip this part and just run it
    if (!zero_flag) {
      a = ram[Player_X_Position]; // get player's horizontal position
      { uint16_t sum = a + 0x4 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add four pixels and store as fireball's horizontal position
      ram[(uint8_t)(Fireball_X_Position + x)] = a;
      a = ram[Player_PageLoc]; // get player's page location
      a = (uint8_t)(a + 0x0 + carry_flag); // add carry and store as fireball's page location
      ram[(uint8_t)(Fireball_PageLoc + x)] = a;
      ram[(uint8_t)(Fireball_Y_Position + x)] = ram[Player_Y_Position]; // get player's vertical position and store
      ram[(uint8_t)(Fireball_Y_HighPos + x)] = 0x1; // set high byte of vertical position
      y = ram[PlayerFacingDir]; // get player's facing direction
      y--; // decrement to use as offset here
      ram[(uint8_t)(Fireball_X_Speed + x)] = ROM(FireballXSpdData + y); // set horizontal speed of fireball accordingly
      ram[(uint8_t)(Fireball_Y_Speed + x)] = 0x4; // set vertical speed of fireball
      ram[Fireball_BoundBoxCtrl + x] = 0x7; // set bounding box size control for fireball
      ram[(uint8_t)(Fireball_State + x)]--; // decrement state to 1 to skip this part from now on
    }
    // RunFB:
    a = x; // add 7 to offset to use
    carry_flag = false; // as fireball offset for next routines
    a = (uint8_t)(a + 0x7 + carry_flag);
    x = a;
    ram[0x0] = 0x50; // set downward movement force here
    ram[0x2] = 0x3; // set maximum speed here
    a = 0x0;
    CALL(ImposeGravity, 0xb6cf); // do sub here to impose gravity on fireball and move vertically
    CALL(MoveObjectHorizontally, 0xb6d2); // do another sub to move it horizontally
    x = ram[ObjectOffset]; // return fireball offset to X
    CALL(RelativeFireballPosition, 0xb6d7); // get relative coordinates
    CALL(GetFireballOffscreenBits, 0xb6da); // get offscreen information
    CALL(GetFireballBoundBox, 0xb6dd); // get bounding box coordinates
    CALL(FireballBGCollision, 0xb6e0); // do fireball to background collision detection
    a = ram[FBall_OffscreenBits]; // get fireball offscreen bits
    a &= 0b11001100; zero_flag = a == 0; // mask out certain bits
    // Original branch: if any bits still set, branch to kill fireball
    if (zero_flag) {
      CALL(FireballEnemyCollision, 0xb6ea); // do fireball to enemy collision detection and deal with collisions
      goto DrawFireball; // draw fireball appropriately and leave
    }
    // EraseFB:
    ram[(uint8_t)(Fireball_State + x)] = 0x0; // erase fireball state
    // NoFBall:
    return; // leave
  }
  // FireballExplosion:
  CALL(RelativeFireballPosition, 0xb6f5);
  goto DrawExplosion_Fireball;
  // -------------------------------------------------------------------------------------
  
DrawFireball:
  y = ram[FBall_SprDataOffset + x]; // get fireball's sprite data offset
  ram[Sprite_Y_Position + y] = ram[Fireball_Rel_YPos]; // get relative vertical coordinate; store as sprite Y coordinate
  ram[Sprite_X_Position + y] = ram[Fireball_Rel_XPos]; // get relative horizontal coordinate; store as sprite X coordinate, then do shared code
  DrawFirebar(); // fallthrough
  return;
  // -------------------------------------------------------------------------------------
  
DrawExplosion_Fireball:
  y = ram[Alt_SprDataOffset + x]; // get OAM data offset of alternate sort for fireball's explosion
  a = ram[(uint8_t)(Fireball_State + x)]; // load fireball state
  ram[(uint8_t)(Fireball_State + x)]++; // increment state for next frame
  a = a >> 1; // divide by 2
  a &= 0b00000111; // mask out all but d3-d1
  carry_flag = a >= 0x3; // check to see if time to kill fireball
  // Original branch: branch if so, otherwise continue to draw explosion
  if (!carry_flag) {
    DrawExplosion_Fireworks(); // fallthrough
    return;
  }
  // KillFireBall:
  ram[(uint8_t)(Fireball_State + x)] = 0x0; // clear fireball state to kill it
  return;
}

// in: x; out: none
void BubbleCheck(void) {
  a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff]; // get part of LSFR
  a &= 0x1;
  ram[0x7] = a; // store pseudorandom bit here
  a = ram[(uint8_t)(Bubble_Y_Position + x)]; // get vertical coordinate for air bubble
  // if offscreen coordinate not set,
  // branch to move air bubble
  if (a != 0xf8) {
    MoveBubl();
    return;
  }
  a = ram[AirBubbleTimer]; zero_flag = a == 0; // if air bubble timer not expired,
  // branch to leave, otherwise create new air bubble
  if (!zero_flag) {
    return;
  }
  SetupBubble(); // fallthrough
  return;
}

// in: x; out: none
void SetupBubble(void) {
  y = 0x0; // load default value here
  a = ram[PlayerFacingDir]; // get player's facing direction
  carry_flag = (a & 1) != 0; a = a >> 1; // move d0 to carry
  // Original branch: branch to use default value if facing left
  if (carry_flag) {
    y = 0x8; // otherwise load alternate value here
  }
  // PosBubl:
  a = y; // use value loaded as adder
  { uint16_t sum = a + ram[Player_X_Position] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add to player's horizontal position
  ram[(uint8_t)(Bubble_X_Position + x)] = a; // save as horizontal position for airbubble
  a = ram[Player_PageLoc];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to player's page location
  ram[(uint8_t)(Bubble_PageLoc + x)] = a; // save as page location for airbubble
  a = ram[Player_Y_Position];
  carry_flag = false; // add eight pixels to player's vertical position
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(uint8_t)(Bubble_Y_Position + x)] = a; // save as vertical position for air bubble
  ram[(uint8_t)(Bubble_Y_HighPos + x)] = 0x1; // set vertical high byte for air bubble
  y = ram[0x7]; // get pseudorandom bit, use as offset
  ram[AirBubbleTimer] = ROM(BubbleTimerData + y); // get data for air bubble timer; set air bubble timer
  MoveBubl(); // fallthrough
  return;
}

// in: x; out: none
void MoveBubl(void) {
  y = ram[0x7]; // get pseudorandom bit again, use as offset
  a = ram[Bubble_YMF_Dummy + x];
  carry_flag = true; // subtract pseudorandom amount from dummy variable
  { uint16_t diff = (uint16_t)(a - ROM(Bubble_MForceData + y) - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[Bubble_YMF_Dummy + x] = a; // save dummy variable
  a = ram[(uint8_t)(Bubble_Y_Position + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from airbubble's vertical coordinate
  // if below the status bar,
  // Original branch: branch to go ahead and use to move air bubble upwards
  if (a < 0x20) {
    a = 0xf8; // otherwise set offscreen coordinate
  }
  // Y_Bubl:
  ram[(uint8_t)(Bubble_Y_Position + x)] = a; // store as new vertical coordinate for air bubble
  // ExitBubl:
  return; // leave
}

void RunGameTimer(void) {
  a = ram[OperMode]; zero_flag = a == 0; // get primary mode of operation
  // branch to leave if in title screen mode
  if (zero_flag) {
    return;
  }
  a = ram[GameEngineSubroutine];
  // if routine number less than eight running,
  // branch to leave
  if (a < 0x8) {
    return;
  }
  // if running death routine,
  // branch to leave
  if (a == 0xb) {
    return;
  }
  a = ram[Player_Y_HighPos];
  // if player below the screen,
  // branch to leave regardless of level type
  if (a >= 0x2) {
    return;
  }
  a = ram[GameTimerCtrlTimer]; zero_flag = a == 0; // if game timer control not yet expired,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[GameTimerDisplay];
  a |= ram[(GameTimerDisplay + 1)]; // otherwise check game timer digits
  a |= ram[(GameTimerDisplay + 2)]; zero_flag = a == 0;
  // Original branch: if game timer digits at 000, branch to time-up code
  if (!zero_flag) {
    y = ram[GameTimerDisplay]; // otherwise check first digit
    y--; zero_flag = y == 0; // if first digit not on 1,
    // Original branch: branch to reset game timer control
    if (zero_flag) {
      a = ram[(GameTimerDisplay + 1)]; // otherwise check second and third digits
      a |= ram[(GameTimerDisplay + 2)]; zero_flag = a == 0;
      if (!zero_flag) { goto ResGTCtrl; } // if timer not at 100, branch to reset game timer control
      ram[EventMusicQueue] = TimeRunningOutMusic; // otherwise load time running out music
    }
    
ResGTCtrl:
    ram[GameTimerCtrlTimer] = 0x18; // reset game timer control
    y = 0x23; // set offset for last digit
    ram[(DigitModifier + 5)] = 0xff; // set value to decrement game timer digit
    CALL(DigitsMathRoutine, 0xb794); // do sub to decrement game timer slowly
    a = 0xa4; // set status nybbles to update game timer display
    PrintStatusBarNumbers(); return; // do sub to update the display
  }
  // TimeUpOn:
  ram[PlayerStatus] = a; // init player status (note A will always be zero here)
  CALL(ForceInjury, 0xb79f); // do sub to kill the player (note player is small here)
  ram[GameTimerExpiredFlag]++; // set game timer expiration flag
  // ExGTimer:
  return; // leave
}

// -------------------------------------------------------------------------------------
// in: x; out: none
void WarpZoneObject(void) {
  a = ram[ScrollLock]; zero_flag = a == 0; // check for scroll lock flag
  // branch if not set to leave
  if (zero_flag) {
    return;
  }
  a = ram[Player_Y_Position]; // check to see if player's vertical coordinate has
  a &= ram[Player_Y_HighPos]; zero_flag = a == 0; // same bits set as in vertical high byte (why?)
  // if so, branch to leave
  if (!zero_flag) {
    return;
  }
  ram[ScrollLock] = a; // otherwise nullify scroll lock flag
  ram[WarpZoneControl]++; // increment warp zone flag to make warp pipes for warp zone
  EraseEnemyObject(); return; // kill this object
}

// -------------------------------------------------------------------------------------
// $00 - used in WhirlpoolActivate to store whirlpool length / 2, page location of center of whirlpool
// and also to store movement force exerted on player
// $01 - used in ProcessWhirlpools to store page location of right extent of whirlpool
// and in WhirlpoolActivate to store center of whirlpool
// $02 - used in ProcessWhirlpools to store right extent of whirlpool and in
// WhirlpoolActivate to store maximum vertical speed
void ProcessWhirlpools(void) {
  a = ram[AreaType]; zero_flag = a == 0; // check for water type level
  // branch to leave if not found
  if (!zero_flag) {
    return;
  }
  ram[Whirlpool_Flag] = a; // otherwise initialize whirlpool flag
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  y = 0x4; // otherwise start with last whirlpool data
  do {
    // WhLoop:
    a = ram[Whirlpool_LeftExtent + y]; // get left extent of whirlpool
    carry_flag = false;
    { uint16_t sum = a + ram[Whirlpool_Length + y] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add length of whirlpool
    ram[0x2] = a; // store result as right extent here
    a = ram[Whirlpool_PageLoc + y]; zero_flag = a == 0; // get page location
    // Original branch: if none or page 0, branch to get next data
    if (!zero_flag) {
      a = (uint8_t)(a + 0x0 + carry_flag); // add carry
      ram[0x1] = a; // store result as page location of right extent here
      a = ram[Player_X_Position]; // get player's horizontal position
      carry_flag = true;
      { uint16_t diff = (uint16_t)(a - ram[Whirlpool_LeftExtent + y] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract left extent
      a = ram[Player_PageLoc]; // get player's page location
      a = (uint8_t)(a - ram[Whirlpool_PageLoc + y] - !carry_flag); neg_flag = (a & 0x80) != 0; // subtract borrow
      if (neg_flag) { goto NextWh; } // if player too far left, branch to get next data
      a = ram[0x2]; // otherwise get right extent
      carry_flag = true;
      { uint16_t diff = (uint16_t)(a - ram[Player_X_Position] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract player's horizontal coordinate
      a = ram[0x1]; // get right extent's page location
      a = (uint8_t)(a - ram[Player_PageLoc] - !carry_flag); neg_flag = (a & 0x80) != 0; // subtract borrow
      if (!neg_flag) { goto WhirlpoolActivate; } // if player within right extent, branch to whirlpool code
    }
    
NextWh:
    y--; neg_flag = (y & 0x80) != 0; // move onto next whirlpool data
  } while (!neg_flag); // do this until all whirlpools are checked
  // ExitWh:
  return; // leave
  
WhirlpoolActivate:
  a = ram[Whirlpool_Length + y]; // get length of whirlpool
  a = a >> 1; // divide by 2
  ram[0x0] = a; // save here
  a = ram[Whirlpool_LeftExtent + y]; // get left extent of whirlpool
  carry_flag = false;
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add length divided by 2
  ram[0x1] = a; // save as center of whirlpool
  a = ram[Whirlpool_PageLoc + y]; // get page location
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry
  ram[0x0] = a; // save as page location of whirlpool center
  a = ram[FrameCounter]; // get frame counter
  carry_flag = (a & 1) != 0; a = a >> 1; // shift d0 into carry (to run on every other frame)
  // Original branch: if d0 not set, branch to last part of code
  if (carry_flag) {
    a = ram[0x1]; // get center
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[Player_X_Position] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract player's horizontal coordinate
    a = ram[0x0]; // get page location of center
    a = (uint8_t)(a - ram[Player_PageLoc] - !carry_flag); neg_flag = (a & 0x80) != 0; // subtract borrow
    // Original branch: if player to the left of center, branch
    if (neg_flag) {
      a = ram[Player_X_Position]; // otherwise slowly pull player left, towards the center
      carry_flag = true;
      { uint16_t diff = (uint16_t)(a - 0x1 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract one pixel
      ram[Player_X_Position] = a; // set player's new horizontal coordinate
      a = ram[Player_PageLoc];
      a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow
    } else {
      // LeftWh:
      a = ram[Player_CollisionBits]; // get player's collision bits
      carry_flag = (a & 1) != 0; a = a >> 1; // shift d0 into carry
      if (!carry_flag) { goto WhPull; } // if d0 not set, branch
      a = ram[Player_X_Position]; // otherwise slowly pull player right, towards the center
      carry_flag = false;
      { uint16_t sum = a + 0x1 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add one pixel
      ram[Player_X_Position] = a; // set player's new horizontal coordinate
      a = ram[Player_PageLoc];
      a = (uint8_t)(a + 0x0 + carry_flag); // add carry
    }
    // SetPWh:
    ram[Player_PageLoc] = a; // set player's new page location
  }
  
WhPull:
  ram[0x0] = 0x10; // set vertical movement force
  a = 0x1;
  ram[Whirlpool_Flag] = a; // set whirlpool flag to be used later
  ram[0x2] = a; // also set maximum vertical speed
  a = a >> 1;
  x = a; // set X for player offset
  ImposeGravity(); return; // jump to put whirlpool effect on player vertically, do not return
}

// -------------------------------------------------------------------------------------
void FlagpoleRoutine(void) {
  x = 0x5; // set enemy object offset
  ram[ObjectOffset] = x; // to special use slot
  a = ram[(uint8_t)(Enemy_ID + x)];
  // if flagpole flag not found,
  // branch to leave
  if (a != FlagpoleFlagObject) {
    return;
  }
  a = ram[GameEngineSubroutine];
  // if flagpole slide routine not running,
  // Original branch: branch to near the end of code
  if (a == 0x4) {
    a = ram[Player_State];
    zero_flag = a == 0x3; // if player state not climbing,
    if (!zero_flag) { goto SkipScore; } // branch to near the end of code
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // check flagpole flag's vertical coordinate
    carry_flag = a >= 0xaa; // if flagpole flag down to a certain point,
    if (carry_flag) { goto GiveFPScr; } // branch to end the level
    a = ram[Player_Y_Position]; // check player's vertical coordinate
    carry_flag = a >= 0xa2; // if player down to a certain point,
    if (carry_flag) { goto GiveFPScr; } // branch to end the level
    a = ram[Enemy_YMF_Dummy + x];
    { uint16_t sum = a + 0xff + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add movement amount to dummy variable
    ram[Enemy_YMF_Dummy + x] = a; // save dummy variable
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get flag's vertical coordinate
    a = (uint8_t)(a + 0x1 + carry_flag); // add 1 plus carry to move flag, and
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // store vertical coordinate
    a = ram[FlagpoleFNum_YMFDummy];
    carry_flag = true; // subtract movement amount from dummy variable
    { uint16_t diff = (uint16_t)(a - 0xff - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[FlagpoleFNum_YMFDummy] = a; // save dummy variable
    a = ram[FlagpoleFNum_Y_Pos];
    a = (uint8_t)(a - 0x1 - !carry_flag); // subtract one plus borrow to move floatey number,
    ram[FlagpoleFNum_Y_Pos] = a; // and store vertical coordinate here
  }
  
SkipScore:
  goto FPGfx; // jump to skip ahead and draw flag and floatey number
  
GiveFPScr:
  y = ram[FlagpoleScore]; // get score offset from earlier (when player touched flagpole)
  a = ROM(FlagpoleScoreMods + y); // get amount to award player points
  x = ROM(FlagpoleScoreDigits + y); // get digit with which to award points
  ram[DigitModifier + x] = a; // store in digit modifier
  CALL(AddToScore, 0xb8a7); // do sub to award player points depending on height of collision
  ram[GameEngineSubroutine] = 0x5; // set to run end-of-level subroutine on next frame
  
FPGfx:
  CALL(GetEnemyOffscreenBits, 0xb8ae); // get offscreen information
  CALL(RelativeEnemyPosition, 0xb8b1); // get relative coordinates
  CALL(FlagpoleGfxHandler, 0xb8b4); // draw flagpole flag and floatey number
  // ExitFlagP:
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void JumpspringHandler(void) {
  CALL(GetEnemyOffscreenBits, 0xb8bc); // get offscreen information
  a = ram[TimerControl]; zero_flag = a == 0; // check master timer control
  // Original branch: branch to last section if set
  if (zero_flag) {
    a = ram[JumpspringAnimCtrl]; zero_flag = a == 0; // check jumpspring frame control
    if (zero_flag) { goto DrawJSpr; } // branch to last section if not set
    y = a;
    y--; // subtract one from frame control,
    a = y; // the only way a poor nmos 6502 can
    a &= 0b00000010; zero_flag = a == 0; // mask out all but d1, original value still in Y
    // Original branch: if set, branch to move player up
    if (zero_flag) {
      ram[Player_Y_Position]++;
      ram[Player_Y_Position]++; // move player's vertical position down two pixels
    } else {
      // DownJSpr:
      ram[Player_Y_Position]--; // move player's vertical position up two pixels
      ram[Player_Y_Position]--;
    }
    // PosJSpr:
    a = ram[(uint8_t)(Jumpspring_FixedYPos + x)]; // get permanent vertical position
    carry_flag = false;
    a = (uint8_t)(a + ROM(Jumpspring_Y_PosData + y) + carry_flag); // add value using frame control as offset
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // store as new vertical position
    // check frame control offset (second frame is $00)
    // Original branch: if offset not yet at third frame ($01), skip to next part
    if (y >= 0x1) {
      a = ram[A_B_Buttons];
      a &= A_Button; zero_flag = a == 0; // check saved controller bits for A button press
      if (zero_flag) { goto BounceJS; } // skip to next part if A not pressed
      a &= ram[PreviousA_B_Buttons]; zero_flag = a == 0; // check for A button pressed in previous frame
      if (!zero_flag) { goto BounceJS; } // skip to next part if so
      ram[JumpspringForce] = 0xf4; // otherwise write new jumpspring force here
    }
    
BounceJS:
    zero_flag = y == 0x3; // check frame control offset again
    if (!zero_flag) { goto DrawJSpr; } // skip to last part if not yet at fifth frame ($03)
    ram[Player_Y_Speed] = ram[JumpspringForce]; // store jumpspring force as player's new vertical speed
    ram[JumpspringAnimCtrl] = 0x0; // initialize jumpspring frame control
  }
  
DrawJSpr:
  CALL(RelativeEnemyPosition, 0xb904); // get jumpspring's relative coordinates
  CALL(EnemyGfxHandler, 0xb907); // draw jumpspring
  CALL(OffscreenBoundsCheck, 0xb90a); // check to see if we need to kill it
  a = ram[JumpspringAnimCtrl]; zero_flag = a == 0; // if frame control at zero, don't bother
  // trying to animate it, just leave
  if (zero_flag) {
    return;
  }
  a = ram[JumpspringTimer]; zero_flag = a == 0;
  // if jumpspring timer not expired yet, leave
  if (!zero_flag) {
    return;
  }
  ram[JumpspringTimer] = 0x4; // otherwise initialize jumpspring timer
  ram[JumpspringAnimCtrl]++; // increment frame control to animate jumpspring
  // ExJSpring:
  return; // leave
}

// -------------------------------------------------------------------------------------
// in: x, y; out: none
void Setup_Vine(void) {
  ram[(uint8_t)(Enemy_ID + x)] = VineObject; // load identifier for vine object; store in buffer
  ram[(uint8_t)(Enemy_Flag + x)] = 0x1; // set flag for enemy object buffer
  ram[(uint8_t)(Enemy_PageLoc + x)] = ram[Block_PageLoc + y]; // copy page location from previous object
  ram[(uint8_t)(Enemy_X_Position + x)] = ram[Block_X_Position + y]; // copy horizontal coordinate from previous object
  a = ram[Block_Y_Position + y];
  ram[(uint8_t)(Enemy_Y_Position + x)] = a; // copy vertical coordinate from previous object
  y = ram[VineFlagOffset]; zero_flag = y == 0; // load vine flag/offset to next available vine slot
  // Original branch: if set at all, don't bother to store vertical
  if (zero_flag) {
    ram[VineStart_Y_Position] = a; // otherwise store vertical coordinate here
  }
  // NextVO:
  ram[VineObjOffset + y] = x; // store object offset to next available vine slot; using vine flag as offset
  ram[VineFlagOffset]++; // increment vine flag offset
  ram[Square2SoundQueue] = Sfx_GrowVine; // load vine grow sound
  return;
}

// -------------------------------------------------------------------------------------
// $06-$07 - used as address to block buffer data
// $02 - used as vertical high nybble of block buffer offset
// in: x; out: x
void VineObjectHandler(void) {
  // check enemy offset for special use slot
  // Original branch: if not in last slot, branch to leave
  if (x == 0x5) {
    y = ram[VineFlagOffset];
    y--; // decrement vine flag in Y, use as offset
    a = ram[VineHeight];
    zero_flag = a == ROM(VineHeightData + y); // if vine has reached certain height,
    // Original branch: branch ahead to skip this part
    if (!zero_flag) {
      a = ram[FrameCounter]; // get frame counter
      a = a >> 1; // shift d1 into carry
      carry_flag = (a & 1) != 0; a = a >> 1;
      if (!carry_flag) { goto RunVSubs; } // if d1 not set (2 frames every 4) skip this part
      a = ram[(Enemy_Y_Position + 5)];
      a = (uint8_t)(a - 0x1 - !carry_flag); // subtract vertical position of vine
      ram[(Enemy_Y_Position + 5)] = a; // one pixel every frame it's time
      ram[VineHeight]++; // increment vine height
    }
    
RunVSubs:
    a = ram[VineHeight]; // if vine still very small,
    carry_flag = a >= 0x8; // branch to leave
    if (!carry_flag) { goto ExitVH; }
    CALL(RelativeEnemyPosition, 0xb973); // get relative coordinates of vine,
    CALL(GetEnemyOffscreenBits, 0xb976); // and any offscreen bits
    y = 0x0; // initialize offset used in draw vine sub
    do {
      // VDrawLoop:
      CALL(DrawVine, 0xb97b); // draw vine
      y++; // increment offset
      zero_flag = y == ram[VineFlagOffset]; // if offset in Y and offset here
    } while (!zero_flag); // do not yet match, loop back to draw more vine
    a = ram[Enemy_OffscreenBits];
    a &= 0b00001100; zero_flag = a == 0; // mask offscreen bits
    // Original branch: if none of the saved offscreen bits set, skip ahead
    if (!zero_flag) {
      y--; // otherwise decrement Y to get proper offset again
      do {
        // KillVine:
        x = ram[VineObjOffset + y]; // get enemy object offset for this vine object
        CALL(EraseEnemyObject, 0xb98f); // kill this vine object
        y--; neg_flag = (y & 0x80) != 0; // decrement Y
      } while (!neg_flag); // if any vine objects left, loop back to kill it
      ram[VineFlagOffset] = a; // initialize vine flag/offset
      ram[VineHeight] = a; // initialize vine height
    }
    // WrCMTile:
    a = ram[VineHeight]; // check vine height
    carry_flag = a >= 0x20; // if vine small (less than 32 pixels tall)
    if (!carry_flag) { goto ExitVH; } // then branch ahead to leave
    x = 0x6; // set offset in X to last enemy slot
    a = 0x1; // set A to obtain horizontal in $04, but we don't care
    y = 0x1b; // set Y to offset to get block at ($04, $10) of coordinates
    CALL(BlockBufferCollision, 0xb9a8); // do a sub to get block buffer address set, return contents
    y = ram[0x2];
    carry_flag = y >= 0xd0; // if vertical high nybble offset beyond extent of
    if (carry_flag) { goto ExitVH; } // current block buffer, branch to leave, do not write
    a = read_byte(indirect_y_addr(0x6)); zero_flag = a == 0; // otherwise check contents of block buffer at 
    if (!zero_flag) { goto ExitVH; } // current offset, if not empty, branch to leave
    dynamic_ram_write(indirect_y_addr(0x6), 0x26); // otherwise, write climbing metatile to block buffer
  }
  
ExitVH:
  x = ram[ObjectOffset]; // get enemy object offset and leave
  return;
}

// -------------------------------------------------------------------------------------
void ProcessCannons(void) {
  a = ram[AreaType]; zero_flag = a == 0; // get area type
  // if water type area, branch to leave
  if (zero_flag) {
    return;
  }
  x = 0x2;
  do {
    // ThreeSChk:
    ram[ObjectOffset] = x; // start at third enemy slot
    a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check enemy buffer flag
    // Original branch: if set, branch to check enemy
    if (zero_flag) {
      a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff]; // otherwise get part of LSFR
      y = ram[SecondaryHardMode]; // get secondary hard mode flag, use as offset
      a &= ROM(CannonBitmasks + y); // mask out bits of LSFR as decided by flag
      carry_flag = a >= 0x6; // check to see if lower nybble is above certain value
      if (carry_flag) { goto Chk_BB; } // if so, branch to check enemy
      y = a; // transfer masked contents of LSFR to Y as pseudorandom offset
      a = ram[Cannon_PageLoc + y]; zero_flag = a == 0; // get page location
      if (zero_flag) { goto Chk_BB; } // if not set or on page 0, branch to check enemy
      a = ram[Cannon_Timer + y]; zero_flag = a == 0; // get cannon timer
      // Original branch: if expired, branch to fire cannon
      if (!zero_flag) {
        a = (uint8_t)(a - 0x0 - !carry_flag); // otherwise subtract borrow (note carry will always be clear here)
        ram[Cannon_Timer + y] = a; // to count timer down
        goto Chk_BB; // then jump ahead to check enemy
      }
      // FireCannon:
      a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set,
      if (!zero_flag) { goto Chk_BB; } // branch to check enemy
      ram[Cannon_Timer + y] = 0xe; // otherwise we start creating one; first, reset cannon timer
      ram[(uint8_t)(Enemy_PageLoc + x)] = ram[Cannon_PageLoc + y]; // get page location of cannon; save as page location of bullet bill
      ram[(uint8_t)(Enemy_X_Position + x)] = ram[Cannon_X_Position + y]; // get horizontal coordinate of cannon; save as horizontal coordinate of bullet bill
      a = ram[Cannon_Y_Position + y]; // get vertical coordinate of cannon
      carry_flag = true;
      a = (uint8_t)(a - 0x8 - !carry_flag); // subtract eight pixels (because enemies are 24 pixels tall)
      ram[(uint8_t)(Enemy_Y_Position + x)] = a; // save as vertical coordinate of bullet bill
      a = 0x1;
      ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // set vertical high byte of bullet bill
      ram[(uint8_t)(Enemy_Flag + x)] = a; // set buffer flag
      a = a >> 1; // shift right once to init A
      ram[(uint8_t)(Enemy_State + x)] = a; // then initialize enemy's state
      ram[Enemy_BoundBoxCtrl + x] = 0x9; // set bounding box size control for bullet bill
      ram[(uint8_t)(Enemy_ID + x)] = BulletBill_CannonVar; // load identifier for bullet bill (cannon variant)
    } else {
      
Chk_BB:
      a = ram[(uint8_t)(Enemy_ID + x)]; // check enemy identifier for bullet bill (cannon variant)
      zero_flag = a == BulletBill_CannonVar;
      if (!zero_flag) { goto Next3Slt; } // if not found, branch to get next slot
      CALL(OffscreenBoundsCheck, 0xba22); // otherwise, check to see if it went offscreen
      a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check enemy buffer flag
      if (zero_flag) { goto Next3Slt; } // if not set, branch to get next slot
      CALL(GetEnemyOffscreenBits, 0xba29); // otherwise, get offscreen information
      CALL(BulletBillHandler, 0xba2c); // then do sub to handle bullet bill
    }
    
Next3Slt:
    x--; neg_flag = (x & 0x80) != 0; // move onto next slot
  } while (!neg_flag); // do this until first three slots are checked
  // ExCannon:
  return; // then leave
}

// --------------------------------
// in: x; out: x
void BulletBillHandler(void) {
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set,
  // Original branch: branch to run subroutines except movement sub
  if (zero_flag) {
    a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0;
    // Original branch: if bullet bill's state set, branch to check defeated state
    if (zero_flag) {
      a = ram[Enemy_OffscreenBits]; // otherwise load offscreen bits
      a &= 0b00001100; // mask out bits
      zero_flag = a == 0b00001100; // check to see if all bits are set
      if (zero_flag) { goto KillBB; } // if so, branch to kill this object
      y = 0x1; // set to move right by default
      CALL(PlayerEnemyDiff, 0xba49); // get horizontal difference between player and bullet bill
      // Original branch: if enemy to the left of player, branch
      if (!neg_flag) {
        y++; // otherwise increment to move left
      }
      // SetupBB:
      ram[(uint8_t)(Enemy_MovingDir + x)] = y; // set bullet bill's moving direction
      y--; // decrement to use as offset
      ram[(uint8_t)(Enemy_X_Speed + x)] = ROM(BulletBillXSpdData + y); // get horizontal speed based on moving direction; and store it
      a = ram[0x0]; // get horizontal difference
      a = (uint8_t)(a + 0x28 + carry_flag); // add 40 pixels
      carry_flag = a >= 0x50; // if less than a certain amount, player is too close
      if (!carry_flag) { goto KillBB; } // to cannon either on left or right side, thus branch
      ram[(uint8_t)(Enemy_State + x)] = 0x1; // otherwise set bullet bill's state
      ram[(EnemyFrameTimer + x) & 0x7ff] = 0xa; // set enemy frame timer
      ram[Square2SoundQueue] = Sfx_Blast; // play fireworks/gunfire sound
    }
    // ChkDSte:
    a = ram[(uint8_t)(Enemy_State + x)]; // check enemy state for d5 set
    a &= 0b00100000; zero_flag = a == 0;
    // Original branch: if not set, skip to move horizontally
    if (!zero_flag) {
      CALL(MoveD_EnemyVertically, 0xba72); // otherwise do sub to move bullet bill vertically
    }
    // BBFly:
    CALL(MoveEnemyHorizontally, 0xba75); // do sub to move bullet bill horizontally
  }
  // RunBBSubs:
  CALL(GetEnemyOffscreenBits, 0xba78); // get offscreen information
  CALL(RelativeEnemyPosition, 0xba7b); // get relative coordinates
  CALL(GetEnemyBoundBox, 0xba7e); // get bounding box coordinates
  CALL(PlayerEnemyCollision, 0xba81); // handle player to enemy collisions
  EnemyGfxHandler(); return; // draw the bullet bill and leave
  
KillBB:
  CALL(EraseEnemyObject, 0xba87); // kill bullet bill and leave
  return;
}

// -------------------------------------------------------------------------------------
// in: none; out: x, carry
void SpawnHammerObj(void) {
  a = ram[(PseudoRandomBitReg + 1)]; // get pseudorandom bits from
  a &= 0b00000111; zero_flag = a == 0; // second part of LSFR
  // Original branch: if any bits are set, branch and use as offset
  if (zero_flag) {
    a = ram[(PseudoRandomBitReg + 1)];
    a &= 0b00001000; // get d3 from same part of LSFR
  }
  // SetMOfs:
  y = a; // use either d3 or d2-d0 for offset here
  a = ram[Misc_State + y]; zero_flag = a == 0; // if any values loaded in
  // Original branch: $2a-$32 where offset is then leave with carry clear
  if (zero_flag) {
    x = ROM(HammerEnemyOfsData + y); // get offset of enemy slot to check using Y as offset
    a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check enemy buffer flag at offset
    if (!zero_flag) { goto NoHammer; } // if buffer flag set, branch to leave with carry clear
    x = ram[ObjectOffset]; // get original enemy object offset
    ram[HammerEnemyOffset + y] = x; // save here
    ram[Misc_State + y] = 0x90; // save hammer's state here
    ram[Misc_BoundBoxCtrl + y] = 0x7; // set something else entirely, here
    carry_flag = true; // return with carry set
    return;
  }
  
NoHammer:
  x = ram[ObjectOffset]; // get original enemy object offset
  carry_flag = false; // return with carry clear
  return;
}

// --------------------------------
// $00 - used to set downward force
// $01 - used to set upward force (residual)
// $02 - used to set maximum speed
// in: x; out: x
void ProcHammerObj(void) {
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set
  // Original branch: skip all of this code and go to last subs at the end
  if (zero_flag) {
    a = ram[(uint8_t)(Misc_State + x)]; // otherwise get hammer's state
    a &= 0b01111111; // mask out d7
    y = ram[HammerEnemyOffset + x]; // get enemy object offset that spawned this hammer
    { uint8_t m = 0x2; carry_flag = a >= m; zero_flag = a == m; } // check hammer's state
    // Original branch: if currently at 2, branch
    if (!zero_flag) {
      if (carry_flag) { goto SetHPos; } // if greater than 2, branch elsewhere
      a = x;
      carry_flag = false; // add 13 bytes to use
      a = (uint8_t)(a + 0xd + carry_flag); // proper misc object
      x = a; // return offset to X
      ram[0x0] = 0x10; // set downward movement force
      ram[0x1] = 0xf; // set upward movement force (not used)
      ram[0x2] = 0x4; // set maximum vertical speed
      a = 0x0; // set A to impose gravity on hammer
      CALL(ImposeGravity, 0xbaea); // do sub to impose gravity on hammer and move vertically
      CALL(MoveObjectHorizontally, 0xbaed); // do sub to move it horizontally
      x = ram[ObjectOffset]; // get original misc object offset
    } else {
      // SetHSpd:
      ram[(uint8_t)(Misc_Y_Speed + x)] = 0xfe; // set hammer's vertical speed
      a = ram[Enemy_State + y]; // get enemy object state
      a &= 0b11110111; // mask out d3
      ram[Enemy_State + y] = a; // store new state
      x = ram[(uint8_t)(Enemy_MovingDir + y)]; // get enemy's moving direction
      x--; // decrement to use as offset
      a = ROM(HammerXSpdData + x); // get proper speed to use based on moving direction
      x = ram[ObjectOffset]; // reobtain hammer's buffer offset
      ram[(uint8_t)(Misc_X_Speed + x)] = a; // set hammer's horizontal speed
      
SetHPos:
      ram[(uint8_t)(Misc_State + x)]--; // decrement hammer's state
      a = ram[Enemy_X_Position + y]; // get enemy's horizontal position
      carry_flag = false;
      { uint16_t sum = a + 0x2 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // set position 2 pixels to the right
      ram[(uint8_t)(Misc_X_Position + x)] = a; // store as hammer's horizontal position
      a = ram[Enemy_PageLoc + y]; // get enemy's page location
      a = (uint8_t)(a + 0x0 + carry_flag); // add carry
      ram[(uint8_t)(Misc_PageLoc + x)] = a; // store as hammer's page location
      a = ram[Enemy_Y_Position + y]; // get enemy's vertical position
      carry_flag = true;
      a = (uint8_t)(a - 0xa - !carry_flag); // move position 10 pixels upward
      ram[(uint8_t)(Misc_Y_Position + x)] = a; // store as hammer's vertical position
      ram[(uint8_t)(Misc_Y_HighPos + x)] = 0x1; // set hammer's vertical high byte
      goto RunHSubs; // unconditional branch to skip first routine
    }
    // RunAllH:
    CALL(PlayerHammerCollision, 0xbb2a); // handle collisions
  }
  
RunHSubs:
  CALL(GetMiscOffscreenBits, 0xbb2d); // get offscreen information
  CALL(RelativeMiscPosition, 0xbb30); // get relative coordinates
  CALL(GetMiscBoundBox, 0xbb33); // get bounding box coordinates
  CALL(DrawHammer, 0xbb36); // draw the hammer
  return; // and we are done here
}

// -------------------------------------------------------------------------------------
// $02 - used to store vertical high nybble offset from block buffer routine
// $06 - used to store low byte of block buffer address
// in: x; out: none
void CoinBlock(void) {
  CALL(FindEmptyMiscSlot, 0xbb3a); // set offset for empty or last misc object buffer slot
  ram[Misc_PageLoc + y] = ram[(uint8_t)(Block_PageLoc + x)]; // get page location of block object; store as page location of misc object
  a = ram[(uint8_t)(Block_X_Position + x)]; // get horizontal coordinate of block object
  a |= 0x5; // add 5 pixels
  ram[Misc_X_Position + y] = a; // store as horizontal coordinate of misc object
  a = ram[(uint8_t)(Block_Y_Position + x)]; // get vertical coordinate of block object
  a = (uint8_t)(a - 0x10 - !carry_flag); // subtract 16 pixels
  ram[Misc_Y_Position + y] = a; // store as vertical coordinate of misc object
  JCoinC(); return; // jump to rest of code as applies to this misc object
}

// in: x; out: x
void SetupJumpCoin(void) {
  CALL(FindEmptyMiscSlot, 0xbb53); // set offset for empty or last misc object buffer slot
  ram[Misc_PageLoc + y] = ram[Block_PageLoc2 + x]; // get page location saved earlier; and save as page location for misc object
  a = ram[0x6]; // get low byte of block buffer offset
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // multiply by 16 to use lower nybble
  a = (uint8_t)(a << 1);
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  a |= 0x5; // add five pixels
  ram[Misc_X_Position + y] = a; // save as horizontal coordinate for misc object
  a = ram[0x2]; // get vertical high nybble offset from earlier
  a = (uint8_t)(a + 0x20 + carry_flag); // add 32 pixels for the status bar
  ram[Misc_Y_Position + y] = a; // store as vertical coordinate
  JCoinC(); // fallthrough
  return;
}

// in: x, y; out: x
void JCoinC(void) {
  ram[Misc_Y_Speed + y] = 0xfb; // set vertical speed
  a = 0x1;
  ram[Misc_Y_HighPos + y] = a; // set vertical high byte
  ram[Misc_State + y] = a; // set state for misc object
  ram[Square2SoundQueue] = a; // load coin grab sound
  ram[ObjectOffset] = x; // store current control bit as misc object offset 
  CALL(GiveOneCoin, 0xbb7f); // update coin tally on the screen and coin amount variable
  ram[CoinTallyFor1Ups]++; // increment coin tally used to activate 1-up block flag
  return;
}

// in: none; out: y, carry
void FindEmptyMiscSlot(void) {
  y = 0x8; // start at end of misc objects buffer
  do {
    // FMiscLoop:
    a = ram[Misc_State + y]; zero_flag = a == 0; // get misc object state
    if (zero_flag) { goto UseMiscS; } // branch if none found to use current offset
    y--; // decrement offset
    { uint8_t m = 0x5; carry_flag = y >= m; zero_flag = y == m; } // do this for three slots
  } while (!zero_flag); // do this until all slots are checked
  y = 0x8; // if no empty slots found, use last slot
  
UseMiscS:
  ram[JumpCoinMiscOffset] = y; // store offset of misc object buffer here (residual)
  return;
}

// -------------------------------------------------------------------------------------
void MiscObjectsCore(void) {
  x = 0x8; // set at end of misc object buffer
  do {
    // MiscLoop:
    ram[ObjectOffset] = x; // store misc object offset here
    a = ram[(uint8_t)(Misc_State + x)]; zero_flag = a == 0; // check misc object state
    // Original branch: branch to check next slot
    if (!zero_flag) {
      carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // otherwise shift d7 into carry
      // Original branch: if d7 not set, jumping coin, thus skip to rest of code here
      if (carry_flag) {
        CALL(ProcHammerObj, 0xbba3); // otherwise go to process hammer,
        goto MiscLoopBack; // then check next slot
        // --------------------------------
        // $00 - used to set downward force
        // $01 - used to set upward force (residual)
        // $02 - used to set maximum speed
      }
      // ProcJumpCoin:
      y = ram[(uint8_t)(Misc_State + x)]; // check misc object state
      y--; zero_flag = y == 0; // decrement to see if it's set to 1
      // Original branch: if so, branch to handle jumping coin
      if (!zero_flag) {
        ram[(uint8_t)(Misc_State + x)]++; // otherwise increment state to either start off or as timer
        a = ram[(uint8_t)(Misc_X_Position + x)]; // get horizontal coordinate for misc object
        carry_flag = false; // whether its jumping coin (state 0 only) or floatey number
        { uint16_t sum = a + ram[ScrollAmount] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add current scroll speed
        ram[(uint8_t)(Misc_X_Position + x)] = a; // store as new horizontal coordinate
        a = ram[(uint8_t)(Misc_PageLoc + x)]; // get page location
        a = (uint8_t)(a + 0x0 + carry_flag); // add carry
        ram[(uint8_t)(Misc_PageLoc + x)] = a; // store as new page location
        a = ram[(uint8_t)(Misc_State + x)];
        zero_flag = a == 0x30; // check state of object for preset value
        if (!zero_flag) { goto RunJCSubs; } // if not yet reached, branch to subroutines
        ram[(uint8_t)(Misc_State + x)] = 0x0; // otherwise nullify object state
        goto MiscLoopBack; // and move onto next slot
      }
      // JCoinRun:
      a = x;
      carry_flag = false; // add 13 bytes to offset for next subroutine
      a = (uint8_t)(a + 0xd + carry_flag);
      x = a;
      ram[0x0] = 0x50; // set downward movement amount
      a = 0x6; // set maximum vertical speed
      ram[0x2] = a;
      a = a >> 1; // divide by 2 and set
      ram[0x1] = a; // as upward movement amount (apparently residual)
      a = 0x0; // set A to impose gravity on jumping coin
      CALL(ImposeGravity, 0xbbdd); // do sub to move coin vertically and impose gravity on it
      x = ram[ObjectOffset]; // get original misc object offset
      a = ram[(uint8_t)(Misc_Y_Speed + x)]; // check vertical speed
      // Original branch: if not moving downward fast enough, keep state as-is
      if (a == 0x5) {
        ram[(uint8_t)(Misc_State + x)]++; // otherwise increment state to change to floatey number
      }
      
RunJCSubs:
      CALL(RelativeMiscPosition, 0xbbea); // get relative coordinates
      CALL(GetMiscOffscreenBits, 0xbbed); // get offscreen information
      CALL(GetMiscBoundBox, 0xbbf0); // get bounding box coordinates (why?)
      CALL(JCoinGfxHandler, 0xbbf3); // draw the coin or floatey number
    }
    
MiscLoopBack:
    x--; neg_flag = (x & 0x80) != 0; // decrement misc object offset
  } while (!neg_flag); // loop back until all misc objects handled
  return; // then leave
}

// -------------------------------------------------------------------------------------
// in: none; out: x
void GiveOneCoin(void) {
  ram[(DigitModifier + 5)] = 0x1; // set digit modifier to add 1 coin; to the current player's coin tally
  x = ram[CurrentPlayer]; // get current player on the screen
  y = ROM(CoinTallyOffsets + x); // get offset for player's coin tally
  CALL(DigitsMathRoutine, 0xbc0b); // update the coin tally
  ram[CoinTally]++; // increment onscreen player's coin amount
  a = ram[CoinTally];
  // does player have 100 coins yet?
  // Original branch: if not, skip all of this
  if (a == 100) {
    ram[CoinTally] = 0x0; // otherwise, reinitialize coin amount
    ram[NumberofLives]++; // give the player an extra life
    ram[Square2SoundQueue] = Sfx_ExtraLife; // play 1-up sound
  }
  // CoinPoints:
  ram[(DigitModifier + 4)] = 0x2; // set digit modifier to award; 200 points to the player
  AddToScore(); // fallthrough
  return;
}

// in: none; out: x
void AddToScore(void) {
  x = ram[CurrentPlayer]; // get current player
  y = ROM(ScoreOffsets + x); // get offset for player's score
  CALL(DigitsMathRoutine, 0xbc2f); // update the score internally with value in digit modifier
  GetSBNybbles(); // fallthrough
  return;
}

// in: none; out: x
void GetSBNybbles(void) {
  y = ram[CurrentPlayer]; // get current player
  a = ROM(StatusBarNybbles + y); // get nybbles based on player, use to update score and coins
  UpdateNumber(); // fallthrough
  return;
}

// in: a; out: x
void UpdateNumber(void) {
  CALL(PrintStatusBarNumbers, 0xbc38); // print status bar numbers based on nybbles, whatever they be
  y = ram[VRAM_Buffer1_Offset];
  a = ram[(VRAM_Buffer1 - 6) + y]; zero_flag = a == 0; // check highest digit of score
  // Original branch: if zero, overwrite with space tile for zero suppression
  if (zero_flag) {
    ram[(VRAM_Buffer1 - 6) + y] = 0x24;
  }
  // NoZSup:
  x = ram[ObjectOffset]; // get enemy object buffer offset
  return;
}

void PwrUpJmp(void) {
  a = 0x1; // this is a residual jump point in enemy object jump table
  ram[(Enemy_State + 5)] = a; // set power-up object's state
  ram[(Enemy_Flag + 5)] = a; // set buffer flag
  ram[(Enemy_BoundBoxCtrl + 5)] = 0x3; // set bounding box size control for power-up object
  a = ram[PowerUpType];
  // check currently loaded power-up type
  // Original branch: if star or 1-up, branch ahead
  if (a < 0x2) {
    a = ram[PlayerStatus]; // otherwise check player's current status
    // Original branch: if player not fiery, use status as power-up type
    if (a >= 0x2) {
      a = a >> 1; // otherwise shift right to force fire flower type
    }
    // StrType:
    ram[PowerUpType] = a; // store type here
  }
  // PutBehind:
  ram[(Enemy_SprAttrib + 5)] = 0b00100000; // set background priority bit
  ram[Square2SoundQueue] = Sfx_GrowPowerUp; // load power-up reveal sound and leave
  return;
}

// -------------------------------------------------------------------------------------
// in: none; out: x
void PowerUpObjHandler(void) {
  x = 0x5; // set object offset for last slot in enemy object buffer
  ram[ObjectOffset] = x;
  a = ram[(Enemy_State + 5)]; zero_flag = a == 0; // check power-up object's state
  // if not set, branch to leave
  if (zero_flag) {
    return;
  }
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // shift to check if d7 was set in object state
  // Original branch: if not set, branch ahead to skip this part
  if (carry_flag) {
    a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set,
    if (!zero_flag) { goto RunPUSubs; } // branch ahead to enemy object routines
    a = ram[PowerUpType]; zero_flag = a == 0; // check power-up type
    // Original branch: if normal mushroom, branch ahead to move it
    if (!zero_flag) {
      zero_flag = a == 0x3;
      if (zero_flag) { goto ShroomM; } // if 1-up mushroom, branch ahead to move it
      zero_flag = a == 0x2;
      if (!zero_flag) { goto RunPUSubs; } // if not star, branch elsewhere to skip movement
      CALL(MoveJumpingEnemy, 0xbca3); // otherwise impose gravity on star power-up and make it jump
      CALL(EnemyJump, 0xbca6); // note that green paratroopa shares the same code here 
      goto RunPUSubs; // then jump to other power-up subroutines
    }
    
ShroomM:
    CALL(MoveNormalEnemy, 0xbcac); // do sub to make mushrooms move
    CALL(EnemyToBGCollisionDet, 0xbcaf); // deal with collisions
  } else {
    // GrowThePowerUp:
    a = ram[FrameCounter]; // get frame counter
    a &= 0x3; zero_flag = a == 0; // mask out all but 2 LSB
    // Original branch: if any bits set here, branch
    if (zero_flag) {
      ram[(Enemy_Y_Position + 5)]--; // otherwise decrement vertical coordinate slowly
      a = ram[(Enemy_State + 5)]; // load power-up object state
      ram[(Enemy_State + 5)]++; // increment state for next frame (to make power-up rise)
      carry_flag = a >= 0x11; // if power-up object state not yet past 16th pixel,
      if (!carry_flag) { goto ChkPUSte; } // branch ahead to last part here
      ram[(uint8_t)(Enemy_X_Speed + x)] = 0x10; // otherwise set horizontal speed
      a = 0b10000000;
      ram[(Enemy_State + 5)] = a; // and then set d7 in power-up object's state
      carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // shift once to init A
      ram[(Enemy_SprAttrib + 5)] = a; // initialize background priority bit set here
      a = (uint8_t)(a << 1 | carry_flag); // rotate A to set right moving direction
      ram[(uint8_t)(Enemy_MovingDir + x)] = a; // set moving direction
    }
    
ChkPUSte:
    a = ram[(Enemy_State + 5)]; // check power-up object's state
    // for if power-up has risen enough
    // if not, don't even bother running these routines
    if (a < 0x6) {
      return;
    }
  }
  
RunPUSubs:
  CALL(RelativeEnemyPosition, 0xbcda); // get coordinates relative to screen
  CALL(GetEnemyOffscreenBits, 0xbcdd); // get offscreen bits
  CALL(GetEnemyBoundBox, 0xbce0); // get bounding box coordinates
  CALL(DrawPowerUp, 0xbce3); // draw the power-up object
  CALL(PlayerEnemyCollision, 0xbce6); // check for collision with player
  CALL(OffscreenBoundsCheck, 0xbce9); // check to see if it went offscreen
  // ExitPUp:
  return; // and we're done
}

// -------------------------------------------------------------------------------------
// These apply to all routines in this section unless otherwise noted:
// $00 - used to store metatile from block buffer routine
// $02 - used to store vertical high nybble offset from block buffer routine
// $05 - used to store metatile stored in A at beginning of PlayerHeadCollision
// $06-$07 - used as block buffer address indirect
// in: a; out: none
void PlayerHeadCollision(void) {
  ram[0x100 + sp--] = a; // store metatile number to stack
  a = 0x11; // load unbreakable block object state by default
  x = ram[SprDataOffset_Ctrl]; // load offset control bit here
  y = ram[PlayerSize]; zero_flag = y == 0; // check player's size
  // Original branch: if small, branch
  if (zero_flag) {
    a = 0x12; // otherwise load breakable block object state
  }
  // DBlockSte:
  ram[(uint8_t)(Block_State + x)] = a; // store into block object buffer
  CALL(DestroyBlockMetatile, 0xbcfe); // store blank metatile in vram buffer to write to name table
  x = ram[SprDataOffset_Ctrl]; // load offset control bit
  a = ram[0x2]; // get vertical high nybble offset used in block buffer routine
  ram[Block_Orig_YPos + x] = a; // set as vertical coordinate for block object
  y = a;
  ram[Block_BBuf_Low + x] = ram[0x6]; // get low byte of block buffer address used in same routine; save as offset here to be used later
  a = read_byte(indirect_y_addr(0x6)); // get contents of block buffer at old address at $06, $07
  CALL(BlockBumpedChk, 0xbd11); // do a sub to check which block player bumped head on
  ram[0x0] = a; // store metatile here
  y = ram[PlayerSize]; zero_flag = y == 0; // check player's size
  // Original branch: if small, use metatile itself as contents of A
  if (zero_flag) {
    a = y; // otherwise init A (note: big = 0)
  }
  // ChkBrick:
  // Original branch: if no match was found in previous sub, skip ahead
  if (carry_flag) {
    ram[(uint8_t)(Block_State + x)] = 0x11; // otherwise load unbreakable state into block object buffer; note this applies to both player sizes
    a = 0xc4; // load empty block metatile into A for now
    y = ram[0x0]; // get metatile from before
    // is it brick with coins (with line)?
    // Original branch: if so, branch
    if (y != 0x58) {
      zero_flag = y == 0x5d; // is it brick with coins (without line)?
      if (!zero_flag) { goto PutMTileB; } // if not, branch ahead to store empty block metatile
    }
    // StartBTmr:
    a = ram[BrickCoinTimerFlag]; zero_flag = a == 0; // check brick coin timer flag
    // Original branch: if set, timer expired or counting down, thus branch
    if (zero_flag) {
      ram[BrickCoinTimer] = 0xb; // if not set, set brick coin timer
      ram[BrickCoinTimerFlag]++; // and set flag linked to it
    }
    // ContBTmr:
    a = ram[BrickCoinTimer]; zero_flag = a == 0; // check brick coin timer
    // Original branch: if not yet expired, branch to use current metatile
    if (zero_flag) {
      y = 0xc4; // otherwise use empty block metatile
    }
    // PutOldMT:
    a = y; // put metatile into A
  }
  
PutMTileB:
  ram[Block_Metatile + x] = a; // store whatever metatile be appropriate here
  CALL(InitBlock_XY_Pos, 0xbd46); // get block object horizontal coordinates saved
  y = ram[0x2]; // get vertical high nybble offset
  dynamic_ram_write(indirect_y_addr(0x6), 0x23); // write blank metatile $23 to block buffer
  ram[BlockBounceTimer] = 0x10; // set block bounce timer
  a = ram[0x100 + ++sp]; // pull original metatile from stack
  ram[0x5] = a; // and save here
  y = 0x0; // set default offset
  a = ram[CrouchingFlag]; zero_flag = a == 0; // is player crouching?
  // Original branch: if so, branch to increment offset
  if (zero_flag) {
    a = ram[PlayerSize]; zero_flag = a == 0; // is player big?
    if (zero_flag) { goto BigBP; } // if so, branch to use default offset
  }
  // SmallBP:
  y++; // increment for small or big and crouching
  
BigBP:
  a = ram[Player_Y_Position]; // get player's vertical coordinate
  carry_flag = false;
  a = (uint8_t)(a + ROM(BlockYPosAdderData + y) + carry_flag); // add value determined by size
  a &= 0xf0; // mask out low nybble to get 16-pixel correspondence
  ram[(uint8_t)(Block_Y_Position + x)] = a; // save as vertical coordinate for block object
  y = ram[(uint8_t)(Block_State + x)]; // get block object state
  // Original branch: if set to value loaded for unbreakable, branch
  if (y != 0x11) {
    CALL(BrickShatter, 0xbd74); // execute code for breakable brick
  } else {
    // Unbreak:
    CALL(BumpBlock, 0xbd7a); // execute code for unbreakable brick or question block
  }
  // InvOBit:
  a = ram[SprDataOffset_Ctrl]; // invert control bit used by block objects
  a ^= 0x1; // and floatey numbers
  ram[SprDataOffset_Ctrl] = a;
  return; // leave!
}

// --------------------------------
// in: x; out: none
void InitBlock_XY_Pos(void) {
  a = ram[Player_X_Position]; // get player's horizontal coordinate
  carry_flag = false;
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add eight pixels
  a &= 0xf0; // mask out low nybble to give 16-pixel correspondence
  ram[(uint8_t)(Block_X_Position + x)] = a; // save as horizontal coordinate for block object
  a = ram[Player_PageLoc];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location of player
  ram[(uint8_t)(Block_PageLoc + x)] = a; // save as page location of block object
  ram[Block_PageLoc2 + x] = a; // save elsewhere to be used later
  ram[(uint8_t)(Block_Y_HighPos + x)] = ram[Player_Y_HighPos]; // save vertical high byte of player into
  return; // vertical high byte of block object and leave
}

// --------------------------------
void BumpBlock(void) {
  CALL(CheckTopOfBlock, 0xbd9d); // check to see if there's a coin directly above this block
  ram[Square1SoundQueue] = Sfx_Bump; // play bump sound
  a = 0x0;
  ram[(uint8_t)(Block_X_Speed + x)] = a; // initialize horizontal speed for block object
  ram[Block_Y_MoveForce + x] = a; // init fractional movement force
  ram[Player_Y_Speed] = a; // init player's vertical speed
  ram[(uint8_t)(Block_Y_Speed + x)] = 0xfe; // set vertical speed for block object
  a = ram[0x5]; // get original metatile from stack
  CALL(BlockBumpedChk, 0xbdb3); // do a sub to check which block player bumped head on
  // if no match was found, branch to leave
  if (!carry_flag) {
    return;
  }
  a = y; // move block number to A
  carry_flag = a >= 0x9; // if block number was within 0-8 range,
  // Original branch: branch to use current number
  if (carry_flag) {
    a = (uint8_t)(a - 0x5 - !carry_flag); // otherwise subtract 5 for second set to get proper number
  }
  // BlockCode:
  switch (JumpEngine(0xbdbf)) {
    case 0xbdd2: MushFlowerBlock(); return;
    case 0xbb38: CoinBlock(); return;
    case 0xbdd8: ExtraLifeMushBlock(); return;
    case 0xbddf: VineBlock(); return;
    case 0xbdd5: StarBlock(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: none
void MushFlowerBlock(void) {
  a = 0x0; // load mushroom/fire flower into power-up type
  
  loc_48599(); return; // BIT instruction opcode
}

// in: x; out: none
void StarBlock(void) {
  a = 0x2; // load star into power-up type
  loc_48599(); // fallthrough
  return;
}

// in: a, x; out: none
void loc_48599(void) {
  
  loc_48602(); return; // BIT instruction opcode
}

// in: x; out: none
void ExtraLifeMushBlock(void) {
  a = 0x3; // load 1-up mushroom into power-up type
  loc_48602(); // fallthrough
  return;
}

// in: a, x; out: none
void loc_48602(void) {
  goto loc_48602;
  // -------------------------------------------------------------------------------------
  
SetupPowerUp:
  ram[(Enemy_ID + 5)] = PowerUpObject; // load power-up identifier into; special use slot of enemy object buffer
  ram[(Enemy_PageLoc + 5)] = ram[(uint8_t)(Block_PageLoc + x)]; // store page location of block object; as page location of power-up object
  ram[(Enemy_X_Position + 5)] = ram[(uint8_t)(Block_X_Position + x)]; // store horizontal coordinate of block object; as horizontal coordinate of power-up object
  ram[(Enemy_Y_HighPos + 5)] = 0x1; // set vertical high byte of power-up object
  a = ram[(uint8_t)(Block_Y_Position + x)]; // get vertical coordinate of block object
  carry_flag = true;
  a = (uint8_t)(a - 0x8 - !carry_flag); // subtract 8 pixels
  ram[(Enemy_Y_Position + 5)] = a; // and use as vertical coordinate of power-up object
  PwrUpJmp(); // fallthrough
  return;
  
loc_48602:
  ram[0x39] = a; // store correct power-up type
  goto SetupPowerUp;
}

void VineBlock(void) {
  x = 0x5; // load last slot for enemy object buffer
  y = ram[SprDataOffset_Ctrl]; // get control bit
  CALL(Setup_Vine, 0xbde6); // set up vine object
  // ExitBlockChk:
  return; // leave
}

// --------------------------------
// in: a; out: y, carry
void BlockBumpedChk(void) {
  y = 0xd; // start at end of metatile data
  do {
    // BumpChkLoop:
    { uint8_t m = ROM(BrickQBlockMetatiles + y); carry_flag = a >= m; zero_flag = a == m; } // check to see if current metatile matches
    // metatile found in block buffer, branch if so
    if (zero_flag) {
      return;
    }
    y--; neg_flag = (y & 0x80) != 0; // otherwise move onto next metatile
  } while (!neg_flag); // do this until all metatiles are checked
  carry_flag = false; // if none match, return with carry clear
  // MatchBump:
  return; // note carry is set if found match
}

// --------------------------------
void BrickShatter(void) {
  CALL(CheckTopOfBlock, 0xbe04); // check to see if there's a coin directly above this block
  a = Sfx_BrickShatter;
  ram[Block_RepFlag + x] = a; // set flag for block object to immediately replace metatile
  ram[NoiseSoundQueue] = a; // load brick shatter sound
  CALL(SpawnBrickChunks, 0xbe0e); // create brick chunk objects
  ram[Player_Y_Speed] = 0xfe; // set vertical speed for player
  ram[(DigitModifier + 5)] = 0x5; // set digit modifier to give player 50 points
  CALL(AddToScore, 0xbe1a); // do sub to update the score
  x = ram[SprDataOffset_Ctrl]; // load control bit and leave
  return;
}

// --------------------------------
// in: none; out: x
void CheckTopOfBlock(void) {
  x = ram[SprDataOffset_Ctrl]; // load control bit
  y = ram[0x2]; zero_flag = y == 0; // get vertical high nybble offset used in block buffer
  // branch to leave if set to zero, because we're at the top
  if (zero_flag) {
    return;
  }
  a = y; // otherwise set to A
  carry_flag = true;
  a = (uint8_t)(a - 0x10 - !carry_flag); // subtract $10 to move up one row in the block buffer
  ram[0x2] = a; // store as new vertical high nybble offset
  y = a;
  a = read_byte(indirect_y_addr(0x6)); // get contents of block buffer in same column, one row up
  // is it a coin? (not underwater)
  // if not, branch to leave
  if (a != 0xc2) {
    return;
  }
  dynamic_ram_write(indirect_y_addr(0x6), 0x0); // otherwise put blank metatile where coin was
  CALL(RemoveCoin_Axe, 0xbe39); // write blank metatile to vram buffer
  x = ram[SprDataOffset_Ctrl]; // get control bit
  CALL(SetupJumpCoin, 0xbe3f); // create jumping coin object and update coin variables
  // TopEx:
  return; // leave!
}

// --------------------------------
// in: x; out: none
void SpawnBrickChunks(void) {
  ram[Block_Orig_XPos + x] = ram[(uint8_t)(Block_X_Position + x)]; // set horizontal coordinate of block object; as original horizontal coordinate here
  a = 0xf0;
  ram[(uint8_t)(Block_X_Speed + x)] = a; // set horizontal speed for brick chunk objects
  ram[(uint8_t)((Block_X_Speed + 2) + x)] = a;
  ram[(uint8_t)(Block_Y_Speed + x)] = 0xfa; // set vertical speed for one
  ram[(uint8_t)((Block_Y_Speed + 2) + x)] = 0xfc; // set lower vertical speed for the other
  a = 0x0;
  ram[Block_Y_MoveForce + x] = a; // init fractional movement force for both
  ram[(Block_Y_MoveForce + 2) + x] = a;
  ram[(uint8_t)((Block_PageLoc + 2) + x)] = ram[(uint8_t)(Block_PageLoc + x)]; // copy page location
  ram[(uint8_t)((Block_X_Position + 2) + x)] = ram[(uint8_t)(Block_X_Position + x)]; // copy horizontal coordinate
  a = ram[(uint8_t)(Block_Y_Position + x)];
  carry_flag = false; // add 8 pixels to vertical coordinate
  a = (uint8_t)(a + 0x8 + carry_flag); // and save as vertical coordinate for one of them
  ram[(uint8_t)((Block_Y_Position + 2) + x)] = a;
  ram[(uint8_t)(Block_Y_Speed + x)] = 0xfa; // set vertical speed...again??? (redundant)
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void BlockObjectsCore(void) {
  a = ram[(uint8_t)(Block_State + x)]; zero_flag = a == 0; // get state of block object
  // Original branch: if not set, branch to leave
  if (!zero_flag) {
    a &= 0xf; // mask out high nybble
    ram[0x100 + sp--] = a; // push to stack
    y = a; // put in Y for now
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x9 + carry_flag); // add 9 bytes to offset (note two block objects are created
    x = a; // when using brick chunks, but only one offset for both)
    y--; zero_flag = y == 0; // decrement Y to check for solid block state
    // Original branch: branch if found, otherwise continue for brick chunks
    if (!zero_flag) {
      CALL(ImposeGravityBlock, 0xbe82); // do sub to impose gravity on one block object object
      CALL(MoveObjectHorizontally, 0xbe85); // do another sub to move horizontally
      a = x;
      carry_flag = false; // move onto next block object
      a = (uint8_t)(a + 0x2 + carry_flag);
      x = a;
      CALL(ImposeGravityBlock, 0xbe8d); // do sub to impose gravity on other block object
      CALL(MoveObjectHorizontally, 0xbe90); // do another sub to move horizontally
      x = ram[ObjectOffset]; // get block object offset used for both
      CALL(RelativeBlockPosition, 0xbe95); // get relative coordinates
      CALL(GetBlockOffscreenBits, 0xbe98); // get offscreen information
      CALL(DrawBrickChunks, 0xbe9b); // draw the brick chunks
      a = ram[0x100 + ++sp]; // get lower nybble of saved state
      y = ram[(uint8_t)(Block_Y_HighPos + x)]; zero_flag = y == 0; // check vertical high byte of block object
      if (zero_flag) { goto UpdSte; } // if above the screen, branch to kill it
      ram[0x100 + sp--] = a; // otherwise save state back into stack
      a = 0xf0;
      carry_flag = a >= ram[(uint8_t)((Block_Y_Position + 2) + x)]; // check to see if bottom block object went
      // Original branch: to the bottom of the screen, and branch if not
      if (!carry_flag) {
        ram[(uint8_t)((Block_Y_Position + 2) + x)] = a; // otherwise set offscreen coordinate
      }
      // ChkTop:
      a = ram[(uint8_t)(Block_Y_Position + x)]; // get top block object's vertical coordinate
      carry_flag = a >= 0xf0; // see if it went to the bottom of the screen
      a = ram[0x100 + ++sp]; // pull block object state from stack
      if (!carry_flag) { goto UpdSte; } // if not, branch to save state
    } else {
      // BouncingBlockHandler:
      CALL(ImposeGravityBlock, 0xbeb5); // do sub to impose gravity on block object
      x = ram[ObjectOffset]; // get block object offset
      CALL(RelativeBlockPosition, 0xbeba); // get relative coordinates
      CALL(GetBlockOffscreenBits, 0xbebd); // get offscreen information
      CALL(DrawBlock, 0xbec0); // draw the block
      a = ram[(uint8_t)(Block_Y_Position + x)]; // get vertical coordinate
      a &= 0xf; // mask out high nybble
      carry_flag = a >= 0x5; // check to see if low nybble wrapped around
      a = ram[0x100 + ++sp]; // pull state from stack
      if (carry_flag) { goto UpdSte; } // if still above amount, not time to kill block yet, thus branch
      ram[Block_RepFlag + x] = 0x1; // otherwise set flag to replace metatile
    }
    // KillBlock:
    a = 0x0; // if branched here, nullify object state
  }
  
UpdSte:
  ram[(uint8_t)(Block_State + x)] = a; // store contents of A in block object state
  return;
}

// -------------------------------------------------------------------------------------
// $02 - used to store offset to block buffer
// $06-$07 - used to store block buffer address
void BlockObjMT_Updater(void) {
  x = 0x1; // set offset to start with second block object
  do {
    // UpdateLoop:
    ram[ObjectOffset] = x; // set offset here
    a = ram[VRAM_Buffer1]; zero_flag = a == 0; // if vram buffer already being used here,
    // Original branch: branch to move onto next block object
    if (zero_flag) {
      a = ram[Block_RepFlag + x]; zero_flag = a == 0; // if flag for block object already clear,
      if (zero_flag) { goto NextBUpd; } // branch to move onto next block object
      ram[0x6] = ram[Block_BBuf_Low + x]; // get low byte of block buffer; store into block buffer address
      ram[0x7] = 0x5; // set high byte of block buffer address
      a = ram[Block_Orig_YPos + x]; // get original vertical coordinate of block object
      ram[0x2] = a; // store here and use as offset to block buffer
      y = a;
      a = ram[Block_Metatile + x]; // get metatile to be written
      dynamic_ram_write(indirect_y_addr(0x6), a); // write it to the block buffer
      CALL(ReplaceBlockMetatile, 0xbef8); // do sub to replace metatile where block object is
      ram[Block_RepFlag + x] = 0x0; // clear block object flag
    }
    
NextBUpd:
    x--; neg_flag = (x & 0x80) != 0; // decrement block object offset
  } while (!neg_flag); // do this until both block objects are dealt with
  return; // then leave
}

// -------------------------------------------------------------------------------------
// $00 - used to store high nybble of horizontal speed as adder
// $01 - used to store low nybble of horizontal speed
// $02 - used to store adder to page location
// in: x; out: a, x, y
void MoveEnemyHorizontally(void) {
  x++; // increment offset for enemy offset
  CALL(MoveObjectHorizontally, 0xbf05); // position object horizontally according to
  x = ram[ObjectOffset]; // counters, return with saved value in A,
  return; // put enemy offset back in X and leave
}

// in: none; out: a
void MovePlayerHorizontally(void) {
  a = ram[JumpspringAnimCtrl]; zero_flag = a == 0; // if jumpspring currently animating,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  x = a; // otherwise set zero for offset to use player's stuff
  MoveObjectHorizontally(); // fallthrough
  return;
}

// in: x; out: a, y
void MoveObjectHorizontally(void) {
  a = ram[(uint8_t)(SprObject_X_Speed + x)]; // get currently saved value (horizontal
  a = (uint8_t)(a << 1); // speed, secondary counter, whatever)
  a = (uint8_t)(a << 1); // and move low nybble to high
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  ram[0x1] = a; // store result here
  a = ram[(uint8_t)(SprObject_X_Speed + x)]; // get saved value again
  a = a >> 1; // move high nybble to low
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  // if < 8, branch, do not change
  if (a >= 0x8) {
    a |= 0b11110000; // otherwise alter high nybble
  }
  // SaveXSpd:
  ram[0x0] = a; // save result here
  y = 0x0; // load default Y value here
  neg_flag = ((a - 0x0) & 0x80) != 0; // if result positive, leave Y alone
  if (neg_flag) {
    y--; // otherwise decrement Y
  }
  // UseAdder:
  ram[0x2] = y; // save Y here
  a = ram[SprObject_X_MoveForce + x]; // get whatever number's here
  carry_flag = false;
  { uint16_t sum = a + ram[0x1] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add low nybble moved to high
  ram[SprObject_X_MoveForce + x] = a; // store result here
  a = 0x0; // init A
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // rotate carry into d0
  ram[0x100 + sp--] = a; // push onto stack
  { bool carry = (a & 1) != 0; a = (uint8_t)(a >> 1 | carry_flag << 7); carry_flag = carry; } // rotate d0 back onto carry
  a = ram[(uint8_t)(SprObject_X_Position + x)];
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add carry plus saved value (high nybble moved to low
  ram[(uint8_t)(SprObject_X_Position + x)] = a; // plus $f0 if necessary) to object's horizontal position
  a = ram[(uint8_t)(SprObject_PageLoc + x)];
  a = (uint8_t)(a + ram[0x2] + carry_flag); // add carry plus other saved value to the
  ram[(uint8_t)(SprObject_PageLoc + x)] = a; // object's page location and save
  a = ram[0x100 + ++sp];
  carry_flag = false; // pull old carry from stack and add
  a = (uint8_t)(a + ram[0x0] + carry_flag); // to high nybble moved to low
  // ExXMove:
  return; // and leave
}

// --------------------------------
// in: x; out: x
void MoveD_EnemyVertically(void) {
  y = 0x3d; // set quick movement amount downwards
  a = ram[(uint8_t)(Enemy_State + x)]; // then check enemy state
  // if not set to unique state for spiny's egg, go ahead
  // and use, otherwise set different movement amount, continue on
  if (a != 0x5) {
    ContVMove();
    return;
  }
  MoveFallingPlatform(); // fallthrough
  return;
}

// in: x; out: x
void MoveFallingPlatform(void) {
  y = 0x20; // set movement amount
  ContVMove(); // fallthrough
  return;
}

// in: x, y; out: x
void ContVMove(void) {
  SetHiMax(); return; // jump to skip the rest of this
}

// --------------------------------
// in: x; out: x
void MoveDropPlatform(void) {
  y = 0x7f; // set movement amount for drop platform
  SetMdMax(); return; // skip ahead of other value set here
}

// in: x; out: x
void MoveEnemySlowVert(void) {
  y = 0xf; // set movement amount for bowser/other objects
  SetMdMax(); // fallthrough
  return;
}

// in: x, y; out: x
void SetMdMax(void) {
  a = 0x2; // set maximum speed in A
  SetXMoveAmt(); return; // unconditional branch
}

// --------------------------------
// in: x; out: x
void MoveJ_EnemyVertically(void) {
  y = 0x1c; // set movement amount for podoboo/other objects
  SetHiMax(); // fallthrough
  return;
}

// in: x, y; out: x
void SetHiMax(void) {
  a = 0x3; // set maximum speed in A
  SetXMoveAmt(); // fallthrough
  return;
}

// in: a, x, y; out: x
void SetXMoveAmt(void) {
  ram[0x0] = y; // set movement amount here
  x++; // increment X for enemy offset
  CALL(ImposeGravitySprObj, 0xbf9b); // do a sub to move enemy object downwards
  x = ram[ObjectOffset]; // get enemy object buffer offset and leave
  return;
}

// --------------------------------
// in: x; out: none
void ImposeGravityBlock(void) {
  y = 0x1; // set offset for maximum speed
  ram[0x0] = 0x50; // set movement amount here
  a = ROM(MaxSpdBlockData + y); // get maximum speed
  ImposeGravitySprObj(); // fallthrough
  return;
}

// in: a, x; out: none
void ImposeGravitySprObj(void) {
  ram[0x2] = a; // set maximum speed here
  a = 0x0; // set value to move downwards
  ImposeGravity(); return; // jump to the code that actually moves it
}

// --------------------------------
// in: x; out: x
void MovePlatformDown(void) {
  a = 0x0; // save value to stack (if branching here, execute next
  
  loc_49081(); return; // part as BIT instruction)
}

// in: x; out: x
void MovePlatformUp(void) {
  a = 0x1; // save value to stack
  loc_49081(); // fallthrough
  return;
}

// in: a, x; out: x
void loc_49081(void) {
  ram[0x100 + sp--] = a;
  y = ram[(uint8_t)(Enemy_ID + x)]; // get enemy object identifier
  x++; // increment offset for enemy object
  a = 0x5; // load default value here
  // residual comparison, object #29 never executes
  // Original branch: this code, thus unconditional branch here
  if (y == 0x29) {
    a = 0x9; // residual code
  }
  // SetDplSpd:
  ram[0x0] = a; // save downward movement amount here
  ram[0x1] = 0xa; // save upward movement amount here
  ram[0x2] = 0x3; // save maximum vertical speed here
  a = ram[0x100 + ++sp]; // get value from stack
  y = a; // use as Y, then move onto code shared by red koopa
  RedPTroopaGrav(); // fallthrough
  return;
}

// in: a, x; out: x
void RedPTroopaGrav(void) {
  CALL(ImposeGravity, 0xbfd3); // do a sub to move object gradually
  x = ram[ObjectOffset]; // get enemy object offset and leave
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used for downward force
// $01 - used for upward force
// $07 - used as adder for vertical position
// in: a, x; out: none
void ImposeGravity(void) {
  ram[0x100 + sp--] = a; // push value to stack
  a = ram[SprObject_YMF_Dummy + x];
  carry_flag = false; // add value in movement force to contents of dummy variable
  { uint16_t sum = a + ram[SprObject_Y_MoveForce + x] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[SprObject_YMF_Dummy + x] = a;
  y = 0x0; // set Y to zero by default
  a = ram[(uint8_t)(SprObject_Y_Speed + x)]; neg_flag = (a & 0x80) != 0; // get current vertical speed
  // Original branch: if currently moving downwards, do not decrement Y
  if (neg_flag) {
    y--; // otherwise decrement Y
  }
  // AlterYP:
  ram[0x7] = y; // store Y here
  { uint16_t sum = a + ram[(uint8_t)(SprObject_Y_Position + x)] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add vertical position to vertical speed plus carry
  ram[(uint8_t)(SprObject_Y_Position + x)] = a; // store as new vertical position
  a = ram[(uint8_t)(SprObject_Y_HighPos + x)];
  a = (uint8_t)(a + ram[0x7] + carry_flag); // add carry plus contents of $07 to vertical high byte
  ram[(uint8_t)(SprObject_Y_HighPos + x)] = a; // store as new vertical high byte
  a = ram[SprObject_Y_MoveForce + x];
  carry_flag = false;
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add downward movement amount to contents of $0433
  ram[SprObject_Y_MoveForce + x] = a;
  a = ram[(uint8_t)(SprObject_Y_Speed + x)]; // add carry to vertical speed and store
  a = (uint8_t)(a + 0x0 + carry_flag);
  ram[(uint8_t)(SprObject_Y_Speed + x)] = a;
  neg_flag = ((a - ram[0x2]) & 0x80) != 0; // compare to maximum speed
  // Original branch: if less than preset value, skip this part
  if (!neg_flag) {
    a = ram[SprObject_Y_MoveForce + x];
    carry_flag = a >= 0x80; // if less positively than preset maximum, skip this part
    if (!carry_flag) { goto ChkUpM; }
    ram[(uint8_t)(SprObject_Y_Speed + x)] = ram[0x2]; // keep vertical speed within maximum value
    ram[SprObject_Y_MoveForce + x] = 0x0; // clear fractional
  }
  
ChkUpM:
  a = ram[0x100 + ++sp]; zero_flag = a == 0; // get value from stack
  // if set to zero, branch to leave
  if (zero_flag) {
    return;
  }
  a = ram[0x2];
  a ^= 0b11111111; // otherwise get two's compliment of maximum speed
  y = a;
  y++;
  ram[0x7] = y; // store two's compliment here
  a = ram[SprObject_Y_MoveForce + x];
  carry_flag = true; // subtract upward movement amount from contents
  { uint16_t diff = (uint16_t)(a - ram[0x1] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // of movement force, note that $01 is twice as large as $00,
  ram[SprObject_Y_MoveForce + x] = a; // thus it effectively undoes add we did earlier
  a = ram[(uint8_t)(SprObject_Y_Speed + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from vertical speed and store
  ram[(uint8_t)(SprObject_Y_Speed + x)] = a;
  neg_flag = ((a - ram[0x7]) & 0x80) != 0; // compare vertical speed to two's compliment
  // if less negatively than preset maximum, skip this part
  if (!neg_flag) {
    return;
  }
  a = ram[SprObject_Y_MoveForce + x];
  // check if fractional part is above certain amount,
  // and if so, branch to leave
  if (a >= 0x80) {
    return;
  }
  ram[(uint8_t)(SprObject_Y_Speed + x)] = ram[0x7]; // keep vertical speed within maximum value
  ram[SprObject_Y_MoveForce + x] = 0xff; // clear fractional
  // ExVMove:
  return; // leave!
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void EnemiesAndLoopsCore(void) {
  a = ram[(uint8_t)(Enemy_Flag + x)]; // check data here for MSB set
  ram[0x100 + sp--] = a; // save in stack
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  // Original branch: if MSB set in enemy flag, branch ahead of jumps
  if (!carry_flag) {
    a = ram[0x100 + ++sp]; zero_flag = a == 0; // get from stack
    // Original branch: if data zero, branch
    if (!zero_flag) {
      goto RunEnemyObjectsCore; // otherwise, jump to run enemy subroutines
    }
    // ChkAreaTsk:
    a = ram[AreaParserTaskNum]; // check number of tasks to perform
    a &= 0x7;
    // if at a specific task, jump and leave
    if (a == 0x7) {
      return;
    }
  } else {
    // ChkBowserF:
    a = ram[0x100 + ++sp]; // get data from stack
    a &= 0b00001111; // mask out high nybble
    y = a;
    a = ram[Enemy_Flag + y]; zero_flag = a == 0; // use as pointer and load same place with different offset
    if (!zero_flag) {
      return;
    }
    ram[(uint8_t)(Enemy_Flag + x)] = a; // if second enemy flag not set, also clear first one
    // ExitELCore:
    return;
  }
  
ProcLoopCommand:
  a = ram[LoopCommand]; zero_flag = a == 0; // check if loop command was found
  if (!zero_flag) {
    a = ram[CurrentColumnPos]; zero_flag = a == 0; // check to see if we're still on the first page
    if (!zero_flag) { goto ChkEnemyFrenzy; } // if not, do not loop yet
    y = 0xb; // start at the end of each set of loop data
    do {
      
FindLoop:
      y--; neg_flag = (y & 0x80) != 0;
      if (neg_flag) { goto ChkEnemyFrenzy; } // if all data is checked and not match, do not loop
      a = ram[WorldNumber]; // check to see if one of the world numbers
      zero_flag = a == ROM(LoopCmdWorldNumber + y); // matches our current world number
      if (!zero_flag) { goto FindLoop; }
      a = ram[CurrentPageLoc]; // check to see if one of the page numbers
      zero_flag = a == ROM(LoopCmdPageNumber + y); // matches the page we're currently on
    } while (!zero_flag);
    a = ram[Player_Y_Position]; // check to see if the player is at the correct position
    zero_flag = a == ROM(LoopCmdYPosition + y); // if not, branch to check for world 7
    if (!zero_flag) { goto WrongChk; }
    a = ram[Player_State]; // check to see if the player is
    zero_flag = a == 0x0; // on solid ground (i.e. not jumping or falling)
    if (!zero_flag) { goto WrongChk; } // if not, player fails to pass loop, and loopback
    a = ram[WorldNumber]; // are we in world 7? (check performed on correct
    zero_flag = a == World7; // vertical position and on solid ground)
    if (!zero_flag) { goto InitMLp; } // if not, initialize flags used there, otherwise
    ram[MultiLoopCorrectCntr]++; // increment counter for correct progression
    
IncMLoop:
    ram[MultiLoopPassCntr]++; // increment master multi-part counter
    a = ram[MultiLoopPassCntr]; // have we done all three parts?
    zero_flag = a == 0x3;
    if (!zero_flag) { goto InitLCmd; } // if not, skip this part
    a = ram[MultiLoopCorrectCntr]; // if so, have we done them all correctly?
    zero_flag = a == 0x3;
    if (zero_flag) { goto InitMLp; } // if so, branch past unnecessary check here
    goto DoLpBack; // unconditional branch if previous branch fails
    
WrongChk:
    a = ram[WorldNumber]; // are we in world 7? (check performed on
    zero_flag = a == World7; // incorrect vertical position or not on solid ground)
    if (zero_flag) { goto IncMLoop; }
    
DoLpBack:
    CALL(ExecGameLoopback, 0xc11e); // if player is not in right place, loop back
    CALL(KillAllEnemies, 0xc121);
    
InitMLp:
    a = 0x0; // initialize counters used for multi-part loop commands
    ram[MultiLoopPassCntr] = a;
    ram[MultiLoopCorrectCntr] = a;
    
InitLCmd:
    ram[LoopCommand] = 0x0; // initialize loop command flag
    // --------------------------------
  }
  
ChkEnemyFrenzy:
  a = ram[EnemyFrenzyQueue]; zero_flag = a == 0; // check for enemy object in frenzy queue
  // Original branch: if not, skip this part
  if (!zero_flag) {
    ram[(uint8_t)(Enemy_ID + x)] = a; // store as enemy object identifier here
    ram[(uint8_t)(Enemy_Flag + x)] = 0x1; // activate enemy object flag
    a = 0x0;
    ram[(uint8_t)(Enemy_State + x)] = a; // initialize state and frenzy queue
    ram[EnemyFrenzyQueue] = a;
    InitEnemyObject(); return; // and then jump to deal with this enemy
    // --------------------------------
    // $06 - used to hold page location of extended right boundary
    // $07 - used to hold high nybble of position of extended right boundary
  }
  // ProcessEnemyData:
  y = ram[EnemyDataOffset]; // get offset of enemy object data
  a = read_byte(indirect_y_addr(EnemyData)); // load first byte
  // check for EOD terminator
  if (a == 0xff) {
  } else {
    // CheckEndofBuffer:
    a &= 0b00001111; // check for special row $0e
    // Original branch: if found, branch, otherwise
    if (a != 0xe) {
      carry_flag = x >= 0x5; // check for end of buffer
      if (!carry_flag) { goto CheckRightBounds; } // if not at end of buffer, branch
      y++;
      a = read_byte(indirect_y_addr(EnemyData)); // check for specific value here
      a &= 0b00111111; // not sure what this was intended for, exactly
      zero_flag = a == 0x2e; // this part is quite possibly residual code
      if (zero_flag) { goto CheckRightBounds; } // but it has the effect of keeping enemies out of
      return; // the sixth slot
    }
    
CheckRightBounds:
    a = ram[ScreenRight_X_Pos]; // add 48 to pixel coordinate of right boundary
    carry_flag = false;
    { uint16_t sum = a + 0x30 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
    a &= 0b11110000; // store high nybble
    ram[0x7] = a;
    a = ram[ScreenRight_PageLoc]; // add carry to page location of right boundary
    a = (uint8_t)(a + 0x0 + carry_flag);
    ram[0x6] = a; // store page location + carry
    y = ram[EnemyDataOffset];
    y++;
    a = read_byte(indirect_y_addr(EnemyData)); // if MSB of enemy object is clear, branch to check for row $0f
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) {
      a = ram[EnemyObjectPageSel]; zero_flag = a == 0; // if page select already set, do not set again
      if (!zero_flag) { goto CheckPageCtrlRow; }
      ram[EnemyObjectPageSel]++; // otherwise, if MSB is set, set page select 
      ram[EnemyObjectPageLoc]++; // and increment page control
    }
    
CheckPageCtrlRow:
    y--;
    a = read_byte(indirect_y_addr(EnemyData)); // reread first byte
    a &= 0xf;
    // check for special row $0f
    // Original branch: if not found, branch to position enemy object
    if (a == 0xf) {
      a = ram[EnemyObjectPageSel]; zero_flag = a == 0; // if page select set,
      if (!zero_flag) { goto PositionEnemyObj; } // branch without reading second byte
      y++;
      a = read_byte(indirect_y_addr(EnemyData)); // otherwise, get second byte, mask out 2 MSB
      a &= 0b00111111;
      ram[EnemyObjectPageLoc] = a; // store as page control for enemy object data
      ram[EnemyDataOffset]++; // increment enemy object data offset 2 bytes
      ram[EnemyDataOffset]++;
      ram[EnemyObjectPageSel]++; // set page select for enemy object data and 
      goto ProcLoopCommand; // jump back to process loop commands again
    }
    
PositionEnemyObj:
    ram[(uint8_t)(Enemy_PageLoc + x)] = ram[EnemyObjectPageLoc]; // store page control as page location; for enemy object
    a = read_byte(indirect_y_addr(EnemyData)); // get first byte of enemy object
    a &= 0b11110000;
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // store column position
    carry_flag = a >= ram[ScreenRight_X_Pos]; // check column position against right boundary
    a = ram[(uint8_t)(Enemy_PageLoc + x)]; // without subtracting, then subtract borrow
    { uint16_t diff = (uint16_t)(a - ram[ScreenRight_PageLoc] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // from page location
    // Original branch: if enemy object beyond or at boundary, branch
    if (!carry_flag) {
      a = read_byte(indirect_y_addr(EnemyData));
      a &= 0b00001111; // check for special row $0e
      zero_flag = a == 0xe; // if found, jump elsewhere
      if (zero_flag) { goto ParseRow0e; }
      goto CheckThreeBytes; // if not found, unconditional jump
    }
    // CheckRightExtBounds:
    a = ram[0x7]; // check right boundary + 48 against
    carry_flag = a >= ram[(uint8_t)(Enemy_X_Position + x)]; // column position without subtracting,
    a = ram[0x6]; // then subtract borrow from page control temp
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(Enemy_PageLoc + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // plus carry
    if (!carry_flag) { goto CheckFrenzyBuffer; } // if enemy object beyond extended boundary, branch
    ram[(uint8_t)(Enemy_Y_HighPos + x)] = 0x1; // store value in vertical high byte
    a = read_byte(indirect_y_addr(EnemyData)); // get first byte again
    a = (uint8_t)(a << 1); // multiply by four to get the vertical
    a = (uint8_t)(a << 1); // coordinate
    a = (uint8_t)(a << 1);
    a = (uint8_t)(a << 1);
    ram[(uint8_t)(Enemy_Y_Position + x)] = a;
    zero_flag = a == 0xe0; // do one last check for special row $0e
    if (zero_flag) { goto ParseRow0e; } // (necessary if branched to $c1cb)
    y++;
    a = read_byte(indirect_y_addr(EnemyData)); // get second byte of object
    a &= 0b01000000; zero_flag = a == 0; // check to see if hard mode bit is set
    // Original branch: if not, branch to check for group enemy objects
    if (!zero_flag) {
      a = ram[SecondaryHardMode]; zero_flag = a == 0; // if set, check to see if secondary hard mode flag
      if (zero_flag) { goto Inc2B; } // is on, and if not, branch to skip this object completely
    }
    // CheckForEnemyGroup:
    a = read_byte(indirect_y_addr(EnemyData)); // get second byte and mask out 2 MSB
    a &= 0b00111111;
    // check for value below $37
    if (a >= 0x37) {
      carry_flag = a >= 0x3f; // if $37 or greater, check for value
      if (!carry_flag) { goto DoGroup; } // below $3f, branch if below $3f
    }
    // BuzzyBeetleMutate:
    // if below $37, check for goomba
    // Original branch: value ($3f or more always fails)
    if (a == Goomba) {
      y = ram[PrimaryHardMode]; zero_flag = y == 0; // check if primary hard mode flag is set
      if (zero_flag) { goto StrID; } // and if so, change goomba to buzzy beetle
      a = BuzzyBeetle;
    }
    
StrID:
    ram[(uint8_t)(Enemy_ID + x)] = a; // store enemy object number into buffer
    ram[(uint8_t)(Enemy_Flag + x)] = 0x1; // set flag for enemy in buffer
    CALL(InitEnemyObject, 0xc210);
    a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check to see if flag is set
    if (!zero_flag) { goto Inc2B; } // if not, leave, otherwise branch
    return;
  }
  
CheckFrenzyBuffer:
  a = ram[EnemyFrenzyBuffer]; zero_flag = a == 0; // if enemy object stored in frenzy buffer
  // Original branch: then branch ahead to store in enemy object buffer
  if (zero_flag) {
    a = ram[VineFlagOffset]; // otherwise check vine flag offset
    // if other value <> 1, leave
    if (a != 0x1) {
      return;
    }
    a = VineObject; // otherwise put vine in enemy identifier
  }
  // StrFre:
  ram[(uint8_t)(Enemy_ID + x)] = a; // store contents of frenzy buffer into enemy identifier value
  InitEnemyObject(); // fallthrough
  return;
  
DoGroup:
  goto HandleGroupEnemies; // handle enemy group objects
  
ParseRow0e:
  y++; // increment Y to load third byte of object
  y++;
  a = read_byte(indirect_y_addr(EnemyData));
  a = a >> 1; // move 3 MSB to the bottom, effectively
  a = a >> 1; // making %xxx00000 into %00000xxx
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  zero_flag = a == ram[WorldNumber]; // is it the same world number as we're on?
  // Original branch: if not, do not use (this allows multiple uses
  if (zero_flag) {
    y--; // of the same area, like the underground bonus areas)
    ram[AreaPointer] = read_byte(indirect_y_addr(EnemyData)); // otherwise, get second byte and use as offset; to addresses for level and enemy object data
    y++;
    a = read_byte(indirect_y_addr(EnemyData)); // get third byte again, and this time mask out
    a &= 0b00011111; // the 3 MSB from before, save as page number to be
    ram[EntrancePage] = a; // used upon entry to area, if area is entered
  }
  // NotUse:
  goto Inc3B;
  
CheckThreeBytes:
  y = ram[EnemyDataOffset]; // load current offset for enemy object data
  a = read_byte(indirect_y_addr(EnemyData)); // get first byte
  a &= 0b00001111; // check for special row $0e
  zero_flag = a == 0xe;
  if (!zero_flag) { goto Inc2B; }
  
Inc3B:
  ram[EnemyDataOffset]++; // if row = $0e, increment three bytes
  
Inc2B:
  ram[EnemyDataOffset]++; // otherwise increment two bytes
  ram[EnemyDataOffset]++;
  ram[EnemyObjectPageSel] = 0x0; // init page select for enemy objects
  x = ram[ObjectOffset]; // reload current offset in enemy buffers
  return; // and leave
  // --------------------------------
  // $00 - used to store Y position of group enemies
  // $01 - used to store enemy ID
  // $02 - used to store page location of right side of screen
  // $03 - used to store X position of right side of screen
  
HandleGroupEnemies:
  y = 0x0; // load value for green koopa troopa
  carry_flag = true;
  a = (uint8_t)(a - 0x37 - !carry_flag); // subtract $37 from second byte read
  ram[0x100 + sp--] = a; // save result in stack for now
  // was byte in $3b-$3e range?
  // Original branch: if so, branch
  if (a < 0x4) {
    ram[0x100 + sp--] = a; // save another copy to stack
    y = Goomba; // load value for goomba enemy
    a = ram[PrimaryHardMode]; zero_flag = a == 0; // if primary hard mode flag not set,
    // Original branch: branch, otherwise change to value
    if (!zero_flag) {
      y = BuzzyBeetle; // for buzzy beetle
    }
    // PullID:
    a = ram[0x100 + ++sp]; // get second copy from stack
  }
  // SnglID:
  ram[0x1] = y; // save enemy id here
  y = 0xb0; // load default y coordinate
  a &= 0x2; zero_flag = a == 0; // check to see if d1 was set
  // Original branch: if so, move y coordinate up,
  if (!zero_flag) {
    y = 0x70; // otherwise branch and use default
  }
  // SetYGp:
  ram[0x0] = y; // save y coordinate here
  ram[0x2] = ram[ScreenRight_PageLoc]; // get page number of right edge of screen; save here
  ram[0x3] = ram[ScreenRight_X_Pos]; // get pixel coordinate of right edge; save here
  y = 0x2; // load two enemies by default
  a = ram[0x100 + ++sp]; // get first copy from stack
  carry_flag = (a & 1) != 0; a = a >> 1; // check to see if d0 was set
  // Original branch: if not, use default value
  if (carry_flag) {
    y++; // otherwise increment to three enemies
  }
  // CntGrp:
  ram[NumberofGroupEnemies] = y; // save number of enemies here
  do {
    // GrLoop:
    x = 0xff; // start at beginning of enemy buffers
    do {
      // GSltLp:
      x++; // increment and branch if past
      carry_flag = x >= 0x5; // end of buffers
      if (carry_flag) { goto NextED; }
      a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check to see if enemy is already
    } while (!zero_flag); // stored in buffer, and branch if so
    ram[(uint8_t)(Enemy_ID + x)] = ram[0x1]; // store enemy object identifier
    ram[(uint8_t)(Enemy_PageLoc + x)] = ram[0x2]; // store page location for enemy object
    a = ram[0x3];
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // store x coordinate for enemy object
    carry_flag = false;
    { uint16_t sum = a + 0x18 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add 24 pixels for next enemy
    ram[0x3] = a;
    a = ram[0x2]; // add carry to page location for
    a = (uint8_t)(a + 0x0 + carry_flag); // next enemy
    ram[0x2] = a;
    ram[(uint8_t)(Enemy_Y_Position + x)] = ram[0x0]; // store y coordinate for enemy object
    a = 0x1; // activate flag for buffer, and
    ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // put enemy within the screen vertically
    ram[(uint8_t)(Enemy_Flag + x)] = a;
    CALL(CheckpointEnemyID, 0xc77e); // process each enemy object separately
    ram[NumberofGroupEnemies]--; zero_flag = ram[NumberofGroupEnemies] == 0; // do this until we run out of enemy objects
  } while (!zero_flag);
  
NextED:
  goto Inc2B; // jump to increment data offset and leave
  // -------------------------------------------------------------------------------------
  
RunEnemyObjectsCore:
  x = ram[ObjectOffset]; // get offset for enemy object buffer
  a = 0x0; // load value 0 for jump engine by default
  y = ram[(uint8_t)(Enemy_ID + x)];
  carry_flag = y >= 0x15; // if enemy object < $15, use default value
  if (carry_flag) {
    a = y; // otherwise subtract $14 from the value and use
    a = (uint8_t)(a - 0x14 - !carry_flag); // as value for jump engine
  }
  // JmpEO:
  switch (JumpEngine(0xc891)) {
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
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// --------------------------------
// loop command data
// in: y; out: none
void ExecGameLoopback(void) {
  a = ram[Player_PageLoc]; // send player back four pages
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[Player_PageLoc] = a;
  a = ram[CurrentPageLoc]; // send current page back four pages
  carry_flag = true;
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[CurrentPageLoc] = a;
  a = ram[ScreenLeft_PageLoc]; // subtract four from page location
  carry_flag = true; // of screen's left border
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[ScreenLeft_PageLoc] = a;
  a = ram[ScreenRight_PageLoc]; // do the same for the page location
  carry_flag = true; // of screen's right border
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[ScreenRight_PageLoc] = a;
  a = ram[AreaObjectPageLoc]; // subtract four from page control
  carry_flag = true; // for area objects
  a = (uint8_t)(a - 0x4 - !carry_flag);
  ram[AreaObjectPageLoc] = a;
  a = 0x0; // initialize page select for both
  ram[EnemyObjectPageSel] = a; // area and enemy objects
  ram[AreaObjectPageSel] = a;
  ram[EnemyDataOffset] = a; // initialize enemy object data offset
  ram[EnemyObjectPageLoc] = a; // and enemy object page control
  ram[AreaDataOffset] = ROM(AreaDataOfsLoopback + y); // adjust area object offset based on; which loop command we encountered
  return;
}

// in: x; out: x
void InitEnemyObject(void) {
  ram[(uint8_t)(Enemy_State + x)] = 0x0; // initialize enemy state
  CALL(CheckpointEnemyID, 0xc22c); // jump ahead to run jump engine and subroutines
  // ExEPar:
  return; // then leave
}

// in: x; out: x
void CheckpointEnemyID(void) {
  a = ram[(uint8_t)(Enemy_ID + x)];
  carry_flag = a >= 0x15; // check enemy object identifier for $15 or greater
  // Original branch: and branch straight to the jump engine if found
  if (!carry_flag) {
    y = a; // save identifier in Y register for now
    a = ram[(uint8_t)(Enemy_Y_Position + x)];
    a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels to what will eventually be the
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // enemy object's vertical coordinate ($00-$14 only)
    ram[EnemyOffscrBitsMasked + x] = 0x1; // set offscreen masked bit
    a = y; // get identifier back and use as offset for jump engine
  }
  // InitEnemyRoutines:
  switch (JumpEngine(0xc281)) {
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
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void NoInitCode(void) {
  return; // this executed when enemy object has no init code
}

// --------------------------------
// in: x; out: none
void InitGoomba(void) {
  CALL(InitNormalEnemy, 0xc2f3); // set appropriate horizontal speed
  SmallBBox(); return; // set $09 as bounding box control, set other values
}

// --------------------------------
// in: x; out: none
void InitPodoboo(void) {
  a = 0x2; // set enemy position to below
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // the bottom of the screen
  ram[(uint8_t)(Enemy_Y_Position + x)] = a;
  a = a >> 1;
  ram[(EnemyIntervalTimer + x) & 0x7ff] = a; // set timer for enemy
  a = a >> 1;
  ram[(uint8_t)(Enemy_State + x)] = a; // initialize enemy state, then jump to use
  SmallBBox(); return; // $09 as bounding box size and set other things
}

// --------------------------------
// in: x; out: none
void InitRetainerObj(void) {
  ram[(uint8_t)(Enemy_Y_Position + x)] = 0xb8; // set fixed vertical position for; princess/mushroom retainer object
  return;
}

// --------------------------------
// in: x; out: y
void InitNormalEnemy(void) {
  y = 0x1; // load offset of 1 by default
  a = ram[PrimaryHardMode]; zero_flag = a == 0; // check for primary hard mode flag set
  if (zero_flag) {
    y--; // if not set, decrement offset
  }
  // GetESpd:
  a = ROM(NormalXSpdData + y); // get appropriate horizontal speed
  SetESpd(); // fallthrough
  return;
}

// in: a, x; out: none
void SetESpd(void) {
  ram[(uint8_t)(Enemy_X_Speed + x)] = a; // store as speed for enemy object
  TallBBox(); return; // branch to set bounding box control and other data
}

// --------------------------------
// in: x; out: none
void InitRedKoopa(void) {
  CALL(InitNormalEnemy, 0xc320); // load appropriate horizontal speed
  ram[(uint8_t)(Enemy_State + x)] = 0x1; // set enemy state for red koopa troopa $03
  return;
}

// --------------------------------
// in: x; out: none
void InitHammerBro(void) {
  a = 0x0; // init horizontal speed and timer used by hammer bro
  ram[HammerThrowingTimer + x] = a; // apparently to time hammer throwing
  ram[(uint8_t)(Enemy_X_Speed + x)] = a;
  y = ram[SecondaryHardMode]; // get secondary hard mode flag
  ram[(EnemyIntervalTimer + x) & 0x7ff] = ROM(HBroWalkingTimerData + y); // set value as delay for hammer bro to walk left
  a = 0xb; // set specific value for bounding box size control
  SetBBox(); return;
}

// --------------------------------
// in: x; out: none
void InitHorizFlySwimEnemy(void) {
  a = 0x0; // initialize horizontal speed
  SetESpd(); return;
}

// --------------------------------
// in: x; out: none
void InitBloober(void) {
  ram[(uint8_t)(BlooperMoveSpeed + x)] = 0x0; // initialize horizontal speed
  SmallBBox(); // fallthrough
  return;
}

// in: x; out: a
void SmallBBox(void) {
  a = 0x9; // set specific bounding box size control
  SetBBox(); return; // unconditional branch
}

// --------------------------------
// in: x; out: none
void InitRedPTroopa(void) {
  y = 0x30; // load central position adder for 48 pixels down
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; neg_flag = (a & 0x80) != 0; // set vertical coordinate into location to
  ram[RedPTroopaOrigXPos + x] = a; // be used as original vertical coordinate
  // Original branch: if vertical coordinate < $80
  if (neg_flag) {
    y = 0xe0; // if => $80, load position adder for 32 pixels up
  }
  // GetCent:
  a = y; // send central position adder to A
  a = (uint8_t)(a + ram[(uint8_t)(Enemy_Y_Position + x)] + carry_flag); // add to current vertical coordinate
  ram[(uint8_t)(RedPTroopaCenterYPos + x)] = a; // store as central vertical coordinate
  TallBBox(); // fallthrough
  return;
}

// in: x; out: none
void TallBBox(void) {
  a = 0x3; // set specific bounding box size control
  SetBBox(); // fallthrough
  return;
}

// in: a, x; out: a
void SetBBox(void) {
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control here
  ram[(uint8_t)(Enemy_MovingDir + x)] = 0x2; // set moving direction for left
  InitVStf(); // fallthrough
  return;
}

// in: x; out: a
void InitVStf(void) {
  a = 0x0; // initialize vertical speed
  ram[(uint8_t)(Enemy_Y_Speed + x)] = a; // and movement force
  ram[Enemy_Y_MoveForce + x] = a;
  return;
}

// --------------------------------
// in: x; out: none
void InitBulletBill(void) {
  ram[(uint8_t)(Enemy_MovingDir + x)] = 0x2; // set moving direction for left
  ram[Enemy_BoundBoxCtrl + x] = 0x9; // set bounding box control for $09
  return;
}

// --------------------------------
// in: x; out: none
void InitCheepCheep(void) {
  CALL(SmallBBox, 0xc377); // set vertical bounding box, speed, init others
  a = ram[(PseudoRandomBitReg + x) & 0x7ff]; // check one portion of LSFR
  a &= 0b00010000; // get d4 from it
  ram[(uint8_t)(CheepCheepMoveMFlag + x)] = a; // save as movement flag of some sort
  ram[CheepCheepOrigYPos + x] = ram[(uint8_t)(Enemy_Y_Position + x)]; // save original vertical coordinate here
  return;
}

// --------------------------------
// in: x; out: none
void InitLakitu(void) {
  a = ram[EnemyFrenzyBuffer]; zero_flag = a == 0; // check to see if an enemy is already in
  // Original branch: the frenzy buffer, and branch to kill lakitu if so
  if (zero_flag) {
    SetupLakitu(); // fallthrough
    return;
  }
  // KillLakitu:
  EraseEnemyObject(); return;
}

// in: x; out: none
void SetupLakitu(void) {
  ram[LakituReappearTimer] = 0x0; // erase counter for lakitu's reappearance
  CALL(InitHorizFlySwimEnemy, 0xc391); // set $03 as bounding box, set other attributes
  TallBBox2(); return; // set $03 as bounding box again (not necessary) and leave
}

// --------------------------------
// $01-$03 - used to hold pseudorandom difference adjusters
// in: x; out: x
void LakituAndSpinyHandler(void) {
  a = ram[FrenzyEnemyTimer]; zero_flag = a == 0; // if timer here not expired, leave
  if (!zero_flag) {
    return;
  }
  // if we are on the special use slot, leave
  if (x >= 0x5) {
    return;
  }
  ram[FrenzyEnemyTimer] = 0x80; // set timer
  y = 0x4; // start with the last enemy slot
  do {
    // ChkLak:
    a = ram[Enemy_ID + y]; // check all enemy slots to see
    zero_flag = a == Lakitu; // if lakitu is on one of them
    if (zero_flag) { goto CreateSpiny; } // if so, branch out of this loop
    y--; neg_flag = (y & 0x80) != 0; // otherwise check another slot
  } while (!neg_flag); // loop until all slots are checked
  ram[LakituReappearTimer]++; // increment reappearance timer
  a = ram[LakituReappearTimer];
  // check to see if we're up to a certain value yet
  // if not, leave
  if (a < 0x7) {
    return;
  }
  x = 0x4; // start with the last enemy slot again
  do {
    // ChkNoEn:
    a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check enemy buffer flag for non-active enemy slot
    if (zero_flag) { goto CreateL; } // branch out of loop if found
    x--; neg_flag = (x & 0x80) != 0; // otherwise check next slot
  } while (!neg_flag); // branch until all slots are checked
  goto RetEOfs; // if no empty slots were found, branch to leave
  
CreateL:
  ram[(uint8_t)(Enemy_State + x)] = 0x0; // initialize enemy state
  ram[(uint8_t)(Enemy_ID + x)] = Lakitu; // create lakitu enemy object
  CALL(SetupLakitu, 0xc3dd); // do a sub to set up lakitu
  a = 0x20;
  CALL(PutAtRightExtent, 0xc3e2); // finish setting up lakitu
  
RetEOfs:
  x = ram[ObjectOffset]; // get enemy object buffer offset again and leave
  // ExLSHand:
  return;
  // --------------------------------
  
CreateSpiny:
  a = ram[Player_Y_Position]; // if player above a certain point, branch to leave
  if (a < 0x2c) {
    return;
  }
  a = ram[Enemy_State + y]; zero_flag = a == 0; // if lakitu is not in normal state, branch to leave
  if (!zero_flag) {
    return;
  }
  ram[(uint8_t)(Enemy_PageLoc + x)] = ram[Enemy_PageLoc + y]; // store horizontal coordinates (high and low) of lakitu; into the coordinates of the spiny we're going to create
  ram[(uint8_t)(Enemy_X_Position + x)] = ram[Enemy_X_Position + y];
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = 0x1; // put spiny within vertical screen unit
  a = ram[Enemy_Y_Position + y]; // put spiny eight pixels above where lakitu is
  carry_flag = true;
  a = (uint8_t)(a - 0x8 - !carry_flag);
  ram[(uint8_t)(Enemy_Y_Position + x)] = a;
  a = ram[(PseudoRandomBitReg + x) & 0x7ff]; // get 2 LSB of LSFR and save to Y
  a &= 0b00000011;
  y = a;
  x = 0x2;
  do {
    // DifLoop:
    ram[(uint8_t)(0x1 + x)] = ROM(PRDiffAdjustData + y); // get three values and save them; to $01-$03
    y++;
    y++; // increment Y four bytes for each value
    y++;
    y++;
    x--; neg_flag = (x & 0x80) != 0; // decrement X for each one
  } while (!neg_flag); // loop until all three are written
  x = ram[ObjectOffset]; // get enemy object buffer offset
  CALL(PlayerLakituDiff, 0xc41f); // move enemy, change direction, get value - difference
  y = ram[Player_X_Speed]; // check player's horizontal speed
  // Original branch: if moving faster than a certain amount, branch elsewhere
  if (y < 0x8) {
    y = a; // otherwise save value in A to Y for now
    a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff];
    a &= 0b00000011; zero_flag = a == 0; // get one of the LSFR parts and save the 2 LSB
    // Original branch: branch if neither bits are set
    if (!zero_flag) {
      a = y;
      a ^= 0b11111111; // otherwise get two's compliment of Y
      y = a;
      y++;
    }
    // UsePosv:
    a = y; // put value from A in Y back to A (they will be lost anyway)
  }
  // SetSpSpd:
  CALL(SmallBBox, 0xc436); // set bounding box control, init attributes, lose contents of A
  y = 0x2;
  ram[(uint8_t)(Enemy_X_Speed + x)] = a; // set horizontal speed to zero because previous contents
  neg_flag = ((a - 0x0) & 0x80) != 0; // of A were lost...branch here will never be taken for
  // Original branch: the same reason
  if (!neg_flag) {
    y--;
  }
  // SpinyRte:
  ram[(uint8_t)(Enemy_MovingDir + x)] = y; // set moving direction to the right
  ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xfd; // set vertical speed to move upwards
  ram[(uint8_t)(Enemy_Flag + x)] = 0x1; // enable enemy object by setting flag
  ram[(uint8_t)(Enemy_State + x)] = 0x5; // put spiny in egg state and leave
  // ChpChpEx:
  return;
}

// --------------------------------
// in: x; out: none
void InitLongFirebar(void) {
  CALL(DuplicateEnemyObj, 0xc45b); // create enemy object for long firebar
  InitShortFirebar(); // fallthrough
  return;
}

// in: x; out: none
void InitShortFirebar(void) {
  ram[(uint8_t)(FirebarSpinState_Low + x)] = 0x0; // initialize low byte of spin state
  a = ram[(uint8_t)(Enemy_ID + x)]; // subtract $1b from enemy identifier
  carry_flag = true; // to get proper offset for firebar data
  a = (uint8_t)(a - 0x1b - !carry_flag);
  y = a;
  ram[FirebarSpinSpeed + x] = ROM(FirebarSpinSpdData + y); // get spinning speed of firebar
  ram[(uint8_t)(FirebarSpinDirection + x)] = ROM(FirebarSpinDirData + y); // get spinning direction of firebar
  a = ram[(uint8_t)(Enemy_Y_Position + x)];
  carry_flag = false; // add four pixels to vertical coordinate
  a = (uint8_t)(a + 0x4 + carry_flag);
  ram[(uint8_t)(Enemy_Y_Position + x)] = a;
  a = ram[(uint8_t)(Enemy_X_Position + x)];
  carry_flag = false; // add four pixels to horizontal coordinate
  { uint16_t sum = a + 0x4 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[(uint8_t)(Enemy_X_Position + x)] = a;
  a = ram[(uint8_t)(Enemy_PageLoc + x)];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location
  ram[(uint8_t)(Enemy_PageLoc + x)] = a;
  TallBBox2(); return; // set bounding box control (not used) and leave
}

// --------------------------------
// $00-$01 - used to hold pseudorandom bits
// in: x; out: none
void InitFlyingCheepCheep(void) {
  a = ram[FrenzyEnemyTimer]; zero_flag = a == 0; // if timer here not expired yet, branch to leave
  if (!zero_flag) {
    return;
  }
  CALL(SmallBBox, 0xc4af); // jump to set bounding box size $09 and init other values
  a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff];
  a &= 0b00000011; // set pseudorandom offset here
  y = a;
  ram[FrenzyEnemyTimer] = ROM(FlyCCTimerData + y); // load timer with pseudorandom offset
  y = 0x3; // load Y with default value
  a = ram[SecondaryHardMode]; zero_flag = a == 0;
  // Original branch: if secondary hard mode flag not set, do not increment Y
  if (!zero_flag) {
    y++; // otherwise, increment Y to allow as many as four onscreen
  }
  // MaxCC:
  ram[0x0] = y; // store whatever pseudorandom bits are in Y
  carry_flag = x >= ram[0x0]; // compare enemy object buffer offset with Y
  // if X => Y, branch to leave
  if (carry_flag) {
    return;
  }
  a = ram[(PseudoRandomBitReg + x) & 0x7ff];
  a &= 0b00000011; // get last two bits of LSFR, first part
  ram[0x0] = a; // and store in two places
  ram[0x1] = a;
  ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xfb; // set vertical speed for cheep-cheep
  a = 0x0; // load default value
  y = ram[Player_X_Speed]; zero_flag = y == 0; // check player's horizontal speed
  // Original branch: if player not moving left or right, skip this part
  if (!zero_flag) {
    a = 0x4;
    carry_flag = y >= 0x19; // if moving to the right but not very quickly,
    if (!carry_flag) { goto GSeed; } // do not change A
    a = (uint8_t)(a << 1); // otherwise, multiply A by 2
  }
  
GSeed:
  ram[0x100 + sp--] = a; // save to stack
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag); // add to last two bits of LSFR we saved earlier
  ram[0x0] = a; // save it there
  a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff];
  a &= 0b00000011; zero_flag = a == 0; // if neither of the last two bits of second LSFR set,
  // Original branch: skip this part and save contents of $00
  if (!zero_flag) {
    a = ram[((PseudoRandomBitReg + 2) + x) & 0x7ff];
    a &= 0b00001111; // otherwise overwrite with lower nybble of
    ram[0x0] = a; // third LSFR part
  }
  // RSeed:
  a = ram[0x100 + ++sp]; // get value from stack we saved earlier
  carry_flag = false;
  a = (uint8_t)(a + ram[0x1] + carry_flag); // add to last two bits of LSFR we saved in other place
  y = a; // use as pseudorandom offset here
  ram[(uint8_t)(Enemy_X_Speed + x)] = ROM(FlyCCXSpeedData + y); // get horizontal speed using pseudorandom offset
  ram[(uint8_t)(Enemy_MovingDir + x)] = 0x1; // set to move towards the right
  a = ram[Player_X_Speed]; zero_flag = a == 0; // if player moving left or right, branch ahead of this part
  if (zero_flag) {
    y = ram[0x0]; // get first LSFR or third LSFR lower nybble
    a = y; // and check for d1 set
    a &= 0b00000010; zero_flag = a == 0;
    if (zero_flag) { goto D2XPos1; } // if d1 not set, branch
    a = ram[(uint8_t)(Enemy_X_Speed + x)];
    a ^= 0xff; // if d1 set, change horizontal speed
    carry_flag = false; // into two's compliment, thus moving in the opposite
    a = (uint8_t)(a + 0x1 + carry_flag); // direction
    ram[(uint8_t)(Enemy_X_Speed + x)] = a;
    ram[(uint8_t)(Enemy_MovingDir + x)]++; // increment to move towards the left
  }
  
D2XPos1:
  a = y; // get first LSFR or third LSFR lower nybble again
  a &= 0b00000010; zero_flag = a == 0;
  // Original branch: check for d1 set again, branch again if not set
  if (!zero_flag) {
    a = ram[Player_X_Position]; // get player's horizontal position
    carry_flag = false;
    { uint16_t sum = a + ROM(FlyCCXPositionData + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // if d1 set, add value obtained from pseudorandom offset
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // and save as enemy's horizontal position
    a = ram[Player_PageLoc]; // get player's page location
    a = (uint8_t)(a + 0x0 + carry_flag); // add carry and jump past this part
  } else {
    // D2XPos2:
    a = ram[Player_X_Position]; // get player's horizontal position
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ROM(FlyCCXPositionData + y) - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // if d1 not set, subtract value obtained from pseudorandom
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // offset and save as enemy's horizontal position
    a = ram[Player_PageLoc]; // get player's page location
    a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow
  }
  // FinCCSt:
  ram[(uint8_t)(Enemy_PageLoc + x)] = a; // save as enemy's page location
  a = 0x1;
  ram[(uint8_t)(Enemy_Flag + x)] = a; // set enemy's buffer flag
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // set enemy's high vertical byte
  ram[(uint8_t)(Enemy_Y_Position + x)] = 0xf8; // put enemy below the screen, and we are done
  return;
}

// --------------------------------
// in: x; out: none
void InitBowser(void) {
  CALL(DuplicateEnemyObj, 0xc54b); // jump to create another bowser object
  ram[BowserFront_Offset] = x; // save offset of first here
  a = 0x0;
  ram[BowserBodyControls] = a; // initialize bowser's body controls
  ram[BridgeCollapseOffset] = a; // and bridge collapse offset
  ram[BowserOrigXPos] = ram[(uint8_t)(Enemy_X_Position + x)]; // store original horizontal position here
  a = 0xdf;
  ram[BowserFireBreathTimer] = a; // store something here
  ram[(uint8_t)(Enemy_MovingDir + x)] = a; // and in moving direction
  a = 0x20;
  ram[BowserFeetCounter] = a; // set bowser's feet timer and in enemy timer
  ram[(EnemyFrameTimer + x) & 0x7ff] = a;
  a = 0x5;
  ram[BowserHitPoints] = a; // give bowser 5 hit points
  a = a >> 1;
  ram[BowserMovementSpeed] = a; // set default movement speed here
  return;
}

// --------------------------------
// in: x; out: none
void DuplicateEnemyObj(void) {
  y = 0xff; // start at beginning of enemy slots
  do {
    // FSLoop:
    y++; // increment one slot
    a = ram[Enemy_Flag + y]; zero_flag = a == 0; // check enemy buffer flag for empty slot
  } while (!zero_flag); // if set, branch and keep checking
  ram[DuplicateObj_Offset] = y; // otherwise set offset here
  a = x; // transfer original enemy buffer offset
  a |= 0b10000000; // store with d7 set as flag in new enemy
  ram[Enemy_Flag + y] = a; // slot as well as enemy offset
  ram[Enemy_PageLoc + y] = ram[(uint8_t)(Enemy_PageLoc + x)]; // copy page location and horizontal coordinates
  ram[Enemy_X_Position + y] = ram[(uint8_t)(Enemy_X_Position + x)]; // from original enemy to new enemy
  a = 0x1;
  ram[(uint8_t)(Enemy_Flag + x)] = a; // set flag as normal for original enemy
  ram[Enemy_Y_HighPos + y] = a; // set high vertical byte for new enemy
  ram[Enemy_Y_Position + y] = ram[(uint8_t)(Enemy_Y_Position + x)]; // copy vertical coordinate from original to new
  // FlmEx:
  return; // and then leave
}

// --------------------------------
// in: x; out: none
void InitBowserFlame(void) {
  a = ram[FrenzyEnemyTimer]; zero_flag = a == 0; // if timer not expired yet, branch to leave
  if (!zero_flag) {
    return;
  }
  ram[Enemy_Y_MoveForce + x] = a; // reset something here
  a = ram[NoiseSoundQueue];
  a |= Sfx_BowserFlame; // load bowser's flame sound into queue
  ram[NoiseSoundQueue] = a;
  y = ram[BowserFront_Offset]; // get bowser's buffer offset
  a = ram[Enemy_ID + y]; // check for bowser
  // Original branch: branch if found
  if (a != Bowser) {
    CALL(SetFlameTimer, 0xc5bd); // get timer data based on flame counter
    carry_flag = false;
    a = (uint8_t)(a + 0x20 + carry_flag); // add 32 frames by default
    y = ram[SecondaryHardMode]; zero_flag = y == 0;
    // Original branch: if secondary mode flag not set, use as timer setting
    if (!zero_flag) {
      carry_flag = true;
      a = (uint8_t)(a - 0x10 - !carry_flag); // otherwise subtract 16 frames for secondary hard mode
    }
    // SetFrT:
    ram[FrenzyEnemyTimer] = a; // set timer accordingly
    a = ram[(PseudoRandomBitReg + x) & 0x7ff];
    a &= 0b00000011; // get 2 LSB from first part of LSFR
    ram[BowserFlamePRandomOfs + x] = a; // set here
    y = a; // use as offset
    a = ROM(FlameYPosData + y); // load vertical position based on pseudorandom offset
    PutAtRightExtent(); // fallthrough
    return;
  }
  // SpawnFromMouth:
  a = ram[Enemy_X_Position + y]; // get bowser's horizontal position
  carry_flag = true;
  a = (uint8_t)(a - 0xe - !carry_flag); // subtract 14 pixels
  ram[(uint8_t)(Enemy_X_Position + x)] = a; // save as flame's horizontal position
  ram[(uint8_t)(Enemy_PageLoc + x)] = ram[Enemy_PageLoc + y]; // copy page location from bowser to flame
  a = ram[Enemy_Y_Position + y];
  carry_flag = false; // add 8 pixels to bowser's vertical position
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[(uint8_t)(Enemy_Y_Position + x)] = a; // save as flame's vertical position
  a = ram[(PseudoRandomBitReg + x) & 0x7ff];
  a &= 0b00000011; // get 2 LSB from first part of LSFR
  ram[Enemy_YMF_Dummy + x] = a; // save here
  y = a; // use as offset
  a = ROM(FlameYPosData + y); // get value here using bits as offset
  y = 0x0; // load default offset
  carry_flag = a >= ram[(uint8_t)(Enemy_Y_Position + x)]; // compare value to flame's current vertical position
  // Original branch: if less, do not increment offset
  if (carry_flag) {
    y++; // otherwise increment now
  }
  // SetMF:
  ram[Enemy_Y_MoveForce + x] = ROM(FlameYMFAdderData + y); // get value here and save; to vertical movement force
  ram[EnemyFrenzyBuffer] = 0x0; // clear enemy frenzy buffer
  FinishFlame(); // fallthrough
  return;
}

// in: a, x; out: a
void PutAtRightExtent(void) {
  ram[(uint8_t)(Enemy_Y_Position + x)] = a; // set vertical position
  a = ram[ScreenRight_X_Pos];
  carry_flag = false;
  { uint16_t sum = a + 0x20 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // place enemy 32 pixels beyond right side of screen
  ram[(uint8_t)(Enemy_X_Position + x)] = a;
  a = ram[ScreenRight_PageLoc];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry
  ram[(uint8_t)(Enemy_PageLoc + x)] = a;
  FinishFlame(); return; // skip this part to finish setting values
}

// in: x; out: a
void FinishFlame(void) {
  ram[Enemy_BoundBoxCtrl + x] = 0x8; // set $08 for bounding box control
  a = 0x1; // set high byte of vertical and
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // enemy buffer flag
  ram[(uint8_t)(Enemy_Flag + x)] = a;
  a = a >> 1;
  ram[Enemy_X_MoveForce + x] = a; // initialize horizontal movement force, and
  ram[(uint8_t)(Enemy_State + x)] = a; // enemy state
  return;
}

// --------------------------------
// in: x; out: none
void InitFireworks(void) {
  a = ram[FrenzyEnemyTimer]; zero_flag = a == 0; // if timer not expired yet, branch to leave
  if (!zero_flag) {
    return;
  }
  ram[FrenzyEnemyTimer] = 0x20; // otherwise reset timer
  ram[FireworksCounter]--; // decrement for each explosion
  y = 0x6; // start at last slot
  do {
    // StarFChk:
    y--;
    a = ram[Enemy_ID + y]; // check for presence of star flag object
    zero_flag = a == StarFlagObject; // if there isn't a star flag object,
  } while (!zero_flag); // routine goes into infinite loop = crash
  a = ram[Enemy_X_Position + y];
  carry_flag = true; // get horizontal coordinate of star flag object, then
  { uint16_t diff = (uint16_t)(a - 0x30 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract 48 pixels from it and save to
  ram[0x100 + sp--] = a; // the stack
  a = ram[Enemy_PageLoc + y];
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract the carry from the page location
  ram[0x0] = a; // of the star flag object
  a = ram[FireworksCounter]; // get fireworks counter
  carry_flag = false;
  a = (uint8_t)(a + ram[Enemy_State + y] + carry_flag); // add state of star flag object (possibly not necessary)
  y = a; // use as offset
  a = ram[0x100 + ++sp]; // get saved horizontal coordinate of star flag - 48 pixels
  carry_flag = false;
  { uint16_t sum = a + ROM(FireworksXPosData + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add number based on offset of fireworks counter
  ram[(uint8_t)(Enemy_X_Position + x)] = a; // store as the fireworks object horizontal coordinate
  a = ram[0x0];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry and store as page location for
  ram[(uint8_t)(Enemy_PageLoc + x)] = a; // the fireworks object
  ram[(uint8_t)(Enemy_Y_Position + x)] = ROM(FireworksYPosData + y); // get vertical position using same offset; and store as vertical coordinate for fireworks object
  a = 0x1;
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // store in vertical high byte
  ram[(uint8_t)(Enemy_Flag + x)] = a; // and activate enemy buffer flag
  a = a >> 1;
  ram[(uint8_t)(ExplosionGfxCounter + x)] = a; // initialize explosion counter
  ram[(uint8_t)(ExplosionTimerCounter + x)] = 0x8; // set explosion timing counter
  // ExitFWk:
  return;
}

// --------------------------------
// in: x; out: x
void BulletBillCheepCheep(void) {
  a = ram[FrenzyEnemyTimer]; zero_flag = a == 0; // if timer not expired yet, branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[AreaType]; zero_flag = a == 0; // are we in a water-type level?
  if (!zero_flag) { goto DoBulletBills; } // if not, branch elsewhere
  // are we past third enemy slot?
  // if so, branch to leave
  if (x >= 0x3) {
    return;
  }
  y = 0x0; // load default offset
  a = ram[(PseudoRandomBitReg + x) & 0x7ff];
  // check first part of LSFR against preset value
  // Original branch: if less than preset, do not increment offset
  if (a >= 0xaa) {
    y++; // otherwise increment
  }
  // ChkW2:
  a = ram[WorldNumber]; // check world number
  // Original branch: if we're on world 2, do not increment offset
  if (a != World2) {
    y++; // otherwise increment
  }
  // Get17ID:
  a = y;
  a &= 0b00000001; // mask out all but last bit of offset
  y = a;
  a = ROM(SwimCC_IDData + y); // load identifier for cheep-cheeps
  
Set17ID:
  ram[(uint8_t)(Enemy_ID + x)] = a; // store whatever's in A as enemy identifier
  a = ram[BitMFilter];
  // if not all bits set, skip init part and compare bits
  if (a == 0xff) {
    ram[BitMFilter] = 0x0; // initialize vertical position filter
  }
  // GetRBit:
  a = ram[(PseudoRandomBitReg + x) & 0x7ff]; // get first part of LSFR
  a &= 0b00000111; // mask out all but 3 LSB
  
ChkRBit:
  y = a; // use as offset
  a = ROM(Bitmasks + y); // load bitmask
  zero_flag = (a & ram[BitMFilter]) == 0; // perform AND on filter without changing it
  if (!zero_flag) {
    y++; // increment offset
    a = y;
    a &= 0b00000111; // mask out all but 3 LSB thus keeping it 0-7
    goto ChkRBit; // do another check
  }
  // AddFBit:
  a |= ram[BitMFilter]; // add bit to already set bits in filter
  ram[BitMFilter] = a; // and store
  a = ROM(Enemy17YPosData + y); // load vertical position using offset
  CALL(PutAtRightExtent, 0xc6f1); // set vertical position and other values
  ram[Enemy_YMF_Dummy + x] = a; // initialize dummy variable
  ram[FrenzyEnemyTimer] = 0x20; // set timer
  CheckpointEnemyID(); return; // process our new enemy object
  
DoBulletBills:
  y = 0xff; // start at beginning of enemy slots
  do {
    
BB_SLoop:
    y++; // move onto the next slot
    carry_flag = y >= 0x5; // branch to play sound if we've done all slots
    if (carry_flag) { goto FireBulletBill; }
    a = ram[Enemy_Flag + y]; zero_flag = a == 0; // if enemy buffer flag not set,
    if (zero_flag) { goto BB_SLoop; } // loop back and check another slot
    a = ram[Enemy_ID + y];
    zero_flag = a == BulletBill_FrenzyVar; // check enemy identifier for
  } while (!zero_flag); // bullet bill object (frenzy variant)
  // ExF17:
  return; // if found, leave
  
FireBulletBill:
  a = ram[Square2SoundQueue];
  a |= Sfx_Blast; // play fireworks/gunfire sound
  ram[Square2SoundQueue] = a;
  a = BulletBill_FrenzyVar; // load identifier for bullet bill object
  goto Set17ID; // unconditional branch
}

// --------------------------------
// in: x; out: none
void InitPiranhaPlant(void) {
  a = 0x1; // set initial speed
  ram[(uint8_t)(PiranhaPlant_Y_Speed + x)] = a;
  a = a >> 1;
  ram[(uint8_t)(Enemy_State + x)] = a; // initialize enemy state and what would normally
  ram[(uint8_t)(PiranhaPlant_MoveFlag + x)] = a; // be used as vertical speed, but not in this case
  a = ram[(uint8_t)(Enemy_Y_Position + x)];
  ram[PiranhaPlantDownYPos + x] = a; // save original vertical coordinate here
  carry_flag = true;
  a = (uint8_t)(a - 0x18 - !carry_flag);
  ram[PiranhaPlantUpYPos + x] = a; // save original vertical coordinate - 24 pixels here
  a = 0x9;
  SetBBox2(); return; // set specific value for bounding box control
}

// --------------------------------
// in: x; out: x
void InitEnemyFrenzy(void) {
  a = ram[(uint8_t)(Enemy_ID + x)]; // load enemy identifier
  ram[EnemyFrenzyBuffer] = a; // save in enemy frenzy buffer
  carry_flag = true;
  a = (uint8_t)(a - 0x12 - !carry_flag); // subtract 12 and use as offset for jump engine
  switch (JumpEngine(0xc7aa)) {
    case 0xc3a4: LakituAndSpinyHandler(); return;
    case 0xc7b7: NoFrenzyCode(); return;
    case 0xc4a8: InitFlyingCheepCheep(); return;
    case 0xc5a3: InitBowserFlame(); return;
    case 0xc63d: InitFireworks(); return;
    case 0xc69c: BulletBillCheepCheep(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void NoFrenzyCode(void) {
  return;
}

// --------------------------------
// in: x; out: none
void EndFrenzy(void) {
  y = 0x5; // start at last slot
  do {
    // LakituChk:
    a = ram[Enemy_ID + y]; // check enemy identifiers
    // for lakitu
    if (a == Lakitu) {
      ram[Enemy_State + y] = 0x1; // if found, set state
    }
    // NextFSlot:
    y--; neg_flag = (y & 0x80) != 0; // move onto the next slot
  } while (!neg_flag); // do this until all slots are checked
  a = 0x0;
  ram[EnemyFrenzyBuffer] = a; // empty enemy frenzy buffer
  ram[(uint8_t)(Enemy_Flag + x)] = a; // disable enemy buffer flag for this object
  return;
}

// --------------------------------
// in: x; out: none
void InitJumpGPTroopa(void) {
  ram[(uint8_t)(Enemy_MovingDir + x)] = 0x2; // set for movement to the left
  ram[(uint8_t)(Enemy_X_Speed + x)] = 0xf8; // set horizontal speed
  TallBBox2(); // fallthrough
  return;
}

// in: x; out: none
void TallBBox2(void) {
  a = 0x3; // set specific value for bounding box control
  SetBBox2(); // fallthrough
  return;
}

// in: a, x; out: none
void SetBBox2(void) {
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box control then leave
  return;
}

// --------------------------------
// in: x; out: none
void InitBalPlatform(void) {
  ram[(uint8_t)(Enemy_Y_Position + x)]--; // raise vertical position by two pixels
  ram[(uint8_t)(Enemy_Y_Position + x)]--;
  y = ram[SecondaryHardMode]; zero_flag = y == 0; // if secondary hard mode flag not set,
  // Original branch: branch ahead
  if (zero_flag) {
    y = 0x2; // otherwise set value here
    CALL(PosPlatform, 0xc7ec); // do a sub to add or subtract pixels
  }
  // AlignP:
  y = 0xff; // set default value here for now
  a = ram[BalPlatformAlignment]; neg_flag = (a & 0x80) != 0; // get current balance platform alignment
  ram[(uint8_t)(Enemy_State + x)] = a; // set platform alignment to object state here
  // Original branch: if old alignment $ff, put $ff as alignment for negative
  if (neg_flag) {
    a = x; // if old contents already $ff, put
    y = a; // object offset as alignment to make next positive
  }
  // SetBPA:
  ram[BalPlatformAlignment] = y; // store whatever value's in Y here
  a = 0x0;
  ram[(uint8_t)(Enemy_MovingDir + x)] = a; // init moving direction
  y = a; // init Y
  CALL(PosPlatform, 0xc802); // do a sub to add 8 pixels, then run shared code here
  InitDropPlatform(); // fallthrough
  return;
}

// --------------------------------
// in: x; out: none
void InitDropPlatform(void) {
  ram[PlatformCollisionFlag + x] = 0xff; // set some value here
  CommonPlatCode(); return; // then jump ahead to execute more code
}

// --------------------------------
// in: x; out: none
void InitHoriPlatform(void) {
  ram[(uint8_t)(XMoveSecondaryCounter + x)] = 0x0; // init one of the moving counters
  CommonPlatCode(); return; // jump ahead to execute more code
}

// --------------------------------
// in: x; out: none
void InitVertPlatform(void) {
  y = 0x40; // set default value here
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; neg_flag = (a & 0x80) != 0; // check vertical position
  // Original branch: if above a certain point, skip this part
  if (neg_flag) {
    a ^= 0xff;
    carry_flag = false; // otherwise get two's compliment
    a = (uint8_t)(a + 0x1 + carry_flag);
    y = 0xc0; // get alternate value to add to vertical position
  }
  // SetYO:
  ram[YPlatformTopYPos + x] = a; // save as top vertical position
  a = y;
  carry_flag = false; // load value from earlier, add number of pixels 
  a = (uint8_t)(a + ram[(uint8_t)(Enemy_Y_Position + x)] + carry_flag); // to vertical position
  ram[(uint8_t)(YPlatformCenterYPos + x)] = a; // save result as central vertical position
  CommonPlatCode(); // fallthrough
  return;
}

// --------------------------------
// in: x; out: none
void CommonPlatCode(void) {
  CALL(InitVStf, 0xc82a); // do a sub to init certain other values 
  SPBBox(); // fallthrough
  return;
}

// in: x; out: none
void SPBBox(void) {
  a = 0x5; // set default bounding box size control
  y = ram[AreaType];
  // check for castle-type level
  // Original branch: use default value if found
  if (y != 0x3) {
    y = ram[SecondaryHardMode]; zero_flag = y == 0; // otherwise check for secondary hard mode flag
    if (!zero_flag) { goto CasPBB; } // if set, use default value
    a = 0x6; // use alternate value if not castle or secondary not set
  }
  
CasPBB:
  ram[Enemy_BoundBoxCtrl + x] = a; // set bounding box size control here and leave
  return;
}

// --------------------------------
// in: x; out: none
void LargeLiftUp(void) {
  CALL(PlatLiftUp, 0xc841); // execute code for platforms going up
  LargeLiftBBox(); return; // overwrite bounding box for large platforms
}

// in: x; out: none
void LargeLiftDown(void) {
  CALL(PlatLiftDown, 0xc847); // execute code for platforms going down
  LargeLiftBBox(); // fallthrough
  return;
}

// in: x; out: none
void LargeLiftBBox(void) {
  SPBBox(); return; // jump to overwrite bounding box size control
}

// --------------------------------
// in: x; out: none
void PlatLiftUp(void) {
  ram[Enemy_Y_MoveForce + x] = 0x10; // set movement amount here
  ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xff; // set moving speed for platforms going up
  CommonSmallLift(); return; // skip ahead to part we should be executing
}

// --------------------------------
// in: x; out: none
void PlatLiftDown(void) {
  ram[Enemy_Y_MoveForce + x] = 0xf0; // set movement amount here
  ram[(uint8_t)(Enemy_Y_Speed + x)] = 0x0; // set moving speed for platforms going down
  CommonSmallLift(); // fallthrough
  return;
}

// --------------------------------
// in: x; out: none
void CommonSmallLift(void) {
  y = 0x1;
  CALL(PosPlatform, 0xc864); // do a sub to add 12 pixels due to preset value  
  ram[Enemy_BoundBoxCtrl + x] = 0x4; // set bounding box control for small platforms
  return;
}

// --------------------------------
// in: x, y; out: none
void PosPlatform(void) {
  a = ram[(uint8_t)(Enemy_X_Position + x)]; // get horizontal coordinate
  carry_flag = false;
  { uint16_t sum = a + ROM(PlatPosDataLow + y) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add or subtract pixels depending on offset
  ram[(uint8_t)(Enemy_X_Position + x)] = a; // store as new horizontal coordinate
  a = ram[(uint8_t)(Enemy_PageLoc + x)];
  a = (uint8_t)(a + ROM(PlatPosDataHigh + y) + carry_flag); // add or subtract page location depending on offset
  ram[(uint8_t)(Enemy_PageLoc + x)] = a; // store as new page location
  return; // and go back
}

// --------------------------------
void EndOfEnemyInitCode(void) {
  return;
}

void NoRunCode(void) {
  return;
}

// --------------------------------
// in: x; out: x
void RunRetainerObj(void) {
  CALL(GetEnemyOffscreenBits, 0xc8d9);
  CALL(RelativeEnemyPosition, 0xc8dc);
  EnemyGfxHandler(); return;
}

// --------------------------------
// in: x; out: x
void RunNormalEnemies(void) {
  ram[Enemy_SprAttrib + x] = 0x0; // init sprite attributes
  CALL(GetEnemyOffscreenBits, 0xc8e7);
  CALL(RelativeEnemyPosition, 0xc8ea);
  CALL(EnemyGfxHandler, 0xc8ed);
  CALL(GetEnemyBoundBox, 0xc8f0);
  CALL(EnemyToBGCollisionDet, 0xc8f3);
  CALL(EnemiesCollision, 0xc8f6);
  CALL(PlayerEnemyCollision, 0xc8f9);
  y = ram[TimerControl]; zero_flag = y == 0; // if master timer control set, skip to last routine
  if (zero_flag) {
    CALL(EnemyMovementSubs, 0xc901);
  }
  // SkipMove:
  OffscreenBoundsCheck(); return;
}

// in: x; out: x
void EnemyMovementSubs(void) {
  a = ram[(uint8_t)(Enemy_ID + x)];
  switch (JumpEngine(0xc909)) {
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
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

void NoMoveCode(void) {
  return;
}

// --------------------------------
// in: x; out: x
void RunBowserFlame(void) {
  CALL(ProcBowserFlame, 0xc937);
  CALL(GetEnemyOffscreenBits, 0xc93a);
  CALL(RelativeEnemyPosition, 0xc93d);
  CALL(GetEnemyBoundBox, 0xc940);
  CALL(PlayerEnemyCollision, 0xc943);
  OffscreenBoundsCheck(); return;
}

// --------------------------------
// in: x; out: x
void RunFirebarObj(void) {
  CALL(ProcFirebar, 0xc949);
  OffscreenBoundsCheck(); return;
}

// --------------------------------
// in: x; out: x
void RunSmallPlatform(void) {
  CALL(GetEnemyOffscreenBits, 0xc94f);
  CALL(RelativeEnemyPosition, 0xc952);
  CALL(SmallPlatformBoundBox, 0xc955);
  CALL(SmallPlatformCollision, 0xc958);
  CALL(RelativeEnemyPosition, 0xc95b);
  CALL(DrawSmallPlatform, 0xc95e);
  CALL(MoveSmallPlatform, 0xc961);
  OffscreenBoundsCheck(); return;
}

// --------------------------------
// in: x; out: x
void RunLargePlatform(void) {
  CALL(GetEnemyOffscreenBits, 0xc967);
  CALL(RelativeEnemyPosition, 0xc96a);
  CALL(LargePlatformBoundBox, 0xc96d);
  CALL(LargePlatformCollision, 0xc970);
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set,
  // Original branch: skip subroutine tree
  if (zero_flag) {
    CALL(LargePlatformSubroutines, 0xc978);
  }
  // SkipPT:
  CALL(RelativeEnemyPosition, 0xc97b);
  CALL(DrawLargePlatform, 0xc97e);
  OffscreenBoundsCheck(); return;
}

// --------------------------------
// in: x; out: x
void LargePlatformSubroutines(void) {
  a = ram[(uint8_t)(Enemy_ID + x)]; // subtract $24 to get proper offset for jump table
  carry_flag = true;
  a = (uint8_t)(a - 0x24 - !carry_flag);
  switch (JumpEngine(0xc989)) {
    case 0xd432: BalancePlatform(); return;
    case 0xd5d3: YMovingPlatform(); return;
    case 0xd64f: MoveLargeLiftPlat(); return;
    case 0xd607: XMovingPlatform(); return;
    case 0xd631: DropPlatform(); return;
    case 0xd63d: RightPlatform(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: a
void EraseEnemyObject(void) {
  a = 0x0; // clear all enemy object variables
  ram[(uint8_t)(Enemy_Flag + x)] = a;
  ram[(uint8_t)(Enemy_ID + x)] = a;
  ram[(uint8_t)(Enemy_State + x)] = a;
  ram[FloateyNum_Control + x] = a;
  ram[(EnemyIntervalTimer + x) & 0x7ff] = a;
  ram[ShellChainCounter + x] = a;
  ram[Enemy_SprAttrib + x] = a;
  ram[(EnemyFrameTimer + x) & 0x7ff] = a;
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void MovePodoboo(void) {
  a = ram[(EnemyIntervalTimer + x) & 0x7ff]; zero_flag = a == 0; // check enemy timer
  // Original branch: branch to move enemy if not expired
  if (zero_flag) {
    CALL(InitPodoboo, 0xc9b7); // otherwise set up podoboo again
    a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff]; // get part of LSFR
    a |= 0b10000000; // set d7
    ram[Enemy_Y_MoveForce + x] = a; // store as movement force
    a &= 0b00001111; // mask out high nybble
    a |= 0x6; // set for at least six intervals
    ram[(EnemyIntervalTimer + x) & 0x7ff] = a; // store as new enemy timer
    ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xf9; // set vertical speed to move podoboo upwards
  }
  // PdbM:
  MoveJ_EnemyVertically(); return; // branch to impose gravity on podoboo
}

// --------------------------------
// $00 - used in HammerBroJumpCode as bitmask
// in: x; out: x
void ProcHammerBro(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // check hammer bro's enemy state for d5 set
  a &= 0b00100000; zero_flag = a == 0;
  // Original branch: if not set, go ahead with code
  if (!zero_flag) {
    MoveDefeatedEnemy(); return; // otherwise jump to something else
  }
  // ChkJH:
  a = ram[(uint8_t)(HammerBroJumpTimer + x)]; zero_flag = a == 0; // check jump timer
  // Original branch: if expired, branch to jump
  if (!zero_flag) {
    ram[(uint8_t)(HammerBroJumpTimer + x)]--; // otherwise decrement jump timer
    a = ram[Enemy_OffscreenBits];
    a &= 0b00001100; zero_flag = a == 0; // check offscreen bits
    // if hammer bro a little offscreen, skip to movement code
    if (!zero_flag) {
      MoveHammerBroXDir();
      return;
    }
    a = ram[HammerThrowingTimer + x]; zero_flag = a == 0; // check hammer throwing timer
    // Original branch: if not expired, skip ahead, do not throw hammer
    if (zero_flag) {
      y = ram[SecondaryHardMode]; // otherwise get secondary hard mode flag
      ram[HammerThrowingTimer + x] = ROM(HammerThrowTmrData + y); // get timer data using flag as offset; set as new timer
      CALL(SpawnHammerObj, 0xc9fe); // do a sub here to spawn hammer object
      if (!carry_flag) { goto DecHT; } // if carry clear, hammer not spawned, skip to decrement timer
      a = ram[(uint8_t)(Enemy_State + x)];
      a |= 0b00001000; // set d3 in enemy state for hammer throw
      ram[(uint8_t)(Enemy_State + x)] = a;
      MoveHammerBroXDir(); return; // jump to move hammer bro
    }
    
DecHT:
    ram[HammerThrowingTimer + x]--; // decrement timer
    MoveHammerBroXDir(); return; // jump to move hammer bro
  }
  // HammerBroJumpCode:
  a = ram[(uint8_t)(Enemy_State + x)]; // get hammer bro's enemy state
  a &= 0b00000111; // mask out all but 3 LSB
  // check for d0 set (for jumping)
  // if set, branch ahead to moving code
  if (a == 0x1) {
    MoveHammerBroXDir();
    return;
  }
  ram[0x0] = 0x0; // load default value here; save into temp variable for now
  y = 0xfa; // set default vertical speed
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; neg_flag = (a & 0x80) != 0; // check hammer bro's vertical coordinate
  // if on the bottom half of the screen, use current speed
  if (neg_flag) {
    SetHJ();
    return;
  }
  y = 0xfd; // otherwise set alternate vertical speed
  carry_flag = a >= 0x70; // check to see if hammer bro is above the middle of screen
  ram[0x0]++; // increment preset value to $01
  // if above the middle of the screen, use current speed and $01
  if (!carry_flag) {
    SetHJ();
    return;
  }
  ram[0x0]--; // otherwise return value to $00
  a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff]; // get part of LSFR, mask out all but LSB
  a &= 0x1; zero_flag = a == 0;
  // if d0 of LSFR set, branch and use current speed and $00
  if (!zero_flag) {
    SetHJ();
    return;
  }
  y = 0xfa; // otherwise reset to default vertical speed
  SetHJ(); // fallthrough
  return;
}

// in: x, y; out: x, y
void SetHJ(void) {
  ram[(uint8_t)(Enemy_Y_Speed + x)] = y; // set vertical speed for jumping
  a = ram[(uint8_t)(Enemy_State + x)]; // set d0 in enemy state for jumping
  a |= 0x1;
  ram[(uint8_t)(Enemy_State + x)] = a;
  a = ram[0x0]; // load preset value here to use as bitmask
  a &= ram[((PseudoRandomBitReg + 2) + x) & 0x7ff]; // and do bit-wise comparison with part of LSFR
  y = a; // then use as offset
  a = ram[SecondaryHardMode]; zero_flag = a == 0; // check secondary hard mode flag
  if (zero_flag) {
    y = a; // if secondary hard mode flag clear, set offset to 0
  }
  // HJump:
  ram[(EnemyFrameTimer + x) & 0x7ff] = ROM(HammerBroJumpLData + y); // get jump length timer data using offset from before; save in enemy timer
  a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff];
  a |= 0b11000000; // get contents of part of LSFR, set d7 and d6, then
  ram[(uint8_t)(HammerBroJumpTimer + x)] = a; // store in jump timer
  MoveHammerBroXDir(); // fallthrough
  return;
}

// in: x; out: x, y
void MoveHammerBroXDir(void) {
  y = 0xfc; // move hammer bro a little to the left
  a = ram[FrameCounter];
  a &= 0b01000000; zero_flag = a == 0; // change hammer bro's direction every 64 frames
  if (zero_flag) {
    y = 0x4; // if d6 set in counter, move him a little to the right
  }
  // Shimmy:
  ram[(uint8_t)(Enemy_X_Speed + x)] = y; // store horizontal speed
  y = 0x1; // set to face right by default
  CALL(PlayerEnemyDiff, 0xca68); // get horizontal difference between player and hammer bro
  // Original branch: if enemy to the left of player, skip this part
  if (!neg_flag) {
    y++; // set to face left
    a = ram[(EnemyIntervalTimer + x) & 0x7ff]; zero_flag = a == 0; // check walking timer
    if (!zero_flag) { goto SetShim; } // if not yet expired, skip to set moving direction
    ram[(uint8_t)(Enemy_X_Speed + x)] = 0xf8; // otherwise, make the hammer bro walk left towards player
  }
  
SetShim:
  ram[(uint8_t)(Enemy_MovingDir + x)] = y; // set moving direction
  MoveNormalEnemy(); // fallthrough
  return;
}

// in: x; out: x, y
void MoveNormalEnemy(void) {
  y = 0x0; // init Y to leave horizontal movement as-is 
  a = ram[(uint8_t)(Enemy_State + x)];
  a &= 0b01000000; zero_flag = a == 0; // check enemy state for d6 set, if set skip
  // Original branch: to move enemy vertically, then horizontally if necessary
  if (zero_flag) {
    a = ram[(uint8_t)(Enemy_State + x)];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // check enemy state for d7 set
    if (carry_flag) { goto SteadM; } // if set, branch to move enemy horizontally
    a = ram[(uint8_t)(Enemy_State + x)];
    a &= 0b00100000; zero_flag = a == 0; // check enemy state for d5 set
    // if set, branch to move defeated enemy object
    if (!zero_flag) {
      MoveDefeatedEnemy();
      return;
    }
    a = ram[(uint8_t)(Enemy_State + x)];
    a &= 0b00000111; zero_flag = a == 0; // check d2-d0 of enemy state for any set bits
    if (zero_flag) { goto SteadM; } // if enemy in normal state, branch to move enemy horizontally
    zero_flag = a == 0x5;
    if (zero_flag) { goto FallE; } // if enemy in state used by spiny's egg, go ahead here
    carry_flag = a >= 0x3;
    if (carry_flag) { goto ReviveStunned; } // if enemy in states $03 or $04, skip ahead to yet another part
  }
  
FallE:
  CALL(MoveD_EnemyVertically, 0xca9a); // do a sub here to move enemy downwards
  y = 0x0;
  a = ram[(uint8_t)(Enemy_State + x)]; // check for enemy state $02
  // Original branch: if found, branch to move enemy horizontally
  if (a != 0x2) {
    a &= 0b01000000; zero_flag = a == 0; // check for d6 set
    if (zero_flag) { goto SteadM; } // if not set, branch to something else
    a = ram[(uint8_t)(Enemy_ID + x)];
    zero_flag = a == PowerUpObject; // check for power-up object
    if (zero_flag) { goto SteadM; }
  } else {
    // MEHor:
    MoveEnemyHorizontally(); return; // jump here to move enemy horizontally for <> $2e and d6 set
  }
  // SlowM:
  y = 0x1; // if branched here, increment Y to slow horizontal movement
  
SteadM:
  a = ram[(uint8_t)(Enemy_X_Speed + x)]; neg_flag = (a & 0x80) != 0; // get current horizontal speed
  ram[0x100 + sp--] = a; // save to stack
  // Original branch: if not moving or moving right, skip, leave Y alone
  if (neg_flag) {
    y++;
    y++; // otherwise increment Y to next data
  }
  // AddHS:
  carry_flag = false;
  a = (uint8_t)(a + ROM(XSpeedAdderData + y) + carry_flag); // add value here to slow enemy down if necessary
  ram[(uint8_t)(Enemy_X_Speed + x)] = a; // save as horizontal speed temporarily
  CALL(MoveEnemyHorizontally, 0xcac3); // then do a sub to move horizontally
  a = ram[0x100 + ++sp];
  ram[(uint8_t)(Enemy_X_Speed + x)] = a; // get old horizontal speed from stack and return to
  return; // original memory location, then leave
  
ReviveStunned:
  a = ram[(EnemyIntervalTimer + x) & 0x7ff]; zero_flag = a == 0; // if enemy timer not expired yet,
  // Original branch: skip ahead to something else
  if (zero_flag) {
    ram[(uint8_t)(Enemy_State + x)] = a; // otherwise initialize enemy state to normal
    a = ram[FrameCounter];
    a &= 0x1; // get d0 of frame counter
    y = a; // use as Y and increment for movement direction
    y++;
    ram[(uint8_t)(Enemy_MovingDir + x)] = y; // store as pseudorandom movement direction
    y--; // decrement for use as pointer
    a = ram[PrimaryHardMode]; zero_flag = a == 0; // check primary hard mode flag
    // Original branch: if not set, use pointer as-is
    if (!zero_flag) {
      y++;
      y++; // otherwise increment 2 bytes to next data
    }
    // SetRSpd:
    ram[(uint8_t)(Enemy_X_Speed + x)] = ROM(RevivedXSpeed + y); // load and store new horizontal speed; and leave
    return;
  }
  // ChkKillGoomba:
  // check to see if enemy timer has reached
  // a certain point, and branch to leave if not
  if (a != 0xe) {
    return;
  }
  a = ram[(uint8_t)(Enemy_ID + x)];
  // check for goomba object
  // branch if not found
  if (a != Goomba) {
    return;
  }
  CALL(EraseEnemyObject, 0xcaf7); // otherwise, kill this goomba object
  // NKGmba:
  return; // leave!
}

// in: x; out: x, y
void MoveDefeatedEnemy(void) {
  CALL(MoveD_EnemyVertically, 0xcae7); // execute sub to move defeated enemy downwards
  MoveEnemyHorizontally(); return; // now move defeated enemy horizontally
}

// --------------------------------
// in: x; out: x
void MoveJumpingEnemy(void) {
  CALL(MoveJ_EnemyVertically, 0xcafb); // do a sub to impose gravity on green paratroopa
  MoveEnemyHorizontally(); return; // jump to move enemy horizontally
}

// in: x; out: x
void ProcMoveRedPTroopa(void) {
  goto ProcMoveRedPTroopa;
  // --------------------------------
  
MoveRedPTroopaDown:
  y = 0x0; // set Y to move downwards
  goto MoveRedPTroopa; // skip to movement routine
  
MoveRedPTroopaUp:
  y = 0x1; // set Y to move upwards
  
MoveRedPTroopa:
  x++; // increment X for enemy offset
  ram[0x0] = 0x3; // set downward movement amount here
  ram[0x1] = 0x6; // set upward movement amount here
  ram[0x2] = 0x2; // set maximum speed here
  a = y; // set movement direction in A, and
  RedPTroopaGrav(); return; // jump to move this thing
  // --------------------------------
  
ProcMoveRedPTroopa:
  a = ram[(uint8_t)(Enemy_Y_Speed + x)];
  a |= ram[Enemy_Y_MoveForce + x]; zero_flag = a == 0; // check for any vertical force or speed
  // Original branch: branch if any found
  if (zero_flag) {
    ram[Enemy_YMF_Dummy + x] = a; // initialize something here
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // check current vs. original vertical coordinate
    carry_flag = a >= ram[RedPTroopaOrigXPos + x];
    if (carry_flag) { goto MoveRedPTUpOrDown; } // if current => original, skip ahead to more code
    a = ram[FrameCounter]; // get frame counter
    a &= 0b00000111; zero_flag = a == 0; // mask out all but 3 LSB
    // if any bits set, branch to leave
    if (!zero_flag) {
      return;
    }
    ram[(uint8_t)(Enemy_Y_Position + x)]++; // otherwise increment red paratroopa's vertical position
    // NoIncPT:
    return; // leave
  }
  
MoveRedPTUpOrDown:
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // check current vs. central vertical coordinate
  carry_flag = a >= ram[(uint8_t)(RedPTroopaCenterYPos + x)];
  // Original branch: if current < central, jump to move downwards
  if (carry_flag) {
    goto MoveRedPTroopaUp; // otherwise jump to move upwards
  }
  // MovPTDwn:
  goto MoveRedPTroopaDown; // move downwards
}

// --------------------------------
// $00 - used to store adder for movement, also used as adder for platform
// $01 - used to store maximum value for secondary counter
// in: x; out: x
void MoveFlyGreenPTroopa(void) {
  CALL(XMoveCntr_GreenPTroopa, 0xcb27); // do sub to increment primary and secondary counters
  CALL(MoveWithXMCntrs, 0xcb2a); // do sub to move green paratroopa accordingly, and horizontally
  y = 0x1; // set Y to move green paratroopa down
  a = ram[FrameCounter];
  a &= 0b00000011; zero_flag = a == 0; // check frame counter 2 LSB for any bits set
  // branch to leave if set to move up/down every fourth frame
  if (!zero_flag) {
    return;
  }
  a = ram[FrameCounter];
  a &= 0b01000000; zero_flag = a == 0; // check frame counter for d6 set
  // Original branch: branch to move green paratroopa down if set
  if (zero_flag) {
    y = 0xff; // otherwise set Y to move green paratroopa up
  }
  // YSway:
  ram[0x0] = y; // store adder here
  a = ram[(uint8_t)(Enemy_Y_Position + x)];
  carry_flag = false; // add or subtract from vertical position
  a = (uint8_t)(a + ram[0x0] + carry_flag); // to give green paratroopa a wavy flight
  ram[(uint8_t)(Enemy_Y_Position + x)] = a;
  // NoMGPT:
  return; // leave!
}

// in: x; out: none
void XMoveCntr_GreenPTroopa(void) {
  a = 0x13; // load preset maximum value for secondary counter
  XMoveCntr_Platform(); // fallthrough
  return;
}

// in: a, x; out: none
void XMoveCntr_Platform(void) {
  ram[0x1] = a; // store value here
  a = ram[FrameCounter];
  a &= 0b00000011; zero_flag = a == 0; // branch to leave if not on
  // every fourth frame
  if (!zero_flag) {
    return;
  }
  y = ram[(uint8_t)(XMoveSecondaryCounter + x)]; // get secondary counter
  a = ram[(uint8_t)(XMovePrimaryCounter + x)]; // get primary counter
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto DecSeXM; } // if d0 of primary counter set, branch elsewhere
  zero_flag = y == ram[0x1]; // compare secondary counter to preset maximum value
  // Original branch: if equal, branch ahead of this part
  if (!zero_flag) {
    ram[(uint8_t)(XMoveSecondaryCounter + x)]++; // increment secondary counter and leave
    // NoIncXM:
    return;
  }
  
IncPXM:
  ram[(uint8_t)(XMovePrimaryCounter + x)]++; // increment primary counter and leave
  return;
  
DecSeXM:
  a = y; zero_flag = a == 0; // put secondary counter in A
  if (zero_flag) { goto IncPXM; } // if secondary counter at zero, branch back
  ram[(uint8_t)(XMoveSecondaryCounter + x)]--; // otherwise decrement secondary counter and leave
  return;
}

// in: x; out: x
void MoveWithXMCntrs(void) {
  a = ram[(uint8_t)(XMoveSecondaryCounter + x)]; // save secondary counter to stack
  ram[0x100 + sp--] = a;
  y = 0x1; // set value here by default
  a = ram[(uint8_t)(XMovePrimaryCounter + x)];
  a &= 0b00000010; zero_flag = a == 0; // if d1 of primary counter is
  // Original branch: set, branch ahead of this part here
  if (zero_flag) {
    a = ram[(uint8_t)(XMoveSecondaryCounter + x)];
    a ^= 0xff; // otherwise change secondary
    carry_flag = false; // counter to two's compliment
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(XMoveSecondaryCounter + x)] = a;
    y = 0x2; // load alternate value here
  }
  // XMRight:
  ram[(uint8_t)(Enemy_MovingDir + x)] = y; // store as moving direction
  CALL(MoveEnemyHorizontally, 0xcb80);
  ram[0x0] = a; // save value obtained from sub here
  a = ram[0x100 + ++sp]; // get secondary counter from stack
  ram[(uint8_t)(XMoveSecondaryCounter + x)] = a; // and return to original place
  return;
}

// --------------------------------
// in: x; out: x
void MoveBloober(void) {
  a = ram[(uint8_t)(Enemy_State + x)];
  a &= 0b00100000; zero_flag = a == 0; // check enemy state for d5 set
  // Original branch: branch if set to move defeated bloober
  if (zero_flag) {
    y = ram[SecondaryHardMode]; // use secondary hard mode flag as offset
    a = ram[((PseudoRandomBitReg + 1) + x) & 0x7ff]; // get LSFR
    a &= ROM(BlooberBitmasks + y); zero_flag = a == 0; // mask out bits in LSFR using bitmask loaded with offset
    // Original branch: if any bits set, skip ahead to make swim
    if (zero_flag) {
      a = x;
      carry_flag = (a & 1) != 0; a = a >> 1; // check to see if on second or fourth slot (1 or 3)
      // Original branch: if not, branch to figure out moving direction
      if (carry_flag) {
        y = ram[Player_MovingDir]; // otherwise, load player's moving direction and
      } else {
        // FBLeft:
        y = 0x2; // set left moving direction by default
        CALL(PlayerEnemyDiff, 0xcba6); // get horizontal difference between player and bloober
        if (!neg_flag) { goto SBMDir; } // if enemy to the right of player, keep left
        y--; // otherwise decrement to set right moving direction
      }
      
SBMDir:
      ram[(uint8_t)(Enemy_MovingDir + x)] = y; // set moving direction of bloober, then continue on here
    }
    // BlooberSwim:
    CALL(ProcSwimmingB, 0xcbae); // execute sub to make bloober swim characteristically
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
    carry_flag = true;
    a = (uint8_t)(a - ram[Enemy_Y_MoveForce + x] - !carry_flag); // subtract movement force
    // check to see if position is above edge of status bar
    // Original branch: if so, don't do it
    if (a >= 0x20) {
      ram[(uint8_t)(Enemy_Y_Position + x)] = a; // otherwise, set new vertical position, make bloober swim
    }
    // SwimX:
    y = ram[(uint8_t)(Enemy_MovingDir + x)]; // check moving direction
    y--; zero_flag = y == 0;
    // Original branch: if moving to the left, branch to second part
    if (zero_flag) {
      a = ram[(uint8_t)(Enemy_X_Position + x)];
      carry_flag = false; // add movement speed to horizontal coordinate
      { uint16_t sum = a + ram[(uint8_t)(BlooperMoveSpeed + x)] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[(uint8_t)(Enemy_X_Position + x)] = a; // store result as new horizontal coordinate
      a = ram[(uint8_t)(Enemy_PageLoc + x)];
      a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location
      ram[(uint8_t)(Enemy_PageLoc + x)] = a; // store as new page location and leave
      return;
    }
    // LeftSwim:
    a = ram[(uint8_t)(Enemy_X_Position + x)];
    carry_flag = true; // subtract movement speed from horizontal coordinate
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(BlooperMoveSpeed + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // store result as new horizontal coordinate
    a = ram[(uint8_t)(Enemy_PageLoc + x)];
    a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from page location
    ram[(uint8_t)(Enemy_PageLoc + x)] = a; // store as new page location and leave
    return;
  }
  // MoveDefeatedBloober:
  MoveEnemySlowVert(); return; // jump to move defeated bloober downwards
}

// in: x; out: none
void ProcSwimmingB(void) {
  a = ram[(uint8_t)(BlooperMoveCounter + x)]; // get enemy's movement counter
  a &= 0b00000010; zero_flag = a == 0; // check for d1 set
  // Original branch: branch if set
  if (zero_flag) {
    a = ram[FrameCounter];
    a &= 0b00000111; // get 3 LSB of frame counter
    ram[0x100 + sp--] = a; // and save it to the stack
    a = ram[(uint8_t)(BlooperMoveCounter + x)]; // get enemy's movement counter
    carry_flag = (a & 1) != 0; a = a >> 1; // check for d0 set
    // Original branch: branch if set
    if (!carry_flag) {
      a = ram[0x100 + ++sp]; zero_flag = a == 0; // pull 3 LSB of frame counter from the stack
      // branch to leave, execute code only every eighth frame
      if (!zero_flag) {
        return;
      }
      a = ram[Enemy_Y_MoveForce + x];
      carry_flag = false; // add to movement force to speed up swim
      a = (uint8_t)(a + 0x1 + carry_flag);
      ram[Enemy_Y_MoveForce + x] = a; // set movement force
      ram[(uint8_t)(BlooperMoveSpeed + x)] = a; // set as movement speed
      // if certain horizontal speed, branch to leave
      if (a != 0x2) {
        return;
      }
      ram[(uint8_t)(BlooperMoveCounter + x)]++; // otherwise increment movement counter
      // BSwimE:
      return;
    }
    // SlowSwim:
    a = ram[0x100 + ++sp]; zero_flag = a == 0; // pull 3 LSB of frame counter from the stack
    // branch to leave, execute code only every eighth frame
    if (!zero_flag) {
      return;
    }
    a = ram[Enemy_Y_MoveForce + x];
    carry_flag = true; // subtract from movement force to slow swim
    a = (uint8_t)(a - 0x1 - !carry_flag); zero_flag = a == 0;
    ram[Enemy_Y_MoveForce + x] = a; // set movement force
    ram[(uint8_t)(BlooperMoveSpeed + x)] = a; // set as movement speed
    // if any speed, branch to leave
    if (!zero_flag) {
      return;
    }
    ram[(uint8_t)(BlooperMoveCounter + x)]++; // otherwise increment movement counter
    ram[(EnemyIntervalTimer + x) & 0x7ff] = 0x2; // set enemy's timer
    // NoSSw:
    return; // leave
  }
  // ChkForFloatdown:
  a = ram[(EnemyIntervalTimer + x) & 0x7ff]; zero_flag = a == 0; // get enemy timer
  if (zero_flag) { goto ChkNearPlayer; } // branch if expired
  
Floatdown:
  a = ram[FrameCounter]; // get frame counter
  carry_flag = (a & 1) != 0; a = a >> 1; // check for d0 set
  // branch to leave on every other frame
  if (carry_flag) {
    return;
  }
  ram[(uint8_t)(Enemy_Y_Position + x)]++; // otherwise increment vertical coordinate
  // NoFD:
  return; // leave
  
ChkNearPlayer:
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
  a = (uint8_t)(a + 0x10 + carry_flag); // add sixteen pixels
  carry_flag = a >= ram[Player_Y_Position]; // compare result with player's vertical coordinate
  if (!carry_flag) { goto Floatdown; } // if modified vertical less than player's, branch
  ram[(uint8_t)(BlooperMoveCounter + x)] = 0x0; // otherwise nullify movement counter
  return;
}

// --------------------------------
// in: x; out: x
void MoveBulletBill(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // check bullet bill's enemy object state for d5 set
  a &= 0b00100000; zero_flag = a == 0;
  // Original branch: if not set, continue with movement code
  if (!zero_flag) {
    MoveJ_EnemyVertically(); return; // otherwise jump to move defeated bullet bill downwards
  }
  // NotDefB:
  ram[(uint8_t)(Enemy_X_Speed + x)] = 0xe8; // set bullet bill's horizontal speed; and move it accordingly (note: this bullet bill
  MoveEnemyHorizontally(); return; // object occurs in frenzy object $17, not from cannons)
}

// --------------------------------
// $02 - used to hold preset values
// $03 - used to hold enemy state
// in: x; out: x
void MoveSwimmingCheepCheep(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // check cheep-cheep's enemy object state
  a &= 0b00100000; zero_flag = a == 0; // for d5 set
  // Original branch: if not set, continue with movement code
  if (!zero_flag) {
    MoveEnemySlowVert(); return; // otherwise jump to move defeated cheep-cheep downwards
  }
  // CCSwim:
  ram[0x3] = a; // save enemy state in $03
  a = ram[(uint8_t)(Enemy_ID + x)]; // get enemy identifier
  carry_flag = true;
  a = (uint8_t)(a - 0xa - !carry_flag); // subtract ten for cheep-cheep identifiers
  y = a; // use as offset
  ram[0x2] = ROM(SwimCCXMoveData + y); // load value here
  a = ram[Enemy_X_MoveForce + x]; // load horizontal force
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[0x2] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract preset value from horizontal force
  ram[Enemy_X_MoveForce + x] = a; // store as new horizontal force
  a = ram[(uint8_t)(Enemy_X_Position + x)]; // get horizontal coordinate
  { uint16_t diff = (uint16_t)(a - 0x0 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract borrow (thus moving it slowly)
  ram[(uint8_t)(Enemy_X_Position + x)] = a; // and save as new horizontal coordinate
  a = ram[(uint8_t)(Enemy_PageLoc + x)];
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow again, this time from the
  ram[(uint8_t)(Enemy_PageLoc + x)] = a; // page location, then save
  ram[0x2] = 0x20; // save new value here
  // check enemy object offset
  // if in first or second slot, branch to leave
  if (x < 0x2) {
    return;
  }
  a = ram[(uint8_t)(CheepCheepMoveMFlag + x)]; // check movement flag
  // if movement speed set to $00,
  // Original branch: branch to move upwards
  if (a >= 0x10) {
    a = ram[Enemy_YMF_Dummy + x];
    carry_flag = false;
    { uint16_t sum = a + ram[0x2] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add preset value to dummy variable to get carry
    ram[Enemy_YMF_Dummy + x] = a; // and save dummy
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
    { uint16_t sum = a + ram[0x3] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add carry to it plus enemy state to slowly move it downwards
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // save as new vertical coordinate
    a = ram[(uint8_t)(Enemy_Y_HighPos + x)];
    a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location and
  } else {
    // CCSwimUpwards:
    a = ram[Enemy_YMF_Dummy + x];
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[0x2] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract preset value to dummy variable to get borrow
    ram[Enemy_YMF_Dummy + x] = a; // and save dummy
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
    { uint16_t diff = (uint16_t)(a - ram[0x3] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract borrow to it plus enemy state to slowly move it upwards
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // save as new vertical coordinate
    a = ram[(uint8_t)(Enemy_Y_HighPos + x)];
    a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from page location
  }
  // ChkSwimYPos:
  ram[(uint8_t)(Enemy_Y_HighPos + x)] = a; // save new page location here
  y = 0x0; // load movement speed to upwards by default
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
  carry_flag = true;
  a = (uint8_t)(a - ram[CheepCheepOrigYPos + x] - !carry_flag); neg_flag = (a & 0x80) != 0; // subtract original coordinate from current
  // Original branch: if result positive, skip to next part
  if (neg_flag) {
    y = 0x10; // otherwise load movement speed to downwards
    a ^= 0xff;
    carry_flag = false; // get two's compliment of result
    a = (uint8_t)(a + 0x1 + carry_flag); // to obtain total difference of original vs. current
  }
  // YPDiff:
  // if difference between original vs. current vertical
  // coordinates < 15 pixels, leave movement speed alone
  if (a < 0xf) {
    return;
  }
  ram[(uint8_t)(CheepCheepMoveMFlag + x)] = y; // otherwise change movement speed
  // ExSwCC:
  return; // leave
}

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
// in: x; out: x
void ProcFirebar(void) {
  CALL(GetEnemyOffscreenBits, 0xcd3e); // get offscreen information
  a = ram[Enemy_OffscreenBits]; // check for d3 set
  a &= 0b00001000; zero_flag = a == 0; // if so, branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set, branch
  // Original branch: ahead of this part
  if (zero_flag) {
    a = ram[FirebarSpinSpeed + x]; // load spinning speed of firebar
    CALL(FirebarSpin, 0xcd50); // modify current spinstate
    a &= 0b00011111; // mask out all but 5 LSB
    ram[(uint8_t)(FirebarSpinState_High + x)] = a; // and store as new high byte of spinstate
  }
  // SusFbar:
  a = ram[(uint8_t)(FirebarSpinState_High + x)]; // get high byte of spinstate
  y = ram[(uint8_t)(Enemy_ID + x)]; // check enemy identifier
  // Original branch: if < $1f (long firebar), branch
  if (y >= 0x1f) {
    // check high byte of spinstate
    // Original branch: if eight, branch to change
    if (a != 0x8) {
      zero_flag = a == 0x18;
      if (!zero_flag) { goto SetupGFB; } // if not at twenty-four branch to not change
    }
    // SkpFSte:
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag); // add one to spinning thing to avoid horizontal state
    ram[(uint8_t)(FirebarSpinState_High + x)] = a;
  }
  
SetupGFB:
  ram[0xef] = a; // save high byte of spinning thing, modified or otherwise
  CALL(RelativeEnemyPosition, 0xcd6e); // get relative coordinates to screen
  CALL(GetFirebarPosition, 0xcd71); // do a sub here (residual, too early to be used now)
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  a = ram[Enemy_Rel_YPos]; // get relative vertical coordinate
  ram[Sprite_Y_Position + y] = a; // store as Y in OAM data
  ram[0x7] = a; // also save here
  a = ram[Enemy_Rel_XPos]; // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X in OAM data
  ram[0x6] = a; // also save here
  ram[0x0] = 0x1; // set $01 value here (not necessary)
  CALL(FirebarCollision, 0xcd8b); // draw fireball part and do collision detection
  y = 0x5; // load value for short firebars by default
  a = ram[(uint8_t)(Enemy_ID + x)];
  // are we doing a long firebar?
  // Original branch: no, branch then
  if (a >= 0x1f) {
    y = 0xb; // otherwise load value for long firebars
  }
  // SetMFbar:
  ram[0xed] = y; // store maximum value for length of firebars
  ram[0x0] = 0x0; // initialize counter here
  do {
    // DrawFbar:
    a = ram[0xef]; // load high byte of spinstate
    CALL(GetFirebarPosition, 0xcda0); // get fireball position data depending on firebar part
    CALL(DrawFirebar_Collision, 0xcda3); // position it properly, draw it and do collision detection
    a = ram[0x0]; // check which firebar part
    if (a == 0x4) {
      y = ram[DuplicateObj_Offset]; // if we arrive at fifth firebar part,
      ram[0x6] = ram[Enemy_SprDataOffset + y]; // get offset from long firebar and load OAM data offset; using long firebar offset, then store as new one here
    }
    // NextFbar:
    ram[0x0]++; // move onto the next firebar part
    a = ram[0x0];
    carry_flag = a >= ram[0xed]; // if we end up at the maximum part, go on and leave
  } while (!carry_flag); // otherwise go back and do another
  // SkipFBar:
  return;
}

// in: none; out: x
void DrawFirebar_Collision(void) {
  ram[0x5] = ram[0x3]; // store mirror data elsewhere
  y = ram[0x6]; // load OAM data offset for firebar
  a = ram[0x1]; // load horizontal adder we got from position loader
  carry_flag = (ram[0x5] & 1) != 0; ram[0x5] = ram[0x5] >> 1; // shift LSB of mirror data
  // Original branch: if carry was set, skip this part
  if (!carry_flag) {
    a ^= 0xff;
    a = (uint8_t)(a + 0x1 + carry_flag); // otherwise get two's compliment of horizontal adder
  }
  // AddHA:
  carry_flag = false; // add horizontal coordinate relative to screen to
  a = (uint8_t)(a + ram[Enemy_Rel_XPos] + carry_flag); // horizontal adder, modified or otherwise
  ram[Sprite_X_Position + y] = a; // store as X coordinate here
  ram[0x6] = a; // store here for now, note offset is saved in Y still
  carry_flag = a >= ram[Enemy_Rel_XPos]; // compare X coordinate of sprite to original X of firebar
  // Original branch: if sprite coordinate => original coordinate, branch
  if (!carry_flag) {
    a = ram[Enemy_Rel_XPos];
    carry_flag = true; // otherwise subtract sprite X from the
    a = (uint8_t)(a - ram[0x6] - !carry_flag); // original one and skip this part
  } else {
    // SubtR1:
    carry_flag = true; // subtract original X from the
    a = (uint8_t)(a - ram[Enemy_Rel_XPos] - !carry_flag); // current sprite X
  }
  // ChkFOfs:
  // if difference of coordinates within a certain range,
  // Original branch: continue by handling vertical adder
  if (a >= 0x59) {
    a = 0xf8; // otherwise, load offscreen Y coordinate
  } else {
    // VAHandl:
    a = ram[Enemy_Rel_YPos]; // if vertical relative coordinate offscreen,
    zero_flag = a == 0xf8; // skip ahead of this part and write into sprite Y coordinate
    if (zero_flag) { goto SetVFbr; }
    a = ram[0x2]; // load vertical adder we got from position loader
    carry_flag = (ram[0x5] & 1) != 0; ram[0x5] = ram[0x5] >> 1; // shift LSB of mirror data one more time
    // Original branch: if carry was set, skip this part
    if (!carry_flag) {
      a ^= 0xff;
      a = (uint8_t)(a + 0x1 + carry_flag); // otherwise get two's compliment of second part
    }
    // AddVA:
    carry_flag = false; // add vertical coordinate relative to screen to 
    a = (uint8_t)(a + ram[Enemy_Rel_YPos] + carry_flag); // the second data, modified or otherwise
  }
  
SetVFbr:
  ram[Sprite_Y_Position + y] = a; // store as Y coordinate here
  ram[0x7] = a; // also store here for now
  FirebarCollision(); // fallthrough
  return;
}

// in: y; out: x
void FirebarCollision(void) {
  CALL(DrawFirebar, 0xce0a); // run sub here to draw current tile of firebar
  a = y; // return OAM data offset and save
  ram[0x100 + sp--] = a; // to the stack for now
  a = ram[StarInvincibleTimer]; // if star mario invincibility timer
  a |= ram[TimerControl]; zero_flag = a == 0; // or master timer controls set
  // Original branch: then skip all of this
  if (zero_flag) {
    ram[0x5] = a; // otherwise initialize counter
    y = ram[Player_Y_HighPos];
    y--; zero_flag = y == 0; // if player's vertical high byte offscreen,
    if (!zero_flag) { goto NoColFB; } // skip all of this
    y = ram[Player_Y_Position]; // get player's vertical position
    a = ram[PlayerSize]; zero_flag = a == 0; // get player's size
    // Original branch: if player small, branch to alter variables
    if (zero_flag) {
      a = ram[CrouchingFlag]; zero_flag = a == 0;
      if (zero_flag) { goto BigJp; } // if player big and not crouching, jump ahead
    }
    // AdjSm:
    ram[0x5]++; // if small or big but crouching, execute this part
    ram[0x5]++; // first increment our counter twice (setting $02 as flag)
    a = y;
    carry_flag = false; // then add 24 pixels to the player's
    a = (uint8_t)(a + 0x18 + carry_flag); // vertical coordinate
    y = a;
    
BigJp:
    a = y; // get vertical coordinate, altered or otherwise, from Y
    
FBCLoop:
    carry_flag = true; // subtract vertical position of firebar
    a = (uint8_t)(a - ram[0x7] - !carry_flag); neg_flag = (a & 0x80) != 0; // from the vertical coordinate of the player
    // Original branch: if player lower on the screen than firebar, 
    if (neg_flag) {
      a ^= 0xff; // skip two's compliment part
      carry_flag = false; // otherwise get two's compliment
      a = (uint8_t)(a + 0x1 + carry_flag);
    }
    // ChkVFBD:
    // if difference => 8 pixels, skip ahead of this part
    if (a < 0x8) {
      a = ram[0x6]; // if firebar on far right on the screen, skip this,
      carry_flag = a >= 0xf0; // because, really, what's the point?
      if (carry_flag) { goto Chk2Ofs; }
      a = ram[(Sprite_X_Position + 4)]; // get OAM X coordinate for sprite #1
      carry_flag = false;
      a = (uint8_t)(a + 0x4 + carry_flag); // add four pixels
      ram[0x4] = a; // store here
      carry_flag = true; // subtract horizontal coordinate of firebar
      a = (uint8_t)(a - ram[0x6] - !carry_flag); neg_flag = (a & 0x80) != 0; // from the X coordinate of player's sprite 1
      // Original branch: if modded X coordinate to the right of firebar
      if (neg_flag) {
        a ^= 0xff; // skip two's compliment part
        carry_flag = false; // otherwise get two's compliment
        a = (uint8_t)(a + 0x1 + carry_flag);
      }
      // ChkFBCl:
      carry_flag = a >= 0x8; // if difference < 8 pixels, collision, thus branch
      if (!carry_flag) { goto ChgSDir; } // to process
    }
    
Chk2Ofs:
    a = ram[0x5]; // if value of $02 was set earlier for whatever reason,
    zero_flag = a == 0x2; // branch to increment OAM offset and leave, no collision
    if (zero_flag) { goto NoColFB; }
    y = ram[0x5]; // otherwise get temp here and use as offset
    a = ram[Player_Y_Position];
    carry_flag = false;
    a = (uint8_t)(a + ROM(FirebarYPos + y) + carry_flag); // add value loaded with offset to player's vertical coordinate
    ram[0x5]++; // then increment temp and jump back
    goto FBCLoop;
    
ChgSDir:
    x = 0x1; // set movement direction by default
    a = ram[0x4]; // if OAM X coordinate of player's sprite 1
    carry_flag = a >= ram[0x6]; // is greater than horizontal coordinate of firebar
    // Original branch: then do not alter movement direction
    if (!carry_flag) {
      x++; // otherwise increment it
    }
    // SetSDir:
    ram[Enemy_MovingDir] = x; // store movement direction here
    x = 0x0;
    a = ram[0x0]; // save value written to $00 to stack
    ram[0x100 + sp--] = a;
    CALL(InjurePlayer, 0xce81); // perform sub to hurt or kill player
    a = ram[0x100 + ++sp];
    ram[0x0] = a; // get value of $00 from stack
  }
  
NoColFB:
  a = ram[0x100 + ++sp]; // get OAM data offset
  carry_flag = false; // add four to it and save
  a = (uint8_t)(a + 0x4 + carry_flag);
  ram[0x6] = a;
  x = ram[ObjectOffset]; // get enemy object buffer offset and leave
  return;
}

// in: a; out: none
void GetFirebarPosition(void) {
  ram[0x100 + sp--] = a; // save high byte of spinstate to the stack
  a &= 0b00001111; // mask out low nybble
  // Original branch: if lower than $09, branch ahead
  if (a >= 0x9) {
    a ^= 0b00001111; // otherwise get two's compliment to oscillate
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  // GetHAdder:
  ram[0x1] = a; // store result, modified or not, here
  y = ram[0x0]; // load number of firebar ball where we're at
  a = ROM(FirebarTblOffsets + y); // load offset to firebar position data
  carry_flag = false;
  a = (uint8_t)(a + ram[0x1] + carry_flag); // add oscillated high byte of spinstate
  y = a; // to offset here and use as new offset
  ram[0x1] = ROM(FirebarPosLookupTbl + y); // get data here and store as horizontal adder
  a = ram[0x100 + ++sp]; // pull whatever was in A from the stack
  ram[0x100 + sp--] = a; // save it again because we still need it
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight this time, to get vertical adder
  a &= 0b00001111; // mask out high nybble
  // if lower than $09, branch ahead
  if (a >= 0x9) {
    a ^= 0b00001111; // otherwise get two's compliment
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  // GetVAdder:
  ram[0x2] = a; // store result here
  y = ram[0x0];
  a = ROM(FirebarTblOffsets + y); // load offset to firebar position data again
  carry_flag = false;
  a = (uint8_t)(a + ram[0x2] + carry_flag); // this time add value in $02 to offset here and use as offset
  y = a;
  ram[0x2] = ROM(FirebarPosLookupTbl + y); // get data here and store as vertica adder
  a = ram[0x100 + ++sp]; // pull out whatever was in A one last time
  a = a >> 1; // divide by eight or shift three to the right
  a = a >> 1;
  a = a >> 1;
  y = a; // use as offset
  ram[0x3] = ROM(FirebarMirrorData + y); // load mirroring data here; store
  return;
}

// --------------------------------
// in: x; out: x
void MoveFlyingCheepCheep(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // check cheep-cheep's enemy state
  a &= 0b00100000; zero_flag = a == 0; // for d5 set
  // Original branch: branch to continue code if not set
  if (!zero_flag) {
    ram[Enemy_SprAttrib + x] = 0x0; // otherwise clear sprite attributes
    MoveJ_EnemyVertically(); return; // and jump to move defeated cheep-cheep downwards
  }
  // FlyCC:
  CALL(MoveEnemyHorizontally, 0xceef); // move cheep-cheep horizontally based on speed and force
  y = 0xd; // set vertical movement amount
  a = 0x5; // set maximum speed
  CALL(SetXMoveAmt, 0xcef6); // branch to impose gravity on flying cheep-cheep
  a = ram[Enemy_Y_MoveForce + x];
  a = a >> 1; // get vertical movement force and
  a = a >> 1; // move high nybble to low
  a = a >> 1;
  a = a >> 1;
  y = a; // save as offset (note this tends to go into reach of code)
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical position
  carry_flag = true; // subtract pseudorandom value based on offset from position
  a = (uint8_t)(a - ROM(PRandomSubtracter + y) - !carry_flag); neg_flag = (a & 0x80) != 0;
  // Original branch: if result within top half of screen, skip this part
  if (neg_flag) {
    a ^= 0xff;
    carry_flag = false; // otherwise get two's compliment
    a = (uint8_t)(a + 0x1 + carry_flag);
  }
  // AddCCF:
  // if result or two's compliment greater than eight,
  // Original branch: skip to the end without changing movement force
  if (a < 0x8) {
    a = ram[Enemy_Y_MoveForce + x];
    carry_flag = false;
    a = (uint8_t)(a + 0x10 + carry_flag); // otherwise add to it
    ram[Enemy_Y_MoveForce + x] = a;
    a = a >> 1; // move high nybble to low again
    a = a >> 1;
    a = a >> 1;
    a = a >> 1;
    y = a;
  }
  // BPGet:
  ram[Enemy_SprAttrib + x] = ROM(FlyCCBPriority + y); // load bg priority data and store (this is very likely; broken or residual code, value is overwritten before
  return; // drawing it next frame), then leave
}

// --------------------------------
// $00 - used to hold horizontal difference
// $01-$03 - used to hold difference adjusters
// in: x; out: x
void MoveLakitu(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // check lakitu's enemy state
  a &= 0b00100000; zero_flag = a == 0; // for d5 set
  // Original branch: if not set, continue with code
  if (!zero_flag) {
    MoveD_EnemyVertically(); return; // otherwise jump to move defeated lakitu downwards
  }
  // ChkLS:
  a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0; // if lakitu's enemy state not set at all,
  // Original branch: go ahead and continue with code
  if (!zero_flag) {
    a = 0x0;
    ram[(uint8_t)(LakituMoveDirection + x)] = a; // otherwise initialize moving direction to move to left
    ram[EnemyFrenzyBuffer] = a; // initialize frenzy buffer
    a = 0x10;
  } else {
    // Fr12S:
    ram[EnemyFrenzyBuffer] = Spiny; // set spiny identifier in frenzy buffer
    y = 0x2;
    do {
      // LdLDa:
      ram[0x1 + y] = ROM(LakituDiffAdj + y); // load values; store in zero page
      y--; neg_flag = (y & 0x80) != 0;
    } while (!neg_flag); // do this until all values are stired
    CALL(PlayerLakituDiff, 0xcf52); // execute sub to set speed and create spinys
  }
  // SetLSpd:
  ram[(uint8_t)(LakituMoveSpeed + x)] = a; // set movement speed returned from sub
  y = 0x1; // set moving direction to right by default
  a = ram[(uint8_t)(LakituMoveDirection + x)];
  a &= 0x1; zero_flag = a == 0; // get LSB of moving direction
  // Original branch: if set, branch to the end to use moving direction
  if (zero_flag) {
    a = ram[(uint8_t)(LakituMoveSpeed + x)];
    a ^= 0xff; // get two's compliment of moving speed
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(LakituMoveSpeed + x)] = a; // store as new moving speed
    y++; // increment moving direction to left
  }
  // SetLMov:
  ram[(uint8_t)(Enemy_MovingDir + x)] = y; // store moving direction
  MoveEnemyHorizontally(); return; // move lakitu horizontally
}

// in: x; out: a
void PlayerLakituDiff(void) {
  y = 0x0; // set Y for default value
  CALL(PlayerEnemyDiff, 0xcf70); // get horizontal difference between enemy and player
  // Original branch: branch if enemy is to the right of the player
  if (neg_flag) {
    y++; // increment Y for left of player
    a = ram[0x0];
    a ^= 0xff; // get two's compliment of low byte of horizontal difference
    carry_flag = false;
    a = (uint8_t)(a + 0x1 + carry_flag); // store two's compliment as horizontal difference
    ram[0x0] = a;
  }
  // ChkLakDif:
  a = ram[0x0]; // get low byte of horizontal difference
  // if within a certain distance of player, branch
  if (a >= 0x3c) {
    ram[0x0] = 0x3c; // otherwise set maximum distance
    a = ram[(uint8_t)(Enemy_ID + x)]; // check if lakitu is in our current enemy slot
    zero_flag = a == Lakitu;
    if (!zero_flag) { goto ChkPSpeed; } // if not, branch elsewhere
    a = y; // compare contents of Y, now in A
    zero_flag = a == ram[(uint8_t)(LakituMoveDirection + x)]; // to what is being used as horizontal movement direction
    if (zero_flag) { goto ChkPSpeed; } // if moving toward the player, branch, do not alter
    a = ram[(uint8_t)(LakituMoveDirection + x)]; zero_flag = a == 0; // if moving to the left beyond maximum distance,
    // Original branch: branch and alter without delay
    if (!zero_flag) {
      ram[(uint8_t)(LakituMoveSpeed + x)]--; // decrement horizontal speed
      a = ram[(uint8_t)(LakituMoveSpeed + x)]; zero_flag = a == 0; // if horizontal speed not yet at zero, branch to leave
      if (!zero_flag) {
        return;
      }
    }
    // SetLMovD:
    ram[(uint8_t)(LakituMoveDirection + x)] = y; // set horizontal direction depending on horizontal; difference between enemy and player if necessary
  }
  
ChkPSpeed:
  a = ram[0x0];
  a &= 0b00111100; // mask out all but four bits in the middle
  a = a >> 1; // divide masked difference by four
  a = a >> 1;
  ram[0x0] = a; // store as new value
  y = 0x0; // init offset
  a = ram[Player_X_Speed]; zero_flag = a == 0;
  // Original branch: if player not moving horizontally, branch
  if (!zero_flag) {
    a = ram[ScrollAmount]; zero_flag = a == 0;
    if (zero_flag) { goto SubDifAdj; } // if scroll speed not set, branch to same place
    y++; // otherwise increment offset
    a = ram[Player_X_Speed];
    // if player not running, branch
    if (a >= 0x19) {
      a = ram[ScrollAmount];
      carry_flag = a >= 0x2; // if scroll speed below a certain amount, branch
      if (!carry_flag) { goto ChkSpinyO; } // to same place
      y++; // otherwise increment once more
    }
    
ChkSpinyO:
    a = ram[(uint8_t)(Enemy_ID + x)]; // check for spiny object
    // Original branch: branch if not found
    if (a == Spiny) {
      a = ram[Player_X_Speed]; zero_flag = a == 0; // if player not moving, skip this part
      if (!zero_flag) { goto SubDifAdj; }
    }
    // ChkEmySpd:
    a = ram[(uint8_t)(Enemy_Y_Speed + x)]; zero_flag = a == 0; // check vertical speed
    if (!zero_flag) { goto SubDifAdj; } // branch if nonzero
    y = 0x0; // otherwise reinit offset
  }
  
SubDifAdj:
  a = ram[0x1 + y]; // get one of three saved values from earlier
  y = ram[0x0]; // get saved horizontal difference
  do {
    // SPixelLak:
    carry_flag = true; // subtract one for each pixel of horizontal difference
    a = (uint8_t)(a - 0x1 - !carry_flag); // from one of three saved values
    y--; neg_flag = (y & 0x80) != 0;
  } while (!neg_flag); // branch until all pixels are subtracted, to adjust difference
  // ExMoveLak:
  return; // leave!!!
}

// -------------------------------------------------------------------------------------
// $04-$05 - used to store name table address in little endian order
void BridgeCollapse(void) {
  x = ram[BowserFront_Offset]; // get enemy offset for bowser
  a = ram[(uint8_t)(Enemy_ID + x)]; // check enemy object identifier for bowser
  // if not found, branch ahead,
  // Original branch: metatile removal not necessary
  if (a == Bowser) {
    ram[ObjectOffset] = x; // store as enemy offset here
    a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0; // if bowser in normal state, skip all of this
    if (zero_flag) { goto RemoveBridge; }
    a &= 0b01000000; zero_flag = a == 0; // if bowser's state has d6 clear, skip to silence music
    if (zero_flag) { goto SetM2; }
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // check bowser's vertical coordinate
    // if bowser not yet low enough, skip this part ahead
    if (a < 0xe0) {
      MoveD_Bowser();
      return;
    }
  }
  
SetM2:
  ram[EventMusicQueue] = Silence; // silence music
  ram[OperMode_Task]++; // move onto next secondary mode in autoctrl mode
  KillAllEnemies(); return; // jump to empty all enemy slots and then leave  
  
RemoveBridge:
  ram[BowserFeetCounter]--; zero_flag = ram[BowserFeetCounter] == 0; // decrement timer to control bowser's feet
  // Original branch: if not expired, skip all of this
  if (zero_flag) {
    ram[BowserFeetCounter] = 0x4; // otherwise, set timer now
    a = ram[BowserBodyControls];
    a ^= 0x1; // invert bit to control bowser's feet
    ram[BowserBodyControls] = a;
    ram[0x5] = 0x22; // put high byte of name table address here for now
    y = ram[BridgeCollapseOffset]; // get bridge collapse offset here
    ram[0x4] = ROM(BridgeCollapseData + y); // load low byte of name table address and store here
    y = ram[VRAM_Buffer1_Offset]; // increment vram buffer offset
    y++;
    x = 0xc; // set offset for tile data for sub to draw blank metatile
    CALL(RemBridge, 0xd03b); // do sub here to remove bowser's bridge metatiles
    x = ram[ObjectOffset]; // get enemy offset
    CALL(MoveVOffset, 0xd040); // set new vram buffer offset
    ram[Square2SoundQueue] = Sfx_Blast; // load the fireworks/gunfire sound into the square 2 sfx; queue while at the same time loading the brick
    ram[NoiseSoundQueue] = Sfx_BrickShatter; // shatter sound into the noise sfx queue thus; producing the unique sound of the bridge collapsing 
    ram[BridgeCollapseOffset]++; // increment bridge collapse offset
    a = ram[BridgeCollapseOffset];
    zero_flag = a == 0xf; // if bridge collapse offset has not yet reached
    if (!zero_flag) { goto NoBFall; } // the end, go ahead and skip this part
    CALL(InitVStf, 0xd055); // initialize whatever vertical speed bowser has
    ram[(uint8_t)(Enemy_State + x)] = 0b01000000; // set bowser's state to one of defeated states (d6 set)
    ram[Square2SoundQueue] = Sfx_BowserFall; // play bowser defeat sound
  }
  
NoBFall:
  BowserGfxHandler(); return; // jump to code that draws bowser
}

// in: x; out: x
void MoveD_Bowser(void) {
  CALL(MoveEnemySlowVert, 0xd011); // do a sub to move bowser downwards
  BowserGfxHandler(); return; // jump to draw bowser's front and rear, then leave
}

// --------------------------------
// in: x; out: x
void RunBowser(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // if d5 in enemy state is not set
  a &= 0b00100000; zero_flag = a == 0; // then branch elsewhere to run bowser
  if (!zero_flag) {
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // otherwise check vertical position
    // if above a certain point, branch to move defeated bowser
    // otherwise proceed to KillAllEnemies
    if (a < 0xe0) {
      MoveD_Bowser();
      return;
    }
    KillAllEnemies(); // fallthrough
    return;
  }
  // BowserControl:
  ram[EnemyFrenzyBuffer] = 0x0; // empty frenzy buffer
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control not set,
  // Original branch: skip jump and execute code here
  if (!zero_flag) {
  } else {
    // ChkMouth:
    a = ram[BowserBodyControls]; neg_flag = (a & 0x80) != 0; // check bowser's mouth
    // Original branch: if bit clear, go ahead with code here
    if (neg_flag) {
    } else {
      // FeetTmr:
      ram[BowserFeetCounter]--; zero_flag = ram[BowserFeetCounter] == 0; // decrement timer to control bowser's feet
      // Original branch: if not expired, skip this part
      if (zero_flag) {
        ram[BowserFeetCounter] = 0x20; // otherwise, reset timer
        a = ram[BowserBodyControls]; // and invert bit used
        a ^= 0b00000001; // to control bowser's feet
        ram[BowserBodyControls] = a;
      }
      // ResetMDr:
      a = ram[FrameCounter]; // check frame counter
      a &= 0b00001111; zero_flag = a == 0; // if not on every sixteenth frame, skip
      // Original branch: ahead to continue code
      if (zero_flag) {
        ram[(uint8_t)(Enemy_MovingDir + x)] = 0x2; // otherwise reset moving/facing direction every; sixteen frames
      }
      // B_FaceP:
      a = ram[(EnemyFrameTimer + x) & 0x7ff]; zero_flag = a == 0; // if timer set here expired,
      // Original branch: branch to next section
      if (!zero_flag) {
        CALL(PlayerEnemyDiff, 0xd0b7); // get horizontal difference between player and bowser,
        if (!neg_flag) { goto GetPRCmp; } // and branch if bowser to the right of the player
        ram[(uint8_t)(Enemy_MovingDir + x)] = 0x1; // set bowser to move and face to the right
        ram[BowserMovementSpeed] = 0x2; // set movement speed
        a = 0x20;
        ram[(EnemyFrameTimer + x) & 0x7ff] = a; // set timer here
        ram[BowserFireBreathTimer] = a; // set timer used for bowser's flame
        a = ram[(uint8_t)(Enemy_X_Position + x)];
        carry_flag = a >= 0xc8; // if bowser to the right past a certain point,
        if (carry_flag) { goto HammerChk; } // skip ahead to some other section
      }
      
GetPRCmp:
      a = ram[FrameCounter]; // get frame counter
      a &= 0b00000011; zero_flag = a == 0;
      if (!zero_flag) { goto HammerChk; } // execute this code every fourth frame, otherwise branch
      a = ram[(uint8_t)(Enemy_X_Position + x)];
      zero_flag = a == ram[BowserOrigXPos]; // if bowser not at original horizontal position,
      // Original branch: branch to skip this part
      if (zero_flag) {
        a = ram[(PseudoRandomBitReg + x) & 0x7ff];
        a &= 0b00000011; // get pseudorandom offset
        y = a;
        ram[MaxRangeFromOrigin] = ROM(PRandomRange + y); // load value using pseudorandom offset; and store here
      }
      // GetDToO:
      a = ram[(uint8_t)(Enemy_X_Position + x)];
      carry_flag = false; // add movement speed to bowser's horizontal
      a = (uint8_t)(a + ram[BowserMovementSpeed] + carry_flag); // coordinate and save as new horizontal position
      ram[(uint8_t)(Enemy_X_Position + x)] = a;
      y = ram[(uint8_t)(Enemy_MovingDir + x)];
      zero_flag = y == 0x1; // if bowser moving and facing to the right, skip ahead
      if (zero_flag) { goto HammerChk; }
      y = 0xff; // set default movement speed here (move left)
      carry_flag = true; // get difference of current vs. original
      a = (uint8_t)(a - ram[BowserOrigXPos] - !carry_flag); neg_flag = (a & 0x80) != 0; // horizontal position
      // Original branch: if current position to the right of original, skip ahead
      if (neg_flag) {
        a ^= 0xff;
        carry_flag = false; // get two's compliment
        a = (uint8_t)(a + 0x1 + carry_flag);
        y = 0x1; // set alternate movement speed here (move right)
      }
      // CompDToO:
      carry_flag = a >= ram[MaxRangeFromOrigin]; // compare difference with pseudorandom value
      if (!carry_flag) { goto HammerChk; } // if difference < pseudorandom value, leave speed alone
      ram[BowserMovementSpeed] = y; // otherwise change bowser's movement speed
    }
    
HammerChk:
    a = ram[(EnemyFrameTimer + x) & 0x7ff]; zero_flag = a == 0; // if timer set here not expired yet, skip ahead to
    if (!zero_flag) { goto MakeBJump; } // some other section of code
    CALL(MoveEnemySlowVert, 0xd116); // otherwise start by moving bowser downwards
    a = ram[WorldNumber]; // check world number
    // Original branch: if world 1-5, skip this part (not time to throw hammers yet)
    if (a >= World6) {
      a = ram[FrameCounter];
      a &= 0b00000011; zero_flag = a == 0; // check to see if it's time to execute sub
      if (!zero_flag) { goto SetHmrTmr; } // if not, skip sub, otherwise
      CALL(SpawnHammerObj, 0xd126); // execute sub on every fourth frame to spawn misc object (hammer)
    }
    
SetHmrTmr:
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get current vertical position
    carry_flag = a >= 0x80; // if still above a certain point
    if (!carry_flag) { goto ChkFireB; } // then skip to world number check for flames
    a = ram[(PseudoRandomBitReg + x) & 0x7ff];
    a &= 0b00000011; // get pseudorandom offset
    y = a;
    ram[(EnemyFrameTimer + x) & 0x7ff] = ROM(PRandomRange + y); // get value using pseudorandom offset; set for timer here
  }
  // SkipToFB:
  goto ChkFireB; // jump to execute flames code
  
MakeBJump:
  // if timer not yet about to expire,
  // Original branch: skip ahead to next part
  if (a == 0x1) {
    ram[(uint8_t)(Enemy_Y_Position + x)]--; // otherwise decrement vertical coordinate
    CALL(InitVStf, 0xd144); // initialize movement amount
    ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xfe; // set vertical speed to move bowser upwards
  }
  do {
    
ChkFireB:
    a = ram[WorldNumber]; // check world number here
    // world 8?
    // Original branch: if so, execute this part here
    if (a != World8) {
      // world 6-7?
      // if so, skip this part here
      if (a >= World6) {
        BowserGfxHandler();
        return;
      }
    }
    // SpawnFBr:
    a = ram[BowserFireBreathTimer]; zero_flag = a == 0; // check timer here
    // if not expired yet, skip all of this
    if (!zero_flag) {
      BowserGfxHandler();
      return;
    }
    ram[BowserFireBreathTimer] = 0x20; // set timer here
    a = ram[BowserBodyControls];
    a ^= 0b10000000; neg_flag = (a & 0x80) != 0; // invert bowser's mouth bit to open
    ram[BowserBodyControls] = a; // and close bowser's mouth
  } while (neg_flag); // if bowser's mouth open, loop back
  CALL(SetFlameTimer, 0xd16a); // get timing for bowser's flame
  y = ram[SecondaryHardMode]; zero_flag = y == 0;
  // Original branch: if secondary hard mode flag not set, skip this
  if (!zero_flag) {
    carry_flag = true;
    a = (uint8_t)(a - 0x10 - !carry_flag); // otherwise subtract from value in A
  }
  // SetFBTmr:
  ram[BowserFireBreathTimer] = a; // set value as timer here
  ram[EnemyFrenzyBuffer] = BowserFlame; // put bowser's flame identifier; in enemy frenzy buffer
  BowserGfxHandler(); // fallthrough
  return;
}

// in: none; out: x
void KillAllEnemies(void) {
  x = 0x4; // start with last enemy slot
  do {
    // KillLoop:
    CALL(EraseEnemyObject, 0xd075); // branch to kill enemy objects
    x--; neg_flag = (x & 0x80) != 0; // move onto next enemy slot
  } while (!neg_flag); // do this until all slots are emptied
  ram[EnemyFrenzyBuffer] = a; // empty frenzy buffer
  x = ram[ObjectOffset]; // get enemy object offset and leave
  return;
}

// --------------------------------
// in: x; out: x
void BowserGfxHandler(void) {
  CALL(ProcessBowserHalf, 0xd17d); // do a sub here to process bowser's front
  y = 0x10; // load default value here to position bowser's rear
  a = ram[(uint8_t)(Enemy_MovingDir + x)]; // check moving direction
  carry_flag = (a & 1) != 0; a = a >> 1;
  // Original branch: if moving left, use default
  if (carry_flag) {
    y = 0xf0; // otherwise load alternate positioning value here
  }
  // CopyFToR:
  a = y; // move bowser's rear object position value to A
  carry_flag = false;
  a = (uint8_t)(a + ram[(uint8_t)(Enemy_X_Position + x)] + carry_flag); // add to bowser's front object horizontal coordinate
  y = ram[DuplicateObj_Offset]; // get bowser's rear object offset
  ram[Enemy_X_Position + y] = a; // store A as bowser's rear horizontal coordinate
  a = ram[(uint8_t)(Enemy_Y_Position + x)];
  carry_flag = false; // add eight pixels to bowser's front object
  a = (uint8_t)(a + 0x8 + carry_flag); // vertical coordinate and store as vertical coordinate
  ram[Enemy_Y_Position + y] = a; // for bowser's rear
  ram[Enemy_State + y] = ram[(uint8_t)(Enemy_State + x)]; // copy enemy state directly from front to rear
  ram[Enemy_MovingDir + y] = ram[(uint8_t)(Enemy_MovingDir + x)]; // copy moving direction also
  a = ram[ObjectOffset]; // save enemy object offset of front to stack
  ram[0x100 + sp--] = a;
  x = ram[DuplicateObj_Offset]; // put enemy object offset of rear as current
  ram[ObjectOffset] = x;
  ram[(uint8_t)(Enemy_ID + x)] = Bowser; // set bowser's enemy identifier; store in bowser's rear object
  CALL(ProcessBowserHalf, 0xd1b1); // do a sub here to process bowser's rear
  a = ram[0x100 + ++sp];
  ram[ObjectOffset] = a; // get original enemy object offset
  x = a;
  ram[BowserGfxFlag] = 0x0; // nullify bowser's front/rear graphics flag
  // ExBGfxH:
  return; // leave!
}

// in: x; out: x
void ProcessBowserHalf(void) {
  ram[BowserGfxFlag]++; // increment bowser's graphics flag, then run subroutines
  CALL(RunRetainerObj, 0xd1c1); // to get offscreen bits, relative position and draw bowser (finally!)
  a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0;
  // if either enemy object not in normal state, branch to leave
  if (!zero_flag) {
    return;
  }
  ram[Enemy_BoundBoxCtrl + x] = 0xa; // set bounding box size control
  CALL(GetEnemyBoundBox, 0xd1cd); // get bounding box coordinates
  PlayerEnemyCollision(); return; // do player-to-enemy collision detection
}

// -------------------------------------------------------------------------------------
// $00 - used to hold movement force and tile number
// $01 - used to hold sprite attribute data
// in: none; out: a
void SetFlameTimer(void) {
  y = ram[BowserFlameTimerCtrl]; // load counter as offset
  ram[BowserFlameTimerCtrl]++; // increment
  a = ram[BowserFlameTimerCtrl]; // mask out all but 3 LSB
  a &= 0b00000111; // to keep in range of 0-7
  ram[BowserFlameTimerCtrl] = a;
  a = ROM(FlameTimerData + y); // load value to be used then leave
  // ExFl:
  return;
}

// in: x; out: x
void ProcBowserFlame(void) {
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control flag set,
  // Original branch: skip all of this
  if (zero_flag) {
    a = 0x40; // load default movement force
    y = ram[SecondaryHardMode]; zero_flag = y == 0;
    // Original branch: if secondary hard mode flag not set, use default
    if (!zero_flag) {
      a = 0x60; // otherwise load alternate movement force to go faster
    }
    // SFlmX:
    ram[0x0] = a; // store value here
    a = ram[Enemy_X_MoveForce + x];
    carry_flag = true; // subtract value from movement force
    { uint16_t diff = (uint16_t)(a - ram[0x0] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
    ram[Enemy_X_MoveForce + x] = a; // save new value
    a = ram[(uint8_t)(Enemy_X_Position + x)];
    { uint16_t diff = (uint16_t)(a - 0x1 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract one from horizontal position to move
    ram[(uint8_t)(Enemy_X_Position + x)] = a; // to the left
    a = ram[(uint8_t)(Enemy_PageLoc + x)];
    a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from page location
    ram[(uint8_t)(Enemy_PageLoc + x)] = a;
    y = ram[BowserFlamePRandomOfs + x]; // get some value here and use as offset
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // load vertical coordinate
    zero_flag = a == ROM(FlameYPosData + y); // compare against coordinate data using $0417,x as offset
    if (zero_flag) { goto SetGfxF; } // if equal, branch and do not modify coordinate
    carry_flag = false;
    a = (uint8_t)(a + ram[Enemy_Y_MoveForce + x] + carry_flag); // otherwise add value here to coordinate and store
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // as new vertical coordinate
  }
  
SetGfxF:
  CALL(RelativeEnemyPosition, 0xd222); // get new relative coordinates
  a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0; // if bowser's flame not in normal state,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  ram[0x0] = 0x51; // otherwise, continue; write first tile number
  y = 0x2; // load attributes without vertical flip by default
  a = ram[FrameCounter];
  a &= 0b00000010; zero_flag = a == 0; // invert vertical flip bit every 2 frames
  // Original branch: if d1 not set, write default value
  if (!zero_flag) {
    y = 0x82; // otherwise write value with vertical flip bit set
  }
  // FlmeAt:
  ram[0x1] = y; // set bowser's flame sprite attributes here
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  x = 0x0;
  do {
    // DrawFlameLoop:
    ram[Sprite_Y_Position + y] = ram[Enemy_Rel_YPos]; // get Y relative coordinate of current enemy object; write into Y coordinate of OAM data
    ram[Sprite_Tilenumber + y] = ram[0x0]; // write current tile number into OAM data
    ram[0x0]++; // increment tile number to draw more bowser's flame
    ram[Sprite_Attributes + y] = ram[0x1]; // write saved attributes into OAM data
    a = ram[Enemy_Rel_XPos];
    ram[Sprite_X_Position + y] = a; // write X relative coordinate of current enemy object
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag);
    ram[Enemy_Rel_XPos] = a; // then add eight to it and store
    y++;
    y++;
    y++;
    y++; // increment Y four times to move onto the next OAM
    x++; // move onto the next OAM, and branch if three
    carry_flag = x >= 0x3; // have not yet been done
  } while (!carry_flag);
  x = ram[ObjectOffset]; // reload original enemy offset
  CALL(GetEnemyOffscreenBits, 0xd267); // get offscreen information
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  a = ram[Enemy_OffscreenBits]; // get enemy object offscreen bits
  carry_flag = (a & 1) != 0; a = a >> 1; // move d0 to carry and result to stack
  ram[0x100 + sp--] = a;
  // Original branch: branch if carry not set
  if (carry_flag) {
    ram[(Sprite_Y_Position + 12) + y] = 0xf8; // otherwise move sprite offscreen, this part likely; residual since flame is only made of three sprites
  }
  // M3FOfs:
  a = ram[0x100 + ++sp]; // get bits from stack
  carry_flag = (a & 1) != 0; a = a >> 1; // move d1 to carry and move bits back to stack
  ram[0x100 + sp--] = a;
  // Original branch: branch if carry not set again
  if (carry_flag) {
    ram[(Sprite_Y_Position + 8) + y] = 0xf8; // otherwise move third sprite offscreen
  }
  // M2FOfs:
  a = ram[0x100 + ++sp]; // get bits from stack again
  carry_flag = (a & 1) != 0; a = a >> 1; // move d2 to carry and move bits back to stack again
  ram[0x100 + sp--] = a;
  // Original branch: branch if carry not set yet again
  if (carry_flag) {
    ram[(Sprite_Y_Position + 4) + y] = 0xf8; // otherwise move second sprite offscreen
  }
  // M1FOfs:
  a = ram[0x100 + ++sp]; // get bits from stack one last time
  carry_flag = (a & 1) != 0; a = a >> 1; // move d3 to carry
  // branch if carry not set one last time
  if (!carry_flag) {
    return;
  }
  ram[Sprite_Y_Position + y] = 0xf8; // otherwise move first sprite offscreen
  // ExFlmeD:
  return; // leave
}

// --------------------------------
// in: x; out: x
void RunFireworks(void) {
  ram[(uint8_t)(ExplosionTimerCounter + x)]--; zero_flag = ram[(uint8_t)(ExplosionTimerCounter + x)] == 0; // decrement explosion timing counter here
  // Original branch: if not expired, skip this part
  if (zero_flag) {
    ram[(uint8_t)(ExplosionTimerCounter + x)] = 0x8; // reset counter
    ram[(uint8_t)(ExplosionGfxCounter + x)]++; // increment explosion graphics counter
    a = ram[(uint8_t)(ExplosionGfxCounter + x)];
    carry_flag = a >= 0x3; // check explosion graphics counter
    if (carry_flag) { goto FireworksSoundScore; } // if at a certain point, branch to kill this object
  }
  // SetupExpl:
  CALL(RelativeEnemyPosition, 0xd2a7); // get relative coordinates of explosion
  ram[Fireball_Rel_YPos] = ram[Enemy_Rel_YPos]; // copy relative coordinates; from the enemy object to the fireball object
  ram[Fireball_Rel_XPos] = ram[Enemy_Rel_XPos]; // first vertical, then horizontal
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  a = ram[(uint8_t)(ExplosionGfxCounter + x)]; // get explosion graphics counter
  CALL(DrawExplosion_Fireworks, 0xd2bb); // do a sub to draw the explosion then leave
  return;
  
FireworksSoundScore:
  ram[(uint8_t)(Enemy_Flag + x)] = 0x0; // disable enemy buffer flag
  ram[Square2SoundQueue] = Sfx_Blast; // play fireworks/gunfire sound
  ram[(DigitModifier + 4)] = 0x5; // set part of score modifier for 500 points
  EndAreaPoints(); return; // jump to award points accordingly then leave
}

// --------------------------------
// in: x; out: x
void RunStarFlagObj(void) {
  ram[EnemyFrenzyBuffer] = 0x0; // initialize enemy frenzy buffer
  a = ram[StarFlagTaskControl]; // check star flag object task number here
  // if greater than 5, branch to exit
  if (a >= 0x5) {
    return;
  }
  switch (JumpEngine(0xd2e7)) {
    case 0xd311: StarFlagExit(); return;
    case 0xd2f2: GameTimerFireworks(); return;
    case 0xd312: AwardGameTimerPoints(); return;
    case 0xd34e: RaiseFlagSetoffFWorks(); return;
    case 0xd3a2: DelayToAreaEnd(); return;
    default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table
  }
}

// in: x; out: none
void GameTimerFireworks(void) {
  y = 0x5; // set default state for star flag object
  a = ram[(GameTimerDisplay + 2)]; // get game timer's last digit
  // Original branch: if last digit of game timer set to 1, skip ahead
  if (a != 0x1) {
    y = 0x3; // otherwise load new value for state
    zero_flag = a == 0x3;
    if (zero_flag) { goto SetFWC; } // if last digit of game timer set to 3, skip ahead
    y = 0x0; // otherwise load one more potential value for state
    zero_flag = a == 0x6;
    if (zero_flag) { goto SetFWC; } // if last digit of game timer set to 6, skip ahead
    a = 0xff; // otherwise set value for no fireworks
  }
  
SetFWC:
  ram[FireworksCounter] = a; // set fireworks counter here
  ram[(uint8_t)(Enemy_State + x)] = y; // set whatever state we have in star flag object
  IncrementSFTask1(); // fallthrough
  return;
}

void IncrementSFTask1(void) {
  ram[StarFlagTaskControl]++; // increment star flag object task number
  StarFlagExit(); // fallthrough
  return;
}

void StarFlagExit(void) {
  return; // leave
}

// in: none; out: x
void AwardGameTimerPoints(void) {
  a = ram[GameTimerDisplay]; // check all game timer digits for any intervals left
  a |= ram[(GameTimerDisplay + 1)];
  a |= ram[(GameTimerDisplay + 2)]; zero_flag = a == 0;
  // if no time left on game timer at all, branch to next task
  if (zero_flag) {
    IncrementSFTask1();
    return;
  }
  a = ram[FrameCounter];
  a &= 0b00000100; zero_flag = a == 0; // check frame counter for d2 set (skip ahead
  // Original branch: for four frames every four frames) branch if not set
  if (!zero_flag) {
    ram[Square2SoundQueue] = Sfx_TimerTick; // load timer tick sound
  }
  // NoTTick:
  y = 0x23; // set offset here to subtract from game timer's last digit
  ram[(DigitModifier + 5)] = 0xff; // set adder here to $ff, or -1, to subtract one; from the last digit of the game timer
  CALL(DigitsMathRoutine, 0xd330); // subtract digit
  ram[(DigitModifier + 5)] = 0x5; // set now to add 50 points; per game timer interval subtracted
  EndAreaPoints(); // fallthrough
  return;
}

// in: none; out: x
void EndAreaPoints(void) {
  y = 0xb; // load offset for mario's score by default
  a = ram[CurrentPlayer]; zero_flag = a == 0; // check player on the screen
  // Original branch: if mario, do not change
  if (!zero_flag) {
    y = 0x11; // otherwise load offset for luigi's score
  }
  // ELPGive:
  CALL(DigitsMathRoutine, 0xd341); // award 50 points per game timer interval
  a = ram[CurrentPlayer]; // get player on the screen (or 500 points per
  a = (uint8_t)(a << 1); // fireworks explosion if branched here from there)
  a = (uint8_t)(a << 1); // shift to high nybble
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a |= 0b00000100; // add four to set nybble for game timer
  UpdateNumber(); return; // jump to print the new score and game timer
}

// in: x; out: x
void RaiseFlagSetoffFWorks(void) {
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // check star flag's vertical position
  // against preset value
  // Original branch: if star flag higher vertically, branch to other code
  if (a >= 0x72) {
    ram[(uint8_t)(Enemy_Y_Position + x)]--; // otherwise, raise star flag by one pixel
    DrawStarFlag(); return; // and skip this part here
  }
  // SetoffF:
  a = ram[FireworksCounter]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // check fireworks counter
  // Original branch: if no fireworks left to go off, skip this part
  if (!zero_flag) {
    if (neg_flag) { goto DrawFlagSetTimer; } // if no fireworks set to go off, skip this part
    ram[EnemyFrenzyBuffer] = Fireworks; // otherwise set fireworks object in frenzy queue
    DrawStarFlag(); // fallthrough
    return;
  }
  
DrawFlagSetTimer:
  CALL(DrawStarFlag, 0xd398); // do sub to draw star flag
  ram[(EnemyIntervalTimer + x) & 0x7ff] = 0x6; // set interval timer here
  IncrementSFTask2(); // fallthrough
  return;
}

// in: x; out: x
void DrawStarFlag(void) {
  CALL(RelativeEnemyPosition, 0xd367); // get relative coordinates of star flag
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  x = 0x3; // do four sprites
  do {
    // DSFLoop:
    a = ram[Enemy_Rel_YPos]; // get relative vertical coordinate
    carry_flag = false;
    a = (uint8_t)(a + ROM(StarFlagYPosAdder + x) + carry_flag); // add Y coordinate adder data
    ram[Sprite_Y_Position + y] = a; // store as Y coordinate
    ram[Sprite_Tilenumber + y] = ROM(StarFlagTileData + x); // get tile number; store as tile number
    ram[Sprite_Attributes + y] = 0x22; // set palette and background priority bits; store as attributes
    a = ram[Enemy_Rel_XPos]; // get relative horizontal coordinate
    carry_flag = false;
    a = (uint8_t)(a + ROM(StarFlagXPosAdder + x) + carry_flag); // add X coordinate adder data
    ram[Sprite_X_Position + y] = a; // store as X coordinate
    y++;
    y++; // increment OAM data offset four bytes
    y++; // for next sprite
    y++;
    x--; neg_flag = (x & 0x80) != 0; // move onto next sprite
  } while (!neg_flag); // do this until all sprites are done
  x = ram[ObjectOffset]; // get enemy object offset and leave
  return;
}

void IncrementSFTask2(void) {
  ram[StarFlagTaskControl]++; // move onto next task
  return;
}

// in: x; out: x
void DelayToAreaEnd(void) {
  CALL(DrawStarFlag, 0xd3a4); // do sub to draw star flag
  a = ram[(EnemyIntervalTimer + x) & 0x7ff]; zero_flag = a == 0; // if interval timer set in previous task
  // not yet expired, branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[EventMusicBuffer]; zero_flag = a == 0; // if event music buffer empty,
  // branch to increment task
  if (zero_flag) {
    IncrementSFTask2();
    return;
  }
  // StarFlagExit2:
  return; // otherwise leave
}

// --------------------------------
// $00 - used to store horizontal difference between player and piranha plant
// in: x; out: none
void MovePiranhaPlant(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0; // check enemy state
  // Original branch: if set at all, branch to leave
  if (zero_flag) {
    a = ram[(EnemyFrameTimer + x) & 0x7ff]; zero_flag = a == 0; // check enemy's timer here
    if (!zero_flag) { goto PutinPipe; } // branch to end if not yet expired
    a = ram[(uint8_t)(PiranhaPlant_MoveFlag + x)]; zero_flag = a == 0; // check movement flag
    // Original branch: if moving, skip to part ahead
    if (zero_flag) {
      a = ram[(uint8_t)(PiranhaPlant_Y_Speed + x)]; neg_flag = (a & 0x80) != 0; // if currently rising, branch 
      // Original branch: to move enemy upwards out of pipe
      if (!neg_flag) {
        CALL(PlayerEnemyDiff, 0xd3c3); // get horizontal difference between player and
        // Original branch: piranha plant, and branch if enemy to right of player
        if (neg_flag) {
          a = ram[0x0]; // otherwise get saved horizontal difference
          a ^= 0xff;
          carry_flag = false; // and change to two's compliment
          a = (uint8_t)(a + 0x1 + carry_flag);
          ram[0x0] = a; // save as new horizontal difference
        }
        // ChkPlayerNearPipe:
        a = ram[0x0]; // get saved horizontal difference
        carry_flag = a >= 0x21;
        if (!carry_flag) { goto PutinPipe; } // if player within a certain distance, branch to leave
      }
      // ReversePlantSpeed:
      a = ram[(uint8_t)(PiranhaPlant_Y_Speed + x)]; // get vertical speed
      a ^= 0xff;
      carry_flag = false; // change to two's compliment
      a = (uint8_t)(a + 0x1 + carry_flag);
      ram[(uint8_t)(PiranhaPlant_Y_Speed + x)] = a; // save as new vertical speed
      ram[(uint8_t)(PiranhaPlant_MoveFlag + x)]++; // increment to set movement flag
    }
    // SetupToMovePPlant:
    a = ram[PiranhaPlantDownYPos + x]; // get original vertical coordinate (lowest point)
    y = ram[(uint8_t)(PiranhaPlant_Y_Speed + x)]; neg_flag = (y & 0x80) != 0; // get vertical speed
    // Original branch: branch if moving downwards
    if (neg_flag) {
      a = ram[PiranhaPlantUpYPos + x]; // otherwise get other vertical coordinate (highest point)
    }
    // RiseFallPiranhaPlant:
    ram[0x0] = a; // save vertical coordinate here
    a = ram[FrameCounter]; // get frame counter
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (!carry_flag) { goto PutinPipe; } // branch to leave if d0 set (execute code every other frame)
    a = ram[TimerControl]; zero_flag = a == 0; // get master timer control
    if (!zero_flag) { goto PutinPipe; } // branch to leave if set (likely not necessary)
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get current vertical coordinate
    carry_flag = false;
    a = (uint8_t)(a + ram[(uint8_t)(PiranhaPlant_Y_Speed + x)] + carry_flag); // add vertical speed to move up or down
    ram[(uint8_t)(Enemy_Y_Position + x)] = a; // save as new vertical coordinate
    zero_flag = a == ram[0x0]; // compare against low or high coordinate
    if (!zero_flag) { goto PutinPipe; } // branch to leave if not yet reached
    ram[(uint8_t)(PiranhaPlant_MoveFlag + x)] = 0x0; // otherwise clear movement flag
    ram[(EnemyFrameTimer + x) & 0x7ff] = 0x40; // set timer to delay piranha plant movement
  }
  
PutinPipe:
  ram[Enemy_SprAttrib + x] = 0b00100000; // set background priority bit in sprite; attributes to give illusion of being inside pipe
  return; // then leave
}

// -------------------------------------------------------------------------------------
// $07 - spinning speed
// in: a, x; out: a
void FirebarSpin(void) {
  ram[0x7] = a; // save spinning speed here
  a = ram[(uint8_t)(FirebarSpinDirection + x)]; zero_flag = a == 0; // check spinning direction
  // Original branch: if moving counter-clockwise, branch to other part
  if (zero_flag) {
    y = 0x18; // possibly residual ldy
    a = ram[(uint8_t)(FirebarSpinState_Low + x)];
    carry_flag = false; // add spinning speed to what would normally be
    { uint16_t sum = a + ram[0x7] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // the horizontal speed
    ram[(uint8_t)(FirebarSpinState_Low + x)] = a;
    a = ram[(uint8_t)(FirebarSpinState_High + x)]; // add carry to what would normally be the vertical speed
    a = (uint8_t)(a + 0x0 + carry_flag);
    return;
  }
  // SpinCounterClockwise:
  y = 0x8; // possibly residual ldy
  a = ram[(uint8_t)(FirebarSpinState_Low + x)];
  carry_flag = true; // subtract spinning speed to what would normally be
  { uint16_t diff = (uint16_t)(a - ram[0x7] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // the horizontal speed
  ram[(uint8_t)(FirebarSpinState_Low + x)] = a;
  a = ram[(uint8_t)(FirebarSpinState_High + x)]; // add carry to what would normally be the vertical speed
  a = (uint8_t)(a - 0x0 - !carry_flag);
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used to hold collision flag, Y movement force + 5 or low byte of name table for rope
// $01 - used to hold high byte of name table for rope
// $02 - used to hold page location of rope
// in: x; out: x
void BalancePlatform(void) {
  a = ram[(uint8_t)(Enemy_Y_HighPos + x)]; // check high byte of vertical position
  if (a == 0x3) {
    EraseEnemyObject(); return; // if far below screen, kill the object
  }
  // DoBPl:
  a = ram[(uint8_t)(Enemy_State + x)]; neg_flag = (a & 0x80) != 0; // get object's state (set to $ff or other platform offset)
  // Original branch: if doing other balance platform, branch to leave
  if (neg_flag) {
    return;
  }
  // CheckBalPlatform:
  y = a; // save offset from state as Y
  ram[0x0] = ram[PlatformCollisionFlag + x]; // get collision flag of platform; store here
  a = ram[(uint8_t)(Enemy_MovingDir + x)]; zero_flag = a == 0; // get moving direction
  if (!zero_flag) {
  } else {
    // ChkForFall:
    a = 0x2d; // check if platform is above a certain point
    carry_flag = a >= ram[(uint8_t)(Enemy_Y_Position + x)];
    if (!carry_flag) { goto ChkOtherForFall; } // if not, branch elsewhere
    zero_flag = y == ram[0x0]; // if collision flag is set to same value as
    // Original branch: enemy state, branch to make platforms fall
    if (!zero_flag) {
      carry_flag = false;
      a = (uint8_t)(a + 0x2 + carry_flag); // otherwise add 2 pixels to vertical position
      ram[(uint8_t)(Enemy_Y_Position + x)] = a; // of current platform and branch elsewhere
      StopPlatforms(); return; // to make platforms stop
    }
    
MakePlatformFall:
    goto InitPlatformFall; // make platforms fall
    
ChkOtherForFall:
    carry_flag = a >= ram[Enemy_Y_Position + y]; // check if other platform is above a certain point
    // Original branch: if not, branch elsewhere
    if (carry_flag) {
      zero_flag = x == ram[0x0]; // if collision flag is set to same value as
      if (zero_flag) { goto MakePlatformFall; } // enemy state, branch to make platforms fall
      carry_flag = false;
      a = (uint8_t)(a + 0x2 + carry_flag); // otherwise add 2 pixels to vertical position
      ram[Enemy_Y_Position + y] = a; // of other platform and branch elsewhere
      StopPlatforms(); return; // jump to stop movement and do not return
    }
    // ChkToMoveBalPlat:
    a = ram[(uint8_t)(Enemy_Y_Position + x)]; // save vertical position to stack
    ram[0x100 + sp--] = a;
    a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // get collision flag
    // Original branch: branch if collision
    if (neg_flag) {
      a = ram[Enemy_Y_MoveForce + x];
      carry_flag = false; // add $05 to contents of moveforce, whatever they be
      { uint16_t sum = a + 0x5 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
      ram[0x0] = a; // store here
      a = ram[(uint8_t)(Enemy_Y_Speed + x)];
      a = (uint8_t)(a + 0x0 + carry_flag); zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // add carry to vertical speed
      if (neg_flag) { goto PlatDn; } // branch if moving downwards
      if (!zero_flag) { goto PlatUp; } // branch elsewhere if moving upwards
      a = ram[0x0];
      carry_flag = a >= 0xb; // check if there's still a little force left
      if (!carry_flag) { goto PlatSt; } // if not enough, branch to stop movement
    } else {
      // ColFlg:
      zero_flag = a == ram[ObjectOffset]; // if collision flag matches
      if (zero_flag) { goto PlatDn; } // current enemy object offset, branch
    }
    
PlatUp:
    CALL(MovePlatformUp, 0xd49a); // do a sub to move upwards
    goto DoOtherPlatform; // jump ahead to remaining code
    
PlatSt:
    CALL(StopPlatforms, 0xd4a0); // do a sub to stop movement
    goto DoOtherPlatform; // jump ahead to remaining code
    
PlatDn:
    CALL(MovePlatformDown, 0xd4a6); // do a sub to move downwards
    
DoOtherPlatform:
    y = ram[(uint8_t)(Enemy_State + x)]; // get offset of other platform
    a = ram[0x100 + ++sp]; // get old vertical coordinate from stack
    carry_flag = true;
    a = (uint8_t)(a - ram[(uint8_t)(Enemy_Y_Position + x)] - !carry_flag); // get difference of old vs. new coordinate
    carry_flag = false;
    a = (uint8_t)(a + ram[Enemy_Y_Position + y] + carry_flag); // add difference to vertical coordinate of other
    ram[Enemy_Y_Position + y] = a; // platform to move it in the opposite direction
    a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // if no collision, skip this part here
    if (!neg_flag) {
      x = a; // put offset which collision occurred here
      CALL(PositionPlayerOnVPlat, 0xd4bc); // and use it to position player accordingly
    }
    // DrawEraseRope:
    y = ram[ObjectOffset]; // get enemy object offset
    a = ram[Enemy_Y_Speed + y]; // check to see if current platform is
    a |= ram[Enemy_Y_MoveForce + y]; zero_flag = a == 0; // moving at all
    // Original branch: if not, skip all of this and branch to leave
    if (!zero_flag) {
      x = ram[VRAM_Buffer1_Offset]; // get vram buffer offset
      carry_flag = x >= 0x20; // if offset beyond a certain point, go ahead
      if (carry_flag) { goto ExitRp; } // and skip this, branch to leave
      a = ram[Enemy_Y_Speed + y];
      ram[0x100 + sp--] = a; // save two copies of vertical speed to stack
      ram[0x100 + sp--] = a;
      CALL(SetupPlatformRope, 0xd4d5); // do a sub to figure out where to put new bg tiles
      ram[VRAM_Buffer1 + x] = ram[0x1]; // write name table address to vram buffer; first the high byte, then the low
      ram[(VRAM_Buffer1 + 1) + x] = ram[0x0];
      ram[(VRAM_Buffer1 + 2) + x] = 0x2; // set length for 2 bytes
      a = ram[Enemy_Y_Speed + y]; neg_flag = (a & 0x80) != 0; // if platform moving upwards, branch 
      // Original branch: to do something else
      if (!neg_flag) {
        ram[(VRAM_Buffer1 + 3) + x] = 0xa2; // otherwise put tile numbers for left
        ram[(VRAM_Buffer1 + 4) + x] = 0xa3; // and right sides of rope in vram buffer
      } else {
        // EraseR1:
        a = 0x24; // put blank tiles in vram buffer
        ram[(VRAM_Buffer1 + 3) + x] = a; // to erase rope
        ram[(VRAM_Buffer1 + 4) + x] = a;
      }
      // OtherRope:
      a = ram[Enemy_State + y]; // get offset of other platform from state
      y = a; // use as Y here
      a = ram[0x100 + ++sp]; // pull second copy of vertical speed from stack
      a ^= 0xff; // invert bits to reverse speed
      CALL(SetupPlatformRope, 0xd508); // do sub again to figure out where to put bg tiles  
      ram[(VRAM_Buffer1 + 5) + x] = ram[0x1]; // write name table address to vram buffer; this time we're doing putting tiles for
      ram[(VRAM_Buffer1 + 6) + x] = ram[0x0]; // the other platform
      ram[(VRAM_Buffer1 + 7) + x] = 0x2; // set length again for 2 bytes
      a = ram[0x100 + ++sp]; neg_flag = (a & 0x80) != 0; // pull first copy of vertical speed from stack
      // Original branch: if moving upwards (note inversion earlier), skip this
      if (neg_flag) {
        ram[(VRAM_Buffer1 + 8) + x] = 0xa2; // otherwise put tile numbers for left
        ram[(VRAM_Buffer1 + 9) + x] = 0xa3; // and right sides of rope in vram; transfer buffer
      } else {
        // EraseR2:
        a = 0x24; // put blank tiles in vram buffer
        ram[(VRAM_Buffer1 + 8) + x] = a; // to erase rope
        ram[(VRAM_Buffer1 + 9) + x] = a;
      }
      // EndRp:
      ram[(VRAM_Buffer1 + 10) + x] = 0x0; // put null terminator at the end
      a = ram[VRAM_Buffer1_Offset]; // add ten bytes to the vram buffer offset
      carry_flag = false; // and store
      a = (uint8_t)(a + 10 + carry_flag);
      ram[VRAM_Buffer1_Offset] = a;
    }
    
ExitRp:
    x = ram[ObjectOffset]; // get enemy object buffer offset and leave
    return;
    
InitPlatformFall:
    a = y; // move offset of other platform from Y to X
    x = a;
    CALL(GetEnemyOffscreenBits, 0xd59c); // get offscreen bits
    a = 0x6;
    CALL(SetupFloateyNumber, 0xd5a1); // award 1000 points to player
    ram[FloateyNum_X_Pos + x] = ram[Player_Rel_XPos]; // put floatey number coordinates where player is
    ram[FloateyNum_Y_Pos + x] = ram[Player_Y_Position];
    ram[(uint8_t)(Enemy_MovingDir + x)] = 0x1; // set moving direction as flag for; falling platforms
    StopPlatforms(); // fallthrough
    return;
  }
  // PlatformFall:
  a = y; // save offset for other platform to stack
  ram[0x100 + sp--] = a;
  CALL(MoveFallingPlatform, 0xd5bf); // make current platform fall
  a = ram[0x100 + ++sp];
  x = a; // pull offset from stack and save to X
  CALL(MoveFallingPlatform, 0xd5c4); // make other platform fall
  x = ram[ObjectOffset];
  a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // if player not standing on either platform,
  // Original branch: skip this part
  if (!neg_flag) {
    x = a; // transfer collision flag offset as offset to X
    CALL(PositionPlayerOnVPlat, 0xd5cf); // and position player appropriately
  }
  // ExPF:
  x = ram[ObjectOffset]; // get enemy object buffer offset and leave
  return;
}

// in: a, y; out: x
void SetupPlatformRope(void) {
  ram[0x100 + sp--] = a; // save second/third copy to stack
  a = ram[Enemy_X_Position + y]; // get horizontal coordinate
  carry_flag = false;
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add eight pixels
  x = ram[SecondaryHardMode]; zero_flag = x == 0; // if secondary hard mode flag set,
  // Original branch: use coordinate as-is
  if (zero_flag) {
    carry_flag = false;
    { uint16_t sum = a + 0x10 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // otherwise add sixteen more pixels
  }
  // GetLRp:
  ram[0x100 + sp--] = a; // save modified horizontal coordinate to stack
  a = ram[Enemy_PageLoc + y];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location
  ram[0x2] = a; // and save here
  a = ram[0x100 + ++sp]; // pull modified horizontal coordinate
  a &= 0b11110000; // from the stack, mask out low nybble
  a = a >> 1; // and shift three bits to the right
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a; // store result here as part of name table low byte
  x = ram[(uint8_t)(Enemy_Y_Position + y)]; // get vertical coordinate
  a = ram[0x100 + ++sp]; neg_flag = (a & 0x80) != 0; // get second/third copy of vertical speed from stack
  // Original branch: skip this part if moving downwards or not at all
  if (neg_flag) {
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x8 + carry_flag); // add eight to vertical coordinate and
    x = a; // save as X
  }
  // GetHRp:
  a = x; // move vertical coordinate to A
  x = ram[VRAM_Buffer1_Offset]; // get vram buffer offset
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // rotate d7 to d0 and d6 into carry
  ram[0x100 + sp--] = a; // save modified vertical coordinate to stack
  a = (uint8_t)(a << 1 | carry_flag); // rotate carry to d0, thus d7 and d6 are at 2 LSB
  a &= 0b00000011; // mask out all bits but d7 and d6, then set
  a |= 0b00100000; // d5 to get appropriate high byte of name table
  ram[0x1] = a; // address, then store
  a = ram[0x2]; // get saved page location from earlier
  a &= 0x1; // mask out all but LSB
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // shift twice to the left and save with the
  a |= ram[0x1]; // rest of the bits of the high byte, to get
  ram[0x1] = a; // the proper name table and the right place on it
  a = ram[0x100 + ++sp]; // get modified vertical coordinate from stack
  a &= 0b11100000; // mask out low nybble and LSB of high nybble
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag); // add to horizontal part saved here
  ram[0x0] = a; // save as name table low byte
  a = ram[Enemy_Y_Position + y];
  // if vertical position not below the
  // bottom of the screen, we're done, branch to leave
  if (a < 0xe8) {
    return;
  }
  a = ram[0x0];
  a &= 0b10111111; // mask out d6 of low byte of name table address
  ram[0x0] = a;
  // ExPRp:
  return; // leave!
}

// in: x, y; out: none
void StopPlatforms(void) {
  CALL(InitVStf, 0xd5b3); // initialize vertical speed and low byte
  ram[Enemy_Y_Speed + y] = a; // for both platforms and leave
  ram[Enemy_Y_MoveForce + y] = a;
  return;
}

// --------------------------------
// in: x; out: x
void YMovingPlatform(void) {
  a = ram[(uint8_t)(Enemy_Y_Speed + x)]; // if platform moving up or down, skip ahead to
  a |= ram[Enemy_Y_MoveForce + x]; zero_flag = a == 0; // check on other position
  if (zero_flag) {
    ram[Enemy_YMF_Dummy + x] = a; // initialize dummy variable
    a = ram[(uint8_t)(Enemy_Y_Position + x)];
    carry_flag = a >= ram[YPlatformTopYPos + x]; // if current vertical position => top position, branch
    if (carry_flag) { goto ChkYCenterPos; } // ahead of all this
    a = ram[FrameCounter];
    a &= 0b00000111; zero_flag = a == 0; // check for every eighth frame
    if (zero_flag) {
      ram[(uint8_t)(Enemy_Y_Position + x)]++; // increase vertical position every eighth frame
    }
    // SkipIY:
    ChkYPCollision(); return; // skip ahead to last part
  }
  
ChkYCenterPos:
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // if current vertical position < central position, branch
  carry_flag = a >= ram[(uint8_t)(YPlatformCenterYPos + x)]; // to slow ascent/move downwards
  if (carry_flag) {
    CALL(MovePlatformUp, 0xd5f7); // otherwise start slowing descent/moving upwards
    ChkYPCollision(); return;
  }
  // YMDown:
  CALL(MovePlatformDown, 0xd5fd); // start slowing ascent/moving downwards
  ChkYPCollision(); // fallthrough
  return;
}

// in: x; out: none
void ChkYPCollision(void) {
  a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // if collision flag not set here, branch
  // to leave
  if (neg_flag) {
    return;
  }
  CALL(PositionPlayerOnVPlat, 0xd605); // otherwise position player appropriately
  // ExYPl:
  return; // leave
}

// --------------------------------
// $00 - used as adder to position player hotizontally
// in: x; out: x
void XMovingPlatform(void) {
  a = 0xe; // load preset maximum value for secondary counter
  CALL(XMoveCntr_Platform, 0xd60b); // do a sub to increment counters for movement
  CALL(MoveWithXMCntrs, 0xd60e); // do a sub to move platform accordingly, and return value
  a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // if no collision with player,
  // branch ahead to leave
  if (neg_flag) {
    return;
  }
  PositionPlayerOnHPlat(); // fallthrough
  return;
}

// in: x; out: none
void PositionPlayerOnHPlat(void) {
  a = ram[Player_X_Position];
  carry_flag = false; // add saved value from second subroutine to
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // current player's position to position
  ram[Player_X_Position] = a; // player accordingly in horizontal position
  a = ram[Player_PageLoc]; // get player's page location
  y = ram[0x0]; neg_flag = (y & 0x80) != 0; // check to see if saved value here is positive or negative
  // Original branch: if negative, branch to subtract
  if (!neg_flag) {
    a = (uint8_t)(a + 0x0 + carry_flag); // otherwise add carry to page location
  } else {
    // PPHSubt:
    a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow from page location
  }
  // SetPVar:
  ram[Player_PageLoc] = a; // save result to player's page location
  ram[Platform_X_Scroll] = y; // put saved value from second sub here to be used later
  CALL(PositionPlayerOnVPlat, 0xd62f); // position player vertically and appropriately
  // ExXMP:
  return; // and we are done here
}

// --------------------------------
// in: x; out: x
void DropPlatform(void) {
  a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // if no collision between platform and player
  // occurred, just leave without moving anything
  if (neg_flag) {
    return;
  }
  CALL(MoveDropPlatform, 0xd638); // otherwise do a sub to move platform down very quickly
  CALL(PositionPlayerOnVPlat, 0xd63b); // do a sub to position player appropriately
  // ExDPl:
  return; // leave
}

// --------------------------------
// $00 - residual value from sub
// in: x; out: x
void RightPlatform(void) {
  CALL(MoveEnemyHorizontally, 0xd63f); // move platform with current horizontal speed, if any
  ram[0x0] = a; // store saved value here (residual code)
  a = ram[PlatformCollisionFlag + x]; neg_flag = (a & 0x80) != 0; // check collision flag, if no collision between player
  // and platform, branch ahead, leave speed unaltered
  if (neg_flag) {
    return;
  }
  ram[(uint8_t)(Enemy_X_Speed + x)] = 0x10; // otherwise set new speed (gets moving if motionless)
  CALL(PositionPlayerOnHPlat, 0xd64d); // use saved value from earlier sub to position player
  // ExRPl:
  return; // then leave
}

// --------------------------------
// in: x; out: none
void MoveLargeLiftPlat(void) {
  CALL(MoveLiftPlatforms, 0xd651); // execute common to all large and small lift platforms
  ChkYPCollision(); return; // branch to position player correctly
}

// in: x; out: none
void MoveSmallPlatform(void) {
  CALL(MoveLiftPlatforms, 0xd657); // execute common to all large and small lift platforms
  // branch to position player correctly
  // ChkSmallPlatCollision:
  a = ram[PlatformCollisionFlag + x]; zero_flag = a == 0; // get bounding box counter saved in collision flag
  // if none found, leave player position alone
  if (zero_flag) {
    return;
  }
  CALL(PositionPlayerOnS_Plat, 0xd678); // use to position player correctly
  // ExLiftP:
  return; // then leave
}

// in: x; out: none
void MoveLiftPlatforms(void) {
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set, skip all of this
  // and branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[Enemy_YMF_Dummy + x];
  carry_flag = false; // add contents of movement amount to whatever's here
  { uint16_t sum = a + ram[Enemy_Y_MoveForce + x] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[Enemy_YMF_Dummy + x] = a;
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // add whatever vertical speed is set to current
  a = (uint8_t)(a + ram[(uint8_t)(Enemy_Y_Speed + x)] + carry_flag); // vertical position plus carry to move up or down
  ram[(uint8_t)(Enemy_Y_Position + x)] = a; // and then leave
  return;
}

// -------------------------------------------------------------------------------------
// $00 - page location of extended left boundary
// $01 - extended left boundary position
// $02 - page location of extended right boundary
// $03 - extended right boundary position
// in: x; out: none
void OffscreenBoundsCheck(void) {
  a = ram[(uint8_t)(Enemy_ID + x)]; // check for cheep-cheep object
  // branch to leave if found
  if (a == FlyingCheepCheep) {
    return;
  }
  a = ram[ScreenLeft_X_Pos]; // get horizontal coordinate for left side of screen
  y = ram[(uint8_t)(Enemy_ID + x)];
  { uint8_t m = HammerBro; carry_flag = y >= m; zero_flag = y == m; } // check for hammer bro object
  if (!zero_flag) {
    { uint8_t m = PiranhaPlant; carry_flag = y >= m; zero_flag = y == m; } // check for piranha plant object
    if (!zero_flag) { goto ExtendLB; } // these two will be erased sooner than others if too far left
  }
  // LimitB:
  { uint16_t sum = a + 0x38 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add 56 pixels to coordinate if hammer bro or piranha plant
  
ExtendLB:
  { uint16_t diff = (uint16_t)(a - 0x48 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract 72 pixels regardless of enemy object
  ram[0x1] = a; // store result here
  a = ram[ScreenLeft_PageLoc];
  { uint16_t diff = (uint16_t)(a - 0x0 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract borrow from page location of left side
  ram[0x0] = a; // store result here
  a = ram[ScreenRight_X_Pos]; // add 72 pixels to the right side horizontal coordinate
  { uint16_t sum = a + 0x48 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x3] = a; // store result here
  a = ram[ScreenRight_PageLoc];
  a = (uint8_t)(a + 0x0 + carry_flag); // then add the carry to the page location
  ram[0x2] = a; // and store result here
  a = ram[(uint8_t)(Enemy_X_Position + x)]; // compare horizontal coordinate of the enemy object
  carry_flag = a >= ram[0x1]; // to modified horizontal left edge coordinate to get carry
  a = ram[(uint8_t)(Enemy_PageLoc + x)];
  a = (uint8_t)(a - ram[0x0] - !carry_flag); neg_flag = (a & 0x80) != 0; // then subtract it from the page coordinate of the enemy object
  // Original branch: if enemy object is too far left, branch to erase it
  if (!neg_flag) {
    a = ram[(uint8_t)(Enemy_X_Position + x)]; // compare horizontal coordinate of the enemy object
    carry_flag = a >= ram[0x3]; // to modified horizontal right edge coordinate to get carry
    a = ram[(uint8_t)(Enemy_PageLoc + x)];
    a = (uint8_t)(a - ram[0x2] - !carry_flag); neg_flag = (a & 0x80) != 0; // then subtract it from the page coordinate of the enemy object
    // if enemy object is on the screen, leave, do not erase enemy
    if (neg_flag) {
      return;
    }
    a = ram[(uint8_t)(Enemy_State + x)]; // if at this point, enemy is offscreen to the right, so check
    // if in state used by spiny's egg, do not erase
    if (a == HammerBro) {
      return;
    }
    // if piranha plant, do not erase
    if (y == PiranhaPlant) {
      return;
    }
    // if flagpole flag, do not erase
    if (y == FlagpoleFlagObject) {
      return;
    }
    // if star flag, do not erase
    if (y == StarFlagObject) {
      return;
    }
    // if jumpspring, do not erase
    // erase all others too far to the right
    if (y == JumpspringObject) {
      return;
    }
  }
  // TooFar:
  CALL(EraseEnemyObject, 0xd6d4); // erase object if necessary
  // ExScrnBd:
  return; // leave
}

// -------------------------------------------------------------------------------------
// some unused space
// -------------------------------------------------------------------------------------
// $01 - enemy buffer offset
// in: x; out: x
void FireballEnemyCollision(void) {
  a = ram[(uint8_t)(Fireball_State + x)]; zero_flag = a == 0; // check to see if fireball state is set at all
  // Original branch: branch to leave if not
  if (!zero_flag) {
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) { goto ExitFBallEnemy; } // branch to leave also if d7 in state is set
    a = ram[FrameCounter];
    carry_flag = (a & 1) != 0; a = a >> 1; // get LSB of frame counter
    if (carry_flag) { goto ExitFBallEnemy; } // branch to leave if set (do routine every other frame)
    a = x;
    a = (uint8_t)(a << 1); // multiply fireball offset by four
    a = (uint8_t)(a << 1);
    carry_flag = false;
    a = (uint8_t)(a + 0x1c + carry_flag); // then add $1c or 28 bytes to it
    y = a; // to use fireball's bounding box coordinates 
    x = 0x4;
    do {
      // FireballEnemyCDLoop:
      ram[0x1] = x; // store enemy object offset here
      a = y;
      ram[0x100 + sp--] = a; // push fireball offset to the stack
      a = ram[(uint8_t)(Enemy_State + x)];
      a &= 0b00100000; zero_flag = a == 0; // check to see if d5 is set in enemy state
      // Original branch: if so, skip to next enemy slot
      if (zero_flag) {
        a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check to see if buffer flag is set
        if (zero_flag) { goto NoFToECol; } // if not, skip to next enemy slot
        a = ram[(uint8_t)(Enemy_ID + x)]; // check enemy identifier
        // Original branch: if < $24, branch to check further
        if (a >= 0x24) {
          carry_flag = a >= 0x2b;
          if (!carry_flag) { goto NoFToECol; } // if in range $24-$2a, skip to next enemy slot
        }
        // GoombaDie:
        // check for goomba identifier
        // Original branch: if not found, continue with code
        if (a == Goomba) {
          a = ram[(uint8_t)(Enemy_State + x)]; // otherwise check for defeated state
          carry_flag = a >= 0x2; // if stomped or otherwise defeated,
          if (carry_flag) { goto NoFToECol; } // skip to next enemy slot
        }
        // NotGoomba:
        a = ram[EnemyOffscrBitsMasked + x]; zero_flag = a == 0; // if any masked offscreen bits set,
        if (!zero_flag) { goto NoFToECol; } // skip to next enemy slot
        a = x;
        a = (uint8_t)(a << 1); // otherwise multiply enemy offset by four
        a = (uint8_t)(a << 1);
        carry_flag = false;
        a = (uint8_t)(a + 0x4 + carry_flag); // add 4 bytes to it
        x = a; // to use enemy's bounding box coordinates
        CALL(SprObjectCollisionCore, 0xd71e); // do fireball-to-enemy collision detection
        x = ram[ObjectOffset]; // return fireball's original offset
        if (!carry_flag) { goto NoFToECol; } // if carry clear, no collision, thus do next enemy slot
        ram[(uint8_t)(Fireball_State + x)] = 0b10000000; // set d7 in enemy state
        x = ram[0x1]; // get enemy offset
        CALL(HandleEnemyFBallCol, 0xd72b); // jump to handle fireball to enemy collision
      }
      
NoFToECol:
      a = ram[0x100 + ++sp]; // pull fireball offset from stack
      y = a; // put it in Y
      x = ram[0x1]; // get enemy object offset
      x--; neg_flag = (x & 0x80) != 0; // decrement it
    } while (!neg_flag); // loop back until collision detection done on all enemies
  }
  
ExitFBallEnemy:
  x = ram[ObjectOffset]; // get original fireball offset and leave
  return;
}

// in: x; out: none
void HandleEnemyFBallCol(void) {
  CALL(RelativeEnemyPosition, 0xd740); // get relative coordinate of enemy
  x = ram[0x1]; // get current enemy object offset
  a = ram[(uint8_t)(Enemy_Flag + x)]; neg_flag = (a & 0x80) != 0; // check buffer flag for d7 set
  // Original branch: branch if not set to continue
  if (neg_flag) {
    a &= 0b00001111; // otherwise mask out high nybble and
    x = a; // use low nybble as enemy offset
    a = ram[(uint8_t)(Enemy_ID + x)];
    zero_flag = a == Bowser; // check enemy identifier for bowser
    if (zero_flag) { goto HurtBowser; } // branch if found
    x = ram[0x1]; // otherwise retrieve current enemy offset
  }
  // ChkBuzzyBeetle:
  a = ram[(uint8_t)(Enemy_ID + x)];
  // check for buzzy beetle
  // branch if found to leave (buzzy beetles fireproof)
  if (a == BuzzyBeetle) {
    return;
  }
  zero_flag = a == Bowser; // check for bowser one more time (necessary if d7 of flag was clear)
  if (!zero_flag) { goto ChkOtherEnemies; } // if not found, branch to check other enemies
  
HurtBowser:
  ram[BowserHitPoints]--; zero_flag = ram[BowserHitPoints] == 0; // decrement bowser's hit points
  // if bowser still has hit points, branch to leave
  if (!zero_flag) {
    return;
  }
  CALL(InitVStf, 0xd763); // otherwise do sub to init vertical speed and movement force
  ram[(uint8_t)(Enemy_X_Speed + x)] = a; // initialize horizontal speed
  ram[EnemyFrenzyBuffer] = a; // init enemy frenzy buffer
  ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xfe; // set vertical speed to make defeated bowser jump a little
  y = ram[WorldNumber]; // use world number as offset
  ram[(uint8_t)(Enemy_ID + x)] = ROM(BowserIdentities + y); // get enemy identifier to replace bowser with; set as new enemy identifier
  a = 0x20; // set A to use starting value for state
  // check to see if using offset of 3 or more
  // Original branch: branch if so
  if (y < 0x3) {
    a |= 0x3; // otherwise add 3 to enemy state
  }
  // SetDBSte:
  ram[(uint8_t)(Enemy_State + x)] = a; // set defeated enemy state
  ram[Square2SoundQueue] = Sfx_BowserFall; // load bowser defeat sound
  x = ram[0x1]; // get enemy offset
  a = 0x9; // award 5000 points to player for defeating bowser
  EnemySmackScore(); return; // unconditional branch to award points
  
ChkOtherEnemies:
  // branch to leave if bullet bill (frenzy variant) 
  if (a == BulletBill_FrenzyVar) {
    return;
  }
  // branch to leave if podoboo
  if (a == Podoboo) {
    return;
  }
  // branch to leave if identifier => $15
  if (a >= 0x15) {
    return;
  }
  ShellOrBlockDefeat(); // fallthrough
  return;
}

// in: x; out: y
void ShellOrBlockDefeat(void) {
  a = ram[(uint8_t)(Enemy_ID + x)]; // check for piranha plant
  { uint8_t m = PiranhaPlant; carry_flag = a >= m; zero_flag = a == m; }
  // Original branch: branch if not found
  if (zero_flag) {
    a = ram[(uint8_t)(Enemy_Y_Position + x)];
    a = (uint8_t)(a + 0x18 + carry_flag); // add 24 pixels to enemy object's vertical position
    ram[(uint8_t)(Enemy_Y_Position + x)] = a;
  }
  // StnE:
  CALL(ChkToStunEnemies, 0xd7a3); // do yet another sub
  a = ram[(uint8_t)(Enemy_State + x)];
  a &= 0b00011111; // mask out 2 MSB of enemy object's state
  a |= 0b00100000; // set d5 to defeat enemy and save as new state
  ram[(uint8_t)(Enemy_State + x)] = a;
  a = 0x2; // award 200 points by default
  y = ram[(uint8_t)(Enemy_ID + x)]; // check for hammer bro
  // Original branch: branch if not found
  if (y == HammerBro) {
    a = 0x6; // award 1000 points for hammer bro
  }
  // GoombaPoints:
  // check for goomba
  // branch if not found
  if (y != Goomba) {
    EnemySmackScore();
    return;
  }
  a = 0x1; // award 100 points for goomba
  EnemySmackScore(); // fallthrough
  return;
}

// in: a, x; out: none
void EnemySmackScore(void) {
  CALL(SetupFloateyNumber, 0xd7be); // update necessary score variables
  ram[Square1SoundQueue] = Sfx_EnemySmack; // play smack enemy sound
  // ExHCF:
  return; // and now let's leave
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void PlayerHammerCollision(void) {
  a = ram[FrameCounter]; // get frame counter
  carry_flag = (a & 1) != 0; a = a >> 1; // shift d0 into carry
  // branch to leave if d0 not set to execute every other frame
  if (!carry_flag) {
    return;
  }
  a = ram[TimerControl]; // if either master timer control
  a |= ram[Misc_OffscreenBits]; zero_flag = a == 0; // or any offscreen bits for hammer are set,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  a = x;
  a = (uint8_t)(a << 1); // multiply misc object offset by four
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + 0x24 + carry_flag); // add 36 or $24 bytes to get proper offset
  y = a; // for misc object bounding box coordinates
  CALL(PlayerCollisionCore, 0xd7da); // do player-to-hammer collision detection
  x = ram[ObjectOffset]; // get misc object offset
  // Original branch: if no collision, then branch
  if (carry_flag) {
    a = ram[Misc_Collision_Flag + x]; zero_flag = a == 0; // otherwise read collision flag
    // if collision flag already set, branch to leave
    if (!zero_flag) {
      return;
    }
    ram[Misc_Collision_Flag + x] = 0x1; // otherwise set collision flag now
    a = ram[(uint8_t)(Misc_X_Speed + x)];
    a ^= 0xff; // get two's compliment of
    carry_flag = false; // hammer's horizontal speed
    a = (uint8_t)(a + 0x1 + carry_flag);
    ram[(uint8_t)(Misc_X_Speed + x)] = a; // set to send hammer flying the opposite direction
    a = ram[StarInvincibleTimer]; zero_flag = a == 0; // if star mario invincibility timer set,
    // branch to leave
    if (!zero_flag) {
      return;
    }
    InjurePlayer(); return; // otherwise jump to hurt player, do not return
  }
  // ClHCol:
  ram[Misc_Collision_Flag + x] = 0x0; // clear collision flag
  // ExPHC:
  return;
}

// in: x; out: x
void PlayerEnemyCollision(void) {
  goto PlayerEnemyCollision;
  // -------------------------------------------------------------------------------------
  
HandlePowerUpCollision:
  CALL(EraseEnemyObject, 0xd802); // erase the power-up object
  a = 0x6;
  CALL(SetupFloateyNumber, 0xd807); // award 1000 points to player by default
  ram[Square2SoundQueue] = Sfx_PowerUpGrab; // play the power-up sound
  a = ram[PowerUpType]; // check power-up type
  // Original branch: if mushroom or fire flower, branch
  if (a >= 0x2) {
    zero_flag = a == 0x3;
    if (zero_flag) { goto SetFor1Up; } // if 1-up mushroom, branch
    ram[StarInvincibleTimer] = 0x23; // otherwise set star mario invincibility; timer, and load the star mario music
    ram[AreaMusicQueue] = StarPowerMusic; // into the area music queue, then leave
    return;
  }
  // Shroom_Flower_PUp:
  a = ram[PlayerStatus]; zero_flag = a == 0; // if player status = small, branch
  if (zero_flag) { goto UpToSuper; }
  // if player status not super, leave
  if (a != 0x1) {
    return;
  }
  x = ram[ObjectOffset]; // get enemy offset, not necessary
  ram[PlayerStatus] = 0x2; // set player status to fiery
  CALL(GetPlayerColors, 0xd832); // run sub to change colors of player
  x = ram[ObjectOffset]; // get enemy offset again, and again not necessary
  a = 0xc; // set value to be used by subroutine tree (fiery)
  goto UpToFiery; // jump to set values accordingly
  
SetFor1Up:
  ram[FloateyNum_Control + x] = 0xb; // change 1000 points into 1-up instead; and then leave
  return;
  
UpToSuper:
  ram[PlayerStatus] = 0x1; // set player status to super
  a = 0x9; // set value to be used by subroutine tree (super)
  
UpToFiery:
  y = 0x0; // set value to be used as new player state
  CALL(SetPRout, 0xd84b); // set values to stop certain things in motion
  // NoPUp:
  return;
  // --------------------------------
  
PlayerEnemyCollision:
  a = ram[FrameCounter]; // check counter for d0 set
  carry_flag = (a & 1) != 0; a = a >> 1;
  // if set, branch to leave
  if (carry_flag) {
    return;
  }
  CALL(CheckPlayerVertical, 0xd85a); // if player object is completely offscreen or
  // if down past 224th pixel row, branch to leave
  if (carry_flag) {
    return;
  }
  a = ram[EnemyOffscrBitsMasked + x]; zero_flag = a == 0; // if current enemy is offscreen by any amount,
  // go ahead and branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[GameEngineSubroutine];
  // if not set to run player control routine
  // on next frame, branch to leave
  if (a != 0x8) {
    return;
  }
  a = ram[(uint8_t)(Enemy_State + x)];
  a &= 0b00100000; zero_flag = a == 0; // if enemy state has d5 set, branch to leave
  if (!zero_flag) {
    return;
  }
  CALL(GetEnemyBoundBoxOfs, 0xd870); // get bounding box offset for current enemy object
  CALL(PlayerCollisionCore, 0xd873); // do collision detection on player vs. enemy
  x = ram[ObjectOffset]; // get enemy object buffer offset
  // Original branch: if collision, branch past this part here
  if (!carry_flag) {
    a = ram[Enemy_CollisionBits + x];
    a &= 0b11111110; // otherwise, clear d0 of current enemy object's
    ram[Enemy_CollisionBits + x] = a; // collision bit
    // NoPECol:
    return;
  }
  // CheckForPUpCollision:
  y = ram[(uint8_t)(Enemy_ID + x)];
  // check for power-up object
  // Original branch: if not found, branch to next part
  if (y == PowerUpObject) {
    goto HandlePowerUpCollision; // otherwise, unconditional jump backwards
  }
  // EColl:
  a = ram[StarInvincibleTimer]; zero_flag = a == 0; // if star mario invincibility timer expired,
  // Original branch: perform task here, otherwise kill enemy like
  if (!zero_flag) {
    ShellOrBlockDefeat(); return; // hit with a shell, or from beneath
  }
  // HandlePECollisions:
  a = ram[Enemy_CollisionBits + x]; // check enemy collision bits for d0 set
  a &= 0b00000001; // or for being offscreen at all
  a |= ram[EnemyOffscrBitsMasked + x]; zero_flag = a == 0;
  // branch to leave if either is true
  if (!zero_flag) {
    return;
  }
  a = 0x1;
  a |= ram[Enemy_CollisionBits + x]; // otherwise set d0 now
  ram[Enemy_CollisionBits + x] = a;
  // branch if spiny
  if (y != Spiny) {
    // branch if piranha plant
    if (y == PiranhaPlant) {
      InjurePlayer();
      return;
    }
    // branch if podoboo
    if (y == Podoboo) {
      InjurePlayer();
      return;
    }
    zero_flag = y == BulletBill_CannonVar; // branch if bullet bill
    if (zero_flag) { goto ChkForPlayerInjury; }
    // branch if object => $15
    if (y >= 0x15) {
      InjurePlayer();
      return;
    }
    a = ram[AreaType]; zero_flag = a == 0; // branch if water type level
    if (zero_flag) {
      InjurePlayer();
      return;
    }
    a = ram[(uint8_t)(Enemy_State + x)]; // branch if d7 of enemy state was set
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    if (carry_flag) { goto ChkForPlayerInjury; }
    a = ram[(uint8_t)(Enemy_State + x)]; // mask out all but 3 LSB of enemy state
    a &= 0b00000111;
    carry_flag = a >= 0x2; // branch if enemy is in normal or falling state
    if (!carry_flag) { goto ChkForPlayerInjury; }
    a = ram[(uint8_t)(Enemy_ID + x)]; // branch to leave if goomba in defeated state
    if (a == Goomba) {
      return;
    }
    ram[Square1SoundQueue] = Sfx_EnemySmack; // play smack enemy sound
    a = ram[(uint8_t)(Enemy_State + x)]; // set d7 in enemy state, thus become moving shell
    a |= 0b10000000;
    ram[(uint8_t)(Enemy_State + x)] = a;
    CALL(EnemyFacePlayer, 0xd8df); // set moving direction and get offset
    ram[(uint8_t)(Enemy_X_Speed + x)] = ROM(KickedShellXSpdData + y); // load and set horizontal speed data with offset
    a = 0x3; // add three to whatever the stomp counter contains
    carry_flag = false; // to give points for kicking the shell
    a = (uint8_t)(a + ram[StompChainCounter] + carry_flag);
    y = ram[(EnemyIntervalTimer + x) & 0x7ff]; // check shell enemy's timer
    // if above a certain point, branch using the points
    // Original branch: data obtained from the stomp counter + 3
    if (y < 0x3) {
      a = ROM(KickedShellPtsData + y); // otherwise, set points based on proximity to timer expiration
    }
    // KSPts:
    CALL(SetupFloateyNumber, 0xd8f7); // set values for floatey number now
    // ExPEC:
    return; // leave!!!
  }
  
ChkForPlayerInjury:
  a = ram[Player_Y_Speed]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // check player's vertical speed
  // Original branch: perform procedure below if player moving upwards
  if (!neg_flag) {
    if (!zero_flag) { goto EnemyStomped; } // or not at all, and branch elsewhere if moving downwards
  }
  // ChkInj:
  a = ram[(uint8_t)(Enemy_ID + x)]; // branch if enemy object < $07
  if (a >= Bloober) {
    a = ram[Player_Y_Position]; // add 12 pixels to player's vertical position
    carry_flag = false;
    a = (uint8_t)(a + 0xc + carry_flag);
    carry_flag = a >= ram[(uint8_t)(Enemy_Y_Position + x)]; // compare modified player's position to enemy's position
    if (!carry_flag) { goto EnemyStomped; } // branch if this player's position above (less than) enemy's
  }
  // ChkETmrs:
  a = ram[StompTimer]; zero_flag = a == 0; // check stomp timer
  // Original branch: branch if set
  if (zero_flag) {
    a = ram[InjuryTimer]; zero_flag = a == 0; // check to see if injured invincibility timer still
    // counting down, and branch elsewhere to leave if so
    if (!zero_flag) {
      ExInjColRoutines();
      return;
    }
    a = ram[Player_Rel_XPos];
    carry_flag = a >= ram[Enemy_Rel_XPos]; // if player's relative position to the left of enemy's
    // Original branch: relative position, branch here
    if (carry_flag) {
      goto ChkEnemyFaceRight; // otherwise do a jump here
    }
    // TInjE:
    a = ram[(uint8_t)(Enemy_MovingDir + x)]; // if enemy moving towards the left,
    // branch, otherwise do a jump here
    // to turn the enemy around
    if (a != 0x1) {
      InjurePlayer();
      return;
    }
    goto LInj;
  }
  
EnemyStomped:
  a = ram[(uint8_t)(Enemy_ID + x)]; // check for spiny, branch to hurt player
  // if found
  if (a == Spiny) {
    InjurePlayer();
    return;
  }
  ram[Square1SoundQueue] = Sfx_EnemyStomp; // otherwise play stomp/swim sound
  a = ram[(uint8_t)(Enemy_ID + x)];
  y = 0x0; // initialize points data offset for stomped enemies
  // branch for cheep-cheep
  if (a != FlyingCheepCheep) {
    zero_flag = a == BulletBill_FrenzyVar; // branch for either bullet bill object
    if (zero_flag) { goto EnemyStompedPts; }
    zero_flag = a == BulletBill_CannonVar;
    if (zero_flag) { goto EnemyStompedPts; }
    zero_flag = a == Podoboo; // branch for podoboo (this branch is logically impossible
    if (zero_flag) { goto EnemyStompedPts; } // for cpu to take due to earlier checking of podoboo)
    y++; // increment points data offset
    zero_flag = a == HammerBro; // branch for hammer bro
    if (zero_flag) { goto EnemyStompedPts; }
    y++; // increment points data offset
    zero_flag = a == Lakitu; // branch for lakitu
    if (zero_flag) { goto EnemyStompedPts; }
    y++; // increment points data offset
    zero_flag = a == Bloober; // branch if NOT bloober
    if (!zero_flag) { goto ChkForDemoteKoopa; }
  }
  
EnemyStompedPts:
  a = ROM(StompedEnemyPtsData + y); // load points data using offset in Y
  CALL(SetupFloateyNumber, 0xd99b); // run sub to set floatey number controls
  a = ram[(uint8_t)(Enemy_MovingDir + x)];
  ram[0x100 + sp--] = a; // save enemy movement direction to stack
  CALL(SetStun, 0xd9a1); // run sub to kill enemy
  a = ram[0x100 + ++sp];
  ram[(uint8_t)(Enemy_MovingDir + x)] = a; // return enemy movement direction from stack
  ram[(uint8_t)(Enemy_State + x)] = 0b00100000; // set d5 in enemy state
  CALL(InitVStf, 0xd9ab); // nullify vertical speed, physics-related thing,
  ram[(uint8_t)(Enemy_X_Speed + x)] = a; // and horizontal speed
  ram[Player_Y_Speed] = 0xfd; // set player's vertical speed, to give bounce
  return;
  
ChkForDemoteKoopa:
  // branch elsewhere if enemy object < $09
  if (a >= 0x9) {
    a &= 0b00000001; // demote koopa paratroopas to ordinary troopas
    ram[(uint8_t)(Enemy_ID + x)] = a;
    y = 0x0; // return enemy to normal state
    ram[(uint8_t)(Enemy_State + x)] = y;
    a = 0x3; // award 400 points to the player
    CALL(SetupFloateyNumber, 0xd9c3);
    CALL(InitVStf, 0xd9c6); // nullify physics-related thing and vertical speed
    CALL(EnemyFacePlayer, 0xd9c9); // turn enemy around if necessary
    ram[(uint8_t)(Enemy_X_Speed + x)] = ROM(DemotedKoopaXSpdData + y); // set appropriate moving speed based on direction
  } else {
    // HandleStompedShellE:
    ram[(uint8_t)(Enemy_State + x)] = 0x4; // set defeated state for enemy
    ram[StompChainCounter]++; // increment the stomp counter
    a = ram[StompChainCounter]; // add whatever is in the stomp counter
    carry_flag = false; // to whatever is in the stomp timer
    a = (uint8_t)(a + ram[StompTimer] + carry_flag);
    CALL(SetupFloateyNumber, 0xd9e4); // award points accordingly
    ram[StompTimer]++; // increment stomp timer of some sort
    y = ram[PrimaryHardMode]; // check primary hard mode flag
    ram[(EnemyIntervalTimer + x) & 0x7ff] = ROM(RevivalRateData + y); // load timer setting according to flag; set as enemy timer to revive stomped enemy
  }
  // SBnce:
  ram[Player_Y_Speed] = 0xfc; // set player's vertical speed for bounce; and then leave!!!
  return;
  
ChkEnemyFaceRight:
  a = ram[(uint8_t)(Enemy_MovingDir + x)]; // check to see if enemy is moving to the right
  // Original branch: if not, branch
  if (a == 0x1) {
    InjurePlayer(); return; // otherwise go back to hurt player
  }
  
LInj:
  CALL(EnemyTurnAround, 0xda01); // turn the enemy around, if necessary
  InjurePlayer(); return; // go back to hurt player
}

// in: none; out: x
void InjurePlayer(void) {
  a = ram[InjuryTimer]; zero_flag = a == 0; // check again to see if injured invincibility timer is
  // at zero, and branch to leave if so
  if (!zero_flag) {
    ExInjColRoutines();
    return;
  }
  ForceInjury(); // fallthrough
  return;
}

// in: a; out: x
void ForceInjury(void) {
  x = ram[PlayerStatus]; zero_flag = x == 0; // check player's status
  if (zero_flag) { goto KillPlayer; } // branch if small
  ram[PlayerStatus] = a; // otherwise set player's status to small
  a = 0x8;
  ram[InjuryTimer] = a; // set injured invincibility timer
  a = (uint8_t)(a << 1);
  ram[Square1SoundQueue] = a; // play pipedown/injury sound
  CALL(GetPlayerColors, 0xd943); // change player's palette if necessary
  a = 0xa; // set subroutine to run on next frame
  
SetKRout:
  y = 0x1; // set new player state
  SetPRout(); // fallthrough
  return;
  
KillPlayer:
  ram[Player_X_Speed] = x; // halt player's horizontal movement by initializing speed
  x++;
  ram[EventMusicQueue] = x; // set event music queue to death music
  ram[Player_Y_Speed] = 0xfc; // set new vertical speed
  a = 0xb; // set subroutine to run on next frame
  goto SetKRout; // branch to set player's state and other things
}

// in: a, y; out: x
void SetPRout(void) {
  ram[GameEngineSubroutine] = a; // load new value to run subroutine on next frame
  ram[Player_State] = y; // store new player state
  y = 0xff;
  ram[TimerControl] = y; // set master timer control flag to halt timers
  y++;
  ram[ScrollAmount] = y; // initialize scroll speed
  ExInjColRoutines(); // fallthrough
  return;
}

// in: none; out: x
void ExInjColRoutines(void) {
  x = ram[ObjectOffset]; // get enemy offset and leave
  return;
}

// in: x; out: y
void EnemyFacePlayer(void) {
  y = 0x1; // set to move right by default
  CALL(PlayerEnemyDiff, 0xda09); // get horizontal difference between player and enemy
  // Original branch: if enemy is to the right of player, do not increment
  if (neg_flag) {
    y++; // otherwise, increment to set to move to the left
  }
  // SFcRt:
  ram[(uint8_t)(Enemy_MovingDir + x)] = y; // set moving direction here
  y--; // then decrement to use as a proper offset
  return;
}

// in: a, x; out: a
void SetupFloateyNumber(void) {
  ram[FloateyNum_Control + x] = a; // set number of points control for floatey numbers
  ram[FloateyNum_Timer + x] = 0x30; // set timer for floatey numbers
  ram[FloateyNum_Y_Pos + x] = ram[(uint8_t)(Enemy_Y_Position + x)]; // set vertical coordinate
  a = ram[Enemy_Rel_XPos];
  ram[FloateyNum_X_Pos + x] = a; // set horizontal coordinate and leave
  // ExSFN:
  return;
}

// -------------------------------------------------------------------------------------
// $01 - used to hold enemy offset for second enemy
// in: x; out: x
void EnemiesCollision(void) {
  a = ram[FrameCounter]; // check counter for d0 set
  carry_flag = (a & 1) != 0; a = a >> 1;
  // if d0 not set, leave
  if (!carry_flag) {
    return;
  }
  a = ram[AreaType]; zero_flag = a == 0;
  // if water area type, leave
  if (zero_flag) {
    return;
  }
  a = ram[(uint8_t)(Enemy_ID + x)];
  // if enemy object => $15, branch to leave
  if (a < 0x15) {
    zero_flag = a == Lakitu; // if lakitu, branch to leave
    if (zero_flag) { goto ExitECRoutine; }
    zero_flag = a == PiranhaPlant; // if piranha plant, branch to leave
    if (zero_flag) { goto ExitECRoutine; }
    a = ram[EnemyOffscrBitsMasked + x]; zero_flag = a == 0; // if masked offscreen bits nonzero, branch to leave
    if (!zero_flag) { goto ExitECRoutine; }
    CALL(GetEnemyBoundBoxOfs, 0xda52); // otherwise, do sub, get appropriate bounding box offset for
    x--; neg_flag = (x & 0x80) != 0; // first enemy we're going to compare, then decrement for second
    if (neg_flag) { goto ExitECRoutine; } // branch to leave if there are no other enemies
    do {
      // ECLoop:
      ram[0x1] = x; // save enemy object buffer offset for second enemy here
      a = y; // save first enemy's bounding box offset to stack
      ram[0x100 + sp--] = a;
      a = ram[(uint8_t)(Enemy_Flag + x)]; zero_flag = a == 0; // check enemy object enable flag
      // Original branch: branch if flag not set
      if (!zero_flag) {
        a = ram[(uint8_t)(Enemy_ID + x)];
        carry_flag = a >= 0x15; // check for enemy object => $15
        if (carry_flag) { goto ReadyNextEnemy; } // branch if true
        zero_flag = a == Lakitu;
        if (zero_flag) { goto ReadyNextEnemy; } // branch if enemy object is lakitu
        zero_flag = a == PiranhaPlant;
        if (zero_flag) { goto ReadyNextEnemy; } // branch if enemy object is piranha plant
        a = ram[EnemyOffscrBitsMasked + x]; zero_flag = a == 0;
        if (!zero_flag) { goto ReadyNextEnemy; } // branch if masked offscreen bits set
        a = x; // get second enemy object's bounding box offset
        a = (uint8_t)(a << 1); // multiply by four, then add four
        a = (uint8_t)(a << 1);
        carry_flag = false;
        a = (uint8_t)(a + 0x4 + carry_flag);
        x = a; // use as new contents of X
        CALL(SprObjectCollisionCore, 0xda7a); // do collision detection using the two enemies here
        x = ram[ObjectOffset]; // use first enemy offset for X
        y = ram[0x1]; // use second enemy offset for Y
        // Original branch: if carry clear, no collision, branch ahead of this
        if (carry_flag) {
          a = ram[(uint8_t)(Enemy_State + x)];
          a |= ram[Enemy_State + y]; // check both enemy states for d7 set
          a &= 0b10000000; zero_flag = a == 0;
          // Original branch: branch if at least one of them is set
          if (zero_flag) {
            a = ram[Enemy_CollisionBits + y]; // load first enemy's collision-related bits
            a &= ROM(SetBitsMask + x); zero_flag = a == 0; // check to see if bit connected to second enemy is
            if (!zero_flag) { goto ReadyNextEnemy; } // already set, and move onto next enemy slot if set
            a = ram[Enemy_CollisionBits + y];
            a |= ROM(SetBitsMask + x); // if the bit is not set, set it now
            ram[Enemy_CollisionBits + y] = a;
          }
          // YesEC:
          CALL(ProcEnemyCollisions, 0xda9d); // react according to the nature of collision
          goto ReadyNextEnemy; // move onto next enemy slot
        }
        // NoEnemyCollision:
        a = ram[Enemy_CollisionBits + y]; // load first enemy's collision-related bits
        a &= ROM(ClearBitsMask + x); // clear bit connected to second enemy
        ram[Enemy_CollisionBits + y] = a; // then move onto next enemy slot
      }
      
ReadyNextEnemy:
      a = ram[0x100 + ++sp]; // get first enemy's bounding box offset from the stack
      y = a; // use as Y again
      x = ram[0x1]; // get and decrement second enemy's object buffer offset
      x--; neg_flag = (x & 0x80) != 0;
    } while (!neg_flag); // loop until all enemy slots have been checked
  }
  
ExitECRoutine:
  x = ram[ObjectOffset]; // get enemy object buffer offset
  return; // leave
}

// in: x, y; out: none
void ProcEnemyCollisions(void) {
  a = ram[Enemy_State + y]; // check both enemy states for d5 set
  a |= ram[(uint8_t)(Enemy_State + x)];
  a &= 0b00100000; zero_flag = a == 0; // if d5 is set in either state, or both, branch
  // to leave and do nothing else at this point
  if (!zero_flag) {
    return;
  }
  a = ram[(uint8_t)(Enemy_State + x)];
  // if second enemy state < $06, branch elsewhere
  if (a >= 0x6) {
    a = ram[(uint8_t)(Enemy_ID + x)]; // check second enemy identifier for hammer bro
    // if hammer bro found in alt state, branch to leave
    if (a == HammerBro) {
      return;
    }
    a = ram[Enemy_State + y]; // check first enemy state for d7 set
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
    // Original branch: branch if d7 is clear
    if (carry_flag) {
      a = 0x6;
      CALL(SetupFloateyNumber, 0xdad3); // award 1000 points for killing enemy
      CALL(ShellOrBlockDefeat, 0xdad6); // then kill enemy, then load
      y = ram[0x1]; // original offset of second enemy
    }
    // ShellCollisions:
    a = y; // move Y to X
    x = a;
    CALL(ShellOrBlockDefeat, 0xdadd); // kill second enemy
    x = ram[ObjectOffset];
    a = ram[ShellChainCounter + x]; // get chain counter for shell
    carry_flag = false;
    a = (uint8_t)(a + 0x4 + carry_flag); // add four to get appropriate point offset
    x = ram[0x1];
    CALL(SetupFloateyNumber, 0xdaea); // award appropriate number of points for second enemy
    x = ram[ObjectOffset]; // load original offset of first enemy
    ram[ShellChainCounter + x]++; // increment chain counter for additional enemies
    // ExitProcessEColl:
    return; // leave!!!
  }
  // ProcSecondEnemyColl:
  a = ram[Enemy_State + y]; // if first enemy state < $06, branch elsewhere
  if (a >= 0x6) {
    a = ram[Enemy_ID + y]; // check first enemy identifier for hammer bro
    // if hammer bro found in alt state, branch to leave
    if (a == HammerBro) {
      return;
    }
    CALL(ShellOrBlockDefeat, 0xdb01); // otherwise, kill first enemy
    y = ram[0x1];
    a = ram[ShellChainCounter + y]; // get chain counter for shell
    carry_flag = false;
    a = (uint8_t)(a + 0x4 + carry_flag); // add four to get appropriate point offset
    x = ram[ObjectOffset];
    CALL(SetupFloateyNumber, 0xdb0e); // award appropriate number of points for first enemy
    x = ram[0x1]; // load original offset of second enemy
    ram[ShellChainCounter + x]++; // increment chain counter for additional enemies
    return; // leave!!!
  }
  // MoveEOfs:
  a = y; // move Y ($01) to X
  x = a;
  CALL(EnemyTurnAround, 0xdb19); // do the sub here using value from $01
  x = ram[ObjectOffset]; // then do it again using value from $08
  EnemyTurnAround(); // fallthrough
  return;
}

// in: x; out: none
void EnemyTurnAround(void) {
  a = ram[(uint8_t)(Enemy_ID + x)]; // check for specific enemies
  // if piranha plant, leave
  if (a == PiranhaPlant) {
    return;
  }
  // if lakitu, leave
  if (a == Lakitu) {
    return;
  }
  // if hammer bro, leave
  if (a == HammerBro) {
    return;
  }
  // if spiny, turn it around
  if (a == Spiny) {
    RXSpd();
    return;
  }
  // if green paratroopa, turn it around
  if (a == GreenParatroopaJump) {
    RXSpd();
    return;
  }
  // if any OTHER enemy object => $07, leave
  if (a >= 0x7) {
    return;
  }
  RXSpd(); // fallthrough
  return;
}

// in: x; out: y
void RXSpd(void) {
  a = ram[(uint8_t)(Enemy_X_Speed + x)]; // load horizontal speed
  a ^= 0xff; // get two's compliment for horizontal speed
  y = a;
  y++;
  ram[(uint8_t)(Enemy_X_Speed + x)] = y; // store as new horizontal speed
  a = ram[(uint8_t)(Enemy_MovingDir + x)];
  a ^= 0b00000011; // invert moving direction and store, then leave
  ram[(uint8_t)(Enemy_MovingDir + x)] = a; // thus effectively turning the enemy around
  // ExTA:
  return; // leave!!!
}

// -------------------------------------------------------------------------------------
// $00 - vertical position of platform
// in: x; out: x
void LargePlatformCollision(void) {
  ram[PlatformCollisionFlag + x] = 0xff; // save value here
  a = ram[TimerControl]; zero_flag = a == 0; // check master timer control
  // if set, branch to leave
  if (!zero_flag) {
    ExLPC();
    return;
  }
  a = ram[(uint8_t)(Enemy_State + x)]; neg_flag = (a & 0x80) != 0; // if d7 set in object state,
  // branch to leave
  if (neg_flag) {
    ExLPC();
    return;
  }
  a = ram[(uint8_t)(Enemy_ID + x)];
  // check enemy object identifier for
  // balance platform, branch if not found
  if (a != 0x24) {
    ChkForPlayerC_LargeP();
    return;
  }
  a = ram[(uint8_t)(Enemy_State + x)];
  x = a; // set state as enemy offset here
  CALL(ChkForPlayerC_LargeP, 0xdb5e); // perform code with state offset, then original offset, in X
  ChkForPlayerC_LargeP(); // fallthrough
  return;
}

// in: x; out: x
void ChkForPlayerC_LargeP(void) {
  CALL(CheckPlayerVertical, 0xdb61); // figure out if player is below a certain point
  // or offscreen, branch to leave if true
  if (carry_flag) {
    ExLPC();
    return;
  }
  a = x;
  CALL(GetEnemyBoundBoxOfsArg, 0xdb67); // get bounding box offset in Y
  ram[0x0] = ram[(uint8_t)(Enemy_Y_Position + x)]; // store vertical coordinate in; temp variable for now
  a = x; // send offset we're on to the stack
  ram[0x100 + sp--] = a;
  CALL(PlayerCollisionCore, 0xdb70); // do player-to-platform collision detection
  a = ram[0x100 + ++sp]; // retrieve offset from the stack
  x = a;
  // if no collision, branch to leave
  if (!carry_flag) {
    ExLPC();
    return;
  }
  CALL(ProcLPlatCollisions, 0xdb77); // otherwise collision, perform sub
  ExLPC(); // fallthrough
  return;
}

// in: none; out: x
void ExLPC(void) {
  x = ram[ObjectOffset]; // get enemy object buffer offset and leave
  return;
}

// --------------------------------
// $00 - counter for bounding boxes
// in: x; out: x
void SmallPlatformCollision(void) {
  a = ram[TimerControl]; zero_flag = a == 0; // if master timer control set,
  // Original branch: branch to leave
  if (zero_flag) {
    ram[PlatformCollisionFlag + x] = a; // otherwise initialize collision flag
    CALL(CheckPlayerVertical, 0xdb85); // do a sub to see if player is below a certain point
    if (carry_flag) { goto ExSPC; } // or entirely offscreen, and branch to leave if true
    ram[0x0] = 0x2; // load counter here for 2 bounding boxes
    do {
      // ChkSmallPlatLoop:
      x = ram[ObjectOffset]; // get enemy object offset
      CALL(GetEnemyBoundBoxOfs, 0xdb90); // get bounding box offset in Y
      a &= 0b00000010; zero_flag = a == 0; // if d1 of offscreen lower nybble bits was set
      if (!zero_flag) { goto ExSPC; } // then branch to leave
      a = ram[BoundingBox_UL_YPos + y]; // check top of platform's bounding box for being
      // above a specific point
      // Original branch: if so, branch, don't do collision detection
      if (a >= 0x20) {
        CALL(PlayerCollisionCore, 0xdb9e); // otherwise, perform player-to-platform collision detection
        if (carry_flag) { goto ProcSPlatCollisions; } // skip ahead if collision
      }
      // MoveBoundBox:
      a = ram[BoundingBox_UL_YPos + y]; // move bounding box vertical coordinates
      carry_flag = false; // 128 pixels downwards
      a = (uint8_t)(a + 0x80 + carry_flag);
      ram[BoundingBox_UL_YPos + y] = a;
      a = ram[BoundingBox_DR_YPos + y];
      carry_flag = false;
      a = (uint8_t)(a + 0x80 + carry_flag);
      ram[BoundingBox_DR_YPos + y] = a;
      ram[0x0]--; zero_flag = ram[0x0] == 0; // decrement counter we set earlier
    } while (!zero_flag); // loop back until both bounding boxes are checked
  }
  
ExSPC:
  x = ram[ObjectOffset]; // get enemy object buffer offset, then leave
  return;
  // --------------------------------
  
ProcSPlatCollisions:
  x = ram[ObjectOffset]; // return enemy object buffer offset to X, then continue
  ProcLPlatCollisions(); // fallthrough
  return;
}

// in: x, y; out: x
void ProcLPlatCollisions(void) {
  a = ram[BoundingBox_DR_YPos + y]; // get difference by subtracting the top
  carry_flag = true; // of the player's bounding box from the bottom
  a = (uint8_t)(a - ram[BoundingBox_UL_YPos] - !carry_flag); // of the platform's bounding box
  // if difference too large or negative,
  // Original branch: branch, do not alter vertical speed of player
  if (a < 0x4) {
    a = ram[Player_Y_Speed]; neg_flag = (a & 0x80) != 0; // check to see if player's vertical speed is moving down
    if (!neg_flag) { goto ChkForTopCollision; } // if so, don't mess with it
    ram[Player_Y_Speed] = 0x1; // otherwise, set vertical; speed of player to kill jump
  }
  
ChkForTopCollision:
  a = ram[BoundingBox_DR_YPos]; // get difference by subtracting the top
  carry_flag = true; // of the platform's bounding box from the bottom
  a = (uint8_t)(a - ram[BoundingBox_UL_YPos + y] - !carry_flag); // of the player's bounding box
  // Original branch: if difference not close enough, skip all of this
  if (a < 0x6) {
    a = ram[Player_Y_Speed]; neg_flag = (a & 0x80) != 0;
    if (neg_flag) { goto PlatformSideCollisions; } // if player's vertical speed moving upwards, skip this
    a = ram[0x0]; // get saved bounding box counter from earlier
    y = ram[(uint8_t)(Enemy_ID + x)];
    // if either of the two small platform objects are found,
    // Original branch: regardless of which one, branch to use bounding box counter
    if (y != 0x2b) {
      zero_flag = y == 0x2c; // as contents of collision flag
      if (zero_flag) { goto SetCollisionFlag; }
      a = x; // otherwise use enemy object buffer offset
    }
    
SetCollisionFlag:
    x = ram[ObjectOffset]; // get enemy object buffer offset
    ram[PlatformCollisionFlag + x] = a; // save either bounding box counter or enemy offset here
    ram[Player_State] = 0x0; // set player state to normal then leave
    return;
  }
  
PlatformSideCollisions:
  ram[0x0] = 0x1; // set value here to indicate possible horizontal; collision on left side of platform
  a = ram[BoundingBox_DR_XPos]; // get difference by subtracting platform's left edge
  carry_flag = true; // from player's right edge
  a = (uint8_t)(a - ram[BoundingBox_UL_XPos + y] - !carry_flag);
  // if difference close enough, skip all of this
  if (a >= 0x8) {
    ram[0x0]++; // otherwise increment value set here for right side collision
    a = ram[BoundingBox_DR_XPos + y]; // get difference by subtracting player's left edge
    carry_flag = false; // from platform's right edge
    a = (uint8_t)(a - ram[BoundingBox_UL_XPos] - !carry_flag);
    carry_flag = a >= 0x9; // if difference not close enough, skip subroutine
    if (carry_flag) { goto NoSideC; } // and instead branch to leave (no collision)
  }
  // SideC:
  CALL(ImpedePlayerMove, 0xdc13); // deal with horizontal collision
  
NoSideC:
  x = ram[ObjectOffset]; // return with enemy object buffer offset
  return;
}

// -------------------------------------------------------------------------------------
// in: a, x; out: none
void PositionPlayerOnS_Plat(void) {
  y = a; // use bounding box counter saved in collision flag
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // for offset
  carry_flag = false; // add positioning data using offset to the vertical
  a = (uint8_t)(a + ROM((PlayerPosSPlatData - 1) + y) + carry_flag); // coordinate
  
  loc_56355(); return; // BIT instruction opcode
}

// in: x; out: none
void PositionPlayerOnVPlat(void) {
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
  loc_56355(); // fallthrough
  return;
}

// in: a, x; out: none
void loc_56355(void) {
  y = ram[GameEngineSubroutine];
  // if certain routine being executed on this frame,
  // skip all of this
  if (y == 0xb) {
    return;
  }
  y = ram[(uint8_t)(Enemy_Y_HighPos + x)];
  // if vertical high byte offscreen, skip this
  if (y != 0x1) {
    return;
  }
  carry_flag = true; // subtract 32 pixels from vertical coordinate
  { uint16_t diff = (uint16_t)(a - 0x20 - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // for the player object's height
  ram[Player_Y_Position] = a; // save as player's new vertical coordinate
  a = y;
  a = (uint8_t)(a - 0x0 - !carry_flag); // subtract borrow and store as player's
  ram[Player_Y_HighPos] = a; // new vertical high byte
  a = 0x0;
  ram[Player_Y_Speed] = a; // initialize vertical speed and low byte of force
  ram[Player_Y_MoveForce] = a; // and then leave
  // ExPlPos:
  return;
}

// -------------------------------------------------------------------------------------
// in: none; out: carry
void CheckPlayerVertical(void) {
  a = ram[Player_OffscreenBits]; // if player object is completely offscreen
  carry_flag = a >= 0xf0; // vertically, leave this routine
  if (carry_flag) {
    return;
  }
  y = ram[Player_Y_HighPos]; // if player high vertical byte is not
  y--; zero_flag = y == 0; // within the screen, leave this routine
  if (!zero_flag) {
    return;
  }
  a = ram[Player_Y_Position]; // if on the screen, check to see how far down
  carry_flag = a >= 0xd0; // the player is vertically
  // ExCPV:
  return;
}

// -------------------------------------------------------------------------------------
// in: none; out: a, y
void GetEnemyBoundBoxOfs(void) {
  a = ram[ObjectOffset]; // get enemy object buffer offset
  GetEnemyBoundBoxOfsArg(); // fallthrough
  return;
}

// in: a; out: a, y
void GetEnemyBoundBoxOfsArg(void) {
  a = (uint8_t)(a << 1); // multiply A by four, then add four
  a = (uint8_t)(a << 1); // to skip player's bounding box
  carry_flag = false;
  a = (uint8_t)(a + 0x4 + carry_flag);
  y = a; // send to Y
  a = ram[Enemy_OffscreenBits]; // get offscreen bits for enemy object
  a &= 0b00001111; // save low nybble
   // check for all bits set
  return;
}

// -------------------------------------------------------------------------------------
// $00-$01 - used to hold many values, essentially temp variables
// $04 - holds lower nybble of vertical coordinate from block buffer routine
// $eb - used to hold block buffer adder
void PlayerBGCollision(void) {
  a = ram[DisableCollisionDet]; zero_flag = a == 0; // if collision detection disabled flag set,
  // branch to leave
  if (!zero_flag) {
    return;
  }
  a = ram[GameEngineSubroutine];
  // if running routine #11 or $0b
  // branch to leave
  if (a == 0xb) {
    return;
  }
  // if running routines $00-$03 branch to leave
  if (a < 0x4) {
    return;
  }
  a = 0x1; // load default player state for swimming
  y = ram[SwimmingFlag]; zero_flag = y == 0; // if swimming flag set,
  // Original branch: branch ahead to set default state
  if (zero_flag) {
    a = ram[Player_State]; zero_flag = a == 0; // if player in normal state,
    // Original branch: branch to set default state for falling
    if (!zero_flag) {
      zero_flag = a == 0x3;
      if (!zero_flag) { goto ChkOnScr; } // if in any other state besides climbing, skip to next part
    }
    // SetFallS:
    a = 0x2; // load default player state for falling
  }
  // SetPSte:
  ram[Player_State] = a; // set whatever player state is appropriate
  
ChkOnScr:
  a = ram[Player_Y_HighPos];
  // check player's vertical high byte for still on the screen
  // branch to leave if not
  if (a != 0x1) {
    return;
  }
  ram[Player_CollisionBits] = 0xff; // initialize player's collision flag
  a = ram[Player_Y_Position];
  // check player's vertical coordinate
  // Original branch: if not too close to the bottom of screen, continue
  if (a >= 0xcf) {
    // ExPBGCol:
    return; // otherwise leave
  }
  // ChkCollSize:
  y = 0x2; // load default offset
  a = ram[CrouchingFlag]; zero_flag = a == 0;
  // Original branch: if player crouching, skip ahead
  if (zero_flag) {
    a = ram[PlayerSize]; zero_flag = a == 0;
    if (!zero_flag) { goto GBBAdr; } // if player small, skip ahead
    y--; // otherwise decrement offset for big player not crouching
    a = ram[SwimmingFlag]; zero_flag = a == 0;
    if (!zero_flag) { goto GBBAdr; } // if swimming flag set, skip ahead
    y--; // otherwise decrement offset
  }
  
GBBAdr:
  a = ROM(BlockBufferAdderData + y); // get value using offset
  ram[0xeb] = a; // store value here
  y = a; // put value into Y, as offset for block buffer routine
  x = ram[PlayerSize]; // get player's size as offset
  a = ram[CrouchingFlag]; zero_flag = a == 0;
  // Original branch: if player not crouching, branch ahead
  if (!zero_flag) {
    x++; // otherwise increment size as offset
  }
  // HeadChk:
  a = ram[Player_Y_Position]; // get player's vertical coordinate
  carry_flag = a >= ROM(PlayerBGUpperExtent + x); // compare with upper extent value based on offset
  // Original branch: if player is too high, skip this part
  if (carry_flag) {
    CALL(BlockBufferColli_Head, 0xdcc3); // do player-to-bg collision detection on top of
    if (zero_flag) { goto DoFootCheck; } // player, and branch if nothing above player's head
    CALL(CheckForCoinMTiles, 0xdcc8); // check to see if player touched coin with their head
    if (carry_flag) { goto AwardTouchedCoin; } // if so, branch to some other part of code
    y = ram[Player_Y_Speed]; neg_flag = (y & 0x80) != 0; // check player's vertical speed
    if (!neg_flag) { goto DoFootCheck; } // if player not moving upwards, branch elsewhere
    y = ram[0x4]; // check lower nybble of vertical coordinate returned
    carry_flag = y >= 0x4; // from collision detection routine
    if (!carry_flag) { goto DoFootCheck; } // if low nybble < 4, branch
    CALL(CheckForSolidMTiles, 0xdcd7); // check to see what player's head bumped on
    // Original branch: if player collided with solid metatile, branch
    if (!carry_flag) {
      y = ram[AreaType]; zero_flag = y == 0; // otherwise check area type
      if (zero_flag) { goto NYSpd; } // if water level, branch ahead
      y = ram[BlockBounceTimer]; zero_flag = y == 0; // if block bounce timer not expired,
      if (!zero_flag) { goto NYSpd; } // branch ahead, do not process collision
      CALL(PlayerHeadCollision, 0xdce6); // otherwise do a sub to process collision
      goto DoFootCheck; // jump ahead to skip these other parts here
    }
    // SolidOrClimb:
    // if climbing metatile,
    // Original branch: branch ahead and do not play sound
    if (a != 0x26) {
      ram[Square1SoundQueue] = Sfx_Bump; // otherwise load bump sound
    }
    
NYSpd:
    ram[Player_Y_Speed] = 0x1; // set player's vertical speed to nullify; jump or swim
  }
  
DoFootCheck:
  y = ram[0xeb]; // get block buffer adder offset
  a = ram[Player_Y_Position];
  carry_flag = a >= 0xcf; // check to see how low player is
  if (carry_flag) { goto DoPlayerSideCheck; } // if player is too far down on screen, skip all of this
  CALL(BlockBufferColli_Feet, 0xdd00); // do player-to-bg collision detection on bottom left of player
  CALL(CheckForCoinMTiles, 0xdd03); // check to see if player touched coin with their left foot
  // Original branch: if so, branch to some other part of code
  if (!carry_flag) {
    ram[0x100 + sp--] = a; // save bottom left metatile to stack
    CALL(BlockBufferColli_Feet, 0xdd09); // do player-to-bg collision detection on bottom right of player
    ram[0x0] = a; // save bottom right metatile here
    a = ram[0x100 + ++sp]; zero_flag = a == 0;
    ram[0x1] = a; // pull bottom left metatile and save here
    if (!zero_flag) { goto ChkFootMTile; } // if anything here, skip this part
    a = ram[0x0]; zero_flag = a == 0; // otherwise check for anything in bottom right metatile
    if (zero_flag) { goto DoPlayerSideCheck; } // and skip ahead if not
    CALL(CheckForCoinMTiles, 0xdd17); // check to see if player touched coin with their right foot
    if (!carry_flag) { goto ChkFootMTile; } // if not, skip unconditional jump and continue code
  }
  
AwardTouchedCoin:
  goto HandleCoinMetatile; // follow the code to erase coin and award to player 1 coin
  
ChkFootMTile:
  CALL(CheckForClimbMTiles, 0xdd1f); // check to see if player landed on climbable metatiles
  // Original branch: if so, branch
  if (!carry_flag) {
    y = ram[Player_Y_Speed]; neg_flag = (y & 0x80) != 0; // check player's vertical speed
    if (neg_flag) { goto DoPlayerSideCheck; } // if player moving upwards, branch
    // Original branch: if player did not touch axe, skip ahead
    if (a == 0xc5) {
      goto HandleAxeMetatile; // otherwise jump to set modes of operation
    }
    // ContChk:
    CALL(ChkInvisibleMTiles, 0xdd2f); // do sub to check for hidden coin or 1-up blocks
    if (zero_flag) { goto DoPlayerSideCheck; } // if either found, branch
    y = ram[JumpspringAnimCtrl]; zero_flag = y == 0; // if jumpspring animating right now,
    // Original branch: branch ahead
    if (zero_flag) {
      y = ram[0x4]; // check lower nybble of vertical coordinate returned
      // from collision detection routine
      // Original branch: if lower nybble < 5, branch
      if (y >= 0x5) {
        ram[0x0] = ram[Player_MovingDir]; // use player's moving direction as temp variable
        ImpedePlayerMove(); return; // jump to impede player's movement in that direction
      }
      // LandPlyr:
      CALL(ChkForLandJumpSpring, 0xdd46); // do sub to check for jumpspring metatiles and deal with it
      a = 0xf0;
      a &= ram[Player_Y_Position]; // mask out lower nybble of player's vertical position
      ram[Player_Y_Position] = a; // and store as new vertical position to land player properly
      CALL(HandlePipeEntry, 0xdd4f); // do sub to process potential pipe entry
      a = 0x0;
      ram[Player_Y_Speed] = a; // initialize vertical speed and fractional
      ram[Player_Y_MoveForce] = a; // movement force to stop player's vertical movement
      ram[StompChainCounter] = a; // initialize enemy stomp counter
    }
    // InitSteP:
    ram[Player_State] = 0x0; // set player's state to normal
  }
  
DoPlayerSideCheck:
  y = ram[0xeb]; // get block buffer adder offset
  y++;
  y++; // increment offset 2 bytes to use adders for side collisions
  ram[0x0] = 0x2; // set value here to be used as counter
  do {
    // SideCheckLoop:
    y++; // move onto the next one
    ram[0xeb] = y; // store it
    a = ram[Player_Y_Position];
    // check player's vertical position
    // Original branch: if player is in status bar area, branch ahead to skip this part
    if (a >= 0x20) {
      // branch to leave if player is too far down
      if (a >= 0xe4) {
        return;
      }
      CALL(BlockBufferColli_Side, 0xdd75); // do player-to-bg collision detection on one half of player
      if (zero_flag) { goto BHalf; } // branch ahead if nothing found
      zero_flag = a == 0x1c; // otherwise check for pipe metatiles
      if (zero_flag) { goto BHalf; } // if collided with sideways pipe (top), branch ahead
      zero_flag = a == 0x6b;
      if (zero_flag) { goto BHalf; } // if collided with water pipe (top), branch ahead
      CALL(CheckForClimbMTiles, 0xdd82); // do sub to see if player bumped into anything climbable
      if (!carry_flag) { goto CheckSideMTiles; } // if not, branch to alternate section of code
    }
    
BHalf:
    y = ram[0xeb]; // load block adder offset
    y++; // increment it
    a = ram[Player_Y_Position]; // get player's vertical position
    // if too high, branch to leave
    if (a < 0x8) {
      return;
    }
    // if too low, branch to leave
    if (a >= 0xd0) {
      return;
    }
    CALL(BlockBufferColli_Side, 0xdd94); // do player-to-bg collision detection on other half of player
    if (!zero_flag) { goto CheckSideMTiles; } // if something found, branch
    ram[0x0]--; zero_flag = ram[0x0] == 0; // otherwise decrement counter
  } while (!zero_flag); // run code until both sides of player are checked
  // ExSCH:
  return; // leave
  
CheckSideMTiles:
  CALL(ChkInvisibleMTiles, 0xdd9e); // check for hidden or coin 1-up blocks
  // branch to leave if either found
  if (zero_flag) {
    return;
  }
  CALL(CheckForClimbMTiles, 0xdda3); // check for climbable metatiles
  // Original branch: if not found, skip and continue with code
  if (carry_flag) {
    goto HandleClimbing; // otherwise jump to handle climbing
  }
  // ContSChk:
  CALL(CheckForCoinMTiles, 0xddab); // check to see if player touched coin
  // Original branch: if so, execute code to erase coin and award to player 1 coin
  if (!carry_flag) {
    CALL(ChkJumpspringMetatiles, 0xddb0); // check for jumpspring metatiles
    // Original branch: if not found, branch ahead to continue cude
    if (carry_flag) {
      a = ram[JumpspringAnimCtrl]; zero_flag = a == 0; // otherwise check jumpspring animation control
      // branch to leave if set
      if (!zero_flag) {
        return;
      }
    } else {
      // ChkPBtm:
      y = ram[Player_State]; // get player's state
      zero_flag = y == 0x0; // check for player's state set to normal
      if (!zero_flag) { goto StopPlayerMove; } // if not, branch to impede player's movement
      y = ram[PlayerFacingDir]; // get player's facing direction
      y--; zero_flag = y == 0;
      if (!zero_flag) { goto StopPlayerMove; } // if facing left, branch to impede movement
      // otherwise check for pipe metatiles
      // Original branch: if collided with sideways pipe (bottom), branch
      if (a != 0x6c) {
        zero_flag = a == 0x1f; // if collided with water pipe (bottom), continue
        if (!zero_flag) { goto StopPlayerMove; } // otherwise branch to impede player's movement
      }
      // PipeDwnS:
      a = ram[Player_SprAttrib]; zero_flag = a == 0; // check player's attributes
      // Original branch: if already set, branch, do not play sound again
      if (zero_flag) {
        ram[Square1SoundQueue] = Sfx_PipeDown_Injury; // otherwise load pipedown/injury sound
      }
      // PlyrPipe:
      a |= 0b00100000;
      ram[Player_SprAttrib] = a; // set background priority bit in player attributes
      a = ram[Player_X_Position];
      a &= 0b00001111; zero_flag = a == 0; // get lower nybble of player's horizontal coordinate
      // Original branch: if at zero, branch ahead to skip this part
      if (!zero_flag) {
        y = 0x0; // set default offset for timer setting data
        a = ram[ScreenLeft_PageLoc]; zero_flag = a == 0; // load page location for left side of screen
        // Original branch: if at page zero, use default offset
        if (!zero_flag) {
          y++; // otherwise increment offset
        }
        // SetCATmr:
        ram[ChangeAreaTimer] = ROM(AreaChangeTimerData + y); // set timer for change of area as appropriate
      }
      // ChkGERtn:
      a = ram[GameEngineSubroutine]; // get number of game engine routine running
      // if running player entrance routine or
      if (a == 0x7) {
        return;
      }
      // player control routine, go ahead and branch to leave
      if (a != 0x8) {
        return;
      }
      ram[GameEngineSubroutine] = 0x2; // otherwise set sideways pipe entry routine to run
      return; // and leave
      // --------------------------------
      // $02 - high nybble of vertical coordinate from block buffer
      // $04 - low nybble of horizontal coordinate from block buffer
      // $06-$07 - block buffer address
    }
    
StopPlayerMove:
    CALL(ImpedePlayerMove, 0xde01); // stop player's movement
    // ExCSM:
    return; // leave
  }
  
HandleCoinMetatile:
  CALL(ErACM, 0xde07); // do sub to erase coin metatile from block buffer
  ram[CoinTallyFor1Ups]++; // increment coin tally used for 1-up blocks
  GiveOneCoin(); return; // update coin amount and tally on the screen
  
HandleAxeMetatile:
  ram[OperMode_Task] = 0x0; // reset secondary mode
  ram[OperMode] = 0x2; // set primary mode to autoctrl mode
  ram[Player_X_Speed] = 0x18; // set horizontal speed and continue to erase axe metatile
  ErACM(); // fallthrough
  return;
  // --------------------------------
  // $02 - high nybble of vertical coordinate from block buffer
  // $04 - low nybble of horizontal coordinate from block buffer
  // $06-$07 - block buffer address
  
HandleClimbing:
  y = ram[0x4]; // check low nybble of horizontal coordinate returned from
  // collision detection routine against certain values, this
  // makes actual physical part of vine or flagpole thinner
  if (y < 0x6) {
    return;
  }
  // than 16 pixels
  if (y >= 0xa) {
    // ExHC:
    return; // leave if too far left or too far right
  }
  // ChkForFlagpole:
  // check climbing metatiles
  // Original branch: branch if flagpole ball found
  if (a != 0x24) {
    zero_flag = a == 0x25;
    if (!zero_flag) { goto VineCollision; } // branch to alternate code if flagpole shaft not found
  }
  // FlagpoleCollision:
  a = ram[GameEngineSubroutine];
  zero_flag = a == 0x5; // check for end-of-level routine running
  if (zero_flag) { goto PutPlayerOnVine; } // if running, branch to end of climbing code
  ram[PlayerFacingDir] = 0x1; // set player's facing direction to right
  ram[ScrollLock]++; // set scroll lock flag
  a = ram[GameEngineSubroutine];
  // check for flagpole slide routine running
  // Original branch: if running, branch to end of flagpole code here
  if (a != 0x4) {
    a = BulletBill_CannonVar; // load identifier for bullet bills (cannon variant)
    CALL(KillEnemies, 0xde58); // get rid of them
    a = Silence;
    ram[EventMusicQueue] = a; // silence music
    a = a >> 1;
    ram[FlagpoleSoundQueue] = a; // load flagpole sound into flagpole sound queue
    x = 0x4; // start at end of vertical coordinate data
    a = ram[Player_Y_Position];
    ram[FlagpoleCollisionYPos] = a; // store player's vertical coordinate here to be used later
    do {
      // ChkFlagpoleYPosLoop:
      carry_flag = a >= ROM(FlagpoleYPosData + x); // compare with current vertical coordinate data
      if (carry_flag) { goto MtchF; } // if player's => current, branch to use current offset
      x--; zero_flag = x == 0; // otherwise decrement offset to use 
    } while (!zero_flag); // do this until all data is checked (use last one if all checked)
    
MtchF:
    ram[FlagpoleScore] = x; // store offset here to be used later
  }
  // RunFR:
  ram[GameEngineSubroutine] = 0x4; // set value to run flagpole slide routine
  goto PutPlayerOnVine; // jump to end of climbing code
  
VineCollision:
  // check for climbing metatile used on vines
  if (a == 0x26) {
    a = ram[Player_Y_Position]; // check player's vertical coordinate
    carry_flag = a >= 0x20; // for being in status bar area
    if (carry_flag) { goto PutPlayerOnVine; } // branch if not that far up
    ram[GameEngineSubroutine] = 0x1; // otherwise set to run autoclimb routine next frame
  }
  
PutPlayerOnVine:
  ram[Player_State] = 0x3; // set player state to climbing
  a = 0x0; // nullify player's horizontal speed
  ram[Player_X_Speed] = a; // and fractional horizontal movement force
  ram[Player_X_MoveForce] = a;
  a = ram[Player_X_Position]; // get player's horizontal coordinate
  carry_flag = true;
  a = (uint8_t)(a - ram[ScreenLeft_X_Pos] - !carry_flag); // subtract from left side horizontal coordinate
  // Original branch: if 16 or more pixels difference, do not alter facing direction
  if (a < 0x10) {
    ram[PlayerFacingDir] = 0x2; // otherwise force player to face left
  }
  // SetVXPl:
  y = ram[PlayerFacingDir]; // get current facing direction, use as offset
  a = ram[0x6]; // get low byte of block buffer address
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // move low nybble to high
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  carry_flag = false;
  a = (uint8_t)(a + ROM((ClimbXPosAdder - 1) + y) + carry_flag); // add pixels depending on facing direction
  ram[Player_X_Position] = a; // store as player's horizontal coordinate
  a = ram[0x6]; zero_flag = a == 0; // get low byte of block buffer address again
  // if not zero, branch
  if (!zero_flag) {
    return;
  }
  a = ram[ScreenRight_PageLoc]; // load page location of right side of screen
  carry_flag = false;
  a = (uint8_t)(a + ROM((ClimbPLocAdder - 1) + y) + carry_flag); // add depending on facing location
  ram[Player_PageLoc] = a; // store as player's page location
  // ExPVne:
  return; // finally, we're done!
}

void ErACM(void) {
  y = ram[0x2]; // load vertical high nybble offset for block buffer
  dynamic_ram_write(indirect_y_addr(0x6), 0x0); // load blank metatile; store to remove old contents from block buffer
  RemoveCoin_Axe(); return; // update the screen accordingly
}

// --------------------------------
// in: a; out: zero
void ChkInvisibleMTiles(void) {
  zero_flag = a == 0x5f; // check for hidden coin block
  // branch to leave if found
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0x60; // check for hidden 1-up block
  // ExCInvT:
  return; // leave with zero flag set if either found
}

// --------------------------------
// $00-$01 - used to hold bottom right and bottom left metatiles (in that order)
// $00 - used as flag by ImpedePlayerMove to restrict specific movement
// in: a; out: none
void ChkForLandJumpSpring(void) {
  CALL(ChkJumpspringMetatiles, 0xdec6); // do sub to check if player landed on jumpspring
  // if carry not set, jumpspring not found, therefore leave
  if (!carry_flag) {
    return;
  }
  ram[VerticalForce] = 0x70; // otherwise set vertical movement force for player
  ram[JumpspringForce] = 0xf9; // set default jumpspring force
  a = 0x3;
  ram[JumpspringTimer] = a; // set jumpspring timer to be used later
  a = a >> 1;
  ram[JumpspringAnimCtrl] = a; // set jumpspring animation control to start animating
  // ExCJSp:
  return; // and leave
}

// in: a; out: carry
void ChkJumpspringMetatiles(void) {
  // check for top jumpspring metatile
  // Original branch: branch to set carry if found
  if (a != 0x67) {
    zero_flag = a == 0x68; // check for bottom jumpspring metatile
    carry_flag = false; // clear carry flag
    // branch to use cleared carry if not found
    if (!zero_flag) {
      return;
    }
  }
  // JSFnd:
  carry_flag = true; // set carry if found
  // NoJSFnd:
  return; // leave
}

void HandlePipeEntry(void) {
  a = ram[Up_Down_Buttons]; // check saved controller bits from earlier
  a &= 0b00000100; zero_flag = a == 0; // for pressing down
  // if not pressing down, branch to leave
  if (zero_flag) {
    return;
  }
  a = ram[0x0];
  // check right foot metatile for warp pipe right metatile
  // branch to leave if not found
  if (a != 0x11) {
    return;
  }
  a = ram[0x1];
  // check left foot metatile for warp pipe left metatile
  // branch to leave if not found
  if (a != 0x10) {
    return;
  }
  ram[ChangeAreaTimer] = 0x30; // set timer for change of area
  ram[GameEngineSubroutine] = 0x3; // set to run vertical pipe entry routine on next frame
  ram[Square1SoundQueue] = Sfx_PipeDown_Injury; // load pipedown/injury sound
  ram[Player_SprAttrib] = 0b00100000; // set background priority bit in player's attributes
  a = ram[WarpZoneControl]; zero_flag = a == 0; // check warp zone control
  // branch to leave if none found
  if (zero_flag) {
    return;
  }
  a &= 0b00000011; // mask out all but 2 LSB
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // multiply by four
  x = a; // save as offset to warp zone numbers (starts at left pipe)
  a = ram[Player_X_Position]; // get player's horizontal position
  // Original branch: if player at left, not near middle, use offset and skip ahead
  if (a >= 0x60) {
    x++; // otherwise increment for middle pipe
    carry_flag = a >= 0xa0;
    if (!carry_flag) { goto GetWNum; } // if player at middle, but not too far right, use offset and skip
    x++; // otherwise increment for last pipe
  }
  
GetWNum:
  y = ROM(WarpZoneNumbers + x); // get warp zone numbers
  y--; // decrement for use as world number
  ram[WorldNumber] = y; // store as world number and offset
  x = ROM(WorldAddrOffsets + y); // get offset to where this world's area offsets are
  ram[AreaPointer] = ROM(AreaAddrOffsets + x); // get area offset based on world offset; store area offset here to be used to change areas
  ram[EventMusicQueue] = Silence; // silence music
  a = 0x0;
  ram[EntrancePage] = a; // initialize starting page number
  ram[AreaNumber] = a; // initialize area number used for area address offset
  ram[LevelNumber] = a; // initialize level number used for world display
  ram[AltEntranceControl] = a; // initialize mode of entry
  ram[Hidden1UpFlag]++; // set flag for hidden 1-up blocks
  ram[FetchNewGameTimerFlag]++; // set flag to load new game timer
  // ExPipeE:
  return; // leave!!!
}

void ImpedePlayerMove(void) {
  a = 0x0; // initialize value here
  y = ram[Player_X_Speed]; // get player's horizontal speed
  x = ram[0x0]; // check value set earlier for
  x--; zero_flag = x == 0; // left side collision
  // Original branch: if right side collision, skip this part
  if (zero_flag) {
    x++; // return value to X
    neg_flag = ((y - 0x0) & 0x80) != 0; // if player moving to the left,
    if (neg_flag) { goto ExIPM; } // branch to invert bit and leave
    a = 0xff; // otherwise load A with value to be used later
  } else {
    // RImpd:
    x = 0x2; // return $02 to X
    neg_flag = ((y - 0x1) & 0x80) != 0; // if player moving to the right,
    if (!neg_flag) { goto ExIPM; } // branch to invert bit and leave
    a = 0x1; // otherwise load A with value to be used here
  }
  // NXSpd:
  ram[SideCollisionTimer] = 0x10; // set timer of some sort
  y = 0x0;
  ram[Player_X_Speed] = y; // nullify player's horizontal speed
  neg_flag = ((a - 0x0) & 0x80) != 0; // if value set in A not set to $ff,
  // Original branch: branch ahead, do not decrement Y
  if (neg_flag) {
    y--; // otherwise decrement Y now
  }
  // PlatF:
  ram[0x0] = y; // store Y as high bits of horizontal adder
  carry_flag = false;
  { uint16_t sum = a + ram[Player_X_Position] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add contents of A to player's horizontal
  ram[Player_X_Position] = a; // position to move player left or right
  a = ram[Player_PageLoc];
  a = (uint8_t)(a + ram[0x0] + carry_flag); // add high bits and carry to
  ram[Player_PageLoc] = a; // page location if necessary
  
ExIPM:
  a = x; // invert contents of X
  a ^= 0xff;
  a &= ram[Player_CollisionBits]; // mask out bit that was set here
  ram[Player_CollisionBits] = a; // store to clear bit
  return;
}

// --------------------------------
// in: a; out: a, carry
void CheckForSolidMTiles(void) {
  CALL(GetMTileAttrib, 0xdf91); // find appropriate offset based on metatile's 2 MSB
  carry_flag = a >= ROM(SolidMTileUpperExt + x); // compare current metatile with solid metatiles
  return;
}

// in: a; out: a, y, carry
void CheckForClimbMTiles(void) {
  CALL(GetMTileAttrib, 0xdf9c); // find appropriate offset based on metatile's 2 MSB
  carry_flag = a >= ROM(ClimbMTileUpperExt + x); // compare current metatile with climbable metatiles
  return;
}

// in: a; out: a, carry
void CheckForCoinMTiles(void) {
  { uint8_t m = 0xc2; carry_flag = a >= m; zero_flag = a == m; } // check for regular coin
  // Original branch: branch if found
  if (!zero_flag) {
    { uint8_t m = 0xc3; carry_flag = a >= m; zero_flag = a == m; } // check for underwater coin
    if (zero_flag) { goto CoinSd; } // branch if found
    carry_flag = false; // otherwise clear carry and leave
    return;
  }
  
CoinSd:
  a = Sfx_CoinGrab;
  ram[Square2SoundQueue] = a; // load coin grab sound and leave
  return;
}

// in: a; out: a, x, y
void GetMTileAttrib(void) {
  y = a; // save metatile value into Y
  a &= 0b11000000; // mask out all but 2 MSB
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // shift and rotate d7-d6 to d1-d0
  a = (uint8_t)(a << 1 | carry_flag);
  x = a; // use as offset for metatile data
  a = y; // get original metatile value back
  // ExEBG:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $06-$07 - address from block buffer routine
// in: x; out: x
void EnemyToBGCollisionDet(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // check enemy state for d6 set
  a &= 0b00100000; zero_flag = a == 0;
  // if set, branch to leave
  if (!zero_flag) {
    return;
  }
  CALL(SubtEnemyYPos, 0xdfc9); // otherwise, do a subroutine here
  // if enemy vertical coord + 62 < 68, branch to leave
  if (!carry_flag) {
    return;
  }
  y = ram[(uint8_t)(Enemy_ID + x)];
  // if enemy object is not spiny, branch elsewhere
  if (y == Spiny) {
    a = ram[(uint8_t)(Enemy_Y_Position + x)];
    // if enemy vertical coordinate < 36 branch to leave
    if (a < 0x25) {
      return;
    }
  }
  // DoIDCheckBGColl:
  // check for some other enemy object
  // Original branch: branch if not found
  if (y == GreenParatroopaJump) {
    EnemyJump(); return; // otherwise jump elsewhere
  }
  // HBChk:
  // check for hammer bro
  // Original branch: branch if not found
  if (y == HammerBro) {
  } else {
    // CInvu:
    // if enemy object is spiny, branch
    if (y != Spiny) {
      zero_flag = y == PowerUpObject; // if special power-up object, branch
      if (zero_flag) { goto YesIn; }
      // if enemy object =>$07, branch to leave
      if (y >= 0x7) {
        return;
      }
    }
    
YesIn:
    CALL(ChkUnderEnemy, 0xdff4); // if enemy object < $07, or = $12 or $2e, do this sub
    if (!zero_flag) { goto HandleEToBGCollision; } // if block underneath enemy, branch
    
NoEToBGCollision:
    goto ChkForRedKoopa; // otherwise skip and do something else
    // --------------------------------
    // $02 - vertical coordinate from block buffer routine
    
HandleEToBGCollision:
    CALL(ChkForNonSolids, 0xdffc); // if something is underneath enemy, find out what
    if (zero_flag) { goto NoEToBGCollision; } // if blank $26, coins, or hidden blocks, jump, enemy falls through
    // Original branch: check for blank metatile $23 and branch if not found
    if (a == 0x23) {
      y = ram[0x2]; // get vertical coordinate used to find block
      dynamic_ram_write(indirect_y_addr(0x6), 0x0); // store default blank metatile in that spot so we won't; trigger this routine accidentally again
      a = ram[(uint8_t)(Enemy_ID + x)];
      // if enemy object => $15, branch ahead
      if (a >= 0x15) {
        ChkToStunEnemies();
        return;
      }
      // if enemy object not goomba, branch ahead of this routine
      if (a == Goomba) {
        CALL(KillEnemyAboveBlock, 0xe015); // if enemy object IS goomba, do this sub
      }
      // GiveOEPoints:
      a = 0x1; // award 100 points for hitting block beneath enemy
      CALL(SetupFloateyNumber, 0xe01a);
      ChkToStunEnemies(); // fallthrough
      return;
      // --------------------------------
      // $04 - low nybble of vertical coordinate from block buffer routine
    }
    // LandEnemyProperly:
    a = ram[0x4]; // check lower nybble of vertical coordinate saved earlier
    carry_flag = true;
    a = (uint8_t)(a - 0x8 - !carry_flag); // subtract eight pixels
    // used to determine whether enemy landed from falling
    // Original branch: branch if lower nybble in range of $0d-$0f before subtract
    if (a < 0x5) {
      a = ram[(uint8_t)(Enemy_State + x)];
      a &= 0b01000000; zero_flag = a == 0; // branch if d6 in enemy state is set
      if (zero_flag) {
        a = ram[(uint8_t)(Enemy_State + x)];
        carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // branch if d7 in enemy state is not set
        if (!carry_flag) { goto ChkLandedEnemyState; }
        
SChkA:
        DoEnemySideCheck(); return; // if lower nybble < $0d, d7 set but d6 not set, jump here
        
ChkLandedEnemyState:
        a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0; // if enemy in normal state, branch back to jump here
        if (zero_flag) { goto SChkA; }
        // if in state used by spiny's egg
        // Original branch: then branch elsewhere
        if (a != 0x5) {
          // if already in state used by koopas and buzzy beetles
          // or in higher numbered state, branch to leave
          if (a >= 0x3) {
            return;
          }
          a = ram[(uint8_t)(Enemy_State + x)]; // load enemy state again (why?)
          zero_flag = a == 0x2; // if not in $02 state (used by koopas and buzzy beetles)
          if (!zero_flag) { goto ProcEnemyDirection; } // then branch elsewhere
          a = 0x10; // load default timer here
          y = ram[(uint8_t)(Enemy_ID + x)]; // check enemy identifier for spiny
          // Original branch: branch if not found
          if (y == Spiny) {
            a = 0x0; // set timer for $00 if spiny
          }
          // SetForStn:
          ram[(EnemyIntervalTimer + x) & 0x7ff] = a; // set timer here
          ram[(uint8_t)(Enemy_State + x)] = 0x3; // set state here, apparently used to render; upside-down koopas and buzzy beetles
          CALL(EnemyLanding, 0xe0a3); // then land it properly
          // ExSteChk:
          return; // then leave
        }
        
ProcEnemyDirection:
        a = ram[(uint8_t)(Enemy_ID + x)]; // check enemy identifier for goomba
        zero_flag = a == Goomba; // branch if found
        if (zero_flag) { goto LandEnemyInitState; }
        // check for spiny
        // Original branch: branch if not found
        if (a == Spiny) {
          ram[(uint8_t)(Enemy_MovingDir + x)] = 0x1; // send enemy moving to the right by default
          ram[(uint8_t)(Enemy_X_Speed + x)] = 0x8; // set horizontal speed accordingly
          a = ram[FrameCounter];
          a &= 0b00000111; zero_flag = a == 0; // if timed appropriately, spiny will skip over
          if (zero_flag) { goto LandEnemyInitState; } // trying to face the player
        }
        // InvtD:
        y = 0x1; // load 1 for enemy to face the left (inverted here)
        CALL(PlayerEnemyDiff, 0xe0c1); // get horizontal difference between player and enemy
        // Original branch: if enemy to the right of player, branch
        if (neg_flag) {
          y++; // if to the left, increment by one for enemy to face right (inverted)
        }
        // CNwCDir:
        a = y;
        zero_flag = a == ram[(uint8_t)(Enemy_MovingDir + x)]; // compare direction in A with current direction in memory
        if (!zero_flag) { goto LandEnemyInitState; }
        CALL(ChkForBump_HammerBroJ, 0xe0cc); // if equal, not facing in correct dir, do sub to turn around
      }
      
LandEnemyInitState:
      CALL(EnemyLanding, 0xe0cf); // land enemy properly
      a = ram[(uint8_t)(Enemy_State + x)];
      a &= 0b10000000; zero_flag = a == 0; // if d7 of enemy state is set, branch
      if (zero_flag) {
        ram[(uint8_t)(Enemy_State + x)] = 0x0; // otherwise initialize enemy state and leave; note this will also turn spiny's egg into spiny
        return;
      }
      // NMovShellFallBit:
      a = ram[(uint8_t)(Enemy_State + x)]; // nullify d6 of enemy state, save other bits
      a &= 0b10111111; // and store, then leave
      ram[(uint8_t)(Enemy_State + x)] = a;
      return;
      // --------------------------------
    }
    
ChkForRedKoopa:
    a = ram[(uint8_t)(Enemy_ID + x)]; // check for red koopa troopa $03
    // Original branch: branch if not found
    if (a == RedKoopa) {
      a = ram[(uint8_t)(Enemy_State + x)]; zero_flag = a == 0;
      // if enemy found and in normal state, branch
      if (zero_flag) {
        ChkForBump_HammerBroJ();
        return;
      }
    }
    // Chk2MSBSt:
    a = ram[(uint8_t)(Enemy_State + x)]; // save enemy state into Y
    y = a;
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // check for d7 set
    // Original branch: branch if not set
    if (carry_flag) {
      a = ram[(uint8_t)(Enemy_State + x)];
      a |= 0b01000000; // set d6
    } else {
      // GetSteFromD:
      a = ROM(EnemyBGCStateData + y); // load new enemy state with old as offset
    }
    // SetD6Ste:
    ram[(uint8_t)(Enemy_State + x)] = a; // set as new state
    DoEnemySideCheck(); // fallthrough
    return;
    // --------------------------------
  }
  // HammerBroBGColl:
  CALL(ChkUnderEnemy, 0xe187); // check to see if hammer bro is standing on anything
  if (!zero_flag) {
    // check for blank metatile $23 and branch if not found
    if (a == 0x23) {
      KillEnemyAboveBlock(); // fallthrough
      return;
    }
    // UnderHammerBro:
    a = ram[(EnemyFrameTimer + x) & 0x7ff]; zero_flag = a == 0; // check timer used by hammer bro
    if (!zero_flag) { goto NoUnderHammerBro; } // branch if not expired
    a = ram[(uint8_t)(Enemy_State + x)];
    a &= 0b10001000; // save d7 and d3 from enemy state, nullify other bits
    ram[(uint8_t)(Enemy_State + x)] = a; // and store
    CALL(EnemyLanding, 0xe1a3); // modify vertical coordinate, speed and something else
    DoEnemySideCheck(); return; // then check for horizontal blockage and leave
  }
  
NoUnderHammerBro:
  a = ram[(uint8_t)(Enemy_State + x)]; // if hammer bro is not standing on anything, set d0
  a |= 0x1; // in the enemy state to indicate jumping or falling, then leave
  ram[(uint8_t)(Enemy_State + x)] = a;
  return;
}

// in: a, x; out: none
void ChkToStunEnemies(void) {
  // perform many comparisons on enemy object identifier
  if (a < 0x9) {
    SetStun();
    return;
  }
  // if the enemy object identifier is equal to the values
  // $09, $0e, $0f or $10, it will be modified, and not
  if (a >= 0x11) {
    SetStun();
    return;
  }
  // modified if not any of those values, note that piranha plant will
  // Original branch: always fail this test because A will still have vertical
  if (a >= 0xa) {
    // coordinate from previous addition, also these comparisons
    // are only necessary if branching from $d7a1
    if (a < PiranhaPlant) {
      SetStun();
      return;
    }
  }
  // Demote:
  a &= 0b00000001; // erase all but LSB, essentially turning enemy object
  ram[(uint8_t)(Enemy_ID + x)] = a; // into green or red koopa troopa to demote them
  SetStun(); // fallthrough
  return;
}

// in: x; out: y
void SetStun(void) {
  a = ram[(uint8_t)(Enemy_State + x)]; // load enemy state
  a &= 0b11110000; // save high nybble
  a |= 0b00000010;
  ram[(uint8_t)(Enemy_State + x)] = a; // set d1 of enemy state
  ram[(uint8_t)(Enemy_Y_Position + x)]--;
  ram[(uint8_t)(Enemy_Y_Position + x)]--; // subtract two pixels from enemy's vertical position
  a = ram[(uint8_t)(Enemy_ID + x)];
  // check for bloober object
  if (a != Bloober) {
    a = 0xfd; // set default vertical speed
    y = ram[AreaType]; zero_flag = y == 0;
    if (!zero_flag) { goto SetNotW; } // if area type not water, set as speed, otherwise
  }
  // SetWYSpd:
  a = 0xff; // change the vertical speed
  
SetNotW:
  ram[(uint8_t)(Enemy_Y_Speed + x)] = a; // set vertical speed now
  y = 0x1;
  CALL(PlayerEnemyDiff, 0xe050); // get horizontal difference between player and enemy object
  // Original branch: branch if enemy is to the right of player
  if (neg_flag) {
    y++; // increment Y if not
  }
  // ChkBBill:
  a = ram[(uint8_t)(Enemy_ID + x)];
  // check for bullet bill (cannon variant)
  if (a != BulletBill_CannonVar) {
    zero_flag = a == BulletBill_FrenzyVar; // check for bullet bill (frenzy variant)
    if (zero_flag) { goto NoCDirF; } // branch if either found, direction does not change
    ram[(uint8_t)(Enemy_MovingDir + x)] = y; // store as moving direction
  }
  
NoCDirF:
  y--; // decrement and use as offset
  ram[(uint8_t)(Enemy_X_Speed + x)] = ROM(EnemyBGCXSpdData + y); // get proper horizontal speed; and store, then leave
  // ExEBGChk:
  return;
}

// --------------------------------
// $00 - used to store bitmask (not used but initialized here)
// $eb - used in DoEnemySideCheck as counter and to compare moving directions
// in: x; out: x
void DoEnemySideCheck(void) {
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // if enemy within status bar, branch to leave
  // because there's nothing there that impedes movement
  if (a < 0x20) {
    return;
  }
  y = 0x16; // start by finding block to the left of enemy ($00,$14)
  ram[0xeb] = 0x2; // set value here in what is also used as; OAM data offset
  do {
    // SdeCLoop:
    a = ram[0xeb]; // check value
    zero_flag = a == ram[(uint8_t)(Enemy_MovingDir + x)]; // compare value against moving direction
    // Original branch: branch if different and do not seek block there
    if (zero_flag) {
      a = 0x1; // set flag in A for save horizontal coordinate 
      CALL(BlockBufferChk_Enemy, 0xe114); // find block to left or right of enemy object
      if (zero_flag) { goto NextSdeC; } // if nothing found, branch
      CALL(ChkForNonSolids, 0xe119); // check for non-solid blocks
      // branch if not found
      if (!zero_flag) {
        ChkForBump_HammerBroJ();
        return;
      }
    }
    
NextSdeC:
    ram[0xeb]--; // move to the next direction
    y++;
    carry_flag = y >= 0x18; // increment Y, loop only if Y < $18, thus we check
  } while (!carry_flag); // enemy ($00, $14) and ($10, $14) pixel coordinates
  // ExESdeC:
  return;
}

// in: x; out: x, y
void ChkForBump_HammerBroJ(void) {
  // check if we're on the special use slot
  // Original branch: and if so, branch ahead and do not play sound
  if (x != 0x5) {
    a = ram[(uint8_t)(Enemy_State + x)]; // if enemy state d7 not set, branch
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // ahead and do not play sound
    if (!carry_flag) { goto NoBump; }
    ram[Square1SoundQueue] = Sfx_Bump; // otherwise, play bump sound; sound will never be played if branching from ChkForRedKoopa
  }
  
NoBump:
  a = ram[(uint8_t)(Enemy_ID + x)]; // check for hammer bro
  // Original branch: branch if not found
  if (a == 0x5) {
    ram[0x0] = 0x0; // initialize value here for bitmask  
    y = 0xfa; // load default vertical speed for jumping
    SetHJ(); return; // jump to code that makes hammer bro jump
  }
  // InvEnemyDir:
  RXSpd(); return; // jump to turn the enemy around
}

// --------------------------------
// $00 - used to hold horizontal difference between player and enemy
// in: x; out: carry, negative
void PlayerEnemyDiff(void) {
  a = ram[(uint8_t)(Enemy_X_Position + x)]; // get distance between enemy object's
  carry_flag = true; // horizontal coordinate and the player's
  { uint16_t diff = (uint16_t)(a - ram[Player_X_Position] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // horizontal coordinate
  ram[0x0] = a; // and store here
  a = ram[(uint8_t)(Enemy_PageLoc + x)];
  { uint16_t diff = (uint16_t)(a - ram[Player_PageLoc] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; neg_flag = (a & 0x80) != 0; } // subtract borrow, then leave
  return;
}

// --------------------------------
// in: x; out: none
void EnemyLanding(void) {
  CALL(InitVStf, 0xe151); // do something here to vertical speed and something else
  a = ram[(uint8_t)(Enemy_Y_Position + x)];
  a &= 0b11110000; // save high nybble of vertical coordinate, and
  a |= 0b00001000; // set d3, then store, probably used to set enemy object
  ram[(uint8_t)(Enemy_Y_Position + x)] = a; // neatly on whatever it's landing on
  return;
}

// in: x; out: carry
void SubtEnemyYPos(void) {
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // add 62 pixels to enemy object's
  carry_flag = false; // vertical coordinate
  a = (uint8_t)(a + 0x3e + carry_flag);
  carry_flag = a >= 0x44; // compare against a certain range
  return; // and leave with flags set for conditional branch
}

// in: x; out: x
void EnemyJump(void) {
  CALL(SubtEnemyYPos, 0xe165); // do a sub here
  // Original branch: if enemy vertical coord + 62 < 68, branch to leave
  if (carry_flag) {
    a = ram[(uint8_t)(Enemy_Y_Speed + x)];
    carry_flag = false; // add two to vertical speed
    a = (uint8_t)(a + 0x2 + carry_flag);
    carry_flag = a >= 0x3; // if green paratroopa not falling, branch ahead
    if (!carry_flag) { goto DoSide; }
    CALL(ChkUnderEnemy, 0xe173); // otherwise, check to see if green paratroopa is 
    if (zero_flag) { goto DoSide; } // standing on anything, then branch to same place if not
    CALL(ChkForNonSolids, 0xe178); // check for non-solid blocks
    if (zero_flag) { goto DoSide; } // branch if found
    CALL(EnemyLanding, 0xe17d); // change vertical coordinate and speed
    ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xfd; // make the paratroopa jump again
  }
  
DoSide:
  DoEnemySideCheck(); return; // check for horizontal blockage, then leave
}

// in: x; out: y
void KillEnemyAboveBlock(void) {
  CALL(ShellOrBlockDefeat, 0xe190); // do this sub to kill enemy
  ram[(uint8_t)(Enemy_Y_Speed + x)] = 0xfc; // alter vertical speed of enemy and leave
  return;
}

// in: x; out: a, x, y, zero
void ChkUnderEnemy(void) {
  a = 0x0; // set flag in A for save vertical coordinate
  y = 0x15; // set Y to check the bottom middle (8,18) of enemy object
  BlockBufferChk_Enemy(); return; // hop to it!
}

// in: a; out: zero
void ChkForNonSolids(void) {
  zero_flag = a == 0x26; // blank metatile used for vines?
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0xc2; // regular coin?
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0xc3; // underwater coin?
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0x5f; // hidden coin block?
  if (zero_flag) {
    return;
  }
  zero_flag = a == 0x60; // hidden 1-up block?
  // NSFnd:
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void FireballBGCollision(void) {
  a = ram[(uint8_t)(Fireball_Y_Position + x)]; // check fireball's vertical coordinate
  // Original branch: if within the status bar area of the screen, branch ahead
  if (a >= 0x18) {
    CALL(BlockBufferChk_FBall, 0xe1d0); // do fireball to background collision detection on bottom of it
    if (zero_flag) { goto ClearBounceFlag; } // if nothing underneath fireball, branch
    CALL(ChkForNonSolids, 0xe1d5); // check for non-solid metatiles
    if (zero_flag) { goto ClearBounceFlag; } // branch if any found
    a = ram[(uint8_t)(Fireball_Y_Speed + x)]; neg_flag = (a & 0x80) != 0; // if fireball's vertical speed set to move upwards,
    if (neg_flag) { goto InitFireballExplode; } // branch to set exploding bit in fireball's state
    a = ram[(uint8_t)(FireballBouncingFlag + x)]; zero_flag = a == 0; // if bouncing flag already set,
    if (!zero_flag) { goto InitFireballExplode; } // branch to set exploding bit in fireball's state
    ram[(uint8_t)(Fireball_Y_Speed + x)] = 0xfd; // otherwise set vertical speed to move upwards (give it bounce)
    ram[(uint8_t)(FireballBouncingFlag + x)] = 0x1; // set bouncing flag
    a = ram[(uint8_t)(Fireball_Y_Position + x)];
    a &= 0xf8; // modify vertical coordinate to land it properly
    ram[(uint8_t)(Fireball_Y_Position + x)] = a; // store as new vertical coordinate
    return; // leave
  }
  
ClearBounceFlag:
  ram[(uint8_t)(FireballBouncingFlag + x)] = 0x0; // clear bouncing flag by default
  return; // leave
  
InitFireballExplode:
  ram[(uint8_t)(Fireball_State + x)] = 0x80; // set exploding flag in fireball's state
  ram[Square1SoundQueue] = Sfx_Bump; // load bump sound
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00 - used to hold one of bitmasks, or offset
// $01 - used for relative X coordinate, also used to store middle screen page location
// $02 - used for relative Y coordinate, also used to store middle screen coordinate
// this data added to relative coordinates of sprite objects
// stored in order: left edge, top edge, right edge, bottom edge
// in: x; out: x
void GetFireballBoundBox(void) {
  a = x; // add seven bytes to offset
  carry_flag = false; // to use in routines as offset for fireball
  a = (uint8_t)(a + 0x7 + carry_flag);
  x = a;
  y = 0x2; // set offset for relative coordinates
  FBallB(); return; // unconditional branch
}

// in: x; out: x
void GetMiscBoundBox(void) {
  a = x; // add nine bytes to offset
  carry_flag = false; // to use in routines as offset for misc object
  a = (uint8_t)(a + 0x9 + carry_flag);
  x = a;
  y = 0x6; // set offset for relative coordinates
  FBallB(); // fallthrough
  return;
}

// in: x, y; out: x
void FBallB(void) {
  CALL(BoundingBoxCore, 0xe23f); // get bounding box coordinates
  CheckRightScreenBBox(); return; // jump to handle any offscreen coordinates
}

// in: x; out: x
void GetEnemyBoundBox(void) {
  ram[0x0] = 0x48; // store bitmask here for now
  y = 0x44; // store another bitmask here for now and jump
  GetMaskedOffScrBits(); return;
}

// in: x; out: x
void SmallPlatformBoundBox(void) {
  ram[0x0] = 0x8; // store bitmask here for now
  y = 0x4; // store another bitmask here for now
  GetMaskedOffScrBits(); // fallthrough
  return;
}

// in: x, y; out: x
void GetMaskedOffScrBits(void) {
  a = ram[(uint8_t)(Enemy_X_Position + x)]; // get enemy object position relative
  carry_flag = true; // to the left side of the screen
  { uint16_t diff = (uint16_t)(a - ram[ScreenLeft_X_Pos] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[0x1] = a; // store here
  a = ram[(uint8_t)(Enemy_PageLoc + x)]; // subtract borrow from current page location
  a = (uint8_t)(a - ram[ScreenLeft_PageLoc] - !carry_flag); neg_flag = (a & 0x80) != 0; // of left side
  // Original branch: if enemy object is beyond left edge, branch
  if (!neg_flag) {
    a |= ram[0x1]; zero_flag = a == 0;
    if (zero_flag) { goto CMBits; } // if precisely at the left edge, branch
    y = ram[0x0]; // if to the right of left edge, use value in $00 for A
  }
  
CMBits:
  a = y; // otherwise use contents of Y
  a &= ram[Enemy_OffscreenBits]; zero_flag = a == 0; // preserve bitwise whatever's in here
  ram[EnemyOffscrBitsMasked + x] = a; // save masked offscreen bits here
  // if anything set here, branch
  if (!zero_flag) {
    MoveBoundBoxOffscreen();
    return;
  }
  SetupEOffsetFBBox(); return; // otherwise, do something else
}

// in: x; out: x
void LargePlatformBoundBox(void) {
  x++; // increment X to get the proper offset
  CALL(GetXOffscreenBits, 0xe276); // then jump directly to the sub for horizontal offscreen bits
  x--; // decrement to return to original offset
  // if completely offscreen, branch to put entire bounding
  // box offscreen, otherwise start getting coordinates
  if (a >= 0xfe) {
    MoveBoundBoxOffscreen();
    return;
  }
  SetupEOffsetFBBox(); // fallthrough
  return;
}

// in: x; out: x
void SetupEOffsetFBBox(void) {
  a = x; // add 1 to offset to properly address
  carry_flag = false; // the enemy object memory locations
  a = (uint8_t)(a + 0x1 + carry_flag);
  x = a;
  y = 0x1; // load 1 as offset here, same reason
  CALL(BoundingBoxCore, 0xe285); // do a sub to get the coordinates of the bounding box
  CheckRightScreenBBox(); return; // jump to handle offscreen coordinates of bounding box
}

// in: x; out: none
void MoveBoundBoxOffscreen(void) {
  a = x; // multiply offset by 4
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  y = a; // use as offset here
  a = 0xff;
  ram[EnemyBoundingBoxCoord + y] = a; // load value into four locations here and leave
  ram[(EnemyBoundingBoxCoord + 1) + y] = a;
  ram[(EnemyBoundingBoxCoord + 2) + y] = a;
  ram[(EnemyBoundingBoxCoord + 3) + y] = a;
  return;
}

// in: x, y; out: x, y
void BoundingBoxCore(void) {
  ram[0x0] = x; // save offset here
  ram[0x2] = ram[SprObject_Rel_YPos + y]; // store object coordinates relative to screen; vertically and horizontally, respectively
  ram[0x1] = ram[SprObject_Rel_XPos + y];
  a = x; // multiply offset by four and save to stack
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  ram[0x100 + sp--] = a;
  y = a; // use as offset for Y, X is left alone
  a = ram[SprObj_BoundBoxCtrl + x]; // load value here to be used as offset for X
  a = (uint8_t)(a << 1); // multiply that by four and use as X
  a = (uint8_t)(a << 1);
  x = a;
  a = ram[0x1]; // add the first number in the bounding box data to the
  carry_flag = false; // relative horizontal coordinate using enemy object offset
  a = (uint8_t)(a + ROM(BoundBoxCtrlData + x) + carry_flag); // and store somewhere using same offset * 4
  ram[BoundingBox_UL_Corner + y] = a; // store here
  a = ram[0x1];
  carry_flag = false;
  a = (uint8_t)(a + ROM((BoundBoxCtrlData + 2) + x) + carry_flag); // add the third number in the bounding box data to the
  ram[BoundingBox_LR_Corner + y] = a; // relative horizontal coordinate and store
  x++; // increment both offsets
  y++;
  a = ram[0x2]; // add the second number to the relative vertical coordinate
  carry_flag = false; // using incremented offset and store using the other
  a = (uint8_t)(a + ROM(BoundBoxCtrlData + x) + carry_flag); // incremented offset
  ram[BoundingBox_UL_Corner + y] = a;
  a = ram[0x2];
  carry_flag = false;
  a = (uint8_t)(a + ROM((BoundBoxCtrlData + 2) + x) + carry_flag); // add the fourth number to the relative vertical coordinate
  ram[BoundingBox_LR_Corner + y] = a; // and store
  a = ram[0x100 + ++sp]; // get original offset loaded into $00 * y from stack
  y = a; // use as Y
  x = ram[0x0]; // get original offset and use as X again
  return;
}

// in: x, y; out: x
void CheckRightScreenBBox(void) {
  a = ram[ScreenLeft_X_Pos]; // add 128 pixels to left side of screen
  carry_flag = false; // and store as horizontal coordinate of middle
  { uint16_t sum = a + 0x80 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x2] = a;
  a = ram[ScreenLeft_PageLoc]; // add carry to page location of left side of screen
  a = (uint8_t)(a + 0x0 + carry_flag); // and store as page location of middle
  ram[0x1] = a;
  a = ram[(uint8_t)(SprObject_X_Position + x)]; // get horizontal coordinate
  carry_flag = a >= ram[0x2]; // compare against middle horizontal coordinate
  a = ram[(uint8_t)(SprObject_PageLoc + x)]; // get page location
  { uint16_t diff = (uint16_t)(a - ram[0x1] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract from middle page location
  // Original branch: if object is on the left side of the screen, branch
  if (carry_flag) {
    a = ram[BoundingBox_DR_XPos + y]; neg_flag = (a & 0x80) != 0; // check right-side edge of bounding box for offscreen
    // Original branch: coordinates, branch if still on the screen
    if (!neg_flag) {
      a = 0xff; // load offscreen value here to use on one or both horizontal sides
      x = ram[BoundingBox_UL_XPos + y]; neg_flag = (x & 0x80) != 0; // check left-side edge of bounding box for offscreen
      // Original branch: coordinates, and branch if still on the screen
      if (!neg_flag) {
        ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
      }
      // SORte:
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // NoOfs:
    x = ram[ObjectOffset]; // get object offset and leave
    return;
  }
  // CheckLeftScreenBBox:
  a = ram[BoundingBox_UL_XPos + y]; neg_flag = (a & 0x80) != 0; // check left-side edge of bounding box for offscreen
  // Original branch: coordinates, and branch if still on the screen
  if (neg_flag) {
    carry_flag = a >= 0xa0; // check to see if left-side edge is in the middle of the
    if (!carry_flag) { goto NoOfs2; } // screen or really offscreen, and branch if still on
    a = 0x0;
    x = ram[BoundingBox_DR_XPos + y]; neg_flag = (x & 0x80) != 0; // check right-side edge of bounding box for offscreen
    // Original branch: coordinates, branch if still onscreen
    if (neg_flag) {
      ram[BoundingBox_DR_XPos + y] = a; // store offscreen value for right side
    }
    // SOLft:
    ram[BoundingBox_UL_XPos + y] = a; // store offscreen value for left side
  }
  
NoOfs2:
  x = ram[ObjectOffset]; // get object offset and leave
  return;
}

// -------------------------------------------------------------------------------------
// $06 - second object's offset
// $07 - counter
// in: y; out: y, carry
void PlayerCollisionCore(void) {
  x = 0x0; // initialize X to use player's bounding box for comparison
  SprObjectCollisionCore(); // fallthrough
  return;
}

// in: x, y; out: y, carry
void SprObjectCollisionCore(void) {
  ram[0x6] = y; // save contents of Y here
  ram[0x7] = 0x1; // save value 1 here as counter, compare horizontal coordinates first
  do {
    // CollisionCoreLoop:
    a = ram[BoundingBox_UL_Corner + y]; // compare left/top coordinates
    carry_flag = a >= ram[BoundingBox_UL_Corner + x]; // of first and second objects' bounding boxes
    // Original branch: if first left/top => second, branch
    if (!carry_flag) {
      { uint8_t m = ram[BoundingBox_LR_Corner + x]; carry_flag = a >= m; zero_flag = a == m; } // otherwise compare to right/bottom of second
      // Original branch: if first left/top < second right/bottom, branch elsewhere
      if (carry_flag) {
        if (zero_flag) { goto CollisionFound; } // if somehow equal, collision, thus branch
        a = ram[BoundingBox_LR_Corner + y]; // if somehow greater, check to see if bottom of
        carry_flag = a >= ram[BoundingBox_UL_Corner + y]; // first object's bounding box is greater than its top
        if (!carry_flag) { goto CollisionFound; } // if somehow less, vertical wrap collision, thus branch
        carry_flag = a >= ram[BoundingBox_UL_Corner + x]; // otherwise compare bottom of first bounding box to the top
        if (carry_flag) { goto CollisionFound; } // of second box, and if equal or greater, collision, thus branch
        y = ram[0x6]; // otherwise return with carry clear and Y = $0006
        return; // note horizontal wrapping never occurs
      }
      // SecondBoxVerticalChk:
      a = ram[BoundingBox_LR_Corner + x]; // check to see if the vertical bottom of the box
      carry_flag = a >= ram[BoundingBox_UL_Corner + x]; // is greater than the vertical top
      if (!carry_flag) { goto CollisionFound; } // if somehow less, vertical wrap collision, thus branch
      a = ram[BoundingBox_LR_Corner + y]; // otherwise compare horizontal right or vertical bottom
      carry_flag = a >= ram[BoundingBox_UL_Corner + x]; // of first box with horizontal left or vertical top of second box
      if (carry_flag) { goto CollisionFound; } // if equal or greater, collision, thus branch
      y = ram[0x6]; // otherwise return with carry clear and Y = $0006
      return;
    }
    // FirstBoxGreater:
    zero_flag = a == ram[BoundingBox_UL_Corner + x]; // compare first and second box horizontal left/vertical top again
    // Original branch: if first coordinate = second, collision, thus branch
    if (!zero_flag) {
      { uint8_t m = ram[BoundingBox_LR_Corner + x]; carry_flag = a >= m; zero_flag = a == m; } // if not, compare with second object right or bottom edge
      if (!carry_flag) { goto CollisionFound; } // if left/top of first less than or equal to right/bottom of second
      if (zero_flag) { goto CollisionFound; } // then collision, thus branch
      { uint8_t m = ram[BoundingBox_LR_Corner + y]; carry_flag = a >= m; zero_flag = a == m; } // otherwise check to see if top of first box is greater than bottom
      // Original branch: if less than or equal, no collision, branch to end
      if (carry_flag) {
        if (zero_flag) { goto NoCollisionFound; }
        a = ram[BoundingBox_LR_Corner + y]; // otherwise compare bottom of first to top of second
        carry_flag = a >= ram[BoundingBox_UL_Corner + x]; // if bottom of first is greater than top of second, vertical wrap
        if (carry_flag) { goto CollisionFound; } // collision, and branch, otherwise, proceed onwards here
      }
      
NoCollisionFound:
      carry_flag = false; // clear carry, then load value set earlier, then leave
      y = ram[0x6]; // like previous ones, if horizontal coordinates do not collide, we do
      return; // not bother checking vertical ones, because what's the point?
    }
    
CollisionFound:
    x++; // increment offsets on both objects to check
    y++; // the vertical coordinates
    ram[0x7]--; neg_flag = (ram[0x7] & 0x80) != 0; // decrement counter to reflect this
  } while (!neg_flag); // if counter not expired, branch to loop
  carry_flag = true; // otherwise we already did both sets, therefore collision, so set carry
  y = ram[0x6]; // load original value set here earlier, then leave
  return;
}

// -------------------------------------------------------------------------------------
// $02 - modified y coordinate
// $03 - stores metatile involved in block buffer collisions
// $04 - comes in with offset to block buffer adder data, goes out with low nybble x/y coordinate
// $05 - modified x coordinate
// $06-$07 - block buffer address
// in: a, x, y; out: a, x, y, zero
void BlockBufferChk_Enemy(void) {
  ram[0x100 + sp--] = a; // save contents of A to stack
  a = x;
  carry_flag = false; // add 1 to X to run sub with enemy offset in mind
  a = (uint8_t)(a + 0x1 + carry_flag);
  x = a;
  a = ram[0x100 + ++sp]; // pull A from stack and jump elsewhere
  BBChk_E(); return; // supposedly used once to set offset for
}

// in: x; out: a, x, y, zero
void BlockBufferChk_FBall(void) {
  y = 0x1a; // set offset for block buffer adder data
  a = x;
  carry_flag = false;
  a = (uint8_t)(a + 0x7 + carry_flag); // add seven bytes to use
  x = a;
  // ResJmpM:
  a = 0x0; // set A to return vertical coordinate
  BBChk_E(); // fallthrough
  return;
}

// in: a, x, y; out: a, x, y, zero
void BBChk_E(void) {
  CALL(BlockBufferCollision, 0xe3a7); // do collision detection subroutine for sprite object
  x = ram[ObjectOffset]; // get object offset
  zero_flag = a == 0x0; // check to see if object bumped into anything
  return;
}

// in: y; out: a, y
void BlockBufferColli_Feet(void) {
  y++; // if branched here, increment to next set of adders
  BlockBufferColli_Head(); // fallthrough
  return;
}

// in: y; out: a, y, zero
void BlockBufferColli_Head(void) {
  a = 0x0; // set flag to return vertical coordinate
  
  loc_58350(); return; // BIT instruction opcode
}

// in: y; out: a, y, zero
void BlockBufferColli_Side(void) {
  a = 0x1; // set flag to return horizontal coordinate
  loc_58350(); // fallthrough
  return;
}

// in: a, y; out: a, y, zero
void loc_58350(void) {
  x = 0x0; // set offset for player object
  BlockBufferCollision(); // fallthrough
  return;
}

// in: a, x, y; out: a, y, zero
void BlockBufferCollision(void) {
  ram[0x100 + sp--] = a; // save contents of A to stack
  ram[0x4] = y; // save contents of Y here
  a = ROM(BlockBuffer_X_Adder + y); // add horizontal coordinate
  carry_flag = false; // of object to value obtained using Y as offset
  { uint16_t sum = a + ram[(uint8_t)(SprObject_X_Position + x)] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  ram[0x5] = a; // store here
  a = ram[(uint8_t)(SprObject_PageLoc + x)];
  a = (uint8_t)(a + 0x0 + carry_flag); // add carry to page location
  a &= 0x1; // get LSB, mask out all other bits
  carry_flag = (a & 1) != 0; a = a >> 1; // move to carry
  a |= ram[0x5]; // get stored value
  a = (uint8_t)(a >> 1 | carry_flag << 7); // rotate carry to MSB of A
  a = a >> 1; // and effectively move high nybble to
  a = a >> 1; // lower, LSB which became MSB will be
  a = a >> 1; // d4 at this point
  CALL(GetBlockBufferAddr, 0xe40a); // get address of block buffer into $06, $07
  y = ram[0x4]; // get old contents of Y
  a = ram[(uint8_t)(SprObject_Y_Position + x)]; // get vertical coordinate of object
  carry_flag = false;
  a = (uint8_t)(a + ROM(BlockBuffer_Y_Adder + y) + carry_flag); // add it to value obtained using Y as offset
  a &= 0b11110000; // mask out low nybble
  carry_flag = true;
  a = (uint8_t)(a - 0x20 - !carry_flag); // subtract 32 pixels for the status bar
  ram[0x2] = a; // store result here
  y = a; // use as offset for block buffer
  ram[0x3] = read_byte(indirect_y_addr(0x6)); // check current content of block buffer; and store here
  y = ram[0x4]; // get old contents of Y again
  a = ram[0x100 + ++sp]; zero_flag = a == 0; // pull A from stack
  // Original branch: if A = 1, branch
  if (zero_flag) {
    a = ram[(uint8_t)(SprObject_Y_Position + x)]; // if A = 0, load vertical coordinate
  } else {
    // RetXC:
    a = ram[(uint8_t)(SprObject_X_Position + x)]; // otherwise load horizontal coordinate
  }
  // RetYC:
  a &= 0b00001111; // and mask out high nybble
  ram[0x4] = a; // store masked out result here
  a = ram[0x3]; zero_flag = a == 0; // get saved content of block buffer
  return; // and leave
}

// -------------------------------------------------------------------------------------
// unused byte
// -------------------------------------------------------------------------------------
// $00 - offset to vine Y coordinate adder
// $02 - offset to sprite data
// in: y; out: y
void DrawVine(void) {
  ram[0x0] = y; // save offset here
  a = ram[Enemy_Rel_YPos]; // get relative vertical coordinate
  carry_flag = false;
  a = (uint8_t)(a + ROM(VineYPosAdder + y) + carry_flag); // add value using offset in Y to get value
  x = ram[VineObjOffset + y]; // get offset to vine
  y = ram[Enemy_SprDataOffset + x]; // get sprite data offset
  ram[0x2] = y; // store sprite data offset here
  CALL(SixSpriteStacker, 0xe448); // stack six sprites on top of each other vertically
  a = ram[Enemy_Rel_XPos]; // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store in first, third and fifth sprites
  ram[(Sprite_X_Position + 8) + y] = a;
  ram[(Sprite_X_Position + 16) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x6 + carry_flag); // add six pixels to second, fourth and sixth sprites
  ram[(Sprite_X_Position + 4) + y] = a; // to give characteristic staggered vine shape to
  ram[(Sprite_X_Position + 12) + y] = a; // our vertical stack of sprites
  ram[(Sprite_X_Position + 20) + y] = a;
  a = 0b00100001; // set bg priority and palette attribute bits
  ram[Sprite_Attributes + y] = a; // set in first, third and fifth sprites
  ram[(Sprite_Attributes + 8) + y] = a;
  ram[(Sprite_Attributes + 16) + y] = a;
  a |= 0b01000000; // additionally, set horizontal flip bit
  ram[(Sprite_Attributes + 4) + y] = a; // for second, fourth and sixth sprites
  ram[(Sprite_Attributes + 12) + y] = a;
  ram[(Sprite_Attributes + 20) + y] = a;
  x = 0x5; // set tiles for six sprites
  do {
    // VineTL:
    ram[Sprite_Tilenumber + y] = 0xe1; // set tile number for sprite
    y++; // move offset to next sprite data
    y++;
    y++;
    y++;
    x--; neg_flag = (x & 0x80) != 0; // move onto next sprite
  } while (!neg_flag); // loop until all sprites are done
  y = ram[0x2]; // get original offset
  a = ram[0x0]; zero_flag = a == 0; // get offset to vine adding data
  // Original branch: if offset not zero, skip this part
  if (zero_flag) {
    ram[Sprite_Tilenumber + y] = 0xe0; // set other tile number for top of vine
  }
  // SkpVTop:
  x = 0x0; // start with the first sprite again
  do {
    // ChkFTop:
    a = ram[VineStart_Y_Position]; // get original starting vertical coordinate
    carry_flag = true;
    a = (uint8_t)(a - ram[Sprite_Y_Position + y] - !carry_flag); // subtract top-most sprite's Y coordinate
    // if two coordinates are less than 100/$64 pixels
    // Original branch: apart, skip this to leave sprite alone
    if (a >= 0x64) {
      ram[Sprite_Y_Position + y] = 0xf8; // otherwise move sprite offscreen
    }
    // NextVSp:
    y++; // move offset to next OAM data
    y++;
    y++;
    y++;
    x++; // move onto next sprite
    zero_flag = x == 0x6; // do this until all sprites are checked
  } while (!zero_flag);
  y = ram[0x0]; // return offset set earlier
  return;
}

// in: a, y; out: y, carry
void SixSpriteStacker(void) {
  x = 0x6; // do six sprites
  do {
    // StkLp:
    ram[Sprite_Data + y] = a; // store X or Y coordinate into OAM data
    carry_flag = false;
    { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add eight pixels
    y++;
    y++; // move offset four bytes forward
    y++;
    y++;
    x--; zero_flag = x == 0; // do another sprite
  } while (!zero_flag); // do this until all sprites are done
  y = ram[0x2]; // get saved OAM data offset and leave
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void DrawHammer(void) {
  y = ram[Misc_SprDataOffset + x]; // get misc object OAM data offset
  a = ram[TimerControl]; zero_flag = a == 0;
  // Original branch: if master timer control set, skip this part
  if (zero_flag) {
    a = ram[(uint8_t)(Misc_State + x)]; // otherwise get hammer's state
    a &= 0b01111111; // mask out d7
    zero_flag = a == 0x1; // check to see if set to 1 yet
    if (zero_flag) { goto GetHPose; } // if so, branch
  }
  // ForceHPose:
  x = 0x0; // reset offset here
  goto RenderH; // do unconditional branch to rendering part
  
GetHPose:
  a = ram[FrameCounter]; // get frame counter
  a = a >> 1; // move d3-d2 to d1-d0
  a = a >> 1;
  a &= 0b00000011; // mask out all but d1-d0 (changes every four frames)
  x = a; // use as timing offset
  
RenderH:
  a = ram[Misc_Rel_YPos]; // get relative vertical coordinate
  carry_flag = false;
  a = (uint8_t)(a + ROM(FirstSprYPos + x) + carry_flag); // add first sprite vertical adder based on offset
  ram[Sprite_Y_Position + y] = a; // store as sprite Y coordinate for first sprite
  carry_flag = false;
  a = (uint8_t)(a + ROM(SecondSprYPos + x) + carry_flag); // add second sprite vertical adder based on offset
  ram[(Sprite_Y_Position + 4) + y] = a; // store as sprite Y coordinate for second sprite
  a = ram[Misc_Rel_XPos]; // get relative horizontal coordinate
  carry_flag = false;
  a = (uint8_t)(a + ROM(FirstSprXPos + x) + carry_flag); // add first sprite horizontal adder based on offset
  ram[Sprite_X_Position + y] = a; // store as sprite X coordinate for first sprite
  carry_flag = false;
  { uint16_t sum = a + ROM(SecondSprXPos + x) + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add second sprite horizontal adder based on offset
  ram[(Sprite_X_Position + 4) + y] = a; // store as sprite X coordinate for second sprite
  ram[Sprite_Tilenumber + y] = ROM(FirstSprTilenum + x); // get and store tile number of first sprite
  ram[(Sprite_Tilenumber + 4) + y] = ROM(SecondSprTilenum + x); // get and store tile number of second sprite
  a = ROM(HammerSprAttrib + x);
  ram[Sprite_Attributes + y] = a; // get and store attribute bytes for both
  ram[(Sprite_Attributes + 4) + y] = a; // note in this case they use the same data
  x = ram[ObjectOffset]; // get misc object offset
  a = ram[Misc_OffscreenBits];
  a &= 0b11111100; zero_flag = a == 0; // check offscreen bits
  // if all bits clear, leave object alone
  if (zero_flag) {
    return;
  }
  ram[(uint8_t)(Misc_State + x)] = 0x0; // otherwise nullify misc object state
  a = 0xf8;
  CALL(DumpTwoSpr, 0xe53f); // do sub to move hammer sprites offscreen
  // NoHOffscr:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00-$01 - used to hold tile numbers ($01 addressed in draw floatey number part)
// $02 - used to hold Y coordinate for floatey number
// $03 - residual byte used for flip (but value set here affects nothing)
// $04 - attribute byte for floatey number
// $05 - used as X coordinate for floatey number
// in: x; out: none
void FlagpoleGfxHandler(void) {
  y = ram[Enemy_SprDataOffset + x]; // get sprite data offset for flagpole flag
  a = ram[Enemy_Rel_XPos]; // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X coordinate for first sprite
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels and store
  ram[(Sprite_X_Position + 4) + y] = a; // as X coordinate for second and third sprites
  ram[(Sprite_X_Position + 8) + y] = a;
  carry_flag = false;
  { uint16_t sum = a + 0xc + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add twelve more pixels and
  ram[0x5] = a; // store here to be used later by floatey number
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
  CALL(DumpTwoSpr, 0xe566); // and do sub to dump into first and second sprites
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add eight pixels
  ram[(Sprite_Y_Position + 8) + y] = a; // and store into third sprite
  ram[0x2] = ram[FlagpoleFNum_Y_Pos]; // get vertical coordinate for floatey number; store it here
  a = 0x1;
  ram[0x3] = a; // set value for flip which will not be used, and
  ram[0x4] = a; // attribute byte for floatey number
  ram[Sprite_Attributes + y] = a; // set attribute bytes for all three sprites
  ram[(Sprite_Attributes + 4) + y] = a;
  ram[(Sprite_Attributes + 8) + y] = a;
  a = 0x7e;
  ram[Sprite_Tilenumber + y] = a; // put triangle shaped tile
  ram[(Sprite_Tilenumber + 8) + y] = a; // into first and third sprites
  ram[(Sprite_Tilenumber + 4) + y] = 0x7f; // put skull tile into second sprite
  a = ram[FlagpoleCollisionYPos]; zero_flag = a == 0; // get vertical coordinate at time of collision
  // Original branch: if zero, branch ahead
  if (!zero_flag) {
    a = y;
    carry_flag = false; // add 12 bytes to sprite data offset
    a = (uint8_t)(a + 0xc + carry_flag);
    y = a; // put back in Y
    a = ram[FlagpoleScore]; // get offset used to award points for touching flagpole
    a = (uint8_t)(a << 1); // multiply by 2 to get proper offset here
    x = a;
    ram[0x0] = ROM(FlagpoleScoreNumTiles + x); // get appropriate tile data
    a = ROM((FlagpoleScoreNumTiles + 1) + x);
    CALL(DrawOneSpriteRow, 0xe5a6); // use it to render floatey number
  }
  // ChkFlagOffscreen:
  x = ram[ObjectOffset]; // get object offset for flag
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  a = ram[Enemy_OffscreenBits]; // get offscreen bits
  a &= 0b00001110; zero_flag = a == 0; // mask out all but d3-d1
  // if none of these bits set, branch to leave
  if (zero_flag) {
    return;
  }
  MoveSixSpritesOffscreen(); // fallthrough
  return;
}

// -------------------------------------------------------------------------------------
// in: y; out: none
void MoveSixSpritesOffscreen(void) {
  a = 0xf8; // set offscreen coordinate if jumping here
  DumpSixSpr(); // fallthrough
  return;
}

// in: a, y; out: none
void DumpSixSpr(void) {
  ram[(Sprite_Data + 20) + y] = a; // dump A contents
  ram[(Sprite_Data + 16) + y] = a; // into third row sprites
  DumpFourSpr(); // fallthrough
  return;
}

// in: a, y; out: none
void DumpFourSpr(void) {
  ram[(Sprite_Data + 12) + y] = a; // into second row sprites
  DumpThreeSpr(); // fallthrough
  return;
}

// in: a, y; out: none
void DumpThreeSpr(void) {
  ram[(Sprite_Data + 8) + y] = a;
  DumpTwoSpr(); // fallthrough
  return;
}

// in: a, y; out: none
void DumpTwoSpr(void) {
  ram[(Sprite_Data + 4) + y] = a; // and into first row sprites
  ram[Sprite_Data + y] = a;
  // ExitDumpSpr:
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void DrawLargePlatform(void) {
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  ram[0x2] = y; // store here
  y++; // add 3 to it for offset
  y++; // to X coordinate
  y++;
  a = ram[Enemy_Rel_XPos]; // get horizontal relative coordinate
  CALL(SixSpriteStacker, 0xe5d5); // store X coordinates using A as base, stack horizontally
  x = ram[ObjectOffset];
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
  CALL(DumpFourSpr, 0xe5dc); // dump into first four sprites as Y coordinate
  y = ram[AreaType];
  { uint8_t m = 0x3; carry_flag = y >= m; zero_flag = y == m; } // check for castle-type level
  if (!zero_flag) {
    y = ram[SecondaryHardMode]; zero_flag = y == 0; // check for secondary hard mode flag set
    if (zero_flag) { goto SetLast2Platform; } // branch if not set elsewhere
  }
  // ShrinkPlatform:
  a = 0xf8; // load offscreen coordinate if flag set or castle-type level
  
SetLast2Platform:
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  ram[(Sprite_Y_Position + 16) + y] = a; // store vertical coordinate or offscreen
  ram[(Sprite_Y_Position + 20) + y] = a; // coordinate into last two sprites as Y coordinate
  a = 0x5b; // load default tile for platform (girder)
  x = ram[CloudTypeOverride]; zero_flag = x == 0;
  // Original branch: if cloud level override flag not set, use
  if (!zero_flag) {
    a = 0x75; // otherwise load other tile for platform (puff)
  }
  // SetPlatformTilenum:
  x = ram[ObjectOffset]; // get enemy object buffer offset
  y++; // increment Y for tile offset
  CALL(DumpSixSpr, 0xe602); // dump tile number into all six sprites
  a = 0x2; // set palette controls
  y++; // increment Y for sprite attributes
  CALL(DumpSixSpr, 0xe608); // dump attributes into all six sprites
  x++; // increment X for enemy objects
  CALL(GetXOffscreenBits, 0xe60c); // get offscreen bits again
  x--;
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // rotate d7 into carry, save remaining
  ram[0x100 + sp--] = a; // bits to the stack
  if (carry_flag) {
    ram[Sprite_Y_Position + y] = 0xf8; // if d7 was set, move first sprite offscreen
  }
  // SChk2:
  a = ram[0x100 + ++sp]; // get bits from stack
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // rotate d6 into carry
  ram[0x100 + sp--] = a; // save to stack
  if (carry_flag) {
    ram[(Sprite_Y_Position + 4) + y] = 0xf8; // if d6 was set, move second sprite offscreen
  }
  // SChk3:
  a = ram[0x100 + ++sp]; // get bits from stack
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // rotate d5 into carry
  ram[0x100 + sp--] = a; // save to stack
  if (carry_flag) {
    ram[(Sprite_Y_Position + 8) + y] = 0xf8; // if d5 was set, move third sprite offscreen
  }
  // SChk4:
  a = ram[0x100 + ++sp]; // get bits from stack
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // rotate d4 into carry
  ram[0x100 + sp--] = a; // save to stack
  if (carry_flag) {
    ram[(Sprite_Y_Position + 12) + y] = 0xf8; // if d4 was set, move fourth sprite offscreen
  }
  // SChk5:
  a = ram[0x100 + ++sp]; // get bits from stack
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // rotate d3 into carry
  ram[0x100 + sp--] = a; // save to stack
  if (carry_flag) {
    ram[(Sprite_Y_Position + 16) + y] = 0xf8; // if d3 was set, move fifth sprite offscreen
  }
  // SChk6:
  a = ram[0x100 + ++sp]; // get bits from stack
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // rotate d2 into carry
  // Original branch: save to stack
  if (carry_flag) {
    ram[(Sprite_Y_Position + 20) + y] = 0xf8; // if d2 was set, move sixth sprite offscreen
  }
  // SLChk:
  a = ram[Enemy_OffscreenBits]; // check d7 of offscreen bits
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // and if d7 is not set, skip sub
  if (!carry_flag) {
    return;
  }
  CALL(MoveSixSpritesOffscreen, 0xe653); // otherwise branch to move all sprites offscreen
  // ExDLPl:
  return;
}

// in: x; out: x
void JCoinGfxHandler(void) {
  goto JCoinGfxHandler;
  // -------------------------------------------------------------------------------------
  
DrawFloateyNumber_Coin:
  a = ram[FrameCounter]; // get frame counter
  carry_flag = (a & 1) != 0; a = a >> 1; // divide by 2
  // Original branch: branch if d0 not set to raise number every other frame
  if (!carry_flag) {
    ram[(uint8_t)(Misc_Y_Position + x)]--; // otherwise, decrement vertical coordinate
  }
  // NotRsNum:
  a = ram[(uint8_t)(Misc_Y_Position + x)]; // get vertical coordinate
  CALL(DumpTwoSpr, 0xe660); // dump into both sprites
  a = ram[Misc_Rel_XPos]; // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a; // store as X coordinate for first sprite
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels
  ram[(Sprite_X_Position + 4) + y] = a; // store as X coordinate for second sprite
  a = 0x2;
  ram[Sprite_Attributes + y] = a; // store attribute byte in both sprites
  ram[(Sprite_Attributes + 4) + y] = a;
  ram[Sprite_Tilenumber + y] = 0xf7; // put tile numbers into both sprites
  ram[(Sprite_Tilenumber + 4) + y] = 0xfb; // that resemble "200"
  return; // then jump to leave (why not an rts here instead?)
  
JCoinGfxHandler:
  y = ram[Misc_SprDataOffset + x]; // get coin/floatey number's OAM data offset
  a = ram[(uint8_t)(Misc_State + x)]; // get state of misc object
  carry_flag = a >= 0x2; // if 2 or greater, 
  if (carry_flag) { goto DrawFloateyNumber_Coin; } // branch to draw floatey number
  a = ram[(uint8_t)(Misc_Y_Position + x)]; // store vertical coordinate as
  ram[Sprite_Y_Position + y] = a; // Y coordinate for first sprite
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels
  ram[(Sprite_Y_Position + 4) + y] = a; // store as Y coordinate for second sprite
  a = ram[Misc_Rel_XPos]; // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a;
  ram[(Sprite_X_Position + 4) + y] = a; // store as X coordinate for first and second sprites
  a = ram[FrameCounter]; // get frame counter
  carry_flag = (a & 1) != 0; a = a >> 1; // divide by 2 to alter every other frame
  a &= 0b00000011; // mask out d2-d1
  x = a; // use as graphical offset
  a = ROM(JumpingCoinTiles + x); // load tile number
  y++; // increment OAM data offset to write tile numbers
  CALL(DumpTwoSpr, 0xe6af); // do sub to dump tile number into both sprites
  y--; // decrement to get old offset
  ram[Sprite_Attributes + y] = 0x2; // set attribute byte in first sprite
  ram[(Sprite_Attributes + 4) + y] = 0x82; // set attribute byte with vertical flip in second sprite
  x = ram[ObjectOffset]; // get misc object offset
  // ExJCGfx:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00-$01 - used to hold tiles for drawing the power-up, $00 also used to hold power-up type
// $02 - used to hold bottom row Y position
// $03 - used to hold flip control (not used here)
// $04 - used to hold sprite attributes
// $05 - used to hold X position
// $07 - counter
// tiles arranged in top left, right, bottom left, right order
// in: none; out: x
void DrawPowerUp(void) {
  y = ram[(Enemy_SprDataOffset + 5)]; // get power-up's sprite data offset
  a = ram[Enemy_Rel_YPos]; // get relative vertical coordinate
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels
  ram[0x2] = a; // store result here
  ram[0x5] = ram[Enemy_Rel_XPos]; // get relative horizontal coordinate; store here
  x = ram[PowerUpType]; // get power-up type
  a = ROM(PowerUpAttributes + x); // get attribute data for power-up type
  a |= ram[(Enemy_SprAttrib + 5)]; // add background priority bit if set
  ram[0x4] = a; // store attributes here
  a = x;
  ram[0x100 + sp--] = a; // save power-up type to the stack
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // multiply by four to get proper offset
  x = a; // use as X
  a = 0x1;
  ram[0x7] = a; // set counter here to draw two rows of sprite object
  ram[0x3] = a; // init d1 of flip control
  do {
    // PUpDrawLoop:
    ram[0x0] = ROM(PowerUpGfxTable + x); // load left tile of power-up object
    a = ROM((PowerUpGfxTable + 1) + x); // load right tile
    CALL(DrawOneSpriteRow, 0xe701); // branch to draw one row of our power-up object
    ram[0x7]--; neg_flag = (ram[0x7] & 0x80) != 0; // decrement counter
  } while (!neg_flag); // branch until two rows are drawn
  y = ram[(Enemy_SprDataOffset + 5)]; // get sprite data offset again
  a = ram[0x100 + ++sp]; zero_flag = a == 0; // pull saved power-up type from the stack
  // Original branch: if regular mushroom, branch, do not change colors or flip
  if (!zero_flag) {
    zero_flag = a == 0x3;
    if (zero_flag) { goto PUpOfs; } // if 1-up mushroom, branch, do not change colors or flip
    ram[0x0] = a; // store power-up type here now
    a = ram[FrameCounter]; // get frame counter
    a = a >> 1; // divide by 2 to change colors every two frames
    a &= 0b00000011; // mask out all but d1 and d0 (previously d2 and d1)
    a |= ram[(Enemy_SprAttrib + 5)]; // add background priority bit if any set
    ram[Sprite_Attributes + y] = a; // set as new palette bits for top left and
    ram[(Sprite_Attributes + 4) + y] = a; // top right sprites for fire flower and star
    x = ram[0x0];
    x--; zero_flag = x == 0; // check power-up type for fire flower
    // Original branch: if found, skip this part
    if (!zero_flag) {
      ram[(Sprite_Attributes + 8) + y] = a; // otherwise set new palette bits  for bottom left
      ram[(Sprite_Attributes + 12) + y] = a; // and bottom right sprites as well for star only
    }
    // FlipPUpRightSide:
    a = ram[(Sprite_Attributes + 4) + y];
    a |= 0b01000000; // set horizontal flip bit for top right sprite
    ram[(Sprite_Attributes + 4) + y] = a;
    a = ram[(Sprite_Attributes + 12) + y];
    a |= 0b01000000; // set horizontal flip bit for bottom right sprite
    ram[(Sprite_Attributes + 12) + y] = a; // note these are only done for fire flower and star power-ups
  }
  
PUpOfs:
  SprObjectOffscrChk(); return; // jump to check to see if power-up is offscreen at all, then leave
}

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
// in: x; out: x
void EnemyGfxHandler(void) {
  ram[0x2] = ram[(uint8_t)(Enemy_Y_Position + x)]; // get enemy object vertical position
  ram[0x5] = ram[Enemy_Rel_XPos]; // get enemy object horizontal position; relative to screen
  ram[0xeb] = ram[Enemy_SprDataOffset + x]; // get sprite data offset
  ram[VerticalFlipFlag] = 0x0; // initialize vertical flip flag by default
  ram[0x3] = ram[(uint8_t)(Enemy_MovingDir + x)]; // get enemy object moving direction
  ram[0x4] = ram[Enemy_SprAttrib + x]; // get enemy object sprite attributes
  a = ram[(uint8_t)(Enemy_ID + x)];
  // is enemy object piranha plant?
  // Original branch: if not, branch
  if (a == PiranhaPlant) {
    y = ram[(uint8_t)(PiranhaPlant_Y_Speed + x)]; neg_flag = (y & 0x80) != 0;
    if (neg_flag) { goto CheckForRetainerObj; } // if piranha plant moving upwards, branch
    y = ram[(EnemyFrameTimer + x) & 0x7ff]; zero_flag = y == 0;
    if (zero_flag) { goto CheckForRetainerObj; } // if timer for movement expired, branch
    return; // if all conditions fail, leave
  }
  
CheckForRetainerObj:
  a = ram[(uint8_t)(Enemy_State + x)]; // store enemy state
  ram[0xed] = a;
  a &= 0b00011111; // nullify all but 5 LSB and use as Y
  y = a;
  a = ram[(uint8_t)(Enemy_ID + x)]; // check for mushroom retainer/princess object
  // Original branch: if not found, branch
  if (a == RetainerObject) {
    y = 0x0; // if found, nullify saved state in Y
    ram[0x3] = 0x1; // set value that will not be used
    a = 0x15; // set value $15 as code for mushroom retainer/princess object
  }
  // CheckForBulletBillCV:
  // otherwise check for bullet bill object
  // Original branch: if not found, branch again
  if (a == BulletBill_CannonVar) {
    ram[0x2]--; // decrement saved vertical position
    a = 0x3;
    y = ram[(EnemyFrameTimer + x) & 0x7ff]; zero_flag = y == 0; // get timer for enemy object
    // Original branch: if expired, do not set priority bit
    if (!zero_flag) {
      a |= 0b00100000; // otherwise do so
    }
    // SBBAt:
    ram[0x4] = a; // set new sprite attributes
    y = 0x0; // nullify saved enemy state both in Y and in
    ram[0xed] = y; // memory location here
    a = 0x8; // set specific value to unconditionally branch once
  }
  // CheckForJumpspring:
  // check for jumpspring object
  if (a == JumpspringObject) {
    y = 0x3; // set enemy state -2 MSB here for jumpspring object
    x = ram[JumpspringAnimCtrl]; // get current frame number for jumpspring object
    a = ROM(JumpspringFrameOffsets + x); // load data using frame number as offset
  }
  // CheckForPodoboo:
  ram[0xef] = a; // store saved enemy object value here
  ram[0xec] = y; // and Y here (enemy state -2 MSB if not changed)
  x = ram[ObjectOffset]; // get enemy object offset
  // check for podoboo object
  // Original branch: branch if not found
  if (a == 0xc) {
    a = ram[(uint8_t)(Enemy_Y_Speed + x)]; neg_flag = (a & 0x80) != 0; // if moving upwards, branch
    if (neg_flag) { goto CheckBowserGfxFlag; }
    ram[VerticalFlipFlag]++; // otherwise, set flag for vertical flip
  }
  
CheckBowserGfxFlag:
  a = ram[BowserGfxFlag]; zero_flag = a == 0; // if not drawing bowser at all, skip to something else
  if (!zero_flag) {
    y = 0x16; // if set to 1, draw bowser's front
    if (a != 0x1) {
      y++; // otherwise draw bowser's rear
    }
    // SBwsrGfxOfs:
    ram[0xef] = y;
  }
  // CheckForGoomba:
  y = ram[0xef]; // check value for goomba object
  // Original branch: branch if not found
  if (y == Goomba) {
    a = ram[(uint8_t)(Enemy_State + x)];
    // check for defeated state
    // Original branch: if not defeated, go ahead and animate
    if (a >= 0x2) {
      ram[0xec] = 0x4; // if defeated, write new value here
    }
    // GmbaAnim:
    a &= 0b00100000; // check for d5 set in enemy object state 
    a |= ram[TimerControl]; zero_flag = a == 0; // or timer disable flag set
    if (!zero_flag) { goto CheckBowserFront; } // if either condition true, do not animate goomba
    a = ram[FrameCounter];
    a &= 0b00001000; zero_flag = a == 0; // check for every eighth frame
    if (!zero_flag) { goto CheckBowserFront; }
    a = ram[0x3];
    a ^= 0b00000011; // invert bits to flip horizontally every eight frames
    ram[0x3] = a; // leave alone otherwise
  }
  
CheckBowserFront:
  a = ROM(EnemyAttributeData + y); // load sprite attribute using enemy object
  a |= ram[0x4]; // as offset, and add to bits already loaded
  ram[0x4] = a;
  a = ROM(EnemyGfxTableOffsets + y); // load value based on enemy object as offset
  x = a; // save as X
  y = ram[0xec]; // get previously saved value
  a = ram[BowserGfxFlag]; zero_flag = a == 0;
  // Original branch: if not drawing bowser object at all, skip all of this
  if (!zero_flag) {
    zero_flag = a == 0x1;
    if (!zero_flag) { goto CheckBowserRear; } // if not drawing front part, branch to draw the rear part
    a = ram[BowserBodyControls]; neg_flag = (a & 0x80) != 0; // check bowser's body control bits
    // Original branch: branch if d7 not set (control's bowser's mouth)      
    if (neg_flag) {
      x = 0xde; // otherwise load offset for second frame
    }
    // ChkFrontSte:
    a = ram[0xed]; // check saved enemy state
    a &= 0b00100000; zero_flag = a == 0; // if bowser not defeated, do not set flag
    if (zero_flag) { goto DrawBowser; }
    
FlipBowserOver:
    ram[VerticalFlipFlag] = x; // set vertical flip flag to nonzero
    
DrawBowser:
    goto DrawEnemyObject; // draw bowser's graphics now
    
CheckBowserRear:
    a = ram[BowserBodyControls]; // check bowser's body control bits
    a &= 0x1; zero_flag = a == 0;
    // Original branch: branch if d0 not set (control's bowser's feet)
    if (!zero_flag) {
      x = 0xe4; // otherwise load offset for second frame
    }
    // ChkRearSte:
    a = ram[0xed]; // check saved enemy state
    a &= 0b00100000; zero_flag = a == 0; // if bowser not defeated, do not set flag
    if (zero_flag) { goto DrawBowser; }
    a = ram[0x2]; // subtract 16 pixels from
    carry_flag = true; // saved vertical coordinate
    a = (uint8_t)(a - 0x10 - !carry_flag);
    ram[0x2] = a;
    goto FlipBowserOver; // jump to set vertical flip flag
  }
  // CheckForSpiny:
  // check if value loaded is for spiny
  // Original branch: if not found, branch
  if (x == 0x24) {
    // if enemy state set to $05, do this,
    // Original branch: otherwise branch
    if (y == 0x5) {
      x = 0x30; // set to spiny egg offset
      ram[0x3] = 0x2; // set enemy direction to reverse sprites horizontally
      ram[0xec] = 0x5; // set enemy state
    }
    // NotEgg:
  } else {
    // CheckForLakitu:
    // check value for lakitu's offset loaded
    // Original branch: branch if not loaded
    if (x == 0x90) {
      a = ram[0xed];
      a &= 0b00100000; zero_flag = a == 0; // check for d5 set in enemy state
      // Original branch: branch if set
      if (zero_flag) {
        a = ram[FrenzyEnemyTimer];
        carry_flag = a >= 0x10; // check timer to see if we've reached a certain range
        if (carry_flag) { goto NoLAFr; } // branch if not
        x = 0x96; // if d6 not set and timer in range, load alt frame for lakitu
      }
      
NoLAFr:
      goto CheckDefeatedState; // skip this next part if we found lakitu but alt frame not needed
    }
    // CheckUpsideDownShell:
    a = ram[0xef]; // check for enemy object => $04
    // Original branch: branch if true
    if (a < 0x4) {
      carry_flag = y >= 0x2;
      if (!carry_flag) { goto CheckRightSideUpShell; } // branch if enemy state < $02
      x = 0x5a; // set for upside-down koopa shell by default
      y = ram[0xef];
      zero_flag = y == BuzzyBeetle; // check for buzzy beetle object
      if (!zero_flag) { goto CheckRightSideUpShell; }
      x = 0x7e; // set for upside-down buzzy beetle shell if found
      ram[0x2]++; // increment vertical position by one pixel
    }
    
CheckRightSideUpShell:
    a = ram[0xec]; // check for value set here
    zero_flag = a == 0x4; // if enemy state < $02, do not change to shell, if
    if (!zero_flag) { goto CheckForHammerBro; } // enemy state => $02 but not = $04, leave shell upside-down
    x = 0x72; // set right-side up buzzy beetle shell by default
    ram[0x2]++; // increment saved vertical position by one pixel
    y = ram[0xef];
    // check for buzzy beetle object
    // Original branch: branch if found
    if (y != BuzzyBeetle) {
      x = 0x66; // change to right-side up koopa shell if not found
      ram[0x2]++; // and increment saved vertical position again
    }
    // CheckForDefdGoomba:
    zero_flag = y == Goomba; // check for goomba object (necessary if previously
    if (!zero_flag) { goto CheckForHammerBro; } // failed buzzy beetle object test)
    x = 0x54; // load for regular goomba
    a = ram[0xed]; // note that this only gets performed if enemy state => $02
    a &= 0b00100000; zero_flag = a == 0; // check saved enemy state for d5 set
    if (!zero_flag) { goto CheckForHammerBro; } // branch if set
    x = 0x8a; // load offset for defeated goomba
    ram[0x2]--; // set different value and decrement saved vertical position
  }
  
CheckForHammerBro:
  y = ram[ObjectOffset];
  a = ram[0xef]; // check for hammer bro object
  // Original branch: branch if not found
  if (a == HammerBro) {
    a = ram[0xed]; zero_flag = a == 0;
    if (zero_flag) { goto CheckToAnimateEnemy; } // branch if not in normal enemy state
    a &= 0b00001000; zero_flag = a == 0;
    if (zero_flag) { goto CheckDefeatedState; } // if d3 not set, branch further away
    x = 0xb4; // otherwise load offset for different frame
  } else {
    // CheckForBloober:
    zero_flag = x == 0x48; // check for cheep-cheep offset loaded
    if (zero_flag) { goto CheckToAnimateEnemy; } // branch if found
    a = ram[(EnemyIntervalTimer + y) & 0x7ff];
    carry_flag = a >= 0x5;
    if (carry_flag) { goto CheckDefeatedState; } // branch if some timer is above a certain point
    zero_flag = x == 0x3c; // check for bloober offset loaded
    if (!zero_flag) { goto CheckToAnimateEnemy; } // branch if not found this time
    zero_flag = a == 0x1;
    if (zero_flag) { goto CheckDefeatedState; } // branch if timer is set to certain point
    ram[0x2]++; // increment saved vertical coordinate three pixels
    ram[0x2]++;
    ram[0x2]++;
    goto CheckAnimationStop; // and do something else
  }
  
CheckToAnimateEnemy:
  a = ram[0xef]; // check for specific enemy objects
  zero_flag = a == Goomba;
  if (zero_flag) { goto CheckDefeatedState; } // branch if goomba
  zero_flag = a == 0x8;
  if (zero_flag) { goto CheckDefeatedState; } // branch if bullet bill (note both variants use $08 here)
  zero_flag = a == Podoboo;
  if (zero_flag) { goto CheckDefeatedState; } // branch if podoboo
  carry_flag = a >= 0x18; // branch if => $18
  if (carry_flag) { goto CheckDefeatedState; }
  y = 0x0;
  // check for mushroom retainer/princess object
  // Original branch: which uses different code here, branch if not found
  if (a == 0x15) {
    y++; // residual instruction
    a = ram[WorldNumber]; // are we on world 8?
    carry_flag = a >= World8;
    if (carry_flag) { goto CheckDefeatedState; } // if so, leave the offset alone (use princess)
    x = 0xa2; // otherwise, set for mushroom retainer object instead
    ram[0xec] = 0x3; // set alternate state here
    goto CheckDefeatedState; // unconditional branch
  }
  // CheckForSecondFrame:
  a = ram[FrameCounter]; // load frame counter
  a &= ROM(EnemyAnimTimingBMask + y); zero_flag = a == 0; // mask it (partly residual, one byte not ever used)
  if (!zero_flag) { goto CheckDefeatedState; } // branch if timing is off
  
CheckAnimationStop:
  a = ram[0xed]; // check saved enemy state
  a &= 0b10100000; // for d7 or d5, or check for timers stopped
  a |= ram[TimerControl]; zero_flag = a == 0;
  // Original branch: if either condition true, branch
  if (zero_flag) {
    a = x;
    carry_flag = false;
    a = (uint8_t)(a + 0x6 + carry_flag); // add $06 to current enemy offset
    x = a; // to animate various enemy objects
  }
  
CheckDefeatedState:
  a = ram[0xed]; // check saved enemy state
  a &= 0b00100000; zero_flag = a == 0; // for d5 set
  // Original branch: branch if not set
  if (!zero_flag) {
    a = ram[0xef];
    carry_flag = a >= 0x4; // check for saved enemy object => $04
    if (!carry_flag) { goto DrawEnemyObject; } // branch if less
    y = 0x1;
    ram[VerticalFlipFlag] = y; // set vertical flip flag
    y--;
    ram[0xec] = y; // init saved value here
  }
  
DrawEnemyObject:
  y = ram[0xeb]; // load sprite data offset
  CALL(DrawEnemyObjRow, 0xea4f); // draw six tiles of data
  CALL(DrawEnemyObjRow, 0xea52); // into sprite data
  CALL(DrawEnemyObjRow, 0xea55);
  x = ram[ObjectOffset]; // get enemy object offset
  y = ram[Enemy_SprDataOffset + x]; // get sprite data offset
  a = ram[0xef];
  { uint8_t m = 0x8; carry_flag = a >= m; zero_flag = a == m; } // get saved enemy object and check
  if (!zero_flag) { goto CheckForVerticalFlip; } // for bullet bill, branch if not found
  
SkipToOffScrChk:
  SprObjectOffscrChk(); return; // jump if found
  
CheckForVerticalFlip:
  a = ram[VerticalFlipFlag]; zero_flag = a == 0; // check if vertical flip flag is set here
  // Original branch: branch if not
  if (!zero_flag) {
    a = ram[Sprite_Attributes + y]; // get attributes of first sprite we dealt with
    a |= 0b10000000; // set bit for vertical flip
    y++;
    y++; // increment two bytes so that we store the vertical flip
    CALL(DumpSixSpr, 0xea72); // in attribute bytes of enemy obj sprite data
    y--;
    y--; // now go back to the Y coordinate offset
    a = y;
    x = a; // give offset to X
    a = ram[0xef];
    // check saved enemy object for hammer bro
    if (a != HammerBro) {
      zero_flag = a == Lakitu; // check saved enemy object for lakitu
      if (zero_flag) { goto FlipEnemyVertically; } // branch for hammer bro or lakitu
      carry_flag = a >= 0x15;
      if (carry_flag) { goto FlipEnemyVertically; } // also branch if enemy object => $15
      a = x;
      carry_flag = false;
      a = (uint8_t)(a + 0x8 + carry_flag); // if not selected objects or => $15, set
      x = a; // offset in X for next row
    }
    
FlipEnemyVertically:
    a = ram[Sprite_Tilenumber + x]; // load first or second row tiles
    ram[0x100 + sp--] = a; // and save tiles to the stack
    a = ram[(Sprite_Tilenumber + 4) + x];
    ram[0x100 + sp--] = a;
    ram[Sprite_Tilenumber + x] = ram[(Sprite_Tilenumber + 16) + y]; // exchange third row tiles; with first or second row tiles
    ram[(Sprite_Tilenumber + 4) + x] = ram[(Sprite_Tilenumber + 20) + y];
    a = ram[0x100 + ++sp]; // pull first or second row tiles from stack
    ram[(Sprite_Tilenumber + 20) + y] = a; // and save in third row
    a = ram[0x100 + ++sp];
    ram[(Sprite_Tilenumber + 16) + y] = a;
  }
  // CheckForESymmetry:
  a = ram[BowserGfxFlag]; zero_flag = a == 0; // are we drawing bowser at all?
  if (!zero_flag) { goto SkipToOffScrChk; } // branch if so
  a = ram[0xef];
  x = ram[0xec]; // get alternate enemy state
  // check for hammer bro object
  if (a == 0x5) {
    SprObjectOffscrChk(); return; // jump if found
  }
  // ContES:
  // check for bloober object
  if (a != Bloober) {
    zero_flag = a == PiranhaPlant; // check for piranha plant object
    if (zero_flag) { goto MirrorEnemyGfx; }
    zero_flag = a == Podoboo; // check for podoboo object
    if (zero_flag) { goto MirrorEnemyGfx; } // branch if either of three are found
    // check for spiny object
    // Original branch: branch closer if not found
    if (a == Spiny) {
      zero_flag = x == 0x5; // check spiny's state
      if (!zero_flag) { goto CheckToMirrorLakitu; } // branch if not an egg, otherwise
    }
    // ESRtnr:
    // check for princess/mushroom retainer object
    if (a == 0x15) {
      ram[(Sprite_Attributes + 20) + y] = 0x42; // set horizontal flip on bottom right sprite; note that palette bits were already set earlier
    }
    // SpnySC:
    carry_flag = x >= 0x2; // if alternate enemy state set to 1 or 0, branch
    if (!carry_flag) { goto CheckToMirrorLakitu; }
  }
  
MirrorEnemyGfx:
  a = ram[BowserGfxFlag]; zero_flag = a == 0; // if enemy object is bowser, skip all of this
  if (zero_flag) {
    a = ram[Sprite_Attributes + y]; // load attribute bits of first sprite
    a &= 0b10100011;
    ram[Sprite_Attributes + y] = a; // save vertical flip, priority, and palette bits
    ram[(Sprite_Attributes + 8) + y] = a; // in left sprite column of enemy object OAM data
    ram[(Sprite_Attributes + 16) + y] = a;
    a |= 0b01000000; // set horizontal flip
    // check for state used by spiny's egg
    // Original branch: if alternate state not set to $05, branch
    if (x == 0x5) {
      a |= 0b10000000; // otherwise set vertical flip
    }
    // EggExc:
    ram[(Sprite_Attributes + 4) + y] = a; // set bits of right sprite column
    ram[(Sprite_Attributes + 12) + y] = a; // of enemy object sprite data
    ram[(Sprite_Attributes + 20) + y] = a;
    zero_flag = x == 0x4; // check alternate enemy state
    if (!zero_flag) { goto CheckToMirrorLakitu; } // branch if not $04
    a = ram[(Sprite_Attributes + 8) + y]; // get second row left sprite attributes
    a |= 0b10000000;
    ram[(Sprite_Attributes + 8) + y] = a; // store bits with vertical flip in
    ram[(Sprite_Attributes + 16) + y] = a; // second and third row left sprites
    a |= 0b01000000;
    ram[(Sprite_Attributes + 12) + y] = a; // store with horizontal and vertical flip in
    ram[(Sprite_Attributes + 20) + y] = a; // second and third row right sprites
  }
  
CheckToMirrorLakitu:
  a = ram[0xef]; // check for lakitu enemy object
  // Original branch: branch if not found
  if (a == Lakitu) {
    a = ram[VerticalFlipFlag]; zero_flag = a == 0;
    // Original branch: branch if vertical flip flag not set
    if (zero_flag) {
      a = ram[(Sprite_Attributes + 16) + y]; // save vertical flip and palette bits
      a &= 0b10000001; // in third row left sprite
      ram[(Sprite_Attributes + 16) + y] = a;
      a = ram[(Sprite_Attributes + 20) + y]; // set horizontal flip and palette bits
      a |= 0b01000001; // in third row right sprite
      ram[(Sprite_Attributes + 20) + y] = a;
      x = ram[FrenzyEnemyTimer]; // check timer
      // branch if timer has not reached a certain range
      if (x >= 0x10) {
        SprObjectOffscrChk();
        return;
      }
      ram[(Sprite_Attributes + 12) + y] = a; // otherwise set same for second row right sprite
      a &= 0b10000001;
      ram[(Sprite_Attributes + 8) + y] = a; // preserve vertical flip and palette bits for left sprite
      SprObjectOffscrChk(); return; // unconditional branch
    }
    // NVFLak:
    a = ram[Sprite_Attributes + y]; // get first row left sprite attributes
    a &= 0b10000001;
    ram[Sprite_Attributes + y] = a; // save vertical flip and palette bits
    a = ram[(Sprite_Attributes + 4) + y]; // get first row right sprite attributes
    a |= 0b01000001; // set horizontal flip and palette bits
    ram[(Sprite_Attributes + 4) + y] = a; // note that vertical flip is left as-is
  }
  // CheckToMirrorJSpring:
  a = ram[0xef]; // check for jumpspring object (any frame)
  // branch if not jumpspring object at all
  if (a < 0x18) {
    SprObjectOffscrChk();
    return;
  }
  a = 0x82;
  ram[(Sprite_Attributes + 8) + y] = a; // set vertical flip and palette bits of 
  ram[(Sprite_Attributes + 16) + y] = a; // second and third row left sprites
  a |= 0b01000000;
  ram[(Sprite_Attributes + 12) + y] = a; // set, in addition to those, horizontal flip
  ram[(Sprite_Attributes + 20) + y] = a; // for second and third row right sprites
  SprObjectOffscrChk(); // fallthrough
  return;
}

// in: none; out: x
void SprObjectOffscrChk(void) {
  x = ram[ObjectOffset]; // get enemy buffer offset
  a = ram[Enemy_OffscreenBits]; // check offscreen information
  a = a >> 1;
  a = a >> 1; // shift three times to the right
  carry_flag = (a & 1) != 0; a = a >> 1; // which puts d2 into carry
  ram[0x100 + sp--] = a; // save to stack
  // Original branch: branch if not set
  if (carry_flag) {
    a = 0x4; // set for right column sprites
    CALL(MoveESprColOffscreen, 0xeb73); // and move them offscreen
  }
  // LcChk:
  a = ram[0x100 + ++sp]; // get from stack
  carry_flag = (a & 1) != 0; a = a >> 1; // move d3 to carry
  ram[0x100 + sp--] = a; // save to stack
  // Original branch: branch if not set
  if (carry_flag) {
    a = 0x0; // set for left column sprites,
    CALL(MoveESprColOffscreen, 0xeb7d); // move them offscreen
  }
  // Row3C:
  a = ram[0x100 + ++sp]; // get from stack again
  a = a >> 1; // move d5 to carry this time
  carry_flag = (a & 1) != 0; a = a >> 1;
  ram[0x100 + sp--] = a; // save to stack again
  // Original branch: branch if carry not set
  if (carry_flag) {
    a = 0x10; // set for third row of sprites
    CALL(MoveESprRowOffscreen, 0xeb88); // and move them offscreen
  }
  // Row23C:
  a = ram[0x100 + ++sp]; // get from stack
  carry_flag = (a & 1) != 0; a = a >> 1; // move d6 into carry
  ram[0x100 + sp--] = a; // save to stack
  if (carry_flag) {
    a = 0x8; // set for second and third rows
    CALL(MoveESprRowOffscreen, 0xeb92); // move them offscreen
  }
  // AllRowC:
  a = ram[0x100 + ++sp]; // get from stack once more
  carry_flag = (a & 1) != 0; a = a >> 1; // move d7 into carry
  if (!carry_flag) {
    return;
  }
  CALL(MoveESprRowOffscreen, 0xeb99); // move all sprites offscreen (A should be 0 by now)
  a = ram[(uint8_t)(Enemy_ID + x)];
  // check enemy identifier for podoboo
  // skip this part if found, we do not want to erase podoboo!
  if (a == Podoboo) {
    return;
  }
  a = ram[(uint8_t)(Enemy_Y_HighPos + x)]; // check high byte of vertical position
  // if not yet past the bottom of the screen, branch
  if (a != 0x2) {
    return;
  }
  CALL(EraseEnemyObject, 0xeba8); // what it says
  // ExEGHandler:
  return;
}

// in: x, y; out: x, y
void DrawEnemyObjRow(void) {
  ram[0x0] = ROM(EnemyGraphicsTable + x); // load two tiles of enemy graphics
  a = ROM((EnemyGraphicsTable + 1) + x);
  DrawOneSpriteRow(); // fallthrough
  return;
}

// in: a, x, y; out: x, y, carry
void DrawOneSpriteRow(void) {
  ram[0x1] = a;
  // draw them
  // -------------------------------------------------------------------------------------
  // $00-$01 - tile numbers
  // $02 - Y coordinate
  // $03 - flip control
  // $04 - sprite attributes
  // $05 - X coordinate
  // DrawSpriteObject:
  a = ram[0x3]; // get saved flip control bits
  a = a >> 1;
  carry_flag = (a & 1) != 0; a = a >> 1; // move d1 into carry
  a = ram[0x0];
  // Original branch: if d1 not set, branch
  if (carry_flag) {
    ram[(Sprite_Tilenumber + 4) + y] = a; // store first tile into second sprite
    ram[Sprite_Tilenumber + y] = ram[0x1]; // and second into first sprite
    a = 0x40; // activate horizontal flip OAM attribute
  } else {
    // NoHFlip:
    ram[Sprite_Tilenumber + y] = a; // store first tile into first sprite
    ram[(Sprite_Tilenumber + 4) + y] = ram[0x1]; // and second into second sprite
    a = 0x0; // clear bit for horizontal flip
  }
  // SetHFAt:
  a |= ram[0x4]; // add other OAM attributes if necessary
  ram[Sprite_Attributes + y] = a; // store sprite attributes
  ram[(Sprite_Attributes + 4) + y] = a;
  a = ram[0x2]; // now the y coordinates
  ram[Sprite_Y_Position + y] = a; // note because they are
  ram[(Sprite_Y_Position + 4) + y] = a; // side by side, they are the same
  a = ram[0x5];
  ram[Sprite_X_Position + y] = a; // store x coordinate, then
  carry_flag = false; // add 8 pixels and store another to
  a = (uint8_t)(a + 0x8 + carry_flag); // put them side by side
  ram[(Sprite_X_Position + 4) + y] = a;
  a = ram[0x2]; // add eight pixels to the next y
  carry_flag = false; // coordinate
  a = (uint8_t)(a + 0x8 + carry_flag);
  ram[0x2] = a;
  a = y; // add eight to the offset in Y to
  carry_flag = false; // move to the next two sprites
  { uint16_t sum = a + 0x8 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  y = a;
  x++; // increment offset to return it to the
  x++; // routine that called this subroutine
  return;
}

// in: a, x; out: y
void MoveESprRowOffscreen(void) {
  carry_flag = false; // add A to enemy object OAM data offset
  { uint16_t sum = a + ram[Enemy_SprDataOffset + x] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }
  y = a; // use as offset
  a = 0xf8;
  DumpTwoSpr(); return; // move first row of sprites offscreen
}

// in: a, x; out: none
void MoveESprColOffscreen(void) {
  carry_flag = false; // add A to enemy object OAM data offset
  a = (uint8_t)(a + ram[Enemy_SprDataOffset + x] + carry_flag);
  y = a; // use as offset
  CALL(MoveColOffscreen, 0xebc8); // move first and second row sprites in column offscreen
  ram[(Sprite_Data + 16) + y] = a; // move third row sprite in column offscreen
  return;
}

// -------------------------------------------------------------------------------------
// $00-$01 - tile numbers
// $02 - relative Y position
// $03 - horizontal flip flag (not used here)
// $04 - attributes
// $05 - relative X position
// in: x; out: x
void DrawBlock(void) {
  ram[0x2] = ram[Block_Rel_YPos]; // get relative vertical coordinate of block object; store here
  ram[0x5] = ram[Block_Rel_XPos]; // get relative horizontal coordinate of block object; store here
  a = 0x3;
  ram[0x4] = a; // set attribute byte here
  a = a >> 1;
  ram[0x3] = a; // set horizontal flip bit here (will not be used)
  y = ram[Block_SprDataOffset + x]; // get sprite data offset
  x = 0x0; // reset X for use as offset to tile data
  do {
    // DBlkLoop:
    ram[0x0] = ROM(DefaultBlockObjTiles + x); // get left tile number; set here
    a = ROM((DefaultBlockObjTiles + 1) + x); // get right tile number
    CALL(DrawOneSpriteRow, 0xebf1); // do sub to write tile numbers to first row of sprites
    zero_flag = x == 0x4; // check incremented offset
  } while (!zero_flag); // and loop back until all four sprites are done
  x = ram[ObjectOffset]; // get block object offset
  y = ram[Block_SprDataOffset + x]; // get sprite data offset
  a = ram[AreaType];
  // check for ground level type area
  // Original branch: if found, branch to next part
  if (a != 0x1) {
    a = 0x86;
    ram[Sprite_Tilenumber + y] = a; // otherwise remove brick tiles with lines
    ram[(Sprite_Tilenumber + 4) + y] = a; // and replace then with lineless brick tiles
  }
  // ChkRep:
  a = ram[Block_Metatile + x]; // check replacement metatile
  { uint8_t m = 0xc4; carry_flag = a >= m; zero_flag = a == m; } // if not used block metatile, then
  // Original branch: branch ahead to use current graphics
  if (zero_flag) {
    a = 0x87; // set A for used block tile
    y++; // increment Y to write to tile bytes
    CALL(DumpFourSpr, 0xec16); // do sub to dump into all four sprites
    y--; // return Y to original offset
    a = 0x3; // set palette bits
    x = ram[AreaType];
    x--; zero_flag = x == 0; // check for ground level type area again
    // Original branch: if found, use current palette bits
    if (!zero_flag) {
      a = a >> 1; // otherwise set to $01
    }
    // SetBFlip:
    x = ram[ObjectOffset]; // put block object offset back in X
    ram[Sprite_Attributes + y] = a; // store attribute byte as-is in first sprite
    a |= 0b01000000;
    ram[(Sprite_Attributes + 4) + y] = a; // set horizontal flip bit for second sprite
    a |= 0b10000000;
    ram[(Sprite_Attributes + 12) + y] = a; // set both flip bits for fourth sprite
    a &= 0b10000011;
    ram[(Sprite_Attributes + 8) + y] = a; // set vertical flip bit for third sprite
  }
  // BlkOffscr:
  a = ram[Block_OffscreenBits]; // get offscreen bits for block object
  ram[0x100 + sp--] = a; // save to stack
  a &= 0b00000100; zero_flag = a == 0; // check to see if d2 in offscreen bits are set
  // Original branch: if not set, branch, otherwise move sprites offscreen
  if (!zero_flag) {
    a = 0xf8; // move offscreen two OAMs
    ram[(Sprite_Y_Position + 4) + y] = a; // on the right side
    ram[(Sprite_Y_Position + 12) + y] = a;
  }
  // PullOfsB:
  a = ram[0x100 + ++sp]; // pull offscreen bits from stack
  ChkLeftCo(); // fallthrough
  return;
}

// in: a, y; out: none
void ChkLeftCo(void) {
  a &= 0b00001000; zero_flag = a == 0; // check to see if d3 in offscreen bits are set
  // if not set, branch, otherwise move sprites offscreen
  if (zero_flag) {
    return;
  }
  MoveColOffscreen(); // fallthrough
  return;
}

// in: y; out: a
void MoveColOffscreen(void) {
  a = 0xf8; // move offscreen two OAMs
  ram[Sprite_Y_Position + y] = a; // on the left side (or two rows of enemy on either side
  ram[(Sprite_Y_Position + 8) + y] = a; // if branched here from enemy graphics handler)
  // ExDBlk:
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used to hold palette bits for attribute byte or relative X position
// in: x; out: none
void DrawBrickChunks(void) {
  ram[0x0] = 0x2; // set palette bits here
  a = 0x75; // set tile number for ball (something residual, likely)
  y = ram[GameEngineSubroutine];
  { uint8_t m = 0x5; carry_flag = y >= m; zero_flag = y == m; } // if end-of-level routine running,
  // Original branch: use palette and tile number assigned
  if (!zero_flag) {
    ram[0x0] = 0x3; // otherwise set different palette bits
    a = 0x84; // and set tile number for brick chunks
  }
  // DChunks:
  y = ram[Block_SprDataOffset + x]; // get OAM data offset
  y++; // increment to start with tile bytes in OAM
  CALL(DumpFourSpr, 0xec6b); // do sub to dump tile number into all four sprites
  a = ram[FrameCounter]; // get frame counter
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1); // move low nybble to high
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1);
  a &= 0xc0; // get what was originally d3-d2 of low nybble
  a |= ram[0x0]; // add palette bits
  y++; // increment offset for attribute bytes
  CALL(DumpFourSpr, 0xec79); // do sub to dump attribute data into all four sprites
  y--;
  y--; // decrement offset to Y coordinate
  a = ram[Block_Rel_YPos]; // get first block object's relative vertical coordinate
  CALL(DumpTwoSpr, 0xec81); // do sub to dump current Y coordinate into two sprites
  ram[Sprite_X_Position + y] = ram[Block_Rel_XPos]; // get first block object's relative horizontal coordinate; save into X coordinate of first sprite
  a = ram[Block_Orig_XPos + x]; // get original horizontal coordinate
  carry_flag = true;
  a = (uint8_t)(a - ram[ScreenLeft_X_Pos] - !carry_flag); // subtract coordinate of left side from original coordinate
  ram[0x0] = a; // store result as relative horizontal coordinate of original
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[Block_Rel_XPos] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // get difference of relative positions of original - current
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add original relative position to result
  a = (uint8_t)(a + 0x6 + carry_flag); // plus 6 pixels to position second brick chunk correctly
  ram[(Sprite_X_Position + 4) + y] = a; // save into X coordinate of second sprite
  a = ram[(Block_Rel_YPos + 1)]; // get second block object's relative vertical coordinate
  ram[(Sprite_Y_Position + 8) + y] = a;
  ram[(Sprite_Y_Position + 12) + y] = a; // dump into Y coordinates of third and fourth sprites
  ram[(Sprite_X_Position + 8) + y] = ram[(Block_Rel_XPos + 1)]; // get second block object's relative horizontal coordinate; save into X coordinate of third sprite
  a = ram[0x0]; // use original relative horizontal position
  carry_flag = true;
  { uint16_t diff = (uint16_t)(a - ram[(Block_Rel_XPos + 1)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // get difference of relative positions of original - current
  { uint16_t sum = a + ram[0x0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add original relative position to result
  a = (uint8_t)(a + 0x6 + carry_flag); // plus 6 pixels to position fourth brick chunk correctly
  ram[(Sprite_X_Position + 12) + y] = a; // save into X coordinate of fourth sprite
  a = ram[Block_OffscreenBits]; // get offscreen bits for block object
  CALL(ChkLeftCo, 0xecbd); // do sub to move left half of sprites offscreen if necessary
  a = ram[Block_OffscreenBits]; // get offscreen bits again
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // shift d7 into carry
  // Original branch: if d7 not set, branch to last part
  if (carry_flag) {
    a = 0xf8;
    CALL(DumpTwoSpr, 0xecc8); // otherwise move top sprites offscreen
  }
  // ChnkOfs:
  a = ram[0x0]; neg_flag = (a & 0x80) != 0; // if relative position on left side of screen,
  // go ahead and leave
  if (!neg_flag) {
    return;
  }
  a = ram[Sprite_X_Position + y]; // otherwise compare left-side X coordinate
  carry_flag = a >= ram[(Sprite_X_Position + 4) + y]; // to right-side X coordinate
  // branch to leave if less
  if (!carry_flag) {
    return;
  }
  a = 0xf8; // otherwise move right half of sprites offscreen
  ram[(Sprite_Y_Position + 4) + y] = a;
  ram[(Sprite_Y_Position + 12) + y] = a;
  // ExBCDr:
  return; // leave
}

// in: y; out: none
void DrawFirebar(void) {
  a = ram[FrameCounter]; // get frame counter
  a = a >> 1; // divide by four
  a = a >> 1;
  ram[0x100 + sp--] = a; // save result to stack
  a &= 0x1; // mask out all but last bit
  a ^= 0x64; // set either tile $64 or $65 as fireball tile
  ram[Sprite_Tilenumber + y] = a; // thus tile changes every four frames
  a = ram[0x100 + ++sp]; // get from stack
  a = a >> 1; // divide by four again
  carry_flag = (a & 1) != 0; a = a >> 1;
  a = 0x2; // load value $02 to set palette in attrib byte
  // Original branch: if last bit shifted out was not set, skip this
  if (carry_flag) {
    a |= 0b11000000; // otherwise flip both ways every eight frames
  }
  // FireA:
  ram[Sprite_Attributes + y] = a; // store attribute byte and leave
  return;
}

// in: a, y; out: x
void DrawExplosion_Fireworks(void) {
  x = a; // use whatever's in A for offset
  a = ROM(ExplosionTiles + x); // get tile number using offset
  y++; // increment Y (contains sprite data offset)
  CALL(DumpFourSpr, 0xed1e); // and dump into tile number part of sprite data
  y--; // decrement Y so we have the proper offset again
  x = ram[ObjectOffset]; // return enemy object buffer offset to X
  a = ram[Fireball_Rel_YPos]; // get relative vertical coordinate
  carry_flag = true; // subtract four pixels vertically
  a = (uint8_t)(a - 0x4 - !carry_flag); // for first and third sprites
  ram[Sprite_Y_Position + y] = a;
  ram[(Sprite_Y_Position + 8) + y] = a;
  carry_flag = false; // add eight pixels vertically
  a = (uint8_t)(a + 0x8 + carry_flag); // for second and fourth sprites
  ram[(Sprite_Y_Position + 4) + y] = a;
  ram[(Sprite_Y_Position + 12) + y] = a;
  a = ram[Fireball_Rel_XPos]; // get relative horizontal coordinate
  carry_flag = true; // subtract four pixels horizontally
  a = (uint8_t)(a - 0x4 - !carry_flag); // for first and second sprites
  ram[Sprite_X_Position + y] = a;
  ram[(Sprite_X_Position + 4) + y] = a;
  carry_flag = false; // add eight pixels horizontally
  a = (uint8_t)(a + 0x8 + carry_flag); // for third and fourth sprites
  ram[(Sprite_X_Position + 8) + y] = a;
  ram[(Sprite_X_Position + 12) + y] = a;
  ram[Sprite_Attributes + y] = 0x2; // set palette attributes for all sprites, but; set no flip at all for first sprite
  ram[(Sprite_Attributes + 4) + y] = 0x82; // set vertical flip for second sprite
  ram[(Sprite_Attributes + 8) + y] = 0x42; // set horizontal flip for third sprite
  ram[(Sprite_Attributes + 12) + y] = 0xc2; // set both flips for fourth sprite
  return; // we are done
}

// -------------------------------------------------------------------------------------
// in: x; out: x
void DrawSmallPlatform(void) {
  y = ram[Enemy_SprDataOffset + x]; // get OAM data offset
  a = 0x5b; // load tile number for small platforms
  y++; // increment offset for tile numbers
  CALL(DumpSixSpr, 0xed6e); // dump tile number into all six sprites
  y++; // increment offset for attributes
  a = 0x2; // load palette controls
  CALL(DumpSixSpr, 0xed74); // dump attributes into all six sprites
  y--; // decrement for original offset
  y--;
  a = ram[Enemy_Rel_XPos]; // get relative horizontal coordinate
  ram[Sprite_X_Position + y] = a;
  ram[(Sprite_X_Position + 12) + y] = a; // dump as X coordinate into first and fourth sprites
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight pixels
  ram[(Sprite_X_Position + 4) + y] = a; // dump into second and fifth sprites
  ram[(Sprite_X_Position + 16) + y] = a;
  carry_flag = false;
  a = (uint8_t)(a + 0x8 + carry_flag); // add eight more pixels
  ram[(Sprite_X_Position + 8) + y] = a; // dump into third and sixth sprites
  ram[(Sprite_X_Position + 20) + y] = a;
  a = ram[(uint8_t)(Enemy_Y_Position + x)]; // get vertical coordinate
  x = a;
  ram[0x100 + sp--] = a; // save to stack
  carry_flag = x >= 0x20; // if vertical coordinate below status bar,
  // Original branch: do not mess with it
  if (!carry_flag) {
    a = 0xf8; // otherwise move first three sprites offscreen
  }
  // TopSP:
  CALL(DumpThreeSpr, 0xed9e); // dump vertical coordinate into Y coordinates
  a = ram[0x100 + ++sp]; // pull from stack
  carry_flag = false;
  a = (uint8_t)(a + 0x80 + carry_flag); // add 128 pixels
  x = a;
  // if below status bar (taking wrap into account)
  // Original branch: then do not change altered coordinate
  if (x < 0x20) {
    a = 0xf8; // otherwise move last three sprites offscreen
  }
  // BotSP:
  ram[(Sprite_Y_Position + 12) + y] = a; // dump vertical coordinate + 128 pixels
  ram[(Sprite_Y_Position + 16) + y] = a; // into Y coordinates
  ram[(Sprite_Y_Position + 20) + y] = a;
  a = ram[Enemy_OffscreenBits]; // get offscreen bits
  ram[0x100 + sp--] = a; // save to stack
  a &= 0b00001000; zero_flag = a == 0; // check d3
  if (!zero_flag) {
    a = 0xf8; // if d3 was set, move first and
    ram[Sprite_Y_Position + y] = a; // fourth sprites offscreen
    ram[(Sprite_Y_Position + 12) + y] = a;
  }
  // SOfs:
  a = ram[0x100 + ++sp]; // move out and back into stack
  ram[0x100 + sp--] = a;
  a &= 0b00000100; zero_flag = a == 0; // check d2
  if (!zero_flag) {
    a = 0xf8; // if d2 was set, move second and
    ram[(Sprite_Y_Position + 4) + y] = a; // fifth sprites offscreen
    ram[(Sprite_Y_Position + 16) + y] = a;
  }
  // SOfs2:
  a = ram[0x100 + ++sp]; // get from stack
  a &= 0b00000010; zero_flag = a == 0; // check d1
  if (!zero_flag) {
    a = 0xf8; // if d1 was set, move third and
    ram[(Sprite_Y_Position + 8) + y] = a; // sixth sprites offscreen
    ram[(Sprite_Y_Position + 20) + y] = a;
  }
  // ExSPl:
  x = ram[ObjectOffset]; // get enemy object offset and leave
  return;
}

// -------------------------------------------------------------------------------------
// in: x; out: none
void DrawBubble(void) {
  y = ram[Player_Y_HighPos]; // if player's vertical high position
  y--; zero_flag = y == 0; // not within screen, skip all of this
  if (!zero_flag) {
    return;
  }
  a = ram[Bubble_OffscreenBits]; // check air bubble's offscreen bits
  a &= 0b00001000; zero_flag = a == 0;
  // if bit set, branch to leave
  if (!zero_flag) {
    return;
  }
  y = ram[Bubble_SprDataOffset + x]; // get air bubble's OAM data offset
  ram[Sprite_X_Position + y] = ram[Bubble_Rel_XPos]; // get relative horizontal coordinate; store as X coordinate here
  ram[Sprite_Y_Position + y] = ram[Bubble_Rel_YPos]; // get relative vertical coordinate; store as Y coordinate here
  ram[Sprite_Tilenumber + y] = 0x74; // put air bubble tile into OAM data
  ram[Sprite_Attributes + y] = 0x2; // set attribute byte
  // ExDBub:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00 - used to store player's vertical offscreen bits
void PlayerGfxHandler(void) {
  a = ram[InjuryTimer]; zero_flag = a == 0; // if player's injured invincibility timer
  // Original branch: not set, skip checkpoint and continue code
  if (!zero_flag) {
    a = ram[FrameCounter];
    carry_flag = (a & 1) != 0; a = a >> 1; // otherwise check frame counter and branch
    // to leave on every other frame (when d0 is set)
    if (carry_flag) {
      return;
    }
  }
  // CntPl:
  a = ram[GameEngineSubroutine]; // if executing specific game engine routine,
  // branch ahead to some other part
  if (a != 0xb) {
    a = ram[PlayerChangeSizeFlag]; zero_flag = a == 0; // if grow/shrink flag set
    // Original branch: then branch to some other code
    if (zero_flag) {
      y = ram[SwimmingFlag]; zero_flag = y == 0; // if swimming flag set, branch to
      // different part, do not return
      if (zero_flag) {
        FindPlayerAction();
        return;
      }
      a = ram[Player_State];
      // if player status normal,
      // branch and do not return
      if (a == 0x0) {
        FindPlayerAction();
        return;
      }
      CALL(FindPlayerAction, 0xef0b); // otherwise jump and return
      a = ram[FrameCounter];
      a &= 0b00000100; zero_flag = a == 0; // check frame counter for d2 set (8 frames every
      // eighth frame), and branch if set to leave
      if (!zero_flag) {
        return;
      }
      x = a; // initialize X to zero
      y = ram[Player_SprDataOffset]; // get player sprite data offset
      a = ram[PlayerFacingDir]; // get player's facing direction
      carry_flag = (a & 1) != 0; a = a >> 1;
      // Original branch: if player facing to the right, use current offset
      if (!carry_flag) {
        y++;
        y++; // otherwise move to next OAM data
        y++;
        y++;
      }
      // SwimKT:
      a = ram[PlayerSize]; zero_flag = a == 0; // check player's size
      // Original branch: if big, use first tile
      if (!zero_flag) {
        a = ram[(Sprite_Tilenumber + 24) + y]; // check tile number of seventh/eighth sprite
        zero_flag = a == ROM(SwimTileRepOffset); // against tile number in player graphics table
        // if spr7/spr8 tile number = value, branch to leave
        if (zero_flag) {
          return;
        }
        x++; // otherwise increment X for second tile
      }
      // BigKTS:
      ram[(Sprite_Tilenumber + 24) + y] = ROM(SwimKickTileNum + x); // overwrite tile number in sprite 7/8; to animate player's feet when swimming
      // ExPGH:
      return; // then leave
    }
    // DoChangeSize:
    CALL(HandleChangeSize, 0xef3c); // find proper offset to graphics table for grow/shrink
    PlayerGfxProcessing(); return; // draw player, then process for fireball throwing
  }
  // PlayerKilled:
  y = 0xe; // load offset for player killed
  a = ROM(PlayerGfxTblOffsets + y); // get offset to graphics table
  PlayerGfxProcessing(); // fallthrough
  return;
}

void FindPlayerAction(void) {
  CALL(ProcessPlayerAction, 0xef36); // find proper offset to graphics table by player's actions
  PlayerGfxProcessing(); return; // draw player, then process for fireball throwing
}

// in: a; out: none
void PlayerGfxProcessing(void) {
  ram[PlayerGfxOffset] = a; // store offset to graphics table here
  a = 0x4;
  CALL(RenderPlayerSub, 0xef4c); // draw player based on offset loaded
  CALL(ChkForPlayerAttrib, 0xef4f); // set horizontal flip bits as necessary
  a = ram[FireballThrowingTimer]; zero_flag = a == 0;
  // Original branch: if fireball throw timer not set, skip to the end
  if (!zero_flag) {
    y = 0x0; // set value to initialize by default
    a = ram[PlayerAnimTimer]; // get animation frame timer
    carry_flag = a >= ram[FireballThrowingTimer]; // compare to fireball throw timer
    ram[FireballThrowingTimer] = y; // initialize fireball throw timer
    if (carry_flag) { goto PlayerOffscreenChk; } // if animation frame timer => fireball throw timer skip to end
    ram[FireballThrowingTimer] = a; // otherwise store animation timer into fireball throw timer
    y = 0x7; // load offset for throwing
    ram[PlayerGfxOffset] = ROM(PlayerGfxTblOffsets + y); // get offset to graphics table; store it for use later
    y = 0x4; // set to update four sprite rows by default
    a = ram[Player_X_Speed];
    a |= ram[Left_Right_Buttons]; zero_flag = a == 0; // check for horizontal speed or left/right button press
    // Original branch: if no speed or button press, branch using set value in Y
    if (!zero_flag) {
      y--; // otherwise set to update only three sprite rows
    }
    // SUpdR:
    a = y; // save in A for use
    CALL(RenderPlayerSub, 0xef79); // in sub, draw player object again
  }
  
PlayerOffscreenChk:
  a = ram[Player_OffscreenBits]; // get player's offscreen bits
  a = a >> 1;
  a = a >> 1; // move vertical bits to low nybble
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a; // store here
  x = 0x3; // check all four rows of player sprites
  a = ram[Player_SprDataOffset]; // get player's sprite data offset
  carry_flag = false;
  a = (uint8_t)(a + 0x18 + carry_flag); // add 24 bytes to start at bottom row
  y = a; // set as offset here
  do {
    // PROfsLoop:
    a = 0xf8; // load offscreen Y coordinate just in case
    carry_flag = (ram[0x0] & 1) != 0; ram[0x0] = ram[0x0] >> 1; // shift bit into carry
    // Original branch: if bit not set, skip, do not move sprites
    if (carry_flag) {
      CALL(DumpTwoSpr, 0xef94); // otherwise dump offscreen Y coordinate into sprite data
    }
    // NPROffscr:
    a = y;
    carry_flag = true; // subtract eight bytes to do
    a = (uint8_t)(a - 0x8 - !carry_flag); // next row up
    y = a;
    x--; neg_flag = (x & 0x80) != 0; // decrement row counter
  } while (!neg_flag); // do this until all sprite rows are checked
  return; // then we are done!
}

// -------------------------------------------------------------------------------------
void DrawPlayer_Intermediate(void) {
  x = 0x5; // store data into zero page memory
  do {
    // PIntLoop:
    ram[(uint8_t)(0x2 + x)] = ROM(IntermediatePlayerData + x); // load data to display player as he always; appears on world/lives display
    x--; neg_flag = (x & 0x80) != 0;
  } while (!neg_flag); // do this until all data is loaded
  x = 0xb8; // load offset for small standing
  y = 0x4; // load sprite data offset
  CALL(DrawPlayerLoop, 0xefb4); // draw player accordingly
  a = ram[(Sprite_Attributes + 36)]; // get empty sprite attributes
  a |= 0b01000000; // set horizontal flip bit for bottom-right sprite
  ram[(Sprite_Attributes + 32)] = a; // store and leave
  return;
}

// -------------------------------------------------------------------------------------
// $00-$01 - used to hold tile numbers, $00 also used to hold upper extent of animation frames
// $02 - vertical position
// $03 - facing direction, used as horizontal flip control
// $04 - attributes
// $05 - horizontal position
// $07 - number of rows to draw
// these also used in IntermediatePlayerData
// in: a; out: none
void RenderPlayerSub(void) {
  ram[0x7] = a; // store number of rows of sprites to draw
  a = ram[Player_Rel_XPos];
  ram[Player_Pos_ForScroll] = a; // store player's relative horizontal position
  ram[0x5] = a; // store it here also
  ram[0x2] = ram[Player_Rel_YPos]; // store player's vertical position
  ram[0x3] = ram[PlayerFacingDir]; // store player's facing direction
  ram[0x4] = ram[Player_SprAttrib]; // store player's sprite attributes
  x = ram[PlayerGfxOffset]; // load graphics table offset
  y = ram[Player_SprDataOffset]; // get player's sprite data offset
  DrawPlayerLoop(); // fallthrough
  return;
}

// in: x, y; out: none
void DrawPlayerLoop(void) {
  do {
    ram[0x0] = ROM(PlayerGraphicsTable + x); // load player's left side
    a = ROM((PlayerGraphicsTable + 1) + x); // now load right side
    CALL(DrawOneSpriteRow, 0xefe6);
    ram[0x7]--; zero_flag = ram[0x7] == 0; // decrement rows of sprites to draw
  } while (!zero_flag); // do this until all rows are drawn
  return;
}

// in: none; out: a
void ProcessPlayerAction(void) {
  a = ram[Player_State]; // get player's state
  zero_flag = a == 0x3;
  if (zero_flag) { goto ActionClimbing; } // if climbing, branch here
  zero_flag = a == 0x2;
  if (zero_flag) { goto ActionFalling; } // if falling, branch here
  // Original branch: if not jumping, branch here
  if (a == 0x1) {
    a = ram[SwimmingFlag]; zero_flag = a == 0;
    if (!zero_flag) { goto ActionSwimming; } // if swimming flag set, branch elsewhere
    y = 0x6; // load offset for crouching
    a = ram[CrouchingFlag]; zero_flag = a == 0; // get crouching flag
    if (!zero_flag) { goto NonAnimatedActs; } // if set, branch to get offset for graphics table
    y = 0x0; // otherwise load offset for jumping
  } else {
    // ProcOnGroundActs:
    y = 0x6; // load offset for crouching
    a = ram[CrouchingFlag]; zero_flag = a == 0; // get crouching flag
    if (!zero_flag) { goto NonAnimatedActs; } // if set, branch to get offset for graphics table
    y = 0x2; // load offset for standing
    a = ram[Player_X_Speed]; // check player's horizontal speed
    a |= ram[Left_Right_Buttons]; zero_flag = a == 0; // and left/right controller bits
    if (zero_flag) { goto NonAnimatedActs; } // if no speed or buttons pressed, use standing offset
    a = ram[Player_XSpeedAbsolute]; // load walking/running speed
    carry_flag = a >= 0x9;
    if (!carry_flag) { goto ActionWalkRun; } // if less than a certain amount, branch, too slow to skid
    a = ram[Player_MovingDir]; // otherwise check to see if moving direction
    a &= ram[PlayerFacingDir]; zero_flag = a == 0; // and facing direction are the same
    if (!zero_flag) { goto ActionWalkRun; } // if moving direction = facing direction, branch, don't skid
    y++; // otherwise increment to skid offset ($03)
  }
  
NonAnimatedActs:
  CALL(GetGfxOffsetAdder, 0xf02a); // do a sub here to get offset adder for graphics table
  ram[PlayerAnimCtrl] = 0x0; // initialize animation frame control
  a = ROM(PlayerGfxTblOffsets + y); // load offset to graphics table using size as offset
  return;
  
ActionFalling:
  y = 0x4; // load offset for walking/running
  CALL(GetGfxOffsetAdder, 0xf038); // get offset to graphics table
  GetCurrentAnimOffset(); return; // execute instructions for falling state
  
ActionWalkRun:
  y = 0x4; // load offset for walking/running
  CALL(GetGfxOffsetAdder, 0xf040); // get offset to graphics table
  goto FourFrameExtent; // execute instructions for normal state
  
ActionClimbing:
  y = 0x5; // load offset for climbing
  a = ram[Player_Y_Speed]; zero_flag = a == 0; // check player's vertical speed
  if (zero_flag) { goto NonAnimatedActs; } // if no speed, branch, use offset as-is
  CALL(GetGfxOffsetAdder, 0xf04c); // otherwise get offset for graphics table
  goto ThreeFrameExtent; // then skip ahead to more code
  
ActionSwimming:
  y = 0x1; // load offset for swimming
  CALL(GetGfxOffsetAdder, 0xf054);
  a = ram[JumpSwimTimer]; // check jump/swim timer
  a |= ram[PlayerAnimCtrl]; zero_flag = a == 0; // and animation frame control
  // Original branch: if any one of these set, branch ahead
  if (zero_flag) {
    a = ram[A_B_Buttons];
    carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // check for A button pressed
    if (carry_flag) { goto FourFrameExtent; } // branch to same place if A button pressed
    GetCurrentAnimOffset(); // fallthrough
    return;
  }
  
FourFrameExtent:
  a = 0x3; // load upper extent for frame control
  goto AnimationControl; // jump to get offset and animate player object
  
ThreeFrameExtent:
  a = 0x2; // load upper extent for frame control for climbing
  
AnimationControl:
  ram[0x0] = a; // store upper extent here
  CALL(GetCurrentAnimOffset, 0xf073); // get proper offset to graphics table
  ram[0x100 + sp--] = a; // save offset to stack
  a = ram[PlayerAnimTimer]; zero_flag = a == 0; // load animation frame timer
  // Original branch: branch if not expired
  if (zero_flag) {
    ram[PlayerAnimTimer] = ram[PlayerAnimTimerSet]; // get animation frame timer amount; and set timer accordingly
    a = ram[PlayerAnimCtrl];
    carry_flag = false; // add one to animation frame control
    a = (uint8_t)(a + 0x1 + carry_flag);
    carry_flag = a >= ram[0x0]; // compare to upper extent
    // Original branch: if frame control + 1 < upper extent, use as next
    if (carry_flag) {
      a = 0x0; // otherwise initialize frame control
    }
    // SetAnimC:
    ram[PlayerAnimCtrl] = a; // store as new animation frame control
  }
  // ExAnimC:
  a = ram[0x100 + ++sp]; // get offset to graphics table from stack and leave
  return;
}

// in: y; out: a
void GetCurrentAnimOffset(void) {
  a = ram[PlayerAnimCtrl]; // get animation frame control
  GetOffsetFromAnimCtrl(); return; // jump to get proper offset to graphics table
}

// in: y; out: y
void GetGfxOffsetAdder(void) {
  a = ram[PlayerSize]; zero_flag = a == 0; // get player's size
  // if player big, use current offset as-is
  if (zero_flag) {
    return;
  }
  a = y; // for big player
  carry_flag = false; // otherwise add eight bytes to offset
  a = (uint8_t)(a + 0x8 + carry_flag); // for small player
  y = a;
  // SzOfs:
  return; // go back
}

// in: none; out: a
void HandleChangeSize(void) {
  y = ram[PlayerAnimCtrl]; // get animation frame control
  a = ram[FrameCounter];
  a &= 0b00000011; zero_flag = a == 0; // get frame counter and execute this code every
  // Original branch: fourth frame, otherwise branch ahead
  if (zero_flag) {
    y++; // increment frame control
    // check for preset upper extent
    // Original branch: if not there yet, skip ahead to use
    if (y >= 0xa) {
      y = 0x0; // otherwise initialize both grow/shrink flag
      ram[PlayerChangeSizeFlag] = y; // and animation frame control
    }
    // CSzNext:
    ram[PlayerAnimCtrl] = y; // store proper frame control
  }
  // GorSLog:
  a = ram[PlayerSize]; zero_flag = a == 0; // get player's size
  // Original branch: if player small, skip ahead to next part
  if (zero_flag) {
    a = ROM(ChangeSizeOffsetAdder + y); // get offset adder based on frame control as offset
    y = 0xf; // load offset for player growing
    GetOffsetFromAnimCtrl(); // fallthrough
    return;
  }
  // ShrinkPlayer:
  a = y; // add ten bytes to frame control as offset
  carry_flag = false;
  a = (uint8_t)(a + 0xa + carry_flag); // this thing apparently uses two of the swimming frames
  x = a; // to draw the player shrinking
  y = 0x9; // load offset for small player swimming
  a = ROM(ChangeSizeOffsetAdder + x); zero_flag = a == 0; // get what would normally be offset adder
  // Original branch: and branch to use offset if nonzero
  if (zero_flag) {
    y = 0x1; // otherwise load offset for big player swimming
  }
  // ShrPlF:
  a = ROM(PlayerGfxTblOffsets + y); // get offset to graphics table based on offset loaded
  return; // and leave
}

// in: a, y; out: a
void GetOffsetFromAnimCtrl(void) {
  a = (uint8_t)(a << 1); // multiply animation frame control
  a = (uint8_t)(a << 1); // by eight to get proper amount
  carry_flag = (a & 0x80) != 0; a = (uint8_t)(a << 1); // to add to our offset
  a = (uint8_t)(a + ROM(PlayerGfxTblOffsets + y) + carry_flag); // add to offset to graphics table
  return; // and return with result in A
}

void ChkForPlayerAttrib(void) {
  y = ram[Player_SprDataOffset]; // get sprite data offset
  a = ram[GameEngineSubroutine];
  // if executing specific game engine routine,
  // Original branch: branch to change third and fourth row OAM attributes
  if (a != 0xb) {
    a = ram[PlayerGfxOffset]; // get graphics table offset
    zero_flag = a == 0x50;
    if (zero_flag) { goto C_S_IGAtt; } // if crouch offset, either standing offset,
    zero_flag = a == 0xb8; // or intermediate growing offset,
    if (zero_flag) { goto C_S_IGAtt; } // go ahead and execute code to change 
    zero_flag = a == 0xc0; // fourth row OAM attributes only
    if (zero_flag) { goto C_S_IGAtt; }
    // if none of these, branch to leave
    if (a != 0xc8) {
      return;
    }
  }
  // KilledAtt:
  a = ram[(Sprite_Attributes + 16) + y];
  a &= 0b00111111; // mask out horizontal and vertical flip bits
  ram[(Sprite_Attributes + 16) + y] = a; // for third row sprites and save
  a = ram[(Sprite_Attributes + 20) + y];
  a &= 0b00111111;
  a |= 0b01000000; // set horizontal flip bit for second
  ram[(Sprite_Attributes + 20) + y] = a; // sprite in the third row
  
C_S_IGAtt:
  a = ram[(Sprite_Attributes + 24) + y];
  a &= 0b00111111; // mask out horizontal and vertical flip bits
  ram[(Sprite_Attributes + 24) + y] = a; // for fourth row sprites and save
  a = ram[(Sprite_Attributes + 28) + y];
  a &= 0b00111111;
  a |= 0b01000000; // set horizontal flip bit for second
  ram[(Sprite_Attributes + 28) + y] = a; // sprite in the fourth row
  // ExPlyrAt:
  return; // leave
}

// -------------------------------------------------------------------------------------
// $00 - used in adding to get proper offset
// in: none; out: y
void RelativePlayerPosition(void) {
  x = 0x0; // set offsets for relative cooordinates
  y = 0x0; // routine to correspond to player object
  RelWOfs(); return; // get the coordinates
}

// in: x; out: x
void RelativeBubblePosition(void) {
  y = 0x1; // set for air bubble offsets
  CALL(GetProperObjOffset, 0xf135); // modify X to get proper air bubble offset
  y = 0x3;
  RelWOfs(); return; // get the coordinates
}

// in: x; out: x
void RelativeFireballPosition(void) {
  y = 0x0; // set for fireball offsets
  CALL(GetProperObjOffset, 0xf13f); // modify X to get proper fireball offset
  y = 0x2;
  RelWOfs(); // fallthrough
  return;
}

// in: x, y; out: x
void RelWOfs(void) {
  CALL(GetObjRelativePosition, 0xf144); // get the coordinates
  x = ram[ObjectOffset]; // return original offset
  return; // leave
}

// in: x; out: x
void RelativeMiscPosition(void) {
  y = 0x2; // set for misc object offsets
  CALL(GetProperObjOffset, 0xf14c); // modify X to get proper misc object offset
  y = 0x6;
  RelWOfs(); return; // get the coordinates
}

// in: x; out: a, x, y, carry
void RelativeEnemyPosition(void) {
  a = 0x1; // get coordinates of enemy object 
  y = 0x1; // relative to the screen
  VariableObjOfsRelPos(); return;
}

// in: x; out: x
void RelativeBlockPosition(void) {
  a = 0x9; // get coordinates of one block object
  y = 0x4; // relative to the screen
  CALL(VariableObjOfsRelPos, 0xf15f);
  x++; // adjust offset for other block object if any
  x++;
  a = 0x9;
  y++; // adjust other and get coordinates for other one
  VariableObjOfsRelPos(); // fallthrough
  return;
}

// in: a, x, y; out: a, x, carry
void VariableObjOfsRelPos(void) {
  ram[0x0] = x; // store value to add to A here
  carry_flag = false;
  a = (uint8_t)(a + ram[0x0] + carry_flag); // add A to value stored
  x = a; // use as enemy offset
  CALL(GetObjRelativePosition, 0xf16d);
  x = ram[ObjectOffset]; // reload old object offset and leave
  return;
}

// in: x, y; out: a, carry
void GetObjRelativePosition(void) {
  ram[SprObject_Rel_YPos + y] = ram[(uint8_t)(SprObject_Y_Position + x)]; // load vertical coordinate low; store here
  a = ram[(uint8_t)(SprObject_X_Position + x)]; // load horizontal coordinate
  carry_flag = true; // subtract left edge coordinate
  { uint16_t diff = (uint16_t)(a - ram[ScreenLeft_X_Pos] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; }
  ram[SprObject_Rel_XPos + y] = a; // store result here
  return;
}

// -------------------------------------------------------------------------------------
// $00 - used as temp variable to hold offscreen bits
void GetPlayerOffscreenBits(void) {
  x = 0x0; // set offsets for player-specific variables
  y = 0x0; // and get offscreen information about player
  GetOffScreenBitsSet(); return;
}

// in: x; out: x
void GetFireballOffscreenBits(void) {
  y = 0x0; // set for fireball offsets
  CALL(GetProperObjOffset, 0xf18b); // modify X to get proper fireball offset
  y = 0x2; // set other offset for fireball's offscreen bits
  GetOffScreenBitsSet(); return; // and get offscreen information about fireball
}

// in: x; out: x
void GetBubbleOffscreenBits(void) {
  y = 0x1; // set for air bubble offsets
  CALL(GetProperObjOffset, 0xf195); // modify X to get proper air bubble offset
  y = 0x3; // set other offset for airbubble's offscreen bits
  GetOffScreenBitsSet(); return; // and get offscreen information about air bubble
}

// in: x; out: x
void GetMiscOffscreenBits(void) {
  y = 0x2; // set for misc object offsets
  CALL(GetProperObjOffset, 0xf19f); // modify X to get proper misc object offset
  y = 0x6; // set other offset for misc object's offscreen bits
  GetOffScreenBitsSet(); return; // and get offscreen information about misc object
}

// in: x, y; out: x
void GetProperObjOffset(void) {
  a = x; // move offset to A
  carry_flag = false;
  a = (uint8_t)(a + ROM(ObjOffsetData + y) + carry_flag); // add amount of bytes to offset depending on setting in Y
  x = a; // put back in X and leave
  return;
}

// in: x; out: x, y
void GetEnemyOffscreenBits(void) {
  a = 0x1; // set A to add 1 byte in order to get enemy offset
  y = 0x1; // set Y to put offscreen bits in Enemy_OffscreenBits
  SetOffscrBitsOffset(); return;
}

// in: x; out: x
void GetBlockOffscreenBits(void) {
  a = 0x9; // set A to add 9 bytes in order to get block obj offset
  y = 0x4; // set Y to put offscreen bits in Block_OffscreenBits
  SetOffscrBitsOffset(); // fallthrough
  return;
}

// in: a, x, y; out: x, y
void SetOffscrBitsOffset(void) {
  ram[0x0] = x;
  carry_flag = false; // add contents of X to A to get
  a = (uint8_t)(a + ram[0x0] + carry_flag); // appropriate offset, then give back to X
  x = a;
  GetOffScreenBitsSet(); // fallthrough
  return;
}

// in: x, y; out: x, y
void GetOffScreenBitsSet(void) {
  a = y; // save offscreen bits offset to stack for now
  ram[0x100 + sp--] = a;
  CALL(RunOffscrBitsSubs, 0xf1c4);
  a = (uint8_t)(a << 1); // move low nybble to high nybble
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a = (uint8_t)(a << 1);
  a |= ram[0x0]; // mask together with previously saved low nybble
  ram[0x0] = a; // store both here
  a = ram[0x100 + ++sp]; // get offscreen bits offset from stack
  y = a;
  ram[SprObject_OffscrBits + y] = ram[0x0]; // get value here and store elsewhere
  x = ram[ObjectOffset];
  return;
}

// in: x; out: a
void RunOffscrBitsSubs(void) {
  CALL(GetXOffscreenBits, 0xf1d9); // do subroutine here
  a = a >> 1; // move high nybble to low
  a = a >> 1;
  a = a >> 1;
  a = a >> 1;
  ram[0x0] = a; // store here
  // --------------------------------
  // GetYOffscreenBits:
  ram[0x4] = x; // save position in buffer to here
  y = 0x1; // start with top of screen
  do {
    // YOfsLoop:
    a = ROM(HighPosUnitData + y); // load coordinate for edge of vertical unit
    carry_flag = true;
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(SprObject_Y_Position + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // subtract from vertical coordinate of object
    ram[0x7] = a; // store here
    a = 0x1; // subtract one from vertical high byte of object
    a = (uint8_t)(a - ram[(uint8_t)(SprObject_Y_HighPos + x)] - !carry_flag);
    x = ROM(DefaultYOnscreenOfs + y); // load offset value here
    neg_flag = ((a - 0x0) & 0x80) != 0;
    // Original branch: if under top of the screen or beyond bottom, branch
    if (!neg_flag) {
      x = ROM((DefaultYOnscreenOfs + 1) + y); // if not, load alternate offset value here
      neg_flag = ((a - 0x1) & 0x80) != 0;
      if (!neg_flag) { goto YLdBData; } // if one vertical unit or more above the screen, branch
      ram[0x6] = 0x20; // if no branching, load value here and store
      a = 0x4; // load some other value and execute subroutine
      CALL(DividePDiff, 0xf25f);
    }
    
YLdBData:
    a = ROM(YOffscreenBitsData + x); // get offscreen data bits using offset
    x = ram[0x4]; // reobtain position in buffer
    // if bits not zero, branch to leave
    if (a != 0x0) {
      return;
    }
    y--; neg_flag = (y & 0x80) != 0; // otherwise, do bottom of the screen now
  } while (!neg_flag);
  // ExYOfsBS:
  return;
}

// --------------------------------
// (these apply to these three subsections)
// $04 - used to store proper offset
// $05 - used as adder in DividePDiff
// $06 - used to store preset value used to compare to pixel difference in $07
// $07 - used to store difference between coordinates of object and screen edges
// in: x; out: a, x
void GetXOffscreenBits(void) {
  ram[0x4] = x; // save position in buffer to here
  y = 0x1; // start with right side of screen
  do {
    // XOfsLoop:
    a = ram[(ScreenEdge_X_Pos + y) & 0x7ff]; // get pixel coordinate of edge
    carry_flag = true; // get difference between pixel coordinate of edge
    { uint16_t diff = (uint16_t)(a - ram[(uint8_t)(SprObject_X_Position + x)] - !carry_flag); carry_flag = diff <= 0xff; a = (uint8_t)diff; } // and pixel coordinate of object position
    ram[0x7] = a; // store here
    a = ram[(ScreenEdge_PageLoc + y) & 0x7ff]; // get page location of edge
    a = (uint8_t)(a - ram[(uint8_t)(SprObject_PageLoc + x)] - !carry_flag); // subtract from page location of object position
    x = ROM(DefaultXOnscreenOfs + y); // load offset value here
    neg_flag = ((a - 0x0) & 0x80) != 0;
    // Original branch: if beyond right edge or in front of left edge, branch
    if (!neg_flag) {
      x = ROM((DefaultXOnscreenOfs + 1) + y); // if not, load alternate offset value here
      neg_flag = ((a - 0x1) & 0x80) != 0;
      if (!neg_flag) { goto XLdBData; } // if one page or more to the left of either edge, branch
      ram[0x6] = 0x38; // if no branching, load value here and store
      a = 0x8; // load some other value and execute subroutine
      CALL(DividePDiff, 0xf21d);
    }
    
XLdBData:
    a = ROM(XOffscreenBitsData + x); // get bits here
    x = ram[0x4]; // reobtain position in buffer
    // if bits not zero, branch to leave
    if (a != 0x0) {
      return;
    }
    y--; neg_flag = (y & 0x80) != 0; // otherwise, do left side of screen now
  } while (!neg_flag); // branch if not already done with left side
  // ExXOfsBS:
  return;
}

// --------------------------------
// in: a, y; out: x
void DividePDiff(void) {
  ram[0x5] = a; // store current value in A here
  a = ram[0x7]; // get pixel difference
  carry_flag = a >= ram[0x6]; // compare to preset value
  // if pixel difference >= preset value, branch
  if (carry_flag) {
    return;
  }
  a = a >> 1; // divide by eight
  a = a >> 1;
  a = a >> 1;
  a &= 0x7; // mask out all but 3 LSB
  carry_flag = y >= 0x1; // right side of the screen or top?
  // Original branch: if so, branch, use difference / 8 as offset
  if (!carry_flag) {
    a = (uint8_t)(a + ram[0x5] + carry_flag); // if not, add value to difference / 8
  }
  // SetOscrO:
  x = a; // use as offset
  // ExDivPD:
  return; // leave
}

// -------------------------------------------------------------------------------------
// unused space
// -------------------------------------------------------------------------------------
void SoundEngine(void) {
  a = ram[OperMode]; zero_flag = a == 0; // are we in title screen mode?
  if (zero_flag) {
    apu_write(SND_MASTERCTRL_REG, a); // if so, disable sound and leave
    return;
  }
  // SndOn:
  apu_write(JOYPAD_PORT2, 0xff); // disable irqs and set frame counter mode???
  apu_write(SND_MASTERCTRL_REG, 0xf); // enable first four channels
  a = ram[PauseModeFlag]; zero_flag = a == 0; // is sound already in pause mode?
  if (zero_flag) {
    a = ram[PauseSoundQueue]; // if not, check pause sfx queue    
    { uint8_t m = 0x1; carry_flag = a >= m; zero_flag = a == m; }
    if (!zero_flag) { goto RunSoundSubroutines; } // if queue is empty, skip pause mode routine
  }
  // InPause:
  a = ram[PauseSoundBuffer]; zero_flag = a == 0; // check pause sfx buffer
  if (!zero_flag) { goto ContPau; }
  a = ram[PauseSoundQueue]; zero_flag = a == 0; // check pause queue
  if (zero_flag) { goto SkipSoundSubroutines; }
  ram[PauseSoundBuffer] = a; // if queue full, store in buffer and activate
  ram[PauseModeFlag] = a; // pause mode to interrupt game sounds
  a = 0x0; // disable sound and clear sfx buffers
  apu_write(SND_MASTERCTRL_REG, a);
  ram[Square1SoundBuffer] = a;
  ram[Square2SoundBuffer] = a;
  ram[NoiseSoundBuffer] = a;
  apu_write(SND_MASTERCTRL_REG, 0xf); // enable sound again
  ram[Squ1_SfxLenCounter] = 0x2a; // store length of sound in pause counter
  
PTone1F:
  a = 0x44; // play first tone
  goto PTRegC; // unconditional branch
  
ContPau:
  a = ram[Squ1_SfxLenCounter]; // check pause length left
  { uint8_t m = 0x24; carry_flag = a >= m; zero_flag = a == m; } // time to play second?
  if (!zero_flag) {
    { uint8_t m = 0x1e; carry_flag = a >= m; zero_flag = a == m; } // time to play first again?
    if (zero_flag) { goto PTone1F; }
    { uint8_t m = 0x18; carry_flag = a >= m; zero_flag = a == m; } // time to play second again?
    if (!zero_flag) { goto DecPauC; } // only load regs during times, otherwise skip
  }
  // PTone2F:
  a = 0x64; // store reg contents and play the pause sfx
  
PTRegC:
  x = 0x84;
  y = 0x7f; zero_flag = y == 0;
  CALL(PlaySqu1Sfx, 0xf32d);
  
DecPauC:
  ram[Squ1_SfxLenCounter]--; zero_flag = ram[Squ1_SfxLenCounter] == 0; // decrement pause sfx counter
  if (!zero_flag) { goto SkipSoundSubroutines; }
  apu_write(SND_MASTERCTRL_REG, 0x0); // disable sound if in pause mode and; not currently playing the pause sfx
  a = ram[PauseSoundBuffer]; // if no longer playing pause sfx, check to see
  // if we need to be playing sound again
  if (a == 0x2) {
    ram[PauseModeFlag] = 0x0; // clear pause mode to allow game sounds again
  }
  // SkipPIn:
  ram[PauseSoundBuffer] = 0x0; // clear pause sfx buffer
  goto SkipSoundSubroutines;
  
RunSoundSubroutines:
  CALL(Square1SfxHandler, 0xf34d); // play sfx on square channel 1
  CALL(Square2SfxHandler, 0xf350); //  ''  ''  '' square channel 2
  CALL(NoiseSfxHandler, 0xf353); //  ''  ''  '' noise channel
  CALL(MusicHandler, 0xf356); // play music on all channels
  a = 0x0; // clear the music queues
  ram[AreaMusicQueue] = a;
  ram[EventMusicQueue] = a;
  
SkipSoundSubroutines:
  a = 0x0; // clear the sound effects queues
  ram[Square1SoundQueue] = a;
  ram[Square2SoundQueue] = a;
  ram[NoiseSoundQueue] = a;
  ram[PauseSoundQueue] = a;
  y = ram[DAC_Counter]; // load some sort of counter 
  a = ram[AreaMusicBuffer];
  a &= 0b00000011; zero_flag = a == 0; // check for specific music
  if (!zero_flag) {
    ram[DAC_Counter]++; // increment and check counter
    carry_flag = y >= 0x30;
    if (!carry_flag) { goto StrWave; } // if not there yet, just store it
  }
  // NoIncDAC:
  a = y; zero_flag = a == 0;
  // Original branch: if we are at zero, do not decrement 
  if (!zero_flag) {
    ram[DAC_Counter]--; // decrement counter
  }
  
StrWave:
  apu_write((SND_DELTA_REG + 1), y); // store into DMC load register (??)
  return; // we are done here
}

// --------------------------------
// in: x, y; out: none
void Dump_Squ1_Regs(void) {
  apu_write((SND_SQUARE1_REG + 1), y); // dump the contents of X and Y into square 1's control regs
  apu_write(SND_SQUARE1_REG, x);
  return;
}

// in: a, x, y; out: a, x, zero
void PlaySqu1Sfx(void) {
  CALL(Dump_Squ1_Regs, 0xf38a); // do sub to set ctrl regs for square 1, then set frequency regs
  SetFreq_Squ1(); // fallthrough
  return;
}

// in: a; out: a, x, y, zero
void SetFreq_Squ1(void) {
  x = 0x0; // set frequency reg offset for square 1 sound channel
  Dump_Freq_Regs(); // fallthrough
  return;
}

// in: a, x; out: a, y, zero
void Dump_Freq_Regs(void) {
  y = a;
  a = read_byte((uint16_t)((FreqRegLookupTbl + 1) + y)); zero_flag = a == 0; // use previous contents of A for sound reg offset
  // if zero, then do not load
  if (zero_flag) {
    return;
  }
  dynamic_ram_write((uint16_t)((SND_REGISTER + 2) + x), a); // first byte goes into LSB of frequency divider
  a = ROM(FreqRegLookupTbl + y); // second byte goes into 3 MSB plus extra bit for 
  a |= 0b00001000; zero_flag = a == 0; // length counter
  dynamic_ram_write((uint16_t)((SND_REGISTER + 3) + x), a);
  // NoTone:
  return;
}

// in: x, y; out: none
void Dump_Sq2_Regs(void) {
  apu_write(SND_SQUARE2_REG, x); // dump the contents of X and Y into square 2's control regs
  apu_write((SND_SQUARE2_REG + 1), y);
  return;
}

// in: a, x, y; out: a, x
void PlaySqu2Sfx(void) {
  CALL(Dump_Sq2_Regs, 0xf3a8); // do sub to set ctrl regs for square 2, then set frequency regs
  SetFreq_Squ2(); // fallthrough
  return;
}

// in: a; out: a, x, y, zero
void SetFreq_Squ2(void) {
  x = 0x4; // set frequency reg offset for square 2 sound channel
  Dump_Freq_Regs(); return; // unconditional branch
}

// in: a; out: none
void SetFreq_Tri(void) {
  x = 0x8; // set frequency reg offset for triangle sound channel
  Dump_Freq_Regs(); return; // unconditional branch
}

// in: none; out: x, carry
void Square1SfxHandler(void) {
  goto Square1SfxHandler;
  // --------------------------------
  
PlayFlagpoleSlide:
  ram[Squ1_SfxLenCounter] = 0x40; // store length of flagpole sound
  a = 0x62; // load part of reg contents for flagpole sound
  CALL(SetFreq_Squ1, 0xf3c8);
  x = 0x99; // now load the rest
  goto FPS2nd;
  
PlaySmallJump:
  a = 0x26; // branch here for small mario jumping sound
  goto JumpRegContents;
  
PlayBigJump:
  a = 0x18; // branch here for big mario jumping sound
  
JumpRegContents:
  x = 0x82; // note that small and big jump borrow each others' reg contents
  y = 0xa7; zero_flag = y == 0; // anyway, this loads the first part of mario's jumping sound
  CALL(PlaySqu1Sfx, 0xf3d9);
  ram[Squ1_SfxLenCounter] = 0x28; // store length of sfx for both jumping sounds; then continue on here
  
ContinueSndJump:
  a = ram[Squ1_SfxLenCounter]; // jumping sounds seem to be composed of three parts
  { uint8_t m = 0x25; carry_flag = a >= m; zero_flag = a == m; } // check for time to play second part yet
  if (zero_flag) {
    x = 0x5f; // load second part
    y = 0xf6; zero_flag = y == 0;
    goto DmpJpFPS; // unconditional branch
  }
  // N2Prt:
  { uint8_t m = 0x20; carry_flag = a >= m; zero_flag = a == m; } // check for third part
  if (!zero_flag) { goto DecJpFPS; }
  x = 0x48; // load third part
  
FPS2nd:
  y = 0xbc; zero_flag = y == 0; // the flagpole slide sound shares part of third part
  
DmpJpFPS:
  CALL(Dump_Squ1_Regs, 0xf3f6);
  if (!zero_flag) { goto DecJpFPS; } // unconditional branch outta here
  
PlayFireballThrow:
  a = 0x5;
  y = 0x99; // load reg contents for fireball throw sound
  goto Fthrow; // unconditional branch
  
PlayBump:
  a = 0xa; // load length of sfx and reg contents for bump sound
  y = 0x93;
  
Fthrow:
  x = 0x9e; // the fireball sound shares reg contents with the bump sound
  ram[Squ1_SfxLenCounter] = a;
  a = 0xc; zero_flag = a == 0; // load offset for bump sound
  CALL(PlaySqu1Sfx, 0xf40c);
  
ContinueBumpThrow:
  a = ram[Squ1_SfxLenCounter]; // check for second part of bump sound
  { uint8_t m = 0x6; carry_flag = a >= m; zero_flag = a == m; }
  if (zero_flag) {
    a = 0xbb; // load second part directly
    apu_write((SND_SQUARE1_REG + 1), a);
  }
  
DecJpFPS:
  goto BranchToDecLength1; // unconditional branch
  
Square1SfxHandler:
  y = ram[Square1SoundQueue]; zero_flag = y == 0; neg_flag = (y & 0x80) != 0; // check for sfx in queue
  if (!zero_flag) {
    ram[Square1SoundBuffer] = y; // if found, put in buffer
    if (neg_flag) { goto PlaySmallJump; } // small jump
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlayBigJump; } // big jump
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlayBump; } // bump
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlaySwimStomp; } // swim/stomp
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlaySmackEnemy; } // smack enemy
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlayPipeDownInj; } // pipedown/injury
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlayFireballThrow; } // fireball throw
    carry_flag = (ram[Square1SoundQueue] & 1) != 0; ram[Square1SoundQueue] = ram[Square1SoundQueue] >> 1;
    if (carry_flag) { goto PlayFlagpoleSlide; } // slide flagpole
  }
  // CheckSfx1Buffer:
  a = ram[Square1SoundBuffer]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // check for sfx in buffer 
  // if not found, exit sub
  if (zero_flag) {
    return;
  }
  if (neg_flag) { goto ContinueSndJump; } // small mario jump 
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueSndJump; } // big mario jump 
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueBumpThrow; } // bump
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueSwimStomp; } // swim/stomp
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueSmackEnemy; } // smack enemy
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinuePipeDownInj; } // pipedown/injury
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueBumpThrow; } // fireball throw
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto DecrementSfx1Length; } // slide flagpole
  // ExS1H:
  return;
  
PlaySwimStomp:
  ram[Squ1_SfxLenCounter] = 0xe; // store length of swim/stomp sound
  y = 0x9c; // store reg contents for swim/stomp sound
  x = 0x9e;
  a = 0x26; zero_flag = a == 0;
  CALL(PlaySqu1Sfx, 0xf468);
  
ContinueSwimStomp:
  y = ram[Squ1_SfxLenCounter]; // look up reg contents in data section based on
  a = ROM((SwimStompEnvelopeData - 1) + y); // length of sound left, used to control sound's
  apu_write(SND_SQUARE1_REG, a); // envelope
  { uint8_t m = 0x6; carry_flag = y >= m; zero_flag = y == m; }
  if (zero_flag) {
    a = 0x9e; // when the length counts down to a certain point, put this
    apu_write((SND_SQUARE1_REG + 2), a); // directly into the LSB of square 1's frequency divider
  }
  
BranchToDecLength1:
  goto DecrementSfx1Length; // unconditional branch (regardless of how we got here)
  
PlaySmackEnemy:
  a = 0xe; // store length of smack enemy sound
  y = 0xcb;
  x = 0x9f;
  ram[Squ1_SfxLenCounter] = a;
  a = 0x28; zero_flag = a == 0; // store reg contents for smack enemy sound
  CALL(PlaySqu1Sfx, 0xf48a);
  if (!zero_flag) { goto DecrementSfx1Length; } // unconditional branch
  
ContinueSmackEnemy:
  y = ram[Squ1_SfxLenCounter]; // check about halfway through
  { uint8_t m = 0x8; carry_flag = y >= m; zero_flag = y == m; }
  if (zero_flag) {
    apu_write((SND_SQUARE1_REG + 2), 0xa0); // if we're at the about-halfway point, make the second tone; in the smack enemy sound
    a = 0x9f;
  } else {
    // SmSpc:
    a = 0x90; // this creates spaces in the sound, giving it its distinct noise
  }
  // SmTick:
  apu_write(SND_SQUARE1_REG, a);
  
DecrementSfx1Length:
  ram[Squ1_SfxLenCounter]--; zero_flag = ram[Squ1_SfxLenCounter] == 0; // decrement length of sfx
  if (!zero_flag) {
    return;
  }
  StopSquare1Sfx(); // fallthrough
  return;
  
PlayPipeDownInj:
  ram[Squ1_SfxLenCounter] = 0x2f; // load length of pipedown sound
  
ContinuePipeDownInj:
  a = ram[Squ1_SfxLenCounter]; // some bitwise logic, forces the regs
  carry_flag = (a & 1) != 0; a = a >> 1; // to be written to only during six specific times
  // Original branch: during which d3 must be set and d1-0 must be clear
  if (!carry_flag) {
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto NoPDwnL; }
    a &= 0b00000010; zero_flag = a == 0;
    if (zero_flag) { goto NoPDwnL; }
    y = 0x91; // and this is where it actually gets written in
    x = 0x9a;
    a = 0x44; zero_flag = a == 0;
    CALL(PlaySqu1Sfx, 0xf4d0);
  }
  
NoPDwnL:
  goto DecrementSfx1Length;
}

// in: none; out: x
void StopSquare1Sfx(void) {
  ram[0xf1] = 0x0; // if end of sfx reached, clear buffer; and stop making the sfx
  apu_write(SND_MASTERCTRL_REG, 0xe);
  x = 0xf;
  apu_write(SND_MASTERCTRL_REG, x);
  // ExSfx1:
  return;
}

// in: none; out: x
void StopSquare2Sfx(void) {
  apu_write(SND_MASTERCTRL_REG, 0xd); // stop playing the sfx
  x = 0xf;
  apu_write(SND_MASTERCTRL_REG, x);
  // ExSfx2:
  return;
}

// in: none; out: x, carry
void Square2SfxHandler(void) {
  goto Square2SfxHandler;
  // --------------------------------
  
PlayCoinGrab:
  a = 0x35; // load length of coin grab sound
  x = 0x8d; // and part of reg contents
  goto CGrab_TTickRegL;
  
PlayTimerTick:
  a = 0x6; // load length of timer tick sound
  x = 0x98; // and part of reg contents
  
CGrab_TTickRegL:
  ram[Squ2_SfxLenCounter] = a;
  y = 0x7f; // load the rest of reg contents 
  a = 0x42; // of coin grab and timer tick sound
  CALL(PlaySqu2Sfx, 0xf52b);
  
ContinueCGrabTTick:
  a = ram[Squ2_SfxLenCounter]; // check for time to play second tone yet
  { uint8_t m = 0x30; carry_flag = a >= m; zero_flag = a == m; } // timer tick sound also executes this, not sure why
  if (zero_flag) {
    a = 0x54; // if so, load the tone directly into the reg
    apu_write((SND_SQUARE2_REG + 2), a);
  }
  // N2Tone:
  goto DecrementSfx2Length;
  
PlayBlast:
  ram[Squ2_SfxLenCounter] = 0x20; // load length of fireworks/gunfire sound
  y = 0x94; // load reg contents of fireworks/gunfire sound
  a = 0x5e;
  goto SBlasJ;
  
ContinueBlast:
  a = ram[Squ2_SfxLenCounter]; // check for time to play second part
  { uint8_t m = 0x18; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) { goto DecrementSfx2Length; }
  y = 0x93; // load second part reg contents then
  a = 0x18;
  
SBlasJ:
  goto BlstSJp; // unconditional branch to load rest of reg contents
  
PlayPowerUpGrab:
  ram[Squ2_SfxLenCounter] = 0x36; // load length of power-up grab sound
  
ContinuePowerUpGrab:
  a = ram[Squ2_SfxLenCounter]; // load frequency reg based on length left over
  carry_flag = (a & 1) != 0; a = a >> 1; // divide by 2
  if (carry_flag) { goto DecrementSfx2Length; } // alter frequency every other frame
  y = a;
  a = ROM((PowerUpGrabFreqData - 1) + y); // use length left over / 2 for frequency offset
  x = 0x5d; // store reg contents of power-up grab sound
  y = 0x7f;
  
LoadSqu2Regs:
  CALL(PlaySqu2Sfx, 0xf567);
  
DecrementSfx2Length:
  ram[Squ2_SfxLenCounter]--; zero_flag = ram[Squ2_SfxLenCounter] == 0; // decrement length of sfx
  if (!zero_flag) {
    return;
  }
  
EmptySfx2Buffer:
  ram[Square2SoundBuffer] = 0x0; // initialize square 2's sound effects buffer
  StopSquare2Sfx(); // fallthrough
  return;
  
Square2SfxHandler:
  a = ram[Square2SoundBuffer]; // special handling for the 1-up sound to keep it
  a &= Sfx_ExtraLife; zero_flag = a == 0; // from being interrupted by other sounds on square 2
  if (!zero_flag) { goto ContinueExtraLife; }
  y = ram[Square2SoundQueue]; zero_flag = y == 0; neg_flag = (y & 0x80) != 0; // check for sfx in queue
  if (!zero_flag) {
    ram[Square2SoundBuffer] = y; // if found, put in buffer and check for the following
    if (neg_flag) { goto PlayBowserFall; } // bowser fall
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayCoinGrab; } // coin grab
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayGrowPowerUp; } // power-up reveal
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayGrowVine; } // vine grow
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayBlast; } // fireworks/gunfire
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayTimerTick; } // timer tick
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayPowerUpGrab; } // power-up grab
    carry_flag = (ram[Square2SoundQueue] & 1) != 0; ram[Square2SoundQueue] = ram[Square2SoundQueue] >> 1;
    if (carry_flag) { goto PlayExtraLife; } // 1-up
  }
  // CheckSfx2Buffer:
  a = ram[Square2SoundBuffer]; zero_flag = a == 0; neg_flag = (a & 0x80) != 0; // check for sfx in buffer
  // if not found, exit sub
  if (zero_flag) {
    return;
  }
  if (neg_flag) { goto ContinueBowserFall; } // bowser fall
  carry_flag = (a & 1) != 0; a = a >> 1;
  // Original branch: coin grab
  if (!carry_flag) {
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto ContinueGrowItems; } // power-up reveal
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto ContinueGrowItems; } // vine grow
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto ContinueBlast; } // fireworks/gunfire
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto Cont_CGrab_TTick; } // timer tick
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto ContinuePowerUpGrab; } // power-up grab
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto ContinueExtraLife; } // 1-up
    // ExS2H:
    return;
  }
  
Cont_CGrab_TTick:
  goto ContinueCGrabTTick;
  
JumpToDecLength2:
  goto DecrementSfx2Length;
  
PlayBowserFall:
  ram[Squ2_SfxLenCounter] = 0x38; // load length of bowser defeat sound
  y = 0xc4; // load contents of reg for bowser defeat sound
  a = 0x18;
  
BlstSJp:
  goto PBFRegs;
  
ContinueBowserFall:
  a = ram[Squ2_SfxLenCounter]; // check for almost near the end
  { uint8_t m = 0x8; carry_flag = a >= m; zero_flag = a == m; }
  if (!zero_flag) { goto DecrementSfx2Length; }
  y = 0xa4; // if so, load the rest of reg contents for bowser defeat sound
  a = 0x5a;
  
PBFRegs:
  x = 0x9f; // the fireworks/gunfire sound shares part of reg contents here
  
EL_LRegs:
  goto LoadSqu2Regs; // this is an unconditional branch outta here
  
PlayExtraLife:
  ram[Squ2_SfxLenCounter] = 0x30; // load length of 1-up sound
  
ContinueExtraLife:
  a = ram[Squ2_SfxLenCounter];
  x = 0x3; // load new tones only every eight frames
  do {
    // DivLLoop:
    carry_flag = (a & 1) != 0; a = a >> 1;
    if (carry_flag) { goto JumpToDecLength2; } // if any bits set here, branch to dec the length
    x--; zero_flag = x == 0;
  } while (!zero_flag); // do this until all bits checked, if none set, continue
  y = a;
  a = ROM((ExtraLifeFreqData - 1) + y); // load our reg contents
  x = 0x82;
  y = 0x7f;
  goto EL_LRegs; // unconditional branch
  
PlayGrowPowerUp:
  a = 0x10; // load length of power-up reveal sound
  goto GrowItemRegs;
  
PlayGrowVine:
  a = 0x20; // load length of vine grow sound
  
GrowItemRegs:
  ram[Squ2_SfxLenCounter] = a;
  apu_write((SND_SQUARE2_REG + 1), 0x7f); // load contents of reg for both sounds directly
  ram[Sfx_SecondaryCounter] = 0x0; // start secondary counter for both sounds
  
ContinueGrowItems:
  ram[Sfx_SecondaryCounter]++; // increment secondary counter for both sounds
  a = ram[Sfx_SecondaryCounter]; // this sound doesn't decrement the usual counter
  a = a >> 1; // divide by 2 to get the offset
  y = a;
  { uint8_t m = ram[Squ2_SfxLenCounter]; carry_flag = y >= m; zero_flag = y == m; } // have we reached the end yet?
  // Original branch: if so, branch to jump, and stop playing sounds
  if (!zero_flag) {
    apu_write(SND_SQUARE2_REG, 0x9d); // load contents of other reg directly
    a = ROM(PUp_VGrow_FreqData + y); // use secondary counter / 2 as offset for frequency regs
    CALL(SetFreq_Squ2, 0xf626);
    return;
  }
  // StopGrowItems:
  goto EmptySfx2Buffer; // branch to stop playing sounds
}

// in: none; out: x, carry
void NoiseSfxHandler(void) {
  goto NoiseSfxHandler;
  // --------------------------------
  
PlayBrickShatter:
  ram[Noise_SfxLenCounter] = 0x20; // load length of brick shatter sound
  
ContinueBrickShatter:
  a = ram[Noise_SfxLenCounter];
  carry_flag = (a & 1) != 0; a = a >> 1; // divide by 2 and check for bit set to use offset
  if (!carry_flag) { goto DecrementSfx3Length; }
  y = a;
  x = ROM(BrickShatterFreqData + y); // load reg contents of brick shatter sound
  a = read_byte((uint16_t)(BrickShatterEnvData + y));
  
PlayNoiseSfx:
  apu_write(SND_NOISE_REG, a); // play the sfx
  apu_write((SND_NOISE_REG + 2), x);
  apu_write((SND_NOISE_REG + 3), 0x18);
  
DecrementSfx3Length:
  ram[Noise_SfxLenCounter]--; zero_flag = ram[Noise_SfxLenCounter] == 0; // decrement length of sfx
  if (!zero_flag) {
    return;
  }
  apu_write(SND_NOISE_REG, 0xf0); // if done, stop playing the sfx
  ram[NoiseSoundBuffer] = 0x0;
  // ExSfx3:
  return;
  
NoiseSfxHandler:
  y = ram[NoiseSoundQueue]; zero_flag = y == 0; // check for sfx in queue
  if (!zero_flag) {
    ram[NoiseSoundBuffer] = y; // if found, put in buffer
    carry_flag = (ram[NoiseSoundQueue] & 1) != 0; ram[NoiseSoundQueue] = ram[NoiseSoundQueue] >> 1;
    if (carry_flag) { goto PlayBrickShatter; } // brick shatter
    carry_flag = (ram[NoiseSoundQueue] & 1) != 0; ram[NoiseSoundQueue] = ram[NoiseSoundQueue] >> 1;
    if (carry_flag) { goto PlayBowserFlame; } // bowser flame
  }
  // CheckNoiseBuffer:
  a = ram[NoiseSoundBuffer]; zero_flag = a == 0; // check for sfx in buffer
  // if not found, exit sub
  if (zero_flag) {
    return;
  }
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueBrickShatter; } // brick shatter
  carry_flag = (a & 1) != 0; a = a >> 1;
  if (carry_flag) { goto ContinueBowserFlame; } // bowser flame
  // ExNH:
  return;
  
PlayBowserFlame:
  ram[Noise_SfxLenCounter] = 0x40; // load length of bowser flame sound
  
ContinueBowserFlame:
  a = ram[Noise_SfxLenCounter];
  carry_flag = (a & 1) != 0; a = a >> 1;
  y = a;
  x = 0xf; // load reg contents of bowser flame sound
  a = read_byte((uint16_t)((BowserFlameEnvData - 1) + y)); zero_flag = a == 0;
  if (!zero_flag) { goto PlayNoiseSfx; } // unconditional branch here
  ContinueMusic(); // fallthrough
  return;
}

// --------------------------------
// in: none; out: x, carry
void ContinueMusic(void) {
  goto HandleSquare2Music; // if we have music, start with square 2 channel
  
LoadEventMusic:
  ram[EventMusicBuffer] = a; // copy event music queue contents to buffer
  { uint8_t m = DeathMusic; carry_flag = a >= m; zero_flag = a == m; } // is it death music?
  // Original branch: if not, jump elsewhere
  if (zero_flag) {
    CALL(StopSquare1Sfx, 0xf6ad); // stop sfx in square 1 and 2
    CALL(StopSquare2Sfx, 0xf6b0); // but clear only square 1's sfx buffer
  }
  // NoStopSfx:
  x = ram[AreaMusicBuffer];
  ram[AreaMusicBuffer_Alt] = x; // save current area music buffer to be re-obtained later
  y = 0x0;
  ram[NoteLengthTblAdder] = y; // default value for additional length byte offset
  ram[AreaMusicBuffer] = y; // clear area music buffer
  zero_flag = a == TimeRunningOutMusic; // is it time running out music?
  if (!zero_flag) { goto FindEventMusicHeader; }
  x = 0x8; // load offset to be added to length byte of header
  ram[NoteLengthTblAdder] = x;
  goto FindEventMusicHeader; // unconditional branch
  
GMLoopB:
  ram[GroundMusicHeaderOfs] = y;
  
HandleAreaMusicLoopB:
  ram[EventMusicBuffer] = 0x0; // clear event music buffer
  ram[AreaMusicBuffer] = a; // copy area music queue contents to buffer
  // is it ground level music?
  if (a == 0x1) {
    ram[GroundMusicHeaderOfs]++; // increment but only if playing ground level music
    y = ram[GroundMusicHeaderOfs]; // is it time to loopback ground level music?
    { uint8_t m = 0x32; carry_flag = y >= m; zero_flag = y == m; }
    if (!zero_flag) { goto LoadHeader; } // branch ahead with alternate offset
    y = 0x11;
    goto GMLoopB; // unconditional branch
  }
  // FindAreaMusicHeader:
  y = 0x8; // load Y for offset of area music
  ram[MusicOffset_Square2] = y; // residual instruction here
  do {
    
FindEventMusicHeader:
    y++; // increment Y pointer based on previously loaded queue contents
    carry_flag = (a & 1) != 0; a = a >> 1; // bit shift and increment until we find a set bit for music
  } while (!carry_flag);
  
LoadHeader:
  a = ROM(MusicHeaderOffsetData + y); // load offset for header
  y = a;
  ram[NoteLenLookupTblOfs] = ROM(MusicHeaderData + y); // now load the header
  ram[MusicDataLow] = ROM((MusicHeaderData + 1) + y);
  ram[MusicDataHigh] = ROM((MusicHeaderData + 2) + y);
  ram[MusicOffset_Triangle] = ROM((MusicHeaderData + 3) + y);
  ram[MusicOffset_Square1] = ROM((MusicHeaderData + 4) + y);
  a = ROM((MusicHeaderData + 5) + y);
  ram[MusicOffset_Noise] = a;
  ram[NoiseDataLoopbackOfs] = a;
  a = 0x1; // initialize music note counters
  ram[Squ2_NoteLenCounter] = a;
  ram[Squ1_NoteLenCounter] = a;
  ram[Tri_NoteLenCounter] = a;
  ram[Noise_BeatLenCounter] = a;
  a = 0x0; // initialize music data offset for square 2
  ram[MusicOffset_Square2] = a;
  ram[AltRegContentFlag] = a; // initialize alternate control reg data used by square 1
  apu_write(SND_MASTERCTRL_REG, 0xb); // disable triangle channel and reenable it
  apu_write(SND_MASTERCTRL_REG, 0xf);
  
HandleSquare2Music:
  ram[Squ2_NoteLenCounter]--; zero_flag = ram[Squ2_NoteLenCounter] == 0; // decrement square 2 note length
  // Original branch: is it time for more data?  if not, branch to end tasks
  if (zero_flag) {
    y = ram[MusicOffset_Square2]; // increment square 2 music offset and fetch data
    ram[MusicOffset_Square2]++;
    a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    // Original branch: if zero, the data is a null terminator
    if (!zero_flag) {
      if (!neg_flag) { goto Squ2NoteHandler; } // if non-negative, data is a note
    } else {
      // EndOfMusicData:
      a = ram[EventMusicBuffer]; // check secondary buffer for time running out music
      { uint8_t m = TimeRunningOutMusic; carry_flag = a >= m; zero_flag = a == m; }
      if (zero_flag) {
        a = ram[AreaMusicBuffer_Alt]; zero_flag = a == 0; // load previously saved contents of primary buffer
        if (!zero_flag) { goto MusicLoopBack; } // and start playing the song again if there is one
      }
      // NotTRO:
      a &= VictoryMusic; zero_flag = a == 0; // check for victory music (the only secondary that loops)
      if (!zero_flag) { goto VictoryMLoopBack; }
      a = ram[AreaMusicBuffer]; // check primary buffer for any music except pipe intro
      a &= 0b01011111; zero_flag = a == 0;
      // Original branch: if any area music except pipe intro, music loops
      if (zero_flag) {
        a = 0x0; // clear primary and secondary buffers and initialize
        ram[AreaMusicBuffer] = a; // control regs of square and triangle channels
        ram[EventMusicBuffer] = a;
        apu_write(SND_TRIANGLE_REG, a);
        a = 0x90;
        apu_write(SND_SQUARE1_REG, a);
        apu_write(SND_SQUARE2_REG, a);
        return;
      }
      
MusicLoopBack:
      goto HandleAreaMusicLoopB;
      
VictoryMLoopBack:
      goto LoadEventMusic;
    }
    // Squ2LengthHandler:
    CALL(ProcessLengthData, 0xf77c); // store length of note
    ram[Squ2_NoteLenBuffer] = a;
    y = ram[MusicOffset_Square2]; // fetch another byte (MUST NOT BE LENGTH BYTE!)
    ram[MusicOffset_Square2]++;
    a = read_byte(indirect_y_addr(MusicData));
    
Squ2NoteHandler:
    x = ram[Square2SoundBuffer]; zero_flag = x == 0; // is there a sound playing on this channel?
    if (zero_flag) {
      CALL(SetFreq_Squ2, 0xf78c); // no, then play the note
      // Original branch: check to see if note is rest
      if (!zero_flag) {
        CALL(LoadControlRegs, 0xf791); // if not, load control regs for square 2
      }
      // Rest:
      ram[Squ2_EnvelopeDataCtrl] = a; // save contents of A
      CALL(Dump_Sq2_Regs, 0xf797); // dump X and Y into square 2 control regs
    }
    // SkipFqL1:
    ram[Squ2_NoteLenCounter] = ram[Squ2_NoteLenBuffer]; // save length in square 2 note counter
  }
  // MiscSqu2MusicTasks:
  a = ram[Square2SoundBuffer]; zero_flag = a == 0; // is there a sound playing on square 2?
  if (zero_flag) {
    a = ram[EventMusicBuffer]; // check for death music or d4 set on secondary buffer
    a &= 0b10010001; zero_flag = a == 0; // note that regs for death music or d4 are loaded by default
    if (!zero_flag) { goto HandleSquare1Music; }
    y = ram[Squ2_EnvelopeDataCtrl]; zero_flag = y == 0; // check for contents saved from LoadControlRegs
    if (!zero_flag) {
      ram[Squ2_EnvelopeDataCtrl]--; // decrement unless already zero
    }
    // NoDecEnv1:
    CALL(LoadEnvelopeData, 0xf7b3); // do a load of envelope data to replace default
    apu_write(SND_SQUARE2_REG, a); // based on offset set by first load unless playing
    x = 0x7f; // death music or d4 set on secondary buffer
    apu_write((SND_SQUARE2_REG + 1), x);
  }
  
HandleSquare1Music:
  y = ram[MusicOffset_Square1]; zero_flag = y == 0; // is there a nonzero offset here?
  // Original branch: if not, skip ahead to the triangle channel
  if (!zero_flag) {
    ram[Squ1_NoteLenCounter]--; zero_flag = ram[Squ1_NoteLenCounter] == 0; // decrement square 1 note length
    // Original branch: is it time for more data?
    if (zero_flag) {
      
FetchSqu1MusicData:
      y = ram[MusicOffset_Square1]; // increment square 1 music offset and fetch data
      ram[MusicOffset_Square1]++;
      a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0;
      // Original branch: if nonzero, then skip this part
      if (zero_flag) {
        apu_write(SND_SQUARE1_REG, 0x83); // store some data into control regs for square 1
        a = 0x94; // and fetch another byte of data, used to give
        apu_write((SND_SQUARE1_REG + 1), a); // death music its unique sound
        ram[AltRegContentFlag] = a;
        goto FetchSqu1MusicData; // unconditional branch
      }
      // Squ1NoteHandler:
      CALL(AlternateLengthHandler, 0xf7de);
      ram[Squ1_NoteLenCounter] = a; // save contents of A in square 1 note counter
      y = ram[Square1SoundBuffer]; zero_flag = y == 0; // is there a sound playing on square 1?
      if (!zero_flag) { goto HandleTriangleMusic; }
      a = x;
      a &= 0b00111110; // change saved data to appropriate note format
      CALL(SetFreq_Squ1, 0xf7eb); // play the note
      if (!zero_flag) {
        CALL(LoadControlRegs, 0xf7f0);
      }
      // SkipCtrlL:
      ram[Squ1_EnvelopeDataCtrl] = a; // save envelope offset
      CALL(Dump_Squ1_Regs, 0xf7f6);
    }
    // MiscSqu1MusicTasks:
    a = ram[Square1SoundBuffer]; zero_flag = a == 0; // is there a sound playing on square 1?
    if (!zero_flag) { goto HandleTriangleMusic; }
    a = ram[EventMusicBuffer]; // check for death music or d4 set on secondary buffer
    a &= 0b10010001; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[Squ1_EnvelopeDataCtrl]; zero_flag = y == 0; // check saved envelope offset
      if (!zero_flag) {
        ram[Squ1_EnvelopeDataCtrl]--; // decrement unless already zero
      }
      // NoDecEnv2:
      CALL(LoadEnvelopeData, 0xf80c); // do a load of envelope data
      apu_write(SND_SQUARE1_REG, a); // based on offset set by first load
    }
    // DeathMAltReg:
    a = ram[AltRegContentFlag]; zero_flag = a == 0; // check for alternate control reg data
    if (zero_flag) {
      a = 0x7f; // load this value if zero, the alternate value
    }
    // DoAltLoad:
    apu_write((SND_SQUARE1_REG + 1), a); // if nonzero, and let's move on
  }
  
HandleTriangleMusic:
  a = ram[MusicOffset_Triangle];
  ram[Tri_NoteLenCounter]--; zero_flag = ram[Tri_NoteLenCounter] == 0; // decrement triangle note length
  // Original branch: is it time for more data?
  if (zero_flag) {
    y = ram[MusicOffset_Triangle]; // increment square 1 music offset and fetch data
    ram[MusicOffset_Triangle]++;
    a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    // Original branch: if zero, skip all this and move on to noise 
    if (!zero_flag) {
      // Original branch: if non-negative, data is note
      if (neg_flag) {
        CALL(ProcessLengthData, 0xf82d); // otherwise, it is length data
        ram[Tri_NoteLenBuffer] = a; // save contents of A
        apu_write(SND_TRIANGLE_REG, 0x1f); // load some default data for triangle control reg
        y = ram[MusicOffset_Triangle]; // fetch another byte
        ram[MusicOffset_Triangle]++;
        a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0;
        if (zero_flag) { goto LoadTriCtrlReg; } // check once more for nonzero data
      }
      // TriNoteHandler:
      CALL(SetFreq_Tri, 0xf840);
      x = ram[Tri_NoteLenBuffer]; // save length in triangle note counter
      ram[Tri_NoteLenCounter] = x;
      a = ram[EventMusicBuffer];
      a &= 0b01101110; zero_flag = a == 0; // check for death music or d4 set on secondary buffer
      // Original branch: if playing any other secondary, skip primary buffer check
      if (zero_flag) {
        a = ram[AreaMusicBuffer]; // check primary buffer for water or castle level music
        a &= 0b00001010; zero_flag = a == 0;
        if (zero_flag) { goto HandleNoiseMusic; } // if playing any other primary, or death or d4, go on to noise routine
      }
      // NotDOrD4:
      a = x; // if playing water or castle music or any secondary
      carry_flag = a >= 0x12; // besides death music or d4 set, check length of note
      if (!carry_flag) {
        a = ram[EventMusicBuffer]; // check for win castle music again if not playing a long note
        a &= EndOfCastleMusic; zero_flag = a == 0;
        if (!zero_flag) {
          a = 0xf; // load value $0f if playing the win castle music and playing a short
          goto LoadTriCtrlReg; // note, load value $1f if playing water or castle level music or any
        }
        // MediN:
        a = 0x1f; // secondary besides death and d4 except win castle or win castle and playing
        goto LoadTriCtrlReg; // a short note, and load value $ff if playing a long note on water, castle
      }
      // LongN:
      a = 0xff; // or any secondary (including win castle) except death and d4
    }
    
LoadTriCtrlReg:
    apu_write(SND_TRIANGLE_REG, a); // save final contents of A into control reg for triangle
  }
  
HandleNoiseMusic:
  a = ram[AreaMusicBuffer]; // check if playing underground or castle music
  a &= 0b11110011; zero_flag = a == 0;
  // if so, skip the noise routine
  if (zero_flag) {
    return;
  }
  ram[Noise_BeatLenCounter]--; zero_flag = ram[Noise_BeatLenCounter] == 0; // decrement noise beat length
  // is it time for more data?
  if (!zero_flag) {
    return;
  }
  do {
    // FetchNoiseBeatData:
    y = ram[MusicOffset_Noise]; // increment noise beat offset and fetch data
    ram[MusicOffset_Noise]++;
    a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0; // get noise beat data, if nonzero, branch to handle
    if (!zero_flag) { goto NoiseBeatHandler; }
    a = ram[NoiseDataLoopbackOfs]; zero_flag = a == 0; // if data is zero, reload original noise beat offset
    ram[MusicOffset_Noise] = a; // and loopback next time around
  } while (!zero_flag); // unconditional branch
  
NoiseBeatHandler:
  CALL(AlternateLengthHandler, 0xf88c);
  ram[Noise_BeatLenCounter] = a; // store length in noise beat counter
  a = x;
  a &= 0b00111110; zero_flag = a == 0; // reload data and erase length bits
  // Original branch: if no beat data, silence
  if (!zero_flag) {
    { uint8_t m = 0x30; carry_flag = a >= m; zero_flag = a == m; } // check the beat data and play the appropriate
    // Original branch: noise accordingly
    if (!zero_flag) {
      { uint8_t m = 0x20; carry_flag = a >= m; zero_flag = a == m; }
      if (!zero_flag) {
        a &= 0b00010000; zero_flag = a == 0;
        if (zero_flag) { goto SilentBeat; }
        a = 0x1c; // short beat data
        x = 0x3;
        y = 0x18;
        goto PlayBeat;
      }
      // StrongBeat:
      a = 0x1c; // strong beat data
      x = 0xc;
      y = 0x18;
      goto PlayBeat;
    }
    // LongBeat:
    a = 0x1c; // long beat data
    x = 0x3;
    y = 0x58;
  } else {
    
SilentBeat:
    a = 0x10; // silence
  }
  
PlayBeat:
  apu_write(SND_NOISE_REG, a); // load beat data into noise regs
  apu_write((SND_NOISE_REG + 2), x);
  apu_write((SND_NOISE_REG + 3), y);
  // ExitMusicHandler:
  return;
}

void MusicHandler(void) {
  a = ram[EventMusicQueue]; zero_flag = a == 0; // check event music queue
  if (zero_flag) {
    a = ram[AreaMusicQueue]; zero_flag = a == 0; // check area music queue
    if (!zero_flag) { goto LoadAreaMusic; }
    a = ram[EventMusicBuffer]; // check both buffers
    a |= ram[AreaMusicBuffer]; zero_flag = a == 0;
    if (!zero_flag) {
      ContinueMusic();
      return;
    }
    return; // no music, then leave
  }
  
LoadEventMusic:
  ram[EventMusicBuffer] = a; // copy event music queue contents to buffer
  { uint8_t m = DeathMusic; carry_flag = a >= m; zero_flag = a == m; } // is it death music?
  // Original branch: if not, jump elsewhere
  if (zero_flag) {
    CALL(StopSquare1Sfx, 0xf6ad); // stop sfx in square 1 and 2
    CALL(StopSquare2Sfx, 0xf6b0); // but clear only square 1's sfx buffer
  }
  // NoStopSfx:
  x = ram[AreaMusicBuffer];
  ram[AreaMusicBuffer_Alt] = x; // save current area music buffer to be re-obtained later
  y = 0x0;
  ram[NoteLengthTblAdder] = y; // default value for additional length byte offset
  ram[AreaMusicBuffer] = y; // clear area music buffer
  zero_flag = a == TimeRunningOutMusic; // is it time running out music?
  if (!zero_flag) { goto FindEventMusicHeader; }
  x = 0x8; // load offset to be added to length byte of header
  ram[NoteLengthTblAdder] = x;
  goto FindEventMusicHeader; // unconditional branch
  
LoadAreaMusic:
  { uint8_t m = 0x4; carry_flag = a >= m; zero_flag = a == m; } // is it underground music?
  // Original branch: no, do not stop square 1 sfx
  if (zero_flag) {
    CALL(StopSquare1Sfx, 0xf6ce);
  }
  // NoStop1:
  y = 0x10; // start counter used only by ground level music
  
GMLoopB:
  ram[GroundMusicHeaderOfs] = y;
  
HandleAreaMusicLoopB:
  ram[EventMusicBuffer] = 0x0; // clear event music buffer
  ram[AreaMusicBuffer] = a; // copy area music queue contents to buffer
  // is it ground level music?
  if (a == 0x1) {
    ram[GroundMusicHeaderOfs]++; // increment but only if playing ground level music
    y = ram[GroundMusicHeaderOfs]; // is it time to loopback ground level music?
    { uint8_t m = 0x32; carry_flag = y >= m; zero_flag = y == m; }
    if (!zero_flag) { goto LoadHeader; } // branch ahead with alternate offset
    y = 0x11;
    goto GMLoopB; // unconditional branch
  }
  // FindAreaMusicHeader:
  y = 0x8; // load Y for offset of area music
  ram[MusicOffset_Square2] = y; // residual instruction here
  do {
    
FindEventMusicHeader:
    y++; // increment Y pointer based on previously loaded queue contents
    carry_flag = (a & 1) != 0; a = a >> 1; // bit shift and increment until we find a set bit for music
  } while (!carry_flag);
  
LoadHeader:
  a = ROM(MusicHeaderOffsetData + y); // load offset for header
  y = a;
  ram[NoteLenLookupTblOfs] = ROM(MusicHeaderData + y); // now load the header
  ram[MusicDataLow] = ROM((MusicHeaderData + 1) + y);
  ram[MusicDataHigh] = ROM((MusicHeaderData + 2) + y);
  ram[MusicOffset_Triangle] = ROM((MusicHeaderData + 3) + y);
  ram[MusicOffset_Square1] = ROM((MusicHeaderData + 4) + y);
  a = ROM((MusicHeaderData + 5) + y);
  ram[MusicOffset_Noise] = a;
  ram[NoiseDataLoopbackOfs] = a;
  a = 0x1; // initialize music note counters
  ram[Squ2_NoteLenCounter] = a;
  ram[Squ1_NoteLenCounter] = a;
  ram[Tri_NoteLenCounter] = a;
  ram[Noise_BeatLenCounter] = a;
  a = 0x0; // initialize music data offset for square 2
  ram[MusicOffset_Square2] = a;
  ram[AltRegContentFlag] = a; // initialize alternate control reg data used by square 1
  apu_write(SND_MASTERCTRL_REG, 0xb); // disable triangle channel and reenable it
  apu_write(SND_MASTERCTRL_REG, 0xf);
  // HandleSquare2Music:
  ram[Squ2_NoteLenCounter]--; zero_flag = ram[Squ2_NoteLenCounter] == 0; // decrement square 2 note length
  // Original branch: is it time for more data?  if not, branch to end tasks
  if (zero_flag) {
    y = ram[MusicOffset_Square2]; // increment square 2 music offset and fetch data
    ram[MusicOffset_Square2]++;
    a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    // Original branch: if zero, the data is a null terminator
    if (!zero_flag) {
      if (!neg_flag) { goto Squ2NoteHandler; } // if non-negative, data is a note
    } else {
      // EndOfMusicData:
      a = ram[EventMusicBuffer]; // check secondary buffer for time running out music
      { uint8_t m = TimeRunningOutMusic; carry_flag = a >= m; zero_flag = a == m; }
      if (zero_flag) {
        a = ram[AreaMusicBuffer_Alt]; zero_flag = a == 0; // load previously saved contents of primary buffer
        if (!zero_flag) { goto MusicLoopBack; } // and start playing the song again if there is one
      }
      // NotTRO:
      a &= VictoryMusic; zero_flag = a == 0; // check for victory music (the only secondary that loops)
      if (!zero_flag) { goto VictoryMLoopBack; }
      a = ram[AreaMusicBuffer]; // check primary buffer for any music except pipe intro
      a &= 0b01011111; zero_flag = a == 0;
      // Original branch: if any area music except pipe intro, music loops
      if (zero_flag) {
        a = 0x0; // clear primary and secondary buffers and initialize
        ram[AreaMusicBuffer] = a; // control regs of square and triangle channels
        ram[EventMusicBuffer] = a;
        apu_write(SND_TRIANGLE_REG, a);
        a = 0x90;
        apu_write(SND_SQUARE1_REG, a);
        apu_write(SND_SQUARE2_REG, a);
        return;
      }
      
MusicLoopBack:
      goto HandleAreaMusicLoopB;
      
VictoryMLoopBack:
      goto LoadEventMusic;
    }
    // Squ2LengthHandler:
    CALL(ProcessLengthData, 0xf77c); // store length of note
    ram[Squ2_NoteLenBuffer] = a;
    y = ram[MusicOffset_Square2]; // fetch another byte (MUST NOT BE LENGTH BYTE!)
    ram[MusicOffset_Square2]++;
    a = read_byte(indirect_y_addr(MusicData));
    
Squ2NoteHandler:
    x = ram[Square2SoundBuffer]; zero_flag = x == 0; // is there a sound playing on this channel?
    if (zero_flag) {
      CALL(SetFreq_Squ2, 0xf78c); // no, then play the note
      // Original branch: check to see if note is rest
      if (!zero_flag) {
        CALL(LoadControlRegs, 0xf791); // if not, load control regs for square 2
      }
      // Rest:
      ram[Squ2_EnvelopeDataCtrl] = a; // save contents of A
      CALL(Dump_Sq2_Regs, 0xf797); // dump X and Y into square 2 control regs
    }
    // SkipFqL1:
    ram[Squ2_NoteLenCounter] = ram[Squ2_NoteLenBuffer]; // save length in square 2 note counter
  }
  // MiscSqu2MusicTasks:
  a = ram[Square2SoundBuffer]; zero_flag = a == 0; // is there a sound playing on square 2?
  if (zero_flag) {
    a = ram[EventMusicBuffer]; // check for death music or d4 set on secondary buffer
    a &= 0b10010001; zero_flag = a == 0; // note that regs for death music or d4 are loaded by default
    if (!zero_flag) { goto HandleSquare1Music; }
    y = ram[Squ2_EnvelopeDataCtrl]; zero_flag = y == 0; // check for contents saved from LoadControlRegs
    if (!zero_flag) {
      ram[Squ2_EnvelopeDataCtrl]--; // decrement unless already zero
    }
    // NoDecEnv1:
    CALL(LoadEnvelopeData, 0xf7b3); // do a load of envelope data to replace default
    apu_write(SND_SQUARE2_REG, a); // based on offset set by first load unless playing
    x = 0x7f; // death music or d4 set on secondary buffer
    apu_write((SND_SQUARE2_REG + 1), x);
  }
  
HandleSquare1Music:
  y = ram[MusicOffset_Square1]; zero_flag = y == 0; // is there a nonzero offset here?
  // Original branch: if not, skip ahead to the triangle channel
  if (!zero_flag) {
    ram[Squ1_NoteLenCounter]--; zero_flag = ram[Squ1_NoteLenCounter] == 0; // decrement square 1 note length
    // Original branch: is it time for more data?
    if (zero_flag) {
      
FetchSqu1MusicData:
      y = ram[MusicOffset_Square1]; // increment square 1 music offset and fetch data
      ram[MusicOffset_Square1]++;
      a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0;
      // Original branch: if nonzero, then skip this part
      if (zero_flag) {
        apu_write(SND_SQUARE1_REG, 0x83); // store some data into control regs for square 1
        a = 0x94; // and fetch another byte of data, used to give
        apu_write((SND_SQUARE1_REG + 1), a); // death music its unique sound
        ram[AltRegContentFlag] = a;
        goto FetchSqu1MusicData; // unconditional branch
      }
      // Squ1NoteHandler:
      CALL(AlternateLengthHandler, 0xf7de);
      ram[Squ1_NoteLenCounter] = a; // save contents of A in square 1 note counter
      y = ram[Square1SoundBuffer]; zero_flag = y == 0; // is there a sound playing on square 1?
      if (!zero_flag) { goto HandleTriangleMusic; }
      a = x;
      a &= 0b00111110; // change saved data to appropriate note format
      CALL(SetFreq_Squ1, 0xf7eb); // play the note
      if (!zero_flag) {
        CALL(LoadControlRegs, 0xf7f0);
      }
      // SkipCtrlL:
      ram[Squ1_EnvelopeDataCtrl] = a; // save envelope offset
      CALL(Dump_Squ1_Regs, 0xf7f6);
    }
    // MiscSqu1MusicTasks:
    a = ram[Square1SoundBuffer]; zero_flag = a == 0; // is there a sound playing on square 1?
    if (!zero_flag) { goto HandleTriangleMusic; }
    a = ram[EventMusicBuffer]; // check for death music or d4 set on secondary buffer
    a &= 0b10010001; zero_flag = a == 0;
    if (zero_flag) {
      y = ram[Squ1_EnvelopeDataCtrl]; zero_flag = y == 0; // check saved envelope offset
      if (!zero_flag) {
        ram[Squ1_EnvelopeDataCtrl]--; // decrement unless already zero
      }
      // NoDecEnv2:
      CALL(LoadEnvelopeData, 0xf80c); // do a load of envelope data
      apu_write(SND_SQUARE1_REG, a); // based on offset set by first load
    }
    // DeathMAltReg:
    a = ram[AltRegContentFlag]; zero_flag = a == 0; // check for alternate control reg data
    if (zero_flag) {
      a = 0x7f; // load this value if zero, the alternate value
    }
    // DoAltLoad:
    apu_write((SND_SQUARE1_REG + 1), a); // if nonzero, and let's move on
  }
  
HandleTriangleMusic:
  a = ram[MusicOffset_Triangle];
  ram[Tri_NoteLenCounter]--; zero_flag = ram[Tri_NoteLenCounter] == 0; // decrement triangle note length
  // Original branch: is it time for more data?
  if (zero_flag) {
    y = ram[MusicOffset_Triangle]; // increment square 1 music offset and fetch data
    ram[MusicOffset_Triangle]++;
    a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0; neg_flag = (a & 0x80) != 0;
    // Original branch: if zero, skip all this and move on to noise 
    if (!zero_flag) {
      // Original branch: if non-negative, data is note
      if (neg_flag) {
        CALL(ProcessLengthData, 0xf82d); // otherwise, it is length data
        ram[Tri_NoteLenBuffer] = a; // save contents of A
        apu_write(SND_TRIANGLE_REG, 0x1f); // load some default data for triangle control reg
        y = ram[MusicOffset_Triangle]; // fetch another byte
        ram[MusicOffset_Triangle]++;
        a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0;
        if (zero_flag) { goto LoadTriCtrlReg; } // check once more for nonzero data
      }
      // TriNoteHandler:
      CALL(SetFreq_Tri, 0xf840);
      x = ram[Tri_NoteLenBuffer]; // save length in triangle note counter
      ram[Tri_NoteLenCounter] = x;
      a = ram[EventMusicBuffer];
      a &= 0b01101110; zero_flag = a == 0; // check for death music or d4 set on secondary buffer
      // Original branch: if playing any other secondary, skip primary buffer check
      if (zero_flag) {
        a = ram[AreaMusicBuffer]; // check primary buffer for water or castle level music
        a &= 0b00001010; zero_flag = a == 0;
        if (zero_flag) { goto HandleNoiseMusic; } // if playing any other primary, or death or d4, go on to noise routine
      }
      // NotDOrD4:
      a = x; // if playing water or castle music or any secondary
      carry_flag = a >= 0x12; // besides death music or d4 set, check length of note
      if (!carry_flag) {
        a = ram[EventMusicBuffer]; // check for win castle music again if not playing a long note
        a &= EndOfCastleMusic; zero_flag = a == 0;
        if (!zero_flag) {
          a = 0xf; // load value $0f if playing the win castle music and playing a short
          goto LoadTriCtrlReg; // note, load value $1f if playing water or castle level music or any
        }
        // MediN:
        a = 0x1f; // secondary besides death and d4 except win castle or win castle and playing
        goto LoadTriCtrlReg; // a short note, and load value $ff if playing a long note on water, castle
      }
      // LongN:
      a = 0xff; // or any secondary (including win castle) except death and d4
    }
    
LoadTriCtrlReg:
    apu_write(SND_TRIANGLE_REG, a); // save final contents of A into control reg for triangle
  }
  
HandleNoiseMusic:
  a = ram[AreaMusicBuffer]; // check if playing underground or castle music
  a &= 0b11110011; zero_flag = a == 0;
  // if so, skip the noise routine
  if (zero_flag) {
    return;
  }
  ram[Noise_BeatLenCounter]--; zero_flag = ram[Noise_BeatLenCounter] == 0; // decrement noise beat length
  // is it time for more data?
  if (!zero_flag) {
    return;
  }
  do {
    // FetchNoiseBeatData:
    y = ram[MusicOffset_Noise]; // increment noise beat offset and fetch data
    ram[MusicOffset_Noise]++;
    a = read_byte(indirect_y_addr(MusicData)); zero_flag = a == 0; // get noise beat data, if nonzero, branch to handle
    if (!zero_flag) { goto NoiseBeatHandler; }
    a = ram[NoiseDataLoopbackOfs]; zero_flag = a == 0; // if data is zero, reload original noise beat offset
    ram[MusicOffset_Noise] = a; // and loopback next time around
  } while (!zero_flag); // unconditional branch
  
NoiseBeatHandler:
  CALL(AlternateLengthHandler, 0xf88c);
  ram[Noise_BeatLenCounter] = a; // store length in noise beat counter
  a = x;
  a &= 0b00111110; zero_flag = a == 0; // reload data and erase length bits
  // Original branch: if no beat data, silence
  if (!zero_flag) {
    { uint8_t m = 0x30; carry_flag = a >= m; zero_flag = a == m; } // check the beat data and play the appropriate
    // Original branch: noise accordingly
    if (!zero_flag) {
      { uint8_t m = 0x20; carry_flag = a >= m; zero_flag = a == m; }
      if (!zero_flag) {
        a &= 0b00010000; zero_flag = a == 0;
        if (zero_flag) { goto SilentBeat; }
        a = 0x1c; // short beat data
        x = 0x3;
        y = 0x18;
        goto PlayBeat;
      }
      // StrongBeat:
      a = 0x1c; // strong beat data
      x = 0xc;
      y = 0x18;
      goto PlayBeat;
    }
    // LongBeat:
    a = 0x1c; // long beat data
    x = 0x3;
    y = 0x58;
  } else {
    
SilentBeat:
    a = 0x10; // silence
  }
  
PlayBeat:
  apu_write(SND_NOISE_REG, a); // load beat data into noise regs
  apu_write((SND_NOISE_REG + 2), x);
  apu_write((SND_NOISE_REG + 3), y);
  // ExitMusicHandler:
  return;
}

// in: a; out: a, x, y, carry
void AlternateLengthHandler(void) {
  x = a; // save a copy of original byte into X
  { bool carry = (a & 1) != 0; a = (uint8_t)(a >> 1 | carry_flag << 7); carry_flag = carry; } // save LSB from original byte into carry
  a = x; // reload original byte and rotate three times
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // turning xx00000x into 00000xxx, with the
  { bool carry = (a & 0x80) != 0; a = (uint8_t)(a << 1 | carry_flag); carry_flag = carry; } // bit in carry as the MSB here
  a = (uint8_t)(a << 1 | carry_flag);
  ProcessLengthData(); // fallthrough
  return;
}

// in: a; out: a, y, carry
void ProcessLengthData(void) {
  a &= 0b00000111; // clear all but the three LSBs
  carry_flag = false;
  { uint16_t sum = a + ram[0xf0] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add offset loaded from first header byte
  { uint16_t sum = a + ram[NoteLengthTblAdder] + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; } // add extra if time running out music
  y = a;
  a = read_byte((uint16_t)(MusicLengthLookupTbl + y)); // load length
  return;
}

// in: none; out: a, x, y, zero
void LoadControlRegs(void) {
  a = ram[EventMusicBuffer]; // check secondary buffer for win castle music
  a &= EndOfCastleMusic; zero_flag = a == 0;
  if (!zero_flag) {
    a = 0x4; // this value is only used for win castle music
  } else {
    // NotECstlM:
    a = ram[AreaMusicBuffer];
    a &= 0b01111101; zero_flag = a == 0; // check primary buffer for water music
    if (!zero_flag) {
      a = 0x8; // this is the default value for all other music
      goto AllMus;
    }
    // WaterMus:
    a = 0x28; // this value is used for water music and all other event music
  }
  
AllMus:
  x = 0x82; // load contents of other sound regs for square 2
  y = 0x7f; zero_flag = y == 0;
  return;
}

// in: y; out: a
void LoadEnvelopeData(void) {
  a = ram[EventMusicBuffer]; // check secondary buffer for win castle music
  a &= EndOfCastleMusic; zero_flag = a == 0;
  if (!zero_flag) {
    a = read_byte((uint16_t)(EndOfCastleMusicEnvData + y)); // load data from offset for win castle music
    return;
  }
  // LoadUsualEnvData:
  a = ram[AreaMusicBuffer]; // check primary buffer for water music
  a &= 0b01111101; zero_flag = a == 0;
  if (!zero_flag) {
    a = read_byte((uint16_t)(AreaMusicEnvData + y)); // load default data from offset for all other music
    return;
  }
  // LoadWaterEventMusEnvData:
  a = read_byte((uint16_t)(WaterEventMusEnvData + y)); // load data from offset for water music and all other event music
  return;
  // --------------------------------
  // music header offsets
}

void cpu_reset_entry(void) { Start(); }
void cpu_nmi_entry(void) { NonMaskableInterrupt(); }
