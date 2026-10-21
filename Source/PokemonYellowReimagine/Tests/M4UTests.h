// m4u: the phone layout's tests and shots (Hud/PYRPhoneLayout).
// Every phone test and shot runs the game at 844 x 390 (the stream page's size on a phone held sideways), switches the
// touch controls on with the stream page's own message, and draws the page's controls in the game (the touch guide) so
// their widgets can be measured and seen. Shots (-PYRShot=<dir>/<name>.png):
// - m4u-phone-battle: a wild Pidgey on Route 1, both cards in the top corners, the moves in the centre column.
// - m4u-phone-party: START, POKéMON with six in the party, the list in the column between the stick and the buttons.
// - m4u-phone-party-menu: the same with STATS / SWITCH / CANCEL open for the first one.
// - m4u-phone-overworld: Route 1, the minimap in the top right below the top bar, a text box in the column.
#pragma once

#include "CoreMinimal.h"

class UObject;

namespace PYRM4UShots
{
    // Called by the game mode's shot staging every tick; true when the shot is one of m4u's.
    bool Tick(UObject* Ctx, const FString& ShotName, int32& Step, double& Clock);
}
