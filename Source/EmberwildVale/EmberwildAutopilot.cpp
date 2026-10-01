// Emberwild Vale — headless playtest autopilot (compile-gated).
//
// Drives the FIRST TWO QUEST LOOPS through the exact same entry points a
// player's keystrokes reach — SEmberwildUIRouter::HandlePanelKey, the pawn's
// InteractPressed core, AInteractableActor::Interact, the subsystem's battle
// entries — and logs [PASS]/[FAIL] per step so a `-game -nullrhi` run
// validates the game without a renderer or human input.
//
// Quest 1 (q_firstcore):  title -> Brann -> gather fiber/stone -> forge:
//                         rope -> Timber Core -> quest completes.
// Quest 2 (q_firstcatch): buy cores -> Maren offers the quest -> weaken a
//                         wild, throw cores until it seals -> collect.
// Grind:                  wild battles for XP (the XP curve needs ~4 wins/level).
// Then:                   Tobin's trainer battle (q_tobin), with rematches.
//
// Battles use dice (catch rolls, damage variance), so each battle stage arms
// the driver with a retry budget instead of assuming a first-try win.
//
// Enable with `PublicDefinitions.Add("EMBERWILD_AUTOPILOT=1")` in Build.cs.
// Remove the definition (this TU compiles to nothing) once the port is verified.

#include "EmberwildGame.h"
#include "EmberwildUI.h"
#include "EmberwildDB.h"
#include "EmberwildWorld.h"
#include "EmberwildState.h"
#include "EmberwildSystems.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Misc/OutputDevice.h"

#ifdef EMBERWILD_AUTOPILOT

namespace
{
	// ------------------------------------------------------------- logging
	void LogP(const FString& Msg)
	{
		UE_LOG(LogTemp, Display, TEXT("[AUTOPILOT] %s"), *Msg);
		if (GLog) GLog->Log(TEXT("[AUTOPILOT] " + Msg));
	}
	void LogStep(bool bPass, const FString& Msg)
	{
		LogP(FString::Printf(TEXT("%s %s"), bPass ? TEXT("[PASS]") : TEXT("[FAIL]"), *Msg));
	}
	void LogInv(UEmberwildSubsystem* Sub)
	{
		TArray<FString> Parts;
		for (const FItemStack& It : Sub->Save.Inventory)
			Parts.Add(FString::Printf(TEXT("%sx%d"), *It.ItemId, It.Count));
		LogP(FString::Printf(TEXT("state: coins=%d party=%d inv=[%s] quests=[%s] done=[%s]"),
			Sub->Save.Coins, Sub->Save.Party.Num(),
			Parts.Num() ? *FString::Join(Parts, TEXT(", ")) : TEXT("empty"),
			Sub->Save.ActiveQuests.Num() ? *FString::Join(Sub->Save.ActiveQuests, TEXT(",")) : TEXT("-"),
			Sub->Save.DoneQuests.Num() ? *FString::Join(Sub->Save.DoneQuests, TEXT(",")) : TEXT("-")));
	}

	// ------------------------------------------------------------- drives
	/** Push a key through the router — the same call the pawn's handlers make. */
	void DriveKey(UEmberwildSubsystem* Sub, const FKey& Key)
	{
		if (Sub && Sub->UIRouter.IsValid())
			Sub->UIRouter->HandlePanelKey(Key);
	}
	void DriveE(UEmberwildSubsystem* Sub) { DriveKey(Sub, EKeys::E); }

	/** Teleport the pawn (game-space x/z -> UE x/-y, like every spawn helper). */
	bool TeleportTo(UEmberwildSubsystem* Sub, float X, float Z)
	{
		AEmberwildPlayer* P = Sub->Player.Get();
		if (!P) return false;
		const float H = AEmberwildWorldBuilder::SurfaceHeight(X, Z);
		P->TeleportTo(FVector(X, -Z, H + 1.f), P->GetActorRotation());
		return true;
	}

