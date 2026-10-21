// m4u: on a phone the stream page's controls never cover the battle HUD, the party list or the party action menu
// (Hud/PYRPhoneLayout), and the Desktop layout is unchanged.
// Each phone test runs the game at 844 x 390 (asked through the engine's resolution change, then checked: a window the
// system refused to size fails loudly), switches touch on with the stream page's own PYRTouch message, measures the
// page's controls from the in-game touch guide's widgets and every protected part from its own widget, both after two
// ticks or more, and asserts zero overlap. Input goes in as the page sends it: the key of the control tapped.
// Each test saves a screenshot named after it next to its log.
#include "Tests/M4UTests.h"

#include "Battle/BattleTestKit.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Battle/PYRBattleHUD.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameTest.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "Hud/PYRHud.h"
#include "Hud/PYRPhoneLayout.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRMoveLearning.h"
#include "Menus/PYRScreens.h"
#include "Misc/Paths.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "Tests/M4TestKit.h"
#include "UnrealClient.h"
#include "UnrealEngine.h"
#include "Widgets/SViewport.h"
#include "World/CoreSeams.h"
#include "Yellow/YellowData.h"

namespace M4U
{
const FVector2f Phone(844, 390);
const TCHAR* const TouchMessage = TEXT("{\"type\":\"PYRTouch\",\"touch\":true,\"width\":844,\"height\":390}");
const TCHAR* const Story[] = {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"), TEXT("EVENT_GOT_OAKS_PARCEL"),
    TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")};

using FRects = TArray<TPair<FString, FBox2f>>;

// ------------------------------------------------------------------------------------------------ input as the page sends it
void Key(UWorld* World, const FKey& K, EInputEvent Event)
{
    if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Event, Event == IE_Released ? 0.f : 1.f));
}

void Tap(UWorld* World, const FKey& K)
{
    Key(World, K, IE_Pressed);
    Key(World, K, IE_Released);
}

// The key a page button sends (touch-layout.json), by its label.
FKey ButtonKey(const FString& Label)
{
    for (const FPYRTouchControl& C : PYRPhoneLayout::ControlsFor(Phone, Phone, true)) if (C.Label == Label) return C.Key;
    return EKeys::Invalid;
}

// ------------------------------------------------------------------------------------------------ measuring
// Absolute (screen) space to viewport px, through the game viewport widget's own geometry.
bool ToView(FBox2f Abs, FBox2f& Out)
{
    if (!GEngine || !GEngine->GameViewport) return false;
    TSharedPtr<SViewport> VW = GEngine->GameViewport->GetGameViewportWidget();
    const FVector2f Px = PYRPhoneLayout::ViewportSize();
    if (!VW.IsValid() || Px.X <= 0) return false;
    const FGeometry& G = VW->GetCachedGeometry();
    const FVector2f Pos = FVector2f(G.GetAbsolutePosition()), Size = FVector2f(G.GetAbsoluteSize());
    if (Size.X < 1 || Size.Y < 1) return false;
    const FVector2f K = Px / Size;
    Out = FBox2f((Abs.Min - Pos) * K, (Abs.Max - Pos) * K);
    return true;
}

bool WidgetRect(const TSharedPtr<SWidget>& W, FBox2f& Out)
{
    if (!W.IsValid()) return false;
    const FGeometry& G = W->GetCachedGeometry();
    const FVector2f Pos = FVector2f(G.GetAbsolutePosition()), Size = FVector2f(G.GetAbsoluteSize());
    if (Size.X < .5f || Size.Y < .5f) return false;
    return ToView(FBox2f(Pos, Pos + Size), Out);
}

FString Str(const FBox2f& B) { return FString::Printf(TEXT("(%.0f,%.0f %.0fx%.0f)"), B.Min.X, B.Min.Y, B.GetSize().X, B.GetSize().Y); }

float OverlapArea(const FBox2f& A, const FBox2f& B)
{
    const float W = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
    const float H = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
    return W > 0 && H > 0 ? W * H : 0.f;
}

bool Inside(const FBox2f& B, FVector2f View) { return B.Min.X >= -.5f && B.Min.Y >= -.5f && B.Max.X <= View.X + .5f && B.Max.Y <= View.Y + .5f; }

// The page's controls as the touch guide draws them (viewport px), each checked against where the model puts it.
FString Controls(FRects& Out)
{
    Out.Reset();
    const auto Guide = PYRPhoneLayout::GuideWidgets();
    if (Guide.Num() == 0) return TEXT("no touch guide on screen (touch is not on?)");
    for (const auto& G : Guide)
    {
        FBox2f R;
        if (!WidgetRect(G.Value, R)) return FString::Printf(TEXT("the guide's %s is not laid out"), *G.Key.Label);
        if (R.Min.Equals(G.Key.View.Min, 1.5f) == false || R.Max.Equals(G.Key.View.Max, 1.5f) == false)
            return FString::Printf(TEXT("the guide's %s sits at %s, the page's model at %s"), *G.Key.Label, *Str(R), *Str(G.Key.View));
        Out.Add({G.Key.Label, R});
    }
    return FString();
}

// Every protected rectangle clear of every control (zero area in common) and, with bInside, fully on screen.
FString Clear(const FRects& Protected, const FRects& Ctrls, bool bInside)
{
    const FVector2f View = PYRPhoneLayout::ViewportSize();
    for (const auto& P : Protected)
    {
        if (bInside && !Inside(P.Value, View)) return FString::Printf(TEXT("%s %s is not fully on the %.0fx%.0f screen"), *P.Key, *Str(P.Value), View.X, View.Y);
        for (const auto& C : Ctrls)
        {
            const float A = OverlapArea(P.Value, C.Value);
            if (A > 0) return FString::Printf(TEXT("%s %s is under the page's %s %s (%.0f px2)"), *P.Key, *Str(P.Value), *C.Key, *Str(C.Value), A);
        }
    }
    return FString();
}

