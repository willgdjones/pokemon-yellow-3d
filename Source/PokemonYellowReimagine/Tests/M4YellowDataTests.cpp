// The YellowData loader and world builder on a tiny inline fixture (no yellowdata output needed), and the log line
// when the folder is missing.
#include "Tests/M4TestKit.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/YellowWorld.h"
#include "Misc/Paths.h"

using namespace M4;

namespace
{
// Two maps: a 6x4 town with a door into a 4x4 room, a trainer, a sign and an item ball; the tables around them.
TMap<FString, FString> Fixture()
{
    TMap<FString, FString> F;
    F.Add(TEXT("manifest.json"), TEXT(R"({"source": {"commit": "fixture"}, "maps": ["TOWN", "ROOM"]})"));
    F.Add(TEXT("maps/TOWN.json"), TEXT(R"({
        "id": "TOWN", "name": "TOWN", "width": 6, "height": 4, "outdoor": true, "origin": {"x": 0, "y": 0}, "music": "Pallet_Town",
        "tiles": ["######", "#.g.v#", "#..w.#", "##.###"],
        "looks": ["TTTTTT", "TpGpvT", "Tppwp.", "TTDTTT"],
        "warps": [{"x": 2, "y": 3, "to_map": "ROOM", "to_warp": 1}],
        "signs": [{"x": 4, "y": 2, "text": "TOWN_SIGN"}],
        "npcs": [{"id": "TOWN_LASS", "x": 1, "y": 1, "sprite": "SPRITE_BRUNETTE_GIRL", "facing": "right", "text": "TOWN_LASS_TEXT",
                  "trainer": {"class": "LASS", "party": 2, "sight": 3, "battle_text": "TOWN_LASS_TEXT", "end_text": "TOWN_LASS_TEXT", "after_text": "TOWN_LASS_TEXT"}}],
        "items": [{"x": 3, "y": 2, "item": "POTION"}],
        "blocked_edges": [{"x": 1, "y": 2, "dir": "right"}]})"));
    F.Add(TEXT("maps/ROOM.json"), TEXT(R"({
        "id": "ROOM", "name": "ROOM", "width": 4, "height": 4, "outdoor": false, "origin": {"x": 100, "y": 0},
        "tiles": ["####", "#..#", "#=.#", "#..#"], "looks": ["WWWW", "WffW", "WCfW", "WmmW"],
        "warps": [{"x": 2, "y": 3, "to_map": "TOWN", "to_warp": 1}]})"));
    F.Add(TEXT("text.json"), TEXT(R"({"TOWN_SIGN": "TOWN\nA quiet place", "TOWN_LASS_TEXT": "Let's battle!"})"));
    F.Add(TEXT("items.json"), TEXT(R"({"POTION": {"name": "POTION", "price": 300}, "OAKS_PARCEL": {"name": "OAK's PARCEL", "price": 0, "key": true},
        "TM_BIDE": {"name": "TM34", "price": 2000, "tm": 34, "move": "BIDE"}})"));
    F.Add(TEXT("marts.json"), TEXT(R"({"ROOM": ["POTION"]})"));
    F.Add(TEXT("wild.json"), TEXT(R"({"TOWN": {"grass_rate": 25, "grass": [[3, "PIDGEY"], [3, "RATTATA"], [4, "PIDGEY"], [4, "RATTATA"], [2, "PIDGEY"],
        [2, "RATTATA"], [5, "PIDGEY"], [5, "RATTATA"], [6, "PIDGEY"], [7, "RATTATA"]]}})"));
    F.Add(TEXT("trainers.json"), TEXT(R"({"LASS": {"name": "LASS", "base_money": 15, "party_index_base": 1,
        "parties": [[{"level": 9, "species": "PIDGEY"}], [{"level": 10, "species": "RATTATA"}, {"level": 11, "species": "NIDORAN_F", "moves": {"3": "GROWL"}}]]}})"));
    F.Add(TEXT("trades.json"), TEXT(R"([])"));
    F.Add(TEXT("events.json"), TEXT(R"([])"));
    F.Add(TEXT("pikachu.json"), TEXT(R"({"start_happiness": 90, "start_mood": 128,
        "deltas": {"levelup": {"delta_by_happiness": {"0-99": 5, "100-199": 3, "200-255": 2}, "mood_target": 138}},
        "face_table": [{"happiness_at_most": 50, "by_mood": [{"mood_at_most": 255, "face": "angry"}]},
                       {"happiness_at_most": 255, "by_mood": [{"mood_at_most": 100, "face": "sad"}, {"mood_at_most": 255, "face": "happy"}]}],
        "faces": {"angry": {"file": "pikachu/angry.png"}, "sad": {"file": "pikachu/sad.png"}, "happy": {"file": "pikachu/happy.png"}}})"));
    F.Add(TEXT("species.json"), TEXT(R"({})"));
    F.Add(TEXT("moves.json"), TEXT(R"({})"));
    F.Add(TEXT("types.json"), TEXT(R"({})"));
    F.Add(TEXT("exp.json"), TEXT(R"({})"));
    return F;
}
}

