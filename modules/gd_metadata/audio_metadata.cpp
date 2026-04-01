
#include "audio_metadata.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/io/image.h"
#include "scene/resources/image_texture.h"

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
#include <taglib/mpeg/id3v1/id3v1tag.h>
#include <taglib/flac/flacfile.h>
#include <taglib/ogg/vorbis/vorbisfile.h>
#include <taglib/audioproperties.h>
#include <taglib/mpeg/id3v2/frames/attachedpictureframe.h>
#include <taglib/flac/flacfile.h>
#include <taglib/flac/flacpicture.h>
#include <taglib/ogg/xiphcomment.h>
#include <taglib/mp4/mp4file.h>
#include <taglib/mp4/mp4item.h>

#include <taglib/asf/asffile.h>
#include <taglib/asf/asftag.h>
#include <taglib/asf/asfpicture.h>

#include <taglib/riff/rifffile.h>
#include <taglib/riff/wav/wavfile.h>
#include <taglib/riff/aiff/aifffile.h>


void AudioMetadata::_bind_methods() {
	ClassDB::bind_static_method(
		"AudioMetadata",
		D_METHOD("get_cover_image", "path"),
		&AudioMetadata::get_cover_image
	);
	ClassDB::bind_static_method(
        "AudioMetadata",
        D_METHOD("get_mp3_image", "path"),
        &AudioMetadata::get_mp3_image
    );
	ClassDB::bind_static_method(
        "AudioMetadata",
        D_METHOD("get_wma_image", "path"),
        &AudioMetadata::get_wma_image
    );
	ClassDB::bind_static_method(
        "AudioMetadata",
        D_METHOD("get_riff_image", "path"),
        &AudioMetadata::get_riff_image
    );
	ClassDB::bind_static_method(
		"AudioMetadata",
		D_METHOD("get_flac_image", "path"),
		&AudioMetadata::get_flac_image
	);
	ClassDB::bind_static_method(
		"AudioMetadata",
		D_METHOD("get_ogg_image", "path"),
		&AudioMetadata::get_ogg_image
	);
	ClassDB::bind_static_method(
		"AudioMetadata",
		D_METHOD("get_m4a_image", "path"),
		&AudioMetadata::get_m4a_image
	);
	ClassDB::bind_method(D_METHOD("read_m4a", "path", "attrs"), &AudioMetadata::read_m4a);
	ClassDB::bind_method(D_METHOD("read_universal", "path", "attrs"), &AudioMetadata::read_universal);
	ClassDB::bind_method(D_METHOD("read_audio", "path", "attrs"), &AudioMetadata::read_audio);
}

static String _convert_taglib_string(const TagLib::String &s) {
    if (s.isEmpty()) return "";
    std::string utf8_str = s.to8Bit(true);
    return String::utf8(utf8_str.c_str());
}


Dictionary AudioMetadata::read_m4a(const String &p_path, const Array &attrs) {
	Dictionary result;
	String abs_path = ProjectSettings::get_singleton()->globalize_path(p_path).simplify_path();

#ifdef _WIN32
	TagLib::MP4::File file((const wchar_t *)abs_path.utf16().get_data());
#else
	TagLib::MP4::File file(abs_path.utf8().get_data());
#endif

	if (!file.isOpen()) {
		return result;
	}

	TagLib::MP4::Tag *tag = file.tag();
	if (!tag) {
		return result;
	}

	const TagLib::MP4::ItemMap &items = tag->itemMap();

	auto get_item_string = [&](const char *key) -> String {
		auto it = items.find(key);
		if (it == items.end()) return "";
		if (!it->second.isValid()) return "";
		return _convert_taglib_string(it->second.toStringList().toString(" "));
	};

	if (attrs.has("title")) {
		String v = get_item_string("\xA9""nam");
		if (!v.is_empty()) result["title"] = v;
	}

	if (attrs.has("artist")) {
		String v = get_item_string("\xA9""ART");
		if (!v.is_empty()) result["artist"] = v;
	}

	if (attrs.has("album")) {
		String v = get_item_string("\xA9""alb");
		if (!v.is_empty()) result["album"] = v;
	}

	if (attrs.has("genre")) {
		String v = get_item_string("\xA9""gen");
		if (!v.is_empty()) result["genre"] = v;
	}

	if (attrs.has("year")) {
		String v = get_item_string("\xA9""day");
		if (!v.is_empty()) result["year"] = v.to_int();
	}

	if (attrs.has("track")) {
		auto it = items.find("trkn");
		if (it != items.end() && it->second.isValid()) {
			auto track_pair = it->second.toIntPair();
			if (track_pair.first != 0) {
				result["track"] = track_pair.first;
			}
		}
	}

	if (attrs.has("duration") && file.audioProperties()) {
		result["duration"] = file.audioProperties()->lengthInSeconds();
	}

	return result;
}

