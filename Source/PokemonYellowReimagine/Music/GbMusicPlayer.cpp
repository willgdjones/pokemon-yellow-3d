#include "Music/GbMusicPlayer.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "World/PYRPaint.h"
#include "Yellow/YellowData.h"

// ------------------------------------------------------------------------------------------------ WAV
bool FGbPcm::FromWav(const TArray<uint8>& B, FGbPcm& Out, FString& Why)
{
    auto U32 = [&B](int32 At) { return uint32(B[At]) | (uint32(B[At + 1]) << 8) | (uint32(B[At + 2]) << 16) | (uint32(B[At + 3]) << 24); };
    auto U16 = [&B](int32 At) { return uint16(B[At] | (B[At + 1] << 8)); };
    if (B.Num() < 44 || FMemory::Memcmp(B.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(B.GetData() + 8, "WAVE", 4) != 0) { Why = TEXT("not a RIFF WAVE"); return false; }
    int32 At = 12, Channels = 1, Bits = 16, Format = 1;
    const uint8* Data = nullptr;
    int32 DataBytes = 0;
    while (At + 8 <= B.Num())
    {
        const uint32 Size = U32(At + 4);
        if (FMemory::Memcmp(B.GetData() + At, "fmt ", 4) == 0 && At + 24 <= B.Num())
        {
            Format = U16(At + 8);
            Channels = U16(At + 10);
            Out.SampleRate = int32(U32(At + 12));
            Bits = U16(At + 22);
        }
        else if (FMemory::Memcmp(B.GetData() + At, "data", 4) == 0)
        {
            Data = B.GetData() + At + 8;
            DataBytes = int32(FMath::Min<int64>(Size, B.Num() - (At + 8)));
            break;
        }
        At += 8 + int32(Size) + (Size & 1);
    }
    if (Format != 1 || Bits != 16) { Why = FString::Printf(TEXT("format %d, %d bits (want PCM 16)"), Format, Bits); return false; }
    if (!Data || Channels < 1) { Why = TEXT("no data chunk"); return false; }
    const int32 Frames = DataBytes / (2 * Channels);
    Out.Samples.SetNumUninitialized(Frames);
    const int16* In = reinterpret_cast<const int16*>(Data);
    for (int32 F = 0; F < Frames; ++F)
    {
        int32 Sum = 0;
        for (int32 C = 0; C < Channels; ++C) Sum += In[F * Channels + C];
        Out.Samples[F] = int16(Sum / Channels);
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ procedural wave
void UGbSoundWave::Setup(TSharedPtr<const FGbPcm, ESPMode::ThreadSafe> InPcm, bool bInLoop)
{
    Pcm = InPcm;
    bLoop = bInLoop;
    Cursor = 0;
    bFinished = false;
    NumChannels = 1;
    SetSampleRate(Pcm.IsValid() ? Pcm->SampleRate : 32000);
    Duration = bInLoop ? INDEFINITELY_LOOPING_DURATION : (Pcm.IsValid() ? Pcm->Seconds() : 1.f);
    bLooping = bInLoop;
    SoundGroup = SOUNDGROUP_Music;
    bCanProcessAsync = true;
}

int32 UGbSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
    OutAudio.SetNumZeroed(NumSamples * sizeof(int16));
    const FGbPcm* P = Pcm.Get();
    if (!P || P->Samples.Num() == 0) return NumSamples;
    int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
    const int32 End = P->LoopEnd > 0 ? FMath::Min(P->LoopEnd, P->Samples.Num()) : P->Samples.Num();
    const int32 Start = FMath::Clamp(P->LoopStart, 0, FMath::Max(0, End - 1));
    int32 C = Cursor.load();
    for (int32 I = 0; I < NumSamples; ++I)
    {
        if (C >= (bLoop ? End : P->Samples.Num()))
        {
            if (!bLoop) { bFinished = true; break; }
            C = Start;
        }
        Out[I] = P->Samples[C++];
    }
    Cursor = C;
    return NumSamples;
}

// ------------------------------------------------------------------------------------------------ player
UGbMusicPlayer* UGbMusicPlayer::Get(const UObject* Ctx)
{
    UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UGbMusicPlayer>() : nullptr;
}

double UGbMusicPlayer::Now() const
{
    return FPlatformTime::Seconds();
}

void UGbMusicPlayer::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Root = UYellowData::MusicRoot();
    const FString Manifest = FPaths::Combine(Root, TEXT("music.json"));
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Manifest))
    {
        bFallback = true;
        UE_LOG(LogYellow, Warning, TEXT("YellowMusic missing: %s (the folk tracks in /Game/Audio/Music stand in)"), *Manifest);
        return;
    }
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
    {
        bFallback = true;
        UE_LOG(LogYellow, Error, TEXT("YellowMusic missing: %s (not valid JSON)"), *Manifest);
        return;
    }
    const TSharedPtr<FJsonObject> Songs = Json->HasTypedField<EJson::Object>(TEXT("songs")) ? Json->GetObjectField(TEXT("songs")) : Json;
    for (const auto& Pair : Songs->Values)
    {
        const TSharedPtr<FJsonObject> O = Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
        if (!O.IsValid()) continue;
        FTrack T;
        T.File = O->GetStringField(TEXT("file"));
        double V = 0;
        if (O->TryGetNumberField(TEXT("loop_start"), V)) T.LoopStart = int32(V);
        if (O->TryGetNumberField(TEXT("loop_end"), V)) T.LoopEnd = int32(V);
        bool bJ = false;
        if (O->TryGetBoolField(TEXT("jingle"), bJ)) T.bJingle = bJ;
        Tracks.Add(FName(*FString(*Pair.Key).ToLower()), T);
    }
    UE_LOG(LogYellow, Display, TEXT("YellowMusic: %d tracks from %s"), Tracks.Num(), *Manifest);
}

