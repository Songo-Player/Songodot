#include "gdtaglib.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/resources/image_texture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include <aifffile.h>
#include <asffile.h>
#include <asfpicture.h>
#include <asftag.h>
#include <attachedpictureframe.h>
#include <audioproperties.h>
#include <fileref.h>
#include <flacfile.h>
#include <flacpicture.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4item.h>
#include <mpegfile.h>
#include <opusfile.h>
#include <tag.h>
#include <textidentificationframe.h>
#include <tfilestream.h>
#include <tiostream.h>
#include <tpropertymap.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <xiphcomment.h>

void GDTagLib::_bind_methods() {
	ClassDB::bind_static_method("GDTagLib", D_METHOD("get_singleton"), &GDTagLib::get_singleton);

	ClassDB::bind_static_method("GDTagLib", D_METHOD("build_music_records", "paths", "progress", "opts"),
			&GDTagLib::build_music_records, DEFVAL(Callable()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("GDTagLib", D_METHOD("refresh_music_library", "previous_records", "current_paths", "opts"),
			&GDTagLib::refresh_music_library, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("GDTagLib", D_METHOD("plan_music_library_update", "previous_records", "current_paths", "opts"),
			&GDTagLib::plan_music_library_update, DEFVAL(Dictionary()));
	ClassDB::bind_static_method("GDTagLib", D_METHOD("scan_directories", "roots", "groups", "progress", "opts"),
			&GDTagLib::scan_directories, DEFVAL(Callable()), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("GDTagLib", D_METHOD("save_music_library", "path", "library"), &GDTagLib::save_music_library);
	ClassDB::bind_static_method("GDTagLib", D_METHOD("load_music_library", "path"), &GDTagLib::load_music_library);
	ClassDB::bind_static_method("GDTagLib", D_METHOD("get_cover", "path"), &GDTagLib::get_cover);
	ClassDB::bind_static_method("GDTagLib", D_METHOD("get_cover_image", "path"), &GDTagLib::get_cover_image);

	ADD_SIGNAL(MethodInfo("library_update_planned",
			PropertyInfo(Variant::INT, "added"),
			PropertyInfo(Variant::INT, "changed"),
			PropertyInfo(Variant::INT, "removed")));
}

GDTagLib *GDTagLib::singleton = nullptr;

GDTagLib::GDTagLib() {
	if (singleton == nullptr) {
		singleton = this;
	}
}

GDTagLib::~GDTagLib() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

GDTagLib *GDTagLib::get_singleton() {
	return singleton;
}

// TagLib::FileName for a globalized Godot path.
#ifdef _WIN32
#define TL_FILE_NAME(m_path) ((const wchar_t *)(m_path).utf16().get_data())
#else
#define TL_FILE_NAME(m_path) ((m_path).utf8().get_data())
#endif

static String _convert_taglib_string(const TagLib::String &s) {
	if (s.isEmpty()) {
		return String();
	}
	std::string utf8_str = s.to8Bit(true);
	return String::utf8(utf8_str.c_str());
}

static String _global_path(const String &p_path) {
	return ProjectSettings::get_singleton()->globalize_path(p_path).simplify_path();
}

// ====================================================================
// Embedded cover art
// ====================================================================

static Ref<Image> _image_from_bytes(const TagLib::ByteVector &p_data, bool p_png) {
	if (p_data.isEmpty()) {
		return Ref<Image>();
	}
	Vector<uint8_t> bytes;
	bytes.resize(p_data.size());
	memcpy(bytes.ptrw(), p_data.data(), p_data.size());

	Ref<Image> image;
	image.instantiate();
	Error err = p_png ? image->load_png_from_buffer(bytes) : image->load_jpg_from_buffer(bytes);
	if (err != OK) {
		return Ref<Image>();
	}
	return image;
}

// PNG and JPEG are the only cover formats decoded; anything else is skipped.
static Ref<Image> _image_from_mime(const TagLib::ByteVector &p_data, const TagLib::String &p_mime) {
	String mime = String::utf8(p_mime.toCString()).to_lower();
	if (mime.contains("png")) {
		return _image_from_bytes(p_data, true);
	}
	if (mime.contains("jpeg") || mime.contains("jpg")) {
		return _image_from_bytes(p_data, false);
	}
	return Ref<Image>();
}

// Front cover if there is one, else the first APIC frame.
static Ref<Image> _cover_from_id3v2(TagLib::ID3v2::Tag *p_tag) {
	if (!p_tag) {
		return Ref<Image>();
	}
	TagLib::ID3v2::AttachedPictureFrame *pic = nullptr;
	for (TagLib::ID3v2::Frame *frame : p_tag->frameList("APIC")) {
		auto *f = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frame);
		if (!f) {
			continue;
		}
		if (f->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
			pic = f;
			break;
		}
		if (!pic) {
			pic = f;
		}
	}
	if (!pic) {
		return Ref<Image>();
	}
	return _image_from_mime(pic->picture(), pic->mimeType());
}

// Front cover if there is one, else the first picture (FLAC and Xiph comments).
static Ref<Image> _cover_from_flac_pictures(const TagLib::List<TagLib::FLAC::Picture *> &p_pictures) {
	TagLib::FLAC::Picture *pic = nullptr;
	for (TagLib::FLAC::Picture *p : p_pictures) {
		if (p->type() == TagLib::FLAC::Picture::FrontCover) {
			pic = p;
			break;
		}
		if (!pic) {
			pic = p;
		}
	}
	if (!pic) {
		return Ref<Image>();
	}
	return _image_from_mime(pic->data(), pic->mimeType());
}

static Ref<Image> _decode_cover_mp3(const String &p_abs_path) {
	TagLib::MPEG::File file(TL_FILE_NAME(p_abs_path), false);
	if (!file.isOpen()) {
		return Ref<Image>();
	}
	return _cover_from_id3v2(file.ID3v2Tag());
}

static Ref<Image> _decode_cover_flac(const String &p_abs_path) {
	TagLib::FLAC::File file(TL_FILE_NAME(p_abs_path), false);
	if (!file.isOpen()) {
		return Ref<Image>();
	}
	return _cover_from_flac_pictures(file.pictureList());
}

template <typename T>
static Ref<Image> _decode_cover_xiph(const String &p_abs_path) {
	T file(TL_FILE_NAME(p_abs_path), false);
	if (!file.isOpen() || !file.tag()) {
		return Ref<Image>();
	}
	return _cover_from_flac_pictures(file.tag()->pictureList());
}

static Ref<Image> _decode_cover_m4a(const String &p_abs_path) {
	TagLib::MP4::File file(TL_FILE_NAME(p_abs_path), false);
	if (!file.isOpen() || !file.tag()) {
		return Ref<Image>();
	}
	const TagLib::MP4::ItemMap &items = file.tag()->itemMap();
	auto it = items.find("covr");
	if (it == items.end() || !it->second.isValid()) {
		return Ref<Image>();
	}
	TagLib::MP4::CoverArtList covers = it->second.toCoverArtList();
	if (covers.isEmpty()) {
		return Ref<Image>();
	}
	const TagLib::MP4::CoverArt &cover = covers.front();
	switch (cover.format()) {
		case TagLib::MP4::CoverArt::JPEG:
			return _image_from_bytes(cover.data(), false);
		case TagLib::MP4::CoverArt::PNG:
			return _image_from_bytes(cover.data(), true);
		default:
			return Ref<Image>();
	}
}

static Ref<Image> _decode_cover_wma(const String &p_abs_path) {
	TagLib::ASF::File file(TL_FILE_NAME(p_abs_path), false);
	if (!file.isOpen() || !file.tag()) {
		return Ref<Image>();
	}
	const TagLib::ASF::AttributeListMap &attrs = file.tag()->attributeListMap();
	auto it = attrs.find("WM/Picture");
	if (it == attrs.end() || it->second.isEmpty()) {
		return Ref<Image>();
	}
	TagLib::ASF::Picture pic;
	bool found = false;
	for (const TagLib::ASF::Attribute &attr : it->second) {
		TagLib::ASF::Picture p = attr.toPicture();
		if (p.type() == TagLib::ASF::Picture::FrontCover) {
			pic = p;
			found = true;
			break;
		}
		if (!found) {
			pic = p;
			found = true;
		}
	}
	if (!found) {
		return Ref<Image>();
	}
	return _image_from_mime(pic.picture(), pic.mimeType());
}

// WAV and AIFF carry covers as an ID3v2 tag; try WAV first, then AIFF.
static Ref<Image> _decode_cover_riff(const String &p_abs_path) {
	{
		TagLib::RIFF::WAV::File wav(TL_FILE_NAME(p_abs_path), false);
		if (wav.isOpen()) {
			return _cover_from_id3v2(wav.ID3v2Tag());
		}
	}
	TagLib::RIFF::AIFF::File aiff(TL_FILE_NAME(p_abs_path), false);
	if (aiff.isOpen()) {
		return _cover_from_id3v2(aiff.tag());
	}
	return Ref<Image>();
}

Ref<Image> GDTagLib::get_cover(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	String abs_path = _global_path(p_path);
	if (ext == "mp3") {
		return _decode_cover_mp3(abs_path);
	}
	if (ext == "flac") {
		return _decode_cover_flac(abs_path);
	}
	if (ext == "ogg") {
		return _decode_cover_xiph<TagLib::Ogg::Vorbis::File>(abs_path);
	}
	if (ext == "opus") {
		return _decode_cover_xiph<TagLib::Ogg::Opus::File>(abs_path);
	}
	if (ext == "m4a" || ext == "m4b" || ext == "mp4") {
		return _decode_cover_m4a(abs_path);
	}
	if (ext == "wma") {
		return _decode_cover_wma(abs_path);
	}
	if (ext == "wav" || ext == "aif" || ext == "aiff") {
		return _decode_cover_riff(abs_path);
	}
	return Ref<Image>();
}

Ref<Texture2D> GDTagLib::get_cover_image(const String &p_path) {
	Ref<Image> img = get_cover(p_path);
	if (img.is_null()) {
		return Ref<Texture2D>();
	}
	return ImageTexture::create_from_image(img);
}

// ====================================================================
// Tag reading
// ====================================================================

// Everything a TagLibMusicRecord needs from one file, as plain data so it can
// be filled on a worker thread and turned into a record on the calling thread
// afterwards. Fields stay empty / 0 when a tag is missing or the file can't
// be opened.
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

static int _disc_from_string(const TagLib::String &p_value) {
	String v = _convert_taglib_string(p_value);
	int slash = v.find("/");
	if (slash != -1) {
		v = v.substr(0, slash);
	}
	return v.to_int();
}

static void _audio_fields(const TagLib::AudioProperties *p_ap, TagFields &r_out) {
	if (!p_ap) {
		return;
	}
	r_out.duration = p_ap->lengthInSeconds();
	r_out.bitrate = p_ap->bitrate();
	r_out.sample_rate = p_ap->sampleRate();
	r_out.channels = p_ap->channels();
}

// Album artist / disc / MusicBrainz album id, read straight from the
// container's own frames instead of via File::properties(), which converts
// the whole tag into a PropertyMap just to look up three keys. Each helper
// mirrors the key mapping properties() would use.
static void _extras_from_id3v2(const TagLib::ID3v2::Tag *p_tag, TagFields &r_out) {
	const TagLib::ID3v2::FrameListMap &frames = p_tag->frameListMap();

	auto tpe2 = frames.find("TPE2");
	if (tpe2 != frames.end() && !tpe2->second.isEmpty()) {
		r_out.album_artist = _convert_taglib_string(tpe2->second.front()->toString());
	}
	auto tpos = frames.find("TPOS");
	if (tpos != frames.end() && !tpos->second.isEmpty()) {
		r_out.disc = _disc_from_string(tpos->second.front()->toString());
	}
	auto txxx = frames.find("TXXX");
	if (txxx != frames.end()) {
		for (TagLib::ID3v2::Frame *frame : txxx->second) {
			auto *user = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame *>(frame);
			if (!user || user->description().upper() != "MUSICBRAINZ ALBUM ID") {
				continue;
			}
			// fieldList() is [description, value...]
			TagLib::StringList values = user->fieldList();
			if (values.size() > 1) {
				values.erase(values.begin());
				r_out.musicbrainz_album_id = _convert_taglib_string(values.toString(" "));
			}
			break;
		}
	}
}

static void _extras_from_xiph(const TagLib::Ogg::XiphComment *p_tag, TagFields &r_out) {
	const TagLib::Ogg::FieldListMap &fields = p_tag->fieldListMap();

	auto aa = fields.find("ALBUMARTIST");
	if (aa != fields.end() && !aa->second.isEmpty()) {
		r_out.album_artist = _convert_taglib_string(aa->second.toString(" "));
	}
	auto dn = fields.find("DISCNUMBER");
	if (dn != fields.end() && !dn->second.isEmpty()) {
		r_out.disc = _disc_from_string(dn->second.toString(" "));
	}
	auto mb = fields.find("MUSICBRAINZ_ALBUMID");
	if (mb != fields.end() && !mb->second.isEmpty()) {
		r_out.musicbrainz_album_id = _convert_taglib_string(mb->second.toString(" "));
	}
}

static void _extras_from_asf(const TagLib::ASF::Tag *p_tag, TagFields &r_out) {
	auto join = [&](const char *p_name) -> TagLib::String {
		TagLib::StringList values;
		for (const TagLib::ASF::Attribute &attr : p_tag->attribute(p_name)) {
			values.append(attr.toString());
		}
		return values.toString(" ");
	};
	r_out.album_artist = _convert_taglib_string(join("WM/AlbumArtist"));
	TagLib::String disc = join("WM/PartOfSet");
	if (!disc.isEmpty()) {
		r_out.disc = _disc_from_string(disc);
	}
	r_out.musicbrainz_album_id = _convert_taglib_string(join("MusicBrainz/Album Id"));
}

// Fallback for containers without a dedicated helper (APE, WavPack, MPC,
// files whose primary tag is APE / RIFF INFO, ...).
static void _extras_from_properties(const TagLib::File *p_file, TagFields &r_out) {
	TagLib::PropertyMap props = p_file->properties();

	auto aa = props.find("ALBUMARTIST");
	if (aa != props.end() && !aa->second.isEmpty()) {
		r_out.album_artist = _convert_taglib_string(aa->second.toString(" "));
	}
	auto dn = props.find("DISCNUMBER");
	if (dn != props.end() && !dn->second.isEmpty()) {
		r_out.disc = _disc_from_string(dn->second.toString(" "));
	}
	auto mb = props.find("MUSICBRAINZ_ALBUMID");
	if (mb != props.end() && !mb->second.isEmpty()) {
		r_out.musicbrainz_album_id = _convert_taglib_string(mb->second.toString(" "));
	}
}

// Picks the same tag File::properties() would have (the first non-empty one
// in the container's TagUnion) and reads the extras from it directly.
static void _read_extra_fields(TagLib::File *p_file, TagLib::Tag *p_tag, TagFields &r_out) {
	if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(p_file)) {
		if (mpeg->hasID3v2Tag() && !mpeg->ID3v2Tag()->isEmpty()) {
			_extras_from_id3v2(mpeg->ID3v2Tag(), r_out);
			return;
		}
	} else if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(p_file)) {
		if (flac->hasXiphComment() && !flac->xiphComment()->isEmpty()) {
			_extras_from_xiph(flac->xiphComment(), r_out);
			return;
		}
	} else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(p_file)) {
		if (wav->hasID3v2Tag() && !wav->ID3v2Tag()->isEmpty()) {
			_extras_from_id3v2(wav->ID3v2Tag(), r_out);
			return;
		}
	} else if (auto *aiff = dynamic_cast<TagLib::RIFF::AIFF::File *>(p_file)) {
		if (aiff->hasID3v2Tag()) {
			_extras_from_id3v2(aiff->tag(), r_out);
		}
		return;
	} else if (auto *xiph = dynamic_cast<TagLib::Ogg::XiphComment *>(p_tag)) {
		// Vorbis / Opus / Speex / Ogg FLAC all expose a bare XiphComment.
		_extras_from_xiph(xiph, r_out);
		return;
	} else if (auto *asf = dynamic_cast<TagLib::ASF::Tag *>(p_tag)) {
		_extras_from_asf(asf, r_out);
		return;
	}
	_extras_from_properties(p_file, r_out);
}

