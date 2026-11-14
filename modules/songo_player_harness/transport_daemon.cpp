#include "transport.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace songo {

namespace {

// How long a daemon we started gets to start listening
const int64_t SPAWN_TIMEOUT_NS = 5000000000;
const int64_t RETRY_INTERVAL_NS = 50000000;

class DaemonTransport : public Transport {
public:
	explicit DaemonTransport(const DaemonSettings &p_settings) :
			settings(p_settings) {
		if (settings.socket_path.empty()) {
			settings.socket_path = default_socket_path();
		}
		// Pick up a daemon that's already playing, but don't start one just
		// because Songo opened
		try_connect();
	}

	~DaemonTransport() override {
		// Writes whatever's queued (a last stop, say) before going
		if (fd >= 0) {
			flush(true);
		}
		disconnect();
	}

	void send(const Message &message, bool start_engine) override {
		out += encode_line(message);
		if (fd < 0 && !waiting_for_daemon && start_engine) {
			start_daemon();
		}
		if (fd >= 0) {
			flush(false);
		}
	}

	void poll(std::vector<Message> &r_events) override {
		if (!events.empty()) {
			r_events.insert(r_events.end(), events.begin(), events.end());
			events.clear();
		}
		if (fd < 0 && waiting_for_daemon) {
			int64_t now = monotonic_ns();
			if (now >= next_try_ns) {
				next_try_ns = now + RETRY_INTERVAL_NS;
				if (!try_connect() && now >= give_up_ns) {
					waiting_for_daemon = false;
					out.clear();
					r_events.push_back(Message("error").add(-1).add("The background player didn't start (" + settings.daemon_path + ")"));
				}
			}
		}
		if (fd < 0) {
			return;
		}
		receive(r_events);
		if (fd >= 0) {
			flush(false);
		}
		r_events.insert(r_events.end(), events.begin(), events.end());
		events.clear();
	}

	Shared *shared() override { return block; }
	bool is_connected() const override { return fd >= 0; }

private:
	DaemonSettings settings;
	int fd = -1;
	std::string in;
	std::string out;
	// Arrived with the hello, mapped once we know its size
	int received_fd = -1;
	Shared *block = nullptr;
	size_t block_size = 0;
	bool waiting_for_daemon = false;
	int64_t next_try_ns = 0;
	int64_t give_up_ns = 0;
	std::vector<Message> events;

	bool try_connect() {
		sockaddr_un addr = {};
		addr.sun_family = AF_UNIX;
		if (settings.socket_path.size() >= sizeof(addr.sun_path)) {
			return false;
		}
		memcpy(addr.sun_path, settings.socket_path.c_str(), settings.socket_path.size());
		int s = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (s < 0) {
			return false;
		}
		if (connect(s, (sockaddr *)&addr, sizeof(addr)) < 0) {
			close(s);
			return false;
		}
		fd = s;
		waiting_for_daemon = false;
		in.clear();
		events.push_back(Message("connected"));
		return true;
	}

	void disconnect() {
		if (fd >= 0) {
			close(fd);
			fd = -1;
		}
		if (received_fd >= 0) {
			close(received_fd);
			received_fd = -1;
		}
		if (block) {
			munmap(block, block_size);
			block = nullptr;
			block_size = 0;
		}
		in.clear();
	}

	void lost_connection() {
		disconnect();
		events.push_back(Message("disconnected"));
	}

