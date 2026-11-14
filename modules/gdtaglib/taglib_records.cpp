#include "taglib_records.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

#include <algorithm>

// ====================================================================
// TagLibMusicRecord
// ====================================================================

#define TLR_BIND(m_prop, m_variant_type)                                                       \
	ClassDB::bind_method(D_METHOD("set_" #m_prop, "value"), &TagLibMusicRecord::set_##m_prop); \
	ClassDB::bind_method(D_METHOD("get_" #m_prop), &TagLibMusicRecord::get_##m_prop);          \
	ADD_PROPERTY(PropertyInfo(m_variant_type, #m_prop), "set_" #m_prop, "get_" #m_prop);

void TagLibMusicRecord::_bind_methods() {
	TLR_BIND(full_path, Variant::STRING);
	TLR_BIND(title, Variant::STRING);
	TLR_BIND(album, Variant::STRING);
	TLR_BIND(artist, Variant::STRING);
	TLR_BIND(album_artist, Variant::STRING);
	TLR_BIND(genre, Variant::STRING);
	TLR_BIND(musicbrainz_album_id, Variant::STRING);
	TLR_BIND(raw_length, Variant::FLOAT);
	TLR_BIND(track, Variant::INT);
	TLR_BIND(disc, Variant::INT);
	TLR_BIND(year, Variant::INT);
	TLR_BIND(bitrate, Variant::INT);
	TLR_BIND(sample_rate, Variant::INT);
	TLR_BIND(channels, Variant::INT);
	TLR_BIND(file_mtime, Variant::INT);
	TLR_BIND(file_size, Variant::INT);
	TLR_BIND(listen_count, Variant::INT);

	ClassDB::bind_method(D_METHOD("get_length_string"), &TagLibMusicRecord::get_length_string);
	ClassDB::bind_method(D_METHOD("get_title_with_track"), &TagLibMusicRecord::get_title_with_track);
	ClassDB::bind_method(D_METHOD("get_file_type"), &TagLibMusicRecord::get_file_type);
	// Derived, so readable as a property but never serialized.
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "file_type", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
			"", "get_file_type");

	ClassDB::bind_method(D_METHOD("is_library_track"), &TagLibMusicRecord::is_library_track);
	ClassDB::bind_method(D_METHOD("get_library"), &TagLibMusicRecord::get_library);
}

#undef TLR_BIND

TagLibMusicRecord::~TagLibMusicRecord() {
	if (library.is_valid()) {
		library->_forget_handle(slot, get_instance_id());
	}
	if (own) {
		memdelete(own);
	}
}

TagLibMusicRecord::Standalone &TagLibMusicRecord::_own() {
	if (!own) {
		own = memnew(Standalone);
	}
	return *own;
}

String TagLibMusicRecord::_str(TagLibLibrary::StrField p_field, String Standalone::*p_member) const {
	if (library.is_valid()) {
		return library->track_string(slot, p_field);
	}
	return own ? own->*p_member : String();
}

void TagLibMusicRecord::_set_str(TagLibLibrary::StrField p_field, String Standalone::*p_member, const String &p_value) {
	if (library.is_valid()) {
		library->set_track_string(slot, p_field, p_value);
	} else {
		_own().*p_member = p_value;
	}
}

#define TLR_STR(m_name, m_field)                                                       \
	String TagLibMusicRecord::get_##m_name() const {                                   \
		return _str(TagLibLibrary::m_field, &Standalone::m_name);                      \
	}                                                                                  \
	void TagLibMusicRecord::set_##m_name(const String &p_value) {                      \
		_set_str(TagLibLibrary::m_field, &Standalone::m_name, p_value);                \
	}

TLR_STR(full_path, S_PATH)
TLR_STR(title, S_TITLE)
TLR_STR(album, S_ALBUM)
TLR_STR(artist, S_ARTIST)
TLR_STR(album_artist, S_ALBUM_ARTIST)
TLR_STR(genre, S_GENRE)
TLR_STR(musicbrainz_album_id, S_MBID)

#undef TLR_STR

// m_expr reads the library value from `t` (a TLTrack copy).
#define TLR_NUM(m_type, m_name, m_field, m_expr)                             \
	m_type TagLibMusicRecord::get_##m_name() const {                         \
		if (library.is_valid()) {                                            \
			TLTrack t = library->track_copy(slot);                           \
			return (m_type)(m_expr);                                         \
		}                                                                    \
		return own ? own->m_name : Standalone().m_name;                      \
	}                                                                        \
	void TagLibMusicRecord::set_##m_name(m_type p_value) {                   \
		if (library.is_valid()) {                                            \
			library->set_track_number(slot, TagLibLibrary::m_field, p_value); \
		} else {                                                             \
			_own().m_name = p_value;                                         \
		}                                                                    \
	}

TLR_NUM(double, raw_length, N_LENGTH, t.length)
TLR_NUM(int, track, N_TRACK, t.track)
TLR_NUM(int, disc, N_DISC, t.disc)
TLR_NUM(int, year, N_YEAR, t.year)
TLR_NUM(int, bitrate, N_BITRATE, t.bitrate)
TLR_NUM(int, sample_rate, N_SAMPLE_RATE, t.sample_rate)
TLR_NUM(int, channels, N_CHANNELS, t.channels)
TLR_NUM(int64_t, file_mtime, N_MTIME, t.mtime)
TLR_NUM(int64_t, file_size, N_SIZE, t.size)
TLR_NUM(int, listen_count, N_LISTEN_COUNT, t.listen_count)

#undef TLR_NUM

static String _pad2(int p_value) {
	String s = itos(p_value);
	if (s.length() < 2) {
		s = "0" + s;
	}
	return s;
}

String TagLibMusicRecord::get_length_string() const {
	int total = MAX((int)get_raw_length(), 0);
	return itos(total / 60) + ":" + _pad2(total % 60);
}

String TagLibMusicRecord::get_title_with_track() const {
	int t = get_track();
	if (t <= 0) {
		return get_title();
	}
	return _pad2(t) + ". " + get_title();
}

String TagLibMusicRecord::get_file_type() const {
	String path = get_full_path();
	if (path.contains("://")) {
		return "stream";
	}
	return path.get_extension().to_lower();
}

void tl_apply_fields(TagLibMusicRecord *r_record, const String &p_path, const TagFields &p_fields) {
	r_record->set_full_path(p_path);
	r_record->set_title(p_fields.title.strip_edges().is_empty() ? p_path.get_file().get_basename() : p_fields.title);
	r_record->set_artist(p_fields.artist.strip_edges().is_empty() ? String("Unknown Artist") : p_fields.artist);
	r_record->set_album(p_fields.album.strip_edges().is_empty() ? String("Unknown Album") : p_fields.album);
	r_record->set_album_artist(p_fields.album_artist.strip_edges());
	r_record->set_genre(p_fields.genre.strip_edges());
	r_record->set_musicbrainz_album_id(p_fields.musicbrainz_album_id.strip_edges());
	r_record->set_raw_length((double)p_fields.duration);
	r_record->set_track(p_fields.track);
	r_record->set_disc(p_fields.disc);
	r_record->set_year(p_fields.year);
	r_record->set_bitrate(p_fields.bitrate);
	r_record->set_sample_rate(p_fields.sample_rate);
	r_record->set_channels(p_fields.channels);
	r_record->set_file_mtime(p_fields.file_mtime);
	r_record->set_file_size(p_fields.file_size);
}

// ====================================================================
// TagLibTrackGroup / TagLibAlbumRecord / TagLibArtistRecord
// ====================================================================

void TagLibTrackGroup::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_name", "value"), &TagLibTrackGroup::set_name);
	ClassDB::bind_method(D_METHOD("get_name"), &TagLibTrackGroup::get_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_name", "get_name");
	ClassDB::bind_method(D_METHOD("set_asset_id", "value"), &TagLibTrackGroup::set_asset_id);
	ClassDB::bind_method(D_METHOD("get_asset_id"), &TagLibTrackGroup::get_asset_id);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "asset_id"), "set_asset_id", "get_asset_id");

	ClassDB::bind_method(D_METHOD("get_music_records"), &TagLibTrackGroup::get_music_records);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "music_records", PROPERTY_HINT_ARRAY_TYPE, "TagLibMusicRecord", PROPERTY_USAGE_NONE),
			"", "get_music_records");
	ClassDB::bind_method(D_METHOD("get_track_list"), &TagLibTrackGroup::get_track_list);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "track_list", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE, "TagLibTrackList"),
			"", "get_track_list");
	ClassDB::bind_method(D_METHOD("get_track_count"), &TagLibTrackGroup::get_track_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "track_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
			"", "get_track_count");
	ClassDB::bind_method(D_METHOD("get_total_length"), &TagLibTrackGroup::get_total_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "total_length", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
			"", "get_total_length");
	ClassDB::bind_method(D_METHOD("get_paths"), &TagLibTrackGroup::get_paths);
	ClassDB::bind_method(D_METHOD("has_track", "record"), &TagLibTrackGroup::has_track);
	ClassDB::bind_method(D_METHOD("duplicate"), &TagLibTrackGroup::duplicate);
}

