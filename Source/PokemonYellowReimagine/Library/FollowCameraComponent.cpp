#include "FollowCameraComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"

UFollowCameraComponent::UFollowCameraComponent()
{
    // Ticks during physics, before the spring arm (post physics), so a confined arm length applies the same frame.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_DuringPhysics;
}

FVector UFollowCameraComponent::CameraFor(float Length) const
{
    if (!Arm) return FVector::ZeroVector;
    const APawn* Pawn = Cast<APawn>(GetOwner());
    const FRotator Rot = Arm->bUsePawnControlRotation && Pawn && Pawn->GetController() ? Pawn->GetController()->GetControlRotation() : Arm->GetComponentRotation();
    return CameraFor(Length, Rot);
}

FVector UFollowCameraComponent::CameraFor(float Length, const FRotator& Rot) const
{
    if (!Arm) return FVector::ZeroVector;
    const FVector Origin = Arm->GetComponentLocation() + Arm->TargetOffset;
    return Origin - Rot.Vector() * Length + FRotationMatrix(Rot).TransformVector(Arm->SocketOffset);
}

float UFollowCameraComponent::Fit(float Want, const FRotator& Rot) const
{
    // The longest arm (up to Want) whose camera is inside the box and sees the player.
    const FVector Origin = Arm->GetComponentLocation() + Arm->TargetOffset;
    const bool bBox = bConfined && Confine.IsValid;
    auto Ok = [&](float L)
    {
        const FVector C = CameraFor(L, Rot);
        return (!bBox || Confine.IsInsideOrOn(C)) && !(ViewBlocked && ViewBlocked(Origin, C));
    };
    if (Ok(Want)) return Want;
    float Lo = 0.f, Hi = Want;
    for (int32 i = 0; i < 14; ++i)
    {
        const float Mid = (Lo + Hi) * .5f;
        (Ok(Mid) ? Lo : Hi) = Mid;
    }
    return Lo;
}

void UFollowCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Arm || Mode != EFollowCameraMode::ThirdPerson) return;
    float Want = FollowArmLength;
    APawn* Pawn = Cast<APawn>(GetOwner());
    AController* Ctrl = Pawn ? Pawn->GetController() : nullptr;
    // Confined, the room's own fit decides the arm (props and people must not pull the camera into the player).
    Arm->bDoCollisionTest = !(bConfined && Confine.IsValid);
    if (bConfined && Confine.IsValid)
    {
        if (!Ctrl || !Arm->bUsePawnControlRotation)
        {
            Arm->TargetArmLength = Fit(Want, Arm->GetComponentRotation());   // Never outside, even for a frame.
            return;
        }
        // The player's pitch, followed through any mouse moves since the last tilt.
        FRotator Rot = Ctrl->GetControlRotation();
        const float Pitch = FRotator::NormalizeAxis(Rot.Pitch);
        if (UserPitch > 900.f) UserPitch = Pitch;
        else if (FMath::Abs(Pitch - SetPitch) > .01f) UserPitch = FMath::Clamp(Pitch, -85.f, 20.f);   // the mouse (or a script) set a new pitch
        // Tilt down in 5-degree steps until the camera has most of its arm (a wall behind the player).
        float Use = UserPitch, Length = Fit(Want, FRotator(UserPitch, Rot.Yaw, 0));
        for (float P = UserPitch - 5.f; Length < Want * .7f && P >= MinPitch; P -= 5.f)
        {
            const float L = Fit(Want, FRotator(P, Rot.Yaw, 0));
            if (L > Length) { Length = L; Use = P; }
        }
        if (FMath::Abs(Use - Pitch) > .01f) { Rot.Pitch = Use; Ctrl->SetControlRotation(Rot); }
        SetPitch = Use;
        Arm->TargetArmLength = Length;
        return;
    }
    // Outdoors: someone standing between the camera and the player (Oak walking up behind him): tilt down over them,
    // and back to the player's own pitch once the view is clear.
    if (Ctrl && Arm->bUsePawnControlRotation && ViewBlocked)
    {
        FRotator Rot = Ctrl->GetControlRotation();
        const float Pitch = FRotator::NormalizeAxis(Rot.Pitch);
        if (UserPitch > 900.f) UserPitch = Pitch;
        else if (FMath::Abs(Pitch - SetPitch) > .01f) UserPitch = FMath::Clamp(Pitch, -85.f, 20.f);   // the mouse (or a script) set a new pitch
        float Use = UserPitch;
        if (Fit(Want, FRotator(UserPitch, Rot.Yaw, 0)) < Want * .9f)
            for (float P = UserPitch - 5.f; P >= -70.f; P -= 5.f)
            {
                Use = P;
                if (Fit(Want, FRotator(P, Rot.Yaw, 0)) >= Want * .9f) break;
            }
        // Ease the tilt (people move; the view shouldn't jump).
        const float Eased = FMath::FInterpTo(Pitch, Use, DeltaTime, 6.f);
        if (FMath::Abs(Eased - Pitch) > .01f) { Rot.Pitch = Eased; Ctrl->SetControlRotation(Rot); }
        SetPitch = Eased;
        Arm->TargetArmLength = FMath::FInterpTo(Arm->TargetArmLength, Want, DeltaTime, 5.f);
        return;
    }
    // Out of the room: the player's own pitch back.
    if (Ctrl && UserPitch <= 900.f)
    {
        FRotator Rot = Ctrl->GetControlRotation();
        const float Pitch = FRotator::NormalizeAxis(Rot.Pitch);
        if (FMath::Abs(Pitch - SetPitch) <= .01f && FMath::Abs(Pitch - UserPitch) > .01f) { Rot.Pitch = UserPitch; Ctrl->SetControlRotation(Rot); }
        UserPitch = SetPitch = 1000.f;
    }
    Arm->TargetArmLength = FMath::FInterpTo(Arm->TargetArmLength, Want, DeltaTime, 5.f);
}

