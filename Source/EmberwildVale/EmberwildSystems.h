// Emberwild Vale — core systems.
// Ports of the Unity original's ProfessionSystem / WeaponMastery / QuestSystem /
// MonsterFactory / DialogueRuntime / BattleEngine. Pure logic, no actors.

#pragma once

#include "CoreMinimal.h"
#include "EmberwildData.h"
#include "EmberwildState.h"
#include "EmberwildDB.h"
#include "EmberwildSystems.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnBattleLog, const FString& /*Msg*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCreatureEvolved, const FCreature& /*C*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLineShown, const FDialogueLine& /*Line*/);
DECLARE_MULTICAST_DELEGATE(FOnDialogueEnded);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTrainerSpawn, const FTrainerDef& /*Trainer*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCreatureGift, const FCreature& /*C*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnShopRequested, const FString& /*ShopId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCraftRequested, const FString& /*StationId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestAccepted, const FQuestDef& /*Quest*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnQuestCompleted, const FQuestDef& /*Quest*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSfx, const FString& /*Id*/);

// ---------------------------------------------------------------- professions
struct FProfessionSystem
{
	static constexpr int32 MaxLevel = 10;
	static const TCHAR* Name(EProfessionId P);
	static const TCHAR* Icon(EProfessionId P);

	static int32 XpToNext(int32 Level)
	{
		return Level >= MaxLevel ? TNumericLimits<int32>::Max() : 40 + Level * 30;
	}
	static int32 LevelYieldBonus(int32 Level)
	{
		if (Level >= 8) return 2;
		if (Level >= 4) return 1;
		return 0;
	}
	/** How many of nodeId a player gathers; Xp via out param. 0 = locked. */
	static int32 GatherYield(FGameStateSave& Save, const FString& NodeId, int32& OutXp);
	static void AddXp(FGameStateSave& Save, EProfessionId P, int32 Xp);
	static bool CanCraft(const FGameStateSave& Save, const FRecipe& R, const FString& CurrentStation);
	static FString CraftBlocker(const FGameStateSave& Save, const FRecipe& R, const FString& CurrentStation);
	static const TCHAR* StationName(const FString& Station);
};

// ---------------------------------------------------------------- weapon mastery
struct FWeaponMastery
{
	static constexpr int32 MaxLevel = 100;
	static constexpr float BonusPerLevel = 0.006f;     // +0.6% ATK per level -> +60% at Lv100
	static constexpr int32 XpPerKO = 8;                // weapon XP: KO with the class equipped
	static constexpr float DefReducePerLevel = 0.005f; // -0.5% damage taken per level -> -50%
	static constexpr int32 ArmorXpPerWin = 6;          // armor XP: victory while wearing the class

	struct FTechnique
	{
		const TCHAR* MoveId;
		int32 UnlockLevel;
		int32 BasePP;
	};
	static const TArray<FTechnique>& Techniques();

	static EWeaponClass ClassOfTechnique(const FString& MoveId);
	static EWeaponClass ClassOf(const FString& ItemId);
	static const TCHAR* Name(EWeaponClass WC);

	static float AttackMultiplier(const FGameStateSave& Save, EWeaponClass WC);
	static float DefenseMultiplier(const FGameStateSave& Save, EWeaponClass WC);
	static int32 DefenseReducePercent(const FGameStateSave& Save, EWeaponClass WC);

	static int32 TechPP(const FGameStateSave& Save, EWeaponClass WC, const FString& MoveId);
	static void SpendTechPP(FGameStateSave& Save, const FString& MoveId);
	static void RestoreTechPP(FGameStateSave& Save, const FString& MoveId);
	static int32 BasePP(const FString& MoveId);

