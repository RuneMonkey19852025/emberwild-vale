// Emberwild Vale — world layer.
// Procedural port of the Unity original's WorldBuilder / PlayerController /
// Interactables / CreatureVisual / MeleeTarget: heightfield terrain, the
// Emberpost town, zone volumes, gather nodes, stations, NPCs, wild creatures.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Camera/CameraComponent.h"
#include "EmberwildData.h"
#include "EmberwildState.h"
#include "EmberwildWorld.generated.h"

class UProceduralMeshComponent;
class ACreatureActor;
class AEmberwildPlayer;

// ---------------------------------------------------------------- world builder
UCLASS()
class AEmberwildWorldBuilder : public AActor
{
	GENERATED_BODY()

public:
	AEmberwildWorldBuilder();

	/** Pure heightfield: identical formulas to the Unity GroundHeight. */
	static float GroundHeight(float X, float Z);

	/** Walkable surface (terrain + bridge deck). */
	static float SurfaceHeight(float X, float Z);
	static bool InTownPad(float X, float Z, float Pad = 0.f);

	void Build(AEmberwildPlayer* Player);

	// zone centers (xz), y = nominal ground height there
	static const TMap<FString, FVector2D>& ZoneCenters();

protected:
	virtual void Tick(float Dt) override;

private:
	void BuildTerrain();
	void BuildWater();
	void BuildBridge();
	void BuildGrass();
	void BuildTown();
	void BuildProps();
	void BuildPlaza();
	void BuildHouse(const FString& Name, FVector2D XZ, float RotDeg, const TCHAR* WallHex, const TCHAR* RoofHex);
	void BuildBellTower(FVector2D XZ);
	void BuildFenceRow(const FVector& From, const FVector& To, int32 Posts);
	void SpawnLantern(const FString& Name, FVector2D XZ);
	void BuildWell(const FVector& At);
	void BuildPathStones(const FVector& From, const FVector& To);
	void ScatterTrees(const TCHAR* Zone, int32 Count, FVector2D XRange, FVector2D ZRange,
		bool bTownPad = true, std::initializer_list<const char*> Models = {});
	void ScatterRocks(const TCHAR* Zone, int32 Count, FVector2D XRange, FVector2D ZRange, bool bTownPad = true);
	void ScatterProps(const TCHAR* Zone, int32 Count, FVector2D XRange, FVector2D ZRange,
		bool bTownPad, std::initializer_list<const char*> Models);

	UPROPERTY() TArray<TObjectPtr<AActor>> Spawned;

	// sway animation state for tree crowns + forge flames
	struct FSWayEntry { TWeakObjectPtr<USceneComponent> Comp; float Phase; bool bFlame; };
	TArray<FSWayEntry> SwayEntries;
	float Elapsed = 0.f;

public:
	// ---- Emberpost town pad (must match BuildTown) ----
	static constexpr float TownCX = 0.f, TownCZ = -12.f, TownHalfW = 34.f, TownHalfH = 26.f, TownFeather = 6.f, TownY = 1.6f;

	// ---- bridge deck constants (must match BuildBridge) ----
	static constexpr float BridgeX0 = 38.f, BridgeX1 = 52.f, BridgeZ = 15.f, BridgeHalfWidth = 2.45f, BridgeArch = 1.5f;

	/** Shared helpers used by actors: toon material factory (hex string). */
	static UMaterialInstanceDynamic* ToonMID(UObject* Outer, const FString& Hex);
	static UMaterialInterface* ToonBase();
	static FLinearColor HexColor(const FString& Hex);
};

// ---------------------------------------------------------------- interactables
UENUM()
enum class EInteractableKind : uint8
{
	Npc, GatherNode, CraftStation, ShopStall, SlotsMachine, MeleeTarget
};

