// Emberwild Vale — orchestrator implementation.

#include "EmberwildGame.h"
#include "EmberwildUI.h"
#include "Widgets/SCompoundWidget.h"
#include "Engine/GameViewportClient.h"
#include "EmberwildDB.h"
#include "EmberwildWorld.h"
#include "EmberwildUI.h"
#include "EmberwildAudio.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UEmberwildSubsystem* Emberwild()
{
	if (GEngine)
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			if (UGameInstance* GI = Ctx.OwningGameInstance)
				if (auto* Sub = GI->GetSubsystem<UEmberwildSubsystem>())
					return Sub;
	return nullptr;
}

void UEmberwildSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Content tables: safe to build here (pure data, no world needed).
	FEmberwildDB::Init();
	NextAutosave = 30.f;
}

void UEmberwildSubsystem::BootWorld()
{
	UWorld* W = GetWorld();
	if (!W) return;

	// ---- load or fresh state ----
	if (!FEmberwildSave::Load(Save))
	{
		Save = FGameStateSave();
		GiveStarter();
		FEmberwildToast::Push(TEXT("Welcome to Emberwild Vale!"));
	}

	CurrentZone = Save.CurZone;

	// ---- world ----
	// (the GameMode's StartPlay already spawned the default pawn)
	Player = Cast<AEmberwildPlayer>(UGameplayStatics::GetPlayerPawn(W, 0));
	World = W->SpawnActor<AEmberwildWorldBuilder>();
	World->Build(Player);

	// place the player on the saved spot (game z maps to UE -y)
	const float Y = AEmberwildWorldBuilder::SurfaceHeight(Save.PlayerX, Save.PlayerZ);
	Player->SetActorLocation(FVector(Save.PlayerX, -Save.PlayerZ, Y + 1.f));

	// spawn player camera rig handled by pawn BeginPlay; build world content:
	// (nodes/stations/npcs are spawned by the builder's helper functions via
	// the static trampoline below)
	{
		int32 NodeIdx = 0;
		EmberwildSpawnNode(World, TEXT("bush_meadowgrass"), FVector2D(5, -25.5f), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_meadowgrass"), FVector2D(9.5f, -25.5f), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_meadowgrass"), FVector2D(12, -25.5f), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_pebble"), FVector2D(2, -18), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_pebble"), FVector2D(14, -15), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("spot_creek"), FVector2D(-2, -26), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_bitterleaf"), FVector2D(-9, -25.5f), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_bitterleaf"), FVector2D(-14, 6), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_bitterleaf"), FVector2D(-6, 22), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_fiber"), FVector2D(16, 4), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_fiber"), FVector2D(-20, -10), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_fiber"), FVector2D(22, -18), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("bush_emberpepper"), FVector2D(30, 26), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("stump_glowcap"), FVector2D(12, 66), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("stump_glowcap"), FVector2D(-8, 80), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_stone"), FVector2D(24, 34), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_stone"), FVector2D(-26, 28), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_copper"), FVector2D(62, 4), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_copper"), FVector2D(74, 26), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_iron"), FVector2D(84, 18), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("rock_moon"), FVector2D(55, 82), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("spot_river"), FVector2D(-30, -40), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("spot_river"), FVector2D(-2, -46), NodeIdx++);
		EmberwildSpawnNode(World, TEXT("spot_lake"), FVector2D(-8, -62), NodeIdx++);

		EmberwildSpawnStation(World, TEXT("forge"), TEXT("Brann's Forge"), FVector2D(-16.4f, -12.2f));
		EmberwildSpawnStation(World, TEXT("campfire"), TEXT("Emberpost Hearth"), FVector2D(4.8f, -8.2f));
		EmberwildSpawnStation(World, TEXT("workbench"), TEXT("Post Workbench"), FVector2D(16.4f, -12.2f));

		EmberwildSpawnShop(World, TEXT("general"), TEXT("Vale Post Traders"), FVector2D(-8.2f, -16.8f));
		EmberwildSpawnSlots(World, FVector2D(-11.8f, -14.2f));
		EmberwildSpawnSlots(World, FVector2D(0.f, -7.37f));

		EmberwildSpawnTarget(World, TEXT("sprigling"), 3, FVector2D(30.f, -24.f));
		EmberwildSpawnTarget(World, TEXT("thornpaw"), 4, FVector2D(34.f, -18.f));
		EmberwildSpawnTarget(World, TEXT("burrowmouse"), 4, FVector2D(-30.f, 8.f));
		EmberwildSpawnTarget(World, TEXT("flitwing"), 5, FVector2D(18.f, 24.f));
		EmberwildSpawnTarget(World, TEXT("puddlepup"), 6, FVector2D(-24.f, -32.f));
		EmberwildSpawnTarget(World, TEXT("cindercub"), 9, FVector2D(56.f, 6.f));
		EmberwildSpawnTarget(World, TEXT("magmite"), 10, FVector2D(78.f, 34.f));
		EmberwildSpawnTarget(World, TEXT("pyrelynx"), 12, FVector2D(88.f, 8.f));
		EmberwildSpawnTarget(World, TEXT("glimmfish"), 7, FVector2D(-30.f, -58.f));
		EmberwildSpawnTarget(World, TEXT("lilypad"), 9, FVector2D(8.f, -66.f));
		EmberwildSpawnTarget(World, TEXT("gloomcap"), 12, FVector2D(0.f, 70.f));
		EmberwildSpawnTarget(World, TEXT("frostwisp"), 15, FVector2D(24.f, 84.f));
		EmberwildSpawnTarget(World, TEXT("dreamora"), 16, FVector2D(40.f, 70.f));

		EmberwildSpawnNpc(World, TEXT("maren"),  FVector2D(7.5f, -14.6f));
		EmberwildSpawnNpc(World, TEXT("brann"),  FVector2D(-13.6f, -10.8f));
		EmberwildSpawnNpc(World, TEXT("juna"),   FVector2D(-5.2f, -13.4f));
		EmberwildSpawnNpc(World, TEXT("odessa"), FVector2D(2.4f, 4.6f));
		EmberwildSpawnNpc(World, TEXT("pell"),   FVector2D(-18.5f, -23.5f));
		EmberwildSpawnNpc(World, TEXT("mira"),   FVector2D(-4.9f, -7.0f));
		EmberwildSpawnNpc(World, TEXT("tobin"),  FVector2D(26.f, -26.f));
		EmberwildSpawnNpc(World, TEXT("sela"),   FVector2D(58, 30));
		EmberwildSpawnNpc(World, TEXT("korrin"), FVector2D(70, 10));
		EmberwildSpawnNpc(World, TEXT("vesna"),  FVector2D(-12, -58));
		EmberwildSpawnNpc(World, TEXT("hollis"), FVector2D(6, -60));
		EmberwildSpawnNpc(World, TEXT("zephy"),  FVector2D(14, 66));
	}

	// ---- UI: pure-Slate router added straight to the viewport ----
	UIRouter = SNew(SEmberwildUIRouter);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(UIRouter.ToSharedRef(), 10);
	}
	UIRouter->Boot(this);

