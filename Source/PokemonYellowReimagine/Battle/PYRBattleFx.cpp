#include "PYRBattleFx.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"

namespace PYRFx
{
UStaticMesh* Shape(const TCHAR* Name)
{
    static TMap<FString, TObjectPtr<UStaticMesh>> Cache;
    if (TObjectPtr<UStaticMesh>* Found = Cache.Find(Name)) return *Found;
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
    if (Mesh) Mesh->AddToRoot();
    Cache.Add(Name, Mesh);
    return Mesh;
}

static UMaterialInterface* LoadBase(bool bFlat)
{
    static UMaterialInterface* Lit = nullptr;
    static UMaterialInterface* Flat = nullptr;
    static bool bFlatTried = false;
    if (bFlat && !bFlatTried)
    {
        bFlatTried = true;
        if (FPackageName::DoesPackageExist(TEXT("/Game/BattleFx/M_FxFlat")))
            Flat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BattleFx/M_FxFlat.M_FxFlat"));
        if (Flat) Flat->AddToRoot();
    }
    if (bFlat && Flat) return Flat;
    if (!Lit)
    {
        Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (Lit) Lit->AddToRoot();
    }
    return Lit;
}

static void MakeMaterial(UStaticMeshComponent* Mesh, bool bFlat)
{
    if (UMaterialInterface* Base = LoadBase(bFlat)) Mesh->SetMaterial(0, UMaterialInstanceDynamic::Create(Base, Mesh));
}

void SetColour(UStaticMeshComponent* Mesh, FLinearColor Colour)
{
    if (!Mesh) return;
    UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    if (!Mid) { MakeMaterial(Mesh, false); Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)); }
    if (Mid) Mid->SetVectorParameterValue(TEXT("Color"), Colour);
}

UStaticMeshComponent* AddPrimitive(AActor* Owner, USceneComponent* Parent, const TCHAR* ShapeName, FVector Loc, FVector Scale, FLinearColor Colour, FRotator Rot, bool bFlat)
{
    if (!Owner) return nullptr;
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
    Mesh->SetStaticMesh(Shape(ShapeName));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->CastShadow = !bFlat;
    USceneComponent* AttachTo = Parent ? Parent : Owner->GetRootComponent();
    if (AttachTo) Mesh->SetupAttachment(AttachTo);
    else Owner->SetRootComponent(Mesh);
    Mesh->SetRelativeLocationAndRotation(Loc, Rot);
    Mesh->SetRelativeScale3D(Scale);
    Mesh->RegisterComponent();
    Owner->AddInstanceComponent(Mesh);
    MakeMaterial(Mesh, bFlat);
    SetColour(Mesh, Colour);
    return Mesh;
}

FLinearColor Palette(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }
}

// ------------------------------------------------------------------------------------------------ move effects
namespace
{
const FLinearColor BoltYellow(1.f, .86f, .1f), CoreWhite(1, 1, 1), Ink(.05f, .05f, .08f);
}

APYRMoveFx::APYRMoveFx()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Tags.Add(TEXT("BattleFx"));
}

APYRMoveFx* APYRMoveFx::Spawn(UWorld* World, EPYRFx Kind, FVector At, FVector InDir, FLinearColor Colour, float Size, float Life)
{
    if (!World) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    InDir = InDir.IsNearlyZero() ? FVector::ForwardVector : InDir.GetSafeNormal();
    APYRMoveFx* Fx = World->SpawnActor<APYRMoveFx>(At, FRotator::ZeroRotator, Params);
    if (!Fx) return nullptr;
    Fx->Kind = Kind;
    Fx->Dir = InDir;
    Fx->Colour = Colour;
    Fx->Size = FMath::Max(10.f, Size);
    Fx->Life = FMath::Max(.05f, Life);
    Fx->From = At;
    Fx->To = At + InDir * Size;
    // Flat effects face along Dir (bolts and rings place their own pieces).
    Fx->SetActorRotation(FRotator(0, InDir.Rotation().Yaw, 0));
    Fx->Build();
    Fx->SetLifeSpan(Fx->Life + .05f);
    return Fx;
}

UStaticMeshComponent* APYRMoveFx::Piece(const TCHAR* ShapeName, FVector Loc, FVector Scale, FLinearColor C, FRotator Rot)
{
    UStaticMeshComponent* P = PYRFx::AddPrimitive(this, nullptr, ShapeName, Loc, Scale, C, Rot, true);
    P->CastShadow = false;
    Pieces.Add(P);
    BaseLoc.Add(Loc);
    BaseScale.Add(Scale);
    Vel.Add(FVector::ZeroVector);
    return P;
}

int32 APYRMoveFx::VisiblePieces() const
{
    int32 N = 0;
    for (const UStaticMeshComponent* P : Pieces) if (P && P->IsVisible() && P->GetStaticMesh() && !P->GetComponentScale().IsNearlyZero()) ++N;
    return N;
}