FString Widgets(const TArray<TPair<FString, TSharedPtr<SWidget>>>& In, const FString& Prefix, FRects& Out)
{
    for (const auto& W : In)
    {
        if (!Prefix.IsEmpty() && !W.Key.StartsWith(Prefix)) continue;
        FBox2f R;
        if (!WidgetRect(W.Value, R)) return FString::Printf(TEXT("%s is not laid out"), *W.Key);
        Out.Add({W.Key, R});
    }
    return FString();
}

FString HudRects(APYRBattleManager* Mgr, FRects& Out)
{
    TSharedPtr<SPYRBattleHUD> Hud = Mgr ? Mgr->GetHud() : nullptr;
    if (!Hud.IsValid()) return TEXT("no battle HUD on the viewport");
    for (const auto& R : Hud->ProtectedRects())
    {
        FBox2f V;
        if (!ToView(R.Value, V)) return TEXT("the viewport widget has no geometry");
        Out.Add({R.Key, V});
    }
    return Out.Num() ? FString() : FString(TEXT("the HUD reports nothing (not painted yet?)"));
}

bool Has(const FRects& Rects, const FString& Name)
{
    for (const auto& R : Rects) if (R.Key.StartsWith(Name)) return true;
    return false;
}

// ------------------------------------------------------------------------------------------------ screenshots
FString LogDir() { return FPaths::GetPath(FPlatformOutputDevices::GetAbsoluteLogFilename()); }

// Asks for a screenshot (the UI included) the first time, then true once the file is there.
bool Shot(const FString& Name, bool& bAsked, FString& Path)
{
    Path = FPaths::Combine(LogDir(), Name + TEXT(".png"));
    if (!bAsked)
    {
        bAsked = true;
        IFileManager::Get().Delete(*Path, false, true, true);
        FScreenshotRequest::RequestScreenshot(Path, true, false);
        return false;
    }
    return IFileManager::Get().FileSize(*Path) > 0;
}

// ------------------------------------------------------------------------------------------------ staging
enum class EScene : uint8 { Overworld, Battle, PartyList, PartyMenu, Wheel };

struct FStage
{
    int32 Step = 0;
    double Clock = 0;
    int32 Ticks = 0;          // ticks in this step
    FString Error;
    FVector2f View = FVector2f::ZeroVector;
    // The stream case: the game stays 16:9 (1280 x 720, as the stream renders it) and the page is a phone's 844 x 390.
    bool bStream = false;
    FVector2f Want() const { return bStream ? FVector2f(1280, 720) : Phone; }
    void Next(int32 To) { Step = To; Clock = 0; Ticks = 0; }
};

FPYRMonSpec Spec(FName Species, int32 Level, TArray<FName> Moves = {})
{
    FPYRMonSpec S;
    S.Species = Species;
    S.Level = Level;
    S.Moves = MoveTemp(Moves);
    return S;
}

// Sets the scene up a step a tick: Red on Route 1 after the Pokedex, the viewport at 844 x 390, touch on, then the scene.
// True once it is ready (laid out for at least two ticks); Error says why it can't be.
bool Stage(UObject* Ctx, EScene Scene, FStage& S, float Dt)
{
    S.Clock += Dt;
    ++S.Ticks;
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    UPYRField* Field = UPYRField::Get(Ctx);
    UPYRTrainerState* State = UPYRTrainerState::Get(Ctx);
    UPYRProgress* Progress = UPYRProgress::Get(Ctx);
    UPYREncounters* Enc = UPYREncounters::Get(Ctx);
    UPYRMenus* Menus = UPYRMenus::Get(Ctx);
    APYRBattleManager* Mgr = APYRBattleManager::Get(Ctx);
    if (!World || !Field || !State || !Progress || !Enc || !Menus || !Mgr || !UYellowData::Data(Ctx)) return false;
    auto Fail = [&S](const FString& Why) { S.Error = Why; return false; };
    switch (S.Step)
    {
    case 0:
    {
        for (const TCHAR* F : Story) Progress->Set(F);
        PYRTestFlags::bNoEncounters = true;   // nothing but the staged fight
        PYRTestFlags::bNoBlocks = true;
        PYRScripts::Refresh(Ctx);
        State->SetItemCount(UPYRTrainerState::Potion, 3);
        State->SetItemCount(UPYRTrainerState::PokeBall, 5);
        const TArray<FName> Four = {PYRData::ThunderShock, PYRData::Growl, PYRData::TailWhip, PYRData::QuickAttack};
        if (Scene == EScene::Battle)
        {
            // One point short of Lv 8, so a knock-out opens the learn prompt over the fight too.
            State->DebugSetParty({Spec(PYRData::Pikachu, 7, Four)});
            State->Party[0].Xp = PYRData::XpForLevel(PYRData::Pikachu, 8) - 1;
        }
        else
            State->DebugSetParty({Spec(PYRData::Pikachu, 12, Four), Spec(PYRData::Pidgey, 9), Spec(PYRData::Rattata, 8), Spec(PYRData::Caterpie, 6),
                Spec(PYRData::Weedle, 6), Spec(PYRData::Spearow, 7)});
        Field->PlaceRed(TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        // The phone's size, then touch on as the stream page reports it.
        if (!S.bStream) FSystemResolution::RequestResolutionChange(int32(Phone.X), int32(Phone.Y), EWindowMode::Windowed);
        PYRPhoneLayout::HandleStreamMessage(TouchMessage);
        S.Next(1);
        return false;
    }
    case 1:
        S.View = PYRPhoneLayout::ViewportSize();
        if (S.View != S.Want())
        {
            if (S.Clock > 20) return Fail(FString::Printf(TEXT("the viewport stayed %.0fx%.0f, not %.0fx%.0f"), S.View.X, S.View.Y, S.Want().X, S.Want().Y));
            return false;
        }
        if (S.Clock < 1.0) return false;   // the world settles at its new size
        if (Scene == EScene::Overworld) { S.Next(9); return false; }
        if (Scene == EScene::Battle || Scene == EScene::Wheel)
        {
            FMath::RandInit(4242);
            FMath::SRandInit(4242);
            if (!Enc->BeginWild(PYRData::Pidgey, 3)) return Fail(TEXT("the wild Pidgey did not start"));
            S.Next(2);
            return false;
        }
        Tap(World, ButtonKey(TEXT("START")));
        S.Next(3);
        return false;
    case 2:
        if (!(Enc->GetPhase() == EPYREncounterPhase::Fight && Mgr->IsBattleActive() && Mgr->GetHud().IsValid()))
            return S.Clock > 20 ? Fail(TEXT("the wild fight never started")) : false;
        Mgr->bFoePassive = true;
        if (S.Clock < 2.5) return false;   // the ring formed, the camera up
        if (Scene == EScene::Wheel) { Tap(World, ButtonKey(TEXT("Party"))); S.Next(6); return false; }
        S.Next(9);
        return false;
    case 3:
    {
        if (!Menus->IsOpen(TEXT("start"))) return S.Clock > 5 ? Fail(TEXT("START did not open the menu")) : false;
        const TArray<FString> Entries = PYRScreens::StartEntries(Ctx);
        const int32 At = Entries.IndexOfByPredicate([](const FString& E) { return E.StartsWith(TEXT("POKéMON")); });
        if (At == INDEX_NONE) return Fail(TEXT("no POKéMON on the START menu"));
        for (int32 I = 0; I < At; ++I) Tap(World, PYRPhoneLayout::StickKey(1));
        Tap(World, ButtonKey(TEXT("A / Talk")));
        S.Next(4);
        return false;
    }
    case 4:
        if (!Menus->IsOpen(TEXT("party"))) return S.Clock > 5 ? Fail(FString::Printf(TEXT("POKéMON did not open the party (%s)"), *Menus->ScreenText())) : false;
        if (Scene == EScene::PartyMenu) { Tap(World, ButtonKey(TEXT("A / Talk"))); S.Next(5); return false; }
        S.Next(9);
        return false;
    case 5:
        if (!Menus->IsOpen(TEXT("party-sub"))) return S.Clock > 5 ? Fail(TEXT("A on the first Pokemon did not open STATS/SWITCH/CANCEL")) : false;
        S.Next(9);
        return false;
    case 6:
        if (!Mgr->IsWheelOpen()) return S.Clock > 5 ? Fail(TEXT("Party did not open the switch menu")) : false;
        S.Next(9);
        return false;
    default:
        // Laid out for two ticks or more.
        return S.Clock >= .6 && S.Ticks >= 3;
    }
}

// The test's state: the staging, its own steps, the screenshot.
struct FTest
{
    FStage S;
    int32 Step = 0;
    double T0 = 0, Last = 0;
    bool bShot = false, bShot2 = false;
    FString Note, ShotPath;
    FVector P0 = FVector::ZeroVector;
};

float Dt(FGameTestContext& Test, FTest& St)
{
    const float D = St.Last > 0 ? float(Test.Seconds - St.Last) : 0.f;
    St.Last = Test.Seconds;
    return D;
}

FString ViewNote(const FTest& St) { return FString::Printf(TEXT("viewport %.0fx%.0f (asked 844x390)"), St.S.View.X, St.S.View.Y); }
}