void UGbMusicPlayer::Deinitialize()
{
    for (FVoice& V : Voices) if (UAudioComponent* C = V.Comp.Get()) C->Stop();
    Voices.Reset();
    if (UAudioComponent* J = JingleComp.Get()) J->Stop();
    Keep.Reset();
    Super::Deinitialize();
}

TSharedPtr<const FGbPcm, ESPMode::ThreadSafe> UGbMusicPlayer::Load(FName Id)
{
    if (const TSharedPtr<const FGbPcm, ESPMode::ThreadSafe>* Found = Cache.Find(Id)) { CacheOrder.Remove(Id); CacheOrder.Add(Id); return *Found; }
    const FTrack* T = Tracks.Find(Id);
    if (!T) return nullptr;
    const FString Path = FPaths::Combine(Root, T->File);
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { LastError = FString::Printf(TEXT("YellowMusic missing: %s"), *Path); UE_LOG(LogYellow, Error, TEXT("%s"), *LastError); return nullptr; }
    TSharedPtr<FGbPcm, ESPMode::ThreadSafe> Pcm = MakeShared<FGbPcm, ESPMode::ThreadSafe>();
    FString Why;
    if (!FGbPcm::FromWav(Bytes, *Pcm, Why)) { LastError = FString::Printf(TEXT("YellowMusic missing: %s (%s)"), *Path, *Why); UE_LOG(LogYellow, Error, TEXT("%s"), *LastError); return nullptr; }
    Pcm->LoopStart = T->LoopStart;
    Pcm->LoopEnd = T->LoopEnd;
    Pcm->bJingle = T->bJingle;
    Cache.Add(Id, Pcm);
    CacheOrder.Add(Id);
    // Keep a few songs decoded (a minute of 32 kHz mono is about 4 MB).
    while (CacheOrder.Num() > 8) { Cache.Remove(CacheOrder[0]); CacheOrder.RemoveAt(0); }
    return Pcm;
}

