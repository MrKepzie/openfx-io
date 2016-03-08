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
 * OFX SeGrain plugin.
 */

#include <cmath>
#include <algorithm>
//#include <iostream>
#ifdef _WINDOWS
#    define NOMINMAX 1
// windows - defined for both Win32 and Win64
#    include <windows.h>
// the following must be included before SePlatform.h tries to include
// them with _CRT_NONSTDC_NO_DEPRECATE=1 and _CRT_SECURE_NO_DEPRECATE=1
#    include <malloc.h>
#    include <io.h>
#    include <tchar.h>
#    include <process.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // _WINDOWS

#include <SeNoise.h>
#include <SeExprBuiltins.h>

#ifdef _WINDOWS
// fix SePlatform.h's bad defines, see https://github.com/wdas/SeExpr/issues/33
#undef snprintf
#undef strtok_r
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#  if defined(_MSC_VER) && _MSC_VER >= 1400
#    define strtok_r(s,d,p) strtok_s(s,d,p)
#  endif
#endif // _WINDOWS

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsRamp.h"
#include "ofxsTransformInteract.h"
#include "ofxsMatrix2D.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "SeGrain"
#define kPluginGrouping "Draw"
#define kPluginDescription "Adds synthetic grain. Push \"presets\" to get predefined types of grain, these are the correct size for 2K scans.\n\nYou can also adjust the sliders to match a sample piece of grain. Find a sample with a rather constant background, blur it to remove the grain, and use as input to this. View with a wipe in the viewer so you can make a match. It helps to view and match each of the red, green, blue separately."
#define kPluginIdentifier "net.sf.openfx.SeGrain"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamSeed "grainSeed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Change this value to make different instances of this operator produce different noise."
#define kParamSeedDefault 134.

#define kParamPresets "grainPresets"
#define kParamPresetsLabel "Presets"
#define kParamPresetsHint "Presets for common types of film."
#define kParamPresetsOptionOther "Other"

#define kSizeMin 0.001 // minimum grain size

struct PresetStruct {
    // Size:
    double red_size;
    double green_size;
    double blue_size;
    // Irregularity:
    double red_i;
    double green_i;
    double blue_i;
    // Intensity:
    double red_m;
    double green_m;
    double blue_m;
    const char* label;
};

#define NUMPRESETS 6
static struct PresetStruct gPresets[NUMPRESETS] =
{
  { /*red_size*/ 3.30, /*green_size*/ 2.90, /*blue_size*/ 2.50, /*red_i*/ 0.60, /*green_i*/ 0.60, /*blue_i*/ 0.60, /*red_m*/ 0.42, /*green_m*/ 0.46, /*blue_m*/ 0.85, /*label*/ "Kodak 5248" },
  { /*red_size*/ 2.70, /*green_size*/ 2.60, /*blue_size*/ 2.40, /*red_i*/ 1.00, /*green_i*/ 0.76, /*blue_i*/ 0.65, /*red_m*/ 0.37, /*green_m*/ 0.60, /*blue_m*/ 1.65, /*label*/ "Kodak 5279" },
  { /*red_size*/ 1.87, /*green_size*/ 2.60, /*blue_size*/ 2.44, /*red_i*/ 1.00, /*green_i*/ 0.76, /*blue_i*/ 0.79, /*red_m*/ 0.41, /*green_m*/ 0.60, /*blue_m*/ 1.80, /*label*/ "Kodak FX214" },
  { /*red_size*/ 0.04, /*green_size*/ 0.10, /*blue_size*/ 0.90, /*red_i*/ 0.90, /*green_i*/ 0.76, /*blue_i*/ 0.81, /*red_m*/ 0.49, /*green_m*/ 0.50, /*blue_m*/ 1.55, /*label*/ "Kodak GT5274" },
  { /*red_size*/ 0.23, /*green_size*/ 1.20, /*blue_size*/ 1.40, /*red_i*/ 0.60, /*green_i*/ 0.86, /*blue_i*/ 0.60, /*red_m*/ 0.48, /*green_m*/ 0.42, /*blue_m*/ 0.87, /*label*/ "Kodak 5217" },
  { /*red_size*/ 0.10, /*green_size*/ 1.60, /*blue_size*/ 1.91, /*red_i*/ 0.60, /*green_i*/ 0.86, /*blue_i*/ 0.73, /*red_m*/ 0.38, /*green_m*/ 0.17, /*blue_m*/ 0.87, /*label*/ "Kodak 5218" },
};

