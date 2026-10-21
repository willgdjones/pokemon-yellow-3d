// QA harness, part 2: the story playthrough. From a new game (Oak's intro and naming included) Red walks Yellow's
// story in order to Cerulean the way a player does: every step on foot along a planned path (doors, stairs, ledges and
// Mt. Moon's floors taken as they come), people talked to with E, trigger tiles walked onto, every fight on the way
// (sight trainers included) won through the battle test hooks. Nothing is set by hand unless a beat never happens, and
// then the record says it was forced.
//
// Logged (QA_REC): story_beat per beat, story_flag for every flag the moment it is set (story_flag_cleared if one is
// taken back), story_script for every script started, story_map for every map change, story_text for every page shown,
// naming and intro records, soft-locks, stuck walks and long warps, and story_end. the external checker checks the
// order and counts against Yellow's.
#include "Tests/QAKit.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRIntroStage.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "Widgets/SWidget.h"
#include "World/CoreSeams.h"

using namespace M4;

namespace
{
enum class EBeat : uint8 { Intro, Walk, Talk, Use, Trigger };

struct FBeat
{
    const TCHAR* Id;
    EBeat Kind;
    const TCHAR* Map;     // Walk/Use/Talk: the map; Trigger: unused (events.json has it)
    int32 X, Y;           // Walk/Use: the local tile (-1: the walkable tile nearest the map's middle)
    const TCHAR* Who;     // Talk: the person's id; Trigger: the events.json id
    const TCHAR* Done;    // the flag that means the beat happened ("": arriving is enough)
    EYDir Face;           // Use: the facing for E
    bool bOptional;       // Yellow lets a player skip it
    const TCHAR* Via = nullptr;  // an interior the way there goes through (the Cerulean thief's yard: the trashed house)
};

// Yellow's order from a new game to Cerulean (pokeyellow scripts/*.asm; the side events that Yellow allows in any order
// come where a player usually meets them).
const FBeat GBeats[] = {
    {TEXT("new-game"), EBeat::Intro, nullptr, 0, 0, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("leave-home"), EBeat::Walk, TEXT("PALLET_TOWN"), 5, 7, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("oak-intercept"), EBeat::Walk, TEXT("PALLET_TOWN"), -2, 1, nullptr, TEXT("EVENT_GOT_STARTER"), EYDir::None, false},
    {TEXT("lab-rival"), EBeat::Walk, TEXT("OAKS_LAB"), 5, 9, nullptr, TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), EYDir::None, false},
    {TEXT("oaks-parcel"), EBeat::Walk, TEXT("VIRIDIAN_MART"), 3, 5, nullptr, TEXT("EVENT_GOT_OAKS_PARCEL"), EYDir::None, false},
    {TEXT("pokedex"), EBeat::Talk, TEXT("OAKS_LAB"), 0, 0, TEXT("OAKSLAB_OAK1"), TEXT("EVENT_GOT_POKEDEX"), EYDir::None, false},
    {TEXT("town-map"), EBeat::Talk, TEXT("BLUES_HOUSE"), 0, 0, TEXT("BLUESHOUSE_DAISY1"), TEXT("EVENT_GOT_TOWN_MAP"), EYDir::None, true},
    {TEXT("old-man"), EBeat::Trigger, nullptr, 0, 0, TEXT("viridian_old_man_catch_demo_first"), TEXT("EVENT_COMPLETED_CATCH_TRAINING"), EYDir::None, false},
    {TEXT("route22-rival"), EBeat::Trigger, nullptr, 0, 0, TEXT("route22_rival_battle_1"), TEXT("EVENT_BEAT_ROUTE22_RIVAL_1ST_BATTLE"), EYDir::None, true},
    {TEXT("oak-poke-balls"), EBeat::Talk, TEXT("OAKS_LAB"), 0, 0, TEXT("OAKSLAB_OAK1"), TEXT("EVENT_GOT_POKEBALLS_FROM_OAK"), EYDir::None, true},
    {TEXT("viridian-forest"), EBeat::Walk, TEXT("VIRIDIAN_FOREST"), -1, -1, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("pewter"), EBeat::Walk, TEXT("PEWTER_CITY"), -1, -1, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("museum"), EBeat::Walk, TEXT("MUSEUM_1F"), -1, -1, nullptr, TEXT(""), EYDir::None, true},
    {TEXT("brock"), EBeat::Talk, TEXT("PEWTER_GYM"), 0, 0, TEXT("PEWTERGYM_BROCK"), TEXT("EVENT_BEAT_BROCK"), EYDir::None, false},
    {TEXT("route-3"), EBeat::Walk, TEXT("ROUTE_3"), -1, -1, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("mt-moon"), EBeat::Walk, TEXT("MT_MOON_1F"), -1, -1, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("mt-moon-super-nerd"), EBeat::Trigger, nullptr, 0, 0, TEXT("mt_moon_b2f_super_nerd_battle"), TEXT("EVENT_BEAT_MT_MOON_EXIT_SUPER_NERD"), EYDir::None, false},
    {TEXT("mt-moon-fossil"), EBeat::Talk, TEXT("MT_MOON_B2F"), 0, 0, TEXT("MTMOONB2F_DOME_FOSSIL"), TEXT("EVENT_GOT_DOME_FOSSIL"), EYDir::None, false},
    {TEXT("mt-moon-rockets"), EBeat::Trigger, nullptr, 0, 0, TEXT("mt_moon_jessie_james"), TEXT("EVENT_BEAT_MT_MOON_3_JESSIE_JAMES"), EYDir::None, false},
    {TEXT("cerulean"), EBeat::Walk, TEXT("CERULEAN_CITY"), -1, -1, nullptr, TEXT(""), EYDir::None, false},
    {TEXT("cerulean-rival"), EBeat::Trigger, nullptr, 0, 0, TEXT("cerulean_rival_battle"), TEXT("EVENT_BEAT_CERULEAN_RIVAL"), EYDir::None, false},
    {TEXT("nugget-bridge"), EBeat::Trigger, nullptr, 0, 0, TEXT("route24_nugget_bridge_prize"), TEXT("EVENT_BEAT_ROUTE24_ROCKET"), EYDir::None, false},
    {TEXT("bill"), EBeat::Talk, TEXT("BILLS_HOUSE"), 0, 0, TEXT("BILLSHOUSE_BILL_POKEMON"), TEXT("EVENT_BILL_SAID_USE_CELL_SEPARATOR"), EYDir::None, false},
    {TEXT("cell-separator"), EBeat::Use, TEXT("BILLS_HOUSE"), 1, 5, nullptr, TEXT("EVENT_USED_CELL_SEPARATOR_ON_BILL"), EYDir::Up, false},
    {TEXT("ss-ticket"), EBeat::Talk, TEXT("BILLS_HOUSE"), 0, 0, TEXT("BILLSHOUSE_BILL1"), TEXT("EVENT_GOT_SS_TICKET"), EYDir::None, false},
    {TEXT("misty"), EBeat::Talk, TEXT("CERULEAN_GYM"), 0, 0, TEXT("CERULEANGYM_MISTY"), TEXT("EVENT_BEAT_MISTY"), EYDir::None, false},
    {TEXT("rocket-thief"), EBeat::Trigger, nullptr, 0, 0, TEXT("cerulean_rocket_thief_battle"), TEXT("EVENT_BEAT_CERULEAN_ROCKET_THIEF"), EYDir::None, false, TEXT("CERULEAN_TRASHED_HOUSE")},
    {TEXT("end"), EBeat::Walk, TEXT("CERULEAN_CITY"), -1, -1, nullptr, TEXT(""), EYDir::None, false},
};