void APYRMoveFx::SetEnds(FVector InFrom, FVector InTo)
{
    From = InFrom;
    To = InTo;
    if (Kind == EPYRFx::Bolt) BuildBolts();
}

void APYRMoveFx::BuildBolts()
{
    // Three jagged bolts, each a chain of thick yellow segments with a thinner white core, re-jagged every
    // 0.05 s. Pieces are reused: 3 bolts x 6 segments x (yellow + white).
    const int32 Bolts = 3, Segs = 6;
    const bool bFirst = Pieces.Num() == 0;
    const FVector Span = To - From;
    const float Len = FMath::Max(1.f, float(Span.Size()));
    const FVector Fwd = Span / Len;
    const FVector Side = FVector::CrossProduct(Fwd, FVector::UpVector).GetSafeNormal();
    const FVector Up = FVector::CrossProduct(Side, Fwd);
    int32 Index = 0;
    for (int32 B = 0; B < Bolts; ++B)
    {
        FVector Prev = From;
        for (int32 S = 1; S <= Segs; ++S)
        {
            const float T = S / float(Segs);
            const float Jag = S == Segs ? 0.f : FMath::FRandRange(-1.f, 1.f) * FMath::Min(60.f, Len * .12f);
            const FVector Next = From + Fwd * Len * T + Side * Jag + Up * FMath::FRandRange(-1.f, 1.f) * FMath::Min(30.f, Len * .06f) + Up * (B - 1) * 12.f;
            const FVector Mid = (Prev + Next) * .5f;
            const FRotator Rot = (Next - Prev).Rotation();
            const float SegLen = float((Next - Prev).Size()) / 100.f;
            for (int32 Layer = 0; Layer < 2; ++Layer)
            {
                const FVector Scale(SegLen * 1.05f, Layer == 0 ? .16f : .06f, Layer == 0 ? .16f : .06f);
                UStaticMeshComponent* P = bFirst ? Piece(TEXT("Cube"), FVector::ZeroVector, Scale, Layer == 0 ? BoltYellow : CoreWhite) : Pieces[Index].Get();
                if (bFirst) { P->SetUsingAbsoluteLocation(true); P->SetUsingAbsoluteRotation(true); P->SetUsingAbsoluteScale(true); }
                P->SetWorldLocationAndRotation(Mid + (Layer == 1 ? -Side * 2.f : FVector::ZeroVector), Rot);
                P->SetWorldScale3D(Scale);
                if (P->GetNumMaterials() > 0 && Layer == 1) P->SetTranslucentSortPriority(5);
                ++Index;
            }
            Prev = Next;
        }
    }
}

