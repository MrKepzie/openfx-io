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
 * OCIOCDLTransform plugin.
 * Apply an ASC CDL grade.
 */

#ifdef OFX_IO_USING_OCIO

#include <cstdio> // fopen...

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "IOUtility.h"
#include "ofxNatron.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

#include "GenericOCIO.h"


namespace OCIO = OCIO_NAMESPACE;

using namespace OFX;
using namespace OFX::IO;

using std::string;

OFXS_NAMESPACE_ANONYMOUS_ENTER

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

#define kPluginIdentifier "fr.inria.openfx.OCIOCDLTransform"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamSlope "slope"
#define kParamSlopeLabel "Slope"
#define kParamSlopeHint "ASC CDL slope"
#define kParamSlopeMin 0.
#define kParamSlopeMax 4.

#define kParamOffset "offset"
#define kParamOffsetLabel "Offset"
#define kParamOffsetHint "ASC CDL offset"
#define kParamOffsetMin -0.2
#define kParamOffsetMax 0.2

#define kParamPower "power"
#define kParamPowerLabel "Power"
#define kParamPowerHint "ASC CDL power"
#define kParamPowerMin 0.
#define kParamPowerMax 4.

#define kParamSaturation "saturation"
#define kParamSaturationLabel "Saturation"
#define kParamSaturationHint "ASC CDL saturation"
#define kParamSaturationMin 0.
#define kParamSaturationMax 4.

#define kParamDirection "direction"
#define kParamDirectionLabel "Direction"
#define kParamDirectionHint "Transform direction."
#define kParamDirectionOptionForward "Forward"
#define kParamDirectionOptionInverse "Inverse"

#define kParamReadFromFile "readFromFile"
#define kParamReadFromFileLabel "Read from file"
#define kParamReadFromFileHint \
    "Load color correction information from the .cc or .ccc file."

#define kParamFile "file"
#define kParamFileLabel "File"
#define kParamFileHint \
    "Specify the src ASC CDL file, on disk, to use for this transform. " \
    "This can be either a .cc or .ccc file. If .ccc is specified, the cccid is required."

// Reload button, and hidden "version" knob to invalidate cache on reload
#define kParamReload "reload"
#define kParamReloadLabel "Reload"
#define kParamReloadHint "Reloads specified files"
#define kParamVersion "version"

#define kParamCCCID "cccId"
#define kParamCCCIDLabel "CCC Id"
#define kParamCCCIDHint "If the source file is an ASC CDL CCC (color correction collection), " \
    "this specifies the id to lookup. OpenColorIO::Contexts (envvars) are obeyed."
#define kParamCCCIDChoice "cccIdIndex"

#define kParamExport "export"
#define kParamExportLabel "Export"
#define kParamExportHint "Export this grade as a ColorCorrection XML file (.cc), which can be loaded with the OCIOFileTransform, or using a FileTransform in an OCIO config. The file must not already exist."
#define kParamExportDefault "Set filename to export this grade as .cc"

#if defined(OFX_SUPPORTS_OPENGLRENDER)
#define kParamEnableGPU "enableGPU"
#define kParamEnableGPULabel "Enable GPU Render"
#define kParamEnableGPUHint \
    "Enable GPU-based OpenGL render.\n" \
    "If the checkbox is checked but is not enabled (i.e. it cannot be unchecked), GPU render can not be enabled or disabled from the plugin and is probably part of the host options.\n" \
    "If the checkbox is not checked and is not enabled (i.e. it cannot be checked), GPU render is not available on this host.\n"
#endif

static bool gHostIsNatron = false; // TODO: generate a CCCId choice param kParamCCCIDChoice from available IDs