// How a file's bytes are fetched for tag reading. Tag parsers touch a file
// in many small reads clustered at its start (ID3v2, FLAC / MP4 / Ogg
// headers) and end (ID3v1, APE, last Ogg page); on an SD card each of those
// is a separate slow request. Reading both ends up front in two large reads
// turns that into a couple of requests per file.
struct ReadConfig {
	TagLib::AudioProperties::ReadStyle style = TagLib::AudioProperties::Average;
	size_t head_bytes = 64 * 1024;
	size_t tail_bytes = 16 * 1024;
};

// Read-only IOStream serving TagLib from three buffers: the file's head and
// tail (read once at open) and a sliding window refilled on a miss, so a run
// of small reads elsewhere (e.g. MPEG frame headers after a large ID3v2 tag)
// still costs one request. Reads straddling buffers go to the file.
class HeadTailStream : public TagLib::IOStream {
public:
	static const size_t WINDOW_BYTES = 64 * 1024;

	HeadTailStream(TagLib::FileName p_name, const ReadConfig &p_config) :
			file(p_name, true) {
		if (!file.isOpen()) {
			return;
		}
		len = file.length();
		file.seek(0);
		head = file.readBlock((size_t)std::min<TagLib::offset_t>(len, (TagLib::offset_t)p_config.head_bytes));
		if (len > (TagLib::offset_t)head.size()) {
			tail_start = std::max<TagLib::offset_t>((TagLib::offset_t)head.size(),
					len - (TagLib::offset_t)p_config.tail_bytes);
			file.seek(tail_start);
			tail = file.readBlock((size_t)(len - tail_start));
		}
	}

