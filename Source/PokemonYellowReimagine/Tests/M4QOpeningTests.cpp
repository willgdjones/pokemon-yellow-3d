// m4q: Oak's opening as a 3D scene and naming by keyboard (the QA bugs story:intro-blank and story:naming-keyboard).
#include "Kanto/PYRField.h"
#include "Kanto/PYRProgress.h"
#include "Menus/PYRIntroStage.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "PokemonYellowReimagineCharacter.h"
#include "PokemonYellowReimagineGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Save/PYRSave.h"
#include "Tests/M4TestKit.h"
#include "Tests/QAKit.h"

using namespace M4;

namespace
{
// The first line of a Yellow system text, as the text box logs its pages.
FString First(const TCHAR* Label) { return FirstLineOf(PYRSysText::Get(Label)); }

FString Desc(FGameTestContext& Test, const TCHAR* Id)
{
    TSharedPtr<FPYRScreen> S = Menus(Test)->Find(Id);
    return S ? S->Describe() : FString();
}

// Index of the first logged page (from From on) that starts with Line, or INDEX_NONE.
int32 PageIndex(const TArray<FString>& Log, const FString& Line, int32 From = 0)
{
    for (int32 I = FMath::Max(0, From); I < Log.Num(); ++I)
        if (FirstLineOf(Log[I]) == Line || Log[I].StartsWith(Line)) return I;
    return INDEX_NONE;
}
}

