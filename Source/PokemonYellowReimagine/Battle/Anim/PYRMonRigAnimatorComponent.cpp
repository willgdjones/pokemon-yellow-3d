#include "PYRMonRigAnimatorComponent.h"
#include "../PYRBattleManager.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogRigAnimator, Log, All);

namespace
{
const TCHAR* Names[] = { TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("attack"), TEXT("hit"), TEXT("death"), TEXT("windup"), TEXT("none") };
constexpr int32 ClipCount = 6;  // Idle..Death have clips; Windup shows the attack's first pose.
}

UPYRMonRigAnimatorComponent::UPYRMonRigAnimatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;  // After movement: the velocity of this frame decides.
}

const TCHAR* UPYRMonRigAnimatorComponent::ClipName(EPYRMonRigClip Clip)
{
    return Names[FMath::Clamp(int32(Clip), 0, int32(EPYRMonRigClip::None))];
}

double UPYRMonRigAnimatorComponent::Now() const
{
    // m4t: the battle clock, so a paused fight holds its poses.
    return APYRBattleManager::BattleNow(this);
}

bool UPYRMonRigAnimatorComponent::FitPath(const FString& MeshPath, EPYRMonRigFamily InFamily, float InHeightCm)
{
    if (MeshPath.IsEmpty()) return false;
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Mesh)
    {
        UE_LOG(LogRigAnimator, Warning, TEXT("RIG_ANIMATOR_MISSING:%s (keeping the placeholder)"), *MeshPath);
        return false;
    }
    return Fit(Mesh, InFamily, InHeightCm);
}

bool UPYRMonRigAnimatorComponent::Fit(USkeletalMesh* Mesh, EPYRMonRigFamily InFamily, float InHeightCm)
{
    AActor* Owner = GetOwner();
    if (!Mesh || !Owner || !Owner->GetRootComponent() || InHeightCm <= 0) return false;
    // /Game/Characters/rat/SK_rat -> folder /Game/Characters/rat, id rat; clips are A_<id>_<clip> beside it.
    FString Folder, Name;
    Mesh->GetOutermost()->GetName().Split(TEXT("/"), &Folder, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    Name.RemoveFromStart(TEXT("SK_"));
    TArray<TObjectPtr<UAnimSequence>> Found;
    Found.SetNum(ClipCount);
    for (int32 I = 0; I < ClipCount; ++I)
    {
        if (InFamily == EPYRMonRigFamily::Creature && I != int32(EPYRMonRigClip::Walk)) continue;  // Walk-only rigs.
        const FString Asset = FString::Printf(TEXT("A_%s_%s"), *Name, Names[I]);
        UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Asset, *Asset),
            nullptr, LOAD_NoWarn | LOAD_Quiet);
        // A rig is enabled atomically: a missing, empty or foreign clip keeps the owner as it was.
        if (!Clip || Clip->GetSkeleton() != Mesh->GetSkeleton() || Clip->GetPlayLength() <= 0)
        {
            UE_LOG(LogRigAnimator, Warning, TEXT("RIG_ANIMATOR_FALLBACK:%s:%s"), *Mesh->GetPathName(), Names[I]);
            return false;
        }
        Clip->bForceRootLock = false;  // m2 rigs tip over when the root is locked to the reference pose.
        Found[I] = Clip;
    }
    Clips = MoveTemp(Found);
    Family = InFamily;
    HeightCm = InHeightCm;
    if (!Rig)
    {
        Rig = NewObject<USkeletalMeshComponent>(Owner, TEXT("RigAnimatorMesh"));
        Owner->AddInstanceComponent(Rig);
        Rig->SetupAttachment(Owner->GetRootComponent());
        Rig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        // Keep posing when nothing renders it (NullRHI tests, off-screen enemies).
        Rig->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Rig->RegisterComponent();
        Rig->AddTickPrerequisiteActor(Owner);
    }
    Rig->SetSkeletalMesh(Mesh);
    Rig->SetAnimationMode(EAnimationMode::AnimationSingleNode);

    // Uniform scale to the height, mesh centred, feet on the capsule bottom (or the owner's origin).
    ACharacter* Character = Cast<ACharacter>(Owner);
    if (Character && bFitCapsule)
    {
        const float Radius = FMath::Max(10.f, InHeightCm * CapsuleRadiusShare);
        Character->GetCapsuleComponent()->SetCapsuleSize(Radius, FMath::Max(Radius, InHeightCm * .5f));
    }
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const float Uniform = InHeightCm / FMath::Max(1.f, float(Bounds.BoxExtent.Z) * 2.f);
    const float Half = Character ? Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 0.f;
    Scale = FVector(Uniform);
    Base = FVector(-Bounds.Origin.X * Uniform, -Bounds.Origin.Y * Uniform,
        -Half - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Uniform);
    Rig->SetRelativeLocationAndRotation(Base, FRotator::ZeroRotator);
    Rig->SetRelativeScale3D(Scale);
    Rig->SetVisibility(true);
    if (bHidePlaceholders)
        for (UActorComponent* Component : Owner->GetComponents())
            if (auto* Placeholder = Cast<UStaticMeshComponent>(Component)) Placeholder->SetVisibility(false);

    State = EPYRMonRigClip::None;
    bDead = false; bWindup = false; DeathStart = -1; SwingUntil = HitUntil = SeenSwing = SeenHit = 0;
    Rig->PlayAnimation(Clips[int32(Family == EPYRMonRigFamily::Humanoid ? EPYRMonRigClip::Idle : EPYRMonRigClip::Walk)], true);
    UE_LOG(LogRigAnimator, Display, TEXT("RIG_ANIMATOR_FITTED:%s %s %.0f cm"), *Mesh->GetPathName(),
        Family == EPYRMonRigFamily::Humanoid ? TEXT("humanoid") : TEXT("creature"), InHeightCm);
    return true;
}

