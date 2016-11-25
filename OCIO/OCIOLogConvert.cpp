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
 * OCIOLogConvert plugin.
 * Use OpenColorIO to convert from SCENE_LINEAR to COMPOSITING_LOG (or back).
 */

#ifdef OFX_IO_USING_OCIO

#include <cstdlib>
#ifdef DEBUG
#include <cstdio> // printf
#endif
#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "IOUtility.h"
#include "ofxNatron.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "GenericOCIO.h"

namespace OCIO = OCIO_NAMESPACE;

using namespace OFX;
using namespace IO;

using std::string;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "OCIOLogConvertOFX"
#define kPluginGrouping "Color/OCIO"
#define kPluginDescription  "Use OpenColorIO to convert from SCENE_LINEAR to COMPOSITING_LOG (or back)."

#define kPluginIdentifier "fr.inria.openfx.OCIOLogConvert"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamOperation "operation"
#define kParamOperationLabel "Operation"
#define kParamOperationHint "Operation to perform. Lin is the SCENE_LINEAR profile and Log is the COMPOSITING_LOG profile of the OCIO configuration."
#define kParamOperationOptionLogToLin "Log to Lin"
#define kParamOperationOptionLinToLog "Lin to Log"

#if defined(OFX_SUPPORTS_OPENGLRENDER)
#define kParamEnableGPU "enableGPU"
#define kParamEnableGPULabel "Enable GPU Render"
#define kParamEnableGPUHint \
    "Enable GPU-based OpenGL render.\n" \
    "If the checkbox is checked but is not enabled (i.e. it cannot be unchecked), GPU render can not be enabled or disabled from the plugin and is probably part of the host options.\n" \
    "If the checkbox is not checked and is not enabled (i.e. it cannot be checked), GPU render is not available on this host.\n"
#endif

static bool gWasOCIOEnvVarFound = false;

class OCIOLogConvertPlugin
    : public ImageEffect
{
public:

    OCIOLogConvertPlugin(OfxImageEffectHandle handle);

    virtual ~OCIOLogConvertPlugin();

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

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

    OCIO::ConstProcessorRcPtr getProcessor(OfxTime time);

    void copyPixelData(bool unpremult,
                       bool premult,
                       bool maskmix,
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

    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);

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

    void loadConfig(double time);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    string _ocioConfigFileName;
    StringParam *_ocioConfigFile; //< filepath of the OCIO config file
    ChoiceParam *_mode;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;

    OCIO::ConstConfigRcPtr _config;

    GenericOCIO::Mutex _procMutex;
    OCIO::ConstProcessorRcPtr _proc;
    int _procMode;

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    BooleanParam* _enableGPU;
    OCIOOpenGLContextData* _openGLContextData; // (OpenGL-only) - the single openGL context, in case the host does not support kNatronOfxImageEffectPropOpenGLContextData
#endif
};

OCIOLogConvertPlugin::OCIOLogConvertPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    , _ocioConfigFileName()
    , _ocioConfigFile(0)
    , _mode(0)
    , _premult(0)
    , _premultChannel(0)
    , _mix(0)
    , _maskApply(0)
    , _maskInvert(0)
    , _procMode(-1)
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
    _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
    assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
    _ocioConfigFile = fetchStringParam(kOCIOParamConfigFile);
    assert(_ocioConfigFile);
    _mode = fetchChoiceParam(kParamOperation);
    assert(_mode);
    _premult = fetchBooleanParam(kParamPremult);
    _premultChannel = fetchChoiceParam(kParamPremultChannel);
    assert(_premult && _premultChannel);
    _mix = fetchDoubleParam(kParamMix);
    _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    _enableGPU = fetchBooleanParam(kParamEnableGPU);
    assert(_enableGPU);
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    if (!gHostDescription.supportsOpenGLRender) {
        _enableGPU->setEnabled(false);
    }
    setSupportsOpenGLRender( _enableGPU->getValue() );
#endif

    loadConfig(0.);
}

OCIOLogConvertPlugin::~OCIOLogConvertPlugin()
{
}

void
OCIOLogConvertPlugin::loadConfig(double time)
{
    string filename;

    _ocioConfigFile->getValueAtTime(time, filename);

    if (filename == _ocioConfigFileName) {
        return;
    }

    _config.reset();
    try {
        _ocioConfigFileName = filename;
        _config = OCIO::Config::CreateFromFile( _ocioConfigFileName.c_str() );
        _mode->setEnabled(true);
        clearPersistentMessage();
    } catch (OCIO::Exception &e) {
        _ocioConfigFileName.clear();
        _mode->setEnabled(false);
        setPersistentMessage( Message::eMessageError, "", string("OpenColorIO error: ") + e.what() );
        _config = OCIO::GetCurrentConfig();
    }
}