// Maps a player passes through on the way somewhere (warps may lead into these, or into the beat's own map).
const TCHAR* const GThrough[] = {TEXT("REDS_HOUSE_1F"), TEXT("ROUTE_2_GATE"), TEXT("VIRIDIAN_FOREST_SOUTH_GATE"), TEXT("VIRIDIAN_FOREST"),
    TEXT("VIRIDIAN_FOREST_NORTH_GATE"), TEXT("MT_MOON_1F"), TEXT("MT_MOON_B1F"), TEXT("MT_MOON_B2F")};

struct FStory
{
    int32 Beat = 0, Phase = 0, Tries = 0, Forced = 0, Failed = 0, SoftLocks = 0, Stuck = 0;
    double BeatT0 = 0, PhaseT0 = 0, BusySince = -1, WarpSince = -1, LastChange = 0;
    QA::FWalker Walker;
    FIntPoint Goal = FIntPoint::ZeroValue;
    EYDir GoalFace = EYDir::None;
    TArray<FName> FlagOrder;
    TSet<FName> Flags;
    int32 RanAt = 0, TextAt = 0, Encounters = 0, Blackouts = 0, Order = 0;
    FName LastMap;
    bool bIntroDone = false, bNamedPlayer = false, bNamedRival = false, bKeyboardTried = false, bIntroLogged = false;
    FString BusyNote;
    // The video's lingering on the opening (GLinger): the screen as last seen, since when, the last key, a name typed.
    FString LingerText;
    double LingerT0 = 0, LingerActed = 0;
    bool bGridTyped = false;
};

// m4s: the playthrough video (Showcase_Playthrough) lingers on each line of Oak's opening and on the naming screens, so
// they can be read: an opening screen stays up LingerSeconds of game time once it has stopped changing (the video plays
// the story about 1.6x fast: about 2 s of the video). The strict story tests go through it at once, as before.
bool GLinger = false;
constexpr double LingerSeconds = 3.2;

UPYRScripts* Scripts(FGameTestContext& Test) { return UPYRScripts::Get(Test.World); }

