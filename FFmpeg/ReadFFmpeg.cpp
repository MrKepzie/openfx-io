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
#include <algorithm>

#include "IOUtility.h"

#include "GenericReader.h"
#include "FFmpegHandler.h"

#define kPluginName "ReadFFmpegOFX"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read video using FFmpeg."
#define kPluginIdentifier "fr.inria.openfx.ReadFFmpeg"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha false
#define kSupportsTiles false

class ReadFFmpegPlugin : public GenericReaderPlugin
{
    FFmpeg::File* _ffmpegFile; //< a ptr to the ffmpeg file, don't delete it the FFmpegFileManager handles their allocation/deallocation

    unsigned char* _buffer;
    int _bufferWidth;
    int _bufferHeight;

public:

    ReadFFmpegPlugin(OfxImageEffectHandle handle);

    virtual ~ReadFFmpegPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    bool loadNearestFrame() const;

private:

    virtual bool isVideoStream(const std::string& filename) OVERRIDE FINAL;

    virtual void onInputFileChanged(const std::string& filename, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components) OVERRIDE FINAL;

    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool getSequenceTimeDomain(const std::string& filename,OfxRangeD &range) OVERRIDE FINAL;

    virtual bool getFrameRegionOfDefinition(const std::string& filename, OfxTime time, OfxRectD *rod, std::string *error) OVERRIDE FINAL;
};

ReadFFmpegPlugin::ReadFFmpegPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles)
, _ffmpegFile(0)
, _buffer(0)
, _bufferWidth(0)
, _bufferHeight(0)
{
    std::string filename;
    _fileParam->getValue(filename);
    _ffmpegFile = new FFmpeg::File(filename);
}

ReadFFmpegPlugin::~ReadFFmpegPlugin() {
    
    if(_buffer){
        delete [] _buffer;
    }
    if (_ffmpegFile) {
        delete _ffmpegFile;
    }
}

bool ReadFFmpegPlugin::loadNearestFrame() const {
    int v;
    _missingFrameParam->getValue(v);
    return v == 0;
}

void ReadFFmpegPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericReaderPlugin::changedParam(args, paramName);
}

void
ReadFFmpegPlugin::onInputFileChanged(const std::string& filename,
                                     OFX::PreMultiplicationEnum *premult,
                                     OFX::PixelComponentEnum *components)
{
    assert(premult && components);
    ///Ffmpeg is RGB opaque.
    ///The GenericReader is responsible for checking if RGB is good enough, otherwise will map it to RGBA
    *components = OFX::ePixelComponentRGB;
    *premult = OFX::eImageOpaque;
    
    assert(_ffmpegFile);
    if (_ffmpegFile) {
        if (_ffmpegFile->getFilename() == filename) {
            return;
        } else {
            _ffmpegFile->open(filename);
        }
        
        if (!_ffmpegFile->isValid()) {
            setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->getError());
        }
    }
}


bool ReadFFmpegPlugin::isVideoStream(const std::string& filename){
    return !FFmpeg::isImageFile(filename);
}

template<int nComponents>
static void
fillWindow(const unsigned char* buffer,
           const OfxRectI& renderWindow,
           float *pixelData,
           const OfxRectI& imgBounds,
           OFX::PixelComponentEnum pixelComponents,
           int rowBytes)
{
    assert((nComponents == 3 && pixelComponents == OFX::ePixelComponentRGB) ||
           (nComponents == 4 && pixelComponents == OFX::ePixelComponentRGBA));
    ///fill the renderWindow in dstImg with the buffer freshly decoded.
    for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
        int srcY = renderWindow.y2 - y - 1;
        float* dst_pixels = (float*)((char*)pixelData + rowBytes*(y-imgBounds.y1));
        const unsigned char* src_pixels = buffer + (imgBounds.x2 - imgBounds.x1) * srcY * 3;

        for (int x = renderWindow.x1; x < renderWindow.x2; ++x) {
            int srcCol = x * 3;
            int dstCol = x * nComponents;
            dst_pixels[dstCol + 0] = intToFloat<256>(src_pixels[srcCol + 0]);
            dst_pixels[dstCol + 1] = intToFloat<256>(src_pixels[srcCol + 1]);
            dst_pixels[dstCol + 2] = intToFloat<256>(src_pixels[srcCol + 2]);
            if (nComponents == 4) {
                // Output is Opaque with alpha=0 by default,
                // but premultiplication is set to opaque.
                // That way, chaining with a Roto node works correctly.
                dst_pixels[dstCol + 3] = 0.f;
            }
        }
    }
}

