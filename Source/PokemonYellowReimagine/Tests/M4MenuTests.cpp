// M4 menus: START and every screen under it (Pokedex, party and stats, the bag, the trainer card, SAVE, OPTION),
// naming Red and his rival, and the PCs (Pokemon boxes, items, Oak's rating).
#include "Tests/M4TestKit.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRScreensCommon.h"
#include "Menus/PYRSysText.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Save/PYRSave.h"
#include "Save/SaveSlot.h"
#include "Misc/FileHelper.h"

using namespace M4;

namespace
{
void Key(FGameTestContext& Test, const FKey& K)
{
    if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Pressed, 1.f));
}

TArray<FString> StartItems(FGameTestContext& Test)
{
    TSharedPtr<FPYRScreen> S = Menus(Test)->Find(TEXT("start"));
    return S ? StaticCastSharedPtr<FPYRListScreen>(S)->Items : TArray<FString>();
}

// START, then down to an entry and Confirm.
bool OpenFromStart(FGameTestContext& Test, const FString& Entry)
{
    Menus(Test)->CloseAll();
    PYRScreens::OpenStart(Test.World);
    const int32 At = StartItems(Test).IndexOfByKey(Entry);
    if (At == INDEX_NONE) return false;
    PressN(Test, EPYRKey::Down, At);
    Press(Test, EPYRKey::Confirm);
    return true;
}

FString Desc(FGameTestContext& Test, const TCHAR* Id)
{
    TSharedPtr<FPYRScreen> S = Menus(Test)->Find(Id);
    return S ? S->Describe() : FString();
}
}

// Enter and Esc open START; the entries are the Game Boy's, in order; POKéDEX only once Red has it.
GAME_TEST(Menu_StartOpens, 60)
{
    static FState St;
    if (!ReadyAnyData(Test)) return EGameTest::Running;
    UPYRMenus* M = Menus(Test);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 6}});
        Key(Test, EKeys::Enter);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.1);
        if (!M->IsOpen(TEXT("start"))) M4_FAIL(TEXT("Enter did not open START"));
        const FString Got = FString::Join(StartItems(Test), TEXT(","));
        const FString Want = FString::Printf(TEXT("POKéMON,ITEM,%s,SAVE,OPTION,EXIT"), *Progress(Test)->PlayerName);
        if (Got != Want) M4_FAIL(TEXT("before the Pokedex START shows %s, want %s"), *Got, *Want);
        St.Note = FString::Printf(TEXT("Enter: %s; "), *Got);
        Key(Test, EKeys::Escape);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.1);
        if (M->AnyOpen()) M4_FAIL(TEXT("Esc did not close START"));
        Flags(Test, {TEXT("EVENT_GOT_POKEDEX")});
        Key(Test, EKeys::Escape);
        M4_NEXT();
    }
    M4_WAIT(.1);
    if (!M->IsOpen(TEXT("start"))) M4_FAIL(TEXT("Esc did not open START"));
    const FString Got = FString::Join(StartItems(Test), TEXT(","));
    const FString Want = FString::Printf(TEXT("POKéDEX,POKéMON,ITEM,%s,SAVE,OPTION,EXIT"), *Progress(Test)->PlayerName);
    if (Got != Want) M4_FAIL(TEXT("with the Pokedex START shows %s, want %s"), *Got, *Want);
    if (!Field(Test)->IsLocked()) M4_FAIL(TEXT("Red can walk while START is open"));
    M4_PASS(TEXT("%sEsc closed it; with the Pokedex, Esc opened %s; Red held while it is open"), *St.Note, *Got);
}

