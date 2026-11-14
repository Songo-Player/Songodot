#include "gdtaglib.h"
#include "taglib_records.h"

#include "core/object/class_db.h"
#include "core/templates/hash_set.h"
#include "core/templates/hashfuncs.h"

#include <algorithm>
#include <cstring>
#include <string>

// ====================================================================
// TLStringPool
// ====================================================================

void TLStringPool::clear() {
	bytes.clear();
	offsets.clear();
	tl_push(offsets, 0u);
	table.clear();
	intern("", 0); // id 0
}

String TLStringPool::get(uint32_t p_id) const {
	uint32_t len = length(p_id);
	return len == 0 ? String() : String::utf8(ptr(p_id), (int)len);
}

bool TLStringPool::equals(uint32_t p_id, const char *p_str, uint32_t p_len) const {
	return length(p_id) == p_len && (p_len == 0 || memcmp(ptr(p_id), p_str, p_len) == 0);
}

static _FORCE_INLINE_ uint32_t _tl_hash_bytes(const char *p_str, uint32_t p_len) {
	return hash_murmur3_buffer(p_str, (int)p_len);
}

void TLStringPool::_table_insert(uint32_t p_id) {
	const uint32_t mask = table.size() - 1;
	uint32_t pos = _tl_hash_bytes(ptr(p_id), length(p_id)) & mask;
	while (table[pos] != -1) {
		pos = (pos + 1) & mask;
	}
	table[pos] = (int32_t)p_id;
}

void TLStringPool::_grow_table() {
	uint32_t cap = 64;
	while (cap < count() * 2 + 2) {
		cap <<= 1;
	}
	table.resize(cap);
	for (uint32_t i = 0; i < cap; i++) {
		table[i] = -1;
	}
	for (uint32_t id = 0; id < count(); id++) {
		_table_insert(id);
	}
}

int64_t TLStringPool::find(const char *p_str, uint32_t p_len) const {
	if (table.is_empty()) {
		return -1;
	}
	const uint32_t mask = table.size() - 1;
	uint32_t pos = _tl_hash_bytes(p_str, p_len) & mask;
	while (table[pos] != -1) {
		if (equals((uint32_t)table[pos], p_str, p_len)) {
			return table[pos];
		}
		pos = (pos + 1) & mask;
	}
	return -1;
}

uint32_t TLStringPool::intern(const char *p_str, uint32_t p_len) {
	int64_t existing = find(p_str, p_len);
	if (existing >= 0) {
		return (uint32_t)existing;
	}
	uint32_t id = count();
	uint32_t start = bytes.size();
	if (p_len > 0) {
		tl_reserve_more(bytes, p_len);
		bytes.resize(start + p_len);
		memcpy(bytes.ptr() + start, p_str, p_len);
	}
	tl_push(offsets, start + p_len);
	// Keep the load factor at or under one half.
	if (table.size() < (count() + 1) * 2) {
		_grow_table();
	} else {
		_table_insert(id);
	}
	return id;
}

uint32_t TLStringPool::intern(const String &p_str) {
	if (p_str.is_empty()) {
		return 0;
	}
	CharString cs = p_str.utf8();
	return intern(cs.get_data(), (uint32_t)cs.length());
}

char *TLStringPool::prepare_bytes(uint32_t p_length) {
	table.clear();
	bytes.resize(p_length);
	return bytes.ptr();
}

uint32_t *TLStringPool::prepare_offsets(uint32_t p_count) {
	table.clear();
	offsets.resize(p_count + 1);
	return offsets.ptr();
}

bool TLStringPool::validate_loaded() const {
	if (offsets.is_empty() || offsets[0] != 0 || offsets[offsets.size() - 1] != bytes.size()) {
		return false;
	}
	for (uint32_t i = 1; i < offsets.size(); i++) {
		if (offsets[i] < offsets[i - 1]) {
			return false;
		}
	}
	// id 0 must be "" (track fields use 0 for "unset").
	return offsets.size() >= 2 && offsets[1] == 0;
}

size_t TLStringPool::memory_usage() const {
	return bytes.get_capacity() + offsets.get_capacity() * sizeof(uint32_t) + table.get_capacity() * sizeof(int32_t);
}

void TLData::clear() {
	strings.clear();
	tracks.clear();
	title_rank.clear();
	all_order.clear();
	albums.clear();
	artists.clear();
	group_tracks.clear();
	group_names.clear();
}

// ====================================================================
// Name helpers (shared with the album / artist grouping)
// ====================================================================

// Collapse the common "various artists" spellings to one canonical value.
// Returns "" for an empty/whitespace input so callers can treat it as unset.
static String _canonical_various_artists(const String &p_in) {
	String s = p_in.strip_edges();
	if (s.is_empty()) {
		return String();
	}
	String l = s.to_lower();
	if (l == "va" || l == "v.a." || l == "v. a." || l == "various" ||
			l == "various artists" || l == "various_artists") {
		return String("Various Artists");
	}
	return s;
}

// Stable, filesystem-safe id derived from a display name, matching the
// scheme Songo-5's *_record.gd build_asset() used. Cover art files are named
// by it, so it must not change.
static String _asset_id_from_name(const String &p_name) {
	String cleaned = p_name.strip_edges().replace(" ", "_");
	String safe;
	for (int i = 0; i < cleaned.length(); i++) {
		char32_t c = cleaned[i];
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				(c >= '0' && c <= '9') || c == '_';
		if (ok) {
			safe += String::chr(c);
		}
	}
	if (safe.length() > 3) {
		return safe;
	}
	return String("defaulted_id_") + itos((int64_t)p_name.hash());
}

