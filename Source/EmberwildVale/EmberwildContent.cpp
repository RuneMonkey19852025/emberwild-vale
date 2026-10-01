// Emberwild Vale — content: moves, species, items, recipes, gathering nodes, type chart.
// Faithful port of the Unity original's MoveDB / CreatureDB / ItemDB / NodeDB / TypeChart.

#include "EmberwildDB.h"

#include <initializer_list>
#include <utility>

// ---------------------------------------------------------------- type chart
float FTypeChart::Effectiveness(EElement Attack, EElement Defender)
{
	switch (Attack)
	{
	case EElement::Ember:
		switch (Defender)
		{
		case EElement::Verdant: case EElement::Frost: case EElement::Toxic: case EElement::Beast: return 2.f;
		case EElement::Tide: case EElement::Stone: case EElement::Ember: return 0.5f;
		default: return 1.f;
		}
	case EElement::Tide:
		switch (Defender)
		{
		case EElement::Ember: case EElement::Stone: return 2.f;
		case EElement::Verdant: case EElement::Tide: case EElement::Volt: return 0.5f;
		default: return 1.f;
		}
	case EElement::Verdant:
		switch (Defender)
		{
		case EElement::Tide: case EElement::Stone: return 2.f;
		case EElement::Ember: case EElement::Toxic: case EElement::Gale: case EElement::Frost: return 0.5f;
		default: return 1.f;
		}
	case EElement::Volt:
		switch (Defender)
		{
		case EElement::Tide: case EElement::Gale: return 2.f;
		case EElement::Volt: case EElement::Stone: return 0.5f;
		default: return 1.f;
		}
	case EElement::Stone:
		switch (Defender)
		{
		case EElement::Ember: case EElement::Gale: case EElement::Frost: return 2.f;
		case EElement::Verdant: case EElement::Beast: return 0.5f;
		default: return 1.f;
		}
	case EElement::Gale:
		switch (Defender)
		{
		case EElement::Verdant: case EElement::Toxic: case EElement::Beast: return 2.f;
		case EElement::Volt: case EElement::Stone: return 0.5f;
		default: return 1.f;
		}
	case EElement::Frost:
		switch (Defender)
		{
		case EElement::Verdant: case EElement::Gale: case EElement::Beast: return 2.f;
		case EElement::Ember: case EElement::Tide: case EElement::Frost: return 0.5f;
		default: return 1.f;
		}
	case EElement::Toxic:
		switch (Defender)
		{
		case EElement::Verdant: case EElement::Mind: return 2.f;
		case EElement::Toxic: case EElement::Stone: case EElement::Shade: return 0.5f;
		default: return 1.f;
		}
	case EElement::Mind:
		switch (Defender)
		{
		case EElement::Toxic: case EElement::Beast: return 2.f;
		case EElement::Shade: return 0.5f;
		default: return 1.f;
		}
	case EElement::Shade:
		switch (Defender)
		{
		case EElement::Mind: return 2.f;
		case EElement::Shade: case EElement::Beast: return 0.5f;
		default: return 1.f;
		}
	case EElement::Beast:
		switch (Defender)
		{
		case EElement::Mind: case EElement::Shade: return 2.f;
		case EElement::Gale: case EElement::Frost: return 0.5f;
		default: return 1.f;
		}
	case EElement::Normal:
		switch (Defender)
		{
		case EElement::Stone: case EElement::Shade: return 0.5f;
		default: return 1.f;
		}
	}
	return 1.f;
}

float FTypeChart::Effectiveness(EElement Attack, EElement Def1, EElement Def2)
{
	if (Def1 == Def2) return Effectiveness(Attack, Def1);
	return Effectiveness(Attack, Def1) * Effectiveness(Attack, Def2);
}

FString FTypeChart::Label(float Mult)
{
	if (Mult >= 2.f) return TEXT("Super effective!");
	if (Mult <= 0.5f && Mult > 0.f) return TEXT("Not very effective...");
	if (Mult <= 0.f) return TEXT("No effect...");
	return TEXT("");
}

FLinearColor FTypeChart::Color(EElement E)
{
	auto Hex = [](const TCHAR* H)
	{
		const FString S(H);
		if (S.Len() == 7 && S[0] == TEXT('#'))
		{
			const int32 R = FParse::HexDigit(S[1]) * 16 + FParse::HexDigit(S[2]);
			const int32 G = FParse::HexDigit(S[3]) * 16 + FParse::HexDigit(S[4]);
			const int32 B = FParse::HexDigit(S[5]) * 16 + FParse::HexDigit(S[6]);
			return FLinearColor(R / 255.f, G / 255.f, B / 255.f, 1.f);
		}
		return FLinearColor::White;
	};
	switch (E)
	{
	case EElement::Normal:  return Hex(TEXT("#A8A878"));
	case EElement::Ember:   return Hex(TEXT("#F08030"));
	case EElement::Tide:    return Hex(TEXT("#6890F0"));
	case EElement::Verdant: return Hex(TEXT("#78C850"));
	case EElement::Volt:    return Hex(TEXT("#F8D030"));
	case EElement::Stone:   return Hex(TEXT("#B8A038"));
	case EElement::Gale:    return Hex(TEXT("#A890F0"));
	case EElement::Frost:   return Hex(TEXT("#98D8D8"));
	case EElement::Toxic:   return Hex(TEXT("#A040A0"));
	case EElement::Mind:    return Hex(TEXT("#F85888"));
	case EElement::Shade:   return Hex(TEXT("#705898"));
	case EElement::Beast:   return Hex(TEXT("#705848"));
	}
	return FLinearColor::Gray;
}

// ---------------------------------------------------------------- tables
TMap<FString, FMoveDef>      FEmberwildDB::Moves;
TMap<FString, FSpeciesDef>   FEmberwildDB::Species;
TArray<FString>              FEmberwildDB::SpeciesOrder;
TMap<FString, FItemDef>      FEmberwildDB::Items;
TArray<FRecipe>              FEmberwildDB::Recipes;
TMap<FString, FNodeDef>      FEmberwildDB::Nodes;
TMap<FString, FQuestDef>     FEmberwildDB::Quests;
TMap<FString, FDialogueNode> FEmberwildDB::Dialogue;
TMap<FString, FTrainerDef>   FEmberwildDB::Trainers;
TMap<FString, FShopDef>      FEmberwildDB::Shops;
TMap<FString, FNpcDef>       FEmberwildDB::Npcs;
TSet<FString>                FEmberwildDB::EvolvedForms;

// ---------------------------------------------------------------- moves
static void M(const TCHAR* Id, const TCHAR* Name, EElement El, int32 Pow, int32 Acc, int32 PP,
              const TCHAR* Desc, const TCHAR* Effect = TEXT(""), const TCHAR* Status = TEXT(""), int32 StatusChance = 0)
{
	FMoveDef D;
	D.Id = Id; D.Name = Name; D.Element = El; D.Power = Pow; D.Accuracy = Acc; D.PP = PP;
	D.Desc = Desc; D.Effect = Effect; D.StatusId = Status; D.StatusChance = StatusChance;
	FEmberwildDB::Moves.Add(D.Id, D);
}