// POKéMON lists the party with levels and HP; STATS shows two pages; SWITCH reorders.
GAME_TEST(Menu_PartySummary, 60)
{
    static FState St;
    if (!ReadyAnyData(Test)) return EGameTest::Running;
    UPYRMenus* M = Menus(Test);
    UPYRTrainerState* S = State(Test);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 9}, {TEXT("pidgey"), 4}});
        S->Party[1].Hp = 3;
        if (!OpenFromStart(Test, TEXT("POKéMON"))) M4_FAIL(TEXT("no POKéMON on START"));
        const FString Party = Desc(Test, TEXT("party"));
        if (!Party.Contains(TEXT("PIKACHU :L9")) || !Party.Contains(TEXT("PIDGEY :L4")) || !Party.Contains(FString::Printf(TEXT("HP 3/%d"), FMath::RoundToInt(S->Party[1].MaxHp))))
            M4_FAIL(TEXT("party screen: %s"), *Party.Replace(TEXT("\n"), TEXT(" | ")));
        St.Note = FString::Printf(TEXT("party: %s; "), *Party.Replace(TEXT("\n"), TEXT(" | ")));
        // STATS on Pikachu: page 1, then page 2.
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Confirm);
        if (Desc(Test, TEXT("stats")) != TEXT("stats of party 0, page 1")) M4_FAIL(TEXT("STATS shows '%s'"), *Desc(Test, TEXT("stats")));
        Press(Test, EPYRKey::Confirm);
        if (Desc(Test, TEXT("stats")) != TEXT("stats of party 0, page 2")) M4_FAIL(TEXT("no second stats page"));
        Press(Test, EPYRKey::Confirm);
        if (M->IsOpen(TEXT("stats"))) M4_FAIL(TEXT("STATS did not close"));
        St.Note += TEXT("STATS pages 1 and 2; ");
        // SWITCH Pidgey to the front.
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Up);
        Press(Test, EPYRKey::Confirm);
        if (S->Party[0].Species != TEXT("pidgey")) M4_FAIL(TEXT("SWITCH left %s first"), *S->Party[0].Species.ToString());
        M4_PASS(TEXT("%sSWITCH put Pidgey first"), *St.Note);
    }
    return EGameTest::Running;
}

// The Pokedex counts seen and owned, lists by number, and opens an owned Pokemon's entry from species.json.
GAME_TEST(Menu_Pokedex, 60)
{
    M4_READY();
    UPYRTrainerState* S = State(Test);
    Party(Test, {{TEXT("pikachu"), 9}});
    AfterPokedex(Test);
    S->Seen.Reset();
    S->Caught.Reset();
    S->MarkCaught(TEXT("pikachu"));
    S->MarkSeen(TEXT("rattata"));
    S->MarkSeen(TEXT("pidgey"));
    if (!OpenFromStart(Test, TEXT("POKéDEX"))) M4_FAIL(TEXT("no POKéDEX on START"));
    const FString Top = Desc(Test, TEXT("pokedex"));
    if (!Top.StartsWith(TEXT("SEEN 3 OWN 1"))) M4_FAIL(TEXT("Pokedex head '%s'"), *Top.Left(40));
    // Down to No.025.
    PressN(Test, EPYRKey::Down, 24);
    const FString Rows = Desc(Test, TEXT("pokedex"));
    if (!Rows.Contains(TEXT("> 025 ● PIKACHU"))) M4_FAIL(TEXT("No.025 row: %s"), *Rows.Replace(TEXT("\n"), TEXT(" | ")));
    Press(Test, EPYRKey::Confirm);
    TSharedPtr<FPYRScreen> Entry = Menus(Test)->Find(TEXT("dex-entry"));
    if (!Entry) M4_FAIL(TEXT("no entry for Pikachu"));
    const FPYRSpeciesInfo* Info = PYRData::Species(TEXT("pikachu"));
    const FString Page = StaticCastSharedPtr<PYRScreensCommon::FPageScreen>(Entry)->Pages[0];
    if (!Page.Contains(TEXT("PIKACHU")) || !Page.Contains(Info->Category.ToString()) || !Page.Contains(TEXT("No.025"))) M4_FAIL(TEXT("entry page '%s'"), *Page.Replace(TEXT("\n"), TEXT(" ")));
    M4_PASS(TEXT("SEEN 3 OWN 1; No.025 ● PIKACHU; entry: %s"), *Page.Replace(TEXT("\n"), TEXT(" ")).Left(120));
}