// headless playtest hook (compile-gated in Build.cs)
EmberwildStartAutopilot(this);
}

void UEmberwildSubsystem::GiveStarter()
{
	FCreature Starter = FMonsterFactory::Create(TEXT("sprigling"), 5);
	if (Starter.Species()) Save.Party.Add(MoveTemp(Starter));
}

float UEmberwildSubsystem::PlaySeconds() const
{
	return (float)FPlatformTime::Seconds();
}

void UEmberwildSubsystem::MarkInteractCooldown(float Seconds)
{
	InteractCooldownUntil = PlaySeconds() + Seconds;
}

bool UEmberwildSubsystem::InteractAllowed() const
{
	return PlaySeconds() >= InteractCooldownUntil;
}

void UEmberwildSubsystem::StartNewGame()
{
	FEmberwildSave::DeleteSave();
	Save = FGameStateSave();
	GiveStarter();
	FEmberwildToast::Clear();
	FEmberwildToast::Push(TEXT("A new story begins in the Vale."));

	// respawn the player at the plaza
	if (Player && World)
	{
		const float Y = AEmberwildWorldBuilder::SurfaceHeight(0.f, -16.f);
		Player->SetActorLocation(FVector(0.f, 16.f, Y + 1.f));
	}
	CurrentZone = TEXT("meadow");
	Save.CurZone = TEXT("meadow");
}

void UEmberwildSubsystem::SetZone(const FString& ZoneId)
{
	if (CurrentZone == ZoneId) return;
	CurrentZone = ZoneId;
	Save.CurZone = ZoneId;
	UE_LOG(LogTemp, Log, TEXT("[World] Entered zone: %s"), *ZoneId);
}

