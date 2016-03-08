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
 * OCIODisplay plugin.
 * Use OpenColorIO to convert for a display device.
 */

#ifdef OFX_IO_USING_OCIO
//#include <iostream>
#include <memory>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "IOUtility.h"
#include "GenericOCIO.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "OCIODisplayOFX"
#define kPluginGrouping "Color/OCIO"
#define kPluginDescription "Uses the OpenColorIO library to apply a colorspace conversion to an image sequence, so that it can be accurately represented on a specific display device."
#define kPluginIdentifier "fr.inria.openfx.OCIODisplay"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamDisplay "display"
#define kParamDisplayChoice "displayIndex"
#define kParamDisplayLabel "Display Device"
#define kParamDisplayHint "Specifies the display device that will be used to view the sequence."

#define kParamView "view"
#define kParamViewChoice "viewIndex"
#define kParamViewLabel "View Transform"
#define kParamViewHint "Specifies the display transform to apply to the scene or image."

#define kParamGain "gain"
#define kParamGainLabel "Gain"
#define kParamGainHint "Exposure adjustment, in scene-linear, prior to the display transform."

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "Gamma correction applied after the display transform."

#define kParamChannelSelector "channelSelector"
#define kParamChannelSelectorLabel "Channel View"
#define kParamChannelSelectorHint "Specify which channels to view (prior to the display transform)."
#define kParamChannelSelectorOptionRGB "RGB"
#define kParamChannelSelectorOptionRGBHint "Color."
#define kParamChannelSelectorOptionR "R"
#define kParamChannelSelectorOptionRHint "Red."
#define kParamChannelSelectorOptionG "G"
#define kParamChannelSelectorOptionGHint "Green."
#define kParamChannelSelectorOptionB "B"
#define kParamChannelSelectorOptionBHint "Blue."
#define kParamChannelSelectorOptionA "A"
#define kParamChannelSelectorOptionAHint "Alpha."
#define kParamChannelSelectorOptionLuminance "Luminance"
#define kParamChannelSelectorOptionLuminanceHint "Luma"
#define kParamChannelSelectorOptionMatteOverlay "Matte overlay"
#define kParamChannelSelectorOptionMatteOverlayHint "Channel overlay mode. Do RGB, and then swizzle later."
enum ChannelSelectorEnum {
    eChannelSelectorRGB,
    eChannelSelectorR,
    eChannelSelectorG,
    eChannelSelectorB,
    eChannelSelectorA,
    eChannelSelectorLuminance,
    //eChannelSelectorMatteOverlay,
};

namespace OCIO = OCIO_NAMESPACE;

static bool gHostIsNatron   = false;

// ChoiceParamType may be OFX::ChoiceParamDescriptor or OFX::ChoiceParam
template <typename ChoiceParamType>
static void
buildDisplayMenu(OCIO::ConstConfigRcPtr config,
                 ChoiceParamType* choice)
{
    choice->resetOptions();
    if (!config) {
        return;
    }
    std::string defaultDisplay = config->getDefaultDisplay();
    for (int i = 0; i < config->getNumDisplays(); ++i) {
        std::string display = config->getDisplay(i);
        choice->appendOption(display);
        if (display == defaultDisplay) {
            choice->setDefault(i);
        }
    }
}

// ChoiceParamType may be OFX::ChoiceParamDescriptor or OFX::ChoiceParam
template <typename ChoiceParamType>
static void
buildViewMenu(OCIO::ConstConfigRcPtr config,
              ChoiceParamType* choice,
              const char* display)
{
    choice->resetOptions();
    if (!config) {
        return;
    }
    for (int i = 0; i < config->getNumViews(display); ++i) {
        std::string view = config->getView(display, i);
        choice->appendOption(view);
    }
}

class OCIODisplayPlugin : public OFX::ImageEffect
{
public:

    OCIODisplayPlugin(OfxImageEffectHandle handle);

