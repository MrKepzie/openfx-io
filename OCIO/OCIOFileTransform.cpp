/*
 OCIOFileTransform plugin.
 Apply a LUT conversion loaded from file.

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


#include "OCIOFileTransform.h"

#include <OpenColorIO/OpenColorIO.h>

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "IOUtility.h"
#include "ofxNatron.h"
#include "GenericOCIO.h"

namespace OCIO = OCIO_NAMESPACE;

#define kPluginName "OCIOFileTransformOFX"
#define kPluginGrouping "Color/OCIO"
#define kPluginDescription  "Use OpenColorIO to apply a transform loaded from the given " \
"file.\n\n" \
"This is usually a 1D or 3D LUT file, but can be other file-based " \
"transform, for example an ASC ColorCorrection XML file.\n\n" \
"Note that the file's transform is applied with no special " \
"input/output colorspace handling - so if the file expects " \
"log-encoded pixels, but you apply the node to a linear " \
"image, you will get incorrect results."

#define kPluginIdentifier "fr.inria.openfx:OCIOFileTransform"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kFileParamName "file"
#define kFileParamLabel "File"
#define kFileParamHint "File containing the transform."

// Reload button, and hidden "version" knob to invalidate cache on reload
#define kReloadParamName "reload"
#define kReloadParamLabel "Reload"
#define kReloadParamHint "Reloads specified files"
#define kVersionParamName "version"

#define kCCCIDParamName "cccId"
#define kCCCIDParamLabel "CCC Id"
#define kCCCIDParamHint "If the source file is an ASC CDL CCC (color correction collection), " \
"this specifies the id to lookup. OpenColorIO::Contexts (envvars) are obeyed."
#define kCCCIDChoiceParamName "cccIdIndex"

#define kDirectionParamName "direction"
#define kDirectionParamLabel "Direction"
#define kDirectionParamHint "Transform direction."
#define kDirectionParamChoiceForward "Forward"
#define kDirectionParamChoiceInverse "Inverse"

#define kInterpolationParamName "interpolation"
#define kInterpolationParamLabel "Interpolation"
#define kInterpolationParamHint "Interpolation method. For files that are not LUTs (mtx, etc) this is ignored."
#define kInterpolationParamChoiceNearest "Nearest"
#define kInterpolationParamChoiceLinear "Linear"
#define kInterpolationParamChoiceTetrahedral "Tetrahedral"
#define kInterpolationParamChoiceBest "Best"

static bool gHostIsNatron = false; // TODO: generate a CCCId choice param kCCCIDChoiceParamName from available IDs

class OCIOFileTransformPlugin : public OFX::ImageEffect
{
public:

    OCIOFileTransformPlugin(OfxImageEffectHandle handle);

    virtual ~OCIOFileTransformPlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);

    /* override is identity */
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime);

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

    /* override changed clip */
    //virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName);

    // override the rod call
    //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);

    // override the roi call
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);

