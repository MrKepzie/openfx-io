/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2013-2017 INRIA
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
#ifdef DEBUG
#include <cstdio> // printf
#endif

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "IOUtility.h"
#include "GenericOCIO.h"

using namespace OFX;
using namespace OFX::IO;

using std::string;

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
#define kParamChannelSelectorOptionAHint "Alpha."
#define kParamChannelSelectorOptionLuminance "Luminance"
#define kParamChannelSelectorOptionLuminanceHint "Luma"
#define kParamChannelSelectorOptionMatteOverlay "Matte overlay"
#define kParamChannelSelectorOptionMatteOverlayHint "Channel overlay mode. Do RGB, and then swizzle later."
enum ChannelSelectorEnum
{
    eChannelSelectorRGB,
    eChannelSelectorR,
    eChannelSelectorG,
    eChannelSelectorB,
    eChannelSelectorA,
    eChannelSelectorLuminance,
    //eChannelSelectorMatteOverlay,
};

#if defined(OFX_SUPPORTS_OPENGLRENDER)
#define kParamEnableGPU "enableGPU"
#define kParamEnableGPULabel "Enable GPU Render"
#define kParamEnableGPUHint \
    "Enable GPU-based OpenGL render.\n" \
    "If the checkbox is checked but is not enabled (i.e. it cannot be unchecked), GPU render can not be enabled or disabled from the plugin and is probably part of the host options.\n" \
    "If the checkbox is not checked and is not enabled (i.e. it cannot be checked), GPU render is not available on this host."
#endif


namespace OCIO = OCIO_NAMESPACE;

static bool gHostIsNatron   = false;

// ChoiceParamType may be ChoiceParamDescriptor or ChoiceParam
template <typename ChoiceParamType>
static void
buildDisplayMenu(OCIO::ConstConfigRcPtr config,
                 ChoiceParamType* choice)
{
    if (!config) {
        return;
    }
    string defaultDisplay = config->getDefaultDisplay();
    std::vector<string> displaysVec( config->getNumDisplays() );

    int defIndex = -1;
    for (std::size_t i = 0; i < displaysVec.size(); ++i) {
        string display = config->getDisplay(i);
        displaysVec[i] = display;
        if (display == defaultDisplay) {
            defIndex = (int)i;
        }
    }
    choice->resetOptions(displaysVec);

    if (defIndex != -1) {
        choice->setDefault(defIndex);
    }
}

// ChoiceParamType may be ChoiceParamDescriptor or ChoiceParam
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
        string view = config->getView(display, i);
        choice->appendOption(view);
    }
}

class OCIODisplayPlugin
    : public ImageEffect
{
public:

    OCIODisplayPlugin(OfxImageEffectHandle handle);

    virtual ~OCIODisplayPlugin();

private:

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;


    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            Clip * & /*identityClip*/,
                            double & /*identityTime*/) OVERRIDE FINAL
    {
        // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
        clearPersistentMessage();

        return false;
    }

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;

    /* override changed clip */
    virtual void changedClip(const InstanceChangedArgs &args, const string &clipName) OVERRIDE FINAL;

    // override the rod call
    //virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    /* The purpose of this action is to allow a plugin to set up any data it may need
       to do OpenGL rendering in an instance. */
    virtual void* contextAttached(bool createContextData) OVERRIDE FINAL;
    /* The purpose of this action is to allow a plugin to deallocate any resource
       allocated in \ref ::kOfxActionOpenGLContextAttached just before the host
       decouples a plugin from an OpenGL context. */
    virtual void contextDetached(void* contextData) OVERRIDE FINAL;

    void renderGPU(const RenderArguments &args);