FIntPoint Middle(const FYMap* M)
{
    FIntPoint Best(M->Width / 2, M->Height / 2);
    int32 BestD = MAX_int32;
    for (int32 Y = 0; Y < M->Height; ++Y)
        for (int32 X = 0; X < M->Width; ++X)
        {
            if (YTile::IsSolid(M->Tile(X, Y)) || M->WarpAt(X, Y) != INDEX_NONE) continue;
            const int32 D = FMath::Square(X - M->Width / 2) + FMath::Square(Y - M->Height / 2);
            if (D < BestD) { BestD = D; Best = FIntPoint(X, Y); }
        }
    return Best;
}

void Record(FGameTestContext& Test, FStory& S, const TCHAR* Kind, TFunction<void(TSharedRef<FJsonObject>)> Fill)
{
    TSharedRef<FJsonObject> R = QA::Rec(Kind);
    R->SetNumberField(TEXT("t"), FMath::RoundToDouble(Test.Seconds * 10) / 10);
    R->SetStringField(TEXT("beat"), GBeats[FMath::Min(S.Beat, int32(UE_ARRAY_COUNT(GBeats)) - 1)].Id);
    if (const UPYRField* F = Field(Test))
    {
        R->SetStringField(TEXT("map"), F->CurrentMap.ToString());
        R->SetArrayField(TEXT("tile"), QA::Point(F->CurrentTile));
    }
    if (Fill) Fill(R);
    QA::Log(R);
}

// Everything that changed since the last tick: flags, scripts, maps, pages, fights.
void Watch(FGameTestContext& Test, FStory& S)
{
    UPYRProgress* P = Progress(Test);
    UPYRScripts* Sc = Scripts(Test);
    UPYRField* F = Field(Test);
    UPYRMenus* Me = Menus(Test);
    UPYREncounters* En = UPYREncounters::Get(Test.World);
    // Flags set in the same frame share one order number (a script sets several at once).
    bool bNewThisTick = false;
    for (const FName& Fl : P->GetFlags())
        if (!S.Flags.Contains(Fl))
        {
            S.Flags.Add(Fl);
            S.FlagOrder.Add(Fl);
            if (!bNewThisTick) { ++S.Order; bNewThisTick = true; }
            const int32 Order = S.Order;
            Record(Test, S, TEXT("story_flag"), [Fl, Order](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("flag"), Fl.ToString()); R->SetNumberField(TEXT("order"), Order); });
            S.LastChange = Test.Seconds;
        }
    for (const FName& Fl : S.Flags.Array())
        if (!P->Has(Fl))
        {
            S.Flags.Remove(Fl);
            Record(Test, S, TEXT("story_flag_cleared"), [Fl](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("flag"), Fl.ToString()); });
        }
    for (; S.RanAt < Sc->Ran.Num(); ++S.RanAt)
    {
        const FString Name = Sc->Ran[S.RanAt];
        Record(Test, S, TEXT("story_script"), [Name](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("name"), Name); });
        S.LastChange = Test.Seconds;
    }
    if (F->CurrentMap != S.LastMap)
    {
        const FName From = S.LastMap;
        S.LastMap = F->CurrentMap;
        Record(Test, S, TEXT("story_map"), [From](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("from"), From.ToString()); });
        S.LastChange = Test.Seconds;
    }
    for (; S.TextAt < Me->TextLog.Num(); ++S.TextAt)
    {
        const FString Text = Me->TextLog[S.TextAt];
        Record(Test, S, TEXT("story_text"), [Text](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("text"), Text); });
        S.LastChange = Test.Seconds;
    }
    if (En->Encounters != S.Encounters)
    {
        S.Encounters = En->Encounters;
        Record(Test, S, TEXT("story_fight"), [En](TSharedRef<FJsonObject> R) { R->SetNumberField(TEXT("count"), En->Encounters); });
        S.LastChange = Test.Seconds;
    }
    if (En->Blackouts != S.Blackouts)
    {
        S.Blackouts = En->Blackouts;
        Record(Test, S, TEXT("story_blackout"), nullptr);
    }
    // A warp that never lands.
    if (F->IsWarping()) { if (S.WarpSince < 0) S.WarpSince = Test.Seconds; }
    else S.WarpSince = -1;
    if (S.WarpSince >= 0 && Test.Seconds - S.WarpSince > 6)
    {
        Record(Test, S, TEXT("story_stuck_warp"), nullptr);
        S.WarpSince = Test.Seconds;
    }
}

// Letters of the naming grid (PYRScreens3 FNamingScreen, data/text/alphabets.asm: 9 columns, ED the last cell of row 5).
bool GridPos(TCHAR C, int32& Row, int32& Col)
{
    static const TCHAR* Rows[] = {TEXT("ABCDEFGHI"), TEXT("JKLMNOPQR"), TEXT("STUVWXYZ ")};
    for (int32 R = 0; R < 3; ++R) for (int32 X = 0; X < 9; ++X) if (Rows[R][X] == C) { Row = R; Col = X; return true; }
    return false;
}

