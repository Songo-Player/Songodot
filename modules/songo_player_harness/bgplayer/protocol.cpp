// Copied from SongoBGPlayer's common/protocol.cpp, minus dispatch_command
// (see ../engine_library.cpp). Keep in sync.

#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace songo {

namespace {

const std::string EMPTY;

std::string escape(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		switch (c) {
			case '\\':
				out += "\\\\";
				break;
			case '\t':
				out += "\\t";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			default:
				out += c;
		}
	}
	return out;
}

} // namespace

const std::string &Message::name() const {
	return fields.empty() ? EMPTY : fields[0];
}

const std::string &Message::arg(size_t i) const {
	return i + 1 < fields.size() ? fields[i + 1] : EMPTY;
}

int Message::arg_int(size_t i) const {
	return (int)strtol(arg(i).c_str(), nullptr, 10);
}

double Message::arg_double(size_t i) const {
	return strtod(arg(i).c_str(), nullptr);
}

Message &Message::add(const std::string &value) {
	fields.push_back(value);
	return *this;
}

Message &Message::add(int value) {
	fields.push_back(std::to_string(value));
	return *this;
}

Message &Message::add(double value) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%.17g", value);
	fields.push_back(buf);
	return *this;
}

std::string encode_line(const Message &message) {
	std::string line;
	for (size_t i = 0; i < message.fields.size(); i++) {
		if (i > 0) {
			line += '\t';
		}
		line += escape(message.fields[i]);
	}
	line += '\n';
	return line;
}

Message decode_line(const std::string &line) {
	Message message;
	std::string field;
	for (size_t i = 0; i < line.size(); i++) {
		char c = line[i];
		if (c == '\t') {
			message.fields.push_back(field);
			field.clear();
		} else if (c == '\\' && i + 1 < line.size()) {
			char next = line[++i];
			field += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
		} else if (c != '\r') {
			field += c;
		}
	}
	message.fields.push_back(field);
	return message;
}

void take_lines(std::string &buffer, std::vector<Message> &r_messages) {
	size_t start = 0;
	while (true) {
		size_t end = buffer.find('\n', start);
		if (end == std::string::npos) {
			break;
		}
		if (end > start) {
			r_messages.push_back(decode_line(buffer.substr(start, end - start)));
		}
		start = end + 1;
	}
	buffer.erase(0, start);
}

Message event_message(const SongoEvent &event) {
	switch (event.type) {
		case SONGO_EVENT_STATE:
			return Message("state").add(event.index);
		case SONGO_EVENT_TRACK:
			return Message("track").add(event.index).add(event.flags).add(event.value).add(event.text);
		case SONGO_EVENT_TITLE:
			return Message("title").add(event.text);
		case SONGO_EVENT_ERROR:
			return Message("error").add(event.index).add(event.text);
		case SONGO_EVENT_QUEUE: {
			Message message("queue");
			message.add(event.index);
			for (int i = 0; i < event.path_count; i++) {
				message.add(event.paths[i]);
			}
			return message;
		}
		case SONGO_EVENT_QUEUE_END:
			return Message("queue_end");
	}
	return Message("unknown");
}

std::string default_socket_path() {
#ifdef _WIN32
	// No daemon on Windows, so nothing listens anywhere
	return std::string();
#else
	const char *override_path = getenv("SONGO_PLAYER_SOCKET");
	if (override_path && override_path[0]) {
		return override_path;
	}
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir && runtime_dir[0] && access(runtime_dir, W_OK) == 0) {
		return std::string(runtime_dir) + "/songo_player.sock";
	}
	return "/tmp/songo_player-" + std::to_string(getuid()) + ".sock";
#endif
}

} // namespace songo