void TagLibTrackGroup::_copy_group_to(TagLibTrackGroup *r_other) const {
	r_other->library = library;
	r_other->name = name;
	r_other->asset_id = asset_id;
	r_other->slots = slots;
}

TypedArray<TagLibMusicRecord> TagLibTrackGroup::get_music_records() const {
	if (library.is_null()) {
		return TypedArray<TagLibMusicRecord>();
	}
	return library->_handles_for(slots);
}

Ref<TagLibTrackList> TagLibTrackGroup::get_track_list() const {
	Ref<TagLibTrackList> list;
	list.instantiate();
	list->library = library;
	list->entries.resize(slots.size());
	for (uint32_t i = 0; i < slots.size(); i++) {
		list->entries[i] = (int32_t)slots[i];
	}
	return list;
}

double TagLibTrackGroup::get_total_length() const {
	if (library.is_null()) {
		return 0.0;
	}
	RWLockRead r(library->lock);
	double total = 0.0;
	for (uint32_t s : slots) {
		total += library->d.tracks[s].length;
	}
	return total;
}

PackedStringArray TagLibTrackGroup::get_paths() const {
	PackedStringArray out;
	if (library.is_null()) {
		return out;
	}
	RWLockRead r(library->lock);
	out.resize(slots.size());
	for (uint32_t i = 0; i < slots.size(); i++) {
		out.set(i, library->_path_of(slots[i]));
	}
	return out;
}