class OCIOCDLTransformPlugin
    : public ImageEffect
{
public:

    OCIOCDLTransformPlugin(OfxImageEffectHandle handle);

    virtual ~OCIOCDLTransformPlugin();

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
    virtual void beginEdit(void) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;

    /* override changed clip */
    virtual void changedClip(const InstanceChangedArgs &args, const string &clipName) OVERRIDE FINAL;

    // override the rod call
    //virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
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

    void updateCCCId();

    void refreshKnobEnabledState(bool readFromFile);

    void loadCDLFromFile();

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

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    bool _firstLoad;
    RGBParam *_slope;
    RGBParam *_offset;
    RGBParam *_power;
    DoubleParam *_saturation;
    ChoiceParam *_direction;
    BooleanParam* _readFromFile;
    StringParam *_file;
    IntParam *_version;
    StringParam *_cccid;
    StringParam *_export;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;

    GenericOCIO::Mutex _procMutex;
    OCIO::ConstProcessorRcPtr _proc;
    double _procSlope_r;
    double _procSlope_g;
    double _procSlope_b;
    double _procOffset_r;
    double _procOffset_g;
    double _procOffset_b;
    double _procPower_r;
    double _procPower_g;
    double _procPower_b;
    double _procSaturation;
    int _procDirection;

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    BooleanParam* _enableGPU;
    OCIOOpenGLContextData* _openGLContextData; // (OpenGL-only) - the single openGL context, in case the host does not support kNatronOfxImageEffectPropOpenGLContextData
#endif
};

OCIOCDLTransformPlugin::OCIOCDLTransformPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    , _firstLoad(true)
    , _slope(0)
    , _offset(0)
    , _power(0)
    , _saturation(0)
    , _direction(0)
    , _readFromFile(0)
    , _file(0)
    , _version(0)
    , _cccid(0)
    , _export(0)
    , _premult(0)
    , _premultChannel(0)
    , _mix(0)
    , _maskApply(0)
    , _maskInvert(0)
    , _procSlope_r(-1)
    , _procSlope_g(-1)
    , _procSlope_b(-1)
    , _procOffset_r(-1)
    , _procOffset_g(-1)
    , _procOffset_b(-1)
    , _procPower_r(-1)
    , _procPower_g(-1)
    , _procPower_b(-1)
    , _procSaturation(-1)
    , _procDirection(-1)
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
    _slope = fetchRGBParam(kParamSlope);
    _offset = fetchRGBParam(kParamOffset);
    _power = fetchRGBParam(kParamPower);
    _saturation = fetchDoubleParam(kParamSaturation);
    _direction = fetchChoiceParam(kParamDirection);
    _readFromFile = fetchBooleanParam(kParamReadFromFile);
    _file = fetchStringParam(kParamFile);
    _version = fetchIntParam(kParamVersion);
    _cccid = fetchStringParam(kParamCCCID);
    _export = fetchStringParam(kParamExport);
    assert(_slope && _offset && _power && _saturation && _direction && _readFromFile && _file && _version && _cccid && _export);
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

    updateCCCId();
    bool readFromFile;
    _readFromFile->getValue(readFromFile);
    refreshKnobEnabledState(readFromFile);
    // WARNING: we cannot setValue() here in the constructor, because it calls changedParam() on an object which is not yet constructed.
    // CDL file loading and parameter setting is delayed until the first call th changedParam(), beginEdit(), or render()
}

OCIOCDLTransformPlugin::~OCIOCDLTransformPlugin()
{
}