void UPYRMonRigAnimatorComponent::PlayAttack(float Seconds) { SwingUntil = Now() + FMath::Max(.05f, Seconds); }
void UPYRMonRigAnimatorComponent::PlayHit(float Seconds) { HitUntil = Now() + FMath::Max(.05f, Seconds); }
void UPYRMonRigAnimatorComponent::SetWindup(bool bWinding) { bWindup = bWinding; }
void UPYRMonRigAnimatorComponent::PlayDeath() { bDead = true; }

void UPYRMonRigAnimatorComponent::Revive()
{
    bDead = false; bWindup = false; DeathStart = -1; SwingUntil = HitUntil = 0;
    State = EPYRMonRigClip::None;  // Restart from idle (a creature restarts its walk).
    if (Rig) { Rig->SetVisibility(true); Rig->SetRelativeScale3D(Scale); }
}

UAnimSequence* UPYRMonRigAnimatorComponent::GetClip(EPYRMonRigClip Clip) const
{
    const int32 Index = int32(Clip == EPYRMonRigClip::Windup ? EPYRMonRigClip::Attack : Clip);
    return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
}

UAnimSequence* UPYRMonRigAnimatorComponent::GetPlayingClip() const
{
    UAnimSingleNodeInstance* Instance = Rig ? Rig->GetSingleNodeInstance() : nullptr;
    return Instance ? Cast<UAnimSequence>(Instance->GetCurrentAsset()) : nullptr;
}

float UPYRMonRigAnimatorComponent::GetPlayRate() const { return Rig ? Rig->GetPlayRate() : 0.f; }

float UPYRMonRigAnimatorComponent::RunThreshold() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    return Character && Character->GetCharacterMovement() ? Character->GetCharacterMovement()->MaxWalkSpeed * RunFraction : RunSpeed;
}

void UPYRMonRigAnimatorComponent::Evaluate(float Speed)
{
    if (!Rig || Clips.Num() != ClipCount) return;
    const double T = Now();
    // Priority: death, hit, attack, wind-up, locomotion, idle.
    EPYRMonRigClip Desired = EPYRMonRigClip::Idle;
    if (bDead) Desired = EPYRMonRigClip::Death;
    else if (T < HitUntil) Desired = EPYRMonRigClip::Hit;
    else if (T < SwingUntil) Desired = EPYRMonRigClip::Attack;
    else if (bWindup) Desired = EPYRMonRigClip::Windup;
    else if (Speed > MoveThreshold) Desired = Speed > RunThreshold() ? EPYRMonRigClip::Run : EPYRMonRigClip::Walk;
    const bool bCreature = Family == EPYRMonRigFamily::Creature;
    const EPYRMonRigClip Index = bCreature ? EPYRMonRigClip::Walk : (Desired == EPYRMonRigClip::Windup ? EPYRMonRigClip::Attack : Desired);
    UAnimSequence* Clip = Clips[int32(Index)];
    const bool bNewImpact = Desired == EPYRMonRigClip::Hit && HitUntil != SeenHit;
    const bool bNewSwing = Desired == EPYRMonRigClip::Attack && SwingUntil != SeenSwing;
    const bool bStart = State == EPYRMonRigClip::None;
    SeenHit = HitUntil; SeenSwing = SwingUntil;
    if (Desired != State || bNewImpact || bNewSwing)
    {
        State = Desired;
        Rate = 1.f;
        const float Length = Clip->GetPlayLength();
        // Attack and hit fit the remaining gameplay window; death fits at most DeathMaxSeconds.
        if (!bCreature && Desired == EPYRMonRigClip::Attack) Rate = Length / FMath::Max(.18f, float(SwingUntil - T));
        else if (!bCreature && Desired == EPYRMonRigClip::Hit) Rate = Length / FMath::Max(.01f, float(HitUntil - T));
        else if (!bCreature && Desired == EPYRMonRigClip::Death) Rate = Length / FMath::Min(DeathMaxSeconds, Length);
        // Restart only on a state change: looping clips keep their phase across ticks.
        if (!bCreature || bStart) Rig->PlayAnimation(Clip, bCreature || Desired <= EPYRMonRigClip::Run);
    }
    Rig->SetPlayRate(Desired == EPYRMonRigClip::Windup || (bCreature && (Speed <= MoveThreshold || Desired == EPYRMonRigClip::Death)) ? 0.f : Rate);

    FVector Size = Scale;
    if (Desired == EPYRMonRigClip::Death)
    {
        if (DeathStart < 0) DeathStart = T;
        const float Age = float(T - DeathStart);
        if (bCreature)  // Pop, then shrink away (the walk-only rig has no death clip).
        {
            const float Shrink = FMath::Max(.01f, CreatureDeathSeconds - .08f);
            Size *= Age < .08f ? 1.f + .25f * Age / .08f : FMath::Max(.001f, 1.25f * FMath::Square(1.f - FMath::Min(1.f, (Age - .08f) / Shrink)));
        }
        const float End = bCreature ? CreatureDeathSeconds : FMath::Min(DeathMaxSeconds, Clip->GetPlayLength());
        if (bHideAfterDeath && Age >= End && Rig->IsVisible()) Rig->SetVisibility(false);
    }
    if (!Rig->GetRelativeScale3D().Equals(Size)) Rig->SetRelativeScale3D(Size);
    if (!Rig->GetRelativeLocation().Equals(Base)) Rig->SetRelativeLocation(Base);
}

void UPYRMonRigAnimatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (const AActor* Owner = GetOwner()) Evaluate(Owner->GetVelocity().Size2D());
}