void APYRMoveFx::Build()
{
    const float S = Size;
    const FLinearColor C = Colour;
    switch (Kind)
    {
    case EPYRFx::Bolt:
        BuildBolts();
        break;
    case EPYRFx::CheekSpark:
        for (int32 i = 0; i < 8; ++i)
        {
            const FVector D = FVector(0, FMath::Cos(i * PI / 4), FMath::Sin(i * PI / 4));
            Piece(TEXT("Cube"), D * 8.f, FVector(.05f, .28f, .05f), i % 2 ? CoreWhite : BoltYellow, D.Rotation() + FRotator(0, 0, 0));
            Vel.Last() = D * 260.f;
        }
        break;
    case EPYRFx::SpeedLines:
        // Local X is Dir: lines lie along it, spread around the body, sliding back.
        for (int32 i = 0; i < 9; ++i)
        {
            const FVector Off(FMath::FRandRange(-.2f, .4f) * S, FMath::FRandRange(-.45f, .45f) * S, FMath::FRandRange(.1f, .9f) * S * .8f);
            Piece(TEXT("Cube"), Off, FVector(FMath::FRandRange(.5f, 1.1f) * S / 100.f, .025f, .025f), i % 3 == 0 ? C : CoreWhite);
            Vel.Last() = FVector(S * 3.f, 0, 0);
        }
        break;
    case EPYRFx::DustPuff:
        for (int32 i = 0; i < 7; ++i)
        {
            const FVector D = FVector(FMath::Cos(i * 2 * PI / 7), FMath::Sin(i * 2 * PI / 7), .15f);
            Piece(TEXT("Sphere"), D * S * .2f, FVector(S * .004f), FMath::Lerp(C, CoreWhite, .3f));
            Vel.Last() = D * S * 2.f;
        }
        break;
    case EPYRFx::WindSwirl:
        for (int32 i = 0; i < 12; ++i)
        {
            const float A = i * 2 * PI / 12;
            const FVector P(0, FMath::Cos(A) * S * .25f, FMath::Sin(A) * S * .25f + S * .35f);
            Piece(TEXT("Cube"), P, FVector(.05f, S * .0022f, .04f), i % 4 == 0 ? PYRFx::Palette(TEXT("cfe8ff")) : CoreWhite, FRotator(0, 0, FMath::RadiansToDegrees(A) + 90.f));
            Vel.Last() = FVector(S * 2.2f, 0, 0);
        }
        break;
    case EPYRFx::Threads:
        for (int32 i = 0; i < 3; ++i)
        {
            Piece(TEXT("Cylinder"), FVector(S * .5f, (i - 1) * 10.f, (i - 1) * 6.f + 20.f), FVector(.04f, .04f, S / 100.f), CoreWhite, FRotator(90, 0, 0));
        }
        break;
    case EPYRFx::StatUp:
    case EPYRFx::StatDown:
    {
        const float Sign = Kind == EPYRFx::StatUp ? 1.f : -1.f;
        Piece(TEXT("Cylinder"), FVector(0, 0, 3), FVector(S * .014f, S * .014f, .01f), C * FLinearColor(1, 1, 1, .6f));  // Flash disc.
        for (int32 i = 0; i < 3; ++i)
        {
            const FVector Base((i - 1) * S * .3f, (i % 2 ? .25f : -.25f) * S, S * (Sign > 0 ? .2f : 1.1f));
            Piece(TEXT("Cone"), Base + FVector(0, 0, Sign * S * .25f), FVector(S * .003f, S * .003f, S * .0025f), C, FRotator(Sign > 0 ? 0.f : 180.f, 0, 0));
            Vel.Last() = FVector(0, 0, Sign * S * 1.4f);
            Piece(TEXT("Cube"), Base, FVector(S * .0012f, S * .0012f, S * .0035f), C);
            Vel.Last() = FVector(0, 0, Sign * S * 1.4f);
        }
        break;
    }
    case EPYRFx::RingPulse:
    case EPYRFx::Shockwave:
    {
        const bool bShock = Kind == EPYRFx::Shockwave;
        for (int32 i = 0; i < 28; ++i)
        {
            const float A = i * 2 * PI / 28;
            Piece(TEXT("Cube"), FVector(FMath::Cos(A), FMath::Sin(A), 0) * 10.f + FVector(0, 0, bShock ? 20.f : 30.f), FVector(.2f, bShock ? .3f : .08f, bShock ? .35f : .12f),
                i % 2 && !bShock ? CoreWhite : C, FRotator(0, FMath::RadiansToDegrees(A) + 90.f, 0));
        }
        break;
    }
    case EPYRFx::Needle:
        Piece(TEXT("Cone"), FVector::ZeroVector, FVector(.12f, .12f, .6f), C, FRotator(-90, 0, 0));
        Piece(TEXT("Cube"), FVector(-45, 0, 0), FVector(.6f, .03f, .03f), CoreWhite);
        break;
    case EPYRFx::Impact:
        // Radial speed lines and flat star sparks around the hit.
        for (int32 i = 0; i < 12; ++i)
        {
            const float A = i * 2 * PI / 12;
            const FVector D(0, FMath::Cos(A), FMath::Sin(A));
            Piece(TEXT("Cube"), D * S * .5f, FVector(.02f, S * .006f, .02f), CoreWhite, FRotator(0, 0, FMath::RadiansToDegrees(A) + 90.f));
            Vel.Last() = D * S * 2.5f;
        }
        for (int32 i = 0; i < 6; ++i)
        {
            const FVector D = FVector(FMath::FRandRange(-.3f, .3f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-.6f, 1.f)).GetSafeNormal();
            Piece(TEXT("Cube"), D * 10.f, FVector(.02f, S * .003f, S * .003f), i % 2 ? C : BoltYellow, FRotator(0, 0, 45.f));
            Vel.Last() = D * S * 3.f;
        }
        Piece(TEXT("Sphere"), FVector::ZeroVector, FVector(S * .008f), CoreWhite);
        break;
    case EPYRFx::Claw:
        for (int32 i = 0; i < 3; ++i)
            Piece(TEXT("Cube"), FVector(0, (i - 1) * S * .18f, S * .5f), FVector(.03f, .05f, S * .009f), i == 1 ? CoreWhite : C, FRotator(0, 0, 25.f));
        break;
    case EPYRFx::PsychicWave:
        for (int32 r = 0; r < 3; ++r)
            for (int32 i = 0; i < 12; ++i)
            {
                const float A = i * 2 * PI / 12;
                Piece(TEXT("Cube"), FVector(-r * 40.f, FMath::Cos(A) * 30.f, FMath::Sin(A) * 30.f + 40.f), FVector(.04f, .16f, .05f), r == 1 ? CoreWhite : C,
                    FRotator(0, 0, FMath::RadiansToDegrees(A) + 90.f));
                Vel.Last() = FVector(S * 1.2f, 0, 0);
            }
        break;
    case EPYRFx::SandSpray:
        for (int32 i = 0; i < 16; ++i)
        {
            const FVector D = FVector(1, FMath::FRandRange(-.5f, .5f), FMath::FRandRange(0.f, .35f)).GetSafeNormal();
            Piece(TEXT("Sphere"), FVector(0, 0, 25), FVector(FMath::FRandRange(.06f, .12f)), FMath::Lerp(C, CoreWhite, FMath::FRand() * .3f));
            Vel.Last() = D * S * 2.2f;
        }
        break;
    case EPYRFx::PowderCloud:
        for (int32 i = 0; i < 9; ++i)
        {
            const FVector D = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(.2f, 1.f)).GetSafeNormal();
            Piece(TEXT("Sphere"), D * S * .2f + FVector(0, 0, 40), FVector(S * .004f), FMath::Lerp(C, CoreWhite, .2f));
            Vel.Last() = D * S * .6f;
        }
        break;
    case EPYRFx::RockFall:
        Piece(TEXT("Sphere"), FVector(0, 0, 900), FVector(S * .007f, S * .006f, S * .0055f), C);
        Vel.Last() = FVector(0, 0, -900.f / FMath::Max(.05f, Life * .6f));
        break;
    case EPYRFx::TailArc:
        for (int32 i = 0; i < 9; ++i)
        {
            const float A = FMath::DegreesToRadians(-80.f + i * 20.f);
            const FVector D(FMath::Cos(A), FMath::Sin(A), 0);
            Piece(TEXT("Cube"), D * S * .85f + FVector(0, 0, 60), FVector(.5f, S * .0022f, .08f), i % 2 ? CoreWhite : C, FRotator(0, FMath::RadiansToDegrees(A) + 90.f, 0));
        }
        break;
    case EPYRFx::Glint:
        for (int32 i = 0; i < 4; ++i)
            Piece(TEXT("Cube"), FVector(0, 0, S * .6f), FVector(.03f, S * .008f, .03f), i % 2 ? C : CoreWhite, FRotator(0, 0, i * 45.f));
        break;
    // ---- m3
    case EPYRFx::WaterJet:
        // Local X is Dir: droplets trail back from the head, biggest at the front, with a white streak core.
        for (int32 i = 0; i < 7; ++i)
        {
            const float K = 1.f - i / 8.f;
            Piece(TEXT("Sphere"), FVector(-i * 26.f, FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(-6.f, 6.f)), FVector(.34f * K, .26f * K, .26f * K), i % 2 ? CoreWhite : C);
        }
        Piece(TEXT("Cube"), FVector(-70, 0, 0), FVector(1.4f, .06f, .06f), CoreWhite);
        break;
    case EPYRFx::Bubbles:
        for (int32 i = 0; i < 8; ++i)
        {
            const FVector Off(FMath::FRandRange(-.5f, .3f) * S, FMath::FRandRange(-.35f, .35f) * S, FMath::FRandRange(-.25f, .35f) * S);
            const float R = FMath::FRandRange(.12f, .24f);
            Piece(TEXT("Sphere"), Off, FVector(R), C * FLinearColor(1, 1, 1, .75f));
            Piece(TEXT("Sphere"), Off + FVector(R * 25.f, -R * 25.f, R * 30.f), FVector(R * .3f), CoreWhite);  // Highlight.
            Vel.Last() = Vel[Vel.Num() - 2] = FVector(0, 0, FMath::FRandRange(20.f, 60.f));
        }
        break;
    case EPYRFx::Flame:
        for (int32 i = 0; i < 6; ++i)
        {
            const FRotator Back(FMath::FRandRange(80.f, 100.f), FMath::FRandRange(-25.f, 25.f), 0);
            Piece(TEXT("Cone"), FVector(-i * 10.f, FMath::FRandRange(-10.f, 10.f), FMath::FRandRange(-8.f, 8.f)), FVector(.22f, .22f, .5f - i * .05f),
                i % 3 == 0 ? BoltYellow : i % 3 == 1 ? C : PYRFx::Palette(TEXT("e3342f")), Back);
        }
        Piece(TEXT("Sphere"), FVector::ZeroVector, FVector(.22f), CoreWhite);
        break;
    case EPYRFx::VineLash:
        for (int32 i = 0; i < 2; ++i)
            Piece(TEXT("Cylinder"), FVector(S * .5f, (i ? 1 : -1) * 12.f, 40.f + i * 8.f), FVector(.06f, .06f, S / 100.f), C, FRotator(-90, 0, (i ? 1 : -1) * 6.f));
        Piece(TEXT("Cube"), FVector(S, 0, 44), FVector(.3f, .18f, .04f), PYRFx::Palette(TEXT("7ad05a")), FRotator(0, 45, 0));
        Piece(TEXT("Cube"), FVector(S + 10, 0, 44), FVector(.08f, .4f, .04f), CoreWhite, FRotator(0, 0, 0));  // Crack flash.
        break;
    case EPYRFx::Notes:
        for (int32 i = 0; i < 6; ++i)
        {
            const float A = i * 2 * PI / 6;
            const FVector D(FMath::Cos(A), FMath::Sin(A), 0);
            const FVector At = D * 40.f + FVector(0, 0, 60);
            Piece(TEXT("Sphere"), At, FVector(.16f, .16f, .12f), i % 2 ? C : Ink);           // Note head.
            Vel.Last() = D * S * 1.2f + FVector(0, 0, 60);
            Piece(TEXT("Cube"), At + FVector(0, 7, 18), FVector(.02f, .02f, .34f), Ink);      // Stem.
            Vel.Last() = D * S * 1.2f + FVector(0, 0, 60);
            Piece(TEXT("Cube"), At + FVector(0, 12, 34), FVector(.02f, .12f, .03f), Ink, FRotator(0, 0, -25));  // Flag.
            Vel.Last() = D * S * 1.2f + FVector(0, 0, 60);
        }
        break;
    case EPYRFx::GasCloud:
        for (int32 i = 0; i < 10; ++i)
        {
            const float T = i / 9.f;
            const FVector At(T * S * .8f, FMath::FRandRange(-.18f, .18f) * S * (.4f + T), 30.f + FMath::FRandRange(0.f, .2f) * S);
            Piece(TEXT("Sphere"), At, FVector(S * .003f * (.6f + T)), FMath::Lerp(C, CoreWhite, FMath::FRand() * .25f) * FLinearColor(1, 1, 1, .85f));
            Vel.Last() = FVector(S * .6f, FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(0.f, 40.f));
        }
        break;
    case EPYRFx::Coins:
        for (int32 i = 0; i < 9; ++i)
        {
            const FVector D = FVector(FMath::FRandRange(-.2f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(.3f, 1.f)).GetSafeNormal();
            Piece(TEXT("Cylinder"), D * 10.f + FVector(0, 0, 40), FVector(.16f, .16f, .02f), i % 3 ? PYRFx::Palette(TEXT("f0c030")) : CoreWhite, FRotator(90, 0, 0));
            Vel.Last() = D * S * 1.4f;
        }
        break;
    case EPYRFx::Stars:
        for (int32 s = 0; s < 5; ++s)
        {
            const FVector At(-s * 34.f, FMath::Sin(s * 1.7f) * 24.f, FMath::Cos(s * 1.3f) * 18.f);
            for (int32 i = 0; i < 3; ++i) Piece(TEXT("Cube"), At, FVector(.04f, .34f, .07f), s % 2 ? BoltYellow : CoreWhite, FRotator(0, 0, i * 60.f));
        }
        break;
    case EPYRFx::Seeds:
        for (int32 i = 0; i < 3; ++i)
        {
            const FVector At(-i * 30.f, (i - 1) * 14.f, (i % 2) * 10.f);
            Piece(TEXT("Sphere"), At, FVector(.14f, .1f, .1f), PYRFx::Palette(TEXT("8a6030")));
            Piece(TEXT("Cone"), At + FVector(8, 0, 5), FVector(.06f, .06f, .1f), PYRFx::Palette(TEXT("5cb85c")), FRotator(-60, 0, 0));
        }
        break;
    case EPYRFx::DrainOrbs:
        for (int32 i = 0; i < 7; ++i)
        {
            Piece(TEXT("Sphere"), FVector(-i * 18.f, FMath::FRandRange(-20.f, 20.f), 40.f + FMath::FRandRange(-20.f, 20.f)), FVector(FMath::FRandRange(.1f, .18f)),
                i % 2 ? CoreWhite : C);
            Vel.Last() = FVector(S / FMath::Max(.1f, Life), 0, 0);
        }
        break;
    case EPYRFx::SleepZ:
        for (int32 z = 0; z < 3; ++z)
        {
            const FVector At(0, z * 16.f, S * .6f + z * 22.f);
            const float K = .6f + z * .2f;
            Piece(TEXT("Cube"), At + FVector(0, 0, 10 * K), FVector(.03f, .2f * K, .035f), C);
            Vel.Last() = FVector(0, 20, 50);
            Piece(TEXT("Cube"), At, FVector(.03f, .26f * K, .035f), C, FRotator(0, 0, -45));
            Vel.Last() = FVector(0, 20, 50);
            Piece(TEXT("Cube"), At - FVector(0, 0, 10 * K), FVector(.03f, .2f * K, .035f), C);
            Vel.Last() = FVector(0, 20, 50);
        }
        break;
    case EPYRFx::DizzyStars:
        for (int32 s = 0; s < 4; ++s)
            for (int32 i = 0; i < 2; ++i)
                Piece(TEXT("Cube"), FVector::ZeroVector, FVector(.03f, .18f, .05f), s % 2 ? BoltYellow : CoreWhite, FRotator(0, 0, i * 90.f + 45.f));
        break;
    case EPYRFx::HealSparkle:
        for (int32 i = 0; i < 10; ++i)
        {
            const FVector At(FMath::FRandRange(-.4f, .4f) * S, FMath::FRandRange(-.4f, .4f) * S, FMath::FRandRange(0.f, .6f) * S);
            for (int32 k = 0; k < 2; ++k)
            {
                Piece(TEXT("Cube"), At, FVector(.03f, .03f, .16f), i % 2 ? C : CoreWhite, FRotator(0, 0, k * 90.f));
                Vel.Last() = FVector(0, 0, S * .8f);
            }
        }
        break;
    case EPYRFx::Coil:
        for (int32 r = 0; r < 3; ++r)
            for (int32 i = 0; i < 14; ++i)
            {
                const float A = i * 2 * PI / 14;
                Piece(TEXT("Cube"), FVector(FMath::Cos(A), FMath::Sin(A), 0) * S * .5f + FVector(0, 0, 20.f + r * S * .3f), FVector(S * .0024f, .1f, .12f),
                    r == 1 ? FMath::Lerp(C, CoreWhite, .3f) : C, FRotator(0, FMath::RadiansToDegrees(A) + 90.f, 0));
            }
        break;
    case EPYRFx::Splash:
        for (int32 i = 0; i < 14; ++i)
        {
            const float A = i * 2 * PI / 14 + FMath::FRandRange(-.2f, .2f);
            const FVector D(FMath::Cos(A) * .6f, FMath::Sin(A) * .6f, FMath::FRandRange(.6f, 1.f));
            Piece(TEXT("Sphere"), FVector(0, 0, 10), FVector(FMath::FRandRange(.08f, .16f)), i % 3 ? C : CoreWhite);
            Vel.Last() = D * S * 2.2f;
        }
        break;
    case EPYRFx::Explosion:
        Piece(TEXT("Sphere"), FVector(0, 0, 60), FVector(S * .006f), CoreWhite);
        Piece(TEXT("Sphere"), FVector(0, 0, 60), FVector(S * .009f), PYRFx::Palette(TEXT("f08030")) * FLinearColor(1, 1, 1, .8f));
        for (int32 i = 0; i < 14; ++i)
        {
            const float A = i * 2 * PI / 14;
            const FVector D(FMath::Cos(A), FMath::Sin(A), FMath::FRandRange(-.2f, .6f));
            Piece(TEXT("Cube"), D * 30.f + FVector(0, 0, 60), FVector(S * .004f, .05f, .05f), i % 2 ? BoltYellow : CoreWhite, D.Rotation());
            Vel.Last() = D * S * 2.f;
        }
        break;
    case EPYRFx::DirtMound:
        Piece(TEXT("Sphere"), FVector(0, 0, 0), FVector(S * .008f, S * .008f, S * .003f), C);
        for (int32 i = 0; i < 5; ++i)
        {
            const float A = i * 2 * PI / 5;
            Piece(TEXT("Sphere"), FVector(FMath::Cos(A), FMath::Sin(A), 0) * S * .45f + FVector(0, 0, 8), FVector(.12f), FMath::Lerp(C, Ink, .3f));
        }
        break;
    case EPYRFx::Crescent:
        for (int32 i = 0; i < 7; ++i)
        {
            const float A = FMath::DegreesToRadians(-70.f + i * 140.f / 6.f);
            Piece(TEXT("Cube"), FVector(FMath::Cos(A) * 30.f, FMath::Sin(A) * 60.f, 40.f), FVector(.06f, .22f, .06f), i % 2 ? C : CoreWhite, FRotator(0, FMath::RadiansToDegrees(A), 0));
        }
        break;
    case EPYRFx::Tornado:
        for (int32 r = 0; r < 6; ++r)
            for (int32 i = 0; i < 8; ++i)
            {
                const float A = i * 2 * PI / 8 + r * .4f;
                const float Rad = S * (.12f + r * .05f);
                Piece(TEXT("Cube"), FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, 20.f + r * S * .1f), FVector(.05f, S * .0018f, .04f), (i + r) % 3 ? CoreWhite : C,
                    FRotator(0, FMath::RadiansToDegrees(A) + 90.f, 0));
            }
        break;
    case EPYRFx::SoundWave:
        for (int32 r = 0; r < 3; ++r)
            for (int32 i = 0; i < 5; ++i)
            {
                const float A = FMath::DegreesToRadians(-50.f + i * 25.f);
                Piece(TEXT("Cube"), FVector(-r * 36.f + FMath::Cos(A) * 30.f, FMath::Sin(A) * 40.f, 40.f), FVector(.04f, .14f, .1f), r == 1 ? C : CoreWhite, FRotator(0, FMath::RadiansToDegrees(A), 0));
            }
        break;
    case EPYRFx::Orb:
        Piece(TEXT("Sphere"), FVector::ZeroVector, FVector(.5f), C * FLinearColor(1, 1, 1, .6f));
        Piece(TEXT("Sphere"), FVector::ZeroVector, FVector(.26f), CoreWhite);
        break;
    case EPYRFx::Ripple:
        for (int32 r = 0; r < 2; ++r)
            for (int32 i = 0; i < 16; ++i)
            {
                const float A = i * 2 * PI / 16;
                Piece(TEXT("Cube"), FVector(FMath::Cos(A), FMath::Sin(A), 0) * 10.f + FVector(0, 0, 2), FVector(.1f, .05f, .02f), r ? C : CoreWhite,
                    FRotator(0, FMath::RadiansToDegrees(A) + 90.f, 0));
            }
        break;
    }
}

