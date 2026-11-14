// How SongoPlayerHarness reaches the engine. Both transports carry the same
// protocol messages (common/protocol.h) and expose the engine's shared memory:
//   - in-process: the engine runs inside Godot's process (Android, where the
//     service shares it, and desktop debugging of that path), loaded at
//     runtime (engine_library.h)
//   - daemon: the engine runs in songo_bgplayer, which outlives Songo
//     (Linux handhelds and desktops)
//
// Plain C++ on purpose, so it can be tested without Godot.

#ifndef SONGO_TRANSPORT_H
#define SONGO_TRANSPORT_H

#include "engine_library.h"
#include "protocol.h"
#include "songo_shared.h"

#include <memory>
#include <string>
#include <vector>

namespace songo {

class Transport {
public:
	virtual ~Transport() = default;

	// Commands are queued while not connected and sent once connected.
	// `start_engine` off for ones that shouldn't start the daemon (settings
	// a scene sets as it loads), which just wait for a connection.
	virtual void send(const Message &message, bool start_engine = true) = 0;
	// Collects the events that arrived since the last poll. Besides the
	// engine's own, transports add "connected" and "disconnected", and
	// "error" for problems reaching the engine.
	virtual void poll(std::vector<Message> &r_events) = 0;
	// The engine's shared block, null while there isn't one. Readers only
	// ever write its heartbeat.
	virtual Shared *shared() = 0;
	virtual bool is_connected() const = 0;
};

struct EngineSettings {
	// Empty for the engine's defaults
	std::string backend;
	std::string device;
};

struct DaemonSettings {
	// Empty for songo::default_socket_path()
	std::string socket_path;
	// songo_bgplayer to start when none is running. Empty to never start one.
	std::string daemon_path;
	EngineSettings engine;
	// Have the daemon quit when we disconnect, for debugging from the editor
	bool exit_with_app = false;
};

// Null with `r_error` set if the engine can't be created. `api` is the loaded
// engine library's (load_engine_library).
std::unique_ptr<Transport> create_in_process_transport(const EngineApi &api, const EngineSettings &settings, std::string &r_error);
// Connects to a running daemon straight away. Only starts one once there's a
// command to send, so just opening Songo never starts it.
std::unique_ptr<Transport> create_daemon_transport(const DaemonSettings &settings);

} // namespace songo

#endif // SONGO_TRANSPORT_H
