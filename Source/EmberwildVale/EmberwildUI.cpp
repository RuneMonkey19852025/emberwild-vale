// Emberwild Vale — UI implementation.
// All panels built in code with Slate; state-driven text updates each tick.
// Key handling mirrors the original's keyboard-driven menus (arrows/E/Esc).

#include "EmberwildUI.h"
#include "EmberwildDB.h"
#include "EmberwildSystems.h"
#include "EmberwildGame.h"
#include "EmberwildAudio.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Widgets/SNullWidget.h"

namespace
{
	// hex string -> linear color, independent of FLinearColor::FromHex
	FLinearColor ThemeHex(const TCHAR* Hex)
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

// ---------------------------------------------------------------- factory
TSharedRef<SWidget> FUIFactory::Padded(const TSharedRef<SWidget>& Child, float Pad)
{
	return SNew(SBorder)
		.Padding(Pad)
		[
			Child
		];
}

// ---------------------------------------------------------------- slots logic
int32 FSlotsMachine::RollSymbol()
{
	// weights per symbol on each reel (berries common, star rare)
	static const int32 Weights[] = { 6, 4, 3, 1 };
	int32 Total = 0;
	for (int32 W : Weights) Total += W;
	int32 Roll = FMath::RandRange(0, Total - 1);
	for (int32 i = 0; i < 4; ++i)
	{
		Roll -= Weights[i];
		if (Roll < 0) return i;
	}
	return 0;
}

FSlotsMachine::FSpinResult FSlotsMachine::Resolve(FGameStateSave& Save, int32& WinStreak)
{
	FSpinResult R;
	for (int32 i = 0; i < 3; ++i) R.Symbols[i] = RollSymbol();

	static const TCHAR* WinItems[] = { TEXT("fiber"), TEXT("stone"), TEXT("copper_bar"), TEXT("moonshard") };
	static const int32 WinItemQty[] = { 2, 2, 1, 1 };
	static const int32 PairPay[] = { 10, 15, 20, 40 };
	static const int32 TriplePay[] = { 60, 90, 150, 500 };

	const bool bTriple = R.Symbols[0] == R.Symbols[1] && R.Symbols[1] == R.Symbols[2];
	int32 MatchSym = -1;
	if (R.Symbols[0] == R.Symbols[1] || R.Symbols[1] == R.Symbols[2] || R.Symbols[0] == R.Symbols[2])
	{
		if (R.Symbols[0] == R.Symbols[1] || R.Symbols[0] == R.Symbols[2]) MatchSym = R.Symbols[0];
		else MatchSym = R.Symbols[1];
	}

	if (bTriple)
	{
		R.CoinsWon = TriplePay[R.Symbols[0]];
		R.ItemWon = WinItems[R.Symbols[0]];
		R.ItemQty = R.Symbols[0] == 3 ? 1 : WinItemQty[R.Symbols[0]] + 1;
	}
	else if (MatchSym >= 0)
	{
		R.CoinsWon = PairPay[MatchSym];
		R.ItemWon = WinItems[MatchSym];
		R.ItemQty = WinItemQty[MatchSym];
	}

	if (R.CoinsWon > 0)
	{
		R.bWin = true;
		if (bTriple && R.Symbols[0] == 3)
		{
			R.bJackpot = true;
			R.CoinsWon += Save.JackpotPot;
			Save.JackpotPot = StartingPot;
		}

		WinStreak++;
		const int32 BaseCoins = R.CoinsWon;
		R.CoinsWon = FMath::RoundToInt(R.CoinsWon * StreakMultiplier(WinStreak));
		const int32 BonusQty = WinStreak >= 3 ? FMath::Min(3, WinStreak - 1) : 0;

		Save.Coins += R.CoinsWon;
		if (!R.ItemWon.IsEmpty())
		{
			Save.AddItem(R.ItemWon, R.ItemQty);
			if (BonusQty > 0) Save.AddItem(R.ItemWon, BonusQty);
		}

		if (R.bJackpot)
			R.Headline = FString::Printf(TEXT("* JACKPOT! *\n+%d coins  +%d %s\nThe pot resets to %dc."),
				R.CoinsWon, R.ItemQty + BonusQty, *FEmberwildDB::ItemName(R.ItemWon), Save.JackpotPot);
		else
			R.Headline = FString::Printf(TEXT("%s%s\n+%d coins  +%d %s"),
				bTriple ? TEXT("TRIPLE! ") : TEXT("Pair! "),
				SymName(R.Symbols[0]), R.CoinsWon, R.ItemQty + BonusQty,
				*FEmberwildDB::ItemName(R.ItemWon));
	}
	else
	{
		const int32 Feed = SpinCost - 5;
		Save.JackpotPot += Feed;
		R.Headline = WinStreak >= 2
			? FString::Printf(TEXT("The streak breaks at x%d — %dc rolls into the jackpot."), WinStreak, Feed)
			: FString::Printf(TEXT("No luck — %dc rolls into the jackpot."), Feed);
		WinStreak = 0;
	}
	return R;
}

// ---------------------------------------------------------------- router construct
void SEmberwildUIRouter::Construct(const FArguments& InArgs)
{
	SetCanTick(true);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			BuildRoot()
		]
	];
}

void SEmberwildUIRouter::Boot(UEmberwildSubsystem* InSub)
{
	Sub = InSub;
}

