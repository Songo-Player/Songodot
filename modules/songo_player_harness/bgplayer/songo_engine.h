// Copied from SongoBGPlayer's engine/include/songo_engine.h. Keep in sync: the module checks
// SONGO_ENGINE_API_VERSION / SHARED_VERSION against the engine it loads.

// libsongo_engine: Songo's music engine, independent of Godot.
//
// Decodes with FFmpeg, walks the play queue (looping, repeat-one, blending
// between songs), applies Songo's EQ and volume, and plays through an output
// backend on its own thread. Hosts drive it through this C API:
//   - songo_bgplayer, the Linux daemon that outlives Songo
//   - Songodot's SongoPlayerHarness module, in-process (Android, desktop
//     debugging), which dlopens this library
//   - the Android plugin's playback service, through JNI
//
// Every command returns straight away; the engine thread applies them in the
// order they were sent. What the engine does in return arrives as events on a
// subscription, and continuously changing state (position, spectrum,
// waveform) is published to shared memory, laid out in songo_shared.h.

#ifndef SONGO_ENGINE_H
#define SONGO_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#define SONGO_API __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

#define SONGO_ENGINE_API_VERSION 2

typedef struct SongoEngine SongoEngine;
typedef struct SongoSubscription SongoSubscription;

typedef enum {
	SONGO_STATE_STOPPED = 0,
	// Opening a song, which can take seconds for network streams
	SONGO_STATE_LOADING = 1,
	SONGO_STATE_PLAYING = 2,
	SONGO_STATE_PAUSED = 3,
} SongoState;

typedef struct {
	// "alsa", or "null" to pace playback in real time without any audio
	// device (tests, headless debugging). NULL for the platform default.
	const char *backend;
	// Backend specific device, NULL for its default (ALSA's "default")
	const char *device;
	// 0 for 44100, which skips resampling for most music
	int sample_rate;
	// How far ahead of the speaker the engine writes, 0 for 200ms. Can be
	// changed later with songo_engine_set_latency.
	int latency_ms;
} SongoEngineConfig;

typedef enum {
	// index: the new SongoState
	SONGO_EVENT_STATE = 0,
	// A song started playing. index: its queue index, value: duration in
	// seconds (<= 0 for live streams), flags: SONGO_TRACK_*, text: its path
	SONGO_EVENT_TRACK = 1,
	// Icecast/Shoutcast "now playing" changed. text: the title, "" when cleared
	SONGO_EVENT_TITLE = 2,
	// index: the queue index it's about (-1 if none), text: the message
	SONGO_EVENT_ERROR = 3,
	// The queue was set or replaced. index: the current index, paths: the queue
	SONGO_EVENT_QUEUE = 4,
	// Playback ran off the end of the queue (looping off)
	SONGO_EVENT_QUEUE_END = 5,
} SongoEventType;

#define SONGO_TRACK_LIVE 1
#define SONGO_TRACK_SEEKABLE 2

typedef struct {
	int type;
	int index;
	int flags;
	double value;
	// Valid until the next poll on the same subscription, or its close
	const char *text;
	const char *const *paths;
	int path_count;
} SongoEvent;

SONGO_API int songo_engine_api_version(void);

// NULL config for all defaults. Returns NULL if the backend is unknown.
SONGO_API SongoEngine *songo_engine_create(const SongoEngineConfig *config);
SONGO_API void songo_engine_destroy(SongoEngine *engine);

// One engine per process for hosts that share it (the Android service and
// SongoPlayerHarness). The first acquire creates it from `config`, later ones
// get the same engine and ignore theirs. Destroyed by the last release.
SONGO_API SongoEngine *songo_engine_acquire_shared(const SongoEngineConfig *config);
SONGO_API void songo_engine_release_shared(SongoEngine *engine);

// ---- Commands ----

// Replaces the queue and starts at `start_index`, `start_position` seconds
// in. With autoplay off it only cues the song up, stopped.
SONGO_API void songo_engine_set_queue(SongoEngine *engine, const char *const *paths, int count,
		int start_index, double start_position, int autoplay);