bool TagLibTrackGroup::has_track(const Ref<TagLibMusicRecord> &p_record) const {
	if (p_record.is_null() || library.is_null() || p_record->library != library) {
		return false;
	}
	return slots.find(p_record->slot) >= 0;
}

void TagLibAlbumRecord::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_album_artist", "value"), &TagLibAlbumRecord::set_album_artist);
	ClassDB::bind_method(D_METHOD("get_album_artist"), &TagLibAlbumRecord::get_album_artist);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "album_artist"), "set_album_artist", "get_album_artist");
	ClassDB::bind_method(D_METHOD("set_artists", "value"), &TagLibAlbumRecord::set_artists);
	ClassDB::bind_method(D_METHOD("get_artists"), &TagLibAlbumRecord::get_artists);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "artists"), "set_artists", "get_artists");
}

Ref<TagLibTrackGroup> TagLibAlbumRecord::duplicate() const {
	Ref<TagLibAlbumRecord> out;
	out.instantiate();
	_copy_group_to(out.ptr());
	out->album_artist = album_artist;
	out->artists = artists;
	return out;
}

Ref<TagLibTrackGroup> TagLibArtistRecord::duplicate() const {
	Ref<TagLibArtistRecord> out;
	out.instantiate();
	_copy_group_to(out.ptr());
	return out;
}

