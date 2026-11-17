#include "songo_player_harness.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/os/os.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <algorithm>

namespace {

// The names SongoBGPlayer gives its builds, eg: libsongo_engine.linux.arm64.so
#if defined(__ANDROID__)
#define SONGO_PLATFORM "android"
#elif defined(_WIN32)
#define SONGO_PLATFORM "windows"
#else
#define SONGO_PLATFORM "linux"
#endif
#ifdef _WIN32
#define SONGO_LIBRARY_SUFFIX ".dll"
#else
#define SONGO_LIBRARY_SUFFIX ".so"
#endif
#if defined(__aarch64__)
#define SONGO_ARCH "arm64"
#elif defined(__x86_64__)
#define SONGO_ARCH "x86_64"
#else
#define SONGO_ARCH "unknown"
#endif
const char *ENGINE_LIBRARY_FILE = "libsongo_engine." SONGO_PLATFORM "." SONGO_ARCH SONGO_LIBRARY_SUFFIX;
const char *DAEMON_FILE = "songo_bgplayer." SONGO_PLATFORM "." SONGO_ARCH;

std::string to_std(const String &p_string) {
	CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

String to_godot(const std::string &p_string) {
	return String::utf8(p_string.c_str(), (int)p_string.size());
}

void add_paths(songo::Message &r_message, const PackedStringArray &p_paths) {
	for (int i = 0; i < p_paths.size(); i++) {
		r_message.add(to_std(p_paths[i]));
	}
}

} // namespace

void SongoPlayerHarness::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));
	ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("stream_title_changed", PropertyInfo(Variant::STRING, "title")));
	ADD_SIGNAL(MethodInfo("track_changed", PropertyInfo(Variant::INT, "index"), PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("queue_changed"));
	ADD_SIGNAL(MethodInfo("engine_connection_changed", PropertyInfo(Variant::BOOL, "connected")));

	ClassDB::bind_method(D_METHOD("play", "path", "seek_time"), &SongoPlayerHarness::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("set_queue", "paths", "start_index", "start_position", "autoplay"), &SongoPlayerHarness::set_queue, DEFVAL(0), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("replace_queue", "paths", "current_index"), &SongoPlayerHarness::replace_queue);
	ClassDB::bind_method(D_METHOD("play_index", "index", "position"), &SongoPlayerHarness::play_index, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("resume"), &SongoPlayerHarness::resume);
	ClassDB::bind_method(D_METHOD("pause"), &SongoPlayerHarness::pause);
	ClassDB::bind_method(D_METHOD("stop"), &SongoPlayerHarness::stop);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &SongoPlayerHarness::seek);
	ClassDB::bind_method(D_METHOD("next"), &SongoPlayerHarness::next);
	ClassDB::bind_method(D_METHOD("previous"), &SongoPlayerHarness::previous);

	ClassDB::bind_method(D_METHOD("get_state"), &SongoPlayerHarness::get_state);
	ClassDB::bind_method(D_METHOD("is_playing"), &SongoPlayerHarness::is_playing);
	ClassDB::bind_method(D_METHOD("is_paused"), &SongoPlayerHarness::is_paused);
	ClassDB::bind_method(D_METHOD("is_live"), &SongoPlayerHarness::is_live);
	ClassDB::bind_method(D_METHOD("is_seekable"), &SongoPlayerHarness::is_seekable);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &SongoPlayerHarness::get_playback_position);
	ClassDB::bind_method(D_METHOD("get_duration"), &SongoPlayerHarness::get_duration);
	ClassDB::bind_method(D_METHOD("get_current_index"), &SongoPlayerHarness::get_current_index);
	ClassDB::bind_method(D_METHOD("get_current_path"), &SongoPlayerHarness::get_current_path);
	ClassDB::bind_method(D_METHOD("get_queue"), &SongoPlayerHarness::get_queue);
	ClassDB::bind_method(D_METHOD("get_stream_title"), &SongoPlayerHarness::get_stream_title);
	ClassDB::bind_method(D_METHOD("is_engine_connected"), &SongoPlayerHarness::is_engine_connected);

	ClassDB::bind_method(D_METHOD("get_magnitude_for_frequency_range", "from_hz", "to_hz", "mode"), &SongoPlayerHarness::get_magnitude_for_frequency_range, DEFVAL(MAGNITUDE_MAX));
	ClassDB::bind_method(D_METHOD("get_waveform", "frame_count"), &SongoPlayerHarness::get_waveform, DEFVAL(1024));

	ClassDB::bind_method(D_METHOD("set_volume", "volume"), &SongoPlayerHarness::set_volume);
	ClassDB::bind_method(D_METHOD("get_volume"), &SongoPlayerHarness::get_volume);
	ClassDB::bind_method(D_METHOD("set_blend_time", "seconds"), &SongoPlayerHarness::set_blend_time);
	ClassDB::bind_method(D_METHOD("get_blend_time"), &SongoPlayerHarness::get_blend_time);
	ClassDB::bind_method(D_METHOD("set_repeat_one", "enabled"), &SongoPlayerHarness::set_repeat_one);
	ClassDB::bind_method(D_METHOD("get_repeat_one"), &SongoPlayerHarness::get_repeat_one);
	ClassDB::bind_method(D_METHOD("set_loop_queue", "enabled"), &SongoPlayerHarness::set_loop_queue);
	ClassDB::bind_method(D_METHOD("get_loop_queue"), &SongoPlayerHarness::get_loop_queue);
	ClassDB::bind_method(D_METHOD("set_eq_enabled", "enabled"), &SongoPlayerHarness::set_eq_enabled);
	ClassDB::bind_method(D_METHOD("get_eq_enabled"), &SongoPlayerHarness::get_eq_enabled);
	ClassDB::bind_method(D_METHOD("set_eq_band_gains", "gains_db"), &SongoPlayerHarness::set_eq_band_gains);
	ClassDB::bind_method(D_METHOD("get_eq_band_gains"), &SongoPlayerHarness::get_eq_band_gains);
	ClassDB::bind_method(D_METHOD("set_eq_auto_preamp", "enabled"), &SongoPlayerHarness::set_eq_auto_preamp);
	ClassDB::bind_method(D_METHOD("get_eq_auto_preamp"), &SongoPlayerHarness::get_eq_auto_preamp);
	ClassDB::bind_method(D_METHOD("set_eq_output_gain_db", "gain_db"), &SongoPlayerHarness::set_eq_output_gain_db);
	ClassDB::bind_method(D_METHOD("get_eq_output_gain_db"), &SongoPlayerHarness::get_eq_output_gain_db);
	ClassDB::bind_method(D_METHOD("set_network_timeout", "seconds"), &SongoPlayerHarness::set_network_timeout);
	ClassDB::bind_method(D_METHOD("get_network_timeout"), &SongoPlayerHarness::get_network_timeout);
	ClassDB::bind_method(D_METHOD("set_user_agent", "user_agent"), &SongoPlayerHarness::set_user_agent);
	ClassDB::bind_method(D_METHOD("get_user_agent"), &SongoPlayerHarness::get_user_agent);
	ClassDB::bind_method(D_METHOD("set_skip_failed", "enabled"), &SongoPlayerHarness::set_skip_failed);
	ClassDB::bind_method(D_METHOD("get_skip_failed"), &SongoPlayerHarness::get_skip_failed);
	ClassDB::bind_method(D_METHOD("set_output_latency_ms", "milliseconds"), &SongoPlayerHarness::set_output_latency_ms);
	ClassDB::bind_method(D_METHOD("get_output_latency_ms"), &SongoPlayerHarness::get_output_latency_ms);
	ClassDB::bind_method(D_METHOD("set_stream_buffer_length", "seconds"), &SongoPlayerHarness::set_stream_buffer_length);
	ClassDB::bind_method(D_METHOD("get_stream_buffer_length"), &SongoPlayerHarness::get_stream_buffer_length);

	ClassDB::bind_method(D_METHOD("set_transport", "transport"), &SongoPlayerHarness::set_transport);
	ClassDB::bind_method(D_METHOD("get_transport"), &SongoPlayerHarness::get_transport);
	ClassDB::bind_method(D_METHOD("set_socket_path", "path"), &SongoPlayerHarness::set_socket_path);
	ClassDB::bind_method(D_METHOD("get_socket_path"), &SongoPlayerHarness::get_socket_path);
	ClassDB::bind_method(D_METHOD("set_daemon_path", "path"), &SongoPlayerHarness::set_daemon_path);
	ClassDB::bind_method(D_METHOD("get_daemon_path"), &SongoPlayerHarness::get_daemon_path);
	ClassDB::bind_method(D_METHOD("set_engine_library_path", "path"), &SongoPlayerHarness::set_engine_library_path);
	ClassDB::bind_method(D_METHOD("get_engine_library_path"), &SongoPlayerHarness::get_engine_library_path);
	ClassDB::bind_method(D_METHOD("set_output_backend", "backend"), &SongoPlayerHarness::set_output_backend);
	ClassDB::bind_method(D_METHOD("get_output_backend"), &SongoPlayerHarness::get_output_backend);
	ClassDB::bind_method(D_METHOD("set_output_device", "device"), &SongoPlayerHarness::set_output_device);
	ClassDB::bind_method(D_METHOD("get_output_device"), &SongoPlayerHarness::get_output_device);
	ClassDB::bind_method(D_METHOD("set_daemon_exits_with_app", "enabled"), &SongoPlayerHarness::set_daemon_exits_with_app);
	ClassDB::bind_method(D_METHOD("get_daemon_exits_with_app"), &SongoPlayerHarness::get_daemon_exits_with_app);
	ClassDB::bind_method(D_METHOD("set_daemon_exit_hook", "command"), &SongoPlayerHarness::set_daemon_exit_hook);
	ClassDB::bind_method(D_METHOD("get_daemon_exit_hook"), &SongoPlayerHarness::get_daemon_exit_hook);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_time", PROPERTY_HINT_RANGE, "0,30,0.1,suffix:s"), "set_blend_time", "get_blend_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "repeat_one"), "set_repeat_one", "get_repeat_one");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop_queue"), "set_loop_queue", "get_loop_queue");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "eq_enabled"), "set_eq_enabled", "get_eq_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "eq_band_gains"), "set_eq_band_gains", "get_eq_band_gains");
	// The EQ's level after its bands: auto preamp takes off the largest boost
	// so boosts don't clip, and the output gain is added on top
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "eq_auto_preamp"), "set_eq_auto_preamp", "get_eq_auto_preamp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "eq_output_gain_db", PROPERTY_HINT_RANGE, "-24,24,0.5"), "set_eq_output_gain_db", "get_eq_output_gain_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "network_timeout", PROPERTY_HINT_RANGE, "1,120,0.5,suffix:s"), "set_network_timeout", "get_network_timeout");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "user_agent"), "set_user_agent", "get_user_agent");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "skip_failed"), "set_skip_failed", "get_skip_failed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "output_latency_ms", PROPERTY_HINT_RANGE, "20,2000,1,suffix:ms"), "set_output_latency_ms", "get_output_latency_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stream_buffer_length", PROPERTY_HINT_RANGE, "0,30,0.1,suffix:s"), "set_stream_buffer_length", "get_stream_buffer_length");

	ADD_GROUP("Engine", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "transport", PROPERTY_HINT_ENUM, "Auto,In Process,Daemon"), "set_transport", "get_transport");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "socket_path"), "set_socket_path", "get_socket_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "daemon_path", PROPERTY_HINT_GLOBAL_FILE), "set_daemon_path", "get_daemon_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "engine_library_path", PROPERTY_HINT_GLOBAL_FILE, "*.so"), "set_engine_library_path", "get_engine_library_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "output_backend", PROPERTY_HINT_ENUM_SUGGESTION, "alsa,null"), "set_output_backend", "get_output_backend");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "output_device"), "set_output_device", "get_output_device");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "daemon_exits_with_app"), "set_daemon_exits_with_app", "get_daemon_exits_with_app");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "daemon_exit_hook"), "set_daemon_exit_hook", "get_daemon_exit_hook");

	BIND_ENUM_CONSTANT(TRANSPORT_AUTO);
	BIND_ENUM_CONSTANT(TRANSPORT_IN_PROCESS);
	BIND_ENUM_CONSTANT(TRANSPORT_DAEMON);
	BIND_ENUM_CONSTANT(STATE_STOPPED);
	BIND_ENUM_CONSTANT(STATE_LOADING);
	BIND_ENUM_CONSTANT(STATE_PLAYING);
	BIND_ENUM_CONSTANT(STATE_PAUSED);
	BIND_ENUM_CONSTANT(MAGNITUDE_AVERAGE);
	BIND_ENUM_CONSTANT(MAGNITUDE_MAX);
}

