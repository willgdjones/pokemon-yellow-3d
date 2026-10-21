// Test helper that plays a battle through the same Press* handlers a player uses: steers, fires moves when ready,
// dodges when a tell is about to land, swaps when its body or type can't do the job, and drinks Potions.
#pragma once

#include "CoreMinimal.h"

class APYRBattleManager;
class APYRMonPawn;

// Aggressive: fight with whoever is out. NoElectric: never lead with Electric moves (Brock).
enum class EPYRBotPlan : uint8 { Aggressive, NoElectric };

struct POKEMONYELLOWREIMAGINE_API FPYRBattleBot
{
    void Step(APYRBattleManager* Manager, EPYRBotPlan Plan, float Dt);

    int32 Dodges = 0, Moves = 0, Swaps = 0, Potions = 0;
    bool bUsePotions = true;
    bool bAllowSwaps = true;

private:
    int32 PickSwap(APYRBattleManager* Manager, EPYRBotPlan Plan) const;
    bool Useless(APYRBattleManager* Manager, APYRMonPawn* Mine, EPYRBotPlan Plan) const;
    float MoveClock = 0;
};