UCLASS()
class AInteractableActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractableActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite) EInteractableKind Kind = EInteractableKind::Npc;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString RefId;   // npcId / nodeId / stationId / shopId / speciesId
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SpawnLevel = 3; // melee targets
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SpawnIndex = 0; // nodes: unique index for respawn bookkeeping

	/** Shown in the interact prompt. */
	FString Prompt() const;
	void Interact(AEmberwildPlayer* Player);

	/** Build this interactable's procedural visuals. */
	void BuildVisual(AEmberwildWorldBuilder* World);

	// gather-node state
	bool bDepleted = false;
	float RespawnAt = -1.f;

protected:
	virtual void Tick(float Dt) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> VisualRoot;
	void BuildNpcVisual(const FNpcDef& Def);
	void BuildNodeVisual(const FNodeDef& Def);
	void BuildStationVisual(const FString& StationId);
	void BuildShopVisual();
	void BuildSlotsVisual();
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> VisualParts;
};

// ---------------------------------------------------------------- creatures (wild targets & follower)
UCLASS()
class ACreatureActor : public AActor
{
	GENERATED_BODY()

public:
	ACreatureActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SpeciesId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIsMeleeTarget = true; // false = battle-spawned body
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bShiny = false;
	UPROPERTY(BlueprintReadOnly) int32 HitPoints = 20;

	static ACreatureActor* Spawn(const FCreature& Creature, FVector At, bool bAsMeleeTarget);
	void BuildBody();

	/** Called by the player's melee swing. Returns damage dealt (0 = out of range handled elsewhere). */
	int32 TakeHit(int32 Damage);
	bool bDefeated = false;

protected:
	virtual void Tick(float Dt) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> BodyRoot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	float BaseZ = 0.f;
	float BobPhase = 0.f;
};

// ---------------------------------------------------------------- player pawn
UCLASS()
class AEmberwildPlayer : public APawn
{
	GENERATED_BODY()

public:
	AEmberwildPlayer();

	UPROPERTY(BlueprintReadOnly) TObjectPtr<USpringArmComponent> SpringArm;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;

	/** Pending creature gifted by dialogue (spawned follower after load). */
	UPROPERTY() TObjectPtr<ACreatureActor> Follower;

	void FaceDirection(const FVector& Dir);

	// melee state
	float SwingTimer = -1.f;  // >= 0 while swinging
	float CooldownTimer = 0.f;
	UPROPERTY() TObjectPtr<USceneComponent> WeaponPivot;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void OnMoveForward(float V);
	void OnMoveRight(float V);
	void OnZoomAxis(float V);
	void OnInteract();
	void OnSwing();
	void OnBag();
	void OnGear();
	void OnJournal();
	void OnCreatures();
	void OnWeapons();
	void OnBack();
	void OnUIUp();
	void OnUIDown();
	void OnUIEnter();
	void OnUIBackspace();
	void OnUIY();
	void OnUIN();
	void OnUIB();
	void OnUIF5();
	void OnUIKey(FKey Key);

	void MoveAxis(const FVector2D& Axis);
	void Zoom(float Delta);
	void InteractPressed();
	void SwingPressed();
	void ToggleBag();
	void ToggleGear();
	void ToggleJournal();
	void ToggleCreatures();
	void ToggleWeapons();
	void CloseTop();
	void BuildBody();

public:
	/** Spring the camera target to another actor (battle focus) — unused for now, kept for parity. */
	float InteractRange = 3.4f;

private:
	UPROPERTY() TObjectPtr<UFloatingPawnMovement> Movement;
	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector2D AccumMove = FVector2D::ZeroVector;
	float TargetArm = 16.f;
	float SwingCooldown = 0.f;
	bool BodyBuilt = false;

public:
	/** False while a menu/battle/dialogue panel is up: world input suppressed. */
	bool bWorldInputAllowed = true;
};

// ---------------------------------------------------------------- game mode
UCLASS()
class AEmberwildGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEmberwildGameMode();

	static AEmberwildGameMode* Get(const UObject* WorldContext);

protected:
	virtual void StartPlay() override;
	virtual void Tick(float Dt) override;
};
