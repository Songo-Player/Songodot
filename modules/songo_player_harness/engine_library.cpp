#include "engine_library.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <mutex>

namespace songo {

namespace {

#ifdef _WIN32
// dlopen / dlsym / dlerror / dlclose, as far as this file needs them
void *open_library(const std::string &p_path, std::string &r_why) {
	int length = MultiByteToWideChar(CP_UTF8, 0, p_path.c_str(), -1, nullptr, 0);
	std::wstring wide((size_t)length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, p_path.c_str(), -1, &wide[0], length);
	HMODULE library = LoadLibraryW(wide.c_str());
	if (!library) {
		r_why = "LoadLibrary error " + std::to_string((unsigned long)GetLastError());
	}
	return (void *)library;
}
void *library_symbol(void *p_library, const char *p_name) {
	return (void *)GetProcAddress((HMODULE)p_library, p_name);
}
void close_library(void *p_library) {
	FreeLibrary((HMODULE)p_library);
}
#else
void *open_library(const std::string &p_path, std::string &r_why) {
	void *library = dlopen(p_path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!library) {
		const char *why = dlerror();
		r_why = why ? why : "not loadable";
	}
	return library;
}
void *library_symbol(void *p_library, const char *p_name) {
	return dlsym(p_library, p_name);
}
void close_library(void *p_library) {
	dlclose(p_library);
}
#endif

std::mutex engine_library_mutex;
EngineApi engine_library_api;
bool engine_library_loaded = false;

template <typename F>
bool resolve_symbol(void *p_library, const char *p_name, F &r_function, std::string &r_missing) {
	r_function = reinterpret_cast<F>(library_symbol(p_library, p_name));
	if (!r_function) {
		r_missing = p_name;
		return false;
	}
	return true;
}

bool resolve_api(void *p_library, EngineApi &r_api, std::string &r_missing) {
#define SONGO_RESOLVE(m_field, m_symbol)                                    \
	if (!resolve_symbol(p_library, #m_symbol, r_api.m_field, r_missing)) { \
		return false;                                                       \
	}
	SONGO_RESOLVE(api_version, songo_engine_api_version)
	SONGO_RESOLVE(acquire_shared, songo_engine_acquire_shared)
	SONGO_RESOLVE(release_shared, songo_engine_release_shared)
	SONGO_RESOLVE(set_queue, songo_engine_set_queue)
	SONGO_RESOLVE(replace_queue, songo_engine_replace_queue)
	SONGO_RESOLVE(play_index, songo_engine_play_index)
	SONGO_RESOLVE(resume, songo_engine_resume)
	SONGO_RESOLVE(pause, songo_engine_pause)
	SONGO_RESOLVE(stop, songo_engine_stop)
	SONGO_RESOLVE(seek, songo_engine_seek)
	SONGO_RESOLVE(next, songo_engine_next)
	SONGO_RESOLVE(previous, songo_engine_previous)
	SONGO_RESOLVE(set_volume, songo_engine_set_volume)
	SONGO_RESOLVE(set_eq, songo_engine_set_eq)
	SONGO_RESOLVE(set_eq_output, songo_engine_set_eq_output)
	SONGO_RESOLVE(set_blend, songo_engine_set_blend)
	SONGO_RESOLVE(set_repeat_one, songo_engine_set_repeat_one)
	SONGO_RESOLVE(set_loop_queue, songo_engine_set_loop_queue)
	SONGO_RESOLVE(set_network_timeout, songo_engine_set_network_timeout)
	SONGO_RESOLVE(set_user_agent, songo_engine_set_user_agent)
	SONGO_RESOLVE(set_skip_failed, songo_engine_set_skip_failed)
	SONGO_RESOLVE(set_latency, songo_engine_set_latency)
	SONGO_RESOLVE(set_stream_buffer, songo_engine_set_stream_buffer)
	SONGO_RESOLVE(subscribe, songo_engine_subscribe)
	SONGO_RESOLVE(subscription_close, songo_subscription_close)
	SONGO_RESOLVE(subscription_poll, songo_subscription_poll)
	SONGO_RESOLVE(shared, songo_engine_shared)
#undef SONGO_RESOLVE
	return true;
}

std::vector<const char *> message_c_strings(const Message &p_message, size_t p_first_arg) {
	std::vector<const char *> out;
	for (size_t i = p_first_arg; i < p_message.arg_count(); i++) {
		out.push_back(p_message.arg(i).c_str());
	}
	return out;
}

} // namespace

const EngineApi *load_engine_library(const std::vector<std::string> &p_candidates, std::string &r_error) {
	std::lock_guard<std::mutex> lock(engine_library_mutex);
	if (engine_library_loaded) {
		return &engine_library_api;
	}

	std::string tried;
	for (const std::string &path : p_candidates) {
		if (path.empty()) {
			continue;
		}
		std::string why;
		void *library = open_library(path, why);
		if (!library) {
			tried += "\n  " + path + ": " + why;
			continue;
		}
		EngineApi api = {};
		std::string missing;
		if (!resolve_api(library, api, missing)) {
			tried += "\n  " + path + ": no " + missing + ", not a libsongo_engine";
			close_library(library);
			continue;
		}
		if (api.api_version() != SONGO_ENGINE_API_VERSION) {
			tried += "\n  " + path + ": engine API version " + std::to_string(api.api_version()) +
					", this Songodot needs " + std::to_string(SONGO_ENGINE_API_VERSION);
			close_library(library);
			continue;
		}
		// Never closed, see load_engine_library's comment
		engine_library_api = api;
		engine_library_loaded = true;
		return &engine_library_api;
	}

	r_error = "Couldn't load the music engine library (libsongo_engine), tried:" + (tried.empty() ? std::string(" nothing") : tried);
	return nullptr;
}

bool dispatch_command(const EngineApi &api, SongoEngine *engine, const Message &m) {
	const std::string &name = m.name();
	if (name == "set_queue") {
		std::vector<const char *> paths = message_c_strings(m, 3);
		api.set_queue(engine, paths.data(), (int)paths.size(), m.arg_int(0), m.arg_double(1), m.arg_int(2));
	} else if (name == "replace_queue") {
		std::vector<const char *> paths = message_c_strings(m, 1);
		api.replace_queue(engine, paths.data(), (int)paths.size(), m.arg_int(0));
	} else if (name == "play_index") {
		api.play_index(engine, m.arg_int(0), m.arg_double(1));
	} else if (name == "resume") {
		api.resume(engine);
	} else if (name == "pause") {
		api.pause(engine);
	} else if (name == "stop") {
		api.stop(engine);
	} else if (name == "next") {
		api.next(engine);
	} else if (name == "previous") {
		api.previous(engine);
	} else if (name == "seek") {
		api.seek(engine, m.arg_double(0));
	} else if (name == "volume") {
		api.set_volume(engine, (float)m.arg_double(0));
	} else if (name == "eq") {
		std::vector<float> gains;
		for (size_t i = 1; i < m.arg_count(); i++) {
			gains.push_back((float)m.arg_double(i));
		}
		api.set_eq(engine, m.arg_int(0), gains.data(), (int)gains.size());
	} else if (name == "eq_output") {
		api.set_eq_output(engine, m.arg_int(0), (float)m.arg_double(1));
	} else if (name == "blend") {
		api.set_blend(engine, m.arg_double(0));
	} else if (name == "repeat_one") {
		api.set_repeat_one(engine, m.arg_int(0));
	} else if (name == "loop_queue") {
		api.set_loop_queue(engine, m.arg_int(0));
	} else if (name == "network_timeout") {
		api.set_network_timeout(engine, m.arg_double(0));
	} else if (name == "user_agent") {
		api.set_user_agent(engine, m.arg(0).c_str());
	} else if (name == "skip_failed") {
		api.set_skip_failed(engine, m.arg_int(0));
	} else if (name == "latency") {
		api.set_latency(engine, m.arg_int(0));
	} else if (name == "stream_buffer") {
		api.set_stream_buffer(engine, m.arg_double(0));
	} else {
		return false;
	}
	return true;
}

} // namespace songo