void UEmberwildSubsystem::RollEncounter(float Dt)
{
	// parity: Unity's RollEncounter checks UiRouter.I.UiOpen — gate on the
	// router's live state so wilds never open mid-dialogue/crafting/battle.
	if (UIRouter.IsValid() && !UIRouter->IsWorldInputAllowed()) return;
	if (EncounterCooldown > 0.f) { EncounterCooldown -= Dt; return; }

	if (CurrentZone == TEXT("hollow_deep")) return; // story area: no random fights

	// ~10s average between wilds while walking
	float Chance = Dt * 0.09f;
	if (Save.EquippedItem == TEXT("lure_charm")) Chance *= 1.25f;

	if (FMath::FRand() < Chance)
	{
		EncounterCooldown = 3.5f;
		FCreature Wild = FMonsterFactory::CreateWildForZone(CurrentZone, Save);
		if (Wild.Species()) StartWildBattle(Wild);
	}
}

void UEmberwildSubsystem::StartWildBattle(const FCreature& Wild)
{
	TArray<FCreature> PlayerTeam = Save.Party;
	TArray<FCreature> EnemyTeam;
	EnemyTeam.Add(Wild);

	Battle = MakeUnique<FBattleEngine>(MoveTemp(PlayerTeam), MoveTemp(EnemyTeam), nullptr);
	Battle->WithState(&Save);
	if (UIRouter.IsValid()) UIRouter->OpenBattle();
}

void UEmberwildSubsystem::StartTrainerBattle(const FTrainerDef& Trainer)
{
	TArray<FCreature> PlayerTeam = Save.Party;
	TArray<FCreature> EnemyTeam;
	for (const FString& Entry : Trainer.Team)
	{
		FCreature C = FMonsterFactory::FromTrainerEntry(Entry);
		if (C.Species()) EnemyTeam.Add(MoveTemp(C));
	}
	UE_LOG(LogTemp, Log, TEXT("[Battle] trainer '%s': %d/%d team members resolved"), *Trainer.Id, EnemyTeam.Num(), Trainer.Team.Num());
	if (EnemyTeam.Num() == 0) return;

	Battle = MakeUnique<FBattleEngine>(MoveTemp(PlayerTeam), MoveTemp(EnemyTeam), &Trainer);
	Battle->WithState(&Save);
	if (UIRouter.IsValid()) UIRouter->OpenBattle();
}

void UEmberwildSubsystem::FinishBattle(EBattleResult Result)
{
	if (!Battle) return;

	// sync the engine's mutated team back into the save FIRST. Parity note:
	// Unity's Creature is a reference type — engine and save share one object,
	// so ApplyRewards's XP lands on the battle-worn state. Here creatures are
	// value copies, so merging must happen before rewards touch XP/Level or
	// the sync below would wipe them back to the engine's stale copies.
	for (FCreature& EngineMon : Battle->PlayerTeam)
		for (FCreature& Saved : Save.Party)
			if (Saved.Uid == EngineMon.Uid)
			{
				Saved.CurHP = EngineMon.CurHP;
				Saved.XP = EngineMon.XP;
				Saved.Level = EngineMon.Level;
				Saved.SpeciesId = EngineMon.SpeciesId;
				Saved.Moves = EngineMon.Moves;
				Saved.MovePP = EngineMon.MovePP;
				Saved.Status = EngineMon.Status;
				Saved.StatusTurns = EngineMon.StatusTurns;
				break;
			}

	if (Result == EBattleResult::PlayerWon || Result == EBattleResult::Caught)
		Battle->ApplyRewards(Save);

	Battle.Reset();
	EncounterCooldown = 6.f;
	// parity (UiRouter.cs:191): defeat objectives complete when the battle closes
	FQuestSystem::AutoCompleteAll(Save);
	FEmberwildSave::Save(Save);
}

void UEmberwildSubsystem::NotifyPlayerDefeat()
{
	// heal and respawn at the ranch (deferred: UI triggers after closing)
	Save.HealAll();
	for (const FWeaponMastery::FTechnique& T : FWeaponMastery::Techniques())
		FWeaponMastery::RestoreTechPP(Save, T.MoveId);
	Save.PlayerX = 0.f;
	Save.PlayerZ = -16.f;
	if (Player && World)
	{
		const float Y = AEmberwildWorldBuilder::SurfaceHeight(0.f, -16.f);
		Player->SetActorLocation(FVector(0.f, 16.f, Y + 1.f));
	}
	FEmberwildToast::Push(TEXT("You wake at the ranch, creatures tended and healed."));
}

