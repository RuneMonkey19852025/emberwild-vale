// Emberwild Vale — headless playtest autopilot (compile-gated).
//
// Drives the QUEST LOOPS through the exact same entry points a player's
// keystrokes reach — SEmberwildUIRouter::HandlePanelKey, the pawn's
// InteractPressed core, AInteractableActor::Interact, the subsystem's battle
// entries — and logs [PASS]/[FAIL] per step so a `-game -nullrhi` run
// validates the game without a renderer or human input.
//
// Act 1: title -> Brann -> 5 gathers -> forge (rope + Timber Core) ->
//        q_firstcore; shop x3 cores -> Maren -> weaken & catch ->
//        q_firstcatch; grind wilds for XP; Tobin's trainer battle ->
//        q_tobin (with rematches budgeted).
// Act 2: Juna offers q_professions -> craft rope/cores/snacks (Tailor,
//        Smith, Chef all to Lv2) -> sworn in; Odessa offers q_emberrock ->
//        miner levels from pebbles -> Korrin (team Lv9/10/9) -> ridge_opened
//        + sigil_piece_a + miner_pick; Sela offers q_sigil; Hollis
//        (Lv12/13) -> sigil_piece_b; mine the ridge copper/iron veins
//        until the rare sigil_piece_c drops (1-in-10 per gather); Sela
//        turn-in completes q_sigil.
// Act 3: Zephy offers q_herald -> grind the team to Lv16 -> beat the
//        Hollow Herald (Mind/Frost/Shade, Lv16/16/18) -> herald_down;
//        the Sleeper offers q_sleeper -> choose wake -> sleeper_awake
//        + the Gildra gift -> q_sleeper completes. Finale rewards.
//
// Battles use dice (catch rolls, damage variance), so each battle stage arms
// the driver with a retry budget instead of assuming a first-try win. HP
// carries between battles (by design), so every restart heals at Mira's
// well — the same loop a real player runs.
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
#include "HAL/PlatformMisc.h"

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
		TArray<FString> Profs;
		for (int32 i = 0; i < 6; ++i)
			Profs.Add(FString::Printf(TEXT("%d"), Sub->Save.ProfLevels.IsValidIndex(i) ? Sub->Save.ProfLevels[i] : 0));
		LogP(FString::Printf(TEXT("state: coins=%d party=%d profs=[%s] inv=[%s] quests=[%s] done=[%s]"),
			Sub->Save.Coins, Sub->Save.Party.Num(), *FString::Join(Profs, TEXT(",")),
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
			// A SpawnsTrainer line ends the tree and opens the battle panel. Stop
			// here: the leftover iterations would land on the battle panel as E
			// presses, playing (and possibly losing or winning) whole turns
			// before the driver ever got control of the fight.
			if (Sub->Battle.IsValid()) break;
			DriveKey(Sub, EKeys::Y); // answers a pending choice; no-op otherwise
			DriveKey(Sub, EKeys::E); // advance / open follow-up node
		}
		Sub->Dialogue.Reset(); // safety: the runtime normally self-ends on the terminal line
	}

	/** Stand next to an NPC and talk to *that* NPC specifically.
	 *  InteractWithNearest takes whichever interactable is closest, which is not
	 *  reliably the one we mean — a gather node beside an NPC used to swallow the
	 *  press and the dialogue never opened at all. */
	void TalkToNpcAt(UEmberwildSubsystem* S, float X, float Z, const TCHAR* NpcId)
	{
		TeleportTo(S, X, Z);
		UWorld* W = S->GetWorld();
		AEmberwildPlayer* P = S->Player.Get();
		if (!W || !P) return;
		for (TActorIterator<AInteractableActor> It(W); It; ++It)
		{
			if (It->Kind != EInteractableKind::Npc || It->RefId != NpcId) continue;
			S->MarkInteractCooldown(0.f); // chained interacts eat the 0.3s window
			It->Interact(P);
			return;
		}
		LogP(FString::Printf(TEXT("NPC '%s' not found near (%.1f, %.1f)"), NpcId, X, Z));
	}

	// ------------------------------------------------------------- panels
	/** Buy Times units of a general-store stock entry via the real shop keys
	 *  (one shop session; the stock cursor wraps). */
	void BuyShopItem(UEmberwildSubsystem* S, const TCHAR* ItemId, int32 Times = 1)
	{
		const FShopDef* Shop = FEmberwildDB::Shop(TEXT("general"));
		if (!Shop) return;
		int32 StockIdx = INDEX_NONE;
		for (int32 i = 0; i < Shop->Stock.Num(); ++i)
		{
			FString StockId = Shop->Stock[i];
			const int32 Colon = StockId.Find(TEXT(":"));
			if (Colon != INDEX_NONE) StockId = StockId.Left(Colon);
			if (StockId == ItemId) { StockIdx = i; break; }
		}
		if (StockIdx == INDEX_NONE)
		{
			LogP(FString::Printf(TEXT("'%s' is not stocked at the general store"), ItemId));
			return;
		}
		TeleportTo(S, -8.2f, -16.8f);
		S->MarkInteractCooldown(0.f);
		InteractWithNearest(S); // ShopStall -> OpenShop (cursor 0)
		for (int32 i = 0; i < StockIdx; ++i)
			DriveKey(S, EKeys::Down);
		for (int32 i = 0; i < Times; ++i)
			DriveE(S);          // buy one per confirm (cursor stays put)
		DriveKey(S, EKeys::Escape); // close the shop so the next stage can act
	}

	/** Open a crafting station and craft Times times at the given visible-recipe
	 *  cursor (list order = recipe registration order, station-filtered). */
	void CraftAt(UEmberwildSubsystem* S, float X, float Z, int32 Cursor, int32 Times)
	{
		TeleportTo(S, X, Z);
		S->MarkInteractCooldown(0.f);
		InteractWithNearest(S); // CraftStation -> OpenCrafting (cursor 0)
		// The panel does NOT advance the cursor after a craft, so walk down to the
		// target ONCE. Re-walking per craft compounds the offset and lands on an
		// unrelated recipe (that silently wrecked the old Smith plan).
		for (int32 D = 0; D < Cursor; ++D)
			DriveKey(S, EKeys::Down);
		for (int32 i = 0; i < Times; ++i)
			DriveE(S);
		DriveKey(S, EKeys::Escape);
	}

	/** Full party heal through the game's own healing loop: talk to Mira at the
	 *  well (her lines carry .Heal()). HP carries between battles by design, so
	 *  every retry starts from a fresh heal like a real player would. */
	void HealAtMira(UEmberwildSubsystem* S)
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
	enum class EPlan { WeakenThenCatch, GrindFight, TobinFight, RidgeFight, HollisFight, ZephyFight, MineLoop };

	struct FBattleDriver
	{
		bool  bArmed = false;
		bool  bSawBattle = false;   // saw a live engine — its disappearance means "closed"
		int32 Ticks = 0;
		int32 BattlesStarted = 0;
		int32 MaxBattles = 1;
		int32 ThrowsThisBattle = 0; // one core per battle, else the retry has no ammo
		int32 SubMenuTicks = 0;  // consecutive ticks spent inside a battle sub-menu
		int32 Gathers = 0;       // successful gathers this sweep
		TSet<FString> RefusedVeins; // nodes that refused a pick — never retried this sweep
		int32 StopLevel = 8;     // GrindFight early-stop
		int32 GrindWildLevel = 5; // GrindFight wild level
		int32 MinParty = 2;      // WeakenThenCatch is done once the party reaches this
		bool bTriedLeadSwitch = false; // one proactive type-aware switch per battle
		EPlan Plan = EPlan::GrindFight;
		float CurrentRate = 0.f; // last rate handed to the timer, so we only re-arm on change
		FTimerHandle Handle;

		static FBattleDriver& Get()
		{
			static FBattleDriver Inst;
			return Inst;
		}

		/** WeakenThenCatch normally stops at the first capture (MinParty 2). The
		 *  Tide catch runs later, with a capture already banked, so it raises the
		 *  bar instead of using the caught_any flag. */
		void SetMinParty(int32 InMinParty) { MinParty = InMinParty; }

		void Arm(UWorld* W, UEmberwildSubsystem* S, EPlan InPlan, int32 InMaxBattles,
			int32 InStopLevel = 8, int32 InWildLevel = 5)
		{
			Plan = InPlan;
			MaxBattles = InMaxBattles;
			StopLevel = InStopLevel;
			GrindWildLevel = InWildLevel;
			MinParty = 2; // callers that need more raise it via SetMinParty after Arm
			bTriedLeadSwitch = false;
			BattlesStarted = 1;
			Ticks = 0;
			bSawBattle = false;
			bArmed = true;
			ThrowsThisBattle = 0;
			SubMenuTicks = 0;
			Gathers = 0;
			RefusedVeins.Reset();
			CurrentRate = 0.f;
			TWeakObjectPtr<UEmberwildSubsystem> WeakSub = S;
			W->GetTimerManager().SetTimer(Handle, [WeakSub]() { Tick(WeakSub); }, 1.0f, true);
		}
		void Stop(UWorld* W) { W->GetTimerManager().ClearTimer(Handle); bArmed = false; }

		/** Grind target. The starter is a Verdant Sprigling, so a Stone wild is
		 *  the ideal dummy: Verdant hits Stone for 2x and Stone hits Verdant for
		 *  0.5x, which keeps the win rate high while Vaultmole's 36 XpYield
		 *  (vs Burrowmouse's 20) roughly doubles the XP per grind win. */
		static const TCHAR* GrindSpecies() { return TEXT("vaultmole"); }

		/** Level the grind wild just under the starter's. XP scales with the
		 *  wild's level (XpYield * Lvl / 6 + 4), so tracking the leader keeps
		 *  every grind win worth as much as possible. */
		static int32 GrindLevelFor(const UEmberwildSubsystem* S)
		{
			const int32 Lvl = (S->Save.Party.IsValidIndex(0) && S->Save.Party[0].Level > 0)
				? S->Save.Party[0].Level : 3;
			return FMath::Max(3, Lvl - 2);
		}

		/** Re-arm the driver timer at *Rate* seconds. */
		void SetRate(UWorld* W, UEmberwildSubsystem* S, float Rate)
		{
			if (FMath::IsNearlyEqual(CurrentRate, Rate)) return;
			CurrentRate = Rate;
			TWeakObjectPtr<UEmberwildSubsystem> WeakSub = S;
			W->GetTimerManager().SetTimer(Handle, [WeakSub]() { Tick(WeakSub); }, Rate, true);
		}

		/** Battle turns are pure engine state the driver advances by hand — it
		 *  reads BattleMode and pushes keys, never waits on a rebuild — so an
		 *  in-battle tick can run far faster than the 1s pace. That matters:
		 *  a grind win is ~23 turns, which at 1s/tick ate 23 seconds of the
		 *  stage window and starved the Tobin grind of levels. */
		/** The mine sweep never opens a battle engine, so this runs ahead of the
		 *  Battle.IsValid() gates in Tick() — those gates used to early-return on
		 *  every tick and the whole plan sat dead for its entire window.
		 *
		 *  While Miner < 2 sweep the meadow pebble/stone nodes to level up (copper
		 *  needs Miner 2); then sweep the ridge copper veins (iron needs Miner 4),
		 *  waiting out the 70s respawn when all are depleted. The Ember fragment
		 *  is the rare tail of the vein pools: 1-in-10 per gather. */
		static void TickMineLoop(FBattleDriver& A, UEmberwildSubsystem* S)
		{
			++A.Ticks;

			AEmberwildPlayer* P = S->Player.Get();
			UWorld* Wld = S->GetWorld();
			if (!P || !Wld) return;

			const int32 MinerLvl = S->Save.ProfLevel(EProfessionId::Miner);

			auto NearestLivingVein = [&]() -> AInteractableActor*
			{
				AInteractableActor* Best = nullptr;
				float BestD = TNumericLimits<float>::Max();
				for (TActorIterator<AInteractableActor> It(Wld); It; ++It)
				{
					if (It->Kind != EInteractableKind::GatherNode || It->bDepleted) continue;
					if (A.RefusedVeins.Contains(It->RefId)) continue;
					// Only target nodes this Miner level can actually pick. The
					// interact refuses an under-levelled node with a toast (no log,
					// no depletion), so the old sweep camped the Miner-4 iron vein
					// at Miner 2 and sat there, silently, for its whole window.
					const FNodeDef* Def = FEmberwildDB::Node(It->RefId);
					if (!Def || MinerLvl < Def->RequiredLevel) continue;
					const bool bTraining = MinerLvl < 2;
					const bool bWanted = bTraining
						? (It->RefId == TEXT("rock_pebble") || It->RefId == TEXT("rock_stone"))
						: (It->RefId == TEXT("rock_copper") || It->RefId == TEXT("rock_iron"));
					if (!bWanted) continue;
					const float D = FVector::DistSquared2D(P->GetActorLocation(), It->GetActorLocation());
					if (D < BestD) { BestD = D; Best = *It; }
				}
				return Best;
			};

			AInteractableActor* Vein = NearestLivingVein();
			if (!Vein)
			{
				if (A.Ticks % 10 == 0)
					LogP(FString::Printf(TEXT("no pickable %s node up — waiting out the 70s respawn (Miner Lv%d, %d gathers so far)"),
						MinerLvl < 2 ? TEXT("training") : TEXT("ridge"), MinerLvl, A.Gathers));
				return; // stand still; later ticks retry
			}

			const int32 PieceBefore = S->Save.CountItem(TEXT("sigil_piece_c"));
			const FString VeinId = Vein->RefId;
			const FVector2D XZ(Vein->GetActorLocation().X, -Vein->GetActorLocation().Y);
			TeleportTo(S, XZ.X, XZ.Y);
			S->MarkInteractCooldown(0.f);
			InteractWithNearest(S);

			// A gather only counts when the node actually depletes. A refusal
			// (level gate, cooldown, depleted race) leaves it alive, and since
			// the player now stands on it, it would stay the nearest pick
			// forever — skip it instead of retrying in silence.
			if (!Vein->bDepleted)
			{
				A.RefusedVeins.Add(VeinId);
				LogP(FString::Printf(TEXT("vein %s refused the pick (Miner Lv%d) — skipping it"),
					*VeinId, MinerLvl));
				return;
			}

			++A.Gathers;
			const int32 PieceAfter = S->Save.CountItem(TEXT("sigil_piece_c"));
			if (PieceAfter > PieceBefore)
				LogP(FString::Printf(TEXT("Ember fragment dropped from %s (gather #%d)"), *VeinId, A.Gathers));
			else if (A.Gathers % 10 == 0)
				LogP(FString::Printf(TEXT("mining: %d gathers (%s) (Miner Lv%d)"),
					A.Gathers,
					PieceAfter > 0 ? TEXT("fragment secured — filling out the window")
								   : TEXT("no fragment yet"),
					MinerLvl));
		}

		static float RateFor(const FBattleDriver& A, UEmberwildSubsystem* S)
		{
			if (!S->Battle.IsValid()) return 1.0f;      // between battles: let the world settle
			if (A.Plan == EPlan::MineLoop) return 0.5f;  // teleport+interact sweep, not turn-based
			return 0.15f;
		}

		/** SWITCH is menu item 2; confirm the first healthy party member. */
		static void SwitchToSlot(UEmberwildSubsystem* S, int32 TargetIdx)
		{
			DriveKey(S, EKeys::Down);
			DriveKey(S, EKeys::Down);
			DriveE(S);                        // open the switch submenu
			for (int32 i = 0; i < TargetIdx; ++i) // menu lists the party in order
				DriveKey(S, EKeys::Down);
			DriveE(S);                        // confirm the switch
		}

		static void SwitchSickMon(UEmberwildSubsystem* S)
		{
			// Pick the strongest living mon, not just the next slot: the party
			// carries an Lv2 burrowmouse ahead of the Tide mon that actually
			// matchups Korrin's Ember team, so "down one" picks the dead weight.
			int32 BestIdx = INDEX_NONE;
			int32 BestLvl = -1;
			for (int32 i = 0; i < S->Save.Party.Num(); ++i)
			{
				const FCreature& C = S->Save.Party[i];
				if (C.CurHP > 0 && C.Level > BestLvl) { BestLvl = C.Level; BestIdx = i; }
			}
			if (BestIdx == INDEX_NONE) { LogP("no living mon to switch to"); return; }
			LogP(FString::Printf(TEXT("active mon is down — switching to slot %d (Lv%d)"),
				BestIdx + 1, BestLvl));
			SwitchToSlot(S, BestIdx);
		}

		/** Open the battle bag and eat the most healing item in stock. */
		static void HealFromBag(UEmberwildSubsystem* S)
		{
			int32 BestIdx = INDEX_NONE, BestHeal = 0;
			for (int32 i = 0; i < S->Save.Inventory.Num(); ++i)
			{
				const FItemDef* D = FEmberwildDB::Item(S->Save.Inventory[i].ItemId);
				if (D && D->HealAmount > 0 && D->HealAmount < 999 && D->HealAmount > BestHeal)
				{
					BestHeal = D->HealAmount;
					BestIdx = i;
				}
			}
			if (BestIdx != INDEX_NONE)
			{
				LogP(FString::Printf(TEXT("eating %s (%d HP) from the battle bag"),
					*FEmberwildDB::Item(S->Save.Inventory[BestIdx].ItemId)->Name, BestHeal));
				DriveKey(S, EKeys::Down); // BAG
				DriveE(S);                // open the bag (cursor 0)
				for (int32 i = 0; i < BestIdx; ++i)
					DriveKey(S, EKeys::Down);
				DriveE(S);                // use it
			}
			else
			{
				DriveE(S); // nothing usable: fight on
			}
		}

		/** Index of the strongest usable attack: never a 0-power status move
		 *  (growl), and prefer STAB/type advantage only when it genuinely
		 *  out-damages the neutral option. The driver blind-confirms menu cursor
		 *  0, and level-ups rotate the learnset window, so cursor 0 is often
		 *  growl — against a 3-mon trainer team that is a guaranteed stall-loss
		 *  (a solo Lv16 sprigling lost every Zephy rematch that way). */
		static int32 BestMoveIdx(UEmberwildSubsystem* S)
		{
			auto* B = S->Battle.Get();
			const FCreature* PM = B ? B->PlayerMon() : nullptr;
			const FCreature* EM = B ? B->EnemyMon() : nullptr;
			if (!PM || !EM || !PM->Species() || !EM->Species()) return 0;

			float BestScore = -1.f;
			int32 BestIdx = 0;
			for (int32 i = 0; i < PM->Moves.Num(); ++i)
			{
				const FMoveDef* MV = FEmberwildDB::Move(PM->Moves[i]);
				if (!MV || MV->Power <= 0) continue;          // growl / lull etc.
				if (PM->CurrentPP(MV->Id) <= 0) continue;     // out of PP
				const float Eff = FTypeChart::Effectiveness(MV->Element, EM->Species()->Element, EM->Species()->Element2);
				const float Stab = PM->Species()->Element == MV->Element ? 1.5f : 1.f;
				const float Score = (float)MV->Power * Eff * Stab;
				if (Score > BestScore) { BestScore = Score; BestIdx = i; }
			}
			return BestIdx;
		}

		/** Goal not met and budget remains? Start the next battle. */
		bool MaybeRestart(UEmberwildSubsystem* S)
		{
			// Reserve the tail of the budget for real rematches: a plan that
			// spends every battle grinding never actually fights the trainer,
			// and the stage then times out with the goal unmet.
			const bool bGrinding = Plan == EPlan::GrindFight || Plan == EPlan::TobinFight
				|| Plan == EPlan::RidgeFight || Plan == EPlan::HollisFight || Plan == EPlan::ZephyFight;
			if (bGrinding && BattlesStarted >= MaxBattles - 3) return false;
			if (BattlesStarted >= MaxBattles) return false;

			switch (Plan)
			{
			case EPlan::WeakenThenCatch:
				if (S->Save.Party.Num() >= MinParty) return false;
				LogP(FString::Printf(TEXT("catch failed — healing and retrying (battle %d/%d)"),
					BattlesStarted + 1, MaxBattles));
				HealAtMira(S);
				BuyShopItem(S, TEXT("core_wood"), 6); // restock: each attempt burns one
				S->StartWildBattle(FMonsterFactory::Create(TEXT("burrowmouse"), 2));
				ThrowsThisBattle = 0;
				break;

			case EPlan::GrindFight:
				// stop early once the starter is strong enough for the next fight
				if (!S->Save.PartyAlive()) return false;
				if (S->Save.Party.IsValidIndex(0) && S->Save.Party[0].Level >= StopLevel) return false;
				{
					const int32 WildLvl = GrindLevelFor(S);
					LogP(FString::Printf(TEXT("grind battle %d/%d (%s Lv%d, stop at Lv%d, starter now Lv%d)"),
						BattlesStarted + 1, MaxBattles, GrindSpecies(), WildLvl, StopLevel,
						S->Save.Party.IsValidIndex(0) ? S->Save.Party[0].Level : 0));
					HealAtMira(S);
					S->StartWildBattle(FMonsterFactory::Create(GrindSpecies(), WildLvl));
				}
				break;			case EPlan::TobinFight:
				if (S->Save.DefeatedTrainers.Contains(TEXT("trainer_tobin"))) return false;
				if (!S->Save.PartyAlive()) return false;
				// Underleveled? A Lv8 starter lost this fight 3x running and the
				// whole act stalled on the q_tobin prereq. Grind neutral wilds to
				// StopLevel first, exactly like the Ridge/Hollis plans.
				if (S->Save.Party.IsValidIndex(0) && S->Save.Party[0].Level < StopLevel)
				{
					const int32 WildLvl = GrindLevelFor(S);
					LogP(FString::Printf(TEXT("grind battle %d/%d (%s Lv%d, stop at Lv%d, starter now Lv%d)"),
						BattlesStarted + 1, MaxBattles, GrindSpecies(), WildLvl, StopLevel,
						S->Save.Party[0].Level));
					HealAtMira(S);
					S->StartWildBattle(FMonsterFactory::Create(GrindSpecies(), WildLvl));
					if (!S->Battle.IsValid()) return false;
					++BattlesStarted;
					Ticks = 0;
					bSawBattle = false;
					bTriedLeadSwitch = false;
					return true;
				}
				LogP(FString::Printf(TEXT("rematch vs Tobin (%d/%d)"), BattlesStarted + 1, MaxBattles));
				HealAtMira(S);
				BuyShopItem(S, TEXT("salve"), 2);
				TeleportTo(S, 26.f, -26.f);
				S->MarkInteractCooldown(0.f);   // chained interacts eat the 0.3s window
				TalkToNpcAt(S, 26.f, -26.f, TEXT("tobin")); // PickNode: tobin_fight
				AdvanceDialogueThroughKeys(S);  // SpawnsTrainer -> battle opens
				break;
			case EPlan::RidgeFight:
				case EPlan::HollisFight:
				case EPlan::ZephyFight:
				{
					const TCHAR* TrainerId = Plan == EPlan::RidgeFight ? TEXT("trainer_korrin")
						: Plan == EPlan::HollisFight ? TEXT("trainer_hollis") : TEXT("trainer_zephy");
					if (S->Save.DefeatedTrainers.Contains(TrainerId)) return false;
					if (!S->Save.PartyAlive()) return false;
					// underleveled? grind neutral wilds first (StopLevel via Arm)
					if (S->Save.Party.IsValidIndex(0) && S->Save.Party[0].Level < StopLevel){
						const int32 WildLvl = GrindLevelFor(S);
						LogP(FString::Printf(TEXT("grind battle %d/%d (%s Lv%d, stop at Lv%d, starter now Lv%d)"),
							BattlesStarted + 1, MaxBattles, GrindSpecies(), WildLvl, StopLevel,
							S->Save.Party.IsValidIndex(0) ? S->Save.Party[0].Level : 0));
						HealAtMira(S);
						S->StartWildBattle(FMonsterFactory::Create(GrindSpecies(), WildLvl));
						if (!S->Battle.IsValid()) return false;
						++BattlesStarted;
						Ticks = 0;
						bSawBattle = false;
					bTriedLeadSwitch = false;
						return true;
					}
					const TCHAR* Who = Plan == EPlan::RidgeFight ? TEXT("Korrin")
						: Plan == EPlan::HollisFight ? TEXT("Hollis") : TEXT("Zephy");
					const float AtX = Plan == EPlan::RidgeFight ? 70.f
						: Plan == EPlan::HollisFight ? 6.f : 14.f;
					const float AtZ = Plan == EPlan::RidgeFight ? 10.f
						: Plan == EPlan::HollisFight ? -60.f : 66.f;
					const TCHAR* NpcId = Plan == EPlan::RidgeFight ? TEXT("korrin")
						: Plan == EPlan::HollisFight ? TEXT("hollis") : TEXT("zephy");
					LogP(FString::Printf(TEXT("rematch vs %s (%d/%d)"),
						Who, BattlesStarted + 1, MaxBattles));
					HealAtMira(S);
					// buy back heals so every attempt walks in stocked
					BuyShopItem(S, TEXT("salve"), 2);
					TeleportTo(S, AtX, AtZ);
					S->MarkInteractCooldown(0.f);   // chained interacts eat the 0.3s window
					TalkToNpcAt(S, AtX, AtZ, NpcId);
					AdvanceDialogueThroughKeys(S);  // SpawnsTrainer -> battle opens
					break;
				}

			case EPlan::MineLoop:
				return false; // handled by the stage check, not by battle restarts
			default:
				break;
			}

			if (!S->Battle.IsValid()) return false;
			++BattlesStarted;
			Ticks = 0;
			bSawBattle = false;
					bTriedLeadSwitch = false;
			return true;
		}

		static void Tick(TWeakObjectPtr<UEmberwildSubsystem> WeakSub)
		{
			UEmberwildSubsystem* S = WeakSub.Get();
			UWorld* W = S ? S->GetWorld() : nullptr;
			FBattleDriver& A = Get();
			if (!S || !W || !A.bArmed) return;
			A.SetRate(W, S, RateFor(A, S));

			// The mine sweep never opens a battle engine, so it must run ahead of
			// every Battle.IsValid() gate below — those gates early-returned on
			// every tick and the whole plan sat dead for its entire window.
			if (A.Plan == EPlan::MineLoop)
			{
				TickMineLoop(A, S);
				return;
			}

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

			// Grind dummies are Stone (Def 15), so they soak more turns than the
			// old Burrowmouse targets — give the grind plans extra headroom.
			const bool bGrindPlan = A.Plan == EPlan::GrindFight || A.Plan == EPlan::TobinFight
				|| A.Plan == EPlan::RidgeFight || A.Plan == EPlan::HollisFight || A.Plan == EPlan::ZephyFight;
			const int32 TickCap = bGrindPlan ? 150 : 90;
			if (++A.Ticks > TickCap)
			{
				LogStep(false, FString::Printf(TEXT("driver phase did not conclude within %d ticks"), TickCap));
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

			// TEMP (act-3 debugging): EMBERWILD_DUMP=1 prints the battle UI state
			// machine every tick — the engine's battle narration never reaches the
			// log, so this is the only way to see what the keys actually land on.
			// Remove together with BuildAct3OnlyStages.
			static const bool bDumpBattle = FPlatformMisc::GetEnvironmentVariable(TEXT("EMBERWILD_DUMP")) == TEXT("1");
			if (bDumpBattle)
			{
				const FCreature* PMd = S->Battle->PlayerMon();
				const FCreature* EMd = S->Battle->EnemyMon();
				int32 Salves = 0;
				for (const FItemStack& It : S->Save.Inventory)
					if (It.ItemId == TEXT("salve")) Salves += It.Count;
				LogP(FString::Printf(TEXT("dump t=%d mode=%s cursor=%d items=%d active=%s %d/%d foe=%s %d/%d salves=%d"),
					A.Ticks,
					UI ? *UI->BattleMode : TEXT("?"), UI ? UI->BattleCursor : -1,
					UI ? UI->BattleMenuItems.Num() : -1,
					PMd ? *PMd->DisplayName() : TEXT("?"), PMd ? PMd->CurHP : -1, PMd ? PMd->MaxHP() : -1,
					EMd ? *EMd->DisplayName() : TEXT("?"), EMd ? EMd->CurHP : -1, EMd ? EMd->MaxHP() : -1,
					Salves));
			}

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
				// The battle bag lists raw inventory and opens its cursor at index
				// 0, which is usually fiber — inert in battle. Confirming there does
				// nothing, so the old driver just re-pressed E until the wedge guard
				// bailed out and no core was ever thrown. Walk onto a capture core
				// first, then confirm.
				if (UI->BattleMode == TEXT("bag") && A.Plan == EPlan::WeakenThenCatch)
				{
					int32 CoreIdx = INDEX_NONE;
					for (int32 i = 0; i < S->Save.Inventory.Num(); ++i)
					{
						const FItemDef* D = FEmberwildDB::Item(S->Save.Inventory[i].ItemId);
						if (D && D->Kind == EItemKind::CaptureCore) { CoreIdx = i; break; }
					}
					if (CoreIdx != INDEX_NONE && UI->BattleCursor < CoreIdx)
					{
						if (CoreIdx - UI->BattleCursor > 1 || A.SubMenuTicks == 0)
							LogP(FString::Printf(TEXT("bag cursor %d -> core at %d"),
								UI->BattleCursor, CoreIdx));
						// step at most 3 per tick so the next tick re-reads the cursor
						for (int32 s = 0; s < FMath::Min(CoreIdx - UI->BattleCursor, 3); ++s)
							DriveKey(S, EKeys::Down);
						return;
					}
				}
				// Moves submenu: never blind-confirm cursor 0. Level-ups rotate the
				// learnset window and land status moves there (sprigling's Lv15
				// window starts with growl; an evolved vaultmole's first slot is
				// growl), and the driver confirms blind — against a 3-mon trainer
				// team that stalls out as a guaranteed loss. Step one key per tick
				// toward the strongest usable attack, re-reading the cursor each
				// tick; confirm only when the cursor sits on it.
				if (UI->BattleMode == TEXT("moves") && A.Plan != EPlan::WeakenThenCatch)
				{
					if (const int32 Best = BestMoveIdx(S); UI->BattleCursor != Best)
					{
						DriveKey(S, UI->BattleCursor < Best ? EKeys::Down : EKeys::Up);
						return;
					}
				}
				// A sub-menu can wedge: the cursor lands on an entry that does
				// nothing and every tick re-confirms it until the tick cap kills
				// the whole plan. Bail back to the root menu instead.
				if (++A.SubMenuTicks > 12)
				{
					LogP(FString::Printf(TEXT("stuck in '%s' for %d ticks — backing out"),
						*UI->BattleMode, A.SubMenuTicks));
					A.SubMenuTicks = 0;
					// one Escape -> BuildBattleMenu(), which also parks the
					// cursor on FIGHT (index 0), not RUN
					DriveKey(S, EKeys::Escape);
				}
				else
				{
					DriveE(S);
				}
				return;
			}
			A.SubMenuTicks = 0;

			// root menu

			// Lead with the best type matchup. The default lead is whatever sits at
			// party index 0 — for a Verdant starter against Korrin's Ember team
			// that is the worst possible choice (0.5x out, 2x in), and the old
			// driver only ever switched after the lead fainted.
			if (!A.bTriedLeadSwitch && A.Plan != EPlan::WeakenThenCatch && S->Battle)
			{
				const FCreature* EM = S->Battle->EnemyMon();
				const FCreature* PM = S->Battle->PlayerMon();
				if (EM && PM && EM->Species())
				{
					int32 ActiveIdx = 0;
					for (int32 i = 0; i < S->Save.Party.Num(); ++i)
						if (S->Save.Party[i].CurHP > 0
							&& S->Save.Party[i].Species()
							&& S->Save.Party[i].SpeciesId == PM->SpeciesId
							&& S->Save.Party[i].Level == PM->Level) { ActiveIdx = i; break; }

					const float ActiveMul = FTypeChart::Effectiveness(PM->Species()->Element, EM->Species()->Element);
					int32 BestIdx = ActiveIdx;
					float BestMul = ActiveMul;
					for (int32 i = 0; i < S->Save.Party.Num(); ++i)
					{
						const FCreature& C = S->Save.Party[i];
						if (C.CurHP <= 0 || !C.Species() || i == ActiveIdx) continue;
						const float Mul = FTypeChart::Effectiveness(C.Species()->Element, EM->Species()->Element);
						if (Mul > BestMul) { BestMul = Mul; BestIdx = i; }
					}
					if (BestIdx != ActiveIdx)
					{
						LogP(FString::Printf(TEXT("leading with %s (%.1fx) over %s (%.1fx)"),
							*S->Save.Party[BestIdx].Species()->Name, BestMul,
							*EM->Species()->Name, ActiveMul));
						A.bTriedLeadSwitch = true;
						SwitchToSlot(S, BestIdx);
						return;
					}
					A.bTriedLeadSwitch = true;
				}
			}
			// trainer fights the party is underleveled for: heal from the battle
			// bag when hurting — strongest item in stock (salve 40 > snack 25)
			// ZephyFight included: a 3-mon trainer team drains the party faster
			// than the between-battle Mira heal cycles alone can cover.
			if ((A.Plan == EPlan::TobinFight || A.Plan == EPlan::RidgeFight
				|| A.Plan == EPlan::HollisFight || A.Plan == EPlan::ZephyFight)
				&& UI && UI->BattleMode == TEXT("menu"))
			{
				if (const FCreature* PM = S->Battle->PlayerMon())
				{
					const bool bHurt = PM->MaxHP() > 0 && (float)PM->CurHP / (float)PM->MaxHP() < 0.45f;
					bool bHasHeal = false;
					for (const FItemStack& It : S->Save.Inventory)
					{
						const FItemDef* D = FEmberwildDB::Item(It.ItemId);
						if (D && D->HealAmount > 0 && D->HealAmount < 999) { bHasHeal = true; break; }
					}
					if (bHurt && bHasHeal)
					{
						LogP(FString::Printf(TEXT("HP low (%d/%d) — healing from the battle bag"), PM->CurHP, PM->MaxHP()));
						HealFromBag(S);
						return;
					}
				}
			}
			if (A.Plan == EPlan::WeakenThenCatch)
			{
				if (const FCreature* EM = S->Battle->EnemyMon())
				{
					const float HpFrac = EM->MaxHP() > 0 ? (float)EM->CurHP / (float)EM->MaxHP() : 0.f;
					// One throw per battle. The driver ticks every second, so an
					// unguarded throw emptied all four cores before the first
					// attempt ended — leaving nothing to retry with.
					if (HpFrac <= 0.45f && A.ThrowsThisBattle == 0
						&& S->Save.CountItem(TEXT("core_wood")) > 0)
					{
						++A.ThrowsThisBattle;
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

		/** TEMP (act-3 wiring check): EMBERWILD_ACT3_ONLY=1 fakes the act 1+2
		 *  completion state and runs only Zephy -> Sleeper, so the new stages and
		 *  the ZephyFight plan validate in ~8 minutes instead of a full ~43-minute
		 *  script. Remove once proven. */
		static const TArray<FStage>& BuildAct3OnlyStages()
		{
			static TArray<FStage> Stages =
			{
				{ 1.f, TEXT("title"), [](UEmberwildSubsystem* S, UWorld*)
				{
					DriveE(S);
					LogStep(S->UIRouter.IsValid() && !S->UIRouter->bTitleActive, "title dismissed, world input live");
				} },

				{ 2.f, TEXT("fake-act12"), [](UEmberwildSubsystem* S, UWorld*)
				{
					for (const TCHAR* QId : { TEXT("q_firstcore"), TEXT("q_firstcatch"), TEXT("q_tobin"),
						TEXT("q_professions"), TEXT("q_emberrock"), TEXT("q_sigil") })
						S->Save.DoneQuests.Add(FString(QId));
					S->Save.SetFlag(TEXT("has_sigil"));
					S->Save.Coins = 800;
					S->Save.AddItem(TEXT("verdant_sigil"), 1);
					if (S->Save.Party.IsValidIndex(0))
					{
						// level bump only — keep the creation moveset (tackle first),
						// exactly what organic play produces: FillMoves here would
						// swap in the Lv15 learnset window whose first move is growl,
						// and the driver's cursor-0 confirm would stall every battle
						S->Save.Party[0].Level = 15;
						S->Save.Party[0].CurHP = S->Save.Party[0].MaxHP();
						// Reproduce the real run's party: the act-1/2 catches (a
						// burrowmouse from q_firstcatch and a tidehound from the lake
						// catch), built at the level a shared-XP playthrough carries
						// into act 3. Party=1 was a test artifact — Zephy's 3-mon team
						// beats any solo mon, so the wiring check never validated the
						// scenario the full run actually faces.
						auto Mate = [](const TCHAR* Sp, int32 Lvl)
						{
							FCreature M = FMonsterFactory::Create(Sp, Lvl);
							M.CurHP = M.MaxHP();
							return M;
						};
						S->Save.Party.Add(Mate(TEXT("burrowmouse"), 12));
						S->Save.Party.Add(Mate(TEXT("tidehound"), 12));
					}
					LogP(							"ACT3-ONLY: act 1+2 state faked (quests done, has_sigil, leader Lv15, party of 3)");
				} },

				{ 4.f, TEXT("zephy-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("descending to the Hollow mouth");
					HealAtMira(S);
					BuyShopItem(S, TEXT("salve"), 4);
					LogP("talking to Zephy");
					TalkToNpcAt(S, 14.f, 66.f, TEXT("zephy"));
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_herald")),
						"quest 'q_herald' accepted from Zephy");
					LogStep(S->Battle.IsValid(), "trainer battle vs Zephy started");
					if (S->Battle.IsValid())
					{
						// StopLevel 18 = parity with the ace (Nocturne Lv18). At 16 a
						// solo Lv16 sprigling lost every wiring-test rematch; the real
						// party shares XP across all slots, so grinding to 18 also
						// lifts the catches that fight alongside the starter.
						FBattleDriver::Get().Arm(W, S, EPlan::ZephyFight, 40, 18);
						LogP("driver armed: grind to Lv18 (ace parity) then fight the Hollow Herald (Lv16/16/18)");
					}
				} },

				{ 400.f, TEXT("herald-check"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					FBattleDriver::Get().Stop(W);
					LogStep(S->Save.DefeatedTrainers.Contains(TEXT("trainer_zephy")),
						"Zephy defeated (DefeatedTrainers)");
					LogStep(S->Save.HasFlag(TEXT("herald_down")), "flag herald_down set");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_herald")),
						"quest 'q_herald' auto-completed");
					LogStep(S->Save.CountItem(TEXT("core_moon")) >= 1, "reward received: core_moon");
					LogInv(S);
				} },

				{ 404.f, TEXT("sleeper"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("descending to the Sleeper");
					TalkToNpcAt(S, 55.f, 82.f, TEXT("sleepers"));
					AdvanceDialogueThroughKeys(S);
					bool bGildra = false;
					for (const FCreature& C : S->Save.Party)
						if (C.SpeciesId == TEXT("gildra")) bGildra = true;
					LogStep(S->Save.HasFlag(TEXT("sleeper_awake")), "flag sleeper_awake set");
					LogStep(bGildra, "Gildra joined the party");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_sleeper")),
						"quest 'q_sleeper' auto-completed");
					LogStep(S->Save.CountItem(TEXT("revive_tonic")) >= 1, "reward received: revive_tonic");
					LogInv(S);
				} },

				{ 410.f, TEXT("done"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_herald")),
						"ACT3-ONLY FINAL: q_herald completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_sleeper")),
						"ACT3-ONLY FINAL: q_sleeper completed");
					LogP("ACT3-ONLY finished — quitting");
					if (GEngine) GEngine->Exec(W, TEXT("exit"));
				} },
			};
			return Stages;
		}

		static const TArray<FStage>& BuildStages()
		{
			// TEMP (act-3 wiring check): see BuildAct3OnlyStages above.
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("EMBERWILD_ACT3_ONLY")) == TEXT("1"))
				return BuildAct3OnlyStages();

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
					S->MarkInteractCooldown(0.f); // chained interacts eat the 0.3s window
					InteractWithNearest(S); // CraftStation "forge" -> OpenCrafting
					LogStep(S->UIRouter.IsValid(), "crafting panel opened at the forge");
					DriveE(S); // cursor starts at r_rope (first anywhere-recipe)
					LogStep(S->Save.HasFlag(TEXT("craft_rope")), "braided rope");
					// E confirms the CURRENT cursor — walk the list until the core is
					// crafted. Gate on the craft FLAG, not the item count: the player
					// already carries cores from Brann, so a count check is true before
					// the first press and the loop never runs at all.
					for (int32 i = 0; i < 14 && !S->Save.HasFlag(TEXT("craft_core_wood")); ++i)
					{
						DriveKey(S, EKeys::Down);
						DriveE(S);
					}
					LogStep(S->Save.HasFlag(TEXT("craft_core_wood")), "crafted Timber Core");
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
					// TalkToNpcAt, not InteractWithNearest: the latter grabs whichever
					// interactable is closest, and Maren stands in the open square.
					TalkToNpcAt(S, 7.5f, -14.6f, TEXT("maren"));
					// maren_idle is a Yes/No choice; either branch offers the quest.
					// AdvanceDialogueThroughKeys answers the first choice with Y ("yes").
					AdvanceDialogueThroughKeys(S);
					if (!S->Save.ActiveQuests.Contains(TEXT("q_firstcatch")))
					{
						// q_firstcatch is prereq-gated on q_firstcore. If the forge
						// craft slipped a stage, the offer is silently refused — say so
						// and try once more rather than reporting a bare FAIL.
						if (S->Save.DoneQuests.Contains(TEXT("q_firstcore")))
							LogP("first offer refused despite q_firstcore done — retrying");
						else
							LogP(FString::Printf(TEXT("first offer refused: q_firstcore %s"),
								S->Save.ActiveQuests.Contains(TEXT("q_firstcore"))
									? TEXT("still active") : TEXT("not accepted")));
						TalkToNpcAt(S, 7.5f, -14.6f, TEXT("maren"));
						AdvanceDialogueThroughKeys(S);
					}
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
						// Dice: a throw at ~36% HP lands about half the time, and a
						// missed catch is strictly worse than no catch — an empty
						// slot makes every later trainer fight much harder. Budget
						// generously and restock cores between attempts.
						FBattleDriver::Get().Arm(W, S, EPlan::WeakenThenCatch, 8);
						LogP("driver armed: fight until foe <=45% HP, throw cores, retry up to 8 battles");
					}
				} },

				{ 110.f, TEXT("maren-collect"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.HasFlag(TEXT("caught_any")), "a creature was caught (flag caught_any)");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcatch")),
						"quest 'q_firstcatch' auto-completed");
					LogStep(S->Save.Party.Num() >= 2,
						FString::Printf(TEXT("party grew via capture (%d members)"), S->Save.Party.Num()));

					// second talk: with caught_first already set the tree routes to
					// maren_yes (Chef gift; the quest offer no-ops on a done quest)
					TalkToNpcAt(S, 7.5f, -14.6f, TEXT("maren"));
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Save.HasFlag(TEXT("caught_first")), "flag caught_first set (quest completion)");
					LogInv(S);
				} },

			// A missed catch is the single most expensive dice roll in the run:
			// an empty party slot loses every later trainer fight. If the first
			// window closed without a capture, keep throwing.
			{ 118.f, TEXT("catch-retry"), [](UEmberwildSubsystem* S, UWorld* W)
			{
				if (S->Save.Party.Num() >= 2) return;
				LogP("no capture yet — re-arming the catch plan");
				S->StartWildBattle(FMonsterFactory::Create(TEXT("burrowmouse"), 2));
				if (S->Battle.IsValid())
					FBattleDriver::Get().Arm(W, S, EPlan::WeakenThenCatch, 10);
			} },

			// ================================================== GRIND + TOBIN
			{ 150.f, TEXT("grind-battle"), [](UEmberwildSubsystem* S, UWorld* W)
			{
					// Wilds must stay winnable: Lv7s beat the Lv5 starter every time
					// and the grind made zero progress. Lv5s are a consistent win.
					S->StartWildBattle(FMonsterFactory::Create(TEXT("burrowmouse"), 5));
					if (S->Battle.IsValid())
					{
						FBattleDriver::Get().Arm(W, S, EPlan::GrindFight, 24, 10, 5);
						LogP("driver armed: grind battles for XP (early-stop at starter Lv9)");
					}
				} },

				{ 310.f, TEXT("tobin-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("talking to Tobin");
					TalkToNpcAt(S, 26.f, -26.f, TEXT("tobin"));
					// tobin_fight ends in SpawnsTrainer -> dialogue ends -> battle opens
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Battle.IsValid(), "trainer battle vs Tobin started");
					if (S->Battle.IsValid())
					{						// In-battle ticks run at 0.15s, so a grind win costs ~5s of wall
						// clock instead of ~25s. StopLevel 10, not 12: Tobin's team is
						// burrowmouse Lv5 / flitwing Lv6, and grinding all the way to
						// 12 burned the whole battle budget so the rematch never ran.
						FBattleDriver::Get().Arm(W, S, EPlan::TobinFight, 30, 10);
						LogP("driver armed: fight Tobin (grind to Lv10 first, then rematch)");
					}
				} },

				// Second chance: the driver spends its budget grinding and can time
				// out before the rematch lands. Re-arm once if Tobin is still up.
				{ 420.f, TEXT("tobin-retry"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					if (S->Save.DefeatedTrainers.Contains(TEXT("trainer_tobin"))) return;
					LogP("Tobin still standing — re-arming the fight");
					FBattleDriver::Get().Arm(W, S, EPlan::TobinFight, 24, 10);
				} },

				{ 450.f, TEXT("tobin-check"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.DefeatedTrainers.Contains(TEXT("trainer_tobin")),
						"Tobin defeated (DefeatedTrainers)");
					LogStep(S->Save.HasFlag(TEXT("beat_tobin")), "flag beat_tobin set");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_tobin")), "quest 'q_tobin' auto-completed");
					LogStep(S->Save.Coins >= 240, FString::Printf(TEXT("reward coins collected (%d)"), S->Save.Coins));
					LogInv(S);
				} },

				// ================================================== ACT 2: JUNA / PROFESSIONS
				{ 454.f, TEXT("juna"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogP("talking to Juna");
					TalkToNpcAt(S, -5.2f, -13.4f, TEXT("juna"));
					LogStep(S->Dialogue.IsValid(), "dialogue runtime started (juna_intro)");
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_professions")),
						"Juna quest 'q_professions' accepted");
					LogInv(S);
				} },

				{ 458.f, TEXT("prof-gather"), [](UEmberwildSubsystem* S, UWorld*)
				{
					// Forager 1->2 (70xp at 8xp/gather): 6 sweeps of the 3 sunfiber
					// patches + 3 sweeps of the bitterleaf row (nodes respawn 40s,
					// so distant patches stay alive between sweeps).
					const FVector2D Fiber[] = { {16.f, 4.f}, {-20.f, -10.f}, {22.f, -18.f} };
					const FVector2D Bitter[] = { {-9.f, -25.5f}, {-14.f, 6.f}, {-6.f, 22.f} };
					for (int32 Sweep = 0; Sweep < 6; ++Sweep)
						for (const FVector2D& At : Fiber)
						{
							TeleportTo(S, At.X, At.Y);
							S->MarkInteractCooldown(0.f);
							InteractWithNearest(S);
						}
					for (int32 Sweep = 0; Sweep < 3; ++Sweep)
						for (const FVector2D& At : Bitter)
						{
							TeleportTo(S, At.X, At.Y);
							S->MarkInteractCooldown(0.f);
							InteractWithNearest(S);
						}
					LogP(FString::Printf(TEXT("Forager Lv%d(%d)"),
						S->Save.ProfLevel(EProfessionId::Forager), S->Save.ProfXpOf(EProfessionId::Forager)));
				} },

				{ 466.f, TEXT("prof-buy"), [](UEmberwildSubsystem* S, UWorld*)
				{
					// Stock up for the profession plan: 12x r_rope (3 fiber each =
					// 36) plus 7x r_core_wood (2 stone each = 14, plus rope).
					// All materials are general-store stock (DoBuy reads Parts[0]).
					BuyShopItem(S, TEXT("fiber"), 45);
					BuyShopItem(S, TEXT("stone"), 20);
					BuyShopItem(S, TEXT("rope"), 8);
					LogInv(S);
				} },

				{ 470.f, TEXT("prof-craft"), [](UEmberwildSubsystem* S, UWorld*)
				{
					// Tailor 1->2 (70xp): r_cloth needs Tailor Lv2, so it cannot be
					// the bootstrap recipe. r_rope is Lv1 and gives 6xp for 3 fiber
					// -> 12 ropes = 72xp. It is an "anywhere" recipe, so it is
					// cursor 0 at every station.
					CraftAt(S, 16.4f, -12.2f, 0, 12); // workbench, r_rope
					LogP(FString::Printf(TEXT("Tailor Lv%d(%d)"),
						S->Save.ProfLevel(EProfessionId::Tailor), S->Save.ProfXpOf(EProfessionId::Tailor)));

					// Smith 1->2 (70xp): r_core_wood is Lv1, 10xp, and needs
					// stone 2 + rope 1 -> 7 crafts = 70xp. forge list order:
					// r_rope(0), r_cloth(1), r_copper(2), r_iron(3), r_salve(4),
					// r_purge(5), r_revive(6), r_core_wood(7).
					CraftAt(S, -16.4f, -12.2f, 7, 7);
					LogP(FString::Printf(TEXT("Smith Lv%d(%d)"),
						S->Save.ProfLevel(EProfessionId::Smith), S->Save.ProfXpOf(EProfessionId::Smith)));
				} },

				{ 478.f, TEXT("prof-check"), [](UEmberwildSubsystem* S, UWorld*)
				{
					auto Lv = [&](EProfessionId P) { return S->Save.ProfLevel(P); };
					LogStep(Lv(EProfessionId::Forager) >= 2, "Forager reached Lv2");
					LogStep(Lv(EProfessionId::Tailor) >= 2, "Tailor reached Lv2");
					LogStep(Lv(EProfessionId::Smith) >= 2, "Smith reached Lv2");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_professions")),
						"quest 'q_professions' auto-completed (prof3_lv2)");
					LogStep(S->Save.HasFlag(TEXT("prof_sworn")), "flag prof_sworn set");
					LogStep(S->Save.CountItem(TEXT("forager_gloves")) >= 1,
						"reward received: forager_gloves");
					LogInv(S);
				} },

				// ================================================== ACT 2: ODESSA -> KORRIN
				// Korrin's team is Ember/Stone/Ember, and the mandatory Verdant
				// starter is hard-countered by it (Verdant -> Ember 0.5x while
				// Ember -> Verdant 2.0x), so grinding alone never wins the fight.
				// Tide is the mirror: Tide -> Ember 2.0x, Ember -> Tide 0.5x.
				// XP goes to every living party member, so this mon levels with
				// the starter through the ridge grind and is ready in time.
				{ 480.f, TEXT("catch-tide"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					if (S->Save.Party.Num() >= 3) return;
					LogP("catching a Tide mon to cover Korrin's Ember team");
					S->StartWildBattle(FMonsterFactory::Create(TEXT("tidehound"), 10));
					if (S->Battle.IsValid())
					{
						FBattleDriver::Get().Arm(W, S, EPlan::WeakenThenCatch, 6);
						FBattleDriver::Get().SetMinParty(3);
					}
				} },

				{ 505.f, TEXT("catch-tide-retry"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					if (S->Save.Party.Num() >= 3) return;
					LogP("no Tide catch yet — re-arming");
					S->StartWildBattle(FMonsterFactory::Create(TEXT("tidehound"), 10));
					if (S->Battle.IsValid())
					{
						FBattleDriver::Get().Arm(W, S, EPlan::WeakenThenCatch, 8);
						FBattleDriver::Get().SetMinParty(3);
					}
				} },

				{ 512.f, TEXT("odessa"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogP("talking to Odessa");
					TalkToNpcAt(S, 2.4f, 4.6f, TEXT("odessa"));
					AdvanceDialogueThroughKeys(S); // answers Y: odessa_intro -> odessa_yes (offers q_emberrock)
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_emberrock")),
						"Odessa quest 'q_emberrock' accepted");
					LogInv(S);
				} },

				{ 522.f, TEXT("korrin-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("talking to Korrin");
					TalkToNpcAt(S, 70.f, 10.f, TEXT("korrin"));
					// korrin_fight ends in SpawnsTrainer -> dialogue ends -> battle opens
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Battle.IsValid(), "trainer battle vs Korrin started");if (S->Battle.IsValid())
					{		// The caught burrowmouse is Lv2, so this is effectively a solo
						// fight. Rematching at Lv10 lost to Korrin's Lv9/10/9 team,
						// while the Hollis Lv12/13 team fell later at ~Lv13 — so grind
						// to 13, not 11, before the rematch.
						FBattleDriver::Get().Arm(W, S, EPlan::RidgeFight, 40, 13);
						LogP("driver armed: grind to Lv13 then fight Korrin (bag-heals, salves restocked)");
					}
				} },

				// Second chance: the grind-to-Lv11 loop plus a 3-mon rematch does
				// not reliably fit the 492->630 window, and the driver spends the
				// tail of its budget teleporting to heal and restock. Re-arm once
				// (same pattern as tobin-retry) rather than declaring the act over
				// with Korrin still standing.
				{ 575.f, TEXT("korrin-retry"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					if (S->Save.DefeatedTrainers.Contains(TEXT("trainer_korrin"))) return;
					LogP("Korrin still standing - re-arming the fight");
					FBattleDriver::Get().Arm(W, S, EPlan::RidgeFight, 40, 13);
				} },

				{ 668.f, TEXT("korrin-check"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.DefeatedTrainers.Contains(TEXT("trainer_korrin")),
						"Korrin defeated (DefeatedTrainers)");
					LogStep(S->Save.HasFlag(TEXT("ridge_opened")), "flag ridge_opened set");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_emberrock")),
						"quest 'q_emberrock' auto-completed");
					LogStep(S->Save.CountItem(TEXT("sigil_piece_a")) >= 1, "got sigil_piece_a (Root, from Korrin)");
					LogStep(S->Save.CountItem(TEXT("miner_pick")) >= 1, "reward received: miner_pick");
					LogStep(S->Save.Coins >= 380, FString::Printf(TEXT("reward coins collected (%d)"), S->Save.Coins));
					LogInv(S);
				} },

				{ 671.f, TEXT("heal-up"), [](UEmberwildSubsystem* S, UWorld*)
				{ HealAtMira(S); } },

				// ================================================== ACT 2: SELA + HOLLIS
				{ 674.f, TEXT("sela"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogP("talking to Sela");
					TalkToNpcAt(S, 58.f, 30.f, TEXT("sela"));
					AdvanceDialogueThroughKeys(S); // answers Y: sela_intro -> sela_yes (offers q_sigil)
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_sigil")),
						"Sela quest 'q_sigil' accepted");
					LogInv(S);
				} },

				{ 680.f, TEXT("hollis-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("talking to Hollis");
					TalkToNpcAt(S, 6.f, -60.f, TEXT("hollis"));
					AdvanceDialogueThroughKeys(S);
					LogStep(S->Battle.IsValid(), "trainer battle vs Hollis started");
					if (S->Battle.IsValid())
					{
						FBattleDriver::Get().Arm(W, S, EPlan::HollisFight, 30, 14);
						LogP("driver armed: grind to Lv12 then fight Hollis (bag-heals, salves restocked)");
					}
				} },

				{ 790.f, TEXT("hollis-check"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogStep(S->Save.DefeatedTrainers.Contains(TEXT("trainer_hollis")),
						"Hollis defeated (DefeatedTrainers)");
					LogStep(S->Save.HasFlag(TEXT("beat_hollis")), "flag beat_hollis set");
					LogStep(S->Save.CountItem(TEXT("sigil_piece_b")) >= 1, "got sigil_piece_b (Tide, from Hollis)");
					LogInv(S);
				} },

				{ 794.f, TEXT("buy-salves"), [](UEmberwildSubsystem* S, UWorld*)
				{
					for (int32 i = 0; i < 4; ++i)
						BuyShopItem(S, TEXT("salve"));
					LogInv(S);
				} },

				{ 800.f, TEXT("mine-loop"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("driver armed: level Miner to 2 on meadow rocks, then camp the ridge copper veins until sigil_piece_c drops (1-in-10 per gather)");
					FBattleDriver::Get().Arm(W, S, EPlan::MineLoop, 99);
				} },

				// Budget: ~175s to train Miner 1->2 (9 gathers over 4 nodes at a
				// 70s respawn), then ~2 copper gathers/70s (2 veins, iron needs
				// Miner 4). ~31 gathers leaves a ~3% chance of no rare drop.
				{ 2080.f, TEXT("mine-check"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					FBattleDriver::Get().Stop(W);
					LogStep(S->Save.CountItem(TEXT("sigil_piece_c")) >= 1,
						FString::Printf(TEXT("got sigil_piece_c (Ember, mined: %d)"), S->Save.CountItem(TEXT("sigil_piece_c"))));
					LogInv(S);
				} },

				{ 2084.f, TEXT("sela-final"), [](UEmberwildSubsystem* S, UWorld*)
				{
					LogP("turning the three fragments in to Sela");
					TalkToNpcAt(S, 58.f, 30.f, TEXT("sela"));
					AdvanceDialogueThroughKeys(S); // sela_loop; AutoCompleteAll completes q_sigil
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_sigil")), "quest 'q_sigil' auto-completed");
					LogStep(S->Save.HasFlag(TEXT("has_sigil")), "flag has_sigil set");
					LogStep(S->Save.CountItem(TEXT("verdant_sigil")) >= 1, "reward received: verdant_sigil");
					LogInv(S);
				} },

				// ================================================== ACT 3: ZEPHY + THE SLEEPER
				{ 2094.f, TEXT("zephy-battle"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("descending to the Hollow mouth");
					HealAtMira(S); // HP carries from the sigil grind; walk in rested
					BuyShopItem(S, TEXT("salve"), 4);
					LogP("talking to Zephy");
					TalkToNpcAt(S, 14.f, 66.f, TEXT("zephy"));
					AdvanceDialogueThroughKeys(S); // zephy_fight offers q_herald, then SpawnsTrainer
					LogStep(S->Save.ActiveQuests.Contains(TEXT("q_herald")),
						"quest 'q_herald' accepted from Zephy");
					LogStep(S->Battle.IsValid(), "trainer battle vs Zephy started");
					if (S->Battle.IsValid())
					{
						// StopLevel 18 = parity with the ace (Nocturne Lv18). At 16 a
						// solo Lv16 sprigling lost every wiring-test rematch; the real
						// party shares XP across all slots, so grinding to 18 also
						// lifts the catches that fight alongside the starter.
						FBattleDriver::Get().Arm(W, S, EPlan::ZephyFight, 40, 18);
						LogP("driver armed: grind to Lv18 (ace parity) then fight the Hollow Herald (Lv16/16/18)");
					}
				} },

				{ 2500.f, TEXT("herald-check"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					FBattleDriver::Get().Stop(W);
					LogStep(S->Save.DefeatedTrainers.Contains(TEXT("trainer_zephy")),
						"Zephy defeated (DefeatedTrainers)");
					LogStep(S->Save.HasFlag(TEXT("herald_down")), "flag herald_down set");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_herald")),
						"quest 'q_herald' auto-completed");
					LogStep(S->Save.CountItem(TEXT("core_moon")) >= 1, "reward received: core_moon");
					LogInv(S);
				} },

				{ 2504.f, TEXT("sleeper"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogP("descending to the Sleeper");
					TalkToNpcAt(S, 55.f, 82.f, TEXT("sleepers"));
					AdvanceDialogueThroughKeys(S); // sleeper_final offers q_sleeper; Y picks wake
					bool bGildra = false;
					for (const FCreature& C : S->Save.Party)
						if (C.SpeciesId == TEXT("gildra")) bGildra = true;
					LogStep(S->Save.HasFlag(TEXT("sleeper_awake")), "flag sleeper_awake set");
					LogStep(bGildra, "Gildra joined the party");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_sleeper")),
						"quest 'q_sleeper' auto-completed");
					LogStep(S->Save.CountItem(TEXT("revive_tonic")) >= 1, "reward received: revive_tonic");
					LogInv(S);
				} },

				// ================================================== summary + quit
				{ 2510.f, TEXT("done"), [](UEmberwildSubsystem* S, UWorld* W)
				{
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcore")),
						"FINAL: quest 1 (q_firstcore) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_firstcatch")),
						"FINAL: quest 2 (q_firstcatch) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_tobin")),
						"FINAL: act 1 rival (q_tobin) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_professions")),
						"FINAL: act 2 professions (q_professions) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_emberrock")),
						"FINAL: act 2 ridge (q_emberrock) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_sigil")),
						"FINAL: act 2 sigil (q_sigil) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_herald")),
						"FINAL: act 3 herald (q_herald) completed");
					LogStep(S->Save.DoneQuests.Contains(TEXT("q_sleeper")),
						"FINAL: act 3 sleeper (q_sleeper) completed");
					LogP("playtest finished — quitting");
					if (GEngine) GEngine->Exec(W, TEXT("exit"));
				} },
			};
			// Stage times drive a catch-up loop: any stage whose At is <= the
			// previous one fires in the same tick as its predecessor, with no
			// frame in between. That silently broke the Korrin trainer fight.
			for (int32 i = 1; i < Stages.Num(); ++i)
				checkf(Stages[i].At > Stages[i - 1].At,
					TEXT("autopilot stage '%s' (t+%g) must come after '%s' (t+%g)"),
					Stages[i].Name, Stages[i].At, Stages[i - 1].Name, Stages[i - 1].At);
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
#ifdef EMBERWILD_AUTOPILOT
	Sub->bSuppressEncounters = true; // the script arms its own battles
	// A playtest must start from a known state. BootWorld() loads
	// Saved/SaveGames/emberwild_save_v1.json whenever it exists, so the previous
	// run's autosave arrives with quests already done — the quest-offer
	// assertions then fail for no reason (Accept() correctly refuses a finished
	// quest). Reset before the first stage runs.
	if (FEmberwildSave::SaveExists())
		LogP("previous save found - resetting to a fresh game for this playtest");
	Sub->StartNewGame();
#endif
	LogP("armed: quest1+2 -> grind -> Tobin -> Juna professions -> Odessa -> Korrin -> Sela -> Hollis -> mine piece C -> Sela turn-in -> Zephy -> wake the Sleeper");
	if (UWorld* W = Sub->GetWorld())
		W->GetTimerManager().SetTimer(A.StepHandle, []() { FAutopilot::Get().RunStages(); }, 0.25f, true);
}

#else

void EmberwildStartAutopilot(UEmberwildSubsystem* Sub)
{
}

#endif // EMBERWILD_AUTOPILOT
