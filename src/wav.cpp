#include <inttypes.h>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#undef MKTAG

#include "includes.h"
#include "nucleas.h"
#include "wav.h"
#include "utils.h"

#if (LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 24, 100))
#define OLDCHANNEL
#endif

#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_IEEE_FLOAT 3
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

struct __attribute__((packed)) RIFF_HDR
{
    uint32_t ChunkID;
    uint32_t ChunkSize;
    uint32_t Format;
};

struct __attribute__((packed)) RIFF_SUBCHUNK
{
    uint32_t SubchunkID;
    uint32_t SubchunkSize;
};

struct __attribute__((packed)) PCM_fmt
{
    uint16_t AudioFormat;
    uint16_t NumChannels;
    uint32_t SampleRate;
    uint32_t ByteRate;
    uint16_t BlockAlign;
    uint16_t BitsPerSample;
};

static void wav_skip_chunk(FSMgr::FileHandle *fil, uint32_t size)
{
    fil->seek(size + (size & 1), SEEK_CUR);
}

static rsrc * wav_alloc_sample(NC_STACK_wav *obj, IDVList &stak, const std::vector<uint8_t> &pcm, int sampleRate, ALenum format)
{
    if ( pcm.empty() || sampleRate <= 0 )
        return NULL;

    stak.Add(NC_STACK_sample::SMPL_ATT_LEN, (int32_t)pcm.size());
    stak.Add(NC_STACK_sample::SMPL_ATT_TYPE, (int32_t)1);

    rsrc *res = obj->NC_STACK_sample::rsrc_func64(stak);

    if ( !res )
        return NULL;

    TSampleData *smpl = (TSampleData *)res->data;

    if ( !smpl )
    {
        obj->rsrc_func65(res);
        return NULL;
    }

    memcpy(smpl->Data, pcm.data(), pcm.size());
    smpl->SampleRate = sampleRate;
    smpl->Format = format;

    return res;
}

static const char * wav_al_format_name(ALenum format)
{
    switch (format)
    {
        case AL_FORMAT_MONO8:
            return "AL_FORMAT_MONO8";

        case AL_FORMAT_STEREO8:
            return "AL_FORMAT_STEREO8";

        case AL_FORMAT_MONO16:
            return "AL_FORMAT_MONO16";

        case AL_FORMAT_STEREO16:
            return "AL_FORMAT_STEREO16";

        default:
            return "unknown";
    }
}

static ALenum wav_pcm_openal_format(uint16_t channels, uint16_t bits)
{
    if ( channels == 1 && bits == 8 )
        return AL_FORMAT_MONO8;

    if ( channels == 2 && bits == 8 )
        return AL_FORMAT_STEREO8;

    if ( channels == 1 && bits == 16 )
        return AL_FORMAT_MONO16;

    if ( channels == 2 && bits == 16 )
        return AL_FORMAT_STEREO16;

    return 0;
}

