/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2015 INRIA
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

#if (defined(_STDINT_H) || defined(_STDINT_H_) || defined(_MSC_STDINT_H_ ) ) && !defined(UINT64_C)
#warning "__STDC_CONSTANT_MACROS has to be defined before including <stdint.h>, this file will probably not compile."
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // ...or stdint.h wont' define UINT64_C, needed by libavutil
#endif
#include "FFmpegFile.h"

#include <cmath>
#include <iostream>
#include <algorithm>

#include <ofxsImageEffect.h>

#if defined(_WIN32) || defined(WIN64)
#  include <windows.h> // for GetSystemInfo()
#define strncasecmp _strnicmp
#else
#  include <unistd.h> // for sysconf()
#endif

using namespace OFX;

using std::string;
using std::make_pair;

// FFMPEG 3.1
#define USE_NEW_FFMPEG_API ( LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 0) )

#define CHECK(x) \
    { \
        int error = x; \
        if (error < 0) { \
            setInternalError(error); \
            return; \
        } \
    } \

//#define TRACE_DECODE_PROCESS 1
//#define TRACE_FILE_OPEN 1

// Use one decoding thread per processor for video decoding.
// source: http://git.savannah.gnu.org/cgit/bino.git/tree/src/media_object.cpp
#if 0
static int
video_decoding_threads()
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

#endif

static bool
extensionCorrespondToImageFile(const string & ext)
{
    return (ext == "bmp" ||
            ext == "cin" ||
            ext == "dpx" ||
            ext == "exr" ||
            ext == "gif" ||
            ext == "jpeg" ||
            ext == "jpg" ||
            ext == "pix" ||
            ext == "png" ||
            ext == "ppm" ||
            ext == "ptx" ||
            ext == "rgb" ||
            ext == "rgba" ||
            ext == "tga" ||
            ext == "tiff" ||
            ext == "webp");
}

bool
FFmpegFile::isImageFile(const string & filename)
{
    ///find the last index of the '.' character
    size_t lastDot = filename.find_last_of('.');

    if (lastDot == string::npos) { //we reached the start of the file, return false because we can't determine from the extension
        return false;
    }
    ++lastDot;//< bypass the '.' character
    string ext;
    std::locale loc;
    while ( lastDot < filename.size() ) {
        ext.append( 1, std::tolower(filename.at(lastDot), loc) );
        ++lastDot;
    }

    return extensionCorrespondToImageFile(ext);
}

namespace {
struct FilterEntry
{
    const char* name;
    bool enableReader;
    bool enableWriter;
};

// Bug 11027 - Nuke write: ffmpeg codec fails has details on individual codecs

// For a full list of formats, define FN_FFMPEGWRITER_PRINT_CODECS in ffmpegWriter.cpp
const FilterEntry kFormatWhitelist[] =
{
    { "3gp",            true,  true },
    { "3g2",            true,  true },
    { "avi",            true,  true },
    { "flv",            true,  false },     // only used with flv codec, which doesn't play in Qt anyway
    { "h264",           true,  true },
    { "hevc",           true,  false },     // hevc codec cannot be read in official qt
    { "m4v",            true,  true },
    { "matroska",       true,  true },     // not readable in Qt but may be used with other software
    { "mov",            true,  true },
    { "mp4",            true,  true },
    { "mpeg",           true,  true },
    { "mpegts",         true,  true },
    { "mxf",            true,  true },     // not readable in Qt but may be used with other software
    { NULL, false, false}
};

// For a full list of formats, define FN_FFMPEGWRITER_PRINT_CODECS in ffmpegWriter.cpp
// A range of codecs are omitted for licensing reasons, or because they support obselete/unnecessary
// formats that confuse the interface.

#define UNSAFEQT0 true // set to true: not really harmful
#define UNSAFEQT false // set to false: we care about QuickTime, because it is used widely - mainly colorshift issues
#define UNSAFEVLC true // set to true: we don't care much about being playable in VLC
#define TERRIBLE false
//#define SHOULDWORK true
#define SHOULDWORK false
const FilterEntry kCodecWhitelist[] =
{
    // Video codecs.
    { "aic",            true,  false },     // Apple Intermediate Codec (no encoder)
    { "avrp",           true,  UNSAFEQT0 && UNSAFEVLC },     // Avid 1:1 10-bit RGB Packer - write not supported as not official qt readable with relevant 3rd party codec.
    { "avui",           true,  false },     // Avid Meridien Uncompressed - write not supported as this is an SD only codec. Only 720x486 and 720x576 are supported. experimental in ffmpeg 2.6.1.
    { "ayuv",           true,  UNSAFEQT0 && UNSAFEVLC },     // Uncompressed packed MS 4:4:4:4 - write not supported as not official qt readable.
    { "cfhd",           true,  false },     // Cineform HD.
    { "cinepak",        true,  true },     // Cinepak.
    { "dxv",            true,  false },     // Resolume DXV
    { "dnxhd",          true,  true },     // VC3/DNxHD
    { "ffv1",           true,  UNSAFEQT0 && UNSAFEVLC },     // FFmpeg video codec #1 - write not supported as not official qt readable.
    { "ffvhuff",        true,  UNSAFEQT0 && UNSAFEVLC },     // Huffyuv FFmpeg variant - write not supported as not official qt readable.
    { "flv",            true,  UNSAFEQT0 },     // FLV / Sorenson Spark / Sorenson H.263 (Flash Video) - write not supported as not official qt readable.
    { "gif",            true,  false },     // GIF (Graphics Interchange Format) - write not supported as 8-bit only.
    { "h264",           true,  false },     // H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (the encoder is libx264)
    { "hevc",           true,  false },     // H.265 / HEVC (High Efficiency Video Coding) (the encoder is libx265)
    { "huffyuv",        true,  UNSAFEQT0 && UNSAFEVLC },     // HuffYUV - write not supported as not official qt readable.
    { "jpeg2000",       true,  UNSAFEQT0 },     // JPEG 2000 - write not supported as not official qt readable.
    { "jpegls",         true,  UNSAFEQT0 },     // JPEG-LS - write not supported as can't be read in in official qt.
    { "libopenh264",    true,  true },     // Cisco libopenh264 H.264/MPEG-4 AVC encoder
    { "libschroedinger", true,  UNSAFEQT0 && UNSAFEVLC },     // libschroedinger Dirac - write untested. VLC plays with a wrong format
    { "libtheora",      true,  UNSAFEQT },     // libtheora Theora - write untested.
    { "libvpx",         true,  UNSAFEQT0 },     // On2 VP8
    { "libvpx-vp9",     true,  UNSAFEQT0 && TERRIBLE },     // Google VP9 -write not supported as it looks terrible (as of libvpx 1.4.0)
    { "libx264",        true,  UNSAFEQT0 },     // H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (encoder)
    { "libx264rgb",     true,  UNSAFEQT0 },     // H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 RGB (encoder)
    { "libx265",        true,  UNSAFEQT0 },     // H.265 / HEVC (High Efficiency Video Coding) (encoder) - resizes the image
    { "ljpeg",          true,  UNSAFEQT0 },     // Lossless JPEG - write not supported as can't be read in in official qt.
    { "mjpeg",          true,  true },     // MJPEG (Motion JPEG) - this looks to be MJPEG-A. MJPEG-B encoding is not supported by FFmpeg so is not included here. To avoid confusion over the MJPEG-A and MJPEG-B variants, this codec is displayed as 'Photo JPEG'. This is done to i) avoid the confusion of the naming, ii) be consistent with Apple QuickTime, iii) the term 'Photo JPEG' is recommend for progressive frames which is appropriate to Nuke/NukeStudio as it does not have interlaced support.
    { "mpeg1video",     true,  TERRIBLE },     // MPEG-1 video - write not supported as it gives random 8x8 blocky errors
    { "mpeg2video",     true,  true },     // MPEG-2 video
    { "mpeg4",          true,  true },     // MPEG-4 part 2
    { "msmpeg4v2",      true,  UNSAFEQT0 },     // MPEG-4 part 2 Microsoft variant version 2 - write not supported as doesn't read in official qt.
    { "msmpeg4",        true,  UNSAFEQT0 },     // MPEG-4 part 2 Microsoft variant version 3 - write not supported as doesn't read in official qt.
    { "png",            true,  true },     // PNG (Portable Network Graphics) image
    { "prores",         true,  false },     // Apple ProRes (the encoder is prores_ks)
    { "qtrle",          true,  true },     // QuickTime Animation (RLE) video
    { "r10k",           true,  UNSAFEQT && UNSAFEVLC },     // AJA Kono 10-bit RGB - write not supported as not official qt readable without colourshifts.
    { "r210",           true,  UNSAFEQT && UNSAFEVLC },     // Uncompressed RGB 10-bit - write not supported as not official qt readable with relevant 3rd party codec without colourshifts.
    { "rawvideo",       true,  UNSAFEQT0 && UNSAFEVLC },     // Uncompressed 4:2:2 8-bit - write not supported as not official qt readable.
    { "svq1",           true,  true },     // Sorenson Vector Quantizer 1 / Sorenson Video 1 / SVQ1
    { "targa",          true,  true },     // Truevision Targa image.
    { "theora",         true,  false },     // Theora (decoder).
    { "tiff",           true,  true },     // TIFF Image
    { "v210",           true,  UNSAFEQT },     // Uncompressed 4:2:2 10-bit- write not supported as not official qt readable without colourshifts.
    { "v308",           true,  UNSAFEQT0 && UNSAFEVLC },     // Uncompressed packed 4:4:4 - write not supported as not official qt readable and 8-bit only.
    { "v408",           true,  UNSAFEQT0 && UNSAFEVLC },     // Uncompressed packed QT 4:4:4:4 - write not supported as official qt can't write, so bad round trip choice and 8-bit only.
    { "v410",           true,  UNSAFEQT0 && UNSAFEVLC },     // Uncompressed 4:4:4 10-bit - write not supported as not official qt readable with standard codecs.
    { "vc2",            true,  UNSAFEQT0 && UNSAFEVLC },     // SMPTE VC-2 (previously BBC Dirac Pro).
    { "vp8",            true,  false },     // On2 VP8 (decoder)
    { "vp9",            true,  false },     // Google VP9 (decoder)

    // Audio codecs.
    { "pcm_alaw",       true,  true },     // PCM A-law / G.711 A-law
    { "pcm_f32be",      true,  true },     // PCM 32-bit floating point big-endian
    { "pcm_f32le",      true,  true },     // PCM 32-bit floating point little-endian
    { "pcm_f64be",      true,  true },     // PCM 64-bit floating point big-endian
    { "pcm_f64le",      true,  true },     // PCM 64-bit floating point little-endian
    { "pcm_mulaw",      true,  true },     // PCM mu-law / G.711 mu-law
    { "pcm_s16be",      true,  true },     // PCM signed 16-bit big-endian
    { "pcm_s16le",      true,  true },     // PCM signed 16-bit little-endian
    { "pcm_s24be",      true,  true },     // PCM signed 24-bit big-endian
    { "pcm_s24le",      true,  true },     // PCM signed 24-bit little-endian
    { "pcm_s32be",      true,  true },     // PCM signed 32-bit big-endian
    { "pcm_s32le",      true,  true },     // PCM signed 32-bit little-endian
    { "pcm_s8",         true,  true },     // PCM signed 8-bit
    { "pcm_u16be",      true,  true },     // PCM unsigned 16-bit big-endian
    { "pcm_u16le",      true,  true },     // PCM unsigned 16-bit little-endian
    { "pcm_u24be",      true,  true },     // PCM unsigned 24-bit big-endian
    { "pcm_u24le",      true,  true },     // PCM unsigned 24-bit little-endian
    { "pcm_u32be",      true,  true },     // PCM unsigned 32-bit big-endian
    { "pcm_u32le",      true,  true },     // PCM unsigned 32-bit little-endian
    { "pcm_u8",         true,  true },     // PCM unsigned 8-bit
    { NULL, false, false}
};

const FilterEntry*
getEntry(const char* name,
         const FilterEntry* whitelist,
         const FilterEntry* blacklist = NULL)
{
    const FilterEntry* iterWhitelist = whitelist;
    const size_t nameLength = strlen(name);

    // check for normal mode
    while (iterWhitelist->name != NULL) {
        size_t iteNameLength = strlen(iterWhitelist->name);
        size_t maxLength = (nameLength > iteNameLength) ? nameLength : iteNameLength;
        if (strncmp(name, iterWhitelist->name, maxLength) == 0) {
            // Found in whitelist, now check blacklist
            if (blacklist) {
                const FilterEntry* iterBlacklist = blacklist;

                while (iterBlacklist->name != NULL) {
                    iteNameLength = strlen(iterBlacklist->name);
                    maxLength = (nameLength > iteNameLength) ? nameLength : iteNameLength;
                    if (strncmp(name, iterBlacklist->name, maxLength) == 0) {
                        // Found in codec whitelist but blacklisted too
                        return NULL;
                    }

                    ++iterBlacklist;
                }
            }

            // Found in whitelist and not in blacklist
            return iterWhitelist;
        }

        ++iterWhitelist;
    }

    return NULL;
}
} // namespace {