Dictionary AudioMetadata::read_universal(const String &p_path, const Array &attrs) {
	Dictionary result;

	String abs_path = ProjectSettings::get_singleton()->globalize_path(p_path).simplify_path();

#ifdef _WIN32
	TagLib::FileRef file((const wchar_t *)abs_path.utf16().get_data());
#else
	TagLib::FileRef file(abs_path.utf8().get_data());
#endif

	if (file.isNull() || !file.tag()) {
		return result;
	}

	TagLib::Tag *tag = file.tag();

	if (attrs.has("title"))  result["title"]  = _convert_taglib_string(tag->title());
	if (attrs.has("artist")) result["artist"] = _convert_taglib_string(tag->artist());
	if (attrs.has("album"))  result["album"]  = _convert_taglib_string(tag->album());
	if (attrs.has("genre"))  result["genre"]  = _convert_taglib_string(tag->genre());

	if (attrs.has("year") && tag->year() != 0)
		result["year"] = tag->year();

	if (attrs.has("track") && tag->track() != 0)
		result["track"] = tag->track();

	if (attrs.has("duration") && file.audioProperties()) {
		result["duration"] = file.audioProperties()->lengthInSeconds();
	}

	return result;
}


Dictionary AudioMetadata::read_audio(const String &path, const Array &attrs) {
	String ext = path.get_extension().to_lower();

	if (ext == "m4a" || ext == "m4b" || ext == "mp4") {
		return read_m4a(path, attrs); // your MP4-specific function
	}

	return read_universal(path, attrs);
}


Ref<Texture2D> AudioMetadata::get_cover_image(const String &path) {
	String ext = path.get_extension().to_lower();

	if (ext == "mp3") return get_mp3_image(path);
	if (ext == "flac") return get_flac_image(path);
	if (ext == "ogg" || ext == "opus") return get_ogg_image(path);
	if (ext == "m4a" || ext == "m4b" || ext == "mp4") return get_m4a_image(path);
	if (ext == "wma") return get_wma_image(path); // optional
	if (ext == "wav" || ext == "aif" || ext == "aiff") return get_riff_image(path); // optional

	return Ref<Texture2D>();
}


Ref<Texture2D> AudioMetadata::get_mp3_image(const String &p_path) {
    String abs_path = ProjectSettings::get_singleton()
                          ->globalize_path(p_path)
                          .simplify_path();

#ifdef _WIN32
    TagLib::MPEG::File file((const wchar_t *)abs_path.utf16().get_data());
#else
    TagLib::MPEG::File file(abs_path.utf8().get_data());
#endif

    if (!file.isOpen()) {
        return Ref<Texture2D>();
    }

    TagLib::ID3v2::Tag *id3v2 = file.ID3v2Tag();
    if (!id3v2) {
        return Ref<Texture2D>();
    }

    TagLib::ID3v2::FrameList frames =
        id3v2->frameListMap()["APIC"];

    if (frames.isEmpty()) {
        return Ref<Texture2D>();
    }

    // Prefer "Front Cover" if available
    TagLib::ID3v2::AttachedPictureFrame *pic = nullptr;

    for (auto it = frames.begin(); it != frames.end(); ++it) {
        auto *f = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(*it);
        if (!f) continue;

        if (f->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
            pic = f;
            break;
        }

        if (!pic) {
            pic = f; // fallback to first image
        }
    }

    if (!pic) {
        return Ref<Texture2D>();
    }

    const TagLib::ByteVector &data = pic->picture();
    if (data.isEmpty()) {
        return Ref<Texture2D>();
    }

	PackedByteArray bytes;
	bytes.resize(data.size());
	memcpy(bytes.ptrw(), data.data(), data.size());

	Ref<Image> image;
	image.instantiate();

	String mime = String::utf8(pic->mimeType().toCString()).to_lower();

	Error err = ERR_UNAVAILABLE;

	if (mime.find("png") != -1) {
		err = image->load_png_from_buffer(bytes);
	} else if (mime.find("jpeg") != -1 || mime.find("jpg") != -1) {
		err = image->load_jpg_from_buffer(bytes);
	} else {
		// Unsupported or rare format (webp, bmp, etc.)
		return Ref<Texture2D>();
	}

	if (err != OK) {
		return Ref<Texture2D>();
	}

	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
	return texture;
}

