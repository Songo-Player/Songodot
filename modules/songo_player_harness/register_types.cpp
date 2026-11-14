#include "register_types.h"

#include "songo_player_harness.h"

void initialize_songo_player_harness_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(SongoPlayerHarness);
}

void uninitialize_songo_player_harness_module(ModuleInitializationLevel p_level) {
}
