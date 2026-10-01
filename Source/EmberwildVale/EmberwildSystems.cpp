// Emberwild Vale — core systems implementation.
// Faithful port of the Unity original's logic; tuning constants unchanged.

#include "EmberwildSystems.h"
#include "EmberwildDB.h"
#include "Algo/Find.h"

// ---------------------------------------------------------------- professions
const TCHAR* FProfessionSystem::Name(EProfessionId P)
{
	switch (P)
	{
	case EProfessionId::Forager: return TEXT("Forager");
	case EProfessionId::Miner:   return TEXT("Miner");
	case EProfessionId::Fisher:  return TEXT("Fisher");
	case EProfessionId::Chef:    return TEXT("Chef");
	case EProfessionId::Smith:   return TEXT("Smith");
	case EProfessionId::Tailor:  return TEXT("Tailor");
	}
	return TEXT("?");
}

const TCHAR* FProfessionSystem::Icon(EProfessionId P)
{
	switch (P)
	{
	case EProfessionId::Forager: return TEXT("*"); // glyph-safe stand-ins for the emoji set
	case EProfessionId::Miner:   return TEXT("/");
	case EProfessionId::Fisher:  return TEXT("=");
	case EProfessionId::Chef:    return TEXT("o");
	case EProfessionId::Smith:   return TEXT("H");
	case EProfessionId::Tailor:  return TEXT("~");
	}
	return TEXT(".");
}

int32 FProfessionSystem::GatherYield(FGameStateSave& Save, const FString& NodeId, int32& OutXp)
{
	OutXp = 0;
	const FNodeDef* Node = FEmberwildDB::Node(NodeId);
	if (!Node) return 0;

	const int32 ProfLevel = Save.ProfLevel(Node->Profession);
	if (ProfLevel < Node->RequiredLevel) return 0;

	int32 Amount = Node->BaseYield + LevelYieldBonus(ProfLevel);

	const FString& Eq = Save.EquippedItem;
	if (Eq == TEXT("forager_gloves") && Node->Profession == EProfessionId::Forager) Amount += 1;
	if (Eq == TEXT("miner_pick")     && Node->Profession == EProfessionId::Miner)   Amount += 1;
	if (Eq == TEXT("angler_rod")     && Node->Profession == EProfessionId::Fisher)  Amount += 1;

	OutXp = Node->XpReward;
	return FMath::Max(1, Amount);
}

void FProfessionSystem::AddXp(FGameStateSave& Save, EProfessionId P, int32 Xp)
{
	if (Save.ProfLevel(P) >= MaxLevel) return;
	Save.AddProfXp(P, Xp);
	while (Save.ProfLevel(P) < MaxLevel && Save.ProfXpOf(P) >= XpToNext(Save.ProfLevel(P)))
	{
		Save.ProfXp[(int32)P] -= XpToNext(Save.ProfLevel(P));
		Save.ProfLevels[(int32)P]++;
		FEmberwildToast::Push(FString::Printf(TEXT("%s %s is now level %d!"),
			Icon(P), Name(P), Save.ProfLevel(P)));
	}
}

bool FProfessionSystem::CanCraft(const FGameStateSave& Save, const FRecipe& R, const FString& CurrentStation)
{
	if (Save.ProfLevel(R.Profession) < R.RequiredLevel) return false;
	if (!R.Station.IsEmpty() && CurrentStation != R.Station) return false;
	for (const FItemStack& Input : R.Inputs)
		if (Save.CountItem(Input.ItemId) < Input.Count) return false;
	return true;
}

FString FProfessionSystem::CraftBlocker(const FGameStateSave& Save, const FRecipe& R, const FString& CurrentStation)
{
	if (Save.ProfLevel(R.Profession) < R.RequiredLevel)
		return FString::Printf(TEXT("%s Lv%d required"), Name(R.Profession), R.RequiredLevel);
	if (!R.Station.IsEmpty() && CurrentStation != R.Station)
		return FString::Printf(TEXT("Requires %s"), StationName(R.Station));
	for (const FItemStack& Input : R.Inputs)
		if (Save.CountItem(Input.ItemId) < Input.Count)
			return FString::Printf(TEXT("Need %dx %s"), Input.Count, *FEmberwildDB::ItemName(Input.ItemId));
	return TEXT("");
}

const TCHAR* FProfessionSystem::StationName(const FString& Station)
{
	if (Station == TEXT("forge"))     return TEXT("a Forge");
	if (Station == TEXT("campfire"))  return TEXT("a Campfire");
	if (Station == TEXT("workbench")) return TEXT("a Workbench");
	return TEXT("anywhere");
}

// ---------------------------------------------------------------- weapon mastery
static TArray<FWeaponMastery::FTechnique> MakeTechniques()
{
	return {
		// weapons
		{ TEXT("mt_rend"),         5,  5 }, // Claws
		{ TEXT("mt_inferno"),     20,  4 }, // Fangs
		{ TEXT("mt_tempest"),     45,  3 }, // Spines
		// armor
		{ TEXT("mt_bulwarkslam"),  8,  5 }, // Plates
		{ TEXT("mt_gracewind"),   20,  5 }, // Charms
		{ TEXT("mt_aegis"),       40,  4 }, // Plates capstone
	};
}

const TArray<FWeaponMastery::FTechnique>& FWeaponMastery::Techniques()
{
	static TArray<FTechnique> T = MakeTechniques();
	return T;
}

EWeaponClass FWeaponMastery::ClassOfTechnique(const FString& MoveId)
{
	if (MoveId == TEXT("mt_rend"))        return EWeaponClass::Claws;
	if (MoveId == TEXT("mt_inferno"))     return EWeaponClass::Fangs;
	if (MoveId == TEXT("mt_tempest"))     return EWeaponClass::Spines;
	if (MoveId == TEXT("mt_bulwarkslam")) return EWeaponClass::Plates;
	if (MoveId == TEXT("mt_gracewind"))   return EWeaponClass::Charms;
	if (MoveId == TEXT("mt_aegis"))       return EWeaponClass::Plates;
	return EWeaponClass::None;
}

EWeaponClass FWeaponMastery::ClassOf(const FString& ItemId)
{
	if (ItemId.IsEmpty()) return EWeaponClass::None;
	if (ItemId == TEXT("gear_claw"))       return EWeaponClass::Claws;
	if (ItemId == TEXT("gear_fang"))       return EWeaponClass::Fangs;
	if (ItemId == TEXT("gear_stormspine")) return EWeaponClass::Spines;
	if (ItemId == TEXT("gear_band") || ItemId == TEXT("gear_hide") || ItemId == TEXT("gear_plate")) return EWeaponClass::Plates;
	if (ItemId == TEXT("gear_charm"))      return EWeaponClass::Charms;
	return EWeaponClass::None;
}

