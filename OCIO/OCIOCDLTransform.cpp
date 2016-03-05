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
 * OCIOCDLTransform plugin.
 * Apply an ASC CDL grade.
 */

#ifdef OFX_IO_USING_OCIO

#include <cstdio> // fopen...
#include <OpenColorIO/OpenColorIO.h>

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "IOUtility.h"
#include "ofxNatron.h"
#include "ofxsCoords.h"
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

static bool gHostIsNatron = false; // TODO: generate a CCCId choice param kParamCCCIDChoice from available IDs

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
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // override the rod call
    //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
    void updateCCCId();

    void refreshKnobEnabledState(bool readFromFile);

    void loadCDLFromFile();

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

    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);

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

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;

    bool _firstLoad;
    OFX::RGBParam *_slope;
    OFX::RGBParam *_offset;
    OFX::RGBParam *_power;
    OFX::DoubleParam *_saturation;
    OFX::ChoiceParam *_direction;
    OFX::BooleanParam* _readFromFile;
    OFX::StringParam *_file;
    OFX::IntParam *_version;
    OFX::StringParam *_cccid;
    OFX::StringParam *_export;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;

    OFX::MultiThread::Mutex _procMutex;
    OCIO_NAMESPACE::ConstProcessorRcPtr _proc;
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
};

OCIOCDLTransformPlugin::OCIOCDLTransformPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
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
OCIOCDLTransformPlugin::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
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
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg(orig.get());
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
    if (!unpremult && !premult && !maskmix) {
        copyPixels(*this, renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else if (unpremult && !premult && !maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierUnPremult<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierUnPremult<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch
        
    } else if (!unpremult && !premult && maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierMaskMix<float, 4, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierMaskMix<float, 3, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierMaskMix<float, 1, 1, true> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch

    } else if (!unpremult && premult && maskmix) {
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::PixelCopierPremultMaskMix<float, 4, 1, float, 4, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::PixelCopierPremultMaskMix<float, 3, 1, float, 3, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::PixelCopierPremultMaskMix<float, 1, 1, float, 1, 1> fred(*this);
            setupAndCopy(fred, time, renderWindow,
                         srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
        } // switch

    } else {
        assert(false); // should never happen
    }
}

void
OCIOCDLTransformPlugin::apply(double time,
                              const OfxRectI& renderWindow,
                              float *pixelData,
                              const OfxRectI& bounds,
                              OFX::PixelComponentEnum pixelComponents,
                              int pixelComponentCount,
                              int rowBytes)
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
        OFX::MultiThread::AutoMutex guard(_procMutex);
        if (!_proc ||
            _procSlope_r != slope_r ||
            _procSlope_g != slope_g ||
            _procSlope_b != slope_b ||
            _procOffset_r != offset_r ||
            _procOffset_g != offset_g ||
            _procOffset_b != offset_b ||
            _procPower_r != power_r ||
            _procPower_g != power_g ||
            _procPower_b != power_b ||
            _procSaturation != saturation ||
            _procDirection != directioni) {

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
            cc->setSat((float)saturation);

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
OCIOCDLTransformPlugin::render(const OFX::RenderArguments &args)
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

    // copy the color-converted window
    copyPixelData(false, premult, true, args.time, args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes, dstImg.get());
}

bool
OCIOCDLTransformPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
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
    std::string file;
    _file->getValueAtTime(time, file);
    std::string cccid;
    _cccid->getValueAtTime(time, cccid);

    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();
    try {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        assert(config);
        OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
        cc->setSOP(sop);
        cc->setSat((float)saturation);

        if (_directioni == 0) {
            cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        } else {
            cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
        }

        OCIO::ConstProcessorRcPtr proc = config->getProcessor(cc);
        if (proc->isNoOp()) {
            identityClip = _srcClip;
            return true;
        }
    } catch (const OCIO::Exception &e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
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
OCIOCDLTransformPlugin::updateCCCId()
{
    // Convoluted equiv to pysting::endswith(m_file, ".ccc")
    // TODO: Could this be queried from the processor?
    std::string srcstring;
    _file->getValue(srcstring);
    const std::string cccext = ".ccc";
    if(std::equal(cccext.rbegin(), cccext.rend(), srcstring.rbegin())) {
        _cccid->setIsSecret(false);
    } else {
        _cccid->setIsSecret(true);
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
        std::string file;
        _file->getValue(file);
        std::string cccid;
        _cccid->getValue(cccid);
        transform = OCIO::CDLTransform::CreateFromFile(file.c_str(), cccid.c_str());
    } catch (const OCIO::Exception &e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
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
    _saturation->setValue(transform->getSat());
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
OCIOCDLTransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    clearPersistentMessage();

    if (_firstLoad || paramName == kParamReadFromFile || paramName == kParamFile || paramName == kParamCCCID) {
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
        _version->setValue(_version->getValue()+1); // invalidate the node cache
        OCIO::ClearAllCaches();
    } else if (paramName == kParamExport && args.reason == OFX::eChangeUserEdit) {
        std::string exportName;
        _export->getValueAtTime(args.time, exportName);
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
                    OFX::throwSuiteStatusException(kOfxStatFailed);
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
                    assert(config);
                    OCIO::CDLTransformRcPtr cc = OCIO::CDLTransform::Create();
                    cc->setSOP(sop);
                    cc->setSat((float)saturation);

                    if (_directioni == 0) {
                        cc->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
                    } else {
                        cc->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
                    }

                    std::fputs(cc->getXML(), file);
                } catch (const OCIO::Exception &e) {
                    setPersistentMessage(OFX::Message::eMessageError, "", e.what());
                    std::fclose(file);
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                    return;
                }
                std::fclose(file);
            }
        }

        // reset back to default
        _export->setValue(kParamExportDefault);
    }
}

void
OCIOCDLTransformPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
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

using namespace OFX;

mDeclarePluginFactory(OCIOCDLTransformPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OCIOCDLTransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
void OCIOCDLTransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);
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
        param->setIsSecret(true); // always secret
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
        param->setIsPersistant(false);
        param->setAnimates(false);
        param->setDefault(kParamExportDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIOCDLTransformPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OCIOCDLTransformPlugin(handle);
}


static OCIOCDLTransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

#endif // OFX_IO_USING_OCIO
