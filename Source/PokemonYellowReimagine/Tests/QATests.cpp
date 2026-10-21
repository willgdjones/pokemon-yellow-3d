// QA harness, part 1: the warp and link tour, the text dump, the talk tour and the visual tour. Each logs its
// observations as QA_REC lines (Tests/QAKit.h); an external checker (not included) judged them against pret/pokeyellow.
#include "Tests/QAKit.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "UnrealClient.h"
#include "World/CoreSeams.h"

using namespace M4;

namespace
{
const TCHAR* DirName(EYDir D) { return YDir::Name(D); }

// Is this tile somewhere Red can stand to step onto a neighbour (walkable, not a warp, on the map)?
bool Standable(const FYMap* M, int32 X, int32 Y)
{
    return M->Contains(X, Y) && !YTile::IsSolid(M->Tile(X, Y)) && M->WarpAt(X, Y) == INDEX_NONE;
}

// Who stands on a Kanto tile (a shown person's id), or "".
FString PersonAt(FGameTestContext& Test, FIntPoint Kanto)
{
    for (const TWeakObjectPtr<APYRPerson>& P : Field(Test)->People)
        if (P.IsValid() && P->IsShown() && P->Tile == Kanto) return P->Id;
    return FString();
}

// ------------------------------------------------------------------------------------------------ the warp tour
struct FWarpCase { FName Map; int32 Warp = 0; };
struct FEdgeCase { FName Map; EYDir Dir = EYDir::None; int32 X = 0, Y = 0; bool bConnected = false; FName To; };

// Off-map scenery: what is drawn on every tile within Ring tiles of an outdoor map that no map covers, and whether
// the collision stops Red there. Where the world drew the scenery beyond the edge (Kanto past the range, the border
// block) the cell names the kind of look drawn on it; otherwise T: a tree, G: ground only, O: something else,
// '-': nothing.
void LogEdgeRings(FGameTestContext& Test, int32 Ring)
{
    const AYellowWorld* W = World(Test);
    const FYellowDataSet* D = Data(Test);
    const TMap<FIntPoint, uint8> Drawn = QA::DrawnOffMap(Test, Ring);
    for (const FName& Id : D->MapOrder)
    {
        const FYMap* M = D->Map(Id);
        if (!M || !M->bOutdoor) continue;
        TArray<TSharedPtr<FJsonValue>> Cells;
        for (int32 Y = -Ring; Y < M->Height + Ring; ++Y)
            for (int32 X = -Ring; X < M->Width + Ring; ++X)
            {
                if (M->Contains(X, Y)) continue;
                const FIntPoint Kt = M->Origin + FIntPoint(X, Y);
                if (W->MapAtKanto(Kt)) continue;
                const uint8 B = Drawn.FindRef(Kt);
                const TCHAR Look = W->SceneryLookAt(Kt);
                const TCHAR* C = B && Look ? QA::LookKind(Look) : (B & 1) ? TEXT("T") : (B & 4) ? TEXT("O") : (B & 2) ? TEXT("G") : TEXT("-");
                const bool bNear = X >= -1 && Y >= -1 && X <= M->Width && Y <= M->Height;
                TArray<TSharedPtr<FJsonValue>> Cell = {MakeShared<FJsonValueNumber>(X), MakeShared<FJsonValueNumber>(Y), MakeShared<FJsonValueString>(C),
                    MakeShared<FJsonValueNumber>(bNear ? (W->ProbeBlocked(Kt) ? 1 : 0) : -1)};
                Cells.Add(MakeShared<FJsonValueArray>(Cell));
            }
        TSharedRef<FJsonObject> R = QA::Rec(TEXT("edge_ring"));
        R->SetStringField(TEXT("map"), Id.ToString());
        R->SetNumberField(TEXT("ring"), Ring);
        R->SetArrayField(TEXT("cells"), Cells);
        QA::Log(R);
    }
}
}