static uint32_t wav_read_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool wav_pcm_convert_to_openal(const PCM_fmt &fmt, const std::vector<uint8_t> &src, std::vector<uint8_t> *dst, ALenum *alFormat, std::string *reason)
{
    if ( fmt.NumChannels < 1 || fmt.NumChannels > 2 )
    {
        if ( reason )
            *reason = fmt::sprintf("unsupported channel count %d", fmt.NumChannels);

        return false;
    }

    ALenum directFormat = 0;

    if ( fmt.AudioFormat == WAVE_FORMAT_PCM )
        directFormat = wav_pcm_openal_format(fmt.NumChannels, fmt.BitsPerSample);

    if ( directFormat )
    {
        *dst = src;
        *alFormat = directFormat;
        return true;
    }

    if ( fmt.AudioFormat == WAVE_FORMAT_PCM && (fmt.BitsPerSample == 24 || fmt.BitsPerSample == 32) )
    {
        int bytesPerSample = fmt.BitsPerSample / 8;
        int frameSize = bytesPerSample * fmt.NumChannels;

        if ( frameSize <= 0 || src.size() < (size_t)frameSize )
        {
            if ( reason )
                *reason = "PCM data is too small";

            return false;
        }

        size_t frameCount = src.size() / frameSize;
        dst->resize(frameCount * fmt.NumChannels * 2);

        const uint8_t *in = src.data();
        uint8_t *out = dst->data();

        for (size_t sampleId = 0; sampleId < frameCount * fmt.NumChannels; sampleId++)
        {
            int32_t sample = 0;

            if ( fmt.BitsPerSample == 24 )
            {
                uint32_t raw = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16);
                sample = (int32_t)raw;
                if ( sample & 0x800000 )
                    sample |= ~0xFFFFFF;

                sample >>= 8;
                in += 3;
            }
            else
            {
                uint32_t raw = wav_read_u32le(in);
                sample = (int32_t)raw;
                sample >>= 16;
                in += 4;
            }

            int16_t outSample = (int16_t)sample;
            out[0] = (uint8_t)(outSample & 0xFF);
            out[1] = (uint8_t)((outSample >> 8) & 0xFF);
            out += 2;
        }

        *alFormat = fmt.NumChannels > 1 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
        return true;
    }

    if ( fmt.AudioFormat == WAVE_FORMAT_IEEE_FLOAT && fmt.BitsPerSample == 32 )
    {
        int frameSize = 4 * fmt.NumChannels;

        if ( frameSize <= 0 || src.size() < (size_t)frameSize )
        {
            if ( reason )
                *reason = "float PCM data is too small";

            return false;
        }

        size_t frameCount = src.size() / frameSize;
        dst->resize(frameCount * fmt.NumChannels * 2);

        const uint8_t *in = src.data();
        uint8_t *out = dst->data();

        for (size_t sampleId = 0; sampleId < frameCount * fmt.NumChannels; sampleId++)
        {
            float value;
            memcpy(&value, in, sizeof(float));
            in += 4;

            if ( value > 1.0f )
                value = 1.0f;
            else if ( value < -1.0f )
                value = -1.0f;

            int16_t outSample = (int16_t)(value * 32767.0f);
            out[0] = (uint8_t)(outSample & 0xFF);
            out[1] = (uint8_t)((outSample >> 8) & 0xFF);
            out += 2;
        }

        *alFormat = fmt.NumChannels > 1 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
        return true;
    }

    if ( reason )
        *reason = fmt::sprintf("unsupported PCM WAV shape: encoding=%d channels=%d rate=%d bits=%d",
                               fmt.AudioFormat,
                               fmt.NumChannels,
                               fmt.SampleRate,
                               fmt.BitsPerSample);

    return false;
}

static bool wav_is_legacy_pcm8_mono(const PCM_fmt &fmt)
{
    return fmt.AudioFormat == WAVE_FORMAT_PCM && fmt.NumChannels == 1 && fmt.BitsPerSample == 8;
}

