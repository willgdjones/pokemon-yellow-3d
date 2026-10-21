#include "World/PYRKit.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#include "World/PYRPaint.h"

namespace PYRKit
{
namespace
{
const TCHAR* const Names[] = {TEXT("WallWood"), TEXT("WallPlaster"), TEXT("WallStone"), TEXT("WallBrick"), TEXT("FloorWood"), TEXT("FloorTile"),
    TEXT("FloorStone"), TEXT("RoofRed"), TEXT("RoofBlue"), TEXT("RoofGrey"), TEXT("RoofBrown"), TEXT("PathDirt"), TEXT("PathPaving"), TEXT("GrassShort"),
    TEXT("GrassTall"), TEXT("Flowers"), TEXT("RockGrey"), TEXT("RockCave"), TEXT("CaveFloor"), TEXT("Water"), TEXT("Sand"), TEXT("TreeCanopy"),
    TEXT("TreeTrunk"), TEXT("Fence"), TEXT("Ledge"), TEXT("Sign"), TEXT("Counter"), TEXT("Carpet"), TEXT("Paint"), TEXT("Glass"), TEXT("Metal"),
    TEXT("Ink")};
static_assert(UE_ARRAY_COUNT(Names) == int32(EPYRKit::Count), "a name per kit surface");

struct FLoaded
{
    TStrongObjectPtr<UMaterialInterface> Material;
    FLinearColor Average = FLinearColor(.5f, .5f, .5f, 1.f);
    bool bTried = false;
};

FLoaded& Loaded(EPYRKit S)
{
    static FLoaded All[int32(EPYRKit::Count)];
    FLoaded& L = All[FMath::Clamp(int32(S), 0, int32(EPYRKit::Count) - 1)];
    if (!L.bTried)
    {
        L.bTried = true;
        L.Material.Reset(Cast<UMaterialInterface>(PYR::LoadOptional(UMaterialInterface::StaticClass(), Path(S))));
        FLinearColor Avg;
        if (L.Material.IsValid() && L.Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("PaintAverage")), Avg)) L.Average = Avg;
    }
    return L;
}

// The nearest material asset up a dynamic instance's parents (MI_Kit_<Surface> for a tinted surface).
const UMaterialInterface* AssetOf(const UMaterialInterface* M)
{
    while (const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(M)) M = Mid->Parent;
    return M;
}
}

const TCHAR* Name(EPYRKit Surface)
{
    return Names[FMath::Clamp(int32(Surface), 0, int32(EPYRKit::Count) - 1)];
}

bool FromName(const FString& InName, EPYRKit& Out)
{
    for (int32 I = 0; I < int32(EPYRKit::Count); ++I)
        if (InName.Equals(Names[I], ESearchCase::IgnoreCase)) { Out = EPYRKit(I); return true; }
    return false;
}

FString Path(EPYRKit Surface)
{
    return FString::Printf(TEXT("/Game/Look/Kit/MI_Kit_%s"), Name(Surface));
}

UMaterialInterface* Base(EPYRKit Surface)
{
    return Loaded(Surface).Material.Get();
}

float PaintTiling(EPYRKit Surface)
{
    float Tiling = 300.f;
    if (UMaterialInterface* M = Loaded(Surface).Material.Get()) M->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Tiling")), Tiling);
    return Tiling;
}

FLinearColor PaintAverage(EPYRKit Surface)
{
    return Loaded(Surface).Average;
}

UMaterialInterface* Get(const UObject* Ctx, EPYRKit Surface, const FLinearColor& Colour, float TilingCm)
{
    FLoaded& L = Loaded(Surface);
    if (!L.Material.IsValid()) return nullptr;
    const bool bTint = Colour.A > 0.f;
    if (!bTint && TilingCm <= 0.f) return L.Material.Get();
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    UPYRPaint* Cache = World ? World->GetSubsystem<UPYRPaint>() : nullptr;
    if (!Cache) return L.Material.Get();
    const FString Key = FString::Printf(TEXT("%s|%08x|%d|%.0f"), Name(Surface), Colour.ToFColor(false).ToPackedARGB(), int32(bTint), TilingCm);
    if (TObjectPtr<UMaterialInstanceDynamic>* Found = Cache->KitCache.Find(Key)) return *Found;
    UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(L.Material.Get(), Cache);
    if (bTint)
    {
        // The paint's average becomes Colour: each channel scaled by Colour / average.
        auto Ratio = [](float Want, float Avg) { return FMath::Clamp(Want / FMath::Max(Avg, .004f), 0.f, 6.f); };
        Mid->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Ratio(Colour.R, L.Average.R), Ratio(Colour.G, L.Average.G), Ratio(Colour.B, L.Average.B), 1.f));
    }
    if (TilingCm > 0.f) Mid->SetScalarParameterValue(TEXT("Tiling"), TilingCm);
    Cache->KitCache.Add(Key, Mid);
    return Mid;
}

bool Apply(UPrimitiveComponent* C, EPYRKit Surface, const FLinearColor& Colour, float TilingCm)
{
    UMaterialInterface* M = C ? Get(C, Surface, Colour, TilingCm) : nullptr;
    if (!M) return false;
    for (int32 I = 0; I < FMath::Max(1, C->GetNumMaterials()); ++I) C->SetMaterial(I, M);
    return true;
}

bool ApplyProp(UStaticMeshComponent* C)
{
    const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
    if (!Mesh) return false;
    const FString MeshPath = Mesh->GetPathName();
    if (!MeshPath.StartsWith(TEXT("/Game/Props/"))) return false;
    FString Id = Mesh->GetName();
    Id.RemoveFromStart(TEXT("SM_"));
    UMaterialInterface* M = Cast<UMaterialInterface>(PYR::LoadOptional(UMaterialInterface::StaticClass(), TEXT("/Game/Look/Kit/Props/MI_KitProp_") + Id));
    if (!M) return false;
    for (int32 I = 0; I < FMath::Max(1, C->GetNumMaterials()); ++I) C->SetMaterial(I, M);
    return true;
}

const UMaterialInterface* RootAsset(const UMaterialInterface* M)
{
    while (const UMaterialInstance* Mi = Cast<UMaterialInstance>(M))
    {
        if (!Mi->Parent) break;
        M = Mi->Parent;
    }
    return M;
}

bool IsKit(const UMaterialInterface* M)
{
    const UMaterialInterface* A = M ? AssetOf(M) : nullptr;
    return A && A->GetPathName().StartsWith(TEXT("/Game/Look/Kit/"));
}

bool SurfaceOf(const UMaterialInterface* M, EPYRKit& Out)
{
    const UMaterialInterface* A = M ? AssetOf(M) : nullptr;
    if (!A) return false;
    FString N = A->GetName();
    return N.RemoveFromStart(TEXT("MI_Kit_")) && FromName(N, Out);
}
}
