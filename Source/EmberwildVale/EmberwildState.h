// Emberwild Vale — persistent state, creature model, JSON save/load, toasts.
// Port of the Unity original's GameState.cs + the Creature half of GameTypes.cs.

#pragma once

#include "CoreMinimal.h"
#include "EmberwildData.h"
#include "EmberwildState.generated.h"

/** One creature in the party or box. Plain data, mirrored on the save. */
USTRUCT(BlueprintType)
struct FCreature
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString SpeciesId;
	UPROPERTY(EditAnywhere) FString Nickname;
	UPROPERTY(EditAnywhere) int32 Level = 1;
	UPROPERTY(EditAnywhere) int32 XP = 0;
	UPROPERTY(EditAnywhere) int32 CurHP = 1;
	UPROPERTY(EditAnywhere) TArray<FString> Moves;
	UPROPERTY(EditAnywhere) FString Weapon;   // equipped creature-weapon item id
	UPROPERTY(EditAnywhere) FString Armor;    // equipped creature-armor item id
	UPROPERTY(EditAnywhere) TMap<FString, int32> MovePP;
	UPROPERTY(EditAnywhere) FString Status;   // "", "poison", "burn", "sleep", "paralyze"
	UPROPERTY(EditAnywhere) int32 StatusTurns = 0;
	UPROPERTY(EditAnywhere) bool bShiny = false;
	UPROPERTY(EditAnywhere) FString Uid;

	const FSpeciesDef* Species() const;

	int32 MaxHP() const;
	int32 ATK() const;
	int32 DEF() const;
	int32 SPD() const;

	int32 GearAtk() const;
	int32 GearDef() const;
	int32 GearSpd() const;
	int32 GearHp() const;

	/** Sum every "prefix+N" segment (e.g. "atk+3|spd+1") on one equipped item. */
	static int32 SumGear(const FString& ItemId, const TCHAR* Prefix);

	/** Identity is the Uid: arrays of creatures search/compare by it. */
	bool operator==(const FCreature& O) const { return Uid == O.Uid; }

	FString DisplayName() const;
	bool KnowsMove(const FString& MoveId) const { return Moves.Contains(MoveId); }
	int32 CurrentPP(const FString& MoveId) const;
	void SetPP(const FString& MoveId, int32 PP) { MovePP.Add(MoveId, PP); }
	void NormalizeMoves();
	void RecalcAfterLevel() { CurHP = FMath::Min(CurHP, MaxHP()); }
};

/** Serializable mirror of MovePP for JSON (one entry per move). */
USTRUCT()
struct FMovePPEntry
{
	GENERATED_BODY()

	UPROPERTY() FString Id;
	UPROPERTY() int32 PP = 0;
	FMovePPEntry() {}
	FMovePPEntry(const FString& InId, int32 InPP) : Id(InId), PP(InPP) {}
};

/** All persistent state. Serialized to JSON into a USaveGame slot. */
USTRUCT()
struct FGameStateSave
{
	GENERATED_BODY()

	// ---- identity ----
	UPROPERTY() FString PlayerName = TEXT("Robin");
	UPROPERTY() int32 Coins = 120;
	UPROPERTY() int32 PlaySeconds = 0;

	// ---- professions: every trade starts at level 1 ----
	UPROPERTY() TArray<int32> ProfLevels = { 1, 1, 1, 1, 1, 1 };
	UPROPERTY() TArray<int32> ProfXp = { 0, 0, 0, 0, 0, 0 };

	// ---- weapon mastery (creature gear classes) ----
	UPROPERTY() TArray<int32> WeaponLevels = { 0, 0, 0, 0, 0 };
	UPROPERTY() TArray<int32> WeaponXp = { 0, 0, 0, 0, 0 };
	// technique PP per move is runtime-only: NOT saved, refills on load/heal
	TMap<FString, int32> TechPP;

	// ---- inventory ----
	UPROPERTY() TArray<FItemStack> Inventory;
	UPROPERTY() FString EquippedItem;

	// ---- player melee combat (overworld) ----
	UPROPERTY() FString PlayerWeapon;       // "" = bare hands
	UPROPERTY() int32 PlayerWeaponLevel = 0;
	UPROPERTY() int32 PlayerWeaponXp = 0;