static rsrc * wav_load_pcm_wav(NC_STACK_wav *obj, IDVList &stak, const std::string &filname, std::string *reason)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(fmt::sprintf("rsrc:%s", filname), "rb");

    if ( !fil )
    {
        if ( reason )
            *reason = "file not found";

        return NULL;
    }

    RIFF_HDR rff;
    bool haveFmt = false;
    PCM_fmt fmt;
    memset(&fmt, 0, sizeof(fmt));

    if ( fil->read(&rff, sizeof(RIFF_HDR)) != sizeof(RIFF_HDR) )
    {
        if ( reason )
            *reason = "file too small for RIFF header";

        delete fil;
        return NULL;
    }

    rff.ChunkID = SWAP32(rff.ChunkID);
    rff.Format = SWAP32(rff.Format);

    if ( rff.ChunkID != TAG_RIFF || rff.Format != TAG_WAVE )
    {
        if ( reason )
            *reason = "not RIFF/WAVE";

        delete fil;
        return NULL;
    }

    while ( 1 )
    {
        RIFF_SUBCHUNK sbchunk;

        if ( fil->read(&sbchunk, sizeof(RIFF_SUBCHUNK)) != sizeof(RIFF_SUBCHUNK) )
            break;

        sbchunk.SubchunkID = SWAP32(sbchunk.SubchunkID);

        if ( sbchunk.SubchunkID == TAG_fmt )
        {
            if ( sbchunk.SubchunkSize >= sizeof(PCM_fmt) )
            {
                fil->read(&fmt, sizeof(PCM_fmt));
                haveFmt = true;

                if ( sbchunk.SubchunkSize > sizeof(PCM_fmt) )
                {
                    uint32_t extraSize = sbchunk.SubchunkSize - sizeof(PCM_fmt);
                    std::vector<uint8_t> extra(extraSize);

                    if ( !extra.empty() )
                        fil->read(extra.data(), extra.size());

                    if ( fmt.AudioFormat == WAVE_FORMAT_EXTENSIBLE && extra.size() >= 24 )
                    {
                        uint32_t subFormat = wav_read_u32le(extra.data() + 8);

                        if ( subFormat == WAVE_FORMAT_PCM || subFormat == WAVE_FORMAT_IEEE_FLOAT )
                            fmt.AudioFormat = (uint16_t)subFormat;
                    }

                    if ( extraSize & 1 )
                        fil->seek(1, SEEK_CUR);
                }
            }
            else
            {
                wav_skip_chunk(fil, sbchunk.SubchunkSize);
            }
        }
        else if ( sbchunk.SubchunkID == TAG_data )
        {
            ALenum alFormat = 0;

            if ( haveFmt && (fmt.AudioFormat == WAVE_FORMAT_PCM || fmt.AudioFormat == WAVE_FORMAT_IEEE_FLOAT) )
            {
                std::vector<uint8_t> rawPcm(sbchunk.SubchunkSize);

                if ( !rawPcm.empty() && fil->read(rawPcm.data(), rawPcm.size()) == rawPcm.size() )
                {
                    std::vector<uint8_t> pcm;
                    std::string convertReason;

                    if ( !wav_pcm_convert_to_openal(fmt, rawPcm, &pcm, &alFormat, &convertReason) )
                    {
                        if ( reason )
                            *reason = convertReason;

                        delete fil;
                        return NULL;
                    }

                    delete fil;
                    rsrc *res = wav_alloc_sample(obj, stak, pcm, fmt.SampleRate, alFormat);

                    if ( res )
                    {
                        const char *path = wav_is_legacy_pcm8_mono(fmt) ? "legacy_wav_direct" : "modern_pcm_wav";

                        ypa_log_out("[AUDIO_LOAD] filename=%s detected=WAV_PCM decoder_path=%s channels=%d sample_rate=%d bits_per_sample=%d sample_format=interleaved_pcm openal_format=%s bytes=%u result=success reason=ok\n",
                                    filname.c_str(),
                                    path,
                                    fmt.NumChannels,
                                    fmt.SampleRate,
                                    fmt.BitsPerSample,
                                    wav_al_format_name(alFormat),
                                    (unsigned int)pcm.size());
                    }

                    return res;
                }

                if ( reason )
                    *reason = fmt::sprintf("short data read: channels=%d rate=%d bits=%d format=%s",
                                           fmt.NumChannels,
                                           fmt.SampleRate,
                                           fmt.BitsPerSample,
                                           wav_al_format_name(alFormat));

                delete fil;
                return NULL;
            }

            if ( reason )
            {
                if ( !haveFmt )
                    *reason = "data chunk found before fmt chunk";
                else if ( fmt.AudioFormat != WAVE_FORMAT_PCM && fmt.AudioFormat != WAVE_FORMAT_IEEE_FLOAT )
                    *reason = fmt::sprintf("unsupported WAV encoding %d: channels=%d rate=%d bits=%d",
                                           fmt.AudioFormat,
                                           fmt.NumChannels,
                                           fmt.SampleRate,
                                           fmt.BitsPerSample);
                else
                    *reason = fmt::sprintf("unsupported PCM WAV shape: encoding=%d channels=%d rate=%d bits=%d",
                                           fmt.AudioFormat,
                                           fmt.NumChannels,
                                           fmt.SampleRate,
                                           fmt.BitsPerSample);
            }

            wav_skip_chunk(fil, sbchunk.SubchunkSize);
        }
        else
        {
            wav_skip_chunk(fil, sbchunk.SubchunkSize);
        }
    }

    delete fil;
    if ( reason )
        *reason = "missing supported PCM data chunk";

    return NULL;
}