	static int32 Level(const FGameStateSave& Save, EWeaponClass WC) { return Save.WeaponLevels.IsValidIndex((int32)WC) ? Save.WeaponLevels[(int32)WC] : 0; }
	static int32 Xp(const FGameStateSave& Save, EWeaponClass WC)    { return Save.WeaponXp.IsValidIndex((int32)WC) ? Save.WeaponXp[(int32)WC] : 0; }
	static int32 XpToNext(int32 Level) { return Level >= MaxLevel ? TNumericLimits<int32>::Max() : 10 + Level * 4; }
	static void AddXp(FGameStateSave& Save, EWeaponClass WC, int32 Xp);
	static bool IsTechniqueAvailable(FGameStateSave& Save, const FCreature& C, const FString& MoveId);
};

// ---------------------------------------------------------------- quests
struct FQuestSystem
{
	static FOnQuestAccepted OnQuestAccepted;
	static FOnQuestCompleted OnQuestCompleted;

	static bool IsOffered(const FGameStateSave& S, const FQuestDef& Q);
	static bool IsActive(const FGameStateSave& S, const FString& QuestId) { return S.ActiveQuests.Contains(QuestId); }
	static bool Accept(FGameStateSave& S, const FString& QuestId);
	static bool Complete(FGameStateSave& S, const FString& QuestId);
	static bool IsComplete(const FGameStateSave& S, const FQuestDef& Q);
	static bool IsObjectiveMet(const FGameStateSave& S, const FString& Obj);
	static FString ObjectiveText(const FGameStateSave& S, const FString& Obj);
	static void AutoCompleteAll(FGameStateSave& S);
	static const FQuestDef* Get(const FString& Id) { return FEmberwildDB::Quest(Id); }
};

// ---------------------------------------------------------------- monster factory
struct FMonsterFactory
{
	static FCreature Create(const FString& SpeciesId, int32 Level);
	static void FillMoves(FCreature& C, const FSpeciesDef& Sp);
	static FCreature CreateWildForZone(const FString& Zone, const FGameStateSave& Save);
	static FCreature FromTrainerEntry(const FString& Entry);
};

// ---------------------------------------------------------------- player melee (overworld)
struct FPlayerCombat
{
	static constexpr int32 MaxMasteryLevel = 50;
	static constexpr int32 ClubDmg = 8, BladeDmg = 14, SpearDmg = 11, AxeDmg = 20;
	static constexpr float ClubCd = 0.9f, BladeCd = 0.65f, SpearCd = 0.75f, AxeCd = 1.3f;
	static constexpr float SpearReach = 3.6f;
	static constexpr float ClubSwing = 0.22f, BladeSwing = 0.18f, SpearSwing = 0.22f, AxeSwing = 0.3f;
	static constexpr int32 XpPerHit = 2;
	static constexpr int32 XpPerKO = 8;

	static int32 BaseDamage(const FString& WeaponId);
	static float Cooldown(const FString& WeaponId);
	static float SwingSeconds(const FString& WeaponId);
	static float Reach(const FString& WeaponId);
	static EPlayerWeaponKind KindOf(const FString& WeaponId);
	static FString NameOf(const FString& WeaponId);
	static int32 DamageRoll(const FGameStateSave& Save);
	static float CooldownOf(const FGameStateSave& Save)   { return Cooldown(Save.PlayerWeapon); }
	static float SwingSecondsOf(const FGameStateSave& S)  { return SwingSeconds(S.PlayerWeapon); }
	static float ReachOf(const FGameStateSave& Save)      { return Reach(Save.PlayerWeapon); }
	static int32 XpToNext(int32 Level) { return 20 + Level * 10; }
	static void AddXp(FGameStateSave& Save, int32 Xp);
	static FString MasteryLabel(const FGameStateSave& Save);
};

// ---------------------------------------------------------------- dialogue runtime
class FDialogueRuntime
{
public:
	FOnLineShown OnLineShown;
	FOnTrainerSpawn OnTrainerSpawn;
	FOnShopRequested OnShopRequested;
	FOnCraftRequested OnCraftRequested;
	FOnCreatureGift OnCreatureGift;
	FOnDialogueEnded OnEnded;