// Reads "... [NAME] cursor r,c" from the naming screen.
bool NamingState(UPYRMenus* Me, FString& Name, int32& Row, int32& Col, bool& bEd)
{
    if (!Me->Top().IsValid() || Me->Top()->Id != TEXT("naming")) return false;
    const FString D = Me->Top()->Describe();
    const int32 Open = D.Find(TEXT("[")), Close = D.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    Name = Open != INDEX_NONE && Close > Open ? D.Mid(Open + 1, Close - Open - 1) : FString();
    const int32 At = D.Find(TEXT("cursor "));
    FString Rest = At != INDEX_NONE ? D.Mid(At + 7) : FString();
    FString A, B;
    Rest.Split(TEXT(","), &A, &B);
    Row = FCString::Atoi(*A);
    Col = FCString::Atoi(*B);
    bEd = D.EndsWith(TEXT(" ED"));
    return true;
}

// To ED (row 4, column 8) and A: the name typed on the grid is done.
void FinishOnGrid(UPYRMenus* Me)
{
    FString Cur; int32 Row = 0, Col = 0; bool bEd = false;
    for (int32 I = 0; I < 12 && NamingState(Me, Cur, Row, Col, bEd) && !bEd; ++I) Me->Press(Row != 4 ? (Row < 4 ? EPYRKey::Down : EPYRKey::Up) : EPYRKey::Right);
    if (NamingState(Me, Cur, Row, Col, bEd)) Me->Press(EPYRKey::Confirm);
}

// Types Name on the grid the Game Boy way (cursor moves and A), then ED (unless bFinish is false).
void TypeOnGrid(UPYRMenus* Me, const FString& Name, bool bFinish = true)
{
    FString Cur; int32 Row = 0, Col = 0; bool bEd = false;
    NamingState(Me, Cur, Row, Col, bEd);
    for (int32 I = 0; I < 8 && Cur.Len() > 0; ++I) { Me->Press(EPYRKey::Back); NamingState(Me, Cur, Row, Col, bEd); }
    for (const TCHAR C : Name)
    {
        int32 R = 0, X = 0;
        if (!GridPos(C, R, X) || !NamingState(Me, Cur, Row, Col, bEd)) continue;
        while (Row != R) { Me->Press(Row < R ? EPYRKey::Down : EPYRKey::Up); NamingState(Me, Cur, Row, Col, bEd); }
        while (Col != X) { Me->Press(Col < X ? EPYRKey::Right : EPYRKey::Left); NamingState(Me, Cur, Row, Col, bEd); }
        Me->Press(EPYRKey::Confirm);
    }
    if (bFinish) FinishOnGrid(Me);
}

void Key(FGameTestContext& Test, const FKey& K)
{
    if (APlayerController* PC = Test.World->GetFirstPlayerController())
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Pressed, 1.f));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Released, 0.f));
    }
}

int32 CountImages(const TSharedPtr<SWidget>& W)
{
    if (!W.IsValid()) return 0;
    int32 N = W->GetType() == FName(TEXT("SImage")) ? 1 : 0;
    if (FChildren* C = W->GetChildren())
        for (int32 I = 0; I < C->Num(); ++I) N += CountImages(C->GetChildAt(I));
    return N;
}

