#ifndef SMB_CODE_H
#define SMB_CODE_H

#include "data.h"
#include "instructions.h"

typedef enum { RUN_STATE_RESET, RUN_STATE_NMI_HANDLER } RunState;

void smb(RunState state);

void PauseRoutine(void);
void SpriteShuffler(void);
void MoveAllSpritesOffscreen(void);
void MoveSpritesOffscreen(void);
void MoveSpritesOffscreenSkip(void);
void GoContinue(void);
void DrawMushroomIcon(void);
void DemoEngine(void);
void ColorRotation(void);
void ReadJoypads(void);
void ReadPortBits(void);
void WritePPUReg1(void);
void PrintStatusBarNumbers(void);
void OutputNumbers(void);
void DigitsMathRoutine(void);
void UpdateTopScore(void);
void TopScoreCheck(void);
void InitializeMemory(void);
void GetAreaMusic(void);
void TransposePlayers(void);
void IncAreaObjOffset(void);
void KillEnemies(void);
void FindEmptyEnemySlot(void);
void GetAreaObjectID(void);
void GetLrgObjAttrib(void);
void GetAreaObjXPosition(void);
void GetAreaObjYPosition(void);
void GetBlockBufferAddr(void);
void FindAreaPointer(void);
void GetScreenPosition(void);
void MovePlayerYAxis(void);
void PlayerPhysicsSub(void);
void GetPlayerAnimSpeed(void);
void ImposeFriction(void);
void Setup_Vine(void);
void SpawnHammerObj(void);
void FindEmptyMiscSlot(void);
void InitBlock_XY_Pos(void);
void BlockBumpedChk(void);
void SpawnBrickChunks(void);
void ExecGameLoopback(void);
void DuplicateEnemyObj(void);
void PosPlatform(void);
void ProcSwimmingB(void);
void GetFirebarPosition(void);
void PlayerLakituDiff(void);
void FirebarSpin(void);
void SetupPlatformRope(void);
void EnemyFacePlayer(void);
void CheckPlayerVertical(void);
void ChkInvisibleMTiles(void);
void ChkForLandJumpSpring(void);
void ChkJumpspringMetatiles(void);
void HandlePipeEntry(void);
void CheckForCoinMTiles(void);
void PlayerEnemyDiff(void);
void SubtEnemyYPos(void);
void ChkForNonSolids(void);
void BoundingBoxCore(void);
void DrawVine(void);
void SixSpriteStacker(void);
void DrawBubble(void);
void GetGfxOffsetAdder(void);
void ChkForPlayerAttrib(void);
void GetObjRelativePosition(void);
void GetProperObjOffset(void);
void RunOffscrBitsSubs(void);
void GetXOffscreenBits(void);
void DividePDiff(void);
void DrawSpriteObject(void);
void Dump_Squ1_Regs(void);
void Dump_Sq2_Regs(void);
void LoadControlRegs(void);
void LoadEnvelopeData(void);

#endif