// Lowercase, trim, and collapse internal whitespace runs to a single space,
// so trivially different spellings of a name hash to the same bucket.
static String _normalize_key(const String &p_in) {
	String t = p_in.strip_edges().to_lower();
	String out;
	bool prev_ws = false;
	for (int i = 0; i < t.length(); i++) {
		char32_t c = t[i];
		bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
		if (ws) {
			if (!prev_ws) {
				out += " ";
			}
			prev_ws = true;
		} else {
			out += String::chr(c);
			prev_ws = false;
		}
	}
	return out;
}

// Splits a UTF-8 path into directory (with trailing '/') and file name.
static void _split_path(const CharString &p_path, uint32_t &r_dir_len) {
	const char *s = p_path.get_data();
	int len = p_path.length();
	r_dir_len = 0;
	for (int i = len - 1; i >= 0; i--) {
		if (s[i] == '/') {
			r_dir_len = (uint32_t)i + 1;
			return;
		}
	}
}

static _FORCE_INLINE_ uint32_t _path_hash(uint32_t p_dir, uint32_t p_file) {
	return hash_murmur3_one_32(p_file, hash_murmur3_one_32(p_dir));
}

static _FORCE_INLINE_ int16_t _clamp16(int p_value) {
	return (int16_t)CLAMP(p_value, -32768, 32767);
}

// A zero-filled byte flag per index.
static void _zeroed(LocalVector<uint8_t> &r_flags, uint32_t p_count) {
	r_flags.resize(p_count);
	if (p_count > 0) {
		memset(r_flags.ptr(), 0, p_count);
	}
}

// ====================================================================
// TagLibLibrary: bindings / lifetime
// ====================================================================

void TagLibLibrary::_bind_methods() {
	ClassDB::bind_static_method("TagLibLibrary", D_METHOD("load_file", "path"), &TagLibLibrary::load_file);
	ClassDB::bind_method(D_METHOD("save", "path"), &TagLibLibrary::save);

	ClassDB::bind_method(D_METHOD("get_track_count"), &TagLibLibrary::get_track_count);
	ClassDB::bind_method(D_METHOD("get_album_count"), &TagLibLibrary::get_album_count);
	ClassDB::bind_method(D_METHOD("get_artist_count"), &TagLibLibrary::get_artist_count);
	ClassDB::bind_method(D_METHOD("get_all_tracks"), &TagLibLibrary::get_all_tracks);
	ClassDB::bind_method(D_METHOD("get_albums"), &TagLibLibrary::get_albums);
	ClassDB::bind_method(D_METHOD("get_artists"), &TagLibLibrary::get_artists);
	ClassDB::bind_method(D_METHOD("find_by_path", "path"), &TagLibLibrary::find_by_path);
	ClassDB::bind_method(D_METHOD("has_path", "path"), &TagLibLibrary::has_path);

	ClassDB::bind_method(D_METHOD("plan_update", "current_paths", "opts"), &TagLibLibrary::plan_update, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("update", "current_paths", "reread_paths", "progress", "opts"),
			&TagLibLibrary::update, DEFVAL(PackedStringArray()), DEFVAL(Callable()), DEFVAL(Dictionary()));

	ClassDB::bind_method(D_METHOD("get_memory_usage"), &TagLibLibrary::get_memory_usage);
}

TagLibLibrary::TagLibLibrary() {}

TagLibLibrary::~TagLibLibrary() {
	// Every handle holds a reference, so none can outlive us.
}

// ====================================================================
// Internals (lock held by the caller)
// ====================================================================

String TagLibLibrary::_path_of(uint32_t p_slot) const {
	const TLTrack &t = d.tracks[p_slot];
	uint32_t dl = d.strings.length(t.dir);
	uint32_t fl = d.strings.length(t.file);
	std::string buf;
	buf.reserve(dl + fl);
	buf.append(d.strings.ptr(t.dir), dl);
	buf.append(d.strings.ptr(t.file), fl);
	return String::utf8(buf.data(), (int)buf.size());
}

int64_t TagLibLibrary::_find_slot(const String &p_path) const {
	if (path_table.is_empty() || p_path.is_empty()) {
		return -1;
	}
	CharString cs = p_path.utf8();
	uint32_t dir_len;
	_split_path(cs, dir_len);
	int64_t dir = d.strings.find(cs.get_data(), dir_len);
	if (dir < 0) {
		return -1;
	}
	int64_t file = d.strings.find(cs.get_data() + dir_len, (uint32_t)cs.length() - dir_len);
	if (file < 0) {
		return -1;
	}
	const uint32_t mask = path_table.size() - 1;
	uint32_t pos = _path_hash((uint32_t)dir, (uint32_t)file) & mask;
	while (path_table[pos] != -1) {
		const TLTrack &t = d.tracks[path_table[pos]];
		if (t.dir == (uint32_t)dir && t.file == (uint32_t)file) {
			return path_table[pos];
		}
		pos = (pos + 1) & mask;
	}
	return -1;
}

void TagLibLibrary::_rebuild_path_table() {
	live_count = 0;
	for (const TLTrack &t : d.tracks) {
		if (t.flags & TL_TRACK_ALIVE) {
			live_count++;
		}
	}
	uint32_t cap = 16;
	while (cap < live_count * 2) {
		cap <<= 1;
	}
	path_table.resize(cap);
	for (uint32_t i = 0; i < cap; i++) {
		path_table[i] = -1;
	}
	const uint32_t mask = cap - 1;
	for (uint32_t slot = 0; slot < d.tracks.size(); slot++) {
		const TLTrack &t = d.tracks[slot];
		if (!(t.flags & TL_TRACK_ALIVE)) {
			continue;
		}
		uint32_t pos = _path_hash(t.dir, t.file) & mask;
		while (path_table[pos] != -1) {
			pos = (pos + 1) & mask;
		}
		path_table[pos] = (int32_t)slot;
	}
}

