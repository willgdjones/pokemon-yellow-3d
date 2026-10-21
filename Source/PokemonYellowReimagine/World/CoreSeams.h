// Test flags other systems read so a test can keep the road quiet.
#pragma once

#include "CoreMinimal.h"

namespace PYRTestFlags
{
    // bNoEncounters: no random wild encounters and no trainer sight lines (Stage/PYREncounters, Kanto/PYRScripts).
    // bNoBlocks: the roadblocks (Oak at the grass, the parcel, the old man, the Pewter boy, the gate) stay out of the way.
    POKEMONYELLOWREIMAGINE_API extern bool bNoEncounters;
    POKEMONYELLOWREIMAGINE_API extern bool bNoBlocks;
}