	explicit FDialogueRuntime(FGameStateSave& InState) : State(InState) {}

	void Start(const FNpcDef& Npc);
	void Advance();
	void Choose(bool bYes);
	void End();

	bool IsActive() const { return bActive; }
	const FDialogueLine* Current() const { return CurrentLine; }
	const FString& NpcId() const { return NpcIdStr; }

private:
	void Jump(const FString& NodeId);
	void ShowCurrent();
	void ApplyLine(const FDialogueLine& Line);
	FString PickNode(const FNpcDef& Npc) const;

	FGameStateSave& State;
	TArray<FDialogueLine> Queue;
	int32 Idx = 0;
	FDialogueLine CurrentStorage;
	const FDialogueLine* CurrentLine = nullptr;
	bool bActive = false;
	FString NpcIdStr;
};

// ---------------------------------------------------------------- battle engine
UENUM(BlueprintType)
enum class EBattleResult : uint8
{
	Ongoing, PlayerWon, PlayerLost, Caught, Fled
};

class FBattleEngine
{
public:
	FOnBattleLog OnLog;
	FOnCreatureEvolved OnEvolved;
	FOnSfx OnSfxRequest;

	TArray<FCreature> PlayerTeam;
	TArray<FCreature> EnemyTeam;
	bool bIsTrainerBattle = false;
	FTrainerDef TrainerDef;
	int32 ActivePlayerIdx = 0;
	int32 ActiveEnemyIdx = 0;
	EBattleResult Result = EBattleResult::Ongoing;

	FBattleEngine(TArray<FCreature> InPlayerTeam, TArray<FCreature> InEnemyTeam, const FTrainerDef* InTrainer);

	/** Call once before the battle so mastery bonuses and KO XP can apply. */
	FBattleEngine& WithState(FGameStateSave* InState) { SaveState = InState; return *this; }

	FCreature* PlayerMon();
	FCreature* EnemyMon();
	void ResetBattleBuffs() { AtkBuffs.Reset(); DefBuffs.Reset(); }

	/** Full turn: player move + enemy response + residual. */
	void PlayerTurn(const FString& MoveId);
	/** Consumed turn (failed flee, item use, etc): only enemy acts. */
	void EnemyOnlyTurn();

	bool TryCatch(const FString& CoreItemId);
	bool TryFlee();

	/** XP/rewards/evolutions after a won or caught battle. */
	void ApplyRewards(FGameStateSave& State);
	/** The caught creature (valid when Result == Caught). */
	const FCreature* Caught() const { return CaughtPtr; }

private:
	struct FBuffEntry { FString Uid; int32 Value; };

	int32 AtkBuff(const FCreature* C) const;
	int32 DefBuff(const FCreature* C) const;
	void Execute(FCreature& Atk, FCreature& Def, const FMoveDef& MV, bool bAtkIsPlayer);
	int32 CalcDamage(const FCreature& Atk, const FCreature& Def, const FMoveDef& MV);
	void ApplyEffect(FCreature& Atk, FCreature& Def, const FMoveDef& MV);
	bool CanAct(FCreature& C);
	void EndOfTurnResidual();
	void TickResidual(FCreature* C);
	void HandleFaint(FCreature& Fainted);
	void GiveItem(FGameStateSave& State, const FString& Give);
	void GainXp(FCreature& C, int32 Xp);
	void TryEvolve(FCreature& C);
	FString EnemyChooseMove(const FCreature& EM);
	void Emit(const FString& Msg);
	void Sfx(const FString& Id);
	static FCreature* Alive(TArray<FCreature>& Team, int32& Idx);
	static void InitPP(TArray<FCreature>& Team);
	static int32 UnlockLevel(const FString& MoveId);

	FGameStateSave* SaveState = nullptr;
	TMap<FString, int32> AtkBuffs;
	TMap<FString, int32> DefBuffs;
	bool bCatchSucceeded = false;
	FCreature CaughtStorage;
	const FCreature* CaughtPtr = nullptr;
};
