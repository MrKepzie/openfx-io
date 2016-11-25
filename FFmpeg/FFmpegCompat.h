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

// Various macros for backward compatibility with older ffmpeg versions
// sources:
// https://github.com/FFMS/ffms2/blob/master/include/ffmscompat.h
// http://git.savannah.gnu.org/cgit/bino.git/tree/src/media_object.cpp

#ifndef FFMPEGCOMPAT_H
#define FFMPEGCOMPAT_H


#ifdef __GNUC__
#       define ffms_used __attribute__( (used) )
#else
#       define ffms_used
#endif

// Defaults to libav compatibility, uncomment (when building with msvc) to force ffmpeg compatibility.
//#define FFMS_USE_FFMPEG_COMPAT

// Attempt to auto-detect whether or not we are using ffmpeg.  Newer versions of ffmpeg have their micro versions 100+
#if LIBAVFORMAT_VERSION_MICRO > 99 || LIBAVUTIL_VERSION_MICRO > 99 || LIBAVCODEC_VERSION_MICRO > 99 || LIBSWSCALE_VERSION_MICRO > 99
#       ifndef FFMS_USE_FFMPEG_COMPAT
#               define FFMS_USE_FFMPEG_COMPAT
#       endif
#endif

// Helper to handle checking for different versions in libav and ffmpeg
// First version is required libav versio, second is required ffmpeg version
#ifdef FFMS_USE_FFMPEG_COMPAT
#  define VERSION_CHECK(LIB, cmp, u1, u2, u3, major, minor, micro) ( (LIB) cmp ( AV_VERSION_INT(major, minor, micro) ) )
#else
#  define VERSION_CHECK(LIB, cmp, major, minor, micro, u1, u2, u3) ( (LIB) cmp ( AV_VERSION_INT(major, minor, micro) ) )
#endif

// Compatibility with older/newer ffmpegs
#ifdef LIBAVFORMAT_VERSION_INT
#       if (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(53, 2, 0 ) )
#               define avformat_open_input(c, s, f, o) av_open_input_file(c, s, f, 0, o) // this works because the parameters/options are not used
#       endif
#       if (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(53, 3, 0 ) )
#               define avformat_find_stream_info(c, o) av_find_stream_info(c)
#       endif
#       if VERSION_CHECK(LIBAVFORMAT_VERSION_INT, <, 53, 17, 0, 53, 25, 0)
#               define avformat_close_input(c) av_close_input_file(*c)
#       endif
#   if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 10, 0)
#               define avformat_new_stream(s, c) av_new_stream(s, c)
#       endif
#   if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(52, 101, 0)
#       define av_dump_format dump_format
#   endif
#   if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 45, 0)
#       define av_guess_format guess_format
#   endif
#   if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 111, 0)
#       define avformat_write_header av_write_header
#   endif
#   if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 105, 0)
#       define avio_open url_fopen
#       define avio_close url_fclose
#   endif
#   if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 107, 0)
#       define AVIO_FLAG_WRITE URL_WRONLY
#   endif
#   if (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(54, 2, 0 ) )
#        define AV_DISPOSITION_ATTACHED_PIC 0xBEEFFACE
#   endif
#endif

