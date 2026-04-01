#include "ffmpeg_audio.h"
#include "core/string/print_string.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

void FFMPEGAudio::_bind_methods() {
    ClassDB::add_signal("FFMPEGAudio", MethodInfo("finished"));

    ClassDB::bind_method(D_METHOD("test_ffmpeg"), &FFMPEGAudio::test_ffmpeg);
    ClassDB::bind_method(D_METHOD("play", "path", "seek_time"), &FFMPEGAudio::play, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("stop"), &FFMPEGAudio::stop);
    ClassDB::bind_method(D_METHOD("pause"), &FFMPEGAudio::pause);
    ClassDB::bind_method(D_METHOD("resume"), &FFMPEGAudio::resume);
    ClassDB::bind_method(D_METHOD("seek", "seconds"), &FFMPEGAudio::seek);
    ClassDB::bind_method(D_METHOD("is_paused"), &FFMPEGAudio::is_paused);
    ClassDB::bind_method(D_METHOD("is_playing"), &FFMPEGAudio::is_playing);
    ClassDB::bind_method(D_METHOD("get_playback_position"), &FFMPEGAudio::get_playback_position);
    ClassDB::bind_method(D_METHOD("get_generator"), &FFMPEGAudio::get_generator);
    ClassDB::bind_method(D_METHOD("get_player"), &FFMPEGAudio::get_player);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "generator", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamGenerator"), "", "get_generator");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamPlayer"), "", "get_player");
}

void FFMPEGAudio::test_ffmpeg() {

    unsigned version = avcodec_version();

    if (version == 0) {
        print_line("FFmpeg failed to load.");
        return;
    }

    print_line("FFmpeg loaded successfully!");
    print_line("avcodec version: ", version);
}

FFMPEGAudio::FFMPEGAudio() {
    // Instantiate and configure the generator once
    generator.instantiate();
    generator->set_mix_rate(44100);
    generator->set_buffer_length(0.2);
}

// This must be present!
FFMPEGAudio::~FFMPEGAudio() {
    // You can call stop() here to be safe
    stop(); 
}


void FFMPEGAudio::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_POSTINITIALIZE: {
            player = memnew(AudioStreamPlayer);
            add_child(player);
            player->set_stream(generator);
        } break;

        case NOTIFICATION_PREDELETE: {
            // Cleanup happens automatically for Ref<>, 
        } break;
    }
}


void FFMPEGAudio::play(String path, double seek_time) {

    if (playing) {
        stop();
    }
    current_path = path;
    player->play();
    frames_played = 0;

    playback = player->get_stream_playback();

    playing = true;

    format_ctx = avformat_alloc_context();

    format_ctx->probesize = 16*1024; // 32 KB is usually plenty to identify an audio stream
    format_ctx->max_analyze_duration = 0; 
    format_ctx->flags |= AVFMT_FLAG_FAST_SEEK;
    format_ctx->flags |= AVFMT_FLAG_NOBUFFER;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "novideo", "1", 0);

    if (avformat_open_input(&format_ctx, path.utf8().get_data(), nullptr, &opts) < 0) {
        print_line("Failed to open file");
        av_dict_free(&opts);
        return;
    }
    av_dict_free(&opts);

    avformat_find_stream_info(format_ctx, nullptr);

    // Find audio stream
    for (unsigned i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1) {
        print_line("No audio stream found");
        return;
    }

    AVCodecParameters *codecpar = format_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);

    codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    avcodec_parameters_to_context(codec_ctx, codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 2);

    AVChannelLayout in_layout = codec_ctx->ch_layout;

    swr_alloc_set_opts2(
        &swr,
        &out_layout,
        AV_SAMPLE_FMT_FLT,
        44100,
        &in_layout,
        codec_ctx->sample_fmt,
        codec_ctx->sample_rate,
        0,
        nullptr
    );

    swr_init(swr);

    // Optional seek
    if (seek_time > 0.0) {
        int64_t ts = seek_time * AV_TIME_BASE;
        av_seek_frame(format_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
    }

    decode_thread.start(_decode_thread_func, this);
}


