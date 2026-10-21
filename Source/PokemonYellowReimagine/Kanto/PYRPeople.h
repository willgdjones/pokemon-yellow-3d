// M4: the map objects from YellowData, each on its Game Boy tile.
// - APYRPerson: every NPC. Its Game Boy sprite picks a model from one table (PYRSprites, with a fallback); `walk`
//   people wander one tile at a time near home, `stay` people face their facing. People block Red, turn to him when he
//   talks, and scripts walk them tile by tile (trainers walking up, Oak leading Red, the Pewter boy).
// - APYRSignpost: a sign (a post, or a plate on a wall) that reads its text.
// - APYRItemBall: a Poke Ball on the ground that gives its item once and is gone for good.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Yellow/YellowData.h"
#include "PYRPeople.generated.h"

class AYellowWorld;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class UBoxComponent;

// A Game Boy sprite's model: /Game/Characters/<Model>/SK_ or SM_, or /Game/Pokemon/<Model>/ for Pokemon sprites.
struct FPYRSpriteModel
{
    FString Model;
    float HeightCm = 160.f;
    bool bPokemon = false;
    bool bProp = false;      // an object on a table (the Pokedex, a fossil, a ball)
    bool bLying = false;     // the Viridian old man before his coffee
};

namespace PYRSprites
{
    // The model for a sprite in a map (map-specific people first: Brock, Misty, Bill, the old man...).
    POKEMONYELLOWREIMAGINE_API FPYRSpriteModel For(FName Sprite, FName Map);
    // Every sprite the table knows (tests).
    POKEMONYELLOWREIMAGINE_API void Known(TArray<FName>& Out);
    // A model (/Game/Characters/<Model> or /Game/Pokemon/<Model>) standing under Parent at HeightCm, idling; null
    // when the model isn't there (no stand-in is made).
    POKEMONYELLOWREIMAGINE_API USceneComponent* SpawnModel(AActor* Owner, USceneComponent* Parent, const FString& Model, float HeightCm, bool bPokemon);
}

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRPerson : public AActor
{
    GENERATED_BODY()
public:
    APYRPerson();
    void Init(AYellowWorld* InWorld, const FYMap& Map, const FYNpc& Npc);
    virtual void Tick(float DeltaSeconds) override;

    FName Map;
    FString Id;
    int32 Index = 0;
    FName Sprite;
    FPYRSpriteModel Model;
    FIntPoint Home = FIntPoint::ZeroValue;   // Kanto tiles
    FIntPoint Tile = FIntPoint::ZeroValue;
    EYDir Facing = EYDir::Down;
    EYDir HomeFacing = EYDir::Down;
    bool bWalker = false;
    int32 WalkAxis = 0;       // 1 up_down, 2 left_right (walkers)
    FString TextId;
    FString Script;           // script_pokecenter_nurse, script_mart...
    FYTrainerRef Trainer;
    FName GiveItem;
    TSharedPtr<FJsonObject> Raw;

    bool IsTrainer() const { return Trainer.bValid; }
    bool IsBeaten() const;
    FString TrainerKey() const;
    // The trainer's Game Boy class name ("BUG CATCHER"), empty for townsfolk.
    FString ClassName() const;
    // m4u: the name the text box shows over his own lines (the class for a trainer, else what he is: OLD MAN, NURSE).
    FString SpeakerName() const;
    TArray<FString> Texts;    // every text.json id he says

    // ---- Movement (tile by tile at the Game Boy's walking pace)
    static constexpr float WalkSpeed = 375.f;
    // One tile in Dir; false when the tile is blocked (bForce ignores people, not walls).
    bool Step(EYDir Dir, bool bForce = false);
    // Several steps in order; OnDone when the last lands (or a step is blocked for good).
    void Walk(const TArray<EYDir>& Steps, TFunction<void()> OnDone = nullptr);
    bool IsMoving() const { return bStepping || Steps.Num() > 0; }
    void Face(EYDir Dir);
    void FaceToward(const FVector& Point);
    void Teleport(FIntPoint Kanto, EYDir Face);
    // Scripts stop the wandering (and bring it back).
    bool bFrozen = false;
    // Hidden people (gone after a story beat, or not there yet) neither block nor talk.
    void SetShown(bool bShow);
    bool IsShown() const { return bShown; }
    // Not drawn while Red is in another part of the world (AYellowWorld::SetFocus); the game logic still sees him.
    void SetCulled(bool bCull);
    // The '!' over a trainer's head.
    void Exclaim(float Seconds);
    bool IsExclaiming() const;

    UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> Capsule;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Visual;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Rig;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Bang;

private:
    TWeakObjectPtr<AYellowWorld> World;
    bool bStepping = false, bShown = true;
    FVector StepFrom = FVector::ZeroVector, StepTo = FVector::ZeroVector;
    float StepT = 0.f;
    TArray<EYDir> Steps;
    TFunction<void()> WalkDone;
    double NextWander = 0, ExclaimUntil = 0;
    FRandomStream Rng;
    FName Clip;
    UPROPERTY() TMap<FName, TObjectPtr<UAnimSequence>> Clips;
    void BuildBody();
    void PlayClip(FName Want);
    void Wander();
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRSignpost : public AActor
{
    GENERATED_BODY()
public:
    APYRSignpost();
    void Init(AYellowWorld* InWorld, const FYMap& Map, const FYSign& Sign);
    FName Map;
    FIntPoint Tile = FIntPoint::ZeroValue;   // Kanto
    FString TextId;
    bool bOnWall = false;
    TArray<FString> Label;   // m4u: the lines lettered on its face (PYRLetters::SignLabel)
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRItemBall : public AActor
{
    GENERATED_BODY()
public:
    APYRItemBall();
    void Init(AYellowWorld* InWorld, const FYMap& Map, const FYItemBall& Ball);
    FName Map;
    FIntPoint Local = FIntPoint::ZeroValue;
    FIntPoint Tile = FIntPoint::ZeroValue;   // Kanto
    FName Item;
    FString Key() const;
    UPROPERTY() TObjectPtr<UBoxComponent> Block;
};