static rsrc * wav_load_legacy_raw_wav(NC_STACK_wav *obj, IDVList &stak, const std::string &filname, std::string *reason)
{
    rsrc *res = NULL;
    FSMgr::FileHandle *fil = uaOpenFileAlloc(fmt::sprintf("rsrc:%s", filname), "rb");

    if ( !fil )
    {
        if ( reason )
            *reason = "file not found";

        return NULL;
    }

    RIFF_HDR rff;

    fil->read(&rff, sizeof(RIFF_HDR));

    rff.ChunkID = SWAP32(rff.ChunkID);
    rff.Format = SWAP32(rff.Format);

    if ( rff.ChunkID == TAG_RIFF && rff.Format == TAG_WAVE )
    {
        PCM_fmt fmt;
        memset(&fmt, 0, sizeof(fmt));

        while ( 1 )
        {
            RIFF_SUBCHUNK sbchunk;

            if ( fil->read(&sbchunk, sizeof(RIFF_SUBCHUNK)) != sizeof(RIFF_SUBCHUNK) )
                break;

            sbchunk.SubchunkID = SWAP32(sbchunk.SubchunkID);

            if (sbchunk.SubchunkID == TAG_data)
            {
                if ( !wav_is_legacy_pcm8_mono(fmt) )
                {
                    if ( reason )
                        *reason = fmt::sprintf("not legacy WAV PCM mono 8-bit: encoding=%d channels=%d rate=%d bits=%d",
                                               fmt.AudioFormat,
                                               fmt.NumChannels,
                                               fmt.SampleRate,
                                               fmt.BitsPerSample);

                    fil->seek(sbchunk.SubchunkSize, SEEK_CUR);
                    continue;
                }

                stak.Add(NC_STACK_sample::SMPL_ATT_LEN, (int32_t)sbchunk.SubchunkSize);
                stak.Add(NC_STACK_sample::SMPL_ATT_TYPE, (int32_t)1);

                res = obj->NC_STACK_sample::rsrc_func64(stak);

                if ( res )
                {
                    TSampleData *smpl = (TSampleData *)res->data;

                    if ( !smpl )
                    {
                        obj->rsrc_func65(res);
                        delete fil;

                        if ( reason )
                            *reason = "legacy raw WAV sample allocation failed";

                        return NULL;
                    }

                    fil->read(smpl->Data, sbchunk.SubchunkSize);
                    smpl->SampleRate = fmt.SampleRate;
                    smpl->Format = AL_FORMAT_MONO8;

                    ypa_log_out("[AUDIO_LOAD] filename=%s detected=WAV_PCM decoder_path=legacy_wav channels=%d sample_rate=%d bits_per_sample=%d sample_format=unsigned_pcm8 openal_format=%s bytes=%u result=success reason=ok\n",
                                filname.c_str(),
                                fmt.NumChannels,
                                fmt.SampleRate,
                                fmt.BitsPerSample,
                                wav_al_format_name(smpl->Format),
                                (unsigned int)sbchunk.SubchunkSize);

                    delete fil;
                    return res;
                }
            }
            else if (sbchunk.SubchunkID == TAG_fmt)
            {
                if (sbchunk.SubchunkSize >= sizeof(PCM_fmt))
                {
                    fil->read(&fmt, sizeof(PCM_fmt));

                    if ( sbchunk.SubchunkSize > sizeof(PCM_fmt) )
                        fil->seek(sbchunk.SubchunkSize - sizeof(PCM_fmt), SEEK_CUR);
                }
                else
                {
                    fil->seek(sbchunk.SubchunkSize, SEEK_CUR);
                }
            }
            else
            {
                fil->seek(sbchunk.SubchunkSize, SEEK_CUR);
            }
        }
    }
    else if ( reason )
    {
        *reason = "not RIFF/WAVE";
    }

    delete fil;

    if ( reason && reason->empty() )
        *reason = "legacy raw WAV data chunk not loaded";

    return NULL;
}