	TagLib::FileName name() const override { return file.name(); }
	bool readOnly() const override { return true; }
	bool isOpen() const override { return file.isOpen(); }
	TagLib::offset_t tell() const override { return pos; }
	TagLib::offset_t length() override { return len; }

	void seek(TagLib::offset_t p_offset, Position p_from) override {
		switch (p_from) {
			case Beginning:
				pos = p_offset;
				break;
			case Current:
				pos += p_offset;
				break;
			case End:
				pos = len + p_offset;
				break;
		}
		pos = std::max<TagLib::offset_t>(pos, 0);
	}

	TagLib::ByteVector readBlock(size_t p_length) override {
		if (!file.isOpen() || pos >= len || p_length == 0) {
			return TagLib::ByteVector();
		}
		size_t n = (size_t)std::min<TagLib::offset_t>((TagLib::offset_t)p_length, len - pos);
		TagLib::ByteVector out;
		if (!_from(head, 0, n, out) && !_from(tail, tail_start, n, out) && !_from(window, window_start, n, out)) {
			file.seek(pos);
			if (n >= WINDOW_BYTES) {
				out = file.readBlock(n);
			} else {
				window_start = pos;
				window = file.readBlock((size_t)std::min<TagLib::offset_t>((TagLib::offset_t)WINDOW_BYTES, len - pos));
				out = window.mid(0, n);
			}
		}
		pos += out.size();
		return out;
	}