// The intro and naming: Oak's words, NEW NAME for both, the player's name first tried on the keyboard (a player at a
// PC types it), then entered on the grid; RED and GARY.
EGameTest TickIntro(FGameTestContext& Test, FStory& S)
{
    UPYRMenus* Me = Menus(Test);
    UPYRProgress* P = Progress(Test);
    if (S.Phase == 0)
    {
        Field(Test)->Lock(TEXT("title"));
        PYRScreens::Intro(Test.World, [&S, W = Test.World]() { S.bIntroDone = true; if (UPYRField* F = UPYRField::Get(W)) F->Unlock(TEXT("title")); PYRScripts::Refresh(W); });
        S.Phase = 1;
        S.PhaseT0 = Test.Seconds;
    }
    // What the opening shows, the moment it is up (its widgets are built on push).
    if (!S.bIntroLogged)
    {
        S.bIntroLogged = true;
        TSharedPtr<FPYRScreen> Page = Me->Find(TEXT("intro"));
        // The opening's pictures: images on its page and the people on Oak's 3D stage (they slide in over the first
        // second, so the stage's own count of loaded models stands for them).
        int32 Images = Page.IsValid() ? CountImages(Page->Widget) : -1;
        if (const AYellowIntroStage* Stage = AYellowIntroStage::Find(Test.World)) Images = FMath::Max(Images, 0) + Stage->ModelsLoaded();
        const FString Desc = Page.IsValid() ? Page->Describe() : FString();
        const bool bFull = Page.IsValid() && Page->IsFullScreen();
        Record(Test, S, TEXT("intro"), [Images, Desc, bFull](TSharedRef<FJsonObject> R)
        {
            R->SetNumberField(TEXT("page_images"), Images);
            R->SetStringField(TEXT("page_text"), Desc);
            R->SetBoolField(TEXT("full_screen"), bFull);
        });
    }
    if (S.bIntroDone && Me->TextLog.Num() && !Me->AnyOpen())
    {
        const FString Player = P->PlayerName, Rival = P->RivalName;
        Record(Test, S, TEXT("naming"), [Player, Rival](TSharedRef<FJsonObject> R)
        {
            R->SetStringField(TEXT("player"), Player);
            R->SetStringField(TEXT("rival"), Rival);
            R->SetStringField(TEXT("want_player"), TEXT("RED"));
            R->SetStringField(TEXT("want_rival"), TEXT("GARY"));
        });
        return EGameTest::Passed;
    }
    const FName Top = TopId(Test);
    if (GLinger && !Top.IsNone())
    {
        const FString Now = Top.ToString() + TEXT("|") + Me->ScreenText();
        if (Now != S.LingerText) { S.LingerText = Now; S.LingerT0 = Test.Seconds; }
        // Still settling (letters coming in), or not yet read; never more than 12 s on one screen.
        if (Test.Seconds - S.LingerT0 < LingerSeconds && Test.Seconds - S.LingerActed < 12.0) return EGameTest::Running;
        S.LingerActed = Test.Seconds;
    }
    if (Top == TEXT("naming"))
    {
        FString Name; int32 Row = 0, Col = 0; bool bEd = false;
        NamingState(Me, Name, Row, Col, bEd);
        const bool bRival = Me->Top()->Describe().Contains(TEXT("RIVAL"));
        if (!bRival && !S.bKeyboardTried)
        {
            // A player at a keyboard types the name and presses Enter.
            S.bKeyboardTried = true;
            Progress(Test)->PlayerName.Reset();   // so the record shows only what the typing gave
            for (const FKey& K : {EKeys::R, EKeys::E, EKeys::D, EKeys::Enter}) Key(Test, K);
            FString After; bool bEd2 = false;
            const bool bStill = NamingState(Me, After, Row, Col, bEd2);
            // Enter ends the name: the name Red now has.
            const FString Given = bStill ? After : Progress(Test)->PlayerName;
            Record(Test, S, TEXT("naming_keyboard"), [Given, bStill](TSharedRef<FJsonObject> R)
            {
                R->SetStringField(TEXT("typed"), TEXT("R E D Enter"));
                R->SetStringField(TEXT("want"), TEXT("RED"));
                R->SetStringField(TEXT("name_after"), Given);
                R->SetBoolField(TEXT("screen_open"), bStill);
            });
            if (!bStill) S.bNamedPlayer = true;
            return EGameTest::Running;
        }
        // For the video the name stays on the grid a moment before ED.
        if (GLinger && !S.bGridTyped)
        {
            TypeOnGrid(Me, bRival ? TEXT("GARY") : TEXT("RED"), false);
            S.bGridTyped = true;
            return EGameTest::Running;
        }
        if (GLinger) FinishOnGrid(Me);
        else TypeOnGrid(Me, bRival ? TEXT("GARY") : TEXT("RED"));
        S.bGridTyped = false;
        (bRival ? S.bNamedRival : S.bNamedPlayer) = true;
        return EGameTest::Running;
    }
    if (Top == TEXT("choice"))
    {
        // The name menu: NEW NAME (first) for both.
        Me->Top()->Cursor = 0;
        Me->Press(EPYRKey::Confirm);
        return EGameTest::Running;
    }
    if (!Top.IsNone()) Me->Press(EPYRKey::Confirm);
    // (The video's lingering takes its time over the opening.)
    if (Test.Seconds - S.PhaseT0 > (GLinger ? 400 : 90))
    {
        const FString Screen = Me->ScreenText();
        Record(Test, S, TEXT("story_softlock"), [Screen](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("screen"), Screen.Left(400)); });
        Me->CloseAll();
        Field(Test)->Unlock(TEXT("title"));
        return EGameTest::Failed;
    }
    return EGameTest::Running;
}

TSet<FName> AllowedFor(FGameTestContext& Test, const FBeat& B, FName Target)
{
    TSet<FName> Out;
    const FYellowDataSet* D = Data(Test);
    for (const FName& Id : D->MapOrder) if (const FYMap* M = D->Map(Id)) if (M->bOutdoor) Out.Add(Id);
    for (const TCHAR* T : GThrough) Out.Add(T);
    Out.Add(Target);
    if (B.Via) Out.Add(B.Via);
    return Out;
}

