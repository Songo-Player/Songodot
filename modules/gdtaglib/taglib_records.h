#ifndef GDTAGLIB_TAGLIB_RECORDS_H
#define GDTAGLIB_TAGLIB_RECORDS_H

// The music library: a compact native store (TagLibLibrary) plus the small
// script-facing objects that read from it.
//
// Memory model. A library of N tracks is a handful of flat arrays: one
// 72-byte TLTrack per file, interned UTF-8 strings (each distinct title,
// album, directory... stored once), and albums / artists as index ranges.
// Nothing per track is an Object. Scripts see tracks through
// TagLibMusicRecord handles, made on demand and shared while referenced
// (asking twice for the same track gives the same instance, so == / in /
// Array.has keep working), and through TagLibTrackList, a list of track
// indices that only makes handles for the items actually read.
//
// Threading. TagLibLibrary is guarded by a read/write lock, so handles can
// be read on the main thread while an import thread runs update(). Lists and
// handles themselves are not synchronized, like Array.

#include "tag_reader.h"

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/rw_lock.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"

class TagLibMusicRecord;
class TagLibAlbumRecord;
class TagLibArtistRecord;
class TagLibTrackList;

// ====================================================================
// Store internals
// ====================================================================

// Exactly-sized vector for the store's big arrays. LocalVector's default
// rounds capacity up to a power of two (up to 2x slack); this one holds what
// a loaded library uses, and grows by half with tl_push / tl_reserve_more
// while importing.
template <typename T>
using TLVec = LocalVector<T, uint32_t, false, true>;

template <typename T>
void tl_reserve_more(TLVec<T> &r_vec, uint32_t p_extra) {
	uint32_t need = r_vec.size() + p_extra;
	if (need > r_vec.get_capacity()) {
		r_vec.reserve(MAX(need, r_vec.get_capacity() + r_vec.get_capacity() / 2 + 16));
	}
}

template <typename T>
void tl_push(TLVec<T> &r_vec, const T &p_value) {
	tl_reserve_more(r_vec, 1);
	r_vec.push_back(p_value);
}

// Interned UTF-8 strings addressed by a 32-bit id. id 0 is always "".
class TLStringPool {
	TLVec<char> bytes;
	TLVec<uint32_t> offsets; // count + 1 entries; string i is [offsets[i], offsets[i + 1])
	LocalVector<int32_t> table; // open addressing over ids, -1 = empty; size is a power of two

	void _table_insert(uint32_t p_id);
	void _grow_table();

public:
	TLStringPool() { clear(); }
	void clear();

	uint32_t count() const { return offsets.size() - 1; }
	const char *ptr(uint32_t p_id) const { return bytes.ptr() + offsets[p_id]; }
	uint32_t length(uint32_t p_id) const { return offsets[p_id + 1] - offsets[p_id]; }
	String get(uint32_t p_id) const;
	bool equals(uint32_t p_id, const char *p_str, uint32_t p_len) const;

	int64_t find(const char *p_str, uint32_t p_len) const; // -1 when absent
	uint32_t intern(const char *p_str, uint32_t p_len);
	uint32_t intern(const String &p_str);

	// Raw storage, for the file format. Loading sizes the storage, fills it
	// through the returned pointers, then calls finish_load() (after
	// validating) to rebuild the hash table.
	const TLVec<char> &get_bytes() const { return bytes; }
	const TLVec<uint32_t> &get_offsets() const { return offsets; }
	char *prepare_bytes(uint32_t p_length);
	uint32_t *prepare_offsets(uint32_t p_count);
	bool validate_loaded() const;
	void finish_load() { _grow_table(); }

	size_t memory_usage() const;
};

enum : uint8_t {
	TL_TRACK_ALIVE = 1,
};

