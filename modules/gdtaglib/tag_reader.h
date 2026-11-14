#ifndef GDTAGLIB_TAG_READER_H
#define GDTAGLIB_TAG_READER_H

// Internal interface between the TagLib reader (gdtaglib.cpp) and the
// music library store (music_library.cpp / taglib_records.h). Not exposed to scripts.

#include "core/string/ustring.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

#include <vector>

// Raw tag values for one file, as read (no fallbacks applied).
struct TagFields {
	String title;
	String artist;
	String album;
	String genre;
	String album_artist;
	String musicbrainz_album_id;
	int year = 0;
	int track = 0;
	int disc = 0;
	int duration = 0;
	int bitrate = 0;
	int sample_rate = 0;
	int channels = 0;
	int64_t file_mtime = 0;
	int64_t file_size = -1;
};

// Reads tags + filesystem stamps for every path on a pool of worker threads,
// into r_fields (resized to p_paths.size(), same order). p_progress, if
// valid, is called on the calling thread with the number of files done;
// returning true cancels. Options are build_music_records' (see gdtaglib.h).
// Returns false when cancelled.
bool gdtaglib_read_fields(const PackedStringArray &p_paths, const Callable &p_progress,
		const Dictionary &p_opts, std::vector<TagFields> &r_fields);

// mtime (seconds) / size (bytes) of a res:// / user:// / absolute path, or
// 0 / -1 when it can't be stat()ed.
void gdtaglib_file_stamp(const String &p_path, int64_t &r_mtime, int64_t &r_size);

#endif // GDTAGLIB_TAG_READER_H
