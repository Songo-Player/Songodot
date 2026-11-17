// Copied from SongoBGPlayer's common/protocol.h (minus dispatch_command).
// Keep in sync: PROTOCOL_VERSION must match the daemon's.
//
// The control protocol between Songo and the engine, used both over the
// daemon's socket and in-process, so both transports carry the same messages.
//
// A message is one line of tab separated fields, the first naming it.
// Backslash, tab, newline and carriage return in fields are escaped as \\,
// \t, \n and \r. Numbers are plain C locale decimals. Easy to poke at by hand:
//
//   socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/songo_player.sock
//
// Commands, to the engine:
//   set_queue <start_index> <start_position> <autoplay 0|1> <path>...
//   replace_queue <current_index> <path>...
//   play_index <index> <position>
//   resume | pause | stop | next | previous
//   seek <seconds>
//   volume <0-1>
//   eq <enabled 0|1> <dB>...          (up to 10 bands)
//   eq_output <auto_preamp 0|1> <dB>  (EQ level after the bands)
//   blend <seconds>
//   repeat_one <0|1>
//   loop_queue <0|1>
//   network_timeout <seconds>
//   user_agent <text>
//   skip_failed <0|1>
//   latency <milliseconds>            (output buffer, 20-2000)
//   stream_buffer <seconds>           (network read-ahead, 0-30)
//   quit                              (daemon only: exit)
//   exit_on_disconnect <0|1>          (daemon only: quit when the last client goes)
//   exit_hook <shell command>         (daemon only: run as it quits, "" for none)
//
// Events, from the engine:
//   hello <protocol_version> <shared_size>   (daemon only, carries the memfd)
//   state <SongoState>
//   track <index> <flags> <duration> <path>
//   title <text>
//   error <index> <message>
//   queue <current_index> <path>...
//   queue_end

#ifndef SONGO_PROTOCOL_H
#define SONGO_PROTOCOL_H

#include "songo_engine.h"

#include <string>
#include <vector>

namespace songo {

constexpr int PROTOCOL_VERSION = 2;

struct Message {
	std::vector<std::string> fields;

	Message() = default;
	explicit Message(const std::string &name) { fields.push_back(name); }

	const std::string &name() const;
	// Fields after the name. Missing ones read as empty / 0.
	const std::string &arg(size_t i) const;
	int arg_int(size_t i) const;
	double arg_double(size_t i) const;
	size_t arg_count() const { return fields.empty() ? 0 : fields.size() - 1; }

	Message &add(const std::string &value);
	Message &add(const char *value) { return add(std::string(value ? value : "")); }
	Message &add(int value);
	Message &add(double value);
};

// One encoded line, newline included
std::string encode_line(const Message &message);
// One line without its newline
Message decode_line(const std::string &line);

// Pulls complete lines off the front of `buffer` (what's been read from a
// socket so far) and decodes them
void take_lines(std::string &buffer, std::vector<Message> &r_messages);

// Carrying out a command on an in-process engine is dispatch_command in
// ../engine_library.h, through the dlopen'ed engine's functions.
Message event_message(const SongoEvent &event);

// Where the daemon listens: $SONGO_PLAYER_SOCKET if set (launchers that need
// to find it), else $XDG_RUNTIME_DIR/songo_player.sock, or
// /tmp/songo_player-<uid>.sock without one (CFWs that run everything as root)
std::string default_socket_path();

} // namespace songo

#endif // SONGO_PROTOCOL_H