// Where a beat walks to (and faces), from its kind; false when it can't be worked out.
bool BeatGoal(FGameTestContext& Test, const FBeat& B, FIntPoint& Out, EYDir& Face, FName& TargetMap, FString& Why)
{
    const FYellowDataSet* D = Data(Test);
    Face = EYDir::None;
    if (B.Kind == EBeat::Trigger)
    {
        TSharedPtr<FJsonObject> E = Scripts(Test)->Event(B.Who);
        const TArray<TSharedPtr<FJsonValue>>* T = nullptr;
        if (!E.IsValid() || !E->TryGetArrayField(TEXT("trigger_coords"), T) || !T->Num()) { Why = TEXT("no trigger tile in events.json"); return false; }
        TargetMap = FName(*E->GetStringField(TEXT("map")));
        const TSharedPtr<FJsonObject> C = (*T)[0]->AsObject();
        Out = QA::K(Test, TargetMap, C->GetIntegerField(TEXT("x")), C->GetIntegerField(TEXT("y")));
        return true;
    }
    TargetMap = FName(B.Map);
    const FYMap* M = D->Map(TargetMap);
    if (!M) { Why = TEXT("no map"); return false; }
    if (B.Kind == EBeat::Talk)
    {
        APYRPerson* P = Person(Test, B.Map, B.Who);
        if (!P) { Why = FString::Printf(TEXT("no %s in %s"), B.Who, B.Map); return false; }
        if (!P->IsShown()) { Why = FString::Printf(TEXT("%s is not shown"), B.Who); return false; }
        return QA::TalkSpot(Test, P, Out, Face) || (Why = TEXT("no tile to talk from"), false);
    }
    FIntPoint L(B.X, B.Y);
    if (B.X == -1) L = Middle(M);
    if (B.X == -2)
    {
        // The first tall grass on the map's top rows.
        for (int32 X = 0; X < M->Width; ++X) if (M->Tile(X, 0) == TEXT('g')) { L = FIntPoint(X, B.Y); break; }
    }
    Out = M->Origin + L;
    Face = B.Face;
    return true;
}

void Force(FGameTestContext& Test, FStory& S, const FBeat& B, const FString& Why)
{
    // Records the failure, then moves the story on as the beat would have.
    ++S.Failed;
    UPYRMenus* Me = Menus(Test);
    const FString Screen = Me->ScreenText();
    const FString Running = Scripts(Test)->RunningName();
    Record(Test, S, TEXT("story_beat"), [&](TSharedRef<FJsonObject> R)
    {
        R->SetStringField(TEXT("id"), B.Id);
        R->SetBoolField(TEXT("ok"), false);
        R->SetBoolField(TEXT("optional"), B.bOptional);
        R->SetStringField(TEXT("why"), Why);
        R->SetStringField(TEXT("screen"), Screen.Left(400));
        R->SetStringField(TEXT("running"), Running);
        R->SetStringField(TEXT("want_flag"), B.Done);
        R->SetNumberField(TEXT("seconds"), FMath::RoundToDouble((Test.Seconds - S.BeatT0) * 10) / 10);
    });
    Me->CloseAll();
    StopWalking(Test);
    if (B.Done && *B.Done)
    {
        ++S.Forced;
        Progress(Test)->Set(FName(B.Done));
        if (B.Kind == EBeat::Trigger || B.Kind == EBeat::Talk)
        {
            // Everything else the event sets (its flags and badges).
            for (const TSharedPtr<FJsonObject>& E : Data(Test)->Events)
            {
                FString Id, Map, Npc;
                E->TryGetStringField(TEXT("id"), Id);
                E->TryGetStringField(TEXT("npc"), Npc);
                if (Id != B.Who && !(B.Kind == EBeat::Talk && Npc == B.Who)) continue;
                const TArray<TSharedPtr<FJsonValue>>* Sets = nullptr;
                if (E->TryGetArrayField(TEXT("sets"), Sets))
                    for (const TSharedPtr<FJsonValue>& V : *Sets)
                    {
                        if (V->Type == EJson::String) Progress(Test)->Set(FName(*V->AsString()));
                        else if (V->Type == EJson::Object)
                        {
                            FString Badge;
                            if (V->AsObject()->TryGetStringField(TEXT("badge"), Badge))
                                State(Test)->AwardBadge(Badge == TEXT("BOULDERBADGE") ? EPYRBadge::Boulder : EPYRBadge::Cascade);
                        }
                    }
            }
        }
        PYRScripts::Refresh(Test.World);
    }
    // Put Red where the beat ends so the next one starts from there.
    FIntPoint Goal;
    EYDir Face;
    FName Map;
    FString Ignored;
    if (BeatGoal(Test, B, Goal, Face, Map, Ignored))
        if (const FYMap* M = Data(Test)->Map(Map)) Place(Test, Map, Goal.X - M->Origin.X, Goal.Y - M->Origin.Y, Face == EYDir::None ? EYDir::Down : Face);
}