// ---------------------------------------------------------------- root build
TSharedRef<SBoxPanel> SEmberwildUIRouter::BuildRoot()
{
	// The root rebuilds content every frame via STextBlock updates stored as
	// shared pointers. For simplicity and robustness the whole panel content
	// is re-rendered as text each Tick through assigned lambdas.
	return SNew(SVerticalBox)

		// ================= HUD (visible outside the title screen) =================
		+ SVerticalBox::Slot().AutoHeight().Padding(10)
		[
			SNew(SBorder)
			.BorderBackgroundColor(FUITheme::Bg())
			.Padding(FMargin(10, 6))
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::Text())
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.Text_Lambda([this]() {
					if (!Sub.IsValid()) return FText::GetEmpty();
					FGameStateSave& S = Sub->Save;
					const FCreature* Lead = S.Party.Num() > 0 ? &S.Party[0] : nullptr;
					FString Hud = FString::Printf(TEXT("%d coins | Zone: %s | Weapon: %s"),
						S.Coins, *Sub->CurrentZone, *FPlayerCombat::MasteryLabel(S));
					if (Lead && Lead->Species())
						Hud += FString::Printf(TEXT("  |  %s Lv%d  %d/%d HP"), *Lead->DisplayName(), Lead->Level, Lead->CurHP, Lead->MaxHP());
					return FUIFactory::F(Hud);
				})
			]
		]

		// ================= interact prompt =================
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 0)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FUITheme::Gold())
			.Text_Lambda([this]() {
				// prompt handled by HUD lambda when near something; kept simple
				return FText::GetEmpty();
			})
		]

		// ================= toasts (bottom-left stack) =================
		+ SVerticalBox::Slot().AutoHeight().VAlign(VAlign_Bottom).Padding(10, 4)
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0, 0, 0, 0))
			.Padding(0)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::Accent2())
				.Text_Lambda([this]() {
					FString Out;
					for (const FToast& T : Toasts) Out += T.Msg + TEXT("\n");
					return FUIFactory::F(Out);
				})
			]
		]

		// ================= active panel overlay =================
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(10)
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0, 0, 0, 0))
			.Padding(0)
			[
				SNew(SBox)
				[
					SAssignNew(CurrentPanelBox, SBox)
				]
			]
		];
}

