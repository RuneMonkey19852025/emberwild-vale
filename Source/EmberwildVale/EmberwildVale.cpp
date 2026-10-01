// Emberwild Vale — module bootstrap.
// Port of the Unity original's entry point wiring; all gameplay bootstraps
// live in engine subsystems (EmberwildGame.h), so the default module impl
// (load → StartupModule → ShutdownModule) is all the engine needs here.
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, EmberwildVale, "EmberwildVale");
