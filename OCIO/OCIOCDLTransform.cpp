/*
 OCIOCDLTransform plugin.
 Apply an ASC CDL grade.

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

#include "OCIOCDLTransform.h"
#include <cstdio>
#include <OpenColorIO/OpenColorIO.h>

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "IOUtility.h"
#include "ofxNatron.h"
#include "ofxsMacros.h"
#include "GenericOCIO.h"

namespace OCIO = OCIO_NAMESPACE;

#define kPluginName "OCIOCDLTransformOFX"
#define kPluginGrouping "Color/OCIO"
#define kPluginDescription \
"Use OpenColorIO to apply an ASC Color Decision List (CDL) grade.\n" \
"The formula applied for each channel is:\nout = (in * slope + offset)^power.\n" \
"The saturation is then applied to all channel using the standard rec709 saturation coefficients:\n" \
"luma = 0.2126 * inR + 0.7152 * inG + 0.0722 * inB\n" \
"outR = Clamp( luma + sat * (inR - luma) )\n" \
"outG = Clamp( luma + sat * (inG - luma) )\n" \
"outB = Clamp( luma + sat * (inB - luma) ).\n\n" \
"The grade can be loaded from an ASC .ccc (Color Correction Collection) or .cc (Color Correction) file."
//, and saved to a .cc file."

#define kPluginIdentifier "fr.inria.openfx:OCIOCDLTransform"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kSlopeParamName "slope"
#define kSlopeParamLabel "Slope"
#define kSlopeParamHint "ASC CDL slope"
#define kSlopeParamMin 0.
#define kSlopeParamMax 4.

#define kOffsetParamName "offset"
#define kOffsetParamLabel "Offset"
#define kOffsetParamHint "ASC CDL offset"
#define kOffsetParamMin -0.2
#define kOffsetParamMax 0.2

#define kPowerParamName "power"
#define kPowerParamLabel "Power"
#define kPowerParamHint "ASC CDL power"
#define kPowerParamMin 0.
#define kPowerParamMax 4.

#define kSaturationParamName "saturation"
#define kSaturationParamLabel "Saturation"
#define kSaturationParamHint "ASC CDL saturation"
#define kSaturationParamMin 0.
#define kSaturationParamMax 4.

#define kDirectionParamName "direction"
#define kDirectionParamLabel "Direction"
#define kDirectionParamHint "Transform direction."
#define kDirectionParamChoiceForward "Forward"
#define kDirectionParamChoiceInverse "Inverse"

#define kReadFromFileParamName "readFromFile"
#define kReadFromFileParamLabel "Read from file"
#define kReadFromFileParamHint \
"Load color correction information from the .cc or .ccc file."

#define kFileParamName "file"
#define kFileParamLabel "File"
#define kFileParamHint \
"Specify the src ASC CDL file, on disk, to use for this transform. " \
"This can be either a .cc or .ccc file. If .ccc is specified, the cccid is required."

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

#define kExportParamName "export"
#define kExportParamLabel "Export"
#define kExportParamHint "Export this grade as a ColorCorrection XML file (.cc), which can be loaded with the OCIOFileTransform, or using a FileTransform in an OCIO config. The file must not already exist."
#define kExportParamDefault "Set filename to export this grade as .cc"

static bool gHostIsNatron = false; // TODO: generate a CCCId choice param kCCCIDChoiceParamName from available IDs

class OCIOCDLTransformPlugin : public OFX::ImageEffect
{
public:

    OCIOCDLTransformPlugin(OfxImageEffectHandle handle);

    virtual ~OCIOCDLTransformPlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
    virtual void beginEdit(void) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* override changed clip */
    //virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // override the rod call
    //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
    void updateCCCId();

    void refreshKnobEnabledState(bool readFromFile);

    void loadCDLFromFile();

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

    bool firstLoad_;
    OFX::RGBParam *slope_;
    OFX::RGBParam *offset_;
    OFX::RGBParam *power_;
    OFX::DoubleParam *saturation_;
    OFX::ChoiceParam *direction_;
    OFX::BooleanParam* readFromFile_;
    OFX::StringParam *file_;
    OFX::IntParam *version_;
    OFX::StringParam *cccid_;
    OFX::StringParam *export_;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};