// Every warp and every map edge from Pallet Town to Cerulean, taken the way a player takes them: Red stands next to
// the warp tile, walks onto it (and on, for an exit mat), and the test records where he lands, on which tile and
// facing which way. Every walkable edge tile with a connection is crossed; edges without one (or out of scope) are
// pushed against. The off-map scenery ring of every outdoor map is logged too.
GAME_TEST(QA_WarpTour, 1800)
{
    static FState St;
    static TArray<FWarpCase> Warps;
    static TArray<FEdgeCase> Edges;
    static int32 Fired = 0, Missed = 0;
    static EYDir Approach = EYDir::None;
    static int32 WarpsBefore = 0, MapsBefore = 0, BlockedBefore = 0;
    static FName FirstMap;
    static FIntPoint FirstTile;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRField* F = Field(Test);
    const AYellowWorld* W = World(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    Quiet(true, true);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 30}});
        AfterPokedex(Test);
        State(Test)->AwardBadge(EPYRBadge::Boulder);
        State(Test)->AwardBadge(EPYRBadge::Cascade);
        for (const FName& Id : D->MapOrder)
        {
            const FYMap* M = D->Map(Id);
            for (int32 I = 0; M && I < M->Warps.Num(); ++I) Warps.Add({Id, I});
            if (!M || !M->bOutdoor) continue;
            for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
            {
                const FYConnection& C = M->Conn[int32(Dir)];
                const bool bConn = C.bValid && !C.bOutOfScope && D->Map(C.Map);
                const int32 Len = Dir == EYDir::Up || Dir == EYDir::Down ? M->Width : M->Height;
                TArray<FEdgeCase> Side;
                for (int32 I = 0; I < Len; ++I)
                {
                    const int32 X = Dir == EYDir::Left ? 0 : Dir == EYDir::Right ? M->Width - 1 : I;
                    const int32 Y = Dir == EYDir::Up ? 0 : Dir == EYDir::Down ? M->Height - 1 : I;
                    if (!Standable(M, X, Y)) continue;
                    const FIntPoint Out = M->Origin + FIntPoint(X, Y) + YDir::Delta(Dir);
                    const FYMap* Other = W->MapAtKanto(Out);
                    if (bConn && (!Other || YTile::IsSolid(W->TileAt(Out)))) continue;
                    Side.Add({Id, Dir, X, Y, bConn, bConn ? C.Map : (Other ? Other->Id : NAME_None)});
                }
                // Up to three crossings per side (first, middle, last); two pushes on a closed side.
                const int32 Want = bConn ? 3 : 2;
                for (int32 K = 0; K < Side.Num() && K < Want; ++K)
                {
                    const int32 Pick = Want == 1 || Side.Num() == 1 ? 0 : FMath::RoundToInt(float(K) * (Side.Num() - 1) / (Want - 1));
                    if (K > 0 && Edges.Num() && Edges.Last().Map == Id && Edges.Last().Dir == Dir && Edges.Last().X == Side[Pick].X && Edges.Last().Y == Side[Pick].Y) continue;
                    Edges.Add(Side[Pick]);
                }
            }
        }
        LogEdgeRings(Test, 4);
        UE_LOG(LogYellow, Display, TEXT("QA_WARP_TOUR %d warps, %d edge cases"), Warps.Num(), Edges.Num());
        St.I = 0;
        St.N = 0;
        M4_NEXT();
    }
    // ---- warps
    if (St.Step == 1)
    {
        if (St.I >= Warps.Num()) { St.I = 0; St.Step = 4; St.T0 = Test.Seconds; return EGameTest::Running; }
        const FWarpCase& C = Warps[St.I];
        const FYMap* M = D->Map(C.Map);
        const FYWarp& Wp = M->Warps[C.Warp];
        // Where Red comes from: outdoors a door is walked into upwards, indoors a mat downwards; else any open side.
        const TArray<EYDir> Order = M->bOutdoor ? TArray<EYDir>{EYDir::Up, EYDir::Left, EYDir::Right, EYDir::Down} : TArray<EYDir>{EYDir::Down, EYDir::Up, EYDir::Left, EYDir::Right};
        Approach = EYDir::None;
        for (const EYDir Dir : Order)
        {
            const FIntPoint From = FIntPoint(Wp.X, Wp.Y) - YDir::Delta(Dir);
            if (Standable(M, From.X, From.Y) && !W->IsEdgeBlocked(M->Origin + From, Dir)) { Approach = Dir; St.P = From; break; }
        }
        WarpsBefore = F->WarpsTaken;
        BlockedBefore = F->WarpsBlocked;
        MapsBefore = F->MapChanges;
        if (Approach == EYDir::None)
        {
            TSharedRef<FJsonObject> Rc = QA::Rec(TEXT("warp"));
            Rc->SetStringField(TEXT("map"), C.Map.ToString());
            Rc->SetNumberField(TEXT("warp"), C.Warp);
            Rc->SetArrayField(TEXT("tile"), QA::Point(FIntPoint(Wp.X, Wp.Y)));
            Rc->SetBoolField(TEXT("fired"), false);
            Rc->SetStringField(TEXT("note"), TEXT("no open tile next to the warp to walk onto it from"));
            QA::Log(Rc);
            ++Missed;
            ++St.I;
            return EGameTest::Running;
        }
        Place(Test, C.Map, St.P.X, St.P.Y, Approach);
        St.Step = 2;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (St.Step == 2 || St.Step == 3)
    {
        const FWarpCase& C = Warps[St.I];
        const FYMap* M = D->Map(C.Map);
        const FYWarp& Wp = M->Warps[C.Warp];
        if (St.Step == 2)
        {
            M4_WAIT(.25);
            const bool bFired = F->WarpsTaken != WarpsBefore || F->IsWarping();
            if (!bFired && Test.Seconds - St.T0 < 3.5)
            {
                // Walk onto the tile and on the same way (an exit mat takes a push past it).
                R->TestInput = FVector(YDir::Delta(Approach).X, YDir::Delta(Approach).Y, 0.f);
                return EGameTest::Running;
            }
            StopWalking(Test);
            if (!bFired)
            {
                TSharedRef<FJsonObject> Rc = QA::Rec(TEXT("warp"));
                Rc->SetStringField(TEXT("map"), C.Map.ToString());
                Rc->SetNumberField(TEXT("warp"), C.Warp);
                Rc->SetArrayField(TEXT("tile"), QA::Point(FIntPoint(Wp.X, Wp.Y)));
                Rc->SetStringField(TEXT("approach"), DirName(Approach));
                Rc->SetBoolField(TEXT("fired"), false);
                Rc->SetStringField(TEXT("stood"), QA::TileText(Test, F->KantoTile));
                const FString Blocker = PersonAt(Test, M->Origin + St.P) + PersonAt(Test, M->Origin + FIntPoint(Wp.X, Wp.Y));
                Rc->SetStringField(TEXT("blocked_by"), Blocker);
                // A warp out of the range stopped the way Yellow stops the player (a guard's line), not a dead tile.
                // Where Yellow has no words (Diglett's Cave, behind Route 2's Cut trees) Red just steps back.
                if (F->WarpsBlocked != BlockedBefore)
                {
                    const FString Line = F->BlockedWarpText(C.Map, C.Warp);
                    Rc->SetStringField(TEXT("blocked_out_of_range"), Line.IsEmpty() ? FString(TEXT("(stepped back; Yellow has no line here)")) : Line);
                }
                FName GM; int32 GW = 0;
                Rc->SetBoolField(TEXT("game_leads"), W->WarpDestination(C.Map, C.Warp, F->LastOutdoor, GM, GW));
                QA::Log(Rc);
                ++Missed;
                ++St.I;
                St.Step = 1;
                return EGameTest::Running;
            }
            St.Step = 3;
            St.T0 = Test.Seconds;
            return EGameTest::Running;
        }
        // Landed: wait for the fade and the step off the door.
        if ((F->IsWarping() || R->IsScriptWalking()) && Test.Seconds - St.T0 < 4.0) return EGameTest::Running;
        M4_WAIT(.3);
        FName GM;
        int32 GW = INDEX_NONE;
        W->WarpDestination(C.Map, C.Warp, F->LastOutdoor, GM, GW);
        TSharedRef<FJsonObject> Rc = QA::Rec(TEXT("warp"));
        Rc->SetStringField(TEXT("map"), C.Map.ToString());
        Rc->SetNumberField(TEXT("warp"), C.Warp);
        Rc->SetArrayField(TEXT("tile"), QA::Point(FIntPoint(Wp.X, Wp.Y)));
        Rc->SetStringField(TEXT("approach"), DirName(Approach));
        Rc->SetBoolField(TEXT("fired"), true);
        Rc->SetStringField(TEXT("to_map"), Wp.ToMap.ToString());
        Rc->SetNumberField(TEXT("to_warp"), Wp.ToWarp);
        Rc->SetStringField(TEXT("game_map"), GM.ToString());
        Rc->SetNumberField(TEXT("game_warp"), GW);
        Rc->SetStringField(TEXT("landed_map"), F->CurrentMap.ToString());
        Rc->SetArrayField(TEXT("landed_tile"), QA::Point(F->CurrentTile));
        Rc->SetStringField(TEXT("landed_facing"), DirName(F->Facing));
        Rc->SetBoolField(TEXT("landed_on_map"), W->MapAt(R->GetActorLocation()) != nullptr);
        if (const FYMap* LM = D->Map(F->CurrentMap)) Rc->SetStringField(TEXT("below_person"), PersonAt(Test, LM->Origin + F->CurrentTile + FIntPoint(0, 1)));
        QA::Log(Rc);
        ++Fired;
        ++St.I;
        St.Step = 1;
        return EGameTest::Running;
    }
    // ---- edges
    if (St.Step == 4)
    {
        if (St.I >= Edges.Num())
        {
            const FString Summary = FString::Printf(TEXT("%d warps (%d fired, %d did not), %d edge cases"), Warps.Num(), Fired, Missed, Edges.Num());
            if (QA::Strict() && Missed) M4_FAIL(TEXT("%s"), *Summary);
            M4_PASS(TEXT("%s"), *Summary);
        }
        const FEdgeCase& E = Edges[St.I];
        const FIntPoint Back = FIntPoint(E.X, E.Y) - YDir::Delta(E.Dir);
        const FYMap* M = D->Map(E.Map);
        const FIntPoint From = Standable(M, Back.X, Back.Y) ? Back : FIntPoint(E.X, E.Y);
        Place(Test, E.Map, From.X, From.Y, E.Dir);
        MapsBefore = F->MapChanges;
        FirstMap = NAME_None;
        St.Step = 5;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    const FEdgeCase& E = Edges[St.I];
    M4_WAIT(.2);
    if (FirstMap.IsNone() && F->MapChanges != MapsBefore) { FirstMap = F->CurrentMap; FirstTile = F->CurrentTile; }
    const FYMap* M = D->Map(E.Map);
    const FIntPoint EdgeK = M->Origin + FIntPoint(E.X, E.Y);
    const FIntPoint Here = W->ToKanto(R->GetActorLocation());
    const int32 Past = FVector2D::DotProduct(FVector2D(Here - EdgeK), FVector2D(YDir::Delta(E.Dir)));
    if (Test.Seconds - St.T0 < 2.2 && Past < 2 && !F->IsLocked())
    {
        R->TestInput = FVector(YDir::Delta(E.Dir).X, YDir::Delta(E.Dir).Y, 0.f);
        return EGameTest::Running;
    }
    StopWalking(Test);
    TSharedRef<FJsonObject> Rc = QA::Rec(TEXT("edge"));
    Rc->SetStringField(TEXT("map"), E.Map.ToString());
    Rc->SetStringField(TEXT("dir"), DirName(E.Dir));
    Rc->SetArrayField(TEXT("tile"), QA::Point(FIntPoint(E.X, E.Y)));
    Rc->SetBoolField(TEXT("connected"), E.bConnected);
    Rc->SetStringField(TEXT("to"), E.To.ToString());
    Rc->SetNumberField(TEXT("past"), Past);
    Rc->SetStringField(TEXT("first_map"), FirstMap.ToString());
    Rc->SetArrayField(TEXT("first_tile"), QA::Point(FirstTile));
    Rc->SetStringField(TEXT("end_map"), F->CurrentMap.ToString());
    Rc->SetArrayField(TEXT("end_tile"), QA::Point(F->CurrentTile));
    Rc->SetBoolField(TEXT("end_on_map"), W->MapAtKanto(Here) != nullptr);
    Rc->SetArrayField(TEXT("end_kanto"), QA::Point(Here));
    QA::Log(Rc);
    ++St.I;
    St.Step = 4;
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ the text dump
// Every string the game can print, as the game fills it: each text.json line and each system line (PYRSysText) with
// the names RED/GARY and with seven-letter names, the menus as their screens describe them, and the naming screen.
GAME_TEST(QA_TextDump, 600)
{
    static FState St;
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    UPYRProgress* P = Progress(Test);
    UPYRMenus* Me = Menus(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        struct FNames { const TCHAR* Tag; const TCHAR* Player; const TCHAR* Rival; };
        const FNames Sets[] = {{TEXT("short"), TEXT("RED"), TEXT("GARY")}, {TEXT("long"), TEXT("WWWWWWW"), TEXT("MMMMMMM")}};
        TArray<FString> Ids;
        D->Text.GetKeys(Ids);
        Ids.Sort();
        TArray<FString> Sys;
        PYRSysText::Table().GetKeys(Sys);
        Sys.Sort();
        int32 N = 0;
        for (const FNames& S : Sets)
        {
            P->PlayerName = S.Player;
            P->RivalName = S.Rival;
            for (const FString& Id : Ids)
            {
                TSharedRef<FJsonObject> R = QA::Rec(TEXT("text_fill"));
                R->SetStringField(TEXT("source"), TEXT("text.json"));
                R->SetStringField(TEXT("id"), Id);
                R->SetStringField(TEXT("names"), S.Tag);
                R->SetStringField(TEXT("text"), PYRGb::Fill(Test.World, D->Text[Id]));
                QA::Log(R);
                ++N;
            }
            for (const FString& Id : Sys)
            {
                TSharedRef<FJsonObject> R = QA::Rec(TEXT("text_fill"));
                R->SetStringField(TEXT("source"), TEXT("systext"));
                R->SetStringField(TEXT("id"), Id);
                R->SetStringField(TEXT("names"), S.Tag);
                // The table's line with its RAM fills kept ({str:..}): Get() without fills would blank them.
                R->SetStringField(TEXT("text"), PYRGb::Fill(Test.World, PYRSysText::Table()[Id]));
                QA::Log(R);
                ++N;
            }
        }
        P->PlayerName = TEXT("RED");
        P->RivalName = TEXT("GARY");
        St.N = N;
        Party(Test, {{TEXT("pikachu"), 12}, {TEXT("pidgey"), 9}});
        AfterPokedex(Test);
        State(Test)->AddItem(UPYRTrainerState::Potion, 3);
        State(Test)->AddItem(UPYRTrainerState::PokeBall, 5);
        Place(Test, TEXT("VIRIDIAN_POKECENTER"), 3, 5, EYDir::Up);
        M4_NEXT();
    }
    // The menus, one after another: open, let it draw, log what it shows, close.
    static const TArray<FString> Screens = {TEXT("start"), TEXT("party"), TEXT("stats"), TEXT("bag"), TEXT("pokedex"), TEXT("card"), TEXT("save"), TEXT("options"),
        TEXT("mart"), TEXT("pc"), TEXT("townmap"), TEXT("naming")};
    if (St.I < Screens.Num())
    {
        if (St.Step == 1)
        {
            Me->CloseAll();
            Me->TextLog.Reset();
            const FString& S = Screens[St.I];
            if (S == TEXT("start")) PYRScreens::OpenStart(Test.World);
            else if (S == TEXT("party")) PYRScreens::OpenParty(Test.World);
            else if (S == TEXT("stats")) PYRScreens::OpenStats(Test.World, 0);
            else if (S == TEXT("bag")) PYRScreens::OpenBag(Test.World);
            else if (S == TEXT("pokedex")) PYRScreens::OpenPokedex(Test.World);
            else if (S == TEXT("card")) PYRScreens::OpenTrainerCard(Test.World);
            else if (S == TEXT("save")) PYRScreens::OpenSave(Test.World);
            else if (S == TEXT("options")) PYRScreens::OpenOptions(Test.World);
            else if (S == TEXT("mart")) PYRScreens::OpenMart(Test.World, TEXT("VIRIDIAN_MART"));
            else if (S == TEXT("pc")) PYRScreens::OpenPc(Test.World);
            else if (S == TEXT("townmap")) PYRScreens::OpenTownMap(Test.World);
            else if (S == TEXT("naming")) PYRScreens::OpenNaming(Test.World, TEXT("YOUR NAME?"), TEXT("RED"), [](FString) {});
            if (S == TEXT("start")) St.Seen = PYRScreens::StartEntries(Test.World);
            else St.Seen.Reset();
            M4_NEXT();
        }
        M4_WAIT(.4);
        TSharedRef<FJsonObject> R = QA::Rec(TEXT("menu"));
        R->SetStringField(TEXT("screen"), Screens[St.I]);
        R->SetStringField(TEXT("text"), Me->ScreenText());
        R->SetArrayField(TEXT("said"), QA::Strings(Me->TextLog));
        if (St.Seen.Num()) R->SetArrayField(TEXT("entries"), QA::Strings(St.Seen));
        QA::Log(R);
        Me->CloseAll();
        ++St.I;
        St.Step = 1;
        St.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    M4_PASS(TEXT("%d filled lines and %d menus logged"), St.N, Screens.Num());
}

// ------------------------------------------------------------------------------------------------ the talk tour
// Everyone and every sign from Pallet Town to Cerulean, talked to or read the way a player does (from the next tile or
// across a counter, facing them, E), after the Pokedex. Each logs every page shown; YES/NO boxes get NO so nothing is
// bought or traded; unbeaten trainers are marked beaten first so they say their after-battle line.
GAME_TEST(QA_TalkTour, 1800)
{
    static FState St;
    static TArray<TWeakObjectPtr<AActor>> Who;
    static int32 Stuck = 0, Silent = 0;
    M4_READY();
    UPYRField* F = Field(Test);
    UPYRMenus* Me = Menus(Test);
    UPYRProgress* P = Progress(Test);
    Quiet(true, true);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 40}});
        AfterPokedex(Test);
        for (const TWeakObjectPtr<APYRPerson>& Pe : F->People) if (Pe.IsValid()) Who.Add(Pe.Get());
        for (const TWeakObjectPtr<APYRSignpost>& S : F->Signs) if (S.IsValid()) Who.Add(S.Get());
        St.I = 0;
        M4_NEXT();
    }
    if (St.I >= Who.Num())
    {
        if (QA::Strict() && Stuck) M4_FAIL(TEXT("%d talked to, %d never finished"), Who.Num(), Stuck);
        M4_PASS(TEXT("%d people and signs talked to; %d never finished, %d said nothing"), Who.Num(), Stuck, Silent);
    }
    AActor* A = Who[St.I].Get();
    APYRPerson* Pe = Cast<APYRPerson>(A);
    APYRSignpost* Sign = Cast<APYRSignpost>(A);
    auto Log = [&](bool bStuck, const FString& Note)
    {
        TSharedRef<FJsonObject> R = QA::Rec(Pe ? TEXT("talk") : TEXT("sign"));
        R->SetStringField(TEXT("map"), Pe ? Pe->Map.ToString() : Sign ? Sign->Map.ToString() : FString());
        if (Pe)
        {
            R->SetStringField(TEXT("npc"), Pe->Id);
            R->SetStringField(TEXT("sprite"), Pe->Sprite.ToString());
            R->SetStringField(TEXT("text_id"), Pe->TextId);
            R->SetBoolField(TEXT("shown"), Pe->IsShown());
            R->SetBoolField(TEXT("trainer"), Pe->IsTrainer());
        }
        if (Sign) R->SetStringField(TEXT("text_id"), Sign->TextId);
        const FYMap* M = Data(Test)->Map(Pe ? Pe->Map : Sign->Map);
        const FIntPoint T = (Pe ? Pe->Home : Sign->Tile) - (M ? M->Origin : FIntPoint::ZeroValue);
        R->SetArrayField(TEXT("tile"), QA::Point(T));
        R->SetArrayField(TEXT("pages"), QA::Strings(Me->TextLog));
        R->SetArrayField(TEXT("scripts"), QA::Strings(St.Seen));
        R->SetBoolField(TEXT("stuck"), bStuck);
        R->SetStringField(TEXT("note"), Note);
        QA::Log(R);
    };
    // Up to five tries per person (a walker steps off as Red arrives, or E lands while the last talk lets go), each
    // waiting a little longer; the walker stands still meanwhile, as in Text_EveryLineMatchesYellow.
    static int32 Tries = 0;
    auto Next = [&]() { if (Pe) Pe->bFrozen = false; Me->CloseAll(); StopWalking(Test); ++St.I; St.Step = 1; St.T0 = Test.Seconds; St.Seen.Reset(); Tries = 0; };
    if (!A || (Pe && !Pe->IsShown())) { Next(); return EGameTest::Running; }
    UPYRScripts* Sc = UPYRScripts::Get(Test.World);
    if (St.Step == 1)
    {
        Me->CloseAll();
        if (Pe) { Pe->bFrozen = true; if (Pe->IsMoving() && Test.Seconds - St.T0 < 3) return EGameTest::Running; }
        if (Pe && Pe->IsTrainer() && !Pe->IsBeaten()) P->Beat(Pe->TrainerKey());
        FIntPoint Spot;
        EYDir Face = EYDir::Up;
        bool bOk = false;
        if (Pe) bOk = QA::TalkSpot(Test, Pe, Spot, Face);
        else
        {
            const AYellowWorld* W = World(Test);
            for (const EYDir Dir : {EYDir::Up, EYDir::Left, EYDir::Right, EYDir::Down})
            {
                const FIntPoint From = Sign->Tile - YDir::Delta(Dir);
                const FYMap* M = W->MapAtKanto(From);
                if (M && M->Id == Sign->Map && !W->IsSolidAt(From) && M->WarpAt(From.X - M->Origin.X, From.Y - M->Origin.Y) == INDEX_NONE) { Spot = From; Face = Dir; bOk = true; break; }
            }
        }
        if (!bOk) { Log(true, TEXT("no tile to talk from")); ++Stuck; Next(); return EGameTest::Running; }
        const FYMap* M = World(Test)->MapAtKanto(Spot);
        Place(Test, M->Id, Spot.X - M->Origin.X, Spot.Y - M->Origin.Y, Face);
        Me->TextLog.Reset();
        St.N = Sc->Ran.Num();
        M4_NEXT();
    }
    if (St.Step == 2) { M4_WAIT(.25 + .35 * Tries); PressE(Test); St.A = 0; M4_NEXT(); }
    // Through everything said: text goes on, YES/NO gets NO, lists (Mart, PC) are logged and left.
    for (int32 I = St.N; I < Sc->Ran.Num(); ++I) St.Seen.AddUnique(Sc->Ran[I]);
    const FName Top = TopId(Test);
    if (!Top.IsNone() && Top != TEXT("text") && Top != TEXT("choice") && Top != TEXT("naming"))
    {
        Me->TextLog.Add(TEXT("[") + Top.ToString() + TEXT("] ") + Me->ScreenText());
        Me->Press(EPYRKey::Back);
        if (TopId(Test) == Top) Me->Pop();
    }
    QA::Drive(Test, false);
    if (QA::Idle(Test))
    {
        if (St.A == 0) St.A = Test.Seconds;
        if (Test.Seconds - St.A > .5)
        {
            if (Me->TextLog.Num() == 0 && Tries < 4) { ++Tries; St.Step = 1; St.T0 = Test.Seconds; return EGameTest::Running; }
            if (Me->TextLog.Num() == 0) ++Silent;
            Log(false, Me->TextLog.Num() ? FString() : TEXT("nothing said"));
            Next();
        }
        return EGameTest::Running;
    }
    St.A = 0;
    if (Test.Seconds - St.T0 > 25)
    {
        Log(true, FString::Printf(TEXT("still busy after 25 s: script '%s', screens %s"), *Sc->RunningName(), *Me->ScreenText().Left(160)));
        ++Stuck;
        Next();
    }
    return EGameTest::Running;
}