// ====================================================================
// TagLibTrackList
// ====================================================================

void TagLibTrackList::_bind_methods() {
	ClassDB::bind_method(D_METHOD("size"), &TagLibTrackList::size);
	ClassDB::bind_method(D_METHOD("is_empty"), &TagLibTrackList::is_empty);
	ClassDB::bind_method(D_METHOD("clear"), &TagLibTrackList::clear);
	ClassDB::bind_method(D_METHOD("get_record", "index"), &TagLibTrackList::get_record);
	ClassDB::bind_method(D_METHOD("set_record", "index", "record"), &TagLibTrackList::set_record);
	ClassDB::bind_method(D_METHOD("append", "record"), &TagLibTrackList::append);
	ClassDB::bind_method(D_METHOD("push_back", "record"), &TagLibTrackList::append);
	ClassDB::bind_method(D_METHOD("append_records", "records"), &TagLibTrackList::append_records);
	ClassDB::bind_method(D_METHOD("insert", "index", "record"), &TagLibTrackList::insert);
	ClassDB::bind_method(D_METHOD("remove_at", "index"), &TagLibTrackList::remove_at);
	ClassDB::bind_method(D_METHOD("erase", "record"), &TagLibTrackList::erase);
	ClassDB::bind_method(D_METHOD("has", "record"), &TagLibTrackList::has);
	ClassDB::bind_method(D_METHOD("find", "record", "from"), &TagLibTrackList::find, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("find_path", "path"), &TagLibTrackList::find_path);
	ClassDB::bind_method(D_METHOD("get_path_at", "index"), &TagLibTrackList::get_path_at);
	ClassDB::bind_method(D_METHOD("get_paths"), &TagLibTrackList::get_paths);
	ClassDB::bind_method(D_METHOD("get_total_length"), &TagLibTrackList::get_total_length);
	ClassDB::bind_method(D_METHOD("duplicate"), &TagLibTrackList::duplicate);
	ClassDB::bind_method(D_METHOD("slice", "begin", "end"), &TagLibTrackList::slice, DEFVAL(INT32_MAX));
	ClassDB::bind_method(D_METHOD("shuffle"), &TagLibTrackList::shuffle);
	ClassDB::bind_method(D_METHOD("reverse"), &TagLibTrackList::reverse);
	ClassDB::bind_method(D_METHOD("sort_by", "key", "descending"), &TagLibTrackList::sort_by, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("sort_custom", "less"), &TagLibTrackList::sort_custom);
	ClassDB::bind_method(D_METHOD("to_array"), &TagLibTrackList::to_array);
	ClassDB::bind_method(D_METHOD("get_library"), &TagLibTrackList::get_library);
	ClassDB::bind_static_method("TagLibTrackList", D_METHOD("from_records", "records"), &TagLibTrackList::from_records);

	ClassDB::bind_method(D_METHOD("_iter_init", "iter"), &TagLibTrackList::_iter_init);
	ClassDB::bind_method(D_METHOD("_iter_next", "iter"), &TagLibTrackList::_iter_next);
	ClassDB::bind_method(D_METHOD("_iter_get", "iter"), &TagLibTrackList::_iter_get);
}

int32_t TagLibTrackList::_entry_for(const Ref<TagLibMusicRecord> &p_record) {
	if (p_record->library.is_valid()) {
		if (library.is_null()) {
			// An empty or extras-only list adopts the first library it sees.
			library = p_record->library;
		}
		if (p_record->library == library) {
			return (int32_t)p_record->slot;
		}
	}
	extras.push_back(p_record);
	return -(int32_t)extras.size();
}

Ref<TagLibMusicRecord> TagLibTrackList::_record_for(int32_t p_entry) const {
	if (p_entry >= 0) {
		return library->_handle((uint32_t)p_entry);
	}
	return extras[(uint32_t)(-p_entry - 1)];
}