// The bag: 20 slots; key items have no count and can't be tossed; TMs show as TMxx; SELECT moves a slot.
GAME_TEST(Menu_BagKeyTm, 60)
{
    M4_READY();
    UPYRTrainerState* S = State(Test);
    Party(Test, {{TEXT("pikachu"), 9}});
    S->Bag.Reset();
    S->AddItem(UPYRTrainerState::Potion, 3);
    S->AddItem(UPYRTrainerState::OaksParcel, 1);
    S->AddItem(UPYRTrainerState::Tm34, 1);
    if (UPYRTrainerState::BagSlots != 20) M4_FAIL(TEXT("the bag has %d slots"), UPYRTrainerState::BagSlots);
    if (S->AddItem(UPYRTrainerState::OaksParcel, 1) && S->ItemCount(UPYRTrainerState::OaksParcel) != 1) M4_FAIL(TEXT("a key item stacked"));
    if (!OpenFromStart(Test, TEXT("ITEM"))) M4_FAIL(TEXT("no ITEM on START"));
    const FString Bag = Desc(Test, TEXT("bag"));
    if (!Bag.Contains(TEXT("POTION x3")) || !Bag.Contains(TEXT("TM34 x1")) || !Bag.Contains(TEXT("PARCEL"))) M4_FAIL(TEXT("bag: %s"), *Bag.Replace(TEXT("\n"), TEXT(" | ")));
    // TOSS the parcel: too important.
    Menus(Test)->TextLog.Reset();
    Press(Test, EPYRKey::Down);
    Press(Test, EPYRKey::Confirm);
    Press(Test, EPYRKey::Down);
    Press(Test, EPYRKey::Confirm);
    const FString Important = FirstLineOf(PYRGb::Fill(Test.World, PYRSysText::Get(TEXT("TooImportantToTossText"))));
    if (!SaidContains(Test, Important) || !S->HasItem(UPYRTrainerState::OaksParcel)) M4_FAIL(TEXT("tossing the parcel said '%s'"), *Said(Test));
    SkipText(Test);
    // SELECT the Potion and drop it on the TM: they swap.
    Menus(Test)->Find(TEXT("bag"))->Cursor = 0;
    Press(Test, EPYRKey::Confirm);
    PressN(Test, EPYRKey::Down, 2);
    Press(Test, EPYRKey::Confirm);
    PressN(Test, EPYRKey::Down, 2);
    Press(Test, EPYRKey::Confirm);
    if (S->Bag[0].Item != UPYRTrainerState::Tm34 || S->Bag[2].Item != UPYRTrainerState::Potion) M4_FAIL(TEXT("SELECT did not swap: %s"), *Desc(Test, TEXT("bag")).Replace(TEXT("\n"), TEXT(" | ")));
    M4_PASS(TEXT("bag %s; parcel toss: '%s'; SELECT swapped the Potion and TM34"), *Bag.Replace(TEXT("\n"), TEXT(" | ")), *Important);
}

// The trainer card: name, money, play time and the badges earned.
GAME_TEST(Menu_TrainerCard, 60)
{
    if (!ReadyAnyData(Test)) return EGameTest::Running;
    UPYRTrainerState* S = State(Test);
    Party(Test, {{TEXT("pikachu"), 9}});
    S->Money = 4321;
    S->AwardBadge(EPYRBadge::Boulder);
    if (!OpenFromStart(Test, Progress(Test)->PlayerName)) M4_FAIL(TEXT("no %s on START"), *Progress(Test)->PlayerName);
    const FString Card = Desc(Test, TEXT("trainer-card"));
    const FString Want = FString::Printf(TEXT("NAME %s MONEY 4321 BADGES %s"), *Progress(Test)->PlayerName, *UPYRTrainerState::BadgeName(EPYRBadge::Boulder).ToString());
    if (Card != Want) M4_FAIL(TEXT("card '%s', want '%s'"), *Card, *Want);
    M4_PASS(TEXT("%s"), *Card);
}