// The loader reads a fixture's maps and tables exactly, and the world builder makes its tiles solid where they should be.
GAME_TEST(YellowData_FixtureLoads, 60)
{
    if (!Ready(Test) && Test.Seconds < 1.0) return EGameTest::Running;
    TSharedPtr<FYellowDataSet> D = MakeShared<FYellowDataSet>();
    D->LoadFromFiles(Fixture());
    if (D->MapOrder.Num() != 2) M4_FAIL(TEXT("%d maps loaded: %s"), D->MapOrder.Num(), *FString::Join(D->Errors, TEXT("; ")));
    const FYMap* Town = D->Map(TEXT("TOWN"));
    const FYMap* Room = D->Map(TEXT("ROOM"));
    if (Town->Width != 6 || Town->Height != 4 || Town->Tile(3, 2) != TEXT('w') || Town->Look(2, 3) != TEXT('D')) M4_FAIL(TEXT("TOWN parsed wrong"));
    if (Town->Music != TEXT("pallet_town") || !Town->bOutdoor || Room->bOutdoor) M4_FAIL(TEXT("TOWN music/outdoor flags"));
    // Warps count from 1 in pokeyellow: TOWN's door leads to ROOM's first warp.
    if (!D->bWarpsOneBased || D->WarpIndex(*Room, Town->Warps[0].ToWarp) != 0) M4_FAIL(TEXT("warp numbering (one-based %d, index %d)"), D->bWarpsOneBased, D->WarpIndex(*Room, Town->Warps[0].ToWarp));
    const FYNpc* Lass = Town->FindNpc(TEXT("TOWN_LASS"));
    if (!Lass || !Lass->Trainer.bValid || Lass->Trainer.Sight != 3 || Lass->Facing != EYDir::Right) M4_FAIL(TEXT("the lass parsed wrong"));
    const TArray<FYSlot> Team = D->TrainerParty(Lass->Trainer);
    if (Team.Num() != 2 || Team[1].Species != YKey::Species(TEXT("NIDORAN_F")) || Team[1].Level != 11 || Team[1].Moves.FindRef(3) != YKey::Move(TEXT("GROWL")))
        M4_FAIL(TEXT("the lass's party 2 is wrong (%d Pokemon)"), Team.Num());
    const FYItem* Potion = D->Item(YKey::Item(TEXT("POTION")));
    const FYItem* Parcel = D->ItemAny(TEXT("OAKS_PARCEL"));
    const FYItem* Tm = D->ItemAny(TEXT("TM_BIDE"));
    if (!Potion || Potion->Price != 300 || !Parcel || !Parcel->bKey || !Tm || Tm->Tm != 34 || Tm->Move != YKey::Move(TEXT("BIDE"))) M4_FAIL(TEXT("items parsed wrong"));
    if (D->Marts.FindRef(TEXT("ROOM")).Num() != 1) M4_FAIL(TEXT("marts parsed wrong"));
    const FYWild* W = D->Wild.Find(TEXT("TOWN"));
    if (!W || W->GrassRate != 25 || W->Grass.Num() != 10 || W->Grass[9].Level != 7) M4_FAIL(TEXT("wild parsed wrong"));
    if (D->TextFor(TEXT("TOWN_SIGN")) != TEXT("TOWN\nA quiet place")) M4_FAIL(TEXT("text parsed wrong: '%s'"), *D->TextFor(TEXT("TOWN_SIGN")));
    if (D->Pikachu.FaceFor(30, 200) != TEXT("angry") || D->Pikachu.FaceFor(150, 50) != TEXT("sad") || D->Pikachu.FaceFor(150, 200) != TEXT("happy")) M4_FAIL(TEXT("Pikachu faces wrong"));
    if (D->Pikachu.Delta(TEXT("levelup"), 150) != 3 || D->Pikachu.MoodTarget(TEXT("levelup")) != 138) M4_FAIL(TEXT("Pikachu deltas wrong"));

    // Build it far from the game's own world and probe every tile.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AYellowWorld* Built = Test.World->SpawnActor<AYellowWorld>(FVector(400000, 400000, 0), FRotator::ZeroRotator, Params);
    Built->Build(D, FVector(400000, 400000, 0), false);
    if (Built->NumMapsBuilt() != 2) M4_FAIL(TEXT("%d maps built"), Built->NumMapsBuilt());
    int32 Tiles = 0;
    for (const FYMap* M : {Town, Room})
        for (int32 Y = 0; Y < M->Height; ++Y)
            for (int32 X = 0; X < M->Width; ++X)
            {
                const FIntPoint K = M->Origin + FIntPoint(X, Y);
                if (Built->MapAtKanto(K) != M) M4_FAIL(TEXT("%s (%d,%d) is not found in its map"), *M->Id.ToString(), X, Y);
                if (Built->ProbeBlocked(K) != YTile::IsSolid(M->Tile(X, Y))) M4_FAIL(TEXT("%s (%d,%d) '%c' probes %d"), *M->Id.ToString(), X, Y, M->Tile(X, Y), Built->ProbeBlocked(K));
                ++Tiles;
            }
    // The ledge only hops down, the blocked edge blocks both ways, the room's door leads back to the town's door.
    if (Built->CanMove(FIntPoint(1, 2), EYDir::Right) || Built->CanMove(FIntPoint(2, 2), EYDir::Left)) M4_FAIL(TEXT("the blocked edge lets Red through"));
    if (YTile::LedgeDir(Town->Tile(4, 1)) != EYDir::Down) M4_FAIL(TEXT("the ledge does not face down"));
    FName To;
    int32 ToWarp = -1;
    if (!Built->WarpDestination(TEXT("ROOM"), 0, TEXT("TOWN"), To, ToWarp) || To != TEXT("TOWN") || ToWarp != 0) M4_FAIL(TEXT("the room's door leads to %s warp %d"), *To.ToString(), ToWarp);
    Built->Destroy();
    M4_PASS(TEXT("2 maps, a trainer's party 2 with its special move, items (key, TM34), marts, wild, text, Pikachu faces; built and probed %d tiles; ledge, blocked edge and doors"), Tiles);
}

// Without the yellowdata folder the game says which file is missing and still starts (Red on a fallback meadow).
GAME_TEST(YellowData_MissingLogged, 60)
{
    if (Test.Seconds < 1.0) return EGameTest::Running;
    const FString Nowhere = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NoYellowDataHere"));
    FYellowDataSet D;
    if (D.LoadFromDisk(Nowhere)) M4_FAIL(TEXT("loading from an empty folder succeeded"));
    if (D.Errors.Num() == 0 || !D.Errors[0].StartsWith(TEXT("YellowData missing: ")) || !D.Errors[0].Contains(TEXT("manifest.json")))
        M4_FAIL(TEXT("errors: %s"), *FString::Join(D.Errors, TEXT("; ")));
    APlayerController* PC = Test.World->GetFirstPlayerController();
    if (!PC || !PC->GetPawn() || PC->GetPawn()->GetActorLocation().Z < -100.f) M4_FAIL(TEXT("no Red standing"));
    M4_PASS(TEXT("'%s'; the game runs (%s)"), *D.Errors[0], HasData(Test) ? TEXT("with this run's YellowData") : TEXT("on the fallback meadow"));
}
