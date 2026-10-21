// m4t's staged screenshots (-PYRShot=<dir>/<name>.png):
// - m4t-learn-move: Pikachu Lv 8 on Route 1's grass stage, "Which move should be forgotten?" with THUNDER WAVE as the
//   new move, the cursor on GROWL, the fight paused behind it.
// - m4t-switch-paused: Viridian Forest, the switch menu open mid-fight with the PAUSED tint and a wild Weedle's Poison
//   Sting frozen in the air.
// - m4t-run-wild: a wild Pidgey on Route 1 just after RUN: the button lit and "Got away safely!".
// - m4t-run-trainer: Brock's Onix in Pewter Gym after RUN: "No! There's no running from a trainer battle!".
#pragma once

#include "CoreMinimal.h"

class UObject;

namespace PYRM4TShots
{
    // Called by the game mode's shot staging every tick; true when the shot is one of m4t's (it staged this tick).
    bool Tick(UObject* Ctx, const FString& ShotName, int32& Step, double& Clock);
}
