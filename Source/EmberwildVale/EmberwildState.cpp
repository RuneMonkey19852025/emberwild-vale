// Emberwild Vale — state implementation: creature stats, JSON save/load, toasts.

#include "EmberwildState.h"
#include "EmberwildDB.h"
#include "Serialization/JsonSerializer.h"

// UE 5.8 removed the FJsonObjectPtr alias from the public Json headers.
using FJsonObjectPtr = TSharedPtr<FJsonObject>;
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

// ---------------------------------------------------------------- creature
const FSpeciesDef* FCreature::Species() const { return FEmberwildDB::SpeciesDef(SpeciesId); }

int32 FCreature::SumGear(const FString& ItemId, const TCHAR* Prefix)
{
	if (ItemId.IsEmpty()) return 0;
	const FItemDef* D = FEmberwildDB::Item(ItemId);
	if (!D || D->EquipBonus.IsEmpty()) return 0;

	int32 Sum = 0;
	const FString PrefixStr = Prefix;
	TArray<FString> Segments;
	D->EquipBonus.ParseIntoArray(Segments, TEXT("|"));
	for (const FString& Seg : Segments)
		if (Seg.StartsWith(PrefixStr))
			Sum += FCString::Atoi(*Seg.Mid(PrefixStr.Len()));
	return Sum;
}

int32 FCreature::GearAtk() const { return SumGear(Weapon, TEXT("atk+")) + SumGear(Armor, TEXT("atk+")); }
int32 FCreature::GearDef() const { return SumGear(Weapon, TEXT("def+")) + SumGear(Armor, TEXT("def+")); }
int32 FCreature::GearSpd() const { return SumGear(Weapon, TEXT("spd+")) + SumGear(Armor, TEXT("spd+")); }
int32 FCreature::GearHp() const  { return SumGear(Weapon, TEXT("hp+"))  + SumGear(Armor, TEXT("hp+")); }

int32 FCreature::MaxHP() const
{
	const FSpeciesDef* S = Species();
	const int32 Base = S ? S->BaseHP : 20;
	return Base * 2 + Level * 4 + 12 + GearHp();
}

int32 FCreature::ATK() const
{
	const FSpeciesDef* S = Species();
	int32 V = (S ? S->BaseATK : 8) * 2 + Level * 2;
	V += GearAtk();
	return Status == TEXT("burn") ? V / 2 : V;
}

int32 FCreature::DEF() const
{
	const FSpeciesDef* S = Species();
	return (S ? S->BaseDEF : 8) * 2 + Level * 2 + GearDef();
}

int32 FCreature::SPD() const
{
	const FSpeciesDef* S = Species();
	return (S ? S->BaseSPD : 8) * 2 + Level + GearSpd();
}

FString FCreature::DisplayName() const
{
	if (!Nickname.IsEmpty()) return Nickname;
	const FSpeciesDef* S = Species();
	return S ? S->Name : TEXT("???");
}

int32 FCreature::CurrentPP(const FString& MoveId) const
{
	if (const int32* PP = MovePP.Find(MoveId)) return *PP;
	const FMoveDef* MD = FEmberwildDB::Move(MoveId);
	return MD ? MD->PP : 10;
}

void FCreature::NormalizeMoves()
{
	TArray<FString> Clean;
	for (const FString& M : Moves)
		if (!M.IsEmpty() && !Clean.Contains(M)) Clean.Add(M);
	if (Clean.Num() == 0) Clean.Add(TEXT("tackle"));
	Moves = Clean;
}

void FGameStateSave::HealAll()
{
	for (FCreature& C : Party)
	{
		C.CurHP = C.MaxHP();
		C.Status = TEXT("");
		C.StatusTurns = 0;
		for (const FString& M : C.Moves)
		{
			const FMoveDef* MD = FEmberwildDB::Move(M);
			C.SetPP(M, MD ? MD->PP : 10);
		}
	}
	// mastery technique PP refills on any full heal (handled by WeaponMastery caller)
}

