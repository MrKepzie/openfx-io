/*
 OFX ffmpegReader plugin.
 Reads a video input file using the libav library.
 
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

#include "ReadFFmpeg.h"

#include <cmath>
#include <sstream>

#include "FFmpegHandler.h"
#include "Lut.h"

ReadFFmpegPlugin::ReadFFmpegPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, "rec709", "reference")
, _ffmpegFile(0)
, _buffer(0)
, _bufferWidth(0)
, _bufferHeight(0)
{
    ///initialize the manager if it isn't
    FFmpeg::FileManager::s_readerManager.initialize();
}

ReadFFmpegPlugin::~ReadFFmpegPlugin() {
    
    if(_buffer){
        delete [] _buffer;
    }
}

bool ReadFFmpegPlugin::loadNearestFrame() const {
    int v;
    _missingFrameParam->getValue(v);
    return v == 0;
}

FFmpeg::File* ReadFFmpegPlugin::getFile(const std::string& filename) const {
    if(_ffmpegFile && _ffmpegFile->filename() == filename){
        return _ffmpegFile;
    }
    return FFmpeg::FileManager::s_readerManager.get(filename);
}


void ReadFFmpegPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericReaderPlugin::changedParam(args, paramName);
}

void ReadFFmpegPlugin::onInputFileChanged(const std::string& filename) {
    _ffmpegFile = getFile(filename);
    if(_ffmpegFile->invalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        return;
    }
}

bool ReadFFmpegPlugin::isVideoStream(const std::string& filename){
    return !FFmpeg::isImageFile(filename);
}

void ReadFFmpegPlugin::decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, OFX::Image* dstImg)
{
    
    _ffmpegFile = getFile(filename);
    
    if(_ffmpegFile->invalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        return;
    }
    
    int width,height,frames;
    double ap;
    
    _ffmpegFile->info(width, height, ap, frames);
    
    OfxRectI imgBounds = dstImg->getBounds();
    
    if((imgBounds.x2 - imgBounds.x1) < width ||
       (imgBounds.y2 - imgBounds.y1) < height){
        setPersistentMessage(OFX::Message::eMessageError, "", "The host provided an image of wrong size, can't decode.");
    }
    
    ///set the pixel aspect ratio
    dstImg->getPropertySet().propSetDouble(kOfxImagePropPixelAspectRatio, ap, 0);
    
    if(_bufferWidth != width || _bufferHeight != height){
        delete [] _buffer;
        _buffer = 0;
    }
    
    if(!_buffer){
        _buffer = new unsigned char[width * height * 3];
        _bufferHeight = height;
        _bufferWidth = width;
    }
    
    //< round the time to an int to get a frame number ? Not sure about this
    try{
        if (!_ffmpegFile->decode(_buffer, std::floor(time+0.5),loadNearestFrame())) {
            setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        }
    }catch(const std::exception& e){
        int choice;
        _missingFrameParam->getValue(choice);
        if(choice == 1){ //error
            setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        }
        return;
    }
    
    ///we (aka the GenericReader) only support float bit depth
    /// and RGBA output clip
    OFX::BitDepthEnum e = dstImg->getPixelDepth();
    if(e != OFX::eBitDepthFloat){
        return;
    }
    
    ///fill the dstImg with the buffer freshly decoded.
    for (int y = imgBounds.y1; y < imgBounds.y2; ++y) {
        int srcY = imgBounds.y2 - y - 1;
        float* dst_pixels = (float*)dstImg->getPixelAddress(0, y);
        const unsigned char* src_pixels = _buffer + (imgBounds.x2 - imgBounds.x1) * srcY * 3;
        
        for (int x = imgBounds.x1; x < imgBounds.x2; ++x) {
            int srcCol = x * 3;
            int dstCol = x * 4;
            dst_pixels[dstCol] = (float)src_pixels[srcCol] / 255.f;
            dst_pixels[dstCol + 1] = (float)src_pixels[srcCol + 1] / 255.f;
            dst_pixels[dstCol + 2] = (float)src_pixels[srcCol + 2] / 255.f;
            dst_pixels[dstCol + 3] = 1.f;
        }
    }
}

bool ReadFFmpegPlugin::getSequenceTimeDomain(const std::string& filename,OfxRangeD &range) {
    
    if(!FFmpeg::isImageFile(filename)){
        _ffmpegFile = getFile(filename);
        
        if(_ffmpegFile->invalid()) {
            setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
            return false;
        }
        
        int width,height,frames;
        double ap;
        _ffmpegFile->info(width, height, ap, frames);
        range.min = 0;
        range.max = frames - 1;
        return true;
    }else{
        return false;
    }
    
}


void ReadFFmpegPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime time,OfxRectD& rod) {
    
    _ffmpegFile = getFile(filename);
    
    if(_ffmpegFile->invalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        return;
    }
    
    
    int width,height,frames;
    double ap;
    _ffmpegFile->info(width, height, ap, frames);
    rod.x1 = 0;
    rod.x2 = width;
    rod.y1 = 0;
    rod.y2 = height;
    
}

using namespace OFX;

#if 0
namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ReadFFmpegPluginFactory p("fr.inria.openfx:ReadFFmpeg", 1, 0);
            ids.push_back(&p);
        }
    };
};
#endif

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
void ReadFFmpegPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc);
    // basic labels
    desc.setLabels("ReadFFmpegOFX", "ReadFFmpegOFX", "ReadFFmpegOFX");
    desc.setPluginDescription("Read images or video using "
#                             ifdef FFMS_USE_FFMPEG_COMPAT
                              "FFmpeg"
#                             else
                              "libav"
#                             endif
                              ".\n\n" + ffmpeg_versions());

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "avi", "flv", "mov", "mp4", "mkv", "r3d", "bmp", "pix", "dpx", "exr", "jpeg", "jpg", "png", "pgm", "ppm", "ptx", "rgba", "rgb", "tiff", "tga", "gif", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadFFmpegPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), /*supportsRGBA =*/ false, /*supportsRGB =*/ false, /*supportsAlpha =*/ false, /*supportsTiles =*/ false);

    GenericReaderDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadFFmpegPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ReadFFmpegPlugin(handle);
}