// SAVE from START writes everything; loading it (CONTINUE) brings back place, party, boxes, bag, PC items, dex,
// money, badges, flags, names and options.
GAME_TEST(Menu_SaveContinue, 90)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    UPYRProgress* P = Progress(Test);
    UPYRSave* Save = UPYRSave::Get(Test.World);
    UPYRField* F = Field(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        UPYRSave::bAllowInAutomation = true;
        Party(Test, {{TEXT("pikachu"), 11}, {TEXT("pidgey"), 6}});
        AfterPokedex(Test);
        S->Money = 2345;
        S->Bag.Reset();
        S->AddItem(UPYRTrainerState::Potion, 4);
        S->AddItem(UPYRTrainerState::Tm34, 1);
        S->AwardBadge(EPYRBadge::Boulder);
        S->MarkSeen(TEXT("rattata"));
        S->MarkCaught(TEXT("pidgey"));
        S->CurrentBox = 2;
        S->Deposit(UPYRTrainerState::MakeMon(TEXT("rattata"), 4));
        P->DepositPc(TEXT("antidote"), 2);
        P->PlayerName = TEXT("ASH");
        P->RivalName = TEXT("GARY");
        P->Options.TextSpeed = EPYRTextSpeed::Slow;
        Place(Test, TEXT("VIRIDIAN_CITY"), 21, 20, EYDir::Left);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.3);
        St.I = Save->Saves;
        if (!OpenFromStart(Test, TEXT("SAVE"))) M4_FAIL(TEXT("no SAVE on START"));
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        Advance(Test, true);
        if (Save->Saves == St.I) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("SAVE never saved: %s"), *Menus(Test)->ScreenText()); return EGameTest::Running; }
        SkipText(Test);
        Menus(Test)->CloseAll();
        if (!Save->Exists()) M4_FAIL(TEXT("no save file after SAVE (%s)"), *Save->LastResult);
        // Everything changes...
        Save->ResetToNewGame();
        P->PlayerName = TEXT("RED");
        S->Money = 1;
        Place(Test, TEXT("PALLET_TOWN"), 5, 6, EYDir::Down);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        M4_WAIT(.3);
        // ...and CONTINUE brings it back.
        FPYRSaveGame Loaded;
        const int32 Code = Save->Load(Loaded);
        if (Code != 0 || !Save->Apply(Loaded)) M4_FAIL(TEXT("loading failed (%d): %s"), Code, *Save->LastResult);
        M4_NEXT();
    }
    M4_WAIT(.3);
    UPYRSave::bAllowInAutomation = false;
    TArray<FString> Bad;
    if (F->CurrentMap != TEXT("VIRIDIAN_CITY") || F->CurrentTile != FIntPoint(21, 20)) Bad.Add(FString::Printf(TEXT("place %s (%d,%d)"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y));
    if (S->Party.Num() != 2 || S->Party[0].Level != 11 || S->Party[1].Species != TEXT("pidgey")) Bad.Add(TEXT("party"));
    if (S->Money != 2345) Bad.Add(FString::Printf(TEXT("money %d"), S->Money));
    if (S->ItemCount(UPYRTrainerState::Potion) != 4 || !S->HasItem(UPYRTrainerState::Tm34)) Bad.Add(TEXT("bag"));
    if (!S->HasBadge(EPYRBadge::Boulder)) Bad.Add(TEXT("badge"));
    if (!S->Seen.Contains(TEXT("rattata")) || !S->Caught.Contains(TEXT("pidgey"))) Bad.Add(TEXT("dex"));
    if (S->CurrentBox != 2 || S->BoxCount(2) != 1) Bad.Add(FString::Printf(TEXT("box %d holds %d"), S->CurrentBox, S->BoxCount(2)));
    if (P->PcCount(TEXT("antidote")) != 2) Bad.Add(TEXT("PC items"));
    if (P->PlayerName != TEXT("ASH") || P->RivalName != TEXT("GARY")) Bad.Add(TEXT("names"));
    if (P->Options.TextSpeed != EPYRTextSpeed::Slow) Bad.Add(TEXT("options"));
    if (!P->Has(TEXT("EVENT_GOT_POKEDEX"))) Bad.Add(TEXT("flags"));
    if (Bad.Num()) M4_FAIL(TEXT("after CONTINUE these differ: %s"), *FString::Join(Bad, TEXT(", ")));
    M4_PASS(TEXT("SAVE from START, then CONTINUE restored VIRIDIAN_CITY (21,20), the party, bag, 2345 money, Boulder Badge, dex, box 3, PC items, names ASH/GARY, SLOW text, flags"));
}

