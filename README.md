# Emberwild Vale — Unreal Engine 5 edition

A creature-taming RPG with professions, crafting, and a 3-act story — a full C++
port of the Unity original in `../../UnityProjects/PokeProfessions` to
**Unreal Engine 5.8**, with all visuals, audio, and UI generated from code.

- 24 procedurally-built creatures across 12 elements with a full effectiveness chart
- 6 professions (Forager, Miner, Fisher, Chef, Smith, Tailor) with XP, levels, yields
- Crafting: capture cores, healing food, profession gear, trainer weapons, creature gear
- Turn-based battles: FIGHT/BAG/SWITCH/RUN, statuses, buffs, draining moves, STAB
- Weapon mastery: gear classes level from KOs and unlock **mastery techniques**
- Trainer melee combat in the overworld (swing weapons at wild creatures)
- The Lucky Snorlax slot machines with a shared progressive jackpot and hot-hand streaks
- 8 quests across 3 acts, 13 NPCs, trainer battles, shops, JSON save/load with autosave

## Requirements

- **Unreal Engine 5.8** for Linux (pre-compiled zip from
  <https://www.unrealengine.com/linux> after logging in with an Epic account,
  or built from source). The `.uproject` pins `"EngineAssociation": "5.8"`.
- ~75 GB disk for the extracted engine, plus ~2 GB for the project's intermediates.

## Build & run

```bash
# 1. Extract the engine zip you downloaded (anywhere, e.g. ~/Unreal)
mkdir -p ~/Unreal && cd ~/Unreal
unzip ~/Downloads/Linux_Unreal_Engine_5.*.zip

# 2. Build the editor target (first build: ~5-10 min on 8 cores)
cd UnrealProjects/EmberwildVale
~/Unreal/Engine/Build/BatchFiles/Linux/Build.sh EmberwildValeEditor Linux Development \
    -project="$PWD/EmberwildVale.uproject" -WaitMutex

# 3. Run in the editor
~/Unreal/Engine/Binaries/Linux/UnrealEditor "$PWD/EmberwildVale.uproject"

# Headless smoke test: boots the real game for 90s with no renderer, exits via timeout
# (healthy run: exit 124, zero "Fatal" lines in Saved/Logs/EmberwildVale.log,
#  and a fresh Saved/SaveGames/emberwild_save_v1.json after the 25s autosave)
timeout 90 ~/Unreal/Engine/Binaries/Linux/UnrealEditor \
    "$PWD/EmberwildVale.uproject" -game -nullrhi -nosplash -log

# Autopilot playtest: drives the quest loops through the real UI key handlers.
# Needs EMBERWILD_AUTOPILOT=1 uncommented in Source/EmberwildVale/EmberwildVale.Build.cs
# and a rebuild. It is wall-clock scheduled across 35 stages ending at ~t+2510s,
# so it needs ~43 minutes — `timeout 90` above will NOT complete it.
# The autopilot calls StartNewGame() at boot, so it always plays from a fresh save
# and any previous autosave in Saved/SaveGames/ is discarded.
# (healthy run: "-- stage: done" in the log, zero "Fatal" lines, PASS/FAIL tallied)
timeout 2800 ~/Unreal/Engine/Binaries/Linux/UnrealEditor \
    "$PWD/EmberwildVale.uproject" -game -nullrhi -nosound -nosplash -log
grep -E '\[PASS\]|\[FAIL\]|stage: done' Saved/Logs/EmberwildVale.log | tail -40

# ...or build & run the standalone game directly without the editor
~/Unreal/Engine/Build/BatchFiles/Linux/Build.sh EmberwildVale Linux Development \
    -project="$PWD/EmberwildVale.uproject" -WaitMutex
./Binaries/Linux/EmberwildVale
```

## Build & run on Windows

Every script committed so far targets Linux (`install-engine.sh`, `package-game.sh`,
`Build/Linux`, `Dist/Linux`). On Windows use the standard Unreal/Visual Studio
toolchain, or the parity scripts added alongside this section:

```powershell
# One-shot: installs Git + Git LFS + Visual Studio (UE 5.8 workloads) + the Epic
# Games Launcher, pulls the LFS content, generates project files and builds.
powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1

# Build (editor by default; "game" for the standalone, "both" for both)
.\build-game.cmd editor
.\build-game.cmd game

# Package a self-contained Windows build -> Dist\Windows\EmberwildVale\
.\package-game.cmd
```

Set `EMBERWILD_ENGINE` if the engine is not in the default launcher folder, e.g.
`set EMBERWILD_ENGINE=D:\Epic\UE_5.8`.

### What has to be installed

| Component | Version | Notes |
|---|---|---|
| Unreal Engine | **5.8** | Epic Games Launcher -> Library -> 5.8 (~75 GB + DDC). The `.uproject` pins `EngineAssociation: 5.8`. |
| Visual Studio | **2026 (18.0+)** or **2022 (17.14+)** | 2026 is what Epic recommends for 5.8. |
| MSVC toolset | 14.38 minimum, 14.50 recommended | via the "Game development with C++" workload |
| Windows SDK | 10.0.26100 (minimum 10.0.22621.0) | |
| .NET SDK | 10.0 | used by UnrealBuildTool |
| Git + Git LFS | any recent | the repo tracks `.pak`/`.ucas`/`.utoc`/`.ushaderbytecode` through LFS |

