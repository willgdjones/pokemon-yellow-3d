#include "PYRPoolRules.h"

#include "../PYRMonPawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

const FName FPYRPoolRules::WaterTag(TEXT("PYR.Water"));
const FName FPYRPoolRules::PlatformTag(TEXT("PYR.Platform"));

namespace
{
// Distance from P to a box's footprint (0 inside).
float FootprintDistance(const FBox& Box, FVector P)
{
    const double Dx = FMath::Max3(Box.Min.X - P.X, 0.0, P.X - Box.Max.X);
    const double Dy = FMath::Max3(Box.Min.Y - P.Y, 0.0, P.Y - Box.Max.Y);
    return float(FMath::Sqrt(Dx * Dx + Dy * Dy));
}
}

void FPYRPoolRules::Reset()
{
    Water.Reset();
    Platforms.Reset();
    WaterBoxes.Reset();
    Samples.Reset();
    Surface = 0;
    World = nullptr;
}

bool FPYRPoolRules::Setup(UWorld* InWorld, FVector Center, float Radius)
{
    Reset();
    if (!InWorld) return false;
    World = InWorld;
    const float Reach = Radius + 600.f;
    for (TActorIterator<AActor> It(InWorld); It; ++It)
    {
        AActor* A = *It;
        if (!IsValid(A)) continue;
        const bool bWater = A->ActorHasTag(WaterTag), bPlatform = A->ActorHasTag(PlatformTag);
        if (!bWater && !bPlatform) continue;
        const FBox Box = A->GetComponentsBoundingBox(true);
        if (!Box.IsValid || FootprintDistance(Box, Center) > Reach) continue;
        if (bWater) { Water.Add(A); WaterBoxes.Add(Box); }
        else Platforms.Add(A);
    }
    if (Water.Num() == 0) { Reset(); return false; }
    Surface = -1e9f;
    for (const FBox& B : WaterBoxes) Surface = FMath::Max(Surface, float(B.Max.Z));
    // Open-water points on a 100 cm grid (platforms drift only a little as they bob).
    for (const FBox& B : WaterBoxes)
        for (float X = B.Min.X + 50.f; X < B.Max.X; X += 100.f)
            for (float Y = B.Min.Y + 50.f; Y < B.Max.Y; Y += 100.f)
            {
                const FVector P(X, Y, Surface);
                if (FootprintDistance(B, P) <= 0.f && !HasFootingAt(P)) Samples.Add(P);
                if (Samples.Num() > 8000) break;
            }
    return true;
}

bool FPYRPoolRules::InFootprint(FVector P) const
{
    for (const FBox& B : WaterBoxes) if (P.X >= B.Min.X && P.X <= B.Max.X && P.Y >= B.Min.Y && P.Y <= B.Max.Y) return true;
    return false;
}

bool FPYRPoolRules::IsWater(const AActor* Actor) const
{
    if (!Actor) return false;
    for (const TWeakObjectPtr<AActor>& W : Water) if (W.Get() == Actor) return true;
    return Actor->ActorHasTag(WaterTag);
}

bool FPYRPoolRules::HasFootingAt(FVector P, float* OutZ) const
{
    UWorld* W = World.Get();
    if (!W) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PYRPoolFooting), false);
    for (const TWeakObjectPtr<AActor>& A : Water) if (A.IsValid()) Query.AddIgnoredActor(A.Get());
    const FVector From(P.X, P.Y, Surface + 250.f), To(P.X, P.Y, Surface - 8.f);
    FHitResult Hit;
    for (int32 Try = 0; Try < 6; ++Try)
    {
        if (!W->LineTraceSingleByObjectType(Hit, From, To, Objects, Query)) return false;
        const UPrimitiveComponent* Comp = Hit.GetComponent();
        // Only things a Pokemon could stand on (skip trigger volumes and effect pieces).
        if (!Comp || Comp->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
        {
            if (OutZ) *OutZ = Hit.ImpactPoint.Z;
            return true;
        }
        Query.AddIgnoredComponent(Comp);
    }
    return false;
}

bool FPYRPoolRules::IsStandingInWater(const APYRMonPawn* Pawn) const
{
    if (!IsActive() || !Pawn) return false;
    const FVector At = Pawn->GetActorLocation();
    if (!InFootprint(At)) return false;
    if (const UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
        if (Move->IsMovingOnGround() && Move->CurrentFloor.bBlockingHit)
        {
            // Walking: on the water surface itself (a blocking surface), or on anything else.
            const AActor* Floor = Move->CurrentFloor.HitResult.GetActor();
            if (Floor) return IsWater(Floor);
        }
    // Falling or floating: in the water once the feet reach the surface with no footing under the body.
    return Pawn->BottomZ() <= Surface + 5.f && !HasFootingAt(At);
}

bool FPYRPoolRules::NearestPlatform(FVector P, FVector& Out) const
{
    float Best = 1e12f;
    bool bFound = false;
    for (const TWeakObjectPtr<AActor>& A : Platforms)
    {
        if (!A.IsValid()) continue;
        const FBox Box = A->GetComponentsBoundingBox(false);
        if (!Box.IsValid) continue;
        const FVector Top(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z);
        const float D = FVector::DistSquared2D(Top, P);
        if (D < Best) { Best = D; Out = Top; bFound = true; }
    }
    return bFound;
}

bool FPYRPoolRules::NearestOpenWater(FVector P, FVector& Out) const
{
    float Best = 1e12f;
    bool bFound = false;
    for (const FVector& S : Samples)
    {
        const float D = FVector::DistSquared2D(S, P);
        if (D < Best) { Best = D; Out = S; bFound = true; }
    }
    return bFound;
}

void FPYRPoolRules::IgnoreWater(UPrimitiveComponent* Comp) const
{
    if (!Comp) return;
    for (const TWeakObjectPtr<AActor>& A : Water) if (A.IsValid()) Comp->IgnoreActorWhenMoving(A.Get(), true);
}