USoundBase* UGbMusicPlayer::FallbackSound(FName Id) const
{
    const FString S = Id.ToString();
    const TCHAR* Asset = TEXT("S_route_1");
    if (S == TEXT("pallet_town")) Asset = TEXT("S_pallet_town");
    else if (S == TEXT("oaks_lab") || S == TEXT("meet_prof_oak")) Asset = TEXT("S_oaks_lab");
    else if (S.StartsWith(TEXT("routes"))) Asset = TEXT("S_route_1");
    else if (S == TEXT("cities2")) Asset = TEXT("S_cerulean_city");
    else if (S.StartsWith(TEXT("cities")) || S == TEXT("museum_guy") || S == TEXT("gym")) Asset = TEXT("S_viridian_city");
    else if (S == TEXT("pokecenter")) Asset = TEXT("S_pokemon_centre");
    else if (S == TEXT("dungeon3")) Asset = TEXT("S_mt_moon");
    else if (S.StartsWith(TEXT("dungeon"))) Asset = TEXT("S_viridian_forest");
    else if (S == TEXT("wild_battle")) Asset = TEXT("S_wild_battle");
    else if (S.Contains(TEXT("battle"))) Asset = TEXT("S_gym_battle");
    else if (S == TEXT("meet_rival")) Asset = TEXT("S_rival_theme");
    else if (S.Contains(TEXT("jessie"))) Asset = TEXT("S_jessie_james");
    return Cast<USoundBase>(PYR::LoadOptional(USoundBase::StaticClass(), FString(TEXT("/Game/Audio/Music/")) + Asset));
}

UAudioComponent* UGbMusicPlayer::StartVoice(FName Id, bool bLoop, float& OutSeconds)
{
    UWorld* World = GetWorld();
    OutSeconds = 0.f;
    USoundBase* Sound = nullptr;
    if (!bFallback)
    {
        TSharedPtr<const FGbPcm, ESPMode::ThreadSafe> Pcm = Load(Id);
        if (!Pcm.IsValid()) return nullptr;
        UGbSoundWave* Wave = NewObject<UGbSoundWave>(this);
        Wave->Setup(Pcm, bLoop);
        Keep.Add(Wave);
        Sound = Wave;
        OutSeconds = Pcm->Seconds();
    }
    else
    {
        Sound = FallbackSound(Id);
        OutSeconds = Sound ? Sound->GetDuration() : 0.f;
        if (OutSeconds > 1000.f) OutSeconds = 3.f;
    }
    if (!Sound || !World) return nullptr;
    UAudioComponent* Comp = UGameplayStatics::CreateSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, true, false);
    if (!Comp) return nullptr;   // No audio device: the choice is still tracked.
    Comp->bIsUISound = true;     // Keeps playing while the game is paused.
    Comp->bAutoDestroy = false;
    Comp->Play();
    Keep.Add(Comp);
    return Comp;
}

bool UGbMusicPlayer::Play(FName Id, float Crossfade)
{
    Id = FName(*Id.ToString().ToLower());
    if (Id.IsNone()) return false;
    if (Id == Song) return true;
    if (!bFallback && !Tracks.Contains(Id)) { LastError = FString::Printf(TEXT("YellowMusic has no song %s"), *Id.ToString()); UE_LOG(LogYellow, Warning, TEXT("%s"), *LastError); return false; }
    for (FVoice& V : Voices) { V.bDying = true; V.Target = 0.f; V.Rate = 1.f / FMath::Max(.05f, Crossfade); }
    float Seconds = 0.f;
    FVoice V;
    V.Id = Id;
    V.Comp = StartVoice(Id, true, Seconds);
    V.Volume = Crossfade > 0.f ? 0.f : 1.f;
    V.Target = 1.f;
    V.Rate = 1.f / FMath::Max(.05f, Crossfade);
    if (UAudioComponent* C = V.Comp.Get()) { C->SetVolumeMultiplier(FMath::Max(.001f, V.Volume)); if (IsJinglePlaying()) C->SetPaused(true); }
    Voices.Add(V);
    Song = Id;
    Played.Add(Id);
    UE_LOG(LogYellow, Display, TEXT("GB_MUSIC %s%s"), *Id.ToString(), bFallback ? TEXT(" (folk fallback)") : TEXT(""));
    return true;
}

