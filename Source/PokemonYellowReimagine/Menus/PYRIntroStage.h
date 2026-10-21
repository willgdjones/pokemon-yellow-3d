// m4q: Oak's opening as an anime 3D scene, beat for beat as Yellow's (engine/movie/oak_speech/oak_speech.asm):
// Oak on a painted-sky stage ("Hello there! Welcome to the world of POKéMON!"), Pikachu beside him ("This world is
// inhabited by creatures called POKéMON!"), Red shown and named, the rival shown and named, Red again ("Your very own
// POKéMON legend is about to unfold!"), then Red shrinks away into his room. The rigged Oak, Pikachu, Red and Gary stand
// on a round grass stage far from Kanto, lit by the world's sun under its painted sky, and a camera moves between shots
// (a white flash between people, as the Game Boy fades to white). PYRScreens::Intro drives the beats with Yellow's text.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PYRIntroStage.generated.h"

class ACameraActor;

UCLASS()
class POKEMONYELLOWREIMAGINE_API AYellowIntroStage : public AActor
{
    GENERATED_BODY()
public:
    AYellowIntroStage();
    // The stage for this world (spawned once, far from every map); the view moves to its camera.
    static AYellowIntroStage* Open(UWorld* World);
    static AYellowIntroStage* Find(const UObject* Ctx);

    // oak, pikachu, red, rival, red-again, shrink: who stands on the stage and where the camera goes.
    void Beat(FName Beat);
    FName GetBeat() const { return CurrentBeat; }
    // Everyone shown now (oak, pikachu, red, rival), in stage order.
    TArray<FName> Shown() const;
    bool IsShown(FName Who) const;
    // True once Red has shrunk away (the shrink beat is over).
    bool IsShrunk() const { return CurrentBeat == TEXT("shrink") && BeatClock > ShrinkSeconds; }
    // Back to the player's view and gone.
    void Close();
    ACameraActor* GetCamera() const { return Camera; }
    // The rigged models that loaded (tests: no capsules).
    int32 ModelsLoaded() const { return Loaded; }
    virtual void Tick(float DeltaSeconds) override;

    static constexpr float ShrinkSeconds = 1.4f;

private:
    struct FSlot
    {
        USceneComponent* Root = nullptr;
        FVector Home = FVector::ZeroVector;   // local, on the stage
        float Show = 0.f;                     // 0 hidden .. 1 in place
        bool bWanted = false;
        float Height = 160.f;
        float YawOffset = 0.f;                // a clip that turns its root
    };
    TMap<FName, FSlot> Slots;
    TArray<FName> Order;
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
    UPROPERTY() TObjectPtr<AActor> OldView;
    FName CurrentBeat;
    double BeatClock = 0;
    FVector CamFrom = FVector::ZeroVector, CamTo = FVector::ZeroVector, LookFrom = FVector::ZeroVector, LookTo = FVector::ZeroVector;
    float CamSeconds = 1.2f;
    int32 Loaded = 0;
    void Build();
    void AddPerson(FName Who, const TCHAR* Model, float Height, bool bPokemon, const FVector& Home);
    void Frame(const FVector& Eye, const FVector& Look, float Seconds);
};