void TagLibLibrary::_set_from_fields(TLTrack &r_track, const String &p_path, const TagFields &p_fields) {
	CharString cs = p_path.utf8();
	uint32_t dir_len;
	_split_path(cs, dir_len);
	r_track.dir = d.strings.intern(cs.get_data(), dir_len);
	r_track.file = d.strings.intern(cs.get_data() + dir_len, (uint32_t)cs.length() - dir_len);

	// Fallback for an untagged (or unparseable) file: the file name without
	// its extension, e.g. "04 - Weightless.flac" -> "04 - Weightless".
	String title = p_fields.title.strip_edges().is_empty() ? p_path.get_file().get_basename() : p_fields.title;
	String artist = p_fields.artist.strip_edges().is_empty() ? String("Unknown Artist") : p_fields.artist;
	String album = p_fields.album.strip_edges().is_empty() ? String("Unknown Album") : p_fields.album;
	r_track.title = d.strings.intern(title);
	r_track.artist = d.strings.intern(artist);
	r_track.album = d.strings.intern(album);
	// Left empty when the file has no album-artist tag; _regroup infers one
	// per album.
	r_track.album_artist = d.strings.intern(p_fields.album_artist.strip_edges());
	r_track.genre = d.strings.intern(p_fields.genre.strip_edges());
	r_track.mbid = d.strings.intern(p_fields.musicbrainz_album_id.strip_edges());

	r_track.length = (float)p_fields.duration;
	r_track.track = _clamp16(p_fields.track);
	r_track.disc = _clamp16(p_fields.disc);
	r_track.year = _clamp16(p_fields.year);
	r_track.bitrate = (uint16_t)CLAMP(p_fields.bitrate, 0, 65535);
	r_track.sample_rate = (uint32_t)MAX(p_fields.sample_rate, 0);
	r_track.channels = (uint8_t)CLAMP(p_fields.channels, 0, 255);
	r_track.mtime = p_fields.file_mtime;
	r_track.size = p_fields.file_size;
}

// --------------------------------------------------------------------
// Album / artist grouping
// --------------------------------------------------------------------
//
// Recomputes albums, (album) artists, title ranks and the All Songs order
// from the live tracks in p_order (the order files were found in, which
// decides "first seen" naming). The rules match the earlier Resource-based
// assembly, so asset ids (and the cover art named by them) carry over.

namespace {

struct IdCount {
	uint32_t id;
	int count;
};

void _bump(LocalVector<IdCount> &r_counts, uint32_t p_id) {
	for (IdCount &c : r_counts) {
		if (c.id == p_id) {
			c.count++;
			return;
		}
	}
	r_counts.push_back({ p_id, 1 });
}

// Highest count; ties go to the first inserted. 0 when empty.
uint32_t _highest(const LocalVector<IdCount> &p_counts, int *r_count = nullptr) {
	uint32_t best = 0;
	int best_count = -1;
	for (const IdCount &c : p_counts) {
		if (c.count > best_count) {
			best_count = c.count;
			best = c.id;
		}
	}
	if (r_count) {
		*r_count = MAX(best_count, 0);
	}
	return best;
}

} // namespace