void FEmberwildDB::InitMoves()
{
	Moves.Empty();

	M(TEXT("tackle"),     TEXT("Tackle"),         EElement::Normal, 18, 100, 35, TEXT("A straightforward charge."));
	M(TEXT("scratch"),    TEXT("Scratch"),        EElement::Normal, 16, 100, 35, TEXT("Quick claws."));
	M(TEXT("quickpeck"),  TEXT("Quick Peck"),     EElement::Gale,   14, 100, 30, TEXT("A rapid jab of beak."));
	M(TEXT("bodyslam"),   TEXT("Body Slam"),      EElement::Normal, 34,  90, 20, TEXT("Full-weight slam."), TEXT("status"), TEXT("paralyze"), 20);
	M(TEXT("emberbit"),   TEXT("Ember Bit"),      EElement::Ember,  20, 100, 25, TEXT("A snapping spark."), TEXT("status"), TEXT("burn"), 10);
	M(TEXT("flurrena"),   TEXT("Flurrena"),       EElement::Ember,  42,  90, 15, TEXT("A whipping cone of flame."), TEXT("status"), TEXT("burn"), 15);
	M(TEXT("tuftide"),    TEXT("Tidepuff"),       EElement::Tide,   20, 100, 25, TEXT("A knockabout splash."));
	M(TEXT("cinder"),     TEXT("Cinder Burst"),   EElement::Ember,  44,  90, 12, TEXT("An erupting shell of embers."));
	M(TEXT("ripple"),     TEXT("Riplash"),        EElement::Tide,   40,  95, 15, TEXT("A coiling water lash."));
	M(TEXT("leafdart"),   TEXT("Leaf Dart"),      EElement::Verdant,18, 100, 30, TEXT("A flicked razor leaf."));
	M(TEXT("briar"),      TEXT("Briar Cage"),     EElement::Verdant,38,  90, 15, TEXT("Thorns lash out."), TEXT("status"), TEXT("poison"), 20);
	M(TEXT("spark"),      TEXT("Spark"),          EElement::Volt,   18, 100, 25, TEXT("A static nip."), TEXT("status"), TEXT("paralyze"), 20);
	M(TEXT("arc"),        TEXT("Arc Bolt"),       EElement::Volt,   44,  90, 12, TEXT("A snapping bolt."), TEXT("status"), TEXT("paralyze"), 10);
	M(TEXT("pebble"),     TEXT("Pebble Toss"),    EElement::Stone,  20,  95, 25, TEXT("Hurl loose stone."));
	M(TEXT("landslide"),  TEXT("Landslide"),      EElement::Stone,  46,  85, 10, TEXT("A crushing fall of rock."));
	M(TEXT("gust"),       TEXT("Gust"),           EElement::Gale,   18, 100, 30, TEXT("A puffing blast of air."));
	M(TEXT("cyclone"),    TEXT("Cyclone Cut"),    EElement::Gale,   42,  90, 12, TEXT("A whirling blade of wind."));
	M(TEXT("frostbite"),  TEXT("Frostbite"),      EElement::Frost,  20, 100, 25, TEXT("A biting chill."), TEXT("status"), TEXT("burn"), 10);
	M(TEXT("glacier"),    TEXT("Glacier Crash"),  EElement::Frost,  46,  85, 10, TEXT("A heavy rush of ice."));
	M(TEXT("venomspit"),  TEXT("Venom Spit"),     EElement::Toxic,  16, 100, 25, TEXT("Sputtering toxin."), TEXT("status"), TEXT("poison"), 35);
	M(TEXT("miasma"),     TEXT("Miasma"),         EElement::Toxic,  38,  90, 12, TEXT("A rolling fog of fumes."), TEXT("status"), TEXT("poison"), 30);
	M(TEXT("mindnip"),    TEXT("Mind Nip"),       EElement::Mind,   20, 100, 25, TEXT("A sting of pure thought."));
	M(TEXT("lucence"),    TEXT("Lucence"),        EElement::Mind,   44,  90, 12, TEXT("A lance of focused will."));
	M(TEXT("shadeslip"),  TEXT("Shade Slip"),     EElement::Shade,  20, 100, 25, TEXT("A strike from nowhere."));
	M(TEXT("nightfall"),  TEXT("Nightfall"),      EElement::Shade,  44,  90, 12, TEXT("Darkness bites deep."));
	M(TEXT("gnaw"),       TEXT("Gnaw"),           EElement::Beast,  22, 100, 25, TEXT("Savage bite."));
	M(TEXT("ravage"),     TEXT("Ravage"),         EElement::Beast,  46,  85, 10, TEXT("A wild flurry of fangs."));
	M(TEXT("harden"),     TEXT("Harden"),         EElement::Stone,   0,   0, 20, TEXT("Raises DEF."), TEXT("buff_def"));
	M(TEXT("growl"),      TEXT("Growl"),          EElement::Normal,  0,   0, 20, TEXT("Lowers foe's ATK."), TEXT("debuff_atk"));
	M(TEXT("focus"),      TEXT("Focus"),          EElement::Mind,    0,   0, 20, TEXT("Raises ATK."), TEXT("buff_atk"));
	M(TEXT("mend"),       TEXT("Mend"),           EElement::Verdant, 0,   0, 10, TEXT("Restores some HP."), TEXT("heal"));
	M(TEXT("soothe"),     TEXT("Soothe"),         EElement::Mind,    0,   0, 10, TEXT("Restores more HP."), TEXT("heal"));
	M(TEXT("leech"),      TEXT("Sap Sip"),        EElement::Verdant,22,  95, 15, TEXT("Drains HP to heal itself."), TEXT("drain"));
	M(TEXT("lullaby"),    TEXT("Lull Drone"),     EElement::Mind,    0,  75, 10, TEXT("Puts the foe to sleep."), TEXT("status"), TEXT("sleep"), 100);
	M(TEXT("flashstep"),  TEXT("Flash Step"),     EElement::Gale,   26, 100, 20, TEXT("Always strikes first."));
	M(TEXT("bulwark"),    TEXT("Bulwark"),        EElement::Stone,  30,  95, 20, TEXT("A brace and counterweight."));
	M(TEXT("springshoot"),TEXT("Spring Shoot"),   EElement::Verdant,26, 100, 20, TEXT("A sudden rising stalk."));
	M(TEXT("douse"),      TEXT("Douse"),          EElement::Tide,   26, 100, 20, TEXT("A aimed jet of water."));
	M(TEXT("mudsling"),   TEXT("Mud Sling"),      EElement::Stone,  24, 100, 20, TEXT("Wet, heavy earth."));
	M(TEXT("singsong"),   TEXT("Singsong"),       EElement::Normal,  0,  80, 10, TEXT("A drowsy tune."), TEXT("status"), TEXT("sleep"), 100);
	M(TEXT("fume"),       TEXT("Cinder Fume"),    EElement::Ember,  26, 100, 20, TEXT("Acrid smoky breath."), TEXT("status"), TEXT("burn"), 20);

	// mastery techniques (unlocked by weapon/armor mastery levels)
	M(TEXT("mt_rend"),       TEXT("Rend"),          EElement::Beast, 42,  95,  5, TEXT("Claws that remember every hunt."));
	M(TEXT("mt_inferno"),    TEXT("Inferno Rush"),  EElement::Ember, 58,  90,  4, TEXT("A fang-fueled blaze."), TEXT("status"), TEXT("burn"), 30);
	M(TEXT("mt_tempest"),    TEXT("Tempest Spine"), EElement::Volt,  70,  85,  3, TEXT("Lightning rides the spine's filament."));
	M(TEXT("mt_bulwarkslam"),TEXT("Bulwark Slam"),  EElement::Stone, 40,  95,  5, TEXT("A armored slam that steals breath."), TEXT("status"), TEXT("paralyze"), 20);
	M(TEXT("mt_gracewind"),  TEXT("Gracewind"),     EElement::Gale,  34, 100,  5, TEXT("Fast as charm-light, always striking first."));
	M(TEXT("mt_aegis"),      TEXT("Aegis Guard"),   EElement::Stone,  0,   0,  4, TEXT("Armor-forged poise: raises ATK and DEF."), TEXT("buff_atk_def"));
}

// ---------------------------------------------------------------- species
static void S(const TCHAR* Id, const TCHAR* Name, EElement El,
              int32 HP, int32 ATK, int32 DEF, int32 SPD, const TCHAR* Desc,
              const TCHAR* Evo, int32 EvoLvl, std::initializer_list<const TCHAR*> Learnset,
              const TCHAR* Body, const TCHAR* Accent, const TCHAR* Shape, float Scale,
              int32 CatchRate, int32 XpYield, const TCHAR* Zone)
{
	FSpeciesDef D;
	D.Id = Id; D.Name = Name; D.Element = El; D.Element2 = El;
	D.BaseHP = HP; D.BaseATK = ATK; D.BaseDEF = DEF; D.BaseSPD = SPD;
	D.Desc = Desc; D.EvolvesInto = Evo; D.EvolveLevel = EvoLvl;
	for (const TCHAR* MV : Learnset) D.Learnset.Add(MV);
	D.PaletteHex = Body; D.AccentHex = Accent; D.Shape = Shape; D.Scale = Scale;
	D.CatchRate = CatchRate; D.XpYield = XpYield; D.HabitatZone = Zone;
	FEmberwildDB::Species.Add(D.Id, D);
	FEmberwildDB::SpeciesOrder.Add(D.Id);
}

