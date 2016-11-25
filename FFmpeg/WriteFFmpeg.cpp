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
 * OFX ffmpegWriter plugin.
 * Writes a video output file using the libav library.
 */


#if (defined(_STDINT_H) || defined(_STDINT_H_) || defined(_MSC_STDINT_H_ ) ) && !defined(UINT64_C)
#warning "__STDC_CONSTANT_MACROS has to be defined before including <stdint.h>, this file will probably not compile."
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // ...or stdint.h wont' define UINT64_C, needed by libavutil
#endif

#include <cstdio>
#include <cstring> // strncpy
#include <cfloat> // DBL_MAX
#include <sstream>
#include <algorithm>
#include <string>
#include <cctype> // ::tolower
#ifdef DEBUG
#include <cstdio>
#define DBG(x) x
#else
#define DBG(x) (void)0
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#    define NOMINMAX 1
// windows - defined for both Win32 and Win64
#    include <windows.h> // for GetSystemInfo()
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#else
#  include <unistd.h> // for sysconf()
#  include <time.h>
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
#include "ofxsMacros.h"


#ifdef OFX_IO_USING_OCIO
#include "GenericOCIO.h"
#endif
#include "GenericWriter.h"
#include "FFmpegFile.h"

#define OFX_FFMPEG_PRINT_CODECS 0 // print list of supported/ignored codecs and formats
#define OFX_FFMPEG_TIMECODE 0     // timecode support
#define OFX_FFMPEG_AUDIO 0        // audio support
#define OFX_FFMPEG_MBDECISION 0   // add the macroblock decision parameter
#define OFX_FFMPEG_PRORES 1       // experimental apple prores support
#define OFX_FFMPEG_PRORES4444 1   // experimental apple prores 4444 support
#define OFX_FFMPEG_DNXHD 1        // experimental DNxHD support (disactivated, because of unsolved color shifting issues)

#if OFX_FFMPEG_PRINT_CODECS
#include <iostream>
#endif

#include "ofxsMultiThread.h"
#ifdef OFX_USE_MULTITHREAD_MUTEX
namespace {
typedef OFX::MultiThread::Mutex Mutex;
typedef OFX::MultiThread::AutoMutex AutoMutex;
}
#else
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
namespace {
typedef tthread::fast_mutex Mutex;
typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
}
#endif

using namespace OFX;
using namespace OFX::IO;

using std::string;
using std::stringstream;
using std::vector;
using std::map;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "WriteFFmpeg"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription \
    "Write images using FFmpeg.\n" \
    "The general recommendation is to write either separate frames (using WriteOIIO), " \
    "or an uncompressed video format, or a \"digital intermediate\" format (ProRes, DNxHD), " \
    "and to transcode the output and mux with audio with a separate tool (such as the ffmpeg or mencoder " \
    "command-line tools).\n" \
    "Further information can be found at https://trac.ffmpeg.org/wiki/Encode/VFX"

#define kPluginIdentifier "fr.inria.openfx.WriteFFmpeg"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 0 // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha false
#define kSupportsXY false

#define kParamFormat "format"
#define kParamFormatLabel "Format"
#define kParamFormatHint "Output format/container."

#define kParamCodec "codec"
#define kParamCodecName "Codec"
#define kParamCodecHint "Output codec used for encoding. " \
    "The general recommendation is to write either separate frames (using WriteOIIO), " \
    "or an uncompressed video format, or a \"digital intermediate\" format (ProRes, DNxHD), " \
    "and to transcode the output and mux with audio with a separate tool (such as the ffmpeg or mencoder " \
    "command-line tools).\n" \
    "The FFmpeg encoder codec name is given between brackets at the end of each codec description.\n" \
    "Please refer to the FFmpeg documentation http://ffmpeg.org/ffmpeg-codecs.html for codec options."

// a string param holding the short name of the codec (used to disambiguiate the codec choice when using different versions of FFmpeg)
#define kParamCodecShortName "codecShortName"
#define kParamCodecShortNameLabel "Codec Name"
#define kParamCodecShortNameHint "The codec used when the writer was configured. If this parameter is visible, this means that this codec may not be supported by this version of the plugin."

#define kParamFPS "fps"
#define kParamFPSLabel "FPS"
#define kParamFPSHint "File frame rate"

#define kParamResetFPS "resetFps"
#define kParamResetFPSLabel "Reset FPS"
#define kParamResetFPSHint "Reset FPS from the input FPS."

#if OFX_FFMPEG_TIMECODE
#define kParamWriteTimeCode "writeTimeCode"
#define kParamWriteTimeCodeLabel "Write Time Code"
#define kParamWriteTimeCodeHint \
    "Add a time code track to the generated QuickTime file. " \
    "This requires the presence of the \"input/timecode\" key in " \
    "the metadata. It is possible to give the time code track its reel name " \
    "though the \"quicktime/reel\" key in metadata. This is automatically " \
    "read from any QuickTime file already containing a time code track and " \
    "propagated through the tree. If this is not present the reel name will " \
    "be written blank. Use the ModifyMetaData node to add it.\n" \
    "If the timecode is missing, the track will not be written."
#endif

#define kParamAdvanced "advanced"
#define kParamAdvancedLabel "Advanced"

#define kParamEnableAlpha "enableAlpha"
#define kParamEnableAlphaLabel "Enable Alpha"
#define kParamEnableAlphaHint \
    "Write alpha channel to the video file (if supported by the codec)."

#define kParamBitrate "bitrateMbps"
#define kParamBitrateLabel "Bitrate"
#define kParamBitrateHint \
    "The target bitrate the codec will attempt to reach (in Megabits/s), within the confines of the bitrate tolerance and " \
    "quality min/max settings. Only supported by certain codecs (e.g. hev1, m2v1, MP42, 3IVD, but not mp4v, avc1 or H264).\n" \
    "Option -b in ffmpeg (multiplied by 1000000)."
#define kParamBitrateDefault 185
#define kParamBitrateMax 4000

#define kParamBitrateTolerance "bitrateToleranceMbps"
#define kParamBitrateToleranceLabel "Bitrate Tolerance"
#define kParamBitrateToleranceHint \
    "Set video bitrate tolerance (in Megabits/s). In 1-pass mode, bitrate " \
    "tolerance specifies how far ratecontrol is willing to deviate from " \
    "the target average bitrate value. This is not related to min/max " \
    "bitrate. Lowering tolerance too much has an adverse effect on " \
    "quality. " \
    "As a guideline, the minimum slider range of target bitrate/target fps is the lowest advisable setting. Anything below this value may result in failed renders." \
    "Only supported by certain codecs (e.g. MP42, 3IVD, but not avc1, hev1, m2v1, mp4v or H264).\n" \
    "Option -bt in ffmpeg (multiplied by 1000000)."

#define kParamQuality "quality"
#define kParamQualityLabel "Quality"
#define kParamQualityHint \
    "The quality range the codec is allowed to vary the image data quantiser " \
    "between to attempt to hit the desired bitrate. Higher values mean increased " \
    "image degradation is possible, but with the upside of lower bit rates. " \
    "Only supported by certain codecs (e.g. VP80, VP90, avc1, but not hev1 or mp4v).\n" \
    "Options -qmin and -qmax in ffmpeg."

#define kParamGopSize "gopSize"
#define kParamGopSizeLabel "GOP Size"
#define kParamGopSizeHint \
    "Specifies how many frames may be grouped together by the codec to form a compression GOP. Exercise caution " \
    "with this control as it may impact whether the resultant file can be opened in other packages. Only supported by " \
    "certain codecs.\n" \
    "Option -g in ffmpeg."

#define kParamBFrames "bFrames"
#define kParamBFramesLabel "B Frames"
#define kParamBFramesHint \
    "Controls the maximum number of B frames found consecutively in the resultant stream, where zero means no limit " \
    "imposed. Must be an integer between -1 and 16. 0 means that B-frames are disabled. If a value of -1 is used, it will choose an automatic value depending on the encoder. Only supported by certain codecs.\n" \
    "Option -bf in ffmpeg."

#define kParamWriteNCLC "writeNCLC"
#define kParamWriteNCLCLabel "Write NCLC"
#define kParamWriteNCLCHint \
    "Write nclc data in the colr atom of the video header. QuickTime only."

//Removed from panel - should never have been exposed as very low level control.
#if OFX_FFMPEG_MBDECISION
#define kParamMBDecision "mbDecision"
#endif

#define kProresCodec "prores_ks"
#define kProresProfileProxy 0
#define kProresProfileProxyName "Apple ProRes 422 Proxy"
#define kProresProfileProxyFourCC "apco"
#define kProresProfileLT 1
#define kProresProfileLTName "Apple ProRes 422 LT"
#define kProresProfileLTFourCC "apcs"
#define kProresProfileSQ 2
#define kProresProfileSQName "Apple ProRes 422"
#define kProresProfileSQFourCC "apcn"
#define kProresProfileHQ 3
#define kProresProfileHQName "Apple ProRes 422 HQ"
#define kProresProfileHQFourCC "apch"
#define kProresProfile4444 4
#define kProresProfile4444Name "Apple ProRes 4444"
#define kProresProfile4444FourCC "ap4h"
#define kProresProfile4444XQ 5
#define kProresProfile4444XQName "Apple ProRes 4444 XQ"
#define kProresProfile4444XQFourCC "ap4x"

#if OFX_FFMPEG_DNXHD
// Valid DNxHD profiles (as of FFmpeg 2.8.6):
// Frame size: 1920x1080p; bitrate: 175Mbps; pixel format: yuv422p10; framerate: 24000/1001
// Frame size: 1920x1080p; bitrate: 185Mbps; pixel format: yuv422p10; framerate: 25/1
// Frame size: 1920x1080p; bitrate: 365Mbps; pixel format: yuv422p10; framerate: 50/1
// Frame size: 1920x1080p; bitrate: 440Mbps; pixel format: yuv422p10; framerate: 60000/1001
// Frame size: 1920x1080p; bitrate: 115Mbps; pixel format: yuv422p; framerate: 24000/1001
// Frame size: 1920x1080p; bitrate: 120Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1920x1080p; bitrate: 145Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1920x1080p; bitrate: 240Mbps; pixel format: yuv422p; framerate: 50/1
// Frame size: 1920x1080p; bitrate: 290Mbps; pixel format: yuv422p; framerate: 60000/1001
// Frame size: 1920x1080p; bitrate: 175Mbps; pixel format: yuv422p; framerate: 24000/1001
// Frame size: 1920x1080p; bitrate: 185Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1920x1080p; bitrate: 220Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1920x1080p; bitrate: 365Mbps; pixel format: yuv422p; framerate: 50/1
// Frame size: 1920x1080p; bitrate: 440Mbps; pixel format: yuv422p; framerate: 60000/1001
// Frame size: 1920x1080i; bitrate: 185Mbps; pixel format: yuv422p10; framerate: 25/1
// Frame size: 1920x1080i; bitrate: 220Mbps; pixel format: yuv422p10; framerate: 30000/1001
// Frame size: 1920x1080i; bitrate: 120Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1920x1080i; bitrate: 145Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1920x1080i; bitrate: 185Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1920x1080i; bitrate: 220Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1280x720p; bitrate: 90Mbps; pixel format: yuv422p10; framerate: 24000/1001
// Frame size: 1280x720p; bitrate: 90Mbps; pixel format: yuv422p10; framerate: 25/1
// Frame size: 1280x720p; bitrate: 180Mbps; pixel format: yuv422p10; framerate: 50/1
// Frame size: 1280x720p; bitrate: 220Mbps; pixel format: yuv422p10; framerate: 60000/1001
// Frame size: 1280x720p; bitrate: 90Mbps; pixel format: yuv422p; framerate: 24000/1001
// Frame size: 1280x720p; bitrate: 90Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1280x720p; bitrate: 110Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1280x720p; bitrate: 180Mbps; pixel format: yuv422p; framerate: 50/1
// Frame size: 1280x720p; bitrate: 220Mbps; pixel format: yuv422p; framerate: 60000/1001
// Frame size: 1280x720p; bitrate: 60Mbps; pixel format: yuv422p; framerate: 24000/1001
// Frame size: 1280x720p; bitrate: 60Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1280x720p; bitrate: 75Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1280x720p; bitrate: 120Mbps; pixel format: yuv422p; framerate: 50/1
// Frame size: 1280x720p; bitrate: 145Mbps; pixel format: yuv422p; framerate: 60000/1001
// Frame size: 1920x1080p; bitrate: 36Mbps; pixel format: yuv422p; framerate: 24000/1001
// Frame size: 1920x1080p; bitrate: 36Mbps; pixel format: yuv422p; framerate: 25/1
// Frame size: 1920x1080p; bitrate: 45Mbps; pixel format: yuv422p; framerate: 30000/1001
// Frame size: 1920x1080p; bitrate: 75Mbps; pixel format: yuv422p; framerate: 50/1
// Frame size: 1920x1080p; bitrate: 90Mbps; pixel format: yuv422p; framerate: 60000/1001
// Frame size: 1920x1080p; bitrate: 350Mbps; pixel format: yuv422p10; framerate: 24000/1001
// Frame size: 1920x1080p; bitrate: 390Mbps; pixel format: yuv422p10; framerate: 25/1
// Frame size: 1920x1080p; bitrate: 440Mbps; pixel format: yuv422p10; framerate: 30000/1001
// Frame size: 1920x1080p; bitrate: 730Mbps; pixel format: yuv422p10; framerate: 50/1
// Frame size: 1920x1080p; bitrate: 880Mbps; pixel format: yuv422p10; framerate: 60000/1001
// Frame size: 960x720p; bitrate: 42Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 960x720p; bitrate: 60Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 960x720p; bitrate: 75Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 960x720p; bitrate: 115Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080p; bitrate: 63Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080p; bitrate: 84Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080p; bitrate: 100Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080p; bitrate: 110Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080i; bitrate: 80Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080i; bitrate: 90Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080i; bitrate: 100Mbps; pixel format: yuv422p; framerate: 0/0
// Frame size: 1440x1080i; bitrate: 110Mbps; pixel format: yuv422p; framerate: 0/0

#ifdef DNXHD_444
#pragma message WARN("This version of FFmpeg seems to support DNxHD 444")
#endif

//#define AVID_DNXHD_444_440X_NAME "DNxHD 444 10-bit 440Mbit"
#define AVID_DNXHD_422_440X_NAME "DNxHD 422 10-bit 440Mbit"
#define AVID_DNXHD_422_220X_NAME "DNxHD 422 10-bit 220Mbit"
#define AVID_DNXHD_422_220_NAME "DNxHD 422 8-bit 220Mbit"
#define AVID_DNXHD_422_145_NAME "DNxHD 422 8-bit 145Mbit"
#define AVID_DNXHD_422_36_NAME "DNxHD 422 8-bit 36Mbit"

#define kParamDNxHDCodecProfile "DNxHDCodecProfile"
#define kParamDNxHDCodecProfileLabel "DNxHD Codec Profile"
#define kParamDNxHDCodecProfileHint "Only for the Avid DNxHD codec, select the target bit rate for the encoded movie. The stream may be resized to 1920x1080 if resolution is not supported. Writing in thin-raster HDV format (1440x1080) is not supported by this plug-in, although FFmpeg supports it."
#define kParamDNxHDCodecProfileOption440x AVID_DNXHD_422_440X_NAME
#define kParamDNxHDCodecProfileOption440xHint "880x in 1080p/60 or 1080p/59.94, 730x in 1080p/50, 440x in 1080p/30, 390x in 1080p/25, 350x in 1080p/24"
#define kParamDNxHDCodecProfileOption220x AVID_DNXHD_422_220X_NAME
#define kParamDNxHDCodecProfileOption220xHint "440x in 1080p/60 or 1080p/59.94, 365x in 1080p/50, 220x in 1080i/60 or 1080i/59.94, 185x in 1080i/50 or 1080p/25, 175x in 1080p/24 or 1080p/23.976, 220x in 1080p/29.97, 220x in 720p/59.94, 175x in 720p/50"
#define kParamDNxHDCodecProfileOption220  AVID_DNXHD_422_220_NAME
#define kParamDNxHDCodecProfileOption220Hint  "440 in 1080p/60 or 1080p/59.94, 365 in 1080p/50, 220 in 1080i/60 or 1080i/59.94, 185 in 1080i/50 or 1080p/25, 175 in 1080p/24 or 1080p/23.976, 220 in 1080p/29.97, 220 in 720p/59.94, 175 in 720p/50"
#define kParamDNxHDCodecProfileOption145  AVID_DNXHD_422_145_NAME
#define kParamDNxHDCodecProfileOption145Hint  "290 in 1080p/60 or 1080p/59.94, 240 in 1080p/50, 145 in 1080i/60 or 1080i/59.94, 120 in 1080i/50 or 1080p/25, 115 in 1080p/24 or 1080p/23.976, 145 in 1080p/29.97, 145 in 720p/59.94, 115 in 720p/50"
#define kParamDNxHDCodecProfileOption36   AVID_DNXHD_422_36_NAME
#define kParamDNxHDCodecProfileOption36Hint   "90 in 1080p/60 or 1080p/59.94, 75 in 1080p/50, 45 in 1080i/60 or 1080i/59.94, 36 in 1080i/50 or 1080p/25, 36 in 1080p/24 or 1080p/23.976, 45 in 1080p/29.97, 100 in 720p/59.94, 85 in 720p/50"

enum DNxHDCodecProfileEnum
{
    eDNxHDCodecProfile440x,
    eDNxHDCodecProfile220x,
    eDNxHDCodecProfile220,
    eDNxHDCodecProfile145,
    eDNxHDCodecProfile36,
};

#define kParamDNxHDEncodeVideoRange "DNxHDEncodeVideoRange"
#define kParamDNxHDEncodeVideoRangeLabel "DNxHD Output Range"
#define kParamDNxHDEncodeVideoRangeHint \
    "When encoding using DNxHD this is used to select between full scale data range " \
    "and 'video/legal' data range.\nFull scale data range is 0-255 for 8-bit and 0-1023 for 10-bit. " \
    "'Video/legal' data range is a reduced range, 16-240 for 8-bit and 64-960 for 10-bit."
#define kParamDNxHDEncodeVideoRangeOptionFull "Full Range"
#define kParamDNxHDEncodeVideoRangeOptionVideo "Video Range"

#endif // if OFX_FFMPEG_DNXHD

#define STR_HELPER(x) # x
#define STR(x) STR_HELPER(x)

// The fourccs for MJPEGA, MJPEGB, and Photo JPEG
static const char*  kJpegCodecs[] = { "jpeg", "mjpa", "mjpb" };
static const int kNumJpegCodecs = sizeof(kJpegCodecs) / sizeof(kJpegCodecs[0]);
static bool
IsJpeg(ChoiceParam *codecParam,
       int codecValue)
{
    string strCodec;

    codecParam->getOption(codecValue, strCodec);

    // Check the fourcc for a JPEG codec
    for (int i = 0; i < kNumJpegCodecs; ++i) {
        const char* codec = kJpegCodecs[i];
        // All labels start with the fourcc.
        const bool startsWithCodec = (strCodec.find(codec) == 0);
        if (startsWithCodec) {
            return true;
        }
    }

    return false;
}

// check if codec is compatible with format.
// libavformat may not implemen query_codec for all formats
static bool
codecCompatible(const AVOutputFormat *ofmt,
                enum AVCodecID codec_id)
{
    string fmt = string(ofmt->name);

    return ( avformat_query_codec(ofmt, codec_id, FF_COMPLIANCE_NORMAL) == 1 ||
             ( fmt == "mxf" && (codec_id == AV_CODEC_ID_MPEG2VIDEO ||
                                codec_id == AV_CODEC_ID_DNXHD ||
                                codec_id == AV_CODEC_ID_DVVIDEO ||
                                codec_id == AV_CODEC_ID_H264) ) ||
             ( fmt == "mpegts" && (codec_id == AV_CODEC_ID_MPEG1VIDEO ||
                                   codec_id == AV_CODEC_ID_MPEG2VIDEO ||
                                   codec_id == AV_CODEC_ID_MPEG4 ||
                                   codec_id == AV_CODEC_ID_H264 ||
                                   codec_id == AV_CODEC_ID_HEVC ||
                                   codec_id == AV_CODEC_ID_CAVS ||
                                   codec_id == AV_CODEC_ID_DIRAC) ) ||
             ( fmt == "mpeg" && (codec_id == AV_CODEC_ID_MPEG1VIDEO ||
                                 codec_id == AV_CODEC_ID_H264) ) );
}

typedef map<string, string> CodecMap;
static CodecMap
CreateCodecKnobLabelsMap()
{
    CodecMap m;

    // Video codecs.
    m["avrp"]          = "AVrp\tAvid 1:1 10-bit RGB Packer";
    m["ayuv"]          = "AYUV\tUncompressed packed MS 4:4:4:4";
    m["cinepak"]       = "cvid\tCinepak"; // disabled in whitelist (bad quality)
#if OFX_FFMPEG_DNXHD
    m["dnxhd"]         = "AVdn\tVC3/DNxHD";
#endif
    m["ffv1"]          = "FFV1\tFFmpeg video codec #1";
    m["ffvhuff"]       = "FFVH\tHuffyuv FFmpeg variant";
    m["flv"]           = "FLV1\tFLV / Sorenson Spark / Sorenson H.263 (Flash Video)";
    m["gif"]           = "gif \tGIF (Graphics Interchange Format)";
    m["huffyuv"]       = "HFYU\tHuffYUV";
    m["jpeg2000"]      = "mjp2\tJPEG 2000"; // disabled in whitelist (bad quality)
    m["jpegls"]        = "MJLS\tJPEG-LS"; // disabled in whitelist
    m["libopenh264"]   = "H264\tCisco libopenh264 H.264/MPEG-4 AVC encoder";
    m["libschroedinger"] = "drac\tlibschroedinger Dirac";
    m["libtheora"]     = "theo\tlibtheora Theora";
    m["libvpx"]        = "VP80\tOn2 VP8"; // write doesn't work yet
    m["libvpx-vp9"]    = "VP90\tGoogle VP9"; // disabled in whitelist (bad quality)
    m["libx264"]       = "avc1\tH.264 / AVC / MPEG-4 AVC / MPEG-4 part 10";
    m["libx264rgb"]    = "avc1\tH.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 RGB";
    m["libx265"]       = "hev1\tH.265 / HEVC (High Efficiency Video Coding)"; // disabled in whitelist (does not work will all sizes)

    m["ljpeg"]         = "LJPG\tLossless JPEG"; // disabled in whitelist
    m["mjpeg"]         = "jpeg\tMotion JPEG";
    m["mpeg1video"]    = "m1v \tMPEG-1 Video"; // disabled in whitelist (random blocks)
    m["mpeg2video"]    = "m2v1\tMPEG-2 Video";
    m["mpeg4"]         = "mp4v\tMPEG-4 Video";
    m["msmpeg4v2"]     = "MP42\tMPEG-4 part 2 Microsoft variant version 2";
    m["msmpeg4"]       = "3IVD\tMPEG-4 part 2 Microsoft variant version 3";
    m["png"]           = "png \tPNG (Portable Network Graphics) image";
    m["qtrle"]         = "rle \tQuickTime Animation (RLE) video";
    m["r10k"]          = "R10k\tAJA Kona 10-bit RGB Codec"; // disabled in whitelist
    m["r210"]          = "r210\tUncompressed RGB 10-bit"; // disabled in whitelist
    m["rawvideo"]      = "RGBx\tUncompressed 4:2:2 8-bit"; // actual fourcc is RGB^x
    m["svq1"]          = "SVQ1\tSorenson Vector Quantizer 1 / Sorenson Video 1 / SVQ1";
    m["targa"]         = "tga \tTruevision Targa image";
    m["tiff"]          = "tiff\tTIFF image"; // disabled in whitelist
    m["v210"]          = "v210\tUncompressed 10-bit 4:2:2";
    m["v308"]          = "v308\tUncompressed 8-bit 4:4:4";
    m["v408"]          = "v308\tUncompressed 8-bit QT 4:4:4:4";
    m["v410"]          = "v410\tUncompressed 4:4:4 10-bit"; // disabled in whitelist
    m["vc2"]           = "drac\tSMPTE VC-2 (previously BBC Dirac Pro)";

    // add the FFmpeg encoder name at the end of each codec description
    for (CodecMap::iterator it = m.begin(); it != m.end(); ++it) {
        it->second += " [";
        it->second += it->first;
        it->second += ']';
    }

    return m;
} // CreateCodecKnobLabelsMap

