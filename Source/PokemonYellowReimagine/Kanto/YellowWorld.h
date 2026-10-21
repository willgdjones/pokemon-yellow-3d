// M4: the world builder. Every map in YellowData's manifest is built at level start, in this one level, from its
// `tiles` (collision) and `looks` (what stands there), at 1 tile = 1 m in the contract's frame: X east, Y south,
// Z = 0 floor, Kanto tile (0,0) at Offset. Outdoor maps sit side by side as their connections put them, so walking
// from one to the next is seamless; interiors and Mt. Moon's floors are packed far to the south.
//
// Collision follows the tiles exactly: every solid tile (#, w, x, =) and every ledge (v < >) is part of a 1 m box
// (adjacent ones merged into rectangles, 4 m tall), and a map's edge is walled wherever no other map continues it.
// Walking stays free and analog on top: ledges are hopped by Red's own code (PokemonYellowReimagineCharacter).
//
// The look (Kanto/YellowWorldLook.cpp) is the anime one: round cel-shaded trees on T, swaying grass tufts on G,
// picket fences on F, painted water, ledge lips, buildings extruded from each connected group of B/R/D/n tiles with
// a red roof for a Pokemon Centre, a blue one for a Mart and the League emblem over a gym door, rock walls and
// stalagmites in caves, and furnished rooms with walls that stay drawn.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Yellow/YellowData.h"
#include "YellowWorld.generated.h"

class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class APokemonYellowReimagineCharacter;
class APostProcessVolume;
class ADirectionalLight;
class ASkyLight;
class UMaterialInstanceDynamic;

namespace YTile
{
    inline bool IsLedge(TCHAR T) { return T == TEXT('v') || T == TEXT('<') || T == TEXT('>'); }
    // Solid for walking (ledges too: they are only ever hopped).
    inline bool IsSolid(TCHAR T) { return T == TEXT('#') || T == TEXT('w') || T == TEXT('x') || T == TEXT('=') || IsLedge(T); }
    inline bool IsWalkable(TCHAR T) { return !IsSolid(T); }
    inline bool IsGrass(TCHAR T) { return T == TEXT('g'); }
    // The direction a ledge is hopped (None for anything else).
    inline EYDir LedgeDir(TCHAR T) { return T == TEXT('v') ? EYDir::Down : T == TEXT('<') ? EYDir::Left : T == TEXT('>') ? EYDir::Right : EYDir::None; }
    constexpr float Size = 100.f;         // cm per tile
    constexpr float WallHeight = 400.f;   // collision boxes
}

// One map's built pieces (its instanced meshes and its collision), so a map can be counted, hidden or probed.
UCLASS()
class POKEMONYELLOWREIMAGINE_API AYellowMapActor : public AActor
{
    GENERATED_BODY()
public:
    AYellowMapActor();
    FName MapId;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Collision;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Floor;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Looks;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Roofs;
    int32 CollisionRects = 0;
    int32 Buildings = 0, Trees = 0, GrassTufts = 0, Fences = 0;
};

// A building found in a map's looks: its tiles, door and kind (for tests and the look).
struct FYBuilding
{
    FName Map;
    FIntRect Rect;            // Kanto tiles.
    TArray<FIntPoint> Doors;  // Kanto tiles of its D looks.
    FName Leads;              // The map its door warp leads to (VIRIDIAN_POKECENTER...).
    enum class EKind : uint8 { House, Centre, Mart, Gym, Lab, Museum, Gate, Other } Kind = EKind::House;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API AYellowWorld : public AActor
{
    GENERATED_BODY()
public:
    AYellowWorld();
    // The game's world (tagged YellowWorld.Main); test worlds built from fixtures are not returned.
    static AYellowWorld* Get(const UObject* Ctx);

    // Build every map in Data at Offset. The main world also spawns the sky, sun and the toon pass.
    void Build(TSharedPtr<const FYellowDataSet> InData, const FVector& InOffset, bool bMain);
    bool IsBuilt() const { return bBuilt; }
    const FYellowDataSet* GetData() const { return Data.Get(); }
    TSharedPtr<const FYellowDataSet> GetDataShared() const { return Data; }

