// The game's in-process test harness: each test is a function run every frame inside the real game, launched from the command line.
//
//   GAME_TEST(Name, TimeoutSeconds) { ...return Running / Passed / Failed; }
//
// defines a test that runs when the game starts with -PYRAutoTest=Name (dots in the requested name match
// underscores in the registered one: -PYRAutoTest=World.Roam.LedgeOneWay runs GAME_TEST(World_Roam_LedgeOneWay)). The body is called every
// frame with the world and the seconds since the test started until it returns Passed or Failed; the
// game then logs GAME_TEST_PASSED:Name or GAME_TEST_FAILED:Name <detail> and exits (0 or 1).
// -PYRShot=<absolute .png> saves a screenshot after -PYRShotDelay seconds (default 2) and exits.
// Every run logs PYR_READY:<project> once the first map is playing.
// -PYRTestSlot=<n>: give every test process that runs at the same time a different slot. Save
// through GameTest::SaveSlot(Name) so parallel tests keep their saves apart (Saved/SaveGames/slot-<n>/).
#pragma once

#include "CoreMinimal.h"

class UWorld;

enum class EGameTest : uint8 { Running, Passed, Failed };

struct FGameTestContext
{
    UWorld* World = nullptr;
    double Seconds = 0;  // Game time since the test started.
    int32 Frame = 0;
    FString Detail;      // Set before returning Failed (or Passed) to explain the result.
};

using FGameTestFunction = EGameTest (*)(FGameTestContext&);

namespace GameTest
{
    POKEMONYELLOWREIMAGINE_API void Register(const TCHAR* Name, FGameTestFunction Function, double TimeoutSeconds);
    // Call once from the game mode's BeginPlay.
    POKEMONYELLOWREIMAGINE_API void Start(UWorld* World);
    // True when this run is an automated test or screenshot (skip menus, saves, intro cameras...).
    POKEMONYELLOWREIMAGINE_API bool Automated();
    // -PYRTestSlot=<n>, or 0 when it isn't set.
    POKEMONYELLOWREIMAGINE_API int32 Slot();
    // Slot name for UGameplayStatics::SaveGameToSlot / LoadGameFromSlot / DoesSaveGameExist: "slot-<n>/Name"
    // under a test slot (the file is Saved/SaveGames/slot-<n>/Name.sav), otherwise Name.
    POKEMONYELLOWREIMAGINE_API FString SaveSlot(const FString& Name);
    // The screenshot waits at least this many more seconds (a staged scene still settling), whatever the delay asked.
    POKEMONYELLOWREIMAGINE_API void HoldShot(double MinSeconds);
    // The shot this run takes (its file name without the extension), empty when none.
    POKEMONYELLOWREIMAGINE_API FString ShotName();
}

#define GAME_TEST(Name, TimeoutSeconds) \
    static EGameTest GameTest_##Name(FGameTestContext& Test); \
    static struct FGameTestRegister_##Name \
    { \
        FGameTestRegister_##Name() { GameTest::Register(TEXT(#Name), &GameTest_##Name, TimeoutSeconds); } \
    } GGameTestRegister_##Name; \
    static EGameTest GameTest_##Name(FGameTestContext& Test)