	// Never written to; TagLib only calls these when saving.
	void writeBlock(const TagLib::ByteVector &) override {}
	void insert(const TagLib::ByteVector &, TagLib::offset_t, size_t) override {}
	void removeBlock(TagLib::offset_t, size_t) override {}
	void truncate(TagLib::offset_t) override {}

private:
	bool _from(const TagLib::ByteVector &p_buf, TagLib::offset_t p_start, size_t p_n, TagLib::ByteVector &r_out) const {
		if (pos < p_start || pos + (TagLib::offset_t)p_n > p_start + (TagLib::offset_t)p_buf.size()) {
			return false;
		}
		r_out = p_buf.mid((unsigned int)(pos - p_start), (unsigned int)p_n);
		return true;
	}

	TagLib::FileStream file;
	TagLib::offset_t len = 0;
	TagLib::offset_t pos = 0;
	TagLib::ByteVector head;
	TagLib::ByteVector tail;
	TagLib::offset_t tail_start = 0;
	TagLib::ByteVector window;
	TagLib::offset_t window_start = 0;
};

// Single pass over every scalar tag plus audio properties a music library
// needs. p_abs_path must already be globalized (ProjectSettings isn't
// touched here, so this is safe on worker threads) and p_ext lower-cased.
// Leaves r_out untouched for an unopenable file.
static void _read_tag_fields(const String &p_abs_path, const String &p_ext,
		const ReadConfig &p_config, TagFields &r_out) {
	HeadTailStream stream(TL_FILE_NAME(p_abs_path), p_config);
	if (!stream.isOpen()) {
		return;
	}

	if (p_ext == "m4a" || p_ext == "m4b" || p_ext == "mp4") {
		TagLib::MP4::File file(&stream, true, p_config.style);
		if (!file.isOpen() || !file.tag()) {
			return;
		}

		const TagLib::MP4::ItemMap &items = file.tag()->itemMap();
		auto get_str = [&](const char *key) -> String {
			auto it = items.find(key);
			if (it == items.end() || !it->second.isValid()) {
				return String();
			}
			return _convert_taglib_string(it->second.toStringList().toString(" "));
		};
		auto get_pair_first = [&](const char *key) -> int {
			auto it = items.find(key);
			if (it == items.end() || !it->second.isValid()) {
				return 0;
			}
			return it->second.toIntPair().first;
		};

		r_out.title = get_str("\xA9"
							  "nam");
		r_out.artist = get_str("\xA9"
							   "ART");
		r_out.album = get_str("\xA9"
							  "alb");
		r_out.genre = get_str("\xA9"
							  "gen");
		r_out.album_artist = get_str("aART");
		r_out.musicbrainz_album_id = get_str("----:com.apple.iTunes:MusicBrainz Album Id");

		String day = get_str("\xA9"
							 "day");
		if (!day.is_empty()) {
			r_out.year = day.to_int();
		}
		r_out.track = get_pair_first("trkn");
		r_out.disc = get_pair_first("disk");

		_audio_fields(file.audioProperties(), r_out);
		return;
	}

	TagLib::FileRef file(&stream, true, p_config.style);
	if (file.isNull() || !file.tag()) {
		return;
	}

	TagLib::Tag *tag = file.tag();
	r_out.title = _convert_taglib_string(tag->title());
	r_out.artist = _convert_taglib_string(tag->artist());
	r_out.album = _convert_taglib_string(tag->album());
	r_out.genre = _convert_taglib_string(tag->genre());
	r_out.year = (int)tag->year();
	r_out.track = (int)tag->track();

	if (file.file()) {
		_read_extra_fields(file.file(), tag, r_out);
	}
	_audio_fields(file.audioProperties(), r_out);
}

// Modification time (seconds) and size (bytes) of a globalized path from a
// single stat(), without opening the file. Leaves 0 / -1 ("unknown", as the
// record fields use) when the file can't be stat'ed.
static void _file_stamp(const String &p_global_path, int64_t &r_mtime, int64_t &r_size) {
	r_mtime = 0;
	r_size = -1;
#ifdef _WIN32
	struct _stat64 st;
	if (_wstat64((const wchar_t *)p_global_path.utf16().get_data(), &st) != 0) {
		return;
	}
#else
	struct stat st;
	if (stat(p_global_path.utf8().get_data(), &st) != 0) {
		return;
	}
#endif
	r_mtime = (int64_t)st.st_mtime;
	r_size = (int64_t)st.st_size;
}