// One music file. Strings are TLStringPool ids. full_path is dir + file,
// with dir keeping its trailing '/'. Written to disk as-is (little endian).
struct TLTrack {
	int64_t mtime = 0; // 0 = unknown
	int64_t size = -1; // -1 = unknown
	uint32_t dir = 0;
	uint32_t file = 0;
	uint32_t title = 0;
	uint32_t album = 0;
	uint32_t artist = 0;
	uint32_t album_artist = 0;
	uint32_t genre = 0;
	uint32_t mbid = 0; // MusicBrainz album id
	float length = 0.0f; // seconds
	uint32_t listen_count = 0;
	uint32_t sample_rate = 0;
	uint16_t bitrate = 0; // kb/s
	int16_t track = 0;
	int16_t disc = 0;
	int16_t year = 0;
	uint8_t channels = 0;
	uint8_t flags = 0;
	uint8_t pad[2] = { 0, 0 };
};
static_assert(sizeof(TLTrack) == 72, "TLTrack is part of the library file format.");

// An album or artist. Track slots are group_tracks[tracks_begin ..+ tracks_count];
// an album's distinct track artists are group_names[names_begin ..+ names_count].
struct TLGroup {
	uint32_t name = 0;
	uint32_t album_artist = 0; // albums only
	uint32_t asset_id = 0;
	uint32_t names_begin = 0;
	uint32_t names_count = 0;
	uint32_t tracks_begin = 0;
	uint32_t tracks_count = 0;
	uint32_t pad = 0;
};
static_assert(sizeof(TLGroup) == 32, "TLGroup is part of the library file format.");

// Everything persisted for a library.
struct TLData {
	TLStringPool strings;
	TLVec<TLTrack> tracks; // indexed by slot; removed tracks stay (not ALIVE) until compacted
	TLVec<uint32_t> title_rank; // per slot: case-insensitive title order
	TLVec<uint32_t> all_order; // live slots by title, for "All Songs"
	TLVec<TLGroup> albums; // display order
	TLVec<TLGroup> artists; // display order
	TLVec<uint32_t> group_tracks;
	TLVec<uint32_t> group_names;

	void clear();
};

// ====================================================================
// TagLibLibrary
// ====================================================================

class TagLibLibrary : public RefCounted {
	GDCLASS(TagLibLibrary, RefCounted);

	friend class TagLibMusicRecord;
	friend class TagLibTrackGroup;
	friend class TagLibTrackList;

public:
	enum StrField {
		S_PATH,
		S_TITLE,
		S_ALBUM,
		S_ARTIST,
		S_ALBUM_ARTIST,
		S_GENRE,
		S_MBID,
	};
	enum NumField {
		N_LENGTH,
		N_TRACK,
		N_DISC,
		N_YEAR,
		N_BITRATE,
		N_SAMPLE_RATE,
		N_CHANNELS,
		N_MTIME,
		N_SIZE,
		N_LISTEN_COUNT,
	};

private:
	mutable RWLock lock;
	TLData d;
	TLVec<int32_t> path_table; // live slots by (dir, file), open addressing, -1 = empty
	uint32_t live_count = 0;
	// Removed / re-read tracks left dead slots or orphaned strings; save()
	// compacts when set.
	bool garbage = false;

	// One live handle per slot at most, so identity comparisons hold.
	Mutex handle_mutex;
	HashMap<uint32_t, ObjectID> handles;

	// --- callers hold `lock` ---
	String _path_of(uint32_t p_slot) const;
	int64_t _find_slot(const String &p_path) const;
	void _rebuild_path_table();
	void _set_from_fields(TLTrack &r_track, const String &p_path, const TagFields &p_fields);
	void _regroup(const LocalVector<uint32_t> &p_order);
	void _compact_into(TLData &r_out) const;
	bool _validate_loaded() const;
	void _finish_load();

	// file format (library_store.cpp)
	static Error _write_file(const String &p_path, const TLData &p_data);
	Error _read_file(const String &p_path);

