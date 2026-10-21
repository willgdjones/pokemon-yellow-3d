// Rig animator (a reusable library piece), renamed UPYRMonRigAnimator* for the battle. Replace POKEMONYELLOWREIMAGINE_API with your module's API macro.
//
// Puts a skeletal mesh imported by the asset importer (SK_<id> with A_<id>_<clip> clips beside it) on its owner and picks
// the clip from state every tick: idle, walk or run from speed; attack, hit and death on request; a held wind-up.
// Creatures (walk-only rigs) play walk while moving and pause it when still. Single-node playback, no Animation
// Blueprint, and it keeps ticking under -NullRHI. Adapted from an earlier character template (FitRig, RigPose).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PYRMonRigAnimatorComponent.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EPYRMonRigFamily : uint8
{
    Humanoid,  // Needs idle, walk, run, attack, hit and death.
    Creature,  // Needs walk only (Tripo quadruped, hexapod, ...): other states are shown by the body transform.
};

UENUM(BlueprintType)
enum class EPYRMonRigClip : uint8 { Idle, Walk, Run, Attack, Hit, Death, Windup, None };

UCLASS(ClassGroup = (Library), meta = (BlueprintSpawnableComponent))
class POKEMONYELLOWREIMAGINE_API UPYRMonRigAnimatorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPYRMonRigAnimatorComponent();

    // ---- Config
    // Visual height in cm; the mesh is scaled uniformly to it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float HeightCm = 170.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") EPYRMonRigFamily Family = EPYRMonRigFamily::Humanoid;
    // Below this ground speed (cm/s) the character is standing.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float MoveThreshold = 5.f;
    // Walk becomes run above this share of the owner's MaxWalkSpeed (or RunSpeed when the owner has no
    // character movement).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float RunFraction = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float RunSpeed = 400.f;
    // Humanoid death plays in at most this many seconds; creatures pop and shrink over CreatureDeathSeconds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float DeathMaxSeconds = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float CreatureDeathSeconds = .35f;
    // Hide the rig when the death clip ends (enemies); keep the last pose otherwise.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") bool bHideAfterDeath = true;
    // Resize an ACharacter owner's capsule to the fitted height (radius = HeightCm * CapsuleRadiusShare).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") bool bFitCapsule = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") float CapsuleRadiusShare = .28f;

    // ---- Setup
    // Load /Game/.../SK_<id>.SK_<id> and its clips; false (and nothing changed) when the mesh or a required clip
    // is missing, has no length or uses another skeleton. Hides the owner's static placeholder meshes when
    // bHidePlaceholders.
    UFUNCTION(BlueprintCallable, Category = "Rig") bool FitPath(const FString& MeshPath, EPYRMonRigFamily InFamily, float InHeightCm);
    bool Fit(USkeletalMesh* Mesh, EPYRMonRigFamily InFamily, float InHeightCm);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig") bool bHidePlaceholders = true;

    // ---- Requests (gameplay keeps its own clocks; these only say what to show)
    // Attack over the next Seconds (the clip is sped up or slowed to fit); a new request restarts it.
    UFUNCTION(BlueprintCallable, Category = "Rig") void PlayAttack(float Seconds);
    UFUNCTION(BlueprintCallable, Category = "Rig") void PlayHit(float Seconds);
    // Hold the attack's first pose (humanoids) or freeze the stride (creatures) until cleared.
    UFUNCTION(BlueprintCallable, Category = "Rig") void SetWindup(bool bWinding);
    UFUNCTION(BlueprintCallable, Category = "Rig") void PlayDeath();
    UFUNCTION(BlueprintCallable, Category = "Rig") void Revive();

    // ---- State
    UFUNCTION(BlueprintPure, Category = "Rig") bool IsFitted() const { return Rig != nullptr; }
    UFUNCTION(BlueprintPure, Category = "Rig") EPYRMonRigClip GetState() const { return State; }
    UFUNCTION(BlueprintPure, Category = "Rig") USkeletalMeshComponent* GetRig() const { return Rig; }
    UAnimSequence* GetClip(EPYRMonRigClip Clip) const;
    UAnimSequence* GetPlayingClip() const;
    float GetPlayRate() const;
    // Where the rig was put: feet on the capsule bottom (or the owner's origin), mesh centred.
    FVector GetBaseLocation() const { return Base; }
    FVector GetBaseScale() const { return Scale; }
    // Decide and apply the clip for a ground speed at the world's current time. Tick calls it with the owner's
    // velocity; tests call it directly for deterministic speeds.
    void Evaluate(float Speed);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    static const TCHAR* ClipName(EPYRMonRigClip Clip);

private:
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Rig;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Clips;  // Indexed by EPYRMonRigClip (Idle..Death).
    FVector Base = FVector::ZeroVector, Scale = FVector::OneVector;
    EPYRMonRigClip State = EPYRMonRigClip::None;
    float Rate = 1.f;
    double SwingUntil = 0, HitUntil = 0, SeenSwing = 0, SeenHit = 0, DeathStart = -1;
    bool bWindup = false, bDead = false;
    double Now() const;
    float RunThreshold() const;
};