// Tags + filesystem stamps for one already-globalized path. Thread safe.
static void _read_file_fields(const String &p_abs_path, const String &p_ext,
		const ReadConfig &p_config, TagFields &r_out) {
	_read_tag_fields(p_abs_path.simplify_path(), p_ext, p_config, r_out);
	_file_stamp(p_abs_path, r_out.file_mtime, r_out.file_size);
}

static Ref<TagLibMusicRecord> _record_from_fields(const String &p_path, const TagFields &p_fields) {
	Ref<TagLibMusicRecord> rec;
	rec.instantiate();
	rec->set_full_path(p_path);

	// Fallback for an untagged (or unparseable) file: the file name without
	// its extension, e.g. "04 - Weightless.flac" -> "04 - Weightless".
	String stem = p_path.get_file().get_basename();

	rec->set_title(p_fields.title.strip_edges().is_empty() ? stem : p_fields.title);
	rec->set_artist(p_fields.artist.strip_edges().is_empty() ? String("Unknown Artist") : p_fields.artist);
	rec->set_album(p_fields.album.strip_edges().is_empty() ? String("Unknown Album") : p_fields.album);

	// Left empty when the file has no album-artist tag; _assemble_library
	// infers a value per album.
	rec->set_album_artist(p_fields.album_artist.strip_edges());
	rec->set_genre(p_fields.genre.strip_edges());
	rec->set_musicbrainz_album_id(p_fields.musicbrainz_album_id.strip_edges());

	rec->set_raw_length((double)p_fields.duration);
	rec->set_track(p_fields.track);
	rec->set_disc(p_fields.disc);
	rec->set_year(p_fields.year);
	rec->set_bitrate(p_fields.bitrate);
	rec->set_sample_rate(p_fields.sample_rate);
	rec->set_channels(p_fields.channels);

	rec->set_file_mtime(p_fields.file_mtime);
	rec->set_file_size(p_fields.file_size);

	return rec;
}

// One file, read on the calling thread (refresh_music_library).
static Ref<TagLibMusicRecord> _build_music_record(const String &p_path) {
	TagFields fields;
	_read_file_fields(ProjectSettings::get_singleton()->globalize_path(p_path),
			p_path.get_extension().to_lower(), ReadConfig(), fields);
	return _record_from_fields(p_path, fields);
}

#ifndef _WIN32
// Pulls a file's head and tail (the regions HeadTailStream will read) into the
// page cache with plain blocking reads, so the prefetch thread keeps exactly
// one request in flight, which is all an SD card can serve anyway.
static void _prefetch_file(const std::string &p_fs_path, const ReadConfig &p_config, std::vector<char> &r_scratch) {
	int fd = open(p_fs_path.c_str(), O_RDONLY);
	if (fd < 0) {
		return;
	}
	struct stat st;
	if (fstat(fd, &st) == 0) {
		off_t len = st.st_size;
		size_t head = (size_t)std::min<off_t>(len, (off_t)p_config.head_bytes);
		if (head > 0 && pread(fd, r_scratch.data(), head, 0) < 0) {
			head = 0;
		}
		if (len > (off_t)head) {
			off_t tail_start = std::max<off_t>((off_t)head, len - (off_t)p_config.tail_bytes);
			if (pread(fd, r_scratch.data(), (size_t)(len - tail_start), tail_start) < 0) {
				// Best effort; the worker reads it for real either way.
			}
		}
	}
	close(fd);
}
#endif

TypedArray<TagLibMusicRecord> GDTagLib::build_music_records(
		const PackedStringArray &p_paths,
		const Callable &p_progress,
		const Dictionary &p_opts) {
	TypedArray<TagLibMusicRecord> out;
	const int count = p_paths.size();
	if (count == 0) {
		return out;
	}

	int threads = (int)p_opts.get("threads", 0);
	if (threads <= 0) {
		// Tag reads are mostly small seeks; past ~8 workers they just contend
		// for the disk, so cap the automatic count there.
		threads = std::min((int)std::thread::hardware_concurrency(), 8);
		if (threads <= 0) {
			threads = 4;
		}
	}
	threads = std::min(threads, count);
	ReadConfig config;
	if ((bool)p_opts.get("fast_properties", false)) {
		config.style = TagLib::AudioProperties::Fast;
	}
	config.head_bytes = (size_t)std::max(0, (int)p_opts.get("head_kb", 64)) * 1024;
	config.tail_bytes = (size_t)std::max(0, (int)p_opts.get("tail_kb", 16)) * 1024;
	const int prefetch_ahead = std::max(0, (int)p_opts.get("prefetch_ahead", 8));
	const int interval_ms = std::max(1, (int)p_opts.get("progress_interval_ms", 100));

	// ProjectSettings is only touched here, on the calling thread; the
	// workers below see nothing but plain absolute paths.
	ProjectSettings *ps = ProjectSettings::get_singleton();
	std::vector<String> abs_paths(count);
	std::vector<String> dirs(count);
	std::vector<String> exts(count);
	for (int i = 0; i < count; i++) {
		abs_paths[i] = ps->globalize_path(p_paths[i]);
		dirs[i] = abs_paths[i].get_base_dir();
		exts[i] = p_paths[i].get_extension().to_lower();
	}

	// Read folder by folder (results still come back in p_paths order) so an
	// album's directory entries and neighbouring files stay hot in cache.
	std::vector<int> order(count);
	for (int i = 0; i < count; i++) {
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		if (dirs[a] != dirs[b]) {
			return dirs[a] < dirs[b];
		}
		return abs_paths[a] < abs_paths[b];
	});

	std::vector<TagFields> fields(count);
	std::atomic<int> next{ 0 }; // next slot in order to hand to a worker
	std::atomic<int> done{ 0 };
	std::atomic<bool> cancel{ false };
	std::mutex wake_mutex;
	std::condition_variable wake; // prefetcher waits on workers moving on

	auto worker = [&]() {
		while (!cancel.load(std::memory_order_relaxed)) {
			int slot = next.fetch_add(1, std::memory_order_relaxed);
			if (slot >= count) {
				break;
			}
			wake.notify_one();
			int i = order[slot];
			_read_file_fields(abs_paths[i], exts[i], config, fields[i]);
			done.fetch_add(1, std::memory_order_release);
		}
		wake.notify_one();
	};

	std::vector<std::thread> pool;
	pool.reserve(threads);
	for (int t = 0; t < threads; t++) {
		pool.emplace_back(worker);
	}