// OPTION changes the text speed, and the letters really come out faster or slower.
GAME_TEST(Menu_Options, 60)
{
    static FState St;
    if (!ReadyAnyData(Test)) return EGameTest::Running;
    UPYRProgress* P = Progress(Test);
    UPYRMenus* M = Menus(Test);
    const FString Line = TEXT("ABCDEFGHIJKLMNOPQRST");
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 9}});
        P->Options = FPYROptions();
        if (!OpenFromStart(Test, TEXT("OPTION"))) M4_FAIL(TEXT("no OPTION on START"));
        // TEXT SPEED: left from MEDIUM is FAST.
        Press(Test, EPYRKey::Left);
        if (P->Options.TextSpeed != EPYRTextSpeed::Fast || !Desc(Test, TEXT("options")).Contains(TEXT("▶TEXT SPEED :FAST"))) M4_FAIL(TEXT("left did not pick FAST: %s"), *Desc(Test, TEXT("options")));
        St.A = P->Options.LetterSeconds();
        M->CloseAll();
        M->Say(Line);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // 20 letters at 1/60 s: complete within 0.6 s, so one Confirm closes the box.
        M4_WAIT(.6);
        Press(Test, EPYRKey::Confirm);
        if (M->AnyOpen()) M4_FAIL(TEXT("at FAST the line was not out after 0.6 s"));
        OpenFromStart(Test, TEXT("OPTION"));
        PressN(Test, EPYRKey::Right, 2);   // FAST -> MEDIUM -> SLOW
        if (P->Options.TextSpeed != EPYRTextSpeed::Slow) M4_FAIL(TEXT("right twice did not pick SLOW"));
        St.B = P->Options.LetterSeconds();
        M->CloseAll();
        M->Say(Line);
        M4_NEXT();
    }
    M4_WAIT(.6);
    // At SLOW (5/60 s a letter) the same line is still typing: Confirm only finishes it.
    Press(Test, EPYRKey::Confirm);
    if (!M->AnyOpen()) M4_FAIL(TEXT("at SLOW the line was already out after 0.6 s"));
    M->CloseAll();
    M4_PASS(TEXT("FAST %.3f s a letter (20 letters out in 0.6 s); SLOW %.3f s a letter (still typing at 0.6 s)"), St.A, St.B);
}

