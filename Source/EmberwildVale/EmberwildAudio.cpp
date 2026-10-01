// Emberwild Vale — audio implementation.

#include "EmberwildAudio.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

namespace
{
	struct FSfxRecipe
	{
		float Freq0, Freq1;  // sine sweep
		float Dur;
		float Noise;         // 0..1 noise mix
		float Vol;
	};

	const FSfxRecipe& RecipeFor(const FString& Id)
	{
		static TMap<FString, FSfxRecipe> Map;
		if (Map.Num() == 0)
		{
			Map.Add(TEXT("gather"),  { 660.f,  880.f, 0.12f, 0.0f, 0.50f });
			Map.Add(TEXT("hit"),     { 220.f,  110.f, 0.10f, 0.6f, 0.70f });
			Map.Add(TEXT("faint"),   { 330.f,  110.f, 0.35f, 0.2f, 0.60f });
			Map.Add(TEXT("coin"),    { 880.f, 1320.f, 0.09f, 0.0f, 0.50f });
			Map.Add(TEXT("ui"),      { 520.f,  560.f, 0.05f, 0.0f, 0.35f });
			Map.Add(TEXT("levelup"), { 523.f, 1046.f, 0.40f, 0.0f, 0.60f });
			Map.Add(TEXT("evolve"),  { 440.f, 1760.f, 0.70f, 0.0f, 0.65f });
			Map.Add(TEXT("craft"),   { 200.f,  320.f, 0.15f, 0.4f, 0.60f });
			Map.Add(TEXT("catch"),   { 990.f,  660.f, 0.30f, 0.1f, 0.55f });
		}
		static FSfxRecipe Fallback = { 440.f, 440.f, 0.08f, 0.f, 0.4f };
		if (const FSfxRecipe* R = Map.Find(Id)) return *R;
		return Fallback;
	}

	USfxWave* GetOrMake(const FString& Id)
	{
		static TMap<FString, USfxWave*> Cache;
		if (USfxWave** Found = Cache.Find(Id)) return *Found;

		const FSfxRecipe R = RecipeFor(Id);
		const int32 SampleRate = 22050;
		const int32 NumSamples = FMath::Max(1, (int32)(R.Dur * SampleRate));

		USfxWave* Wave = NewObject<USfxWave>(GetTransientPackage());
		Wave->Pcm.SetNumUninitialized(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const float T = (float)i / SampleRate;
			const float Progress = (float)i / NumSamples;
			const float Freq = FMath::Lerp(R.Freq0, R.Freq1, Progress);
			float Sample = FMath::Sin(2.f * PI * Freq * T);
			if (R.Noise > 0.f)
				Sample = FMath::Lerp(Sample, FMath::FRand() * 2.f - 1.f, R.Noise);
			const float Env = FMath::Pow(1.f - Progress, 1.6f); // percussive envelope
			Wave->Pcm[i] = (int16)FMath::Clamp(Sample * Env * R.Vol * 32767.f, -32768.f, 32767.f);
		}
		Wave->Cursor = 0;
		Wave->SetSampleRate((uint32)SampleRate);
		Wave->SetNumFrames((uint32)NumSamples);
		Wave->Duration = R.Dur;
		Wave->NumChannels = 1;

		Cache.Add(Id, Wave);
		return Wave;
	}
}

int32 USfxWave::GeneratePCMData(uint8* PCMData, const int32 NumBytes)
{
	// one-shot exhausted: report 0 so the mixer ends the voice (never return a
	// negative-sized tail — that once memzero'd a wrapped-huge length and
	// stalled the audio render thread)
	if (Cursor < 0 || Cursor >= Pcm.Num()) return 0;

	const int32 SamplesAvail = Pcm.Num() - Cursor;
	const int32 SamplesToWrite = FMath::Min(NumBytes / 2, SamplesAvail);
	if (SamplesToWrite > 0)
		FMemory::Memcpy(PCMData, Pcm.GetData() + Cursor, SamplesToWrite * 2);
	// zero-fill the tail so partial buffers end in silence instead of garbage
	if (const int32 Tail = NumBytes - SamplesToWrite * 2)
		FMemory::Memzero(PCMData + SamplesToWrite * 2, Tail);
	Cursor += SamplesToWrite;
	return SamplesToWrite * 2;
}

void UEmberwildSfxComponent::PlayNamed(const FString& Id)
{
	if (USfxWave* Wave = GetOrMake(Id))
	{
		Wave->Cursor = 0; // replays start from the top (shared cursor is per-wave)
		SetSound(Wave);
		Play();
	}
}

UEmberwildSfxComponent* EmberwildSfx::Get()
{
	if (GEngine)
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())				if (UWorld* W = Ctx.World())
					if (AGameModeBase* Gov = W->GetAuthGameMode())
				{
					UEmberwildSfxComponent* Comp = Gov->FindComponentByClass<UEmberwildSfxComponent>();
					if (!Comp)
					{
						Comp = NewObject<UEmberwildSfxComponent>(Gov);
						Comp->bAutoActivate = false;
						Gov->AddInstanceComponent(Comp);
						Comp->RegisterComponent();
					}
					return Comp;
				}
	return nullptr;
}
