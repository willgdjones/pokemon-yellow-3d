#include "Menus/PYRIntroStage.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRPeople.h"
#include "World/PYRKit.h"
#include "World/PYRPaint.h"

using namespace PYR;

namespace
{
// Far west of Kanto, where no map is built: the stage stands alone under the world's sun and painted sky.
const FVector StageAt(-400000.f, -400000.f, 0.f);

float Ease(float T)
{
    T = FMath::Clamp(T, 0.f, 1.f);
    return T * T * (3.f - 2.f * T);
}
}

AYellowIntroStage::AYellowIntroStage()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Movable);
    Tags.Add(TEXT("IntroStage"));
}

AYellowIntroStage* AYellowIntroStage::Find(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<AYellowIntroStage> It(World); It; ++It) return *It;
    return nullptr;
}

AYellowIntroStage* AYellowIntroStage::Open(UWorld* World)
{
    if (!World) return nullptr;
    if (AYellowIntroStage* Old = Find(World)) Old->Close();
    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AYellowIntroStage* S = World->SpawnActor<AYellowIntroStage>(StageAt, FRotator::ZeroRotator, P);
    if (!S) return nullptr;
    S->Build();
    S->Camera = World->SpawnActor<ACameraActor>(StageAt + FVector(1500, 400, 520), FRotator(-12, 190, 0), P);
    if (S->Camera)
    {
        S->Camera->GetCameraComponent()->SetFieldOfView(55.f);
        S->Camera->GetCameraComponent()->bConstrainAspectRatio = false;
    }
    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        S->OldView = PC->GetViewTarget();
        PC->SetViewTarget(S->Camera);
    }
    UE_LOG(LogTemp, Display, TEXT("YELLOW_INTRO_STAGE open (%d models)"), S->Loaded);
    return S;
}

void AYellowIntroStage::Build()
{
    // The stage: a round grass platform with an earth rim and a few flowers, round anime trees and bushes behind it.
    const FLinearColor Grass = PYRColor::Hex(0x86cf5e), Earth = PYRColor::Hex(0xb88a5a), Leaf = PYRColor::Hex(0x3f9a4a),
        LeafLight = PYRColor::Hex(0x55b050), Trunk = PYRColor::Hex(0x8a5a3a);
    // m4v: painted with the anime kit (World/PYRKit).
    PYRKit::Apply(Shape(this, nullptr, EShape::Cylinder, FVector(0, 0, -40), FVector(1500, 1500, 80), Earth), EPYRKit::Ledge, Earth);
    PYRKit::Apply(Shape(this, nullptr, EShape::Cylinder, FVector(0, 0, -2), FVector(1440, 1440, 6), Grass), EPYRKit::GrassShort, Grass);
    static const FLinearColor Petals[] = {PYRColor::Hex(0xe3342f), PYRColor::Hex(0xfcfcf6), PYRColor::Hex(0xffd733)};
    FRandomStream Rng(0x0a4b);
    for (int32 I = 0; I < 40; ++I)
    {
        const float A = Rng.FRandRange(0, 2 * PI), R = Rng.FRandRange(350, 690);
        PYRKit::Apply(Shape(this, nullptr, EShape::Sphere, FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 6), FVector(16, 16, 10), Petals[I % 3]), EPYRKit::Flowers, Petals[I % 3]);
    }
    // Trees round the back of the stage (the camera looks toward -X).
    for (int32 I = 0; I < 14; ++I)
    {
        const float Y = -1500.f + I * 230.f + Rng.FRandRange(-60, 60);
        const float X = -1050.f - Rng.FRandRange(0, 500) - FMath::Abs(Y) * .15f;
        const float S = Rng.FRandRange(.9f, 1.4f);
        PYRKit::Apply(Shape(this, nullptr, EShape::Cylinder, FVector(X, Y, 60 * S), FVector(40, 40, 120) * S, Trunk), EPYRKit::TreeTrunk, Trunk);
        PYRKit::Apply(Shape(this, nullptr, EShape::Sphere, FVector(X, Y, 230 * S), FVector(220, 220, 220) * S, I % 2 ? Leaf : LeafLight), EPYRKit::TreeCanopy, I % 2 ? Leaf : LeafLight);
        PYRKit::Apply(Shape(this, nullptr, EShape::Sphere, FVector(X + 20, Y - 30, 330 * S), FVector(140, 140, 130) * S, I % 2 ? LeafLight : Leaf), EPYRKit::TreeCanopy, I % 2 ? LeafLight : Leaf);
    }
    // A far meadow under it all, so the horizon is green hills and sky.
    PYRKit::Apply(Shape(this, nullptr, EShape::Plane, FVector(0, 0, -60), FVector(400000, 400000, 1), Grass), EPYRKit::GrassShort, Grass);

    AddPerson(TEXT("oak"), TEXT("professor_oak"), 172.f, false, FVector(0, 0, 0));
    AddPerson(TEXT("pikachu"), TEXT("pikachu"), 48.f, true, FVector(70, -120, 0));
    AddPerson(TEXT("red"), TEXT("red"), 150.f, false, FVector(0, 0, 0));
    // Red's standing clip carries a quarter turn in its root: turned back so he faces the camera.
    Slots[TEXT("red")].YawOffset = -90.f;
    AddPerson(TEXT("rival"), TEXT("gary"), 155.f, false, FVector(0, 0, 0));
    Slots[TEXT("rival")].YawOffset = 120.f;   // Gary's model is built facing the other way: turned round to the camera
}

