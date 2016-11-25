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
 * OFX SeNoise plugin.
 */

//#define SENOISE_PERLIN // perlin() is not in the open source version of SeExpr, although it is mentionned in the SeExpr doc
#define SENOISE_VORONOI

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>
//#include <iostream>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
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
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#ifdef DEBUG
#include <cstdio>
#define DBG(x) x
#else
#define DBG(x) (void)0
#endif

#include "ofxsMacros.h"

GCC_DIAG_OFF(deprecated)
#include <SeNoise.h>
#include <SeExprBuiltins.h>
GCC_DIAG_ON(deprecated)

#ifdef SENOISE_VORONOI
// SeExprBuiltins.cpp doesn't export Voronoi functions and data structures, see https://github.com/wdas/SeExpr/issues/32
#include <SeExprNode.h>
namespace SeExpr {
struct VoronoiPointData
    : public SeExprFuncNode::Data
{
    SeVec3d points[27];
    SeVec3d cell;
    double jitter;
    VoronoiPointData() : jitter(-1) {}
};

SeVec3d voronoiFn(VoronoiPointData& data, int n, const SeVec3d* args);
SeVec3d cvoronoiFn(VoronoiPointData& data, int n, const SeVec3d* args);
SeVec3d pvoronoiFn(VoronoiPointData& data, int n, const SeVec3d* args);
}
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
// fix SePlatform.h's bad defines, see https://github.com/wdas/SeExpr/issues/33
#undef snprintf
#undef strtok_r
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#  if defined(_MSC_VER) && _MSC_VER >= 1400
#    define strtok_r(s, d, p) strtok_s(s, d, p)
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsRamp.h"
#include "ofxsTransformInteract.h"
#include "ofxsMatrix2D.h"

using namespace OFX;

using std::string;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "SeNoise"
#define kPluginGrouping "Draw"
#define kPluginDescription "Generate noise."
#define kPluginIdentifier "net.sf.openfx.SeNoise"
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

#define kParamReplace "replace"
#define kParamReplaceLabel "Replace"
#define kParamReplaceHint "Clear the selected channel(s) before drawing into them."

#define kParamNoiseType "noiseType"
#define kParamNoiseTypeLabel "Noise Type"
#define kParamNoiseTypeHint "Kind of noise."
#define kParamNoiseTypeCellNoise "Cell Noise"
#define kParamNoiseTypeCellNoiseHint "Cell noise generates a field of constant colored cubes based on the integer location.  This is the same as the prman cellnoise function. You may want to set xRotate and yRotate to 0 in the Transform tab to get square cells."
#define kParamNoiseTypeNoise "Noise"
#define kParamNoiseTypeNoiseHint "Noise is a random function that smoothly blends between samples at integer locations.  This is Ken Perlin's original noise function."
#ifdef SENOISE_PERLIN
#define kParamNoiseTypePerlin "Perlin"
#define kParamNoiseTypePerlinHint "\"Improved Perlin Noise\", based on Ken Perlin's 2002 Java reference code."
#endif
#define kParamNoiseTypeFBM "FBM"
#define kParamNoiseTypeFBMHint "FBM (Fractal Brownian Motion) is a multi-frequency noise function.  The base frequency is the same as the \"Noise\" function.  The total number of frequencies is controlled by octaves.  The lacunarity is the spacing between the frequencies - a value of 2 means each octave is twice the previous frequency.  The gain controls how much each frequency is scaled relative to the previous frequency."
#define kParamNoiseTypeTurbulence "Turbulence"
#define kParamNoiseTypeTurbulenceHint "turbulence is a variant of fbm where the absolute value of each noise term is taken.  This gives a more billowy appearance."
#ifdef SENOISE_VORONOI
#define kParamNoiseTypeVoronoi "Voronoi"
#define kParamNoiseTypeVoronoiHint "Voronoi is a cellular noise pattern. It is a jittered variant of cellnoise. The type parameter describes different variants of the noise function.  The jitter param controls how irregular the pattern is (jitter = 0 is like ordinary cellnoise).  The fbm* params can be used to distort the noise field.  When fbmScale is zero (the default), there is no distortion.  The remaining params are the same as for the fbm function. NOTE: This does not necessarily return [0,1] value, because it can return arbitrary distance."
#endif
enum NoiseTypeEnum
{
    eNoiseTypeCellNoise,
    eNoiseTypeNoise,
#ifdef SENOISE_PERLIN
    eNoiseTypePerlin,
#endif
    eNoiseTypeFBM,
    eNoiseTypeTurbulence,
#ifdef SENOISE_VORONOI
    eNoiseTypeVoronoi,
#endif
};

#define kParamNoiseTypeDefault eNoiseTypeFBM

