#include "PYRRing.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/PYRKit.h"
#include "World/PYRQuery.h"

namespace
{
const FLinearColor CreamSage = FLinearColor::FromSRGBColor(FColor(0xC9, 0xC6, 0xA2));
const FLinearColor WornEarth = FLinearColor::FromSRGBColor(FColor(0x8A, 0x74, 0x55));
const FLinearColor Sunflower = FLinearColor::FromSRGBColor(FColor(0xF2, 0xD1, 0x4B));

UInstancedStaticMeshComponent* MakeDabs(AActor* Owner, const TCHAR* Name, const TCHAR* Mesh)
{
    UInstancedStaticMeshComponent* Comp = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
    Comp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->SetCastShadow(false);
    Comp->SetMobility(EComponentMobility::Movable);
    return Comp;
}
}

APYRRing::APYRRing()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Rim = MakeDabs(this, TEXT("Rim"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Rim->SetupAttachment(RootComponent);
    Worn = MakeDabs(this, TEXT("Worn"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Worn->SetupAttachment(RootComponent);
    FillArc = MakeDabs(this, TEXT("FillArc"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    FillArc->SetupAttachment(RootComponent);
    Tags.Add(TEXT("BattleRing"));
}

void APYRRing::Paint(UInstancedStaticMeshComponent* Comp, const FLinearColor& Colour)
{
    PYRWorld::Paint(Comp, Colour);
}

float APYRRing::GroundZ(const FVector& XY) const
{
    FHitResult Hit;
    return PYRWorld::Ground(GetWorld(), XY, Center.Z, Hit, 250.f, 400.f) ? Hit.ImpactPoint.Z : Center.Z;
}

bool APYRRing::IsInGap(const FVector& Point) const
{
    return bGap && FVector::Dist2D(Point, GapCenter) <= GapWidth * .5f + 60.f;
}

void APYRRing::Setup(const FVector& InCenter, float InRadius, bool bInGap, const FVector& InGapCenter)
{
    Center = InCenter;
    Radius = InRadius;
    bGap = bInGap;
    GapCenter = InGapCenter;
    SetActorLocation(FVector::ZeroVector);
    Paint(Rim, CreamSage);
    Paint(Worn, WornEarth);
    Paint(FillArc, Sunflower);
    // m4v: the ring's stones and worn earth painted with the anime kit (World/PYRKit).
    PYRKit::Apply(Rim, EPYRKit::RockGrey, CreamSage, 60.f);
    PYRKit::Apply(Worn, EPYRKit::PathDirt, WornEarth);
    PYRKit::Apply(FillArc, EPYRKit::Paint, Sunflower);
    const FVector GapDir = (GapCenter - Center).GetSafeNormal2D();
    const FVector Side = FVector::CrossProduct(GapDir, FVector::UpVector);
    const float GapHalfAngle = FMath::Atan2(GapWidth * .5f, Radius);
    auto InGapArc = [&](const FVector& Dir, float Extra)
    {
        return bGap && FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, GapDir), -1.f, 1.f)) < GapHalfAngle + Extra;
    };

    // Painted dabs: two slightly offset rows of flattened discs read as one brushed band.
    const int32 Count = FMath::Clamp(FMath::RoundToInt(2.f * PI * Radius / 70.f), 48, 160);
    RimTransforms.Reset();
    for (int32 Row = 0; Row < 2; ++Row)
        for (int32 I = 0; I < Count; ++I)
        {
            const float A = (I + Row * .5f) / Count * 2.f * PI;
            const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
            if (InGapArc(Dir, 0.f)) continue;
            FVector P = Center + Dir * (Radius + (Row ? -35.f : 15.f));
            P.Z = GroundZ(P) + 3.f;
            const float Size = (Row ? .75f : 1.f) * (0.9f + .25f * FMath::Frac(I * .618f));
            RimTransforms.Add(FTransform(FRotator(0, FMath::RadiansToDegrees(A), 0), P, FVector(Size * 1.1f, Size * .7f, .04f)));
        }
    Rim->ClearInstances();
    Rim->AddInstances(RimTransforms, false, true);

    Worn->ClearInstances();
    FillTransforms.Reset();
    if (bGap)  // Worn earth scuffs across the gap, and the fill arc just inside it.
    {
        for (int32 I = 0; I < 7; ++I)
        {
            FVector P = GapCenter + Side * ((I - 3) * 45.f) + GapDir * (FMath::Frac(I * .37f) * 60.f - 30.f);
            P.Z = GroundZ(P) + 2.f;
            Worn->AddInstance(FTransform(FRotator(0, I * 40.f, 0), P, FVector(.8f, .5f, .02f)), true);
        }
        for (int32 I = 0; I < 16; ++I)
        {
            const float A = PI * (I / 15.f);
            FVector P = GapCenter - GapDir * 40.f + (Side * -FMath::Cos(A) - GapDir * FMath::Sin(A)) * (GapWidth * .5f);
            P.Z = GroundZ(P) + 15.f;
            FillTransforms.Add(FTransform(FRotator::ZeroRotator, P, FVector(.18f)));
        }
    }

    // Invisible walls: pawns only, so traces and cameras pass through.
    const int32 WallCount = 40;
    const float Arc = 2.f * PI * Radius / WallCount;
    for (int32 I = 0; I < WallCount; ++I)
    {
        const float A = (I + .5f) / WallCount * 2.f * PI;
        const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0);
        if (InGapArc(Dir, PI / WallCount)) continue;
        UBoxComponent* Wall = NewObject<UBoxComponent>(this);
        Wall->SetupAttachment(RootComponent);
        Wall->SetBoxExtent(FVector(20.f, Arc * .55f, 600.f));
        Wall->SetCollisionObjectType(ECC_WorldDynamic);
        Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
        Wall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Wall->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
        Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Wall->SetHiddenInGame(true);
        Wall->RegisterComponent();
        Wall->SetWorldLocationAndRotation(Center + Dir * (Radius + 20.f) + FVector(0, 0, 400.f), FRotator(0, FMath::RadiansToDegrees(A), 0));
        Walls.Add(Wall);
    }
}

void APYRRing::Rise(float Seconds)
{
    Grow = 0.f;
    GrowRate = 1.f / FMath::Max(.05f, Seconds);
}

void APYRRing::SetFill(float Fraction)
{
    Fill = FMath::Clamp(Fraction, 0.f, 1.f);
    const int32 Want = FMath::FloorToInt(Fill * FillTransforms.Num() + .001f);
    if (Want == FillShown) return;
    FillArc->ClearInstances();
    for (int32 I = 0; I < Want; ++I) FillArc->AddInstance(FillTransforms[I], true);
    FillShown = Want;
}

void APYRRing::FadeOut()
{
    for (UBoxComponent* Wall : Walls) if (Wall) Wall->DestroyComponent();
    Walls.Reset();
    SetFill(0);
    Fade = 1.f;
}

void APYRRing::GetRimPoints(int32 Count, TArray<FVector>& Out) const
{
    Out.Reset();
    for (int32 I = 0; I < Count; ++I)
    {
        const float A = I * 2.f * PI / Count;
        FVector P = Center + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Radius;
        P.Z = GroundZ(P);
        Out.Add(P);
    }
}

void APYRRing::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    float Scale = -1.f;
    if (Grow < 1.f) { Grow = FMath::Min(1.f, Grow + DeltaSeconds * GrowRate); Scale = Grow; }
    if (Fade >= 0.f)
    {
        Fade -= DeltaSeconds;
        if (Fade <= 0.f) { Destroy(); return; }
        Scale = Fade;
    }
    if (Scale < 0.f) return;
    TArray<FTransform> Now = RimTransforms;
    for (FTransform& T : Now) T.SetScale3D(T.GetScale3D() * FVector(Scale, Scale, 1.f));
    if (Now.Num()) Rim->BatchUpdateInstancesTransforms(0, Now, false, true, true);
    Worn->SetVisibility(Scale > .05f);
}