private:
    void updateCCCId();

    template<bool masked>
    void copyPixelData(double time,
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
        copyPixelData<masked>(time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    template<bool masked>
    void copyPixelData(double time,
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
        copyPixelData<masked>(time,
                              renderWindow,
                              srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                              dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    template<bool masked>
    void copyPixelData(double time,
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
        copyPixelData<masked>(time,
                              renderWindow,
                              srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                              dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    template<bool masked>
    void copyPixelData(double time,
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

    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes);

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

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    OFX::StringParam *file_;
    OFX::IntParam *version_;
    OFX::StringParam *cccid_;
    OFX::ChoiceParam *direction_;
    OFX::ChoiceParam *interpolation_;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};

OCIOFileTransformPlugin::OCIOFileTransformPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, maskClip_(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && (srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA || srcClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!maskClip_ || maskClip_->getPixelComponents() == OFX::ePixelComponentAlpha);
    file_ = fetchStringParam(kFileParamName);
    version_ = fetchIntParam(kVersionParamName);
    cccid_ = fetchStringParam(kCCCIDParamName);
    direction_ = fetchChoiceParam(kDirectionParamName);
    interpolation_ = fetchChoiceParam(kInterpolationParamName);
    assert(file_ && version_ && cccid_ && direction_ && interpolation_);
    _mix = fetchDoubleParam(kMixParamName);
    _maskInvert = fetchBooleanParam(kMaskInvertParamName);
    assert(_mix && _maskInvert);
    updateCCCId();
}

OCIOFileTransformPlugin::~OCIOFileTransformPlugin()
{
}

/* set up and run a copy processor */
void
OCIOFileTransformPlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
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
        processor.doMasking(true);
        processor.setOrigImg(orig.get());
        processor.setMaskImg(mask.get());
    }

    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);

    double mix;
    _mix->getValueAtTime(time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(time, maskInvert);
    processor.setMaskMix(mix, maskInvert);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

template<bool masked>
void
OCIOFileTransformPlugin::copyPixelData(double time,
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
    if (masked && getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopier<float, 4, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopier<float, 3, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopier<float, 1, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch
    } else {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopier<float, 4, 1, false> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopier<float, 3, 1, false> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopier<float, 1, 1, false> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch
    }
}

void
OCIOFileTransformPlugin::apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    // are we in the image bounds
    if(renderWindow.x1 < bounds.x1 || renderWindow.x1 >= bounds.x2 || renderWindow.y1 < bounds.y1 || renderWindow.y1 >= bounds.y2 ||
       renderWindow.x2 <= bounds.x1 || renderWindow.x2 > bounds.x2 || renderWindow.y2 <= bounds.y1 || renderWindow.y2 > bounds.y2) {
        throw std::runtime_error("OCIO: render window outside of image bounds");
    }
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB) {
        throw std::runtime_error("OCIO: invalid components (only RGB and RGBA are supported)");
    }

    OCIOProcessor processor(*this);
    // set the images
    processor.setDstImg(pixelData, bounds, pixelComponents, OFX::eBitDepthFloat, rowBytes);

    std::string file;
    file_->getValueAtTime(time, file);
    std::string cccid;
    cccid_->getValueAtTime(time, cccid);

    try {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        assert(config);
        OCIO::FileTransformRcPtr transform = OCIO::FileTransform::Create();
        transform->setSrc(file.c_str());

        transform->setCCCId(cccid.c_str());

        int direction_i;
        direction_->getValueAtTime(time, direction_i);

        if (direction_i == 0) {
            transform->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        } else {
            transform->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        }

        int interpolation_i;
        interpolation_->getValueAtTime(time, interpolation_i);

        if (interpolation_i == 0) {
            transform->setInterpolation(OCIO::INTERP_NEAREST);
        } else if(interpolation_i == 1) {
            transform->setInterpolation(OCIO::INTERP_LINEAR);
        } else if(interpolation_i == 2) {
            transform->setInterpolation(OCIO::INTERP_TETRAHEDRAL);
        } else if(interpolation_i == 3) {
            transform->setInterpolation(OCIO::INTERP_BEST);
        } else {
            // Should never happen
            setPersistentMessage(OFX::Message::eMessageError, "", "OCIO Interpolation value out of bounds");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }

        processor.setValues(config, transform, OCIO::TRANSFORM_DIR_FORWARD);
    } catch (const OCIO::Exception &e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

/* Override the render */
void
OCIOFileTransformPlugin::render(const OFX::RenderArguments &args)
{
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(srcClip_);
    std::auto_ptr<OFX::Image> srcImg(srcClip_->fetchImage(args.time));
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

    // copy renderWindow to the temporary image
    copyPixelData<false>(args.time, args.renderWindow, srcPixelData, bounds, pixelComponents, bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, bitDepth, tmpRowBytes);

    ///do the color-space conversion
    apply(args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, tmpRowBytes);

    // copy the color-converted window
    copyPixelData<true>(args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, bitDepth, tmpRowBytes, dstImg.get());
}

bool
OCIOFileTransformPlugin::isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    std::string file;
    file_->getValue(file);
    if (file.empty()) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}

void
OCIOFileTransformPlugin::updateCCCId()
{
    // Convoluted equiv to pysting::endswith(m_file, ".ccc")
    // TODO: Could this be queried from the processor?
    std::string srcstring;
    file_->getValue(srcstring);
    const std::string cccext = "ccc";
    const std::string ccext = "cc";
    if(std::equal(cccext.rbegin(), cccext.rend(), srcstring.rbegin()) ||
       std::equal(ccext.rbegin(), ccext.rend(), srcstring.rbegin())) {
        cccid_->setIsSecret(false);
    } else {
        cccid_->setIsSecret(true);
    }
}

void
OCIOFileTransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    clearPersistentMessage();
    // Only show the cccid knob when loading a .cc/.ccc file. Set
    // hidden state when the src is changed, or the node properties
    // are shown
    if (paramName == kFileParamName) {
        updateCCCId();
    } else if (paramName == kReloadParamName) {
        version_->setValue(version_->getValue()+1); // invalidate the node cache
        OCIO::ClearAllCaches();
    }

}

using namespace OFX;

mDeclarePluginFactory(OCIOFileTransformPluginFactory, {}, {});

static std::string
supportedFormats()
{
    std::ostringstream os;

    os << "Supported formats:\n";
    for(int i=0; i<OCIO::FileTransform::getNumFormats(); ++i)
    {
        const char* name = OCIO::FileTransform::getFormatNameByIndex(i);
        const char* exten = OCIO::FileTransform::getFormatExtensionByIndex(i);
        os << "\n." << exten << " (" << name << ")";
    }
    
    return os.str();
}

/** @brief The basic describe function, passed a plugin descriptor */
void OCIOFileTransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(std::string(kPluginDescription) + "\n\n" + supportedFormats());

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextPaint);

    // add supported pixel depths
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSupportsTiles(true);
    desc.setRenderThreadSafety(eRenderFullySafe);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OCIOFileTransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(true);

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    StringParamDescriptor *file = desc.defineStringParam(kFileParamName);
    file->setLabels(kFileParamLabel, kFileParamLabel, kFileParamLabel);
    file->setHint(std::string(kFileParamHint) + "\n\n" + supportedFormats());
    file->setStringType(eStringTypeFilePath);
    file->setFilePathExists(true);
    file->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*file);

    PushButtonParamDescriptor *reload = desc.definePushButtonParam(kReloadParamName);
    reload->setLabels(kReloadParamLabel, kReloadParamLabel, kReloadParamLabel);
    reload->setHint(kReloadParamHint);
    page->addChild(*reload);

    IntParamDescriptor *version = desc.defineIntParam(kVersionParamName);
    version->setIsSecret(true);
    version->setDefault(1);
    page->addChild(*version);

    StringParamDescriptor *cccid = desc.defineStringParam(kCCCIDParamName);
    cccid->setLabels(kCCCIDParamLabel, kCCCIDParamLabel, kCCCIDParamLabel);
    cccid->setHint(kCCCIDParamHint);
    page->addChild(*cccid);

    ChoiceParamDescriptor *direction = desc.defineChoiceParam(kDirectionParamName);
    direction->setLabels(kDirectionParamLabel, kDirectionParamLabel, kDirectionParamLabel);
    direction->setHint(kDirectionParamHint);
    direction->appendOption(kDirectionParamChoiceForward);
    direction->appendOption(kDirectionParamChoiceInverse);
    page->addChild(*direction);

    ChoiceParamDescriptor *interpolation = desc.defineChoiceParam(kInterpolationParamName);
    interpolation->setLabels(kInterpolationParamLabel, kInterpolationParamLabel, kInterpolationParamLabel);
    interpolation->setHint(kInterpolationParamHint);
    interpolation->appendOption(kInterpolationParamChoiceNearest);
    interpolation->appendOption(kInterpolationParamChoiceLinear);
    interpolation->appendOption(kInterpolationParamChoiceTetrahedral);
    interpolation->appendOption(kInterpolationParamChoiceBest);
    interpolation->setDefault(1);
    page->addChild(*interpolation);

    ofxsMaskMixDescribeParams(desc, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIOFileTransformPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OCIOFileTransformPlugin(handle);
}


void getOCIOFileTransformPluginID(OFX::PluginFactoryArray &ids)
{
    static OCIOFileTransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