// ---------------------------------------------------------------- panel content
TSharedRef<SWidget> SEmberwildUIRouter::BuildPanelContent(const FString& PanelId)
{
	FGameStateSave& S = Sub->Save;

	// ---------------- title ----------------
	if (PanelId == TEXT("title"))
	{
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(24)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
				.ColorAndOpacity(FUITheme::Accent())
				.Text(FText::FromString(TEXT("EMBERWILD VALE")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::TextDim())
				.Text(FText::FromString(TEXT("A creature-taming RPG with professions, crafting and a 3-act story.\n\nE - advance dialogue / interact      Space - swing weapon\nI - Bag    G - Gear    J - Journal    C - Creatures    Q - Weapons\nF5 - Save    Esc - close panels\n\nWASD move · mouse wheel zoom · walk in the grass to meet wild creatures.")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12)
			[
				SNew(SButton)
				.Text(FText::FromString(FEmberwildSave::SaveExists() ? TEXT("CONTINUE  [E]") : TEXT("BEGIN  [E]")))
				.OnClicked_Lambda([this]() {
					OpenPanel = TEXT("");
					bTitleActive = false;
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("NEW GAME")))
				.OnClicked_Lambda([this]() {
					if (Sub.IsValid())
					{
						Sub->StartNewGame();
						bTitleActive = false;
					}
					return FReply::Handled();
				})
			]
		];
	}

	// ---------------- dialogue ----------------
	if (PanelId == TEXT("dialogue"))
	{
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FUITheme::Accent())
				.Text_Lambda([this]() { return FUIFactory::F(DlgSpeaker); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FUITheme::Text())
				.Text_Lambda([this]() { return FUIFactory::F(DlgText); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::TextDim())
				.Text_Lambda([this]() {
					if (bChoicePending) return FText::FromString(TEXT("[Y] Yes    [N] No"));
					return FText::FromString(TEXT("[E] Continue"));
				})
			]
		];
	}

	// ---------------- battle ----------------
	if (PanelId == TEXT("battle"))
	{
		FString MenuStr;
		for (int32 i = 0; i < BattleMenuItems.Num(); ++i)
			MenuStr += FString::Printf(TEXT("%s%s %s\n"), i == BattleCursor ? TEXT("> ") : TEXT("  "),
				i == BattleCursor ? TEXT("*") : TEXT(" "), *BattleMenuItems[i]);

		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
					.ColorAndOpacity(FUITheme::Text())
					.Text_Lambda([this]() {
						if (!Sub.IsValid() || !Sub->Battle) return FText::FromString(TEXT("---"));
						const FCreature* P = Sub->Battle->PlayerMon();
						const FCreature* E = Sub->Battle->EnemyMon();
						if (!P || !E) return FText::FromString(TEXT("---"));
						return FUIFactory::F(FString::Printf(
							TEXT("%s Lv%d  %d/%d HP%s      VS      %s Lv%d  %d/%d HP%s"),
							*P->DisplayName(), P->Level, P->CurHP, P->MaxHP(),
							P->Status.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *P->Status),
							*E->DisplayName(), E->Level, E->CurHP, E->MaxHP(),
							E->Status.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *E->Status)));
					})
				]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 10)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FUITheme::TextDim())
						.Text_Lambda([this]() {
							FString Out;
							for (const FString& L : BattleLog) Out += L + TEXT("\n");
							return FUIFactory::F(Out);
						})
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().MinWidth(220.f).VAlign(VAlign_Bottom)
			[
				SNew(SBorder).BorderBackgroundColor(FUITheme::BgSoft()).Padding(12)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 12))
					.ColorAndOpacity(FUITheme::Text())
					.Text_Lambda([this]() {
						FString MenuStr;
						for (int32 i = 0; i < BattleMenuItems.Num(); ++i)
							MenuStr += FString::Printf(TEXT("%s%s %s\n"), i == BattleCursor ? TEXT("> ") : TEXT("  "),
								i == BattleCursor ? TEXT("*") : TEXT(" "), *BattleMenuItems[i]);
						return FUIFactory::F(MenuStr);
					})
				]
			]
		];
	}

	// ---------------- slots ----------------
	if (PanelId == TEXT("slots"))
	{
		auto ReelText = [this](int32 i) {
			FString Sym = TEXT("?");
			if (!bSpinning) Sym = FSlotsMachine::SymGlyph(FinalSymbols[i]);
			else if (SpinTimer <= 0.f) Sym = FSlotsMachine::SymGlyph(FinalSymbols[i]);
			return FUIFactory::F(Sym);
		};
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
				.ColorAndOpacity(FUITheme::Accent())
				.Text(FText::FromString(TEXT("THE LUCKY SNORLAX")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::TextDim())
				.Text_Lambda([this]() {
					if (!Sub.IsValid()) return FText::GetEmpty();
					return FUIFactory::F(FString::Printf(TEXT("%d coins    JACKPOT %dc    25c a spin"),
						Sub->Save.Coins, Sub->Save.JackpotPot));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8)[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 40)).Text_Lambda([ReelText]() { return ReelText(0); }) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8)[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 40)).Text_Lambda([ReelText]() { return ReelText(1); }) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8)[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 40)).Text_Lambda([ReelText]() { return ReelText(2); }) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FUITheme::Gold())
				.Text_Lambda([this]() { return FUIFactory::F(SlotsResult.IsEmpty() ? TEXT("Pull the lever! [E]") : SlotsResult); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::TextDim())
				.Text(FText::FromString(TEXT("ooo 60c+fiber   /// 90c+stone   HHH 150c+copper   *** 500c + THE JACKPOT\nany pair 10-40c + material  -  losses feed the pot  -  hot hand: up to +100% coins")))
			]
		];
	}

	// ---------------- crafting ----------------
	if (PanelId == TEXT("crafting"))
	{
		FString List;
		int32 Idx = 0;
		for (const FRecipe& R : FEmberwildDB::Recipes)
		{
			// parity (CraftingPanel.cs:82): show "anywhere" recipes at every station
			if (!R.Station.IsEmpty() && R.Station != CraftStation) continue;
			const bool bCan = FProfessionSystem::CanCraft(S, R, CraftStation);
			FString Line = FString::Printf(TEXT("%s%dx %s"), Idx == CraftCursor ? TEXT("> ") : TEXT("   "),
				R.ResultCount, *FEmberwildDB::ItemName(R.ResultItemId));
			Line += FString::Printf(TEXT("   [%s Lv%d]"), FProfessionSystem::Name(R.Profession), R.RequiredLevel);
			if (!bCan)
			{
				const FString Blocker = FProfessionSystem::CraftBlocker(S, R, CraftStation);
				Line += TEXT("   (") + Blocker + TEXT(")");
			}
			List += Line + TEXT("\n");
			++Idx;
		}
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
				.ColorAndOpacity(FUITheme::Accent())
				.Text_Lambda([this]() { return FUIFactory::F(TEXT("CRAFTING - ") + CraftStation); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 12))
				.Text(FText::FromString(List))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::TextDim())
				.Text(FText::FromString(TEXT("[up/down] select   [E] craft   [Esc] close")))
			]
		];
	}

	// ---------------- shop ----------------
	if (PanelId == TEXT("shop"))
	{
		const FShopDef* ShopDef = FEmberwildDB::Shop(ShopId);
		FString List;
		if (ShopDef)
		{
			for (int32 i = 0; i < ShopDef->Stock.Num(); ++i)
			{
				TArray<FString> Parts;
				ShopDef->Stock[i].ParseIntoArray(Parts, TEXT(":"));
				const FItemDef* D = FEmberwildDB::Item(Parts[0]);
				List += FString::Printf(TEXT("%s%-18s %5dc   (sell %dc)\n"),
					i == ShopCursor ? TEXT("> ") : TEXT("   "),
					D ? *D->Name : *Parts[0],
					D ? D->BasePrice : 0,
					D ? FMath::Max(1, D->BasePrice / 2) : 0);
			}
		}
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
				.ColorAndOpacity(FUITheme::Accent())
				.Text_Lambda([this, &S]() { return FUIFactory::F(TEXT("SHOP - ") + ShopId + FString::Printf(TEXT("   (%d coins)"), S.Coins)); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 12))
				.Text(FText::FromString(List))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FUITheme::TextDim())
				.Text(FText::FromString(TEXT("[up/down] select   [E] buy   [B] sell   [Esc] close")))
			]
		];
	}

	// ---------------- party / creatures ----------------
	if (PanelId == TEXT("party"))
	{
		FString List = TEXT("PARTY\n");
		for (int32 i = 0; i < S.Party.Num(); ++i)
		{
			const FCreature& C = S.Party[i];
			if (!C.Species()) continue;
			List += FString::Printf(TEXT("%s%s Lv%d   %d/%d HP   weapon:%s armor:%s\n"),
				i == ListCursor ? TEXT("> ") : TEXT("   "),
				*C.DisplayName(), C.Level, C.CurHP, C.MaxHP(),
				C.Weapon.IsEmpty() ? TEXT("-") : *FEmberwildDB::ItemName(C.Weapon),
				C.Armor.IsEmpty() ? TEXT("-") : *FEmberwildDB::ItemName(C.Armor));
		}
		List += TEXT("\nBOX\n");
		for (const FCreature& C : S.Box)
			if (C.Species())
				List += FString::Printf(TEXT("   %s Lv%d\n"), *C.DisplayName(), C.Level);
		List += TEXT("\n[up/down] select   [W] weapon   [A] armor   [X] to box   [Esc] close");
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 12)).Text(FText::FromString(List)) ];
	}

	// ---------------- bag ----------------
	if (PanelId == TEXT("bag"))
	{
		FString List;
		for (int32 i = 0; i < S.Inventory.Num(); ++i)
		{
			const FItemStack& St = S.Inventory[i];
			const FItemDef* D = FEmberwildDB::Item(St.ItemId);
			FString Tag;
			if (D && D->Kind == EItemKind::CaptureCore) Tag = TEXT("  [core]");
			if (D && (D->Kind == EItemKind::Food || D->Kind == EItemKind::Consumable)) Tag = TEXT("  [use]");
			if (D && D->Kind == EItemKind::Equipment) Tag = TEXT("  [equip]");
			if (S.EquippedItem == St.ItemId) Tag = TEXT("  [EQUIPPED]");
			List += FString::Printf(TEXT("%s%-18s x%-3d%s\n"),
				i == ListCursor ? TEXT("> ") : TEXT("   "),
				D ? *D->Name : *St.ItemId, St.Count, *Tag);
		}
		if (S.Inventory.Num() == 0) List = TEXT("(bag is empty)");
		List += TEXT("\n[up/down] select   [E] use/equip   [Esc] close");
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 12)).Text(FText::FromString(List)) ];
	}

	// ---------------- gear (trainer equipment + mastery) ----------------
	if (PanelId == TEXT("gear"))
	{
		FString List = FString::Printf(TEXT("Trainer weapon: %s\nEquipped gear: %s\n\n"),
			*FPlayerCombat::MasteryLabel(S),
			S.EquippedItem.IsEmpty() ? TEXT("-") : *FEmberwildDB::ItemName(S.EquippedItem));
		List += TEXT("Professions\n");
		for (int32 i = 0; i < 6; ++i)
		{
			const EProfessionId P = (EProfessionId)i;
			List += FString::Printf(TEXT("  %s Lv%d   (%d/%d xp)\n"),
				FProfessionSystem::Name(P), S.ProfLevel(P), S.ProfXpOf(P),
				FProfessionSystem::XpToNext(S.ProfLevel(P)));
		}
		List += TEXT("\nGear mastery (creature equipment)\n");
		for (int32 i = 1; i < 6; ++i)
		{
			const EWeaponClass WC = (EWeaponClass)i;
			List += FString::Printf(TEXT("  %s Lv%d   (+%d%% atk / -%d%% dmg taken)\n"),
				FWeaponMastery::Name(WC), FWeaponMastery::Level(S, WC),
				FMath::RoundToInt((FWeaponMastery::AttackMultiplier(S, WC) - 1.f) * 100.f),
				FWeaponMastery::DefenseReducePercent(S, WC));
		}
		List += TEXT("\n[up/down] select equip gear   [Esc] close");
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 12)).Text(FText::FromString(List)) ];
	}

	// ---------------- weapons (trainer melee) ----------------
	if (PanelId == TEXT("weapons"))
	{
		FString List = FString::Printf(TEXT("Equipped: %s\n\n"), *FPlayerCombat::NameOf(S.PlayerWeapon));
		static const TCHAR* Weapons[] = { TEXT("wpn_club"), TEXT("wpn_blade"), TEXT("wpn_spear"), TEXT("wpn_axe") };
		for (int32 i = 0; i < 4; ++i)
		{
			const FItemDef* D = FEmberwildDB::Item(Weapons[i]);
			const bool bOwned = S.CountItem(Weapons[i]) > 0;
			List += FString::Printf(TEXT("%s%-14s %-24s %s\n"),
				S.PlayerWeapon == Weapons[i] ? TEXT("> ") : TEXT("   "),
				D ? *D->Name : *FString(Weapons[i]),
				*FString::Printf(TEXT("%d dmg"), FPlayerCombat::BaseDamage(Weapons[i])),
				bOwned ? TEXT("owned") : TEXT("not owned"));
		}
		List += TEXT("\n[up/down] select   [E] equip   [Esc] close");
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 12)).Text(FText::FromString(List)) ];
	}

	// ---------------- journal ----------------
	if (PanelId == TEXT("journal"))
	{
		FString List = TEXT("QUESTS\n");
		for (const FString& Qid : S.ActiveQuests)
			if (const FQuestDef* Q = FEmberwildDB::Quest(Qid))
			{
				List += FString::Printf(TEXT("  %s (Act %s)\n    %s\n"), *Q->Name, *Q->Act, *Q->Summary);
				for (const FString& Obj : Q->Objectives)
					List += FString::Printf(TEXT("    %s %s\n"),
						FQuestSystem::IsObjectiveMet(S, Obj) ? TEXT("[x]") : TEXT("[ ]"),
						*FQuestSystem::ObjectiveText(S, Obj));
			}
		List += FString::Printf(TEXT("\nBESTIARY  %d seen / %d caught\n"), S.SeenSpecies.Num(), S.CaughtSpecies.Num());
		for (const FString& Sp : FEmberwildDB::SpeciesOrder)
			if (const FSpeciesDef* D = FEmberwildDB::SpeciesDef(Sp))
			{
				if (S.SeenSpecies.Contains(Sp))
					List += FString::Printf(TEXT("  %s - %s%s\n"), *D->Name, *D->Desc,
						S.CaughtSpecies.Contains(Sp) ? TEXT("  [caught]") : TEXT(""));
				else
					List += FString::Printf(TEXT("  ??? (unseen)\n"));
			}
		List += TEXT("\n[Esc] close");
		return SNew(SBorder).BorderBackgroundColor(FUITheme::Bg()).Padding(16)
		[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 12)).Text(FText::FromString(List)) ];
	}

	return SNew(STextBlock).Text(FText::GetEmpty());
}

