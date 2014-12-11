/*
 OCIOColorSpace plugin.
 Convert from one colorspace to another.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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


#include "OCIOColorSpace.h"

//#include <iostream>
#include <memory>

#include <GenericOCIO.h>

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "IOUtility.h"

#ifdef OFX_IO_USING_OCIO

#define kPluginName "OCIOColorSpaceOFX"
#define kPluginGrouping "Color/OCIO"
#define kPluginDescription "ColorSpace transformation using OpenColorIO configuration file."
#define kPluginIdentifier "fr.inria.openfx.OCIOColorSpace"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

class OCIOColorSpacePlugin : public OFX::ImageEffect
{
public:

    OCIOColorSpacePlugin(OfxImageEffectHandle handle);

    virtual ~OCIOColorSpacePlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* override changed clip */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // override the rod call
    //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
                       double time,
                       const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       OFX::Image* dstImg)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        copyPixelData(unpremult,
                      premult,
                      maskmix,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
                       double time,
                       const OfxRectI &renderWindow,
                       const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
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
        copyPixelData(unpremult,
                      premult,
                      maskmix,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
                       double time,
                       const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       OFX::BitDepthEnum dstBitDepth,
                       int dstRowBytes)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        copyPixelData(unpremult,
                      premult,
                      maskmix,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
                       double time,
                       const OfxRectI &renderWindow,
                       const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
                       OFX::BitDepthEnum srcPixelDepth,
                       int srcRowBytes,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       OFX::BitDepthEnum dstBitDepth,
                       int dstRowBytes);

    void setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                      double time,
                      const OfxRectI &renderWindow,
                      const void *srcPixelData,
                      const OfxRectI& srcBounds,
                      OFX::PixelComponentEnum srcPixelComponents,
                      OFX::BitDepthEnum srcPixelDepth,
                      int srcRowBytes,
                      void *dstPixelData,
                      const OfxRectI& dstBounds,
                      OFX::PixelComponentEnum dstPixelComponents,
                      OFX::BitDepthEnum dstPixelDepth,
                      int dstRowBytes);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;

    std::auto_ptr<GenericOCIO> _ocio;
};

OCIOColorSpacePlugin::OCIOColorSpacePlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, maskClip_(0)
, _ocio(new GenericOCIO(this))
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && (srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA || srcClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!maskClip_ || maskClip_->getPixelComponents() == OFX::ePixelComponentAlpha);
    _premult = fetchBooleanParam(kParamPremult);
    _premultChannel = fetchChoiceParam(kParamPremultChannel);
    assert(_premult && _premultChannel);
    _mix = fetchDoubleParam(kParamMix);
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);
}

OCIOColorSpacePlugin::~OCIOColorSpacePlugin()
{
}

/* set up and run a copy processor */
void
OCIOColorSpacePlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                                   double time,
                                   const OfxRectI &renderWindow,
                                   const void *srcPixelData,
                                   const OfxRectI& srcBounds,
                                   OFX::PixelComponentEnum srcPixelComponents,
                                   OFX::BitDepthEnum srcPixelDepth,
                                   int srcRowBytes,
                                   void *dstPixelData,
                                   const OfxRectI& dstBounds,
                                   OFX::PixelComponentEnum dstPixelComponents,
                                   OFX::BitDepthEnum dstPixelDepth,
                                   int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);

    // make sure bit depths are sane
    if(srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(time) : 0);
    std::auto_ptr<OFX::Image> orig(srcClip_->fetchImage(time));
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg(orig.get());
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, 0);

    // set the render window
    processor.setRenderWindow(renderWindow);

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);
    processor.setPremultMaskMix(premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

void
OCIOColorSpacePlugin::copyPixelData(bool unpremult,
                                    bool premult,
                                    bool maskmix,
                                    double time,
                                    const OfxRectI& renderWindow,
                                    const void *srcPixelData,
                                    const OfxRectI& srcBounds,
                                    OFX::PixelComponentEnum srcPixelComponents,
                                    OFX::BitDepthEnum srcPixelDepth,
                                    int srcRowBytes,
                                    void *dstPixelData,
                                    const OfxRectI& dstBounds,
                                    OFX::PixelComponentEnum dstPixelComponents,
                                    OFX::BitDepthEnum dstBitDepth,
                                    int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (!unpremult && !premult && !maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopier<float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopier<float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopier<float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }
    } else if (unpremult && !premult && !maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierUnPremult<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierUnPremult<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch

    } else if (!unpremult && !premult && maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierMaskMix<float, 4, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierMaskMix<float, 3, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierMaskMix<float, 1, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch

    } else if (!unpremult && premult && maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierPremultMaskMix<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierPremultMaskMix<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierPremultMaskMix<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch

    } else {
        assert(false); // should never happen
    }
}

/* Override the render */
void
OCIOColorSpacePlugin::render(const OFX::RenderArguments &args)
{
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(srcClip_);
    std::auto_ptr<const OFX::Image> srcImg(srcClip_->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (srcImg->getRenderScale().x != args.renderScale.x ||
        srcImg->getRenderScale().y != args.renderScale.y ||
        srcImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OFX::BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcImg->getPixelComponents();

    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat || dstBitDepth != srcBitDepth) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ((dstComponents != OFX::ePixelComponentRGBA && dstComponents != OFX::ePixelComponentRGB && dstComponents != OFX::ePixelComponentAlpha) ||
        dstComponents != srcComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        //throw std::runtime_error("render window outside of image bounds");
    }

    const void* srcPixelData = NULL;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    getImageData(srcImg.get(), &srcPixelData, &bounds, &pixelComponents, &bitDepth, &srcRowBytes);

    // allocate temporary image
    int pixelBytes = getPixelBytes(srcComponents, srcBitDepth);
    int tmpRowBytes = (args.renderWindow.x2-args.renderWindow.x1) * pixelBytes;
    size_t memSize = (args.renderWindow.y2-args.renderWindow.y1) * tmpRowBytes;
    OFX::ImageMemory mem(memSize,this);
    float *tmpPixelData = (float*)mem.lock();

    bool premult;
    _premult->getValueAtTime(args.time, premult);

    // copy renderWindow to the temporary image
    copyPixelData(premult, false, false, args.time, args.renderWindow, srcPixelData, bounds, pixelComponents, bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, bitDepth, tmpRowBytes);

    ///do the color-space conversion
    _ocio->apply(args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, tmpRowBytes);

    // copy the color-converted window and apply masking
    copyPixelData(false, premult, true, args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, bitDepth, tmpRowBytes, dstImg.get());
}

bool
OCIOColorSpacePlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    if (_ocio->isIdentity(args.time)) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}

void
OCIOColorSpacePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    return _ocio->changedParam(args, paramName);
}

void
OCIOColorSpacePlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
        switch (srcClip_->getPreMultiplication()) {
            case OFX::eImageOpaque:
                break;
            case OFX::eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case OFX::eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
        }
    }
}

using namespace OFX;

mDeclarePluginFactory(OCIOColorSpacePluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OCIOColorSpacePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextPaint);

    // add supported pixel depths
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OCIOColorSpacePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, OCIO_NAMESPACE::ROLE_REFERENCE);
    GenericOCIO::describeInContextOutput(desc, context, page, OCIO_NAMESPACE::ROLE_REFERENCE);

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIOColorSpacePluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OCIOColorSpacePlugin(handle);
}


void getOCIOColorSpacePluginID(OFX::PluginFactoryArray &ids)
{
    static OCIOColorSpacePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

#else // !OFX_IO_USING_OCIO
void getOCIOColorSpacePluginID(OFX::PluginFactoryArray &ids)
{
}
#endif