#ifndef _WIN32
	// One I/O thread keeps the next prefetch_ahead files' head and tail in the
	// page cache, so the device stays busy while workers parse and their reads
	// mostly hit memory. It never touches a file a worker has already taken.
	std::thread prefetcher;
	if (prefetch_ahead > 0) {
		std::vector<std::string> fs_paths(count);
		for (int i = 0; i < count; i++) {
			fs_paths[i] = abs_paths[i].simplify_path().utf8().get_data();
		}
		prefetcher = std::thread([&, fs_paths = std::move(fs_paths)]() {
			std::vector<char> scratch(std::max(config.head_bytes, config.tail_bytes));
			for (int slot = 0; slot < count; slot++) {
				{
					std::unique_lock<std::mutex> lock(wake_mutex);
					// Timed so a notify racing the predicate check can't stall us.
					while (!cancel.load() && slot >= next.load() + prefetch_ahead) {
						wake.wait_for(lock, std::chrono::milliseconds(20));
					}
				}
				if (cancel.load()) {
					break;
				}
				if (slot < next.load()) {
					continue; // a worker already has it
				}
				_prefetch_file(fs_paths[order[slot]], config, scratch);
			}
		});
	}
#endif

	// The calling thread only reports progress (throttled to interval_ms) so
	// the callable always runs on the thread that called us.
	int reported = 0;
	auto last_report = std::chrono::steady_clock::now();
	while (done.load(std::memory_order_acquire) < count && !cancel.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(std::min(interval_ms, 10)));
		if (!p_progress.is_valid()) {
			continue;
		}
		auto now = std::chrono::steady_clock::now();
		int current = done.load(std::memory_order_acquire);
		if (current == reported || now - last_report < std::chrono::milliseconds(interval_ms)) {
			continue;
		}
		reported = current;
		last_report = now;
		Variant ret = p_progress.call(current);
		if (ret.get_type() == Variant::BOOL && (bool)ret) {
			cancel.store(true);
		}
	}
	for (std::thread &t : pool) {
		t.join();
	}
#ifndef _WIN32
	if (prefetcher.joinable()) {
		wake.notify_all();
		prefetcher.join();
	}
#endif

	if (cancel.load()) {
		return out;
	}
	if (p_progress.is_valid() && reported != count) {
		p_progress.call(count);
	}

	out.resize(count);
	for (int i = 0; i < count; i++) {
		out.set(i, _record_from_fields(p_paths[i], fields[i]));
	}
	return out;
}

// ====================================================================
// Library assembly (albums + album artists)
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
// scheme Songo-5's *_record.gd build_asset() used.
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

