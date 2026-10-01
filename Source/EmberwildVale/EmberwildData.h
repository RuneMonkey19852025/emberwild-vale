// Emberwild Vale — game data tables.
// Faithful port of the Unity original's CreatureDB / ItemDB / NodeDB / TypeChart / StoryData.
// All content is plain data, exactly like the original: extend these tables to add content.

#pragma once

#include "CoreMinimal.h"
#include "EmberwildData.generated.h"

// ---------------------------------------------------------------- elements
UENUM(BlueprintType)
enum class EElement : uint8
{
	Normal, Ember, Tide, Verdant, Volt, Stone, Gale, Frost, Toxic, Mind, Shade, Beast
};

UENUM(BlueprintType)
enum class EProfessionId : uint8
{
	Forager,   // gathering plants, berries, herbs
	Miner,     // mining ore nodes, stone
	Fisher,    // fishing spots
	Chef,      // cooking food, battle snacks
	Smith,     // smelting ore, forging capture cores and gear
	Tailor     // weaving fibers into apparel, ropes, packs
};

UENUM(BlueprintType)
enum class EItemKind : uint8
{
	Material, Consumable, CaptureCore, KeyItem, Food, Equipment
};

/** Melee weapon classes the PLAYER swings in the overworld. */
UENUM(BlueprintType)
enum class EPlayerWeaponKind : uint8
{
	None = 0, Club, Blade, Spear, Axe
};

/** Weapon classes for mastery. None covers bare attacks (no XP). */
UENUM(BlueprintType)
enum class EWeaponClass : uint8
{
	None = 0, Claws, Fangs, Spines, Plates, Charms
};

UENUM(BlueprintType)
enum class EStatusKind : uint8
{
	None, Poison, Burn, Sleep, Paralyze
};

// ---------------------------------------------------------------- items
USTRUCT(BlueprintType)
struct FItemDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) FString Desc;
	UPROPERTY(EditAnywhere) EItemKind Kind = EItemKind::Material;
	UPROPERTY(EditAnywhere) int32 BasePrice = 0;      // shop sell/buy baseline
	UPROPERTY(EditAnywhere) bool bSellable = true;
	UPROPERTY(EditAnywhere) FString ProfessionId;     // which profession yields/crafts it ("" for none)
	UPROPERTY(EditAnywhere) FString IconColorHex = "#B9C4CC"; // UI swatch
	UPROPERTY(EditAnywhere) int32 HealAmount = 0;     // for consumables/food: creature HP restored
	UPROPERTY(EditAnywhere) int32 ConditionCure = 0;  // 0 none, 1 cures status
	UPROPERTY(EditAnywhere) float CatchBonus = 1.f;   // for CaptureCore items
	UPROPERTY(EditAnywhere) FString EquipBonus;       // e.g. "gather+1", "atk+3|spd+1"
	UPROPERTY(EditAnywhere) EPlayerWeaponKind PlayerWeaponKind = EPlayerWeaponKind::None;
};

USTRUCT(BlueprintType)
struct FItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString ItemId;
	UPROPERTY(EditAnywhere) int32 Count = 0;

	FItemStack() {}
	FItemStack(const FString& InId, int32 InCount) : ItemId(InId), Count(InCount) {}
};

USTRUCT(BlueprintType)
struct FRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString ResultItemId;
	UPROPERTY(EditAnywhere) int32 ResultCount = 1;
	UPROPERTY(EditAnywhere) EProfessionId Profession = EProfessionId::Forager;
	UPROPERTY(EditAnywhere) int32 RequiredLevel = 1;
	UPROPERTY(EditAnywhere) int32 XpReward = 8;
	UPROPERTY(EditAnywhere) TArray<FItemStack> Inputs;
	UPROPERTY(EditAnywhere) FString Station; // "campfire", "forge", "workbench", "" = anywhere
};

// ---------------------------------------------------------------- moves & species
USTRUCT(BlueprintType)
struct FMoveDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) EElement Element = EElement::Normal;
	UPROPERTY(EditAnywhere) int32 Power = 0;
	UPROPERTY(EditAnywhere) int32 Accuracy = 100;  // 0 = never misses
	UPROPERTY(EditAnywhere) int32 PP = 10;
	UPROPERTY(EditAnywhere) FString Desc;
	UPROPERTY(EditAnywhere) FString Effect;   // "", "buff_atk", "debuff_def", "heal", "drain", "status", "buff_atk_def"
	UPROPERTY(EditAnywhere) FString StatusId; // applied status, if any
	UPROPERTY(EditAnywhere) int32 StatusChance = 0; // percent
};

