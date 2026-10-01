// Emberwild Vale — procedural audio.
// Port of the Unity original's Sfx.cs: every sound is synthesized at runtime
// from sine sweeps + noise envelopes — no assets, no network.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "EmberwildAudio.generated.h"

/** A tiny synthesized one-shot: PCM generated once, streamed from memory. */
UCLASS()
class USfxWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	TArray<int16> Pcm;
	int32 Cursor = 0;

	virtual int32 GeneratePCMData(uint8* PCMData, const int32 NumBytes) override;
};

/** Plays named one-shot effects from a cached wave bank. */
UCLASS(ClassGroup = (Emberwild), meta = (BlueprintSpawnableComponent))
class UEmberwildSfxComponent : public UAudioComponent
{
	GENERATED_BODY()

public:
	void PlayNamed(const FString& Id);
};

/** Static access: lazily attaches the singleton component to the game mode. */
struct EmberwildSfx
{
	static UEmberwildSfxComponent* Get();
};
