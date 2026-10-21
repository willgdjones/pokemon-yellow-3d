// The player controller (m4).
// - Keys go to the Game Boy screens first (UPYRMenus::HandleKey): while a menu or the text box is up, nothing else
//   hears them. With nothing open, Enter or Esc opens START (out of battle; in battle it pauses the fight too).
// - The mouse is captured while walking and in battle (CapturePermanently, hidden cursor, GameOnly input) so the camera
//   never stops turning at the window's edge. It is released only while a menu is open, and captured again when the
//   menu closes, when the window gets focus back and after an alt-tab.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PYRPlayerController.generated.h"

UCLASS()
class POKEMONYELLOWREIMAGINE_API APYRPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void PlayerTick(float DeltaTime) override;

    // What Enter/Esc does with nothing open.
    void StartPressed();
    int32 StartPresses = 0;
    // Menus call this as they open and close.
    void SetMenuMouse(bool bMenuOpen);
    // Capture the mouse for play (the state tests check).
    void CaptureMouse();
    bool IsMouseCaptured() const { return bCaptured; }
    int32 Captures = 0, Releases = 0;
    // m4t: the battle HUD calls this as a fight starts and ends: the cursor is free in battle (RUN, the switch menu).
    void SetBattleMouse(bool bInBattle);

private:
    void FreeMouse();
    // m4t: keys for the battle's switch menu while it is open; true when the key was used up.
    bool SwitchMenuKey(const FKey& Key);
    bool bCaptured = false, bMenuMouse = false, bBattleMouse = false;
    FDelegateHandle ActivationHandle;
    void OnActivationChanged(bool bActive);
    bool bNeedsRecapture = false;
};
