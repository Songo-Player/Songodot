#ifndef GDTAGLIB_H
#define GDTAGLIB_H

#include "taglib_records.h"

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/variant/typed_array.h"
#include "scene/resources/texture.h"

// TagLib helpers: tag reading, directory scanning and cover art. Everything
// is static; the single module-owned instance (get_singleton()) only exists
// to carry signals (library_update_planned, emitted by
// TagLibLibrary.plan_update). The music library itself is TagLibLibrary
// (taglib_records.h).
class GDTagLib : public Object {
	GDCLASS(GDTagLib, Object);

	static GDTagLib *singleton;

protected:
	static void _bind_methods();

public:
	GDTagLib();
	~GDTagLib();
	static GDTagLib *get_singleton();

	// Reads tags for every path on a pool of worker threads and returns
	// standalone (not library-backed) records in p_paths order, with no
	// de-duplication. A music library should use TagLibLibrary.update
	// instead, which reads with the same options straight into its compact
	// store. progress, if valid, is called with the number of files read so
	// far, throttled and always on the calling thread; returning true cancels
	// and returns an empty array.
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
	//           parallel to its paths, ready for TagLibLibrary.plan_update.
	static Dictionary scan_directories(const PackedStringArray &p_roots, const Dictionary &p_groups,
			const Callable &p_progress, const Dictionary &p_opts);

	// Decoded embedded cover as an Image, with no texture creation. Safe on
	// worker threads.
	static Ref<Image> get_cover(const String &p_path);
	// get_cover() uploaded as an ImageTexture.
	static Ref<Texture2D> get_cover_image(const String &p_path);
};

#endif // GDTAGLIB_H
