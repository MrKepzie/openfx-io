/*
 OFX GenericWriter plugin.
 A base class for all OpenFX-based encoders.
 
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
#ifndef Io_GenericWriter_h
#define Io_GenericWriter_h

#include <memory>
#include <ofxsImageEffect.h>
#include "IOUtility.h"
#include "ofxsMacros.h"
#include "ofxsPixelProcessor.h" // for getImageData
#include "ofxsCopier.h" // for copyPixels

namespace OFX {
    class PixelProcessorFilterBase;
}
class GenericOCIO;

/**
 * @brief A generic writer plugin, derive this to create a new writer for a specific file format.
 * This class propose to handle the common stuff among writers:
 * - common params
 * - a way to inform the host about the colour-space of the data.
 **/
class GenericWriterPlugin : public OFX::ImageEffect {
    
public:
    
    GenericWriterPlugin(OfxImageEffectHandle handle);
    
    virtual ~GenericWriterPlugin();
    
    /**
     * @brief Don't override this function, the GenericWriterPlugin class already does the rendering. The "encoding" of the frame
     * must be done by the pure virtual function encode(...) instead.
     * The render function also copies the image from the input clip to the output clip (only if the effect is connected downstream)
     * in order to be able to branch this effect in the middle of an effect tree.
     **/
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE;

    /** @brief client begin sequence render function */
    virtual void beginSequenceRender(const OFX::BeginSequenceRenderArguments &args) OVERRIDE;
    
    /** @brief client end sequence render function */
    virtual void endSequenceRender(const OFX::EndSequenceRenderArguments &args) OVERRIDE;
    
    /**
     * @brief Don't override this. It returns the projects region of definition.
     **/
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /**
     * @brief Don't override this. It returns the source region of definition.
     **/
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    /**
     * @brief Don't override this. It returns the frame range to render.
     **/
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /**
     * @brief You can override this to take actions in response to a param change.
     * Make sure you call the base-class version of this function at the end: i.e:
     *
     * void MyReader::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
     *      if (.....) {
     *
     *      } else if(.....) {
     *
     *      } else {
     *          GenericReaderPlugin::changedParam(args,paramName);
     *      }
     * }
     **/
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE;
    
    /**
     * @brief Overriden to handle premultiplication parameter given the input clip.
     * Make sure you call the base class implementation if you override it.
     **/
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE;
    
    /**
     * @brief Overriden to set the clips premultiplication according to the user and plug-ins wishes.
     **/
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE;
    
    /**
     * @brief Overriden to clear any OCIO cache.
     * This function calls clearAnyCache() if you have any cache to clear.
     **/
    void purgeCaches(void) OVERRIDE FINAL;

    
    
protected:
    
    /**
     * @brief Override this function to actually encode the image in the file pointed to by filename.
     * If the file is a video-stream then you should encode the frame at the time given in parameters.
     * You must write the decoded image into dstImg. This function should convert the  pixels from srcImg
     * into the color-space and bitdepths of the newly created images's file.
     * You can inform the host of the bitdepth you support in input in the describe() function.
     * You can always skip the color-space conversion, but for all linear hosts it would produce either
     * false colors or sub-par performances in the case the end-user has to prepend a color-space conversion
     * effect her/himself.
     *
     * @pre The filename has been validated against the supported file extensions.
     * You don't need to check this yourself.
     **/
    virtual void encode(const std::string& filename,
                        OfxTime time,
                        const float *pixelData,
                        const OfxRectI& bounds,
                        float pixelAspectRatio,
                        OFX::PixelComponentEnum pixelComponents,
                        int rowBytes) = 0;
    
    virtual void beginEncode(const std::string& /*filename*/,
                             const OfxRectI& /*rod*/,
                             float /*pixelAspectRatio*/,
                             const OFX::BeginSequenceRenderArguments &/*args*/) {}
    
    virtual void endEncode(const OFX::EndSequenceRenderArguments &/*args*/) {}

    /**
     * @brief Overload to return false if the given file extension is a video file extension or
     * true if this is an image file extension.
     **/
    virtual bool isImageFile(const std::string& fileExtension) const = 0;
    
    virtual void setOutputFrameRate(double /*fps*/) {}
    