#define kParamNoiseSize "noiseSize"
#define kParamNoiseSizeLabel "Noise Size"
#define kParamNoiseSizeHint "Size of noise in pixels, corresponding to its lowest frequency."
#define kParamNoiseSizeDefault 350., 350.

#define kParamNoiseZ "noiseZ"
#define kParamNoiseZLabel "Z0"
#define kParamNoiseZHint "Z coordinate on the noise at frame=0. The noise pattern is different for every integer value of Z, so this can be used as a random seed."
#define kParamNoiseZDefault 0.

#define kParamNoiseZSlope "noiseZSlope"
#define kParamNoiseZSlopeLabel "Z Slope"
#define kParamNoiseZSlopeHint "Z is computed as Z = Z0 + frame * Z_slope. 0 means a constant noise, 1 means a different noise pattern at every frame."
#define kParamNoiseZSlopeDefault 0.

#ifdef SENOISE_VORONOI
#define kParamVoronoiType "voronoiType"
#define kParamVoronoiTypeLabel "Voronoi Type"
#define kParamVoronoiTypeHint "Different variants of the Voronoi noise function."
#define kParamVoronoiTypeCell "Cell"
#define kParamVoronoiType2 "Type 2"
#define kParamVoronoiType3 "Type 3"
#define kParamVoronoiType4 "Type 4"
#define kParamVoronoiType5 "Type 5"
enum VoronoiTypeEnum
{
    eVoronoiTypeCell,
    eVoronoiType2,
    eVoronoiType3,
    eVoronoiType4,
    eVoronoiType5,
};

#define kParamVoronoiTypeDefault eVoronoiTypeCell

#define kParamJitter "jitter"
#define kParamJitterLabel "Jitter"
#define kParamJitterHint "The jitter param controls how irregular the pattern is (jitter = 0 is like ordinary cellnoise)."
#define kParamJitterDefault 0.5

#define kParamFBMScale "fbmScale"
#define kParamFBMScaleLabel "FBM Scale"
#define kParamFBMScaleHint "The fbm* params can be used to distort the noise field.  When fbmScale is zero (the default), there is no distortion."
#define kParamFBMScaleDefault 0.

#endif

#define kParamOctaves "fbmOctaves"
#define kParamOctavesLabel "Octaves"
#define kParamOctavesHint "The total number of frequencies is controlled by octaves."
#define kParamOctavesDefault 6

#define kParamLacunarity "fbmLacunarity"
#define kParamLacunarityLabel "Lacunarity"
#define kParamLacunarityHint "The lacunarity is the spacing between the frequencies - a value of 2 means each octave is twice the previous frequency."
#define kParamLacunarityDefault 2.

#define kParamGain "fbmGain"
#define kParamGainLabel "Gain"
#define kParamGainHint "The gain controls how much each frequency is scaled relative to the previous frequency."
#define kParamGainDefault 0.5

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "The gamma output for noise."
#define kParamGammaDefault 1.

#define kParamXRotate "XRotate"
#define kParamXRotateLabel "X Rotate"
#define kParamXRotateHint "Rotation about the X axis in the 3D noise space (X,Y,Z). Noise artifacts may appear if it is 0 or a multiple of 90."
#define kParamXRotateDefault 27.

#define kParamYRotate "YRotate"
#define kParamYRotateLabel "Y Rotate"
#define kParamYRotateHint "Rotation about the Y axis in the 3D noise space (X,Y,Z). Noise artifacts may appear if it is 0 or a multiple of 90."
#define kParamYRotateDefault 37.

#define kPageTransform "transformPage"
#define kPageTransformLabel "Transform"
#define kPageTransformHint "Transform applied to the noise"

#define kGroupTransform "transformGroup"

#define kPageColor "colorPage"
#define kPageColorLabel "Color"
#define kPageColorHint "Color properties of the noise"

#define kGroupColor "colorGroup"


static bool gHostIsNatron   = false;

class SeNoiseProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;
    // plugin parameter values
    bool _replace;
    NoiseTypeEnum _noiseType;
#ifdef SENOISE_VORONOI
    VoronoiTypeEnum _voronoiType;
    double _jitter;
    double _fbmScale;
#endif
    int _octaves;
    double _lacunarity;
    double _gain;
    Matrix3x3 _invtransform;
    RampTypeEnum _type;
    OfxPointD _point0;
    OfxRGBAColourD _color0;
    OfxPointD _point1;
    OfxRGBAColourD _color1;
    OfxPointD _renderScale;

public:
    SeNoiseProcessorBase(ImageEffect &instance,
                         const RenderArguments &args)
        : ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        // initialize plugin parameter values
        , _replace(false)
        , _noiseType(eNoiseTypeCellNoise)
#ifdef SENOISE_VORONOI
        , _voronoiType(eVoronoiTypeCell)
        , _jitter(0.5)
        , _fbmScale(0.)
