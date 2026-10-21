// m4u, round 2: the blind judge's findings that were real, each with its regression test. Signs carry their words, the
// text box names who speaks, Cerulean Gym's swimmers are in the water, the move names are big enough to read and the
// battle caption keeps off Pikachu. (The trees, windows, gym walls, the Pewter massif and the Route 4 shelf are looks:
// their proof is the QA tour's and the shots' pictures.)
#include "Battle/BattleTestKit.h"
#include "Battle/PYRBattleHUD.h"
#include "Battle/PYRBattleManager.h"
#include "Battle/PYRMonPawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameTest.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Party/PYRSpecies.h"
#include "Tests/M4TestKit.h"
#include "Tests/QAKit.h"
#include "World/PYRLetters.h"
#include "Yellow/YellowData.h"

using namespace M4;

namespace
{
struct FJudgeState { int32 Step = 0; double T0 = 0; FString Note; };
}

// What each sign's board says: the town or the heading in capitals, in at most two short lines; every signpost in the
// first towns has words on it.
GAME_TEST(M4uSignLabels, 120)
{
    M4_READY();
    struct FCase { const TCHAR* Text; TArray<FString> Want; };
    const FCase Cases[] = {
        {TEXT("PEWTER CITY\nA Stone Gray City"), {TEXT("PEWTER"), TEXT("CITY")}},
        {TEXT("Grass and caves\nhandled easily!\nBIKE SHOP"), {TEXT("BIKE SHOP")}},
        {TEXT("TRAINER TIPS\n\nPress START"), {TEXT("TRAINER"), TEXT("TIPS")}},
        {TEXT("ROUTE 2\nVIRIDIAN CITY -\nPEWTER CITY"), {TEXT("ROUTE 2")}},
        {TEXT("All your item\nneeds fulfilled!\nPOK\x00e9MON MART"), {TEXT("POK\x00e9MON"), TEXT("MART")}},
        {TEXT("Beware! ZUBAT is\na bloodsucker!"), {TEXT("BEWARE!"), TEXT("ZUBAT IS")}},
    };
    FString Note;
    for (const FCase& C : Cases)
    {
        const TArray<FString> Got = PYRLetters::SignLabel(C.Text);
        if (Got != C.Want) M4_FAIL(TEXT("'%s' lettered as '%s', want '%s'"), C.Text, *FString::Join(Got, TEXT("/")), *FString::Join(C.Want, TEXT("/")));
    }
    for (const TCHAR* Ch : {TEXT("A"), TEXT("Z"), TEXT("0"), TEXT("9"), TEXT("\x00e9"), TEXT("!"), TEXT("-")})
        if (!PYRLetters::HasGlyph(Ch[0])) M4_FAIL(TEXT("no glyph for '%s'"), Ch);
    int32 Posts = 0, Lettered = 0;
    const FYellowDataSet* D = Data(Test);
    for (const TWeakObjectPtr<APYRSignpost>& S : Field(Test)->Signs)
    {
        if (!S.IsValid()) continue;
        const FString Map = S->Map.ToString();
        if (!(Map.Contains(TEXT("PALLET")) || Map.Contains(TEXT("VIRIDIAN")) || Map.Contains(TEXT("PEWTER")) || Map.Contains(TEXT("CERULEAN")) || Map.StartsWith(TEXT("ROUTE_"))))
            continue;
        const FString Text = D->TextFor(S->TextId);
        bool bWords = false;
        for (TCHAR C : Text) bWords |= FChar::IsAlpha(C);
        if (!bWords || S->bOnWall) continue;
        ++Posts;
        if (S->Label.Num() == 0) M4_FAIL(TEXT("the signpost %s at %s (%d,%d) is blank (its text: %s)"), *S->TextId, *Map, S->Tile.X, S->Tile.Y, *Text.Left(60));
        ++Lettered;
        if (S->TextId.Contains(TEXT("PewterCitySignText")) && S->Label != TArray<FString>{TEXT("PEWTER"), TEXT("CITY")})
            M4_FAIL(TEXT("Pewter's sign reads %s"), *FString::Join(S->Label, TEXT("/")));
    }
    if (Posts < 10) M4_FAIL(TEXT("only %d signposts with words found in the first towns and routes"), Posts);
    M4_PASS(TEXT("%d unit labels right; %d of %d signposts in the first towns and routes lettered"), UE_ARRAY_COUNT(Cases), Lettered, Posts);
}