void TagLibLibrary::_regroup(const LocalVector<uint32_t> &p_order) {
	TLStringPool &S = d.strings;
	TLVec<TLTrack> &T = d.tracks;

	// --- title ranks: case-insensitive order of every distinct title -----
	{
		const uint32_t string_count = S.count();
		LocalVector<uint32_t> rank_of; // string id -> rank, only for titles
		rank_of.resize(string_count);
		LocalVector<uint8_t> seen;
		_zeroed(seen, string_count);
		LocalVector<uint32_t> title_ids;
		// Every slot, removed ones included: lists may still hold them and
		// sort_by("title") needs a rank for each.
		for (uint32_t slot = 0; slot < T.size(); slot++) {
			uint32_t id = T[slot].title;
			if (!seen[id]) {
				seen[id] = 1;
				title_ids.push_back(id);
			}
		}
		LocalVector<String> titles;
		titles.resize(title_ids.size());
		for (uint32_t i = 0; i < title_ids.size(); i++) {
			titles[i] = S.get(title_ids[i]);
		}
		LocalVector<uint32_t> idx;
		idx.resize(title_ids.size());
		for (uint32_t i = 0; i < idx.size(); i++) {
			idx[i] = i;
		}
		std::stable_sort(idx.ptr(), idx.ptr() + idx.size(), [&](uint32_t a, uint32_t b) {
			return titles[a].nocasecmp_to(titles[b]) < 0;
		});
		for (uint32_t r = 0; r < idx.size(); r++) {
			rank_of[title_ids[idx[r]]] = r;
		}
		d.title_rank.resize(T.size());
		for (uint32_t slot = 0; slot < T.size(); slot++) {
			d.title_rank[slot] = rank_of[T[slot].title];
		}
	}
	const TLVec<uint32_t> &rank = d.title_rank;
	auto by_title = [&](uint32_t a, uint32_t b) { return rank[a] < rank[b]; };
	auto by_disc_track_title = [&](uint32_t a, uint32_t b) {
		const TLTrack &x = T[a];
		const TLTrack &y = T[b];
		if (x.disc != y.disc) {
			return x.disc < y.disc;
		}
		if (x.track != y.track) {
			return x.track < y.track;
		}
		return rank[a] < rank[b];
	};

	// --- group tracks by album identity (first-seen order) ------------
	// Every track of one real album should key the same:
	//   - all "Unknown Album" tracks collapse into a single shared bucket;
	//   - a MusicBrainz Album ID, when tagged, keys the album exactly;
	//   - otherwise the normalized album name alone -- deliberately NOT the
	//     per-track artist, which varies within an album (features, split
	//     credits). Two unrelated same-named albums without an MBID merge
	//     and resolve to "Various Artists".
	LocalVector<String> album_keys;
	LocalVector<uint32_t> album_name_ids; // display name, from the first track
	LocalVector<LocalVector<uint32_t>> album_groups;
	{
		HashMap<String, uint32_t> key_index;
		HashMap<uint64_t, uint32_t> pair_index; // (album id, mbid id) -> group
		for (uint32_t slot : p_order) {
			const TLTrack &t = T[slot];
			uint64_t pair = ((uint64_t)t.album << 32) | t.mbid;
			uint32_t group;
			const uint32_t *known = pair_index.getptr(pair);
			if (known) {
				group = *known;
			} else {
				String album_norm = _normalize_key(S.get(t.album));
				String key;
				if (album_norm.is_empty() || album_norm == "unknown album") {
					key = "\x1funknown-album";
				} else if (S.length(t.mbid) > 0) {
					key = "mbid\x1f" + S.get(t.mbid);
				} else {
					key = "name\x1f" + album_norm;
				}
				const uint32_t *existing = key_index.getptr(key);
				if (existing) {
					group = *existing;
				} else {
					group = album_keys.size();
					key_index.insert(key, group);
					album_keys.push_back(key);
					album_name_ids.push_back(t.album);
					album_groups.push_back(LocalVector<uint32_t>());
				}
				pair_index.insert(pair, group);
			}
			album_groups[group].push_back(slot);
		}
	}

	// asset_id stays the readable sanitized name while that name is unique;
	// genuinely distinct same-named albums fall back to a key-derived suffix
	// so their cover-art files never collide.
	LocalVector<String> album_names;
	LocalVector<String> album_bases;
	HashMap<String, int> album_base_counts;
	album_names.resize(album_keys.size());
	album_bases.resize(album_keys.size());
	for (uint32_t i = 0; i < album_keys.size(); i++) {
		album_names[i] = S.get(album_name_ids[i]);
		album_bases[i] = _asset_id_from_name(album_names[i]);
		int *c = album_base_counts.getptr(album_bases[i]);
		if (c) {
			(*c)++;
		} else {
			album_base_counts.insert(album_bases[i], 1);
		}
	}

	// Per track-artist string: its comma-separated, canonicalized credits
	// ("A, B" counts toward both), minus "Unknown Artist" and duplicates.
	HashMap<uint32_t, LocalVector<uint32_t>> artist_pieces;
	auto pieces_of = [&](uint32_t p_artist) -> const LocalVector<uint32_t> & {
		LocalVector<uint32_t> *cached = artist_pieces.getptr(p_artist);
		if (cached) {
			return *cached;
		}
		LocalVector<uint32_t> pieces;
		Vector<String> parts = S.get(p_artist).split(",", false);
		for (const String &part : parts) {
			String piece = _canonical_various_artists(part);
			if (piece.is_empty() || piece == "Unknown Artist") {
				continue;
			}
			uint32_t id = S.intern(piece);
			if (pieces.find(id) < 0) {
				pieces.push_back(id);
			}
		}
		return artist_pieces.insert(p_artist, pieces)->value;
	};
	HashMap<uint32_t, uint32_t> album_artist_canon; // tag string -> canonical (0 = unset)
	auto canon_album_artist = [&](uint32_t p_id) -> uint32_t {
		if (p_id == 0) {
			return 0;
		}
		const uint32_t *cached = album_artist_canon.getptr(p_id);
		if (cached) {
			return *cached;
		}
		uint32_t canon = S.intern(_canonical_various_artists(S.get(p_id)));
		album_artist_canon.insert(p_id, canon);
		return canon;
	};
	const uint32_t various_id = S.intern(String("Various Artists"));

	// --- albums + (album) artists --------------------------------------
	struct PendingAlbum {
		uint32_t name, album_artist, asset_id;
		LocalVector<uint32_t> names;
		LocalVector<uint32_t> tracks;
	};
	LocalVector<PendingAlbum> albums;
	albums.resize(album_keys.size());
	LocalVector<uint32_t> artist_names; // resolved album-artist string ids
	LocalVector<LocalVector<uint32_t>> artist_groups;
	HashMap<uint32_t, uint32_t> artist_index;

	for (uint32_t i = 0; i < album_keys.size(); i++) {
		LocalVector<uint32_t> &group = album_groups[i];
		PendingAlbum &album = albums[i];
		album.name = album_name_ids[i];

		LocalVector<IdCount> explicit_album_artists;
		LocalVector<IdCount> artist_record_counts;
		for (uint32_t slot : group) {
			for (uint32_t piece : pieces_of(T[slot].artist)) {
				if (album.names.find(piece) < 0) {
					album.names.push_back(piece);
				}
				_bump(artist_record_counts, piece);
			}
			uint32_t aa = canon_album_artist(T[slot].album_artist);
			if (aa != 0) {
				_bump(explicit_album_artists, aa);
			}
		}

		// Resolve the album artist:
		//  1. an explicit album-artist tag wins -- the most common one if they differ;
		//  2. otherwise, only when a single artist is credited on a MAJORITY of the
		//     album's tracks does that artist become the album artist;
		//  3. otherwise it's treated as a compilation -> "Various Artists".
		uint32_t resolved = 0;
		if (!explicit_album_artists.is_empty()) {
			resolved = _highest(explicit_album_artists);
		} else {
			int top_count = 0;
			uint32_t top = _highest(artist_record_counts, &top_count);
			if (top_count > 0 && top_count * 2 > (int)group.size()) {
				resolved = top;
			}
		}
		if (resolved == 0) {
			resolved = various_id;
		}
		album.album_artist = resolved;

		String asset = album_bases[i];
		if (album_base_counts[album_bases[i]] > 1) {
			asset += "_" + itos((int64_t)album_keys[i].hash());
		}
		album.asset_id = S.intern(asset);

		// Tracks ordered for album playback: disc, then track, then title.
		std::stable_sort(group.ptr(), group.ptr() + group.size(), by_disc_track_title);
		album.tracks = group;

		// The artists directory is album-artist driven: tracks on a
		// "Various Artists" album are not filed under any single artist.
		if (resolved != various_id) {
			uint32_t artist;
			const uint32_t *existing = artist_index.getptr(resolved);
			if (existing) {
				artist = *existing;
			} else {
				artist = artist_names.size();
				artist_index.insert(resolved, artist);
				artist_names.push_back(resolved);
				artist_groups.push_back(LocalVector<uint32_t>());
			}
			for (uint32_t slot : group) {
				artist_groups[artist].push_back(slot);
			}
		}
	}

	// --- display order -------------------------------------------------
	LocalVector<uint32_t> album_order;
	album_order.resize(albums.size());
	for (uint32_t i = 0; i < album_order.size(); i++) {
		album_order[i] = i;
	}
	std::stable_sort(album_order.ptr(), album_order.ptr() + album_order.size(), [&](uint32_t a, uint32_t b) {
		bool au = album_names[a] == "Unknown Album";
		bool bu = album_names[b] == "Unknown Album";
		if (au != bu) {
			return bu; // "Unknown Album" sinks to the bottom
		}
		return album_names[a].nocasecmp_to(album_names[b]) < 0;
	});

	LocalVector<String> artist_display;
	artist_display.resize(artist_names.size());
	HashMap<String, int> artist_base_counts;
	LocalVector<String> artist_bases;
	artist_bases.resize(artist_names.size());
	for (uint32_t i = 0; i < artist_names.size(); i++) {
		artist_display[i] = S.get(artist_names[i]);
		artist_bases[i] = _asset_id_from_name(artist_display[i]);
		int *c = artist_base_counts.getptr(artist_bases[i]);
		if (c) {
			(*c)++;
		} else {
			artist_base_counts.insert(artist_bases[i], 1);
		}
	}
	LocalVector<uint32_t> artist_order;
	artist_order.resize(artist_names.size());
	for (uint32_t i = 0; i < artist_order.size(); i++) {
		artist_order[i] = i;
	}
	std::stable_sort(artist_order.ptr(), artist_order.ptr() + artist_order.size(), [&](uint32_t a, uint32_t b) {
		return artist_display[a].nocasecmp_to(artist_display[b]) < 0;
	});

	// --- write out ------------------------------------------------------
	d.albums.clear();
	d.artists.clear();
	d.group_tracks.clear();
	d.group_names.clear();
	d.albums.reserve(albums.size());
	d.artists.reserve(artist_names.size());

	for (uint32_t i : album_order) {
		const PendingAlbum &a = albums[i];
		TLGroup g;
		g.name = a.name;
		g.album_artist = a.album_artist;
		g.asset_id = a.asset_id;
		g.names_begin = d.group_names.size();
		g.names_count = a.names.size();
		for (uint32_t n : a.names) {
			tl_push(d.group_names, n);
		}
		g.tracks_begin = d.group_tracks.size();
		g.tracks_count = a.tracks.size();
		for (uint32_t s : a.tracks) {
			tl_push(d.group_tracks, s);
		}
		tl_push(d.albums, g);
	}

	// An artist's tracks span albums, so they're listed by title.
	for (uint32_t i : artist_order) {
		LocalVector<uint32_t> &tracks = artist_groups[i];
		std::stable_sort(tracks.ptr(), tracks.ptr() + tracks.size(), by_title);
		String asset = artist_bases[i];
		if (artist_base_counts[artist_bases[i]] > 1) {
			asset += "_" + itos((int64_t)artist_display[i].hash());
		}
		TLGroup g;
		g.name = artist_names[i];
		g.asset_id = S.intern(asset);
		g.tracks_begin = d.group_tracks.size();
		g.tracks_count = tracks.size();
		for (uint32_t s : tracks) {
			tl_push(d.group_tracks, s);
		}
		tl_push(d.artists, g);
	}

	d.all_order.resize(p_order.size());
	for (uint32_t i = 0; i < p_order.size(); i++) {
		d.all_order[i] = p_order[i];
	}
	std::stable_sort(d.all_order.ptr(), d.all_order.ptr() + d.all_order.size(), by_title);
}

