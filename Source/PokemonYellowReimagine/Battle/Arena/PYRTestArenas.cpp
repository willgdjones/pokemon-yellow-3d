#include "PYRTestArenas.h"

#include "../PYRBattleFx.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace PYRArenas
{
const FVector CaveCentre(0, 84000, -15000), PoolCentre(0, 72000, -15000);
const FVector PoolWalkway(-1700, 0, 0);

const TArray<FVector>& PlatformOffsets()
{
    // 4 m platforms with 3 m of water between neighbours: P1-P2 across Y, P1-P3-P4 across X.
    static const TArray<FVector> Offsets = {FVector(-750, -500, 0), FVector(-750, 200, 0), FVector(-50, -150, 0), FVector(650, -500, 0), FVector(650, 250, 0)};
    return Offsets;
}

namespace
{
AStaticMeshActor* Block(UWorld* World, FVector Centre, FVector Size, FLinearColor Colour, FName Tag, FName Tag2 = NAME_None, const TCHAR* Shape = TEXT("Cube"))
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Centre, FRotator::ZeroRotator, Params);
    A->SetMobility(EComponentMobility::Movable);
    A->GetStaticMeshComponent()->SetStaticMesh(PYRFx::Shape(Shape));
    A->SetActorScale3D(Size / 100.f);
    PYRFx::SetColour(A->GetStaticMeshComponent(), Colour);
    A->Tags.Add(Tag);
    if (!Tag2.IsNone()) A->Tags.Add(Tag2);
    return A;
}

bool Built(UWorld* World, FName Tag)
{
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It) if (It->ActorHasTag(Tag)) return true;
    return false;
}
}

FVector EnsureCave(UWorld* World)
{
    if (!World || Built(World, TEXT("PYRTestCave"))) return CaveCentre;
    const FName T(TEXT("PYRTestCave"));
    Block(World, CaveCentre + FVector(0, 0, -50), FVector(3200, 3200, 100), PYRFx::Palette(TEXT("7a6a5a")), T);
    // The rock ceiling: its underside 4 m above the floor (thick, so traces starting under it never land on top).
    Block(World, CaveCentre + FVector(0, 0, CaveCeiling + 250), FVector(3200, 3200, 500), PYRFx::Palette(TEXT("4a3e36")), T, TEXT("Ceiling"));
    for (const FVector& P : {FVector(300, 450, 0), FVector(-450, -550, 0), FVector(650, -350, 0)})
        Block(World, CaveCentre + P + FVector(0, 0, CaveCeiling * .5f), FVector(160, 160, CaveCeiling), PYRFx::Palette(TEXT("8a7a66")), T, TEXT("PYR.Pillar"), TEXT("Cylinder"));
    return CaveCentre;
}

FVector EnsurePool(UWorld* World)
{
    if (!World || Built(World, TEXT("PYRTestPool"))) return PoolCentre;
    const FName T(TEXT("PYRTestPool"));
    const FLinearColor Tile = PYRFx::Palette(TEXT("e8f0f4")), Deep = PYRFx::Palette(TEXT("3a78b0"));
    // The basin floor 3 m down, and the walkway all round (top at Z 0).
    Block(World, PoolCentre + FVector(0, 0, -350), FVector(2600, 1800, 100), Deep, T);
    Block(World, PoolCentre + FVector(-1700, 0, -200), FVector(800, 3400, 400), Tile, T, TEXT("Walkway"));
    Block(World, PoolCentre + FVector(1700, 0, -200), FVector(800, 3400, 400), Tile, T, TEXT("Walkway"));
    Block(World, PoolCentre + FVector(0, -1300, -200), FVector(2600, 800, 400), Tile, T, TEXT("Walkway"));
    Block(World, PoolCentre + FVector(0, 1300, -200), FVector(2600, 800, 400), Tile, T, TEXT("Walkway"));
    // The water surface: a plain blocking slab, top at PoolSurface.
    Block(World, PoolCentre + FVector(0, 0, PoolSurface - 10), FVector(2600, 1800, 20), PYRFx::Palette(TEXT("6ec6f0")), T, TEXT("PYR.Water"));
    for (const FVector& P : PlatformOffsets())
        Block(World, PoolCentre + P + FVector(0, 0, PlatformTop - 30), FVector(400, 400, 60), PYRFx::Palette(TEXT("c9a24a")), T, TEXT("PYR.Platform"));
    return PoolCentre;
}
}
