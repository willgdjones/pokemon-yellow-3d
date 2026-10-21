// M4: YellowData's pictures (minimap PNGs, the Town Map, Red's minimap sprite, Pikachu's faces) loaded from disk with
// IImageWrapper into transient textures with nearest filtering, cached per game instance. A missing file is logged
// as "YellowData missing: <path>" once and gives null.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "YellowImages.generated.h"

class UTexture2D;

UCLASS()
class POKEMONYELLOWREIMAGINE_API UYellowImages : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static UYellowImages* Get(const UObject* Ctx);
    // A picture by path relative to the YellowData folder ("minimap/PALLET_TOWN.png").
    UTexture2D* Load(const FString& Relative);
    static UTexture2D* LoadAbsolute(const FString& Path, UObject* Outer);

private:
    UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> Cache;
    TSet<FString> Missing;
};