	// --- no `lock` needed ---
	Ref<TagLibMusicRecord> _handle(uint32_t p_slot);
	void _forget_handle(uint32_t p_slot, ObjectID p_id);
	TypedArray<TagLibMusicRecord> _handles_for(const LocalVector<uint32_t> &p_slots);

protected:
	static void _bind_methods();

public:
	// A library read from p_path. Empty when the file is missing, unreadable
	// or in another format version.
	static Ref<TagLibLibrary> load_file(const String &p_path);
	// Writes beside p_path then renames over it.
	Error save(const String &p_path);

	int get_track_count() const;
	int get_album_count() const;
	int get_artist_count() const;

	// Every track, by title. A new list each call.
	Ref<TagLibTrackList> get_all_tracks();
	// Album / artist records in display order. New instances each call.
	TypedArray<TagLibAlbumRecord> get_albums();
	TypedArray<TagLibArtistRecord> get_artists();

	// The library's record for an exact path, or null.
	Ref<TagLibMusicRecord> find_by_path(const String &p_path);
	bool has_path(const String &p_path) const;

	// Parse-free diff of the library against the files now on disk. Returns
	// { added, changed, removed } PackedStringArrays and emits GDTagLib's
	// library_update_planned (deferred) with the counts.
	// opts: force_reread (false), follow_retags (true: changed mtime/size
	// means changed), mtimes / sizes (scan_directories stamps parallel to
	// current_paths, used instead of stat(); size -1 entries are stat()ed).
	Dictionary plan_update(const PackedStringArray &p_current_paths, const Dictionary &p_opts);

	// Brings the library in line with current_paths, in place: reads tags for
	// new paths and for reread_paths (keeping their play counts), drops
	// tracks not in current_paths, then regroups albums / artists.
	// Existing records stay valid. progress / opts are as for
	// GDTagLib.build_music_records, plus force_reread. Returns { added,
	// removed, rescanned, changed_albums (album names of every track read),
	// cancelled }; a cancelled update changes nothing.
	Dictionary update(const PackedStringArray &p_current_paths, const PackedStringArray &p_reread_paths,
			const Callable &p_progress, const Dictionary &p_opts);

	// Approximate heap bytes held by the store (not handles or lists).
	int64_t get_memory_usage() const;

	// --- record accessors, used by the handle classes ---
	String track_string(uint32_t p_slot, StrField p_field) const;
	TLTrack track_copy(uint32_t p_slot) const;
	void set_track_string(uint32_t p_slot, StrField p_field, const String &p_value);
	void set_track_number(uint32_t p_slot, NumField p_field, double p_value);

	TagLibLibrary();
	~TagLibLibrary();
};

// Fills a standalone record from read tags, with the library's fallbacks
// (file name for a missing title, "Unknown Artist" / "Unknown Album").
void tl_apply_fields(TagLibMusicRecord *r_record, const String &p_path, const TagFields &p_fields);

// ====================================================================
// TagLibMusicRecord
// ====================================================================

// One track. Either a handle onto a TagLibLibrary track (what the library
// hands out), or standalone with its own fields (TagLibMusicRecord.new(),
// e.g. a web radio stream).
class TagLibMusicRecord : public RefCounted {
	GDCLASS(TagLibMusicRecord, RefCounted);

	friend class TagLibLibrary;
	friend class TagLibTrackGroup;
	friend class TagLibTrackList;

	struct Standalone {
		String full_path;
		String title;
		String album;
		String artist;
		String album_artist;
		String genre;
		String musicbrainz_album_id;
		double raw_length = 0.0;
		int track = 0;
		int disc = 0;
		int year = 0;
		int bitrate = 0;
		int sample_rate = 0;
		int channels = 0;
		int64_t file_mtime = 0;
		int64_t file_size = -1;
		int listen_count = 0;
	};

	Ref<TagLibLibrary> library; // set: a library handle
	uint32_t slot = 0;
	Standalone *own = nullptr; // standalone fields, allocated on first write