OCIOCDLTransformPlugin::OCIOCDLTransformPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, maskClip_(0)
, firstLoad_(true)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && (srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA || srcClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!maskClip_ || maskClip_->getPixelComponents() == OFX::ePixelComponentAlpha);
    slope_ = fetchRGBParam(kSlopeParamName);
    offset_ = fetchRGBParam(kOffsetParamName);
    power_ = fetchRGBParam(kPowerParamName);
    saturation_ = fetchDoubleParam(kSaturationParamName);
    direction_ = fetchChoiceParam(kDirectionParamName);
    readFromFile_ = fetchBooleanParam(kReadFromFileParamName);
    file_ = fetchStringParam(kFileParamName);
    version_ = fetchIntParam(kVersionParamName);
    cccid_ = fetchStringParam(kCCCIDParamName);
    export_ = fetchStringParam(kExportParamName);
    assert(slope_ && offset_ && power_ && saturation_ && direction_ && readFromFile_ && file_ && version_ && cccid_ && export_);
    _mix = fetchDoubleParam(kMixParamName);
    _maskInvert = fetchBooleanParam(kMaskInvertParamName);
    assert(_mix && _maskInvert);
    updateCCCId();
    bool readFromFile;
    readFromFile_->getValue(readFromFile);
    refreshKnobEnabledState(readFromFile);
    // WARNING: we cannot setValue() here in the constructor, because it calls changedParam() on an object which is not yet constructed.
    // CDL file loading and parameter setting is delayed until the first call th changedParam(), beginEdit(), or render()
}

OCIOCDLTransformPlugin::~OCIOCDLTransformPlugin()
{
}