// The text box names the speaker over his own lines: the nurse by talking to her, Misty by hers (her Game Boy sprite is a
// brunette girl's), nobody over a sign.
GAME_TEST(M4uSpeakerNames, 180)
{
    static FJudgeState St;
    M4_READY();
    Quiet(true, true);
    UPYRMenus* Me = Menus(Test);
    // First a sign (nobody named), then each talk in four steps: stand, press E, check the name, play the scene out (a
    // script holds the field till then). Misty's talk starts her fight, so she comes last.
    struct FWho { const TCHAR* Map; const TCHAR* Sprite; const TCHAR* Want; };
    static const FWho Who[] = {{TEXT("VIRIDIAN_POKECENTER"), TEXT("NURSE"), TEXT("NURSE")}, {TEXT("CERULEAN_GYM"), TEXT("BRUNETTE_GIRL"), TEXT("MISTY")}};
    if (St.Step == 0)
    {
        Party(Test, {{TEXT("pikachu"), 20}});
        AfterPokedex(Test);
        for (const TWeakObjectPtr<APYRSignpost>& S : Field(Test)->Signs)
            if (S.IsValid() && !S->bOnWall && S->Map == TEXT("VIRIDIAN_CITY") && S->Label == TArray<FString>{TEXT("VIRIDIAN"), TEXT("CITY")})
            {
                const FYMap* M = Data(Test)->Map(S->Map);
                if (!M || !Place(Test, S->Map, S->Tile.X - M->Origin.X, S->Tile.Y - M->Origin.Y + 1, EYDir::Up)) M4_FAIL(TEXT("can't stand south of Viridian's sign"));
                M4_NEXT();
            }
        M4_FAIL(TEXT("no Viridian City sign"));
    }
    if (St.Step == 1)
    {
        M4_WAIT(.5);
        PressE(Test);
        M4_NEXT();
    }
    if (St.Step == 2)
    {
        if (TopId(Test) != TEXT("text")) { if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("reading Viridian's sign showed no text: %s"), *Me->ScreenText()); return EGameTest::Running; }
        if (!Me->ShownSpeaker().IsEmpty()) M4_FAIL(TEXT("the sign's text is named '%s'"), *Me->ShownSpeaker());
        St.Note = FString::Printf(TEXT("sign unnamed (%s); "), *Me->ScreenText().Left(40).Replace(TEXT("\n"), TEXT(" ")));
        M4_NEXT();
    }
    const int32 Talk = (St.Step - 3) / 4, Phase = (St.Step - 3) % 4;
    if (Talk >= UE_ARRAY_COUNT(Who)) M4_PASS(TEXT("%s"), *St.Note);
    const FWho& W = Who[Talk];
    if (Phase == 0)
    {
        if (!QA::Idle(Test)) { Advance(Test); if (Test.Seconds - St.T0 > 40) M4_FAIL(TEXT("never idle before the %s: %s"), W.Sprite, *Me->ScreenText()); return EGameTest::Running; }
        APYRPerson* P = Field(Test)->FindPersonBySprite(W.Map, W.Sprite);
        if (!P || !PlaceToTalk(Test, P)) M4_FAIL(TEXT("no %s in %s to talk to"), W.Sprite, W.Map);
        M4_NEXT();
    }
    if (Phase == 1)
    {
        M4_WAIT(.3);
        PressE(Test);
        M4_NEXT();
    }
    if (Phase == 2)
    {
        if (TopId(Test) != TEXT("text")) { if (Test.Seconds - St.T0 > 8) M4_FAIL(TEXT("no text after talking to the %s: %s"), W.Sprite, *Me->ScreenText()); return EGameTest::Running; }
        const FString Got = Me->ShownSpeaker();
        if (Got != W.Want) M4_FAIL(TEXT("the %s's line is named '%s', want %s: %s"), W.Sprite, *Got, W.Want, *Me->ScreenText().Left(120));
        St.Note += FString::Printf(TEXT("%s named %s; "), W.Sprite, *Got);
        M4_NEXT();
    }
    M4_NEXT();
}

// Cerulean Gym's swimmers are in the pool to the waist (their feet a metre under its floor), everyone else on the floor.
GAME_TEST(M4uSwimmersInWater, 90)
{
    M4_READY();
    AYellowWorld* W = World(Test);
    int32 InWater = 0, OnFloor = 0;
    for (const TWeakObjectPtr<APYRPerson>& P : Field(Test)->People)
    {
        if (!P.IsValid() || P->Map != TEXT("CERULEAN_GYM") || !P->Visual) continue;
        const bool bWater = W->LookAt(P->Tile) == TEXT('W');
        const float Z = P->Visual->GetRelativeLocation().Z;
        if (bWater && !FMath::IsNearlyEqual(Z, -185.f, 1.f)) M4_FAIL(TEXT("%s on a water tile stands at %.0f (want -185, in the water)"), *P->Id, Z);
        if (!bWater && !FMath::IsNearlyEqual(Z, -85.f, 1.f)) M4_FAIL(TEXT("%s on the floor stands at %.0f (want -85)"), *P->Id, Z);
        (bWater ? InWater : OnFloor) += 1;
    }
    if (InWater == 0) M4_FAIL(TEXT("no one in Cerulean Gym's water (%d on the floor)"), OnFloor);
    M4_PASS(TEXT("%d swimmers in the water, %d people on the floor"), InWater, OnFloor);
}

