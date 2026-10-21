#include "World/PYRAreaLook.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Kanto/YellowWorld.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PokemonYellowReimagineCharacter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stage/PYREncounters.h"
#include "Stage/PYRStage.h"
#include "Engine/Texture.h"
#include "World/PYRPaint.h"

DEFINE_LOG_CATEGORY_STATIC(LogAreaLook, Log, All);

namespace
{
FLinearColor ParseColour(const TSharedPtr<FJsonValue>& V, bool& bOk)
{
    bOk = false;
    if (!V.IsValid()) return FLinearColor::White;
    FString Hex;
    if (V->TryGetString(Hex))
    {
        Hex.RemoveFromStart(TEXT("#"));
        if (Hex.Len() != 6) return FLinearColor::White;
        bOk = true;
        return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
    }
    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
    if (V->TryGetArray(Arr) && Arr->Num() >= 3)
    {
        bOk = true;
        return FLinearColor(float((*Arr)[0]->AsNumber()), float((*Arr)[1]->AsNumber()), float((*Arr)[2]->AsNumber()), 1.f);
    }
    return FLinearColor::White;
}

// One layer of the table (the defaults, a base area, the area) over Out.
void Layer(const TSharedPtr<FJsonObject>& O, FPYRLookValues& Out)
{
    if (!O.IsValid()) return;
    const TSharedPtr<FJsonObject>* Post = nullptr;
    if (O->TryGetObjectField(TEXT("post"), Post))
        for (const auto& Pair : (*Post)->Values)
        {
            double Number = 0;
            if (Pair.Value->TryGetNumber(Number)) { Out.Scalars.Add(FName(*Pair.Key), float(Number)); continue; }
            bool bOk = false;
            const FLinearColor C = ParseColour(Pair.Value, bOk);
            if (bOk) Out.Vectors.Add(FName(*Pair.Key), C);
        }
    const TSharedPtr<FJsonObject>* Sun = nullptr;
    if (O->TryGetObjectField(TEXT("sun"), Sun))
    {
        const TArray<TSharedPtr<FJsonValue>>* Rot = nullptr;
        if ((*Sun)->TryGetArrayField(TEXT("rotation"), Rot) && Rot->Num() >= 2)
            Out.SunRotation = FRotator(float((*Rot)[0]->AsNumber()), float((*Rot)[1]->AsNumber()), Rot->Num() > 2 ? float((*Rot)[2]->AsNumber()) : 0.f);
        bool bOk = false;
        const FLinearColor C = ParseColour((*Sun)->TryGetField(TEXT("color")), bOk);
        if (bOk) Out.SunColor = C;
        double I = 0;
        if ((*Sun)->TryGetNumberField(TEXT("intensity"), I)) Out.SunIntensity = float(I);
    }
    double D = 0;
    if (O->TryGetNumberField(TEXT("sky_light"), D)) Out.SkyLight = float(D);
    bool bOk = false;
    FLinearColor C = ParseColour(O->TryGetField(TEXT("sky_light_color")), bOk);
    if (bOk) Out.SkyLightColor = C;
    const TSharedPtr<FJsonObject>* Sky = nullptr;
    if (O->TryGetObjectField(TEXT("sky"), Sky))
    {
        C = ParseColour((*Sky)->TryGetField(TEXT("top")), bOk);
        if (bOk) Out.SkyTop = C;
        C = ParseColour((*Sky)->TryGetField(TEXT("horizon")), bOk);
        if (bOk) Out.SkyHorizon = C;
        if ((*Sky)->TryGetNumberField(TEXT("painted"), D)) Out.SkyPainted = float(D);
        if ((*Sky)->TryGetNumberField(TEXT("painted_offset"), D)) Out.SkyPaintedOffset = float(D);
        if ((*Sky)->TryGetNumberField(TEXT("painted_scale"), D)) Out.SkyPaintedScale = float(D);
        FString Tex;
        if ((*Sky)->TryGetStringField(TEXT("texture"), Tex)) Out.SkyTexture = Tex;
        C = ParseColour((*Sky)->TryGetField(TEXT("painted_tint")), bOk);
        if (bOk) Out.SkyPaintedTint = C;
    }
    const TSharedPtr<FJsonObject>* Volume = nullptr;
    if (O->TryGetObjectField(TEXT("volume"), Volume))
    {
        if ((*Volume)->TryGetNumberField(TEXT("vignette"), D)) Out.Vignette = float(D);
        if ((*Volume)->TryGetNumberField(TEXT("bloom"), D)) Out.Bloom = float(D);
    }
}
}

