// M4: the story scripts behind UPYRScripts (Kanto/PYRScripts.h): who is shown, what each special person does, the
// events of events.json and the trigger tiles. Every line is a text.json label or a pokeyellow system line.
#pragma once

#include "CoreMinimal.h"
#include "Yellow/YellowData.h"

class UPYRScripts;
class APYRPerson;
struct FPYRFight;

namespace PYRStory
{
    void Refresh(UPYRScripts* S);
    bool Talk(UPYRScripts* S, APYRPerson* Person);
    bool OnTile(UPYRScripts* S, FName Map, FIntPoint Local);
    void OnMap(UPYRScripts* S, FName From, FName To);
    bool RunEvent(UPYRScripts* S, const FString& Id, APYRPerson* Talker);
    bool UseTile(UPYRScripts* S, FIntPoint Kanto);
    // A trainer class's fight: Yellow's party (with special moves), the prize, the kind and the battle theme.
    void FillFight(const UPYRScripts* S, FName Class, const TArray<FYSlot>& Party, FPYRFight& Out);
    // After a trainer is beaten (gym leaders' badges and TMs, the rival walking off).
    void AfterTrainer(UPYRScripts* S, APYRPerson* Trainer);
}