bool TagLibTrackList::_entry_is(int32_t p_entry, const Ref<TagLibMusicRecord> &p_record) const {
	if (p_entry >= 0) {
		return p_record->library == library && p_record->slot == (uint32_t)p_entry;
	}
	return extras[(uint32_t)(-p_entry - 1)] == p_record;
}

bool TagLibTrackList::_fix_index(int64_t &r_index, bool p_allow_end) const {
	int64_t n = entries.size();
	if (r_index < 0) {
		r_index += n;
	}
	return r_index >= 0 && (r_index < n || (p_allow_end && r_index == n));
}

Variant TagLibTrackList::getvar(const Variant &p_key, bool *r_valid) const {
	if (p_key.get_type() == Variant::INT) {
		int64_t i = p_key;
		if (!_fix_index(i)) {
			if (r_valid) {
				*r_valid = false;
			}
			return Variant();
		}
		if (r_valid) {
			*r_valid = true;
		}
		return _record_for(entries[i]);
	}
	return RefCounted::getvar(p_key, r_valid);
}

void TagLibTrackList::setvar(const Variant &p_key, const Variant &p_value, bool *r_valid) {
	if (p_key.get_type() == Variant::INT) {
		int64_t i = p_key;
		Ref<TagLibMusicRecord> rec = p_value;
		bool ok = rec.is_valid() && _fix_index(i);
		if (ok) {
			entries[i] = _entry_for(rec);
		}
		if (r_valid) {
			*r_valid = ok;
		}
		return;
	}
	RefCounted::setvar(p_key, p_value, r_valid);
}

void TagLibTrackList::clear() {
	entries.clear();
	extras.clear();
}

Ref<TagLibMusicRecord> TagLibTrackList::get_record(int64_t p_index) const {
	ERR_FAIL_COND_V_MSG(!_fix_index(p_index), Ref<TagLibMusicRecord>(), "TagLibTrackList index out of bounds.");
	return _record_for(entries[p_index]);
}

void TagLibTrackList::set_record(int64_t p_index, const Ref<TagLibMusicRecord> &p_record) {
	ERR_FAIL_COND(p_record.is_null());
	ERR_FAIL_COND_MSG(!_fix_index(p_index), "TagLibTrackList index out of bounds.");
	entries[p_index] = _entry_for(p_record);
}

void TagLibTrackList::append(const Ref<TagLibMusicRecord> &p_record) {
	ERR_FAIL_COND(p_record.is_null());
	entries.push_back(_entry_for(p_record));
}

void TagLibTrackList::append_records(const Variant &p_records) {
	Ref<TagLibTrackList> other = p_records;
	if (other.is_valid()) {
		if (other->library == library || library.is_null()) {
			if (library.is_null()) {
				library = other->library;
			}
			for (int32_t e : other->entries) {
				entries.push_back(e >= 0 ? e : _entry_for(other->extras[(uint32_t)(-e - 1)]));
			}
		} else {
			for (int32_t e : other->entries) {
				entries.push_back(_entry_for(other->_record_for(e)));
			}
		}
		return;
	}
	ERR_FAIL_COND_MSG(p_records.get_type() != Variant::ARRAY, "Expected an Array of TagLibMusicRecord or a TagLibTrackList.");
	Array arr = p_records;
	for (int i = 0; i < arr.size(); i++) {
		Ref<TagLibMusicRecord> rec = arr[i];
		if (rec.is_valid()) {
			entries.push_back(_entry_for(rec));
		}
	}
}

void TagLibTrackList::insert(int64_t p_index, const Ref<TagLibMusicRecord> &p_record) {
	ERR_FAIL_COND(p_record.is_null());
	ERR_FAIL_COND_MSG(!_fix_index(p_index, true), "TagLibTrackList index out of bounds.");
	entries.insert((uint32_t)p_index, _entry_for(p_record));
}