bool
FFmpegFile::isFormatWhitelistedForReading(const char* formatName)
{
    const FilterEntry* whitelistEntry = getEntry(formatName, kFormatWhitelist);

    return (whitelistEntry && whitelistEntry->enableReader);
}

bool
FFmpegFile::isFormatWhitelistedForWriting(const char* formatName)
{
    const FilterEntry* whitelistEntry = getEntry(formatName, kFormatWhitelist);

    return (whitelistEntry && whitelistEntry->enableWriter);
}

bool
FFmpegFile::isCodecWhitelistedForReading(const char* codecName)
{
    const FilterEntry* whitelistEntry = getEntry(codecName, kCodecWhitelist);

    return (whitelistEntry && whitelistEntry->enableReader);
}

bool
FFmpegFile::isCodecWhitelistedForWriting(const char* codecName)
{
    const FilterEntry* whitelistEntry = getEntry(codecName, kCodecWhitelist);

    return (whitelistEntry && whitelistEntry->enableWriter);
}

SwsContext*
FFmpegFile::Stream::getConvertCtx(AVPixelFormat srcPixelFormat,
                                  int srcWidth,
                                  int srcHeight,
                                  int srcColorRange,
                                  AVPixelFormat dstPixelFormat,
                                  int dstWidth,
                                  int dstHeight)
{
    // Reset is flagged when the UI colour matrix selection is
    // modified. This causes a new convert context to be created
    // that reflects the UI selection.
    if (_resetConvertCtx) {
        _resetConvertCtx = false;
        if (_convertCtx) {
            sws_freeContext(_convertCtx);
            _convertCtx = NULL;
        }
    }

    if (!_convertCtx) {
        //Preventing deprecated pixel format used error messages, see:
        //https://libav.org/doxygen/master/pixfmt_8h.html#a9a8e335cf3be472042bc9f0cf80cd4c5
        //This manually sets them to the new versions of equivalent types.
        switch (srcPixelFormat) {
        case AV_PIX_FMT_YUVJ420P:
            srcPixelFormat = AV_PIX_FMT_YUV420P;
            if (srcColorRange == AVCOL_RANGE_UNSPECIFIED) {
                srcColorRange = AVCOL_RANGE_JPEG;
            }
            break;
        case AV_PIX_FMT_YUVJ422P:
            srcPixelFormat = AV_PIX_FMT_YUV422P;
            if (srcColorRange == AVCOL_RANGE_UNSPECIFIED) {
                srcColorRange = AVCOL_RANGE_JPEG;
            }
            break;
        case AV_PIX_FMT_YUVJ444P:
            srcPixelFormat = AV_PIX_FMT_YUV444P;
            if (srcColorRange == AVCOL_RANGE_UNSPECIFIED) {
                srcColorRange = AVCOL_RANGE_JPEG;
            }
            break;
        case AV_PIX_FMT_YUVJ440P:
            srcPixelFormat = AV_PIX_FMT_YUV440P;
            if (srcColorRange == AVCOL_RANGE_UNSPECIFIED) {
                srcColorRange = AVCOL_RANGE_JPEG;
            }
        default:
            break;
        }

        _convertCtx = sws_getContext(srcWidth, srcHeight, srcPixelFormat, // src format
                                     dstWidth, dstHeight, dstPixelFormat,        // dest format
                                     SWS_BICUBIC, NULL, NULL, NULL);

        // Set up the SoftWareScaler to convert colorspaces correctly.
        // Colorspace conversion makes no sense for RGB->RGB conversions
        if ( !isYUV() ) {
            return _convertCtx;
        }

        int colorspace = isRec709Format() ? SWS_CS_ITU709 : SWS_CS_ITU601;
        // Optional color space override
        if (_colorMatrixTypeOverride > 0) {
            if (_colorMatrixTypeOverride == 1) {
                colorspace = SWS_CS_ITU709;
            } else {
                colorspace = SWS_CS_ITU601;
            }
        }

        // sws_setColorspaceDetails takes a flag indicating the white-black range of the input:
        //     0  -  mpeg, 16..235
        //     1  -  jpeg,  0..255
        int srcRange;
        // Set this flag according to the color_range reported by the codec context.
        switch (srcColorRange) {
        case AVCOL_RANGE_MPEG:
            srcRange = 0;
            break;
        case AVCOL_RANGE_JPEG:
            srcRange = 1;
            break;
        case AVCOL_RANGE_UNSPECIFIED:
        default:
            // If the colour range wasn't specified, set the flag according to
            // whether the data is YUV or not.
            srcRange = isYUV() ? 0 : 1;
            break;
        }

        int result = sws_setColorspaceDetails(_convertCtx,
                                              sws_getCoefficients(colorspace), // inv_table
                                              srcRange, // srcRange -flag indicating the white-black range of the input (1=jpeg / 0=mpeg) 0 = 16..235, 1 = 0..255
                                              sws_getCoefficients(SWS_CS_DEFAULT), // table
                                              1, // dstRange - 0 = 16..235, 1 = 0..255
                                              0, // brightness fixed point, with 0 meaning no change,
                                              1 << 16, // contrast   fixed point, with 1<<16 meaning no change,
                                              1 << 16); // saturation fixed point, with 1<<16 meaning no change);

        assert(result != -1);
    }

    return _convertCtx;
} // FFmpegFile::Stream::getConvertCtx