	/** The pawn's InteractPressed core: nearest interactable in range acts. */
	void InteractWithNearest(UEmberwildSubsystem* Sub)
	{
		AEmberwildPlayer* P = Sub->Player.Get();
		UWorld* W = Sub->GetWorld();
		if (!P || !W) return;

		AInteractableActor* Best = nullptr;
		float BestD = P->InteractRange * P->InteractRange;
		for (TActorIterator<AInteractableActor> It(W); It; ++It)
		{
			const float D = FVector::DistSquared2D(P->GetActorLocation(), It->GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
		}
		if (Best) Best->Interact(P);
		else LogP("no interactable in range");
	}

	/** Run an NPC tree through the real dialogue keys: E advances, Y answers. */
	void AdvanceDialogueThroughKeys(UEmberwildSubsystem* Sub)
	{
		for (int32 Guard = 0; Guard < 24 && Sub->Dialogue.IsValid(); ++Guard)
		{
			DriveKey(Sub, EKeys::Y); // answers a pending choice; no-op otherwise
			DriveKey(Sub, EKeys::E); // advance / open follow-up node
		}
		Sub->Dialogue.Reset(); // safety: the runtime normally self-ends on the terminal line
	}

	/**
	 * Full party heal through the game's own healing loop: talk to Mira at the
	 * well (her lines carry .Heal()). HP carries between battles by design, so
	 * every retry starts from a fresh heal like a real player would.
	 */
	static void HealAtMira(UEmberwildSubsystem* S)
	{
		TeleportTo(S, -4.9f, -7.0f);
		// the anti-double-interact cooldown would eat chained autopilot interacts
		S->MarkInteractCooldown(0.f);
		InteractWithNearest(S);
		AdvanceDialogueThroughKeys(S);
		LogP(FString::Printf(TEXT("healed at Mira's well (starter %d/%d HP)"),
			S->Save.Party.Num() ? S->Save.Party[0].CurHP : 0,
			S->Save.Party.Num() ? S->Save.Party[0].MaxHP() : 0));
	}

	// ------------------------------------------------------------- battle AI
	// One shared driver for every battle: a 1s tick that reads the router's
	// live menu state and keys through it. Dice-dependent outcomes (catches,
	// close trainer fights) get a retry budget: when a battle closes without
	// meeting its goal, the driver starts the next one itself.
	enum class EPlan { WeakenThenCatch, GrindFight, TobinFight };

	struct FBattleDriver
	{
		bool  bArmed = false;
		bool  bSawBattle = false;   // saw a live engine — its disappearance means "closed"
		int32 Ticks = 0;
		int32 BattlesStarted = 0;
		int32 MaxBattles = 1;
		EPlan Plan = EPlan::GrindFight;
		FTimerHandle Handle;

		static FBattleDriver& Get()
		{
			static FBattleDriver Inst;
			return Inst;
		}

		void Arm(UWorld* W, UEmberwildSubsystem* S, EPlan InPlan, int32 InMaxBattles)
		{
			Plan = InPlan;
			MaxBattles = InMaxBattles;
			BattlesStarted = 1;
			Ticks = 0;
			bSawBattle = false;
			bArmed = true;
			TWeakObjectPtr<UEmberwildSubsystem> WeakSub = S;
			W->GetTimerManager().SetTimer(Handle, [WeakSub]() { Tick(WeakSub); }, 1.0f, true);
		}
		void Stop(UWorld* W) { W->GetTimerManager().ClearTimer(Handle); bArmed = false; }

		/** SWITCH is menu item 2; confirm the first healthy party member. */
		static void SwitchSickMon(UEmberwildSubsystem* S)
		{
			LogP("active mon is down — switching");
			DriveKey(S, EKeys::Down);
			DriveKey(S, EKeys::Down);
			DriveE(S);              // open the switch submenu
			if (S->Save.Party.IsValidIndex(0) && S->Save.Party[0].CurHP <= 0)
				DriveKey(S, EKeys::Down); // skip the fainted first slot
			DriveE(S);              // confirm the switch
		}

		/** Goal not met and budget remains? Start the next battle. */
		bool MaybeRestart(UEmberwildSubsystem* S)
		{
			if (BattlesStarted >= MaxBattles) return false;

			switch (Plan)
			{
			case EPlan::WeakenThenCatch:
				if (S->Save.HasFlag(TEXT("caught_any"))) return false;
				if (S->Save.CountItem(TEXT("core_wood")) <= 0) return false;
				LogP(FString::Printf(TEXT("catch failed — healing and retrying (battle %d/%d)"),
					BattlesStarted + 1, MaxBattles));
				HealAtMira(S);
				S->StartWildBattle(FMonsterFactory::Create(TEXT("burrowmouse"), 2));
				break;

			case EPlan::GrindFight:
				// stop early once the starter is strong enough for the rival
				if (!S->Save.PartyAlive()) return false;
				if (S->Save.Party.IsValidIndex(0) && S->Save.Party[0].Level >= 8) return false;
				LogP(FString::Printf(TEXT("grind battle %d/%d"), BattlesStarted + 1, MaxBattles));
				HealAtMira(S);
				S->StartWildBattle(FMonsterFactory::Create(TEXT("burrowmouse"), 5));
				break;

			case EPlan::TobinFight:
				if (S->Save.DefeatedTrainers.Contains(TEXT("trainer_tobin"))) return false;
				if (!S->Save.PartyAlive()) return false;
				LogP(FString::Printf(TEXT("rematch vs Tobin (%d/%d)"), BattlesStarted + 1, MaxBattles));
				HealAtMira(S);
				TeleportTo(S, 26.f, -26.f);
				S->MarkInteractCooldown(0.f);   // Mira's heal just consumed the 0.3s window
				InteractWithNearest(S);         // PickNode routes to tobin_fight again
				AdvanceDialogueThroughKeys(S);  // SpawnsTrainer -> battle opens
				break;
			}

			if (!S->Battle.IsValid()) return false;
			++BattlesStarted;
			Ticks = 0;
			bSawBattle = false;
			return true;
		}

		static void Tick(TWeakObjectPtr<UEmberwildSubsystem> WeakSub)
		{
			UEmberwildSubsystem* S = WeakSub.Get();
			UWorld* W = S ? S->GetWorld() : nullptr;
			FBattleDriver& A = Get();
			if (!S || !W || !A.bArmed) return;

			// engine gone: battle closed (confirmed over-screen, caught, or lost)
			if (!S->Battle.IsValid())
			{
				if (A.bSawBattle)
				{
					LogP("battle finished and closed");
					LogInv(S);
					if (!A.MaybeRestart(S))
						A.Stop(W);
				}
				return;
			}
			A.bSawBattle = true;

			if (++A.Ticks > 90)
			{
				LogStep(false, "battle did not conclude within 90 turns");
				A.Stop(W);
				return;
			}

			// battle-over screen: confirm through the real input path
			if (S->UIRouter.IsValid() && S->UIRouter->bBattleOver)
			{
				const EBattleResult R = S->Battle->Result;
				LogStep(R != EBattleResult::Ongoing,
					FString::Printf(TEXT("battle concluded (result=%d, ticks=%d)"), (int32)R, A.Ticks));
				DriveE(S);
				return; // engine resets; next tick hits the "finished and closed" path
			}

			SEmberwildUIRouter* UI = S->UIRouter.Get();

			// a fainted active mon must be switched before anything else
			if (UI && UI->BattleMode == TEXT("menu") && S->Battle->PlayerMon()
				&& S->Battle->PlayerMon()->CurHP <= 0)
			{
				SwitchSickMon(S);
				return;
			}

			// in a sub-menu: confirm the highlighted entry (throw / move / switch)
			if (UI && (UI->BattleMode == TEXT("bag") || UI->BattleMode == TEXT("moves")
				|| UI->BattleMode == TEXT("switch")))
			{
				DriveE(S);
				return;
			}

			// root menu
			if (A.Plan == EPlan::TobinFight && UI && UI->BattleMode == TEXT("menu"))
			{
				// heal up with a snack when the active mon is hurting (bag cursor
				// walks the live inventory, so compute the snack's index each time)
				if (const FCreature* PM = S->Battle->PlayerMon())
				{
					const bool bHurt = PM->MaxHP() > 0 && (float)PM->CurHP / (float)PM->MaxHP() < 0.4f;
					int32 SnackIdx = S->Save.Inventory.IndexOfByPredicate([](const FItemStack& It)
						{ return It.ItemId == TEXT("trail_snack"); });
					if (bHurt && SnackIdx != INDEX_NONE)
					{
						LogP(FString::Printf(TEXT("HP low (%d/%d) — eating a trail snack"), PM->CurHP, PM->MaxHP()));
						DriveKey(S, EKeys::Down);  // BAG
						DriveE(S);                 // open the bag (cursor 0)
						for (int32 i = 0; i < SnackIdx; ++i)
							DriveKey(S, EKeys::Down);
						DriveE(S);                 // use the snack
						return;
					}
				}
			}
			if (A.Plan == EPlan::WeakenThenCatch)
			{
				if (const FCreature* EM = S->Battle->EnemyMon())
				{
					const float HpFrac = EM->MaxHP() > 0 ? (float)EM->CurHP / (float)EM->MaxHP() : 0.f;
					if (HpFrac <= 0.45f && S->Save.CountItem(TEXT("core_wood")) > 0)
					{
						LogP(FString::Printf(TEXT("foe at %.0f%% — throwing a core (BAG)"), HpFrac * 100.f));
						DriveKey(S, EKeys::Down); // BAG
						DriveE(S);
						return;
					}
				}
			}
			DriveE(S); // cursor sits on FIGHT; confirm straight into the move list
		}
	};

	// ------------------------------------------------------------- script
	struct FStage
	{
		float At; // seconds after boot
		const TCHAR* Name;
		TFunction<void(UEmberwildSubsystem*, UWorld*)> Fn;
	};

	struct FAutopilot
	{
		TWeakObjectPtr<UEmberwildSubsystem> Sub;
		double Start = 0.f;
		int32 Next = 0;
		FTimerHandle StepHandle;

		static FAutopilot& Get()
		{
			static FAutopilot Inst;
			return Inst;
		}

		void RunStages()
		{
			UEmberwildSubsystem* S = Sub.Get();
			UWorld* W = S ? S->GetWorld() : nullptr;
			if (!S || !W) return;

			const double Elapsed = FPlatformTime::Seconds() - Start;
			static const TArray<FStage> Stages = BuildStages();
			while (Next < Stages.Num() && Elapsed >= Stages[Next].At)
			{
				const FStage& St = Stages[Next++];
				LogP(FString::Printf(TEXT("-- stage: %s (t+%.1fs) --"), St.Name, Elapsed));
				St.Fn(S, W);
			}
			if (Next >= Stages.Num())
			{
				if (UWorld* Wr = Sub->GetWorld())
					Wr->GetTimerManager().ClearTimer(StepHandle);
			}
		}

		static const TArray<FStage>& BuildStages()
		{
			static TArray<FStage> Stages =
			{
				// ================================================== QUEST 1
				{ 1.f, TEXT("title"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->UIRouter.IsValid(), "router booted");
					DriveE(S); // title consumes E/Enter -> starts in the meadow
					LogStep(S->UIRouter.IsValid() && !S->UIRouter->bTitleActive, "title dismissed, world input live");
				} },

				{ 3.f, TEXT("brann"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogP("talking to Brann");
					TeleportTo(S, -13.6f, -10.8f);
					InteractWithNearest(S);
					LogStep(S->Dialogue.IsValid(), "dialogue runtime started (brann_intro)");
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_firstcore")), "Brann quest 'q_firstcore' accepted");
					LogStep(!S->Dialogue.IsValid(), "dialogue runtime ended cleanly");
					LogInv(S);
				} },

				// 3x meadow grass (fiber for rope) + 2x pebbles (stone for the core)
				{ 5.f, TEXT("gather1"), [](UEmberwildSubsystem* S, UWorld*)
				{ TeleportTo(S, 5.f, -25.5f); InteractWithNearest(S); } },
				{ 6.5f, TEXT("gather2"), [](UEmberwildSubsystem* S, UWorld*)
				{ TeleportTo(S, 9.5f, -25.5f); InteractWithNearest(S); } },
				{ 8.f, TEXT("gather3"), [](UEmberwildSubsystem* S, UWorld*)
				{ TeleportTo(S, 12.f, -25.5f); InteractWithNearest(S); } },
				{ 9.5f, TEXT("gather4"), [](UEmberwildSubsystem* S, UWorld*)
				{ TeleportTo(S, 2.f, -18.f); InteractWithNearest(S); } },
				{ 11.f, TEXT("gather5"), [](UEmberwildSubsystem* S, UWorld*)
				{ TeleportTo(S, 14.f, -15.f); InteractWithNearest(S); } },
				{ 12.5f, TEXT("gather-check"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.CountItem(TEXT("fiber")) >= 3,
						FString::Printf(TEXT("gathered fiber (%d)"), S->Save.CountItem(TEXT("fiber"))));
					LogStep(S->Save.CountItem(TEXT("stone")) >= 2,
						FString::Printf(TEXT("gathered stone (%d)"), S->Save.CountItem(TEXT("stone"))));
					LogStep(S->Save.ProfXpOf(EProfessionId::Forager) > 0 || S->Save.ProfXpOf(EProfessionId::Miner) > 0,
						"gather XP awarded");
					LogInv(S);
				} },

				{ 14.f, TEXT("forge"), [](UEmberwildSubsystem* S, UWorld*)
				{
					TeleportTo(S, -16.4f, -12.2f);
					InteractWithNearest(S); // CraftStation "forge" -> OpenCrafting
					LogStep(S->UIRouter.IsValid(), "crafting panel opened at the forge");
					DriveE(S); // cursor starts at r_rope (first anywhere-recipe)
					LogStep(S->Save.CountItem(TEXT("rope")) >= 1, "braided rope");
					// E confirms the CURRENT cursor — walk the list until the core is crafted
					for (int32 i = 0; i < 14 && S->Save.CountItem(TEXT("core_wood")) < 1; ++i)
					{
						DriveKey(S, EKeys::Down);
						DriveE(S);
					}
					LogStep(S->Save.CountItem(TEXT("core_wood")) >= 1, "crafted Timber Core");
					LogStep(S->Save.HasFlag(TEXT("craft_core_wood")), "flag craft_core_wood set");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcore")),
						"quest 'q_firstcore' auto-completed");
					LogInv(S);
				} },

				// ================================================== QUEST 2
				{ 30.f, TEXT("shop"), [](UEmberwildSubsystem* S, UWorld*)
				{
					TeleportTo(S, -8.2f, -16.8f);
					InteractWithNearest(S); // ShopStall "general" -> OpenShop
					LogStep(S->UIRouter.IsValid(), "shop panel opened");
					// stock cursor 0 = core_wood:6; each E buys one — take three
					DriveE(S);
					DriveE(S);
					DriveE(S);
					LogStep(S->Save.CountItem(TEXT("core_wood")) >= 4,
						FString::Printf(TEXT("bought capture cores (%d total)"), S->Save.CountItem(TEXT("core_wood"))));
					DriveKey(S, EKeys::Escape); // close the shop
					LogInv(S);
				} },

				{ 34.f, TEXT("maren-offer"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogP("talking to Maren");
					TeleportTo(S, 7.5f, -14.6f);
					InteractWithNearest(S);
					// maren_idle is a Yes/No choice; either branch offers the quest.
					// AdvanceDialogueThroughKeys answers the first choice with Y ("yes").
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_firstcatch")),
						"Maren quest 'q_firstcatch' accepted");
					LogInv(S);
				} },

