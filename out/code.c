#include "code.h"

void smb(RunState state) {
  switch (state) {
    case RUN_STATE_RESET: goto Start;
    case RUN_STATE_NMI_HANDLER: goto NonMaskableInterrupt;
  }
  
Start:
    sei(); // pretty standard 6502 type init here
    cld();
    lda_imm(0b00010000); // init PPU control register 1 
    write_byte(PPU_CTRL_REG1, a);
    ldx_imm(0xff); // reset stack pointer
    sp = x;
  
VBlank1:
    lda_abs(PPU_STATUS); // wait two frames
    if (!neg_flag) { goto VBlank1; }
  
VBlank2:
    lda_abs(PPU_STATUS);
    if (!neg_flag) { goto VBlank2; }
    ldy_imm(ColdBootOffset); // load default cold boot pointer
    ldx_imm(0x5); // this is where we check for a warm boot
  
WBootCheck:
    lda_absx(TopScoreDisplay); // check each score digit in the top score
    cmp_imm(10); // to see if we have a valid digit
    if (carry_flag) { goto ColdBoot; } // if not, give up and proceed with cold boot
    dex();
    if (!neg_flag) { goto WBootCheck; }
    lda_abs(WarmBootValidation); // second checkpoint, check to see if 
    cmp_imm(0xa5); // another location has a specific value
    if (!zero_flag) { goto ColdBoot; }
    ldy_imm(WarmBootOffset); // if passed both, load warm boot pointer
  
ColdBoot:
    InitializeMemory(); // clear memory using pointer in Y
    write_byte(SND_DELTA_REG + 1, a); // reset delta counter load register
    write_byte(OperMode, a); // reset primary mode of operation
    lda_imm(0xa5); // set warm boot flag
    write_byte(WarmBootValidation, a);
    write_byte(PseudoRandomBitReg, a); // set seed for pseudorandom register
    lda_imm(0b00001111);
    write_byte(SND_MASTERCTRL_REG, a); // enable all sound channels except dmc
    lda_imm(0b00000110);
    write_byte(PPU_CTRL_REG2, a); // turn off clipping for OAM and background
    jsr(MoveAllSpritesOffscreen, 0);
    InitializeNameTables(); // initialize both name tables
    inc_abs(DisableScreenFlag); // set flag to disable screen output
    lda_abs(Mirror_PPU_CTRL_REG1);
    ora_imm(0b10000000); // enable NMIs
    WritePPUReg1();
    return; // <rti> //  EndlessLoop: jmp EndlessLoop ; endless loop, need I say more?
    // -------------------------------------------------------------------------------------
    // $00 - vram buffer address table low, also used for pseudorandom bit
    // $01 - vram buffer address table high
  
NonMaskableInterrupt:
    lda_abs(Mirror_PPU_CTRL_REG1); // disable NMIs in mirror reg
    and_imm(0b01111111); // save all other bits
    write_byte(Mirror_PPU_CTRL_REG1, a);
    and_imm(0b01111110); // alter name table address to be $2800
    write_byte(PPU_CTRL_REG1, a); // (essentially $2000) but save other bits
    lda_abs(Mirror_PPU_CTRL_REG2); // disable OAM and background display by default
    and_imm(0b11100110);
    ldy_abs(DisableScreenFlag); // get screen disable flag
    if (!zero_flag) { goto ScreenOff; } // if set, used bits as-is
    lda_abs(Mirror_PPU_CTRL_REG2); // otherwise reenable bits and save them
    ora_imm(0b00011110);
  
ScreenOff:
    write_byte(Mirror_PPU_CTRL_REG2, a); // save bits for later but not in register at the moment
    and_imm(0b11100111); // disable screen for now
    write_byte(PPU_CTRL_REG2, a);
    ldx_abs(PPU_STATUS); // reset flip-flop and reset scroll registers to zero
    lda_imm(0x0);
    InitScroll();
    write_byte(PPU_SPR_ADDR, a); // reset spr-ram address register
    lda_imm(0x2); // perform spr-ram DMA access on $0200-$02ff
    write_byte(SPR_DMA, a);
    ldx_abs(VRAM_Buffer_AddrCtrl); // load control for pointer to buffer contents
    lda_absx(VRAM_AddrTable_Low); // set indirect at $00 to pointer
    write_byte(0x0, a);
    lda_absx(VRAM_AddrTable_High);
    write_byte(0x1, a);
    jsr(UpdateScreen, 1); // update screen with buffer contents
    ldy_imm(0x0);
    ldx_abs(VRAM_Buffer_AddrCtrl); // check for usage of $0341
    cpx_imm(0x6);
    if (!zero_flag) { goto InitBuffer; }
    iny(); // get offset based on usage
  
InitBuffer:
    ldx_absy(VRAM_Buffer_Offset);
    lda_imm(0x0); // clear buffer header at last location
    write_byte(VRAM_Buffer1_Offset + x, a);
    write_byte(VRAM_Buffer1 + x, a);
    write_byte(VRAM_Buffer_AddrCtrl, a); // reinit address control to $0301
    lda_abs(Mirror_PPU_CTRL_REG2); // copy mirror of $2001 to register
    write_byte(PPU_CTRL_REG2, a);
    jsr(SoundEngine, 2); // play sound
    jsr(ReadJoypads, 3); // read joypads
    jsr(PauseRoutine, 4); // handle pause
    UpdateTopScore();
    lda_abs(GamePauseStatus); // check for pause status
    lsr_acc();
    if (carry_flag) { goto PauseSkip; }
    lda_abs(TimerControl); // if master timer control not set, decrement
    if (zero_flag) { goto DecTimers; } // all frame and interval timers
    dec_abs(TimerControl);
    if (!zero_flag) { goto NoDecTimers; }
  
DecTimers:
    ldx_imm(0x14); // load end offset for end of frame timers
    dec_abs(IntervalTimerControl); // decrement interval timer control,
    if (!neg_flag) { goto DecTimersLoop; } // if not expired, only frame timers will decrement
    lda_imm(0x14);
    write_byte(IntervalTimerControl, a); // if control for interval timers expired,
    ldx_imm(0x23); // interval timers will decrement along with frame timers
  
DecTimersLoop:
    lda_absx(Timers); // check current timer
    if (zero_flag) { goto SkipExpTimer; } // if current timer expired, branch to skip,
    dec_absx(Timers); // otherwise decrement the current timer
  
SkipExpTimer:
    dex(); // move onto next timer
    if (!neg_flag) { goto DecTimersLoop; } // do this until all timers are dealt with
  
NoDecTimers:
    inc_zp(FrameCounter); // increment frame counter
  
PauseSkip:
    ldx_imm(0x0);
    ldy_imm(0x7);
    lda_abs(PseudoRandomBitReg); // get first memory location of LSFR bytes
    and_imm(0b00000010); // mask out all but d1
    write_byte(0x0, a); // save here
    lda_abs(PseudoRandomBitReg + 1); // get second memory location
    and_imm(0b00000010); // mask out all but d1
    eor_zp(0x0); // perform exclusive-OR on d1 from first and second bytes
    carry_flag = false; // if neither or both are set, carry will be clear
    if (zero_flag) { goto RotPRandomBit; }
    carry_flag = true; // if one or the other is set, carry will be set
  
RotPRandomBit:
    ror_absx(PseudoRandomBitReg); // rotate carry into d7, and rotate last bit into carry
    inx(); // increment to next byte
    dey(); // decrement for loop
    if (!zero_flag) { goto RotPRandomBit; }
    lda_abs(Sprite0HitDetectFlag); // check for flag here
    if (zero_flag) { goto SkipSprite0; }
  
Sprite0Clr:
    lda_abs(PPU_STATUS); // wait for sprite 0 flag to clear, which will
    and_imm(0b01000000); // not happen until vblank has ended
    if (!zero_flag) { goto Sprite0Clr; }
    lda_abs(GamePauseStatus); // if in pause mode, do not bother with sprites at all
    lsr_acc();
    if (carry_flag) { goto Sprite0Hit; }
    jsr(MoveSpritesOffscreen, 5);
    SpriteShuffler();
  
Sprite0Hit:
    lda_abs(PPU_STATUS); // do sprite #0 hit detection
    and_imm(0b01000000);
    if (zero_flag) { goto Sprite0Hit; }
    ldy_imm(0x14); // small delay, to wait until we hit horizontal blank time
  
HBlankDelay:
    dey();
    if (!zero_flag) { goto HBlankDelay; }
  
SkipSprite0:
    lda_abs(HorizontalScroll); // set scroll registers from variables
    write_byte(PPU_SCROLL_REG, a);
    lda_abs(VerticalScroll);
    write_byte(PPU_SCROLL_REG, a);
    lda_abs(Mirror_PPU_CTRL_REG1); // load saved mirror of $2000
    pha();
    write_byte(PPU_CTRL_REG1, a);
    lda_abs(GamePauseStatus); // if in pause mode, do not perform operation mode stuff
    lsr_acc();
    if (carry_flag) { goto SkipMainOper; }
    jsr(OperModeExecutionTree, 6); // otherwise do one of many, many possible subroutines
  
SkipMainOper:
    lda_abs(PPU_STATUS); // reset flip-flop
    pla();
    ora_imm(0b10000000); // reactivate NMIs
    write_byte(PPU_CTRL_REG1, a);
    return; // <rti> // we are done until the next frame!
    // -------------------------------------------------------------------------------------
  
PauseRoutine:
    lda_abs(OperMode); // are we in victory mode?
    cmp_imm(VictoryModeValue); // if so, go ahead
    if (zero_flag) { goto ChkPauseTimer; }
    cmp_imm(GameModeValue); // are we in game mode?
    if (!zero_flag) { goto ExitPause; } // if not, leave
    lda_abs(OperMode_Task); // if we are in game mode, are we running game engine?
    cmp_imm(0x3);
    if (!zero_flag) { goto ExitPause; } // if not, leave
  
ChkPauseTimer:
    lda_abs(GamePauseTimer); // check if pause timer is still counting down
    if (zero_flag) { goto ChkStart; }
    dec_abs(GamePauseTimer); // if so, decrement and leave
    goto rts;
  
ChkStart:
    lda_abs(SavedJoypad1Bits); // check to see if start is pressed
    and_imm(Start_Button); // on controller 1
    if (zero_flag) { goto ClrPauseTimer; }
    lda_abs(GamePauseStatus); // check to see if timer flag is set
    and_imm(0b10000000); // and if so, do not reset timer (residual,
    if (!zero_flag) { goto ExitPause; } // joypad reading routine makes this unnecessary)
    lda_imm(0x2b); // set pause timer
    write_byte(GamePauseTimer, a);
    lda_abs(GamePauseStatus);
    tay();
    iny(); // set pause sfx queue for next pause mode
    write_byte(PauseSoundQueue, y);
    eor_imm(0b00000001); // invert d0 and set d7
    ora_imm(0b10000000);
    if (!zero_flag) { goto SetPause; } // unconditional branch
  
ClrPauseTimer:
    lda_abs(GamePauseStatus); // clear timer flag if timer is at zero and start button
    and_imm(0b01111111); // is not pressed
  
SetPause:
    write_byte(GamePauseStatus, a);
  
ExitPause:
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used for preset value
    // -------------------------------------------------------------------------------------
  
OperModeExecutionTree:
    lda_abs(OperMode); // this is the heart of the entire program,
    // jsr JumpEngine
    switch (a) {
      case 0: goto TitleScreenMode;
      case 1: goto GameMode;
      case 2: goto VictoryMode;
      case 3: goto GameOverMode;
    }
  
MoveAllSpritesOffscreen:
    ldy_imm(0x0); // this routine moves all sprites off the screen
    //  in multiple places, the bit absolute ($2c) instruction opcode is used to skip the next instruction using only one byte
    goto MoveSpritesOffscreenSkip; //  .db $2c ;BIT instruction opcode
  
MoveSpritesOffscreen:
    ldy_imm(0x4); // this routine moves all but sprite 0
  
MoveSpritesOffscreenSkip:
    lda_imm(0xf8); // off the screen
  
SprInitLoop:
    write_byte(Sprite_Y_Position + y, a); // write 248 into OAM data's Y coordinate
    iny(); // which will move it off the screen
    iny();
    iny();
    iny();
    if (!zero_flag) { goto SprInitLoop; }
    goto rts;
    // -------------------------------------------------------------------------------------
  
TitleScreenMode:
    lda_abs(OperMode_Task);
    // jsr JumpEngine
    switch (a) {
      case 0: goto InitializeGame;
      case 1: goto ScreenRoutines;
      case 2: goto PrimaryGameSetup;
      case 3: goto GameMenuRoutine;
    }
  
GameMenuRoutine:
    ldy_imm(0x0);
    lda_abs(SavedJoypad1Bits); // check to see if either player pressed
    ora_abs(SavedJoypad2Bits); // only the start button (either joypad)
    cmp_imm(Start_Button);
    if (zero_flag) { goto StartGame; }
    cmp_imm(A_Button + Start_Button); // check to see if A + start was pressed
    if (!zero_flag) { goto ChkSelect; } // if not, branch to check select button
  
StartGame:
    goto ChkContinue; // if either start or A + start, execute here
  
ChkSelect:
    cmp_imm(Select_Button); // check to see if the select button was pressed
    if (zero_flag) { goto SelectBLogic; } // if so, branch reset demo timer
    ldx_abs(DemoTimer); // otherwise check demo timer
    if (!zero_flag) { goto ChkWorldSel; } // if demo timer not expired, branch to check world selection
    write_byte(SelectTimer, a); // set controller bits here if running demo
    DemoEngine(); // run through the demo actions
    if (carry_flag) { goto ResetTitle; } // if carry flag set, demo over, thus branch
    goto RunDemo; // otherwise, run game engine for demo
  
ChkWorldSel:
    ldx_abs(WorldSelectEnableFlag); // check to see if world selection has been enabled
    if (zero_flag) { goto NullJoypad; }
    cmp_imm(B_Button); // if so, check to see if the B button was pressed
    if (!zero_flag) { goto NullJoypad; }
    iny(); // if so, increment Y and execute same code as select
  
SelectBLogic:
    lda_abs(DemoTimer); // if select or B pressed, check demo timer one last time
    if (zero_flag) { goto ResetTitle; } // if demo timer expired, branch to reset title screen mode
    lda_imm(0x18); // otherwise reset demo timer
    write_byte(DemoTimer, a);
    lda_abs(SelectTimer); // check select/B button timer
    if (!zero_flag) { goto NullJoypad; } // if not expired, branch
    lda_imm(0x10); // otherwise reset select button timer
    write_byte(SelectTimer, a);
    cpy_imm(0x1); // was the B button pressed earlier?  if so, branch
    if (zero_flag) { goto IncWorldSel; } // note this will not be run if world selection is disabled
    lda_abs(NumberOfPlayers); // if no, must have been the select button, therefore
    eor_imm(0b00000001); // change number of players and draw icon accordingly
    write_byte(NumberOfPlayers, a);
    DrawMushroomIcon();
    goto NullJoypad;
  
IncWorldSel:
    ldx_abs(WorldSelectNumber); // increment world select number
    inx();
    txa();
    and_imm(0b00000111); // mask out higher bits
    write_byte(WorldSelectNumber, a); // store as current world select number
    GoContinue();
  
UpdateShroom:
    lda_absx(WSelectBufferTemplate); // write template for world select in vram buffer
    write_byte(VRAM_Buffer1 - 1 + x, a); // do this until all bytes are written
    inx();
    cpx_imm(0x6);
    if (neg_flag) { goto UpdateShroom; }
    ldy_abs(WorldNumber); // get world number from variable and increment for
    iny(); // proper display, and put in blank byte before
    write_byte(VRAM_Buffer1 + 3, y); // null terminator
  
NullJoypad:
    lda_imm(0x0); // clear joypad bits for player 1
    write_byte(SavedJoypad1Bits, a);
  
RunDemo:
    jsr(GameCoreRoutine, 7); // run game engine
    lda_zp(GameEngineSubroutine); // check to see if we're running lose life routine
    cmp_imm(0x6);
    if (!zero_flag) { goto ExitMenu; } // if not, do not do all the resetting below
  
ResetTitle:
    lda_imm(0x0); // reset game modes, disable
    write_byte(OperMode, a); // sprite 0 check and disable
    write_byte(OperMode_Task, a); // screen output
    write_byte(Sprite0HitDetectFlag, a);
    inc_abs(DisableScreenFlag);
    goto rts;
  
ChkContinue:
    ldy_abs(DemoTimer); // if timer for demo has expired, reset modes
    if (zero_flag) { goto ResetTitle; }
    asl_acc(); // check to see if A button was also pushed
    if (!carry_flag) { goto StartWorld1; } // if not, don't load continue function's world number
    lda_abs(ContinueWorld); // load previously saved world number for secret
    GoContinue(); // continue function when pressing A + start
  
StartWorld1:
    jsr(LoadAreaPointer, 8);
    inc_abs(Hidden1UpFlag); // set 1-up box flag for both players
    inc_abs(OffScr_Hidden1UpFlag);
    inc_abs(FetchNewGameTimerFlag); // set fetch new game timer flag
    inc_abs(OperMode); // set next game mode
    lda_abs(WorldSelectEnableFlag); // if world select flag is on, then primary
    write_byte(PrimaryHardMode, a); // hard mode must be on as well
    lda_imm(0x0);
    write_byte(OperMode_Task, a); // set game mode here, and clear demo timer
    write_byte(DemoTimer, a);
    ldx_imm(0x17);
    lda_imm(0x0);
  
InitScores:
    write_byte(ScoreAndCoinDisplay + x, a); // clear player scores and coin displays
    dex();
    if (!neg_flag) { goto InitScores; }
  
ExitMenu:
    goto rts;
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
  
VictoryMode:
    jsr(VictoryModeSubroutines, 9); // run victory mode subroutines
    lda_abs(OperMode_Task); // get current task of victory mode
    if (zero_flag) { goto AutoPlayer; } // if on bridge collapse, skip enemy processing
    ldx_imm(0x0);
    write_byte(ObjectOffset, x); // otherwise reset enemy object offset 
    jsr(EnemiesAndLoopsCore, 10); // and run enemy code
  
AutoPlayer:
    jsr(RelativePlayerPosition, 11); // get player's relative coordinates
    goto PlayerGfxHandler; // draw the player, then leave
  
VictoryModeSubroutines:
    lda_abs(OperMode_Task);
    // jsr JumpEngine
    switch (a) {
      case 0: goto BridgeCollapse;
      case 1: goto SetupVictoryMode;
      case 2: goto PlayerVictoryWalk;
      case 3: goto PrintVictoryMessages;
      case 4: goto PlayerEndWorld;
    }
  
SetupVictoryMode:
    ldx_abs(ScreenRight_PageLoc); // get page location of right side of screen
    inx(); // increment to next page
    write_byte(DestinationPageLoc, x); // store here
    lda_imm(EndOfCastleMusic);
    write_byte(EventMusicQueue, a); // play win castle music
    goto IncModeTask_B; // jump to set next major task in victory mode
    // -------------------------------------------------------------------------------------
  
PlayerVictoryWalk:
    ldy_imm(0x0); // set value here to not walk player by default
    write_byte(VictoryWalkControl, y);
    lda_zp(Player_PageLoc); // get player's page location
    cmp_zp(DestinationPageLoc); // compare with destination page location
    if (!zero_flag) { goto PerformWalk; } // if page locations don't match, branch
    lda_zp(Player_X_Position); // otherwise get player's horizontal position
    cmp_imm(0x60); // compare with preset horizontal position
    if (carry_flag) { goto DontWalk; } // if still on other page, branch ahead
  
PerformWalk:
    inc_zp(VictoryWalkControl); // otherwise increment value and Y
    iny(); // note Y will be used to walk the player
  
DontWalk:
    tya(); // put contents of Y in A and
    jsr(AutoControlPlayer, 12); // use A to move player to the right or not
    lda_abs(ScreenLeft_PageLoc); // check page location of left side of screen
    cmp_zp(DestinationPageLoc); // against set value here
    if (zero_flag) { goto ExitVWalk; } // branch if equal to change modes if necessary
    lda_abs(ScrollFractional);
    carry_flag = false; // do fixed point math on fractional part of scroll
    adc_imm(0x80);
    write_byte(ScrollFractional, a); // save fractional movement amount
    lda_imm(0x1); // set 1 pixel per frame
    adc_imm(0x0); // add carry from previous addition
    tay(); // use as scroll amount
    jsr(ScrollScreen, 13); // do sub to scroll the screen
    jsr(UpdScrollVar, 14); // do another sub to update screen and scroll variables
    inc_zp(VictoryWalkControl); // increment value to stay in this routine
  
ExitVWalk:
    lda_zp(VictoryWalkControl); // load value set here
    if (zero_flag) { goto IncModeTask_A; } // if zero, branch to change modes
    goto rts; // otherwise leave
    // -------------------------------------------------------------------------------------
  
PrintVictoryMessages:
    lda_abs(SecondaryMsgCounter); // load secondary message counter
    if (!zero_flag) { goto IncMsgCounter; } // if set, branch to increment message counters
    lda_abs(PrimaryMsgCounter); // otherwise load primary message counter
    if (zero_flag) { goto ThankPlayer; } // if set to zero, branch to print first message
    cmp_imm(0x9); // if at 9 or above, branch elsewhere (this comparison
    if (carry_flag) { goto IncMsgCounter; } // is residual code, counter never reaches 9)
    ldy_abs(WorldNumber); // check world number
    cpy_imm(World8);
    if (!zero_flag) { goto MRetainerMsg; } // if not at world 8, skip to next part
    cmp_imm(0x3); // check primary message counter again
    if (!carry_flag) { goto IncMsgCounter; } // if not at 3 yet (world 8 only), branch to increment
    sbc_imm(0x1); // otherwise subtract one
    goto ThankPlayer; // and skip to next part
  
MRetainerMsg:
    cmp_imm(0x2); // check primary message counter
    if (!carry_flag) { goto IncMsgCounter; } // if not at 2 yet (world 1-7 only), branch
  
ThankPlayer:
    tay(); // put primary message counter into Y
    if (!zero_flag) { goto SecondPartMsg; } // if counter nonzero, skip this part, do not print first message
    lda_abs(CurrentPlayer); // otherwise get player currently on the screen
    if (zero_flag) { goto EvalForMusic; } // if mario, branch
    iny(); // otherwise increment Y once for luigi and
    if (!zero_flag) { goto EvalForMusic; } // do an unconditional branch to the same place
  
SecondPartMsg:
    iny(); // increment Y to do world 8's message
    lda_abs(WorldNumber);
    cmp_imm(World8); // check world number
    if (zero_flag) { goto EvalForMusic; } // if at world 8, branch to next part
    dey(); // otherwise decrement Y for world 1-7's message
    cpy_imm(0x4); // if counter at 4 (world 1-7 only)
    if (carry_flag) { goto SetEndTimer; } // branch to set victory end timer
    cpy_imm(0x3); // if counter at 3 (world 1-7 only)
    if (carry_flag) { goto IncMsgCounter; } // branch to keep counting
  
EvalForMusic:
    cpy_imm(0x3); // if counter not yet at 3 (world 8 only), branch
    if (!zero_flag) { goto PrintMsg; } // to print message only (note world 1-7 will only
    lda_imm(VictoryMusic); // reach this code if counter = 0, and will always branch)
    write_byte(EventMusicQueue, a); // otherwise load victory music first (world 8 only)
  
PrintMsg:
    tya(); // put primary message counter in A
    carry_flag = false; // add $0c or 12 to counter thus giving an appropriate value,
    adc_imm(0xc); // ($0c-$0d = first), ($0e = world 1-7's), ($0f-$12 = world 8's)
    write_byte(VRAM_Buffer_AddrCtrl, a); // write message counter to vram address controller
  
IncMsgCounter:
    lda_abs(SecondaryMsgCounter);
    carry_flag = false;
    adc_imm(0x4); // add four to secondary message counter
    write_byte(SecondaryMsgCounter, a);
    lda_abs(PrimaryMsgCounter);
    adc_imm(0x0); // add carry to primary message counter
    write_byte(PrimaryMsgCounter, a);
    cmp_imm(0x7); // check primary counter one more time
  
SetEndTimer:
    if (!carry_flag) { goto ExitMsgs; } // if not reached value yet, branch to leave
    lda_imm(0x6);
    write_byte(WorldEndTimer, a); // otherwise set world end timer
  
IncModeTask_A:
    inc_abs(OperMode_Task); // move onto next task in mode
  
ExitMsgs:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
  
PlayerEndWorld:
    lda_abs(WorldEndTimer); // check to see if world end timer expired
    if (!zero_flag) { goto EndExitOne; } // branch to leave if not
    ldy_abs(WorldNumber); // check world number
    cpy_imm(World8); // if on world 8, player is done with game, 
    if (carry_flag) { goto EndChkBButton; } // thus branch to read controller
    lda_imm(0x0);
    write_byte(AreaNumber, a); // otherwise initialize area number used as offset
    write_byte(LevelNumber, a); // and level number control to start at area 1
    write_byte(OperMode_Task, a); // initialize secondary mode of operation
    inc_abs(WorldNumber); // increment world number to move onto the next world
    jsr(LoadAreaPointer, 15); // get area address offset for the next area
    inc_abs(FetchNewGameTimerFlag); // set flag to load game timer from header
    lda_imm(GameModeValue);
    write_byte(OperMode, a); // set mode of operation to game mode
  
EndExitOne:
    goto rts; // and leave
  
EndChkBButton:
    lda_abs(SavedJoypad1Bits);
    ora_abs(SavedJoypad2Bits); // check to see if B button was pressed on
    and_imm(B_Button); // either controller
    if (zero_flag) { goto EndExitTwo; } // branch to leave if not
    lda_imm(0x1); // otherwise set world selection flag
    write_byte(WorldSelectEnableFlag, a);
    lda_imm(0xff); // remove onscreen player's lives
    write_byte(NumberofLives, a);
    jsr(TerminateGame, 16); // do sub to continue other player or end game
  
EndExitTwo:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // data is used as tiles for numbers
    // that appear when you defeat enemies
  
FloateyNumbersRoutine:
    lda_absx(FloateyNum_Control); // load control for floatey number
    if (zero_flag) { goto EndExitOne; } // if zero, branch to leave
    cmp_imm(0xb); // if less than $0b, branch
    if (!carry_flag) { goto ChkNumTimer; }
    lda_imm(0xb); // otherwise set to $0b, thus keeping
    write_byte(FloateyNum_Control + x, a); // it in range
  
ChkNumTimer:
    tay(); // use as Y
    lda_absx(FloateyNum_Timer); // check value here
    if (!zero_flag) { goto DecNumTimer; } // if nonzero, branch ahead
    write_byte(FloateyNum_Control + x, a); // initialize floatey number control and leave
    goto rts;
  
DecNumTimer:
    dec_absx(FloateyNum_Timer); // decrement value here
    cmp_imm(0x2b); // if not reached a certain point, branch  
    if (!zero_flag) { goto ChkTallEnemy; }
    cpy_imm(0xb); // check offset for $0b
    if (!zero_flag) { goto LoadNumTiles; } // branch ahead if not found
    inc_abs(NumberofLives); // give player one extra life (1-up)
    lda_imm(Sfx_ExtraLife);
    write_byte(Square2SoundQueue, a); // and play the 1-up sound
  
LoadNumTiles:
    lda_absy(ScoreUpdateData); // load point value here
    lsr_acc(); // move high nybble to low
    lsr_acc();
    lsr_acc();
    lsr_acc();
    tax(); // use as X offset, essentially the digit
    lda_absy(ScoreUpdateData); // load again and this time
    and_imm(0b00001111); // mask out the high nybble
    write_byte(DigitModifier + x, a); // store as amount to add to the digit
    jsr(AddToScore, 17); // update the score accordingly
  
ChkTallEnemy:
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset for enemy object
    lda_zpx(Enemy_ID); // get enemy object identifier
    cmp_imm(Spiny);
    if (zero_flag) { goto FloateyPart; } // branch if spiny
    cmp_imm(PiranhaPlant);
    if (zero_flag) { goto FloateyPart; } // branch if piranha plant
    cmp_imm(HammerBro);
    if (zero_flag) { goto GetAltOffset; } // branch elsewhere if hammer bro
    cmp_imm(GreyCheepCheep);
    if (zero_flag) { goto FloateyPart; } // branch if cheep-cheep of either color
    cmp_imm(RedCheepCheep);
    if (zero_flag) { goto FloateyPart; }
    cmp_imm(TallEnemy);
    if (carry_flag) { goto GetAltOffset; } // branch elsewhere if enemy object => $09
    lda_zpx(Enemy_State);
    cmp_imm(0x2); // if enemy state defeated or otherwise
    if (carry_flag) { goto FloateyPart; } // $02 or greater, branch beyond this part
  
GetAltOffset:
    ldx_abs(SprDataOffset_Ctrl); // load some kind of control bit
    ldy_absx(Alt_SprDataOffset); // get alternate OAM data offset
    ldx_zp(ObjectOffset); // get enemy object offset again
  
FloateyPart:
    lda_absx(FloateyNum_Y_Pos); // get vertical coordinate for
    cmp_imm(0x18); // floatey number, if coordinate in the
    if (!carry_flag) { goto SetupNumSpr; } // status bar, branch
    sbc_imm(0x1);
    write_byte(FloateyNum_Y_Pos + x, a); // otherwise subtract one and store as new
  
SetupNumSpr:
    lda_absx(FloateyNum_Y_Pos); // get vertical coordinate
    sbc_imm(0x8); // subtract eight and dump into the
    jsr(DumpTwoSpr, 18); // left and right sprite's Y coordinates
    lda_absx(FloateyNum_X_Pos); // get horizontal coordinate
    write_byte(Sprite_X_Position + y, a); // store into X coordinate of left sprite
    carry_flag = false;
    adc_imm(0x8); // add eight pixels and store into X
    write_byte(Sprite_X_Position + 4 + y, a); // coordinate of right sprite
    lda_imm(0x2);
    write_byte(Sprite_Attributes + y, a); // set palette control in attribute bytes
    write_byte(Sprite_Attributes + 4 + y, a); // of left and right sprites
    lda_absx(FloateyNum_Control);
    asl_acc(); // multiply our floatey number control by 2
    tax(); // and use as offset for look-up table
    lda_absx(FloateyNumTileData);
    write_byte(Sprite_Tilenumber + y, a); // display first half of number of points
    lda_absx(FloateyNumTileData + 1);
    write_byte(Sprite_Tilenumber + 4 + y, a); // display the second half
    ldx_zp(ObjectOffset); // get enemy object offset and leave
    goto rts;
    // -------------------------------------------------------------------------------------
  
ScreenRoutines:
    lda_abs(ScreenRoutineTask); // run one of the following subroutines
    // jsr JumpEngine
    switch (a) {
      case 0: goto InitScreen;
      case 1: goto SetupIntermediate;
      case 2: goto WriteTopStatusLine;
      case 3: goto WriteBottomStatusLine;
      case 4: goto DisplayTimeUp;
      case 5: goto ResetSpritesAndScreenTimer;
      case 6: goto DisplayIntermediate;
      case 7: goto ResetSpritesAndScreenTimer;
      case 8: goto AreaParserTaskControl;
      case 9: goto GetAreaPalette;
      case 10: goto GetBackgroundColor;
      case 11: goto GetAlternatePalette1;
      case 12: goto DrawTitleScreen;
      case 13: goto ClearBuffersDrawIcon;
      case 14: goto WriteTopScore;
    }
  
InitScreen:
    jsr(MoveAllSpritesOffscreen, 19); // initialize all sprites including sprite #0
    InitializeNameTables(); // and erase both name and attribute tables
    lda_abs(OperMode);
    if (zero_flag) { goto NextSubtask; } // if mode still 0, do not load
    ldx_imm(0x3); // into buffer pointer
    goto SetVRAMAddr_A;
    // -------------------------------------------------------------------------------------
  
SetupIntermediate:
    lda_abs(BackgroundColorCtrl); // save current background color control
    pha(); // and player status to stack
    lda_abs(PlayerStatus);
    pha();
    lda_imm(0x0); // set background color to black
    write_byte(PlayerStatus, a); // and player status to not fiery
    lda_imm(0x2); // this is the ONLY time background color control
    write_byte(BackgroundColorCtrl, a); // is set to less than 4
    jsr(GetPlayerColors, 20);
    pla(); // we only execute this routine for
    write_byte(PlayerStatus, a); // the intermediate lives display
    pla(); // and once we're done, we return bg
    write_byte(BackgroundColorCtrl, a); // color ctrl and player status from stack
    goto IncSubtask; // then move onto the next task
    // -------------------------------------------------------------------------------------
  
GetAreaPalette:
    ldy_abs(AreaType); // select appropriate palette to load
    ldx_absy(AreaPalette); // based on area type
  
SetVRAMAddr_A:
    write_byte(VRAM_Buffer_AddrCtrl, x); // store offset into buffer control
  
NextSubtask:
    goto IncSubtask; // move onto next task
    // -------------------------------------------------------------------------------------
    // $00 - used as temp counter in GetPlayerColors
  
GetBackgroundColor:
    ldy_abs(BackgroundColorCtrl); // check background color control
    if (zero_flag) { goto NoBGColor; } // if not set, increment task and fetch palette
    lda_absy(BGColorCtrl_Addr - 4); // put appropriate palette into vram
    write_byte(VRAM_Buffer_AddrCtrl, a); // note that if set to 5-7, $0301 will not be read
  
NoBGColor:
    inc_abs(ScreenRoutineTask); // increment to next subtask and plod on through
  
GetPlayerColors:
    ldx_abs(VRAM_Buffer1_Offset); // get current buffer offset
    ldy_imm(0x0);
    lda_abs(CurrentPlayer); // check which player is on the screen
    if (zero_flag) { goto ChkFiery; }
    ldy_imm(0x4); // load offset for luigi
  
ChkFiery:
    lda_abs(PlayerStatus); // check player status
    cmp_imm(0x2);
    if (!zero_flag) { goto StartClrGet; } // if fiery, load alternate offset for fiery player
    ldy_imm(0x8);
  
StartClrGet:
    lda_imm(0x3); // do four colors
    write_byte(0x0, a);
  
ClrGetLoop:
    lda_absy(PlayerColors); // fetch player colors and store them
    write_byte(VRAM_Buffer1 + 3 + x, a); // in the buffer
    iny();
    inx();
    dec_zp(0x0);
    if (!neg_flag) { goto ClrGetLoop; }
    ldx_abs(VRAM_Buffer1_Offset); // load original offset from before
    ldy_abs(BackgroundColorCtrl); // if this value is four or greater, it will be set
    if (!zero_flag) { goto SetBGColor; } // therefore use it as offset to background color
    ldy_abs(AreaType); // otherwise use area type bits from area offset as offset
  
SetBGColor:
    lda_absy(BackgroundColors); // to background color instead
    write_byte(VRAM_Buffer1 + 3 + x, a);
    lda_imm(0x3f); // set for sprite palette address
    write_byte(VRAM_Buffer1 + x, a); // save to buffer
    lda_imm(0x10);
    write_byte(VRAM_Buffer1 + 1 + x, a);
    lda_imm(0x4); // write length byte to buffer
    write_byte(VRAM_Buffer1 + 2 + x, a);
    lda_imm(0x0); // now the null terminator
    write_byte(VRAM_Buffer1 + 7 + x, a);
    txa(); // move the buffer pointer ahead 7 bytes
    carry_flag = false; // in case we want to write anything else later
    adc_imm(0x7);
  
SetVRAMOffset:
    write_byte(VRAM_Buffer1_Offset, a); // store as new vram buffer offset
    goto rts;
    // -------------------------------------------------------------------------------------
  
GetAlternatePalette1:
    lda_abs(AreaStyle); // check for mushroom level style
    cmp_imm(0x1);
    if (!zero_flag) { goto NoAltPal; }
    lda_imm(0xb); // if found, load appropriate palette
  
SetVRAMAddr_B:
    write_byte(VRAM_Buffer_AddrCtrl, a);
  
NoAltPal:
    goto IncSubtask; // now onto the next task
    // -------------------------------------------------------------------------------------
  
WriteTopStatusLine:
    lda_imm(0x0); // select main status bar
    jsr(WriteGameText, 21); // output it
    goto IncSubtask; // onto the next task
    // -------------------------------------------------------------------------------------
  
WriteBottomStatusLine:
    jsr(GetSBNybbles, 22); // write player's score and coin tally to screen
    ldx_abs(VRAM_Buffer1_Offset);
    lda_imm(0x20); // write address for world-area number on screen
    write_byte(VRAM_Buffer1 + x, a);
    lda_imm(0x73);
    write_byte(VRAM_Buffer1 + 1 + x, a);
    lda_imm(0x3); // write length for it
    write_byte(VRAM_Buffer1 + 2 + x, a);
    ldy_abs(WorldNumber); // first the world number
    iny();
    tya();
    write_byte(VRAM_Buffer1 + 3 + x, a);
    lda_imm(0x28); // next the dash
    write_byte(VRAM_Buffer1 + 4 + x, a);
    ldy_abs(LevelNumber); // next the level number
    iny(); // increment for proper number display
    tya();
    write_byte(VRAM_Buffer1 + 5 + x, a);
    lda_imm(0x0); // put null terminator on
    write_byte(VRAM_Buffer1 + 6 + x, a);
    txa(); // move the buffer offset up by 6 bytes
    carry_flag = false;
    adc_imm(0x6);
    write_byte(VRAM_Buffer1_Offset, a);
    goto IncSubtask;
    // -------------------------------------------------------------------------------------
  
DisplayTimeUp:
    lda_abs(GameTimerExpiredFlag); // if game timer not expired, increment task
    if (zero_flag) { goto NoTimeUp; } // control 2 tasks forward, otherwise, stay here
    lda_imm(0x0);
    write_byte(GameTimerExpiredFlag, a); // reset timer expiration flag
    lda_imm(0x2); // output time-up screen to buffer
    goto OutputInter;
  
NoTimeUp:
    inc_abs(ScreenRoutineTask); // increment control task 2 tasks forward
    goto IncSubtask;
    // -------------------------------------------------------------------------------------
  
DisplayIntermediate:
    lda_abs(OperMode); // check primary mode of operation
    if (zero_flag) { goto NoInter; } // if in title screen mode, skip this
    cmp_imm(GameOverModeValue); // are we in game over mode?
    if (zero_flag) { goto GameOverInter; } // if so, proceed to display game over screen
    lda_abs(AltEntranceControl); // otherwise check for mode of alternate entry
    if (!zero_flag) { goto NoInter; } // and branch if found
    ldy_abs(AreaType); // check if we are on castle level
    cpy_imm(0x3); // and if so, branch (possibly residual)
    if (zero_flag) { goto PlayerInter; }
    lda_abs(DisableIntermediate); // if this flag is set, skip intermediate lives display
    if (!zero_flag) { goto NoInter; } // and jump to specific task, otherwise
  
PlayerInter:
    jsr(DrawPlayer_Intermediate, 23); // put player in appropriate place for
    lda_imm(0x1); // lives display, then output lives display to buffer
  
OutputInter:
    jsr(WriteGameText, 24);
    jsr(ResetScreenTimer, 25);
    lda_imm(0x0);
    write_byte(DisableScreenFlag, a); // reenable screen output
    goto rts;
  
GameOverInter:
    lda_imm(0x12); // set screen timer
    write_byte(ScreenTimer, a);
    lda_imm(0x3); // output game over screen to buffer
    jsr(WriteGameText, 26);
    goto IncModeTask_B;
  
NoInter:
    lda_imm(0x8); // set for specific task and leave
    write_byte(ScreenRoutineTask, a);
    goto rts;
    // -------------------------------------------------------------------------------------
  
AreaParserTaskControl:
    inc_abs(DisableScreenFlag); // turn off screen
  
TaskLoop:
    jsr(AreaParserTaskHandler, 27); // render column set of current area
    lda_abs(AreaParserTaskNum); // check number of tasks
    if (!zero_flag) { goto TaskLoop; } // if tasks still not all done, do another one
    dec_abs(ColumnSets); // do we need to render more column sets?
    if (!neg_flag) { goto OutputCol; }
    inc_abs(ScreenRoutineTask); // if not, move on to the next task
  
OutputCol:
    lda_imm(0x6); // set vram buffer to output rendered column set
    write_byte(VRAM_Buffer_AddrCtrl, a); // on next NMI
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - vram buffer address table low
    // $01 - vram buffer address table high
  
DrawTitleScreen:
    lda_abs(OperMode); // are we in title screen mode?
    if (!zero_flag) { goto IncModeTask_B; } // if not, exit
    lda_imm(HIGH_BYTE(TitleScreenDataOffset)); // load address $1ec0 into
    write_byte(PPU_ADDRESS, a); // the vram address register
    lda_imm(LOW_BYTE(TitleScreenDataOffset));
    write_byte(PPU_ADDRESS, a);
    lda_imm(0x3); // put address $0300 into
    write_byte(0x1, a); // the indirect at $00
    ldy_imm(0x0);
    write_byte(0x0, y);
    lda_abs(PPU_DATA); // do one garbage read
  
OutputTScr:
    lda_abs(PPU_DATA); // get title screen from chr-rom
    write_byte(read_word(0x0) + y, a); // store 256 bytes into buffer
    iny();
    if (!zero_flag) { goto ChkHiByte; } // if not past 256 bytes, do not increment
    inc_zp(0x1); // otherwise increment high byte of indirect
  
ChkHiByte:
    lda_zp(0x1); // check high byte?
    cmp_imm(0x4); // at $0400?
    if (!zero_flag) { goto OutputTScr; } // if not, loop back and do another
    cpy_imm(0x3a); // check if offset points past end of data
    if (!carry_flag) { goto OutputTScr; } // if not, loop back and do another
    lda_imm(0x5); // set buffer transfer control to $0300,
    goto SetVRAMAddr_B; // increment task and exit
    // -------------------------------------------------------------------------------------
  
ClearBuffersDrawIcon:
    lda_abs(OperMode); // check game mode
    if (!zero_flag) { goto IncModeTask_B; } // if not title screen mode, leave
    ldx_imm(0x0); // otherwise, clear buffer space
  
TScrClear:
    write_byte(VRAM_Buffer1 - 1 + x, a);
    write_byte(VRAM_Buffer1 - 1 + 0x100 + x, a);
    dex();
    if (!zero_flag) { goto TScrClear; }
    DrawMushroomIcon(); // draw player select icon
  
IncSubtask:
    inc_abs(ScreenRoutineTask); // move onto next task
    goto rts;
    // -------------------------------------------------------------------------------------
  
WriteTopScore:
    lda_imm(0xfa); // run display routine to display top score on title
    jsr(UpdateNumber, 28);
  
IncModeTask_B:
    inc_abs(OperMode_Task); // move onto next mode
    goto rts;
    // -------------------------------------------------------------------------------------
  
WriteGameText:
    pha(); // save text number to stack
    asl_acc();
    tay(); // multiply by 2 and use as offset
    cpy_imm(0x4); // if set to do top status bar or world/lives display,
    if (!carry_flag) { goto LdGameText; } // branch to use current offset as-is
    cpy_imm(0x8); // if set to do time-up or game over,
    if (!carry_flag) { goto Chk2Players; } // branch to check players
    ldy_imm(0x8); // otherwise warp zone, therefore set offset
  
Chk2Players:
    lda_abs(NumberOfPlayers); // check for number of players
    if (!zero_flag) { goto LdGameText; } // if there are two, use current offset to also print name
    iny(); // otherwise increment offset by one to not print name
  
LdGameText:
    ldx_absy(GameTextOffsets); // get offset to message we want to print
    ldy_imm(0x0);
  
GameTextLoop:
    lda_absx(GameText); // load message data
    cmp_imm(0xff); // check for terminator
    if (zero_flag) { goto EndGameText; } // branch to end text if found
    write_byte(VRAM_Buffer1 + y, a); // otherwise write data to buffer
    inx(); // and increment increment
    iny();
    if (!zero_flag) { goto GameTextLoop; } // do this for 256 bytes if no terminator found
  
EndGameText:
    lda_imm(0x0); // put null terminator at end
    write_byte(VRAM_Buffer1 + y, a);
    pla(); // pull original text number from stack
    tax();
    cmp_imm(0x4); // are we printing warp zone?
    if (carry_flag) { goto PrintWarpZoneNumbers; }
    dex(); // are we printing the world/lives display?
    if (!zero_flag) { goto CheckPlayerName; } // if not, branch to check player's name
    lda_abs(NumberofLives); // otherwise, check number of lives
    carry_flag = false; // and increment by one for display
    adc_imm(0x1);
    cmp_imm(10); // more than 9 lives?
    if (!carry_flag) { goto PutLives; }
    sbc_imm(10); // if so, subtract 10 and put a crown tile
    ldy_imm(0x9f); // next to the difference...strange things happen if
    write_byte(VRAM_Buffer1 + 7, y); // the number of lives exceeds 19
  
PutLives:
    write_byte(VRAM_Buffer1 + 8, a);
    ldy_abs(WorldNumber); // write world and level numbers (incremented for display)
    iny(); // to the buffer in the spaces surrounding the dash
    write_byte(VRAM_Buffer1 + 19, y);
    ldy_abs(LevelNumber);
    iny();
    write_byte(VRAM_Buffer1 + 21, y); // we're done here
    goto rts;
  
CheckPlayerName:
    lda_abs(NumberOfPlayers); // check number of players
    if (zero_flag) { goto ExitChkName; } // if only 1 player, leave
    lda_abs(CurrentPlayer); // load current player
    dex(); // check to see if current message number is for time up
    if (!zero_flag) { goto ChkLuigi; }
    ldy_abs(OperMode); // check for game over mode
    cpy_imm(GameOverModeValue);
    if (zero_flag) { goto ChkLuigi; }
    eor_imm(0b00000001); // if not, must be time up, invert d0 to do other player
  
ChkLuigi:
    lsr_acc();
    if (!carry_flag) { goto ExitChkName; } // if mario is current player, do not change the name
    ldy_imm(0x4);
  
NameLoop:
    lda_absy(LuigiName); // otherwise, replace "MARIO" with "LUIGI"
    write_byte(VRAM_Buffer1 + 3 + y, a);
    dey();
    if (!neg_flag) { goto NameLoop; } // do this until each letter is replaced
  
ExitChkName:
    goto rts;
  
PrintWarpZoneNumbers:
    sbc_imm(0x4); // subtract 4 and then shift to the left
    asl_acc(); // twice to get proper warp zone number
    asl_acc(); // offset
    tax();
    ldy_imm(0x0);
  
WarpNumLoop:
    lda_absx(WarpZoneNumbers); // print warp zone numbers into the
    write_byte(VRAM_Buffer1 + 27 + y, a); // placeholders from earlier
    inx();
    iny(); // put a number in every fourth space
    iny();
    iny();
    iny();
    cpy_imm(0xc);
    if (!carry_flag) { goto WarpNumLoop; }
    lda_imm(0x2c); // load new buffer pointer at end of message
    goto SetVRAMOffset;
    // -------------------------------------------------------------------------------------
  
ResetSpritesAndScreenTimer:
    lda_abs(ScreenTimer); // check if screen timer has expired
    if (!zero_flag) { goto NoReset; } // if not, branch to leave
    jsr(MoveAllSpritesOffscreen, 29); // otherwise reset sprites now
  
ResetScreenTimer:
    lda_imm(0x7); // reset timer again
    write_byte(ScreenTimer, a);
    inc_abs(ScreenRoutineTask); // move onto next task
  
NoReset:
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - temp vram buffer offset
    // $01 - temp metatile buffer offset
    // $02 - temp metatile graphics table offset
    // $03 - used to store attribute bits
    // $04 - used to determine attribute table row
    // $05 - used to determine attribute table column
    // $06 - metatile graphics table address low
    // $07 - metatile graphics table address high
  
RenderAreaGraphics:
    lda_abs(CurrentColumnPos); // store LSB of where we're at
    and_imm(0x1);
    write_byte(0x5, a);
    ldy_abs(VRAM_Buffer2_Offset); // store vram buffer offset
    write_byte(0x0, y);
    lda_abs(CurrentNTAddr_Low); // get current name table address we're supposed to render
    write_byte(VRAM_Buffer2 + 1 + y, a);
    lda_abs(CurrentNTAddr_High);
    write_byte(VRAM_Buffer2 + y, a);
    lda_imm(0x9a); // store length byte of 26 here with d7 set
    write_byte(VRAM_Buffer2 + 2 + y, a); // to increment by 32 (in columns)
    lda_imm(0x0); // init attribute row
    write_byte(0x4, a);
    tax();
  
DrawMTLoop:
    write_byte(0x1, x); // store init value of 0 or incremented offset for buffer
    lda_absx(MetatileBuffer); // get first metatile number, and mask out all but 2 MSB
    and_imm(0b11000000);
    write_byte(0x3, a); // store attribute table bits here
    asl_acc(); // note that metatile format is:
    rol_acc(); // %xx000000 - attribute table bits, 
    rol_acc(); // %00xxxxxx - metatile number
    tay(); // rotate bits to d1-d0 and use as offset here
    lda_absy(MetatileGraphics_Low); // get address to graphics table from here
    write_byte(0x6, a);
    lda_absy(MetatileGraphics_High);
    write_byte(0x7, a);
    lda_absx(MetatileBuffer); // get metatile number again
    asl_acc(); // multiply by 4 and use as tile offset
    asl_acc();
    write_byte(0x2, a);
    lda_abs(AreaParserTaskNum); // get current task number for level processing and
    and_imm(0b00000001); // mask out all but LSB, then invert LSB, multiply by 2
    eor_imm(0b00000001); // to get the correct column position in the metatile,
    asl_acc(); // then add to the tile offset so we can draw either side
    adc_zp(0x2); // of the metatiles
    tay();
    ldx_zp(0x0); // use vram buffer offset from before as X
    lda_indy(0x6);
    write_byte(VRAM_Buffer2 + 3 + x, a); // get first tile number (top left or top right) and store
    iny();
    lda_indy(0x6); // now get the second (bottom left or bottom right) and store
    write_byte(VRAM_Buffer2 + 4 + x, a);
    ldy_zp(0x4); // get current attribute row
    lda_zp(0x5); // get LSB of current column where we're at, and
    if (!zero_flag) { goto RightCheck; } // branch if set (clear = left attrib, set = right)
    lda_zp(0x1); // get current row we're rendering
    lsr_acc(); // branch if LSB set (clear = top left, set = bottom left)
    if (carry_flag) { goto LLeft; }
    rol_zp(0x3); // rotate attribute bits 3 to the left
    rol_zp(0x3); // thus in d1-d0, for upper left square
    rol_zp(0x3);
    goto SetAttrib;
  
RightCheck:
    lda_zp(0x1); // get LSB of current row we're rendering
    lsr_acc(); // branch if set (clear = top right, set = bottom right)
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
    write_byte(AttributeBuffer + y, a); // the old, and store
    inc_zp(0x0); // increment vram buffer offset by 2
    inc_zp(0x0);
    ldx_zp(0x1); // get current gfx buffer row, and check for
    inx(); // the bottom of the screen
    cpx_imm(0xd);
    if (!carry_flag) { goto DrawMTLoop; } // if not there yet, loop back
    ldy_zp(0x0); // get current vram buffer offset, increment by 3
    iny(); // (for name table address and length bytes)
    iny();
    iny();
    lda_imm(0x0);
    write_byte(VRAM_Buffer2 + y, a); // put null terminator at end of data for name table
    write_byte(VRAM_Buffer2_Offset, y); // store new buffer offset
    inc_abs(CurrentNTAddr_Low); // increment name table address low
    lda_abs(CurrentNTAddr_Low); // check current low byte
    and_imm(0b00011111); // if no wraparound, just skip this part
    if (!zero_flag) { goto ExitDrawM; }
    lda_imm(0x80); // if wraparound occurs, make sure low byte stays
    write_byte(CurrentNTAddr_Low, a); // just under the status bar
    lda_abs(CurrentNTAddr_High); // and then invert d2 of the name table address high
    eor_imm(0b00000100); // to move onto the next appropriate name table
    write_byte(CurrentNTAddr_High, a);
  
ExitDrawM:
    goto SetVRAMCtrl; // jump to set buffer to $0341 and leave
    // -------------------------------------------------------------------------------------
    // $00 - temp attribute table address high (big endian order this time!)
    // $01 - temp attribute table address low
  
RenderAttributeTables:
    lda_abs(CurrentNTAddr_Low); // get low byte of next name table address
    and_imm(0b00011111); // to be written to, mask out all but 5 LSB,
    carry_flag = true; // subtract four 
    sbc_imm(0x4);
    and_imm(0b00011111); // mask out bits again and store
    write_byte(0x1, a);
    lda_abs(CurrentNTAddr_High); // get high byte and branch if borrow not set
    if (carry_flag) { goto SetATHigh; }
    eor_imm(0b00000100); // otherwise invert d2
  
SetATHigh:
    and_imm(0b00000100); // mask out all other bits
    ora_imm(0x23); // add $2300 to the high byte and store
    write_byte(0x0, a);
    lda_zp(0x1); // get low byte - 4, divide by 4, add offset for
    lsr_acc(); // attribute table and store
    lsr_acc();
    adc_imm(0xc0); // we should now have the appropriate block of
    write_byte(0x1, a); // attribute table in our temp address
    ldx_imm(0x0);
    ldy_abs(VRAM_Buffer2_Offset); // get buffer offset
  
AttribLoop:
    lda_zp(0x0);
    write_byte(VRAM_Buffer2 + y, a); // store high byte of attribute table address
    lda_zp(0x1);
    carry_flag = false; // get low byte, add 8 because we want to start
    adc_imm(0x8); // below the status bar, and store
    write_byte(VRAM_Buffer2 + 1 + y, a);
    write_byte(0x1, a); // also store in temp again
    lda_absx(AttributeBuffer); // fetch current attribute table byte and store
    write_byte(VRAM_Buffer2 + 3 + y, a); // in the buffer
    lda_imm(0x1);
    write_byte(VRAM_Buffer2 + 2 + y, a); // store length of 1 in buffer
    lsr_acc();
    write_byte(AttributeBuffer + x, a); // clear current byte in attribute buffer
    iny(); // increment buffer offset by 4 bytes
    iny();
    iny();
    iny();
    inx(); // increment attribute offset and check to see
    cpx_imm(0x7); // if we're at the end yet
    if (!carry_flag) { goto AttribLoop; }
    write_byte(VRAM_Buffer2 + y, a); // put null terminator at the end
    write_byte(VRAM_Buffer2_Offset, y); // store offset in case we want to do any more
  
SetVRAMCtrl:
    lda_imm(0x6);
    write_byte(VRAM_Buffer_AddrCtrl, a); // set buffer to $0341 and leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used as temporary counter in ColorRotation
    // -------------------------------------------------------------------------------------
    // $00 - temp store for offset control bit
    // $01 - temp vram buffer offset
    // $02 - temp store for vertical high nybble in block buffer routine
    // $03 - temp adder for high byte of name table address
    // $04, $05 - name table address low/high
    // $06, $07 - block buffer address low/high
  
RemoveCoin_Axe:
    ldy_imm(0x41); // set low byte so offset points to $0341
    lda_imm(0x3); // load offset for default blank metatile
    ldx_abs(AreaType); // check area type
    if (!zero_flag) { goto WriteBlankMT; } // if not water type, use offset
    lda_imm(0x4); // otherwise load offset for blank metatile used in water
  
WriteBlankMT:
    jsr(PutBlockMetatile, 30); // do a sub to write blank metatile to vram buffer
    lda_imm(0x6);
    write_byte(VRAM_Buffer_AddrCtrl, a); // set vram address controller to $0341 and leave
    goto rts;
  
ReplaceBlockMetatile:
    jsr(WriteBlockMetatile, 31); // write metatile to vram buffer to replace block object
    inc_abs(Block_ResidualCounter); // increment unused counter (residual code)
    dec_absx(Block_RepFlag); // decrement flag (residual code)
    goto rts; // leave
  
DestroyBlockMetatile:
    lda_imm(0x0); // force blank metatile if branched/jumped to this point
  
WriteBlockMetatile:
    ldy_imm(0x3); // load offset for blank metatile
    cmp_imm(0x0); // check contents of A for blank metatile
    if (zero_flag) { goto UseBOffset; } // branch if found (unconditional if branched from 8a6b)
    ldy_imm(0x0); // load offset for brick metatile w/ line
    cmp_imm(0x58);
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is brick with coins (w/ line)
    cmp_imm(0x51);
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is breakable brick w/ line
    iny(); // increment offset for brick metatile w/o line
    cmp_imm(0x5d);
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is brick with coins (w/o line)
    cmp_imm(0x52);
    if (zero_flag) { goto UseBOffset; } // use offset if metatile is breakable brick w/o line
    iny(); // if any other metatile, increment offset for empty block
  
UseBOffset:
    tya(); // put Y in A
    ldy_abs(VRAM_Buffer1_Offset); // get vram buffer offset
    iny(); // move onto next byte
    jsr(PutBlockMetatile, 32); // get appropriate block data and write to vram buffer
  
MoveVOffset:
    dey(); // decrement vram buffer offset
    tya(); // add 10 bytes to it
    carry_flag = false;
    adc_imm(10);
    goto SetVRAMOffset; // branch to store as new vram buffer offset
  
PutBlockMetatile:
    write_byte(0x0, x); // store control bit from SprDataOffset_Ctrl
    write_byte(0x1, y); // store vram buffer offset for next byte
    asl_acc();
    asl_acc(); // multiply A by four and use as X
    tax();
    ldy_imm(0x20); // load high byte for name table 0
    lda_zp(0x6); // get low byte of block buffer pointer
    cmp_imm(0xd0); // check to see if we're on odd-page block buffer
    if (!carry_flag) { goto SaveHAdder; } // if not, use current high byte
    ldy_imm(0x24); // otherwise load high byte for name table 1
  
SaveHAdder:
    write_byte(0x3, y); // save high byte here
    and_imm(0xf); // mask out high nybble of block buffer pointer
    asl_acc(); // multiply by 2 to get appropriate name table low byte
    write_byte(0x4, a); // and then store it here
    lda_imm(0x0);
    write_byte(0x5, a); // initialize temp high byte
    lda_zp(0x2); // get vertical high nybble offset used in block buffer routine
    carry_flag = false;
    adc_imm(0x20); // add 32 pixels for the status bar
    asl_acc();
    rol_zp(0x5); // shift and rotate d7 onto d0 and d6 into carry
    asl_acc();
    rol_zp(0x5); // shift and rotate d6 onto d0 and d5 into carry
    adc_zp(0x4); // add low byte of name table and carry to vertical high nybble
    write_byte(0x4, a); // and store here
    lda_zp(0x5); // get whatever was in d7 and d6 of vertical high nybble
    adc_imm(0x0); // add carry
    carry_flag = false;
    adc_zp(0x3); // then add high byte of name table
    write_byte(0x5, a); // store here
    ldy_zp(0x1); // get vram buffer offset to be used
    RemBridge(); goto rts; // <fallthrough>
    // -------------------------------------------------------------------------------------
    // $00 - temp joypad bit
  
ReadJoypads:
    lda_imm(0x1); // reset and clear strobe of joypad ports
    write_byte(JOYPAD_PORT, a);
    lsr_acc();
    tax(); // start with joypad 1's port
    write_byte(JOYPAD_PORT, a);
    jsr(ReadPortBits, 33);
    inx(); // increment for joypad 2's port
  
ReadPortBits:
    ldy_imm(0x8);
  
PortLoop:
    pha(); // push previous bit onto stack
    lda_absx(JOYPAD_PORT); // read current bit on joypad port
    write_byte(0x0, a); // check d1 and d0 of port output
    lsr_acc(); // this is necessary on the old
    ora_zp(0x0); // famicom systems in japan
    lsr_acc();
    pla(); // read bits from stack
    rol_acc(); // rotate bit from carry flag
    dey();
    if (!zero_flag) { goto PortLoop; } // count down bits left
    write_byte(SavedJoypadBits + x, a); // save controller status here always
    pha();
    and_imm(0b00110000); // check for select or start
    and_absx(JoypadBitMask); // if neither saved state nor current state
    if (zero_flag) { goto Save8Bits; } // have any of these two set, branch
    pla();
    and_imm(0b11001111); // otherwise store without select
    write_byte(SavedJoypadBits + x, a); // or start bits and leave
    goto rts;
  
Save8Bits:
    pla();
    write_byte(JoypadBitMask + x, a); // save with all bits in another place and leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - vram buffer address table low
    // $01 - vram buffer address table high
  
WriteBufferToScreen:
    write_byte(PPU_ADDRESS, a); // store high byte of vram address
    iny();
    lda_indy(0x0); // load next byte (second)
    write_byte(PPU_ADDRESS, a); // store low byte of vram address
    iny();
    lda_indy(0x0); // load next byte (third)
    asl_acc(); // shift to left and save in stack
    pha();
    lda_abs(Mirror_PPU_CTRL_REG1); // load mirror of $2000,
    ora_imm(0b00000100); // set ppu to increment by 32 by default
    if (carry_flag) { goto SetupWrites; } // if d7 of third byte was clear, ppu will
    and_imm(0b11111011); // only increment by 1
  
SetupWrites:
    WritePPUReg1(); // write to register
    pla(); // pull from stack and shift to left again
    asl_acc();
    if (!carry_flag) { goto GetLength; } // if d6 of third byte was clear, do not repeat byte
    ora_imm(0b00000010); // otherwise set d1 and increment Y
    iny();
  
GetLength:
    lsr_acc(); // shift back to the right to get proper length
    lsr_acc(); // note that d1 will now be in carry
    tax();
  
OutputToVRAM:
    if (carry_flag) { goto RepeatByte; } // if carry set, repeat loading the same byte
    iny(); // otherwise increment Y to load next byte
  
RepeatByte:
    lda_indy(0x0); // load more data from buffer and write to vram
    write_byte(PPU_DATA, a);
    dex(); // done writing?
    if (!zero_flag) { goto OutputToVRAM; }
    carry_flag = true;
    tya();
    adc_zp(0x0); // add end length plus one to the indirect at $00
    write_byte(0x0, a); // to allow this routine to read another set of updates
    lda_imm(0x0);
    adc_zp(0x1);
    write_byte(0x1, a);
    lda_imm(0x3f); // sets vram address to $3f00
    write_byte(PPU_ADDRESS, a);
    lda_imm(0x0);
    write_byte(PPU_ADDRESS, a);
    write_byte(PPU_ADDRESS, a); // then reinitializes it for some reason
    write_byte(PPU_ADDRESS, a);
  
UpdateScreen:
    ldx_abs(PPU_STATUS); // reset flip-flop
    ldy_imm(0x0); // load first byte from indirect as a pointer
    lda_indy(0x0);
    if (!zero_flag) { goto WriteBufferToScreen; } // if byte is zero we have no further updates to make here
    InitScroll(); goto rts; // <fallthrough>
    // -------------------------------------------------------------------------------------
  
DigitsMathRoutine:
    lda_abs(OperMode); // check mode of operation
    cmp_imm(TitleScreenModeValue);
    if (zero_flag) { goto EraseDMods; } // if in title screen mode, branch to lock score
    ldx_imm(0x5);
  
AddModLoop:
    lda_absx(DigitModifier); // load digit amount to increment
    carry_flag = false;
    adc_absy(DisplayDigits); // add to current digit
    if (neg_flag) { goto BorrowOne; } // if result is a negative number, branch to subtract
    cmp_imm(10);
    if (carry_flag) { goto CarryOne; } // if digit greater than $09, branch to add
  
StoreNewD:
    write_byte(DisplayDigits + y, a); // store as new score or game timer digit
    dey(); // move onto next digits in score or game timer
    dex(); // and digit amounts to increment
    if (!neg_flag) { goto AddModLoop; } // loop back if we're not done yet
  
EraseDMods:
    lda_imm(0x0); // store zero here
    ldx_imm(0x6); // start with the last digit
  
EraseMLoop:
    write_byte(DigitModifier - 1 + x, a); // initialize the digit amounts to increment
    dex();
    if (!neg_flag) { goto EraseMLoop; } // do this until they're all reset, then leave
    goto rts;
  
BorrowOne:
    dec_absx(DigitModifier - 1); // decrement the previous digit, then put $09 in
    lda_imm(0x9); // the game timer digit we're currently on to "borrow
    if (!zero_flag) { goto StoreNewD; } // the one", then do an unconditional branch back
  
CarryOne:
    carry_flag = true; // subtract ten from our digit to make it a
    sbc_imm(10); // proper BCD number, then increment the digit
    inc_absx(DigitModifier - 1); // preceding current digit to "carry the one" properly
    goto StoreNewD; // go back to just after we branched here
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
  
InitializeGame:
    ldy_imm(0x6f); // clear all memory as in initialization procedure,
    InitializeMemory(); // but this time, clear only as far as $076f
    ldy_imm(0x1f);
  
ClrSndLoop:
    write_byte(SoundMemory + y, a); // clear out memory used
    dey(); // by the sound engines
    if (!neg_flag) { goto ClrSndLoop; }
    lda_imm(0x18); // set demo timer
    write_byte(DemoTimer, a);
    jsr(LoadAreaPointer, 34);
  
InitializeArea:
    ldy_imm(0x4b); // clear all memory again, only as far as $074b
    InitializeMemory(); // this is only necessary if branching from
    ldx_imm(0x21);
    lda_imm(0x0);
  
ClrTimersLoop:
    write_byte(Timers + x, a); // clear out memory between
    dex(); // $0780 and $07a1
    if (!neg_flag) { goto ClrTimersLoop; }
    lda_abs(HalfwayPage);
    ldy_abs(AltEntranceControl); // if AltEntranceControl not set, use halfway page, if any found
    if (zero_flag) { goto StartPage; }
    lda_abs(EntrancePage); // otherwise use saved entry page number here
  
StartPage:
    write_byte(ScreenLeft_PageLoc, a); // set as value here
    write_byte(CurrentPageLoc, a); // also set as current page
    write_byte(BackloadingFlag, a); // set flag here if halfway page or saved entry page number found
    GetScreenPosition(); // get pixel coordinates for screen borders
    ldy_imm(0x20); // if on odd numbered page, use $2480 as start of rendering
    and_imm(0b00000001); // otherwise use $2080, this address used later as name table
    if (zero_flag) { goto SetInitNTHigh; } // address for rendering of game area
    ldy_imm(0x24);
  
SetInitNTHigh:
    write_byte(CurrentNTAddr_High, y); // store name table address
    ldy_imm(0x80);
    write_byte(CurrentNTAddr_Low, y);
    asl_acc(); // store LSB of page number in high nybble
    asl_acc(); // of block buffer column position
    asl_acc();
    asl_acc();
    write_byte(BlockBufferColumnPos, a);
    dec_abs(AreaObjectLength); // set area object lengths for all empty
    dec_abs(AreaObjectLength + 1);
    dec_abs(AreaObjectLength + 2);
    lda_imm(0xb); // set value for renderer to update 12 column sets
    write_byte(ColumnSets, a); // 12 column sets = 24 metatile columns = 1 1/2 screens
    jsr(GetAreaDataAddrs, 35); // get enemy and level addresses and load header
    lda_abs(PrimaryHardMode); // check to see if primary hard mode has been activated
    if (!zero_flag) { goto SetSecHard; } // if so, activate the secondary no matter where we're at
    lda_abs(WorldNumber); // otherwise check world number
    cmp_imm(World5); // if less than 5, do not activate secondary
    if (!carry_flag) { goto CheckHalfway; }
    if (!zero_flag) { goto SetSecHard; } // if not equal to, then world > 5, thus activate
    lda_abs(LevelNumber); // otherwise, world 5, so check level number
    cmp_imm(Level3); // if 1 or 2, do not set secondary hard mode flag
    if (!carry_flag) { goto CheckHalfway; }
  
SetSecHard:
    inc_abs(SecondaryHardMode); // set secondary hard mode flag for areas 5-3 and beyond
  
CheckHalfway:
    lda_abs(HalfwayPage);
    if (zero_flag) { goto DoneInitArea; }
    lda_imm(0x2); // if halfway page set, overwrite start position from header
    write_byte(PlayerEntranceCtrl, a);
  
DoneInitArea:
    lda_imm(Silence); // silence music
    write_byte(AreaMusicQueue, a);
    lda_imm(0x1); // disable screen output
    write_byte(DisableScreenFlag, a);
    inc_abs(OperMode_Task); // increment one of the modes
    goto rts;
    // -------------------------------------------------------------------------------------
  
PrimaryGameSetup:
    lda_imm(0x1);
    write_byte(FetchNewGameTimerFlag, a); // set flag to load game timer from header
    write_byte(PlayerSize, a); // set player's size to small
    lda_imm(0x2);
    write_byte(NumberofLives, a); // give each player three lives
    write_byte(OffScr_NumberofLives, a);
  
SecondaryGameSetup:
    lda_imm(0x0);
    write_byte(DisableScreenFlag, a); // enable screen output
    tay();
  
ClearVRLoop:
    write_byte(VRAM_Buffer1 - 1 + y, a); // clear buffer at $0300-$03ff
    iny();
    if (!zero_flag) { goto ClearVRLoop; }
    write_byte(GameTimerExpiredFlag, a); // clear game timer exp flag
    write_byte(DisableIntermediate, a); // clear skip lives display flag
    write_byte(BackloadingFlag, a); // clear value here
    lda_imm(0xff);
    write_byte(BalPlatformAlignment, a); // initialize balance platform assignment flag
    lda_abs(ScreenLeft_PageLoc); // get left side page location
    lsr_abs(Mirror_PPU_CTRL_REG1); // shift LSB of ppu register #1 mirror out
    and_imm(0x1); // mask out all but LSB of page location
    ror_acc(); // rotate LSB of page location into carry then onto mirror
    rol_abs(Mirror_PPU_CTRL_REG1); // this is to set the proper PPU name table
    GetAreaMusic(); // load proper music into queue
    lda_imm(0x38); // load sprite shuffle amounts to be used later
    write_byte(SprShuffleAmt + 2, a);
    lda_imm(0x48);
    write_byte(SprShuffleAmt + 1, a);
    lda_imm(0x58);
    write_byte(SprShuffleAmt, a);
    ldx_imm(0xe); // load default OAM offsets into $06e4-$06f2
  
ShufAmtLoop:
    lda_absx(DefaultSprOffsets);
    write_byte(SprDataOffset + x, a);
    dex(); // do this until they're all set
    if (!neg_flag) { goto ShufAmtLoop; }
    ldy_imm(0x3); // set up sprite #0
  
ISpr0Loop:
    lda_absy(Sprite0Data);
    write_byte(Sprite_Data + y, a);
    dey();
    if (!neg_flag) { goto ISpr0Loop; }
    DoNothing2(); // these jsrs doesn't do anything useful
    jsr(DoNothing1, 36);
    inc_abs(Sprite0HitDetectFlag); // set sprite #0 check flag
    inc_abs(OperMode_Task); // increment to next task
    goto rts;
    // -------------------------------------------------------------------------------------
    // $06 - RAM address low
    // $07 - RAM address high
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
  
Entrance_GameTimerSetup:
    lda_abs(ScreenLeft_PageLoc); // set current page for area objects
    write_byte(Player_PageLoc, a); // as page location for player
    lda_imm(0x28); // store value here
    write_byte(VerticalForceDown, a); // for fractional movement downwards if necessary
    lda_imm(0x1); // set high byte of player position and
    write_byte(PlayerFacingDir, a); // set facing direction so that player faces right
    write_byte(Player_Y_HighPos, a);
    lda_imm(0x0); // set player state to on the ground by default
    write_byte(Player_State, a);
    dec_abs(Player_CollisionBits); // initialize player's collision bits
    ldy_imm(0x0); // initialize halfway page
    write_byte(HalfwayPage, y);
    lda_abs(AreaType); // check area type
    if (!zero_flag) { goto ChkStPos; } // if water type, set swimming flag, otherwise do not set
    iny();
  
ChkStPos:
    write_byte(SwimmingFlag, y);
    ldx_abs(PlayerEntranceCtrl); // get starting position loaded from header
    ldy_abs(AltEntranceControl); // check alternate mode of entry flag for 0 or 1
    if (zero_flag) { goto SetStPos; }
    cpy_imm(0x1);
    if (zero_flag) { goto SetStPos; }
    ldx_absy(AltYPosOffset - 2); // if not 0 or 1, override $0710 with new offset in X
  
SetStPos:
    lda_absy(PlayerStarting_X_Pos); // load appropriate horizontal position
    write_byte(Player_X_Position, a); // and vertical positions for the player, using
    lda_absx(PlayerStarting_Y_Pos); // AltEntranceControl as offset for horizontal and either $0710
    write_byte(Player_Y_Position, a); // or value that overwrote $0710 as offset for vertical
    lda_absx(PlayerBGPriorityData);
    write_byte(Player_SprAttrib, a); // set player sprite attributes using offset in X
    jsr(GetPlayerColors, 37); // get appropriate player palette
    ldy_abs(GameTimerSetting); // get timer control value from header
    if (zero_flag) { goto ChkOverR; } // if set to zero, branch (do not use dummy byte for this)
    lda_abs(FetchNewGameTimerFlag); // do we need to set the game timer? if not, use 
    if (zero_flag) { goto ChkOverR; } // old game timer setting
    lda_absy(GameTimerData); // if game timer is set and game timer flag is also set,
    write_byte(GameTimerDisplay, a); // use value of game timer control for first digit of game timer
    lda_imm(0x1);
    write_byte(GameTimerDisplay + 2, a); // set last digit of game timer to 1
    lsr_acc();
    write_byte(GameTimerDisplay + 1, a); // set second digit of game timer
    write_byte(FetchNewGameTimerFlag, a); // clear flag for game timer reset
    write_byte(StarInvincibleTimer, a); // clear star mario timer
  
ChkOverR:
    ldy_abs(JoypadOverride); // if controller bits not set, branch to skip this part
    if (zero_flag) { goto ChkSwimE; }
    lda_imm(0x3); // set player state to climbing
    write_byte(Player_State, a);
    ldx_imm(0x0); // set offset for first slot, for block object
    InitBlock_XY_Pos();
    lda_imm(0xf0); // set vertical coordinate for block object
    write_byte(Block_Y_Position, a);
    ldx_imm(0x5); // set offset in X for last enemy object buffer slot
    ldy_imm(0x0); // set offset in Y for object coordinates used earlier
    Setup_Vine(); // do a sub to grow vine
  
ChkSwimE:
    ldy_abs(AreaType); // if level not water-type,
    if (!zero_flag) { goto SetPESub; } // skip this subroutine
    jsr(SetupBubble, 38); // otherwise, execute sub to set up air bubbles
  
SetPESub:
    lda_imm(0x7); // set to run player entrance subroutine
    write_byte(GameEngineSubroutine, a); // on the next frame of game engine
    goto rts;
    // -------------------------------------------------------------------------------------
    // page numbers are in order from -1 to -4
  
PlayerLoseLife:
    inc_abs(DisableScreenFlag); // disable screen and sprite 0 check
    lda_imm(0x0);
    write_byte(Sprite0HitDetectFlag, a);
    lda_imm(Silence); // silence music
    write_byte(EventMusicQueue, a);
    dec_abs(NumberofLives); // take one life from player
    if (!neg_flag) { goto StillInGame; } // if player still has lives, branch
    lda_imm(0x0);
    write_byte(OperMode_Task, a); // initialize mode task,
    lda_imm(GameOverModeValue); // switch to game over mode
    write_byte(OperMode, a); // and leave
    goto rts;
  
StillInGame:
    lda_abs(WorldNumber); // multiply world number by 2 and use
    asl_acc(); // as offset
    tax();
    lda_abs(LevelNumber); // if in area -3 or -4, increment
    and_imm(0x2); // offset by one byte, otherwise
    if (zero_flag) { goto GetHalfway; } // leave offset alone
    inx();
  
GetHalfway:
    ldy_absx(HalfwayPageNybbles); // get halfway page number with offset
    lda_abs(LevelNumber); // check area number's LSB
    lsr_acc();
    tya(); // if in area -2 or -4, use lower nybble
    if (carry_flag) { goto MaskHPNyb; }
    lsr_acc(); // move higher nybble to lower if area
    lsr_acc(); // number is -1 or -3
    lsr_acc();
    lsr_acc();
  
MaskHPNyb:
    and_imm(0b00001111); // mask out all but lower nybble
    cmp_abs(ScreenLeft_PageLoc);
    if (zero_flag) { goto SetHalfway; } // left side of screen must be at the halfway page,
    if (!carry_flag) { goto SetHalfway; } // otherwise player must start at the
    lda_imm(0x0); // beginning of the level
  
SetHalfway:
    write_byte(HalfwayPage, a); // store as halfway page for player
    TransposePlayers(); // switch players around if 2-player game
    goto ContinueGame; // continue the game
    // -------------------------------------------------------------------------------------
  
GameOverMode:
    lda_abs(OperMode_Task);
    // jsr JumpEngine
    switch (a) {
      case 0: SetupGameOver(); goto rts;
      case 1: goto ScreenRoutines;
      case 2: goto RunGameOver;
    }
  
RunGameOver:
    lda_imm(0x0); // reenable screen
    write_byte(DisableScreenFlag, a);
    lda_abs(SavedJoypad1Bits); // check controller for start pressed
    and_imm(Start_Button);
    if (!zero_flag) { goto TerminateGame; }
    lda_abs(ScreenTimer); // if not pressed, wait for
    if (!zero_flag) { goto GameIsOn; } // screen timer to expire
  
TerminateGame:
    lda_imm(Silence); // silence music
    write_byte(EventMusicQueue, a);
    TransposePlayers(); // check if other player can keep
    if (!carry_flag) { goto ContinueGame; } // going, and do so if possible
    lda_abs(WorldNumber); // otherwise put world number of current
    write_byte(ContinueWorld, a); // player into secret continue function variable
    lda_imm(0x0);
    asl_acc(); // residual ASL instruction
    write_byte(OperMode_Task, a); // reset all modes to title screen and
    write_byte(ScreenTimer, a); // leave
    write_byte(OperMode, a);
    goto rts;
  
ContinueGame:
    jsr(LoadAreaPointer, 39); // update level pointer with
    lda_imm(0x1); // actual world and area numbers, then
    write_byte(PlayerSize, a); // reset player's size, status, and
    inc_abs(FetchNewGameTimerFlag); // set game timer flag to reload
    lda_imm(0x0); // game timer from header
    write_byte(TimerControl, a); // also set flag for timers to count again
    write_byte(PlayerStatus, a);
    write_byte(GameEngineSubroutine, a); // reset task for game core
    write_byte(OperMode_Task, a); // set modes and leave
    lda_imm(0x1); // if in game over mode, switch back to
    write_byte(OperMode, a); // game mode, because game is still on
  
GameIsOn:
    goto rts;
    // -------------------------------------------------------------------------------------
  
DoNothing1:
    lda_imm(0xff); // this is residual code, this value is
    write_byte(0x6c9, a); // not used anywhere in the program
    DoNothing2(); goto rts; // <fallthrough>
  
AreaParserTaskHandler:
    ldy_abs(AreaParserTaskNum); // check number of tasks here
    if (!zero_flag) { goto DoAPTasks; } // if already set, go ahead
    ldy_imm(0x8);
    write_byte(AreaParserTaskNum, y); // otherwise, set eight by default
  
DoAPTasks:
    dey();
    tya();
    jsr(AreaParserTasks, 40);
    dec_abs(AreaParserTaskNum); // if all tasks not complete do not
    if (!zero_flag) { goto SkipATRender; } // render attribute table yet
    jsr(RenderAttributeTables, 41);
  
SkipATRender:
    goto rts;
  
AreaParserTasks:
    // jsr JumpEngine
    switch (a) {
      case 0: goto IncrementColumnPos;
      case 1: goto RenderAreaGraphics;
      case 2: goto RenderAreaGraphics;
      case 3: goto AreaParserCore;
      case 4: goto IncrementColumnPos;
      case 5: goto RenderAreaGraphics;
      case 6: goto RenderAreaGraphics;
      case 7: goto AreaParserCore;
    }
  
IncrementColumnPos:
    inc_abs(CurrentColumnPos); // increment column where we're at
    lda_abs(CurrentColumnPos);
    and_imm(0b00001111); // mask out higher nybble
    if (!zero_flag) { goto NoColWrap; }
    write_byte(CurrentColumnPos, a); // if no bits left set, wrap back to zero (0-f)
    inc_abs(CurrentPageLoc); // and increment page number where we're at
  
NoColWrap:
    inc_abs(BlockBufferColumnPos); // increment column offset where we're at
    lda_abs(BlockBufferColumnPos);
    and_imm(0b00011111); // mask out all but 5 LSB (0-1f)
    write_byte(BlockBufferColumnPos, a); // and save
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used as counter, store for low nybble for background, ceiling byte for terrain
    // $01 - used to store floor byte for terrain
    // $07 - used to store terrain metatile
    // $06-$07 - used to store block buffer address
  
AreaParserCore:
    lda_abs(BackloadingFlag); // check to see if we are starting right of start
    if (zero_flag) { goto RenderSceneryTerrain; } // if not, go ahead and render background, foreground and terrain
    jsr(ProcessAreaData, 42); // otherwise skip ahead and load level data
  
RenderSceneryTerrain:
    ldx_imm(0xc);
    lda_imm(0x0);
  
ClrMTBuf:
    write_byte(MetatileBuffer + x, a); // clear out metatile buffer
    dex();
    if (!neg_flag) { goto ClrMTBuf; }
    ldy_abs(BackgroundScenery); // do we need to render the background scenery?
    if (zero_flag) { goto RendFore; } // if not, skip to check the foreground
    lda_abs(CurrentPageLoc); // otherwise check for every third page
  
ThirdP:
    cmp_imm(0x3);
    if (neg_flag) { goto RendBack; } // if less than three we're there
    carry_flag = true;
    sbc_imm(0x3); // if 3 or more, subtract 3 and 
    if (!neg_flag) { goto ThirdP; } // do an unconditional branch
  
RendBack:
    asl_acc(); // move results to higher nybble
    asl_acc();
    asl_acc();
    asl_acc();
    adc_absy(BSceneDataOffsets - 1); // add to it offset loaded from here
    adc_abs(CurrentColumnPos); // add to the result our current column position
    tax();
    lda_absx(BackSceneryData); // load data from sum of offsets
    if (zero_flag) { goto RendFore; } // if zero, no scenery for that part
    pha();
    and_imm(0xf); // save to stack and clear high nybble
    carry_flag = true;
    sbc_imm(0x1); // subtract one (because low nybble is $01-$0c)
    write_byte(0x0, a); // save low nybble
    asl_acc(); // multiply by three (shift to left and add result to old one)
    adc_zp(0x0); // note that since d7 was nulled, the carry flag is always clear
    tax(); // save as offset for background scenery metatile data
    pla(); // get high nybble from stack, move low
    lsr_acc();
    lsr_acc();
    lsr_acc();
    lsr_acc();
    tay(); // use as second offset (used to determine height)
    lda_imm(0x3); // use previously saved memory location for counter
    write_byte(0x0, a);
  
SceLoop1:
    lda_absx(BackSceneryMetatiles); // load metatile data from offset of (lsb - 1) * 3
    write_byte(MetatileBuffer + y, a); // store into buffer from offset of (msb / 16)
    inx();
    iny();
    cpy_imm(0xb); // if at this location, leave loop
    if (zero_flag) { goto RendFore; }
    dec_zp(0x0); // decrement until counter expires, barring exception
    if (!zero_flag) { goto SceLoop1; }
  
RendFore:
    ldx_abs(ForegroundScenery); // check for foreground data needed or not
    if (zero_flag) { goto RendTerr; } // if not, skip this part
    ldy_absx(FSceneDataOffsets - 1); // load offset from location offset by header value, then
    ldx_imm(0x0); // reinit X
  
SceLoop2:
    lda_absy(ForeSceneryData); // load data until counter expires
    if (zero_flag) { goto NoFore; } // do not store if zero found
    write_byte(MetatileBuffer + x, a);
  
NoFore:
    iny();
    inx();
    cpx_imm(0xd); // store up to end of metatile buffer
    if (!zero_flag) { goto SceLoop2; }
  
RendTerr:
    ldy_abs(AreaType); // check world type for water level
    if (!zero_flag) { goto TerMTile; } // if not water level, skip this part
    lda_abs(WorldNumber); // check world number, if not world number eight
    cmp_imm(World8); // then skip this part
    if (!zero_flag) { goto TerMTile; }
    lda_imm(0x62); // if set as water level and world number eight,
    goto StoreMT; // use castle wall metatile as terrain type
  
TerMTile:
    lda_absy(TerrainMetatiles); // otherwise get appropriate metatile for area type
    ldy_abs(CloudTypeOverride); // check for cloud type override
    if (zero_flag) { goto StoreMT; } // if not set, keep value otherwise
    lda_imm(0x88); // use cloud block terrain
  
StoreMT:
    write_byte(0x7, a); // store value here
    ldx_imm(0x0); // initialize X, use as metatile buffer offset
    lda_abs(TerrainControl); // use yet another value from the header
    asl_acc(); // multiply by 2 and use as yet another offset
    tay();
  
TerrLoop:
    lda_absy(TerrainRenderBits); // get one of the terrain rendering bit data
    write_byte(0x0, a);
    iny(); // increment Y and use as offset next time around
    write_byte(0x1, y);
    lda_abs(CloudTypeOverride); // skip if value here is zero
    if (zero_flag) { goto NoCloud2; }
    cpx_imm(0x0); // otherwise, check if we're doing the ceiling byte
    if (zero_flag) { goto NoCloud2; }
    lda_zp(0x0); // if not, mask out all but d3
    and_imm(0b00001000);
    write_byte(0x0, a);
  
NoCloud2:
    ldy_imm(0x0); // start at beginning of bitmasks
  
TerrBChk:
    lda_absy(Bitmasks); // load bitmask, then perform AND on contents of first byte
    bit_zp(0x0);
    if (zero_flag) { goto NextTBit; } // if not set, skip this part (do not write terrain to buffer)
    lda_zp(0x7);
    write_byte(MetatileBuffer + x, a); // load terrain type metatile number and store into buffer here
  
NextTBit:
    inx(); // continue until end of buffer
    cpx_imm(0xd);
    if (zero_flag) { goto RendBBuf; } // if we're at the end, break out of this loop
    lda_abs(AreaType); // check world type for underground area
    cmp_imm(0x2);
    if (!zero_flag) { goto EndUChk; } // if not underground, skip this part
    cpx_imm(0xb);
    if (!zero_flag) { goto EndUChk; } // if we're at the bottom of the screen, override
    lda_imm(0x54); // old terrain type with ground level terrain type
    write_byte(0x7, a);
  
EndUChk:
    iny(); // increment bitmasks offset in Y
    cpy_imm(0x8);
    if (!zero_flag) { goto TerrBChk; } // if not all bits checked, loop back    
    ldy_zp(0x1);
    if (!zero_flag) { goto TerrLoop; } // unconditional branch, use Y to load next byte
  
RendBBuf:
    jsr(ProcessAreaData, 43); // do the area data loading routine now
    lda_abs(BlockBufferColumnPos);
    GetBlockBufferAddr(); // get block buffer address from where we're at
    ldx_imm(0x0);
    ldy_imm(0x0); // init index regs and start at beginning of smaller buffer
  
ChkMTLow:
    write_byte(0x0, y);
    lda_absx(MetatileBuffer); // load stored metatile number
    and_imm(0b11000000); // mask out all but 2 MSB
    asl_acc();
    rol_acc(); // make %xx000000 into %000000xx
    rol_acc();
    tay(); // use as offset in Y
    lda_absx(MetatileBuffer); // reload original unmasked value here
    cmp_absy(BlockBuffLowBounds); // check for certain values depending on bits set
    if (carry_flag) { goto StrBlock; } // if equal or greater, branch
    lda_imm(0x0); // if less, init value before storing
  
StrBlock:
    ldy_zp(0x0); // get offset for block buffer
    write_byte(read_word(0x6) + y, a); // store value into block buffer
    tya();
    carry_flag = false; // add 16 (move down one row) to offset
    adc_imm(0x10);
    tay();
    inx(); // increment column value
    cpx_imm(0xd);
    if (!carry_flag) { goto ChkMTLow; } // continue until we pass last row, then leave
    goto rts;
    // numbers lower than these with the same attribute bits
    // will not be stored in the block buffer
  
ProcessAreaData:
    ldx_imm(0x2); // start at the end of area object buffer
  
ProcADLoop:
    write_byte(ObjectOffset, x);
    lda_imm(0x0); // reset flag
    write_byte(BehindAreaParserFlag, a);
    ldy_abs(AreaDataOffset); // get offset of area data pointer
    lda_indy(AreaData); // get first byte of area object
    cmp_imm(0xfd); // if end-of-area, skip all this crap
    if (zero_flag) { goto RdyDecode; }
    lda_absx(AreaObjectLength); // check area object buffer flag
    if (!neg_flag) { goto RdyDecode; } // if buffer not negative, branch, otherwise
    iny();
    lda_indy(AreaData); // get second byte of area object
    asl_acc(); // check for page select bit (d7), branch if not set
    if (!carry_flag) { goto Chk1Row13; }
    lda_abs(AreaObjectPageSel); // check page select
    if (!zero_flag) { goto Chk1Row13; }
    inc_abs(AreaObjectPageSel); // if not already set, set it now
    inc_abs(AreaObjectPageLoc); // and increment page location
  
Chk1Row13:
    dey();
    lda_indy(AreaData); // reread first byte of level object
    and_imm(0xf); // mask out high nybble
    cmp_imm(0xd); // row 13?
    if (!zero_flag) { goto Chk1Row14; }
    iny(); // if so, reread second byte of level object
    lda_indy(AreaData);
    dey(); // decrement to get ready to read first byte
    and_imm(0b01000000); // check for d6 set (if not, object is page control)
    if (!zero_flag) { goto CheckRear; }
    lda_abs(AreaObjectPageSel); // if page select is set, do not reread
    if (!zero_flag) { goto CheckRear; }
    iny(); // if d6 not set, reread second byte
    lda_indy(AreaData);
    and_imm(0b00011111); // mask out all but 5 LSB and store in page control
    write_byte(AreaObjectPageLoc, a);
    inc_abs(AreaObjectPageSel); // increment page select
    goto NextAObj;
  
Chk1Row14:
    cmp_imm(0xe); // row 14?
    if (!zero_flag) { goto CheckRear; }
    lda_abs(BackloadingFlag); // check flag for saved page number and branch if set
    if (!zero_flag) { goto RdyDecode; } // to render the object (otherwise bg might not look right)
  
CheckRear:
    lda_abs(AreaObjectPageLoc); // check to see if current page of level object is
    cmp_abs(CurrentPageLoc); // behind current page of renderer
    if (!carry_flag) { goto SetBehind; } // if so branch
  
RdyDecode:
    jsr(DecodeAreaData, 44); // do sub and do not turn on flag
    goto ChkLength;
  
SetBehind:
    inc_abs(BehindAreaParserFlag); // turn on flag if object is behind renderer
  
NextAObj:
    jsr(IncAreaObjOffset, 45); // increment buffer offset and move on
  
ChkLength:
    ldx_zp(ObjectOffset); // get buffer offset
    lda_absx(AreaObjectLength); // check object length for anything stored here
    if (neg_flag) { goto ProcLoopb; } // if not, branch to handle loopback
    dec_absx(AreaObjectLength); // otherwise decrement length or get rid of it
  
ProcLoopb:
    dex(); // decrement buffer offset
    if (!neg_flag) { goto ProcADLoop; } // and loopback unless exceeded buffer
    lda_abs(BehindAreaParserFlag); // check for flag set if objects were behind renderer
    if (!zero_flag) { goto ProcessAreaData; } // branch if true to load more level data, otherwise
    lda_abs(BackloadingFlag); // check for flag set if starting right of page $00
    if (!zero_flag) { goto ProcessAreaData; } // branch if true to load more level data, otherwise leave
  
EndAParse:
    goto rts;
  
IncAreaObjOffset:
    inc_abs(AreaDataOffset); // increment offset of level pointer
    inc_abs(AreaDataOffset);
    lda_imm(0x0); // reset page select
    write_byte(AreaObjectPageSel, a);
    goto rts;
  
DecodeAreaData:
    lda_absx(AreaObjectLength); // check current buffer flag
    if (neg_flag) { goto Chk1stB; }
    ldy_absx(AreaObjOffsetBuffer); // if not, get offset from buffer
  
Chk1stB:
    ldx_imm(0x10); // load offset of 16 for special row 15
    lda_indy(AreaData); // get first byte of level object again
    cmp_imm(0xfd);
    if (zero_flag) { goto EndAParse; } // if end of level, leave this routine
    and_imm(0xf); // otherwise, mask out low nybble
    cmp_imm(0xf); // row 15?
    if (zero_flag) { goto ChkRow14; } // if so, keep the offset of 16
    ldx_imm(0x8); // otherwise load offset of 8 for special row 12
    cmp_imm(0xc); // row 12?
    if (zero_flag) { goto ChkRow14; } // if so, keep the offset value of 8
    ldx_imm(0x0); // otherwise nullify value by default
  
ChkRow14:
    write_byte(0x7, x); // store whatever value we just loaded here
    ldx_zp(ObjectOffset); // get object offset again
    cmp_imm(0xe); // row 14?
    if (!zero_flag) { goto ChkRow13; }
    lda_imm(0x0); // if so, load offset with $00
    write_byte(0x7, a);
    lda_imm(0x2e); // and load A with another value
    if (!zero_flag) { goto NormObj; } // unconditional branch
  
ChkRow13:
    cmp_imm(0xd); // row 13?
    if (!zero_flag) { goto ChkSRows; }
    lda_imm(0x22); // if so, load offset with 34
    write_byte(0x7, a);
    iny(); // get next byte
    lda_indy(AreaData);
    and_imm(0b01000000); // mask out all but d6 (page control obj bit)
    if (zero_flag) { goto LeavePar; } // if d6 clear, branch to leave (we handled this earlier)
    lda_indy(AreaData); // otherwise, get byte again
    and_imm(0b01111111); // mask out d7
    cmp_imm(0x4b); // check for loop command in low nybble
    if (!zero_flag) { goto Mask2MSB; } // (plus d6 set for object other than page control)
    inc_abs(LoopCommand); // if loop command, set loop command flag
  
Mask2MSB:
    and_imm(0b00111111); // mask out d7 and d6
    goto NormObj; // and jump
  
ChkSRows:
    cmp_imm(0xc); // row 12-15?
    if (carry_flag) { goto SpecObj; }
    iny(); // if not, get second byte of level object
    lda_indy(AreaData);
    and_imm(0b01110000); // mask out all but d6-d4
    if (!zero_flag) { goto LrgObj; } // if any bits set, branch to handle large object
    lda_imm(0x16);
    write_byte(0x7, a); // otherwise set offset of 24 for small object
    lda_indy(AreaData); // reload second byte of level object
    and_imm(0b00001111); // mask out higher nybble and jump
    goto NormObj;
  
LrgObj:
    write_byte(0x0, a); // store value here (branch for large objects)
    cmp_imm(0x70); // check for vertical pipe object
    if (!zero_flag) { goto NotWPipe; }
    lda_indy(AreaData); // if not, reload second byte
    and_imm(0b00001000); // mask out all but d3 (usage control bit)
    if (zero_flag) { goto NotWPipe; } // if d3 clear, branch to get original value
    lda_imm(0x0); // otherwise, nullify value for warp pipe
    write_byte(0x0, a);
  
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
    write_byte(0x0, a); // store value here (branch for small objects and rows 13 and 14)
    lda_absx(AreaObjectLength); // is there something stored here already?
    if (!neg_flag) { goto RunAObj; } // if so, branch to do its particular sub
    lda_abs(AreaObjectPageLoc); // otherwise check to see if the object we've loaded is on the
    cmp_abs(CurrentPageLoc); // same page as the renderer, and if so, branch
    if (zero_flag) { goto InitRear; }
    ldy_abs(AreaDataOffset); // if not, get old offset of level pointer
    lda_indy(AreaData); // and reload first byte
    and_imm(0b00001111);
    cmp_imm(0xe); // row 14?
    if (!zero_flag) { goto LeavePar; }
    lda_abs(BackloadingFlag); // if so, check backloading flag
    if (!zero_flag) { goto StrAObj; } // if set, branch to render object, else leave
  
LeavePar:
    goto rts;
  
InitRear:
    lda_abs(BackloadingFlag); // check backloading flag to see if it's been initialized
    if (zero_flag) { goto BackColC; } // branch to column-wise check
    lda_imm(0x0); // if not, initialize both backloading and 
    write_byte(BackloadingFlag, a); // behind-renderer flags and leave
    write_byte(BehindAreaParserFlag, a);
    write_byte(ObjectOffset, a);
  
LoopCmdE:
    goto rts;
  
BackColC:
    ldy_abs(AreaDataOffset); // get first byte again
    lda_indy(AreaData);
    and_imm(0b11110000); // mask out low nybble and move high to low
    lsr_acc();
    lsr_acc();
    lsr_acc();
    lsr_acc();
    cmp_abs(CurrentColumnPos); // is this where we're at?
    if (!zero_flag) { goto LeavePar; } // if not, branch to leave
  
StrAObj:
    lda_abs(AreaDataOffset); // if so, load area obj offset and store in buffer
    write_byte(AreaObjOffsetBuffer + x, a);
    jsr(IncAreaObjOffset, 46); // do sub to increment to next object data
  
RunAObj:
    lda_zp(0x0); // get stored value and add offset to it
    carry_flag = false; // then use the jump engine with current contents of A
    adc_zp(0x7);
    // jsr JumpEngine
    switch (a) {
      case 0: goto VerticalPipe;
      case 1: goto AreaStyleObject;
      case 2: goto RowOfBricks;
      case 3: goto RowOfSolidBlocks;
      case 4: goto RowOfCoins;
      case 5: goto ColumnOfBricks;
      case 6: goto ColumnOfSolidBlocks;
      case 7: goto VerticalPipe;
      case 8: goto Hole_Empty;
      case 9: goto PulleyRopeObject;
      case 10: goto Bridge_High;
      case 11: goto Bridge_Middle;
      case 12: goto Bridge_Low;
      case 13: goto Hole_Water;
      case 14: goto QuestionBlockRow_High;
      case 15: goto QuestionBlockRow_Low;
      case 16: goto EndlessRope;
      case 17: goto BalancePlatRope;
      case 18: goto CastleObject;
      case 19: goto StaircaseObject;
      case 20: goto ExitPipe;
      case 21: goto FlagBalls_Residual;
      case 22: goto QuestionBlock;
      case 23: goto QuestionBlock;
      case 24: goto QuestionBlock;
      case 25: goto Hidden1UpBlock;
      case 26: goto BrickWithItem;
      case 27: goto BrickWithItem;
      case 28: goto BrickWithItem;
      case 29: goto BrickWithCoins;
      case 30: goto BrickWithItem;
      case 31: WaterPipe(); goto rts;
      case 32: goto EmptyBlock;
      case 33: Jumpspring(); goto rts;
      case 34: goto IntroPipe;
      case 35: goto FlagpoleObject;
      case 36: goto AxeObj;
      case 37: goto ChainObj;
      case 38: goto CastleBridgeObj;
      case 39: goto ScrollLockObject_Warp;
      case 40: ScrollLockObject(); goto rts;
      case 41: ScrollLockObject(); goto rts;
      case 42: AreaFrenzy(); goto rts;
      case 43: AreaFrenzy(); goto rts;
      case 44: AreaFrenzy(); goto rts;
      case 45: goto LoopCmdE;
      case 46: goto AlterAreaAttributes;
    }
  
AlterAreaAttributes:
    ldy_absx(AreaObjOffsetBuffer); // load offset for level object data saved in buffer
    iny(); // load second byte
    lda_indy(AreaData);
    pha(); // save in stack for now
    and_imm(0b01000000);
    if (!zero_flag) { goto Alter2; } // branch if d6 is set
    pla();
    pha(); // pull and push offset to copy to A
    and_imm(0b00001111); // mask out high nybble and store as
    write_byte(TerrainControl, a); // new terrain height type bits
    pla();
    and_imm(0b00110000); // pull and mask out all but d5 and d4
    lsr_acc(); // move bits to lower nybble and store
    lsr_acc(); // as new background scenery bits
    lsr_acc();
    lsr_acc();
    write_byte(BackgroundScenery, a); // then leave
    goto rts;
  
Alter2:
    pla();
    and_imm(0b00000111); // mask out all but 3 LSB
    cmp_imm(0x4); // if four or greater, set color control bits
    if (!carry_flag) { goto SetFore; } // and nullify foreground scenery bits
    write_byte(BackgroundColorCtrl, a);
    lda_imm(0x0);
  
SetFore:
    write_byte(ForegroundScenery, a); // otherwise set new foreground scenery bits
    goto rts;
    // --------------------------------
  
ScrollLockObject_Warp:
    ldx_imm(0x4); // load value of 4 for game text routine as default
    lda_abs(WorldNumber); // warp zone (4-3-2), then check world number
    if (zero_flag) { goto WarpNum; }
    inx(); // if world number > 1, increment for next warp zone (5)
    ldy_abs(AreaType); // check area type
    dey();
    if (!zero_flag) { goto WarpNum; } // if ground area type, increment for last warp zone
    inx(); // (8-7-6) and move on
  
WarpNum:
    txa();
    write_byte(WarpZoneControl, a); // store number here to be used by warp zone routine
    jsr(WriteGameText, 47); // print text and warp zone numbers
    lda_imm(PiranhaPlant);
    KillEnemies(); // load identifier for piranha plants and do sub
    ScrollLockObject(); goto rts; // <fallthrough>
    // --------------------------------
    // --------------------------------
    // $06 - used by MushroomLedge to store length
  
AreaStyleObject:
    lda_abs(AreaStyle); // load level object style and jump to the right sub
    // jsr JumpEngine
    switch (a) {
      case 0: goto TreeLedge;
      case 1: goto MushroomLedge;
      case 2: goto BulletBillCannon;
    }
  
TreeLedge:
    GetLrgObjAttrib(); // get row and length of green ledge
    lda_absx(AreaObjectLength); // check length counter for expiration
    if (zero_flag) { goto EndTreeL; }
    if (!neg_flag) { goto MidTreeL; }
    tya();
    write_byte(AreaObjectLength + x, a); // store lower nybble into buffer flag as length of ledge
    lda_abs(CurrentPageLoc);
    ora_abs(CurrentColumnPos); // are we at the start of the level?
    if (zero_flag) { goto MidTreeL; }
    lda_imm(0x16); // render start of tree ledge
    goto NoUnder;
  
MidTreeL:
    ldx_zp(0x7);
    lda_imm(0x17); // render middle of tree ledge
    write_byte(MetatileBuffer + x, a); // note that this is also used if ledge position is
    lda_imm(0x4c); // at the start of level for continuous effect
    goto AllUnder; // now render the part underneath
  
EndTreeL:
    lda_imm(0x18); // render end of tree ledge
    goto NoUnder;
  
MushroomLedge:
    jsr(ChkLrgObjLength, 48); // get shroom dimensions
    write_byte(0x6, y); // store length here for now
    if (!carry_flag) { goto EndMushL; }
    lda_absx(AreaObjectLength); // divide length by 2 and store elsewhere
    lsr_acc();
    write_byte(MushroomLedgeHalfLen + x, a);
    lda_imm(0x19); // render start of mushroom
    goto NoUnder;
  
EndMushL:
    lda_imm(0x1b); // if at the end, render end of mushroom
    ldy_absx(AreaObjectLength);
    if (zero_flag) { goto NoUnder; }
    lda_absx(MushroomLedgeHalfLen); // get divided length and store where length
    write_byte(0x6, a); // was stored originally
    ldx_zp(0x7);
    lda_imm(0x1a);
    write_byte(MetatileBuffer + x, a); // render middle of mushroom
    cpy_zp(0x6); // are we smack dab in the center?
    if (!zero_flag) { goto MushLExit; } // if not, branch to leave
    inx();
    lda_imm(0x4f);
    write_byte(MetatileBuffer + x, a); // render stem top of mushroom underneath the middle
    lda_imm(0x50);
  
AllUnder:
    inx();
    ldy_imm(0xf); // set $0f to render all way down
    goto RenderUnderPart; // now render the stem of mushroom
  
NoUnder:
    ldx_zp(0x7); // load row of ledge
    ldy_imm(0x0); // set 0 for no bottom on this part
    goto RenderUnderPart;
    // --------------------------------
    // tiles used by pulleys and rope object
  
PulleyRopeObject:
    jsr(ChkLrgObjLength, 49); // get length of pulley/rope object
    ldy_imm(0x0); // initialize metatile offset
    if (carry_flag) { goto RenderPul; } // if starting, render left pulley
    iny();
    lda_absx(AreaObjectLength); // if not at the end, render rope
    if (!zero_flag) { goto RenderPul; }
    iny(); // otherwise render right pulley
  
RenderPul:
    lda_absy(PulleyRopeMetatiles);
    write_byte(MetatileBuffer, a); // render at the top of the screen
  
MushLExit:
    goto rts; // and leave
    // --------------------------------
    // $06 - used to store upper limit of rows for CastleObject
  
CastleObject:
    GetLrgObjAttrib(); // save lower nybble as starting row
    write_byte(0x7, y); // if starting row is above $0a, game will crash!!!
    ldy_imm(0x4);
    jsr(ChkLrgObjFixedLength, 50); // load length of castle if not already loaded
    txa();
    pha(); // save obj buffer offset to stack
    ldy_absx(AreaObjectLength); // use current length as offset for castle data
    ldx_zp(0x7); // begin at starting row
    lda_imm(0xb);
    write_byte(0x6, a); // load upper limit of number of rows to print
  
CRendLoop:
    lda_absy(CastleMetatiles); // load current byte using offset
    write_byte(MetatileBuffer + x, a);
    inx(); // store in buffer and increment buffer offset
    lda_zp(0x6);
    if (zero_flag) { goto ChkCFloor; } // have we reached upper limit yet?
    iny(); // if not, increment column-wise
    iny(); // to byte in next row
    iny();
    iny();
    iny();
    dec_zp(0x6); // move closer to upper limit
  
ChkCFloor:
    cpx_imm(0xb); // have we reached the row just before floor?
    if (!zero_flag) { goto CRendLoop; } // if not, go back and do another row
    pla();
    tax(); // get obj buffer offset from before
    lda_abs(CurrentPageLoc);
    if (zero_flag) { goto ExitCastle; } // if we're at page 0, we do not need to do anything else
    lda_absx(AreaObjectLength); // check length
    cmp_imm(0x1); // if length almost about to expire, put brick at floor
    if (zero_flag) { goto PlayerStop; }
    ldy_zp(0x7); // check starting row for tall castle ($00)
    if (!zero_flag) { goto NotTall; }
    cmp_imm(0x3); // if found, then check to see if we're at the second column
    if (zero_flag) { goto PlayerStop; }
  
NotTall:
    cmp_imm(0x2); // if not tall castle, check to see if we're at the third column
    if (!zero_flag) { goto ExitCastle; } // if we aren't and the castle is tall, don't create flag yet
    GetAreaObjXPosition(); // otherwise, obtain and save horizontal pixel coordinate
    pha();
    FindEmptyEnemySlot(); // find an empty place on the enemy object buffer
    pla();
    write_byte(Enemy_X_Position + x, a); // then write horizontal coordinate for star flag
    lda_abs(CurrentPageLoc);
    write_byte(Enemy_PageLoc + x, a); // set page location for star flag
    lda_imm(0x1);
    write_byte(Enemy_Y_HighPos + x, a); // set vertical high byte
    write_byte(Enemy_Flag + x, a); // set flag for buffer
    lda_imm(0x90);
    write_byte(Enemy_Y_Position + x, a); // set vertical coordinate
    lda_imm(StarFlagObject); // set star flag value in buffer itself
    write_byte(Enemy_ID + x, a);
    goto rts;
  
PlayerStop:
    ldy_imm(0x52); // put brick at floor to stop player at end of level
    write_byte(MetatileBuffer + 10, y); // this is only done if we're on the second column
  
ExitCastle:
    goto rts;
    // --------------------------------
    // --------------------------------
    // $05 - used to store length of vertical shaft in RenderSidewaysPipe
    // $06 - used to store leftover horizontal length in RenderSidewaysPipe
    //  and vertical length in VerticalPipe and GetPipeHeight
  
IntroPipe:
    ldy_imm(0x3); // check if length set, if not set, set it
    jsr(ChkLrgObjFixedLength, 51);
    ldy_imm(0xa); // set fixed value and render the sideways part
    jsr(RenderSidewaysPipe, 52);
    if (carry_flag) { goto NoBlankP; } // if carry flag set, not time to draw vertical pipe part
    ldx_imm(0x6); // blank everything above the vertical pipe part
  
VPipeSectLoop:
    lda_imm(0x0); // all the way to the top of the screen
    write_byte(MetatileBuffer + x, a); // because otherwise it will look like exit pipe
    dex();
    if (!neg_flag) { goto VPipeSectLoop; }
    lda_absy(VerticalPipeData); // draw the end of the vertical pipe part
    write_byte(MetatileBuffer + 7, a);
  
NoBlankP:
    goto rts;
  
ExitPipe:
    ldy_imm(0x3); // check if length set, if not set, set it
    jsr(ChkLrgObjFixedLength, 53);
    GetLrgObjAttrib(); // get vertical length, then plow on through RenderSidewaysPipe
  
RenderSidewaysPipe:
    dey(); // decrement twice to make room for shaft at bottom
    dey(); // and store here for now as vertical length
    write_byte(0x5, y);
    ldy_absx(AreaObjectLength); // get length left over and store here
    write_byte(0x6, y);
    ldx_zp(0x5); // get vertical length plus one, use as buffer offset
    inx();
    lda_absy(SidePipeShaftData); // check for value $00 based on horizontal offset
    cmp_imm(0x0);
    if (zero_flag) { goto DrawSidePart; } // if found, do not draw the vertical pipe shaft
    ldx_imm(0x0);
    ldy_zp(0x5); // init buffer offset and get vertical length
    jsr(RenderUnderPart, 54); // and render vertical shaft using tile number in A
    carry_flag = false; // clear carry flag to be used by IntroPipe
  
DrawSidePart:
    ldy_zp(0x6); // render side pipe part at the bottom
    lda_absy(SidePipeTopPart);
    write_byte(MetatileBuffer + x, a); // note that the pipe parts are stored
    lda_absy(SidePipeBottomPart); // backwards horizontally
    write_byte(MetatileBuffer + 1 + x, a);
    goto rts;
  
VerticalPipe:
    jsr(GetPipeHeight, 55);
    lda_zp(0x0); // check to see if value was nullified earlier
    if (zero_flag) { goto WarpPipe; } // (if d3, the usage control bit of second byte, was set)
    iny();
    iny();
    iny();
    iny(); // add four if usage control bit was not set
  
WarpPipe:
    tya(); // save value in stack
    pha();
    lda_abs(AreaNumber);
    ora_abs(WorldNumber); // if at world 1-1, do not add piranha plant ever
    if (zero_flag) { goto DrawPipe; }
    ldy_absx(AreaObjectLength); // if on second column of pipe, branch
    if (zero_flag) { goto DrawPipe; } // (because we only need to do this once)
    FindEmptyEnemySlot(); // check for an empty moving data buffer space
    if (carry_flag) { goto DrawPipe; } // if not found, too many enemies, thus skip
    GetAreaObjXPosition(); // get horizontal pixel coordinate
    carry_flag = false;
    adc_imm(0x8); // add eight to put the piranha plant in the center
    write_byte(Enemy_X_Position + x, a); // store as enemy's horizontal coordinate
    lda_abs(CurrentPageLoc); // add carry to current page number
    adc_imm(0x0);
    write_byte(Enemy_PageLoc + x, a); // store as enemy's page coordinate
    lda_imm(0x1);
    write_byte(Enemy_Y_HighPos + x, a);
    write_byte(Enemy_Flag + x, a); // activate enemy flag
    GetAreaObjYPosition(); // get piranha plant's vertical coordinate and store here
    write_byte(Enemy_Y_Position + x, a);
    lda_imm(PiranhaPlant); // write piranha plant's value into buffer
    write_byte(Enemy_ID + x, a);
    jsr(InitPiranhaPlant, 56);
  
DrawPipe:
    pla(); // get value saved earlier and use as Y
    tay();
    ldx_zp(0x7); // get buffer offset
    lda_absy(VerticalPipeData); // draw the appropriate pipe with the Y we loaded earlier
    write_byte(MetatileBuffer + x, a); // render the top of the pipe
    inx();
    lda_absy(VerticalPipeData + 2); // render the rest of the pipe
    ldy_zp(0x6); // subtract one from length and render the part underneath
    dey();
    goto RenderUnderPart;
  
GetPipeHeight:
    ldy_imm(0x1); // check for length loaded, if not, load
    jsr(ChkLrgObjFixedLength, 57); // pipe length of 2 (horizontal)
    GetLrgObjAttrib();
    tya(); // get saved lower nybble as height
    and_imm(0x7); // save only the three lower bits as
    write_byte(0x6, a); // vertical length, then load Y with
    ldy_absx(AreaObjectLength); // length left over
    goto rts;
    // --------------------------------
  
Hole_Water:
    jsr(ChkLrgObjLength, 58); // get low nybble and save as length
    lda_imm(0x86); // render waves
    write_byte(MetatileBuffer + 10, a);
    ldx_imm(0xb);
    ldy_imm(0x1); // now render the water underneath
    lda_imm(0x87);
    goto RenderUnderPart;
    // --------------------------------
  
QuestionBlockRow_High:
    lda_imm(0x3); // start on the fourth row
    goto QuestionBlockRow_LowSkip; //  .db $2c ;BIT instruction opcode
  
QuestionBlockRow_Low:
    lda_imm(0x7); // start on the eighth row
  
QuestionBlockRow_LowSkip:
    pha(); // save whatever row to the stack for now
    jsr(ChkLrgObjLength, 59); // get low nybble and save as length
    pla();
    tax(); // render question boxes with coins
    lda_imm(0xc0);
    write_byte(MetatileBuffer + x, a);
    goto rts;
    // --------------------------------
  
Bridge_High:
    lda_imm(0x6); // start on the seventh row from top of screen
    goto Bridge_LowSkip; //  .db $2c ;BIT instruction opcode
  
Bridge_Middle:
    lda_imm(0x7); // start on the eighth row
    goto Bridge_LowSkip; //  .db $2c ;BIT instruction opcode
  
Bridge_Low:
    lda_imm(0x9); // start on the tenth row
  
Bridge_LowSkip:
    pha(); // save whatever row to the stack for now
    jsr(ChkLrgObjLength, 60); // get low nybble and save as length
    pla();
    tax(); // render bridge railing
    lda_imm(0xb);
    write_byte(MetatileBuffer + x, a);
    inx();
    ldy_imm(0x0); // now render the bridge itself
    lda_imm(0x63);
    goto RenderUnderPart;
    // --------------------------------
  
FlagBalls_Residual:
    GetLrgObjAttrib(); // get low nybble from object byte
    ldx_imm(0x2); // render flag balls on third row from top
    lda_imm(0x6d); // of screen downwards based on low nybble
    goto RenderUnderPart;
    // --------------------------------
  
FlagpoleObject:
    lda_imm(0x24); // render flagpole ball on top
    write_byte(MetatileBuffer, a);
    ldx_imm(0x1); // now render the flagpole shaft
    ldy_imm(0x8);
    lda_imm(0x25);
    jsr(RenderUnderPart, 61);
    lda_imm(0x61); // render solid block at the bottom
    write_byte(MetatileBuffer + 10, a);
    GetAreaObjXPosition();
    carry_flag = true; // get pixel coordinate of where the flagpole is,
    sbc_imm(0x8); // subtract eight pixels and use as horizontal
    write_byte(Enemy_X_Position + 5, a); // coordinate for the flag
    lda_abs(CurrentPageLoc);
    sbc_imm(0x0); // subtract borrow from page location and use as
    write_byte(Enemy_PageLoc + 5, a); // page location for the flag
    lda_imm(0x30);
    write_byte(Enemy_Y_Position + 5, a); // set vertical coordinate for flag
    lda_imm(0xb0);
    write_byte(FlagpoleFNum_Y_Pos, a); // set initial vertical coordinate for flagpole's floatey number
    lda_imm(FlagpoleFlagObject);
    write_byte(Enemy_ID + 5, a); // set flag identifier, note that identifier and coordinates
    inc_zp(Enemy_Flag + 5); // use last space in enemy object buffer
    goto rts;
    // --------------------------------
  
EndlessRope:
    ldx_imm(0x0); // render rope from the top to the bottom of screen
    ldy_imm(0xf);
    goto DrawRope;
  
BalancePlatRope:
    txa(); // save object buffer offset for now
    pha();
    ldx_imm(0x1); // blank out all from second row to the bottom
    ldy_imm(0xf); // with blank used for balance platform rope
    lda_imm(0x44);
    jsr(RenderUnderPart, 62);
    pla(); // get back object buffer offset
    tax();
    GetLrgObjAttrib(); // get vertical length from lower nybble
    ldx_imm(0x1);
  
DrawRope:
    lda_imm(0x40); // render the actual rope
    goto RenderUnderPart;
    // --------------------------------
  
RowOfCoins:
    ldy_abs(AreaType); // get area type
    lda_absy(CoinMetatileData); // load appropriate coin metatile
    goto GetRow;
    // --------------------------------
  
CastleBridgeObj:
    ldy_imm(0xc); // load length of 13 columns
    jsr(ChkLrgObjFixedLength, 63);
    goto ChainObj;
  
AxeObj:
    lda_imm(0x8); // load bowser's palette into sprite portion of palette
    write_byte(VRAM_Buffer_AddrCtrl, a);
  
ChainObj:
    ldy_zp(0x0); // get value loaded earlier from decoder
    ldx_absy(C_ObjectRow - 2); // get appropriate row and metatile for object
    lda_absy(C_ObjectMetatile - 2);
    goto ColObj;
  
EmptyBlock:
    GetLrgObjAttrib(); // get row location
    ldx_zp(0x7);
    lda_imm(0xc4);
  
ColObj:
    ldy_imm(0x0); // column length of 1
    goto RenderUnderPart;
    // --------------------------------
  
RowOfBricks:
    ldy_abs(AreaType); // load area type obtained from area offset pointer
    lda_abs(CloudTypeOverride); // check for cloud type override
    if (zero_flag) { goto DrawBricks; }
    ldy_imm(0x4); // if cloud type, override area type
  
DrawBricks:
    lda_absy(BrickMetatiles); // get appropriate metatile
    goto GetRow; // and go render it
  
RowOfSolidBlocks:
    ldy_abs(AreaType); // load area type obtained from area offset pointer
    lda_absy(SolidBlockMetatiles); // get metatile
  
GetRow:
    pha(); // store metatile here
    jsr(ChkLrgObjLength, 64); // get row number, load length
  
DrawRow:
    ldx_zp(0x7);
    ldy_imm(0x0); // set vertical height of 1
    pla();
    goto RenderUnderPart; // render object
  
ColumnOfBricks:
    ldy_abs(AreaType); // load area type obtained from area offset
    lda_absy(BrickMetatiles); // get metatile (no cloud override as for row)
    goto GetRow2;
  
ColumnOfSolidBlocks:
    ldy_abs(AreaType); // load area type obtained from area offset
    lda_absy(SolidBlockMetatiles); // get metatile
  
GetRow2:
    pha(); // save metatile to stack for now
    GetLrgObjAttrib(); // get length and row
    pla(); // restore metatile
    ldx_zp(0x7); // get starting row
    goto RenderUnderPart; // now render the column
    // --------------------------------
  
BulletBillCannon:
    GetLrgObjAttrib(); // get row and length of bullet bill cannon
    ldx_zp(0x7); // start at first row
    lda_imm(0x64); // render bullet bill cannon
    write_byte(MetatileBuffer + x, a);
    inx();
    dey(); // done yet?
    if (neg_flag) { goto SetupCannon; }
    lda_imm(0x65); // if not, render middle part
    write_byte(MetatileBuffer + x, a);
    inx();
    dey(); // done yet?
    if (neg_flag) { goto SetupCannon; }
    lda_imm(0x66); // if not, render bottom until length expires
    jsr(RenderUnderPart, 65);
  
SetupCannon:
    ldx_abs(Cannon_Offset); // get offset for data used by cannons and whirlpools
    GetAreaObjYPosition(); // get proper vertical coordinate for cannon
    write_byte(Cannon_Y_Position + x, a); // and store it here
    lda_abs(CurrentPageLoc);
    write_byte(Cannon_PageLoc + x, a); // store page number for cannon here
    GetAreaObjXPosition(); // get proper horizontal coordinate for cannon
    write_byte(Cannon_X_Position + x, a); // and store it here
    inx();
    cpx_imm(0x6); // increment and check offset
    if (!carry_flag) { goto StrCOffset; } // if not yet reached sixth cannon, branch to save offset
    ldx_imm(0x0); // otherwise initialize it
  
StrCOffset:
    write_byte(Cannon_Offset, x); // save new offset and leave
    goto rts;
    // --------------------------------
  
StaircaseObject:
    jsr(ChkLrgObjLength, 66); // check and load length
    if (!carry_flag) { goto NextStair; } // if length already loaded, skip init part
    lda_imm(0x9); // start past the end for the bottom
    write_byte(StaircaseControl, a); // of the staircase
  
NextStair:
    dec_abs(StaircaseControl); // move onto next step (or first if starting)
    ldy_abs(StaircaseControl);
    ldx_absy(StaircaseRowData); // get starting row and height to render
    lda_absy(StaircaseHeightData);
    tay();
    lda_imm(0x61); // now render solid block staircase
    goto RenderUnderPart;
    // --------------------------------
    // --------------------------------
    // $07 - used to save ID of brick object
  
Hidden1UpBlock:
    lda_abs(Hidden1UpFlag); // if flag not set, do not render object
    if (zero_flag) { goto ExitDecBlock; }
    lda_imm(0x0); // if set, init for the next one
    write_byte(Hidden1UpFlag, a);
    goto BrickWithItem; // jump to code shared with unbreakable bricks
  
QuestionBlock:
    jsr(GetAreaObjectID, 67); // get value from level decoder routine
    goto DrawQBlk; // go to render it
  
BrickWithCoins:
    lda_imm(0x0); // initialize multi-coin timer flag
    write_byte(BrickCoinTimerFlag, a);
  
BrickWithItem:
    jsr(GetAreaObjectID, 68); // save area object ID
    write_byte(0x7, y);
    lda_imm(0x0); // load default adder for bricks with lines
    ldy_abs(AreaType); // check level type for ground level
    dey();
    if (zero_flag) { goto BWithL; } // if ground type, do not start with 5
    lda_imm(0x5); // otherwise use adder for bricks without lines
  
BWithL:
    carry_flag = false; // add object ID to adder
    adc_zp(0x7);
    tay(); // use as offset for metatile
  
DrawQBlk:
    lda_absy(BrickQBlockMetatiles); // get appropriate metatile for brick (question block
    pha(); // if branched to here from question block routine)
    GetLrgObjAttrib(); // get row from location byte
    goto DrawRow; // now render the object
  
GetAreaObjectID:
    lda_zp(0x0); // get value saved from area parser routine
    carry_flag = true;
    sbc_imm(0x0); // possibly residual code
    tay(); // save to Y
  
ExitDecBlock:
    goto rts;
    // --------------------------------
  
Hole_Empty:
    jsr(ChkLrgObjLength, 69); // get lower nybble and save as length
    if (!carry_flag) { goto NoWhirlP; } // skip this part if length already loaded
    lda_abs(AreaType); // check for water type level
    if (!zero_flag) { goto NoWhirlP; } // if not water type, skip this part
    ldx_abs(Whirlpool_Offset); // get offset for data used by cannons and whirlpools
    GetAreaObjXPosition(); // get proper vertical coordinate of where we're at
    carry_flag = true;
    sbc_imm(0x10); // subtract 16 pixels
    write_byte(Whirlpool_LeftExtent + x, a); // store as left extent of whirlpool
    lda_abs(CurrentPageLoc); // get page location of where we're at
    sbc_imm(0x0); // subtract borrow
    write_byte(Whirlpool_PageLoc + x, a); // save as page location of whirlpool
    iny();
    iny(); // increment length by 2
    tya();
    asl_acc(); // multiply by 16 to get size of whirlpool
    asl_acc(); // note that whirlpool will always be
    asl_acc(); // two blocks bigger than actual size of hole
    asl_acc(); // and extend one block beyond each edge
    write_byte(Whirlpool_Length + x, a); // save size of whirlpool here
    inx();
    cpx_imm(0x5); // increment and check offset
    if (!carry_flag) { goto StrWOffset; } // if not yet reached fifth whirlpool, branch to save offset
    ldx_imm(0x0); // otherwise initialize it
  
StrWOffset:
    write_byte(Whirlpool_Offset, x); // save new offset here
  
NoWhirlP:
    ldx_abs(AreaType); // get appropriate metatile, then
    lda_absx(HoleMetatiles); // render the hole proper
    ldx_imm(0x8);
    ldy_imm(0xf); // start at ninth row and go to bottom, run RenderUnderPart
    // --------------------------------
  
RenderUnderPart:
    write_byte(AreaObjectHeight, y); // store vertical length to render
    ldy_absx(MetatileBuffer); // check current spot to see if there's something
    if (zero_flag) { goto DrawThisRow; } // we need to keep, if nothing, go ahead
    cpy_imm(0x17);
    if (zero_flag) { goto WaitOneRow; } // if middle part (tree ledge), wait until next row
    cpy_imm(0x1a);
    if (zero_flag) { goto WaitOneRow; } // if middle part (mushroom ledge), wait until next row
    cpy_imm(0xc0);
    if (zero_flag) { goto DrawThisRow; } // if question block w/ coin, overwrite
    cpy_imm(0xc0);
    if (carry_flag) { goto WaitOneRow; } // if any other metatile with palette 3, wait until next row
    cpy_imm(0x54);
    if (!zero_flag) { goto DrawThisRow; } // if cracked rock terrain, overwrite
    cmp_imm(0x50);
    if (zero_flag) { goto WaitOneRow; } // if stem top of mushroom, wait until next row
  
DrawThisRow:
    write_byte(MetatileBuffer + x, a); // render contents of A from routine that called this
  
WaitOneRow:
    inx();
    cpx_imm(0xd); // stop rendering if we're at the bottom of the screen
    if (carry_flag) { goto ExitUPartR; }
    ldy_abs(AreaObjectHeight); // decrement, and stop rendering if there is no more length
    dey();
    if (!neg_flag) { goto RenderUnderPart; }
  
ExitUPartR:
    goto rts;
    // --------------------------------
  
ChkLrgObjLength:
    GetLrgObjAttrib(); // get row location and size (length if branched to from here)
  
ChkLrgObjFixedLength:
    lda_absx(AreaObjectLength); // check for set length counter
    carry_flag = false; // clear carry flag for not just starting
    if (!neg_flag) { goto LenSet; } // if counter not set, load it, otherwise leave alone
    tya(); // save length into length counter
    write_byte(AreaObjectLength + x, a);
    carry_flag = true; // set carry flag if just starting
  
LenSet:
    goto rts;
  
LoadAreaPointer:
    jsr(FindAreaPointer, 70); // find it and store it here
    write_byte(AreaPointer, a);
  
GetAreaType:
    and_imm(0b01100000); // mask out all but d6 and d5
    asl_acc();
    rol_acc();
    rol_acc();
    rol_acc(); // make %0xx00000 into %000000xx
    write_byte(AreaType, a); // save 2 MSB as area type
    goto rts;
  
FindAreaPointer:
    ldy_abs(WorldNumber); // load offset from world variable
    lda_absy(WorldAddrOffsets);
    carry_flag = false; // add area number used to find data
    adc_abs(AreaNumber);
    tay();
    lda_absy(AreaAddrOffsets); // from there we have our area pointer
    goto rts;
  
GetAreaDataAddrs:
    lda_abs(AreaPointer); // use 2 MSB for Y
    jsr(GetAreaType, 71);
    tay();
    lda_abs(AreaPointer); // mask out all but 5 LSB
    and_imm(0b00011111);
    write_byte(AreaAddrsLOffset, a); // save as low offset
    lda_absy(EnemyAddrHOffsets); // load base value with 2 altered MSB,
    carry_flag = false; // then add base value to 5 LSB, result
    adc_abs(AreaAddrsLOffset); // becomes offset for level data
    tay();
    lda_absy(EnemyDataAddrLow); // use offset to load pointer
    write_byte(EnemyDataLow, a);
    lda_absy(EnemyDataAddrHigh);
    write_byte(EnemyDataHigh, a);
    ldy_abs(AreaType); // use area type as offset
    lda_absy(AreaDataHOffsets); // do the same thing but with different base value
    carry_flag = false;
    adc_abs(AreaAddrsLOffset);
    tay();
    lda_absy(AreaDataAddrLow); // use this offset to load another pointer
    write_byte(AreaDataLow, a);
    lda_absy(AreaDataAddrHigh);
    write_byte(AreaDataHigh, a);
    ldy_imm(0x0); // load first byte of header
    lda_indy(AreaData);
    pha(); // save it to the stack for now
    and_imm(0b00000111); // save 3 LSB for foreground scenery or bg color control
    cmp_imm(0x4);
    if (!carry_flag) { goto StoreFore; }
    write_byte(BackgroundColorCtrl, a); // if 4 or greater, save value here as bg color control
    lda_imm(0x0);
  
StoreFore:
    write_byte(ForegroundScenery, a); // if less, save value here as foreground scenery
    pla(); // pull byte from stack and push it back
    pha();
    and_imm(0b00111000); // save player entrance control bits
    lsr_acc(); // shift bits over to LSBs
    lsr_acc();
    lsr_acc();
    write_byte(PlayerEntranceCtrl, a); // save value here as player entrance control
    pla(); // pull byte again but do not push it back
    and_imm(0b11000000); // save 2 MSB for game timer setting
    carry_flag = false;
    rol_acc(); // rotate bits over to LSBs
    rol_acc();
    rol_acc();
    write_byte(GameTimerSetting, a); // save value here as game timer setting
    iny();
    lda_indy(AreaData); // load second byte of header
    pha(); // save to stack
    and_imm(0b00001111); // mask out all but lower nybble
    write_byte(TerrainControl, a);
    pla(); // pull and push byte to copy it to A
    pha();
    and_imm(0b00110000); // save 2 MSB for background scenery type
    lsr_acc();
    lsr_acc(); // shift bits to LSBs
    lsr_acc();
    lsr_acc();
    write_byte(BackgroundScenery, a); // save as background scenery
    pla();
    and_imm(0b11000000);
    carry_flag = false;
    rol_acc(); // rotate bits over to LSBs
    rol_acc();
    rol_acc();
    cmp_imm(0b00000011); // if set to 3, store here
    if (!zero_flag) { goto StoreStyle; } // and nullify other value
    write_byte(CloudTypeOverride, a); // otherwise store value in other place
    lda_imm(0x0);
  
StoreStyle:
    write_byte(AreaStyle, a);
    lda_zp(AreaDataLow); // increment area data address by 2 bytes
    carry_flag = false;
    adc_imm(0x2);
    write_byte(AreaDataLow, a);
    lda_zp(AreaDataHigh);
    adc_imm(0x0);
    write_byte(AreaDataHigh, a);
    goto rts;
    // -------------------------------------------------------------------------------------
    // GAME LEVELS DATA
  
GameMode:
    lda_abs(OperMode_Task);
    // jsr JumpEngine
    switch (a) {
      case 0: goto InitializeArea;
      case 1: goto ScreenRoutines;
      case 2: goto SecondaryGameSetup;
      case 3: goto GameCoreRoutine;
    }
  
GameCoreRoutine:
    ldx_abs(CurrentPlayer); // get which player is on the screen
    lda_absx(SavedJoypadBits); // use appropriate player's controller bits
    write_byte(SavedJoypadBits, a); // as the master controller bits
    jsr(GameRoutines, 72); // execute one of many possible subs
    lda_abs(OperMode_Task); // check major task of operating mode
    cmp_imm(0x3); // if we are supposed to be here,
    if (carry_flag) { goto GameEngine; } // branch to the game engine itself
    goto rts;
  
GameEngine:
    jsr(ProcFireball_Bubble, 73); // process fireballs and air bubbles
    ldx_imm(0x0);
  
ProcELoop:
    write_byte(ObjectOffset, x); // put incremented offset in X as enemy object offset
    jsr(EnemiesAndLoopsCore, 74); // process enemy objects
    jsr(FloateyNumbersRoutine, 75); // process floatey numbers
    inx();
    cpx_imm(0x6); // do these two subroutines until the whole buffer is done
    if (!zero_flag) { goto ProcELoop; }
    jsr(GetPlayerOffscreenBits, 76); // get offscreen bits for player object
    jsr(RelativePlayerPosition, 77); // get relative coordinates for player object
    jsr(PlayerGfxHandler, 78); // draw the player
    jsr(BlockObjMT_Updater, 79); // replace block objects with metatiles if necessary
    ldx_imm(0x1);
    write_byte(ObjectOffset, x); // set offset for second
    jsr(BlockObjectsCore, 80); // process second block object
    dex();
    write_byte(ObjectOffset, x); // set offset for first
    jsr(BlockObjectsCore, 81); // process first block object
    jsr(MiscObjectsCore, 82); // process misc objects (hammer, jumping coins)
    jsr(ProcessCannons, 83); // process bullet bill cannons
    jsr(ProcessWhirlpools, 84); // process whirlpools
    jsr(FlagpoleRoutine, 85); // process the flagpole
    jsr(RunGameTimer, 86); // count down the game timer
    ColorRotation(); // cycle one of the background colors
    lda_zp(Player_Y_HighPos);
    cmp_imm(0x2); // if player is below the screen, don't bother with the music
    if (!neg_flag) { goto NoChgMus; }
    lda_abs(StarInvincibleTimer); // if star mario invincibility timer at zero,
    if (zero_flag) { goto ClrPlrPal; } // skip this part
    cmp_imm(0x4);
    if (!zero_flag) { goto NoChgMus; } // if not yet at a certain point, continue
    lda_abs(IntervalTimerControl); // if interval timer not yet expired,
    if (!zero_flag) { goto NoChgMus; } // branch ahead, don't bother with the music
    GetAreaMusic(); // to re-attain appropriate level music
  
NoChgMus:
    ldy_abs(StarInvincibleTimer); // get invincibility timer
    lda_zp(FrameCounter); // get frame counter
    cpy_imm(0x8); // if timer still above certain point,
    if (carry_flag) { goto CycleTwo; } // branch to cycle player's palette quickly
    lsr_acc(); // otherwise, divide by 8 to cycle every eighth frame
    lsr_acc();
  
CycleTwo:
    lsr_acc(); // if branched here, divide by 2 to cycle every other frame
    jsr(CyclePlayerPalette, 87); // do sub to cycle the palette (note: shares fire flower code)
    goto SaveAB; // then skip this sub to finish up the game engine
  
ClrPlrPal:
    jsr(ResetPalStar, 88); // do sub to clear player's palette bits in attributes
  
SaveAB:
    lda_zp(A_B_Buttons); // save current A and B button
    write_byte(PreviousA_B_Buttons, a); // into temp variable to be used on next frame
    lda_imm(0x0);
    write_byte(Left_Right_Buttons, a); // nullify left and right buttons temp variable
  
UpdScrollVar:
    lda_abs(VRAM_Buffer_AddrCtrl);
    cmp_imm(0x6); // if vram address controller set to 6 (one of two $0341s)
    if (zero_flag) { goto ExitEng; } // then branch to leave
    lda_abs(AreaParserTaskNum); // otherwise check number of tasks
    if (!zero_flag) { goto RunParser; }
    lda_abs(ScrollThirtyTwo); // get horizontal scroll in 0-31 or $00-$20 range
    cmp_imm(0x20); // check to see if exceeded $21
    if (neg_flag) { goto ExitEng; } // branch to leave if not
    lda_abs(ScrollThirtyTwo);
    sbc_imm(0x20); // otherwise subtract $20 to set appropriately
    write_byte(ScrollThirtyTwo, a); // and store
    lda_imm(0x0); // reset vram buffer offset used in conjunction with
    write_byte(VRAM_Buffer2_Offset, a); // level graphics buffer at $0341-$035f
  
RunParser:
    jsr(AreaParserTaskHandler, 89); // update the name table with more level graphics
  
ExitEng:
    goto rts; // and after all that, we're finally done!
    // -------------------------------------------------------------------------------------
  
ScrollHandler:
    lda_abs(Player_X_Scroll); // load value saved here
    carry_flag = false;
    adc_abs(Platform_X_Scroll); // add value used by left/right platforms
    write_byte(Player_X_Scroll, a); // save as new value here to impose force on scroll
    lda_abs(ScrollLock); // check scroll lock flag
    if (!zero_flag) { goto InitScrlAmt; } // skip a bunch of code here if set
    lda_abs(Player_Pos_ForScroll);
    cmp_imm(0x50); // check player's horizontal screen position
    if (!carry_flag) { goto InitScrlAmt; } // if less than 80 pixels to the right, branch
    lda_abs(SideCollisionTimer); // if timer related to player's side collision
    if (!zero_flag) { goto InitScrlAmt; } // not expired, branch
    ldy_abs(Player_X_Scroll); // get value and decrement by one
    dey(); // if value originally set to zero or otherwise
    if (neg_flag) { goto InitScrlAmt; } // negative for left movement, branch
    iny();
    cpy_imm(0x2); // if value $01, branch and do not decrement
    if (!carry_flag) { goto ChkNearMid; }
    dey(); // otherwise decrement by one
  
ChkNearMid:
    lda_abs(Player_Pos_ForScroll);
    cmp_imm(0x70); // check player's horizontal screen position
    if (!carry_flag) { goto ScrollScreen; } // if less than 112 pixels to the right, branch
    ldy_abs(Player_X_Scroll); // otherwise get original value undecremented
  
ScrollScreen:
    tya();
    write_byte(ScrollAmount, a); // save value here
    carry_flag = false;
    adc_abs(ScrollThirtyTwo); // add to value already set here
    write_byte(ScrollThirtyTwo, a); // save as new value here
    tya();
    carry_flag = false;
    adc_abs(ScreenLeft_X_Pos); // add to left side coordinate
    write_byte(ScreenLeft_X_Pos, a); // save as new left side coordinate
    write_byte(HorizontalScroll, a); // save here also
    lda_abs(ScreenLeft_PageLoc);
    adc_imm(0x0); // add carry to page location for left
    write_byte(ScreenLeft_PageLoc, a); // side of the screen
    and_imm(0x1); // get LSB of page location
    write_byte(0x0, a); // save as temp variable for PPU register 1 mirror
    lda_abs(Mirror_PPU_CTRL_REG1); // get PPU register 1 mirror
    and_imm(0b11111110); // save all bits except d0
    ora_zp(0x0); // get saved bit here and save in PPU register 1
    write_byte(Mirror_PPU_CTRL_REG1, a); // mirror to be used to set name table later
    GetScreenPosition(); // figure out where the right side is
    lda_imm(0x8);
    write_byte(ScrollIntervalTimer, a); // set scroll timer (residual, not used elsewhere)
    goto ChkPOffscr; // skip this part
  
InitScrlAmt:
    lda_imm(0x0);
    write_byte(ScrollAmount, a); // initialize value here
  
ChkPOffscr:
    ldx_imm(0x0); // set X for player offset
    GetXOffscreenBits(); // get horizontal offscreen bits for player
    write_byte(0x0, a); // save them here
    ldy_imm(0x0); // load default offset (left side)
    asl_acc(); // if d7 of offscreen bits are set,
    if (carry_flag) { goto KeepOnscr; } // branch with default offset
    iny(); // otherwise use different offset (right side)
    lda_zp(0x0);
    and_imm(0b00100000); // check offscreen bits for d5 set
    if (zero_flag) { goto InitPlatScrl; } // if not set, branch ahead of this part
  
KeepOnscr:
    lda_absy(ScreenEdge_X_Pos); // get left or right side coordinate based on offset
    carry_flag = true;
    sbc_absy(X_SubtracterData); // subtract amount based on offset
    write_byte(Player_X_Position, a); // store as player position to prevent movement further
    lda_absy(ScreenEdge_PageLoc); // get left or right page location based on offset
    sbc_imm(0x0); // subtract borrow
    write_byte(Player_PageLoc, a); // save as player's page location
    lda_zp(Left_Right_Buttons); // check saved controller bits
    cmp_absy(OffscrJoypadBitsData); // against bits based on offset
    if (zero_flag) { goto InitPlatScrl; } // if not equal, branch
    lda_imm(0x0);
    write_byte(Player_X_Speed, a); // otherwise nullify horizontal speed of player
  
InitPlatScrl:
    lda_imm(0x0); // nullify platform force imposed on scroll
    write_byte(Platform_X_Scroll, a);
    goto rts;
  
GameRoutines:
    lda_zp(GameEngineSubroutine); // run routine based on number (a few of these routines are   
    // jsr JumpEngine
    switch (a) {
      case 0: goto Entrance_GameTimerSetup;
      case 1: goto Vine_AutoClimb;
      case 2: goto SideExitPipeEntry;
      case 3: goto VerticalPipeEntry;
      case 4: goto FlagpoleSlide;
      case 5: goto PlayerEndLevel;
      case 6: goto PlayerLoseLife;
      case 7: goto PlayerEntrance;
      case 8: goto PlayerCtrlRoutine;
      case 9: goto PlayerChangeSize;
      case 10: goto PlayerInjuryBlink;
      case 11: goto PlayerDeath;
      case 12: goto PlayerFireFlower;
    }
  
PlayerEntrance:
    lda_abs(AltEntranceControl); // check for mode of alternate entry
    cmp_imm(0x2);
    if (zero_flag) { goto EntrMode2; } // if found, branch to enter from pipe or with vine
    lda_imm(0x0);
    ldy_zp(Player_Y_Position); // if vertical position above a certain
    cpy_imm(0x30); // point, nullify controller bits and continue
    if (!carry_flag) { goto AutoControlPlayer; } // with player movement code, do not return
    lda_abs(PlayerEntranceCtrl); // check player entry bits from header
    cmp_imm(0x6);
    if (zero_flag) { goto ChkBehPipe; } // if set to 6 or 7, execute pipe intro code
    cmp_imm(0x7); // otherwise branch to normal entry
    if (!zero_flag) { goto PlayerRdy; }
  
ChkBehPipe:
    lda_abs(Player_SprAttrib); // check for sprite attributes
    if (!zero_flag) { goto IntroEntr; } // branch if found
    lda_imm(0x1);
    goto AutoControlPlayer; // force player to walk to the right
  
IntroEntr:
    jsr(EnterSidePipe, 90); // execute sub to move player to the right
    dec_abs(ChangeAreaTimer); // decrement timer for change of area
    if (!zero_flag) { goto ExitEntr; } // branch to exit if not yet expired
    inc_abs(DisableIntermediate); // set flag to skip world and lives display
    goto NextArea; // jump to increment to next area and set modes
  
EntrMode2:
    lda_abs(JoypadOverride); // if controller override bits set here,
    if (!zero_flag) { goto VineEntr; } // branch to enter with vine
    lda_imm(0xff); // otherwise, set value here then execute sub
    MovePlayerYAxis(); // to move player upwards (note $ff = -1)
    lda_zp(Player_Y_Position); // check to see if player is at a specific coordinate
    cmp_imm(0x91); // if player risen to a certain point (this requires pipes
    if (!carry_flag) { goto PlayerRdy; } // to be at specific height to look/function right) branch
    goto rts; // to the last part, otherwise leave
  
VineEntr:
    lda_abs(VineHeight);
    cmp_imm(0x60); // check vine height
    if (!zero_flag) { goto ExitEntr; } // if vine not yet reached maximum height, branch to leave
    lda_zp(Player_Y_Position); // get player's vertical coordinate
    cmp_imm(0x99); // check player's vertical coordinate against preset value
    ldy_imm(0x0); // load default values to be written to 
    lda_imm(0x1); // this value moves player to the right off the vine
    if (!carry_flag) { goto OffVine; } // if vertical coordinate < preset value, use defaults
    lda_imm(0x3);
    write_byte(Player_State, a); // otherwise set player state to climbing
    iny(); // increment value in Y
    lda_imm(0x8); // set block in block buffer to cover hole, then 
    write_byte(Block_Buffer_1 + 0xb4, a); // use same value to force player to climb
  
OffVine:
    write_byte(DisableCollisionDet, y); // set collision detection disable flag
    jsr(AutoControlPlayer, 91); // use contents of A to move player up or right, execute sub
    lda_zp(Player_X_Position);
    cmp_imm(0x48); // check player's horizontal position
    if (!carry_flag) { goto ExitEntr; } // if not far enough to the right, branch to leave
  
PlayerRdy:
    lda_imm(0x8); // set routine to be executed by game engine next frame
    write_byte(GameEngineSubroutine, a);
    lda_imm(0x1); // set to face player to the right
    write_byte(PlayerFacingDir, a);
    lsr_acc(); // init A
    write_byte(AltEntranceControl, a); // init mode of entry
    write_byte(DisableCollisionDet, a); // init collision detection disable flag
    write_byte(JoypadOverride, a); // nullify controller override bits
  
ExitEntr:
    goto rts; // leave!
    // -------------------------------------------------------------------------------------
    // $07 - used to hold upper limit of high byte when player falls down hole
  
AutoControlPlayer:
    write_byte(SavedJoypadBits, a); // override controller bits with contents of A if executing here
  
PlayerCtrlRoutine:
    lda_zp(GameEngineSubroutine); // check task here
    cmp_imm(0xb); // if certain value is set, branch to skip controller bit loading
    if (zero_flag) { goto SizeChk; }
    lda_abs(AreaType); // are we in a water type area?
    if (!zero_flag) { goto SaveJoyp; } // if not, branch
    ldy_zp(Player_Y_HighPos);
    dey(); // if not in vertical area between
    if (!zero_flag) { goto DisJoyp; } // status bar and bottom, branch
    lda_zp(Player_Y_Position);
    cmp_imm(0xd0); // if nearing the bottom of the screen or
    if (!carry_flag) { goto SaveJoyp; } // not in the vertical area between status bar or bottom,
  
DisJoyp:
    lda_imm(0x0); // disable controller bits
    write_byte(SavedJoypadBits, a);
  
SaveJoyp:
    lda_abs(SavedJoypadBits); // otherwise store A and B buttons in $0a
    and_imm(0b11000000);
    write_byte(A_B_Buttons, a);
    lda_abs(SavedJoypadBits); // store left and right buttons in $0c
    and_imm(0b00000011);
    write_byte(Left_Right_Buttons, a);
    lda_abs(SavedJoypadBits); // store up and down buttons in $0b
    and_imm(0b00001100);
    write_byte(Up_Down_Buttons, a);
    and_imm(0b00000100); // check for pressing down
    if (zero_flag) { goto SizeChk; } // if not, branch
    lda_zp(Player_State); // check player's state
    if (!zero_flag) { goto SizeChk; } // if not on the ground, branch
    ldy_zp(Left_Right_Buttons); // check left and right
    if (zero_flag) { goto SizeChk; } // if neither pressed, branch
    lda_imm(0x0);
    write_byte(Left_Right_Buttons, a); // if pressing down while on the ground,
    write_byte(Up_Down_Buttons, a); // nullify directional bits
  
SizeChk:
    jsr(PlayerMovementSubs, 92); // run movement subroutines
    ldy_imm(0x1); // is player small?
    lda_abs(PlayerSize);
    if (!zero_flag) { goto ChkMoveDir; }
    ldy_imm(0x0); // check for if crouching
    lda_abs(CrouchingFlag);
    if (zero_flag) { goto ChkMoveDir; } // if not, branch ahead
    ldy_imm(0x2); // if big and crouching, load y with 2
  
ChkMoveDir:
    write_byte(Player_BoundBoxCtrl, y); // set contents of Y as player's bounding box size control
    lda_imm(0x1); // set moving direction to right by default
    ldy_zp(Player_X_Speed); // check player's horizontal speed
    if (zero_flag) { goto PlayerSubs; } // if not moving at all horizontally, skip this part
    if (!neg_flag) { goto SetMoveDir; } // if moving to the right, use default moving direction
    asl_acc(); // otherwise change to move to the left
  
SetMoveDir:
    write_byte(Player_MovingDir, a); // set moving direction
  
PlayerSubs:
    jsr(ScrollHandler, 93); // move the screen if necessary
    jsr(GetPlayerOffscreenBits, 94); // get player's offscreen bits
    jsr(RelativePlayerPosition, 95); // get coordinates relative to the screen
    ldx_imm(0x0); // set offset for player object
    jsr(BoundingBoxCore, 96); // get player's bounding box coordinates
    jsr(PlayerBGCollision, 97); // do collision detection and process
    lda_zp(Player_Y_Position);
    cmp_imm(0x40); // check to see if player is higher than 64th pixel
    if (!carry_flag) { goto PlayerHole; } // if so, branch ahead
    lda_zp(GameEngineSubroutine);
    cmp_imm(0x5); // if running end-of-level routine, branch ahead
    if (zero_flag) { goto PlayerHole; }
    cmp_imm(0x7); // if running player entrance routine, branch ahead
    if (zero_flag) { goto PlayerHole; }
    cmp_imm(0x4); // if running routines $00-$03, branch ahead
    if (!carry_flag) { goto PlayerHole; }
    lda_abs(Player_SprAttrib);
    and_imm(0b11011111); // otherwise nullify player's
    write_byte(Player_SprAttrib, a); // background priority flag
  
PlayerHole:
    lda_zp(Player_Y_HighPos); // check player's vertical high byte
    cmp_imm(0x2); // for below the screen
    if (neg_flag) { goto ExitCtrl; } // branch to leave if not that far down
    ldx_imm(0x1);
    write_byte(ScrollLock, x); // set scroll lock
    ldy_imm(0x4);
    write_byte(0x7, y); // set value here
    ldx_imm(0x0); // use X as flag, and clear for cloud level
    ldy_abs(GameTimerExpiredFlag); // check game timer expiration flag
    if (!zero_flag) { goto HoleDie; } // if set, branch
    ldy_abs(CloudTypeOverride); // check for cloud type override
    if (!zero_flag) { goto ChkHoleX; } // skip to last part if found
  
HoleDie:
    inx(); // set flag in X for player death
    ldy_zp(GameEngineSubroutine);
    cpy_imm(0xb); // check for some other routine running
    if (zero_flag) { goto ChkHoleX; } // if so, branch ahead
    ldy_abs(DeathMusicLoaded); // check value here
    if (!zero_flag) { goto HoleBottom; } // if already set, branch to next part
    iny();
    write_byte(EventMusicQueue, y); // otherwise play death music
    write_byte(DeathMusicLoaded, y); // and set value here
  
HoleBottom:
    ldy_imm(0x6);
    write_byte(0x7, y); // change value here
  
ChkHoleX:
    cmp_zp(0x7); // compare vertical high byte with value set here
    if (neg_flag) { goto ExitCtrl; } // if less, branch to leave
    dex(); // otherwise decrement flag in X
    if (neg_flag) { goto CloudExit; } // if flag was clear, branch to set modes and other values
    ldy_abs(EventMusicBuffer); // check to see if music is still playing
    if (!zero_flag) { goto ExitCtrl; } // branch to leave if so
    lda_imm(0x6); // otherwise set to run lose life routine
    write_byte(GameEngineSubroutine, a); // on next frame
  
ExitCtrl:
    goto rts; // leave
  
CloudExit:
    lda_imm(0x0);
    write_byte(JoypadOverride, a); // clear controller override bits if any are set
    jsr(SetEntr, 98); // do sub to set secondary mode
    inc_abs(AltEntranceControl); // set mode of entry to 3
    goto rts;
    // -------------------------------------------------------------------------------------
  
Vine_AutoClimb:
    lda_zp(Player_Y_HighPos); // check to see whether player reached position
    if (!zero_flag) { goto AutoClimb; } // above the status bar yet and if so, set modes
    lda_zp(Player_Y_Position);
    cmp_imm(0xe4);
    if (!carry_flag) { goto SetEntr; }
  
AutoClimb:
    lda_imm(0b00001000); // set controller bits override to up
    write_byte(JoypadOverride, a);
    ldy_imm(0x3); // set player state to climbing
    write_byte(Player_State, y);
    goto AutoControlPlayer;
  
SetEntr:
    lda_imm(0x2); // set starting position to override
    write_byte(AltEntranceControl, a);
    goto ChgAreaMode; // set modes
    // -------------------------------------------------------------------------------------
  
VerticalPipeEntry:
    lda_imm(0x1); // set 1 as movement amount
    MovePlayerYAxis(); // do sub to move player downwards
    jsr(ScrollHandler, 99); // do sub to scroll screen with saved force if necessary
    ldy_imm(0x0); // load default mode of entry
    lda_abs(WarpZoneControl); // check warp zone control variable/flag
    if (!zero_flag) { goto ChgAreaPipe; } // if set, branch to use mode 0
    iny();
    lda_abs(AreaType); // check for castle level type
    cmp_imm(0x3);
    if (!zero_flag) { goto ChgAreaPipe; } // if not castle type level, use mode 1
    iny();
    goto ChgAreaPipe; // otherwise use mode 2
  
SideExitPipeEntry:
    jsr(EnterSidePipe, 100); // execute sub to move player to the right
    ldy_imm(0x2);
  
ChgAreaPipe:
    dec_abs(ChangeAreaTimer); // decrement timer for change of area
    if (!zero_flag) { goto ExitCAPipe; }
    write_byte(AltEntranceControl, y); // when timer expires set mode of alternate entry
  
ChgAreaMode:
    inc_abs(DisableScreenFlag); // set flag to disable screen output
    lda_imm(0x0);
    write_byte(OperMode_Task, a); // set secondary mode of operation
    write_byte(Sprite0HitDetectFlag, a); // disable sprite 0 check
  
ExitCAPipe:
    goto rts; // leave
  
EnterSidePipe:
    lda_imm(0x8); // set player's horizontal speed
    write_byte(Player_X_Speed, a);
    ldy_imm(0x1); // set controller right button by default
    lda_zp(Player_X_Position); // mask out higher nybble of player's
    and_imm(0b00001111); // horizontal position
    if (!zero_flag) { goto RightPipe; }
    write_byte(Player_X_Speed, a); // if lower nybble = 0, set as horizontal speed
    tay(); // and nullify controller bit override here
  
RightPipe:
    tya(); // use contents of Y to
    jsr(AutoControlPlayer, 101); // execute player control routine with ctrl bits nulled
    goto rts;
    // -------------------------------------------------------------------------------------
  
PlayerChangeSize:
    lda_abs(TimerControl); // check master timer control
    cmp_imm(0xf8); // for specific moment in time
    if (!zero_flag) { goto EndChgSize; } // branch if before or after that point
    goto InitChangeSize; // otherwise run code to get growing/shrinking going
  
EndChgSize:
    cmp_imm(0xc4); // check again for another specific moment
    if (!zero_flag) { goto ExitChgSize; } // and branch to leave if before or after that point
    jsr(DonePlayerTask, 102); // otherwise do sub to init timer control and set routine
  
ExitChgSize:
    goto rts; // and then leave
    // -------------------------------------------------------------------------------------
  
PlayerInjuryBlink:
    lda_abs(TimerControl); // check master timer control
    cmp_imm(0xf0); // for specific moment in time
    if (carry_flag) { goto ExitBlink; } // branch if before that point
    cmp_imm(0xc8); // check again for another specific point
    if (zero_flag) { goto DonePlayerTask; } // branch if at that point, and not before or after
    goto PlayerCtrlRoutine; // otherwise run player control routine
  
ExitBlink:
    if (!zero_flag) { goto ExitBoth; } // do unconditional branch to leave
  
InitChangeSize:
    ldy_abs(PlayerChangeSizeFlag); // if growing/shrinking flag already set
    if (!zero_flag) { goto ExitBoth; } // then branch to leave
    write_byte(PlayerAnimCtrl, y); // otherwise initialize player's animation frame control
    inc_abs(PlayerChangeSizeFlag); // set growing/shrinking flag
    lda_abs(PlayerSize);
    eor_imm(0x1); // invert player's size
    write_byte(PlayerSize, a);
  
ExitBoth:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // $00 - used in CyclePlayerPalette to store current palette to cycle
  
PlayerDeath:
    lda_abs(TimerControl); // check master timer control
    cmp_imm(0xf0); // for specific moment in time
    if (carry_flag) { goto ExitDeath; } // branch to leave if before that point
    goto PlayerCtrlRoutine; // otherwise run player control routine
  
DonePlayerTask:
    lda_imm(0x0);
    write_byte(TimerControl, a); // initialize master timer control to continue timers
    lda_imm(0x8);
    write_byte(GameEngineSubroutine, a); // set player control routine to run next frame
    goto rts; // leave
  
PlayerFireFlower:
    lda_abs(TimerControl); // check master timer control
    cmp_imm(0xc0); // for specific moment in time
    if (zero_flag) { goto ResetPalFireFlower; } // branch if at moment, not before or after
    lda_zp(FrameCounter); // get frame counter
    lsr_acc();
    lsr_acc(); // divide by four to change every four frames
  
CyclePlayerPalette:
    and_imm(0x3); // mask out all but d1-d0 (previously d3-d2)
    write_byte(0x0, a); // store result here to use as palette bits
    lda_abs(Player_SprAttrib); // get player attributes
    and_imm(0b11111100); // save any other bits but palette bits
    ora_zp(0x0); // add palette bits
    write_byte(Player_SprAttrib, a); // store as new player attributes
    goto rts; // and leave
  
ResetPalFireFlower:
    jsr(DonePlayerTask, 103); // do sub to init timer control and run player control routine
  
ResetPalStar:
    lda_abs(Player_SprAttrib); // get player attributes
    and_imm(0b11111100); // mask out palette bits to force palette 0
    write_byte(Player_SprAttrib, a); // store as new player attributes
    goto rts; // and leave
  
ExitDeath:
    goto rts; // leave from death routine
    // -------------------------------------------------------------------------------------
  
FlagpoleSlide:
    lda_zp(Enemy_ID + 5); // check special use enemy slot
    cmp_imm(FlagpoleFlagObject); // for flagpole flag object
    if (!zero_flag) { goto NoFPObj; } // if not found, branch to something residual
    lda_abs(FlagpoleSoundQueue); // load flagpole sound
    write_byte(Square1SoundQueue, a); // into square 1's sfx queue
    lda_imm(0x0);
    write_byte(FlagpoleSoundQueue, a); // init flagpole sound queue
    ldy_zp(Player_Y_Position);
    cpy_imm(0x9e); // check to see if player has slid down
    if (carry_flag) { goto SlidePlayer; } // far enough, and if so, branch with no controller bits set
    lda_imm(0x4); // otherwise force player to climb down (to slide)
  
SlidePlayer:
    goto AutoControlPlayer; // jump to player control routine
  
NoFPObj:
    inc_zp(GameEngineSubroutine); // increment to next routine (this may
    goto rts; // be residual code)
    // -------------------------------------------------------------------------------------
  
PlayerEndLevel:
    lda_imm(0x1); // force player to walk to the right
    jsr(AutoControlPlayer, 104);
    lda_zp(Player_Y_Position); // check player's vertical position
    cmp_imm(0xae);
    if (!carry_flag) { goto ChkStop; } // if player is not yet off the flagpole, skip this part
    lda_abs(ScrollLock); // if scroll lock not set, branch ahead to next part
    if (zero_flag) { goto ChkStop; } // because we only need to do this part once
    lda_imm(EndOfLevelMusic);
    write_byte(EventMusicQueue, a); // load win level music in event music queue
    lda_imm(0x0);
    write_byte(ScrollLock, a); // turn off scroll lock to skip this part later
  
ChkStop:
    lda_abs(Player_CollisionBits); // get player collision bits
    lsr_acc(); // check for d0 set
    if (carry_flag) { goto RdyNextA; } // if d0 set, skip to next part
    lda_abs(StarFlagTaskControl); // if star flag task control already set,
    if (!zero_flag) { goto InCastle; } // go ahead with the rest of the code
    inc_abs(StarFlagTaskControl); // otherwise set task control now (this gets ball rolling!)
  
InCastle:
    lda_imm(0b00100000); // set player's background priority bit to
    write_byte(Player_SprAttrib, a); // give illusion of being inside the castle
  
RdyNextA:
    lda_abs(StarFlagTaskControl);
    cmp_imm(0x5); // if star flag task control not yet set
    if (!zero_flag) { goto ExitNA; } // beyond last valid task number, branch to leave
    inc_abs(LevelNumber); // increment level number used for game logic
    lda_abs(LevelNumber);
    cmp_imm(0x3); // check to see if we have yet reached level -4
    if (!zero_flag) { goto NextArea; } // and skip this last part here if not
    ldy_abs(WorldNumber); // get world number as offset
    lda_abs(CoinTallyFor1Ups); // check third area coin tally for bonus 1-ups
    cmp_absy(Hidden1UpCoinAmts); // against minimum value, if player has not collected
    if (!carry_flag) { goto NextArea; } // at least this number of coins, leave flag clear
    inc_abs(Hidden1UpFlag); // otherwise set hidden 1-up box control flag
  
NextArea:
    inc_abs(AreaNumber); // increment area number used for address loader
    jsr(LoadAreaPointer, 105); // get new level pointer
    inc_abs(FetchNewGameTimerFlag); // set flag to load new game timer
    jsr(ChgAreaMode, 106); // do sub to set secondary mode, disable screen and sprite 0
    write_byte(HalfwayPage, a); // reset halfway page to 0 (beginning)
    lda_imm(Silence);
    write_byte(EventMusicQueue, a); // silence music and leave
  
ExitNA:
    goto rts;
    // -------------------------------------------------------------------------------------
  
PlayerMovementSubs:
    lda_imm(0x0); // set A to init crouch flag by default
    ldy_abs(PlayerSize); // is player small?
    if (!zero_flag) { goto SetCrouch; } // if so, branch
    lda_zp(Player_State); // check state of player
    if (!zero_flag) { goto ProcMove; } // if not on the ground, branch
    lda_zp(Up_Down_Buttons); // load controller bits for up and down
    and_imm(0b00000100); // single out bit for down button
  
SetCrouch:
    write_byte(CrouchingFlag, a); // store value in crouch flag
  
ProcMove:
    jsr(PlayerPhysicsSub, 107); // run sub related to jumping and swimming
    lda_abs(PlayerChangeSizeFlag); // if growing/shrinking flag set,
    if (!zero_flag) { goto NoMoveSub; } // branch to leave
    lda_zp(Player_State);
    cmp_imm(0x3); // get player state
    if (zero_flag) { goto MoveSubs; } // if climbing, branch ahead, leave timer unset
    ldy_imm(0x18);
    write_byte(ClimbSideTimer, y); // otherwise reset timer now
  
MoveSubs:
    // jsr JumpEngine
    switch (a) {
      case 0: goto OnGroundStateSub;
      case 1: goto JumpSwimSub;
      case 2: goto FallingSub;
      case 3: goto ClimbingSub;
    }
  
NoMoveSub:
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used by ClimbingSub to store high vertical adder
  
OnGroundStateSub:
    GetPlayerAnimSpeed(); // do a sub to set animation frame timing
    lda_zp(Left_Right_Buttons);
    if (zero_flag) { goto GndMove; } // if left/right controller bits not set, skip instruction
    write_byte(PlayerFacingDir, a); // otherwise set new facing direction
  
GndMove:
    ImposeFriction(); // do a sub to impose friction on player's walk/run
    jsr(MovePlayerHorizontally, 108); // do another sub to move player horizontally
    write_byte(Player_X_Scroll, a); // set returned value as player's movement speed for scroll
    goto rts;
    // --------------------------------
  
FallingSub:
    lda_abs(VerticalForceDown);
    write_byte(VerticalForce, a); // dump vertical movement force for falling into main one
    goto LRAir; // movement force, then skip ahead to process left/right movement
    // --------------------------------
  
JumpSwimSub:
    ldy_zp(Player_Y_Speed); // if player's vertical speed zero
    if (!neg_flag) { goto DumpFall; } // or moving downwards, branch to falling
    lda_zp(A_B_Buttons);
    and_imm(A_Button); // check to see if A button is being pressed
    and_zp(PreviousA_B_Buttons); // and was pressed in previous frame
    if (!zero_flag) { goto ProcSwim; } // if so, branch elsewhere
    lda_abs(JumpOrigin_Y_Position); // get vertical position player jumped from
    carry_flag = true;
    sbc_zp(Player_Y_Position); // subtract current from original vertical coordinate
    cmp_abs(DiffToHaltJump); // compare to value set here to see if player is in mid-jump
    if (!carry_flag) { goto ProcSwim; } // or just starting to jump, if just starting, skip ahead
  
DumpFall:
    lda_abs(VerticalForceDown); // otherwise dump falling into main fractional
    write_byte(VerticalForce, a);
  
ProcSwim:
    lda_abs(SwimmingFlag); // if swimming flag not set,
    if (zero_flag) { goto LRAir; } // branch ahead to last part
    GetPlayerAnimSpeed(); // do a sub to get animation frame timing
    lda_zp(Player_Y_Position);
    cmp_imm(0x14); // check vertical position against preset value
    if (carry_flag) { goto LRWater; } // if not yet reached a certain position, branch ahead
    lda_imm(0x18);
    write_byte(VerticalForce, a); // otherwise set fractional
  
LRWater:
    lda_zp(Left_Right_Buttons); // check left/right controller bits (check for swimming)
    if (zero_flag) { goto LRAir; } // if not pressing any, skip
    write_byte(PlayerFacingDir, a); // otherwise set facing direction accordingly
  
LRAir:
    lda_zp(Left_Right_Buttons); // check left/right controller bits (check for jumping/falling)
    if (zero_flag) { goto JSMove; } // if not pressing any, skip
    ImposeFriction(); // otherwise process horizontal movement
  
JSMove:
    jsr(MovePlayerHorizontally, 109); // do a sub to move player horizontally
    write_byte(Player_X_Scroll, a); // set player's speed here, to be used for scroll later
    lda_zp(GameEngineSubroutine);
    cmp_imm(0xb); // check for specific routine selected
    if (!zero_flag) { goto ExitMov1; } // branch if not set to run
    lda_imm(0x28);
    write_byte(VerticalForce, a); // otherwise set fractional
  
ExitMov1:
    goto MovePlayerVertically; // jump to move player vertically, then leave
    // --------------------------------
  
ClimbingSub:
    lda_abs(Player_YMF_Dummy);
    carry_flag = false; // add movement force to dummy variable
    adc_abs(Player_Y_MoveForce); // save with carry
    write_byte(Player_YMF_Dummy, a);
    ldy_imm(0x0); // set default adder here
    lda_zp(Player_Y_Speed); // get player's vertical speed
    if (!neg_flag) { goto MoveOnVine; } // if not moving upwards, branch
    dey(); // otherwise set adder to $ff
  
MoveOnVine:
    write_byte(0x0, y); // store adder here
    adc_zp(Player_Y_Position); // add carry to player's vertical position
    write_byte(Player_Y_Position, a); // and store to move player up or down
    lda_zp(Player_Y_HighPos);
    adc_zp(0x0); // add carry to player's page location
    write_byte(Player_Y_HighPos, a); // and store
    lda_zp(Left_Right_Buttons); // compare left/right controller bits
    and_abs(Player_CollisionBits); // to collision flag
    if (zero_flag) { goto InitCSTimer; } // if not set, skip to end
    ldy_abs(ClimbSideTimer); // otherwise check timer 
    if (!zero_flag) { goto ExitCSub; } // if timer not expired, branch to leave
    ldy_imm(0x18);
    write_byte(ClimbSideTimer, y); // otherwise set timer now
    ldx_imm(0x0); // set default offset here
    ldy_zp(PlayerFacingDir); // get facing direction
    lsr_acc(); // move right button controller bit to carry
    if (carry_flag) { goto ClimbFD; } // if controller right pressed, branch ahead
    inx();
    inx(); // otherwise increment offset by 2 bytes
  
ClimbFD:
    dey(); // check to see if facing right
    if (zero_flag) { goto CSetFDir; } // if so, branch, do not increment
    inx(); // otherwise increment by 1 byte
  
CSetFDir:
    lda_zp(Player_X_Position);
    carry_flag = false; // add or subtract from player's horizontal position
    adc_absx(ClimbAdderLow); // using value here as adder and X as offset
    write_byte(Player_X_Position, a);
    lda_zp(Player_PageLoc); // add or subtract carry or borrow using value here
    adc_absx(ClimbAdderHigh); // from the player's page location
    write_byte(Player_PageLoc, a);
    lda_zp(Left_Right_Buttons); // get left/right controller bits again
    eor_imm(0b00000011); // invert them and store them while player
    write_byte(PlayerFacingDir, a); // is on vine to face player in opposite direction
  
ExitCSub:
    goto rts; // then leave
  
InitCSTimer:
    write_byte(ClimbSideTimer, a); // initialize timer here
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used to store offset to friction data
  
PlayerPhysicsSub:
    lda_zp(Player_State); // check player state
    cmp_imm(0x3);
    if (!zero_flag) { goto CheckForJumping; } // if not climbing, branch
    ldy_imm(0x0);
    lda_zp(Up_Down_Buttons); // get controller bits for up/down
    and_abs(Player_CollisionBits); // check against player's collision detection bits
    if (zero_flag) { goto ProcClimb; } // if not pressing up or down, branch
    iny();
    and_imm(0b00001000); // check for pressing up
    if (!zero_flag) { goto ProcClimb; }
    iny();
  
ProcClimb:
    ldx_absy(Climb_Y_MForceData); // load value here
    write_byte(Player_Y_MoveForce, x); // store as vertical movement force
    lda_imm(0x8); // load default animation timing
    ldx_absy(Climb_Y_SpeedData); // load some other value here
    write_byte(Player_Y_Speed, x); // store as vertical speed
    if (neg_flag) { goto SetCAnim; } // if climbing down, use default animation timing value
    lsr_acc(); // otherwise divide timer setting by 2
  
SetCAnim:
    write_byte(PlayerAnimTimerSet, a); // store animation timer setting and leave
    goto rts;
  
CheckForJumping:
    lda_abs(JumpspringAnimCtrl); // if jumpspring animating, 
    if (!zero_flag) { goto NoJump; } // skip ahead to something else
    lda_zp(A_B_Buttons); // check for A button press
    and_imm(A_Button);
    if (zero_flag) { goto NoJump; } // if not, branch to something else
    and_zp(PreviousA_B_Buttons); // if button not pressed in previous frame, branch
    if (zero_flag) { goto ProcJumping; }
  
NoJump:
    goto X_Physics; // otherwise, jump to something else
  
ProcJumping:
    lda_zp(Player_State); // check player state
    if (zero_flag) { goto InitJS; } // if on the ground, branch
    lda_abs(SwimmingFlag); // if swimming flag not set, jump to do something else
    if (zero_flag) { goto NoJump; } // to prevent midair jumping, otherwise continue
    lda_abs(JumpSwimTimer); // if jump/swim timer nonzero, branch
    if (!zero_flag) { goto InitJS; }
    lda_zp(Player_Y_Speed); // check player's vertical speed
    if (!neg_flag) { goto InitJS; } // if player's vertical speed motionless or down, branch
    goto X_Physics; // if timer at zero and player still rising, do not swim
  
InitJS:
    lda_imm(0x20); // set jump/swim timer
    write_byte(JumpSwimTimer, a);
    ldy_imm(0x0); // initialize vertical force and dummy variable
    write_byte(Player_YMF_Dummy, y);
    write_byte(Player_Y_MoveForce, y);
    lda_zp(Player_Y_HighPos); // get vertical high and low bytes of jump origin
    write_byte(JumpOrigin_Y_HighPos, a); // and store them next to each other here
    lda_zp(Player_Y_Position);
    write_byte(JumpOrigin_Y_Position, a);
    lda_imm(0x1); // set player state to jumping/swimming
    write_byte(Player_State, a);
    lda_abs(Player_XSpeedAbsolute); // check value related to walking/running speed
    cmp_imm(0x9);
    if (!carry_flag) { goto ChkWtr; } // branch if below certain values, increment Y
    iny(); // for each amount equal or exceeded
    cmp_imm(0x10);
    if (!carry_flag) { goto ChkWtr; }
    iny();
    cmp_imm(0x19);
    if (!carry_flag) { goto ChkWtr; }
    iny();
    cmp_imm(0x1c);
    if (!carry_flag) { goto ChkWtr; } // note that for jumping, range is 0-4 for Y
    iny();
  
ChkWtr:
    lda_imm(0x1); // set value here (apparently always set to 1)
    write_byte(DiffToHaltJump, a);
    lda_abs(SwimmingFlag); // if swimming flag disabled, branch
    if (zero_flag) { goto GetYPhy; }
    ldy_imm(0x5); // otherwise set Y to 5, range is 5-6
    lda_abs(Whirlpool_Flag); // if whirlpool flag not set, branch
    if (zero_flag) { goto GetYPhy; }
    iny(); // otherwise increment to 6
  
GetYPhy:
    lda_absy(JumpMForceData); // store appropriate jump/swim
    write_byte(VerticalForce, a); // data here
    lda_absy(FallMForceData);
    write_byte(VerticalForceDown, a);
    lda_absy(InitMForceData);
    write_byte(Player_Y_MoveForce, a);
    lda_absy(PlayerYSpdData);
    write_byte(Player_Y_Speed, a);
    lda_abs(SwimmingFlag); // if swimming flag disabled, branch
    if (zero_flag) { goto PJumpSnd; }
    lda_imm(Sfx_EnemyStomp); // load swim/goomba stomp sound into
    write_byte(Square1SoundQueue, a); // square 1's sfx queue
    lda_zp(Player_Y_Position);
    cmp_imm(0x14); // check vertical low byte of player position
    if (carry_flag) { goto X_Physics; } // if below a certain point, branch
    lda_imm(0x0); // otherwise reset player's vertical speed
    write_byte(Player_Y_Speed, a); // and jump to something else to keep player
    goto X_Physics; // from swimming above water level
  
PJumpSnd:
    lda_imm(Sfx_BigJump); // load big mario's jump sound by default
    ldy_abs(PlayerSize); // is mario big?
    if (zero_flag) { goto SJumpSnd; }
    lda_imm(Sfx_SmallJump); // if not, load small mario's jump sound
  
SJumpSnd:
    write_byte(Square1SoundQueue, a); // store appropriate jump sound in square 1 sfx queue
  
X_Physics:
    ldy_imm(0x0);
    write_byte(0x0, y); // init value here
    lda_zp(Player_State); // if mario is on the ground, branch
    if (zero_flag) { goto ProcPRun; }
    lda_abs(Player_XSpeedAbsolute); // check something that seems to be related
    cmp_imm(0x19); // to mario's speed
    if (carry_flag) { goto GetXPhy; } // if =>$19 branch here
    if (!carry_flag) { goto ChkRFast; } // if not branch elsewhere
  
ProcPRun:
    iny(); // if mario on the ground, increment Y
    lda_abs(AreaType); // check area type
    if (zero_flag) { goto ChkRFast; } // if water type, branch
    dey(); // decrement Y by default for non-water type area
    lda_zp(Left_Right_Buttons); // get left/right controller bits
    cmp_zp(Player_MovingDir); // check against moving direction
    if (!zero_flag) { goto ChkRFast; } // if controller bits <> moving direction, skip this part
    lda_zp(A_B_Buttons); // check for b button pressed
    and_imm(B_Button);
    if (!zero_flag) { goto SetRTmr; } // if pressed, skip ahead to set timer
    lda_abs(RunningTimer); // check for running timer set
    if (!zero_flag) { goto GetXPhy; } // if set, branch
  
ChkRFast:
    iny(); // if running timer not set or level type is water, 
    inc_zp(0x0); // increment Y again and temp variable in memory
    lda_abs(RunningSpeed);
    if (!zero_flag) { goto FastXSp; } // if running speed set here, branch
    lda_abs(Player_XSpeedAbsolute);
    cmp_imm(0x21); // otherwise check player's walking/running speed
    if (!carry_flag) { goto GetXPhy; } // if less than a certain amount, branch ahead
  
FastXSp:
    inc_zp(0x0); // if running speed set or speed => $21 increment $00
    goto GetXPhy; // and jump ahead
  
SetRTmr:
    lda_imm(0xa); // if b button pressed, set running timer
    write_byte(RunningTimer, a);
  
GetXPhy:
    lda_absy(MaxLeftXSpdData); // get maximum speed to the left
    write_byte(MaximumLeftSpeed, a);
    lda_zp(GameEngineSubroutine); // check for specific routine running
    cmp_imm(0x7); // (player entrance)
    if (!zero_flag) { goto GetXPhy2; } // if not running, skip and use old value of Y
    ldy_imm(0x3); // otherwise set Y to 3
  
GetXPhy2:
    lda_absy(MaxRightXSpdData); // get maximum speed to the right
    write_byte(MaximumRightSpeed, a);
    ldy_zp(0x0); // get other value in memory
    lda_absy(FrictionData); // get value using value in memory as offset
    write_byte(FrictionAdderLow, a);
    lda_imm(0x0);
    write_byte(FrictionAdderHigh, a); // init something here
    lda_zp(PlayerFacingDir);
    cmp_zp(Player_MovingDir); // check facing direction against moving direction
    if (zero_flag) { goto ExitPhy; } // if the same, branch to leave
    asl_abs(FrictionAdderLow); // otherwise shift d7 of friction adder low into carry
    rol_abs(FrictionAdderHigh); // then rotate carry onto d0 of friction adder high
  
ExitPhy:
    goto rts; // and then leave
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
    // $00 - used to store downward movement force in FireballObjCore
    // $02 - used to store maximum vertical speed in FireballObjCore
    // $07 - used to store pseudorandom bit in BubbleCheck
  
ProcFireball_Bubble:
    lda_abs(PlayerStatus); // check player's status
    cmp_imm(0x2);
    if (!carry_flag) { goto ProcAirBubbles; } // if not fiery, branch
    lda_zp(A_B_Buttons);
    and_imm(B_Button); // check for b button pressed
    if (zero_flag) { goto ProcFireballs; } // branch if not pressed
    and_zp(PreviousA_B_Buttons);
    if (!zero_flag) { goto ProcFireballs; } // if button pressed in previous frame, branch
    lda_abs(FireballCounter); // load fireball counter
    and_imm(0b00000001); // get LSB and use as offset for buffer
    tax();
    lda_zpx(Fireball_State); // load fireball state
    if (!zero_flag) { goto ProcFireballs; } // if not inactive, branch
    ldy_zp(Player_Y_HighPos); // if player too high or too low, branch
    dey();
    if (!zero_flag) { goto ProcFireballs; }
    lda_abs(CrouchingFlag); // if player crouching, branch
    if (!zero_flag) { goto ProcFireballs; }
    lda_zp(Player_State); // if player's state = climbing, branch
    cmp_imm(0x3);
    if (zero_flag) { goto ProcFireballs; }
    lda_imm(Sfx_Fireball); // play fireball sound effect
    write_byte(Square1SoundQueue, a);
    lda_imm(0x2); // load state
    write_byte(Fireball_State + x, a);
    ldy_abs(PlayerAnimTimerSet); // copy animation frame timer setting
    write_byte(FireballThrowingTimer, y); // into fireball throwing timer
    dey();
    write_byte(PlayerAnimTimer, y); // decrement and store in player's animation timer
    inc_abs(FireballCounter); // increment fireball counter
  
ProcFireballs:
    ldx_imm(0x0);
    jsr(FireballObjCore, 110); // process first fireball object
    ldx_imm(0x1);
    jsr(FireballObjCore, 111); // process second fireball object, then do air bubbles
  
ProcAirBubbles:
    lda_abs(AreaType); // if not water type level, skip the rest of this
    if (!zero_flag) { goto BublExit; }
    ldx_imm(0x2); // otherwise load counter and use as offset
  
BublLoop:
    write_byte(ObjectOffset, x); // store offset
    jsr(BubbleCheck, 112); // check timers and coordinates, create air bubble
    jsr(RelativeBubblePosition, 113); // get relative coordinates
    jsr(GetBubbleOffscreenBits, 114); // get offscreen information
    DrawBubble(); // draw the air bubble
    dex();
    if (!neg_flag) { goto BublLoop; } // do this until all three are handled
  
BublExit:
    goto rts; // then leave
  
FireballObjCore:
    write_byte(ObjectOffset, x); // store offset as current object
    lda_zpx(Fireball_State); // check for d7 = 1
    asl_acc();
    if (carry_flag) { goto FireballExplosion; } // if so, branch to get relative coordinates and draw explosion
    ldy_zpx(Fireball_State); // if fireball inactive, branch to leave
    if (zero_flag) { goto NoFBall; }
    dey(); // if fireball state set to 1, skip this part and just run it
    if (zero_flag) { goto RunFB; }
    lda_zp(Player_X_Position); // get player's horizontal position
    adc_imm(0x4); // add four pixels and store as fireball's horizontal position
    write_byte(Fireball_X_Position + x, a);
    lda_zp(Player_PageLoc); // get player's page location
    adc_imm(0x0); // add carry and store as fireball's page location
    write_byte(Fireball_PageLoc + x, a);
    lda_zp(Player_Y_Position); // get player's vertical position and store
    write_byte(Fireball_Y_Position + x, a);
    lda_imm(0x1); // set high byte of vertical position
    write_byte(Fireball_Y_HighPos + x, a);
    ldy_zp(PlayerFacingDir); // get player's facing direction
    dey(); // decrement to use as offset here
    lda_absy(FireballXSpdData); // set horizontal speed of fireball accordingly
    write_byte(Fireball_X_Speed + x, a);
    lda_imm(0x4); // set vertical speed of fireball
    write_byte(Fireball_Y_Speed + x, a);
    lda_imm(0x7);
    write_byte(Fireball_BoundBoxCtrl + x, a); // set bounding box size control for fireball
    dec_zpx(Fireball_State); // decrement state to 1 to skip this part from now on
  
RunFB:
    txa(); // add 7 to offset to use
    carry_flag = false; // as fireball offset for next routines
    adc_imm(0x7);
    tax();
    lda_imm(0x50); // set downward movement force here
    write_byte(0x0, a);
    lda_imm(0x3); // set maximum speed here
    write_byte(0x2, a);
    lda_imm(0x0);
    ImposeGravity(); // do sub here to impose gravity on fireball and move vertically
    jsr(MoveObjectHorizontally, 115); // do another sub to move it horizontally
    ldx_zp(ObjectOffset); // return fireball offset to X
    jsr(RelativeFireballPosition, 116); // get relative coordinates
    jsr(GetFireballOffscreenBits, 117); // get offscreen information
    jsr(GetFireballBoundBox, 118); // get bounding box coordinates
    jsr(FireballBGCollision, 119); // do fireball to background collision detection
    lda_abs(FBall_OffscreenBits); // get fireball offscreen bits
    and_imm(0b11001100); // mask out certain bits
    if (!zero_flag) { goto EraseFB; } // if any bits still set, branch to kill fireball
    jsr(FireballEnemyCollision, 120); // do fireball to enemy collision detection and deal with collisions
    goto DrawFireball; // draw fireball appropriately and leave
  
EraseFB:
    lda_imm(0x0); // erase fireball state
    write_byte(Fireball_State + x, a);
  
NoFBall:
    goto rts; // leave
  
FireballExplosion:
    jsr(RelativeFireballPosition, 121);
    goto DrawExplosion_Fireball;
  
BubbleCheck:
    lda_absx(PseudoRandomBitReg + 1); // get part of LSFR
    and_imm(0x1);
    write_byte(0x7, a); // store pseudorandom bit here
    lda_zpx(Bubble_Y_Position); // get vertical coordinate for air bubble
    cmp_imm(0xf8); // if offscreen coordinate not set,
    if (!zero_flag) { goto MoveBubl; } // branch to move air bubble
    lda_abs(AirBubbleTimer); // if air bubble timer not expired,
    if (!zero_flag) { goto ExitBubl; } // branch to leave, otherwise create new air bubble
  
SetupBubble:
    ldy_imm(0x0); // load default value here
    lda_zp(PlayerFacingDir); // get player's facing direction
    lsr_acc(); // move d0 to carry
    if (!carry_flag) { goto PosBubl; } // branch to use default value if facing left
    ldy_imm(0x8); // otherwise load alternate value here
  
PosBubl:
    tya(); // use value loaded as adder
    adc_zp(Player_X_Position); // add to player's horizontal position
    write_byte(Bubble_X_Position + x, a); // save as horizontal position for airbubble
    lda_zp(Player_PageLoc);
    adc_imm(0x0); // add carry to player's page location
    write_byte(Bubble_PageLoc + x, a); // save as page location for airbubble
    lda_zp(Player_Y_Position);
    carry_flag = false; // add eight pixels to player's vertical position
    adc_imm(0x8);
    write_byte(Bubble_Y_Position + x, a); // save as vertical position for air bubble
    lda_imm(0x1);
    write_byte(Bubble_Y_HighPos + x, a); // set vertical high byte for air bubble
    ldy_zp(0x7); // get pseudorandom bit, use as offset
    lda_absy(BubbleTimerData); // get data for air bubble timer
    write_byte(AirBubbleTimer, a); // set air bubble timer
  
MoveBubl:
    ldy_zp(0x7); // get pseudorandom bit again, use as offset
    lda_absx(Bubble_YMF_Dummy);
    carry_flag = true; // subtract pseudorandom amount from dummy variable
    sbc_absy(Bubble_MForceData);
    write_byte(Bubble_YMF_Dummy + x, a); // save dummy variable
    lda_zpx(Bubble_Y_Position);
    sbc_imm(0x0); // subtract borrow from airbubble's vertical coordinate
    cmp_imm(0x20); // if below the status bar,
    if (carry_flag) { goto Y_Bubl; } // branch to go ahead and use to move air bubble upwards
    lda_imm(0xf8); // otherwise set offscreen coordinate
  
Y_Bubl:
    write_byte(Bubble_Y_Position + x, a); // store as new vertical coordinate for air bubble
  
ExitBubl:
    goto rts; // leave
  
RunGameTimer:
    lda_abs(OperMode); // get primary mode of operation
    if (zero_flag) { goto ExGTimer; } // branch to leave if in title screen mode
    lda_zp(GameEngineSubroutine);
    cmp_imm(0x8); // if routine number less than eight running,
    if (!carry_flag) { goto ExGTimer; } // branch to leave
    cmp_imm(0xb); // if running death routine,
    if (zero_flag) { goto ExGTimer; } // branch to leave
    lda_zp(Player_Y_HighPos);
    cmp_imm(0x2); // if player below the screen,
    if (carry_flag) { goto ExGTimer; } // branch to leave regardless of level type
    lda_abs(GameTimerCtrlTimer); // if game timer control not yet expired,
    if (!zero_flag) { goto ExGTimer; } // branch to leave
    lda_abs(GameTimerDisplay);
    ora_abs(GameTimerDisplay + 1); // otherwise check game timer digits
    ora_abs(GameTimerDisplay + 2);
    if (zero_flag) { goto TimeUpOn; } // if game timer digits at 000, branch to time-up code
    ldy_abs(GameTimerDisplay); // otherwise check first digit
    dey(); // if first digit not on 1,
    if (!zero_flag) { goto ResGTCtrl; } // branch to reset game timer control
    lda_abs(GameTimerDisplay + 1); // otherwise check second and third digits
    ora_abs(GameTimerDisplay + 2);
    if (!zero_flag) { goto ResGTCtrl; } // if timer not at 100, branch to reset game timer control
    lda_imm(TimeRunningOutMusic);
    write_byte(EventMusicQueue, a); // otherwise load time running out music
  
ResGTCtrl:
    lda_imm(0x18); // reset game timer control
    write_byte(GameTimerCtrlTimer, a);
    ldy_imm(0x23); // set offset for last digit
    lda_imm(0xff); // set value to decrement game timer digit
    write_byte(DigitModifier + 5, a);
    jsr(DigitsMathRoutine, 122); // do sub to decrement game timer slowly
    lda_imm(0xa4); // set status nybbles to update game timer display
    PrintStatusBarNumbers(); goto rts; // do sub to update the display
  
TimeUpOn:
    write_byte(PlayerStatus, a); // init player status (note A will always be zero here)
    jsr(ForceInjury, 123); // do sub to kill the player (note player is small here)
    inc_abs(GameTimerExpiredFlag); // set game timer expiration flag
  
ExGTimer:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
  
WarpZoneObject:
    lda_abs(ScrollLock); // check for scroll lock flag
    if (zero_flag) { goto ExGTimer; } // branch if not set to leave
    lda_zp(Player_Y_Position); // check to see if player's vertical coordinate has
    and_zp(Player_Y_HighPos); // same bits set as in vertical high byte (why?)
    if (!zero_flag) { goto ExGTimer; } // if so, branch to leave
    write_byte(ScrollLock, a); // otherwise nullify scroll lock flag
    inc_abs(WarpZoneControl); // increment warp zone flag to make warp pipes for warp zone
    EraseEnemyObject(); goto rts; // kill this object
    // -------------------------------------------------------------------------------------
    // $00 - used in WhirlpoolActivate to store whirlpool length / 2, page location of center of whirlpool
    // and also to store movement force exerted on player
    // $01 - used in ProcessWhirlpools to store page location of right extent of whirlpool
    // and in WhirlpoolActivate to store center of whirlpool
    // $02 - used in ProcessWhirlpools to store right extent of whirlpool and in
    // WhirlpoolActivate to store maximum vertical speed
  
ProcessWhirlpools:
    lda_abs(AreaType); // check for water type level
    if (!zero_flag) { goto ExitWh; } // branch to leave if not found
    write_byte(Whirlpool_Flag, a); // otherwise initialize whirlpool flag
    lda_abs(TimerControl); // if master timer control set,
    if (!zero_flag) { goto ExitWh; } // branch to leave
    ldy_imm(0x4); // otherwise start with last whirlpool data
  
WhLoop:
    lda_absy(Whirlpool_LeftExtent); // get left extent of whirlpool
    carry_flag = false;
    adc_absy(Whirlpool_Length); // add length of whirlpool
    write_byte(0x2, a); // store result as right extent here
    lda_absy(Whirlpool_PageLoc); // get page location
    if (zero_flag) { goto NextWh; } // if none or page 0, branch to get next data
    adc_imm(0x0); // add carry
    write_byte(0x1, a); // store result as page location of right extent here
    lda_zp(Player_X_Position); // get player's horizontal position
    carry_flag = true;
    sbc_absy(Whirlpool_LeftExtent); // subtract left extent
    lda_zp(Player_PageLoc); // get player's page location
    sbc_absy(Whirlpool_PageLoc); // subtract borrow
    if (neg_flag) { goto NextWh; } // if player too far left, branch to get next data
    lda_zp(0x2); // otherwise get right extent
    carry_flag = true;
    sbc_zp(Player_X_Position); // subtract player's horizontal coordinate
    lda_zp(0x1); // get right extent's page location
    sbc_zp(Player_PageLoc); // subtract borrow
    if (!neg_flag) { goto WhirlpoolActivate; } // if player within right extent, branch to whirlpool code
  
NextWh:
    dey(); // move onto next whirlpool data
    if (!neg_flag) { goto WhLoop; } // do this until all whirlpools are checked
  
ExitWh:
    goto rts; // leave
  
WhirlpoolActivate:
    lda_absy(Whirlpool_Length); // get length of whirlpool
    lsr_acc(); // divide by 2
    write_byte(0x0, a); // save here
    lda_absy(Whirlpool_LeftExtent); // get left extent of whirlpool
    carry_flag = false;
    adc_zp(0x0); // add length divided by 2
    write_byte(0x1, a); // save as center of whirlpool
    lda_absy(Whirlpool_PageLoc); // get page location
    adc_imm(0x0); // add carry
    write_byte(0x0, a); // save as page location of whirlpool center
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // shift d0 into carry (to run on every other frame)
    if (!carry_flag) { goto WhPull; } // if d0 not set, branch to last part of code
    lda_zp(0x1); // get center
    carry_flag = true;
    sbc_zp(Player_X_Position); // subtract player's horizontal coordinate
    lda_zp(0x0); // get page location of center
    sbc_zp(Player_PageLoc); // subtract borrow
    if (!neg_flag) { goto LeftWh; } // if player to the left of center, branch
    lda_zp(Player_X_Position); // otherwise slowly pull player left, towards the center
    carry_flag = true;
    sbc_imm(0x1); // subtract one pixel
    write_byte(Player_X_Position, a); // set player's new horizontal coordinate
    lda_zp(Player_PageLoc);
    sbc_imm(0x0); // subtract borrow
    goto SetPWh; // jump to set player's new page location
  
LeftWh:
    lda_abs(Player_CollisionBits); // get player's collision bits
    lsr_acc(); // shift d0 into carry
    if (!carry_flag) { goto WhPull; } // if d0 not set, branch
    lda_zp(Player_X_Position); // otherwise slowly pull player right, towards the center
    carry_flag = false;
    adc_imm(0x1); // add one pixel
    write_byte(Player_X_Position, a); // set player's new horizontal coordinate
    lda_zp(Player_PageLoc);
    adc_imm(0x0); // add carry
  
SetPWh:
    write_byte(Player_PageLoc, a); // set player's new page location
  
WhPull:
    lda_imm(0x10);
    write_byte(0x0, a); // set vertical movement force
    lda_imm(0x1);
    write_byte(Whirlpool_Flag, a); // set whirlpool flag to be used later
    write_byte(0x2, a); // also set maximum vertical speed
    lsr_acc();
    tax(); // set X for player offset
    ImposeGravity(); goto rts; // jump to put whirlpool effect on player vertically, do not return
    // -------------------------------------------------------------------------------------
  
FlagpoleRoutine:
    ldx_imm(0x5); // set enemy object offset
    write_byte(ObjectOffset, x); // to special use slot
    lda_zpx(Enemy_ID);
    cmp_imm(FlagpoleFlagObject); // if flagpole flag not found,
    if (!zero_flag) { goto ExitFlagP; } // branch to leave
    lda_zp(GameEngineSubroutine);
    cmp_imm(0x4); // if flagpole slide routine not running,
    if (!zero_flag) { goto SkipScore; } // branch to near the end of code
    lda_zp(Player_State);
    cmp_imm(0x3); // if player state not climbing,
    if (!zero_flag) { goto SkipScore; } // branch to near the end of code
    lda_zpx(Enemy_Y_Position); // check flagpole flag's vertical coordinate
    cmp_imm(0xaa); // if flagpole flag down to a certain point,
    if (carry_flag) { goto GiveFPScr; } // branch to end the level
    lda_zp(Player_Y_Position); // check player's vertical coordinate
    cmp_imm(0xa2); // if player down to a certain point,
    if (carry_flag) { goto GiveFPScr; } // branch to end the level
    lda_absx(Enemy_YMF_Dummy);
    adc_imm(0xff); // add movement amount to dummy variable
    write_byte(Enemy_YMF_Dummy + x, a); // save dummy variable
    lda_zpx(Enemy_Y_Position); // get flag's vertical coordinate
    adc_imm(0x1); // add 1 plus carry to move flag, and
    write_byte(Enemy_Y_Position + x, a); // store vertical coordinate
    lda_abs(FlagpoleFNum_YMFDummy);
    carry_flag = true; // subtract movement amount from dummy variable
    sbc_imm(0xff);
    write_byte(FlagpoleFNum_YMFDummy, a); // save dummy variable
    lda_abs(FlagpoleFNum_Y_Pos);
    sbc_imm(0x1); // subtract one plus borrow to move floatey number,
    write_byte(FlagpoleFNum_Y_Pos, a); // and store vertical coordinate here
  
SkipScore:
    goto FPGfx; // jump to skip ahead and draw flag and floatey number
  
GiveFPScr:
    ldy_abs(FlagpoleScore); // get score offset from earlier (when player touched flagpole)
    lda_absy(FlagpoleScoreMods); // get amount to award player points
    ldx_absy(FlagpoleScoreDigits); // get digit with which to award points
    write_byte(DigitModifier + x, a); // store in digit modifier
    jsr(AddToScore, 124); // do sub to award player points depending on height of collision
    lda_imm(0x5);
    write_byte(GameEngineSubroutine, a); // set to run end-of-level subroutine on next frame
  
FPGfx:
    jsr(GetEnemyOffscreenBits, 125); // get offscreen information
    jsr(RelativeEnemyPosition, 126); // get relative coordinates
    jsr(FlagpoleGfxHandler, 127); // draw flagpole flag and floatey number
  
ExitFlagP:
    goto rts;
    // -------------------------------------------------------------------------------------
  
JumpspringHandler:
    jsr(GetEnemyOffscreenBits, 128); // get offscreen information
    lda_abs(TimerControl); // check master timer control
    if (!zero_flag) { goto DrawJSpr; } // branch to last section if set
    lda_abs(JumpspringAnimCtrl); // check jumpspring frame control
    if (zero_flag) { goto DrawJSpr; } // branch to last section if not set
    tay();
    dey(); // subtract one from frame control,
    tya(); // the only way a poor nmos 6502 can
    and_imm(0b00000010); // mask out all but d1, original value still in Y
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
    write_byte(Enemy_Y_Position + x, a); // store as new vertical position
    cpy_imm(0x1); // check frame control offset (second frame is $00)
    if (!carry_flag) { goto BounceJS; } // if offset not yet at third frame ($01), skip to next part
    lda_zp(A_B_Buttons);
    and_imm(A_Button); // check saved controller bits for A button press
    if (zero_flag) { goto BounceJS; } // skip to next part if A not pressed
    and_zp(PreviousA_B_Buttons); // check for A button pressed in previous frame
    if (!zero_flag) { goto BounceJS; } // skip to next part if so
    lda_imm(0xf4);
    write_byte(JumpspringForce, a); // otherwise write new jumpspring force here
  
BounceJS:
    cpy_imm(0x3); // check frame control offset again
    if (!zero_flag) { goto DrawJSpr; } // skip to last part if not yet at fifth frame ($03)
    lda_abs(JumpspringForce);
    write_byte(Player_Y_Speed, a); // store jumpspring force as player's new vertical speed
    lda_imm(0x0);
    write_byte(JumpspringAnimCtrl, a); // initialize jumpspring frame control
  
DrawJSpr:
    jsr(RelativeEnemyPosition, 129); // get jumpspring's relative coordinates
    jsr(EnemyGfxHandler, 130); // draw jumpspring
    OffscreenBoundsCheck(); // check to see if we need to kill it
    lda_abs(JumpspringAnimCtrl); // if frame control at zero, don't bother
    if (zero_flag) { goto ExJSpring; } // trying to animate it, just leave
    lda_abs(JumpspringTimer);
    if (!zero_flag) { goto ExJSpring; } // if jumpspring timer not expired yet, leave
    lda_imm(0x4);
    write_byte(JumpspringTimer, a); // otherwise initialize jumpspring timer
    inc_abs(JumpspringAnimCtrl); // increment frame control to animate jumpspring
  
ExJSpring:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
    // $06-$07 - used as address to block buffer data
    // $02 - used as vertical high nybble of block buffer offset
  
VineObjectHandler:
    cpx_imm(0x5); // check enemy offset for special use slot
    if (!zero_flag) { goto ExitVH; } // if not in last slot, branch to leave
    ldy_abs(VineFlagOffset);
    dey(); // decrement vine flag in Y, use as offset
    lda_abs(VineHeight);
    cmp_absy(VineHeightData); // if vine has reached certain height,
    if (zero_flag) { goto RunVSubs; } // branch ahead to skip this part
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // shift d1 into carry
    lsr_acc();
    if (!carry_flag) { goto RunVSubs; } // if d1 not set (2 frames every 4) skip this part
    lda_zp(Enemy_Y_Position + 5);
    sbc_imm(0x1); // subtract vertical position of vine
    write_byte(Enemy_Y_Position + 5, a); // one pixel every frame it's time
    inc_abs(VineHeight); // increment vine height
  
RunVSubs:
    lda_abs(VineHeight); // if vine still very small,
    cmp_imm(0x8); // branch to leave
    if (!carry_flag) { goto ExitVH; }
    jsr(RelativeEnemyPosition, 131); // get relative coordinates of vine,
    jsr(GetEnemyOffscreenBits, 132); // and any offscreen bits
    ldy_imm(0x0); // initialize offset used in draw vine sub
  
VDrawLoop:
    DrawVine(); // draw vine
    iny(); // increment offset
    cpy_abs(VineFlagOffset); // if offset in Y and offset here
    if (!zero_flag) { goto VDrawLoop; } // do not yet match, loop back to draw more vine
    lda_abs(Enemy_OffscreenBits);
    and_imm(0b00001100); // mask offscreen bits
    if (zero_flag) { goto WrCMTile; } // if none of the saved offscreen bits set, skip ahead
    dey(); // otherwise decrement Y to get proper offset again
  
KillVine:
    ldx_absy(VineObjOffset); // get enemy object offset for this vine object
    EraseEnemyObject(); // kill this vine object
    dey(); // decrement Y
    if (!neg_flag) { goto KillVine; } // if any vine objects left, loop back to kill it
    write_byte(VineFlagOffset, a); // initialize vine flag/offset
    write_byte(VineHeight, a); // initialize vine height
  
WrCMTile:
    lda_abs(VineHeight); // check vine height
    cmp_imm(0x20); // if vine small (less than 32 pixels tall)
    if (!carry_flag) { goto ExitVH; } // then branch ahead to leave
    ldx_imm(0x6); // set offset in X to last enemy slot
    lda_imm(0x1); // set A to obtain horizontal in $04, but we don't care
    ldy_imm(0x1b); // set Y to offset to get block at ($04, $10) of coordinates
    jsr(BlockBufferCollision, 133); // do a sub to get block buffer address set, return contents
    ldy_zp(0x2);
    cpy_imm(0xd0); // if vertical high nybble offset beyond extent of
    if (carry_flag) { goto ExitVH; } // current block buffer, branch to leave, do not write
    lda_indy(0x6); // otherwise check contents of block buffer at 
    if (!zero_flag) { goto ExitVH; } // current offset, if not empty, branch to leave
    lda_imm(0x26);
    write_byte(read_word(0x6) + y, a); // otherwise, write climbing metatile to block buffer
  
ExitVH:
    ldx_zp(ObjectOffset); // get enemy object offset and leave
    goto rts;
    // -------------------------------------------------------------------------------------
  
ProcessCannons:
    lda_abs(AreaType); // get area type
    if (zero_flag) { goto ExCannon; } // if water type area, branch to leave
    ldx_imm(0x2);
  
ThreeSChk:
    write_byte(ObjectOffset, x); // start at third enemy slot
    lda_zpx(Enemy_Flag); // check enemy buffer flag
    if (!zero_flag) { goto Chk_BB; } // if set, branch to check enemy
    lda_absx(PseudoRandomBitReg + 1); // otherwise get part of LSFR
    ldy_abs(SecondaryHardMode); // get secondary hard mode flag, use as offset
    and_absy(CannonBitmasks); // mask out bits of LSFR as decided by flag
    cmp_imm(0x6); // check to see if lower nybble is above certain value
    if (carry_flag) { goto Chk_BB; } // if so, branch to check enemy
    tay(); // transfer masked contents of LSFR to Y as pseudorandom offset
    lda_absy(Cannon_PageLoc); // get page location
    if (zero_flag) { goto Chk_BB; } // if not set or on page 0, branch to check enemy
    lda_absy(Cannon_Timer); // get cannon timer
    if (zero_flag) { goto FireCannon; } // if expired, branch to fire cannon
    sbc_imm(0x0); // otherwise subtract borrow (note carry will always be clear here)
    write_byte(Cannon_Timer + y, a); // to count timer down
    goto Chk_BB; // then jump ahead to check enemy
  
FireCannon:
    lda_abs(TimerControl); // if master timer control set,
    if (!zero_flag) { goto Chk_BB; } // branch to check enemy
    lda_imm(0xe); // otherwise we start creating one
    write_byte(Cannon_Timer + y, a); // first, reset cannon timer
    lda_absy(Cannon_PageLoc); // get page location of cannon
    write_byte(Enemy_PageLoc + x, a); // save as page location of bullet bill
    lda_absy(Cannon_X_Position); // get horizontal coordinate of cannon
    write_byte(Enemy_X_Position + x, a); // save as horizontal coordinate of bullet bill
    lda_absy(Cannon_Y_Position); // get vertical coordinate of cannon
    carry_flag = true;
    sbc_imm(0x8); // subtract eight pixels (because enemies are 24 pixels tall)
    write_byte(Enemy_Y_Position + x, a); // save as vertical coordinate of bullet bill
    lda_imm(0x1);
    write_byte(Enemy_Y_HighPos + x, a); // set vertical high byte of bullet bill
    write_byte(Enemy_Flag + x, a); // set buffer flag
    lsr_acc(); // shift right once to init A
    write_byte(Enemy_State + x, a); // then initialize enemy's state
    lda_imm(0x9);
    write_byte(Enemy_BoundBoxCtrl + x, a); // set bounding box size control for bullet bill
    lda_imm(BulletBill_CannonVar);
    write_byte(Enemy_ID + x, a); // load identifier for bullet bill (cannon variant)
    goto Next3Slt; // move onto next slot
  
Chk_BB:
    lda_zpx(Enemy_ID); // check enemy identifier for bullet bill (cannon variant)
    cmp_imm(BulletBill_CannonVar);
    if (!zero_flag) { goto Next3Slt; } // if not found, branch to get next slot
    OffscreenBoundsCheck(); // otherwise, check to see if it went offscreen
    lda_zpx(Enemy_Flag); // check enemy buffer flag
    if (zero_flag) { goto Next3Slt; } // if not set, branch to get next slot
    jsr(GetEnemyOffscreenBits, 134); // otherwise, get offscreen information
    jsr(BulletBillHandler, 135); // then do sub to handle bullet bill
  
Next3Slt:
    dex(); // move onto next slot
    if (!neg_flag) { goto ThreeSChk; } // do this until first three slots are checked
  
ExCannon:
    goto rts; // then leave
    // --------------------------------
  
BulletBillHandler:
    lda_abs(TimerControl); // if master timer control set,
    if (!zero_flag) { goto RunBBSubs; } // branch to run subroutines except movement sub
    lda_zpx(Enemy_State);
    if (!zero_flag) { goto ChkDSte; } // if bullet bill's state set, branch to check defeated state
    lda_abs(Enemy_OffscreenBits); // otherwise load offscreen bits
    and_imm(0b00001100); // mask out bits
    cmp_imm(0b00001100); // check to see if all bits are set
    if (zero_flag) { goto KillBB; } // if so, branch to kill this object
    ldy_imm(0x1); // set to move right by default
    PlayerEnemyDiff(); // get horizontal difference between player and bullet bill
    if (neg_flag) { goto SetupBB; } // if enemy to the left of player, branch
    iny(); // otherwise increment to move left
  
SetupBB:
    write_byte(Enemy_MovingDir + x, y); // set bullet bill's moving direction
    dey(); // decrement to use as offset
    lda_absy(BulletBillXSpdData); // get horizontal speed based on moving direction
    write_byte(Enemy_X_Speed + x, a); // and store it
    lda_zp(0x0); // get horizontal difference
    adc_imm(0x28); // add 40 pixels
    cmp_imm(0x50); // if less than a certain amount, player is too close
    if (!carry_flag) { goto KillBB; } // to cannon either on left or right side, thus branch
    lda_imm(0x1);
    write_byte(Enemy_State + x, a); // otherwise set bullet bill's state
    lda_imm(0xa);
    write_byte(EnemyFrameTimer + x, a); // set enemy frame timer
    lda_imm(Sfx_Blast);
    write_byte(Square2SoundQueue, a); // play fireworks/gunfire sound
  
ChkDSte:
    lda_zpx(Enemy_State); // check enemy state for d5 set
    and_imm(0b00100000);
    if (zero_flag) { goto BBFly; } // if not set, skip to move horizontally
    jsr(MoveD_EnemyVertically, 136); // otherwise do sub to move bullet bill vertically
  
BBFly:
    jsr(MoveEnemyHorizontally, 137); // do sub to move bullet bill horizontally
  
RunBBSubs:
    jsr(GetEnemyOffscreenBits, 138); // get offscreen information
    jsr(RelativeEnemyPosition, 139); // get relative coordinates
    jsr(GetEnemyBoundBox, 140); // get bounding box coordinates
    jsr(PlayerEnemyCollision, 141); // handle player to enemy collisions
    goto EnemyGfxHandler; // draw the bullet bill and leave
  
KillBB:
    EraseEnemyObject(); // kill bullet bill and leave
    goto rts;
    // -------------------------------------------------------------------------------------
  
SpawnHammerObj:
    lda_abs(PseudoRandomBitReg + 1); // get pseudorandom bits from
    and_imm(0b00000111); // second part of LSFR
    if (!zero_flag) { goto SetMOfs; } // if any bits are set, branch and use as offset
    lda_abs(PseudoRandomBitReg + 1);
    and_imm(0b00001000); // get d3 from same part of LSFR
  
SetMOfs:
    tay(); // use either d3 or d2-d0 for offset here
    lda_zpy(Misc_State); // if any values loaded in
    if (!zero_flag) { goto NoHammer; } // $2a-$32 where offset is then leave with carry clear
    ldx_absy(HammerEnemyOfsData); // get offset of enemy slot to check using Y as offset
    lda_zpx(Enemy_Flag); // check enemy buffer flag at offset
    if (!zero_flag) { goto NoHammer; } // if buffer flag set, branch to leave with carry clear
    ldx_zp(ObjectOffset); // get original enemy object offset
    txa();
    write_byte(HammerEnemyOffset + y, a); // save here
    lda_imm(0x90);
    write_byte(Misc_State + y, a); // save hammer's state here
    lda_imm(0x7);
    write_byte(Misc_BoundBoxCtrl + y, a); // set something else entirely, here
    carry_flag = true; // return with carry set
    goto rts;
  
NoHammer:
    ldx_zp(ObjectOffset); // get original enemy object offset
    carry_flag = false; // return with carry clear
    goto rts;
    // --------------------------------
    // $00 - used to set downward force
    // $01 - used to set upward force (residual)
    // $02 - used to set maximum speed
  
ProcHammerObj:
    lda_abs(TimerControl); // if master timer control set
    if (!zero_flag) { goto RunHSubs; } // skip all of this code and go to last subs at the end
    lda_zpx(Misc_State); // otherwise get hammer's state
    and_imm(0b01111111); // mask out d7
    ldy_absx(HammerEnemyOffset); // get enemy object offset that spawned this hammer
    cmp_imm(0x2); // check hammer's state
    if (zero_flag) { goto SetHSpd; } // if currently at 2, branch
    if (carry_flag) { goto SetHPos; } // if greater than 2, branch elsewhere
    txa();
    carry_flag = false; // add 13 bytes to use
    adc_imm(0xd); // proper misc object
    tax(); // return offset to X
    lda_imm(0x10);
    write_byte(0x0, a); // set downward movement force
    lda_imm(0xf);
    write_byte(0x1, a); // set upward movement force (not used)
    lda_imm(0x4);
    write_byte(0x2, a); // set maximum vertical speed
    lda_imm(0x0); // set A to impose gravity on hammer
    ImposeGravity(); // do sub to impose gravity on hammer and move vertically
    jsr(MoveObjectHorizontally, 142); // do sub to move it horizontally
    ldx_zp(ObjectOffset); // get original misc object offset
    goto RunAllH; // branch to essential subroutines
  
SetHSpd:
    lda_imm(0xfe);
    write_byte(Misc_Y_Speed + x, a); // set hammer's vertical speed
    lda_zpy(Enemy_State); // get enemy object state
    and_imm(0b11110111); // mask out d3
    write_byte(Enemy_State + y, a); // store new state
    ldx_zpy(Enemy_MovingDir); // get enemy's moving direction
    dex(); // decrement to use as offset
    lda_absx(HammerXSpdData); // get proper speed to use based on moving direction
    ldx_zp(ObjectOffset); // reobtain hammer's buffer offset
    write_byte(Misc_X_Speed + x, a); // set hammer's horizontal speed
  
SetHPos:
    dec_zpx(Misc_State); // decrement hammer's state
    lda_zpy(Enemy_X_Position); // get enemy's horizontal position
    carry_flag = false;
    adc_imm(0x2); // set position 2 pixels to the right
    write_byte(Misc_X_Position + x, a); // store as hammer's horizontal position
    lda_zpy(Enemy_PageLoc); // get enemy's page location
    adc_imm(0x0); // add carry
    write_byte(Misc_PageLoc + x, a); // store as hammer's page location
    lda_zpy(Enemy_Y_Position); // get enemy's vertical position
    carry_flag = true;
    sbc_imm(0xa); // move position 10 pixels upward
    write_byte(Misc_Y_Position + x, a); // store as hammer's vertical position
    lda_imm(0x1);
    write_byte(Misc_Y_HighPos + x, a); // set hammer's vertical high byte
    if (!zero_flag) { goto RunHSubs; } // unconditional branch to skip first routine
  
RunAllH:
    jsr(PlayerHammerCollision, 143); // handle collisions
  
RunHSubs:
    jsr(GetMiscOffscreenBits, 144); // get offscreen information
    jsr(RelativeMiscPosition, 145); // get relative coordinates
    jsr(GetMiscBoundBox, 146); // get bounding box coordinates
    jsr(DrawHammer, 147); // draw the hammer
    goto rts; // and we are done here
    // -------------------------------------------------------------------------------------
    // $02 - used to store vertical high nybble offset from block buffer routine
    // $06 - used to store low byte of block buffer address
  
CoinBlock:
    FindEmptyMiscSlot(); // set offset for empty or last misc object buffer slot
    lda_zpx(Block_PageLoc); // get page location of block object
    write_byte(Misc_PageLoc + y, a); // store as page location of misc object
    lda_zpx(Block_X_Position); // get horizontal coordinate of block object
    ora_imm(0x5); // add 5 pixels
    write_byte(Misc_X_Position + y, a); // store as horizontal coordinate of misc object
    lda_zpx(Block_Y_Position); // get vertical coordinate of block object
    sbc_imm(0x10); // subtract 16 pixels
    write_byte(Misc_Y_Position + y, a); // store as vertical coordinate of misc object
    goto JCoinC; // jump to rest of code as applies to this misc object
  
SetupJumpCoin:
    FindEmptyMiscSlot(); // set offset for empty or last misc object buffer slot
    lda_absx(Block_PageLoc2); // get page location saved earlier
    write_byte(Misc_PageLoc + y, a); // and save as page location for misc object
    lda_zp(0x6); // get low byte of block buffer offset
    asl_acc();
    asl_acc(); // multiply by 16 to use lower nybble
    asl_acc();
    asl_acc();
    ora_imm(0x5); // add five pixels
    write_byte(Misc_X_Position + y, a); // save as horizontal coordinate for misc object
    lda_zp(0x2); // get vertical high nybble offset from earlier
    adc_imm(0x20); // add 32 pixels for the status bar
    write_byte(Misc_Y_Position + y, a); // store as vertical coordinate
  
JCoinC:
    lda_imm(0xfb);
    write_byte(Misc_Y_Speed + y, a); // set vertical speed
    lda_imm(0x1);
    write_byte(Misc_Y_HighPos + y, a); // set vertical high byte
    write_byte(Misc_State + y, a); // set state for misc object
    write_byte(Square2SoundQueue, a); // load coin grab sound
    write_byte(ObjectOffset, x); // store current control bit as misc object offset 
    jsr(GiveOneCoin, 148); // update coin tally on the screen and coin amount variable
    inc_abs(CoinTallyFor1Ups); // increment coin tally used to activate 1-up block flag
    goto rts;
    // -------------------------------------------------------------------------------------
  
MiscObjectsCore:
    ldx_imm(0x8); // set at end of misc object buffer
  
MiscLoop:
    write_byte(ObjectOffset, x); // store misc object offset here
    lda_zpx(Misc_State); // check misc object state
    if (zero_flag) { goto MiscLoopBack; } // branch to check next slot
    asl_acc(); // otherwise shift d7 into carry
    if (!carry_flag) { goto ProcJumpCoin; } // if d7 not set, jumping coin, thus skip to rest of code here
    jsr(ProcHammerObj, 149); // otherwise go to process hammer,
    goto MiscLoopBack; // then check next slot
    // --------------------------------
    // $00 - used to set downward force
    // $01 - used to set upward force (residual)
    // $02 - used to set maximum speed
  
ProcJumpCoin:
    ldy_zpx(Misc_State); // check misc object state
    dey(); // decrement to see if it's set to 1
    if (zero_flag) { goto JCoinRun; } // if so, branch to handle jumping coin
    inc_zpx(Misc_State); // otherwise increment state to either start off or as timer
    lda_zpx(Misc_X_Position); // get horizontal coordinate for misc object
    carry_flag = false; // whether its jumping coin (state 0 only) or floatey number
    adc_abs(ScrollAmount); // add current scroll speed
    write_byte(Misc_X_Position + x, a); // store as new horizontal coordinate
    lda_zpx(Misc_PageLoc); // get page location
    adc_imm(0x0); // add carry
    write_byte(Misc_PageLoc + x, a); // store as new page location
    lda_zpx(Misc_State);
    cmp_imm(0x30); // check state of object for preset value
    if (!zero_flag) { goto RunJCSubs; } // if not yet reached, branch to subroutines
    lda_imm(0x0);
    write_byte(Misc_State + x, a); // otherwise nullify object state
    goto MiscLoopBack; // and move onto next slot
  
JCoinRun:
    txa();
    carry_flag = false; // add 13 bytes to offset for next subroutine
    adc_imm(0xd);
    tax();
    lda_imm(0x50); // set downward movement amount
    write_byte(0x0, a);
    lda_imm(0x6); // set maximum vertical speed
    write_byte(0x2, a);
    lsr_acc(); // divide by 2 and set
    write_byte(0x1, a); // as upward movement amount (apparently residual)
    lda_imm(0x0); // set A to impose gravity on jumping coin
    ImposeGravity(); // do sub to move coin vertically and impose gravity on it
    ldx_zp(ObjectOffset); // get original misc object offset
    lda_zpx(Misc_Y_Speed); // check vertical speed
    cmp_imm(0x5);
    if (!zero_flag) { goto RunJCSubs; } // if not moving downward fast enough, keep state as-is
    inc_zpx(Misc_State); // otherwise increment state to change to floatey number
  
RunJCSubs:
    jsr(RelativeMiscPosition, 150); // get relative coordinates
    jsr(GetMiscOffscreenBits, 151); // get offscreen information
    jsr(GetMiscBoundBox, 152); // get bounding box coordinates (why?)
    jsr(JCoinGfxHandler, 153); // draw the coin or floatey number
  
MiscLoopBack:
    dex(); // decrement misc object offset
    if (!neg_flag) { goto MiscLoop; } // loop back until all misc objects handled
    goto rts; // then leave
    // -------------------------------------------------------------------------------------
  
GiveOneCoin:
    lda_imm(0x1); // set digit modifier to add 1 coin
    write_byte(DigitModifier + 5, a); // to the current player's coin tally
    ldx_abs(CurrentPlayer); // get current player on the screen
    ldy_absx(CoinTallyOffsets); // get offset for player's coin tally
    jsr(DigitsMathRoutine, 154); // update the coin tally
    inc_abs(CoinTally); // increment onscreen player's coin amount
    lda_abs(CoinTally);
    cmp_imm(100); // does player have 100 coins yet?
    if (!zero_flag) { goto CoinPoints; } // if not, skip all of this
    lda_imm(0x0);
    write_byte(CoinTally, a); // otherwise, reinitialize coin amount
    inc_abs(NumberofLives); // give the player an extra life
    lda_imm(Sfx_ExtraLife);
    write_byte(Square2SoundQueue, a); // play 1-up sound
  
CoinPoints:
    lda_imm(0x2); // set digit modifier to award
    write_byte(DigitModifier + 4, a); // 200 points to the player
  
AddToScore:
    ldx_abs(CurrentPlayer); // get current player
    ldy_absx(ScoreOffsets); // get offset for player's score
    jsr(DigitsMathRoutine, 155); // update the score internally with value in digit modifier
  
GetSBNybbles:
    ldy_abs(CurrentPlayer); // get current player
    lda_absy(StatusBarNybbles); // get nybbles based on player, use to update score and coins
  
UpdateNumber:
    PrintStatusBarNumbers(); // print status bar numbers based on nybbles, whatever they be
    ldy_abs(VRAM_Buffer1_Offset);
    lda_absy(VRAM_Buffer1 - 6); // check highest digit of score
    if (!zero_flag) { goto NoZSup; } // if zero, overwrite with space tile for zero suppression
    lda_imm(0x24);
    write_byte(VRAM_Buffer1 - 6 + y, a);
  
NoZSup:
    ldx_zp(ObjectOffset); // get enemy object buffer offset
    goto rts;
    // -------------------------------------------------------------------------------------
  
SetupPowerUp:
    lda_imm(PowerUpObject); // load power-up identifier into
    write_byte(Enemy_ID + 5, a); // special use slot of enemy object buffer
    lda_zpx(Block_PageLoc); // store page location of block object
    write_byte(Enemy_PageLoc + 5, a); // as page location of power-up object
    lda_zpx(Block_X_Position); // store horizontal coordinate of block object
    write_byte(Enemy_X_Position + 5, a); // as horizontal coordinate of power-up object
    lda_imm(0x1);
    write_byte(Enemy_Y_HighPos + 5, a); // set vertical high byte of power-up object
    lda_zpx(Block_Y_Position); // get vertical coordinate of block object
    carry_flag = true;
    sbc_imm(0x8); // subtract 8 pixels
    write_byte(Enemy_Y_Position + 5, a); // and use as vertical coordinate of power-up object
    PwrUpJmp(); goto rts; // <fallthrough>
    // -------------------------------------------------------------------------------------
  
PowerUpObjHandler:
    ldx_imm(0x5); // set object offset for last slot in enemy object buffer
    write_byte(ObjectOffset, x);
    lda_zp(Enemy_State + 5); // check power-up object's state
    if (zero_flag) { goto ExitPUp; } // if not set, branch to leave
    asl_acc(); // shift to check if d7 was set in object state
    if (!carry_flag) { goto GrowThePowerUp; } // if not set, branch ahead to skip this part
    lda_abs(TimerControl); // if master timer control set,
    if (!zero_flag) { goto RunPUSubs; } // branch ahead to enemy object routines
    lda_zp(PowerUpType); // check power-up type
    if (zero_flag) { goto ShroomM; } // if normal mushroom, branch ahead to move it
    cmp_imm(0x3);
    if (zero_flag) { goto ShroomM; } // if 1-up mushroom, branch ahead to move it
    cmp_imm(0x2);
    if (!zero_flag) { goto RunPUSubs; } // if not star, branch elsewhere to skip movement
    jsr(MoveJumpingEnemy, 156); // otherwise impose gravity on star power-up and make it jump
    jsr(EnemyJump, 157); // note that green paratroopa shares the same code here 
    goto RunPUSubs; // then jump to other power-up subroutines
  
ShroomM:
    jsr(MoveNormalEnemy, 158); // do sub to make mushrooms move
    jsr(EnemyToBGCollisionDet, 159); // deal with collisions
    goto RunPUSubs; // run the other subroutines
  
GrowThePowerUp:
    lda_zp(FrameCounter); // get frame counter
    and_imm(0x3); // mask out all but 2 LSB
    if (!zero_flag) { goto ChkPUSte; } // if any bits set here, branch
    dec_zp(Enemy_Y_Position + 5); // otherwise decrement vertical coordinate slowly
    lda_zp(Enemy_State + 5); // load power-up object state
    inc_zp(Enemy_State + 5); // increment state for next frame (to make power-up rise)
    cmp_imm(0x11); // if power-up object state not yet past 16th pixel,
    if (!carry_flag) { goto ChkPUSte; } // branch ahead to last part here
    lda_imm(0x10);
    write_byte(Enemy_X_Speed + x, a); // otherwise set horizontal speed
    lda_imm(0b10000000);
    write_byte(Enemy_State + 5, a); // and then set d7 in power-up object's state
    asl_acc(); // shift once to init A
    write_byte(Enemy_SprAttrib + 5, a); // initialize background priority bit set here
    rol_acc(); // rotate A to set right moving direction
    write_byte(Enemy_MovingDir + x, a); // set moving direction
  
ChkPUSte:
    lda_zp(Enemy_State + 5); // check power-up object's state
    cmp_imm(0x6); // for if power-up has risen enough
    if (!carry_flag) { goto ExitPUp; } // if not, don't even bother running these routines
  
RunPUSubs:
    jsr(RelativeEnemyPosition, 160); // get coordinates relative to screen
    jsr(GetEnemyOffscreenBits, 161); // get offscreen bits
    jsr(GetEnemyBoundBox, 162); // get bounding box coordinates
    jsr(DrawPowerUp, 163); // draw the power-up object
    jsr(PlayerEnemyCollision, 164); // check for collision with player
    OffscreenBoundsCheck(); // check to see if it went offscreen
  
ExitPUp:
    goto rts; // and we're done
    // -------------------------------------------------------------------------------------
    // These apply to all routines in this section unless otherwise noted:
    // $00 - used to store metatile from block buffer routine
    // $02 - used to store vertical high nybble offset from block buffer routine
    // $05 - used to store metatile stored in A at beginning of PlayerHeadCollision
    // $06-$07 - used as block buffer address indirect
  
PlayerHeadCollision:
    pha(); // store metatile number to stack
    lda_imm(0x11); // load unbreakable block object state by default
    ldx_abs(SprDataOffset_Ctrl); // load offset control bit here
    ldy_abs(PlayerSize); // check player's size
    if (!zero_flag) { goto DBlockSte; } // if small, branch
    lda_imm(0x12); // otherwise load breakable block object state
  
DBlockSte:
    write_byte(Block_State + x, a); // store into block object buffer
    jsr(DestroyBlockMetatile, 165); // store blank metatile in vram buffer to write to name table
    ldx_abs(SprDataOffset_Ctrl); // load offset control bit
    lda_zp(0x2); // get vertical high nybble offset used in block buffer routine
    write_byte(Block_Orig_YPos + x, a); // set as vertical coordinate for block object
    tay();
    lda_zp(0x6); // get low byte of block buffer address used in same routine
    write_byte(Block_BBuf_Low + x, a); // save as offset here to be used later
    lda_indy(0x6); // get contents of block buffer at old address at $06, $07
    BlockBumpedChk(); // do a sub to check which block player bumped head on
    write_byte(0x0, a); // store metatile here
    ldy_abs(PlayerSize); // check player's size
    if (!zero_flag) { goto ChkBrick; } // if small, use metatile itself as contents of A
    tya(); // otherwise init A (note: big = 0)
  
ChkBrick:
    if (!carry_flag) { goto PutMTileB; } // if no match was found in previous sub, skip ahead
    ldy_imm(0x11); // otherwise load unbreakable state into block object buffer
    write_byte(Block_State + x, y); // note this applies to both player sizes
    lda_imm(0xc4); // load empty block metatile into A for now
    ldy_zp(0x0); // get metatile from before
    cpy_imm(0x58); // is it brick with coins (with line)?
    if (zero_flag) { goto StartBTmr; } // if so, branch
    cpy_imm(0x5d); // is it brick with coins (without line)?
    if (!zero_flag) { goto PutMTileB; } // if not, branch ahead to store empty block metatile
  
StartBTmr:
    lda_abs(BrickCoinTimerFlag); // check brick coin timer flag
    if (!zero_flag) { goto ContBTmr; } // if set, timer expired or counting down, thus branch
    lda_imm(0xb);
    write_byte(BrickCoinTimer, a); // if not set, set brick coin timer
    inc_abs(BrickCoinTimerFlag); // and set flag linked to it
  
ContBTmr:
    lda_abs(BrickCoinTimer); // check brick coin timer
    if (!zero_flag) { goto PutOldMT; } // if not yet expired, branch to use current metatile
    ldy_imm(0xc4); // otherwise use empty block metatile
  
PutOldMT:
    tya(); // put metatile into A
  
PutMTileB:
    write_byte(Block_Metatile + x, a); // store whatever metatile be appropriate here
    InitBlock_XY_Pos(); // get block object horizontal coordinates saved
    ldy_zp(0x2); // get vertical high nybble offset
    lda_imm(0x23);
    write_byte(read_word(0x6) + y, a); // write blank metatile $23 to block buffer
    lda_imm(0x10);
    write_byte(BlockBounceTimer, a); // set block bounce timer
    pla(); // pull original metatile from stack
    write_byte(0x5, a); // and save here
    ldy_imm(0x0); // set default offset
    lda_abs(CrouchingFlag); // is player crouching?
    if (!zero_flag) { goto SmallBP; } // if so, branch to increment offset
    lda_abs(PlayerSize); // is player big?
    if (zero_flag) { goto BigBP; } // if so, branch to use default offset
  
SmallBP:
    iny(); // increment for small or big and crouching
  
BigBP:
    lda_zp(Player_Y_Position); // get player's vertical coordinate
    carry_flag = false;
    adc_absy(BlockYPosAdderData); // add value determined by size
    and_imm(0xf0); // mask out low nybble to get 16-pixel correspondence
    write_byte(Block_Y_Position + x, a); // save as vertical coordinate for block object
    ldy_zpx(Block_State); // get block object state
    cpy_imm(0x11);
    if (zero_flag) { goto Unbreak; } // if set to value loaded for unbreakable, branch
    jsr(BrickShatter, 166); // execute code for breakable brick
    goto InvOBit; // skip subroutine to do last part of code here
  
Unbreak:
    jsr(BumpBlock, 167); // execute code for unbreakable brick or question block
  
InvOBit:
    lda_abs(SprDataOffset_Ctrl); // invert control bit used by block objects
    eor_imm(0x1); // and floatey numbers
    write_byte(SprDataOffset_Ctrl, a);
    goto rts; // leave!
    // --------------------------------
  
BumpBlock:
    jsr(CheckTopOfBlock, 168); // check to see if there's a coin directly above this block
    lda_imm(Sfx_Bump);
    write_byte(Square1SoundQueue, a); // play bump sound
    lda_imm(0x0);
    write_byte(Block_X_Speed + x, a); // initialize horizontal speed for block object
    write_byte(Block_Y_MoveForce + x, a); // init fractional movement force
    write_byte(Player_Y_Speed, a); // init player's vertical speed
    lda_imm(0xfe);
    write_byte(Block_Y_Speed + x, a); // set vertical speed for block object
    lda_zp(0x5); // get original metatile from stack
    BlockBumpedChk(); // do a sub to check which block player bumped head on
    if (!carry_flag) { goto ExitBlockChk; } // if no match was found, branch to leave
    tya(); // move block number to A
    cmp_imm(0x9); // if block number was within 0-8 range,
    if (!carry_flag) { goto BlockCode; } // branch to use current number
    sbc_imm(0x5); // otherwise subtract 5 for second set to get proper number
  
BlockCode:
    // jsr JumpEngine
    switch (a) {
      case 0: goto MushFlowerBlock;
      case 1: goto CoinBlock;
      case 2: goto CoinBlock;
      case 3: goto ExtraLifeMushBlock;
      case 4: goto MushFlowerBlock;
      case 5: goto VineBlock;
      case 6: goto StarBlock;
      case 7: goto CoinBlock;
      case 8: goto ExtraLifeMushBlock;
    }
  
MushFlowerBlock:
    lda_imm(0x0); // load mushroom/fire flower into power-up type
    goto ExtraLifeMushBlockSkip; //  .db $2c ;BIT instruction opcode
  
StarBlock:
    lda_imm(0x2); // load star into power-up type
    goto ExtraLifeMushBlockSkip; //  .db $2c ;BIT instruction opcode
  
ExtraLifeMushBlock:
    lda_imm(0x3); // load 1-up mushroom into power-up type
  
ExtraLifeMushBlockSkip:
    write_byte(0x39, a); // store correct power-up type
    goto SetupPowerUp;
  
VineBlock:
    ldx_imm(0x5); // load last slot for enemy object buffer
    ldy_abs(SprDataOffset_Ctrl); // get control bit
    Setup_Vine(); // set up vine object
  
ExitBlockChk:
    goto rts; // leave
    // --------------------------------
    // --------------------------------
  
BrickShatter:
    jsr(CheckTopOfBlock, 169); // check to see if there's a coin directly above this block
    lda_imm(Sfx_BrickShatter);
    write_byte(Block_RepFlag + x, a); // set flag for block object to immediately replace metatile
    write_byte(NoiseSoundQueue, a); // load brick shatter sound
    SpawnBrickChunks(); // create brick chunk objects
    lda_imm(0xfe);
    write_byte(Player_Y_Speed, a); // set vertical speed for player
    lda_imm(0x5);
    write_byte(DigitModifier + 5, a); // set digit modifier to give player 50 points
    jsr(AddToScore, 170); // do sub to update the score
    ldx_abs(SprDataOffset_Ctrl); // load control bit and leave
    goto rts;
    // --------------------------------
  
CheckTopOfBlock:
    ldx_abs(SprDataOffset_Ctrl); // load control bit
    ldy_zp(0x2); // get vertical high nybble offset used in block buffer
    if (zero_flag) { goto TopEx; } // branch to leave if set to zero, because we're at the top
    tya(); // otherwise set to A
    carry_flag = true;
    sbc_imm(0x10); // subtract $10 to move up one row in the block buffer
    write_byte(0x2, a); // store as new vertical high nybble offset
    tay();
    lda_indy(0x6); // get contents of block buffer in same column, one row up
    cmp_imm(0xc2); // is it a coin? (not underwater)
    if (!zero_flag) { goto TopEx; } // if not, branch to leave
    lda_imm(0x0);
    write_byte(read_word(0x6) + y, a); // otherwise put blank metatile where coin was
    jsr(RemoveCoin_Axe, 171); // write blank metatile to vram buffer
    ldx_abs(SprDataOffset_Ctrl); // get control bit
    jsr(SetupJumpCoin, 172); // create jumping coin object and update coin variables
  
TopEx:
    goto rts; // leave!
    // --------------------------------
  
BlockObjectsCore:
    lda_zpx(Block_State); // get state of block object
    if (zero_flag) { goto UpdSte; } // if not set, branch to leave
    and_imm(0xf); // mask out high nybble
    pha(); // push to stack
    tay(); // put in Y for now
    txa();
    carry_flag = false;
    adc_imm(0x9); // add 9 bytes to offset (note two block objects are created
    tax(); // when using brick chunks, but only one offset for both)
    dey(); // decrement Y to check for solid block state
    if (zero_flag) { goto BouncingBlockHandler; } // branch if found, otherwise continue for brick chunks
    jsr(ImposeGravityBlock, 173); // do sub to impose gravity on one block object object
    jsr(MoveObjectHorizontally, 174); // do another sub to move horizontally
    txa();
    carry_flag = false; // move onto next block object
    adc_imm(0x2);
    tax();
    jsr(ImposeGravityBlock, 175); // do sub to impose gravity on other block object
    jsr(MoveObjectHorizontally, 176); // do another sub to move horizontally
    ldx_zp(ObjectOffset); // get block object offset used for both
    jsr(RelativeBlockPosition, 177); // get relative coordinates
    jsr(GetBlockOffscreenBits, 178); // get offscreen information
    jsr(DrawBrickChunks, 179); // draw the brick chunks
    pla(); // get lower nybble of saved state
    ldy_zpx(Block_Y_HighPos); // check vertical high byte of block object
    if (zero_flag) { goto UpdSte; } // if above the screen, branch to kill it
    pha(); // otherwise save state back into stack
    lda_imm(0xf0);
    cmp_zpx(Block_Y_Position + 2); // check to see if bottom block object went
    if (carry_flag) { goto ChkTop; } // to the bottom of the screen, and branch if not
    write_byte(Block_Y_Position + 2 + x, a); // otherwise set offscreen coordinate
  
ChkTop:
    lda_zpx(Block_Y_Position); // get top block object's vertical coordinate
    cmp_imm(0xf0); // see if it went to the bottom of the screen
    pla(); // pull block object state from stack
    if (!carry_flag) { goto UpdSte; } // if not, branch to save state
    if (carry_flag) { goto KillBlock; } // otherwise do unconditional branch to kill it
  
BouncingBlockHandler:
    jsr(ImposeGravityBlock, 180); // do sub to impose gravity on block object
    ldx_zp(ObjectOffset); // get block object offset
    jsr(RelativeBlockPosition, 181); // get relative coordinates
    jsr(GetBlockOffscreenBits, 182); // get offscreen information
    jsr(DrawBlock, 183); // draw the block
    lda_zpx(Block_Y_Position); // get vertical coordinate
    and_imm(0xf); // mask out high nybble
    cmp_imm(0x5); // check to see if low nybble wrapped around
    pla(); // pull state from stack
    if (carry_flag) { goto UpdSte; } // if still above amount, not time to kill block yet, thus branch
    lda_imm(0x1);
    write_byte(Block_RepFlag + x, a); // otherwise set flag to replace metatile
  
KillBlock:
    lda_imm(0x0); // if branched here, nullify object state
  
UpdSte:
    write_byte(Block_State + x, a); // store contents of A in block object state
    goto rts;
    // -------------------------------------------------------------------------------------
    // $02 - used to store offset to block buffer
    // $06-$07 - used to store block buffer address
  
BlockObjMT_Updater:
    ldx_imm(0x1); // set offset to start with second block object
  
UpdateLoop:
    write_byte(ObjectOffset, x); // set offset here
    lda_abs(VRAM_Buffer1); // if vram buffer already being used here,
    if (!zero_flag) { goto NextBUpd; } // branch to move onto next block object
    lda_absx(Block_RepFlag); // if flag for block object already clear,
    if (zero_flag) { goto NextBUpd; } // branch to move onto next block object
    lda_absx(Block_BBuf_Low); // get low byte of block buffer
    write_byte(0x6, a); // store into block buffer address
    lda_imm(0x5);
    write_byte(0x7, a); // set high byte of block buffer address
    lda_absx(Block_Orig_YPos); // get original vertical coordinate of block object
    write_byte(0x2, a); // store here and use as offset to block buffer
    tay();
    lda_absx(Block_Metatile); // get metatile to be written
    write_byte(read_word(0x6) + y, a); // write it to the block buffer
    jsr(ReplaceBlockMetatile, 184); // do sub to replace metatile where block object is
    lda_imm(0x0);
    write_byte(Block_RepFlag + x, a); // clear block object flag
  
NextBUpd:
    dex(); // decrement block object offset
    if (!neg_flag) { goto UpdateLoop; } // do this until both block objects are dealt with
    goto rts; // then leave
    // -------------------------------------------------------------------------------------
    // $00 - used to store high nybble of horizontal speed as adder
    // $01 - used to store low nybble of horizontal speed
    // $02 - used to store adder to page location
  
MoveEnemyHorizontally:
    inx(); // increment offset for enemy offset
    jsr(MoveObjectHorizontally, 185); // position object horizontally according to
    ldx_zp(ObjectOffset); // counters, return with saved value in A,
    goto rts; // put enemy offset back in X and leave
  
MovePlayerHorizontally:
    lda_abs(JumpspringAnimCtrl); // if jumpspring currently animating,
    if (!zero_flag) { goto ExXMove; } // branch to leave
    tax(); // otherwise set zero for offset to use player's stuff
  
MoveObjectHorizontally:
    lda_zpx(SprObject_X_Speed); // get currently saved value (horizontal
    asl_acc(); // speed, secondary counter, whatever)
    asl_acc(); // and move low nybble to high
    asl_acc();
    asl_acc();
    write_byte(0x1, a); // store result here
    lda_zpx(SprObject_X_Speed); // get saved value again
    lsr_acc(); // move high nybble to low
    lsr_acc();
    lsr_acc();
    lsr_acc();
    cmp_imm(0x8); // if < 8, branch, do not change
    if (!carry_flag) { goto SaveXSpd; }
    ora_imm(0b11110000); // otherwise alter high nybble
  
SaveXSpd:
    write_byte(0x0, a); // save result here
    ldy_imm(0x0); // load default Y value here
    cmp_imm(0x0); // if result positive, leave Y alone
    if (!neg_flag) { goto UseAdder; }
    dey(); // otherwise decrement Y
  
UseAdder:
    write_byte(0x2, y); // save Y here
    lda_absx(SprObject_X_MoveForce); // get whatever number's here
    carry_flag = false;
    adc_zp(0x1); // add low nybble moved to high
    write_byte(SprObject_X_MoveForce + x, a); // store result here
    lda_imm(0x0); // init A
    rol_acc(); // rotate carry into d0
    pha(); // push onto stack
    ror_acc(); // rotate d0 back onto carry
    lda_zpx(SprObject_X_Position);
    adc_zp(0x0); // add carry plus saved value (high nybble moved to low
    write_byte(SprObject_X_Position + x, a); // plus $f0 if necessary) to object's horizontal position
    lda_zpx(SprObject_PageLoc);
    adc_zp(0x2); // add carry plus other saved value to the
    write_byte(SprObject_PageLoc + x, a); // object's page location and save
    pla();
    carry_flag = false; // pull old carry from stack and add
    adc_zp(0x0); // to high nybble moved to low
  
ExXMove:
    goto rts; // and leave
    // -------------------------------------------------------------------------------------
    // $00 - used for downward force
    // $01 - used for upward force
    // $02 - used for maximum vertical speed
  
MovePlayerVertically:
    ldx_imm(0x0); // set X for player offset
    lda_abs(TimerControl);
    if (!zero_flag) { goto NoJSChk; } // if master timer control set, branch ahead
    lda_abs(JumpspringAnimCtrl); // otherwise check to see if jumpspring is animating
    if (!zero_flag) { goto ExXMove; } // branch to leave if so
  
NoJSChk:
    lda_abs(VerticalForce); // dump vertical force 
    write_byte(0x0, a);
    lda_imm(0x4); // set maximum vertical speed here
    goto ImposeGravitySprObj; // then jump to move player vertically
    // --------------------------------
  
MoveD_EnemyVertically:
    ldy_imm(0x3d); // set quick movement amount downwards
    lda_zpx(Enemy_State); // then check enemy state
    cmp_imm(0x5); // if not set to unique state for spiny's egg, go ahead
    if (!zero_flag) { goto ContVMove; } // and use, otherwise set different movement amount, continue on
  
MoveFallingPlatform:
    ldy_imm(0x20); // set movement amount
  
ContVMove:
    goto SetHiMax; // jump to skip the rest of this
    // --------------------------------
  
MoveRedPTroopaDown:
    ldy_imm(0x0); // set Y to move downwards
    goto MoveRedPTroopa; // skip to movement routine
  
MoveRedPTroopaUp:
    ldy_imm(0x1); // set Y to move upwards
  
MoveRedPTroopa:
    inx(); // increment X for enemy offset
    lda_imm(0x3);
    write_byte(0x0, a); // set downward movement amount here
    lda_imm(0x6);
    write_byte(0x1, a); // set upward movement amount here
    lda_imm(0x2);
    write_byte(0x2, a); // set maximum speed here
    tya(); // set movement direction in A, and
    goto RedPTroopaGrav; // jump to move this thing
    // --------------------------------
  
MoveDropPlatform:
    ldy_imm(0x7f); // set movement amount for drop platform
    if (!zero_flag) { goto SetMdMax; } // skip ahead of other value set here
  
MoveEnemySlowVert:
    ldy_imm(0xf); // set movement amount for bowser/other objects
  
SetMdMax:
    lda_imm(0x2); // set maximum speed in A
    if (!zero_flag) { goto SetXMoveAmt; } // unconditional branch
    // --------------------------------
  
MoveJ_EnemyVertically:
    ldy_imm(0x1c); // set movement amount for podoboo/other objects
  
SetHiMax:
    lda_imm(0x3); // set maximum speed in A
  
SetXMoveAmt:
    write_byte(0x0, y); // set movement amount here
    inx(); // increment X for enemy offset
    jsr(ImposeGravitySprObj, 186); // do a sub to move enemy object downwards
    ldx_zp(ObjectOffset); // get enemy object buffer offset and leave
    goto rts;
    // --------------------------------
  
ImposeGravityBlock:
    ldy_imm(0x1); // set offset for maximum speed
    lda_imm(0x50); // set movement amount here
    write_byte(0x0, a);
    lda_absy(MaxSpdBlockData); // get maximum speed
  
ImposeGravitySprObj:
    write_byte(0x2, a); // set maximum speed here
    lda_imm(0x0); // set value to move downwards
    ImposeGravity(); goto rts; // jump to the code that actually moves it
    // --------------------------------
  
MovePlatformDown:
    lda_imm(0x0); // save value to stack (if branching here, execute next
    goto MovePlatformUpSkip; //  .db $2c     ;part as BIT instruction)
  
MovePlatformUp:
    lda_imm(0x1); // save value to stack
  
MovePlatformUpSkip:
    pha();
    ldy_zpx(Enemy_ID); // get enemy object identifier
    inx(); // increment offset for enemy object
    lda_imm(0x5); // load default value here
    cpy_imm(0x29); // residual comparison, object #29 never executes
    if (!zero_flag) { goto SetDplSpd; } // this code, thus unconditional branch here
    lda_imm(0x9); // residual code
  
SetDplSpd:
    write_byte(0x0, a); // save downward movement amount here
    lda_imm(0xa); // save upward movement amount here
    write_byte(0x1, a);
    lda_imm(0x3); // save maximum vertical speed here
    write_byte(0x2, a);
    pla(); // get value from stack
    tay(); // use as Y, then move onto code shared by red koopa
  
RedPTroopaGrav:
    ImposeGravity(); // do a sub to move object gradually
    ldx_zp(ObjectOffset); // get enemy object offset and leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used for downward force
    // $01 - used for upward force
    // $07 - used as adder for vertical position
    // -------------------------------------------------------------------------------------
  
EnemiesAndLoopsCore:
    lda_zpx(Enemy_Flag); // check data here for MSB set
    pha(); // save in stack
    asl_acc();
    if (carry_flag) { goto ChkBowserF; } // if MSB set in enemy flag, branch ahead of jumps
    pla(); // get from stack
    if (zero_flag) { goto ChkAreaTsk; } // if data zero, branch
    goto RunEnemyObjectsCore; // otherwise, jump to run enemy subroutines
  
ChkAreaTsk:
    lda_abs(AreaParserTaskNum); // check number of tasks to perform
    and_imm(0x7);
    cmp_imm(0x7); // if at a specific task, jump and leave
    if (zero_flag) { goto ExitELCore; }
    goto ProcLoopCommand; // otherwise, jump to process loop command/load enemies
  
ChkBowserF:
    pla(); // get data from stack
    and_imm(0b00001111); // mask out high nybble
    tay();
    lda_zpy(Enemy_Flag); // use as pointer and load same place with different offset
    if (!zero_flag) { goto ExitELCore; }
    write_byte(Enemy_Flag + x, a); // if second enemy flag not set, also clear first one
  
ExitELCore:
    goto rts;
    // --------------------------------
    // loop command data
  
ExecGameLoopback:
    lda_zp(Player_PageLoc); // send player back four pages
    carry_flag = true;
    sbc_imm(0x4);
    write_byte(Player_PageLoc, a);
    lda_abs(CurrentPageLoc); // send current page back four pages
    carry_flag = true;
    sbc_imm(0x4);
    write_byte(CurrentPageLoc, a);
    lda_abs(ScreenLeft_PageLoc); // subtract four from page location
    carry_flag = true; // of screen's left border
    sbc_imm(0x4);
    write_byte(ScreenLeft_PageLoc, a);
    lda_abs(ScreenRight_PageLoc); // do the same for the page location
    carry_flag = true; // of screen's right border
    sbc_imm(0x4);
    write_byte(ScreenRight_PageLoc, a);
    lda_abs(AreaObjectPageLoc); // subtract four from page control
    carry_flag = true; // for area objects
    sbc_imm(0x4);
    write_byte(AreaObjectPageLoc, a);
    lda_imm(0x0); // initialize page select for both
    write_byte(EnemyObjectPageSel, a); // area and enemy objects
    write_byte(AreaObjectPageSel, a);
    write_byte(EnemyDataOffset, a); // initialize enemy object data offset
    write_byte(EnemyObjectPageLoc, a); // and enemy object page control
    lda_absy(AreaDataOfsLoopback); // adjust area object offset based on
    write_byte(AreaDataOffset, a); // which loop command we encountered
    goto rts;
  
ProcLoopCommand:
    lda_abs(LoopCommand); // check if loop command was found
    if (zero_flag) { goto ChkEnemyFrenzy; }
    lda_abs(CurrentColumnPos); // check to see if we're still on the first page
    if (!zero_flag) { goto ChkEnemyFrenzy; } // if not, do not loop yet
    ldy_imm(0xb); // start at the end of each set of loop data
  
FindLoop:
    dey();
    if (neg_flag) { goto ChkEnemyFrenzy; } // if all data is checked and not match, do not loop
    lda_abs(WorldNumber); // check to see if one of the world numbers
    cmp_absy(LoopCmdWorldNumber); // matches our current world number
    if (!zero_flag) { goto FindLoop; }
    lda_abs(CurrentPageLoc); // check to see if one of the page numbers
    cmp_absy(LoopCmdPageNumber); // matches the page we're currently on
    if (!zero_flag) { goto FindLoop; }
    lda_zp(Player_Y_Position); // check to see if the player is at the correct position
    cmp_absy(LoopCmdYPosition); // if not, branch to check for world 7
    if (!zero_flag) { goto WrongChk; }
    lda_zp(Player_State); // check to see if the player is
    cmp_imm(0x0); // on solid ground (i.e. not jumping or falling)
    if (!zero_flag) { goto WrongChk; } // if not, player fails to pass loop, and loopback
    lda_abs(WorldNumber); // are we in world 7? (check performed on correct
    cmp_imm(World7); // vertical position and on solid ground)
    if (!zero_flag) { goto InitMLp; } // if not, initialize flags used there, otherwise
    inc_abs(MultiLoopCorrectCntr); // increment counter for correct progression
  
IncMLoop:
    inc_abs(MultiLoopPassCntr); // increment master multi-part counter
    lda_abs(MultiLoopPassCntr); // have we done all three parts?
    cmp_imm(0x3);
    if (!zero_flag) { goto InitLCmd; } // if not, skip this part
    lda_abs(MultiLoopCorrectCntr); // if so, have we done them all correctly?
    cmp_imm(0x3);
    if (zero_flag) { goto InitMLp; } // if so, branch past unnecessary check here
    if (!zero_flag) { goto DoLpBack; } // unconditional branch if previous branch fails
  
WrongChk:
    lda_abs(WorldNumber); // are we in world 7? (check performed on
    cmp_imm(World7); // incorrect vertical position or not on solid ground)
    if (zero_flag) { goto IncMLoop; }
  
DoLpBack:
    jsr(ExecGameLoopback, 187); // if player is not in right place, loop back
    jsr(KillAllEnemies, 188);
  
InitMLp:
    lda_imm(0x0); // initialize counters used for multi-part loop commands
    write_byte(MultiLoopPassCntr, a);
    write_byte(MultiLoopCorrectCntr, a);
  
InitLCmd:
    lda_imm(0x0); // initialize loop command flag
    write_byte(LoopCommand, a);
    // --------------------------------
  
ChkEnemyFrenzy:
    lda_abs(EnemyFrenzyQueue); // check for enemy object in frenzy queue
    if (zero_flag) { goto ProcessEnemyData; } // if not, skip this part
    write_byte(Enemy_ID + x, a); // store as enemy object identifier here
    lda_imm(0x1);
    write_byte(Enemy_Flag + x, a); // activate enemy object flag
    lda_imm(0x0);
    write_byte(Enemy_State + x, a); // initialize state and frenzy queue
    write_byte(EnemyFrenzyQueue, a);
    goto InitEnemyObject; // and then jump to deal with this enemy
    // --------------------------------
    // $06 - used to hold page location of extended right boundary
    // $07 - used to hold high nybble of position of extended right boundary
  
ProcessEnemyData:
    ldy_abs(EnemyDataOffset); // get offset of enemy object data
    lda_indy(EnemyData); // load first byte
    cmp_imm(0xff); // check for EOD terminator
    if (!zero_flag) { goto CheckEndofBuffer; }
    goto CheckFrenzyBuffer; // if found, jump to check frenzy buffer, otherwise
  
CheckEndofBuffer:
    and_imm(0b00001111); // check for special row $0e
    cmp_imm(0xe);
    if (zero_flag) { goto CheckRightBounds; } // if found, branch, otherwise
    cpx_imm(0x5); // check for end of buffer
    if (!carry_flag) { goto CheckRightBounds; } // if not at end of buffer, branch
    iny();
    lda_indy(EnemyData); // check for specific value here
    and_imm(0b00111111); // not sure what this was intended for, exactly
    cmp_imm(0x2e); // this part is quite possibly residual code
    if (zero_flag) { goto CheckRightBounds; } // but it has the effect of keeping enemies out of
    goto rts; // the sixth slot
  
CheckRightBounds:
    lda_abs(ScreenRight_X_Pos); // add 48 to pixel coordinate of right boundary
    carry_flag = false;
    adc_imm(0x30);
    and_imm(0b11110000); // store high nybble
    write_byte(0x7, a);
    lda_abs(ScreenRight_PageLoc); // add carry to page location of right boundary
    adc_imm(0x0);
    write_byte(0x6, a); // store page location + carry
    ldy_abs(EnemyDataOffset);
    iny();
    lda_indy(EnemyData); // if MSB of enemy object is clear, branch to check for row $0f
    asl_acc();
    if (!carry_flag) { goto CheckPageCtrlRow; }
    lda_abs(EnemyObjectPageSel); // if page select already set, do not set again
    if (!zero_flag) { goto CheckPageCtrlRow; }
    inc_abs(EnemyObjectPageSel); // otherwise, if MSB is set, set page select 
    inc_abs(EnemyObjectPageLoc); // and increment page control
  
CheckPageCtrlRow:
    dey();
    lda_indy(EnemyData); // reread first byte
    and_imm(0xf);
    cmp_imm(0xf); // check for special row $0f
    if (!zero_flag) { goto PositionEnemyObj; } // if not found, branch to position enemy object
    lda_abs(EnemyObjectPageSel); // if page select set,
    if (!zero_flag) { goto PositionEnemyObj; } // branch without reading second byte
    iny();
    lda_indy(EnemyData); // otherwise, get second byte, mask out 2 MSB
    and_imm(0b00111111);
    write_byte(EnemyObjectPageLoc, a); // store as page control for enemy object data
    inc_abs(EnemyDataOffset); // increment enemy object data offset 2 bytes
    inc_abs(EnemyDataOffset);
    inc_abs(EnemyObjectPageSel); // set page select for enemy object data and 
    goto ProcLoopCommand; // jump back to process loop commands again
  
PositionEnemyObj:
    lda_abs(EnemyObjectPageLoc); // store page control as page location
    write_byte(Enemy_PageLoc + x, a); // for enemy object
    lda_indy(EnemyData); // get first byte of enemy object
    and_imm(0b11110000);
    write_byte(Enemy_X_Position + x, a); // store column position
    cmp_abs(ScreenRight_X_Pos); // check column position against right boundary
    lda_zpx(Enemy_PageLoc); // without subtracting, then subtract borrow
    sbc_abs(ScreenRight_PageLoc); // from page location
    if (carry_flag) { goto CheckRightExtBounds; } // if enemy object beyond or at boundary, branch
    lda_indy(EnemyData);
    and_imm(0b00001111); // check for special row $0e
    cmp_imm(0xe); // if found, jump elsewhere
    if (zero_flag) { goto ParseRow0e; }
    goto CheckThreeBytes; // if not found, unconditional jump
  
CheckRightExtBounds:
    lda_zp(0x7); // check right boundary + 48 against
    cmp_zpx(Enemy_X_Position); // column position without subtracting,
    lda_zp(0x6); // then subtract borrow from page control temp
    sbc_zpx(Enemy_PageLoc); // plus carry
    if (!carry_flag) { goto CheckFrenzyBuffer; } // if enemy object beyond extended boundary, branch
    lda_imm(0x1); // store value in vertical high byte
    write_byte(Enemy_Y_HighPos + x, a);
    lda_indy(EnemyData); // get first byte again
    asl_acc(); // multiply by four to get the vertical
    asl_acc(); // coordinate
    asl_acc();
    asl_acc();
    write_byte(Enemy_Y_Position + x, a);
    cmp_imm(0xe0); // do one last check for special row $0e
    if (zero_flag) { goto ParseRow0e; } // (necessary if branched to $c1cb)
    iny();
    lda_indy(EnemyData); // get second byte of object
    and_imm(0b01000000); // check to see if hard mode bit is set
    if (zero_flag) { goto CheckForEnemyGroup; } // if not, branch to check for group enemy objects
    lda_abs(SecondaryHardMode); // if set, check to see if secondary hard mode flag
    if (zero_flag) { goto Inc2B; } // is on, and if not, branch to skip this object completely
  
CheckForEnemyGroup:
    lda_indy(EnemyData); // get second byte and mask out 2 MSB
    and_imm(0b00111111);
    cmp_imm(0x37); // check for value below $37
    if (!carry_flag) { goto BuzzyBeetleMutate; }
    cmp_imm(0x3f); // if $37 or greater, check for value
    if (!carry_flag) { goto DoGroup; } // below $3f, branch if below $3f
  
BuzzyBeetleMutate:
    cmp_imm(Goomba); // if below $37, check for goomba
    if (!zero_flag) { goto StrID; } // value ($3f or more always fails)
    ldy_abs(PrimaryHardMode); // check if primary hard mode flag is set
    if (zero_flag) { goto StrID; } // and if so, change goomba to buzzy beetle
    lda_imm(BuzzyBeetle);
  
StrID:
    write_byte(Enemy_ID + x, a); // store enemy object number into buffer
    lda_imm(0x1);
    write_byte(Enemy_Flag + x, a); // set flag for enemy in buffer
    jsr(InitEnemyObject, 189);
    lda_zpx(Enemy_Flag); // check to see if flag is set
    if (!zero_flag) { goto Inc2B; } // if not, leave, otherwise branch
    goto rts;
  
CheckFrenzyBuffer:
    lda_abs(EnemyFrenzyBuffer); // if enemy object stored in frenzy buffer
    if (!zero_flag) { goto StrFre; } // then branch ahead to store in enemy object buffer
    lda_abs(VineFlagOffset); // otherwise check vine flag offset
    cmp_imm(0x1);
    if (!zero_flag) { goto ExEPar; } // if other value <> 1, leave
    lda_imm(VineObject); // otherwise put vine in enemy identifier
  
StrFre:
    write_byte(Enemy_ID + x, a); // store contents of frenzy buffer into enemy identifier value
  
InitEnemyObject:
    lda_imm(0x0); // initialize enemy state
    write_byte(Enemy_State + x, a);
    jsr(CheckpointEnemyID, 190); // jump ahead to run jump engine and subroutines
  
ExEPar:
    goto rts; // then leave
  
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
    cmp_abs(WorldNumber); // is it the same world number as we're on?
    if (!zero_flag) { goto NotUse; } // if not, do not use (this allows multiple uses
    dey(); // of the same area, like the underground bonus areas)
    lda_indy(EnemyData); // otherwise, get second byte and use as offset
    write_byte(AreaPointer, a); // to addresses for level and enemy object data
    iny();
    lda_indy(EnemyData); // get third byte again, and this time mask out
    and_imm(0b00011111); // the 3 MSB from before, save as page number to be
    write_byte(EntrancePage, a); // used upon entry to area, if area is entered
  
NotUse:
    goto Inc3B;
  
CheckThreeBytes:
    ldy_abs(EnemyDataOffset); // load current offset for enemy object data
    lda_indy(EnemyData); // get first byte
    and_imm(0b00001111); // check for special row $0e
    cmp_imm(0xe);
    if (!zero_flag) { goto Inc2B; }
  
Inc3B:
    inc_abs(EnemyDataOffset); // if row = $0e, increment three bytes
  
Inc2B:
    inc_abs(EnemyDataOffset); // otherwise increment two bytes
    inc_abs(EnemyDataOffset);
    lda_imm(0x0); // init page select for enemy objects
    write_byte(EnemyObjectPageSel, a);
    ldx_zp(ObjectOffset); // reload current offset in enemy buffers
    goto rts; // and leave
  
CheckpointEnemyID:
    lda_zpx(Enemy_ID);
    cmp_imm(0x15); // check enemy object identifier for $15 or greater
    if (carry_flag) { goto InitEnemyRoutines; } // and branch straight to the jump engine if found
    tay(); // save identifier in Y register for now
    lda_zpx(Enemy_Y_Position);
    adc_imm(0x8); // add eight pixels to what will eventually be the
    write_byte(Enemy_Y_Position + x, a); // enemy object's vertical coordinate ($00-$14 only)
    lda_imm(0x1);
    write_byte(EnemyOffscrBitsMasked + x, a); // set offscreen masked bit
    tya(); // get identifier back and use as offset for jump engine
  
InitEnemyRoutines:
    // jsr JumpEngine
    switch (a) {
      case 0: goto InitNormalEnemy;
      case 1: goto InitNormalEnemy;
      case 2: goto InitNormalEnemy;
      case 3: goto InitRedKoopa;
      case 4: NoInitCode(); goto rts;
      case 5: goto InitHammerBro;
      case 6: goto InitGoomba;
      case 7: goto InitBloober;
      case 8: InitBulletBill(); goto rts;
      case 9: NoInitCode(); goto rts;
      case 10: goto InitCheepCheep;
      case 11: goto InitCheepCheep;
      case 12: goto InitPodoboo;
      case 13: goto InitPiranhaPlant;
      case 14: goto InitJumpGPTroopa;
      case 15: goto InitRedPTroopa;
      case 16: goto InitHorizFlySwimEnemy;
      case 17: goto InitLakitu;
      case 18: goto InitEnemyFrenzy;
      case 19: NoInitCode(); goto rts;
      case 20: goto InitEnemyFrenzy;
      case 21: goto InitEnemyFrenzy;
      case 22: goto InitEnemyFrenzy;
      case 23: goto InitEnemyFrenzy;
      case 24: EndFrenzy(); goto rts;
      case 25: NoInitCode(); goto rts;
      case 26: NoInitCode(); goto rts;
      case 27: goto InitShortFirebar;
      case 28: goto InitShortFirebar;
      case 29: goto InitShortFirebar;
      case 30: goto InitShortFirebar;
      case 31: goto InitLongFirebar;
      case 32: NoInitCode(); goto rts;
      case 33: NoInitCode(); goto rts;
      case 34: NoInitCode(); goto rts;
      case 35: NoInitCode(); goto rts;
      case 36: InitBalPlatform(); goto rts;
      case 37: InitVertPlatform(); goto rts;
      case 38: goto LargeLiftUp;
      case 39: goto LargeLiftDown;
      case 40: goto InitHoriPlatform;
      case 41: goto InitDropPlatform;
      case 42: goto InitHoriPlatform;
      case 43: goto PlatLiftUp;
      case 44: goto PlatLiftDown;
      case 45: goto InitBowser;
      case 46: PwrUpJmp(); goto rts;
      case 47: Setup_Vine(); goto rts;
      case 48: NoInitCode(); goto rts;
      case 49: NoInitCode(); goto rts;
      case 50: NoInitCode(); goto rts;
      case 51: NoInitCode(); goto rts;
      case 52: NoInitCode(); goto rts;
      case 53: InitRetainerObj(); goto rts;
      case 54: EndOfEnemyInitCode(); goto rts;
    }
  
InitGoomba:
    jsr(InitNormalEnemy, 191); // set appropriate horizontal speed
    goto SmallBBox; // set $09 as bounding box control, set other values
    // --------------------------------
  
InitPodoboo:
    lda_imm(0x2); // set enemy position to below
    write_byte(Enemy_Y_HighPos + x, a); // the bottom of the screen
    write_byte(Enemy_Y_Position + x, a);
    lsr_acc();
    write_byte(EnemyIntervalTimer + x, a); // set timer for enemy
    lsr_acc();
    write_byte(Enemy_State + x, a); // initialize enemy state, then jump to use
    goto SmallBBox; // $09 as bounding box size and set other things
    // --------------------------------
  
InitNormalEnemy:
    ldy_imm(0x1); // load offset of 1 by default
    lda_abs(PrimaryHardMode); // check for primary hard mode flag set
    if (!zero_flag) { goto GetESpd; }
    dey(); // if not set, decrement offset
  
GetESpd:
    lda_absy(NormalXSpdData); // get appropriate horizontal speed
  
SetESpd:
    write_byte(Enemy_X_Speed + x, a); // store as speed for enemy object
    goto TallBBox; // branch to set bounding box control and other data
    // --------------------------------
  
InitRedKoopa:
    jsr(InitNormalEnemy, 192); // load appropriate horizontal speed
    lda_imm(0x1); // set enemy state for red koopa troopa $03
    write_byte(Enemy_State + x, a);
    goto rts;
    // --------------------------------
  
InitHammerBro:
    lda_imm(0x0); // init horizontal speed and timer used by hammer bro
    write_byte(HammerThrowingTimer + x, a); // apparently to time hammer throwing
    write_byte(Enemy_X_Speed + x, a);
    ldy_abs(SecondaryHardMode); // get secondary hard mode flag
    lda_absy(HBroWalkingTimerData);
    write_byte(EnemyIntervalTimer + x, a); // set value as delay for hammer bro to walk left
    lda_imm(0xb); // set specific value for bounding box size control
    goto SetBBox;
    // --------------------------------
  
InitHorizFlySwimEnemy:
    lda_imm(0x0); // initialize horizontal speed
    goto SetESpd;
    // --------------------------------
  
InitBloober:
    lda_imm(0x0); // initialize horizontal speed
    write_byte(BlooperMoveSpeed + x, a);
  
SmallBBox:
    lda_imm(0x9); // set specific bounding box size control
    if (!zero_flag) { goto SetBBox; } // unconditional branch
    // --------------------------------
  
InitRedPTroopa:
    ldy_imm(0x30); // load central position adder for 48 pixels down
    lda_zpx(Enemy_Y_Position); // set vertical coordinate into location to
    write_byte(RedPTroopaOrigXPos + x, a); // be used as original vertical coordinate
    if (!neg_flag) { goto GetCent; } // if vertical coordinate < $80
    ldy_imm(0xe0); // if => $80, load position adder for 32 pixels up
  
GetCent:
    tya(); // send central position adder to A
    adc_zpx(Enemy_Y_Position); // add to current vertical coordinate
    write_byte(RedPTroopaCenterYPos + x, a); // store as central vertical coordinate
  
TallBBox:
    lda_imm(0x3); // set specific bounding box size control
  
SetBBox:
    write_byte(Enemy_BoundBoxCtrl + x, a); // set bounding box control here
    lda_imm(0x2); // set moving direction for left
    write_byte(Enemy_MovingDir + x, a);
    InitVStf(); goto rts; // <fallthrough>
  
InitCheepCheep:
    jsr(SmallBBox, 193); // set vertical bounding box, speed, init others
    lda_absx(PseudoRandomBitReg); // check one portion of LSFR
    and_imm(0b00010000); // get d4 from it
    write_byte(CheepCheepMoveMFlag + x, a); // save as movement flag of some sort
    lda_zpx(Enemy_Y_Position);
    write_byte(CheepCheepOrigYPos + x, a); // save original vertical coordinate here
    goto rts;
    // --------------------------------
  
InitLakitu:
    lda_abs(EnemyFrenzyBuffer); // check to see if an enemy is already in
    if (!zero_flag) { goto KillLakitu; } // the frenzy buffer, and branch to kill lakitu if so
  
SetupLakitu:
    lda_imm(0x0); // erase counter for lakitu's reappearance
    write_byte(LakituReappearTimer, a);
    jsr(InitHorizFlySwimEnemy, 194); // set $03 as bounding box, set other attributes
    goto TallBBox2; // set $03 as bounding box again (not necessary) and leave
  
KillLakitu:
    EraseEnemyObject(); goto rts;
    // --------------------------------
    // $01-$03 - used to hold pseudorandom difference adjusters
  
LakituAndSpinyHandler:
    lda_abs(FrenzyEnemyTimer); // if timer here not expired, leave
    if (!zero_flag) { goto ExLSHand; }
    cpx_imm(0x5); // if we are on the special use slot, leave
    if (carry_flag) { goto ExLSHand; }
    lda_imm(0x80); // set timer
    write_byte(FrenzyEnemyTimer, a);
    ldy_imm(0x4); // start with the last enemy slot
  
ChkLak:
    lda_zpy(Enemy_ID); // check all enemy slots to see
    cmp_imm(Lakitu); // if lakitu is on one of them
    if (zero_flag) { goto CreateSpiny; } // if so, branch out of this loop
    dey(); // otherwise check another slot
    if (!neg_flag) { goto ChkLak; } // loop until all slots are checked
    inc_abs(LakituReappearTimer); // increment reappearance timer
    lda_abs(LakituReappearTimer);
    cmp_imm(0x7); // check to see if we're up to a certain value yet
    if (!carry_flag) { goto ExLSHand; } // if not, leave
    ldx_imm(0x4); // start with the last enemy slot again
  
ChkNoEn:
    lda_zpx(Enemy_Flag); // check enemy buffer flag for non-active enemy slot
    if (zero_flag) { goto CreateL; } // branch out of loop if found
    dex(); // otherwise check next slot
    if (!neg_flag) { goto ChkNoEn; } // branch until all slots are checked
    if (neg_flag) { goto RetEOfs; } // if no empty slots were found, branch to leave
  
CreateL:
    lda_imm(0x0); // initialize enemy state
    write_byte(Enemy_State + x, a);
    lda_imm(Lakitu); // create lakitu enemy object
    write_byte(Enemy_ID + x, a);
    jsr(SetupLakitu, 195); // do a sub to set up lakitu
    lda_imm(0x20);
    jsr(PutAtRightExtent, 196); // finish setting up lakitu
  
RetEOfs:
    ldx_zp(ObjectOffset); // get enemy object buffer offset again and leave
  
ExLSHand:
    goto rts;
    // --------------------------------
  
CreateSpiny:
    lda_zp(Player_Y_Position); // if player above a certain point, branch to leave
    cmp_imm(0x2c);
    if (!carry_flag) { goto ExLSHand; }
    lda_zpy(Enemy_State); // if lakitu is not in normal state, branch to leave
    if (!zero_flag) { goto ExLSHand; }
    lda_zpy(Enemy_PageLoc); // store horizontal coordinates (high and low) of lakitu
    write_byte(Enemy_PageLoc + x, a); // into the coordinates of the spiny we're going to create
    lda_zpy(Enemy_X_Position);
    write_byte(Enemy_X_Position + x, a);
    lda_imm(0x1); // put spiny within vertical screen unit
    write_byte(Enemy_Y_HighPos + x, a);
    lda_zpy(Enemy_Y_Position); // put spiny eight pixels above where lakitu is
    carry_flag = true;
    sbc_imm(0x8);
    write_byte(Enemy_Y_Position + x, a);
    lda_absx(PseudoRandomBitReg); // get 2 LSB of LSFR and save to Y
    and_imm(0b00000011);
    tay();
    ldx_imm(0x2);
  
DifLoop:
    lda_absy(PRDiffAdjustData); // get three values and save them
    write_byte(0x1 + x, a); // to $01-$03
    iny();
    iny(); // increment Y four bytes for each value
    iny();
    iny();
    dex(); // decrement X for each one
    if (!neg_flag) { goto DifLoop; } // loop until all three are written
    ldx_zp(ObjectOffset); // get enemy object buffer offset
    jsr(PlayerLakituDiff, 197); // move enemy, change direction, get value - difference
    ldy_zp(Player_X_Speed); // check player's horizontal speed
    cpy_imm(0x8);
    if (carry_flag) { goto SetSpSpd; } // if moving faster than a certain amount, branch elsewhere
    tay(); // otherwise save value in A to Y for now
    lda_absx(PseudoRandomBitReg + 1);
    and_imm(0b00000011); // get one of the LSFR parts and save the 2 LSB
    if (zero_flag) { goto UsePosv; } // branch if neither bits are set
    tya();
    eor_imm(0b11111111); // otherwise get two's compliment of Y
    tay();
    iny();
  
UsePosv:
    tya(); // put value from A in Y back to A (they will be lost anyway)
  
SetSpSpd:
    jsr(SmallBBox, 198); // set bounding box control, init attributes, lose contents of A
    ldy_imm(0x2);
    write_byte(Enemy_X_Speed + x, a); // set horizontal speed to zero because previous contents
    cmp_imm(0x0); // of A were lost...branch here will never be taken for
    if (neg_flag) { goto SpinyRte; } // the same reason
    dey();
  
SpinyRte:
    write_byte(Enemy_MovingDir + x, y); // set moving direction to the right
    lda_imm(0xfd);
    write_byte(Enemy_Y_Speed + x, a); // set vertical speed to move upwards
    lda_imm(0x1);
    write_byte(Enemy_Flag + x, a); // enable enemy object by setting flag
    lda_imm(0x5);
    write_byte(Enemy_State + x, a); // put spiny in egg state and leave
  
ChpChpEx:
    goto rts;
    // --------------------------------
  
InitLongFirebar:
    jsr(DuplicateEnemyObj, 199); // create enemy object for long firebar
  
InitShortFirebar:
    lda_imm(0x0); // initialize low byte of spin state
    write_byte(FirebarSpinState_Low + x, a);
    lda_zpx(Enemy_ID); // subtract $1b from enemy identifier
    carry_flag = true; // to get proper offset for firebar data
    sbc_imm(0x1b);
    tay();
    lda_absy(FirebarSpinSpdData); // get spinning speed of firebar
    write_byte(FirebarSpinSpeed + x, a);
    lda_absy(FirebarSpinDirData); // get spinning direction of firebar
    write_byte(FirebarSpinDirection + x, a);
    lda_zpx(Enemy_Y_Position);
    carry_flag = false; // add four pixels to vertical coordinate
    adc_imm(0x4);
    write_byte(Enemy_Y_Position + x, a);
    lda_zpx(Enemy_X_Position);
    carry_flag = false; // add four pixels to horizontal coordinate
    adc_imm(0x4);
    write_byte(Enemy_X_Position + x, a);
    lda_zpx(Enemy_PageLoc);
    adc_imm(0x0); // add carry to page location
    write_byte(Enemy_PageLoc + x, a);
    goto TallBBox2; // set bounding box control (not used) and leave
    // --------------------------------
    // $00-$01 - used to hold pseudorandom bits
  
InitFlyingCheepCheep:
    lda_abs(FrenzyEnemyTimer); // if timer here not expired yet, branch to leave
    if (!zero_flag) { goto ChpChpEx; }
    jsr(SmallBBox, 200); // jump to set bounding box size $09 and init other values
    lda_absx(PseudoRandomBitReg + 1);
    and_imm(0b00000011); // set pseudorandom offset here
    tay();
    lda_absy(FlyCCTimerData); // load timer with pseudorandom offset
    write_byte(FrenzyEnemyTimer, a);
    ldy_imm(0x3); // load Y with default value
    lda_abs(SecondaryHardMode);
    if (zero_flag) { goto MaxCC; } // if secondary hard mode flag not set, do not increment Y
    iny(); // otherwise, increment Y to allow as many as four onscreen
  
MaxCC:
    write_byte(0x0, y); // store whatever pseudorandom bits are in Y
    cpx_zp(0x0); // compare enemy object buffer offset with Y
    if (carry_flag) { goto ChpChpEx; } // if X => Y, branch to leave
    lda_absx(PseudoRandomBitReg);
    and_imm(0b00000011); // get last two bits of LSFR, first part
    write_byte(0x0, a); // and store in two places
    write_byte(0x1, a);
    lda_imm(0xfb); // set vertical speed for cheep-cheep
    write_byte(Enemy_Y_Speed + x, a);
    lda_imm(0x0); // load default value
    ldy_zp(Player_X_Speed); // check player's horizontal speed
    if (zero_flag) { goto GSeed; } // if player not moving left or right, skip this part
    lda_imm(0x4);
    cpy_imm(0x19); // if moving to the right but not very quickly,
    if (!carry_flag) { goto GSeed; } // do not change A
    asl_acc(); // otherwise, multiply A by 2
  
GSeed:
    pha(); // save to stack
    carry_flag = false;
    adc_zp(0x0); // add to last two bits of LSFR we saved earlier
    write_byte(0x0, a); // save it there
    lda_absx(PseudoRandomBitReg + 1);
    and_imm(0b00000011); // if neither of the last two bits of second LSFR set,
    if (zero_flag) { goto RSeed; } // skip this part and save contents of $00
    lda_absx(PseudoRandomBitReg + 2);
    and_imm(0b00001111); // otherwise overwrite with lower nybble of
    write_byte(0x0, a); // third LSFR part
  
RSeed:
    pla(); // get value from stack we saved earlier
    carry_flag = false;
    adc_zp(0x1); // add to last two bits of LSFR we saved in other place
    tay(); // use as pseudorandom offset here
    lda_absy(FlyCCXSpeedData); // get horizontal speed using pseudorandom offset
    write_byte(Enemy_X_Speed + x, a);
    lda_imm(0x1); // set to move towards the right
    write_byte(Enemy_MovingDir + x, a);
    lda_zp(Player_X_Speed); // if player moving left or right, branch ahead of this part
    if (!zero_flag) { goto D2XPos1; }
    ldy_zp(0x0); // get first LSFR or third LSFR lower nybble
    tya(); // and check for d1 set
    and_imm(0b00000010);
    if (zero_flag) { goto D2XPos1; } // if d1 not set, branch
    lda_zpx(Enemy_X_Speed);
    eor_imm(0xff); // if d1 set, change horizontal speed
    carry_flag = false; // into two's compliment, thus moving in the opposite
    adc_imm(0x1); // direction
    write_byte(Enemy_X_Speed + x, a);
    inc_zpx(Enemy_MovingDir); // increment to move towards the left
  
D2XPos1:
    tya(); // get first LSFR or third LSFR lower nybble again
    and_imm(0b00000010);
    if (zero_flag) { goto D2XPos2; } // check for d1 set again, branch again if not set
    lda_zp(Player_X_Position); // get player's horizontal position
    carry_flag = false;
    adc_absy(FlyCCXPositionData); // if d1 set, add value obtained from pseudorandom offset
    write_byte(Enemy_X_Position + x, a); // and save as enemy's horizontal position
    lda_zp(Player_PageLoc); // get player's page location
    adc_imm(0x0); // add carry and jump past this part
    goto FinCCSt;
  
D2XPos2:
    lda_zp(Player_X_Position); // get player's horizontal position
    carry_flag = true;
    sbc_absy(FlyCCXPositionData); // if d1 not set, subtract value obtained from pseudorandom
    write_byte(Enemy_X_Position + x, a); // offset and save as enemy's horizontal position
    lda_zp(Player_PageLoc); // get player's page location
    sbc_imm(0x0); // subtract borrow
  
FinCCSt:
    write_byte(Enemy_PageLoc + x, a); // save as enemy's page location
    lda_imm(0x1);
    write_byte(Enemy_Flag + x, a); // set enemy's buffer flag
    write_byte(Enemy_Y_HighPos + x, a); // set enemy's high vertical byte
    lda_imm(0xf8);
    write_byte(Enemy_Y_Position + x, a); // put enemy below the screen, and we are done
    goto rts;
    // --------------------------------
  
InitBowser:
    jsr(DuplicateEnemyObj, 201); // jump to create another bowser object
    write_byte(BowserFront_Offset, x); // save offset of first here
    lda_imm(0x0);
    write_byte(BowserBodyControls, a); // initialize bowser's body controls
    write_byte(BridgeCollapseOffset, a); // and bridge collapse offset
    lda_zpx(Enemy_X_Position);
    write_byte(BowserOrigXPos, a); // store original horizontal position here
    lda_imm(0xdf);
    write_byte(BowserFireBreathTimer, a); // store something here
    write_byte(Enemy_MovingDir + x, a); // and in moving direction
    lda_imm(0x20);
    write_byte(BowserFeetCounter, a); // set bowser's feet timer and in enemy timer
    write_byte(EnemyFrameTimer + x, a);
    lda_imm(0x5);
    write_byte(BowserHitPoints, a); // give bowser 5 hit points
    lsr_acc();
    write_byte(BowserMovementSpeed, a); // set default movement speed here
    goto rts;
    // --------------------------------
  
DuplicateEnemyObj:
    ldy_imm(0xff); // start at beginning of enemy slots
  
FSLoop:
    iny(); // increment one slot
    lda_zpy(Enemy_Flag); // check enemy buffer flag for empty slot
    if (!zero_flag) { goto FSLoop; } // if set, branch and keep checking
    write_byte(DuplicateObj_Offset, y); // otherwise set offset here
    txa(); // transfer original enemy buffer offset
    ora_imm(0b10000000); // store with d7 set as flag in new enemy
    write_byte(Enemy_Flag + y, a); // slot as well as enemy offset
    lda_zpx(Enemy_PageLoc);
    write_byte(Enemy_PageLoc + y, a); // copy page location and horizontal coordinates
    lda_zpx(Enemy_X_Position); // from original enemy to new enemy
    write_byte(Enemy_X_Position + y, a);
    lda_imm(0x1);
    write_byte(Enemy_Flag + x, a); // set flag as normal for original enemy
    write_byte(Enemy_Y_HighPos + y, a); // set high vertical byte for new enemy
    lda_zpx(Enemy_Y_Position);
    write_byte(Enemy_Y_Position + y, a); // copy vertical coordinate from original to new
  
FlmEx:
    goto rts; // and then leave
    // --------------------------------
  
InitBowserFlame:
    lda_abs(FrenzyEnemyTimer); // if timer not expired yet, branch to leave
    if (!zero_flag) { goto FlmEx; }
    write_byte(Enemy_Y_MoveForce + x, a); // reset something here
    lda_zp(NoiseSoundQueue);
    ora_imm(Sfx_BowserFlame); // load bowser's flame sound into queue
    write_byte(NoiseSoundQueue, a);
    ldy_abs(BowserFront_Offset); // get bowser's buffer offset
    lda_zpy(Enemy_ID); // check for bowser
    cmp_imm(Bowser);
    if (zero_flag) { goto SpawnFromMouth; } // branch if found
    jsr(SetFlameTimer, 202); // get timer data based on flame counter
    carry_flag = false;
    adc_imm(0x20); // add 32 frames by default
    ldy_abs(SecondaryHardMode);
    if (zero_flag) { goto SetFrT; } // if secondary mode flag not set, use as timer setting
    carry_flag = true;
    sbc_imm(0x10); // otherwise subtract 16 frames for secondary hard mode
  
SetFrT:
    write_byte(FrenzyEnemyTimer, a); // set timer accordingly
    lda_absx(PseudoRandomBitReg);
    and_imm(0b00000011); // get 2 LSB from first part of LSFR
    write_byte(BowserFlamePRandomOfs + x, a); // set here
    tay(); // use as offset
    lda_absy(FlameYPosData); // load vertical position based on pseudorandom offset
  
PutAtRightExtent:
    write_byte(Enemy_Y_Position + x, a); // set vertical position
    lda_abs(ScreenRight_X_Pos);
    carry_flag = false;
    adc_imm(0x20); // place enemy 32 pixels beyond right side of screen
    write_byte(Enemy_X_Position + x, a);
    lda_abs(ScreenRight_PageLoc);
    adc_imm(0x0); // add carry
    write_byte(Enemy_PageLoc + x, a);
    goto FinishFlame; // skip this part to finish setting values
  
SpawnFromMouth:
    lda_zpy(Enemy_X_Position); // get bowser's horizontal position
    carry_flag = true;
    sbc_imm(0xe); // subtract 14 pixels
    write_byte(Enemy_X_Position + x, a); // save as flame's horizontal position
    lda_zpy(Enemy_PageLoc);
    write_byte(Enemy_PageLoc + x, a); // copy page location from bowser to flame
    lda_zpy(Enemy_Y_Position);
    carry_flag = false; // add 8 pixels to bowser's vertical position
    adc_imm(0x8);
    write_byte(Enemy_Y_Position + x, a); // save as flame's vertical position
    lda_absx(PseudoRandomBitReg);
    and_imm(0b00000011); // get 2 LSB from first part of LSFR
    write_byte(Enemy_YMF_Dummy + x, a); // save here
    tay(); // use as offset
    lda_absy(FlameYPosData); // get value here using bits as offset
    ldy_imm(0x0); // load default offset
    cmp_zpx(Enemy_Y_Position); // compare value to flame's current vertical position
    if (!carry_flag) { goto SetMF; } // if less, do not increment offset
    iny(); // otherwise increment now
  
SetMF:
    lda_absy(FlameYMFAdderData); // get value here and save
    write_byte(Enemy_Y_MoveForce + x, a); // to vertical movement force
    lda_imm(0x0);
    write_byte(EnemyFrenzyBuffer, a); // clear enemy frenzy buffer
  
FinishFlame:
    lda_imm(0x8); // set $08 for bounding box control
    write_byte(Enemy_BoundBoxCtrl + x, a);
    lda_imm(0x1); // set high byte of vertical and
    write_byte(Enemy_Y_HighPos + x, a); // enemy buffer flag
    write_byte(Enemy_Flag + x, a);
    lsr_acc();
    write_byte(Enemy_X_MoveForce + x, a); // initialize horizontal movement force, and
    write_byte(Enemy_State + x, a); // enemy state
    goto rts;
    // --------------------------------
    // --------------------------------
  
BulletBillCheepCheep:
    lda_abs(FrenzyEnemyTimer); // if timer not expired yet, branch to leave
    if (!zero_flag) { goto ExF17; }
    lda_abs(AreaType); // are we in a water-type level?
    if (!zero_flag) { goto DoBulletBills; } // if not, branch elsewhere
    cpx_imm(0x3); // are we past third enemy slot?
    if (carry_flag) { goto ExF17; } // if so, branch to leave
    ldy_imm(0x0); // load default offset
    lda_absx(PseudoRandomBitReg);
    cmp_imm(0xaa); // check first part of LSFR against preset value
    if (!carry_flag) { goto ChkW2; } // if less than preset, do not increment offset
    iny(); // otherwise increment
  
ChkW2:
    lda_abs(WorldNumber); // check world number
    cmp_imm(World2);
    if (zero_flag) { goto Get17ID; } // if we're on world 2, do not increment offset
    iny(); // otherwise increment
  
Get17ID:
    tya();
    and_imm(0b00000001); // mask out all but last bit of offset
    tay();
    lda_absy(SwimCC_IDData); // load identifier for cheep-cheeps
  
Set17ID:
    write_byte(Enemy_ID + x, a); // store whatever's in A as enemy identifier
    lda_abs(BitMFilter);
    cmp_imm(0xff); // if not all bits set, skip init part and compare bits
    if (!zero_flag) { goto GetRBit; }
    lda_imm(0x0); // initialize vertical position filter
    write_byte(BitMFilter, a);
  
GetRBit:
    lda_absx(PseudoRandomBitReg); // get first part of LSFR
    and_imm(0b00000111); // mask out all but 3 LSB
  
ChkRBit:
    tay(); // use as offset
    lda_absy(Bitmasks); // load bitmask
    bit_abs(BitMFilter); // perform AND on filter without changing it
    if (zero_flag) { goto AddFBit; }
    iny(); // increment offset
    tya();
    and_imm(0b00000111); // mask out all but 3 LSB thus keeping it 0-7
    goto ChkRBit; // do another check
  
AddFBit:
    ora_abs(BitMFilter); // add bit to already set bits in filter
    write_byte(BitMFilter, a); // and store
    lda_absy(Enemy17YPosData); // load vertical position using offset
    jsr(PutAtRightExtent, 203); // set vertical position and other values
    write_byte(Enemy_YMF_Dummy + x, a); // initialize dummy variable
    lda_imm(0x20); // set timer
    write_byte(FrenzyEnemyTimer, a);
    goto CheckpointEnemyID; // process our new enemy object
  
DoBulletBills:
    ldy_imm(0xff); // start at beginning of enemy slots
  
BB_SLoop:
    iny(); // move onto the next slot
    cpy_imm(0x5); // branch to play sound if we've done all slots
    if (carry_flag) { goto FireBulletBill; }
    lda_zpy(Enemy_Flag); // if enemy buffer flag not set,
    if (zero_flag) { goto BB_SLoop; } // loop back and check another slot
    lda_zpy(Enemy_ID);
    cmp_imm(BulletBill_FrenzyVar); // check enemy identifier for
    if (!zero_flag) { goto BB_SLoop; } // bullet bill object (frenzy variant)
  
ExF17:
    goto rts; // if found, leave
  
FireBulletBill:
    lda_zp(Square2SoundQueue);
    ora_imm(Sfx_Blast); // play fireworks/gunfire sound
    write_byte(Square2SoundQueue, a);
    lda_imm(BulletBill_FrenzyVar); // load identifier for bullet bill object
    if (!zero_flag) { goto Set17ID; } // unconditional branch
    // --------------------------------
    // $00 - used to store Y position of group enemies
    // $01 - used to store enemy ID
    // $02 - used to store page location of right side of screen
    // $03 - used to store X position of right side of screen
  
HandleGroupEnemies:
    ldy_imm(0x0); // load value for green koopa troopa
    carry_flag = true;
    sbc_imm(0x37); // subtract $37 from second byte read
    pha(); // save result in stack for now
    cmp_imm(0x4); // was byte in $3b-$3e range?
    if (carry_flag) { goto SnglID; } // if so, branch
    pha(); // save another copy to stack
    ldy_imm(Goomba); // load value for goomba enemy
    lda_abs(PrimaryHardMode); // if primary hard mode flag not set,
    if (zero_flag) { goto PullID; } // branch, otherwise change to value
    ldy_imm(BuzzyBeetle); // for buzzy beetle
  
PullID:
    pla(); // get second copy from stack
  
SnglID:
    write_byte(0x1, y); // save enemy id here
    ldy_imm(0xb0); // load default y coordinate
    and_imm(0x2); // check to see if d1 was set
    if (zero_flag) { goto SetYGp; } // if so, move y coordinate up,
    ldy_imm(0x70); // otherwise branch and use default
  
SetYGp:
    write_byte(0x0, y); // save y coordinate here
    lda_abs(ScreenRight_PageLoc); // get page number of right edge of screen
    write_byte(0x2, a); // save here
    lda_abs(ScreenRight_X_Pos); // get pixel coordinate of right edge
    write_byte(0x3, a); // save here
    ldy_imm(0x2); // load two enemies by default
    pla(); // get first copy from stack
    lsr_acc(); // check to see if d0 was set
    if (!carry_flag) { goto CntGrp; } // if not, use default value
    iny(); // otherwise increment to three enemies
  
CntGrp:
    write_byte(NumberofGroupEnemies, y); // save number of enemies here
  
GrLoop:
    ldx_imm(0xff); // start at beginning of enemy buffers
  
GSltLp:
    inx(); // increment and branch if past
    cpx_imm(0x5); // end of buffers
    if (carry_flag) { goto NextED; }
    lda_zpx(Enemy_Flag); // check to see if enemy is already
    if (!zero_flag) { goto GSltLp; } // stored in buffer, and branch if so
    lda_zp(0x1);
    write_byte(Enemy_ID + x, a); // store enemy object identifier
    lda_zp(0x2);
    write_byte(Enemy_PageLoc + x, a); // store page location for enemy object
    lda_zp(0x3);
    write_byte(Enemy_X_Position + x, a); // store x coordinate for enemy object
    carry_flag = false;
    adc_imm(0x18); // add 24 pixels for next enemy
    write_byte(0x3, a);
    lda_zp(0x2); // add carry to page location for
    adc_imm(0x0); // next enemy
    write_byte(0x2, a);
    lda_zp(0x0); // store y coordinate for enemy object
    write_byte(Enemy_Y_Position + x, a);
    lda_imm(0x1); // activate flag for buffer, and
    write_byte(Enemy_Y_HighPos + x, a); // put enemy within the screen vertically
    write_byte(Enemy_Flag + x, a);
    jsr(CheckpointEnemyID, 204); // process each enemy object separately
    dec_abs(NumberofGroupEnemies); // do this until we run out of enemy objects
    if (!zero_flag) { goto GrLoop; }
  
NextED:
    goto Inc2B; // jump to increment data offset and leave
    // --------------------------------
  
InitPiranhaPlant:
    lda_imm(0x1); // set initial speed
    write_byte(PiranhaPlant_Y_Speed + x, a);
    lsr_acc();
    write_byte(Enemy_State + x, a); // initialize enemy state and what would normally
    write_byte(PiranhaPlant_MoveFlag + x, a); // be used as vertical speed, but not in this case
    lda_zpx(Enemy_Y_Position);
    write_byte(PiranhaPlantDownYPos + x, a); // save original vertical coordinate here
    carry_flag = true;
    sbc_imm(0x18);
    write_byte(PiranhaPlantUpYPos + x, a); // save original vertical coordinate - 24 pixels here
    lda_imm(0x9);
    goto SetBBox2; // set specific value for bounding box control
    // --------------------------------
  
InitEnemyFrenzy:
    lda_zpx(Enemy_ID); // load enemy identifier
    write_byte(EnemyFrenzyBuffer, a); // save in enemy frenzy buffer
    carry_flag = true;
    sbc_imm(0x12); // subtract 12 and use as offset for jump engine
    // jsr JumpEngine
    switch (a) {
      case 0: goto LakituAndSpinyHandler;
      case 1: NoFrenzyCode(); goto rts;
      case 2: goto InitFlyingCheepCheep;
      case 3: goto InitBowserFlame;
      case 4: InitFireworks(); goto rts;
      case 5: goto BulletBillCheepCheep;
    }
    // --------------------------------
  
InitJumpGPTroopa:
    lda_imm(0x2); // set for movement to the left
    write_byte(Enemy_MovingDir + x, a);
    lda_imm(0xf8); // set horizontal speed
    write_byte(Enemy_X_Speed + x, a);
  
TallBBox2:
    lda_imm(0x3); // set specific value for bounding box control
  
SetBBox2:
    write_byte(Enemy_BoundBoxCtrl + x, a); // set bounding box control then leave
    goto rts;
    // --------------------------------
  
InitDropPlatform:
    lda_imm(0xff);
    write_byte(PlatformCollisionFlag + x, a); // set some value here
    goto CommonPlatCode; // then jump ahead to execute more code
    // --------------------------------
  
InitHoriPlatform:
    lda_imm(0x0);
    write_byte(XMoveSecondaryCounter + x, a); // init one of the moving counters
    goto CommonPlatCode; // jump ahead to execute more code
    // --------------------------------
  
CommonPlatCode:
    InitVStf(); // do a sub to init certain other values 
  
SPBBox:
    lda_imm(0x5); // set default bounding box size control
    ldy_abs(AreaType);
    cpy_imm(0x3); // check for castle-type level
    if (zero_flag) { goto CasPBB; } // use default value if found
    ldy_abs(SecondaryHardMode); // otherwise check for secondary hard mode flag
    if (!zero_flag) { goto CasPBB; } // if set, use default value
    lda_imm(0x6); // use alternate value if not castle or secondary not set
  
CasPBB:
    write_byte(Enemy_BoundBoxCtrl + x, a); // set bounding box size control here and leave
    goto rts;
    // --------------------------------
  
LargeLiftUp:
    jsr(PlatLiftUp, 205); // execute code for platforms going up
    goto LargeLiftBBox; // overwrite bounding box for large platforms
  
LargeLiftDown:
    jsr(PlatLiftDown, 206); // execute code for platforms going down
  
LargeLiftBBox:
    goto SPBBox; // jump to overwrite bounding box size control
    // --------------------------------
  
PlatLiftUp:
    lda_imm(0x10); // set movement amount here
    write_byte(Enemy_Y_MoveForce + x, a);
    lda_imm(0xff); // set moving speed for platforms going up
    write_byte(Enemy_Y_Speed + x, a);
    goto CommonSmallLift; // skip ahead to part we should be executing
    // --------------------------------
  
PlatLiftDown:
    lda_imm(0xf0); // set movement amount here
    write_byte(Enemy_Y_MoveForce + x, a);
    lda_imm(0x0); // set moving speed for platforms going down
    write_byte(Enemy_Y_Speed + x, a);
    // --------------------------------
  
CommonSmallLift:
    ldy_imm(0x1);
    PosPlatform(); // do a sub to add 12 pixels due to preset value  
    lda_imm(0x4);
    write_byte(Enemy_BoundBoxCtrl + x, a); // set bounding box control for small platforms
    goto rts;
    // --------------------------------
  
RunEnemyObjectsCore:
    ldx_zp(ObjectOffset); // get offset for enemy object buffer
    lda_imm(0x0); // load value 0 for jump engine by default
    ldy_zpx(Enemy_ID);
    cpy_imm(0x15); // if enemy object < $15, use default value
    if (!carry_flag) { goto JmpEO; }
    tya(); // otherwise subtract $14 from the value and use
    sbc_imm(0x14); // as value for jump engine
  
JmpEO:
    // jsr JumpEngine
    switch (a) {
      case 0: goto RunNormalEnemies;
      case 1: goto RunBowserFlame;
      case 2: goto RunFireworks;
      case 3: NoRunCode(); goto rts;
      case 4: NoRunCode(); goto rts;
      case 5: NoRunCode(); goto rts;
      case 6: NoRunCode(); goto rts;
      case 7: goto RunFirebarObj;
      case 8: goto RunFirebarObj;
      case 9: goto RunFirebarObj;
      case 10: goto RunFirebarObj;
      case 11: goto RunFirebarObj;
      case 12: goto RunFirebarObj;
      case 13: goto RunFirebarObj;
      case 14: goto RunFirebarObj;
      case 15: NoRunCode(); goto rts;
      case 16: goto RunLargePlatform;
      case 17: goto RunLargePlatform;
      case 18: goto RunLargePlatform;
      case 19: goto RunLargePlatform;
      case 20: goto RunLargePlatform;
      case 21: goto RunLargePlatform;
      case 22: goto RunLargePlatform;
      case 23: goto RunSmallPlatform;
      case 24: goto RunSmallPlatform;
      case 25: goto RunBowser;
      case 26: goto PowerUpObjHandler;
      case 27: goto VineObjectHandler;
      case 28: NoRunCode(); goto rts;
      case 29: goto RunStarFlagObj;
      case 30: goto JumpspringHandler;
      case 31: NoRunCode(); goto rts;
      case 32: goto WarpZoneObject;
      case 33: goto RunRetainerObj;
    }
  
RunRetainerObj:
    jsr(GetEnemyOffscreenBits, 207);
    jsr(RelativeEnemyPosition, 208);
    goto EnemyGfxHandler;
    // --------------------------------
  
RunNormalEnemies:
    lda_imm(0x0); // init sprite attributes
    write_byte(Enemy_SprAttrib + x, a);
    jsr(GetEnemyOffscreenBits, 209);
    jsr(RelativeEnemyPosition, 210);
    jsr(EnemyGfxHandler, 211);
    jsr(GetEnemyBoundBox, 212);
    jsr(EnemyToBGCollisionDet, 213);
    jsr(EnemiesCollision, 214);
    jsr(PlayerEnemyCollision, 215);
    ldy_abs(TimerControl); // if master timer control set, skip to last routine
    if (!zero_flag) { goto SkipMove; }
    jsr(EnemyMovementSubs, 216);
  
SkipMove:
    OffscreenBoundsCheck(); goto rts;
  
EnemyMovementSubs:
    lda_zpx(Enemy_ID);
    // jsr JumpEngine
    switch (a) {
      case 0: goto MoveNormalEnemy;
      case 1: goto MoveNormalEnemy;
      case 2: goto MoveNormalEnemy;
      case 3: goto MoveNormalEnemy;
      case 4: goto MoveNormalEnemy;
      case 5: goto ProcHammerBro;
      case 6: goto MoveNormalEnemy;
      case 7: goto MoveBloober;
      case 8: goto MoveBulletBill;
      case 9: NoMoveCode(); goto rts;
      case 10: goto MoveSwimmingCheepCheep;
      case 11: goto MoveSwimmingCheepCheep;
      case 12: goto MovePodoboo;
      case 13: MovePiranhaPlant(); goto rts;
      case 14: goto MoveJumpingEnemy;
      case 15: goto ProcMoveRedPTroopa;
      case 16: goto MoveFlyGreenPTroopa;
      case 17: goto MoveLakitu;
      case 18: goto MoveNormalEnemy;
      case 19: NoMoveCode(); goto rts;
      case 20: goto MoveFlyingCheepCheep;
    }
  
RunBowserFlame:
    jsr(ProcBowserFlame, 217);
    jsr(GetEnemyOffscreenBits, 218);
    jsr(RelativeEnemyPosition, 219);
    jsr(GetEnemyBoundBox, 220);
    jsr(PlayerEnemyCollision, 221);
    OffscreenBoundsCheck(); goto rts;
    // --------------------------------
  
RunFirebarObj:
    jsr(ProcFirebar, 222);
    OffscreenBoundsCheck(); goto rts;
    // --------------------------------
  
RunSmallPlatform:
    jsr(GetEnemyOffscreenBits, 223);
    jsr(RelativeEnemyPosition, 224);
    jsr(SmallPlatformBoundBox, 225);
    jsr(SmallPlatformCollision, 226);
    jsr(RelativeEnemyPosition, 227);
    jsr(DrawSmallPlatform, 228);
    jsr(MoveSmallPlatform, 229);
    OffscreenBoundsCheck(); goto rts;
    // --------------------------------
  
RunLargePlatform:
    jsr(GetEnemyOffscreenBits, 230);
    jsr(RelativeEnemyPosition, 231);
    jsr(LargePlatformBoundBox, 232);
    jsr(LargePlatformCollision, 233);
    lda_abs(TimerControl); // if master timer control set,
    if (!zero_flag) { goto SkipPT; } // skip subroutine tree
    jsr(LargePlatformSubroutines, 234);
  
SkipPT:
    jsr(RelativeEnemyPosition, 235);
    jsr(DrawLargePlatform, 236);
    OffscreenBoundsCheck(); goto rts;
    // --------------------------------
  
LargePlatformSubroutines:
    lda_zpx(Enemy_ID); // subtract $24 to get proper offset for jump table
    carry_flag = true;
    sbc_imm(0x24);
    // jsr JumpEngine
    switch (a) {
      case 0: goto BalancePlatform;
      case 1: goto YMovingPlatform;
      case 2: goto MoveLargeLiftPlat;
      case 3: goto MoveLargeLiftPlat;
      case 4: goto XMovingPlatform;
      case 5: goto DropPlatform;
      case 6: goto RightPlatform;
    }
  
MovePodoboo:
    lda_absx(EnemyIntervalTimer); // check enemy timer
    if (!zero_flag) { goto PdbM; } // branch to move enemy if not expired
    jsr(InitPodoboo, 237); // otherwise set up podoboo again
    lda_absx(PseudoRandomBitReg + 1); // get part of LSFR
    ora_imm(0b10000000); // set d7
    write_byte(Enemy_Y_MoveForce + x, a); // store as movement force
    and_imm(0b00001111); // mask out high nybble
    ora_imm(0x6); // set for at least six intervals
    write_byte(EnemyIntervalTimer + x, a); // store as new enemy timer
    lda_imm(0xf9);
    write_byte(Enemy_Y_Speed + x, a); // set vertical speed to move podoboo upwards
  
PdbM:
    goto MoveJ_EnemyVertically; // branch to impose gravity on podoboo
    // --------------------------------
    // $00 - used in HammerBroJumpCode as bitmask
  
ProcHammerBro:
    lda_zpx(Enemy_State); // check hammer bro's enemy state for d5 set
    and_imm(0b00100000);
    if (zero_flag) { goto ChkJH; } // if not set, go ahead with code
    goto MoveDefeatedEnemy; // otherwise jump to something else
  
ChkJH:
    lda_zpx(HammerBroJumpTimer); // check jump timer
    if (zero_flag) { goto HammerBroJumpCode; } // if expired, branch to jump
    dec_zpx(HammerBroJumpTimer); // otherwise decrement jump timer
    lda_abs(Enemy_OffscreenBits);
    and_imm(0b00001100); // check offscreen bits
    if (!zero_flag) { goto MoveHammerBroXDir; } // if hammer bro a little offscreen, skip to movement code
    lda_absx(HammerThrowingTimer); // check hammer throwing timer
    if (!zero_flag) { goto DecHT; } // if not expired, skip ahead, do not throw hammer
    ldy_abs(SecondaryHardMode); // otherwise get secondary hard mode flag
    lda_absy(HammerThrowTmrData); // get timer data using flag as offset
    write_byte(HammerThrowingTimer + x, a); // set as new timer
    jsr(SpawnHammerObj, 238); // do a sub here to spawn hammer object
    if (!carry_flag) { goto DecHT; } // if carry clear, hammer not spawned, skip to decrement timer
    lda_zpx(Enemy_State);
    ora_imm(0b00001000); // set d3 in enemy state for hammer throw
    write_byte(Enemy_State + x, a);
    goto MoveHammerBroXDir; // jump to move hammer bro
  
DecHT:
    dec_absx(HammerThrowingTimer); // decrement timer
    goto MoveHammerBroXDir; // jump to move hammer bro
  
HammerBroJumpCode:
    lda_zpx(Enemy_State); // get hammer bro's enemy state
    and_imm(0b00000111); // mask out all but 3 LSB
    cmp_imm(0x1); // check for d0 set (for jumping)
    if (zero_flag) { goto MoveHammerBroXDir; } // if set, branch ahead to moving code
    lda_imm(0x0); // load default value here
    write_byte(0x0, a); // save into temp variable for now
    ldy_imm(0xfa); // set default vertical speed
    lda_zpx(Enemy_Y_Position); // check hammer bro's vertical coordinate
    if (neg_flag) { goto SetHJ; } // if on the bottom half of the screen, use current speed
    ldy_imm(0xfd); // otherwise set alternate vertical speed
    cmp_imm(0x70); // check to see if hammer bro is above the middle of screen
    inc_zp(0x0); // increment preset value to $01
    if (!carry_flag) { goto SetHJ; } // if above the middle of the screen, use current speed and $01
    dec_zp(0x0); // otherwise return value to $00
    lda_absx(PseudoRandomBitReg + 1); // get part of LSFR, mask out all but LSB
    and_imm(0x1);
    if (!zero_flag) { goto SetHJ; } // if d0 of LSFR set, branch and use current speed and $00
    ldy_imm(0xfa); // otherwise reset to default vertical speed
  
SetHJ:
    write_byte(Enemy_Y_Speed + x, y); // set vertical speed for jumping
    lda_zpx(Enemy_State); // set d0 in enemy state for jumping
    ora_imm(0x1);
    write_byte(Enemy_State + x, a);
    lda_zp(0x0); // load preset value here to use as bitmask
    and_absx(PseudoRandomBitReg + 2); // and do bit-wise comparison with part of LSFR
    tay(); // then use as offset
    lda_abs(SecondaryHardMode); // check secondary hard mode flag
    if (!zero_flag) { goto HJump; }
    tay(); // if secondary hard mode flag clear, set offset to 0
  
HJump:
    lda_absy(HammerBroJumpLData); // get jump length timer data using offset from before
    write_byte(EnemyFrameTimer + x, a); // save in enemy timer
    lda_absx(PseudoRandomBitReg + 1);
    ora_imm(0b11000000); // get contents of part of LSFR, set d7 and d6, then
    write_byte(HammerBroJumpTimer + x, a); // store in jump timer
  
MoveHammerBroXDir:
    ldy_imm(0xfc); // move hammer bro a little to the left
    lda_zp(FrameCounter);
    and_imm(0b01000000); // change hammer bro's direction every 64 frames
    if (!zero_flag) { goto Shimmy; }
    ldy_imm(0x4); // if d6 set in counter, move him a little to the right
  
Shimmy:
    write_byte(Enemy_X_Speed + x, y); // store horizontal speed
    ldy_imm(0x1); // set to face right by default
    PlayerEnemyDiff(); // get horizontal difference between player and hammer bro
    if (neg_flag) { goto SetShim; } // if enemy to the left of player, skip this part
    iny(); // set to face left
    lda_absx(EnemyIntervalTimer); // check walking timer
    if (!zero_flag) { goto SetShim; } // if not yet expired, skip to set moving direction
    lda_imm(0xf8);
    write_byte(Enemy_X_Speed + x, a); // otherwise, make the hammer bro walk left towards player
  
SetShim:
    write_byte(Enemy_MovingDir + x, y); // set moving direction
  
MoveNormalEnemy:
    ldy_imm(0x0); // init Y to leave horizontal movement as-is 
    lda_zpx(Enemy_State);
    and_imm(0b01000000); // check enemy state for d6 set, if set skip
    if (!zero_flag) { goto FallE; } // to move enemy vertically, then horizontally if necessary
    lda_zpx(Enemy_State);
    asl_acc(); // check enemy state for d7 set
    if (carry_flag) { goto SteadM; } // if set, branch to move enemy horizontally
    lda_zpx(Enemy_State);
    and_imm(0b00100000); // check enemy state for d5 set
    if (!zero_flag) { goto MoveDefeatedEnemy; } // if set, branch to move defeated enemy object
    lda_zpx(Enemy_State);
    and_imm(0b00000111); // check d2-d0 of enemy state for any set bits
    if (zero_flag) { goto SteadM; } // if enemy in normal state, branch to move enemy horizontally
    cmp_imm(0x5);
    if (zero_flag) { goto FallE; } // if enemy in state used by spiny's egg, go ahead here
    cmp_imm(0x3);
    if (carry_flag) { goto ReviveStunned; } // if enemy in states $03 or $04, skip ahead to yet another part
  
FallE:
    jsr(MoveD_EnemyVertically, 239); // do a sub here to move enemy downwards
    ldy_imm(0x0);
    lda_zpx(Enemy_State); // check for enemy state $02
    cmp_imm(0x2);
    if (zero_flag) { goto MEHor; } // if found, branch to move enemy horizontally
    and_imm(0b01000000); // check for d6 set
    if (zero_flag) { goto SteadM; } // if not set, branch to something else
    lda_zpx(Enemy_ID);
    cmp_imm(PowerUpObject); // check for power-up object
    if (zero_flag) { goto SteadM; }
    if (!zero_flag) { goto SlowM; } // if any other object where d6 set, jump to set Y
  
MEHor:
    goto MoveEnemyHorizontally; // jump here to move enemy horizontally for <> $2e and d6 set
  
SlowM:
    ldy_imm(0x1); // if branched here, increment Y to slow horizontal movement
  
SteadM:
    lda_zpx(Enemy_X_Speed); // get current horizontal speed
    pha(); // save to stack
    if (!neg_flag) { goto AddHS; } // if not moving or moving right, skip, leave Y alone
    iny();
    iny(); // otherwise increment Y to next data
  
AddHS:
    carry_flag = false;
    adc_absy(XSpeedAdderData); // add value here to slow enemy down if necessary
    write_byte(Enemy_X_Speed + x, a); // save as horizontal speed temporarily
    jsr(MoveEnemyHorizontally, 240); // then do a sub to move horizontally
    pla();
    write_byte(Enemy_X_Speed + x, a); // get old horizontal speed from stack and return to
    goto rts; // original memory location, then leave
  
ReviveStunned:
    lda_absx(EnemyIntervalTimer); // if enemy timer not expired yet,
    if (!zero_flag) { goto ChkKillGoomba; } // skip ahead to something else
    write_byte(Enemy_State + x, a); // otherwise initialize enemy state to normal
    lda_zp(FrameCounter);
    and_imm(0x1); // get d0 of frame counter
    tay(); // use as Y and increment for movement direction
    iny();
    write_byte(Enemy_MovingDir + x, y); // store as pseudorandom movement direction
    dey(); // decrement for use as pointer
    lda_abs(PrimaryHardMode); // check primary hard mode flag
    if (zero_flag) { goto SetRSpd; } // if not set, use pointer as-is
    iny();
    iny(); // otherwise increment 2 bytes to next data
  
SetRSpd:
    lda_absy(RevivedXSpeed); // load and store new horizontal speed
    write_byte(Enemy_X_Speed + x, a); // and leave
    goto rts;
  
MoveDefeatedEnemy:
    jsr(MoveD_EnemyVertically, 241); // execute sub to move defeated enemy downwards
    goto MoveEnemyHorizontally; // now move defeated enemy horizontally
  
ChkKillGoomba:
    cmp_imm(0xe); // check to see if enemy timer has reached
    if (!zero_flag) { goto NKGmba; } // a certain point, and branch to leave if not
    lda_zpx(Enemy_ID);
    cmp_imm(Goomba); // check for goomba object
    if (!zero_flag) { goto NKGmba; } // branch if not found
    EraseEnemyObject(); // otherwise, kill this goomba object
  
NKGmba:
    goto rts; // leave!
    // --------------------------------
  
MoveJumpingEnemy:
    jsr(MoveJ_EnemyVertically, 242); // do a sub to impose gravity on green paratroopa
    goto MoveEnemyHorizontally; // jump to move enemy horizontally
    // --------------------------------
  
ProcMoveRedPTroopa:
    lda_zpx(Enemy_Y_Speed);
    ora_absx(Enemy_Y_MoveForce); // check for any vertical force or speed
    if (!zero_flag) { goto MoveRedPTUpOrDown; } // branch if any found
    write_byte(Enemy_YMF_Dummy + x, a); // initialize something here
    lda_zpx(Enemy_Y_Position); // check current vs. original vertical coordinate
    cmp_absx(RedPTroopaOrigXPos);
    if (carry_flag) { goto MoveRedPTUpOrDown; } // if current => original, skip ahead to more code
    lda_zp(FrameCounter); // get frame counter
    and_imm(0b00000111); // mask out all but 3 LSB
    if (!zero_flag) { goto NoIncPT; } // if any bits set, branch to leave
    inc_zpx(Enemy_Y_Position); // otherwise increment red paratroopa's vertical position
  
NoIncPT:
    goto rts; // leave
  
MoveRedPTUpOrDown:
    lda_zpx(Enemy_Y_Position); // check current vs. central vertical coordinate
    cmp_zpx(RedPTroopaCenterYPos);
    if (!carry_flag) { goto MovPTDwn; } // if current < central, jump to move downwards
    goto MoveRedPTroopaUp; // otherwise jump to move upwards
  
MovPTDwn:
    goto MoveRedPTroopaDown; // move downwards
    // --------------------------------
    // $00 - used to store adder for movement, also used as adder for platform
    // $01 - used to store maximum value for secondary counter
  
MoveFlyGreenPTroopa:
    jsr(XMoveCntr_GreenPTroopa, 243); // do sub to increment primary and secondary counters
    jsr(MoveWithXMCntrs, 244); // do sub to move green paratroopa accordingly, and horizontally
    ldy_imm(0x1); // set Y to move green paratroopa down
    lda_zp(FrameCounter);
    and_imm(0b00000011); // check frame counter 2 LSB for any bits set
    if (!zero_flag) { goto NoMGPT; } // branch to leave if set to move up/down every fourth frame
    lda_zp(FrameCounter);
    and_imm(0b01000000); // check frame counter for d6 set
    if (!zero_flag) { goto YSway; } // branch to move green paratroopa down if set
    ldy_imm(0xff); // otherwise set Y to move green paratroopa up
  
YSway:
    write_byte(0x0, y); // store adder here
    lda_zpx(Enemy_Y_Position);
    carry_flag = false; // add or subtract from vertical position
    adc_zp(0x0); // to give green paratroopa a wavy flight
    write_byte(Enemy_Y_Position + x, a);
  
NoMGPT:
    goto rts; // leave!
  
XMoveCntr_GreenPTroopa:
    lda_imm(0x13); // load preset maximum value for secondary counter
  
XMoveCntr_Platform:
    write_byte(0x1, a); // store value here
    lda_zp(FrameCounter);
    and_imm(0b00000011); // branch to leave if not on
    if (!zero_flag) { goto NoIncXM; } // every fourth frame
    ldy_zpx(XMoveSecondaryCounter); // get secondary counter
    lda_zpx(XMovePrimaryCounter); // get primary counter
    lsr_acc();
    if (carry_flag) { goto DecSeXM; } // if d0 of primary counter set, branch elsewhere
    cpy_zp(0x1); // compare secondary counter to preset maximum value
    if (zero_flag) { goto IncPXM; } // if equal, branch ahead of this part
    inc_zpx(XMoveSecondaryCounter); // increment secondary counter and leave
  
NoIncXM:
    goto rts;
  
IncPXM:
    inc_zpx(XMovePrimaryCounter); // increment primary counter and leave
    goto rts;
  
DecSeXM:
    tya(); // put secondary counter in A
    if (zero_flag) { goto IncPXM; } // if secondary counter at zero, branch back
    dec_zpx(XMoveSecondaryCounter); // otherwise decrement secondary counter and leave
    goto rts;
  
MoveWithXMCntrs:
    lda_zpx(XMoveSecondaryCounter); // save secondary counter to stack
    pha();
    ldy_imm(0x1); // set value here by default
    lda_zpx(XMovePrimaryCounter);
    and_imm(0b00000010); // if d1 of primary counter is
    if (!zero_flag) { goto XMRight; } // set, branch ahead of this part here
    lda_zpx(XMoveSecondaryCounter);
    eor_imm(0xff); // otherwise change secondary
    carry_flag = false; // counter to two's compliment
    adc_imm(0x1);
    write_byte(XMoveSecondaryCounter + x, a);
    ldy_imm(0x2); // load alternate value here
  
XMRight:
    write_byte(Enemy_MovingDir + x, y); // store as moving direction
    jsr(MoveEnemyHorizontally, 245);
    write_byte(0x0, a); // save value obtained from sub here
    pla(); // get secondary counter from stack
    write_byte(XMoveSecondaryCounter + x, a); // and return to original place
    goto rts;
    // --------------------------------
  
MoveBloober:
    lda_zpx(Enemy_State);
    and_imm(0b00100000); // check enemy state for d5 set
    if (!zero_flag) { goto MoveDefeatedBloober; } // branch if set to move defeated bloober
    ldy_abs(SecondaryHardMode); // use secondary hard mode flag as offset
    lda_absx(PseudoRandomBitReg + 1); // get LSFR
    and_absy(BlooberBitmasks); // mask out bits in LSFR using bitmask loaded with offset
    if (!zero_flag) { goto BlooberSwim; } // if any bits set, skip ahead to make swim
    txa();
    lsr_acc(); // check to see if on second or fourth slot (1 or 3)
    if (!carry_flag) { goto FBLeft; } // if not, branch to figure out moving direction
    ldy_zp(Player_MovingDir); // otherwise, load player's moving direction and
    if (carry_flag) { goto SBMDir; } // do an unconditional branch to set
  
FBLeft:
    ldy_imm(0x2); // set left moving direction by default
    PlayerEnemyDiff(); // get horizontal difference between player and bloober
    if (!neg_flag) { goto SBMDir; } // if enemy to the right of player, keep left
    dey(); // otherwise decrement to set right moving direction
  
SBMDir:
    write_byte(Enemy_MovingDir + x, y); // set moving direction of bloober, then continue on here
  
BlooberSwim:
    jsr(ProcSwimmingB, 246); // execute sub to make bloober swim characteristically
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    carry_flag = true;
    sbc_absx(Enemy_Y_MoveForce); // subtract movement force
    cmp_imm(0x20); // check to see if position is above edge of status bar
    if (!carry_flag) { goto SwimX; } // if so, don't do it
    write_byte(Enemy_Y_Position + x, a); // otherwise, set new vertical position, make bloober swim
  
SwimX:
    ldy_zpx(Enemy_MovingDir); // check moving direction
    dey();
    if (!zero_flag) { goto LeftSwim; } // if moving to the left, branch to second part
    lda_zpx(Enemy_X_Position);
    carry_flag = false; // add movement speed to horizontal coordinate
    adc_zpx(BlooperMoveSpeed);
    write_byte(Enemy_X_Position + x, a); // store result as new horizontal coordinate
    lda_zpx(Enemy_PageLoc);
    adc_imm(0x0); // add carry to page location
    write_byte(Enemy_PageLoc + x, a); // store as new page location and leave
    goto rts;
  
LeftSwim:
    lda_zpx(Enemy_X_Position);
    carry_flag = true; // subtract movement speed from horizontal coordinate
    sbc_zpx(BlooperMoveSpeed);
    write_byte(Enemy_X_Position + x, a); // store result as new horizontal coordinate
    lda_zpx(Enemy_PageLoc);
    sbc_imm(0x0); // subtract borrow from page location
    write_byte(Enemy_PageLoc + x, a); // store as new page location and leave
    goto rts;
  
MoveDefeatedBloober:
    goto MoveEnemySlowVert; // jump to move defeated bloober downwards
  
ProcSwimmingB:
    lda_zpx(BlooperMoveCounter); // get enemy's movement counter
    and_imm(0b00000010); // check for d1 set
    if (!zero_flag) { goto ChkForFloatdown; } // branch if set
    lda_zp(FrameCounter);
    and_imm(0b00000111); // get 3 LSB of frame counter
    pha(); // and save it to the stack
    lda_zpx(BlooperMoveCounter); // get enemy's movement counter
    lsr_acc(); // check for d0 set
    if (carry_flag) { goto SlowSwim; } // branch if set
    pla(); // pull 3 LSB of frame counter from the stack
    if (!zero_flag) { goto BSwimE; } // branch to leave, execute code only every eighth frame
    lda_absx(Enemy_Y_MoveForce);
    carry_flag = false; // add to movement force to speed up swim
    adc_imm(0x1);
    write_byte(Enemy_Y_MoveForce + x, a); // set movement force
    write_byte(BlooperMoveSpeed + x, a); // set as movement speed
    cmp_imm(0x2);
    if (!zero_flag) { goto BSwimE; } // if certain horizontal speed, branch to leave
    inc_zpx(BlooperMoveCounter); // otherwise increment movement counter
  
BSwimE:
    goto rts;
  
SlowSwim:
    pla(); // pull 3 LSB of frame counter from the stack
    if (!zero_flag) { goto NoSSw; } // branch to leave, execute code only every eighth frame
    lda_absx(Enemy_Y_MoveForce);
    carry_flag = true; // subtract from movement force to slow swim
    sbc_imm(0x1);
    write_byte(Enemy_Y_MoveForce + x, a); // set movement force
    write_byte(BlooperMoveSpeed + x, a); // set as movement speed
    if (!zero_flag) { goto NoSSw; } // if any speed, branch to leave
    inc_zpx(BlooperMoveCounter); // otherwise increment movement counter
    lda_imm(0x2);
    write_byte(EnemyIntervalTimer + x, a); // set enemy's timer
  
NoSSw:
    goto rts; // leave
  
ChkForFloatdown:
    lda_absx(EnemyIntervalTimer); // get enemy timer
    if (zero_flag) { goto ChkNearPlayer; } // branch if expired
  
Floatdown:
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // check for d0 set
    if (carry_flag) { goto NoFD; } // branch to leave on every other frame
    inc_zpx(Enemy_Y_Position); // otherwise increment vertical coordinate
  
NoFD:
    goto rts; // leave
  
ChkNearPlayer:
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    adc_imm(0x10); // add sixteen pixels
    cmp_zp(Player_Y_Position); // compare result with player's vertical coordinate
    if (!carry_flag) { goto Floatdown; } // if modified vertical less than player's, branch
    lda_imm(0x0);
    write_byte(BlooperMoveCounter + x, a); // otherwise nullify movement counter
    goto rts;
    // --------------------------------
  
MoveBulletBill:
    lda_zpx(Enemy_State); // check bullet bill's enemy object state for d5 set
    and_imm(0b00100000);
    if (zero_flag) { goto NotDefB; } // if not set, continue with movement code
    goto MoveJ_EnemyVertically; // otherwise jump to move defeated bullet bill downwards
  
NotDefB:
    lda_imm(0xe8); // set bullet bill's horizontal speed
    write_byte(Enemy_X_Speed + x, a); // and move it accordingly (note: this bullet bill
    goto MoveEnemyHorizontally; // object occurs in frenzy object $17, not from cannons)
    // --------------------------------
    // $02 - used to hold preset values
    // $03 - used to hold enemy state
  
MoveSwimmingCheepCheep:
    lda_zpx(Enemy_State); // check cheep-cheep's enemy object state
    and_imm(0b00100000); // for d5 set
    if (zero_flag) { goto CCSwim; } // if not set, continue with movement code
    goto MoveEnemySlowVert; // otherwise jump to move defeated cheep-cheep downwards
  
CCSwim:
    write_byte(0x3, a); // save enemy state in $03
    lda_zpx(Enemy_ID); // get enemy identifier
    carry_flag = true;
    sbc_imm(0xa); // subtract ten for cheep-cheep identifiers
    tay(); // use as offset
    lda_absy(SwimCCXMoveData); // load value here
    write_byte(0x2, a);
    lda_absx(Enemy_X_MoveForce); // load horizontal force
    carry_flag = true;
    sbc_zp(0x2); // subtract preset value from horizontal force
    write_byte(Enemy_X_MoveForce + x, a); // store as new horizontal force
    lda_zpx(Enemy_X_Position); // get horizontal coordinate
    sbc_imm(0x0); // subtract borrow (thus moving it slowly)
    write_byte(Enemy_X_Position + x, a); // and save as new horizontal coordinate
    lda_zpx(Enemy_PageLoc);
    sbc_imm(0x0); // subtract borrow again, this time from the
    write_byte(Enemy_PageLoc + x, a); // page location, then save
    lda_imm(0x20);
    write_byte(0x2, a); // save new value here
    cpx_imm(0x2); // check enemy object offset
    if (!carry_flag) { goto ExSwCC; } // if in first or second slot, branch to leave
    lda_zpx(CheepCheepMoveMFlag); // check movement flag
    cmp_imm(0x10); // if movement speed set to $00,
    if (!carry_flag) { goto CCSwimUpwards; } // branch to move upwards
    lda_absx(Enemy_YMF_Dummy);
    carry_flag = false;
    adc_zp(0x2); // add preset value to dummy variable to get carry
    write_byte(Enemy_YMF_Dummy + x, a); // and save dummy
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    adc_zp(0x3); // add carry to it plus enemy state to slowly move it downwards
    write_byte(Enemy_Y_Position + x, a); // save as new vertical coordinate
    lda_zpx(Enemy_Y_HighPos);
    adc_imm(0x0); // add carry to page location and
    goto ChkSwimYPos; // jump to end of movement code
  
CCSwimUpwards:
    lda_absx(Enemy_YMF_Dummy);
    carry_flag = true;
    sbc_zp(0x2); // subtract preset value to dummy variable to get borrow
    write_byte(Enemy_YMF_Dummy + x, a); // and save dummy
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    sbc_zp(0x3); // subtract borrow to it plus enemy state to slowly move it upwards
    write_byte(Enemy_Y_Position + x, a); // save as new vertical coordinate
    lda_zpx(Enemy_Y_HighPos);
    sbc_imm(0x0); // subtract borrow from page location
  
ChkSwimYPos:
    write_byte(Enemy_Y_HighPos + x, a); // save new page location here
    ldy_imm(0x0); // load movement speed to upwards by default
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    carry_flag = true;
    sbc_absx(CheepCheepOrigYPos); // subtract original coordinate from current
    if (!neg_flag) { goto YPDiff; } // if result positive, skip to next part
    ldy_imm(0x10); // otherwise load movement speed to downwards
    eor_imm(0xff);
    carry_flag = false; // get two's compliment of result
    adc_imm(0x1); // to obtain total difference of original vs. current
  
YPDiff:
    cmp_imm(0xf); // if difference between original vs. current vertical
    if (!carry_flag) { goto ExSwCC; } // coordinates < 15 pixels, leave movement speed alone
    tya();
    write_byte(CheepCheepMoveMFlag + x, a); // otherwise change movement speed
  
ExSwCC:
    goto rts; // leave
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
  
ProcFirebar:
    jsr(GetEnemyOffscreenBits, 247); // get offscreen information
    lda_abs(Enemy_OffscreenBits); // check for d3 set
    and_imm(0b00001000); // if so, branch to leave
    if (!zero_flag) { goto SkipFBar; }
    lda_abs(TimerControl); // if master timer control set, branch
    if (!zero_flag) { goto SusFbar; } // ahead of this part
    lda_absx(FirebarSpinSpeed); // load spinning speed of firebar
    jsr(FirebarSpin, 248); // modify current spinstate
    and_imm(0b00011111); // mask out all but 5 LSB
    write_byte(FirebarSpinState_High + x, a); // and store as new high byte of spinstate
  
SusFbar:
    lda_zpx(FirebarSpinState_High); // get high byte of spinstate
    ldy_zpx(Enemy_ID); // check enemy identifier
    cpy_imm(0x1f);
    if (!carry_flag) { goto SetupGFB; } // if < $1f (long firebar), branch
    cmp_imm(0x8); // check high byte of spinstate
    if (zero_flag) { goto SkpFSte; } // if eight, branch to change
    cmp_imm(0x18);
    if (!zero_flag) { goto SetupGFB; } // if not at twenty-four branch to not change
  
SkpFSte:
    carry_flag = false;
    adc_imm(0x1); // add one to spinning thing to avoid horizontal state
    write_byte(FirebarSpinState_High + x, a);
  
SetupGFB:
    write_byte(0xef, a); // save high byte of spinning thing, modified or otherwise
    jsr(RelativeEnemyPosition, 249); // get relative coordinates to screen
    GetFirebarPosition(); // do a sub here (residual, too early to be used now)
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
    write_byte(Sprite_Y_Position + y, a); // store as Y in OAM data
    write_byte(0x7, a); // also save here
    lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a); // store as X in OAM data
    write_byte(0x6, a); // also save here
    lda_imm(0x1);
    write_byte(0x0, a); // set $01 value here (not necessary)
    jsr(FirebarCollision, 250); // draw fireball part and do collision detection
    ldy_imm(0x5); // load value for short firebars by default
    lda_zpx(Enemy_ID);
    cmp_imm(0x1f); // are we doing a long firebar?
    if (!carry_flag) { goto SetMFbar; } // no, branch then
    ldy_imm(0xb); // otherwise load value for long firebars
  
SetMFbar:
    write_byte(0xed, y); // store maximum value for length of firebars
    lda_imm(0x0);
    write_byte(0x0, a); // initialize counter here
  
DrawFbar:
    lda_zp(0xef); // load high byte of spinstate
    GetFirebarPosition(); // get fireball position data depending on firebar part
    jsr(DrawFirebar_Collision, 251); // position it properly, draw it and do collision detection
    lda_zp(0x0); // check which firebar part
    cmp_imm(0x4);
    if (!zero_flag) { goto NextFbar; }
    ldy_abs(DuplicateObj_Offset); // if we arrive at fifth firebar part,
    lda_absy(Enemy_SprDataOffset); // get offset from long firebar and load OAM data offset
    write_byte(0x6, a); // using long firebar offset, then store as new one here
  
NextFbar:
    inc_zp(0x0); // move onto the next firebar part
    lda_zp(0x0);
    cmp_zp(0xed); // if we end up at the maximum part, go on and leave
    if (!carry_flag) { goto DrawFbar; } // otherwise go back and do another
  
SkipFBar:
    goto rts;
  
DrawFirebar_Collision:
    lda_zp(0x3); // store mirror data elsewhere
    write_byte(0x5, a);
    ldy_zp(0x6); // load OAM data offset for firebar
    lda_zp(0x1); // load horizontal adder we got from position loader
    lsr_zp(0x5); // shift LSB of mirror data
    if (carry_flag) { goto AddHA; } // if carry was set, skip this part
    eor_imm(0xff);
    adc_imm(0x1); // otherwise get two's compliment of horizontal adder
  
AddHA:
    carry_flag = false; // add horizontal coordinate relative to screen to
    adc_abs(Enemy_Rel_XPos); // horizontal adder, modified or otherwise
    write_byte(Sprite_X_Position + y, a); // store as X coordinate here
    write_byte(0x6, a); // store here for now, note offset is saved in Y still
    cmp_abs(Enemy_Rel_XPos); // compare X coordinate of sprite to original X of firebar
    if (carry_flag) { goto SubtR1; } // if sprite coordinate => original coordinate, branch
    lda_abs(Enemy_Rel_XPos);
    carry_flag = true; // otherwise subtract sprite X from the
    sbc_zp(0x6); // original one and skip this part
    goto ChkFOfs;
  
SubtR1:
    carry_flag = true; // subtract original X from the
    sbc_abs(Enemy_Rel_XPos); // current sprite X
  
ChkFOfs:
    cmp_imm(0x59); // if difference of coordinates within a certain range,
    if (!carry_flag) { goto VAHandl; } // continue by handling vertical adder
    lda_imm(0xf8); // otherwise, load offscreen Y coordinate
    if (!zero_flag) { goto SetVFbr; } // and unconditionally branch to move sprite offscreen
  
VAHandl:
    lda_abs(Enemy_Rel_YPos); // if vertical relative coordinate offscreen,
    cmp_imm(0xf8); // skip ahead of this part and write into sprite Y coordinate
    if (zero_flag) { goto SetVFbr; }
    lda_zp(0x2); // load vertical adder we got from position loader
    lsr_zp(0x5); // shift LSB of mirror data one more time
    if (carry_flag) { goto AddVA; } // if carry was set, skip this part
    eor_imm(0xff);
    adc_imm(0x1); // otherwise get two's compliment of second part
  
AddVA:
    carry_flag = false; // add vertical coordinate relative to screen to 
    adc_abs(Enemy_Rel_YPos); // the second data, modified or otherwise
  
SetVFbr:
    write_byte(Sprite_Y_Position + y, a); // store as Y coordinate here
    write_byte(0x7, a); // also store here for now
  
FirebarCollision:
    DrawFirebar(); // run sub here to draw current tile of firebar
    tya(); // return OAM data offset and save
    pha(); // to the stack for now
    lda_abs(StarInvincibleTimer); // if star mario invincibility timer
    ora_abs(TimerControl); // or master timer controls set
    if (!zero_flag) { goto NoColFB; } // then skip all of this
    write_byte(0x5, a); // otherwise initialize counter
    ldy_zp(Player_Y_HighPos);
    dey(); // if player's vertical high byte offscreen,
    if (!zero_flag) { goto NoColFB; } // skip all of this
    ldy_zp(Player_Y_Position); // get player's vertical position
    lda_abs(PlayerSize); // get player's size
    if (!zero_flag) { goto AdjSm; } // if player small, branch to alter variables
    lda_abs(CrouchingFlag);
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
    sbc_zp(0x7); // from the vertical coordinate of the player
    if (!neg_flag) { goto ChkVFBD; } // if player lower on the screen than firebar, 
    eor_imm(0xff); // skip two's compliment part
    carry_flag = false; // otherwise get two's compliment
    adc_imm(0x1);
  
ChkVFBD:
    cmp_imm(0x8); // if difference => 8 pixels, skip ahead of this part
    if (carry_flag) { goto Chk2Ofs; }
    lda_zp(0x6); // if firebar on far right on the screen, skip this,
    cmp_imm(0xf0); // because, really, what's the point?
    if (carry_flag) { goto Chk2Ofs; }
    lda_abs(Sprite_X_Position + 4); // get OAM X coordinate for sprite #1
    carry_flag = false;
    adc_imm(0x4); // add four pixels
    write_byte(0x4, a); // store here
    carry_flag = true; // subtract horizontal coordinate of firebar
    sbc_zp(0x6); // from the X coordinate of player's sprite 1
    if (!neg_flag) { goto ChkFBCl; } // if modded X coordinate to the right of firebar
    eor_imm(0xff); // skip two's compliment part
    carry_flag = false; // otherwise get two's compliment
    adc_imm(0x1);
  
ChkFBCl:
    cmp_imm(0x8); // if difference < 8 pixels, collision, thus branch
    if (!carry_flag) { goto ChgSDir; } // to process
  
Chk2Ofs:
    lda_zp(0x5); // if value of $02 was set earlier for whatever reason,
    cmp_imm(0x2); // branch to increment OAM offset and leave, no collision
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
    cmp_zp(0x6); // is greater than horizontal coordinate of firebar
    if (carry_flag) { goto SetSDir; } // then do not alter movement direction
    inx(); // otherwise increment it
  
SetSDir:
    write_byte(Enemy_MovingDir, x); // store movement direction here
    ldx_imm(0x0);
    lda_zp(0x0); // save value written to $00 to stack
    pha();
    jsr(InjurePlayer, 252); // perform sub to hurt or kill player
    pla();
    write_byte(0x0, a); // get value of $00 from stack
  
NoColFB:
    pla(); // get OAM data offset
    carry_flag = false; // add four to it and save
    adc_imm(0x4);
    write_byte(0x6, a);
    ldx_zp(ObjectOffset); // get enemy object buffer offset and leave
    goto rts;
    // --------------------------------
  
MoveFlyingCheepCheep:
    lda_zpx(Enemy_State); // check cheep-cheep's enemy state
    and_imm(0b00100000); // for d5 set
    if (zero_flag) { goto FlyCC; } // branch to continue code if not set
    lda_imm(0x0);
    write_byte(Enemy_SprAttrib + x, a); // otherwise clear sprite attributes
    goto MoveJ_EnemyVertically; // and jump to move defeated cheep-cheep downwards
  
FlyCC:
    jsr(MoveEnemyHorizontally, 253); // move cheep-cheep horizontally based on speed and force
    ldy_imm(0xd); // set vertical movement amount
    lda_imm(0x5); // set maximum speed
    jsr(SetXMoveAmt, 254); // branch to impose gravity on flying cheep-cheep
    lda_absx(Enemy_Y_MoveForce);
    lsr_acc(); // get vertical movement force and
    lsr_acc(); // move high nybble to low
    lsr_acc();
    lsr_acc();
    tay(); // save as offset (note this tends to go into reach of code)
    lda_zpx(Enemy_Y_Position); // get vertical position
    carry_flag = true; // subtract pseudorandom value based on offset from position
    sbc_absy(PRandomSubtracter);
    if (!neg_flag) { goto AddCCF; } // if result within top half of screen, skip this part
    eor_imm(0xff);
    carry_flag = false; // otherwise get two's compliment
    adc_imm(0x1);
  
AddCCF:
    cmp_imm(0x8); // if result or two's compliment greater than eight,
    if (carry_flag) { goto BPGet; } // skip to the end without changing movement force
    lda_absx(Enemy_Y_MoveForce);
    carry_flag = false;
    adc_imm(0x10); // otherwise add to it
    write_byte(Enemy_Y_MoveForce + x, a);
    lsr_acc(); // move high nybble to low again
    lsr_acc();
    lsr_acc();
    lsr_acc();
    tay();
  
BPGet:
    lda_absy(FlyCCBPriority); // load bg priority data and store (this is very likely
    write_byte(Enemy_SprAttrib + x, a); // broken or residual code, value is overwritten before
    goto rts; // drawing it next frame), then leave
    // --------------------------------
    // $00 - used to hold horizontal difference
    // $01-$03 - used to hold difference adjusters
  
MoveLakitu:
    lda_zpx(Enemy_State); // check lakitu's enemy state
    and_imm(0b00100000); // for d5 set
    if (zero_flag) { goto ChkLS; } // if not set, continue with code
    goto MoveD_EnemyVertically; // otherwise jump to move defeated lakitu downwards
  
ChkLS:
    lda_zpx(Enemy_State); // if lakitu's enemy state not set at all,
    if (zero_flag) { goto Fr12S; } // go ahead and continue with code
    lda_imm(0x0);
    write_byte(LakituMoveDirection + x, a); // otherwise initialize moving direction to move to left
    write_byte(EnemyFrenzyBuffer, a); // initialize frenzy buffer
    lda_imm(0x10);
    if (!zero_flag) { goto SetLSpd; } // load horizontal speed and do unconditional branch
  
Fr12S:
    lda_imm(Spiny);
    write_byte(EnemyFrenzyBuffer, a); // set spiny identifier in frenzy buffer
    ldy_imm(0x2);
  
LdLDa:
    lda_absy(LakituDiffAdj); // load values
    write_byte(0x1 + y, a); // store in zero page
    dey();
    if (!neg_flag) { goto LdLDa; } // do this until all values are stired
    jsr(PlayerLakituDiff, 255); // execute sub to set speed and create spinys
  
SetLSpd:
    write_byte(LakituMoveSpeed + x, a); // set movement speed returned from sub
    ldy_imm(0x1); // set moving direction to right by default
    lda_zpx(LakituMoveDirection);
    and_imm(0x1); // get LSB of moving direction
    if (!zero_flag) { goto SetLMov; } // if set, branch to the end to use moving direction
    lda_zpx(LakituMoveSpeed);
    eor_imm(0xff); // get two's compliment of moving speed
    carry_flag = false;
    adc_imm(0x1);
    write_byte(LakituMoveSpeed + x, a); // store as new moving speed
    iny(); // increment moving direction to left
  
SetLMov:
    write_byte(Enemy_MovingDir + x, y); // store moving direction
    goto MoveEnemyHorizontally; // move lakitu horizontally
  
PlayerLakituDiff:
    ldy_imm(0x0); // set Y for default value
    PlayerEnemyDiff(); // get horizontal difference between enemy and player
    if (!neg_flag) { goto ChkLakDif; } // branch if enemy is to the right of the player
    iny(); // increment Y for left of player
    lda_zp(0x0);
    eor_imm(0xff); // get two's compliment of low byte of horizontal difference
    carry_flag = false;
    adc_imm(0x1); // store two's compliment as horizontal difference
    write_byte(0x0, a);
  
ChkLakDif:
    lda_zp(0x0); // get low byte of horizontal difference
    cmp_imm(0x3c); // if within a certain distance of player, branch
    if (!carry_flag) { goto ChkPSpeed; }
    lda_imm(0x3c); // otherwise set maximum distance
    write_byte(0x0, a);
    lda_zpx(Enemy_ID); // check if lakitu is in our current enemy slot
    cmp_imm(Lakitu);
    if (!zero_flag) { goto ChkPSpeed; } // if not, branch elsewhere
    tya(); // compare contents of Y, now in A
    cmp_zpx(LakituMoveDirection); // to what is being used as horizontal movement direction
    if (zero_flag) { goto ChkPSpeed; } // if moving toward the player, branch, do not alter
    lda_zpx(LakituMoveDirection); // if moving to the left beyond maximum distance,
    if (zero_flag) { goto SetLMovD; } // branch and alter without delay
    dec_zpx(LakituMoveSpeed); // decrement horizontal speed
    lda_zpx(LakituMoveSpeed); // if horizontal speed not yet at zero, branch to leave
    if (!zero_flag) { goto ExMoveLak; }
  
SetLMovD:
    tya(); // set horizontal direction depending on horizontal
    write_byte(LakituMoveDirection + x, a); // difference between enemy and player if necessary
  
ChkPSpeed:
    lda_zp(0x0);
    and_imm(0b00111100); // mask out all but four bits in the middle
    lsr_acc(); // divide masked difference by four
    lsr_acc();
    write_byte(0x0, a); // store as new value
    ldy_imm(0x0); // init offset
    lda_zp(Player_X_Speed);
    if (zero_flag) { goto SubDifAdj; } // if player not moving horizontally, branch
    lda_abs(ScrollAmount);
    if (zero_flag) { goto SubDifAdj; } // if scroll speed not set, branch to same place
    iny(); // otherwise increment offset
    lda_zp(Player_X_Speed);
    cmp_imm(0x19); // if player not running, branch
    if (!carry_flag) { goto ChkSpinyO; }
    lda_abs(ScrollAmount);
    cmp_imm(0x2); // if scroll speed below a certain amount, branch
    if (!carry_flag) { goto ChkSpinyO; } // to same place
    iny(); // otherwise increment once more
  
ChkSpinyO:
    lda_zpx(Enemy_ID); // check for spiny object
    cmp_imm(Spiny);
    if (!zero_flag) { goto ChkEmySpd; } // branch if not found
    lda_zp(Player_X_Speed); // if player not moving, skip this part
    if (!zero_flag) { goto SubDifAdj; }
  
ChkEmySpd:
    lda_zpx(Enemy_Y_Speed); // check vertical speed
    if (!zero_flag) { goto SubDifAdj; } // branch if nonzero
    ldy_imm(0x0); // otherwise reinit offset
  
SubDifAdj:
    lda_absy(0x1); // get one of three saved values from earlier
    ldy_zp(0x0); // get saved horizontal difference
  
SPixelLak:
    carry_flag = true; // subtract one for each pixel of horizontal difference
    sbc_imm(0x1); // from one of three saved values
    dey();
    if (!neg_flag) { goto SPixelLak; } // branch until all pixels are subtracted, to adjust difference
  
ExMoveLak:
    goto rts; // leave!!!
    // -------------------------------------------------------------------------------------
    // $04-$05 - used to store name table address in little endian order
  
BridgeCollapse:
    ldx_abs(BowserFront_Offset); // get enemy offset for bowser
    lda_zpx(Enemy_ID); // check enemy object identifier for bowser
    cmp_imm(Bowser); // if not found, branch ahead,
    if (!zero_flag) { goto SetM2; } // metatile removal not necessary
    write_byte(ObjectOffset, x); // store as enemy offset here
    lda_zpx(Enemy_State); // if bowser in normal state, skip all of this
    if (zero_flag) { goto RemoveBridge; }
    and_imm(0b01000000); // if bowser's state has d6 clear, skip to silence music
    if (zero_flag) { goto SetM2; }
    lda_zpx(Enemy_Y_Position); // check bowser's vertical coordinate
    cmp_imm(0xe0); // if bowser not yet low enough, skip this part ahead
    if (!carry_flag) { goto MoveD_Bowser; }
  
SetM2:
    lda_imm(Silence); // silence music
    write_byte(EventMusicQueue, a);
    inc_abs(OperMode_Task); // move onto next secondary mode in autoctrl mode
    goto KillAllEnemies; // jump to empty all enemy slots and then leave  
  
MoveD_Bowser:
    jsr(MoveEnemySlowVert, 256); // do a sub to move bowser downwards
    goto BowserGfxHandler; // jump to draw bowser's front and rear, then leave
  
RemoveBridge:
    dec_abs(BowserFeetCounter); // decrement timer to control bowser's feet
    if (!zero_flag) { goto NoBFall; } // if not expired, skip all of this
    lda_imm(0x4);
    write_byte(BowserFeetCounter, a); // otherwise, set timer now
    lda_abs(BowserBodyControls);
    eor_imm(0x1); // invert bit to control bowser's feet
    write_byte(BowserBodyControls, a);
    lda_imm(0x22); // put high byte of name table address here for now
    write_byte(0x5, a);
    ldy_abs(BridgeCollapseOffset); // get bridge collapse offset here
    lda_absy(BridgeCollapseData); // load low byte of name table address and store here
    write_byte(0x4, a);
    ldy_abs(VRAM_Buffer1_Offset); // increment vram buffer offset
    iny();
    ldx_imm(0xc); // set offset for tile data for sub to draw blank metatile
    RemBridge(); // do sub here to remove bowser's bridge metatiles
    ldx_zp(ObjectOffset); // get enemy offset
    jsr(MoveVOffset, 257); // set new vram buffer offset
    lda_imm(Sfx_Blast); // load the fireworks/gunfire sound into the square 2 sfx
    write_byte(Square2SoundQueue, a); // queue while at the same time loading the brick
    lda_imm(Sfx_BrickShatter); // shatter sound into the noise sfx queue thus
    write_byte(NoiseSoundQueue, a); // producing the unique sound of the bridge collapsing 
    inc_abs(BridgeCollapseOffset); // increment bridge collapse offset
    lda_abs(BridgeCollapseOffset);
    cmp_imm(0xf); // if bridge collapse offset has not yet reached
    if (!zero_flag) { goto NoBFall; } // the end, go ahead and skip this part
    InitVStf(); // initialize whatever vertical speed bowser has
    lda_imm(0b01000000);
    write_byte(Enemy_State + x, a); // set bowser's state to one of defeated states (d6 set)
    lda_imm(Sfx_BowserFall);
    write_byte(Square2SoundQueue, a); // play bowser defeat sound
  
NoBFall:
    goto BowserGfxHandler; // jump to code that draws bowser
    // --------------------------------
  
RunBowser:
    lda_zpx(Enemy_State); // if d5 in enemy state is not set
    and_imm(0b00100000); // then branch elsewhere to run bowser
    if (zero_flag) { goto BowserControl; }
    lda_zpx(Enemy_Y_Position); // otherwise check vertical position
    cmp_imm(0xe0); // if above a certain point, branch to move defeated bowser
    if (!carry_flag) { goto MoveD_Bowser; } // otherwise proceed to KillAllEnemies
  
KillAllEnemies:
    ldx_imm(0x4); // start with last enemy slot
  
KillLoop:
    EraseEnemyObject(); // branch to kill enemy objects
    dex(); // move onto next enemy slot
    if (!neg_flag) { goto KillLoop; } // do this until all slots are emptied
    write_byte(EnemyFrenzyBuffer, a); // empty frenzy buffer
    ldx_zp(ObjectOffset); // get enemy object offset and leave
    goto rts;
  
BowserControl:
    lda_imm(0x0);
    write_byte(EnemyFrenzyBuffer, a); // empty frenzy buffer
    lda_abs(TimerControl); // if master timer control not set,
    if (zero_flag) { goto ChkMouth; } // skip jump and execute code here
    goto SkipToFB; // otherwise, jump over a bunch of code
  
ChkMouth:
    lda_abs(BowserBodyControls); // check bowser's mouth
    if (!neg_flag) { goto FeetTmr; } // if bit clear, go ahead with code here
    goto HammerChk; // otherwise skip a whole section starting here
  
FeetTmr:
    dec_abs(BowserFeetCounter); // decrement timer to control bowser's feet
    if (!zero_flag) { goto ResetMDr; } // if not expired, skip this part
    lda_imm(0x20); // otherwise, reset timer
    write_byte(BowserFeetCounter, a);
    lda_abs(BowserBodyControls); // and invert bit used
    eor_imm(0b00000001); // to control bowser's feet
    write_byte(BowserBodyControls, a);
  
ResetMDr:
    lda_zp(FrameCounter); // check frame counter
    and_imm(0b00001111); // if not on every sixteenth frame, skip
    if (!zero_flag) { goto B_FaceP; } // ahead to continue code
    lda_imm(0x2); // otherwise reset moving/facing direction every
    write_byte(Enemy_MovingDir + x, a); // sixteen frames
  
B_FaceP:
    lda_absx(EnemyFrameTimer); // if timer set here expired,
    if (zero_flag) { goto GetPRCmp; } // branch to next section
    PlayerEnemyDiff(); // get horizontal difference between player and bowser,
    if (!neg_flag) { goto GetPRCmp; } // and branch if bowser to the right of the player
    lda_imm(0x1);
    write_byte(Enemy_MovingDir + x, a); // set bowser to move and face to the right
    lda_imm(0x2);
    write_byte(BowserMovementSpeed, a); // set movement speed
    lda_imm(0x20);
    write_byte(EnemyFrameTimer + x, a); // set timer here
    write_byte(BowserFireBreathTimer, a); // set timer used for bowser's flame
    lda_zpx(Enemy_X_Position);
    cmp_imm(0xc8); // if bowser to the right past a certain point,
    if (carry_flag) { goto HammerChk; } // skip ahead to some other section
  
GetPRCmp:
    lda_zp(FrameCounter); // get frame counter
    and_imm(0b00000011);
    if (!zero_flag) { goto HammerChk; } // execute this code every fourth frame, otherwise branch
    lda_zpx(Enemy_X_Position);
    cmp_abs(BowserOrigXPos); // if bowser not at original horizontal position,
    if (!zero_flag) { goto GetDToO; } // branch to skip this part
    lda_absx(PseudoRandomBitReg);
    and_imm(0b00000011); // get pseudorandom offset
    tay();
    lda_absy(PRandomRange); // load value using pseudorandom offset
    write_byte(MaxRangeFromOrigin, a); // and store here
  
GetDToO:
    lda_zpx(Enemy_X_Position);
    carry_flag = false; // add movement speed to bowser's horizontal
    adc_abs(BowserMovementSpeed); // coordinate and save as new horizontal position
    write_byte(Enemy_X_Position + x, a);
    ldy_zpx(Enemy_MovingDir);
    cpy_imm(0x1); // if bowser moving and facing to the right, skip ahead
    if (zero_flag) { goto HammerChk; }
    ldy_imm(0xff); // set default movement speed here (move left)
    carry_flag = true; // get difference of current vs. original
    sbc_abs(BowserOrigXPos); // horizontal position
    if (!neg_flag) { goto CompDToO; } // if current position to the right of original, skip ahead
    eor_imm(0xff);
    carry_flag = false; // get two's compliment
    adc_imm(0x1);
    ldy_imm(0x1); // set alternate movement speed here (move right)
  
CompDToO:
    cmp_abs(MaxRangeFromOrigin); // compare difference with pseudorandom value
    if (!carry_flag) { goto HammerChk; } // if difference < pseudorandom value, leave speed alone
    write_byte(BowserMovementSpeed, y); // otherwise change bowser's movement speed
  
HammerChk:
    lda_absx(EnemyFrameTimer); // if timer set here not expired yet, skip ahead to
    if (!zero_flag) { goto MakeBJump; } // some other section of code
    jsr(MoveEnemySlowVert, 258); // otherwise start by moving bowser downwards
    lda_abs(WorldNumber); // check world number
    cmp_imm(World6);
    if (!carry_flag) { goto SetHmrTmr; } // if world 1-5, skip this part (not time to throw hammers yet)
    lda_zp(FrameCounter);
    and_imm(0b00000011); // check to see if it's time to execute sub
    if (!zero_flag) { goto SetHmrTmr; } // if not, skip sub, otherwise
    jsr(SpawnHammerObj, 259); // execute sub on every fourth frame to spawn misc object (hammer)
  
SetHmrTmr:
    lda_zpx(Enemy_Y_Position); // get current vertical position
    cmp_imm(0x80); // if still above a certain point
    if (!carry_flag) { goto ChkFireB; } // then skip to world number check for flames
    lda_absx(PseudoRandomBitReg);
    and_imm(0b00000011); // get pseudorandom offset
    tay();
    lda_absy(PRandomRange); // get value using pseudorandom offset
    write_byte(EnemyFrameTimer + x, a); // set for timer here
  
SkipToFB:
    goto ChkFireB; // jump to execute flames code
  
MakeBJump:
    cmp_imm(0x1); // if timer not yet about to expire,
    if (!zero_flag) { goto ChkFireB; } // skip ahead to next part
    dec_zpx(Enemy_Y_Position); // otherwise decrement vertical coordinate
    InitVStf(); // initialize movement amount
    lda_imm(0xfe);
    write_byte(Enemy_Y_Speed + x, a); // set vertical speed to move bowser upwards
  
ChkFireB:
    lda_abs(WorldNumber); // check world number here
    cmp_imm(World8); // world 8?
    if (zero_flag) { goto SpawnFBr; } // if so, execute this part here
    cmp_imm(World6); // world 6-7?
    if (carry_flag) { goto BowserGfxHandler; } // if so, skip this part here
  
SpawnFBr:
    lda_abs(BowserFireBreathTimer); // check timer here
    if (!zero_flag) { goto BowserGfxHandler; } // if not expired yet, skip all of this
    lda_imm(0x20);
    write_byte(BowserFireBreathTimer, a); // set timer here
    lda_abs(BowserBodyControls);
    eor_imm(0b10000000); // invert bowser's mouth bit to open
    write_byte(BowserBodyControls, a); // and close bowser's mouth
    if (neg_flag) { goto ChkFireB; } // if bowser's mouth open, loop back
    jsr(SetFlameTimer, 260); // get timing for bowser's flame
    ldy_abs(SecondaryHardMode);
    if (zero_flag) { goto SetFBTmr; } // if secondary hard mode flag not set, skip this
    carry_flag = true;
    sbc_imm(0x10); // otherwise subtract from value in A
  
SetFBTmr:
    write_byte(BowserFireBreathTimer, a); // set value as timer here
    lda_imm(BowserFlame); // put bowser's flame identifier
    write_byte(EnemyFrenzyBuffer, a); // in enemy frenzy buffer
    // --------------------------------
  
BowserGfxHandler:
    jsr(ProcessBowserHalf, 261); // do a sub here to process bowser's front
    ldy_imm(0x10); // load default value here to position bowser's rear
    lda_zpx(Enemy_MovingDir); // check moving direction
    lsr_acc();
    if (!carry_flag) { goto CopyFToR; } // if moving left, use default
    ldy_imm(0xf0); // otherwise load alternate positioning value here
  
CopyFToR:
    tya(); // move bowser's rear object position value to A
    carry_flag = false;
    adc_zpx(Enemy_X_Position); // add to bowser's front object horizontal coordinate
    ldy_abs(DuplicateObj_Offset); // get bowser's rear object offset
    write_byte(Enemy_X_Position + y, a); // store A as bowser's rear horizontal coordinate
    lda_zpx(Enemy_Y_Position);
    carry_flag = false; // add eight pixels to bowser's front object
    adc_imm(0x8); // vertical coordinate and store as vertical coordinate
    write_byte(Enemy_Y_Position + y, a); // for bowser's rear
    lda_zpx(Enemy_State);
    write_byte(Enemy_State + y, a); // copy enemy state directly from front to rear
    lda_zpx(Enemy_MovingDir);
    write_byte(Enemy_MovingDir + y, a); // copy moving direction also
    lda_zp(ObjectOffset); // save enemy object offset of front to stack
    pha();
    ldx_abs(DuplicateObj_Offset); // put enemy object offset of rear as current
    write_byte(ObjectOffset, x);
    lda_imm(Bowser); // set bowser's enemy identifier
    write_byte(Enemy_ID + x, a); // store in bowser's rear object
    jsr(ProcessBowserHalf, 262); // do a sub here to process bowser's rear
    pla();
    write_byte(ObjectOffset, a); // get original enemy object offset
    tax();
    lda_imm(0x0); // nullify bowser's front/rear graphics flag
    write_byte(BowserGfxFlag, a);
  
ExBGfxH:
    goto rts; // leave!
  
ProcessBowserHalf:
    inc_abs(BowserGfxFlag); // increment bowser's graphics flag, then run subroutines
    jsr(RunRetainerObj, 263); // to get offscreen bits, relative position and draw bowser (finally!)
    lda_zpx(Enemy_State);
    if (!zero_flag) { goto ExBGfxH; } // if either enemy object not in normal state, branch to leave
    lda_imm(0xa);
    write_byte(Enemy_BoundBoxCtrl + x, a); // set bounding box size control
    jsr(GetEnemyBoundBox, 264); // get bounding box coordinates
    goto PlayerEnemyCollision; // do player-to-enemy collision detection
    // -------------------------------------------------------------------------------------
    // $00 - used to hold movement force and tile number
    // $01 - used to hold sprite attribute data
  
SetFlameTimer:
    ldy_abs(BowserFlameTimerCtrl); // load counter as offset
    inc_abs(BowserFlameTimerCtrl); // increment
    lda_abs(BowserFlameTimerCtrl); // mask out all but 3 LSB
    and_imm(0b00000111); // to keep in range of 0-7
    write_byte(BowserFlameTimerCtrl, a);
    lda_absy(FlameTimerData); // load value to be used then leave
  
ExFl:
    goto rts;
  
ProcBowserFlame:
    lda_abs(TimerControl); // if master timer control flag set,
    if (!zero_flag) { goto SetGfxF; } // skip all of this
    lda_imm(0x40); // load default movement force
    ldy_abs(SecondaryHardMode);
    if (zero_flag) { goto SFlmX; } // if secondary hard mode flag not set, use default
    lda_imm(0x60); // otherwise load alternate movement force to go faster
  
SFlmX:
    write_byte(0x0, a); // store value here
    lda_absx(Enemy_X_MoveForce);
    carry_flag = true; // subtract value from movement force
    sbc_zp(0x0);
    write_byte(Enemy_X_MoveForce + x, a); // save new value
    lda_zpx(Enemy_X_Position);
    sbc_imm(0x1); // subtract one from horizontal position to move
    write_byte(Enemy_X_Position + x, a); // to the left
    lda_zpx(Enemy_PageLoc);
    sbc_imm(0x0); // subtract borrow from page location
    write_byte(Enemy_PageLoc + x, a);
    ldy_absx(BowserFlamePRandomOfs); // get some value here and use as offset
    lda_zpx(Enemy_Y_Position); // load vertical coordinate
    cmp_absy(FlameYPosData); // compare against coordinate data using $0417,x as offset
    if (zero_flag) { goto SetGfxF; } // if equal, branch and do not modify coordinate
    carry_flag = false;
    adc_absx(Enemy_Y_MoveForce); // otherwise add value here to coordinate and store
    write_byte(Enemy_Y_Position + x, a); // as new vertical coordinate
  
SetGfxF:
    jsr(RelativeEnemyPosition, 265); // get new relative coordinates
    lda_zpx(Enemy_State); // if bowser's flame not in normal state,
    if (!zero_flag) { goto ExFl; } // branch to leave
    lda_imm(0x51); // otherwise, continue
    write_byte(0x0, a); // write first tile number
    ldy_imm(0x2); // load attributes without vertical flip by default
    lda_zp(FrameCounter);
    and_imm(0b00000010); // invert vertical flip bit every 2 frames
    if (zero_flag) { goto FlmeAt; } // if d1 not set, write default value
    ldy_imm(0x82); // otherwise write value with vertical flip bit set
  
FlmeAt:
    write_byte(0x1, y); // set bowser's flame sprite attributes here
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    ldx_imm(0x0);
  
DrawFlameLoop:
    lda_abs(Enemy_Rel_YPos); // get Y relative coordinate of current enemy object
    write_byte(Sprite_Y_Position + y, a); // write into Y coordinate of OAM data
    lda_zp(0x0);
    write_byte(Sprite_Tilenumber + y, a); // write current tile number into OAM data
    inc_zp(0x0); // increment tile number to draw more bowser's flame
    lda_zp(0x1);
    write_byte(Sprite_Attributes + y, a); // write saved attributes into OAM data
    lda_abs(Enemy_Rel_XPos);
    write_byte(Sprite_X_Position + y, a); // write X relative coordinate of current enemy object
    carry_flag = false;
    adc_imm(0x8);
    write_byte(Enemy_Rel_XPos, a); // then add eight to it and store
    iny();
    iny();
    iny();
    iny(); // increment Y four times to move onto the next OAM
    inx(); // move onto the next OAM, and branch if three
    cpx_imm(0x3); // have not yet been done
    if (!carry_flag) { goto DrawFlameLoop; }
    ldx_zp(ObjectOffset); // reload original enemy offset
    jsr(GetEnemyOffscreenBits, 266); // get offscreen information
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    lda_abs(Enemy_OffscreenBits); // get enemy object offscreen bits
    lsr_acc(); // move d0 to carry and result to stack
    pha();
    if (!carry_flag) { goto M3FOfs; } // branch if carry not set
    lda_imm(0xf8); // otherwise move sprite offscreen, this part likely
    write_byte(Sprite_Y_Position + 12 + y, a); // residual since flame is only made of three sprites
  
M3FOfs:
    pla(); // get bits from stack
    lsr_acc(); // move d1 to carry and move bits back to stack
    pha();
    if (!carry_flag) { goto M2FOfs; } // branch if carry not set again
    lda_imm(0xf8); // otherwise move third sprite offscreen
    write_byte(Sprite_Y_Position + 8 + y, a);
  
M2FOfs:
    pla(); // get bits from stack again
    lsr_acc(); // move d2 to carry and move bits back to stack again
    pha();
    if (!carry_flag) { goto M1FOfs; } // branch if carry not set yet again
    lda_imm(0xf8); // otherwise move second sprite offscreen
    write_byte(Sprite_Y_Position + 4 + y, a);
  
M1FOfs:
    pla(); // get bits from stack one last time
    lsr_acc(); // move d3 to carry
    if (!carry_flag) { goto ExFlmeD; } // branch if carry not set one last time
    lda_imm(0xf8);
    write_byte(Sprite_Y_Position + y, a); // otherwise move first sprite offscreen
  
ExFlmeD:
    goto rts; // leave
    // --------------------------------
  
RunFireworks:
    dec_zpx(ExplosionTimerCounter); // decrement explosion timing counter here
    if (!zero_flag) { goto SetupExpl; } // if not expired, skip this part
    lda_imm(0x8);
    write_byte(ExplosionTimerCounter + x, a); // reset counter
    inc_zpx(ExplosionGfxCounter); // increment explosion graphics counter
    lda_zpx(ExplosionGfxCounter);
    cmp_imm(0x3); // check explosion graphics counter
    if (carry_flag) { goto FireworksSoundScore; } // if at a certain point, branch to kill this object
  
SetupExpl:
    jsr(RelativeEnemyPosition, 267); // get relative coordinates of explosion
    lda_abs(Enemy_Rel_YPos); // copy relative coordinates
    write_byte(Fireball_Rel_YPos, a); // from the enemy object to the fireball object
    lda_abs(Enemy_Rel_XPos); // first vertical, then horizontal
    write_byte(Fireball_Rel_XPos, a);
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    lda_zpx(ExplosionGfxCounter); // get explosion graphics counter
    jsr(DrawExplosion_Fireworks, 268); // do a sub to draw the explosion then leave
    goto rts;
  
FireworksSoundScore:
    lda_imm(0x0); // disable enemy buffer flag
    write_byte(Enemy_Flag + x, a);
    lda_imm(Sfx_Blast); // play fireworks/gunfire sound
    write_byte(Square2SoundQueue, a);
    lda_imm(0x5); // set part of score modifier for 500 points
    write_byte(DigitModifier + 4, a);
    goto EndAreaPoints; // jump to award points accordingly then leave
    // --------------------------------
  
RunStarFlagObj:
    lda_imm(0x0); // initialize enemy frenzy buffer
    write_byte(EnemyFrenzyBuffer, a);
    lda_abs(StarFlagTaskControl); // check star flag object task number here
    cmp_imm(0x5); // if greater than 5, branch to exit
    if (carry_flag) { goto StarFlagExit; }
    // jsr JumpEngine
    switch (a) {
      case 0: goto StarFlagExit;
      case 1: goto GameTimerFireworks;
      case 2: goto AwardGameTimerPoints;
      case 3: goto RaiseFlagSetoffFWorks;
      case 4: goto DelayToAreaEnd;
    }
  
GameTimerFireworks:
    ldy_imm(0x5); // set default state for star flag object
    lda_abs(GameTimerDisplay + 2); // get game timer's last digit
    cmp_imm(0x1);
    if (zero_flag) { goto SetFWC; } // if last digit of game timer set to 1, skip ahead
    ldy_imm(0x3); // otherwise load new value for state
    cmp_imm(0x3);
    if (zero_flag) { goto SetFWC; } // if last digit of game timer set to 3, skip ahead
    ldy_imm(0x0); // otherwise load one more potential value for state
    cmp_imm(0x6);
    if (zero_flag) { goto SetFWC; } // if last digit of game timer set to 6, skip ahead
    lda_imm(0xff); // otherwise set value for no fireworks
  
SetFWC:
    write_byte(FireworksCounter, a); // set fireworks counter here
    write_byte(Enemy_State + x, y); // set whatever state we have in star flag object
  
IncrementSFTask1:
    inc_abs(StarFlagTaskControl); // increment star flag object task number
  
StarFlagExit:
    goto rts; // leave
  
AwardGameTimerPoints:
    lda_abs(GameTimerDisplay); // check all game timer digits for any intervals left
    ora_abs(GameTimerDisplay + 1);
    ora_abs(GameTimerDisplay + 2);
    if (zero_flag) { goto IncrementSFTask1; } // if no time left on game timer at all, branch to next task
    lda_zp(FrameCounter);
    and_imm(0b00000100); // check frame counter for d2 set (skip ahead
    if (zero_flag) { goto NoTTick; } // for four frames every four frames) branch if not set
    lda_imm(Sfx_TimerTick);
    write_byte(Square2SoundQueue, a); // load timer tick sound
  
NoTTick:
    ldy_imm(0x23); // set offset here to subtract from game timer's last digit
    lda_imm(0xff); // set adder here to $ff, or -1, to subtract one
    write_byte(DigitModifier + 5, a); // from the last digit of the game timer
    jsr(DigitsMathRoutine, 269); // subtract digit
    lda_imm(0x5); // set now to add 50 points
    write_byte(DigitModifier + 5, a); // per game timer interval subtracted
  
EndAreaPoints:
    ldy_imm(0xb); // load offset for mario's score by default
    lda_abs(CurrentPlayer); // check player on the screen
    if (zero_flag) { goto ELPGive; } // if mario, do not change
    ldy_imm(0x11); // otherwise load offset for luigi's score
  
ELPGive:
    jsr(DigitsMathRoutine, 270); // award 50 points per game timer interval
    lda_abs(CurrentPlayer); // get player on the screen (or 500 points per
    asl_acc(); // fireworks explosion if branched here from there)
    asl_acc(); // shift to high nybble
    asl_acc();
    asl_acc();
    ora_imm(0b00000100); // add four to set nybble for game timer
    goto UpdateNumber; // jump to print the new score and game timer
  
RaiseFlagSetoffFWorks:
    lda_zpx(Enemy_Y_Position); // check star flag's vertical position
    cmp_imm(0x72); // against preset value
    if (!carry_flag) { goto SetoffF; } // if star flag higher vertically, branch to other code
    dec_zpx(Enemy_Y_Position); // otherwise, raise star flag by one pixel
    goto DrawStarFlag; // and skip this part here
  
SetoffF:
    lda_abs(FireworksCounter); // check fireworks counter
    if (zero_flag) { goto DrawFlagSetTimer; } // if no fireworks left to go off, skip this part
    if (neg_flag) { goto DrawFlagSetTimer; } // if no fireworks set to go off, skip this part
    lda_imm(Fireworks);
    write_byte(EnemyFrenzyBuffer, a); // otherwise set fireworks object in frenzy queue
  
DrawStarFlag:
    jsr(RelativeEnemyPosition, 271); // get relative coordinates of star flag
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    ldx_imm(0x3); // do four sprites
  
DSFLoop:
    lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
    carry_flag = false;
    adc_absx(StarFlagYPosAdder); // add Y coordinate adder data
    write_byte(Sprite_Y_Position + y, a); // store as Y coordinate
    lda_absx(StarFlagTileData); // get tile number
    write_byte(Sprite_Tilenumber + y, a); // store as tile number
    lda_imm(0x22); // set palette and background priority bits
    write_byte(Sprite_Attributes + y, a); // store as attributes
    lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
    carry_flag = false;
    adc_absx(StarFlagXPosAdder); // add X coordinate adder data
    write_byte(Sprite_X_Position + y, a); // store as X coordinate
    iny();
    iny(); // increment OAM data offset four bytes
    iny(); // for next sprite
    iny();
    dex(); // move onto next sprite
    if (!neg_flag) { goto DSFLoop; } // do this until all sprites are done
    ldx_zp(ObjectOffset); // get enemy object offset and leave
    goto rts;
  
DrawFlagSetTimer:
    jsr(DrawStarFlag, 272); // do sub to draw star flag
    lda_imm(0x6);
    write_byte(EnemyIntervalTimer + x, a); // set interval timer here
  
IncrementSFTask2:
    inc_abs(StarFlagTaskControl); // move onto next task
    goto rts;
  
DelayToAreaEnd:
    jsr(DrawStarFlag, 273); // do sub to draw star flag
    lda_absx(EnemyIntervalTimer); // if interval timer set in previous task
    if (!zero_flag) { goto StarFlagExit2; } // not yet expired, branch to leave
    lda_abs(EventMusicBuffer); // if event music buffer empty,
    if (zero_flag) { goto IncrementSFTask2; } // branch to increment task
  
StarFlagExit2:
    goto rts; // otherwise leave
    // --------------------------------
    // $00 - used to store horizontal difference between player and piranha plant
    // -------------------------------------------------------------------------------------
    // $07 - spinning speed
  
FirebarSpin:
    write_byte(0x7, a); // save spinning speed here
    lda_zpx(FirebarSpinDirection); // check spinning direction
    if (!zero_flag) { goto SpinCounterClockwise; } // if moving counter-clockwise, branch to other part
    ldy_imm(0x18); // possibly residual ldy
    lda_zpx(FirebarSpinState_Low);
    carry_flag = false; // add spinning speed to what would normally be
    adc_zp(0x7); // the horizontal speed
    write_byte(FirebarSpinState_Low + x, a);
    lda_zpx(FirebarSpinState_High); // add carry to what would normally be the vertical speed
    adc_imm(0x0);
    goto rts;
  
SpinCounterClockwise:
    ldy_imm(0x8); // possibly residual ldy
    lda_zpx(FirebarSpinState_Low);
    carry_flag = true; // subtract spinning speed to what would normally be
    sbc_zp(0x7); // the horizontal speed
    write_byte(FirebarSpinState_Low + x, a);
    lda_zpx(FirebarSpinState_High); // add carry to what would normally be the vertical speed
    sbc_imm(0x0);
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used to hold collision flag, Y movement force + 5 or low byte of name table for rope
    // $01 - used to hold high byte of name table for rope
    // $02 - used to hold page location of rope
  
BalancePlatform:
    lda_zpx(Enemy_Y_HighPos); // check high byte of vertical position
    cmp_imm(0x3);
    if (!zero_flag) { goto DoBPl; }
    EraseEnemyObject(); goto rts; // if far below screen, kill the object
  
DoBPl:
    lda_zpx(Enemy_State); // get object's state (set to $ff or other platform offset)
    if (!neg_flag) { goto CheckBalPlatform; } // if doing other balance platform, branch to leave
    goto rts;
  
CheckBalPlatform:
    tay(); // save offset from state as Y
    lda_absx(PlatformCollisionFlag); // get collision flag of platform
    write_byte(0x0, a); // store here
    lda_zpx(Enemy_MovingDir); // get moving direction
    if (zero_flag) { goto ChkForFall; }
    goto PlatformFall; // if set, jump here
  
ChkForFall:
    lda_imm(0x2d); // check if platform is above a certain point
    cmp_zpx(Enemy_Y_Position);
    if (!carry_flag) { goto ChkOtherForFall; } // if not, branch elsewhere
    cpy_zp(0x0); // if collision flag is set to same value as
    if (zero_flag) { goto MakePlatformFall; } // enemy state, branch to make platforms fall
    carry_flag = false;
    adc_imm(0x2); // otherwise add 2 pixels to vertical position
    write_byte(Enemy_Y_Position + x, a); // of current platform and branch elsewhere
    StopPlatforms(); goto rts; // to make platforms stop
  
MakePlatformFall:
    goto InitPlatformFall; // make platforms fall
  
ChkOtherForFall:
    cmp_zpy(Enemy_Y_Position); // check if other platform is above a certain point
    if (!carry_flag) { goto ChkToMoveBalPlat; } // if not, branch elsewhere
    cpx_zp(0x0); // if collision flag is set to same value as
    if (zero_flag) { goto MakePlatformFall; } // enemy state, branch to make platforms fall
    carry_flag = false;
    adc_imm(0x2); // otherwise add 2 pixels to vertical position
    write_byte(Enemy_Y_Position + y, a); // of other platform and branch elsewhere
    StopPlatforms(); goto rts; // jump to stop movement and do not return
  
ChkToMoveBalPlat:
    lda_zpx(Enemy_Y_Position); // save vertical position to stack
    pha();
    lda_absx(PlatformCollisionFlag); // get collision flag
    if (!neg_flag) { goto ColFlg; } // branch if collision
    lda_absx(Enemy_Y_MoveForce);
    carry_flag = false; // add $05 to contents of moveforce, whatever they be
    adc_imm(0x5);
    write_byte(0x0, a); // store here
    lda_zpx(Enemy_Y_Speed);
    adc_imm(0x0); // add carry to vertical speed
    if (neg_flag) { goto PlatDn; } // branch if moving downwards
    if (!zero_flag) { goto PlatUp; } // branch elsewhere if moving upwards
    lda_zp(0x0);
    cmp_imm(0xb); // check if there's still a little force left
    if (!carry_flag) { goto PlatSt; } // if not enough, branch to stop movement
    if (carry_flag) { goto PlatUp; } // otherwise keep branch to move upwards
  
ColFlg:
    cmp_zp(ObjectOffset); // if collision flag matches
    if (zero_flag) { goto PlatDn; } // current enemy object offset, branch
  
PlatUp:
    jsr(MovePlatformUp, 274); // do a sub to move upwards
    goto DoOtherPlatform; // jump ahead to remaining code
  
PlatSt:
    StopPlatforms(); // do a sub to stop movement
    goto DoOtherPlatform; // jump ahead to remaining code
  
PlatDn:
    jsr(MovePlatformDown, 275); // do a sub to move downwards
  
DoOtherPlatform:
    ldy_zpx(Enemy_State); // get offset of other platform
    pla(); // get old vertical coordinate from stack
    carry_flag = true;
    sbc_zpx(Enemy_Y_Position); // get difference of old vs. new coordinate
    carry_flag = false;
    adc_zpy(Enemy_Y_Position); // add difference to vertical coordinate of other
    write_byte(Enemy_Y_Position + y, a); // platform to move it in the opposite direction
    lda_absx(PlatformCollisionFlag); // if no collision, skip this part here
    if (neg_flag) { goto DrawEraseRope; }
    tax(); // put offset which collision occurred here
    jsr(PositionPlayerOnVPlat, 276); // and use it to position player accordingly
  
DrawEraseRope:
    ldy_zp(ObjectOffset); // get enemy object offset
    lda_zpy(Enemy_Y_Speed); // check to see if current platform is
    ora_absy(Enemy_Y_MoveForce); // moving at all
    if (zero_flag) { goto ExitRp; } // if not, skip all of this and branch to leave
    ldx_abs(VRAM_Buffer1_Offset); // get vram buffer offset
    cpx_imm(0x20); // if offset beyond a certain point, go ahead
    if (carry_flag) { goto ExitRp; } // and skip this, branch to leave
    lda_zpy(Enemy_Y_Speed);
    pha(); // save two copies of vertical speed to stack
    pha();
    SetupPlatformRope(); // do a sub to figure out where to put new bg tiles
    lda_zp(0x1); // write name table address to vram buffer
    write_byte(VRAM_Buffer1 + x, a); // first the high byte, then the low
    lda_zp(0x0);
    write_byte(VRAM_Buffer1 + 1 + x, a);
    lda_imm(0x2); // set length for 2 bytes
    write_byte(VRAM_Buffer1 + 2 + x, a);
    lda_zpy(Enemy_Y_Speed); // if platform moving upwards, branch 
    if (neg_flag) { goto EraseR1; } // to do something else
    lda_imm(0xa2);
    write_byte(VRAM_Buffer1 + 3 + x, a); // otherwise put tile numbers for left
    lda_imm(0xa3); // and right sides of rope in vram buffer
    write_byte(VRAM_Buffer1 + 4 + x, a);
    goto OtherRope; // jump to skip this part
  
EraseR1:
    lda_imm(0x24); // put blank tiles in vram buffer
    write_byte(VRAM_Buffer1 + 3 + x, a); // to erase rope
    write_byte(VRAM_Buffer1 + 4 + x, a);
  
OtherRope:
    lda_zpy(Enemy_State); // get offset of other platform from state
    tay(); // use as Y here
    pla(); // pull second copy of vertical speed from stack
    eor_imm(0xff); // invert bits to reverse speed
    SetupPlatformRope(); // do sub again to figure out where to put bg tiles  
    lda_zp(0x1); // write name table address to vram buffer
    write_byte(VRAM_Buffer1 + 5 + x, a); // this time we're doing putting tiles for
    lda_zp(0x0); // the other platform
    write_byte(VRAM_Buffer1 + 6 + x, a);
    lda_imm(0x2);
    write_byte(VRAM_Buffer1 + 7 + x, a); // set length again for 2 bytes
    pla(); // pull first copy of vertical speed from stack
    if (!neg_flag) { goto EraseR2; } // if moving upwards (note inversion earlier), skip this
    lda_imm(0xa2);
    write_byte(VRAM_Buffer1 + 8 + x, a); // otherwise put tile numbers for left
    lda_imm(0xa3); // and right sides of rope in vram
    write_byte(VRAM_Buffer1 + 9 + x, a); // transfer buffer
    goto EndRp; // jump to skip this part
  
EraseR2:
    lda_imm(0x24); // put blank tiles in vram buffer
    write_byte(VRAM_Buffer1 + 8 + x, a); // to erase rope
    write_byte(VRAM_Buffer1 + 9 + x, a);
  
EndRp:
    lda_imm(0x0); // put null terminator at the end
    write_byte(VRAM_Buffer1 + 10 + x, a);
    lda_abs(VRAM_Buffer1_Offset); // add ten bytes to the vram buffer offset
    carry_flag = false; // and store
    adc_imm(10);
    write_byte(VRAM_Buffer1_Offset, a);
  
ExitRp:
    ldx_zp(ObjectOffset); // get enemy object buffer offset and leave
    goto rts;
  
InitPlatformFall:
    tya(); // move offset of other platform from Y to X
    tax();
    jsr(GetEnemyOffscreenBits, 277); // get offscreen bits
    lda_imm(0x6);
    jsr(SetupFloateyNumber, 278); // award 1000 points to player
    lda_abs(Player_Rel_XPos);
    write_byte(FloateyNum_X_Pos + x, a); // put floatey number coordinates where player is
    lda_zp(Player_Y_Position);
    write_byte(FloateyNum_Y_Pos + x, a);
    lda_imm(0x1); // set moving direction as flag for
    write_byte(Enemy_MovingDir + x, a); // falling platforms
    StopPlatforms(); goto rts; // <fallthrough>
  
PlatformFall:
    tya(); // save offset for other platform to stack
    pha();
    jsr(MoveFallingPlatform, 279); // make current platform fall
    pla();
    tax(); // pull offset from stack and save to X
    jsr(MoveFallingPlatform, 280); // make other platform fall
    ldx_zp(ObjectOffset);
    lda_absx(PlatformCollisionFlag); // if player not standing on either platform,
    if (neg_flag) { goto ExPF; } // skip this part
    tax(); // transfer collision flag offset as offset to X
    jsr(PositionPlayerOnVPlat, 281); // and position player appropriately
  
ExPF:
    ldx_zp(ObjectOffset); // get enemy object buffer offset and leave
    goto rts;
    // --------------------------------
  
YMovingPlatform:
    lda_zpx(Enemy_Y_Speed); // if platform moving up or down, skip ahead to
    ora_absx(Enemy_Y_MoveForce); // check on other position
    if (!zero_flag) { goto ChkYCenterPos; }
    write_byte(Enemy_YMF_Dummy + x, a); // initialize dummy variable
    lda_zpx(Enemy_Y_Position);
    cmp_absx(YPlatformTopYPos); // if current vertical position => top position, branch
    if (carry_flag) { goto ChkYCenterPos; } // ahead of all this
    lda_zp(FrameCounter);
    and_imm(0b00000111); // check for every eighth frame
    if (!zero_flag) { goto SkipIY; }
    inc_zpx(Enemy_Y_Position); // increase vertical position every eighth frame
  
SkipIY:
    goto ChkYPCollision; // skip ahead to last part
  
ChkYCenterPos:
    lda_zpx(Enemy_Y_Position); // if current vertical position < central position, branch
    cmp_zpx(YPlatformCenterYPos); // to slow ascent/move downwards
    if (!carry_flag) { goto YMDown; }
    jsr(MovePlatformUp, 282); // otherwise start slowing descent/moving upwards
    goto ChkYPCollision;
  
YMDown:
    jsr(MovePlatformDown, 283); // start slowing ascent/moving downwards
  
ChkYPCollision:
    lda_absx(PlatformCollisionFlag); // if collision flag not set here, branch
    if (neg_flag) { goto ExYPl; } // to leave
    jsr(PositionPlayerOnVPlat, 284); // otherwise position player appropriately
  
ExYPl:
    goto rts; // leave
    // --------------------------------
    // $00 - used as adder to position player hotizontally
  
XMovingPlatform:
    lda_imm(0xe); // load preset maximum value for secondary counter
    jsr(XMoveCntr_Platform, 285); // do a sub to increment counters for movement
    jsr(MoveWithXMCntrs, 286); // do a sub to move platform accordingly, and return value
    lda_absx(PlatformCollisionFlag); // if no collision with player,
    if (neg_flag) { goto ExXMP; } // branch ahead to leave
  
PositionPlayerOnHPlat:
    lda_zp(Player_X_Position);
    carry_flag = false; // add saved value from second subroutine to
    adc_zp(0x0); // current player's position to position
    write_byte(Player_X_Position, a); // player accordingly in horizontal position
    lda_zp(Player_PageLoc); // get player's page location
    ldy_zp(0x0); // check to see if saved value here is positive or negative
    if (neg_flag) { goto PPHSubt; } // if negative, branch to subtract
    adc_imm(0x0); // otherwise add carry to page location
    goto SetPVar; // jump to skip subtraction
  
PPHSubt:
    sbc_imm(0x0); // subtract borrow from page location
  
SetPVar:
    write_byte(Player_PageLoc, a); // save result to player's page location
    write_byte(Platform_X_Scroll, y); // put saved value from second sub here to be used later
    jsr(PositionPlayerOnVPlat, 287); // position player vertically and appropriately
  
ExXMP:
    goto rts; // and we are done here
    // --------------------------------
  
DropPlatform:
    lda_absx(PlatformCollisionFlag); // if no collision between platform and player
    if (neg_flag) { goto ExDPl; } // occurred, just leave without moving anything
    jsr(MoveDropPlatform, 288); // otherwise do a sub to move platform down very quickly
    jsr(PositionPlayerOnVPlat, 289); // do a sub to position player appropriately
  
ExDPl:
    goto rts; // leave
    // --------------------------------
    // $00 - residual value from sub
  
RightPlatform:
    jsr(MoveEnemyHorizontally, 290); // move platform with current horizontal speed, if any
    write_byte(0x0, a); // store saved value here (residual code)
    lda_absx(PlatformCollisionFlag); // check collision flag, if no collision between player
    if (neg_flag) { goto ExRPl; } // and platform, branch ahead, leave speed unaltered
    lda_imm(0x10);
    write_byte(Enemy_X_Speed + x, a); // otherwise set new speed (gets moving if motionless)
    jsr(PositionPlayerOnHPlat, 291); // use saved value from earlier sub to position player
  
ExRPl:
    goto rts; // then leave
    // --------------------------------
  
MoveLargeLiftPlat:
    jsr(MoveLiftPlatforms, 292); // execute common to all large and small lift platforms
    goto ChkYPCollision; // branch to position player correctly
  
MoveSmallPlatform:
    jsr(MoveLiftPlatforms, 293); // execute common to all large and small lift platforms
    goto ChkSmallPlatCollision; // branch to position player correctly
  
MoveLiftPlatforms:
    lda_abs(TimerControl); // if master timer control set, skip all of this
    if (!zero_flag) { goto ExLiftP; } // and branch to leave
    lda_absx(Enemy_YMF_Dummy);
    carry_flag = false; // add contents of movement amount to whatever's here
    adc_absx(Enemy_Y_MoveForce);
    write_byte(Enemy_YMF_Dummy + x, a);
    lda_zpx(Enemy_Y_Position); // add whatever vertical speed is set to current
    adc_zpx(Enemy_Y_Speed); // vertical position plus carry to move up or down
    write_byte(Enemy_Y_Position + x, a); // and then leave
    goto rts;
  
ChkSmallPlatCollision:
    lda_absx(PlatformCollisionFlag); // get bounding box counter saved in collision flag
    if (zero_flag) { goto ExLiftP; } // if none found, leave player position alone
    jsr(PositionPlayerOnS_Plat, 294); // use to position player correctly
  
ExLiftP:
    goto rts; // then leave
    // -------------------------------------------------------------------------------------
    // $00 - page location of extended left boundary
    // $01 - extended left boundary position
    // $02 - page location of extended right boundary
    // $03 - extended right boundary position
    // -------------------------------------------------------------------------------------
    // some unused space
    //       .db $ff, $ff, $ff
    // -------------------------------------------------------------------------------------
    // $01 - enemy buffer offset
  
FireballEnemyCollision:
    lda_zpx(Fireball_State); // check to see if fireball state is set at all
    if (zero_flag) { goto ExitFBallEnemy; } // branch to leave if not
    asl_acc();
    if (carry_flag) { goto ExitFBallEnemy; } // branch to leave also if d7 in state is set
    lda_zp(FrameCounter);
    lsr_acc(); // get LSB of frame counter
    if (carry_flag) { goto ExitFBallEnemy; } // branch to leave if set (do routine every other frame)
    txa();
    asl_acc(); // multiply fireball offset by four
    asl_acc();
    carry_flag = false;
    adc_imm(0x1c); // then add $1c or 28 bytes to it
    tay(); // to use fireball's bounding box coordinates 
    ldx_imm(0x4);
  
FireballEnemyCDLoop:
    write_byte(0x1, x); // store enemy object offset here
    tya();
    pha(); // push fireball offset to the stack
    lda_zpx(Enemy_State);
    and_imm(0b00100000); // check to see if d5 is set in enemy state
    if (!zero_flag) { goto NoFToECol; } // if so, skip to next enemy slot
    lda_zpx(Enemy_Flag); // check to see if buffer flag is set
    if (zero_flag) { goto NoFToECol; } // if not, skip to next enemy slot
    lda_zpx(Enemy_ID); // check enemy identifier
    cmp_imm(0x24);
    if (!carry_flag) { goto GoombaDie; } // if < $24, branch to check further
    cmp_imm(0x2b);
    if (!carry_flag) { goto NoFToECol; } // if in range $24-$2a, skip to next enemy slot
  
GoombaDie:
    cmp_imm(Goomba); // check for goomba identifier
    if (!zero_flag) { goto NotGoomba; } // if not found, continue with code
    lda_zpx(Enemy_State); // otherwise check for defeated state
    cmp_imm(0x2); // if stomped or otherwise defeated,
    if (carry_flag) { goto NoFToECol; } // skip to next enemy slot
  
NotGoomba:
    lda_absx(EnemyOffscrBitsMasked); // if any masked offscreen bits set,
    if (!zero_flag) { goto NoFToECol; } // skip to next enemy slot
    txa();
    asl_acc(); // otherwise multiply enemy offset by four
    asl_acc();
    carry_flag = false;
    adc_imm(0x4); // add 4 bytes to it
    tax(); // to use enemy's bounding box coordinates
    jsr(SprObjectCollisionCore, 295); // do fireball-to-enemy collision detection
    ldx_zp(ObjectOffset); // return fireball's original offset
    if (!carry_flag) { goto NoFToECol; } // if carry clear, no collision, thus do next enemy slot
    lda_imm(0b10000000);
    write_byte(Fireball_State + x, a); // set d7 in enemy state
    ldx_zp(0x1); // get enemy offset
    jsr(HandleEnemyFBallCol, 296); // jump to handle fireball to enemy collision
  
NoFToECol:
    pla(); // pull fireball offset from stack
    tay(); // put it in Y
    ldx_zp(0x1); // get enemy object offset
    dex(); // decrement it
    if (!neg_flag) { goto FireballEnemyCDLoop; } // loop back until collision detection done on all enemies
  
ExitFBallEnemy:
    ldx_zp(ObjectOffset); // get original fireball offset and leave
    goto rts;
  
HandleEnemyFBallCol:
    jsr(RelativeEnemyPosition, 297); // get relative coordinate of enemy
    ldx_zp(0x1); // get current enemy object offset
    lda_zpx(Enemy_Flag); // check buffer flag for d7 set
    if (!neg_flag) { goto ChkBuzzyBeetle; } // branch if not set to continue
    and_imm(0b00001111); // otherwise mask out high nybble and
    tax(); // use low nybble as enemy offset
    lda_zpx(Enemy_ID);
    cmp_imm(Bowser); // check enemy identifier for bowser
    if (zero_flag) { goto HurtBowser; } // branch if found
    ldx_zp(0x1); // otherwise retrieve current enemy offset
  
ChkBuzzyBeetle:
    lda_zpx(Enemy_ID);
    cmp_imm(BuzzyBeetle); // check for buzzy beetle
    if (zero_flag) { goto ExHCF; } // branch if found to leave (buzzy beetles fireproof)
    cmp_imm(Bowser); // check for bowser one more time (necessary if d7 of flag was clear)
    if (!zero_flag) { goto ChkOtherEnemies; } // if not found, branch to check other enemies
  
HurtBowser:
    dec_abs(BowserHitPoints); // decrement bowser's hit points
    if (!zero_flag) { goto ExHCF; } // if bowser still has hit points, branch to leave
    InitVStf(); // otherwise do sub to init vertical speed and movement force
    write_byte(Enemy_X_Speed + x, a); // initialize horizontal speed
    write_byte(EnemyFrenzyBuffer, a); // init enemy frenzy buffer
    lda_imm(0xfe);
    write_byte(Enemy_Y_Speed + x, a); // set vertical speed to make defeated bowser jump a little
    ldy_abs(WorldNumber); // use world number as offset
    lda_absy(BowserIdentities); // get enemy identifier to replace bowser with
    write_byte(Enemy_ID + x, a); // set as new enemy identifier
    lda_imm(0x20); // set A to use starting value for state
    cpy_imm(0x3); // check to see if using offset of 3 or more
    if (carry_flag) { goto SetDBSte; } // branch if so
    ora_imm(0x3); // otherwise add 3 to enemy state
  
SetDBSte:
    write_byte(Enemy_State + x, a); // set defeated enemy state
    lda_imm(Sfx_BowserFall);
    write_byte(Square2SoundQueue, a); // load bowser defeat sound
    ldx_zp(0x1); // get enemy offset
    lda_imm(0x9); // award 5000 points to player for defeating bowser
    if (!zero_flag) { goto EnemySmackScore; } // unconditional branch to award points
  
ChkOtherEnemies:
    cmp_imm(BulletBill_FrenzyVar);
    if (zero_flag) { goto ExHCF; } // branch to leave if bullet bill (frenzy variant) 
    cmp_imm(Podoboo);
    if (zero_flag) { goto ExHCF; } // branch to leave if podoboo
    cmp_imm(0x15);
    if (carry_flag) { goto ExHCF; } // branch to leave if identifier => $15
  
ShellOrBlockDefeat:
    lda_zpx(Enemy_ID); // check for piranha plant
    cmp_imm(PiranhaPlant);
    if (!zero_flag) { goto StnE; } // branch if not found
    lda_zpx(Enemy_Y_Position);
    adc_imm(0x18); // add 24 pixels to enemy object's vertical position
    write_byte(Enemy_Y_Position + x, a);
  
StnE:
    jsr(ChkToStunEnemies, 298); // do yet another sub
    lda_zpx(Enemy_State);
    and_imm(0b00011111); // mask out 2 MSB of enemy object's state
    ora_imm(0b00100000); // set d5 to defeat enemy and save as new state
    write_byte(Enemy_State + x, a);
    lda_imm(0x2); // award 200 points by default
    ldy_zpx(Enemy_ID); // check for hammer bro
    cpy_imm(HammerBro);
    if (!zero_flag) { goto GoombaPoints; } // branch if not found
    lda_imm(0x6); // award 1000 points for hammer bro
  
GoombaPoints:
    cpy_imm(Goomba); // check for goomba
    if (!zero_flag) { goto EnemySmackScore; } // branch if not found
    lda_imm(0x1); // award 100 points for goomba
  
EnemySmackScore:
    jsr(SetupFloateyNumber, 299); // update necessary score variables
    lda_imm(Sfx_EnemySmack); // play smack enemy sound
    write_byte(Square1SoundQueue, a);
  
ExHCF:
    goto rts; // and now let's leave
    // -------------------------------------------------------------------------------------
  
PlayerHammerCollision:
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // shift d0 into carry
    if (!carry_flag) { goto ExPHC; } // branch to leave if d0 not set to execute every other frame
    lda_abs(TimerControl); // if either master timer control
    ora_abs(Misc_OffscreenBits); // or any offscreen bits for hammer are set,
    if (!zero_flag) { goto ExPHC; } // branch to leave
    txa();
    asl_acc(); // multiply misc object offset by four
    asl_acc();
    carry_flag = false;
    adc_imm(0x24); // add 36 or $24 bytes to get proper offset
    tay(); // for misc object bounding box coordinates
    jsr(PlayerCollisionCore, 300); // do player-to-hammer collision detection
    ldx_zp(ObjectOffset); // get misc object offset
    if (!carry_flag) { goto ClHCol; } // if no collision, then branch
    lda_absx(Misc_Collision_Flag); // otherwise read collision flag
    if (!zero_flag) { goto ExPHC; } // if collision flag already set, branch to leave
    lda_imm(0x1);
    write_byte(Misc_Collision_Flag + x, a); // otherwise set collision flag now
    lda_zpx(Misc_X_Speed);
    eor_imm(0xff); // get two's compliment of
    carry_flag = false; // hammer's horizontal speed
    adc_imm(0x1);
    write_byte(Misc_X_Speed + x, a); // set to send hammer flying the opposite direction
    lda_abs(StarInvincibleTimer); // if star mario invincibility timer set,
    if (!zero_flag) { goto ExPHC; } // branch to leave
    goto InjurePlayer; // otherwise jump to hurt player, do not return
  
ClHCol:
    lda_imm(0x0); // clear collision flag
    write_byte(Misc_Collision_Flag + x, a);
  
ExPHC:
    goto rts;
    // -------------------------------------------------------------------------------------
  
HandlePowerUpCollision:
    EraseEnemyObject(); // erase the power-up object
    lda_imm(0x6);
    jsr(SetupFloateyNumber, 301); // award 1000 points to player by default
    lda_imm(Sfx_PowerUpGrab);
    write_byte(Square2SoundQueue, a); // play the power-up sound
    lda_zp(PowerUpType); // check power-up type
    cmp_imm(0x2);
    if (!carry_flag) { goto Shroom_Flower_PUp; } // if mushroom or fire flower, branch
    cmp_imm(0x3);
    if (zero_flag) { goto SetFor1Up; } // if 1-up mushroom, branch
    lda_imm(0x23); // otherwise set star mario invincibility
    write_byte(StarInvincibleTimer, a); // timer, and load the star mario music
    lda_imm(StarPowerMusic); // into the area music queue, then leave
    write_byte(AreaMusicQueue, a);
    goto rts;
  
Shroom_Flower_PUp:
    lda_abs(PlayerStatus); // if player status = small, branch
    if (zero_flag) { goto UpToSuper; }
    cmp_imm(0x1); // if player status not super, leave
    if (!zero_flag) { goto NoPUp; }
    ldx_zp(ObjectOffset); // get enemy offset, not necessary
    lda_imm(0x2); // set player status to fiery
    write_byte(PlayerStatus, a);
    jsr(GetPlayerColors, 302); // run sub to change colors of player
    ldx_zp(ObjectOffset); // get enemy offset again, and again not necessary
    lda_imm(0xc); // set value to be used by subroutine tree (fiery)
    goto UpToFiery; // jump to set values accordingly
  
SetFor1Up:
    lda_imm(0xb); // change 1000 points into 1-up instead
    write_byte(FloateyNum_Control + x, a); // and then leave
    goto rts;
  
UpToSuper:
    lda_imm(0x1); // set player status to super
    write_byte(PlayerStatus, a);
    lda_imm(0x9); // set value to be used by subroutine tree (super)
  
UpToFiery:
    ldy_imm(0x0); // set value to be used as new player state
    jsr(SetPRout, 303); // set values to stop certain things in motion
  
NoPUp:
    goto rts;
    // --------------------------------
  
PlayerEnemyCollision:
    lda_zp(FrameCounter); // check counter for d0 set
    lsr_acc();
    if (carry_flag) { goto NoPUp; } // if set, branch to leave
    CheckPlayerVertical(); // if player object is completely offscreen or
    if (carry_flag) { goto NoPECol; } // if down past 224th pixel row, branch to leave
    lda_absx(EnemyOffscrBitsMasked); // if current enemy is offscreen by any amount,
    if (!zero_flag) { goto NoPECol; } // go ahead and branch to leave
    lda_zp(GameEngineSubroutine);
    cmp_imm(0x8); // if not set to run player control routine
    if (!zero_flag) { goto NoPECol; } // on next frame, branch to leave
    lda_zpx(Enemy_State);
    and_imm(0b00100000); // if enemy state has d5 set, branch to leave
    if (!zero_flag) { goto NoPECol; }
    jsr(GetEnemyBoundBoxOfs, 304); // get bounding box offset for current enemy object
    jsr(PlayerCollisionCore, 305); // do collision detection on player vs. enemy
    ldx_zp(ObjectOffset); // get enemy object buffer offset
    if (carry_flag) { goto CheckForPUpCollision; } // if collision, branch past this part here
    lda_absx(Enemy_CollisionBits);
    and_imm(0b11111110); // otherwise, clear d0 of current enemy object's
    write_byte(Enemy_CollisionBits + x, a); // collision bit
  
NoPECol:
    goto rts;
  
CheckForPUpCollision:
    ldy_zpx(Enemy_ID);
    cpy_imm(PowerUpObject); // check for power-up object
    if (!zero_flag) { goto EColl; } // if not found, branch to next part
    goto HandlePowerUpCollision; // otherwise, unconditional jump backwards
  
EColl:
    lda_abs(StarInvincibleTimer); // if star mario invincibility timer expired,
    if (zero_flag) { goto HandlePECollisions; } // perform task here, otherwise kill enemy like
    goto ShellOrBlockDefeat; // hit with a shell, or from beneath
  
HandlePECollisions:
    lda_absx(Enemy_CollisionBits); // check enemy collision bits for d0 set
    and_imm(0b00000001); // or for being offscreen at all
    ora_absx(EnemyOffscrBitsMasked);
    if (!zero_flag) { goto ExPEC; } // branch to leave if either is true
    lda_imm(0x1);
    ora_absx(Enemy_CollisionBits); // otherwise set d0 now
    write_byte(Enemy_CollisionBits + x, a);
    cpy_imm(Spiny); // branch if spiny
    if (zero_flag) { goto ChkForPlayerInjury; }
    cpy_imm(PiranhaPlant); // branch if piranha plant
    if (zero_flag) { goto InjurePlayer; }
    cpy_imm(Podoboo); // branch if podoboo
    if (zero_flag) { goto InjurePlayer; }
    cpy_imm(BulletBill_CannonVar); // branch if bullet bill
    if (zero_flag) { goto ChkForPlayerInjury; }
    cpy_imm(0x15); // branch if object => $15
    if (carry_flag) { goto InjurePlayer; }
    lda_abs(AreaType); // branch if water type level
    if (zero_flag) { goto InjurePlayer; }
    lda_zpx(Enemy_State); // branch if d7 of enemy state was set
    asl_acc();
    if (carry_flag) { goto ChkForPlayerInjury; }
    lda_zpx(Enemy_State); // mask out all but 3 LSB of enemy state
    and_imm(0b00000111);
    cmp_imm(0x2); // branch if enemy is in normal or falling state
    if (!carry_flag) { goto ChkForPlayerInjury; }
    lda_zpx(Enemy_ID); // branch to leave if goomba in defeated state
    cmp_imm(Goomba);
    if (zero_flag) { goto ExPEC; }
    lda_imm(Sfx_EnemySmack); // play smack enemy sound
    write_byte(Square1SoundQueue, a);
    lda_zpx(Enemy_State); // set d7 in enemy state, thus become moving shell
    ora_imm(0b10000000);
    write_byte(Enemy_State + x, a);
    EnemyFacePlayer(); // set moving direction and get offset
    lda_absy(KickedShellXSpdData); // load and set horizontal speed data with offset
    write_byte(Enemy_X_Speed + x, a);
    lda_imm(0x3); // add three to whatever the stomp counter contains
    carry_flag = false; // to give points for kicking the shell
    adc_abs(StompChainCounter);
    ldy_absx(EnemyIntervalTimer); // check shell enemy's timer
    cpy_imm(0x3); // if above a certain point, branch using the points
    if (carry_flag) { goto KSPts; } // data obtained from the stomp counter + 3
    lda_absy(KickedShellPtsData); // otherwise, set points based on proximity to timer expiration
  
KSPts:
    jsr(SetupFloateyNumber, 306); // set values for floatey number now
  
ExPEC:
    goto rts; // leave!!!
  
ChkForPlayerInjury:
    lda_zp(Player_Y_Speed); // check player's vertical speed
    if (neg_flag) { goto ChkInj; } // perform procedure below if player moving upwards
    if (!zero_flag) { goto EnemyStomped; } // or not at all, and branch elsewhere if moving downwards
  
ChkInj:
    lda_zpx(Enemy_ID); // branch if enemy object < $07
    cmp_imm(Bloober);
    if (!carry_flag) { goto ChkETmrs; }
    lda_zp(Player_Y_Position); // add 12 pixels to player's vertical position
    carry_flag = false;
    adc_imm(0xc);
    cmp_zpx(Enemy_Y_Position); // compare modified player's position to enemy's position
    if (!carry_flag) { goto EnemyStomped; } // branch if this player's position above (less than) enemy's
  
ChkETmrs:
    lda_abs(StompTimer); // check stomp timer
    if (!zero_flag) { goto EnemyStomped; } // branch if set
    lda_abs(InjuryTimer); // check to see if injured invincibility timer still
    if (!zero_flag) { goto ExInjColRoutines; } // counting down, and branch elsewhere to leave if so
    lda_abs(Player_Rel_XPos);
    cmp_abs(Enemy_Rel_XPos); // if player's relative position to the left of enemy's
    if (!carry_flag) { goto TInjE; } // relative position, branch here
    goto ChkEnemyFaceRight; // otherwise do a jump here
  
TInjE:
    lda_zpx(Enemy_MovingDir); // if enemy moving towards the left,
    cmp_imm(0x1); // branch, otherwise do a jump here
    if (!zero_flag) { goto InjurePlayer; } // to turn the enemy around
    goto LInj;
  
InjurePlayer:
    lda_abs(InjuryTimer); // check again to see if injured invincibility timer is
    if (!zero_flag) { goto ExInjColRoutines; } // at zero, and branch to leave if so
  
ForceInjury:
    ldx_abs(PlayerStatus); // check player's status
    if (zero_flag) { goto KillPlayer; } // branch if small
    write_byte(PlayerStatus, a); // otherwise set player's status to small
    lda_imm(0x8);
    write_byte(InjuryTimer, a); // set injured invincibility timer
    asl_acc();
    write_byte(Square1SoundQueue, a); // play pipedown/injury sound
    jsr(GetPlayerColors, 307); // change player's palette if necessary
    lda_imm(0xa); // set subroutine to run on next frame
  
SetKRout:
    ldy_imm(0x1); // set new player state
  
SetPRout:
    write_byte(GameEngineSubroutine, a); // load new value to run subroutine on next frame
    write_byte(Player_State, y); // store new player state
    ldy_imm(0xff);
    write_byte(TimerControl, y); // set master timer control flag to halt timers
    iny();
    write_byte(ScrollAmount, y); // initialize scroll speed
  
ExInjColRoutines:
    ldx_zp(ObjectOffset); // get enemy offset and leave
    goto rts;
  
KillPlayer:
    write_byte(Player_X_Speed, x); // halt player's horizontal movement by initializing speed
    inx();
    write_byte(EventMusicQueue, x); // set event music queue to death music
    lda_imm(0xfc);
    write_byte(Player_Y_Speed, a); // set new vertical speed
    lda_imm(0xb); // set subroutine to run on next frame
    if (!zero_flag) { goto SetKRout; } // branch to set player's state and other things
  
EnemyStomped:
    lda_zpx(Enemy_ID); // check for spiny, branch to hurt player
    cmp_imm(Spiny); // if found
    if (zero_flag) { goto InjurePlayer; }
    lda_imm(Sfx_EnemyStomp); // otherwise play stomp/swim sound
    write_byte(Square1SoundQueue, a);
    lda_zpx(Enemy_ID);
    ldy_imm(0x0); // initialize points data offset for stomped enemies
    cmp_imm(FlyingCheepCheep); // branch for cheep-cheep
    if (zero_flag) { goto EnemyStompedPts; }
    cmp_imm(BulletBill_FrenzyVar); // branch for either bullet bill object
    if (zero_flag) { goto EnemyStompedPts; }
    cmp_imm(BulletBill_CannonVar);
    if (zero_flag) { goto EnemyStompedPts; }
    cmp_imm(Podoboo); // branch for podoboo (this branch is logically impossible
    if (zero_flag) { goto EnemyStompedPts; } // for cpu to take due to earlier checking of podoboo)
    iny(); // increment points data offset
    cmp_imm(HammerBro); // branch for hammer bro
    if (zero_flag) { goto EnemyStompedPts; }
    iny(); // increment points data offset
    cmp_imm(Lakitu); // branch for lakitu
    if (zero_flag) { goto EnemyStompedPts; }
    iny(); // increment points data offset
    cmp_imm(Bloober); // branch if NOT bloober
    if (!zero_flag) { goto ChkForDemoteKoopa; }
  
EnemyStompedPts:
    lda_absy(StompedEnemyPtsData); // load points data using offset in Y
    jsr(SetupFloateyNumber, 308); // run sub to set floatey number controls
    lda_zpx(Enemy_MovingDir);
    pha(); // save enemy movement direction to stack
    jsr(SetStun, 309); // run sub to kill enemy
    pla();
    write_byte(Enemy_MovingDir + x, a); // return enemy movement direction from stack
    lda_imm(0b00100000);
    write_byte(Enemy_State + x, a); // set d5 in enemy state
    InitVStf(); // nullify vertical speed, physics-related thing,
    write_byte(Enemy_X_Speed + x, a); // and horizontal speed
    lda_imm(0xfd); // set player's vertical speed, to give bounce
    write_byte(Player_Y_Speed, a);
    goto rts;
  
ChkForDemoteKoopa:
    cmp_imm(0x9); // branch elsewhere if enemy object < $09
    if (!carry_flag) { goto HandleStompedShellE; }
    and_imm(0b00000001); // demote koopa paratroopas to ordinary troopas
    write_byte(Enemy_ID + x, a);
    ldy_imm(0x0); // return enemy to normal state
    write_byte(Enemy_State + x, y);
    lda_imm(0x3); // award 400 points to the player
    jsr(SetupFloateyNumber, 310);
    InitVStf(); // nullify physics-related thing and vertical speed
    EnemyFacePlayer(); // turn enemy around if necessary
    lda_absy(DemotedKoopaXSpdData);
    write_byte(Enemy_X_Speed + x, a); // set appropriate moving speed based on direction
    goto SBnce; // then move onto something else
  
HandleStompedShellE:
    lda_imm(0x4); // set defeated state for enemy
    write_byte(Enemy_State + x, a);
    inc_abs(StompChainCounter); // increment the stomp counter
    lda_abs(StompChainCounter); // add whatever is in the stomp counter
    carry_flag = false; // to whatever is in the stomp timer
    adc_abs(StompTimer);
    jsr(SetupFloateyNumber, 311); // award points accordingly
    inc_abs(StompTimer); // increment stomp timer of some sort
    ldy_abs(PrimaryHardMode); // check primary hard mode flag
    lda_absy(RevivalRateData); // load timer setting according to flag
    write_byte(EnemyIntervalTimer + x, a); // set as enemy timer to revive stomped enemy
  
SBnce:
    lda_imm(0xfc); // set player's vertical speed for bounce
    write_byte(Player_Y_Speed, a); // and then leave!!!
    goto rts;
  
ChkEnemyFaceRight:
    lda_zpx(Enemy_MovingDir); // check to see if enemy is moving to the right
    cmp_imm(0x1);
    if (!zero_flag) { goto LInj; } // if not, branch
    goto InjurePlayer; // otherwise go back to hurt player
  
LInj:
    jsr(EnemyTurnAround, 312); // turn the enemy around, if necessary
    goto InjurePlayer; // go back to hurt player
  
SetupFloateyNumber:
    write_byte(FloateyNum_Control + x, a); // set number of points control for floatey numbers
    lda_imm(0x30);
    write_byte(FloateyNum_Timer + x, a); // set timer for floatey numbers
    lda_zpx(Enemy_Y_Position);
    write_byte(FloateyNum_Y_Pos + x, a); // set vertical coordinate
    lda_abs(Enemy_Rel_XPos);
    write_byte(FloateyNum_X_Pos + x, a); // set horizontal coordinate and leave
  
ExSFN:
    goto rts;
    // -------------------------------------------------------------------------------------
    // $01 - used to hold enemy offset for second enemy
  
EnemiesCollision:
    lda_zp(FrameCounter); // check counter for d0 set
    lsr_acc();
    if (!carry_flag) { goto ExSFN; } // if d0 not set, leave
    lda_abs(AreaType);
    if (zero_flag) { goto ExSFN; } // if water area type, leave
    lda_zpx(Enemy_ID);
    cmp_imm(0x15); // if enemy object => $15, branch to leave
    if (carry_flag) { goto ExitECRoutine; }
    cmp_imm(Lakitu); // if lakitu, branch to leave
    if (zero_flag) { goto ExitECRoutine; }
    cmp_imm(PiranhaPlant); // if piranha plant, branch to leave
    if (zero_flag) { goto ExitECRoutine; }
    lda_absx(EnemyOffscrBitsMasked); // if masked offscreen bits nonzero, branch to leave
    if (!zero_flag) { goto ExitECRoutine; }
    jsr(GetEnemyBoundBoxOfs, 313); // otherwise, do sub, get appropriate bounding box offset for
    dex(); // first enemy we're going to compare, then decrement for second
    if (neg_flag) { goto ExitECRoutine; } // branch to leave if there are no other enemies
  
ECLoop:
    write_byte(0x1, x); // save enemy object buffer offset for second enemy here
    tya(); // save first enemy's bounding box offset to stack
    pha();
    lda_zpx(Enemy_Flag); // check enemy object enable flag
    if (zero_flag) { goto ReadyNextEnemy; } // branch if flag not set
    lda_zpx(Enemy_ID);
    cmp_imm(0x15); // check for enemy object => $15
    if (carry_flag) { goto ReadyNextEnemy; } // branch if true
    cmp_imm(Lakitu);
    if (zero_flag) { goto ReadyNextEnemy; } // branch if enemy object is lakitu
    cmp_imm(PiranhaPlant);
    if (zero_flag) { goto ReadyNextEnemy; } // branch if enemy object is piranha plant
    lda_absx(EnemyOffscrBitsMasked);
    if (!zero_flag) { goto ReadyNextEnemy; } // branch if masked offscreen bits set
    txa(); // get second enemy object's bounding box offset
    asl_acc(); // multiply by four, then add four
    asl_acc();
    carry_flag = false;
    adc_imm(0x4);
    tax(); // use as new contents of X
    jsr(SprObjectCollisionCore, 314); // do collision detection using the two enemies here
    ldx_zp(ObjectOffset); // use first enemy offset for X
    ldy_zp(0x1); // use second enemy offset for Y
    if (!carry_flag) { goto NoEnemyCollision; } // if carry clear, no collision, branch ahead of this
    lda_zpx(Enemy_State);
    ora_zpy(Enemy_State); // check both enemy states for d7 set
    and_imm(0b10000000);
    if (!zero_flag) { goto YesEC; } // branch if at least one of them is set
    lda_absy(Enemy_CollisionBits); // load first enemy's collision-related bits
    and_absx(SetBitsMask); // check to see if bit connected to second enemy is
    if (!zero_flag) { goto ReadyNextEnemy; } // already set, and move onto next enemy slot if set
    lda_absy(Enemy_CollisionBits);
    ora_absx(SetBitsMask); // if the bit is not set, set it now
    write_byte(Enemy_CollisionBits + y, a);
  
YesEC:
    jsr(ProcEnemyCollisions, 315); // react according to the nature of collision
    goto ReadyNextEnemy; // move onto next enemy slot
  
NoEnemyCollision:
    lda_absy(Enemy_CollisionBits); // load first enemy's collision-related bits
    and_absx(ClearBitsMask); // clear bit connected to second enemy
    write_byte(Enemy_CollisionBits + y, a); // then move onto next enemy slot
  
ReadyNextEnemy:
    pla(); // get first enemy's bounding box offset from the stack
    tay(); // use as Y again
    ldx_zp(0x1); // get and decrement second enemy's object buffer offset
    dex();
    if (!neg_flag) { goto ECLoop; } // loop until all enemy slots have been checked
  
ExitECRoutine:
    ldx_zp(ObjectOffset); // get enemy object buffer offset
    goto rts; // leave
  
ProcEnemyCollisions:
    lda_zpy(Enemy_State); // check both enemy states for d5 set
    ora_zpx(Enemy_State);
    and_imm(0b00100000); // if d5 is set in either state, or both, branch
    if (!zero_flag) { goto ExitProcessEColl; } // to leave and do nothing else at this point
    lda_zpx(Enemy_State);
    cmp_imm(0x6); // if second enemy state < $06, branch elsewhere
    if (!carry_flag) { goto ProcSecondEnemyColl; }
    lda_zpx(Enemy_ID); // check second enemy identifier for hammer bro
    cmp_imm(HammerBro); // if hammer bro found in alt state, branch to leave
    if (zero_flag) { goto ExitProcessEColl; }
    lda_zpy(Enemy_State); // check first enemy state for d7 set
    asl_acc();
    if (!carry_flag) { goto ShellCollisions; } // branch if d7 is clear
    lda_imm(0x6);
    jsr(SetupFloateyNumber, 316); // award 1000 points for killing enemy
    jsr(ShellOrBlockDefeat, 317); // then kill enemy, then load
    ldy_zp(0x1); // original offset of second enemy
  
ShellCollisions:
    tya(); // move Y to X
    tax();
    jsr(ShellOrBlockDefeat, 318); // kill second enemy
    ldx_zp(ObjectOffset);
    lda_absx(ShellChainCounter); // get chain counter for shell
    carry_flag = false;
    adc_imm(0x4); // add four to get appropriate point offset
    ldx_zp(0x1);
    jsr(SetupFloateyNumber, 319); // award appropriate number of points for second enemy
    ldx_zp(ObjectOffset); // load original offset of first enemy
    inc_absx(ShellChainCounter); // increment chain counter for additional enemies
  
ExitProcessEColl:
    goto rts; // leave!!!
  
ProcSecondEnemyColl:
    lda_zpy(Enemy_State); // if first enemy state < $06, branch elsewhere
    cmp_imm(0x6);
    if (!carry_flag) { goto MoveEOfs; }
    lda_zpy(Enemy_ID); // check first enemy identifier for hammer bro
    cmp_imm(HammerBro); // if hammer bro found in alt state, branch to leave
    if (zero_flag) { goto ExitProcessEColl; }
    jsr(ShellOrBlockDefeat, 320); // otherwise, kill first enemy
    ldy_zp(0x1);
    lda_absy(ShellChainCounter); // get chain counter for shell
    carry_flag = false;
    adc_imm(0x4); // add four to get appropriate point offset
    ldx_zp(ObjectOffset);
    jsr(SetupFloateyNumber, 321); // award appropriate number of points for first enemy
    ldx_zp(0x1); // load original offset of second enemy
    inc_absx(ShellChainCounter); // increment chain counter for additional enemies
    goto rts; // leave!!!
  
MoveEOfs:
    tya(); // move Y ($01) to X
    tax();
    jsr(EnemyTurnAround, 322); // do the sub here using value from $01
    ldx_zp(ObjectOffset); // then do it again using value from $08
  
EnemyTurnAround:
    lda_zpx(Enemy_ID); // check for specific enemies
    cmp_imm(PiranhaPlant);
    if (zero_flag) { goto ExTA; } // if piranha plant, leave
    cmp_imm(Lakitu);
    if (zero_flag) { goto ExTA; } // if lakitu, leave
    cmp_imm(HammerBro);
    if (zero_flag) { goto ExTA; } // if hammer bro, leave
    cmp_imm(Spiny);
    if (zero_flag) { goto RXSpd; } // if spiny, turn it around
    cmp_imm(GreenParatroopaJump);
    if (zero_flag) { goto RXSpd; } // if green paratroopa, turn it around
    cmp_imm(0x7);
    if (carry_flag) { goto ExTA; } // if any OTHER enemy object => $07, leave
  
RXSpd:
    lda_zpx(Enemy_X_Speed); // load horizontal speed
    eor_imm(0xff); // get two's compliment for horizontal speed
    tay();
    iny();
    write_byte(Enemy_X_Speed + x, y); // store as new horizontal speed
    lda_zpx(Enemy_MovingDir);
    eor_imm(0b00000011); // invert moving direction and store, then leave
    write_byte(Enemy_MovingDir + x, a); // thus effectively turning the enemy around
  
ExTA:
    goto rts; // leave!!!
    // -------------------------------------------------------------------------------------
    // $00 - vertical position of platform
  
LargePlatformCollision:
    lda_imm(0xff); // save value here
    write_byte(PlatformCollisionFlag + x, a);
    lda_abs(TimerControl); // check master timer control
    if (!zero_flag) { goto ExLPC; } // if set, branch to leave
    lda_zpx(Enemy_State); // if d7 set in object state,
    if (neg_flag) { goto ExLPC; } // branch to leave
    lda_zpx(Enemy_ID);
    cmp_imm(0x24); // check enemy object identifier for
    if (!zero_flag) { goto ChkForPlayerC_LargeP; } // balance platform, branch if not found
    lda_zpx(Enemy_State);
    tax(); // set state as enemy offset here
    jsr(ChkForPlayerC_LargeP, 323); // perform code with state offset, then original offset, in X
  
ChkForPlayerC_LargeP:
    CheckPlayerVertical(); // figure out if player is below a certain point
    if (carry_flag) { goto ExLPC; } // or offscreen, branch to leave if true
    txa();
    GetEnemyBoundBoxOfsArg(); // get bounding box offset in Y
    lda_zpx(Enemy_Y_Position); // store vertical coordinate in
    write_byte(0x0, a); // temp variable for now
    txa(); // send offset we're on to the stack
    pha();
    jsr(PlayerCollisionCore, 324); // do player-to-platform collision detection
    pla(); // retrieve offset from the stack
    tax();
    if (!carry_flag) { goto ExLPC; } // if no collision, branch to leave
    jsr(ProcLPlatCollisions, 325); // otherwise collision, perform sub
  
ExLPC:
    ldx_zp(ObjectOffset); // get enemy object buffer offset and leave
    goto rts;
    // --------------------------------
    // $00 - counter for bounding boxes
  
SmallPlatformCollision:
    lda_abs(TimerControl); // if master timer control set,
    if (!zero_flag) { goto ExSPC; } // branch to leave
    write_byte(PlatformCollisionFlag + x, a); // otherwise initialize collision flag
    CheckPlayerVertical(); // do a sub to see if player is below a certain point
    if (carry_flag) { goto ExSPC; } // or entirely offscreen, and branch to leave if true
    lda_imm(0x2);
    write_byte(0x0, a); // load counter here for 2 bounding boxes
  
ChkSmallPlatLoop:
    ldx_zp(ObjectOffset); // get enemy object offset
    jsr(GetEnemyBoundBoxOfs, 326); // get bounding box offset in Y
    and_imm(0b00000010); // if d1 of offscreen lower nybble bits was set
    if (!zero_flag) { goto ExSPC; } // then branch to leave
    lda_absy(BoundingBox_UL_YPos); // check top of platform's bounding box for being
    cmp_imm(0x20); // above a specific point
    if (!carry_flag) { goto MoveBoundBox; } // if so, branch, don't do collision detection
    jsr(PlayerCollisionCore, 327); // otherwise, perform player-to-platform collision detection
    if (carry_flag) { goto ProcSPlatCollisions; } // skip ahead if collision
  
MoveBoundBox:
    lda_absy(BoundingBox_UL_YPos); // move bounding box vertical coordinates
    carry_flag = false; // 128 pixels downwards
    adc_imm(0x80);
    write_byte(BoundingBox_UL_YPos + y, a);
    lda_absy(BoundingBox_DR_YPos);
    carry_flag = false;
    adc_imm(0x80);
    write_byte(BoundingBox_DR_YPos + y, a);
    dec_zp(0x0); // decrement counter we set earlier
    if (!zero_flag) { goto ChkSmallPlatLoop; } // loop back until both bounding boxes are checked
  
ExSPC:
    ldx_zp(ObjectOffset); // get enemy object buffer offset, then leave
    goto rts;
    // --------------------------------
  
ProcSPlatCollisions:
    ldx_zp(ObjectOffset); // return enemy object buffer offset to X, then continue
  
ProcLPlatCollisions:
    lda_absy(BoundingBox_DR_YPos); // get difference by subtracting the top
    carry_flag = true; // of the player's bounding box from the bottom
    sbc_abs(BoundingBox_UL_YPos); // of the platform's bounding box
    cmp_imm(0x4); // if difference too large or negative,
    if (carry_flag) { goto ChkForTopCollision; } // branch, do not alter vertical speed of player
    lda_zp(Player_Y_Speed); // check to see if player's vertical speed is moving down
    if (!neg_flag) { goto ChkForTopCollision; } // if so, don't mess with it
    lda_imm(0x1); // otherwise, set vertical
    write_byte(Player_Y_Speed, a); // speed of player to kill jump
  
ChkForTopCollision:
    lda_abs(BoundingBox_DR_YPos); // get difference by subtracting the top
    carry_flag = true; // of the platform's bounding box from the bottom
    sbc_absy(BoundingBox_UL_YPos); // of the player's bounding box
    cmp_imm(0x6);
    if (carry_flag) { goto PlatformSideCollisions; } // if difference not close enough, skip all of this
    lda_zp(Player_Y_Speed);
    if (neg_flag) { goto PlatformSideCollisions; } // if player's vertical speed moving upwards, skip this
    lda_zp(0x0); // get saved bounding box counter from earlier
    ldy_zpx(Enemy_ID);
    cpy_imm(0x2b); // if either of the two small platform objects are found,
    if (zero_flag) { goto SetCollisionFlag; } // regardless of which one, branch to use bounding box counter
    cpy_imm(0x2c); // as contents of collision flag
    if (zero_flag) { goto SetCollisionFlag; }
    txa(); // otherwise use enemy object buffer offset
  
SetCollisionFlag:
    ldx_zp(ObjectOffset); // get enemy object buffer offset
    write_byte(PlatformCollisionFlag + x, a); // save either bounding box counter or enemy offset here
    lda_imm(0x0);
    write_byte(Player_State, a); // set player state to normal then leave
    goto rts;
  
PlatformSideCollisions:
    lda_imm(0x1); // set value here to indicate possible horizontal
    write_byte(0x0, a); // collision on left side of platform
    lda_abs(BoundingBox_DR_XPos); // get difference by subtracting platform's left edge
    carry_flag = true; // from player's right edge
    sbc_absy(BoundingBox_UL_XPos);
    cmp_imm(0x8); // if difference close enough, skip all of this
    if (!carry_flag) { goto SideC; }
    inc_zp(0x0); // otherwise increment value set here for right side collision
    lda_absy(BoundingBox_DR_XPos); // get difference by subtracting player's left edge
    carry_flag = false; // from platform's right edge
    sbc_abs(BoundingBox_UL_XPos);
    cmp_imm(0x9); // if difference not close enough, skip subroutine
    if (carry_flag) { goto NoSideC; } // and instead branch to leave (no collision)
  
SideC:
    ImpedePlayerMove(); // deal with horizontal collision
  
NoSideC:
    ldx_zp(ObjectOffset); // return with enemy object buffer offset
    goto rts;
    // -------------------------------------------------------------------------------------
  
PositionPlayerOnS_Plat:
    tay(); // use bounding box counter saved in collision flag
    lda_zpx(Enemy_Y_Position); // for offset
    carry_flag = false; // add positioning data using offset to the vertical
    adc_absy(PlayerPosSPlatData - 1); // coordinate
    goto PositionPlayerOnVPlatSkip; //  .db $2c ;BIT instruction opcode
  
PositionPlayerOnVPlat:
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
  
PositionPlayerOnVPlatSkip:
    ldy_zp(GameEngineSubroutine);
    cpy_imm(0xb); // if certain routine being executed on this frame,
    if (zero_flag) { goto ExPlPos; } // skip all of this
    ldy_zpx(Enemy_Y_HighPos);
    cpy_imm(0x1); // if vertical high byte offscreen, skip this
    if (!zero_flag) { goto ExPlPos; }
    carry_flag = true; // subtract 32 pixels from vertical coordinate
    sbc_imm(0x20); // for the player object's height
    write_byte(Player_Y_Position, a); // save as player's new vertical coordinate
    tya();
    sbc_imm(0x0); // subtract borrow and store as player's
    write_byte(Player_Y_HighPos, a); // new vertical high byte
    lda_imm(0x0);
    write_byte(Player_Y_Speed, a); // initialize vertical speed and low byte of force
    write_byte(Player_Y_MoveForce, a); // and then leave
  
ExPlPos:
    goto rts;
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
  
GetEnemyBoundBoxOfs:
    lda_zp(ObjectOffset); // get enemy object buffer offset
    GetEnemyBoundBoxOfsArg(); goto rts; // <fallthrough>
  
PlayerBGCollision:
    lda_abs(DisableCollisionDet); // if collision detection disabled flag set,
    if (!zero_flag) { goto ExPBGCol; } // branch to leave
    lda_zp(GameEngineSubroutine);
    cmp_imm(0xb); // if running routine #11 or $0b
    if (zero_flag) { goto ExPBGCol; } // branch to leave
    cmp_imm(0x4);
    if (!carry_flag) { goto ExPBGCol; } // if running routines $00-$03 branch to leave
    lda_imm(0x1); // load default player state for swimming
    ldy_abs(SwimmingFlag); // if swimming flag set,
    if (!zero_flag) { goto SetPSte; } // branch ahead to set default state
    lda_zp(Player_State); // if player in normal state,
    if (zero_flag) { goto SetFallS; } // branch to set default state for falling
    cmp_imm(0x3);
    if (!zero_flag) { goto ChkOnScr; } // if in any other state besides climbing, skip to next part
  
SetFallS:
    lda_imm(0x2); // load default player state for falling
  
SetPSte:
    write_byte(Player_State, a); // set whatever player state is appropriate
  
ChkOnScr:
    lda_zp(Player_Y_HighPos);
    cmp_imm(0x1); // check player's vertical high byte for still on the screen
    if (!zero_flag) { goto ExPBGCol; } // branch to leave if not
    lda_imm(0xff);
    write_byte(Player_CollisionBits, a); // initialize player's collision flag
    lda_zp(Player_Y_Position);
    cmp_imm(0xcf); // check player's vertical coordinate
    if (!carry_flag) { goto ChkCollSize; } // if not too close to the bottom of screen, continue
  
ExPBGCol:
    goto rts; // otherwise leave
  
ChkCollSize:
    ldy_imm(0x2); // load default offset
    lda_abs(CrouchingFlag);
    if (!zero_flag) { goto GBBAdr; } // if player crouching, skip ahead
    lda_abs(PlayerSize);
    if (!zero_flag) { goto GBBAdr; } // if player small, skip ahead
    dey(); // otherwise decrement offset for big player not crouching
    lda_abs(SwimmingFlag);
    if (!zero_flag) { goto GBBAdr; } // if swimming flag set, skip ahead
    dey(); // otherwise decrement offset
  
GBBAdr:
    lda_absy(BlockBufferAdderData); // get value using offset
    write_byte(0xeb, a); // store value here
    tay(); // put value into Y, as offset for block buffer routine
    ldx_abs(PlayerSize); // get player's size as offset
    lda_abs(CrouchingFlag);
    if (zero_flag) { goto HeadChk; } // if player not crouching, branch ahead
    inx(); // otherwise increment size as offset
  
HeadChk:
    lda_zp(Player_Y_Position); // get player's vertical coordinate
    cmp_absx(PlayerBGUpperExtent); // compare with upper extent value based on offset
    if (!carry_flag) { goto DoFootCheck; } // if player is too high, skip this part
    jsr(BlockBufferColli_Head, 328); // do player-to-bg collision detection on top of
    if (zero_flag) { goto DoFootCheck; } // player, and branch if nothing above player's head
    jsr(CheckForCoinMTiles, 329); // check to see if player touched coin with their head
    if (carry_flag) { goto AwardTouchedCoin; } // if so, branch to some other part of code
    ldy_zp(Player_Y_Speed); // check player's vertical speed
    if (!neg_flag) { goto DoFootCheck; } // if player not moving upwards, branch elsewhere
    ldy_zp(0x4); // check lower nybble of vertical coordinate returned
    cpy_imm(0x4); // from collision detection routine
    if (!carry_flag) { goto DoFootCheck; } // if low nybble < 4, branch
    jsr(CheckForSolidMTiles, 330); // check to see what player's head bumped on
    if (carry_flag) { goto SolidOrClimb; } // if player collided with solid metatile, branch
    ldy_abs(AreaType); // otherwise check area type
    if (zero_flag) { goto NYSpd; } // if water level, branch ahead
    ldy_abs(BlockBounceTimer); // if block bounce timer not expired,
    if (!zero_flag) { goto NYSpd; } // branch ahead, do not process collision
    jsr(PlayerHeadCollision, 331); // otherwise do a sub to process collision
    goto DoFootCheck; // jump ahead to skip these other parts here
  
SolidOrClimb:
    cmp_imm(0x26); // if climbing metatile,
    if (zero_flag) { goto NYSpd; } // branch ahead and do not play sound
    lda_imm(Sfx_Bump);
    write_byte(Square1SoundQueue, a); // otherwise load bump sound
  
NYSpd:
    lda_imm(0x1); // set player's vertical speed to nullify
    write_byte(Player_Y_Speed, a); // jump or swim
  
DoFootCheck:
    ldy_zp(0xeb); // get block buffer adder offset
    lda_zp(Player_Y_Position);
    cmp_imm(0xcf); // check to see how low player is
    if (carry_flag) { goto DoPlayerSideCheck; } // if player is too far down on screen, skip all of this
    jsr(BlockBufferColli_Feet, 332); // do player-to-bg collision detection on bottom left of player
    jsr(CheckForCoinMTiles, 333); // check to see if player touched coin with their left foot
    if (carry_flag) { goto AwardTouchedCoin; } // if so, branch to some other part of code
    pha(); // save bottom left metatile to stack
    jsr(BlockBufferColli_Feet, 334); // do player-to-bg collision detection on bottom right of player
    write_byte(0x0, a); // save bottom right metatile here
    pla();
    write_byte(0x1, a); // pull bottom left metatile and save here
    if (!zero_flag) { goto ChkFootMTile; } // if anything here, skip this part
    lda_zp(0x0); // otherwise check for anything in bottom right metatile
    if (zero_flag) { goto DoPlayerSideCheck; } // and skip ahead if not
    jsr(CheckForCoinMTiles, 335); // check to see if player touched coin with their right foot
    if (!carry_flag) { goto ChkFootMTile; } // if not, skip unconditional jump and continue code
  
AwardTouchedCoin:
    goto HandleCoinMetatile; // follow the code to erase coin and award to player 1 coin
  
ChkFootMTile:
    jsr(CheckForClimbMTiles, 336); // check to see if player landed on climbable metatiles
    if (carry_flag) { goto DoPlayerSideCheck; } // if so, branch
    ldy_zp(Player_Y_Speed); // check player's vertical speed
    if (neg_flag) { goto DoPlayerSideCheck; } // if player moving upwards, branch
    cmp_imm(0xc5);
    if (!zero_flag) { goto ContChk; } // if player did not touch axe, skip ahead
    goto HandleAxeMetatile; // otherwise jump to set modes of operation
  
ContChk:
    ChkInvisibleMTiles(); // do sub to check for hidden coin or 1-up blocks
    if (zero_flag) { goto DoPlayerSideCheck; } // if either found, branch
    ldy_abs(JumpspringAnimCtrl); // if jumpspring animating right now,
    if (!zero_flag) { goto InitSteP; } // branch ahead
    ldy_zp(0x4); // check lower nybble of vertical coordinate returned
    cpy_imm(0x5); // from collision detection routine
    if (!carry_flag) { goto LandPlyr; } // if lower nybble < 5, branch
    lda_zp(Player_MovingDir);
    write_byte(0x0, a); // use player's moving direction as temp variable
    ImpedePlayerMove(); goto rts; // jump to impede player's movement in that direction
  
LandPlyr:
    ChkForLandJumpSpring(); // do sub to check for jumpspring metatiles and deal with it
    lda_imm(0xf0);
    and_zp(Player_Y_Position); // mask out lower nybble of player's vertical position
    write_byte(Player_Y_Position, a); // and store as new vertical position to land player properly
    HandlePipeEntry(); // do sub to process potential pipe entry
    lda_imm(0x0);
    write_byte(Player_Y_Speed, a); // initialize vertical speed and fractional
    write_byte(Player_Y_MoveForce, a); // movement force to stop player's vertical movement
    write_byte(StompChainCounter, a); // initialize enemy stomp counter
  
InitSteP:
    lda_imm(0x0);
    write_byte(Player_State, a); // set player's state to normal
  
DoPlayerSideCheck:
    ldy_zp(0xeb); // get block buffer adder offset
    iny();
    iny(); // increment offset 2 bytes to use adders for side collisions
    lda_imm(0x2); // set value here to be used as counter
    write_byte(0x0, a);
  
SideCheckLoop:
    iny(); // move onto the next one
    write_byte(0xeb, y); // store it
    lda_zp(Player_Y_Position);
    cmp_imm(0x20); // check player's vertical position
    if (!carry_flag) { goto BHalf; } // if player is in status bar area, branch ahead to skip this part
    cmp_imm(0xe4);
    if (carry_flag) { goto ExSCH; } // branch to leave if player is too far down
    jsr(BlockBufferColli_Side, 337); // do player-to-bg collision detection on one half of player
    if (zero_flag) { goto BHalf; } // branch ahead if nothing found
    cmp_imm(0x1c); // otherwise check for pipe metatiles
    if (zero_flag) { goto BHalf; } // if collided with sideways pipe (top), branch ahead
    cmp_imm(0x6b);
    if (zero_flag) { goto BHalf; } // if collided with water pipe (top), branch ahead
    jsr(CheckForClimbMTiles, 338); // do sub to see if player bumped into anything climbable
    if (!carry_flag) { goto CheckSideMTiles; } // if not, branch to alternate section of code
  
BHalf:
    ldy_zp(0xeb); // load block adder offset
    iny(); // increment it
    lda_zp(Player_Y_Position); // get player's vertical position
    cmp_imm(0x8);
    if (!carry_flag) { goto ExSCH; } // if too high, branch to leave
    cmp_imm(0xd0);
    if (carry_flag) { goto ExSCH; } // if too low, branch to leave
    jsr(BlockBufferColli_Side, 339); // do player-to-bg collision detection on other half of player
    if (!zero_flag) { goto CheckSideMTiles; } // if something found, branch
    dec_zp(0x0); // otherwise decrement counter
    if (!zero_flag) { goto SideCheckLoop; } // run code until both sides of player are checked
  
ExSCH:
    goto rts; // leave
  
CheckSideMTiles:
    ChkInvisibleMTiles(); // check for hidden or coin 1-up blocks
    if (zero_flag) { goto ExCSM; } // branch to leave if either found
    jsr(CheckForClimbMTiles, 340); // check for climbable metatiles
    if (!carry_flag) { goto ContSChk; } // if not found, skip and continue with code
    goto HandleClimbing; // otherwise jump to handle climbing
  
ContSChk:
    jsr(CheckForCoinMTiles, 341); // check to see if player touched coin
    if (carry_flag) { goto HandleCoinMetatile; } // if so, execute code to erase coin and award to player 1 coin
    ChkJumpspringMetatiles(); // check for jumpspring metatiles
    if (!carry_flag) { goto ChkPBtm; } // if not found, branch ahead to continue cude
    lda_abs(JumpspringAnimCtrl); // otherwise check jumpspring animation control
    if (!zero_flag) { goto ExCSM; } // branch to leave if set
    goto StopPlayerMove; // otherwise jump to impede player's movement
  
ChkPBtm:
    ldy_zp(Player_State); // get player's state
    cpy_imm(0x0); // check for player's state set to normal
    if (!zero_flag) { goto StopPlayerMove; } // if not, branch to impede player's movement
    ldy_zp(PlayerFacingDir); // get player's facing direction
    dey();
    if (!zero_flag) { goto StopPlayerMove; } // if facing left, branch to impede movement
    cmp_imm(0x6c); // otherwise check for pipe metatiles
    if (zero_flag) { goto PipeDwnS; } // if collided with sideways pipe (bottom), branch
    cmp_imm(0x1f); // if collided with water pipe (bottom), continue
    if (!zero_flag) { goto StopPlayerMove; } // otherwise branch to impede player's movement
  
PipeDwnS:
    lda_abs(Player_SprAttrib); // check player's attributes
    if (!zero_flag) { goto PlyrPipe; } // if already set, branch, do not play sound again
    ldy_imm(Sfx_PipeDown_Injury);
    write_byte(Square1SoundQueue, y); // otherwise load pipedown/injury sound
  
PlyrPipe:
    ora_imm(0b00100000);
    write_byte(Player_SprAttrib, a); // set background priority bit in player attributes
    lda_zp(Player_X_Position);
    and_imm(0b00001111); // get lower nybble of player's horizontal coordinate
    if (zero_flag) { goto ChkGERtn; } // if at zero, branch ahead to skip this part
    ldy_imm(0x0); // set default offset for timer setting data
    lda_abs(ScreenLeft_PageLoc); // load page location for left side of screen
    if (zero_flag) { goto SetCATmr; } // if at page zero, use default offset
    iny(); // otherwise increment offset
  
SetCATmr:
    lda_absy(AreaChangeTimerData); // set timer for change of area as appropriate
    write_byte(ChangeAreaTimer, a);
  
ChkGERtn:
    lda_zp(GameEngineSubroutine); // get number of game engine routine running
    cmp_imm(0x7);
    if (zero_flag) { goto ExCSM; } // if running player entrance routine or
    cmp_imm(0x8); // player control routine, go ahead and branch to leave
    if (!zero_flag) { goto ExCSM; }
    lda_imm(0x2);
    write_byte(GameEngineSubroutine, a); // otherwise set sideways pipe entry routine to run
    goto rts; // and leave
    // --------------------------------
    // $02 - high nybble of vertical coordinate from block buffer
    // $04 - low nybble of horizontal coordinate from block buffer
    // $06-$07 - block buffer address
  
StopPlayerMove:
    ImpedePlayerMove(); // stop player's movement
  
ExCSM:
    goto rts; // leave
  
HandleCoinMetatile:
    jsr(ErACM, 342); // do sub to erase coin metatile from block buffer
    inc_abs(CoinTallyFor1Ups); // increment coin tally used for 1-up blocks
    goto GiveOneCoin; // update coin amount and tally on the screen
  
HandleAxeMetatile:
    lda_imm(0x0);
    write_byte(OperMode_Task, a); // reset secondary mode
    lda_imm(0x2);
    write_byte(OperMode, a); // set primary mode to autoctrl mode
    lda_imm(0x18);
    write_byte(Player_X_Speed, a); // set horizontal speed and continue to erase axe metatile
  
ErACM:
    ldy_zp(0x2); // load vertical high nybble offset for block buffer
    lda_imm(0x0); // load blank metatile
    write_byte(read_word(0x6) + y, a); // store to remove old contents from block buffer
    goto RemoveCoin_Axe; // update the screen accordingly
    // --------------------------------
    // $02 - high nybble of vertical coordinate from block buffer
    // $04 - low nybble of horizontal coordinate from block buffer
    // $06-$07 - block buffer address
  
HandleClimbing:
    ldy_zp(0x4); // check low nybble of horizontal coordinate returned from
    cpy_imm(0x6); // collision detection routine against certain values, this
    if (!carry_flag) { goto ExHC; } // makes actual physical part of vine or flagpole thinner
    cpy_imm(0xa); // than 16 pixels
    if (!carry_flag) { goto ChkForFlagpole; }
  
ExHC:
    goto rts; // leave if too far left or too far right
  
ChkForFlagpole:
    cmp_imm(0x24); // check climbing metatiles
    if (zero_flag) { goto FlagpoleCollision; } // branch if flagpole ball found
    cmp_imm(0x25);
    if (!zero_flag) { goto VineCollision; } // branch to alternate code if flagpole shaft not found
  
FlagpoleCollision:
    lda_zp(GameEngineSubroutine);
    cmp_imm(0x5); // check for end-of-level routine running
    if (zero_flag) { goto PutPlayerOnVine; } // if running, branch to end of climbing code
    lda_imm(0x1);
    write_byte(PlayerFacingDir, a); // set player's facing direction to right
    inc_abs(ScrollLock); // set scroll lock flag
    lda_zp(GameEngineSubroutine);
    cmp_imm(0x4); // check for flagpole slide routine running
    if (zero_flag) { goto RunFR; } // if running, branch to end of flagpole code here
    lda_imm(BulletBill_CannonVar); // load identifier for bullet bills (cannon variant)
    KillEnemies(); // get rid of them
    lda_imm(Silence);
    write_byte(EventMusicQueue, a); // silence music
    lsr_acc();
    write_byte(FlagpoleSoundQueue, a); // load flagpole sound into flagpole sound queue
    ldx_imm(0x4); // start at end of vertical coordinate data
    lda_zp(Player_Y_Position);
    write_byte(FlagpoleCollisionYPos, a); // store player's vertical coordinate here to be used later
  
ChkFlagpoleYPosLoop:
    cmp_absx(FlagpoleYPosData); // compare with current vertical coordinate data
    if (carry_flag) { goto MtchF; } // if player's => current, branch to use current offset
    dex(); // otherwise decrement offset to use 
    if (!zero_flag) { goto ChkFlagpoleYPosLoop; } // do this until all data is checked (use last one if all checked)
  
MtchF:
    write_byte(FlagpoleScore, x); // store offset here to be used later
  
RunFR:
    lda_imm(0x4);
    write_byte(GameEngineSubroutine, a); // set value to run flagpole slide routine
    goto PutPlayerOnVine; // jump to end of climbing code
  
VineCollision:
    cmp_imm(0x26); // check for climbing metatile used on vines
    if (!zero_flag) { goto PutPlayerOnVine; }
    lda_zp(Player_Y_Position); // check player's vertical coordinate
    cmp_imm(0x20); // for being in status bar area
    if (carry_flag) { goto PutPlayerOnVine; } // branch if not that far up
    lda_imm(0x1);
    write_byte(GameEngineSubroutine, a); // otherwise set to run autoclimb routine next frame
  
PutPlayerOnVine:
    lda_imm(0x3); // set player state to climbing
    write_byte(Player_State, a);
    lda_imm(0x0); // nullify player's horizontal speed
    write_byte(Player_X_Speed, a); // and fractional horizontal movement force
    write_byte(Player_X_MoveForce, a);
    lda_zp(Player_X_Position); // get player's horizontal coordinate
    carry_flag = true;
    sbc_abs(ScreenLeft_X_Pos); // subtract from left side horizontal coordinate
    cmp_imm(0x10);
    if (carry_flag) { goto SetVXPl; } // if 16 or more pixels difference, do not alter facing direction
    lda_imm(0x2);
    write_byte(PlayerFacingDir, a); // otherwise force player to face left
  
SetVXPl:
    ldy_zp(PlayerFacingDir); // get current facing direction, use as offset
    lda_zp(0x6); // get low byte of block buffer address
    asl_acc();
    asl_acc(); // move low nybble to high
    asl_acc();
    asl_acc();
    carry_flag = false;
    adc_absy(ClimbXPosAdder - 1); // add pixels depending on facing direction
    write_byte(Player_X_Position, a); // store as player's horizontal coordinate
    lda_zp(0x6); // get low byte of block buffer address again
    if (!zero_flag) { goto ExPVne; } // if not zero, branch
    lda_abs(ScreenRight_PageLoc); // load page location of right side of screen
    carry_flag = false;
    adc_absy(ClimbPLocAdder - 1); // add depending on facing location
    write_byte(Player_PageLoc, a); // store as player's page location
  
ExPVne:
    goto rts; // finally, we're done!
    // --------------------------------
    // --------------------------------
    // $00-$01 - used to hold bottom right and bottom left metatiles (in that order)
    // $00 - used as flag by ImpedePlayerMove to restrict specific movement
    // --------------------------------
  
CheckForSolidMTiles:
    jsr(GetMTileAttrib, 343); // find appropriate offset based on metatile's 2 MSB
    cmp_absx(SolidMTileUpperExt); // compare current metatile with solid metatiles
    goto rts;
  
CheckForClimbMTiles:
    jsr(GetMTileAttrib, 344); // find appropriate offset based on metatile's 2 MSB
    cmp_absx(ClimbMTileUpperExt); // compare current metatile with climbable metatiles
    goto rts;
  
CheckForCoinMTiles:
    cmp_imm(0xc2); // check for regular coin
    if (zero_flag) { goto CoinSd; } // branch if found
    cmp_imm(0xc3); // check for underwater coin
    if (zero_flag) { goto CoinSd; } // branch if found
    carry_flag = false; // otherwise clear carry and leave
    goto rts;
  
CoinSd:
    lda_imm(Sfx_CoinGrab);
    write_byte(Square2SoundQueue, a); // load coin grab sound and leave
    goto rts;
  
GetMTileAttrib:
    tay(); // save metatile value into Y
    and_imm(0b11000000); // mask out all but 2 MSB
    asl_acc();
    rol_acc(); // shift and rotate d7-d6 to d1-d0
    rol_acc();
    tax(); // use as offset for metatile data
    tya(); // get original metatile value back
  
ExEBG:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // $06-$07 - address from block buffer routine
  
EnemyToBGCollisionDet:
    lda_zpx(Enemy_State); // check enemy state for d6 set
    and_imm(0b00100000);
    if (!zero_flag) { goto ExEBG; } // if set, branch to leave
    jsr(SubtEnemyYPos, 345); // otherwise, do a subroutine here
    if (!carry_flag) { goto ExEBG; } // if enemy vertical coord + 62 < 68, branch to leave
    ldy_zpx(Enemy_ID);
    cpy_imm(Spiny); // if enemy object is not spiny, branch elsewhere
    if (!zero_flag) { goto DoIDCheckBGColl; }
    lda_zpx(Enemy_Y_Position);
    cmp_imm(0x25); // if enemy vertical coordinate < 36 branch to leave
    if (!carry_flag) { goto ExEBG; }
  
DoIDCheckBGColl:
    cpy_imm(GreenParatroopaJump); // check for some other enemy object
    if (!zero_flag) { goto HBChk; } // branch if not found
    goto EnemyJump; // otherwise jump elsewhere
  
HBChk:
    cpy_imm(HammerBro); // check for hammer bro
    if (!zero_flag) { goto CInvu; } // branch if not found
    goto HammerBroBGColl; // otherwise jump elsewhere
  
CInvu:
    cpy_imm(Spiny); // if enemy object is spiny, branch
    if (zero_flag) { goto YesIn; }
    cpy_imm(PowerUpObject); // if special power-up object, branch
    if (zero_flag) { goto YesIn; }
    cpy_imm(0x7); // if enemy object =>$07, branch to leave
    if (carry_flag) { goto ExEBGChk; }
  
YesIn:
    jsr(ChkUnderEnemy, 346); // if enemy object < $07, or = $12 or $2e, do this sub
    if (!zero_flag) { goto HandleEToBGCollision; } // if block underneath enemy, branch
  
NoEToBGCollision:
    goto ChkForRedKoopa; // otherwise skip and do something else
    // --------------------------------
    // $02 - vertical coordinate from block buffer routine
  
HandleEToBGCollision:
    jsr(ChkForNonSolids, 347); // if something is underneath enemy, find out what
    if (zero_flag) { goto NoEToBGCollision; } // if blank $26, coins, or hidden blocks, jump, enemy falls through
    cmp_imm(0x23);
    if (!zero_flag) { goto LandEnemyProperly; } // check for blank metatile $23 and branch if not found
    ldy_zp(0x2); // get vertical coordinate used to find block
    lda_imm(0x0); // store default blank metatile in that spot so we won't
    write_byte(read_word(0x6) + y, a); // trigger this routine accidentally again
    lda_zpx(Enemy_ID);
    cmp_imm(0x15); // if enemy object => $15, branch ahead
    if (carry_flag) { goto ChkToStunEnemies; }
    cmp_imm(Goomba); // if enemy object not goomba, branch ahead of this routine
    if (!zero_flag) { goto GiveOEPoints; }
    jsr(KillEnemyAboveBlock, 348); // if enemy object IS goomba, do this sub
  
GiveOEPoints:
    lda_imm(0x1); // award 100 points for hitting block beneath enemy
    jsr(SetupFloateyNumber, 349);
  
ChkToStunEnemies:
    cmp_imm(0x9); // perform many comparisons on enemy object identifier
    if (!carry_flag) { goto SetStun; }
    cmp_imm(0x11); // if the enemy object identifier is equal to the values
    if (carry_flag) { goto SetStun; } // $09, $0e, $0f or $10, it will be modified, and not
    cmp_imm(0xa); // modified if not any of those values, note that piranha plant will
    if (!carry_flag) { goto Demote; } // always fail this test because A will still have vertical
    cmp_imm(PiranhaPlant); // coordinate from previous addition, also these comparisons
    if (!carry_flag) { goto SetStun; } // are only necessary if branching from $d7a1
  
Demote:
    and_imm(0b00000001); // erase all but LSB, essentially turning enemy object
    write_byte(Enemy_ID + x, a); // into green or red koopa troopa to demote them
  
SetStun:
    lda_zpx(Enemy_State); // load enemy state
    and_imm(0b11110000); // save high nybble
    ora_imm(0b00000010);
    write_byte(Enemy_State + x, a); // set d1 of enemy state
    dec_zpx(Enemy_Y_Position);
    dec_zpx(Enemy_Y_Position); // subtract two pixels from enemy's vertical position
    lda_zpx(Enemy_ID);
    cmp_imm(Bloober); // check for bloober object
    if (zero_flag) { goto SetWYSpd; }
    lda_imm(0xfd); // set default vertical speed
    ldy_abs(AreaType);
    if (!zero_flag) { goto SetNotW; } // if area type not water, set as speed, otherwise
  
SetWYSpd:
    lda_imm(0xff); // change the vertical speed
  
SetNotW:
    write_byte(Enemy_Y_Speed + x, a); // set vertical speed now
    ldy_imm(0x1);
    PlayerEnemyDiff(); // get horizontal difference between player and enemy object
    if (!neg_flag) { goto ChkBBill; } // branch if enemy is to the right of player
    iny(); // increment Y if not
  
ChkBBill:
    lda_zpx(Enemy_ID);
    cmp_imm(BulletBill_CannonVar); // check for bullet bill (cannon variant)
    if (zero_flag) { goto NoCDirF; }
    cmp_imm(BulletBill_FrenzyVar); // check for bullet bill (frenzy variant)
    if (zero_flag) { goto NoCDirF; } // branch if either found, direction does not change
    write_byte(Enemy_MovingDir + x, y); // store as moving direction
  
NoCDirF:
    dey(); // decrement and use as offset
    lda_absy(EnemyBGCXSpdData); // get proper horizontal speed
    write_byte(Enemy_X_Speed + x, a); // and store, then leave
  
ExEBGChk:
    goto rts;
    // --------------------------------
    // $04 - low nybble of vertical coordinate from block buffer routine
  
LandEnemyProperly:
    lda_zp(0x4); // check lower nybble of vertical coordinate saved earlier
    carry_flag = true;
    sbc_imm(0x8); // subtract eight pixels
    cmp_imm(0x5); // used to determine whether enemy landed from falling
    if (carry_flag) { goto ChkForRedKoopa; } // branch if lower nybble in range of $0d-$0f before subtract
    lda_zpx(Enemy_State);
    and_imm(0b01000000); // branch if d6 in enemy state is set
    if (!zero_flag) { goto LandEnemyInitState; }
    lda_zpx(Enemy_State);
    asl_acc(); // branch if d7 in enemy state is not set
    if (!carry_flag) { goto ChkLandedEnemyState; }
  
SChkA:
    goto DoEnemySideCheck; // if lower nybble < $0d, d7 set but d6 not set, jump here
  
ChkLandedEnemyState:
    lda_zpx(Enemy_State); // if enemy in normal state, branch back to jump here
    if (zero_flag) { goto SChkA; }
    cmp_imm(0x5); // if in state used by spiny's egg
    if (zero_flag) { goto ProcEnemyDirection; } // then branch elsewhere
    cmp_imm(0x3); // if already in state used by koopas and buzzy beetles
    if (carry_flag) { goto ExSteChk; } // or in higher numbered state, branch to leave
    lda_zpx(Enemy_State); // load enemy state again (why?)
    cmp_imm(0x2); // if not in $02 state (used by koopas and buzzy beetles)
    if (!zero_flag) { goto ProcEnemyDirection; } // then branch elsewhere
    lda_imm(0x10); // load default timer here
    ldy_zpx(Enemy_ID); // check enemy identifier for spiny
    cpy_imm(Spiny);
    if (!zero_flag) { goto SetForStn; } // branch if not found
    lda_imm(0x0); // set timer for $00 if spiny
  
SetForStn:
    write_byte(EnemyIntervalTimer + x, a); // set timer here
    lda_imm(0x3); // set state here, apparently used to render
    write_byte(Enemy_State + x, a); // upside-down koopas and buzzy beetles
    EnemyLanding(); // then land it properly
  
ExSteChk:
    goto rts; // then leave
  
ProcEnemyDirection:
    lda_zpx(Enemy_ID); // check enemy identifier for goomba
    cmp_imm(Goomba); // branch if found
    if (zero_flag) { goto LandEnemyInitState; }
    cmp_imm(Spiny); // check for spiny
    if (!zero_flag) { goto InvtD; } // branch if not found
    lda_imm(0x1);
    write_byte(Enemy_MovingDir + x, a); // send enemy moving to the right by default
    lda_imm(0x8);
    write_byte(Enemy_X_Speed + x, a); // set horizontal speed accordingly
    lda_zp(FrameCounter);
    and_imm(0b00000111); // if timed appropriately, spiny will skip over
    if (zero_flag) { goto LandEnemyInitState; } // trying to face the player
  
InvtD:
    ldy_imm(0x1); // load 1 for enemy to face the left (inverted here)
    PlayerEnemyDiff(); // get horizontal difference between player and enemy
    if (!neg_flag) { goto CNwCDir; } // if enemy to the right of player, branch
    iny(); // if to the left, increment by one for enemy to face right (inverted)
  
CNwCDir:
    tya();
    cmp_zpx(Enemy_MovingDir); // compare direction in A with current direction in memory
    if (!zero_flag) { goto LandEnemyInitState; }
    jsr(ChkForBump_HammerBroJ, 350); // if equal, not facing in correct dir, do sub to turn around
  
LandEnemyInitState:
    EnemyLanding(); // land enemy properly
    lda_zpx(Enemy_State);
    and_imm(0b10000000); // if d7 of enemy state is set, branch
    if (!zero_flag) { goto NMovShellFallBit; }
    lda_imm(0x0); // otherwise initialize enemy state and leave
    write_byte(Enemy_State + x, a); // note this will also turn spiny's egg into spiny
    goto rts;
  
NMovShellFallBit:
    lda_zpx(Enemy_State); // nullify d6 of enemy state, save other bits
    and_imm(0b10111111); // and store, then leave
    write_byte(Enemy_State + x, a);
    goto rts;
    // --------------------------------
  
ChkForRedKoopa:
    lda_zpx(Enemy_ID); // check for red koopa troopa $03
    cmp_imm(RedKoopa);
    if (!zero_flag) { goto Chk2MSBSt; } // branch if not found
    lda_zpx(Enemy_State);
    if (zero_flag) { goto ChkForBump_HammerBroJ; } // if enemy found and in normal state, branch
  
Chk2MSBSt:
    lda_zpx(Enemy_State); // save enemy state into Y
    tay();
    asl_acc(); // check for d7 set
    if (!carry_flag) { goto GetSteFromD; } // branch if not set
    lda_zpx(Enemy_State);
    ora_imm(0b01000000); // set d6
    goto SetD6Ste; // jump ahead of this part
  
GetSteFromD:
    lda_absy(EnemyBGCStateData); // load new enemy state with old as offset
  
SetD6Ste:
    write_byte(Enemy_State + x, a); // set as new state
    // --------------------------------
    // $00 - used to store bitmask (not used but initialized here)
    // $eb - used in DoEnemySideCheck as counter and to compare moving directions
  
DoEnemySideCheck:
    lda_zpx(Enemy_Y_Position); // if enemy within status bar, branch to leave
    cmp_imm(0x20); // because there's nothing there that impedes movement
    if (!carry_flag) { goto ExESdeC; }
    ldy_imm(0x16); // start by finding block to the left of enemy ($00,$14)
    lda_imm(0x2); // set value here in what is also used as
    write_byte(0xeb, a); // OAM data offset
  
SdeCLoop:
    lda_zp(0xeb); // check value
    cmp_zpx(Enemy_MovingDir); // compare value against moving direction
    if (!zero_flag) { goto NextSdeC; } // branch if different and do not seek block there
    lda_imm(0x1); // set flag in A for save horizontal coordinate 
    jsr(BlockBufferChk_Enemy, 351); // find block to left or right of enemy object
    if (zero_flag) { goto NextSdeC; } // if nothing found, branch
    jsr(ChkForNonSolids, 352); // check for non-solid blocks
    if (!zero_flag) { goto ChkForBump_HammerBroJ; } // branch if not found
  
NextSdeC:
    dec_zp(0xeb); // move to the next direction
    iny();
    cpy_imm(0x18); // increment Y, loop only if Y < $18, thus we check
    if (!carry_flag) { goto SdeCLoop; } // enemy ($00, $14) and ($10, $14) pixel coordinates
  
ExESdeC:
    goto rts;
  
ChkForBump_HammerBroJ:
    cpx_imm(0x5); // check if we're on the special use slot
    if (zero_flag) { goto NoBump; } // and if so, branch ahead and do not play sound
    lda_zpx(Enemy_State); // if enemy state d7 not set, branch
    asl_acc(); // ahead and do not play sound
    if (!carry_flag) { goto NoBump; }
    lda_imm(Sfx_Bump); // otherwise, play bump sound
    write_byte(Square1SoundQueue, a); // sound will never be played if branching from ChkForRedKoopa
  
NoBump:
    lda_zpx(Enemy_ID); // check for hammer bro
    cmp_imm(0x5);
    if (!zero_flag) { goto InvEnemyDir; } // branch if not found
    lda_imm(0x0);
    write_byte(0x0, a); // initialize value here for bitmask  
    ldy_imm(0xfa); // load default vertical speed for jumping
    goto SetHJ; // jump to code that makes hammer bro jump
  
InvEnemyDir:
    goto RXSpd; // jump to turn the enemy around
    // --------------------------------
    // $00 - used to hold horizontal difference between player and enemy
  
SubtEnemyYPos:
    lda_zpx(Enemy_Y_Position); // add 62 pixels to enemy object's
    carry_flag = false; // vertical coordinate
    adc_imm(0x3e);
    cmp_imm(0x44); // compare against a certain range
    goto rts; // and leave with flags set for conditional branch
  
EnemyJump:
    jsr(SubtEnemyYPos, 353); // do a sub here
    if (!carry_flag) { goto DoSide; } // if enemy vertical coord + 62 < 68, branch to leave
    lda_zpx(Enemy_Y_Speed);
    carry_flag = false; // add two to vertical speed
    adc_imm(0x2);
    cmp_imm(0x3); // if green paratroopa not falling, branch ahead
    if (!carry_flag) { goto DoSide; }
    jsr(ChkUnderEnemy, 354); // otherwise, check to see if green paratroopa is 
    if (zero_flag) { goto DoSide; } // standing on anything, then branch to same place if not
    jsr(ChkForNonSolids, 355); // check for non-solid blocks
    if (zero_flag) { goto DoSide; } // branch if found
    EnemyLanding(); // change vertical coordinate and speed
    lda_imm(0xfd);
    write_byte(Enemy_Y_Speed + x, a); // make the paratroopa jump again
  
DoSide:
    goto DoEnemySideCheck; // check for horizontal blockage, then leave
    // --------------------------------
  
HammerBroBGColl:
    jsr(ChkUnderEnemy, 356); // check to see if hammer bro is standing on anything
    if (zero_flag) { goto NoUnderHammerBro; }
    cmp_imm(0x23); // check for blank metatile $23 and branch if not found
    if (!zero_flag) { goto UnderHammerBro; }
  
KillEnemyAboveBlock:
    jsr(ShellOrBlockDefeat, 357); // do this sub to kill enemy
    lda_imm(0xfc); // alter vertical speed of enemy and leave
    write_byte(Enemy_Y_Speed + x, a);
    goto rts;
  
UnderHammerBro:
    lda_absx(EnemyFrameTimer); // check timer used by hammer bro
    if (!zero_flag) { goto NoUnderHammerBro; } // branch if not expired
    lda_zpx(Enemy_State);
    and_imm(0b10001000); // save d7 and d3 from enemy state, nullify other bits
    write_byte(Enemy_State + x, a); // and store
    EnemyLanding(); // modify vertical coordinate, speed and something else
    goto DoEnemySideCheck; // then check for horizontal blockage and leave
  
NoUnderHammerBro:
    lda_zpx(Enemy_State); // if hammer bro is not standing on anything, set d0
    ora_imm(0x1); // in the enemy state to indicate jumping or falling, then leave
    write_byte(Enemy_State + x, a);
    goto rts;
  
ChkUnderEnemy:
    lda_imm(0x0); // set flag in A for save vertical coordinate
    ldy_imm(0x15); // set Y to check the bottom middle (8,18) of enemy object
    goto BlockBufferChk_Enemy; // hop to it!
  
ChkForNonSolids:
    cmp_imm(0x26); // blank metatile used for vines?
    if (zero_flag) { goto NSFnd; }
    cmp_imm(0xc2); // regular coin?
    if (zero_flag) { goto NSFnd; }
    cmp_imm(0xc3); // underwater coin?
    if (zero_flag) { goto NSFnd; }
    cmp_imm(0x5f); // hidden coin block?
    if (zero_flag) { goto NSFnd; }
    cmp_imm(0x60); // hidden 1-up block?
  
NSFnd:
    goto rts;
    // -------------------------------------------------------------------------------------
  
FireballBGCollision:
    lda_zpx(Fireball_Y_Position); // check fireball's vertical coordinate
    cmp_imm(0x18);
    if (!carry_flag) { goto ClearBounceFlag; } // if within the status bar area of the screen, branch ahead
    jsr(BlockBufferChk_FBall, 358); // do fireball to background collision detection on bottom of it
    if (zero_flag) { goto ClearBounceFlag; } // if nothing underneath fireball, branch
    jsr(ChkForNonSolids, 359); // check for non-solid metatiles
    if (zero_flag) { goto ClearBounceFlag; } // branch if any found
    lda_zpx(Fireball_Y_Speed); // if fireball's vertical speed set to move upwards,
    if (neg_flag) { goto InitFireballExplode; } // branch to set exploding bit in fireball's state
    lda_zpx(FireballBouncingFlag); // if bouncing flag already set,
    if (!zero_flag) { goto InitFireballExplode; } // branch to set exploding bit in fireball's state
    lda_imm(0xfd);
    write_byte(Fireball_Y_Speed + x, a); // otherwise set vertical speed to move upwards (give it bounce)
    lda_imm(0x1);
    write_byte(FireballBouncingFlag + x, a); // set bouncing flag
    lda_zpx(Fireball_Y_Position);
    and_imm(0xf8); // modify vertical coordinate to land it properly
    write_byte(Fireball_Y_Position + x, a); // store as new vertical coordinate
    goto rts; // leave
  
ClearBounceFlag:
    lda_imm(0x0);
    write_byte(FireballBouncingFlag + x, a); // clear bouncing flag by default
    goto rts; // leave
  
InitFireballExplode:
    lda_imm(0x80);
    write_byte(Fireball_State + x, a); // set exploding flag in fireball's state
    lda_imm(Sfx_Bump);
    write_byte(Square1SoundQueue, a); // load bump sound
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // $00 - used to hold one of bitmasks, or offset
    // $01 - used for relative X coordinate, also used to store middle screen page location
    // $02 - used for relative Y coordinate, also used to store middle screen coordinate
    // this data added to relative coordinates of sprite objects
    // stored in order: left edge, top edge, right edge, bottom edge
  
GetFireballBoundBox:
    txa(); // add seven bytes to offset
    carry_flag = false; // to use in routines as offset for fireball
    adc_imm(0x7);
    tax();
    ldy_imm(0x2); // set offset for relative coordinates
    if (!zero_flag) { goto FBallB; } // unconditional branch
  
GetMiscBoundBox:
    txa(); // add nine bytes to offset
    carry_flag = false; // to use in routines as offset for misc object
    adc_imm(0x9);
    tax();
    ldy_imm(0x6); // set offset for relative coordinates
  
FBallB:
    jsr(BoundingBoxCore, 360); // get bounding box coordinates
    goto CheckRightScreenBBox; // jump to handle any offscreen coordinates
  
GetEnemyBoundBox:
    ldy_imm(0x48); // store bitmask here for now
    write_byte(0x0, y);
    ldy_imm(0x44); // store another bitmask here for now and jump
    goto GetMaskedOffScrBits;
  
SmallPlatformBoundBox:
    ldy_imm(0x8); // store bitmask here for now
    write_byte(0x0, y);
    ldy_imm(0x4); // store another bitmask here for now
  
GetMaskedOffScrBits:
    lda_zpx(Enemy_X_Position); // get enemy object position relative
    carry_flag = true; // to the left side of the screen
    sbc_abs(ScreenLeft_X_Pos);
    write_byte(0x1, a); // store here
    lda_zpx(Enemy_PageLoc); // subtract borrow from current page location
    sbc_abs(ScreenLeft_PageLoc); // of left side
    if (neg_flag) { goto CMBits; } // if enemy object is beyond left edge, branch
    ora_zp(0x1);
    if (zero_flag) { goto CMBits; } // if precisely at the left edge, branch
    ldy_zp(0x0); // if to the right of left edge, use value in $00 for A
  
CMBits:
    tya(); // otherwise use contents of Y
    and_abs(Enemy_OffscreenBits); // preserve bitwise whatever's in here
    write_byte(EnemyOffscrBitsMasked + x, a); // save masked offscreen bits here
    if (!zero_flag) { goto MoveBoundBoxOffscreen; } // if anything set here, branch
    goto SetupEOffsetFBBox; // otherwise, do something else
  
LargePlatformBoundBox:
    inx(); // increment X to get the proper offset
    GetXOffscreenBits(); // then jump directly to the sub for horizontal offscreen bits
    dex(); // decrement to return to original offset
    cmp_imm(0xfe); // if completely offscreen, branch to put entire bounding
    if (carry_flag) { goto MoveBoundBoxOffscreen; } // box offscreen, otherwise start getting coordinates
  
SetupEOffsetFBBox:
    txa(); // add 1 to offset to properly address
    carry_flag = false; // the enemy object memory locations
    adc_imm(0x1);
    tax();
    ldy_imm(0x1); // load 1 as offset here, same reason
    jsr(BoundingBoxCore, 361); // do a sub to get the coordinates of the bounding box
    goto CheckRightScreenBBox; // jump to handle offscreen coordinates of bounding box
  
MoveBoundBoxOffscreen:
    txa(); // multiply offset by 4
    asl_acc();
    asl_acc();
    tay(); // use as offset here
    lda_imm(0xff);
    write_byte(EnemyBoundingBoxCoord + y, a); // load value into four locations here and leave
    write_byte(EnemyBoundingBoxCoord + 1 + y, a);
    write_byte(EnemyBoundingBoxCoord + 2 + y, a);
    write_byte(EnemyBoundingBoxCoord + 3 + y, a);
    goto rts;
  
BoundingBoxCore:
    write_byte(0x0, x); // save offset here
    lda_absy(SprObject_Rel_YPos); // store object coordinates relative to screen
    write_byte(0x2, a); // vertically and horizontally, respectively
    lda_absy(SprObject_Rel_XPos);
    write_byte(0x1, a);
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
    write_byte(BoundingBox_UL_Corner + y, a); // store here
    lda_zp(0x1);
    carry_flag = false;
    adc_absx(BoundBoxCtrlData + 2); // add the third number in the bounding box data to the
    write_byte(BoundingBox_LR_Corner + y, a); // relative horizontal coordinate and store
    inx(); // increment both offsets
    iny();
    lda_zp(0x2); // add the second number to the relative vertical coordinate
    carry_flag = false; // using incremented offset and store using the other
    adc_absx(BoundBoxCtrlData); // incremented offset
    write_byte(BoundingBox_UL_Corner + y, a);
    lda_zp(0x2);
    carry_flag = false;
    adc_absx(BoundBoxCtrlData + 2); // add the fourth number to the relative vertical coordinate
    write_byte(BoundingBox_LR_Corner + y, a); // and store
    pla(); // get original offset loaded into $00 * y from stack
    tay(); // use as Y
    ldx_zp(0x0); // get original offset and use as X again
    goto rts;
  
CheckRightScreenBBox:
    lda_abs(ScreenLeft_X_Pos); // add 128 pixels to left side of screen
    carry_flag = false; // and store as horizontal coordinate of middle
    adc_imm(0x80);
    write_byte(0x2, a);
    lda_abs(ScreenLeft_PageLoc); // add carry to page location of left side of screen
    adc_imm(0x0); // and store as page location of middle
    write_byte(0x1, a);
    lda_zpx(SprObject_X_Position); // get horizontal coordinate
    cmp_zp(0x2); // compare against middle horizontal coordinate
    lda_zpx(SprObject_PageLoc); // get page location
    sbc_zp(0x1); // subtract from middle page location
    if (!carry_flag) { goto CheckLeftScreenBBox; } // if object is on the left side of the screen, branch
    lda_absy(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    if (neg_flag) { goto NoOfs; } // coordinates, branch if still on the screen
    lda_imm(0xff); // load offscreen value here to use on one or both horizontal sides
    ldx_absy(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
    if (neg_flag) { goto SORte; } // coordinates, and branch if still on the screen
    write_byte(BoundingBox_UL_XPos + y, a); // store offscreen value for left side
  
SORte:
    write_byte(BoundingBox_DR_XPos + y, a); // store offscreen value for right side
  
NoOfs:
    ldx_zp(ObjectOffset); // get object offset and leave
    goto rts;
  
CheckLeftScreenBBox:
    lda_absy(BoundingBox_UL_XPos); // check left-side edge of bounding box for offscreen
    if (!neg_flag) { goto NoOfs2; } // coordinates, and branch if still on the screen
    cmp_imm(0xa0); // check to see if left-side edge is in the middle of the
    if (!carry_flag) { goto NoOfs2; } // screen or really offscreen, and branch if still on
    lda_imm(0x0);
    ldx_absy(BoundingBox_DR_XPos); // check right-side edge of bounding box for offscreen
    if (!neg_flag) { goto SOLft; } // coordinates, branch if still onscreen
    write_byte(BoundingBox_DR_XPos + y, a); // store offscreen value for right side
  
SOLft:
    write_byte(BoundingBox_UL_XPos + y, a); // store offscreen value for left side
  
NoOfs2:
    ldx_zp(ObjectOffset); // get object offset and leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // $06 - second object's offset
    // $07 - counter
  
PlayerCollisionCore:
    ldx_imm(0x0); // initialize X to use player's bounding box for comparison
  
SprObjectCollisionCore:
    write_byte(0x6, y); // save contents of Y here
    lda_imm(0x1);
    write_byte(0x7, a); // save value 1 here as counter, compare horizontal coordinates first
  
CollisionCoreLoop:
    lda_absy(BoundingBox_UL_Corner); // compare left/top coordinates
    cmp_absx(BoundingBox_UL_Corner); // of first and second objects' bounding boxes
    if (carry_flag) { goto FirstBoxGreater; } // if first left/top => second, branch
    cmp_absx(BoundingBox_LR_Corner); // otherwise compare to right/bottom of second
    if (!carry_flag) { goto SecondBoxVerticalChk; } // if first left/top < second right/bottom, branch elsewhere
    if (zero_flag) { goto CollisionFound; } // if somehow equal, collision, thus branch
    lda_absy(BoundingBox_LR_Corner); // if somehow greater, check to see if bottom of
    cmp_absy(BoundingBox_UL_Corner); // first object's bounding box is greater than its top
    if (!carry_flag) { goto CollisionFound; } // if somehow less, vertical wrap collision, thus branch
    cmp_absx(BoundingBox_UL_Corner); // otherwise compare bottom of first bounding box to the top
    if (carry_flag) { goto CollisionFound; } // of second box, and if equal or greater, collision, thus branch
    ldy_zp(0x6); // otherwise return with carry clear and Y = $0006
    goto rts; // note horizontal wrapping never occurs
  
SecondBoxVerticalChk:
    lda_absx(BoundingBox_LR_Corner); // check to see if the vertical bottom of the box
    cmp_absx(BoundingBox_UL_Corner); // is greater than the vertical top
    if (!carry_flag) { goto CollisionFound; } // if somehow less, vertical wrap collision, thus branch
    lda_absy(BoundingBox_LR_Corner); // otherwise compare horizontal right or vertical bottom
    cmp_absx(BoundingBox_UL_Corner); // of first box with horizontal left or vertical top of second box
    if (carry_flag) { goto CollisionFound; } // if equal or greater, collision, thus branch
    ldy_zp(0x6); // otherwise return with carry clear and Y = $0006
    goto rts;
  
FirstBoxGreater:
    cmp_absx(BoundingBox_UL_Corner); // compare first and second box horizontal left/vertical top again
    if (zero_flag) { goto CollisionFound; } // if first coordinate = second, collision, thus branch
    cmp_absx(BoundingBox_LR_Corner); // if not, compare with second object right or bottom edge
    if (!carry_flag) { goto CollisionFound; } // if left/top of first less than or equal to right/bottom of second
    if (zero_flag) { goto CollisionFound; } // then collision, thus branch
    cmp_absy(BoundingBox_LR_Corner); // otherwise check to see if top of first box is greater than bottom
    if (!carry_flag) { goto NoCollisionFound; } // if less than or equal, no collision, branch to end
    if (zero_flag) { goto NoCollisionFound; }
    lda_absy(BoundingBox_LR_Corner); // otherwise compare bottom of first to top of second
    cmp_absx(BoundingBox_UL_Corner); // if bottom of first is greater than top of second, vertical wrap
    if (carry_flag) { goto CollisionFound; } // collision, and branch, otherwise, proceed onwards here
  
NoCollisionFound:
    carry_flag = false; // clear carry, then load value set earlier, then leave
    ldy_zp(0x6); // like previous ones, if horizontal coordinates do not collide, we do
    goto rts; // not bother checking vertical ones, because what's the point?
  
CollisionFound:
    inx(); // increment offsets on both objects to check
    iny(); // the vertical coordinates
    dec_zp(0x7); // decrement counter to reflect this
    if (!neg_flag) { goto CollisionCoreLoop; } // if counter not expired, branch to loop
    carry_flag = true; // otherwise we already did both sets, therefore collision, so set carry
    ldy_zp(0x6); // load original value set here earlier, then leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // $02 - modified y coordinate
    // $03 - stores metatile involved in block buffer collisions
    // $04 - comes in with offset to block buffer adder data, goes out with low nybble x/y coordinate
    // $05 - modified x coordinate
    // $06-$07 - block buffer address
  
BlockBufferChk_Enemy:
    pha(); // save contents of A to stack
    txa();
    carry_flag = false; // add 1 to X to run sub with enemy offset in mind
    adc_imm(0x1);
    tax();
    pla(); // pull A from stack and jump elsewhere
    goto BBChk_E;
    //  ResidualMiscObjectCode:
    //        txa
    //        clc           ;supposedly used once to set offset for
    //        adc #$0d      ;miscellaneous objects
    //        tax
    //        ldy #$1b      ;supposedly used once to set offset for block buffer data
    //        jmp ResJmpM   ;probably used in early stages to do misc to bg collision detection
  
BlockBufferChk_FBall:
    ldy_imm(0x1a); // set offset for block buffer adder data
    txa();
    carry_flag = false;
    adc_imm(0x7); // add seven bytes to use
    tax();
    lda_imm(0x0); //  ResJmpM: lda #$00 ;set A to return vertical coordinate
  
BBChk_E:
    jsr(BlockBufferCollision, 362); // do collision detection subroutine for sprite object
    ldx_zp(ObjectOffset); // get object offset
    cmp_imm(0x0); // check to see if object bumped into anything
    goto rts;
  
BlockBufferColli_Feet:
    iny(); // if branched here, increment to next set of adders
  
BlockBufferColli_Head:
    lda_imm(0x0); // set flag to return vertical coordinate
    goto BlockBufferColli_SideSkip; //  .db $2c ;BIT instruction opcode
  
BlockBufferColli_Side:
    lda_imm(0x1); // set flag to return horizontal coordinate
  
BlockBufferColli_SideSkip:
    ldx_imm(0x0); // set offset for player object
  
BlockBufferCollision:
    pha(); // save contents of A to stack
    write_byte(0x4, y); // save contents of Y here
    lda_absy(BlockBuffer_X_Adder); // add horizontal coordinate
    carry_flag = false; // of object to value obtained using Y as offset
    adc_zpx(SprObject_X_Position);
    write_byte(0x5, a); // store here
    lda_zpx(SprObject_PageLoc);
    adc_imm(0x0); // add carry to page location
    and_imm(0x1); // get LSB, mask out all other bits
    lsr_acc(); // move to carry
    ora_zp(0x5); // get stored value
    ror_acc(); // rotate carry to MSB of A
    lsr_acc(); // and effectively move high nybble to
    lsr_acc(); // lower, LSB which became MSB will be
    lsr_acc(); // d4 at this point
    GetBlockBufferAddr(); // get address of block buffer into $06, $07
    ldy_zp(0x4); // get old contents of Y
    lda_zpx(SprObject_Y_Position); // get vertical coordinate of object
    carry_flag = false;
    adc_absy(BlockBuffer_Y_Adder); // add it to value obtained using Y as offset
    and_imm(0b11110000); // mask out low nybble
    carry_flag = true;
    sbc_imm(0x20); // subtract 32 pixels for the status bar
    write_byte(0x2, a); // store result here
    tay(); // use as offset for block buffer
    lda_indy(0x6); // check current content of block buffer
    write_byte(0x3, a); // and store here
    ldy_zp(0x4); // get old contents of Y again
    pla(); // pull A from stack
    if (!zero_flag) { goto RetXC; } // if A = 1, branch
    lda_zpx(SprObject_Y_Position); // if A = 0, load vertical coordinate
    goto RetYC; // and jump
  
RetXC:
    lda_zpx(SprObject_X_Position); // otherwise load horizontal coordinate
  
RetYC:
    and_imm(0b00001111); // and mask out high nybble
    write_byte(0x4, a); // store masked out result here
    lda_zp(0x3); // get saved content of block buffer
    goto rts; // and leave
    // -------------------------------------------------------------------------------------
    // unused byte
    //       .db $ff
    // -------------------------------------------------------------------------------------
    // $00 - offset to vine Y coordinate adder
    // $02 - offset to sprite data
    // -------------------------------------------------------------------------------------
  
DrawHammer:
    ldy_absx(Misc_SprDataOffset); // get misc object OAM data offset
    lda_abs(TimerControl);
    if (!zero_flag) { goto ForceHPose; } // if master timer control set, skip this part
    lda_zpx(Misc_State); // otherwise get hammer's state
    and_imm(0b01111111); // mask out d7
    cmp_imm(0x1); // check to see if set to 1 yet
    if (zero_flag) { goto GetHPose; } // if so, branch
  
ForceHPose:
    ldx_imm(0x0); // reset offset here
    if (zero_flag) { goto RenderH; } // do unconditional branch to rendering part
  
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
    write_byte(Sprite_Y_Position + y, a); // store as sprite Y coordinate for first sprite
    carry_flag = false;
    adc_absx(SecondSprYPos); // add second sprite vertical adder based on offset
    write_byte(Sprite_Y_Position + 4 + y, a); // store as sprite Y coordinate for second sprite
    lda_abs(Misc_Rel_XPos); // get relative horizontal coordinate
    carry_flag = false;
    adc_absx(FirstSprXPos); // add first sprite horizontal adder based on offset
    write_byte(Sprite_X_Position + y, a); // store as sprite X coordinate for first sprite
    carry_flag = false;
    adc_absx(SecondSprXPos); // add second sprite horizontal adder based on offset
    write_byte(Sprite_X_Position + 4 + y, a); // store as sprite X coordinate for second sprite
    lda_absx(FirstSprTilenum);
    write_byte(Sprite_Tilenumber + y, a); // get and store tile number of first sprite
    lda_absx(SecondSprTilenum);
    write_byte(Sprite_Tilenumber + 4 + y, a); // get and store tile number of second sprite
    lda_absx(HammerSprAttrib);
    write_byte(Sprite_Attributes + y, a); // get and store attribute bytes for both
    write_byte(Sprite_Attributes + 4 + y, a); // note in this case they use the same data
    ldx_zp(ObjectOffset); // get misc object offset
    lda_abs(Misc_OffscreenBits);
    and_imm(0b11111100); // check offscreen bits
    if (zero_flag) { goto NoHOffscr; } // if all bits clear, leave object alone
    lda_imm(0x0);
    write_byte(Misc_State + x, a); // otherwise nullify misc object state
    lda_imm(0xf8);
    jsr(DumpTwoSpr, 363); // do sub to move hammer sprites offscreen
  
NoHOffscr:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // $00-$01 - used to hold tile numbers ($01 addressed in draw floatey number part)
    // $02 - used to hold Y coordinate for floatey number
    // $03 - residual byte used for flip (but value set here affects nothing)
    // $04 - attribute byte for floatey number
    // $05 - used as X coordinate for floatey number
  
FlagpoleGfxHandler:
    ldy_absx(Enemy_SprDataOffset); // get sprite data offset for flagpole flag
    lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a); // store as X coordinate for first sprite
    carry_flag = false;
    adc_imm(0x8); // add eight pixels and store
    write_byte(Sprite_X_Position + 4 + y, a); // as X coordinate for second and third sprites
    write_byte(Sprite_X_Position + 8 + y, a);
    carry_flag = false;
    adc_imm(0xc); // add twelve more pixels and
    write_byte(0x5, a); // store here to be used later by floatey number
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    jsr(DumpTwoSpr, 364); // and do sub to dump into first and second sprites
    adc_imm(0x8); // add eight pixels
    write_byte(Sprite_Y_Position + 8 + y, a); // and store into third sprite
    lda_abs(FlagpoleFNum_Y_Pos); // get vertical coordinate for floatey number
    write_byte(0x2, a); // store it here
    lda_imm(0x1);
    write_byte(0x3, a); // set value for flip which will not be used, and
    write_byte(0x4, a); // attribute byte for floatey number
    write_byte(Sprite_Attributes + y, a); // set attribute bytes for all three sprites
    write_byte(Sprite_Attributes + 4 + y, a);
    write_byte(Sprite_Attributes + 8 + y, a);
    lda_imm(0x7e);
    write_byte(Sprite_Tilenumber + y, a); // put triangle shaped tile
    write_byte(Sprite_Tilenumber + 8 + y, a); // into first and third sprites
    lda_imm(0x7f);
    write_byte(Sprite_Tilenumber + 4 + y, a); // put skull tile into second sprite
    lda_abs(FlagpoleCollisionYPos); // get vertical coordinate at time of collision
    if (zero_flag) { goto ChkFlagOffscreen; } // if zero, branch ahead
    tya();
    carry_flag = false; // add 12 bytes to sprite data offset
    adc_imm(0xc);
    tay(); // put back in Y
    lda_abs(FlagpoleScore); // get offset used to award points for touching flagpole
    asl_acc(); // multiply by 2 to get proper offset here
    tax();
    lda_absx(FlagpoleScoreNumTiles); // get appropriate tile data
    write_byte(0x0, a);
    lda_absx(FlagpoleScoreNumTiles + 1);
    jsr(DrawOneSpriteRow, 365); // use it to render floatey number
  
ChkFlagOffscreen:
    ldx_zp(ObjectOffset); // get object offset for flag
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    lda_abs(Enemy_OffscreenBits); // get offscreen bits
    and_imm(0b00001110); // mask out all but d3-d1
    if (zero_flag) { goto ExitDumpSpr; } // if none of these bits set, branch to leave
    // -------------------------------------------------------------------------------------
  
MoveSixSpritesOffscreen:
    lda_imm(0xf8); // set offscreen coordinate if jumping here
  
DumpSixSpr:
    write_byte(Sprite_Data + 20 + y, a); // dump A contents
    write_byte(Sprite_Data + 16 + y, a); // into third row sprites
  
DumpFourSpr:
    write_byte(Sprite_Data + 12 + y, a); // into second row sprites
  
DumpThreeSpr:
    write_byte(Sprite_Data + 8 + y, a);
  
DumpTwoSpr:
    write_byte(Sprite_Data + 4 + y, a); // and into first row sprites
    write_byte(Sprite_Data + y, a);
  
ExitDumpSpr:
    goto rts;
    // -------------------------------------------------------------------------------------
  
DrawLargePlatform:
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    write_byte(0x2, y); // store here
    iny(); // add 3 to it for offset
    iny(); // to X coordinate
    iny();
    lda_abs(Enemy_Rel_XPos); // get horizontal relative coordinate
    SixSpriteStacker(); // store X coordinates using A as base, stack horizontally
    ldx_zp(ObjectOffset);
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    jsr(DumpFourSpr, 366); // dump into first four sprites as Y coordinate
    ldy_abs(AreaType);
    cpy_imm(0x3); // check for castle-type level
    if (zero_flag) { goto ShrinkPlatform; }
    ldy_abs(SecondaryHardMode); // check for secondary hard mode flag set
    if (zero_flag) { goto SetLast2Platform; } // branch if not set elsewhere
  
ShrinkPlatform:
    lda_imm(0xf8); // load offscreen coordinate if flag set or castle-type level
  
SetLast2Platform:
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    write_byte(Sprite_Y_Position + 16 + y, a); // store vertical coordinate or offscreen
    write_byte(Sprite_Y_Position + 20 + y, a); // coordinate into last two sprites as Y coordinate
    lda_imm(0x5b); // load default tile for platform (girder)
    ldx_abs(CloudTypeOverride);
    if (zero_flag) { goto SetPlatformTilenum; } // if cloud level override flag not set, use
    lda_imm(0x75); // otherwise load other tile for platform (puff)
  
SetPlatformTilenum:
    ldx_zp(ObjectOffset); // get enemy object buffer offset
    iny(); // increment Y for tile offset
    jsr(DumpSixSpr, 367); // dump tile number into all six sprites
    lda_imm(0x2); // set palette controls
    iny(); // increment Y for sprite attributes
    jsr(DumpSixSpr, 368); // dump attributes into all six sprites
    inx(); // increment X for enemy objects
    GetXOffscreenBits(); // get offscreen bits again
    dex();
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    asl_acc(); // rotate d7 into carry, save remaining
    pha(); // bits to the stack
    if (!carry_flag) { goto SChk2; }
    lda_imm(0xf8); // if d7 was set, move first sprite offscreen
    write_byte(Sprite_Y_Position + y, a);
  
SChk2:
    pla(); // get bits from stack
    asl_acc(); // rotate d6 into carry
    pha(); // save to stack
    if (!carry_flag) { goto SChk3; }
    lda_imm(0xf8); // if d6 was set, move second sprite offscreen
    write_byte(Sprite_Y_Position + 4 + y, a);
  
SChk3:
    pla(); // get bits from stack
    asl_acc(); // rotate d5 into carry
    pha(); // save to stack
    if (!carry_flag) { goto SChk4; }
    lda_imm(0xf8); // if d5 was set, move third sprite offscreen
    write_byte(Sprite_Y_Position + 8 + y, a);
  
SChk4:
    pla(); // get bits from stack
    asl_acc(); // rotate d4 into carry
    pha(); // save to stack
    if (!carry_flag) { goto SChk5; }
    lda_imm(0xf8); // if d4 was set, move fourth sprite offscreen
    write_byte(Sprite_Y_Position + 12 + y, a);
  
SChk5:
    pla(); // get bits from stack
    asl_acc(); // rotate d3 into carry
    pha(); // save to stack
    if (!carry_flag) { goto SChk6; }
    lda_imm(0xf8); // if d3 was set, move fifth sprite offscreen
    write_byte(Sprite_Y_Position + 16 + y, a);
  
SChk6:
    pla(); // get bits from stack
    asl_acc(); // rotate d2 into carry
    if (!carry_flag) { goto SLChk; } // save to stack
    lda_imm(0xf8);
    write_byte(Sprite_Y_Position + 20 + y, a); // if d2 was set, move sixth sprite offscreen
  
SLChk:
    lda_abs(Enemy_OffscreenBits); // check d7 of offscreen bits
    asl_acc(); // and if d7 is not set, skip sub
    if (!carry_flag) { goto ExDLPl; }
    jsr(MoveSixSpritesOffscreen, 369); // otherwise branch to move all sprites offscreen
  
ExDLPl:
    goto rts;
    // -------------------------------------------------------------------------------------
  
DrawFloateyNumber_Coin:
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // divide by 2
    if (carry_flag) { goto NotRsNum; } // branch if d0 not set to raise number every other frame
    dec_zpx(Misc_Y_Position); // otherwise, decrement vertical coordinate
  
NotRsNum:
    lda_zpx(Misc_Y_Position); // get vertical coordinate
    jsr(DumpTwoSpr, 370); // dump into both sprites
    lda_abs(Misc_Rel_XPos); // get relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a); // store as X coordinate for first sprite
    carry_flag = false;
    adc_imm(0x8); // add eight pixels
    write_byte(Sprite_X_Position + 4 + y, a); // store as X coordinate for second sprite
    lda_imm(0x2);
    write_byte(Sprite_Attributes + y, a); // store attribute byte in both sprites
    write_byte(Sprite_Attributes + 4 + y, a);
    lda_imm(0xf7);
    write_byte(Sprite_Tilenumber + y, a); // put tile numbers into both sprites
    lda_imm(0xfb); // that resemble "200"
    write_byte(Sprite_Tilenumber + 4 + y, a);
    goto ExJCGfx; // then jump to leave (why not an rts here instead?)
  
JCoinGfxHandler:
    ldy_absx(Misc_SprDataOffset); // get coin/floatey number's OAM data offset
    lda_zpx(Misc_State); // get state of misc object
    cmp_imm(0x2); // if 2 or greater, 
    if (carry_flag) { goto DrawFloateyNumber_Coin; } // branch to draw floatey number
    lda_zpx(Misc_Y_Position); // store vertical coordinate as
    write_byte(Sprite_Y_Position + y, a); // Y coordinate for first sprite
    carry_flag = false;
    adc_imm(0x8); // add eight pixels
    write_byte(Sprite_Y_Position + 4 + y, a); // store as Y coordinate for second sprite
    lda_abs(Misc_Rel_XPos); // get relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a);
    write_byte(Sprite_X_Position + 4 + y, a); // store as X coordinate for first and second sprites
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // divide by 2 to alter every other frame
    and_imm(0b00000011); // mask out d2-d1
    tax(); // use as graphical offset
    lda_absx(JumpingCoinTiles); // load tile number
    iny(); // increment OAM data offset to write tile numbers
    jsr(DumpTwoSpr, 371); // do sub to dump tile number into both sprites
    dey(); // decrement to get old offset
    lda_imm(0x2);
    write_byte(Sprite_Attributes + y, a); // set attribute byte in first sprite
    lda_imm(0x82);
    write_byte(Sprite_Attributes + 4 + y, a); // set attribute byte with vertical flip in second sprite
    ldx_zp(ObjectOffset); // get misc object offset
  
ExJCGfx:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
    // $00-$01 - used to hold tiles for drawing the power-up, $00 also used to hold power-up type
    // $02 - used to hold bottom row Y position
    // $03 - used to hold flip control (not used here)
    // $04 - used to hold sprite attributes
    // $05 - used to hold X position
    // $07 - counter
    // tiles arranged in top left, right, bottom left, right order
  
DrawPowerUp:
    ldy_abs(Enemy_SprDataOffset + 5); // get power-up's sprite data offset
    lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
    carry_flag = false;
    adc_imm(0x8); // add eight pixels
    write_byte(0x2, a); // store result here
    lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
    write_byte(0x5, a); // store here
    ldx_zp(PowerUpType); // get power-up type
    lda_absx(PowerUpAttributes); // get attribute data for power-up type
    ora_abs(Enemy_SprAttrib + 5); // add background priority bit if set
    write_byte(0x4, a); // store attributes here
    txa();
    pha(); // save power-up type to the stack
    asl_acc();
    asl_acc(); // multiply by four to get proper offset
    tax(); // use as X
    lda_imm(0x1);
    write_byte(0x7, a); // set counter here to draw two rows of sprite object
    write_byte(0x3, a); // init d1 of flip control
  
PUpDrawLoop:
    lda_absx(PowerUpGfxTable); // load left tile of power-up object
    write_byte(0x0, a);
    lda_absx(PowerUpGfxTable + 1); // load right tile
    jsr(DrawOneSpriteRow, 372); // branch to draw one row of our power-up object
    dec_zp(0x7); // decrement counter
    if (!neg_flag) { goto PUpDrawLoop; } // branch until two rows are drawn
    ldy_abs(Enemy_SprDataOffset + 5); // get sprite data offset again
    pla(); // pull saved power-up type from the stack
    if (zero_flag) { goto PUpOfs; } // if regular mushroom, branch, do not change colors or flip
    cmp_imm(0x3);
    if (zero_flag) { goto PUpOfs; } // if 1-up mushroom, branch, do not change colors or flip
    write_byte(0x0, a); // store power-up type here now
    lda_zp(FrameCounter); // get frame counter
    lsr_acc(); // divide by 2 to change colors every two frames
    and_imm(0b00000011); // mask out all but d1 and d0 (previously d2 and d1)
    ora_abs(Enemy_SprAttrib + 5); // add background priority bit if any set
    write_byte(Sprite_Attributes + y, a); // set as new palette bits for top left and
    write_byte(Sprite_Attributes + 4 + y, a); // top right sprites for fire flower and star
    ldx_zp(0x0);
    dex(); // check power-up type for fire flower
    if (zero_flag) { goto FlipPUpRightSide; } // if found, skip this part
    write_byte(Sprite_Attributes + 8 + y, a); // otherwise set new palette bits  for bottom left
    write_byte(Sprite_Attributes + 12 + y, a); // and bottom right sprites as well for star only
  
FlipPUpRightSide:
    lda_absy(Sprite_Attributes + 4);
    ora_imm(0b01000000); // set horizontal flip bit for top right sprite
    write_byte(Sprite_Attributes + 4 + y, a);
    lda_absy(Sprite_Attributes + 12);
    ora_imm(0b01000000); // set horizontal flip bit for bottom right sprite
    write_byte(Sprite_Attributes + 12 + y, a); // note these are only done for fire flower and star power-ups
  
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
  
EnemyGfxHandler:
    lda_zpx(Enemy_Y_Position); // get enemy object vertical position
    write_byte(0x2, a);
    lda_abs(Enemy_Rel_XPos); // get enemy object horizontal position
    write_byte(0x5, a); // relative to screen
    ldy_absx(Enemy_SprDataOffset);
    write_byte(0xeb, y); // get sprite data offset
    lda_imm(0x0);
    write_byte(VerticalFlipFlag, a); // initialize vertical flip flag by default
    lda_zpx(Enemy_MovingDir);
    write_byte(0x3, a); // get enemy object moving direction
    lda_absx(Enemy_SprAttrib);
    write_byte(0x4, a); // get enemy object sprite attributes
    lda_zpx(Enemy_ID);
    cmp_imm(PiranhaPlant); // is enemy object piranha plant?
    if (!zero_flag) { goto CheckForRetainerObj; } // if not, branch
    ldy_zpx(PiranhaPlant_Y_Speed);
    if (neg_flag) { goto CheckForRetainerObj; } // if piranha plant moving upwards, branch
    ldy_absx(EnemyFrameTimer);
    if (zero_flag) { goto CheckForRetainerObj; } // if timer for movement expired, branch
    goto rts; // if all conditions fail, leave
  
CheckForRetainerObj:
    lda_zpx(Enemy_State); // store enemy state
    write_byte(0xed, a);
    and_imm(0b00011111); // nullify all but 5 LSB and use as Y
    tay();
    lda_zpx(Enemy_ID); // check for mushroom retainer/princess object
    cmp_imm(RetainerObject);
    if (!zero_flag) { goto CheckForBulletBillCV; } // if not found, branch
    ldy_imm(0x0); // if found, nullify saved state in Y
    lda_imm(0x1); // set value that will not be used
    write_byte(0x3, a);
    lda_imm(0x15); // set value $15 as code for mushroom retainer/princess object
  
CheckForBulletBillCV:
    cmp_imm(BulletBill_CannonVar); // otherwise check for bullet bill object
    if (!zero_flag) { goto CheckForJumpspring; } // if not found, branch again
    dec_zp(0x2); // decrement saved vertical position
    lda_imm(0x3);
    ldy_absx(EnemyFrameTimer); // get timer for enemy object
    if (zero_flag) { goto SBBAt; } // if expired, do not set priority bit
    ora_imm(0b00100000); // otherwise do so
  
SBBAt:
    write_byte(0x4, a); // set new sprite attributes
    ldy_imm(0x0); // nullify saved enemy state both in Y and in
    write_byte(0xed, y); // memory location here
    lda_imm(0x8); // set specific value to unconditionally branch once
  
CheckForJumpspring:
    cmp_imm(JumpspringObject); // check for jumpspring object
    if (!zero_flag) { goto CheckForPodoboo; }
    ldy_imm(0x3); // set enemy state -2 MSB here for jumpspring object
    ldx_abs(JumpspringAnimCtrl); // get current frame number for jumpspring object
    lda_absx(JumpspringFrameOffsets); // load data using frame number as offset
  
CheckForPodoboo:
    write_byte(0xef, a); // store saved enemy object value here
    write_byte(0xec, y); // and Y here (enemy state -2 MSB if not changed)
    ldx_zp(ObjectOffset); // get enemy object offset
    cmp_imm(0xc); // check for podoboo object
    if (!zero_flag) { goto CheckBowserGfxFlag; } // branch if not found
    lda_zpx(Enemy_Y_Speed); // if moving upwards, branch
    if (neg_flag) { goto CheckBowserGfxFlag; }
    inc_abs(VerticalFlipFlag); // otherwise, set flag for vertical flip
  
CheckBowserGfxFlag:
    lda_abs(BowserGfxFlag); // if not drawing bowser at all, skip to something else
    if (zero_flag) { goto CheckForGoomba; }
    ldy_imm(0x16); // if set to 1, draw bowser's front
    cmp_imm(0x1);
    if (zero_flag) { goto SBwsrGfxOfs; }
    iny(); // otherwise draw bowser's rear
  
SBwsrGfxOfs:
    write_byte(0xef, y);
  
CheckForGoomba:
    ldy_zp(0xef); // check value for goomba object
    cpy_imm(Goomba);
    if (!zero_flag) { goto CheckBowserFront; } // branch if not found
    lda_zpx(Enemy_State);
    cmp_imm(0x2); // check for defeated state
    if (!carry_flag) { goto GmbaAnim; } // if not defeated, go ahead and animate
    ldx_imm(0x4); // if defeated, write new value here
    write_byte(0xec, x);
  
GmbaAnim:
    and_imm(0b00100000); // check for d5 set in enemy object state 
    ora_abs(TimerControl); // or timer disable flag set
    if (!zero_flag) { goto CheckBowserFront; } // if either condition true, do not animate goomba
    lda_zp(FrameCounter);
    and_imm(0b00001000); // check for every eighth frame
    if (!zero_flag) { goto CheckBowserFront; }
    lda_zp(0x3);
    eor_imm(0b00000011); // invert bits to flip horizontally every eight frames
    write_byte(0x3, a); // leave alone otherwise
  
CheckBowserFront:
    lda_absy(EnemyAttributeData); // load sprite attribute using enemy object
    ora_zp(0x4); // as offset, and add to bits already loaded
    write_byte(0x4, a);
    lda_absy(EnemyGfxTableOffsets); // load value based on enemy object as offset
    tax(); // save as X
    ldy_zp(0xec); // get previously saved value
    lda_abs(BowserGfxFlag);
    if (zero_flag) { goto CheckForSpiny; } // if not drawing bowser object at all, skip all of this
    cmp_imm(0x1);
    if (!zero_flag) { goto CheckBowserRear; } // if not drawing front part, branch to draw the rear part
    lda_abs(BowserBodyControls); // check bowser's body control bits
    if (!neg_flag) { goto ChkFrontSte; } // branch if d7 not set (control's bowser's mouth)      
    ldx_imm(0xde); // otherwise load offset for second frame
  
ChkFrontSte:
    lda_zp(0xed); // check saved enemy state
    and_imm(0b00100000); // if bowser not defeated, do not set flag
    if (zero_flag) { goto DrawBowser; }
  
FlipBowserOver:
    write_byte(VerticalFlipFlag, x); // set vertical flip flag to nonzero
  
DrawBowser:
    goto DrawEnemyObject; // draw bowser's graphics now
  
CheckBowserRear:
    lda_abs(BowserBodyControls); // check bowser's body control bits
    and_imm(0x1);
    if (zero_flag) { goto ChkRearSte; } // branch if d0 not set (control's bowser's feet)
    ldx_imm(0xe4); // otherwise load offset for second frame
  
ChkRearSte:
    lda_zp(0xed); // check saved enemy state
    and_imm(0b00100000); // if bowser not defeated, do not set flag
    if (zero_flag) { goto DrawBowser; }
    lda_zp(0x2); // subtract 16 pixels from
    carry_flag = true; // saved vertical coordinate
    sbc_imm(0x10);
    write_byte(0x2, a);
    goto FlipBowserOver; // jump to set vertical flip flag
  
CheckForSpiny:
    cpx_imm(0x24); // check if value loaded is for spiny
    if (!zero_flag) { goto CheckForLakitu; } // if not found, branch
    cpy_imm(0x5); // if enemy state set to $05, do this,
    if (!zero_flag) { goto NotEgg; } // otherwise branch
    ldx_imm(0x30); // set to spiny egg offset
    lda_imm(0x2);
    write_byte(0x3, a); // set enemy direction to reverse sprites horizontally
    lda_imm(0x5);
    write_byte(0xec, a); // set enemy state
  
NotEgg:
    goto CheckForHammerBro; // skip a big chunk of this if we found spiny but not in egg
  
CheckForLakitu:
    cpx_imm(0x90); // check value for lakitu's offset loaded
    if (!zero_flag) { goto CheckUpsideDownShell; } // branch if not loaded
    lda_zp(0xed);
    and_imm(0b00100000); // check for d5 set in enemy state
    if (!zero_flag) { goto NoLAFr; } // branch if set
    lda_abs(FrenzyEnemyTimer);
    cmp_imm(0x10); // check timer to see if we've reached a certain range
    if (carry_flag) { goto NoLAFr; } // branch if not
    ldx_imm(0x96); // if d6 not set and timer in range, load alt frame for lakitu
  
NoLAFr:
    goto CheckDefeatedState; // skip this next part if we found lakitu but alt frame not needed
  
CheckUpsideDownShell:
    lda_zp(0xef); // check for enemy object => $04
    cmp_imm(0x4);
    if (carry_flag) { goto CheckRightSideUpShell; } // branch if true
    cpy_imm(0x2);
    if (!carry_flag) { goto CheckRightSideUpShell; } // branch if enemy state < $02
    ldx_imm(0x5a); // set for upside-down koopa shell by default
    ldy_zp(0xef);
    cpy_imm(BuzzyBeetle); // check for buzzy beetle object
    if (!zero_flag) { goto CheckRightSideUpShell; }
    ldx_imm(0x7e); // set for upside-down buzzy beetle shell if found
    inc_zp(0x2); // increment vertical position by one pixel
  
CheckRightSideUpShell:
    lda_zp(0xec); // check for value set here
    cmp_imm(0x4); // if enemy state < $02, do not change to shell, if
    if (!zero_flag) { goto CheckForHammerBro; } // enemy state => $02 but not = $04, leave shell upside-down
    ldx_imm(0x72); // set right-side up buzzy beetle shell by default
    inc_zp(0x2); // increment saved vertical position by one pixel
    ldy_zp(0xef);
    cpy_imm(BuzzyBeetle); // check for buzzy beetle object
    if (zero_flag) { goto CheckForDefdGoomba; } // branch if found
    ldx_imm(0x66); // change to right-side up koopa shell if not found
    inc_zp(0x2); // and increment saved vertical position again
  
CheckForDefdGoomba:
    cpy_imm(Goomba); // check for goomba object (necessary if previously
    if (!zero_flag) { goto CheckForHammerBro; } // failed buzzy beetle object test)
    ldx_imm(0x54); // load for regular goomba
    lda_zp(0xed); // note that this only gets performed if enemy state => $02
    and_imm(0b00100000); // check saved enemy state for d5 set
    if (!zero_flag) { goto CheckForHammerBro; } // branch if set
    ldx_imm(0x8a); // load offset for defeated goomba
    dec_zp(0x2); // set different value and decrement saved vertical position
  
CheckForHammerBro:
    ldy_zp(ObjectOffset);
    lda_zp(0xef); // check for hammer bro object
    cmp_imm(HammerBro);
    if (!zero_flag) { goto CheckForBloober; } // branch if not found
    lda_zp(0xed);
    if (zero_flag) { goto CheckToAnimateEnemy; } // branch if not in normal enemy state
    and_imm(0b00001000);
    if (zero_flag) { goto CheckDefeatedState; } // if d3 not set, branch further away
    ldx_imm(0xb4); // otherwise load offset for different frame
    if (!zero_flag) { goto CheckToAnimateEnemy; } // unconditional branch
  
CheckForBloober:
    cpx_imm(0x48); // check for cheep-cheep offset loaded
    if (zero_flag) { goto CheckToAnimateEnemy; } // branch if found
    lda_absy(EnemyIntervalTimer);
    cmp_imm(0x5);
    if (carry_flag) { goto CheckDefeatedState; } // branch if some timer is above a certain point
    cpx_imm(0x3c); // check for bloober offset loaded
    if (!zero_flag) { goto CheckToAnimateEnemy; } // branch if not found this time
    cmp_imm(0x1);
    if (zero_flag) { goto CheckDefeatedState; } // branch if timer is set to certain point
    inc_zp(0x2); // increment saved vertical coordinate three pixels
    inc_zp(0x2);
    inc_zp(0x2);
    goto CheckAnimationStop; // and do something else
  
CheckToAnimateEnemy:
    lda_zp(0xef); // check for specific enemy objects
    cmp_imm(Goomba);
    if (zero_flag) { goto CheckDefeatedState; } // branch if goomba
    cmp_imm(0x8);
    if (zero_flag) { goto CheckDefeatedState; } // branch if bullet bill (note both variants use $08 here)
    cmp_imm(Podoboo);
    if (zero_flag) { goto CheckDefeatedState; } // branch if podoboo
    cmp_imm(0x18); // branch if => $18
    if (carry_flag) { goto CheckDefeatedState; }
    ldy_imm(0x0);
    cmp_imm(0x15); // check for mushroom retainer/princess object
    if (!zero_flag) { goto CheckForSecondFrame; } // which uses different code here, branch if not found
    iny(); // residual instruction
    lda_abs(WorldNumber); // are we on world 8?
    cmp_imm(World8);
    if (carry_flag) { goto CheckDefeatedState; } // if so, leave the offset alone (use princess)
    ldx_imm(0xa2); // otherwise, set for mushroom retainer object instead
    lda_imm(0x3); // set alternate state here
    write_byte(0xec, a);
    if (!zero_flag) { goto CheckDefeatedState; } // unconditional branch
  
CheckForSecondFrame:
    lda_zp(FrameCounter); // load frame counter
    and_absy(EnemyAnimTimingBMask); // mask it (partly residual, one byte not ever used)
    if (!zero_flag) { goto CheckDefeatedState; } // branch if timing is off
  
CheckAnimationStop:
    lda_zp(0xed); // check saved enemy state
    and_imm(0b10100000); // for d7 or d5, or check for timers stopped
    ora_abs(TimerControl);
    if (!zero_flag) { goto CheckDefeatedState; } // if either condition true, branch
    txa();
    carry_flag = false;
    adc_imm(0x6); // add $06 to current enemy offset
    tax(); // to animate various enemy objects
  
CheckDefeatedState:
    lda_zp(0xed); // check saved enemy state
    and_imm(0b00100000); // for d5 set
    if (zero_flag) { goto DrawEnemyObject; } // branch if not set
    lda_zp(0xef);
    cmp_imm(0x4); // check for saved enemy object => $04
    if (!carry_flag) { goto DrawEnemyObject; } // branch if less
    ldy_imm(0x1);
    write_byte(VerticalFlipFlag, y); // set vertical flip flag
    dey();
    write_byte(0xec, y); // init saved value here
  
DrawEnemyObject:
    ldy_zp(0xeb); // load sprite data offset
    jsr(DrawEnemyObjRow, 373); // draw six tiles of data
    jsr(DrawEnemyObjRow, 374); // into sprite data
    jsr(DrawEnemyObjRow, 375);
    ldx_zp(ObjectOffset); // get enemy object offset
    ldy_absx(Enemy_SprDataOffset); // get sprite data offset
    lda_zp(0xef);
    cmp_imm(0x8); // get saved enemy object and check
    if (!zero_flag) { goto CheckForVerticalFlip; } // for bullet bill, branch if not found
  
SkipToOffScrChk:
    goto SprObjectOffscrChk; // jump if found
  
CheckForVerticalFlip:
    lda_abs(VerticalFlipFlag); // check if vertical flip flag is set here
    if (zero_flag) { goto CheckForESymmetry; } // branch if not
    lda_absy(Sprite_Attributes); // get attributes of first sprite we dealt with
    ora_imm(0b10000000); // set bit for vertical flip
    iny();
    iny(); // increment two bytes so that we store the vertical flip
    jsr(DumpSixSpr, 376); // in attribute bytes of enemy obj sprite data
    dey();
    dey(); // now go back to the Y coordinate offset
    tya();
    tax(); // give offset to X
    lda_zp(0xef);
    cmp_imm(HammerBro); // check saved enemy object for hammer bro
    if (zero_flag) { goto FlipEnemyVertically; }
    cmp_imm(Lakitu); // check saved enemy object for lakitu
    if (zero_flag) { goto FlipEnemyVertically; } // branch for hammer bro or lakitu
    cmp_imm(0x15);
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
    write_byte(Sprite_Tilenumber + x, a); // with first or second row tiles
    lda_absy(Sprite_Tilenumber + 20);
    write_byte(Sprite_Tilenumber + 4 + x, a);
    pla(); // pull first or second row tiles from stack
    write_byte(Sprite_Tilenumber + 20 + y, a); // and save in third row
    pla();
    write_byte(Sprite_Tilenumber + 16 + y, a);
  
CheckForESymmetry:
    lda_abs(BowserGfxFlag); // are we drawing bowser at all?
    if (!zero_flag) { goto SkipToOffScrChk; } // branch if so
    lda_zp(0xef);
    ldx_zp(0xec); // get alternate enemy state
    cmp_imm(0x5); // check for hammer bro object
    if (!zero_flag) { goto ContES; }
    goto SprObjectOffscrChk; // jump if found
  
ContES:
    cmp_imm(Bloober); // check for bloober object
    if (zero_flag) { goto MirrorEnemyGfx; }
    cmp_imm(PiranhaPlant); // check for piranha plant object
    if (zero_flag) { goto MirrorEnemyGfx; }
    cmp_imm(Podoboo); // check for podoboo object
    if (zero_flag) { goto MirrorEnemyGfx; } // branch if either of three are found
    cmp_imm(Spiny); // check for spiny object
    if (!zero_flag) { goto ESRtnr; } // branch closer if not found
    cpx_imm(0x5); // check spiny's state
    if (!zero_flag) { goto CheckToMirrorLakitu; } // branch if not an egg, otherwise
  
ESRtnr:
    cmp_imm(0x15); // check for princess/mushroom retainer object
    if (!zero_flag) { goto SpnySC; }
    lda_imm(0x42); // set horizontal flip on bottom right sprite
    write_byte(Sprite_Attributes + 20 + y, a); // note that palette bits were already set earlier
  
SpnySC:
    cpx_imm(0x2); // if alternate enemy state set to 1 or 0, branch
    if (!carry_flag) { goto CheckToMirrorLakitu; }
  
MirrorEnemyGfx:
    lda_abs(BowserGfxFlag); // if enemy object is bowser, skip all of this
    if (!zero_flag) { goto CheckToMirrorLakitu; }
    lda_absy(Sprite_Attributes); // load attribute bits of first sprite
    and_imm(0b10100011);
    write_byte(Sprite_Attributes + y, a); // save vertical flip, priority, and palette bits
    write_byte(Sprite_Attributes + 8 + y, a); // in left sprite column of enemy object OAM data
    write_byte(Sprite_Attributes + 16 + y, a);
    ora_imm(0b01000000); // set horizontal flip
    cpx_imm(0x5); // check for state used by spiny's egg
    if (!zero_flag) { goto EggExc; } // if alternate state not set to $05, branch
    ora_imm(0b10000000); // otherwise set vertical flip
  
EggExc:
    write_byte(Sprite_Attributes + 4 + y, a); // set bits of right sprite column
    write_byte(Sprite_Attributes + 12 + y, a); // of enemy object sprite data
    write_byte(Sprite_Attributes + 20 + y, a);
    cpx_imm(0x4); // check alternate enemy state
    if (!zero_flag) { goto CheckToMirrorLakitu; } // branch if not $04
    lda_absy(Sprite_Attributes + 8); // get second row left sprite attributes
    ora_imm(0b10000000);
    write_byte(Sprite_Attributes + 8 + y, a); // store bits with vertical flip in
    write_byte(Sprite_Attributes + 16 + y, a); // second and third row left sprites
    ora_imm(0b01000000);
    write_byte(Sprite_Attributes + 12 + y, a); // store with horizontal and vertical flip in
    write_byte(Sprite_Attributes + 20 + y, a); // second and third row right sprites
  
CheckToMirrorLakitu:
    lda_zp(0xef); // check for lakitu enemy object
    cmp_imm(Lakitu);
    if (!zero_flag) { goto CheckToMirrorJSpring; } // branch if not found
    lda_abs(VerticalFlipFlag);
    if (!zero_flag) { goto NVFLak; } // branch if vertical flip flag not set
    lda_absy(Sprite_Attributes + 16); // save vertical flip and palette bits
    and_imm(0b10000001); // in third row left sprite
    write_byte(Sprite_Attributes + 16 + y, a);
    lda_absy(Sprite_Attributes + 20); // set horizontal flip and palette bits
    ora_imm(0b01000001); // in third row right sprite
    write_byte(Sprite_Attributes + 20 + y, a);
    ldx_abs(FrenzyEnemyTimer); // check timer
    cpx_imm(0x10);
    if (carry_flag) { goto SprObjectOffscrChk; } // branch if timer has not reached a certain range
    write_byte(Sprite_Attributes + 12 + y, a); // otherwise set same for second row right sprite
    and_imm(0b10000001);
    write_byte(Sprite_Attributes + 8 + y, a); // preserve vertical flip and palette bits for left sprite
    if (!carry_flag) { goto SprObjectOffscrChk; } // unconditional branch
  
NVFLak:
    lda_absy(Sprite_Attributes); // get first row left sprite attributes
    and_imm(0b10000001);
    write_byte(Sprite_Attributes + y, a); // save vertical flip and palette bits
    lda_absy(Sprite_Attributes + 4); // get first row right sprite attributes
    ora_imm(0b01000001); // set horizontal flip and palette bits
    write_byte(Sprite_Attributes + 4 + y, a); // note that vertical flip is left as-is
  
CheckToMirrorJSpring:
    lda_zp(0xef); // check for jumpspring object (any frame)
    cmp_imm(0x18);
    if (!carry_flag) { goto SprObjectOffscrChk; } // branch if not jumpspring object at all
    lda_imm(0x82);
    write_byte(Sprite_Attributes + 8 + y, a); // set vertical flip and palette bits of 
    write_byte(Sprite_Attributes + 16 + y, a); // second and third row left sprites
    ora_imm(0b01000000);
    write_byte(Sprite_Attributes + 12 + y, a); // set, in addition to those, horizontal flip
    write_byte(Sprite_Attributes + 20 + y, a); // for second and third row right sprites
  
SprObjectOffscrChk:
    ldx_zp(ObjectOffset); // get enemy buffer offset
    lda_abs(Enemy_OffscreenBits); // check offscreen information
    lsr_acc();
    lsr_acc(); // shift three times to the right
    lsr_acc(); // which puts d2 into carry
    pha(); // save to stack
    if (!carry_flag) { goto LcChk; } // branch if not set
    lda_imm(0x4); // set for right column sprites
    jsr(MoveESprColOffscreen, 377); // and move them offscreen
  
LcChk:
    pla(); // get from stack
    lsr_acc(); // move d3 to carry
    pha(); // save to stack
    if (!carry_flag) { goto Row3C; } // branch if not set
    lda_imm(0x0); // set for left column sprites,
    jsr(MoveESprColOffscreen, 378); // move them offscreen
  
Row3C:
    pla(); // get from stack again
    lsr_acc(); // move d5 to carry this time
    lsr_acc();
    pha(); // save to stack again
    if (!carry_flag) { goto Row23C; } // branch if carry not set
    lda_imm(0x10); // set for third row of sprites
    jsr(MoveESprRowOffscreen, 379); // and move them offscreen
  
Row23C:
    pla(); // get from stack
    lsr_acc(); // move d6 into carry
    pha(); // save to stack
    if (!carry_flag) { goto AllRowC; }
    lda_imm(0x8); // set for second and third rows
    jsr(MoveESprRowOffscreen, 380); // move them offscreen
  
AllRowC:
    pla(); // get from stack once more
    lsr_acc(); // move d7 into carry
    if (!carry_flag) { goto ExEGHandler; }
    jsr(MoveESprRowOffscreen, 381); // move all sprites offscreen (A should be 0 by now)
    lda_zpx(Enemy_ID);
    cmp_imm(Podoboo); // check enemy identifier for podoboo
    if (zero_flag) { goto ExEGHandler; } // skip this part if found, we do not want to erase podoboo!
    lda_zpx(Enemy_Y_HighPos); // check high byte of vertical position
    cmp_imm(0x2); // if not yet past the bottom of the screen, branch
    if (!zero_flag) { goto ExEGHandler; }
    EraseEnemyObject(); // what it says
  
ExEGHandler:
    goto rts;
  
DrawEnemyObjRow:
    lda_absx(EnemyGraphicsTable); // load two tiles of enemy graphics
    write_byte(0x0, a);
    lda_absx(EnemyGraphicsTable + 1);
  
DrawOneSpriteRow:
    write_byte(0x1, a);
    goto DrawSpriteObject; // draw them
  
MoveESprRowOffscreen:
    carry_flag = false; // add A to enemy object OAM data offset
    adc_absx(Enemy_SprDataOffset);
    tay(); // use as offset
    lda_imm(0xf8);
    goto DumpTwoSpr; // move first row of sprites offscreen
  
MoveESprColOffscreen:
    carry_flag = false; // add A to enemy object OAM data offset
    adc_absx(Enemy_SprDataOffset);
    tay(); // use as offset
    jsr(MoveColOffscreen, 382); // move first and second row sprites in column offscreen
    write_byte(Sprite_Data + 16 + y, a); // move third row sprite in column offscreen
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00-$01 - tile numbers
    // $02 - relative Y position
    // $03 - horizontal flip flag (not used here)
    // $04 - attributes
    // $05 - relative X position
  
DrawBlock:
    lda_abs(Block_Rel_YPos); // get relative vertical coordinate of block object
    write_byte(0x2, a); // store here
    lda_abs(Block_Rel_XPos); // get relative horizontal coordinate of block object
    write_byte(0x5, a); // store here
    lda_imm(0x3);
    write_byte(0x4, a); // set attribute byte here
    lsr_acc();
    write_byte(0x3, a); // set horizontal flip bit here (will not be used)
    ldy_absx(Block_SprDataOffset); // get sprite data offset
    ldx_imm(0x0); // reset X for use as offset to tile data
  
DBlkLoop:
    lda_absx(DefaultBlockObjTiles); // get left tile number
    write_byte(0x0, a); // set here
    lda_absx(DefaultBlockObjTiles + 1); // get right tile number
    jsr(DrawOneSpriteRow, 383); // do sub to write tile numbers to first row of sprites
    cpx_imm(0x4); // check incremented offset
    if (!zero_flag) { goto DBlkLoop; } // and loop back until all four sprites are done
    ldx_zp(ObjectOffset); // get block object offset
    ldy_absx(Block_SprDataOffset); // get sprite data offset
    lda_abs(AreaType);
    cmp_imm(0x1); // check for ground level type area
    if (zero_flag) { goto ChkRep; } // if found, branch to next part
    lda_imm(0x86);
    write_byte(Sprite_Tilenumber + y, a); // otherwise remove brick tiles with lines
    write_byte(Sprite_Tilenumber + 4 + y, a); // and replace then with lineless brick tiles
  
ChkRep:
    lda_absx(Block_Metatile); // check replacement metatile
    cmp_imm(0xc4); // if not used block metatile, then
    if (!zero_flag) { goto BlkOffscr; } // branch ahead to use current graphics
    lda_imm(0x87); // set A for used block tile
    iny(); // increment Y to write to tile bytes
    jsr(DumpFourSpr, 384); // do sub to dump into all four sprites
    dey(); // return Y to original offset
    lda_imm(0x3); // set palette bits
    ldx_abs(AreaType);
    dex(); // check for ground level type area again
    if (zero_flag) { goto SetBFlip; } // if found, use current palette bits
    lsr_acc(); // otherwise set to $01
  
SetBFlip:
    ldx_zp(ObjectOffset); // put block object offset back in X
    write_byte(Sprite_Attributes + y, a); // store attribute byte as-is in first sprite
    ora_imm(0b01000000);
    write_byte(Sprite_Attributes + 4 + y, a); // set horizontal flip bit for second sprite
    ora_imm(0b10000000);
    write_byte(Sprite_Attributes + 12 + y, a); // set both flip bits for fourth sprite
    and_imm(0b10000011);
    write_byte(Sprite_Attributes + 8 + y, a); // set vertical flip bit for third sprite
  
BlkOffscr:
    lda_abs(Block_OffscreenBits); // get offscreen bits for block object
    pha(); // save to stack
    and_imm(0b00000100); // check to see if d2 in offscreen bits are set
    if (zero_flag) { goto PullOfsB; } // if not set, branch, otherwise move sprites offscreen
    lda_imm(0xf8); // move offscreen two OAMs
    write_byte(Sprite_Y_Position + 4 + y, a); // on the right side
    write_byte(Sprite_Y_Position + 12 + y, a);
  
PullOfsB:
    pla(); // pull offscreen bits from stack
  
ChkLeftCo:
    and_imm(0b00001000); // check to see if d3 in offscreen bits are set
    if (zero_flag) { goto ExDBlk; } // if not set, branch, otherwise move sprites offscreen
  
MoveColOffscreen:
    lda_imm(0xf8); // move offscreen two OAMs
    write_byte(Sprite_Y_Position + y, a); // on the left side (or two rows of enemy on either side
    write_byte(Sprite_Y_Position + 8 + y, a); // if branched here from enemy graphics handler)
  
ExDBlk:
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00 - used to hold palette bits for attribute byte or relative X position
  
DrawBrickChunks:
    lda_imm(0x2); // set palette bits here
    write_byte(0x0, a);
    lda_imm(0x75); // set tile number for ball (something residual, likely)
    ldy_zp(GameEngineSubroutine);
    cpy_imm(0x5); // if end-of-level routine running,
    if (zero_flag) { goto DChunks; } // use palette and tile number assigned
    lda_imm(0x3); // otherwise set different palette bits
    write_byte(0x0, a);
    lda_imm(0x84); // and set tile number for brick chunks
  
DChunks:
    ldy_absx(Block_SprDataOffset); // get OAM data offset
    iny(); // increment to start with tile bytes in OAM
    jsr(DumpFourSpr, 385); // do sub to dump tile number into all four sprites
    lda_zp(FrameCounter); // get frame counter
    asl_acc();
    asl_acc();
    asl_acc(); // move low nybble to high
    asl_acc();
    and_imm(0xc0); // get what was originally d3-d2 of low nybble
    ora_zp(0x0); // add palette bits
    iny(); // increment offset for attribute bytes
    jsr(DumpFourSpr, 386); // do sub to dump attribute data into all four sprites
    dey();
    dey(); // decrement offset to Y coordinate
    lda_abs(Block_Rel_YPos); // get first block object's relative vertical coordinate
    jsr(DumpTwoSpr, 387); // do sub to dump current Y coordinate into two sprites
    lda_abs(Block_Rel_XPos); // get first block object's relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a); // save into X coordinate of first sprite
    lda_absx(Block_Orig_XPos); // get original horizontal coordinate
    carry_flag = true;
    sbc_abs(ScreenLeft_X_Pos); // subtract coordinate of left side from original coordinate
    write_byte(0x0, a); // store result as relative horizontal coordinate of original
    carry_flag = true;
    sbc_abs(Block_Rel_XPos); // get difference of relative positions of original - current
    adc_zp(0x0); // add original relative position to result
    adc_imm(0x6); // plus 6 pixels to position second brick chunk correctly
    write_byte(Sprite_X_Position + 4 + y, a); // save into X coordinate of second sprite
    lda_abs(Block_Rel_YPos + 1); // get second block object's relative vertical coordinate
    write_byte(Sprite_Y_Position + 8 + y, a);
    write_byte(Sprite_Y_Position + 12 + y, a); // dump into Y coordinates of third and fourth sprites
    lda_abs(Block_Rel_XPos + 1); // get second block object's relative horizontal coordinate
    write_byte(Sprite_X_Position + 8 + y, a); // save into X coordinate of third sprite
    lda_zp(0x0); // use original relative horizontal position
    carry_flag = true;
    sbc_abs(Block_Rel_XPos + 1); // get difference of relative positions of original - current
    adc_zp(0x0); // add original relative position to result
    adc_imm(0x6); // plus 6 pixels to position fourth brick chunk correctly
    write_byte(Sprite_X_Position + 12 + y, a); // save into X coordinate of fourth sprite
    lda_abs(Block_OffscreenBits); // get offscreen bits for block object
    jsr(ChkLeftCo, 388); // do sub to move left half of sprites offscreen if necessary
    lda_abs(Block_OffscreenBits); // get offscreen bits again
    asl_acc(); // shift d7 into carry
    if (!carry_flag) { goto ChnkOfs; } // if d7 not set, branch to last part
    lda_imm(0xf8);
    jsr(DumpTwoSpr, 389); // otherwise move top sprites offscreen
  
ChnkOfs:
    lda_zp(0x0); // if relative position on left side of screen,
    if (!neg_flag) { goto ExBCDr; } // go ahead and leave
    lda_absy(Sprite_X_Position); // otherwise compare left-side X coordinate
    cmp_absy(Sprite_X_Position + 4); // to right-side X coordinate
    if (!carry_flag) { goto ExBCDr; } // branch to leave if less
    lda_imm(0xf8); // otherwise move right half of sprites offscreen
    write_byte(Sprite_Y_Position + 4 + y, a);
    write_byte(Sprite_Y_Position + 12 + y, a);
  
ExBCDr:
    goto rts; // leave
    // -------------------------------------------------------------------------------------
  
DrawFireball:
    ldy_absx(FBall_SprDataOffset); // get fireball's sprite data offset
    lda_abs(Fireball_Rel_YPos); // get relative vertical coordinate
    write_byte(Sprite_Y_Position + y, a); // store as sprite Y coordinate
    lda_abs(Fireball_Rel_XPos); // get relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a); // store as sprite X coordinate, then do shared code
    DrawFirebar(); goto rts; // <fallthrough>
    // -------------------------------------------------------------------------------------
  
DrawExplosion_Fireball:
    ldy_absx(Alt_SprDataOffset); // get OAM data offset of alternate sort for fireball's explosion
    lda_zpx(Fireball_State); // load fireball state
    inc_zpx(Fireball_State); // increment state for next frame
    lsr_acc(); // divide by 2
    and_imm(0b00000111); // mask out all but d3-d1
    cmp_imm(0x3); // check to see if time to kill fireball
    if (carry_flag) { goto KillFireBall; } // branch if so, otherwise continue to draw explosion
  
DrawExplosion_Fireworks:
    tax(); // use whatever's in A for offset
    lda_absx(ExplosionTiles); // get tile number using offset
    iny(); // increment Y (contains sprite data offset)
    jsr(DumpFourSpr, 390); // and dump into tile number part of sprite data
    dey(); // decrement Y so we have the proper offset again
    ldx_zp(ObjectOffset); // return enemy object buffer offset to X
    lda_abs(Fireball_Rel_YPos); // get relative vertical coordinate
    carry_flag = true; // subtract four pixels vertically
    sbc_imm(0x4); // for first and third sprites
    write_byte(Sprite_Y_Position + y, a);
    write_byte(Sprite_Y_Position + 8 + y, a);
    carry_flag = false; // add eight pixels vertically
    adc_imm(0x8); // for second and fourth sprites
    write_byte(Sprite_Y_Position + 4 + y, a);
    write_byte(Sprite_Y_Position + 12 + y, a);
    lda_abs(Fireball_Rel_XPos); // get relative horizontal coordinate
    carry_flag = true; // subtract four pixels horizontally
    sbc_imm(0x4); // for first and second sprites
    write_byte(Sprite_X_Position + y, a);
    write_byte(Sprite_X_Position + 4 + y, a);
    carry_flag = false; // add eight pixels horizontally
    adc_imm(0x8); // for third and fourth sprites
    write_byte(Sprite_X_Position + 8 + y, a);
    write_byte(Sprite_X_Position + 12 + y, a);
    lda_imm(0x2); // set palette attributes for all sprites, but
    write_byte(Sprite_Attributes + y, a); // set no flip at all for first sprite
    lda_imm(0x82);
    write_byte(Sprite_Attributes + 4 + y, a); // set vertical flip for second sprite
    lda_imm(0x42);
    write_byte(Sprite_Attributes + 8 + y, a); // set horizontal flip for third sprite
    lda_imm(0xc2);
    write_byte(Sprite_Attributes + 12 + y, a); // set both flips for fourth sprite
    goto rts; // we are done
  
KillFireBall:
    lda_imm(0x0); // clear fireball state to kill it
    write_byte(Fireball_State + x, a);
    goto rts;
    // -------------------------------------------------------------------------------------
  
DrawSmallPlatform:
    ldy_absx(Enemy_SprDataOffset); // get OAM data offset
    lda_imm(0x5b); // load tile number for small platforms
    iny(); // increment offset for tile numbers
    jsr(DumpSixSpr, 391); // dump tile number into all six sprites
    iny(); // increment offset for attributes
    lda_imm(0x2); // load palette controls
    jsr(DumpSixSpr, 392); // dump attributes into all six sprites
    dey(); // decrement for original offset
    dey();
    lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
    write_byte(Sprite_X_Position + y, a);
    write_byte(Sprite_X_Position + 12 + y, a); // dump as X coordinate into first and fourth sprites
    carry_flag = false;
    adc_imm(0x8); // add eight pixels
    write_byte(Sprite_X_Position + 4 + y, a); // dump into second and fifth sprites
    write_byte(Sprite_X_Position + 16 + y, a);
    carry_flag = false;
    adc_imm(0x8); // add eight more pixels
    write_byte(Sprite_X_Position + 8 + y, a); // dump into third and sixth sprites
    write_byte(Sprite_X_Position + 20 + y, a);
    lda_zpx(Enemy_Y_Position); // get vertical coordinate
    tax();
    pha(); // save to stack
    cpx_imm(0x20); // if vertical coordinate below status bar,
    if (carry_flag) { goto TopSP; } // do not mess with it
    lda_imm(0xf8); // otherwise move first three sprites offscreen
  
TopSP:
    jsr(DumpThreeSpr, 393); // dump vertical coordinate into Y coordinates
    pla(); // pull from stack
    carry_flag = false;
    adc_imm(0x80); // add 128 pixels
    tax();
    cpx_imm(0x20); // if below status bar (taking wrap into account)
    if (carry_flag) { goto BotSP; } // then do not change altered coordinate
    lda_imm(0xf8); // otherwise move last three sprites offscreen
  
BotSP:
    write_byte(Sprite_Y_Position + 12 + y, a); // dump vertical coordinate + 128 pixels
    write_byte(Sprite_Y_Position + 16 + y, a); // into Y coordinates
    write_byte(Sprite_Y_Position + 20 + y, a);
    lda_abs(Enemy_OffscreenBits); // get offscreen bits
    pha(); // save to stack
    and_imm(0b00001000); // check d3
    if (zero_flag) { goto SOfs; }
    lda_imm(0xf8); // if d3 was set, move first and
    write_byte(Sprite_Y_Position + y, a); // fourth sprites offscreen
    write_byte(Sprite_Y_Position + 12 + y, a);
  
SOfs:
    pla(); // move out and back into stack
    pha();
    and_imm(0b00000100); // check d2
    if (zero_flag) { goto SOfs2; }
    lda_imm(0xf8); // if d2 was set, move second and
    write_byte(Sprite_Y_Position + 4 + y, a); // fifth sprites offscreen
    write_byte(Sprite_Y_Position + 16 + y, a);
  
SOfs2:
    pla(); // get from stack
    and_imm(0b00000010); // check d1
    if (zero_flag) { goto ExSPl; }
    lda_imm(0xf8); // if d1 was set, move third and
    write_byte(Sprite_Y_Position + 8 + y, a); // sixth sprites offscreen
    write_byte(Sprite_Y_Position + 20 + y, a);
  
ExSPl:
    ldx_zp(ObjectOffset); // get enemy object offset and leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------
    // $00 - used to store player's vertical offscreen bits
  
PlayerGfxHandler:
    lda_abs(InjuryTimer); // if player's injured invincibility timer
    if (zero_flag) { goto CntPl; } // not set, skip checkpoint and continue code
    lda_zp(FrameCounter);
    lsr_acc(); // otherwise check frame counter and branch
    if (carry_flag) { goto ExPGH; } // to leave on every other frame (when d0 is set)
  
CntPl:
    lda_zp(GameEngineSubroutine); // if executing specific game engine routine,
    cmp_imm(0xb); // branch ahead to some other part
    if (zero_flag) { goto PlayerKilled; }
    lda_abs(PlayerChangeSizeFlag); // if grow/shrink flag set
    if (!zero_flag) { goto DoChangeSize; } // then branch to some other code
    ldy_abs(SwimmingFlag); // if swimming flag set, branch to
    if (zero_flag) { goto FindPlayerAction; } // different part, do not return
    lda_zp(Player_State);
    cmp_imm(0x0); // if player status normal,
    if (zero_flag) { goto FindPlayerAction; } // branch and do not return
    jsr(FindPlayerAction, 394); // otherwise jump and return
    lda_zp(FrameCounter);
    and_imm(0b00000100); // check frame counter for d2 set (8 frames every
    if (!zero_flag) { goto ExPGH; } // eighth frame), and branch if set to leave
    tax(); // initialize X to zero
    ldy_abs(Player_SprDataOffset); // get player sprite data offset
    lda_zp(PlayerFacingDir); // get player's facing direction
    lsr_acc();
    if (carry_flag) { goto SwimKT; } // if player facing to the right, use current offset
    iny();
    iny(); // otherwise move to next OAM data
    iny();
    iny();
  
SwimKT:
    lda_abs(PlayerSize); // check player's size
    if (zero_flag) { goto BigKTS; } // if big, use first tile
    lda_absy(Sprite_Tilenumber + 24); // check tile number of seventh/eighth sprite
    cmp_abs(SwimTileRepOffset); // against tile number in player graphics table
    if (zero_flag) { goto ExPGH; } // if spr7/spr8 tile number = value, branch to leave
    inx(); // otherwise increment X for second tile
  
BigKTS:
    lda_absx(SwimKickTileNum); // overwrite tile number in sprite 7/8
    write_byte(Sprite_Tilenumber + 24 + y, a); // to animate player's feet when swimming
  
ExPGH:
    goto rts; // then leave
  
FindPlayerAction:
    jsr(ProcessPlayerAction, 395); // find proper offset to graphics table by player's actions
    goto PlayerGfxProcessing; // draw player, then process for fireball throwing
  
DoChangeSize:
    jsr(HandleChangeSize, 396); // find proper offset to graphics table for grow/shrink
    goto PlayerGfxProcessing; // draw player, then process for fireball throwing
  
PlayerKilled:
    ldy_imm(0xe); // load offset for player killed
    lda_absy(PlayerGfxTblOffsets); // get offset to graphics table
  
PlayerGfxProcessing:
    write_byte(PlayerGfxOffset, a); // store offset to graphics table here
    lda_imm(0x4);
    jsr(RenderPlayerSub, 397); // draw player based on offset loaded
    ChkForPlayerAttrib(); // set horizontal flip bits as necessary
    lda_abs(FireballThrowingTimer);
    if (zero_flag) { goto PlayerOffscreenChk; } // if fireball throw timer not set, skip to the end
    ldy_imm(0x0); // set value to initialize by default
    lda_abs(PlayerAnimTimer); // get animation frame timer
    cmp_abs(FireballThrowingTimer); // compare to fireball throw timer
    write_byte(FireballThrowingTimer, y); // initialize fireball throw timer
    if (carry_flag) { goto PlayerOffscreenChk; } // if animation frame timer => fireball throw timer skip to end
    write_byte(FireballThrowingTimer, a); // otherwise store animation timer into fireball throw timer
    ldy_imm(0x7); // load offset for throwing
    lda_absy(PlayerGfxTblOffsets); // get offset to graphics table
    write_byte(PlayerGfxOffset, a); // store it for use later
    ldy_imm(0x4); // set to update four sprite rows by default
    lda_zp(Player_X_Speed);
    ora_zp(Left_Right_Buttons); // check for horizontal speed or left/right button press
    if (zero_flag) { goto SUpdR; } // if no speed or button press, branch using set value in Y
    dey(); // otherwise set to update only three sprite rows
  
SUpdR:
    tya(); // save in A for use
    jsr(RenderPlayerSub, 398); // in sub, draw player object again
  
PlayerOffscreenChk:
    lda_abs(Player_OffscreenBits); // get player's offscreen bits
    lsr_acc();
    lsr_acc(); // move vertical bits to low nybble
    lsr_acc();
    lsr_acc();
    write_byte(0x0, a); // store here
    ldx_imm(0x3); // check all four rows of player sprites
    lda_abs(Player_SprDataOffset); // get player's sprite data offset
    carry_flag = false;
    adc_imm(0x18); // add 24 bytes to start at bottom row
    tay(); // set as offset here
  
PROfsLoop:
    lda_imm(0xf8); // load offscreen Y coordinate just in case
    lsr_zp(0x0); // shift bit into carry
    if (!carry_flag) { goto NPROffscr; } // if bit not set, skip, do not move sprites
    jsr(DumpTwoSpr, 399); // otherwise dump offscreen Y coordinate into sprite data
  
NPROffscr:
    tya();
    carry_flag = true; // subtract eight bytes to do
    sbc_imm(0x8); // next row up
    tay();
    dex(); // decrement row counter
    if (!neg_flag) { goto PROfsLoop; } // do this until all sprite rows are checked
    goto rts; // then we are done!
    // -------------------------------------------------------------------------------------
  
DrawPlayer_Intermediate:
    ldx_imm(0x5); // store data into zero page memory
  
PIntLoop:
    lda_absx(IntermediatePlayerData); // load data to display player as he always
    write_byte(0x2 + x, a); // appears on world/lives display
    dex();
    if (!neg_flag) { goto PIntLoop; } // do this until all data is loaded
    ldx_imm(0xb8); // load offset for small standing
    ldy_imm(0x4); // load sprite data offset
    jsr(DrawPlayerLoop, 400); // draw player accordingly
    lda_abs(Sprite_Attributes + 36); // get empty sprite attributes
    ora_imm(0b01000000); // set horizontal flip bit for bottom-right sprite
    write_byte(Sprite_Attributes + 32, a); // store and leave
    goto rts;
    // -------------------------------------------------------------------------------------
    // $00-$01 - used to hold tile numbers, $00 also used to hold upper extent of animation frames
    // $02 - vertical position
    // $03 - facing direction, used as horizontal flip control
    // $04 - attributes
    // $05 - horizontal position
    // $07 - number of rows to draw
    // these also used in IntermediatePlayerData
  
RenderPlayerSub:
    write_byte(0x7, a); // store number of rows of sprites to draw
    lda_abs(Player_Rel_XPos);
    write_byte(Player_Pos_ForScroll, a); // store player's relative horizontal position
    write_byte(0x5, a); // store it here also
    lda_abs(Player_Rel_YPos);
    write_byte(0x2, a); // store player's vertical position
    lda_zp(PlayerFacingDir);
    write_byte(0x3, a); // store player's facing direction
    lda_abs(Player_SprAttrib);
    write_byte(0x4, a); // store player's sprite attributes
    ldx_abs(PlayerGfxOffset); // load graphics table offset
    ldy_abs(Player_SprDataOffset); // get player's sprite data offset
  
DrawPlayerLoop:
    lda_absx(PlayerGraphicsTable); // load player's left side
    write_byte(0x0, a);
    lda_absx(PlayerGraphicsTable + 1); // now load right side
    jsr(DrawOneSpriteRow, 401);
    dec_zp(0x7); // decrement rows of sprites to draw
    if (!zero_flag) { goto DrawPlayerLoop; } // do this until all rows are drawn
    goto rts;
  
ProcessPlayerAction:
    lda_zp(Player_State); // get player's state
    cmp_imm(0x3);
    if (zero_flag) { goto ActionClimbing; } // if climbing, branch here
    cmp_imm(0x2);
    if (zero_flag) { goto ActionFalling; } // if falling, branch here
    cmp_imm(0x1);
    if (!zero_flag) { goto ProcOnGroundActs; } // if not jumping, branch here
    lda_abs(SwimmingFlag);
    if (!zero_flag) { goto ActionSwimming; } // if swimming flag set, branch elsewhere
    ldy_imm(0x6); // load offset for crouching
    lda_abs(CrouchingFlag); // get crouching flag
    if (!zero_flag) { goto NonAnimatedActs; } // if set, branch to get offset for graphics table
    ldy_imm(0x0); // otherwise load offset for jumping
    goto NonAnimatedActs; // go to get offset to graphics table
  
ProcOnGroundActs:
    ldy_imm(0x6); // load offset for crouching
    lda_abs(CrouchingFlag); // get crouching flag
    if (!zero_flag) { goto NonAnimatedActs; } // if set, branch to get offset for graphics table
    ldy_imm(0x2); // load offset for standing
    lda_zp(Player_X_Speed); // check player's horizontal speed
    ora_zp(Left_Right_Buttons); // and left/right controller bits
    if (zero_flag) { goto NonAnimatedActs; } // if no speed or buttons pressed, use standing offset
    lda_abs(Player_XSpeedAbsolute); // load walking/running speed
    cmp_imm(0x9);
    if (!carry_flag) { goto ActionWalkRun; } // if less than a certain amount, branch, too slow to skid
    lda_zp(Player_MovingDir); // otherwise check to see if moving direction
    and_zp(PlayerFacingDir); // and facing direction are the same
    if (!zero_flag) { goto ActionWalkRun; } // if moving direction = facing direction, branch, don't skid
    iny(); // otherwise increment to skid offset ($03)
  
NonAnimatedActs:
    GetGfxOffsetAdder(); // do a sub here to get offset adder for graphics table
    lda_imm(0x0);
    write_byte(PlayerAnimCtrl, a); // initialize animation frame control
    lda_absy(PlayerGfxTblOffsets); // load offset to graphics table using size as offset
    goto rts;
  
ActionFalling:
    ldy_imm(0x4); // load offset for walking/running
    GetGfxOffsetAdder(); // get offset to graphics table
    goto GetCurrentAnimOffset; // execute instructions for falling state
  
ActionWalkRun:
    ldy_imm(0x4); // load offset for walking/running
    GetGfxOffsetAdder(); // get offset to graphics table
    goto FourFrameExtent; // execute instructions for normal state
  
ActionClimbing:
    ldy_imm(0x5); // load offset for climbing
    lda_zp(Player_Y_Speed); // check player's vertical speed
    if (zero_flag) { goto NonAnimatedActs; } // if no speed, branch, use offset as-is
    GetGfxOffsetAdder(); // otherwise get offset for graphics table
    goto ThreeFrameExtent; // then skip ahead to more code
  
ActionSwimming:
    ldy_imm(0x1); // load offset for swimming
    GetGfxOffsetAdder();
    lda_abs(JumpSwimTimer); // check jump/swim timer
    ora_abs(PlayerAnimCtrl); // and animation frame control
    if (!zero_flag) { goto FourFrameExtent; } // if any one of these set, branch ahead
    lda_zp(A_B_Buttons);
    asl_acc(); // check for A button pressed
    if (carry_flag) { goto FourFrameExtent; } // branch to same place if A button pressed
  
GetCurrentAnimOffset:
    lda_abs(PlayerAnimCtrl); // get animation frame control
    goto GetOffsetFromAnimCtrl; // jump to get proper offset to graphics table
  
FourFrameExtent:
    lda_imm(0x3); // load upper extent for frame control
    goto AnimationControl; // jump to get offset and animate player object
  
ThreeFrameExtent:
    lda_imm(0x2); // load upper extent for frame control for climbing
  
AnimationControl:
    write_byte(0x0, a); // store upper extent here
    jsr(GetCurrentAnimOffset, 402); // get proper offset to graphics table
    pha(); // save offset to stack
    lda_abs(PlayerAnimTimer); // load animation frame timer
    if (!zero_flag) { goto ExAnimC; } // branch if not expired
    lda_abs(PlayerAnimTimerSet); // get animation frame timer amount
    write_byte(PlayerAnimTimer, a); // and set timer accordingly
    lda_abs(PlayerAnimCtrl);
    carry_flag = false; // add one to animation frame control
    adc_imm(0x1);
    cmp_zp(0x0); // compare to upper extent
    if (!carry_flag) { goto SetAnimC; } // if frame control + 1 < upper extent, use as next
    lda_imm(0x0); // otherwise initialize frame control
  
SetAnimC:
    write_byte(PlayerAnimCtrl, a); // store as new animation frame control
  
ExAnimC:
    pla(); // get offset to graphics table from stack and leave
    goto rts;
  
HandleChangeSize:
    ldy_abs(PlayerAnimCtrl); // get animation frame control
    lda_zp(FrameCounter);
    and_imm(0b00000011); // get frame counter and execute this code every
    if (!zero_flag) { goto GorSLog; } // fourth frame, otherwise branch ahead
    iny(); // increment frame control
    cpy_imm(0xa); // check for preset upper extent
    if (!carry_flag) { goto CSzNext; } // if not there yet, skip ahead to use
    ldy_imm(0x0); // otherwise initialize both grow/shrink flag
    write_byte(PlayerChangeSizeFlag, y); // and animation frame control
  
CSzNext:
    write_byte(PlayerAnimCtrl, y); // store proper frame control
  
GorSLog:
    lda_abs(PlayerSize); // get player's size
    if (!zero_flag) { goto ShrinkPlayer; } // if player small, skip ahead to next part
    lda_absy(ChangeSizeOffsetAdder); // get offset adder based on frame control as offset
    ldy_imm(0xf); // load offset for player growing
  
GetOffsetFromAnimCtrl:
    asl_acc(); // multiply animation frame control
    asl_acc(); // by eight to get proper amount
    asl_acc(); // to add to our offset
    adc_absy(PlayerGfxTblOffsets); // add to offset to graphics table
    goto rts; // and return with result in A
  
ShrinkPlayer:
    tya(); // add ten bytes to frame control as offset
    carry_flag = false;
    adc_imm(0xa); // this thing apparently uses two of the swimming frames
    tax(); // to draw the player shrinking
    ldy_imm(0x9); // load offset for small player swimming
    lda_absx(ChangeSizeOffsetAdder); // get what would normally be offset adder
    if (!zero_flag) { goto ShrPlF; } // and branch to use offset if nonzero
    ldy_imm(0x1); // otherwise load offset for big player swimming
  
ShrPlF:
    lda_absy(PlayerGfxTblOffsets); // get offset to graphics table based on offset loaded
    goto rts; // and leave
    // -------------------------------------------------------------------------------------
    // $00 - used in adding to get proper offset
  
RelativePlayerPosition:
    ldx_imm(0x0); // set offsets for relative cooordinates
    ldy_imm(0x0); // routine to correspond to player object
    goto RelWOfs; // get the coordinates
  
RelativeBubblePosition:
    ldy_imm(0x1); // set for air bubble offsets
    jsr(GetProperObjOffset, 403); // modify X to get proper air bubble offset
    ldy_imm(0x3);
    goto RelWOfs; // get the coordinates
  
RelativeFireballPosition:
    ldy_imm(0x0); // set for fireball offsets
    jsr(GetProperObjOffset, 404); // modify X to get proper fireball offset
    ldy_imm(0x2);
  
RelWOfs:
    GetObjRelativePosition(); // get the coordinates
    ldx_zp(ObjectOffset); // return original offset
    goto rts; // leave
  
RelativeMiscPosition:
    ldy_imm(0x2); // set for misc object offsets
    jsr(GetProperObjOffset, 405); // modify X to get proper misc object offset
    ldy_imm(0x6);
    goto RelWOfs; // get the coordinates
  
RelativeEnemyPosition:
    lda_imm(0x1); // get coordinates of enemy object 
    ldy_imm(0x1); // relative to the screen
    goto VariableObjOfsRelPos;
  
RelativeBlockPosition:
    lda_imm(0x9); // get coordinates of one block object
    ldy_imm(0x4); // relative to the screen
    jsr(VariableObjOfsRelPos, 406);
    inx(); // adjust offset for other block object if any
    inx();
    lda_imm(0x9);
    iny(); // adjust other and get coordinates for other one
  
VariableObjOfsRelPos:
    write_byte(0x0, x); // store value to add to A here
    carry_flag = false;
    adc_zp(0x0); // add A to value stored
    tax(); // use as enemy offset
    GetObjRelativePosition();
    ldx_zp(ObjectOffset); // reload old object offset and leave
    goto rts;
  
GetPlayerOffscreenBits:
    ldx_imm(0x0); // set offsets for player-specific variables
    ldy_imm(0x0); // and get offscreen information about player
    goto GetOffScreenBitsSet;
  
GetFireballOffscreenBits:
    ldy_imm(0x0); // set for fireball offsets
    jsr(GetProperObjOffset, 407); // modify X to get proper fireball offset
    ldy_imm(0x2); // set other offset for fireball's offscreen bits
    goto GetOffScreenBitsSet; // and get offscreen information about fireball
  
GetBubbleOffscreenBits:
    ldy_imm(0x1); // set for air bubble offsets
    jsr(GetProperObjOffset, 408); // modify X to get proper air bubble offset
    ldy_imm(0x3); // set other offset for airbubble's offscreen bits
    goto GetOffScreenBitsSet; // and get offscreen information about air bubble
  
GetMiscOffscreenBits:
    ldy_imm(0x2); // set for misc object offsets
    jsr(GetProperObjOffset, 409); // modify X to get proper misc object offset
    ldy_imm(0x6); // set other offset for misc object's offscreen bits
    goto GetOffScreenBitsSet; // and get offscreen information about misc object
  
GetProperObjOffset:
    txa(); // move offset to A
    carry_flag = false;
    adc_absy(ObjOffsetData); // add amount of bytes to offset depending on setting in Y
    tax(); // put back in X and leave
    goto rts;
  
GetEnemyOffscreenBits:
    lda_imm(0x1); // set A to add 1 byte in order to get enemy offset
    ldy_imm(0x1); // set Y to put offscreen bits in Enemy_OffscreenBits
    goto SetOffscrBitsOffset;
  
GetBlockOffscreenBits:
    lda_imm(0x9); // set A to add 9 bytes in order to get block obj offset
    ldy_imm(0x4); // set Y to put offscreen bits in Block_OffscreenBits
  
SetOffscrBitsOffset:
    write_byte(0x0, x);
    carry_flag = false; // add contents of X to A to get
    adc_zp(0x0); // appropriate offset, then give back to X
    tax();
  
GetOffScreenBitsSet:
    tya(); // save offscreen bits offset to stack for now
    pha();
    jsr(RunOffscrBitsSubs, 410);
    asl_acc(); // move low nybble to high nybble
    asl_acc();
    asl_acc();
    asl_acc();
    ora_zp(0x0); // mask together with previously saved low nybble
    write_byte(0x0, a); // store both here
    pla(); // get offscreen bits offset from stack
    tay();
    lda_zp(0x0); // get value here and store elsewhere
    write_byte(SprObject_OffscrBits + y, a);
    ldx_zp(ObjectOffset);
    goto rts;
  
RunOffscrBitsSubs:
    GetXOffscreenBits(); // do subroutine here
    lsr_acc(); // move high nybble to low
    lsr_acc();
    lsr_acc();
    lsr_acc();
    write_byte(0x0, a); // store here
    goto GetYOffscreenBits;
    // --------------------------------
    // (these apply to these three subsections)
    // $04 - used to store proper offset
    // $05 - used as adder in DividePDiff
    // $06 - used to store preset value used to compare to pixel difference in $07
    // $07 - used to store difference between coordinates of object and screen edges
    // --------------------------------
  
GetYOffscreenBits:
    write_byte(0x4, x); // save position in buffer to here
    ldy_imm(0x1); // start with top of screen
  
YOfsLoop:
    lda_absy(HighPosUnitData); // load coordinate for edge of vertical unit
    carry_flag = true;
    sbc_zpx(SprObject_Y_Position); // subtract from vertical coordinate of object
    write_byte(0x7, a); // store here
    lda_imm(0x1); // subtract one from vertical high byte of object
    sbc_zpx(SprObject_Y_HighPos);
    ldx_absy(DefaultYOnscreenOfs); // load offset value here
    cmp_imm(0x0);
    if (neg_flag) { goto YLdBData; } // if under top of the screen or beyond bottom, branch
    ldx_absy(DefaultYOnscreenOfs + 1); // if not, load alternate offset value here
    cmp_imm(0x1);
    if (!neg_flag) { goto YLdBData; } // if one vertical unit or more above the screen, branch
    lda_imm(0x20); // if no branching, load value here and store
    write_byte(0x6, a);
    lda_imm(0x4); // load some other value and execute subroutine
    DividePDiff();
  
YLdBData:
    lda_absx(YOffscreenBitsData); // get offscreen data bits using offset
    ldx_zp(0x4); // reobtain position in buffer
    cmp_imm(0x0);
    if (!zero_flag) { goto ExYOfsBS; } // if bits not zero, branch to leave
    dey(); // otherwise, do bottom of the screen now
    if (!neg_flag) { goto YOfsLoop; }
  
ExYOfsBS:
    goto rts;
    // --------------------------------
    // -------------------------------------------------------------------------------------
    // $00-$01 - tile numbers
    // $02 - Y coordinate
    // $03 - flip control
    // $04 - sprite attributes
    // $05 - X coordinate
  
DrawSpriteObject:
    lda_zp(0x3); // get saved flip control bits
    lsr_acc();
    lsr_acc(); // move d1 into carry
    lda_zp(0x0);
    if (!carry_flag) { goto NoHFlip; } // if d1 not set, branch
    write_byte(Sprite_Tilenumber + 4 + y, a); // store first tile into second sprite
    lda_zp(0x1); // and second into first sprite
    write_byte(Sprite_Tilenumber + y, a);
    lda_imm(0x40); // activate horizontal flip OAM attribute
    if (!zero_flag) { goto SetHFAt; } // and unconditionally branch
  
NoHFlip:
    write_byte(Sprite_Tilenumber + y, a); // store first tile into first sprite
    lda_zp(0x1); // and second into second sprite
    write_byte(Sprite_Tilenumber + 4 + y, a);
    lda_imm(0x0); // clear bit for horizontal flip
  
SetHFAt:
    ora_zp(0x4); // add other OAM attributes if necessary
    write_byte(Sprite_Attributes + y, a); // store sprite attributes
    write_byte(Sprite_Attributes + 4 + y, a);
    lda_zp(0x2); // now the y coordinates
    write_byte(Sprite_Y_Position + y, a); // note because they are
    write_byte(Sprite_Y_Position + 4 + y, a); // side by side, they are the same
    lda_zp(0x5);
    write_byte(Sprite_X_Position + y, a); // store x coordinate, then
    carry_flag = false; // add 8 pixels and store another to
    adc_imm(0x8); // put them side by side
    write_byte(Sprite_X_Position + 4 + y, a);
    lda_zp(0x2); // add eight pixels to the next y
    carry_flag = false; // coordinate
    adc_imm(0x8);
    write_byte(0x2, a);
    tya(); // add eight to the offset in Y to
    carry_flag = false; // move to the next two sprites
    adc_imm(0x8);
    tay();
    inx(); // increment offset to return it to the
    inx(); // routine that called this subroutine
    goto rts;
    // -------------------------------------------------------------------------------------
    // unused space
    //         .db $ff, $ff, $ff, $ff, $ff, $ff
    // -------------------------------------------------------------------------------------
  
SoundEngine:
    lda_abs(OperMode); // are we in title screen mode?
    if (!zero_flag) { goto SndOn; }
    write_byte(SND_MASTERCTRL_REG, a); // if so, disable sound and leave
    goto rts;
  
SndOn:
    lda_imm(0xff);
    write_byte(JOYPAD_PORT2, a); // disable irqs and set frame counter mode???
    lda_imm(0xf);
    write_byte(SND_MASTERCTRL_REG, a); // enable first four channels
    lda_abs(PauseModeFlag); // is sound already in pause mode?
    if (!zero_flag) { goto InPause; }
    lda_zp(PauseSoundQueue); // if not, check pause sfx queue    
    cmp_imm(0x1);
    if (!zero_flag) { goto RunSoundSubroutines; } // if queue is empty, skip pause mode routine
  
InPause:
    lda_abs(PauseSoundBuffer); // check pause sfx buffer
    if (!zero_flag) { goto ContPau; }
    lda_zp(PauseSoundQueue); // check pause queue
    if (zero_flag) { goto SkipSoundSubroutines; }
    write_byte(PauseSoundBuffer, a); // if queue full, store in buffer and activate
    write_byte(PauseModeFlag, a); // pause mode to interrupt game sounds
    lda_imm(0x0); // disable sound and clear sfx buffers
    write_byte(SND_MASTERCTRL_REG, a);
    write_byte(Square1SoundBuffer, a);
    write_byte(Square2SoundBuffer, a);
    write_byte(NoiseSoundBuffer, a);
    lda_imm(0xf);
    write_byte(SND_MASTERCTRL_REG, a); // enable sound again
    lda_imm(0x2a); // store length of sound in pause counter
    write_byte(Squ1_SfxLenCounter, a);
  
PTone1F:
    lda_imm(0x44); // play first tone
    if (!zero_flag) { goto PTRegC; } // unconditional branch
  
ContPau:
    lda_abs(Squ1_SfxLenCounter); // check pause length left
    cmp_imm(0x24); // time to play second?
    if (zero_flag) { goto PTone2F; }
    cmp_imm(0x1e); // time to play first again?
    if (zero_flag) { goto PTone1F; }
    cmp_imm(0x18); // time to play second again?
    if (!zero_flag) { goto DecPauC; } // only load regs during times, otherwise skip
  
PTone2F:
    lda_imm(0x64); // store reg contents and play the pause sfx
  
PTRegC:
    ldx_imm(0x84);
    ldy_imm(0x7f);
    jsr(PlaySqu1Sfx, 411);
  
DecPauC:
    dec_abs(Squ1_SfxLenCounter); // decrement pause sfx counter
    if (!zero_flag) { goto SkipSoundSubroutines; }
    lda_imm(0x0); // disable sound if in pause mode and
    write_byte(SND_MASTERCTRL_REG, a); // not currently playing the pause sfx
    lda_abs(PauseSoundBuffer); // if no longer playing pause sfx, check to see
    cmp_imm(0x2); // if we need to be playing sound again
    if (!zero_flag) { goto SkipPIn; }
    lda_imm(0x0); // clear pause mode to allow game sounds again
    write_byte(PauseModeFlag, a);
  
SkipPIn:
    lda_imm(0x0); // clear pause sfx buffer
    write_byte(PauseSoundBuffer, a);
    if (zero_flag) { goto SkipSoundSubroutines; }
  
RunSoundSubroutines:
    jsr(Square1SfxHandler, 412); // play sfx on square channel 1
    jsr(Square2SfxHandler, 413); //  ''  ''  '' square channel 2
    jsr(NoiseSfxHandler, 414); //  ''  ''  '' noise channel
    jsr(MusicHandler, 415); // play music on all channels
    lda_imm(0x0); // clear the music queues
    write_byte(AreaMusicQueue, a);
    write_byte(EventMusicQueue, a);
  
SkipSoundSubroutines:
    lda_imm(0x0); // clear the sound effects queues
    write_byte(Square1SoundQueue, a);
    write_byte(Square2SoundQueue, a);
    write_byte(NoiseSoundQueue, a);
    write_byte(PauseSoundQueue, a);
    ldy_abs(DAC_Counter); // load some sort of counter 
    lda_zp(AreaMusicBuffer);
    and_imm(0b00000011); // check for specific music
    if (zero_flag) { goto NoIncDAC; }
    inc_abs(DAC_Counter); // increment and check counter
    cpy_imm(0x30);
    if (!carry_flag) { goto StrWave; } // if not there yet, just store it
  
NoIncDAC:
    tya();
    if (zero_flag) { goto StrWave; } // if we are at zero, do not decrement 
    dec_abs(DAC_Counter); // decrement counter
  
StrWave:
    write_byte(SND_DELTA_REG + 1, y); // store into DMC load register (??)
    goto rts; // we are done here
    // --------------------------------
  
Dump_Squ1_Regs:
    write_byte(SND_SQUARE1_REG + 1, y); // dump the contents of X and Y into square 1's control regs
    write_byte(SND_SQUARE1_REG, x);
    goto rts;
  
PlaySqu1Sfx:
    jsr(Dump_Squ1_Regs, 416); // do sub to set ctrl regs for square 1, then set frequency regs
  
SetFreq_Squ1:
    ldx_imm(0x0); // set frequency reg offset for square 1 sound channel
  
Dump_Freq_Regs:
    tay();
    lda_absy(FreqRegLookupTbl + 1); // use previous contents of A for sound reg offset
    if (zero_flag) { goto NoTone; } // if zero, then do not load
    write_byte(SND_REGISTER + 2 + x, a); // first byte goes into LSB of frequency divider
    lda_absy(FreqRegLookupTbl); // second byte goes into 3 MSB plus extra bit for 
    ora_imm(0b00001000); // length counter
    write_byte(SND_REGISTER + 3 + x, a);
  
NoTone:
    goto rts;
  
Dump_Sq2_Regs:
    write_byte(SND_SQUARE2_REG, x); // dump the contents of X and Y into square 2's control regs
    write_byte(SND_SQUARE2_REG + 1, y);
    goto rts;
  
PlaySqu2Sfx:
    jsr(Dump_Sq2_Regs, 417); // do sub to set ctrl regs for square 2, then set frequency regs
  
SetFreq_Squ2:
    ldx_imm(0x4); // set frequency reg offset for square 2 sound channel
    if (!zero_flag) { goto Dump_Freq_Regs; } // unconditional branch
  
SetFreq_Tri:
    ldx_imm(0x8); // set frequency reg offset for triangle sound channel
    if (!zero_flag) { goto Dump_Freq_Regs; } // unconditional branch
    // --------------------------------
  
PlayFlagpoleSlide:
    lda_imm(0x40); // store length of flagpole sound
    write_byte(Squ1_SfxLenCounter, a);
    lda_imm(0x62); // load part of reg contents for flagpole sound
    jsr(SetFreq_Squ1, 418);
    ldx_imm(0x99); // now load the rest
    if (!zero_flag) { goto FPS2nd; }
  
PlaySmallJump:
    lda_imm(0x26); // branch here for small mario jumping sound
    if (!zero_flag) { goto JumpRegContents; }
  
PlayBigJump:
    lda_imm(0x18); // branch here for big mario jumping sound
  
JumpRegContents:
    ldx_imm(0x82); // note that small and big jump borrow each others' reg contents
    ldy_imm(0xa7); // anyway, this loads the first part of mario's jumping sound
    jsr(PlaySqu1Sfx, 419);
    lda_imm(0x28); // store length of sfx for both jumping sounds
    write_byte(Squ1_SfxLenCounter, a); // then continue on here
  
ContinueSndJump:
    lda_abs(Squ1_SfxLenCounter); // jumping sounds seem to be composed of three parts
    cmp_imm(0x25); // check for time to play second part yet
    if (!zero_flag) { goto N2Prt; }
    ldx_imm(0x5f); // load second part
    ldy_imm(0xf6);
    if (!zero_flag) { goto DmpJpFPS; } // unconditional branch
  
N2Prt:
    cmp_imm(0x20); // check for third part
    if (!zero_flag) { goto DecJpFPS; }
    ldx_imm(0x48); // load third part
  
FPS2nd:
    ldy_imm(0xbc); // the flagpole slide sound shares part of third part
  
DmpJpFPS:
    jsr(Dump_Squ1_Regs, 420);
    if (!zero_flag) { goto DecJpFPS; } // unconditional branch outta here
  
PlayFireballThrow:
    lda_imm(0x5);
    ldy_imm(0x99); // load reg contents for fireball throw sound
    if (!zero_flag) { goto Fthrow; } // unconditional branch
  
PlayBump:
    lda_imm(0xa); // load length of sfx and reg contents for bump sound
    ldy_imm(0x93);
  
Fthrow:
    ldx_imm(0x9e); // the fireball sound shares reg contents with the bump sound
    write_byte(Squ1_SfxLenCounter, a);
    lda_imm(0xc); // load offset for bump sound
    jsr(PlaySqu1Sfx, 421);
  
ContinueBumpThrow:
    lda_abs(Squ1_SfxLenCounter); // check for second part of bump sound
    cmp_imm(0x6);
    if (!zero_flag) { goto DecJpFPS; }
    lda_imm(0xbb); // load second part directly
    write_byte(SND_SQUARE1_REG + 1, a);
  
DecJpFPS:
    if (!zero_flag) { goto BranchToDecLength1; } // unconditional branch
  
Square1SfxHandler:
    ldy_zp(Square1SoundQueue); // check for sfx in queue
    if (zero_flag) { goto CheckSfx1Buffer; }
    write_byte(Square1SoundBuffer, y); // if found, put in buffer
    if (neg_flag) { goto PlaySmallJump; } // small jump
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlayBigJump; } // big jump
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlayBump; } // bump
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlaySwimStomp; } // swim/stomp
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlaySmackEnemy; } // smack enemy
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlayPipeDownInj; } // pipedown/injury
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlayFireballThrow; } // fireball throw
    lsr_zp(Square1SoundQueue);
    if (carry_flag) { goto PlayFlagpoleSlide; } // slide flagpole
  
CheckSfx1Buffer:
    lda_zp(Square1SoundBuffer); // check for sfx in buffer 
    if (zero_flag) { goto ExS1H; } // if not found, exit sub
    if (neg_flag) { goto ContinueSndJump; } // small mario jump 
    lsr_acc();
    if (carry_flag) { goto ContinueSndJump; } // big mario jump 
    lsr_acc();
    if (carry_flag) { goto ContinueBumpThrow; } // bump
    lsr_acc();
    if (carry_flag) { goto ContinueSwimStomp; } // swim/stomp
    lsr_acc();
    if (carry_flag) { goto ContinueSmackEnemy; } // smack enemy
    lsr_acc();
    if (carry_flag) { goto ContinuePipeDownInj; } // pipedown/injury
    lsr_acc();
    if (carry_flag) { goto ContinueBumpThrow; } // fireball throw
    lsr_acc();
    if (carry_flag) { goto DecrementSfx1Length; } // slide flagpole
  
ExS1H:
    goto rts;
  
PlaySwimStomp:
    lda_imm(0xe); // store length of swim/stomp sound
    write_byte(Squ1_SfxLenCounter, a);
    ldy_imm(0x9c); // store reg contents for swim/stomp sound
    ldx_imm(0x9e);
    lda_imm(0x26);
    jsr(PlaySqu1Sfx, 422);
  
ContinueSwimStomp:
    ldy_abs(Squ1_SfxLenCounter); // look up reg contents in data section based on
    lda_absy(SwimStompEnvelopeData - 1); // length of sound left, used to control sound's
    write_byte(SND_SQUARE1_REG, a); // envelope
    cpy_imm(0x6);
    if (!zero_flag) { goto BranchToDecLength1; }
    lda_imm(0x9e); // when the length counts down to a certain point, put this
    write_byte(SND_SQUARE1_REG + 2, a); // directly into the LSB of square 1's frequency divider
  
BranchToDecLength1:
    if (!zero_flag) { goto DecrementSfx1Length; } // unconditional branch (regardless of how we got here)
  
PlaySmackEnemy:
    lda_imm(0xe); // store length of smack enemy sound
    ldy_imm(0xcb);
    ldx_imm(0x9f);
    write_byte(Squ1_SfxLenCounter, a);
    lda_imm(0x28); // store reg contents for smack enemy sound
    jsr(PlaySqu1Sfx, 423);
    if (!zero_flag) { goto DecrementSfx1Length; } // unconditional branch
  
ContinueSmackEnemy:
    ldy_abs(Squ1_SfxLenCounter); // check about halfway through
    cpy_imm(0x8);
    if (!zero_flag) { goto SmSpc; }
    lda_imm(0xa0); // if we're at the about-halfway point, make the second tone
    write_byte(SND_SQUARE1_REG + 2, a); // in the smack enemy sound
    lda_imm(0x9f);
    if (!zero_flag) { goto SmTick; }
  
SmSpc:
    lda_imm(0x90); // this creates spaces in the sound, giving it its distinct noise
  
SmTick:
    write_byte(SND_SQUARE1_REG, a);
  
DecrementSfx1Length:
    dec_abs(Squ1_SfxLenCounter); // decrement length of sfx
    if (!zero_flag) { goto ExSfx1; }
  
StopSquare1Sfx:
    ldx_imm(0x0); // if end of sfx reached, clear buffer
    write_byte(0xf1, x); // and stop making the sfx
    ldx_imm(0xe);
    write_byte(SND_MASTERCTRL_REG, x);
    ldx_imm(0xf);
    write_byte(SND_MASTERCTRL_REG, x);
  
ExSfx1:
    goto rts;
  
PlayPipeDownInj:
    lda_imm(0x2f); // load length of pipedown sound
    write_byte(Squ1_SfxLenCounter, a);
  
ContinuePipeDownInj:
    lda_abs(Squ1_SfxLenCounter); // some bitwise logic, forces the regs
    lsr_acc(); // to be written to only during six specific times
    if (carry_flag) { goto NoPDwnL; } // during which d3 must be set and d1-0 must be clear
    lsr_acc();
    if (carry_flag) { goto NoPDwnL; }
    and_imm(0b00000010);
    if (zero_flag) { goto NoPDwnL; }
    ldy_imm(0x91); // and this is where it actually gets written in
    ldx_imm(0x9a);
    lda_imm(0x44);
    jsr(PlaySqu1Sfx, 424);
  
NoPDwnL:
    goto DecrementSfx1Length;
    // --------------------------------
  
PlayCoinGrab:
    lda_imm(0x35); // load length of coin grab sound
    ldx_imm(0x8d); // and part of reg contents
    if (!zero_flag) { goto CGrab_TTickRegL; }
  
PlayTimerTick:
    lda_imm(0x6); // load length of timer tick sound
    ldx_imm(0x98); // and part of reg contents
  
CGrab_TTickRegL:
    write_byte(Squ2_SfxLenCounter, a);
    ldy_imm(0x7f); // load the rest of reg contents 
    lda_imm(0x42); // of coin grab and timer tick sound
    jsr(PlaySqu2Sfx, 425);
  
ContinueCGrabTTick:
    lda_abs(Squ2_SfxLenCounter); // check for time to play second tone yet
    cmp_imm(0x30); // timer tick sound also executes this, not sure why
    if (!zero_flag) { goto N2Tone; }
    lda_imm(0x54); // if so, load the tone directly into the reg
    write_byte(SND_SQUARE2_REG + 2, a);
  
N2Tone:
    if (!zero_flag) { goto DecrementSfx2Length; }
  
PlayBlast:
    lda_imm(0x20); // load length of fireworks/gunfire sound
    write_byte(Squ2_SfxLenCounter, a);
    ldy_imm(0x94); // load reg contents of fireworks/gunfire sound
    lda_imm(0x5e);
    if (!zero_flag) { goto SBlasJ; }
  
ContinueBlast:
    lda_abs(Squ2_SfxLenCounter); // check for time to play second part
    cmp_imm(0x18);
    if (!zero_flag) { goto DecrementSfx2Length; }
    ldy_imm(0x93); // load second part reg contents then
    lda_imm(0x18);
  
SBlasJ:
    if (!zero_flag) { goto BlstSJp; } // unconditional branch to load rest of reg contents
  
PlayPowerUpGrab:
    lda_imm(0x36); // load length of power-up grab sound
    write_byte(Squ2_SfxLenCounter, a);
  
ContinuePowerUpGrab:
    lda_abs(Squ2_SfxLenCounter); // load frequency reg based on length left over
    lsr_acc(); // divide by 2
    if (carry_flag) { goto DecrementSfx2Length; } // alter frequency every other frame
    tay();
    lda_absy(PowerUpGrabFreqData - 1); // use length left over / 2 for frequency offset
    ldx_imm(0x5d); // store reg contents of power-up grab sound
    ldy_imm(0x7f);
  
LoadSqu2Regs:
    jsr(PlaySqu2Sfx, 426);
  
DecrementSfx2Length:
    dec_abs(Squ2_SfxLenCounter); // decrement length of sfx
    if (!zero_flag) { goto ExSfx2; }
  
EmptySfx2Buffer:
    ldx_imm(0x0); // initialize square 2's sound effects buffer
    write_byte(Square2SoundBuffer, x);
  
StopSquare2Sfx:
    ldx_imm(0xd); // stop playing the sfx
    write_byte(SND_MASTERCTRL_REG, x);
    ldx_imm(0xf);
    write_byte(SND_MASTERCTRL_REG, x);
  
ExSfx2:
    goto rts;
  
Square2SfxHandler:
    lda_zp(Square2SoundBuffer); // special handling for the 1-up sound to keep it
    and_imm(Sfx_ExtraLife); // from being interrupted by other sounds on square 2
    if (!zero_flag) { goto ContinueExtraLife; }
    ldy_zp(Square2SoundQueue); // check for sfx in queue
    if (zero_flag) { goto CheckSfx2Buffer; }
    write_byte(Square2SoundBuffer, y); // if found, put in buffer and check for the following
    if (neg_flag) { goto PlayBowserFall; } // bowser fall
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayCoinGrab; } // coin grab
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayGrowPowerUp; } // power-up reveal
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayGrowVine; } // vine grow
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayBlast; } // fireworks/gunfire
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayTimerTick; } // timer tick
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayPowerUpGrab; } // power-up grab
    lsr_zp(Square2SoundQueue);
    if (carry_flag) { goto PlayExtraLife; } // 1-up
  
CheckSfx2Buffer:
    lda_zp(Square2SoundBuffer); // check for sfx in buffer
    if (zero_flag) { goto ExS2H; } // if not found, exit sub
    if (neg_flag) { goto ContinueBowserFall; } // bowser fall
    lsr_acc();
    if (carry_flag) { goto Cont_CGrab_TTick; } // coin grab
    lsr_acc();
    if (carry_flag) { goto ContinueGrowItems; } // power-up reveal
    lsr_acc();
    if (carry_flag) { goto ContinueGrowItems; } // vine grow
    lsr_acc();
    if (carry_flag) { goto ContinueBlast; } // fireworks/gunfire
    lsr_acc();
    if (carry_flag) { goto Cont_CGrab_TTick; } // timer tick
    lsr_acc();
    if (carry_flag) { goto ContinuePowerUpGrab; } // power-up grab
    lsr_acc();
    if (carry_flag) { goto ContinueExtraLife; } // 1-up
  
ExS2H:
    goto rts;
  
Cont_CGrab_TTick:
    goto ContinueCGrabTTick;
  
JumpToDecLength2:
    goto DecrementSfx2Length;
  
PlayBowserFall:
    lda_imm(0x38); // load length of bowser defeat sound
    write_byte(Squ2_SfxLenCounter, a);
    ldy_imm(0xc4); // load contents of reg for bowser defeat sound
    lda_imm(0x18);
  
BlstSJp:
    if (!zero_flag) { goto PBFRegs; }
  
ContinueBowserFall:
    lda_abs(Squ2_SfxLenCounter); // check for almost near the end
    cmp_imm(0x8);
    if (!zero_flag) { goto DecrementSfx2Length; }
    ldy_imm(0xa4); // if so, load the rest of reg contents for bowser defeat sound
    lda_imm(0x5a);
  
PBFRegs:
    ldx_imm(0x9f); // the fireworks/gunfire sound shares part of reg contents here
  
EL_LRegs:
    if (!zero_flag) { goto LoadSqu2Regs; } // this is an unconditional branch outta here
  
PlayExtraLife:
    lda_imm(0x30); // load length of 1-up sound
    write_byte(Squ2_SfxLenCounter, a);
  
ContinueExtraLife:
    lda_abs(Squ2_SfxLenCounter);
    ldx_imm(0x3); // load new tones only every eight frames
  
DivLLoop:
    lsr_acc();
    if (carry_flag) { goto JumpToDecLength2; } // if any bits set here, branch to dec the length
    dex();
    if (!zero_flag) { goto DivLLoop; } // do this until all bits checked, if none set, continue
    tay();
    lda_absy(ExtraLifeFreqData - 1); // load our reg contents
    ldx_imm(0x82);
    ldy_imm(0x7f);
    if (!zero_flag) { goto EL_LRegs; } // unconditional branch
  
PlayGrowPowerUp:
    lda_imm(0x10); // load length of power-up reveal sound
    if (!zero_flag) { goto GrowItemRegs; }
  
PlayGrowVine:
    lda_imm(0x20); // load length of vine grow sound
  
GrowItemRegs:
    write_byte(Squ2_SfxLenCounter, a);
    lda_imm(0x7f); // load contents of reg for both sounds directly
    write_byte(SND_SQUARE2_REG + 1, a);
    lda_imm(0x0); // start secondary counter for both sounds
    write_byte(Sfx_SecondaryCounter, a);
  
ContinueGrowItems:
    inc_abs(Sfx_SecondaryCounter); // increment secondary counter for both sounds
    lda_abs(Sfx_SecondaryCounter); // this sound doesn't decrement the usual counter
    lsr_acc(); // divide by 2 to get the offset
    tay();
    cpy_abs(Squ2_SfxLenCounter); // have we reached the end yet?
    if (zero_flag) { goto StopGrowItems; } // if so, branch to jump, and stop playing sounds
    lda_imm(0x9d); // load contents of other reg directly
    write_byte(SND_SQUARE2_REG, a);
    lda_absy(PUp_VGrow_FreqData); // use secondary counter / 2 as offset for frequency regs
    jsr(SetFreq_Squ2, 427);
    goto rts;
  
StopGrowItems:
    goto EmptySfx2Buffer; // branch to stop playing sounds
    // --------------------------------
  
PlayBrickShatter:
    lda_imm(0x20); // load length of brick shatter sound
    write_byte(Noise_SfxLenCounter, a);
  
ContinueBrickShatter:
    lda_abs(Noise_SfxLenCounter);
    lsr_acc(); // divide by 2 and check for bit set to use offset
    if (!carry_flag) { goto DecrementSfx3Length; }
    tay();
    ldx_absy(BrickShatterFreqData); // load reg contents of brick shatter sound
    lda_absy(BrickShatterEnvData);
  
PlayNoiseSfx:
    write_byte(SND_NOISE_REG, a); // play the sfx
    write_byte(SND_NOISE_REG + 2, x);
    lda_imm(0x18);
    write_byte(SND_NOISE_REG + 3, a);
  
DecrementSfx3Length:
    dec_abs(Noise_SfxLenCounter); // decrement length of sfx
    if (!zero_flag) { goto ExSfx3; }
    lda_imm(0xf0); // if done, stop playing the sfx
    write_byte(SND_NOISE_REG, a);
    lda_imm(0x0);
    write_byte(NoiseSoundBuffer, a);
  
ExSfx3:
    goto rts;
  
NoiseSfxHandler:
    ldy_zp(NoiseSoundQueue); // check for sfx in queue
    if (zero_flag) { goto CheckNoiseBuffer; }
    write_byte(NoiseSoundBuffer, y); // if found, put in buffer
    lsr_zp(NoiseSoundQueue);
    if (carry_flag) { goto PlayBrickShatter; } // brick shatter
    lsr_zp(NoiseSoundQueue);
    if (carry_flag) { goto PlayBowserFlame; } // bowser flame
  
CheckNoiseBuffer:
    lda_zp(NoiseSoundBuffer); // check for sfx in buffer
    if (zero_flag) { goto ExNH; } // if not found, exit sub
    lsr_acc();
    if (carry_flag) { goto ContinueBrickShatter; } // brick shatter
    lsr_acc();
    if (carry_flag) { goto ContinueBowserFlame; } // bowser flame
  
ExNH:
    goto rts;
  
PlayBowserFlame:
    lda_imm(0x40); // load length of bowser flame sound
    write_byte(Noise_SfxLenCounter, a);
  
ContinueBowserFlame:
    lda_abs(Noise_SfxLenCounter);
    lsr_acc();
    tay();
    ldx_imm(0xf); // load reg contents of bowser flame sound
    lda_absy(BowserFlameEnvData - 1);
    if (!zero_flag) { goto PlayNoiseSfx; } // unconditional branch here
    // --------------------------------
  
ContinueMusic:
    goto HandleSquare2Music; // if we have music, start with square 2 channel
  
MusicHandler:
    lda_zp(EventMusicQueue); // check event music queue
    if (!zero_flag) { goto LoadEventMusic; }
    lda_zp(AreaMusicQueue); // check area music queue
    if (!zero_flag) { goto LoadAreaMusic; }
    lda_abs(EventMusicBuffer); // check both buffers
    ora_zp(AreaMusicBuffer);
    if (!zero_flag) { goto ContinueMusic; }
    goto rts; // no music, then leave
  
LoadEventMusic:
    write_byte(EventMusicBuffer, a); // copy event music queue contents to buffer
    cmp_imm(DeathMusic); // is it death music?
    if (!zero_flag) { goto NoStopSfx; } // if not, jump elsewhere
    jsr(StopSquare1Sfx, 428); // stop sfx in square 1 and 2
    jsr(StopSquare2Sfx, 429); // but clear only square 1's sfx buffer
  
NoStopSfx:
    ldx_zp(AreaMusicBuffer);
    write_byte(AreaMusicBuffer_Alt, x); // save current area music buffer to be re-obtained later
    ldy_imm(0x0);
    write_byte(NoteLengthTblAdder, y); // default value for additional length byte offset
    write_byte(AreaMusicBuffer, y); // clear area music buffer
    cmp_imm(TimeRunningOutMusic); // is it time running out music?
    if (!zero_flag) { goto FindEventMusicHeader; }
    ldx_imm(0x8); // load offset to be added to length byte of header
    write_byte(NoteLengthTblAdder, x);
    if (!zero_flag) { goto FindEventMusicHeader; } // unconditional branch
  
LoadAreaMusic:
    cmp_imm(0x4); // is it underground music?
    if (!zero_flag) { goto NoStop1; } // no, do not stop square 1 sfx
    jsr(StopSquare1Sfx, 430);
  
NoStop1:
    ldy_imm(0x10); // start counter used only by ground level music
  
GMLoopB:
    write_byte(GroundMusicHeaderOfs, y);
  
HandleAreaMusicLoopB:
    ldy_imm(0x0); // clear event music buffer
    write_byte(EventMusicBuffer, y);
    write_byte(AreaMusicBuffer, a); // copy area music queue contents to buffer
    cmp_imm(0x1); // is it ground level music?
    if (!zero_flag) { goto FindAreaMusicHeader; }
    inc_abs(GroundMusicHeaderOfs); // increment but only if playing ground level music
    ldy_abs(GroundMusicHeaderOfs); // is it time to loopback ground level music?
    cpy_imm(0x32);
    if (!zero_flag) { goto LoadHeader; } // branch ahead with alternate offset
    ldy_imm(0x11);
    if (!zero_flag) { goto GMLoopB; } // unconditional branch
  
FindAreaMusicHeader:
    ldy_imm(0x8); // load Y for offset of area music
    write_byte(MusicOffset_Square2, y); // residual instruction here
  
FindEventMusicHeader:
    iny(); // increment Y pointer based on previously loaded queue contents
    lsr_acc(); // bit shift and increment until we find a set bit for music
    if (!carry_flag) { goto FindEventMusicHeader; }
  
LoadHeader:
    lda_absy(MusicHeaderOffsetData); // load offset for header
    tay();
    lda_absy(MusicHeaderData); // now load the header
    write_byte(NoteLenLookupTblOfs, a);
    lda_absy(MusicHeaderData + 1);
    write_byte(MusicDataLow, a);
    lda_absy(MusicHeaderData + 2);
    write_byte(MusicDataHigh, a);
    lda_absy(MusicHeaderData + 3);
    write_byte(MusicOffset_Triangle, a);
    lda_absy(MusicHeaderData + 4);
    write_byte(MusicOffset_Square1, a);
    lda_absy(MusicHeaderData + 5);
    write_byte(MusicOffset_Noise, a);
    write_byte(NoiseDataLoopbackOfs, a);
    lda_imm(0x1); // initialize music note counters
    write_byte(Squ2_NoteLenCounter, a);
    write_byte(Squ1_NoteLenCounter, a);
    write_byte(Tri_NoteLenCounter, a);
    write_byte(Noise_BeatLenCounter, a);
    lda_imm(0x0); // initialize music data offset for square 2
    write_byte(MusicOffset_Square2, a);
    write_byte(AltRegContentFlag, a); // initialize alternate control reg data used by square 1
    lda_imm(0xb); // disable triangle channel and reenable it
    write_byte(SND_MASTERCTRL_REG, a);
    lda_imm(0xf);
    write_byte(SND_MASTERCTRL_REG, a);
  
HandleSquare2Music:
    dec_abs(Squ2_NoteLenCounter); // decrement square 2 note length
    if (!zero_flag) { goto MiscSqu2MusicTasks; } // is it time for more data?  if not, branch to end tasks
    ldy_zp(MusicOffset_Square2); // increment square 2 music offset and fetch data
    inc_zp(MusicOffset_Square2);
    lda_indy(MusicData);
    if (zero_flag) { goto EndOfMusicData; } // if zero, the data is a null terminator
    if (!neg_flag) { goto Squ2NoteHandler; } // if non-negative, data is a note
    if (!zero_flag) { goto Squ2LengthHandler; } // otherwise it is length data
  
EndOfMusicData:
    lda_abs(EventMusicBuffer); // check secondary buffer for time running out music
    cmp_imm(TimeRunningOutMusic);
    if (!zero_flag) { goto NotTRO; }
    lda_abs(AreaMusicBuffer_Alt); // load previously saved contents of primary buffer
    if (!zero_flag) { goto MusicLoopBack; } // and start playing the song again if there is one
  
NotTRO:
    and_imm(VictoryMusic); // check for victory music (the only secondary that loops)
    if (!zero_flag) { goto VictoryMLoopBack; }
    lda_zp(AreaMusicBuffer); // check primary buffer for any music except pipe intro
    and_imm(0b01011111);
    if (!zero_flag) { goto MusicLoopBack; } // if any area music except pipe intro, music loops
    lda_imm(0x0); // clear primary and secondary buffers and initialize
    write_byte(AreaMusicBuffer, a); // control regs of square and triangle channels
    write_byte(EventMusicBuffer, a);
    write_byte(SND_TRIANGLE_REG, a);
    lda_imm(0x90);
    write_byte(SND_SQUARE1_REG, a);
    write_byte(SND_SQUARE2_REG, a);
    goto rts;
  
MusicLoopBack:
    goto HandleAreaMusicLoopB;
  
VictoryMLoopBack:
    goto LoadEventMusic;
  
Squ2LengthHandler:
    jsr(ProcessLengthData, 431); // store length of note
    write_byte(Squ2_NoteLenBuffer, a);
    ldy_zp(MusicOffset_Square2); // fetch another byte (MUST NOT BE LENGTH BYTE!)
    inc_zp(MusicOffset_Square2);
    lda_indy(MusicData);
  
Squ2NoteHandler:
    ldx_zp(Square2SoundBuffer); // is there a sound playing on this channel?
    if (!zero_flag) { goto SkipFqL1; }
    jsr(SetFreq_Squ2, 432); // no, then play the note
    if (zero_flag) { goto Rest; } // check to see if note is rest
    jsr(LoadControlRegs, 433); // if not, load control regs for square 2
  
Rest:
    write_byte(Squ2_EnvelopeDataCtrl, a); // save contents of A
    jsr(Dump_Sq2_Regs, 434); // dump X and Y into square 2 control regs
  
SkipFqL1:
    lda_abs(Squ2_NoteLenBuffer); // save length in square 2 note counter
    write_byte(Squ2_NoteLenCounter, a);
  
MiscSqu2MusicTasks:
    lda_zp(Square2SoundBuffer); // is there a sound playing on square 2?
    if (!zero_flag) { goto HandleSquare1Music; }
    lda_abs(EventMusicBuffer); // check for death music or d4 set on secondary buffer
    and_imm(0b10010001); // note that regs for death music or d4 are loaded by default
    if (!zero_flag) { goto HandleSquare1Music; }
    ldy_abs(Squ2_EnvelopeDataCtrl); // check for contents saved from LoadControlRegs
    if (zero_flag) { goto NoDecEnv1; }
    dec_abs(Squ2_EnvelopeDataCtrl); // decrement unless already zero
  
NoDecEnv1:
    jsr(LoadEnvelopeData, 435); // do a load of envelope data to replace default
    write_byte(SND_SQUARE2_REG, a); // based on offset set by first load unless playing
    ldx_imm(0x7f); // death music or d4 set on secondary buffer
    write_byte(SND_SQUARE2_REG + 1, x);
  
HandleSquare1Music:
    ldy_zp(MusicOffset_Square1); // is there a nonzero offset here?
    if (zero_flag) { goto HandleTriangleMusic; } // if not, skip ahead to the triangle channel
    dec_abs(Squ1_NoteLenCounter); // decrement square 1 note length
    if (!zero_flag) { goto MiscSqu1MusicTasks; } // is it time for more data?
  
FetchSqu1MusicData:
    ldy_zp(MusicOffset_Square1); // increment square 1 music offset and fetch data
    inc_zp(MusicOffset_Square1);
    lda_indy(MusicData);
    if (!zero_flag) { goto Squ1NoteHandler; } // if nonzero, then skip this part
    lda_imm(0x83);
    write_byte(SND_SQUARE1_REG, a); // store some data into control regs for square 1
    lda_imm(0x94); // and fetch another byte of data, used to give
    write_byte(SND_SQUARE1_REG + 1, a); // death music its unique sound
    write_byte(AltRegContentFlag, a);
    if (!zero_flag) { goto FetchSqu1MusicData; } // unconditional branch
  
Squ1NoteHandler:
    jsr(AlternateLengthHandler, 436);
    write_byte(Squ1_NoteLenCounter, a); // save contents of A in square 1 note counter
    ldy_zp(Square1SoundBuffer); // is there a sound playing on square 1?
    if (!zero_flag) { goto HandleTriangleMusic; }
    txa();
    and_imm(0b00111110); // change saved data to appropriate note format
    jsr(SetFreq_Squ1, 437); // play the note
    if (zero_flag) { goto SkipCtrlL; }
    jsr(LoadControlRegs, 438);
  
SkipCtrlL:
    write_byte(Squ1_EnvelopeDataCtrl, a); // save envelope offset
    jsr(Dump_Squ1_Regs, 439);
  
MiscSqu1MusicTasks:
    lda_zp(Square1SoundBuffer); // is there a sound playing on square 1?
    if (!zero_flag) { goto HandleTriangleMusic; }
    lda_abs(EventMusicBuffer); // check for death music or d4 set on secondary buffer
    and_imm(0b10010001);
    if (!zero_flag) { goto DeathMAltReg; }
    ldy_abs(Squ1_EnvelopeDataCtrl); // check saved envelope offset
    if (zero_flag) { goto NoDecEnv2; }
    dec_abs(Squ1_EnvelopeDataCtrl); // decrement unless already zero
  
NoDecEnv2:
    jsr(LoadEnvelopeData, 440); // do a load of envelope data
    write_byte(SND_SQUARE1_REG, a); // based on offset set by first load
  
DeathMAltReg:
    lda_abs(AltRegContentFlag); // check for alternate control reg data
    if (!zero_flag) { goto DoAltLoad; }
    lda_imm(0x7f); // load this value if zero, the alternate value
  
DoAltLoad:
    write_byte(SND_SQUARE1_REG + 1, a); // if nonzero, and let's move on
  
HandleTriangleMusic:
    lda_zp(MusicOffset_Triangle);
    dec_abs(Tri_NoteLenCounter); // decrement triangle note length
    if (!zero_flag) { goto HandleNoiseMusic; } // is it time for more data?
    ldy_zp(MusicOffset_Triangle); // increment square 1 music offset and fetch data
    inc_zp(MusicOffset_Triangle);
    lda_indy(MusicData);
    if (zero_flag) { goto LoadTriCtrlReg; } // if zero, skip all this and move on to noise 
    if (!neg_flag) { goto TriNoteHandler; } // if non-negative, data is note
    jsr(ProcessLengthData, 441); // otherwise, it is length data
    write_byte(Tri_NoteLenBuffer, a); // save contents of A
    lda_imm(0x1f);
    write_byte(SND_TRIANGLE_REG, a); // load some default data for triangle control reg
    ldy_zp(MusicOffset_Triangle); // fetch another byte
    inc_zp(MusicOffset_Triangle);
    lda_indy(MusicData);
    if (zero_flag) { goto LoadTriCtrlReg; } // check once more for nonzero data
  
TriNoteHandler:
    jsr(SetFreq_Tri, 442);
    ldx_abs(Tri_NoteLenBuffer); // save length in triangle note counter
    write_byte(Tri_NoteLenCounter, x);
    lda_abs(EventMusicBuffer);
    and_imm(0b01101110); // check for death music or d4 set on secondary buffer
    if (!zero_flag) { goto NotDOrD4; } // if playing any other secondary, skip primary buffer check
    lda_zp(AreaMusicBuffer); // check primary buffer for water or castle level music
    and_imm(0b00001010);
    if (zero_flag) { goto HandleNoiseMusic; } // if playing any other primary, or death or d4, go on to noise routine
  
NotDOrD4:
    txa(); // if playing water or castle music or any secondary
    cmp_imm(0x12); // besides death music or d4 set, check length of note
    if (carry_flag) { goto LongN; }
    lda_abs(EventMusicBuffer); // check for win castle music again if not playing a long note
    and_imm(EndOfCastleMusic);
    if (zero_flag) { goto MediN; }
    lda_imm(0xf); // load value $0f if playing the win castle music and playing a short
    if (!zero_flag) { goto LoadTriCtrlReg; } // note, load value $1f if playing water or castle level music or any
  
MediN:
    lda_imm(0x1f); // secondary besides death and d4 except win castle or win castle and playing
    if (!zero_flag) { goto LoadTriCtrlReg; } // a short note, and load value $ff if playing a long note on water, castle
  
LongN:
    lda_imm(0xff); // or any secondary (including win castle) except death and d4
  
LoadTriCtrlReg:
    write_byte(SND_TRIANGLE_REG, a); // save final contents of A into control reg for triangle
  
HandleNoiseMusic:
    lda_zp(AreaMusicBuffer); // check if playing underground or castle music
    and_imm(0b11110011);
    if (zero_flag) { goto ExitMusicHandler; } // if so, skip the noise routine
    dec_abs(Noise_BeatLenCounter); // decrement noise beat length
    if (!zero_flag) { goto ExitMusicHandler; } // is it time for more data?
  
FetchNoiseBeatData:
    ldy_abs(MusicOffset_Noise); // increment noise beat offset and fetch data
    inc_abs(MusicOffset_Noise);
    lda_indy(MusicData); // get noise beat data, if nonzero, branch to handle
    if (!zero_flag) { goto NoiseBeatHandler; }
    lda_abs(NoiseDataLoopbackOfs); // if data is zero, reload original noise beat offset
    write_byte(MusicOffset_Noise, a); // and loopback next time around
    if (!zero_flag) { goto FetchNoiseBeatData; } // unconditional branch
  
NoiseBeatHandler:
    jsr(AlternateLengthHandler, 443);
    write_byte(Noise_BeatLenCounter, a); // store length in noise beat counter
    txa();
    and_imm(0b00111110); // reload data and erase length bits
    if (zero_flag) { goto SilentBeat; } // if no beat data, silence
    cmp_imm(0x30); // check the beat data and play the appropriate
    if (zero_flag) { goto LongBeat; } // noise accordingly
    cmp_imm(0x20);
    if (zero_flag) { goto StrongBeat; }
    and_imm(0b00010000);
    if (zero_flag) { goto SilentBeat; }
    lda_imm(0x1c); // short beat data
    ldx_imm(0x3);
    ldy_imm(0x18);
    if (!zero_flag) { goto PlayBeat; }
  
StrongBeat:
    lda_imm(0x1c); // strong beat data
    ldx_imm(0xc);
    ldy_imm(0x18);
    if (!zero_flag) { goto PlayBeat; }
  
LongBeat:
    lda_imm(0x1c); // long beat data
    ldx_imm(0x3);
    ldy_imm(0x58);
    if (!zero_flag) { goto PlayBeat; }
  
SilentBeat:
    lda_imm(0x10); // silence
  
PlayBeat:
    write_byte(SND_NOISE_REG, a); // load beat data into noise regs
    write_byte(SND_NOISE_REG + 2, x);
    write_byte(SND_NOISE_REG + 3, y);
  
ExitMusicHandler:
    goto rts;
  
AlternateLengthHandler:
    tax(); // save a copy of original byte into X
    ror_acc(); // save LSB from original byte into carry
    txa(); // reload original byte and rotate three times
    rol_acc(); // turning xx00000x into 00000xxx, with the
    rol_acc(); // bit in carry as the MSB here
    rol_acc();
  
ProcessLengthData:
    and_imm(0b00000111); // clear all but the three LSBs
    carry_flag = false;
    adc_zp(0xf0); // add offset loaded from first header byte
    adc_abs(NoteLengthTblAdder); // add extra if time running out music
    tay();
    lda_absy(MusicLengthLookupTbl); // load length
    goto rts;
  
LoadControlRegs:
    lda_abs(EventMusicBuffer); // check secondary buffer for win castle music
    and_imm(EndOfCastleMusic);
    if (zero_flag) { goto NotECstlM; }
    lda_imm(0x4); // this value is only used for win castle music
    if (!zero_flag) { goto AllMus; } // unconditional branch
  
NotECstlM:
    lda_zp(AreaMusicBuffer);
    and_imm(0b01111101); // check primary buffer for water music
    if (zero_flag) { goto WaterMus; }
    lda_imm(0x8); // this is the default value for all other music
    if (!zero_flag) { goto AllMus; }
  
WaterMus:
    lda_imm(0x28); // this value is used for water music and all other event music
  
AllMus:
    ldx_imm(0x82); // load contents of other sound regs for square 2
    ldy_imm(0x7f);
    goto rts;
  
LoadEnvelopeData:
    lda_abs(EventMusicBuffer); // check secondary buffer for win castle music
    and_imm(EndOfCastleMusic);
    if (zero_flag) { goto LoadUsualEnvData; }
    lda_absy(EndOfCastleMusicEnvData); // load data from offset for win castle music
    goto rts;
  
LoadUsualEnvData:
    lda_zp(AreaMusicBuffer); // check primary buffer for water music
    and_imm(0b01111101);
    if (zero_flag) { goto LoadWaterEventMusEnvData; }
    lda_absy(AreaMusicEnvData); // load default data from offset for all other music
    goto rts;
  
LoadWaterEventMusEnvData:
    lda_absy(WaterEventMusEnvData); // load data from offset for water music and all other event music
    goto rts;
    // --------------------------------
    // music header offsets
    
rts:
  switch (pop_jsr()) {
    case 0: goto jsr_ret_0;
    case 1: goto jsr_ret_1;
    case 2: goto jsr_ret_2;
    case 3: goto jsr_ret_3;
    case 4: goto jsr_ret_4;
    case 5: goto jsr_ret_5;
    case 6: goto jsr_ret_6;
    case 7: goto jsr_ret_7;
    case 8: goto jsr_ret_8;
    case 9: goto jsr_ret_9;
    case 10: goto jsr_ret_10;
    case 11: goto jsr_ret_11;
    case 12: goto jsr_ret_12;
    case 13: goto jsr_ret_13;
    case 14: goto jsr_ret_14;
    case 15: goto jsr_ret_15;
    case 16: goto jsr_ret_16;
    case 17: goto jsr_ret_17;
    case 18: goto jsr_ret_18;
    case 19: goto jsr_ret_19;
    case 20: goto jsr_ret_20;
    case 21: goto jsr_ret_21;
    case 22: goto jsr_ret_22;
    case 23: goto jsr_ret_23;
    case 24: goto jsr_ret_24;
    case 25: goto jsr_ret_25;
    case 26: goto jsr_ret_26;
    case 27: goto jsr_ret_27;
    case 28: goto jsr_ret_28;
    case 29: goto jsr_ret_29;
    case 30: goto jsr_ret_30;
    case 31: goto jsr_ret_31;
    case 32: goto jsr_ret_32;
    case 33: goto jsr_ret_33;
    case 34: goto jsr_ret_34;
    case 35: goto jsr_ret_35;
    case 36: goto jsr_ret_36;
    case 37: goto jsr_ret_37;
    case 38: goto jsr_ret_38;
    case 39: goto jsr_ret_39;
    case 40: goto jsr_ret_40;
    case 41: goto jsr_ret_41;
    case 42: goto jsr_ret_42;
    case 43: goto jsr_ret_43;
    case 44: goto jsr_ret_44;
    case 45: goto jsr_ret_45;
    case 46: goto jsr_ret_46;
    case 47: goto jsr_ret_47;
    case 48: goto jsr_ret_48;
    case 49: goto jsr_ret_49;
    case 50: goto jsr_ret_50;
    case 51: goto jsr_ret_51;
    case 52: goto jsr_ret_52;
    case 53: goto jsr_ret_53;
    case 54: goto jsr_ret_54;
    case 55: goto jsr_ret_55;
    case 56: goto jsr_ret_56;
    case 57: goto jsr_ret_57;
    case 58: goto jsr_ret_58;
    case 59: goto jsr_ret_59;
    case 60: goto jsr_ret_60;
    case 61: goto jsr_ret_61;
    case 62: goto jsr_ret_62;
    case 63: goto jsr_ret_63;
    case 64: goto jsr_ret_64;
    case 65: goto jsr_ret_65;
    case 66: goto jsr_ret_66;
    case 67: goto jsr_ret_67;
    case 68: goto jsr_ret_68;
    case 69: goto jsr_ret_69;
    case 70: goto jsr_ret_70;
    case 71: goto jsr_ret_71;
    case 72: goto jsr_ret_72;
    case 73: goto jsr_ret_73;
    case 74: goto jsr_ret_74;
    case 75: goto jsr_ret_75;
    case 76: goto jsr_ret_76;
    case 77: goto jsr_ret_77;
    case 78: goto jsr_ret_78;
    case 79: goto jsr_ret_79;
    case 80: goto jsr_ret_80;
    case 81: goto jsr_ret_81;
    case 82: goto jsr_ret_82;
    case 83: goto jsr_ret_83;
    case 84: goto jsr_ret_84;
    case 85: goto jsr_ret_85;
    case 86: goto jsr_ret_86;
    case 87: goto jsr_ret_87;
    case 88: goto jsr_ret_88;
    case 89: goto jsr_ret_89;
    case 90: goto jsr_ret_90;
    case 91: goto jsr_ret_91;
    case 92: goto jsr_ret_92;
    case 93: goto jsr_ret_93;
    case 94: goto jsr_ret_94;
    case 95: goto jsr_ret_95;
    case 96: goto jsr_ret_96;
    case 97: goto jsr_ret_97;
    case 98: goto jsr_ret_98;
    case 99: goto jsr_ret_99;
    case 100: goto jsr_ret_100;
    case 101: goto jsr_ret_101;
    case 102: goto jsr_ret_102;
    case 103: goto jsr_ret_103;
    case 104: goto jsr_ret_104;
    case 105: goto jsr_ret_105;
    case 106: goto jsr_ret_106;
    case 107: goto jsr_ret_107;
    case 108: goto jsr_ret_108;
    case 109: goto jsr_ret_109;
    case 110: goto jsr_ret_110;
    case 111: goto jsr_ret_111;
    case 112: goto jsr_ret_112;
    case 113: goto jsr_ret_113;
    case 114: goto jsr_ret_114;
    case 115: goto jsr_ret_115;
    case 116: goto jsr_ret_116;
    case 117: goto jsr_ret_117;
    case 118: goto jsr_ret_118;
    case 119: goto jsr_ret_119;
    case 120: goto jsr_ret_120;
    case 121: goto jsr_ret_121;
    case 122: goto jsr_ret_122;
    case 123: goto jsr_ret_123;
    case 124: goto jsr_ret_124;
    case 125: goto jsr_ret_125;
    case 126: goto jsr_ret_126;
    case 127: goto jsr_ret_127;
    case 128: goto jsr_ret_128;
    case 129: goto jsr_ret_129;
    case 130: goto jsr_ret_130;
    case 131: goto jsr_ret_131;
    case 132: goto jsr_ret_132;
    case 133: goto jsr_ret_133;
    case 134: goto jsr_ret_134;
    case 135: goto jsr_ret_135;
    case 136: goto jsr_ret_136;
    case 137: goto jsr_ret_137;
    case 138: goto jsr_ret_138;
    case 139: goto jsr_ret_139;
    case 140: goto jsr_ret_140;
    case 141: goto jsr_ret_141;
    case 142: goto jsr_ret_142;
    case 143: goto jsr_ret_143;
    case 144: goto jsr_ret_144;
    case 145: goto jsr_ret_145;
    case 146: goto jsr_ret_146;
    case 147: goto jsr_ret_147;
    case 148: goto jsr_ret_148;
    case 149: goto jsr_ret_149;
    case 150: goto jsr_ret_150;
    case 151: goto jsr_ret_151;
    case 152: goto jsr_ret_152;
    case 153: goto jsr_ret_153;
    case 154: goto jsr_ret_154;
    case 155: goto jsr_ret_155;
    case 156: goto jsr_ret_156;
    case 157: goto jsr_ret_157;
    case 158: goto jsr_ret_158;
    case 159: goto jsr_ret_159;
    case 160: goto jsr_ret_160;
    case 161: goto jsr_ret_161;
    case 162: goto jsr_ret_162;
    case 163: goto jsr_ret_163;
    case 164: goto jsr_ret_164;
    case 165: goto jsr_ret_165;
    case 166: goto jsr_ret_166;
    case 167: goto jsr_ret_167;
    case 168: goto jsr_ret_168;
    case 169: goto jsr_ret_169;
    case 170: goto jsr_ret_170;
    case 171: goto jsr_ret_171;
    case 172: goto jsr_ret_172;
    case 173: goto jsr_ret_173;
    case 174: goto jsr_ret_174;
    case 175: goto jsr_ret_175;
    case 176: goto jsr_ret_176;
    case 177: goto jsr_ret_177;
    case 178: goto jsr_ret_178;
    case 179: goto jsr_ret_179;
    case 180: goto jsr_ret_180;
    case 181: goto jsr_ret_181;
    case 182: goto jsr_ret_182;
    case 183: goto jsr_ret_183;
    case 184: goto jsr_ret_184;
    case 185: goto jsr_ret_185;
    case 186: goto jsr_ret_186;
    case 187: goto jsr_ret_187;
    case 188: goto jsr_ret_188;
    case 189: goto jsr_ret_189;
    case 190: goto jsr_ret_190;
    case 191: goto jsr_ret_191;
    case 192: goto jsr_ret_192;
    case 193: goto jsr_ret_193;
    case 194: goto jsr_ret_194;
    case 195: goto jsr_ret_195;
    case 196: goto jsr_ret_196;
    case 197: goto jsr_ret_197;
    case 198: goto jsr_ret_198;
    case 199: goto jsr_ret_199;
    case 200: goto jsr_ret_200;
    case 201: goto jsr_ret_201;
    case 202: goto jsr_ret_202;
    case 203: goto jsr_ret_203;
    case 204: goto jsr_ret_204;
    case 205: goto jsr_ret_205;
    case 206: goto jsr_ret_206;
    case 207: goto jsr_ret_207;
    case 208: goto jsr_ret_208;
    case 209: goto jsr_ret_209;
    case 210: goto jsr_ret_210;
    case 211: goto jsr_ret_211;
    case 212: goto jsr_ret_212;
    case 213: goto jsr_ret_213;
    case 214: goto jsr_ret_214;
    case 215: goto jsr_ret_215;
    case 216: goto jsr_ret_216;
    case 217: goto jsr_ret_217;
    case 218: goto jsr_ret_218;
    case 219: goto jsr_ret_219;
    case 220: goto jsr_ret_220;
    case 221: goto jsr_ret_221;
    case 222: goto jsr_ret_222;
    case 223: goto jsr_ret_223;
    case 224: goto jsr_ret_224;
    case 225: goto jsr_ret_225;
    case 226: goto jsr_ret_226;
    case 227: goto jsr_ret_227;
    case 228: goto jsr_ret_228;
    case 229: goto jsr_ret_229;
    case 230: goto jsr_ret_230;
    case 231: goto jsr_ret_231;
    case 232: goto jsr_ret_232;
    case 233: goto jsr_ret_233;
    case 234: goto jsr_ret_234;
    case 235: goto jsr_ret_235;
    case 236: goto jsr_ret_236;
    case 237: goto jsr_ret_237;
    case 238: goto jsr_ret_238;
    case 239: goto jsr_ret_239;
    case 240: goto jsr_ret_240;
    case 241: goto jsr_ret_241;
    case 242: goto jsr_ret_242;
    case 243: goto jsr_ret_243;
    case 244: goto jsr_ret_244;
    case 245: goto jsr_ret_245;
    case 246: goto jsr_ret_246;
    case 247: goto jsr_ret_247;
    case 248: goto jsr_ret_248;
    case 249: goto jsr_ret_249;
    case 250: goto jsr_ret_250;
    case 251: goto jsr_ret_251;
    case 252: goto jsr_ret_252;
    case 253: goto jsr_ret_253;
    case 254: goto jsr_ret_254;
    case 255: goto jsr_ret_255;
    case 256: goto jsr_ret_256;
    case 257: goto jsr_ret_257;
    case 258: goto jsr_ret_258;
    case 259: goto jsr_ret_259;
    case 260: goto jsr_ret_260;
    case 261: goto jsr_ret_261;
    case 262: goto jsr_ret_262;
    case 263: goto jsr_ret_263;
    case 264: goto jsr_ret_264;
    case 265: goto jsr_ret_265;
    case 266: goto jsr_ret_266;
    case 267: goto jsr_ret_267;
    case 268: goto jsr_ret_268;
    case 269: goto jsr_ret_269;
    case 270: goto jsr_ret_270;
    case 271: goto jsr_ret_271;
    case 272: goto jsr_ret_272;
    case 273: goto jsr_ret_273;
    case 274: goto jsr_ret_274;
    case 275: goto jsr_ret_275;
    case 276: goto jsr_ret_276;
    case 277: goto jsr_ret_277;
    case 278: goto jsr_ret_278;
    case 279: goto jsr_ret_279;
    case 280: goto jsr_ret_280;
    case 281: goto jsr_ret_281;
    case 282: goto jsr_ret_282;
    case 283: goto jsr_ret_283;
    case 284: goto jsr_ret_284;
    case 285: goto jsr_ret_285;
    case 286: goto jsr_ret_286;
    case 287: goto jsr_ret_287;
    case 288: goto jsr_ret_288;
    case 289: goto jsr_ret_289;
    case 290: goto jsr_ret_290;
    case 291: goto jsr_ret_291;
    case 292: goto jsr_ret_292;
    case 293: goto jsr_ret_293;
    case 294: goto jsr_ret_294;
    case 295: goto jsr_ret_295;
    case 296: goto jsr_ret_296;
    case 297: goto jsr_ret_297;
    case 298: goto jsr_ret_298;
    case 299: goto jsr_ret_299;
    case 300: goto jsr_ret_300;
    case 301: goto jsr_ret_301;
    case 302: goto jsr_ret_302;
    case 303: goto jsr_ret_303;
    case 304: goto jsr_ret_304;
    case 305: goto jsr_ret_305;
    case 306: goto jsr_ret_306;
    case 307: goto jsr_ret_307;
    case 308: goto jsr_ret_308;
    case 309: goto jsr_ret_309;
    case 310: goto jsr_ret_310;
    case 311: goto jsr_ret_311;
    case 312: goto jsr_ret_312;
    case 313: goto jsr_ret_313;
    case 314: goto jsr_ret_314;
    case 315: goto jsr_ret_315;
    case 316: goto jsr_ret_316;
    case 317: goto jsr_ret_317;
    case 318: goto jsr_ret_318;
    case 319: goto jsr_ret_319;
    case 320: goto jsr_ret_320;
    case 321: goto jsr_ret_321;
    case 322: goto jsr_ret_322;
    case 323: goto jsr_ret_323;
    case 324: goto jsr_ret_324;
    case 325: goto jsr_ret_325;
    case 326: goto jsr_ret_326;
    case 327: goto jsr_ret_327;
    case 328: goto jsr_ret_328;
    case 329: goto jsr_ret_329;
    case 330: goto jsr_ret_330;
    case 331: goto jsr_ret_331;
    case 332: goto jsr_ret_332;
    case 333: goto jsr_ret_333;
    case 334: goto jsr_ret_334;
    case 335: goto jsr_ret_335;
    case 336: goto jsr_ret_336;
    case 337: goto jsr_ret_337;
    case 338: goto jsr_ret_338;
    case 339: goto jsr_ret_339;
    case 340: goto jsr_ret_340;
    case 341: goto jsr_ret_341;
    case 342: goto jsr_ret_342;
    case 343: goto jsr_ret_343;
    case 344: goto jsr_ret_344;
    case 345: goto jsr_ret_345;
    case 346: goto jsr_ret_346;
    case 347: goto jsr_ret_347;
    case 348: goto jsr_ret_348;
    case 349: goto jsr_ret_349;
    case 350: goto jsr_ret_350;
    case 351: goto jsr_ret_351;
    case 352: goto jsr_ret_352;
    case 353: goto jsr_ret_353;
    case 354: goto jsr_ret_354;
    case 355: goto jsr_ret_355;
    case 356: goto jsr_ret_356;
    case 357: goto jsr_ret_357;
    case 358: goto jsr_ret_358;
    case 359: goto jsr_ret_359;
    case 360: goto jsr_ret_360;
    case 361: goto jsr_ret_361;
    case 362: goto jsr_ret_362;
    case 363: goto jsr_ret_363;
    case 364: goto jsr_ret_364;
    case 365: goto jsr_ret_365;
    case 366: goto jsr_ret_366;
    case 367: goto jsr_ret_367;
    case 368: goto jsr_ret_368;
    case 369: goto jsr_ret_369;
    case 370: goto jsr_ret_370;
    case 371: goto jsr_ret_371;
    case 372: goto jsr_ret_372;
    case 373: goto jsr_ret_373;
    case 374: goto jsr_ret_374;
    case 375: goto jsr_ret_375;
    case 376: goto jsr_ret_376;
    case 377: goto jsr_ret_377;
    case 378: goto jsr_ret_378;
    case 379: goto jsr_ret_379;
    case 380: goto jsr_ret_380;
    case 381: goto jsr_ret_381;
    case 382: goto jsr_ret_382;
    case 383: goto jsr_ret_383;
    case 384: goto jsr_ret_384;
    case 385: goto jsr_ret_385;
    case 386: goto jsr_ret_386;
    case 387: goto jsr_ret_387;
    case 388: goto jsr_ret_388;
    case 389: goto jsr_ret_389;
    case 390: goto jsr_ret_390;
    case 391: goto jsr_ret_391;
    case 392: goto jsr_ret_392;
    case 393: goto jsr_ret_393;
    case 394: goto jsr_ret_394;
    case 395: goto jsr_ret_395;
    case 396: goto jsr_ret_396;
    case 397: goto jsr_ret_397;
    case 398: goto jsr_ret_398;
    case 399: goto jsr_ret_399;
    case 400: goto jsr_ret_400;
    case 401: goto jsr_ret_401;
    case 402: goto jsr_ret_402;
    case 403: goto jsr_ret_403;
    case 404: goto jsr_ret_404;
    case 405: goto jsr_ret_405;
    case 406: goto jsr_ret_406;
    case 407: goto jsr_ret_407;
    case 408: goto jsr_ret_408;
    case 409: goto jsr_ret_409;
    case 410: goto jsr_ret_410;
    case 411: goto jsr_ret_411;
    case 412: goto jsr_ret_412;
    case 413: goto jsr_ret_413;
    case 414: goto jsr_ret_414;
    case 415: goto jsr_ret_415;
    case 416: goto jsr_ret_416;
    case 417: goto jsr_ret_417;
    case 418: goto jsr_ret_418;
    case 419: goto jsr_ret_419;
    case 420: goto jsr_ret_420;
    case 421: goto jsr_ret_421;
    case 422: goto jsr_ret_422;
    case 423: goto jsr_ret_423;
    case 424: goto jsr_ret_424;
    case 425: goto jsr_ret_425;
    case 426: goto jsr_ret_426;
    case 427: goto jsr_ret_427;
    case 428: goto jsr_ret_428;
    case 429: goto jsr_ret_429;
    case 430: goto jsr_ret_430;
    case 431: goto jsr_ret_431;
    case 432: goto jsr_ret_432;
    case 433: goto jsr_ret_433;
    case 434: goto jsr_ret_434;
    case 435: goto jsr_ret_435;
    case 436: goto jsr_ret_436;
    case 437: goto jsr_ret_437;
    case 438: goto jsr_ret_438;
    case 439: goto jsr_ret_439;
    case 440: goto jsr_ret_440;
    case 441: goto jsr_ret_441;
    case 442: goto jsr_ret_442;
    case 443: goto jsr_ret_443;
  }
}

void SpriteShuffler(void) {
  ldy_abs(AreaType); // load level type, likely residual code
  lda_imm(0x28); // load preset value which will put it at
  write_byte(0x0, a); // sprite #10
  ldx_imm(0xe); // start at the end of OAM data offsets
  
ShuffleLoop:
    lda_absx(SprDataOffset); // check for offset value against
    cmp_zp(0x0); // the preset value
    if (!carry_flag) { goto NextSprOffset; } // if less, skip this part
    ldy_abs(SprShuffleAmtOffset); // get current offset to preset value we want to add
    carry_flag = false;
    adc_absy(SprShuffleAmt); // get shuffle amount, add to current sprite offset
    if (!carry_flag) { goto StrSprOffset; } // if not exceeded $ff, skip second add
    carry_flag = false;
    adc_zp(0x0); // otherwise add preset value $28 to offset
  
StrSprOffset:
    write_byte(SprDataOffset + x, a); // store new offset here or old one if branched to here
  
NextSprOffset:
    dex(); // move backwards to next one
    if (!neg_flag) { goto ShuffleLoop; }
    ldx_abs(SprShuffleAmtOffset); // load offset
    inx();
    cpx_imm(0x3); // check if offset + 1 goes to 3
    if (!zero_flag) { goto SetAmtOffset; } // if offset + 1 not 3, store
    ldx_imm(0x0); // otherwise, init to 0
  
SetAmtOffset:
    write_byte(SprShuffleAmtOffset, x);
    ldx_imm(0x8); // load offsets for values and storage
    ldy_imm(0x2);
  
SetMiscOffset:
    lda_absy(SprDataOffset + 5); // load one of three OAM data offsets
    write_byte(Misc_SprDataOffset - 2 + x, a); // store first one unmodified, but
    carry_flag = false; // add eight to the second and eight
    adc_imm(0x8); // more to the third one
    write_byte(Misc_SprDataOffset - 1 + x, a); // note that due to the way X is set up,
    carry_flag = false; // this code loads into the misc sprite offsets
    adc_imm(0x8);
    write_byte(Misc_SprDataOffset + x, a);
    dex();
    dex();
    dex();
    dey();
    if (!neg_flag) { goto SetMiscOffset; } // do this until all misc spr offsets are loaded
    return; // <rts>
}

void GoContinue(void) {
  write_byte(WorldNumber, a); // start both players at the first area
  write_byte(OffScr_WorldNumber, a); // of the previously saved world number
  ldx_imm(0x0); // note that on power-up using this function
  write_byte(AreaNumber, x); // will make no difference
  write_byte(OffScr_AreaNumber, x);
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void DrawMushroomIcon(void) {
  ldy_imm(0x7); // read eight bytes to be read by transfer routine
  
IconDataRead:
    lda_absy(MushroomIconData); // note that the default position is set for a
    write_byte(VRAM_Buffer1 - 1 + y, a); // 1-player game
    dey();
    if (!neg_flag) { goto IconDataRead; }
    lda_abs(NumberOfPlayers); // check number of players
    if (zero_flag) { goto ExitIcon; } // if set to 1-player game, we're done
    lda_imm(0x24); // otherwise, load blank tile in 1-player position
    write_byte(VRAM_Buffer1 + 3, a);
    lda_imm(0xce); // then load shroom icon tile in 2-player position
    write_byte(VRAM_Buffer1 + 5, a);
  
ExitIcon:
    return; // <rts>
}

void DemoEngine(void) {
  ldx_abs(DemoAction); // load current demo action
  lda_abs(DemoActionTimer); // load current action timer
  if (!zero_flag) { goto DoAction; } // if timer still counting down, skip
  inx();
  inc_abs(DemoAction); // if expired, increment action, X, and
  carry_flag = true; // set carry by default for demo over
  lda_absx(DemoTimingData - 1); // get next timer
  write_byte(DemoActionTimer, a); // store as current timer
  if (zero_flag) { goto DemoOver; } // if timer already at zero, skip
  
DoAction:
    lda_absx(DemoActionData - 1); // get and perform action (current or next)
    write_byte(SavedJoypad1Bits, a);
    dec_abs(DemoActionTimer); // decrement action timer
    carry_flag = false; // clear carry if demo still going
  
DemoOver:
    return; // <rts>
}

void ColorRotation(void) {
  lda_zp(FrameCounter); // get frame counter
  and_imm(0x7); // mask out all but three LSB
  if (!zero_flag) { goto ExitColorRot; } // branch if not set to zero to do this every eighth frame
  ldx_abs(VRAM_Buffer1_Offset); // check vram buffer offset
  cpx_imm(0x31);
  if (carry_flag) { goto ExitColorRot; } // if offset over 48 bytes, branch to leave
  tay(); // otherwise use frame counter's 3 LSB as offset here
  
GetBlankPal:
    lda_absy(BlankPalette); // get blank palette for palette 3
    write_byte(VRAM_Buffer1 + x, a); // store it in the vram buffer
    inx(); // increment offsets
    iny();
    cpy_imm(0x8);
    if (!carry_flag) { goto GetBlankPal; } // do this until all bytes are copied
    ldx_abs(VRAM_Buffer1_Offset); // get current vram buffer offset
    lda_imm(0x3);
    write_byte(0x0, a); // set counter here
    lda_abs(AreaType); // get area type
    asl_acc(); // multiply by 4 to get proper offset
    asl_acc();
    tay(); // save as offset here
  
GetAreaPal:
    lda_absy(Palette3Data); // fetch palette to be written based on area type
    write_byte(VRAM_Buffer1 + 3 + x, a); // store it to overwrite blank palette in vram buffer
    iny();
    inx();
    dec_zp(0x0); // decrement counter
    if (!neg_flag) { goto GetAreaPal; } // do this until the palette is all copied
    ldx_abs(VRAM_Buffer1_Offset); // get current vram buffer offset
    ldy_abs(ColorRotateOffset); // get color cycling offset
    lda_absy(ColorRotatePalette);
    write_byte(VRAM_Buffer1 + 4 + x, a); // get and store current color in second slot of palette
    lda_abs(VRAM_Buffer1_Offset);
    carry_flag = false; // add seven bytes to vram buffer offset
    adc_imm(0x7);
    write_byte(VRAM_Buffer1_Offset, a);
    inc_abs(ColorRotateOffset); // increment color cycling offset
    lda_abs(ColorRotateOffset);
    cmp_imm(0x6); // check to see if it's still in range
    if (!carry_flag) { goto ExitColorRot; } // if so, branch to leave
    lda_imm(0x0);
    write_byte(ColorRotateOffset, a); // otherwise, init to keep it in range
  
ExitColorRot:
    return; // <rts> // leave
}

void RemBridge(void) {
  lda_absx(BlockGfxData); // write top left and top right
  write_byte(VRAM_Buffer1 + 2 + y, a); // tile numbers into first spot
  lda_absx(BlockGfxData + 1);
  write_byte(VRAM_Buffer1 + 3 + y, a);
  lda_absx(BlockGfxData + 2); // write bottom left and bottom
  write_byte(VRAM_Buffer1 + 7 + y, a); // right tiles numbers into
  lda_absx(BlockGfxData + 3); // second spot
  write_byte(VRAM_Buffer1 + 8 + y, a);
  lda_zp(0x4);
  write_byte(VRAM_Buffer1 + y, a); // write low byte of name table
  carry_flag = false; // into first slot as read
  adc_imm(0x20); // add 32 bytes to value
  write_byte(VRAM_Buffer1 + 5 + y, a); // write low byte of name table
  lda_zp(0x5); // plus 32 bytes into second slot
  write_byte(VRAM_Buffer1 - 1 + y, a); // write high byte of name
  write_byte(VRAM_Buffer1 + 4 + y, a); // table address to both slots
  lda_imm(0x2);
  write_byte(VRAM_Buffer1 + 1 + y, a); // put length of 2 in
  write_byte(VRAM_Buffer1 + 6 + y, a); // both slots
  lda_imm(0x0);
  write_byte(VRAM_Buffer1 + 9 + y, a); // put null terminator at end
  ldx_zp(0x0); // get offset control bit here
  return; // <rts> // and leave
  // -------------------------------------------------------------------------------------
  // METATILE GRAPHICS TABLE
}

void InitializeNameTables(void) {
  lda_abs(PPU_STATUS); // reset flip-flop
  lda_abs(Mirror_PPU_CTRL_REG1); // load mirror of ppu reg $2000
  ora_imm(0b00010000); // set sprites for first 4k and background for second 4k
  and_imm(0b11110000); // clear rest of lower nybble, leave higher alone
  WritePPUReg1();
  lda_imm(0x24); // set vram address to start of name table 1
  WriteNTAddr();
  lda_imm(0x20); // and then set it to name table 0
  WriteNTAddr();
  return; // <rts>
}

void WriteNTAddr(void) {
  write_byte(PPU_ADDRESS, a);
  lda_imm(0x0);
  write_byte(PPU_ADDRESS, a);
  ldx_imm(0x4); // clear name table with blank tile #24
  ldy_imm(0xc0);
  lda_imm(0x24);
  
InitNTLoop:
    write_byte(PPU_DATA, a); // count out exactly 768 tiles
    dey();
    if (!zero_flag) { goto InitNTLoop; }
    dex();
    if (!zero_flag) { goto InitNTLoop; }
    ldy_imm(64); // now to clear the attribute table (with zero this time)
    txa();
    write_byte(VRAM_Buffer1_Offset, a); // init vram buffer 1 offset
    write_byte(VRAM_Buffer1, a); // init vram buffer 1
  
InitATLoop:
    write_byte(PPU_DATA, a);
    dey();
    if (!zero_flag) { goto InitATLoop; }
    write_byte(HorizontalScroll, a); // reset scroll variables
    write_byte(VerticalScroll, a);
    InitScroll(); // initialize scroll registers to zero
    return; // <rts>
}

void InitScroll(void) {
  write_byte(PPU_SCROLL_REG, a); // store contents of A into scroll registers
  write_byte(PPU_SCROLL_REG, a); // and end whatever subroutine led us here
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void WritePPUReg1(void) {
  write_byte(PPU_CTRL_REG1, a); // write contents of A to PPU register 1
  write_byte(Mirror_PPU_CTRL_REG1, a); // and its mirror
  return; // <rts>
  // -------------------------------------------------------------------------------------
  // $00 - used to store status bar nybbles
  // $02 - used as temp vram offset
  // $03 - used to store length of status bar number
  // status bar name table offset and length data
}

void PrintStatusBarNumbers(void) {
  write_byte(0x0, a); // store player-specific offset
  OutputNumbers(); // use first nybble to print the coin display
  lda_zp(0x0); // move high nybble to low
  lsr_acc(); // and print to score display
  lsr_acc();
  lsr_acc();
  lsr_acc();
  OutputNumbers();
  return; // <rts>
}

void OutputNumbers(void) {
  carry_flag = false; // add 1 to low nybble
  adc_imm(0x1);
  and_imm(0b00001111); // mask out high nybble
  cmp_imm(0x6);
  if (carry_flag) { goto ExitOutputN; }
  pha(); // save incremented value to stack for now and
  asl_acc(); // shift to left and use as offset
  tay();
  ldx_abs(VRAM_Buffer1_Offset); // get current buffer pointer
  lda_imm(0x20); // put at top of screen by default
  cpy_imm(0x0); // are we writing top score on title screen?
  if (!zero_flag) { goto SetupNums; }
  lda_imm(0x22); // if so, put further down on the screen
  
SetupNums:
    write_byte(VRAM_Buffer1 + x, a);
    lda_absy(StatusBarData); // write low vram address and length of thing
    write_byte(VRAM_Buffer1 + 1 + x, a); // we're printing to the buffer
    lda_absy(StatusBarData + 1);
    write_byte(VRAM_Buffer1 + 2 + x, a);
    write_byte(0x3, a); // save length byte in counter
    write_byte(0x2, x); // and buffer pointer elsewhere for now
    pla(); // pull original incremented value from stack
    tax();
    lda_absx(StatusBarOffset); // load offset to value we want to write
    carry_flag = true;
    sbc_absy(StatusBarData + 1); // subtract from length byte we read before
    tay(); // use value as offset to display digits
    ldx_zp(0x2);
  
DigitPLoop:
    lda_absy(DisplayDigits); // write digits to the buffer
    write_byte(VRAM_Buffer1 + 3 + x, a);
    inx();
    iny();
    dec_zp(0x3); // do this until all the digits are written
    if (!zero_flag) { goto DigitPLoop; }
    lda_imm(0x0); // put null terminator at end
    write_byte(VRAM_Buffer1 + 3 + x, a);
    inx(); // increment buffer pointer by 3
    inx();
    inx();
    write_byte(VRAM_Buffer1_Offset, x); // store it in case we want to use it again
  
ExitOutputN:
    return; // <rts>
}

void UpdateTopScore(void) {
  ldx_imm(0x5); // start with mario's score
  TopScoreCheck();
  ldx_imm(0xb); // now do luigi's score
  TopScoreCheck();
  return; // <rts>
}

void TopScoreCheck(void) {
  ldy_imm(0x5); // start with the lowest digit
  carry_flag = true;
  
GetScoreDiff:
    lda_absx(PlayerScoreDisplay); // subtract each player digit from each high score digit
    sbc_absy(TopScoreDisplay); // from lowest to highest, if any top score digit exceeds
    dex(); // any player digit, borrow will be set until a subsequent
    dey(); // subtraction clears it (player digit is higher than top)
    if (!neg_flag) { goto GetScoreDiff; }
    if (!carry_flag) { goto NoTopSc; } // check to see if borrow is still set, if so, no new high score
    inx(); // increment X and Y once to the start of the score
    iny();
  
CopyScore:
    lda_absx(PlayerScoreDisplay); // store player's score digits into high score memory area
    write_byte(TopScoreDisplay + y, a);
    inx();
    iny();
    cpy_imm(0x6); // do this until we have stored them all
    if (!carry_flag) { goto CopyScore; }
  
NoTopSc:
    return; // <rts>
}

void InitializeMemory(void) {
  ldx_imm(0x7); // set initial high byte to $0700-$07ff
  lda_imm(0x0); // set initial low byte to start of page (at $00 of page)
  write_byte(0x6, a);
  
InitPageLoop:
    write_byte(0x7, x);
  
InitByteLoop:
    cpx_imm(0x1); // check to see if we're on the stack ($0100-$01ff)
    if (!zero_flag) { goto InitByte; } // if not, go ahead anyway
    cpy_imm(0x60); // otherwise, check to see if we're at $0160-$01ff
    if (carry_flag) { goto SkipByte; } // if so, skip write
  
InitByte:
    write_byte(read_word(0x6) + y, a); // otherwise, initialize byte with current low byte in Y
  
SkipByte:
    dey();
    cpy_imm(0xff); // do this until all bytes in page have been erased
    if (!zero_flag) { goto InitByteLoop; }
    dex(); // go onto the next page
    if (!neg_flag) { goto InitPageLoop; } // do this until all pages of memory have been erased
    return; // <rts>
}

void GetAreaMusic(void) {
  lda_abs(OperMode); // if in title screen mode, leave
  if (zero_flag) { goto ExitGetM; }
  lda_abs(AltEntranceControl); // check for specific alternate mode of entry
  cmp_imm(0x2); // if found, branch without checking starting position
  if (zero_flag) { goto ChkAreaType; } // from area object data header
  ldy_imm(0x5); // select music for pipe intro scene by default
  lda_abs(PlayerEntranceCtrl); // check value from level header for certain values
  cmp_imm(0x6);
  if (zero_flag) { goto StoreMusic; } // load music for pipe intro scene if header
  cmp_imm(0x7); // start position either value $06 or $07
  if (zero_flag) { goto StoreMusic; }
  
ChkAreaType:
    ldy_abs(AreaType); // load area type as offset for music bit
    lda_abs(CloudTypeOverride);
    if (zero_flag) { goto StoreMusic; } // check for cloud type override
    ldy_imm(0x4); // select music for cloud type level if found
  
StoreMusic:
    lda_absy(MusicSelectData); // otherwise select appropriate music for level type
    write_byte(AreaMusicQueue, a); // store in queue and leave
  
ExitGetM:
    return; // <rts>
}

void SetupGameOver(void) {
  lda_imm(0x0); // reset screen routine task control for title screen, game,
  write_byte(ScreenRoutineTask, a); // and game over modes
  write_byte(Sprite0HitDetectFlag, a); // disable sprite 0 check
  lda_imm(GameOverMusic);
  write_byte(EventMusicQueue, a); // put game over music in secondary queue
  inc_abs(DisableScreenFlag); // disable screen output
  inc_abs(OperMode_Task); // set secondary mode to 1
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void TransposePlayers(void) {
  carry_flag = true; // set carry flag by default to end game
  lda_abs(NumberOfPlayers); // if only a 1 player game, leave
  if (zero_flag) { goto ExTrans; }
  lda_abs(OffScr_NumberofLives); // does offscreen player have any lives left?
  if (neg_flag) { goto ExTrans; } // branch if not
  lda_abs(CurrentPlayer); // invert bit to update
  eor_imm(0b00000001); // which player is on the screen
  write_byte(CurrentPlayer, a);
  ldx_imm(0x6);
  
TransLoop:
    lda_absx(OnscreenPlayerInfo); // transpose the information
    pha(); // of the onscreen player
    lda_absx(OffscreenPlayerInfo); // with that of the offscreen player
    write_byte(OnscreenPlayerInfo + x, a);
    pla();
    write_byte(OffscreenPlayerInfo + x, a);
    dex();
    if (!neg_flag) { goto TransLoop; }
    carry_flag = false; // clear carry flag to get game going
  
ExTrans:
    return; // <rts>
}

void DoNothing2(void) {
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void ScrollLockObject(void) {
  lda_abs(ScrollLock); // invert scroll lock to turn it on
  eor_imm(0b00000001);
  write_byte(ScrollLock, a);
  return; // <rts>
  // --------------------------------
  // $00 - used to store enemy identifier in KillEnemies
}

void KillEnemies(void) {
  write_byte(0x0, a); // store identifier here
  lda_imm(0x0);
  ldx_imm(0x4); // check for identifier in enemy object buffer
  
KillELoop:
    ldy_zpx(Enemy_ID);
    cpy_zp(0x0); // if not found, branch
    if (!zero_flag) { goto NoKillE; }
    write_byte(Enemy_Flag + x, a); // if found, deactivate enemy object flag
  
NoKillE:
    dex(); // do this until all slots are checked
    if (!neg_flag) { goto KillELoop; }
    return; // <rts>
}

void AreaFrenzy(void) {
  ldx_zp(0x0); // use area object identifier bit as offset
  lda_absx(FrenzyIDData - 8); // note that it starts at 8, thus weird address here
  ldy_imm(0x5);
  
FreCompLoop:
    dey(); // check regular slots of enemy object buffer
    if (neg_flag) { goto ExitAFrenzy; } // if all slots checked and enemy object not found, branch to store
    cmp_zpy(Enemy_ID); // check for enemy object in buffer versus frenzy object
    if (!zero_flag) { goto FreCompLoop; }
    lda_imm(0x0); // if enemy object already present, nullify queue and leave
  
ExitAFrenzy:
    write_byte(EnemyFrenzyQueue, a); // store enemy into frenzy queue
    return; // <rts>
}

void WaterPipe(void) {
  GetLrgObjAttrib(); // get row and lower nybble
  ldy_absx(AreaObjectLength); // get length (residual code, water pipe is 1 col thick)
  ldx_zp(0x7); // get row
  lda_imm(0x6b);
  write_byte(MetatileBuffer + x, a); // draw something here and below it
  lda_imm(0x6c);
  write_byte(MetatileBuffer + 1 + x, a);
  return; // <rts>
}

void FindEmptyEnemySlot(void) {
  ldx_imm(0x0); // start at first enemy slot
  
EmptyChkLoop:
    carry_flag = false; // clear carry flag by default
    lda_zpx(Enemy_Flag); // check enemy buffer for nonzero
    if (zero_flag) { goto ExitEmptyChk; } // if zero, leave
    inx();
    cpx_imm(0x5); // if nonzero, check next value
    if (!zero_flag) { goto EmptyChkLoop; }
  
ExitEmptyChk:
    return; // <rts> // if all values nonzero, carry flag is set
}

void Jumpspring(void) {
  GetLrgObjAttrib();
  FindEmptyEnemySlot(); // find empty space in enemy object buffer
  GetAreaObjXPosition(); // get horizontal coordinate for jumpspring
  write_byte(Enemy_X_Position + x, a); // and store
  lda_abs(CurrentPageLoc); // store page location of jumpspring
  write_byte(Enemy_PageLoc + x, a);
  GetAreaObjYPosition(); // get vertical coordinate for jumpspring
  write_byte(Enemy_Y_Position + x, a); // and store
  write_byte(Jumpspring_FixedYPos + x, a); // store as permanent coordinate here
  lda_imm(JumpspringObject);
  write_byte(Enemy_ID + x, a); // write jumpspring object to enemy object buffer
  ldy_imm(0x1);
  write_byte(Enemy_Y_HighPos + x, y); // store vertical high byte
  inc_zpx(Enemy_Flag); // set flag for enemy object buffer
  ldx_zp(0x7);
  lda_imm(0x67); // draw metatiles in two rows where jumpspring is
  write_byte(MetatileBuffer + x, a);
  lda_imm(0x68);
  write_byte(MetatileBuffer + 1 + x, a);
  return; // <rts>
}

void GetLrgObjAttrib(void) {
  ldy_absx(AreaObjOffsetBuffer); // get offset saved from area obj decoding routine
  lda_indy(AreaData); // get first byte of level object
  and_imm(0b00001111);
  write_byte(0x7, a); // save row location
  iny();
  lda_indy(AreaData); // get next byte, save lower nybble (length or height)
  and_imm(0b00001111); // as Y, then leave
  tay();
  return; // <rts>
  // --------------------------------
}

void GetAreaObjXPosition(void) {
  lda_abs(CurrentColumnPos); // multiply current offset where we're at by 16
  asl_acc(); // to obtain horizontal pixel coordinate
  asl_acc();
  asl_acc();
  asl_acc();
  return; // <rts>
  // --------------------------------
}

void GetAreaObjYPosition(void) {
  lda_zp(0x7); // multiply value by 16
  asl_acc();
  asl_acc(); // this will give us the proper vertical pixel coordinate
  asl_acc();
  asl_acc();
  carry_flag = false;
  adc_imm(32); // add 32 pixels for the status bar
  return; // <rts>
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
  write_byte(0x7, a);
  pla();
  and_imm(0b00001111); // pull from stack, mask out high nybble
  carry_flag = false;
  adc_absy(BlockBufferAddr); // add to low byte
  write_byte(0x6, a); // store here and leave
  return; // <rts>
  // -------------------------------------------------------------------------------------
  // unused space
  //       .db $ff, $ff
  // -------------------------------------------------------------------------------------
}

void GetScreenPosition(void) {
  lda_abs(ScreenLeft_X_Pos); // get coordinate of screen's left boundary
  carry_flag = false;
  adc_imm(0xff); // add 255 pixels
  write_byte(ScreenRight_X_Pos, a); // store as coordinate of screen's right boundary
  lda_abs(ScreenLeft_PageLoc); // get page number where left boundary is
  adc_imm(0x0); // add carry from before
  write_byte(ScreenRight_PageLoc, a); // store as page number where right boundary is
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void MovePlayerYAxis(void) {
  carry_flag = false;
  adc_zp(Player_Y_Position); // add contents of A to player position
  write_byte(Player_Y_Position, a);
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void GetPlayerAnimSpeed(void) {
  ldy_imm(0x0); // initialize offset in Y
  lda_abs(Player_XSpeedAbsolute); // check player's walking/running speed
  cmp_imm(0x1c); // against preset amount
  if (carry_flag) { goto SetRunSpd; } // if greater than a certain amount, branch ahead
  iny(); // otherwise increment Y
  cmp_imm(0xe); // compare against lower amount
  if (carry_flag) { goto ChkSkid; } // if greater than this but not greater than first, skip increment
  iny(); // otherwise increment Y again
  
ChkSkid:
    lda_abs(SavedJoypadBits); // get controller bits
    and_imm(0b01111111); // mask out A button
    if (zero_flag) { goto SetAnimSpd; } // if no other buttons pressed, branch ahead of all this
    and_imm(0x3); // mask out all others except left and right
    cmp_zp(Player_MovingDir); // check against moving direction
    if (!zero_flag) { goto ProcSkid; } // if left/right controller bits <> moving direction, branch
    lda_imm(0x0); // otherwise set zero value here
  
SetRunSpd:
    write_byte(RunningSpeed, a); // store zero or running speed here
    goto SetAnimSpd;
  
ProcSkid:
    lda_abs(Player_XSpeedAbsolute); // check player's walking/running speed
    cmp_imm(0xb); // against one last amount
    if (carry_flag) { goto SetAnimSpd; } // if greater than this amount, branch
    lda_zp(PlayerFacingDir);
    write_byte(Player_MovingDir, a); // otherwise use facing direction to set moving direction
    lda_imm(0x0);
    write_byte(Player_X_Speed, a); // nullify player's horizontal speed
    write_byte(Player_X_MoveForce, a); // and dummy variable for player
  
SetAnimSpd:
    lda_absy(PlayerAnimTmrData); // get animation timer setting using Y as offset
    write_byte(PlayerAnimTimerSet, a);
    return; // <rts>
}

void ImposeFriction(void) {
  and_abs(Player_CollisionBits); // perform AND between left/right controller bits and collision flag
  cmp_imm(0x0); // then compare to zero (this instruction is redundant)
  if (!zero_flag) { goto JoypFrict; } // if any bits set, branch to next part
  lda_zp(Player_X_Speed);
  if (zero_flag) { goto SetAbsSpd; } // if player has no horizontal speed, branch ahead to last part
  if (!neg_flag) { goto RghtFrict; } // if player moving to the right, branch to slow
  if (neg_flag) { goto LeftFrict; } // otherwise logic dictates player moving left, branch to slow
  
JoypFrict:
    lsr_acc(); // put right controller bit into carry
    if (!carry_flag) { goto RghtFrict; } // if left button pressed, carry = 0, thus branch
  
LeftFrict:
    lda_abs(Player_X_MoveForce); // load value set here
    carry_flag = false;
    adc_abs(FrictionAdderLow); // add to it another value set here
    write_byte(Player_X_MoveForce, a); // store here
    lda_zp(Player_X_Speed);
    adc_abs(FrictionAdderHigh); // add value plus carry to horizontal speed
    write_byte(Player_X_Speed, a); // set as new horizontal speed
    cmp_abs(MaximumRightSpeed); // compare against maximum value for right movement
    if (neg_flag) { goto XSpdSign; } // if horizontal speed greater negatively, branch
    lda_abs(MaximumRightSpeed); // otherwise set preset value as horizontal speed
    write_byte(Player_X_Speed, a); // thus slowing the player's left movement down
    goto SetAbsSpd; // skip to the end
  
RghtFrict:
    lda_abs(Player_X_MoveForce); // load value set here
    carry_flag = true;
    sbc_abs(FrictionAdderLow); // subtract from it another value set here
    write_byte(Player_X_MoveForce, a); // store here
    lda_zp(Player_X_Speed);
    sbc_abs(FrictionAdderHigh); // subtract value plus borrow from horizontal speed
    write_byte(Player_X_Speed, a); // set as new horizontal speed
    cmp_abs(MaximumLeftSpeed); // compare against maximum value for left movement
    if (!neg_flag) { goto XSpdSign; } // if horizontal speed greater positively, branch
    lda_abs(MaximumLeftSpeed); // otherwise set preset value as horizontal speed
    write_byte(Player_X_Speed, a); // thus slowing the player's right movement down
  
XSpdSign:
    cmp_imm(0x0); // if player not moving or moving to the right,
    if (!neg_flag) { goto SetAbsSpd; } // branch and leave horizontal speed value unmodified
    eor_imm(0xff);
    carry_flag = false; // otherwise get two's compliment to get absolute
    adc_imm(0x1); // unsigned walking/running speed
  
SetAbsSpd:
    write_byte(Player_XSpeedAbsolute, a); // store walking/running speed here and leave
    return; // <rts>
}

void Setup_Vine(void) {
  lda_imm(VineObject); // load identifier for vine object
  write_byte(Enemy_ID + x, a); // store in buffer
  lda_imm(0x1);
  write_byte(Enemy_Flag + x, a); // set flag for enemy object buffer
  lda_zpy(Block_PageLoc);
  write_byte(Enemy_PageLoc + x, a); // copy page location from previous object
  lda_zpy(Block_X_Position);
  write_byte(Enemy_X_Position + x, a); // copy horizontal coordinate from previous object
  lda_zpy(Block_Y_Position);
  write_byte(Enemy_Y_Position + x, a); // copy vertical coordinate from previous object
  ldy_abs(VineFlagOffset); // load vine flag/offset to next available vine slot
  if (!zero_flag) { goto NextVO; } // if set at all, don't bother to store vertical
  write_byte(VineStart_Y_Position, a); // otherwise store vertical coordinate here
  
NextVO:
    txa(); // store object offset to next available vine slot
    write_byte(VineObjOffset + y, a); // using vine flag as offset
    inc_abs(VineFlagOffset); // increment vine flag offset
    lda_imm(Sfx_GrowVine);
    write_byte(Square2SoundQueue, a); // load vine grow sound
    return; // <rts>
}

void FindEmptyMiscSlot(void) {
  ldy_imm(0x8); // start at end of misc objects buffer
  
FMiscLoop:
    lda_zpy(Misc_State); // get misc object state
    if (zero_flag) { goto UseMiscS; } // branch if none found to use current offset
    dey(); // decrement offset
    cpy_imm(0x5); // do this for three slots
    if (!zero_flag) { goto FMiscLoop; } // do this until all slots are checked
    ldy_imm(0x8); // if no empty slots found, use last slot
  
UseMiscS:
    write_byte(JumpCoinMiscOffset, y); // store offset of misc object buffer here (residual)
    return; // <rts>
}

void PwrUpJmp(void) {
  lda_imm(0x1); // this is a residual jump point in enemy object jump table
  write_byte(Enemy_State + 5, a); // set power-up object's state
  write_byte(Enemy_Flag + 5, a); // set buffer flag
  lda_imm(0x3);
  write_byte(Enemy_BoundBoxCtrl + 5, a); // set bounding box size control for power-up object
  lda_zp(PowerUpType);
  cmp_imm(0x2); // check currently loaded power-up type
  if (carry_flag) { goto PutBehind; } // if star or 1-up, branch ahead
  lda_abs(PlayerStatus); // otherwise check player's current status
  cmp_imm(0x2);
  if (!carry_flag) { goto StrType; } // if player not fiery, use status as power-up type
  lsr_acc(); // otherwise shift right to force fire flower type
  
StrType:
    write_byte(PowerUpType, a); // store type here
  
PutBehind:
    lda_imm(0b00100000);
    write_byte(Enemy_SprAttrib + 5, a); // set background priority bit
    lda_imm(Sfx_GrowPowerUp);
    write_byte(Square2SoundQueue, a); // load power-up reveal sound and leave
    return; // <rts>
}

void InitBlock_XY_Pos(void) {
  lda_zp(Player_X_Position); // get player's horizontal coordinate
  carry_flag = false;
  adc_imm(0x8); // add eight pixels
  and_imm(0xf0); // mask out low nybble to give 16-pixel correspondence
  write_byte(Block_X_Position + x, a); // save as horizontal coordinate for block object
  lda_zp(Player_PageLoc);
  adc_imm(0x0); // add carry to page location of player
  write_byte(Block_PageLoc + x, a); // save as page location of block object
  write_byte(Block_PageLoc2 + x, a); // save elsewhere to be used later
  lda_zp(Player_Y_HighPos);
  write_byte(Block_Y_HighPos + x, a); // save vertical high byte of player into
  return; // <rts> // vertical high byte of block object and leave
  // --------------------------------
}

void BlockBumpedChk(void) {
  ldy_imm(0xd); // start at end of metatile data
  
BumpChkLoop:
    cmp_absy(BrickQBlockMetatiles); // check to see if current metatile matches
    if (zero_flag) { goto MatchBump; } // metatile found in block buffer, branch if so
    dey(); // otherwise move onto next metatile
    if (!neg_flag) { goto BumpChkLoop; } // do this until all metatiles are checked
    carry_flag = false; // if none match, return with carry clear
  
MatchBump:
    return; // <rts> // note carry is set if found match
}

void SpawnBrickChunks(void) {
  lda_zpx(Block_X_Position); // set horizontal coordinate of block object
  write_byte(Block_Orig_XPos + x, a); // as original horizontal coordinate here
  lda_imm(0xf0);
  write_byte(Block_X_Speed + x, a); // set horizontal speed for brick chunk objects
  write_byte(Block_X_Speed + 2 + x, a);
  lda_imm(0xfa);
  write_byte(Block_Y_Speed + x, a); // set vertical speed for one
  lda_imm(0xfc);
  write_byte(Block_Y_Speed + 2 + x, a); // set lower vertical speed for the other
  lda_imm(0x0);
  write_byte(Block_Y_MoveForce + x, a); // init fractional movement force for both
  write_byte(Block_Y_MoveForce + 2 + x, a);
  lda_zpx(Block_PageLoc);
  write_byte(Block_PageLoc + 2 + x, a); // copy page location
  lda_zpx(Block_X_Position);
  write_byte(Block_X_Position + 2 + x, a); // copy horizontal coordinate
  lda_zpx(Block_Y_Position);
  carry_flag = false; // add 8 pixels to vertical coordinate
  adc_imm(0x8); // and save as vertical coordinate for one of them
  write_byte(Block_Y_Position + 2 + x, a);
  lda_imm(0xfa);
  write_byte(Block_Y_Speed + x, a); // set vertical speed...again??? (redundant)
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void ImposeGravity(void) {
  pha(); // push value to stack
  lda_absx(SprObject_YMF_Dummy);
  carry_flag = false; // add value in movement force to contents of dummy variable
  adc_absx(SprObject_Y_MoveForce);
  write_byte(SprObject_YMF_Dummy + x, a);
  ldy_imm(0x0); // set Y to zero by default
  lda_zpx(SprObject_Y_Speed); // get current vertical speed
  if (!neg_flag) { goto AlterYP; } // if currently moving downwards, do not decrement Y
  dey(); // otherwise decrement Y
  
AlterYP:
    write_byte(0x7, y); // store Y here
    adc_zpx(SprObject_Y_Position); // add vertical position to vertical speed plus carry
    write_byte(SprObject_Y_Position + x, a); // store as new vertical position
    lda_zpx(SprObject_Y_HighPos);
    adc_zp(0x7); // add carry plus contents of $07 to vertical high byte
    write_byte(SprObject_Y_HighPos + x, a); // store as new vertical high byte
    lda_absx(SprObject_Y_MoveForce);
    carry_flag = false;
    adc_zp(0x0); // add downward movement amount to contents of $0433
    write_byte(SprObject_Y_MoveForce + x, a);
    lda_zpx(SprObject_Y_Speed); // add carry to vertical speed and store
    adc_imm(0x0);
    write_byte(SprObject_Y_Speed + x, a);
    cmp_zp(0x2); // compare to maximum speed
    if (neg_flag) { goto ChkUpM; } // if less than preset value, skip this part
    lda_absx(SprObject_Y_MoveForce);
    cmp_imm(0x80); // if less positively than preset maximum, skip this part
    if (!carry_flag) { goto ChkUpM; }
    lda_zp(0x2);
    write_byte(SprObject_Y_Speed + x, a); // keep vertical speed within maximum value
    lda_imm(0x0);
    write_byte(SprObject_Y_MoveForce + x, a); // clear fractional
  
ChkUpM:
    pla(); // get value from stack
    if (zero_flag) { goto ExVMove; } // if set to zero, branch to leave
    lda_zp(0x2);
    eor_imm(0b11111111); // otherwise get two's compliment of maximum speed
    tay();
    iny();
    write_byte(0x7, y); // store two's compliment here
    lda_absx(SprObject_Y_MoveForce);
    carry_flag = true; // subtract upward movement amount from contents
    sbc_zp(0x1); // of movement force, note that $01 is twice as large as $00,
    write_byte(SprObject_Y_MoveForce + x, a); // thus it effectively undoes add we did earlier
    lda_zpx(SprObject_Y_Speed);
    sbc_imm(0x0); // subtract borrow from vertical speed and store
    write_byte(SprObject_Y_Speed + x, a);
    cmp_zp(0x7); // compare vertical speed to two's compliment
    if (!neg_flag) { goto ExVMove; } // if less negatively than preset maximum, skip this part
    lda_absx(SprObject_Y_MoveForce);
    cmp_imm(0x80); // check if fractional part is above certain amount,
    if (carry_flag) { goto ExVMove; } // and if so, branch to leave
    lda_zp(0x7);
    write_byte(SprObject_Y_Speed + x, a); // keep vertical speed within maximum value
    lda_imm(0xff);
    write_byte(SprObject_Y_MoveForce + x, a); // clear fractional
  
ExVMove:
    return; // <rts> // leave!
}

void NoInitCode(void) {
  return; // <rts> // this executed when enemy object has no init code
  // --------------------------------
}

void InitRetainerObj(void) {
  lda_imm(0xb8); // set fixed vertical position for
  write_byte(Enemy_Y_Position + x, a); // princess/mushroom retainer object
  return; // <rts>
  // --------------------------------
}

void InitVStf(void) {
  lda_imm(0x0); // initialize vertical speed
  write_byte(Enemy_Y_Speed + x, a); // and movement force
  write_byte(Enemy_Y_MoveForce + x, a);
  return; // <rts>
  // --------------------------------
}

void InitBulletBill(void) {
  lda_imm(0x2); // set moving direction for left
  write_byte(Enemy_MovingDir + x, a);
  lda_imm(0x9); // set bounding box control for $09
  write_byte(Enemy_BoundBoxCtrl + x, a);
  return; // <rts>
  // --------------------------------
}

void InitFireworks(void) {
  lda_abs(FrenzyEnemyTimer); // if timer not expired yet, branch to leave
  if (!zero_flag) { goto ExitFWk; }
  lda_imm(0x20); // otherwise reset timer
  write_byte(FrenzyEnemyTimer, a);
  dec_abs(FireworksCounter); // decrement for each explosion
  ldy_imm(0x6); // start at last slot
  
StarFChk:
    dey();
    lda_zpy(Enemy_ID); // check for presence of star flag object
    cmp_imm(StarFlagObject); // if there isn't a star flag object,
    if (!zero_flag) { goto StarFChk; } // routine goes into infinite loop = crash
    lda_zpy(Enemy_X_Position);
    carry_flag = true; // get horizontal coordinate of star flag object, then
    sbc_imm(0x30); // subtract 48 pixels from it and save to
    pha(); // the stack
    lda_zpy(Enemy_PageLoc);
    sbc_imm(0x0); // subtract the carry from the page location
    write_byte(0x0, a); // of the star flag object
    lda_abs(FireworksCounter); // get fireworks counter
    carry_flag = false;
    adc_zpy(Enemy_State); // add state of star flag object (possibly not necessary)
    tay(); // use as offset
    pla(); // get saved horizontal coordinate of star flag - 48 pixels
    carry_flag = false;
    adc_absy(FireworksXPosData); // add number based on offset of fireworks counter
    write_byte(Enemy_X_Position + x, a); // store as the fireworks object horizontal coordinate
    lda_zp(0x0);
    adc_imm(0x0); // add carry and store as page location for
    write_byte(Enemy_PageLoc + x, a); // the fireworks object
    lda_absy(FireworksYPosData); // get vertical position using same offset
    write_byte(Enemy_Y_Position + x, a); // and store as vertical coordinate for fireworks object
    lda_imm(0x1);
    write_byte(Enemy_Y_HighPos + x, a); // store in vertical high byte
    write_byte(Enemy_Flag + x, a); // and activate enemy buffer flag
    lsr_acc();
    write_byte(ExplosionGfxCounter + x, a); // initialize explosion counter
    lda_imm(0x8);
    write_byte(ExplosionTimerCounter + x, a); // set explosion timing counter
  
ExitFWk:
    return; // <rts>
}

void NoFrenzyCode(void) {
  return; // <rts>
  // --------------------------------
}

void EndFrenzy(void) {
  ldy_imm(0x5); // start at last slot
  
LakituChk:
    lda_zpy(Enemy_ID); // check enemy identifiers
    cmp_imm(Lakitu); // for lakitu
    if (!zero_flag) { goto NextFSlot; }
    lda_imm(0x1); // if found, set state
    write_byte(Enemy_State + y, a);
  
NextFSlot:
    dey(); // move onto the next slot
    if (!neg_flag) { goto LakituChk; } // do this until all slots are checked
    lda_imm(0x0);
    write_byte(EnemyFrenzyBuffer, a); // empty enemy frenzy buffer
    write_byte(Enemy_Flag + x, a); // disable enemy buffer flag for this object
    return; // <rts>
}

void InitBalPlatform(void) {
  dec_zpx(Enemy_Y_Position); // raise vertical position by two pixels
  dec_zpx(Enemy_Y_Position);
  ldy_abs(SecondaryHardMode); // if secondary hard mode flag not set,
  if (!zero_flag) { goto AlignP; } // branch ahead
  ldy_imm(0x2); // otherwise set value here
  PosPlatform(); // do a sub to add or subtract pixels
  
AlignP:
    ldy_imm(0xff); // set default value here for now
    lda_abs(BalPlatformAlignment); // get current balance platform alignment
    write_byte(Enemy_State + x, a); // set platform alignment to object state here
    if (!neg_flag) { goto SetBPA; } // if old alignment $ff, put $ff as alignment for negative
    txa(); // if old contents already $ff, put
    tay(); // object offset as alignment to make next positive
  
SetBPA:
    write_byte(BalPlatformAlignment, y); // store whatever value's in Y here
    lda_imm(0x0);
    write_byte(Enemy_MovingDir + x, a); // init moving direction
    tay(); // init Y
    PosPlatform(); // do a sub to add 8 pixels, then run shared code here
    // --------------------------------
}

void InitVertPlatform(void) {
  ldy_imm(0x40); // set default value here
  lda_zpx(Enemy_Y_Position); // check vertical position
  if (!neg_flag) { goto SetYO; } // if above a certain point, skip this part
  eor_imm(0xff);
  carry_flag = false; // otherwise get two's compliment
  adc_imm(0x1);
  ldy_imm(0xc0); // get alternate value to add to vertical position
  
SetYO:
    write_byte(YPlatformTopYPos + x, a); // save as top vertical position
    tya();
    carry_flag = false; // load value from earlier, add number of pixels 
    adc_zpx(Enemy_Y_Position); // to vertical position
    write_byte(YPlatformCenterYPos + x, a); // save result as central vertical position
    // --------------------------------
}

void PosPlatform(void) {
  lda_zpx(Enemy_X_Position); // get horizontal coordinate
  carry_flag = false;
  adc_absy(PlatPosDataLow); // add or subtract pixels depending on offset
  write_byte(Enemy_X_Position + x, a); // store as new horizontal coordinate
  lda_zpx(Enemy_PageLoc);
  adc_absy(PlatPosDataHigh); // add or subtract page location depending on offset
  write_byte(Enemy_PageLoc + x, a); // store as new page location
  return; // <rts> // and go back
  // --------------------------------
}

void EndOfEnemyInitCode(void) {
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void NoRunCode(void) {
  return; // <rts>
  // --------------------------------
}

void NoMoveCode(void) {
  return; // <rts>
  // --------------------------------
}

void EraseEnemyObject(void) {
  lda_imm(0x0); // clear all enemy object variables
  write_byte(Enemy_Flag + x, a);
  write_byte(Enemy_ID + x, a);
  write_byte(Enemy_State + x, a);
  write_byte(FloateyNum_Control + x, a);
  write_byte(EnemyIntervalTimer + x, a);
  write_byte(ShellChainCounter + x, a);
  write_byte(Enemy_SprAttrib + x, a);
  write_byte(EnemyFrameTimer + x, a);
  return; // <rts>
  // -------------------------------------------------------------------------------------
}

void GetFirebarPosition(void) {
  pha(); // save high byte of spinstate to the stack
  and_imm(0b00001111); // mask out low nybble
  cmp_imm(0x9);
  if (!carry_flag) { goto GetHAdder; } // if lower than $09, branch ahead
  eor_imm(0b00001111); // otherwise get two's compliment to oscillate
  carry_flag = false;
  adc_imm(0x1);
  
GetHAdder:
    write_byte(0x1, a); // store result, modified or not, here
    ldy_zp(0x0); // load number of firebar ball where we're at
    lda_absy(FirebarTblOffsets); // load offset to firebar position data
    carry_flag = false;
    adc_zp(0x1); // add oscillated high byte of spinstate
    tay(); // to offset here and use as new offset
    lda_absy(FirebarPosLookupTbl); // get data here and store as horizontal adder
    write_byte(0x1, a);
    pla(); // pull whatever was in A from the stack
    pha(); // save it again because we still need it
    carry_flag = false;
    adc_imm(0x8); // add eight this time, to get vertical adder
    and_imm(0b00001111); // mask out high nybble
    cmp_imm(0x9); // if lower than $09, branch ahead
    if (!carry_flag) { goto GetVAdder; }
    eor_imm(0b00001111); // otherwise get two's compliment
    carry_flag = false;
    adc_imm(0x1);
  
GetVAdder:
    write_byte(0x2, a); // store result here
    ldy_zp(0x0);
    lda_absy(FirebarTblOffsets); // load offset to firebar position data again
    carry_flag = false;
    adc_zp(0x2); // this time add value in $02 to offset here and use as offset
    tay();
    lda_absy(FirebarPosLookupTbl); // get data here and store as vertica adder
    write_byte(0x2, a);
    pla(); // pull out whatever was in A one last time
    lsr_acc(); // divide by eight or shift three to the right
    lsr_acc();
    lsr_acc();
    tay(); // use as offset
    lda_absy(FirebarMirrorData); // load mirroring data here
    write_byte(0x3, a); // store
    return; // <rts>
}

void MovePiranhaPlant(void) {
  lda_zpx(Enemy_State); // check enemy state
  if (!zero_flag) { goto PutinPipe; } // if set at all, branch to leave
  lda_absx(EnemyFrameTimer); // check enemy's timer here
  if (!zero_flag) { goto PutinPipe; } // branch to end if not yet expired
  lda_zpx(PiranhaPlant_MoveFlag); // check movement flag
  if (!zero_flag) { goto SetupToMovePPlant; } // if moving, skip to part ahead
  lda_zpx(PiranhaPlant_Y_Speed); // if currently rising, branch 
  if (neg_flag) { goto ReversePlantSpeed; } // to move enemy upwards out of pipe
  PlayerEnemyDiff(); // get horizontal difference between player and
  if (!neg_flag) { goto ChkPlayerNearPipe; } // piranha plant, and branch if enemy to right of player
  lda_zp(0x0); // otherwise get saved horizontal difference
  eor_imm(0xff);
  carry_flag = false; // and change to two's compliment
  adc_imm(0x1);
  write_byte(0x0, a); // save as new horizontal difference
  
ChkPlayerNearPipe:
    lda_zp(0x0); // get saved horizontal difference
    cmp_imm(0x21);
    if (!carry_flag) { goto PutinPipe; } // if player within a certain distance, branch to leave
  
ReversePlantSpeed:
    lda_zpx(PiranhaPlant_Y_Speed); // get vertical speed
    eor_imm(0xff);
    carry_flag = false; // change to two's compliment
    adc_imm(0x1);
    write_byte(PiranhaPlant_Y_Speed + x, a); // save as new vertical speed
    inc_zpx(PiranhaPlant_MoveFlag); // increment to set movement flag
  
SetupToMovePPlant:
    lda_absx(PiranhaPlantDownYPos); // get original vertical coordinate (lowest point)
    ldy_zpx(PiranhaPlant_Y_Speed); // get vertical speed
    if (!neg_flag) { goto RiseFallPiranhaPlant; } // branch if moving downwards
    lda_absx(PiranhaPlantUpYPos); // otherwise get other vertical coordinate (highest point)
  
RiseFallPiranhaPlant:
    write_byte(0x0, a); // save vertical coordinate here
    lda_zp(FrameCounter); // get frame counter
    lsr_acc();
    if (!carry_flag) { goto PutinPipe; } // branch to leave if d0 set (execute code every other frame)
    lda_abs(TimerControl); // get master timer control
    if (!zero_flag) { goto PutinPipe; } // branch to leave if set (likely not necessary)
    lda_zpx(Enemy_Y_Position); // get current vertical coordinate
    carry_flag = false;
    adc_zpx(PiranhaPlant_Y_Speed); // add vertical speed to move up or down
    write_byte(Enemy_Y_Position + x, a); // save as new vertical coordinate
    cmp_zp(0x0); // compare against low or high coordinate
    if (!zero_flag) { goto PutinPipe; } // branch to leave if not yet reached
    lda_imm(0x0);
    write_byte(PiranhaPlant_MoveFlag + x, a); // otherwise clear movement flag
    lda_imm(0x40);
    write_byte(EnemyFrameTimer + x, a); // set timer to delay piranha plant movement
  
PutinPipe:
    lda_imm(0b00100000); // set background priority bit in sprite
    write_byte(Enemy_SprAttrib + x, a); // attributes to give illusion of being inside pipe
    return; // <rts> // then leave
}

void SetupPlatformRope(void) {
  pha(); // save second/third copy to stack
  lda_zpy(Enemy_X_Position); // get horizontal coordinate
  carry_flag = false;
  adc_imm(0x8); // add eight pixels
  ldx_abs(SecondaryHardMode); // if secondary hard mode flag set,
  if (!zero_flag) { goto GetLRp; } // use coordinate as-is
  carry_flag = false;
  adc_imm(0x10); // otherwise add sixteen more pixels
  
GetLRp:
    pha(); // save modified horizontal coordinate to stack
    lda_zpy(Enemy_PageLoc);
    adc_imm(0x0); // add carry to page location
    write_byte(0x2, a); // and save here
    pla(); // pull modified horizontal coordinate
    and_imm(0b11110000); // from the stack, mask out low nybble
    lsr_acc(); // and shift three bits to the right
    lsr_acc();
    lsr_acc();
    write_byte(0x0, a); // store result here as part of name table low byte
    ldx_zpy(Enemy_Y_Position); // get vertical coordinate
    pla(); // get second/third copy of vertical speed from stack
    if (!neg_flag) { goto GetHRp; } // skip this part if moving downwards or not at all
    txa();
    carry_flag = false;
    adc_imm(0x8); // add eight to vertical coordinate and
    tax(); // save as X
  
GetHRp:
    txa(); // move vertical coordinate to A
    ldx_abs(VRAM_Buffer1_Offset); // get vram buffer offset
    asl_acc();
    rol_acc(); // rotate d7 to d0 and d6 into carry
    pha(); // save modified vertical coordinate to stack
    rol_acc(); // rotate carry to d0, thus d7 and d6 are at 2 LSB
    and_imm(0b00000011); // mask out all bits but d7 and d6, then set
    ora_imm(0b00100000); // d5 to get appropriate high byte of name table
    write_byte(0x1, a); // address, then store
    lda_zp(0x2); // get saved page location from earlier
    and_imm(0x1); // mask out all but LSB
    asl_acc();
    asl_acc(); // shift twice to the left and save with the
    ora_zp(0x1); // rest of the bits of the high byte, to get
    write_byte(0x1, a); // the proper name table and the right place on it
    pla(); // get modified vertical coordinate from stack
    and_imm(0b11100000); // mask out low nybble and LSB of high nybble
    carry_flag = false;
    adc_zp(0x0); // add to horizontal part saved here
    write_byte(0x0, a); // save as name table low byte
    lda_zpy(Enemy_Y_Position);
    cmp_imm(0xe8); // if vertical position not below the
    if (!carry_flag) { goto ExPRp; } // bottom of the screen, we're done, branch to leave
    lda_zp(0x0);
    and_imm(0b10111111); // mask out d6 of low byte of name table address
    write_byte(0x0, a);
  
ExPRp:
    return; // <rts> // leave!
}

void StopPlatforms(void) {
  InitVStf(); // initialize vertical speed and low byte
  write_byte(Enemy_Y_Speed + y, a); // for both platforms and leave
  write_byte(Enemy_Y_MoveForce + y, a);
  return; // <rts>
}

void OffscreenBoundsCheck(void) {
  lda_zpx(Enemy_ID); // check for cheep-cheep object
  cmp_imm(FlyingCheepCheep); // branch to leave if found
  if (zero_flag) { goto ExScrnBd; }
  lda_abs(ScreenLeft_X_Pos); // get horizontal coordinate for left side of screen
  ldy_zpx(Enemy_ID);
  cpy_imm(HammerBro); // check for hammer bro object
  if (zero_flag) { goto LimitB; }
  cpy_imm(PiranhaPlant); // check for piranha plant object
  if (!zero_flag) { goto ExtendLB; } // these two will be erased sooner than others if too far left
  
LimitB:
    adc_imm(0x38); // add 56 pixels to coordinate if hammer bro or piranha plant
  
ExtendLB:
    sbc_imm(0x48); // subtract 72 pixels regardless of enemy object
    write_byte(0x1, a); // store result here
    lda_abs(ScreenLeft_PageLoc);
    sbc_imm(0x0); // subtract borrow from page location of left side
    write_byte(0x0, a); // store result here
    lda_abs(ScreenRight_X_Pos); // add 72 pixels to the right side horizontal coordinate
    adc_imm(0x48);
    write_byte(0x3, a); // store result here
    lda_abs(ScreenRight_PageLoc);
    adc_imm(0x0); // then add the carry to the page location
    write_byte(0x2, a); // and store result here
    lda_zpx(Enemy_X_Position); // compare horizontal coordinate of the enemy object
    cmp_zp(0x1); // to modified horizontal left edge coordinate to get carry
    lda_zpx(Enemy_PageLoc);
    sbc_zp(0x0); // then subtract it from the page coordinate of the enemy object
    if (neg_flag) { goto TooFar; } // if enemy object is too far left, branch to erase it
    lda_zpx(Enemy_X_Position); // compare horizontal coordinate of the enemy object
    cmp_zp(0x3); // to modified horizontal right edge coordinate to get carry
    lda_zpx(Enemy_PageLoc);
    sbc_zp(0x2); // then subtract it from the page coordinate of the enemy object
    if (neg_flag) { goto ExScrnBd; } // if enemy object is on the screen, leave, do not erase enemy
    lda_zpx(Enemy_State); // if at this point, enemy is offscreen to the right, so check
    cmp_imm(HammerBro); // if in state used by spiny's egg, do not erase
    if (zero_flag) { goto ExScrnBd; }
    cpy_imm(PiranhaPlant); // if piranha plant, do not erase
    if (zero_flag) { goto ExScrnBd; }
    cpy_imm(FlagpoleFlagObject); // if flagpole flag, do not erase
    if (zero_flag) { goto ExScrnBd; }
    cpy_imm(StarFlagObject); // if star flag, do not erase
    if (zero_flag) { goto ExScrnBd; }
    cpy_imm(JumpspringObject); // if jumpspring, do not erase
    if (zero_flag) { goto ExScrnBd; } // erase all others too far to the right
  
TooFar:
    EraseEnemyObject(); // erase object if necessary
  
ExScrnBd:
    return; // <rts> // leave
}

void EnemyFacePlayer(void) {
  ldy_imm(0x1); // set to move right by default
  PlayerEnemyDiff(); // get horizontal difference between player and enemy
  if (!neg_flag) { goto SFcRt; } // if enemy is to the right of player, do not increment
  iny(); // otherwise, increment to set to move to the left
  
SFcRt:
    write_byte(Enemy_MovingDir + x, y); // set moving direction here
    dey(); // then decrement to use as a proper offset
    return; // <rts>
}

void CheckPlayerVertical(void) {
  lda_abs(Player_OffscreenBits); // if player object is completely offscreen
  cmp_imm(0xf0); // vertically, leave this routine
  if (carry_flag) { goto ExCPV; }
  ldy_zp(Player_Y_HighPos); // if player high vertical byte is not
  dey(); // within the screen, leave this routine
  if (!zero_flag) { goto ExCPV; }
  lda_zp(Player_Y_Position); // if on the screen, check to see how far down
  cmp_imm(0xd0); // the player is vertically
  
ExCPV:
    return; // <rts>
}

void GetEnemyBoundBoxOfsArg(void) {
  asl_acc(); // multiply A by four, then add four
  asl_acc(); // to skip player's bounding box
  carry_flag = false;
  adc_imm(0x4);
  tay(); // send to Y
  lda_abs(Enemy_OffscreenBits); // get offscreen bits for enemy object
  and_imm(0b00001111); // save low nybble
  cmp_imm(0b00001111); // check for all bits set
  return; // <rts>
  // -------------------------------------------------------------------------------------
  // $00-$01 - used to hold many values, essentially temp variables
  // $04 - holds lower nybble of vertical coordinate from block buffer routine
  // $eb - used to hold block buffer adder
}

void ChkInvisibleMTiles(void) {
  cmp_imm(0x5f); // check for hidden coin block
  if (zero_flag) { goto ExCInvT; } // branch to leave if found
  cmp_imm(0x60); // check for hidden 1-up block
  
ExCInvT:
    return; // <rts> // leave with zero flag set if either found
}

void ChkForLandJumpSpring(void) {
  ChkJumpspringMetatiles(); // do sub to check if player landed on jumpspring
  if (!carry_flag) { goto ExCJSp; } // if carry not set, jumpspring not found, therefore leave
  lda_imm(0x70);
  write_byte(VerticalForce, a); // otherwise set vertical movement force for player
  lda_imm(0xf9);
  write_byte(JumpspringForce, a); // set default jumpspring force
  lda_imm(0x3);
  write_byte(JumpspringTimer, a); // set jumpspring timer to be used later
  lsr_acc();
  write_byte(JumpspringAnimCtrl, a); // set jumpspring animation control to start animating
  
ExCJSp:
    return; // <rts> // and leave
}

void ChkJumpspringMetatiles(void) {
  cmp_imm(0x67); // check for top jumpspring metatile
  if (zero_flag) { goto JSFnd; } // branch to set carry if found
  cmp_imm(0x68); // check for bottom jumpspring metatile
  carry_flag = false; // clear carry flag
  if (!zero_flag) { goto NoJSFnd; } // branch to use cleared carry if not found
  
JSFnd:
    carry_flag = true; // set carry if found
  
NoJSFnd:
    return; // <rts> // leave
}

void HandlePipeEntry(void) {
  lda_zp(Up_Down_Buttons); // check saved controller bits from earlier
  and_imm(0b00000100); // for pressing down
  if (zero_flag) { goto ExPipeE; } // if not pressing down, branch to leave
  lda_zp(0x0);
  cmp_imm(0x11); // check right foot metatile for warp pipe right metatile
  if (!zero_flag) { goto ExPipeE; } // branch to leave if not found
  lda_zp(0x1);
  cmp_imm(0x10); // check left foot metatile for warp pipe left metatile
  if (!zero_flag) { goto ExPipeE; } // branch to leave if not found
  lda_imm(0x30);
  write_byte(ChangeAreaTimer, a); // set timer for change of area
  lda_imm(0x3);
  write_byte(GameEngineSubroutine, a); // set to run vertical pipe entry routine on next frame
  lda_imm(Sfx_PipeDown_Injury);
  write_byte(Square1SoundQueue, a); // load pipedown/injury sound
  lda_imm(0b00100000);
  write_byte(Player_SprAttrib, a); // set background priority bit in player's attributes
  lda_abs(WarpZoneControl); // check warp zone control
  if (zero_flag) { goto ExPipeE; } // branch to leave if none found
  and_imm(0b00000011); // mask out all but 2 LSB
  asl_acc();
  asl_acc(); // multiply by four
  tax(); // save as offset to warp zone numbers (starts at left pipe)
  lda_zp(Player_X_Position); // get player's horizontal position
  cmp_imm(0x60);
  if (!carry_flag) { goto GetWNum; } // if player at left, not near middle, use offset and skip ahead
  inx(); // otherwise increment for middle pipe
  cmp_imm(0xa0);
  if (!carry_flag) { goto GetWNum; } // if player at middle, but not too far right, use offset and skip
  inx(); // otherwise increment for last pipe
  
GetWNum:
    ldy_absx(WarpZoneNumbers); // get warp zone numbers
    dey(); // decrement for use as world number
    write_byte(WorldNumber, y); // store as world number and offset
    ldx_absy(WorldAddrOffsets); // get offset to where this world's area offsets are
    lda_absx(AreaAddrOffsets); // get area offset based on world offset
    write_byte(AreaPointer, a); // store area offset here to be used to change areas
    lda_imm(Silence);
    write_byte(EventMusicQueue, a); // silence music
    lda_imm(0x0);
    write_byte(EntrancePage, a); // initialize starting page number
    write_byte(AreaNumber, a); // initialize area number used for area address offset
    write_byte(LevelNumber, a); // initialize level number used for world display
    write_byte(AltEntranceControl, a); // initialize mode of entry
    inc_abs(Hidden1UpFlag); // set flag for hidden 1-up blocks
    inc_abs(FetchNewGameTimerFlag); // set flag to load new game timer
  
ExPipeE:
    return; // <rts> // leave!!!
}

void ImpedePlayerMove(void) {
  lda_imm(0x0); // initialize value here
  ldy_zp(Player_X_Speed); // get player's horizontal speed
  ldx_zp(0x0); // check value set earlier for
  dex(); // left side collision
  if (!zero_flag) { goto RImpd; } // if right side collision, skip this part
  inx(); // return value to X
  cpy_imm(0x0); // if player moving to the left,
  if (neg_flag) { goto ExIPM; } // branch to invert bit and leave
  lda_imm(0xff); // otherwise load A with value to be used later
  goto NXSpd; // and jump to affect movement
  
RImpd:
    ldx_imm(0x2); // return $02 to X
    cpy_imm(0x1); // if player moving to the right,
    if (!neg_flag) { goto ExIPM; } // branch to invert bit and leave
    lda_imm(0x1); // otherwise load A with value to be used here
  
NXSpd:
    ldy_imm(0x10);
    write_byte(SideCollisionTimer, y); // set timer of some sort
    ldy_imm(0x0);
    write_byte(Player_X_Speed, y); // nullify player's horizontal speed
    cmp_imm(0x0); // if value set in A not set to $ff,
    if (!neg_flag) { goto PlatF; } // branch ahead, do not decrement Y
    dey(); // otherwise decrement Y now
  
PlatF:
    write_byte(0x0, y); // store Y as high bits of horizontal adder
    carry_flag = false;
    adc_zp(Player_X_Position); // add contents of A to player's horizontal
    write_byte(Player_X_Position, a); // position to move player left or right
    lda_zp(Player_PageLoc);
    adc_zp(0x0); // add high bits and carry to
    write_byte(Player_PageLoc, a); // page location if necessary
  
ExIPM:
    txa(); // invert contents of X
    eor_imm(0xff);
    and_abs(Player_CollisionBits); // mask out bit that was set here
    write_byte(Player_CollisionBits, a); // store to clear bit
    return; // <rts>
}

void PlayerEnemyDiff(void) {
  lda_zpx(Enemy_X_Position); // get distance between enemy object's
  carry_flag = true; // horizontal coordinate and the player's
  sbc_zp(Player_X_Position); // horizontal coordinate
  write_byte(0x0, a); // and store here
  lda_zpx(Enemy_PageLoc);
  sbc_zp(Player_PageLoc); // subtract borrow, then leave
  return; // <rts>
  // --------------------------------
}

void EnemyLanding(void) {
  InitVStf(); // do something here to vertical speed and something else
  lda_zpx(Enemy_Y_Position);
  and_imm(0b11110000); // save high nybble of vertical coordinate, and
  ora_imm(0b00001000); // set d3, then store, probably used to set enemy object
  write_byte(Enemy_Y_Position + x, a); // neatly on whatever it's landing on
  return; // <rts>
}

void DrawVine(void) {
  write_byte(0x0, y); // save offset here
  lda_abs(Enemy_Rel_YPos); // get relative vertical coordinate
  carry_flag = false;
  adc_absy(VineYPosAdder); // add value using offset in Y to get value
  ldx_absy(VineObjOffset); // get offset to vine
  ldy_absx(Enemy_SprDataOffset); // get sprite data offset
  write_byte(0x2, y); // store sprite data offset here
  SixSpriteStacker(); // stack six sprites on top of each other vertically
  lda_abs(Enemy_Rel_XPos); // get relative horizontal coordinate
  write_byte(Sprite_X_Position + y, a); // store in first, third and fifth sprites
  write_byte(Sprite_X_Position + 8 + y, a);
  write_byte(Sprite_X_Position + 16 + y, a);
  carry_flag = false;
  adc_imm(0x6); // add six pixels to second, fourth and sixth sprites
  write_byte(Sprite_X_Position + 4 + y, a); // to give characteristic staggered vine shape to
  write_byte(Sprite_X_Position + 12 + y, a); // our vertical stack of sprites
  write_byte(Sprite_X_Position + 20 + y, a);
  lda_imm(0b00100001); // set bg priority and palette attribute bits
  write_byte(Sprite_Attributes + y, a); // set in first, third and fifth sprites
  write_byte(Sprite_Attributes + 8 + y, a);
  write_byte(Sprite_Attributes + 16 + y, a);
  ora_imm(0b01000000); // additionally, set horizontal flip bit
  write_byte(Sprite_Attributes + 4 + y, a); // for second, fourth and sixth sprites
  write_byte(Sprite_Attributes + 12 + y, a);
  write_byte(Sprite_Attributes + 20 + y, a);
  ldx_imm(0x5); // set tiles for six sprites
  
VineTL:
    lda_imm(0xe1); // set tile number for sprite
    write_byte(Sprite_Tilenumber + y, a);
    iny(); // move offset to next sprite data
    iny();
    iny();
    iny();
    dex(); // move onto next sprite
    if (!neg_flag) { goto VineTL; } // loop until all sprites are done
    ldy_zp(0x2); // get original offset
    lda_zp(0x0); // get offset to vine adding data
    if (!zero_flag) { goto SkpVTop; } // if offset not zero, skip this part
    lda_imm(0xe0);
    write_byte(Sprite_Tilenumber + y, a); // set other tile number for top of vine
  
SkpVTop:
    ldx_imm(0x0); // start with the first sprite again
  
ChkFTop:
    lda_abs(VineStart_Y_Position); // get original starting vertical coordinate
    carry_flag = true;
    sbc_absy(Sprite_Y_Position); // subtract top-most sprite's Y coordinate
    cmp_imm(0x64); // if two coordinates are less than 100/$64 pixels
    if (!carry_flag) { goto NextVSp; } // apart, skip this to leave sprite alone
    lda_imm(0xf8);
    write_byte(Sprite_Y_Position + y, a); // otherwise move sprite offscreen
  
NextVSp:
    iny(); // move offset to next OAM data
    iny();
    iny();
    iny();
    inx(); // move onto next sprite
    cpx_imm(0x6); // do this until all sprites are checked
    if (!zero_flag) { goto ChkFTop; }
    ldy_zp(0x0); // return offset set earlier
    return; // <rts>
}

void SixSpriteStacker(void) {
  ldx_imm(0x6); // do six sprites
  
StkLp:
    write_byte(Sprite_Data + y, a); // store X or Y coordinate into OAM data
    carry_flag = false;
    adc_imm(0x8); // add eight pixels
    iny();
    iny(); // move offset four bytes forward
    iny();
    iny();
    dex(); // do another sprite
    if (!zero_flag) { goto StkLp; } // do this until all sprites are done
    ldy_zp(0x2); // get saved OAM data offset and leave
    return; // <rts>
}

void DrawFirebar(void) {
  lda_zp(FrameCounter); // get frame counter
  lsr_acc(); // divide by four
  lsr_acc();
  pha(); // save result to stack
  and_imm(0x1); // mask out all but last bit
  eor_imm(0x64); // set either tile $64 or $65 as fireball tile
  write_byte(Sprite_Tilenumber + y, a); // thus tile changes every four frames
  pla(); // get from stack
  lsr_acc(); // divide by four again
  lsr_acc();
  lda_imm(0x2); // load value $02 to set palette in attrib byte
  if (!carry_flag) { goto FireA; } // if last bit shifted out was not set, skip this
  ora_imm(0b11000000); // otherwise flip both ways every eight frames
  
FireA:
    write_byte(Sprite_Attributes + y, a); // store attribute byte and leave
    return; // <rts>
}

void DrawBubble(void) {
  ldy_zp(Player_Y_HighPos); // if player's vertical high position
  dey(); // not within screen, skip all of this
  if (!zero_flag) { goto ExDBub; }
  lda_abs(Bubble_OffscreenBits); // check air bubble's offscreen bits
  and_imm(0b00001000);
  if (!zero_flag) { goto ExDBub; } // if bit set, branch to leave
  ldy_absx(Bubble_SprDataOffset); // get air bubble's OAM data offset
  lda_abs(Bubble_Rel_XPos); // get relative horizontal coordinate
  write_byte(Sprite_X_Position + y, a); // store as X coordinate here
  lda_abs(Bubble_Rel_YPos); // get relative vertical coordinate
  write_byte(Sprite_Y_Position + y, a); // store as Y coordinate here
  lda_imm(0x74);
  write_byte(Sprite_Tilenumber + y, a); // put air bubble tile into OAM data
  lda_imm(0x2);
  write_byte(Sprite_Attributes + y, a); // set attribute byte
  
ExDBub:
    return; // <rts> // leave
}

void GetGfxOffsetAdder(void) {
  lda_abs(PlayerSize); // get player's size
  if (zero_flag) { goto SzOfs; } // if player big, use current offset as-is
  tya(); // for big player
  carry_flag = false; // otherwise add eight bytes to offset
  adc_imm(0x8); // for small player
  tay();
  
SzOfs:
    return; // <rts> // go back
}

void ChkForPlayerAttrib(void) {
  ldy_abs(Player_SprDataOffset); // get sprite data offset
  lda_zp(GameEngineSubroutine);
  cmp_imm(0xb); // if executing specific game engine routine,
  if (zero_flag) { goto KilledAtt; } // branch to change third and fourth row OAM attributes
  lda_abs(PlayerGfxOffset); // get graphics table offset
  cmp_imm(0x50);
  if (zero_flag) { goto C_S_IGAtt; } // if crouch offset, either standing offset,
  cmp_imm(0xb8); // or intermediate growing offset,
  if (zero_flag) { goto C_S_IGAtt; } // go ahead and execute code to change 
  cmp_imm(0xc0); // fourth row OAM attributes only
  if (zero_flag) { goto C_S_IGAtt; }
  cmp_imm(0xc8);
  if (!zero_flag) { goto ExPlyrAt; } // if none of these, branch to leave
  
KilledAtt:
    lda_absy(Sprite_Attributes + 16);
    and_imm(0b00111111); // mask out horizontal and vertical flip bits
    write_byte(Sprite_Attributes + 16 + y, a); // for third row sprites and save
    lda_absy(Sprite_Attributes + 20);
    and_imm(0b00111111);
    ora_imm(0b01000000); // set horizontal flip bit for second
    write_byte(Sprite_Attributes + 20 + y, a); // sprite in the third row
  
C_S_IGAtt:
    lda_absy(Sprite_Attributes + 24);
    and_imm(0b00111111); // mask out horizontal and vertical flip bits
    write_byte(Sprite_Attributes + 24 + y, a); // for fourth row sprites and save
    lda_absy(Sprite_Attributes + 28);
    and_imm(0b00111111);
    ora_imm(0b01000000); // set horizontal flip bit for second
    write_byte(Sprite_Attributes + 28 + y, a); // sprite in the fourth row
  
ExPlyrAt:
    return; // <rts> // leave
}

void GetObjRelativePosition(void) {
  lda_zpx(SprObject_Y_Position); // load vertical coordinate low
  write_byte(SprObject_Rel_YPos + y, a); // store here
  lda_zpx(SprObject_X_Position); // load horizontal coordinate
  carry_flag = true; // subtract left edge coordinate
  sbc_abs(ScreenLeft_X_Pos);
  write_byte(SprObject_Rel_XPos + y, a); // store result here
  return; // <rts>
  // -------------------------------------------------------------------------------------
  // $00 - used as temp variable to hold offscreen bits
}

void GetXOffscreenBits(void) {
  write_byte(0x4, x); // save position in buffer to here
  ldy_imm(0x1); // start with right side of screen
  
XOfsLoop:
    lda_absy(ScreenEdge_X_Pos); // get pixel coordinate of edge
    carry_flag = true; // get difference between pixel coordinate of edge
    sbc_zpx(SprObject_X_Position); // and pixel coordinate of object position
    write_byte(0x7, a); // store here
    lda_absy(ScreenEdge_PageLoc); // get page location of edge
    sbc_zpx(SprObject_PageLoc); // subtract from page location of object position
    ldx_absy(DefaultXOnscreenOfs); // load offset value here
    cmp_imm(0x0);
    if (neg_flag) { goto XLdBData; } // if beyond right edge or in front of left edge, branch
    ldx_absy(DefaultXOnscreenOfs + 1); // if not, load alternate offset value here
    cmp_imm(0x1);
    if (!neg_flag) { goto XLdBData; } // if one page or more to the left of either edge, branch
    lda_imm(0x38); // if no branching, load value here and store
    write_byte(0x6, a);
    lda_imm(0x8); // load some other value and execute subroutine
    DividePDiff();
  
XLdBData:
    lda_absx(XOffscreenBitsData); // get bits here
    ldx_zp(0x4); // reobtain position in buffer
    cmp_imm(0x0); // if bits not zero, branch to leave
    if (!zero_flag) { goto ExXOfsBS; }
    dey(); // otherwise, do left side of screen now
    if (!neg_flag) { goto XOfsLoop; } // branch if not already done with left side
  
ExXOfsBS:
    return; // <rts>
}

void DividePDiff(void) {
  write_byte(0x5, a); // store current value in A here
  lda_zp(0x7); // get pixel difference
  cmp_zp(0x6); // compare to preset value
  if (carry_flag) { goto ExDivPD; } // if pixel difference >= preset value, branch
  lsr_acc(); // divide by eight
  lsr_acc();
  lsr_acc();
  and_imm(0x7); // mask out all but 3 LSB
  cpy_imm(0x1); // right side of the screen or top?
  if (carry_flag) { goto SetOscrO; } // if so, branch, use difference / 8 as offset
  adc_zp(0x5); // if not, add value to difference / 8
  
SetOscrO:
    tax(); // use as offset
  
ExDivPD:
    return; // <rts> // leave
}