    virtual ~OCIODisplayPlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;


    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, OFX::Clip * &/*identityClip*/, double &/*identityTime*/) OVERRIDE FINAL
    {
        // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
        clearPersistentMessage();
        return false;
    }

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* override changed clip */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // override the rod call
    //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
    void displayCheck(double time);
    void viewCheck(double time, bool setDefaultIfInvalid = false);

    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);

    void copyPixelData(bool unpremult,
                       bool premult,
                       int premultChannel,
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
                      premultChannel,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       int premultChannel,
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
                      premultChannel,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       int premultChannel,
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
                      premultChannel,
                      time,
                      renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(bool unpremult,
                       bool premult,
                       int premultChannel,
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

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::StringParam* _display;
    OFX::StringParam* _view;
    OFX::ChoiceParam* _displayChoice;
    OFX::ChoiceParam* _viewChoice;
    OFX::DoubleParam* _gain;
    OFX::DoubleParam* _gamma;
    OFX::ChoiceParam* _channel;

    std::auto_ptr<GenericOCIO> _ocio;

    OFX::MultiThread::Mutex _procMutex;
    OCIO_NAMESPACE::ConstProcessorRcPtr _proc;
    std::string _procInputSpace;
    ChannelSelectorEnum _procChannel;
    std::string _procDisplay;
    std::string _procView;
    double _procGain;
    double _procGamma;
};

OCIODisplayPlugin::OCIODisplayPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _dstClip(0)
, _srcClip(0)
, _display(0)
, _view(0)
, _displayChoice(0)
, _viewChoice(0)
, _gain(0)
, _gamma(0)
, _channel(0)
, _ocio(new GenericOCIO(this))
, _procChannel(eChannelSelectorRGB)
, _procGain(-1)
, _procGamma(-1)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                        _dstClip->getPixelComponents() == OFX::ePixelComponentRGB));
    _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
           (_srcClip && (_srcClip->getPixelComponents() == OFX::ePixelComponentRGBA ||
                         _srcClip->getPixelComponents() == OFX::ePixelComponentRGB)));
    _premult = fetchBooleanParam(kParamPremult);
    _premultChannel = fetchChoiceParam(kParamPremultChannel);
    assert(_premult && _premultChannel);
    _display = fetchStringParam(kParamDisplay);
    _view = fetchStringParam(kParamView);

    _gain = fetchDoubleParam(kParamGain);
    _gamma = fetchDoubleParam(kParamGamma);
    _channel = fetchChoiceParam(kParamChannelSelector);
    assert(_display && _view && _gain && _gamma && _channel);
    _display = fetchStringParam(kParamDisplay);
    _view = fetchStringParam(kParamView);
    if (gHostIsNatron) {
        _display->setIsSecret(true);
        _view->setIsSecret(true);
        _displayChoice = fetchChoiceParam(kParamDisplayChoice);
        _viewChoice = fetchChoiceParam(kParamViewChoice);
        // the choice menu can only be modified in Natron
        // Natron supports changing the entries in a choiceparam
        // Nuke (at least up to 8.0v3) does not
        OCIO::ConstConfigRcPtr config = _ocio->getConfig();
        buildDisplayMenu(config, _displayChoice);
        std::string display;
        _display->getValue(display);
        buildViewMenu(config, _viewChoice, display.c_str());
    }
    displayCheck(0.);
    viewCheck(0.);
}

OCIODisplayPlugin::~OCIODisplayPlugin()
{
}


