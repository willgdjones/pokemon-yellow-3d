// m4t: the three battle controls the designer hit on the stream.
// - The learn-move prompt (Menus/PYRMoveLearning): a fifth move is forgotten only after the player picks it and confirms;
//   battle keys, held keys and the stream's battle buttons never answer it; NO keeps the four; TMs and evolution use it.
// - The battle pause (APYRBattleManager::SetBattlePaused): the switch menu, voluntary or forced by a faint, holds the
//   fight: HP, cooldowns, the swap and Potion timers, the heal, poison, the foe brain and a projectile in flight.
// - Run (X, the pad's Y, the HUD's RUN, the stream's RUN): any wild battle ends with "Got away safely!" and Red back on
//   the tile where it started; trainers refuse with Yellow's line and the fight goes on.
// Every test drives the game the way a player does: keys through the player controller, the HUD's own click and touch
// handlers, the stream layout's key codes. The learn and switch tests fight on the m2 test pad (no YellowData needed);
// the Run and TM tests use Kanto (YellowData).
#include "Tests/M4TestKit.h"

#include "Battle/BattleTestKit.h"
#include "Battle/PYRAttack.h"
#include "Battle/PYRBattleHUD.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRMoveLearning.h"
#include "Menus/PYRScreens.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Music/GbMusicPlayer.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stage/PYREncounters.h"
#include "World/CoreSeams.h"

using namespace M4;
using PYRMoveLearning::EStage;

namespace M4T
{
// ------------------------------------------------------------------------------------------------ input like a player
void Key(UWorld* World, const FKey& K, EInputEvent Event)
{
    if (APlayerController* PC = World->GetFirstPlayerController())
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Event, Event == IE_Released ? 0.f : 1.f));
}

void Tap(UWorld* World, const FKey& K)
{
    Key(World, K, IE_Pressed);
    Key(World, K, IE_Released);
}

// A browser keyCode (what the stream page sends) as the engine's key.
FKey BrowserKey(int32 Code)
{
    if (Code >= 65 && Code <= 90) return FKey(*FString::Chr(TCHAR(Code)));
    static const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
    if (Code >= 48 && Code <= 57) return Digits[Code - 48];
    switch (Code)
    {
    case 13: return EKeys::Enter;
    case 9: return EKeys::Tab;
    case 16: return EKeys::LeftShift;
    case 27: return EKeys::Escape;
    case 32: return EKeys::SpaceBar;
    default: return EKeys::Invalid;
    }
}

// The stream's phone layout (touch-layout.json, staged with the package): every button as label -> key.
TArray<TPair<FString, FKey>> StreamButtons()
{
    TArray<TPair<FString, FKey>> Out;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("touch-layout.json")))) return Out;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return Out;
    for (const TCHAR* Group : {TEXT("buttons"), TEXT("extra")})
    {
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (!Root->TryGetArrayField(Group, List)) continue;
        for (const TSharedPtr<FJsonValue>& V : *List)
        {
            const TSharedPtr<FJsonObject>* B = nullptr;
            if (V->TryGetObject(B)) Out.Add({(*B)->GetStringField(TEXT("label")), BrowserKey(int32((*B)->GetNumberField(TEXT("key"))))});
        }
    }
    return Out;
}

FKey StreamKey(const FString& Label)
{
    for (const TPair<FString, FKey>& B : StreamButtons()) if (B.Key == Label) return B.Value;
    return EKeys::Invalid;
}

// A click (or a tap) on the battle HUD at a point of its own layout, through its own handlers.
bool HudPress(APYRBattleManager* Mgr, bool bTouch, TFunctionRef<FVector2f(FVector2f)> Where)
{
    TSharedPtr<SPYRBattleHUD> Hud = Mgr->GetHud();
    if (!Hud.IsValid()) Hud = SNew(SPYRBattleHUD).Manager(Mgr);   // No game viewport: the same widget, unparented.
    FGeometry Geo = Hud->GetCachedGeometry();
    if (Geo.GetLocalSize().X < 10.f) Geo = FGeometry::MakeRoot(FVector2f(1920, 1080), FSlateLayoutTransform());
    const FVector2f Local = Where(FVector2f(Geo.GetLocalSize()));
    const FVector2f Screen = FVector2f(Geo.LocalToAbsolute(FVector2D(Local)));
    const int32 Before = Hud->Clicks;
    if (bTouch)
    {
        FPointerEvent Touch(0, 1, Screen, Screen, 1.f, true);
        Hud->OnTouchStarted(Geo, Touch);
    }
    else
    {
        FPointerEvent Click(0, FVector2D(Screen), FVector2D(Screen), TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
        Hud->OnMouseButtonDown(Geo, Click);
    }
    return Hud->Clicks > Before;
}

bool PressRunButton(APYRBattleManager* Mgr, bool bTouch)
{
    return HudPress(Mgr, bTouch, [](FVector2f Size) { return SPYRBattleHUD::RunRect(Size).GetCenter(); });
}

// ------------------------------------------------------------------------------------------------ the learn prompt
// Pikachu Lv 7 with THUNDERSHOCK, GROWL, TAIL WHIP, QUICK ATTACK, one point short of Lv 8 (THUNDER WAVE), on the pad.
void StartLevel8(FGameTestContext& Test)
{
    PYRTest::Fight(Test, {PYRTest::Mon(PYRData::Pikachu, 7, {PYRData::ThunderShock, PYRData::Growl, PYRData::TailWhip, PYRData::QuickAttack})},
        {PYRTest::Mon(PYRData::Pidgey, 3)});
    UPYRTrainerState* S = PYRTest::S(Test);
    S->Party[0].Xp = PYRData::XpForLevel(PYRData::Pikachu, 8) - 1;
    if (UPYRMenus* Me = UPYRMenus::Get(Test.World)) Me->TextLog.Reset();
}

TArray<FName> Four() { return {PYRData::ThunderShock, PYRData::Growl, PYRData::TailWhip, PYRData::QuickAttack}; }

FName Level8Move() { const TArray<FName> M = PYRData::MovesLearned(PYRData::Pikachu, 7, 8); return M.Num() ? M[0] : NAME_None; }

FString Name(FName Move) { return PYRScreens::MoveName(Move); }

FString Said(UWorld* World)
{
    const UPYRMenus* Me = UPYRMenus::Get(World);
    return Me ? FString::Join(Me->TextLog, TEXT(" | ")).Replace(TEXT("\n"), TEXT(" ")) : FString();
}

bool Stage(UWorld* World, EStage Want, PYRMoveLearning::FView& V) { return PYRMoveLearning::View(World, V) && V.Stage == Want; }

// Confirm through the text boxes after an answer (Enter, as a player would) until nothing is open.
bool CloseTexts(UWorld* World)
{
    UPYRMenus* Me = UPYRMenus::Get(World);
    if (!Me || !Me->AnyOpen()) return true;
    if (Me->Top()->Id == TEXT("text")) Tap(World, EKeys::Enter);
    return false;
}

// Every battle input a mashing player could send: the move keys, dodge, hop, Potion, aim, swap, Run, the pad's Run and
// switch buttons, and the stream's battle buttons (not START or A/Talk: E and Enter are the prompt's own confirm).
TArray<FKey> BattleKeys(bool bWithStream)
{
    TArray<FKey> Keys = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::SpaceBar, EKeys::R, EKeys::F, EKeys::Q,
        EKeys::Tab, EKeys::X, EKeys::LeftShift, EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_LeftShoulder};
    if (bWithStream)
        for (const TPair<FString, FKey>& B : StreamButtons())
            if (B.Key != TEXT("START") && B.Key != TEXT("A / Talk") && B.Value.IsValid()) Keys.AddUnique(B.Value);
    return Keys;
}
}

