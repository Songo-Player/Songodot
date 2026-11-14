// libsongo_engine, loaded at runtime.
//
// The engine (FFmpeg and mbedTLS inside, built by SongoBGPlayer) is a shared
// library of its own rather than part of Godot: Godot carries its own
// mbedTLS, and the songo_bgplayer daemon loads the same library, so FFmpeg
// ships once. Only in-process playback (Android, desktop debugging) needs it
// in Godot's process, so it's dlopen'ed then, and a Godot without it still
// runs and plays through the daemon.

#ifndef SONGO_ENGINE_LIBRARY_H
#define SONGO_ENGINE_LIBRARY_H

#include "protocol.h"
#include "songo_engine.h"

#include <string>
#include <vector>

namespace songo {

// The C API from songo_engine.h, as function pointers
struct EngineApi {
	int (*api_version)(void);
	SongoEngine *(*acquire_shared)(const SongoEngineConfig *config);
	void (*release_shared)(SongoEngine *engine);

	void (*set_queue)(SongoEngine *engine, const char *const *paths, int count, int start_index, double start_position, int autoplay);
	void (*replace_queue)(SongoEngine *engine, const char *const *paths, int count, int current_index);
	void (*play_index)(SongoEngine *engine, int index, double position);
	void (*resume)(SongoEngine *engine);
	void (*pause)(SongoEngine *engine);
	void (*stop)(SongoEngine *engine);
	void (*seek)(SongoEngine *engine, double seconds);
	void (*next)(SongoEngine *engine);
	void (*previous)(SongoEngine *engine);

	void (*set_volume)(SongoEngine *engine, float volume);
	void (*set_eq)(SongoEngine *engine, int enabled, const float *gains_db, int count);
	void (*set_blend)(SongoEngine *engine, double seconds);
	void (*set_repeat_one)(SongoEngine *engine, int enabled);
	void (*set_loop_queue)(SongoEngine *engine, int enabled);
	void (*set_network_timeout)(SongoEngine *engine, double seconds);
	void (*set_user_agent)(SongoEngine *engine, const char *user_agent);
	void (*set_skip_failed)(SongoEngine *engine, int enabled);
	void (*set_latency)(SongoEngine *engine, int milliseconds);
	void (*set_stream_buffer)(SongoEngine *engine, double seconds);

	SongoSubscription *(*subscribe)(SongoEngine *engine);
	void (*subscription_close)(SongoSubscription *subscription);
	int (*subscription_poll)(SongoSubscription *subscription, SongoEvent *r_event);

	void *(*shared)(SongoEngine *engine, size_t *r_size);
};

// Loads the engine from the first of `p_candidates` that opens (a path, or a
// bare file name for the dynamic linker to find, eg: from an APK's native
// libraries). Loaded once per process and never unloaded, since the engine's
// threads run its code. Null with `r_error` set if none would load.
const EngineApi *load_engine_library(const std::vector<std::string> &p_candidates, std::string &r_error);

// Carries out a command message on an in-process engine. False if it isn't one.
bool dispatch_command(const EngineApi &api, SongoEngine *engine, const Message &message);

} // namespace songo

#endif // SONGO_ENGINE_LIBRARY_H