#define kParamGroupSize "grainSize"
#define kParamGroupSizeLabel "Size"
#define kParamGroupSizeHint "Grain size."

#define kParamSizeAll "grainSizeAll"
#define kParamSizeAllLabel "All"
#define kParamSizeAllHint "Global factor on grain size. Useful if working with scans which are not 2K (the preset sizes are computed for 2K scans)."
#define kParamSizeAllDefault 1.

#define kParamSizeRed "grainSizeRed"
#define kParamSizeRedLabel "Red"
#define kParamSizeRedHint "Red grain size (in pixels)."
#define kParamSizeRedDefault (gPresets[0].red_size)

#define kParamSizeGreen "grainSizeGreen"
#define kParamSizeGreenLabel "Green"
#define kParamSizeGreenHint "Green grain size (in pixels)."
#define kParamSizeGreenDefault (gPresets[0].green_size)

#define kParamSizeBlue "grainSizeBlue"
#define kParamSizeBlueLabel "Blue"
#define kParamSizeBlueHint "Blue grain size (in pixels)."
#define kParamSizeBlueDefault (gPresets[0].blue_size)


#define kParamGroupIrregularity "grainIrregularity"
#define kParamGroupIrregularityLabel "Irregularity"
#define kParamGroupIrregularityHint "Grain irregularity."

#define kParamIrregularityRed "grainIrregularityRed"
#define kParamIrregularityRedLabel "Red"
#define kParamIrregularityRedHint "Red grain irregularity."
#define kParamIrregularityRedDefault (gPresets[0].red_i)

#define kParamIrregularityGreen "grainIrregularityGreen"
#define kParamIrregularityGreenLabel "Green"
#define kParamIrregularityGreenHint "Green grain irregularity."
#define kParamIrregularityGreenDefault (gPresets[0].green_i)

#define kParamIrregularityBlue "grainIrregularityBlue"
#define kParamIrregularityBlueLabel "Blue"
#define kParamIrregularityBlueHint "Blue grain irregularity."
#define kParamIrregularityBlueDefault (gPresets[0].blue_i)


#define kParamGroupIntensity "grainIntensity"
#define kParamGroupIntensityLabel "Intensity"
#define kParamGroupIntensityHint "Amount of grain to add to a white pixel."

#define kParamIntensityRed "grainIntensityRed"
#define kParamIntensityRedLabel "Red"
#define kParamIntensityRedHint "Amount of red grain to add to a white pixel."
#define kParamIntensityRedDefault (gPresets[0].red_m)

#define kParamIntensityGreen "grainIntensityGreen"
#define kParamIntensityGreenLabel "Green"
#define kParamIntensityGreenHint "Amount of green grain to add to a white pixel."
#define kParamIntensityGreenDefault (gPresets[0].green_m)

#define kParamIntensityBlue "grainIntensityBlue"
#define kParamIntensityBlueLabel "Blue"
#define kParamIntensityBlueHint "Amount of blue grain to add to a white pixel."
#define kParamIntensityBlueDefault (gPresets[0].blue_m)

#define kParamColorCorr "colorCorr"
#define kParamColorCorrLabel "Correlation"
#define kParamColorCorrHint "This parameter specifies the apparent colorfulness of the grain.  The value represents how closely the grain in each channel overlaps. This means that negative color correlation values decrease the amount of overlap, which increases the apparent color of the grain, while positive values decrease its colorfulness."
#define kParamColorCorrDefault 0.

#define kParamIntensityBlack "grainBlack"
#define kParamIntensityBlackLabel "Black"
#define kParamIntensityBlackHint "Amount of grain to add everywhere."
#define kParamIntensityBlackDefault 0.,0.,0.

