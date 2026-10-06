// Emberwild Vale — game orchestrator.
// Port of the Unity original's Game.cs + UiRouter wiring: owns the save state,
// dialogue runtime, battle engine, encounters, and routes interactions to UI.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EmberwildData.h"
#include "EmberwildState.h"
#include "EmberwildSystems.h"
#include "EmberwildGame.generated.h"

class AInteractableActor;
class AEmberwildPlayer;
class AEmberwildWorldBuilder;
class ACreatureActor;
class SEmberwildUIRouter;

/** Global access to the running session (like the original's Game.I). */
UEmberwildSubsystem* Emberwild();

UCLASS()
class UEmberwildSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---------------- state ----------------
	FGameStateSave Save;
	TUniquePtr<FDialogueRuntime> Dialogue;
	TUniquePtr<FBattleEngine> Battle;
	FString CurrentZone = TEXT("meadow");
	float EncounterCooldown = 0.f;
	float NextAutosave = 0.f;
	bool bSleeperSpawned = false;
	bool bUiOpen = false;

	/** Headless-playtest hook: the autopilot scripts its own battles, so it
	 *  silences random wilds (playtest-only — nothing sets this normally). */
	bool bSuppressEncounters = false;

	// world refs
	UPROPERTY() TObjectPtr<AEmberwildWorldBuilder> World;
	UPROPERTY() TObjectPtr<AEmberwildPlayer> Player;
	TSharedPtr<SEmberwildUIRouter> UIRouter;
	UPROPERTY() TArray<TObjectPtr<ACreatureActor>> MeleeTargets;

	// ---------------- boot / lifecycle ----------------
	void BootWorld();
	void StartNewGame();
	bool HasSave() const { return FEmberwildSave::SaveExists(); }

	// ---------------- zones / encounters ----------------
	void SetZone(const FString& ZoneId);
	void RollEncounter(float Dt);
	void StartWildBattle(const FCreature& Wild);
	void StartTrainerBattle(const FTrainerDef& Trainer);
	void FinishBattle(EBattleResult Result);
	void NotifyPlayerDefeat();

	// ---------------- routing ----------------
	void TalkTo(const FString& NpcId);
	void OpenCrafting(const FString& StationId);
	void OpenShop(const FString& ShopId);
	void OpenSlots();
	void HandleInteract(AInteractableActor* Target, AEmberwildPlayer* PlayerInst);
	void SpawnSleeper();

	// ---------------- helpers ----------------
	void GiveStarter();
	void MarkInteractCooldown(float Seconds);
	bool InteractAllowed() const;
	float PlaySeconds() const;

	/** Wild melee target KOs also drop loot + coins, like a battle win. */
	void AwardMeleeKO(const FString& SpeciesId, int32 Level);

private:
	float InteractCooldownUntil = 0.f;
	FString PendingDialogueNpc;
};

// ---------------- world spawn helpers (implemented in EmberwildWorld.cpp) ----------------
void EmberwildSpawnNode(AEmberwildWorldBuilder* World, const FString& NodeId, const FVector2D& XZ, int32 Index);
void EmberwildSpawnStation(AEmberwildWorldBuilder* World, const FString& StationId, const FString& Label, const FVector2D& XZ);
void EmberwildSpawnShop(AEmberwildWorldBuilder* World, const FString& ShopId, const FString& Label, const FVector2D& XZ);
void EmberwildSpawnSlots(AEmberwildWorldBuilder* World, const FVector2D& XZ);
void EmberwildSpawnTarget(AEmberwildWorldBuilder* World, const FString& SpeciesId, int32 Level, const FVector2D& XZ);
void EmberwildSpawnNpc(AEmberwildWorldBuilder* World, const FString& NpcId, const FVector2D& XZ);

/** Headless playtest autopilot (EmberwildAutopilot.cpp; no-op unless EMBERWILD_AUTOPILOT=1). */
void EmberwildStartAutopilot(UEmberwildSubsystem* Sub);
