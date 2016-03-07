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
 * OCIOLookTransform plugin.
 * Apply a "look".
 */

#ifdef OFX_IO_USING_OCIO

//#include <iostream>
#include <memory>

#include <GenericOCIO.h>

#include <ofxsProcessing.H>
#include <ofxsCopier.h>
#include "ofxsCoords.h"
#include <ofxsMacros.h>
#include <ofxNatron.h>

#include "IOUtility.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "OCIOLookTransformOFX"
#define kPluginGrouping "Color/OCIO"
#define kPluginDescription \
"OpenColorIO LookTransform\n\n" \
"A 'look' is a named color transform, intended to modify the look of an " \
"image in a 'creative' manner (as opposed to a colorspace definion which " \
"tends to be technically/mathematically defined).\n\n" \
"Examples of looks may be a neutral grade, to be applied to film scans " \
"prior to VFX work, or a per-shot DI grade decided on by the director, " \
"to be applied just before the viewing transform.\n\n" \
"OCIOLooks must be predefined in the OpenColorIO configuration before usage, " \
"and often reference per-shot/sequence LUTs/CCs.\n\n" \
"See the \'Look Combination\' parameter for further syntax details.\n\n" \
"See opencolorio.org for look configuration customization examples."

#define kPluginIdentifier "fr.inria.openfx.OCIOLookTransform"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamLookChoice "lookChoice"
#define kParamLookChoiceLabel "Look"
#define kParamLookChoiceHint "Look to apply (if \"Single Look\" is checked) or append to the Look Combination (when the \"Append\" button is pressed)."

#define kParamLookAppend "append"
#define kParamLookAppendLabel "Append Look to Combination"
#define kParamLookAppendHint "Append the selected Look to the Look Combination"

#define kParamSingleLook "singleLook"
#define kParamSingleLookLabel "Single Look"
#define kParamSingleLookHint "When checked, only the selected Look is applied. When not checked, the Look Combination is applied."

#define kParamLookCombination "lookCombination"
#define kParamLookCombinationLabel "Look Combination"
#define kParamLookCombinationHint \
"Specify the look(s) to apply.\n" \
"This may be empty, the name of a single look, or a combination of looks using the 'look syntax'.\n" \
"If it is empty, no look is applied.\n" \
"Look Syntax:\n" \
"Multiple looks are combined with commas: 'firstlook, secondlook'\n" \
"Direction is specified with +/- prefixes: '+firstlook, -secondlook'\n" \
"Missing look 'fallbacks' specified with |: 'firstlook, -secondlook | -secondlook'"

#define kParamDirection "direction"
#define kParamDirectionLabel "Direction"
#define kParamDirectionHint "Transform direction."
#define kParamDirectionOptionForward "Forward"
#define kParamDirectionOptionInverse "Inverse"

namespace OCIO = OCIO_NAMESPACE;

static bool gHostIsNatron   = false;

// ChoiceParamType may be OFX::ChoiceParamDescriptor or OFX::ChoiceParam
template <typename ChoiceParamType>
static void
buildLookChoiceMenu(OCIO::ConstConfigRcPtr config,
                ChoiceParamType* choice)
{
    choice->resetOptions();
    if (!config) {
        return;
    }
    for (int i = 0; i < config->getNumLooks(); ++i) {
        choice->appendOption(config->getLookNameByIndex(i));
    }
}

class OCIOLookTransformPlugin : public OFX::ImageEffect
{
public:

    OCIOLookTransformPlugin(OfxImageEffectHandle handle);