// sets the correct choice menu item from the display string value
void
OCIODisplayPlugin::displayCheck(double time)
{
    if (!_displayChoice) {
        return;
    }
    OCIO::ConstConfigRcPtr config = _ocio->getConfig();
    if (!config) {
        return;
    }
    std::string displayName;
    _display->getValueAtTime(time, displayName);
    int displayIndex = -1;
    for(int i = 0; i < config->getNumDisplays(); ++i) {
        if (displayName == config->getDisplay(i)) {
            displayIndex = i;
        }
    }
    if (displayIndex >= 0) {
        int displayIndexOld;
        _displayChoice->getValueAtTime(time, displayIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (displayIndexOld != displayIndex) {
            _displayChoice->setValue(displayIndex);
        }
        _display->setEnabled(false);
        _display->setIsSecret(true);
        _displayChoice->setEnabled(true);
        _displayChoice->setIsSecret(false);
    } else {
        // the input space name is not valid
        _display->setEnabled(true);
        _display->setIsSecret(false);
        _displayChoice->setEnabled(false);
        _displayChoice->setIsSecret(true);
    }
}

// sets the correct choice menu item from the view string value
void
OCIODisplayPlugin::viewCheck(double time, bool setDefaultIfInvalid)
{
    if (!_viewChoice) {
        return;
    }
    OCIO::ConstConfigRcPtr config = _ocio->getConfig();
    if (!config) {
        return;
    }
    std::string displayName;
    _display->getValueAtTime(time, displayName);
    std::string viewName;
    _view->getValueAtTime(time, viewName);
    int numViews = config->getNumViews(displayName.c_str());
    int viewIndex = -1;
    for(int i = 0; i < numViews; ++i) {
        if (viewName == config->getView(displayName.c_str(), i)) {
            viewIndex = i;
        }
    }
    if (viewIndex >= 0) {
        int viewIndexOld;
        _viewChoice->getValueAtTime(time, viewIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (viewIndexOld != viewIndex) {
            _viewChoice->setValue(viewIndex);
        }
        _view->setEnabled(false);
        _view->setIsSecret(true);
        _viewChoice->setEnabled(true);
        _viewChoice->setIsSecret(false);
    } else {
        // the view name is not valid
        if (setDefaultIfInvalid) {
            _view->setValue(config->getDefaultView(displayName.c_str()));
        } else {
            _view->setEnabled(true);
            _view->setIsSecret(false);
            _viewChoice->setEnabled(false);
            _viewChoice->setIsSecret(true);
        }
    }
}

/* set up and run a copy processor */
void
OCIODisplayPlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
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

    // set the images
    assert(dstPixelData && srcPixelData);
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, 0);

    // set the render window
    processor.setRenderWindow(renderWindow);

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    processor.setPremultMaskMix(premult, premultChannel, 1.);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

void
OCIODisplayPlugin::copyPixelData(bool unpremult,
                                 bool premult,
                                 int premultChannel,
                                 double time,
                                 const OfxRectI& renderWindow,
                                 const void *srcPixelData,
                                 const OfxRectI& srcBounds,
                                 OFX::PixelComponentEnum srcPixelComponents,
                                 int srcPixelComponentCount,
                                 OFX::BitDepthEnum srcBitDepth,
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
    if (((!unpremult && !premult) || (unpremult && premult))) {
        copyPixels(*this, renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else if (unpremult && !premult) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
            fred.setPremultMaskMix(true, premultChannel, 1.);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierUnPremult<float, 3, 1, float, 3, 1> fred(*this);
            fred.setPremultMaskMix(true, premultChannel, 1.);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierUnPremult<float, 1, 1, float, 1, 1> fred(*this);
            fred.setPremultMaskMix(true, premultChannel, 1.);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch

    } else {
        // not handled: (should never happen in OCIODisplay)
        // !unpremult && premult
        assert(false); // should never happen
    }
}

void
OCIODisplayPlugin::apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes)
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
    processor.setDstImg(pixelData, bounds, pixelComponents, pixelComponentCount, OFX::eBitDepthFloat, rowBytes);

    std::string inputSpace;
    _ocio->getInputColorspaceAtTime(time, inputSpace);
    ChannelSelectorEnum channel = (ChannelSelectorEnum)_channel->getValueAtTime(time);
    std::string display;
    _display->getValueAtTime(time, display);
    std::string view;
    _view->getValueAtTime(time, view);
    double gain = _gain->getValueAtTime(time);
    double gamma = _gamma->getValueAtTime(time);

    try {
        OCIO::ConstConfigRcPtr config = _ocio->getConfig();
        assert(config);
        OFX::MultiThread::AutoMutex guard(_procMutex);
        if (!_proc ||
            _procInputSpace != inputSpace ||
            _procChannel != channel ||
            _procDisplay != display ||
            _procView != view ||
            _procGain != gain ||
            _procGamma != gamma) {

            OCIO::DisplayTransformRcPtr transform = OCIO::DisplayTransform::Create();
            transform->setInputColorSpaceName(inputSpace.c_str());

            transform->setDisplay(display.c_str());

            transform->setView(view.c_str());

            // Specify an (optional) linear color correction
            {
                float m44[16];
                float offset4[4];
                const float slope4f[] = { gain, gain, gain, gain };
                OCIO::MatrixTransform::Scale(m44, offset4, slope4f);

                OCIO::MatrixTransformRcPtr mtx =  OCIO::MatrixTransform::Create();
                mtx->setValue(m44, offset4);

                transform->setLinearCC(mtx);
            }

            // Specify an (optional) post-display transform.
            {
                float exponent = 1.0f/std::max(1e-6f, (float)gamma);
                const float exponent4f[] = { exponent, exponent, exponent, exponent };
                OCIO::ExponentTransformRcPtr cc =  OCIO::ExponentTransform::Create();
                cc->setValue(exponent4f);
                transform->setDisplayCC(cc);
            }

            // Add Channel swizzling
            {
                int channelHot[4] = { 0, 0, 0, 0};

                switch(channel)
                {
                    case eChannelSelectorLuminance: // Luma
                        channelHot[0] = 1;
                        channelHot[1] = 1;
                        channelHot[2] = 1;
                        break;
                        //case eChannelSelectorMatteOverlay: //  Channel overlay mode. Do rgb, and then swizzle later
                        //    channelHot[0] = 1;
                        //    channelHot[1] = 1;
                        //    channelHot[2] = 1;
                        //    channelHot[3] = 1;
                        //    break;
                    case eChannelSelectorRGB: // RGB
                        channelHot[0] = 1;
                        channelHot[1] = 1;
                        channelHot[2] = 1;
                        channelHot[3] = 1;
                        break;
                    case eChannelSelectorR: // R
                        channelHot[0] = 1;
                        break;
                    case eChannelSelectorG: // G
                        channelHot[1] = 1;
                        break;
                    case eChannelSelectorB: // B
                        channelHot[2] = 1;
                        break;
                    case eChannelSelectorA: // A
                        channelHot[3] = 1;
                        break;
                    default:
                        break;
                }

                float lumacoef[3];
                config->getDefaultLumaCoefs(lumacoef);
                float m44[16];
                float offset[4];
                OCIO::MatrixTransform::View(m44, offset, channelHot, lumacoef);
                OCIO::MatrixTransformRcPtr swizzle = OCIO::MatrixTransform::Create();
                swizzle->setValue(m44, offset);
                transform->setChannelView(swizzle);
            }
            
            OCIO::ConstContextRcPtr context = _ocio->getLocalContext(time);
            _proc = config->getProcessor(context, transform, OCIO::TRANSFORM_DIR_FORWARD);
            _procInputSpace = inputSpace;
            _procChannel = channel;
            _procDisplay = display;
            _procView = view;
            _procGain = gain;
            _procGamma = gamma;
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

    // Hack to emulate Channel overlay mode
    //if (channel == eChannelSelectorMatteOverlay) {
    //    for (int i = 0; i < rowWidth; ++i) {
    //        rOut[i] = rOut[i] + (1.0f - rOut[i]) * (0.5f * aOut[i]);
    //        gOut[i] = gOut[i] - gOut[i] * (0.5f * aOut[i]);
    //        bOut[i] = bOut[i] - bOut[i] * (0.5f * aOut[i]);
    //    }
    //}
}

/* Override the render */
void
OCIODisplayPlugin::render(const OFX::RenderArguments &args)
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
    int premultChannel;
    _premultChannel->getValueAtTime(args.time, premultChannel);

    // copy renderWindow to the temporary image
    copyPixelData(premult, false, premultChannel, args.time, args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);

    ///do the color-space conversion
    apply(args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, tmpRowBytes);

    // copy the color-converted window and apply masking
    copyPixelData(false, premult, premultChannel, args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes, dstImg.get());
}

void
OCIODisplayPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    OCIO::ConstConfigRcPtr config = _ocio->getConfig();

    if (!config) {
        return _ocio->changedParam(args, paramName);
    }
    // the other parameters assume there is a valid config
    if (paramName == kParamDisplay) {
        assert(_display);
        displayCheck(args.time);
        if (_viewChoice) {
            std::string display;
            _display->getValue(display);
            buildViewMenu(config, _viewChoice, display.c_str());
            viewCheck(args.time, true);
        }
    } else if ( paramName == kParamDisplayChoice && args.reason == OFX::eChangeUserEdit) {
        assert(_display);
        int displayIndex;
        _displayChoice->getValue(displayIndex);
        std::string displayOld;
        _display->getValue(displayOld);
        assert(0 <= displayIndex && displayIndex < config->getNumDisplays());
        std::string display = config->getDisplay(displayIndex);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (display != displayOld) {
            _display->setValue(display);
        }
    } else if (paramName == kParamView) {
        assert(_view);
        viewCheck(args.time);
    } else if ( paramName == kParamViewChoice && args.reason == OFX::eChangeUserEdit) {
        assert(_view);
        std::string display;
        _display->getValue(display);
        int viewIndex;
        _viewChoice->getValueAtTime(args.time, viewIndex);
        std::string viewOld;
        _view->getValueAtTime(args.time, viewOld);
        assert(0 <= viewIndex && viewIndex < config->getNumViews(display.c_str()));
        std::string view = config->getView(display.c_str(), viewIndex);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (view != viewOld) {
            _view->setValue(view);
        }
    } else {
        return _ocio->changedParam(args, paramName);
    }
}

