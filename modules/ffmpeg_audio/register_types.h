#ifndef FFMPEG_AUDIO_REGISTER_TYPES_H
#define FFMPEG_AUDIO_REGISTER_TYPES_H

#include "modules/register_module_types.h"

void initialize_ffmpeg_audio_module(ModuleInitializationLevel p_level);
void uninitialize_ffmpeg_audio_module(ModuleInitializationLevel p_level);

#endif