/* set up and run a copy processor */
void
OCIOLogConvertPlugin::setupAndCopy(PixelProcessorFilterBase & processor,
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

    std::auto_ptr<const Image> orig( ( _srcClip && _srcClip->isConnected() ) ?
                                     _srcClip->fetchImage(time) : 0 );

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg( orig.get() );
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, 0);

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
OCIOLogConvertPlugin::copyPixelData(bool unpremult,
                                    bool premult,
                                    bool maskmix,
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
    if (!unpremult && !premult && !maskmix) {
        copyPixels(*this, renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else if (unpremult && !premult && !maskmix) {
        if (dstPixelComponents == ePixelComponentRGBA) {
            PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == ePixelComponentRGB) {
            PixelCopierUnPremult<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == ePixelComponentAlpha) {
            PixelCopierUnPremult<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch
    } else if (!unpremult && !premult && maskmix) {
        if (dstPixelComponents == ePixelComponentRGBA) {
            PixelCopierMaskMix<float, 4, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == ePixelComponentRGB) {
            PixelCopierMaskMix<float, 3, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == ePixelComponentAlpha) {
            PixelCopierMaskMix<float, 1, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch
    } else if (!unpremult && premult && maskmix) {
        if (dstPixelComponents == ePixelComponentRGBA) {
            PixelCopierPremultMaskMix<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == ePixelComponentRGB) {
            PixelCopierPremultMaskMix<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == ePixelComponentAlpha) {
            PixelCopierPremultMaskMix<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch
    } else {
        assert(false); // should never happen
    }
} // OCIOLogConvertPlugin::copyPixelData

OCIO::ConstProcessorRcPtr
OCIOLogConvertPlugin::getProcessor(OfxTime time)
{
    int mode_i = _mode->getValueAtTime(time);

    try {
        GenericOCIO::AutoMutex guard(_procMutex);
        if ( !_proc ||
             ( _procMode != mode_i) ) {
            const char * src = 0;
            const char * dst = 0;

            if (mode_i == 0) {
                src = OCIO::ROLE_COMPOSITING_LOG;
                dst = OCIO::ROLE_SCENE_LINEAR;
            } else {
                src = OCIO::ROLE_SCENE_LINEAR;
                dst = OCIO::ROLE_COMPOSITING_LOG;
            }

            _proc = _config->getProcessor(src, dst);
        }
    } catch (const OCIO::Exception &e) {
        setPersistentMessage( Message::eMessageError, "", e.what() );
        throwSuiteStatusException(kOfxStatFailed);
    }

    return _proc;
} // getProcessor

void
OCIOLogConvertPlugin::apply(double time,
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
OCIOLogConvertPlugin::contextAttached(bool createContextData)
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
OCIOLogConvertPlugin::contextDetached(void* contextData)
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
OCIOLogConvertPlugin::renderGPU(const RenderArguments &args)
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
OCIOLogConvertPlugin::render(const RenderArguments &args)
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

    if (!_srcClip) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    assert(_srcClip);
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

    if (!_dstClip) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    assert(_dstClip);
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

    // copy renderWindow to the temporary image
    copyPixelData(premult, false, false, args.time, args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);

    ///do the color-space conversion
    apply(args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, tmpRowBytes);

    // copy the color-converted window
    copyPixelData( false, premult, true, args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes, dstImg.get() );
} // OCIOLogConvertPlugin::render

bool
OCIOLogConvertPlugin::isIdentity(const IsIdentityArguments &args,
                                 Clip * &identityClip,
                                 double & /*identityTime*/)
{
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();

    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }


    return false;
}

void
OCIOLogConvertPlugin::changedParam(const InstanceChangedArgs &args,
                                   const string &paramName)
{
    if (paramName == kOCIOParamConfigFile) {
        loadConfig(args.time); // re-load the new OCIO config
        if ( !_config && (args.reason == eChangeUserEdit) ) {
            string filename;
            _ocioConfigFile->getValue(filename);
            sendMessage(Message::eMessageError, "", string("Cannot load OCIO config file \"") + filename + '"');
        }
    } else if (paramName == kOCIOHelpButton) {
        string msg = "OpenColorIO Help\n"
                     "The OCIO configuration file can be set using the \"OCIO\" environment variable, which should contain the full path to the .ocio file.\n"
                     "OpenColorIO version (compiled with / running with): " OCIO_VERSION "/";
        msg += OCIO::GetVersion();
        msg += '\n';
        if (_config) {
            const char* configdesc = _config->getDescription();
            int configdesclen = std::strlen(configdesc);
            if (configdesclen > 0) {
                msg += "\nThis OCIO configuration is ";
                msg += configdesc;
                if (configdesc[configdesclen - 1] != '\n') {
                    msg += '\n';
                }
            }
            msg += '\n';

            {
                int csidx = _config->getIndexForColorSpace(OCIO::ROLE_SCENE_LINEAR);
                const char* csname = _config->getColorSpaceNameByIndex(csidx);;
                msg += "SCENE_LINEAR colorspace: ";
                msg += csname;
                OCIO::ConstColorSpaceRcPtr cs = _config->getColorSpace(csname);
                string csdesc = cs ? cs->getDescription() : "(no colorspace)";
                csdesc.erase(csdesc.find_last_not_of(" \n\r\t") + 1);
                int csdesclen = csdesc.size();
                if (csdesclen > 0) {
                    msg += " (";
                    msg += csdesc;
                    msg += ")\n";
                } else {
                    msg += '\n';
                }
            }
            msg += '\n';
            {
                int csidx = _config->getIndexForColorSpace(OCIO::ROLE_COMPOSITING_LOG);
                const char* csname = _config->getColorSpaceNameByIndex(csidx);;
                msg += "COMPOSITING_LOG colorspace: ";
                msg += csname;
                OCIO::ConstColorSpaceRcPtr cs = _config->getColorSpace(csname);
                string csdesc = cs ? cs->getDescription() : "(no colorspace)";
                csdesc.erase(csdesc.find_last_not_of(" \n\r\t") + 1);
                int csdesclen = csdesc.size();
                if (csdesclen > 0) {
                    msg += " (";
                    msg += csdesc;
                    msg += ")\n";
                } else {
                    msg += '\n';
                }
            }
        }
        sendMessage(Message::eMessageMessage, "", msg);
#ifdef OFX_SUPPORTS_OPENGLRENDER
    } else if (paramName == kParamEnableGPU) {
        bool supportsGL = _enableGPU->getValueAtTime(args.time);
        setSupportsOpenGLRender(supportsGL);
        setSupportsTiles(!supportsGL);
#endif
    }
} // OCIOLogConvertPlugin::changedParam

void
OCIOLogConvertPlugin::changedClip(const InstanceChangedArgs &args,
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

mDeclarePluginFactory(OCIOLogConvertPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void
OCIOLogConvertPluginFactory::describe(ImageEffectDescriptor &desc)
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
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
OCIOLogConvertPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    char* file = std::getenv("OCIO");
    OCIO::ConstConfigRcPtr config;
    if (file != NULL) {
        try {
            config = OCIO::Config::CreateFromFile(file);
            gWasOCIOEnvVarFound = true;
        } catch (OCIO::Exception &e) {
        }
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    ////////// OCIO config file
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamConfigFile);
        param->setLabel(kOCIOParamConfigFileLabel);
        param->setHint(kOCIOParamConfigFileHint);
        param->setStringType(eStringTypeFilePath);
        param->setFilePathExists(true);
        // the OCIO config can only be set in a portable fashion using the environment variable.
        // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
        // disable it, and set the default from the env variable.
        assert( getImageEffectHostDescription() );
        //param->setEnabled(false); // done in constructor
        if (file == NULL) {
            param->setDefault("WARNING: Open an OCIO config file, or set the OCIO environnement variable");
        } else if (!config) {
            string s("ERROR: Invalid OCIO configuration '");
            s += file;
            s += '\'';
            param->setDefault(s);
        } else {
            param->setDefault(file);
        }
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kOCIOHelpButton);
        param->setLabel(kOCIOHelpButtonLabel);
        param->setHint(kOCIOHelpButtonHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOperation);
        param->setLabel(kParamOperationLabel);
        param->setHint(kParamOperationHint);
        param->appendOption(kParamOperationOptionLogToLin);
        param->appendOption(kParamOperationOptionLinToLog);
        if (!config) {
            //param->setEnabled(false); // done in constructor
        }
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

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);

#ifdef OFX_SUPPORTS_OPENGLRENDER
    desc.setSupportsOpenGLRender(true);
#endif
} // OCIOLogConvertPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
OCIOLogConvertPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            ContextEnum /*context*/)
{
    return new OCIOLogConvertPlugin(handle);
}

static OCIOLogConvertPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // OFX_IO_USING_OCIO
