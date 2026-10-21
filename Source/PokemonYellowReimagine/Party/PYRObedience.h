// Yellow's obedience check for traded Pokemon (pret/pokeyellow engine/battle/core.asm, CheckForDisobedience), used
// on every move press. A Pokemon whose original trainer isn't Red obeys at or below the obedience level (10 with no
// badge, 30 with Cascade, 50 with Rainbow, 70 with Marsh, always with Earth; UPYRTrainerState::ObedienceLevel).
// Above it, with c = the obedience level and d = the Pokemon's level:
//   1. r uniform in [0, c + d): below c it obeys.                                   P(obey)       = c / (c + d)
//   2. r uniform in [0, c + d): below c it uses one of its other moves instead.     P(other move) = d/(c+d) * c/(c+d)
//   3. otherwise, with b = d - c and r uniform in 0..255:
//      r < b        it naps (sleep for 1-7 turns: "... began to nap!"),
//      b <= r < 2b  "... won't obey!" and it hurts itself as in confusion,
//      r >= 2b      it does nothing, with one of four lines (loafing around, won't obey, turned away, ignored orders).
// Live-battle rule built on it: every press that isn't obeyed is ignored, and that move's ring runs for 1 s only, so
// the player is never locked out; a nap lasts 1.2 s per Gen 1 turn.
#pragma once

#include "CoreMinimal.h"

enum class EPYRObey : uint8
{
    Obeys,
    OtherMove,      // Used a different one of its moves.
    Naps,           // "<mon> began to nap!"
    HurtsItself,    // "<mon> won't obey!" then "It hurt itself in its confusion!"
    Loafs,          // "<mon> is loafing around."
    WontObey,       // "<mon> won't obey!"
    TurnsAway,      // "<mon> turned away!"
    IgnoresOrders,  // "<mon> ignored orders!"
};

namespace PYRObedience
{
    // Rand returns a byte (0..255), as the Game Boy's BattleRandom does. bCanUseOtherMove: it has another usable move
    // (Yellow falls through to "does nothing" when it doesn't).
    POKEMONYELLOWREIMAGINE_API EPYRObey Roll(int32 Level, int32 ObedienceLevel, TFunctionRef<int32()> Rand, bool bCanUseOtherMove);
    POKEMONYELLOWREIMAGINE_API EPYRObey Roll(int32 Level, int32 ObedienceLevel, bool bCanUseOtherMove);
    // Yellow's line for the result (empty for Obeys and OtherMove).
    POKEMONYELLOWREIMAGINE_API FString Line(EPYRObey Result, const FString& Name);
    // Nap length in seconds for 1-7 Gen 1 sleep turns.
    POKEMONYELLOWREIMAGINE_API float NapSeconds(int32 Turns);
}