// Copies the live tracks (and only the strings they and the groups use)
// into r_out, renumbering slots densely. Used to save without garbage.
void TagLibLibrary::_compact_into(TLData &r_out) const {
	r_out.clear();
	const TLStringPool &S = d.strings;
	LocalVector<uint32_t> remap_string;
	remap_string.resize(S.count());
	for (uint32_t i = 0; i < remap_string.size(); i++) {
		remap_string[i] = UINT32_MAX;
	}
	auto str = [&](uint32_t p_id) -> uint32_t {
		if (remap_string[p_id] == UINT32_MAX) {
			remap_string[p_id] = r_out.strings.intern(S.ptr(p_id), S.length(p_id));
		}
		return remap_string[p_id];
	};

	LocalVector<uint32_t> remap_slot;
	remap_slot.resize(d.tracks.size());
	r_out.tracks.reserve(live_count);
	r_out.title_rank.reserve(live_count);
	for (uint32_t slot = 0; slot < d.tracks.size(); slot++) {
		const TLTrack &t = d.tracks[slot];
		if (!(t.flags & TL_TRACK_ALIVE)) {
			remap_slot[slot] = UINT32_MAX;
			continue;
		}
		remap_slot[slot] = r_out.tracks.size();
		TLTrack c = t;
		c.dir = str(t.dir);
		c.file = str(t.file);
		c.title = str(t.title);
		c.album = str(t.album);
		c.artist = str(t.artist);
		c.album_artist = str(t.album_artist);
		c.genre = str(t.genre);
		c.mbid = str(t.mbid);
		tl_push(r_out.tracks, c);
		tl_push(r_out.title_rank, slot < d.title_rank.size() ? d.title_rank[slot] : 0u);
	}

	for (uint32_t slot : d.all_order) {
		if (remap_slot[slot] != UINT32_MAX) {
			tl_push(r_out.all_order, remap_slot[slot]);
		}
	}

	auto copy_groups = [&](const TLVec<TLGroup> &p_in, TLVec<TLGroup> &r_groups) {
		for (const TLGroup &g : p_in) {
			TLGroup c;
			c.name = str(g.name);
			c.album_artist = str(g.album_artist);
			c.asset_id = str(g.asset_id);
			c.names_begin = r_out.group_names.size();
			for (uint32_t i = 0; i < g.names_count; i++) {
				tl_push(r_out.group_names, str(d.group_names[g.names_begin + i]));
			}
			c.names_count = r_out.group_names.size() - c.names_begin;
			c.tracks_begin = r_out.group_tracks.size();
			for (uint32_t i = 0; i < g.tracks_count; i++) {
				uint32_t s = remap_slot[d.group_tracks[g.tracks_begin + i]];
				if (s != UINT32_MAX) {
					tl_push(r_out.group_tracks, s);
				}
			}
			c.tracks_count = r_out.group_tracks.size() - c.tracks_begin;
			tl_push(r_groups, c);
		}
	};
	copy_groups(d.albums, r_out.albums);
	copy_groups(d.artists, r_out.artists);
}

