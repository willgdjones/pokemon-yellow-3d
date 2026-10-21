// Per-world glue owned by the battle folder: B opens the badge case anywhere (in battle too), and the flat test pad
// the battle tests fight on (far from the Game Boy maps and the stages). m4: the m2/m3 scene staging for battle
// screenshots is retired; the fights and their shots run on the classic stages (Stage/PYREncounters).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRBattleWorld.generated.h"

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRBattleWorld : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(const EWorldType::Type Type) const override { return Type == EWorldType::Game || Type == EWorldType::PIE; }
    virtual void Deinitialize() override;
    virtual void Tick(float Dt) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRBattleWorld, STATGROUP_Tickables); }

    // Builds the flat 40 m test pad with a 100 cm ledge box at PadCentre once; returns its centre.
    static FVector EnsureTestPad(UWorld* World);
    static const FVector PadCentre;
};