#define kParamIntensityMinimum "grainMinimum"
#define kParamIntensityMinimumLabel "Minimum"
#define kParamIntensityMinimumHint "Minimum black level."
#define kParamIntensityMinimumDefault 0.,0.,0.


static bool gHostIsNatron   = false;

class SeGrainProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool  _doMasking;
    double _mix;
    bool _maskInvert;
    OfxPointD _renderScale;
    double _time;
    double _seed;
    //double _size[3];
    //double _irregularity[3];
    double _intensity[3];
    double _colorCorr;
    double _black[3];
    double _minimum[3];
    OFX::Matrix3x3 _invtransform[3];

public:
    SeGrainProcessorBase(OFX::ImageEffect &instance, const OFX::RenderArguments &args)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _renderScale(args.renderScale)
    , _time(args.time)
    , _seed(0)
    , _colorCorr(0.)
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) {_maskImg = v; _maskInvert = maskInvert;}

    void doMasking(bool v) {_doMasking = v;}

    void setValues(double mix,
                   double seed,
                   double size[3],
                   double irregularity[3],
                   double intensity[3],
                   double colorCorr,
                   double black[3],
                   double minimum[3])
    {
        _mix = mix;
        // set plugin parameter values
        _seed = seed;
        _colorCorr = colorCorr;
        for (int c = 0;c < 3; ++c) {
            //_size[c] = size[c];
            //_irregularity[c] = irregularity[c];
            _intensity[c] = intensity[c];
            _black[c] = black[c];
            _minimum[c] = minimum[c];

            Matrix3x3 sizeMat(1./_renderScale.x/std::max(size[c], kSizeMin), 0., 0.,
                              0., 1./_renderScale.x/std::max(size[c], kSizeMin), 0.,
                              0., 0., _time + (1+c) * seed + irregularity[c]/2.);
            double rads = irregularity[c] * 45. * M_PI / 180.;
            double ca = std::cos(rads);
            double sa = std::sin(rads);
            Matrix3x3 rotX(1, 0, 0,
                           0, ca, sa,
                           0,-sa, ca);
            Matrix3x3 rotY(0, 1, 0,
                           sa, 0, ca,
                           ca, 0, -sa);
            _invtransform[c] = rotY * rotX * sizeMat;
        }
    }

};