void FEmberwildDB::InitSpecies()
{
	Species.Empty();
	SpeciesOrder.Empty();

	// ---------- Meadowfall (starting zone) ----------
	S(TEXT("sprigling"), TEXT("Sprigling"), EElement::Verdant, 12, 11, 10, 9,
	  TEXT("A sprouting seed-fox that naps in sunbeams."),
	  TEXT("thornpaw"), 14, { TEXT("tackle"), TEXT("leafdart"), TEXT("growl"), TEXT("springshoot"), TEXT("leech"), TEXT("briar") },
	  TEXT("#7FC46B"), TEXT("#3E7A3A"), TEXT("quad"), 1.f, 72, 22, TEXT("meadow"));
	S(TEXT("thornpaw"), TEXT("Thornpaw"), EElement::Verdant, 16, 15, 13, 12,
	  TEXT("Its bramble tail sweeps trails clean behind it."),
	  TEXT("briarwolf"), 30, { TEXT("scratch"), TEXT("leafdart"), TEXT("springshoot"), TEXT("leech"), TEXT("briar"), TEXT("focus") },
	  TEXT("#5FA84F"), TEXT("#2C5E2A"), TEXT("quad"), 1.25f, 45, 42, TEXT("meadow"));
	S(TEXT("briarwolf"), TEXT("Briarwolf"), EElement::Verdant, 22, 22, 18, 15,
	  TEXT("A guardian of overgrown ruins, patient and proud."),
	  TEXT("thornmare"), 40, { TEXT("scratch"), TEXT("leafdart"), TEXT("briar"), TEXT("focus"), TEXT("ravage"), TEXT("mend") },
	  TEXT("#4A8A3E"), TEXT("#1F4A1E"), TEXT("quad"), 1.55f, 30, 66, TEXT("meadow"));
	S(TEXT("thornmare"), TEXT("Thornmare"), EElement::Verdant, 30, 28, 24, 20,
	  TEXT("Its thicket mane blooms each spring, then goes to war."),
	  TEXT(""), 0, { TEXT("leafdart"), TEXT("briar"), TEXT("focus"), TEXT("ravage"), TEXT("leech"), TEXT("springshoot") },
	  TEXT("#3C7A34"), TEXT("#18401A"), TEXT("quad"), 1.85f, 25, 84, TEXT("meadow"));

	S(TEXT("puddlepup"), TEXT("Puddlepup"), EElement::Tide, 13, 10, 11, 10,
	  TEXT("Chases raindrops and puddles with equal joy."),
	  TEXT("tidehound"), 15, { TEXT("tackle"), TEXT("tuftide"), TEXT("growl"), TEXT("douse"), TEXT("ripple"), TEXT("bodyslam") },
	  TEXT("#6FA8DC"), TEXT("#33628F"), TEXT("quad"), 1.f, 72, 22, TEXT("meadow"));
	S(TEXT("tidehound"), TEXT("Tidehound"), EElement::Tide, 18, 14, 14, 14,
	  TEXT("Its bark sounds like a wave clapping stone."),
	  TEXT("maelmoor"), 30, { TEXT("tackle"), TEXT("tuftide"), TEXT("douse"), TEXT("ripple"), TEXT("bodyslam"), TEXT("harden") },
	  TEXT("#5B94C8"), TEXT("#28527E"), TEXT("quad"), 1.3f, 45, 42, TEXT("meadow"));
	S(TEXT("maelmoor"), TEXT("Maelmoor"), EElement::Tide, 24, 21, 19, 16,
	  TEXT("Old riverfolk say it herds the currents themselves."),
	  TEXT("abysshepherd"), 40, { TEXT("douse"), TEXT("ripple"), TEXT("bodyslam"), TEXT("harden"), TEXT("glacier"), TEXT("soothe") },
	  TEXT("#477FB3"), TEXT("#1E4066"), TEXT("quad"), 1.6f, 30, 66, TEXT("meadow"));
	S(TEXT("abysshepherd"), TEXT("Abysshepherd"), EElement::Tide, 32, 27, 25, 21,
	  TEXT("It surveys the deep water and counts it, like coins."),
	  TEXT(""), 0, { TEXT("ripple"), TEXT("bodyslam"), TEXT("harden"), TEXT("glacier"), TEXT("soothe"), TEXT("lullaby") },
	  TEXT("#3A6C9E"), TEXT("#183252"), TEXT("quad"), 1.9f, 25, 84, TEXT("meadow"));

	S(TEXT("cindercub"), TEXT("Cindercub"), EElement::Ember, 12, 12, 9, 11,
	  TEXT("Sleeps curled around its own warm tail."),
	  TEXT("pyrelynx"), 14, { TEXT("scratch"), TEXT("emberbit"), TEXT("growl"), TEXT("fume"), TEXT("flurrena"), TEXT("focus") },
	  TEXT("#E8824A"), TEXT("#A34A22"), TEXT("quad"), 1.f, 72, 22, TEXT("meadow"));
	S(TEXT("pyrelynx"), TEXT("Pyrelynx"), EElement::Ember, 17, 17, 12, 15,
	  TEXT("Twin tail-flames flicker with its mood."),
	  TEXT("solarmane"), 30, { TEXT("scratch"), TEXT("emberbit"), TEXT("fume"), TEXT("flurrena"), TEXT("focus"), TEXT("quickpeck") },
	  TEXT("#D96F35"), TEXT("#8C3B18"), TEXT("quad"), 1.3f, 45, 42, TEXT("meadow"));
	S(TEXT("solarmane"), TEXT("Solarmane"), EElement::Ember, 23, 24, 17, 19,
	  TEXT("Its mane burns brightest at noon."),
	  TEXT("pyrelord"), 40, { TEXT("emberbit"), TEXT("fume"), TEXT("flurrena"), TEXT("focus"), TEXT("cinder"), TEXT("ravage") },
	  TEXT("#C25C28"), TEXT("#6E2D12"), TEXT("quad"), 1.6f, 30, 66, TEXT("meadow"));
	S(TEXT("pyrelord"), TEXT("Pyrelord"), EElement::Ember, 31, 31, 24, 24,
	  TEXT("Dawn arrives a little early wherever it stretches."),
	  TEXT(""), 0, { TEXT("flurrena"), TEXT("fume"), TEXT("cinder"), TEXT("focus"), TEXT("ravage"), TEXT("emberbit") },
	  TEXT("#B04E22"), TEXT("#5E2410"), TEXT("quad"), 1.9f, 25, 84, TEXT("meadow"));

	S(TEXT("flitwing"), TEXT("Flitwing"), EElement::Gale, 11, 11, 8, 13,
	  TEXT("A pocket-sized storm in feathers."),
	  TEXT("galedart"), 13, { TEXT("quickpeck"), TEXT("gust"), TEXT("growl"), TEXT("flashstep"), TEXT("cyclone"), TEXT("bodyslam") },
	  TEXT("#9FB6E8"), TEXT("#5A6FA8"), TEXT("avian"), 0.9f, 78, 20, TEXT("meadow"));
	S(TEXT("galedart"), TEXT("Galedart"), EElement::Gale, 15, 16, 11, 18,
	  TEXT("Rides thermals over the whole valley in a morning."),
	  TEXT("skylineer"), 36, { TEXT("quickpeck"), TEXT("gust"), TEXT("flashstep"), TEXT("cyclone"), TEXT("bodyslam"), TEXT("focus") },
	  TEXT("#8AA5DE"), TEXT("#4A5F98"), TEXT("avian"), 1.2f, 45, 44, TEXT("meadow"));
	S(TEXT("skylineer"), TEXT("Skyliner"), EElement::Gale, 23, 23, 17, 26,
	  TEXT("It races the horizon and usually wins."),
	  TEXT(""), 0, { TEXT("quickpeck"), TEXT("flashstep"), TEXT("cyclone"), TEXT("bodyslam"), TEXT("focus"), TEXT("gust") },
	  TEXT("#7A98D2"), TEXT("#3A5088"), TEXT("avian"), 1.5f, 35, 64, TEXT("meadow"));

	S(TEXT("burrowmouse"), TEXT("Burrowmouse"), EElement::Normal, 12, 10, 10, 11,
	  TEXT("Its cheeks are bigger than its ambitions."),
	  TEXT("vaultmole"), 12, { TEXT("tackle"), TEXT("scratch"), TEXT("growl"), TEXT("mudsling"), TEXT("bulwark"), TEXT("bodyslam") },
	  TEXT("#C4A882"), TEXT("#8A7050"), TEXT("quad"), 0.9f, 82, 20, TEXT("meadow"));
	S(TEXT("vaultmole"), TEXT("Vaultmole"), EElement::Stone, 17, 14, 15, 8,
	  TEXT("Digs counting-holes: one for every coin it finds."),
	  TEXT("grottolord"), 36, { TEXT("scratch"), TEXT("mudsling"), TEXT("bulwark"), TEXT("pebble"), TEXT("landslide"), TEXT("harden") },
	  TEXT("#A88F6C"), TEXT("#6E5A40"), TEXT("quad"), 1.15f, 50, 44, TEXT("meadow"));

	S(TEXT("thumper"), TEXT("Thumper"), EElement::Beast, 12, 13, 9, 12,
	  TEXT("Its ears pick up buried seeds three fields away."),
	  TEXT("boulderhare"), 16, { TEXT("tackle"), TEXT("gnaw"), TEXT("growl"), TEXT("mudsling"), TEXT("ravage"), TEXT("bodyslam") },
	  TEXT("#D8B890"), TEXT("#8A6848"), TEXT("quad"), 0.95f, 68, 21, TEXT("meadow"));
	S(TEXT("boulderhare"), TEXT("Boulderhare"), EElement::Beast, 18, 20, 13, 16,
	  TEXT("It kicks boulders for practice and grudges."),
	  TEXT(""), 0, { TEXT("gnaw"), TEXT("ravage"), TEXT("mudsling"), TEXT("bodyslam"), TEXT("focus"), TEXT("bulwark") },
	  TEXT("#C4A276"), TEXT("#6E5438"), TEXT("quad"), 1.25f, 44, 46, TEXT("meadow"));
	S(TEXT("grottolord"), TEXT("Grottolord"), EElement::Stone, 27, 21, 27, 11,
	  TEXT("Its burrow is a bank. Its patience, a vault door."),
	  TEXT(""), 0, { TEXT("mudsling"), TEXT("bulwark"), TEXT("landslide"), TEXT("harden"), TEXT("pebble"), TEXT("bodyslam") },
	  TEXT("#96795A"), TEXT("#58452E"), TEXT("quad"), 1.55f, 35, 64, TEXT("meadow"));

	// ---------- Emberrock Ridge ----------
	S(TEXT("magmite"), TEXT("Magmite"), EElement::Ember, 14, 13, 12, 6,
	  TEXT("A slow blob of cooling lava. Warm to hold."),
	  TEXT("cragma"), 24, { TEXT("emberbit"), TEXT("pebble"), TEXT("harden"), TEXT("fume"), TEXT("landslide"), TEXT("flurrena") },
	  TEXT("#D06838"), TEXT("#7A3418"), TEXT("rock"), 1.f, 55, 26, TEXT("ridge"));
	S(TEXT("cragma"), TEXT("Cragma"), EElement::Stone, 20, 18, 20, 6,
	  TEXT("What the lava left behind grew a temper."),
	  TEXT("obcelandor"), 40, { TEXT("pebble"), TEXT("harden"), TEXT("landslide"), TEXT("bulwark"), TEXT("emberbit"), TEXT("fume") },
	  TEXT("#9A6A4A"), TEXT("#5A3A24"), TEXT("rock"), 1.4f, 40, 50, TEXT("ridge"));
	S(TEXT("obcelandor"), TEXT("Obcelandor"), EElement::Stone, 30, 26, 30, 9,
	  TEXT("A walking monument. Maps disagree about where it lives."),
	  TEXT(""), 0, { TEXT("landslide"), TEXT("bulwark"), TEXT("harden"), TEXT("fume"), TEXT("emberbit"), TEXT("pebble") },
	  TEXT("#825840"), TEXT("#482C1C"), TEXT("rock"), 1.8f, 32, 72, TEXT("ridge"));
	S(TEXT("ashkit"), TEXT("Ashkit"), EElement::Ember, 13, 14, 9, 13,
	  TEXT("Leaves gray footprints that glow briefly."),
	  TEXT("smoulder"), 22, { TEXT("scratch"), TEXT("emberbit"), TEXT("flashstep"), TEXT("fume"), TEXT("flurrena"), TEXT("growl") },
	  TEXT("#B85A50"), TEXT("#70302C"), TEXT("quad"), 1.f, 55, 26, TEXT("ridge"));
	S(TEXT("smoulder"), TEXT("Smoulder"), EElement::Ember, 19, 20, 13, 17,
	  TEXT("Its purr is a low banked fire."),
	  TEXT("pyrelynx_king"), 38, { TEXT("emberbit"), TEXT("fume"), TEXT("flurrena"), TEXT("flashstep"), TEXT("focus"), TEXT("scratch") },
	  TEXT("#A04840"), TEXT("#5C2622"), TEXT("quad"), 1.3f, 40, 50, TEXT("ridge"));

	S(TEXT("sparkit"), TEXT("Sparkit"), EElement::Volt, 12, 12, 9, 14,
	  TEXT("Static clings to its fur like a second coat."),
	  TEXT("voltfang"), 18, { TEXT("spark"), TEXT("flashstep"), TEXT("growl"), TEXT("mudsling"), TEXT("arc"), TEXT("focus") },
	  TEXT("#E8D05A"), TEXT("#8C7A20"), TEXT("quad"), 0.95f, 58, 26, TEXT("ridge"));
	S(TEXT("voltfang"), TEXT("Voltfang"), EElement::Volt, 18, 19, 13, 20,
	  TEXT("Its bite carries a warning label."),
	  TEXT("fulgurox"), 34, { TEXT("spark"), TEXT("arc"), TEXT("flashstep"), TEXT("focus"), TEXT("bodyslam"), TEXT("cyclone") },
	  TEXT("#D8C048"), TEXT("#786818"), TEXT("quad"), 1.25f, 40, 52, TEXT("ridge"));
	S(TEXT("fulgurox"), TEXT("Fulgurox"), EElement::Volt, 24, 26, 18, 22,
	  TEXT("When it howls, the ridge answers in thunder."),
	  TEXT(""), 0, { TEXT("arc"), TEXT("flashstep"), TEXT("focus"), TEXT("cyclone"), TEXT("bodyslam"), TEXT("gnaw") },
	  TEXT("#C8B038"), TEXT("#685810"), TEXT("quad"), 1.55f, 30, 68, TEXT("ridge"));
	S(TEXT("pyrelynx_king"), TEXT("Emberlynx"), EElement::Ember, 27, 28, 19, 24,
	  TEXT("Its whiskers are lit fuses. It is always calm about it."),
	  TEXT(""), 0, { TEXT("fume"), TEXT("flurrena"), TEXT("flashstep"), TEXT("focus"), TEXT("scratch"), TEXT("cinder") },
	  TEXT("#8E3C34"), TEXT("#4E1E1A"), TEXT("quad"), 1.6f, 32, 70, TEXT("ridge"));

	// ---------- Mirrorlake / fishing ----------
	S(TEXT("glimmfish"), TEXT("Glimmfish"), EElement::Tide, 12, 9, 10, 12,
	  TEXT("Schools sparkle like spilled coins."),
	  TEXT("luminkeel"), 16, { TEXT("tuftide"), TEXT("douse"), TEXT("growl"), TEXT("ripple"), TEXT("soothe"), TEXT("bodyslam") },
	  TEXT("#8AC8D8"), TEXT("#4A8898"), TEXT("aquatic"), 0.9f, 65, 22, TEXT("lake"));
	S(TEXT("luminkeel"), TEXT("Luminkeel"), EElement::Tide, 18, 14, 14, 15,
	  TEXT("Its fin-fronds glow where the water is deepest."),
	  TEXT("aurorafin"), 38, { TEXT("tuftide"), TEXT("douse"), TEXT("ripple"), TEXT("soothe"), TEXT("bodyslam"), TEXT("glacier") },
	  TEXT("#6FB0C4"), TEXT("#3A7080"), TEXT("aquatic"), 1.25f, 45, 46, TEXT("lake"));
	S(TEXT("aurorafin"), TEXT("Aurorafin"), EElement::Tide, 26, 21, 20, 22,
	  TEXT("The lake lights up at dusk wherever it surfaces."),
	  TEXT(""), 0, { TEXT("douse"), TEXT("ripple"), TEXT("soothe"), TEXT("bodyslam"), TEXT("glacier"), TEXT("lullaby") },
	  TEXT("#5C9CB4"), TEXT("#2E5E6E"), TEXT("aquatic"), 1.6f, 32, 68, TEXT("lake"));
	S(TEXT("lilypad"), TEXT("Lilypad"), EElement::Verdant, 14, 9, 13, 8,
	  TEXT("A patient floater. Quite content. Do not disturb."),
	  TEXT("bloomdrift"), 18, { TEXT("leafdart"), TEXT("leech"), TEXT("harden"), TEXT("springshoot"), TEXT("mend"), TEXT("briar") },
	  TEXT("#7AB86A"), TEXT("#4A7A40"), TEXT("aquatic"), 1.f, 60, 24, TEXT("lake"));
	S(TEXT("bloomdrift"), TEXT("Bloomdrift"), EElement::Verdant, 20, 14, 18, 10,
	  TEXT("It blooms once a year, magnificently, then naps."),
	  TEXT("lilycrown"), 38, { TEXT("leafdart"), TEXT("leech"), TEXT("springshoot"), TEXT("mend"), TEXT("briar"), TEXT("soothe") },
	  TEXT("#68A85A"), TEXT("#3A6A32"), TEXT("aquatic"), 1.3f, 45, 48, TEXT("lake"));

	S(TEXT("frostfin"), TEXT("Frostfin"), EElement::Frost, 12, 11, 10, 12,
	  TEXT("Schools of it turn the shallows to slush."),
	  TEXT("rimefin"), 18, { TEXT("frostbite"), TEXT("douse"), TEXT("growl"), TEXT("gust"), TEXT("soothe"), TEXT("bodyslam") },
	  TEXT("#A8D8E8"), TEXT("#4A88A0"), TEXT("aquatic"), 0.95f, 62, 23, TEXT("lake"));
	S(TEXT("rimefin"), TEXT("Rimefin"), EElement::Frost, 18, 16, 15, 17,
	  TEXT("Its wake freezes into lace across the water."),
	  TEXT(""), 0, { TEXT("frostbite"), TEXT("glacier"), TEXT("douse"), TEXT("soothe"), TEXT("cyclone"), TEXT("bodyslam") },
	  TEXT("#8CC8DC"), TEXT("#3A7288"), TEXT("aquatic"), 1.3f, 42, 47, TEXT("lake"));
	S(TEXT("lilycrown"), TEXT("Lilycrown"), EElement::Verdant, 28, 20, 26, 14,
	  TEXT("Its flower court opens in ceremony at first light."),
	  TEXT(""), 0, { TEXT("leech"), TEXT("springshoot"), TEXT("mend"), TEXT("briar"), TEXT("soothe"), TEXT("leafdart") },
	  TEXT("#58984C"), TEXT("#2C5A28"), TEXT("aquatic"), 1.6f, 32, 68, TEXT("lake"));

	// ---------- Hushpine Hollow ----------
	S(TEXT("mothlumen"), TEXT("Mothlumen"), EElement::Mind, 12, 11, 9, 12,
	  TEXT("Wings dusted with dream-pollen."),
	  TEXT("dreamora"), 20, { TEXT("mindnip"), TEXT("lullaby"), TEXT("soothe"), TEXT("lucence"), TEXT("gust"), TEXT("growl") },
	  TEXT("#C8A8E0"), TEXT("#8068A8"), TEXT("bug"), 0.9f, 60, 24, TEXT("hollow"));
	S(TEXT("dreamora"), TEXT("Dreamora"), EElement::Mind, 18, 17, 13, 16,
	  TEXT("It hums the thoughts you haven't said aloud."),
	  TEXT("reverie"), 40, { TEXT("mindnip"), TEXT("lullaby"), TEXT("soothe"), TEXT("lucence"), TEXT("nightfall"), TEXT("focus") },
	  TEXT("#B090CC"), TEXT("#68508C"), TEXT("bug"), 1.25f, 45, 48, TEXT("hollow"));
	S(TEXT("reverie"), TEXT("Reverie"), EElement::Mind, 26, 25, 19, 23,
	  TEXT("Half remembered by morning, impossible to forget by night."),
	  TEXT(""), 0, { TEXT("lucence"), TEXT("lullaby"), TEXT("soothe"), TEXT("nightfall"), TEXT("focus"), TEXT("mindnip") },
	  TEXT("#9E7CBE"), TEXT("#543C76"), TEXT("bug"), 1.6f, 32, 70, TEXT("hollow"));
	S(TEXT("gloomcap"), TEXT("Gloomcap"), EElement::Toxic, 14, 12, 12, 7,
	  TEXT("A grumpy mushroom with opinions about weather."),
	  TEXT("sporesage"), 22, { TEXT("venomspit"), TEXT("harden"), TEXT("leech"), TEXT("miasma"), TEXT("springshoot"), TEXT("growl") },
	  TEXT("#9A78B0"), TEXT("#5C4874"), TEXT("bug"), 1.f, 60, 25, TEXT("hollow"));
	S(TEXT("sporesage"), TEXT("Sporesage"), EElement::Toxic, 20, 18, 17, 9,
	  TEXT("Speaks in spore-clouds. The trees listen."),
	  TEXT("mycelorn"), 40, { TEXT("venomspit"), TEXT("miasma"), TEXT("leech"), TEXT("springshoot"), TEXT("mend"), TEXT("harden") },
	  TEXT("#8668A0"), TEXT("#4C3860"), TEXT("bug"), 1.35f, 45, 50, TEXT("hollow"));
	S(TEXT("mycelorn"), TEXT("Mycelorn"), EElement::Toxic, 29, 25, 24, 13,
	  TEXT("A prophet of rot: what it quotes, the forest does."),
	  TEXT(""), 0, { TEXT("miasma"), TEXT("leech"), TEXT("springshoot"), TEXT("mend"), TEXT("harden"), TEXT("venomspit") },
	  TEXT("#6E5490"), TEXT("#3A2A50"), TEXT("bug"), 1.7f, 32, 72, TEXT("hollow"));
	S(TEXT("umbrafox"), TEXT("Umbrafox"), EElement::Shade, 15, 15, 10, 14,
	  TEXT("Seen only as a smile, and then not even that."),
	  TEXT("nocturne"), 24, { TEXT("shadeslip"), TEXT("growl"), TEXT("nightfall"), TEXT("lullaby"), TEXT("ravage"), TEXT("mindnip") },
	  TEXT("#6A5A8A"), TEXT("#3A3054"), TEXT("quad"), 1.f, 55, 28, TEXT("hollow"));
	S(TEXT("nocturne"), TEXT("Nocturne"), EElement::Shade, 21, 22, 14, 18,
	  TEXT("The Hollow's own shadow, walking politely abroad."),
	  TEXT("eclipse"), 42, { TEXT("shadeslip"), TEXT("nightfall"), TEXT("lullaby"), TEXT("ravage"), TEXT("mindnip"), TEXT("focus") },
	  TEXT("#564873"), TEXT("#2A2240"), TEXT("quad"), 1.3f, 40, 54, TEXT("hollow"));

	S(TEXT("duskstalker"), TEXT("Duskstalker"), EElement::Beast, 13, 15, 9, 14,
	  TEXT("You hear the paws. You never see the paws."),
	  TEXT("nightmaw"), 22, { TEXT("gnaw"), TEXT("flashstep"), TEXT("growl"), TEXT("nightfall"), TEXT("ravage"), TEXT("focus") },
	  TEXT("#8A6A4A"), TEXT("#4A3428"), TEXT("quad"), 1.f, 58, 26, TEXT("hollow"));
	S(TEXT("nightmaw"), TEXT("Nightmaw"), EElement::Beast, 19, 22, 13, 19,
	  TEXT("Its yawn is the last thing the dark says."),
	  TEXT(""), 0, { TEXT("ravage"), TEXT("gnaw"), TEXT("flashstep"), TEXT("nightfall"), TEXT("focus"), TEXT("bodyslam") },
	  TEXT("#6E5238"), TEXT("#382818"), TEXT("quad"), 1.35f, 40, 52, TEXT("hollow"));

	S(TEXT("blightwing"), TEXT("Blightwing"), EElement::Toxic, 12, 11, 10, 13,
	  TEXT("A moth whose dust is best left unsniffed."),
	  TEXT("miasmoth"), 20, { TEXT("venomspit"), TEXT("gust"), TEXT("lullaby"), TEXT("leech"), TEXT("miasma"), TEXT("growl") },
	  TEXT("#7A9A50"), TEXT("#3E5A2A"), TEXT("bug"), 0.95f, 60, 24, TEXT("hollow"));
	S(TEXT("miasmoth"), TEXT("Miasmoth"), EElement::Toxic, 18, 16, 14, 18,
	  TEXT("Its wingbeat spreads a green summer storm."),
	  TEXT(""), 0, { TEXT("miasma"), TEXT("venomspit"), TEXT("leech"), TEXT("gust"), TEXT("cyclone"), TEXT("lullaby") },
	  TEXT("#688A42"), TEXT("#2E4A1E"), TEXT("bug"), 1.3f, 42, 50, TEXT("hollow"));
	S(TEXT("eclipse"), TEXT("Eclipse"), EElement::Shade, 29, 29, 20, 26,
	  TEXT("The moon borrows its smile on dark nights."),
	  TEXT(""), 0, { TEXT("nightfall"), TEXT("lullaby"), TEXT("ravage"), TEXT("mindnip"), TEXT("focus"), TEXT("shadeslip") },
	  TEXT("#44375C"), TEXT("#1E1830"), TEXT("quad"), 1.65f, 30, 76, TEXT("hollow"));

	// ---------- The Hollow (rare) ----------
	S(TEXT("sleeperling"), TEXT("Sleeperling"), EElement::Mind, 16, 14, 14, 12,
	  TEXT("A fragment of a Hollow Sleeper, dreaming itself awake."),
	  TEXT("dreamwarden"), 45, { TEXT("mindnip"), TEXT("lullaby"), TEXT("soothe"), TEXT("lucence"), TEXT("nightfall"), TEXT("focus") },
	  TEXT("#B8C8F0"), TEXT("#6878A8"), TEXT("spirit"), 1.1f, 25, 40, TEXT("hollow_deep"));
	S(TEXT("dreamwarden"), TEXT("Dreamwarden"), EElement::Mind, 30, 26, 26, 22,
	  TEXT("It keeps the Sleeper's dreams from wandering off alone."),
	  TEXT(""), 0, { TEXT("lucence"), TEXT("lullaby"), TEXT("soothe"), TEXT("nightfall"), TEXT("focus"), TEXT("bodyslam") },
	  TEXT("#9CB2E8"), TEXT("#506090"), TEXT("spirit"), 1.5f, 22, 90, TEXT("hollow_deep"));
	S(TEXT("frostwisp"), TEXT("Frostwisp"), EElement::Frost, 15, 14, 11, 16,
	  TEXT("A cold breath the mountain forgot to exhale."),
	  TEXT("glacierveil"), 45, { TEXT("frostbite"), TEXT("gust"), TEXT("soothe"), TEXT("glacier"), TEXT("cyclone"), TEXT("lullaby") },
	  TEXT("#BFE4E8"), TEXT("#6AA8B0"), TEXT("spirit"), 1.f, 28, 38, TEXT("hollow_deep"));
	S(TEXT("glacierveil"), TEXT("Glacierveil"), EElement::Frost, 27, 25, 22, 25,
	  TEXT("Winter follows it around like a loyal dog."),
	  TEXT(""), 0, { TEXT("glacier"), TEXT("cyclone"), TEXT("soothe"), TEXT("frostbite"), TEXT("gust"), TEXT("lullaby") },
	  TEXT("#A2D4DA"), TEXT("#549098"), TEXT("spirit"), 1.45f, 24, 88, TEXT("hollow_deep"));
	S(TEXT("gildra"), TEXT("Gildra"), EElement::Beast, 22, 20, 16, 18,
	  TEXT("A golden-maned guardian beast from valley legend."),
	  TEXT("aurelion"), 60, { TEXT("gnaw"), TEXT("ravage"), TEXT("focus"), TEXT("bulwark"), TEXT("cyclone"), TEXT("nightfall") },
	  TEXT("#E0B84A"), TEXT("#A87C28"), TEXT("quad"), 1.5f, 15, 60, TEXT("hollow_deep"));
	S(TEXT("aurelion"), TEXT("Aurelion"), EElement::Beast, 32, 30, 24, 27,
	  TEXT("The legend, grown into its whole name. The Vale shines brighter for it."),
	  TEXT(""), 0, { TEXT("ravage"), TEXT("focus"), TEXT("bulwark"), TEXT("cyclone"), TEXT("nightfall"), TEXT("gnaw") },
	  TEXT("#F2CC58"), TEXT("#B88C30"), TEXT("quad"), 1.85f, 12, 110, TEXT("hollow_deep"));

	// evolved-forms index for IsBaseForm / WildPoolFor
	EvolvedForms.Reset();
	for (const TPair<FString, FSpeciesDef>& KV : Species)
		if (!KV.Value.EvolvesInto.IsEmpty())
			EvolvedForms.Add(KV.Value.EvolvesInto);
}