/*static*/ double
FFmpegFile::Stream::GetStreamAspectRatio(Stream* stream)
{
    if (stream->_avstream->sample_aspect_ratio.num) {
#if TRACE_FILE_OPEN
        std::cout << "      Aspect ratio (from stream)=" << av_q2d(stream->_avstream->sample_aspect_ratio) << std::endl;
#endif

        return av_q2d(stream->_avstream->sample_aspect_ratio);
    } else if (stream->_codecContext->sample_aspect_ratio.num) {
#if TRACE_FILE_OPEN
        std::cout << "      Aspect ratio (from codec)=" << av_q2d(stream->_codecContext->sample_aspect_ratio) << std::endl;
#endif

        return av_q2d(stream->_codecContext->sample_aspect_ratio);
    }
#if TRACE_FILE_OPEN
    else {
        std::cout << "      Aspect ratio unspecified, assuming " << stream->_aspect << std::endl;
    }
#endif

    return stream->_aspect;
}

// get stream start time
int64_t
FFmpegFile::getStreamStartTime(Stream & stream)
{
#if TRACE_FILE_OPEN
    std::cout << "      Determining stream start PTS:" << std::endl;
#endif

    // Read from stream. If the value read isn't valid, get it from the first frame in the stream that provides such a
    // value.
    int64_t startPTS = stream._avstream->start_time;
#if TRACE_FILE_OPEN
    if ( startPTS != int64_t(AV_NOPTS_VALUE) ) {
        std::cout << "        Obtained from AVStream::start_time=";
    }
#endif

    if ( startPTS ==  int64_t(AV_NOPTS_VALUE) ) {
#if TRACE_FILE_OPEN
        std::cout << "        Not specified by AVStream::start_time, searching frames..." << std::endl;
#endif

        // Seek 1st key-frame in video stream.
        avcodec_flush_buffers(stream._codecContext);

        if (av_seek_frame(_context, stream._idx, 0, 0) >= 0) {
            av_init_packet(&_avPacket);

            // Read frames until we get one for the video stream that contains a valid PTS.
            do {
                if (av_read_frame(_context, &_avPacket) < 0) {
                    // Read error or EOF. Abort search for PTS.
#if TRACE_FILE_OPEN
                    std::cout << "          Read error, aborted search" << std::endl;
#endif
                    break;
                }
                if (_avPacket.stream_index == stream._idx) {
                    // Packet read for video stream. Get its PTS. Loop will continue if the PTS is AV_NOPTS_VALUE.
                    startPTS = _avPacket.pts;
                }

                av_packet_unref(&_avPacket);
            } while ( startPTS ==  int64_t(AV_NOPTS_VALUE) );
        }
#if TRACE_FILE_OPEN
        else {
            std::cout << "          Seek error, aborted search" << std::endl;
        }
#endif

#if TRACE_FILE_OPEN
        if ( startPTS != int64_t(AV_NOPTS_VALUE) ) {
            std::cout << "        Found by searching frames=";
        }
#endif
    }

    // If we still don't have a valid initial PTS, assume 0. (This really shouldn't happen for any real media file, as
    // it would make meaningful playback presentation timing and seeking impossible.)
    if ( startPTS ==  int64_t(AV_NOPTS_VALUE) ) {
#if TRACE_FILE_OPEN
        std::cout << "        Not found by searching frames, assuming ";
#endif
        startPTS = 0;
    }

#if TRACE_FILE_OPEN
    std::cout << startPTS << " ticks, " << double(startPTS) * double(stream._avstream->time_base.num) /
        double(stream._avstream->time_base.den) << " s" << std::endl;
#endif

    return startPTS;
} // FFmpegFile::getStreamStartTime

// Get the video stream duration in frames...
int64_t
FFmpegFile::getStreamFrames(Stream & stream)
{
#if TRACE_FILE_OPEN
    std::cout << "      Determining stream frame count:" << std::endl;
#endif

    int64_t frames = 0;

    // Obtain from movie duration if specified. This is preferred since mov/mp4 formats allow the media in
    // tracks (=streams) to be remapped in time to the final movie presentation without needing to recode the
    // underlying tracks content; the movie duration thus correctly describes the final presentation.
    if (_context->duration != 0) {
        // Annoyingly, FFmpeg exposes the movie duration converted (with round-to-nearest semantics) to units of
        // AV_TIME_BASE (microseconds in practice) and does not expose the original rational number duration
        // from a mov/mp4 file's "mvhd" atom/box. Accuracy may be lost in this conversion; a duration that was
        // an exact number of frames as a rational may end up as a duration slightly over or under that number
        // of frames in units of AV_TIME_BASE.
        // Conversion to whole frames rounds up the resulting number of frames because a partial frame is still
        // a frame. However, in an attempt to compensate for AVFormatContext's inaccurate representation of
        // duration, with unknown rounding direction, the conversion to frames subtracts 1 unit (microsecond)
        // from that duration. The rationale for this is thus:
        // * If the stored duration exactly represents an exact number of frames, then that duration minus 1
        //   will result in that same number of frames once rounded up.
        // * If the stored duration is for an exact number of frames that was rounded down, then that duration
        //   minus 1 will result in that number of frames once rounded up.
        // * If the stored duration is for an exact number of frames that was rounded up, then that duration
        //   minus 1 will result in that number of frames once rounded up, while that duration unchanged would
        //   result in 1 more frame being counted after rounding up.
        // * If the original duration in the file was not for an exact number of frames, then the movie timebase
        //   would have to be >= 10^6 for there to be any chance of this calculation resulting in the wrong
        //   number of frames. This isn't a case that I've seen. Even if that were to be the case, the original
        //   duration would have to be <= 1 microsecond greater than an exact number of frames in order to
        //   result in the wrong number of frames, which is highly improbable.
        int64_t divisor = int64_t(AV_TIME_BASE) * stream._fpsDen;
        frames = ( (_context->duration - 1) * stream._fpsNum + divisor - 1 ) / divisor;

        // The above calculation is not reliable, because it seems in some situations (such as rendering out a mov
        // with 5 frames at 24 fps from Nuke) the duration has been rounded up to the nearest millisecond, which
        // leads to an extra frame being reported.  To attempt to work around this, compare against the number of
        // frames in the stream, and if they differ by one, use that value instead.
        int64_t streamFrames = stream._avstream->nb_frames;
        if ( (streamFrames > 0) && (std::abs( (double)(frames - streamFrames) ) <= 1) ) {
            frames = streamFrames;
        }
#if TRACE_FILE_OPEN
        std::cout << "        Obtained from AVFormatContext::duration & framerate=";
#endif
    }

    // If number of frames still unknown, obtain from stream's number of frames if specified. Will be 0 if
    // unknown.
    if (!frames) {
#if TRACE_FILE_OPEN
        std::cout << "        Not specified by AVFormatContext::duration, obtaining from AVStream::nb_frames..." << std::endl;
#endif
        frames = stream._avstream->nb_frames;
#if TRACE_FILE_OPEN
        if (frames) {
            std::cout << "        Obtained from AVStream::nb_frames=";
        }
#endif
    }

    // If number of frames still unknown, attempt to calculate from stream's duration, fps and timebase.
    if (!frames) {
#if TRACE_FILE_OPEN
        std::cout << "        Not specified by AVStream::nb_frames, calculating from duration & framerate..." << std::endl;
#endif
        frames = (int64_t(stream._avstream->duration) * stream._avstream->time_base.num  * stream._fpsNum) /
                 (int64_t(stream._avstream->time_base.den) * stream._fpsDen);
#if TRACE_FILE_OPEN
        if (frames) {
            std::cout << "        Calculated from duration & framerate=";
        }
#endif
    }

    // If the number of frames is still unknown, attempt to measure it from the last frame PTS for the stream in the
    // file relative to first (which we know from earlier).
    if (!frames) {
#if TRACE_FILE_OPEN
        std::cout << "        Not specified by duration & framerate, searching frames for last PTS..." << std::endl;
#endif

        int64_t maxPts = stream._startPTS;

        // Seek last key-frame.
        avcodec_flush_buffers(stream._codecContext);
        av_seek_frame(_context, stream._idx, stream.frameToPts(1 << 29), AVSEEK_FLAG_BACKWARD);

        // Read up to last frame, extending max PTS for every valid PTS value found for the video stream.
        av_init_packet(&_avPacket);

        while (av_read_frame(_context, &_avPacket) >= 0) {
            if ( (_avPacket.stream_index == stream._idx) && ( _avPacket.pts != int64_t(AV_NOPTS_VALUE) ) && (_avPacket.pts > maxPts) ) {
                maxPts = _avPacket.pts;
            }
            av_packet_unref(&_avPacket);
        }
#if TRACE_FILE_OPEN
        std::cout << "          Start PTS=" << stream._startPTS << ", Max PTS found=" << maxPts << std::endl;
#endif

        // Compute frame range from min to max PTS. Need to add 1 as both min and max are at starts of frames, so stream
        // extends for 1 frame beyond this.
        frames = 1 + stream.ptsToFrame(maxPts);
#if TRACE_FILE_OPEN
        std::cout << "        Calculated from frame PTS range=";
#endif
    }

#if TRACE_FILE_OPEN
    std::cout << frames << std::endl;
#endif

    return frames;
} // FFmpegFile::getStreamFrames