#endif

    void displayCheck(double time);
    void viewCheck(double time, bool setDefaultIfInvalid = false);

    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);

    OCIO::ConstProcessorRcPtr getProcessor(OfxTime time);

    void copyPixelData(bool unpremult,
                       bool premult,
                       int premultChannel,
                       double time,
                       const OfxRectI &renderWindow,
                       const Image* srcImg,
                       Image* dstImg)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        PixelComponentEnum srcPixelComponents;
        BitDepthEnum srcBitDepth;
        int srcRowBytes;

        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        int srcPixelComponentCount = srcImg->getPixelComponentCount();
        void* dstPixelData;
        OfxRectI dstBounds;
        PixelComponentEnum dstPixelComponents;
        BitDepthEnum dstBitDepth;
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
                       PixelComponentEnum srcPixelComponents,
                       int srcPixelComponentCount,
                       BitDepthEnum srcBitDepth,
                       int srcRowBytes,
                       Image* dstImg)
    {
        void* dstPixelData;
        OfxRectI dstBounds;
        PixelComponentEnum dstPixelComponents;
        BitDepthEnum dstBitDepth;
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
                       const Image* srcImg,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       PixelComponentEnum dstPixelComponents,
                       int dstPixelComponentCount,
                       BitDepthEnum dstBitDepth,
                       int dstRowBytes)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        PixelComponentEnum srcPixelComponents;
        BitDepthEnum srcBitDepth;
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
                       PixelComponentEnum srcPixelComponents,
                       int srcPixelComponentCount,
                       BitDepthEnum srcPixelDepth,
                       int srcRowBytes,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       PixelComponentEnum dstPixelComponents,
                       int dstPixelComponentCount,
                       BitDepthEnum dstBitDepth,
                       int dstRowBytes);

    void setupAndCopy(PixelProcessorFilterBase & processor,
                      double time,
                      const OfxRectI &renderWindow,
                      const void *srcPixelData,
                      const OfxRectI& srcBounds,
                      PixelComponentEnum srcPixelComponents,
                      int srcPixelComponentCount,
                      BitDepthEnum srcPixelDepth,
                      int srcRowBytes,
                      void *dstPixelData,
                      const OfxRectI& dstBounds,
                      PixelComponentEnum dstPixelComponents,
                      int dstPixelComponentCount,
                      BitDepthEnum dstPixelDepth,
                      int dstRowBytes);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    StringParam* _display;
    StringParam* _view;
    ChoiceParam* _displayChoice;
    ChoiceParam* _viewChoice;
    DoubleParam* _gain;
    DoubleParam* _gamma;
    ChoiceParam* _channel;

    std::auto_ptr<GenericOCIO> _ocio;

    GenericOCIO::Mutex _procMutex;
    OCIO::ConstProcessorRcPtr _proc;
    string _procInputSpace;
    ChannelSelectorEnum _procChannel;
    string _procDisplay;
    string _procView;
    double _procGain;
    double _procGamma;

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    BooleanParam* _enableGPU;
    OCIOOpenGLContextData* _openGLContextData; // (OpenGL-only) - the single openGL context, in case the host does not support kNatronOfxImageEffectPropOpenGLContextData
#endif
};

OCIODisplayPlugin::OCIODisplayPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _display(0)
    , _view(0)
    , _displayChoice(0)
    , _viewChoice(0)
    , _gain(0)
    , _gamma(0)
    , _channel(0)
    , _ocio( new GenericOCIO(this) )
    , _procChannel(eChannelSelectorRGB)
    , _procGain(-1)
    , _procGamma(-1)
#if defined(OFX_SUPPORTS_OPENGLRENDER)
    , _enableGPU(0)
    , _openGLContextData(NULL)
#endif
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                         _dstClip->getPixelComponents() == ePixelComponentRGB) );
    _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert( (!_srcClip && getContext() == eContextGenerator) ||
            ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                           _srcClip->getPixelComponents() == ePixelComponentRGB) ) );
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

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    _enableGPU = fetchBooleanParam(kParamEnableGPU);
    assert(_enableGPU);
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        _enableGPU->setEnabled(false);
    }
    setSupportsOpenGLRender( _enableGPU->getValue() );
