// Audio cues (a reusable library piece). Replace POKEMONYELLOWREIMAGINE_API with your module's API macro.
//
// Named sound cues and music loops with crossfade, for the whole game instance (music keeps playing through
// OpenLevel). A generic audio cue player: per-cue volume, cooldown (rapid repeats are dropped, not stacked)
// and pitch variation; 2D cues for UI and player feedback, 3D cues at an actor with a room-scale attenuation;
// one persistent 2D component per music track, crossfaded; ducking. Missing sounds and machines without an
// audio device (-nosound, servers) stay silent and never fail.
//
//   auto* Audio = GetGameInstance()->GetSubsystem<UAudioCuesSubsystem>();
//   Audio->PlayCue(TEXT("pickup"), Hero);         // /Game/Audio/S_pickup unless registered otherwise
//   Audio->PlayMusic(TEXT("music"));              // loops if the sound wave is set to loop
//   Audio->PlayMusic(TEXT("boss_music"), 1.5f);   // crossfade
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AudioCuesSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;

UENUM(BlueprintType)
enum class EAudioCueResult : uint8
{
    Played,
    Suppressed,  // Inside the cue's cooldown.
    Missing,     // No such sound.
    NoDevice,    // Logic ran, but there is no audio device (-nosound, dedicated server).
};

USTRUCT(BlueprintType)
struct FAudioCue
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USoundBase> Sound = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Volume = 1.f;
    // Seconds (real time) before the same cue plays again.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Cooldown = .08f;
    // Random pitch in [1 - PitchVariance, 1 + PitchVariance]; 0 for signals that must sound the same every time.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PitchVariance = .06f;
    // UI and player feedback play in 2D; the rest at the actor passed to PlayCue.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool b2D = false;
};

UCLASS()
class POKEMONYELLOWREIMAGINE_API UAudioCuesSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    // Unregistered names load <CueFolder>/S_<Name>.S_<Name> with the default cue settings.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") FString CueFolder = TEXT("/Game/Audio");
    // 3D cues stay near full volume across a room and use distance mostly for direction (top-down cameras put
    // the listener far above the floor).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float AttenuationRadius = 1800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float FalloffDistance = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float MusicVolume = .9f;

    UFUNCTION(BlueprintCallable, Category = "Audio") void RegisterCue(FName Name, const FAudioCue& Cue);
    UFUNCTION(BlueprintCallable, Category = "Audio") USoundBase* FindSound(FName Name);
    UFUNCTION(BlueprintCallable, Category = "Audio") EAudioCueResult PlayCue(FName Name, AActor* At = nullptr);
    // Starts (or keeps) a music track; a different current track fades out over CrossfadeSeconds while this one
    // fades in. Asking for the playing track again un-ducks it and never restarts it.
    UFUNCTION(BlueprintCallable, Category = "Audio") EAudioCueResult PlayMusic(FName Name, float CrossfadeSeconds = 1.5f);
    UFUNCTION(BlueprintCallable, Category = "Audio") void StopMusic(float FadeSeconds = 1.f);
    // Lower the current track (death, victory, dialogue); PlayMusic of the same track restores it.
    UFUNCTION(BlueprintCallable, Category = "Audio") void DuckMusic(float Volume = .35f, float Seconds = .4f);

    UFUNCTION(BlueprintPure, Category = "Audio") FName GetMusic() const { return Current; }
    UFUNCTION(BlueprintPure, Category = "Audio") FName GetPreviousMusic() const { return Previous; }
    UAudioComponent* GetMusicComponent(FName Name) const;
    UAudioComponent* GetLastCueComponent() const { return LastComponent.Get(); }
    bool HasAudioDevice() const;
    int32 Played = 0, Suppressed = 0, MusicStarts = 0;

    virtual void Deinitialize() override;

private:
    UPROPERTY(Transient) TMap<FName, FAudioCue> Cues;
    UPROPERTY(Transient) TMap<FName, TObjectPtr<UAudioComponent>> Music;
    UPROPERTY(Transient) TObjectPtr<USoundAttenuation> Attenuation;
    TSet<FName> MissingNames;
    TMap<FName, double> LastPlayed;
    TWeakObjectPtr<UAudioComponent> LastComponent;
    FName Current = NAME_None, Previous = NAME_None;
    FRandomStream Random{0x5eed};
    USoundAttenuation* GetAttenuation();
};
