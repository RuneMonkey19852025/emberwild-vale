// Emberwild Vale — the authored narrative of Emberwild Vale.
// Faithful port of the Unity original's StoryData.cs: acts, quests, dialogue,
// trainers and shops as one readable data table.
//
// STORY OUTLINE (3 acts):
//   Act 1 - "Smoke and Small Debts": arrive, learn a profession, craft a
//           first capture core, catch a first friend, beat Tobin.
//   Act 2 - "The Sigil of Root, Tide and Ember": the ridge opens; Korrin
//           holds a sigil fragment; Sela sends you for all three.
//   Act 3 - "The Hollow Sleepers": Zephy guards the descent; wake the
//           Sleeper with the assembled Verdant Sigil.

#include "EmberwildDB.h"

// ---------------------------------------------------------------- builders
static FNpcDef& MakeNpc(const TCHAR* Id, const TCHAR* Name, const TCHAR* Role, const TCHAR* ColorHex,
                    const TCHAR* Zone, std::initializer_list<const TCHAR*> Nodes)
{
	FNpcDef D;
	D.Id = Id; D.Name = Name; D.Role = Role; D.ColorHex = ColorHex; D.Zone = Zone;
	for (const TCHAR* N : Nodes) D.DialogueNodes.Add(N);
	return FEmberwildDB::Npcs.Add(D.Id, MoveTemp(D));
}

static FShopDef& MakeShop(const TCHAR* Id, const TCHAR* Name, std::initializer_list<const TCHAR*> Stock)
{
	FShopDef D;
	D.Id = Id; D.Name = Name;
	for (const TCHAR* S : Stock) D.Stock.Add(S);
	return FEmberwildDB::Shops.Add(D.Id, MoveTemp(D));
}

static FQuestDef& MakeQuest(const TCHAR* Id, const TCHAR* Name, const TCHAR* Summary, const TCHAR* Giver,
                        const TCHAR* Act, int32 RewardCoins,
                        std::initializer_list<const TCHAR*> Prereqs,
                        std::initializer_list<const TCHAR*> Objectives,
                        std::initializer_list<const TCHAR*> OnCompleteFlags = {},
                        std::initializer_list<const TCHAR*> OnCompleteGiveItems = {})
{
	FQuestDef D;
	D.Id = Id; D.Name = Name; D.Summary = Summary; D.GiverNpcId = Giver; D.Act = Act;
	D.RewardCoins = RewardCoins;
	for (const TCHAR* P : Prereqs) D.PrereqQuests.Add(P);
	for (const TCHAR* O : Objectives) D.Objectives.Add(O);
	for (const TCHAR* F : OnCompleteFlags) D.OnCompleteFlags.Add(F);
	for (const TCHAR* G : OnCompleteGiveItems) D.OnCompleteGiveItems.Add(G);
	return FEmberwildDB::Quests.Add(D.Id, MoveTemp(D));
}

struct FLineBuilder : public FDialogueLine
{
	FLineBuilder(const TCHAR* Speaker, const TCHAR* Text) { this->Speaker = Speaker; this->Text = Text; }
	FLineBuilder& Terminal(const TCHAR* Rule = TEXT("end")) { EndRule = Rule; return *this; }
	FLineBuilder& ChoiceBranch(const TCHAR* Yes, const TCHAR* No) { EndRule = TEXT("choice"); YesNode = Yes; NoNode = No; return *this; }
	FLineBuilder& OffersQuest(const TCHAR* QId) { QuestId = QId; return *this; }
	FLineBuilder& SetsFlag(const TCHAR* F) { SetFlag = F; return *this; }
	FLineBuilder& Gives(const TCHAR* G) { GiveItem = G; return *this; }
	FLineBuilder& Teaches(const TCHAR* T) { this->Teach = T; return *this; }
	FLineBuilder& SpawnsCreature(const TCHAR* S) { SpawnCreature = S; return *this; }
	FLineBuilder& SpawnsTrainer(const TCHAR* T) { SpawnTrainer = T; return *this; }
	FLineBuilder& Heal() { bHealParty = true; return *this; }
	FLineBuilder& OpensShop(const TCHAR* S) { this->OpenShop = S; return *this; }
	FLineBuilder& OpensCraft(const TCHAR* S) { this->OpenCraft = S; return *this; }
	FLineBuilder& Advance() { EndRule = TEXT(""); return *this; }
};

