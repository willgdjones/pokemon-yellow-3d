#include "Kanto/PYRProgress.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

UPYRProgress* UPYRProgress::Get(const UObject* Ctx)
{
    const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UPYRProgress>() : nullptr;
}

void UPYRProgress::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ResetToNewGame();
}

void UPYRProgress::Set(FName Flag, bool bOn)
{
    if (Flag.IsNone()) return;
    if (bOn) Flags.Add(Flag);
    else Flags.Remove(Flag);
}

bool UPYRProgress::DepositPc(FName Item, int32 Count)
{
    if (Item.IsNone() || Count <= 0) return false;
    for (FPYRItemStack& S : PcItems) if (S.Item == Item) { S.Count = FMath::Min(99, S.Count + Count); return true; }
    if (PcItems.Num() >= PcSlots) return false;
    PcItems.Add(FPYRItemStack{Item, FMath::Min(99, Count)});
    return true;
}

bool UPYRProgress::WithdrawPc(FName Item, int32 Count)
{
    for (int32 I = 0; I < PcItems.Num(); ++I)
    {
        if (PcItems[I].Item != Item) continue;
        if (PcItems[I].Count < Count) return false;
        PcItems[I].Count -= Count;
        if (PcItems[I].Count <= 0) PcItems.RemoveAt(I);
        return true;
    }
    return false;
}

int32 UPYRProgress::PcCount(FName Item) const
{
    for (const FPYRItemStack& S : PcItems) if (S.Item == Item) return S.Count;
    return 0;
}

FString UPYRProgress::Fill(const FString& Text) const
{
    FString Out = Text;
    Out.ReplaceInline(TEXT("<PLAYER>"), *PlayerName, ESearchCase::CaseSensitive);
    Out.ReplaceInline(TEXT("<RIVAL>"), *RivalName, ESearchCase::CaseSensitive);
    Out.ReplaceInline(TEXT("<PKMN>"), TEXT("POKéMON"), ESearchCase::CaseSensitive);
    Out.ReplaceInline(TEXT("<POKe>"), TEXT("POKé"), ESearchCase::CaseSensitive);
    return Out;
}

FPYRProgressSnapshot UPYRProgress::MakeSnapshot() const
{
    FPYRProgressSnapshot S;
    S.PlayerName = PlayerName;
    S.RivalName = RivalName;
    S.Options = Options;
    S.Flags = Flags.Array();
    S.Taken = Taken.Array();
    S.Beaten = Beaten.Array();
    S.PlaySeconds = PlaySeconds;
    S.RepelSteps = RepelSteps;
    S.PcItems = PcItems;
    S.TrainerId = TrainerId;
    return S;
}

void UPYRProgress::ApplySnapshot(const FPYRProgressSnapshot& S)
{
    PlayerName = S.PlayerName.IsEmpty() ? TEXT("RED") : S.PlayerName.Left(7);
    RivalName = S.RivalName.IsEmpty() ? TEXT("GARY") : S.RivalName.Left(7);
    Options = S.Options;
    Flags = TSet<FName>(S.Flags);
    Taken = TSet<FString>(S.Taken);
    Beaten = TSet<FString>(S.Beaten);
    PlaySeconds = FMath::Max(0.0, S.PlaySeconds);
    RepelSteps = FMath::Max(0, S.RepelSteps);
    PcItems = S.PcItems;
    TrainerId = S.TrainerId;
}

void UPYRProgress::ResetToNewGame()
{
    PlayerName = TEXT("RED");
    RivalName = TEXT("GARY");
    Options = FPYROptions();
    Flags.Reset();
    Taken.Reset();
    Beaten.Reset();
    PlaySeconds = 0;
    RepelSteps = 0;
    PcItems.Reset();
    // Yellow starts Red's PC with a Potion.
    PcItems.Add(FPYRItemStack{FName(TEXT("potion")), 1});
    TrainerId = FMath::RandRange(10000, 65535) % 65536;
}