using namespace M4U;

#define M4U_STAGE(Scene) \
    if (St.Step == 0) \
    { \
        if (!Stage(Test.World, Scene, St.S, Dt(Test, St))) \
        { \
            if (!St.S.Error.IsEmpty()) M4_FAIL(TEXT("staging: %s"), *St.S.Error); \
            return EGameTest::Running; \
        } \
        if (!PYRPhoneLayout::IsPhone()) M4_FAIL(TEXT("the layout is %s at 844x390 with touch on"), PYRPhoneLayout::Name(PYRPhoneLayout::Current())); \
        St.Step = 1; St.T0 = Test.Seconds; \
    } \
    Dt(Test, St)

// ------------------------------------------------------------------------------------------------ the battle HUD
GAME_TEST(M4uPhoneBattleHudClear, 150)
{
    static FTest St;
    M4_READY();
    M4U_STAGE(EScene::Battle);
    UWorld* W = Test.World;
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    UPYRTrainerState* State = UPYRTrainerState::Get(W);
    if (St.Step == 1)
    {
        FRects Ctrls, Hud;
        FString Why = Controls(Ctrls);
        if (Why.IsEmpty()) Why = HudRects(Mgr, Hud);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        for (const TCHAR* Need : {TEXT("foe card"), TEXT("player card"), TEXT("move 1"), TEXT("move 2"), TEXT("move 3"), TEXT("move 4"), TEXT("RUN"), TEXT("items")})
            if (!Has(Hud, Need)) M4_FAIL(TEXT("the HUD has no %s"), Need);
        Why = Clear(Hud, Ctrls, true);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        St.Note = FString::Printf(TEXT("%s; %d HUD parts (both cards with name, level and HP, 4 moves, items, RUN, dodge, swap) on screen and clear of %d page controls; "),
            *ViewNote(St), Hud.Num(), Ctrls.Num());
        // Move 1 through the page's button: its key, as the page sends it. The foe can't fall to it.
        if (APYRMonPawn* F = Mgr->GetFoeMon()) F->Hp = F->MaxHp = 999.f;
        const FKey One = ButtonKey(TEXT("1"));
        if (!One.IsValid()) M4_FAIL(TEXT("the page has no button 1"));
        Tap(W, One);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        const APYRMonPawn* P = Mgr->GetPlayerMon();
        if (!P || Mgr->GetCooldownFraction(0) <= 0.f) { if (Test.Seconds - St.T0 > 3) M4_FAIL(TEXT("button 1 did not fire move 1 (no cooldown ring)")); return EGameTest::Running; }
        St.Note += FString::Printf(TEXT("button 1 fired %s (cooldown ring %.2f); "), *P->LastMove.ToString(), Mgr->GetCooldownFraction(0));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (!Shot(TEXT("M4uPhoneBattleHudClear"), St.bShot, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
        // The learn prompt over the fight: Lv 8 and THUNDER WAVE.
        PYRTest::KnockOut(Mgr);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        PYRMoveLearning::FView V;
        if (!PYRMoveLearning::View(W, V) || !V.bTakesInput) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no learn prompt after the knock-out")); PYRTest::KnockOut(Mgr); return EGameTest::Running; }
        if (Test.Seconds - St.T0 < .5) return EGameTest::Running;
        Tap(W, ButtonKey(TEXT("A / Talk")));   // YES: the list of moves
        M4_NEXT();
    }
    if (St.Step == 5)
    {
        M4_WAIT(.5);
        PYRMoveLearning::FView V;
        if (!PYRMoveLearning::View(W, V) || V.Stage != PYRMoveLearning::EStage::List) M4_FAIL(TEXT("A did not open the list of moves"));
        FRects Ctrls, Parts;
        FString Why = Controls(Ctrls);
        TArray<TPair<FString, TSharedPtr<SWidget>>> Buttons;
        for (const TSharedPtr<SWidget>& B : PYRMoveLearning::Buttons(W)) Buttons.Add({FString::Printf(TEXT("learn prompt choice %d"), Buttons.Num() + 1), B});
        if (Why.IsEmpty()) Why = Widgets(Buttons, FString(), Parts);
        if (Why.IsEmpty() && Parts.Num() < 5) Why = FString::Printf(TEXT("the learn list has %d choices"), Parts.Num());
        if (Why.IsEmpty()) Why = Clear(Parts, Ctrls, true);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("learn prompt: %s"), *Why);
        if (!St.bShot2) St.Note += FString::Printf(TEXT("the learn prompt's %d choices clear too; "), Parts.Num());
        if (!Shot(TEXT("M4uPhoneBattleHudClear-learn"), St.bShot2, St.ShotPath)) return EGameTest::Running;
        M4_NEXT();
    }
    if (!Shot(TEXT("M4uPhoneBattleHudClear-learn"), St.bShot2, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot")); return EGameTest::Running; }
    M4_PASS(TEXT("%sscreenshots %s/M4uPhoneBattleHudClear.png and -learn.png"), *St.Note, *LogDir());
}

// ------------------------------------------------------------------------------------------------ the party list
GAME_TEST(M4uPhonePartyListClear, 150)
{
    static FTest St;
    M4_READY();
    M4U_STAGE(EScene::PartyList);
    UPYRMenus* Menus = UPYRMenus::Get(Test.World);
    if (St.Step == 1)
    {
        FRects Ctrls, Parts;
        FString Why = Controls(Ctrls);
        if (Why.IsEmpty()) Why = Widgets(Menus->ProtectedWidgets(), TEXT("party "), Parts);
        if (Why.IsEmpty() && Parts.Num() != 24) Why = FString::Printf(TEXT("%d party parts, not 6 rows of name, level, HP bar and HP"), Parts.Num());
        if (Why.IsEmpty()) Why = Clear(Parts, Ctrls, true);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        int32 Names = 0;
        for (const auto& P : Parts) if (P.Key.Contains(TEXT(" name "))) ++Names;
        St.Note = FString::Printf(TEXT("%s; START then POKéMON by the page's keys; %d names, levels, HP bars and HP numbers (%d rows) on screen and clear of %d page controls; "),
            *ViewNote(St), Parts.Num(), Names, Ctrls.Num());
        M4_NEXT();
    }
    if (!Shot(TEXT("M4uPhonePartyListClear"), St.bShot, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
    M4_PASS(TEXT("%sscreenshot %s"), *St.Note, *St.ShotPath);
}

// ------------------------------------------------------------------------------------------------ the party action menu
GAME_TEST(M4uPhonePartyMenuClear, 180)
{
    static FTest St;
    M4_READY();
    M4U_STAGE(EScene::PartyMenu);
    UWorld* W = Test.World;
    UPYRMenus* Menus = UPYRMenus::Get(W);
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    UPYREncounters* Enc = UPYREncounters::Get(W);
    if (St.Step == 1)
    {
        FRects Ctrls, Parts;
        FString Why = Controls(Ctrls);
        if (Why.IsEmpty()) Why = Widgets(Menus->ProtectedWidgets(), TEXT("party-sub: "), Parts);
        if (Why.IsEmpty() && Parts.Num() != 3) Why = FString::Printf(TEXT("the action menu has %d choices"), Parts.Num());
        if (Why.IsEmpty()) Why = Clear(Parts, Ctrls, true);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        St.Note = FString::Printf(TEXT("%s; STATS, SWITCH and CANCEL on screen and clear of %d page controls; "), *ViewNote(St), Ctrls.Num());
        if (!Shot(TEXT("M4uPhonePartyMenuClear"), St.bShot, St.ShotPath)) return EGameTest::Running;
        // CANCEL: the stick down twice, then A, as the page sends them.
        Tap(W, PYRPhoneLayout::StickKey(1));
        Tap(W, PYRPhoneLayout::StickKey(1));
        Tap(W, ButtonKey(TEXT("A / Talk")));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.3);
        if (Menus->IsOpen(TEXT("party-sub"))) M4_FAIL(TEXT("CANCEL (stick down twice, A) left the action menu open"));
        if (!Menus->IsOpen(TEXT("party"))) M4_FAIL(TEXT("CANCEL closed the party too"));
        St.Note += TEXT("CANCEL by the stick and A closed it; ");
        // Out of the menus (B twice), then the paused switch menu in a wild fight.
        Tap(W, ButtonKey(TEXT("B / Back")));
        Tap(W, ButtonKey(TEXT("B / Back")));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.5);
        if (Menus->AnyMenu()) M4_FAIL(TEXT("B twice left a menu open (%s)"), *Menus->ScreenText());
        if (!Enc->BeginWild(PYRData::Pidgey, 3)) M4_FAIL(TEXT("the wild Pidgey did not start"));
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        if (!(Enc->GetPhase() == EPYREncounterPhase::Fight && Mgr->IsBattleActive())) { if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("the fight never started")); return EGameTest::Running; }
        Mgr->bFoePassive = true;
        M4_WAIT(2.5);
        Tap(W, ButtonKey(TEXT("Party")));
        M4_NEXT();
    }
    if (St.Step == 5)
    {
        M4_WAIT(.6);
        if (!Mgr->IsWheelOpen() || !Mgr->IsBattlePaused()) M4_FAIL(TEXT("Party did not open the paused switch menu"));
        FRects Ctrls, Hud;
        FString Why = Controls(Ctrls);
        if (Why.IsEmpty()) Why = HudRects(Mgr, Hud);
        int32 Slots = 0;
        for (const auto& R : Hud) if (R.Key.StartsWith(TEXT("switch slot"))) ++Slots;
        if (Why.IsEmpty() && Slots != 6) Why = FString::Printf(TEXT("the switch menu has %d slots"), Slots);
        if (Why.IsEmpty()) Why = Clear(Hud, Ctrls, true);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("switch menu: %s"), *Why);
        St.Note += FString::Printf(TEXT("the paused switch menu's %d slots and the HUD on screen and clear; "), Slots);
        M4_NEXT();
    }
    if (!Shot(TEXT("M4uPhonePartyMenuClear-switch"), St.bShot2, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
    M4_PASS(TEXT("%sscreenshots %s/M4uPhonePartyMenuClear.png and -switch.png"), *St.Note, *LogDir());
}

// ------------------------------------------------------------------------------------------------ the overworld
GAME_TEST(M4uPhoneOverworldTouch, 150)
{
    static FTest St;
    M4_READY();
    M4U_STAGE(EScene::Overworld);
    UWorld* W = Test.World;
    UPYRMenus* Menus = UPYRMenus::Get(W);
    UPYRHud* Hud = UPYRHud::Get(W);
    APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(W);
    if (!Red || !Hud) M4_FAIL(TEXT("no Red or no minimap"));
    if (St.Step == 1)
    {
        FRects Ctrls;
        FString Why = Controls(Ctrls);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        const FVector2f View = PYRPhoneLayout::ViewportSize();
        float Smallest = 1e9f;
        for (const auto& C : Ctrls)
        {
            if (!Inside(C.Value, View)) M4_FAIL(TEXT("the page's %s %s is not fully on screen"), *C.Key, *Str(C.Value));
            const bool bTouch = !C.Key.StartsWith(TEXT("Full screen")) && C.Key != TEXT("Stats");
            if (bTouch && (C.Value.GetSize().X < PYRPhoneLayout::MinControl - .5f || C.Value.GetSize().Y < PYRPhoneLayout::MinControl - .5f))
                M4_FAIL(TEXT("the page's %s is %s, under 44 x 44"), *C.Key, *Str(C.Value));
            if (bTouch) Smallest = FMath::Min3(Smallest, C.Value.GetSize().X, C.Value.GetSize().Y);
        }
        St.Note = FString::Printf(TEXT("%s; %d page controls on screen, every touch control at least 44x44 (smallest side %.0f); "), *ViewNote(St), Ctrls.Num(), Smallest);
        // The stick held up for a second.
        St.P0 = Red->GetActorLocation();
        Key(W, PYRPhoneLayout::StickKey(0), IE_Pressed);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(1.0);
        Key(W, PYRPhoneLayout::StickKey(0), IE_Released);
        const float Moved = FVector::Dist2D(Red->GetActorLocation(), St.P0);
        if (Moved < 100.f) M4_FAIL(TEXT("a second on the stick moved Red %.0f cm, under a tile"), Moved);
        St.Note += FString::Printf(TEXT("a second on the stick walked Red %.1f tiles; "), Moved / 100.f);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.6);
        if (!Hud->IsShown()) { if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("the minimap is hidden")); return EGameTest::Running; }
        // A text box (Route 1's sign) over the road, the minimap still up.
        Menus->Say(TEXT("ROUTE 1\nPALLET TOWN -\nVIRIDIAN CITY"));
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(.6);
        FRects Ctrls, Parts;
        FString Why = Controls(Ctrls);
        TArray<TPair<FString, TSharedPtr<SWidget>>> Mine = Menus->ProtectedWidgets();
        Mine.Add({TEXT("minimap"), Hud->GetWidget()});
        if (Why.IsEmpty()) Why = Widgets(Mine, FString(), Parts);
        if (Why.IsEmpty() && !Has(Parts, TEXT("text box"))) Why = TEXT("no text box");
        if (Why.IsEmpty() && !Hud->IsShown()) Why = TEXT("the minimap went away under the text box");
        if (Why.IsEmpty()) Why = Clear(Parts, Ctrls, true);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        for (const auto& P : Parts) St.Note += FString::Printf(TEXT("%s at %s; "), *P.Key, *Str(P.Value));
        St.Note += TEXT("both clear of every page control; ");
        M4_NEXT();
    }
    if (!Shot(TEXT("M4uPhoneOverworldTouch"), St.bShot, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
    M4_PASS(TEXT("%sscreenshot %s"), *St.Note, *St.ShotPath);
}

// ------------------------------------------------------------------------------------------------ the stream on a phone
// What the playtester saw: the stream renders 1280 x 720 and the phone's page (844 x 390) shows it between two bars, its
// controls over the picture. Touch on with the page's size; then the party list, the action menu, the minimap, the
// battle HUD and the switch menu each clear of every control as the page places it on the picture.
GAME_TEST(M4uStreamPhoneClear, 200)
{
    static FTest St;
    St.S.bStream = true;
    M4_READY();
    M4U_STAGE(EScene::PartyMenu);
    UWorld* W = Test.World;
    UPYRMenus* Menus = UPYRMenus::Get(W);
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    UPYREncounters* Enc = UPYREncounters::Get(W);
    UPYRHud* Hud = UPYRHud::Get(W);
    auto Check = [&](const FString& What, FRects Parts, int32 Want) -> FString
    {
        FRects Ctrls;
        FString Why = Controls(Ctrls);
        if (Why.IsEmpty() && Want > 0 && Parts.Num() != Want) Why = FString::Printf(TEXT("%d parts, not %d"), Parts.Num(), Want);
        if (Why.IsEmpty()) Why = Clear(Parts, Ctrls, true);
        if (Why.IsEmpty()) St.Note += FString::Printf(TEXT("%s: %d parts clear of %d page controls; "), *What, Parts.Num(), Ctrls.Num());
        return Why.IsEmpty() ? Why : What + TEXT(": ") + Why;
    };
    if (St.Step == 1)
    {
        if (!St.bShot)
        {
            const FVector2f Page = PYRPhoneLayout::PageSize();
            St.Note = FString::Printf(TEXT("viewport %.0fx%.0f shown on a %.0fx%.0f page; "), St.S.View.X, St.S.View.Y, Page.X, Page.Y);
            FRects Rows, Sub;
            FString Why = Widgets(Menus->ProtectedWidgets(), TEXT("party "), Rows);
            if (Why.IsEmpty()) Why = Check(TEXT("party list"), Rows, 24);
            if (Why.IsEmpty()) Why = Widgets(Menus->ProtectedWidgets(), TEXT("party-sub: "), Sub);
            if (Why.IsEmpty()) Why = Check(TEXT("STATS/SWITCH/CANCEL"), Sub, 3);
            if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        }
        if (!Shot(TEXT("M4uStreamPhoneClear-party"), St.bShot, St.ShotPath)) return EGameTest::Running;
        for (int32 I = 0; I < 3; ++I) Tap(W, ButtonKey(TEXT("B / Back")));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.8);
        if (Menus->AnyOpen()) M4_FAIL(TEXT("B three times left %s"), *Menus->ScreenText());
        if (!Hud || !Hud->IsShown()) { if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("no minimap on the road")); return EGameTest::Running; }
        FRects Map;
        TArray<TPair<FString, TSharedPtr<SWidget>>> MapWidget;
        MapWidget.Add(TPair<FString, TSharedPtr<SWidget>>(TEXT("minimap"), Hud->GetWidget()));
        FString Why = Widgets(MapWidget, FString(), Map);
        if (Why.IsEmpty()) Why = Check(TEXT("minimap"), Map, 1);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        if (!Enc->BeginWild(PYRData::Pidgey, 3)) M4_FAIL(TEXT("the wild Pidgey did not start"));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (!(Enc->GetPhase() == EPYREncounterPhase::Fight && Mgr->IsBattleActive())) { if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("the fight never started")); return EGameTest::Running; }
        Mgr->bFoePassive = true;
        M4_WAIT(2.5);
        if (!St.bShot2)
        {
            FRects Parts;
            FString Why = HudRects(Mgr, Parts);
            if (Why.IsEmpty()) Why = Check(TEXT("battle HUD"), Parts, 0);
            if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        }
        if (!Shot(TEXT("M4uStreamPhoneClear-battle"), St.bShot2, St.ShotPath)) return EGameTest::Running;
        Tap(W, ButtonKey(TEXT("Party")));
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(.6);
        if (!Mgr->IsWheelOpen()) M4_FAIL(TEXT("Party did not open the switch menu"));
        FRects Parts;
        FString Why = HudRects(Mgr, Parts);
        if (Why.IsEmpty()) Why = Check(TEXT("switch menu and HUD"), Parts, 0);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s"), *Why);
        St.bShot = false;
        M4_NEXT();
    }
    if (!Shot(TEXT("M4uStreamPhoneClear-switch"), St.bShot, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
    M4_PASS(TEXT("%sscreenshots %s/M4uStreamPhoneClear-party.png, -battle.png, -switch.png"), *St.Note, *LogDir());
}

// ------------------------------------------------------------------------------------------------ Desktop
// At the default test size (1280 x 720, DPI scale 0.666, so the HUD's local space is 1921.92 x 1081.08) the layout is
// Desktop and the HUD sits exactly where m4t drew it: these viewport px are read off m4t's code (28/24 card insets, the
// moves 28 from the bottom-right corner, RUN at x 604...), to 3 px.
GAME_TEST(M4uDesktopLayoutUnchanged, 120)
{
    static FTest St;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    if (St.Step == 0)
    {
        const FVector2f View = PYRPhoneLayout::ViewportSize();
        if (View != FVector2f(1280, 720)) M4_FAIL(TEXT("the test viewport is %.0fx%.0f, not 1280x720"), View.X, View.Y);
        if (PYRPhoneLayout::Current() != EPYRLayout::Desktop) M4_FAIL(TEXT("the layout is %s at 1280x720 without touch"), PYRPhoneLayout::Name(PYRPhoneLayout::Current()));
        if (PYRPhoneLayout::Controls().Num()) M4_FAIL(TEXT("page controls modelled without a page"));
        PYRTest::Fight(Test, {PYRTest::Mon(PYRData::Pikachu, 10)}, {PYRTest::Mon(PYRData::Pidgey, 5)}, EPYRBattleKind::Wild);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(1.5);
        TSharedPtr<SPYRBattleHUD> Hud = Mgr->GetHud();
        if (!Hud.IsValid()) M4_FAIL(TEXT("no battle HUD"));
        const FGeometry& G = Hud->GetCachedGeometry();
        const FVector2f Size = FVector2f(G.GetLocalSize());
        const FPYRHudLayout L = FPYRHudLayout::Make(Size);
        if (L.bPhone) M4_FAIL(TEXT("the HUD laid out for a phone"));
        auto Px = [&G](FVector2f Local) { FBox2f V; ToView(FBox2f(FVector2f(G.LocalToAbsolute(FVector2D(Local))), FVector2f(G.LocalToAbsolute(FVector2D(Local)))), V); return V.Min; };
        struct FWant { const TCHAR* Name; FVector2f Got, Want; };
        const FWant Checks[] = {
            {TEXT("foe card"), Px(L.FoeCard), FVector2f(18.65f, 15.98f)},
            {TEXT("foe card end"), Px(L.FoeCard + FPYRHudLayout::CardSize()), FVector2f(238.43f, 66.60f)},
            {TEXT("player card"), Px(L.PlayerCard), FVector2f(18.65f, 646.74f)},
            {TEXT("items"), Px(L.Items), FVector2f(250.42f, 658.73f)},
            {TEXT("RUN"), Px(L.Run.Min), FVector2f(402.26f, 658.73f)},
            {TEXT("move 1"), Px(L.MovesStart), FVector2f(1029.58f, 649.40f)},
            {TEXT("move 4 end"), Px(L.MoveBox(3).Max), FVector2f(1261.35f, 701.35f)},
            {TEXT("dodge"), Px(L.Dodge), FVector2f(1029.58f, 618.77f)},
            {TEXT("swap centre"), Px(L.SwapCentre), FVector2f(1237.37f, 602.78f)},
            {TEXT("caption top"), Px(FVector2f(L.CaptionCentreX, L.CaptionTop)), FVector2f(640.f, 446.40f)},
            {TEXT("switch menu centre"), Px(L.WheelCentre), FVector2f(640.f, 360.f)},
        };
        for (const FWant& C : Checks)
            if (!C.Got.Equals(C.Want, 3.f)) M4_FAIL(TEXT("%s at (%.1f, %.1f) px, m4t had it at (%.1f, %.1f)"), C.Name, C.Got.X, C.Got.Y, C.Want.X, C.Want.Y);
        if (L.WheelScale != 1.f) M4_FAIL(TEXT("the switch menu is scaled %.2f"), L.WheelScale);
        St.Note = FString::Printf(TEXT("Desktop at 1280x720 (HUD local %.1fx%.1f); %d HUD positions as m4t to 3 px (foe card at (%.1f,%.1f), move 1 at (%.1f,%.1f)); "),
            Size.X, Size.Y, int32(UE_ARRAY_COUNT(Checks)), Checks[0].Got.X, Checks[0].Got.Y, Checks[5].Got.X, Checks[5].Got.Y);
        M4_NEXT();
    }
    if (!Shot(TEXT("M4uDesktopLayoutUnchanged"), St.bShot, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
    M4_PASS(TEXT("%sscreenshot %s"), *St.Note, *St.ShotPath);
}

// ------------------------------------------------------------------------------------------------ m4v: the HUD unchanged
// M4vHudReadable (rendered, 844 x 390 with touch on, as the stream on a phone): a Route 1 wild battle's HUD, then (Run,
// back on the road) the START menu and POKéMON's party list. Every part's name, place and size must be m4u's: the
// layout recorded from the untouched m4u game (Content/Look/Kit/hud-m4u.json), to 1.5 px. The anime look changes the
// world's materials and grade only, never a widget. Screenshots M4vHudReadable-battle, -start and -party beside the log.
// -M4vRecordHud=<json> records the layout instead (run once on m4u).
namespace M4V
{
using FLayout = TArray<TPair<FString, FBox2f>>;

FString Compare(const FString& Scene, const FLayout& Got, const TSharedPtr<FJsonObject>& Ref, int32& Parts)
{
    const TArray<TSharedPtr<FJsonValue>>* Want = nullptr;
    if (!Ref.IsValid() || !Ref->TryGetArrayField(Scene, Want)) return FString::Printf(TEXT("the m4u layout has no %s"), *Scene);
    if (Want->Num() != Got.Num()) return FString::Printf(TEXT("%s has %d parts, m4u had %d"), *Scene, Got.Num(), Want->Num());
    for (int32 I = 0; I < Got.Num(); ++I)
    {
        const TSharedPtr<FJsonObject> W = (*Want)[I]->AsObject();
        const TArray<TSharedPtr<FJsonValue>> R = W->GetArrayField(TEXT("rect"));
        const FBox2f B(FVector2f(R[0]->AsNumber(), R[1]->AsNumber()), FVector2f(R[0]->AsNumber() + R[2]->AsNumber(), R[1]->AsNumber() + R[3]->AsNumber()));
        if (W->GetStringField(TEXT("name")) != Got[I].Key) return FString::Printf(TEXT("%s part %d is %s, m4u had %s"), *Scene, I, *Got[I].Key, *W->GetStringField(TEXT("name")));
        if (!B.Min.Equals(Got[I].Value.Min, 1.5f) || !B.Max.Equals(Got[I].Value.Max, 1.5f))
            return FString::Printf(TEXT("%s %s at %s, m4u had it at %s"), *Scene, *Got[I].Key, *Str(Got[I].Value), *Str(B));
        ++Parts;
    }
    return FString();
}

TArray<TSharedPtr<FJsonValue>> ToJson(const FLayout& L)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const auto& P : L)
    {
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("name"), P.Key);
        O->SetArrayField(TEXT("rect"), {MakeShared<FJsonValueNumber>(P.Value.Min.X), MakeShared<FJsonValueNumber>(P.Value.Min.Y),
            MakeShared<FJsonValueNumber>(P.Value.GetSize().X), MakeShared<FJsonValueNumber>(P.Value.GetSize().Y)});
        Out.Add(MakeShared<FJsonValueObject>(O));
    }
    return Out;
}
}

GAME_TEST(M4vHudReadable, 200)
{
    static FTest St;
    static M4V::FLayout Battle, Start, Party;
    static bool bShotStart = false, bShotParty = false;
    M4_READY();
    M4U_STAGE(EScene::Battle);
    UWorld* W = Test.World;
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    UPYRMenus* Menus = UPYRMenus::Get(W);
    UPYREncounters* Enc = UPYREncounters::Get(W);
    if (St.Step == 1)
    {
        FRects Hud;
        const FString Why = HudRects(Mgr, Hud);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("battle HUD: %s"), *Why);
        for (const auto& R : Hud) if (!Inside(R.Value, PYRPhoneLayout::ViewportSize())) M4_FAIL(TEXT("battle HUD %s %s is off the 844x390 screen"), *R.Key, *Str(R.Value));
        Battle = Hud;
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        if (!Shot(TEXT("M4vHudReadable-battle"), St.bShot, St.ShotPath)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *St.ShotPath); return EGameTest::Running; }
        Mgr->PressRun();   // a wild Pidgey: "Got away safely!", then back on the road
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (Enc->IsBusy()) { if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("Run did not end the wild battle")); return EGameTest::Running; }
        M4_WAIT(1.0);
        Tap(W, ButtonKey(TEXT("START")));
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        if (!Menus->IsOpen(TEXT("start"))) { if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("START did not open the menu")); return EGameTest::Running; }
        M4_WAIT(.8);
        FRects Rows;
        const FString Why = Widgets(Menus->ProtectedWidgets(), TEXT("start"), Rows);
        if (!Why.IsEmpty() || Rows.Num() == 0) M4_FAIL(TEXT("START menu: %s"), Why.IsEmpty() ? TEXT("no entries laid out") : *Why);
        Start = Rows;
        M4_NEXT();
    }
    if (St.Step == 5)
    {
        FString Path;
        if (!Shot(TEXT("M4vHudReadable-start"), bShotStart, Path)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *Path); return EGameTest::Running; }
        const TArray<FString> Entries = PYRScreens::StartEntries(W);
        const int32 At = Entries.IndexOfByPredicate([](const FString& E) { return E.StartsWith(TEXT("POKéMON")); });
        if (At == INDEX_NONE) M4_FAIL(TEXT("no POKéMON on the START menu"));
        for (int32 I = 0; I < At; ++I) Tap(W, PYRPhoneLayout::StickKey(1));
        Tap(W, ButtonKey(TEXT("A / Talk")));
        M4_NEXT();
    }
    if (St.Step == 6)
    {
        if (!Menus->IsOpen(TEXT("party"))) { if (Test.Seconds - St.T0 > 5) M4_FAIL(TEXT("POKéMON did not open the party")); return EGameTest::Running; }
        M4_WAIT(.8);
        FRects Rows;
        const FString Why = Widgets(Menus->ProtectedWidgets(), TEXT("party "), Rows);
        if (!Why.IsEmpty() || Rows.Num() == 0) M4_FAIL(TEXT("party list: %s"), Why.IsEmpty() ? TEXT("no rows laid out") : *Why);
        Party = Rows;
        M4_NEXT();
    }
    FString Path;
    if (!Shot(TEXT("M4vHudReadable-party"), bShotParty, Path)) { if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("no screenshot at %s"), *Path); return EGameTest::Running; }
    FString Record;
    if (FParse::Value(FCommandLine::Get(), TEXT("M4vRecordHud="), Record) && !Record.IsEmpty())
    {
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("viewport"), FString::Printf(TEXT("%.0fx%.0f"), St.S.View.X, St.S.View.Y));
        O->SetArrayField(TEXT("battle"), M4V::ToJson(Battle));
        O->SetArrayField(TEXT("start"), M4V::ToJson(Start));
        O->SetArrayField(TEXT("party"), M4V::ToJson(Party));
        FString Text;
        FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
        if (!FFileHelper::SaveStringToFile(Text, *Record, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) M4_FAIL(TEXT("could not write %s"), *Record);
        M4_PASS(TEXT("recorded %d battle HUD parts, %d START entries, %d party rows at %s to %s"), Battle.Num(), Start.Num(), Party.Num(), *ViewNote(St), *Record);
    }
    const FString RefPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Look/Kit/hud-m4u.json")));
    FString Text;
    TSharedPtr<FJsonObject> Ref;
    if (!FFileHelper::LoadFileToString(Text, *RefPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref)) M4_FAIL(TEXT("no m4u layout at %s"), *RefPath);
    int32 Parts = 0;
    for (const TPair<const TCHAR*, const M4V::FLayout*>& Scene : {TPair<const TCHAR*, const M4V::FLayout*>(TEXT("battle"), &Battle),
             TPair<const TCHAR*, const M4V::FLayout*>(TEXT("start"), &Start), TPair<const TCHAR*, const M4V::FLayout*>(TEXT("party"), &Party)})
    {
        const FString Why = M4V::Compare(Scene.Key, *Scene.Value, Ref, Parts);
        if (!Why.IsEmpty()) M4_FAIL(TEXT("%s (%s)"), *Why, *ViewNote(St));
    }
    M4_PASS(TEXT("%s: %d parts (battle HUD %d, START %d, party %d) named, placed and sized as m4u to 1.5 px, all on screen; screenshots %s/M4vHudReadable-battle, -start, -party.png"),
        *ViewNote(St), Parts, Battle.Num(), Start.Num(), Party.Num(), *LogDir());
}