void
OCIODisplayPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
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


mDeclarePluginFactory(OCIODisplayPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OCIODisplayPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
void OCIODisplayPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
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

    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, OCIO_NAMESPACE::ROLE_REFERENCE);
    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
    const char * display = config ? config->getDefaultDisplay() : NULL;
    const char * view = display ? config->getDefaultView(display) : NULL;

    // display device
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamDisplay);
        param->setLabel(kParamDisplayLabel);
        param->setHint(kParamDisplayHint);
        param->setAnimates(false);
        if (display) {
            param->setDefault(display);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    if (gHostIsNatron) {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDisplayChoice);
        param->setLabel(kParamDisplayLabel);
        param->setHint(kParamDisplayHint);
        if (config) {
            buildDisplayMenu(config, param);
        } else {
            param->setEnabled(false);
        }
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // view transform
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamView);
        param->setLabel(kParamViewLabel);
        param->setHint(kParamViewHint);
        if (display) {
            param->setDefault(view);
        }
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    if (gHostIsNatron) {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamViewChoice);
        param->setLabel(kParamViewLabel);
        param->setHint(kParamViewHint);
        if (config) {
            buildViewMenu(config, param, config->getDefaultDisplay());
        } else {
            param->setEnabled(false);
        }
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // gain
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGain);
        param->setLabel(kParamGainLabel);
        param->setHint(kParamGainHint);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(1./64., 64.);
        param->setDefault(1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // gamma
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGamma);
        param->setLabel(kParamGammaLabel);
        param->setHint(kParamGammaHint);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0., 4.);
        param->setDefault(1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // channel view
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamChannelSelector);
        param->setLabel(kParamChannelSelectorLabel);
        param->setHint(kParamChannelSelectorHint);
        assert(param->getNOptions() == eChannelSelectorRGB);
        param->appendOption(kParamChannelSelectorOptionRGB, kParamChannelSelectorOptionRGBHint);
        assert(param->getNOptions() == eChannelSelectorR);
        param->appendOption(kParamChannelSelectorOptionR, kParamChannelSelectorOptionRHint);
        assert(param->getNOptions() == eChannelSelectorG);
        param->appendOption(kParamChannelSelectorOptionG, kParamChannelSelectorOptionGHint);
        assert(param->getNOptions() == eChannelSelectorB);
        param->appendOption(kParamChannelSelectorOptionB, kParamChannelSelectorOptionBHint);
        assert(param->getNOptions() == eChannelSelectorA);
        param->appendOption(kParamChannelSelectorOptionA, kParamChannelSelectorOptionAHint);
        assert(param->getNOptions() == eChannelSelectorLuminance);
        param->appendOption(kParamChannelSelectorOptionLuminance, kParamChannelSelectorOptionLuminanceHint);
        //assert(param->getNOptions() == eChannelSelectorMatteOverlay);
        //param->appendOption(kParamChannelSelectorOptionMatteOverlay, kParamChannelSelectorOptionMatteOverlayHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    GenericOCIO::describeInContextContext(desc, context, page);
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kOCIOHelpDisplaysButton);
        param->setLabel(kOCIOHelpButtonLabel);
        param->setHint(kOCIOHelpButtonHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIODisplayPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OCIODisplayPlugin(handle);
}


static OCIODisplayPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // OFX_IO_USING_OCIO