using namespace M4T;

// ------------------------------------------------------------------------------------------------ 1. the learn prompt
// Pikachu with four moves reaches Lv 8: Yellow's three lines and YES/NO; YES opens the list; picking GROWL asks
// "Forget GROWL?" and the four are unchanged until its YES; then GROWL's slot holds THUNDER WAVE, with Yellow's lines.
GAME_TEST(M4t_LearnMove_ForgetsOnlyAfterConfirm, 120)
{
    static FState St;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UPYRTrainerState* S = PYRTest::S(Test);
    UWorld* W = Test.World;
    PYRMoveLearning::FView V;
    const FName New = Level8Move();
    if (St.Step == 0)
    {
        if (New != PYRData::ThunderWave) M4_FAIL(TEXT("Pikachu's Lv 8 move is '%s', not THUNDER WAVE"), *New.ToString());
        StartLevel8(Test);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(1.2);
        PYRTest::KnockOut(Mgr);
        if (S->Party[0].Level != 8) M4_FAIL(TEXT("Pikachu is Lv %d"), S->Party[0].Level);
        if (!Stage(W, EStage::Ask, V)) M4_FAIL(TEXT("no learn prompt at Lv 8 (%s)"), *UPYRMenus::Get(W)->ScreenText());
        const FString Mon = UPYRTrainerState::DisplayName(S->Party[0]);
        const FString Want = FString::Printf(TEXT("%s is trying to learn %s!\nBut, %s can't learn more than 4 moves!\nDelete an older move to make room for %s?"),
            *Mon, *Name(New), *Mon, *Name(New));
        if (V.Text != Want) M4_FAIL(TEXT("the prompt says '%s', Yellow says '%s'"), *V.Text.Replace(TEXT("\n"), TEXT(" / ")), *Want.Replace(TEXT("\n"), TEXT(" / ")));
        if (V.Cursor != 0) M4_FAIL(TEXT("YES/NO does not start on YES"));
        if (!Mgr->IsBattleActive() || !Mgr->IsBattlePaused()) M4_FAIL(TEXT("the fight is not paused under the prompt"));
        St.Note = FString::Printf(TEXT("'%s' YES/NO; "), *V.Text.Replace(TEXT("\n"), TEXT(" / ")));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(PYRMoveLearning::InputDelay + .1);
        Tap(W, EKeys::Enter);   // YES
        if (!Stage(W, EStage::List, V)) M4_FAIL(TEXT("YES did not open the list"));
        if (V.Text != TEXT("Which move should be forgotten?")) M4_FAIL(TEXT("the list asks '%s'"), *V.Text);
        if (V.Rows.Num() != 4) M4_FAIL(TEXT("the list has %d moves"), V.Rows.Num());
        const FString Shown = UPYRMenus::Get(W)->ScreenText();
        for (const FName& M : Four()) if (!Shown.Contains(Name(M))) M4_FAIL(TEXT("%s missing from the list"), *Name(M));
        Tap(W, EKeys::S);       // GROWL
        Tap(W, EKeys::Enter);
        if (!Stage(W, EStage::Confirm, V)) M4_FAIL(TEXT("picking GROWL did not ask for a confirm"));
        if (V.Text != FString::Printf(TEXT("Forget %s?"), *Name(PYRData::Growl))) M4_FAIL(TEXT("the confirm says '%s'"), *V.Text);
        if (V.Cursor != 1) M4_FAIL(TEXT("'Forget GROWL?' does not start on NO"));
        // The pick alone changes nothing.
        if (S->Party[0].Carried != Four() || Mgr->GetPlayerMon()->Carried != Four()) M4_FAIL(TEXT("a move changed before the confirm"));
        St.Note += TEXT("list of four + NEW, GROWL picked: 'Forget GROWL?' (cursor on NO), the four unchanged; ");
        Tap(W, EKeys::W);       // YES
        Tap(W, EKeys::Enter);
        const TArray<FName> Want = {PYRData::ThunderShock, New, PYRData::TailWhip, PYRData::QuickAttack};
        if (S->Party[0].Carried != Want) M4_FAIL(TEXT("after YES the moves are %s"), *FString::JoinBy(S->Party[0].Carried, TEXT(","), [](FName N) { return N.ToString(); }));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (!CloseTexts(W)) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the texts never closed")); return EGameTest::Running; }
        const FString All = Said(W);
        const FString Mon = UPYRTrainerState::DisplayName(S->Party[0]);
        for (const FString& Part : {FString(TEXT("1, 2 and... Poof!")), FString::Printf(TEXT("%s forgot %s!"), *Mon, *Name(PYRData::Growl)), FString(TEXT("And...")),
                 FString::Printf(TEXT("%s learned %s!"), *Mon, *Name(New))})
            if (!All.Contains(Part)) M4_FAIL(TEXT("never said '%s' (%s)"), *Part, *All);
        if (Mgr->IsBattlePaused()) M4_FAIL(TEXT("the fight is still paused after the prompt"));
        if (Mgr->GetPlayerMon() && Mgr->GetPlayerMon()->Carried != S->Party[0].Carried) M4_FAIL(TEXT("the fighter did not get its new moves"));
        M4_NEXT();
    }
    if (Mgr->IsBattleActive()) { if (Test.Seconds - St.T0 > 6) M4_FAIL(TEXT("the fight never ended")); return EGameTest::Running; }
    M4_PASS(TEXT("%sYES: GROWL's slot holds %s; '1, 2 and... Poof! ... forgot GROWL! And... learned %s!'; the fight resumed and ended"), *St.Note, *Name(New), *Name(New));
}

