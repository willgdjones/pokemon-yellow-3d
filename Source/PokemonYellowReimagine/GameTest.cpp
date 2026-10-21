#include "GameTest.h"

#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameTest, Log, All);


namespace
{
struct FEntry { FGameTestFunction Function; double Timeout; };

TMap<FString, FEntry>& Registry()
{
    static TMap<FString, FEntry> Tests;
    return Tests;
}

struct FRun
{
    TWeakObjectPtr<UWorld> World;
    FString Name;
    FEntry Entry{};
    double Started = -1;
    FGameTestContext Context;
    FString ShotPath;
    double ShotAt = 0;
    bool bShotRequested = false;
    bool bDone = false;
    FTSTicker::FDelegateHandle Ticker;
    // The run's clock: game time, carried on across a level change (the test reopens the map) and running on in real
    // time while the game is paused (a pause menu), so a test's Seconds and a shot's delay never stall.
    double Clock = 0;
    double LastWorldTime = 0;
    TWeakObjectPtr<UWorld> ClockWorld;
};

FRun& Run()
{
    static FRun State;
    return State;
}

void Finish(bool bPassed, const FString& Detail)
{
    FRun& R = Run();
    if (R.bDone) return;
    R.bDone = true;
    if (bPassed) { UE_LOG(LogGameTest, Display, TEXT("GAME_TEST_PASSED:%s %s"), *R.Name, *Detail); }
    else { UE_LOG(LogGameTest, Error, TEXT("GAME_TEST_FAILED:%s %s"), *R.Name, *Detail); }
    FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}

bool Tick(float DeltaTime)
{
    FRun& R = Run();
    UWorld* World = R.World.Get();
    if (R.bDone || !World) return !R.bDone;
    const double WorldTime = World->GetTimeSeconds();
    if (R.ClockWorld.Get() != World) R.ClockWorld = World;
    else R.Clock += World->IsPaused() ? double(DeltaTime) : FMath::Max(0.0, WorldTime - R.LastWorldTime);
    R.LastWorldTime = WorldTime;
    const double Now = R.Clock;
    // -PYRVideoFrames=<dir>: every frame saved as <dir>/f00000.png..., the Slate UI included (-dumpmovie never
    // draws the UI). With -benchmark -fps=20 each frame is 1/20 s of game time (e.g. to cut a video with ffmpeg).
    {
        static FString VideoDir;
        static bool bVideoArgs = false;
        static int32 VideoFrame = 0;
        if (!bVideoArgs) { bVideoArgs = true; FParse::Value(FCommandLine::Get(), TEXT("PYRVideoFrames="), VideoDir); }
        if (!VideoDir.IsEmpty() && !FScreenshotRequest::IsScreenshotRequested())
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(VideoDir, FString::Printf(TEXT("f%05d.png"), VideoFrame++)), true, false);
    }
    if (!R.ShotPath.IsEmpty())
    {
        if (!R.bShotRequested && Now >= R.ShotAt)
        {
            R.bShotRequested = true;
            IFileManager::Get().Delete(*R.ShotPath, false, true, true);
            FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([]()
            {
                UE_LOG(LogGameTest, Display, TEXT("PYR_SHOT_SAVED:%s"), *Run().ShotPath);
                Run().bDone = true;
                FPlatformMisc::RequestExitWithStatus(false, 0);
            });
            // This game shows no debug text, so shots keep the Slate UI (HUD, speech bubbles, panels) unless
            // -PYRShotNoUI asks for the world alone.
            FScreenshotRequest::RequestScreenshot(R.ShotPath, !FParse::Param(FCommandLine::Get(), TEXT("PYRShotNoUI")), false);
        }
        return true;
    }
    if (R.Name.IsEmpty()) return false;
    if (R.Started < 0) R.Started = Now;
    R.Context.World = World;
    R.Context.Seconds = Now - R.Started;
    ++R.Context.Frame;
    if (R.Context.Seconds > R.Entry.Timeout) { Finish(false, FString::Printf(TEXT("timeout after %.0f s %s"), R.Entry.Timeout, *R.Context.Detail)); return false; }
    const EGameTest Result = R.Entry.Function(R.Context);
    if (Result != EGameTest::Running) Finish(Result == EGameTest::Passed, R.Context.Detail);
    return !R.bDone;
}
}

namespace GameTest
{
void Register(const TCHAR* Name, FGameTestFunction Function, double TimeoutSeconds)
{
    Registry().Add(Name, FEntry{Function, TimeoutSeconds});
}

bool Automated()
{
    FString Ignored;
    return FParse::Value(FCommandLine::Get(), TEXT("PYRAutoTest="), Ignored)
        || FParse::Value(FCommandLine::Get(), TEXT("PYRShot="), Ignored)
        || FParse::Param(FCommandLine::Get(), TEXT("unattended"));
}

int32 Slot()
{
    int32 Value = 0;
    FParse::Value(FCommandLine::Get(), TEXT("PYRTestSlot="), Value);
    return FMath::Max(Value, 0);
}

FString SaveSlot(const FString& Name)
{
    const int32 Value = Slot();
    return Value > 0 ? FString::Printf(TEXT("slot-%d/%s"), Value, *Name) : Name;
}

void HoldShot(double MinSeconds)
{
    FRun& R = Run();
    if (!R.ShotPath.IsEmpty() && !R.bShotRequested) R.ShotAt = FMath::Max(R.ShotAt, R.Clock + MinSeconds);
}

FString ShotName()
{
    FString Shot;
    if (!FParse::Value(FCommandLine::Get(), TEXT("PYRShot="), Shot)) return FString();
    return FPaths::GetBaseFilename(Shot);
}

void Start(UWorld* World)
{
    FRun& R = Run();
    UE_LOG(LogGameTest, Display, TEXT("PYR_READY:%s"), FApp::GetProjectName());
    if (R.Ticker.IsValid() || R.World.IsValid() || R.bDone) { R.World = World; return; }  // A level change keeps the same run.
    R.World = World;
    FString Name;
    if (FParse::Value(FCommandLine::Get(), TEXT("PYRShot="), R.ShotPath))
    {
        double Delay = 2;
        FParse::Value(FCommandLine::Get(), TEXT("PYRShotDelay="), Delay);
        R.ShotAt = R.Clock + Delay;
    }
    else if (FParse::Value(FCommandLine::Get(), TEXT("PYRAutoTest="), Name))
    {
        R.Name = Name;
        // Test names are often written dotted (World.Roam.LedgeOneWay); GAME_TEST names can't hold dots, so
        // World_Roam_LedgeOneWay answers for it. The pass/fail marker keeps the requested name.
        const FEntry* Found = Registry().Find(Name);
        if (!Found) Found = Registry().Find(Name.Replace(TEXT("."), TEXT("_")));
        if (Found) R.Entry = *Found;
        else
        {
            TArray<FString> Known;
            Registry().GetKeys(Known);
            Finish(false, FString::Printf(TEXT("unknown test (known: %s)"), *FString::Join(Known, TEXT(", "))));
            return;
        }
    }
    else return;
    R.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
}
}