// ====================================================================
// Handles
// ====================================================================

Ref<TagLibMusicRecord> TagLibLibrary::_handle(uint32_t p_slot) {
	MutexLock ml(handle_mutex);
	const ObjectID *id = handles.getptr(p_slot);
	if (id) {
		TagLibMusicRecord *rec = Object::cast_to<TagLibMusicRecord>(ObjectDB::get_instance(*id));
		// reference() fails once the count has hit zero (the handle is being
		// freed on another thread), so a dying handle is never revived.
		if (rec && rec->reference()) {
			Ref<TagLibMusicRecord> out(rec);
			rec->unreference();
			return out;
		}
	}
	Ref<TagLibMusicRecord> rec;
	rec.instantiate();
	rec->library = Ref<TagLibLibrary>(this);
	rec->slot = p_slot;
	handles.insert(p_slot, rec->get_instance_id());
	return rec;
}

void TagLibLibrary::_forget_handle(uint32_t p_slot, ObjectID p_id) {
	MutexLock ml(handle_mutex);
	const ObjectID *id = handles.getptr(p_slot);
	if (id && *id == p_id) {
		handles.erase(p_slot);
	}
}

TypedArray<TagLibMusicRecord> TagLibLibrary::_handles_for(const LocalVector<uint32_t> &p_slots) {
	TypedArray<TagLibMusicRecord> out;
	out.resize(p_slots.size());
	for (uint32_t i = 0; i < p_slots.size(); i++) {
		out.set(i, _handle(p_slots[i]));
	}
	return out;
}

// ====================================================================
// Record accessors
// ====================================================================

String TagLibLibrary::track_string(uint32_t p_slot, StrField p_field) const {
	RWLockRead r(lock);
	ERR_FAIL_UNSIGNED_INDEX_V(p_slot, d.tracks.size(), String());
	const TLTrack &t = d.tracks[p_slot];
	switch (p_field) {
		case S_PATH:
			return _path_of(p_slot);
		case S_TITLE:
			return d.strings.get(t.title);
		case S_ALBUM:
			return d.strings.get(t.album);
		case S_ARTIST:
			return d.strings.get(t.artist);
		case S_ALBUM_ARTIST:
			return d.strings.get(t.album_artist);
		case S_GENRE:
			return d.strings.get(t.genre);
		case S_MBID:
			return d.strings.get(t.mbid);
	}
	return String();
}

TLTrack TagLibLibrary::track_copy(uint32_t p_slot) const {
	RWLockRead r(lock);
	ERR_FAIL_UNSIGNED_INDEX_V(p_slot, d.tracks.size(), TLTrack());
	return d.tracks[p_slot];
}

void TagLibLibrary::set_track_string(uint32_t p_slot, StrField p_field, const String &p_value) {
	ERR_FAIL_COND_MSG(p_field == S_PATH, "A library track's full_path can't be changed.");
	RWLockWrite w(lock);
	ERR_FAIL_UNSIGNED_INDEX(p_slot, d.tracks.size());
	TLTrack &t = d.tracks[p_slot];
	uint32_t id = d.strings.intern(p_value);
	uint32_t *field = nullptr;
	switch (p_field) {
		case S_TITLE:
			field = &t.title;
			break;
		case S_ALBUM:
			field = &t.album;
			break;
		case S_ARTIST:
			field = &t.artist;
			break;
		case S_ALBUM_ARTIST:
			field = &t.album_artist;
			break;
		case S_GENRE:
			field = &t.genre;
			break;
		case S_MBID:
			field = &t.mbid;
			break;
		case S_PATH:
			return;
	}
	if (*field != id) {
		*field = id;
		garbage = true; // the old value may now be unused
	}
}

void TagLibLibrary::set_track_number(uint32_t p_slot, NumField p_field, double p_value) {
	RWLockWrite w(lock);
	ERR_FAIL_UNSIGNED_INDEX(p_slot, d.tracks.size());
	TLTrack &t = d.tracks[p_slot];
	int64_t v = (int64_t)p_value;
	switch (p_field) {
		case N_LENGTH:
			t.length = (float)p_value;
			break;
		case N_TRACK:
			t.track = _clamp16((int)v);
			break;
		case N_DISC:
			t.disc = _clamp16((int)v);
			break;
		case N_YEAR:
			t.year = _clamp16((int)v);
			break;
		case N_BITRATE:
			t.bitrate = (uint16_t)CLAMP(v, 0, 65535);
			break;
		case N_SAMPLE_RATE:
			t.sample_rate = (uint32_t)CLAMP(v, 0, (int64_t)UINT32_MAX);
			break;
		case N_CHANNELS:
			t.channels = (uint8_t)CLAMP(v, 0, 255);
			break;
		case N_MTIME:
			t.mtime = v;
			break;
		case N_SIZE:
			t.size = v;
			break;
		case N_LISTEN_COUNT:
			t.listen_count = (uint32_t)CLAMP(v, 0, (int64_t)UINT32_MAX);
			break;
	}
}