	// ---- Lucky Snorlax shared progressive jackpot ----
	UPROPERTY() int32 JackpotPot = 150;

	// ---- party & box ----
	UPROPERTY() TArray<FCreature> Party;
	UPROPERTY() TArray<FCreature> Box;
	UPROPERTY() TMap<FString, FMovePPEntry> MovePPMirror; // flattened PP entries

	// ---- story ----
	UPROPERTY() TArray<FString> Flags;
	UPROPERTY() TArray<FString> ActiveQuests;
	UPROPERTY() TArray<FString> DoneQuests;
	UPROPERTY() TArray<FString> DefeatedTrainers;

	// ---- journal / bestiary ----
	UPROPERTY() TArray<FString> SeenSpecies;
	UPROPERTY() TArray<FString> CaughtSpecies;
	UPROPERTY() int32 CatchCount = 0;
	UPROPERTY() int32 BattleWins = 0;

	// ---- world ----
	UPROPERTY() float PlayerX = 0.f;
	UPROPERTY() float PlayerZ = -16.f;
	UPROPERTY() FString CurZone = TEXT("meadow");

	// ---------------- inventory helpers ----------------
	int32 CountItem(const FString& Id) const
	{
		for (const FItemStack& S : Inventory) if (S.ItemId == Id) return S.Count;
		return 0;
	}
	void AddItem(const FString& Id, int32 N)
	{
		if (N <= 0) return;
		for (FItemStack& S : Inventory) if (S.ItemId == Id) { S.Count += N; return; }
		(void)Inventory.Emplace_GetRef(Id, N);
	}
	bool RemoveItem(const FString& Id, int32 N)
	{
		if (CountItem(Id) < N) return false;
		for (int32 i = 0; i < Inventory.Num(); ++i)
			if (Inventory[i].ItemId == Id)
			{
				Inventory[i].Count -= N;
				if (Inventory[i].Count <= 0) Inventory.RemoveAt(i);
				return true;
			}
		return false;
	}

	// ---------------- flags ----------------
	bool HasFlag(const FString& F) const { return !F.IsEmpty() && Flags.Contains(F); }
	void SetFlag(const FString& F) { if (!F.IsEmpty() && !Flags.Contains(F)) Flags.Add(F); }

	int32 ProfLevel(EProfessionId P) const { return ProfLevels.IsValidIndex((int32)P) ? ProfLevels[(int32)P] : 1; }
	int32 ProfXpOf(EProfessionId P) const { return ProfXp.IsValidIndex((int32)P) ? ProfXp[(int32)P] : 0; }
	void AddProfXp(EProfessionId P, int32 Xp) { if (ProfXp.IsValidIndex((int32)P)) ProfXp[(int32)P] += Xp; }

	// ---------------- party helpers ----------------
	const FCreature* FirstHealthy() const
	{
		for (const FCreature& C : Party) if (C.CurHP > 0) return &C;
		return nullptr;
	}
	bool PartyAlive() const { return FirstHealthy() != nullptr; }
	void HealAll();

	/** Copy runtime MovePP into the flat mirror (call before serializing). */
	void SyncPPForSave();
	/** Rebuild each creature's MovePP from the mirror; missing moves get full PP. */
	void RestorePPAfterLoad();
};

// ---------------------------------------------------------------- toasts
struct FEmberwildToast
{
	// pending toast messages; the HUD drains these every frame
	static TArray<FString> Pending;

	static void Push(const FString& Msg)
	{
		Pending.Add(Msg);
		if (Pending.Num() > 6) Pending.RemoveAt(0);
	}
	static void Clear() { Pending.Reset(); }
	static bool Drain(FString& OutMsg)
	{
		if (Pending.Num() == 0) return false;
		OutMsg = Pending[0];
		Pending.RemoveAt(0);
		return true;
	}
};

// ---------------------------------------------------------------- persistence
struct FEmberwildSave
{
	static const TCHAR* SlotName() { return TEXT("emberwild_save_v1"); }
	static const int32 UserIndex = 0;

	static bool SaveExists();
	static void Save(FGameStateSave& State);   // syncs PP mirror, writes JSON
	static bool Load(FGameStateSave& OutState); // returns false if absent/corrupt
	static void DeleteSave();
};