    /**
     * @brief Must return whether your plug-in expects an input stream to be premultiplied or unpremultiplied to encode
     * properly into the file.
     **/
    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const = 0;

    
    OFX::Clip* _inputClip; //< Mantated input clip
    OFX::Clip *_outputClip; //< Mandated output clip
    OFX::StringParam  *_fileParam; //< The output file
    OFX::ChoiceParam *_frameRange; //<The frame range type
    OFX::IntParam* _firstFrame; //< the first frame if the frame range type is "Manual"
    OFX::IntParam* _lastFrame; //< the last frame if the frame range type is "Manual"
    OFX::ChoiceParam* _outputFormatType; //< the type of output format
    OFX::ChoiceParam* _outputFormat; //< the output format to render
    OFX::ChoiceParam* _premult;
    std::auto_ptr<GenericOCIO> _ocio;

private:
    
    /**
     * @brief Retrieves the output filename at the given time and checks if the extension is supported.
     **/
    void getOutputFileNameAndExtension(OfxTime time,std::string& filename);
    
    /**
     * @brief Override if you want to do something when the output image/video file changed.
     * You shouldn't do any strong processing as this is called on the main thread and
     * the getRegionOfDefinition() and  decode() should open the file in a separate thread.
     **/
    virtual void onOutputFileChanged(const std::string& /*newFile*/) {}

    /**
     * @brief Override to clear any cache you may have.
     **/
    virtual void clearAnyCache() {}
    
    void getRegionOfDefinitionInternal(OfxTime time,OfxRectD& rod);

    void copyPixelData(const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       OFX::Image* dstImg)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        int srcPixelComponentCount = srcImg->getPixelComponentCount();
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        int dstPixelComponentCount = dstImg->getPixelComponentCount();
        copyPixels(*this,
                   renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
                       int srcPixelComponentCount,
                       OFX::BitDepthEnum srcBitDepth,
                       int srcRowBytes,
                       OFX::Image* dstImg)
    {
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        int dstPixelComponentCount = dstImg->getPixelComponentCount();
        copyPixels(*this,
                   renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       int dstPixelComponentCount,
                       OFX::BitDepthEnum dstBitDepth,
                       int dstRowBytes)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        int srcPixelComponentCount = srcImg->getPixelComponentCount();
        copyPixels(*this,
                   renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void unPremultPixelData(const OfxRectI &renderWindow,
                            const void *srcPixelData,
                            const OfxRectI& srcBounds,
                            OFX::PixelComponentEnum srcPixelComponents,
                            int srcPixelComponentCount,
                            OFX::BitDepthEnum srcPixelDepth,
                            int srcRowBytes,
                            void *dstPixelData,
                            const OfxRectI& dstBounds,
                            OFX::PixelComponentEnum dstPixelComponents,
                            int dstPixelComponentCount,
                            OFX::BitDepthEnum dstBitDepth,
                            int dstRowBytes);
    
    void premultPixelData(const OfxRectI &renderWindow,
                          const void *srcPixelData,
                          const OfxRectI& srcBounds,
                          OFX::PixelComponentEnum srcPixelComponents,
                          int srcPixelComponentCount,
                          OFX::BitDepthEnum srcPixelDepth,
                          int srcRowBytes,
                          void *dstPixelData,
                          const OfxRectI& dstBounds,
                          OFX::PixelComponentEnum dstPixelComponents,
                          int dstPixelComponentCount,
                          OFX::BitDepthEnum dstBitDepth,
                          int dstRowBytes);
};

void GenericWriterDescribe(OFX::ImageEffectDescriptor &desc,OFX::RenderSafetyEnum safety);
OFX::PageParamDescriptor* GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault);
void GenericWriterDescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context,OFX::PageParamDescriptor* defaultPage);

#define mDeclareWriterPluginFactory(CLASS, LOADFUNCDEF, UNLOADFUNCDEF,ISVIDEOSTREAM) \
  class CLASS : public OFX::PluginFactoryHelper<CLASS>                       \
  {                                                                     \
  public:                                                                \
    CLASS(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<CLASS>(id, verMaj, verMin){} \
    virtual void load() LOADFUNCDEF ;                                   \
    virtual void unload() UNLOADFUNCDEF ;                               \
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context); \
    bool isVideoStreamPlugin() const { return ISVIDEOSTREAM; }  \
    virtual void describe(OFX::ImageEffectDescriptor &desc);      \
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context); \
  };

#endif