void UFollowCameraComponent::BeginPlay()
{
    Super::BeginPlay();
    if (bApplyOnBeginPlay) Apply();
}

void UFollowCameraComponent::FindOrCreate()
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->GetRootComponent()) return;
    if (!Arm) Arm = Owner->FindComponentByClass<USpringArmComponent>();
    if (!Arm)
    {
        Arm = NewObject<USpringArmComponent>(Owner, TEXT("FollowCameraArm"));
        Owner->AddInstanceComponent(Arm);
        Arm->SetupAttachment(Owner->GetRootComponent());
        Arm->RegisterComponent();
    }
    if (!Camera) Camera = Owner->FindComponentByClass<UCameraComponent>();
    if (!Camera)
    {
        Camera = NewObject<UCameraComponent>(Owner, TEXT("FollowCamera"));
        Owner->AddInstanceComponent(Camera);
        Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
        Camera->RegisterComponent();
    }
}

void UFollowCameraComponent::SetMode(EFollowCameraMode NewMode)
{
    Mode = NewMode;
    Apply();
}

void UFollowCameraComponent::CycleMode()
{
    SetMode(EFollowCameraMode((uint8(Mode) + 1) % 3));
}

void UFollowCameraComponent::Apply()
{
    FindOrCreate();
    if (!Arm || !Camera) return;
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    if (Mode == EFollowCameraMode::ThirdPerson)
    {
        Arm->SetUsingAbsoluteRotation(false);
        Arm->bUsePawnControlRotation = true;
        Arm->TargetArmLength = FollowArmLength;
        Arm->SocketOffset = FollowSocketOffset;
        Arm->TargetOffset = FVector::ZeroVector;
        Arm->bDoCollisionTest = true;
        Arm->bEnableCameraLag = false;
        Camera->SetFieldOfView(FollowFov);
        if (Character) Character->bUseControllerRotationYaw = false;
        if (Movement) { Movement->bOrientRotationToMovement = true; Movement->bUseControllerDesiredRotation = false; }
        return;
    }
    const bool bIso = Mode == EFollowCameraMode::Isometric;
    // Fixed yaw: the arm ignores the controller, which is then free to carry the aim.
    Arm->bUsePawnControlRotation = false;
    Arm->SetUsingAbsoluteRotation(true);
    Arm->SetWorldRotation(bIso ? FRotator(IsometricPitch, IsometricYaw, 0) : FRotator(TopDownPitch, 0, 0));
    Arm->TargetArmLength = bIso ? IsometricArmLength : TopDownArmLength;
    Arm->SocketOffset = FVector::ZeroVector;
    Arm->TargetOffset = FVector::ZeroVector;
    Arm->bDoCollisionTest = false;
    Arm->bEnableCameraLag = true;
    Arm->CameraLagSpeed = FixedLagSpeed;
    Camera->SetFieldOfView(bIso ? IsometricFov : TopDownFov);
    if (Character) Character->bUseControllerRotationYaw = false;
    if (Movement)
    {
        Movement->bOrientRotationToMovement = !bFixedAimWithController;
        Movement->bUseControllerDesiredRotation = bFixedAimWithController;
    }
}

float UFollowCameraComponent::GetMoveYaw() const
{
    if (IsFixed() && Arm) return Arm->GetComponentRotation().Yaw;
    const APawn* Pawn = Cast<APawn>(GetOwner());
    if (Pawn && Pawn->GetController()) return Pawn->GetController()->GetControlRotation().Yaw;
    return GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.f;
}

FVector UFollowCameraComponent::GetMoveDirection(EAxis::Type Axis) const
{
    return FRotationMatrix(FRotator(0, GetMoveYaw(), 0)).GetUnitAxis(Axis);
}