	Standalone &_own();
	String _str(TagLibLibrary::StrField p_field, String Standalone::*p_member) const;
	void _set_str(TagLibLibrary::StrField p_field, String Standalone::*p_member, const String &p_value);

protected:
	static void _bind_methods();

public:
	String get_full_path() const;
	void set_full_path(const String &p_value);
	String get_title() const;
	void set_title(const String &p_value);
	String get_album() const;
	void set_album(const String &p_value);
	String get_artist() const;
	void set_artist(const String &p_value);
	String get_album_artist() const;
	void set_album_artist(const String &p_value);
	String get_genre() const;
	void set_genre(const String &p_value);
	String get_musicbrainz_album_id() const;
	void set_musicbrainz_album_id(const String &p_value);

	double get_raw_length() const;
	void set_raw_length(double p_value);
	int get_track() const;
	void set_track(int p_value);
	int get_disc() const;
	void set_disc(int p_value);
	int get_year() const;
	void set_year(int p_value);
	int get_bitrate() const;
	void set_bitrate(int p_value);
	int get_sample_rate() const;
	void set_sample_rate(int p_value);
	int get_channels() const;
	void set_channels(int p_value);
	int64_t get_file_mtime() const;
	void set_file_mtime(int64_t p_value);
	int64_t get_file_size() const;
	void set_file_size(int64_t p_value);
	// Play count. User data, not from tags: kept across re-reads and saved
	// with the library.
	int get_listen_count() const;
	void set_listen_count(int p_value);

	// "m:ss" style duration, e.g. 63.0 -> "1:03".
	String get_length_string() const;
	// "07. Title" when track > 0, otherwise just the title.
	String get_title_with_track() const;
	// Lowercase file extension ("mp3", "flac", ...), or "stream" when
	// full_path is a URL.
	String get_file_type() const;

	bool is_library_track() const { return library.is_valid(); }
	Ref<TagLibLibrary> get_library() const { return library; }

	~TagLibMusicRecord();
};

// ====================================================================
// Albums and artists
// ====================================================================

// Shared base of TagLibAlbumRecord / TagLibArtistRecord: a named, ordered
// set of library tracks. A snapshot: regrouping the library (an import)
// makes new group records rather than changing these.
class TagLibTrackGroup : public RefCounted {
	GDCLASS(TagLibTrackGroup, RefCounted);

	friend class TagLibLibrary;

protected:
	Ref<TagLibLibrary> library;
	String name;
	String asset_id;
	LocalVector<uint32_t> slots;

	static void _bind_methods();
	void _copy_group_to(TagLibTrackGroup *r_other) const;

public:
	String get_name() const { return name; }
	void set_name(const String &p_value) { name = p_value; }
	String get_asset_id() const { return asset_id; }
	void set_asset_id(const String &p_value) { asset_id = p_value; }

	// The tracks as records, in group order. A new Array each call; prefer
	// track_list / track_count / total_length for large groups.
	TypedArray<TagLibMusicRecord> get_music_records() const;
	Ref<TagLibTrackList> get_track_list() const;
	int get_track_count() const { return (int)slots.size(); }
	double get_total_length() const;
	PackedStringArray get_paths() const;
	bool has_track(const Ref<TagLibMusicRecord> &p_record) const;
	virtual Ref<TagLibTrackGroup> duplicate() const = 0;
};

// A group of tracks that share an album.
class TagLibAlbumRecord : public TagLibTrackGroup {
	GDCLASS(TagLibAlbumRecord, TagLibTrackGroup);

	friend class TagLibLibrary;

	String album_artist;
	PackedStringArray artists; // every distinct per-track artist, first-seen order

protected:
	static void _bind_methods();

public:
	String get_album_artist() const { return album_artist; }
	void set_album_artist(const String &p_value) { album_artist = p_value; }
	PackedStringArray get_artists() const { return artists; }
	void set_artists(const PackedStringArray &p_value) { artists = p_value; }