void TagLibTrackList::remove_at(int64_t p_index) {
	ERR_FAIL_COND_MSG(!_fix_index(p_index), "TagLibTrackList index out of bounds.");
	entries.remove_at((uint32_t)p_index);
}

void TagLibTrackList::erase(const Ref<TagLibMusicRecord> &p_record) {
	int i = find(p_record);
	if (i >= 0) {
		entries.remove_at((uint32_t)i);
	}
}

bool TagLibTrackList::has(const Ref<TagLibMusicRecord> &p_record) const {
	return find(p_record) >= 0;
}

int TagLibTrackList::find(const Ref<TagLibMusicRecord> &p_record, int64_t p_from) const {
	if (p_record.is_null()) {
		return -1;
	}
	if (p_from < 0) {
		p_from = MAX(p_from + (int64_t)entries.size(), 0);
	}
	const bool same_library = p_record->library.is_valid() && p_record->library == library;
	for (int64_t i = p_from; i < (int64_t)entries.size(); i++) {
		int32_t e = entries[i];
		if (e >= 0) {
			if (same_library && (uint32_t)e == p_record->slot) {
				return (int)i;
			}
		} else if (extras[(uint32_t)(-e - 1)] == p_record) {
			return (int)i;
		}
	}
	return -1;
}

int TagLibTrackList::find_path(const String &p_path) const {
	int64_t slot = -1;
	if (library.is_valid()) {
		RWLockRead r(library->lock);
		slot = library->_find_slot(p_path);
	}
	for (uint32_t i = 0; i < entries.size(); i++) {
		int32_t e = entries[i];
		if (e >= 0) {
			if (e == slot) {
				return (int)i;
			}
		} else if (extras[(uint32_t)(-e - 1)]->get_full_path() == p_path) {
			return (int)i;
		}
	}
	return -1;
}

String TagLibTrackList::get_path_at(int64_t p_index) const {
	ERR_FAIL_COND_V_MSG(!_fix_index(p_index), String(), "TagLibTrackList index out of bounds.");
	int32_t e = entries[p_index];
	if (e >= 0) {
		return library->track_string((uint32_t)e, TagLibLibrary::S_PATH);
	}
	return extras[(uint32_t)(-e - 1)]->get_full_path();
}

PackedStringArray TagLibTrackList::get_paths() const {
	PackedStringArray out;
	out.resize(entries.size());
	if (library.is_valid()) {
		RWLockRead r(library->lock);
		for (uint32_t i = 0; i < entries.size(); i++) {
			if (entries[i] >= 0) {
				out.set(i, library->_path_of((uint32_t)entries[i]));
			}
		}
	}
	for (uint32_t i = 0; i < entries.size(); i++) {
		if (entries[i] < 0) {
			out.set(i, extras[(uint32_t)(-entries[i] - 1)]->get_full_path());
		}
	}
	return out;
}

double TagLibTrackList::get_total_length() const {
	double total = 0.0;
	if (library.is_valid()) {
		RWLockRead r(library->lock);
		for (int32_t e : entries) {
			if (e >= 0) {
				total += library->d.tracks[e].length;
			}
		}
	}
	for (int32_t e : entries) {
		if (e < 0) {
			total += extras[(uint32_t)(-e - 1)]->get_raw_length();
		}
	}
	return total;
}

Ref<TagLibTrackList> TagLibTrackList::duplicate() const {
	Ref<TagLibTrackList> out;
	out.instantiate();
	out->library = library;
	out->entries = entries;
	out->extras = extras;
	return out;
}

Ref<TagLibTrackList> TagLibTrackList::slice(int64_t p_begin, int64_t p_end) const {
	Ref<TagLibTrackList> out;
	out.instantiate();
	out->library = library;
	int64_t n = entries.size();
	if (p_begin < 0) {
		p_begin = MAX(p_begin + n, 0);
	}
	if (p_end < 0) {
		p_end = MAX(p_end + n, 0);
	}
	p_begin = MIN(p_begin, n);
	p_end = MIN(p_end, n);
	for (int64_t i = p_begin; i < p_end; i++) {
		int32_t e = entries[i];
		out->entries.push_back(e >= 0 ? e : out->_entry_for(extras[(uint32_t)(-e - 1)]));
	}
	return out;
}