// Every move name fits its icon at 10 pt or more (it was 9 pt, 6 px on a 720p screen); Pikachu's moves at 11 or more.
GAME_TEST(M4uMoveNamesReadable, 60)
{
    const float Width = FPYRHudLayout::Icon - 8;
    FString Note;
    for (const TCHAR* Name : {TEXT("THUNDERSHOCK"), TEXT("THUNDER WAVE"), TEXT("QUICK ATTACK"), TEXT("TAIL WHIP"), TEXT("GROWL"), TEXT("THUNDERBOLT"),
        TEXT("DOUBLE TEAM"), TEXT("SAND-ATTACK"), TEXT("SELFDESTRUCT")})
    {
        FString L1, L2;
        int32 Font = 0;
        PYRDraw::MoveLabel(Name, Width, L1, L2, Font);
        const float W1 = PYRDraw::Measure(L1, Font).X, W2 = PYRDraw::Measure(L2, Font).X;
        if ((L1 + L2).Replace(TEXT(" "), TEXT("")) != FString(Name).Replace(TEXT(" "), TEXT(""))) M4_FAIL(TEXT("%s split as '%s' / '%s'"), Name, *L1, *L2);
        if (Font < 10) M4_FAIL(TEXT("%s at %d pt"), Name, Font);
        const bool bStarter = FString(Name) == TEXT("THUNDERSHOCK") || FString(Name) == TEXT("GROWL") || FString(Name) == TEXT("TAIL WHIP") || FString(Name) == TEXT("THUNDER WAVE");
        if (bStarter && Font < 11) M4_FAIL(TEXT("Pikachu's %s at only %d pt"), Name, Font);
        if (Font > 10 && (W1 > Width + .5f || W2 > Width + .5f)) M4_FAIL(TEXT("%s at %d pt is %.0f wide in a %.0f icon"), Name, Font, FMath::Max(W1, W2), Width);
        Note += FString::Printf(TEXT("%s %d pt%s; "), Name, Font, L2.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s / %s)"), *L1, *L2));
    }
    M4_PASS(TEXT("%s"), *Note);
}

// A caption doesn't cover Pikachu (Battle/PYRBattleHUD's CaptionTopClear, which the HUD paints with): the Desktop band
// leaves 62% down when Pikachu stands there, and against Onix, whose coil fills the ring's far half, takes the gap
// between the two; with nobody under it, it stays where it always was. Boxes in the HUD's 1920 x 1080 space, from the
// m4u Pewter Gym shot (Onix's coil, Pikachu on the near platform, Red behind).
GAME_TEST(M4uCaptionClearOfPokemon, 60)
{
    FPYRHudLayout L;
    L.Size = FVector2f(1920, 1080);
    L.CaptionCentreX = 960;
    L.CaptionTop = 1080 * .62f;
    const FVector2f Text(560, 48);
    const FBox2f Onix(FVector2f(740, 80), FVector2f(1170, 535)), Pikachu(FVector2f(900, 600), FVector2f(1010, 700)), Red(FVector2f(930, 770), FVector2f(1000, 880));
    const float Alone = PYRDraw::CaptionTopClear(L, Text, {});
    if (Alone != L.CaptionTop) M4_FAIL(TEXT("with nobody under it the caption moved to %.0f"), Alone);
    const float WithOnix = PYRDraw::CaptionTopClear(L, Text, {{Onix, 3.f}, {Pikachu, 3.f}, {Red, 1.f}});
    const FBox2f Band = PYRDraw::CaptionBand(L, Text, WithOnix);
    if (Band.Intersect(Pikachu)) M4_FAIL(TEXT("the caption at %.0f covers Pikachu"), WithOnix);
    if (Band.Intersect(Onix) && Band.Overlap(Onix).GetArea() > 4000.f) M4_FAIL(TEXT("the caption at %.0f covers Onix (%.0f px2)"), WithOnix, Band.Overlap(Onix).GetArea());
    const FBox2f Low(FVector2f(880, 300), FVector2f(1040, 420));
    const float WithPidgey = PYRDraw::CaptionTopClear(L, Text, {{Low, 3.f}, {FBox2f(FVector2f(880, 420), FVector2f(1040, 520)), 3.f}});
    if (WithPidgey != L.CaptionTop) M4_FAIL(TEXT("a clear default (Pokemon higher up) still moved the caption to %.0f"), WithPidgey);
    M4_PASS(TEXT("alone %.0f; Onix, Pikachu and Red: %.0f (band %.0f-%.0f, Pikachu at %.0f-%.0f); Pokemon up the ring: %.0f"), Alone, WithOnix, Band.Min.Y,
        Band.Max.Y, Pikachu.Min.Y, Pikachu.Max.Y, WithPidgey);
}
