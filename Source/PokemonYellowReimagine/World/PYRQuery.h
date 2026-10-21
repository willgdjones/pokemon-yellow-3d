// Small world queries shared by the battle ring and camera: tagged actors, the walkable ground under a point, and the
// painted ring material.
#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;

namespace PYRWorld
{
    POKEMONYELLOWREIMAGINE_API void FindAllTagged(const UWorld* World, FName Tag, TArray<AActor*>& Out);
    // The first surface a walking body would stand on below RefZ + Up (down to RefZ - Down).
    POKEMONYELLOWREIMAGINE_API bool Ground(const UWorld* World, const FVector& XY, float RefZ, FHitResult& Hit, float Up, float Down);
    // /Game/Encounters/M_Paint with Colour on every slot.
    POKEMONYELLOWREIMAGINE_API void Paint(UPrimitiveComponent* Comp, const FLinearColor& Colour);
}
