#include "gdtaglib.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// ====================================================================
// Flat binary library store (save_music_library / load_music_library)
// ====================================================================
//
// Layout, all integers little-endian:
//
//   "GDTL"  u32 version
//   u32 string_count   { u32 byte_len, utf8 bytes } * string_count
//   u32 record_count   { record } * record_count
//   u32 album_count    { album } * album_count
//   u32 artist_count   { artist } * artist_count
//
//   record: u32 str x7 (full_path, title, album, artist, album_artist, genre,
//           musicbrainz_album_id), f64 raw_length, i32 x6 (track, disc, year,
//           bitrate, sample_rate, channels), i64 file_mtime, i64 file_size,
//           i32 listen_count
//   album:  u32 str x3 (name, album_artist, asset_id),
//           u32 n { u32 str } * n            (artists)
//           u32 n { u32 record_index } * n   (music_records)
//   artist: u32 str x2 (name, asset_id),
//           u32 n { u32 record_index } * n   (music_records)
//
// Every string is an index into the string table, so the many repeated
// artist / album / genre values are stored (and allocated on load) once.
// Albums and artists reference records by index, so loading restores the
// shared instances rather than copies.
//
// This is the same format the gdtaglib GDExtension wrote, so existing
// library files keep loading. When bumping LIBRARY_FORMAT_VERSION keep the
// reader able to parse older versions: listen counts are user data that a
// re-import can't recover.

static const char LIBRARY_MAGIC[4] = { 'G', 'D', 'T', 'L' };
static const uint32_t LIBRARY_FORMAT_VERSION = 1;

namespace {

void _put_u32(std::vector<uint8_t> &r_buf, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	r_buf.insert(r_buf.end(), b, b + 4);
}

class LibraryWriter {
	// Everything after the string table; the table is only complete once
	// every record has been written, so it's prepended in finish().
	std::vector<uint8_t> body;
	std::vector<std::string> strings;
	std::unordered_map<std::string, uint32_t> string_index;

public:
	void u32(uint32_t v) { _put_u32(body, v); }
	void i32(int32_t v) { u32((uint32_t)v); }
	void i64(int64_t v) {
		u32((uint32_t)((uint64_t)v & 0xffffffffu));
		u32((uint32_t)((uint64_t)v >> 32));
	}
	void f64(double v) {
		int64_t bits;
		memcpy(&bits, &v, sizeof(bits));
		i64(bits);
	}
	void str(const String &p_value) {
		CharString cs = p_value.utf8();
		std::string s(cs.get_data(), cs.length());
		auto it = string_index.find(s);
		if (it != string_index.end()) {
			u32(it->second);
			return;
		}
		uint32_t idx = (uint32_t)strings.size();
		string_index.emplace(s, idx);
		strings.push_back(std::move(s));
		u32(idx);
	}

	// Header + string table + body, ready to write out.
	Vector<uint8_t> finish() {
		std::vector<uint8_t> head(LIBRARY_MAGIC, LIBRARY_MAGIC + 4);
		_put_u32(head, LIBRARY_FORMAT_VERSION);
		_put_u32(head, (uint32_t)strings.size());
		for (const std::string &s : strings) {
			_put_u32(head, (uint32_t)s.size());
			head.insert(head.end(), s.begin(), s.end());
		}
		Vector<uint8_t> out;
		out.resize((int64_t)(head.size() + body.size()));
		uint8_t *w = out.ptrw();
		memcpy(w, head.data(), head.size());
		memcpy(w + head.size(), body.data(), body.size());
		return out;
	}
};

class LibraryReader {
	const uint8_t *data;
	size_t size;
	size_t pos = 0;

public:
	bool failed = false;
	std::vector<String> strings;

	LibraryReader(const uint8_t *p_data, size_t p_size) :
			data(p_data), size(p_size) {}

	bool has(size_t p_bytes) {
		if (failed || size - pos < p_bytes) {
			failed = true;
			return false;
		}
		return true;
	}
	const uint8_t *take(size_t p_bytes) {
		if (!has(p_bytes)) {
			return nullptr;
		}
		const uint8_t *p = data + pos;
		pos += p_bytes;
		return p;
	}
	uint32_t u32() {
		const uint8_t *b = take(4);
		if (!b) {
			return 0;
		}
		return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	}
	int32_t i32() { return (int32_t)u32(); }
	int64_t i64() {
		uint64_t lo = u32();
		uint64_t hi = u32();
		return (int64_t)(lo | (hi << 32));
	}
	double f64() {
		int64_t bits = i64();
		double v;
		memcpy(&v, &bits, sizeof(v));
		return v;
	}
	String str() {
		uint32_t idx = u32();
		if (idx >= strings.size()) {
			failed = true;
			return String();
		}
		return strings[idx];
	}
	// A count that must be followed by at least p_min_each bytes per element;
	// rejects absurd counts from a corrupt file before anything is allocated.
	uint32_t count(size_t p_min_each) {
		uint32_t n = u32();
		if (!failed && p_min_each > 0 && (size - pos) / p_min_each < n) {
			failed = true;
			return 0;
		}
		return n;
	}
};

} // namespace