SongoPlayerHarness::SongoPlayerHarness() {
	// Signals keep flowing while the game is paused
	set_process_mode(PROCESS_MODE_ALWAYS);
	eq_band_gains.resize(songo::EQ_BANDS);
	eq_band_gains.fill(0.0f);
}

SongoPlayerHarness::~SongoPlayerHarness() {
	transport.reset();
}

void SongoPlayerHarness::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}
			// Picks up a daemon that's still playing from last time
			_ensure_transport();
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_poll();
		} break;
		case NOTIFICATION_PREDELETE: {
			transport.reset();
		} break;
	}
}

Vector<String> SongoPlayerHarness::_search_dirs() const {
	Vector<String> dirs;
	dirs.push_back(OS::get_singleton()->get_executable_path().get_base_dir());
	// A filesystem path when running from the project's folder (the editor,
	// or an export run with the project as its working directory)
	String project_bin = ProjectSettings::get_singleton()->globalize_path("res://addons/songo_player_harness/bin");
	if (!project_bin.begins_with("res://")) {
		dirs.push_back(project_bin);
	}
	return dirs;
}

std::vector<std::string> SongoPlayerHarness::_engine_library_candidates() const {
	std::vector<std::string> candidates;
	if (!engine_library_path.is_empty()) {
		candidates.push_back(to_std(ProjectSettings::get_singleton()->globalize_path(engine_library_path)));
		return candidates;
	}
#ifdef __ANDROID__
	// From the APK's native libraries, which the plugin AAR puts it in (and
	// whose playback service may have loaded it already)
	candidates.push_back(ENGINE_LIBRARY_FILE);
#endif
	for (const String &dir : _search_dirs()) {
		candidates.push_back(to_std(dir.path_join(ENGINE_LIBRARY_FILE)));
	}
	return candidates;
}

