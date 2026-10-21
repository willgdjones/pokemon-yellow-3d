// The battle ring: a painted rim of flattened cream-sage dabs that follows the ground, invisible walls that
// keep Pokemon in, and (wild rings only) one worn 3 m gap toward Red with a fill arc for the 3 s retreat.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PYRRing.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRRing : public AActor
{
    GENERATED_BODY()
public:
    APYRRing();
    static constexpr float GapWidth = 300.f;

    void Setup(const FVector& InCenter, float InRadius, bool bInGap, const FVector& InGapCenter);
    // 0..1 of the retreat timer, shown as an arc of dabs around the gap.
    void SetFill(float Fraction);
    // The rim grows in over Seconds.
    void Rise(float Seconds);
    // Walls go at once; the rim shrinks away over a second and the actor destroys itself.
    void FadeOut();
    virtual void Tick(float DeltaSeconds) override;

    // Rim points on the ground, for the camera test.
    void GetRimPoints(int32 Count, TArray<FVector>& Out) const;
    bool IsInGap(const FVector& Point) const;
    bool HasGap() const { return bGap; }
    int32 NumWalls() const { return Walls.Num(); }
    float GetRadius() const { return Radius; }
    FVector GetCenter() const { return Center; }
    FVector GetGapCenter() const { return GapCenter; }
    float GetFill() const { return Fill; }
    bool IsFading() const { return Fade >= 0.f; }

    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Rim;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Worn;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> FillArc;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> Walls;

private:
    void Paint(UInstancedStaticMeshComponent* Comp, const FLinearColor& Colour);
    float GroundZ(const FVector& XY) const;
    FVector Center = FVector::ZeroVector, GapCenter = FVector::ZeroVector;
    float Radius = 1000.f, Fill = 0, Grow = 1, GrowRate = 0, Fade = -1;
    bool bGap = false;
    TArray<FTransform> RimTransforms;
    TArray<FTransform> FillTransforms;
    int32 FillShown = 0;
};
