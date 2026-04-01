#ifndef FFMPEG_AUDIO_H
#define FFMPEG_AUDIO_H

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"

#include "servers/audio/effects/audio_stream_generator.h"
#include "scene/audio/audio_stream_player.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

class FFMPEGAudio : public Node {
    GDCLASS(FFMPEGAudio, Object);

protected:
    static void _bind_methods();
    void _notification(int p_what);
    bool playing = false;

private:
    Ref<AudioStreamGenerator> generator;
    Ref<AudioStreamGeneratorPlayback> playback;

    AudioStreamPlayer *player = nullptr;

    Thread decode_thread;

    // FFmpeg state
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    SwrContext *swr = nullptr;
    int audio_stream_index = -1;

    static void _decode_thread_func(void *p_userdata);
    bool stream_finished = false;
    bool paused = false;
    Mutex mutex;
    uint64_t frames_played = 0;
    String current_path;

public:
    void test_ffmpeg();
    void play(String path, double seek_time = 0.0);
    void stop();
    void pause();
    void resume();
    void seek(double seconds);
    bool is_paused() const;
    bool is_playing() const;
    double get_playback_position() const;
    Ref<AudioStreamGenerator> get_generator() const { return generator; }
    AudioStreamPlayer* get_player() const { return player; }
    FFMPEGAudio();
    ~FFMPEGAudio();
};

#endif