// ------------------------------------------------------------------------------------------------ the visual tour
// One screenshot of every map and interior (Red near its middle, the ordinary player camera looking north) and one of
// each story beat and main screen. QA_VisualTour takes them all in one run; QA_Shot_<name> takes one. The pictures
// go to -QAShotDir  as <name>.png, and
// the checker reviewed each with an image reviewer against the Game Boy map.
namespace
{
const TCHAR* const GMapIds[] = {
    TEXT("PALLET_TOWN"), TEXT("REDS_HOUSE_1F"), TEXT("REDS_HOUSE_2F"), TEXT("BLUES_HOUSE"), TEXT("OAKS_LAB"), TEXT("ROUTE_1"), TEXT("VIRIDIAN_CITY"),
    TEXT("VIRIDIAN_POKECENTER"), TEXT("VIRIDIAN_MART"), TEXT("VIRIDIAN_SCHOOL_HOUSE"), TEXT("VIRIDIAN_NICKNAME_HOUSE"), TEXT("VIRIDIAN_GYM"), TEXT("ROUTE_22"),
    TEXT("ROUTE_22_GATE"), TEXT("ROUTE_2"), TEXT("ROUTE_2_GATE"), TEXT("ROUTE_2_TRADE_HOUSE"), TEXT("DIGLETTS_CAVE_ROUTE_2"), TEXT("VIRIDIAN_FOREST_SOUTH_GATE"),
    TEXT("VIRIDIAN_FOREST"), TEXT("VIRIDIAN_FOREST_NORTH_GATE"), TEXT("PEWTER_CITY"), TEXT("MUSEUM_1F"), TEXT("MUSEUM_2F"), TEXT("PEWTER_GYM"),
    TEXT("PEWTER_NIDORAN_HOUSE"), TEXT("PEWTER_MART"), TEXT("PEWTER_SPEECH_HOUSE"), TEXT("PEWTER_POKECENTER"), TEXT("ROUTE_3"), TEXT("MT_MOON_POKECENTER"),
    TEXT("MT_MOON_1F"), TEXT("MT_MOON_B1F"), TEXT("MT_MOON_B2F"), TEXT("ROUTE_4"), TEXT("CERULEAN_CITY"), TEXT("CERULEAN_TRASHED_HOUSE"),
    TEXT("CERULEAN_MELANIES_HOUSE"), TEXT("CERULEAN_POKECENTER"), TEXT("CERULEAN_GYM"), TEXT("BIKE_SHOP"), TEXT("CERULEAN_MART"), TEXT("CERULEAN_BADGE_HOUSE"),
    TEXT("ROUTE_24"), TEXT("ROUTE_25"), TEXT("BILLS_HOUSE")};

// Story beats and screens: name, how it is staged (an events.json id, or a special), and whether to wait for a fight.
struct FBeatShot { const TCHAR* Name; const TCHAR* Event; bool bFight; };
const FBeatShot GBeats[] = {
    {TEXT("intro"), TEXT("@intro"), false},
    {TEXT("naming-player"), TEXT("@naming-player"), false},
    {TEXT("naming-rival"), TEXT("@naming-rival"), false},
    {TEXT("oak-intercept"), TEXT("@oak"), false},
    {TEXT("oaks-lab-pikachu"), TEXT("@lab"), false},
    {TEXT("lab-rival-battle"), TEXT("oaks_lab_rival_battle"), true},
    {TEXT("viridian-parcel"), TEXT("viridian_mart_oaks_parcel"), false},
    {TEXT("pokedex"), TEXT("oaks_lab_deliver_parcel_pokedex"), false},
    {TEXT("old-man"), TEXT("viridian_old_man_catch_demo_first"), false},
    {TEXT("route22-rival"), TEXT("route22_rival_battle_1"), false},
    {TEXT("brock"), TEXT("pewter_gym_brock_tm34"), false},
    {TEXT("brock-battle"), TEXT("pewter_gym_brock_tm34"), true},
    {TEXT("mt-moon-rockets"), TEXT("mt_moon_jessie_james"), false},
    {TEXT("mt-moon-fossil"), TEXT("mt_moon_dome_fossil"), false},
    {TEXT("cerulean-rival"), TEXT("cerulean_rival_battle"), false},
    {TEXT("nugget-bridge"), TEXT("route24_nugget_bridge_prize"), false},
    {TEXT("bill"), TEXT("bills_house_bill_asks_help"), false},
    {TEXT("misty"), TEXT("cerulean_gym_misty_tm11"), false},
    {TEXT("misty-battle"), TEXT("cerulean_gym_misty_tm11"), true},
    {TEXT("ui-start"), TEXT("@ui-start"), false},
    {TEXT("ui-party"), TEXT("@ui-party"), false},
    {TEXT("ui-bag"), TEXT("@ui-bag"), false},
    {TEXT("ui-pokedex"), TEXT("@ui-pokedex"), false},
    {TEXT("ui-card"), TEXT("@ui-card"), false},
    // Looking out past the edges where Kanto goes on beyond the range (Route 21's sea, Route 23, Routes 9 and 5).
    {TEXT("edge-pallet-south"), TEXT("@edge:PALLET_TOWN:down"), false},
    {TEXT("edge-route22-north"), TEXT("@edge:ROUTE_22:up"), false},
    {TEXT("edge-cerulean-east"), TEXT("@edge:CERULEAN_CITY:right"), false},
    {TEXT("edge-cerulean-south"), TEXT("@edge:CERULEAN_CITY:down"), false},
};

struct FShotPlan { FString Name; FString Map; const FBeatShot* Beat = nullptr; };

TArray<FShotPlan> AllShots()
{
    TArray<FShotPlan> Out;
    for (const TCHAR* M : GMapIds) Out.Add({FString(TEXT("map-")) + M, M, nullptr});
    for (const FBeatShot& B : GBeats) Out.Add({FString(TEXT("beat-")) + B.Name, FString(), &B});
    return Out;
}

// "QA_Shot_<suffix>": map ids as they are (QA_Shot_PALLET_TOWN), beats with underscores (QA_Shot_beat_oak_intercept).
FString ShotTestName(const FShotPlan& S)
{
    return TEXT("QA_Shot_") + (S.Beat ? S.Name.Replace(TEXT("-"), TEXT("_")) : S.Map);
}

// The walkable tile nearest a map's middle (not a warp).
FIntPoint MiddleTile(const FYMap* M)
{
    FIntPoint Best(M->Width / 2, M->Height / 2);
    int32 BestD = MAX_int32;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            if (!Standable(M, X, Y)) continue;
            const int32 Dd = FMath::Square(X - M->Width / 2) + FMath::Square(Y - M->Height / 2);
            if (Dd < BestD) { BestD = Dd; Best = FIntPoint(X, Y); }
        }
    return Best;
}

