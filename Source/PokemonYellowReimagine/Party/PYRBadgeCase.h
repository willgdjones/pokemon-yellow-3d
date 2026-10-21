// The badge case: Red's eight Kanto badges in a leather case, earned ones in colour and the rest as empty slots.
// Open it with B anywhere (also from the party wheel in battle, which shows the hint); worker `core`'s pause menu calls
// PYRShowBadgeCase. B or Esc closes it.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PYRBadgeCase.generated.h"

class APlayerController;

// Opens the badge case for PC (the seam for the pause menu). Does nothing if it is already open.
POKEMONYELLOWREIMAGINE_API void PYRShowBadgeCase(APlayerController* PC);

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRBadgeCase : public UObject
{
    GENERATED_BODY()
public:
    static void Open(APlayerController* PC);
    static void Toggle(APlayerController* PC);
    static bool IsOpen();
    static void Close();
    // What the open case shows: one entry per badge in Yellow's order (Boulder .. Earth), true when lit. Empty when
    // the case is closed.
    static TArray<bool> ShownSlots();
    // The slot labels as shown ("Boulder: earned", "Thunder: empty"...), for tests and logs.
    static FString Describe();
};