// Creature MovePP maps serialize directly inside each creature's JSON object
// (the "pp" field), so no separate mirror pass is needed. These hooks exist
// to keep the save/load flow symmetric and to fill any missing moves.
void FGameStateSave::SyncPPForSave()
{
}

void FGameStateSave::RestorePPAfterLoad()
{
	auto Restore = [](FCreature& C)
	{
		C.NormalizeMoves();
		for (const FString& M : C.Moves)
		{
			if (C.MovePP.Contains(M)) continue; // loaded from JSON
			const FMoveDef* MD = FEmberwildDB::Move(M);
			C.SetPP(M, MD ? MD->PP : 10);
		}
	};
	for (FCreature& C : Party) Restore(C);
	for (FCreature& C : Box) Restore(C);
}

// ---------------------------------------------------------------- JSON save
static FString SavePath()
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("emberwild_save_v1.json");
}

static FJsonObjectPtr StateToJson(const FGameStateSave& S)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("playerName"), S.PlayerName);
	Root->SetNumberField(TEXT("coins"), S.Coins);
	Root->SetNumberField(TEXT("playSeconds"), S.PlaySeconds);

	auto IntArr = [](const TArray<int32>& A) {
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 V : A) Out.Add(MakeShared<FJsonValueNumber>(V));
		return Out;
	};
	Root->SetArrayField(TEXT("profLevels"), IntArr(S.ProfLevels));
	Root->SetArrayField(TEXT("profXp"), IntArr(S.ProfXp));
	Root->SetArrayField(TEXT("weaponLevels"), IntArr(S.WeaponLevels));
	Root->SetArrayField(TEXT("weaponXp"), IntArr(S.WeaponXp));

	TArray<TSharedPtr<FJsonValue>> Inv;
	for (const FItemStack& Stack : S.Inventory)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), Stack.ItemId);
		O->SetNumberField(TEXT("n"), Stack.Count);
		Inv.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("inventory"), Inv);
	Root->SetStringField(TEXT("equippedItem"), S.EquippedItem);
	Root->SetStringField(TEXT("playerWeapon"), S.PlayerWeapon);
	Root->SetNumberField(TEXT("playerWeaponLevel"), S.PlayerWeaponLevel);
	Root->SetNumberField(TEXT("playerWeaponXp"), S.PlayerWeaponXp);
	Root->SetNumberField(TEXT("jackpotPot"), S.JackpotPot);

	auto CreatureArr = [](const TArray<FCreature>& List) {
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FCreature& C : List)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("species"), C.SpeciesId);
			O->SetStringField(TEXT("nick"), C.Nickname);
			O->SetNumberField(TEXT("lvl"), C.Level);
			O->SetNumberField(TEXT("xp"), C.XP);
			O->SetNumberField(TEXT("hp"), C.CurHP);
			TArray<TSharedPtr<FJsonValue>> Mvs;
			for (const FString& M : C.Moves) Mvs.Add(MakeShared<FJsonValueString>(M));
			O->SetArrayField(TEXT("moves"), Mvs);
			O->SetStringField(TEXT("weapon"), C.Weapon);
			O->SetStringField(TEXT("armor"), C.Armor);
			TSharedRef<FJsonObject> PPO = MakeShared<FJsonObject>();
			for (const TPair<FString, int32>& KV : C.MovePP) PPO->SetNumberField(KV.Key, KV.Value);
			O->SetObjectField(TEXT("pp"), PPO);
			O->SetStringField(TEXT("status"), C.Status);
			O->SetNumberField(TEXT("statusTurns"), C.StatusTurns);
			O->SetBoolField(TEXT("shiny"), C.bShiny);
			O->SetStringField(TEXT("uid"), C.Uid);
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return Out;
	};
	Root->SetArrayField(TEXT("party"), CreatureArr(S.Party));
	Root->SetArrayField(TEXT("box"), CreatureArr(S.Box));

	auto StrArr = [](const TArray<FString>& A) {
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& V : A) Out.Add(MakeShared<FJsonValueString>(V));
		return Out;
	};
	Root->SetArrayField(TEXT("flags"), StrArr(S.Flags));
	Root->SetArrayField(TEXT("activeQuests"), StrArr(S.ActiveQuests));
	Root->SetArrayField(TEXT("doneQuests"), StrArr(S.DoneQuests));
	Root->SetArrayField(TEXT("defeatedTrainers"), StrArr(S.DefeatedTrainers));
	Root->SetArrayField(TEXT("seenSpecies"), StrArr(S.SeenSpecies));
	Root->SetArrayField(TEXT("caughtSpecies"), StrArr(S.CaughtSpecies));
	Root->SetNumberField(TEXT("catchCount"), S.CatchCount);
	Root->SetNumberField(TEXT("battleWins"), S.BattleWins);
	Root->SetNumberField(TEXT("playerX"), S.PlayerX);
	Root->SetNumberField(TEXT("playerZ"), S.PlayerZ);
	Root->SetStringField(TEXT("curZone"), S.CurZone);
	return Root;
}

