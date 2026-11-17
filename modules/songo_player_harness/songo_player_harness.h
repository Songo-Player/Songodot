#ifndef SONGO_PLAYER_HARNESS_H
#define SONGO_PLAYER_HARNESS_H

#include "transport.h"

#include "scene/main/node.h"

#include <memory>
#include <string>
#include <vector>

// Songo's player, as a node. The music itself runs in libsongo_engine
// (SongoBGPlayer), outside Godot's audio, either in this process or in the
// songo_bgplayer daemon (see transport.h). Commands go to the engine and
// return straight away; what happens arrives as signals, and the getters read
// the engine's live state from shared memory.
//
// The engine library and the daemon are found, unless their paths are set,
// next to the Godot executable (exports) and then in the project's
// addons/songo_player_harness/bin (running a project from its folder).
class SongoPlayerHarness : public Node {
	GDCLASS(SongoPlayerHarness, Node)

public:
	enum Transport {
		// The daemon on Linux, in-process on Android
		TRANSPORT_AUTO,
		TRANSPORT_IN_PROCESS,
		TRANSPORT_DAEMON,
	};

	enum State {
		STATE_STOPPED,
		STATE_LOADING,
		STATE_PLAYING,
		STATE_PAUSED,
	};

	// Same values as AudioEffectSpectrumAnalyzerInstance's
	enum MagnitudeMode {
		MAGNITUDE_AVERAGE,
		MAGNITUDE_MAX,
	};

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	std::unique_ptr<songo::Transport> transport;
	bool transport_failed = false;
	Transport transport_mode = TRANSPORT_AUTO;
	String socket_path;
	String daemon_path;
	String engine_library_path;
	String output_backend;
	String output_device;
	bool daemon_exits_with_app = false;
	String daemon_exit_hook;

	// Used until the engine's status is readable
	float volume = 1.0f;
	double blend_time = 0.0;
	bool repeat_one = false;
	bool loop_queue = false;
	bool eq_enabled = false;
	PackedFloat32Array eq_band_gains;
	bool eq_auto_preamp = true;
	float eq_output_gain_db = 0.0f;
	double network_timeout = 10.0;
	String user_agent;
	bool skip_failed = true;
	int output_latency_ms = 200;
	double stream_buffer_length = 1.0;

	// From the engine's events
	int state = STATE_STOPPED;
	int current_index = -1;
	String current_path;
	String stream_title;
	PackedStringArray queue;

	// The spectrum frame heard this process frame, read once per frame
	uint64_t spectrum_process_frame = UINT64_MAX;
	bool spectrum_valid = false;
	float spectrum[2][songo::FFT_SIZE];

	bool _ensure_transport();
	void _send(const songo::Message &p_message, bool p_start_engine = true);
	// Settings don't start the daemon, they wait for it
	void _send_setting(const songo::Message &p_message) { _send(p_message, false); }
	void _poll();
	void _handle(const songo::Message &p_message);
	bool _read_status(songo::Status &r_status) const;
	void _refresh_spectrum();
	// Places to look for the engine library and the daemon, see above
	Vector<String> _search_dirs() const;
	std::vector<std::string> _engine_library_candidates() const;
	String _default_daemon_path() const;

public:
	SongoPlayerHarness();
	~SongoPlayerHarness();

	// ---- Playback ----
	void play(const String &p_path, double p_seek_time = 0.0);
	void set_queue(const PackedStringArray &p_paths, int p_start_index = 0, double p_start_position = 0.0, bool p_autoplay = true);
	void replace_queue(const PackedStringArray &p_paths, int p_current_index);
	void play_index(int p_index, double p_position = 0.0);
	void resume();
	void pause();
	void stop();
	void seek(double p_seconds);
	void next();
	void previous();

	// ---- State ----
	int get_state() const;
	// Also true while a song is opening, like FFMPEGPlayer's
	bool is_playing() const;
	bool is_paused() const;
	bool is_live() const;
	bool is_seekable() const;
	double get_playback_position() const;
	double get_duration() const;
	int get_current_index() const;
	String get_current_path() const { return current_path; }
	PackedStringArray get_queue() const { return queue; }
	String get_stream_title() const { return stream_title; }
	bool is_engine_connected() const;

	// ---- Visualizers ----
	// What AudioEffectSpectrumAnalyzerInstance's would return for the audio
	// coming out of the speaker right now
	Vector2 get_magnitude_for_frequency_range(float p_from_hz, float p_to_hz, MagnitudeMode p_mode = MAGNITUDE_MAX);
	// The last `p_frame_count` stereo frames heard, oldest first
	PackedVector2Array get_waveform(int p_frame_count = 1024);

	// ---- Settings ----
	void set_volume(float p_volume);
	float get_volume() const;
	void set_blend_time(double p_seconds);
	double get_blend_time() const;
	void set_repeat_one(bool p_enabled);
	bool get_repeat_one() const;
	void set_loop_queue(bool p_enabled);
	bool get_loop_queue() const;
	void set_eq_enabled(bool p_enabled);
	bool get_eq_enabled() const;
	void set_eq_band_gains(const PackedFloat32Array &p_gains_db);
	PackedFloat32Array get_eq_band_gains() const;
	void set_eq_auto_preamp(bool p_enabled);
	bool get_eq_auto_preamp() const;
	void set_eq_output_gain_db(float p_gain_db);
	float get_eq_output_gain_db() const;
	void set_network_timeout(double p_seconds);
	double get_network_timeout() const;
	void set_user_agent(const String &p_user_agent);
	String get_user_agent() const;
	void set_skip_failed(bool p_enabled);
	bool get_skip_failed() const { return skip_failed; }
	void set_output_latency_ms(int p_milliseconds);
	int get_output_latency_ms() const { return output_latency_ms; }
	void set_stream_buffer_length(double p_seconds);
	double get_stream_buffer_length() const { return stream_buffer_length; }

	// ---- Where the engine runs (read when it's first needed) ----
	void set_transport(Transport p_transport) { transport_mode = p_transport; }
	Transport get_transport() const { return transport_mode; }
	void set_socket_path(const String &p_path) { socket_path = p_path; }
	String get_socket_path() const { return socket_path; }
	void set_daemon_path(const String &p_path) { daemon_path = p_path; }
	String get_daemon_path() const { return daemon_path; }
	// libsongo_engine for in-process playback, empty to search for it
	void set_engine_library_path(const String &p_path) { engine_library_path = p_path; }
	String get_engine_library_path() const { return engine_library_path; }
	void set_output_backend(const String &p_backend) { output_backend = p_backend; }
	String get_output_backend() const { return output_backend; }
	void set_output_device(const String &p_device) { output_device = p_device; }
	String get_output_device() const { return output_device; }
	// Can change while connected, eg: off just before quitting with the music on
	void set_daemon_exits_with_app(bool p_enabled);
	// A shell command the daemon runs as it quits, eg: undoing CFW tweaks
	// made for music playing on after Songo exits
	void set_daemon_exit_hook(const String &p_command);
	String get_daemon_exit_hook() const { return daemon_exit_hook; }
	bool get_daemon_exits_with_app() const { return daemon_exits_with_app; }
};

VARIANT_ENUM_CAST(SongoPlayerHarness::Transport);
VARIANT_ENUM_CAST(SongoPlayerHarness::State);
VARIANT_ENUM_CAST(SongoPlayerHarness::MagnitudeMode);

#endif // SONGO_PLAYER_HARNESS_H