#endif
        , _octaves(6)
        , _lacunarity(2.)
        , _gain(0.5)
        , _invtransform()
        , _type(eRampTypeNone)
        , _point0()
        , _color0()
        , _point1()
        , _color1()
        , _renderScale(args.renderScale)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   bool replace,
                   NoiseTypeEnum noiseType,
#ifdef SENOISE_VORONOI
                   VoronoiTypeEnum voronoiType,
                   double jitter,
                   double fbmScale,
#endif
                   int octaves,
                   double lacunarity,
                   double gain,
                   const Matrix3x3& invtransform,
                   RampTypeEnum type,
                   const OfxPointD& point0,
                   const OfxRGBAColourD& color0,
                   const OfxPointD& point1,
                   const OfxRGBAColourD& color1)
    {
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        // set plugin parameter values
        _replace = replace;
        _noiseType = noiseType;
#ifdef SENOISE_VORONOI
        _voronoiType = voronoiType;
        _jitter = jitter;
        _fbmScale = fbmScale;
#endif
        _octaves = octaves;
        _lacunarity = lacunarity;
        _gain = gain;
        _invtransform = invtransform;
        _type = type;
        _point0 = point0;
        _color0 = color0;
        _point1 = point1;
        _color1 = color1;
    }
};


template <class PIX, int nComponents, int maxValue>
class SeNoiseProcessor
    : public SeNoiseProcessorBase
{
public:
    SeNoiseProcessor(ImageEffect &instance,
                     const RenderArguments &args)
        : SeNoiseProcessorBase(instance, args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        const bool processR = _processR && (nComponents != 1);
        const bool processG = _processG && (nComponents >= 2);
        const bool processB = _processB && (nComponents >= 3);
        const bool processA = _processA && (nComponents == 1 || nComponents == 4);

        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
#ifdef SENOISE_VORONOI
        SeExpr::VoronoiPointData voronoiPointData;
#endif
        const double norm2 = (_point1.x - _point0.x) * (_point1.x - _point0.x) + (_point1.y - _point0.y) * (_point1.y - _point0.y);
        const double nx = norm2 == 0. ? 0. : (_point1.x - _point0.x) / norm2;
        const double ny = norm2 == 0. ? 0. : (_point1.y - _point0.y) / norm2;

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsToRGBA<PIX, nComponents, maxValue>(srcPix, unpPix);
                double t_r = _replace ? 0. : unpPix[0];
                double t_g = _replace ? 0. : unpPix[1];
                double t_b = _replace ? 0. : unpPix[2];
                double t_a = _replace ? 0. : unpPix[3];
                Point3D p(x + 0.5, y + 0.5, 1);
                p = _invtransform * p;
                double args[3] = { p.x, p.y, p.z };
                // process the pixel (the actual computation goes here)
                double result;
                switch (_noiseType) {
                case eNoiseTypeCellNoise: {
                    // double cellnoise(const SeVec3d& p)
                    SeExpr::CellNoise<3, 1>(args, &result);
                    break;
                }
                case eNoiseTypeNoise: {
                    // double noise(int n, const SeVec3d* args)
                    SeExpr::Noise<3, 1>(args, &result);
                    result = .5 * result + .5;
                    break;
                }
#ifdef SENOISE_PERLIN
                case eNoiseTypePerlin: {
                    n = SeExpr::perlin(1, &p);
                    break;
                }
#endif
                case eNoiseTypeFBM: {
                    // double fbm(int n, const SeVec3d* args) in SeExprBuiltins.cpp
                    SeExpr::FBM<3, 1, false>(args, &result, _octaves, _lacunarity, _gain);
                    result = .5 * result + .5;
                    break;
                }
                case eNoiseTypeTurbulence: {
                    // double turbulence(int n, const SeVec3d* args)
                    SeExpr::FBM<3, 1, true>(args, &result, _octaves, _lacunarity, _gain);
                    break;
                    //result = .5*result+.5;
                }
#ifdef SENOISE_VORONOI
                case eNoiseTypeVoronoi: {
                    SeVec3d args[7];
                    args[0].setValue(p.x, p.y, p.z);
                    args[1][0] = (int)_voronoiType + 1;
                    args[2][0] = _jitter;
                    args[3][0] = _fbmScale;
                    args[4][0] = _octaves;
                    args[5][0] = _lacunarity;
                    args[6][0] = _gain;
                    result = SeExpr::voronoiFn(voronoiPointData, 7, args)[0];
                    break;
                }
#endif
                }
                //result = result*result; // gamma = 0.5 (TODO: gamma param)

                // combine with ramp color
                OfxRGBAColourD rampColor;
                if (_type == eRampTypeNone) {
                    rampColor = _color1;
                } else {
                    OfxPointI p_pixel;
                    OfxPointD p;
                    p_pixel.x = x;
                    p_pixel.y = y;
                    Coords::toCanonical(p_pixel, _dstImg->getRenderScale(), _dstImg->getPixelAspectRatio(), &p);

                    double t = ofxsRampFunc(_point0, nx, ny, _type, p);

                    rampColor.r = _color0.r * (1 - t) + _color1.r * t;
                    rampColor.g = _color0.g * (1 - t) + _color1.g * t;
                    rampColor.b = _color0.b * (1 - t) + _color1.b * t;
                    rampColor.a = _color0.a * (1 - t) + _color1.a * t;
                }
                // coverity[dead_error_line]
                tmpPix[0] = processR ? t_r * (1. - result) + rampColor.r * result : unpPix[0];
                // coverity[dead_error_line]
                tmpPix[1] = processG ? t_g * (1. - result) + rampColor.g * result : unpPix[1];
                // coverity[dead_error_line]
                tmpPix[2] = processB ? t_b * (1. - result) + rampColor.b * result : unpPix[2];
                // coverity[dead_error_line]
                tmpPix[3] = processA ? t_a * (1. - result) + rampColor.a * result : unpPix[3];

                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SeNoisePlugin
    : public ImageEffect
{
public:

    /** @brief ctor */
    SeNoisePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _replace(0)
        , _mix(0)
        , _maskApply(0)
        , _maskInvert(0)
        , _noiseType(0)
        , _noiseSize(0)
        , _noiseZ(0)
        , _noiseZSlope(0)
#ifdef SENOISE_VORONOI
        , _voronoiType(0)
        , _jitter(0)
        , _fbmScale(0)
#endif
        , _octaves(0)
        , _lacunarity(0)
        , _gain(0)
        , _pageTransform(0)
        , _groupTransform(0)
        , _translate(0)
        , _rotate(0)
        , _scale(0)
        , _scaleUniform(0)
        , _skewX(0)
        , _skewY(0)
        , _skewOrder(0)
        , _center(0)
        , _transformInteractOpen(0)
        , _transformInteractive(0)
        , _xRotate(0)
        , _yRotate(0)
        , _pageColor(0)
        , _groupColor(0)
        , _groupColorIsOpen(false)
        , _point0(0)
        , _color0(0)
        , _point1(0)
        , _color1(0)
        , _type(0)
        , _rampInteractOpen(0)
        , _rampInteractive(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // fetch noise parameters

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kNatronOfxParamProcessR);
        _processG = fetchBooleanParam(kNatronOfxParamProcessG);
        _processB = fetchBooleanParam(kNatronOfxParamProcessB);
        _processA = fetchBooleanParam(kNatronOfxParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _replace = fetchBooleanParam(kParamReplace);
        assert(_replace);

        _noiseType = fetchChoiceParam(kParamNoiseType);
        _noiseSize = fetchDouble2DParam(kParamNoiseSize);
        _noiseZ = fetchDoubleParam(kParamNoiseZ);
        _noiseZSlope = fetchDoubleParam(kParamNoiseZSlope);
#ifdef SENOISE_VORONOI
        _voronoiType = fetchChoiceParam(kParamVoronoiType);
        _jitter = fetchDoubleParam(kParamJitter);
        _fbmScale = fetchDoubleParam(kParamFBMScale);
#endif
        _octaves = fetchIntParam(kParamOctaves);
        _lacunarity = fetchDoubleParam(kParamLacunarity);
        _gain = fetchDoubleParam(kParamGain);
        assert(_noiseType && _noiseSize && _noiseZ && _noiseZSlope &&
#ifdef SENOISE_VORONOI
               _voronoiType && _jitter && _fbmScale &&
#endif
               _octaves && _lacunarity && _gain);

        if ( paramExists(kPageTransform) ) {
            _pageTransform = fetchPageParam(kPageTransform);
            assert(_pageTransform);
        }
        if ( paramExists(kGroupTransform) ) {
            _groupTransform = fetchGroupParam(kGroupTransform);
            assert(_groupTransform);
        }
        _translate = fetchDouble2DParam(kParamTransformTranslate);
        _rotate = fetchDoubleParam(kParamTransformRotate);
        _scale = fetchDouble2DParam(kParamTransformScale);
        _scaleUniform = fetchBooleanParam(kParamTransformScaleUniform);
        _skewX = fetchDoubleParam(kParamTransformSkewX);
        _skewY = fetchDoubleParam(kParamTransformSkewY);
        _skewOrder = fetchChoiceParam(kParamTransformSkewOrder);
        _center = fetchDouble2DParam(kParamTransformCenter);
        _transformInteractOpen = fetchBooleanParam(kParamTransformInteractOpen);
        _transformInteractive = fetchBooleanParam(kParamTransformInteractive);
        assert(_translate && _rotate && _scale && _scaleUniform && _skewX && _skewY && _skewOrder && _center && _transformInteractive);
        _xRotate = fetchDoubleParam(kParamXRotate);
        _yRotate = fetchDoubleParam(kParamYRotate);

        if ( paramExists(kPageColor) ) {
            _pageColor = fetchPageParam(kPageColor);
            assert(_pageColor);
        }
        if ( paramExists(kGroupColor) ) {
            _groupColor = fetchGroupParam(kGroupColor);
            assert(_groupColor);
            // get the initial state
            //_groupColor->setOpen(_groupColor->getIsOpen());
            _groupColorIsOpen = _groupColor->getIsOpen();
        }
        _point0 = fetchDouble2DParam(kParamRampPoint0);
        _point1 = fetchDouble2DParam(kParamRampPoint1);
        _color0 = fetchRGBAParam(kParamRampColor0);
        _color1 = fetchRGBAParam(kParamRampColor1);
        _type = fetchChoiceParam(kParamRampType);
        _rampInteractOpen = fetchBooleanParam(kParamRampInteractOpen);
        _rampInteractive = fetchBooleanParam(kParamRampInteractive);
        assert(_point0 && _point1 && _color0 && _color1 && _type && _rampInteractive);

        // update visibility
        InstanceChangedArgs args = { eChangeUserEdit, 0., {0., 0.}};
        changedParam(args, kParamNoiseType);
        changedParam(args, kParamRampType);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(SeNoiseProcessorBase &, const RenderArguments &args);

    bool getInverseTransformCanonical(double time, Matrix3x3* invtransform) const;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        double noiseZSlope;

        _noiseZSlope->getValue(noiseZSlope);
        if (noiseZSlope != 0.) {
            clipPreferences.setOutputFrameVarying(true);
            clipPreferences.setOutputHasContinousSamples(true);
        }
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    BooleanParam* _replace;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    ChoiceParam* _noiseType;
    Double2DParam* _noiseSize;
    DoubleParam* _noiseZ;
    DoubleParam* _noiseZSlope;
#ifdef SENOISE_VORONOI
    ChoiceParam* _voronoiType;
    DoubleParam* _jitter;
    DoubleParam* _fbmScale;
#endif
    IntParam* _octaves;
    DoubleParam* _lacunarity;
    DoubleParam* _gain;
    PageParam* _pageTransform;
    GroupParam* _groupTransform;
    Double2DParam* _translate;
    DoubleParam* _rotate;
    Double2DParam* _scale;
    BooleanParam* _scaleUniform;
    DoubleParam* _skewX;
    DoubleParam* _skewY;
    ChoiceParam* _skewOrder;
    Double2DParam* _center;
    BooleanParam* _transformInteractOpen;
    BooleanParam* _transformInteractive;
    DoubleParam* _xRotate;
    DoubleParam* _yRotate;
    PageParam* _pageColor;
    GroupParam* _groupColor;
    bool _groupColorIsOpen;
    Double2DParam* _point0;
    RGBAParam* _color0;
    Double2DParam* _point1;
    RGBAParam* _color1;
    ChoiceParam* _type;
    BooleanParam* _rampInteractOpen;
    BooleanParam* _rampInteractive;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
SeNoisePlugin::setupAndProcess(SeNoiseProcessorBase &processor,
                               const RenderArguments &args)
{
    const double time = args.time;

    std::auto_ptr<Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    // fetch noise parameter values
    int noiseType_i;
    _noiseType->getValueAtTime(time, noiseType_i);
    NoiseTypeEnum noiseType = (NoiseTypeEnum)noiseType_i;
    OfxPointD noiseSize;
    _noiseSize->getValueAtTime(time, noiseSize.x, noiseSize.y);

    double noiseZ;
    _noiseZ->getValueAtTime(time, noiseZ);
    double noiseZSlope;
    _noiseZSlope->getValueAtTime(time, noiseZSlope);
#ifdef SENOISE_VORONOI
    VoronoiTypeEnum voronoiType = eVoronoiTypeCell;
    double jitter = 0.5;
    double fbmScale = 0.;
    if (noiseType == eNoiseTypeVoronoi) {
        int voronoiType_i;
        _voronoiType->getValueAtTime(time, voronoiType_i);
        voronoiType = (VoronoiTypeEnum)voronoiType_i;
        _jitter->getValueAtTime(time, jitter);
        _fbmScale->getValueAtTime(time, fbmScale);
    }
#endif
    int octaves = 6;
    double lacunarity = 2.;
    double gain = 0.5;
    if ( (noiseType == eNoiseTypeFBM) || noiseType == eNoiseTypeTurbulence
#ifdef SENOISE_VORONOI
         || noiseType == eNoiseTypeVoronoi
#endif
          ) {
        _octaves->getValueAtTime(time, octaves);
        _lacunarity->getValueAtTime(time, lacunarity);
        _gain->getValueAtTime(time, gain);
    }

    // TODO: transform parameters

    // Ramp parameters
    int type_i;
    _type->getValueAtTime(time, type_i);
    RampTypeEnum type = (RampTypeEnum)type_i;
    OfxPointD point0;
    _point0->getValueAtTime(time, point0.x, point0.y);
    OfxRGBAColourD color0;
    _color0->getValueAtTime(time, color0.r, color0.g, color0.b, color0.a);
    OfxPointD point1;
    _point1->getValueAtTime(time, point1.x, point1.y);
    OfxRGBAColourD color1;
    _color1->getValueAtTime(time, color1.r, color1.g, color1.b, color1.a);

    double mix;
    _mix->getValueAtTime(time, mix);

    bool processR, processG, processB, processA, replace;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);
    _replace->getValueAtTime(time, replace);

    Matrix3x3 sizeMat(1. / args.renderScale.x / noiseSize.x, 0., 0.,
                      0., 1. / args.renderScale.x / noiseSize.y, 0.,
                      0., 0., noiseZ + time * noiseZSlope);
    Matrix3x3 invtransform;
    getInverseTransformCanonical(time, &invtransform);

    double xRotate, yRotate, rads, c, s;
    _xRotate->getValueAtTime(time, xRotate);
    rads = xRotate * M_PI / 180.;
    c = std::cos(rads);
    s = std::sin(rads);
    Matrix3x3 rotX(1, 0, 0,
                   0, c, s,
                   0, -s, c);
    _yRotate->getValueAtTime(time, yRotate);
    rads = xRotate * M_PI / 180.;
    c = std::cos(rads);
    s = std::sin(rads);
    Matrix3x3 rotY(0, 1, 0,
                   s, 0, c,
                   c, 0, -s);

    processor.setValues(mix,
                        processR, processG, processB, processA, replace,
                        noiseType,
#ifdef SENOISE_VORONOI
                        voronoiType, jitter, fbmScale,
#endif
                        octaves, lacunarity, gain,
                        rotY * rotX * sizeMat * invtransform,
                        type, point0, color0, point1, color1);
    processor.process();
} // SeNoisePlugin::setupAndProcess

bool
SeNoisePlugin::getInverseTransformCanonical(double time,
                                            Matrix3x3* invtransform) const
{
    // NON-GENERIC
    OfxPointD center;

    _center->getValueAtTime(time, center.x, center.y);
    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    double rotate;
    _rotate->getValueAtTime(time, rotate);
    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    OfxPointD scale;
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    double rot = ofxsToRadians(rotate);
    *invtransform = ofxsMatInverseTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);

    return true;
}

// the overridden render function
void
SeNoisePlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
    case ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    case ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
    case ePixelComponentXY:
        renderForComponents<2>(args);
        break;
    case ePixelComponentAlpha:
        renderForComponents<1>(args);
        break;
    default:
        //std::cout << "components usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
SeNoisePlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        //std::cout << "depth usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
SeNoisePlugin::renderForBitDepth(const RenderArguments &args)
{
    SeNoiseProcessor<PIX, nComponents, maxValue> fred(*this, args);
    setupAndProcess(fred, args);
}

bool
SeNoisePlugin::isIdentity(const IsIdentityArguments &args,
                          Clip * &identityClip,
                          double & /*identityTime*/)
{
    //std::cout << "isIdentity!\n";
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        _processA->getValueAtTime(time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    // TODO: which plugin parameter values give identity?
    //if (...) {
    //    identityClip = _srcClip;
    //    //std::cout << "isIdentity! true\n";
    //    return true;
    //}

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // SeNoisePlugin::isIdentity

void
SeNoisePlugin::changedParam(const InstanceChangedArgs &args,
                            const string &paramName)
{
    if ( gHostIsNatron && (paramName == kPageTransform) && (args.reason == eChangeUserEdit) ) {
        bool isOpen = _pageTransform->getIsEnable() && !_pageTransform->getIsSecret();
        //DBG(std::printf("kPageTransform=%d\n",(int)isOpen));
        _transformInteractOpen->setValue(isOpen);
    } else if ( !gHostIsNatron && (paramName == kGroupTransform) && (args.reason == eChangeUserEdit) ) {
        // we have to track the group state by ourselves:
        // as per the specs, getIsOpen() only returns the initial state
        _transformInteractOpen->setValue( !_transformInteractOpen->getValue() );
    } else if ( gHostIsNatron && (paramName == kPageColor) && (args.reason == eChangeUserEdit) ) {
        bool isOpen = _pageColor->getIsEnable() && !_pageColor->getIsSecret();
        //DBG(std::printf("kPageColor=%d\n",(int)isOpen));
        _rampInteractOpen->setValue(isOpen);
    } else if ( !gHostIsNatron && (paramName == kGroupColor) && (args.reason == eChangeUserEdit) ) {
        // we have to track the group state by ourselves:
        // as per the specs, getIsOpen() only returns the initial state
        _rampInteractOpen->setValue( !_rampInteractOpen->getValue() );
    } else if ( (paramName == kParamNoiseType) && (args.reason == eChangeUserEdit) ) {
        int noiseType_i;
        _noiseType->getValue(noiseType_i);
        NoiseTypeEnum noiseType = (NoiseTypeEnum)noiseType_i;
        bool isfbm = (noiseType == eNoiseTypeFBM) || (noiseType == eNoiseTypeTurbulence)
#ifdef SENOISE_VORONOI
                     || (noiseType == eNoiseTypeVoronoi)
#endif
        ;
#ifdef SENOISE_VORONOI
        bool isvoronoi = (noiseType == eNoiseTypeVoronoi);
        _voronoiType->setIsSecretAndDisabled(!isvoronoi);
        _jitter->setIsSecretAndDisabled(!isvoronoi);
        _fbmScale->setIsSecretAndDisabled(!isvoronoi);
#endif
        _octaves->setIsSecretAndDisabled(!isfbm);
        _lacunarity->setIsSecretAndDisabled(!isfbm);
        _gain->setIsSecretAndDisabled(!isfbm);
    } else if ( (paramName == kParamRampType) && (args.reason == eChangeUserEdit) ) {
        int type_i;
        _type->getValue(type_i);
        RampTypeEnum type = (RampTypeEnum)type_i;
        bool noramp = (type == eRampTypeNone);
        _color0->setIsSecretAndDisabled(noramp);
        _point0->setIsSecretAndDisabled(noramp);
        _point1->setIsSecretAndDisabled(noramp);
        _rampInteractOpen->setIsSecretAndDisabled(noramp);
        _rampInteractive->setIsSecretAndDisabled(noramp);
    }
}

class SeNoiseOverlayDescriptor
    : public DefaultEffectOverlayDescriptor<SeNoiseOverlayDescriptor, OverlayInteractFromHelpers2<TransformInteractHelper, RampInteractHelper> >
{
};

mDeclarePluginFactory(SeNoisePluginFactory, {}, {});

void
SeNoisePluginFactory::describe(ImageEffectDescriptor &desc)
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
    desc.setOverlayInteractDescriptor(new SeNoiseOverlayDescriptor);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}

void
SeNoisePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    gHostIsNatron = (getImageEffectHostDescription()->isNatron);

    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
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
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessR);
        param->setLabel(kNatronOfxParamProcessRLabel);
        param->setHint(kNatronOfxParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessG);
        param->setLabel(kNatronOfxParamProcessGLabel);
        param->setHint(kNatronOfxParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessB);
        param->setLabel(kNatronOfxParamProcessBLabel);
        param->setHint(kNatronOfxParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kNatronOfxParamProcessAHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamReplace);
        param->setLabel(kParamReplaceLabel);
        param->setHint(kParamReplaceHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }


    // describe plugin params
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamNoiseSize);
        param->setLabel(kParamNoiseSizeLabel);
        param->setHint(kParamNoiseSizeHint);
        param->setRange(0., 0., DBL_MAX, DBL_MAX);
        param->setDisplayRange(1., 1., 1000., 1000.);
        param->setDefault(kParamNoiseSizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamNoiseZ);
        param->setLabel(kParamNoiseZLabel);
        param->setHint(kParamNoiseZHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 5.);
        param->setDefault(kParamNoiseZDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamNoiseZSlope);
        param->setLabel(kParamNoiseZSlopeLabel);
        param->setHint(kParamNoiseZSlopeHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamNoiseZSlopeDefault);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamNoiseType);
        param->setLabel(kParamNoiseTypeLabel);
        param->setHint(kParamNoiseTypeHint);
        assert(param->getNOptions() == eNoiseTypeCellNoise);
        param->appendOption(kParamNoiseTypeCellNoise, kParamNoiseTypeCellNoiseHint);
        assert(param->getNOptions() == eNoiseTypeNoise);
        param->appendOption(kParamNoiseTypeNoise, kParamNoiseTypeNoiseHint);