// ---------------------------------------------------------------- tick
void SEmberwildUIRouter::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	const float Dt = InDeltaTime;
	// drain toasts
	for (int32 i = Toasts.Num() - 1; i >= 0; --i)
	{
		Toasts[i].Remaining -= Dt;
		if (Toasts[i].Remaining <= 0) Toasts.RemoveAt(i);
	}
	// collect new ones
	FString Msg;
	while (FEmberwildToast::Drain(Msg))
		Toasts.Add({ Msg, 3.5f });

	if (!Sub.IsValid()) return;

	// pull any new battle log lines (engine broadcasts are polled here)
	if (Sub->Battle.IsValid() && Sub->Battle->OnLog.IsBound() == false)
	{
		// first tick with an engine: attach log forwarding
		Sub->Battle->OnLog.AddLambda([this](const FString& Msg)
		{
			BattleLog.Add(Msg);
			while (BattleLog.Num() > 40) BattleLog.RemoveAt(0);
		});
	}

	// slots spin animation
	if (OpenPanel == TEXT("slots") && bSpinning)
	{
		SpinTimer -= Dt;
		if (SpinTimer <= 0.f)
		{
			bSpinning = false;
			const FSlotsMachine::FSpinResult R = FSlotsMachine::Resolve(Sub->Save, WinStreak);
			for (int32 i = 0; i < 3; ++i) FinalSymbols[i] = R.Symbols[i];
			SlotsResult = R.Headline;
			if (auto* Sfx = EmberwildSfx::Get()) Sfx->PlayNamed(R.bJackpot ? TEXT("evolve") : R.bWin ? TEXT("levelup") : TEXT("hit"));
		}
	}

	// rebuild the active panel when the id changes; list-style panels
	// (inventory/recipes/stock) rebuild every frame to stay current
	const bool bListPanel = OpenPanel == TEXT("crafting") || OpenPanel == TEXT("shop")
		|| OpenPanel == TEXT("bag") || OpenPanel == TEXT("party")
		|| OpenPanel == TEXT("weapons") || OpenPanel == TEXT("gear") || OpenPanel == TEXT("journal");
	if (CurrentPanelBox.IsValid() && (LastBuiltPanel != OpenPanel || bListPanel))
	{
		LastBuiltPanel = OpenPanel;
		CurrentPanelBox->SetContent(OpenPanel.IsEmpty()
			? SNullWidget::NullWidget
			: BuildPanelContent(OpenPanel));
	}
}

