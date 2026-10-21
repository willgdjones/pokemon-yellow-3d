#include "AudioCuesSubsystem.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogAudioCues, Log, All);

void UAudioCuesSubsystem::RegisterCue(FName Name, const FAudioCue& Cue)
{
    Cues.Add(Name, Cue);
    MissingNames.Remove(Name);
}

USoundBase* UAudioCuesSubsystem::FindSound(FName Name)
{
    if (const FAudioCue* Cue = Cues.Find(Name)) return Cue->Sound;
    if (Name.IsNone() || MissingNames.Contains(Name)) return nullptr;
    const FString Asset = TEXT("S_") + Name.ToString();
    const FString Path = FString::Printf(TEXT("%s/%s.%s"), *CueFolder, *Asset, *Asset);
    USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Sound)
    {
        MissingNames.Add(Name);  // Warn once per name, then stay silent.
        UE_LOG(LogAudioCues, Warning, TEXT("AUDIO_CUE_MISSING:%s"), *Path);
        return nullptr;
    }
    FAudioCue Cue;
    Cue.Sound = Sound;
    Cues.Add(Name, Cue);
    return Sound;
}

bool UAudioCuesSubsystem::HasAudioDevice() const
{
    const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    return World && World->GetAudioDeviceRaw() != nullptr;
}

USoundAttenuation* UAudioCuesSubsystem::GetAttenuation()
{
    if (!Attenuation)
    {
        Attenuation = NewObject<USoundAttenuation>(this);
        FSoundAttenuationSettings& A = Attenuation->Attenuation;
        A.bAttenuate = true;
        A.bSpatialize = true;
        A.AttenuationShape = EAttenuationShape::Sphere;
        A.AttenuationShapeExtents = FVector(AttenuationRadius, 0, 0);
        A.FalloffDistance = FalloffDistance;
        A.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
        A.NonSpatializedRadiusStart = 300.f;  // Sounds right at the listener play centred.
        A.NonSpatializedRadiusEnd = 150.f;
    }
    return Attenuation;
}

EAudioCueResult UAudioCuesSubsystem::PlayCue(FName Name, AActor* At)
{
    LastComponent.Reset();
    USoundBase* Sound = FindSound(Name);
    if (!Sound) return EAudioCueResult::Missing;
    const FAudioCue Cue = Cues.FindChecked(Name);
    const double Now = FPlatformTime::Seconds();
    double& Last = LastPlayed.FindOrAdd(Name, -1000.0);
    if (Now - Last < Cue.Cooldown) { ++Suppressed; return EAudioCueResult::Suppressed; }
    Last = Now;
    UWorld* World = At && At->GetWorld() ? At->GetWorld() : (GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
    if (!World || !World->GetAudioDeviceRaw()) return EAudioCueResult::NoDevice;
    const float Pitch = Cue.PitchVariance > 0 ? Random.FRandRange(1.f - Cue.PitchVariance, 1.f + Cue.PitchVariance) : 1.f;
    UAudioComponent* Component = Cue.b2D || !At
        ? UGameplayStatics::SpawnSound2D(World, Sound, Cue.Volume, Pitch)
        : UGameplayStatics::SpawnSoundAtLocation(World, Sound, At->GetActorLocation(), FRotator::ZeroRotator,
            Cue.Volume, Pitch, 0.f, GetAttenuation());
    if (!Component) return EAudioCueResult::NoDevice;
    LastComponent = Component;
    ++Played;
    return EAudioCueResult::Played;
}

EAudioCueResult UAudioCuesSubsystem::PlayMusic(FName Name, float CrossfadeSeconds)
{
    USoundBase* Sound = FindSound(Name);
    if (!Sound) return EAudioCueResult::Missing;
    UAudioComponent* Playing = Music.FindRef(Name);
    if (Name == Current)
    {
        if (Playing) Playing->AdjustVolume(.5f, MusicVolume);  // Undo a duck; never restart the track.
        return Playing ? EAudioCueResult::Played : EAudioCueResult::NoDevice;
    }
    UAudioComponent* Old = Music.FindRef(Current);
    const bool bCrossfade = !Current.IsNone();
    if (Old) Old->FadeOut(CrossfadeSeconds, 0.f);
    Previous = Current;
    Current = Name;
    ++MusicStarts;
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World || !World->GetAudioDeviceRaw()) return EAudioCueResult::NoDevice;
    if (!Playing)
    {
        // Persistent 2D component: owned by the audio device, not the level, so it plays on through OpenLevel.
        Playing = UGameplayStatics::CreateSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, true, false);
        if (!Playing) return EAudioCueResult::NoDevice;
        Music.Add(Name, Playing);
    }
    Playing->FadeIn(bCrossfade ? CrossfadeSeconds : .75f, MusicVolume);
    return EAudioCueResult::Played;
}

void UAudioCuesSubsystem::StopMusic(float FadeSeconds)
{
    if (UAudioComponent* Playing = Music.FindRef(Current)) Playing->FadeOut(FadeSeconds, 0.f);
    Previous = Current;
    Current = NAME_None;
}

void UAudioCuesSubsystem::DuckMusic(float Volume, float Seconds)
{
    if (UAudioComponent* Playing = Music.FindRef(Current)) Playing->AdjustVolume(Seconds, Volume);
}

UAudioComponent* UAudioCuesSubsystem::GetMusicComponent(FName Name) const { return Music.FindRef(Name); }

void UAudioCuesSubsystem::Deinitialize()
{
    for (auto& Pair : Music) if (Pair.Value) Pair.Value->Stop();
    Music.Empty();
    Super::Deinitialize();
}