    virtual ~OCIOLookTransformPlugin();

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
        int srcPixelComponentCount = srcImg->getPixelComponentCount();
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        int dstPixelComponentCount = dstImg->getPixelComponentCount();
        copyPixelData(unpremult,
                      premult,
                      maskmix,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
                       double time,
                       const OfxRectI &renderWindow,
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
        copyPixelData(unpremult,
                      premult,
                      maskmix,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
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
        copyPixelData(unpremult,
                      premult,
                      maskmix,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
                       double time,
                       const OfxRectI &renderWindow,
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

    void setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                      double time,
                      const OfxRectI &renderWindow,
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
                      OFX::BitDepthEnum dstPixelDepth,
                      int dstRowBytes);

    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;

    OFX::ChoiceParam* _lookChoice;
    OFX::PushButtonParam* _lookAppend;
    OFX::BooleanParam* _singleLook;
    OFX::StringParam* _lookCombination;

    OFX::ChoiceParam *_direction;

    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;

    std::auto_ptr<GenericOCIO> _ocio;

    OFX::MultiThread::Mutex _procMutex;
    OCIO_NAMESPACE::ConstProcessorRcPtr _proc;
    std::string _procLook;
    std::string _procInputSpace;
    std::string _procOutputSpace;
    int _procDirection;
};

OCIOLookTransformPlugin::OCIOLookTransformPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _dstClip(0)
, _srcClip(0)
, _maskClip(0)
, _ocio(new GenericOCIO(this))
, _procDirection(-1)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                        _dstClip->getPixelComponents() == OFX::ePixelComponentRGB));
    _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
           (_srcClip && (_srcClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                         _srcClip->getPixelComponents() == OFX::ePixelComponentRGB)));
    _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!_maskClip || _maskClip->getPixelComponents() == OFX::ePixelComponentAlpha);
    _lookChoice = fetchChoiceParam(kParamLookChoice);
    _lookAppend = fetchPushButtonParam(kParamLookAppend);
    _singleLook = fetchBooleanParam(kParamSingleLook);
    _lookCombination = fetchStringParam(kParamLookCombination);
    assert(_lookChoice && _lookAppend && _singleLook && _lookCombination);
    _direction = fetchChoiceParam(kParamDirection);
    assert(_direction);
    _premult = fetchBooleanParam(kParamPremult);
    _premultChannel = fetchChoiceParam(kParamPremultChannel);
    assert(_premult && _premultChannel);
    _mix = fetchDoubleParam(kParamMix);
    _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);

    bool singleLook = _singleLook->getValue();
    _lookChoice->setEvaluateOnChange(singleLook);
    _lookCombination->setEnabled(!singleLook);
    _lookCombination->setEvaluateOnChange(!singleLook);

    OCIO::ConstConfigRcPtr config = _ocio->getConfig();
    if (!config) {
        // secret should not be set on the descriptor, unless the parameter should *always* be secret
        _lookChoice->setIsSecret(true);
        _lookAppend->setIsSecret(true);
        _singleLook->setIsSecret(true);
    } else if (!_ocio->configIsDefault()) {
        if (gHostIsNatron) {
            // the choice menu can only be modified in Natron
            // Natron supports changing the entries in a choiceparam
            // Nuke (at least up to 8.0v3) does not
            OCIO::ConstConfigRcPtr config = _ocio->getConfig();
            buildLookChoiceMenu(config, _lookChoice);
        } else {
            _lookChoice->setEnabled(false);
            _lookChoice->setIsSecret(true);
            _lookAppend->setEnabled(false);
            _lookAppend->setIsSecret(true);
            _singleLook->setValue(true);
            _singleLook->setIsSecret(true);
            _singleLook->setEnabled(false);
        }
    }

}

OCIOLookTransformPlugin::~OCIOLookTransformPlugin()
{
}