// ====================================================================
// Script API
// ====================================================================

int TagLibLibrary::get_track_count() const {
	RWLockRead r(lock);
	return (int)live_count;
}

int TagLibLibrary::get_album_count() const {
	RWLockRead r(lock);
	return (int)d.albums.size();
}

int TagLibLibrary::get_artist_count() const {
	RWLockRead r(lock);
	return (int)d.artists.size();
}

Ref<TagLibTrackList> TagLibLibrary::get_all_tracks() {
	Ref<TagLibTrackList> list;
	list.instantiate();
	list->library = Ref<TagLibLibrary>(this);
	RWLockRead r(lock);
	list->entries.resize(d.all_order.size());
	for (uint32_t i = 0; i < d.all_order.size(); i++) {
		list->entries[i] = (int32_t)d.all_order[i];
	}
	return list;
}

TypedArray<TagLibAlbumRecord> TagLibLibrary::get_albums() {
	TypedArray<TagLibAlbumRecord> out;
	Ref<TagLibLibrary> self(this);
	RWLockRead r(lock);
	out.resize(d.albums.size());
	for (uint32_t i = 0; i < d.albums.size(); i++) {
		const TLGroup &g = d.albums[i];
		Ref<TagLibAlbumRecord> a;
		a.instantiate();
		a->library = self;
		a->name = d.strings.get(g.name);
		a->album_artist = d.strings.get(g.album_artist);
		a->asset_id = d.strings.get(g.asset_id);
		a->artists.resize(g.names_count);
		for (uint32_t n = 0; n < g.names_count; n++) {
			a->artists.set(n, d.strings.get(d.group_names[g.names_begin + n]));
		}
		a->slots.resize(g.tracks_count);
		memcpy(a->slots.ptr(), d.group_tracks.ptr() + g.tracks_begin, g.tracks_count * sizeof(uint32_t));
		out.set(i, a);
	}
	return out;
}

TypedArray<TagLibArtistRecord> TagLibLibrary::get_artists() {
	TypedArray<TagLibArtistRecord> out;
	Ref<TagLibLibrary> self(this);
	RWLockRead r(lock);
	out.resize(d.artists.size());
	for (uint32_t i = 0; i < d.artists.size(); i++) {
		const TLGroup &g = d.artists[i];
		Ref<TagLibArtistRecord> a;
		a.instantiate();
		a->library = self;
		a->name = d.strings.get(g.name);
		a->asset_id = d.strings.get(g.asset_id);
		a->slots.resize(g.tracks_count);
		memcpy(a->slots.ptr(), d.group_tracks.ptr() + g.tracks_begin, g.tracks_count * sizeof(uint32_t));
		out.set(i, a);
	}
	return out;
}

Ref<TagLibMusicRecord> TagLibLibrary::find_by_path(const String &p_path) {
	int64_t slot;
	{
		RWLockRead r(lock);
		slot = _find_slot(p_path);
	}
	if (slot < 0) {
		return Ref<TagLibMusicRecord>();
	}
	return _handle((uint32_t)slot);
}

bool TagLibLibrary::has_path(const String &p_path) const {
	RWLockRead r(lock);
	return _find_slot(p_path) >= 0;
}

int64_t TagLibLibrary::get_memory_usage() const {
	RWLockRead r(lock);
	return (int64_t)(d.strings.memory_usage() +
			d.tracks.get_capacity() * sizeof(TLTrack) +
			d.title_rank.get_capacity() * sizeof(uint32_t) +
			d.all_order.get_capacity() * sizeof(uint32_t) +
			(d.albums.get_capacity() + d.artists.get_capacity()) * sizeof(TLGroup) +
			(d.group_tracks.get_capacity() + d.group_names.get_capacity()) * sizeof(uint32_t) +
			path_table.get_capacity() * sizeof(int32_t));
}

// --------------------------------------------------------------------
// Incremental updates
// --------------------------------------------------------------------

