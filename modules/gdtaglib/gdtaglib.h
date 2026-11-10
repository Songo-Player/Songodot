#ifndef GDTAGLIB_H
#define GDTAGLIB_H

#include "taglib_records.h"

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/variant/typed_array.h"
#include "scene/resources/texture.h"

// Music library helpers backed by TagLib. Everything is static; the single
// module-owned instance (get_singleton()) only exists to carry signals.
class GDTagLib : public Object {
	GDCLASS(GDTagLib, Object);

	static GDTagLib *singleton;

protected:
	static void _bind_methods();

public:
	GDTagLib();
	~GDTagLib();
	static GDTagLib *get_singleton();

	// Reads tags for every path on a pool of worker threads and returns the
	// records in p_paths order (no de-duplication). progress, if valid, is
	// called with the number of files read so far, throttled and always on
	// the calling thread; returning true cancels and returns an empty array.
	//
	// opts (all optional):
	//   threads               int   - worker count (default: hardware threads,
	//                                 capped at 8)
	//   fast_properties       bool  - TagLib Fast read style (default false).
	//                                 Skips ADTS .aac durations and ID3v2 tags
	//                                 not at offset 0 in exchange for less I/O.
	//   progress_interval_ms  int   - minimum gap between progress calls (100)
	//   prefetch_ahead        int   - files a separate I/O thread pre-reads
	//                                 ahead of the workers (8, 0 = off; not on
	//                                 Windows)
	//   head_kb / tail_kb     int   - bytes read up front from each end of a
	//                                 file in one request each (64 / 16)
	//
	// Files are read grouped by folder regardless of p_paths order.
	static TypedArray<TagLibMusicRecord> build_music_records(
			const PackedStringArray &p_paths,
			const Callable &p_progress,
			const Dictionary &p_opts);

	// Incremental rebuild. Reuses previous records whose file is still
	// present and unchanged, reads tags for new or modified files, drops
	// records for files that disappeared, then recomputes the album and
	// (album) artist groupings from the whole set. The result also carries
	// "added", "removed" and "rescanned" PackedStringArrays.
	//
	// opts (all optional):
	//   force_reread   bool  - re-read tags for every still-present file
	//   follow_retags  bool  - re-read when mtime/size changed (default true)
	static Dictionary refresh_music_library(
			const Array &p_previous_records,
			const PackedStringArray &p_current_paths,
			const Dictionary &p_opts);

	// Parse-free preview of a refresh_music_library() call: diffs the current
	// paths against previous records (path set + mtime/size) without reading
	// tags. Returns { added, changed, removed } PackedStringArrays and emits
	// `library_update_planned` (deferred, so on the main thread) with the
	// three counts.
	//
	// opts: force_reread / follow_retags as for refresh_music_library, plus
	// optional "mtimes" / "sizes" PackedInt64Arrays parallel to current_paths
	// (scan_directories' stamps) used instead of a stat() per file. Entries
	// with size -1 are stat()ed.
	static Dictionary plan_music_library_update(
			const Array &p_previous_records,
			const PackedStringArray &p_current_paths,
			const Dictionary &p_opts);

	// One native walk of every root, sorting matching files into groups by
	// extension: groups is { "name": PackedStringArray(["mp3", ...]) } and the
	// result is { "name": PackedStringArray(paths) }. Paths are built from the
	// roots as given (root.path_join(...)), hidden entries are skipped and
	// symlinked directories are followed once. progress, if valid, is called
	// with the running match count every few dozen files and once at the end,
	// on the calling thread.
	//
	// opts (all optional):
	//   stamps  PackedStringArray - groups to also stat while walking; each
	//           gets "<name>_mtimes" / "<name>_sizes" PackedInt64Arrays
	//           parallel to its paths, ready for plan_music_library_update.
	static Dictionary scan_directories(const PackedStringArray &p_roots, const Dictionary &p_groups,
			const Callable &p_progress, const Dictionary &p_opts);

	// Persist / restore a whole library ({ music_records, albums, artists },
	// as refresh_music_library returns) as a flat binary file, keeping
	// listen_count. Albums / artists come back pointing at the same record
	// instances as music_records. load returns an empty Dictionary when the
	// file is missing, corrupt or from a newer format version.
	static Error save_music_library(const String &p_path, const Dictionary &p_library);
	static Dictionary load_music_library(const String &p_path);

	// Decoded embedded cover as an Image, with no texture creation. Safe on
	// worker threads.
	static Ref<Image> get_cover(const String &p_path);
	// get_cover() uploaded as an ImageTexture.
	static Ref<Texture2D> get_cover_image(const String &p_path);
};

#endif // GDTAGLIB_H