String SongoPlayerHarness::_default_daemon_path() const {
	Vector<String> dirs = _search_dirs();
	for (const String &dir : dirs) {
		String path = dir.path_join(DAEMON_FILE);
#ifdef _WIN32
		if (FileAccess::exists(path)) {
#else
		if (access(path.utf8().get_data(), F_OK) == 0) {
#endif
			return path;
		}
	}
	// Not found: named anyway, so the error says where it was expected
	return dirs[0].path_join(DAEMON_FILE);
}

bool SongoPlayerHarness::_ensure_transport() {
	if (transport) {
		return true;
	}
	if (transport_failed || Engine::get_singleton()->is_editor_hint()) {
		return false;
	}

	Transport mode = transport_mode;
	if (mode == TRANSPORT_AUTO) {
#if defined(__ANDROID__) || defined(_WIN32)
		mode = TRANSPORT_IN_PROCESS;
#else
		mode = TRANSPORT_DAEMON;
#endif
	}
#ifdef _WIN32
	if (mode == TRANSPORT_DAEMON) {
		WARN_PRINT("SongoPlayerHarness: there's no background player on Windows, playing in-process.");
		mode = TRANSPORT_IN_PROCESS;
	}
#endif

	songo::EngineSettings engine_settings;
	engine_settings.backend = to_std(output_backend);
	engine_settings.device = to_std(output_device);

	if (mode == TRANSPORT_IN_PROCESS) {
		std::string error;
		const songo::EngineApi *api = songo::load_engine_library(_engine_library_candidates(), error);
		if (api) {
			transport = songo::create_in_process_transport(*api, engine_settings, error);
		}
		if (!transport) {
			transport_failed = true;
			ERR_PRINT("SongoPlayerHarness: " + to_godot(error));
			call_deferred(SNAME("emit_signal"), SNAME("error"), to_godot(error));
			return false;
		}
	}
#ifndef _WIN32
	else {
		songo::DaemonSettings settings;
		settings.socket_path = to_std(socket_path);
		settings.daemon_path = to_std(daemon_path.is_empty() ? _default_daemon_path() : ProjectSettings::get_singleton()->globalize_path(daemon_path));
		settings.engine = engine_settings;
		settings.exit_with_app = daemon_exits_with_app;
		transport = songo::create_daemon_transport(settings);
	}
#endif
	return true;
}

void SongoPlayerHarness::_send(const songo::Message &p_message, bool p_start_engine) {
	if (_ensure_transport()) {
		transport->send(p_message, p_start_engine);
	}
}

void SongoPlayerHarness::_poll() {
	if (!transport) {
		return;
	}
	std::vector<songo::Message> events;
	transport->poll(events);
	for (const songo::Message &event : events) {
		_handle(event);
	}
	// Keeps the engine analysing while we're around, so a visualizer has a
	// spectrum the moment it appears. It stops once Songo closes or sleeps.
	if (songo::Shared *shared = transport->shared()) {
		songo::touch_heartbeat(shared, songo::monotonic_ns());
	}
}

void SongoPlayerHarness::_handle(const songo::Message &p_message) {
	const std::string &name = p_message.name();
	if (name == "state") {
		int new_state = p_message.arg_int(0);
		if (new_state != state) {
			state = new_state;
			emit_signal("state_changed", state);
		}
	} else if (name == "track") {
		current_index = p_message.arg_int(0);
		current_path = to_godot(p_message.arg(3));
		emit_signal("track_changed", current_index, current_path);
	} else if (name == "title") {
		String title = to_godot(p_message.arg(0));
		if (title != stream_title) {
			stream_title = title;
			emit_signal("stream_title_changed", stream_title);
		}
	} else if (name == "error") {
		String message = to_godot(p_message.arg(1));
		print_line("SongoPlayerHarness: " + message);
		emit_signal("error", message);
	} else if (name == "queue") {
		current_index = p_message.arg_int(0);
		queue.clear();
		for (size_t i = 1; i < p_message.arg_count(); i++) {
			queue.push_back(to_godot(p_message.arg(i)));
		}
		emit_signal("queue_changed");
	} else if (name == "queue_end") {
		emit_signal("finished");
	} else if (name == "connected") {
		// A daemon we didn't start (or started before this was changed)
		// needs telling. The in-process engine ignores it.
		_send_setting(songo::Message("exit_on_disconnect").add(daemon_exits_with_app ? 1 : 0));
		// Only if we have one, so connecting doesn't wipe the one an earlier
		// Songo left when it quit with the music on
		if (!daemon_exit_hook.is_empty()) {
			_send_setting(songo::Message("exit_hook").add(to_std(daemon_exit_hook)));
		}
		emit_signal("engine_connection_changed", true);
	} else if (name == "disconnected") {
		if (state != STATE_STOPPED) {
			state = STATE_STOPPED;
			emit_signal("state_changed", state);
		}
		emit_signal("engine_connection_changed", false);
	}
}

bool SongoPlayerHarness::_read_status(songo::Status &r_status) const {
	songo::Shared *shared = transport ? transport->shared() : nullptr;
	return shared && songo::read_status(shared, r_status);
}

// ---- Playback ----

void SongoPlayerHarness::play(const String &p_path, double p_seek_time) {
	PackedStringArray paths;
	paths.push_back(p_path);
	set_queue(paths, 0, p_seek_time, true);
}

void SongoPlayerHarness::set_queue(const PackedStringArray &p_paths, int p_start_index, double p_start_position, bool p_autoplay) {
	songo::Message message("set_queue");
	message.add(p_start_index).add(p_start_position).add(p_autoplay ? 1 : 0);
	add_paths(message, p_paths);
	_send(message);
}

void SongoPlayerHarness::replace_queue(const PackedStringArray &p_paths, int p_current_index) {
	songo::Message message("replace_queue");
	message.add(p_current_index);
	add_paths(message, p_paths);
	_send(message);
}

void SongoPlayerHarness::play_index(int p_index, double p_position) {
	_send(songo::Message("play_index").add(p_index).add(p_position));
}

void SongoPlayerHarness::resume() {
	_send(songo::Message("resume"));
}

void SongoPlayerHarness::pause() {
	_send(songo::Message("pause"));
}

void SongoPlayerHarness::stop() {
	// Don't start a daemon just to tell it to stop
	if (transport && transport->is_connected()) {
		transport->send(songo::Message("stop"));
	}
}

void SongoPlayerHarness::seek(double p_seconds) {
	_send(songo::Message("seek").add(p_seconds));
}

void SongoPlayerHarness::next() {
	_send(songo::Message("next"));
}

void SongoPlayerHarness::previous() {
	_send(songo::Message("previous"));
}

// ---- State ----

int SongoPlayerHarness::get_state() const {
	songo::Status status;
	return _read_status(status) ? (int)status.state : state;
}

bool SongoPlayerHarness::is_playing() const {
	int current = get_state();
	return current == STATE_PLAYING || current == STATE_LOADING;
}

bool SongoPlayerHarness::is_paused() const {
	return get_state() == STATE_PAUSED;
}

bool SongoPlayerHarness::is_live() const {
	songo::Status status;
	return _read_status(status) && (status.flags & songo::STATUS_LIVE);
}

bool SongoPlayerHarness::is_seekable() const {
	songo::Status status;
	return _read_status(status) && (status.flags & songo::STATUS_SEEKABLE);
}

double SongoPlayerHarness::get_playback_position() const {
	songo::Status status;
	return _read_status(status) ? songo::status_position_at(status, songo::monotonic_ns()) : 0.0;
}

double SongoPlayerHarness::get_duration() const {
	songo::Status status;
	return _read_status(status) ? status.duration : -1.0;
}

int SongoPlayerHarness::get_current_index() const {
	songo::Status status;
	return _read_status(status) ? status.index : current_index;
}

void SongoPlayerHarness::set_daemon_exits_with_app(bool p_enabled) {
	daemon_exits_with_app = p_enabled;
	if (transport && transport->is_connected()) {
		transport->send(songo::Message("exit_on_disconnect").add(p_enabled ? 1 : 0), false);
	}
}

void SongoPlayerHarness::set_daemon_exit_hook(const String &p_command) {
	daemon_exit_hook = p_command;
	if (transport && transport->is_connected()) {
		transport->send(songo::Message("exit_hook").add(to_std(p_command)), false);
	}
}

bool SongoPlayerHarness::is_engine_connected() const {
	return transport && transport->is_connected();
}

// ---- Visualizers ----

void SongoPlayerHarness::_refresh_spectrum() {
	uint64_t frame = Engine::get_singleton()->get_process_frames();
	if (frame == spectrum_process_frame) {
		return;
	}
	spectrum_process_frame = frame;
	spectrum_valid = false;

	songo::Shared *shared = transport ? transport->shared() : nullptr;
	if (!shared) {
		return;
	}
	int64_t now = songo::monotonic_ns();
	songo::touch_heartbeat(shared, now);
	songo::Status status;
	if (!songo::read_status(shared, status) || status.state != STATE_PLAYING) {
		return;
	}
	spectrum_valid = songo::read_audible_spectrum(shared, now, spectrum);
}

Vector2 SongoPlayerHarness::get_magnitude_for_frequency_range(float p_from_hz, float p_to_hz, MagnitudeMode p_mode) {
	_refresh_spectrum();
	songo::Shared *shared = transport ? transport->shared() : nullptr;
	if (!spectrum_valid || !shared) {
		return Vector2();
	}

	// Same bin picking as AudioEffectSpectrumAnalyzerInstance
	const int fft_size = songo::FFT_SIZE;
	float mix_rate = (float)shared->sample_rate;
	int begin_pos = (int)(p_from_hz * fft_size / (mix_rate * 0.5f));
	int end_pos = (int)(p_to_hz * fft_size / (mix_rate * 0.5f));
	begin_pos = CLAMP(begin_pos, 0, fft_size - 1);
	end_pos = CLAMP(end_pos, 0, fft_size - 1);
	if (begin_pos > end_pos) {
		std::swap(begin_pos, end_pos);
	}

	if (p_mode == MAGNITUDE_AVERAGE) {
		Vector2 avg;
		for (int i = begin_pos; i <= end_pos; i++) {
			avg += Vector2(spectrum[0][i], spectrum[1][i]);
		}
		return avg / float(end_pos - begin_pos + 1);
	}
	Vector2 max;
	for (int i = begin_pos; i <= end_pos; i++) {
		max.x = MAX(max.x, spectrum[0][i]);
		max.y = MAX(max.y, spectrum[1][i]);
	}
	return max;
}

PackedVector2Array SongoPlayerHarness::get_waveform(int p_frame_count) {
	PackedVector2Array out;
	p_frame_count = CLAMP(p_frame_count, 1, songo::WAVE_FRAMES / 2);
	out.resize(p_frame_count);
	songo::Shared *shared = transport ? transport->shared() : nullptr;
	if (!shared) {
		out.fill(Vector2());
		return out;
	}
	int64_t now = songo::monotonic_ns();
	songo::touch_heartbeat(shared, now);
	songo::Status status;
	std::vector<float> frames((size_t)p_frame_count * 2);
	if (!songo::read_status(shared, status) || status.state != STATE_PLAYING ||
			!songo::read_audible_waveform(shared, now, p_frame_count, reinterpret_cast<float(*)[2]>(frames.data()))) {
		out.fill(Vector2());
		return out;
	}
	Vector2 *dst = out.ptrw();
	for (int i = 0; i < p_frame_count; i++) {
		dst[i] = Vector2(frames[(size_t)i * 2], frames[(size_t)i * 2 + 1]);
	}
	return out;
}

// ---- Settings ----

void SongoPlayerHarness::set_volume(float p_volume) {
	volume = CLAMP(p_volume, 0.0f, 1.0f);
	_send_setting(songo::Message("volume").add((double)volume));
}

float SongoPlayerHarness::get_volume() const {
	songo::Status status;
	return _read_status(status) ? status.volume : volume;
}

void SongoPlayerHarness::set_blend_time(double p_seconds) {
	blend_time = MAX(p_seconds, 0.0);
	_send_setting(songo::Message("blend").add(blend_time));
}

double SongoPlayerHarness::get_blend_time() const {
	songo::Status status;
	return _read_status(status) ? status.blend_seconds : blend_time;
}

void SongoPlayerHarness::set_repeat_one(bool p_enabled) {
	repeat_one = p_enabled;
	_send_setting(songo::Message("repeat_one").add(p_enabled ? 1 : 0));
}

bool SongoPlayerHarness::get_repeat_one() const {
	songo::Status status;
	return _read_status(status) ? (status.flags & songo::STATUS_REPEAT_ONE) != 0 : repeat_one;
}

void SongoPlayerHarness::set_loop_queue(bool p_enabled) {
	loop_queue = p_enabled;
	_send_setting(songo::Message("loop_queue").add(p_enabled ? 1 : 0));
}

bool SongoPlayerHarness::get_loop_queue() const {
	songo::Status status;
	return _read_status(status) ? (status.flags & songo::STATUS_LOOP_QUEUE) != 0 : loop_queue;
}

void SongoPlayerHarness::set_eq_enabled(bool p_enabled) {
	eq_enabled = p_enabled;
	songo::Message message("eq");
	message.add(eq_enabled ? 1 : 0);
	for (int i = 0; i < eq_band_gains.size(); i++) {
		message.add((double)eq_band_gains[i]);
	}
	_send_setting(message);
}

bool SongoPlayerHarness::get_eq_enabled() const {
	songo::Status status;
	return _read_status(status) ? (status.flags & songo::STATUS_EQ_ENABLED) != 0 : eq_enabled;
}

void SongoPlayerHarness::set_eq_band_gains(const PackedFloat32Array &p_gains_db) {
	eq_band_gains = p_gains_db;
	eq_band_gains.resize(songo::EQ_BANDS);
	set_eq_enabled(eq_enabled);
}

PackedFloat32Array SongoPlayerHarness::get_eq_band_gains() const {
	songo::Status status;
	if (!_read_status(status)) {
		return eq_band_gains;
	}
	PackedFloat32Array gains;
	for (int band = 0; band < songo::EQ_BANDS; band++) {
		gains.push_back(status.eq_gains_db[band]);
	}
	return gains;
}

void SongoPlayerHarness::set_eq_auto_preamp(bool p_enabled) {
	eq_auto_preamp = p_enabled;
	_send_setting(songo::Message("eq_output").add(eq_auto_preamp ? 1 : 0).add((double)eq_output_gain_db));
}

bool SongoPlayerHarness::get_eq_auto_preamp() const {
	songo::Status status;
	return _read_status(status) ? (status.flags & songo::STATUS_EQ_AUTO_PREAMP) != 0 : eq_auto_preamp;
}

void SongoPlayerHarness::set_eq_output_gain_db(float p_gain_db) {
	eq_output_gain_db = p_gain_db;
	set_eq_auto_preamp(eq_auto_preamp);
}

float SongoPlayerHarness::get_eq_output_gain_db() const {
	songo::Status status;
	return _read_status(status) ? status.eq_output_gain_db : eq_output_gain_db;
}

void SongoPlayerHarness::set_network_timeout(double p_seconds) {
	network_timeout = MAX(p_seconds, 1.0);
	_send_setting(songo::Message("network_timeout").add(network_timeout));
}

double SongoPlayerHarness::get_network_timeout() const {
	return network_timeout;
}

void SongoPlayerHarness::set_user_agent(const String &p_user_agent) {
	user_agent = p_user_agent;
	_send_setting(songo::Message("user_agent").add(to_std(user_agent)));
}

String SongoPlayerHarness::get_user_agent() const {
	return user_agent;
}

void SongoPlayerHarness::set_output_latency_ms(int p_milliseconds) {
	output_latency_ms = CLAMP(p_milliseconds, 20, 2000);
	_send_setting(songo::Message("latency").add(output_latency_ms));
}

void SongoPlayerHarness::set_stream_buffer_length(double p_seconds) {
	stream_buffer_length = CLAMP(p_seconds, 0.0, 30.0);
	_send_setting(songo::Message("stream_buffer").add(stream_buffer_length));
}

void SongoPlayerHarness::set_skip_failed(bool p_enabled) {
	skip_failed = p_enabled;
	_send_setting(songo::Message("skip_failed").add(p_enabled ? 1 : 0));
}
