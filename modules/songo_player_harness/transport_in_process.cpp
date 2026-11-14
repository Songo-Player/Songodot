#include "transport.h"

namespace songo {

namespace {

class InProcessTransport : public Transport {
public:
	InProcessTransport(const EngineApi &p_api, SongoEngine *p_engine) :
			api(p_api), engine(p_engine) {
		subscription = api.subscribe(engine);
		size_t size = 0;
		Shared *block = static_cast<Shared *>(api.shared(engine, &size));
		if (shared_is_compatible(block, size)) {
			block_ptr = block;
		}
		announced = false;
	}

	~InProcessTransport() override {
		api.subscription_close(subscription);
		// The Android service may still hold it, so this only stops the
		// engine if we were the last user
		api.release_shared(engine);
	}

	void send(const Message &message, bool) override {
		dispatch_command(api, engine, message);
	}

	void poll(std::vector<Message> &r_events) override {
		if (!announced) {
			announced = true;
			r_events.push_back(Message("connected"));
		}
		SongoEvent event;
		while (api.subscription_poll(subscription, &event)) {
			r_events.push_back(event_message(event));
		}
	}

	Shared *shared() override { return block_ptr; }
	bool is_connected() const override { return true; }

private:
	const EngineApi &api;
	SongoEngine *engine;
	SongoSubscription *subscription = nullptr;
	Shared *block_ptr = nullptr;
	bool announced;
};

} // namespace

std::unique_ptr<Transport> create_in_process_transport(const EngineApi &api, const EngineSettings &settings, std::string &r_error) {
	SongoEngineConfig config = {};
	config.backend = settings.backend.empty() ? nullptr : settings.backend.c_str();
	config.device = settings.device.empty() ? nullptr : settings.device.c_str();
	SongoEngine *engine = api.acquire_shared(&config);
	if (!engine) {
		r_error = "Couldn't start the music engine (audio backend \"" + settings.backend + "\")";
		return nullptr;
	}
	return std::make_unique<InProcessTransport>(api, engine);
}

} // namespace songo