FFmpegFile::FFmpegFile(const string & filename)
    : _filename(filename)
    , _context(NULL)
    , _format(NULL)
    , _streams()
    , _errorMsg()
    , _invalidState(false)
    , _avPacket()
#ifdef OFX_IO_MT_FFMPEG
    , _lock()
    , _invalidStateLock()
#endif
{
#ifdef OFX_IO_MT_FFMPEG
    //MultiThread::AutoMutex guard(_lock); // not needed in a constructor: we are the only owner
#endif

#if TRACE_FILE_OPEN
    std::cout << "FFmpeg Reader=" << this << "::c'tor(): filename=" << filename << std::endl;
#endif

    assert( !_filename.empty() );
    CHECK( avformat_open_input(&_context, _filename.c_str(), _format, NULL) );
    CHECK( avformat_find_stream_info(_context, NULL) );

#if TRACE_FILE_OPEN
    std::cout << "  " << _context->nb_streams << " streams:" << std::endl;
#endif

    // fill the array with all available video streams
    bool unsuported_codec = false;

    // find all streams that the library is able to decode
    for (unsigned i = 0; i < _context->nb_streams; ++i) {
#if TRACE_FILE_OPEN
        std::cout << "    FFmpeg stream index " << i << ": ";
#endif
        AVStream* avstream = _context->streams[i];

        // be sure to have a valid stream
        if (!avstream || !avstream->FFMSCODEC) {
#if TRACE_FILE_OPEN
            std::cout << "No valid stream or codec, skipping..." << std::endl;
#endif
            continue;
        }

        AVCodecContext *avctx;
        avctx = avcodec_alloc_context3(NULL);
        if (!avctx) {
            setError( "cannot allocate codec context" );

            return;
        }

        int ret = make_context(avctx, avstream);
        if (ret < 0) {
#if TRACE_FILE_OPEN
            std::cout << "Could not convert to context, skipping..." << std::endl;
#endif
            continue;
        }

        // considering only video streams, skipping audio
        if (avctx->codec_type != AVMEDIA_TYPE_VIDEO) {
#if TRACE_FILE_OPEN
            std::cout << "Not a video stream, skipping..." << std::endl;
#endif
            continue;
        }
        if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
#         if TRACE_FILE_OPEN
            std::cout << "Unknown pixel format, skipping..." << std::endl;
#         endif
            continue;
        }

        // find the codec
        AVCodec* videoCodec = avcodec_find_decoder(avctx->codec_id);
        if (videoCodec == NULL) {
#if TRACE_FILE_OPEN
            std::cout << "Decoder not found, skipping..." << std::endl;
#endif
            continue;
        }

        // skip codecs not in the white list
        string reason;
        if ( !isCodecWhitelistedForReading(videoCodec->name) ) {
# if TRACE_FILE_OPEN
            std::cout << "Decoder \"" << videoCodec->name << "\" disallowed, skipping..." << std::endl;
# endif
            unsuported_codec = true;
            continue;
        }

        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            // source: http://git.savannah.gnu.org/cgit/bino.git/tree/src/media_object.cpp

            // Some codecs support multi-threaded decoding (eg mpeg). Its fast but causes problems when opening many readers
            // simultaneously since each opens as many threads as you have cores. This leads to resource starvation and failed reads.
            // Hopefully, getNumCPUs() will give us the right number of usable cores

            // Activate multithreaded decoding. This must be done before opening the codec; see
            // http://lists.gnu.org/archive/html/bino-list/2011-08/msg00019.html
#          ifdef AV_CODEC_CAP_AUTO_THREADS
            // Do not use AV_CODEC_CAP_AUTO_THREADS, since it may create too many threads
            //if (avstream->codec->codec && (avstream->codec->codec->capabilities & AV_CODEC_CAP_AUTO_THREADS)) {
            //    avstream->codec->thread_count = 0;
            //} else
#          endif
            {
                avctx->thread_count = std::min( (int)MultiThread::getNumCPUs(), OFX_FFMPEG_MAX_THREADS ); // ask for the number of available cores for multithreading
#             ifdef AV_CODEC_CAP_SLICE_THREADS
                if ( avctx->codec && (avctx->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) ) {
                    // multiple threads are used to decode a single frame. Reduces delay
                    avctx->thread_type = FF_THREAD_SLICE;
                }
#             endif
                //avstream->codec->thread_count = video_decoding_threads(); // bino's strategy (disabled)
            }
            // Set CODEC_FLAG_EMU_EDGE in the same situations in which ffplay sets it.
            // I don't know what exactly this does, but it is necessary to fix the problem
            // described in this thread: http://lists.nongnu.org/archive/html/bino-list/2012-02/msg00039.html
            int lowres = 0;
#ifdef FF_API_LOWRES
            lowres = avctx->lowres;
#endif
            if ( lowres || ( videoCodec && (videoCodec->capabilities & CODEC_CAP_DR1) ) ) {
                avctx->flags |= CODEC_FLAG_EMU_EDGE;
            }
        }

        // skip if the codec can't be open
        if (avcodec_open2(avctx, videoCodec, NULL) < 0) {
#if TRACE_FILE_OPEN
            std::cout << "Decoder \"" << videoCodec->name << "\" failed to open, skipping..." << std::endl;
#endif
            continue;
        }

#if TRACE_FILE_OPEN
        std::cout << "Video decoder \"" << videoCodec->name << "\" opened ok, getting stream properties:" << std::endl;