// ---------------------------------------------------------------- input
FReply SEmberwildUIRouter::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (HandlePanelKey(Key)) return FReply::Handled();
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

bool SEmberwildUIRouter::HandlePanelKey(const FKey& Key)
{
	if (!Sub.IsValid()) return false;

	// the battle panel consumes everything (including Esc) while active
	if (OpenPanel == TEXT("battle")) return HandleBattleKey(Key);

	// global keys
	if (Key == EKeys::F5)
	{
		FEmberwildSave::Save(Sub->Save);
		FEmberwildToast::Push(TEXT("Saved."));
		return true;
	}
	if (Key == EKeys::Escape)
	{
		CloseTop();
		return true;
	}

	if (bTitleActive)
	{
		if (Key == EKeys::E || Key == EKeys::Enter)
		{
			bTitleActive = false;
			OpenPanel = TEXT("");
			return true;
		}
		return false;
	}

	if (OpenPanel == TEXT("dialogue"))return HandleDialogueKey(Key);
	if (OpenPanel == TEXT("slots"))   return HandleSlotsKey(Key);
	if (OpenPanel == TEXT("crafting") || OpenPanel == TEXT("shop") || OpenPanel == TEXT("bag")
		|| OpenPanel == TEXT("party") || OpenPanel == TEXT("weapons") || OpenPanel == TEXT("gear"))
		return HandleListKey(Key, OpenPanel);
	// journal: display only
	if (OpenPanel == TEXT("journal")) return Key == EKeys::Escape;

	return false;
}

bool SEmberwildUIRouter::HandleDialogueKey(const FKey& Key)
{
	if (!Sub.IsValid() || !Sub->Dialogue) return false;

	if (bChoicePending)
	{
		if (Key == EKeys::Y) { bChoicePending = false; Sub->Dialogue->Choose(true);  return true; }
		if (Key == EKeys::N) { bChoicePending = false; Sub->Dialogue->Choose(false); return true; }
		return false;
	}
	if (Key == EKeys::E || Key == EKeys::Enter || Key == EKeys::SpaceBar)
	{
		Sub->Dialogue->Advance();
		return true;
	}
	return false;
}

bool SEmberwildUIRouter::HandleSlotsKey(const FKey& Key)
{
	if (!Sub.IsValid()) return false;
	if (Key == EKeys::E || Key == EKeys::Enter || Key == EKeys::SpaceBar)
	{
		if (!bSpinning) DoSpin();
		return true;
	}
	return false;
}

bool SEmberwildUIRouter::HandleBattleKey(const FKey& Key)
{
	if (!Sub.IsValid() || !Sub->Battle) return true;

	const bool bUp = Key == EKeys::Up;
	const bool bDown = Key == EKeys::Down;
	const bool bConfirm = Key == EKeys::E || Key == EKeys::Enter || Key == EKeys::SpaceBar;
	const bool bBack = Key == EKeys::BackSpace || Key == EKeys::Escape;

	if (bBattleOver)
	{
		if (bConfirm || bBack)
		{
			const EBattleResult R = Sub->Battle->Result;
			Sub->FinishBattle(R);
			OpenPanel = TEXT("");
			bBattleOver = false;
			if (R == EBattleResult::PlayerLost)
				Sub->NotifyPlayerDefeat();
		}
		return true;
	}

	if (bUp)   { BattleCursor = (BattleCursor + BattleMenuItems.Num() - 1) % FMath::Max(1, BattleMenuItems.Num()); return true; }
	if (bDown) { BattleCursor = (BattleCursor + 1) % FMath::Max(1, BattleMenuItems.Num()); return true; }
	if (bBack && BattleMode != TEXT("menu")) { BuildBattleMenu(); return true; }
	if (bConfirm && BattleMenuItems.IsValidIndex(BattleCursor)) { DoBattleMenuItem(BattleCursor); return true; }
	return false;
}

