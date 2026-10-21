// m4q integration checks: what the QA harness's visual tour found after the worker's merge.
// - Look_WaterShowsOnEveryMap: Yellow's water (ponds, the sea, rivers) is drawn a little below the banks; nothing flat
//   may lie over it. The scenery canvas's far meadow once sat above it and hid every pond in Kanto (visual-035, -069,
//   -072, warp-002).
// - Stream_KeyboardTitleToWalkAndSave: the stream check's keyboard player (stream-002/003) without the stream: the
//   title, NEW GAME, Oak's talk by E, a typed name (A S H Enter), GARY from the list, then D held to walk and Enter
//   for START, every key through the player controller as a keyboard sends it.
// - Talk_PersonOnFacedTileBeforePikachu: E talks to whoever stands on the tile Red faces, even with Pikachu just ahead
//   to one side (Pikachu once took the talk: Text_EveryLineMatchesYellow's Cerulean Mart customer, the talk tour's
//   text-026/027).
#include "Tests/QAKit.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/YellowWorld.h"
#include "Partner/PYRPikachuFollower.h"
#include "Kanto/PYRProgress.h"
#include "Menus/PYRMenus.h"
#include "PokemonYellowReimagineCharacter.h"
#include "PokemonYellowReimagineGameMode.h"

using namespace M4;

GAME_TEST(Look_WaterShowsOnEveryMap, 120)
{
    M4_READY();
    const FYellowDataSet* D = Data(Test);
    if (!D) M4_FAIL(TEXT("no Kanto"));
    int32 Water = 0;
    FString Note;
    for (const FName& Id : D->MapOrder)
    {
        const FYMap* M = D->Map(Id);
        if (!M || !M->bOutdoor) continue;
        FString Why;
        int32 N = 0;
        if (!QA::WaterShows(Test, Id, Why, &N)) M4_FAIL(TEXT("%s"), *Why);
        if (N) Note += FString::Printf(TEXT("%s %d, "), *Id.ToString(), N);
        Water += N;
    }
    if (Water == 0) M4_FAIL(TEXT("no water tiles found on any outdoor map"));
    M4_PASS(TEXT("%d water tiles, each with its surface showing (%s)"), Water, *Note.LeftChop(2));
}

namespace
{
void KeyEvent(FGameTestContext& Test, const FKey& K, EInputEvent E)
{
    if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, E, 1.f));
}
void Tap(FGameTestContext& Test, const FKey& K) { KeyEvent(Test, K, IE_Pressed); KeyEvent(Test, K, IE_Released); }
}

GAME_TEST(Stream_KeyboardTitleToWalkAndSave, 150)
{
    static FState St;
    M4_READY();
    UPYRMenus* M = Menus(Test);
    UPYRField* F = Field(Test);
    if (Test.Seconds - St.T0 > 60 && St.Step > 0) M4_FAIL(TEXT("stuck at step %d: %s"), St.Step, *M->ScreenText().Left(160));
    switch (St.Step)
    {
    case 0:
        Quiet(true, true);
        if (APokemonYellowReimagineGameMode* GM = Test.World->GetAuthGameMode<APokemonYellowReimagineGameMode>()) GM->StartTitle();
        M4_NEXT();
    case 1: M4_WAIT(.5); Tap(Test, EKeys::Enter); M4_NEXT();   // NEW GAME
    case 2:   // Oak's talk by E, up to the player's name list
        if (TopId(Test) == TEXT("choice")) M4_NEXT();
        if (Test.Seconds - St.A > .3) { Tap(Test, EKeys::E); St.A = Test.Seconds; }
        return EGameTest::Running;
    case 3: M4_WAIT(.3); Tap(Test, EKeys::E); M4_NEXT();   // NEW NAME
    case 4:
        M4_WAIT(.5);
        if (TopId(Test) != TEXT("naming")) M4_FAIL(TEXT("NEW NAME opened %s"), *TopId(Test).ToString());
        for (const FKey& K : {EKeys::A, EKeys::S, EKeys::H, EKeys::Enter}) Tap(Test, K);
        M4_NEXT();
    case 5:   // through Oak's words to the rival's list
        if (TopId(Test) == TEXT("choice")) M4_NEXT();
        if (Test.Seconds - St.A > .3) { Tap(Test, EKeys::E); St.A = Test.Seconds; }
        return EGameTest::Running;
    case 6: M4_WAIT(.3); Tap(Test, EKeys::S); Tap(Test, EKeys::S); Tap(Test, EKeys::E); M4_NEXT();   // GARY
    case 7:   // Oak's last words, until Red is in his room with nothing open
        if (!M->Top().IsValid() && Test.Seconds - St.T0 > 1.f && !F->IsLocked()) { St.P = F->CurrentTile; M4_NEXT(); }
        if (M->Top().IsValid() && Test.Seconds - St.A > .3) { Tap(Test, EKeys::E); St.A = Test.Seconds; }
        return EGameTest::Running;
    case 8: M4_WAIT(.3); KeyEvent(Test, EKeys::D, IE_Pressed); M4_NEXT();   // walk: D held
    case 9: M4_WAIT(.8); KeyEvent(Test, EKeys::D, IE_Released); M4_NEXT();
    case 10: M4_WAIT(.6); Tap(Test, EKeys::Enter); M4_NEXT();   // START
    default:
    {
        M4_WAIT(.4);
        const FString Name = Progress(Test)->PlayerName, Rival = Progress(Test)->RivalName;
        if (Name != TEXT("ASH") || Rival != TEXT("GARY")) M4_FAIL(TEXT("names %s and %s, not ASH and GARY"), *Name, *Rival);
        if (F->CurrentTile == St.P) M4_FAIL(TEXT("D held for 0.8 s did not move Red off %s %d,%d (locks: %s)"), *F->CurrentMap.ToString(), St.P.X, St.P.Y, *F->LockText());
        if (!M->IsOpen(TEXT("start"))) M4_FAIL(TEXT("Enter in the field opened no START menu (%s; locks: %s)"), *M->ScreenText().Left(80), *F->LockText());
        M4_PASS(TEXT("title, NEW GAME, ASH typed, GARY; D walked Red from %d,%d to %d,%d and Enter opened START"), St.P.X, St.P.Y, F->CurrentTile.X, F->CurrentTile.Y);
    }
    }
}