void
ReadFFmpegPlugin::decode(const std::string& filename,
                         OfxTime time,
                         const OfxRectI& renderWindow,
                         float *pixelData,
                         const OfxRectI& imgBounds,
                         OFX::PixelComponentEnum pixelComponents,
                         int rowBytes)
{
    if (_ffmpegFile && filename != _ffmpegFile->getFilename()) {
        _ffmpegFile->open(filename);
    } else if (!_ffmpegFile) {
        return;
    }
    if (!_ffmpegFile->isValid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->getError());
        return;
    }

    /// we only support RGB or RGBA output clip
    if ((pixelComponents != OFX::ePixelComponentRGB) &&
        (pixelComponents != OFX::ePixelComponentRGBA)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    ///blindly ignore the filename, we suppose that the file is the same than the file loaded in the changedParam
    if (!_ffmpegFile) {
        setPersistentMessage(OFX::Message::eMessageError, "",filename +  ": Missing frame");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    int width,height,frames;
    double ap;
    
    _ffmpegFile->getInfo(width, height, ap, frames);

    // wrong assert:
    // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
    //  "If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one."
    //  The renderWindow itself may or may not be the full image...
    //assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == width && renderWindow.y1 == 0 && renderWindow.y2 == height));

    if((imgBounds.x2 - imgBounds.x1) < width ||
       (imgBounds.y2 - imgBounds.y1) < height){
        setPersistentMessage(OFX::Message::eMessageError, "", "The host provided an image of wrong size, can't decode.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    ///set the pixel aspect ratio
    // sorry, but this seems to be read-only,
    // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImagePropPixelAspectRatio
    //dstImg->getPropertySet().propSetDouble(kOfxImagePropPixelAspectRatio, ap, 0);
    
    if (_bufferWidth != width || _bufferHeight != height){
        delete [] _buffer;
        _buffer = 0;
    }
    
    if (!_buffer){
        _buffer = new unsigned char[width * height * 3];
        _bufferHeight = height;
        _bufferWidth = width;
    }
    
    //< round the time to an int to get a frame number ? Not sure about this
    try {
        if (!_ffmpegFile->decode(_buffer, std::floor(time+0.5),loadNearestFrame())) {
            setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->getError());
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    } catch (const std::exception& e) {
        int choice;
        _missingFrameParam->getValue(choice);
        if(choice == 1){ //error
            setPersistentMessage(OFX::Message::eMessageError, "", e.what());
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        return;
    }

    ///fill the renderWindow in dstImg with the buffer freshly decoded.
    if (pixelComponents == OFX::ePixelComponentRGB) {
        fillWindow<3>(_buffer, renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
    } else if (pixelComponents == OFX::ePixelComponentRGBA) {
        fillWindow<4>(_buffer, renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
    }
}

bool ReadFFmpegPlugin::getSequenceTimeDomain(const std::string& filename,OfxRangeD &range) {
    

    assert(_ffmpegFile);

    
    if (!FFmpeg::isImageFile(filename)) {
        
        int width,height,frames;
        double ap;
        if (_ffmpegFile) {
            _ffmpegFile->getInfo(width, height, ap, frames);
            
            range.min = 0;
            range.max = frames - 1;
        } else {
            return false;
        }
        return true;
    } else {
        return false;
    }
    
}


bool
ReadFFmpegPlugin::getFrameRegionOfDefinition(const std::string& filename,
                                             OfxTime /*time*/,
                                             OfxRectD *rod,
                                             std::string *error)
{
    assert(rod);
    ///blindly ignore the filename, we suppose that the file is the same than the file loaded in the changedParam
    if (_ffmpegFile && filename != _ffmpegFile->getFilename()) {
        _ffmpegFile->open(filename);
    } else if (!_ffmpegFile) {
        return false;
    }
    if (!_ffmpegFile->isValid()) {
        if (error) {
            *error = _ffmpegFile->getError();
        }
        //setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->getError());
        return false;
    }
    
    if(!_ffmpegFile) {
        if (error) {
            *error = filename + ": no such file";
        }
        return false;
    }
    int width,height,frames;
    double ap;
    _ffmpegFile->getInfo(width, height, ap, frames);
    rod->x1 = 0;
    rod->x2 = width;
    rod->y1 = 0;
    rod->y2 = height;
    return true;
}


using namespace OFX;

mDeclareReaderPluginFactory(ReadFFmpegPluginFactory, {}, {},true);

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

static
std::vector<std::string> &
split(const std::string &s, char delim, std::vector<std::string> &elems)
{
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

/** @brief The basic describe function, passed a plugin descriptor */
void
ReadFFmpegPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginDescription("Read images or video using "
#                             ifdef FFMS_USE_FFMPEG_COMPAT
                              "FFmpeg"
#                             else
                              "libav"
#                             endif
                              ".\n\n" + ffmpeg_versions());

#ifdef OFX_EXTENSIONS_TUTTLE
    av_register_all();
    std::vector<std::string> extensions;
	{
		AVInputFormat* iFormat = av_iformat_next(NULL);
		while (iFormat != NULL) {
			if (iFormat->extensions != NULL) {
				std::string extStr( iFormat->extensions );
                split(extStr, ',', extensions);

				// name's format defines (in general) extensions
				// require to fix extension in LibAV/FFMpeg to don't use it.
				extStr = iFormat->name;
                split(extStr, ',', extensions);
			}
			iFormat = av_iformat_next( iFormat );
		}
	}

	// Hack: Add basic video container extensions
	// as some versions of LibAV doesn't declare properly all extensions...
	extensions.push_back("mov");
	extensions.push_back("avi");
    extensions.push_back("mp4");
	extensions.push_back("mpg");
	extensions.push_back("mkv");
	extensions.push_back("flv");
	extensions.push_back("m2ts");

	// sort / unique
	std::sort(extensions.begin(), extensions.end());
	extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());


    //const char* extensions[] = { "avi", "flv", "mov", "mp4", "mkv", "r3d", "bmp", "pix", "dpx", "exr", "jpeg", "jpg", "png", "pgm", "ppm", "ptx", "rgba", "rgb", "tiff", "tga", "gif", NULL };
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(0);
#endif
    
#ifndef OFX_IO_MT_FFMPEG
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadFFmpegPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles);

    GenericReaderDescribeInContextEnd(desc, context, page, "rec709", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadFFmpegPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    ReadFFmpegPlugin* ret =  new ReadFFmpegPlugin(handle);
    ret->restoreStateFromParameters();
    return ret;
}



void getReadFFmpegPluginID(OFX::PluginFactoryArray &ids)
{
    static ReadFFmpegPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