const TCHAR* FWeaponMastery::Name(EWeaponClass WC)
{
	switch (WC)
	{
	case EWeaponClass::Claws:  return TEXT("Claws");
	case EWeaponClass::Fangs:  return TEXT("Fangs");
	case EWeaponClass::Spines: return TEXT("Spines");
	case EWeaponClass::Plates: return TEXT("Plates");
	case EWeaponClass::Charms: return TEXT("Charms");
	default:                   return TEXT("");
	}
}

float FWeaponMastery::AttackMultiplier(const FGameStateSave& Save, EWeaponClass WC)
{
	if ((int32)WC <= 0) return 1.f;
	return 1.f + BonusPerLevel * Level(Save, WC);
}

float FWeaponMastery::DefenseMultiplier(const FGameStateSave& Save, EWeaponClass WC)
{
	if ((int32)WC <= 0) return 1.f;
	return FMath::Max(0.5f, 1.f - DefReducePerLevel * Level(Save, WC));
}

int32 FWeaponMastery::DefenseReducePercent(const FGameStateSave& Save, EWeaponClass WC)
{
	return FMath::RoundToInt((1.f - DefenseMultiplier(Save, WC)) * 100.f);
}

int32 FWeaponMastery::BasePP(const FString& MoveId)
{
	for (const FTechnique& T : Techniques())
		if (MoveId == T.MoveId) return T.BasePP;
	return 3;
}

int32 FWeaponMastery::TechPP(const FGameStateSave& Save, EWeaponClass WC, const FString& MoveId)
{
	if ((int32)WC <= 0) return 0;
	if (const int32* V = Save.TechPP.Find(MoveId)) return *V;
	return BasePP(MoveId);
}

void FWeaponMastery::SpendTechPP(FGameStateSave& Save, const FString& MoveId)
{
	Save.TechPP.Add(MoveId, FMath::Max(0, TechPP(Save, ClassOfTechnique(MoveId), MoveId) - 1));
}

void FWeaponMastery::RestoreTechPP(FGameStateSave& Save, const FString& MoveId)
{
	Save.TechPP.Add(MoveId, BasePP(MoveId));
}

void FWeaponMastery::AddXp(FGameStateSave& Save, EWeaponClass WC, int32 Xp)
{
	if ((int32)WC <= 0 || Xp <= 0) return;
	if (Level(Save, WC) >= MaxLevel) return;

	Save.WeaponXp[(int32)WC] += Xp;
	while (Level(Save, WC) < MaxLevel && Save.WeaponXp[(int32)WC] >= XpToNext(Level(Save, WC)))
	{
		Save.WeaponXp[(int32)WC] -= XpToNext(Level(Save, WC));
		Save.WeaponLevels[(int32)WC]++;
		FEmberwildToast::Push(FString::Printf(TEXT("! %s mastery is now level %d!"),
			Name(WC), Level(Save, WC)));
	}
}

bool FWeaponMastery::IsTechniqueAvailable(FGameStateSave& Save, const FCreature& C, const FString& MoveId)
{
	if (C.CurHP <= 0) return false;
	const EWeaponClass WC = ClassOfTechnique(MoveId);
	if (WC == EWeaponClass::None) return false;
	// the technique's class must be the weapon or armor actually worn
	if (ClassOf(C.Weapon) != WC && ClassOf(C.Armor) != WC) return false;
	if (Level(Save, WC) < [&]() { for (const FTechnique& T : Techniques()) if (MoveId == T.MoveId) return T.UnlockLevel; return TNumericLimits<int32>::Max(); }()) return false;
	return TechPP(Save, WC, MoveId) > 0;
}

// ---------------------------------------------------------------- quests
FOnQuestAccepted  FQuestSystem::OnQuestAccepted;
FOnQuestCompleted FQuestSystem::OnQuestCompleted;

static bool HasPrereqs(const FGameStateSave& S, const FQuestDef& Q)
{
	for (const FString& Id : Q.PrereqQuests)  if (!S.DoneQuests.Contains(Id)) return false;
	for (const FString& F : Q.RequiresFlags)  if (!S.HasFlag(F)) return false;
	for (const FString& F : Q.ForbidsFlags)   if (S.HasFlag(F)) return false;
	return true;
}

bool FQuestSystem::IsOffered(const FGameStateSave& S, const FQuestDef& Q)
{
	if (S.DoneQuests.Contains(Q.Id) || S.ActiveQuests.Contains(Q.Id)) return false;
	return HasPrereqs(S, Q);
}

bool FQuestSystem::Accept(FGameStateSave& S, const FString& QuestId)
{
	const FQuestDef* Q = Get(QuestId);
	if (!Q || S.ActiveQuests.Contains(QuestId) || S.DoneQuests.Contains(QuestId)) return false;

	S.ActiveQuests.Add(QuestId);
	for (const FString& F : Q->OnAcceptFlags) S.SetFlag(F);
	for (const FString& Give : Q->OnAcceptGiveItems)
	{
		TArray<FString> Parts;
		Give.ParseIntoArray(Parts, TEXT(":"));
		const int32 N = Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 1;
		S.AddItem(Parts[0], N);
		FEmberwildToast::Push(FString::Printf(TEXT("Received %dx %s"), N, *FEmberwildDB::ItemName(Parts[0])));
	}
	FEmberwildToast::Push(FString::Printf(TEXT("Quest accepted: %s"), *Q->Name));
	OnQuestAccepted.Broadcast(*Q);
	return true;
}

bool FQuestSystem::Complete(FGameStateSave& S, const FString& QuestId)
{
	const FQuestDef* Q = Get(QuestId);
	if (!Q || !S.ActiveQuests.Contains(QuestId)) return false;
	if (!IsComplete(S, *Q)) return false;

	S.ActiveQuests.Remove(QuestId);
	S.DoneQuests.Add(QuestId);
	for (const FString& F : Q->OnCompleteFlags) S.SetFlag(F);
	for (const FString& Give : Q->OnCompleteGiveItems)
	{
		TArray<FString> Parts;
		Give.ParseIntoArray(Parts, TEXT(":"));
		const int32 N = Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 1;
		S.AddItem(Parts[0], N);
		FEmberwildToast::Push(FString::Printf(TEXT("Received %dx %s"), N, *FEmberwildDB::ItemName(Parts[0])));
	}
	if (Q->RewardCoins > 0) S.Coins += Q->RewardCoins;

	FEmberwildToast::Push(FString::Printf(TEXT("Quest complete: %s%s"),
		*Q->Name, Q->RewardCoins > 0 ? *FString::Printf(TEXT("  +%d coins"), Q->RewardCoins) : TEXT("")));
	OnQuestCompleted.Broadcast(*Q);
	return true;
}