// Oak's intro: NEW NAME spells Red's name on the grid, the rival takes a listed name, and the text uses both.
GAME_TEST(Menu_NamingRedRival, 90)
{
    static FState St;
    if (!ReadyAnyData(Test)) return EGameTest::Running;
    UPYRMenus* M = Menus(Test);
    UPYRProgress* P = Progress(Test);
    if (St.Step == 0)
    {
        M->CloseAll();
        M->TextLog.Reset();
        PYRScreens::Intro(Test.World, [P]() { P->Set(TEXT("test_intro_done")); });
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Oak talks; the name choice: NEW NAME.
        const FName Top = SkipText(Test);
        if (Top != TEXT("choice")) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("no name choice: %s"), *M->ScreenText()); return EGameTest::Running; }
        const FString Items = FString::Join(StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items, TEXT(","));
        if (Items != TEXT("NEW NAME,RED,ASH,JACK")) M4_FAIL(TEXT("Red's names: %s"), *Items);
        Press(Test, EPYRKey::Confirm);
        if (TopId(Test) != TEXT("naming")) M4_FAIL(TEXT("NEW NAME opened '%s'"), *TopId(Test).ToString());
        // Z (row 3, column 8), E (row 1, column 5), D (row 1, column 4), then ED.
        PressN(Test, EPYRKey::Down, 2);
        PressN(Test, EPYRKey::Right, 7);
        Press(Test, EPYRKey::Confirm);
        PressN(Test, EPYRKey::Up, 2);
        PressN(Test, EPYRKey::Left, 3);
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Left);
        Press(Test, EPYRKey::Confirm);
        if (!Desc(Test, TEXT("naming")).Contains(TEXT("[ZED]"))) M4_FAIL(TEXT("typed '%s'"), *Desc(Test, TEXT("naming")));
        // ED is the grid's last cell (row 5, column 9).
        PressN(Test, EPYRKey::Down, 4);
        PressN(Test, EPYRKey::Right, 5);
        if (!Desc(Test, TEXT("naming")).EndsWith(TEXT(" ED"))) M4_FAIL(TEXT("not on ED: %s"), *Desc(Test, TEXT("naming")));
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        // The rival: BLUE from the list.
        const FName Top = SkipText(Test);
        if (Top != TEXT("choice")) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("no rival choice: %s"), *M->ScreenText()); return EGameTest::Running; }
        const FString Items = FString::Join(StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items, TEXT(","));
        if (Items != TEXT("NEW NAME,BLUE,GARY,JOHN")) M4_FAIL(TEXT("rival names: %s"), *Items);
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    SkipText(Test);
    if (!P->Has(TEXT("test_intro_done"))) { if (Test.Seconds - St.T0 > 10) M4_FAIL(TEXT("the intro never ended: %s"), *M->ScreenText()); return EGameTest::Running; }
    if (P->PlayerName != TEXT("ZED") || P->RivalName != TEXT("BLUE")) M4_FAIL(TEXT("names %s / %s"), *P->PlayerName, *P->RivalName);
    if (!SaidContains(Test, TEXT("ZED")) || !SaidContains(Test, TEXT("BLUE"))) M4_FAIL(TEXT("Oak's text does not use the names: %s"), *Said(Test).Right(300));
    P->PlayerName = TEXT("RED");
    P->RivalName = TEXT("GARY");
    M4_PASS(TEXT("NEW NAME typed ZED on the grid, the rival picked BLUE; Oak said both"));
}

