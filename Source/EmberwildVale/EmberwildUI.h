// Emberwild Vale — UI layer.
// Port of the Unity original's UIFactory + UiRouter + all panels. The panels
// are built in code from Slate widgets; the router owns them like the original.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "EmberwildData.h"
#include "EmberwildState.h"

class UEmberwildSubsystem;

// ---------------------------------------------------------------- theme
namespace EmberwildTheme
{
	// hex string -> linear color (works in headers; FLinearColor::FromHex is engine-internal)
	inline FLinearColor HexToLinear(const TCHAR* Hex)
	{
		const FString S(Hex);
		if (S.Len() == 7 && S[0] == TEXT('#'))
		{
			const int32 R = FParse::HexDigit(S[1]) * 16 + FParse::HexDigit(S[2]);
			const int32 G = FParse::HexDigit(S[3]) * 16 + FParse::HexDigit(S[4]);
			const int32 B = FParse::HexDigit(S[5]) * 16 + FParse::HexDigit(S[6]);
			return FLinearColor(R / 255.f, G / 255.f, B / 255.f, 1.f);
		}
		return FLinearColor::White;
	}
}

struct FUITheme
{
	static FLinearColor Bg()        { return FLinearColor(0.09f, 0.10f, 0.12f, 0.96f); }
	static FLinearColor BgSoft()    { return FLinearColor(0.13f, 0.15f, 0.18f, 0.97f); }
	static FLinearColor Panel()     { return FLinearColor(0.17f, 0.19f, 0.22f, 1.f); }
	static FLinearColor Line()      { return FLinearColor(0.28f, 0.31f, 0.35f, 1.f); }
	static FLinearColor Text()      { return FLinearColor(0.92f, 0.93f, 0.95f, 1.f); }
	static FLinearColor TextDim()   { return FLinearColor(0.62f, 0.66f, 0.70f, 1.f); }
	static FLinearColor Accent()    { return EmberwildTheme::HexToLinear(TEXT("#F2994A")); }
	static FLinearColor Accent2()   { return EmberwildTheme::HexToLinear(TEXT("#6FCF97")); }
	static FLinearColor Danger()    { return EmberwildTheme::HexToLinear(TEXT("#EB5757")); }
	static FLinearColor Gold()      { return EmberwildTheme::HexToLinear(TEXT("#F2CC58")); }
};

// ---------------------------------------------------------------- factory helpers
class FUIFactory
{
public:
	static TSharedRef<SWidget> Padded(const TSharedRef<SWidget>& Child, float Pad);
	static FText F(const FString& S) { return FText::FromString(S); }
};

// ---------------------------------------------------------------- slots machine
struct FSlotsMachine
{
	// pay table (port of SlotsPanel.cs)
	static constexpr int32 SpinCost = 25;
	static constexpr int32 StartingPot = 150;
	static const TCHAR* SymGlyph(int32 Sym) { static const TCHAR* G[] = { TEXT("o"), TEXT("/"), TEXT("H"), TEXT("*") }; return G[Sym]; }
	static const TCHAR* SymName(int32 Sym)  { static const TCHAR* N[] = { TEXT("Berries"), TEXT("Pick"), TEXT("Hammer"), TEXT("Star") }; return N[Sym]; }
	static int32 RollSymbol();
	static float StreakMultiplier(int32 Streak) { return 1.f + 0.25f * FMath::Min(Streak, 4); }

	// resolve a spin: fills payouts; returns true when the spin wins
	struct FSpinResult
	{
		int32 Symbols[3] = { 0, 0, 0 };
		int32 CoinsWon = 0;
		FString ItemWon;
		int32 ItemQty = 0;
		bool bJackpot = false;
		bool bWin = false;
		FString Headline;
	};
	static FSpinResult Resolve(FGameStateSave& Save, int32& WinStreak);
};

// ---------------------------------------------------------------- router
class SEmberwildUIRouter : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEmberwildUIRouter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Boot(UEmberwildSubsystem* InSub);

	// routing API called by the subsystem & player pawn
	/** False while any panel is open: the pawn suppresses movement/swing. */
	bool IsWorldInputAllowed() const { return OpenPanel.IsEmpty() && !bTitleActive; }
	/** Menu key handling, also called directly by the pawn's key bindings. */
	bool HandlePanelKey(const FKey& Key);
	void OpenDialogue();
	void ShowDialogueLine(const FDialogueLine& Line);
	void CloseDialogue();
	void OpenBattle();
	void OpenCrafting(const FString& StationId);
	void OpenShop(const FString& ShopId);
	void OpenSlots();
	void TogglePanel(const FString& PanelId);
	void CloseTop();

	// world title screen gate
	bool bTitleActive = true;

private:
	// ---- which overlay is up: "" = none; single-active model like the original ----
	FString OpenPanel = TEXT("title");
	TWeakObjectPtr<UEmberwildSubsystem> Sub;
	TSharedPtr<class SBox> CurrentPanelBox;
	FString LastBuiltPanel;

public:
	// battle state (public: the headless autopilot drives/verifies through it)
	TArray<FString> BattleLog;
	FString BattleMode;         // "menu","moves","bag","party"
	int32 BattleCursor = 0;
	TArray<FString> BattleMenuItems;
	TArray<FString> BattleMoveIds;
	bool bBattleOver = false;

private:
	// dialogue state
	FString DlgSpeaker, DlgText;
	TArray<FString> DlgChoices; // {"Yes","No"} while a choice is pending
	bool bChoicePending = false;

	// crafting
	FString CraftStation;
	int32 CraftCursor = 0;

	// shop
	FString ShopId;
	int32 ShopCursor = 0;

	// slots
	int32 FinalSymbols[3] = { 0, 0, 0 };
	bool bSpinning = false;
	float SpinTimer = 0.f;
	FString SlotsResult;
	int32 WinStreak = 0;

	// generic list panels (bag/party/gear/weapons/journal)
	int32 ListCursor = 0;
	FString ListHint;

	// toasts
	struct FToast { FString Msg; float Remaining; };
	TArray<FToast> Toasts;

	// ---- building ----
	TSharedRef<class SBoxPanel> BuildRoot();
	TSharedRef<SWidget> BuildPanelContent(const FString& PanelId);
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	// ---- input ----
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	void BuildBattleMenu();
	bool HandleBattleKey(const FKey& Key);
	bool HandleDialogueKey(const FKey& Key);
	bool HandleSlotsKey(const FKey& Key);
	bool HandleListKey(const FKey& Key, const FString& PanelId);

	// ---- actions ----
	void DoPlayerMove(const FString& MoveId);
	void DoBattleMenuItem(int32 Idx);
	void DoUseItem(int32 InvIdx);
	void DoSwitchTo(int32 PartyIdx);
	void DoBuy(int32 StockIdx);
	void DoSell(const FString& ItemId);
	void DoCraft(const FRecipe& Recipe);
	void DoSpin();
	void EquipTrainerItem(const FString& ItemId);
	void EquipCreatureGear(int32 PartyIdx, const FString& Slot, const FString& ItemId);
	void ReleaseCreature(int32 PartyIdx);
	void ProcessDefeat();

	// ---- helpers ----
	void Route(UEmberwildSubsystem* InSub) { Sub = InSub; }
};