/* set up and run a copy processor */
void
OCIOCDLTransformPlugin::setupAndCopy(PixelProcessorFilterBase & processor,
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
OCIOCDLTransformPlugin::copyPixelData(bool unpremult,
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
} // OCIOCDLTransformPlugin::copyPixelData

OCIO::ConstProcessorRcPtr
OCIOCDLTransformPlugin::getProcessor(OfxTime time)
{
    if (_firstLoad) {
        _firstLoad = false;
        bool readFromFile;
        _readFromFile->getValue(readFromFile);
        if (readFromFile) {
            loadCDLFromFile();
        }
    }

    float sop[9];
    double slope_r, slope_g, slope_b;
    _slope->getValueAtTime(time, slope_r, slope_g, slope_b);
    double offset_r, offset_g, offset_b;
    _offset->getValueAtTime(time, offset_r, offset_g, offset_b);
    double power_r, power_g, power_b;
    _power->getValueAtTime(time, power_r, power_g, power_b);
    double saturation = _saturation->getValueAtTime(time);
    int directioni = _direction->getValueAtTime(time);

    try {
        GenericOCIO::AutoMutex guard(_procMutex);
        if ( !_proc ||
             ( _procSlope_r != slope_r) ||
             ( _procSlope_g != slope_g) ||
             ( _procSlope_b != slope_b) ||
             ( _procOffset_r != offset_r) ||
             ( _procOffset_g != offset_g) ||
             ( _procOffset_b != offset_b) ||
             ( _procPower_r != power_r) ||
             ( _procPower_g != power_g) ||
             ( _procPower_b != power_b) ||
             ( _procSaturation != saturation) ||
             ( _procDirection != directioni) ) {
            OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
            assert(config);
            OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
            sop[0] = (float)slope_r;
            sop[1] = (float)slope_g;
            sop[2] = (float)slope_b;
            sop[3] = (float)offset_r;
            sop[4] = (float)offset_g;
            sop[5] = (float)offset_b;
            sop[6] = (float)power_r;
            sop[7] = (float)power_g;
            sop[8] = (float)power_b;
            cc->setSOP(sop);
            cc->setSat( (float)saturation );

            if (directioni == 0) {
                cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
            } else {
                cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
            }

            _proc = config->getProcessor(cc);
            _procSlope_r = slope_r;
            _procSlope_g = slope_g;
            _procSlope_b = slope_b;
            _procOffset_r = offset_r;
            _procOffset_g = offset_g;
            _procOffset_b = offset_b;
            _procPower_r = power_r;
            _procPower_g = power_g;
            _procPower_b = power_b;
            _procSaturation = saturation;
            _procDirection = directioni;
        }
    } catch (const OCIO::Exception &e) {
        setPersistentMessage( Message::eMessageError, "", e.what() );
        throwSuiteStatusException(kOfxStatFailed);
    }

    return _proc;
} // getProecssor

void
OCIOCDLTransformPlugin::apply(double time,
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
OCIOCDLTransformPlugin::contextAttached(bool createContextData)
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
OCIOCDLTransformPlugin::contextDetached(void* contextData)
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
OCIOCDLTransformPlugin::renderGPU(const RenderArguments &args)
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
OCIOCDLTransformPlugin::render(const RenderArguments &args)
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
} // OCIOCDLTransformPlugin::render

bool
OCIOCDLTransformPlugin::isIdentity(const IsIdentityArguments &args,
                                   Clip * &identityClip,
                                   double & /*identityTime*/)
{
    const double time = args.time;
    float sop[9];
    double saturation;
    double r, g, b;

    _slope->getValueAtTime(time, r, g, b);
    sop[0] = (float)r;
    sop[1] = (float)g;
    sop[2] = (float)b;
    _offset->getValueAtTime(time, r, g, b);
    sop[3] = (float)r;
    sop[4] = (float)g;
    sop[5] = (float)b;
    _power->getValueAtTime(time, r, g, b);
    sop[6] = (float)r;
    sop[7] = (float)g;
    sop[8] = (float)b;
    _saturation->getValueAtTime(time, saturation);
    int _directioni;
    _direction->getValueAtTime(time, _directioni);
    string file;
    _file->getValueAtTime(time, file);
    string cccid;
    _cccid->getValueAtTime(time, cccid);

    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();
    try {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        if (!config) {
            throw std::runtime_error("OCIO: no current config");
        }
        OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
        cc->setSOP(sop);
        cc->setSat( (float)saturation );

        if (_directioni == 0) {
            cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        } else {
            cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        }

        OCIO::ConstProcessorRcPtr proc = config->getProcessor(cc);
        if ( proc->isNoOp() ) {
            identityClip = _srcClip;

            return true;
        }
    } catch (const std::exception &e) {
        setPersistentMessage( Message::eMessageError, "", e.what() );
        throwSuiteStatusException(kOfxStatFailed);
    }

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
} // OCIOCDLTransformPlugin::isIdentity

void
OCIOCDLTransformPlugin::updateCCCId()
{
    // Convoluted equiv to pysting::endswith(m_file, ".ccc")
    // TODO: Could this be queried from the processor?
    string srcstring;

    _file->getValue(srcstring);
    const string cccext = ".ccc";
    if ( std::equal( cccext.rbegin(), cccext.rend(), srcstring.rbegin() ) ) {
        _cccid->setIsSecretAndDisabled(false);
    } else {
        _cccid->setIsSecretAndDisabled(true);
    }
}

void
OCIOCDLTransformPlugin::refreshKnobEnabledState(bool readFromFile)
{
    if (readFromFile) {
        _slope->setEnabled(false);
        _offset->setEnabled(false);
        _power->setEnabled(false);
        _saturation->setEnabled(false);

        // We leave these active to allow knob re-use with the import/export buttons
        //m_fileKnob->enable();
        //m_cccidKnob->enable();

        loadCDLFromFile();
    } else {
        _slope->setEnabled(true);
        _offset->setEnabled(true);
        _power->setEnabled(true);
        _saturation->setEnabled(true);

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
        string file;
        _file->getValue(file);
        string cccid;
        _cccid->getValue(cccid);
        transform = OCIO::CDLTransform::CreateFromFile( file.c_str(), cccid.c_str() );
    } catch (const OCIO::Exception &e) {
        setPersistentMessage( Message::eMessageError, "", e.what() );
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }


    float sop[9];
    transform->getSOP(sop);

    _slope->deleteAllKeys();
    _slope->setValue(sop[0], sop[1], sop[2]);
    _offset->deleteAllKeys();
    _offset->setValue(sop[3], sop[4], sop[5]);
    _power->deleteAllKeys();
    _power->setValue(sop[6], sop[7], sop[8]);
    _saturation->deleteAllKeys();
    _saturation->setValue( transform->getSat() );
}

void
OCIOCDLTransformPlugin::beginEdit()
{
    if (_firstLoad) {
        _firstLoad = false;
        bool readFromFile;
        _readFromFile->getValue(readFromFile);
        if (readFromFile) {
            loadCDLFromFile();
        }
    }
}

void
OCIOCDLTransformPlugin::changedParam(const InstanceChangedArgs &args,
                                     const string &paramName)
{
    clearPersistentMessage();

    if ( _firstLoad || (paramName == kParamReadFromFile) || (paramName == kParamFile) || (paramName == kParamCCCID) ) {
        _firstLoad = false;
        bool readFromFile;
        _readFromFile->getValue(readFromFile);
        refreshKnobEnabledState(readFromFile);
        if (readFromFile) {
            loadCDLFromFile();
        }
    }

    // Only show the cccid knob when loading a .cc/.ccc file. Set
    // hidden state when the src is changed, or the node properties
    // are shown
    if (paramName == kParamFile) {
        updateCCCId();
    } else if (paramName == kParamReload) {
        _version->setValue(_version->getValue() + 1); // invalidate the node cache
        OCIO::ClearAllCaches();
    } else if ( (paramName == kParamExport) && (args.reason == eChangeUserEdit) ) {
        string exportName;
        _export->getValueAtTime(args.time, exportName);
        // if file already exists, don't overwrite it
        if ( exportName.empty() ) {
            sendMessage(Message::eMessageError, "", "Export file name is empty, please enter a valid non-existing file name.");
        } else {
            std::FILE *file = std::fopen(exportName.c_str(), "r");
            if (file) {
                std::fclose(file);
                sendMessage(Message::eMessageError, "", string("File ") + exportName + " already exists, please select another filename");
            } else {
                file = std::fopen(exportName.c_str(), "w");
                if (!file) {
                    sendMessage(Message::eMessageError, "", string("File ") + exportName + " cannot be written");
                    throwSuiteStatusException(kOfxStatFailed);

                    return;
                }
                const double time = args.time;
                float sop[9];
                double saturation;
                double r, g, b;
                _slope->getValueAtTime(time, r, g, b);
                sop[0] = (float)r;
                sop[1] = (float)g;
                sop[2] = (float)b;
                _offset->getValueAtTime(time, r, g, b);
                sop[3] = (float)r;
                sop[4] = (float)g;
                sop[5] = (float)b;
                _power->getValueAtTime(time, r, g, b);
                sop[6] = (float)r;
                sop[7] = (float)g;
                sop[8] = (float)b;
                _saturation->getValueAtTime(time, saturation);
                int _directioni;
                _direction->getValueAtTime(time, _directioni);

                try {
                    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
                    if (!config) {
                        throw std::runtime_error("OCIO: no current config");
                    }
                    OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
                    cc->setSOP(sop);
                    cc->setSat( (float)saturation );

                    if (_directioni == 0) {
                        cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
                    } else {
                        cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
                    }

                    std::fputs(cc->getXML(), file);
                } catch (const std::exception &e) {
                    setPersistentMessage( Message::eMessageError, "", e.what() );
                    std::fclose(file);
                    throwSuiteStatusException(kOfxStatFailed);

                    return;
                }
                std::fclose(file);
            }
        }

        // reset back to default
        _export->setValue(kParamExportDefault);
#if defined(OFX_SUPPORTS_OPENGLRENDER)
    } else if (paramName == kParamEnableGPU) {
        bool supportsGL = _enableGPU->getValueAtTime(args.time);
        setSupportsOpenGLRender(supportsGL);
        setSupportsTiles(!supportsGL);
#endif
    }
} // OCIOCDLTransformPlugin::changedParam

void
OCIOCDLTransformPlugin::changedClip(const InstanceChangedArgs &args,
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

mDeclarePluginFactory(OCIOCDLTransformPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void
OCIOCDLTransformPluginFactory::describe(ImageEffectDescriptor &desc)
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

#if defined(OFX_SUPPORTS_OPENGLRENDER)
    desc.setSupportsOpenGLRender(true);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
OCIOCDLTransformPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                                 ContextEnum context)
{
    gHostIsNatron = (getImageEffectHostDescription()->isNatron);
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

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // ASC CDL grade numbers
    {
        RGBParamDescriptor *param = desc.defineRGBParam(kParamSlope);
        param->setLabel(kParamSlopeLabel);
        param->setHint(kParamSlopeHint);
        param->setRange(kParamSlopeMin, kParamSlopeMin, kParamSlopeMin, kParamSlopeMax, kParamSlopeMax, kParamSlopeMax);
        param->setDisplayRange(kParamSlopeMin, kParamSlopeMin, kParamSlopeMin, kParamSlopeMax, kParamSlopeMax, kParamSlopeMax);
        param->setDefault(1., 1., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBParamDescriptor *param = desc.defineRGBParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setRange(kParamOffsetMin, kParamOffsetMin, kParamOffsetMin, kParamOffsetMax, kParamOffsetMax, kParamOffsetMax);
        param->setDisplayRange(kParamOffsetMin, kParamOffsetMin, kParamOffsetMin, kParamOffsetMax, kParamOffsetMax, kParamOffsetMax);
        param->setDefault(0., 0., 0.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBParamDescriptor *param = desc.defineRGBParam(kParamPower);
        param->setLabel(kParamPowerLabel);
        param->setHint(kParamPowerHint);
        param->setRange(kParamPowerMin, kParamPowerMin, kParamPowerMin, kParamPowerMax, kParamPowerMax, kParamPowerMax);
        param->setDisplayRange(kParamPowerMin, kParamPowerMin, kParamPowerMin, kParamPowerMax, kParamPowerMax, kParamPowerMax);
        param->setDefault(1., 1., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSaturation);
        param->setLabel(kParamSaturationLabel);
        param->setHint(kParamSaturationHint);
        param->setRange(kParamSaturationMin, kParamSaturationMax);
        param->setDisplayRange(kParamSaturationMin, kParamSaturationMax);
        param->setDefault(1.);
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
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamReadFromFile);
        param->setLabel(kParamReadFromFileLabel);
        param->setHint(kParamReadFromFileHint);
        param->setAnimates(false);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor *param = desc.defineStringParam(kParamFile);
        param->setLabel(kParamFileLabel);
        param->setHint(kParamFileHint);
        param->setStringType(eStringTypeFilePath);
        param->setFilePathExists(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamReload);
        param->setLabel(kParamReloadLabel);
        param->setHint(kParamReloadHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamVersion);
        param->setIsSecretAndDisabled(true); // always secret
        param->setDefault(1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor *param = desc.defineStringParam(kParamCCCID);
        param->setLabel(kParamCCCIDLabel);
        param->setHint(kParamCCCIDHint);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor *param = desc.defineStringParam(kParamExport);
        param->setLabel(kParamExportLabel);
        param->setHint(kParamExportHint);
        param->setStringType(eStringTypeFilePath);
        param->setFilePathExists(false); // necessary for output files
        param->setEvaluateOnChange(false);
        param->setIsPersistent(false);
        param->setAnimates(false);
        param->setDefault(kParamExportDefault);
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
} // OCIOCDLTransformPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
OCIOCDLTransformPluginFactory::createInstance(OfxImageEffectHandle handle,
                                              ContextEnum /*context*/)
{
    return new OCIOCDLTransformPlugin(handle);
}

static OCIOCDLTransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // OFX_IO_USING_OCIO
