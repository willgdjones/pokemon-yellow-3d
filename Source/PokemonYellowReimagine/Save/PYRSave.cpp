#include "Save/PYRSave.h"

#include "Engine/World.h"
#include "GameTest.h"
#include "Kanto/PYRField.h"
#include "Kanto/YellowWorld.h"
#include "Misc/DateTime.h"
#include "Save/SaveSlot.h"

DEFINE_LOG_CATEGORY_STATIC(LogPYRSave, Log, All);

bool UPYRSave::bAllowInAutomation = false;

UPYRSave* UPYRSave::Get(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UPYRSave>() : nullptr;
}

bool UPYRSave::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UPYRSave::CanUseDisk() const { return !GameTest::Automated() || bAllowInAutomation; }

bool UPYRSave::Exists() const { return CanUseDisk() && FSaveSlot::Exists(SlotName()); }

bool UPYRSave::CanContinue()
{
    if (!Exists()) return false;
    FPYRSaveGame S;
    const int32 Code = Load(S);
    const UPYRField* Field = UPYRField::Get(this);
    const FYMap* M = Code == 0 && Field && Field->GetYellowWorld() && Field->GetYellowWorld()->GetData() ? Field->GetYellowWorld()->GetData()->Map(S.Map) : nullptr;
    if (M && M->Contains(S.TileX, S.TileY)) return true;
    LastResult = Code == 3 ? TEXT("outdated") : Code == 0 ? TEXT("map not in this build") : TEXT("invalid");
    UE_LOG(LogPYRSave, Display, TEXT("PYR_SAVE_NOT_CONTINUED %s: %s (a new game starts; SAVE replaces it)"), *FSaveSlot::PathFor(SlotName()), *LastResult);
    return false;
}

FPYRSaveGame UPYRSave::Capture() const
{
    FPYRSaveGame S;
    if (const UPYRField* Field = UPYRField::Get(this))
    {
        S.Map = Field->CurrentMap;
        S.TileX = Field->CurrentTile.X;
        S.TileY = Field->CurrentTile.Y;
        S.Facing = YDir::Name(Field->Facing);
        S.LastOutdoor = Field->LastOutdoor;
    }
    if (const UPYRProgress* P = UPYRProgress::Get(this))
    {
        S.Progress = P->MakeSnapshot();
        S.PlayerName = P->PlayerName;
        S.PlaySeconds = P->PlaySeconds;
    }
    if (const UPYRTrainerState* T = UPYRTrainerState::Get(this))
    {
        S.Trainer = T->MakeSnapshot();
        S.Badges = T->BadgeCount();
        S.DexOwned = T->Caught.Num();
    }
    S.SavedAt = FDateTime::UtcNow().ToIso8601();
    return S;
}

FString UPYRSave::Fingerprint(const FPYRSaveGame& S) const
{
    // What counts as a change: place, tile, money, party, bag, flags, beaten trainers and items taken.
    FString Out = FString::Printf(TEXT("%s|%d|%d|%d|%d|%d|%d|%d|%d"), *S.Map.ToString(), S.TileX, S.TileY, S.Trainer.Money, S.Trainer.Party.Num(),
        S.Trainer.Bag.Num(), S.Progress.Flags.Num(), S.Progress.Beaten.Num(), S.Progress.Taken.Num());
    for (const FPYRMon& M : S.Trainer.Party) Out += FString::Printf(TEXT("|%s%d"), *M.Species.ToString(), M.Level);
    for (const FPYRItemStack& I : S.Trainer.Bag) Out += FString::Printf(TEXT("|%s%d"), *I.Item.ToString(), I.Count);
    return Out;
}

bool UPYRSave::HasUnsavedChanges() const
{
    return Fingerprint(Capture()) != SavedPrint;
}

bool UPYRSave::SaveNow(const FString& Reason, FString* Why)
{
    if (!CanUseDisk()) { if (Why) *Why = TEXT("automated run"); LastResult = TEXT("skipped"); return false; }
    const FPYRSaveGame S = Capture();
    if (S.Map.IsNone()) { if (Why) *Why = TEXT("Red is not on a map"); LastResult = TEXT("no map"); return false; }
    if (!FSaveSlot::Save(SlotName(), S, FPYRSaveGame::Version))
    {
        if (Why) *Why = TEXT("could not write the slot");
        LastResult = TEXT("write failed");
        return false;
    }
    ++Saves;
    SavedPrint = Fingerprint(S);
    LastResult = TEXT("saved");
    UE_LOG(LogPYRSave, Display, TEXT("PYR_SAVED %s at %s (%d,%d) money %d party %d badges %d (%s)"), *FSaveSlot::PathFor(SlotName()), *S.Map.ToString(),
        S.TileX, S.TileY, S.Trainer.Money, S.Trainer.Party.Num(), S.Badges, *Reason);
    return true;
}

int32 UPYRSave::Load(FPYRSaveGame& Out) const
{
    if (!CanUseDisk()) return 1;
    int32 Found = 0;
    switch (FSaveSlot::Load(SlotName(), Out, FPYRSaveGame::Version, &Found))
    {
    case ESaveSlotResult::Loaded: return 0;
    case ESaveSlotResult::Missing: return 1;
    case ESaveSlotResult::Outdated: return 3;
    default: return 2;
    }
}

bool UPYRSave::Apply(const FPYRSaveGame& S)
{
    UPYRField* Field = UPYRField::Get(this);
    const FYMap* M = Field && Field->GetYellowWorld() && Field->GetYellowWorld()->GetData() ? Field->GetYellowWorld()->GetData()->Map(S.Map) : nullptr;
    if (!M || !M->Contains(S.TileX, S.TileY)) { LastResult = TEXT("map not in this build"); return false; }
    if (UPYRProgress* P = UPYRProgress::Get(this)) P->ApplySnapshot(S.Progress);
    if (UPYRTrainerState* T = UPYRTrainerState::Get(this)) T->ApplySnapshot(S.Trainer);
    Field->LastOutdoor = S.LastOutdoor;
    Field->PlaceRed(S.Map, S.TileX, S.TileY, YDir::FromName(S.Facing));
    bContinued = true;
    SavedPrint = Fingerprint(Capture());
    LastResult = TEXT("continued");
    UE_LOG(LogPYRSave, Display, TEXT("PYR_CONTINUED %s (%d,%d)"), *S.Map.ToString(), S.TileX, S.TileY);
    return true;
}

void UPYRSave::ResetToNewGame()
{
    if (UPYRProgress* P = UPYRProgress::Get(this)) P->ResetToNewGame();
    if (UPYRTrainerState* T = UPYRTrainerState::Get(this)) T->ResetToNewGame();
    SavedPrint.Reset();
}
