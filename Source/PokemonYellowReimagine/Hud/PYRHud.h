// M4: the Game Boy Color minimap in the bottom-right corner: a Game Boy screen (160 x 144 at 1.5x) showing a 10 x 9
// tile window of the real tiles (YellowData minimap/kanto_outdoor.png outdoors, minimap/ringed/<MAP>.png indoors) that
// scrolls smoothly with Red's continuous position, with his Game Boy sprite in the middle facing the way he moves
// (sprites/red_minimap.png: 16 x 16 frames, 4 facings x 2 steps). It hides during battles and while a menu is open.
// m4r: like the Game Boy screen it also shows everyone else in the window: every shown person and trainer, the item
// balls, Pikachu and Red, each as its own Game Boy overworld sprite (the NPC's SPRITE_* from the Yellow map data) from
// YellowData sprites/people_<PAL>.png in the map's palette, at the actor's live position, facing its way and stepping
// while it walks. People hidden by story flags (APYRPerson::IsShown) and Pikachu before Red has it are not drawn.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PYRHud.generated.h"

class SPYRMinimap;
class UTexture2D;

// One sprite on the minimap (tests read these).
struct FPYRMinimapMarker
{
    TWeakObjectPtr<AActor> Actor;
    FName Sprite;                         // SPRITE_* (SPRITE_RED for Red, SPRITE_PIKACHU, SPRITE_POKE_BALL for item balls)
    int32 Row = INDEX_NONE;               // its row in the people atlas
    int32 Frame = 0;                      // facing * 2 + step (down, up, left, right)
    FVector2D Tile = FVector2D::ZeroVector;   // picture tiles, continuous (the actor's centre)
    FIntPoint Kanto = FIntPoint::ZeroValue;    // the actor's live Kanto tile
    bool bRed = false;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UPYRHud : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UPYRHud* Get(const UObject* Ctx);
    void Start();
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPYRHud, STATGROUP_Tickables); }
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

    // ---- What the minimap shows (tests)
    static constexpr float Scale = 1.5f;
    static constexpr float ScreenW = 160.f;
    static constexpr float ScreenH = 144.f;
    bool IsShown() const { return bShown; }
    // The picture in use ("minimap/kanto_outdoor.png" or "minimap/<MAP>.png"), empty without one.
    const FString& GetImage() const { return Image; }
    // The window's top-left corner in the picture's tiles (continuous: it scrolls with Red).
    FVector2D GetWindowTiles() const { return Window; }
    // Where the widget sits: its anchor on the viewport (1,1 = bottom-right) and its size in px. m4u: on the phone layout
    // it sits in the top right below the stream page's top bar, clear of the touch buttons (Hud/PYRPhoneLayout).
    FVector2D GetAnchor() const { return Anchor; }
    TSharedPtr<class SWidget> GetWidget() const;
    FVector2D GetSize() const { return FVector2D(ScreenW, ScreenH) * Scale; }
    int32 GetSpriteFrame() const { return Frame; }
    // Everyone drawn this frame (inside the window, Red last), and the atlas in use ("sprites/people_PAL_PALLET.png").
    const TArray<FPYRMinimapMarker>& GetMarkers() const { return Markers; }
    const FString& GetPeopleImage() const { return PeopleImage; }
    // A sprite's row in the people atlas (INDEX_NONE when the atlas doesn't have it).
    int32 AtlasRow(FName SpriteId) const { const int32* R = AtlasRows.Find(SpriteId); return R ? *R : INDEX_NONE; }
    int32 AtlasRowCount() const { return AtlasRows.Num(); }

private:
    TSharedPtr<SPYRMinimap> Widget;
    TSharedPtr<class SWidget> Root;
    TSharedPtr<class SConstraintCanvas> Canvas;
    FVector2D Anchor = FVector2D(1.f, 1.f);
    FString PlaceKey;
    void Place();
    bool bStarted = false, bShown = false, bAdded = false;
    FString Image;
    FVector2D Window = FVector2D::ZeroVector;
    FVector2D OriginPx = FVector2D::ZeroVector;   // picture pixels of Kanto tile (0,0) outdoors
    float PxPerTile = 16.f;
    bool bOutdoorLoaded = false;
    int32 Frame = 0;
    float StepClock = 0.f;
    UPROPERTY() TObjectPtr<UTexture2D> Texture;
    UPROPERTY() TObjectPtr<UTexture2D> Sprite;
    UPROPERTY() TObjectPtr<UTexture2D> People;
    FString PeopleImage;
    TMap<FName, int32> AtlasRows;
    TArray<FPYRMinimapMarker> Markers;
    // Where each actor was last tick (its step frame runs while it moves) and its step clock.
    TMap<TWeakObjectPtr<AActor>, FVector> LastAt;
    TMap<TWeakObjectPtr<AActor>, float> Clocks;
    TMap<TWeakObjectPtr<AActor>, double> LastMoved;
    void LoadOutdoorInfo();
    void LoadAtlas();
    // Rooms: minimap/ringed/<MAP>.png has RoomRing tiles of border block round the room (-1 not read yet).
    int32 RoomRing = -1, PictureRing = 0;
    void LoadRoomRing();
    void GatherMarkers(const class AYellowWorld* W, const struct FYMap* M, float DeltaTime);
};
