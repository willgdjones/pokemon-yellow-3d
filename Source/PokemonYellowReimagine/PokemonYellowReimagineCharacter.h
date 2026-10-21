#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PokemonYellowReimagineCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UFollowCameraComponent;

// Red: walks freely and analog over the Game Boy's tiles (4.5 m/s, 6.5 m/s with Shift), crouches with Ctrl or C (half
// speed, capsule and outline 40% lower), and talks, reads and takes with E (UPYRField::Interact). T talks to Pikachu.
// Ledges are one-way as on the Game Boy: walking into a ledge tile in its hop direction hops him over it to the tile
// beyond (a short arc); from any other side the ledge is a wall. START, the menus and the text box belong to the player
// controller (Menu/PYRPlayerController), so they work in battle too. Keys are bound in code; tests call the handlers.
UCLASS()
class POKEMONYELLOWREIMAGINE_API APokemonYellowReimagineCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    APokemonYellowReimagineCharacter();
    static APokemonYellowReimagineCharacter* Find(const UObject* Ctx);
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

    static constexpr float WalkSpeed = 450.f;
    static constexpr float RunSpeed = 650.f;

    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UFollowCameraComponent> FollowCamera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USkeletalMeshComponent> Rigged;

    // ---- Key handlers (tests call these)
    void DoInteract();
    void TalkToPikachu();
    void SetRun(bool bRun);
    void SetCrouch(bool bCrouch);
    bool IsRunning() const { return bRunning && GetGroundSpeed() > WalkSpeed + 10.f; }
    bool IsCrouching() const { return bIsCrouched; }
    float GetGroundSpeed() const;
    // Between leaving a ledge and landing beyond it.
    bool IsHopping() const { return bHopping; }
    int32 GetHops() const { return Hops; }
    // Tests: walk as if the stick pointed this way (world direction; zero stops).
    FVector TestInput = FVector::ZeroVector;

    // ---- Story control
    void LockInput(bool bLock);
    bool IsInputLocked() const { return bInputLocked; }
    void ScriptWalkTo(const FVector& Target) { ScriptTarget = Target; bScriptWalk = true; ScriptPath.Reset(); }
    void ScriptWalkPath(const TArray<FVector>& Points);
    bool IsScriptWalking() const { return bScriptWalk; }
    void StopScriptWalk() { bScriptWalk = false; ScriptPath.Reset(); }
    // Place on the ground facing Yaw; the camera turns that way too.
    void PlaceAt(const FVector& Ground, float Yaw);

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    void RunPressed() { SetRun(true); }
    void RunReleased() { SetRun(false); }
    void CrouchPressed() { SetCrouch(!bIsCrouched); }
    void BuildBody();
    void UpdateLedgeHop(float DeltaTime);
    void UpdateClip();
    void UpdateRoomCamera();
    // While someone talks to Red face to face, the camera turns a little to one side so the two stand apart in the
    // picture, and turns back when the talk ends.
    void UpdateTalkCamera(float DeltaTime);
    bool bTalkTurn = false;
    float TalkYawFrom = 0.f, TalkYawWant = 0.f, TalkYawSet = 0.f;
    bool bTalkYawSet = false;

    bool bRunning = false, bInputLocked = false, bScriptWalk = false, bPainted = false;
    FVector ScriptTarget = FVector::ZeroVector;
    TArray<FVector> ScriptPath;
    float StepPhase = 0.f;
    float VisualBaseZ = 0.f;
    bool bHopping = false;
    int32 Hops = 0;
    FVector HopFrom = FVector::ZeroVector, HopTo = FVector::ZeroVector;
    float HopT = 0.f;
    float LedgePush = 0.f;
    FName Clip;
    UPROPERTY() TMap<FName, TObjectPtr<class UAnimSequence>> Clips;
};
