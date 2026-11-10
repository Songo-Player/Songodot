#include "taglib_records.h"

#include "core/object/class_db.h"

#define TLR_BIND_STR(m_class, m_prop)                                                \
	ClassDB::bind_method(D_METHOD("set_" #m_prop, "value"), &m_class::set_##m_prop); \
	ClassDB::bind_method(D_METHOD("get_" #m_prop), &m_class::get_##m_prop);          \
	ADD_PROPERTY(PropertyInfo(Variant::STRING, #m_prop), "set_" #m_prop, "get_" #m_prop);

#define TLR_BIND_NUM(m_class, m_prop, m_variant_type)                                \
	ClassDB::bind_method(D_METHOD("set_" #m_prop, "value"), &m_class::set_##m_prop); \
	ClassDB::bind_method(D_METHOD("get_" #m_prop), &m_class::get_##m_prop);          \
	ADD_PROPERTY(PropertyInfo(m_variant_type, #m_prop), "set_" #m_prop, "get_" #m_prop);

// Albums and artists keep their name in Resource's name (resource_name), whose
// set_name / get_name Resource already binds. This adds the "name" property
// scripts use; it isn't stored, since resource_name already is.
#define TLR_BIND_NAME() \
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_name", "get_name");

#define TLR_BIND_RECORDS(m_class)                                                              \
	ClassDB::bind_method(D_METHOD("set_music_records", "value"), &m_class::set_music_records); \
	ClassDB::bind_method(D_METHOD("get_music_records"), &m_class::get_music_records);          \
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "music_records", PROPERTY_HINT_ARRAY_TYPE,       \
						 MAKE_RESOURCE_TYPE_HINT("TagLibMusicRecord")),                        \
			"set_music_records", "get_music_records");

// --- TagLibMusicRecord -------------------------------------------------

void TagLibMusicRecord::_bind_methods() {
	TLR_BIND_STR(TagLibMusicRecord, full_path);
	TLR_BIND_STR(TagLibMusicRecord, title);
	TLR_BIND_STR(TagLibMusicRecord, album);
	TLR_BIND_STR(TagLibMusicRecord, artist);
	TLR_BIND_STR(TagLibMusicRecord, album_artist);
	TLR_BIND_STR(TagLibMusicRecord, genre);
	TLR_BIND_STR(TagLibMusicRecord, musicbrainz_album_id);
	TLR_BIND_NUM(TagLibMusicRecord, raw_length, Variant::FLOAT);
	TLR_BIND_NUM(TagLibMusicRecord, track, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, disc, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, year, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, bitrate, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, sample_rate, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, channels, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, file_mtime, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, file_size, Variant::INT);
	TLR_BIND_NUM(TagLibMusicRecord, listen_count, Variant::INT);

	ClassDB::bind_method(D_METHOD("get_length_string"), &TagLibMusicRecord::get_length_string);
	ClassDB::bind_method(D_METHOD("get_title_with_track"), &TagLibMusicRecord::get_title_with_track);
	ClassDB::bind_method(D_METHOD("get_file_type"), &TagLibMusicRecord::get_file_type);
	// Derived, so readable as a property but never serialized.
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "file_type", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
			"", "get_file_type");
}

static String _pad2(int p_value) {
	String s = itos(p_value);
	if (s.length() < 2) {
		s = "0" + s;
	}
	return s;
}

String TagLibMusicRecord::get_length_string() const {
	int total = MAX((int)raw_length, 0);
	return itos(total / 60) + ":" + _pad2(total % 60);
}

String TagLibMusicRecord::get_title_with_track() const {
	if (track <= 0) {
		return title;
	}
	return _pad2(track) + ". " + title;
}

String TagLibMusicRecord::get_file_type() const {
	if (full_path.contains("://")) {
		return "stream";
	}
	return full_path.get_extension().to_lower();
}

// --- TagLibAlbumRecord -------------------------------------------------

void TagLibAlbumRecord::_bind_methods() {
	TLR_BIND_NAME();
	TLR_BIND_STR(TagLibAlbumRecord, album_artist);
	TLR_BIND_STR(TagLibAlbumRecord, asset_id);

	ClassDB::bind_method(D_METHOD("set_artists", "value"), &TagLibAlbumRecord::set_artists);
	ClassDB::bind_method(D_METHOD("get_artists"), &TagLibAlbumRecord::get_artists);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "artists"), "set_artists", "get_artists");

	TLR_BIND_RECORDS(TagLibAlbumRecord);
}

// --- TagLibArtistRecord ------------------------------------------------

void TagLibArtistRecord::_bind_methods() {
	TLR_BIND_NAME();
	TLR_BIND_STR(TagLibArtistRecord, asset_id);

	TLR_BIND_RECORDS(TagLibArtistRecord);
}
