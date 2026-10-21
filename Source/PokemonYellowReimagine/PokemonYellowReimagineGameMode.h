#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PokemonYellowReimagineGameMode.generated.h"

class AYellowWorld;
class APYRPikachuFollower;

// M4: builds the Game Boy Kanto from YellowData in this one level (Kanto/YellowWorld), spawns Red and Pikachu, starts
// the overworld (Kanto/PYRField), the stages and encounters, the scripts, the music and the minimap, then shows the
// title: CONTINUE with a save, else NEW GAME (Oak's intro and naming) and Red wakes in his room (REDS_HOUSE_2F, 3,6).
// Automated runs skip the title and start a new game as RED and GARY. The four m4 screenshots (pallet-town-3d,
// start-menu, viridian-mart-buy, route1-battle-stage) are staged here from the -PYRShot name.
// Without YellowData a flat meadow stands in, so the battle tests and the Boot check still run.
UCLASS()
class POKEMONYELLOWREIMAGINE_API APokemonYellowReimagineGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    APokemonYellowReimagineGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY() TObjectPtr<AYellowWorld> World;
    UPROPERTY() TObjectPtr<APYRPikachuFollower> Pikachu;

    // A fresh game in Red's room (the title's NEW GAME after the intro, and every automated run).
    void StartNewGame();
    // A player at the keyboard: a new game behind the title (CONTINUE / NEW GAME / OPTION), Red held until the intro
    // or the loaded save lets him go. BeginPlay calls it outside automated runs; the stream test drives it by key.
    void StartTitle();
    // Red's first tile in a new game.
    static constexpr int32 StartX = 3;
    static constexpr int32 StartY = 6;

private:
    FString ShotName;
    bool bShotTyped = false;
    double ShotClock = 0;
    double ShotMoveAt = -10;
    int32 ShotStep = 0;
    void TickShot(float DeltaSeconds);
};