GAME_TEST(Talk_PersonOnFacedTileBeforePikachu, 90)
{
    static FState St;
    M4_READY();
    UPYRMenus* M = Menus(Test);
    UPYRField* F = Field(Test);
    if (St.Step == 0)
    {
        Quiet(true, true);
        Party(Test, {{TEXT("pikachu"), 10}});
        AfterPokedex(Test);
        Place(Test, TEXT("REDS_HOUSE_1F"), 3, 3, EYDir::Down);
        M4_NEXT();
    }
    APYRPerson* Mom = nullptr;
    for (const TWeakObjectPtr<APYRPerson>& P : F->People) if (P.IsValid() && P->Id == TEXT("REDSHOUSE1F_MOM")) Mom = P.Get();
    if (!Mom) M4_FAIL(TEXT("no Mom in REDS_HOUSE_1F"));
    if (St.Step == 1)
    {
        M4_WAIT(.5);
        FIntPoint Spot;
        EYDir Face = EYDir::Up;
        if (!QA::TalkSpot(Test, Mom, Spot, Face)) M4_FAIL(TEXT("nowhere to stand beside Mom"));
        const FYMap* Map = World(Test)->MapAtKanto(Spot);
        Place(Test, Map->Id, Spot.X - Map->Origin.X, Spot.Y - Map->Origin.Y, Face);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        M4_WAIT(.4);
        // Pikachu a step ahead and to one side: inside Red's old "Pikachu in front" reach, beside the person he faces.
        APYRPikachuFollower* Pika = APYRPikachuFollower::Find(Test.World);
        const APokemonYellowReimagineCharacter* R = M4::Red(Test);
        if (!Pika || !R) M4_FAIL(TEXT("no Pikachu or Red"));
        const FVector Fwd = R->GetActorForwardVector().GetSafeNormal2D();
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
        Pika->SetActorHiddenInGame(false);
        Pika->SetActorLocation(R->GetActorLocation() + Fwd * 70.f + Side * 55.f);
        M->TextLog.Reset();
        M->CloseAll();
        PressE(Test);
        M4_NEXT();
    }
    M4_WAIT(.8);
    if (M->IsOpen(TEXT("pikachu-face"))) M4_FAIL(TEXT("E showed Pikachu's face, not Mom's words"));
    if (M->TextLog.Num() == 0) M4_FAIL(TEXT("Mom said nothing to E (Red at %s facing %s, Mom at %s)"), *QA::TileText(Test, F->KantoTile), YDir::Name(F->Facing), *QA::TileText(Test, Mom->Tile));
    M4_PASS(TEXT("Mom answered E with Pikachu beside her: '%s'"), *M->TextLog[0].Left(40).ReplaceCharWithEscapedChar());
}