// ------------------------------------------------------------------------------------------------ the shots
namespace PYRM4UShots
{
bool Tick(UObject* Ctx, const FString& Name, int32& Step, double& Clock)
{
    EScene Scene;
    if (Name == TEXT("m4u-phone-battle")) Scene = EScene::Battle;
    else if (Name == TEXT("m4u-phone-party")) Scene = EScene::PartyList;
    else if (Name == TEXT("m4u-phone-party-menu")) Scene = EScene::PartyMenu;
    else if (Name == TEXT("m4u-phone-overworld")) Scene = EScene::Overworld;
    else return false;
    static FStage S;
    static double Last = 0;
    static bool bReady = false, bSaid = false;
    const float D = float(FMath::Max(0.0, Clock - Last));
    Last = Clock;
    if (bReady) return true;
    GameTest::HoldShot(2.0);
    if (!S.Error.IsEmpty()) return true;
    if (!Stage(Ctx, Scene, S, D))
    {
        if (!S.Error.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("PYR_SHOT_FAILED:%s %s"), *Name, *S.Error);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }
        return true;
    }
    if (Scene == EScene::Overworld && !bSaid)
    {
        // The sign's text box over the road, with the minimap.
        bSaid = true;
        if (UPYRMenus* Menus = UPYRMenus::Get(Ctx)) Menus->Say(TEXT("ROUTE 1\nPALLET TOWN -\nVIRIDIAN CITY"));
        S.Next(9);
        return true;
    }
    bReady = true;
    UE_LOG(LogTemp, Display, TEXT("M4U_SHOT %s staged at %.0fx%.0f, layout %s"), *Name, S.View.X, S.View.Y, PYRPhoneLayout::Name(PYRPhoneLayout::Current()));
    GameTest::HoldShot(1.0);
    return true;
}
}