Ref<Texture2D> AudioMetadata::get_flac_image(const String &p_path) {
    String abs_path = ProjectSettings::get_singleton()
                          ->globalize_path(p_path)
                          .simplify_path();

#ifdef _WIN32
    TagLib::FLAC::File file((const wchar_t *)abs_path.utf16().get_data());
#else
    TagLib::FLAC::File file(abs_path.utf8().get_data());
#endif

    if (!file.isOpen()) {
        return Ref<Texture2D>();
    }

    const TagLib::List<TagLib::FLAC::Picture *> &pictures = file.pictureList();
    if (pictures.isEmpty()) {
        return Ref<Texture2D>();
    }

    // Prefer Front Cover if present
    TagLib::FLAC::Picture *pic = nullptr;
    for (auto it = pictures.begin(); it != pictures.end(); ++it) {
        if ((*it)->type() == TagLib::FLAC::Picture::FrontCover) {
            pic = *it;
            break;
        }
        if (!pic) {
            pic = *it; // fallback
        }
    }

    if (!pic) {
        return Ref<Texture2D>();
    }

    const TagLib::ByteVector &data = pic->data();
    if (data.isEmpty()) {
        return Ref<Texture2D>();
    }

    // Copy TagLib bytes → Godot buffer
    PackedByteArray bytes;
    bytes.resize(data.size());
    memcpy(bytes.ptrw(), data.data(), data.size());

    Ref<Image> image;
    image.instantiate();

    String mime = String::utf8(pic->mimeType().toCString()).to_lower();
    Error err = ERR_UNAVAILABLE;

    if (mime.find("png") != -1) {
        err = image->load_png_from_buffer(bytes);
    } else if (mime.find("jpeg") != -1 || mime.find("jpg") != -1) {
        err = image->load_jpg_from_buffer(bytes);
    } else {
        // Rare / unsupported (webp, bmp, etc.)
        return Ref<Texture2D>();
    }

    if (err != OK) {
        return Ref<Texture2D>();
    }

    return ImageTexture::create_from_image(image);
}

Ref<Texture2D> AudioMetadata::get_ogg_image(const String &p_path) {
    String abs_path = ProjectSettings::get_singleton()
                          ->globalize_path(p_path)
                          .simplify_path();

#ifdef _WIN32
    TagLib::Ogg::Vorbis::File file((const wchar_t *)abs_path.utf16().get_data());
#else
    TagLib::Ogg::Vorbis::File file(abs_path.utf8().get_data());
#endif

    if (!file.isOpen()) {
        return Ref<Texture2D>();
    }

    TagLib::Ogg::XiphComment *tag = file.tag();
    if (!tag) {
        return Ref<Texture2D>();
    }

    const TagLib::List<TagLib::FLAC::Picture *> &pictures = tag->pictureList();
    if (pictures.isEmpty()) {
        return Ref<Texture2D>();
    }

    // Prefer Front Cover
    TagLib::FLAC::Picture *pic = nullptr;
    for (auto it = pictures.begin(); it != pictures.end(); ++it) {
        if ((*it)->type() == TagLib::FLAC::Picture::FrontCover) {
            pic = *it;
            break;
        }
        if (!pic) {
            pic = *it;
        }
    }

    if (!pic) {
        return Ref<Texture2D>();
    }

    const TagLib::ByteVector &data = pic->data();
    if (data.isEmpty()) {
        return Ref<Texture2D>();
    }

    PackedByteArray bytes;
    bytes.resize(data.size());
    memcpy(bytes.ptrw(), data.data(), data.size());

    Ref<Image> image;
    image.instantiate();

    String mime = String::utf8(pic->mimeType().toCString()).to_lower();
    Error err = ERR_UNAVAILABLE;

    if (mime.find("png") != -1) {
        err = image->load_png_from_buffer(bytes);
    } else if (mime.find("jpeg") != -1 || mime.find("jpg") != -1) {
        err = image->load_jpg_from_buffer(bytes);
    } else {
        return Ref<Texture2D>();
    }

    if (err != OK) {
        return Ref<Texture2D>();
    }

    return ImageTexture::create_from_image(image);
}