// The Centre PC: someone's PC deposits and withdraws Pokemon (never Pikachu), changes among 12 boxes of 20; Red's PC
// stores items; Oak rates the Pokedex; LOG OFF.
GAME_TEST(PC_BoxesAndItems, 120)
{
    static FState St;
    M4_READY();
    UPYRTrainerState* S = State(Test);
    UPYRProgress* P = Progress(Test);
    UPYRMenus* M = Menus(Test);
    Quiet(true, true);
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 9}, {TEXT("pidgey"), 5}});
        AfterPokedex(Test);
        S->Storage.Reset();
        S->CurrentBox = 0;
        S->Bag.Reset();
        S->AddItem(UPYRTrainerState::Potion, 3);
        if (UPYRTrainerState::NumBoxes != 12 || UPYRTrainerState::BoxSize != 20) M4_FAIL(TEXT("%d boxes of %d"), UPYRTrainerState::NumBoxes, UPYRTrainerState::BoxSize);
        // The PC's hidden event in the Viridian Centre, used from the tile below.
        const FYMap* Centre = Data(Test)->Map(TEXT("VIRIDIAN_POKECENTER"));
        const FYHiddenEvent* Pc = Centre->HiddenEvents.FindByPredicate([](const FYHiddenEvent& H) { return H.Kind == TEXT("OpenPokemonCenterPC"); });
        if (!Pc) M4_FAIL(TEXT("no Centre PC in VIRIDIAN_POKECENTER's hidden events"));
        Place(Test, TEXT("VIRIDIAN_POKECENTER"), Pc->X, Pc->Y + 1, EYDir::Up);
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        M4_WAIT(.3);
        Key(Test, EKeys::E);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.2);
        if (SkipText(Test) != TEXT("pc")) M4_FAIL(TEXT("E at the PC opened '%s'"), *M->ScreenText());
        const FString Pc = FString::Join(StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items, TEXT(","));
        St.Note = FString::Printf(TEXT("PC: %s; "), *Pc);
        // SOMEONE's PC, DEPOSIT, Pidgey.
        Press(Test, EPYRKey::Confirm);
        if (SkipText(Test) != TEXT("pc-bill")) M4_FAIL(TEXT("someone's PC opened '%s'"), *M->ScreenText());
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        SkipText(Test);
        if (S->Party.Num() != 1 || S->BoxCount(0) != 1 || S->Storage[0].Species != TEXT("pidgey")) M4_FAIL(TEXT("deposit: party %d, box 1 holds %d"), S->Party.Num(), S->BoxCount(0));
        // Pikachu refuses (and it is the last one anyway).
        S->AddMonValue(UPYRTrainerState::MakeMon(TEXT("rattata"), 5));
        M->TextLog.Reset();
        if (TopId(Test) != TEXT("pc-bill")) M4_FAIL(TEXT("after the deposit: %s"), *M->ScreenText());
        M->Top()->Cursor = 1;
        Press(Test, EPYRKey::Confirm);   // DEPOSIT again
        Press(Test, EPYRKey::Confirm);   // Pikachu
        if (S->Party[0].Species != TEXT("pikachu") || S->Party.Num() != 2) M4_FAIL(TEXT("Pikachu went into the box"));
        const FString Refuse = FirstLineOf(PYRGb::Fill(Test.World, PYRSysText::Get(TEXT("RefusingText"), {{TEXT("wNameBuffer"), TEXT("PIKACHU")}})));
        if (!SaidContains(Test, Refuse)) M4_FAIL(TEXT("depositing Pikachu said '%s', want '%s'"), *Said(Test), *Refuse);
        St.Note += FString::Printf(TEXT("deposited Pidgey into box 1; Pikachu: '%s'; "), *Refuse);
        SkipText(Test);
        // WITHDRAW it again.
        if (TopId(Test) != TEXT("pc-bill")) M4_FAIL(TEXT("after Pikachu refused: %s"), *M->ScreenText());
        M->Top()->Cursor = 0;
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Confirm);
        SkipText(Test);
        if (S->Party.Num() != 3 || S->BoxCount(0) != 0) M4_FAIL(TEXT("withdraw: party %d, box 1 holds %d, storage %d; screens %s; said %s"), S->Party.Num(), S->BoxCount(0), S->Storage.Num(), *M->ScreenText().Replace(TEXT("\n"), TEXT(" ")), *Said(Test).Replace(TEXT("\n"), TEXT(" ")));
        // CHANGE BOX to box 3: twelve boxes listed.
        M->Top()->Cursor = 3;
        Press(Test, EPYRKey::Confirm);
        if (SkipText(Test) != TEXT("pc-boxes")) M4_FAIL(TEXT("CHANGE BOX opened '%s'"), *M->ScreenText());
        if (StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items.Num() != 12) M4_FAIL(TEXT("%d boxes listed"), StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items.Num());
        PressN(Test, EPYRKey::Down, 2);
        Press(Test, EPYRKey::Confirm);
        if (S->CurrentBox != 2) M4_FAIL(TEXT("box %d after CHANGE BOX"), S->CurrentBox + 1);
        St.Note += TEXT("withdrew it; changed to box 3 of 12; ");
        // SEE YA! back to the PC list, then RED's PC: DEPOSIT ITEM, the Potions (all 3... one).
        M->Top()->Cursor = 4;
        Press(Test, EPYRKey::Confirm);
        if (TopId(Test) != TEXT("pc")) M4_FAIL(TEXT("SEE YA! left '%s'"), *TopId(Test).ToString());
        St.I = P->PcCount(UPYRTrainerState::Potion);
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        if (SkipText(Test) != TEXT("pc-mine")) M4_FAIL(TEXT("RED's PC opened '%s'"), *M->ScreenText());
        Press(Test, EPYRKey::Down);
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Confirm);
        Press(Test, EPYRKey::Confirm);
        SkipText(Test);
        if (P->PcCount(UPYRTrainerState::Potion) != St.I + 1 || S->ItemCount(UPYRTrainerState::Potion) != 2) M4_FAIL(TEXT("item deposit: PC %d, bag %d"), P->PcCount(UPYRTrainerState::Potion), S->ItemCount(UPYRTrainerState::Potion));
        St.Note += FString::Printf(TEXT("stored a Potion in RED's PC (%d there); "), P->PcCount(UPYRTrainerState::Potion));
        // LOG OFF, then PROF.OAK's PC: the rating.
        M->Top()->Cursor = 3;
        Press(Test, EPYRKey::Confirm);
        M->Top()->Cursor = 2;
        M->TextLog.Reset();
        Press(Test, EPYRKey::Confirm);
        M4_NEXT();
    }
    Advance(Test, true);
    if (TopId(Test) == TEXT("pc"))
    {
        if (!SaidContains(Test, TEXT("POKéDEX")) && !SaidContains(Test, TEXT("seen"))) M4_FAIL(TEXT("no rating: %s"), *Said(Test));
        const FString Rating = Said(Test).Replace(TEXT("\n"), TEXT(" "));
        Press(Test, EPYRKey::Back);
        if (M->IsOpen(TEXT("pc"))) M4_FAIL(TEXT("LOG OFF did not close the PC"));
        M4_PASS(TEXT("%sOak: '%s'; LOG OFF"), *St.Note, *Rating.Left(120));
    }
    if (Test.Seconds - St.T0 > 15) M4_FAIL(TEXT("Oak's PC never came back: %s"), *M->ScreenText());
    return EGameTest::Running;
}