static bool ObjectiveMet(const FGameStateSave& S, const FString& Obj)
{
	const int32 Space = Obj.Find(TEXT(" "));
	const FString Verb = Space < 0 ? Obj : Obj.Left(Space);
	const FString Arg  = Space < 0 ? FString() : Obj.Mid(Space + 1);

	if (Verb == TEXT("craft"))       return S.HasFlag(TEXT("craft_") + Arg);
	if (Verb == TEXT("catch_any"))   return S.HasFlag(TEXT("caught_any"));
	if (Verb == TEXT("defeat"))      return S.DefeatedTrainers.Contains(Arg);
	if (Verb == TEXT("collect"))     return S.CountItem(Arg) > 0;
	if (Verb == TEXT("prof3_lv2"))
	{
		int32 Count = 0;
		for (int32 i = 0; i < 6; ++i)
			if (S.ProfLevels.IsValidIndex(i) && S.ProfLevels[i] >= 2) Count++;
		return Count >= 3;
	}
	if (Verb == TEXT("wake_sleeper")) return S.HasFlag(TEXT("sleeper_awake"));
	return false;
}

bool FQuestSystem::IsComplete(const FGameStateSave& S, const FQuestDef& Q)
{
	if (Q.Objectives.Num() == 0) return true;
	for (const FString& Obj : Q.Objectives)
		if (!ObjectiveMet(S, Obj)) return false;
	return true;
}

bool FQuestSystem::IsObjectiveMet(const FGameStateSave& S, const FString& Obj)
{
	return ObjectiveMet(S, Obj);
}

FString FQuestSystem::ObjectiveText(const FGameStateSave& S, const FString& Obj)
{
	const int32 Space = Obj.Find(TEXT(" "));
	const FString Verb = Space < 0 ? Obj : Obj.Left(Space);
	const FString Arg  = Space < 0 ? FString() : Obj.Mid(Space + 1);

	if (Verb == TEXT("craft"))      return FString::Printf(TEXT("Craft %s"), *FEmberwildDB::ItemName(Arg));
	if (Verb == TEXT("catch_any"))  return TEXT("Catch a wild creature");
	if (Verb == TEXT("defeat"))
	{
		FString NameStr = Arg;
		if (const FTrainerDef* T = FEmberwildDB::Trainer(Arg)) NameStr = T->Name;
		return FString::Printf(TEXT("Defeat %s"), *NameStr);
	}
	if (Verb == TEXT("collect"))    return FString::Printf(TEXT("Obtain %s"), *FEmberwildDB::ItemName(Arg));
	if (Verb == TEXT("prof3_lv2"))  return TEXT("Reach Lv2 in 3 professions");
	if (Verb == TEXT("wake_sleeper")) return TEXT("Wake the Sleeper");
	return Obj;
}

void FQuestSystem::AutoCompleteAll(FGameStateSave& S)
{
	for (int32 i = S.ActiveQuests.Num() - 1; i >= 0; --i)
	{
		const FQuestDef* Q = Get(S.ActiveQuests[i]);
		if (Q && IsComplete(S, *Q)) Complete(S, Q->Id);
	}
}

// ---------------------------------------------------------------- monster factory
FCreature FMonsterFactory::Create(const FString& SpeciesId, int32 Level)
{
	const FSpeciesDef* Sp = FEmberwildDB::SpeciesDef(SpeciesId);
	if (!Sp) return FCreature();

	FCreature C;
	C.SpeciesId = SpeciesId;
	C.Level = FMath::Clamp(Level, 1, FEmberwildDB::MaxCreatureLevel());
	C.Uid = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(10);
	FillMoves(C, *Sp);
	C.CurHP = C.MaxHP();
	C.bShiny = FMath::FRand() < 0.015f; // 1.5% shiny
	return C;
}

void FMonsterFactory::FillMoves(FCreature& C, const FSpeciesDef& Sp)
{
	C.Moves.Reset();
	// a creature knows up to its level's learnset entries, keeps last 4
	const int32 UpTo = FMath::Min(Sp.Learnset.Num(), C.Level);
	const int32 Start = FMath::Max(0, UpTo - 4);
	for (int32 i = Start; i < UpTo; ++i)
	{
		const FString MV = Sp.Learnset[i];
		if (!C.Moves.Contains(MV)) C.Moves.Add(FEmberwildDB::SafeMove(MV));
	}
	if (C.Moves.Num() == 0) C.Moves.Add(TEXT("tackle"));
}

FCreature FMonsterFactory::CreateWildForZone(const FString& Zone, const FGameStateSave& Save)
{
	TArray<FString> Pool = FEmberwildDB::WildPoolFor(Zone);
	if (Pool.Num() == 0) return Create(TEXT("burrowmouse"), 3);

	const FString SpeciesId = Pool[FMath::RandRange(0, Pool.Num() - 1)];

	int32 Lo = 2, Hi = 4; // meadow: gentler than the starter (lv5)
	if      (Zone == TEXT("ridge"))       { Lo = 8;  Hi = 12; }
	else if (Zone == TEXT("lake"))        { Lo = 6;  Hi = 10; }
	else if (Zone == TEXT("hollow"))      { Lo = 10; Hi = 14; }
	else if (Zone == TEXT("hollow_deep")) { Lo = 14; Hi = 18; }

	int32 Lvl = FMath::RandRange(Lo, Hi);
	if (Save.EquippedItem == TEXT("scout_band")) Lvl = FMath::Max(2, Lvl - 2);

	return Create(SpeciesId, Lvl);
}

FCreature FMonsterFactory::FromTrainerEntry(const FString& Entry)
{
	TArray<FString> Parts;
	Entry.ParseIntoArray(Parts, TEXT(":"));
	const int32 Lvl = Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 5;
	return Create(Parts[0], Lvl);
}

// ---------------------------------------------------------------- player combat
int32 FPlayerCombat::BaseDamage(const FString& WeaponId)
{
	switch (KindOf(WeaponId))
	{
	case EPlayerWeaponKind::Club:  return ClubDmg;
	case EPlayerWeaponKind::Blade: return BladeDmg;
	case EPlayerWeaponKind::Spear: return SpearDmg;
	case EPlayerWeaponKind::Axe:   return AxeDmg;
	default: return 0;
	}
}

float FPlayerCombat::Cooldown(const FString& WeaponId)
{
	switch (KindOf(WeaponId))
	{
	case EPlayerWeaponKind::Blade: return BladeCd;
	case EPlayerWeaponKind::Spear: return SpearCd;
	case EPlayerWeaponKind::Axe:   return AxeCd;
	default: return ClubCd;
	}
}

float FPlayerCombat::SwingSeconds(const FString& WeaponId)
{
	switch (KindOf(WeaponId))
	{
	case EPlayerWeaponKind::Blade: return BladeSwing;
	case EPlayerWeaponKind::Spear: return SpearSwing;
	case EPlayerWeaponKind::Axe:   return AxeSwing;
	default: return ClubSwing;
	}
}

