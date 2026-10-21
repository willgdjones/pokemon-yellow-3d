// Pikachu's happiness and mood as Yellow keeps them (pikachu.json, engine/events/pikachu_happiness.asm): happiness
// 0-255 changes by banded deltas (level up, fainting, items, gym leaders, walking...) and each event pulls the mood
// toward a target; the face Red sees when he talks to Pikachu comes from both. The numbers live in UPYRTrainerState
// (BondPoints, PikachuMood); this is the read and change side other folders use. The two battle abilities unlock at
// Close (150: Pikachu dodges one attack by itself per battle) and Best friends (200: Thundershock recharges a third
// faster).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PYRBond.generated.h"

namespace PYRBond
{
    POKEMONYELLOWREIMAGINE_API FString TierName(int32 Tier);
    // Apply one of Yellow's happiness events (levelup, fainted, used_item, gym_leader, walking, deposited, traded,
    // careless_trainer, poison_fainted, used_tm_hm, used_x_item). Returns the change.
    POKEMONYELLOWREIMAGINE_API int32 Apply(const UObject* Ctx, const FString& Event);
    // The face picture's name for the current happiness and mood ("happy", "grumpy"...).
    POKEMONYELLOWREIMAGINE_API FString Face(const UObject* Ctx);
    // A coarse mood word for older callers (grumpy, content, happy).
    POKEMONYELLOWREIMAGINE_API FName MoodFor(int32 Friendship);
    POKEMONYELLOWREIMAGINE_API const TCHAR* MoodText(int32 Friendship);
}

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRBond : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static UPYRBond* Get(const UObject* Ctx);
    int32 GetFriendship() const;
    FName GetMood() const { return PYRBond::MoodFor(GetFriendship()); }
};
