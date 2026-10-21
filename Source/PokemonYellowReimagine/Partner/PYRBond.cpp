#include "Partner/PYRBond.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Party/PYRTrainerState.h"
#include "Yellow/YellowData.h"

namespace PYRBond
{
FString TierName(int32 Tier)
{
    static const TCHAR* Names[] = {TEXT("Getting to know you"), TEXT("Friendly"), TEXT("Close"), TEXT("Best friends")};
    return Names[FMath::Clamp(Tier, 0, 3)];
}

const TCHAR* MoodText(int32 Friendship)
{
    return Friendship >= 150 ? TEXT("happy") : Friendship >= 100 ? TEXT("content") : TEXT("grumpy");
}

FName MoodFor(int32 Friendship)
{
    return Friendship >= 150 ? FName(TEXT("happy")) : Friendship >= 100 ? FName(TEXT("content")) : FName(TEXT("grumpy"));
}

int32 Apply(const UObject* Ctx, const FString& Event)
{
    UPYRTrainerState* State = UPYRTrainerState::Get(Ctx);
    if (!State) return 0;
    const FYellowDataSet* Data = UYellowData::Data(Ctx);
    // Yellow's bands (engine/events/pikachu_happiness.asm) stand in when pikachu.json is missing.
    struct FBand { const TCHAR* Event; int32 Low, Mid, High, Mood; };
    static const FBand Defaults[] = {
        {TEXT("levelup"), 5, 3, 2, 138}, {TEXT("used_item"), 5, 3, 2, 131}, {TEXT("used_x_item"), 1, 1, 0, -1}, {TEXT("gym_leader"), 3, 2, 1, -1},
        {TEXT("used_tm_hm"), 1, 1, 0, 148}, {TEXT("walking"), 2, 1, 1, -1}, {TEXT("deposited"), -3, -3, -5, 98}, {TEXT("fainted"), -1, -1, -1, 108},
        {TEXT("poison_fainted"), -5, -5, -10, 98}, {TEXT("careless_trainer"), -5, -5, -10, 108}, {TEXT("traded"), -10, -10, -20, 0}};
    int32 Delta = 0, Mood = -1;
    const int32 Now = State->BondPoints;
    if (Data && Data->Pikachu.Deltas.Contains(Event))
    {
        Delta = Data->Pikachu.Delta(Event, Now);
        Mood = Data->Pikachu.MoodTarget(Event);
    }
    else
        for (const FBand& B : Defaults)
            if (Event == B.Event) { Delta = Now < 100 ? B.Low : Now < 200 ? B.Mid : B.High; Mood = B.Mood; }
    State->AddBond(Delta);
    // A rise moves the mood up to the target, a fall down to it.
    if (Mood >= 0)
    {
        if (Delta >= 0) State->PikachuMood = FMath::Max(State->PikachuMood, Mood);
        else State->PikachuMood = FMath::Min(State->PikachuMood, Mood);
    }
    return Delta;
}

FString Face(const UObject* Ctx)
{
    const UPYRTrainerState* State = UPYRTrainerState::Get(Ctx);
    const FYellowDataSet* Data = UYellowData::Data(Ctx);
    const int32 Happy = State ? State->BondPoints : 90, Mood = State ? State->PikachuMood : 128;
    if (Data && Data->Pikachu.IsLoaded()) return Data->Pikachu.FaceFor(Happy, Mood);
    return MoodFor(Happy).ToString();
}
}

UPYRBond* UPYRBond::Get(const UObject* Ctx)
{
    const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UPYRBond>() : nullptr;
}

int32 UPYRBond::GetFriendship() const
{
    const UGameInstance* GI = GetGameInstance();
    const UPYRTrainerState* State = GI ? GI->GetSubsystem<UPYRTrainerState>() : nullptr;
    return State ? State->BondPoints : 0;
}