// ---------------------------------------------------------------- items
static void I_(const TCHAR* Id, const TCHAR* Name, const TCHAR* Desc, EItemKind Kind, int32 Price,
               const TCHAR* Prof = TEXT(""), const TCHAR* Icon = TEXT("#B9C4CC"),
               int32 Heal = 0, int32 Cure = 0, float Catch = 1.f, const TCHAR* Equip = TEXT(""),
               EPlayerWeaponKind WKind = EPlayerWeaponKind::None, bool bSellable = true)
{
	FItemDef D;
	D.Id = Id; D.Name = Name; D.Desc = Desc; D.Kind = Kind; D.BasePrice = Price;
	D.ProfessionId = Prof; D.IconColorHex = Icon; D.HealAmount = Heal; D.ConditionCure = Cure;
	D.CatchBonus = Catch; D.EquipBonus = Equip; D.PlayerWeaponKind = WKind; D.bSellable = bSellable;
	FEmberwildDB::Items.Add(D.Id, D);
}

static void R(const TCHAR* Id, const TCHAR* Result, int32 ResultCount, EProfessionId Prof, int32 ReqLvl,
              int32 Xp, const TCHAR* Station, std::initializer_list<std::pair<const TCHAR*, int32>> Inputs)
{
	FRecipe D;
	D.Id = Id; D.ResultItemId = Result; D.ResultCount = ResultCount; D.Profession = Prof;
	D.RequiredLevel = ReqLvl; D.XpReward = Xp; D.Station = Station;
	for (const auto& P : Inputs) (void)D.Inputs.Emplace_GetRef(P.first, P.second);
	FEmberwildDB::Recipes.Add(D);
}