void FFMPEGAudio::_decode_thread_func(void *p_userdata) {

    FFMPEGAudio *self = static_cast<FFMPEGAudio *>(p_userdata);

    if (!self) {
        return;
    }

    const size_t FRAMES_PER_CHUNK = 2048;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    Vector<float> pcm_buffer;
    pcm_buffer.resize(FRAMES_PER_CHUNK * 2);

    while (self->playing) {

        bool is_paused;

        {
            MutexLock lock(self->mutex);
            is_paused = self->paused;
        }

        if (is_paused) {
            OS::get_singleton()->delay_usec(5000);
            continue;
        }


        int read_ret = av_read_frame(self->format_ctx, packet);

        if (read_ret < 0) {
            if (!self->playing)
                break;
            if (read_ret == AVERROR_EOF)
                break;
            // Transient error, retry
            print_line("av_read_frame error: ", read_ret);
            continue;
        }

        if (packet->stream_index != self->audio_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        avcodec_send_packet(self->codec_ctx, packet);

        while (self->playing &&
               avcodec_receive_frame(self->codec_ctx, frame) == 0) {
            

            // Resize buffer if needed
            int needed = frame->nb_samples * 2;  // *2 for stereo
            if (pcm_buffer.size() < needed)
                pcm_buffer.resize(needed);

            float *pcm_ptr = pcm_buffer.ptrw();

            int out_samples = swr_convert(
                self->swr,
                (uint8_t**)&pcm_ptr,
                frame->nb_samples,  // ← use actual frame size, not FRAMES_PER_CHUNK
                (const uint8_t**)frame->extended_data,
                frame->nb_samples
            );
            
            #ifdef false
            float *pcm_ptr = pcm_buffer.ptrw();

            int out_samples_OLD = swr_convert(
                self->swr,
                (uint8_t**)&pcm_ptr,
                FRAMES_PER_CHUNK,
                (const uint8_t**)frame->extended_data,
                frame->nb_samples
            );
            #endif

            int written = 0;

            while (written < out_samples && self->playing) {
                while (self->playing && !self->playback->can_push_buffer(1)) {
                    OS::get_singleton()->delay_usec(200);
                }

                AudioFrame af;
                af.left = pcm_buffer[written * 2];
                af.right = pcm_buffer[written * 2 + 1];

                self->playback->push_frame(af);
                {
                    MutexLock lock(self->mutex);
                    self->frames_played++;
                }
                written++;
            }
        }

        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);

    if (self->playing) {
        self->call_deferred("emit_signal", "finished");
        self->stream_finished = true;
    }
    self->playing = false;
}

double FFMPEGAudio::get_playback_position() const {

    MutexLock lock(mutex);

    if (!generator.is_valid()) {
        return 0.0;
    }

    return (double)frames_played / generator->get_mix_rate();
}

void FFMPEGAudio::stop() {
    {
        MutexLock lock(mutex);
        playing = false;
        paused = false;
    }

    if (decode_thread.is_started()) {
        decode_thread.wait_to_finish();
    }

    stream_finished = false;

    if (player) {
        player->stop();
    }

    if (playback.is_valid()) {
        playback->clear_buffer();
    }

    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }

    if (format_ctx) {
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }

    if (swr) {
        swr_free(&swr);
        swr = nullptr;
    }
}


void FFMPEGAudio::pause() {
    {
        MutexLock lock(mutex);
        paused = true;
    }

    if (player) {
        player->set_stream_paused(true);
    }

    if (playback.is_valid()) {
        playback->clear_buffer();
    }
}

void FFMPEGAudio::resume() {

    {
        MutexLock lock(mutex);
        paused = false;
    }

    if (player) {
        player->set_stream_paused(false);
    }
}

bool FFMPEGAudio::is_paused() const {
	MutexLock lock(mutex);
	return paused;
}


bool FFMPEGAudio::is_playing() const {
	MutexLock lock(mutex);
	return paused == false && playing == true;
}

void FFMPEGAudio::seek(double seconds) {
    if (!format_ctx || audio_stream_index < 0 || !generator.is_valid())
        return;

    stop();
    play(current_path, seconds);

    MutexLock lock(mutex);
    frames_played = (uint64_t)(seconds * generator->get_mix_rate());
}