float FPlayerCombat::Reach(const FString& WeaponId)
{
	return KindOf(WeaponId) == EPlayerWeaponKind::Spear ? SpearReach : 2.6f;
}

EPlayerWeaponKind FPlayerCombat::KindOf(const FString& WeaponId)
{
	const FItemDef* D = FEmberwildDB::Item(WeaponId);
	return D ? D->PlayerWeaponKind : EPlayerWeaponKind::None;
}

FString FPlayerCombat::NameOf(const FString& WeaponId)
{
	return WeaponId.IsEmpty() ? TEXT("Bare Hands") : FEmberwildDB::ItemName(WeaponId);
}

int32 FPlayerCombat::DamageRoll(const FGameStateSave& Save)
{
	int32 Dmg = BaseDamage(Save.PlayerWeapon);
	if (Dmg > 0)
		Dmg = FMath::RoundToInt(Dmg * (1.f + 0.01f * Save.PlayerWeaponLevel));
	return FMath::Max(1, FMath::RandRange(FMath::Max(1, Dmg - 1), Dmg + 1));
}

void FPlayerCombat::AddXp(FGameStateSave& Save, int32 Xp)
{
	if (Xp <= 0) return;
	if (Save.PlayerWeapon.IsEmpty()) return; // bare hands teach nothing
	if (Save.PlayerWeaponLevel >= MaxMasteryLevel) return;

	Save.PlayerWeaponXp += Xp;
	while (Save.PlayerWeaponLevel < MaxMasteryLevel && Save.PlayerWeaponXp >= XpToNext(Save.PlayerWeaponLevel))
	{
		Save.PlayerWeaponXp -= XpToNext(Save.PlayerWeaponLevel);
		Save.PlayerWeaponLevel++;
		FEmberwildToast::Push(FString::Printf(TEXT("! %s skill is now level %d!"),
			*NameOf(Save.PlayerWeapon), Save.PlayerWeaponLevel));
	}
}

FString FPlayerCombat::MasteryLabel(const FGameStateSave& Save)
{
	if (Save.PlayerWeapon.IsEmpty()) return TEXT("Bare Hands");
	if (Save.PlayerWeaponLevel <= 0) return NameOf(Save.PlayerWeapon);
	return FString::Printf(TEXT("%s  Lv%d  (%d%% stronger)"),
		*NameOf(Save.PlayerWeapon), Save.PlayerWeaponLevel, Save.PlayerWeaponLevel);
}

// ---------------------------------------------------------------- dialogue runtime
void FDialogueRuntime::Start(const FNpcDef& NpcDefRef)
{
	if (NpcDefRef.DialogueNodes.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Dialogue] NPC has no dialogue nodes: %s"), *NpcDefRef.Id);
		return;
	}
	NpcIdStr = NpcDefRef.Id;
	Jump(PickNode(NpcDefRef));
}

void FDialogueRuntime::Jump(const FString& NodeId)
{
	const FDialogueNode* Node = FEmberwildDB::DialogueNode(NodeId);
	if (!Node || Node->Lines.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Dialogue] Missing/empty node '%s' for NPC '%s'"), *NodeId, *NpcIdStr);
		End();
		return;
	}
	Queue = Node->Lines;
	Idx = 0;
	bActive = true;
	ShowCurrent();
}

void FDialogueRuntime::ShowCurrent()
{
	while (Idx < Queue.Num() && Queue[Idx].Text.IsEmpty()) Idx++;
	if (Idx >= Queue.Num()) { End(); return; }

	CurrentStorage = Queue[Idx];
	CurrentLine = &CurrentStorage;
	ApplyLine(CurrentStorage);
	if (bActive) OnLineShown.Broadcast(CurrentStorage);
}

void FDialogueRuntime::ApplyLine(const FDialogueLine& Line)
{
	if (!Line.SetFlag.IsEmpty()) State.SetFlag(Line.SetFlag);

	if (!Line.GiveItem.IsEmpty())
	{
		TArray<FString> Parts;
		Line.GiveItem.ParseIntoArray(Parts, TEXT(":"));
		const int32 N = Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 1;
		State.AddItem(Parts[0], N);
		FEmberwildToast::Push(FString::Printf(TEXT("Received %dx %s"), N, *FEmberwildDB::ItemName(Parts[0])));
	}

	if (!Line.Teach.IsEmpty())
	{
		TArray<FString> Parts;
		Line.Teach.ParseIntoArray(Parts, TEXT(":"));
		if (Parts.Num() == 2)
		{
			// teach = "Prof:level" - raise the trade TO that level
			EProfessionId P = EProfessionId::Forager;
			bool bFound = false;
			for (int32 i = 0; i < 6; ++i)
				if (Parts[0] == FProfessionSystem::Name((EProfessionId)i)) { P = (EProfessionId)i; bFound = true; break; }
			const int32 Target = FMath::Clamp(FCString::Atoi(*Parts[1]), 1, FProfessionSystem::MaxLevel);
			if (bFound && State.ProfLevel(P) < Target)
			{
				State.ProfLevels[(int32)P] = Target;
				FEmberwildToast::Push(FString::Printf(TEXT("%s trained to level %d"),
					FProfessionSystem::Name(P), Target));
			}
		}
	}

	if (Line.bHealParty)
	{
		State.HealAll();
		for (const FWeaponMastery::FTechnique& T : FWeaponMastery::Techniques())
			FWeaponMastery::RestoreTechPP(State, T.MoveId);
		FEmberwildToast::Push(TEXT("Your creatures are fully rested!"));
	}

	if (!Line.QuestId.IsEmpty())
		if (const FQuestDef* Q = FEmberwildDB::Quest(Line.QuestId))
			if (FQuestSystem::IsOffered(State, *Q))
				FQuestSystem::Accept(State, Q->Id);

	if (!Line.SpawnCreature.IsEmpty())
	{
		TArray<FString> Parts;
		Line.SpawnCreature.ParseIntoArray(Parts, TEXT(":"));
		const int32 Lvl = Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 5;
		FCreature C = FMonsterFactory::Create(Parts[0], Lvl);
		if (C.Species() && !C.SpeciesId.IsEmpty())
		{
			// gift creatures always go to the party if there's room
			if (State.Party.Num() < 6) State.Party.Add(C);
			else State.Box.Add(C);
			OnCreatureGift.Broadcast(C);
		}
	}

	if (!Line.SpawnTrainer.IsEmpty())
	{
		const FTrainerDef* T = FEmberwildDB::Trainer(Line.SpawnTrainer);
		UE_LOG(LogTemp, Log, TEXT("[Dialogue] spawn trainer '%s' (def %s)"), *Line.SpawnTrainer, T ? TEXT("found") : TEXT("MISSING"));
		if (T)
			OnTrainerSpawn.Broadcast(*T);
	}
}