void TagLibTrackList::shuffle() {
	// Fisher-Yates with the engine RNG, as Array.shuffle does.
	for (int64_t i = (int64_t)entries.size() - 1; i > 0; i--) {
		int64_t j = Math::rand() % (i + 1);
		SWAP(entries[i], entries[j]);
	}
}

void TagLibTrackList::reverse() {
	std::reverse(entries.ptr(), entries.ptr() + entries.size());
}

void TagLibTrackList::sort_by(const String &p_key, bool p_descending) {
	const uint32_t n = entries.size();
	if (n < 2) {
		return;
	}

	enum Kind { K_TITLE, K_TRACK, K_TEXT, K_NUMBER };
	Kind kind;
	TagLibLibrary::StrField text_field = TagLibLibrary::S_TITLE;
	if (p_key == "title") {
		kind = K_TITLE;
	} else if (p_key == "track") {
		kind = K_TRACK;
	} else if (p_key == "album") {
		kind = K_TEXT;
		text_field = TagLibLibrary::S_ALBUM;
	} else if (p_key == "artist") {
		kind = K_TEXT;
		text_field = TagLibLibrary::S_ARTIST;
	} else if (p_key == "album_artist") {
		kind = K_TEXT;
		text_field = TagLibLibrary::S_ALBUM_ARTIST;
	} else if (p_key == "genre") {
		kind = K_TEXT;
		text_field = TagLibLibrary::S_GENRE;
	} else if (p_key == "path") {
		kind = K_TEXT;
		text_field = TagLibLibrary::S_PATH;
	} else if (p_key == "year" || p_key == "length" || p_key == "listen_count") {
		kind = K_NUMBER;
	} else {
		ERR_FAIL_MSG("Unknown TagLibTrackList sort key '" + p_key + "'.");
	}

	// One key per entry, built up front. Library titles use the library's
	// precomputed case-insensitive ranks; everything else compares values.
	struct Key {
		int64_t a = 0;
		int64_t b = 0;
		int64_t c = 0;
		String text;
		bool ranked = false;
	};
	LocalVector<Key> keys;
	keys.resize(n);
	bool all_ranked = true;
	for (uint32_t i = 0; i < n; i++) {
		int32_t e = entries[i];
		Key &k = keys[i];
		if (e >= 0) {
			TLTrack t = library->track_copy((uint32_t)e);
			uint32_t rank;
			{
				RWLockRead r(library->lock);
				rank = (uint32_t)e < library->d.title_rank.size() ? library->d.title_rank[e] : UINT32_MAX;
			}
			switch (kind) {
				case K_TITLE:
				case K_TRACK:
					k.a = kind == K_TRACK ? t.disc : 0;
					k.b = kind == K_TRACK ? t.track : 0;
					k.c = rank;
					k.ranked = rank != UINT32_MAX;
					break;
				case K_TEXT:
					k.text = library->track_string((uint32_t)e, text_field);
					break;
				case K_NUMBER:
					k.a = p_key == "year" ? t.year : (p_key == "length" ? (int64_t)(t.length * 1000.0f) : t.listen_count);
					break;
			}
		} else {
			const Ref<TagLibMusicRecord> &rec = extras[(uint32_t)(-e - 1)];
			switch (kind) {
				case K_TITLE:
				case K_TRACK:
					k.a = kind == K_TRACK ? rec->get_disc() : 0;
					k.b = kind == K_TRACK ? rec->get_track() : 0;
					break;
				case K_TEXT:
					k.text = text_field == TagLibLibrary::S_PATH ? rec->get_full_path() : (text_field == TagLibLibrary::S_ALBUM ? rec->get_album() : (text_field == TagLibLibrary::S_ARTIST ? rec->get_artist() : (text_field == TagLibLibrary::S_ALBUM_ARTIST ? rec->get_album_artist() : rec->get_genre())));
					break;
				case K_NUMBER:
					k.a = p_key == "year" ? rec->get_year() : (p_key == "length" ? (int64_t)(rec->get_raw_length() * 1000.0) : rec->get_listen_count());
					break;
			}
		}
		if (!k.ranked) {
			all_ranked = false;
		}
	}
	// Mixed lists (ranks don't cover standalone records) compare titles as text.
	if ((kind == K_TITLE || kind == K_TRACK) && !all_ranked) {
		for (uint32_t i = 0; i < n; i++) {
			int32_t e = entries[i];
			keys[i].text = e >= 0 ? library->track_string((uint32_t)e, TagLibLibrary::S_TITLE) : extras[(uint32_t)(-e - 1)]->get_title();
		}
	}

	LocalVector<uint32_t> idx;
	idx.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		idx[i] = i;
	}
	auto less = [&](uint32_t x, uint32_t y) -> bool {
		const Key &p = keys[x];
		const Key &q = keys[y];
		if (p.a != q.a) {
			return p.a < q.a;
		}
		if (p.b != q.b) {
			return p.b < q.b;
		}
		if (kind == K_TEXT || ((kind == K_TITLE || kind == K_TRACK) && !all_ranked)) {
			return p.text.nocasecmp_to(q.text) < 0;
		}
		return p.c < q.c;
	};
	if (p_descending) {
		std::stable_sort(idx.ptr(), idx.ptr() + n, [&](uint32_t x, uint32_t y) { return less(y, x); });
	} else {
		std::stable_sort(idx.ptr(), idx.ptr() + n, less);
	}
	LocalVector<int32_t> sorted;
	sorted.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		sorted[i] = entries[idx[i]];
	}
	entries = sorted;
}

