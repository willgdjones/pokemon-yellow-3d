// Stand-ins for Mt. Moon and the Cerulean Gym pool, 150 m under the world (clear of any Kanto terrain), so the battle tests run the cave and water rules
// without depending on the world build (the real places carry the same tags: PYR.Pillar, PYR.Water, PYR.Platform).
//   - The cave (centre (0, 84000, -15000)): a 30 m floor, a rock ceiling 4 m up and three stalagmites tagged PYR.Pillar.
//   - The pool (centre (0, 72000, -15000)): a 26 x 18 m basin whose water top is 40 cm under the walkway (tagged PYR.Water, blocking like a
//     plain mesh), five 4 m platforms tagged PYR.Platform (tops 10 cm under the walkway) 3 m apart, and a walkway all round.
#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace PYRArenas
{
    extern POKEMONYELLOWREIMAGINE_API const FVector CaveCentre, PoolCentre;
    constexpr float CaveCeiling = 400.f;     // Above the cave floor.
    constexpr float PoolSurface = -40.f;     // Water top, relative to PoolCentre.
    constexpr float PlatformTop = -10.f;
    // Platform centres relative to PoolCentre (tops at PlatformTop), and the walkway spot where Red stands (-X side).
    POKEMONYELLOWREIMAGINE_API const TArray<FVector>& PlatformOffsets();
    extern POKEMONYELLOWREIMAGINE_API const FVector PoolWalkway;
    POKEMONYELLOWREIMAGINE_API FVector EnsureCave(UWorld* World);
    POKEMONYELLOWREIMAGINE_API FVector EnsurePool(UWorld* World);
}
