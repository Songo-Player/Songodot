#ifndef AUDIO_METADATA_H
#define AUDIO_METADATA_H

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "scene/resources/texture.h"

class AudioMetadata : public Object {
    GDCLASS(AudioMetadata, Object);

protected:
    static void _bind_methods();

public:
	Dictionary read_m4a(const String &p_path, const Array &attrs);
	Dictionary read_universal(const String &p_path, const Array &attrs);
	Dictionary read_audio(const String &p_path, const Array &attrs);
	static Ref<Texture2D> get_cover_image(const String &p_path);
	static Ref<Texture2D> get_mp3_image(const String &p_path);
	static Ref<Texture2D> get_flac_image(const String &p_path);
	static Ref<Texture2D> get_ogg_image(const String &p_path);
	static Ref<Texture2D> get_m4a_image(const String &p_path);
	static Ref<Texture2D> get_wma_image(const String &p_path);
	static Ref<Texture2D> get_riff_image(const String &p_path);
};

#endif