// Every battle input, the same keys held from before the prompt opened and the stream's battle buttons, sent while the
// prompt is up (inside its 0.3 s guard, after it, and again later): it stays on its question and the four stay.
GAME_TEST(M4t_LearnMove_AttackKeysIgnored, 120)
{
    static FState St;
    static TArray<FKey> HeldKeys;
    static int32 PotionsBefore = 0, RunsBefore = 0;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UPYRTrainerState* S = PYRTest::S(Test);
    UWorld* W = Test.World;
    PYRMoveLearning::FView V;
    auto Mash = [&](bool bGuard) -> FString
    {
        for (const FKey& K : BattleKeys(true)) { Key(W, K, IE_Pressed); Key(W, K, IE_Repeat); Key(W, K, IE_Released); }
        // The keys held since before the prompt: pressed again (a bouncing key) and repeated, never let go.
        for (const FKey& K : HeldKeys) { Key(W, K, IE_Repeat); Key(W, K, IE_Pressed); }
        // Inside the guard even the prompt's own keys do nothing.
        if (bGuard) for (const FKey& K : {EKeys::Enter, EKeys::Escape, EKeys::S}) Tap(W, K);
        // The battle's own handlers and the HUD's RUN button, as a stray click or the bot would reach them.
        for (int32 I = 0; I < 4; ++I) Mgr->PressMove(I);
        Mgr->PressDodge(); Mgr->PressPotion(); Mgr->PressHop(); Mgr->BeginAim(); Mgr->PressRun(); Mgr->OpenWheel(true);
        PressRunButton(Mgr, false);
        PressRunButton(Mgr, true);
        if (!Stage(W, EStage::Ask, V)) return FString::Printf(TEXT("the prompt moved on (%s)"), *UPYRMenus::Get(W)->ScreenText().Replace(TEXT("\n"), TEXT(" | ")));
        if (V.Cursor != 0) return TEXT("the YES/NO cursor moved");
        if (S->Party[0].Carried != Four()) return TEXT("the moves changed");
        // (The win is decided at the knock-out; it waits under the prompt.)
        if (!Mgr->IsBattleActive() || !Mgr->IsBattlePaused() || Mgr->IsWheelOpen()) return TEXT("the fight moved under the prompt (ended, unpaused or the switch menu opened)");
        if (S->ItemCount(UPYRTrainerState::Potion) != PotionsBefore) return TEXT("a Potion was used");
        if (Mgr->RunPresses != RunsBefore) return TEXT("Run went through");
        return FString();
    };
    if (St.Step == 0)
    {
        StartLevel8(Test);
        HeldKeys = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::SpaceBar, EKeys::E, EKeys::R, EKeys::F};
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(1.2);
        // The foe can't fall to the moves the held keys fire: only the knock-out below levels Pikachu.
        if (APYRMonPawn* F = Mgr->GetFoeMon()) F->Hp = F->MaxHp = 5000.f;
        for (const FKey& K : HeldKeys) Key(W, K, IE_Pressed);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.25);   // The keys are down (the moves they fired are on their way).
        APlayerController* PC = W->GetFirstPlayerController();
        for (const FKey& K : HeldKeys) if (!PC->IsInputKeyDown(K)) M4_FAIL(TEXT("%s is not held"), *K.ToString());
        PotionsBefore = S->ItemCount(UPYRTrainerState::Potion);
        PYRTest::KnockOut(Mgr);
        if (!Stage(W, EStage::Ask, V)) M4_FAIL(TEXT("no learn prompt at Lv 8"));
        RunsBefore = Mgr->RunPresses;
        const FString Bad = Mash(true);
        if (!Bad.IsEmpty()) M4_FAIL(TEXT("inside the 0.3 s guard: %s"), *Bad);
        St.Note = FString::Printf(TEXT("%d battle keys and stream buttons (the layout read: %s; START and A/Talk left out, E and Enter are the prompt's), %d held keys, the handlers and RUN clicks/taps: inside the guard nothing; "),
            BattleKeys(true).Num(), *FString::JoinBy(StreamButtons(), TEXT(","), [](const TPair<FString, FKey>& B) { return B.Key; }), HeldKeys.Num());
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(PYRMoveLearning::InputDelay + .2);
        const FString Bad = Mash(false);
        if (!Bad.IsEmpty()) M4_FAIL(TEXT("after the guard: %s"), *Bad);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        M4_WAIT(1.5);
        const FString Bad = Mash(false);
        if (!Bad.IsEmpty()) M4_FAIL(TEXT("1.5 s later: %s"), *Bad);
        // Let go of the held keys: the prompt is still on its question.
        for (const FKey& K : HeldKeys) Key(W, K, IE_Released);
        M4_NEXT();
    }
    if (St.Step == 5)
    {
        M4_WAIT(.3);
        if (!Stage(W, EStage::Ask, V) || S->Party[0].Carried != Four()) M4_FAIL(TEXT("letting go answered the prompt"));
        St.Note += TEXT("after the guard and 1.5 s later the same; let go, still asking with the four; ");
        M4_NEXT();
    }
    // Keep the four (NO, then YES to abandon), as a player would.
    if (PYRMoveLearning::StepKeep(W) || UPYRMenus::Get(W)->AnyOpen()) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the prompt never closed")); return EGameTest::Running; }
    if (S->Party[0].Carried != Four()) M4_FAIL(TEXT("the four changed"));
    M4_PASS(TEXT("%sNO kept them"), *St.Note);
}

// NO asks "Abandon learning THUNDER WAVE?"; its NO (or Backspace) goes back to the question; Esc from the list asks
// again; YES: "PIKACHU did not learn THUNDER WAVE!" and the four are untouched.
GAME_TEST(M4t_LearnMove_Abandon, 120)
{
    static FState St;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UPYRTrainerState* S = PYRTest::S(Test);
    UWorld* W = Test.World;
    PYRMoveLearning::FView V;
    const FName New = Level8Move();
    if (St.Step == 0) { StartLevel8(Test); M4_NEXT(); }
    if (St.Step == 1)
    {
        M4_WAIT(1.2);
        PYRTest::KnockOut(Mgr);
        if (!Stage(W, EStage::Ask, V)) M4_FAIL(TEXT("no learn prompt at Lv 8"));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(PYRMoveLearning::InputDelay + .1);
        const FString Abandon = FString::Printf(TEXT("Abandon learning %s?"), *Name(New));
        Tap(W, EKeys::S);       // NO
        Tap(W, EKeys::Enter);
        if (!Stage(W, EStage::Abandon, V) || V.Text != Abandon) M4_FAIL(TEXT("NO did not ask '%s'"), *Abandon);
        Tap(W, EKeys::S);       // NO: back to the question
        Tap(W, EKeys::Enter);
        if (!Stage(W, EStage::Ask, V)) M4_FAIL(TEXT("NO to abandoning did not go back to the question"));
        Tap(W, EKeys::BackSpace);
        if (!Stage(W, EStage::Abandon, V)) M4_FAIL(TEXT("Backspace on the question did not ask to abandon"));
        Tap(W, EKeys::Escape);  // Back: the question again
        if (!Stage(W, EStage::Ask, V)) M4_FAIL(TEXT("Esc on the abandon question did not go back"));
        Tap(W, EKeys::Enter);   // YES: the list
        if (!Stage(W, EStage::List, V)) M4_FAIL(TEXT("no list"));
        Tap(W, EKeys::Escape);  // Back from the list
        if (!Stage(W, EStage::Abandon, V)) M4_FAIL(TEXT("Esc from the list did not ask '%s'"), *Abandon);
        if (S->Party[0].Carried != Four()) M4_FAIL(TEXT("a move changed while going back and forth"));
        Tap(W, EKeys::Enter);   // YES, abandon
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (!CloseTexts(W)) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the texts never closed")); return EGameTest::Running; }
        const FString Line = FString::Printf(TEXT("%s did not learn %s!"), *UPYRTrainerState::DisplayName(S->Party[0]), *Name(New));
        if (!Said(W).Contains(Line)) M4_FAIL(TEXT("never said '%s' (%s)"), *Line, *Said(W));
        if (S->Party[0].Carried != Four() || S->Party[0].Pending.Num() != 0) M4_FAIL(TEXT("the four changed or the move still waits"));
        if (Mgr->IsBattlePaused()) M4_FAIL(TEXT("still paused"));
        M4_PASS(TEXT("NO -> 'Abandon learning %s?', NO and Backspace/Esc back to the question, Esc from the list asks again, YES -> '%s'; the four untouched"),
            *Name(New), *Line);
    }
    return EGameTest::Running;
}