#ifdef LIBAVCODEC_VERSION_INT
#       if (LIBAVCODEC_VERSION_INT) >= (AV_VERSION_INT(52, 94, 3 ) ) // there are ~3 revisions where this will break but fixing that is :effort:
#               undef SampleFormat
#       else
#               define AVSampleFormat SampleFormat
#               define av_get_bits_per_sample_fmt av_get_bits_per_sample_format
#               define AV_SAMPLE_FMT_U8         SAMPLE_FMT_U8
#               define AV_SAMPLE_FMT_S16        SAMPLE_FMT_S16
#               define AV_SAMPLE_FMT_S32        SAMPLE_FMT_S32
#               define AV_SAMPLE_FMT_FLT        SAMPLE_FMT_FLT
#               define AV_SAMPLE_FMT_DBL        SAMPLE_FMT_DBL
#       endif
#       if (LIBAVCODEC_VERSION_INT) < (AV_VERSION_INT(53, 6, 0 ) )
#               define avcodec_open2(a, c, o) avcodec_open(a, c)
#               define avcodec_alloc_context3(c) avcodec_alloc_context()
#       endif
#   if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 30, 2)
#       define AV_PKT_FLAG_KEY PKT_FLAG_KEY
#   endif
#   if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 64, 0)
#       define AVMediaType CodecType
#       define AVMEDIA_TYPE_UNKNOWN CODEC_TYPE_UNKNOWN
#       define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#       define AVMEDIA_TYPE_AUDIO CODEC_TYPE_AUDIO
#       define AVMEDIA_TYPE_DATA CODEC_TYPE_DATA
#       define AVMEDIA_TYPE_SUBTITLE CODEC_TYPE_SUBTITLE
#       define AVMEDIA_TYPE_ATTACHMENT CODEC_TYPE_ATTACHMENT
#       define AVMEDIA_TYPE_NB CODEC_TYPE_NB
#   endif
#   if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 54, 25, 0, 54, 51, 100)
#       define AV_CODEC_ID_NONE CODEC_ID_NONE
#       define AV_CODEC_ID_RAWVIDEO CODEC_ID_RAWVIDEO
#       define AV_CODEC_ID_MJPEG CODEC_ID_MJPEG
#       define AVCodecID CodecID
//      other codec types may have to be defined too
#   endif
#   if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(52, 112, 0)
#       define avcodec_thread_init(c, n) (0)
#   endif
#   if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 54, 28, 0, 54, 59, 100)
namespace {
inline void
avcodec_free_frame(AVFrame **frame) { av_freep(frame); }
};
#   endif
#   ifndef AVCODEC_MAX_AUDIO_FRAME_SIZE
#       define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
#   endif
#   if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 54, 28, 0, 54, 59, 100)
static void
av_frame_free(AVFrame **frame) { av_freep(frame); }

#               define av_frame_unref avcodec_get_frame_defaults
#   elif VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 55, 28, 1, 55, 45, 101)
#               define av_frame_free avcodec_free_frame
#               define av_frame_unref avcodec_get_frame_defaults
#   endif
#       if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 57, 8, 0, 57, 12, 100)
#               define av_packet_unref av_free_packet
#       endif

// should the following check be on (LIBAVFORMAT_VERSION_INT) < (AV_VERSION_INT(57,5,0)) ?
// https://ffmpeg.org/pipermail/ffmpeg-cvslog/2016-April/099192.html
// https://ffmpeg.org/pipermail/ffmpeg-cvslog/2016-April/099152.html
#       if VERSION_CHECK(LIBAVCODEC_VERSION_INT, <, 57, 14, 0, 57, 33, 100)
#               define FFMSCODEC codec
namespace {
inline ffms_used int
make_context(AVCodecContext *dst,
             AVStream *src) { return avcodec_copy_context(dst, src->codec); }
};
#       else
#               define FFMSCODEC codecpar
namespace {
inline ffms_used int
make_context(AVCodecContext *dst,
             AVStream *src) { return avcodec_parameters_to_context(dst, src->codecpar); }
}
# endif
#endif // ifdef LIBAVCODEC_VERSION_INT