// MPEG-2 codecs (non-SD)
#if 0

// HDV
m["hdv1"]          = "MPEG2 HDV 720p30";
m["hdv2"]          = "MPEG2 HDV 1080i60";
m["hdv3"]          = "MPEG2 HDV 1080i50";
m["hdv4"]          = "MPEG2 HDV 720p24";
m["hdv5"]          = "MPEG2 HDV 720p25";
m["hdv6"]          = "MPEG2 HDV 1080p24";
m["hdv7"]          = "MPEG2 HDV 1080p25";
m["hdv8"]          = "MPEG2 HDV 1080p30";
m["hdv9"]          = "MPEG2 HDV 720p60 JVC";
m["hdva"]          = "MPEG2 HDV 720p50";

// XDCAM
m["xdv1"]          = "XDCAM EX 720p30 35Mb/s";
m["xdv2"]          = "XDCAM HD 1080i60 35Mb/s";
m["xdv3"]          = "XDCAM HD 1080i50 35Mb/s";
m["xdv4"]          = "XDCAM EX 720p24 35Mb/s";
m["xdv5"]          = "XDCAM EX 720p25 35Mb/s";
m["xdv6"]          = "XDCAM HD 1080p24 35Mb/s";
m["xdv7"]          = "XDCAM HD 1080p25 35Mb/s";
m["xdv8"]          = "XDCAM HD 1080p30 35Mb/s";
m["xdv9"]          = "XDCAM EX 720p60 35Mb/s";
m["xdva"]          = "XDCAM EX 720p50 35Mb/s";

m["xdvb"]          = "XDCAM EX 1080i60 50Mb/s CBR";
m["xdvc"]          = "XDCAM EX 1080i50 50Mb/s CBR";
m["xdvd"]          = "XDCAM EX 1080p24 50Mb/s CBR";
m["xdve"]          = "XDCAM EX 1080p25 50Mb/s CBR";
m["xdvf"]          = "XDCAM EX 1080p30 50Mb/s CBR";

m["xd51"]          = "XDCAM HD422 720p30 50Mb/s CBR";
m["xd54"]          = "XDCAM HD422 720p24 50Mb/s CBR";
m["xd55"]          = "XDCAM HD422 720p25 50Mb/s CBR";
m["xd59"]          = "XDCAM HD422 720p60 50Mb/s CBR";
m["xd5a"]          = "XDCAM HD422 720p50 50Mb/s CBR";
m["xd5b"]          = "XDCAM HD422 1080i60 50Mb/s CBR";
m["xd5c"]          = "XDCAM HD422 1080i50 50Mb/s CBR";
m["xd5d"]          = "XDCAM HD422 1080p24 50Mb/s CBR";
m["xd5e"]          = "XDCAM HD422 1080p25 50Mb/s CBR";
m["xd5f"]          = "XDCAM HD422 1080p30 50Mb/s CBR";

m["xdhd"]          = "XDCAM HD 540p";
m["xdh2"]          = "XDCAM HD422 540p";

#endif

static const CodecMap kCodecKnobLabels = CreateCodecKnobLabelsMap();
static
const char*
getCodecKnobLabel(const char* codecShortName)
{
    CodecMap::const_iterator it = kCodecKnobLabels.find( string(codecShortName) );
    if ( it != kCodecKnobLabels.end() ) {
        return it->second.c_str();
    } else {
        return NULL;
    }
}

static
const char*
getCodecFromShortName(const string& name)
{
    string prefix(kProresCodec);

    if ( !name.compare(0, prefix.size(), prefix) ) {
        return kProresCodec;
    }

    return name.c_str();
}

static
int
getProfileFromShortName(const string& name)
{
    string prefix(kProresCodec);

    if ( !name.compare(0, prefix.size(), prefix) ) {
        if ( !name.compare(prefix.size(), string::npos, kProresProfile4444FourCC) ) {
            return kProresProfile4444;
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileHQFourCC) ) {
            return kProresProfileHQ;
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileSQFourCC) ) {
            return kProresProfileSQ;
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileLTFourCC) ) {
            return kProresProfileLT;
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileProxyFourCC) ) {
            return kProresProfileProxy;
        }
    }

    return -1;
}

// see libavcodec/proresenc_kostya.c for the list of profiles
static
const char*
getProfileStringFromShortName(const string& name)
{
    string prefix(kProresCodec);

    if ( !name.compare(0, prefix.size(), prefix) ) {
        if ( !name.compare(prefix.size(), string::npos, kProresProfile4444FourCC) ) {
            return "4444";
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileHQFourCC) ) {
            return "hq";
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileSQFourCC) ) {
            return "standard";
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileLTFourCC) ) {
            return "lt";
        }
        if ( !name.compare(prefix.size(), string::npos, kProresProfileProxyFourCC) ) {
            return "proxy";
        }
    }

    return "auto";
}