	// Starts songo_bgplayer detached from us, in its own session, so it
	// carries on after Songo exits and isn't left as our zombie
	void start_daemon() {
		if (settings.daemon_path.empty()) {
			out.clear();
			events.push_back(Message("error").add(-1).add("No background player to start"));
			return;
		}
		if (access(settings.daemon_path.c_str(), X_OK) != 0) {
			// Exports don't always keep the executable bit
			chmod(settings.daemon_path.c_str(), 0755);
		}

		size_t slash = settings.socket_path.find_last_of('/');
		std::string log = (slash == std::string::npos ? std::string(".") : settings.socket_path.substr(0, slash)) + "/songo_bgplayer.log";
		std::vector<std::string> args = { settings.daemon_path, "--idle", "--socket", settings.socket_path, "--log", log };
		if (!settings.engine.backend.empty()) {
			args.insert(args.end(), { "--backend", settings.engine.backend });
		}
		if (!settings.engine.device.empty()) {
			args.insert(args.end(), { "--device", settings.engine.device });
		}
		if (settings.exit_with_app) {
			args.push_back("--exit-on-disconnect");
		}
		// Built before forking: only async-signal-safe calls between fork and exec
		std::vector<char *> argv;
		for (std::string &arg : args) {
			argv.push_back(arg.data());
		}
		argv.push_back(nullptr);
		const char *path = settings.daemon_path.c_str();

		pid_t child = fork();
		if (child == 0) {
			setsid();
			if (fork() != 0) {
				_exit(0);
			}
			int null_fd = open("/dev/null", O_RDWR);
			if (null_fd >= 0) {
				dup2(null_fd, 0);
				dup2(null_fd, 1);
				dup2(null_fd, 2);
			}
			execv(path, argv.data());
			_exit(127);
		}
		if (child > 0) {
			waitpid(child, nullptr, 0);
		}

		waiting_for_daemon = true;
		int64_t now = monotonic_ns();
		next_try_ns = now + RETRY_INTERVAL_NS;
		give_up_ns = now + SPAWN_TIMEOUT_NS;
	}

	void flush(bool blocking) {
		while (fd >= 0 && !out.empty()) {
			ssize_t n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL | (blocking ? 0 : MSG_DONTWAIT));
			if (n > 0) {
				out.erase(0, (size_t)n);
			} else if (n < 0 && errno == EINTR) {
				continue;
			} else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && !blocking) {
				return;
			} else {
				lost_connection();
				return;
			}
		}
	}

	void receive(std::vector<Message> &r_events) {
		char buf[65536];
		while (fd >= 0) {
			iovec iov = { buf, sizeof(buf) };
			msghdr msg = {};
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 4)];
			msg.msg_control = control;
			msg.msg_controllen = sizeof(control);
			ssize_t n = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
				break;
			}
			if (n <= 0) {
				lost_connection();
				return;
			}
			for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
				if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
					int count = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
					for (int i = 0; i < count; i++) {
						int passed;
						memcpy(&passed, CMSG_DATA(cmsg) + i * sizeof(int), sizeof(int));
						if (received_fd < 0) {
							received_fd = passed;
						} else {
							close(passed);
						}
					}
				}
			}
			in.append(buf, (size_t)n);
		}

		std::vector<Message> messages;
		take_lines(in, messages);
		for (const Message &message : messages) {
			if (message.name() == "hello") {
				if (!handle_hello(message, r_events)) {
					return;
				}
			} else {
				r_events.push_back(message);
			}
		}
	}

	bool handle_hello(const Message &hello, std::vector<Message> &r_events) {
		if (hello.arg_int(0) != PROTOCOL_VERSION) {
			// A daemon from another Songo version is still running. Ask it to
			// go and start ours, keeping what was queued to send.
			std::string queued = out;
			out = encode_line(Message("quit"));
			flush(true);
			disconnect();
			out = queued;
			r_events.push_back(Message("disconnected"));
			start_daemon();
			return false;
		}
		size_t size = (size_t)hello.arg_double(1);
		if (received_fd >= 0 && size >= sizeof(Shared)) {
			void *memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, received_fd, 0);
			if (memory != MAP_FAILED) {
				if (shared_is_compatible(static_cast<Shared *>(memory), size)) {
					block = static_cast<Shared *>(memory);
					block_size = size;
				} else {
					munmap(memory, size);
				}
			}
		}
		if (received_fd >= 0) {
			close(received_fd);
			received_fd = -1;
		}
		if (!block) {
			r_events.push_back(Message("error").add(-1).add("Couldn't map the background player's shared memory, visualizers won't work"));
		}
		return true;
	}
};

} // namespace

std::unique_ptr<Transport> create_daemon_transport(const DaemonSettings &settings) {
	return std::make_unique<DaemonTransport>(settings);
}

} // namespace songo