static bool JsonToState(const FJsonObjectPtr& Root, FGameStateSave& S)
{
	if (!Root.IsValid()) return false;

	// validate against DB: reject saves with unknown species (schema mismatch)
	auto CreatureList = [](const TArray<TSharedPtr<FJsonValue>>& Arr, TArray<FCreature>& Out) -> bool
	{
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			if (!V.IsValid() || !V->AsObject().IsValid()) return false;
			const TSharedRef<FJsonObject> O = V->AsObject().ToSharedRef();
			FCreature C;
			C.SpeciesId = O->GetStringField(TEXT("species"));
			if (!FEmberwildDB::SpeciesDef(C.SpeciesId)) return false;
			C.Nickname = O->GetStringField(TEXT("nick"));
			C.Level = (int32)O->GetNumberField(TEXT("lvl"));
			C.XP = (int32)O->GetNumberField(TEXT("xp"));
			C.CurHP = (int32)O->GetNumberField(TEXT("hp"));
			for (const TSharedPtr<FJsonValue>& M : O->GetArrayField(TEXT("moves")))
				if (M.IsValid()) C.Moves.Add(M->AsString());
			C.Weapon = O->GetStringField(TEXT("weapon"));
			C.Armor = O->GetStringField(TEXT("armor"));
			const TSharedPtr<FJsonObject>* PPO = nullptr;
			if (O->TryGetObjectField(TEXT("pp"), PPO))
				for (const TPair<FString, TSharedPtr<FJsonValue>> KV : (*PPO)->Values)
					if (KV.Value.IsValid()) C.MovePP.Add(KV.Key, (int32)KV.Value->AsNumber());
			C.Status = O->GetStringField(TEXT("status"));
			C.StatusTurns = (int32)O->GetNumberField(TEXT("statusTurns"));
			C.bShiny = O->GetBoolField(TEXT("shiny"));
			C.Uid = O->GetStringField(TEXT("uid"));
			if (C.Uid.IsEmpty()) C.Uid = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(10);
			if (C.Weapon.Len() && !FEmberwildDB::Item(C.Weapon)) C.Weapon = TEXT("");
			if (C.Armor.Len() && !FEmberwildDB::Item(C.Armor)) C.Armor = TEXT("");
			C.NormalizeMoves();
			Out.Add(MoveTemp(C));
		}
		return true;
	};

	S.PlayerName = Root->GetStringField(TEXT("playerName"));
	S.Coins = (int32)Root->GetNumberField(TEXT("coins"));
	S.PlaySeconds = (int32)Root->GetNumberField(TEXT("playSeconds"));
	auto ReadInts = [](const TArray<TSharedPtr<FJsonValue>>& Arr, TArray<int32>& Out, int32 Min) {
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			const int32 Val = V.IsValid() ? (int32)V->AsNumber() : Min;
			Out.Add(FMath::Max(Min, Val));
		}
	};
	ReadInts(Root->GetArrayField(TEXT("profLevels")), S.ProfLevels, 1);
	ReadInts(Root->GetArrayField(TEXT("profXp")), S.ProfXp, 0);
	ReadInts(Root->GetArrayField(TEXT("weaponLevels")), S.WeaponLevels, 0);
	ReadInts(Root->GetArrayField(TEXT("weaponXp")), S.WeaponXp, 0);
	if (S.ProfLevels.Num() != 6) S.ProfLevels = { 1, 1, 1, 1, 1, 1 };
	if (S.ProfXp.Num() != 6) S.ProfXp = { 0, 0, 0, 0, 0, 0 };
	if (S.WeaponLevels.Num() != 5) S.WeaponLevels = { 0, 0, 0, 0, 0 };
	if (S.WeaponXp.Num() != 5) S.WeaponXp = { 0, 0, 0, 0, 0 };

	S.Inventory.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("inventory")))
		if (V.IsValid() && V->AsObject().IsValid())
		{
			const TSharedRef<FJsonObject> O = V->AsObject().ToSharedRef();
			(void)S.Inventory.Emplace_GetRef(O->GetStringField(TEXT("id")), (int32)O->GetNumberField(TEXT("n")));
		}
	S.EquippedItem = Root->GetStringField(TEXT("equippedItem"));
	if (S.EquippedItem.Len() && !FEmberwildDB::Item(S.EquippedItem)) S.EquippedItem = TEXT("");
	S.PlayerWeapon = Root->GetStringField(TEXT("playerWeapon"));
	if (S.PlayerWeapon.Len() && !FEmberwildDB::Item(S.PlayerWeapon)) S.PlayerWeapon = TEXT("");
	S.PlayerWeaponLevel = (int32)Root->GetNumberField(TEXT("playerWeaponLevel"));
	S.PlayerWeaponXp = (int32)Root->GetNumberField(TEXT("playerWeaponXp"));
	S.JackpotPot = (int32)Root->GetNumberField(TEXT("jackpotPot"));

	if (!CreatureList(Root->GetArrayField(TEXT("party")), S.Party)) return false;
	if (!CreatureList(Root->GetArrayField(TEXT("box")), S.Box)) return false;

	auto ReadStrs = [](const TArray<TSharedPtr<FJsonValue>>& Arr) {
		TArray<FString> Out;
		for (const TSharedPtr<FJsonValue>& V : Arr)
			if (V.IsValid()) Out.Add(V->AsString());
		return Out;
	};
	S.Flags = ReadStrs(Root->GetArrayField(TEXT("flags")));
	S.ActiveQuests = ReadStrs(Root->GetArrayField(TEXT("activeQuests")));
	S.DoneQuests = ReadStrs(Root->GetArrayField(TEXT("doneQuests")));
	S.DefeatedTrainers = ReadStrs(Root->GetArrayField(TEXT("defeatedTrainers")));
	S.SeenSpecies = ReadStrs(Root->GetArrayField(TEXT("seenSpecies")));
	S.CaughtSpecies = ReadStrs(Root->GetArrayField(TEXT("caughtSpecies")));
	S.CatchCount = (int32)Root->GetNumberField(TEXT("catchCount"));
	S.BattleWins = (int32)Root->GetNumberField(TEXT("battleWins"));
	S.PlayerX = (float)Root->GetNumberField(TEXT("playerX"));
	S.PlayerZ = (float)Root->GetNumberField(TEXT("playerZ"));
	S.CurZone = Root->GetStringField(TEXT("curZone"));
	return true;
}

bool FEmberwildSave::SaveExists()
{
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*SavePath());
}

void FEmberwildSave::Save(FGameStateSave& State)
{
	State.SyncPPForSave();
	FString Json;
	auto Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(StateToJson(State), Writer);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SavePath()), true);
	FFileHelper::SaveStringToFile(Json, *SavePath(), FFileHelper::EEncodingOptions::ForceUTF8);
}

bool FEmberwildSave::Load(FGameStateSave& OutState)
{
	if (!SaveExists()) return false;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *SavePath())) return false;

	FJsonObjectPtr Root;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	FGameStateSave Loaded;
	if (!JsonToState(Root, Loaded)) return false;
	Loaded.RestorePPAfterLoad();
	OutState = MoveTemp(Loaded);
	return true;
}

void FEmberwildSave::DeleteSave()
{
	IFileManager::Get().Delete(*SavePath());
}

TArray<FString> FEmberwildToast::Pending;