bool SEmberwildUIRouter::HandleListKey(const FKey& Key, const FString& PanelId)
{
	if (!Sub.IsValid()) return false;
	FGameStateSave& S = Sub->Save;

	const bool bUp = Key == EKeys::Up;
	const bool bDown = Key == EKeys::Down;
	const bool bConfirm = Key == EKeys::E || Key == EKeys::Enter;

	if (PanelId == TEXT("crafting"))
	{
		// count recipes visible at this station
		TArray<const FRecipe*> Visible;		for (const FRecipe& R : FEmberwildDB::Recipes)
		{
			// parity (CraftingPanel.cs:82): same filter the panel builder uses
			if (!R.Station.IsEmpty() && R.Station != CraftStation)
				continue;
			Visible.Add(&R);
		}

		if (bUp)   { CraftCursor = (CraftCursor + Visible.Num() - 1) % FMath::Max(1, Visible.Num()); return true; }
		if (bDown) { CraftCursor = (CraftCursor + 1) % FMath::Max(1, Visible.Num()); return true; }
		if (bConfirm && Visible.IsValidIndex(CraftCursor))
		{
			DoCraft(*Visible[CraftCursor]);
			return true;
		}
		return false;
	}

	if (PanelId == TEXT("shop"))
	{
		const FShopDef* ShopDef = FEmberwildDB::Shop(ShopId);
		const int32 N = ShopDef ? ShopDef->Stock.Num() : 0;
		if (bUp)   { ShopCursor = (ShopCursor + N - 1) % FMath::Max(1, N); return true; }
		if (bDown) { ShopCursor = (ShopCursor + 1) % FMath::Max(1, N); return true; }
		if (bConfirm && ShopDef && ShopDef->Stock.IsValidIndex(ShopCursor))
		{
			DoBuy(ShopCursor);
			return true;
		}
		if (Key == EKeys::B)
		{
			// sell the selected stock item type from inventory
			if (ShopDef && ShopDef->Stock.IsValidIndex(ShopCursor))
			{
				TArray<FString> Parts;
				ShopDef->Stock[ShopCursor].ParseIntoArray(Parts, TEXT(":"));
				DoSell(Parts[0]);
			}
			return true;
		}
		return false;
	}

	if (PanelId == TEXT("bag"))
	{
		const int32 N = S.Inventory.Num();
		if (bUp)   { ListCursor = (ListCursor + N - 1) % FMath::Max(1, N); return true; }
		if (bDown) { ListCursor = (ListCursor + 1) % FMath::Max(1, N); return true; }
		if (bConfirm && S.Inventory.IsValidIndex(ListCursor))
		{
			DoUseItem(ListCursor);
			return true;
		}
		return false;
	}

	if (PanelId == TEXT("party"))
	{
		const int32 N = S.Party.Num();
		if (bUp)   { ListCursor = (ListCursor + N - 1) % FMath::Max(1, N); return true; }
		if (bDown) { ListCursor = (ListCursor + 1) % FMath::Max(1, N); return true; }
		if (Key == EKeys::W) { EquipCreatureGear(ListCursor, TEXT("weapon"), TEXT("")); return true; }
		if (Key == EKeys::A) { EquipCreatureGear(ListCursor, TEXT("armor"), TEXT("")); return true; }
		if (Key == EKeys::X) { ReleaseCreature(ListCursor); return true; }
		return false;
	}

	if (PanelId == TEXT("weapons"))
	{
		// weapons panel cycles all 4 entries with up/down
	if (bUp || bDown) { ListCursor = (ListCursor + 1) % 4; return true; }
		if (bConfirm)
		{
			static const TCHAR* Weapons[] = { TEXT("wpn_club"), TEXT("wpn_blade"), TEXT("wpn_spear"), TEXT("wpn_axe") };
			EquipTrainerItem(Weapons[ListCursor % 4]);
			return true;
		}
		return false;
	}

	return false;
}

// ---------------------------------------------------------------- actions
void SEmberwildUIRouter::BuildBattleMenu()
{
	BattleMode = TEXT("menu");
	BattleCursor = 0;
	BattleMenuItems = { TEXT("FIGHT"), TEXT("BAG"), TEXT("SWITCH"), TEXT("RUN") };
	BattleMoveIds.Reset();
}

void SEmberwildUIRouter::DoBattleMenuItem(int32 Idx)
{
	if (!Sub.IsValid() || !Sub->Battle) return;

	if (BattleMode == TEXT("menu"))
	{
		switch (Idx)
		{
		case 0: // FIGHT -> show moves + techniques
		{
			BattleMode = TEXT("moves");
			BattleCursor = 0;
			BattleMenuItems.Reset();
			BattleMoveIds.Reset();
			if (const FCreature* P = Sub->Battle->PlayerMon())
			{
				for (const FString& MV : P->Moves)
				{
					BattleMoveIds.Add(MV);
					BattleMenuItems.Add(FString::Printf(TEXT("%s  (%d PP)"), *FEmberwildDB::Move(MV)->Name, P->CurrentPP(MV)));
				}
				// mastery techniques for the equipped gear
				for (const FWeaponMastery::FTechnique& T : FWeaponMastery::Techniques())
					if (FWeaponMastery::IsTechniqueAvailable(Sub->Save, *P, T.MoveId))
					{
						BattleMoveIds.Add(T.MoveId);
						BattleMenuItems.Add(FString::Printf(TEXT("* %s  (%d tech PP)"),
							*FEmberwildDB::Move(T.MoveId)->Name,
							FWeaponMastery::TechPP(Sub->Save, FWeaponMastery::ClassOfTechnique(T.MoveId), T.MoveId)));
					}
			}
			break;
		}
		case 1: // BAG
			BattleMode = TEXT("bag");
			BattleCursor = 0;
			BattleMenuItems.Reset();
			for (const FItemStack& St : Sub->Save.Inventory)
				BattleMenuItems.Add(FString::Printf(TEXT("%s x%d"), *FEmberwildDB::ItemName(St.ItemId), St.Count));
			if (BattleMenuItems.Num() == 0) BattleMenuItems.Add(TEXT("(bag empty)"));
			break;
		case 2: // SWITCH
			BattleMode = TEXT("switch");
			BattleCursor = 0;
			BattleMenuItems.Reset();
			for (const FCreature& C : Sub->Save.Party)
				BattleMenuItems.Add(FString::Printf(TEXT("%s Lv%d %d/%d HP"),
					*C.DisplayName(), C.Level, C.CurHP, C.MaxHP()));
			break;
		case 3: // RUN
			if (Sub->Battle->TryFlee())
				bBattleOver = true;
			else
				Sub->Battle->EnemyOnlyTurn();
			break;
		}
		return;
	}

	if (BattleMode == TEXT("moves"))
	{
		if (BattleMoveIds.IsValidIndex(Idx))
		{
			DoPlayerMove(BattleMoveIds[Idx]);
		}
		return;
	}

	if (BattleMode == TEXT("bag"))
	{
		if (Sub->Save.Inventory.IsValidIndex(Idx))
			DoUseItem(Idx);
		return;
	}

	if (BattleMode == TEXT("switch"))
	{
		if (Sub->Save.Party.IsValidIndex(Idx))
			DoSwitchTo(Idx);
		return;
	}
}

void SEmberwildUIRouter::DoPlayerMove(const FString& MoveId)
{
	if (!Sub.IsValid() || !Sub->Battle) return;

	Sub->Battle->PlayerTurn(MoveId);
	BuildBattleMenu();

	if (Sub->Battle->Result != EBattleResult::Ongoing)
		bBattleOver = true;
}