void FPYRLookValues::LerpTo(const FPYRLookValues& B, float A)
{
    for (const auto& Pair : B.Scalars)
    {
        float& V = Scalars.FindOrAdd(Pair.Key, Pair.Value);
        V = FMath::Lerp(V, Pair.Value, A);
    }
    for (const auto& Pair : B.Vectors)
    {
        FLinearColor& V = Vectors.FindOrAdd(Pair.Key, Pair.Value);
        V = FMath::Lerp(V, Pair.Value, A);
    }
    SunRotation = FQuat::Slerp(SunRotation.Quaternion(), B.SunRotation.Quaternion(), A).Rotator();
    SunColor = FMath::Lerp(SunColor, B.SunColor, A);
    SunIntensity = FMath::Lerp(SunIntensity, B.SunIntensity, A);
    SkyLight = FMath::Lerp(SkyLight, B.SkyLight, A);
    SkyLightColor = FMath::Lerp(SkyLightColor, B.SkyLightColor, A);
    SkyTop = FMath::Lerp(SkyTop, B.SkyTop, A);
    SkyHorizon = FMath::Lerp(SkyHorizon, B.SkyHorizon, A);
    SkyPainted = FMath::Lerp(SkyPainted, B.SkyPainted, A);
    SkyPaintedOffset = FMath::Lerp(SkyPaintedOffset, B.SkyPaintedOffset, A);
    SkyPaintedScale = A < .5f ? SkyPaintedScale : B.SkyPaintedScale;
    SkyTexture = A < .5f ? SkyTexture : B.SkyTexture;
    SkyPaintedTint = FMath::Lerp(SkyPaintedTint, B.SkyPaintedTint, A);
    Vignette = FMath::Lerp(Vignette, B.Vignette, A);
    Bloom = FMath::Lerp(Bloom, B.Bloom, A);
}

UPYRAreaLook* UPYRAreaLook::Get(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UPYRAreaLook>() : nullptr;
}

FString UPYRAreaLook::TablePath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Look/Kit/areas.json")));
}

void UPYRAreaLook::Load()
{
    if (bTriedLoad) return;
    bTriedLoad = true;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *TablePath())) { Error = TEXT("missing ") + TablePath(); UE_LOG(LogAreaLook, Error, TEXT("PYR_AREA_LOOK %s"), *Error); return; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) { Error = TEXT("bad JSON in ") + TablePath(); UE_LOG(LogAreaLook, Error, TEXT("PYR_AREA_LOOK %s"), *Error); return; }
    double D = 0;
    if (Root->TryGetNumberField(TEXT("blend_seconds"), D)) BlendSeconds = FMath::Max(.01f, float(D));
    const TSharedPtr<FJsonObject>* Obj = nullptr;
    if (Root->TryGetObjectField(TEXT("defaults"), Obj)) Defaults = *Obj;
    if (Root->TryGetObjectField(TEXT("areas"), Obj))
        for (const auto& Pair : (*Obj)->Values)
            if (Pair.Value->Type == EJson::Object) Areas.Add(FName(*Pair.Key), Pair.Value->AsObject());
    const TArray<TSharedPtr<FJsonValue>>* Rules = nullptr;
    if (Root->TryGetArrayField(TEXT("maps"), Rules))
        for (const TSharedPtr<FJsonValue>& R : *Rules)
        {
            const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
            if (R->TryGetArray(Pair) && Pair->Num() == 2) MapRules.Add({(*Pair)[0]->AsString(), FName(*(*Pair)[1]->AsString())});
        }
    if (Root->TryGetObjectField(TEXT("stages"), Obj))
        for (const auto& Pair : (*Obj)->Values) StageAreas.Add(FString(*Pair.Key), FName(*Pair.Value->AsString()));
    bLoaded = Areas.Num() > 0;
    UE_LOG(LogAreaLook, Display, TEXT("PYR_AREA_LOOK loaded %d areas, %d map rules, %d stages from %s"), Areas.Num(), MapRules.Num(), StageAreas.Num(), *TablePath());
}