    // ---- Coordinates (Kanto tiles are the maps' origin + local x, y)
    FVector GetOffset() const { return Offset; }
    FIntPoint ToKanto(const FVector& World) const;
    FVector KantoCenter(FIntPoint Kanto) const;
    // World centre of a map's tile (feet height).
    FVector TileCenter(FName Map, int32 X, int32 Y) const;
    const FYMap* MapAtKanto(FIntPoint Kanto) const;
    // The map under a world point, and the local tile there.
    const FYMap* MapAt(const FVector& World, FIntPoint* OutLocal = nullptr) const;
    TCHAR TileAt(FIntPoint Kanto) const;   // '#' off every map
    TCHAR LookAt(FIntPoint Kanto) const;
    bool IsSolidAt(FIntPoint Kanto) const { return YTile::IsSolid(TileAt(Kanto)); }
    // A tile edge nobody may cross (the Game Boy's blocked edges between cave floors of two heights).
    bool IsEdgeBlocked(FIntPoint Kanto, EYDir Dir) const;
    // One step from Kanto in Dir is possible for tiles and edges (people and objects aside).
    bool CanMove(FIntPoint Kanto, EYDir Dir) const;
    // Physical probe: does anything of this world's collision block a 30 cm box at the tile's centre (tests).
    bool ProbeBlocked(FIntPoint Kanto) const;

    // ---- Warps
    // Where warp WarpIndex of Map leads: the destination map and its warp index. LastOutdoor stands in for LAST_MAP.
    bool WarpDestination(FName Map, int32 WarpIndex, FName LastOutdoor, FName& OutMap, int32& OutWarp) const;

    // ---- Build products
    AYellowMapActor* MapActor(FName Map) const;
    int32 NumMapsBuilt() const { return MapActors.Num(); }
    const TArray<FYBuilding>& GetBuildings() const { return Buildings; }
    const FYBuilding* BuildingWithDoorTo(FName LeadsTo) const;
    int32 TotalCollisionRects() const;
    // Kanto past the maps built: the out-of-range neighbours' own tiles and each map's border block (scenery only).
    AYellowMapActor* GetSceneryActor() const { return SceneryActor; }
    // The scenery round a forest map (its border block of trees), when it has one.
    AYellowMapActor* GetSceneryActorFor(FName Map) const { const TObjectPtr<AYellowMapActor>* A = ForestScenery.Find(Map); return A ? A->Get() : nullptr; }
    // Kanto far past the edges (m4s, Kanto/YellowWorldFar.cpp): the rest of Kanto from kanto.json (the out-of-range maps
    // merged into big pieces, the Town Map's sea, tree lines, hills, mountains and the landmarks' massifs). Each look
    // component's tags are "KantoFar", its source (a map id, a landmark such as MT_MOON, SEA or KANTO_GROUND) and its
    // kind ("ground", "sea" or "feature"). No collision.
    AYellowMapActor* GetFarActor() const { return FarActor; }
    // The Town Map's sea at a Kanto tile (kanto.json), with the rules past the picture's edges.
    bool IsKantoSea(FIntPoint Kanto) const;
    // The out-of-range Kanto map drawn on a tile (kanto.json), or None.
    FName KantoMapAt(FIntPoint Kanto) const { const TPair<TCHAR, FName>* P = KantoMapTiles.Find(Kanto); return P ? P->Value : NAME_None; }

    // ---- What is drawn: only the maps that can be seen from where Red is. Outdoors, every outdoor map and Kanto's
    // scenery; in a room, cave or the forest, that map alone (with a black void round rooms and caves, as the Game Boy
    // shows nothing past their walls). People, signs and items of hidden maps are hidden with them.
    void SetFocus(FName Map);
    FName GetFocus() const { return Focus; }
    bool IsMapDrawn(FName Map) const;

    // ---- Things between the camera and Red (trees, buildings) are hidden while they are in the way.
    int32 NewOccluder(const FIntRect& KantoTiles, float HeightCm);
    void AddOccluderPart(int32 Occluder, UInstancedStaticMeshComponent* Component, int32 Instance);
    void SetOccluderRoof(int32 Occluder, UProceduralMeshComponent* Roof, int32 Section);
    // Hides what stands between Eye and Target (and shows again what no longer does).
    void UpdateOccluders(const FVector& Eye, const FVector& Target, const TArray<FVector>& Also = TArray<FVector>());
    void ShowAllOccluders();
    int32 NumOccluders() const { return Occluders.Num(); }
    int32 NumHiddenOccluders() const;
    bool IsOccluderHiddenAt(FIntPoint Kanto) const;