void FDialogueRuntime::Advance()
{
	if (!bActive || !CurrentLine) return;
	const FDialogueLine Line = *CurrentLine;

	if (!Line.OpenShop.IsEmpty())  { End(); OnShopRequested.Broadcast(Line.OpenShop); return; }
	if (!Line.OpenCraft.IsEmpty()) { End(); OnCraftRequested.Broadcast(Line.OpenCraft); return; }

	if (Line.EndRule == TEXT("choice")) return; // must Choose()

	if (Line.EndRule == TEXT("end") || Idx >= Queue.Num() - 1) { End(); return; }
	Idx++;
	ShowCurrent();
}

void FDialogueRuntime::Choose(bool bYes)
{
	if (!bActive || !CurrentLine || CurrentLine->EndRule != TEXT("choice")) return;
	const FString Target = bYes ? CurrentLine->YesNode : CurrentLine->NoNode;
	if (Target.IsEmpty()) { End(); return; }
	Jump(Target);
}

void FDialogueRuntime::End()
{
	if (!bActive) return;
	bActive = false;
	CurrentLine = nullptr;
	if (!NpcIdStr.IsEmpty()) State.SetFlag(TEXT("met_") + NpcIdStr);
	OnEnded.Broadcast();
}

FString FDialogueRuntime::PickNode(const FNpcDef& NpcDefRef) const
{
	auto ActiveOrDone = [&](const TCHAR* QId)
	{
		const FString Q(QId);
		return State.ActiveQuests.Contains(Q) || State.DoneQuests.Contains(Q);
	};

	const FString& Id = NpcDefRef.Id;
	if (Id == TEXT("brann"))   return ActiveOrDone(TEXT("q_firstcore")) ? TEXT("brann_loop") : TEXT("brann_intro");
	if (Id == TEXT("maren"))
	{
		if (State.HasFlag(TEXT("caught_first"))) return TEXT("maren_yes");
		return ActiveOrDone(TEXT("q_firstcatch")) ? TEXT("maren_no") : TEXT("maren_idle");
	}
	if (Id == TEXT("pell"))    return State.HasFlag(TEXT("met_pell"))   ? TEXT("pell_loop")   : TEXT("pell_intro");
	if (Id == TEXT("mira"))    return State.HasFlag(TEXT("met_mira"))   ? TEXT("mira_loop")   : TEXT("mira_intro");
	if (Id == TEXT("juna"))    return ActiveOrDone(TEXT("q_professions")) ? TEXT("juna_loop") : TEXT("juna_intro");
	if (Id == TEXT("odessa"))  return State.HasFlag(TEXT("ridge_opened")) ? TEXT("odessa_loop") : TEXT("odessa_intro");
	if (Id == TEXT("tobin"))   return State.DefeatedTrainers.Contains(TEXT("trainer_tobin"))  ? TEXT("tobin_loop")  : TEXT("tobin_fight");
	if (Id == TEXT("sela"))    return State.HasFlag(TEXT("has_sigil"))  ? TEXT("sela_loop")   : TEXT("sela_intro");
	if (Id == TEXT("korrin"))  return State.DefeatedTrainers.Contains(TEXT("trainer_korrin")) ? TEXT("korrin_loop") : TEXT("korrin_fight");
	if (Id == TEXT("vesna"))   return State.HasFlag(TEXT("met_vesna"))  ? TEXT("vesna_loop")  : TEXT("vesna_intro");
	if (Id == TEXT("hollis"))  return State.DefeatedTrainers.Contains(TEXT("trainer_hollis")) ? TEXT("hollis_loop") : TEXT("hollis_fight");
	if (Id == TEXT("zephy"))   return State.HasFlag(TEXT("herald_down")) ? TEXT("zephy_loop") : TEXT("zephy_fight");
	if (Id == TEXT("sleepers"))return State.HasFlag(TEXT("sleeper_awake")) ? TEXT("sleeper_done") : TEXT("sleeper_final");

	return NpcDefRef.DialogueNodes.Num() > 0 ? NpcDefRef.DialogueNodes[0] : FString();
}

// ---------------------------------------------------------------- battle engine
FBattleEngine::FBattleEngine(TArray<FCreature> InPlayerTeam, TArray<FCreature> InEnemyTeam, const FTrainerDef* InTrainer)
{
	PlayerTeam = MoveTemp(InPlayerTeam);
	EnemyTeam = MoveTemp(InEnemyTeam);
	bIsTrainerBattle = InTrainer != nullptr;
	if (InTrainer) TrainerDef = *InTrainer;
	InitPP(PlayerTeam);
	InitPP(EnemyTeam);
}

void FBattleEngine::InitPP(TArray<FCreature>& Team)
{
	for (FCreature& C : Team)
		for (const FString& M : C.Moves)
			if (!C.MovePP.Contains(M))
			{
				const FMoveDef* MD = FEmberwildDB::Move(M);
				C.MovePP.Add(M, MD ? MD->PP : 10);
			}
}

int32 FBattleEngine::UnlockLevel(const FString& MoveId)
{
	for (const FWeaponMastery::FTechnique& T : FWeaponMastery::Techniques())
		if (MoveId == T.MoveId) return T.UnlockLevel;
	return TNumericLimits<int32>::Max();
}

FCreature* FBattleEngine::Alive(TArray<FCreature>& Team, int32& Idx)
{
	if (Team.Num() == 0) return nullptr;
	if (!Team.IsValidIndex(Idx)) Idx = 0;
	if (Team[Idx].CurHP <= 0)
		for (int32 i = 0; i < Team.Num(); ++i)
			if (Team[i].CurHP > 0) { Idx = i; break; }
	return Team.IsValidIndex(Idx) && Team[Idx].CurHP > 0 ? &Team[Idx] : nullptr;
}

FCreature* FBattleEngine::PlayerMon() { return Alive(PlayerTeam, ActivePlayerIdx); }
FCreature* FBattleEngine::EnemyMon()  { return Alive(EnemyTeam, ActiveEnemyIdx); }

void FBattleEngine::Emit(const FString& Msg)
{
	if (!Msg.IsEmpty()) OnLog.Broadcast(Msg);
}

void FBattleEngine::Sfx(const FString& Id)
{
	OnSfxRequest.Broadcast(Id);
}

