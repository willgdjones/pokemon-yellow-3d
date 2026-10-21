// M4: Game Boy Yellow's data, read at runtime from <ProjectDir>/YellowData (worker `yellowdata` extracts it from the
// pret/pokeyellow disassembly; see the data contract in the m4 task and YellowData/README). Everything the world,
// the menus and the tables need comes from here: maps (tiles, looks, warps, signs, people, items), text, species,
// moves, items, marts, wild tables, trainers, trades, events, Pikachu's happiness and the experience tables.
//
// - FYellowDataSet holds one loaded copy. UYellowData (a game-instance subsystem) loads the real one once; tests build
//   their own from inline JSON fixtures (FYellowDataSet::LoadFromFiles) and hand it to a world builder of their own.
// - Every missing or broken file is logged as "YellowData missing: <path>" (LogYellow, Error) and kept in Errors, and
//   the rest still loads: nothing here crashes on bad data.
// - -YellowDataDir=<abs path> reads another folder (the yellowdata worker's output before the merge).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "YellowData.generated.h"

class FJsonObject;
class FJsonValue;

POKEMONYELLOWREIMAGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogYellow, Log, All);

// Game Boy facing. The world frame is X east, Y south (the map seen from above), so Down is +Y.
enum class EYDir : uint8 { Down, Up, Left, Right, None };

namespace YDir
{
    POKEMONYELLOWREIMAGINE_API FIntPoint Delta(EYDir D);
    // Actor yaw for a facing: Right 0, Down 90, Left 180, Up -90.
    POKEMONYELLOWREIMAGINE_API float Yaw(EYDir D);
    // The facing nearest a yaw (or a flat direction).
    POKEMONYELLOWREIMAGINE_API EYDir FromYaw(float Yaw);
    POKEMONYELLOWREIMAGINE_API EYDir FromVector(const FVector& V);
    POKEMONYELLOWREIMAGINE_API EYDir FromName(const FString& Name);
    POKEMONYELLOWREIMAGINE_API EYDir Opposite(EYDir D);
    POKEMONYELLOWREIMAGINE_API const TCHAR* Name(EYDir D);
}

// Canonical keys, so the game's code and the extracted tables agree however either spells an id.
namespace YKey
{
    // Items: lower-case letters and digits only ("POKE_BALL", "POKé BALL" and "poke-ball" are all "pokeball"; "TM_34"
    // and "TM34" are "tm34").
    POKEMONYELLOWREIMAGINE_API FName Item(const FString& Any);
    // Species: this game's ids ("nidoran-f", "pikachu"); unknown names become lower-case kebab ("MR_MIME" -> "mr-mime").
    POKEMONYELLOWREIMAGINE_API FName Species(const FString& Any);
    // Moves: this game's ids ("thunder-wave", "poison-powder"); unknown names become lower-case kebab.
    POKEMONYELLOWREIMAGINE_API FName Move(const FString& Any);
    // Lower-case letters and digits only (with é as e, ♀ as f and ♂ as m).
    POKEMONYELLOWREIMAGINE_API FString Alnum(const FString& Any);
}

struct FYWarp
{
    int32 X = 0, Y = 0;
    FName ToMap;           // A map id, or LAST_MAP (back to the outdoor map Red came from).
    int32 ToWarp = 0;      // As written in the file (see FYellowDataSet::WarpIndex).
    bool bOutOfScope = false;
    bool bLastMap = false; // The Game Boy's LAST_MAP warp, already resolved to ToMap by the extractor.
    bool bDoor = false;    // A door tile (data/tilesets/door_tile_ids.asm): arriving here, Red steps one tile down.
};

// A tile edge the Game Boy won't let anyone cross (cave floors at two heights: blocked_edges).
struct FYEdge { int32 X = 0, Y = 0; EYDir Dir = EYDir::None; };

// Something on a tile that answers A without being a person (PCs, bookcases, posters: hidden_events).
struct FYHiddenEvent { int32 X = 0, Y = 0; FString Kind; FString Arg; };

struct FYSign { int32 X = 0, Y = 0; FString Text; };

struct FYTrainerRef
{
    bool bValid = false;
    FName Class;           // trainers.json key.
    int32 Party = 0;       // As written (see FYellowDataSet::TrainerParty).
    int32 Sight = 0;       // Tiles along the trainer's facing.
    FString BattleText, EndText, AfterText;  // text ids (said on sight, on defeat, and when talked to later)
    FName Event;           // The flag set once beaten (EVENT_BEAT_ROUTE_3_TRAINER_0).
};

