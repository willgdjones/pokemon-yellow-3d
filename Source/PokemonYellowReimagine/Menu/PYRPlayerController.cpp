#include "Menu/PYRPlayerController.h"

#include "Battle/PYRBattleManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Stage/PYREncounters.h"

namespace
{
// QA hook, as UPYRMenus's (off unless asked): -QAMenuLog or PYR_QA_MENU_LOG=1 logs each key the controller
// gets and why START was refused, so the stream check can tell a key that never arrived from one the game turned down.
bool QAKeyLog()
{
    static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("QAMenuLog")) || FPlatformMisc::GetEnvironmentVariable(TEXT("PYR_QA_MENU_LOG")) == TEXT("1");
    return bOn;
}
}

void APYRPlayerController::BeginPlay()
{
    Super::BeginPlay();
    CaptureMouse();
    if (FSlateApplication::IsInitialized())
        ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &APYRPlayerController::OnActivationChanged);
}

void APYRPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (FSlateApplication::IsInitialized() && ActivationHandle.IsValid()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    Super::EndPlay(Reason);
}

void APYRPlayerController::OnActivationChanged(bool bActive)
{
    // Alt-tab back in: capture again (unless a menu wants the cursor).
    if (bActive) bNeedsRecapture = true;
}

void APYRPlayerController::CaptureMouse()
{
    bShowMouseCursor = false;
    FInputModeGameOnly Mode;
    Mode.SetConsumeCaptureMouseDown(false);
    SetInputMode(Mode);
    if (ULocalPlayer* LP = GetLocalPlayer())
        if (UGameViewportClient* VC = LP->ViewportClient)
        {
            VC->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently);
            VC->SetMouseLockMode(EMouseLockMode::LockAlways);
            VC->SetHideCursorDuringCapture(true);
        }
    if (FSlateApplication::IsInitialized())
        if (UGameViewportClient* VC = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr)
            if (TSharedPtr<SViewport> Widget = VC->GetGameViewportWidget()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    bCaptured = true;
    ++Captures;
}

void APYRPlayerController::SetMenuMouse(bool bMenuOpen)
{
    bMenuMouse = bMenuOpen;
    if (!bMenuMouse && !bBattleMouse) { CaptureMouse(); return; }
    FreeMouse();
}

void APYRPlayerController::SetBattleMouse(bool bInBattle)
{
    if (bBattleMouse == bInBattle) return;
    bBattleMouse = bInBattle;
    if (!bMenuMouse && !bBattleMouse) CaptureMouse();
    else FreeMouse();
}

void APYRPlayerController::FreeMouse()
{
    // A menu is open, or a fight is on (its camera never turns, and RUN and the switch menu can be clicked): free the
    // cursor; the keyboard still drives everything.
    bShowMouseCursor = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    if (ULocalPlayer* LP = GetLocalPlayer())
        if (UGameViewportClient* VC = LP->ViewportClient)
        {
            VC->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
            VC->SetMouseLockMode(EMouseLockMode::DoNotLock);
        }
    bCaptured = false;
    ++Releases;
}

void APYRPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (bNeedsRecapture && !bMenuMouse && !bBattleMouse) { bNeedsRecapture = false; CaptureMouse(); }
}

bool APYRPlayerController::SwitchMenuKey(const FKey& Key)
{
    // m4t: while the battle's switch menu is open (and paused) it has the keys. Arrows move its cursor (and still reach
    // the game, so a held Q released with a direction keeps the m2 flick); E, Enter, Space or the pad's A send out the
    // Pokemon under the cursor; 1-6 send one directly; Q, Tab, Esc, Backspace or the pad's B cancel (not a forced one).
    // Every other key is held back: nothing in the fight moves under the menu.
    APYRBattleManager* BM = APYRBattleManager::Find(this);
    if (!BM || !BM->IsBattleActive() || !BM->IsWheelOpen() || Key.IsMouseButton()) return false;
    if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Left)
    {
        BM->MoveWheelCursor(-1);
        return false;
    }
    if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Right)
    {
        BM->MoveWheelCursor(1);
        return false;
    }
    static const FKey Digits[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
    for (int32 I = 0; I < 6; ++I) if (Key == Digits[I]) { BM->PressSwap(I); return true; }
    if (Key == EKeys::E || Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom) { BM->ConfirmWheel(); return true; }
    if (Key == EKeys::Q || Key == EKeys::Tab || Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right
        || Key == EKeys::Gamepad_LeftShoulder)
    {
        if (!BM->IsWheelHeld()) BM->CancelWheel();
        return true;
    }
    return true;
}

void APYRPlayerController::StartPressed()
{
    ++StartPresses;
    UPYRField* Field = UPYRField::Get(this);
    const UPYREncounters* Enc = UPYREncounters::Get(this);
    // Not while Red is being walked, warped or taken to a fight.
    if (Field && (Field->IsWarping() || Field->IsLockedBy(TEXT("script"))))
    {
        if (QAKeyLog()) UE_LOG(LogTemp, Display, TEXT("QA_KEY START refused: %s"), Field->IsWarping() ? TEXT("warping") : TEXT("a script holds Red"));
        return;
    }
    if (Enc && Enc->IsBusy() && Enc->GetPhase() != EPYREncounterPhase::Fight)
    {
        if (QAKeyLog()) UE_LOG(LogTemp, Display, TEXT("QA_KEY START refused: an encounter is starting"));
        return;
    }
    PYRScreens::OpenStart(this);
}

bool APYRPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (QAKeyLog() && (Params.Event == IE_Pressed || Params.Event == IE_Released) && !Params.Key.IsMouseButton())
    {
        const UPYRMenus* M = UPYRMenus::Get(this);
        const UPYRField* F = UPYRField::Get(this);
        UE_LOG(LogTemp, Display, TEXT("QA_KEY %s %s menus=%d locks=%s"), *Params.Key.ToString(), Params.Event == IE_Pressed ? TEXT("down") : TEXT("up"),
            M && M->AnyOpen() ? 1 : 0, F ? *F->LockText() : TEXT("-"));
    }
    if (Params.Event == IE_Pressed)
    {
        UPYRMenus* Menus = UPYRMenus::Get(this);
        if (Menus && Menus->AnyOpen())
        {
            if (Menus->HandleKey(Params.Key)) return true;
        }
        else if (SwitchMenuKey(Params.Key)) return true;
        else if (Params.Key == EKeys::Enter || Params.Key == EKeys::Escape || Params.Key == EKeys::Gamepad_Special_Right)
        {
            StartPressed();
            return true;
        }
    }
    // Releases always reach the game, or a key held while a menu opened would stay held after it closed.
    return Super::InputKey(Params);
}
