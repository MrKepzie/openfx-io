/*
 OFX ffmpegWriter plugin.
 Writes a video output file using the libav library.
 
 Copyright (C) 2013 INRIA
 Author Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 */


#if (defined(_STDINT_H) || defined(_STDINT_H_) || defined(_MSC_STDINT_H_)) && !defined(UINT64_C)
#warning "__STDC_CONSTANT_MACROS has to be defined before including <stdint.h>, this file will probably not compile."
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // ...or stdint.h wont' define UINT64_C, needed by libavutil
#endif
#include "WriteFFmpeg.h"

#include <cstdio>
#include <sstream>

#if _WIN32
#define snprintf sprintf_s
#endif

#if defined(_WIN32) || defined(WIN64)
#  include <windows.h> // for GetSystemInfo()
#else
#  include <unistd.h> // for sysconf()
#endif

extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avio.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}
#include "FFmpegCompat.h"
#include "IOUtility.h"

#include "GenericWriter.h"

#define kPluginName "WriteFFmpeg"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write images using FFmpeg."
#define kPluginIdentifier "fr.inria.openfx.WriteFFmpeg"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha false

struct AVCodecContext;
struct AVFormatContext;
struct AVStream;

class WriteFFmpegPlugin : public GenericWriterPlugin
{
public:

    WriteFFmpegPlugin(OfxImageEffectHandle handle);

    virtual ~WriteFFmpegPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;



private:

    virtual void beginEncode(const std::string& filename,const OfxRectI& rod,const OFX::BeginSequenceRenderArguments& args) OVERRIDE FINAL;

    virtual void endEncode(const OFX::EndSequenceRenderArguments& args) OVERRIDE FINAL;

    virtual void encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;


    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;

    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const { return OFX::eImageUnPreMultiplied; }

    void freeFormat();
    
    
    ///These members are not protected and only read/written by/to by the same thread.
    AVCodecContext*   _codecContext;
    AVFormatContext*  _formatContext;
    AVStream* _stream;
    int _lastTimeEncoded; //< the frame index of the last frame encoded.

    OFX::ChoiceParam* _format;
    OFX::DoubleParam* _fps;

    OFX::ChoiceParam* _codec;
    OFX::IntParam* _bitRate;
    OFX::IntParam* _bitRateTolerance;
    OFX::IntParam* _gopSize;
    OFX::IntParam* _bFrames;
    OFX::ChoiceParam* _macroBlockDecision;

};


#define kParamFormat "format"
#define kParamFPS "fps"
#define kParamAdvanced "advanced"
#define kParamCodec "codec"
#define kParamBitRate "bitRate"
#define kParamBitRateTolerance "bitRateTolerance"
#define kParamGop "gop"
#define kParamBFrames "bframes"
#define kParamMBDecision "mbDecision"



class FFmpegSingleton {
    
public:
    
    static FFmpegSingleton &Instance() {
        return m_instance;
    };
    
    
    const std::vector<std::string>& getFormatsShortNames() const { return _formatsShortNames; }
    
    const std::vector<std::string>& getFormatsLongNames() const { return _formatsLongNames; }
    
    const std::vector<std::string>& getCodecsShortNames() const { return _codecsShortNames; }
    
    const std::vector<std::string>& getCodecsLongNames() const { return _codecsLongNames; }
    
private:
    
    FFmpegSingleton &operator= (const FFmpegSingleton &) {
        return *this;
    }
    FFmpegSingleton(const FFmpegSingleton &) {}
    
    static FFmpegSingleton m_instance;
    
    FFmpegSingleton();
    
    ~FFmpegSingleton();
    
    
    std::vector<std::string> _formatsLongNames;
    std::vector<std::string> _formatsShortNames;
    std::vector<std::string> _codecsLongNames;
    std::vector<std::string> _codecsShortNames;
};

FFmpegSingleton FFmpegSingleton::m_instance = FFmpegSingleton();