void FEmberwildDB::InitItems()
{
	Items.Empty();
	Recipes.Empty();

	// ---------------- Materials ----------------
	I_(TEXT("fiber"),        TEXT("Sunfiber"),      TEXT("Tough golden stalks. Weaves into rope and cloth."), EItemKind::Material, 4,  TEXT("Forager"), TEXT("#C9B458"));
	I_(TEXT("bitterleaf"),   TEXT("Bitterleaf"),    TEXT("Pungent herb. Cooks into medicine."),               EItemKind::Material, 5,  TEXT("Forager"), TEXT("#7C9A4E"));
	I_(TEXT("glowcap"),      TEXT("Glowcap"),       TEXT("A mushroom that hums with soft light."),            EItemKind::Material, 7,  TEXT("Forager"), TEXT("#BEE39B"));
	I_(TEXT("emberpepper"),  TEXT("Emberpepper"),   TEXT("Warm to the touch. Loved by Ember creatures."),     EItemKind::Material, 6,  TEXT("Forager"), TEXT("#D9543F"));
	I_(TEXT("stone"),        TEXT("Fieldstone"),    TEXT("Plain, honest rock."),                              EItemKind::Material, 2,  TEXT("Miner"),   TEXT("#8D9096"));
	I_(TEXT("copper_ore"),   TEXT("Copper Ore"),    TEXT("Rusty veins of workable metal."),                   EItemKind::Material, 8,  TEXT("Miner"),   TEXT("#C77B46"));
	I_(TEXT("iron_ore"),     TEXT("Iron Ore"),      TEXT("Heavy and dependable."),                            EItemKind::Material, 12, TEXT("Miner"),   TEXT("#9AA3AB"));
	I_(TEXT("moonshard"),    TEXT("Moonshard"),     TEXT("Chipped from a Sleepers' monolith. Cold, faintly humming."), EItemKind::Material, 30, TEXT("Miner"), TEXT("#B7C4E8"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::None, false);
	I_(TEXT("copper_bar"),   TEXT("Copper Bar"),    TEXT("Smelted and ready for the forge."),                 EItemKind::Material, 18, TEXT("Smith"),   TEXT("#E08D57"));
	I_(TEXT("iron_bar"),     TEXT("Iron Bar"),      TEXT("The backbone of good tools."),                      EItemKind::Material, 26, TEXT("Smith"),   TEXT("#B8C0C8"));
	I_(TEXT("rope"),         TEXT("Braided Rope"),  TEXT("Three strands of Sunfiber, knotted by hand."),      EItemKind::Material, 10, TEXT("Tailor"),  TEXT("#D8C088"));
	I_(TEXT("cloth"),        TEXT("Woven Cloth"),   TEXT("Soft, sturdy fabric."),                             EItemKind::Material, 12, TEXT("Tailor"),  TEXT("#C9CDD4"));
	I_(TEXT("raw_fish"),     TEXT("Silverfin"),     TEXT("A common river fish. Tastes better cooked."),       EItemKind::Material, 6,  TEXT("Fisher"),  TEXT("#9FB8CC"));
	I_(TEXT("gildfish"),     TEXT("Gildfish"),      TEXT("A fat golden fish. Rare catch."),                   EItemKind::Material, 20, TEXT("Fisher"),  TEXT("#E8C35A"));

	// ---------------- Food / Consumables ----------------
	I_(TEXT("trail_snack"),  TEXT("Trail Snack"),   TEXT("Restores 25 HP to one creature."),    EItemKind::Food, 14, TEXT(""), TEXT("#D9A05B"), 25);
	I_(TEXT("hearty_stew"),  TEXT("Hearty Stew"),   TEXT("Restores 70 HP to one creature."),    EItemKind::Food, 40, TEXT(""), TEXT("#B3663B"), 70);
	I_(TEXT("spice_bun"),    TEXT("Spice Bun"),     TEXT("Restores 45 HP and cures status."),   EItemKind::Food, 30, TEXT(""), TEXT("#E0B14C"), 45, 1);
	I_(TEXT("salve"),        TEXT("Green Salve"),   TEXT("Restores 40 HP. Bitterleaf medicine."), EItemKind::Consumable, 26, TEXT(""), TEXT("#6FA86F"), 40);
	I_(TEXT("purge_draught"),TEXT("Purge Draught"), TEXT("Cures any status condition."),        EItemKind::Consumable, 24, TEXT(""), TEXT("#8FD0C8"), 0, 1);
	I_(TEXT("revive_tonic"), TEXT("Revive Tonic"),  TEXT("Revives a fainted creature to half HP."), EItemKind::Consumable, 90, TEXT(""), TEXT("#E8E3B0"), 999);

	// ---------------- Capture cores ----------------
	I_(TEXT("core_wood"), TEXT("Timber Core"),    TEXT("A carved capture core. Catch bonus x1."),    EItemKind::CaptureCore, 15,  TEXT(""), TEXT("#A0784C"), 0, 0, 1.f);
	I_(TEXT("core_iron"), TEXT("Ironbound Core"), TEXT("Reinforced capture core. Catch bonus x2."),  EItemKind::CaptureCore, 45,  TEXT(""), TEXT("#8E99A3"), 0, 0, 2.f);
	I_(TEXT("core_moon"), TEXT("Moonlit Core"),   TEXT("Inlaid with Moonshard. Catch bonus x3.5."),  EItemKind::CaptureCore, 120, TEXT(""), TEXT("#A9BCE8"), 0, 0, 3.5f);

	// ---------------- Trainer equipment ----------------
	I_(TEXT("forager_gloves"), TEXT("Forager Gloves"),  TEXT("Equipped: gathering yields +1."), EItemKind::Equipment, 60,  TEXT(""), TEXT("#B08968"), 0, 0, 1.f, TEXT("gather+1"), EPlayerWeaponKind::None, false);
	I_(TEXT("miner_pick"),     TEXT("Reinforced Pick"), TEXT("Equipped: mining yields +1."),    EItemKind::Equipment, 60,  TEXT(""), TEXT("#7F8C99"), 0, 0, 1.f, TEXT("mine+1"),   EPlayerWeaponKind::None, false);
	I_(TEXT("angler_rod"),     TEXT("Warded Rod"),      TEXT("Equipped: fishing yields +1."),   EItemKind::Equipment, 60,  TEXT(""), TEXT("#6C8EA4"), 0, 0, 1.f, TEXT("fish+1"),   EPlayerWeaponKind::None, false);
	I_(TEXT("lure_charm"),     TEXT("Lure Charm"),      TEXT("Equipped: wild encounters are 25% more frequent."), EItemKind::Equipment, 80, TEXT(""), TEXT("#D4A5C9"), 0, 0, 1.f, TEXT("lure"),   EPlayerWeaponKind::None, false);
	I_(TEXT("scout_band"),     TEXT("Scout Band"),      TEXT("Equipped: wild creatures appear 2 levels weaker."), EItemKind::Equipment, 100, TEXT(""), TEXT("#98B4D4"), 0, 0, 1.f, TEXT("scout"), EPlayerWeaponKind::None, false);

	// ---------------- Trainer melee weapons (overworld) ----------------
	I_(TEXT("wpn_club"),  TEXT("Oaken Club"),  TEXT("Trainer weapon: 8 dmg, slow. A honest branch with intent."),  EItemKind::Equipment, 30,  TEXT(""), TEXT("#8A5E33"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::Club);
	I_(TEXT("wpn_blade"), TEXT("Copper Blade"),TEXT("Trainer weapon: 14 dmg, medium. Smith-forged, keen at the edge."), EItemKind::Equipment, 90, TEXT(""), TEXT("#E08D57"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::Blade);
	I_(TEXT("wpn_spear"), TEXT("Longspear"),   TEXT("Trainer weapon: 11 dmg, long reach. Keeps the fangs at arm's length."), EItemKind::Equipment, 65, TEXT(""), TEXT("#A8B0B8"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::Spear);
	I_(TEXT("wpn_axe"),   TEXT("Vale Axe"),    TEXT("Trainer weapon: 20 dmg, heavy. Splits logs; makes do with hide."), EItemKind::Equipment, 150, TEXT(""), TEXT("#B08968"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::Axe);

	// ---------------- Creature gear ----------------
	I_(TEXT("gear_claw"),       TEXT("Bone Claw"),      TEXT("Creature weapon: +3 ATK."),                          EItemKind::Equipment, 35,  TEXT(""), TEXT("#E4DCC8"), 0, 0, 1.f, TEXT("atk+3"));
	I_(TEXT("gear_fang"),       TEXT("Ember Fang"),     TEXT("Creature weapon: +6 ATK, +1 SPD. Warm to the tooth."), EItemKind::Equipment, 75,  TEXT(""), TEXT("#E07040"), 0, 0, 1.f, TEXT("atk+6|spd+1"));
	I_(TEXT("gear_stormspine"), TEXT("Storm Spine"),    TEXT("Creature weapon: +9 ATK, +2 SPD. Forged around a charged filament."), EItemKind::Equipment, 150, TEXT(""), TEXT("#B8D8F0"), 0, 0, 1.f, TEXT("atk+9|spd+2"));
	I_(TEXT("gear_band"),       TEXT("Traveler's Band"),TEXT("Creature armor: +2 DEF, +4 HP."),                    EItemKind::Equipment, 30,  TEXT(""), TEXT("#C8A878"), 0, 0, 1.f, TEXT("def+2|hp+4"));
	I_(TEXT("gear_hide"),       TEXT("Ranger Hide"),    TEXT("Creature armor: +5 DEF, +8 HP."),                    EItemKind::Equipment, 70,  TEXT(""), TEXT("#8A9A5B"), 0, 0, 1.f, TEXT("def+5|hp+8"));
	I_(TEXT("gear_plate"),      TEXT("Vale Plate"),     TEXT("Creature armor: +9 DEF, +14 HP. Smith-forged shell."), EItemKind::Equipment, 140, TEXT(""), TEXT("#A8B0B8"), 0, 0, 1.f, TEXT("def+9|hp+14"));
	I_(TEXT("gear_charm"),      TEXT("Zephyr Charm"),   TEXT("Creature armor: +4 SPD. Light as a rumor."),         EItemKind::Equipment, 90,  TEXT(""), TEXT("#D8E8F8"), 0, 0, 1.f, TEXT("spd+4"));

	// ---------------- Key items ----------------
	I_(TEXT("lantern"),       TEXT("Sleeper's Lantern"),     TEXT("Its flame leans toward the Hollow. Story item."), EItemKind::KeyItem, 0, TEXT(""), TEXT("#F5E6A8"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::None, false);
	I_(TEXT("sigil_piece_a"), TEXT("Sigil Fragment: Root"),  TEXT("One third of the Verdant Sigil."), EItemKind::KeyItem, 0, TEXT(""), TEXT("#78C850"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::None, false);
	I_(TEXT("sigil_piece_b"), TEXT("Sigil Fragment: Tide"),  TEXT("One third of the Verdant Sigil."), EItemKind::KeyItem, 0, TEXT(""), TEXT("#6890F0"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::None, false);
	I_(TEXT("sigil_piece_c"), TEXT("Sigil Fragment: Ember"), TEXT("One third of the Verdant Sigil."), EItemKind::KeyItem, 0, TEXT(""), TEXT("#F08030"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::None, false);
	I_(TEXT("verdant_sigil"), TEXT("Verdant Sigil"),         TEXT("Complete. It answers the Hollow Sleepers."), EItemKind::KeyItem, 0, TEXT(""), TEXT("#9BE870"), 0, 0, 1.f, TEXT(""), EPlayerWeaponKind::None, false);

	// ---------------- Recipes ----------------
	R(TEXT("r_rope"),     TEXT("rope"),        1, EProfessionId::Tailor, 1,  6,  TEXT(""),            { {TEXT("fiber"), 3} });
	R(TEXT("r_cloth"),    TEXT("cloth"),       1, EProfessionId::Tailor, 2,  9,  TEXT(""),            { {TEXT("fiber"), 4} });
	R(TEXT("r_copper"),   TEXT("copper_bar"),  1, EProfessionId::Smith,  1,  8,  TEXT("forge"),       { {TEXT("copper_ore"), 2} });
	R(TEXT("r_iron"),     TEXT("iron_bar"),    1, EProfessionId::Smith,  3,  12, TEXT("forge"),       { {TEXT("iron_ore"), 2} });
	R(TEXT("r_snack"),    TEXT("trail_snack"), 2, EProfessionId::Chef,   1,  7,  TEXT("campfire"),    { {TEXT("fiber"), 1}, {TEXT("bitterleaf"), 1} });
	R(TEXT("r_stew"),     TEXT("hearty_stew"), 1, EProfessionId::Chef,   3,  15, TEXT("campfire"),    { {TEXT("raw_fish"), 1}, {TEXT("glowcap"), 1}, {TEXT("bitterleaf"), 1} });
	R(TEXT("r_bun"),      TEXT("spice_bun"),   1, EProfessionId::Chef,   2,  10, TEXT("campfire"),    { {TEXT("emberpepper"), 1}, {TEXT("fiber"), 2} });
	R(TEXT("r_salve"),    TEXT("salve"),       1, EProfessionId::Chef,   2,  9,  TEXT(""),            { {TEXT("bitterleaf"), 2}, {TEXT("glowcap"), 1} });
	R(TEXT("r_purge"),    TEXT("purge_draught"),1,EProfessionId::Chef,   3,  12, TEXT(""),            { {TEXT("bitterleaf"), 2}, {TEXT("emberpepper"), 1} });
	R(TEXT("r_revive"),   TEXT("revive_tonic"),1, EProfessionId::Chef,   5,  25, TEXT(""),            { {TEXT("gildfish"), 1}, {TEXT("glowcap"), 2}, {TEXT("bitterleaf"), 2} });
	R(TEXT("r_core_wood"),TEXT("core_wood"),   1, EProfessionId::Smith,  1,  10, TEXT("forge"),       { {TEXT("stone"), 2}, {TEXT("rope"), 1} });
	R(TEXT("r_core_iron"),TEXT("core_iron"),   1, EProfessionId::Smith,  3,  20, TEXT("forge"),       { {TEXT("iron_bar"), 1}, {TEXT("copper_bar"), 1}, {TEXT("rope"), 1} });
	R(TEXT("r_gloves"),   TEXT("forager_gloves"),1,EProfessionId::Tailor,3,  22, TEXT("workbench"),   { {TEXT("cloth"), 2}, {TEXT("rope"), 1}, {TEXT("copper_bar"), 1} });
	R(TEXT("r_pick"),     TEXT("miner_pick"),  1, EProfessionId::Smith,  4,  25, TEXT("forge"),       { {TEXT("iron_bar"), 2}, {TEXT("rope"), 1} });
	R(TEXT("r_rod"),      TEXT("angler_rod"),  1, EProfessionId::Tailor, 4,  25, TEXT("workbench"),   { {TEXT("rope"), 2}, {TEXT("iron_bar"), 1}, {TEXT("cloth"), 1} });
	R(TEXT("r_lure"),     TEXT("lure_charm"),  1, EProfessionId::Tailor, 5,  30, TEXT("workbench"),   { {TEXT("gildfish"), 1}, {TEXT("cloth"), 2}, {TEXT("glowcap"), 1} });
	R(TEXT("r_scout"),    TEXT("scout_band"),  1, EProfessionId::Tailor, 5,  30, TEXT("workbench"),   { {TEXT("cloth"), 2}, {TEXT("iron_bar"), 1}, {TEXT("moonshard"), 1} });

	// trainer weapon recipes
	R(TEXT("r_wpn_club"),  TEXT("wpn_club"),  1, EProfessionId::Forager, 1, 8,  TEXT(""),      { {TEXT("fiber"), 2}, {TEXT("stone"), 1} });
	R(TEXT("r_wpn_blade"), TEXT("wpn_blade"), 1, EProfessionId::Smith,   2, 20, TEXT("forge"),     { {TEXT("copper_bar"), 2}, {TEXT("rope"), 1} });
	R(TEXT("r_wpn_spear"), TEXT("wpn_spear"), 1, EProfessionId::Tailor,  2, 16, TEXT("workbench"), { {TEXT("rope"), 1}, {TEXT("copper_bar"), 1}, {TEXT("fiber"), 2} });
	R(TEXT("r_wpn_axe"),   TEXT("wpn_axe"),   1, EProfessionId::Smith,   5, 40, TEXT("forge"),     { {TEXT("iron_bar"), 2}, {TEXT("rope"), 1}, {TEXT("fiber"), 2} });

	// creature gear recipes
	R(TEXT("r_gear_claw"),       TEXT("gear_claw"),       1, EProfessionId::Smith,  1, 12, TEXT("forge"),     { {TEXT("copper_bar"), 1}, {TEXT("stone"), 2} });
	R(TEXT("r_gear_fang"),       TEXT("gear_fang"),       1, EProfessionId::Smith,  3, 22, TEXT("forge"),     { {TEXT("iron_bar"), 1}, {TEXT("emberpepper"), 2}, {TEXT("rope"), 1} });
	R(TEXT("r_gear_stormspine"), TEXT("gear_stormspine"), 1, EProfessionId::Smith,  6, 45, TEXT("forge"),     { {TEXT("iron_bar"), 2}, {TEXT("moonshard"), 1}, {TEXT("copper_bar"), 2} });
	R(TEXT("r_gear_band"),       TEXT("gear_band"),       1, EProfessionId::Tailor, 1, 10, TEXT("workbench"), { {TEXT("fiber"), 3}, {TEXT("rope"), 1} });
	R(TEXT("r_gear_hide"),       TEXT("gear_hide"),       1, EProfessionId::Tailor, 3, 20, TEXT("workbench"), { {TEXT("cloth"), 2}, {TEXT("rope"), 1}, {TEXT("copper_bar"), 1} });
	R(TEXT("r_gear_plate"),      TEXT("gear_plate"),      1, EProfessionId::Smith,  5, 40, TEXT("forge"),     { {TEXT("iron_bar"), 2}, {TEXT("cloth"), 1}, {TEXT("rope"), 1} });
	R(TEXT("r_gear_charm"),      TEXT("gear_charm"),      1, EProfessionId::Tailor, 5, 35, TEXT("workbench"), { {TEXT("cloth"), 1}, {TEXT("glowcap"), 2}, {TEXT("copper_bar"), 1} });
}

// ---------------------------------------------------------------- nodes
static void N(const TCHAR* Id, const TCHAR* Name, EProfessionId Prof, int32 ReqLvl, int32 Xp,
              std::initializer_list<const TCHAR*> Pool, const TCHAR* Zone, int32 BaseYield = 1)
{
	FNodeDef D;
	D.Id = Id; D.Name = Name; D.Profession = Prof; D.RequiredLevel = ReqLvl;
	D.XpReward = Xp; D.Zone = Zone; D.BaseYield = BaseYield;
	for (const TCHAR* It : Pool) D.ItemPool.Add(It);

	// weights: uniform 3 except last (rare) 1
	D.PoolWeights.Reset();
	for (int32 i = 0; i < D.ItemPool.Num(); ++i)
		D.PoolWeights.Add(i == D.ItemPool.Num() - 1 ? 1 : 3);

	FEmberwildDB::Nodes.Add(D.Id, D);
}

void FEmberwildDB::InitNodes()
{
	Nodes.Empty();

	// ---- Meadowfall ---- (Lv0 starter nodes: gatherable before any training)
	N(TEXT("bush_meadowgrass"), TEXT("Meadow Grass"),     EProfessionId::Forager, 0, 8,  { TEXT("fiber") },                                        TEXT("meadow"));
	N(TEXT("rock_pebble"),      TEXT("Pebble Scatter"),   EProfessionId::Miner,   0, 8,  { TEXT("stone") },                                        TEXT("meadow"));
	N(TEXT("spot_creek"),       TEXT("Creek Shallows"),   EProfessionId::Fisher,  0, 9,  { TEXT("raw_fish") },                                     TEXT("meadow"));
	N(TEXT("bush_bitterleaf"),  TEXT("Bitterleaf Bush"),  EProfessionId::Forager, 1, 8,  { TEXT("bitterleaf"), TEXT("bitterleaf"), TEXT("fiber") }, TEXT("meadow"));
	N(TEXT("bush_fiber"),       TEXT("Sunfiber Patch"),   EProfessionId::Forager, 1, 8,  { TEXT("fiber"), TEXT("fiber"), TEXT("bitterleaf") },      TEXT("meadow"));
	N(TEXT("bush_emberpepper"), TEXT("Emberpepper Plant"),EProfessionId::Forager, 2, 8,  { TEXT("emberpepper"), TEXT("emberpepper"), TEXT("bitterleaf") }, TEXT("meadow"));
	N(TEXT("stump_glowcap"),    TEXT("Glowcap Stump"),    EProfessionId::Forager, 3, 8,  { TEXT("glowcap"), TEXT("glowcap"), TEXT("fiber") },        TEXT("hollow"));
	N(TEXT("rock_stone"),       TEXT("Loose Scree"),      EProfessionId::Miner,   1, 8,  { TEXT("stone"), TEXT("stone"), TEXT("copper_ore") },      TEXT("meadow"));
	N(TEXT("rock_copper"),      TEXT("Copper Vein"),      EProfessionId::Miner,   2, 10, { TEXT("copper_ore"), TEXT("copper_ore"), TEXT("stone"), TEXT("sigil_piece_c") }, TEXT("ridge"));
	N(TEXT("rock_iron"),        TEXT("Iron Vein"),        EProfessionId::Miner,   4, 12, { TEXT("iron_ore"), TEXT("iron_ore"), TEXT("stone"), TEXT("sigil_piece_c") }, TEXT("ridge"));
	N(TEXT("rock_moon"),        TEXT("Hollow Monolith"),  EProfessionId::Miner,   6, 20, { TEXT("moonshard"), TEXT("stone"), TEXT("iron_ore") },   TEXT("hollow_deep"));
	N(TEXT("spot_river"),       TEXT("River Shoal"),      EProfessionId::Fisher,  1, 9,  { TEXT("raw_fish"), TEXT("raw_fish"), TEXT("raw_fish"), TEXT("gildfish") }, TEXT("meadow"));
	N(TEXT("spot_lake"),        TEXT("Mirrorlake Pier"),  EProfessionId::Fisher,  3, 10, { TEXT("raw_fish"), TEXT("gildfish"), TEXT("raw_fish"), TEXT("gildfish") }, TEXT("lake"));
}

// ---------------------------------------------------------------- lookups
const FMoveDef* FEmberwildDB::Move(const FString& Id)          { return Moves.Find(Id); }
const FSpeciesDef* FEmberwildDB::SpeciesDef(const FString& Id) { return Species.Find(Id); }
const FItemDef* FEmberwildDB::Item(const FString& Id)          { return Items.Find(Id); }
const FNodeDef* FEmberwildDB::Node(const FString& Id)          { return Nodes.Find(Id); }
const FQuestDef* FEmberwildDB::Quest(const FString& Id)        { return Quests.Find(Id); }
const FDialogueNode* FEmberwildDB::DialogueNode(const FString& Id) { return Dialogue.Find(Id); }
const FTrainerDef* FEmberwildDB::Trainer(const FString& Id)    { return Trainers.Find(Id); }
const FShopDef* FEmberwildDB::Shop(const FString& Id)          { return Shops.Find(Id); }
const FNpcDef* FEmberwildDB::Npc(const FString& Id)            { return Npcs.Find(Id); }
const FRecipe* FEmberwildDB::Recipe(const FString& Id)
{
	for (const FRecipe& R : Recipes) if (R.Id == Id) return &R;
	return nullptr;
}

FString FEmberwildDB::ItemName(const FString& Id)
{
	const FItemDef* D = Item(Id);
	return D ? D->Name : Id;
}

FString FEmberwildDB::SafeMove(const FString& Id)
{
	return Moves.Contains(Id) ? Id : TEXT("tackle");
}

bool FEmberwildDB::IsBaseForm(const FString& SpeciesId)
{
	return !EvolvedForms.Contains(SpeciesId);
}

TArray<FString> FEmberwildDB::WildPoolFor(const FString& Zone)
{
	TArray<FString> List;
	for (const TPair<FString, FSpeciesDef>& KV : Species)
	{
		if ((KV.Value.HabitatZone == Zone || (Zone == TEXT("meadow") && KV.Value.HabitatZone == TEXT("lake")))
			&& IsBaseForm(KV.Key))
		{
			List.Add(KV.Key);
		}
	}
	return List;
}

TArray<FWildLootDrop> FEmberwildDB::RollWildLoot(const FString& SpeciesId, int32 Level)
{
	TArray<FWildLootDrop> Drops;
	const FSpeciesDef* Sp = SpeciesDef(SpeciesId);
	if (!Sp) return Drops;

	// specials first
	if (SpeciesId == TEXT("vaultmole")) (void)Drops.Emplace_GetRef(TEXT("copper_ore"), 1);
	if (SpeciesId == TEXT("cragma"))
	{
		(void)Drops.Emplace_GetRef(TEXT("copper_ore"), 1);
		if (FMath::FRand() < 0.35f) (void)Drops.Emplace_GetRef(TEXT("iron_ore"), 1);
	}
	if ((SpeciesId == TEXT("sleeperling") || SpeciesId == TEXT("frostwisp") || SpeciesId == TEXT("gildra")) && FMath::FRand() < 0.6f)
		(void)Drops.Emplace_GetRef(TEXT("moonshard"), 1);

	// element-themed material, quantity scaling with level
	const TCHAR* Mat = TEXT("fiber");
	switch (Sp->Element)
	{
	case EElement::Verdant: Mat = TEXT("fiber");       break;
	case EElement::Tide:    Mat = TEXT("raw_fish");    break;
	case EElement::Ember:   Mat = TEXT("emberpepper");break;
	case EElement::Stone:   Mat = TEXT("stone");       break;
	case EElement::Toxic:
	case EElement::Shade:   Mat = TEXT("bitterleaf");  break;
	case EElement::Mind:
	case EElement::Frost:   Mat = TEXT("glowcap");     break;
	default:                Mat = TEXT("fiber");       break; // Gale, Beast, Normal
	}
	const int32 N = 1 + Level / 12;
	(void)Drops.Emplace_GetRef(Mat, N);
	if (FMath::FRand() < 0.25f) (void)Drops.Emplace_GetRef(Mat, 1); // bonus handful
	return Drops;
}

FString FEmberwildDB::RollNodeItem(const FNodeDef& NodeDef)
{
	int32 Total = 0;
	for (int32 W : NodeDef.PoolWeights) Total += W;
	int32 Roll = FMath::RandRange(0, FMath::Max(0, Total - 1));
	for (int32 i = 0; i < NodeDef.ItemPool.Num(); ++i)
	{
		Roll -= NodeDef.PoolWeights[i];
		if (Roll < 0) return NodeDef.ItemPool[i];
	}
	return NodeDef.ItemPool.Num() > 0 ? NodeDef.ItemPool[0] : FString();
}