void AYellowIntroStage::AddPerson(FName Who, const TCHAR* Model, float Height, bool bPokemon, const FVector& Home)
{
    FSlot Slot;
    Slot.Root = Group(this, nullptr, Home, FRotator(0, bPokemon ? 25.f : 0.f, 0));
    Slot.Home = Home;
    Slot.Height = Height;
    if (PYRSprites::SpawnModel(this, Slot.Root, Model, Height, bPokemon)) ++Loaded;
    Slot.Root->SetVisibility(false, true);
    Slots.Add(Who, Slot);
    Order.Add(Who);
}

void AYellowIntroStage::Frame(const FVector& Eye, const FVector& Look, float Seconds)
{
    CamFrom = Camera ? Camera->GetActorLocation() - StageAt : Eye;
    LookFrom = Camera ? CamFrom + Camera->GetActorForwardVector() * 400.f : Look;
    CamTo = Eye;
    LookTo = Look;
    CamSeconds = FMath::Max(.05f, Seconds);
}

void AYellowIntroStage::Beat(FName InBeat)
{
    CurrentBeat = InBeat;
    BeatClock = 0;
    auto Want = [this](std::initializer_list<const TCHAR*> Who)
    {
        for (auto& Pair : Slots) Pair.Value.bWanted = false;
        for (const TCHAR* W : Who) if (FSlot* S = Slots.Find(W)) S->bWanted = true;
    };
    // Red and the rival stand left of centre while their name is asked (the naming box is on the right).
    // The designer's opening: Oak greets Red with Pikachu at his feet (the Game Boy brings Pikachu in on the next page).
    if (InBeat == TEXT("oak")) { Want({TEXT("oak"), TEXT("pikachu")}); Frame(FVector(560, -60, 190), FVector(0, -55, 62), 2.4f); }
    else if (InBeat == TEXT("pikachu")) { Want({TEXT("oak"), TEXT("pikachu")}); Frame(FVector(520, -150, 120), FVector(20, -60, 80), 1.4f); }
    else if (InBeat == TEXT("red")) { Want({TEXT("red")}); Frame(FVector(420, 40, 125), FVector(0, -110, 95), 1.2f); }
    else if (InBeat == TEXT("rival")) { Want({TEXT("rival")}); Frame(FVector(480, -30, 135), FVector(0, -120, 100), 1.2f); }
    else if (InBeat == TEXT("red-again")) { Want({TEXT("red")}); Frame(FVector(380, 0, 130), FVector(0, 0, 100), 1.2f); }
    else if (InBeat == TEXT("shrink")) { Want({TEXT("red")}); Frame(FVector(230, 0, 95), FVector(0, 0, 55), ShrinkSeconds); }
    UE_LOG(LogTemp, Display, TEXT("YELLOW_INTRO_BEAT %s"), *InBeat.ToString());
}

TArray<FName> AYellowIntroStage::Shown() const
{
    TArray<FName> Out;
    for (const FName& Who : Order) if (IsShown(Who)) Out.Add(Who);
    return Out;
}

bool AYellowIntroStage::IsShown(FName Who) const
{
    const FSlot* S = Slots.Find(Who);
    return S && S->Root && S->Show > .5f;
}

void AYellowIntroStage::Close()
{
    UWorld* World = GetWorld();
    if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
    {
        AActor* Back = OldView && OldView != Camera ? OldView.Get() : PC->GetPawn();
        if (Back) PC->SetViewTarget(Back);
    }
    if (Camera) Camera->Destroy();
    Camera = nullptr;
    Destroy();
}

void AYellowIntroStage::Tick(float Dt)
{
    Super::Tick(Dt);
    BeatClock += Dt;
    // People slide in from the side and pop up to size (the Game Boy's pictures slide in), or shrink away.
    for (auto& Pair : Slots)
    {
        FSlot& S = Pair.Value;
        if (!S.Root) continue;
        const float Target = S.bWanted ? 1.f : 0.f;
        S.Show = FMath::FInterpConstantTo(S.Show, Target, Dt, S.bWanted ? 2.2f : 3.5f);
        const float E = Ease(S.Show);
        float Scale = FMath::Max(.01f, E);
        if (Pair.Key == TEXT("red") && CurrentBeat == TEXT("shrink"))
            Scale = FMath::Max(.01f, 1.f - Ease(float(BeatClock) / ShrinkSeconds));
        S.Root->SetRelativeLocation(S.Home + FVector(0, (1.f - E) * 260.f, 0));
        // People turn to the camera (Pikachu stays turned toward Oak).
        if (Camera && Pair.Key != TEXT("pikachu"))
        {
            const FVector To = Camera->GetActorLocation() - S.Root->GetComponentLocation();
            S.Root->SetWorldRotation(FRotator(0, To.Rotation().Yaw + S.YawOffset, 0));
        }
        S.Root->SetRelativeScale3D(FVector(Scale));
        const bool bVisible = S.Show > .02f && Scale > .02f;
        if (S.Root->IsVisible() != bVisible) S.Root->SetVisibility(bVisible, true);
    }
    if (Camera)
    {
        const float T = Ease(float(BeatClock) / CamSeconds);
        const FVector Eye = FMath::Lerp(CamFrom, CamTo, T);
        const FVector Look = FMath::Lerp(LookFrom, LookTo, T);
        // A slow drift keeps the shot alive between beats.
        const FVector Drift(0, FMath::Sin(float(BeatClock) * .35f) * 12.f, FMath::Sin(float(BeatClock) * .5f) * 4.f);
        Camera->SetActorLocationAndRotation(StageAt + Eye + Drift, (Look - Eye - Drift).Rotation());
    }
}
