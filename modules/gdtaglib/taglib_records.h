#ifndef GDTAGLIB_TAGLIB_RECORDS_H
#define GDTAGLIB_TAGLIB_RECORDS_H

// Plain data Resource types produced by GDTagLib's library helpers. Named
// with a "TagLib" prefix so they can sit alongside a project's own
// MusicRecord / AlbumRecord / ArtistRecord classes.

#include "core/io/resource.h"
#include "core/variant/typed_array.h"

// Compact inline accessors, invoked from a public: section of each class.
#define TLR_PROP_STR(m_name)                                       \
	void set_##m_name(const String &p_value) { m_name = p_value; } \
	String get_##m_name() const { return m_name; }
#define TLR_PROP(m_type, m_name)                            \
	void set_##m_name(m_type p_value) { m_name = p_value; } \
	m_type get_##m_name() const { return m_name; }

// One music file's metadata plus its full_path. Album art is not carried
// here; use GDTagLib::get_cover / get_cover_image.
class TagLibMusicRecord : public Resource {
	GDCLASS(TagLibMusicRecord, Resource);

	String full_path;
	String title;
	String album;
	String artist;
	String album_artist;
	String genre;
	String musicbrainz_album_id; // preferred album identity when present
	double raw_length = 0.0; // seconds
	int track = 0;
	int disc = 0;
	int year = 0;
	int bitrate = 0; // kb/s
	int sample_rate = 0; // Hz
	int channels = 0;
	// Filesystem stamps, used to decide whether a still-present file needs
	// its tags re-read. 0 / -1 mean unknown.
	int64_t file_mtime = 0;
	int64_t file_size = -1;
	// Play count. User data, not from tags: kept across re-reads and
	// persisted by GDTagLib::save_music_library.
	int listen_count = 0;

protected:
	static void _bind_methods();

public:
	TLR_PROP_STR(full_path)
	TLR_PROP_STR(title)
	TLR_PROP_STR(album)
	TLR_PROP_STR(artist)
	TLR_PROP_STR(album_artist)
	TLR_PROP_STR(genre)
	TLR_PROP_STR(musicbrainz_album_id)
	TLR_PROP(double, raw_length)
	TLR_PROP(int, track)
	TLR_PROP(int, disc)
	TLR_PROP(int, year)
	TLR_PROP(int, bitrate)
	TLR_PROP(int, sample_rate)
	TLR_PROP(int, channels)
	TLR_PROP(int64_t, file_mtime)
	TLR_PROP(int64_t, file_size)
	TLR_PROP(int, listen_count)

	// "m:ss" style duration, e.g. 63.0 -> "1:03".
	String get_length_string() const;
	// "07. Title" when track > 0, otherwise just the title.
	String get_title_with_track() const;
	// Lowercase file extension ("mp3", "flac", ...), or "stream" when
	// full_path is a URL.
	String get_file_type() const;
};

// A group of TagLibMusicRecords that share an album.
class TagLibAlbumRecord : public Resource {
	GDCLASS(TagLibAlbumRecord, Resource);

	// name is Resource's own (set_name / get_name are already bound there).
	String album_artist;
	String asset_id;
	PackedStringArray artists; // every distinct per-track artist, first-seen order
	TypedArray<TagLibMusicRecord> music_records;

protected:
	static void _bind_methods();

public:
	TLR_PROP_STR(album_artist)
	TLR_PROP_STR(asset_id)

	void set_artists(const PackedStringArray &p_value) { artists = p_value; }
	PackedStringArray get_artists() const { return artists; }

	void set_music_records(const TypedArray<TagLibMusicRecord> &p_value) { music_records = p_value; }
	TypedArray<TagLibMusicRecord> get_music_records() const { return music_records; }
};

// A group of TagLibMusicRecords attributed to one album artist. Tracks on
// "Various Artists" albums are not filed under any artist.
class TagLibArtistRecord : public Resource {
	GDCLASS(TagLibArtistRecord, Resource);

	// name is Resource's own (set_name / get_name are already bound there).
	String asset_id;
	TypedArray<TagLibMusicRecord> music_records;

protected:
	static void _bind_methods();

public:
	TLR_PROP_STR(asset_id)

	void set_music_records(const TypedArray<TagLibMusicRecord> &p_value) { music_records = p_value; }
	TypedArray<TagLibMusicRecord> get_music_records() const { return music_records; }
};

#undef TLR_PROP_STR
#undef TLR_PROP

#endif // GDTAGLIB_TAGLIB_RECORDS_H
