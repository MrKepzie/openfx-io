/*
 OFX ffmpegWriter plugin.
 Writes a video output file using the libav library.
 
 Copyright (C) 2015 INRIA
 Authors:
    Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
    Frederic Devernay frederic.devernay@inria.fr

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
 
 inspired by mov64Writer.cpp
 Copyright (c) 2014 The Foundry Visionmongers Ltd.  All Rights Reserved.
 */


#if (defined(_STDINT_H) || defined(_STDINT_H_) || defined(_MSC_STDINT_H_)) && !defined(UINT64_C)
#warning "__STDC_CONSTANT_MACROS has to be defined before including <stdint.h>, this file will probably not compile."
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // ...or stdint.h wont' define UINT64_C, needed by libavutil
#endif
#include "WriteFFmpeg.h"

#include <cstdio>
#include <cstring>
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
#define OFX_FFMPEG_DNXHD 1        // experimental DNxHD support (should use porofiles)

#if OFX_FFMPEG_PRINT_CODECS
#include <iostream>
#endif

#define kPluginName "WriteFFmpeg"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write images using FFmpeg."
#define kPluginIdentifier "fr.inria.openfx.WriteFFmpeg"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha false

#define kParamFormat "format"
#define kParamFormatLabel "Format"
#define kParamFormatHint "Output format/container."

#define kParamCodec "codec"
#define kParamCodecName "Codec"
#define kParamCodecHint "Output codec used for encoding."

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

#define kParamBitrate "bitrate"
#define kParamBitrateLabel "Bitrate"
#define kParamBitrateHint \
"The target bitrate the codec will attempt to reach, within the confines of the bitrate tolerance and " \
"quality min/max settings. Only supported by certain codecs."

#define kParamBitrateTolerance "bitrateTolerance"
#define kParamBitrateToleranceLabel "Bitrate Tolerance"
#define kParamBitrateToleranceHint \
"The amount the codec is allowed to vary from the target bitrate based on image and quality settings. " \
"Exercise caution with this control as too small a number for your image data will result in failed renders. " \
"As a guideline, the minimum slider range of target bitrate/target fps is the lowest advisable setting. " \
"Only supported by certain codecs."

#define kParamQuality "quality"
#define kParamQualityLabel "Quality"
#define kParamQualityHint \
"The quality range the codec is allowed to vary the image data quantiser " \
"between to attempt to hit the desired bitrate. Higher values mean increased " \
"image degradation is possible, but with the upside of lower bit rates. " \
"Only supported by certain codecs."

#define kParamGopSize "gopSize"
#define kParamGopSizeLabel "GOP Size"
#define kParamGopSizeHint \
"Specifies how many frames may be grouped together by the codec to form a compression GOP. Exercise caution " \
"with this control as it may impact whether the resultant file can be opened in other packages. Only supported by " \
"certain codecs."

#define kParamBFrames "bFrames"
#define kParamBFramesLabel "B Frames"
#define kParamBFramesHint \
"Controls the maximum number of B frames found consecutively in the resultant stream, where zero means no limit " \
"imposed. Only supported by certain codecs."

#define kParamWriteNCLC "writeNCLC"
#define kParamWriteNCLCLabel "Write NCLC"
#define kParamWriteNCLCHint \
"Write nclc data in the colr atom of the video header."

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
// Valid DNxHD profiles (as of FFmpeg 2.6.1):
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

//#define AVID_DNXHD_444_440X_NAME "DNxHD 444 10-bit 440Mbit"
#define AVID_DNXHD_422_220X_NAME "DNxHD 422 10-bit 220Mbit"
#define AVID_DNXHD_422_220_NAME "DNxHD 422 8-bit 220Mbit"
#define AVID_DNXHD_422_145_NAME "DNxHD 422 8-bit 145Mbit"
#define AVID_DNXHD_422_36_NAME "DNxHD 422 8-bit 36Mbit"

#define kParamDNxHDCodecProfile "DNxHDCodecProfile"
#define kParamDNxHDCodecProfileLabel "DNxHD Codec Profile"
#define kParamDNxHDCodecProfileHint "Only for the Avid DNxHD codec, select the target bit rate for the encoded movie. The stream may be resized to 1920x1080 if resolution is not supported."
//#define kParamDNxHDCodecProfileOption440x AVID_DNXHD_444_440X_NAME
//#define kParamDNxHDCodecProfileOption440xHint AVID_DNXHD_444_440X_NAME
#define kParamDNxHDCodecProfileOption220x AVID_DNXHD_422_220X_NAME
#define kParamDNxHDCodecProfileOption220xHint "440x in 1080p/60 or 1080p/59.94, 365x in 1080p/50, 220x in 1080i/60 or 1080i/59.94, 185x in 1080i/50 or 1080p/25, 175x in 1080p/24 or 1080p/23.976, 220x in 1080p/29.97, 220x in 720p/59.94, 175x in 720p/50"
#define kParamDNxHDCodecProfileOption220  AVID_DNXHD_422_220_NAME
#define kParamDNxHDCodecProfileOption220Hint  "440 in 1080p/60 or 1080p/59.94, 365 in 1080p/50, 220 in 1080i/60 or 1080i/59.94, 185 in 1080i/50 or 1080p/25, 175 in 1080p/24 or 1080p/23.976, 220 in 1080p/29.97, 220 in 720p/59.94, 175 in 720p/50"
#define kParamDNxHDCodecProfileOption145  AVID_DNXHD_422_145_NAME
#define kParamDNxHDCodecProfileOption145Hint  "290 in 1080p/60 or 1080p/59.94, 240 in 1080p/50, 145 in 1080i/60 or 1080i/59.94, 120 in 1080i/50 or 1080p/25, 115 in 1080p/24 or 1080p/23.976, 145 in 1080p/29.97, 145 in 720p/59.94, 115 in 720p/50"
#define kParamDNxHDCodecProfileOption36   AVID_DNXHD_422_36_NAME
#define kParamDNxHDCodecProfileOption36Hint   "90 in 1080p/60 or 1080p/59.94, 75 in 1080p/50, 45 in 1080i/60 or 1080i/59.94, 36 in 1080i/50 or 1080p/25, 36 in 1080p/24 or 1080p/23.976, 45 in 1080p/29.97, 100 in 720p/59.94, 85 in 720p/50"