/* set up and run a copy processor */
void
OCIOCDLTransformPlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
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
        processor.setMaskImg(mask.get());
    }

    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg(orig.get());
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
OCIOCDLTransformPlugin::copyPixelData(double time,
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
OCIOCDLTransformPlugin::apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
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

    if (firstLoad_) {
        firstLoad_ = false;
        bool readFromFile;
        readFromFile_->getValue(readFromFile);
        if (readFromFile) {
            loadCDLFromFile();
        }
    }

    float sop[9];
    double saturation;
    double r, g, b;
    slope_->getValueAtTime(time, r, g, b);
    sop[0] = r;
    sop[1] = g;
    sop[2] = b;
    offset_->getValueAtTime(time, r, g, b);
    sop[3] = r;
    sop[4] = g;
    sop[5] = b;
    power_->getValueAtTime(time, r, g, b);
    sop[6] = r;
    sop[7] = g;
    sop[8] = b;
    saturation_->getValueAtTime(time, saturation);
    int direction_i;
    direction_->getValueAtTime(time, direction_i);
    std::string file;
    file_->getValueAtTime(time, file);
    std::string cccid;
    cccid_->getValueAtTime(time, cccid);

    try {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        assert(config);
        OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
        cc->setSOP(sop);
        cc->setSat(saturation);

        if (direction_i == 0) {
            cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        } else {
            cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        }

        processor.setValues(config, cc);
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
OCIOCDLTransformPlugin::render(const OFX::RenderArguments &args)
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
OCIOCDLTransformPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    const double time = args.time;
    float sop[9];
    double saturation;
    double r, g, b;
    slope_->getValueAtTime(time, r, g, b);
    sop[0] = r;
    sop[1] = g;
    sop[2] = b;
    offset_->getValueAtTime(time, r, g, b);
    sop[3] = r;
    sop[4] = g;
    sop[5] = b;
    power_->getValueAtTime(time, r, g, b);
    sop[6] = r;
    sop[7] = g;
    sop[8] = b;
    saturation_->getValueAtTime(time, saturation);
    int direction_i;
    direction_->getValueAtTime(time, direction_i);
    std::string file;
    file_->getValueAtTime(time, file);
    std::string cccid;
    cccid_->getValueAtTime(time, cccid);

    try {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        assert(config);
        OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
        cc->setSOP(sop);
        cc->setSat(saturation);

        if (direction_i == 0) {
            cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        } else {
            cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        }

        OCIO::ConstProcessorRcPtr proc = config->getProcessor(cc);
        if (proc->isNoOp()) {
            identityClip = srcClip_;
            return true;
        }
    } catch (const OCIO::Exception &e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    return false;
}

void
OCIOCDLTransformPlugin::updateCCCId()
{
    // Convoluted equiv to pysting::endswith(m_file, ".ccc")
    // TODO: Could this be queried from the processor?
    std::string srcstring;
    file_->getValue(srcstring);
    const std::string cccext = ".ccc";
    if(std::equal(cccext.rbegin(), cccext.rend(), srcstring.rbegin())) {
        cccid_->setIsSecret(false);
    } else {
        cccid_->setIsSecret(true);
    }
}

void
OCIOCDLTransformPlugin::refreshKnobEnabledState(bool readFromFile)
{
    if (readFromFile) {
        slope_->setEnabled(false);
        offset_->setEnabled(false);
        power_->setEnabled(false);
        saturation_->setEnabled(false);

        // We leave these active to allow knob re-use with the import/export buttons
        //m_fileKnob->enable();
        //m_cccidKnob->enable();

        loadCDLFromFile();
    } else {
        slope_->setEnabled(true);
        offset_->setEnabled(true);
        power_->setEnabled(true);
        saturation_->setEnabled(true);

        // We leave these active to allow knob re-use with the import/export buttons
        //m_fileKnob->disable();
        //m_cccidKnob->disable();
    }
}

void
OCIOCDLTransformPlugin::loadCDLFromFile()
{
    OCIO::CDLTransformRcPtr transform;

    try {
        // This is inexpensive to call multiple times, as OCIO caches results
        // internally.
        std::string file;
        file_->getValue(file);
        std::string cccid;
        cccid_->getValue(cccid);
        transform = OCIO::CDLTransform::CreateFromFile(file.c_str(), cccid.c_str());
    } catch (const OCIO::Exception &e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }


    float sop[9];
    transform->getSOP(sop);

    slope_->deleteAllKeys();
    slope_->setValue(sop[0], sop[1], sop[2]);
    offset_->deleteAllKeys();
    offset_->setValue(sop[3], sop[4], sop[5]);
    power_->deleteAllKeys();
    power_->setValue(sop[6], sop[7], sop[8]);
    saturation_->deleteAllKeys();
    saturation_->setValue(transform->getSat());
}

void
OCIOCDLTransformPlugin::beginEdit()
{
    if (firstLoad_) {
        firstLoad_ = false;
        bool readFromFile;
        readFromFile_->getValue(readFromFile);
        if (readFromFile) {
            loadCDLFromFile();
        }
    }
}

void
OCIOCDLTransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    clearPersistentMessage();

    if (firstLoad_ || paramName == kReadFromFileParamName || paramName == kFileParamName || paramName == kCCCIDParamName) {
        firstLoad_ = false;
        bool readFromFile;
        readFromFile_->getValue(readFromFile);
        refreshKnobEnabledState(readFromFile);
        if (readFromFile) {
            loadCDLFromFile();
        }
    }

    // Only show the cccid knob when loading a .cc/.ccc file. Set
    // hidden state when the src is changed, or the node properties
    // are shown
    if (paramName == kFileParamName) {
        updateCCCId();
    } else if (paramName == kReloadParamName) {
        version_->setValue(version_->getValue()+1); // invalidate the node cache
        OCIO::ClearAllCaches();
    } else if (paramName == kExportParamName && args.reason == OFX::eChangeUserEdit) {
        std::string exportName;
        export_->getValueAtTime(args.time, exportName);
        // if file already exists, don't overwrite it
        if (exportName.empty()) {
            sendMessage(OFX::Message::eMessageError, "", "Export file name is empty, please enter a valid non-existing file name.");
        } else {
            std::FILE *file = std::fopen(exportName.c_str(), "r");
            if (file) {
                std::fclose(file);
                sendMessage(OFX::Message::eMessageError, "", std::string("File ") + exportName + " already exists, please select another filename");
            } else {
                file = std::fopen(exportName.c_str(), "w");
                if (!file) {
                    sendMessage(OFX::Message::eMessageError, "", std::string("File ") + exportName + " cannot be written");
                }
                const double time = args.time;
                float sop[9];
                double saturation;
                double r, g, b;
                slope_->getValueAtTime(time, r, g, b);
                sop[0] = r;
                sop[1] = g;
                sop[2] = b;
                offset_->getValueAtTime(time, r, g, b);
                sop[3] = r;
                sop[4] = g;
                sop[5] = b;
                power_->getValueAtTime(time, r, g, b);
                sop[6] = r;
                sop[7] = g;
                sop[8] = b;
                saturation_->getValueAtTime(time, saturation);
                int direction_i;
                direction_->getValueAtTime(time, direction_i);

                try {
                    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
                    assert(config);
                    OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
                    cc->setSOP(sop);
                    cc->setSat(saturation);

                    if (direction_i == 0) {
                        cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
                    } else {
                        cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
                    }

                    std::fputs(cc->getXML(), file);
                } catch (const OCIO::Exception &e) {
                    setPersistentMessage(OFX::Message::eMessageError, "", e.what());
                    std::fclose(file);
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                }
                std::fclose(file);
            }
        }

        // reset back to default
        export_->setValue(kExportParamDefault);
    }
}

using namespace OFX;

mDeclarePluginFactory(OCIOCDLTransformPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OCIOCDLTransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
void OCIOCDLTransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);
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

    // ASC CDL grade numbers
    RGBParamDescriptor *slope = desc.defineRGBParam(kSlopeParamName);
    slope->setLabels(kSlopeParamLabel, kSlopeParamLabel, kSlopeParamLabel);
    slope->setHint(kSlopeParamHint);
    slope->setRange(kSlopeParamMin, kSlopeParamMin, kSlopeParamMin, kSlopeParamMax, kSlopeParamMax, kSlopeParamMax);
    slope->setDisplayRange(kSlopeParamMin, kSlopeParamMin, kSlopeParamMin, kSlopeParamMax, kSlopeParamMax, kSlopeParamMax);
    slope->setDefault(1., 1., 1.);
    page->addChild(*slope);

    RGBParamDescriptor *offset = desc.defineRGBParam(kOffsetParamName);
    offset->setLabels(kOffsetParamLabel, kOffsetParamLabel, kOffsetParamLabel);
    offset->setHint(kOffsetParamHint);
    offset->setRange(kOffsetParamMin, kOffsetParamMin, kOffsetParamMin, kOffsetParamMax, kOffsetParamMax, kOffsetParamMax);
    offset->setDisplayRange(kOffsetParamMin, kOffsetParamMin, kOffsetParamMin, kOffsetParamMax, kOffsetParamMax, kOffsetParamMax);
    offset->setDefault(0., 0., 0.);
    page->addChild(*offset);

    RGBParamDescriptor *power = desc.defineRGBParam(kPowerParamName);
    power->setLabels(kPowerParamLabel, kPowerParamLabel, kPowerParamLabel);
    power->setHint(kPowerParamHint);
    power->setRange(kPowerParamMin, kPowerParamMin, kPowerParamMin, kPowerParamMax, kPowerParamMax, kPowerParamMax);
    power->setDisplayRange(kPowerParamMin, kPowerParamMin, kPowerParamMin, kPowerParamMax, kPowerParamMax, kPowerParamMax);
    power->setDefault(1., 1., 1.);
    page->addChild(*power);

    DoubleParamDescriptor *saturation = desc.defineDoubleParam(kSaturationParamName);
    saturation->setLabels(kSaturationParamLabel, kSaturationParamLabel, kSaturationParamLabel);
    saturation->setHint(kSaturationParamHint);
    saturation->setRange(kSaturationParamMin, kSaturationParamMax);
    saturation->setDisplayRange(kSaturationParamMin, kSaturationParamMax);
    saturation->setDefault(1.);
    page->addChild(*saturation);

    ChoiceParamDescriptor *direction = desc.defineChoiceParam(kDirectionParamName);
    direction->setLabels(kDirectionParamLabel, kDirectionParamLabel, kDirectionParamLabel);
    direction->setHint(kDirectionParamHint);
    direction->appendOption(kDirectionParamChoiceForward);
    direction->appendOption(kDirectionParamChoiceInverse);
    direction->setDefault(0);
    page->addChild(*direction);

    BooleanParamDescriptor *readFromFile = desc.defineBooleanParam(kReadFromFileParamName);
    readFromFile->setLabels(kReadFromFileParamLabel, kReadFromFileParamLabel, kReadFromFileParamLabel);
    readFromFile->setHint(kReadFromFileParamHint);
    readFromFile->setAnimates(false);
    readFromFile->setDefault(false);
    page->addChild(*readFromFile);

    StringParamDescriptor *file = desc.defineStringParam(kFileParamName);
    file->setLabels(kFileParamLabel, kFileParamLabel, kFileParamLabel);
    file->setHint(kFileParamHint);
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

    StringParamDescriptor *export_ = desc.defineStringParam(kExportParamName);
    export_->setLabels(kExportParamLabel, kExportParamLabel, kExportParamLabel);
    export_->setHint(kExportParamHint);
    export_->setStringType(eStringTypeFilePath);
    export_->setFilePathExists(false); // necessary for output files
    export_->setEvaluateOnChange(false);
    export_->setIsPersistant(false);
    export_->setAnimates(false);
    export_->setDefault(kExportParamDefault);
    page->addChild(*export_);

    ofxsMaskMixDescribeParams(desc, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIOCDLTransformPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OCIOCDLTransformPlugin(handle);
}


void getOCIOCDLTransformPluginID(OFX::PluginFactoryArray &ids)
{
    static OCIOCDLTransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