void UEmberwildSubsystem::TalkTo(const FString& NpcId)
{
	const FNpcDef* Def = FEmberwildDB::Npc(NpcId);
	if (!Def) return;

	Dialogue = MakeUnique<FDialogueRuntime>(Save);
	if (UIRouter)
	{
		Dialogue->OnLineShown.AddLambda([this](const FDialogueLine& Line)
		{
			if (UIRouter.IsValid()) UIRouter->ShowDialogueLine(Line);
		});
		Dialogue->OnTrainerSpawn.AddLambda([this](const FTrainerDef& T)
		{
			if (Dialogue) Dialogue->End();
			StartTrainerBattle(T);
		});
		Dialogue->OnShopRequested.AddLambda([this](const FString& ShopId) { OpenShop(ShopId); });
		Dialogue->OnCraftRequested.AddLambda([this](const FString& StationId) { OpenCrafting(StationId); });
		Dialogue->OnEnded.AddLambda([this]()
		{
			FQuestSystem::AutoCompleteAll(Save);
			if (UIRouter.IsValid()) UIRouter->CloseDialogue();
		});
		// parity (UiRouter.cs:105): quests whose objectives are already met
		// complete as the talk opens
		FQuestSystem::AutoCompleteAll(Save);
		UIRouter->OpenDialogue();
	}
	Dialogue->Start(*Def);
}

void UEmberwildSubsystem::OpenCrafting(const FString& StationId)
{
	if (UIRouter.IsValid()) UIRouter->OpenCrafting(StationId);
}

void UEmberwildSubsystem::OpenShop(const FString& ShopId)
{
	if (UIRouter.IsValid()) UIRouter->OpenShop(ShopId);
}

void UEmberwildSubsystem::OpenSlots()
{
	if (UIRouter.IsValid()) UIRouter->OpenSlots();
}

void UEmberwildSubsystem::HandleInteract(AInteractableActor* Target, AEmberwildPlayer* PlayerInst)
{
	if (!Target || !InteractAllowed()) return;

	switch (Target->Kind)
	{
	case EInteractableKind::Npc:
		MarkInteractCooldown(0.3f);
		TalkTo(Target->RefId);
		break;
	case EInteractableKind::GatherNode:
	{
		const FNodeDef* Def = FEmberwildDB::Node(Target->RefId);
		if (!Def || Target->bDepleted) return;
		if (Save.ProfLevel(Def->Profession) < Def->RequiredLevel)
		{
			FEmberwildToast::Push(FString::Printf(TEXT("Requires %s level %d"),
				FProfessionSystem::Name(Def->Profession), Def->RequiredLevel));
			return;
		}
		int32 Xp = 0;
		const int32 Amount = FProfessionSystem::GatherYield(Save, Target->RefId, Xp);
		if (Amount <= 0) return;

		const FString Item = FEmberwildDB::RollNodeItem(*Def);
		Save.AddItem(Item, Amount);
		FProfessionSystem::AddXp(Save, Def->Profession, Xp);
		FEmberwildToast::Push(FString::Printf(TEXT("+%d %s  (+%d xp)"), Amount, *FEmberwildDB::ItemName(Item), Xp));
		if (auto* Sfx = EmberwildSfx::Get()) Sfx->PlayNamed(TEXT("gather"));

		Target->bDepleted = true;
		Target->RespawnAt = PlaySeconds() + (Def->Profession == EProfessionId::Forager ? 40.f
			: Def->Profession == EProfessionId::Miner ? 70.f : 55.f);
		Target->SetActorHiddenInGame(true);
		break;
	}
	case EInteractableKind::CraftStation:
		MarkInteractCooldown(0.3f);
		OpenCrafting(Target->RefId);
		break;
	case EInteractableKind::ShopStall:
		MarkInteractCooldown(0.3f);
		OpenShop(Target->RefId);
		break;
	case EInteractableKind::SlotsMachine:
		MarkInteractCooldown(0.3f);
		OpenSlots();
		break;
	case EInteractableKind::MeleeTarget:
		break; // handled by weapon swings, not E
	}
}

void UEmberwildSubsystem::SpawnSleeper()
{
	if (bSleeperSpawned || !World) return;
	if (!Save.DefeatedTrainers.Contains(TEXT("trainer_zephy"))) return;
	bSleeperSpawned = true;
	EmberwildSpawnNpc(World, TEXT("sleepers"), FVector2D(55.f, 82.f));
	FEmberwildToast::Push(TEXT("Something stirs in the deep Hollow..."));
}