#endif

        Stream* stream = new Stream();
        stream->_idx = i;
        stream->_avstream = avstream;
        stream->_codecContext = avctx;
        stream->_videoCodec = videoCodec;
        stream->_avFrame = av_frame_alloc();
        {
            // In |engine| the output bit depth was hard coded to 16-bits.
            // Now it will use the bit depth reported by the decoder so
            // that if a decoder outputs 10-bits then |engine| will convert
            // this correctly. This means that the following change is
            // requireded for FFmpeg decoders. Currently |_bitDepth| is used
            // internally so this change has no side effects.
            // [openfx-io note] when using insternal ffmpeg 8bits->16 bits conversion,
            // (255 = 100%) becomes (65280 =99.6%)
            stream->_bitDepth = avctx->bits_per_raw_sample;
            //stream->_bitDepth = 16; // enabled in Nuke's reader

            const AVPixFmtDescriptor* avPixFmtDescriptor = av_pix_fmt_desc_get(stream->_codecContext->pix_fmt);
            if (avPixFmtDescriptor == NULL) {
                throw std::runtime_error("av_pix_fmt_desc_get() failed");
            }
            // Sanity check the number of components.
            // Only 3 or 4 components are supported by |engine|, that is
            // Nuke/NukeStudio will only accept 3 or 4 component data.
            // For a monochrome image (single channel) promote to 3
            // channels. This is in keeping with all the assumptions
            // throughout the code that if it is not 4 channels data
            // then it must be three channel data. This ensures that
            // all the buffer size calculations are correct.
            stream->_numberOfComponents = avPixFmtDescriptor->nb_components;
            if (3 > stream->_numberOfComponents) {
                stream->_numberOfComponents = 3;
            }
            // AVCodecContext::bits_pre_raw_sample may not be set, if
            // it's not set, try with the following utility function.
            if (0 == stream->_bitDepth) {
                stream->_bitDepth = av_get_bits_per_pixel(avPixFmtDescriptor) / stream->_numberOfComponents;
            }
        }

        if (stream->_bitDepth > 8) {
#        if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 53, 6, 0, 53, 6, 0)
            stream->_outputPixelFormat = (4 == stream->_numberOfComponents) ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB48LE; // 16-bit.
#         else
            stream->_outputPixelFormat = (4 == stream->_numberOfComponents) ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGB48LE; // 16-bit.
#         endif
        } else {
            stream->_outputPixelFormat = (4 == stream->_numberOfComponents) ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24; // 8-bit
        }
#if TRACE_FILE_OPEN
        std::cout << "      Timebase=" << avstream->time_base.num << "/" << avstream->time_base.den << " s/tick" << std::endl;
        std::cout << "      Duration=" << avstream->duration << " ticks, " <<
            double(avstream->duration) * double(avstream->time_base.num) /
            double(avstream->time_base.den) << " s" << std::endl;
        std::cout << "      BitDepth=" << stream->_bitDepth << std::endl;
        std::cout << "      NumberOfComponents=" << stream->_numberOfComponents << std::endl;
#endif

        // If FPS is specified, record it.
        // Otherwise assume 1 fps (default value).
        if ( (avstream->r_frame_rate.num != 0) && (avstream->r_frame_rate.den != 0) ) {
            stream->_fpsNum = avstream->r_frame_rate.num;
            stream->_fpsDen = avstream->r_frame_rate.den;
#if TRACE_FILE_OPEN
            std::cout << "      Framerate=" << stream->_fpsNum << "/" << stream->_fpsDen << ", " <<
                double(stream->_fpsNum) / double(stream->_fpsDen) << " fps" << std::endl;
#endif
        }
#if TRACE_FILE_OPEN
        else {
            std::cout << "      Framerate unspecified, assuming 1 fps" << std::endl;
        }
#endif

        stream->_width  = avctx->width;
        stream->_height = avctx->height;
#if TRACE_FILE_OPEN
        std::cout << "      Image size=" << stream->_width << "x" << stream->_height << std::endl;
#endif

        // set aspect ratio
        stream->_aspect = Stream::GetStreamAspectRatio(stream);

        // set stream start time and numbers of frames
        stream->_startPTS = getStreamStartTime(*stream);
        stream->_frames   = getStreamFrames(*stream);

        // save the stream
        _streams.push_back(stream);
    }
    if ( _streams.empty() ) {
        setError( unsuported_codec ? "unsupported codec..." : "unable to find video stream" );
    }
}

// destructor
FFmpegFile::~FFmpegFile()
{
#ifdef OFX_IO_MT_FFMPEG
    AutoMutex guard(_lock);
#endif

    // force to close all resources needed for all streams
    for (unsigned int i = 0; i < _streams.size(); ++i) {
        delete _streams[i];
    }
    _streams.clear();

    if (_context) {
        avformat_close_input(&_context);
        av_free(_context);
    }
    _filename.clear();
    _errorMsg.clear();
}

const char*
FFmpegFile::getColorspace() const
{
    //The preferred colorspace is figured out from a number of sources - initially we look for a number
    //of different metadata sources that may be present in the file. If these fail we then fall back
    //to using the codec's underlying storage mechanism - if RGB we default to gamma 1.8, if YCbCr we
    //default to gamma 2.2 (note prores special case). Note we also ignore the NCLC atom for reading
    //purposes, as in practise it tends to be incorrect.

    //First look for the meta keys that (recent) Nukes would've written, or special cases in Arri meta.
    //Doubles up searching for lower case keys as the ffmpeg searches are case sensitive, and the keys
    //have been seen to be lower cased (particularly in old Arri movs).
    if (_context && _context->metadata) {
        AVDictionaryEntry* t;

        t = av_dict_get(_context->metadata, "uk.co.thefoundry.Colorspace", NULL, AV_DICT_IGNORE_SUFFIX);
        if (!t) {
            av_dict_get(_context->metadata, "uk.co.thefoundry.colorspace", NULL, AV_DICT_IGNORE_SUFFIX);
        }
        if (t) {
#if 0
            //Validate t->value against root list, to make sure it's been written with a LUT
            //we have a matching conversion for.
            bool found = false;
            int i     = 0;
            while (!found && LUT::builtin_names[i] != NULL) {
                found = !strcasecmp(t->value, LUT::builtin_names[i++]);
            }
#else
            bool found = true;
#endif
            if (found) {
                return t->value;
            }
        }

        t = av_dict_get(_context->metadata, "com.arri.camera.ColorGammaSxS", NULL, AV_DICT_IGNORE_SUFFIX);
        if (!t) {
            av_dict_get(_context->metadata, "com.arri.camera.colorgammasxs", NULL, AV_DICT_IGNORE_SUFFIX);
        }
        if ( t && !strncasecmp(t->value, "LOG-C", 5) ) {
            return "AlexaV3LogC";
        }
        if ( t && !strncasecmp(t->value, "REC-709", 7) ) {
            return "rec709";
        }
    }

    //Special case for prores - the util YUV will report RGB, due to pixel format support, but for
    //compatibility and consistency with official quicktime, we need to be using 2.2 for 422 material
    //and 1.8 for 4444. Protected to deal with ffmpeg vagaries.
    if (!_streams.empty() && _streams[0]->_codecContext && _streams[0]->_codecContext->codec_id) {
        if (_streams[0]->_codecContext->codec_id == AV_CODEC_ID_PRORES) {
            if ( ( _streams[0]->_codecContext->codec_tag == MKTAG('a', 'p', '4', 'h') ) ||
                 ( _streams[0]->_codecContext->codec_tag == MKTAG('a', 'p', '4', 'x') ) ) {
                return "Gamma1.8";
            } else {
                return "Gamma2.2";
            }
        }
    }

    return isYUV() ? "Gamma2.2" : "Gamma1.8";
} // FFmpegFile::getColorspace

void
FFmpegFile::setError(const char* msg,
                     const char* prefix)
{
#ifdef OFX_IO_MT_FFMPEG
    AutoMutex guard(_invalidStateLock);
#endif
    if (prefix) {
        _errorMsg = prefix;
        _errorMsg += msg;
#if TRACE_DECODE_PROCESS
        std::cout << "!!ERROR: " << prefix << msg << std::endl;
#endif
    } else {
        _errorMsg = msg;
#if TRACE_DECODE_PROCESS
        std::cout << "!!ERROR: " << msg << std::endl;
#endif
    }
    _invalidState = true;
}

const string &
FFmpegFile::getError() const
{
#ifdef OFX_IO_MT_FFMPEG
    AutoMutex guard(_lock);
#endif

    return _errorMsg;
}

// return true if the reader can't decode the frame
bool
FFmpegFile::isInvalid() const
{
#ifdef OFX_IO_MT_FFMPEG
    AutoMutex guard(_invalidStateLock);
#endif

    return _invalidState;
}

bool
FFmpegFile::seekFrame(int frame,
                      Stream* stream)
{
    ///Private should not lock

    avcodec_flush_buffers(stream->_codecContext);
    int64_t timestamp = stream->frameToPts(frame);
    int error = av_seek_frame(_context, stream->_idx, timestamp, AVSEEK_FLAG_BACKWARD);
    if (error < 0) {
        // Seek error. Abort attempt to read and decode frames.
        setInternalError(error, "FFmpeg Reader failed to seek frame: ");

        return false;
    }

    return true;
}