////////////////////////////////////////////////////////////////////////////////
// MyAVFrame
// Wrapper class for the FFmpeg AVFrame structure.
// In order negate the chances of memory leaks when using FFmpeg this class
// provides the initialisation, allocation and release of memory associated
// with an AVFrame structure.
//
// Note that this has been designed to be a drop in replacement for host
// managed memory for colourspace, pixel format and sample format conversions.
// It is not designed to be used with avcodec_decode_video2 as the underlying
// decoder usually manages memory.
//
// Example usage:
//
// Audio
//
//    MyAVFrame avFrame;
//    ret = avFrame.alloc(channels, nb_samples, _avSampleFormat, 1);
//    :
//    ret = swr_convert(_swrContext, avFrame->data, ...);
//    :
//    ret = avcodec_encode_audio2(avCodecContext, &pkt, avFrame, &gotPacket);
//    :
//
// Video
//
//    MyAVFrame avFrame;
//    ret = avFrame.alloc(width(), height(), pixelFormatCodec, 1);
//    if (!ret) {
//      :
//      sws_scale(convertCtx, ..., avFrame->data, avFrame->linesize);
//      :
//    }
//
// IMPORTANT
// This class has been purposefully designed NOT to have parameterised
// constructors or assignment operators. The reason for this is due to the
// complexity of managing the lifetime of the structure and its associated
// memory buffers.
//
class MyAVFrame
{
public:
    ////////////////////////////////////////////////////////////////////////////////
    // Ctor. Initialise the AVFrame structure.
    MyAVFrame()
        : _avFrame(NULL)
    {
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Dtor. Release all resources. Release the AVFrame buffers and the
    // AVFrame structure.
    ~MyAVFrame();

    ////////////////////////////////////////////////////////////////////////////////
    // alloc
    // VIDEO SPECIFIC.
    // Allocate a buffer or buffers for the AVFrame structure. How many
    // buffers depends upon |avPixelFormat| which is a VIDEO format.
    //
    // @param width Frame width in pixels.
    // @param height Frame height in pixels.
    // @param avPixelFormat An AVPixelFormat enumeration for the frame pixel format.
    // @param align Buffer byte alignment.
    //
    // @return 0 if successful.
    //         <0 otherwise.
    //
    int alloc(int width, int height, enum AVPixelFormat avPixelFormat, int align);

    ////////////////////////////////////////////////////////////////////////////////
    // alloc
    // AUDIO SPECIFIC.
    // Allocate a buffer or buffers for the AVFrame structure. How many
    // buffers depends upon |avSampleFormat| which is an AUDIO format.
    //
    // @param nbChannels The number of audio channels.
    // @param nbSamples The number of audio samples that the buffer will hold.
    // @param avSampleFormat An AVSampleFormat enumeration for the audio format.
    // @param align Buffer byte alignment.
    //
    // @return 0 if successful.
    //         <0 otherwise.
    //
    int alloc(int nbChannels, int nbSamples, enum AVSampleFormat avSampleFormat, int align);

    // operator dereference overload.
    AVFrame* operator->() const { return _avFrame; }

    // operator type cast overload.
    operator AVFrame*() { return _avFrame; }

private:
    AVFrame* _avFrame;

    // Release any memory allocated to the data member variable of
    // the AVFrame structure.
    void deallocateAVFrameData();

    // Probably do not need the following or the headaches
    // of trying to support the functionality.
    // Hide the copy constuctor. Who would manage memory?
    MyAVFrame(MyAVFrame& avFrame);
    // Hide the assignment operator. Who would manage memory?
    MyAVFrame& operator=(const MyAVFrame& /* rhs*/) { return *this; }
};

////////////////////////////////////////////////////////////////////////////////
// MyAVFrame
// Wrapper class for the ffmpeg AVFrame structure.
// In order negate the chances of memory leaks when using ffmpeg this class
// provides the initialisation, allocation and release of memory associated
// with an AVFrame structure.
//
// Note that this has been designed to be a drop in replacement for host
// managed memory for colourspace, pixel format and sample format conversions.
// It is not designed to be used with avcodec_decode_video2 as the underlying
// decoder usually manages memory.
//
// Example usage:
//
// Audio
//
//    MyAVFrame avFrame;
//    ret = avFrame.alloc(channels, nb_samples, _avSampleFormat, 1);
//    :
//    ret = swr_convert(_swrContext, avFrame->data, ...);
//    :
//    ret = avcodec_encode_audio2(avCodecContext, &pkt, avFrame, &gotPacket);
//    :
//
// Video
//
//    MyAVFrame avFrame;
//    ret = avFrame.alloc(width(), height(), pixelFormatCodec, 1);
//    if (!ret) {
//      :
//      sws_scale(convertCtx, ..., avFrame->data, avFrame->linesize);
//      :
//    }
//
// IMPORTANT
// This class has been purposefully designed NOT to have parameterised
// constructors or assignment operators. The reason for this is due to the
// complexity of managing the lifetime of the structure and its associated
// memory buffers.
//
MyAVFrame::~MyAVFrame()
{
    // The most important part of this class.
    // Two deallocations may be required, one from the AVFrame structure
    // and one for any image data, AVFrame::data.
    deallocateAVFrameData();
    av_frame_free(&_avFrame);
}

////////////////////////////////////////////////////////////////////////////////
// alloc
// VIDEO SPECIFIC.
// Allocate a buffer or buffers for the AVFrame structure. How many
// buffers depends upon |avPixelFormat| which is a VIDEO format.
//
// @param width Frame width in pixels.
// @param height Frame height in pixels.
// @param avPixelFormat An AVPixelFormat enumeration for the frame pixel format.
// @param align Buffer byte alignment.
//
// @return 0 if successful.
//         <0 otherwise.
//
int
MyAVFrame::alloc(int width,
                 int height,
                 enum AVPixelFormat avPixelFormat,
                 int align)
{
    int ret = 0;

    if (!_avFrame) {
        _avFrame = av_frame_alloc();
    }
    if (_avFrame) {
        deallocateAVFrameData(); // In case this method is called multiple times on the same object.
        // av_image_alloc will return the size of the buffer in bytes if successful,
        // otherwise it will return <0.
        int bufferSize = av_image_alloc(_avFrame->data, _avFrame->linesize, width, height, avPixelFormat, align);
        if (bufferSize > 0) {
            // Set the frame fields for a video buffer as some
            // encoders rely on them, e.g. Lossless JPEG.
            _avFrame->width = width;
            _avFrame->height = height;
            _avFrame->format = (int)avPixelFormat;
            ret = 0;
        } else {
            ret = -1;
        }
    } else {
        // Failed to allocate an AVFrame.
        ret = -2;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// alloc
// AUDIO SPECIFIC.
// Allocate a buffer or buffers for the AVFrame structure. How many
// buffers depends upon |avSampleFormat| which is an AUDIO format.
//
// @param nbChannels The number of audio channels.
// @param nbSamples The number of audio samples that the buffer will hold.
// @param avSampleFormat An AVSampleFormat enumeration for the audio format.
// @param align Buffer byte alignment.
//
// @return 0 if successful.
//         <0 otherwise.
//
int
MyAVFrame::alloc(int nbChannels,
                 int nbSamples,
                 enum AVSampleFormat avSampleFormat,
                 int align)
{
    int ret = 0;

    if (!_avFrame) {
        _avFrame = av_frame_alloc();
    }
    if (_avFrame) {
        deallocateAVFrameData(); // In case this method is called multiple times on the same object.
        // av_samples_alloc will return >= if successful, otherwise it will return <0.
        ret = av_samples_alloc(_avFrame->data, _avFrame->linesize, nbChannels, nbSamples, avSampleFormat, align);
        if (ret >= 0) {
            // Set the frame fields for an audio buffer as some
            // encoders rely on them.
            _avFrame->nb_samples = nbSamples;
            _avFrame->format = (int)avSampleFormat;
            ret = 0;
        } else {
            ret = -1;
        }
    } else {
        // Failed to allocate an AVFrame.
        ret = -2;
    }

    return ret;
}

// Release any memory allocated to the data member variable of
// the AVFrame structure.
void
MyAVFrame::deallocateAVFrameData()
{
    if (_avFrame && _avFrame->data[0]) {
        av_freep(_avFrame->data);
    }
}

////////////////////////////////////////////////////////////////////////////////
// MyAVPicture
// Wrapper class for the FFmpeg AVPicture structure.
// In order negate the chances of memory leaks when using FFmpeg this class
// provides the initialisation, allocation and release of memory associated
// with an AVPicture structure.
//
// Example usage:
//
//    MyAVPicture avPicture;
//    ret = avPicture.alloc(width(), height(), pixelFormatCodec);
//    if (!ret) {
//      :
//    }
//
// IMPORTANT
// This class has been purposefully designed NOT to have parameterised
// constructors or assignment operators. The reason for this is due to the
// complexity of managing the lifetime of the structure and its associated
// memory buffers.
//
class MyAVPicture
{
public:
    ////////////////////////////////////////////////////////////////////////////////
    // Ctor. Initialise the AVPicture structure.
    MyAVPicture()
    {
        for (int i = 0; i < 4; ++i) {
            data[i] = NULL;
            linesize[i] = 0;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Dtor. Release all resources. Release the AVPicture buffers and the
    // AVPicture structure.
    ~MyAVPicture();

    ////////////////////////////////////////////////////////////////////////////////
    // alloc
    // VIDEO SPECIFIC.
    // Allocate a buffer or buffers for the AVPicture structure. How many
    // buffers depends upon |avPixelFormat| which is a VIDEO format.
    //
    // @param width Frame width in pixels.
    // @param height Frame height in pixels.
    // @param avPixelFormat An AVPixelFormat enumeration for the frame pixel format.
    // @param align Buffer byte alignment.
    //
    // @return 0 if successful.
    //         <0 otherwise.
    //
    int alloc(int width, int height, enum AVPixelFormat avPixelFormat);

#if 1
    uint8_t *data[4];    ///< pointers to the image data planes
    int linesize[4];     ///< number of bytes per line
#else
    // operator dereference overload.
    AVPicture* operator->() { return &_avPicture; }

    // operator type cast overload.
    operator AVPicture*() { return &_avPicture; }

private:
    AVPicture _avPicture;
#endif

private:
    // Release any memory allocated to the data member variable of
    // the AVPicture structure.
    void deallocateAVPictureData();

    // Probably do not need the following or the headaches
    // of trying to support the functionality.
    // Hide the copy constuctor. Who would manage memory?
    MyAVPicture(MyAVPicture& avPicture);
    // Hide the assignment operator. Who would manage memory?
    MyAVPicture& operator=(const MyAVPicture& /*rhs*/) { return *this; }
};

////////////////////////////////////////////////////////////////////////////////
// MyAVPicture
// Wrapper class for the ffmpeg AVPicture structure.
// In order negate the chances of memory leaks when using ffmpeg this class
// provides the initialisation, allocation and release of memory associated
// with an AVPicture structure.
//
// Example usage:
//
//    MyAVPicture avPicture;
//    ret = avPicture.alloc(width(), height(), pixelFormatCodec);
//    if (!ret) {
//      :
//    }
//
// IMPORTANT
// This class has been purposefully designed NOT to have parameterised
// constructors or assignment operators. The reason for this is due to the
// complexity of managing the lifetime of the structure and its associated
// memory buffers.
//
MyAVPicture::~MyAVPicture()
{
    // The most important part of this class.
    deallocateAVPictureData();
}

////////////////////////////////////////////////////////////////////////////////
// alloc
// VIDEO SPECIFIC.
// Allocate a buffer or buffers for the AVPicture structure. How many
// buffers depends upon |avPixelFormat| which is a VIDEO format.
//
// @param width Frame width in pixels.
// @param height Frame height in pixels.
// @param avPixelFormat An AVPixelFormat enumeration for the frame pixel format.
// @param align Buffer byte alignment.
//
// @return 0 if successful.
//         <0 otherwise.
//
int
MyAVPicture::alloc(int width,
                   int height,
                   enum AVPixelFormat avPixelFormat)
{
    deallocateAVPictureData(); // In case this method is called multiple times on the same object.
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(51, 63, 100) // https://ffmpeg.org/pipermail/ffmpeg-cvslog/2015-October/094884.html
    int ret = av_image_alloc(data, linesize, width, height, avPixelFormat, 1);
    if (ret > 0) { // av_image_alloc returns bytes on success
        ret = 0;
    }
#else
    int ret = avpicture_alloc(&_avPicture, avPixelFormat, width, height);
#endif

    return ret;
}

// Release any memory allocated to the data member variable of
// the AVPicture structure.
void
MyAVPicture::deallocateAVPictureData()
{
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(51, 63, 100) // https://ffmpeg.org/pipermail/ffmpeg-cvslog/2015-October/094884.html
    if (data[0]) {
        av_freep(&data[0]);
        for (int i = 0; i < 4; ++i) {
            data[i] = NULL;
            linesize[i] = 0;
        }
    }
#else
    if (_avPicture.data[0]) {
        avpicture_free(&_avPicture);
        for (int i = 0; i < 4; ++i) {
            _avPicture.data[i] = NULL;
            _avPicture.linesize[i] = 0;
        }
    }
#endif
}

class WriteFFmpegPlugin
    : public GenericWriterPlugin
{
private:
    enum WriterError { SUCCESS = 0, IGNORE_FINISH, CLEANUP };

public:

    WriteFFmpegPlugin(OfxImageEffectHandle handle, const vector<string>& extensions);

    virtual ~WriteFFmpegPlugin();

private:

    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;
    virtual void onOutputFileChanged(const string &filename, bool setColorSpace) OVERRIDE FINAL;

    /** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
    virtual void beginEdit(void) OVERRIDE FINAL;
    virtual void beginEncode(const string& filename, const OfxRectI& rodPixel, float pixelAspectRatio, const BeginSequenceRenderArguments& args) OVERRIDE FINAL;
    virtual void endEncode(const EndSequenceRenderArguments& args) OVERRIDE FINAL;
    virtual void encode(const string& filename,
                        const OfxTime time,
                        const string& viewName,
                        const float *pixelData,
                        const OfxRectI& bounds,
                        const float pixelAspectRatio,
                        const int pixelDataNComps,
                        const int dstNCompsStartIndex,
                        const int dstNComps,
                        const int rowBytes) OVERRIDE FINAL;
    virtual bool isImageFile(const string& fileExtension) const OVERRIDE FINAL;
    virtual void setOutputFrameRate(double fps) OVERRIDE FINAL;
    virtual PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return eImageUnPreMultiplied; }

private:
    void updateVisibility();
    void checkCodec();
    void freeFormat();
    void getColorInfo(AVColorPrimaries *color_primaries, AVColorTransferCharacteristic *color_trc) const;
    AVPixelFormat                 getPixelFormat(AVCodec* videoCodec) const;
    AVOutputFormat*               initFormat(bool reportErrors) const;
    bool                          initCodec(AVOutputFormat* fmt, AVCodecID& outCodecId, AVCodec*& outCodec) const;

    void getPixelFormats(AVCodec*          videoCodec,
                         AVPixelFormat&    outNukeBufferPixelFormat,
                         AVPixelFormat&    outTargetPixelFormat,
                         int&              outBitDepth) const;

    void updateBitrateToleranceRange();
    bool isRec709Format(const int height) const;
    static bool IsYUV(AVPixelFormat pixelFormat);
    static bool IsYUVFromShortName(const char* shortName, int codecProfile);
    static bool IsRGBFromShortName(const char* shortName, int codecProfile);
    static int            GetPixelFormatBitDepth(const AVPixelFormat pixelFormat);
    static AVPixelFormat  GetPixelFormatFromBitDepth(const int bitDepth, const bool hasAlpha);
    static void           GetCodecSupportedParams(AVCodec* codec, bool& outBitrateParam, bool& outBitrateTolParam, bool& outQualityParams, bool& outInterGOPParams, bool& outInterBParams);

    void configureAudioStream(AVCodec* avCodec, AVStream* avStream);
    void configureVideoStream(AVCodec* avCodec, AVStream* avStream);
    void configureTimecodeStream(AVCodec* avCodec, AVStream* avStream);
    AVStream* addStream(AVFormatContext* avFormatContext, enum AVCodecID avCodecId, AVCodec** pavCodec);
    int openCodec(AVFormatContext* avFormatContext, AVCodec* avCodec, AVStream* avStream);
    int writeAudio(AVFormatContext* avFormatContext, AVStream* avStream, bool flush);
    int writeVideo(AVFormatContext* avFormatContext, AVStream* avStream, bool flush, double time, const float *pixelData = NULL, const OfxRectI* bounds = NULL, int pixelDataNComps = 0, int dstNComps = 0, int rowBytes = 0);
    int writeToFile(AVFormatContext* avFormatContext, bool finalise, double time, const float *pixelData = NULL, const OfxRectI* bounds = NULL, int pixelDataNComps = 0, int dstNComps = 0, int rowBytes = 0);

    int colourSpaceConvert(MyAVPicture* avPicture, AVFrame* avFrame, AVPixelFormat srcPixelFormat, AVPixelFormat dstPixelFormat, AVCodecContext* avCodecContext);

    // Returns true if the selected channels contain alpha and that the channel is valid
    bool alphaEnabled() const;

    // Returns the nmber of destination channels this will write into.
    int numberOfDestChannels() const;

    bool codecIndexIsInRange( unsigned int codecIndex) const;
    bool codecIsDisallowed( const string& codecShortName, string& reason ) const;

    ///These members are not protected and only read/written by/to by the same thread.
    string _filename;
    OfxRectI _rodPixel;
    float _pixelAspectRatio;
    bool _isOpen; // Flag for the configuration state of the FFmpeg components.
    WriterError _error;
    AVFormatContext*  _formatContext;
    AVStream* _streamVideo;
    AVStream* _streamAudio;
    AVStream* _streamTimecode;
    Mutex _nextFrameToEncodeMutex;
    int _nextFrameToEncode; //< the frame index we need to encode next, INT_MIN means uninitialized
    int _firstFrameToEncode;
    int _lastFrameToEncode;
    int _frameStep;
    ChoiceParam* _format;
    DoubleParam* _fps;
#ifdef OFX_FFMPEG_DNXHD
    ChoiceParam* _dnxhdCodecProfile;
    ChoiceParam* _encodeVideoRange;
#endif
#if OFX_FFMPEG_TIMECODE
    BooleanParam* _writeTimeCode;
#endif

    ChoiceParam* _codec;
    StringParam* _codecShortName;
    BooleanParam* _enableAlpha;
    DoubleParam* _bitrate;
    DoubleParam* _bitrateTolerance;
    Int2DParam* _quality;
    IntParam* _gopSize;
    IntParam* _bFrames;
    BooleanParam* _writeNCLC;
#if OFX_FFMPEG_MBDECISION
    ChoiceParam* _mbDecision;
#endif

    // Used in writeVideo as a contiguous buffer. The size of the buffer remains throughout
    // the encoding of the whole video. Since the plug-in is instanceSafe, we do not need to lock it
    // since 2 renders will never use it at the same time.
    // We do not use a vector<uint8_t> here because of unnecessary initialization
    // http://stackoverflow.com/questions/17347254/why-is-allocation-and-deallocation-of-stdvector-slower-than-dynamic-array-on-m
    uint8_t* _scratchBuffer;
    std::size_t _scratchBufferSize;
};


class FFmpegSingleton
{
public:

    static FFmpegSingleton &Instance()
    {
        return m_instance;
    };
    const vector<string>& getFormatsShortNames() const { return _formatsShortNames; }

    const vector<string>& getFormatsLongNames() const { return _formatsLongNames; }

    const vector<vector<size_t> >& getFormatsCodecs() const { return _formatsCodecs; }

    const vector<string>& getCodecsShortNames() const { return _codecsShortNames; }

    const vector<string>& getCodecsLongNames() const { return _codecsLongNames; }

    const vector<string>& getCodecsKnobLabels() const { return _codecsKnobLabels; }

    const vector<AVCodecID>& getCodecsIds() const { return _codecsIds; }

    const vector<vector<size_t> >& getCodecsFormats() const { return _codecsFormats; }

private:

    FFmpegSingleton &operator= (const FFmpegSingleton &)
    {
        return *this;
    }

    FFmpegSingleton(const FFmpegSingleton &) {}

    static FFmpegSingleton m_instance;

    FFmpegSingleton();

    ~FFmpegSingleton();


    vector<string> _formatsLongNames;
    vector<string> _formatsShortNames;
    vector<vector<size_t> > _formatsCodecs; // for each format, give the list of compatible codecs (indices in the codecs list)
    vector<string> _codecsLongNames;
    vector<string> _codecsShortNames;
    vector<string> _codecsKnobLabels;
    vector<AVCodecID>   _codecsIds;
    vector<vector<size_t> > _codecsFormats; // for each codec, give the list of compatible formats (indices in the formats list)
};

FFmpegSingleton FFmpegSingleton::m_instance = FFmpegSingleton();

FFmpegSingleton::FFmpegSingleton()
{
    // TODO: add a log buffer and a way to display it / clear it.
    av_log_set_level(AV_LOG_WARNING);
    //av_log_set_level(AV_LOG_DEBUG);
    avcodec_register_all();
    av_register_all();

    _formatsLongNames.push_back("guess from filename");
    _formatsShortNames.push_back("default");
    AVOutputFormat* fmt = av_oformat_next(NULL);
    while (fmt) {
        if (fmt->video_codec != AV_CODEC_ID_NONE) { // if this is a video format, it should have a default video codec
            if ( FFmpegFile::isFormatWhitelistedForWriting( fmt->name ) ) {
                if (fmt->long_name) {
                    _formatsLongNames.push_back( string(fmt->long_name) + string(" [") + string(fmt->name) + string("]") );
                } else {
                    _formatsLongNames.push_back(fmt->name);
                }
                _formatsShortNames.push_back(fmt->name);
#                 if OFX_FFMPEG_PRINT_CODECS
                std::cout << "Format: " << fmt->name << " = " << fmt->long_name << std::endl;
#                 endif //  FFMPEG_PRINT_CODECS
            }
#         if OFX_FFMPEG_PRINT_CODECS
            else {
                std::cout << "Disallowed Format: " << fmt->name << " = " << fmt->long_name << std::endl;
            }
#         endif //  FFMPEG_PRINT_CODECS
        }
        fmt = av_oformat_next(fmt);
    }
    assert( _formatsLongNames.size() == _formatsShortNames.size() );

#if OFX_FFMPEG_PRORES
    // Apple ProRes support.
    // short name must start with prores_ap
    // knoblabel must start with FourCC
#if OFX_FFMPEG_PRORES4444
    _codecsShortNames.push_back(kProresCodec kProresProfile4444FourCC);
    _codecsLongNames.push_back              (kProresProfile4444Name);
    _codecsKnobLabels.push_back             (kProresProfile4444FourCC "\t"kProresProfile4444Name);
    _codecsIds.push_back                    (AV_CODEC_ID_PRORES);
#endif
    _codecsShortNames.push_back(kProresCodec kProresProfileHQFourCC);
    _codecsLongNames.push_back              (kProresProfileHQName);
    _codecsKnobLabels.push_back             (kProresProfileHQFourCC "\t"kProresProfileHQName);
    _codecsIds.push_back                    (AV_CODEC_ID_PRORES);

    _codecsShortNames.push_back(kProresCodec kProresProfileSQFourCC);
    _codecsLongNames.push_back              (kProresProfileSQName);
    _codecsKnobLabels.push_back             (kProresProfileSQFourCC "\t"kProresProfileSQName);
    _codecsIds.push_back                    (AV_CODEC_ID_PRORES);

    _codecsShortNames.push_back(kProresCodec kProresProfileLTFourCC);
    _codecsLongNames.push_back              (kProresProfileLTName);
    _codecsKnobLabels.push_back             (kProresProfileLTFourCC "\t"kProresProfileLTName);
    _codecsIds.push_back                    (AV_CODEC_ID_PRORES);

    _codecsShortNames.push_back(kProresCodec kProresProfileProxyFourCC);
    _codecsLongNames.push_back              (kProresProfileProxyName);
    _codecsKnobLabels.push_back             (kProresProfileProxyFourCC "\t"kProresProfileProxyName);
    _codecsIds.push_back                    (AV_CODEC_ID_PRORES);
#endif

    AVCodec* c = av_codec_next(NULL);
    while (c) {
        if ( (c->type == AVMEDIA_TYPE_VIDEO) && av_codec_is_encoder(c) ) {
            if ( FFmpegFile::isCodecWhitelistedForWriting( c->name ) &&
                 (c->long_name) ) {
                const char* knobLabel = getCodecKnobLabel(c->name);
                if (knobLabel == NULL) {
#             if OFX_FFMPEG_PRINT_CODECS
                    std::cout << "Codec whitelisted but unknown: " << c->name << " = " << c->long_name << std::endl;
#             endif //  FFMPEG_PRINT_CODECS
                } else {
#             if OFX_FFMPEG_PRINT_CODECS
                    std::cout << "Codec[" << _codecsLongNames.size() << "]: " << c->name << " = " << c->long_name << std::endl;
#             endif //  FFMPEG_PRINT_CODECS
                    _codecsLongNames.push_back(c->long_name);
                    _codecsShortNames.push_back(c->name);
                    _codecsKnobLabels.push_back(knobLabel);
                    _codecsIds.push_back(c->id);
                    _codecsFormats.push_back( vector<size_t>() );
                    assert( _codecsLongNames.size() == _codecsShortNames.size() );
                    assert( _codecsLongNames.size() == _codecsKnobLabels.size() );
                    assert( _codecsLongNames.size() == _codecsIds.size() );
                }
            }
#         if OFX_FFMPEG_PRINT_CODECS
            else {
                std::cout << "Disallowed Codec: " << c->name << " = " << c->long_name << std::endl;
            }
#         endif //  FFMPEG_PRINT_CODECS
        }
        c = av_codec_next(c);
    }
    // fill the entries in _codecsFormats and _formatsCodecs
    _codecsFormats.resize( _codecsIds.size() );
    _formatsCodecs.resize( _formatsShortNames.size() );
    for (size_t f = 1; f < _formatsShortNames.size(); ++f) { // format 0 is "default"
        fmt = av_guess_format(_formatsShortNames[f].c_str(), NULL, NULL);
        if (fmt) {
            for (size_t c = 0; c < _codecsIds.size(); ++c) {
                if ( codecCompatible(fmt, _codecsIds[c]) ) {
                    _codecsFormats[c].push_back(f);
                    _formatsCodecs[f].push_back(c);
                }
            }
        }
    }
}

FFmpegSingleton::~FFmpegSingleton()
{
}

WriteFFmpegPlugin::WriteFFmpegPlugin(OfxImageEffectHandle handle,
                                     const vector<string>& extensions)
    : GenericWriterPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsXY, kSupportsAlpha)
    , _filename()
    , _pixelAspectRatio(1.)
    , _isOpen(false)
    , _error(IGNORE_FINISH)
    , _formatContext(0)
    , _streamVideo(0)
    , _streamAudio(0)
    , _streamTimecode(0)
    , _nextFrameToEncodeMutex()
    , _nextFrameToEncode(INT_MIN)
    , _firstFrameToEncode(1)
    , _lastFrameToEncode(1)
    , _frameStep(1)
    , _format(0)
    , _fps(0)
#if OFX_FFMPEG_DNXHD
    , _dnxhdCodecProfile(0)
    , _encodeVideoRange(0)
#endif
#if OFX_FFMPEG_TIMECODE
    , _writeTimeCode(0)
#endif
    , _codec(0)
    , _codecShortName(0)
    , _enableAlpha(0)
    , _bitrate(0)
    , _bitrateTolerance(0)
    , _quality(0)
    , _gopSize(0)
    , _bFrames(0)
    , _writeNCLC(0)
#if OFX_FFMPEG_MBDECISION
    , _mbDecision(0)
#endif
    , _scratchBuffer(0)
    , _scratchBufferSize(0)
{
    _rodPixel.x1 = _rodPixel.y1 = 0;
    _rodPixel.x2 = _rodPixel.y2 = -1;
    _format = fetchChoiceParam(kParamFormat);
    _fps = fetchDoubleParam(kParamFPS);
#if OFX_FFMPEG_DNXHD
    _dnxhdCodecProfile = fetchChoiceParam(kParamDNxHDCodecProfile);
    _encodeVideoRange = fetchChoiceParam(kParamDNxHDEncodeVideoRange);
#endif
#if OFX_FFMPEG_TIMECODE
    _writeTimeCode = fetchBooleanParam(kParamWriteTimeCode);
#endif
    _codec = fetchChoiceParam(kParamCodec);
    _codecShortName = fetchStringParam(kParamCodecShortName);
    _enableAlpha = fetchBooleanParam(kParamEnableAlpha);
    _bitrate = fetchDoubleParam(kParamBitrate);
    _bitrateTolerance = fetchDoubleParam(kParamBitrateTolerance);
    _quality = fetchInt2DParam(kParamQuality);
    _gopSize = fetchIntParam(kParamGopSize);
    _bFrames = fetchIntParam(kParamBFrames);
    _writeNCLC = fetchBooleanParam(kParamWriteNCLC);
#if OFX_FFMPEG_MBDECISION
    _mbDecision = fetchChoiceParam(kParamMBDecision);
#endif

    updateVisibility();
}

WriteFFmpegPlugin::~WriteFFmpegPlugin()
{
    delete [] _scratchBuffer;
    _scratchBufferSize = 0;
}

bool
WriteFFmpegPlugin::isImageFile(const string& ext) const
{
    return (ext == "bmp" ||
            ext == "pix" ||
            ext == "dpx" ||
            ext == "exr" ||
            ext == "jpeg" ||
            ext == "jpg" ||
            ext == "png" ||
            ext == "ppm" ||
            ext == "ptx" ||
            ext == "tiff" ||
            ext == "tga" ||
            ext == "rgba" ||
            ext == "rgb");
}

bool
WriteFFmpegPlugin::isRec709Format(const int height) const
{
    // First check for codecs which require special handling:
    //  * JPEG codecs always use Rec 601.
    assert(_codec);
    int codec = _codec->getValue();
    const bool isJpeg = IsJpeg(_codec, codec);
    if (isJpeg) {
        return false;
    }

    // Using method described in step 5 of QuickTimeCodecReader::setPreferredMetadata
    return (height >= 720);
}

// Figure out if an FFmpeg codec is definitely YUV based from its underlying storage
// pixel format type.
/*static*/
bool
WriteFFmpegPlugin::IsYUV(AVPixelFormat pix_fmt)
{
    // from swscale_internal.h
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    return desc && !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 2;
}

// Figure out if a codec is definitely YUV based from its shortname.
/*static*/
bool
WriteFFmpegPlugin::IsYUVFromShortName(const char* shortName,
                                      int /*codecProfile*/)
{
    return ( !strcmp(shortName, kProresCodec kProresProfileHQFourCC) ||
             !strcmp(shortName, kProresCodec kProresProfileSQFourCC) ||
             !strcmp(shortName, kProresCodec kProresProfileLTFourCC) ||
             !strcmp(shortName, kProresCodec kProresProfileProxyFourCC) ||
             ( /*(codecProfile != (int)eDNxHDCodecProfile440x) &&*/ !strcmp(shortName, "dnxhd") ) ||
             !strcmp(shortName, "mjpeg") ||
             !strcmp(shortName, "mpeg1video") ||
             !strcmp(shortName, "mpeg4") ||
             !strcmp(shortName, "v210") );
}

// Figure out if a codec is definitely RGB based from its shortname.
/*static*/
bool
WriteFFmpegPlugin::IsRGBFromShortName(const char* shortName,
                                      int codecProfile)
{
    (void)codecProfile;

    return ( !strcmp(shortName, kProresCodec kProresProfile4444FourCC) ||
             !strcmp(shortName, kProresCodec kProresProfile4444XQFourCC) ||
             //((codecProfile == (int)eDNxHDCodecProfile440x) && !strcmp(shortName, "dnxhd")) ||
             !strcmp(shortName, "png")  ||
             !strcmp(shortName, "qtrle") );
}

void
WriteFFmpegPlugin::getColorInfo(AVColorPrimaries *color_primaries,
                                AVColorTransferCharacteristic *color_trc) const
{
    //AVCOL_PRI_RESERVED0   = 0,
    //AVCOL_PRI_BT709       = 1, ///< also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP177 Annex B
    //AVCOL_PRI_UNSPECIFIED = 2,
    //AVCOL_PRI_RESERVED    = 3,
    //AVCOL_PRI_BT470M      = 4, ///< also FCC Title 47 Code of Federal Regulations 73.682 (a)(20)
    //
    //AVCOL_PRI_BT470BG     = 5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM
    //AVCOL_PRI_SMPTE170M   = 6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
    //AVCOL_PRI_SMPTE240M   = 7, ///< functionally identical to above
    //AVCOL_PRI_FILM        = 8, ///< colour filters using Illuminant C
    //AVCOL_PRI_BT2020      = 9, ///< ITU-R BT2020
    //AVCOL_PRI_NB,              ///< Not part of ABI

    //AVCOL_TRC_RESERVED0    = 0,
    //AVCOL_TRC_BT709        = 1,  ///< also ITU-R BT1361
    //AVCOL_TRC_UNSPECIFIED  = 2,
    //AVCOL_TRC_RESERVED     = 3,
    //AVCOL_TRC_GAMMA22      = 4,  ///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
    //AVCOL_TRC_GAMMA28      = 5,  ///< also ITU-R BT470BG
    //AVCOL_TRC_SMPTE170M    = 6,  ///< also ITU-R BT601-6 525 or 625 / ITU-R BT1358 525 or 625 / ITU-R BT1700 NTSC
    //AVCOL_TRC_SMPTE240M    = 7,
    //AVCOL_TRC_LINEAR       = 8,  ///< "Linear transfer characteristics"
    //AVCOL_TRC_LOG          = 9,  ///< "Logarithmic transfer characteristic (100:1 range)"
    //AVCOL_TRC_LOG_SQRT     = 10, ///< "Logarithmic transfer characteristic (100 * Sqrt(10) : 1 range)"
    //AVCOL_TRC_IEC61966_2_4 = 11, ///< IEC 61966-2-4
    //AVCOL_TRC_BT1361_ECG   = 12, ///< ITU-R BT1361 Extended Colour Gamut
    //AVCOL_TRC_IEC61966_2_1 = 13, ///< IEC 61966-2-1 (sRGB or sYCC)
    //AVCOL_TRC_BT2020_10    = 14, ///< ITU-R BT2020 for 10 bit system
    //AVCOL_TRC_BT2020_12    = 15, ///< ITU-R BT2020 for 12 bit system

    *color_primaries = AVCOL_PRI_UNSPECIFIED;
    *color_trc = AVCOL_TRC_UNSPECIFIED;

# ifdef OFX_IO_USING_OCIO
    string selection;
    assert( _ocio.get() );
    _ocio->getOutputColorspace(selection);
    if ( (selection.find("sRGB") != string::npos) || // sRGB in nuke-default and blender
         ( selection.find("srgb") != string::npos) ||
         ( selection == "sRGB Curve") || // natron
         ( selection == "sRGB D65") || // blender-cycles
         ( selection == "sRGB (D60 sim.)") || // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
         ( selection == "out_srgbd60sim") ||
         ( selection == "rrt_srgb") || // rrt_srgb in aces
         ( selection == "srgb8") ) { // srgb8 in spi-vfx
        *color_primaries = AVCOL_PRI_BT709;
        *color_trc = AVCOL_TRC_IEC61966_2_1;///< IEC 61966-2-1 (sRGB or sYCC)
    } else if ( (selection.find("Rec709") != string::npos) || // Rec709 in nuke-default
                ( selection.find("rec709") != string::npos) ||
                ( selection == "Rec 709 Curve") || // natron
                ( selection == "nuke_rec709") || // nuke_rec709 in blender
                ( selection == "Rec.709 - Full") || // aces 1.0.0
                ( selection == "out_rec709full") || // aces 1.0.0
                ( selection == "rrt_rec709_full_100nits") || // aces 0.7.1
                ( selection == "rrt_rec709") || // rrt_rec709 in aces
                ( selection == "hd10") ) { // hd10 in spi-anim and spi-vfx
        *color_primaries = AVCOL_PRI_BT709;
        *color_trc = AVCOL_TRC_BT709;///< also ITU-R BT1361
    } else if ( (selection.find("KodakLog") != string::npos) ||
                ( selection.find("kodaklog") != string::npos) ||
                ( selection.find("Cineon") != string::npos) || // Cineon in nuke-default, blender
                ( selection.find("cineon") != string::npos) ||
                ( selection == "Cineon Log Curve") || // natron
                ( selection == "REDlogFilm") || // REDlogFilm in aces 1.0.0
                ( selection == "adx10") ||
                ( selection == "lg10") || // lg10 in spi-vfx and blender
                ( selection == "lm10") ||
                ( selection == "lgf") ) {
        *color_primaries = AVCOL_PRI_BT709;
        *color_trc = AVCOL_TRC_LOG;///< "Logarithmic transfer characteristic (100:1 range)"
    } else if ( (selection.find("Gamma2.2") != string::npos) ||
                ( selection == "rrt_Gamma2.2") ||
                ( selection == "vd8") || // vd8, vd10, vd16 in spi-anim and spi-vfx
                ( selection == "vd10") ||
                ( selection == "vd16") ||
                ( selection == "VD16") ) { // VD16 in blender
        *color_primaries = AVCOL_PRI_BT709;
        *color_trc = AVCOL_TRC_GAMMA22;///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
    } else if ( (selection.find("linear") != string::npos) ||
                ( selection.find("Linear") != string::npos) ||
                ( selection == "Linear sRGB / REC.709 D65") || // natron
                ( selection == "ACES2065-1") || // ACES2065-1 in aces 1.0.0
                ( selection == "aces") || // aces in aces
                ( selection == "lnf") || // lnf, ln16 in spi-anim and spi-vfx
                ( selection == "ln16") ) {
        *color_primaries = AVCOL_PRI_BT709;
        *color_trc = AVCOL_TRC_LINEAR;
    } else if ( (selection.find("Rec2020") != string::npos) ||
                ( selection == "Rec 2020 12 Bit Curve") || // natron
                ( selection == "aces") || // aces in aces
                ( selection == "lnf") || // lnf, ln16 in spi-anim and spi-vfx
                ( selection == "ln16") ) {
        *color_primaries = AVCOL_PRI_BT2020;
        *color_trc = AVCOL_TRC_BT2020_12;
    }
# endif // ifdef OFX_IO_USING_OCIO
} // WriteFFmpegPlugin::getColorInfo

AVOutputFormat*
WriteFFmpegPlugin::initFormat(bool reportErrors) const
{
    assert(_format);
    int format = _format->getValue();
    AVOutputFormat* fmt = NULL;

    if (!format) { // first item is "Default"
        fmt = av_guess_format(NULL, _filename.c_str(), NULL);
        if (!fmt && reportErrors) {
            return NULL;
        }
    } else {
        const vector<string>& formatsShortNames = FFmpegSingleton::Instance().getFormatsShortNames();
        assert( format < (int)formatsShortNames.size() );

        fmt = av_guess_format(formatsShortNames[format].c_str(), NULL, NULL);
        if (!fmt && reportErrors) {
            return NULL;
        }
    }

    return fmt;
}

bool
WriteFFmpegPlugin::initCodec(AVOutputFormat* fmt,
                             AVCodecID& outCodecId,
                             AVCodec*& outVideoCodec) const
{
    if (!fmt) {
        return false;
    }
    outCodecId = fmt->video_codec;
    const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();

    assert(_codec);
    int codec = _codec->getValue();
    assert( codec >= 0 && codec < (int)codecsShortNames.size() );

    AVCodec* userCodec = avcodec_find_encoder_by_name( getCodecFromShortName(codecsShortNames[codec]) );
    if (userCodec) {
        outCodecId = userCodec->id;
    }
#if OFX_FFMPEG_PRORES
    if (outCodecId == AV_CODEC_ID_PRORES) {
        // use prores_ks instead of prores
        outVideoCodec = userCodec;

        return true;
    }
#endif
    outVideoCodec = avcodec_find_encoder(outCodecId);
    if (!outVideoCodec) {
        return false;
    }

    return true;
}

void
WriteFFmpegPlugin::getPixelFormats(AVCodec* videoCodec,
                                   AVPixelFormat& outNukeBufferPixelFormat,
                                   AVPixelFormat& outTargetPixelFormat,
                                   int& outBitDepth) const
{
    assert(videoCodec);
    if (!videoCodec) {
        outNukeBufferPixelFormat = AV_PIX_FMT_NONE;
        outTargetPixelFormat = AV_PIX_FMT_NONE;
        outBitDepth = 0;

        return;
    }
    const bool hasAlpha = alphaEnabled();
#if OFX_FFMPEG_PRORES
    if (AV_CODEC_ID_PRORES == videoCodec->id) {
        int index = _codec->getValue();
        const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
        assert( index < (int)codecsShortNames.size() );
        int profile = getProfileFromShortName(codecsShortNames[index]);
        if (profile == kProresProfile4444 /*|| profile == kProresProfile4444XQ*/) {
            // Prores 4444
            if (hasAlpha) {
                outTargetPixelFormat = AV_PIX_FMT_YUVA444P10;
            } else {
                outTargetPixelFormat = AV_PIX_FMT_YUV444P10;
            }
            outBitDepth = 10;
        } else {
            // in prores_ks, all other profiles use AV_PIX_FMT_YUV422P10
            outTargetPixelFormat = AV_PIX_FMT_YUV422P10;
            outBitDepth = 10;
        }
    } else
#endif
#if OFX_FFMPEG_DNXHD
    if (AV_CODEC_ID_DNXHD == videoCodec->id) {
        DNxHDCodecProfileEnum dnxhdCodecProfile = (DNxHDCodecProfileEnum)_dnxhdCodecProfile->getValue();
        if ( (dnxhdCodecProfile == eDNxHDCodecProfile220x) || (dnxhdCodecProfile == eDNxHDCodecProfile440x) ) {
            outTargetPixelFormat = AV_PIX_FMT_YUV422P10;
            outBitDepth = 10;
        } else {
            outTargetPixelFormat = AV_PIX_FMT_YUV422P;
        }
    } else
#endif // FN_LICENSED_PRORES_CODEC
    if (videoCodec->pix_fmts != NULL) {
        //This is the most frequent path, where we can guess best pix format using ffmpeg.
        //find highest bit depth pix fmt.
        const AVPixelFormat* currPixFormat  = videoCodec->pix_fmts;
        while (*currPixFormat != -1) {
            int currPixFormatBitDepth             = GetPixelFormatBitDepth(*currPixFormat);
            if (currPixFormatBitDepth  > outBitDepth) {
                outBitDepth                     = currPixFormatBitDepth;
            }
            currPixFormat++;
        }

        //figure out nukeBufferPixelFormat from this.
        outNukeBufferPixelFormat = GetPixelFormatFromBitDepth(outBitDepth, hasAlpha);

        //call best_pix_fmt using the full list.
        const int hasAlphaInt = hasAlpha ? 1 : 0;

        // gather the formats that have the highest bit depth (avcodec_find_best_pix_fmt_of_list doesn't do the best job: it prefers yuv422p over yuv422p10)
        vector<AVPixelFormat> bestFormats;
        currPixFormat  = videoCodec->pix_fmts;
        while (*currPixFormat != -1) {
            int currPixFormatBitDepth             = GetPixelFormatBitDepth(*currPixFormat);
            if (currPixFormatBitDepth  == outBitDepth) {
                bestFormats.push_back(*currPixFormat);
            }
            currPixFormat++;
        }
        bestFormats.push_back(AV_PIX_FMT_NONE);

        // FFmpeg bug https://trac.ffmpeg.org/ticket/5223 : do not pass &loss as the last parameter:
        // there is a bug in avcodec_find_best_pix_fmt_of_list: the loss mask is not the same for all pixel formats
        // (the loss of first format is used a a mask for the second, etc) so that if the first pixel format
        // loses resolution, resolution loss is not considered for other formats.
        // If we pass NULL, the considered loss is ~0 for all formats
        outTargetPixelFormat = avcodec_find_best_pix_fmt_of_list(/*videoCodec->pix_fmts*/ &bestFormats[0], outNukeBufferPixelFormat, hasAlphaInt, NULL);

#ifdef DEBUG
        int loss = av_get_pix_fmt_loss(outTargetPixelFormat, outNukeBufferPixelFormat, hasAlphaInt);
        // loss is a combination of
        // FF_LOSS_RESOLUTION
        // FF_LOSS_DEPTH
        // FF_LOSS_COLORSPACE
        // FF_LOSS_ALPHA
        // FF_LOSS_COLORQUANT
        // FF_LOSS_CHROMA
        std::printf( "WriteFFmpeg: pixel format selected: %s->%s\n", av_get_pix_fmt_name(outNukeBufferPixelFormat),
                     av_get_pix_fmt_name(outTargetPixelFormat) );
        if (loss & FF_LOSS_RESOLUTION) {
            std::printf("WriteFFmpeg: pixel format loses RESOLUTION\n");
        }
        if (loss & FF_LOSS_DEPTH) {
            std::printf("WriteFFmpeg: pixel format loses DEPTH\n");
        }
        if (loss & FF_LOSS_COLORSPACE) {
            std::printf("WriteFFmpeg: pixel format loses COLORSPACE\n");
        }
        if (loss & FF_LOSS_ALPHA) {
            std::printf("WriteFFmpeg: pixel format loses ALPHA\n");
        }
        if (loss & FF_LOSS_COLORQUANT) {
            std::printf("WriteFFmpeg: pixel format loses COLORQUANT\n");
        }
        if (loss & FF_LOSS_CHROMA) {
            std::printf("WriteFFmpeg: pixel format loses CHROMA\n");
        }
#endif

        if (AV_CODEC_ID_QTRLE == videoCodec->id) {
            if ( hasAlphaInt &&
                 (AV_PIX_FMT_ARGB != outTargetPixelFormat) &&
                 (AV_PIX_FMT_RGBA != outTargetPixelFormat) &&
                 (AV_PIX_FMT_ABGR != outTargetPixelFormat) &&
                 (AV_PIX_FMT_BGRA != outTargetPixelFormat) ) {
                // WARNING: Workaround.
                //
                // If the source has alpha, then the alpha channel must be
                // preserved. QT RLE supports an alpha channel, however
                // |avcodec_find_best_pix_fmt_of_list| above seems to default
                // to RGB24 always, despite having support for ARGB and the
                // |hasAlpha| flag being set.
                //
                // So search for a suitable RGB+alpha pixel format that is
                // supported by the QT RLE codec and force the output pixel
                // format.
                //
                currPixFormat  = videoCodec->pix_fmts;
                while (*currPixFormat != -1) {
                    AVPixelFormat avPixelFormat = *currPixFormat++;
                    if ( (AV_PIX_FMT_ARGB == avPixelFormat) ||
                         (AV_PIX_FMT_RGBA == avPixelFormat) ||
                         (AV_PIX_FMT_ABGR == avPixelFormat) ||
                         (AV_PIX_FMT_BGRA == avPixelFormat) ) {
                        outTargetPixelFormat = avPixelFormat;
                        break;
                    }
                }
            }
        }

        //Unlike the other cases, we're now done figuring out all aspects, so return.
        //return; // don't return, avcodec_find_best_pix_fmt_of_list may have returned a lower bitdepth than outBitDepth
    } else {
        //Lowest common denominator defaults.
        outTargetPixelFormat     = AV_PIX_FMT_YUV420P;
    }

    outBitDepth           = GetPixelFormatBitDepth(outTargetPixelFormat);
    outNukeBufferPixelFormat = GetPixelFormatFromBitDepth(outBitDepth, hasAlpha);
} // WriteFFmpegPlugin::getPixelFormats

// av_get_bits_per_sample knows about surprisingly few codecs.
// We have to do this manually.
/*static*/
int
WriteFFmpegPlugin::GetPixelFormatBitDepth(const AVPixelFormat pixelFormat)
{
    switch (pixelFormat) {
    case AV_PIX_FMT_NONE:

        return 0;
        break;

    case AV_PIX_FMT_BGRA64LE:
    case AV_PIX_FMT_BGRA64BE:
    case AV_PIX_FMT_RGBA64LE:
    case AV_PIX_FMT_RGBA64BE:
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_RGB48BE:
    case AV_PIX_FMT_GRAY16LE:
    case AV_PIX_FMT_GRAY16BE:
    case AV_PIX_FMT_YA16LE:
    case AV_PIX_FMT_YA16BE:

        return 16;
        break;

    case AV_PIX_FMT_YUV411P:         // Uncompressed 4:1:1 12bit
        return 12;
        break;

    case AV_PIX_FMT_YUV422P10LE:     // Uncompressed 4:2:2 10bit - planar
    case AV_PIX_FMT_YUV422P10BE:     // Uncompressed 4:2:2 10bit - planar
    case AV_PIX_FMT_YUV444P10LE:     // Uncompressed 4:4:4 10bit - planar
    case AV_PIX_FMT_YUV444P10BE:     // Uncompressed 4:4:4 10bit - planar
    case AV_PIX_FMT_YUVA444P10LE:     // Uncompressed 4:4:4:4 10bit - planar
    case AV_PIX_FMT_YUVA444P10BE:     // Uncompressed 4:4:4:4 10bit - planar
    case AV_PIX_FMT_YUVA444P:        // Uncompressed packed QT 4:4:4:4
        return 10;
        break;

    case AV_PIX_FMT_YUV420P:     // MPEG-1, MPEG-2, MPEG-4 part2 (default)
    case AV_PIX_FMT_YUVJ420P:     // MJPEG
    case AV_PIX_FMT_YUV422P:     // DNxHD
    case AV_PIX_FMT_YUVJ422P:     // MJPEG
    case AV_PIX_FMT_YUV444P:     // Uncompressed 4:4:4 planar
    case AV_PIX_FMT_YUVJ444P:     // Uncompressed 4:4:4 planar
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_PAL8:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_YA8:
    case AV_PIX_FMT_MONOBLACK:
    case AV_PIX_FMT_RGB555LE:
    case AV_PIX_FMT_RGB555BE:

        return 8;
        break;
    default:
#if OFX_FFMPEG_PRINT_CODECS
        std::cout << "** Format " << av_get_pix_fmt_name(pixelFormat) << "not handled" << std::endl;
#endif

        return 8;
        break;
    } // switch
} // WriteFFmpegPlugin::GetPixelFormatBitDepth

/*static*/
AVPixelFormat
WriteFFmpegPlugin::GetPixelFormatFromBitDepth(const int bitDepth,
                                              const bool hasAlpha)
{
    if (bitDepth == 0) {
        return AV_PIX_FMT_NONE;
    }
    AVPixelFormat pixelFormat;
    if (hasAlpha) {
        pixelFormat = (bitDepth > 8) ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGB32;
    } else {
        pixelFormat = (bitDepth > 8) ? AV_PIX_FMT_RGB48 : AV_PIX_FMT_RGB24;
    }

    return pixelFormat;
}

/*static*/
void
WriteFFmpegPlugin::GetCodecSupportedParams(AVCodec* codec,
                                           bool& outBitrateParam,
                                           bool& outBitrateTolParam,
                                           bool& outQualityParams,
                                           bool& outInterGOPParams,
                                           bool& outInterBParams)
{
    bool outLossyParams = false;

    assert(codec);
    if (!codec) {
        outBitrateParam = false;
        outBitrateTolParam = false;
        outQualityParams = false;
        outInterGOPParams = false;
        outInterBParams = false;

        return;
    }
    //The flags on the codec can't be trusted to indicate capabilities, so use the props bitmask on the descriptor instead.
    const AVCodecDescriptor* codecDesc = avcodec_descriptor_get(codec->id);

    outLossyParams    = codecDesc ? !!(codecDesc->props & AV_CODEC_PROP_LOSSY) : false;
    outInterGOPParams = codecDesc ? !(codecDesc->props & AV_CODEC_PROP_INTRA_ONLY) : false;
    outInterBParams   = codecDesc ? !(codecDesc->props & AV_CODEC_PROP_INTRA_ONLY) : false;

    //Overrides for specific cases where the codecs don't follow the rules.
    //PNG doesn't observe the params, despite claiming to be lossy.
    if ( codecDesc && (codecDesc->id == AV_CODEC_ID_PNG) ) {
        outLossyParams = outInterGOPParams = outInterBParams = false;
    }
    //Mpeg4 ms var 3 / AV_CODEC_ID_MSMPEG4V3 doesn't have a descriptor, but needs the params.
    if ( codecDesc && (codec->id == AV_CODEC_ID_MSMPEG4V3) ) {
        outLossyParams = outInterGOPParams = outInterBParams = true;
    }
    //QTRLE supports differing GOPs, but any b frame settings causes unreadable files.
    if ( codecDesc && (codecDesc->id == AV_CODEC_ID_QTRLE) ) {
        outLossyParams = outInterBParams = false;
        outInterGOPParams = true;
    }
#if OFX_FFMPEG_PRORES
    // Prores is profile-based, we don't need the bitrate parameters
    if ( codecDesc && (codecDesc->id == AV_CODEC_ID_PRORES) ) {
        outLossyParams = outInterBParams = outInterGOPParams = false;
    }
#endif
#if OFX_FFMPEG_DNXHD
    // DNxHD is profile-based, we don't need the bitrate parameters
    if ( codecDesc && (codecDesc->id == AV_CODEC_ID_DNXHD) ) {
        outLossyParams = outInterBParams = outInterGOPParams = false;
    }
#endif
    /* && codec->id != AV_CODEC_ID_PRORES*/

    if (!outLossyParams) {
        outBitrateParam = outBitrateTolParam = outQualityParams = false;
    } else {
        string codecShortName = codec->name;
        outBitrateParam = outBitrateTolParam = outQualityParams = true;

        // handle codec-specific cases.
        // Note that x264 is used in qpmin/qpmax mode
        if (codecShortName == "mpeg4") {
            outBitrateParam = outBitrateTolParam = false;
            outQualityParams = false;
        } else if (codecShortName == "libx264") {
            outBitrateParam = outBitrateTolParam = false;
        } else if (codecShortName == "libx264rgb") {
            outBitrateParam = outBitrateTolParam = false;
        } else if (codecShortName == "libx265") {
            outBitrateTolParam = false;
            outQualityParams = false;
        } else if (codecShortName == "libopenh264") {
            outBitrateTolParam = false;
            outQualityParams = false;
        } else if (codecShortName == "cinepak") {
            outBitrateParam = outBitrateTolParam = false;
            outInterGOPParams = outInterBParams = false;
            outQualityParams = false;
        } else if (codecShortName == "mpeg2video") {
            outQualityParams = false;
        } else if (codecShortName == "mpeg1video") {
            outQualityParams = false;
        } else if (codecShortName == "flv") {
            outQualityParams = false;
        } else if (codecShortName == "svq1") {
            // svq1 is h263-based
            outQualityParams = false;
        } else if (codecShortName == "mjpeg") {
            outQualityParams = false;
            outInterGOPParams = outInterBParams = false;
        } else if (codecShortName == "jpeg2000") {
            outBitrateParam = outBitrateTolParam = false;
            outQualityParams = false;
            outInterGOPParams = outInterBParams = false;
        } else if (codecShortName == "jpegls") {
            outBitrateParam = outBitrateTolParam = false;
            outQualityParams = false;
            outInterGOPParams = outInterBParams = false;
        } else if (codecShortName == "libvpx") {
            outBitrateTolParam = false;
            outInterBParams = false;
        } else if (codecShortName == "libvpx-vp9") {
            // libvpx/VP9 could be lossless??
            outBitrateTolParam = false;
            outInterBParams = false;
        } else if (codecShortName == "libschroedinger") {
            outBitrateTolParam = false;
            outQualityParams = false;
        } else if (codecShortName == "vc2") {
            // libvpx/VP9 could be lossless??
            outBitrateTolParam = false;
            outQualityParams  = false;
            outInterGOPParams = outInterBParams = false;
        }
    }
} // WriteFFmpegPlugin::GetCodecSupportedParams

#if OFX_FFMPEG_AUDIO
////////////////////////////////////////////////////////////////////////////////
// configureAudioStream
// Set audio parameters of the audio stream.
//
// @param avCodec A pointer reference to an AVCodec to receive the AVCodec if
//                the codec can be located and is on the codec white list.
// @param avStream A reference to an AVStream of a audio stream.
//
void
WriteFFmpegPlugin::configureAudioStream(AVCodec* avCodec,
                                        AVStream* avStream)
{
    assert(avCodec && avStream);
    if (!avCodec || !avStream) {
        return;
    }
    AVCodecContext* avCodecContext = avStream->codec;
    assert(avCodecContext);
    if (!avCodecContext) {
        return;
    }
    avcodec_get_context_defaults3(avCodecContext, avCodec);
    avCodecContext->sample_fmt = audioReader_->getSampleFormat();
    //avCodecContext->bit_rate    = 64000; // Calculate...
    avCodecContext->sample_rate = audioReader_->getSampleRate();
    avCodecContext->channels = audioReader_->getNumberOfChannels();
}

#endif

////////////////////////////////////////////////////////////////////////////////
// configureVideoStream
// Set video parameters of the video stream.
//
// @param avCodec A pointer reference to an AVCodec to receive the AVCodec if
//                the codec can be located and is on the codec white list.
// @param avStream A reference to an AVStream of a audio stream.
//
void
WriteFFmpegPlugin::configureVideoStream(AVCodec* avCodec,
                                        AVStream* avStream)
{
    assert(avCodec && avStream && _formatContext);
    if (!avCodec || !avStream || !_formatContext) {
        return;
    }
    AVCodecContext* avCodecContext = avStream->codec;
    assert(avCodecContext);
    if (!avCodecContext) {
        return;
    }
    avcodec_get_context_defaults3(avCodecContext, avCodec);
    //avCodecContext->strict_std_compliance = FF_COMPLIANCE_STRICT;

    //Only update the relevant context variables where the user is able to set them.
    //This deals with cases where values are left on an old value when knob disabled.
    bool bitrateParam    = false;
    bool bitrateTolParam = false;
    bool qualityParams   = false;
    bool interGOPParams  = false;
    bool interBParams    = false;
    if (avCodec) {
        GetCodecSupportedParams(avCodec, bitrateParam, bitrateTolParam, qualityParams, interGOPParams, interBParams);
    }

    assert(_bitrate && _bitrateTolerance && _quality);
    if (bitrateParam) {
        double bitrate = _bitrate->getValue();
        avCodecContext->bit_rate = (int)(bitrate * 1000000);
    }
    if (bitrateTolParam) {
        double bitrateTolerance = _bitrateTolerance->getValue();
        avCodecContext->bit_rate_tolerance = (int)(bitrateTolerance * 1000000);
    }
    if (qualityParams) {
        int qMin, qMax;
        _quality->getValue(qMin, qMax);

        avCodecContext->qmin = qMin;
        avCodecContext->qmax = qMax;
    }

    avCodecContext->width = (_rodPixel.x2 - _rodPixel.x1);
    avCodecContext->height = (_rodPixel.y2 - _rodPixel.y1);

    getColorInfo(&avCodecContext->color_primaries, &avCodecContext->color_trc);

    av_dict_set(&_formatContext->metadata, kMetaKeyApplication, kPluginIdentifier, 0);

    av_dict_set(&_formatContext->metadata, kMetaKeyApplicationVersion, STR(kPluginVersionMajor) "."STR (kPluginVersionMinor), 0);

    //const char* lutName = GetLutName(lut());
    //if (lutName)
    //    av_dict_set(&_formatContext->metadata, kMetaKeyColorspace, lutName, 0);

    av_dict_set(&_formatContext->metadata, kMetaKeyWriter, kMetaValueWriter64, 0);

    int codec = _codec->getValue();
    const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
    int dnxhdCodecProfile_i = 0;
#if OFX_FFMPEG_DNXHD
    _dnxhdCodecProfile->getValue(dnxhdCodecProfile_i);
#endif
    //Write the NCLC atom in the case the underlying storage is YUV.
    if ( IsYUVFromShortName(codecsShortNames[codec].c_str(), dnxhdCodecProfile_i) ) {
        bool writeNCLC = _writeNCLC->getValue();

        // Primaries are always 709.
        //avCodecContext->color_primaries = AVCOL_PRI_BT709;
        if (writeNCLC) {
            av_dict_set(&avStream->metadata, kNCLCPrimariesKey, "1", 0);
        }

        // Transfer function is always set to unknown. This results in more correct reading
        // on the part of QT Player.
        avCodecContext->color_trc = AVCOL_TRC_UNSPECIFIED;
        if (writeNCLC) {
            av_dict_set(&avStream->metadata, kNCLCTransferKey, "2", 0);
        }

        // Matrix is based on that used when writing (a combo of height and legacy codec in general).
        if ( isRec709Format(avCodecContext->height) ) {
            if (avCodecContext->color_primaries == AVCOL_PRI_BT2020) {
                avCodecContext->colorspace = AVCOL_SPC_BT2020_NCL;
            } else {
                avCodecContext->colorspace = AVCOL_SPC_BT709;
            }
            if (writeNCLC) {
                av_dict_set(&avStream->metadata, kNCLCMatrixKey, "1", 0);
            }
        } else {
            avCodecContext->color_primaries = AVCOL_PRI_BT470BG;
            avCodecContext->color_trc = AVCOL_TRC_GAMMA28;
            avCodecContext->colorspace = AVCOL_SPC_BT470BG;
            if (writeNCLC) {
                av_dict_set(&avStream->metadata, kNCLCMatrixKey, "6", 0);
            }
        }
    }

    // From the Apple QuickTime movie guidelines. Set the
    // appropriate pixel aspect ratio for the movie.
    // Scale by 100 and convert to int for a reliable comparison, e.g.
    // 0.9100000000 != 0.9100002344. This is done as the pixel aspect
    // ratio is provided as a double and the author cannot find a
    // rational representation (num/den) of par.
    int32_t par = (int32_t)(_inputClip->getPixelAspectRatio() * 100.0);
    if (200 == par) {
        avCodecContext->sample_aspect_ratio.num = 2;
        avCodecContext->sample_aspect_ratio.den = 1;
    } else if (150 == par) {
        avCodecContext->sample_aspect_ratio.num = 3;
        avCodecContext->sample_aspect_ratio.den = 2;
    } else if (146 == par) {
        // PAL 16:9
        avCodecContext->sample_aspect_ratio.num = 118;
        avCodecContext->sample_aspect_ratio.den = 81;
    } else if (133 == par) {
        avCodecContext->sample_aspect_ratio.num = 4;
        avCodecContext->sample_aspect_ratio.den = 3;
    } else if (121 == par) {
        // NTSC 16:9
        avCodecContext->sample_aspect_ratio.num = 40;
        avCodecContext->sample_aspect_ratio.den = 33;
    } else if (109 == par) {
        // PAL 4:3
        avCodecContext->sample_aspect_ratio.num = 59;
        avCodecContext->sample_aspect_ratio.den = 54;
    } else if (100 == par) {
        // Typically HD
        avCodecContext->sample_aspect_ratio.num = 1;
        avCodecContext->sample_aspect_ratio.den = 1;
    } else if (91 == par) {
        // NTSC 4:3
        avCodecContext->sample_aspect_ratio.num = 10;
        avCodecContext->sample_aspect_ratio.den = 11;
    }

    double fps = _fps->getValue();
    // timebase: This is the fundamental unit of time (in seconds) in terms
    // of which frame timestamps are represented. For fixed-fps content,
    // timebase should be 1/framerate and timestamp increments should be
    // identical to 1.
    //
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
    //streamVideo_->codec->time_base = av_d2q(1.0 / fps_, 100);
    //const float CONVERSION_FACTOR = 1000.0f;
    //avCodecContext->time_base.num = (int)CONVERSION_FACTOR;
    //avCodecContext->time_base.den = (int)(fps * CONVERSION_FACTOR);

    // Trap fractional frame rates so that they can be specified correctly
    // in a QuickTime movie. The rational number representation of fractional
    // frame rates is 24000/1001, 30000/1001, 60000/1001, etc.
    // WARNING: There are some big assumptions here. The assumption is
    //          that by specifying 23.98, 29.97 on the UI the intended
    //          frame rate is 24/1.001, 30/1.001, etc. so the frame rate
    //          is corrected here.
    int frameRate = (0.0 < fps) ? (int)fps : 0;
    AVRational frame_rate;
    if ( ( (23.969 < fps) && (fps < 23.981) ) || ( (29.969 < fps) && (29.981 < fps) ) || ( (59.939 < fps) && (fps < 59.941) ) ) {
        frame_rate.num = std::ceil(fps) * 1000;
        frame_rate.den = 1001;
    } else {
        // integers are represented exactly as float, so most of the time the denominator will be 1
        frame_rate = av_d2q(fps, INT_MAX);
    }
    avCodecContext->time_base = av_inv_q(frame_rate);;
    // [mov @ 0x1042d7600] Using AVStream.codec.time_base as a timebase hint to the muxer is deprecated. Set AVStream.time_base instead.
    // copy timebase while removing common factors
    avStream->time_base = av_add_q(avCodecContext->time_base, (AVRational) {0, 1});

    int gopSize = _gopSize->getValue();
    if (interGOPParams) {
        avCodecContext->gop_size = gopSize;
    }

    int bFrames = _bFrames->getValue();
    // NOTE: in new ffmpeg, bframes don't seem to work correctly - ffmpeg crashes...
    if (interBParams && bFrames) {
        avCodecContext->max_b_frames = bFrames;
#if FF_API_PRIVATE_OPT
        avCodecContext->b_frame_strategy = 0;
#endif
        avCodecContext->b_quant_factor = 2.0f;
    }

    FieldEnum fieldOrder = _inputClip->getFieldOrder();
    bool progressive = (fieldOrder == eFieldNone);
    // for the time being, we only support progressive encoding
    if (!progressive) {
        setPersistentMessage(Message::eMessageError, "", "only progressive video is supported");
        throwSuiteStatusException(kOfxStatFailed);
    }
    // Set this field so that an 'fiel' atom is inserted
    // into the QuickTime 'moov' atom.
    avCodecContext->field_order = AV_FIELD_PROGRESSIVE;

#if OFX_FFMPEG_DNXHD
    // the following was moved here from openCodec()
    if (AV_CODEC_ID_DNXHD == avCodecContext->codec_id) {
        // This writer will rescale for any source format
        // that is not natively supported by DNxHD. Check
        // that the source format matches a native DNxHD
        // format. If it's not supported, then force to
        // 1920x1080 (the most flexible format for DNxHD).
        // This will also mean that avcodec_open2 will not
        // fail as the format matches a native DNxHD format.
        // Any other frame dimensions result in error.
        int srcWidth = avCodecContext->width;
        int srcHeight = avCodecContext->height;
        if ( ( ( (1920 == srcWidth) /*|| (1440 == srcWidth)*/ ) && (1080 == srcHeight) ) ||
             ( ( (1280 == srcWidth) /*|| (960 == srcWidth)*/ ) && (720 == srcHeight) ) ) {
            // No conversion necessary.
        } else {
            avCodecContext->width = 1920;
            avCodecContext->height = 1080;
        }
        // If alpha is to be encoded, then the following must
        // be set to 32. This will ensure the correct bit depth
        // information will be embedded in the QuickTime movie.
        // Otherwise the following must be set to 24.
        const bool hasAlpha = alphaEnabled();
        avCodecContext->bits_per_coded_sample = (hasAlpha) ? 32 : 24;
        int mbs = 0;
        DNxHDCodecProfileEnum dnxhdCodecProfile = (DNxHDCodecProfileEnum)dnxhdCodecProfile_i;
        switch (dnxhdCodecProfile) {
        case eDNxHDCodecProfile440x:
            // 880x in 1080p/60 or 1080p/59.94, 730x in 1080p/50, 440x in 1080p/30, 390x in 1080p/25, 350x in 1080p/24
            if ( ( avCodecContext->width == 1920) && ( avCodecContext->height == 1080) ) {
                if (frameRate > 50) {
                    //case 60:
                    //case 59:
                    mbs = progressive ? 880 : /*0*/ 220;
                } else if (frameRate > 29) {
                    //case 50:
                    mbs = progressive ? 730 : /*0*/ 220;
                } else if (frameRate > 25) {
                    //case 29:
                    mbs = progressive ? 440 : /*0*/ 220;
                } else if (frameRate > 24) {
                    //case 25:
                    mbs = progressive ? 390 : /*0*/ 185;
                } else {
                    //case 24:
                    //case 23:
                    mbs = progressive ? 350 : /*0*/ 145;
                }
            }
            break;
        case eDNxHDCodecProfile220x:
        case eDNxHDCodecProfile220:
            // 440x in 1080p/60 or 1080p/59.94, 365x in 1080p/50, 220x in 1080i/60 or 1080i/59.94, 185x in 1080i/50 or 1080p/25, 175x in 1080p/24 or 1080p/23.976, 220x in 1080p/29.97, 220x in 720p/59.94, 175x in 720p/50
            if ( ( avCodecContext->width == 1920) && ( avCodecContext->height == 1080) ) {
                if (frameRate > 50) {
                    //case 60:
                    //case 59:
                    mbs = progressive ? 440 : 220;
                } else if (frameRate > 29) {
                    //case 50:
                    mbs = progressive ? 365 : 185;
                } else if (frameRate > 25) {
                    //case 29:
                    mbs = progressive ? 220 : /*0*/ 145;
                } else if (frameRate > 24) {
                    //case 25:
                    mbs = progressive ? 185 : /*0*/ 120;
                } else {
                    //case 24:
                    //case 23:
                    mbs = progressive ? 175 : /*0*/ 120;
                }
            } else {
                if (frameRate > 50) {
                    //case 60:
                    //case 59:
                    mbs = progressive ? 220 : 0;     // 720i unsupported in ffmpeg
                } else {
                    //case 50:
                    mbs = progressive ? 175 : 0;     // 720i unsupported in ffmpeg
                }
            }
            break;
        case eDNxHDCodecProfile145:
            // 290 in 1080p/60 or 1080p/59.94, 240 in 1080p/50, 145 in 1080i/60 or 1080i/59.94, 120 in 1080i/50 or 1080p/25, 115 in 1080p/24 or 1080p/23.976, 145 in 1080p/29.97, 145 in 720p/59.94, 115 in 720p/50
            if ( ( avCodecContext->width == 1920) && ( avCodecContext->height == 1080) ) {
                if (frameRate > 50) {
                    //case 60:
                    //case 59:
                    mbs = progressive ? 290 : 145;
                } else if (frameRate > 29) {
                    //case 50:
                    mbs = progressive ? 240 : 120;
                } else if (frameRate > 25) {
                    //case 29:
                    mbs = progressive ? 145 : /*0*/ 120;    // 120 is the lowest possible bitrate for 1920x1080i
                } else if (frameRate > 24) {
                    //case 25:
                    mbs = 120 /*progressive ? 120 : 0*/;    // 120 is the lowest possible bitrate for 1920x1080i
                } else {
                    //case 24:
                    //case 23:
                    mbs = progressive ? 115 : /*0*/ 120;    // 120 is the lowest possible bitrate for 1920x1080i
                }
            } else {
                if (frameRate > 50) {
                    //case 60:
                    //case 59:
                    mbs = progressive ? 145 : 0;     // 720i unsupported
                } else {
                    //case 50:
                    mbs = progressive ? 115 : 0;     // 720i unsupported
                }
            }
            break;
        case eDNxHDCodecProfile36:
            // 90 in 1080p/60 or 1080p/59.94, 75 in 1080p/50, 45 in 1080i/60 or 1080i/59.94, 36 in 1080i/50 or 1080p/25, 36 in 1080p/24 or 1080p/23.976, 45 in 1080p/29.97, 100 in 720p/59.94, 85 in 720p/50
            if ( ( avCodecContext->width == 1920) && ( avCodecContext->height == 1080) ) {
                if (frameRate > 50) {
                    //case 60:
                    //case 59:
                    mbs = progressive ? 90 : /*45*/ 120;    // 45 is not unsupported by ffmpeg for 1920x1080i
                } else if (frameRate > 29) {
                    //case 50:
                    mbs = progressive ? 75 : /*36*/ 120;    // 36 is not unsupported by ffmpeg 1920x1080i
                } else if (frameRate > 25) {
                    //case 29:
                    mbs = progressive ? 45 : /*0*/ 120;
                } else if (frameRate > 24) {
                    //case 25:
                    mbs = progressive ? 36 : /*0*/ 120;
                } else {
                    //case 24:
                    //case 23:
                    mbs = progressive ? 36 : /*0*/ 120;
                }
            } else {
                switch (frameRate) {
                case 60:
                case 59:
                    mbs = progressive ? 100 : 0;         // 720i unsupported in ffmpeg
                    break;
                case 50:
                    mbs = progressive ? 85 : 0;         // 720i unsupported in ffmpeg
                    break;
                default:
                    break;
                }
            }
            break;
        } // switch
        if (mbs == 0) {
            setPersistentMessage(Message::eMessageError, "", "frameRate not supported for DNxHD");
            throwSuiteStatusException(kOfxStatFailed);
        }
        avCodecContext->bit_rate = mbs * 1000000;
    }
#endif // DNxHD

    //Currently not set - the main problem being that the mov32 reader will use it to set its defaults.
    //TODO: investigate using the writer key in mov32 to ignore this value when set to mov64.
    //av_dict_set(&_formatContext->metadata, kMetaKeyPixelFormat, "YCbCr  8-bit 422 (2vuy)", 0);

    const char* ycbcrmetavalue = isRec709Format(avCodecContext->height) ? "Rec 709" : "Rec 601";
    av_dict_set(&_formatContext->metadata, kMetaKeyYCbCrMatrix, ycbcrmetavalue, 0);

#if OFX_FFMPEG_MBDECISION
    int mbDecision = _mbDecision->getValue();
    avCodecContext->mb_decision = mbDecision;
#else
    avCodecContext->mb_decision = FF_MB_DECISION_SIMPLE;
#endif

# if OFX_FFMPEG_TIMECODE
    bool writeTimecode = _writeTimecode->getValue(writeTimecode);

    // Create a timecode stream for QuickTime movies. (There was no
    // requirement at the time of writing for any other file format.
    // Also not all containers support timecode.)
    if ( writeTimecode && !strcmp(_formatContext->oformat->name, "mov") ) {
        // Retrieve the timecode from Nuke/NukeStudio.
        // Adding a 'timecode' metadata item to the video stream
        // metadata will automagically create a timecode track
        // in the QuickTime movie created by FFmpeg.
        const MetaData::Bundle& metaData = iop->_fetchMetaData("");
        size_t size = metaData.size();
        MetaData::Bundle::PropertyPtr property = metaData.getData("input/timecode");
        if (property) {
            string timecode = MetaData::propertyToString(property).c_str();
            if ( 0 == timecode.size() ) {
                timecode = "00:00:00:00"; // Set a sane default.
            }
            av_dict_set(&avStream->metadata, "timecode", timecode.c_str(), 0);
        }
    }
# endif
} // WriteFFmpegPlugin::configureVideoStream

////////////////////////////////////////////////////////////////////////////////
// addStream
// Add a new stream to the AVFormatContext of the file. This will search for
// the codec and if found, validate it against the codec whitelist. If the codec
// can be used, a new stream is created and configured.
//
// @param avFormatContext A reference to an AVFormatContext of the file.
// @param avCodecId An AVCodecID enumeration of the codec to attempt to open.
// @param pavCodec A pointer reference to an AVCodec to receive the AVCodec if
//                 the codec can be located and is on the codec white list.
//                 This can be NULL for non-codec streams, e.g. timecode.
//
// @return A reference to an AVStream if successful.
//         NULL otherwise.
//
AVStream*
WriteFFmpegPlugin::addStream(AVFormatContext* avFormatContext,
                             enum AVCodecID avCodecId,
                             AVCodec** pavCodec)
{
    AVStream* avStream = NULL;
    AVCodec* avCodec = NULL;

    // Find the encoder.
#if OFX_FFMPEG_PRORES
    if (avCodecId == AV_CODEC_ID_PRORES) {
        // use prores_ks instead of prores
        avCodec = avcodec_find_encoder_by_name(kProresCodec);
    } else
#endif
    {
        avCodec = avcodec_find_encoder(avCodecId);
    }
    if (!avCodec) {
        setPersistentMessage(Message::eMessageError, "", "could not find codec");

        return NULL;
    }

    avStream = avformat_new_stream(avFormatContext, avCodec);
    if (!avStream) {
        setPersistentMessage(Message::eMessageError, "", "could not allocate stream");

        return NULL;
    }
    avStream->id = avFormatContext->nb_streams - 1;

    switch (avCodec->type) {
    case AVMEDIA_TYPE_AUDIO:
#     if OFX_FFMPEG_AUDIO
        configureAudioStream(avCodec, avStream);
#     endif
        break;

    case AVMEDIA_TYPE_VIDEO:
        configureVideoStream(avCodec, avStream);
        break;

    default:
        break;
    }

    // Update the caller provided pointer with the codec.
    *pavCodec = avCodec;

    return avStream;
}

////////////////////////////////////////////////////////////////////////////////
// openCodec
// Open a codec.
//
// @param avFormatContext A reference to an AVFormatContext of the file.
// @param avCodec A reference to an AVCodec of the video codec.
// @param avStream A reference to an AVStream of a video stream.
//
// @return 0 if successful,
//         <0 otherwise.
//
int
WriteFFmpegPlugin::openCodec(AVFormatContext* avFormatContext,
                             AVCodec* avCodec,
                             AVStream* avStream)
{
    assert(avFormatContext && avCodec && avStream);
    if (!avFormatContext || !avCodec || !avStream) {
        return -1;
    }
    AVCodecContext* avCodecContext = avStream->codec;
    assert(avCodecContext);
    if (!avCodecContext) {
        return -1;
    }
    if (AVMEDIA_TYPE_AUDIO == avCodecContext->codec_type) {
        // Audio codecs.
        int error = avcodec_open2(avCodecContext, avCodec, NULL);
        if (error < 0) {
            // Report the error.
            char szError[1024] = { 0 };
            av_strerror( error, szError, sizeof(szError) );
            setPersistentMessage(Message::eMessageError, "", string("Could not open audio codec: ") + szError);

            return -1;
        }
    } else if (AVMEDIA_TYPE_VIDEO == avCodecContext->codec_type) {
        int error = avcodec_open2(avCodecContext, avCodec, NULL);
        if (error < 0) {
            // Report the error.
            char szError[1024] = { 0 };
            av_strerror( error, szError, sizeof(szError) );
            setPersistentMessage(Message::eMessageError, "", string("Could not open video codec: ") + szError);

            return -4;
        }
    } else if (AVMEDIA_TYPE_DATA == avCodecContext->codec_type) {
        // Timecode codecs.
    }

    return 0;
}

#if OFX_FFMPEG_AUDIO
////////////////////////////////////////////////////////////////////////////////
// writeAudio
//
// * Read audio from the source file (this will also convert to the desired
//   sample format for the file).
// * Encode
// * Write to file.
//
// NOTE: Writing compressed audio is not yet implemented. E.g. 'flushing' the
//       encoder when the writer has finished writing all the video frames.
//
// @param avFormatContext A reference to an AVFormatContext of the file.
// @param avStream A reference to an AVStream of an audio stream.
// @param flush A boolean value to flag that any remaining frames in the interal
//              queue of the encoder should be written to the file. No new
//              frames will be queued for encoding.
//
// @return 0 if successful,
//         <0 otherwise for any failure to read (and convert) the audio format,
//         encode the audio or write to the file.
//
int
WriteFFmpegPlugin::writeAudio(AVFormatContext* avFormatContext,
                              AVStream* avStream,
                              bool flush)
{
    int ret = 0;
    MyAVFrame avFrame;
    int nbSamples = audioReader_->read(avFrame);

    if (nbSamples) {
        AVPacket pkt = {0}; // data and size must be 0
        av_init_packet(&pkt);

        if (flush) {
            // A slight hack.
            // So that the durations of the video track and audio track will be
            // as close as possible, when flushing (finishing) calculate how many
            // remaining audio samples to write using the difference between the
            // the duration of the video track and the duration of the audio track.
            double videoTime = _streamVideo->pts.val * av_q2d(_streamVideo->time_base);
            double audioTime = _streamAudio->pts.val * av_q2d(_streamAudio->time_base);
            double delta = videoTime - audioTime;
            if (0.0f <= delta) {
                nbSamples = delta / av_q2d(_streamAudio->time_base);
                // Add one sample to the count to guarantee that the audio track
                // will be the same length or very slightly longer than the video
                // track. This will then end the final loop that writes audio up
                // to the duration of the video track.
                if (avFrame->nb_samples > nbSamples) {
                    avFrame->nb_samples = nbSamples;
                }
            }
        }

        AVCodecContext* avCodecContext = avStream->codec;
        int gotPacket;
        ret = avcodec_encode_audio2(avCodecContext, &pkt, avFrame, &gotPacket);
        if (!ret && gotPacket) {
            pkt.stream_index = avStream->index;
            ret = av_write_frame(avFormatContext, &pkt);
        }

        if (ret < 0) {
            // Report the error.
            char szError[1024] = { 0 };
            av_strerror( ret, szError, sizeof(szError) );
            iop->error(szError);
        }
    }

    return ret;
}

#endif // if OFX_FFMPEG_AUDIO

// the following was taken from libswscale/utils.c:
static int
handle_jpeg(enum AVPixelFormat *format)
{
    assert(format);
    if (!format) {
        return 0;
    }
    switch (*format) {
    case AV_PIX_FMT_YUVJ420P:
        *format = AV_PIX_FMT_YUV420P;

        return 1;
    case AV_PIX_FMT_YUVJ411P:
        *format = AV_PIX_FMT_YUV411P;

        return 1;
    case AV_PIX_FMT_YUVJ422P:
        *format = AV_PIX_FMT_YUV422P;

        return 1;
    case AV_PIX_FMT_YUVJ444P:
        *format = AV_PIX_FMT_YUV444P;

        return 1;
    case AV_PIX_FMT_YUVJ440P:
        *format = AV_PIX_FMT_YUV440P;

        return 1;
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY16LE:
    case AV_PIX_FMT_GRAY16BE:

        return 1;
    default:

        return 0;
    }
}

////////////////////////////////////////////////////////////////////////////////
// More of a utility function added since supported DNxHD.
// This codec requires colour space conversions, and more to the point, requires
// VIDEO levels for all formats. Specifically, 4:4:4 requires video levels on
// the input RGB component data!
//
int
WriteFFmpegPlugin::colourSpaceConvert(MyAVPicture* avPicture,
                                      AVFrame* avFrame,
                                      AVPixelFormat srcPixelFormat,
                                      AVPixelFormat dstPixelFormat,
                                      AVCodecContext* avCodecContext)
{
    if (!avPicture || !avFrame || !avCodecContext) {
        return -1;
    }
    int ret = 0;
    int width = (_rodPixel.x2 - _rodPixel.x1);
    int height = (_rodPixel.y2 - _rodPixel.y1);
    int dstRange = IsYUV(dstPixelFormat) ? 0 : 1; // 0 = 16..235, 1 = 0..255
    dstRange |= handle_jpeg(&dstPixelFormat); // may modify dstPixelFormat
    if (AV_CODEC_ID_DNXHD == avCodecContext->codec_id) {
        int encodeVideoRange = _encodeVideoRange->getValue();
        dstRange = !(encodeVideoRange);
    }

    SwsContext* convertCtx = sws_getCachedContext(NULL,
                                                  width, height, srcPixelFormat, // from
                                                  avCodecContext->width, avCodecContext->height, dstPixelFormat,// to
                                                  SWS_BICUBIC, NULL, NULL, NULL);
    if (!convertCtx) {
        return -1;
    }
    // Set up the sws (SoftWareScaler) to convert colourspaces correctly, in the sws_scale function below
    //const int colorspace = (width < 1000) ? SWS_CS_ITU601 : SWS_CS_ITU709;
    // it's the output size that counts (e.g. for DNxHD), and we prefer using height
    const int colorspace = isRec709Format(avCodecContext->height) ? SWS_CS_ITU709 : SWS_CS_ITU601;

    // Only apply colorspace conversions for YUV.
    if ( IsYUV(dstPixelFormat) ) {
        ret = sws_setColorspaceDetails(convertCtx,
                                       sws_getCoefficients(SWS_CS_DEFAULT), // inv_table
                                       1, // srcRange - 0 = 16..235, 1 = 0..255
                                       sws_getCoefficients(colorspace), // table
                                       dstRange, // dstRange - 0 = 16..235, 1 = 0..255
                                       0, // brightness fixed point, with 0 meaning no change,
                                       1 << 16, // contrast   fixed point, with 1<<16 meaning no change,
                                       1 << 16); // saturation fixed point, with 1<<16 meaning no change);
    }

    sws_scale(convertCtx,
              avPicture->data, // src
              avPicture->linesize, // src rowbytes
              0,
              height,
              avFrame->data, // dst
              avFrame->linesize); // dst rowbytes

    return ret;
}

bool
WriteFFmpegPlugin::alphaEnabled() const
{
    // is the writer configured to write alpha channel to file ?
    bool enableAlpha = _enableAlpha->getValue();

    return enableAlpha && _inputClip->getPixelComponents() == ePixelComponentRGBA;
}

int
WriteFFmpegPlugin::numberOfDestChannels() const
{
    // 4 channels (RGBA) if alpha is enabled, else 3 (RGB)
    return alphaEnabled() ? 4 : 3;
}

////////////////////////////////////////////////////////////////////////////////
// writeVideo
//
// * Convert Nuke float RGB values to the ffmpeg pixel format of the encoder.
// * Encode.
// * Write to file.
//
// @param avFormatContext A reference to an AVFormatContext of the file.
// @param avStream A reference to an AVStream of a video stream.
// @param flush A boolean value to flag that any remaining frames in the interal
//              queue of the encoder should be written to the file. No new
//              frames will be queued for encoding.
//
// @return 0 if successful,
//         <0 otherwise for any failure to convert the pixel format, encode the
//         video or write to the file.
//
int
WriteFFmpegPlugin::writeVideo(AVFormatContext* avFormatContext,
                              AVStream* avStream,
                              bool flush,
                              double time,
                              const float *pixelData,
                              const OfxRectI* bounds,
                              int pixelDataNComps,
                              int dstNComps,
                              int rowBytes)
{
    assert(dstNComps == 3 || dstNComps == 4 || dstNComps == 0);
    // FIXME enum needed for error codes.
    if (!_isOpen) {
        return -5; //writer is not open!
    }
    if (!avStream) {
        return -6;
    }
    assert(avFormatContext);
    if ( !avFormatContext || ( !flush && (!pixelData || !bounds) ) ) {
        return -7;
    }
    int ret = 0;
    // First convert from Nuke floating point RGB to either 16-bit or 8-bit RGB.
    // Create a buffer to hold either  16-bit or 8-bit RGB.
    AVCodecContext* avCodecContext = avStream->codec;
    assert(avCodecContext);
    if (!avCodecContext) {
        return -8;
    }
    // Create another buffer to convert from either 16-bit or 8-bit RGB
    // to the input pixel format required by the encoder.
    AVPixelFormat pixelFormatCodec = avCodecContext->pix_fmt;
    int width = _rodPixel.x2 - _rodPixel.x1;
    int height = _rodPixel.y2 - _rodPixel.y1;
    MyAVPicture avPicture;
    AVFrame* avFrame = NULL;

    if (!flush) {
        assert(pixelData && bounds);
        assert(bounds->x1 == _rodPixel.x1 && bounds->x2 == _rodPixel.x2 &&
               bounds->y1 == _rodPixel.y1 && bounds->y2 == _rodPixel.y2);

        const bool hasAlpha = alphaEnabled();
        AVPixelFormat pixelFormatNuke;
        if (hasAlpha) {
            pixelFormatNuke = (avCodecContext->bits_per_raw_sample > 8) ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGBA;
        } else {
            pixelFormatNuke = (avCodecContext->bits_per_raw_sample > 8) ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;
        }

        ret = avPicture.alloc(width, height, pixelFormatNuke);
        if (!ret) {
            // Convert floating point values to unsigned values.
            assert(rowBytes);
            const int numDestChannels = hasAlpha ? 4 : 3;

            for (int y = 0; y < height; ++y) {
                int srcY = height - 1 - y;
                const float* src_pixels = (float*)( (char*)pixelData + srcY * rowBytes );

                if (avCodecContext->bits_per_raw_sample > 8) {
                    assert(pixelFormatNuke == AV_PIX_FMT_RGBA64 || pixelFormatNuke == AV_PIX_FMT_RGB48LE);

                    // avPicture.linesize is in bytes, but stride is U16 (2 bytes), so divide linesize by 2
                    unsigned short* dst_pixels = reinterpret_cast<unsigned short*>(avPicture.data[0]) + y * (avPicture.linesize[0] / 2);

                    for (int x = 0; x < width; ++x) {
                        int srcCol = x * pixelDataNComps;
                        int dstCol = x * numDestChannels;
                        dst_pixels[dstCol + 0] = floatToInt<65536>(src_pixels[srcCol + 0]);
                        dst_pixels[dstCol + 1] = floatToInt<65536>(src_pixels[srcCol + 1]);
                        dst_pixels[dstCol + 2] = floatToInt<65536>(src_pixels[srcCol + 2]);
                        if (hasAlpha) {
                            dst_pixels[dstCol + 3] = floatToInt<65536>( (pixelDataNComps == 4) ? src_pixels[srcCol + 3] : 1. );
                        }
                    }
                } else {
                    assert(pixelFormatNuke == AV_PIX_FMT_RGBA || pixelFormatNuke == AV_PIX_FMT_RGB24);

                    unsigned char* dst_pixels = avPicture.data[0] + y * avPicture.linesize[0];

                    for (int x = 0; x < width; ++x) {
                        int srcCol = x * pixelDataNComps;
                        int dstCol = x * numDestChannels;
                        dst_pixels[dstCol + 0] = floatToInt<256>(src_pixels[srcCol + 0]);
                        dst_pixels[dstCol + 1] = floatToInt<256>(src_pixels[srcCol + 1]);
                        dst_pixels[dstCol + 2] = floatToInt<256>(src_pixels[srcCol + 2]);
                        if (hasAlpha) {
                            dst_pixels[dstCol + 3] = floatToInt<256>( (pixelDataNComps == 4) ? src_pixels[srcCol + 3] : 1. );
                        }
                    }
                }
            }

            avFrame = av_frame_alloc(); // Create an AVFrame structure and initialise to zero.
            assert(avFrame);
            if (!avFrame) {
                ret = -1;
            } else {
                // For any codec an
                // intermediate buffer is allocated for the
                // colour space conversion.
                int bufferSize = av_image_alloc(avFrame->data, avFrame->linesize, avCodecContext->width, avCodecContext->height, pixelFormatCodec, 1);
                if (bufferSize > 0) {
                    // Set the frame fields for a video buffer as some
                    // encoders rely on them, e.g. Lossless JPEG.
                    avFrame->width = avCodecContext->width;
                    avFrame->height = avCodecContext->height;
                    avFrame->format = pixelFormatCodec;
                    colourSpaceConvert(&avPicture, avFrame, pixelFormatNuke, pixelFormatCodec, avCodecContext);
                } else {
                    // av_image_alloc failed.
                    ret = -1;
                }
            }
        }
    }

    if (!ret) {
        bool error = false;
        if (avFrame) {
            //avFrame->pts = AV_NOPTS_VALUE; // let ffmpeg guess the pts
            avFrame->pts = ( (int)time - _firstFrameToEncode );
            av_frame_set_pkt_duration(avFrame, 1);
        }
        if ( (avFormatContext->oformat->flags & AVFMT_RAWPICTURE) != 0 ) {
            AVPacket pkt;
            av_init_packet(&pkt);
            pkt.flags |= AV_PKT_FLAG_KEY;
            pkt.stream_index = avStream->index;
            pkt.data = avFrame ? avFrame->data[0] : NULL;
            pkt.size = sizeof(AVPicture);
            pkt.pts  = pkt.dts  = time - _firstFrameToEncode;
            const int writeResult = av_write_frame(avFormatContext, &pkt);
            const bool writeSucceeded = (writeResult == 0);
            if (!writeSucceeded) {
                error = true;
            }
        } else {
            // Use a contiguous block of memory. This is to scope the
            // buffer allocation and ensure that memory is released even
            // if errors or exceptions occur. A vector will allocate
            // contiguous memory.

            AVPacket pkt;
            av_init_packet(&pkt);
            // NOTE: If |flush| is true, then avFrame will be NULL at this point as
            //       alloc will not have been called.
            pkt.stream_index = avStream->index;
            pkt.data = &_scratchBuffer[0];
            pkt.size = _scratchBufferSize;
            // Encode a frame of video.
            //
            // Note that the uncompressed source frame to be encoded must be in an
            // appropriate pixel format for the encoder prior to calling this method as
            // this method does NOT perform an pixel format conversion, e.g. through using
            // Sws_xxx.
            int got_packet = 0;
            int encodeResult = avcodec_encode_video2(avCodecContext, &pkt, avFrame, &got_packet);
            // coded_frame is deprecated
            // see https://ffmpeg.org/pipermail/ffmpeg-cvslog/2015-July/092046.html
            //if (avCodecContext->coded_frame && !ret && got_packet) {
            //    avCodecContext->coded_frame->pts = pkt.pts;
            //    avCodecContext->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
            //}
            if (encodeResult < 0) {
                // Report the error.
                char szError[1024] = { 0 };
                av_strerror( encodeResult, szError, sizeof(szError) );
                setPersistentMessage(Message::eMessageError, "", string("Cannot encode frame: ") + szError);
                error = true;
            } else {
                if (flush && !got_packet) {
                    // Flag that the flush is complete.
                    ret = -10;
                }
                if (got_packet) {
                    // codecs with AV_CODEC_CAP_DELAY (e.g. png with multithreading) may not return a packet although encoding was successful
                    // coded_frame is deprecated
                    // see https://ffmpeg.org/pipermail/ffmpeg-cvslog/2015-July/092046.html
                    //if (avCodecContext->coded_frame && (avCodecContext->coded_frame->pts != AV_NOPTS_VALUE))
                    //    //pkt.pts = av_rescale_q(avCodecContext->coded_frame->pts, avCodecContext->time_base, avStream->time_base);
                    //    pkt.pts = avCodecContext->coded_frame->pts;
                    //    av_packet_rescale_ts(&pkt, avCodecContext->time_base, avStream->time_base);
                    //if (avCodecContext->coded_frame && avCodecContext->coded_frame->key_frame)
                    //    pkt.flags |= AV_PKT_FLAG_KEY;
                    av_packet_rescale_ts(&pkt, avCodecContext->time_base, avStream->time_base);

                    const int writeResult = av_write_frame(avFormatContext, &pkt);
                    const bool writeSucceeded = (writeResult == 0);
                    if (!writeSucceeded) {
                        // Report the error.
                        char szError[1024] = { 0 };
                        av_strerror( writeResult, szError, sizeof(szError) );
                        setPersistentMessage(Message::eMessageError, "", string("Cannot write frame: ") + szError);
                        error = true;
                    }
                }
            }
        }
        if (error) {
            av_log(avCodecContext, AV_LOG_ERROR, "error writing frame to file\n");
            ret = -2;
        }
    }

    // If the source frame buffer address is not the
    // same as the output buffer address, assume that
    // an intermediate buffer was allocated above and
    // must now be released.
    if (avFrame) {
        if (avFrame->data[0] != avPicture.data[0]) {
            av_freep(avFrame->data);
        }
        av_frame_free(&avFrame);
    }

    return ret;
} // WriteFFmpegPlugin::writeVideo

////////////////////////////////////////////////////////////////////////////////
// writeToFile
// Write video and if specifed audio to the movie. Interleave the audio and
// video as specified in the QuickTime movie recommendation. This is to have
// ~0.5s of audio interleaved with the video.
//
// @param avFormatContext A reference to the AVFormatContext of the file to
//                        write.
// @param finalise A flag to indicate that the streams should be flushed as
//                 no further frames are to be encoded.
//
// @return 0 if successful.
//         <0 otherwise.
//
int
WriteFFmpegPlugin::writeToFile(AVFormatContext* avFormatContext,
                               bool finalise,
                               double time,
                               const float *pixelData,
                               const OfxRectI* bounds,
                               int pixelDataNComps,
                               int dstNComps,
                               int rowBytes)
{
#if OFX_FFMPEG_AUDIO
    // Write interleaved audio and video if an audio file has
    // been specified. Otherwise just write video.
    //
    // If an audio file has been specified, calculate the
    // target stream time of the audio stream. For a QuickTime
    // movie write the audio in ~0.5s chunks so that there is
    // an approximate 0.5s interleave of audio and video.
    double videoTime = streamVideo_->pts.val * av_q2d(_streamVideo_->time_base);
    if (streamAudio_) {
        // Determine the current audio stream time.
        double audioTime = streamAudio_->pts.val * av_q2d(streamAudio_->time_base);
        // Determine the target audio stream time. This is
        // the current video time rounded to the nearest
        // 0.5s boundary.
        double targetTime = ( (int)(videoTime / 0.5) ) * 0.5;
        if (audioTime < targetTime) {
            // If audio stream is more than 0.5s behind, write
            // another ~0.5s of audio.
            double sourceDuration = audioReader_->getDuration();
            while ( (audioTime < targetTime) && (audioTime < sourceDuration) ) {
                writeAudio(avFormatContext, streamAudio_, finalise);
                audioTime = streamAudio_->pts.val * av_q2d(streamAudio_->time_base);
            }
        }
    }
#endif
    if (!_streamVideo) {
        return -6;
    }
    assert(avFormatContext);
    if ( !avFormatContext || ( !finalise && (!pixelData || !bounds) ) ) {
        return -7;
    }

    return writeVideo(avFormatContext, _streamVideo, finalise, time, pixelData, bounds, pixelDataNComps, dstNComps, rowBytes);
}

////////////////////////////////////////////////////////////////////////////////
// open
// Internal function to create all the required streams for writing a QuickTime
// movie.
//
// @return true if successful,
//         false otherwise.
//
void
WriteFFmpegPlugin::beginEncode(const string& filename,
                               const OfxRectI& rodPixel,
                               float pixelAspectRatio,
                               const BeginSequenceRenderArguments& args)
{
    if (!args.sequentialRenderStatus || _formatContext || _streamVideo) {
        setPersistentMessage(Message::eMessageError, "", "Another render is currently active");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if (args.isInteractive) {
        setPersistentMessage(Message::eMessageError, "", "can only write files when in non-interactive mode.");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    assert(!_formatContext);

    // first, check that the codec setting is OK
    checkCodec();

    ////////////////////                        ////////////////////
    //////////////////// INTIALIZE FORMAT       ////////////////////

    _filename = filename;
    _rodPixel = rodPixel;
    _pixelAspectRatio = pixelAspectRatio;

    AVOutputFormat* avOutputFormat = initFormat(/* reportErrors = */ true);
    if (!avOutputFormat) {
        setPersistentMessage(Message::eMessageError, "", "Invalid file extension");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if (!_formatContext) {
#     if defined(FFMS_USE_FFMPEG_COMPAT) && LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(54, 0, 0)
        avformat_alloc_output_context2( &_formatContext, avOutputFormat, NULL, filename.c_str() );
#     else
#       ifdef FFMS_USE_FFMPEG_COMPAT
        _formatContext = avformat_alloc_output_context( NULL, avOutputFormat, filename.c_str() );
#       else
        _formatContext = avformat_alloc_context();
        assert(_formatContext);
        _formatContext->oformat = fmt;
#       endif
#     endif
    }

    //snprintf(_formatContext->filename, sizeof(_formatContext->filename), "%s", filename.c_str());
    _formatContext->filename[sizeof(_formatContext->filename) - 1] = 0; // force NUL terminated string
    std::strncpy( _formatContext->filename, filename.c_str(), sizeof(_formatContext->filename) - 1 );

    /////////////////////                            ////////////////////
    ////////////////////    INITIALISE STREAM     ////////////////////

#if OFX_FFMPEG_AUDIO
    // Create an audio stream if a file has been provided.
    if ( _audioFile && (strlen(_audioFile) > 0) ) {
        if (!streamAudio_) {
            // Attempt to create an audio reader.
            audioReader_.reset( new AudioReader() );

            // TODO: If the sample format is configurable via a knob, set
            //       the desired format here, e.g.:
            //audioReader_->setSampleFormat(_avSampleFormat);

            if ( !audioReader_->open(_audioFile) ) {
                AVCodec* audioCodec = NULL;
                streamAudio_ = addStream(formatContext_, AV_CODEC_ID_PCM_S16LE, &audioCodec);
                if (!streamAudio_ || !audioCodec) {
                    freeFormat();

                    return false;
                }

                // Bug 45010 The following flags must be set BEFORE calling
                // openCodec (avcodec_open2). This will ensure that codec
                // specific data is created and initialized. (E.g. for MPEG4
                // the AVCodecContext::extradata will contain Elementary Stream
                // Descriptor which is required for QuickTime to decode the
                // stream.)
                AVCodecContext* avCodecContext = streamAudio_->codec;
                if ( !strcmp(formatContext_->oformat->name, "mp4") ||
                     !strcmp(formatContext_->oformat->name, "mov") ||
                     !strcmp(formatContext_->oformat->name, "3gp") ) {
                    avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
                }
                // Some formats want stream headers to be separate.
                if (formatContext_->oformat->flags & AVFMT_GLOBALHEADER) {
                    avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
                }
                // Activate multithreaded decoding. This must be done before opening the codec; see
                // http://lists.gnu.org/archive/html/bino-list/2011-08/msg00019.html
#              ifdef AV_CODEC_CAP_AUTO_THREADS
                if (avCodecContext->codec->capabilities & AV_CODEC_CAP_AUTO_THREADS) {
                    avCodecContext->thread_count = 0;
                } else
#              endif
                {
                    avCodecContext->thread_count = std::min(MultiThread::getNumCPUs(), OFX_FFMPEG_MAX_THREADS);
                }

                if (openCodec(formatContext_, audioCodec, streamAudio_) < 0) {
                    freeFormat();

                    return false;
                }

                // If audio has been specified, set the start position (in seconds).
                // The values are negated to convert them from a video time to an
                // audio time.
                // So for a knob value of -10s, this means that the audio starts at
                // a video time of -10s. So this is converted to +10s audio time. I.e.
                // The video starts +10s into the audio. Vice-versa for a knob value
                // of +10s. The video starts -10s into the audio. In this case the
                // audio reader will provide 10s of silence for the first 10s of
                // video.
                audioReader_->setStartPosition( -( (_audioOffsetUnit == 0) ? _audioOffset : (_audioOffset / fps_) ) );
            } else {
                setPersistentMessage(Message::eMessageError, "", "failed to open the audio file\nIt does not contain audio or is an invalid file");
                throwSuiteStatusException(kOfxStatFailed);

                return false;
            }
        }
    }
#endif // if OFX_FFMPEG_AUDIO

    // Create a video stream.
    AVCodecID codecId = AV_CODEC_ID_NONE;
    AVCodec* videoCodec = NULL;
    if ( !initCodec(avOutputFormat, codecId, videoCodec) ) {
        setPersistentMessage(Message::eMessageError, "", "Unable to find codec");
        freeFormat();
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    // Test if the container recognises the codec type.
    bool isCodecSupportedInContainer = codecCompatible(avOutputFormat, codecId);
    // mov seems to be able to cope with anything, which the above function doesn't seem to think is the case (even with FF_COMPLIANCE_EXPERIMENTAL)
    // and it doesn't return -1 for in this case, so we'll special-case this situation to allow this
    //isCodecSupportedInContainer |= (strcmp(_formatContext->oformat->name, "mov") == 0); // commented out [FD]: recent ffmpeg gives correct answer
    if (!isCodecSupportedInContainer) {
        setPersistentMessage(Message::eMessageError, "", "the selected codec is not supported in this container.");
        freeFormat();
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    AVPixelFormat targetPixelFormat     = AV_PIX_FMT_YUV420P;
    AVPixelFormat nukeBufferPixelFormat = AV_PIX_FMT_RGB24;
    int outBitDepth                     = 8;
    getPixelFormats(videoCodec, nukeBufferPixelFormat, targetPixelFormat, outBitDepth);
    assert(!_streamVideo);
    if (!_streamVideo) {
        _streamVideo = addStream(_formatContext, codecId, &videoCodec);
        if (!_streamVideo || !videoCodec) {
            freeFormat();
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }

        AVCodecContext* avCodecContext = _streamVideo->codec;
        avCodecContext->pix_fmt = targetPixelFormat;

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(51, 63, 100) // https://ffmpeg.org/pipermail/ffmpeg-cvslog/2015-October/094884.html
        std::size_t picSize = av_image_get_buffer_size(targetPixelFormat, rodPixel.x2 - rodPixel.x1, rodPixel.y2 - rodPixel.y1, 1);
#else
        std::size_t picSize = (std::size_t)avpicture_get_size(targetPixelFormat, rodPixel.x2 - rodPixel.x1, rodPixel.y2 - rodPixel.y1);
#endif
        if (_scratchBufferSize < picSize) {
            delete [] _scratchBuffer;
            _scratchBuffer = new uint8_t[picSize];
            _scratchBufferSize = picSize;
        }

        avCodecContext->bits_per_raw_sample = outBitDepth;
        avCodecContext->sample_aspect_ratio = av_d2q(pixelAspectRatio, 255);

        // Now that the stream has been created, and the pixel format
        // is known, for DNxHD, set the YUV range.
        if (AV_CODEC_ID_DNXHD == avCodecContext->codec_id) {
            int encodeVideoRange = _encodeVideoRange->getValue();
            // Set the metadata for the YUV range. This modifies the appropriate
            // field in the 'ACLR' atom in the video sample description.
            // Set 'full range' = 1 or 'legal range' = 2.
            av_dict_set(&_streamVideo->metadata, kACLRYuvRange, encodeVideoRange ? "2" : "1", 0);
        }

        // Bug 45010 The following flags must be set BEFORE calling
        // openCodec (avcodec_open2). This will ensure that codec
        // specific data is created and initialized. (E.g. for MPEG4
        // the AVCodecContext::extradata will contain Elementary Stream
        // Descriptor which is required for QuickTime to decode the
        // stream.)
        if ( !strcmp(_formatContext->oformat->name, "mp4") ||
             !strcmp(_formatContext->oformat->name, "mov") ||
             !strcmp(_formatContext->oformat->name, "3gp") ) {
            avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
        // Some formats want stream headers to be separate.
        if (_formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
            avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
#if OFX_FFMPEG_PRORES
        if (codecId == AV_CODEC_ID_PRORES) {
            int index = _codec->getValue();
            const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
            assert( index < (int)codecsShortNames.size() );
            //avCodecContext->profile = getProfileFromShortName(codecsShortNames[index]);
            av_opt_set(avCodecContext->priv_data, "profile", getProfileStringFromShortName(codecsShortNames[index]), 0);
            av_opt_set(avCodecContext->priv_data, "bits_per_mb", "8000", 0);
            av_opt_set(avCodecContext->priv_data, "vendor", "ap10", 0);
        }
#endif

        // Activate multithreaded decoding. This must be done before opening the codec; see
        // http://lists.gnu.org/archive/html/bino-list/2011-08/msg00019.html
#     ifdef AV_CODEC_CAP_AUTO_THREADS
        if (avCodecContext->codec->capabilities & AV_CODEC_CAP_AUTO_THREADS) {
            avCodecContext->thread_count = 0;
        } else
#     endif
        {
            avCodecContext->thread_count = MultiThread::getNumCPUs();
        }
#     ifdef AV_CODEC_CAP_SLICE_THREADS
        if (avCodecContext->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
            // multiple threads are used to decode a single frame. Reduces delay
            // also, mjpeg prefers this, see libavfocodec/frame_thread_encoder.c:ff_frame_thread_encoder_init()
            avCodecContext->thread_type = FF_THREAD_SLICE;
        }
#     endif


# if OFX_FFMPEG_PRINT_CODECS
        std::cout << "Format: " << _formatContext->oformat->name << " Codec: " << videoCodec->name << " nukeBufferPixelFormat: " << av_get_pix_fmt_name(nukeBufferPixelFormat) << " targetPixelFormat: " << av_get_pix_fmt_name(targetPixelFormat) << " outBitDepth: " << outBitDepth << " Profile: " << _streamVideo->codec->profile << std::endl;
# endif //  FFMPEG_PRINT_CODECS
        if (openCodec(_formatContext, videoCodec, _streamVideo) < 0) {
            freeFormat();
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }

        if ( !(avOutputFormat->flags & AVFMT_NOFILE) ) {
            int error = avio_open(&_formatContext->pb, filename.c_str(), AVIO_FLAG_WRITE);
            if (error < 0) {
                // Report the error.
                char szError[1024] = { 0 };
                av_strerror( error, szError, sizeof(szError) );
                setPersistentMessage(Message::eMessageError, "", string("Unable to open file: ") + szError);
                freeFormat();
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
        }

        int error = avformat_write_header(_formatContext, NULL);
        if (error < 0) {
            // Report the error.
            char szError[1024] = { 0 };
            av_strerror( error, szError, sizeof(szError) );
            setPersistentMessage(Message::eMessageError, "", string("Unable to write file header: ") + szError);
            freeFormat();
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
    }

    // Special behaviour.
    // Valid on Aug 2014 for ffmpeg v2.1.4
    //
    // R.e. libavformat/movenc.c::mov_write_udta_tag
    // R.e. libavformat/movenc.c::mov_write_string_metadata
    //
    // Remove all ffmpeg references from the QuickTime movie.
    // The 'encoder' key in the AVFormatContext metadata will
    // result in the writer adding @swr with libavformat
    // version information in the 'udta' atom.
    //
    // Prevent the @swr libavformat reference from appearing
    // in the 'udta' atom by setting the 'encoder' key in the
    // metadata to null. From movenc.c a zero length value
    // will not be written to the 'udta' atom.
    //
    AVDictionaryEntry* tag = av_dict_get(_formatContext->metadata, "encoder", NULL, AV_DICT_IGNORE_SUFFIX);
    if (tag) {
        av_dict_set(&_formatContext->metadata, "encoder", "", 0); // Set the 'encoder' key to null.
    }
    // Flag that we didn't encode any frame yet
    {
        AutoMutex lock(_nextFrameToEncodeMutex);
        _nextFrameToEncode = (int)args.frameRange.min;
        _firstFrameToEncode = (int)args.frameRange.min;
        _lastFrameToEncode = (int)args.frameRange.max;
        _frameStep = (int)args.frameStep;
    }

    _isOpen = true;
    _error = CLEANUP;
} // WriteFFmpegPlugin::beginEncode

inline void
sleep(const unsigned int milliseconds)
{
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    Sleep(milliseconds);
#else
    struct timespec tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&tv, 0);
#endif
}

#define checkAvError() if (error < 0) { \
        char errorBuf[1024]; \
        av_strerror( error, errorBuf, sizeof(errorBuf) ); \
        setPersistentMessage(Message::eMessageError, "", errorBuf); \
        throwSuiteStatusException(kOfxStatFailed); return; \
}


void
WriteFFmpegPlugin::encode(const string& filename,
                          const OfxTime time,
                          const string& /*viewName*/,
                          const float *pixelData,
                          const OfxRectI& bounds,
                          const float pixelAspectRatio,
                          const int pixelDataNComps,
                          const int dstNCompsStartIndex,
                          const int dstNComps,
                          const int rowBytes)
{
    assert(dstNCompsStartIndex == 0);
    if ( (dstNComps != 4) && (dstNComps != 3) ) {
        setPersistentMessage(Message::eMessageError, "", "can only write RGBA or RGB components images");
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    if (!_isOpen || !_formatContext) {
        setPersistentMessage(Message::eMessageError, "", "file is not open");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if (!pixelData || !_streamVideo) {
        throwSuiteStatusException(kOfxStatErrBadHandle);

        return;
    }
    if ( filename != string(_formatContext->filename) ) {
        stringstream ss;
        ss << "Trying to render " << filename << " but another active render is rendering " << string(_formatContext->filename);
        setPersistentMessage( Message::eMessageError, "", ss.str() );
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }


    if (pixelAspectRatio != _pixelAspectRatio) {
        setPersistentMessage(Message::eMessageError, "", "all images in the sequence do not have the same pixel aspect ratio");
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    ///Check that we're really encoding in sequential order
    {
        AutoMutex lock(_nextFrameToEncodeMutex);

        while (_nextFrameToEncode != time && _nextFrameToEncode != INT_MIN) {
            lock.unlock();
            sleep(1);
            lock.relock();
        }

        if (_nextFrameToEncode == INT_MIN) {
            // Another thread aborted
            if ( abort() ) {
                setPersistentMessage(Message::eMessageError, "", "Render aborted");
            }
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
        try {
            _error = IGNORE_FINISH;


            if (_isOpen) {
                _error = CLEANUP;

                if (!_streamVideo) {
                    throwSuiteStatusException(kOfxStatErrBadHandle);

                    return;
                }
                assert(_formatContext);
                if ( !writeToFile(_formatContext, false, time, pixelData, &bounds, pixelDataNComps, dstNComps, rowBytes) ) {
                    _error = SUCCESS;
                    _nextFrameToEncode = (int)time + _frameStep;
                    if ( abort() ) {
                        _nextFrameToEncode = INT_MIN;
                    }
                } else {
                    throwSuiteStatusException(kOfxStatFailed);

                    return;
                }
            }
        } catch (const std::exception& e) {
            _nextFrameToEncode = INT_MIN;
            throw e;
        }
    } // MultiThread::AutoMutex lock(_nextFrameToEncodeMutex);
} // WriteFFmpegPlugin::encode

////////////////////////////////////////////////////////////////////////////////
// finish
// Complete the encoding, finalise and close the file.
// Some video codecs may have data remaining in their encoding buffers. This
// must be 'flushed' to the file.
// This method flags the 'flush' to get the remaining encoded data out of the
// encoder(s) and into the file.
// Audio is written after video so it may require more writes from the audio
// stream in order to ensure that the duration of the audio and video tracks
// are the same.
//
void
WriteFFmpegPlugin::endEncode(const EndSequenceRenderArguments & /*args*/)
{
    if (!_formatContext) {
        return;
    }


    if (_error == IGNORE_FINISH) {
        freeFormat();

        return;
    }

    bool flushFrames = true;
    while (flushFrames) {
        // Continue to write the audio/video interleave while there are still
        // frames in the video and/or audio encoder queues, without queuing any
        // new data to encode. This is ffmpeg specific.
        flushFrames = !writeToFile(_formatContext, true, -1) ? true : false;
    }
#if OFX_FFMPEG_AUDIO
    // The audio is written in ~0.5s chunks only when the video stream position
    // passes a multiple of 0.5s interval. Flushing the video encoder may result
    // in less than 0.5s of video being written to the file so at this point the
    // video duration may be longer than the audio duration. This final stage
    // writes enough audio samples to the file so that the duration of both
    // audio and video are equal.
    double videoTime = _streamVideo->pts.val * av_q2d(_streamVideo->time_base);
    if (_streamAudio) {
        // Determine the current audio stream time.
        double audioTime = _streamAudio->pts.val * av_q2d(_streamAudio->time_base);
        if (audioTime < videoTime) {
            int ret = 0;
            double sourceDuration = audioReader_->getDuration();
            while ( (audioTime < videoTime) && (audioTime < sourceDuration) && !ret ) {
                ret = writeAudio(_formatContext, _streamAudio, true);
                audioTime = _streamAudio->pts.val * av_q2d(_streamAudio->time_base);
            }
        }
    }
#endif

    // Finalise the movie.
    av_write_trailer(_formatContext);

    freeFormat();
}

void
WriteFFmpegPlugin::setOutputFrameRate(double fps)
{
    _fps->setValue(fps);
}

void
WriteFFmpegPlugin::updateVisibility()
{
    //The advanced params are enabled/disabled based on the codec chosen and its capabilities.
    //We also investigated setting the defaults based on the codec defaults, however all current
    //codecs defaulted the same, and as a user experience it was pretty counter intuitive.
    //Check knob exists, to deal with cases where Nuke might not have updated underlying writer (#44774)
    //(we still want to use showPanel to update when loading from script and the like).
    int index = _codec->getValue();
    //assert(index < _codec->getNOptions());
    const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
    string codecShortName;

    _codecShortName->getValue(codecShortName);
    //assert(index < (int)codecsShortNames.size());
    // codecShortName may be empty if this was configured in an old version
    if ( !codecShortName.empty() && ( ( (int)codecsShortNames.size() <= index ) || (codecShortName != codecsShortNames[index]) ) ) {
        _codecShortName->setIsSecret(false); // something may be wrong. Make it visible, at least
    } else {
        _codecShortName->setIsSecret(true);
        if ( (int)codecsShortNames.size() <= index ) {
            codecShortName = codecsShortNames[index];
        }
    }

    AVCodec* codec = avcodec_find_encoder_by_name( codecShortName.c_str() );
    bool bitrateParam    = false;
    bool bitrateTolParam = false;
    bool qualityParams   = false;
    bool interGOPParams  = false;
    bool interBParams    = false;
    if (codec) {
        GetCodecSupportedParams(codec, bitrateParam, bitrateTolParam, qualityParams, interGOPParams, interBParams);
    }

    _bitrate->setIsSecretAndDisabled(!bitrateParam);
    _bitrateTolerance->setIsSecretAndDisabled(!bitrateTolParam);
    _quality->setIsSecretAndDisabled(!qualityParams);

    _gopSize->setIsSecretAndDisabled(!interGOPParams);
    _bFrames->setIsSecretAndDisabled(!interBParams);

    //We use the bitrate to set the min range for bitrate tolerance.
    updateBitrateToleranceRange();

#if OFX_FFMPEG_DNXHD
    // Only enable the DNxHD codec profile knob if the Avid
    // DNxHD codec has been selected.
    // Only enable the video range knob if the Avid DNxHD codec
    // has been selected.
    bool isdnxhd = ( !strcmp(codecShortName.c_str(), "dnxhd") );
    _dnxhdCodecProfile->setIsSecretAndDisabled(!isdnxhd);
    _encodeVideoRange->setIsSecretAndDisabled(!isdnxhd);
#endif


    ///Do not allow custom channel shuffling for the user, it's either RGB or RGBA
    for (int i = 0; i < 4; ++i) {
        _processChannels[i]->setIsSecretAndDisabled(true);
    }
    _outputComponents->setIsSecretAndDisabled(true);
} // WriteFFmpegPlugin::updateVisibility

////////////////////////////////////////////////////////////////////////////////
// updateBitrateToleranceRange
// Utility to update tolerance knob's slider range.
// Employed in place of chained knob_changed calls.
// Only valid for use from knob_changed.
//
void
WriteFFmpegPlugin::updateBitrateToleranceRange()
{
    //Bitrate tolerance should in theory be allowed down to target bitrate/target framerate.
    //We're not force limiting the range since the upper range is not bounded.
    double bitrate = _bitrate->getValue();
    double fps = _fps->getValue();
    // guard agains division by zero
    double minRange = (fps > 0.) ? bitrate / fps : bitrate;

    _bitrateTolerance->setRange(minRange, DBL_MAX);
    _bitrateTolerance->setDisplayRange(minRange, kParamBitrateMax);
}

// Check that the secret codecShortName corresponds to the selected codec.
// It may change because different versions of FFmpeg support different codecs, so the choice menu may
// be different in different instances.
// This is also done in beginEdit() because setValue() cannot be called in the plugin constructor.
void
WriteFFmpegPlugin::checkCodec()
{
    int codec = _codec->getValue();
    const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();

    //assert(codec < (int)codecsShortNames.size());
    string codecShortName;

    _codecShortName->getValue(codecShortName);
    // codecShortName may be empty if this was configured in an old version
    if ( !codecShortName.empty() && ( ( (int)codecsShortNames.size() <= codec ) || (codecShortName != codecsShortNames[codec]) ) ) {
        // maybe it's another one but the label changed, if yes select it
        vector<string>::const_iterator it;

        it = find (codecsShortNames.begin(), codecsShortNames.end(), codecShortName);
        if ( it != codecsShortNames.end() ) {
            // found it! update the choice param
            codec = it - codecsShortNames.begin();
            _codec->setValue(codec);
            // hide the codec name
            _codecShortName->setIsSecret(true);
        } else {
            _codecShortName->setIsSecret(false);
            setPersistentMessage(Message::eMessageError, "", string("writer was configured for unavailable codec \"") + codecShortName + "\".");
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
    } else if ( (int)codecsShortNames.size() <= codec ) {
        setPersistentMessage(Message::eMessageError, "", "writer was configured for unavailable codec.");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
}

// Check that the secret codecShortName corresponds to the selected codec.
// This is also done in beginEdit() because setValue() cannot be called in the plugin constructor.
void
WriteFFmpegPlugin::beginEdit()
{
    checkCodec();
}

void
WriteFFmpegPlugin::onOutputFileChanged(const string &filename,
                                       bool setColorSpace)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, video files are assumed to be rec709.
        if ( _ocio->hasColorspace("Rec709") ) {
            // nuke-default
            _ocio->setOutputColorspace("Rec709");
        } else if ( _ocio->hasColorspace("nuke_rec709") ) {
            // blender
            _ocio->setOutputColorspace("nuke_rec709");
        } else if ( _ocio->hasColorspace("Rec.709 - Full") ) {
            // out_rec709full or "Rec.709 - Full" in aces 1.0.0
            _ocio->setOutputColorspace("Rec.709 - Full");
        } else if ( _ocio->hasColorspace("out_rec709full") ) {
            // out_rec709full or "Rec.709 - Full" in aces 1.0.0
            _ocio->setOutputColorspace("out_rec709full");
        } else if ( _ocio->hasColorspace("rrt_rec709_full_100nits") ) {
            // rrt_rec709_full_100nits in aces 0.7.1
            _ocio->setOutputColorspace("rrt_rec709_full_100nits");
        } else if ( _ocio->hasColorspace("rrt_rec709") ) {
            // rrt_rec709 in aces 0.1.1
            _ocio->setOutputColorspace("rrt_rec709");
        } else if ( _ocio->hasColorspace("hd10") ) {
            // hd10 in spi-anim and spi-vfx
            _ocio->setOutputColorspace("hd10");
        }
#     endif
    }
    // Switch the 'format' knob based on the new filename suffix
    string suffix = filename.substr(filename.find_last_of(".") + 1);
    if ( !suffix.empty() ) {
        // downcase the suffix
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
        // Compare found suffix to known formats
        const vector<string>& formatsShortNames = FFmpegSingleton::Instance().getFormatsShortNames();
        for (size_t i = 0; i < formatsShortNames.size(); ++i) {
            bool setFormat = false;
            if (suffix.compare(formatsShortNames[i]) == 0) {
                setFormat = true;;
            } else if (formatsShortNames[i] == "matroska") {
                if ( (suffix.compare("mkv") == 0) ||
                     ( suffix.compare("mk3d") == 0) ) {
                    setFormat = true;;
                }
            } else if (formatsShortNames[i] == "mpeg") {
                if (suffix.compare("mpg") == 0) {
                    setFormat = true;;
                }
            } else if (formatsShortNames[i] == "mpegts") {
                if ( (suffix.compare("m2ts") == 0) ||
                     ( suffix.compare("mts") == 0) ||
                     ( suffix.compare("ts") == 0) ) {
                    setFormat = true;;
                }
            } else if (formatsShortNames[i] == "mp4") {
                if ( (suffix.compare("mov") == 0) ||
                     ( suffix.compare("3gp") == 0) ||
                     ( suffix.compare("3g2") == 0) ||
                     ( suffix.compare("mj2") == 0) ) {
                    setFormat = true;;
                }
            }
            if (setFormat) {
                _format->setValue(i);
                AVOutputFormat* fmt = av_guess_format(FFmpegSingleton::Instance().getFormatsShortNames()[i].c_str(), NULL, NULL);
                const vector<AVCodecID>& codecs = FFmpegSingleton::Instance().getCodecsIds();
                // is the current codec compatible with this format ?
                if ( !codecCompatible(fmt, codecs[_codec->getValue()]) ) {
                    // set the default codec for this format, or the first compatible codec
                    enum AVCodecID default_video_codec = fmt->video_codec;
                    bool codecSet = false;
                    int compatible_codec = -1;
                    for (size_t c = 0; !codecSet && c < codecs.size(); ++c) {
                        if (codecs[c] == default_video_codec) {
                            _codec->setValue(c);
                            // exit loop
                            codecSet = true;
                        } else if ( (compatible_codec == -1) && codecCompatible(fmt, codecs[c]) ) {
                            compatible_codec = c;
                        }
                    }
                    // if the default codec was not available, use the first compatible codec
                    if ( !codecSet && (compatible_codec != -1) ) {
                        _codec->setValue(compatible_codec);
                    }
                }
                // exit loop
                break;;
            }
        }
    }
    // also check that the codec setting is OK
    checkCodec();
} // WriteFFmpegPlugin::onOutputFileChanged

void
WriteFFmpegPlugin::changedParam(const InstanceChangedArgs &args,
                                const string &paramName)
{
    if (paramName == kParamCodec) {
        // update the secret parameter
        int codec = _codec->getValue();
        const vector<string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
        assert( codec < (int)codecsShortNames.size() );
        _codecShortName->setValue(codecsShortNames[codec]);
        updateVisibility();
        int format = _format->getValue();
        if (format > 0) {
            AVOutputFormat* fmt = av_guess_format(FFmpegSingleton::Instance().getFormatsShortNames()[format].c_str(), NULL, NULL);
            if ( fmt && !codecCompatible(fmt, FFmpegSingleton::Instance().getCodecsIds()[codec]) ) {
                setPersistentMessage(Message::eMessageError, "", "the selected codec is not supported in this container.");
            } else {
                clearPersistentMessage();
            }
        }
    } else if (paramName == kParamFormat) {
        int codec = _codec->getValue();
        int format = _format->getValue();
        if (format > 0) {
            AVOutputFormat* fmt = av_guess_format(FFmpegSingleton::Instance().getFormatsShortNames()[format].c_str(), NULL, NULL);
            if ( fmt && !codecCompatible(fmt, FFmpegSingleton::Instance().getCodecsIds()[codec]) ) {
                setPersistentMessage(Message::eMessageError, "", "the selected codec is not supported in this container.");
            } else {
                clearPersistentMessage();
            }
        }
    } else if ( (paramName == kParamFPS) || (paramName == kParamBitrate) ) {
        updateBitrateToleranceRange();
    } else if ( (paramName == kParamResetFPS) && (args.reason == eChangeUserEdit) ) {
        double fps = _inputClip->getFrameRate();
        _fps->setValue(fps);
        updateBitrateToleranceRange();
    } else if ( (paramName == kParamQuality) && (args.reason == eChangeUserEdit) ) {
        int qMin, qMax;
        _quality->getValue(qMin, qMax);
        if (qMax < qMin) {
            // reorder
            _quality->setValue(qMax, qMin);
        }
    } else {
        GenericWriterPlugin::changedParam(args, paramName);
    }

    // also check that the codec setting is OK
    checkCodec();
}

void
WriteFFmpegPlugin::freeFormat()
{
    if (_streamVideo) {
        avcodec_close(_streamVideo->codec);
        _streamVideo = NULL;
    }
    if (_streamAudio) {
        avcodec_close(_streamAudio->codec);
        _streamAudio = NULL;
    }
    if (_formatContext) {
        if ( !(_formatContext->oformat->flags & AVFMT_NOFILE) ) {
            avio_close(_formatContext->pb);
        }
        avformat_free_context(_formatContext);
        _formatContext = NULL;
    }
    {
        AutoMutex lock(_nextFrameToEncodeMutex);
        _nextFrameToEncode = INT_MIN;
        _firstFrameToEncode = 1;
        _lastFrameToEncode = 1;
        _frameStep = 1;
    }
    _scratchBufferSize = 0;
    delete [] _scratchBuffer;
    _scratchBuffer = 0;
    _isOpen = false;
}

mDeclareWriterPluginFactory(WriteFFmpegPluginFactory, {}, true);
static
std::list<string> &
split(const string &s,
      char delim,
      std::list<string> &elems)
{
    stringstream ss(s);
    string item;

    while ( std::getline(ss, item, delim) ) {
        elems.push_back(item);
    }

    return elems;
}

static
string
ffmpeg_versions()
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

void
WriteFFmpegPluginFactory::load()
{
    _extensions.clear();
#if 0
    // hard-coded extensions list
    const char* extensionsl[] = { "avi", "flv", "mov", "mp4", "mkv", "bmp", "pix", "dpx", "jpeg", "jpg", "png", "pgm", "ppm", "rgba", "rgb", "tiff", "tga", "gif", NULL };
    for (const char** ext = extensionsl; *ext != NULL; ++ext) {
        _extensions.push_back(*ext);
    }
#else
    {
        std::list<string> extensionsl;
        AVOutputFormat* oFormat = av_oformat_next(NULL);
        while (oFormat != NULL) {
            //DBG(std::printf("WriteFFmpeg: \"%s\", // %s (%s)\n", oFormat->extensions ? oFormat->extensions : oFormat->name, oFormat->name, oFormat->long_name));
            if (oFormat->extensions != NULL) {
                string extStr(oFormat->extensions);
                split(extStr, ',', extensionsl);
            }
            {
                // name's format defines (in general) extensions
                // require to fix extension in LibAV/FFMpeg to don't use it.
                string extStr(oFormat->name);
                split(extStr, ',', extensionsl);
            }
            oFormat = av_oformat_next( oFormat );
        }

        // Hack: Add basic video container extensions
        // as some versions of LibAV doesn't declare properly all extensions...
        extensionsl.push_back("avi"); // AVI (Audio Video Interleaved)
        extensionsl.push_back("flv"); // flv (FLV (Flash Video))
        extensionsl.push_back("mkv"); // matroska,webm (Matroska / WebM)
        extensionsl.push_back("mov"); // QuickTime / MOV
        extensionsl.push_back("mp4"); // MP4 (MPEG-4 Part 14)
        extensionsl.push_back("mpg"); // MPEG-1 Systems / MPEG program stream
        extensionsl.push_back("m2ts"); // mpegts (MPEG-TS (MPEG-2 Transport Stream))
        extensionsl.push_back("mts"); // mpegts (MPEG-TS (MPEG-2 Transport Stream))
        extensionsl.push_back("ts"); // mpegts (MPEG-TS (MPEG-2 Transport Stream))
        extensionsl.push_back("mxf"); // mxf (MXF (Material eXchange Format))

        // remove audio and subtitle-only formats
        const char* extensions_blacklist[] = {
            "aa", // aa (Audible AA format files)
            "aac", // aac (raw ADTS AAC (Advanced Audio Coding))
            "ac3", // ac3 (raw AC-3)
            "act", // act (ACT Voice file format)
            // "adf", // adf (Artworx Data Format)
            "adp", "dtk", // adp (ADP) Audio format used on the Nintendo Gamecube.
            "adx", // adx (CRI ADX) Audio-only format used in console video games.
            "aea", // aea (MD STUDIO audio)
            "afc", // afc (AFC) Audio format used on the Nintendo Gamecube.
            "aiff", // aiff (Audio IFF)
            "amr", // amr (3GPP AMR)
            // "anm", // anm (Deluxe Paint Animation)
            "apc", // apc (CRYO APC)
            "ape", "apl", "mac", // ape (Monkey's Audio)
            // "apng", // apng (Animated Portable Network Graphics)
            "aqt", "aqtitle", // aqtitle (AQTitle subtitles)
            // "asf", // asf (ASF (Advanced / Active Streaming Format))
            // "asf_o", // asf_o (ASF (Advanced / Active Streaming Format))
            "ass", // ass (SSA (SubStation Alpha) subtitle)
            "ast", // ast (AST (Audio Stream))
            "au", // au (Sun AU)
            // "avi", // avi (AVI (Audio Video Interleaved))
            "avr", // avr (AVR (Audio Visual Research)) Audio format used on Mac.
            // "avs", // avs (AVS)
            // "bethsoftvid", // bethsoftvid (Bethesda Softworks VID)
            // "bfi", // bfi (Brute Force & Ignorance)
            "bin", // bin (Binary text)
            // "bink", // bink (Bink)
            "bit", // bit (G.729 BIT file format)
            // "bmv", // bmv (Discworld II BMV)
            "bfstm", "bcstm", // bfstm (BFSTM (Binary Cafe Stream)) Audio format used on the Nintendo WiiU (based on BRSTM).
            "brstm", // brstm (BRSTM (Binary Revolution Stream)) Audio format used on the Nintendo Wii.
            "boa", // boa (Black Ops Audio)
            // "c93", // c93 (Interplay C93)
            "caf", // caf (Apple CAF (Core Audio Format))
            // "cavsvideo", // cavsvideo (raw Chinese AVS (Audio Video Standard))
            // "cdg", // cdg (CD Graphics)
            // "cdxl,xl", // cdxl (Commodore CDXL video)
            // "cine", // cine (Phantom Cine)
            // "concat", // concat (Virtual concatenation script)
            // "data", // data (raw data)
            "302", "daud", // daud (D-Cinema audio)
            // "dfa", // dfa (Chronomaster DFA)
            // "dirac", // dirac (raw Dirac)
            // "dnxhd", // dnxhd (raw DNxHD (SMPTE VC-3))
            "dsf", // dsf (DSD Stream File (DSF))
            // "dsicin", // dsicin (Delphine Software International CIN)
            "dss", // dss (Digital Speech Standard (DSS))
            "dts", // dts (raw DTS)
            "dtshd", // dtshd (raw DTS-HD)
            // "dv,dif", // dv (DV (Digital Video))
            "dvbsub", // dvbsub (raw dvbsub)
            // "dxa", // dxa (DXA)
            // "ea", // ea (Electronic Arts Multimedia)
            // "cdata", // ea_cdata (Electronic Arts cdata)
            "eac3", // eac3 (raw E-AC-3)
            "paf", "fap", "epaf", // epaf (Ensoniq Paris Audio File)
            "ffm", // ffm (FFM (FFserver live feed))
            "ffmetadata", // ffmetadata (FFmpeg metadata in text)
            // "flm", // filmstrip (Adobe Filmstrip)
            "flac", // flac (raw FLAC)
            // "flic", // flic (FLI/FLC/FLX animation)
            // "flv", // flv (FLV (Flash Video))
            // "flv", // live_flv (live RTMP FLV (Flash Video))
            // "4xm", // 4xm (4X Technologies)
            // "frm", // frm (Megalux Frame)
            "g722", "722", // g722 (raw G.722)
            "tco", "rco", "g723_1", // g723_1 (G.723.1)
            "g729", // g729 (G.729 raw format demuxer)
            // "gif", // gif (CompuServe Graphics Interchange Format (GIF))
            "gsm", // gsm (raw GSM)
            // "gxf", // gxf (GXF (General eXchange Format))
            // "h261", // h261 (raw H.261)
            // "h263", // h263 (raw H.263)
            // "h26l,h264,264,avc", // h264 (raw H.264 video)
            // "hevc,h265,265", // hevc (raw HEVC video)
            // "hls,applehttp", // hls,applehttp (Apple HTTP Live Streaming)
            // "hnm", // hnm (Cryo HNM v4)
            // "ico", // ico (Microsoft Windows ICO)
            // "idcin", // idcin (id Cinematic)
            // "idf", // idf (iCE Draw File)
            // "iff", // iff (IFF (Interchange File Format))
            "ilbc", // ilbc (iLBC storage)
            // "image2", // image2 (image2 sequence)
            "image2pipe", // image2pipe (piped image2 sequence)
            "alias_pix", // alias_pix (Alias/Wavefront PIX image)
            "brender_pix", // brender_pix (BRender PIX image)
            // "cgi", // ingenient (raw Ingenient MJPEG)
            "ipmovie", // ipmovie (Interplay MVE)
            "sf", "ircam", // ircam (Berkeley/IRCAM/CARL Sound Format)
            "iss", // iss (Funcom ISS)
            // "iv8", // iv8 (IndigoVision 8000 video)
            // "ivf", // ivf (On2 IVF)
            "jacosub", // jacosub (JACOsub subtitle format)
            "jv", // jv (Bitmap Brothers JV)
            "latm", // latm (raw LOAS/LATM)
            "lmlm4", // lmlm4 (raw lmlm4)
            "loas", // loas (LOAS AudioSyncStream)
            "lrc", // lrc (LRC lyrics)
            // "lvf", // lvf (LVF)
            // "lxf", // lxf (VR native stream (LXF))
            // "m4v", // m4v (raw MPEG-4 video)
            "mka", "mks", // "mkv,mk3d,mka,mks", // matroska,webm (Matroska / WebM)
            "mgsts", // mgsts (Metal Gear Solid: The Twin Snakes)
            "microdvd", // microdvd (MicroDVD subtitle format)
            // "mjpg,mjpeg,mpo", // mjpeg (raw MJPEG video)
            "mlp", // mlp (raw MLP)
            // "mlv", // mlv (Magic Lantern Video (MLV))
            "mm", // mm (American Laser Games MM)
            "mmf", // mmf (Yamaha SMAF)
            "m4a", // "mov,mp4,m4a,3gp,3g2,mj2", // mov,mp4,m4a,3gp,3g2,mj2 (QuickTime / MOV)
            "mp2", "mp3", "m2a", "mpa", // mp3 (MP2/3 (MPEG audio layer 2/3))
            "mpc", // mpc (Musepack)
            "mpc8", // mpc8 (Musepack SV8)
            // "mpeg", // mpeg (MPEG-PS (MPEG-2 Program Stream))
            "mpegts", // mpegts (MPEG-TS (MPEG-2 Transport Stream))
            "mpegtsraw", // mpegtsraw (raw MPEG-TS (MPEG-2 Transport Stream))
            "mpegvideo", // mpegvideo (raw MPEG video)
            // "mjpg", // mpjpeg (MIME multipart JPEG)
            "txt", "mpl2", // mpl2 (MPL2 subtitles)
            "sub", "mpsub", // mpsub (MPlayer subtitles)
            "msnwctcp", // msnwctcp (MSN TCP Webcam stream)
            // "mtv", // mtv (MTV)
            // "mv", // mv (Silicon Graphics Movie)
            // "mvi", // mvi (Motion Pixels MVI)
            // "mxf", // mxf (MXF (Material eXchange Format))
            // "mxg", // mxg (MxPEG clip)
            // "v", // nc (NC camera feed)
            "nist", "sph", "nistsphere", // nistsphere (NIST SPeech HEader REsources)
            // "nsv", // nsv (Nullsoft Streaming Video)
            // "nut", // nut (NUT)
            // "nuv", // nuv (NuppelVideo)
            // "ogg", // ogg (Ogg)
            "oma", "omg", "aa3", // oma (Sony OpenMG audio)
            // "paf", // paf (Amazing Studio Packed Animation File)
            "al", "alaw", // alaw (PCM A-law)
            "ul", "mulaw", // mulaw (PCM mu-law)
            "f64be", // f64be (PCM 64-bit floating-point big-endian)
            "f64le", // f64le (PCM 64-bit floating-point little-endian)
            "f32be", // f32be (PCM 32-bit floating-point big-endian)
            "f32le", // f32le (PCM 32-bit floating-point little-endian)
            "s32be", // s32be (PCM signed 32-bit big-endian)
            "s32le", // s32le (PCM signed 32-bit little-endian)
            "s24be", // s24be (PCM signed 24-bit big-endian)
            "s24le", // s24le (PCM signed 24-bit little-endian)
            "s16be", // s16be (PCM signed 16-bit big-endian)
            "sw", "s16le", // s16le (PCM signed 16-bit little-endian)
            "sb", "s8", // s8 (PCM signed 8-bit)
            "u32be", // u32be (PCM unsigned 32-bit big-endian)
            "u32le", // u32le (PCM unsigned 32-bit little-endian)
            "u24be", // u24be (PCM unsigned 24-bit big-endian)
            "u24le", // u24le (PCM unsigned 24-bit little-endian)
            "u16be", // u16be (PCM unsigned 16-bit big-endian)
            "uw", "u16le", // u16le (PCM unsigned 16-bit little-endian)
            "ub", "u8", // u8 (PCM unsigned 8-bit)
            "pjs", // pjs (PJS (Phoenix Japanimation Society) subtitles)
            "pmp", // pmp (Playstation Portable PMP)
            "pva", // pva (TechnoTrend PVA)
            "pvf", // pvf (PVF (Portable Voice Format))
            "qcp", // qcp (QCP)
            // "r3d", // r3d (REDCODE R3D)
            // "yuv,cif,qcif,rgb", // rawvideo (raw video)
            "rt", "realtext", // realtext (RealText subtitle format)
            "rsd", "redspark", // redspark (RedSpark)
            // "rl2", // rl2 (RL2)
            // "rm", // rm (RealMedia)
            // "roq", // roq (id RoQ)
            // "rpl", // rpl (RPL / ARMovie)
            "rsd", // rsd (GameCube RSD)
            "rso", // rso (Lego Mindstorms RSO)
            "rtp", // rtp (RTP input)
            "rtsp", // rtsp (RTSP input)
            "smi", "sami", // sami (SAMI subtitle format)
            "sap", // sap (SAP input)
            "sbg", // sbg (SBaGen binaural beats script)
            "sdp", // sdp (SDP)
            // "sdr2", // sdr2 (SDR2)
            "film_cpk", // film_cpk (Sega FILM / CPK)
            "shn", // shn (raw Shorten)
            // "vb,son", // siff (Beam Software SIFF)
            // "sln", // sln (Asterisk raw pcm)
            "smk", // smk (Smacker)
            // "mjpg", // smjpeg (Loki SDL MJPEG)
            "smush", // smush (LucasArts Smush)
            "sol", // sol (Sierra SOL)
            "sox", // sox (SoX native)
            "spdif", // spdif (IEC 61937 (compressed data in S/PDIF))
            "srt", // srt (SubRip subtitle)
            "psxstr", // psxstr (Sony Playstation STR)
            "stl", // stl (Spruce subtitle format)
            "sub", "subviewer1", // subviewer1 (SubViewer v1 subtitle format)
            "sub", "subviewer", // subviewer (SubViewer subtitle format)
            "sup", // sup (raw HDMV Presentation Graphic Stream subtitles)
            // "swf", // swf (SWF (ShockWave Flash))
            "tak", // tak (raw TAK)
            "tedcaptions", // tedcaptions (TED Talks captions)
            "thp", // thp (THP)
            "tiertexseq", // tiertexseq (Tiertex Limited SEQ)
            "tmv", // tmv (8088flex TMV)
            "thd", "truehd", // truehd (raw TrueHD)
            "tta", // tta (TTA (True Audio))
            // "txd", // txd (Renderware TeXture Dictionary)
            "ans", "art", "asc", "diz", "ice", "nfo", "txt", "vt", // tty (Tele-typewriter)
            // "vc1", // vc1 (raw VC-1)
            "vc1test", // vc1test (VC-1 test bitstream)
            // "viv", // vivo (Vivo)
            "vmd", // vmd (Sierra VMD)
            "idx", "vobsub", // vobsub (VobSub subtitle format)
            "voc", // voc (Creative Voice)
            "txt", "vplayer", // vplayer (VPlayer subtitles)
            "vqf", "vql", "vqe", // vqf (Nippon Telegraph and Telephone Corporation (NTT) TwinVQ)
            "w64", // w64 (Sony Wave64)
            "wav", // wav (WAV / WAVE (Waveform Audio))
            "wc3movie", // wc3movie (Wing Commander III movie)
            "webm_dash_manifest", // webm_dash_manifest (WebM DASH Manifest)
            "vtt", "webvtt", // webvtt (WebVTT subtitle)
            "wsaud", // wsaud (Westwood Studios audio)
            "wsvqa", // wsvqa (Westwood Studios VQA)
            "wtv", // wtv (Windows Television (WTV))
            "wv", // wv (WavPack)
            "xa", // xa (Maxis XA)
            "xbin", // xbin (eXtended BINary text (XBIN))
            "xmv", // xmv (Microsoft XMV)
            "xwma", // xwma (Microsoft xWMA)
            // "yop", // yop (Psygnosis YOP)
            // "y4m", // yuv4mpegpipe (YUV4MPEG pipe)
            "bmp_pipe", // bmp_pipe (piped bmp sequence)
            "dds_pipe", // dds_pipe (piped dds sequence)
            "dpx_pipe", // dpx_pipe (piped dpx sequence)
            "exr_pipe", // exr_pipe (piped exr sequence)
            "j2k_pipe", // j2k_pipe (piped j2k sequence)
            "jpeg_pipe", // jpeg_pipe (piped jpeg sequence)
            "jpegls_pipe", // jpegls_pipe (piped jpegls sequence)
            "pictor_pipe", // pictor_pipe (piped pictor sequence)
            "png_pipe", // png_pipe (piped png sequence)
            "qdraw_pipe", // qdraw_pipe (piped qdraw sequence)
            "sgi_pipe", // sgi_pipe (piped sgi sequence)
            "sunrast_pipe", // sunrast_pipe (piped sunrast sequence)
            "tiff_pipe", // tiff_pipe (piped tiff sequence)
            "webp_pipe", // webp_pipe (piped webp sequence)

            // OIIO and PFM extensions:
            "bmp", "cin", /*"dds",*/ "dpx", /*"f3d",*/ "fits", "hdr", "ico",
            "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png",
            "pbm", "pgm", "ppm",
            "pfm",
            "psd", "pdd", "psb", /*"ptex",*/ "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile",

            NULL
        };
        for (const char*const* e = extensions_blacklist; *e != NULL; ++e) {
            extensionsl.remove(*e);
        }

        _extensions.assign( extensionsl.begin(), extensionsl.end() );
        // sort / unique
        std::sort( _extensions.begin(), _extensions.end() );
        _extensions.erase( std::unique( _extensions.begin(), _extensions.end() ), _extensions.end() );
    }
#endif // if 0
} // WriteFFmpegPluginFactory::load

/** @brief The basic describe function, passed a plugin descriptor */
void
WriteFFmpegPluginFactory::describe(ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc, eRenderFullySafe, _extensions, kPluginEvaluation, false, false);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription( "Write images or video file using "
#                             ifdef FFMS_USE_FFMPEG_COMPAT
                               "FFmpeg"
#                             else
                               "libav"
#                             endif
                               ".\n\n" + ffmpeg_versions() );


    ///This plug-in only supports sequential render
    desc.setSequentialRender(true);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
WriteFFmpegPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                            ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsXY,
                                                                    "scene_linear", "rec709", false);

    ///If the host doesn't support sequential render, fail.
    int hostSequentialRender = getImageEffectHostDescription()->sequentialRender;

    if (hostSequentialRender == 0) {
        //throwSuiteStatusException(kOfxStatErrMissingHostFeature);
    }


    ///////////Output format
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
        const vector<string>& formatsV = FFmpegSingleton::Instance().getFormatsLongNames();
        const vector<vector<size_t> >& formatsCodecs = FFmpegSingleton::Instance().getFormatsCodecs();
        const vector<string>& codecsV = FFmpegSingleton::Instance().getCodecsShortNames();
        param->setLabel(kParamFormatLabel);
        param->setHint(kParamFormatHint);
        for (unsigned int i = 0; i < formatsV.size(); ++i) {
            if ( formatsCodecs[i].empty() ) {
                param->appendOption(formatsV[i]);
            } else {
                string hint = "Compatible with ";
                for (unsigned int j = 0; j < formatsCodecs[i].size(); ++j) {
                    if (j != 0) {
                        hint.append(", ");
                    }
                    hint.append(codecsV[formatsCodecs[i][j]]);
                }
                hint.append(".");
                param->appendOption(formatsV[i], hint);
            }
        }
        param->setAnimates(false);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }

    ///////////Codec
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamCodec);
        param->setLabel(kParamCodecName);
        param->setHint(kParamCodecHint);
        const vector<string>& codecsV = FFmpegSingleton::Instance().getCodecsKnobLabels();// getCodecsLongNames();
        const vector<vector<size_t> >& codecsFormats = FFmpegSingleton::Instance().getCodecsFormats();
        const vector<string>& formatsV = FFmpegSingleton::Instance().getFormatsShortNames();
        for (unsigned int i = 0; i < codecsV.size(); ++i) {
            if ( codecsFormats[i].empty() ) {
                param->appendOption(codecsV[i]);
            } else {
                string hint = "Compatible with ";
                for (unsigned int j = 0; j < codecsFormats[i].size(); ++j) {
                    if (j != 0) {
                        hint.append(", ");
                    }
                    hint.append(formatsV[codecsFormats[i][j]]);
                }
                hint.append(".");
                param->appendOption(codecsV[i], hint);
            }
        }
        param->setAnimates(false);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // codecShortName: a secret parameter that holds the real codec name
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamCodecShortName);
        param->setLabel(kParamCodecShortNameLabel);
        param->setHint(kParamCodecShortNameHint);
        param->setAnimates(false);
        param->setEnabled(false); // non-editable
        if (page) {
            page->addChild(*param);
        }
    }

    ///////////FPS
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFPS);
        param->setLabel(kParamFPSLabel);
        param->setHint(kParamFPSHint);
        param->setRange(0., 100.);
        param->setDisplayRange(0., 100.);
        param->setDefault(24.); // should be set from the input FPS
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetFPS);
        param->setLabel(kParamResetFPSLabel);
        param->setHint(kParamResetFPSHint);
        if (page) {
            page->addChild(*param);
        }
    }

#if OFX_FFMPEG_DNXHD
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDNxHDCodecProfile);
        param->setLabel(kParamDNxHDCodecProfileLabel);
        param->setHint(kParamDNxHDCodecProfileHint);
        assert(param->getNOptions() == (int)eDNxHDCodecProfile440x);
        param->appendOption(kParamDNxHDCodecProfileOption440x, kParamDNxHDCodecProfileOption440xHint);
        assert(param->getNOptions() == (int)eDNxHDCodecProfile220x);
        param->appendOption(kParamDNxHDCodecProfileOption220x, kParamDNxHDCodecProfileOption220xHint);
        assert(param->getNOptions() == (int)eDNxHDCodecProfile220);
        param->appendOption(kParamDNxHDCodecProfileOption220, kParamDNxHDCodecProfileOption220Hint);
        assert(param->getNOptions() == (int)eDNxHDCodecProfile145);
        param->appendOption(kParamDNxHDCodecProfileOption145, kParamDNxHDCodecProfileOption145Hint);
        assert(param->getNOptions() == (int)eDNxHDCodecProfile36);
        param->appendOption(kParamDNxHDCodecProfileOption36, kParamDNxHDCodecProfileOption36Hint);

        param->setAnimates(false);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }
#endif

#if OFX_FFMPEG_TIMECODE
    ///////////Write Time Code
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamWriteTimeCode);
        param->setLabel(kParamWriteTimeCodeLabel);
        param->setHint(kParamWriteTimeCodeHint);
        param->setDefault(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
#endif

    /////////// Advanced group
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamAdvanced);
        group->setLabel(kParamAdvancedLabel);
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }

        ///////////Enable Alpha
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnableAlpha);
            param->setLabel(kParamEnableAlphaLabel);
            param->setHint(kParamEnableAlphaHint);
            param->setDefault(true);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

#if OFX_FFMPEG_DNXHD
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDNxHDEncodeVideoRange);
            param->setLabel(kParamDNxHDEncodeVideoRangeLabel);
            param->setHint(kParamDNxHDEncodeVideoRangeHint);
            param->appendOption(kParamDNxHDEncodeVideoRangeOptionFull);
            param->appendOption(kParamDNxHDEncodeVideoRangeOptionVideo);
            param->setAnimates(false);
            param->setDefault(1);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
#endif

        ///////////bit-rate
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBitrate);
            param->setLabel(kParamBitrateLabel);
            param->setHint(kParamBitrateHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, kParamBitrateMax);
            param->setDefault(kParamBitrateDefault);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ///////////bit-rate tolerance
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBitrateTolerance);
            param->setLabel(kParamBitrateToleranceLabel);
            param->setHint(kParamBitrateToleranceHint);
            param->setRange(0, kParamBitrateMax);
            param->setDefault(kParamBitrateMax);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ////////////Quality
        {
            Int2DParamDescriptor* param = desc.defineInt2DParam(kParamQuality);
            param->setLabel(kParamQualityLabel);
            param->setHint(kParamQualityHint);
            param->setRange(0, 0, 100, 100);
            param->setDefault(2, 31);
            param->setDimensionLabels("min", "max");
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ///////////Gop size
        {
            IntParamDescriptor* param = desc.defineIntParam(kParamGopSize);
            param->setLabel(kParamGopSizeLabel);
            param->setHint(kParamGopSizeHint);
            param->setRange(0, 30);
            param->setDefault(12);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ////////////B Frames
        {
            IntParamDescriptor* param = desc.defineIntParam(kParamBFrames);
            param->setLabel(kParamBFramesLabel);
            param->setHint(kParamBFramesHint);
            param->setRange(-1, FF_MAX_B_FRAMES);
            param->setDefault(0);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
        ///////////Write NCLC
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamWriteNCLC);
            param->setLabel(kParamWriteNCLCLabel);
            param->setHint(kParamWriteNCLCHint);
            param->setDefault(true);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

#if OFX_FFMPEG_MBDECISION
        ////////////Macro block decision
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMBDecision);
            param->setLabel("Macro block decision mode");
            assert(param->getNOptions() == FF_MB_DECISION_SIMPLE);
            param->appendOption("FF_MB_DECISION_SIMPLE");
            assert(param->getNOptions() == FF_MB_DECISION_BITS);
            param->appendOption("FF_MB_DECISION_BITS");
            assert(param->getNOptions() == FF_MB_DECISION_RD);
            param->appendOption("FF_MB_DECISION_RD");
            param->setDefault(0);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
#endif
    }
    GenericWriterDescribeInContextEnd(desc, context, page);
} // WriteFFmpegPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
WriteFFmpegPluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    WriteFFmpegPlugin* ret = new WriteFFmpegPlugin(handle, _extensions);

    ret->restoreStateFromParams();

    return ret;
}

static WriteFFmpegPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
