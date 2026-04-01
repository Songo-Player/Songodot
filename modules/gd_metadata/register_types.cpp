#include "register_types.h"
#include "audio_metadata.h"
#include "core/object/class_db.h"

#include "core/config/engine.h"
#include "core/os/os.h"

void initialize_gd_metadata_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    print_line("✅ [gd_metadata] Initializing AudioMetadata class...");
    ClassDB::register_class<AudioMetadata>();
}

void uninitialize_gd_metadata_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
        return;

    print_line("🧹 [gd_metadata] Uninitialized.");
}
