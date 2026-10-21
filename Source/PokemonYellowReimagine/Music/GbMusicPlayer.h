// M4: the remade Game Boy music (worker `gbmusic`'s YellowMusic/: 16-bit mono 32 kHz WAVs and music.json with loop
// points). Songs are read from disk, decoded once and played through a procedural sound wave that loops between
// the loop points exactly; a new song crossfades over 0.5 s; a jingle (heal, item get, level up, catch, evolution)
// pauses the song, plays once and the song resumes where it was. When YellowMusic is missing the folk tracks in
// /Game/Audio/Music stand in (a fixed map from Game Boy ids), so the game is never silent.
// The chosen song and jingle are tracked on the game thread, so tests see them without an audio device.
#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "Subsystems/WorldSubsystem.h"
#include <atomic>
#include "GbMusicPlayer.generated.h"

class UAudioComponent;
class USoundBase;

struct FGbPcm
{
    TArray<int16> Samples;
    int32 SampleRate = 32000;
    int32 LoopStart = 0, LoopEnd = 0;   // samples; LoopEnd 0 = the end
    bool bJingle = false;
    float Seconds() const { return SampleRate > 0 ? Samples.Num() / float(SampleRate) : 0.f; }
    // Parse a 16-bit PCM WAV (mono or stereo, mixed to mono). False with Why on a bad file.
    static bool FromWav(const TArray<uint8>& Bytes, FGbPcm& Out, FString& Why);
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UGbSoundWave : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    void Setup(TSharedPtr<const FGbPcm, ESPMode::ThreadSafe> InPcm, bool bInLoop);
    virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
    bool IsFinished() const { return bFinished.load(); }
private:
    TSharedPtr<const FGbPcm, ESPMode::ThreadSafe> Pcm;
    std::atomic<int32> Cursor{0};
    std::atomic<bool> bFinished{false};
    bool bLoop = true;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UGbMusicPlayer : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UGbMusicPlayer* Get(const UObject* Ctx);
    // A Game Boy sound effect (YellowMusic sfx_<Name>), else the nearest /Game/Audio/SFX cue.
    static void PlaySfx(const UObject* Ctx, const TCHAR* Name);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UGbMusicPlayer, STATGROUP_Tickables); }
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

    // Start a song by its Game Boy id (pallet_town, routes1, wild_battle...), crossfading from the current one.
    // Asking for the song already playing does nothing. False when the id is unknown (the old song keeps playing).
    bool Play(FName Id, float CrossfadeSeconds = .5f);
    void Stop(float FadeSeconds = .5f);
    // A jingle: the song pauses, the jingle plays once, the song resumes; OnDone runs after it.
    bool Jingle(FName Id, TFunction<void()> OnDone = nullptr);
    FName GetSong() const { return Song; }
    FName GetJingle() const { return JingleId; }
    bool IsJinglePlaying() const { return !JingleId.IsNone(); }
    // True when YellowMusic/music.json was read.
    bool HasYellowMusic() const { return Tracks.Num() > 0; }
    bool IsFallback() const { return bFallback; }
    bool Knows(FName Id) const { return Tracks.Contains(Id); }

    // Evidence for tests.
    TArray<FName> Played;       // songs started, in order
    TArray<FName> Jingles;      // jingles started
    int32 Resumes = 0;          // songs resumed after a jingle
    FString LastError;

private:
    struct FTrack { FString File; int32 LoopStart = 0, LoopEnd = 0; bool bJingle = false; };
    struct FVoice
    {
        FName Id;
        TWeakObjectPtr<UAudioComponent> Comp;
        float Volume = 0.f, Target = 1.f, Rate = 2.f;
        bool bDying = false;
    };
    TMap<FName, FTrack> Tracks;
    TMap<FName, TSharedPtr<const FGbPcm, ESPMode::ThreadSafe>> Cache;
    TArray<FName> CacheOrder;
    TArray<FVoice> Voices;
    UPROPERTY() TArray<TObjectPtr<UObject>> Keep;   // waves and components alive while playing
    FName Song, JingleId;
    TWeakObjectPtr<UAudioComponent> JingleComp;
    double JingleEndsAt = 0;
    TFunction<void()> JingleDone;
    bool bFallback = false;
    FString Root;

    TSharedPtr<const FGbPcm, ESPMode::ThreadSafe> Load(FName Id);
    UAudioComponent* StartVoice(FName Id, bool bLoop, float& OutSeconds);
    USoundBase* FallbackSound(FName Id) const;
    double Now() const;
};
