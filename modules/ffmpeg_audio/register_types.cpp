#include "register_types.h"

#include "core/object/class_db.h"
#include "ffmpeg_audio.h"

void initialize_ffmpeg_audio_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_class<FFMPEGAudio>();
}

void uninitialize_ffmpeg_audio_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}