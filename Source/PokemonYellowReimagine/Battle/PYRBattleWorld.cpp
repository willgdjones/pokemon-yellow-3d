#include "PYRBattleWorld.h"

#include "PYRBattleFx.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "../Party/PYRBadgeCase.h"

// Far south-west of every map, the stages and the m3 test arenas.
const FVector UPYRBattleWorld::PadCentre(-60000, 60000, 0);

FVector UPYRBattleWorld::EnsureTestPad(UWorld* World)
{
    if (!World) return PadCentre;
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It) if (It->ActorHasTag(TEXT("PYRTestPad"))) return PadCentre;
    auto Block = [World](FVector Centre, FVector Size, FLinearColor Colour, FName Tag)
    {
        AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Centre, FRotator::ZeroRotator);
        A->SetMobility(EComponentMobility::Movable);
        A->GetStaticMeshComponent()->SetStaticMesh(PYRFx::Shape(TEXT("Cube")));
        A->SetActorScale3D(Size / 100.f);
        PYRFx::SetColour(A->GetStaticMeshComponent(), Colour);
        A->Tags.Add(TEXT("PYRTestPad"));
        A->Tags.Add(Tag);
        return A;
    };
    // A flat 40 m meadow pad, top at Z = 0, and a 100 cm ledge box on its +X side (a step up at X = 400).
    Block(PadCentre + FVector(0, 0, -50), FVector(4000, 4000, 100), PYRFx::Palette(TEXT("7cc85a")), TEXT("PadFloor"));
    Block(PadCentre + FVector(1200, 0, 50), FVector(1600, 3000, 100), PYRFx::Palette(TEXT("5cb85c")), TEXT("Ledge"));
    Block(PadCentre + FVector(405, 0, 96), FVector(20, 3000, 10), PYRFx::Palette(TEXT("c9a24a")), TEXT("LedgeLip"));
    return PadCentre;
}

void UPYRBattleWorld::Deinitialize()
{
    UPYRBadgeCase::Close();
    Super::Deinitialize();
}

void UPYRBattleWorld::Tick(float Dt)
{
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld()) return;
    APlayerController* PC = World->GetFirstPlayerController();
    // B: the badge case, anywhere (in battle too: the party wheel shows the hint). The open case closes itself on B.
    if (PC && PC->WasInputKeyJustPressed(EKeys::B) && !UPYRBadgeCase::IsOpen()) UPYRBadgeCase::Open(PC);
}