// The walkable tile nearest a map's middle with nobody beside Red or in the two tiles ahead of him (the camera looks
// north over his shoulder: a person a step in front stands inside him in the picture). The plain middle if none is.
FIntPoint ClearMiddleTile(FGameTestContext& Test, const FYMap* M)
{
    TArray<FIntPoint> People;
    for (const TWeakObjectPtr<APYRPerson>& P : Field(Test)->People)
        if (P.IsValid() && P->IsShown() && P->Map == M->Id) People.Add(P->Tile - M->Origin);
    FIntPoint Best = MiddleTile(M);
    int32 BestD = MAX_int32;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            if (!Standable(M, X, Y)) continue;
            bool bClear = true;
            for (const FIntPoint& T : People)
                bClear &= !(FMath::Abs(T.X - X) <= 1 && T.Y - Y >= -1 && T.Y - Y <= 1) && !(T.X == X && (T.Y == Y - 2));
            if (!bClear) continue;
            const int32 Dd = FMath::Square(X - M->Width / 2) + FMath::Square(Y - M->Height / 2);
            if (Dd < BestD) { BestD = Dd; Best = FIntPoint(X, Y); }
        }
    return Best;
}

struct FShotRun
{
    TArray<FShotPlan> Shots;
    int32 I = 0, Phase = 0, Taken = 0, Unstaged = 0;
    double T0 = 0, ReadyAt = -1;
    FString Path, Note;
    FIntPoint Walk = FIntPoint::ZeroValue;
    bool bStaged = false, bPressed = false;
    int32 RanAt = 0;   // scripts that ran before this shot
    // QA::ShotCheck: judged where the picture would be taken, instead of taking it.
    TFunction<bool(FGameTestContext&, FString&)> Check;
};

