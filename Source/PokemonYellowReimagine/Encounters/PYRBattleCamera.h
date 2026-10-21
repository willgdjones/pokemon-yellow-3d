// The battle camera. It starts exactly where the follow camera was, sweeps low toward the ring (0.4 s), then
// rises (0.6 s) to a fixed-yaw anime three-quarter view from behind Red's side: 30 degrees down on a ring (28 in
// the gym, so Onix rears up against the back wall), as close as it can be while the whole ring, with head room for the fighters, stays
// on screen. The low angle keeps the place's trees, hills and sky behind the fight and makes the Pokemon big.
// During the fight it only pans (at most 2 m) to keep the fighters, Red and every tell in frame, hides Canopy actors,
// clears the trees between it and the ring, and fades Occluder/Trunk meshes that block the view.
// In a cave (SetCave) it cuts away every ceiling and wall piece between itself and the ring, so it looks down through the
// rock without ever clipping into the cave: the pieces over the ring's ceiling height or outside the ring hide until
// Release. Stalagmite pillars inside the ring stay.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "PYRBattleCamera.generated.h"

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRBattleCamera : public ACameraActor
{
    GENERATED_BODY()
public:
    APYRBattleCamera();
    static constexpr float Fov = 60.f;
    static constexpr float MaxPan = 200.f;
    static constexpr float RingPitch = 30.f;
    static constexpr float GymPitch = 28.f;

    // Work out the final pose (recomputed only here). Radius in cm; Yaw looks from Red's side toward the foe.
    void Frame(const FVector& Center, float Radius, float Yaw, bool bGym);
    // Start the no-cut move from the current view, via a low pass near LowFocus, to the final pose.
    void BeginSweep(const FVector& FromLocation, const FRotator& FromRotation, float FromFov, const FVector& LowFocus,
        float LowSeconds = .4f, float RiseSeconds = .6f);
    void SnapToFinal();
    // Points to keep in frame this tick (fighters, Red, tells).
    void SetSubjects(const TArray<FVector>& Points) { Subjects = Points; }
    // Caves: the ceiling's height (world Z) and the ring the view must reach; bInCave false outside caves.
    void SetCave(bool bInCave, float CeilingZ, const FVector& RingCenter, float RingRadius);
    bool IsCave() const { return bCave; }
    int32 NumCutAway() const { return CutAway.Num(); }
    // Unhide canopy and occluders.
    void Release();
    virtual void Tick(float DeltaSeconds) override;

    // Project to normalised screen coordinates (-1..1 on both axes) with this camera's pose, FOV and aspect.
    bool Project(const FVector& Point, FVector2D& Out) const;
    bool IsSettled() const { return SweepTime >= LowTime + RiseTime; }
    float GetFinalDistance() const { return Distance; }
    float GetPitchDown() const { return PitchDown; }
    float Aspect = 16.f / 9.f;
    int32 NumHidden() const { return Hidden.Num(); }
    int32 NumClearedTrees() const { return ClearedTrees; }

private:
    FVector FinalLocation() const;
    void UpdateOccluders();
    void ClearTrees();
    int32 ClearedTrees = 0;
    float PanLimit = MaxPan;
    FVector Center = FVector::ZeroVector, Aim = FVector::ZeroVector, Pan = FVector::ZeroVector;
    float Radius = 1000.f, Yaw = 0, PitchDown = 55.f, Distance = 3000.f;
    FVector FromLoc = FVector::ZeroVector, LowLoc = FVector::ZeroVector;
    FRotator FromRot = FRotator::ZeroRotator, LowRot = FRotator::ZeroRotator;
    float FromFov = 90.f, SweepTime = 1000.f, LowTime = .4f, RiseTime = .6f, OccluderClock = 0;
    TArray<FVector> Subjects;
    TMap<TWeakObjectPtr<AActor>, double> Hidden;  // Actor -> last time it blocked the view.
    TArray<TWeakObjectPtr<AActor>> Canopy;
    void CutCave();
    bool bCave = false;
    float CaveCeilingZ = 0.f;
    FVector CaveCenter = FVector::ZeroVector;
    float CaveRadius = 0.f;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> CutAway;
};