Error GDTagLib::save_music_library(const String &p_path, const Dictionary &p_library) {
	Array records = p_library.get("music_records", Array());
	Array albums = p_library.get("albums", Array());
	Array artists = p_library.get("artists", Array());

	LibraryWriter w;

	// Record identity -> index, so albums / artists can point at them.
	std::unordered_map<const TagLibMusicRecord *, uint32_t> record_index;
	std::vector<Ref<TagLibMusicRecord>> valid_records;
	for (int i = 0; i < records.size(); i++) {
		Ref<TagLibMusicRecord> r = records[i];
		if (r.is_null() || record_index.count(r.ptr())) {
			continue;
		}
		record_index.emplace(r.ptr(), (uint32_t)valid_records.size());
		valid_records.push_back(r);
	}

	w.u32((uint32_t)valid_records.size());
	for (const Ref<TagLibMusicRecord> &r : valid_records) {
		w.str(r->get_full_path());
		w.str(r->get_title());
		w.str(r->get_album());
		w.str(r->get_artist());
		w.str(r->get_album_artist());
		w.str(r->get_genre());
		w.str(r->get_musicbrainz_album_id());
		w.f64(r->get_raw_length());
		w.i32(r->get_track());
		w.i32(r->get_disc());
		w.i32(r->get_year());
		w.i32(r->get_bitrate());
		w.i32(r->get_sample_rate());
		w.i32(r->get_channels());
		w.i64(r->get_file_mtime());
		w.i64(r->get_file_size());
		w.i32(r->get_listen_count());
	}

	// Albums / artists may only reference records in music_records; anything
	// else is dropped from the saved grouping.
	auto write_record_refs = [&](const TypedArray<TagLibMusicRecord> &p_refs) {
		std::vector<uint32_t> idx;
		for (int i = 0; i < p_refs.size(); i++) {
			Ref<TagLibMusicRecord> r = p_refs[i];
			if (r.is_null()) {
				continue;
			}
			auto it = record_index.find(r.ptr());
			if (it != record_index.end()) {
				idx.push_back(it->second);
			}
		}
		w.u32((uint32_t)idx.size());
		for (uint32_t v : idx) {
			w.u32(v);
		}
	};

	std::vector<Ref<TagLibAlbumRecord>> valid_albums;
	for (int i = 0; i < albums.size(); i++) {
		Ref<TagLibAlbumRecord> a = albums[i];
		if (a.is_valid()) {
			valid_albums.push_back(a);
		}
	}
	w.u32((uint32_t)valid_albums.size());
	for (const Ref<TagLibAlbumRecord> &a : valid_albums) {
		w.str(a->get_name());
		w.str(a->get_album_artist());
		w.str(a->get_asset_id());
		PackedStringArray names = a->get_artists();
		w.u32((uint32_t)names.size());
		for (const String &name : names) {
			w.str(name);
		}
		write_record_refs(a->get_music_records());
	}

	std::vector<Ref<TagLibArtistRecord>> valid_artists;
	for (int i = 0; i < artists.size(); i++) {
		Ref<TagLibArtistRecord> a = artists[i];
		if (a.is_valid()) {
			valid_artists.push_back(a);
		}
	}
	w.u32((uint32_t)valid_artists.size());
	for (const Ref<TagLibArtistRecord> &a : valid_artists) {
		w.str(a->get_name());
		w.str(a->get_asset_id());
		write_record_refs(a->get_music_records());
	}

	Vector<uint8_t> bytes = w.finish();

	// Write beside the target then rename over it, so a crash or power loss
	// mid-save leaves the previous library intact instead of a torn file.
	String tmp_path = p_path + ".tmp";
	{
		Error err = OK;
		Ref<FileAccess> f = FileAccess::open(tmp_path, FileAccess::WRITE, &err);
		if (f.is_null()) {
			ERR_PRINT("GDTagLib: can't open '" + tmp_path + "' for writing.");
			return err != OK ? err : ERR_FILE_CANT_WRITE;
		}
		f->store_buffer(bytes);
		f->flush();
		err = f->get_error();
		f->close();
		if (err != OK) {
			DirAccess::remove_absolute(tmp_path);
			ERR_PRINT("GDTagLib: failed writing '" + tmp_path + "'.");
			return err;
		}
	}
	Error err = DirAccess::rename_absolute(tmp_path, p_path);
	if (err != OK) {
		DirAccess::remove_absolute(tmp_path);
		ERR_PRINT("GDTagLib: can't move '" + tmp_path + "' to '" + p_path + "'.");
	}
	return err;
}