	virtual Ref<TagLibTrackGroup> duplicate() const override;
};

// A group of tracks attributed to one album artist. Tracks on "Various
// Artists" albums are not filed under any artist.
class TagLibArtistRecord : public TagLibTrackGroup {
	GDCLASS(TagLibArtistRecord, TagLibTrackGroup);

protected:
	static void _bind_methods() {}

public:
	virtual Ref<TagLibTrackGroup> duplicate() const override;
};

// ====================================================================
// TagLibTrackList
// ====================================================================

// An ordered list of tracks that stores 4 bytes per library track and only
// makes TagLibMusicRecord handles for the items read. Usable much like an
// Array: size(), list[i], `for record in list`, append / insert / erase,
// duplicate(), shuffle(). Standalone records (or another library's) can be
// mixed in.
class TagLibTrackList : public RefCounted {
	GDCLASS(TagLibTrackList, RefCounted);

	friend class TagLibLibrary;
	friend class TagLibTrackGroup;

	Ref<TagLibLibrary> library;
	// >= 0: a slot in `library`; < 0: -(i + 1) indexes `extras`.
	LocalVector<int32_t> entries;
	LocalVector<Ref<TagLibMusicRecord>> extras;

	int32_t _entry_for(const Ref<TagLibMusicRecord> &p_record);
	Ref<TagLibMusicRecord> _record_for(int32_t p_entry) const;
	bool _entry_is(int32_t p_entry, const Ref<TagLibMusicRecord> &p_record) const;
	bool _fix_index(int64_t &r_index, bool p_allow_end = false) const;

protected:
	static void _bind_methods();

public:
	virtual Variant getvar(const Variant &p_key, bool *r_valid = nullptr) const override;
	virtual void setvar(const Variant &p_key, const Variant &p_value, bool *r_valid = nullptr) override;

	int size() const { return (int)entries.size(); }
	bool is_empty() const { return entries.is_empty(); }
	void clear();

	Ref<TagLibMusicRecord> get_record(int64_t p_index) const;
	void set_record(int64_t p_index, const Ref<TagLibMusicRecord> &p_record);
	void append(const Ref<TagLibMusicRecord> &p_record);
	// Appends an Array of records or another TagLibTrackList.
	void append_records(const Variant &p_records);
	void insert(int64_t p_index, const Ref<TagLibMusicRecord> &p_record);
	void remove_at(int64_t p_index);
	void erase(const Ref<TagLibMusicRecord> &p_record);

	bool has(const Ref<TagLibMusicRecord> &p_record) const;
	int find(const Ref<TagLibMusicRecord> &p_record, int64_t p_from = 0) const;
	int find_path(const String &p_path) const;
	String get_path_at(int64_t p_index) const;
	PackedStringArray get_paths() const;
	double get_total_length() const;

	Ref<TagLibTrackList> duplicate() const;
	Ref<TagLibTrackList> slice(int64_t p_begin, int64_t p_end) const;
	void shuffle();
	void reverse();
	// Stable sort by "title", "track" (disc, track, title), "album",
	// "artist", "album_artist", "genre", "year", "length", "listen_count" or
	// "path". Text compares ignore case.
	void sort_by(const String &p_key, bool p_descending = false);
	void sort_custom(const Callable &p_less);
	TypedArray<TagLibMusicRecord> to_array() const;

	Ref<TagLibLibrary> get_library() const { return library; }

	// A new list holding p_records: an Array of records or a TagLibTrackList
	// (copied).
	static Ref<TagLibTrackList> from_records(const Variant &p_records);

	bool _iter_init(const Array &p_iter);
	bool _iter_next(const Array &p_iter);
	Variant _iter_get(const Variant &p_iter) const;
};

#endif // GDTAGLIB_TAGLIB_RECORDS_H