#ifdef SENOISE_PERLIN
        assert(param->getNOptions() == eNoiseTypePerlin);
        param->appendOption(kParamNoiseTypePerlin, kParamNoiseTypePerlinHint);
#endif
        assert(param->getNOptions() == eNoiseTypeFBM);
        param->appendOption(kParamNoiseTypeFBM, kParamNoiseTypeFBMHint);
        assert(param->getNOptions() == eNoiseTypeTurbulence);
        param->appendOption(kParamNoiseTypeTurbulence, kParamNoiseTypeTurbulenceHint);
#ifdef SENOISE_VORONOI
        assert(param->getNOptions() == eNoiseTypeVoronoi);
        param->appendOption(kParamNoiseTypeVoronoi, kParamNoiseTypeVoronoiHint);
#endif
        param->setDefault(kParamNoiseTypeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
#ifdef SENOISE_VORONOI
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamVoronoiType);
        param->setLabel(kParamVoronoiTypeLabel);
        param->setHint(kParamVoronoiTypeHint);
        assert(param->getNOptions() == eVoronoiTypeCell);
        param->appendOption(kParamVoronoiTypeCell);
        assert(param->getNOptions() == eVoronoiType2);
        param->appendOption(kParamVoronoiType2);
        assert(param->getNOptions() == eVoronoiType3);
        param->appendOption(kParamVoronoiType3);
        assert(param->getNOptions() == eVoronoiType4);
        param->appendOption(kParamVoronoiType4);
        assert(param->getNOptions() == eVoronoiType5);
        param->appendOption(kParamVoronoiType5);
        param->setDefault(kParamVoronoiTypeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamJitter);
        param->setLabel(kParamJitterLabel);
        param->setHint(kParamJitterHint);
        param->setRange(1.e-3, 1.);
        param->setDisplayRange(1.e-3, 1.);
        param->setDefault(kParamJitterDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFBMScale);
        param->setLabel(kParamFBMScaleLabel);
        param->setHint(kParamFBMScaleHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(kParamFBMScaleDefault);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamOctaves);
        param->setLabel(kParamOctavesLabel);
        param->setHint(kParamOctavesHint);
        param->setRange(1, 1000);
        param->setDisplayRange(1, 10);
        param->setDefault(kParamOctavesDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLacunarity);
        param->setLabel(kParamLacunarityLabel);
        param->setHint(kParamLacunarityHint);
        param->setRange(1., DBL_MAX);
        param->setDisplayRange(1., 10.);
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(kParamLacunarityDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGain);
        param->setLabel(kParamGainLabel);
        param->setHint(kParamGainHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0.1, 1.);
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(kParamGainDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGamma);
        param->setLabel(kParamGammaLabel);
        param->setHint(kParamGammaHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamGammaDefault);
        param->setEnabled(false); // TODO: gamma parameter
        param->setIsSecret(true); // TODO: gamma parameter
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PageParamDescriptor *subpage = page;
        if (gHostIsNatron) {
            subpage = desc.definePageParam(kPageTransform);
            subpage->setLabel(kPageTransformLabel);
            subpage->setHint(kPageTransformHint);
        }
        GroupParamDescriptor* group = NULL;
        if (!gHostIsNatron) {
            // Natron makes separate tabs for each parameter page,
            // but we have to use a group for Nuke and possibly others
            group = desc.defineGroupParam(kGroupTransform);
            group->setLabel(kPageTransformLabel);
            group->setHint(kPageTransformHint);
            group->setOpen(false);
        }
        ofxsTransformDescribeParams(desc, subpage, group, /*isOpen=*/ false, /*oldParams=*/ false);
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamXRotate);
            param->setLabel(kParamXRotateLabel);
            param->setHint(kParamXRotateHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(0., 90.);
            param->setDoubleType(eDoubleTypeAngle);
            param->setDefault(kParamXRotateDefault);
            if (group) {
                param->setParent(*group);
            }
            if (subpage) {
                subpage->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamYRotate);
            param->setLabel(kParamYRotateLabel);
            param->setHint(kParamYRotateHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(0., 90.);
            param->setDoubleType(eDoubleTypeAngle);
            param->setDefault(kParamYRotateDefault);
            if (group) {
                param->setParent(*group);
            }
            if (subpage) {
                subpage->addChild(*param);
            }
        }

        if (subpage && group) {
            subpage->addChild(*group);
        }
    }
    {
        PageParamDescriptor *subpage = page;
        if (gHostIsNatron) {
            subpage = desc.definePageParam(kPageColor);
            subpage->setLabel(kPageColorLabel);
            subpage->setHint(kPageColorHint);
        }
        GroupParamDescriptor* group = NULL;
        if (!gHostIsNatron) {
            // Natron makes separate tabs for each parameter page,
            // but we have to use a group for Nuke and possibly others
            group = desc.defineGroupParam(kGroupColor);
            group->setLabel(kPageColorLabel);
            group->setHint(kPageColorHint);
            group->setOpen(false);
        }
        ofxsRampDescribeParams(desc, subpage, group, eRampTypeNone, /*isOpen=*/ false, /*oldParams=*/ false);
        if (subpage && group) {
            subpage->addChild(*group);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
    //std::cout << "describeInContext! OK\n";
} // SeNoisePluginFactory::describeInContext

ImageEffect*
SeNoisePluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    return new SeNoisePlugin(handle);
}

static SeNoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