void UGbMusicPlayer::Stop(float Fade)
{
    for (FVoice& V : Voices) { V.bDying = true; V.Target = 0.f; V.Rate = 1.f / FMath::Max(.05f, Fade); }
    Song = NAME_None;
}

bool UGbMusicPlayer::Jingle(FName Id, TFunction<void()> OnDone)
{
    Id = FName(*Id.ToString().ToLower());
    if (!bFallback && !Tracks.Contains(Id)) { if (OnDone) OnDone(); return false; }
    // A jingle over a jingle replaces it (the song stays paused).
    if (UAudioComponent* Old = JingleComp.Get()) Old->Stop();
    for (FVoice& V : Voices) if (UAudioComponent* C = V.Comp.Get()) C->SetPaused(true);
    float Seconds = 0.f;
    JingleComp = bFallback ? nullptr : StartVoice(Id, false, Seconds);
    if (bFallback) Seconds = 1.5f;   // The folk tracks have no jingles: a short hush in the song instead.
    JingleId = Id;
    JingleEndsAt = Now() + FMath::Clamp(Seconds, .3f, 12.f);
    if (JingleDone) { TFunction<void()> Prev = MoveTemp(JingleDone); Prev(); }
    JingleDone = MoveTemp(OnDone);
    Jingles.Add(Id);
    UE_LOG(LogYellow, Display, TEXT("GB_JINGLE %s (%.1f s)"), *Id.ToString(), Seconds);
    return true;
}

void UGbMusicPlayer::Tick(float DeltaTime)
{
    const float Dt = FMath::Min(DeltaTime, .1f);
    if (IsJinglePlaying() && Now() >= JingleEndsAt)
    {
        if (UAudioComponent* J = JingleComp.Get()) J->Stop();
        JingleComp = nullptr;
        JingleId = NAME_None;
        for (FVoice& V : Voices) if (UAudioComponent* C = V.Comp.Get()) C->SetPaused(false);
        ++Resumes;
        UE_LOG(LogYellow, Display, TEXT("GB_MUSIC_RESUME %s"), *Song.ToString());
        if (JingleDone) { TFunction<void()> Done = MoveTemp(JingleDone); JingleDone = nullptr; Done(); }
    }
    for (int32 I = Voices.Num() - 1; I >= 0; --I)
    {
        FVoice& V = Voices[I];
        V.Volume = FMath::FInterpConstantTo(V.Volume, V.Target, Dt, V.Rate);
        UAudioComponent* C = V.Comp.Get();
        if (C) C->SetVolumeMultiplier(FMath::Max(.001f, V.Volume));
        if (V.bDying && V.Volume <= .001f)
        {
            if (C) { C->Stop(); Keep.Remove(C); if (USoundBase* S = C->Sound) Keep.Remove(S); C->DestroyComponent(); }
            Voices.RemoveAt(I);
        }
    }
}

void UGbMusicPlayer::PlaySfx(const UObject* Ctx, const TCHAR* Name)
{
    UGbMusicPlayer* Self = Get(Ctx);
    if (!Self) return;
    const FName Id(*FString::Printf(TEXT("sfx_%s"), Name));
    if (!Self->bFallback && Self->Tracks.Contains(Id))
    {
        float Seconds = 0.f;
        if (UAudioComponent* C = Self->StartVoice(Id, false, Seconds)) C->bAutoDestroy = true;
        return;
    }
    // Stand-ins from the m1-m3 effects.
    const FString N = Name;
    const TCHAR* Cue = N.Contains(TEXT("press")) || N.Contains(TEXT("start")) ? TEXT("S_cooldown_ready")
        : N.Contains(TEXT("heal")) ? TEXT("S_potion_spray") : N.Contains(TEXT("ball")) ? TEXT("S_ball_wobble")
        : N.Contains(TEXT("ledge")) ? TEXT("S_ledge_hop") : N.Contains(TEXT("spot")) ? TEXT("S_trainer_spot") : nullptr;
    if (Cue) PYR::Sfx(Ctx, Cue);
}