TArray<FName> UPYRAreaLook::AreaIds() const
{
    TArray<FName> Out;
    Areas.GetKeys(Out);
    return Out;
}

FName UPYRAreaLook::AreaForMap(FName Map) const
{
    const FString Id = Map.ToString();
    for (const auto& Rule : MapRules)
    {
        const bool bWild = Rule.Key.EndsWith(TEXT("*"));
        if (bWild ? Id.StartsWith(Rule.Key.LeftChop(1)) : Id == Rule.Key) return Rule.Value;
        if (Rule.Key.StartsWith(TEXT("*")) && Id.EndsWith(Rule.Key.RightChop(1))) return Rule.Value;
    }
    return NAME_None;
}

FName UPYRAreaLook::AreaForStage(const FString& Stage) const
{
    const FName* A = StageAreas.Find(Stage);
    return A ? *A : NAME_None;
}

bool UPYRAreaLook::Values(FName Area, FPYRLookValues& Out) const
{
    const TSharedPtr<FJsonObject>* O = Areas.Find(Area);
    if (!O) return false;
    TArray<TSharedPtr<FJsonObject>> Chain;
    for (TSharedPtr<FJsonObject> At = *O; At.IsValid() && Chain.Num() < 8; )
    {
        Chain.Insert(At, 0);
        FString Base;
        const TSharedPtr<FJsonObject>* Next = At->TryGetStringField(TEXT("base"), Base) ? Areas.Find(FName(*Base)) : nullptr;
        At = Next ? *Next : nullptr;
    }
    Out = FPYRLookValues();
    Layer(Defaults, Out);
    for (const TSharedPtr<FJsonObject>& L : Chain) Layer(L, Out);
    return true;
}

void UPYRAreaLook::Want(FName Area, bool bSnap)
{
    Load();
    if (!bLoaded || Area.IsNone() || !Areas.Contains(Area)) return;
    if (Area == Target && !bSnap) return;
    FPYRLookValues V;
    Values(Area, V);
    if (Target.IsNone()) bSnap = true;
    From = Now;
    To = V;
    Target = Area;
    Blend = bSnap ? 1.f : 0.f;
    if (bSnap) Now = To;
    bDirty = true;
    UE_LOG(LogAreaLook, Display, TEXT("PYR_AREA_LOOK %s%s"), *Area.ToString(), bSnap ? TEXT(" (snap)") : TEXT(""));
}

void UPYRAreaLook::Force(FName Area, bool bSnap)
{
    Forced = Area;
    if (!Area.IsNone()) Want(Area, bSnap);
    else if (!FocusArea.IsNone()) Want(FocusArea, bSnap);
}

void UPYRAreaLook::OnFocus(FName Map, bool bWasOutdoor, bool bIsOutdoor)
{
    Load();
    FocusArea = AreaForMap(Map);
    if (Forced.IsNone() && !bInBattle) Want(FocusArea, !(bWasOutdoor && bIsOutdoor));
}

void UPYRAreaLook::Tick(float DeltaTime)
{
    Load();
    if (!bLoaded) return;
    AYellowWorld* World = AYellowWorld::Get(this);
    if (!World) return;
    // A fight on a battle stage shows the stage's area.
    const UPYREncounters* Enc = UPYREncounters::Get(this);
    const APYRBattleStage* Stage = Enc ? Enc->GetCurrentStage() : nullptr;
    const bool bBattle = Stage && Enc && (Enc->GetPhase() == EPYREncounterPhase::Fight || Enc->GetPhase() == EPYREncounterPhase::After);
    if (Forced.IsNone())
    {
        if (bBattle && !bInBattle) Want(AreaForStage(APYRBattleStage::Name(Stage->Kind)), true);
        else if (!bBattle && bInBattle) Want(FocusArea, true);
        else if (!bBattle && FocusArea.IsNone() && !World->GetFocus().IsNone()) { FocusArea = AreaForMap(World->GetFocus()); Want(FocusArea, true); }
    }
    bInBattle = bBattle;
    if (Target.IsNone()) return;
    if (Blend < 1.f)
    {
        Blend = FMath::Min(1.f, Blend + DeltaTime / BlendSeconds);
        Now = From;
        Now.LerpTo(To, FMath::SmoothStep(0.f, 1.f, Blend));
        bDirty = true;
    }
    // Everything is set when it changes; only the glow's centre follows Red every frame (when there is a glow).
    Apply(World, bDirty);
    bDirty = false;
}