// An M3 save (version 1: a 3D place in the retired world) is rejected cleanly: no CONTINUE, the file left as it was,
// and SAVE writes an M4 save over it that continues.
GAME_TEST(Save_M3Rejected, 90)
{
    static FState St;
    M4_READY();
    UPYRSave* Save = UPYRSave::Get(Test.World);
    const FString Path = FSaveSlot::PathFor(UPYRSave::SlotName());
    const FString M3 = TEXT("{\"magic\":\"PYR_SAVE\",\"version\":1,\"type\":\"/Script/PokemonYellowReimagine.PYRSaveGame\",")
        TEXT("\"data\":{\"location\":{\"x\":1200,\"y\":-300,\"z\":0},\"yaw\":90,\"place\":\"cerulean-gym\",\"flags\":[\"got-pikachu\"],")
        TEXT("\"trainerFormat\":\"snapshot\",\"trainer\":{},\"money\":3000,\"party\":[\"pikachu\"],\"partyLevels\":[14],\"badges\":1}}");
    Quiet(true, true);
    if (St.Step == 0)
    {
        UPYRSave::bAllowInAutomation = true;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        if (!FFileHelper::SaveStringToFile(M3, *Path)) M4_FAIL(TEXT("could not write the M3 save to %s"), *Path);
        M4_NEXT();
    }
    UPYRSave::bAllowInAutomation = true;
    FPYRSaveGame Loaded;
    const int32 Code = Save->Load(Loaded);
    const bool bOffered = Save->CanContinue();
    FString After;
    FFileHelper::LoadFileToString(After, *Path);
    TArray<FString> Bad;
    if (Code != 3) Bad.Add(FString::Printf(TEXT("load code %d, not 3 (outdated)"), Code));
    if (bOffered) Bad.Add(TEXT("CONTINUE offered"));
    if (Save->LastResult != TEXT("outdated")) Bad.Add(FString::Printf(TEXT("result '%s'"), *Save->LastResult));
    if (After != M3) Bad.Add(TEXT("the M3 file changed"));
    Place(Test, TEXT("PALLET_TOWN"), 5, 6, EYDir::Down);
    FString Why;
    if (!Save->SaveNow(TEXT("test"), &Why)) Bad.Add(TEXT("SAVE failed: ") + Why);
    else if (!Save->CanContinue()) Bad.Add(TEXT("the new save is not offered: ") + Save->LastResult);
    UPYRSave::bAllowInAutomation = false;
    if (Bad.Num()) M4_FAIL(TEXT("%s"), *FString::Join(Bad, TEXT(", ")));
    M4_PASS(TEXT("an M3 version-1 save loads as outdated (3), CONTINUE is not offered, the file is untouched, and SAVE replaces it with one that continues"));
}
