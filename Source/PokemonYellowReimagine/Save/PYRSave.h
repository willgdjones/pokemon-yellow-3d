// Saving and continuing (m4). One slot, "pyr-main" (Save/SaveSlot: <Saved>/SaveGames/pyr-main.json, versioned,
// validated and written atomically), holds FPYRSaveGame: Red's map, tile and facing, the journey (UPYRProgress:
// names, options, flags, items taken, trainers beaten, play time, Repel, PC items) and everything Red carries
// (UPYRTrainerState: party, boxes, bag, money, badges, the Pokedex, Pikachu's happiness and mood).
//  - SAVE in the START menu writes it ("Would you like to SAVE the game?").
//  - Launching with a valid save offers CONTINUE on the title; CONTINUE puts Red back on his tile.
//  - Automated runs never touch the player's save: under a test slot the file lives in Saved/SaveGames/slot-<n>/, and
//    without one nothing is read or written unless a test asks (bAllowInAutomation).
#pragma once

#include "CoreMinimal.h"
#include "Kanto/PYRProgress.h"
#include "Party/PYRTrainerState.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRSave.generated.h"

USTRUCT()
struct FPYRSaveGame
{
    GENERATED_BODY()
    static constexpr int32 Version = 2;

    UPROPERTY() FName Map;
    UPROPERTY() int32 TileX = 0;
    UPROPERTY() int32 TileY = 0;
    UPROPERTY() FString Facing;
    UPROPERTY() FName LastOutdoor;
    UPROPERTY() FPYRProgressSnapshot Progress;
    UPROPERTY() FPYRTrainerSnapshot Trainer;
    // A readable summary, checked on load and shown on the title.
    UPROPERTY() FString PlayerName;
    UPROPERTY() int32 Badges = 0;
    UPROPERTY() int32 DexOwned = 0;
    UPROPERTY() double PlaySeconds = 0;
    UPROPERTY() FString SavedAt;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRSave : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRSave* Get(const UObject* Ctx);
    static const TCHAR* SlotName() { return TEXT("pyr-main"); }
    // Tests that exercise saving and continuing set this; other automated runs never read or write a save.
    static bool bAllowInAutomation;

    // The game as a save.
    FPYRSaveGame Capture() const;
    // Write the slot now; false (and Why) when it can't.
    bool SaveNow(const FString& Reason, FString* Why = nullptr);
    // Read the slot: 0 loaded, 1 missing, 2 invalid, 3 outdated.
    int32 Load(FPYRSaveGame& Out) const;
    // Restore a save: the journey, the trainer state, and Red on his tile.
    bool Apply(const FPYRSaveGame& Save);
    // A new game's state (Red is not moved).
    void ResetToNewGame();
    bool Exists() const;
    // A save that loads and whose map is in this build: the title offers CONTINUE only then. An older save (M3's
    // version 1 kept a 3D place in the retired world) stays on disk untouched, a new game begins and SAVE replaces it.
    bool CanContinue();
    bool CanUseDisk() const;
    // Something changed since the last save or load.
    bool HasUnsavedChanges() const;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    // Evidence for tests and the report.
    bool bContinued = false;
    FString LastResult;
    int32 Saves = 0;

private:
    FString Fingerprint(const FPYRSaveGame& S) const;
    FString SavedPrint;
};