**Visual Studio workloads/components** (all added by `setup-windows.ps1`):
`Desktop development with C++`, `Game development with C++`,
`.NET desktop development`, `.NET Multi-platform App UI development`, plus the
`MSVC v143` toolset, `C++ profiling tools`, `C++ AddressSanitizer` and
`Windows 11 SDK 10.0.26100`. The Epic Games Launcher (which the VS
"Unreal Engine installer" component would install) is installed separately.

> **Git LFS matters.** `Content/ShaderArchive-*.ushaderbytecode` and the cooked
> `.pak`/`.ucas`/`.utoc` files are stored as LFS pointers. Run `git lfs install`
> once and `git lfs pull` after cloning, or the project opens with missing content.

Manual equivalent of what `build-game.cmd` does:

```powershell
& "$env:ProgramFiles\Epic Games\UE_5.8\Engine\Build\BatchFiles\GenerateProjectFiles.bat" -project="$PWD\EmberwildVale.uproject" -game
& "$env:ProgramFiles\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" EmberwildValeEditor Win64 Development -project="$PWD\EmberwildVale.uproject" -WaitMutex
```

Then open `EmberwildVale.sln` (or double-click `EmberwildVale.uproject`) and press Play.

## Controls

| Key | Action |
|-----|--------|
| WASD / arrows | Move (camera-relative) |
| Mouse wheel | Zoom |
| E | Interact / advance dialogue / confirm |
| Space / Left click | Swing weapon (overworld melee) |
| Q | Weapons panel |
| Y / N | Answer dialogue choices |
| I | Bag |
| G | Gear & professions |
| J | Journal (quests, professions, bestiary) |
| C | Creatures (party, equip, release) |
| F5 | Save (autosaves every 25s) |
| Esc | Close top panel |
| In battle | Arrows move cursor · E/Enter/Space confirm · Backspace back |

## The loop

1. Talk to **Brann** (E) → gather fiber + stone at the Lv0 nodes by the fences
2. Craft **rope** (Tailor) and a **Timber Core** at the forge
3. Weaken a wild creature in battle → **BAG → throw the core** → it joins your party
4. Swing at roaming creatures with **Space/click** — KOs drop loot and coins,
   buy/craft real trainer weapons (Q panel)
5. Follow the quests (J panel): Tobin → the ridge → three sigil fragments →
   the Hollow → the Sleeper, and receive **Gildra**
6. Feed the **Lucky Snorlax** (two machines in Emberpost); triple stars take the pot

## Project layout

```
Source/EmberwildVale/
  EmberwildData.h        all UENUM/USTRUCT defs: elements, items, species, quests, dialogue
  EmberwildContent.cpp   content tables: moves, 24 species, items, recipes, nodes, type chart
  EmberwildStory.cpp     NPCs, quests, dialogue trees, trainers, shops
  EmberwildState.h/.cpp  FCreature stats, FGameStateSave, JSON save/load, toasts
  EmberwildSystems.h/.cpp professions, mastery, quests, factory, combat, dialogue, battle engine
  EmberwildWorld.h/.cpp  heightfield terrain, Emberpost town, interactables, player pawn, game mode
  EmberwildGame.h/.cpp   UGameInstanceSubsystem orchestrator + world spawn helpers
  EmberwildUI.h/.cpp     Slate UI router: title, HUD, dialogue, battle, crafting, shop,
                         bag, party, gear, weapons, journal, slots, toasts
  EmberwildAudio.h/.cpp  procedural SFX: synthesized PCM one-shots (sweeps + noise)
  EmberwildAutopilot.cpp compile-gated headless playtest (see Build.cs to enable)
Config/                  DefaultEngine.ini (game mode), DefaultInput.ini (legacy bindings)
```

## Port notes

- The game plane is **x/z with height on z** (Unity convention); everything maps to
  UE via `(x, z, h) → (x, -z, h)` at the spawn/build boundary only.
- All tuning constants (catch formulas, XP curves, mastery bonuses, slot weights,
  dialogue, stat math) are unchanged from the original — the two games play identically.
- Content is plain data: extend `EmberwildContent.cpp` / `EmberwildStory.cpp` to add
  creatures, items, recipes, quests, or dialogue.
- The original's scene bootstrap, EditorSceneBootstrap, and PlayerPrefs save are
  replaced by UE's `StartPlay` boot + a JSON save at
  `Saved/SaveGames/emberwild_save_v1.json`.

## Known gaps vs the Unity original

- Creature bodies are simplified chibi silhouettes (egg body + shape-specific
  ears/fins/crest) instead of the original's 7-shape procedural rigs.
- Dialogue/battle/quest UI is a single keyboard-driven Slate overlay rather than
  mouse-driven panels.
- No creature follower model walking behind you (gifted creatures join directly).