#ifdef LIBAVUTIL_VERSION_INT
#       if (LIBAVUTIL_VERSION_INT) < (AV_VERSION_INT(51, 1, 0 ) )
#               define av_get_picture_type_char av_get_pict_type_char
#               define AV_PICTURE_TYPE_B FF_B_TYPE
#       endif
#       if (LIBAVUTIL_VERSION_INT) < (AV_VERSION_INT(51, 2, 0 ) )
#               define av_get_pix_fmt_name avcodec_get_pix_fmt_name
#       endif
#       if (LIBAVUTIL_VERSION_INT) < (AV_VERSION_INT(51, 4, 0 ) )
#               define av_get_bytes_per_sample(a) (av_get_bits_per_sample_fmt(a) / 8)
#       endif
#       if (LIBAVUTIL_VERSION_INT) < (AV_VERSION_INT(51, 12, 0 ) )
#               define av_set_opt_int(o, n, v, s) av_set_int(o, n, v)
#       endif
#   if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(51, 5, 0)
#       define AV_DICT_IGNORE_SUFFIX AV_METADATA_IGNORE_SUFFIX
#       define AV_DICT_DONT_OVERWRITE AV_METADATA_DONT_OVERWRITE
#       define AVDictionaryEntry AVMetadataTag
#       define av_dict_get av_metadata_get
#       define av_dict_set av_metadata_set2
#       define av_dict_free av_metadata_free
#   else
#       define av_metadata_conv(ctx, d_conv, s_conv)
#   endif
#   if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 27, 0, 51, 46, 100)
#       define av_get_packed_sample_fmt(fmt) ( fmt < AV_SAMPLE_FMT_U8P ? fmt : fmt - (AV_SAMPLE_FMT_U8P - AV_SAMPLE_FMT_U8) )
#   endif
#   if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 44, 0, 51, 76, 100)
#       include <libavutil/pixdesc.h>

#       if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 51, 42, 0, 51, 74, 100)
#           define AVPixelFormat PixelFormat
#           define AV_PIX_FMT_NB       PIX_FMT_NB
#           define AV_PIX_FMT_YUVJ420P PIX_FMT_YUVJ420P
#           define AV_PIX_FMT_YUV420P  PIX_FMT_YUV420P
#           define AV_PIX_FMT_YUVJ422P PIX_FMT_YUVJ422P
#           define AV_PIX_FMT_YUV422P  PIX_FMT_YUV422P
#           define AV_PIX_FMT_YUVJ444P PIX_FMT_YUVJ444P
#           define AV_PIX_FMT_YUV444P  PIX_FMT_YUV444P
#           define AV_PIX_FMT_YUVJ440P PIX_FMT_YUVJ440P
#           define AV_PIX_FMT_YUV440P  PIX_FMT_YUV440P
#           define AV_PIX_FMT_RGB48LE  PIX_FMT_RGB48LE
#           define AV_PIX_FMT_RGBA     PIX_FMT_RGBA
// 64LE appeared with libav & ffmpeg 53,6,0
//#           define AV_PIX_FMT_RGBA64LE PIX_FMT_RGBA64LE
#           define AV_PIX_FMT_RGB24    PIX_FMT_RGB24
#       endif
namespace {
inline const AVPixFmtDescriptor *
av_pix_fmt_desc_get(AVPixelFormat pix_fmt)
{
    if ( (pix_fmt < 0) || (pix_fmt >= AV_PIX_FMT_NB) ) {
        return NULL;
    }

    return &av_pix_fmt_descriptors[pix_fmt];
}
};
#   endif
#   if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 52, 9, 0, 52, 20, 100)
#     define AV_PIX_FMT_FLAG_RGB PIX_FMT_RGB
#   endif
#   if VERSION_CHECK(LIBAVUTIL_VERSION_INT, <, 52, 9, 0, 52, 20, 100)
#       define av_frame_alloc avcodec_alloc_frame
#   endif
#       if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >, 55, 0, 0, 55, 0, 100) || defined(FF_API_PLUS1_MINUS1)
#               define FFMS_DEPTH(x) ( (x).depth )
#       else
#               define FFMS_DEPTH(x) ( (x).depth_minus1 + 1 )
# endif
#endif // ifdef LIBAVUTIL_VERSION_INT
#ifdef AVERROR
#define AVERROR_IO AVERROR(EIO)
#define AVERROR_NUMEXPECTED AVERROR(EDOM)
#define AVERROR_NOMEM AVERROR(ENOMEM)
#define AVERROR_NOFMT AVERROR(EILSEQ)
#define AVERROR_NOTSUPP AVERROR(ENOSYS)
#define AVERROR_NOENT AVERROR(ENOENT)
#endif

#endif // ifndef FFMPEGCOMPAT_H