Ref<Texture2D> AudioMetadata::get_m4a_image(const String &p_path) {
	String abs_path = ProjectSettings::get_singleton()
						  ->globalize_path(p_path)
						  .simplify_path();

#ifdef _WIN32
	TagLib::MP4::File file((const wchar_t *)abs_path.utf16().get_data());
#else
	TagLib::MP4::File file(abs_path.utf8().get_data());
#endif

	if (!file.isOpen()) {
		return Ref<Texture2D>();
	}

	TagLib::MP4::Tag *tag = file.tag();
	if (!tag) {
		return Ref<Texture2D>();
	}

	const TagLib::MP4::ItemMap &items = tag->itemMap();
	auto it = items.find("covr");
	if (it == items.end() || !it->second.isValid()) {
		return Ref<Texture2D>();
	}

	TagLib::MP4::CoverArtList covers = it->second.toCoverArtList();
	if (covers.isEmpty()) {
		return Ref<Texture2D>();
	}

	// Prefer first cover art (MP4 does not distinguish front/back officially)
	const TagLib::MP4::CoverArt &cover = covers.front();
	const TagLib::ByteVector &data = cover.data();

	if (data.isEmpty()) {
		return Ref<Texture2D>();
	}

	PackedByteArray bytes;
	bytes.resize(data.size());
	memcpy(bytes.ptrw(), data.data(), data.size());

	Ref<Image> image;
	image.instantiate();

	Error err = ERR_UNAVAILABLE;

	switch (cover.format()) {
		case TagLib::MP4::CoverArt::JPEG:
			err = image->load_jpg_from_buffer(bytes);
			break;
		case TagLib::MP4::CoverArt::PNG:
			err = image->load_png_from_buffer(bytes);
			break;
		default:
			return Ref<Texture2D>();
	}

	if (err != OK) {
		return Ref<Texture2D>();
	}

	return ImageTexture::create_from_image(image);
}

Ref<Texture2D> AudioMetadata::get_wma_image(const String &p_path) {
    String abs_path = ProjectSettings::get_singleton()
                          ->globalize_path(p_path)
                          .simplify_path();

#ifdef _WIN32
    TagLib::ASF::File file((const wchar_t *)abs_path.utf16().get_data());
#else
    TagLib::ASF::File file(abs_path.utf8().get_data());
#endif

    if (!file.isOpen()) {
        return Ref<Texture2D>();
    }

    TagLib::ASF::Tag *tag = file.tag();
    if (!tag) {
        return Ref<Texture2D>();
    }

    const TagLib::ASF::AttributeListMap &attrs = tag->attributeListMap();
    auto it = attrs.find("WM/Picture");

    if (it == attrs.end() || it->second.isEmpty()) {
        return Ref<Texture2D>();
    }

    // Pick the front cover if available, else first picture
    TagLib::ASF::Picture pic_obj;
    bool pic_found = false;

    for (auto &attr : it->second) {
        TagLib::ASF::Picture p = attr.toPicture(); // copy of picture

        if (p.type() == TagLib::ASF::Picture::FrontCover) {
            pic_obj = p;
            pic_found = true;
            break;
        }

        if (!pic_found) {
            pic_obj = p; // fallback
            pic_found = true;
        }
    }

    if (!pic_found) {
        return Ref<Texture2D>();
    }

    const TagLib::ByteVector &data = pic_obj.picture();
    if (data.isEmpty()) {
        return Ref<Texture2D>();
    }

    // Copy TagLib bytes → Godot buffer
    PackedByteArray bytes;
    bytes.resize(data.size());
    memcpy(bytes.ptrw(), data.data(), data.size());

    Ref<Image> image;
    image.instantiate();

    String mime = String::utf8(pic_obj.mimeType().toCString()).to_lower();
    Error err = ERR_UNAVAILABLE;

    if (mime.find("png") != -1) {
        err = image->load_png_from_buffer(bytes);
    } else if (mime.find("jpeg") != -1 || mime.find("jpg") != -1) {
        err = image->load_jpg_from_buffer(bytes);
    } else {
        return Ref<Texture2D>(); // unsupported format
    }

    if (err != OK) {
        return Ref<Texture2D>();
    }

    return ImageTexture::create_from_image(image);
}


