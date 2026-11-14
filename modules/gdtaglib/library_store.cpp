#include "taglib_records.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include <cstring>

// ====================================================================
// Library file (TagLibLibrary.load_file / save)
// ====================================================================
//
// Version 2 layout, all little-endian. Every section is a u32 element count
// followed by the elements, written straight from memory, so loading is a
// handful of reads with no per-track work:
//
//   "GDTL"  u32 version (2)  u32 flags (bit 0: has garbage, compact on load)
//   u32 string_count  u32 byte_length  u32 offsets[string_count + 1]  bytes
//   u32 n  TLTrack[n]          (72 bytes each, see taglib_records.h)
//   u32 n  u32 title_rank[n]
//   u32 n  u32 all_order[n]
//   u32 n  TLGroup[n]          albums  (32 bytes each)
//   u32 n  TLGroup[n]          artists
//   u32 n  u32 group_tracks[n]
//   u32 n  u32 group_names[n]
//
// Saving never compacts (it runs for play counts, on the main thread), so
// removed tracks and orphaned strings are written too and flagged; the next
// load drops them before any record exists.
//
// Any other version (including the earlier Resource-based v1 store) is
// rejected: the library loads empty and the next import rebuilds it.

#if defined(BIG_ENDIAN_ENABLED)
#error "The GDTL v2 library format is written from memory and assumes a little-endian target."
#endif

static const char LIBRARY_MAGIC[4] = { 'G', 'D', 'T', 'L' };
static const uint32_t LIBRARY_FORMAT_V2 = 2;
static const uint32_t LIBRARY_FLAG_GARBAGE = 1;

namespace {

template <typename T>
void _store_section(const Ref<FileAccess> &p_file, const TLVec<T> &p_items) {
	p_file->store_32(p_items.size());
	if (!p_items.is_empty()) {
		p_file->store_buffer((const uint8_t *)p_items.ptr(), (uint64_t)p_items.size() * sizeof(T));
	}
}

// Reads a section written by _store_section, refusing counts that can't fit
// in the rest of the file (so a corrupt count never allocates).
template <typename T>
bool _load_section(const Ref<FileAccess> &p_file, TLVec<T> &r_items) {
	uint64_t remaining = p_file->get_length() - p_file->get_position();
	if (remaining < 4) {
		return false;
	}
	uint32_t n = p_file->get_32();
	uint64_t bytes = (uint64_t)n * sizeof(T);
	if (bytes > remaining - 4) {
		return false;
	}
	r_items.resize(n);
	return n == 0 || p_file->get_buffer((uint8_t *)r_items.ptr(), bytes) == bytes;
}

// Every member of TLData is a LocalVector (or a pool made of them), which
// owns a heap block through a plain pointer, so swapping the bytes swaps
// ownership without copying any data.
void _swap_data(TLData &a, TLData &b) {
	alignas(TLData) uint8_t tmp[sizeof(TLData)];
	memcpy(tmp, (void *)&a, sizeof(TLData));
	memcpy((void *)&a, (void *)&b, sizeof(TLData));
	memcpy((void *)&b, tmp, sizeof(TLData));
}

} // namespace

// --------------------------------------------------------------------
// Validation / load completion (caller holds the lock, or owns `this`)
// --------------------------------------------------------------------

bool TagLibLibrary::_validate_loaded() const {
	if (!d.strings.validate_loaded()) {
		return false;
	}
	const uint32_t sc = d.strings.count();
	const uint32_t tc = d.tracks.size();
	if (d.title_rank.size() != tc) {
		return false;
	}
	for (const TLTrack &t : d.tracks) {
		if (t.dir >= sc || t.file >= sc || t.title >= sc || t.album >= sc || t.artist >= sc ||
				t.album_artist >= sc || t.genre >= sc || t.mbid >= sc) {
			return false;
		}
	}
	for (uint32_t s : d.all_order) {
		if (s >= tc) {
			return false;
		}
	}
	for (uint32_t s : d.group_tracks) {
		if (s >= tc) {
			return false;
		}
	}
	for (uint32_t s : d.group_names) {
		if (s >= sc) {
			return false;
		}
	}
	auto groups_ok = [&](const TLVec<TLGroup> &p_groups) {
		for (const TLGroup &g : p_groups) {
			if (g.name >= sc || g.album_artist >= sc || g.asset_id >= sc) {
				return false;
			}
			if ((uint64_t)g.names_begin + g.names_count > d.group_names.size() ||
					(uint64_t)g.tracks_begin + g.tracks_count > d.group_tracks.size()) {
				return false;
			}
		}
		return true;
	};
	return groups_ok(d.albums) && groups_ok(d.artists);
}

void TagLibLibrary::_finish_load() {
	d.strings.finish_load();
	_rebuild_path_table();
	if (garbage) {
		// No record or list can point at this library yet, so renumbering
		// slots is safe here (and only here).
		TLData compact;
		_compact_into(compact);
		_swap_data(d, compact);
		garbage = false;
		_rebuild_path_table();
	}
}