// Replaces the queue without interrupting the current song, which is at
// `current_index` in the new one (reordering, shuffling, appending).
SONGO_API void songo_engine_replace_queue(SongoEngine *engine, const char *const *paths, int count,
		int current_index);
SONGO_API void songo_engine_play_index(SongoEngine *engine, int index, double position);
// Resumes when paused, starts the current song when stopped
SONGO_API void songo_engine_resume(SongoEngine *engine);
SONGO_API void songo_engine_pause(SongoEngine *engine);
// Stops and lets go of the audio device. The queue stays.
SONGO_API void songo_engine_stop(SongoEngine *engine);
SONGO_API void songo_engine_seek(SongoEngine *engine, double seconds);
// Both restart the current song instead while repeating one
SONGO_API void songo_engine_next(SongoEngine *engine);
// Restarts the current song, or goes back one within its first 3 seconds
SONGO_API void songo_engine_previous(SongoEngine *engine);

// Linear gain, 0 to 1
SONGO_API void songo_engine_set_volume(SongoEngine *engine, float volume);
// Songo's EQ: 10 bands (31 Hz to 16 kHz), gains in dB per band. Off and all bands at 0 dB sound the same
SONGO_API void songo_engine_set_eq(SongoEngine *engine, int enabled, const float *gains_db, int count);
// The EQ's level after its bands, only while it's on. Auto preamp lowers it by
// the largest boost the bands make so boosts don't clip; output_gain_db is added
// on top. Starts as auto preamp on, 0 dB.
SONGO_API void songo_engine_set_eq_output(SongoEngine *engine, int auto_preamp, float output_gain_db);
// Crossfade length between songs, 0 plays them back to back
SONGO_API void songo_engine_set_blend(SongoEngine *engine, double seconds);
SONGO_API void songo_engine_set_repeat_one(SongoEngine *engine, int enabled);
// Wrap from the end of the queue to the start instead of stopping
SONGO_API void songo_engine_set_loop_queue(SongoEngine *engine, int enabled);
SONGO_API void songo_engine_set_network_timeout(SongoEngine *engine, double seconds);
SONGO_API void songo_engine_set_user_agent(SongoEngine *engine, const char *user_agent);
// On (the default), songs that won't open are skipped and a stream that dies
// moves on to the next song, as the background player always has. Off, both
// report an error and stop, leaving it to the host (Songo's WebRadio failover).
SONGO_API void songo_engine_set_skip_failed(SongoEngine *engine, int enabled);
// The output buffer: how far ahead of the speaker the engine writes (20 to
// 2000ms, 200 to start with). Bigger rides out a slow device better, smaller
// makes volume and EQ changes heard sooner. Changing it while playing
// reopens the output, a short blip. Android caps it at what the device allows.
SONGO_API void songo_engine_set_latency(SongoEngine *engine, int milliseconds);
// How far network streams decode ahead, on a thread of their own (0 to 30s,
// 1s to start with), so a stalled connection doesn't stall the music.
// Applies to streams opened after it. 0 reads them like local files.
SONGO_API void songo_engine_set_stream_buffer(SongoEngine *engine, double seconds);

// ---- Events ----

// Each subscription gets every event from when it was made, starting with a
// snapshot of the current queue, song, stream title and state.
SONGO_API SongoSubscription *songo_engine_subscribe(SongoEngine *engine);
SONGO_API void songo_subscription_close(SongoSubscription *subscription);
// 1 and fills `r_event` if one was waiting, 0 if not
SONGO_API int songo_subscription_poll(SongoSubscription *subscription, SongoEvent *r_event);
// Readable while events are waiting, for poll(). -1 where unsupported.
SONGO_API int songo_subscription_fd(SongoSubscription *subscription);

// ---- Shared memory ----

// The songo::Shared block from songo_shared.h. Readers write only its
// reader_heartbeat_ns.
SONGO_API void *songo_engine_shared(SongoEngine *engine, size_t *r_size);
// The memfd behind it, for handing to another process. -1 when the block is
// plain process memory.
SONGO_API int songo_engine_shared_fd(SongoEngine *engine);

#ifdef __cplusplus
}
#endif

#endif // SONGO_ENGINE_H