void FBattleEngine::PlayerTurn(const FString& MoveId)
{
	if (Result != EBattleResult::Ongoing) return;
	FCreature* PM = PlayerMon();
	FCreature* EM = EnemyMon();
	if (!PM || !EM) return;

	FMoveDef PlayerMove;
	if (const FMoveDef* PMD = FEmberwildDB::Move(MoveId)) PlayerMove = *PMD;
	else if (const FMoveDef* TD = FEmberwildDB::Move(TEXT("tackle"))) PlayerMove = *TD;

	// technique: spend its class's PP instead of the creature's move PP
	if (FWeaponMastery::ClassOfTechnique(MoveId) != EWeaponClass::None && SaveState)
		FWeaponMastery::SpendTechPP(*SaveState, MoveId);

	FMoveDef EnemyMove;
	if (const FMoveDef* EMD = FEmberwildDB::Move(EnemyChooseMove(*EM))) EnemyMove = *EMD;
	else if (const FMoveDef* TD = FEmberwildDB::Move(TEXT("tackle"))) EnemyMove = *TD;

	const bool bPPrio = PlayerMove.Id == TEXT("flashstep");
	const bool bEPrio = EnemyMove.Id == TEXT("flashstep");
	const bool bPlayerFirst = (bPPrio != bEPrio) ? bPPrio : PM->SPD() >= EM->SPD();

	if (bPlayerFirst)
	{
		Execute(*PM, *EM, PlayerMove, true);
		if (FCreature* NextEnemy = EnemyMon(); Result == EBattleResult::Ongoing && NextEnemy && NextEnemy->CurHP > 0)
			if (FCreature* NextPlayer = PlayerMon())
				Execute(*NextEnemy, *NextPlayer, EnemyMove, false);
	}
	else
	{
		Execute(*EM, *PM, EnemyMove, false);
		if (FCreature* NextPlayer = PlayerMon(); Result == EBattleResult::Ongoing && NextPlayer && NextPlayer->CurHP > 0)
			if (FCreature* NextEnemy = EnemyMon())
				Execute(*NextPlayer, *NextEnemy, PlayerMove, true);
	}

	EndOfTurnResidual();
}

void FBattleEngine::EnemyOnlyTurn()
{
	if (Result != EBattleResult::Ongoing) return;
	FCreature* EM = EnemyMon();
	FCreature* PM = PlayerMon();
	if (!EM || !PM) return;
	FMoveDef MV;
	if (const FMoveDef* MD = FEmberwildDB::Move(EnemyChooseMove(*EM))) MV = *MD;
	else if (const FMoveDef* TD = FEmberwildDB::Move(TEXT("tackle"))) MV = *TD;
	Execute(*EM, *PM, MV, false);
	EndOfTurnResidual();
}

bool FBattleEngine::TryCatch(const FString& CoreItemId)
{
	if (Result != EBattleResult::Ongoing) return false;
	if (bIsTrainerBattle) { Emit(TEXT("You can't catch another trainer's creature!")); return false; }
	FCreature* EM = EnemyMon();
	if (!EM || !EM->Species()) return false;

	const FItemDef* Core = FEmberwildDB::Item(CoreItemId);
	const float Bonus = Core ? Core->CatchBonus : 1.f;

	const float HpFactor = (3.f * EM->MaxHP() - 2.f * EM->CurHP) / (3.f * EM->MaxHP());
	const float Rate = (EM->Species()->CatchRate / 255.f) * HpFactor * (0.35f + 0.2f * Bonus) * 2.2f;
	const bool bOk = FMath::FRand() < FMath::Clamp(Rate, 0.f, 1.f);

	if (bOk)
	{
		Emit(FString::Printf(TEXT("The core seals! %s was caught!"), *EM->DisplayName()));
		bCatchSucceeded = true;
		Result = EBattleResult::Caught;
	}
	else
	{
		Emit(FString::Printf(TEXT("The core wobbles... %s broke free!"), *EM->DisplayName()));
	}
	return bOk;
}

bool FBattleEngine::TryFlee()
{
	if (Result != EBattleResult::Ongoing) return false;
	if (bIsTrainerBattle) { Emit(TEXT("There's no running from a trainer battle!")); return false; }
	FCreature* PM = PlayerMon();
	FCreature* EM = EnemyMon();
	// faster than the foe ~always escapes; slower gets 40% + level factor
	float Odds = 0.4f;
	if (PM && EM)
		Odds = PM->SPD() >= EM->SPD() ? 0.95f : 0.4f + 0.2f * (PM->Level - EM->Level) / 10.f;
	Odds = FMath::Clamp(Odds, 0.25f, 0.95f);
	if (FMath::FRand() < Odds)
	{
		Emit(TEXT("You slipped away safely."));
		Result = EBattleResult::Fled;
		return true;
	}
	Emit(TEXT("Couldn't escape!"));
	return false;
}

int32 FBattleEngine::AtkBuff(const FCreature* C) const { return C ? AtkBuffs.FindRef(C->Uid) : 0; }
int32 FBattleEngine::DefBuff(const FCreature* C) const { return C ? DefBuffs.FindRef(C->Uid) : 0; }

void FBattleEngine::Execute(FCreature& Atk, FCreature& Def, const FMoveDef& InMove, bool bAtkIsPlayer)
{
	if (Atk.CurHP <= 0 || Def.CurHP <= 0) return;
	if (!CanAct(Atk)) return;

	FMoveDef MV = InMove;
	const int32 PP = Atk.CurrentPP(MV.Id);
	if (PP <= 0)
	{
		Emit(FString::Printf(TEXT("%s has no PP left for %s! It flails!"), *Atk.DisplayName(), *MV.Name));
		if (const FMoveDef* TD = FEmberwildDB::Move(TEXT("tackle"))) MV = *TD;
	}
	else
	{
		Atk.SetPP(MV.Id, PP - 1);
	}

	Emit(FString::Printf(TEXT("%s used %s!"), *Atk.DisplayName(), *MV.Name));

	if (MV.Accuracy > 0 && FMath::RandRange(0, 99) >= MV.Accuracy)
	{
		Emit(TEXT("It missed!"));
		return;
	}

	if (MV.Power > 0)
	{
		const int32 Dmg = CalcDamage(Atk, Def, MV);
		Def.CurHP = FMath::Max(0, Def.CurHP - Dmg);
		Emit(FString::Printf(TEXT("%s took %d damage."), *Def.DisplayName(), Dmg));
		Sfx(TEXT("hit"));

		if (MV.Effect == TEXT("drain"))
		{
			const int32 Heal = FMath::Max(1, Dmg / 2);
			Atk.CurHP = FMath::Min(Atk.MaxHP(), Atk.CurHP + Heal);
			Emit(FString::Printf(TEXT("%s drained %d HP."), *Atk.DisplayName(), Heal));
		}
	}

	ApplyEffect(Atk, Def, MV);

	if (Def.CurHP <= 0)
	{
		Emit(FString::Printf(TEXT("%s fainted!"), *Def.DisplayName()));
		Sfx(TEXT("faint"));

		// mastery XP: every KO with an equipped weapon trains its class
		if (SaveState && Algo::FindBy(PlayerTeam, Atk.Uid, &FCreature::Uid) != nullptr)
			FWeaponMastery::AddXp(*SaveState, FWeaponMastery::ClassOf(Atk.Weapon), FWeaponMastery::XpPerKO);

		HandleFaint(Def);
	}
}

