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

#if (defined(_STDINT_H) || defined(_STDINT_H_) || defined(_MSC_STDINT_H_)) && !defined(UINT64_C)
#warning "__STDC_CONSTANT_MACROS has to be defined before including <stdint.h>, this file will probably not compile."
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // ...or stdint.h wont' define UINT64_C, needed by libavutil
#endif
#include "ReadFFmpeg.h"

#include <cmath>
#include <sstream>
#include <algorithm>

#include "IOUtility.h"

#include "GenericReader.h"
#include "FFmpegFile.h"

#define kPluginName "ReadFFmpegOFX"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read video using FFmpeg."
#define kPluginIdentifier "fr.inria.openfx.ReadFFmpeg"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamMaxRetries "maxRetries"
#define kParamMaxRetriesLabel "Max retries per frame"
#define kParamMaxRetriesHint "Some video files are sometimes tricky to read and needs several retries before successfully decoding a frame. This" \
" parameter controls how many times we should attempt to decode the same frame before failing. "

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha false
#define kSupportsTiles false


class ReadFFmpegPlugin : public GenericReaderPlugin
{
    
    FFmpegFileManager& _manager;
    OFX::IntParam *_maxRetries;
    
public:

    ReadFFmpegPlugin(FFmpegFileManager& manager,OfxImageEffectHandle handle);

    virtual ~ReadFFmpegPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    bool loadNearestFrame() const;

private:

    virtual bool isVideoStream(const std::string& filename) OVERRIDE FINAL;

    virtual void onInputFileChanged(const std::string& filename, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components, int *componentCount) OVERRIDE FINAL;

    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes) OVERRIDE FINAL;

    virtual bool getSequenceTimeDomain(const std::string& filename, OfxRangeI &range) OVERRIDE FINAL;

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, double *par, std::string *error) OVERRIDE FINAL;
    
    virtual bool getFrameRate(const std::string& filename, double* fps) OVERRIDE FINAL;
    
    virtual void restoreState(const std::string& filename) OVERRIDE FINAL;
};

ReadFFmpegPlugin::ReadFFmpegPlugin(FFmpegFileManager& manager,OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, false)
, _manager(manager)
, _maxRetries(0)
{
    _maxRetries = fetchIntParam(kParamMaxRetries);
    assert(_maxRetries);
    int originalFrameRangeMin, originalFrameRangeMax;
    _originalFrameRange->getValue(originalFrameRangeMin, originalFrameRangeMax);
    if (originalFrameRangeMin == 0) {
        // probably a buggy instance from before Jan 19 2015, where 0 is the first frame
        _originalFrameRange->setValue(originalFrameRangeMin+1, originalFrameRangeMax+1);
        int timeOffset;
        _timeOffset->getValue(timeOffset);
        _timeOffset->setValue(timeOffset - 1);
    }
}

ReadFFmpegPlugin::~ReadFFmpegPlugin()
{
    
}

void
ReadFFmpegPlugin::restoreState(const std::string& /*filename*/)
{
    //_manager.getOrCreate(this, filename);
}

bool
ReadFFmpegPlugin::loadNearestFrame() const
{
    int v;
    _missingFrameParam->getValue(v);
    return v == 0;
}

void
ReadFFmpegPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                               const std::string &paramName)
{
    GenericReaderPlugin::changedParam(args, paramName);
}

void
ReadFFmpegPlugin::onInputFileChanged(const std::string& filename,
                                     OFX::PreMultiplicationEnum *premult,
                                     OFX::PixelComponentEnum *components,
                                     int *componentCount)
{
    assert(premult && components && componentCount);
    //Clear all opened files by this plug-in since the user changed the selected file/sequence
    _manager.clear(this);
    FFmpegFile* file = _manager.getOrCreate(this, filename);
    
    if (!file || file->isInvalid()) {
        if (file) {
            setPersistentMessage(OFX::Message::eMessageError, "", file->getError());
        } else {
            setPersistentMessage(OFX::Message::eMessageError, "", "Cannot open file.");
        }
        *components = OFX::ePixelComponentNone;
        *componentCount = 0;
        *premult = OFX::eImageOpaque;
        return;
    }
    *componentCount = file->getNumberOfComponents();
    *components = (*componentCount > 3) ? OFX::ePixelComponentRGBA : OFX::ePixelComponentRGB;
    ///Ffmpeg is RGB opaque.
    *premult = (*componentCount > 3) ? OFX::eImageUnPreMultiplied : OFX::eImageOpaque;
}


bool
ReadFFmpegPlugin::isVideoStream(const std::string& filename)
{
    return !FFmpegFile::isImageFile(filename);
}