// Yellow's opening in order on the 3D stage: Oak with Pikachu at his feet ("Hello there!", as the designer asked),
// Pikachu still beside him ("This world is
// inhabited..."), Red while his name is asked, the rival while his is, Red again ("Your very own POKéMON legend..."),
// then Red shrinks and the game begins in his room on Yellow's start tile (REDS_HOUSE_2F 3,6).
GAME_TEST(Opening_PlaysYellowIntroInOrder, 150)
{
    static FState St;
    static TArray<FString> Seen;     // "page first line|who was on the stage"
    M4_READY();
    UPYRMenus* M = Menus(Test);
    UPYRProgress* P = Progress(Test);
    UPYRField* F = Field(Test);
    if (St.Step == 0)
    {
        Quiet(true, true);
        if (APokemonYellowReimagineGameMode* GM = Test.World->GetAuthGameMode<APokemonYellowReimagineGameMode>()) GM->StartNewGame();
        M->CloseAll();
        M->TextLog.Reset();
        P->Set(TEXT("test_intro_done"), false);
        PYRScreens::Intro(Test.World, [P]() { P->Set(TEXT("test_intro_done")); });
        if (!AYellowIntroStage::Find(Test.World)) M4_FAIL(TEXT("no 3D stage for the opening"));
        if (AYellowIntroStage::Find(Test.World)->ModelsLoaded() < 4) M4_FAIL(TEXT("only %d of Oak, Pikachu, Red and Gary loaded"), AYellowIntroStage::Find(Test.World)->ModelsLoaded());
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Each new page: who stands on the stage while it is shown (noted .7 s after it appears, once the people
        // have slid in).
        const AYellowIntroStage* Stage = AYellowIntroStage::Find(Test.World);
        if (M->TextLog.Num() > St.N && St.B <= 0) St.B = Test.Seconds;
        if (Stage && M->TextLog.Num() > St.N && St.B > 0 && Test.Seconds - St.B > .7)
        {
            FString Who;
            for (const FName& N : Stage->Shown()) Who += (Who.IsEmpty() ? TEXT("") : TEXT("+")) + N.ToString();
            for (; St.N < M->TextLog.Num(); ++St.N) Seen.Add(FirstLineOf(M->TextLog[St.N]) + TEXT("|") + Who);
            St.B = 0;
        }
        if (P->Has(TEXT("test_intro_done")))
        {
            if (AYellowIntroStage::Find(Test.World)) M4_FAIL(TEXT("the stage is still up after the opening"));
            M4_NEXT();
        }
        if (Test.Seconds - St.T0 > 110) M4_FAIL(TEXT("the opening never ended: %s | %s"), *M->ScreenText(), *FString::Join(Seen, TEXT(" / ")));
        // A player reading on: text goes on after its letters are out; RED and GARY from Yellow's lists.
        const FName Top = TopId(Test);
        if (Top == TEXT("choice"))
        {
            const bool bRival = StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items.Contains(TEXT("GARY"));
            M->Top()->Cursor = bRival ? 2 : 1;
            Press(Test, EPYRKey::Confirm);
            St.T0 = Test.Seconds;
        }
        else if (Top == TEXT("text") && St.N >= M->TextLog.Num() && Test.Seconds - St.A > .3) { Press(Test, EPYRKey::Confirm); St.A = Test.Seconds; }
        return EGameTest::Running;
    }
    M4_WAIT(.6);
    // The order of Yellow's lines and who was on the stage for each.
    struct FWant { FString Line; const TCHAR* Who; };
    const TArray<FWant> Want = {
        {First(TEXT("OakSpeechText1")), TEXT("oak+pikachu")},
        {First(TEXT("OakSpeechText2A")), TEXT("oak+pikachu")},
        {First(TEXT("IntroducePlayerText")), TEXT("red")},
        {FirstLineOf(P->Fill(PYRSysText::Get(TEXT("YourNameIsText")))), TEXT("red")},
        {First(TEXT("IntroduceRivalText")), TEXT("rival")},
        {FirstLineOf(P->Fill(PYRSysText::Get(TEXT("HisNameIsText")))), TEXT("rival")},
        {FirstLineOf(P->Fill(PYRSysText::Get(TEXT("OakSpeechText3")))), TEXT("red")}};
    TArray<FString> Lines, Who;
    for (const FString& S : Seen) { FString L, Wh; S.Split(TEXT("|"), &L, &Wh); Lines.Add(L); Who.Add(Wh); }
    int32 At = 0;
    for (const FWant& W : Want)
    {
        const int32 I = PageIndex(Lines, W.Line, At);
        if (I == INDEX_NONE) M4_FAIL(TEXT("'%s' missing or out of order in: %s"), *W.Line, *FString::Join(Seen, TEXT(" / ")));
        if (Who[I] != W.Who) M4_FAIL(TEXT("'%s' shown with '%s' on the stage, not %s"), *W.Line, *Who[I], W.Who);
        At = I + 1;
    }
    if (F->CurrentMap != TEXT("REDS_HOUSE_2F") || F->CurrentTile != FIntPoint(3, 6))
        M4_FAIL(TEXT("after the opening Red is at %s %d,%d, not REDS_HOUSE_2F 3,6"), *F->CurrentMap.ToString(), F->CurrentTile.X, F->CurrentTile.Y);
    if (APlayerController* PC = Test.World->GetFirstPlayerController())
        if (PC->GetViewTarget() != Red(Test)) M4_FAIL(TEXT("the view did not come back to Red (%s)"), *GetNameSafe(PC->GetViewTarget()));
    M4_PASS(TEXT("%d lines in Yellow's order with the right people on the stage; Red shrank into REDS_HOUSE_2F 3,6"), Want.Num());
}

// Typed names: letters type on the naming screen (Shift for lower case), Backspace deletes, Enter ends it, seven
// letters at most; Oak's lines use both names and the save keeps them. Start skips Oak's talk to the question.
GAME_TEST(Naming_TypedNamesUsedInDialogue, 150)
{
    static FState St;
    M4_READY();
    UPYRMenus* M = Menus(Test);
    UPYRProgress* P = Progress(Test);
    if (St.Step == 0)
    {
        Quiet(true, true);
        M->CloseAll();
        M->TextLog.Reset();
        P->Set(TEXT("test_intro_done"), false);
        PYRScreens::Intro(Test.World, [P]() { P->Set(TEXT("test_intro_done")); });
        M4_NEXT();
    }
    if (St.Step == 1)
    {
        // Start (Esc) during Oak's first words: straight on to "First, what is your name?".
        M4_WAIT(.5);
        if (TopId(Test) != TEXT("text")) M4_FAIL(TEXT("Oak is not talking: %s"), *M->ScreenText());
        M->HandleKey(EKeys::Escape);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        const FName Top = TopId(Test);
        if (Top != TEXT("choice")) { if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("Start did not skip to the name question: %s"), *M->ScreenText()); return EGameTest::Running; }
        if (!M->TextLog.Num() || FirstLineOf(M->TextLog.Last()) != FirstLineOf(PYRSysText::Get(TEXT("IntroducePlayerText"))))
            M4_FAIL(TEXT("the question is not Oak's 'First, what is your name?': %s"), M->TextLog.Num() ? *M->TextLog.Last() : TEXT("(nothing)"));
        Press(Test, EPYRKey::Confirm);   // NEW NAME
        if (TopId(Test) != TEXT("naming")) M4_FAIL(TEXT("NEW NAME opened '%s'"), *TopId(Test).ToString());
        // WASD and E are letters here: "WEDDX", Backspace, then "Z".
        M->Type(TEXT("WEDDX\bZ"));
        if (!Desc(Test, TEXT("naming")).Contains(TEXT("[WEDDZ]"))) M4_FAIL(TEXT("typed WEDDX, Backspace, Z: %s"), *Desc(Test, TEXT("naming")));
        M->Type(TEXT("\n"));
        if (TopId(Test) == TEXT("naming")) M4_FAIL(TEXT("Enter did not end the name: %s"), *Desc(Test, TEXT("naming")));
        if (P->PlayerName != TEXT("WEDDZ")) M4_FAIL(TEXT("the player's name is '%s', not WEDDZ"), *P->PlayerName);
        M4_NEXT();
    }
    if (St.Step == 3)
    {
        const FName Top = TopId(Test);
        if (Top == TEXT("text")) { if (Test.Seconds - St.A > .3) { Press(Test, EPYRKey::Confirm); St.A = Test.Seconds; } return EGameTest::Running; }
        if (Top != TEXT("choice")) { if (Test.Seconds - St.T0 > 20) M4_FAIL(TEXT("no rival question: %s"), *M->ScreenText()); return EGameTest::Running; }
        Press(Test, EPYRKey::Confirm);   // NEW NAME
        if (TopId(Test) != TEXT("naming")) M4_FAIL(TEXT("NEW NAME (rival) opened '%s'"), *TopId(Test).ToString());
        // Eight letters, the last one dropped (seven at most), a lower-case letter with Shift, then Enter.
        M->Type(TEXT("SHIGERUS"));
        if (!Desc(Test, TEXT("naming")).Contains(TEXT("[SHIGERU]"))) M4_FAIL(TEXT("seven letters at most: %s"), *Desc(Test, TEXT("naming")));
        M->Type(TEXT("\b"));
        M->Type(TEXT("u"), true);
        if (!Desc(Test, TEXT("naming")).Contains(TEXT("[SHIGERu]"))) M4_FAIL(TEXT("Shift did not type lower case: %s"), *Desc(Test, TEXT("naming")));
        M->Type(TEXT("\bU\n"));
        if (P->RivalName != TEXT("SHIGERU")) M4_FAIL(TEXT("the rival's name is '%s', not SHIGERU"), *P->RivalName);
        M4_NEXT();
    }
    if (St.Step == 4)
    {
        if (TopId(Test) == TEXT("text") && Test.Seconds - St.A > .3) { Press(Test, EPYRKey::Confirm); St.A = Test.Seconds; }
        if (!P->Has(TEXT("test_intro_done"))) { if (Test.Seconds - St.T0 > 60) M4_FAIL(TEXT("the opening never ended: %s"), *M->ScreenText()); return EGameTest::Running; }
        const FString Said = FString::Join(M->TextLog, TEXT("\n"));
        for (const TCHAR* Part : {TEXT("name is WEDDZ!"), TEXT("name is SHIGERU!"), TEXT("WEDDZ!")})
            if (!Said.Replace(TEXT("\n"), TEXT(" ")).Contains(Part)) M4_FAIL(TEXT("Oak never said '%s': %s"), Part, *Said.Right(400));
        if (Said.Contains(TEXT("WEDDZed")) || Said.Contains(TEXT("SHIGERUed"))) M4_FAIL(TEXT("a name got a suffix: %s"), *Said.Right(300));
        // The save keeps both names.
        UPYRSave::bAllowInAutomation = true;
        UPYRSave* Save = UPYRSave::Get(Test.World);
        FString Why;
        if (!Save || !Save->SaveNow(TEXT("test"), &Why)) M4_FAIL(TEXT("save failed: %s"), *Why);
        FPYRSaveGame Loaded;
        if (Save->Load(Loaded) != 0) M4_FAIL(TEXT("the save did not load back"));
        if (Loaded.Progress.PlayerName != TEXT("WEDDZ") || Loaded.Progress.RivalName != TEXT("SHIGERU"))
            M4_FAIL(TEXT("the save holds %s / %s"), *Loaded.Progress.PlayerName, *Loaded.Progress.RivalName);
        P->PlayerName = TEXT("RED");
        P->RivalName = TEXT("GARY");
        M4_PASS(TEXT("Start skipped to the question; typed WEDDZ (Backspace worked) and SHIGERU (7 letters, Shift for lower case); Oak said both; the save keeps them"));
    }
    return EGameTest::Running;
}
