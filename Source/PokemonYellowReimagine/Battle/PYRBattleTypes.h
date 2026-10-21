// The battle seam shared with `journey`: what kind of fight, who is in it, where, and how it ended.
#pragma once

#include "CoreMinimal.h"
#include "../Party/PYRSpecies.h"
#include "PYRBattleTypes.generated.h"

UENUM(BlueprintType)
enum class EPYRBattleKind : uint8
{
    Wild,     // A ring on the spot with one worn gap (RequestFlee).
    Trainer,  // A closed ring; the trainer sends the next Pokemon when one faints and blocks Poke Balls.
    Lab,      // Gary in Oak's lab (with bNoBlackout).
    Gym,      // Pewter Gym: the floor is the stage.
    Demo,     // The Viridian old man's catching lesson: the bot plays his side and his ball always catches.
};

UENUM(BlueprintType)
enum class EPYRBattleOutcome : uint8 { Won, Lost, Fled, Caught };

// Everything `journey` passes to APYRBattleManager::StartBattle.
USTRUCT(BlueprintType)
struct FPYRBattleSetup
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EPYRBattleKind Kind = EPYRBattleKind::Wild;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FPYRMonSpec> Foes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName TrainerId;
    // The trainer as Yellow names him in text ("BUG CATCHER", "BROCK"); empty: made from TrainerId.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString TrainerTitle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Prize = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Center = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Radius = 800.f;
    // A crouched approach: the foe is stunned for its first 2 s.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bFoeStunned = false;
    // The lab battle: a loss reports Lost but never blacks out.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bNoBlackout = false;
    // m3, for worker `encounters` (cave rings): the ceiling's height in cm above the floor at Center, as traced in
    // Mt. Moon. 0 means no ceiling. Fliers never hover higher than this minus their half-height, so their top stays
    // under the rock; APYRBattleManager::GetCeilingZ() gives the world height back for the battle camera.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHoverHeightCm = 0.f;
};
// m3 pool arenas need nothing in the setup: any water tagged PYR.Water (with platforms tagged PYR.Platform) that meets
// the ring or gym floor at Center/Radius turns on the pool rules (Battle/Arena/PYRPoolRules.h).

// ---- m1 seam, kept so the m1 encounter code compiles until `journey` moves to StartBattle.
USTRUCT(BlueprintType)
struct FPYRArena
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Center = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Radius = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bGap = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector GapCenter = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bGym = false;
};

USTRUCT(BlueprintType)
struct FPYRFoe
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Species;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 3;
};

UENUM(BlueprintType)
enum class EPYRBattleResult : uint8 { Won, Lost, Fled, Caught };
