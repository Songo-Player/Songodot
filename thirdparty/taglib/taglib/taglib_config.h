/* taglib_config.h. Hand written for Godot's modules/gdtaglib, replacing the
 * CMake generated file from taglib_config.h.cmake.
 *
 * Only the formats Songo-5 scans for are built (see modules/gdtaglib/SCsub):
 * MP3/AAC (MPEG), FLAC, Ogg Vorbis/Opus, MP4/M4A, ASF/WMA and WAV/AIFF.
 * APE stays enabled because the MPEG reader uses it for APE tags on MP3s.
 * Keep this list in sync with the source directories SCsub compiles. */

#ifndef TAGLIB_TAGLIB_CONFIG_H
#define TAGLIB_TAGLIB_CONFIG_H

#define TAGLIB_WITH_APE 1
#define TAGLIB_WITH_ASF 1
/* #undef TAGLIB_WITH_DSF */
/* #undef TAGLIB_WITH_MOD */
#define TAGLIB_WITH_MP4 1
#define TAGLIB_WITH_RIFF 1
/* #undef TAGLIB_WITH_SHORTEN */
/* #undef TAGLIB_WITH_TRUEAUDIO */
#define TAGLIB_WITH_VORBIS 1

#endif