struct FYNpc
{
    FString Id;            // Unique within the map (the object's id or its 1-based index).
    int32 Index = 0;       // 0-based order in the map file.
    int32 X = 0, Y = 0;
    FName Sprite;          // SPRITE_* constant.
    bool bWalk = false;
    EYDir Facing = EYDir::Down;
    // Walkers: 0 any direction, 1 up and down only, 2 left and right only.
    int32 WalkAxis = 0;
    FString Text;          // text.json id.
    TArray<FString> Texts; // every text label the person uses
    FYTrainerRef Trainer;
    FName Item;            // An object that is an item (a Poke Ball sprite on a table).
    bool bHiddenAtStart = false;
    FString Toggle;        // The Game Boy's show/hide switch for this object.
    FString Script;        // script_pokecenter_nurse, script_cable_club_receptionist...
    TSharedPtr<FJsonObject> Raw;
};

struct FYItemBall { int32 X = 0, Y = 0; FName Item; FString RawItem; FString Id; };

struct FYConnection { bool bValid = false; FName Map; int32 Offset = 0; bool bOutOfScope = false; };

struct FYMap
{
    FName Id;
    FString Name;
    int32 Width = 0, Height = 0;
    bool bOutdoor = false, bCave = false;
    FIntPoint Origin = FIntPoint::ZeroValue;
    FString Tileset, Music, Palette;
    int32 EncounterRate = 0;
    FYConnection Conn[4];  // Indexed by EYDir: Down (south), Up (north), Left (west), Right (east).
    TArray<FString> Tiles, Looks;
    TArray<FYWarp> Warps;
    TArray<FYSign> Signs;
    TArray<FYNpc> Npcs;
    TArray<FYItemBall> Items, Hidden;
    TArray<FYEdge> BlockedEdges;
    TArray<FYHiddenEvent> HiddenEvents;
    // The border block's looks as two rows of two (scenery.json): what the Game Boy draws past an edge with no map.
    TArray<FString> BorderLooks;

    bool Contains(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < Width && Y < Height; }
    // '#' off the map or for a short row; ' ' for a missing look.
    TCHAR Tile(int32 X, int32 Y) const;
    TCHAR Look(int32 X, int32 Y) const;
    // In Kanto tiles (origin + local).
    FIntRect WorldRect() const { return FIntRect(Origin, Origin + FIntPoint(Width, Height)); }
    int32 WarpAt(int32 X, int32 Y) const;
    const FYNpc* FindNpc(const FString& NpcId) const;
};

struct FYItem
{
    FName Id;              // YKey::Item.
    FString RawId;         // As in items.json.
    FString Name;          // Game Boy caps ("POKé BALL").
    int32 Price = 0;
    bool bKey = false, bFieldUse = false, bBattleUse = false;
    int32 Tm = 0, Hm = 0;  // TM/HM number (0 when it is neither).
    FName Move;            // TMs and HMs.
};

struct FYSlot
{
    int32 Level = 0;
    FName Species;
    TMap<int32, FName> Moves;   // trainers.json special moves: slot (1-4) -> move
};

struct FYWild
{
    int32 GrassRate = 0, WaterRate = 0;
    TArray<FYSlot> Grass;  // Ten slots (Yellow's 51,51,39,25,25,25,13,13,11,3 of 256).
};

struct FYTrainerClass
{
    FName Class;
    FString Name;          // Game Boy caps ("YOUNGSTER", "BROCK").
    int32 BaseMoney = 0;
    int32 PartyBase = 1;   // the number the maps use for the first party
    TArray<TArray<FYSlot>> Parties;
};

// Yellow's Pikachu happiness and mood (pikachu.json, engine/events/pikachu_happiness.asm and
// engine/pikachu/pikachu_pic_animation.asm), with Yellow's own numbers standing in when the file is missing.
struct FYPikachuTable
{
    int32 Start = 90, StartMood = 128;
    struct FDelta { int32 Low = 0, Mid = 0, High = 0; int32 MoodTarget = -1; };
    TMap<FString, FDelta> Deltas;         // levelup, fainted, used_item, gym_leader, walking, deposited, traded...
    struct FFaceRow { int32 HappinessAtMost = 255; TArray<TPair<int32, FString>> ByMood; };
    TArray<FFaceRow> FaceTable;
    TMap<FString, FString> FaceFiles;     // face -> "pikachu/<face>.png"
    // The change for an event at a happiness (bands 0-99, 100-199, 200-255); Default when the event is unknown.
    int32 Delta(const FString& Event, int32 Happiness, int32 Default = 0) const;
    // Where the event moves the mood (-1: unchanged).
    int32 MoodTarget(const FString& Event) const;
    // The face for a happiness and mood (the first row at or above the happiness, then the first mood at or above).
    FString FaceFor(int32 Happiness, int32 Mood) const;
    bool IsLoaded() const { return FaceTable.Num() > 0; }
};