static int wav_ffmpeg_read(void *opaque, uint8_t *buf, int buf_size)
{
    FSMgr::FileHandle *fil = (FSMgr::FileHandle *)opaque;
    size_t readed = fil->read(buf, buf_size);

    if ( readed == 0 )
        return AVERROR_EOF;

    return (int)readed;
}

static int64_t wav_ffmpeg_seek(void *opaque, int64_t offset, int whence)
{
    FSMgr::FileHandle *fil = (FSMgr::FileHandle *)opaque;

    if ( whence == AVSEEK_SIZE )
    {
        size_t cur = fil->tell();
        fil->seek(0, SEEK_END);
        size_t size = fil->tell();
        fil->seek((long int)cur, SEEK_SET);
        return (int64_t)size;
    }

    whence &= ~AVSEEK_FORCE;

    int origin = SEEK_SET;

    if ( whence == SEEK_CUR )
        origin = SEEK_CUR;
    else if ( whence == SEEK_END )
        origin = SEEK_END;

    if ( fil->seek((long int)offset, origin) != 0 )
        return -1;

    return (int64_t)fil->tell();
}

static bool wav_decode_frame(SwrContext *swrCtx, AVCodecContext *codecCtx, AVFrame *frame, int outChannels, std::vector<uint8_t> *pcm)
{
    int dstSamples = (int)av_rescale_rnd(
        swr_get_delay(swrCtx, codecCtx->sample_rate) + frame->nb_samples,
        codecCtx->sample_rate,
        codecCtx->sample_rate,
        AV_ROUND_UP);

    if ( dstSamples <= 0 )
        return true;

    uint8_t *outData = NULL;
    int outLineSize = 0;

    if ( av_samples_alloc(&outData, &outLineSize, outChannels, dstSamples, AV_SAMPLE_FMT_S16, 0) < 0 )
        return false;

    int converted = swr_convert(swrCtx, &outData, dstSamples, (const uint8_t **)frame->extended_data, frame->nb_samples);

    if ( converted < 0 )
    {
        av_freep(&outData);
        return false;
    }

    size_t bytes = converted * outChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    size_t oldSize = pcm->size();
    pcm->resize(oldSize + bytes);
    memcpy(pcm->data() + oldSize, outData, bytes);

    av_freep(&outData);
    return true;
}

static bool wav_receive_decoded_frames(SwrContext *swrCtx, AVCodecContext *codecCtx, AVFrame *frame, int outChannels, std::vector<uint8_t> *pcm)
{
    while ( 1 )
    {
        int ret = avcodec_receive_frame(codecCtx, frame);

        if ( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF )
            return true;

        if ( ret < 0 )
            return false;

        bool ok = wav_decode_frame(swrCtx, codecCtx, frame, outChannels, pcm);
        av_frame_unref(frame);

        if ( !ok )
            return false;
    }
}

static SwrContext * wav_create_resampler(AVCodecContext *codecCtx, int outChannels, int sampleRate)
{
    SwrContext *swrCtx = NULL;

#ifndef OLDCHANNEL
    AVChannelLayout outLayout;
    AVChannelLayout inLayout;
    memset(&outLayout, 0, sizeof(outLayout));
    memset(&inLayout, 0, sizeof(inLayout));
    av_channel_layout_default(&outLayout, outChannels);

    if ( codecCtx->ch_layout.nb_channels > 0 )
        av_channel_layout_copy(&inLayout, &codecCtx->ch_layout);
    else
        av_channel_layout_default(&inLayout, outChannels);

    if ( swr_alloc_set_opts2(&swrCtx, &outLayout, AV_SAMPLE_FMT_S16, sampleRate,
                             &inLayout, codecCtx->sample_fmt, sampleRate, 0, NULL) < 0 )
    {
        swrCtx = NULL;
    }

    av_channel_layout_uninit(&outLayout);
    av_channel_layout_uninit(&inLayout);
#else
    swrCtx = swr_alloc_set_opts(NULL,
                                outChannels > 1 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO,
                                AV_SAMPLE_FMT_S16,
                                sampleRate,
                                codecCtx->channel_layout ? codecCtx->channel_layout : av_get_default_channel_layout(codecCtx->channels),
                                codecCtx->sample_fmt,
                                sampleRate,
                                0,
                                NULL);
#endif

    return swrCtx;
}