// The bag's TM34 (BIDE) on Pikachu with four moves, and a move learned on evolving, open the same prompt. The TM's
// prompt is answered with clicks on its own buttons (NO, then YES to abandon): the TM stays, the four stay.
GAME_TEST(M4t_LearnMove_TMAndEvolutionUseSamePrompt, 180)
{
    static FState St;
    static FName From, Into, EvoMove;
    static int32 EvoLevel = 0;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    UWorld* W = Test.World;
    UPYRMenus* Me = Menus(Test);
    PYRMoveLearning::FView V;
    Quiet(true, true);
    const FName Tm = YKey::Item(TEXT("TM_BIDE"));
    const FYItem* TmInfo = Data(Test)->Item(Tm);
    if (!TmInfo || TmInfo->Move.IsNone()) M4_FAIL(TEXT("items.json has no TM_BIDE"));
    auto Click = [&](int32 Index) -> bool
    {
        TArray<TSharedPtr<SWidget>> B = PYRMoveLearning::Buttons(W);
        if (!B.IsValidIndex(Index) || !B[Index].IsValid()) return false;
        const FPointerEvent E(0, FVector2D(10, 10), FVector2D(10, 10), TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
        return B[Index]->OnMouseButtonDown(B[Index]->GetCachedGeometry(), E).IsEventHandled();
    };
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 10}});
        if (S->Party[0].Carried.Num() != 4) M4_FAIL(TEXT("Lv 10 Pikachu carries %d moves"), S->Party[0].Carried.Num());
        St.Seen.Reset();
        for (const FName& M : S->Party[0].Carried) St.Seen.Add(M.ToString());
        S->Bag.Reset();
        S->AddItem(Tm, 1);
        Place(Test, TEXT("ROUTE_1"), 9, 26, EYDir::Up);
        Me->TextLog.Reset();
        PYRScreens::UseItem(W, Tm);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // "Booted up a TM!", "Teach BIDE to a POKéMON?" YES, then Pikachu from the party.
        if (TopId(Test) == TEXT("learn")) M4_NEXT();
        if (TopId(Test) == TEXT("party")) Press(Test, EPYRKey::Confirm);
        else Advance(Test, true);
        if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the TM never reached the learn prompt: %s"), *Me->ScreenText());
        return EGameTest::Running;
    }
    if (St.Step == 2)
    {
        if (!Stage(W, EStage::Ask, V) || !V.Text.Contains(FString::Printf(TEXT("trying to learn %s!"), *Name(TmInfo->Move)))) M4_FAIL(TEXT("the TM's prompt says '%s'"), *V.Text);
        St.Note = FString::Printf(TEXT("TM34: '%s', "), *FirstLineOf(V.Text));
        M4_WAIT(PYRMoveLearning::InputDelay + .1);
        if (!Click(1)) M4_FAIL(TEXT("no NO button to click"));
        if (!Stage(W, EStage::Abandon, V)) M4_FAIL(TEXT("clicking NO did not ask to abandon"));
        if (!Click(0)) M4_FAIL(TEXT("no YES button to click"));
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (Me->AnyOpen()) { Advance(Test, true); if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the TM's texts never closed")); return EGameTest::Running; }
        for (const FString& M : St.Seen) if (!S->Party[0].Carried.Contains(FName(*M))) M4_FAIL(TEXT("%s was forgotten"), *M);
        if (S->ItemCount(Tm) != 1) M4_FAIL(TEXT("the TM was used up without being learned"));
        St.Note += TEXT("clicked NO then YES: did not learn, the TM kept; ");
        // A Pokemon that learns a move the moment it evolves (Yellow's learnset of the new form at its level).
        From = Into = EvoMove = NAME_None;
        for (const FName& Sp : PYRData::AllSpecies())
            for (int32 L = 2; L <= 60 && From.IsNone(); ++L)
            {
                const FName To = PYRData::EvolutionAt(Sp, L);
                if (To.IsNone()) continue;
                for (int32 L2 = L; L2 <= FMath::Min(60, PYRData::LearnsetCap(To)) && From.IsNone(); ++L2)
                    for (const FName& M : PYRData::MovesLearned(To, L2 - 1, L2))
                        if (!PYRData::DefaultMoves(Sp, L2).Contains(M)) { From = Sp; Into = To; EvoMove = M; EvoLevel = L2; break; }
            }
        if (From.IsNone()) M4_FAIL(TEXT("no species learns a move on evolving in the tables"));
        // Four moves already (its own, then fillers it can't be learning).
        FPYRMonSpec Spec;
        Spec.Species = From;
        Spec.Level = EvoLevel;
        Spec.Moves = PYRData::DefaultMoves(From, EvoLevel);
        for (const FName& Fill : {PYRData::Tackle, PYRData::Growl, PYRData::Leer, PYRData::Scratch, PYRData::Harden})
            if (Spec.Moves.Num() < 4 && Fill != EvoMove) Spec.Moves.AddUnique(Fill);
        S->DebugSetParty({Spec});
        S->Party[0].EvolvePending = Into;
        St.Seen.Reset();
        for (const FName& M : S->Party[0].Carried) St.Seen.Add(M.ToString());
        if (S->Party[0].Carried.Num() != 4) M4_FAIL(TEXT("%s carries %d moves"), *From.ToString(), S->Party[0].Carried.Num());
        APokemonYellowReimagineCharacter* R = Red(Test);
        PYRScreens::EvolvePending(W, R->GetActorLocation() + R->GetActorForwardVector() * 250.f - FVector(0, 0, 88.f), R->GetActorRotation().Yaw + 180.f, nullptr);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        // The evolution scene plays (its text goes on with confirm), then the prompt.
        if (TopId(Test) == TEXT("learn")) M4_NEXT();
        if (Me->IsTalking()) Advance(Test, true);
        if (Test.Seconds - St.T0 > 40) M4_FAIL(TEXT("%s -> %s never asked about %s: %s"), *From.ToString(), *Into.ToString(), *EvoMove.ToString(), *Me->ScreenText());
        return EGameTest::Running;
    }
    if (St.Step == 5)
    {
        if (!Stage(W, EStage::Ask, V) || !V.Text.Contains(FString::Printf(TEXT("trying to learn %s!"), *Name(EvoMove)))) M4_FAIL(TEXT("the evolution's prompt says '%s'"), *V.Text);
        if (S->Party[0].Species != Into) M4_FAIL(TEXT("it did not evolve"));
        M4_WAIT(PYRMoveLearning::InputDelay + .1);
        // Keep the four with the keys: Down to NO, Enter, Enter (YES, abandon).
        Tap(W, EKeys::Down);
        Tap(W, EKeys::Enter);
        Tap(W, EKeys::Enter);
        M4_NEXT();
    }
    if (Me->AnyOpen()) { Advance(Test, true); if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the evolution's texts never closed")); return EGameTest::Running; }
    for (const FString& M : St.Seen) if (!S->Party[0].Carried.Contains(FName(*M))) M4_FAIL(TEXT("%s was forgotten"), *M);
    M4_PASS(TEXT("%s%s Lv %d evolving into %s: '%s ... trying to learn %s!' in the same prompt, kept the four"), *St.Note, *From.ToString(), EvoLevel, *Into.ToString(),
        *UPYRTrainerState::DisplayName(S->Party[0]), *Name(EvoMove));
}

// ------------------------------------------------------------------------------------------------ 2. the battle pause
namespace M4T
{
// What must hold still while the battle is paused.
struct FSnap
{
    float PlayerHp = 0, FoeHp = 0, HealLeft = 0, BrainClock = 0, Swap = 0, Potion = 0;
    float PlayerCd[4] = {0, 0, 0, 0}, FoeCd[4] = {0, 0, 0, 0};
    double NextPoison = 0, Clock = 0;
    FVector FoeAt = FVector::ZeroVector, PlayerAt = FVector::ZeroVector, Head = FVector::ZeroVector;
    int32 Phase = -1;
    bool bLive = false;
};

APYRAttack* LiveFoeAttack(UWorld* World)
{
    for (TActorIterator<APYRAttack> It(World); It; ++It)
        if (It->IsLive() && It->User.IsValid() && !It->User->bPlayerSide) return *It;
    return nullptr;
}

FSnap Take(APYRBattleManager* Mgr, APYRMonPawn* P, APYRAttack* A)
{
    FSnap S;
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (P) { S.PlayerHp = P->Hp; S.HealLeft = P->HealLeft; S.NextPoison = P->NextPoisonTick; S.PlayerAt = P->GetActorLocation(); for (int32 I = 0; I < 4; ++I) S.PlayerCd[I] = P->Cooldown[I]; }
    if (F) { S.FoeHp = F->Hp; S.BrainClock = F->BrainClock; S.FoeAt = F->GetActorLocation(); for (int32 I = 0; I < 4; ++I) S.FoeCd[I] = F->Cooldown[I]; }
    S.Swap = Mgr->SwapSecondsLeft();
    S.Potion = Mgr->PotionSecondsLeft();
    S.Clock = Mgr->Now();
    if (A) { S.Head = A->Head; S.Phase = int32(A->Phase); S.bLive = A->IsLive(); }
    return S;
}

// Empty when nothing moved; else what did.
FString Diff(const FSnap& A, const FSnap& B)
{
    TArray<FString> D;
    if (A.PlayerHp != B.PlayerHp) D.Add(FString::Printf(TEXT("HP %.1f->%.1f"), A.PlayerHp, B.PlayerHp));
    if (A.FoeHp != B.FoeHp) D.Add(FString::Printf(TEXT("foe HP %.1f->%.1f"), A.FoeHp, B.FoeHp));
    if (A.HealLeft != B.HealLeft) D.Add(TEXT("Potion heal"));
    if (A.BrainClock != B.BrainClock) D.Add(TEXT("foe AI timer"));
    if (A.Swap != B.Swap) D.Add(FString::Printf(TEXT("swap timer %.2f->%.2f"), A.Swap, B.Swap));
    if (A.Potion != B.Potion) D.Add(FString::Printf(TEXT("Potion timer %.2f->%.2f"), A.Potion, B.Potion));
    if (A.NextPoison != B.NextPoison) D.Add(TEXT("poison tick"));
    if (A.Clock != B.Clock) D.Add(TEXT("battle clock"));
    for (int32 I = 0; I < 4; ++I) if (A.PlayerCd[I] != B.PlayerCd[I] || A.FoeCd[I] != B.FoeCd[I]) { D.Add(FString::Printf(TEXT("cooldown %d"), I)); break; }
    if (!A.FoeAt.Equals(B.FoeAt, .01f)) D.Add(TEXT("foe position"));
    if (!A.PlayerAt.Equals(B.PlayerAt, .01f)) D.Add(TEXT("player position"));
    if (!A.Head.Equals(B.Head, .01f) || A.Phase != B.Phase || A.bLive != B.bLive) D.Add(TEXT("the attack in flight"));
    return FString::Join(D, TEXT(", "));
}

// The pause fight: Pikachu, Pidgey and Caterpie against a Weedle with Poison Sting (a needle in flight).
void StartPauseFight(FGameTestContext& Test)
{
    PYRTest::Fight(Test, {PYRTest::Mon(PYRData::Pikachu, 14), PYRTest::Mon(PYRData::Pidgey, 12), PYRTest::Mon(PYRData::Caterpie, 12)},
        {PYRTest::Mon(PYRData::Weedle, 9, {PYRData::PoisonSting, PYRData::StringShot})}, EPYRBattleKind::Wild, FVector(0, 900, 0));
}

// Everything running at once: the swap timer (from a swap), a Potion healing and its timer, a move's cooldown, poison,
// the foe's brain, and the foe's needle on its way (the player out of its reach, so it flies on).
void SetRunning(APYRBattleManager* Mgr)
{
    APYRMonPawn* P = Mgr->GetPlayerMon();
    APYRMonPawn* F = Mgr->GetFoeMon();
    if (!P || !F) return;
    F->Hp = F->MaxHp = 5000.f;
    PYRTest::Place(P, Mgr->GetCenter() + FVector(0, -400, 0));
    PYRTest::Place(F, P->GetActorLocation() + FVector(0, PYRData::Move(PYRData::PoisonSting)->Range + 500.f, 0));
    PYRTest::Face(F, P);
    PYRTest::Face(P, F);
    P->Hp = FMath::RoundToFloat(P->MaxHp * .5f);
    Mgr->PressPotion();
    Mgr->PressMove(0);
    P->Status = EPYRStatus::Poison;
    P->NextPoisonTick = Mgr->Now() + .7;
    Mgr->DebugForceFoeMove(PYRData::PoisonSting);
    Mgr->bFoePassive = false;   // The foe's brain runs (its timer is one of the things that must stop).
}
}

// The switch menu, opened by Q (tapped, so it stays open) while Weedle's needle is in flight, a Potion heals, a move
// and the swap timer cool down and poison ticks: for 5 s nothing changes (and a hit lands nothing). Esc closes it and
// everything goes on.
GAME_TEST(M4t_Switch_PausesBattle, 120)
{
    static FState St;
    static FSnap Before;
    static TWeakObjectPtr<APYRAttack> Needle;   // The foe's needle caught in flight.
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UWorld* W = Test.World;
    if (St.Step == 0) { StartPauseFight(Test); M4_NEXT(); }
    if (St.Step == 1)
    {
        M4_WAIT(1.2);
        Mgr->PressSwap(1);   // Pidgey in: the 30 s swap timer runs.
        if (Mgr->ActiveIndex() != 1) M4_FAIL(TEXT("the first swap failed"));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(1.3);
        SetRunning(Mgr);
        M4_NEXT();
    }
    APYRAttack* A = LiveFoeAttack(W);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (St.Step == 3)
    {
        // The needle is flying: Q down and up (no direction), as a tap on the stream's Party button would.
        if (!A || A->Phase != APYRAttack::EPhase::Active) { if (Test.Seconds - St.T0 > 4) M4_FAIL(TEXT("Weedle's Poison Sting never flew")); return EGameTest::Running; }
        Tap(W, EKeys::Q);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        if (!Mgr->IsBattlePaused()) { if (Test.Seconds - St.T0 > 1) M4_FAIL(TEXT("Q did not pause the fight")); return EGameTest::Running; }
        if (!Mgr->IsWheelOpen() || !Mgr->IsPausedFor(APYRBattleManager::PauseSwitch())) M4_FAIL(TEXT("paused without the switch menu"));
        if (!A || A->Phase != APYRAttack::EPhase::Active) M4_FAIL(TEXT("the needle was gone before the menu opened (retry)"));
        if (!P || P->HealLeft <= 0 || Mgr->PotionSecondsLeft() <= 0 || Mgr->SwapSecondsLeft() <= 0 || P->Cooldown[0] <= 0)
            M4_FAIL(TEXT("not everything was running (heal %.1f, Potion %.1f, swap %.1f, cooldown %.2f)"), P ? P->HealLeft : -1.f, Mgr->PotionSecondsLeft(), Mgr->SwapSecondsLeft(), P ? P->Cooldown[0] : -1.f);
        Needle = A;
        Before = Take(Mgr, P, A);
        St.Note = FString::Printf(TEXT("open with the needle at %s, heal %.1f HP left, Potion %.1f s, swap %.1f s, cooldown %.2f s, poison due; "),
            *A->Head.ToCompactString(), P->HealLeft, Before.Potion, Before.Swap, Before.PlayerCd[0]);
        M4_NEXT();
    }
    if (St.Step == 5)
    {
        // A hit tried while paused lands nothing.
        if (St.I == 0 && Test.Seconds - St.T0 > 1) { St.I = 1; if (Mgr->ResolveHit(Mgr->GetFoeMon(), P, *PYRData::Move(PYRData::Tackle), P->GetActorLocation()) != 0.f) M4_FAIL(TEXT("a hit did damage while paused")); }
        const FString Moved = Diff(Before, Take(Mgr, P, Needle.Get()));
        if (!Moved.IsEmpty()) M4_FAIL(TEXT("after %.1f s paused: %s"), Test.Seconds - St.T0, *Moved);
        if (!Mgr->IsWheelOpen()) M4_FAIL(TEXT("the menu closed by itself"));
        M4_WAIT(5.0);
        St.Note += FString::Printf(TEXT("5 s later nothing moved (held %.1f s); "), Mgr->PausedSeconds());
        Tap(W, EKeys::Escape);
        if (Mgr->IsWheelOpen() || Mgr->IsBattlePaused()) M4_FAIL(TEXT("Esc did not close the menu"));
        M4_NEXT();
    }
    M4_WAIT(1.0);
    const FSnap After = Take(Mgr, P, Needle.Get());
    TArray<FString> Still;
    if (After.Clock - Before.Clock < .8) Still.Add(TEXT("battle clock"));
    if (After.Swap > Before.Swap - .8f) Still.Add(TEXT("swap timer"));
    if (After.Potion > Before.Potion - .8f) Still.Add(TEXT("Potion timer"));
    if (After.PlayerCd[0] >= Before.PlayerCd[0]) Still.Add(TEXT("cooldown"));
    if (After.HealLeft >= Before.HealLeft && Before.HealLeft > 0) Still.Add(TEXT("Potion heal"));
    if (After.Head.Equals(Before.Head, .01f) && After.bLive) Still.Add(TEXT("the needle"));
    if (After.FoeAt.Equals(Before.FoeAt, .01f) && After.BrainClock == Before.BrainClock && Mgr->TellLog.Num() == 0) Still.Add(TEXT("the foe"));
    if (!Still.IsEmpty()) M4_FAIL(TEXT("%s1 s after closing these did not go on: %s"), *St.Note, *FString::Join(Still, TEXT(", ")));
    M4_PASS(TEXT("%sEsc closed it and 1 s later all of it went on (swap %.1f -> %.1f, Potion %.1f -> %.1f, heal %.1f -> %.1f)"), *St.Note, Before.Swap, After.Swap,
        Before.Potion, After.Potion, Before.HealLeft, After.HealLeft);
}

// The same hold for the switch forced by a faint: it can't be cancelled (Q, Esc, Backspace do nothing), the needle and
// every timer wait, and the arrows and Enter send the next Pokemon, which still gets its half second untouchable.
GAME_TEST(M4t_ForcedSwitch_PausesBattle, 120)
{
    static FState St;
    static FSnap Before;
    static TWeakObjectPtr<APYRAttack> Needle;   // The foe's needle caught in flight.
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UWorld* W = Test.World;
    if (St.Step == 0) { StartPauseFight(Test); M4_NEXT(); }
    if (St.Step == 1)
    {
        M4_WAIT(1.2);
        Mgr->PressSwap(1);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(1.3);
        SetRunning(Mgr);
        M4_NEXT();
    }
    APYRAttack* A = LiveFoeAttack(W);
    APYRMonPawn* P = Mgr->GetPlayerMon();
    if (St.Step == 3)
    {
        if (!A || A->Phase != APYRAttack::EPhase::Active) { if (Test.Seconds - St.T0 > 4) M4_FAIL(TEXT("Weedle's Poison Sting never flew")); return EGameTest::Running; }
        // Pidgey faints (a blow it can't survive) while the needle flies.
        P->Hp = 1;
        P->HealLeft = 0;
        P->InvulnerableUntil = 0;
        Mgr->bNoRandom = true;
        Mgr->ResolveHit(Mgr->GetFoeMon(), P, *PYRData::Move(PYRData::Tackle), P->GetActorLocation());
        if (!P->bFainted) M4_FAIL(TEXT("Pidgey did not faint"));
        if (!Mgr->IsWheelOpen() || !Mgr->IsWheelForced() || !Mgr->IsBattlePaused()) M4_FAIL(TEXT("the faint did not open a paused, forced switch menu"));
        if (A->Phase != APYRAttack::EPhase::Active) M4_FAIL(TEXT("the needle was gone (retry)"));
        Needle = A;
        Before = Take(Mgr, nullptr, A);
        St.Note = FString::Printf(TEXT("Pidgey fainted with the needle at %s, swap %.1f s; "), *A->Head.ToCompactString(), Before.Swap);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        const FString Moved = Diff(Before, Take(Mgr, nullptr, Needle.Get()));
        if (!Moved.IsEmpty()) M4_FAIL(TEXT("after %.1f s paused: %s"), Test.Seconds - St.T0, *Moved);
        if (St.I == 0 && Test.Seconds - St.T0 > 1)
        {
            // No way out of a forced switch.
            St.I = 1;
            for (const FKey& K : {EKeys::Q, EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right, EKeys::X}) Tap(W, K);
            if (!Mgr->IsWheelOpen() || !Mgr->IsBattlePaused()) M4_FAIL(TEXT("a forced switch was cancelled"));
        }
        M4_WAIT(5.0);
        // The arrows move the cursor (Caterpie, then Pikachu: Pidgey is fainted), Enter sends it out.
        const int32 Start = Mgr->GetWheelCursor();
        Tap(W, EKeys::Down);
        const int32 Pick = Mgr->GetWheelCursor();
        if (Pick == Start || Pick == 1) M4_FAIL(TEXT("the cursor went %d -> %d"), Start, Pick);
        Tap(W, EKeys::Enter);
        if (Mgr->IsWheelOpen() || Mgr->IsBattlePaused() || Mgr->ActiveIndex() != Pick) M4_FAIL(TEXT("Enter did not send out slot %d"), Pick + 1);
        APYRMonPawn* In = Mgr->GetPlayerMon();
        const float Untouchable = In ? float(In->InvulnerableUntil - Mgr->Now()) : 0.f;
        if (!PYRTest::Near(Untouchable, .5f, .05f)) M4_FAIL(TEXT("the incoming Pokemon is untouchable for %.2f s"), Untouchable);
        St.Note += FString::Printf(TEXT("5 s later nothing moved; Q/Esc/Backspace/B/X refused; Down + Enter sent slot %d, untouchable %.2f s; "), Pick + 1, Untouchable);
        M4_NEXT();
    }
    M4_WAIT(1.0);
    const FSnap After = Take(Mgr, nullptr, Needle.Get());
    if (After.Clock - Before.Clock < .8 || After.Swap > Before.Swap - .8f) M4_FAIL(TEXT("%sthe fight did not go on (clock +%.2f, swap %.1f -> %.1f)"), *St.Note, After.Clock - Before.Clock, Before.Swap, After.Swap);
    if (After.Head.Equals(Before.Head, .01f) && After.bLive) M4_FAIL(TEXT("%sthe needle stayed frozen"), *St.Note);
    M4_PASS(TEXT("%sthen the fight went on (swap %.1f -> %.1f)"), *St.Note, Before.Swap, After.Swap);
}

// ------------------------------------------------------------------------------------------------ 3. Run
namespace M4T
{
enum class ERunBy : uint8 { Keyboard, Touch, HudClick, HudTap };

// A grass tile on Route 1 with a walkable, grass-free tile south of it: Red steps north into the grass.
bool GrassStep(FGameTestContext& Test, FIntPoint& Grass)
{
    const FYMap* M = Data(Test) ? Data(Test)->Map(TEXT("ROUTE_1")) : nullptr;
    if (!M) return false;
    for (int32 Y = M->Height - 3; Y > 2; --Y)
        for (int32 X = 1; X < M->Width - 1; ++X)
        {
            const TCHAR Here = M->Tile(X, Y), South = M->Tile(X, Y + 1);
            if (Here == TEXT('g') && South != TEXT('g') && !YTile::IsSolid(South) && M->WarpAt(X, Y + 1) == INDEX_NONE && !YTile::IsSolid(Here)) { Grass = FIntPoint(X, Y); return true; }
        }
    return false;
}

// One wild battle met by walking into Route 1's grass, run from by Way; checks the escape and the way back.
EGameTest RunFromWild(FGameTestContext& Test, FState& St, ERunBy Way, bool bGapCheck, const TCHAR* What)
{
    M4_READY();
    UWorld* W = Test.World;
    UPYRField* F = Field(Test);
    UPYREncounters* Enc = UPYREncounters::Get(W);
    APYRBattleManager* Mgr = APYRBattleManager::Get(W);
    UPYRTrainerState* S = State(Test);
    static FIntPoint Grass;
    static int32 XpBefore = 0, PartyBefore = 0, StoreBefore = 0, ReturnsBefore = 0;
    if (St.Step == 0)
    {
        Quiet(false, true);
        AfterPokedex(Test);
        Party(Test, {{TEXT("pikachu"), 9}});
        if (!GrassStep(Test, Grass)) M4_FAIL(TEXT("no grass on Route 1"));
        Place(Test, TEXT("ROUTE_1"), Grass.X, Grass.Y + 1, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.5);
        Enc->ForceSlot = 0;
        Enc->bForceEncounter = true;
        ReturnsBefore = Enc->Returns;
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        if (!Enc->IsBusy())
        {
            WalkToTile(Test, TEXT("ROUTE_1"), Grass.X, Grass.Y, 10.f);
            if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no encounter stepping into the grass at %d,%d"), Grass.X, Grass.Y);
            return EGameTest::Running;
        }
        StopWalking(Test);
        St.P = Enc->ReturnTile;
        St.Note = FString::Printf(TEXT("%s: met a wild %s at ROUTE_1 %d,%d; "), What, *Enc->GetFight().Foes[0].Species.ToString(), St.P.X, St.P.Y);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        if (Enc->GetPhase() != EPYREncounterPhase::Fight || !Mgr->IsBattleActive() || Test.Seconds - St.T0 < 2.0)
        {
            if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the fight never started"));
            return EGameTest::Running;
        }
        Mgr->bFoePassive = true;   // The wild Pokemon holds still: the run, not the fight, is under test.
        XpBefore = S->Party[0].Xp;
        PartyBefore = S->Party.Num();
        StoreBefore = S->Storage.Num();
        if (bGapCheck)
        {
            // The worn gap's 3 s timer holds while the switch menu has the fight paused.
            Tap(W, EKeys::Q);
            St.I = 1;
        }
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        if (St.I == 1)
        {
            if (!Mgr->IsBattlePaused()) { if (Test.Seconds - St.T0 > 1) M4_FAIL(TEXT("Q did not pause the fight")); return EGameTest::Running; }
            Enc->GapTime = 1.5f;   // As if Red had stood in the gap for 1.5 s.
            St.I = 3;
            St.T0 = Test.Seconds;
            return EGameTest::Running;
        }
        if (St.I == 3)
        {
            M4_WAIT(1.0);
            if (!Mgr->IsBattlePaused()) M4_FAIL(TEXT("the switch menu let go of the fight"));
            if (Enc->GapTime != 1.5f) M4_FAIL(TEXT("the gap timer moved under the switch menu (%.2f)"), Enc->GapTime);
            // Run doesn't go through while the menu is up.
            Tap(W, EKeys::X);
            if (Mgr->RunPresses != 0 || Mgr->IsEnding()) M4_FAIL(TEXT("Run went through the switch menu"));
            Tap(W, EKeys::Escape);
            St.Note += TEXT("the gap timer held 1 s under the switch menu, Run ignored there; ");
            St.I = 2;
            St.T0 = Test.Seconds;
            return EGameTest::Running;
        }
        M4_WAIT(.3);
        switch (Way)
        {
        case ERunBy::Keyboard: Tap(W, EKeys::X); break;
        case ERunBy::Touch:
        {
            const FKey K = StreamKey(TEXT("RUN"));
            if (!K.IsValid()) M4_FAIL(TEXT("touch-layout.json has no RUN button"));
            Tap(W, K);
            St.Note += FString::Printf(TEXT("the stream's RUN button (key %s); "), *K.ToString());
            break;
        }
        case ERunBy::HudClick: if (!PressRunButton(Mgr, false)) M4_FAIL(TEXT("the HUD's RUN button took no click")); St.Note += TEXT("clicked the HUD's RUN; "); break;
        case ERunBy::HudTap: if (!PressRunButton(Mgr, true)) M4_FAIL(TEXT("the HUD's RUN button took no tap")); St.Note += TEXT("tapped the HUD's RUN; "); break;
        }
        M4_NEXT();
    }
    if (St.Step == 5)
    {
        // The key goes through the player's input on the next frame.
        if (Mgr->IsBattleActive() && !Mgr->IsEnding()) { if (Test.Seconds - St.T0 > 1.5) M4_FAIL(TEXT("Run did nothing")); return EGameTest::Running; }
        if (!PYRTest::CaptionSeen(Mgr, TEXT("Got away safely!"))) M4_FAIL(TEXT("no 'Got away safely!' (last '%s')"), *Mgr->LastCaption().ToString());
        M4_NEXT();
    }
    // Back on the road (then the fade back to colour).
    if (St.Step == 6)
    {
        if (Enc->IsBusy() || Enc->Returns == ReturnsBefore) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("never came back from the fight")); return EGameTest::Running; }
        M4_NEXT();
    }
    M4_WAIT(.8);
    APlayerController* PC = W->GetFirstPlayerController();
    APokemonYellowReimagineCharacter* R = Red(Test);
    if (Enc->LastOutcome != EPYRBattleOutcome::Fled) M4_FAIL(TEXT("the fight ended %d, not Fled"), int32(Enc->LastOutcome));
    if (F->CurrentMap != TEXT("ROUTE_1") || F->CurrentTile != St.P) M4_FAIL(TEXT("Red is at %s %d,%d, not the encounter's tile %d,%d"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y, St.P.X, St.P.Y);
    if (!PC || PC->GetPawn() != R || PC->GetViewTarget() != R) M4_FAIL(TEXT("Red is not back in control with his camera"));
    if (S->Party[0].Xp != XpBefore) M4_FAIL(TEXT("experience was given (%d -> %d)"), XpBefore, S->Party[0].Xp);
    if (S->Party.Num() != PartyBefore || S->Storage.Num() != StoreBefore) M4_FAIL(TEXT("the wild Pokemon was caught"));
    if (Mgr->IsBattleActive() || Mgr->GetFoeMon()) M4_FAIL(TEXT("the wild Pokemon is still there"));
    for (TActorIterator<APYRMonPawn> It(W); It; ++It) if (!It->bVisualOnly) M4_FAIL(TEXT("a fighter is left: %s"), *It->Species.ToString());
    if (Enc->GetRing() || Enc->GetCamera()) M4_FAIL(TEXT("the ring or the battle camera is left"));
    if (F->GetFade() > .05f) M4_FAIL(TEXT("the colour did not come back (fade %.2f)"), F->GetFade());
    const UGbMusicPlayer* Music = UGbMusicPlayer::Get(W);
    const FYMap* Map = F->GetMap();
    if (Music && Map && !Map->Music.IsEmpty() && Music->GetSong() != FName(*Map->Music)) M4_FAIL(TEXT("the music is '%s', not Route 1's '%s'"), *Music->GetSong().ToString(), *Map->Music);
    const bool bSaid = Said(W).Contains(TEXT("Got away safely!"));
    M4_PASS(TEXT("%s'Got away safely!'%s; Fled, back on ROUTE_1 %d,%d with his camera, Route 1's music and full colour, no experience, the wild Pokemon gone"),
        *St.Note, bSaid ? TEXT("") : TEXT(" (caption only)"), St.P.X, St.P.Y);
}
}