FFmpegSingleton::FFmpegSingleton(){
    av_log_set_level(AV_LOG_WARNING);
    av_register_all();
    
    AVOutputFormat* fmt = av_oformat_next(NULL);
    while (fmt) {
        
        if (fmt->video_codec != AV_CODEC_ID_NONE) {
            if (fmt->long_name) {
                _formatsLongNames.push_back(std::string(fmt->long_name) + std::string(" (") + std::string(fmt->name) + std::string(")"));
                _formatsShortNames.push_back(fmt->name);
            }
        }
        fmt = av_oformat_next(fmt);
    }
    
    AVCodec* c = av_codec_next(NULL);
    while (c) {
        if (c->type == AVMEDIA_TYPE_VIDEO && c->encode2) {
            if (c->long_name) {
                _codecsLongNames.push_back(c->long_name);
                _codecsShortNames.push_back(c->name);
            }
        }
        c = av_codec_next(c);
    }
}

FFmpegSingleton::~FFmpegSingleton(){
    
}

// Use one decoding thread per processor for video decoding.
// source: http://git.savannah.gnu.org/cgit/bino.git/tree/src/media_object.cpp
static int video_decoding_threads()
{
    static long n = -1;
    if (n < 0) {
#if defined(WIN32) || defined(WIN64)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        n = si.dwNumberOfProcessors;
#else
        n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
        if (n < 1) {
            n = 1;
        } else if (n > 16) {
            n = 16;
        }
    }
    return n;
}

using namespace OFX;

WriteFFmpegPlugin::WriteFFmpegPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
, _codecContext(0)
, _formatContext(0)
, _stream(0)
, _lastTimeEncoded(-1)
, _format(0)
, _fps(0)
, _codec(0)
, _bitRate(0)
, _bitRateTolerance(0)
, _gopSize(0)
,_bFrames(0)
, _macroBlockDecision(0)
{
    _format = fetchChoiceParam(kParamFormat);
    _fps = fetchDoubleParam(kParamFPS);
    _codec = fetchChoiceParam(kParamCodec);
    _bitRate = fetchIntParam(kParamBitRate);
    _bitRateTolerance = fetchIntParam(kParamBitRateTolerance);
    _gopSize = fetchIntParam(kParamGop);
    _bFrames = fetchIntParam(kParamBFrames);
    _macroBlockDecision = fetchChoiceParam(kParamMBDecision);
}

WriteFFmpegPlugin::~WriteFFmpegPlugin(){
    
}

void WriteFFmpegPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName){
    GenericWriterPlugin::changedParam(args, paramName);
}


bool WriteFFmpegPlugin::isImageFile(const std::string& ext) const{
    return ext == "bmp" ||
    ext == "pix" ||
    ext == "dpx" ||
    ext == "exr" ||
    ext == "jpeg"||
    ext == "jpg" ||
    ext == "png" ||
    ext == "ppm" ||
    ext == "ptx" ||
    ext == "tiff" ||
    ext == "tga" ||
    ext == "rgba" ||
    ext == "rgb";
}