void TagLibTrackList::sort_custom(const Callable &p_less) {
	const uint32_t n = entries.size();
	LocalVector<Ref<TagLibMusicRecord>> recs;
	recs.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		recs[i] = _record_for(entries[i]);
	}
	LocalVector<uint32_t> idx;
	idx.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		idx[i] = i;
	}
	std::stable_sort(idx.ptr(), idx.ptr() + n, [&](uint32_t x, uint32_t y) {
		return (bool)p_less.call(recs[x], recs[y]);
	});
	LocalVector<int32_t> sorted;
	sorted.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		sorted[i] = entries[idx[i]];
	}
	entries = sorted;
}

TypedArray<TagLibMusicRecord> TagLibTrackList::to_array() const {
	TypedArray<TagLibMusicRecord> out;
	out.resize(entries.size());
	for (uint32_t i = 0; i < entries.size(); i++) {
		out.set(i, _record_for(entries[i]));
	}
	return out;
}

Ref<TagLibTrackList> TagLibTrackList::from_records(const Variant &p_records) {
	Ref<TagLibTrackList> other = p_records;
	if (other.is_valid()) {
		return other->duplicate();
	}
	Ref<TagLibTrackList> out;
	out.instantiate();
	if (p_records.get_type() == Variant::ARRAY) {
		out->append_records(p_records);
	}
	return out;
}

bool TagLibTrackList::_iter_init(const Array &p_iter) {
	Array iter = p_iter;
	iter[0] = 0;
	return !entries.is_empty();
}

bool TagLibTrackList::_iter_next(const Array &p_iter) {
	Array iter = p_iter;
	int64_t i = (int64_t)iter[0] + 1;
	iter[0] = i;
	return i < (int64_t)entries.size();
}

Variant TagLibTrackList::_iter_get(const Variant &p_iter) const {
	int64_t i = p_iter;
	if (i < 0 || i >= (int64_t)entries.size()) {
		return Variant();
	}
	return _record_for(entries[i]);
}