    // What the look builder put on a map tile ("tv", "chair", "table", "bookshelf", "doorway", ...; None: floor or
    // nothing), and the rooms closed over with a lid (nothing outside them shows). Evidence for tests.
    FName FurnitureAt(FName Map, int32 X, int32 Y) const;
    // A room's top row is its back wall, one flat face (m4r, visual-001): a solid top-row tile that is the upper half
    // of a two-tall PC or desk, or a Pokemon Centre machine's top, is drawn as wall and the furniture stands in the row
    // below against it, as the Game Boy draws it. (A bookshelf's top over its shelf stays the shelf.)
    static bool IsBackWallTile(const FYMap& Map, int32 X, int32 Y);
    // Viridian Gym's spinner floor (pokeyellow scripts/ViridianGym.asm, ViridianGymArrowTilePlayerMovement): an arrow
    // tile (Dir: the way it sends the player, in tiles) or the stop square where that slide ends (bStop). The looks
    // table calls both 'm', like a door mat. m4r, visual-055.
    static bool GymSpinnerAt(const FYMap& Map, int32 X, int32 Y, FIntPoint& Dir, bool& bStop);
    bool IsRoomClosed(FName Map) const { return ClosedRooms.Contains(Map); }
    TMap<FIntPoint, FName> Furniture;   // Kanto tile -> what
    TSet<FIntPoint> OutdoorDoorways;    // back-wall doorways that show the outdoors beyond (Kanto tiles)
    TSet<FName> ThinBackRooms;          // rooms whose back wall is a thin strip behind furniture standing in the top row
    TSet<FName> ClosedRooms;
    // The look drawn on an off-map Kanto tile (0 when none): the neighbour's or the border block's look code.
    TCHAR SceneryLookAt(FIntPoint Kanto) const { const TCHAR* L = SceneryLooks.Find(Kanto); return L ? *L : 0; }
    // Which map's scenery a tile shows (a neighbour's id, or "border").
    FName ScenerySourceAt(FIntPoint Kanto) const { const FName* N = ScenerySource.Find(Kanto); return N ? *N : NAME_None; }

    // ---- The light and the look (m4v: World/PYRAreaLook sets them per area).
    ADirectionalLight* GetSun() const { return Sun; }
    ASkyLight* GetSkyFill() const { return SkyFill; }
    APostProcessVolume* GetToonVolume() const { return ToonVolume; }
    UMaterialInstanceDynamic* GetToonMaterial() const { return ToonMaterial; }
    UMaterialInstanceDynamic* GetSkyMaterial() const { return SkyMaterial; }
    // The cloud pieces round the sky dome shown or hidden (the painted sky has its own clouds).
    void SetDomeClouds(bool bShow);

    virtual void Tick(float DeltaSeconds) override;

private:
    void Index();
    void BuildMap(const FYMap& Map);
    void BuildCollision(const FYMap& Map, AYellowMapActor* Actor);
    void BuildLooks(const FYMap& Map, AYellowMapActor* Actor, bool bScenery = false);    // YellowWorldLook.cpp
    void BuildScenery();
    void BuildKantoFar();                                          // YellowWorldFar.cpp
    AYellowMapActor* BuildSceneryCanvas(FName Id, const TMap<FIntPoint, TCHAR>& Looks, const FLinearColor& Meadow, bool bForest);
    void BuildLook();                                              // sky, sun, toon pass
    void FindBuildings(const FYMap& Map, TArray<FYBuilding>& Out) const;

    TSharedPtr<const FYellowDataSet> Data;
    FVector Offset = FVector::ZeroVector;
    bool bBuilt = false, bMain = false;
    TArray<const FYMap*> MapList;
    TMap<FIntPoint, TArray<int32>> Chunks;   // 16x16-tile chunks -> maps touching them
    UPROPERTY() TMap<FName, TObjectPtr<AYellowMapActor>> MapActors;
    TArray<FYBuilding> Buildings;
    TSet<FIntVector> Edges;   // (Kanto x, y, direction) both ways
    UPROPERTY() TObjectPtr<AYellowMapActor> SceneryActor;
    UPROPERTY() TMap<FName, TObjectPtr<AYellowMapActor>> ForestScenery;
    UPROPERTY() TObjectPtr<AYellowMapActor> FarActor;
    TMap<FIntPoint, TPair<TCHAR, FName>> KantoMapTiles;   // every out-of-range Kanto map's tiles (kanto.json)
    TSet<FName> PlateauScenery;                        // scenery sources on the PLATEAU tileset (open ground is grass)
    FName Focus;
    struct FOccluder
    {
        FIntRect Tiles;
        float Height = 300.f;
        TArray<TPair<TWeakObjectPtr<UInstancedStaticMeshComponent>, int32>> Parts;
        TArray<FTransform> Saved;
        TWeakObjectPtr<UProceduralMeshComponent> Roof;
        int32 RoofSection = INDEX_NONE;
        bool bHidden = false;
    };
    TArray<FOccluder> Occluders;
    TMap<FIntPoint, TArray<int32>> OccluderAt;
    void HideOccluder(FOccluder& O, bool bHide);
    TMap<FIntPoint, TCHAR> SceneryLooks;
    TMap<FIntPoint, FName> ScenerySource;
    UPROPERTY() TObjectPtr<AActor> SkyDome;
    UPROPERTY() TObjectPtr<APostProcessVolume> ToonVolume;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ToonMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SkyMaterial;
    UPROPERTY() TObjectPtr<ADirectionalLight> Sun;
    UPROPERTY() TObjectPtr<ASkyLight> SkyFill;
    FRandomStream Rng{0x4b414e54};
};