void WriteFFmpegPlugin::beginEncode(const std::string& filename,const OfxRectI& rod,const OFX::BeginSequenceRenderArguments& args)
{
    if (!args.sequentialRenderStatus || _formatContext || _stream) {
        setPersistentMessage(OFX::Message::eMessageError, "", "FFmpeg: can only write files in sequential order");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    if (args.isInteractive) {
        setPersistentMessage(OFX::Message::eMessageError, "", "FFmpeg: can only write files when in non-interactive mode.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    assert(!_formatContext);
    
        ////////////////////                        ////////////////////
        //////////////////// INTIALISE FORMAT       ////////////////////
    
    AVOutputFormat* fmt = 0;
    int formatValue;
    _format->getValue(formatValue);
    
    if (formatValue == 0) {
        fmt = av_guess_format(NULL, filename.c_str(), NULL);
        if (!fmt) {
            setPersistentMessage(OFX::Message::eMessageError, "","Invalid file extension");
            throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    } else {
        const std::vector<std::string>& formatsShortNames = FFmpegSingleton::Instance().getFormatsShortNames();
        assert(formatValue < (int)formatsShortNames.size());
        
        fmt = av_guess_format(formatsShortNames[formatValue].c_str(), NULL, NULL);
        if (!fmt) {
            setPersistentMessage(OFX::Message::eMessageError, "","Invalid file extension");
            throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }
    
#     if defined(FFMS_USE_FFMPEG_COMPAT) && LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(54, 0, 0)
    avformat_alloc_output_context2(&_formatContext, fmt, NULL, filename.c_str());
#     else
#       ifdef FFMS_USE_FFMPEG_COMPAT
    _formatContext = avformat_alloc_output_context(NULL, fmt, filename.c_str());
#       else
    _formatContext = avformat_alloc_context();
    _formatContext->oformat = fmt;
#       endif
#     endif
    
    /////////////////////                            ////////////////////
    ////////////////////    INITIALISE STREAM     ////////////////////
    
    AVCodecID codecId = fmt->video_codec;
    int codecValue;
    _codec->getValue(codecValue);
    if (codecValue != 0) {
        const std::vector<std::string>& codecShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
        assert(codecValue < (int)codecShortNames.size());
        AVCodec* userCodec = avcodec_find_encoder_by_name(codecShortNames[codecValue].c_str());
        if (userCodec) {
            codecId = userCodec->id;
        }
    }
    
    AVCodec* videoCodec = avcodec_find_encoder(codecId);
    if (!videoCodec) {
        setPersistentMessage(OFX::Message::eMessageError, "","Unable to find codec");
        freeFormat();
        return;
    }
    
    PixelFormat pixFMT = PIX_FMT_YUV420P;
    
    if (videoCodec->pix_fmts != NULL) {
        pixFMT = *videoCodec->pix_fmts;
    }
    else {
        if (strcmp(fmt->name, "gif") == 0) {
            pixFMT = PIX_FMT_RGB24;
        }
    }

    
    bool isCodecSupportedInContainer = (avformat_query_codec(fmt, codecId, FF_COMPLIANCE_NORMAL) == 1);
    // mov seems to be able to cope with anything, which the above function doesn't seem to think is the case (even with FF_COMPLIANCE_EXPERIMENTAL)
    // and it doesn't return -1 for this case, so we'll special-case this situation to allow this
    isCodecSupportedInContainer |= (strcmp(_formatContext->oformat->name, "mov") == 0);
    
    if (!isCodecSupportedInContainer) {
        setPersistentMessage(OFX::Message::eMessageError, "","The selected codec is not supported in this container.");
        freeFormat();
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    
    
    assert(!_stream);
    _stream = avformat_new_stream(_formatContext, NULL);
    if (!_stream) {
        setPersistentMessage(OFX::Message::eMessageError,"" ,"Out of memory");
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    _codecContext = _stream->codec;
    
    // this seems to be needed for certain codecs, as otherwise they don't have relevant options set
    avcodec_get_context_defaults3(_codecContext, videoCodec);
    
    _codecContext->pix_fmt = pixFMT;   // this is set to the first element of FMT a choice could be added
    
    int bitRateValue;
    _bitRate->getValue(bitRateValue);
    _codecContext->bit_rate = bitRateValue;
    
    int bitRateTol;
    _bitRateTolerance->getValue(bitRateTol);
    _codecContext->bit_rate_tolerance = bitRateTol;
    
    
    _codecContext->width = (rod.x2 - rod.x1);
    _codecContext->height = (rod.y2 - rod.y1);
    
    // Bug 23953
    // ffmpeg does a horrible job of converting floats to AVRationals
    // It adds 0.5 randomly and does some other stuff
    // To work around that, we just multiply the fps by what I think is a reasonable number to make it an int
    // and use the reasonable number as the numerator for the timebase.
    // Timebase is not the frame rate; it's the inverse of the framerate
    // So instead of doing 1/fps, we just set the numerator and denominator of the timebase directly.
    // The upshot is that this allows ffmpeg to properly do framerates of 23.78 (or 23.796, which is what the user really wants when they put that in).
    //
    // The code was this:
    //stream_->codec->time_base = av_d2q(1.0 / fps_, 100);
    const float CONVERSION_FACTOR = 1000.0f;
    _codecContext->time_base.num = (int) CONVERSION_FACTOR;
    
    double fps;
    _fps->getValue(fps);
    _codecContext->time_base.den = (int) (fps * CONVERSION_FACTOR);
    
    int gopSize;
    _gopSize->getValue(gopSize);
    _codecContext->gop_size = gopSize;
    
    int bFrames;
    _bFrames->getValue(bFrames);
    if (bFrames != 0) {
        _codecContext->max_b_frames = bFrames;
        _codecContext->b_frame_strategy = 0;
        _codecContext->b_quant_factor = 2.0f;
    }
    
    int mbDecision;
    _macroBlockDecision->getValue(mbDecision);
    _codecContext->mb_decision = mbDecision;
    
    if (!strcmp(_formatContext->oformat->name, "mp4") || !strcmp(_formatContext->oformat->name, "mov") || !strcmp(_formatContext->oformat->name, "3gp"))
        _codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
    
    if (_formatContext->oformat->flags & AVFMT_GLOBALHEADER)
        _codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
    
    if (_codecContext->codec_type == AVMEDIA_TYPE_VIDEO) {
        // source: http://git.savannah.gnu.org/cgit/bino.git/tree/src/media_object.cpp
        
        // Activate multithreaded decoding. This must be done before opening the codec; see
        // http://lists.gnu.org/archive/html/bino-list/2011-08/msg00019.html
        _codecContext->thread_count = video_decoding_threads();
        // Set CODEC_FLAG_EMU_EDGE in the same situations in which ffplay sets it.
        // I don't know what exactly this does, but it is necessary to fix the problem
        // described in this thread: http://lists.nongnu.org/archive/html/bino-list/2012-02/msg00039.html
        int lowres = 0;
#ifdef FF_API_LOWRES
        lowres = _codecContext->lowres;
#endif
        if (lowres || (videoCodec && (videoCodec->capabilities & CODEC_CAP_DR1)))
            _codecContext->flags |= CODEC_FLAG_EMU_EDGE;
    }
    if (avcodec_open2(_codecContext, videoCodec, NULL) < 0) {
        setPersistentMessage(OFX::Message::eMessageError,"" ,"Unable to open codec");
        freeFormat();
        throwSuiteStatusException(kOfxStatFailed);
    }
    
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&_formatContext->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
            setPersistentMessage(OFX::Message::eMessageError,"" ,"Unable to open file");
            freeFormat();
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    
    avformat_write_header(_formatContext, NULL);
    
    ///Flag that we didn't encode any frame yet
    _lastTimeEncoded = -1;
    
}


void WriteFFmpegPlugin::endEncode(const OFX::EndSequenceRenderArguments &args)
{
    if (!_formatContext) {
        return;
    }
    av_write_trailer(_formatContext);
    avcodec_close(_codecContext);
    if (!(_formatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_close(_formatContext->pb);
    }
    freeFormat();
}

#define checkAvError() if (error < 0) { \
                        char errorBuf[1024]; \
                        av_strerror(error, errorBuf, sizeof(errorBuf)); \
                        setPersistentMessage(OFX::Message::eMessageError, "", errorBuf); \
                        OFX::throwSuiteStatusException(kOfxStatFailed); \
                    }


void WriteFFmpegPlugin::encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB) {
        setPersistentMessage(OFX::Message::eMessageError, "", "FFmpeg: can only write RGBA or RGB components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    
    if (!_formatContext || (_formatContext && filename != std::string(_formatContext->filename))) {
        setPersistentMessage(OFX::Message::eMessageError, "", "FFmpeg: can only write files in sequential order");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    ///Check that we're really encoding in sequential order
    if (_lastTimeEncoded != -1 && _lastTimeEncoded != time -1 && _lastTimeEncoded != time + 1) {
        setPersistentMessage(OFX::Message::eMessageError, "", "FFmpeg: can only write files in sequential order");
        OFX::throwSuiteStatusException(kOfxStatFailed);

    }

    
    int numChannels = 0;
    switch(pixelComponents)
    {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            break;
        //case OFX::ePixelComponentAlpha:
        //    numChannels = 1;
        //    break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    assert(numChannels);

    snprintf(_formatContext->filename, sizeof(_formatContext->filename), "%s", filename.c_str());
    

    int w = (bounds.x2 - bounds.x1);
    int h = (bounds.y2 - bounds.y1);
    
    
    AVPicture picture;
    int error = avpicture_alloc(&picture, PIX_FMT_RGB24,w, h);
    checkAvError();


    for (int y = bounds.y1; y < bounds.y2; ++y) {
        int srcY = bounds.y2 - y - 1;
        const float* src_pixels = (float*)((char*)pixelData + (y-bounds.y1)*rowBytes);
        unsigned char* dst_pixels = picture.data[0] + (bounds.x2 - bounds.x1) * srcY * 3;
        
        for (int x = bounds.x1; x < bounds.x2; ++x) {
            int srcCol = x * numChannels;
            int dstCol = x * 3;
            dst_pixels[dstCol + 0] = floatToInt<256>(src_pixels[srcCol + 0]);
            dst_pixels[dstCol + 1] = floatToInt<256>(src_pixels[srcCol + 1]);
            dst_pixels[dstCol + 2] = floatToInt<256>(src_pixels[srcCol + 2]);
        }
    }

    
    // now allocate an image frame for the image in the output codec's format...
    AVFrame* output = av_frame_alloc();
    PixelFormat pixFMT = _codecContext->pix_fmt;
    
    error = av_image_alloc(output->data, output->linesize, w, h, pixFMT, 1);
    checkAvError();
    
    SwsContext* convertCtx = sws_getContext(w, h, PIX_FMT_RGB24,w, h,
                                            pixFMT, SWS_BICUBIC, NULL, NULL, NULL);
    
    int sliceHeight = sws_scale(convertCtx, picture.data, picture.linesize, 0, h, output->data, output->linesize);
    assert(sliceHeight > 0);
    
    if ((_formatContext->oformat->flags & AVFMT_RAWPICTURE) != 0) {
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = _stream->index;
        pkt.data = (uint8_t*)output;
        pkt.size = sizeof(AVPicture);
        error = av_interleaved_write_frame(_formatContext, &pkt);
    }
    else {
#if LIBAVCODEC_VERSION_INT<AV_VERSION_INT(54,1,0)
        size_t picSize = avpicture_get_size(pixFMT,w, h);
        uint8_t* outbuf = (uint8_t*)av_malloc(picSize);
        assert(outbuf != NULL);
        error = avcodec_encode_video(_codecContext, outbuf, picSize, output);
        checkAvError();
        
        AVPacket pkt;
        av_init_packet(&pkt);
        if (_codecContext->coded_frame && _codecContext->coded_frame->pts != (int64_t)AV_NOPTS_VALUE)
            pkt.pts = av_rescale_q(_codecContext->coded_frame->pts, _codecContext->time_base, _stream->time_base);
        if (_codecContext->coded_frame && _codecContext->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
        
        pkt.stream_index = _stream->index;
        pkt.data = outbuf;
        pkt.size = error;
        
        error = av_interleaved_write_frame(_formatContext, &pkt);
        av_free(outbuf);
#else
        AVPacket pkt;
        int got_packet;
        av_init_packet(&pkt);
        pkt.size = 0;
        pkt.data = NULL;
        pkt.stream_index = _stream->index;
        error = avcodec_encode_video2(_codecContext, &pkt, output, &got_packet);
        checkAvError();
        
        if (got_packet) {
            if (pkt.pts != AV_NOPTS_VALUE) {
                pkt.pts = av_rescale_q(pkt.pts, _codecContext->time_base, _stream->time_base);
            }
            if (pkt.dts != AV_NOPTS_VALUE) {
                pkt.dts = av_rescale_q(pkt.dts, _codecContext->time_base, _stream->time_base);
            }

            error = av_interleaved_write_frame(_formatContext, &pkt);
            av_free_packet(&pkt);
            checkAvError();
        }
#endif
    }
    
    avpicture_free(&picture);
    av_free(output);
    
    checkAvError();
    
    _lastTimeEncoded = time;
    
}

void WriteFFmpegPlugin::freeFormat() { 
    for (int i = 0; i < static_cast<int>(_formatContext->nb_streams); ++i){
        av_freep(&_formatContext->streams[i]);
    }
    av_free(_formatContext);
    _formatContext = NULL;
    _stream = NULL;
}


using namespace OFX;

mDeclareWriterPluginFactory(WriteFFmpegPluginFactory, {}, {}, true);

static std::string ffmpeg_versions()
{
    std::ostringstream oss;
#ifdef FFMS_USE_FFMPEG_COMPAT
    oss << "FFmpeg ";
#else
    oss << "libav";
#endif
    oss << " versions (compiled with / running with):" << std::endl;
    oss << "libavformat ";
    oss << LIBAVFORMAT_VERSION_MAJOR << '.' << LIBAVFORMAT_VERSION_MINOR << '.' << LIBAVFORMAT_VERSION_MICRO << " / ";
    oss << (avformat_version() >> 16) << '.' << (avformat_version() >> 8 & 0xff) << '.' << (avformat_version() & 0xff) << std::endl;
    //oss << "libavdevice ";
    //oss << LIBAVDEVICE_VERSION_MAJOR << '.' << LIBAVDEVICE_VERSION_MINOR << '.' << LIBAVDEVICE_VERSION_MICRO << " / ";
    //oss << avdevice_version() >> 16 << '.' << avdevice_version() >> 8 & 0xff << '.' << avdevice_version() & 0xff << std::endl;
    oss << "libavcodec ";
    oss << LIBAVCODEC_VERSION_MAJOR << '.' << LIBAVCODEC_VERSION_MINOR << '.' << LIBAVCODEC_VERSION_MICRO << " / ";
    oss << (avcodec_version() >> 16) << '.' << (avcodec_version() >> 8 & 0xff) << '.' << (avcodec_version() & 0xff) << std::endl;
    oss << "libavutil ";
    oss << LIBAVUTIL_VERSION_MAJOR << '.' << LIBAVUTIL_VERSION_MINOR << '.' << LIBAVUTIL_VERSION_MICRO << " / ";
    oss << (avutil_version() >> 16) << '.' << (avutil_version() >> 8 & 0xff) << '.' << (avutil_version() & 0xff) << std::endl;
    oss << "libswscale ";
    oss << LIBSWSCALE_VERSION_MAJOR << '.' << LIBSWSCALE_VERSION_MINOR << '.' << LIBSWSCALE_VERSION_MICRO << " / ";
    oss << (swscale_version() >> 16) << '.' << (swscale_version() >> 8 & 0xff) << '.' << (swscale_version() & 0xff) << std::endl;
    return oss.str();
}

/** @brief The basic describe function, passed a plugin descriptor */
void WriteFFmpegPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc,OFX::eRenderInstanceSafe);
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginDescription("Write images or video file using "
#                             ifdef FFMS_USE_FFMPEG_COMPAT
                              "FFmpeg"
#                             else
                              "libav"
#                             endif
                              ".\n\n" + ffmpeg_versions());

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "avi", "flv", "mov", "mp4", "mkv", "bmp", "pix", "dpx", "jpeg", "jpg", "png", "pgm", "ppm", "rgba", "rgb", "tiff", "tga", "gif", NULL };
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(0);
#endif

    ///We support only a single render call per instance
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    
    ///check that the host supports sequential render
    
    ///This plug-in only supports sequential render
    int hostSequentialRender = OFX::getImageEffectHostDescription()->sequentialRender;
    if (hostSequentialRender == 1 || hostSequentialRender == 2) {
        desc.getPropertySet().propSetInt(kOfxImageEffectInstancePropSequentialRender, 1);
    }
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WriteFFmpegPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    
    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha,
                                                                    "reference", "rec709");

    ///If the host doesn't support sequential render, fail.
    int hostSequentialRender = OFX::getImageEffectHostDescription()->sequentialRender;
    if (hostSequentialRender == 0) {
        throwSuiteStatusException(kOfxStatErrMissingHostFeature);
    }

    
    ///////////Output format
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
        const std::vector<std::string>& formatsV = FFmpegSingleton::Instance().getFormatsLongNames();
        param->setLabels("Format", "Format", "Format");
        param->setHint("The outputformat");
        for (unsigned int i = 0; i < formatsV.size(); ++i) {
            param->appendOption(formatsV[i],"");

        }
        param->setAnimates(false);
        param->setDefault(0);
        page->addChild(*param);
    }

    ///////////FPS
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFPS);
        param->setLabels("fps", "fps", "fps");
        param->setRange(0.f, 100.f);
        param->setDefault(24.f);
        param->setAnimates(false);
        page->addChild(*param);
    }

    /////////// Advanced group
    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kParamAdvanced);
        group->setLabels("Advanced", "Advanced", "Advanced");
        group->setOpen(false);
        page->addChild(*group);

        ///////////Codec
        {
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamCodec);
            param->setLabels("Codec","Codec","Codec");
            const std::vector<std::string>& codecsV = FFmpegSingleton::Instance().getCodecsLongNames();
            for (unsigned int i = 0; i < codecsV.size(); ++i) {
                param->appendOption(codecsV[i],"");
            }
            param->setAnimates(false);
            param->setParent(*group);
            param->setDefault(0);
            page->addChild(*param);
        }

        ///////////bit-rate
        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBitRate);
            param->setLabels("Bitrate", "Bitrate", "Bitrate");
            param->setRange(0, 400000);
            param->setDefault(400000);
            param->setParent(*group);
            param->setAnimates(false);
            page->addChild(*param);
        }

        ///////////bit-rate tolerance
        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBitRateTolerance);
            param->setLabels("Bitrate tolerance", "Bitrate tolerance", "Bitrate tolerance");
            param->setRange(0, 4000 * 10000);
            param->setDefault(4000 * 10000);
            param->setParent(*group);
            param->setAnimates(false);
            page->addChild(*param);
        }

        ///////////Gop size
        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamGop);
            param->setLabels("GOP Size", "GOP Size", "GOP Size");
            param->setRange(0, 30);
            param->setDefault(12);
            param->setParent(*group);
            param->setAnimates(false);
            page->addChild(*param);
        }

        ////////////B Frames
        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBFrames);
            param->setLabels("B Frames", "B Frames", "B Frames");
            param->setRange(0, 30);
            param->setDefault(0);
            param->setParent(*group);
            param->setAnimates(false);
            page->addChild(*param);
        }

        ////////////Macro block decision
        {
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMBDecision);
            param->setLabels("Macro block decision mode", "Macro block decision mode", "Macro block decision mode");
            param->appendOption("FF_MB_DECISION_SIMPLE");
            param->appendOption("FF_MB_DECISION_BITS");
            param->appendOption("FF_MB_DECISION_RD");
            param->setDefault(FF_MB_DECISION_SIMPLE);
            param->setParent(*group);
            param->setAnimates(false);
            page->addChild(*param);
        }
    }
    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteFFmpegPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WriteFFmpegPlugin(handle);
}


void getWriteFFmpegPluginID(OFX::PluginFactoryArray &ids)
{
    static WriteFFmpegPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

