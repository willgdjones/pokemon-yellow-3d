// Shared helpers for the battle tests (BattleTests.cpp for m2, BattleTestsM3.cpp for m3): start a fight through
// StartBattle with a debug party, place and face fighters, read captions.
#pragma once

#include "CoreMinimal.h"
#include "PYRBattleBot.h"
#include "PYRBattleTypes.h"
#include "../GameTest.h"
#include "../Party/PYRSpecies.h"

class APYRAttack;
class APYRBattleManager;
class APYRMonPawn;
class AActor;
class UPYRTrainerState;
class UWorld;

#define PYR_TEST(Id, Name, TimeoutSeconds) \
    static EGameTest PYRTest_##Id(FGameTestContext& Test); \
    static struct FPYRTestRegister_##Id \
    { \
        FPYRTestRegister_##Id() { GameTest::Register(TEXT(Name), &PYRTest_##Id, TimeoutSeconds); } \
    } GPYRTestRegister_##Id; \
    static EGameTest PYRTest_##Id(FGameTestContext& Test)

#define PYR_READY() if (!PYRTest::Ready(Test)) return EGameTest::Running
#define PYR_FAIL(...) do { Test.Detail = FString::Printf(__VA_ARGS__); return EGameTest::Failed; } while (0)
#define PYR_PASS(...) do { Test.Detail = FString::Printf(__VA_ARGS__); return EGameTest::Passed; } while (0)
#define PYR_WAIT(Wait) if (Test.Seconds - St.T0 < (Wait)) return EGameTest::Running

namespace PYRTest
{
struct FState
{
    int32 Step = 0;
    double T0 = 0;
    float A = 0, B = 0, C = 0;
    FVector V = FVector::ZeroVector;
    int32 I = 0;
    FString Note;
    FPYRBattleBot Bot;
};

bool Ready(FGameTestContext& Test);
APYRBattleManager* M(FGameTestContext& Test);
UPYRTrainerState* S(FGameTestContext& Test);
FPYRMonSpec Mon(FName Species, int32 Level, TArray<FName> Moves = {});
// A fight on the m2 test pad (Wild, Trainer, Gym...), passive foe, no dice.
APYRBattleManager* Fight(FGameTestContext& Test, const TArray<FPYRMonSpec>& Party, const TArray<FPYRMonSpec>& Foes,
    EPYRBattleKind Kind = EPYRBattleKind::Wild, FVector FoeOffset = FVector(0, 500, 0));
void Place(APYRMonPawn* P, FVector XY);
void Face(APYRMonPawn* P, const AActor* At);
APYRAttack* FoeAttack(UWorld* World);
bool Near(float A, float B, float Tol);
bool CaptionSeen(APYRBattleManager* Mgr, const FString& Part);
// The foe down from one landed hit with its dice pinned (deterministic in any fight); false when it couldn't land now
// (the fight paused or ending, the foe dived or underground): try again next tick.
bool KnockOut(APYRBattleManager* Mgr);

// m3 arenas: the open pad, the Mt. Moon-style cave (4 m ceiling, pillars) and the Cerulean-style pool.
enum class EArena : uint8 { Open, Cave, Pool };
const TCHAR* ArenaName(EArena Arena);
// A fight in Arena, centred on its middle (the pad's fight spot for Open). FoeAt is relative to the centre (zero: the
// default spot). Radius 0 uses the arena's own (open 1000, cave 1100, pool 1350). The cave passes its 4 m ceiling
// as MaxHoverHeightCm unless Ceiling says otherwise (-1).
APYRBattleManager* FightIn(FGameTestContext& Test, EArena Arena, const TArray<FPYRMonSpec>& Party, const TArray<FPYRMonSpec>& Foes,
    EPYRBattleKind Kind = EPYRBattleKind::Wild, FVector FoeAt = FVector::ZeroVector, float Radius = 0.f, float Ceiling = -1.f);
FVector ArenaCentre(EArena Arena);
}