void APYRMoveFx::Tick(float Dt)
{
    Super::Tick(Dt);
    Age += Dt;
    const float A = FMath::Clamp(Age / Life, 0.f, 1.f);
    const float Fade = 1.f - A;
    switch (Kind)
    {
    case EPYRFx::Bolt:
        Rejag -= Dt;
        if (Rejag <= 0) { Rejag = .05f; BuildBolts(); }
        return;
    case EPYRFx::RingPulse:
    case EPYRFx::Shockwave:
    {
        const float R = FMath::Lerp(20.f, Size, FMath::Sqrt(A));
        const int32 N = Pieces.Num();
        for (int32 i = 0; i < N; ++i)
        {
            const float Ang = i * 2 * PI / N;
            Pieces[i]->SetRelativeLocation(FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0) * R + FVector(0, 0, BaseLoc[i].Z));
            Pieces[i]->SetRelativeScale3D(FVector(R * 2 * PI / N / 100.f * 1.1f, BaseScale[i].Y, BaseScale[i].Z * (Kind == EPYRFx::Shockwave ? 1.f : Fade + .2f)));
        }
        return;
    }
    case EPYRFx::Threads:
        for (int32 i = 0; i < Pieces.Num(); ++i)
            Pieces[i]->SetRelativeLocation(BaseLoc[i] + FVector(0, FMath::Sin(Age * 30.f + i) * 6.f, 0));
        if (A > .7f) for (int32 i = 0; i < Pieces.Num(); ++i) Pieces[i]->SetRelativeScale3D(BaseScale[i] * FVector(1, 1, (1 - A) / .3f));
        return;
    case EPYRFx::DizzyStars:
        // Two pieces per star, circling the head.
        for (int32 i = 0; i < Pieces.Num(); ++i)
        {
            const float Ang = Age * 6.f + (i / 2) * PI * .5f;
            Pieces[i]->SetRelativeLocation(FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0) * Size * .45f + FVector(0, 0, Size * .95f));
            Pieces[i]->SetRelativeScale3D(BaseScale[i] * FMath::Min(1.f, Fade * 4.f));
        }
        return;
    case EPYRFx::Ripple:
    {
        const int32 N = Pieces.Num() / 2;
        for (int32 i = 0; i < Pieces.Num(); ++i)
        {
            const bool bInner = i >= N;
            const float R = FMath::Lerp(15.f, Size, FMath::Sqrt(A)) * (bInner ? .6f : 1.f);
            const float Ang = (i % FMath::Max(1, N)) * 2 * PI / FMath::Max(1, N);
            Pieces[i]->SetRelativeLocation(FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0) * R + FVector(0, 0, 2));
            Pieces[i]->SetRelativeScale3D(FVector(R * 2 * PI / FMath::Max(1, N) / 100.f * 1.05f, BaseScale[i].Y, BaseScale[i].Z) * FMath::Min(1.f, Fade * 2.f + .1f));
        }
        return;
    }
    case EPYRFx::DirtMound:
        for (int32 i = 0; i < Pieces.Num(); ++i)
            Pieces[i]->SetRelativeScale3D(BaseScale[i] * (1.f + .08f * FMath::Sin(Age * 24.f + i)) * FMath::Min(1.f, Fade * 5.f));
        return;
    case EPYRFx::Orb:
        for (int32 i = 0; i < Pieces.Num(); ++i)
            Pieces[i]->SetRelativeScale3D(BaseScale[i] * (1.f + .15f * FMath::Sin(Age * 20.f)) * FMath::Min(1.f, Fade * 3.f));
        return;
    case EPYRFx::StatUp:
    case EPYRFx::StatDown:
    case EPYRFx::CheekSpark:
    case EPYRFx::SpeedLines:
    case EPYRFx::DustPuff:
    case EPYRFx::WindSwirl:
    case EPYRFx::Impact:
    case EPYRFx::PsychicWave:
    case EPYRFx::SandSpray:
    case EPYRFx::PowderCloud:
    case EPYRFx::RockFall:
    default:
    {
        const bool bGrow = Kind == EPYRFx::DustPuff || Kind == EPYRFx::PowderCloud || Kind == EPYRFx::GasCloud || Kind == EPYRFx::Explosion;
        const bool bSpin = Kind == EPYRFx::WindSwirl;
        for (int32 i = 0; i < Pieces.Num(); ++i)
        {
            FVector P = Pieces[i]->GetRelativeLocation() + Vel[i] * Dt;
            if (Kind == EPYRFx::SandSpray || Kind == EPYRFx::Splash || Kind == EPYRFx::Coins) Vel[i].Z -= 900.f * Dt;
            if (Kind == EPYRFx::Coins) Pieces[i]->AddRelativeRotation(FRotator(0, 900.f * Dt, 0));
            if (Kind == EPYRFx::Stars) Pieces[i]->AddRelativeRotation(FRotator(0, 0, 540.f * Dt));
            if (Kind == EPYRFx::Bubbles) P.Y += FMath::Sin(Age * 14.f + i) * 12.f * Dt;
            if (Kind == EPYRFx::Tornado)
            {
                const FVector Turned = FVector(P.X, P.Y, 0).RotateAngleAxis(600.f * Dt, FVector::UpVector);
                P = FVector(Turned.X, Turned.Y, P.Z);
                Pieces[i]->AddRelativeRotation(FRotator(0, 600.f * Dt, 0));
            }
            if (Kind == EPYRFx::RockFall && P.Z < 30.f) { P.Z = 30.f; Vel[i] = FVector::ZeroVector; }
            if (bSpin)
            {
                const FVector Radial(0, P.Y, P.Z - Size * .35f);
                const FVector Turned = Radial.RotateAngleAxis(720.f * Dt, FVector::ForwardVector) * (1.f + Dt * 1.5f);
                P = FVector(P.X, Turned.Y, Turned.Z + Size * .35f);
                Pieces[i]->AddRelativeRotation(FRotator(0, 0, 720.f * Dt));
            }
            Pieces[i]->SetRelativeLocation(P);
            const float K = bGrow ? (1.f + A * 1.5f) * FMath::Min(1.f, Fade * 3.f) : Kind == EPYRFx::RockFall ? 1.f : FMath::Min(1.f, Fade * 2.f);
            Pieces[i]->SetRelativeScale3D(BaseScale[i] * FMath::Max(.01f, K));
        }
        return;
    }
    }
}