Ref<Texture2D> AudioMetadata::get_riff_image(const String &p_path) {
	String abs_path = ProjectSettings::get_singleton()
						  ->globalize_path(p_path)
						  .simplify_path();

	TagLib::File *base = nullptr;

#ifdef _WIN32
	// Try WAV
	auto *wav = new TagLib::RIFF::WAV::File((const wchar_t *)abs_path.utf16().get_data());
	if (wav->isOpen()) {
		base = wav;
	} else {
		delete wav;
		auto *aif = new TagLib::RIFF::AIFF::File((const wchar_t *)abs_path.utf16().get_data());
		if (aif->isOpen()) base = aif;
		else delete aif;
	}
#else
	auto *wav = new TagLib::RIFF::WAV::File(abs_path.utf8().get_data());
	if (wav->isOpen()) {
		base = wav;
	} else {
		delete wav;
		auto *aif = new TagLib::RIFF::AIFF::File(abs_path.utf8().get_data());
		if (aif->isOpen()) base = aif;
		else delete aif;
	}
#endif

	if (!base) {
		return Ref<Texture2D>();
	}

	TagLib::ID3v2::Tag *id3 = nullptr;

	if (auto *wf = dynamic_cast<TagLib::RIFF::WAV::File *>(base)) {
		id3 = wf->ID3v2Tag();
	}
	else if (auto *af = dynamic_cast<TagLib::RIFF::AIFF::File *>(base)) {
		id3 = af->tag(); // AIFF stores ID3 directly
	}

	if (!id3) {
		delete base;
		return Ref<Texture2D>();
	}

	auto frames = id3->frameList("APIC");
	if (frames.isEmpty()) {
		delete base;
		return Ref<Texture2D>();
	}

	TagLib::ID3v2::AttachedPictureFrame *pic = nullptr;

	for (auto it = frames.begin(); it != frames.end(); ++it) {
		auto *f = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(*it);
		if (!f) continue;

		if (f->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
			pic = f;
			break;
		}

		if (!pic) pic = f;
	}

	if (!pic) {
		delete base;
		return Ref<Texture2D>();
	}

	const TagLib::ByteVector &data = pic->picture();
	if (data.isEmpty()) {
		delete base;
		return Ref<Texture2D>();
	}

	PackedByteArray bytes;
	bytes.resize(data.size());
	memcpy(bytes.ptrw(), data.data(), data.size());

	Ref<Image> image;
	image.instantiate();

	String mime = String::utf8(pic->mimeType().toCString()).to_lower();
	Error err = ERR_UNAVAILABLE;

	if (mime.find("png") != -1) {
		err = image->load_png_from_buffer(bytes);
	} else if (mime.find("jpeg") != -1 || mime.find("jpg") != -1) {
		err = image->load_jpg_from_buffer(bytes);
	} else {
		delete base;
		return Ref<Texture2D>();
	}

	delete base;

	if (err != OK) {
		return Ref<Texture2D>();
	}

	return ImageTexture::create_from_image(image);
}