template <class PIX, int nComponents, int maxValue>
class SeGrainProcessor : public SeGrainProcessorBase
{
    
public:
    SeGrainProcessor(OFX::ImageEffect &instance, const OFX::RenderArguments &args)
    : SeGrainProcessorBase(instance,args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        const int octaves = 2;
        const double lacunarity = 2.;
        const double gain = 0.5;
        float unpPix[4];

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                Point3D p(x + 0.5, y + 0.5, 1);
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsToRGBA<PIX, nComponents, maxValue>(srcPix, unpPix);

                double result[3];
                for (int c = 0; c < 3; ++c) {
                    Point3D pc = _invtransform[c] * p;
                    double args[3] = { pc.x, pc.y, pc.z };
                    // process the pixel (the actual computation goes here)
                    // double fbm(int n, const SeVec3d* args) in SeExprBuiltins.cpp
                    SeExpr::FBM<3,1,false>(args, &result[c], octaves, lacunarity, gain);
                }
                if (_colorCorr != 0.) {
                    // apply color correction:
                    // "The value represents how closely the grain in each channel overlaps. This means that negative color correlation values decrease the amount of overlap, which increases the apparent color of the grain, while positive values decrease its colorfulness."
                    double l = 0.2126 * result[0] + 0.7152 * result[1] + 0.0722 * result[2]; // Rec709 color math
                    //double l = (result[0] + result[1] + result[2])/3; // average color math
                    for (int c = 0; c < 3; ++c) {
                        result[c] = result[c] * (1. - _colorCorr) + l * _colorCorr;
                    }
                }
                for (int c = 0; c < 3; ++c) {
                    unpPix[c] = std::max(_minimum[c], unpPix[c] + result[c] *(unpPix[c] * _intensity[c] + _black[c]));
                }
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(unpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SeGrainPlugin : public OFX::ImageEffect
{
public:

    /** @brief ctor */
    SeGrainPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    , _mix(0)
    , _maskApply(0)
    , _maskInvert(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                            _dstClip->getPixelComponents() == ePixelComponentAlpha));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == ePixelComponentAlpha)));
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // fetch noise parameters

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _seed = fetchDoubleParam(kParamSeed);
        _presets = fetchChoiceParam(kParamPresets);
        _sizeAll = fetchDoubleParam(kParamSizeAll);
        _sizeRed = fetchDoubleParam(kParamSizeRed);
        _sizeGreen = fetchDoubleParam(kParamSizeGreen);
        _sizeBlue = fetchDoubleParam(kParamSizeBlue);
        _irregularityRed = fetchDoubleParam(kParamIrregularityRed);
        _irregularityGreen = fetchDoubleParam(kParamIrregularityGreen);
        _irregularityBlue = fetchDoubleParam(kParamIrregularityBlue);
        _intensityRed = fetchDoubleParam(kParamIntensityRed);
        _intensityGreen = fetchDoubleParam(kParamIntensityGreen);
        _intensityBlue = fetchDoubleParam(kParamIntensityBlue);
        _colorCorr = fetchDoubleParam(kParamColorCorr);
        _intensityBlack = fetchRGBParam(kParamIntensityBlack);
        _intensityMinimum = fetchRGBParam(kParamIntensityMinimum);
        assert(_seed && _presets && _sizeAll && _sizeRed && _sizeGreen && _sizeBlue && _irregularityRed && _irregularityGreen && _irregularityBlue && _intensityRed && _intensityGreen && _intensityBlue && _colorCorr && _intensityBlack && _intensityMinimum);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_sublabel);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(SeGrainProcessorBase &, const OFX::RenderArguments &args);

    bool getInverseTransformCanonical(double time, OFX::Matrix3x3* invtransform) const;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        clipPreferences.setOutputFrameVarying(true);
        clipPreferences.setOutputHasContinousSamples(true);
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;

    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;

    DoubleParam* _seed;
    ChoiceParam* _presets;
    DoubleParam* _sizeAll;
    DoubleParam* _sizeRed;
    DoubleParam* _sizeGreen;
    DoubleParam* _sizeBlue;
    DoubleParam* _irregularityRed;
    DoubleParam* _irregularityGreen;
    DoubleParam* _irregularityBlue;
    DoubleParam* _intensityRed;
    DoubleParam* _intensityGreen;
    DoubleParam* _intensityBlue;
    DoubleParam* _colorCorr;
    RGBParam* _intensityBlack;
    RGBParam* _intensityMinimum;
    StringParam* _sublabel;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