int32 FBattleEngine::CalcDamage(const FCreature& Atk, const FCreature& Def, const FMoveDef& MV)
{
	const FSpeciesDef* AtkSp = Atk.Species();
	const FSpeciesDef* DefSp = Def.Species();
	if (!AtkSp || !DefSp) return 1;

	const float Eff = FTypeChart::Effectiveness(MV.Element, DefSp->Element, DefSp->Element2);
	const float Stab = AtkSp->Element == MV.Element ? 1.5f : 1.f;
	const float Variance = FMath::FRandRange(0.85f, 1.f);

	const float AtkStat = Atk.ATK() * (1.f + 0.25f * AtkBuff(&Atk));
	const float DefStat = FMath::Max(1.f, Def.DEF() * (1.f + 0.25f * DefBuff(&Def)));

	// weapon mastery: the equipped weapon's class hits harder as it levels
	float Mastery = 1.f;
	if (SaveState && Algo::FindBy(PlayerTeam, Atk.Uid, &FCreature::Uid) != nullptr)
		Mastery = FWeaponMastery::AttackMultiplier(*SaveState, FWeaponMastery::ClassOf(Atk.Weapon));

	// armor mastery: the defender's equipped armor class takes less damage
	float Armor = 1.f;
	if (SaveState && PlayerTeam.Contains(Def))
		Armor = FWeaponMastery::DefenseMultiplier(*SaveState, FWeaponMastery::ClassOf(Def.Armor));

	float Dmg = ((2.f * Atk.Level / 5.f + 2.f) * MV.Power * AtkStat / DefStat) / 50.f + 2.f;
	Dmg *= Eff * Stab * Variance * Mastery * Armor;

	const FString Label = FTypeChart::Label(Eff);
	if (!Label.IsEmpty()) Emit(Label);

	return FMath::Max(1, FMath::RoundToInt(Dmg));
}

void FBattleEngine::ApplyEffect(FCreature& Atk, FCreature& Def, const FMoveDef& MV)
{
	if (MV.Effect == TEXT("buff_atk"))
	{
		AtkBuffs.Add(Atk.Uid, FMath::Min(3, AtkBuff(&Atk) + 1));
		Emit(FString::Printf(TEXT("%s's attack rose!"), *Atk.DisplayName()));
	}
	else if (MV.Effect == TEXT("buff_def"))
	{
		DefBuffs.Add(Atk.Uid, FMath::Min(3, DefBuff(&Atk) + 1));
		Emit(FString::Printf(TEXT("%s's defense rose!"), *Atk.DisplayName()));
	}
	else if (MV.Effect == TEXT("debuff_atk"))
	{
		AtkBuffs.Add(Def.Uid, FMath::Max(-3, AtkBuff(&Def) - 1));
		Emit(FString::Printf(TEXT("%s's attack fell!"), *Def.DisplayName()));
	}
	else if (MV.Effect == TEXT("debuff_def"))
	{
		DefBuffs.Add(Def.Uid, FMath::Max(-3, DefBuff(&Def) - 1));
		Emit(FString::Printf(TEXT("%s's defense fell!"), *Def.DisplayName()));
	}
	else if (MV.Effect == TEXT("buff_atk_def"))
	{
		AtkBuffs.Add(Atk.Uid, FMath::Min(3, AtkBuff(&Atk) + 1));
		DefBuffs.Add(Atk.Uid, FMath::Min(3, DefBuff(&Atk) + 1));
		Emit(FString::Printf(TEXT("%s braces - attack and defense rose!"), *Atk.DisplayName()));
	}
	else if (MV.Effect == TEXT("heal"))
	{
		const int32 Heal = FMath::Max(1, Atk.MaxHP() / 3);
		Atk.CurHP = FMath::Min(Atk.MaxHP(), Atk.CurHP + Heal);
		Emit(FString::Printf(TEXT("%s recovered health."), *Atk.DisplayName()));
	}
	else if (MV.Effect == TEXT("status"))
	{
		if (MV.StatusId.IsEmpty()) return;
		if (Def.Status == MV.StatusId) return;
		if (!Def.Status.IsEmpty()) { Emit(FString::Printf(TEXT("%s already has a condition."), *Def.DisplayName())); return; }
		if (FMath::RandRange(0, 99) < (MV.StatusChance > 0 ? MV.StatusChance : 100))
		{
			Def.Status = MV.StatusId;
			Def.StatusTurns = MV.StatusId == TEXT("sleep") ? 2 : 0;
			Emit(FString::Printf(TEXT("%s got %s!"), *Def.DisplayName(), *Def.Status));
		}
	}
}

bool FBattleEngine::CanAct(FCreature& C)
{
	if (C.Status == TEXT("sleep"))
	{
		if (C.StatusTurns <= 0)
		{
			C.Status = TEXT("");
			Emit(FString::Printf(TEXT("%s woke up!"), *C.DisplayName()));
			return true;
		}
		C.StatusTurns--;
		Emit(FString::Printf(TEXT("%s is fast asleep."), *C.DisplayName()));
		return false;
	}
	if (C.Status == TEXT("paralyze") && FMath::FRand() < 0.25f)
	{
		Emit(FString::Printf(TEXT("%s is paralyzed and can't move!"), *C.DisplayName()));
		return false;
	}
	return true;
}

void FBattleEngine::EndOfTurnResidual()
{
	TickResidual(PlayerMon());
	if (Result == EBattleResult::Ongoing) TickResidual(EnemyMon());
}

void FBattleEngine::TickResidual(FCreature* C)
{
	if (!C || C->CurHP <= 0) return;
	if (C->Status != TEXT("poison") && C->Status != TEXT("burn")) return;

	const int32 Dmg = FMath::Max(1, C->MaxHP() / 12);
	C->CurHP = FMath::Max(0, C->CurHP - Dmg);
	Emit(FString::Printf(TEXT("%s is hurt by %s."), *C->DisplayName(), *C->Status));

	if (C->CurHP <= 0)
	{
		Emit(FString::Printf(TEXT("%s fainted!"), *C->DisplayName()));
		HandleFaint(*C);
	}
}

void FBattleEngine::HandleFaint(FCreature& Fainted)
{
	const bool bWasEnemy = EnemyTeam.Contains(Fainted);

	if (bWasEnemy)
	{
		for (int32 i = 0; i < EnemyTeam.Num(); ++i)
			if (EnemyTeam[i].CurHP > 0)
			{
				ActiveEnemyIdx = i;
				if (bIsTrainerBattle)
					Emit(FString::Printf(TEXT("Trainer sent out %s!"), *EnemyTeam[i].DisplayName()));
				return;
			}
		Result = EBattleResult::PlayerWon;
	}
	else
	{
		for (int32 i = 0; i < PlayerTeam.Num(); ++i)
			if (PlayerTeam[i].CurHP > 0)
			{
				ActivePlayerIdx = i;
				Emit(FString::Printf(TEXT("Go, %s!"), *PlayerTeam[i].DisplayName()));
				return;
			}
		Result = EBattleResult::PlayerLost;
	}
}