// decode a single frame into the buffer thread safe
bool
FFmpegFile::decode(const ImageEffect* plugin,
                   int frame,
                   bool loadNearest,
                   int maxRetries,
                   unsigned char* buffer)
{
    const unsigned int streamIdx = 0;

#ifdef OFX_IO_MT_FFMPEG
    AutoMutex guard(_lock);
#endif

    if ( streamIdx >= _streams.size() ) {
        return false;
    }

    assert(streamIdx == 0);//, "FFmpegFile functions always assume only the first stream is in use");

    // get the stream
    Stream* stream = _streams[streamIdx];

    // Translate from the 1-based frames expected to 0-based frame offsets for use in the rest of this code.
    int desiredFrame = frame - 1;


    // Early-out if out-of-range frame requested.

    if (desiredFrame < 0) {
        if (loadNearest) {
            desiredFrame = 0;
        } else {
            throw std::runtime_error("Missing frame");
        }
    } else if (desiredFrame >= stream->_frames) {
        if (loadNearest) {
            desiredFrame = (int)stream->_frames - 1;
        } else {
            throw std::runtime_error("Missing frame");
        }
    }

#if TRACE_DECODE_PROCESS
    std::cout << "FFmpeg Reader=" << this << "::decode(): frame=" << desiredFrame << ", videoStream=" << streamIdx << ", streamIdx=" << stream->_idx << std::endl;
#endif

    // Number of read retries remaining when decode stall is detected before we give up (in the case of post-seek stalls,
    // such retries are applied only after we've searched all the way back to the start of the file and failed to find a
    // successful start point for playback)..
    //
    // We have a rather annoying case with a small subset of media files in which decode latency (between input and output
    // frames) will exceed the maximum above which we detect decode stall at certain frames on the first pass through the
    // file but those same frames will decode succesfully on a second attempt. The root cause of this is not understood but
    // it appears to be some oddity of FFmpeg. While I don't really like it, retrying decode enables us to successfully
    // decode those files rather than having to fail the read.
    int retriesRemaining = std::max(1, maxRetries);

    // Whether we have just performed a seek and are still awaiting the first decoded frame after that seek. This controls
    // how we respond when a decode stall is detected.
    //
    // One cause of such stalls is when a file contains incorrect information indicating that a frame is a key-frame when it
    // is not; a seek may land at such a frame but the decoder will not be able to start decoding until a real key-frame is
    // reached, which may be a long way in the future. Once a frame has been decoded, we will expect it to be the first frame
    // input to decode but it will actually be the next real key-frame found, leading to subsequent frames appearing as
    // earlier frame numbers and the movie ending earlier than it should. To handle such cases, when a stall is detected
    // immediately after a seek, we seek to the frame before the previous seek's landing frame, allowing us to search back
    // through the movie for a valid key frame from which decode commences correctly; if this search reaches the beginning of
    // the movie, we give up and fail the read, thus ensuring that this method will exit at some point.
    //
    // Stalls once seeking is complete and frames are being decoded are handled differently; these result in immediate read
    // failure.
    bool awaitingFirstDecodeAfterSeek = false;

    // If the frame we want is not the next one to be decoded, seek to the keyframe before/at our desired frame. Set the last
    // seeked frame to indicate that we need to synchronise frame indices once we've read the first frame of the video stream,
    // since we don't yet know which frame number the seek will land at. Also invalidate current indices, reset accumulated
    // decode latency and record that we're awaiting the first decoded frame after a seek.
    int lastSeekedFrame = -1; // 0-based index of the last frame to which we seeked when seek in progress / negative when no
    // seek in progress,

    if (desiredFrame != stream->_decodeNextFrameOut) {
#if TRACE_DECODE_PROCESS
        std::cout << "  Next frame expected out=" << stream->_decodeNextFrameOut << ", Seeking to desired frame" << std::endl;
#endif

        lastSeekedFrame = desiredFrame;
        stream->_decodeNextFrameIn  = -1;
        stream->_decodeNextFrameOut = -1;
        stream->_accumDecodeLatency = 0;
        awaitingFirstDecodeAfterSeek = true;

        if ( !seekFrame(desiredFrame, stream) ) {
            return false;
        }
    }
#if TRACE_DECODE_PROCESS
    else {
        std::cout << "  Next frame expected out=" << stream->_decodeNextFrameOut << ", No seek required" << std::endl;
    }
#endif

    av_init_packet(&_avPacket);

    // Loop until the desired frame has been decoded. May also break from within loop on failure conditions where the
    // desired frame will never be decoded.
    bool hasPicture = false;
    do {
        bool decodeAttempted = false;
        int frameDecoded = 0;
        int srcColourRange = stream->_codecContext->color_range;

        // If the next frame to decode is within range of frames (or negative implying invalid; we've just seeked), read
        // a new frame from the source file and feed it to the decoder if it's for the video stream.
        if (stream->_decodeNextFrameIn < stream->_frames) {
#if TRACE_DECODE_PROCESS
            std::cout << "  Next frame expected in=";
            if (stream->_decodeNextFrameIn >= 0) {
                std::cout << stream->_decodeNextFrameIn;
            } else {
                std::cout << "unknown";
            }
#endif

            int error = av_read_frame(_context, &_avPacket);
            // [FD] 2015/01/20
            // the following if() was not in Nuke's FFmpeg Reader.cpp
            if (error == (int)AVERROR_EOF) {
                // getStreamFrames() was probably wrong
                stream->_frames = stream->_decodeNextFrameIn;
                if (loadNearest) {
                    // try again
                    desiredFrame = (int)stream->_frames - 1;
                    lastSeekedFrame = desiredFrame;
                    stream->_decodeNextFrameIn  = -1;
                    stream->_decodeNextFrameOut = -1;
                    stream->_accumDecodeLatency = 0;
                    awaitingFirstDecodeAfterSeek = true;

                    if ( !seekFrame(desiredFrame, stream) ) {
                        return false;
                    }
                }
                continue;
            }
            if (error < 0) {
                // Read error. Abort attempt to read and decode frames.
#if TRACE_DECODE_PROCESS
                std::cout << ", Read failed" << std::endl;
#endif
                setInternalError(error, "FFmpeg Reader failed to read frame: ");
                break;
            }
#if TRACE_DECODE_PROCESS
            std::cout << ", Read OK, Packet data:" << std::endl;
            std::cout << "    PTS=" << _avPacket.pts <<
                ", DTS=" << _avPacket.dts <<
                ", Duration=" << _avPacket.duration <<
                ", KeyFrame=" << ( (_avPacket.flags & AV_PKT_FLAG_KEY) ? 1 : 0 ) <<
                ", Corrupt=" << ( (_avPacket.flags & AV_PKT_FLAG_CORRUPT) ? 1 : 0 ) <<
                ", StreamIdx=" << _avPacket.stream_index <<
                ", PktSize=" << _avPacket.size;
#endif

            // If the packet read belongs to the video stream, synchronise frame indices from it if required and feed it
            // into the decoder.
            if (_avPacket.stream_index == stream->_idx) {
#if TRACE_DECODE_PROCESS
                std::cout << ", Relevant stream" << std::endl;
#endif

                // If the packet read has a valid PTS, record that we have seen a PTS for this stream.
                if ( _avPacket.pts != int64_t(AV_NOPTS_VALUE) ) {
                    stream->_ptsSeen = true;
                }

                // If a seek is in progress, we need to synchronise frame indices if we can...
                if (lastSeekedFrame >= 0) {
#if TRACE_DECODE_PROCESS
                    std::cout << "    In seek (" << lastSeekedFrame << ")";
#endif

                    // Determine which frame the seek landed at, using whichever kind of timestamp is currently selected for this
                    // stream. If there's no timestamp available at that frame, we can't synchronise frame indices to know which
                    // frame we're first going to decode, so we need to seek back to an earlier frame in hope of obtaining a
                    // timestamp. Likewise, if the landing frame is after the seek target frame (this can happen, presumably a bug
                    // in FFmpeg seeking), we need to seek back to an earlier frame so that we can start decoding at or before the
                    // desired frame.
                    int landingFrame = ( _avPacket.*stream->_timestampField == int64_t(AV_NOPTS_VALUE) ? -1 :
                                         stream->ptsToFrame(_avPacket.*stream->_timestampField) );

                    if ( ( _avPacket.*stream->_timestampField == int64_t(AV_NOPTS_VALUE) ) || (landingFrame  > lastSeekedFrame) ) {
#if TRACE_DECODE_PROCESS
                        std::cout << ", landing frame not found";
                        if ( _avPacket.*stream->_timestampField == int64_t(AV_NOPTS_VALUE) ) {
                            std::cout << " (no timestamp)";
                        } else {
                            std::cout << " (landed after target at " << landingFrame << ")";
                        }
#endif

                        // Wind back 1 frame from last seeked frame. If that takes us to before frame 0, we're never going to be
                        // able to synchronise using the current timestamp source...
                        if (--lastSeekedFrame < 0) {
#if TRACE_DECODE_PROCESS
                            std::cout << ", can't seek before start";
#endif

                            // If we're currently using PTSs to determine the landing frame and we've never seen a valid PTS for any
                            // frame from this stream, switch to using DTSs and retry the read from the initial desired frame.
                            if ( (stream->_timestampField == &AVPacket::pts) && !stream->_ptsSeen ) {
                                stream->_timestampField = &AVPacket::dts;
                                lastSeekedFrame = desiredFrame;
#if TRACE_DECODE_PROCESS
                                std::cout << ", PTSs absent, switching to use DTSs";
#endif
                            }
                            // Otherwise, failure to find a landing point isn't caused by an absence of PTSs from the file or isn't
                            // recovered by using DTSs instead. Something is wrong with the file. Abort attempt to read and decode frames.
                            else {
#if TRACE_DECODE_PROCESS
                                if (stream->_timestampField == &AVPacket::dts) {
                                    std::cout << ", search using DTSs failed";
                                } else {
                                    std::cout << ", PTSs present";
                                }
                                std::cout << ",  giving up" << std::endl;
#endif
                                setError("FFmpeg Reader failed to find timing reference frame, possible file corruption");
                                break;
                            }
                        }

                        // Seek to the new frame. By leaving the seek in progress, we will seek backwards frame by frame until we
                        // either successfully synchronise frame indices or give up having reached the beginning of the stream.
#if TRACE_DECODE_PROCESS
                        std::cout << ", seeking to " << lastSeekedFrame << std::endl;
#endif
                        if ( !seekFrame(lastSeekedFrame, stream) ) {
                            break;
                        }
                    }
                    // Otherwise, we have a valid landing frame, so set that as the next frame into and out of decode and set
                    // no seek in progress.
                    else {
#if TRACE_DECODE_PROCESS
                        std::cout << ", landed at " << landingFrame << std::endl;
#endif
                        stream->_decodeNextFrameOut = stream->_decodeNextFrameIn = landingFrame;
                        lastSeekedFrame = -1;
                    }
                }

                // If there's no seek in progress, feed this frame into the decoder.
                if (lastSeekedFrame < 0) {
#if TRACE_DECODE_BITSTREAM
                    // H.264 ONLY
                    std::cout << "  Decoding input frame " << stream->_decodeNextFrameIn << " bitstream:" << std::endl;
                    uint8_t *data = _avPacket.data;
                    uint32_t remain = _avPacket.size;
                    while (remain > 0) {
                        if (remain < 4) {
                            std::cout << "    Insufficient remaining bytes (" << remain << ") for block size at BlockOffset=" << (data - _avPacket.data) << std::endl;
                            remain = 0;
                        } else {
                            uint32_t blockSize = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
                            data += 4;
                            remain -= 4;
                            std::cout << "    BlockOffset=" << (data - _avPacket.data) << ", Size=" << blockSize;
                            if (remain < blockSize) {
                                std::cout << ", Insufficient remaining bytes (" << remain << ")" << std::endl;
                                remain = 0;
                            } else {
                                std::cout << ", Bytes:";
                                int count = (blockSize > 16 ? 16 : blockSize);
                                for (int offset = 0; offset < count; offset++) {
                                    static const char hexTable[] = "0123456789ABCDEF";
                                    uint8_t byte = data[offset];
                                    std::cout << ' ' << hexTable[byte >> 4] << hexTable[byte & 0xF];
                                }
                                std::cout << std::endl;
                                data += blockSize;
                                remain -= blockSize;
                            }
                        }
                    }
#elif TRACE_DECODE_PROCESS
                    std::cout << "  Decoding input frame " << stream->_decodeNextFrameIn << std::endl;
#endif

                    // Advance next frame to input.
                    ++stream->_decodeNextFrameIn;

                    // Decode the frame just read. frameDecoded indicates whether a decoded frame was output.
                    decodeAttempted = true;

#if USE_NEW_FFMPEG_API
                    error = avcodec_send_packet(stream->_codecContext, &_avPacket);
                    if (error == 0) {
                        error = avcodec_receive_frame(stream->_codecContext, stream->_avFrame);
                        if ( error == AVERROR(EAGAIN) ) {
                            frameDecoded = 0;
                            error = 0;
                        } else if (error < 0) {
                            frameDecoded = 0;
                        } else {
                            frameDecoded = 1;
                        }
                    }
#else
                    error = avcodec_decode_video2(stream->_codecContext, stream->_avFrame, &frameDecoded, &_avPacket);
#endif
                    if (error < 0) {
                        // Decode error. Abort attempt to read and decode frames.
                        setInternalError(error, "FFmpeg Reader failed to decode frame: ");
                        break;
                    }
                }
            } //_avPacket.stream_index == stream->_idx
#if TRACE_DECODE_PROCESS
            else {
                std::cout << ", Irrelevant stream" << std::endl;
            }
#endif
        } //stream->_decodeNextFrameIn < stream->_frames
          // If the next frame to decode is out of frame range, there's nothing more to read and the decoder will be fed
          // null input frames in order to obtain any remaining output.
        else {
#if TRACE_DECODE_PROCESS
            std::cout << "  No more frames to read, pumping remaining decoder output" << std::endl;
#endif

            // Obtain remaining frames from the decoder. pkt_ contains NULL packet data pointer and size at this point,
            // required to pump out remaining frames with no more input. frameDecoded indicates whether a decoded frame
            // was output.
            decodeAttempted = true;
            int error = 0;
            if ( (AV_CODEC_ID_PRORES == stream->_codecContext->codec_id) ||
                 (AV_CODEC_ID_DNXHD == stream->_codecContext->codec_id) ) {
                // Apple ProRes specific.
                // The ProRes codec is I-frame only so will not have any
                // remaining frames.
            } else {
#if USE_NEW_FFMPEG_API
                error = avcodec_send_packet(stream->_codecContext, &_avPacket);
                if (error == 0) {
                    error = avcodec_receive_frame(stream->_codecContext, stream->_avFrame);
                    if ( error == AVERROR(EAGAIN) ) {
                        frameDecoded = 0;
                        error = 0;
                    } else if (error < 0) {
                        frameDecoded = 0;
                        // Decode error. Abort attempt to read and decode frames.
                    } else {
                        frameDecoded = 1;
                    }
                }
#else
                error = avcodec_decode_video2(stream->_codecContext, stream->_avFrame, &frameDecoded, &_avPacket);
#endif
            }
            if (error < 0) {
                // Decode error. Abort attempt to read and decode frames.
                setInternalError(error, "FFmpeg Reader failed to decode frame: ");
                break;
            }
        }

        // If a frame was decoded, ...
        if (frameDecoded) {
#if TRACE_DECODE_PROCESS
            std::cout << "    Frame decoded=" << stream->_decodeNextFrameOut;
#endif

            // Now that we have had a frame decoded, we know that seek landed at a valid place to start decode. Any decode
            // stalls detected after this point will result in immediate decode failure.
            awaitingFirstDecodeAfterSeek = false;

            // If the frame just output from decode is the desired one, get the decoded picture from it and set that we
            // have a picture.
            if (stream->_decodeNextFrameOut == desiredFrame) {
#if TRACE_DECODE_PROCESS
                std::cout << ", is desired frame" << std::endl;
#endif

                SwsContext* context = NULL;
                {
                    context = stream->getConvertCtx(stream->_codecContext->pix_fmt, stream->_width, stream->_height,
                                                    srcColourRange,
                                                    stream->_outputPixelFormat, stream->_width, stream->_height);
                }

                // Scale if any of the decoding path has provided a convert
                // context. Otherwise, no scaling/conversion is required after
                // decoding the frame.
                if (context) {
                    uint8_t *data[4];
                    int linesize[4];
#if 0
                    {
                        AVPicture output;
                        avpicture_fill(&output, buffer, stream->_outputPixelFormat, stream->_width, stream->_height);
                        data[0] = output.data[0];
                        data[1] = output.data[1];
                        data[2] = output.data[2];
                        data[3] = output.data[3];
                        linesize[0] = output.linesize[0];
                        linesize[1] = output.linesize[1];
                        linesize[2] = output.linesize[2];
                        linesize[3] = output.linesize[3];
                    }
#else
                    av_image_fill_arrays(data, linesize, buffer, stream->_outputPixelFormat, stream->_width, stream->_height, 1);
#endif
                    sws_scale(context,
                              stream->_avFrame->data,
                              stream->_avFrame->linesize,
                              0,
                              stream->_height,
                              data,
                              linesize);
                }

                hasPicture = true;
            }
#if TRACE_DECODE_PROCESS
            else {
                std::cout << ", is not desired frame (" << desiredFrame << ")" << std::endl;
            }
#endif

            // Advance next output frame expected from decode.
            ++stream->_decodeNextFrameOut;
        }
        // If no frame was decoded but decode was attempted, determine whether this constitutes a decode stall and handle if so.
        else if (decodeAttempted) {
            // Failure to get an output frame for an input frame increases the accumulated decode latency for this stream.
            ++stream->_accumDecodeLatency;

#if TRACE_DECODE_PROCESS
            std::cout << "    No frame decoded, accumulated decode latency=" << stream->_accumDecodeLatency << ", max allowed latency=" << stream->getCodecDelay() << std::endl;
#endif

            // If the accumulated decode latency exceeds the maximum delay permitted for this codec at this time (the delay can
            // change dynamically if the codec discovers B-frames mid-stream), we've detected a decode stall.
            if ( stream->_accumDecodeLatency > stream->getCodecDelay() ) {
                int seekTargetFrame; // Target frame for any seek we might perform to attempt decode stall recovery.

                // Handle a post-seek decode stall.
                if (awaitingFirstDecodeAfterSeek) {
                    // If there's anywhere in the file to seek back to before the last seek's landing frame (which can be found in
                    // stream->_decodeNextFrameOut, since we know we've not decoded any frames since landing), then set up a seek to
                    // the frame before that landing point to try to find a valid decode start frame earlier in the file.
                    if (stream->_decodeNextFrameOut > 0) {
                        seekTargetFrame = stream->_decodeNextFrameOut - 1;
#if TRACE_DECODE_PROCESS
                        std::cout << "    Post-seek stall detected, trying earlier decode start, seeking frame " << seekTargetFrame << std::endl;
#endif
                    }
                    // Otherwise, there's nowhere to seek back. If we have any retries remaining, use one to attempt the read again,
                    // starting from the desired frame.
                    else if (retriesRemaining > 0) {
                        --retriesRemaining;
                        seekTargetFrame = desiredFrame;
#if TRACE_DECODE_PROCESS
                        std::cout << "    Post-seek stall detected, at start of file, retrying from desired frame " << seekTargetFrame << std::endl;
#endif
                    }
                    // Otherwise, all we can do is to fail the read so that this method exits safely.
                    else {
#if TRACE_DECODE_PROCESS
                        std::cout << "    Post-seek STALL DETECTED, at start of file, no more retries, failed read" << std::endl;
#endif
                        setError("FFmpeg Reader failed to find decode reference frame, possible file corruption");
                        break;
                    }
                }
                // Handle a mid-decode stall. All we can do is to fail the read so that this method exits safely.
                else {
                    // If we have any retries remaining, use one to attempt the read again, starting from the desired frame.
                    if (retriesRemaining > 0) {
                        --retriesRemaining;
                        seekTargetFrame = desiredFrame;
#if TRACE_DECODE_PROCESS
                        std::cout << "    Mid-decode stall detected, retrying from desired frame " << seekTargetFrame << std::endl;
#endif
                    }
                    // Otherwise, all we can do is to fail the read so that this method exits safely.
                    else {
#if TRACE_DECODE_PROCESS
                        std::cout << "    Mid-decode STALL DETECTED, no more retries, failed read" << std::endl;
#endif
                        setError("FFmpeg Reader detected decoding stall, possible file corruption");
                        break;
                    }
                } // if (awaitingFirstDecodeAfterSeek)

                // If we reach here, seek to the target frame chosen above in an attempt to recover from the decode stall.
                lastSeekedFrame = seekTargetFrame;
                stream->_decodeNextFrameIn  = -1;
                stream->_decodeNextFrameOut = -1;
                stream->_accumDecodeLatency = 0;
                awaitingFirstDecodeAfterSeek = true;

                if ( !seekFrame(seekTargetFrame, stream) ) {
                    break;
                }
            } // if (decodeAttempted)
        } // if (stream->_decodeNextFrameIn < stream->_frames)
        av_packet_unref(&_avPacket);
        if ( plugin->abort() ) {
            return false;
        }
    } while (!hasPicture);

#if TRACE_DECODE_PROCESS
    std::cout << "<-validPicture=" << hasPicture << " for frame " << desiredFrame << std::endl;
#endif

    // If read failed, reset the next frame expected out so that we seek and restart decode on next read attempt. Also free
    // the AVPacket, since it won't have been freed at the end of the above loop (we reach here by a break from the main
    // loop when hasPicture is false).
    if (!hasPicture) {
        if (_avPacket.size > 0) {
            av_packet_unref(&_avPacket);
        }
        stream->_decodeNextFrameOut = -1;
    }

    return hasPicture;
} // FFmpegFile::decode