void NextBeat(FGameTestContext& Test, FStory& S, bool bOk, const FString& Note)
{
    const FBeat& B = GBeats[S.Beat];
    if (bOk)
    {
        Record(Test, S, TEXT("story_beat"), [&](TSharedRef<FJsonObject> R)
        {
            R->SetStringField(TEXT("id"), B.Id);
            R->SetBoolField(TEXT("ok"), true);
            R->SetBoolField(TEXT("optional"), B.bOptional);
            R->SetStringField(TEXT("note"), Note);
            R->SetNumberField(TEXT("tries"), S.Tries);
            R->SetNumberField(TEXT("seconds"), FMath::RoundToDouble((Test.Seconds - S.BeatT0) * 10) / 10);
        });
    }
    ++S.Beat;
    S.Phase = 0;
    S.Tries = 0;
    S.BeatT0 = S.PhaseT0 = Test.Seconds;
    S.BusySince = -1;
}
}

namespace
{
EGameTest RunStory(FGameTestContext& Test, bool bStrict)
{
    static FStory S;
    M4_READY();
    UPYRField* F = Field(Test);
    UPYRMenus* Me = Menus(Test);
    UPYRProgress* P = Progress(Test);
    Quiet(true, false);
    if (S.LastMap.IsNone())
    {
        S.LastMap = F->CurrentMap;
        S.BeatT0 = S.PhaseT0 = S.LastChange = Test.Seconds;
        Record(Test, S, TEXT("story_start"), [P](TSharedRef<FJsonObject> R) { R->SetNumberField(TEXT("flags"), P->GetFlags().Num()); });
    }
    Watch(Test, S);
    if (S.Beat >= int32(UE_ARRAY_COUNT(GBeats)))
    {
        const FString End = F->CurrentMap.ToString();
        const int32 Flags = S.FlagOrder.Num();
        Record(Test, S, TEXT("story_end"), [Flags](TSharedRef<FJsonObject> R)
        {
            R->SetNumberField(TEXT("forced"), S.Forced);
            R->SetNumberField(TEXT("failed"), S.Failed);
            R->SetNumberField(TEXT("softlocks"), S.SoftLocks);
            R->SetNumberField(TEXT("stuck"), S.Stuck);
            R->SetNumberField(TEXT("flags"), Flags);
            R->SetNumberField(TEXT("fights"), S.Encounters);
        });
        const FString Summary = FString::Printf(TEXT("%d beats, %d failed (%d forced), %d soft-locks, %d stuck walks, %d fights, ended in %s"),
            int32(UE_ARRAY_COUNT(GBeats)), S.Failed, S.Forced, S.SoftLocks, S.Stuck, S.Encounters, *End);
        if ((bStrict || QA::Strict()) && (S.Failed || S.Forced || S.SoftLocks || End != TEXT("CERULEAN_CITY"))) M4_FAIL(TEXT("%s"), *Summary);
        // Strict: every flag set once, in Yellow's order (a flag cleared and set again shows twice).
        if (bStrict)
        {
            TSet<FName> Once;
            for (const FName& Fl : S.FlagOrder) if (Once.Contains(Fl)) M4_FAIL(TEXT("%s set twice (%s)"), *Fl.ToString(), *Summary); else Once.Add(Fl);
        }
        M4_PASS(TEXT("%s"), *Summary);
    }
    const FBeat& B = GBeats[S.Beat];
    if (B.Kind == EBeat::Intro)
    {
        const EGameTest R = TickIntro(Test, S);
        if (R == EGameTest::Passed) NextBeat(Test, S, true, FString::Printf(TEXT("named %s and %s"), *P->PlayerName, *P->RivalName));
        else if (R == EGameTest::Failed) { Force(Test, S, B, TEXT("the intro never finished")); ++S.Beat; S.Phase = 0; }
        return EGameTest::Running;
    }
    const bool bDone = B.Done && *B.Done && P->Has(FName(B.Done));
    // Busy: text, a choice, a script or a fight. The player gets through it (YES to questions).
    if (!QA::Idle(Test))
    {
        StopWalking(Test);
        if (S.BusySince < 0) { S.BusySince = Test.Seconds; S.LastChange = Test.Seconds; }
        QA::Drive(Test, true);
        // A soft-lock: busy with nothing changing for a minute.
        if (Test.Seconds - S.LastChange > 60)
        {
            ++S.SoftLocks;
            const FString Screen = Me->ScreenText(), Running = Scripts(Test)->RunningName();
            const bool bLocked = F->IsLocked();
            Record(Test, S, TEXT("story_softlock"), [Screen, Running, bLocked](TSharedRef<FJsonObject> R)
            {
                R->SetStringField(TEXT("screen"), Screen.Left(400));
                R->SetStringField(TEXT("running"), Running);
                R->SetBoolField(TEXT("locked"), bLocked);
            });
            Me->CloseAll();
            S.LastChange = Test.Seconds;
        }
        return EGameTest::Running;
    }
    S.BusySince = -1;
    if (bDone) { NextBeat(Test, S, true, FString()); return EGameTest::Running; }
    if (Test.Seconds - S.BeatT0 > 420) { Force(Test, S, B, TEXT("timed out after 420 s")); NextBeat(Test, S, false, FString()); return EGameTest::Running; }
    if (S.Phase == 0)
    {
        FName Target;
        FString Why;
        if (!BeatGoal(Test, B, S.Goal, S.GoalFace, Target, Why)) { Force(Test, S, B, Why); NextBeat(Test, S, false, FString()); return EGameTest::Running; }
        S.Walker.Allowed = AllowedFor(Test, B, Target);
        S.Walker.bUseAllowed = true;
        if (!S.Walker.Start(Test, S.Goal))
        {
            Force(Test, S, B, S.Walker.Why);
            NextBeat(Test, S, false, FString());
            return EGameTest::Running;
        }
        S.Phase = 1;
        S.PhaseT0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (S.Phase == 1)
    {
        const QA::FWalker::EState W = S.Walker.Tick(Test);
        if (W == QA::FWalker::EState::Walking) return EGameTest::Running;
        if (W != QA::FWalker::EState::Arrived)
        {
            ++S.Stuck;
            const FString Why = S.Walker.Why;
            Record(Test, S, TEXT("story_stuck"), [Why](TSharedRef<FJsonObject> R) { R->SetStringField(TEXT("why"), Why); });
            if (++S.Tries > 3) { Force(Test, S, B, Why); NextBeat(Test, S, false, FString()); return EGameTest::Running; }
            S.Phase = 0;
            return EGameTest::Running;
        }
        // Arrived: a walk beat is done unless it waits for a flag; the others act.
        if (B.Kind == EBeat::Walk)
        {
            if (!B.Done || !*B.Done) { NextBeat(Test, S, true, QA::TileText(Test, F->KantoTile)); return EGameTest::Running; }
            S.Phase = 3;
            S.PhaseT0 = Test.Seconds;
            return EGameTest::Running;
        }
        if (B.Kind == EBeat::Talk || B.Kind == EBeat::Use)
        {
            QA::Face(Test, S.GoalFace);
            S.Phase = 2;
            S.PhaseT0 = Test.Seconds;
            return EGameTest::Running;
        }
        S.Phase = 3;
        S.PhaseT0 = Test.Seconds;
        return EGameTest::Running;
    }
    if (S.Phase == 2)
    {
        if (Test.Seconds - S.PhaseT0 < .3) return EGameTest::Running;
        PressE(Test);
        S.Phase = 3;
        S.PhaseT0 = Test.Seconds;
        return EGameTest::Running;
    }
    // Phase 3: acted, idle again, the flag not set. Give it a moment, then try again (a walker stepped away, a
    // trigger wants stepping off and on); after three tries the beat failed.
    if (Test.Seconds - S.PhaseT0 < 1.5) return EGameTest::Running;
    if (++S.Tries > 3)
    {
        Force(Test, S, B, FString::Printf(TEXT("%s never set after %d tries at %s"), B.Done, S.Tries - 1, *QA::TileText(Test, F->KantoTile)));
        NextBeat(Test, S, false, FString());
        return EGameTest::Running;
    }
    if (B.Kind == EBeat::Trigger || B.Kind == EBeat::Walk)
    {
        // Step off the tile (back the way Red came, or any open side) and walk on again.
        const AYellowWorld* W = World(Test);
        for (const EYDir Dir : {EYDir::Down, EYDir::Up, EYDir::Left, EYDir::Right})
        {
            const FIntPoint Off = F->KantoTile + YDir::Delta(Dir);
            if (W->CanMove(F->KantoTile, Dir) && !QA::PeopleTiles(Test).Contains(Off))
            {
                const FYMap* M = W->MapAtKanto(Off);
                Place(Test, M->Id, Off.X - M->Origin.X, Off.Y - M->Origin.Y, YDir::Opposite(Dir));
                break;
            }
        }
    }
    S.Phase = 0;
    return EGameTest::Running;
}
}

GAME_TEST(QA_StoryPlaythrough, 5400) { return RunStory(Test, false); }

// m4q: Yellow's story from a new game to Cerulean, strictly: Oak stops Red at the grass and catches Pikachu, the lab
// fight, the Parcel then the Pokedex, the old man's lesson, Route 22, the forest, Pewter's museum, Brock, Route 3,
// Mt. Moon (Jessie and James, the fossil), Cerulean (the rival on the way, Nugget Bridge, Bill, Misty, the thief);
// every beat on foot and happening by itself (none forced), every flag once, no soft-lock, ending in Cerulean.
GAME_TEST(Story_FullPlaythroughToCerulean, 1800) { return RunStory(Test, true); }

// m4q: the playthrough video. The same strict story, run rendered with the UI showing (run it rendered, not with -NullRHI);
// Tools/showcase_video.py records it from the package with -dumpmovie.
// m4s: it lingers on the opening's lines and the naming screens (GLinger), so the video shows them.
GAME_TEST(Showcase_Playthrough, 1800) { GLinger = true; return RunStory(Test, true); }