static rsrc * wav_load_modern_audio(NC_STACK_wav *obj, IDVList &stak, const std::string &filname, std::string *reason)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc(fmt::sprintf("rsrc:%s", filname), "rb");

    if ( !fil )
    {
        if ( reason )
            *reason = "file not found";

        return NULL;
    }

    const int avioBufferSize = 32768;
    uint8_t *avioBuffer = (uint8_t *)av_malloc(avioBufferSize);
    AVIOContext *avioCtx = NULL;
    AVFormatContext *formatCtx = NULL;
    AVCodecContext *codecCtx = NULL;
    SwrContext *swrCtx = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    std::vector<uint8_t> pcm;
    int outChannels = 1;
    int sampleRate = 0;
    int streamId = -1;
    AVCodecParameters *codecPar = NULL;
#if LIBAVCODEC_VERSION_MAJOR < 59
    AVCodec *codec = NULL;
#else
    const AVCodec *codec = NULL;
#endif
    bool openedInput = false;
    rsrc *res = NULL;

    if ( !avioBuffer )
    {
        if ( reason )
            *reason = "could not allocate FFmpeg AVIO buffer";

        goto cleanup;
    }

    avioCtx = avio_alloc_context(avioBuffer, avioBufferSize, 0, fil, wav_ffmpeg_read, NULL, wav_ffmpeg_seek);

    if ( !avioCtx )
    {
        if ( reason )
            *reason = "could not allocate FFmpeg AVIO context";

        goto cleanup;
    }

    avioCtx->seekable = AVIO_SEEKABLE_NORMAL;
    avioBuffer = NULL;
    formatCtx = avformat_alloc_context();

    if ( !formatCtx )
    {
        if ( reason )
            *reason = "could not allocate FFmpeg format context";

        goto cleanup;
    }

    formatCtx->pb = avioCtx;
    formatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    if ( avformat_open_input(&formatCtx, NULL, NULL, NULL) < 0 )
    {
        if ( reason )
            *reason = "FFmpeg could not open/probe input";

        goto cleanup;
    }

    openedInput = true;

    if ( avformat_find_stream_info(formatCtx, NULL) < 0 )
    {
        if ( reason )
            *reason = "FFmpeg could not find stream info";

        goto cleanup;
    }

    streamId = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if ( streamId < 0 )
    {
        if ( reason )
            *reason = "FFmpeg found no audio stream";

        goto cleanup;
    }

    codecPar = formatCtx->streams[streamId]->codecpar;
    codec = avcodec_find_decoder(codecPar->codec_id);

    if ( !codec )
    {
        if ( reason )
            *reason = "FFmpeg found no decoder for audio stream";

        goto cleanup;
    }

    codecCtx = avcodec_alloc_context3(codec);

    if ( !codecCtx )
    {
        if ( reason )
            *reason = "could not allocate FFmpeg codec context";

        goto cleanup;
    }

    if ( avcodec_parameters_to_context(codecCtx, codecPar) < 0 )
    {
        if ( reason )
            *reason = "FFmpeg could not copy codec parameters";

        goto cleanup;
    }

    if ( avcodec_open2(codecCtx, codec, NULL) < 0 )
    {
        if ( reason )
            *reason = "FFmpeg could not open decoder";

        goto cleanup;
    }

    sampleRate = codecCtx->sample_rate;

    if ( sampleRate <= 0 )
    {
        if ( reason )
            *reason = "decoded audio has invalid sample rate";

        goto cleanup;
    }

#ifndef OLDCHANNEL
    outChannels = codecCtx->ch_layout.nb_channels > 1 ? 2 : 1;
#else
    outChannels = codecCtx->channels > 1 ? 2 : 1;