// Key with the highest count; ties resolve to the first-inserted key
// (Dictionaries preserve insertion order).
static String _highest_count_key(const Dictionary &p_counts) {
	Array keys = p_counts.keys();
	String best;
	int best_count = -1;
	for (int i = 0; i < keys.size(); i++) {
		int c = (int)(int64_t)p_counts[keys[i]];
		if (c > best_count) {
			best_count = c;
			best = keys[i];
		}
	}
	return best;
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

// Stable identity for the album a record belongs to, chosen so every track of
// one real album keys the same:
//   - all "Unknown Album" tracks collapse into a single shared bucket;
//   - a MusicBrainz Album ID, when tagged, keys the album exactly;
//   - otherwise the normalized album name alone -- deliberately NOT the
//     per-track artist, which varies within an album (features, split credits)
//     and was splitting real albums apart. Two unrelated same-named albums
//     without an MBID will merge and resolve to "Various Artists".
static String _album_key(const Ref<TagLibMusicRecord> &p_rec) {
	String album_norm = _normalize_key(p_rec->get_album());
	if (album_norm.is_empty() || album_norm == "unknown album") {
		return "\x1funknown-album";
	}
	String mbid = p_rec->get_musicbrainz_album_id().strip_edges();
	if (!mbid.is_empty()) {
		return "mbid\x1f" + mbid;
	}
	return "name\x1f" + album_norm;
}

template <typename T>
static TypedArray<T> _to_typed_array(const std::vector<Ref<T>> &p_items) {
	TypedArray<T> out;
	out.resize(p_items.size());
	for (size_t i = 0; i < p_items.size(); i++) {
		out.set((int)i, p_items[i]);
	}
	return out;
}

static bool _title_less(const Ref<TagLibMusicRecord> &x, const Ref<TagLibMusicRecord> &y) {
	return x->get_title().nocasecmp_to(y->get_title()) < 0;
}

// Recompute albums + (album) artists from a full set of records and package
// the three lists into a Dictionary, always from the whole library, never
// merged.
static Dictionary _assemble_library(const std::vector<Ref<TagLibMusicRecord>> &p_records) {
	// --- group records by album identity (first-seen order) ----------
	std::vector<String> album_keys;
	std::vector<String> album_names; // human display name, from the first record
	std::vector<std::vector<Ref<TagLibMusicRecord>>> album_groups;
	Dictionary album_index;
	for (const Ref<TagLibMusicRecord> &rec : p_records) {
		String key = _album_key(rec);
		int idx;
		if (album_index.has(key)) {
			idx = (int)(int64_t)album_index[key];
		} else {
			idx = (int)album_keys.size();
			album_index[key] = idx;
			album_keys.push_back(key);
			album_names.push_back(rec->get_album());
			album_groups.push_back(std::vector<Ref<TagLibMusicRecord>>());
		}
		album_groups[idx].push_back(rec);
	}

	// asset_id stays the readable sanitized name while that name is unique;
	// genuinely distinct same-named albums fall back to a key-derived suffix
	// so their cover-art files never collide. Order-independent.
	Dictionary album_base_counts;
	std::vector<String> album_bases;
	for (const String &album_name : album_names) {
		String base = _asset_id_from_name(album_name);
		album_bases.push_back(base);
		album_base_counts[base] = (int)(int64_t)album_base_counts.get(base, 0) + 1;
	}

	// --- build album + (album) artist records -----------------------
	std::vector<Ref<TagLibAlbumRecord>> albums;
	std::vector<Ref<TagLibArtistRecord>> artists;
	std::vector<std::vector<Ref<TagLibMusicRecord>>> artist_groups;
	Dictionary artist_index;

	for (int i = 0; i < (int)album_keys.size(); i++) {
		std::vector<Ref<TagLibMusicRecord>> &group = album_groups[i];

		Ref<TagLibAlbumRecord> album;
		album.instantiate();
		album->set_name(album_names[i]);

		PackedStringArray distinct_artists; // every distinct per-track artist, first seen
		Dictionary explicit_album_artists; // canonical album-artist tag -> record count
		Dictionary artist_record_counts; // canonical track artist -> # records crediting them

		for (const Ref<TagLibMusicRecord> &rec : group) {
			// Split the artist tag on commas, the way the app historically did,
			// so a track credited "A, B" counts toward both A and B.
			Vector<String> parts = rec->get_artist().split(",", false);
			Dictionary seen_here; // count each artist at most once per record
			for (const String &part : parts) {
				String piece = _canonical_various_artists(part); // trims + folds VA spellings
				if (piece.is_empty() || piece == "Unknown Artist" || seen_here.has(piece)) {
					continue;
				}
				seen_here[piece] = true;
				if (!distinct_artists.has(piece)) {
					distinct_artists.push_back(piece);
				}
				artist_record_counts[piece] = (int)(int64_t)artist_record_counts.get(piece, 0) + 1;
			}

			String aa = _canonical_various_artists(rec->get_album_artist());
			if (!aa.is_empty()) {
				explicit_album_artists[aa] = (int)(int64_t)explicit_album_artists.get(aa, 0) + 1;
			}
		}
		album->set_artists(distinct_artists);

		// Resolve the album artist:
		//  1. an explicit album-artist tag wins -- the most common one if they differ;
		//  2. otherwise, only when a single artist is credited on a MAJORITY of the
		//     album's tracks does that artist become the album artist;
		//  3. otherwise it's treated as a compilation -> "Various Artists".
		String resolved;
		if (!explicit_album_artists.is_empty()) {
			resolved = _highest_count_key(explicit_album_artists);
		} else {
			String top = _highest_count_key(artist_record_counts);
			int top_count = top.is_empty() ? 0 : (int)(int64_t)artist_record_counts[top];
			if (top_count > 0 && top_count * 2 > (int)group.size()) {
				resolved = top;
			}
		}
		if (resolved.is_empty()) {
			resolved = "Various Artists";
		}
		album->set_album_artist(resolved);

		String album_asset_id = album_bases[i];
		if ((int)(int64_t)album_base_counts[album_bases[i]] > 1) {
			album_asset_id += "_" + itos((int64_t)album_keys[i].hash());
		}
		album->set_asset_id(album_asset_id);

		// tracks ordered for album playback: disc, then track, then title
		std::sort(group.begin(), group.end(),
				[](const Ref<TagLibMusicRecord> &x, const Ref<TagLibMusicRecord> &y) {
					if (x->get_disc() != y->get_disc()) {
						return x->get_disc() < y->get_disc();
					}
					if (x->get_track() != y->get_track()) {
						return x->get_track() < y->get_track();
					}
					return _title_less(x, y);
				});
		album->set_music_records(_to_typed_array(group));
		albums.push_back(album);

		// The artists directory is album-artist driven: tracks on a
		// "Various Artists" album are not filed under any single artist.
		if (resolved != "Various Artists") {
			int artist_idx;
			if (artist_index.has(resolved)) {
				artist_idx = (int)(int64_t)artist_index[resolved];
			} else {
				Ref<TagLibArtistRecord> artist;
				artist.instantiate();
				artist->set_name(resolved);
				artist_idx = (int)artists.size();
				artist_index[resolved] = artist_idx;
				artists.push_back(artist);
				artist_groups.push_back(std::vector<Ref<TagLibMusicRecord>>());
			}
			std::vector<Ref<TagLibMusicRecord>> &artist_group = artist_groups[artist_idx];
			artist_group.insert(artist_group.end(), group.begin(), group.end());
		}
	}

	// artist asset_id: same readable-unless-colliding scheme as albums.
	// An artist's tracks span albums, so they're listed by title.
	Dictionary artist_base_counts;
	for (const Ref<TagLibArtistRecord> &artist : artists) {
		String base = _asset_id_from_name(artist->get_name());
		artist_base_counts[base] = (int)(int64_t)artist_base_counts.get(base, 0) + 1;
	}
	for (int i = 0; i < (int)artists.size(); i++) {
		const Ref<TagLibArtistRecord> &artist = artists[i];
		String base = _asset_id_from_name(artist->get_name());
		String id = base;
		if ((int)(int64_t)artist_base_counts[base] > 1) {
			id += "_" + itos((int64_t)artist->get_name().hash());
		}
		artist->set_asset_id(id);

		std::sort(artist_groups[i].begin(), artist_groups[i].end(), _title_less);
		artist->set_music_records(_to_typed_array(artist_groups[i]));
	}

	// --- order the top level lists the way Songo-5 presents them -----
	std::vector<Ref<TagLibMusicRecord>> records_sorted = p_records;
	std::sort(records_sorted.begin(), records_sorted.end(), _title_less);

	std::sort(albums.begin(), albums.end(),
			[](const Ref<TagLibAlbumRecord> &x, const Ref<TagLibAlbumRecord> &y) {
				bool xu = x->get_name() == "Unknown Album";
				bool yu = y->get_name() == "Unknown Album";
				if (xu != yu) {
					return yu; // "Unknown Album" sinks to the bottom
				}
				return x->get_name().nocasecmp_to(y->get_name()) < 0;
			});

	std::sort(artists.begin(), artists.end(),
			[](const Ref<TagLibArtistRecord> &x, const Ref<TagLibArtistRecord> &y) {
				return x->get_name().nocasecmp_to(y->get_name()) < 0;
			});

	Dictionary out;
	out["music_records"] = _to_typed_array(records_sorted);
	out["albums"] = _to_typed_array(albums);
	out["artists"] = _to_typed_array(artists);
	return out;
}

// ====================================================================
// Incremental updates
// ====================================================================

// True when the file at p_path no longer matches the record we hold for it,
// using only cheap filesystem stamps (no tag parsing). Shared by
// refresh_music_library and plan_music_library_update so both agree.
// p_known_mtime / p_known_size are stamps the caller already has (from
// scan_directories); a size of -1 means unknown, so the file is stat()ed.
static bool _record_needs_reread(const Ref<TagLibMusicRecord> &p_old, const String &p_path,
		bool p_force, bool p_follow, int64_t p_known_mtime = 0, int64_t p_known_size = -1) {
	if (p_force) {
		return true;
	}
	if (!p_follow) {
		return false;
	}
	int64_t mtime = p_known_mtime;
	int64_t size = p_known_size;
	if (size < 0) {
		String g = ProjectSettings::get_singleton()->globalize_path(p_path);
		_file_stamp(g, mtime, size);
	}
	bool mtime_changed = p_old->get_file_mtime() != 0 && mtime != 0 &&
			p_old->get_file_mtime() != mtime;
	bool size_changed = p_old->get_file_size() >= 0 && size >= 0 &&
			p_old->get_file_size() != size;
	return mtime_changed || size_changed;
}

static Dictionary _index_records_by_path(const Array &p_records) {
	Dictionary by_path;
	for (int i = 0; i < p_records.size(); i++) {
		Ref<TagLibMusicRecord> r = p_records[i];
		if (r.is_valid() && !r->get_full_path().is_empty()) {
			by_path[r->get_full_path()] = r;
		}
	}
	return by_path;
}

static PackedStringArray _removed_paths(const Dictionary &p_prev_by_path, const Dictionary &p_seen) {
	PackedStringArray removed;
	Array prev_paths = p_prev_by_path.keys();
	for (int i = 0; i < prev_paths.size(); i++) {
		String pp = prev_paths[i];
		if (!p_seen.has(pp)) {
			removed.push_back(pp);
		}
	}
	return removed;
}

Dictionary GDTagLib::plan_music_library_update(
		const Array &p_previous_records,
		const PackedStringArray &p_current_paths,
		const Dictionary &p_opts) {
	bool force_reread = (bool)p_opts.get("force_reread", false);
	bool follow_retags = (bool)p_opts.get("follow_retags", true);

	// Stamps from scan_directories, only trusted when they line up 1:1.
	PackedInt64Array known_mtimes = p_opts.get("mtimes", PackedInt64Array());
	PackedInt64Array known_sizes = p_opts.get("sizes", PackedInt64Array());
	bool have_stamps = known_mtimes.size() == p_current_paths.size() &&
			known_sizes.size() == p_current_paths.size();

	Dictionary prev_by_path = _index_records_by_path(p_previous_records);

	Dictionary seen;
	PackedStringArray added;
	PackedStringArray changed;

	for (int i = 0; i < p_current_paths.size(); i++) {
		const String &path = p_current_paths[i];
		if (path.is_empty() || seen.has(path)) {
			continue;
		}
		seen[path] = true;
		if (!prev_by_path.has(path)) {
			added.push_back(path);
			continue;
		}
		Ref<TagLibMusicRecord> old = prev_by_path[path];
		bool reread = have_stamps
				? _record_needs_reread(old, path, force_reread, follow_retags, known_mtimes[i], known_sizes[i])
				: _record_needs_reread(old, path, force_reread, follow_retags);
		if (reread) {
			changed.push_back(path);
		}
	}

	PackedStringArray removed = _removed_paths(prev_by_path, seen);

	Dictionary out;
	out["added"] = added;
	out["changed"] = changed;
	out["removed"] = removed;

	GDTagLib *s = get_singleton();
	if (s) {
		// Deferred so a caller on a worker thread still delivers on the main one.
		s->call_deferred(SNAME("emit_signal"), SNAME("library_update_planned"),
				added.size(), changed.size(), removed.size());
	}
	return out;
}

Dictionary GDTagLib::refresh_music_library(
		const Array &p_previous_records,
		const PackedStringArray &p_current_paths,
		const Dictionary &p_opts) {
	bool force_reread = (bool)p_opts.get("force_reread", false);
	bool follow_retags = (bool)p_opts.get("follow_retags", true);

	Dictionary prev_by_path = _index_records_by_path(p_previous_records);

	Dictionary seen;
	PackedStringArray added;
	PackedStringArray rescanned;
	std::vector<Ref<TagLibMusicRecord>> records;

	for (int i = 0; i < p_current_paths.size(); i++) {
		const String &path = p_current_paths[i];
		if (path.is_empty() || seen.has(path)) {
			continue;
		}
		seen[path] = true;

		if (!prev_by_path.has(path)) {
			records.push_back(_build_music_record(path));
			added.push_back(path);
			continue;
		}

		Ref<TagLibMusicRecord> old = prev_by_path[path];
		if (_record_needs_reread(old, path, force_reread, follow_retags)) {
			Ref<TagLibMusicRecord> fresh = _build_music_record(path);
			// Play counts aren't in the tags, so a re-read must not reset them.
			fresh->set_listen_count(old->get_listen_count());
			records.push_back(fresh);
			rescanned.push_back(path);
		} else {
			records.push_back(old);
		}
	}

	Dictionary out = _assemble_library(records);
	out["added"] = added;
	out["removed"] = _removed_paths(prev_by_path, seen);
	out["rescanned"] = rescanned;
	return out;
}