void UEmberwildSubsystem::AwardMeleeKO(const FString& SpeciesId, int32 Level)
{
	Save.BattleWins++;
	const int32 Coins = 8 + Level * 2;
	Save.Coins += Coins;
	FEmberwildToast::Push(FString::Printf(TEXT("You picked up %d coins."), Coins));

	for (const FWildLootDrop& Drop : FEmberwildDB::RollWildLoot(SpeciesId, Level))
	{
		Save.AddItem(Drop.Id, Drop.N);
		FEmberwildToast::Push(FString::Printf(TEXT("+%d %s"), Drop.N, *FEmberwildDB::ItemName(Drop.Id)));
	}
	FEmberwildSave::Save(Save);
}

// ---------------- spawn helpers ----------------
void EmberwildSpawnNode(AEmberwildWorldBuilder* World, const FString& NodeId, const FVector2D& XZ, int32 Index)
{
	if (!World || !World->GetWorld()) return;
	if (!FEmberwildDB::Node(NodeId)) return;

	FActorSpawnParameters Params;
	AInteractableActor* A = World->GetWorld()->SpawnActor<AInteractableActor>(Params);
	A->Kind = EInteractableKind::GatherNode;
	A->RefId = NodeId;
	A->SpawnIndex = Index;
	A->SetActorLocation(FVector(XZ.X, -XZ.Y, AEmberwildWorldBuilder::GroundHeight(XZ.X, XZ.Y)));
	A->BuildVisual(nullptr);
}

void EmberwildSpawnStation(AEmberwildWorldBuilder* World, const FString& StationId, const FString& Label, const FVector2D& XZ)
{
	if (!World || !World->GetWorld()) return;
	AInteractableActor* A = World->GetWorld()->SpawnActor<AInteractableActor>();
	A->Kind = EInteractableKind::CraftStation;
	A->RefId = StationId;
	A->SetActorLocation(FVector(XZ.X, -XZ.Y, AEmberwildWorldBuilder::GroundHeight(XZ.X, XZ.Y)));
	A->BuildVisual(nullptr);
}

void EmberwildSpawnShop(AEmberwildWorldBuilder* World, const FString& ShopId, const FString& Label, const FVector2D& XZ)
{
	if (!World || !World->GetWorld()) return;
	AInteractableActor* A = World->GetWorld()->SpawnActor<AInteractableActor>();
	A->Kind = EInteractableKind::ShopStall;
	A->RefId = ShopId;
	A->SetActorLocation(FVector(XZ.X, -XZ.Y, AEmberwildWorldBuilder::GroundHeight(XZ.X, XZ.Y)));
	A->BuildVisual(nullptr);
}

void EmberwildSpawnSlots(AEmberwildWorldBuilder* World, const FVector2D& XZ)
{
	if (!World || !World->GetWorld()) return;
	AInteractableActor* A = World->GetWorld()->SpawnActor<AInteractableActor>();
	A->Kind = EInteractableKind::SlotsMachine;
	A->RefId = TEXT("slots");
	A->SetActorLocation(FVector(XZ.X, -XZ.Y, AEmberwildWorldBuilder::GroundHeight(XZ.X, XZ.Y)));
	A->BuildVisual(nullptr);
}

void EmberwildSpawnTarget(AEmberwildWorldBuilder* World, const FString& SpeciesId, int32 Level, const FVector2D& XZ)
{
	if (!World || !World->GetWorld()) return;
	if (!FEmberwildDB::SpeciesDef(SpeciesId)) return;

	FCreature C = FMonsterFactory::Create(SpeciesId, Level);
	if (!C.Species()) return;

	ACreatureActor* A = World->GetWorld()->SpawnActor<ACreatureActor>();
	A->SpeciesId = SpeciesId;
	A->Level = Level;
	A->HitPoints = FMath::Max(6, C.MaxHP() / 4); // overworld targets are softer than battle creatures
	A->bIsMeleeTarget = true;
	A->SetActorLocation(FVector(XZ.X, -XZ.Y, AEmberwildWorldBuilder::GroundHeight(XZ.X, XZ.Y)));
	A->BuildBody();

	if (auto* Sub = Emberwild()) Sub->MeleeTargets.Add(A);
}

void EmberwildSpawnNpc(AEmberwildWorldBuilder* World, const FString& NpcId, const FVector2D& XZ)
{
	if (!World || !World->GetWorld()) return;
	if (!FEmberwildDB::Npc(NpcId)) return;

	AInteractableActor* A = World->GetWorld()->SpawnActor<AInteractableActor>();
	A->Kind = EInteractableKind::Npc;
	A->RefId = NpcId;
	A->SetActorLocation(FVector(XZ.X, -XZ.Y, AEmberwildWorldBuilder::GroundHeight(XZ.X, XZ.Y)));
	A->BuildVisual(nullptr);
}
