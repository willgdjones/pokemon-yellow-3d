// m4v: the designer's shots of the anime look (-PYRShot=<dir>/<name>.png):
// - m4v-pallet-town, m4v-viridian-forest, m4v-mt-moon: the frame of that name in Content/Look/Kit/look-frames.json,
//   staged as its look test stages it, from its fixed camera, once the area's grade has settled;
// - m4v-battle-stage: a Route 1 wild battle at 844 x 390 with the battle HUD and touch on (m4u's m4u-phone-battle staging).
#pragma once

#include "CoreMinimal.h"

class UObject;

namespace PYRM4VShots
{
    // Called by the game mode's shot staging every tick; true when the shot is one of m4v's.
    bool Tick(UObject* Ctx, const FString& ShotName, int32& Step, double& Clock);
}
