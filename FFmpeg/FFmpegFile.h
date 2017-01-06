/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2013-2017 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX ffmpeg Reader plugin.
 * Reads a video input file using the libav library.
 */

#ifndef __Io__FFmpegHandler__
#define __Io__FFmpegHandler__

#if (defined(_STDINT_H) || defined(_STDINT_H_) || defined(_MSC_STDINT_H_ ) ) && !defined(UINT64_C)
#warning "__STDC_CONSTANT_MACROS has to be defined before including <stdint.h>, this file will probably not compile."
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // ...or stdint.h wont' define UINT64_C, needed by libavutil
#endif

#include <vector>
#include <string>
#include <map>
#include <list>
#include <algorithm>
#include <locale>
#include <cstdio>
extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}
#include "FFmpegCompat.h"

#include "ofxsMultiThread.h"
#ifndef OFX_USE_MULTITHREAD_MUTEX
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
#endif

#define CHECKMSG(x, msg) \
    { \
        int error = (x); \
        if (error < 0) { \
            setError( (msg), "" ); \
            close(); \
            return; \
        } \
    }

//Used in ffmpeg metadata dicts to stash values written to and from nclc atom.
#define kNCLCPrimariesKey "fn_primaries"
#define kNCLCTransferKey "fn_transfer_function"
#define kNCLCMatrixKey "fn_matrix"
#define kNCLCUnknownLabel "Unknown"
#define kNCLCReservedLabel "Reserved"

// Avid DNxHD specific. Label for switching between video legal and full range.
#define kACLRYuvRange "fn_aclr_yuv_range"

// metadata keys used by Nuke
#define kMetaKeyApplication        "uk.co.thefoundry.Application"
#define kMetaKeyApplicationVersion "uk.co.thefoundry.ApplicationVersion"
#define kMetaKeyYCbCrMatrix        "uk.co.thefoundry.YCbCrMatrix"
#define kMetaKeyPixelFormat        "uk.co.thefoundry.PixelFormat"
#define kMetaKeyColorspace         "uk.co.thefoundry.Colorspace"
#define kMetaKeyWriter             "uk.co.thefoundry.Writer"
#define kMetaValueWriter64         "mov64"

#define OFX_FFMPEG_MAX_THREADS 32 // defined in libavcodec/mpegvideo.h and libavcodec/h264.h

////////////////////////////////////////////////////////////////////////////////
// Chunksize static names.
////////////////////////////////////////////////////////////////////////////////

#define kChunkSizeKey "fn_log2chunksize"

namespace OFX {
class ImageEffect;
}

class FFmpegFile
{
public:
#ifdef OFX_USE_MULTITHREAD_MUTEX
    typedef OFX::MultiThread::Mutex Mutex;
    typedef OFX::MultiThread::AutoMutex AutoMutex;
#else
    typedef tthread::fast_mutex Mutex;
    typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
#endif

private:
    struct Stream
    {
        int _idx;                      // stream index
        AVStream* _avstream;           // video stream
        AVCodecContext* _codecContext; // video codec context
        AVCodec* _videoCodec;
        AVFrame* _avFrame;             // decoding frame
        SwsContext* _convertCtx;
        bool _resetConvertCtx;
        int _fpsNum;
        int _fpsDen;
        int64_t _startPTS;     // PTS of the first frame in the stream
        int64_t _frames;       // video duration in frames
        bool _ptsSeen;                      // True if a read AVPacket has ever contained a valid PTS during this stream's decode,
        // indicating that this stream does contain PTSs.
        int64_t AVPacket::*_timestampField; // Pointer to member of AVPacket from which timestamps are to be retrieved. Enables
        // fallback to using DTSs for a stream if PTSs turn out not to be available.
        int _width;
        int _height;
        double _aspect;
        int _bitDepth;
        int _numberOfComponents;
        enum AVPixelFormat _outputPixelFormat;
        uint8_t _componentPosition[4];
        int _colorMatrixTypeOverride;                       // Option to override the default YCbCr color matrix. 0 means no override (default).
        bool _doNotAttachPrefix;
        bool _matchMetaFormat;
        int _decodeNextFrameIn; // The 0-based index of the next frame to be fed into decode. Negative before any
        // frames have been decoded or when we've just seeked but not yet found a relevant frame. Equal to
        // frames_ when all available frames have been fed into decode.
        int _decodeNextFrameOut; // The 0-based index of the next frame expected out of decode. Negative before
        // any frames have been decoded or when we've just seeked but not yet found a relevant frame. Equal to
        // frames_ when all available frames have been output from decode.
        int _accumDecodeLatency; // The number of frames that have been input without any frame being output so far in this stream
        // since the last seek. This is part of a guard mechanism to detect when decode appears to have
        // stalled and ensure that FFmpegFile::decode() does not loop indefinitely.

