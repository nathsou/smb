#ifndef SMB_CODE_H
#define SMB_CODE_H

#include "data.h"
#include "instructions.h"

void Start(void);
void NonMaskableInterrupt(void);
void PauseRoutine(void);
void SpriteShuffler(void);
void OperModeExecutionTree(void);
void TitleScreenMode(void);
void GameMenuRoutine(void);
void VictoryMode(void);
void ScreenRoutines(void);
void InitScreen(void);
void SetupIntermediate(void);
void GetAreaPalette(void);
void SetVRAMAddr_A(void);
void NextSubtask(void);
void GetBackgroundColor(void);
void GetPlayerColors(void);
void SetVRAMOffset(void);
void GetAlternatePalette1(void);
void SetVRAMAddr_B(void);
void NoAltPal(void);
void WriteTopStatusLine(void);
void WriteBottomStatusLine(void);
void DisplayTimeUp(void);
void DisplayIntermediate(void);
void GameOverInter(void);
void NoInter(void);
void AreaParserTaskControl(void);
void DrawTitleScreen(void);
void ClearBuffersDrawIcon(void);
void IncSubtask(void);
void WriteTopScore(void);
void IncModeTask_B(void);
void ResetSpritesAndScreenTimer(void);
void InitializeGame(void);
void InitializeArea(void);
void PrimaryGameSetup(void);
void SecondaryGameSetup(void);
void GameOverMode(void);
void SetupGameOver(void);
void RunGameOver(void);
void TerminateGame(void);
void ContinueGame(void);
void GameMode(void);
void GameCoreRoutine(void);
void UpdScrollVar(void);
void PlayerGfxHandler(void);
void FindPlayerAction(void);
void DoChangeSize(void);
void PlayerKilled(void);
void PlayerGfxProcessing(void);
void MoveAllSpritesOffscreen(void);
void MoveSpritesOffscreen(void);
void MoveSpritesOffscreenSkip(void);
void GoContinue(void);
void DrawMushroomIcon(void);
void DemoEngine(void);
void VictoryModeSubroutines(void);
void SetupVictoryMode(void);
void PlayerVictoryWalk(void);
void PrintVictoryMessages(void);
void IncModeTask_A(void);
void PlayerEndWorld(void);
void BridgeCollapse(void);
void MoveD_Bowser(void);
void RemoveBridge(void);
void FloateyNumbersRoutine(void);
void OutputInter(void);
void WriteGameText(void);
void ResetScreenTimer(void);
void RenderAreaGraphics(void);
void RenderAttributeTables(void);
void SetVRAMCtrl(void);
void ColorRotation(void);
void RemoveCoin_Axe(void);
void ReplaceBlockMetatile(void);
void DestroyBlockMetatile(void);
void WriteBlockMetatile(void);
void MoveVOffset(void);
void PutBlockMetatile(void);
void RemBridge(void);
void InitializeNameTables(void);
void WriteNTAddr(void);
void ReadJoypads(void);
void ReadPortBits(void);
void WriteBufferToScreen(void);
void UpdateScreen(void);
void InitScroll(void);
void WritePPUReg1(void);
void PrintStatusBarNumbers(void);
void OutputNumbers(void);
void DigitsMathRoutine(void);
void UpdateTopScore(void);
void TopScoreCheck(void);
void InitializeMemory(void);
void GetAreaMusic(void);
void Entrance_GameTimerSetup(void);
void PlayerLoseLife(void);
void TransposePlayers(void);
void DoNothing1(void);
void DoNothing2(void);
void AreaParserTaskHandler(void);
void AreaParserTasks(void);
void IncrementColumnPos(void);
void AreaParserCore(void);
void ProcessAreaData(void);
void IncAreaObjOffset(void);
void DecodeAreaData(void);
void LoopCmdE(void);
void BackColC(void);
void StrAObj(void);
void RunAObj(void);
void AlterAreaAttributes(void);
void ScrollLockObject_Warp(void);
void ScrollLockObject(void);
void AreaFrenzy(void);
void AreaStyleObject(void);
void TreeLedge(void);
void MushroomLedge(void);
void AllUnder(void);
void NoUnder(void);
void PulleyRopeObject(void);
void CastleObject(void);
void WaterPipe(void);
void IntroPipe(void);
void ExitPipe(void);
void RenderSidewaysPipe(void);
void VerticalPipe(void);
void Hole_Water(void);
void QuestionBlockRow_High(void);
void QuestionBlockRow_Low(void);
void QuestionBlockRow_LowSkip(void);
void Bridge_High(void);
void Bridge_Middle(void);
void Bridge_Low(void);
void Bridge_LowSkip(void);
void FlagBalls_Residual(void);
void FlagpoleObject(void);
void EndlessRope(void);
void BalancePlatRope(void);
void DrawRope(void);
void RowOfCoins(void);
void CastleBridgeObj(void);
void AxeObj(void);
void ChainObj(void);
void EmptyBlock(void);
void ColObj(void);
void RowOfBricks(void);
void RowOfSolidBlocks(void);
void GetRow(void);
void DrawRow(void);
void ColumnOfBricks(void);
void ColumnOfSolidBlocks(void);
void GetRow2(void);
void BulletBillCannon(void);
void StaircaseObject(void);
void Jumpspring(void);
void Hidden1UpBlock(void);
void QuestionBlock(void);
void BrickWithCoins(void);
void BrickWithItem(void);
void DrawQBlk(void);
void Hole_Empty(void);
void RenderUnderPart(void);
void KillEnemies(void);
void GetPipeHeight(void);
void FindEmptyEnemySlot(void);
void GetAreaObjectID(void);
void ChkLrgObjLength(void);
void ChkLrgObjFixedLength(void);
void GetLrgObjAttrib(void);
void GetAreaObjXPosition(void);
void GetAreaObjYPosition(void);
void GetBlockBufferAddr(void);
void LoadAreaPointer(void);
void GetAreaType(void);
void FindAreaPointer(void);
void GetAreaDataAddrs(void);
void ScrollHandler(void);
void ScrollScreen(void);
void InitScrlAmt(void);
void ChkPOffscr(void);
void GetScreenPosition(void);
void GameRoutines(void);
void PlayerEntrance(void);
void AutoControlPlayer(void);
void PlayerCtrlRoutine(void);
void Vine_AutoClimb(void);
void SetEntr(void);
void VerticalPipeEntry(void);
void SideExitPipeEntry(void);
void ChgAreaPipe(void);
void ChgAreaMode(void);
void PlayerChangeSize(void);
void PlayerInjuryBlink(void);
void InitChangeSize(void);
void PlayerDeath(void);
void DonePlayerTask(void);
void PlayerFireFlower(void);
void CyclePlayerPalette(void);
void ResetPalFireFlower(void);
void ResetPalStar(void);
void FlagpoleSlide(void);
void PlayerEndLevel(void);
void NextArea(void);
void MovePlayerYAxis(void);
void EnterSidePipe(void);
void PlayerMovementSubs(void);
void OnGroundStateSub(void);
void FallingSub(void);
void JumpSwimSub(void);
void LRAir(void);
void ClimbingSub(void);
void PlayerPhysicsSub(void);
void GetPlayerAnimSpeed(void);
void ImposeFriction(void);
void ProcFireball_Bubble(void);
void FireballObjCore(void);
void BubbleCheck(void);
void SetupBubble(void);
void MoveBubl(void);
void RunGameTimer(void);
void WarpZoneObject(void);
void ProcessWhirlpools(void);
void ImposeGravity(void);
void FlagpoleRoutine(void);
void JumpspringHandler(void);
void Setup_Vine(void);
void VineObjectHandler(void);
void ProcessCannons(void);
void BulletBillHandler(void);
void EnemyGfxHandler(void);
void SprObjectOffscrChk(void);
void SpawnHammerObj(void);
void ProcHammerObj(void);
void CoinBlock(void);
void SetupJumpCoin(void);
void JCoinC(void);
void FindEmptyMiscSlot(void);
void MiscObjectsCore(void);
void GiveOneCoin(void);
void AddToScore(void);
void GetSBNybbles(void);
void UpdateNumber(void);
void SetupPowerUp(void);
void PwrUpJmp(void);
void PowerUpObjHandler(void);
void PlayerHeadCollision(void);
void InitBlock_XY_Pos(void);
void BumpBlock(void);
void MushFlowerBlock(void);
void StarBlock(void);
void ExtraLifeMushBlock(void);
void ExtraLifeMushBlockSkip(void);
void VineBlock(void);
void BlockBumpedChk(void);
void BrickShatter(void);
void CheckTopOfBlock(void);
void SpawnBrickChunks(void);
void BlockObjectsCore(void);
void BlockObjMT_Updater(void);
void MoveEnemyHorizontally(void);
void MovePlayerHorizontally(void);
void MoveObjectHorizontally(void);
void MovePlayerVertically(void);
void MoveD_EnemyVertically(void);
void MoveFallingPlatform(void);
void ContVMove(void);
void MoveRedPTroopaDown(void);
void MoveRedPTroopaUp(void);
void MoveRedPTroopa(void);
void MoveDropPlatform(void);
void MoveEnemySlowVert(void);
void SetMdMax(void);
void MoveJ_EnemyVertically(void);
void SetHiMax(void);
void SetXMoveAmt(void);
void ImposeGravityBlock(void);
void ImposeGravitySprObj(void);
void MovePlatformDown(void);
void MovePlatformUp(void);
void MovePlatformUpSkip(void);
void RedPTroopaGrav(void);
void EnemiesAndLoopsCore(void);
void ExecGameLoopback(void);
void ProcLoopCommand(void);
void InitEnemyObject(void);
void DoGroup(void);
void ParseRow0e(void);
void Inc2B(void);
void CheckThreeBytes(void);
void Inc3B(void);
void CheckpointEnemyID(void);
void NoInitCode(void);
void InitGoomba(void);
void InitPodoboo(void);
void InitRetainerObj(void);
void InitNormalEnemy(void);
void SetESpd(void);
void InitRedKoopa(void);
void InitHammerBro(void);
void InitHorizFlySwimEnemy(void);
void InitBloober(void);
void SmallBBox(void);
void InitRedPTroopa(void);
void TallBBox(void);
void SetBBox(void);
void InitVStf(void);
void InitBulletBill(void);
void InitCheepCheep(void);
void InitLakitu(void);
void SetupLakitu(void);
void KillLakitu(void);
void LakituAndSpinyHandler(void);
void InitLongFirebar(void);
void InitShortFirebar(void);
void InitFlyingCheepCheep(void);
void InitBowser(void);
void InitBowserFlame(void);
void PutAtRightExtent(void);
void SpawnFromMouth(void);
void FinishFlame(void);
void InitFireworks(void);
void BulletBillCheepCheep(void);
void HandleGroupEnemies(void);
void InitPiranhaPlant(void);
void InitEnemyFrenzy(void);
void NoFrenzyCode(void);
void EndFrenzy(void);
void InitJumpGPTroopa(void);
void TallBBox2(void);
void SetBBox2(void);
void InitBalPlatform(void);
void InitDropPlatform(void);
void InitHoriPlatform(void);
void InitVertPlatform(void);
void LargeLiftUp(void);
void LargeLiftBBox(void);
void CommonPlatCode(void);
void SPBBox(void);
void LargeLiftDown(void);
void PlatLiftUp(void);
void PlatLiftDown(void);
void CommonSmallLift(void);
void EndOfEnemyInitCode(void);
void DuplicateEnemyObj(void);
void PosPlatform(void);
void RunEnemyObjectsCore(void);
void NoRunCode(void);
void RunRetainerObj(void);
void RunNormalEnemies(void);
void RunBowserFlame(void);
void RunFirebarObj(void);
void RunSmallPlatform(void);
void RunLargePlatform(void);
void RunBowser(void);
void KillAllEnemies(void);
void BowserControl(void);
void BowserGfxHandler(void);
void RunFireworks(void);
void RunStarFlagObj(void);
void GameTimerFireworks(void);
void IncrementSFTask1(void);
void StarFlagExit(void);
void AwardGameTimerPoints(void);
void EndAreaPoints(void);
void RaiseFlagSetoffFWorks(void);
void DrawStarFlag(void);
void DrawFlagSetTimer(void);
void IncrementSFTask2(void);
void DelayToAreaEnd(void);
void OffscreenBoundsCheck(void);
void EnemyMovementSubs(void);
void NoMoveCode(void);
void MovePodoboo(void);
void ProcHammerBro(void);
void SetHJ(void);
void MoveHammerBroXDir(void);
void MoveNormalEnemy(void);
void MoveDefeatedEnemy(void);
void ChkKillGoomba(void);
void MoveJumpingEnemy(void);
void ProcMoveRedPTroopa(void);
void MoveFlyGreenPTroopa(void);
void MoveBloober(void);
void MoveBulletBill(void);
void MoveSwimmingCheepCheep(void);
void MoveFlyingCheepCheep(void);
void MoveLakitu(void);
void MovePiranhaPlant(void);
void LargePlatformSubroutines(void);
void BalancePlatform(void);
void StopPlatforms(void);
void YMovingPlatform(void);
void ChkYPCollision(void);
void XMovingPlatform(void);
void PositionPlayerOnHPlat(void);
void DropPlatform(void);
void RightPlatform(void);
void MoveLargeLiftPlat(void);
void EraseEnemyObject(void);
void XMoveCntr_GreenPTroopa(void);
void XMoveCntr_Platform(void);
void MoveWithXMCntrs(void);
void ProcSwimmingB(void);
void ProcFirebar(void);
void DrawFirebar_Collision(void);
void FirebarCollision(void);
void GetFirebarPosition(void);
void PlayerLakituDiff(void);
void ProcessBowserHalf(void);
void SetFlameTimer(void);
void ProcBowserFlame(void);
void FirebarSpin(void);
void SetupPlatformRope(void);
void InitPlatformFall(void);
void PlatformFall(void);
void MoveSmallPlatform(void);
void MoveLiftPlatforms(void);
void ChkSmallPlatCollision(void);
void FireballEnemyCollision(void);
void HandleEnemyFBallCol(void);
void ShellOrBlockDefeat(void);
void EnemySmackScore(void);
void PlayerHammerCollision(void);
void HandlePowerUpCollision(void);
void UpToFiery(void);
void PlayerEnemyCollision(void);
void InjurePlayer(void);
void ForceInjury(void);
void SetKRout(void);
void SetPRout(void);
void ExInjColRoutines(void);
void KillPlayer(void);
void EnemyStomped(void);
void ChkEnemyFaceRight(void);
void LInj(void);
void EnemyFacePlayer(void);
void SetupFloateyNumber(void);
void EnemiesCollision(void);
void ECLoop(void);
void ReadyNextEnemy(void);
void ExitECRoutine(void);
void ProcEnemyCollisions(void);
void EnemyTurnAround(void);
void RXSpd(void);
void LargePlatformCollision(void);
void ChkForPlayerC_LargeP(void);
void ExLPC(void);
void SmallPlatformCollision(void);
void ProcLPlatCollisions(void);
void PositionPlayerOnS_Plat(void);
void PositionPlayerOnVPlat(void);
void PositionPlayerOnVPlatSkip(void);
void CheckPlayerVertical(void);
void GetEnemyBoundBoxOfs(void);
void GetEnemyBoundBoxOfsArg(void);
void PlayerBGCollision(void);
void ErACM(void);
void ImpedePlayerMove(void);
void HandleClimbing(void);
void ChkInvisibleMTiles(void);
void ChkForLandJumpSpring(void);
void ChkJumpspringMetatiles(void);
void HandlePipeEntry(void);
void CheckForSolidMTiles(void);
void CheckForClimbMTiles(void);
void CheckForCoinMTiles(void);
void GetMTileAttrib(void);
void EnemyToBGCollisionDet(void);
void ChkToStunEnemies(void);
void SetStun(void);
void LandEnemyProperly(void);
void ChkForRedKoopa(void);
void DoEnemySideCheck(void);
void ChkForBump_HammerBroJ(void);
void EnemyJump(void);
void PlayerEnemyDiff(void);
void EnemyLanding(void);
void SubtEnemyYPos(void);
void HammerBroBGColl(void);
void KillEnemyAboveBlock(void);
void UnderHammerBro(void);
void NoUnderHammerBro(void);
void ChkUnderEnemy(void);
void BlockBufferChk_Enemy(void);
void ChkForNonSolids(void);
void FireballBGCollision(void);
void GetFireballBoundBox(void);
void GetMiscBoundBox(void);
void FBallB(void);
void GetEnemyBoundBox(void);
void SmallPlatformBoundBox(void);
void GetMaskedOffScrBits(void);
void MoveBoundBoxOffscreen(void);
void LargePlatformBoundBox(void);
void SetupEOffsetFBBox(void);
void CheckRightScreenBBox(void);
void BoundingBoxCore(void);
void PlayerCollisionCore(void);
void SprObjectCollisionCore(void);
void BlockBufferChk_FBall(void);
void BBChk_E(void);
void BlockBufferColli_Feet(void);
void BlockBufferColli_Head(void);
void BlockBufferColli_Side(void);
void BlockBufferColli_SideSkip(void);
void BlockBufferCollision(void);
void DrawVine(void);
void SixSpriteStacker(void);
void DrawHammer(void);
void FlagpoleGfxHandler(void);
void MoveSixSpritesOffscreen(void);
void DumpSixSpr(void);
void DumpFourSpr(void);
void DumpThreeSpr(void);
void DumpTwoSpr(void);
void DrawLargePlatform(void);
void DrawFloateyNumber_Coin(void);
void JCoinGfxHandler(void);
void DrawPowerUp(void);
void DrawEnemyObjRow(void);
void DrawOneSpriteRow(void);
void MoveESprRowOffscreen(void);
void MoveESprColOffscreen(void);
void DrawBlock(void);
void ChkLeftCo(void);
void MoveColOffscreen(void);
void DrawBrickChunks(void);
void DrawFireball(void);
void DrawFirebar(void);
void DrawExplosion_Fireball(void);
void DrawExplosion_Fireworks(void);
void KillFireBall(void);
void DrawSmallPlatform(void);
void DrawBubble(void);
void DrawPlayer_Intermediate(void);
void RenderPlayerSub(void);
void DrawPlayerLoop(void);
void ProcessPlayerAction(void);
void GetCurrentAnimOffset(void);
void FourFrameExtent(void);
void ThreeFrameExtent(void);
void AnimationControl(void);
void GetGfxOffsetAdder(void);
void HandleChangeSize(void);
void GetOffsetFromAnimCtrl(void);
void ShrinkPlayer(void);
void ChkForPlayerAttrib(void);
void RelativePlayerPosition(void);
void RelativeBubblePosition(void);
void RelativeFireballPosition(void);
void RelWOfs(void);
void RelativeMiscPosition(void);
void RelativeEnemyPosition(void);
void RelativeBlockPosition(void);
void VariableObjOfsRelPos(void);
void GetObjRelativePosition(void);
void GetPlayerOffscreenBits(void);
void GetFireballOffscreenBits(void);
void GetBubbleOffscreenBits(void);
void GetMiscOffscreenBits(void);
void GetProperObjOffset(void);
void GetEnemyOffscreenBits(void);
void GetBlockOffscreenBits(void);
void SetOffscrBitsOffset(void);
void GetOffScreenBitsSet(void);
void RunOffscrBitsSubs(void);
void GetXOffscreenBits(void);
void GetYOffscreenBits(void);
void DividePDiff(void);
void DrawSpriteObject(void);
void SoundEngine(void);
void Dump_Squ1_Regs(void);
void PlaySqu1Sfx(void);
void SetFreq_Squ1(void);
void Dump_Freq_Regs(void);
void Dump_Sq2_Regs(void);
void PlaySqu2Sfx(void);
void SetFreq_Squ2(void);
void SetFreq_Tri(void);
void PlayFlagpoleSlide(void);
void PlaySmallJump(void);
void PlayBigJump(void);
void JumpRegContents(void);
void ContinueSndJump(void);
void FPS2nd(void);
void DmpJpFPS(void);
void PlayFireballThrow(void);
void PlayBump(void);
void Fthrow(void);
void ContinueBumpThrow(void);
void DecJpFPS(void);
void Square1SfxHandler(void);
void BranchToDecLength1(void);
void PlaySmackEnemy(void);
void ContinueSmackEnemy(void);
void DecrementSfx1Length(void);
void StopSquare1Sfx(void);
void PlayPipeDownInj(void);
void ContinuePipeDownInj(void);
void PlayCoinGrab(void);
void PlayTimerTick(void);
void CGrab_TTickRegL(void);
void ContinueCGrabTTick(void);
void DecrementSfx2Length(void);
void EmptySfx2Buffer(void);
void StopSquare2Sfx(void);
void PlayBlast(void);
void ContinueBlast(void);
void SBlasJ(void);
void PlayPowerUpGrab(void);
void ContinuePowerUpGrab(void);
void LoadSqu2Regs(void);
void JumpToDecLength2(void);
void BlstSJp(void);
void ContinueBowserFall(void);
void PBFRegs(void);
void EL_LRegs(void);
void PlayExtraLife(void);
void ContinueExtraLife(void);
void PlayGrowPowerUp(void);
void PlayGrowVine(void);
void GrowItemRegs(void);
void ContinueGrowItems(void);
void Square2SfxHandler(void);
void PlayBowserFall(void);
void PlayBrickShatter(void);
void ContinueBrickShatter(void);
void PlayNoiseSfx(void);
void DecrementSfx3Length(void);
void NoiseSfxHandler(void);
void ContinueMusic(void);
void MusicHandler(void);
void LoadEventMusic(void);
void LoadAreaMusic(void);
void GMLoopB(void);
void HandleAreaMusicLoopB(void);
void FindEventMusicHeader(void);
void LoadHeader(void);
void HandleSquare2Music(void);
void AlternateLengthHandler(void);
void ProcessLengthData(void);
void LoadControlRegs(void);
void LoadEnvelopeData(void);

#endif