bool
FFmpegFile::getFPS(double & fps,
                   unsigned streamIdx)
{
    if ( streamIdx >= _streams.size() ) {
        return false;
    }

    // get the stream
    Stream* stream = _streams[streamIdx];
    // guard against division by zero
    assert(stream->_fpsDen);
    fps = stream->_fpsDen ? ( (double)stream->_fpsNum / stream->_fpsDen ) : stream->_fpsNum;

    return true;
}

// get stream information
bool
FFmpegFile::getInfo(int & width,
                    int & height,
                    double & aspect,
                    int & frames,
                    unsigned streamIdx)
{
    if ( streamIdx >= _streams.size() ) {
        return false;
    }

    // get the stream
    Stream* stream = _streams[streamIdx];

    width  = stream->_width;
    height = stream->_height;
    aspect = stream->_aspect;
    frames = (int)stream->_frames;

    return true;
}

std::size_t
FFmpegFile::getBufferBytesCount() const
{
    if ( _streams.empty() ) {
        return 0;
    }

    Stream* stream = _streams[0];
    std::size_t pixelDepth = stream->_bitDepth > 8 ? sizeof(unsigned short) : sizeof(unsigned char);

    // this is the first stream (in fact the only one we consider for now), allocate the output buffer according to the bitdepth
    return stream->_width * stream->_height * stream->_numberOfComponents * pixelDepth;
}