        Stream()
            : _idx(0)
            , _avstream(NULL)
            , _codecContext(NULL)
            , _videoCodec(NULL)
            , _avFrame(NULL)
            , _convertCtx(NULL)
            , _resetConvertCtx(true)
            , _fpsNum(1)
            , _fpsDen(1)
            , _startPTS(0)
            , _frames(0)
            , _ptsSeen(false)
            , _timestampField(&AVPacket::pts)
            , _width(0)
            , _height(0)
            , _aspect(1.0)
            , _bitDepth(8)
            , _numberOfComponents(3)
            , _outputPixelFormat(AV_PIX_FMT_RGB24)
            , _colorMatrixTypeOverride(0)
            , _doNotAttachPrefix(true)
            , _matchMetaFormat(true)
            , _decodeNextFrameIn(-1)
            , _decodeNextFrameOut(-1)
            , _accumDecodeLatency(0)
        {
            // The purpose of this is to avoid an RGB->RGB conversion.
            // This saves memory and improves performance. For example
            // Nuke expects RGBA component order. So for any RGB pixel
            // format which does not have this ordering, the following
            // should be used to remap the components, e.g. ARGB->RGBA,
            // BGRA->RGBA, BGR->RGB, etc. The output of the DNxHD decoder
            // is an example of this in action.
            _componentPosition[0] = 0; // Red position.
            _componentPosition[1] = 1; // Green position.
            _componentPosition[2] = 2; // Blue position.
            _componentPosition[3] = 3; // Alpha position.
        }

        ~Stream()
        {
            if (_avFrame) {
                av_free(_avFrame);
            }

            if (_codecContext) {
                avcodec_flush_buffers(_codecContext);
                avcodec_free_context(&_codecContext);
            }

            if (_convertCtx) {
                sws_freeContext(_convertCtx);
            }
        }

        static void destroy(Stream* s)
        {
            delete(s);
        }

        int64_t frameToPts(int frame) const
        {
            int64_t numerator = int64_t(frame) * _fpsDen *  _avstream->time_base.den;
            int64_t denominator = int64_t(_fpsNum) * _avstream->time_base.num;

            // guard against division by zero
            assert(denominator);

            return _startPTS + denominator ? (numerator / denominator) : numerator;
        }

        int ptsToFrame(int64_t pts) const
        {
            int64_t numerator = int64_t(pts - _startPTS) * _avstream->time_base.num * _fpsNum;
            int64_t denominator = int64_t(_avstream->time_base.den) * _fpsDen;

            // guard against division by zero
            assert(denominator);

            return static_cast<int>(denominator ? (numerator / denominator) : numerator);
        }

        bool isRec709Format()
        {
            // First check for codecs which require special handling:
            //  * JPEG codecs always use Rec 601.
            AVCodecID codecID = _codecContext->codec_id;

            if (codecID == AV_CODEC_ID_MJPEG) {
                return false;
            }

            // Using method described in step 5 of QuickTimeCodecReader::setPreferredMetadata
            return (_height >= 720);
        }

        bool isYUV() const
        {
            // from swscale_internal.h
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(_codecContext->pix_fmt);

            return !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 2;
        }

        static double GetStreamAspectRatio(Stream* stream);

        // Generate the conversion context used by SoftWareScaler if not already set.
        // |reset| forces recalculation of cached context.
        SwsContext* getConvertCtx(AVPixelFormat srcPixelFormat, int srcWidth, int srcHeight, int srcColorRange, AVPixelFormat dstPixelFormat, int dstWidth, int dstHeight);

        // Return the number of input frames needed by this stream's codec before it can produce output. We expect to have to
        // wait this many frames to receive output; any more and a decode stall is detected.
        // In FFmpeg 2.1.4, I found that some codecs now support multithreaded decode which appears as latency
        // I needed to add the thread_count onto the codec delay - pickles
        int getCodecDelay() const
        {
            return ( ( (_videoCodec->capabilities & CODEC_CAP_DELAY) ? _codecContext->delay : 0 )
                     + _codecContext->has_b_frames
                     + _codecContext->thread_count );
        }
    };

    std::string _filename;

    // AV structure
    AVFormatContext* _context;
    AVInputFormat*   _format;

    // store all video streams available in the file
    std::vector<Stream*> _streams;

    // reader error state
    std::string _errorMsg;  // internal decoding error string
    bool _invalidState;     // true if the reader is in an invalid state
    AVPacket _avPacket;

#ifdef OFX_IO_MT_FFMPEG
    // internal lock for multithread access
    mutable Mutex _lock;
    mutable Mutex _invalidStateLock;
#endif

    // set reader error
    void setError(const char* msg, const char* prefix = 0);

    // set FFmpeg library error
    void setInternalError(const int error,
                          const char* prefix = 0)
    {
        char errorBuf[1024];

        av_strerror( error, errorBuf, sizeof(errorBuf) );
        setError(errorBuf, prefix);
    }