Dictionary GDTagLib::load_music_library(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return Dictionary();
	}
	Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path);
	LibraryReader r(bytes.ptr(), (size_t)bytes.size());

	const uint8_t *magic = r.take(4);
	if (!magic || memcmp(magic, LIBRARY_MAGIC, 4) != 0) {
		ERR_PRINT("GDTagLib: '" + p_path + "' is not a music library file.");
		return Dictionary();
	}
	uint32_t version = r.u32();
	if (version == 0 || version > LIBRARY_FORMAT_VERSION) {
		ERR_PRINT("GDTagLib: '" + p_path + "' has unsupported library format version " + itos(version) + ".");
		return Dictionary();
	}

	uint32_t string_count = r.count(4);
	r.strings.reserve(string_count);
	for (uint32_t i = 0; i < string_count && !r.failed; i++) {
		uint32_t len = r.u32();
		const uint8_t *p = r.take(len);
		if (!p) {
			break;
		}
		r.strings.push_back(len == 0 ? String() : String::utf8((const char *)p, (int)len));
	}

	// 7 strings + f64 + 6 i32 + 2 i64 + i32
	const size_t RECORD_BYTES = 7 * 4 + 8 + 6 * 4 + 2 * 8 + 4;
	uint32_t record_count = r.count(RECORD_BYTES);
	std::vector<Ref<TagLibMusicRecord>> records;
	records.reserve(record_count);
	TypedArray<TagLibMusicRecord> out_records;
	for (uint32_t i = 0; i < record_count && !r.failed; i++) {
		Ref<TagLibMusicRecord> rec;
		rec.instantiate();
		rec->set_full_path(r.str());
		rec->set_title(r.str());
		rec->set_album(r.str());
		rec->set_artist(r.str());
		rec->set_album_artist(r.str());
		rec->set_genre(r.str());
		rec->set_musicbrainz_album_id(r.str());
		rec->set_raw_length(r.f64());
		rec->set_track(r.i32());
		rec->set_disc(r.i32());
		rec->set_year(r.i32());
		rec->set_bitrate(r.i32());
		rec->set_sample_rate(r.i32());
		rec->set_channels(r.i32());
		rec->set_file_mtime(r.i64());
		rec->set_file_size(r.i64());
		rec->set_listen_count(r.i32());
		records.push_back(rec);
		out_records.push_back(rec);
	}

	auto read_record_refs = [&]() {
		TypedArray<TagLibMusicRecord> refs;
		uint32_t n = r.count(4);
		for (uint32_t i = 0; i < n && !r.failed; i++) {
			uint32_t idx = r.u32();
			if (idx >= records.size()) {
				r.failed = true;
				break;
			}
			refs.push_back(records[idx]);
		}
		return refs;
	};

	TypedArray<TagLibAlbumRecord> out_albums;
	uint32_t album_count = r.count(3 * 4 + 4 + 4);
	for (uint32_t i = 0; i < album_count && !r.failed; i++) {
		Ref<TagLibAlbumRecord> a;
		a.instantiate();
		a->set_name(r.str());
		a->set_album_artist(r.str());
		a->set_asset_id(r.str());
		PackedStringArray names;
		uint32_t n = r.count(4);
		for (uint32_t j = 0; j < n && !r.failed; j++) {
			names.push_back(r.str());
		}
		a->set_artists(names);
		a->set_music_records(read_record_refs());
		out_albums.push_back(a);
	}

	TypedArray<TagLibArtistRecord> out_artists;
	uint32_t artist_count = r.count(2 * 4 + 4);
	for (uint32_t i = 0; i < artist_count && !r.failed; i++) {
		Ref<TagLibArtistRecord> a;
		a.instantiate();
		a->set_name(r.str());
		a->set_asset_id(r.str());
		a->set_music_records(read_record_refs());
		out_artists.push_back(a);
	}

	if (r.failed) {
		ERR_PRINT("GDTagLib: '" + p_path + "' is truncated or corrupt.");
		return Dictionary();
	}

	Dictionary out;
	out["music_records"] = out_records;
	out["albums"] = out_albums;
	out["artists"] = out_artists;
	return out;
}