class POKEMONYELLOWREIMAGINE_API FYellowDataSet
{
public:
    // Reads files by path relative to the data folder; returns false when the file is missing.
    using FReader = TFunction<bool(const FString& Relative, FString& OutText)>;

    // Load from disk (<Root>/...).
    bool LoadFromDisk(const FString& Root);
    // Load from inline files (fixtures): relative path -> JSON text.
    bool LoadFromFiles(const TMap<FString, FString>& Files);
    bool Load(FReader Reader, const FString& Label);

    FString Root;                          // Disk root ("" for fixtures).
    TArray<FString> Errors;                // "YellowData missing: <path>" lines.
    bool IsLoaded() const { return MapOrder.Num() > 0; }
    FString SourceCommit;

    TArray<FName> MapOrder;                // manifest order
    TMap<FName, FYMap> Maps;
    TMap<FString, FString> Text;
    TMap<FName, FYItem> Items;
    TArray<FName> ItemOrder;               // items.json order
    TMap<FName, TArray<FName>> Marts;      // map id -> item keys
    TMap<FName, FYWild> Wild;
    TMap<FName, FYTrainerClass> Trainers;  // trainers.json key (as written) -> class
    TArray<TSharedPtr<FJsonValue>> Trades;
    TArray<TSharedPtr<FJsonObject>> Events;
    FYPikachuTable Pikachu;
    TSharedPtr<FJsonObject> SpeciesJson, MovesJson, TypesJson, ExpJson;
    // Kanto past the maps built (scenery.json): each out-of-range neighbour (Route 21's sea, Route 23, Route 9,
    // Route 5) cut to a strip along its seam, as looks only (every tile solid). Drawn, never walked.
    TArray<FYMap> Scenery;
    // The rest of Kanto (kanto.json, m4s): every outdoor map out of the range at its place (looks only, every tile
    // solid), the Town Map's squares as land ('L') or sea ('~') with the Kanto tile of square (0,0)'s centre and the
    // tiles per square, and the landmarks the far view is built round (MT_MOON, POKEMON_LEAGUE...: the Kanto tile of
    // the warp into each). Drawn far past the edges (Kanto/YellowWorldFar.cpp), never walked.
    TArray<FYMap> Kanto;
    TArray<FString> TownMapRows;
    FVector2D TownMapSquare0 = FVector2D::ZeroVector, TownMapSquareTiles = FVector2D(22.f, 22.f);
    TMap<FName, FIntPoint> Landmarks;

    const FYMap* Map(FName Id) const { return Maps.Find(Id); }
    const FYItem* Item(FName Key) const { return Items.Find(Key); }
    const FYItem* ItemAny(const FString& Any) const { return Items.Find(YKey::Item(Any)); }
    // A text id's string ("" and a logged error when it is missing).
    FString TextFor(const FString& Id) const;
    bool HasText(const FString& Id) const { return Text.Contains(Id); }
    // The warp in Map a warp entry leads to (0-based index into its Warps), or INDEX_NONE.
    int32 WarpIndex(const FYMap& To, int32 ToWarp) const;
    // A trainer class by the name an NPC uses (tolerates OPP_ prefixes and case).
    const FYTrainerClass* TrainerClass(FName Class) const;
    // The party an NPC's trainer ref names (empty when unknown).
    TArray<FYSlot> TrainerParty(const FYTrainerRef& Ref) const;
    // Every map whose file declares outdoor.
    void OutdoorMaps(TArray<FName>& Out) const;
    // How the files number warps and trainer parties (pokeyellow counts both from 1; decided from the data on load).
    bool bWarpsOneBased = true, bPartiesOneBased = true;

    // Parse one map file (public for the fixture tests).
    static bool ParseMap(const TSharedPtr<FJsonObject>& Json, FYMap& Out, FString& Why);

private:
    void Missing(const FString& Relative, const FString& Why = FString());
    TSharedPtr<FJsonObject> ReadObject(const FString& Relative, bool bRequired);
    TSharedPtr<FJsonValue> ReadValue(const FString& Relative, bool bRequired);
    void ParseTables();
    FReader Read;
    FString SourceLabel;
};

// The game's copy, loaded once per game instance (and species, moves and experience merged into PYRData).
UCLASS()
class POKEMONYELLOWREIMAGINE_API UYellowData : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static UYellowData* Get(const UObject* Ctx);
    // The loaded data (never null once initialised; empty when YellowData is missing).
    static const FYellowDataSet* Data(const UObject* Ctx);
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // <ProjectDir>/YellowData, or -YellowDataDir=.
    static FString DefaultRoot();
    // <ProjectDir>/YellowMusic, or -YellowMusicDir=.
    static FString MusicRoot();

    TSharedPtr<FYellowDataSet> Set;
    // Load again (tests that change the folder).
    void Reload();
};