void SEmberwildUIRouter::DoUseItem(int32 InvIdx)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;
	if (!S.Inventory.IsValidIndex(InvIdx)) return;

	const FString ItemId = S.Inventory[InvIdx].ItemId;
	const FItemDef* D = FEmberwildDB::Item(ItemId);
	if (!D) return;

	// in battle: cores are throws, heals apply to the active mon
	if (OpenPanel == TEXT("battle") && Sub->Battle)
	{
		if (D->Kind == EItemKind::CaptureCore)
		{
			if (Sub->Battle->TryCatch(ItemId))
			{
				S.RemoveItem(ItemId, 1);
				bBattleOver = true;
			}
			else
			{
				S.RemoveItem(ItemId, 1);
				Sub->Battle->EnemyOnlyTurn();
			}
			return;
		}
		if (D->HealAmount > 0 || D->ConditionCure > 0)
		{
			if (FCreature* P = Sub->Battle->PlayerMon())
			{
				if (D->HealAmount == 999) // revive
				{
					P->CurHP = P->MaxHP() / 2;
				}
				else
				{
					P->CurHP = FMath::Min(P->MaxHP(), P->CurHP + D->HealAmount);
				}
				if (D->ConditionCure > 0) { P->Status = TEXT(""); P->StatusTurns = 0; }
				S.RemoveItem(ItemId, 1);
				BattleLog.Add(FString::Printf(TEXT("Used %s."), *D->Name));
				Sub->Battle->EnemyOnlyTurn();
				BuildBattleMenu();
			}
			return;
		}
		FEmberwildToast::Push(TEXT("Not usable right now."));
		return;
	}

	// overworld use
	if (D->Kind == EItemKind::Equipment)
	{
		EquipTrainerItem(ItemId);
		return;
	}
	if (D->HealAmount > 0 || D->ConditionCure > 0)
	{
		// heal first ailing party member
		for (FCreature& C : S.Party)
		{
			const bool bNeedsHeal = D->HealAmount == 999 ? C.CurHP <= 0 : C.CurHP < C.MaxHP();
			if (bNeedsHeal || (D->ConditionCure > 0 && !C.Status.IsEmpty()))
			{
				if (D->HealAmount == 999) C.CurHP = C.MaxHP() / 2;
				else C.CurHP = FMath::Min(C.MaxHP(), C.CurHP + D->HealAmount);
				if (D->ConditionCure > 0) { C.Status = TEXT(""); C.StatusTurns = 0; }
				S.RemoveItem(ItemId, 1);
				FEmberwildToast::Push(FString::Printf(TEXT("Used %s on %s."), *D->Name, *C.DisplayName()));
				return;
			}
		}
		FEmberwildToast::Push(TEXT("Nobody needs that right now."));
	}
}

void SEmberwildUIRouter::DoSwitchTo(int32 PartyIdx)
{
	if (!Sub.IsValid() || !Sub->Battle) return;
	FGameStateSave& S = Sub->Save;

	// find that creature's index inside the engine's team
	if (!S.Party.IsValidIndex(PartyIdx)) return;
	const FString Uid = S.Party[PartyIdx].Uid;
	for (int32 i = 0; i < Sub->Battle->PlayerTeam.Num(); ++i)
	{
		if (Sub->Battle->PlayerTeam[i].Uid == Uid && Sub->Battle->PlayerTeam[i].CurHP > 0)
		{
			Sub->Battle->ActivePlayerIdx = i;
			BattleLog.Add(FString::Printf(TEXT("Go, %s!"), *Sub->Battle->PlayerTeam[i].DisplayName()));
			Sub->Battle->EnemyOnlyTurn();
			BuildBattleMenu();
			return;
		}
	}
	FEmberwildToast::Push(TEXT("That creature can't battle."));
}

void SEmberwildUIRouter::DoBuy(int32 StockIdx)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;
	const FShopDef* ShopDef = FEmberwildDB::Shop(ShopId);
	if (!ShopDef || !ShopDef->Stock.IsValidIndex(StockIdx)) return;

	TArray<FString> Parts;
	ShopDef->Stock[StockIdx].ParseIntoArray(Parts, TEXT(":"));
	const FItemDef* D = FEmberwildDB::Item(Parts[0]);
	if (!D) return;

	if (S.Coins < D->BasePrice) { FEmberwildToast::Push(TEXT("Not enough coins.")); return; }
	S.Coins -= D->BasePrice;
	S.AddItem(D->Id, 1);
	FEmberwildToast::Push(FString::Printf(TEXT("Bought %s (-%dc)."), *D->Name, D->BasePrice));
}

void SEmberwildUIRouter::DoSell(const FString& ItemId)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;
	const FItemDef* D = FEmberwildDB::Item(ItemId);
	if (!D || !D->bSellable) { FEmberwildToast::Push(TEXT("Can't sell that.")); return; }
	if (S.CountItem(ItemId) <= 0) { FEmberwildToast::Push(TEXT("You have none.")); return; }

	const int32 Price = FMath::Max(1, D->BasePrice / 2);
	S.RemoveItem(ItemId, 1);
	S.Coins += Price;
	FEmberwildToast::Push(FString::Printf(TEXT("Sold %s (+%dc)."), *D->Name, Price));
}

void SEmberwildUIRouter::DoCraft(const FRecipe& R)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;

	if (!FProfessionSystem::CanCraft(S, R, CraftStation))
	{
		FEmberwildToast::Push(FProfessionSystem::CraftBlocker(S, R, CraftStation));
		return;
	}
	for (const FItemStack& In : R.Inputs) S.RemoveItem(In.ItemId, In.Count);
	S.AddItem(R.ResultItemId, R.ResultCount);
	FProfessionSystem::AddXp(S, R.Profession, R.XpReward);
	S.SetFlag(TEXT("craft_") + R.ResultItemId);
	FEmberwildToast::Push(FString::Printf(TEXT("Crafted %dx %s  (+%d xp)"),
		R.ResultCount, *FEmberwildDB::ItemName(R.ResultItemId), R.XpReward));
	if (auto* Sfx = EmberwildSfx::Get()) Sfx->PlayNamed(TEXT("craft"));
	FQuestSystem::AutoCompleteAll(S);
}