Dictionary TagLibLibrary::plan_update(const PackedStringArray &p_current_paths, const Dictionary &p_opts) {
	const bool force_reread = (bool)p_opts.get("force_reread", false);
	const bool follow_retags = (bool)p_opts.get("follow_retags", true);

	// Stamps from scan_directories, only trusted when they line up 1:1.
	PackedInt64Array known_mtimes = p_opts.get("mtimes", PackedInt64Array());
	PackedInt64Array known_sizes = p_opts.get("sizes", PackedInt64Array());
	const bool have_stamps = known_mtimes.size() == p_current_paths.size() &&
			known_sizes.size() == p_current_paths.size();

	PackedStringArray added;
	PackedStringArray changed;
	PackedStringArray removed;
	{
		RWLockRead r(lock);
		LocalVector<uint8_t> seen;
		_zeroed(seen, d.tracks.size());
		HashSet<String> new_seen;

		for (int i = 0; i < p_current_paths.size(); i++) {
			const String &path = p_current_paths[i];
			if (path.is_empty()) {
				continue;
			}
			int64_t slot = _find_slot(path);
			if (slot < 0) {
				if (!new_seen.has(path)) {
					new_seen.insert(path);
					added.push_back(path);
				}
				continue;
			}
			if (seen[slot]) {
				continue;
			}
			seen[slot] = 1;

			bool reread = force_reread;
			if (!reread && follow_retags) {
				int64_t mtime = have_stamps ? known_mtimes[i] : 0;
				int64_t size = have_stamps ? known_sizes[i] : -1;
				if (size < 0) {
					gdtaglib_file_stamp(path, mtime, size);
				}
				const TLTrack &t = d.tracks[slot];
				bool mtime_changed = t.mtime != 0 && mtime != 0 && t.mtime != mtime;
				bool size_changed = t.size >= 0 && size >= 0 && t.size != size;
				reread = mtime_changed || size_changed;
			}
			if (reread) {
				changed.push_back(path);
			}
		}
		for (uint32_t slot = 0; slot < d.tracks.size(); slot++) {
			if ((d.tracks[slot].flags & TL_TRACK_ALIVE) && !seen[slot]) {
				removed.push_back(_path_of(slot));
			}
		}
	}

	Dictionary out;
	out["added"] = added;
	out["changed"] = changed;
	out["removed"] = removed;

	GDTagLib *s = GDTagLib::get_singleton();
	if (s) {
		// Deferred so a caller on a worker thread still delivers on the main one.
		s->call_deferred(SNAME("emit_signal"), SNAME("library_update_planned"),
				added.size(), changed.size(), removed.size());
	}
	return out;
}

Dictionary TagLibLibrary::update(const PackedStringArray &p_current_paths, const PackedStringArray &p_reread_paths,
		const Callable &p_progress, const Dictionary &p_opts) {
	const bool force_reread = (bool)p_opts.get("force_reread", false);

	// order: every current path once, as a slot (>= 0) or -(new index + 1).
	LocalVector<int64_t> order;
	PackedStringArray to_read;
	LocalVector<int64_t> read_target; // same encoding as order
	LocalVector<uint32_t> removed_slots;
	uint32_t new_count = 0;
	{
		RWLockRead r(lock);
		LocalVector<uint8_t> seen;
		_zeroed(seen, d.tracks.size());
		LocalVector<uint8_t> reread;
		_zeroed(reread, d.tracks.size());
		for (const String &path : p_reread_paths) {
			int64_t slot = _find_slot(path);
			if (slot >= 0) {
				reread[slot] = 1;
			}
		}
		HashSet<String> new_seen;

		for (const String &path : p_current_paths) {
			if (path.is_empty()) {
				continue;
			}
			int64_t slot = _find_slot(path);
			if (slot >= 0) {
				if (seen[slot]) {
					continue;
				}
				seen[slot] = 1;
				order.push_back(slot);
				if (force_reread || reread[slot]) {
					to_read.push_back(path);
					read_target.push_back(slot);
				}
			} else {
				if (new_seen.has(path)) {
					continue;
				}
				new_seen.insert(path);
				int64_t enc = -(int64_t)(++new_count);
				order.push_back(enc);
				to_read.push_back(path);
				read_target.push_back(enc);
			}
		}
		for (uint32_t slot = 0; slot < d.tracks.size(); slot++) {
			if ((d.tracks[slot].flags & TL_TRACK_ALIVE) && !seen[slot]) {
				removed_slots.push_back(slot);
			}
		}
	}

	Dictionary out;
	PackedStringArray added;
	PackedStringArray removed;
	PackedStringArray rescanned;
	PackedStringArray changed_albums;
	out["cancelled"] = false;

	if (to_read.is_empty() && removed_slots.is_empty()) {
		out["added"] = added;
		out["removed"] = removed;
		out["rescanned"] = rescanned;
		out["changed_albums"] = changed_albums;
		return out;
	}

	// Slow part, with no lock held: the library stays readable throughout.
	std::vector<TagFields> fields;
	if (!gdtaglib_read_fields(to_read, p_progress, p_opts, fields)) {
		out["cancelled"] = true;
		out["added"] = added;
		out["removed"] = removed;
		out["rescanned"] = rescanned;
		out["changed_albums"] = changed_albums;
		return out;
	}

	{
		RWLockWrite w(lock);
		for (uint32_t slot : removed_slots) {
			d.tracks[slot].flags &= ~TL_TRACK_ALIVE;
			removed.push_back(_path_of(slot));
		}
		if (!removed_slots.is_empty()) {
			garbage = true;
		}

		const uint32_t first_new = d.tracks.size();
		d.tracks.resize(first_new + new_count);
		HashSet<uint32_t> album_ids;
		for (uint32_t k = 0; k < to_read.size(); k++) {
			int64_t target = read_target[k];
			uint32_t slot;
			uint32_t listens = 0;
			if (target >= 0) {
				slot = (uint32_t)target;
				// Play counts aren't in the tags, so a re-read keeps them.
				listens = d.tracks[slot].listen_count;
				rescanned.push_back(to_read[k]);
				garbage = true;
			} else {
				slot = first_new + (uint32_t)(-target - 1);
				added.push_back(to_read[k]);
			}
			TLTrack t;
			_set_from_fields(t, to_read[k], fields[k]);
			t.listen_count = listens;
			t.flags = TL_TRACK_ALIVE;
			d.tracks[slot] = t;
			album_ids.insert(t.album);
		}
		fields.clear();

		LocalVector<uint32_t> live_order;
		live_order.resize(order.size());
		for (uint32_t i = 0; i < order.size(); i++) {
			int64_t e = order[i];
			live_order[i] = e >= 0 ? (uint32_t)e : first_new + (uint32_t)(-e - 1);
		}
		_rebuild_path_table();
		_regroup(live_order);

		for (uint32_t id : album_ids) {
			changed_albums.push_back(d.strings.get(id));
		}
	}

	out["added"] = added;
	out["removed"] = removed;
	out["rescanned"] = rescanned;
	out["changed_albums"] = changed_albums;
	return out;
}