#endif

    if (gHostIsNatron) {
        _display->setIsSecretAndDisabled(true);
        _view->setIsSecretAndDisabled(true);
        _displayChoice = fetchChoiceParam(kParamDisplayChoice);
        _viewChoice = fetchChoiceParam(kParamViewChoice);
        // the choice menu can only be modified in Natron
        // Natron supports changing the entries in a choiceparam
        // Nuke (at least up to 8.0v3) does not
        OCIO::ConstConfigRcPtr config = _ocio->getConfig();
        buildDisplayMenu(config, _displayChoice);
        string display;
        _display->getValue(display);
        buildViewMenu( config, _viewChoice, display.c_str() );
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
    string displayName;
    _display->getValueAtTime(time, displayName);
    int displayIndex = -1;
    for (int i = 0; i < config->getNumDisplays(); ++i) {
        if ( displayName == config->getDisplay(i) ) {
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
        _display->setIsSecretAndDisabled(true);
        _displayChoice->setIsSecretAndDisabled(false);
    } else {
        // the input space name is not valid
        _display->setIsSecretAndDisabled(false);
        _displayChoice->setIsSecretAndDisabled(true);
    }
}

// sets the correct choice menu item from the view string value
void
OCIODisplayPlugin::viewCheck(double time,
                             bool setDefaultIfInvalid)
{
    if (!_viewChoice) {
        return;
    }
    OCIO::ConstConfigRcPtr config = _ocio->getConfig();
    if (!config) {
        return;
    }
    string displayName;
    _display->getValueAtTime(time, displayName);
    string viewName;
    _view->getValueAtTime(time, viewName);
    int numViews = config->getNumViews( displayName.c_str() );
    int viewIndex = -1;
    for (int i = 0; i < numViews; ++i) {
        if ( viewName == config->getView(displayName.c_str(), i) ) {
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
        _view->setIsSecretAndDisabled(true);
        _viewChoice->setIsSecretAndDisabled(false);
    } else {
        // the view name is not valid
        if (setDefaultIfInvalid) {
            _view->setValue( config->getDefaultView( displayName.c_str() ) );
        } else {
            _view->setIsSecretAndDisabled(false);
            _viewChoice->setIsSecretAndDisabled(true);
        }
    }
}

/* set up and run a copy processor */
void
OCIODisplayPlugin::setupAndCopy(PixelProcessorFilterBase & processor,
                                double time,
                                const OfxRectI &renderWindow,
                                const void *srcPixelData,
                                const OfxRectI& srcBounds,
                                PixelComponentEnum srcPixelComponents,
                                int srcPixelComponentCount,
                                BitDepthEnum srcPixelDepth,
                                int srcRowBytes,
                                void *dstPixelData,
                                const OfxRectI& dstBounds,
                                PixelComponentEnum dstPixelComponents,
                                int dstPixelComponentCount,
                                BitDepthEnum dstPixelDepth,
                                int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);

    // make sure bit depths are sane
    if ( (srcPixelDepth != dstPixelDepth) || (srcPixelComponents != dstPixelComponents) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

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
                                 PixelComponentEnum srcPixelComponents,
                                 int srcPixelComponentCount,
                                 BitDepthEnum srcBitDepth,
                                 int srcRowBytes,
                                 void *dstPixelData,
                                 const OfxRectI& dstBounds,
                                 PixelComponentEnum dstPixelComponents,
                                 int dstPixelComponentCount,
                                 BitDepthEnum dstBitDepth,
                                 int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    // do the rendering
    if ( (dstBitDepth != eBitDepthFloat) || ( (dstPixelComponents != ePixelComponentRGBA) && (dstPixelComponents != ePixelComponentRGB) && (dstPixelComponents != ePixelComponentAlpha) ) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }
    if ( ( (!unpremult && !premult) || (unpremult && premult) ) ) {
        copyPixels(*this, renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else if (unpremult && !premult) {
        if (dstPixelComponents == ePixelComponentRGBA) {
            PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
            fred.setPremultMaskMix(true, premultChannel, 1.);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == ePixelComponentRGB) {
            PixelCopierUnPremult<float, 3, 1, float, 3, 1> fred(*this);
            fred.setPremultMaskMix(true, premultChannel, 1.);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == ePixelComponentAlpha) {
            PixelCopierUnPremult<float, 1, 1, float, 1, 1> fred(*this);
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

OCIO::ConstProcessorRcPtr
OCIODisplayPlugin::getProcessor(OfxTime time)
{
    string inputSpace;

    _ocio->getInputColorspaceAtTime(time, inputSpace);
    ChannelSelectorEnum channel = (ChannelSelectorEnum)_channel->getValueAtTime(time);
    string display;
    _display->getValueAtTime(time, display);
    string view;
    _view->getValueAtTime(time, view);
    double gain = _gain->getValueAtTime(time);
    double gamma = _gamma->getValueAtTime(time);

    try {
        OCIO::ConstConfigRcPtr config = _ocio->getConfig();
        if (!config) {
            throw std::runtime_error("OCIO: no current config");
        }
        GenericOCIO::AutoMutex guard(_procMutex);
        if ( !_proc ||
             ( _procInputSpace != inputSpace) ||
             ( _procChannel != channel) ||
             ( _procDisplay != display) ||
             ( _procView != view) ||
             ( _procGain != gain) ||
             ( _procGamma != gamma) ) {
            OCIO::DisplayTransformRcPtr transform = OCIO::DisplayTransform::Create();
            transform->setInputColorSpaceName( inputSpace.c_str() );

            transform->setDisplay( display.c_str() );

            transform->setView( view.c_str() );

            // Specify an (optional) linear color correction
            {
                float m44[16];
                float offset4[4];
                const float slope4f[] = { (float)gain, (float)gain, (float)gain, (float)gain };
                OCIO::MatrixTransform::Scale(m44, offset4, slope4f);

                OCIO::MatrixTransformRcPtr mtx =  OCIO::MatrixTransform::Create();
                mtx->setValue(m44, offset4);

                transform->setLinearCC(mtx);
            }

            // Specify an (optional) post-display transform.
            {
                float exponent = 1.0f / std::max(1e-6f, (float)gamma);
                const float exponent4f[] = { exponent, exponent, exponent, exponent };
                OCIO::ExponentTransformRcPtr cc =  OCIO::ExponentTransform::Create();
                cc->setValue(exponent4f);
                transform->setDisplayCC(cc);
            }

            // Add Channel swizzling
            {
                int channelHot[4] = { 0, 0, 0, 0};

                switch (channel) {
                case eChannelSelectorLuminance:     // Luma
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
                case eChannelSelectorRGB:     // RGB
                    channelHot[0] = 1;
                    channelHot[1] = 1;
                    channelHot[2] = 1;
                    channelHot[3] = 1;
                    break;
                case eChannelSelectorR:     // R
                    channelHot[0] = 1;
                    break;
                case eChannelSelectorG:     // G
                    channelHot[1] = 1;
                    break;
                case eChannelSelectorB:     // B
                    channelHot[2] = 1;
                    break;
                case eChannelSelectorA:     // A
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
    } catch (const OCIO::Exception &e) {
        setPersistentMessage( Message::eMessageError, "", e.what() );
        throwSuiteStatusException(kOfxStatFailed);
    }

    return _proc;
} // OCIODisplayPlugin::getProcessor

void
OCIODisplayPlugin::apply(double time,
                         const OfxRectI& renderWindow,
                         float *pixelData,
                         const OfxRectI& bounds,
                         PixelComponentEnum pixelComponents,
                         int pixelComponentCount,
                         int rowBytes)
{
    // are we in the image bounds
    if ( (renderWindow.x1 < bounds.x1) || (renderWindow.x1 >= bounds.x2) || (renderWindow.y1 < bounds.y1) || (renderWindow.y1 >= bounds.y2) ||
         ( renderWindow.x2 <= bounds.x1) || ( renderWindow.x2 > bounds.x2) || ( renderWindow.y2 <= bounds.y1) || ( renderWindow.y2 > bounds.y2) ) {
        throw std::runtime_error("OCIO: render window outside of image bounds");
    }
    if ( (pixelComponents != ePixelComponentRGBA) && (pixelComponents != ePixelComponentRGB) ) {
        throw std::runtime_error("OCIO: invalid components (only RGB and RGBA are supported)");
    }

    OCIOProcessor processor(*this);
    // set the images
    processor.setDstImg(pixelData, bounds, pixelComponents, pixelComponentCount, eBitDepthFloat, rowBytes);

    processor.setProcessor( getProcessor(time) );

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

#if defined(OFX_SUPPORTS_OPENGLRENDER)

/*
 * Action called when an effect has just been attached to an OpenGL
 * context.
 *
 * The purpose of this action is to allow a plugin to set up any data it may need
 * to do OpenGL rendering in an instance. For example...
 *  - allocate a lookup table on a GPU,
 *  - create an openCL or CUDA context that is bound to the host's OpenGL
 *    context so it can share buffers.
 */
void*
OCIODisplayPlugin::contextAttached(bool createContextData)
{
#ifdef DEBUG
    if (getImageEffectHostDescription()->isNatron && !createContextData) {
        std::printf("ERROR: Natron did not ask to create context data\n");
    }
#endif
    if (createContextData) {
        // This will load OpenGL functions the first time it is executed (thread-safe)
        return new OCIOOpenGLContextData;
    } else {
        if (_openGLContextData) {
#         ifdef DEBUG
            std::printf("ERROR: contextAttached() called but context already attached\n");
#         endif
            contextDetached(NULL);
        }
        _openGLContextData = new OCIOOpenGLContextData;
    }

    return NULL;
}

/*
 * Action called when an effect is about to be detached from an
 * OpenGL context
 *
 * The purpose of this action is to allow a plugin to deallocate any resource
 * allocated in \ref ::kOfxActionOpenGLContextAttached just before the host
 * decouples a plugin from an OpenGL context.
 * The host must call this with the same OpenGL context active as it
 * called with the corresponding ::kOfxActionOpenGLContextAttached.
 */
void
OCIODisplayPlugin::contextDetached(void* contextData)
{
    if (contextData) {
        OCIOOpenGLContextData* myData = (OCIOOpenGLContextData*)contextData;
        delete myData;
    } else {
        if (!_openGLContextData) {
#         ifdef DEBUG
            std::printf("ERROR: contextDetached() called but no context attached\n");
#         endif
        }
        delete _openGLContextData;
        _openGLContextData = NULL;
    }
}

void
OCIODisplayPlugin::renderGPU(const RenderArguments &args)
{
    std::auto_ptr<Texture> srcImg( _srcClip->loadTexture(args.time) );
    if ( !srcImg.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if ( (srcImg->getRenderScale().x != args.renderScale.x) ||
         ( srcImg->getRenderScale().y != args.renderScale.y) ||
         ( srcImg->getField() != args.fieldToRender) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    std::auto_ptr<Texture> dstImg( _dstClip->loadTexture(args.time) );
    if ( !dstImg.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if ( (dstImg->getRenderScale().x != args.renderScale.x) ||
         ( dstImg->getRenderScale().y != args.renderScale.y) ||
         ( dstImg->getField() != args.fieldToRender) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    PixelComponentEnum srcComponents = srcImg->getPixelComponents();
    BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if ( (dstBitDepth != eBitDepthFloat) || (dstBitDepth != srcBitDepth) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ( ( (dstComponents != ePixelComponentRGBA) && (dstComponents != ePixelComponentRGB) && (dstComponents != ePixelComponentAlpha) ) ||
         ( dstComponents != srcComponents) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if ( (args.renderWindow.x1 < dstBounds.x1) || (args.renderWindow.x1 >= dstBounds.x2) || (args.renderWindow.y1 < dstBounds.y1) || (args.renderWindow.y1 >= dstBounds.y2) ||
         ( args.renderWindow.x2 <= dstBounds.x1) || ( args.renderWindow.x2 > dstBounds.x2) || ( args.renderWindow.y2 <= dstBounds.y1) || ( args.renderWindow.y2 > dstBounds.y2) ) {
        throwSuiteStatusException(kOfxStatErrValue);

        return;
        //throw std::runtime_error("render window outside of image bounds");
    }

    OCIOOpenGLContextData* contextData = NULL;
    if (getImageEffectHostDescription()->isNatron && !args.openGLContextData) {
#     ifdef DEBUG
        std::printf("ERROR: Natron did not provide the contextData pointer to the OpenGL render func.\n");
#     endif
    }
    if (args.openGLContextData) {
        // host provided kNatronOfxImageEffectPropOpenGLContextData,
        // which was returned by kOfxActionOpenGLContextAttached
        contextData = (OCIOOpenGLContextData*)args.openGLContextData;
    } else {
        if (!_openGLContextData) {
            // Sony Catalyst Edit never calls kOfxActionOpenGLContextAttached
#         ifdef DEBUG
            std::printf( ("ERROR: OpenGL render() called without calling contextAttached() first. Calling it now.\n") );
#         endif
            contextAttached(false);
            assert(_openGLContextData);
        }
        contextData = _openGLContextData;
    }
    if (!contextData) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    OCIO::ConstProcessorRcPtr proc = getProcessor(args.time);
    assert(proc);

    GenericOCIO::applyGL(srcImg.get(), proc, &contextData->procLut3D, &contextData->procLut3DID, &contextData->procShaderProgramID, &contextData->procFragmentShaderID, &contextData->procLut3DCacheID, &contextData->procShaderCacheID);
} // renderGPU

#endif // defined(OFX_SUPPORTS_OPENGLRENDER)


/* Override the render */
void
OCIODisplayPlugin::render(const RenderArguments &args)
{
    if (!_srcClip) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if (!_dstClip) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    assert(_srcClip && _dstClip);

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    if (args.openGLEnabled) {
        renderGPU(args);

        return;
    }
#endif

    std::auto_ptr<const Image> srcImg( _srcClip->fetchImage(args.time) );
    if ( !srcImg.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if ( (srcImg->getRenderScale().x != args.renderScale.x) ||
         ( srcImg->getRenderScale().y != args.renderScale.y) ||
         ( srcImg->getField() != args.fieldToRender) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    PixelComponentEnum srcComponents = srcImg->getPixelComponents();

    std::auto_ptr<Image> dstImg( _dstClip->fetchImage(args.time) );
    if ( !dstImg.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if ( (dstImg->getRenderScale().x != args.renderScale.x) ||
         ( dstImg->getRenderScale().y != args.renderScale.y) ||
         ( dstImg->getField() != args.fieldToRender) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if ( (dstBitDepth != eBitDepthFloat) || (dstBitDepth != srcBitDepth) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ( ( (dstComponents != ePixelComponentRGBA) && (dstComponents != ePixelComponentRGB) && (dstComponents != ePixelComponentAlpha) ) ||
         ( dstComponents != srcComponents) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if ( (args.renderWindow.x1 < dstBounds.x1) || (args.renderWindow.x1 >= dstBounds.x2) || (args.renderWindow.y1 < dstBounds.y1) || (args.renderWindow.y1 >= dstBounds.y2) ||
         ( args.renderWindow.x2 <= dstBounds.x1) || ( args.renderWindow.x2 > dstBounds.x2) || ( args.renderWindow.y2 <= dstBounds.y1) || ( args.renderWindow.y2 > dstBounds.y2) ) {
        throwSuiteStatusException(kOfxStatErrValue);

        return;
        //throw std::runtime_error("render window outside of image bounds");
    }

    const void* srcPixelData = NULL;
    OfxRectI bounds;
    PixelComponentEnum pixelComponents;
    BitDepthEnum bitDepth;
    int srcRowBytes;
    getImageData(srcImg.get(), &srcPixelData, &bounds, &pixelComponents, &bitDepth, &srcRowBytes);
    int pixelComponentCount = srcImg->getPixelComponentCount();

    // allocate temporary image
    int pixelBytes = pixelComponentCount * getComponentBytes(srcBitDepth);
    int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
    size_t memSize = (args.renderWindow.y2 - args.renderWindow.y1) * tmpRowBytes;
    ImageMemory mem(memSize, this);
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
    copyPixelData( false, premult, premultChannel, args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes, dstImg.get() );
} // OCIODisplayPlugin::render

void
OCIODisplayPlugin::changedParam(const InstanceChangedArgs &args,
                                const string &paramName)
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
            string display;
            _display->getValue(display);
            buildViewMenu( config, _viewChoice, display.c_str() );
            viewCheck(args.time, true);
        }
    } else if ( (paramName == kParamDisplayChoice) && (args.reason == eChangeUserEdit) ) {
        assert(_display);
        int displayIndex;
        _displayChoice->getValue(displayIndex);
        string displayOld;
        _display->getValue(displayOld);
        assert( 0 <= displayIndex && displayIndex < config->getNumDisplays() );
        string display = config->getDisplay(displayIndex);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (display != displayOld) {
            _display->setValue(display);
        }
    } else if (paramName == kParamView) {
        assert(_view);
        viewCheck(args.time);
    } else if ( (paramName == kParamViewChoice) && (args.reason == eChangeUserEdit) ) {
        assert(_view);
        string display;
        _display->getValue(display);
        int viewIndex;
        _viewChoice->getValueAtTime(args.time, viewIndex);
        string viewOld;
        _view->getValueAtTime(args.time, viewOld);
        assert( 0 <= viewIndex && viewIndex < config->getNumViews( display.c_str() ) );
        string view = config->getView(display.c_str(), viewIndex);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (view != viewOld) {
            _view->setValue(view);
        }
#ifdef OFX_SUPPORTS_OPENGLRENDER
    } else if (paramName == kParamEnableGPU) {
        bool supportsGL = _enableGPU->getValueAtTime(args.time);
        setSupportsOpenGLRender(supportsGL);
        setSupportsTiles(!supportsGL);
#endif
    } else {
        return _ocio->changedParam(args, paramName);
    }
} // OCIODisplayPlugin::changedParam

void
OCIODisplayPlugin::changedClip(const InstanceChangedArgs &args,
                               const string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) && _srcClip && (args.reason == eChangeUserEdit) ) {
        if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
            _premult->setValue(false);
        } else {
            switch ( _srcClip->getPreMultiplication() ) {
            case eImageOpaque:
                _premult->setValue(false);
                break;
            case eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
            }
        }
    }
}

mDeclarePluginFactory(OCIODisplayPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void
OCIODisplayPluginFactory::describe(ImageEffectDescriptor &desc)
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
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_SUPPORTS_OPENGLRENDER
    desc.setSupportsOpenGLRender(true);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
OCIODisplayPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                            ContextEnum context)
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

    gHostIsNatron = (getImageEffectHostDescription()->isNatron);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, OCIO::ROLE_REFERENCE);
    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
    const char * display = config ? config->getDefaultDisplay() : NULL;
    const char * view = display ? config->getDefaultView(display) : NULL;

    // display device
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamDisplay);
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
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDisplayChoice);
        param->setLabel(kParamDisplayLabel);
        param->setHint(kParamDisplayHint);
        if (config) {
            buildDisplayMenu(config, param);
        } else {
            //param->setEnabled(false); // done in constructor
        }
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // view transform
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamView);
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
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamViewChoice);
        param->setLabel(kParamViewLabel);
        param->setHint(kParamViewHint);
        if (config) {
            buildViewMenu( config, param, config->getDefaultDisplay() );
        } else {
            //param->setEnabled(false); // done in constructor
        }
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // gain
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGain);
        param->setLabel(kParamGainLabel);
        param->setHint(kParamGainHint);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(1. / 64., 64.);
        param->setDefault(1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // gamma
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGamma);
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
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamChannelSelector);
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

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnableGPU);
        param->setLabel(kParamEnableGPULabel);
        param->setHint(kParamEnableGPUHint);
        const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
        // Resolve advertises OpenGL support in its host description, but never calls render with OpenGL enabled
        if ( gHostDescription.supportsOpenGLRender && (gHostDescription.hostName != "DaVinciResolveLite") ) {
            param->setDefault(true);
            if (gHostDescription.APIVersionMajor * 100 + gHostDescription.APIVersionMinor < 104) {
                // Switching OpenGL render from the plugin was introduced in OFX 1.4
                param->setEnabled(false);
            }
        } else {
            param->setDefault(false);
            param->setEnabled(false);
        }

        if (page) {
            page->addChild(*param);
        }
    }
#endif

    GenericOCIO::describeInContextContext(desc, context, page);
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kOCIOHelpDisplaysButton);
        param->setLabel(kOCIOHelpButtonLabel);
        param->setHint(kOCIOHelpButtonHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
} // OCIODisplayPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
OCIODisplayPluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    return new OCIODisplayPlugin(handle);
}

static OCIODisplayPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // OFX_IO_USING_OCIO