void SEmberwildUIRouter::DoSpin()
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;

	if (S.Coins < FSlotsMachine::SpinCost)
	{
		FEmberwildToast::Push(TEXT("Not enough coins - win some at the Vale Post."));
		return;
	}
	S.Coins -= FSlotsMachine::SpinCost;
	bSpinning = true;
	SpinTimer = 1.6f;
	SlotsResult = TEXT("...");
	if (auto* Sfx = EmberwildSfx::Get()) Sfx->PlayNamed(TEXT("coin"));
}

void SEmberwildUIRouter::EquipTrainerItem(const FString& ItemId)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;
	const FItemDef* D = FEmberwildDB::Item(ItemId);
	if (!D) return;

	if (D->Kind == EItemKind::Equipment && D->PlayerWeaponKind != EPlayerWeaponKind::None)
	{
		if (S.CountItem(ItemId) <= 0) { FEmberwildToast::Push(TEXT("You don't own that.")); return; }
		S.PlayerWeapon = ItemId;
		FEmberwildToast::Push(FString::Printf(TEXT("Equipped %s."), *D->Name));
	}
	else if (D->Kind == EItemKind::Equipment)
	{
		if (S.CountItem(ItemId) <= 0) { FEmberwildToast::Push(TEXT("You don't own that.")); return; }
		S.EquippedItem = (S.EquippedItem == ItemId) ? TEXT("") : ItemId;
		FEmberwildToast::Push(S.EquippedItem.IsEmpty()
			? FString::Printf(TEXT("Unequipped %s."), *D->Name)
			: FString::Printf(TEXT("Equipped %s."), *D->Name));
	}
}

void SEmberwildUIRouter::EquipCreatureGear(int32 PartyIdx, const FString& Slot, const FString& ItemId)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;
	if (!S.Party.IsValidIndex(PartyIdx)) return;

	// cycle through owned gear of the right class
	FCreature& C = S.Party[PartyIdx];
	const FString Current = Slot == TEXT("weapon") ? C.Weapon : C.Armor;

	TArray<FString> Pool;
	static const TCHAR* CreatureWeapons[] = { TEXT("gear_claw"), TEXT("gear_fang"), TEXT("gear_stormspine") };
	static const TCHAR* CreatureArmors[] = { TEXT("gear_band"), TEXT("gear_hide"), TEXT("gear_charm"), TEXT("gear_plate") };
	const bool bWeaponSlot = (Slot == TEXT("weapon"));
	const int32 PoolSize = bWeaponSlot ? 3 : 4;
	for (int32 Gi = 0; Gi < PoolSize; ++Gi)
	{
		const TCHAR* W = bWeaponSlot ? CreatureWeapons[Gi] : CreatureArmors[Gi];
		if (S.CountItem(W) > 0) Pool.Add(W);
	}

	if (Pool.Num() == 0) { FEmberwildToast::Push(TEXT("No owned gear for that slot.")); return; }

	int32 NewIdx = 0;
	for (int32 i = 0; i < Pool.Num(); ++i)
		if (Pool[i] == Current) { NewIdx = (i + 1) % (Pool.Num() + 1); break; }

	const FString Next = NewIdx < Pool.Num() ? Pool[NewIdx] : FString();
	if (Slot == TEXT("weapon")) C.Weapon = Next; else C.Armor = Next;
	FEmberwildToast::Push(Next.IsEmpty()
		? FString::Printf(TEXT("%s: %s unequipped."), *C.DisplayName(), *Slot)
		: FString::Printf(TEXT("%s: %s -> %s."), *C.DisplayName(), *Slot, *FEmberwildDB::ItemName(Next)));
}

void SEmberwildUIRouter::ReleaseCreature(int32 PartyIdx)
{
	if (!Sub.IsValid()) return;
	FGameStateSave& S = Sub->Save;
	if (!S.Party.IsValidIndex(PartyIdx)) return;
	if (S.Party.Num() <= 1) { FEmberwildToast::Push(TEXT("Your last friend stays.")); return; }

	FEmberwildToast::Push(FString::Printf(TEXT("%s bounds off to the meadow. Bye, friend!"),
		*S.Party[PartyIdx].DisplayName()));
	S.Party.RemoveAt(PartyIdx);
	ListCursor = 0;
}

// ---------------------------------------------------------------- routing impl
void SEmberwildUIRouter::OpenDialogue() { OpenPanel = TEXT("dialogue"); }
void SEmberwildUIRouter::CloseDialogue() { OpenPanel = TEXT(""); }
void SEmberwildUIRouter::OpenBattle()
{
	OpenPanel = TEXT("battle");
	BattleLog.Reset();
	bBattleOver = false;
	BuildBattleMenu();
}
void SEmberwildUIRouter::OpenCrafting(const FString& StationId)
{
	OpenPanel = TEXT("crafting");
	CraftStation = StationId;
	CraftCursor = 0;
}
void SEmberwildUIRouter::OpenShop(const FString& InShopId)
{
	OpenPanel = TEXT("shop");
	ShopId = InShopId;
	ShopCursor = 0;
}
void SEmberwildUIRouter::OpenSlots()
{
	OpenPanel = TEXT("slots");
	SlotsResult = TEXT("");
	FinalSymbols[0] = FinalSymbols[1] = FinalSymbols[2] = 0;
}
void SEmberwildUIRouter::TogglePanel(const FString& PanelId)
{
	OpenPanel = (OpenPanel == PanelId) ? TEXT("") : PanelId;
	ListCursor = 0;
}
void SEmberwildUIRouter::CloseTop()
{
	OpenPanel = TEXT("");
}

// ---------------------------------------------------------------- routing impl
void SEmberwildUIRouter::ShowDialogueLine(const FDialogueLine& Line)
{
	DlgSpeaker = Line.Speaker;
	DlgText = Line.Text;
	bChoicePending = Line.EndRule == TEXT("choice");
}

void SEmberwildUIRouter::ProcessDefeat()
{
	if (Sub.IsValid()) Sub->NotifyPlayerDefeat();
}