SeGrainPlugin::setupAndProcess(SeGrainProcessorBase &processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if (mask.get()) {
        if (mask->getRenderScale().x != args.renderScale.x ||
            mask->getRenderScale().y != args.renderScale.y ||
            (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);

    // fetch grain parameter values

    double mix;
    _mix->getValueAtTime(time, mix);

    double seed;
    _seed->getValueAtTime(time, seed);
    double sizeAll;
    double size[3];
    _sizeAll->getValueAtTime(time, sizeAll);
    _sizeRed->getValueAtTime(time, size[0]);
    _sizeGreen->getValueAtTime(time, size[1]);
    _sizeBlue->getValueAtTime(time, size[2]);
    size[0] *= sizeAll;
    size[1] *= sizeAll;
    size[2] *= sizeAll;
    double irregularity[3];
    _irregularityRed->getValueAtTime(time, irregularity[0]);
    _irregularityGreen->getValueAtTime(time, irregularity[1]);
    _irregularityBlue->getValueAtTime(time, irregularity[2]);
    double intensity[3];
    _intensityRed->getValueAtTime(time, intensity[0]);
    _intensityGreen->getValueAtTime(time, intensity[1]);
    _intensityBlue->getValueAtTime(time, intensity[2]);
    double colorCorr;
    _colorCorr->getValueAtTime(time, colorCorr);
    double black[3];
    _intensityBlack->getValueAtTime(time, black[0], black[1], black[2]);
    double minimum[3];
    _intensityMinimum->getValueAtTime(time, minimum[0], minimum[1], minimum[2]);

    processor.setValues(mix, seed, size, irregularity, intensity, colorCorr, black, minimum);
    processor.process();
}

// the overridden render function
void
SeGrainPlugin::render(const OFX::RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
        case OFX::ePixelComponentRGBA:
            renderForComponents<4>(args);
            break;
        case OFX::ePixelComponentRGB:
            renderForComponents<3>(args);
            break;
        case OFX::ePixelComponentXY:
            renderForComponents<2>(args);
            break;
        case OFX::ePixelComponentAlpha:
            renderForComponents<1>(args);
            break;
        default:
            //std::cout << "components usupported\n";
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
            break;
    } // switch
    //std::cout << "render! OK\n";
}

template<int nComponents>
void
SeGrainPlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte:
            renderForBitDepth<unsigned char, nComponents, 255>(args);
            break;

        case OFX::eBitDepthUShort:
            renderForBitDepth<unsigned short, nComponents, 65535>(args);
            break;

        case OFX::eBitDepthFloat:
            renderForBitDepth<float, nComponents, 1>(args);
            break;
        default:
            //std::cout << "depth usupported\n";
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
SeGrainPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    SeGrainProcessor<PIX, nComponents, maxValue> fred(*this, args);
    setupAndProcess(fred, args);
}


bool
SeGrainPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    //std::cout << "isIdentity!\n";
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;
        return true;
    }

    // TODO: which plugin parameter values give identity?
    //if (...) {
    //    identityClip = _srcClip;
    //    //std::cout << "isIdentity! true\n";
    //    return true;
    //}

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

    //std::cout << "isIdentity! false\n";
    return false;
}

void
SeGrainPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                         const std::string &paramName)
{
    const double time = args.time;
    if (paramName == kParamPresets && args.reason == OFX::eChangeUserEdit) {
        int preset;
        _presets->getValueAtTime(time, preset);
        if (preset >= NUMPRESETS) {
            _sublabel->setValue("");
        } else {
            _sublabel->setValue(gPresets[preset].label);
            _sizeAll->setValue(1.);
            _sizeRed->setValue(gPresets[preset].red_size);
            _sizeGreen->setValue(gPresets[preset].green_size);
            _sizeBlue->setValue(gPresets[preset].blue_size);
            _irregularityRed->setValue(gPresets[preset].red_i);
            _irregularityGreen->setValue(gPresets[preset].green_i);
            _irregularityBlue->setValue(gPresets[preset].blue_i);
            _intensityRed->setValue(gPresets[preset].red_m);
            _intensityGreen->setValue(gPresets[preset].green_m);
            _intensityBlue->setValue(gPresets[preset].blue_m);
            _colorCorr->setValue(0.);
            _intensityBlack->setValue(0., 0., 0.);
            _intensityMinimum->setValue(0., 0., 0.);
        }
    }
}

class SeGrainOverlayDescriptor : public DefaultEffectOverlayDescriptor<SeGrainOverlayDescriptor, OFX::OverlayInteractFromHelpers2<TransformInteractHelper, RampInteractHelper> > {};

mDeclarePluginFactory(SeGrainPluginFactory, {}, {});

void SeGrainPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setOverlayInteractDescriptor(new SeGrainOverlayDescriptor);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}


void SeGrainPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
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
    
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSeed);
        param->setLabel(kParamSeedLabel);
        param->setHint(kParamSeedHint);
        param->setDefault(kParamSeedDefault);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(-DBL_MAX, DBL_MAX);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamPresets);
        param->setLabel(kParamPresetsLabel);
        param->setHint(kParamPresetsHint);
        for (int i = 0; i < NUMPRESETS; ++i) {
            param->appendOption(gPresets[i].label);
        }
        param->appendOption(kParamPresetsOptionOther);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupSize);
        if (group) {
            group->setLabel(kParamGroupSizeLabel);
            group->setHint(kParamGroupSizeHint);
            group->setOpen(true);
        }
        
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSizeAll);
            param->setLabel(kParamSizeAllLabel);
            param->setHint(kParamSizeAllHint);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 100.);
            param->setDefault(kParamSizeAllDefault);
            param->setDoubleType(eDoubleTypeScale);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSizeRed);
            param->setLabel(kParamSizeRedLabel);
            param->setHint(kParamSizeRedHint);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 100.);
            param->setDefault(kParamSizeRedDefault);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSizeGreen);
            param->setLabel(kParamSizeGreenLabel);
            param->setHint(kParamSizeGreenHint);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 100.);
            param->setDefault(kParamSizeGreenDefault);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSizeBlue);
            param->setLabel(kParamSizeBlueLabel);
            param->setHint(kParamSizeBlueHint);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 100.);
            param->setDefault(kParamSizeBlueDefault);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        if (page) {
            page->addChild(*group);
        }
    }

    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupIrregularity);
        if (group) {
            group->setLabel(kParamGroupIrregularityLabel);
            group->setHint(kParamGroupIrregularityHint);
            group->setOpen(true);
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamIrregularityRed);
            param->setLabel(kParamIrregularityRedLabel);
            param->setHint(kParamIrregularityRedHint);
            param->setDefault(kParamIrregularityRedDefault);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamIrregularityGreen);
            param->setLabel(kParamIrregularityGreenLabel);
            param->setHint(kParamIrregularityGreenHint);
            param->setDefault(kParamIrregularityGreenDefault);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamIrregularityBlue);
            param->setLabel(kParamIrregularityBlueLabel);
            param->setHint(kParamIrregularityBlueHint);
            param->setDefault(kParamIrregularityBlueDefault);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        if (page) {
            page->addChild(*group);
        }
    }

    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupIntensity);
        if (group) {
            group->setLabel(kParamGroupIntensityLabel);
            group->setHint(kParamGroupIntensityHint);
            group->setOpen(true);
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamIntensityRed);
            param->setLabel(kParamIntensityRedLabel);
            param->setHint(kParamIntensityRedHint);
            param->setDefault(kParamIntensityRedDefault);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamIntensityGreen);
            param->setLabel(kParamIntensityGreenLabel);
            param->setHint(kParamIntensityGreenHint);
            param->setDefault(kParamIntensityGreenDefault);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamIntensityBlue);
            param->setLabel(kParamIntensityBlueLabel);
            param->setHint(kParamIntensityBlueHint);
            param->setDefault(kParamIntensityBlueDefault);
            param->setRange(0., 1.);
            param->setDisplayRange(0., 1.);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamColorCorr);
            param->setLabel(kParamColorCorrLabel);
            param->setHint(kParamColorCorrHint);
            param->setDefault(kParamColorCorrDefault);
            param->setRange(-1., 1.);
            param->setDisplayRange(-1., 1.);

            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::RGBParamDescriptor* param = desc.defineRGBParam(kParamIntensityBlack);
            param->setLabel(kParamIntensityBlackLabel);
            param->setHint(kParamIntensityBlackHint);
            param->setDefault(kParamIntensityBlackDefault);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::RGBParamDescriptor* param = desc.defineRGBParam(kParamIntensityMinimum);
            param->setLabel(kParamIntensityMinimumLabel);
            param->setHint(kParamIntensityMinimumHint);
            param->setDisplayRange(0., 0., 0., 0.01, 0.01, 0.01);
            param->setDefault(kParamIntensityMinimumDefault);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        if (page) {
            page->addChild(*group);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);

    // sublabel
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecret(true); // always secret
        param->setEnabled(false);
        param->setIsPersistant(true);
        param->setEvaluateOnChange(false);
        param->setDefault(gPresets[0].label);
        if (page) {
            page->addChild(*param);
        }
    }

    //std::cout << "describeInContext! OK\n";
}

OFX::ImageEffect* SeGrainPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new SeGrainPlugin(handle);
}


static SeGrainPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