void FBattleEngine::GiveItem(FGameStateSave& State, const FString& Give)
{
	TArray<FString> Parts;
	Give.ParseIntoArray(Parts, TEXT(":"));
	const int32 N = Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 1;
	State.AddItem(Parts[0], N);
	FEmberwildToast::Push(FString::Printf(TEXT("Received %dx %s"), N, *FEmberwildDB::ItemName(Parts[0])));
}

void FBattleEngine::GainXp(FCreature& C, int32 Xp)
{
	C.XP += Xp;
	while (C.XP >= FEmberwildDB::XpToNextCreature(C.Level) && C.Level < FEmberwildDB::MaxCreatureLevel())
	{
		C.XP -= FEmberwildDB::XpToNextCreature(C.Level);
		C.Level++;
		C.CurHP = FMath::Min(C.MaxHP(), C.CurHP + 6);
		Emit(FString::Printf(TEXT("%s grew to level %d!"), *C.DisplayName(), C.Level));
		TryEvolve(C);
	}
}

void FBattleEngine::TryEvolve(FCreature& C)
{
	const FSpeciesDef* Sp = C.Species();
	if (!Sp || Sp->EvolvesInto.IsEmpty() || C.Level < Sp->EvolveLevel) return;

	const FSpeciesDef* Evo = FEmberwildDB::SpeciesDef(Sp->EvolvesInto);
	if (!Evo) return;

	const FString OldName = C.DisplayName();
	C.SpeciesId = Evo->Id;
	if (C.Nickname == OldName) C.Nickname = TEXT("");
	C.CurHP = FMath::Min(C.MaxHP(), C.CurHP + 10);
	FMonsterFactory::FillMoves(C, *Evo);
	Emit(FString::Printf(TEXT("What? %s is evolving! ... %s evolved into %s!"), *OldName, *OldName, *Evo->Name));
	OnEvolved.Broadcast(C);
}

FString FBattleEngine::EnemyChooseMove(const FCreature& EM)
{
	if (EM.Moves.Num() == 0) return TEXT("tackle");

	FString Best;
	int32 BestP = -1;
	for (const FString& Id : EM.Moves)
	{
		const FMoveDef* D = FEmberwildDB::Move(Id);
		if (!D || EM.CurrentPP(Id) <= 0) continue;
		if (D->Power > BestP) { BestP = D->Power; Best = Id; }
	}
	if (Best.IsEmpty()) return TEXT("tackle");
	if (FMath::FRand() < 0.25f)
	{
		// 25% wildcard: any move with PP left
		TArray<FString> WithPP;
		for (const FString& Id : EM.Moves)
			if (EM.CurrentPP(Id) > 0 && FEmberwildDB::Move(Id))
				WithPP.Add(Id);
		if (WithPP.Num() > 0) return WithPP[FMath::RandRange(0, WithPP.Num() - 1)];
		// enemies don't use mastery techniques (those belong to the player's gear)
	}
	return Best;
}

void FBattleEngine::ApplyRewards(FGameStateSave& State)
{
	if (bCatchSucceeded)
	{
		if (FCreature* Caught = EnemyMon())
		{
			if (!State.CaughtSpecies.Contains(Caught->SpeciesId)) State.CaughtSpecies.Add(Caught->SpeciesId);
			State.CatchCount++;
			State.SetFlag(TEXT("caught_any"));
			Caught->Status = TEXT("");
			Caught->StatusTurns = 0;
			CaughtStorage = *Caught;
			CaughtPtr = &CaughtStorage;
			if (State.Party.Num() < 6) State.Party.Add(CaughtStorage);
			else State.Box.Add(CaughtStorage);
			Emit(FString::Printf(TEXT("%s joined your %s!"), *CaughtStorage.DisplayName(),
				State.Party.Contains(CaughtStorage) ? TEXT("party") : TEXT("box")));
		}
		State.Coins += 5;
		return;
	}

	if (Result != EBattleResult::PlayerWon) return;

	State.BattleWins++;

	// only the foes actually knocked out grant XP
	int32 XpGain = 0;
	for (const FCreature& E : EnemyTeam)
		if (E.Species() && E.CurHP <= 0)
			XpGain += E.Species()->XpYield * E.Level / 6 + 4;
	if (XpGain == 0 && EnemyTeam.Num() > 0 && EnemyTeam[0].Species())
		XpGain = EnemyTeam[0].Species()->XpYield * EnemyTeam[0].Level / 6 + 4;

	if (bIsTrainerBattle)
	{
		State.Coins += TrainerDef.RewardCoins;
		if (!State.DefeatedTrainers.Contains(TrainerDef.Id)) State.DefeatedTrainers.Add(TrainerDef.Id);
		for (const FString& F : TrainerDef.OnDefeatFlags) State.SetFlag(F);
		for (const FString& G : TrainerDef.OnDefeatGiveItems) GiveItem(State, G);
		Emit(FString::Printf(TEXT("You earned %d coins!"), TrainerDef.RewardCoins));
	}
	else
	{
		int32 Level = 4;
		for (const FCreature& E : EnemyTeam)
			if (E.CurHP <= 0) { Level = E.Level; break; }
		const int32 Coins = 8 + Level * 2;
		State.Coins += Coins;
		Emit(FString::Printf(TEXT("You picked up %d coins."), Coins));

		// wild loot: every defeated wild creature sheds crafting materials
		if (!bIsTrainerBattle)
			for (const FCreature& E : EnemyTeam)
				if (E.CurHP <= 0)
					for (const FWildLootDrop& Drop : FEmberwildDB::RollWildLoot(E.SpeciesId, E.Level))
					{
						State.AddItem(Drop.Id, Drop.N);
						FEmberwildToast::Push(FString::Printf(TEXT("+%d %s"), Drop.N, *FEmberwildDB::ItemName(Drop.Id)));
					}
	}

	for (FCreature& P : State.Party)
		if (P.CurHP > 0) GainXp(P, XpGain);

	// armor mastery: every survivor wearing armor trains its class on victory
	for (FCreature& P : State.Party)
	{
		if (P.CurHP <= 0) continue;
		const EWeaponClass ArmorClass = FWeaponMastery::ClassOf(P.Armor);
		if (ArmorClass != EWeaponClass::None)
			FWeaponMastery::AddXp(State, ArmorClass, FWeaponMastery::ArmorXpPerWin);
	}
}