static void MakeNode(const TCHAR* Id, std::initializer_list<FLineBuilder> Lines)
{
	FDialogueNode D;
	D.Id = Id;
	for (const FLineBuilder& L : Lines) D.Lines.Add(L);
	FEmberwildDB::Dialogue.Add(D.Id, MoveTemp(D));
}

static FTrainerDef& MakeTrainer(const TCHAR* Id, const TCHAR* Name, const TCHAR* Title, int32 RewardCoins,
                            std::initializer_list<const TCHAR*> Team,
                            std::initializer_list<const TCHAR*> WinLines,
                            std::initializer_list<const TCHAR*> LoseLines,
                            std::initializer_list<const TCHAR*> OnDefeatFlags = {},
                            std::initializer_list<const TCHAR*> OnDefeatGiveItems = {})
{
	FTrainerDef D;
	D.Id = Id; D.Name = Name; D.Title = Title; D.RewardCoins = RewardCoins;
	for (const TCHAR* T : Team) D.Team.Add(T);
	for (const TCHAR* W : WinLines) D.WinLines.Add(W);
	for (const TCHAR* L : LoseLines) D.LoseLines.Add(L);
	for (const TCHAR* F : OnDefeatFlags) D.OnDefeatFlags.Add(F);
	for (const TCHAR* G : OnDefeatGiveItems) D.OnDefeatGiveItems.Add(G);
	return FEmberwildDB::Trainers.Add(D.Id, MoveTemp(D));
}