// Clears the story for the next beat.
void Fresh(FGameTestContext& Test)
{
    Menus(Test)->CloseAll();
    StopWalking(Test);
    Progress(Test)->ResetToNewGame();
    // Pikachu at Yellow's starting level, and text at its fastest speed so each picture shows whole lines.
    Party(Test, {{TEXT("pikachu"), 5}});
    Progress(Test)->Options.TextSpeed = EPYRTextSpeed::Fast;
    PYRScripts::Refresh(Test.World);
}

// Sets up an events.json beat the way the story reaches it; returns the tile to walk to (or talk from) and facing.
bool StageEvent(FGameTestContext& Test, const FString& Id, FIntPoint& OutWalk, bool& bTalk, FString& Why)
{
    UPYRScripts* Sc = UPYRScripts::Get(Test.World);
    TSharedPtr<FJsonObject> E = Sc->Event(Id);
    if (!E.IsValid()) { Why = TEXT("not in events.json"); return false; }
    AfterPokedex(Test);
    UPYRProgress* G = Progress(Test);
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (E->TryGetArrayField(TEXT("requires"), A)) for (const TSharedPtr<FJsonValue>& V : *A) if (V->Type == EJson::String) G->Set(FName(*V->AsString()));
    if (E->TryGetArrayField(TEXT("requires_not"), A)) for (const TSharedPtr<FJsonValue>& V : *A) if (V->Type == EJson::String) G->Set(FName(*V->AsString()), false);
    if (Id == TEXT("oaks_lab_deliver_parcel_pokedex") || Id == TEXT("oaks_lab_rival_battle"))
    {
        for (const TCHAR* Fl : {TEXT("EVENT_GOT_OAKS_PARCEL"), TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB")}) G->Set(Fl, false);
        if (Id == TEXT("oaks_lab_deliver_parcel_pokedex")) { G->Set(TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB")); State(Test)->AddItem(UPYRTrainerState::OaksParcel, 1); }
    }
    if (Id == TEXT("viridian_mart_oaks_parcel")) for (const TCHAR* Fl : {TEXT("EVENT_GOT_OAKS_PARCEL"), TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")}) G->Set(Fl, false);
    if (Id == TEXT("viridian_old_man_catch_demo_first")) G->Set(TEXT("EVENT_COMPLETED_CATCH_TRAINING"), false);
    PYRScripts::Refresh(Test.World);
    const FString MapId = E->GetStringField(TEXT("map"));
    const FYMap* M = Data(Test)->Map(FName(*MapId));
    if (!M) { Why = TEXT("no map ") + MapId; return false; }
    const TArray<TSharedPtr<FJsonValue>>* Trig = nullptr;
    FString Npc;
    E->TryGetStringField(TEXT("npc"), Npc);
    if (Id == TEXT("viridian_mart_oaks_parcel"))
    {
        // Walked into the Mart from the street.
        const FYMap* City = Data(Test)->Map(TEXT("VIRIDIAN_CITY"));
        for (const FYWarp& Wp : City->Warps) if (Wp.ToMap == TEXT("VIRIDIAN_MART")) { Place(Test, City->Id, Wp.X, Wp.Y + 1, EYDir::Up); OutWalk = City->Origin + FIntPoint(Wp.X, Wp.Y); bTalk = false; return true; }
    }
    if (Id == TEXT("oaks_lab_rival_battle")) { Place(Test, TEXT("OAKS_LAB"), 5, 4, EYDir::Down); OutWalk = M->Origin + FIntPoint(5, 6); bTalk = false; return true; }
    if (E->TryGetArrayField(TEXT("trigger_coords"), Trig) && Trig->Num())
    {
        const TSharedPtr<FJsonObject> T0 = (*Trig)[0]->AsObject();
        const FIntPoint T(T0->GetIntegerField(TEXT("x")), T0->GetIntegerField(TEXT("y")));
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FIntPoint From = T - YDir::Delta(Dir);
            if (!Standable(M, From.X, From.Y)) continue;
            Place(Test, M->Id, From.X, From.Y, Dir);
            OutWalk = M->Origin + T;
            bTalk = false;
            return true;
        }
        Why = TEXT("no tile next to the trigger");
        return false;
    }
    APYRPerson* P = Person(Test, *MapId, *Npc);
    if (!P || !P->IsShown()) { Why = TEXT("no one to talk to: ") + Npc; return false; }
    if (!PlaceToTalk(Test, P)) { Why = TEXT("no tile to talk to ") + Npc; return false; }
    bTalk = true;
    return true;
}

EGameTest RunShots(FGameTestContext& Test, FShotRun& Run)
{
    UPYRMenus* Me = Menus(Test);
    UPYRField* F = Field(Test);
    APokemonYellowReimagineCharacter* R = Red(Test);
    UPYREncounters* En = UPYREncounters::Get(Test.World);
    UPYRScripts::Get(Test.World)->bSight = false;
    if (Run.I >= Run.Shots.Num())
    {
        if (QA::Strict() && Run.Unstaged) M4_FAIL(TEXT("%d shots, %d not staged"), Run.Taken, Run.Unstaged);
        M4_PASS(TEXT("%d shots taken (%d not staged as planned) in %s"), Run.Taken, Run.Unstaged, *QA::ShotDir());
    }
    const FShotPlan& S = Run.Shots[Run.I];
    const FString Kind = S.Beat ? S.Beat->Event : TEXT("@map");
    // Maps and screens keep the story still (no roadblock or map script starts behind the picture); beats play it.
    Quiet(true, Kind == TEXT("@map") || Kind.StartsWith(TEXT("@ui-")) || Kind.StartsWith(TEXT("@edge:")));
    if (Run.Phase == 3)
    {
        // After a beat's picture the scene is played out (text on, YES, fights won), so no script is left holding Red.
        if (!QA::Idle(Test) && Test.Seconds - Run.T0 < 90) { QA::Drive(Test, true); return EGameTest::Running; }
        Menus(Test)->CloseAll();
        ++Run.I;
        Run.Phase = 0;
        return EGameTest::Running;
    }
    if (Run.Phase == 0)
    {
        Fresh(Test);
        Run.bStaged = false;
        Run.bPressed = false;
        Run.RanAt = UPYRScripts::Get(Test.World)->Ran.Num();
        Run.Note.Reset();
        Run.ReadyAt = -1;
        Run.T0 = Test.Seconds;
        Run.Path = FPaths::ConvertRelativePathToFull(QA::ShotDir() / S.Name + TEXT(".png"));
        if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-22.f, -90.f, 0.f));
        if (Kind == TEXT("@map"))
        {
            const FYMap* M = Data(Test)->Map(FName(*S.Map));
            if (!M) { Run.Note = TEXT("no map"); Run.Phase = 2; return EGameTest::Running; }
            AfterPokedex(Test);
            PYRScripts::Refresh(Test.World);
            const FIntPoint T = ClearMiddleTile(Test, M);
            Place(Test, M->Id, T.X, T.Y, EYDir::Up);
            Quiet(true, true);
            Run.ReadyAt = Test.Seconds + 2.5;
            Run.bStaged = true;
        }
        else if (Kind == TEXT("@intro") || Kind == TEXT("@naming-player") || Kind == TEXT("@naming-rival"))
        {
            Progress(Test)->ResetToNewGame();
            State(Test)->DebugSetParty(TArray<FPYRMonSpec>());
            Place(Test, TEXT("REDS_HOUSE_2F"), 3, 6, EYDir::Up);
            PYRScreens::Intro(Test.World, nullptr);
        }
        else if (Kind == TEXT("@oak") || Kind == TEXT("@lab"))
        {
            Progress(Test)->ResetToNewGame();
            State(Test)->DebugSetParty(TArray<FPYRMonSpec>());
            PYRScripts::Refresh(Test.World);
            const FYMap* Pallet = Data(Test)->Map(TEXT("PALLET_TOWN"));
            int32 GX = 10;
            for (int32 X = 0; X < Pallet->Width; ++X) if (Pallet->Tile(X, 0) == TEXT('g')) { GX = X; break; }
            Place(Test, TEXT("PALLET_TOWN"), GX, 3, EYDir::Up);
        }
        else if (Kind.StartsWith(TEXT("@edge:")))
        {
            // Red two tiles inside the edge at its middle (the nearest walkable tile), the camera looking out.
            TArray<FString> Parts;
            Kind.ParseIntoArray(Parts, TEXT(":"));
            const FYMap* M = Parts.Num() == 3 ? Data(Test)->Map(FName(*Parts[1])) : nullptr;
            const EYDir Out = Parts.Num() == 3 ? YDir::FromName(Parts[2]) : EYDir::None;
            if (M && Out != EYDir::None)
            {
                // One or two tiles from the edge, nearest its middle, with three open tiles behind (the follow camera
                // sits there, so no roof comes between it and the edge).
                const FIntPoint D = YDir::Delta(Out);
                const FIntPoint Mid(M->Width / 2, M->Height / 2);
                FIntPoint Best = Out == EYDir::Down ? FIntPoint(Mid.X, M->Height - 2) : Out == EYDir::Up ? FIntPoint(Mid.X, 1)
                    : Out == EYDir::Left ? FIntPoint(1, Mid.Y) : FIntPoint(M->Width - 2, Mid.Y);
                // Where the edge leads to a route out of the range (Route 23 past Route 22), stand under that route,
                // up to six tiles in if nothing nearer is open (Route 22's gate fills its north side), so the picture
                // shows the route and not the border block beside it.
                FIntRect Span(0, 0, -1, -1);
                const FYConnection& C = M->Conn[int32(Out)];
                if (C.bValid && C.bOutOfScope)
                    for (const FYMap& Sc : Data(Test)->Scenery)
                        if (Sc.Id == C.Map) Span = FIntRect(Sc.WorldRect().Min - M->Origin, Sc.WorldRect().Max - M->Origin);
                const FIntPoint SpanMid((Span.Min.X + Span.Max.X) / 2, (Span.Min.Y + Span.Max.Y) / 2);
                int32 BestD = MAX_int32;
                for (int32 Y = 0; Y < M->Height; ++Y)
                    for (int32 X = 0; X < M->Width; ++X)
                    {
                        const int32 FromEdge = Out == EYDir::Down ? M->Height - 1 - Y : Out == EYDir::Up ? Y : Out == EYDir::Left ? X : M->Width - 1 - X;
                        const bool bInSpan = Span.Max.X > Span.Min.X && (D.X != 0 ? Y >= Span.Min.Y && Y < Span.Max.Y : X >= Span.Min.X && X < Span.Max.X);
                        if (FromEdge < 1 || FromEdge > (bInSpan ? 6 : 2) || !Standable(M, X, Y)) continue;
                        bool bOpen = true;
                        for (int32 K = 1; K <= 3 && bOpen; ++K) bOpen = Standable(M, X - D.X * K, Y - D.Y * K);
                        if (!bOpen) continue;
                        const FIntPoint Centre = bInSpan ? SpanMid : Mid;
                        const int32 Dd = (bInSpan ? FromEdge * 100 : 100000) + (D.X != 0 ? FMath::Abs(Y - Centre.Y) : FMath::Abs(X - Centre.X));
                        if (Dd < BestD) { BestD = Dd; Best = FIntPoint(X, Y); }
                    }
                AfterPokedex(Test);
                Place(Test, M->Id, Best.X, Best.Y, Out);
                if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-18.f, YDir::Yaw(Out), 0.f));
                Run.ReadyAt = Test.Seconds + 2.5;
                Run.bStaged = true;
            }
            else Run.Note = TEXT("no such map or direction");
        }
        else if (Kind.StartsWith(TEXT("@ui-")))
        {
            AfterPokedex(Test);
            State(Test)->AddItem(UPYRTrainerState::Potion, 2);
            State(Test)->AddItem(UPYRTrainerState::PokeBall, 5);
            Party(Test, {{TEXT("pikachu"), 12}, {TEXT("pidgey"), 9}});
            // The staged save's play time, as a journey this far would have (a fresh one reads TIME/0:00, visual-093).
            Progress(Test)->PlaySeconds = 2 * 3600 + 14 * 60;
            Place(Test, TEXT("PALLET_TOWN"), 9, 12, EYDir::Up);   // facing the houses and the lab (south is the sea past the fence)
            const FString U = Kind.Mid(4);
            if (U == TEXT("start")) PYRScreens::OpenStart(Test.World);
            else if (U == TEXT("party")) PYRScreens::OpenParty(Test.World);
            else if (U == TEXT("bag")) PYRScreens::OpenBag(Test.World);
            else if (U == TEXT("pokedex")) PYRScreens::OpenPokedex(Test.World);
            else if (U == TEXT("card")) PYRScreens::OpenTrainerCard(Test.World);
            Run.ReadyAt = Test.Seconds + 1.5;
            Run.bStaged = true;
        }
        else
        {
            FIntPoint Walk;
            bool bTalk = false;
            FString Why;
            if (!StageEvent(Test, Kind, Walk, bTalk, Why)) { Run.Note = Why; Run.ReadyAt = Test.Seconds + 1.0; }
            else
            {
                Run.Note = bTalk ? TEXT("talk") : TEXT("walk");
                Run.Walk = Walk;
            }
        }
        Run.Phase = 1;
        return EGameTest::Running;
    }
    if (Run.Phase == 1)
    {
        // Stage until the beat is on screen, then give it a moment to settle.
        const double Since = Test.Seconds - Run.T0;
        if (Run.ReadyAt < 0)
        {
            if (Kind == TEXT("@intro"))
            {
                if (TopId(Test) == TEXT("text") && Since > 1.5) { Run.bStaged = true; Run.ReadyAt = Test.Seconds + 1.5; }
            }
            else if (Kind == TEXT("@naming-player") || Kind == TEXT("@naming-rival"))
            {
                const FName Top = TopId(Test);
                const bool bRival = Kind == TEXT("@naming-rival");
                if (Top == TEXT("naming"))
                {
                    const FString Desc = Me->Top()->Describe();
                    const bool bIsRival = Desc.Contains(TEXT("RIVAL"));
                    if (bIsRival == bRival)
                    {
                        // Three letters in, the cursor left on the grid.
                        if (!Run.bStaged) { Me->Press(EPYRKey::Confirm); Me->Press(EPYRKey::Right); Me->Press(EPYRKey::Confirm); Me->Press(EPYRKey::Down); Me->Press(EPYRKey::Confirm); }
                        Run.bStaged = true;
                        Run.ReadyAt = Test.Seconds + 1.0;
                    }
                    else M4::Advance(Test);
                }
                else if (Top == TEXT("choice") || (Top != TEXT("text") && !Top.IsNone()))
                {
                    // The name menu: NEW NAME is the first entry.
                    TSharedPtr<FPYRScreen> L = Me->Top();
                    L->Cursor = 0;
                    Me->Press(EPYRKey::Confirm);
                }
                else Me->Press(EPYRKey::Confirm);
            }
            else if (Kind == TEXT("@oak") || Kind == TEXT("@lab"))
            {
                UPYRScripts* Sc = UPYRScripts::Get(Test.World);
                bool bOakRan = false;
                for (int32 K = Run.RanAt; K < Sc->Ran.Num(); ++K) bOakRan |= Sc->Ran[K] == TEXT("oak_stops_red");
                if (!bOakRan) WalkToTile(Test, TEXT("PALLET_TOWN"), F->CurrentTile.X, 0, 5.f);
                else
                {
                    StopWalking(Test);
                    const FString Screen = Me->ScreenText();
                    if (Kind == TEXT("@oak"))
                    {
                        // After "Hey! Wait!" Oak walks up: shoot at "Whew..." with him beside Red.
                        if (Screen.Contains(TEXT("Whew"))) { Run.bStaged = true; Run.ReadyAt = Test.Seconds + 2.0; }
                        else if (TopId(Test) == TEXT("text")) Me->Press(EPYRKey::Confirm);
                    }
                    else
                    {
                        if (F->CurrentMap == TEXT("OAKS_LAB") && TopId(Test) == TEXT("text") && Progress(Test)->Has(TEXT("EVENT_OAK_ASKED_TO_CHOOSE_MON"))) { Run.bStaged = true; Run.ReadyAt = Test.Seconds + 2.0; }
                        else if (TopId(Test) == TEXT("text") || TopId(Test) == TEXT("choice")) Me->Press(EPYRKey::Confirm);
                    }
                }
            }
            else
            {
                // An event: walk onto its trigger or press E, then shoot the first words (or the fight).
                if (Run.Note == TEXT("talk") && Since > .3 && !Run.bPressed) { PressE(Test); Run.bPressed = true; }
                else if (Run.Note == TEXT("walk") && QA::Idle(Test)) WalkTo(Test, World(Test)->KantoCenter(Run.Walk), 6.f);
                if (!QA::Idle(Test)) StopWalking(Test);
                if (S.Beat && S.Beat->bFight)
                {
                    if (En->GetPhase() == EPYREncounterPhase::Fight) { Run.bStaged = true; Run.ReadyAt = Test.Seconds + 2.5; }
                    else if (TopId(Test) == TEXT("text") || TopId(Test) == TEXT("choice")) Me->Press(EPYRKey::Confirm);
                }
                else if (TopId(Test) == TEXT("text") && Since > .6) { Run.bStaged = true; Run.ReadyAt = Test.Seconds + 2.0; }
            }
            if (Run.ReadyAt < 0 && Since > 30)
            {
                FString Locks;
                for (const TCHAR* L : {TEXT("warp"), TEXT("script"), TEXT("menu"), TEXT("battle"), TEXT("title")}) if (F->IsLockedBy(L)) Locks += FString(L) + TEXT(" ");
                Run.Note += FString::Printf(TEXT(" (never staged: screens '%s', locks '%s', script '%s', at %s)"), *Me->ScreenText().Left(120), *Locks,
                    *UPYRScripts::Get(Test.World)->RunningName(), *QA::TileText(Test, F->KantoTile));
                Run.ReadyAt = Test.Seconds;
            }
            return EGameTest::Running;
        }
        if (Test.Seconds < Run.ReadyAt) return EGameTest::Running;
        StopWalking(Test);
        if (Run.Check)
        {
            if (!Run.bStaged) M4_FAIL(TEXT("%s was never staged: %s"), *S.Name, *Run.Note);
            FString Why;
            const bool bOk = Run.Check(Test, Why);
            if (bOk) M4_PASS(TEXT("%s: %s"), *S.Name, *Why);
            M4_FAIL(TEXT("%s: %s"), *S.Name, *Why);
        }
        IFileManager::Get().Delete(*Run.Path, false, true, true);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Run.Path), true);
        FScreenshotRequest::RequestScreenshot(Run.Path, true, false);
        Run.Phase = 2;
        Run.T0 = Test.Seconds;
        return EGameTest::Running;
    }
    // Wait for the file (the request is served when the next frame is drawn).
    const bool bSaved = IFileManager::Get().FileSize(*Run.Path) > 0;
    if (!bSaved && Test.Seconds - Run.T0 < 10) return EGameTest::Running;
    TSharedRef<FJsonObject> Rc = QA::Rec(TEXT("shot"));
    Rc->SetStringField(TEXT("name"), S.Name);
    Rc->SetStringField(TEXT("test"), ShotTestName(S));
    Rc->SetStringField(TEXT("path"), bSaved ? Run.Path : FString());
    Rc->SetStringField(TEXT("map"), F->CurrentMap.ToString());
    Rc->SetArrayField(TEXT("tile"), QA::Point(F->CurrentTile));
    Rc->SetBoolField(TEXT("staged"), Run.bStaged);
    Rc->SetStringField(TEXT("note"), Run.Note);
    Rc->SetStringField(TEXT("screen"), Me->ScreenText().Left(600));
    if (R) Rc->SetBoolField(TEXT("red_hidden"), R->IsHidden());
    // Where the camera and Red were (a camera under the ground or inside a wall shows here).
    if (R) Rc->SetStringField(TEXT("red_at"), R->GetActorLocation().ToCompactString());
    if (APlayerController* PC = Test.World->GetFirstPlayerController())
        if (PC->PlayerCameraManager) Rc->SetStringField(TEXT("camera_at"), PC->PlayerCameraManager->GetCameraLocation().ToCompactString() + TEXT(" ") + PC->PlayerCameraManager->GetCameraRotation().ToCompactString());
    QA::Log(Rc);
    Run.Taken += bSaved ? 1 : 0;
    Run.Unstaged += Run.bStaged ? 0 : 1;
    Run.Phase = 3;
    Run.T0 = Test.Seconds;
    return EGameTest::Running;
}
}

