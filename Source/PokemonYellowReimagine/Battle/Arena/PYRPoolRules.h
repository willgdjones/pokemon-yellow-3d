// The water arena rules (Misty's pool, or anywhere tagged water meets a ring). The battle manager calls Setup when a
// fight starts: it finds actors tagged PYR.Water (the water surface) and PYR.Platform (floating platforms) whose
// footprint meets the ring, and every body asks it where the water is.
//   - Runners and crawlers stand only on platforms and dry ground (the walkway). Stepping or landing on open water is
//     a fall: no damage, 1 s lost, then they pop up on the nearest platform. They cross gaps with the dodge dash or a
//     hop (E).
//   - Fliers hover over the water freely (never below the surface).
//   - Swimmers move through open water at 6 m/s and can dive out of reach (see APYRMonPawn::TryDive); on dry ground
//     they flop at 1.5 m/s.
// It works whether the water surface blocks pawns or not: "open water" is decided by what solid ground lies under a
// point with the water ignored.
#pragma once

#include "CoreMinimal.h"

class AActor;
class UPrimitiveComponent;
class UWorld;
class APYRMonPawn;

struct POKEMONYELLOWREIMAGINE_API FPYRPoolRules
{
    static const FName WaterTag;     // PYR.Water
    static const FName PlatformTag;  // PYR.Platform

    // Finds tagged water meeting the circle (Center, Radius). False (and inactive) when there is none.
    bool Setup(UWorld* InWorld, FVector Center, float Radius);
    void Reset();
    bool IsActive() const { return Water.Num() > 0; }

    // The top of the water surface.
    float SurfaceZ() const { return Surface; }
    // Inside the water's footprint (x/y only).
    bool InFootprint(FVector P) const;
    // Solid ground (a platform, the walkway) at or above the surface under P, the water itself ignored.
    bool HasFootingAt(FVector P, float* OutZ = nullptr) const;
    // Open water at P: in the footprint with no footing above the surface.
    bool IsOpenWater(FVector P) const { return InFootprint(P) && !HasFootingAt(P); }
    // A runner or crawler whose feet are on (or in) open water.
    bool IsStandingInWater(const APYRMonPawn* Pawn) const;
    // The top centre of the platform nearest P (x/y). False when there are no platforms.
    bool NearestPlatform(FVector P, FVector& Out) const;
    // The open-water point nearest P, at the surface. False when there is none.
    bool NearestOpenWater(FVector P, FVector& Out) const;
    // Lets Comp's moves pass through the water surface (swimmers under it).
    void IgnoreWater(UPrimitiveComponent* Comp) const;
    // Is this actor one of the pool's water surfaces?
    bool IsWater(const AActor* Actor) const;

    TArray<TWeakObjectPtr<AActor>> Water, Platforms;

private:
    TWeakObjectPtr<UWorld> World;
    TArray<FBox> WaterBoxes;
    TArray<FVector> Samples;  // Open-water points on a 100 cm grid, at the surface.
    float Surface = 0;
};
