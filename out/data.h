#ifndef SMB_DATA_H
#define SMB_DATA_H

#include "stdint.h"
#include "constants.h"

extern const uint8_t data[9465];

#define VRAM_AddrTable_Low 0x8000
#define VRAM_AddrTable_High 0x8013
#define VRAM_Buffer_Offset 0x8026
#define WSelectBufferTemplate 0x8028
#define MushroomIconData 0x802e
#define DemoActionData 0x8036
#define DemoTimingData 0x804b
#define FloateyNumTileData 0x8061
#define ScoreUpdateData 0x8079
#define AreaPalette 0x8085
#define BGColorCtrl_Addr 0x8089
#define BackgroundColors 0x808d
#define PlayerColors 0x8095
#define GameText 0x80a1
#define TopStatusBarLine 0x80a1
#define WorldLivesDisplay 0x80c8
#define TwoPlayerTimeUp 0x80e7
#define OnePlayerTimeUp 0x80ef
#define TwoPlayerGameOver 0x80fa
#define OnePlayerGameOver 0x8102
#define WarpZoneWelcome 0x810f
#define LuigiName 0x813c
#define WarpZoneNumbers 0x8141
#define GameTextOffsets 0x814d
#define ColorRotatePalette 0x8157
#define BlankPalette 0x815d
#define Palette3Data 0x8165
#define BlockGfxData 0x8175
#define MetatileGraphics_Low 0x8189
#define MetatileGraphics_High 0x818d
#define Palette0_MTiles 0x8191
#define Palette1_MTiles 0x822d
#define Palette2_MTiles 0x82e5
#define Palette3_MTiles 0x830d
#define WaterPaletteData 0x8325
#define GroundPaletteData 0x8349
#define UndergroundPaletteData 0x836d
#define CastlePaletteData 0x8391
#define DaySnowPaletteData 0x83b5
#define NightSnowPaletteData 0x83bd
#define MushroomPaletteData 0x83c5
#define BowserPaletteData 0x83cd
#define MarioThanksMessage 0x83d5
#define LuigiThanksMessage 0x83e9
#define MushroomRetainerSaved 0x83fd
#define PrincessSaved1 0x8429
#define PrincessSaved2 0x8440
#define WorldSelectMessage1 0x845f
#define WorldSelectMessage2 0x8470
#define StatusBarData 0x8485
#define StatusBarOffset 0x8491
#define DefaultSprOffsets 0x8497
#define Sprite0Data 0x84a6
#define MusicSelectData 0x84aa
#define PlayerStarting_X_Pos 0x84b0
#define AltYPosOffset 0x84b4
#define PlayerStarting_Y_Pos 0x84b6
#define PlayerBGPriorityData 0x84bf
#define GameTimerData 0x84c7
#define HalfwayPageNybbles 0x84cb
#define BSceneDataOffsets 0x84db
#define BackSceneryData 0x84de
#define BackSceneryMetatiles 0x856e
#define FSceneDataOffsets 0x8592
#define ForeSceneryData 0x8595
#define TerrainMetatiles 0x85bc
#define TerrainRenderBits 0x85c0
#define BlockBuffLowBounds 0x85e0
#define FrenzyIDData 0x85e4
#define PulleyRopeMetatiles 0x85e7
#define CastleMetatiles 0x85ea
#define SidePipeShaftData 0x8621
#define SidePipeTopPart 0x8625
#define SidePipeBottomPart 0x8629
#define VerticalPipeData 0x862d
#define CoinMetatileData 0x8635
#define C_ObjectRow 0x8639
#define C_ObjectMetatile 0x863c
#define SolidBlockMetatiles 0x863f
#define BrickMetatiles 0x8643
#define StaircaseHeightData 0x8648
#define StaircaseRowData 0x8651
#define HoleMetatiles 0x865a
#define BlockBufferAddr 0x865e
#define AreaDataOfsLoopback 0x8662
#define WorldAddrOffsets 0x866d
#define AreaAddrOffsets 0x8675
#define World1Areas 0x8675
#define World2Areas 0x867a
#define World3Areas 0x867f
#define World4Areas 0x8683
#define World5Areas 0x8688
#define World6Areas 0x868c
#define World7Areas 0x8690
#define World8Areas 0x8695
#define EnemyAddrHOffsets 0x8699
#define EnemyDataAddrLow 0x869d
#define EnemyDataAddrHigh 0x86bf
#define AreaDataHOffsets 0x86e1
#define AreaDataAddrLow 0x86e5
#define AreaDataAddrHigh 0x8707
#define E_CastleArea1 0x8729
#define E_CastleArea2 0x8750
#define E_CastleArea3 0x8769
#define E_CastleArea4 0x8798
#define E_CastleArea5 0x87c3
#define E_CastleArea6 0x87d8
#define E_GroundArea1 0x8812
#define E_GroundArea2 0x8837
#define E_GroundArea3 0x8854
#define E_GroundArea4 0x8862
#define E_GroundArea5 0x8889
#define E_GroundArea6 0x88ba
#define E_GroundArea7 0x88d8
#define E_GroundArea8 0x88f5
#define E_GroundArea9 0x890a
#define E_GroundArea10 0x8934
#define E_GroundArea11 0x8935
#define E_GroundArea12 0x8959
#define E_GroundArea13 0x8962
#define E_GroundArea14 0x8987
#define E_GroundArea15 0x89aa
#define E_GroundArea16 0x89b3
#define E_GroundArea17 0x89b4
#define E_GroundArea18 0x89ee
#define E_GroundArea19 0x8a19
#define E_GroundArea20 0x8a47
#define E_GroundArea21 0x8a63
#define E_GroundArea22 0x8a6c
#define E_UndergroundArea1 0x8a91
#define E_UndergroundArea2 0x8abe
#define E_UndergroundArea3 0x8aec
#define E_WaterArea1 0x8b19
#define E_WaterArea2 0x8b2a
#define E_WaterArea3 0x8b54
#define L_CastleArea1 0x8b68
#define L_CastleArea2 0x8bc9
#define L_CastleArea3 0x8c48
#define L_CastleArea4 0x8cbb
#define L_CastleArea5 0x8d28
#define L_CastleArea6 0x8db3
#define L_GroundArea1 0x8e24
#define L_GroundArea2 0x8e87
#define L_GroundArea3 0x8ef0
#define L_GroundArea4 0x8f43
#define L_GroundArea5 0x8fd2
#define L_GroundArea6 0x9047
#define L_GroundArea7 0x90ac
#define L_GroundArea8 0x9101
#define L_GroundArea9 0x9186
#define L_GroundArea10 0x91eb
#define L_GroundArea11 0x91f4
#define L_GroundArea12 0x9233
#define L_GroundArea13 0x9248
#define L_GroundArea14 0x92af
#define L_GroundArea15 0x9314
#define L_GroundArea16 0x9387
#define L_GroundArea17 0x93b8
#define L_GroundArea18 0x944b
#define L_GroundArea19 0x94be
#define L_GroundArea20 0x9537
#define L_GroundArea21 0x9590
#define L_GroundArea22 0x95bb
#define L_UndergroundArea1 0x95ee
#define L_UndergroundArea2 0x9691
#define L_UndergroundArea3 0x9732
#define L_WaterArea1 0x97bf
#define L_WaterArea2 0x97fe
#define L_WaterArea3 0x9879
#define X_SubtracterData 0x9895
#define OffscrJoypadBitsData 0x9897
#define Hidden1UpCoinAmts 0x9899
#define ClimbAdderLow 0x98a1
#define ClimbAdderHigh 0x98a5
#define JumpMForceData 0x98a9
#define FallMForceData 0x98b0
#define PlayerYSpdData 0x98b7
#define InitMForceData 0x98be
#define MaxLeftXSpdData 0x98c5
#define MaxRightXSpdData 0x98c8
#define FrictionData 0x98cc
#define Climb_Y_SpeedData 0x98cf
#define Climb_Y_MForceData 0x98d2
#define PlayerAnimTmrData 0x98d5
#define FireballXSpdData 0x98d8
#define Bubble_MForceData 0x98da
#define BubbleTimerData 0x98dc
#define FlagpoleScoreMods 0x98de
#define FlagpoleScoreDigits 0x98e3
#define Jumpspring_Y_PosData 0x98e8
#define VineHeightData 0x98ec
#define CannonBitmasks 0x98ee
#define BulletBillXSpdData 0x98f0
#define HammerEnemyOfsData 0x98f2
#define HammerXSpdData 0x98fb
#define CoinTallyOffsets 0x98fd
#define ScoreOffsets 0x98ff
#define StatusBarNybbles 0x9901
#define BlockYPosAdderData 0x9903
#define BrickQBlockMetatiles 0x9905
#define MaxSpdBlockData 0x9913
#define LoopCmdWorldNumber 0x9915
#define LoopCmdPageNumber 0x9920
#define LoopCmdYPosition 0x992b
#define NormalXSpdData 0x9936
#define HBroWalkingTimerData 0x9938
#define PRDiffAdjustData 0x993a
#define FirebarSpinSpdData 0x9946
#define FirebarSpinDirData 0x994b
#define FlyCCXPositionData 0x9950
#define FlyCCXSpeedData 0x9960
#define FlyCCTimerData 0x996c
#define FlameYPosData 0x9970
#define FlameYMFAdderData 0x9974
#define FireworksXPosData 0x9976
#define FireworksYPosData 0x997c
#define Bitmasks 0x9982
#define Enemy17YPosData 0x998a
#define SwimCC_IDData 0x9992
#define PlatPosDataLow 0x9994
#define PlatPosDataHigh 0x9997
#define HammerThrowTmrData 0x999a
#define XSpeedAdderData 0x999c
#define RevivedXSpeed 0x99a0
#define HammerBroJumpLData 0x99a4
#define BlooberBitmasks 0x99a6
#define SwimCCXMoveData 0x99a8
#define FirebarPosLookupTbl 0x99ac
#define FirebarMirrorData 0x9a0f
#define FirebarTblOffsets 0x9a13
#define FirebarYPos 0x9a1f
#define PRandomSubtracter 0x9a21
#define FlyCCBPriority 0x9a26
#define LakituDiffAdj 0x9a2b
#define BridgeCollapseData 0x9a2e
#define PRandomRange 0x9a3d
#define FlameTimerData 0x9a41
#define StarFlagYPosAdder 0x9a49
#define StarFlagXPosAdder 0x9a4d
#define StarFlagTileData 0x9a51
#define BowserIdentities 0x9a55
#define ResidualXSpdData 0x9a5d
#define KickedShellXSpdData 0x9a5f
#define DemotedKoopaXSpdData 0x9a61
#define KickedShellPtsData 0x9a63
#define StompedEnemyPtsData 0x9a66
#define RevivalRateData 0x9a6a
#define SetBitsMask 0x9a6c
#define ClearBitsMask 0x9a73
#define PlayerPosSPlatData 0x9a7a
#define PlayerBGUpperExtent 0x9a7c
#define AreaChangeTimerData 0x9a7e
#define ClimbXPosAdder 0x9a80
#define ClimbPLocAdder 0x9a82
#define FlagpoleYPosData 0x9a84
#define SolidMTileUpperExt 0x9a89
#define ClimbMTileUpperExt 0x9a8d
#define EnemyBGCStateData 0x9a91
#define EnemyBGCXSpdData 0x9a97
#define BoundBoxCtrlData 0x9a99
#define BlockBufferAdderData 0x9ac9
#define BlockBuffer_X_Adder 0x9acc
#define BlockBuffer_Y_Adder 0x9ae8
#define VineYPosAdder 0x9b04
#define FirstSprXPos 0x9b06
#define FirstSprYPos 0x9b0a
#define SecondSprXPos 0x9b0e
#define SecondSprYPos 0x9b12
#define FirstSprTilenum 0x9b16
#define SecondSprTilenum 0x9b1a
#define HammerSprAttrib 0x9b1e
#define FlagpoleScoreNumTiles 0x9b22
#define JumpingCoinTiles 0x9b2c
#define PowerUpGfxTable 0x9b30
#define PowerUpAttributes 0x9b40
#define EnemyGraphicsTable 0x9b44
#define EnemyGfxTableOffsets 0x9c46
#define EnemyAttributeData 0x9c61
#define EnemyAnimTimingBMask 0x9c7c
#define JumpspringFrameOffsets 0x9c7e
#define DefaultBlockObjTiles 0x9c83
#define ExplosionTiles 0x9c87
#define PlayerGfxTblOffsets 0x9c8a
#define PlayerGraphicsTable 0x9c9a
#define SwimKickTileNum 0x9d6a
#define IntermediatePlayerData 0x9d6c
#define ChangeSizeOffsetAdder 0x9d72
#define ObjOffsetData 0x9d86
#define XOffscreenBitsData 0x9d89
#define DefaultXOnscreenOfs 0x9d99
#define YOffscreenBitsData 0x9d9c
#define DefaultYOnscreenOfs 0x9da5
#define HighPosUnitData 0x9da8
#define SwimStompEnvelopeData 0x9daa
#define ExtraLifeFreqData 0x9db8
#define PowerUpGrabFreqData 0x9dbe
#define PUp_VGrow_FreqData 0x9ddc
#define BrickShatterFreqData 0x9dfc
#define MusicHeaderData 0x9e0c
#define TimeRunningOutHdr 0x9e3d
#define Star_CloudHdr 0x9e42
#define EndOfLevelMusHdr 0x9e48
#define ResidualHeaderData 0x9e4d
#define UndergroundMusHdr 0x9e52
#define SilenceHdr 0x9e57
#define CastleMusHdr 0x9e5b
#define VictoryMusHdr 0x9e60
#define GameOverMusHdr 0x9e65
#define WaterMusHdr 0x9e6a
#define WinCastleMusHdr 0x9e70
#define GroundLevelPart1Hdr 0x9e75
#define GroundLevelPart2AHdr 0x9e7b
#define GroundLevelPart2BHdr 0x9e81
#define GroundLevelPart2CHdr 0x9e87
#define GroundLevelPart3AHdr 0x9e8d
#define GroundLevelPart3BHdr 0x9e93
#define GroundLevelLeadInHdr 0x9e99
#define GroundLevelPart4AHdr 0x9e9f
#define GroundLevelPart4BHdr 0x9ea5
#define GroundLevelPart4CHdr 0x9eab
#define DeathMusHdr 0x9eb1
#define Star_CloudMData 0x9eb7
#define GroundM_P1Data 0x9f00
#define SilenceData 0x9f1b
#define GroundM_P2AData 0x9f48
#define GroundM_P2BData 0x9f74
#define GroundM_P2CData 0x9f9c
#define GroundM_P3AData 0x9fc1
#define GroundM_P3BData 0x9fda
#define GroundMLdInData 0x9ff8
#define GroundM_P4AData 0xa024
#define GroundM_P4BData 0xa04a
#define DeathMusData 0xa071
#define GroundM_P4CData 0xa073
#define CastleMusData 0xa0a3
#define GameOverMusData 0xa144
#define TimeRunOutMusData 0xa171
#define WinLevelMusData 0xa1af
#define UndergroundMusData 0xa210
#define WaterMusData 0xa251
#define EndOfCastleMusData 0xa350
#define VictoryMusData 0xa3c7
#define FreqRegLookupTbl 0xa3ff
#define MusicLengthLookupTbl 0xa465
#define EndOfCastleMusicEnvData 0xa495
#define AreaMusicEnvData 0xa499
#define WaterEventMusEnvData 0xa4a1
#define BowserFlameEnvData 0xa4c9
#define BrickShatterEnvData 0xa4e9

#endif