void UPYRAreaLook::Apply(AYellowWorld* World, bool bAll)
{
    if (UMaterialInstanceDynamic* Toon = World->GetToonMaterial())
    {
        const float* Glow = Now.Scalars.Find(TEXT("GlowAmount"));
        if (!bAll && !(Glow && *Glow > 0.f)) return;
        if (bAll)
        {
            for (const auto& Pair : Now.Scalars) Toon->SetScalarParameterValue(Pair.Key, Pair.Value);
            for (const auto& Pair : Now.Vectors) Toon->SetVectorParameterValue(Pair.Key, Pair.Value);
        }
        // The pool of light round the player (Mt. Moon's glow) follows Red, or the middle of the fight.
        FVector Centre = FVector::ZeroVector;
        if (const APokemonYellowReimagineCharacter* Red = APokemonYellowReimagineCharacter::Find(this)) Centre = Red->GetActorLocation();
        if (bInBattle)
            if (const UPYREncounters* Enc = UPYREncounters::Get(this))
                if (const APYRBattleStage* Stage = Enc->GetCurrentStage()) Centre = Stage->Centre;
        Toon->SetVectorParameterValue(TEXT("GlowCenter"), FLinearColor(Centre.X, Centre.Y, Centre.Z, 1.f));
    }
    if (!bAll) return;
    if (APostProcessVolume* Volume = World->GetToonVolume())
    {
        Volume->Settings.VignetteIntensity = Now.Vignette;
        Volume->Settings.BloomIntensity = Now.Bloom;
    }
    if (ADirectionalLight* Sun = World->GetSun())
        if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
        {
            if (!L->GetComponentRotation().Equals(Now.SunRotation, .01f)) Sun->SetActorRotation(Now.SunRotation);
            L->SetLightColor(Now.SunColor);
            L->SetIntensity(Now.SunIntensity);
        }
    if (ASkyLight* Fill = World->GetSkyFill())
        if (USkyLightComponent* L = Fill->GetLightComponent())
        {
            L->SetIntensity(Now.SkyLight);
            L->SetLightColor(Now.SkyLightColor);
        }
    if (UMaterialInstanceDynamic* Sky = World->GetSkyMaterial())
    {
        if (Now.SkyTop != FLinearColor::Black) Sky->SetVectorParameterValue(TEXT("Top"), Now.SkyTop);
        if (Now.SkyHorizon != FLinearColor::Black) Sky->SetVectorParameterValue(TEXT("Horizon"), Now.SkyHorizon);
        Sky->SetScalarParameterValue(TEXT("SkyTexAmount"), Now.SkyPainted);
        Sky->SetScalarParameterValue(TEXT("SkyTexOffset"), Now.SkyPaintedOffset);
        Sky->SetScalarParameterValue(TEXT("SkyTexScale"), Now.SkyPaintedScale);
        static TMap<FString, TWeakObjectPtr<UTexture>> Textures;
        const FString Want = Now.SkyTexture.IsEmpty() ? FString(TEXT("/Game/Look/Kit/Textures/T_Kit_Sky")) : Now.SkyTexture;
        TWeakObjectPtr<UTexture>& Tex = Textures.FindOrAdd(Want);
        if (!Tex.IsValid()) Tex = Cast<UTexture>(PYR::LoadOptional(UTexture::StaticClass(), Want));
        if (Tex.IsValid()) Sky->SetTextureParameterValue(TEXT("SkyTex"), Tex.Get());
        Sky->SetVectorParameterValue(TEXT("SkyTexTint"), Now.SkyPaintedTint);
    }
    // The painted sky has its own clouds: the dome's cloud pieces show only over the plain gradient.
    World->SetDomeClouds(Now.SkyPainted < .5f);
}
