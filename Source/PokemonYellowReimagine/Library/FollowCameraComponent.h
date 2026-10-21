// Follow camera (a reusable library piece). Replace POKEMONYELLOWREIMAGINE_API with your module's API macro.
//
// Camera options for a character with a spring arm and a camera (the starter's Boom and Camera; created when
// missing): a third-person follow camera that the mouse turns, and two fixed high-angle cameras (top-down and
// isometric) that ignore the mouse and follow with a little lag. Adapted from an earlier camera template (ConfigureCamera).
// In the fixed modes WASD must move relative to the camera, not the controller: use GetMoveDirection().
// pokemon-yellow-reimagine m3: SetConfine(Box) keeps the third-person camera inside a box (the room Red is in): the arm
// is shortened each frame, before the spring arm updates, so the camera never leaves the room or sees through a wall.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FollowCameraComponent.generated.h"

class UCameraComponent;
class USpringArmComponent;

UENUM(BlueprintType)
enum class EFollowCameraMode : uint8 { ThirdPerson, TopDown, Isometric };

UCLASS(ClassGroup = (Library), meta = (BlueprintSpawnableComponent))
class POKEMONYELLOWREIMAGINE_API UFollowCameraComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFollowCameraComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") EFollowCameraMode Mode = EFollowCameraMode::ThirdPerson;
    // Third person: the arm turns with the controller (mouse look).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float FollowArmLength = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FVector FollowSocketOffset = FVector(0, 0, 60);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float FollowFov = 90.f;
    // Top-down: straight down from high up.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float TopDownPitch = -89.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float TopDownArmLength = 1400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float TopDownFov = 70.f;
    // Isometric: 55 degrees down, 45 degrees round, long lens.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float IsometricPitch = -55.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float IsometricYaw = 45.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float IsometricArmLength = 1900.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float IsometricFov = 50.f;
    // Fixed cameras follow with lag and never collide with walls (a wall must not pull the camera into the player).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float FixedLagSpeed = 12.f;
    // Fixed cameras: true turns the body to the controller yaw (aim with the mouse or stick), false faces the
    // movement direction.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") bool bFixedAimWithController = false;
    // Apply the mode at BeginPlay.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") bool bApplyOnBeginPlay = true;

    UFUNCTION(BlueprintCallable, Category = "Camera") void SetMode(EFollowCameraMode NewMode);
    UFUNCTION(BlueprintCallable, Category = "Camera") void CycleMode();
    UFUNCTION(BlueprintCallable, Category = "Camera") void Apply();
    UFUNCTION(BlueprintPure, Category = "Camera") bool IsFixed() const { return Mode != EFollowCameraMode::ThirdPerson; }
    // Yaw that "forward" means for movement: the camera's yaw in fixed modes, the controller's otherwise.
    UFUNCTION(BlueprintPure, Category = "Camera") float GetMoveYaw() const;
    // World direction for a move axis (EAxis::X forward, EAxis::Y right), flat.
    FVector GetMoveDirection(EAxis::Type Axis) const;

    // Keep the camera inside Box (world space) until ClearConfine; the arm grows back smoothly afterwards.
    void SetConfine(const FBox& Box) { Confine = Box; bConfined = true; }
    void ClearConfine() { bConfined = false; }
    bool IsConfined() const { return bConfined; }
    const FBox& GetConfine() const { return Confine; }
    // Where the camera goes for an arm of Length this frame (as the spring arm computes it, before its collision test).
    FVector CameraFor(float Length) const;
    FVector CameraFor(float Length, const FRotator& Rotation) const;
    // pokemon-yellow-reimagine m4q: something the confined camera must not look through (a cave wall, a tall shelf)
    // between the arm's origin and a camera point. With it, a confined camera that would end up pressed against the
    // player tilts down (to MinPitch at most) until it has room, and gives the player's own pitch back afterwards.
    TFunction<bool(const FVector& From, const FVector& To)> ViewBlocked;
    float MinPitch = -80.f;
    float GetUserPitch() const { return UserPitch; }
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    USpringArmComponent* GetArm() const { return Arm; }
    UCameraComponent* GetCamera() const { return Camera; }

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(Transient) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(Transient) TObjectPtr<UCameraComponent> Camera;
    void FindOrCreate();
    FBox Confine = FBox(ForceInit);
    bool bConfined = false;
    float UserPitch = 1000.f, SetPitch = 1000.f;   // the player's pitch and the one last applied (1000: none yet)
    float Fit(float Want, const FRotator& Rotation) const;
};