FFmpegFileManager::FFmpegFileManager()
    : _files()
    , _lock(0)
{
}

FFmpegFileManager::~FFmpegFileManager()
{
    for (FilesMap::iterator it = _files.begin(); it != _files.end(); ++it) {
        for (std::list<FFmpegFile*>::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
            delete *it2;
        }
    }
    _files.clear();
    delete _lock;
}

void
FFmpegFileManager::init()
{
    _lock = new FFmpegFile::Mutex;
}

void
FFmpegFileManager::clear(void const * plugin)
{
    assert(_lock);
    FFmpegFile::AutoMutex guard(*_lock);
    FilesMap::iterator found = _files.find(plugin);
    if ( found != _files.end() ) {
        for (std::list<FFmpegFile*>::iterator it = found->second.begin(); it != found->second.end(); ++it) {
            delete *it;
        }
        _files.erase(found);
    }
}

FFmpegFile*
FFmpegFileManager::get(void const * plugin,
                       const string &filename) const
{
    if (filename.empty() || !plugin) {
        return 0;
    }
    assert(_lock);
    FFmpegFile::AutoMutex guard(*_lock);
    FilesMap::iterator found = _files.find(plugin);
    if ( found != _files.end() ) {
        for (std::list<FFmpegFile*>::iterator it = found->second.begin(); it != found->second.end(); ++it) {
            if ( (*it)->getFilename() == filename ) {
                if ( (*it)->isInvalid() ) {
                    delete *it;
                    found->second.erase(it);
                    break;
                } else {
                    return *it;
                }
            }
        }
    }

    return NULL;
}

FFmpegFile*
FFmpegFileManager::getOrCreate(void const * plugin,
                               const string &filename) const
{
    if (filename.empty() || !plugin) {
        return 0;
    }
    assert(_lock);
    FFmpegFile::AutoMutex guard(*_lock);
    FilesMap::iterator found = _files.find(plugin);
    if ( found != _files.end() ) {
        for (std::list<FFmpegFile*>::iterator it = found->second.begin(); it != found->second.end(); ++it) {
            if ( (*it)->getFilename() == filename ) {
                if ( (*it)->isInvalid() ) {
                    delete *it;
                    found->second.erase(it);
                    break;
                } else {
                    return *it;
                }
            }
        }
    }

    FFmpegFile* file = new FFmpegFile(filename);
    if ( found == _files.end() ) {
        std::list<FFmpegFile*> fileList;
        fileList.push_back(file);
        _files.insert( make_pair(plugin, fileList) );
    } else {
        found->second.push_back(file);
    }

    return file;
}