#endif

    swrCtx = wav_create_resampler(codecCtx, outChannels, sampleRate);

    if ( !swrCtx )
    {
        if ( reason )
            *reason = "could not allocate FFmpeg resampler";

        goto cleanup;
    }

    if ( swr_init(swrCtx) < 0 )
    {
        if ( reason )
            *reason = "could not initialize FFmpeg resampler";

        goto cleanup;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();

    if ( !packet || !frame )
    {
        if ( reason )
            *reason = "could not allocate FFmpeg packet/frame";

        goto cleanup;
    }

    while ( av_read_frame(formatCtx, packet) >= 0 )
    {
        if ( packet->stream_index == streamId )
        {
            if ( avcodec_send_packet(codecCtx, packet) < 0 )
            {
                av_packet_unref(packet);
                if ( reason )
                    *reason = "FFmpeg decoder rejected packet";

                goto cleanup;
            }

            if ( !wav_receive_decoded_frames(swrCtx, codecCtx, frame, outChannels, &pcm) )
            {
                av_packet_unref(packet);
                if ( reason )
                    *reason = "FFmpeg failed while receiving decoded frames";

                goto cleanup;
            }
        }

        av_packet_unref(packet);
    }

    if ( avcodec_send_packet(codecCtx, NULL) >= 0 )
    {
        if ( !wav_receive_decoded_frames(swrCtx, codecCtx, frame, outChannels, &pcm) )
        {
            if ( reason )
                *reason = "FFmpeg failed while flushing decoder";

            goto cleanup;
        }
    }

    if ( pcm.empty() )
    {
        if ( reason )
            *reason = "FFmpeg decoded an empty PCM buffer";

        goto cleanup;
    }

    res = wav_alloc_sample(obj, stak, pcm, sampleRate, outChannels > 1 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16);

    if ( !res && reason )
        *reason = "could not allocate sample for FFmpeg decoded PCM";
    else if ( res )
    {
        const char *sampleFmtName = av_get_sample_fmt_name(codecCtx->sample_fmt);

        if ( !sampleFmtName )
            sampleFmtName = "unknown";

        ypa_log_out("[AUDIO_LOAD] filename=%s detected=%s decoder_path=modern_ffmpeg channels=%d sample_rate=%d bits_per_sample=16 sample_format=%s->s16_interleaved openal_format=%s bytes=%u result=success reason=ok\n",
                    filname.c_str(),
                    avcodec_get_name(codecPar->codec_id),
                    outChannels,
                    sampleRate,
                    sampleFmtName,
                    wav_al_format_name(outChannels > 1 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16),
                    (unsigned int)pcm.size());
    }

cleanup:
    if ( frame )
        av_frame_free(&frame);

    if ( packet )
        av_packet_free(&packet);

    if ( swrCtx )
        swr_free(&swrCtx);

    if ( codecCtx )
        avcodec_free_context(&codecCtx);

    if ( openedInput )
        avformat_close_input(&formatCtx);
    else if ( formatCtx )
        avformat_free_context(formatCtx);

    if ( avioCtx )
    {
        av_freep(&avioCtx->buffer);
        avio_context_free(&avioCtx);
    }

    if ( avioBuffer )
        av_free(avioBuffer);

    delete fil;
    return res;
}

rsrc * NC_STACK_wav::rsrc_func64(IDVList &stak)
{
    std::string filename = stak.Get<std::string>(RSRC_ATT_NAME, "");

    if ( filename.empty() )
        return NULL;

    std::string pcmReason;
    std::string modernReason;
    std::string legacyReason;

    rsrc *res = wav_load_legacy_raw_wav(this, stak, filename, &legacyReason);

    if ( res )
        return res;

    res = wav_load_pcm_wav(this, stak, filename, &pcmReason);

    if ( res )
        return res;

    res = wav_load_modern_audio(this, stak, filename, &modernReason);

    if ( res )
        return res;

    ypa_log_out("[AUDIO_LOAD] filename=%s decoder_path=legacy_wav->modern_pcm_wav->modern_ffmpeg result=failure legacy_reason=\"%s\" pcm_reason=\"%s\" ffmpeg_reason=\"%s\"\n",
                filename.c_str(),
                legacyReason.c_str(),
                pcmReason.c_str(),
                modernReason.c_str());

    return NULL;
}
