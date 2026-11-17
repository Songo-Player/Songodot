// Copied from SongoBGPlayer's engine/include/songo_shared.h. Keep in sync: the module checks
// SONGO_ENGINE_API_VERSION / SHARED_VERSION against the engine it loads.

// The block of memory the engine publishes its live state to: playback
// status, a spectrum matching Godot's AudioEffectSpectrumAnalyzer, and a
// waveform ring. In-process hosts get a pointer to it; other processes map the
// memfd the daemon hands them.
//
// The engine thread is the only writer, apart from reader_heartbeat_ns.
// Readers never block it: every part is guarded by a seqlock, so a reader
// that races a write just reads again. No locks live in here, so either side
// can die at any point without wedging the other.
//
// All times are CLOCK_MONOTONIC nanoseconds, which every process on a device
// shares, so readers in another process can compare them to their own clock.

#ifndef SONGO_SHARED_H
#define SONGO_SHARED_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <type_traits>

namespace songo {

constexpr uint32_t SHARED_MAGIC = 0x534f4e47; // "SONG"
// Bump on any layout change, readers refuse a block with another version
constexpr uint32_t SHARED_VERSION = 2;

// Bins per channel, as AudioEffectSpectrumAnalyzer's default FFT_SIZE_1024.
// Each spectrum frame analyses twice this many audio frames.
constexpr int FFT_SIZE = 1024;
// ~1.5s of history at 44.1kHz, comfortably more than any output latency
constexpr int SPECTRUM_FRAMES = 32;
// ~1.5s of stereo audio at 44.1kHz, for oscilloscope style visualizers
constexpr int WAVE_FRAMES = 65536;
// The most the engine writes to the waveform ring at once
constexpr int WAVE_WRITE_MARGIN = 8192;
constexpr int EQ_BANDS = 10;

enum StatusFlags : uint32_t {
	STATUS_LIVE = 1 << 0,
	STATUS_SEEKABLE = 1 << 1,
	STATUS_REPEAT_ONE = 1 << 2,
	STATUS_LOOP_QUEUE = 1 << 3,
	STATUS_EQ_ENABLED = 1 << 4,
	STATUS_EQ_AUTO_PREAMP = 1 << 5,
};

struct Status {
	uint32_t state; // SongoState
	uint32_t flags; // StatusFlags
	int32_t index; // current queue index, -1 for none
	int32_t queue_length;
	// Bumped every time a song starts, so readers can tell a restart apart
	uint64_t track_serial;
	// What's coming out of the speaker, `position` seconds into the song at
	// position_time_ns. Readers extrapolate from there while playing.
	double position;
	int64_t position_time_ns;
	// <= 0 for live streams and unknown lengths
	double duration;
	double blend_seconds;
	float volume;
	float eq_gains_db[EQ_BANDS];
	float eq_output_gain_db;
};

struct SpectrumFrame {
	std::atomic<uint32_t> seq;
	// Shared::generation when written. Frames from before a flush are stale.
	uint32_t generation;
	// When the last audio frame of this block reaches the speaker
	int64_t audible_time_ns;
	// |X[k]| / FFT_SIZE per channel, exactly as Godot's analyzer stores it
	float magnitude[2][FFT_SIZE];
};

struct Shared {
	uint32_t magic;
	uint32_t version;
	uint32_t size;
	uint32_t sample_rate;
	uint32_t fft_size;
	uint32_t spectrum_frames;
	uint32_t wave_frames;
	uint32_t reserved;

	std::atomic<uint32_t> status_seq;
	Status status;

	// Readers bump this while they're using the spectrum or waveform. The
	// engine skips analysing audio nobody has looked at for a while.
	std::atomic<int64_t> reader_heartbeat_ns;

	// Bumped when queued audio is thrown away (skip, seek, stop), which makes
	// every spectrum frame and waveform anchor from before it stale
	std::atomic<uint32_t> generation;

	// Spectrum frames written so far. The newest is at (count - 1) % SPECTRUM_FRAMES.
	std::atomic<uint64_t> spectrum_count;
	SpectrumFrame spectrum[SPECTRUM_FRAMES];