USTRUCT(BlueprintType)
struct FSpeciesDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) EElement Element = EElement::Normal;
	UPROPERTY(EditAnywhere) EElement Element2 = EElement::Normal; // second type or same as first
	UPROPERTY(EditAnywhere) int32 BaseHP = 10;
	UPROPERTY(EditAnywhere) int32 BaseATK = 10;
	UPROPERTY(EditAnywhere) int32 BaseDEF = 10;
	UPROPERTY(EditAnywhere) int32 BaseSPD = 10;
	UPROPERTY(EditAnywhere) FString Desc;
	UPROPERTY(EditAnywhere) FString EvolvesInto;  // "" if none
	UPROPERTY(EditAnywhere) int32 EvolveLevel = 0;
	UPROPERTY(EditAnywhere) TArray<FString> Learnset; // move ids by level order (level = 1+index)
	UPROPERTY(EditAnywhere) FString PaletteHex;  // primary body color
	UPROPERTY(EditAnywhere) FString AccentHex;   // secondary/accent color
	UPROPERTY(EditAnywhere) FString Shape;       // "quad","serpent","avian","aquatic","rock","spirit","bug"
	UPROPERTY(EditAnywhere) float Scale = 1.f;
	UPROPERTY(EditAnywhere) int32 CatchRate = 45;  // higher = easier
	UPROPERTY(EditAnywhere) int32 XpYield = 24;
	UPROPERTY(EditAnywhere) FString HabitatZone;   // where wild ones spawn
};

// ---------------------------------------------------------------- gathering nodes
USTRUCT(BlueprintType)
struct FNodeDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) EProfessionId Profession = EProfessionId::Forager;
	UPROPERTY(EditAnywhere) int32 RequiredLevel = 1;
	UPROPERTY(EditAnywhere) TArray<FString> ItemPool; // pick weighted, last entry rare
	UPROPERTY(EditAnywhere) TArray<int32> PoolWeights;
	UPROPERTY(EditAnywhere) int32 BaseYield = 1;
	UPROPERTY(EditAnywhere) int32 XpReward = 8;
	UPROPERTY(EditAnywhere) FString Zone;
};

// ---------------------------------------------------------------- story
USTRUCT(BlueprintType)
struct FNpcDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) FString Role;
	UPROPERTY(EditAnywhere) FString ColorHex;
	UPROPERTY(EditAnywhere) FString Zone;
	UPROPERTY(EditAnywhere) TArray<FString> DialogueNodes;
};

USTRUCT(BlueprintType)
struct FQuestDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) FString Summary;
	UPROPERTY(EditAnywhere) FString GiverNpcId;
	UPROPERTY(EditAnywhere) FString Act;
	UPROPERTY(EditAnywhere) TArray<FString> PrereqQuests;
	UPROPERTY(EditAnywhere) TArray<FString> RequiresFlags;
	UPROPERTY(EditAnywhere) TArray<FString> ForbidsFlags;
	UPROPERTY(EditAnywhere) TArray<FString> Objectives;
	UPROPERTY(EditAnywhere) TArray<FString> OnAcceptFlags;
	UPROPERTY(EditAnywhere) TArray<FString> OnCompleteFlags;
	UPROPERTY(EditAnywhere) TArray<FString> OnAcceptGiveItems;
	UPROPERTY(EditAnywhere) TArray<FString> OnCompleteGiveItems;
	UPROPERTY(EditAnywhere) TArray<FString> OnAcceptTeach;
	UPROPERTY(EditAnywhere) TArray<FString> OnCompleteTeach;
	UPROPERTY(EditAnywhere) int32 RewardCoins = 0;
};

USTRUCT(BlueprintType)
struct FDialogueLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Speaker;
	UPROPERTY(EditAnywhere) FString Text;
	UPROPERTY(EditAnywhere) FString QuestId;       // offers this quest
	UPROPERTY(EditAnywhere) FString SetFlag;       // unconditional flag on this line
	UPROPERTY(EditAnywhere) FString GiveItem;      // "itemId:count"
	UPROPERTY(EditAnywhere) FString Teach;         // "profId:level"
	UPROPERTY(EditAnywhere) FString SpawnCreature; // "speciesId:level"
	UPROPERTY(EditAnywhere) FString SpawnTrainer;  // trainerId
	UPROPERTY(EditAnywhere) bool bHealParty = false; // free full party heal
	UPROPERTY(EditAnywhere) FString OpenShop;      // shopId
	UPROPERTY(EditAnywhere) FString OpenCraft;     // station id ("forge","campfire","workbench")
	UPROPERTY(EditAnywhere) FString EndRule;       // "" = advance, "end", "choice"
	UPROPERTY(EditAnywhere) FString YesNode;
	UPROPERTY(EditAnywhere) FString NoNode;
};

USTRUCT(BlueprintType)
struct FDialogueNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) TArray<FDialogueLine> Lines;
};

USTRUCT(BlueprintType)
struct FTrainerDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) FString Title;
	UPROPERTY(EditAnywhere) TArray<FString> Team; // "species:level"
	UPROPERTY(EditAnywhere) TArray<FString> PreBattle;
	UPROPERTY(EditAnywhere) TArray<FString> WinLines;
	UPROPERTY(EditAnywhere) TArray<FString> LoseLines;
	UPROPERTY(EditAnywhere) TArray<FString> OnDefeatFlags;
	UPROPERTY(EditAnywhere) TArray<FString> OnDefeatGiveItems;
	UPROPERTY(EditAnywhere) int32 RewardCoins = 80;
};

USTRUCT(BlueprintType)
struct FShopDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FString Id;
	UPROPERTY(EditAnywhere) FString Name;
	UPROPERTY(EditAnywhere) TArray<FString> Stock; // "itemId:count" (-1 = infinite)
};
