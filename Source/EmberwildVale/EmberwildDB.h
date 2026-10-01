// Emberwild Vale — static content database (all tables initialized at boot).
// Port of the Unity original's CreatureDB / ItemDB / NodeDB / TypeChart / StoryData.

#pragma once

#include "CoreMinimal.h"
#include "EmberwildData.h"

class UEmberwildGameInstance;

struct FTypeChart
{
	// attacker -> defender -> multiplier; anything absent = 1.0
	static float Effectiveness(EElement Attack, EElement Defender);
	static float Effectiveness(EElement Attack, EElement Def1, EElement Def2);
	static FString Label(float Mult);
	static FLinearColor Color(EElement E);
};

struct FWildLootDrop
{
	FString Id;
	int32 N = 1;
	FWildLootDrop() {}
	FWildLootDrop(const FString& InId, int32 InN) : Id(InId), N(InN) {}
};

/** Static content + pure helper lookups. Initialized once from the GameInstance. */
struct FEmberwildDB
{
	// ---------------- tables ----------------
	static TMap<FString, FMoveDef> Moves;
	static TMap<FString, FSpeciesDef> Species;
	static TArray<FString> SpeciesOrder;
	static TMap<FString, FItemDef> Items;
	static TArray<FRecipe> Recipes;
	static TMap<FString, FNodeDef> Nodes;
	static TMap<FString, FQuestDef> Quests;
	static TMap<FString, FDialogueNode> Dialogue;
	static TMap<FString, FTrainerDef> Trainers;
	static TMap<FString, FShopDef> Shops;
	static TMap<FString, FNpcDef> Npcs;

	static void Init();

	// ---------------- lookup helpers ----------------
	static const FMoveDef* Move(const FString& Id);
	static const FSpeciesDef* SpeciesDef(const FString& Id);
	static const FItemDef* Item(const FString& Id);
	static const FNodeDef* Node(const FString& Id);
	static const FQuestDef* Quest(const FString& Id);
	static const FDialogueNode* DialogueNode(const FString& Id);
	static const FTrainerDef* Trainer(const FString& Id);
	static const FShopDef* Shop(const FString& Id);
	static const FNpcDef* Npc(const FString& Id);
	static const FRecipe* Recipe(const FString& Id);

	static FString ItemName(const FString& Id);   // falls back to the id
	static FString SafeMove(const FString& Id);   // typo-proof: resolves to "tackle"
	static bool IsBaseForm(const FString& SpeciesId);
	static TArray<FString> WildPoolFor(const FString& Zone);
	static TArray<FWildLootDrop> RollWildLoot(const FString& SpeciesId, int32 Level);
	static FString RollNodeItem(const FNodeDef& NodeDef);
	static int32 XpToNextCreature(int32 Level) { return 18 + Level * 9; }
	static int32 MaxCreatureLevel() { return 200; }

private:
	static void InitMoves();
	static void InitSpecies();
	static void InitItems();
	static void InitNodes();
	static void InitStory();
	static TSet<FString> EvolvedForms;
};