    // get stream start time
    int64_t getStreamStartTime(Stream& stream);

    // Get the video stream duration in frames...
    int64_t getStreamFrames(Stream& stream);

    bool seekFrame(int frame, Stream* stream);

public:

    //FFmpegFile();

    // constructor
    FFmpegFile(const std::string& filename);

    // destructor
    ~FFmpegFile();

    const std::string& getFilename() const
    {
        return _filename;
    }

    // get the internal error string
    const std::string& getError() const;

    // return true if the reader can't decode the frame
    bool isInvalid() const;

    // return the numbers of streams supported by the reader
    unsigned int getNbStreams() const
    {
        return _streams.size();
    }

    void setColorMatrixTypeOverride(int colorMatrixType) const
    {
        if ( _streams.empty() ) {
            return;
        }

        // mov64Reader::decode always uses stream 0
        Stream* stream = _streams[0];
        stream->_colorMatrixTypeOverride = colorMatrixType;
        stream->_resetConvertCtx = true;
    }

    void setDoNotAttachPrefix(bool doNotAttachPrefix) const
    {
        if ( _streams.empty() ) {
            return;
        }

        // mov64Reader::decode always uses stream 0
        Stream* stream = _streams[0];
        stream->_doNotAttachPrefix = doNotAttachPrefix;
    }

    void setMatchMetaFormat(bool matchMetaFormat) const
    {
        if ( _streams.empty() ) {
            return;
        }

        // mov64Reader::decode always uses stream 0
        Stream* stream = _streams[0];
        stream->_matchMetaFormat = matchMetaFormat;
    }

    bool isRec709Format() const
    {
        if ( _streams.empty() ) {
            return false;
        }

        // mov64Reader::decode always uses stream 0
        Stream* stream = _streams[0];

        return stream->isRec709Format();
    }

    bool isYUV() const
    {
        if ( _streams.empty() ) {
            return false;
        }

        // mov64Reader::decode always uses stream 0
        Stream* stream = _streams[0];

        return stream->isYUV();
    }

    int getBitDepth() const
    {
        if ( _streams.empty() ) {
            return 0;
        }

        // mov64Reader::decode always uses stream 0
        // Sometimes AVCodec reports a bitdepth of 0 (eg PNG codec).

        // In this case, assume 8 bit.
        return std::max(8, _streams[0]->_bitDepth);
    }

    int getNumberOfComponents() const
    {
        if ( _streams.empty() ) {
            return 0;
        }

        // mov64Reader::decode always uses stream 0
        return _streams[0]->_numberOfComponents;
    }

    int getComponentPosition(int componentIndex) const
    {
        if ( _streams.empty() ) {
            return 0;
        }

        // mov64Reader::decode always uses stream 0
        return _streams[0]->_componentPosition[componentIndex];
    }

    int getWidth() const
    {
        if ( _streams.empty() ) {
            return 0;
        }

        // mov64Reader::decode always uses stream 0
        return _streams[0]->_width;
    }

    int getHeight() const
    {
        if ( _streams.empty() ) {
            return 0;
        }

        // mov64Reader::decode always uses stream 0
        return _streams[0]->_height;
    }

    std::size_t getSizeOfData() const
    {
        if ( _streams.empty() ) {
            return 0;
        }

        return _streams[0]->_bitDepth > 8 ? sizeof(unsigned short) : sizeof(unsigned char);
    }

    // decode a single frame into the buffer (stream 0). Thread safe
    bool decode(const OFX::ImageEffect* plugin, int frame, bool loadNearest, int maxRetries, unsigned char* buffer);

    // get stream information
    bool getFPS(double& fps,
                unsigned streamIdx = 0);

    // get stream information
    bool getInfo(int& width,
                 int& height,
                 double& aspect,
                 int& frames,
                 unsigned streamIdx = 0);

    const char* getColorspace() const;

    std::size_t getBufferBytesCount() const;

    static bool isImageFile(const std::string& filename);

    //! Check whether a named container format is Whitelisted
    static bool isFormatWhitelistedForReading(const char* name);
    static bool isFormatWhitelistedForWriting(const char* name);

    //! Check whether a certain codec name is Whitelisted
    static bool isCodecWhitelistedForReading(const char* name);
    static bool isCodecWhitelistedForWriting(const char* name);
};


class FFmpegFileManager
{
    ///For each plug-in instance, a list of opened files
    typedef std::map<void const *, std::list<FFmpegFile*> > FilesMap;
    mutable FilesMap _files;
    mutable FFmpegFile::Mutex* _lock;

public:

    FFmpegFileManager();

    ~FFmpegFileManager();

    void init();

    void clear(void const * plugin);

    FFmpegFile* get(void const * plugin, const std::string &filename) const;
    FFmpegFile* getOrCreate(void const * plugin, const std::string &filename) const;
};


#endif /* defined(__Io__FFmpegHandler__) */