GAME_TEST(M4t_Run_WildEscapes_Keyboard, 120) { static FState St; return RunFromWild(Test, St, ERunBy::Keyboard, true, TEXT("X")); }
GAME_TEST(M4t_Run_WildEscapes_Touch, 120) { static FState St; return RunFromWild(Test, St, ERunBy::Touch, false, TEXT("stream touch")); }

// The HUD's RUN button: a mouse click in one wild battle, then a finger tap in the next.
GAME_TEST(M4t_Run_WildEscapes_HUDButton, 180)
{
    static FState Click, TapRun;
    static bool bClicked = false;
    if (!bClicked)
    {
        const EGameTest R = RunFromWild(Test, Click, ERunBy::HudClick, false, TEXT("HUD click"));
        if (R != EGameTest::Passed) return R;
        bClicked = true;
        Click.Note = Test.Detail;
        return EGameTest::Running;
    }
    const EGameTest R = RunFromWild(Test, TapRun, ERunBy::HudTap, false, TEXT("HUD tap"));
    if (R == EGameTest::Passed) Test.Detail = Click.Note + TEXT(" || ") + Test.Detail;
    return R;
}

// Trainer battles of every kind refuse: a trainer (X), Brock's gym (the pad's Y), Gary in the lab (a click on RUN)
// and a trainer again from the stream's RUN. Yellow's line shows; the fight goes on with the same HP and party.
// (The old man's lesson has no fighter of Red's to take a key; PressRun refuses every kind but Wild.)
GAME_TEST(M4t_Run_TrainerRefused, 150)
{
    static FState St;
    if (!PYRTest::Ready(Test)) return EGameTest::Running;
    APYRBattleManager* Mgr = PYRTest::M(Test);
    UPYRTrainerState* S = PYRTest::S(Test);
    UWorld* W = Test.World;
    struct FCase { EPYRBattleKind Kind; const TCHAR* What; };
    static const FCase Cases[] = {{EPYRBattleKind::Trainer, TEXT("Bug Catcher (X)")}, {EPYRBattleKind::Gym, TEXT("Brock's gym (pad Y)")},
        {EPYRBattleKind::Lab, TEXT("Gary in the lab (HUD RUN click)")}, {EPYRBattleKind::Trainer, TEXT("Jessie and James (the stream's RUN)")}};
    static float PHp = 0, FHp = 0;
    static int32 Refused = 0;
    static FString PartyBefore;
    auto PartyText = [S]() { FString T; for (const FPYRMon& M : S->Party) T += FString::Printf(TEXT("%s %d %.0f;"), *M.Species.ToString(), M.Level, M.Hp); return T; };
    if (St.I >= UE_ARRAY_COUNT(Cases)) M4_PASS(TEXT("%s"), *St.Note);
    const FCase& C = Cases[St.I];
    if (St.Step == 0)
    {
        PYRTest::Fight(Test, {PYRTest::Mon(PYRData::Pikachu, 12), PYRTest::Mon(PYRData::Pidgey, 9)}, {PYRTest::Mon(PYRData::Geodude, 10), PYRTest::Mon(PYRData::Onix, 12)}, C.Kind);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(1.5);
        if (!Mgr->IsBattleActive()) M4_FAIL(TEXT("%s: no fight"), C.What);
        PHp = Mgr->GetPlayerMon() ? Mgr->GetPlayerMon()->Hp : -1.f;
        FHp = Mgr->GetFoeMon() ? Mgr->GetFoeMon()->Hp : -1.f;
        PartyBefore = PartyText();
        Refused = Mgr->RunRefusals;
        Mgr->CaptionLog.Reset();
        switch (St.I)
        {
        case 0: Tap(W, EKeys::X); break;
        case 1: Tap(W, EKeys::Gamepad_FaceButton_Top); break;
        case 2: if (!PressRunButton(Mgr, false)) M4_FAIL(TEXT("%s: the HUD's RUN took no click"), C.What); break;
        default:
        {
            const FKey K = StreamKey(TEXT("RUN"));
            if (!K.IsValid()) M4_FAIL(TEXT("touch-layout.json has no RUN button"));
            Tap(W, K);
            break;
        }
        }
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        if (Mgr->RunRefusals == Refused) { if (Test.Seconds - St.T0 > 1.5) M4_FAIL(TEXT("%s: Run did nothing"), C.What); return EGameTest::Running; }
        const FString Line = TEXT("No! There's no running from a trainer battle!");
        if (!PYRTest::CaptionSeen(Mgr, Line)) M4_FAIL(TEXT("%s: no refusal (last '%s')"), C.What, *Mgr->LastCaption().ToString());
        M4_NEXT();
    }
    M4_WAIT(1.5);
    if (!Mgr->IsBattleActive() || Mgr->IsEnding()) M4_FAIL(TEXT("%s: the fight ended"), C.What);
    const float PNow = Mgr->GetPlayerMon() ? Mgr->GetPlayerMon()->Hp : -1.f, FNow = Mgr->GetFoeMon() ? Mgr->GetFoeMon()->Hp : -1.f;
    if (PNow != PHp || FNow != FHp) M4_FAIL(TEXT("%s: HP changed (%.0f -> %.0f, foe %.0f -> %.0f)"), C.What, PHp, PNow, FHp, FNow);
    if (PartyText() != PartyBefore) M4_FAIL(TEXT("%s: the party changed"), C.What);
    St.Note += FString::Printf(TEXT("%s: refused, 1.5 s later still fighting with the same HP and party; "), C.What);
    ++St.I;
    St.Step = 0;
    return EGameTest::Running;
}