enum DNxHDCodecProfileEnum {
    //eDNxHDCodecProfile440x,
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

#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// The fourccs for MJPEGA, MJPEGB, and Photo JPEG
static const char*  kJpegCodecs[] = { "jpeg", "mjpa", "mjpb" };
static const int    kNumJpegCodecs = sizeof(kJpegCodecs) / sizeof(kJpegCodecs[0]);

static bool IsJpeg(OFX::ChoiceParam *codecParam, int codecValue)
{
    std::string strCodec;
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

typedef std::map<std::string, std::string> CodecMap;

static CodecMap CreateCodecKnobLabelsMap()
{
    CodecMap m;

    // Video codecs.
#if OFX_FFMPEG_DNXHD
    m["dnxhd"]         = "AVdn\tVC3/DNxHD";
#endif
    m["mjpeg"]         = "jpeg\tPhoto - JPEG";
    m["mpeg1video"]    = "mp1v\tMPEG-1 Video";
    m["mpeg2video"]    = "mp2v\tMPEG-2 Video";
    m["mpeg4"]         = "mp4v\tMPEG-4 Video";
    m["png"]           = "png \tPNG";
    m["qtrle"]         = "rle \tAnimation";
    m["v210"]          = "v210\tUncompressed 10-bit 4:2:2";
    return m;
}

static const CodecMap kCodecKnobLabels = CreateCodecKnobLabelsMap();

static
const char* getCodecKnobLabel(const char* codecShortName)
{
    CodecMap::const_iterator it = kCodecKnobLabels.find(std::string(codecShortName));
    if (it != kCodecKnobLabels.end()) {
        return it->second.c_str();
    } else {
        return NULL;
    }
}

static
const char* getCodecFromShortName(const std::string& name)
{
    std::string prefix(kProresCodec);
    if (!name.compare(0, prefix.size(), prefix)) {
        return kProresCodec;
    }
    return name.c_str();
}

static
int getProfileFromShortName(const std::string& name)
{
    std::string prefix(kProresCodec);
    if (!name.compare(0, prefix.size(), prefix)) {
        if (!name.compare(prefix.size(), std::string::npos, kProresProfile4444FourCC)) {
            return kProresProfile4444;
        }
        if (!name.compare(prefix.size(), std::string::npos, kProresProfileHQFourCC)) {
            return kProresProfileHQ;
        }
        if (!name.compare(prefix.size(), std::string::npos, kProresProfileSQFourCC)) {
            return kProresProfileSQ;
        }
        if (!name.compare(prefix.size(), std::string::npos, kProresProfileLTFourCC)) {
            return kProresProfileLT;
        }
        if (!name.compare(prefix.size(), std::string::npos, kProresProfileProxyFourCC)) {
            return kProresProfileProxy;
        }
    }
    return -1;
}

struct AVCodecContext;
struct AVFormatContext;
struct AVStream;

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
    MyAVFrame& operator=(const MyAVFrame& rhs) { return *this; }
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
int MyAVFrame::alloc(int width, int height, enum AVPixelFormat avPixelFormat, int align)
{
    int ret = 0;
    if (!_avFrame)
        _avFrame = av_frame_alloc();
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
int MyAVFrame::alloc(int nbChannels, int nbSamples, enum AVSampleFormat avSampleFormat, int align)
{
    int ret = 0;
    if (!_avFrame)
        _avFrame = av_frame_alloc();
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
void MyAVFrame::deallocateAVFrameData()
{
    if (_avFrame && _avFrame->data[0])
        av_freep(_avFrame->data);
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
            _avPicture.data[i] = NULL;
            _avPicture.linesize[i] = 0;
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

    // operator dereference overload.
    AVPicture* operator->() { return &_avPicture; }
    // operator type cast overload.
    operator AVPicture*() { return &_avPicture; }

private:
    AVPicture _avPicture;

    // Release any memory allocated to the data member variable of
    // the AVPicture structure.
    void deallocateAVPictureData();

    // Probably do not need the following or the headaches
    // of trying to support the functionality.
    // Hide the copy constuctor. Who would manage memory?
    MyAVPicture(MyAVPicture& avPicture);
    // Hide the assignment operator. Who would manage memory?
    MyAVPicture& operator=(const MyAVPicture& rhs) { return *this; }
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
int MyAVPicture::alloc(int width, int height, enum AVPixelFormat avPixelFormat)
{
    deallocateAVPictureData(); // In case this method is called multiple times on the same object.
    int ret = avpicture_alloc(&_avPicture, avPixelFormat, width, height);
    return ret;
}


// Release any memory allocated to the data member variable of
// the AVPicture structure.
void MyAVPicture::deallocateAVPictureData()
{
    if (_avPicture.data[0]) {
        avpicture_free(&_avPicture);
        for (int i = 0; i < 4; ++i) {
            _avPicture.data[i] = NULL;
            _avPicture.linesize[i] = 0;
        }
    }
}




class WriteFFmpegPlugin : public GenericWriterPlugin
{
private:
    enum WriterError { SUCCESS = 0, IGNORE_FINISH, CLEANUP };

public:

    WriteFFmpegPlugin(OfxImageEffectHandle handle);

    virtual ~WriteFFmpegPlugin();

private:
    void updateVisibility();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    virtual void onOutputFileChanged(const std::string &filename) OVERRIDE FINAL;

    virtual void beginEncode(const std::string& filename,const OfxRectI& rod,const OFX::BeginSequenceRenderArguments& args) OVERRIDE FINAL;

    virtual void endEncode(const OFX::EndSequenceRenderArguments& args) OVERRIDE FINAL;

    virtual void encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;


    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;

    virtual void setOutputFrameRate(double fps) OVERRIDE FINAL;
    
    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const { return OFX::eImageUnPreMultiplied; }

    void freeFormat();
    AVColorTransferCharacteristic getColorTransferCharacteristic() const;
    AVPixelFormat                 getPixelFormat(AVCodec* videoCodec) const;
    AVOutputFormat*               initFormat(bool reportErrors) const;
    bool                          initCodec(AVOutputFormat* fmt, AVCodecID& outCodecId, AVCodec*& outCodec) const;

    void getPixelFormats(AVCodec*          videoCodec,
                         AVPixelFormat&    outNukeBufferPixelFormat,
                         AVPixelFormat&    outTargetPixelFormat,
                         int&              outBitDepth) const;

    int encodeVideo(AVCodecContext* avCodecContext, uint8_t* out, int outSize, const AVFrame* avFrame);
    void updateBitrateToleranceRange();
    bool isRec709Format(const int height) const;
    static bool IsYUV(AVPixelFormat pixelFormat);
    static bool IsYUVFromShortName(const char* shortName, int codecProfile);
    static bool IsRGBFromShortName(const char* shortName, int codecProfile);


    static int            GetPixelFormatBitDepth(const AVPixelFormat pixelFormat);
    static AVPixelFormat  GetPixelFormatFromBitDepth(const int bitDepth, const bool hasAlpha);
    static void           GetCodecSupportedParams(AVCodec* codec, bool& outLossyParams,
                                                  bool& outInterGOPParams, bool& outInterBParams);

    void configureAudioStream(AVCodec* avCodec, AVStream* avStream);
    void configureVideoStream(AVCodec* avCodec, AVStream* avStream);
    void configureTimecodeStream(AVCodec* avCodec, AVStream* avStream);
    AVStream* addStream(AVFormatContext* avFormatContext, enum AVCodecID avCodecId, AVCodec** pavCodec);
    int openCodec(AVFormatContext* avFormatContext, AVCodec* avCodec, AVStream* avStream);
    int writeAudio(AVFormatContext* avFormatContext, AVStream* avStream, bool flush);
    int writeVideo(AVFormatContext* avFormatContext, AVStream* avStream, bool flush, const float *pixelData = NULL, const OfxRectI* bounds = NULL, OFX::PixelComponentEnum pixelComponents = OFX::ePixelComponentNone, int rowBytes = 0);
    int writeToFile(AVFormatContext* avFormatContext, bool finalise, const float *pixelData = NULL, const OfxRectI* bounds = NULL, OFX::PixelComponentEnum pixelComponents = OFX::ePixelComponentNone, int rowBytes = 0);

    int colourSpaceConvert(AVPicture* avPicture, AVFrame* avFrame, AVPixelFormat srcPixelFormat, AVPixelFormat dstPixelFormat, AVCodecContext* avCodecContext);

    // Returns true if the selected channels contain alpha and that the channel is valid
    bool alphaEnabled() const;

    // Returns the nmber of destination channels this will write into.
    int numberOfDestChannels() const;

    bool codecIndexIsInRange( unsigned int codecIndex) const;
    bool codecIsDisallowed( const std::string& codecShortName, std::string& reason ) const;

    ///These members are not protected and only read/written by/to by the same thread.
    std::string _filename;
    OfxRectI _rod;
    bool _isOpen; // Flag for the configuration state of the FFmpeg components.
    WriterError _error;
    AVFormatContext*  _formatContext;
    AVStream* _streamVideo;
    AVStream* _streamAudio;
    AVStream* _streamTimecode;
    int _lastTimeEncoded; //< the frame index of the last frame encoded.

    OFX::ChoiceParam* _format;
    OFX::DoubleParam* _fps;
#ifdef OFX_FFMPEG_DNXHD
    OFX::ChoiceParam* _dnxhdCodecProfile;
    OFX::ChoiceParam* _encodeVideoRange;
#endif
#if OFX_FFMPEG_TIMECODE
    OFX::BooleanParam* _writeTimeCode;
#endif

    OFX::ChoiceParam* _codec;
    OFX::IntParam* _bitrate;
    OFX::IntParam* _bitrateTolerance;
    OFX::Int2DParam* _quality;
    OFX::IntParam* _gopSize;
    OFX::IntParam* _bFrames;
    OFX::BooleanParam* _writeNCLC;
#if OFX_FFMPEG_MBDECISION
    OFX::ChoiceParam* _mbDecision;
#endif
};





class FFmpegSingleton {
    
public:
    
    static FFmpegSingleton &Instance() {
        return m_instance;
    };
    
    
    const std::vector<std::string>& getFormatsShortNames() const { return _formatsShortNames; }
    
    const std::vector<std::string>& getFormatsLongNames() const { return _formatsLongNames; }
    
    const std::vector<std::string>& getCodecsShortNames() const { return _codecsShortNames; }
    
    const std::vector<std::string>& getCodecsLongNames() const { return _codecsLongNames; }

    const std::vector<std::string>& getCodecsKnobLabels() const { return _codecsKnobLabels; }

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
    std::vector<std::string> _codecsKnobLabels;
};

FFmpegSingleton FFmpegSingleton::m_instance = FFmpegSingleton();

FFmpegSingleton::FFmpegSingleton()
{
    // TODO: add a log buffer and a way to display it / clear it.
    av_log_set_level(AV_LOG_WARNING);
    //av_log_set_level(AV_LOG_DEBUG);
    av_register_all();
    
    AVOutputFormat* fmt = av_oformat_next(NULL);
    while (fmt) {
        
        if (fmt->video_codec != AV_CODEC_ID_NONE) {
            if (FFmpegFile::isFormatWhitelistedForWriting( fmt->name ) ) {
                if (fmt->long_name) {
                    _formatsLongNames.push_back(std::string(fmt->long_name) + std::string(" (") + std::string(fmt->name) + std::string(")"));
                    _formatsShortNames.push_back(fmt->name);
#                 if OFX_FFMPEG_PRINT_CODECS
                    std::cout << "Format: " << fmt->name << " = " << fmt->long_name << std::endl;
#                 endif //  FFMPEG_PRINT_CODECS
                }
            }
#         if OFX_FFMPEG_PRINT_CODECS
            else {
                std::cout << "Disallowed Format: " << fmt->name << " = " << fmt->long_name << std::endl;
            }
#         endif //  FFMPEG_PRINT_CODECS

        }
        fmt = av_oformat_next(fmt);
    }

#if OFX_FFMPEG_PRORES
    // Apple ProRes support.
    // short name must start with prores_ap
    // knoblabel must start with FourCC
#if OFX_FFMPEG_PRORES4444
    _codecsShortNames.push_back(kProresCodec kProresProfile4444FourCC);
    _codecsLongNames.push_back              (kProresProfile4444Name);
    _codecsKnobLabels.push_back             (kProresProfile4444FourCC"\t"kProresProfile4444Name);
#endif
    _codecsShortNames.push_back(kProresCodec kProresProfileHQFourCC);
    _codecsLongNames.push_back              (kProresProfileHQName);
    _codecsKnobLabels.push_back             (kProresProfileHQFourCC"\t"kProresProfileHQName);

    _codecsShortNames.push_back(kProresCodec kProresProfileSQFourCC);
    _codecsLongNames.push_back              (kProresProfileSQName);
    _codecsKnobLabels.push_back             (kProresProfileSQFourCC"\t"kProresProfileSQName);

    _codecsShortNames.push_back(kProresCodec kProresProfileLTFourCC);
    _codecsLongNames.push_back              (kProresProfileLTName);
    _codecsKnobLabels.push_back             (kProresProfileLTFourCC"\t"kProresProfileLTName);

    _codecsShortNames.push_back(kProresCodec kProresProfileProxyFourCC);
    _codecsLongNames.push_back              (kProresProfileProxyName);
    _codecsKnobLabels.push_back             (kProresProfileProxyFourCC"\t"kProresProfileProxyName);
#endif

    AVCodec* c = av_codec_next(NULL);
    while (c) {
        if (c->type == AVMEDIA_TYPE_VIDEO && c->encode2) {
            if (FFmpegFile::isCodecWhitelistedForWriting( c->name ) &&
                (c->long_name)) {
                const char* knobLabel = getCodecKnobLabel(c->name);
                if (knobLabel == NULL) {
#             if OFX_FFMPEG_PRINT_CODECS
                    std::cout << "Codec whitelisted but unknown: " << c->name << " = " << c->long_name << std::endl;
#             endif //  FFMPEG_PRINT_CODECS
                } else {
                    _codecsLongNames.push_back(c->long_name);
                    _codecsShortNames.push_back(c->name);
                    _codecsKnobLabels.push_back(knobLabel);
#             if OFX_FFMPEG_PRINT_CODECS
                    std::cout << "Codec: " << c->name << " = " << c->long_name << std::endl;
#             endif //  FFMPEG_PRINT_CODECS
                }
            }
#         if FFMPEG_PRINT_CODECS
            else {
                std::cout << "Disallowed Codec: " << c->name << " = " << c->long_name << std::endl;
            }
#         endif //  FFMPEG_PRINT_CODECS
        }
        c = av_codec_next(c);
    }
}

FFmpegSingleton::~FFmpegSingleton(){
    
}

using namespace OFX;

WriteFFmpegPlugin::WriteFFmpegPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
, _filename()
, _isOpen(false)
, _error(IGNORE_FINISH)
, _formatContext(0)
, _streamVideo(0)
, _streamAudio(0)
, _streamTimecode(0)
, _lastTimeEncoded(-1)
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
, _bitrate(0)
, _bitrateTolerance(0)
, _quality(0)
, _gopSize(0)
, _bFrames(0)
, _writeNCLC(0)
#if OFX_FFMPEG_MBDECISION
, _mbDecision(0)
#endif
{
    _rod.x1 = _rod.y1 = 0;
    _rod.x2 = _rod.y2 = -1;
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
    _bitrate = fetchIntParam(kParamBitrate);
    _bitrateTolerance = fetchIntParam(kParamBitrateTolerance);
    _quality = fetchInt2DParam(kParamQuality);
    _gopSize = fetchIntParam(kParamGopSize);
    _bFrames = fetchIntParam(kParamBFrames);
    _writeNCLC = fetchBooleanParam(kParamWriteNCLC);
#if OFX_FFMPEG_MBDECISION
    _mbDecision = fetchChoiceParam(kParamMBDecision);
#endif

    updateVisibility();
}

WriteFFmpegPlugin::~WriteFFmpegPlugin(){
    
}



bool WriteFFmpegPlugin::isImageFile(const std::string& ext) const
{
    return (ext == "bmp" ||
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
            ext == "rgb");
}

bool WriteFFmpegPlugin::isRec709Format(const int height) const
{
    // First check for codecs which require special handling:
    //  * JPEG codecs always use Rec 601.
    int codec;
    _codec->getValue(codec);
    const bool isJpeg = IsJpeg(_codec, codec);
    if (isJpeg) {
        return false;
    }

    // Using method described in step 5 of QuickTimeCodecReader::setPreferredMetadata
    return ((_rod.y2 - _rod.y1) >= 720);
}

// Figure out if an FFmpeg codec is definitely YUV based from its underlying storage
// pixel format type.
/*static*/
bool WriteFFmpegPlugin::IsYUV(AVPixelFormat pix_fmt)
{
    // from swscale_internal.h
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    return !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 2;
}

// Figure out if a codec is definitely YUV based from its shortname.
/*static*/
bool WriteFFmpegPlugin::IsYUVFromShortName(const char* shortName, int codecProfile)
{
    return (!strcmp(shortName, kProresCodec kProresProfileHQFourCC) ||
            !strcmp(shortName, kProresCodec kProresProfileSQFourCC) ||
            !strcmp(shortName, kProresCodec kProresProfileLTFourCC) ||
            !strcmp(shortName, kProresCodec kProresProfileProxyFourCC) ||
            //((codecProfile != (int)eDNxHDCodecProfile440x) && !strcmp(shortName, "dnxhd")) ||
            !strcmp(shortName, "mjpeg") ||
            !strcmp(shortName, "mpeg1video") ||
            !strcmp(shortName, "mpeg4") ||
            !strcmp(shortName, "v210"));
}

// Figure out if a codec is definitely RGB based from its shortname.
/*static*/
bool WriteFFmpegPlugin::IsRGBFromShortName(const char* shortName, int codecProfile)
{
    (void)codecProfile;
    return (!strcmp(shortName, kProresCodec kProresProfile4444FourCC) ||
            !strcmp(shortName, kProresCodec kProresProfile4444XQFourCC) ||
            //((codecProfile == (int)eDNxHDCodecProfile440x) && !strcmp(shortName, "dnxhd")) ||
            !strcmp(shortName, "png")  ||
            !strcmp(shortName, "qtrle"));
}

AVColorTransferCharacteristic WriteFFmpegPlugin::getColorTransferCharacteristic() const
{
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
# ifdef OFX_IO_USING_OCIO
    std::string selection;
    _ocio->getOutputColorspace(selection);
    if (selection.find("sRGB") != std::string::npos ||
        selection.find("srgb") != std::string::npos ||
        selection.find("rrt_srgb") != std::string::npos ||
        selection.find("srgb8") != std::string::npos ||
        selection.find("vd16") != std::string::npos ||
        selection.find("VD16") != std::string::npos) {
        return AVCOL_TRC_IEC61966_2_1;///< IEC 61966-2-1 (sRGB or sYCC)
    } else if (selection.find("Rec709") != std::string::npos ||
               selection.find("rec709") != std::string::npos ||
               selection.find("nuke_rec709") != std::string::npos ||
               selection.find("rrt_rec709_full_100nits") != std::string::npos ||
               selection.find("rrt_rec709") != std::string::npos ||
               selection.find("hd10") != std::string::npos) {
        return AVCOL_TRC_BT709;///< also ITU-R BT1361
#  if 0 // float values should be divided by 100 for this to work?
    } else if (selection.find("KodakLog") != std::string::npos ||
               selection.find("kodaklog") != std::string::npos ||
               selection.find("Cineon") != std::string::npos ||
               selection.find("cineon") != std::string::npos ||
               selection.find("adx10") != std::string::npos ||
               selection.find("lg10") != std::string::npos ||
               selection.find("lm10") != std::string::npos ||
               selection.find("lgf") != std::string::npos) {
        return AVCOL_TRC_LOG;///< "Logarithmic transfer characteristic (100:1 range)"
#  endif
    } else if (selection.find("Gamma2.2") != std::string::npos ||
               selection.find("rrt_Gamma2.2") != std::string::npos) {
        return AVCOL_TRC_GAMMA22;///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
    } else if (selection.find("linear") != std::string::npos ||
               selection.find("Linear") != std::string::npos) {
        return AVCOL_TRC_LINEAR;
    }
# endif

    return AVCOL_TRC_UNSPECIFIED;
}


AVOutputFormat* WriteFFmpegPlugin::initFormat(bool reportErrors) const
{
    int format;
    _format->getValue(format);
    AVOutputFormat* fmt = NULL;

    if (!format) {
        fmt = av_guess_format(NULL, _filename.c_str(), NULL);
        if (!fmt && reportErrors) {
            return NULL;
        }
    } else {
        const std::vector<std::string>& formatsShortNames = FFmpegSingleton::Instance().getFormatsShortNames();
        assert(format < (int)formatsShortNames.size());

        fmt = av_guess_format(formatsShortNames[format].c_str(), NULL, NULL);
        if (!fmt && reportErrors) {
            return NULL;
        }
    }
    
    return fmt;
}

bool WriteFFmpegPlugin::initCodec(AVOutputFormat* fmt, AVCodecID& outCodecId, AVCodec*& outVideoCodec) const
{
    outCodecId = fmt->video_codec;
    const std::vector<std::string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();

    int codec;
    _codec->getValue(codec);

    AVCodec* userCodec = avcodec_find_encoder_by_name(getCodecFromShortName(codecsShortNames[codec]));
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

void WriteFFmpegPlugin::getPixelFormats(AVCodec* videoCodec, AVPixelFormat& outNukeBufferPixelFormat, AVPixelFormat& outTargetPixelFormat, int& outBitDepth) const
{
    assert(videoCodec);

    const bool hasAlpha = alphaEnabled();
#if OFX_FFMPEG_PRORES
    if (AV_CODEC_ID_PRORES == videoCodec->id) {
        int index;
        _codec->getValue(index);
        const std::vector<std::string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
        assert(index < (int)codecsShortNames.size());
        int profile = getProfileFromShortName(codecsShortNames[index]);
        if (profile == kProresProfile4444 /*|| profile == kProresProfile4444XQ*/) {
            // Prores 4444
            // TODO: we could handle alpha too, using AV_PIX_FMT_YUVA444P10
            outTargetPixelFormat = AV_PIX_FMT_YUVA444P10;
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
        int dnxhdCodecProfile_i;
        _dnxhdCodecProfile->getValue(dnxhdCodecProfile_i);
        DNxHDCodecProfileEnum dnxhdCodecProfile = (DNxHDCodecProfileEnum)dnxhdCodecProfile_i;
        if (dnxhdCodecProfile == eDNxHDCodecProfile220x /*|| dnxhdCodecProfile == eDNxHDCodecProfile440x*/) {
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
        int currPixFormatBitDepth           = outBitDepth;
        while (*currPixFormat != -1) {
            currPixFormatBitDepth             = GetPixelFormatBitDepth(*currPixFormat);
            if (currPixFormatBitDepth  > outBitDepth)
                outBitDepth                     = currPixFormatBitDepth;
            currPixFormat++;
        }

        //figure out nukeBufferPixelFormat from this.
        outNukeBufferPixelFormat = GetPixelFormatFromBitDepth(outBitDepth, hasAlpha);

        //call best_pix_fmt using the full list.
        const int hasAlphaInt = hasAlpha ? 1 : 0;
        int loss     = 0; //Potentially we should error, or at least report if over a certain value?

        // gather the formats that have the highest bit depth (avcodec_find_best_pix_fmt_of_list doesn't do the best job: it prefers yuv422p over yuv422p10)
        std::vector<AVPixelFormat> bestFormats;
        currPixFormat  = videoCodec->pix_fmts;
        while (*currPixFormat != -1) {
            currPixFormatBitDepth             = GetPixelFormatBitDepth(*currPixFormat);
            if (currPixFormatBitDepth  == outBitDepth)
                bestFormats.push_back(*currPixFormat);
            currPixFormat++;
        }
        bestFormats.push_back(AV_PIX_FMT_NONE);

        outTargetPixelFormat = avcodec_find_best_pix_fmt_of_list(/*videoCodec->pix_fmts*/ &bestFormats[0], outNukeBufferPixelFormat, hasAlphaInt, &loss);

        if (AV_CODEC_ID_QTRLE == videoCodec->id) {
            if (hasAlphaInt &&
                (AV_PIX_FMT_ARGB != outTargetPixelFormat) &&
                (AV_PIX_FMT_RGBA != outTargetPixelFormat) &&
                (AV_PIX_FMT_ABGR != outTargetPixelFormat) &&
                (AV_PIX_FMT_BGRA != outTargetPixelFormat)) {
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
                    if ((AV_PIX_FMT_ARGB == avPixelFormat) ||
                        (AV_PIX_FMT_RGBA == avPixelFormat) ||
                        (AV_PIX_FMT_ABGR == avPixelFormat) ||
                        (AV_PIX_FMT_BGRA == avPixelFormat)) {
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
}

// av_get_bits_per_sample knows about surprisingly few codecs.
// We have to do this manually.
/*static*/
int WriteFFmpegPlugin::GetPixelFormatBitDepth(const AVPixelFormat pixelFormat)
{
    switch (pixelFormat) {
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

        case AV_PIX_FMT_YUV411P:     // Uncompressed 4:1:1 12bit
            return 12;
            break;

        case AV_PIX_FMT_YUV422P10LE: // Uncompressed 4:2:2 10bit - planar
        case AV_PIX_FMT_YUV422P10BE: // Uncompressed 4:2:2 10bit - planar
        case AV_PIX_FMT_YUV444P10LE: // Uncompressed 4:4:4 10bit - planar
        case AV_PIX_FMT_YUV444P10BE: // Uncompressed 4:4:4 10bit - planar
        case AV_PIX_FMT_YUVA444P10LE: // Uncompressed 4:4:4:4 10bit - planar
        case AV_PIX_FMT_YUVA444P10BE: // Uncompressed 4:4:4:4 10bit - planar
        case AV_PIX_FMT_YUVA444P:    // Uncompressed packed QT 4:4:4:4
            return 10;
            break;

        case AV_PIX_FMT_YUV420P: // MPEG-1, MPEG-2, MPEG-4 part2 (default)
        case AV_PIX_FMT_YUVJ420P: // MJPEG
        case AV_PIX_FMT_YUV422P: // DNxHD
        case AV_PIX_FMT_YUVJ422P: // MJPEG
        case AV_PIX_FMT_YUV444P: // Uncompressed 4:4:4 planar
        case AV_PIX_FMT_YUVJ444P: // Uncompressed 4:4:4 planar
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
    }
}

/*static*/
AVPixelFormat WriteFFmpegPlugin::GetPixelFormatFromBitDepth(const int bitDepth, const bool hasAlpha)
{
    AVPixelFormat pixelFormat;
    if (hasAlpha)
        pixelFormat = (bitDepth > 8) ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGB32;
    else
        pixelFormat = (bitDepth > 8) ? AV_PIX_FMT_RGB48 : AV_PIX_FMT_RGB24;
    return pixelFormat;
}

/*static*/
void WriteFFmpegPlugin::GetCodecSupportedParams(AVCodec* codec, bool& outLossyParams, bool& outInterGOPParams, bool& outInterBParams)
{
    assert(codec);
    //The flags on the codec can't be trusted to indicate capabilities, so use the props bitmask on the descriptor instead.
    const AVCodecDescriptor* codecDesc = avcodec_descriptor_get(codec->id);

    outLossyParams    = codecDesc ? !!(codecDesc->props & AV_CODEC_PROP_LOSSY) : false;
    outInterGOPParams = codecDesc ? !(codecDesc->props & AV_CODEC_PROP_INTRA_ONLY) : false;
    outInterBParams   = codecDesc ? !(codecDesc->props & AV_CODEC_PROP_INTRA_ONLY) : false;

    //Overrides for specific cases where the codecs don't follow the rules.
    //PNG doesn't observe the params, despite claiming to be lossy.
    if (codecDesc && (codecDesc->id == AV_CODEC_ID_PNG)) {
        outLossyParams = outInterGOPParams = outInterBParams = false;
    }
    //Mpeg4 ms var 3 / AV_CODEC_ID_MSMPEG4V3 doesn't have a descriptor, but needs the params.
    if (!codecDesc && (codec->id == AV_CODEC_ID_MSMPEG4V3)) {
        outLossyParams = outInterGOPParams = outInterBParams = true;
    }
    //QTRLE supports differing GOPs, but any b frame settings causes unreadable files.
    if (codecDesc && (codecDesc->id == AV_CODEC_ID_QTRLE)) {
        outLossyParams = outInterBParams = false;
        outInterGOPParams = true;
    }
#if OFX_FFMPEG_PRORES
    // Prores is profile-based, we don't need the bitrate parameters
    if (codecDesc && (codecDesc->id == AV_CODEC_ID_PRORES)) {
        outLossyParams = outInterBParams = outInterGOPParams = false;
    }
#endif
#if OFX_FFMPEG_DNXHD
    // DNxHD is profile-based, we don't need the bitrate parameters
    if (codecDesc && (codecDesc->id == AV_CODEC_ID_DNXHD)) {
        outLossyParams = outInterBParams = outInterGOPParams = false;
    }
#endif
    /* && codec->id != AV_CODEC_ID_PRORES*/
}


#if OFX_FFMPEG_AUDIO
////////////////////////////////////////////////////////////////////////////////
// configureAudioStream
// Set audio parameters of the audio stream.
//
// @param avCodec A pointer reference to an AVCodec to receive the AVCodec if
//                the codec can be located and is on the codec white list.
// @param avStream A reference to an AVStream of a audio stream.
//
void WriteFFmpegPlugin::configureAudioStream(AVCodec* avCodec, AVStream* avStream)
{
    AVCodecContext* avCodecContext = avStream->codec;
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
void WriteFFmpegPlugin::configureVideoStream(AVCodec* avCodec, AVStream* avStream)
{
    AVCodecContext* avCodecContext = avStream->codec;
    avcodec_get_context_defaults3(avCodecContext, avCodec);

    //Only update the relevant context variables where the user is able to set them.
    //This deals with cases where values are left on an old value when knob disabled.
    bool lossyParams    = false;
    bool interGOPParams = false;
    bool interBParams   = false;
    if (avCodec) GetCodecSupportedParams(avCodec, lossyParams, interGOPParams, interBParams);

    if (lossyParams) {
        int bitrate;
        _bitrate->getValue(bitrate);
        int bitrateTolerance;
        _bitrateTolerance->getValue(bitrateTolerance);
        int qMin, qMax;
        _quality->getValue(qMin, qMax);

        avCodecContext->bit_rate = bitrate;
        avCodecContext->bit_rate_tolerance = bitrateTolerance;
        avCodecContext->qmin = qMin;
        avCodecContext->qmax = qMax;
    }

    avCodecContext->width = (_rod.x2 - _rod.x1);
    avCodecContext->height = (_rod.y2 - _rod.y1);

    avCodecContext->color_trc = getColorTransferCharacteristic();

    av_dict_set(&_formatContext->metadata, kMetaKeyApplication, kPluginIdentifier, 0);

    av_dict_set(&_formatContext->metadata, kMetaKeyApplicationVersion, STR(kPluginVersionMajor)"."STR(kPluginVersionMinor), 0);

    //Currently not set - the main problem being that the mov32 reader will use it to set its defaults.
    //TODO: investigate using the writer key in mov32 to ignore this value when set to mov64.
    //av_dict_set(&_formatContext->metadata, kMetaKeyPixelFormat, "YCbCr  8-bit 422 (2vuy)", 0);

    const char* ycbcrmetavalue = isRec709Format(avCodecContext->height) ? "Rec 709" : "Rec 601";
    av_dict_set(&_formatContext->metadata, kMetaKeyYCbCrMatrix, ycbcrmetavalue, 0);

    //const char* lutName = GetLutName(lut());
    //if (lutName)
    //    av_dict_set(&_formatContext->metadata, kMetaKeyColorspace, lutName, 0);

    av_dict_set(&_formatContext->metadata, kMetaKeyWriter, kMetaValueWriter64, 0);

    int codec = 0;
    _codec->getValue(codec);
    const std::vector<std::string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
    int dnxhdCodecProfile = 0;
    _dnxhdCodecProfile->getValue(dnxhdCodecProfile);
    //Write the NCLC atom in the case the underlying storage is YUV.
    if(IsYUVFromShortName(codecsShortNames[codec].c_str(), dnxhdCodecProfile)) {
        bool writeNCLC = false;
        _writeNCLC->getValue(writeNCLC);

        // Primaries are always 709.
        avCodecContext->color_primaries = AVCOL_PRI_BT709;
        if (writeNCLC)
            av_dict_set(&avStream->metadata, kNCLCPrimariesKey, "1", 0);

        // Transfer function is always set to unknown. This results in more correct reading
        // on the part of QT Player.
        avCodecContext->color_trc = AVCOL_TRC_UNSPECIFIED;
        if (writeNCLC)
            av_dict_set(&avStream->metadata, kNCLCTransferKey, "2", 0);

        // Matrix is based on that used when writing (a combo of height and legacy codec in general).
        if (isRec709Format(avCodecContext->height)) {
            avCodecContext->colorspace = AVCOL_SPC_BT709;
            if (writeNCLC)
                av_dict_set(&avStream->metadata, kNCLCMatrixKey, "1", 0);
        } else {
            avCodecContext->colorspace = AVCOL_SPC_BT470BG;
            if (writeNCLC)
                av_dict_set(&avStream->metadata, kNCLCMatrixKey, "6", 0);
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

    double fps = 0.;
    _fps->getValue(fps);
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
    const float CONVERSION_FACTOR = 1000.0f;
    avCodecContext->time_base.num = (int)CONVERSION_FACTOR;
    avCodecContext->time_base.den = (int)(fps * CONVERSION_FACTOR);

    // Trap fractional frame rates so that they can be specified correctly
    // in a QuickTime movie. The rational number representation of fractional
    // frame rates is 24000/1001, 30000/1001, 60000/1001, etc.
    // WARNING: There are some big assumptions here. The assumption is
    //          that by specifying 23.98, 29.97 on the UI the intended
    //          frame rate is 24/1.001, 30/1.001, etc. so the frame rate
    //          is corrected here.
    int frameRate = (0.0 < fps) ? (int)fps : 0;
    if ((23 == frameRate) || (29 == frameRate) || (59 == frameRate)) {
        avCodecContext->time_base.num = 1001;
        avCodecContext->time_base.den = (frameRate + 1) * 1000;
    } else {
        avCodecContext->time_base.num = 100;
        avCodecContext->time_base.den = frameRate * 100;
    }
    // [mov @ 0x1042d7600] Using AVStream.codec.time_base as a timebase hint to the muxer is deprecated. Set AVStream.time_base instead.
    avStream->time_base.num = 100;
    avStream->time_base.den = frameRate * 100;

    int gopSize = 0;
    _gopSize->getValue(gopSize);
    if (interGOPParams)
        avCodecContext->gop_size = gopSize;

    int bFrames;
    _bFrames->getValue(bFrames);
    // NOTE: in new ffmpeg, bframes don't seem to work correctly - ffmpeg crashes...
    if (interBParams && bFrames) {
        avCodecContext->max_b_frames = bFrames;
        avCodecContext->b_frame_strategy = 0;
        avCodecContext->b_quant_factor = 2.0f;
    }

    FieldEnum fieldOrder = _inputClip->getFieldOrder();
    bool progressive = (fieldOrder == eFieldNone);
    // for the time being, we only support progressive encoding
    if (!progressive) {
        setPersistentMessage(OFX::Message::eMessageError, "", "only progressive video is supported");
        throwSuiteStatusException(kOfxStatFailed);
    }
    // Set this field so that an 'fiel' atom is inserted
    // into the QuickTime 'moov' atom.
    avCodecContext->field_order = AV_FIELD_PROGRESSIVE;

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
        if ((((1920 == srcWidth) /*|| (1440 == srcWidth)*/) && (1080 == srcHeight)) ||
            (((1280 == srcWidth) /*|| (960 == srcWidth)*/) && (720 == srcHeight))) {
            // No conversion necessary.
        } else {
            avCodecContext->width = 1920;
            avCodecContext->height = 1080;
        }
        // If alpha is to be encoded, then the following must
        // be set to 32. This will ensure the correct bit depth
        // information will be embedded in the QuickTime movie.
        // Otherwise the following must be set to 24.
        //const bool hasAlpha = alphaEnabled();
        //avCodecContext->bits_per_coded_sample = (hasAlpha) ? 32 : 24;
        int dnxhdCodecProfile_i;
        _dnxhdCodecProfile->getValue(dnxhdCodecProfile_i);
        DNxHDCodecProfileEnum dnxhdCodecProfile = (DNxHDCodecProfileEnum)dnxhdCodecProfile_i;
        int mbs = 0;
        switch (dnxhdCodecProfile) {
            case eDNxHDCodecProfile220x:
            case eDNxHDCodecProfile220:
                // 440x in 1080p/60 or 1080p/59.94, 365x in 1080p/50, 220x in 1080i/60 or 1080i/59.94, 185x in 1080i/50 or 1080p/25, 175x in 1080p/24 or 1080p/23.976, 220x in 1080p/29.97, 220x in 720p/59.94, 175x in 720p/50
                if (avCodecContext->width == 1920 && avCodecContext->height == 1080) {
                    switch (frameRate) {
                        case 60:
                        case 59:
                            mbs = progressive ? 440 : 220;
                            break;
                        case 50:
                            mbs = progressive ? 365 : 185;
                            break;
                        case 29:
                            mbs = progressive ? 220 : 0;
                        case 25:
                            mbs = progressive ? 185 : 0;
                            break;
                        case 24:
                        case 23:
                            mbs = progressive ? 175 : 0;
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (frameRate) {
                        case 60:
                        case 59:
                            mbs = progressive ? 220 : 0;
                            break;
                        case 50:
                            mbs = progressive ? 175 : 0;
                            break;
                        default:
                            break;
                    }
                }
                break;
            case eDNxHDCodecProfile145:
                // 290 in 1080p/60 or 1080p/59.94, 240 in 1080p/50, 145 in 1080i/60 or 1080i/59.94, 120 in 1080i/50 or 1080p/25, 115 in 1080p/24 or 1080p/23.976, 145 in 1080p/29.97, 145 in 720p/59.94, 115 in 720p/50
                if (avCodecContext->width == 1920 && avCodecContext->height == 1080) {
                    switch (frameRate) {
                        case 60:
                        case 59:
                            mbs = progressive ? 290 : 145;
                            break;
                        case 50:
                            mbs = progressive ? 240 : 120;
                            break;
                        case 29:
                            mbs = progressive ? 145 : 0;
                        case 25:
                            mbs = progressive ? 120 : 0;
                            break;
                        case 24:
                        case 23:
                            mbs = progressive ? 115 : 0;
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (frameRate) {
                        case 60:
                        case 59:
                            mbs = progressive ? 145 : 0;
                            break;
                        case 50:
                            mbs = progressive ? 115 : 0;
                            break;
                        default:
                            break;
                    }
                }
                break;
            case eDNxHDCodecProfile36:
                // 90 in 1080p/60 or 1080p/59.94, 75 in 1080p/50, 45 in 1080i/60 or 1080i/59.94, 36 in 1080i/50 or 1080p/25, 36 in 1080p/24 or 1080p/23.976, 45 in 1080p/29.97, 100 in 720p/59.94, 85 in 720p/50
                if (avCodecContext->width == 1920 && avCodecContext->height == 1080) {
                    switch (frameRate) {
                        case 60:
                        case 59:
                            mbs = progressive ? 90 : 45;
                            break;
                        case 50:
                            mbs = progressive ? 75 : 36;
                            break;
                        case 29:
                            mbs = progressive ? 45 : 0;
                        case 25:
                            mbs = progressive ? 36 : 0;
                            break;
                        case 24:
                        case 23:
                            mbs = progressive ? 36 : 0;
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (frameRate) {
                        case 60:
                        case 59:
                            mbs = progressive ? 100 : 0;
                            break;
                        case 50:
                            mbs = progressive ? 85 : 0;
                            break;
                        default:
                            break;
                    }
                }
                break;
        }
        if (mbs == 0) {
            setPersistentMessage(OFX::Message::eMessageError, "", "frameRate not supported for DNxHD");
            throwSuiteStatusException(kOfxStatFailed);
        }
        avCodecContext->bit_rate = mbs * 1000000;
    }

#if OFX_FFMPEG_MBDECISION
    int mbDecision;
    _mbDecision->getValue(mbDecision);
    avCodecContext->mb_decision = mbDecision;
#else
    avCodecContext->mb_decision = FF_MB_DECISION_SIMPLE;
#endif

# if OFX_FFMPEG_TIMECODE
    bool writeTimecode;
    _writeTimecode->getValue(writeTimecode);

    // Create a timecode stream for QuickTime movies. (There was no
    // requirement at the time of writing for any other file format.
    // Also not all containers support timecode.)
    if (writeTimecode && !strcmp(_formatContext->oformat->name, "mov")) {
        // Retrieve the timecode from Nuke/NukeStudio.
        // Adding a 'timecode' metadata item to the video stream
        // metadata will automagically create a timecode track
        // in the QuickTime movie created by FFmpeg.
        const MetaData::Bundle& metaData = iop->_fetchMetaData("");
        size_t size = metaData.size();
        MetaData::Bundle::PropertyPtr property = metaData.getData("input/timecode");
        if (property) {
            std::string timecode = MetaData::propertyToString(property).c_str();
            if (0 == timecode.size())
                timecode = "00:00:00:00"; // Set a sane default.
            av_dict_set(&avStream->metadata, "timecode", timecode.c_str(), 0);
        }
    }
# endif
}

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
AVStream* WriteFFmpegPlugin::addStream(AVFormatContext* avFormatContext, enum AVCodecID avCodecId, AVCodec** pavCodec)
{
    AVStream* avStream = NULL;

    AVCodec* avCodec = NULL;
    if (pavCodec) {
        // Find the encoder.
#if OFX_FFMPEG_PRORES
        if (avCodecId == AV_CODEC_ID_PRORES) {
            // use prores_ks instead of prores
            avCodec = avcodec_find_encoder_by_name(kProresCodec);
        } else
#endif
        avCodec = avcodec_find_encoder(avCodecId);
        if (!avCodec) {
            setPersistentMessage(OFX::Message::eMessageError, "", "could not find codec");
            return NULL;
        }
    }
    
    avStream = avformat_new_stream(avFormatContext, avCodec);
    if (!avStream) {
        setPersistentMessage(OFX::Message::eMessageError, "", "could not allocate stream");
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
int WriteFFmpegPlugin::openCodec(AVFormatContext* avFormatContext, AVCodec* avCodec, AVStream* avStream)
{
    AVCodecContext* avCodecContext = avStream->codec;
    if (AVMEDIA_TYPE_AUDIO == avCodecContext->codec_type) {
        // Audio codecs.
        if (avcodec_open2(avCodecContext, avCodec, NULL) < 0) {
            setPersistentMessage(OFX::Message::eMessageError, "", "could not open audio codec");
            return -1;
        }
    } else if (AVMEDIA_TYPE_VIDEO == avCodecContext->codec_type) {
        if (avcodec_open2(avCodecContext, avCodec, NULL) < 0) {
            setPersistentMessage(OFX::Message::eMessageError, "", "unable to open video codec");
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
int WriteFFmpegPlugin::writeAudio(AVFormatContext* avFormatContext, AVStream* avStream, bool flush)
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
                if (avFrame->nb_samples > nbSamples)
                    avFrame->nb_samples = nbSamples;
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
            char szError[1024];
            av_strerror(ret, szError, 1024);
            iop->error(szError);
        }
    }

    return ret;
}
#endif

// the following was taken from libswscale/utils.c:
static int handle_jpeg(enum AVPixelFormat *format)
{
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
int WriteFFmpegPlugin::colourSpaceConvert(AVPicture* avPicture, AVFrame* avFrame, AVPixelFormat srcPixelFormat, AVPixelFormat dstPixelFormat, AVCodecContext* avCodecContext)
{
    int ret = 0;

    int width = (_rod.x2 - _rod.x1);
    int height = (_rod.y2 - _rod.y1);

    int dstRange = IsYUV(dstPixelFormat) ? 0 : 1; // 0 = 16..235, 1 = 0..255
    dstRange |= handle_jpeg(&dstPixelFormat); // may modify dstPixelFormat
    if (AV_CODEC_ID_DNXHD == avCodecContext->codec_id) {
        int encodeVideoRange;
        _encodeVideoRange->getValue(encodeVideoRange);
        dstRange = !(encodeVideoRange);
    }

    SwsContext* convertCtx = sws_getCachedContext(NULL,
                                                  width, height, srcPixelFormat, // from
                                                  avCodecContext->width, avCodecContext->height, dstPixelFormat,// to
                                                  SWS_BICUBIC, NULL, NULL, NULL);

    // Set up the sws (SoftWareScaler) to convert colourspaces correctly, in the sws_scale function below
    const int colorspace = (width < 1000) ? SWS_CS_ITU601 : SWS_CS_ITU709;

    // Only apply colorspace conversions for YUV.
    if (IsYUV(dstPixelFormat)) {
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

bool WriteFFmpegPlugin::alphaEnabled() const
{
    // is the writer configured to write alpha channel to file ?
    return false;
}

int WriteFFmpegPlugin::numberOfDestChannels() const
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
int WriteFFmpegPlugin::writeVideo(AVFormatContext* avFormatContext, AVStream* avStream, bool flush, const float *pixelData, const OfxRectI* bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    // FIXME enum needed for error codes.
    if (!_isOpen) {
        return -5; //writer is not open!
    }
    if (!avStream) {
        return -6;
    }
    int ret = 0;
    // First convert from Nuke floating point RGB to either 16-bit or 8-bit RGB.
    // Create a buffer to hold either  16-bit or 8-bit RGB.
    AVCodecContext* avCodecContext = avStream->codec;
    // Create another buffer to convert from either 16-bit or 8-bit RGB
    // to the input pixel format required by the encoder.
    AVPixelFormat pixelFormatCodec = avCodecContext->pix_fmt;
    int width = _rod.x2-_rod.x1;
    int height = _rod.y2-_rod.y1;
    int picSize = avpicture_get_size(pixelFormatCodec, width, height);

    AVPicture avPicture = {{0}};
    AVFrame* avFrame = NULL;

    if (!flush) {

        assert(pixelData && bounds);
        const bool hasAlpha = alphaEnabled();

        AVPixelFormat pixelFormatNuke;
        if (hasAlpha)
            pixelFormatNuke = (avCodecContext->bits_per_raw_sample > 8) ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGBA;
        else
            pixelFormatNuke = (avCodecContext->bits_per_raw_sample > 8) ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;

        ret = avpicture_alloc(&avPicture, pixelFormatNuke, width, height);
        if (!ret) {

            // Convert floating point values to unsigned values.
            int numChannels = 0;
            switch(pixelComponents) {
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
                    assert(false);
                    OFX::throwSuiteStatusException(kOfxStatErrFormat);
                    return -1;
            }
            assert(numChannels);
            assert(rowBytes);
            const int numDestChannels = hasAlpha ? 4 : 3;

            for (int y = 0; y < height; ++y) {
                int srcY = height - 1 - y;
                const float* src_pixels = (float*)((char*)pixelData + srcY * rowBytes);

                if (avCodecContext->bits_per_raw_sample > 8) {
                    assert(pixelFormatNuke == AV_PIX_FMT_RGBA64 || pixelFormatNuke == AV_PIX_FMT_RGB48);

                    // avPicture.linesize is in bytes, but stride is U16 (2 bytes), so divide linesize by 2
                    unsigned short* dst_pixels = reinterpret_cast<unsigned short*>(avPicture.data[0]) + y * (avPicture.linesize[0] / 2);

                    for (int x = 0; x < width; ++x) {
                        int srcCol = x * numChannels;
                        int dstCol = x * numDestChannels;
                        dst_pixels[dstCol + 0] = floatToInt<65536>(src_pixels[srcCol + 0]);
                        dst_pixels[dstCol + 1] = floatToInt<65536>(src_pixels[srcCol + 1]);
                        dst_pixels[dstCol + 2] = floatToInt<65536>(src_pixels[srcCol + 2]);
                        if (hasAlpha) {
                            dst_pixels[dstCol + 3] = floatToInt<65536>((numDestChannels == 4) ? src_pixels[srcCol + 3] : 1.);
                        }
                    }
               } else {
                   assert(pixelFormatNuke == AV_PIX_FMT_RGB32 || pixelFormatNuke == AV_PIX_FMT_RGB24);

                    unsigned char* dst_pixels = avPicture.data[0] + y * avPicture.linesize[0];

                    for (int x = 0; x < width; ++x) {
                        int srcCol = x * numChannels;
                        int dstCol = x * 3;
                        dst_pixels[dstCol + 0] = floatToInt<256>(src_pixels[srcCol + 0]);
                        dst_pixels[dstCol + 1] = floatToInt<256>(src_pixels[srcCol + 1]);
                        dst_pixels[dstCol + 2] = floatToInt<256>(src_pixels[srcCol + 2]);
                        if (hasAlpha) {
                            dst_pixels[dstCol + 3] = floatToInt<256>((numDestChannels == 4) ? src_pixels[srcCol + 3] : 1.);
                        }
                   }
                }
            }

            avFrame = av_frame_alloc(); // Create an AVFrame structure and initialise to zero.
            {
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
        if ((avFormatContext->oformat->flags & AVFMT_RAWPICTURE) != 0) {
            AVPacket pkt;
            av_init_packet(&pkt);
            pkt.flags |= AV_PKT_FLAG_KEY;
            pkt.stream_index = avStream->index;
            pkt.data = avFrame->data[0];
            pkt.size = sizeof(AVPicture);
            const int writeResult = av_write_frame(avFormatContext, &pkt);
            const bool writeSucceeded = (writeResult == 0);
            if (!writeSucceeded) {
                error = true;
            }
        } else {
            // Allocate a contiguous block of memory. This is to scope the
            // buffer allocation and ensure that memory is released even
            // if errors or exceptions occur. A std::vector will allocate
            // contiguous memory.
            std::vector<uint8_t> outbuf(picSize);
            
            AVPacket pkt;
            av_init_packet(&pkt);
            // NOTE: If |flush| is true, then avFrame will be NULL at this point as
            //       alloc will not have been called.
            const int bytesEncoded = encodeVideo(avCodecContext, outbuf.data(), picSize, avFrame);
            const bool encodeSucceeded = (bytesEncoded > 0);
            if (encodeSucceeded) {
                if (avCodecContext->coded_frame && (avCodecContext->coded_frame->pts != AV_NOPTS_VALUE))
                    pkt.pts = av_rescale_q(avCodecContext->coded_frame->pts, avCodecContext->time_base, avStream->time_base);
                if (avCodecContext->coded_frame && avCodecContext->coded_frame->key_frame)
                    pkt.flags |= AV_PKT_FLAG_KEY;
                
                pkt.stream_index = avStream->index;
                pkt.data = &outbuf[0];
                pkt.size = bytesEncoded;
                
                const int writeResult = av_write_frame(avFormatContext, &pkt);
                const bool writeSucceeded = (writeResult == 0);
                if (!writeSucceeded) {
                    // Report the error.
                    char szError[1024];
                    av_strerror(bytesEncoded, szError, 1024);
                    setPersistentMessage(OFX::Message::eMessageError, "", szError);
                    error = true;
                }
            } else {
                if (bytesEncoded < 0) {
                    // Report the error.
                    char szError[1024];
                    av_strerror(bytesEncoded, szError, 1024);
                    setPersistentMessage(OFX::Message::eMessageError, "", szError);
                    error = true;
                } else if (flush) {
                    // Flag that the flush is complete.
                    ret = -10;
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
        if (avFrame->data[0] != avPicture.data[0])
            av_freep(avFrame->data);
        av_frame_free(&avFrame);
    }

    if (avPicture.data[0])
        avpicture_free(&avPicture);

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// encodeVideo
// Encode a frame of video.
//
// Note that the uncompressed source frame to be encoded must be in an
// appropriate pixel format for the encoder prior to calling this method as
// this method does NOT perform an pixel format conversion, e.g. through using
// Sws_xxx.
//
// @param avCodecContext A reference to an AVCodecContext of a video stream.
// @param out A reference to a buffer to receive the encoded frame.
// @param outSize The size in bytes of |out|.
// @param avFrame A constant reference to an AVFrame that contains the source data
//                to be encoded. This must be in an appropriate pixel format for
//                the encoder.
//
// @return <0 for any failure to encode the frame, otherwise the size in byte
//         of the encoded frame.
//
int WriteFFmpegPlugin::encodeVideo(AVCodecContext* avCodecContext, uint8_t* out, int outSize, const AVFrame* avFrame)
{
    int ret, got_packet = 0;

    if (outSize < FF_MIN_BUFFER_SIZE) {
        av_log(avCodecContext, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }

    AVPacket pkt;
    av_init_packet(&pkt);

    pkt.data = out;
    pkt.size = outSize;

    {
        ret = avcodec_encode_video2(avCodecContext, &pkt, avFrame, &got_packet);
        if (!ret && got_packet && avCodecContext->coded_frame) {
            avCodecContext->coded_frame->pts = pkt.pts;
            avCodecContext->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
        }
    }
    
    return ret ? ret : pkt.size;
}

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
int WriteFFmpegPlugin::writeToFile(AVFormatContext* avFormatContext, bool finalise, const float *pixelData, const OfxRectI* bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
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
        double targetTime = ((int)(videoTime / 0.5)) * 0.5;
        if (audioTime < targetTime) {
            // If audio stream is more than 0.5s behind, write
            // another ~0.5s of audio.
            double sourceDuration = audioReader_->getDuration();
            while ((audioTime < targetTime) && (audioTime < sourceDuration)) {
                writeAudio(avFormatContext, streamAudio_, finalise);
                audioTime = streamAudio_->pts.val * av_q2d(streamAudio_->time_base);
            }
        }
    }
#endif
    return writeVideo(avFormatContext, _streamVideo, finalise, pixelData, bounds, pixelComponents, rowBytes);
}

////////////////////////////////////////////////////////////////////////////////
// open
// Internal function to create all the required streams for writing a QuickTime
// movie.
//
// @return true if successful,
//         false otherwise.
//
void WriteFFmpegPlugin::beginEncode(const std::string& filename,
                                    const OfxRectI& rod,
                                    const OFX::BeginSequenceRenderArguments& args)
{
    if (!args.sequentialRenderStatus || _formatContext || _streamVideo) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Another render is currently active");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    if (args.isInteractive) {
        setPersistentMessage(OFX::Message::eMessageError, "", "can only write files when in non-interactive mode.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    assert(!_formatContext);
    
        ////////////////////                        ////////////////////
        //////////////////// INTIALIZE FORMAT       ////////////////////
    
    _filename = filename;
    _rod = rod;

    AVOutputFormat* avOutputFormat = initFormat(/* reportErrors = */ true);
    if (!avOutputFormat) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Invalid file extension");
        throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (!_formatContext) {
#     if defined(FFMS_USE_FFMPEG_COMPAT) && LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(54, 0, 0)
        avformat_alloc_output_context2(&_formatContext, avOutputFormat, NULL, filename.c_str());
#     else
#       ifdef FFMS_USE_FFMPEG_COMPAT
        _formatContext = avformat_alloc_output_context(NULL, avOutputFormat, filename.c_str());
#       else
        _formatContext = avformat_alloc_context();
        _formatContext->oformat = fmt;
#       endif
#     endif
    }

    snprintf(_formatContext->filename, sizeof(_formatContext->filename), "%s", filename.c_str());

    /////////////////////                            ////////////////////
    ////////////////////    INITIALISE STREAM     ////////////////////

#if OFX_FFMPEG_AUDIO
    // Create an audio stream if a file has been provided.
    if (_audioFile && (strlen(_audioFile) > 0)) {
        if (!streamAudio_) {
            // Attempt to create an audio reader.
            audioReader_.reset(new AudioReader());

            // TODO: If the sample format is configurable via a knob, set
            //       the desired format here, e.g.:
            //audioReader_->setSampleFormat(_avSampleFormat);

            if (!audioReader_->open(_audioFile)) {
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
                if (!strcmp(formatContext_->oformat->name, "mp4") ||
                    !strcmp(formatContext_->oformat->name, "mov") ||
                    !strcmp(formatContext_->oformat->name, "3gp")) {
                    avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
                }
                // Some formats want stream headers to be separate.
                if (formatContext_->oformat->flags & AVFMT_GLOBALHEADER)
                    avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;

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
                audioReader_->setStartPosition(-((_audioOffsetUnit == 0) ? _audioOffset : (_audioOffset / fps_)));
                
            } else {
                setPersistentMessage(OFX::Message::eMessageError, "", "failed to open the audio file\nIt does not contain audio or is an invalid file");
                throwSuiteStatusException(kOfxStatFailed);
                return false;
            }
        }
    }
#endif

    // Create a video stream.
    AVCodecID codecId = AV_CODEC_ID_NONE;
    AVCodec* videoCodec = NULL;
    if (!initCodec(avOutputFormat, codecId, videoCodec)) {
        setPersistentMessage(OFX::Message::eMessageError, "","Unable to find codec");
        freeFormat();
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // Test if the container recognises the codec type.
    bool isCodecSupportedInContainer = (avformat_query_codec(avOutputFormat, codecId, FF_COMPLIANCE_NORMAL) == 1);
    // mov seems to be able to cope with anything, which the above function doesn't seem to think is the case (even with FF_COMPLIANCE_EXPERIMENTAL)
    // and it doesn't return -1 for in this case, so we'll special-case this situation to allow this
    isCodecSupportedInContainer |= (strcmp(_formatContext->oformat->name, "mov") == 0);
    if (!isCodecSupportedInContainer) {
        setPersistentMessage(OFX::Message::eMessageError, "","the selected codec is not supported in this container.");
        freeFormat();
        OFX::throwSuiteStatusException(kOfxStatFailed);
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
        avCodecContext->bits_per_raw_sample = outBitDepth;

        // Now that the stream has been created, and the pixel format
        // is known, for DNxHD, set the YUV range.
        if (AV_CODEC_ID_DNXHD == avCodecContext->codec_id) {
            int encodeVideoRange;
            _encodeVideoRange->getValue(encodeVideoRange);
            // Set the metadata for the YUV range. This modifies the appropriate
            // field in the 'ACLR' atom in the video sample description.
            // Set 'full range' = 1 or 'legal range' = 2.
            av_dict_set(&_streamVideo->metadata, kACLRYuvRange, encodeVideoRange ? "2" :"1", 0);
        }

        // Bug 45010 The following flags must be set BEFORE calling
        // openCodec (avcodec_open2). This will ensure that codec
        // specific data is created and initialized. (E.g. for MPEG4
        // the AVCodecContext::extradata will contain Elementary Stream
        // Descriptor which is required for QuickTime to decode the
        // stream.)
        if (!strcmp(_formatContext->oformat->name, "mp4") ||
            !strcmp(_formatContext->oformat->name, "mov") ||
            !strcmp(_formatContext->oformat->name, "3gp")) {
            avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }
        // Some formats want stream headers to be separate.
        if (_formatContext->oformat->flags & AVFMT_GLOBALHEADER)
            avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
#if OFX_FFMPEG_PRORES
        if (codecId == AV_CODEC_ID_PRORES) {
            int index;
            _codec->getValue(index);
            const std::vector<std::string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
            assert(index < (int)codecsShortNames.size());
            avCodecContext->profile = getProfileFromShortName(codecsShortNames[index]);
            av_opt_set(avCodecContext->priv_data, "bits_per_mb", "8000", 0);
            av_opt_set(avCodecContext->priv_data, "vendor", "ap10", 0);
        }
#endif

# if OFX_FFMPEG_PRINT_CODECS
        std::cout << "Format: " << _formatContext->oformat->name << " Codec: " << videoCodec->name << " nukeBufferPixelFormat: " << av_get_pix_fmt_name(nukeBufferPixelFormat) << " targetPixelFormat: " << av_get_pix_fmt_name(targetPixelFormat) << " outBitDepth: " << outBitDepth << " Profile: " << _streamVideo->codec->profile << std::endl;
# endif //  FFMPEG_PRINT_CODECS
        if (openCodec(_formatContext, videoCodec, _streamVideo) < 0) {
            freeFormat();
            throwSuiteStatusException(kOfxStatFailed);
            return;
        }

        if (!(avOutputFormat->flags & AVFMT_NOFILE)) {
            if (avio_open(&_formatContext->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
                setPersistentMessage(OFX::Message::eMessageError, "","unable to open file");
                freeFormat();
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
        }
        
        avformat_write_header(_formatContext, NULL);
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
    if (tag)
        av_dict_set(&_formatContext->metadata, "encoder", "", 0); // Set the 'encoder' key to null.

    ///Flag that we didn't encode any frame yet
    _lastTimeEncoded = -1;

    _isOpen = true;
}



#define checkAvError() if (error < 0) { \
                        char errorBuf[1024]; \
                        av_strerror(error, errorBuf, sizeof(errorBuf)); \
                        setPersistentMessage(OFX::Message::eMessageError, "", errorBuf); \
                        OFX::throwSuiteStatusException(kOfxStatFailed); return; \
                    }


void WriteFFmpegPlugin::encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB) {
        setPersistentMessage(OFX::Message::eMessageError, "", "can only write RGBA or RGB components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    
    if (!_isOpen) {
        setPersistentMessage(OFX::Message::eMessageError, "", "file is not open");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (!_formatContext || (_formatContext && filename != std::string(_formatContext->filename))) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Another render is currently active");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    ///Check that we're really encoding in sequential order
    if (_lastTimeEncoded != -1 && _lastTimeEncoded != time -1 && _lastTimeEncoded != time + 1) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Another render is currently active");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;

    }

    _error = IGNORE_FINISH;


    if (_isOpen) {
        _error = CLEANUP;

        if (!writeToFile(_formatContext, false, pixelData, &bounds, pixelComponents, rowBytes)) {
            _error = SUCCESS;
            _lastTimeEncoded = (int)time;
        } else {
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }
}

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
void WriteFFmpegPlugin::endEncode(const OFX::EndSequenceRenderArguments &/*args*/)
{
    if (!_formatContext) {
        return;
    }


    if (_error == IGNORE_FINISH)
        return;

    bool flushFrames = true;
    while (flushFrames) {
        // Continue to write the audio/video interleave while there are still
        // frames in the video and/or audio encoder queues, without queuing any
        // new data to encode. This is ffmpeg specific.
        flushFrames = !writeToFile(_formatContext, true) ? true : false;
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
            while ((audioTime < videoTime) && (audioTime < sourceDuration) && !ret) {
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

void WriteFFmpegPlugin::updateVisibility()
{
    //The advanced params are enabled/disabled based on the codec chosen and its capabilities.
    //We also investigated setting the defaults based on the codec defaults, however all current
    //codecs defaulted the same, and as a user experience it was pretty counter intuitive.
    //Check knob exists, to deal with cases where Nuke might not have updated underlying writer (#44774)
    //(we still want to use showPanel to update when loading from script and the like).
    int index;
    _codec->getValue(index);
    const std::vector<std::string>& codecsShortNames = FFmpegSingleton::Instance().getCodecsShortNames();
    assert(index < (int)codecsShortNames.size());

    AVCodec* codec = avcodec_find_encoder_by_name(getCodecFromShortName(codecsShortNames[index]));

    bool lossyParams    = false;
    bool interGOPParams = false;
    bool interBParams   = false;

    if (codec) {
        GetCodecSupportedParams(codec, lossyParams, interGOPParams, interBParams);
    }

    _bitrate->setEnabled(lossyParams);
    _bitrate->setIsSecret(!lossyParams);
    _bitrateTolerance->setEnabled(lossyParams);
    _bitrateTolerance->setIsSecret(!lossyParams);
    _quality->setEnabled(lossyParams);
    _quality->setIsSecret(!lossyParams);

    _gopSize->setEnabled(interGOPParams);
    _gopSize->setIsSecret(!interGOPParams);
    _bFrames->setEnabled(interBParams);
    _bFrames->setIsSecret(!interBParams);

    //We use the bitrate to set the min range for bitrate tolerance.
    updateBitrateToleranceRange();

#if OFX_FFMPEG_DNXHD
    // Only enable the DNxHD codec profile knob if the Avid
    // DNxHD codec has been selected.
    // Only enable the video range knob if the Avid DNxHD codec
    // has been selected.
    bool isdnxhd = (!strcmp(codecsShortNames[index].c_str(), "dnxhd"));
    _dnxhdCodecProfile->setEnabled(isdnxhd);
    _dnxhdCodecProfile->setIsSecret(!isdnxhd);
    _encodeVideoRange->setEnabled(isdnxhd);
    _encodeVideoRange->setIsSecret(!isdnxhd);
#endif
}


////////////////////////////////////////////////////////////////////////////////
// updateBitrateToleranceRange
// Utility to update tolerance knob's slider range.
// Employed in place of chained knob_changed calls.
// Only valid for use from knob_changed.
//
void WriteFFmpegPlugin::updateBitrateToleranceRange()
{
    //Bitrate tolerance should in theory be allowed down to target bitrate/target framerate.
    //We're not force limiting the range since the upper range is not bounded.
    int bitrate = 0;
    _bitrate->getValue(bitrate);
    double fps = 24.;
    _fps->getValue(fps);
    double minRange = bitrate / fps;
    _bitrateTolerance->setRange(minRange, 4000 * 10000);
}

void WriteFFmpegPlugin::onOutputFileChanged(const std::string &filename)
{
    // Switch the 'format' knob based on the new filename suffix
    std::string suffix = filename.substr(filename.find_last_of(".") + 1);
    if (!suffix.empty()) {
        // Compare found suffix to known formats
        const std::vector<std::string>& formatsShortNames = FFmpegSingleton::Instance().getFormatsShortNames();
        for (size_t i = 0; i < formatsShortNames.size(); ++i) {
            if (suffix.compare(formatsShortNames[i]) == 0) {
                _format->setValue(i);
                break;
            }
        }
    }
}

void WriteFFmpegPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamCodec && args.reason == eChangeUserEdit) {
        updateVisibility();
        return;
    }
    if (paramName == kParamFPS || paramName == kParamBitrate) {
        updateBitrateToleranceRange();
        return;
    }
    if (paramName == kParamResetFPS && args.reason == eChangeUserEdit) {
        double fps = _inputClip->getFrameRate();
        _fps->setValue(fps);
        updateBitrateToleranceRange();
        return;
    }
    if (paramName == kParamQuality && args.reason == eChangeUserEdit) {
        int qMin, qMax;
        _quality->getValue(qMin, qMax);
        if (qMax < qMin) {
            // reorder
            _quality->setValue(qMax, qMin);
        }
        return;
    }
    GenericWriterPlugin::changedParam(args, paramName);
}

void WriteFFmpegPlugin::freeFormat()
{
    if (_streamVideo) {
        avcodec_close(_streamVideo->codec);
        _streamVideo = NULL;
    }
    if (_streamAudio) {
        avcodec_close(_streamAudio->codec);
        _streamAudio = NULL;
    }
    if (!(_formatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_close(_formatContext->pb);
    }
    avformat_free_context(_formatContext);
    _formatContext = NULL;
    _streamVideo = NULL;
    _lastTimeEncoded = -1;
    _isOpen = false;
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
    desc.setLabel(kPluginName);
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
        //throwSuiteStatusException(kOfxStatErrMissingHostFeature);
    }

    
    ///////////Output format
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
        const std::vector<std::string>& formatsV = FFmpegSingleton::Instance().getFormatsLongNames();
        param->setLabel(kParamFormatLabel);
        param->setHint(kParamFormatHint);
        for (unsigned int i = 0; i < formatsV.size(); ++i) {
            param->appendOption(formatsV[i],"");

        }
        param->setAnimates(false);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }

    ///////////Codec
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamCodec);
        param->setLabel(kParamCodecName);
        param->setHint(kParamCodecHint);
        const std::vector<std::string>& codecsV = FFmpegSingleton::Instance().getCodecsKnobLabels();// getCodecsLongNames();
        for (unsigned int i = 0; i < codecsV.size(); ++i) {
            param->appendOption(codecsV[i]);
        }
        param->setAnimates(false);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }

    ///////////FPS
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFPS);
        param->setLabel(kParamFPSLabel);
        param->setHint(kParamFPSHint);
        param->setRange(0.f, 100.f);
        param->setDefault(24.f); // should be set from the input FPS
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetFPS);
        param->setLabel(kParamResetFPSLabel);
        param->setHint(kParamResetFPSHint);
        if (page) {
            page->addChild(*param);
        }
    }

#if OFX_FFMPEG_DNXHD
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDNxHDCodecProfile);
        param->setLabel(kParamDNxHDCodecProfileLabel);
        param->setHint(kParamDNxHDCodecProfileHint);
        //assert(param->getNOptions() == (int)eDNxHDCodecProfile440x);
        //param->appendOption(kParamDNxHDCodecProfileOption440x, kParamDNxHDCodecProfileOption440xHint);
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamWriteTimeCode);
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
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kParamAdvanced);
        group->setLabel(kParamAdvancedLabel);
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }

#ifdef OFX_FFMPEG_DNXHD
        {
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDNxHDEncodeVideoRange);
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
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBitrate);
            param->setLabel(kParamBitrateLabel);
            param->setHint(kParamBitrateHint);
            param->setRange(0, 400000);
            param->setDefault(400000);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ///////////bit-rate tolerance
        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBitrateTolerance);
            param->setLabel(kParamBitrateToleranceLabel);
            param->setHint(kParamBitrateToleranceHint);
            param->setRange(833, 4000 * 10000);
            param->setDefault(4000 * 10000);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ////////////Quality
        {
            OFX::Int2DParamDescriptor* param = desc.defineInt2DParam(kParamQuality);
            param->setLabel(kParamQualityLabel);
            param->setHint(kParamQualityHint);
            param->setRange(0,0,100,100);
            param->setDefault(2,31);
            param->setDimensionLabels("min", "max");
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        ///////////Gop size
        {
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamGopSize);
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
            OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBFrames);
            param->setLabel(kParamBFramesLabel);
            param->setHint(kParamBFramesHint);
            param->setRange(0, FF_MAX_B_FRAMES);
            param->setDefault(0);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
        ///////////Write NCLC
        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamWriteNCLC);
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
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMBDecision);
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

