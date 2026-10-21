// Shared helpers for the M4 tests (M4WorldTests, M4StoryTests, M4EncounterTests, M4MenuTests): the Yellow world is
// built from YellowData, Red is placed on a map tile, menus are driven a key at a time the way a player would.
// Tests that need YellowData fail with "YellowData missing: <path>" without it .
#pragma once

#include "CoreMinimal.h"
#include "GameTest.h"
#include "Yellow/YellowData.h"

class AYellowWorld;
class APokemonYellowReimagineCharacter;
class APYRPerson;
class UPYRField;
class UPYRMenus;
class UPYRProgress;
class UPYRTrainerState;
enum class EPYRKey : uint8;

#define M4_FAIL(...) do { Test.Detail = FString::Printf(__VA_ARGS__); return EGameTest::Failed; } while (0)
#define M4_PASS(...) do { Test.Detail = FString::Printf(__VA_ARGS__); return EGameTest::Passed; } while (0)
// The world is up and Red stands in it; a data test fails at once without YellowData.
#define M4_READY() do { if (!M4::HasData(Test)) M4_FAIL(TEXT("%s"), *M4::MissingText()); if (!M4::Ready(Test)) return EGameTest::Running; } while (0)
#define M4_WAIT(Delay) if (Test.Seconds - St.T0 < (Delay)) return EGameTest::Running
#define M4_NEXT() do { ++St.Step; St.T0 = Test.Seconds; return EGameTest::Running; } while (0)

namespace M4
{
struct FState
{
    int32 Step = 0;
    double T0 = 0;
    int32 I = 0, N = 0;
    float A = 0, B = 0;
    FVector V = FVector::ZeroVector;
    FIntPoint P = FIntPoint::ZeroValue;
    FString Note;
    TArray<FString> Seen;
};

bool HasData(FGameTestContext& Test);
FString MissingText();
bool Ready(FGameTestContext& Test);
// For tests that don't need YellowData: the game is up and Red exists (on a map, or on the fallback meadow).
bool ReadyAnyData(FGameTestContext& Test);
const FYellowDataSet* Data(FGameTestContext& Test);
AYellowWorld* World(FGameTestContext& Test);
UPYRField* Field(FGameTestContext& Test);
APokemonYellowReimagineCharacter* Red(FGameTestContext& Test);
UPYRMenus* Menus(FGameTestContext& Test);
UPYRTrainerState* State(FGameTestContext& Test);
UPYRProgress* Progress(FGameTestContext& Test);

// A party of (species, level), no items.
void Party(FGameTestContext& Test, std::initializer_list<TPair<const TCHAR*, int32>> Mons);
void Flags(FGameTestContext& Test, std::initializer_list<const TCHAR*> Names);
// The story so far up to the Pokedex (Pikachu, the lab fight, the parcel delivered, the Pokedex).
void AfterPokedex(FGameTestContext& Test);
// Red on a map tile (local), facing Face; the menus closed and the input cleared.
bool Place(FGameTestContext& Test, FName Map, int32 X, int32 Y, EYDir Face);
// Red's tile in Kanto coordinates, and on its map.
FIntPoint KantoTile(FGameTestContext& Test);
// Walk Red with the test stick toward a world point; true once he is within Tolerance (the stick is let go).
bool WalkTo(FGameTestContext& Test, const FVector& Target, float Tolerance = 18.f);
// Walk toward a tile (local to Map).
bool WalkToTile(FGameTestContext& Test, FName Map, int32 X, int32 Y, float Tolerance = 18.f);
void StopWalking(FGameTestContext& Test);
// One key into the menus (false when none is open).
bool Press(FGameTestContext& Test, EPYRKey Key);
// Moves the talking along: text goes on, a YES/NO box gets bYes. Returns true while anything is still open.
bool Advance(FGameTestContext& Test, bool bYes = true);
// Confirm through plain text boxes until something else is on top; returns that screen's id (None: nothing open).
FName SkipText(FGameTestContext& Test);
// Presses a key N times.
void PressN(FGameTestContext& Test, EPYRKey Key, int32 N);
// Everything said so far, joined (the text log).
FString Said(FGameTestContext& Test);
bool SaidContains(FGameTestContext& Test, const FString& Part);
// The first non-empty line of a text (new lines, page breaks and their escaped forms split lines).
FString FirstLineOf(const FString& Text);
// The top screen's id ("" when none).
FName TopId(FGameTestContext& Test);
// A person by map and object id.
APYRPerson* Person(FGameTestContext& Test, const TCHAR* Map, const TCHAR* Id);
// Red placed facing a person from the next tile (or across a counter), ready for E. False when no tile fits.
bool PlaceToTalk(FGameTestContext& Test, APYRPerson* Person);
// E, as the player presses it.
void PressE(FGameTestContext& Test);
// Keeps the road quiet (no wild Pokemon, no trainer sight, no roadblocks) for tests about something else.
void Quiet(bool bNoEncounters, bool bNoBlocks);
}
