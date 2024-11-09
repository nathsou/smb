#ifndef SMB_CODE_H
#define SMB_CODE_H

#include "data.h"
#include "instructions.h"

typedef enum { RUN_STATE_RESET, RUN_STATE_NMI_HANDLER } RunState;

void smb(RunState state);

void SpriteShuffler(void);
void GoContinue(void);
void DrawMushroomIcon(void);
void DemoEngine(void);
void ColorRotation(void);
void RemBridge(void);
void InitScroll(void);
void WritePPUReg1(void);
void OutputNumbers(void);
void TopScoreCheck(void);
void InitializeMemory(void);
void GetAreaMusic(void);
void SetupGameOver(void);
void TransposePlayers(void);
void DoNothing2(void);
void ScrollLockObject(void);
void KillEnemies(void);
void AreaFrenzy(void);
void FindEmptyEnemySlot(void);
void GetLrgObjAttrib(void);
void GetAreaObjXPosition(void);
void GetAreaObjYPosition(void);
void GetBlockBufferAddr(void);
void GetScreenPosition(void);
void MovePlayerYAxis(void);
void GetPlayerAnimSpeed(void);
void ImposeFriction(void);
void Setup_Vine(void);
void FindEmptyMiscSlot(void);
void PwrUpJmp(void);
void InitBlock_XY_Pos(void);
void BlockBumpedChk(void);
void SpawnBrickChunks(void);
void ImposeGravity(void);
void NoInitCode(void);
void InitRetainerObj(void);
void InitVStf(void);
void InitBulletBill(void);
void InitFireworks(void);
void NoFrenzyCode(void);
void EndFrenzy(void);
void InitVertPlatform(void);
void PosPlatform(void);
void EndOfEnemyInitCode(void);
void NoRunCode(void);
void NoMoveCode(void);
void EraseEnemyObject(void);
void GetFirebarPosition(void);
void SetupPlatformRope(void);
void CheckPlayerVertical(void);
void GetEnemyBoundBoxOfsArg(void);
void ChkInvisibleMTiles(void);
void ChkJumpspringMetatiles(void);
void HandlePipeEntry(void);
void ImpedePlayerMove(void);
void PlayerEnemyDiff(void);
void SixSpriteStacker(void);
void DrawFirebar(void);
void DrawBubble(void);
void GetGfxOffsetAdder(void);
void ChkForPlayerAttrib(void);
void GetObjRelativePosition(void);
void DividePDiff(void);

#endif