// ---------------------------------------------------------------- data
void FEmberwildDB::InitStory()
{
	Quests.Empty();
	Dialogue.Empty();
	Trainers.Empty();
	Shops.Empty();
	Npcs.Empty();

	// ================= NPCs =================
	MakeNpc(TEXT("maren"),   TEXT("Maren"),      TEXT("Rancher"),          TEXT("#D9884A"), TEXT("meadow"), { TEXT("maren_idle"), TEXT("maren_yes"), TEXT("maren_no") });
	MakeNpc(TEXT("pell"),    TEXT("Old Pell"),   TEXT("Ferryman"),         TEXT("#7A9E7E"), TEXT("meadow"), { TEXT("pell_intro"), TEXT("pell_loop") });
	MakeNpc(TEXT("mira"),    TEXT("Mira"),       TEXT("Well Tender"),      TEXT("#8FD0A8"), TEXT("meadow"), { TEXT("mira_intro"), TEXT("mira_loop") });
	MakeNpc(TEXT("juna"),    TEXT("Juna"),       TEXT("Professions Warden"),TEXT("#C9A0DC"),TEXT("meadow"), { TEXT("juna_intro"), TEXT("juna_loop") });
	MakeNpc(TEXT("brann"),   TEXT("Brann"),      TEXT("Smith"),            TEXT("#B0653A"), TEXT("meadow"), { TEXT("brann_intro"), TEXT("brann_accept"), TEXT("brann_decline"), TEXT("brann_loop") });
	MakeNpc(TEXT("odessa"),  TEXT("Odessa"),     TEXT("Ranger Captain"),   TEXT("#5A7FBF"), TEXT("meadow"), { TEXT("odessa_intro"), TEXT("odessa_yes"), TEXT("odessa_no"), TEXT("odessa_loop") });
	MakeNpc(TEXT("tobin"),   TEXT("Tobin"),      TEXT("Young Trainer"),    TEXT("#E0C060"), TEXT("meadow"), { TEXT("tobin_fight"), TEXT("tobin_loop") });
	MakeNpc(TEXT("sela"),    TEXT("Sela"),       TEXT("Herbalist"),        TEXT("#7FBF9E"), TEXT("ridge"),  { TEXT("sela_intro"), TEXT("sela_yes"), TEXT("sela_no"), TEXT("sela_loop") });
	MakeNpc(TEXT("korrin"),  TEXT("Korrin"),     TEXT("Prospector"),       TEXT("#8A6F5A"), TEXT("ridge"),  { TEXT("korrin_fight"), TEXT("korrin_loop") });
	MakeNpc(TEXT("vesna"),   TEXT("Vesna"),      TEXT("Angler"),           TEXT("#6FA8B8"), TEXT("lake"),   { TEXT("vesna_intro"), TEXT("vesna_loop") });
	MakeNpc(TEXT("hollis"),  TEXT("Hollis"),     TEXT("Wandering Bard"),   TEXT("#B87FB8"), TEXT("lake"),   { TEXT("hollis_fight"), TEXT("hollis_loop") });
	MakeNpc(TEXT("zephy"),   TEXT("Zephy"),      TEXT("Hollow Herald"),    TEXT("#8878B8"), TEXT("hollow"), { TEXT("zephy_fight"), TEXT("zephy_loop") });
	MakeNpc(TEXT("sleepers"),TEXT("The Sleeper"),TEXT("Hollow Dreamer"),   TEXT("#9AA8D8"), TEXT("hollow_deep"), { TEXT("sleeper_final"), TEXT("sleeper_done") });

	// ================= SHOPS =================
	MakeShop(TEXT("general"), TEXT("Vale Post Traders"), {
		TEXT("core_wood:6"), TEXT("core_iron:3"), TEXT("trail_snack:8"), TEXT("salve:6"), TEXT("purge_draught:4"),
		TEXT("rope:5"), TEXT("fiber:-1"), TEXT("bitterleaf:-1"), TEXT("stone:-1"),
		TEXT("gear_claw:2"), TEXT("gear_band:2"), TEXT("gear_fang:1"), TEXT("gear_hide:1") });

	// ================= QUESTS =================
	MakeQuest(TEXT("q_firstcore"), TEXT("First Core"),
		TEXT("Brann: gather fiber and stone, braid rope, and craft a Timber Core at his forge."),
		TEXT("brann"), TEXT("1"), 60,
		{}, { TEXT("craft core_wood") },
		{ TEXT("made_first_core") }, { TEXT("trail_snack:2") });

	MakeQuest(TEXT("q_firstcatch"), TEXT("First Friend"),
		TEXT("Maren: weaken a wild creature and catch it with a capture core."),
		TEXT("maren"), TEXT("1"), 40,
		{ TEXT("q_firstcore") }, { TEXT("catch_any") },
		{ TEXT("caught_first") }, { TEXT("core_wood:2") });

	MakeQuest(TEXT("q_tobin"), TEXT("Rival Winds"),
		TEXT("Tobin waits at the ranch gate for a battle. Show him the meadow chose well."),
		TEXT("tobin"), TEXT("1"), 80,
		{ TEXT("q_firstcatch") }, { TEXT("defeat trainer_tobin") },
		{ TEXT("beat_tobin") });

	MakeQuest(TEXT("q_professions"), TEXT("Hands for the Vale"),
		TEXT("Juna: reach level 2 in any three professions and be sworn into the Vale rolls."),
		TEXT("juna"), TEXT("2"), 100,
		{ TEXT("q_firstcore") }, { TEXT("prof3_lv2") },
		{ TEXT("prof_sworn") }, { TEXT("forager_gloves:1") });

	MakeQuest(TEXT("q_emberrock"), TEXT("Smoke over Emberrock"),
		TEXT("Odessa: cross the western bridge and deal with Korrin on Emberrock Ridge."),
		TEXT("odessa"), TEXT("2"), 120,
		{ TEXT("q_tobin") }, { TEXT("defeat trainer_korrin") },
		{ TEXT("ridge_opened") }, { TEXT("miner_pick:1") });

	MakeQuest(TEXT("q_sigil"), TEXT("The Verdant Sigil"),
		TEXT("Sela: recover the three sigil fragments. Root - beat Korrin. Tide - beat Hollis. Ember - rarely surfaces when mining the ridge veins."),
		TEXT("sela"), TEXT("2"), 150,
		{ TEXT("q_emberrock") }, { TEXT("collect sigil_piece_a"), TEXT("collect sigil_piece_b"), TEXT("collect sigil_piece_c") },
		{ TEXT("has_sigil") }, { TEXT("verdant_sigil:1") });

	MakeQuest(TEXT("q_herald"), TEXT("The Hollow Herald"),
		TEXT("Zephy bars the descent into the Hollow. Defeat the herald of the Sleepers."),
		TEXT("zephy"), TEXT("3"), 200,
		{ TEXT("q_sigil") }, { TEXT("defeat trainer_zephy") },
		{ TEXT("herald_down") }, { TEXT("hearty_stew:2"), TEXT("core_moon:1") });

	MakeQuest(TEXT("q_sleeper"), TEXT("The Hollow Sleepers"),
		TEXT("Descend to the Sleeper with the Verdant Sigil and the lantern. Wake what dreams below."),
		TEXT("sleepers"), TEXT("3"), 500,
		{ TEXT("q_herald") }, { TEXT("wake_sleeper") },
		{ TEXT("sleeper_awake") }, { TEXT("revive_tonic:1"), TEXT("core_moon:2") });

	// ================= DIALOGUE =================
	// --- Brann, smith ---
	MakeNode(TEXT("brann_intro"), {
		FLineBuilder(TEXT("Brann"), TEXT("New face. Good. The Vale needs hands that work, not watchers.")).Advance(),
		FLineBuilder(TEXT("Brann"), TEXT("First lesson: everything starts with a capture core. I can teach you the smithing for it. Interested?"))
			.ChoiceBranch(TEXT("brann_accept"), TEXT("brann_decline")) });
	MakeNode(TEXT("brann_accept"), {
		FLineBuilder(TEXT("Brann"), TEXT("Then it's settled. I'll show you the basics of the forge."))
			.Teaches(TEXT("Smith:1")).OffersQuest(TEXT("q_firstcore")).Advance(),
		FLineBuilder(TEXT("Brann"), TEXT("And here's the tailor's trick for braiding rope - fiber and knuckle grease. Gather fiber and stone, braid rope, and craft a Timber Core at my forge."))
			.Teaches(TEXT("Tailor:1")) });
	MakeNode(TEXT("brann_decline"), {
		FLineBuilder(TEXT("Brann"), TEXT("Suit yourself. The forge isn't going anywhere. Neither is the smoke on the ridge.")) });
	MakeNode(TEXT("brann_loop"), {
		FLineBuilder(TEXT("Brann"), TEXT("Forge is lit. Craft away, and mind your fingers.")).OpensCraft(TEXT("forge")) });

	// --- Maren, rancher ---
	MakeNode(TEXT("maren_idle"), {
		FLineBuilder(TEXT("Maren"), TEXT("You've that look. Caught yourself a creature yet?"))
			.ChoiceBranch(TEXT("maren_yes"), TEXT("maren_no")) });
	MakeNode(TEXT("maren_yes"), {
		FLineBuilder(TEXT("Maren"), TEXT("Good on you. Keep them fed and they'll walk through fire for you. Here - ranch-style camp cooking, so you can feed them right."))
			.Teaches(TEXT("Chef:1")).OffersQuest(TEXT("q_firstcatch")) });
	MakeNode(TEXT("maren_no"), {
		FLineBuilder(TEXT("Maren"), TEXT("Weaken one first - fight it near-death - then throw a core. The meadow's patient. So am I."))
			.OffersQuest(TEXT("q_firstcatch")) });

	// --- Old Pell, ferryman ---
	MakeNode(TEXT("pell_intro"), {
		FLineBuilder(TEXT("Old Pell"), TEXT("Ah. The lantern chose you. It did me, sixty years back. I still hear it humming of nights.")) });
	MakeNode(TEXT("pell_loop"), {
		FLineBuilder(TEXT("Old Pell"), TEXT("Sixty years I've ferried folk to the Hollow mouth. None came back the same. Most didn't come back at all.")) });

	// --- Mira, well tender - heals your party for free ---
	MakeNode(TEXT("mira_intro"), {
		FLineBuilder(TEXT("Mira"), TEXT("Mind the stones, traveler - the well water remembers every stumble. Your creatures look weary. Here, let the water work.")).Heal().Advance(),
		FLineBuilder(TEXT("Mira"), TEXT("There. Good as spring-water fresh. Rest is the only medicine I can't draw from a rope - anything ails them, come find me.")) });
	MakeNode(TEXT("mira_loop"), {
		FLineBuilder(TEXT("Mira"), TEXT("Need the water's kindness again? Hold still a moment.")).Heal() });

	// --- Juna, professions warden ---
	MakeNode(TEXT("juna_intro"), {
		FLineBuilder(TEXT("Juna"), TEXT("I keep the Vale's professions: Forager, Miner, Fisher, Chef, Smith, Tailor. A person of three trades is worth ten of none. Reach level 2 in three trades and I'll swear you in."))
			.OffersQuest(TEXT("q_professions")) });
	MakeNode(TEXT("juna_loop"), {
		FLineBuilder(TEXT("Juna"), TEXT("The rolls remember every hand that fed the Vale. Keep working.")) });

	// --- Odessa, ranger captain ---
	MakeNode(TEXT("odessa_intro"), {
		FLineBuilder(TEXT("Odessa"), TEXT("Smoke over Emberrock Ridge, three days running. My prospectors should've been back by now. Will you go look?"))
			.ChoiceBranch(TEXT("odessa_yes"), TEXT("odessa_no")) });
	MakeNode(TEXT("odessa_yes"), {
		FLineBuilder(TEXT("Odessa"), TEXT("The bridge is west of here. Korrin runs that ridge like a private kingdom. Talk sense into him - however much sense takes."))
			.OffersQuest(TEXT("q_emberrock")) });
	MakeNode(TEXT("odessa_no"), {
		FLineBuilder(TEXT("Odessa"), TEXT("The smoke won't wait forever. Neither will I.")) });
	MakeNode(TEXT("odessa_loop"), {
		FLineBuilder(TEXT("Odessa"), TEXT("The Hollow... my grandmother spoke of it like weather. Something that just happens to the Vale, when it happens.")) });

	// --- Tobin, rival ---
	MakeNode(TEXT("tobin_fight"), {
		FLineBuilder(TEXT("Tobin"), TEXT("Heard you caught something! Ranch rules: we battle to see whose meadow education stuck. Let's go!"))
			.OffersQuest(TEXT("q_tobin"))
			.SpawnsTrainer(TEXT("trainer_tobin")) });
	MakeNode(TEXT("tobin_loop"), {
		FLineBuilder(TEXT("Tobin"), TEXT("Rematch anytime. I'm building a team that'll make the ridge look tame.")) });

	// --- Sela, herbalist ---
	MakeNode(TEXT("sela_intro"), {
		FLineBuilder(TEXT("Sela"), TEXT("You reek faintly of the Hollow, lantern-bearer. Good. I have work for exactly your kind of trouble. Will you hear it?"))
			.ChoiceBranch(TEXT("sela_yes"), TEXT("sela_no")) });
	MakeNode(TEXT("sela_yes"), {
		FLineBuilder(TEXT("Sela"), TEXT("The Verdant Sigil, in three fragments. Root sleeps under meadow stones, Tide sank with the lake traffic, Ember burned into the ridge fires. Bring me all three."))
			.OffersQuest(TEXT("q_sigil")) });
	MakeNode(TEXT("sela_no"), {
		FLineBuilder(TEXT("Sela"), TEXT("The fragments will keep. Unlike the Vale.")) });
	MakeNode(TEXT("sela_loop"), {
		FLineBuilder(TEXT("Sela"), TEXT("Grind bitterleaf, not your teeth. The Hollow feeds on hurry.")) });

	// --- Korrin, prospector ---
	MakeNode(TEXT("korrin_fight"), {
		FLineBuilder(TEXT("Korrin"), TEXT("Odessa sent you? The ridge is MINE by right of finding it. Show me your creatures are worth more than my claim!"))
			.SpawnsTrainer(TEXT("trainer_korrin")) });
	MakeNode(TEXT("korrin_loop"), {
		FLineBuilder(TEXT("Korrin"), TEXT("Fine, fine, the smoke's from the Hollow monolith. My digging woke it. Take your pick and leave me my claim.")).OpensCraft(TEXT("forge")) });

	// --- Vesna, angler --- (teaches fishing basics)
	MakeNode(TEXT("vesna_intro"), {
		FLineBuilder(TEXT("Vesna"), TEXT("Mirrorlake's gone quiet. Even the gildfish are nervous, and gildfish don't know enough to be nervous.")).Advance(),
		FLineBuilder(TEXT("Vesna"), TEXT("Here - how to cast so the line doesn't snap. Go on, the shoals are free.")).Teaches(TEXT("Fisher:1")) });
	MakeNode(TEXT("vesna_loop"), {
		FLineBuilder(TEXT("Vesna"), TEXT("Cast a line. The lake calms when it's fished. Always has.")) });

	// --- Hollis, bard ---
	MakeNode(TEXT("hollis_fight"), {
		FLineBuilder(TEXT("Hollis"), TEXT("A duel, then! Every saga needs one at the lakeside. Try to leave me a good verse for my defeat!"))
			.SpawnsTrainer(TEXT("trainer_hollis")) });
	MakeNode(TEXT("hollis_loop"), {
		FLineBuilder(TEXT("Hollis"), TEXT("'The lantern-bearer came, saw, and humbled me.' - I'm still workshopping it.")) });

	// --- Zephy, herald ---
	MakeNode(TEXT("zephy_fight"), {
		FLineBuilder(TEXT("Zephy"), TEXT("The Vale forgets. The Sleepers remember. I am the bell before the quiet. Ring, little light."))
			.OffersQuest(TEXT("q_herald"))
			.SpawnsTrainer(TEXT("trainer_zephy")) });
	MakeNode(TEXT("zephy_loop"), {
		FLineBuilder(TEXT("Zephy"), TEXT("So the Vale keeps its noise a while longer. Go down. Wake it gently.")) });

	// --- The Sleeper (finale) ---
	MakeNode(TEXT("sleeper_final"), {
		FLineBuilder(TEXT("The Sleeper"), TEXT("Little light. You climbed down carrying a dead god's sigil and a living flame."))
			.OffersQuest(TEXT("q_sleeper"))
			.ChoiceBranch(TEXT("sleeper_wake"), TEXT("sleeper_wait")) });
	MakeNode(TEXT("sleeper_wake"), {
		FLineBuilder(TEXT("The Sleeper"), TEXT("Then wake me. The Vale dreamed itself a guardian and forgot to tell it the war ended. I will hold. Take Gildra - my last gift to your small fires."))
			.SetsFlag(TEXT("sleeper_awake")).SpawnsCreature(TEXT("gildra:20")) });
	MakeNode(TEXT("sleeper_wait"), {
		FLineBuilder(TEXT("The Sleeper"), TEXT("Then sit. The dark is gentler than its reputation.")) });
	MakeNode(TEXT("sleeper_done"), {
		FLineBuilder(TEXT("The Sleeper"), TEXT("The Vale holds. Go tend your professions, little light. Grow things.")) });

	// ================= TRAINERS =================
	MakeTrainer(TEXT("trainer_tobin"), TEXT("Tobin"), TEXT("Ranch hand"), 80,
		{ TEXT("burrowmouse:5"), TEXT("flitwing:6") },
		{ TEXT("Ha! The meadow likes you better. Fine - you've earned the gate road.") },
		{ TEXT("Told you! Ranch hands rule the meadow!") },
		{ TEXT("beat_tobin") }, { TEXT("trail_snack:2") });

	MakeTrainer(TEXT("trainer_korrin"), TEXT("Korrin"), TEXT("Ridge prospector"), 150,
		{ TEXT("magmite:9"), TEXT("cragma:10"), TEXT("ashkit:9") },
		{ TEXT("Fine! FINE. The smoke's from the Hollow monolith - my deep digging woke it. Take the Root fragment I pried loose.") },
		{ TEXT("Come back when your creatures can lift a pick.") },
		{ TEXT("ridge_opened") }, { TEXT("sigil_piece_a:1"), TEXT("copper_ore:3") });

	MakeTrainer(TEXT("trainer_hollis"), TEXT("Hollis"), TEXT("Wandering bard"), 160,
		{ TEXT("mothlumen:12"), TEXT("dreamora:13") },
		{ TEXT("A verse for the victor! Take these gildfish - a bard pays in dinner.") },
		{ TEXT("And THAT is how you write a defeat ballad!") },
		{ TEXT("beat_hollis") }, { TEXT("sigil_piece_b:1"), TEXT("gildfish:2") });

	MakeTrainer(TEXT("trainer_zephy"), TEXT("Zephy"), TEXT("Hollow Herald"), 300,
		{ TEXT("sleeperling:16"), TEXT("frostwisp:16"), TEXT("nocturne:18") },
		{ TEXT("So the Vale gets to keep its noise a while longer. The Ember fragment still smolders in the ridge ores - mine it out. Then go down and wake it gently.") },
		{ TEXT("The quiet comes either way. I merely hurry it.") },
		{ TEXT("herald_down") }, { TEXT("hearty_stew:2") });
}

void FEmberwildDB::Init()
{
	InitMoves();
	InitSpecies();
	InitItems();
	InitNodes();
	InitStory();
}