// --------------------------------------------------------------------
// Writing
// --------------------------------------------------------------------

Error TagLibLibrary::save(const String &p_path) {
	RWLockRead r(lock);

	// Write beside the target then rename over it, so a crash or power loss
	// mid-save leaves the previous library intact instead of a torn file.
	String tmp_path = p_path + ".tmp";
	{
		Error err = OK;
		Ref<FileAccess> f = FileAccess::open(tmp_path, FileAccess::WRITE, &err);
		if (f.is_null()) {
			ERR_PRINT("TagLibLibrary: can't open '" + tmp_path + "' for writing.");
			return err != OK ? err : ERR_FILE_CANT_WRITE;
		}
		f->store_buffer((const uint8_t *)LIBRARY_MAGIC, 4);
		f->store_32(LIBRARY_FORMAT_V2);
		f->store_32(garbage ? LIBRARY_FLAG_GARBAGE : 0);

		const TLVec<uint32_t> &offsets = d.strings.get_offsets();
		const TLVec<char> &bytes = d.strings.get_bytes();
		f->store_32(d.strings.count());
		f->store_32(bytes.size());
		f->store_buffer((const uint8_t *)offsets.ptr(), (uint64_t)offsets.size() * sizeof(uint32_t));
		if (!bytes.is_empty()) {
			f->store_buffer((const uint8_t *)bytes.ptr(), bytes.size());
		}
		_store_section(f, d.tracks);
		_store_section(f, d.title_rank);
		_store_section(f, d.all_order);
		_store_section(f, d.albums);
		_store_section(f, d.artists);
		_store_section(f, d.group_tracks);
		_store_section(f, d.group_names);

		f->flush();
		err = f->get_error();
		f->close();
		if (err != OK) {
			DirAccess::remove_absolute(tmp_path);
			ERR_PRINT("TagLibLibrary: failed writing '" + tmp_path + "'.");
			return err;
		}
	}
	Error err = DirAccess::rename_absolute(tmp_path, p_path);
	if (err != OK) {
		DirAccess::remove_absolute(tmp_path);
		ERR_PRINT("TagLibLibrary: can't move '" + tmp_path + "' to '" + p_path + "'.");
	}
	return err;
}

// --------------------------------------------------------------------
// Reading
// --------------------------------------------------------------------

Ref<TagLibLibrary> TagLibLibrary::load_file(const String &p_path) {
	Ref<TagLibLibrary> lib;
	lib.instantiate();
	if (!FileAccess::exists(p_path)) {
		return lib;
	}
	Error err;
	{
		RWLockWrite w(lib->lock);
		err = lib->_read_file(p_path);
		if (err != OK) {
			lib->d.clear();
			lib->garbage = false;
			lib->_rebuild_path_table();
		}
	}
	if (err != OK) {
		ERR_PRINT("TagLibLibrary: '" + p_path + "' is not a readable music library (error " + itos(err) + ").");
	}
	return lib;
}

Error TagLibLibrary::_read_file(const String &p_path) {
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (f.is_null()) {
		return err != OK ? err : ERR_FILE_CANT_OPEN;
	}
	uint8_t magic[4];
	if (f->get_buffer(magic, 4) != 4 || memcmp(magic, LIBRARY_MAGIC, 4) != 0) {
		return ERR_FILE_UNRECOGNIZED;
	}
	uint32_t version = f->get_32();
	if (version != LIBRARY_FORMAT_V2) {
		return ERR_FILE_UNRECOGNIZED;
	}
	uint32_t flags = f->get_32();

	uint32_t string_count = f->get_32();
	uint32_t byte_length = f->get_32();
	uint64_t remaining = f->get_length() - f->get_position();
	uint64_t offsets_bytes = ((uint64_t)string_count + 1) * sizeof(uint32_t);
	if (offsets_bytes + byte_length > remaining) {
		return ERR_FILE_CORRUPT;
	}
	uint32_t *offsets = d.strings.prepare_offsets(string_count);
	if (f->get_buffer((uint8_t *)offsets, offsets_bytes) != offsets_bytes) {
		return ERR_FILE_CORRUPT;
	}
	char *bytes = d.strings.prepare_bytes(byte_length);
	if (byte_length > 0 && f->get_buffer((uint8_t *)bytes, byte_length) != byte_length) {
		return ERR_FILE_CORRUPT;
	}

	if (!_load_section(f, d.tracks) || !_load_section(f, d.title_rank) || !_load_section(f, d.all_order) ||
			!_load_section(f, d.albums) || !_load_section(f, d.artists) ||
			!_load_section(f, d.group_tracks) || !_load_section(f, d.group_names)) {
		return ERR_FILE_CORRUPT;
	}
	if (!_validate_loaded()) {
		return ERR_FILE_CORRUPT;
	}
	garbage = (flags & LIBRARY_FLAG_GARBAGE) != 0;
	for (const TLTrack &t : d.tracks) {
		if (!(t.flags & TL_TRACK_ALIVE)) {
			garbage = true;
			break;
		}
	}
	_finish_load();
	return OK;
}