	// Waveform frame `wave_anchor_frame` reaches the speaker at
	// wave_anchor_time_ns. Frame numbers count up forever, the ring holds the
	// last WAVE_FRAMES of them at frame % WAVE_FRAMES.
	std::atomic<uint32_t> wave_anchor_seq;
	uint32_t wave_anchor_generation;
	uint64_t wave_anchor_frame;
	int64_t wave_anchor_time_ns;
	std::atomic<uint64_t> wave_count;
	float wave[WAVE_FRAMES][2];
};

// Shared is mapped into processes built separately (x86_64 and arm64, GCC and
// clang), so the atomics must be plain memory on every target
static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomics must be lock free to share across processes");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "atomics must be lock free to share across processes");
static_assert(std::atomic<int64_t>::is_always_lock_free, "atomics must be lock free to share across processes");
static_assert(std::is_standard_layout<Shared>::value, "Shared is shared across processes");

inline int64_t monotonic_ns() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

// ---- Seqlock ----
// The writer makes the sequence odd while it writes. A reader copies the data
// out and keeps it only if the sequence was even and unchanged throughout.

inline void seq_write_begin(std::atomic<uint32_t> &seq) {
	seq.store(seq.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);
}

inline void seq_write_end(std::atomic<uint32_t> &seq) {
	std::atomic_thread_fence(std::memory_order_release);
	seq.store(seq.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

// Copies `size` bytes guarded by `seq` into `dst`. False if the writer kept
// getting in the way, which only happens if it's writing nonstop.
inline bool seq_read(const std::atomic<uint32_t> &seq, const void *src, void *dst, size_t size) {
	for (int attempt = 0; attempt < 64; attempt++) {
		uint32_t before = seq.load(std::memory_order_acquire);
		if (before & 1) {
			continue;
		}
		memcpy(dst, src, size);
		std::atomic_thread_fence(std::memory_order_acquire);
		if (seq.load(std::memory_order_relaxed) == before) {
			return true;
		}
	}
	return false;
}

// ---- Reader helpers ----

// False if `shared` isn't a block this build understands
inline bool shared_is_compatible(const Shared *shared, size_t mapped_size) {
	return shared && mapped_size >= sizeof(Shared) && shared->magic == SHARED_MAGIC &&
			shared->version == SHARED_VERSION && shared->size == sizeof(Shared);
}

inline bool read_status(const Shared *shared, Status &r_status) {
	return seq_read(shared->status_seq, &shared->status, &r_status, sizeof(Status));
}

// Seconds into the song the speaker is at `now_ns`
inline double status_position_at(const Status &status, int64_t now_ns) {
	double position = status.position;
	if (status.state == 2 /* SONGO_STATE_PLAYING */ && now_ns > status.position_time_ns) {
		position += (double)(now_ns - status.position_time_ns) / 1e9;
	}
	if (status.duration > 0.0 && position > status.duration) {
		position = status.duration;
	}
	return position < 0.0 ? 0.0 : position;
}

inline void touch_heartbeat(Shared *shared, int64_t now_ns) {
	shared->reader_heartbeat_ns.store(now_ns, std::memory_order_relaxed);
}

// Copies the newest spectrum frame that's reached the speaker by `now_ns`
// into `r_magnitude`. False if there isn't one, e.g. right after a skip.
inline bool read_audible_spectrum(const Shared *shared, int64_t now_ns, float (&r_magnitude)[2][FFT_SIZE]) {
	uint32_t generation = shared->generation.load(std::memory_order_acquire);
	uint64_t count = shared->spectrum_count.load(std::memory_order_acquire);
	uint64_t oldest = count > SPECTRUM_FRAMES ? count - SPECTRUM_FRAMES : 0;
	for (uint64_t n = count; n > oldest; n--) {
		const SpectrumFrame &frame = shared->spectrum[(n - 1) % SPECTRUM_FRAMES];
		for (int attempt = 0; attempt < 64; attempt++) {
			uint32_t before = frame.seq.load(std::memory_order_acquire);
			if (before & 1) {
				continue;
			}
			uint32_t frame_generation = frame.generation;
			int64_t audible = frame.audible_time_ns;
			bool usable = frame_generation == generation && audible <= now_ns;
			if (usable) {
				memcpy(r_magnitude, frame.magnitude, sizeof(frame.magnitude));
			}
			std::atomic_thread_fence(std::memory_order_acquire);
			if (frame.seq.load(std::memory_order_relaxed) != before) {
				continue;
			}
			if (frame_generation != generation) {
				// Everything older is from before the flush too
				return false;
			}
			if (usable) {
				return true;
			}
			break;
		}
	}
	return false;
}

// Copies the `frame_count` stereo frames leading up to what the speaker is
// playing at `now_ns` into `r_frames`, oldest first. False if that audio isn't
// available (nothing played yet, or right after a skip).
inline bool read_audible_waveform(const Shared *shared, int64_t now_ns, int frame_count, float (*r_frames)[2]) {
	if (frame_count <= 0 || frame_count > WAVE_FRAMES / 2 || shared->sample_rate == 0) {
		return false;
	}
	struct Anchor {
		uint32_t generation;
		uint64_t frame;
		int64_t time_ns;
	} anchor;
	for (int attempt = 0;; attempt++) {
		if (attempt == 64) {
			return false;
		}
		uint32_t before = shared->wave_anchor_seq.load(std::memory_order_acquire);
		if (before & 1) {
			continue;
		}
		anchor.generation = shared->wave_anchor_generation;
		anchor.frame = shared->wave_anchor_frame;
		anchor.time_ns = shared->wave_anchor_time_ns;
		std::atomic_thread_fence(std::memory_order_acquire);
		if (shared->wave_anchor_seq.load(std::memory_order_relaxed) == before) {
			break;
		}
	}
	if (anchor.generation != shared->generation.load(std::memory_order_acquire)) {
		return false;
	}

	int64_t offset = (now_ns - anchor.time_ns) * (int64_t)shared->sample_rate / 1000000000;
	int64_t end = (int64_t)anchor.frame + offset;
	uint64_t written = shared->wave_count.load(std::memory_order_acquire);
	if (end > (int64_t)written) {
		end = (int64_t)written;
	}
	int64_t start = end - frame_count;
	if (start < 0) {
		return false;
	}
	for (int i = 0; i < frame_count; i++) {
		const float *src = shared->wave[(uint64_t)(start + i) % WAVE_FRAMES];
		r_frames[i][0] = src[0];
		r_frames[i][1] = src[1];
	}
	// If the writer lapped us while copying, part of it is newer audio. The
	// margin covers a chunk it's partway through writing, which isn't
	// counted in wave_count yet.
	std::atomic_thread_fence(std::memory_order_acquire);
	written = shared->wave_count.load(std::memory_order_relaxed);
	return (int64_t)written - start <= WAVE_FRAMES - WAVE_WRITE_MARGIN;
}

} // namespace songo

#endif // SONGO_SHARED_H