				{ 40.f, TEXT("catch-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					FCreature Wild = FMonsterFactory::Create(TEXT("burrowmouse"), 2);
					LogStep(Wild.Species() != nullptr, "wild burrowmouse spawned for the catch attempt");
					S->StartWildBattle(Wild);
					if (S->Battle.IsValid())
					{
						// dice: up to 4 attempts at ~30-38% per throw with 4 cores
						FBattleDriver::Get().Arm(W, S, EPlan::WeakenThenCatch, 4);
						LogP("driver armed: fight until foe <=45% HP, throw cores, retry up to 4 battles");
					}
				} },

				{ 76.f, TEXT("maren-collect"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.HasFlag(TEXT("caught_any")), "a creature was caught (flag caught_any)");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcatch")),
						"quest 'q_firstcatch' auto-completed");
					LogStep(S->Save.Party.Num() >= 2,
						FString::Printf(TEXT("party grew via capture (%d members)"), S->Save.Party.Num()));

					// second talk: with caught_first already set the tree routes to
					// maren_yes (Chef gift; the quest offer no-ops on a done quest)
					TeleportTo(S, 7.5f, -14.6f);
					InteractWithNearest(S);
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Save.HasFlag(TEXT("caught_first")), "flag caught_first set (quest completion)");
					LogInv(S);
				} },

				// ================================================== GRIND + TOBIN
				{ 84.f, TEXT("grind-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					// the XP curve wants ~5 wins per level; stop early at starter Lv7
					S->StartWildBattle(FMonsterFactory::Create(TEXT("burrowmouse"), 6));
					if (S->Battle.IsValid())
					{
						FBattleDriver::Get().Arm(W, S, EPlan::GrindFight, 14);
						LogP("driver armed: grind battles for XP (early-stop at starter Lv7)");
					}
				} },

				{ 300.f, TEXT("tobin-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("talking to Tobin");
					TeleportTo(S, 26.f, -26.f);
					InteractWithNearest(S);
					// tobin_fight ends in SpawnsTrainer -> dialogue ends -> battle opens
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Battle.IsValid(), "trainer battle vs Tobin started");
					if (S->Battle.IsValid())
					{
						FBattleDriver::Get().Arm(W, S, EPlan::TobinFight, 3);
						LogP("driver armed: fight Tobin, snack-heal when hurt (two rematches budgeted)");
					}
				} },

				{ 380.f, TEXT("tobin-check"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.DefeatedTrainers.Contains(TEXT("trainer_tobin")),
						"Tobin defeated (DefeatedTrainers)");
					LogStep(S->Save.HasFlag(TEXT("beat_tobin")), "flag beat_tobin set");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_tobin")), "quest 'q_tobin' auto-completed");
					LogStep(S->Save.Coins >= 240, FString::Printf(TEXT("reward coins collected (%d)"), S->Save.Coins));
					LogInv(S);
				} },

				// ================================================== summary + quit
				{ 400.f, TEXT("done"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcore")),
						"FINAL: quest 1 (q_firstcore) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcatch")),
						"FINAL: quest 2 (q_firstcatch) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_tobin")),
						"FINAL: Tobin quest (q_tobin) completed");
					LogP("playtest finished — quitting");
					if (GEngine) GEngine->Exec(W, TEXT("exit"));
				} },
			};
			return Stages;
		}
	};
}

void EmberwildStartAutopilot(UEmberwildSubsystem* Sub)
{
	FAutopilot& A = FAutopilot::Get();
	A.Sub = Sub;
	A.Start = FPlatformTime::Seconds();
	A.Next = 0;
	LogP("armed: quest1 (Brann/gather/forge) -> quest2 (shop/Maren/catch) -> grind -> Tobin");
	if (UWorld* W = Sub->GetWorld())
		W->GetTimerManager().SetTimer(A.StepHandle, []() { FAutopilot::Get().RunStages(); }, 0.25f, true);
}

#else

void EmberwildStartAutopilot(UEmberwildSubsystem* Sub)
{
}

#endif // EMBERWILD_AUTOPILOT