template<int nDstComp,int nSrcComp,int maxValue,typename PIX>
static void
fillWindow(const PIX* buffer,
           const OfxRectI& renderWindow,
           float *pixelData,
           const OfxRectI& imgBounds,
           OFX::PixelComponentEnum pixelComponents,
           int rowBytes)
{
    assert(nSrcComp >= 3 && nSrcComp <= 4);
    assert((nDstComp == 3 && pixelComponents == OFX::ePixelComponentRGB) ||
           (nDstComp == 4 && pixelComponents == OFX::ePixelComponentRGBA));
    ///fill the renderWindow in dstImg with the buffer freshly decoded.
    for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
        int srcY = renderWindow.y2 - y - 1;
        float* dst_pixels = (float*)((char*)pixelData + rowBytes*(y-imgBounds.y1));
        const PIX* src_pixels = buffer + (imgBounds.x2 - imgBounds.x1) * srcY * nSrcComp;

        for (int x = renderWindow.x1; x < renderWindow.x2; ++x) {
            int srcCol = x * nSrcComp ;
            int dstCol = x * nDstComp;
            dst_pixels[dstCol + 0] = intToFloat<maxValue>(src_pixels[srcCol + 0]);
            dst_pixels[dstCol + 1] = intToFloat<maxValue>(src_pixels[srcCol + 1]);
            dst_pixels[dstCol + 2] = intToFloat<maxValue>(src_pixels[srcCol + 2]);
            if (nDstComp == 4) {
                // Output is Opaque with alpha=0 by default,
                // but premultiplication is set to opaque.
                // That way, chaining with a Roto node works correctly.
                dst_pixels[dstCol + 3] = nSrcComp == 4 ? intToFloat<maxValue>(src_pixels[srcCol + 3]) : 0.f;
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
                         int pixelComponentCount,
                         int rowBytes)
{
    FFmpegFile* file = _manager.getOrCreate(this, filename);
    if (file && file->isInvalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", file->getError());
        return;
    }

    /// we only support RGB or RGBA output clip
    if ((pixelComponents != OFX::ePixelComponentRGB) &&
        (pixelComponents != OFX::ePixelComponentRGBA)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    assert((pixelComponents == OFX::ePixelComponentRGB && pixelComponentCount == 3) || (pixelComponents == OFX::ePixelComponentRGBA && pixelComponentCount == 4));

    ///blindly ignore the filename, we suppose that the file is the same than the file loaded in the changedParam
    if (!file) {
        setPersistentMessage(OFX::Message::eMessageError, "", filename +  ": Missing frame");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    int width,height,frames;
    double ap;
    file->getInfo(width, height, ap, frames);

    // wrong assert:
    // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
    //  "If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one."
    //  The renderWindow itself may or may not be the full image...
    //assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == width && renderWindow.y1 == 0 && renderWindow.y2 == height));

    if((imgBounds.x2 - imgBounds.x1) < width ||
       (imgBounds.y2 - imgBounds.y1) < height){
        setPersistentMessage(OFX::Message::eMessageError, "", "The host provided an image of wrong size, can't decode.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    int maxRetries;
    _maxRetries->getValue(maxRetries);
    
    try {
        // first frame of the video file is 1 in OpenFX, but 0 in File::decode, thus the -0.5 
        if ( !file->decode((int)std::floor(time-0.5), loadNearestFrame(), maxRetries) ) {
            
            setPersistentMessage(OFX::Message::eMessageError, "", file->getError());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
            
        }
    } catch (const std::exception& e) {
        int choice;
        _missingFrameParam->getValue(choice);
        if(choice == 1){ //error
            setPersistentMessage(OFX::Message::eMessageError, "", e.what());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        return;
    }

    const unsigned char* buffer = file->getData();
    std::size_t sizeOfData = file->getSizeOfData();
    unsigned int numComponents = file->getNumberOfComponents();
    assert(sizeOfData == sizeof(unsigned char) || sizeOfData == sizeof(unsigned short));
    ///fill the renderWindow in dstImg with the buffer freshly decoded.
    if (pixelComponents == OFX::ePixelComponentRGB) {
        if (sizeOfData == sizeof(unsigned char)) {
            if (numComponents == 3) {
                fillWindow<3,3,256,unsigned char>(buffer, renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            } else if (numComponents == 4) {
                fillWindow<3,4,256,unsigned char>(buffer, renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            }
        } else {
            if (numComponents == 3) {
                fillWindow<3,3,65536,unsigned short>(reinterpret_cast<const unsigned short*>(buffer), renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            } else {
                fillWindow<3,4,65536,unsigned short>(reinterpret_cast<const unsigned short*>(buffer), renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            }
        }
        
    } else if (pixelComponents == OFX::ePixelComponentRGBA) {
        if (sizeOfData == sizeof(unsigned char)) {
            if (numComponents == 3) {
                fillWindow<4,3,256,unsigned char>(buffer, renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            } else {
                fillWindow<4,4,256,unsigned char>(buffer, renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            }
        } else {
            if (numComponents == 3) {
                fillWindow<4,3,65536,unsigned short>(reinterpret_cast<const unsigned short*>(buffer), renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            } else {
                fillWindow<4,4,65536,unsigned short>(reinterpret_cast<const unsigned short*>(buffer), renderWindow, pixelData, imgBounds, pixelComponents, rowBytes);
            }
        }
    }
}

bool
ReadFFmpegPlugin::getSequenceTimeDomain(const std::string& filename, OfxRangeI &range)
{
    if (FFmpegFile::isImageFile(filename)) {
        range.min = range.max = 0.;
        return false;
    }

    int width,height,frames;
    double ap;
    FFmpegFile* file = _manager.getOrCreate(this, filename);
    if (!file || file->isInvalid()) {
        range.min = range.max = 0.;
        return false;
    }
    file->getInfo(width, height, ap, frames);

    range.min = 1;
    range.max = frames;
    return true;
}

bool
ReadFFmpegPlugin::getFrameRate(const std::string& filename,
                               double* fps)
{
    assert(fps);
    
    FFmpegFile* file = _manager.getOrCreate(this, filename);
    if (!file || file->isInvalid()) {
        return false;
    }

    bool gotFps = file->getFPS(*fps);
    return gotFps;
}


bool
ReadFFmpegPlugin::getFrameBounds(const std::string& filename,
                                 OfxTime /*time*/,
                                 OfxRectI *bounds,
                                 double *par,
                                 std::string *error)
{
    assert(bounds && par);
    FFmpegFile* file = _manager.getOrCreate(this, filename);
    if (!file || file->isInvalid()) {
        if (error && file) {
            *error = file->getError();
        }
        return false;
    }

    int width,height,frames;
    double ap;
    if (!file->getInfo(width, height, ap, frames)) {
        width = 0;
        height = 0;
        ap = 1.;
    }
    bounds->x1 = 0;
    bounds->x2 = width;
    bounds->y1 = 0;
    bounds->y2 = height;
    *par = ap;
    return true;
}


using namespace OFX;

class ReadFFmpegPluginFactory : public OFX::PluginFactoryHelper<ReadFFmpegPluginFactory>
{
    FFmpegFileManager _manager;
    
public:
    ReadFFmpegPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin)
    : OFX::PluginFactoryHelper<ReadFFmpegPluginFactory>(id, verMaj, verMin)
    , _manager()
    {}
    
    virtual void load() {}
    virtual void unload() {}
    
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
    
    bool isVideoStreamPlugin() const { return true; }
    
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
};


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



#ifdef OFX_IO_MT_FFMPEG
static int
FFmpegLockManager(void** mutex, enum AVLockOp op)
{
    switch (op) {
        case AV_LOCK_CREATE: // Create a mutex.
            try {
                *mutex = static_cast< void* >(new OFX::MultiThread::Mutex);
                return 0;
            }
            catch(...) {
                // Return error if mutex creation failed.
                return 1;
            }
            
        case AV_LOCK_OBTAIN: // Lock the specified mutex.
            try {
                static_cast< OFX::MultiThread::Mutex* >(*mutex)->lock();
                return 0;
            }
            catch(...) {
                // Return error if mutex lock failed.
                return 1;
            }
            
        case AV_LOCK_RELEASE: // Unlock the specified mutex.
            // Mutex unlock can't fail.
            static_cast< OFX::MultiThread::Mutex* >(*mutex)->unlock();
            return 0;
            
        case AV_LOCK_DESTROY: // Destroy the specified mutex.
            // Mutex destruction can't fail.
            delete static_cast< OFX::MultiThread::Mutex* >(*mutex);
            *mutex = 0;
            return 0;
            
        default: // Unknown operation.
            assert(false);
            return 1;
    }
}
#endif

/** @brief The basic describe function, passed a plugin descriptor */
void
ReadFFmpegPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles, false);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription("Read images or video using "
#                             ifdef FFMS_USE_FFMPEG_COMPAT
                              "FFmpeg"
#                             else
                              "libav"
#                             endif
                              ".\n\n" + ffmpeg_versions());
#ifdef OFX_IO_MT_FFMPEG
    // Register a lock manager callback with FFmpeg, providing it the ability to use mutex locking around
    // otherwise non-thread-safe calls.
    av_lockmgr_register(FFmpegLockManager);
#endif
    
    
    av_log_set_level(AV_LOG_WARNING);
    avcodec_register_all();
    av_register_all();
    
    
    _manager.init();
    
#ifdef OFX_EXTENSIONS_TUTTLE
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
    
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    
  
    
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadFFmpegPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles);
    
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamMaxRetries);
        param->setLabel(kParamMaxRetriesLabel);
        param->setHint(kParamMaxRetriesHint);
        param->setAnimates(false);
        param->setDefault(10);
        param->setRange(0, 100);
        param->setDisplayRange(0, 20);
        page->addChild(*param);
    }

    GenericReaderDescribeInContextEnd(desc, context, page, "rec709", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadFFmpegPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    ReadFFmpegPlugin* ret =  new ReadFFmpegPlugin(_manager,handle);
    ret->restoreStateFromParameters();
    return ret;
}



void getReadFFmpegPluginID(OFX::PluginFactoryArray &ids)
{
    static ReadFFmpegPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