GAME_TEST(QA_VisualTour, 2400)
{
    static FShotRun Run;
    M4_READY();
    if (Run.Shots.Num() == 0)
    {
        Run.Shots = AllShots();
        // -QAShots=map-PALLET_TOWN,beat-intro: only these (in tour order).
        FString Only;
        if (FParse::Value(FCommandLine::Get(), TEXT("QAShots="), Only, /*bShouldStopOnSeparator*/ false))
        {
            TArray<FString> Names;
            Only.ParseIntoArray(Names, TEXT(","), true);
            Run.Shots.RemoveAll([&Names](const FShotPlan& S) { return !Names.Contains(S.Name); });
        }
    }
    return RunShots(Test, Run);
}

// QA_Shot_<name>: one shot of the tour (the regression test a fix names).
namespace
{
EGameTest QAShotOne(FGameTestContext& Test)
{
    static FShotRun Run;
    M4_READY();
    if (Run.Shots.Num() == 0)
    {
        FString Name;
        FParse::Value(FCommandLine::Get(), TEXT("PYRAutoTest="), Name);
        Name.ReplaceInline(TEXT("."), TEXT("_"));
        for (const FShotPlan& S : AllShots()) if (ShotTestName(S) == Name) Run.Shots.Add(S);
        if (Run.Shots.Num() == 0) M4_FAIL(TEXT("no QA shot named %s"), *Name);
    }
    return RunShots(Test, Run);
}

}

EGameTest QA::ShotCheck(FGameTestContext& Test, const FString& Shot, TFunction<bool(FGameTestContext&, FString&)> Check)
{
    static FShotRun Run;
    M4_READY();
    if (Run.Shots.Num() == 0)
    {
        for (const FShotPlan& S : AllShots()) if (S.Name == Shot) Run.Shots.Add(S);
        if (Run.Shots.Num() == 0) M4_FAIL(TEXT("no visual tour shot named %s"), *Shot);
        Run.Check = MoveTemp(Check);
    }
    return RunShots(Test, Run);
}

namespace
{
struct FRegisterShots
{
    FRegisterShots() { for (const FShotPlan& S : AllShots()) GameTest::Register(*ShotTestName(S), &QAShotOne, 180); }
} GRegisterShots;
}