// ------------------------------------------------------------------------------------------------ bursts
APYRFxBurst::APYRFxBurst()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

APYRFxBurst* APYRFxBurst::Spawn(UWorld* World, FVector At, FLinearColor Colour, int32 Count, float Size, float Speed, float Life, const TCHAR* ShapeName, float Gravity)
{
    if (!World) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APYRFxBurst* Fx = World->SpawnActor<APYRFxBurst>(At, FRotator::ZeroRotator, Params);
    if (!Fx) return nullptr;
    Fx->Life = Life;
    Fx->Gravity = Gravity;
    for (int32 i = 0; i < Count; ++i)
    {
        const FVector Dir = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-.2f, 1.f)).GetSafeNormal();
        // Flat graphic sparks: long thin shards pointing along their flight.
        const FVector Scale = FVector(Size * 2.2f, Size * .5f, Size * .5f) / 100.f;
        UStaticMeshComponent* Piece = PYRFx::AddPrimitive(Fx, nullptr, ShapeName, FVector::ZeroVector, Scale,
            FMath::Lerp(Colour, FLinearColor::White, FMath::FRand() * .35f), Dir.Rotation(), true);
        Piece->CastShadow = false;
        Fx->Pieces.Add(Piece);
        Fx->Velocities.Add(Dir * Speed * FMath::FRandRange(.6f, 1.2f));
        Fx->BaseScales.Add(Scale);
    }
    Fx->SetLifeSpan(Life + .1f);
    return Fx;
}

void APYRFxBurst::Tick(float Dt)
{
    Super::Tick(Dt);
    Age += Dt;
    const float K = FMath::Clamp(1.f - Age / FMath::Max(.01f, Life), 0.f, 1.f);
    for (int32 i = 0; i < Pieces.Num(); ++i)
    {
        Velocities[i].Z += Gravity * Dt;
        Pieces[i]->AddRelativeLocation(Velocities[i] * Dt);
        Pieces[i]->SetRelativeScale3D(BaseScales[i] * K);
    }
}

void UPYRShakeModifier::Kick(float InStrength, float Seconds)
{
    Strength = FMath::Max(Strength * (Left > 0 ? 1.f : 0.f), InStrength);
    Left = Total = Seconds;
}

bool UPYRShakeModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    Super::ModifyCamera(DeltaTime, InOutPOV);
    if (Left <= 0) return false;
    // Real time, so the hit-stop freeze still shakes.
    const float Dt = FMath::Min(FApp::GetDeltaTime(), .05);
    Left -= Dt;
    const float K = Strength * FMath::Max(0.f, Left / FMath::Max(.01f, Total));
    InOutPOV.Location += FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * K;
    return false;
}
