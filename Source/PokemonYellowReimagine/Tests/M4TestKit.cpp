#include "Tests/M4TestKit.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kanto/PYRField.h"
#include "Kanto/PYRPeople.h"
#include "Kanto/PYRProgress.h"
#include "Kanto/PYRScripts.h"
#include "Kanto/YellowWorld.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRMoveLearning.h"
#include "Party/PYRSpecies.h"
#include "Party/PYRTrainerState.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Stage/PYREncounters.h"
#include "World/CoreSeams.h"

namespace M4
{
bool HasData(FGameTestContext& Test)
{
    const FYellowDataSet* D = UYellowData::Data(Test.World);
    return D && D->IsLoaded();
}

FString MissingText()
{
    return FString::Printf(TEXT("YellowData missing: %s (this test needs the yellowdata worker's output)"), *UYellowData::DefaultRoot());
}

bool Ready(FGameTestContext& Test)
{
    const AYellowWorld* W = World(Test);
    const UPYRField* F = Field(Test);
    return Test.Seconds > .6 && W && W->IsBuilt() && F && Red(Test) && !F->CurrentMap.IsNone();
}

bool ReadyAnyData(FGameTestContext& Test)
{
    return HasData(Test) ? Ready(Test) : Test.Seconds > .6 && Red(Test) != nullptr;
}

const FYellowDataSet* Data(FGameTestContext& Test) { return UYellowData::Data(Test.World); }
AYellowWorld* World(FGameTestContext& Test) { return AYellowWorld::Get(Test.World); }
UPYRField* Field(FGameTestContext& Test) { return UPYRField::Get(Test.World); }
APokemonYellowReimagineCharacter* Red(FGameTestContext& Test) { return APokemonYellowReimagineCharacter::Find(Test.World); }
UPYRMenus* Menus(FGameTestContext& Test) { return UPYRMenus::Get(Test.World); }
UPYRTrainerState* State(FGameTestContext& Test) { return UPYRTrainerState::Get(Test.World); }
UPYRProgress* Progress(FGameTestContext& Test) { return UPYRProgress::Get(Test.World); }

void Party(FGameTestContext& Test, std::initializer_list<TPair<const TCHAR*, int32>> Mons)
{
    TArray<FPYRMonSpec> Specs;
    for (const TPair<const TCHAR*, int32>& M : Mons) { FPYRMonSpec S; S.Species = M.Key; S.Level = M.Value; Specs.Add(S); }
    if (UPYRTrainerState* S = State(Test)) S->DebugSetParty(Specs);
}

void Flags(FGameTestContext& Test, std::initializer_list<const TCHAR*> Names)
{
    if (UPYRProgress* P = Progress(Test)) for (const TCHAR* N : Names) P->Set(FName(N));
    PYRScripts::Refresh(Test.World);
}

void AfterPokedex(FGameTestContext& Test)
{
    if (UPYRTrainerState* S = State(Test)) if (S->Party.Num() == 0) Party(Test, {{TEXT("pikachu"), 6}});
    Flags(Test, {TEXT("EVENT_FOLLOWED_OAK_INTO_LAB"), TEXT("EVENT_OAK_ASKED_TO_CHOOSE_MON"), TEXT("EVENT_GOT_STARTER"), TEXT("EVENT_BATTLED_RIVAL_IN_OAKS_LAB"),
        TEXT("EVENT_GOT_OAKS_PARCEL"), TEXT("EVENT_OAK_GOT_PARCEL"), TEXT("EVENT_GOT_POKEDEX")});
}

bool Place(FGameTestContext& Test, FName Map, int32 X, int32 Y, EYDir Face)
{
    if (UPYRMenus* M = Menus(Test)) M->CloseAll();
    StopWalking(Test);
    if (APokemonYellowReimagineCharacter* R = Red(Test)) R->StopScriptWalk();
    UPYRField* F = Field(Test);
    return F && F->PlaceRed(Map, X, Y, Face);
}

FIntPoint KantoTile(FGameTestContext& Test)
{
    const AYellowWorld* W = World(Test);
    const APokemonYellowReimagineCharacter* R = Red(Test);
    return W && R ? W->ToKanto(R->GetActorLocation()) : FIntPoint::ZeroValue;
}

bool WalkTo(FGameTestContext& Test, const FVector& Target, float Tolerance)
{
    APokemonYellowReimagineCharacter* R = Red(Test);
    if (!R) return false;
    const FVector To = (Target - R->GetActorLocation()) * FVector(1, 1, 0);
    if (To.Size() <= Tolerance) { R->TestInput = FVector::ZeroVector; return true; }
    R->TestInput = To.GetSafeNormal() * FMath::Clamp(To.Size() / 60.f, .3f, 1.f);
    return false;
}

bool WalkToTile(FGameTestContext& Test, FName Map, int32 X, int32 Y, float Tolerance)
{
    const AYellowWorld* W = World(Test);
    return W && WalkTo(Test, W->TileCenter(Map, X, Y), Tolerance);
}

void StopWalking(FGameTestContext& Test)
{
    // The test's stick only: a script's own walk goes on.
    if (APokemonYellowReimagineCharacter* R = Red(Test)) R->TestInput = FVector::ZeroVector;
}

bool Press(FGameTestContext& Test, EPYRKey Key)
{
    UPYRMenus* M = Menus(Test);
    return M && M->Press(Key);
}

bool Advance(FGameTestContext& Test, bool bYes)
{
    UPYRMenus* M = Menus(Test);
    if (!M || !M->AnyOpen()) return false;
    const FName Top = M->Top()->Id;
    if (Top == TEXT("choice"))
    {
        // The YES/NO box starts on YES.
        const TArray<FString>& Items = StaticCastSharedPtr<FPYRListScreen>(M->Top())->Items;
        if (!bYes && Items.Num() == 2 && Items[1] == TEXT("NO") && M->Top()->Cursor == 0) M->Press(EPYRKey::Down);
        M->Press(EPYRKey::Confirm);
    }
    else if (Top == TEXT("learn"))
    {
        // m4t: the learn-move prompt. YES all the way forgets the first move (YES, the first row, YES to "Forget X?");
        // NO keeps the old four.
        if (!bYes) { PYRMoveLearning::StepKeep(Test.World); return true; }
        PYRMoveLearning::FView V;
        if (!PYRMoveLearning::View(Test.World, V) || !V.bTakesInput) return true;
        if (V.Stage != PYRMoveLearning::EStage::List && V.Cursor != 0) M->Press(EPYRKey::Up);
        else M->Press(EPYRKey::Confirm);
    }
    else if (Top == TEXT("naming"))
    {
        // Straight to ED: the default name.
        for (int32 I = 0; I < 6 && TopId(Test) == TEXT("naming"); ++I) M->Press(EPYRKey::Up);
        M->Press(EPYRKey::Confirm);
    }
    else M->Press(EPYRKey::Confirm);
    return M->AnyOpen();
}

FName SkipText(FGameTestContext& Test)
{
    UPYRMenus* M = Menus(Test);
    for (int32 I = 0; I < 40 && M && TopId(Test) == TEXT("text"); ++I) M->Press(EPYRKey::Confirm);
    return TopId(Test);
}

void PressN(FGameTestContext& Test, EPYRKey Key, int32 N)
{
    for (int32 I = 0; I < N; ++I) Press(Test, Key);
}

FString Said(FGameTestContext& Test)
{
    const UPYRMenus* M = Menus(Test);
    return M ? FString::Join(M->TextLog, TEXT(" | ")) : FString();
}

bool SaidContains(FGameTestContext& Test, const FString& Part)
{
    const FString All = Said(Test).Replace(TEXT("\n"), TEXT(" "));
    return All.Contains(Part.Replace(TEXT("\n"), TEXT(" ")));
}

FString FirstLineOf(const FString& Text)
{
    const FString T = Text.Replace(TEXT("\\n"), TEXT("\n")).Replace(TEXT("\\f"), TEXT("\n")).Replace(TEXT("\f"), TEXT("\n"));
    TArray<FString> Lines;
    T.ParseIntoArray(Lines, TEXT("\n"), true);
    for (const FString& L : Lines) if (!L.TrimStartAndEnd().IsEmpty()) return L.TrimStartAndEnd();
    return FString();
}

FName TopId(FGameTestContext& Test)
{
    const UPYRMenus* M = Menus(Test);
    return M && M->Top().IsValid() ? M->Top()->Id : NAME_None;
}

APYRPerson* Person(FGameTestContext& Test, const TCHAR* Map, const TCHAR* Id)
{
    const UPYRField* F = Field(Test);
    return F ? F->FindPerson(FName(Map), Id) : nullptr;
}

bool PlaceToTalk(FGameTestContext& Test, APYRPerson* P)
{
    const AYellowWorld* W = World(Test);
    const FYellowDataSet* D = Data(Test);
    const FYMap* M = P && D ? D->Map(P->Map) : nullptr;
    if (!W || !M) return false;
    auto Open = [W, M](FIntPoint K)
    {
        const FIntPoint L = K - M->Origin;
        return M->Contains(L.X, L.Y) && !YTile::IsSolid(M->Tile(L.X, L.Y)) && M->WarpAt(L.X, L.Y) == INDEX_NONE;
    };
    // Across a counter first (a nurse or clerk), then next to them.
    for (int32 Reach = 2; Reach >= 1; --Reach)
        for (const EYDir Dir : {EYDir::Up, EYDir::Down, EYDir::Left, EYDir::Right})
        {
            const FIntPoint From = P->Tile - YDir::Delta(Dir) * Reach;
            if (!Open(From)) continue;
            if (Reach == 2 && W->TileAt(P->Tile - YDir::Delta(Dir)) != TEXT('=')) continue;
            if (Reach == 1 && W->IsEdgeBlocked(From, Dir)) continue;
            const FIntPoint L = From - M->Origin;
            return Place(Test, M->Id, L.X, L.Y, Dir);
        }
    return false;
}

void PressE(FGameTestContext& Test)
{
    if (APlayerController* PC = Test.World->GetFirstPlayerController()) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.f));
}

void Quiet(bool bNoEncounters, bool bNoBlocks)
{
    PYRTestFlags::bNoEncounters = bNoEncounters;
    PYRTestFlags::bNoBlocks = bNoBlocks;
}
}
