// Versioned JSON save slot (a reusable library piece). Replace POKEMONYELLOWREIMAGINE_API with your module's API macro.
//
// Versioned save and load of any USTRUCT to a named slot: <ProjectSaved>/SaveGames/<Slot>.json:
// canonical text with a fixed header, bounded reads, validation before anything is applied,
// atomic replacement (write a temp file, flush, rename over the old one; the old file is never deleted first) and
// at most one rejected file kept as <Slot>.json.invalid for evidence.
//
//   FMySave Data; Data.Level = 3;
//   FSaveSlot::Save(TEXT("slot1"), Data, /*Version*/ 2);
//   ESaveSlotResult Result = FSaveSlot::Load(TEXT("slot1"), Data, 2);   // Loaded, Missing, Invalid, Outdated
//
// Only UPROPERTY fields are saved (FJsonObjectConverter). Out is only written when the result is Loaded.
#pragma once

#include "CoreMinimal.h"

enum class ESaveSlotResult : uint8
{
    Loaded,    // Out now holds the saved data.
    Missing,   // No save in this slot (a new game).
    Invalid,   // Unreadable, too big, tampered or another struct: kept as <Slot>.json.invalid, Out untouched.
    Outdated,  // A valid save of another version: FoundVersion says which; migrate it or start again.
};

class POKEMONYELLOWREIMAGINE_API FSaveSlot
{
public:
    // Largest file Load reads; anything bigger is Invalid.
    static constexpr int64 MaxBytes = 256 * 1024;

    // Letters, digits, '-' and '_', 1-64 characters (no paths).
    static bool IsValidName(const FString& Slot);
    static FString Directory();
    static FString PathFor(const FString& Slot);
    static bool Exists(const FString& Slot);
    static bool Delete(const FString& Slot);

    static bool SaveStruct(const FString& Slot, const UScriptStruct* Type, const void* Data, int32 Version);
    static ESaveSlotResult LoadStruct(const FString& Slot, const UScriptStruct* Type, void* Out, int32 Version,
        int32* FoundVersion = nullptr);

    template <typename T> static bool Save(const FString& Slot, const T& Data, int32 Version)
    {
        return SaveStruct(Slot, T::StaticStruct(), &Data, Version);
    }
    template <typename T> static ESaveSlotResult Load(const FString& Slot, T& Out, int32 Version, int32* FoundVersion = nullptr)
    {
        return LoadStruct(Slot, T::StaticStruct(), &Out, Version, FoundVersion);
    }

    static const TCHAR* ResultName(ESaveSlotResult Result);
};