/* set up and run a copy processor */
void
OCIOLookTransformPlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                                      double time,
                                      const OfxRectI &renderWindow,
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
                                      OFX::BitDepthEnum dstPixelDepth,
                                      int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);

    // make sure bit depths are sane
    if(srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    std::auto_ptr<const OFX::Image> orig((_srcClip && _srcClip->isConnected()) ?
                                         _srcClip->fetchImage(time) : 0);

    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if (doMasking) {
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    if (!orig.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg(orig.get());
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, 0);

    // set the render window
    processor.setRenderWindow(renderWindow);

    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);
    processor.setPremultMaskMix(premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

void
OCIOLookTransformPlugin::copyPixelData(bool unpremult,
                                       bool premult,
                                       bool maskmix,
                                       double time,
                                       const OfxRectI& renderWindow,
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
                                       int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    if (!unpremult && !premult && !maskmix) {
        copyPixels(*this, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else if (unpremult && !premult && !maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierUnPremult<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierUnPremult<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch

    } else if (!unpremult && !premult && maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierMaskMix<float, 4, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierMaskMix<float, 3, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierMaskMix<float, 1, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch

    } else if (!unpremult && premult && maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierPremultMaskMix<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierPremultMaskMix<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierPremultMaskMix<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch

    } else {
        assert(false); // should never happen
    }
}

void
OCIOLookTransformPlugin::apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes)
{
    // are we in the image bounds
    if(renderWindow.x1 < bounds.x1 || renderWindow.x1 >= bounds.x2 || renderWindow.y1 < bounds.y1 || renderWindow.y1 >= bounds.y2 ||
       renderWindow.x2 <= bounds.x1 || renderWindow.x2 > bounds.x2 || renderWindow.y2 <= bounds.y1 || renderWindow.y2 > bounds.y2) {
        throw std::runtime_error("OCIO: render window outside of image bounds");
    }
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB) {
        throw std::runtime_error("OCIO: invalid components (only RGB and RGBA are supported)");
    }

    OCIO::ConstConfigRcPtr config = _ocio->getConfig();
    assert(config);

    bool singleLook = _singleLook->getValueAtTime(time);
    std::string lookCombination;
    _lookCombination->getValueAtTime(time, lookCombination);
    if (_ocio->isIdentity(time) && !singleLook && lookCombination.empty()) {
        return; // isIdentity
    }
    std::string inputSpace;
    _ocio->getInputColorspaceAtTime(time, inputSpace);
    std::string look;
    if (singleLook) {
        int lookChoice_i = _lookChoice->getValueAtTime(time);
        look = config->getLookNameByIndex(lookChoice_i);
    } else {
        look = lookCombination;
    }
    int directioni = _direction->getValueAtTime(time);
    std::string outputSpace;
    _ocio->getOutputColorspaceAtTime(time, outputSpace);

    OCIOProcessor processor(*this);
    // set the images
    processor.setDstImg(pixelData, bounds, pixelComponents, pixelComponentCount, OFX::eBitDepthFloat, rowBytes);

    try {
        OFX::MultiThread::AutoMutex guard(_procMutex);
        if (!_proc ||
            _procLook != look ||
            _procInputSpace != inputSpace ||
            _procOutputSpace != outputSpace ||
            _procDirection != directioni) {

            OCIO::TransformDirection direction = OCIO::TRANSFORM_DIR_UNKNOWN;
            OCIO::LookTransformRcPtr transform = OCIO::LookTransform::Create();
            transform->setLooks(look.c_str());

            if (directioni == 0) {
                transform->setSrc(inputSpace.c_str());
                transform->setDst(outputSpace.c_str());
                direction = OCIO::TRANSFORM_DIR_FORWARD;
            } else {
                // The TRANSFORM_DIR_INVERSE applies an inverse for the end-to-end transform,
                // which would otherwise do dst->inv look -> src.
                // This is an unintuitive result for the artist (who would expect in, out to
                // remain unchanged), so we account for that here by flipping src/dst

                transform->setSrc(outputSpace.c_str());
                transform->setDst(inputSpace.c_str());
                direction = OCIO::TRANSFORM_DIR_INVERSE;
            }
            _proc = config->getProcessor(transform, direction);
        }
        processor.setProcessor(_proc);
    } catch (const OCIO::Exception &e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

/* Override the render */
void
OCIOLookTransformPlugin::render(const OFX::RenderArguments &args)
{
    if (!_srcClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(_srcClip);
    std::auto_ptr<const OFX::Image> srcImg(_srcClip->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (srcImg->getRenderScale().x != args.renderScale.x ||
        srcImg->getRenderScale().y != args.renderScale.y ||
        srcImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    OFX::BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcImg->getPixelComponents();

    if (!_dstClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(_dstClip);
    std::auto_ptr<OFX::Image> dstImg(_dstClip->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat || dstBitDepth != srcBitDepth) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ((dstComponents != OFX::ePixelComponentRGBA && dstComponents != OFX::ePixelComponentRGB && dstComponents != OFX::ePixelComponentAlpha) ||
        dstComponents != srcComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        return;
        //throw std::runtime_error("render window outside of image bounds");
    }

    const void* srcPixelData = NULL;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    getImageData(srcImg.get(), &srcPixelData, &bounds, &pixelComponents, &bitDepth, &srcRowBytes);
    int pixelComponentCount = srcImg->getPixelComponentCount();

    // allocate temporary image
    int pixelBytes = pixelComponentCount * getComponentBytes(srcBitDepth);
    int tmpRowBytes = (args.renderWindow.x2-args.renderWindow.x1) * pixelBytes;
    size_t memSize = (args.renderWindow.y2-args.renderWindow.y1) * tmpRowBytes;
    OFX::ImageMemory mem(memSize,this);
    float *tmpPixelData = (float*)mem.lock();

    bool premult;
    _premult->getValueAtTime(args.time, premult);

    // copy renderWindow to the temporary image
    copyPixelData(premult, false, false, args.time, args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);

    ///do the color-space conversion
    apply(args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, tmpRowBytes);

    // copy the color-converted window and apply masking
    copyPixelData(false, premult, true, args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes, dstImg.get());
}

bool
OCIOLookTransformPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();

    if (_ocio->isIdentity(args.time)) {
        bool singleLook;
        _singleLook->getValueAtTime(args.time, singleLook);
        if (!singleLook) {
            std::string lookCombination;
            _lookCombination->getValueAtTime(args.time, lookCombination);
            if (lookCombination.empty()) {
                identityClip = _srcClip;
                return true;
            }
        }
    }

    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;
        return true;
    }

    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if (!OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0)) {
                identityClip = _srcClip;
                return true;
            }
        }
    }

    return false;
}

void
OCIOLookTransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamLookAppend) {
        OCIO::ConstConfigRcPtr config = _ocio->getConfig();
        std::string lookCombination;
        int lookChoice;
        _lookCombination->getValueAtTime(args.time, lookCombination);
        _lookChoice->getValueAtTime(args.time, lookChoice);
        std::string look = config->getLookNameByIndex(lookChoice);
        if (!look.empty()) {
            if (lookCombination.empty()) {
                lookCombination = look;
            } else {
                lookCombination += ", ";
                lookCombination += look;
            }
            _lookCombination->setValue(lookCombination);
        }
    } else if (paramName == kParamSingleLook && args.reason == OFX::eChangeUserEdit) {
        bool singleLook;
        _singleLook->getValueAtTime(args.time, singleLook);
        _lookChoice->setEvaluateOnChange(singleLook);
        _lookCombination->setEnabled(!singleLook);
        _lookCombination->setEvaluateOnChange(!singleLook);
    } else {
        _ocio->changedParam(args, paramName);
    }
    // this must be done after handling by GenericOCIO (to make sure the new config is loaded)
    if (paramName == kOCIOParamConfigFile && args.reason == OFX::eChangeUserEdit) {
        if (!_ocio->configIsDefault()) {
            if (gHostIsNatron) {
                // the choice menu can only be modified in Natron
                // Natron supports changing the entries in a choiceparam
                // Nuke (at least up to 8.0v3) does not
                OCIO::ConstConfigRcPtr config = _ocio->getConfig();
                buildLookChoiceMenu(config, _lookChoice);
            } else {
                _lookChoice->setEnabled(false);
                _lookChoice->setIsSecret(true);
                _lookAppend->setEnabled(false);
                _lookAppend->setIsSecret(true);
                _singleLook->setValue(true);
                _singleLook->setIsSecret(true);
                _singleLook->setEnabled(false);
            }
        }
    }
}

void
OCIOLookTransformPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        if (_srcClip->getPixelComponents() != OFX::ePixelComponentRGBA) {
            _premult->setValue(false);
        } else switch (_srcClip->getPreMultiplication()) {
            case OFX::eImageOpaque:
                _premult->setValue(false);
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


mDeclarePluginFactory(OCIOLookTransformPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OCIOLookTransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
void OCIOLookTransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
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

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, OCIO::ROLE_REFERENCE);
    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSingleLook);
        param->setLabel(kParamSingleLookLabel);
        param->setHint(kParamSingleLookHint);
        if (config) {
            param->setDefault(true);
        } else {
            param->setDefault(false);
            param->setEnabled(false);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamLookChoice);
        param->setLabel(kParamLookChoiceLabel);
        param->setHint(kParamLookChoiceHint);
        if (config) {
            buildLookChoiceMenu(config, param);
        } else {
            param->setEnabled(false);
        }
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamLookAppend);
        param->setLabel(kParamLookAppendLabel);
        param->setHint(kParamLookAppendHint);
        if (!config) {
            param->setEnabled(false);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamLookCombination);
        param->setLabel(kParamLookCombinationLabel);
        param->setHint(kParamLookCombinationHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDirection);
        param->setLabel(kParamDirectionLabel);
        param->setHint(kParamDirectionHint);
        param->appendOption(kParamDirectionOptionForward);
        param->appendOption(kParamDirectionOptionInverse);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }
    GenericOCIO::describeInContextOutput(desc, context, page, OCIO::ROLE_REFERENCE);
    GenericOCIO::describeInContextContext(desc, context, page);
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kOCIOHelpLooksButton);
        param->setLabel(kOCIOHelpButtonLabel);
        param->setHint(kOCIOHelpButtonHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIOLookTransformPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OCIOLookTransformPlugin(handle);
}



static OCIOLookTransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // OFX_IO_USING_OCIO

