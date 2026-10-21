#include "Yellow/YellowTables.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Party/PYRSpecies.h"
#include "Yellow/YellowData.h"

namespace YJson
{
// Shared with YellowData.cpp.
FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Default);
int32 Int(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32 Default);
const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key);
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key);
TSharedPtr<FJsonObject> AsObj(const TSharedPtr<FJsonValue>& V);
FString AsStr(const TSharedPtr<FJsonValue>& V);
int32 AsInt(const TSharedPtr<FJsonValue>& V, int32 Default);
}

namespace YellowTables
{
using namespace YJson;

EPYRType TypeFrom(const FString& Name)
{
    const FString K = YKey::Alnum(Name);
    struct FRow { const TCHAR* Key; EPYRType Type; };
    static const FRow Rows[] = {
        {TEXT("normal"), EPYRType::Normal}, {TEXT("fire"), EPYRType::Fire}, {TEXT("water"), EPYRType::Water},
        {TEXT("electric"), EPYRType::Electric}, {TEXT("grass"), EPYRType::Grass}, {TEXT("ice"), EPYRType::Ice},
        {TEXT("fighting"), EPYRType::Fighting}, {TEXT("poison"), EPYRType::Poison}, {TEXT("ground"), EPYRType::Ground},
        {TEXT("flying"), EPYRType::Flying}, {TEXT("psychic"), EPYRType::Psychic}, {TEXT("bug"), EPYRType::Bug},
        {TEXT("rock"), EPYRType::Rock}, {TEXT("ghost"), EPYRType::Ghost}, {TEXT("dragon"), EPYRType::Dragon},
    };
    for (const FRow& R : Rows) if (K.StartsWith(R.Key)) return R.Type;
    return EPYRType::None;
}

FString TitleCase(const FString& GbName)
{
    FString Out = GbName;
    bool bStart = true;
    for (int32 I = 0; I < Out.Len(); ++I)
    {
        TCHAR& C = Out[I];
        if (FChar::IsAlpha(C)) { C = bStart ? FChar::ToUpper(C) : FChar::ToLower(C); bStart = false; }
        else bStart = C == TEXT(' ') || C == TEXT('.') || C == TEXT('-');
    }
    // POKé stays as the game spells it.
    Out.ReplaceInline(TEXT("POKé"), TEXT("Poké"), ESearchCase::CaseSensitive);
    return Out;
}

namespace
{
EPYRGrowth GrowthFrom(const TSharedPtr<FJsonValue>& V)
{
    if (V.IsValid() && V->Type == EJson::Number)
    {
        // pokeyellow growth ids: 0 medium fast, 3 medium slow, 4 fast, 5 slow (1 and 2 are unused in Gen 1).
        switch (int32(V->AsNumber())) { case 3: return EPYRGrowth::MediumSlow; case 4: return EPYRGrowth::Fast; case 5: return EPYRGrowth::Slow; default: return EPYRGrowth::MediumFast; }
    }
    const FString K = YKey::Alnum(AsStr(V));
    if (K.Contains(TEXT("mediumslow"))) return EPYRGrowth::MediumSlow;
    if (K.Contains(TEXT("mediumfast"))) return EPYRGrowth::MediumFast;
    if (K.Contains(TEXT("slow"))) return EPYRGrowth::Slow;
    if (K.Contains(TEXT("fast"))) return EPYRGrowth::Fast;
    return EPYRGrowth::MediumFast;
}

FLinearColor TypeTint(EPYRType T)
{
    return PYRData::TypeColour(T);
}

EPYRBody BodyFor(const FPYRSpeciesInfo& S)
{
    if (S.Type1 == EPYRType::Flying || S.Type2 == EPYRType::Flying) return EPYRBody::Flier;
    if (S.HeightM >= 3.f) return EPYRBody::Giant;
    if (S.Type1 == EPYRType::Bug && S.HeightM < .6f) return EPYRBody::Crawler;
    return EPYRBody::Runner;
}

float Num(const TSharedPtr<FJsonObject>& O, std::initializer_list<const TCHAR*> Keys, float Default)
{
    for (const TCHAR* K : Keys)
    {
        const TSharedPtr<FJsonValue> V = O.IsValid() ? O->TryGetField(K) : nullptr;
        if (V.IsValid() && V->Type == EJson::Number) return float(V->AsNumber());
        if (V.IsValid() && V->Type == EJson::String && V->AsString().IsNumeric()) return FCString::Atof(*V->AsString());
    }
    return Default;
}
}

int32 ImportSpecies(const TSharedPtr<FJsonObject>& Json)
{
    if (!Json.IsValid()) return 0;
    int32 Count = 0;
    for (const auto& Pair : Json->Values)
    {
        const TSharedPtr<FJsonObject> O = AsObj(Pair.Value);
        if (!O.IsValid()) continue;
        const FString Key(*Pair.Key);
        const FString GbName = Str(O, TEXT("name"), Key);
        const FName Id = YKey::Species(GbName.IsEmpty() ? Key : GbName);
        if (Id.IsNone()) continue;
        const bool bNew = PYRData::Species(Id) == nullptr;
        FPYRSpeciesInfo& S = PYRData::MutableSpecies(Id);
        S.GbName = GbName.ToUpper();
        if (bNew) S.Name = FText::FromString(TitleCase(GbName));
        S.Dex = Int(O, TEXT("dex"), S.Dex);
        if (const TArray<TSharedPtr<FJsonValue>>* Types = Arr(O, TEXT("types")))
        {
            if (Types->Num() > 0) S.Type1 = TypeFrom(AsStr((*Types)[0]));
            S.Type2 = Types->Num() > 1 ? TypeFrom(AsStr((*Types)[1])) : EPYRType::None;
            if (S.Type2 == S.Type1) S.Type2 = EPYRType::None;
        }
        if (const TSharedPtr<FJsonObject> B = Obj(O, TEXT("base")))
        {
            S.BaseHp = int32(Num(B, {TEXT("hp")}, S.BaseHp));
            S.BaseAttack = int32(Num(B, {TEXT("atk"), TEXT("attack")}, S.BaseAttack));
            S.BaseDefence = int32(Num(B, {TEXT("def"), TEXT("defense"), TEXT("defence")}, S.BaseDefence));
            S.BaseSpeed = int32(Num(B, {TEXT("spd"), TEXT("speed")}, S.BaseSpeed));
            S.BaseSpecial = int32(Num(B, {TEXT("spc"), TEXT("special")}, S.BaseSpecial));
        }
        S.CatchRate = Int(O, TEXT("catch_rate"), S.CatchRate);
        S.BaseExp = Int(O, TEXT("base_exp"), S.BaseExp);
        if (const TSharedPtr<FJsonValue> G = O->TryGetField(TEXT("growth"))) S.Growth = GrowthFrom(G);
        // Yellow's whole learnset (the built-in rows stop near level 25). Level-1 moves are kept from the old row when
        // the file lists none.
        if (const TArray<TSharedPtr<FJsonValue>>* L = Arr(O, TEXT("learnset")))
        {
            TArray<FPYRLearn> Learn;
            for (const TSharedPtr<FJsonValue>& E : *L)
            {
                int32 Level = 0;
                FString Move;
                if (E->Type == EJson::Array && E->AsArray().Num() >= 2) { Level = AsInt(E->AsArray()[0], 0); Move = AsStr(E->AsArray()[1]); }
                else if (const TSharedPtr<FJsonObject> EO = AsObj(E)) { Level = Int(EO, TEXT("level"), 0); Move = Str(EO, TEXT("move"), FString()); }
                const FName M = YKey::Move(Move);
                if (Level > 0 && !M.IsNone()) Learn.Add(FPYRLearn{Level, M});
            }
            for (const FString& MovesKey : {FString(TEXT("start_moves")), FString(TEXT("base_moves")), FString(TEXT("level1_moves"))})
                if (const TArray<TSharedPtr<FJsonValue>>* Start = Arr(O, *MovesKey))
                    for (int32 I = Start->Num() - 1; I >= 0; --I)
                    {
                        const FName M = YKey::Move(AsStr((*Start)[I]));
                        if (!M.IsNone() && !Learn.ContainsByPredicate([&](const FPYRLearn& X) { return X.Level == 1 && X.Move == M; })) Learn.Insert(FPYRLearn{1, M}, 0);
                    }
            const bool bHasLevelOne = Learn.ContainsByPredicate([](const FPYRLearn& X) { return X.Level == 1; });
            if (!bHasLevelOne)
                for (int32 I = S.Learnset.Num() - 1; I >= 0; --I) if (S.Learnset[I].Level == 1) Learn.Insert(S.Learnset[I], 0);
            Learn.StableSort([](const FPYRLearn& A, const FPYRLearn& B) { return A.Level < B.Level; });
            if (Learn.Num() > 0) S.Learnset = MoveTemp(Learn);
        }
        if (const TArray<TSharedPtr<FJsonValue>>* Evos = Arr(O, TEXT("evolutions")))
        {
            S.Evolutions.Reset();
            for (const TSharedPtr<FJsonValue>& E : *Evos)
            {
                const TSharedPtr<FJsonObject> EO = AsObj(E);
                if (!EO.IsValid()) continue;
                FPYREvolution Ev;
                const FString Method = Str(EO, TEXT("method"), TEXT("level")).ToLower();
                Ev.Into = YKey::Species(Str(EO, TEXT("to"), FString()));
                const FString Param = Str(EO, TEXT("param"), FString());
                if (Method.Contains(TEXT("item"))) { Ev.Kind = EPYREvolveKind::Item; Ev.Item = YKey::Item(Param); }
                else if (Method.Contains(TEXT("trade"))) { Ev.Kind = EPYREvolveKind::Trade; Ev.Level = Param.IsNumeric() ? FCString::Atoi(*Param) : 1; }
                else { Ev.Kind = EPYREvolveKind::Level; Ev.Level = Param.IsNumeric() ? FCString::Atoi(*Param) : Int(EO, TEXT("level"), Int(EO, TEXT("min_level"), 0)); }
                if (Ev.Into.IsNone()) continue;
                S.Evolutions.Add(Ev);
                if (Ev.Kind == EPYREvolveKind::Level && Ev.Level > 0) { S.EvolveLevel = Ev.Level; S.EvolvesInto = Ev.Into; }
            }
        }
        if (const TArray<TSharedPtr<FJsonValue>>* Tms = Arr(O, TEXT("tm")))
        {
            S.Tms.Reset();
            for (const TSharedPtr<FJsonValue>& E : *Tms)
            {
                const FString T = AsStr(E);
                // Numbers (1-50 TMs, 51-55 HMs) or names ("TM34", "HM01").
                if (E->Type == EJson::Number) S.Tms.AddUnique(int32(E->AsNumber()));
                else if (T.StartsWith(TEXT("HM"), ESearchCase::IgnoreCase)) S.Tms.AddUnique(50 + FCString::Atoi(*YKey::Alnum(T).Mid(2)));
                else S.Tms.AddUnique(FCString::Atoi(*YKey::Alnum(T).Replace(TEXT("tm"), TEXT(""))));
            }
        }
        if (const TSharedPtr<FJsonObject> D = Obj(O, TEXT("dex_entry")) ? Obj(O, TEXT("dex_entry")) : Obj(O, TEXT("dex")))
        {
            S.Category = FText::FromString(Str(D, TEXT("category"), FString()));
            const float H = Num(D, {TEXT("height_m")}, 0.f);
            if (H > 0.f && bNew) S.HeightM = H;
            S.WeightKg = Num(D, {TEXT("weight_kg")}, 0.f);
            S.DexText = FText::FromString(Str(D, TEXT("text"), FString()));
        }
        if (bNew)
        {
            S.Colour = TypeTint(S.Type1);
            S.Accent = PYRData::TypeColour(S.Type2 != EPYRType::None ? S.Type2 : S.Type1) * .6f;
            S.Body = BodyFor(S);
        }
        ++Count;
    }
    return Count;
}

int32 ImportMoves(const TSharedPtr<FJsonObject>& Json)
{
    if (!Json.IsValid()) return 0;
    int32 Count = 0;
    for (const auto& Pair : Json->Values)
    {
        const TSharedPtr<FJsonObject> O = AsObj(Pair.Value);
        if (!O.IsValid()) continue;
        const FString Key(*Pair.Key);
        const FString GbName = Str(O, TEXT("name"), Key);
        const FName Id = YKey::Move(Key.IsEmpty() ? GbName : Key);
        if (Id.IsNone()) continue;
        const bool bNew = PYRData::Move(Id) == nullptr;
        FPYRMoveInfo& M = PYRData::MutableMove(Id);
        M.GbName = GbName.ToUpper();
        M.Pp = Int(O, TEXT("pp"), M.Pp);
        if (!bNew) { ++Count; continue; }  // The built-in rows already carry Yellow's numbers and the live-battle layer.
        M.Name = FText::FromString(TitleCase(GbName));
        M.Type = TypeFrom(Str(O, TEXT("type"), TEXT("normal")));
        if (M.Type == EPYRType::None) M.Type = EPYRType::Normal;
        M.Power = Num(O, {TEXT("power")}, 0.f);
        float Acc = Num(O, {TEXT("accuracy")}, 100.f);
        if (Acc > 100.f) Acc = FMath::RoundToFloat(Acc * 100.f / 255.f);
        M.Accuracy = Acc;
        const FString Effect = Str(O, TEXT("effect"), FString()).ToUpper();
        const bool bSpecial = M.Type == EPYRType::Fire || M.Type == EPYRType::Water || M.Type == EPYRType::Grass || M.Type == EPYRType::Electric
            || M.Type == EPYRType::Ice || M.Type == EPYRType::Psychic || M.Type == EPYRType::Dragon;
        // The live-battle layer for a move this game had no row for: damaging moves lunge (physical) or fly (special),
        // stronger ones cool down longer; status moves are debuffs, self boosts or powders by their Yellow effect.
        if (M.Power > 0)
        {
            M.Shape = bSpecial ? EPYRMoveShape::Projectile : EPYRMoveShape::Dash;
            M.Tier = M.Power >= 90 ? EPYRMoveTier::Big : M.Power >= 60 ? EPYRMoveTier::Tactical : EPYRMoveTier::Quick;
            M.Cooldown = M.Tier == EPYRMoveTier::Big ? 12.f : M.Tier == EPYRMoveTier::Tactical ? 3.5f : 1.1f;
            M.Range = bSpecial ? 1300.f : 400.f;
            M.Radius = bSpecial ? 65.f : 60.f;
            M.TellSeconds = M.Tier == EPYRMoveTier::Big ? .9f : .6f;
            M.HitSeconds = bSpecial ? .6f : .3f;
            if (Effect.Contains(TEXT("PARALYZE"))) { M.Effect = EPYREffect::Paralyse; M.EffectChance = .1f; }
            else if (Effect.Contains(TEXT("BURN"))) { M.Effect = EPYREffect::Burn; M.EffectChance = .1f; }
            else if (Effect.Contains(TEXT("POISON"))) { M.Effect = EPYREffect::Poison; M.EffectChance = .2f; }
            else if (Effect.Contains(TEXT("FLINCH"))) { M.Effect = EPYREffect::Flinch; M.EffectChance = .1f; }
            else if (Effect.Contains(TEXT("DRAIN"))) M.Effect = EPYREffect::Drain;
        }
        else
        {
            M.Tier = EPYRMoveTier::Tactical;
            M.Cooldown = 5.f;
            M.TellSeconds = .6f;
            M.HitSeconds = .2f;
            M.Range = 1000.f;
            auto StatOf = [&Effect]() {
                if (Effect.Contains(TEXT("ATTACK"))) return EPYRStat::Attack;
                if (Effect.Contains(TEXT("DEFENSE"))) return EPYRStat::Defence;
                if (Effect.Contains(TEXT("SPEED"))) return EPYRStat::Speed;
                if (Effect.Contains(TEXT("SPECIAL"))) return EPYRStat::Special;
                if (Effect.Contains(TEXT("ACCURACY"))) return EPYRStat::Accuracy;
                return EPYRStat::Evasion;
            };
            const int32 Stages = Effect.Contains(TEXT("2")) ? 2 : 1;
            if (Effect.Contains(TEXT("UP"))) { M.Shape = EPYRMoveShape::Self; M.Effect = EPYREffect::SelfStat; M.Stat = StatOf(); M.Stages = Stages; M.Accuracy = 0; }
            else if (Effect.Contains(TEXT("DOWN"))) { M.Shape = EPYRMoveShape::Debuff; M.Effect = EPYREffect::FoeStat; M.Stat = StatOf(); M.Stages = -Stages; }
            else if (Effect.Contains(TEXT("SLEEP"))) { M.Shape = EPYRMoveShape::Circle; M.Radius = 160.f; M.Effect = EPYREffect::Sleep; }
            else if (Effect.Contains(TEXT("PARALYZE"))) { M.Shape = EPYRMoveShape::Circle; M.Radius = 160.f; M.Effect = EPYREffect::Paralyse; }
            else if (Effect.Contains(TEXT("POISON"))) { M.Shape = EPYRMoveShape::Circle; M.Radius = 160.f; M.Effect = EPYREffect::Poison; }
            else if (Effect.Contains(TEXT("CONFUSION"))) { M.Shape = EPYRMoveShape::Projectile; M.Radius = 70.f; M.Effect = EPYREffect::Confuse; }
            else if (Effect.Contains(TEXT("HEAL"))) { M.Shape = EPYRMoveShape::Self; M.Effect = EPYREffect::Heal; M.Accuracy = 0; }
            else { M.Shape = EPYRMoveShape::Self; M.Effect = EPYREffect::Splash; M.Accuracy = 0; }
        }
        ++Count;
    }
    return Count;
}

int32 ImportExp(const TSharedPtr<FJsonObject>& Json)
{
    if (!Json.IsValid()) return 0;
    int32 Count = 0;
    // {growth: [exp by level]} or {growth: {level: exp}}; the growth names as in species.json.
    const TSharedPtr<FJsonObject> Root = Obj(Json, TEXT("rates")) ? Obj(Json, TEXT("rates")) : Obj(Json, TEXT("growth_rates")) ? Obj(Json, TEXT("growth_rates")) : Json;
    for (const auto& Pair : Root->Values)
    {
        // Gen 1 uses four groups; the two slightly-fast and slightly-slow rates are unused.
        const FString Key(*Pair.Key);
        if (Key.StartsWith(TEXT("_")) || Key.Contains(TEXT("slightly"))) continue;
        const EPYRGrowth G = GrowthFrom(MakeShared<FJsonValueString>(Key));
        TArray<int32> ByLevel;
        if (Pair.Value->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>& A = Pair.Value->AsArray();
            // A list of 100 starts at level 1; 101 starts at level 0.
            if (A.Num() == 100) ByLevel.Add(0);
            for (const TSharedPtr<FJsonValue>& V : A) ByLevel.Add(AsInt(V, 0));
        }
        else if (const TSharedPtr<FJsonObject> O = AsObj(Pair.Value))
        {
            ByLevel.SetNumZeroed(101);
            for (const auto& L : O->Values) { const int32 Level = FCString::Atoi(*L.Key); if (Level >= 0 && Level <= 100) ByLevel[Level] = AsInt(L.Value, 0); }
        }
        if (ByLevel.Num() >= 101) { PYRData::SetXpTable(G, ByLevel); ++Count; }
    }
    return Count;
}

void ParsePikachu(const TSharedPtr<FJsonObject>& Json, FYPikachuTable& Out)
{
    if (!Json.IsValid()) return;
    Out.Start = Int(Json, TEXT("start_happiness"), Out.Start);
    Out.StartMood = Int(Json, TEXT("start_mood"), Out.StartMood);
    if (const TSharedPtr<FJsonObject> D = Obj(Json, TEXT("deltas")))
        for (const auto& Pair : D->Values)
        {
            const TSharedPtr<FJsonObject> E = AsObj(Pair.Value);
            if (!E.IsValid()) continue;
            FYPikachuTable::FDelta Delta;
            if (const TSharedPtr<FJsonObject> Bands = Obj(E, TEXT("delta_by_happiness")))
                for (const auto& B : Bands->Values)
                {
                    const int32 Low = FCString::Atoi(*B.Key);
                    const int32 V = AsInt(B.Value, 0);
                    if (Low < 100) Delta.Low = V; else if (Low < 200) Delta.Mid = V; else Delta.High = V;
                }
            Delta.MoodTarget = Int(E, TEXT("mood_target"), -1);
            Out.Deltas.Add(FString(*Pair.Key), Delta);
        }
    if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(Json, TEXT("face_table")))
        for (const TSharedPtr<FJsonValue>& R : *Rows)
            if (const TSharedPtr<FJsonObject> RO = AsObj(R))
            {
                FYPikachuTable::FFaceRow Row;
                Row.HappinessAtMost = Int(RO, TEXT("happiness_at_most"), 255);
                if (const TArray<TSharedPtr<FJsonValue>>* By = Arr(RO, TEXT("by_mood")))
                    for (const TSharedPtr<FJsonValue>& M : *By)
                        if (const TSharedPtr<FJsonObject> MO = AsObj(M))
                            Row.ByMood.Add(TPair<int32, FString>(Int(MO, TEXT("mood_at_most"), 255), Str(MO, TEXT("face"), FString())));
                Out.FaceTable.Add(Row);
            }
    if (const TSharedPtr<FJsonObject> Faces = Obj(Json, TEXT("faces")))
        for (const auto& Pair : Faces->Values)
            if (const TSharedPtr<FJsonObject> F = AsObj(Pair.Value)) Out.FaceFiles.Add(FString(*Pair.Key), Str(F, TEXT("file"), FString::Printf(TEXT("pikachu/%s.png"), *Pair.Key)));
}

void Import(const FYellowDataSet& Data)
{
    const int32 Moves = ImportMoves(Data.MovesJson);
    const int32 Species = ImportSpecies(Data.SpeciesJson);
    const int32 Exp = ImportExp(Data.ExpJson);
    UE_LOG(LogYellow, Display, TEXT("YellowTables: %d species, %d moves, %d growth tables imported"), Species, Moves, Exp);
}
}
