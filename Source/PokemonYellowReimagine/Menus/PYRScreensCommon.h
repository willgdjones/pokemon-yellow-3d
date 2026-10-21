// Shared pieces of the Game Boy screens (PYRScreens*.cpp only): lookups, a paged full-screen box, the quantity picker
// and the money box.
#pragma once

#include "CoreMinimal.h"
#include "Kanto/PYRProgress.h"
#include "Menus/PYRMenus.h"
#include "Menus/PYRScreens.h"
#include "Menus/PYRSysText.h"
#include "Party/PYRTrainerState.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Yellow/YellowData.h"

namespace PYRScreensCommon
{
inline UPYRMenus* Menus(const UObject* C) { return UPYRMenus::Get(C); }
inline UPYRTrainerState* State(const UObject* C) { return UPYRTrainerState::Get(C); }
inline UPYRProgress* Prog(const UObject* C) { return UPYRProgress::Get(C); }
inline const FYellowDataSet* Data(const UObject* C) { return UYellowData::Data(C); }
inline FString Sys(const TCHAR* Label, const TMap<FString, FString>& Fills = TMap<FString, FString>()) { return PYRSysText::Get(Label, Fills); }
inline TMap<FString, FString> F1(const TCHAR* A, const FString& V) { TMap<FString, FString> M; M.Add(A, V); return M; }
inline TMap<FString, FString> F2(const TCHAR* A, const FString& V, const TCHAR* B, const FString& W) { TMap<FString, FString> M; M.Add(A, V); M.Add(B, W); return M; }

// A full-screen white box with pages of text (and an optional picture); Confirm or Back turns the page, then closes.
class FPageScreen : public FPYRScreen
{
public:
    TArray<FString> Pages;
    int32 At = 0;
    TFunction<void()> OnDone;
    TSharedPtr<SWidget> Extra;
    bool bFull = true;
    virtual bool IsFullScreen() const override { return bFull; }
    virtual TSharedRef<SWidget> Build() override
    {
        TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
        if (Extra.IsValid()) Col->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 20))[Extra.ToSharedRef()];
        Col->AddSlot().AutoHeight()[PYRGb::Text(Pages.IsValidIndex(At) ? Pages[At] : FString(), 30)];
        if (!bFull) return PYRGb::PlaceAligned(PYRGb::Box(Col, 24.f), FVector2D(.5f, .45f), FVector2D(.5f, .5f));
        return PYRGb::Page(PYRGb::Box(Col, 30.f));
    }
    virtual bool Input(EPYRKey Key) override
    {
        if (Key != EPYRKey::Confirm && Key != EPYRKey::Back) return true;
        if (At + 1 < Pages.Num()) { ++At; Refresh(); return true; }
        TFunction<void()> Done = OnDone;
        Close();
        if (Done) Done();
        return true;
    }
    virtual FString Describe() const override { return Pages.IsValidIndex(At) ? Pages[At] : FString(); }
};

inline TSharedRef<FPYRListScreen> MakeList(FName Id, const TArray<FString>& Items, FVector2D Anchor, TFunction<void(int32)> OnPick, TFunction<void()> OnBack = nullptr)
{
    TSharedRef<FPYRListScreen> L = MakeShared<FPYRListScreen>();
    L->Id = Id;
    L->Items = Items;
    L->Anchor = Anchor;
    L->OnPick = MoveTemp(OnPick);
    L->OnBack = MoveTemp(OnBack);
    return L;
}

// The quantity picker: Up/Down by one, Left/Right by ten, 1..Max; Confirm picks, Back gives 0.
class FQuantityScreen : public FPYRScreen
{
public:
    int32 Qty = 1, Max = 99, UnitPrice = 0;
    TFunction<void(int32)> OnPick;
    virtual TSharedRef<SWidget> Build() override
    {
        const FString Line = UnitPrice > 0 ? FString::Printf(TEXT("×%02d  %s"), Qty, *PYRGb::Money(Qty * UnitPrice)) : FString::Printf(TEXT("×%02d"), Qty);
        return PYRGb::PlaceAligned(PYRGb::Box(PYRGb::Text(Line, 28)), FVector2D(.95f, .62f), FVector2D(1.f, 0.f));
    }
    virtual bool Input(EPYRKey Key) override
    {
        switch (Key)
        {
        case EPYRKey::Up: Qty = Qty >= Max ? 1 : Qty + 1; Refresh(); return true;
        case EPYRKey::Down: Qty = Qty <= 1 ? Max : Qty - 1; Refresh(); return true;
        case EPYRKey::Right: Qty = FMath::Min(Max, Qty + 10); Refresh(); return true;
        case EPYRKey::Left: Qty = FMath::Max(1, Qty - 10); Refresh(); return true;
        case EPYRKey::Confirm: { TFunction<void(int32)> P = OnPick; const int32 Q = Qty; Close(); if (P) P(Q); return true; }
        case EPYRKey::Back: { TFunction<void(int32)> P = OnPick; Close(); if (P) P(0); return true; }
        default: return true;
        }
    }
    virtual FString Describe() const override { return FString::Printf(TEXT("qty %d of %d (%d each)"), Qty, Max, UnitPrice); }
};

inline void AskQuantity(const UObject* Ctx, int32 Max, int32 UnitPrice, TFunction<void(int32)> OnPick)
{
    TSharedRef<FQuantityScreen> Q = MakeShared<FQuantityScreen>();
    Q->Id = TEXT("quantity");
    Q->Max = FMath::Max(1, Max);
    Q->UnitPrice = UnitPrice;
    Q->OnPick = MoveTemp(OnPick);
    if (UPYRMenus* M = Menus(Ctx)) M->Push(Q);
}

// Red's money in a box (the Mart).
class FMoneyScreen : public FPYRScreen
{
public:
    virtual TSharedRef<SWidget> Build() override
    {
        const UPYRTrainerState* S = State(GetWorld());
        return PYRGb::Place(PYRGb::Box(PYRGb::Text(FString::Printf(TEXT("MONEY\n%s"), *PYRGb::Money(S ? S->Money : 0)), 26)), FVector2D(.74f, .04f));
    }
    virtual bool Input(EPYRKey) override { return false; }
    virtual FString Describe() const override { const UPYRTrainerState* S = State(GetWorld()); return FString::Printf(TEXT("MONEY %d"), S ? S->Money : 0); }
};
}
