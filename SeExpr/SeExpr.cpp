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
 * OFX SeExpr plugin.
 * Execute a SeExpr script.
 */

#include <vector>
#include <algorithm>
#include <limits>
#include <set>

#include <stdio.h> // for snprintf & _snprintf
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

#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsMultiThread.h"
#include "ofxsFormatResolution.h"
#include "ofxsRectangleInteract.h"
#include "ofxsFilter.h"

#include <SeExpression.h>
#include <SeExprFunc.h>
#include <SeExprNode.h>
#include <SeExprBuiltins.h>
#include <SeMutex.h>
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

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "SeExpr"
#define kPluginGrouping "Merge"
#define kPluginDescription \
"Use the Walt Disney Animation Studio SeExpr expresion language to process pixels of the input image.\n" \
"SeExpr Home Page: http://www.disneyanimation.com/technology/seexpr.html\n" \
"SeExpr Language Documentation: http://wdas.github.io/SeExpr/doxygen/userdoc.html\n" \
"SeExpr is licensed under the Apache License v2 and is copyright of Disney Enterprises, Inc.\n\n" \
"Some extensions were added to the language for filtering and blending several input images. " \
"The following pre-defined variables can be used in the script:\n\n" \
"- x: X coordinate (in pixel units) of the pixel to render.\n\n" \
"- y: Y coordinate (in pixel units) of the pixel to render.\n\n" \
"- u: X coordinate (normalized in the [0,1] range) of the output pixel to render.\n\n" \
"- v: Y coordinate (normalized in the [0,1] range) of the output pixel to render.\n\n" \
"- sx, sy: Scale at which the image is being rendered. Depending on the zoom level " \
"of the viewer, the image might be rendered at a lower scale than usual. This parameter is useful when producing spatial " \
"effects that need to be invariant to the pixel scale, especially when using X and Y coordinates. (0.5,0.5) means that the " \
"image is being rendered at half of its original size.\n\n" \
"- par: The pixel aspect ratio.\n\n" \
"- cx, cy: Shortcuts for (x + 0.5)/par/sx and (y + 0.5)/sy, a.k.a. the canonical coordinates of the current pixel.\n\n" \
"- frame: Current frame being rendered\n\n" \
"- Cs, As: Color (RGB vector) and alpha (scalar) of the image from input 1.\n\n" \
"- CsN, AsN: Color (RGB vector) and alpha (scalar) of the image from input N, e.g. Cs2 and As2 for input 2.\n\n" \
"- output_width: Width of the output image being rendered.\n\n" \
"- output_height: Height of the output image being rendered.\n\n" \
"- input_width, input_height: Size of image from input 1, in pixels.\n\n" \
"- input_widthN, input_heightN: Size of image from input N, e.g. input_width2 and input_height2 for input 2.\n\n" \
"- color cpixel(int i, int f, float x, float y, int interp = 0): interpolates the color from input i at the pixel position (x,y) in the image, at frame f.\n" \
"- float apixel(int i, int f, float x, float y, int interp = 0): interpolates the alpha from input i at the pixel position (x,y) in the image, at frame f.\n" \
"The pixel position of the center of the bottom-left pixel is (0., 0.).\n"\
"First input has index i=1.\n"\
"'interp' controls the interpolation filter, and can take one of the following values:\n"\
"0: impulse - (nearest neighbor / box) Use original values\n"\
"1: bilinear - (tent / triangle) Bilinear interpolation between original values\n"\
"2: cubic - (cubic spline) Some smoothing\n"\
"3: Keys - (Catmull-Rom / Hermite spline) Some smoothing, plus minor sharpening (*)\n"\
"4: Simon - Some smoothing, plus medium sharpening (*)\n"\
"5: Rifman - Some smoothing, plus significant sharpening (*)\n"\
"6: Mitchell - Some smoothing, plus blurring to hide pixelation (*+)\n"\
"7: Parzen - (cubic B-spline) Greatest smoothing of all filters (+)\n"\
"8: notch - Flat smoothing (which tends to hide moire' patterns) (+)\n"\
"Some filters may produce values outside of the initial range (*) or modify the values even at integer positions (+).\n\n" \
"Usage example (Application of the Multiply Merge operator on the input 1 and 2):\n\n" \
"Cs * Cs2\n\n" \
"Another merge operator example (over):\n\n" \
"Cs + Cs2 * (1 -  As)\n\n" \
"Generating a time-varying colored Perlin noise with size x1:\n" \
"cnoise([cx/x1,cy/x1,frame])\n\n" \
"A more complex example used to average pixels over the previous, current and next frame:\n\n" \
"prev = cpixel(1,frame - 1,x,y);\n" \
"cur = Cs;\n" \
"next = cpixel(1,frame + 1,x,y);\n" \
"(prev + cur + next) / 3;\n\n" \
"To use custom variables that are pre-defined in the plug-in (scalars, positions and colors) you must reference them " \
"using their script-name in the expression. For example, the parameter x1 can be referenced using x1 in the script:\n\n" \
"Cs + x1\n\n" \
"Note that for expressions that span multiple lines, you must end each instruction by a semicolumn (';') as you would do in C/C++. The last line " \
"of your expression will always be considered as the final value of the pixel and must not be terminated by a semicolumn.\n" \
"More documentation is available on the SeExpr website: \n\n" \
"The input frame range used to render a given output frame is computed automatically if the following conditions hold:\n"\
"- The 'frame' parameter to cpixel/apixel must not depend on the color or alpha of a pixel, nor on the result of another call to cpixel/apixel\n" \
"- A call to cpixel/apixel must not depend on the color or alpha of a pixel, as in the following:\n\n" \
"if (As > 0.1) {\n" \
"    src = cpixel(1,frame,x,y);\n" \
"} else {\n" \
"    src = [0,0,0];\n" \
"}\n" \
"If one of these conditions does not hold, all frames from the specified input frame range are asked for.\n"

#define kPluginIdentifier "fr.inria.openfx.SeExpr"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
// History:
// version 1: initial version
// version 2: $scale replaced with $scalex, $scaley; added $par, $cx, $cy; getPixel replaced by cpixel/apixel

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kSourceClipCount 10
#define kParamsCount 10

#define kSeExprCPixelFuncName "cpixel"
#define kSeExprAPixelFuncName "apixel"
#define kSeExprCurrentTimeVarName "frame"
#define kSeExprXCoordVarName "x"
#define kSeExprYCoordVarName "y"
#define kSeExprUCoordVarName "u"
#define kSeExprVCoordVarName "v"
#define kSeExprPARVarName "par"
#define kSeExprXCanCoordVarName "cx"
#define kSeExprYCanCoordVarName "cy"
#define kSeExprInputWidthVarName "input_width"
#define kSeExprInputHeightVarName "input_height"
#define kSeExprOutputWidthVarName "output_width"
#define kSeExprOutputHeightVarName "output_height"
#define kSeExprColorVarName "Cs"
#define kSeExprAlphaVarName "As"
#define kSeExprRenderScaleXVarName "sx"
#define kSeExprRenderScaleYVarName "sy"

#define kSeExprDefaultRGBScript "#Just copy the source RGB\nCs"
#define kSeExprDefaultAlphaScript "#Just copy the source alpha\nAs"

#define kParamRegionOfDefinition "rod"
#define kParamRegionOfDefinitionLabel "Region of Definition"
#define kParamRegionOfDefinitionHint "Region of definition (extent) of the output."

#define kParamRegionOfDefinitionOptionUnion "Union"
#define kParamRegionOfDefinitionOptionUnionHelp "The output region is the union of the regions of definition of all connected inputs."
#define kParamRegionOfDefinitionOptionIntersection "Intersection"
#define kParamRegionOfDefinitionOptionIntersectionHelp "The output region is the intersection the regions of definition of all connected inputs."
#define kParamRegionOfDefinitionOptionSize "Size"
#define kParamRegionOfDefinitionOptionSizeHelp "The output region is the size of the rectangle overlay."
#define kParamRegionOfDefinitionOptionFormat "Format"
#define kParamRegionOfDefinitionOptionFormatHelp "The output region is the specified format."
#define kParamRegionOfDefinitionOptionProject "Project"
#define kParamRegionOfDefinitionOptionProjectHelp "The output region is the size of the project."
#define kParamRegionOfDefinitionOptionCustomInput "Input"
#define kParamRegionOfDefinitionOptionCustomInputHelp "The output region is the region of definition of input "
enum RegionOfDefinitionEnum {
    eRegionOfDefinitionOptionUnion,
    eRegionOfDefinitionOptionIntersection,
    eRegionOfDefinitionOptionSize,
    eRegionOfDefinitionOptionFormat,
    eRegionOfDefinitionOptionProject,
    eRegionOfDefinitionOptionCustom,
};

#define kParamGeneratorFormat "format"
#define kParamGeneratorFormatLabel "Format"
#define kParamGeneratorFormatHint "The output format"

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output components"
#define kParamOutputComponentsHint "Specify what components to output. In RGB only, the alpha script will not be executed. Similarily, in alpha only, the RGB script " \
"will not be executed."
#define kParamOutputComponentsOptionRGBA "RGBA"
#define kParamOutputComponentsOptionRGB "RGB"
#define kParamOutputComponentsOptionAlpha "Alpha"

#define kParamLayerInput "layerInput"
#define kParamLayerInputChoice kParamLayerInput "Choice"
#define kParamLayerInputLabel "Input Layer "
#define kParamLayerInputHint "Select which layer from the input to use when calling cpixel/apixel on input "

#define kParamDoubleParamNumber "doubleParamsNb"
#define kParamDoubleParamNumberLabel "No. of Scalar Params"
#define kParamDoubleParamNumberHint "Use this to control how many scalar parameters should be exposed to the SeExpr expression."

#define kParamDouble "x"
#define kParamDoubleLabel "x"
#define kParamDoubleHint "A custom 1-dimensional variable that can be referenced in the expression by its script-name, x"

#define kParamDouble2DParamNumber "double2DParamsNb"
#define kParamDouble2DParamNumberLabel "No. of 2D Params"
#define kParamDouble2DParamNumberHint "Use this to control how many 2D (position) parameters should be exposed to the SeExpr expression."

#define kParamDouble2D "pos"
#define kParamDouble2DLabel "pos"
#define kParamDouble2DHint "A custom 2-dimensional variable that can be referenced in the expression by its script-name, pos"

#define kParamColorNumber "colorParamsNb"
#define kParamColorNumberLabel "No. of Color Params"
#define kParamColorNumberHint "Use this to control how many color parameters should be exposed to the SeExpr expression."

#define kParamColor "color"
#define kParamColorLabel "color"
#define kParamColorHint "A custom RGB variable that can be referenced in the expression by its script-name, color"

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Input Frame Range"
#define kParamFrameRangeHint "Default input frame range to fetch images from (may be relative or absolute, depending on the \"frameRangeAbsolute\" parameter). Only used if the frame range cannot be statically computed from the expression. This parameter can be animated."
#define kParamFrameRangeDefault 0,0

#define kParamFrameRangeAbsolute "frameRangeAbsolute"
#define kParamFrameRangeAbsoluteLabel "Absolute Frame Range"
#define kParamFrameRangeAbsoluteHint "If checked, the frame range is relative to the current frame."
#define kParamFrameRangeAbsoluteDefault false

#define kParamScript "script"
#define kParamScriptLabel "RGB Script"
#define kParamScriptHint "Contents of the SeExpr expression. This expression should output the RGB components. See the description of the plug-in and " \
"http://www.disneyanimation.com/technology/seexpr.html for documentation. On Nuke, the characters '$', '[' ']' must be preceded with a backslash (as '\\$', '\\[', '\\]') to avoid TCL variable and expression substitution."

#define kParamShowScript "showScript"
#define kParamShowScriptLabel "Show RGB Script"
#define kParamShowScriptHint "Show the contents of the RGB script as seen by SeExpr in a dialog window. It may be different from the script visible in the GUI, because the host may perform vcariable or expression substitution on the RGB script parameter."

#define kParamAlphaScript "alphaScript"
#define kParamAlphaScriptLabel "Alpha Script"
#define kParamAlphaScriptHint "Contents of the SeExpr expression. This expression should output the alpha component only. See the description of the plug-in and " \
"http://www.disneyanimation.com/technology/seexpr.html for documentation. On Nuke, the characters '$', '[' ']' must be preceded with a backslash (as '\\$', '\\[', '\\]') to avoid TCL variable and expression substitution."

#define kParamShowAlphaScript "showAlphaScript"
#define kParamShowAlphaScriptLabel "Show Alpha Script"
#define kParamShowAlphaScriptHint "Show the contents of the Alpha script as seen by SeExpr in a dialog window. It may be different from the script visible in the GUI, because the host may perform vcariable or expression substitution on the Alpha script parameter."

#define kParamValidate                  "validate"
#define kParamValidateLabel             "Validate"
#define kParamValidateHint              "Validate the script contents and execute it on next render. This locks the script and all its parameters."

#define kSeExprColorPlaneName "Color"
#define kSeExprBackwardMotionPlaneName "Backward"
#define kSeExprForwardMotionPlaneName "Forward"
#define kSeExprDisparityLeftPlaneName "DisparityLeft"
#define kSeExprDisparityRightPlaneName "DisparityRight"

static bool gHostIsMultiPlanar = false;
static bool gHostIsNatron = false;
static bool gHostSupportsRGBA   = false;
static bool gHostSupportsRGB    = false;
static bool gHostSupportsAlpha  = false;
static OFX::PixelComponentEnum gOutputComponentsMap[4];

class SeExprProcessorBase;



////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SeExprPlugin : public OFX::ImageEffect {
public:
    /** @brief ctor */
    SeExprPlugin(OfxImageEffectHandle handle);

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    // override isIdentity
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;
    
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
    virtual void getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents) OVERRIDE FINAL;

    OFX::Clip* getClip(int index) const {
        assert(index >= 0 && index < kSourceClipCount);
        return _srcClip[index];
    }
    
    OFX::DoubleParam**  getDoubleParams()  { return _doubleParams; }
    
    OFX::Double2DParam**  getDouble2DParams()  { return _double2DParams; }
    
    OFX::RGBParam**  getRGBParams()  { return _colorParams; }
    
private:
    
    void buildChannelMenus();
    
    void setupAndProcess(SeExprProcessorBase & processor, const OFX::RenderArguments &args);

    OFX::PixelComponentEnum getOutputComponents() const;

    std::string getOfxComponentsForClip(int inputNumber) const;
    
    std::string getOfxPlaneForClip(int inputNumber) const;
    
    OFX::Clip *_srcClip[kSourceClipCount];
    OFX::Clip* _maskClip;
    OFX::Clip *_dstClip;
    
    std::vector<std::list<std::string> > _clipLayerOptions;
    OFX::ChoiceParam *_clipLayerToFetch[kSourceClipCount];
    OFX::StringParam *_clipLayerToFetchString[kSourceClipCount];
    
    OFX::IntParam *_doubleParamCount;
    OFX::DoubleParam* _doubleParams[kParamsCount];
    
    OFX::IntParam *_double2DParamCount;
    OFX::Double2DParam* _double2DParams[kParamsCount];
    
    OFX::IntParam *_colorParamCount;
    OFX::RGBParam* _colorParams[kParamsCount];

    OFX::Int2DParam *_frameRange;
    OFX::BooleanParam *_frameRangeAbsolute;

    OFX::StringParam *_rgbScript;
    OFX::StringParam *_alphaScript;
    OFX::BooleanParam *_validate;
    
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
    
    OFX::ChoiceParam* _boundingBox;
    
    OFX::ChoiceParam* _format;
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OFX::BooleanParam* _interactive;
    
    OFX::ChoiceParam* _outputComponents;
    

};

OFX::PixelComponentEnum
SeExprPlugin::getOutputComponents() const
{
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    return gOutputComponentsMap[outputComponents_i];
}

class OFXSeExpression;

// Base class for processor, note that we do not use the multi-thread suite.
class SeExprProcessorBase
{
    
protected:
    
    OfxTime _renderTime;
    int _renderView;
    SeExprPlugin* _plugin;
    std::string _layersToFetch[kSourceClipCount];
    OFXSeExpression* _rgbExpr;
    OFXSeExpression* _alphaExpr;
    const OFX::Image* _srcCurTime[kSourceClipCount];
    int _nSrcComponents[kSourceClipCount];
    OFX::Image* _dstImg;
    bool _maskInvert;
    const OFX::Image* _maskImg;
    bool _doMasking;
    double _mix;
    
    // <clipIndex, <time, image> >
    typedef std::map<OfxTime, const OFX::Image*> FetchedImagesForClipMap;
    typedef std::map<int, FetchedImagesForClipMap> FetchedImagesMap;
    FetchedImagesMap _images;
    
public:
    
    SeExprProcessorBase(SeExprPlugin* instance);
    
    virtual ~SeExprProcessorBase();
    
    SeExprPlugin* getPlugin() const {
        return _plugin;
    }
    
    void setDstImg(OFX::Image* dstImg) {
        _dstImg = dstImg;
    }
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}
    

    
    void setValues(OfxTime time, int view, double mix, const std::string& rgbExpr, const std::string& alphaExpr, std::string* layers,
                   const OfxRectI& dstPixelRod, OfxPointI* inputSizes, const OfxPointI& outputSize, const OfxPointD& renderScale, double par);
    
    bool isExprOk(std::string* error);

    void prefetchImage(int inputIndex,
                       OfxTime time)
    {
        // find or create input
        FetchedImagesForClipMap& foundInput = _images[inputIndex];

        FetchedImagesForClipMap::iterator foundImage = foundInput.find(time);
        if (foundImage != foundInput.end()) {
            // image already fetched
            return;
        }

        OFX::Clip* clip = _plugin->getClip(inputIndex);
        assert(clip);

        if (!clip->isConnected()) {
            // clip is not connected, image is NULL
            return;
        }

        OFX::Image *img;
        if (gHostIsMultiPlanar) {
            img = clip->fetchImagePlane(time, _renderView,  _layersToFetch[inputIndex].c_str());
        } else {
            img = clip->fetchImage(time);
        }
        if (!img) {
            return;
        }
        std::pair<FetchedImagesForClipMap::iterator, bool> ret = foundInput.insert(std::make_pair(time, img));
        assert(ret.second);
    }

    const OFX::Image* getImage(int inputIndex,
                               OfxTime time)
    {
        // find or create input
        FetchedImagesForClipMap& foundInput = _images[inputIndex];

        FetchedImagesForClipMap::iterator foundImage = foundInput.find(time);
        if (foundImage != foundInput.end()) {
            return foundImage->second;
        }
        return NULL;
    }

    virtual void process(OfxRectI procWindow) = 0;
};


// implementation of the "apixel" function
template <typename PIX, int nComps, OFX::FilterEnum interp, bool alpha>
static void pixelForDepthCompsFilter(const OFX::Image* img, double x, double y, SeVec3d& result)
{
    result.setValue(0., 0., 0.);
    if ((alpha && nComps != 1 && nComps != 4) ||
        (!alpha && nComps <= 1)) {
        // no value
        return;
    }
    float pix[4];
    // In OFX pixel coordinates, the center of pixel (0,0) has coordinates (0.5,0.5)
    OFX::ofxsFilterInterpolate2D<PIX,nComps,interp,/*clamp=*/true>(x+0.5, y+0.5, img, /*blackOutside=*/false, pix);
    if (alpha) {
        if (nComps == 1) {
            // alpha input
            result.setValue(pix[0], 0., 0.);
        } else if (nComps == 4) {
            // RGBA input
            result.setValue(pix[3], 0., 0.);
        }
    } else {
        if (nComps == 2) {
            // XY input: no B color
            result.setValue(pix[0], pix[1], 0.);
        } else if (nComps >= 3) {
            // alpha input: no color
            result.setValue(pix[0], pix[1], pix[2]);
        }
    }
}

template <typename PIX, int nComps, bool alpha>
static void pixelForDepthComps(const OFX::Image* img, OFX::FilterEnum interp, double x, double y,  SeVec3d& result)
{
    switch (interp) {
        case OFX::eFilterImpulse:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterImpulse,alpha>(img, x, y, result);
        case OFX::eFilterBilinear:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterBilinear,alpha>(img, x, y, result);
        case OFX::eFilterCubic:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterCubic,alpha>(img, x, y, result);
        case OFX::eFilterKeys:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterKeys,alpha>(img, x, y, result);
        case OFX::eFilterSimon:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterSimon,alpha>(img, x, y, result);
        case OFX::eFilterRifman:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterRifman,alpha>(img, x, y, result);
        case OFX::eFilterMitchell:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterMitchell,alpha>(img, x, y, result);
        case OFX::eFilterParzen:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterParzen,alpha>(img, x, y, result);
        case OFX::eFilterNotch:
            return pixelForDepthCompsFilter<PIX,nComps,OFX::eFilterNotch,alpha>(img, x, y, result);
        default:
            result.setValue(0., 0., 0.);
    }
}

template <typename PIX, bool alpha>
static void pixelForDepth(const OFX::Image* img, OFX::FilterEnum interp, double x, double y, SeVec3d& result)
{
    int nComponents = img->getPixelComponentCount();
    switch (nComponents) {
        case 1:
            return pixelForDepthComps<PIX,1,alpha>(img, interp, x, y, result);
        case 2:
            return pixelForDepthComps<PIX,2,alpha>(img, interp, x, y, result);
        case 3:
            return pixelForDepthComps<PIX,3,alpha>(img, interp, x, y, result);
        case 4:
            return pixelForDepthComps<PIX,4,alpha>(img, interp, x, y, result);
        default:
            result.setValue(0., 0., 0.);
    }
}

template<bool alpha>
class PixelFuncX : public SeExprFuncX
{
    SeExprProcessorBase* _processor;

public:


    PixelFuncX(SeExprProcessorBase* processor)
    : SeExprFuncX(true)  // Thread Safe
    , _processor(processor)
    {}

    virtual ~PixelFuncX() {}

private:

    virtual bool prep(SeExprFuncNode* node, bool /*wantVec*/)
    {
        // check number of arguments
        int nargs = node->nargs();
        if (nargs < 4 || 5 < nargs) {
            node->addError("Wrong number of arguments, should be 4 or 5");
            return false;
        }

        for (int i = 0; i < nargs; ++i) {

            if (node->child(i)->isVec()) {
                node->addError("Wrong arguments, should be all scalars");
                return false;
            }
            if (!node->child(i)->prep(false)) {
                return false;
            }
        }

        SeVec3d v;
        node->child(0)->eval(v);
        int inputIndex = (int)SeExpr::round(v[0]) - 1;
        if (inputIndex < 0 || inputIndex >= kSourceClipCount) {
            node->addError("Invalid input index");
            return false;
        }
        return true;
    }



    virtual void eval(const SeExprFuncNode* node, SeVec3d& result) const
    {
        SeVec3d v;
        node->child(0)->eval(v);
        int inputIndex = (int)SeExpr::round(v[0]) - 1;
        if (inputIndex < 0) {
            inputIndex = 0;
        } else if (inputIndex >= kSourceClipCount) {
            inputIndex = kSourceClipCount - 1;
        }
        node->child(1)->eval(v);
        OfxTime frame = SeExpr::round(v[0]);
        node->child(2)->eval(v);
        double x = v[0];
        node->child(3)->eval(v);
        double y = v[0];
        OFX::FilterEnum interp = OFX::eFilterImpulse;
        if (node->nargs() == 5) {
            node->child(4)->eval(v);
            int interp_i = SeExpr::round(v[0]);
            if (interp_i < 0) {
                interp_i = 0;
            } else if (interp_i > (int)OFX::eFilterNotch) {
                interp_i = (int)OFX::eFilterNotch;
            }
            interp = (OFX::FilterEnum)interp_i;
        }
        if (frame != frame || x != x || y != y) {
            // one of the parameters is NaN
            result.setValue(0., 0., 0.);
            return;
        }
        _processor->prefetchImage(inputIndex, frame);
        const OFX::Image* img = _processor->getImage(inputIndex, frame);
        if (!img) {
            // be black and transparent
            result.setValue(0., 0., 0.);
        } else {
            OFX::BitDepthEnum depth = img->getPixelDepth();
            switch (depth) {
                case OFX::eBitDepthFloat:
                    pixelForDepth<float, alpha>(img, interp, x, y, result);
                    break;
                case OFX::eBitDepthUByte:
                    pixelForDepth<unsigned char, 255>(img, interp, x, y, result);
                    break;
                case OFX::eBitDepthUShort:
                    pixelForDepth<unsigned short, 65535>(img, interp, x, y, result);
                    break;
                default:
                    result.setValue(0., 0., 0.);
                    break;
            }
        }
    }
    
};


class DoubleParamVarRef : public SeExprVarRef
{
    //Used to call getValue only once per expression evaluation and not once per pixel
    //Using SeExpr lock is faster than calling the multi-thread suite to get a mutex
    SeExprInternal::Mutex _lock;
    bool _varSet;
    double _value;
    OFX::DoubleParam* _param;
    
public:
    
    DoubleParamVarRef(OFX::DoubleParam* param)
    : SeExprVarRef()
    , _lock()
    , _varSet(false)
    , _value(0)
    , _param(param)
    {
        
    }
    
    virtual ~DoubleParamVarRef() {}
    
    //! returns true for a vector type, false for a scalar type
    virtual bool isVec() { return false;}
    
    //! returns this variable's value by setting result, node refers to
    //! where in the parse tree the evaluation is occurring
    virtual void eval(const SeExprVarNode* /*node*/, SeVec3d& result)
    {
        SeExprInternal::AutoLock<SeExprInternal::Mutex> locker(_lock);
        if (!_varSet) {
            _param->getValue(_value);
            _varSet = true;
        } else {
            result[0] = _value;
        }
    }
};

class Double2DParamVarRef : public SeExprVarRef
{
    //Used to call getValue only once per expression evaluation and not once per pixel
    //Using SeExpr lock is faster than calling the multi-thread suite to get a mutex
    SeExprInternal::Mutex _lock;
    bool _varSet;
    double _value[2];
    OFX::Double2DParam* _param;
    
public:
    
    Double2DParamVarRef(OFX::Double2DParam* param)
    : SeExprVarRef()
    , _lock()
    , _varSet(false)
    , _value()
    , _param(param)
    {
        
    }
    
    virtual ~Double2DParamVarRef() {}
    
    //! returns true for a vector type, false for a scalar type
    virtual bool isVec() { return true;}
    
    //! returns this variable's value by setting result, node refers to
    //! where in the parse tree the evaluation is occurring
    virtual void eval(const SeExprVarNode* /*node*/, SeVec3d& result)
    {
        SeExprInternal::AutoLock<SeExprInternal::Mutex> locker(_lock);
        if (!_varSet) {
            _param->getValue(_value[0],_value[1]);
            _varSet = true;
        } else {
            result[0] = _value[0];
            result[1] = _value[1];
        }
    }
};

class ColorParamVarRef : public SeExprVarRef
{
    //Used to call getValue only once per expression evaluation and not once per pixel
    //Using SeExpr lock is faster than calling the multi-thread suite to get a mutex
    SeExprInternal::Mutex _lock;
    bool _varSet;
    double _value[3];
    OFX::RGBParam* _param;
    
public:
    
    ColorParamVarRef(OFX::RGBParam* param)
    : SeExprVarRef()
    , _lock()
    , _varSet(false)
    , _value()
    , _param(param)
    {
        
    }
    
    virtual ~ColorParamVarRef() {}
    
    //! returns true for a vector type, false for a scalar type
    virtual bool isVec() { return true;}
    
    //! returns this variable's value by setting result, node refers to
    //! where in the parse tree the evaluation is occurring
    virtual void eval(const SeExprVarNode* /*node*/, SeVec3d& result)
    {
        SeExprInternal::AutoLock<SeExprInternal::Mutex> locker(_lock);
        if (!_varSet) {
            _param->getValue(_value[0],_value[1],_value[2]);
            _varSet = true;
        } else {
            result[0] = _value[0];
            result[1] = _value[1];
            result[2] = _value[2];
        }
    }
};

class SimpleScalar : public SeExprVarRef
{
public:
    double _value;
    
    SimpleScalar() : SeExprVarRef(), _value(0) {}
    
    virtual ~SimpleScalar() {}
    
    virtual bool isVec() { return false;}
   
    virtual void eval(const SeExprVarNode* /*node*/, SeVec3d& result)
    {
        result[0] = _value;
    }
};

class SimpleVec : public SeExprVarRef
{
public:
    double _value[3];
    
    SimpleVec() : SeExprVarRef(), _value() { _value[0] = _value[1] = _value[2] = 0.; }
    
    virtual ~SimpleVec() {}
    
    virtual bool isVec() { return true;}
    
    virtual void eval(const SeExprVarNode* /*node*/, SeVec3d& result)
    {
        result[0] = _value[0];
        result[1] = _value[1];
        result[2] = _value[2];
    }
};

class StubSeExpression;

class StubPixelFuncX : public SeExprFuncX
{
    StubSeExpression* _expr;

public:

    StubPixelFuncX(StubSeExpression* expr)
    : SeExprFuncX(true)  // Thread Safe
    , _expr(expr)
    {}

    virtual ~StubPixelFuncX() {}
private:


    virtual bool prep(SeExprFuncNode* node, bool /*wantVec*/);
    virtual void eval(const SeExprFuncNode* node, SeVec3d& result) const;

};

typedef std::map<int,std::vector<OfxTime> > FramesNeeded;

/**
 * @brief Used to determine what are the frames needed and RoIs of the expression
 **/
class StubSeExpression : public SeExpression
{
    
    
    mutable SimpleScalar _nanScalar,_zeroScalar;
    mutable StubPixelFuncX _pixel;
    mutable SeExprFunc _pixelFunction;
    mutable SimpleScalar _currentTime;

    mutable FramesNeeded _images;
    
public:
    
    StubSeExpression(const std::string& expr, OfxTime time);
    
    virtual ~StubSeExpression();
    
    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const std::string& name) const OVERRIDE FINAL;
    
    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const std::string& name) const OVERRIDE FINAL;
    
    void onPixelCalled(int inputIndex, OfxTime time) {
        //Register image needed
        FramesNeeded::iterator foundInput = _images.find(inputIndex);
        if (foundInput == _images.end()) {
            std::vector<OfxTime> times;
            times.push_back(time);
            _images.insert(std::make_pair(inputIndex, times));
        } else {
            if (std::find(foundInput->second.begin(), foundInput->second.end(), time) == foundInput->second.end()) {
                foundInput->second.push_back(time);
            }
        }
    }

    const FramesNeeded& getFramesNeeded() const
    {
        return _images;
    }
    

};

class OFXSeExpression : public SeExpression
{
    mutable PixelFuncX<false> _cpixel;
    mutable SeExprFunc _cpixelFunction;
    mutable PixelFuncX<true> _apixel;
    mutable SeExprFunc _apixelFunction;
    OfxRectI _dstPixelRod;
    typedef std::map<std::string,SeExprVarRef*> VariablesMap;
    VariablesMap _variables;
    
    SimpleScalar _scalex;
    SimpleScalar _scaley;

    SimpleScalar _curTime;
    SimpleScalar _xCoord;
    SimpleScalar _yCoord;
    SimpleScalar _uCoord;
    SimpleScalar _vCoord;
    SimpleScalar _par;
    SimpleScalar _xCanCoord;
    SimpleScalar _yCanCoord;

    SimpleScalar _outputWidth;
    SimpleScalar _outputHeight;
    SimpleScalar _inputWidths[kSourceClipCount];
    SimpleScalar _inputHeights[kSourceClipCount];
    
    SimpleVec _inputColors[kSourceClipCount];
    SimpleScalar _inputAlphas[kSourceClipCount];
    
    DoubleParamVarRef* _doubleRef[kParamsCount];
    Double2DParamVarRef* _double2DRef[kParamsCount];
    ColorParamVarRef* _colorRef[kParamsCount];
public:
    
    
    
    
    OFXSeExpression(SeExprProcessorBase* processor,const std::string& expr, OfxTime time,
                    const OfxPointD& renderScale, double par, const OfxRectI& outputRod);
    
    virtual ~OFXSeExpression();
    
    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const std::string& name) const OVERRIDE FINAL;
    
    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const std::string& name) const OVERRIDE FINAL;
    
    /** NOT MT-SAFE, this object is to be used PER-THREAD*/
    void setXY(int x, int y) {
        _xCoord._value = x;
        _yCoord._value = y;
        _uCoord._value = (x + 0.5 - _dstPixelRod.x1) / (_dstPixelRod.x2 - _dstPixelRod.x1);
        _vCoord._value = (y + 0.5 - _dstPixelRod.y1) / (_dstPixelRod.y2 - _dstPixelRod.y1);
        _xCanCoord._value = (x + 0.5) * _par._value /_scalex._value;
        _yCanCoord._value = (y + 0.5) / _scaley._value;
    }
    
    void setRGBA(int inputIndex, float r, float g, float b, float a) {
        _inputColors[inputIndex]._value[0] = r;
        _inputColors[inputIndex]._value[1] = g;
        _inputColors[inputIndex]._value[2] = b;
        _inputAlphas[inputIndex]._value = a;
    }
    
    void setSize(int inputNumber, int w, int h) {
        if (inputNumber == -1) {
            _outputWidth._value = w;
            _outputHeight._value = h;
        } else {
            _inputWidths[inputNumber]._value = w;
            _inputHeights[inputNumber]._value = h;
        }
    }

    
};

OFXSeExpression::OFXSeExpression( SeExprProcessorBase* processor, const std::string& expr, OfxTime time,
                                 const OfxPointD& renderScale, double par, const OfxRectI& outputRod)
: SeExpression(expr)
, _cpixel(processor)
, _cpixelFunction(_cpixel, 4, 5)
, _apixel(processor)
, _apixelFunction(_apixel, 4, 5)
, _dstPixelRod(outputRod)
, _variables()
, _scalex()
, _scaley()
, _curTime()
, _xCoord()
, _yCoord()
, _uCoord()
, _vCoord()
, _par()
, _xCanCoord()
, _yCanCoord()
, _outputWidth()
, _outputHeight()
, _inputWidths()
, _inputHeights()
, _inputColors()
, _inputAlphas()
, _doubleRef()
, _double2DRef()
, _colorRef()
{
    _dstPixelRod = outputRod;
    
    _scalex._value = renderScale.x;
    _variables[kSeExprRenderScaleXVarName] = &_scalex;

    _scaley._value = renderScale.y;
    _variables[kSeExprRenderScaleYVarName] = &_scaley;

    _curTime._value = time;
    _variables[kSeExprCurrentTimeVarName] = &_curTime;
    
    _variables[kSeExprXCoordVarName] = &_xCoord;
    
    _variables[kSeExprYCoordVarName] = &_yCoord;
    
    _variables[kSeExprUCoordVarName] = &_uCoord;
    
    _variables[kSeExprVCoordVarName] = &_vCoord;

    _par._value = par;
    _variables[kSeExprPARVarName] = &_par;

    _variables[kSeExprXCanCoordVarName] = &_xCanCoord;

    _variables[kSeExprYCanCoordVarName] = &_yCanCoord;

    _variables[kSeExprOutputWidthVarName] = &_outputWidth;

    _variables[kSeExprOutputHeightVarName] = &_outputHeight;

    char name[256];

    for (int i = 0; i < kSourceClipCount; ++i) {
        snprintf(name, sizeof(name), kSeExprInputWidthVarName "%d", i+1);
        _variables[name] = &_inputWidths[i];
        snprintf(name, sizeof(name), kSeExprInputHeightVarName "%d", i+1);
        _variables[name] = &_inputHeights[i];
        snprintf(name, sizeof(name), kSeExprColorVarName "%d", i+1);
        _variables[name] = &_inputColors[i];
        snprintf(name, sizeof(name), kSeExprAlphaVarName "%d", i+1);
        _variables[name] = &_inputAlphas[i];
        if (i == 0) {
            // default names for the first input
            _variables[kSeExprInputWidthVarName] = &_inputWidths[i];
            _variables[kSeExprInputHeightVarName] = &_inputHeights[i];
            _variables[kSeExprColorVarName] = &_inputColors[i];
            _variables[kSeExprAlphaVarName] = &_inputAlphas[i];
        }
    }

    assert(processor);
    SeExprPlugin* plugin = processor->getPlugin();

    OFX::DoubleParam** doubleParams = plugin->getDoubleParams();
    OFX::Double2DParam** double2DParams = plugin->getDouble2DParams();
    OFX::RGBParam** colorParams = plugin->getRGBParams();

    for (int i = 0; i < kParamsCount; ++i) {
        _doubleRef[i] = new DoubleParamVarRef(doubleParams[i]);
        _double2DRef[i]  = new Double2DParamVarRef(double2DParams[i]);
        _colorRef[i]  = new ColorParamVarRef(colorParams[i]);
        snprintf(name, sizeof(name), kParamDouble "%d", i+1);
        _variables[name] = _doubleRef[i];
        snprintf(name, sizeof(name), kParamDouble2D "%d", i+1);
        _variables[name] = _double2DRef[i];
        snprintf(name, sizeof(name), kParamColor "%d", i+1);
        _variables[name] = _colorRef[i];
    }
}

OFXSeExpression::~OFXSeExpression()
{
    for (int i = 0; i < kParamsCount; ++i) {
        delete _doubleRef[i];
        delete _double2DRef[i];
        delete _colorRef[i];
    }
}

SeExprVarRef*
OFXSeExpression::resolveVar(const std::string& varName) const
{
    VariablesMap::const_iterator found = _variables.find(varName);
    if (found == _variables.end()) {
        return 0;
    }
    return found->second;
}


SeExprFunc* OFXSeExpression::resolveFunc(const std::string& funcName) const
{
    // check if it is builtin so we get proper behavior
    if (SeExprFunc::lookup(funcName)) {
        return 0;
    }
    if (funcName == kSeExprCPixelFuncName) {
        return &_cpixelFunction;
    }
    if (funcName == kSeExprAPixelFuncName) {
        return &_apixelFunction;
    }
    return 0;
}


bool
StubPixelFuncX::prep(SeExprFuncNode* node, bool /*wantVec*/)
{
    // check number of arguments
    int nargs = node->nargs();
    if (nargs < 4 || 5 < nargs) {
        node->addError("Wrong number of arguments, should be 4 or 5");
        return false;
    }

    for (int i = 0; i < nargs; ++i) {

        if (node->child(i)->isVec()) {
            node->addError("Wrong arguments, should be all scalars");
            return false;
        }
        if (!node->child(i)->prep(false)) {
            return false;
        }
    }

    SeVec3d v;
    node->child(0)->eval(v);
    int inputIndex = (int)SeExpr::round(v[0]) - 1;
    if (inputIndex < 0 || inputIndex >= kSourceClipCount) {
        node->addError("Invalid input index");
        return false;
    }
    return true;
}

void
StubPixelFuncX::eval(const SeExprFuncNode* node, SeVec3d& result) const
{
    SeVec3d v;
    node->child(0)->eval(v);
    int inputIndex = (int)SeExpr::round(v[0]) - 1;
    node->child(1)->eval(v);
    OfxTime frame = SeExpr::round(v[0]);


    _expr->onPixelCalled(inputIndex, frame);
    result[0] = result[1] = result[2] = std::numeric_limits<double>::quiet_NaN();
}

StubSeExpression::StubSeExpression(const std::string& expr, OfxTime time)
: SeExpression(expr)
, _nanScalar()
, _zeroScalar()
, _pixel(this)
, _pixelFunction(_pixel, 4, 5)
, _currentTime()
{
    _nanScalar._value = std::numeric_limits<double>::quiet_NaN();
    _currentTime._value = time;
}

StubSeExpression::~StubSeExpression()
{
    
}

/** override resolveVar to add external variables */
SeExprVarRef*
StubSeExpression::resolveVar(const std::string& varName) const
{
    if (varName == kSeExprCurrentTimeVarName) {
        return &_currentTime;
    } else if (varName == kSeExprXCoordVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprYCoordVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprUCoordVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprUCoordVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprXCanCoordVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprYCanCoordVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprOutputWidthVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprOutputHeightVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprColorVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprAlphaVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprInputWidthVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprInputHeightVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprRenderScaleXVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprRenderScaleYVarName) {
        return &_nanScalar;
    }

    return &_nanScalar;
}

/** override resolveFunc to add external functions */
SeExprFunc*
StubSeExpression::resolveFunc(const std::string& funcName) const
{
    // check if it is builtin so we get proper behavior
    if (SeExprFunc::lookup(funcName)) {
        return 0;
    }
    if (funcName == kSeExprCPixelFuncName || funcName == kSeExprAPixelFuncName) {
        return &_pixelFunction;
    }

    return 0;
}

SeExprProcessorBase::SeExprProcessorBase(SeExprPlugin* instance)
: _renderTime(0)
, _renderView(0)
, _plugin(instance)
, _rgbExpr(0)
, _alphaExpr(0)
, _srcCurTime()
, _dstImg(0)
, _maskInvert(false)
, _maskImg(0)
, _doMasking(false)
, _mix(0.)
, _images()
{
    for (int i = 0; i < kSourceClipCount; ++i) {
        _srcCurTime[i] = 0;
        _nSrcComponents[i] = 0;
    }
}

SeExprProcessorBase::~SeExprProcessorBase()
{
    delete _rgbExpr;
    delete _alphaExpr;
    for (FetchedImagesMap::iterator it = _images.begin(); it!=_images.end(); ++it) {
        for (FetchedImagesForClipMap::iterator it2 = it->second.begin(); it2!= it->second.end(); ++it2) {
            delete it2->second;
        }
    }
}

void
SeExprProcessorBase::setValues(OfxTime time, int view, double mix, const std::string& rgbExpr, const std::string& alphaExpr, std::string* layers, const OfxRectI& dstPixelRod, OfxPointI* inputSizes, const OfxPointI& outputSize, const OfxPointD& renderScale, double par)
{
    _renderTime = time;
    _renderView = view;
    if (!rgbExpr.empty()) {
        _rgbExpr = new OFXSeExpression(this, rgbExpr, time, renderScale, par, dstPixelRod);
    }
    if (!alphaExpr.empty()) {
        _alphaExpr = new OFXSeExpression(this, alphaExpr, time, renderScale, par, dstPixelRod);
    }
    if (gHostIsMultiPlanar) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            _layersToFetch[i] = layers[i];
        }
    }
    for (int i = 0; i < kSourceClipCount; ++i) {
        if (_rgbExpr) {
            _rgbExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
        if (_alphaExpr) {
            _alphaExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
    }
    if (_rgbExpr) {
        _rgbExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    if (_alphaExpr) {
        _alphaExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    assert(_alphaExpr || _rgbExpr);
    _mix = mix;
}

bool
SeExprProcessorBase::isExprOk(std::string* error)
{
    if (_rgbExpr && !_rgbExpr->isValid()) {
        *error = _rgbExpr->parseError();
        return false;
    }
    if (_alphaExpr && !_alphaExpr->isValid()) {
        *error = _alphaExpr->parseError();
        return false;
    }
    
    //Run the expression once to initialize all the images fields before multi-threading
    if (_rgbExpr) {
        (void)_rgbExpr->evaluate();
    }
    if (_alphaExpr) {
        (void)_alphaExpr->evaluate();
    }
    
    //Ensure the image of the input 0 at the current time exists for the mix

    for (int i = 0; i < kSourceClipCount; ++i) {
        prefetchImage(i, _renderTime);
        _srcCurTime[i] = getImage(i, _renderTime);
        _nSrcComponents[i] = _srcCurTime[i] ? _srcCurTime[i]->getPixelComponentCount() : 0;
    }

    return true;
}


// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue>
class SeExprProcessor : public SeExprProcessorBase
{
    
public:
    // ctor
    SeExprProcessor(SeExprPlugin* instance)
    : SeExprProcessorBase(instance)
    {}
    
    // and do some processing
    virtual void process(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert((nComponents == 4 && _rgbExpr && _alphaExpr) ||
               (nComponents == 3 && _rgbExpr && !_alphaExpr) ||
               (nComponents == 1 && !_rgbExpr && _alphaExpr));

        
        float tmpPix[4];
        PIX srcPixels[kSourceClipCount][4];

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_plugin->abort()) {
                break;
            }
        
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                for (int i = kSourceClipCount - 1; i  >= 0; --i) {
                    const PIX* src_pixels  = _srcCurTime[i] ? (const PIX*) _srcCurTime[i]->getPixelAddress(x,y) : 0;
                    for (int k = 0; k < 4; ++k) {
                        if (k < _nSrcComponents[i]) {
                            srcPixels[i][k] = src_pixels ? src_pixels[k] : 0;
                        } else {
                            srcPixels[i][k] = 0;
                        }
                    }
                    float r = srcPixels[i][0] / (float)maxValue;
                    float g = srcPixels[i][1] / (float)maxValue;
                    float b = srcPixels[i][2] / (float)maxValue;
                    float a = srcPixels[i][nComponents == 4 ? 3 : 0] / (float)maxValue;
                    if (_rgbExpr) {
                        _rgbExpr->setRGBA(i, r, g, b, a);
                    }
                    if (_alphaExpr) {
                        _alphaExpr->setRGBA(i, r, g, b, a);
                    }
                }
                
                if (_rgbExpr) {
                    _rgbExpr->setXY(x, y);
                    SeVec3d result = _rgbExpr->evaluate();
                    tmpPix[0] = result[0];
                    tmpPix[1] = result[1];
                    tmpPix[2] = result[2];
                } else {
                    tmpPix[0] = 0.;
                    tmpPix[1] = 0.;
                    tmpPix[2] = 0.;
                }
                if (_alphaExpr) {
                    _alphaExpr->setXY(x, y);
                    SeVec3d result = _alphaExpr->evaluate();
                    if (nComponents == 4) {
                        tmpPix[3] = result[0];
                    } else {
                        tmpPix[0] = result[0];
                        tmpPix[3] = 0.;
                    }
                    
                } else {
                    tmpPix[3] = 0.;
                }
                
                
                OFX::ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixels[0], _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

SeExprPlugin::SeExprPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
{
    char name[256];
    if (getContext() != OFX::eContextGenerator) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            if (i == 0 && getContext() == OFX::eContextFilter) {
                _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
            } else {
			  snprintf(name, sizeof(name), "%d", i+1);
			  _srcClip[i] = fetchClip(name);
            }
        }
    }
    _clipLayerOptions.resize(kSourceClipCount);
    
    _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!_maskClip || _maskClip->getPixelComponents() == OFX::ePixelComponentAlpha);
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);

    _doubleParamCount = fetchIntParam(kParamDoubleParamNumber);
    assert(_doubleParamCount);
    _double2DParamCount = fetchIntParam(kParamDouble2DParamNumber);
    assert(_double2DParamCount);
    _colorParamCount = fetchIntParam(kParamColorNumber);
    assert(_colorParamCount);

    for (int i = 0; i < kParamsCount; ++i) {
        if (gHostIsMultiPlanar) {
            snprintf(name, sizeof(name), kParamLayerInput "%d", i+1 );
            _clipLayerToFetch[i] = fetchChoiceParam(name);
            snprintf(name, sizeof(name), kParamLayerInputChoice "%d", i+1 );
            _clipLayerToFetchString[i] = fetchStringParam(name);
        } else {
            _clipLayerToFetch[i] = 0;
            _clipLayerToFetchString[i] = 0;
        }
        
        snprintf(name, sizeof(name), kParamDouble "%d", i+1 );
        _doubleParams[i] = fetchDoubleParam(name);
        snprintf(name, sizeof(name), kParamDouble2D "%d", i+1 );
        _double2DParams[i] = fetchDouble2DParam(name);
        snprintf(name, sizeof(name), kParamColor "%d", i+1 );
        _colorParams[i] = fetchRGBParam(name);
    }

    _frameRange = fetchInt2DParam(kParamFrameRange);
    _frameRangeAbsolute = fetchBooleanParam(kParamFrameRangeAbsolute);
    assert(_frameRange && _frameRangeAbsolute);

    _rgbScript = fetchStringParam(kParamScript);
    assert(_rgbScript);
    _alphaScript = fetchStringParam(kParamAlphaScript);
    assert(_alphaScript);
    _validate = fetchBooleanParam(kParamValidate);
    assert(_validate);

    _mix = fetchDoubleParam(kParamMix);
    _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);

    _boundingBox = fetchChoiceParam(kParamRegionOfDefinition);
    assert(_boundingBox);
    
    _outputComponents = fetchChoiceParam(kParamOutputComponents);
    assert(_outputComponents);
    
    _format = fetchChoiceParam(kParamGeneratorFormat);
    _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
    _size = fetchDouble2DParam(kParamRectangleInteractSize);
    _interactive = fetchBooleanParam(kParamRectangleInteractInteractive);
    assert(_format && _btmLeft && _size && _interactive);
    
    // update visibility
    OFX::InstanceChangedArgs args = { OFX::eChangeUserEdit, 0, {1,1} };
    changedParam(args, kParamDoubleParamNumber);
    changedParam(args, kParamDouble2DParamNumber);
    changedParam(args, kParamColorNumber);
    changedParam(args, kParamValidate);
    changedParam(args, kParamRegionOfDefinition);
    changedParam(args, kParamOutputComponents);
}

std::string
SeExprPlugin::getOfxComponentsForClip(int inputNumber) const
{
    assert(inputNumber >= 0 && inputNumber < kSourceClipCount);
    int opt_i;
    _clipLayerToFetch[inputNumber]->getValue(opt_i);
    std::string opt;
    _clipLayerToFetch[inputNumber]->getOption(opt_i, opt);
    
    if (opt == kSeExprColorPlaneName) {

        return _srcClip[inputNumber]->getPixelComponentsProperty();
    } else if (opt == kSeExprForwardMotionPlaneName || opt == kSeExprBackwardMotionPlaneName) {

        return kFnOfxImageComponentMotionVectors;
    } else if (opt == kSeExprDisparityLeftPlaneName || opt == kSeExprDisparityRightPlaneName) {

        return kFnOfxImageComponentStereoDisparity;
    } else {
        std::list<std::string> components = _srcClip[inputNumber]->getComponentsPresent();
        for (std::list<std::string>::iterator it = components.begin(); it != components.end(); ++it) {
            std::vector<std::string> layerChannels = OFX::mapPixelComponentCustomToLayerChannels(*it);
            if (layerChannels.empty()) {
                continue;
            }
            // first element is layer name
            if (layerChannels[0] == opt) {
                return *it;
            }
        }
    }
    return std::string();
}

std::string
SeExprPlugin::getOfxPlaneForClip(int inputNumber) const
{
    assert(inputNumber >= 0 && inputNumber < kSourceClipCount);
    int opt_i;
    _clipLayerToFetch[inputNumber]->getValue(opt_i);
    std::string opt;
    _clipLayerToFetch[inputNumber]->getOption(opt_i, opt);
    
    if (opt == kSeExprColorPlaneName) {
        return kFnOfxImagePlaneColour;
    } else if (opt == kSeExprForwardMotionPlaneName) {
        return kFnOfxImagePlaneForwardMotionVector;
    } else if (opt == kSeExprBackwardMotionPlaneName) {
        return kFnOfxImagePlaneBackwardMotionVector;
    } else if (opt == kSeExprDisparityLeftPlaneName) {
        return kFnOfxImagePlaneStereoDisparityLeft;
    } else if (opt == kSeExprDisparityRightPlaneName) {
        return kFnOfxImagePlaneStereoDisparityRight;
    } else {
        std::list<std::string> components = _srcClip[inputNumber]->getComponentsPresent();
        for (std::list<std::string>::iterator it = components.begin(); it!=components.end(); ++it) {
            std::vector<std::string> layerChannels = OFX::mapPixelComponentCustomToLayerChannels(*it);
            if (layerChannels.empty()) {
                continue;
            }
            // first element is layer name
            if (layerChannels[0] == opt) {
                return *it;
            }
        }
    }
    return std::string();
}

void
SeExprPlugin::setupAndProcess(SeExprProcessorBase & processor, const OFX::RenderArguments &args)
{
    
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    std::string rgbScript,alphaScript;
    if (dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA) {
        _rgbScript->getValue(rgbScript);
    }
    if (dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha) {
        _alphaScript->getValue(alphaScript);
    }
    
    
    std::string inputLayers[kSourceClipCount];
    if (gHostIsMultiPlanar) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            inputLayers[i] = getOfxPlaneForClip(i);
        }
    }
    
    double mix;
    _mix->getValue(mix);
    
    processor.setDstImg(dst.get());
    
    // auto ptr for the mask.
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        
        // say we are masking
        processor.doMasking(true);
        
        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    OfxPointI inputSizes[kSourceClipCount];
    for (int i = 0; i < kSourceClipCount; ++i) {
        if (_srcClip[i]->isConnected()) {
            OfxRectD rod = _srcClip[i]->getRegionOfDefinition(args.time);
            double par = _srcClip[i]->getPixelAspectRatio();
            OfxRectI pixelRod;
            OFX::Coords::toPixelEnclosing(rod, args.renderScale, par, &pixelRod);
            inputSizes[i].x = pixelRod.x2 - pixelRod.x1;
            inputSizes[i].y = pixelRod.y2 - pixelRod.y1;
        } else {
            inputSizes[i].x = inputSizes[i].y = 0.;
        }
    }
    
    OFX::RegionOfDefinitionArguments rodArgs;
    rodArgs.time = args.time;
    rodArgs.view = args.viewsToRender;
    rodArgs.renderScale = args.renderScale;
    OfxRectD outputRod;
    getRegionOfDefinition(rodArgs, outputRod);
    OfxRectI outputPixelRod;
    
    double par = dst->getPixelAspectRatio();
    
    OFX::Coords::toPixelEnclosing(outputRod, args.renderScale, par, &outputPixelRod);
    OfxPointI outputSize;
    outputSize.x = outputPixelRod.x2 - outputPixelRod.x1;
    outputSize.y = outputPixelRod.y2 - outputPixelRod.y1;
    
    processor.setValues(args.time, args.renderView, mix, rgbScript, alphaScript, inputLayers, outputPixelRod, inputSizes, outputSize, args.renderScale, par);
    
    std::string error;
    if (!processor.isExprOk(&error)) {
        setPersistentMessage(OFX::Message::eMessageError, "", error);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    
    
    processor.process(args.renderWindow);
}

void
SeExprPlugin::render(const OFX::RenderArguments &args)
{
    clearPersistentMessage();
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }

    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);

    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    OFX::PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    if (dstComponents != outputComponents) {
        setPersistentMessage(OFX::Message::eMessageError, "", "SeExpr: OFX Host did not take into account output components");
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                SeExprProcessor<unsigned char, 4, 255> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                SeExprProcessor<unsigned short, 4, 65535> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                SeExprProcessor<float, 4, 1> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
                return;
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                SeExprProcessor<unsigned char, 3, 255> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                SeExprProcessor<unsigned short, 3, 65535> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                SeExprProcessor<float, 3, 1> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
                return;
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                SeExprProcessor<unsigned char, 1, 255> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                SeExprProcessor<unsigned short, 1, 65535> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                SeExprProcessor<float, 1, 1> fred(this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
                return;
        }
    }

}


void
SeExprPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (paramName == kParamDoubleParamNumber && args.reason == OFX::eChangeUserEdit) {
        int numVisible;
        _doubleParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _doubleParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamDouble2DParamNumber && args.reason == OFX::eChangeUserEdit) {
        int numVisible;
        _double2DParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _double2DParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamColorNumber && args.reason == OFX::eChangeUserEdit) {
        int numVisible;
        _colorParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _colorParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamValidate && args.reason == OFX::eChangeUserEdit) {
        if (!gHostIsNatron) {
            bool validated;
            _validate->getValue(validated);
            
            _doubleParamCount->setEnabled(!validated);
            _double2DParamCount->setEnabled(!validated);
            _colorParamCount->setEnabled(!validated);
            _doubleParamCount->setEvaluateOnChange(validated);
            _double2DParamCount->setEvaluateOnChange(validated);
            _colorParamCount->setEvaluateOnChange(validated);
            _rgbScript->setEnabled(!validated);
            _rgbScript->setEvaluateOnChange(validated);
            _alphaScript->setEnabled(!validated);
            _alphaScript->setEvaluateOnChange(validated);
            clearPersistentMessage();
        }
    } else if (paramName == kParamRegionOfDefinition && args.reason == OFX::eChangeUserEdit) {
        int boundingBox_i;
        _boundingBox->getValue(boundingBox_i);
        RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
        bool hasFormat = (boundingBox == eRegionOfDefinitionOptionFormat);
        bool hasSize = (boundingBox == eRegionOfDefinitionOptionSize);

        _format->setEnabled(hasFormat);
        _format->setIsSecret(!hasFormat);
        _size->setEnabled(hasSize);
        _size->setIsSecret(!hasSize);
        _btmLeft->setEnabled(hasSize);
        _btmLeft->setIsSecret(!hasSize);
        _interactive->setEnabled(hasSize);
        _interactive->setIsSecret(!hasSize);
    } else if (paramName == kParamOutputComponents && args.reason == OFX::eChangeUserEdit) {
        OFX::PixelComponentEnum outputComponents = getOutputComponents();
        if (outputComponents == OFX::ePixelComponentRGB || outputComponents == OFX::ePixelComponentRGBA) { // RGB || RGBA
            _rgbScript->setIsSecret(false);
        } else {
            _rgbScript->setIsSecret(true);
        }
        if (outputComponents == OFX::ePixelComponentRGBA || outputComponents == OFX::ePixelComponentAlpha) { // RGBA || alpha
            _alphaScript->setIsSecret(false);
        } else {
            _alphaScript->setIsSecret(true);
        }
    } else if (paramName == kParamShowScript && args.reason == OFX::eChangeUserEdit) {
        std::string script;
        _rgbScript->getValueAtTime(args.time, script);
        sendMessage(OFX::Message::eMessageMessage, "", "RGB Script:\n" + script);
    } else if (paramName == kParamShowAlphaScript && args.reason == OFX::eChangeUserEdit) {
        std::string script;
        _alphaScript->getValueAtTime(args.time, script);
        sendMessage(OFX::Message::eMessageMessage, "", "Alpha Script:\n" + script);
    } else {
        
        char name[256];
        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), kParamLayerInput "%d", i+1 );
            if (paramName == std::string(name) && args.reason == OFX::eChangeUserEdit) {
                int cur_i;
                _clipLayerToFetch[i]->getValue(cur_i);
                std::string opt;
                _clipLayerToFetch[i]->getOption(cur_i, opt);
                _clipLayerToFetchString[i]->setValue(opt);
                break;
            }
        }
    }
}


bool
SeExprPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                         OFX::Clip * &identityClip,
                         double &/*identityTime*/)
{
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();


    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if (!OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0)) {
                identityClip = _srcClip[0];
                return true;
            }
        }
    }

    return false;
}

void
SeExprPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (!gHostIsMultiPlanar) {
        return;
    }
    if (args.reason == OFX::eChangeUserEdit) {
        std::string strName;
        char name[256];
        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), "%d", i+1);
            if (name == clipName) {
                assert(_srcClip[i]);
                _clipLayerToFetch[i]->setIsSecret(!_srcClip[i]->isConnected());
            }
        }
    }
}

namespace {
    static bool hasListChanged(const std::list<std::string>& oldList, const std::list<std::string>& newList)
    {
        if (oldList.size() != newList.size()) {
            return true;
        }
        
        std::list<std::string>::const_iterator itNew = newList.begin();
        for (std::list<std::string>::const_iterator it = oldList.begin(); it!=oldList.end(); ++it,++itNew) {
            if (*it != *itNew) {
                return true;
            }
        }
        return false;
    }
}

void
SeExprPlugin::buildChannelMenus()
{
    for (int i = 0; i < kSourceClipCount; ++i) {
        std::list<std::string> components = _srcClip[i]->getComponentsPresent();
        if (!hasListChanged(_clipLayerOptions[i], components)) {
            continue;
        }
        _clipLayerToFetch[i]->resetOptions();

        _clipLayerOptions[i] = components;
        
        std::vector<std::string> options;
        options.push_back(kSeExprColorPlaneName);
        
        for (std::list<std::string> ::iterator it = components.begin(); it!=components.end(); ++it) {
            const std::string& comp = *it;
            if (comp == kOfxImageComponentAlpha) {
                continue;
            } else if (comp == kOfxImageComponentRGB) {
                continue;
            } else if (comp == kOfxImageComponentRGBA) {
                continue;
            } else if (comp == kFnOfxImageComponentMotionVectors) {
                options.push_back(kSeExprBackwardMotionPlaneName);
                options.push_back(kSeExprForwardMotionPlaneName);
            } else if (comp == kFnOfxImageComponentStereoDisparity) {
                options.push_back(kSeExprDisparityLeftPlaneName);
                options.push_back(kSeExprDisparityRightPlaneName);
#ifdef OFX_EXTENSIONS_NATRON
            } else {
                std::vector<std::string> layerChannels = OFX::mapPixelComponentCustomToLayerChannels(*it);
                if (layerChannels.empty()) {
                    continue;
                }
                // first element is layer name
                options.push_back(layerChannels[0]);
#endif
            }
        }
        for (std::size_t j = 0; j < options.size(); ++j) {
            _clipLayerToFetch[i]->appendOption(options[j]);
        }
        std::string valueStr;
        _clipLayerToFetchString[i]->getValue(valueStr);
        if (valueStr.empty()) {
            int cur_i;
            _clipLayerToFetch[i]->getValue(cur_i);
            _clipLayerToFetch[i]->getOption(cur_i, valueStr);
            _clipLayerToFetchString[i]->setValue(valueStr);
        } else {
            int foundOption = -1;
            for (std::size_t j = 0; j < options.size(); ++j) {
                if (options[j] == valueStr) {
                    foundOption = j;
                    break;
                }
            }
            if (foundOption != -1) {
                _clipLayerToFetch[i]->setValue(foundOption);
            } else {
                _clipLayerToFetch[i]->setValue(0);
                _clipLayerToFetchString[i]->setValue(options[0]);
            }
        }
    }
}

void
SeExprPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    if (gHostIsMultiPlanar) {
        buildChannelMenus();
    }

    double par = 0.;
    
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    switch (boundingBox) {
        case eRegionOfDefinitionOptionSize: {
            //size
            break;
        }
        case eRegionOfDefinitionOptionFormat: {
            //format
            int index;
            _format->getValue(index);
            int w, h;
            getFormatResolution( (OFX::EParamFormat)index, &w, &h, &par );
            break;
        }
        case eRegionOfDefinitionOptionProject: {
            //project format

            /// this should be the defalut value given by the host, no need to set it.
            /// @see Instance::setupClipPreferencesArgs() in HostSupport, it should have
            /// the line:
            /// double inputPar = getProjectPixelAspectRatio();

            //par = getProjectPixelAspectRatio();
            break;
        }
        default:
            break;
    }
    
    if (par != 0.) {
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
    }

    //We're frame varying since we don't know what the user may output at any frame
    clipPreferences.setOutputFrameVarying(true);
    clipPreferences.setOutputHasContinousSamples(true);

    OFX::PixelComponentEnum outputComponents = getOutputComponents();
    if (outputComponents == OFX::ePixelComponentRGB) {
        clipPreferences.setOutputPremultiplication(OFX::eImageOpaque);
    }
    clipPreferences.setClipComponents(*_dstClip, outputComponents);
}

void
SeExprPlugin::getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents)
{
    for (int i = 0; i < kSourceClipCount; ++i) {
        
        if (!_srcClip[i]->isConnected()) {
            continue;
        }
        
        std::string ofxComp = getOfxComponentsForClip(i);
        if (!ofxComp.empty()) {
            clipComponents.addClipComponents(*_srcClip[i], ofxComp);
        }
    }
    
    OFX::PixelComponentEnum outputComps = _dstClip->getPixelComponents();
    clipComponents.addClipComponents(*_dstClip, outputComps);
    clipComponents.setPassThroughClip(_srcClip[0], args.time, args.view);
}


void
SeExprPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames)
{
    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }

    //To determine the frames needed of the expression, we just execute the expression for 1 pixel
    //and record what are the calls made to getPixel in order to figure out the Roi.
    //We trust that only evaluating the expression for 1 pixel will make all the calls to getPixel
    //In other words, we do not support scripts that do not fetch all images needed for all pixels, e.g:
    /*
     if(x > 0) {
     srcCol = getPixel(0,frame,5,5)
     } else {
     srcCol = [0,0,0]
     }
     */
    const double time = args.time;
    FramesNeeded framesNeeded;
    
    OFX::PixelComponentEnum outputComponents = getOutputComponents();
    if (outputComponents == OFX::ePixelComponentRGB || outputComponents == OFX::ePixelComponentRGBA) {// RGB || RGBA
        std::string rgbScript;
        _rgbScript->getValue(rgbScript);
        
        StubSeExpression expr(rgbScript, time);
        if (!expr.isValid()) {
            setPersistentMessage(OFX::Message::eMessageError, "", expr.parseError());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }

        (void)expr.evaluate();
        const FramesNeeded& rgbNeeded = expr.getFramesNeeded();
        for (FramesNeeded::const_iterator it = rgbNeeded.begin(); it != rgbNeeded.end() ;++it) {
            std::vector<OfxTime>& frames = framesNeeded[it->first];
            for (std::size_t j = 0; j < it->second.size(); ++j) {
                
                bool found = false;
                for (std::size_t i = 0;  i < frames.size() ; ++i) {
                    if (frames[i] == it->second[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    frames.push_back(it->second[j]);
                }
            }
            
        }
    }
    if (outputComponents == OFX::ePixelComponentRGBA || outputComponents == OFX::ePixelComponentAlpha) { // RGBA || alpha
        std::string alphaScript;
        _alphaScript->getValue(alphaScript);
        
        StubSeExpression expr(alphaScript, time);
        if (!expr.isValid()) {
            setPersistentMessage(OFX::Message::eMessageError, "", expr.parseError());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }

        (void)expr.evaluate();
        const FramesNeeded& alphaNeeded = expr.getFramesNeeded();
        for (FramesNeeded::const_iterator it = alphaNeeded.begin(); it != alphaNeeded.end() ;++it) {
            std::vector<OfxTime>& frames = framesNeeded[it->first];
            for (std::size_t j = 0; j < it->second.size(); ++j) {
                
                bool found = false;
                for (std::size_t i = 0;  i < frames.size() ; ++i) {
                    if (frames[i] == it->second[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    frames.push_back(it->second[j]);
                }
            }
            
        }
    }

    bool useDefaultRange[kSourceClipCount];
    std::fill(useDefaultRange, useDefaultRange + kSourceClipCount, false);
    for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
        assert(it->first >= 0  && it->first < kSourceClipCount);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            if (it->second[i] != it->second[i]) {
                //This number is NaN! The user probably used something dependant on a pixel value as a time for the getPixel function
                // we fall back on using the default frame range
                useDefaultRange[it->first] = true;
            }
        }
    }

    for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
        if (useDefaultRange[it->first]) {
            continue;
        }
        OFX::Clip* clip = getClip(it->first);
        assert(clip);

        bool hasFetchedCurrentTime = false;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            assert (it->second[i] == it->second[i]);
            OfxRangeD range;
            if (it->second[i] == time) {
                hasFetchedCurrentTime = true;
            }
            range.min = range.max = it->second[i];
            frames.setFramesNeeded(*clip, range);
        }
        if (!hasFetchedCurrentTime) {
            OfxRangeD range;
            range.min = range.max = time;
            frames.setFramesNeeded(*clip, range);
        }
    }

    // for clips that could not have their range computed from the expression,
    // use the default range
    OfxRangeD range;
    int t1, t2;
    _frameRange->getValueAtTime(time, t1, t2);
    bool absolute;
    _frameRangeAbsolute->getValueAtTime(time, absolute);
    if (absolute) {
        range.min = std::min(t1, t2);
        range.max = std::max(t1, t2);
    } else {
        range.min = time + std::min(t1, t2);
        range.max = time + std::max(t1, t2);
    }

    for (int i = 0; i < kSourceClipCount; ++i) {
        if (useDefaultRange[i]) {
            OFX::Clip* clip = getClip(i);
            assert(clip);
            frames.setFramesNeeded(*clip, range);
        }
    }
}

// override the roi call
void
SeExprPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                      OFX::RegionOfInterestSetter &rois)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }

    if (!kSupportsTiles) {
        // The effect requires full images to render any region
        for (int i = 0; i < kSourceClipCount; ++i) {
            OfxRectD srcRoI;

            if (_srcClip[i] && _srcClip[i]->isConnected()) {
                srcRoI = _srcClip[i]->getRegionOfDefinition(args.time);
                rois.setRegionOfInterest(*_srcClip[i], srcRoI);
            }
        }
    } else {
       
        
        //Notify that we will need the RoI for all connected input clips at the current time
        for (int i = 0; i < kSourceClipCount; ++i) {
            OFX::Clip* clip = getClip(i);
            assert(clip);
            if (clip->isConnected()) {
                rois.setRegionOfInterest(*clip, args.regionOfInterest);
            }
        }
        
        //To determine the ROIs of the expression, we just execute the expression at the 4 corners of the render window
        //and record what are the calls made to getPixel in order to figure out the Roi.
        
        std::set<OFX::Clip*> processedClips;
        
        OFX::PixelComponentEnum outputComponents = getOutputComponents();
        if (outputComponents == OFX::ePixelComponentRGB || outputComponents == OFX::ePixelComponentRGBA) { // RGB || RGBA
            std::string rgbScript;
            _rgbScript->getValue(rgbScript);
            
            StubSeExpression expr(rgbScript,args.time);
            if (!expr.isValid()) {
                setPersistentMessage(OFX::Message::eMessageError, "", expr.parseError());
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            //Now evaluate the expression once and determine whether the user will call getPixel.
            //If he/she does, then we have no choice but to ask for the entire input image because we do not know
            //what the user may need (typically when applying UVMaps and stuff)
            
            (void)expr.evaluate();
            const FramesNeeded& framesNeeded = expr.getFramesNeeded();
            
            for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
                OFX::Clip* clip = getClip(it->first);
                assert(clip);
                std::pair<std::set<OFX::Clip*>::iterator,bool> ret = processedClips.insert(clip);
                if (ret.second) {
                    if (clip->isConnected()) {
                        rois.setRegionOfInterest(*clip, clip->getRegionOfDefinition(args.time));
                    }
                }
            }
        }
        if (outputComponents == OFX::ePixelComponentRGBA || outputComponents == OFX::ePixelComponentAlpha) { // RGBA || alpha
            std::string alphaScript;
            _alphaScript->getValue(alphaScript);
            
            StubSeExpression expr(alphaScript,args.time);
            if (!expr.isValid()) {
                setPersistentMessage(OFX::Message::eMessageError, "", expr.parseError());
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            //Now evaluate the expression once and determine whether the user will call getPixel.
            //If he/she does, then we have no choice but to ask for the entire input image because we do not know
            //what the user may need (typically when applying UVMaps and stuff)
            
            (void)expr.evaluate();
            const FramesNeeded& framesNeeded = expr.getFramesNeeded();
            
            for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
                OFX::Clip* clip = getClip(it->first);
                assert(clip);
                std::pair<std::set<OFX::Clip*>::iterator,bool> ret = processedClips.insert(clip);
                if (ret.second) {
                    if (clip->isConnected()) {
                        rois.setRegionOfInterest(*clip, clip->getRegionOfDefinition(args.time));
                    }
                }
            }
        }
        
    }
}

bool
SeExprPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }
    
    bool rodSet = false;
    
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    switch (boundingBox) {
        case eRegionOfDefinitionOptionUnion: {
            //union of inputs
            for (int i = 0; i < kSourceClipCount; ++i) {
                if (_srcClip[i]->isConnected()) {
                    OfxRectD srcRod = _srcClip[i]->getRegionOfDefinition(args.time);
                    if (rodSet) {
                        OFX::Coords::rectBoundingBox(srcRod, rod, &rod);
                    } else {
                        rod = srcRod;
                        rodSet = true;
                    }
                }
            }
            break;
        }
        case eRegionOfDefinitionOptionIntersection: {
            //intersection of inputs
            bool rodSet = false;
            for (int i = 0; i < kSourceClipCount; ++i) {
                if (_srcClip[i]->isConnected()) {
                    OfxRectD srcRod = _srcClip[i]->getRegionOfDefinition(args.time);
                    if (rodSet) {
                        OFX::Coords::rectIntersection<OfxRectD>(srcRod, rod, &rod);
                    } else {
                        rod = srcRod;
                    }
                    rodSet = true;
                }
            }
            break;
        }
        case eRegionOfDefinitionOptionSize: {
            // custom size
            _size->getValue(rod.x2, rod.y2);
            _btmLeft->getValue(rod.x1, rod.y1);
            rod.x2 += rod.x1;
            rod.y2 += rod.y1;

            rodSet = true;
            break;
        }
        case eRegionOfDefinitionOptionFormat: {
            // format

            int format_i;
            _format->getValue(format_i);
            double par = -1;
            int w = 0, h = 0;
            getFormatResolution( (OFX::EParamFormat)format_i, &w, &h, &par );
            assert(par != -1);
            rod.x1 = rod.y1 = 0;
            rod.x2 = w;
            rod.y2 = h;

            rodSet = true;
            break;
        }
        case eRegionOfDefinitionOptionProject: {
            // project
            OfxPointD size = getProjectSize();
            OfxPointD offset = getProjectOffset();
            rod.x1 = offset.x;
            rod.y1 = offset.y;
            rod.x2 = offset.x + size.x;
            rod.y2 = offset.y + size.y;
            rodSet =true;
            break;
        }
        default: {
            int inputIndex = boundingBox_i - (int)eRegionOfDefinitionOptionCustom;
            assert(inputIndex >= 0 && inputIndex < kSourceClipCount);
            rod = _srcClip[inputIndex]->getRegionOfDefinition(args.time);
            rodSet = true;
            break;
        }
    }
    if (!rodSet) {
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        rod.x1 = offset.x;
        rod.y1 = offset.y;
        rod.x2 = offset.x + size.x;
        rod.y2 = offset.y + size.y;
    }
    return true;
}


class SeExprInteract
: public OFX::RectangleInteract
{
public:
    
    SeExprInteract(OfxInteractHandle handle,
                      OFX::ImageEffect* effect);
    
    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL;
    virtual bool penMotion(const OFX::PenArgs &args) OVERRIDE FINAL;
    virtual bool penDown(const OFX::PenArgs &args) OVERRIDE FINAL;
    virtual bool penUp(const OFX::PenArgs &args) OVERRIDE FINAL;
    virtual bool keyDown(const OFX::KeyArgs &args) OVERRIDE FINAL;
    virtual bool keyUp(const OFX::KeyArgs & args) OVERRIDE FINAL;
    virtual void loseFocus(const OFX::FocusArgs &args) OVERRIDE FINAL;
    
private:
    
    virtual void aboutToCheckInteractivity(OfxTime time) OVERRIDE FINAL;
    virtual bool allowTopLeftInteraction() const OVERRIDE FINAL;
    virtual bool allowBtmRightInteraction() const OVERRIDE FINAL;
    virtual bool allowBtmLeftInteraction() const OVERRIDE FINAL;
    virtual bool allowBtmMidInteraction() const OVERRIDE FINAL;
    virtual bool allowMidLeftInteraction() const OVERRIDE FINAL;
    virtual bool allowCenterInteraction() const OVERRIDE FINAL;
    
    OFX::ChoiceParam* _boundingBox;
    RegionOfDefinitionEnum _bboxType;
};

SeExprInteract::SeExprInteract(OfxInteractHandle handle,
                                     OFX::ImageEffect* effect)
: RectangleInteract(handle,effect)
, _boundingBox(0)
, _bboxType(eRegionOfDefinitionOptionUnion)
{
    _boundingBox = effect->fetchChoiceParam(kParamRegionOfDefinition);
    assert(_boundingBox);
}

void SeExprInteract::aboutToCheckInteractivity(OfxTime /*time*/)
{
    int type_i;
    _boundingBox->getValue(type_i);
    _bboxType = (RegionOfDefinitionEnum)type_i;
}

bool SeExprInteract::allowTopLeftInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool SeExprInteract::allowBtmRightInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool SeExprInteract::allowBtmLeftInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool SeExprInteract::allowBtmMidInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool SeExprInteract::allowMidLeftInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool SeExprInteract::allowCenterInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::draw(const OFX::DrawArgs &args)
{
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    if (boundingBox != eRegionOfDefinitionOptionSize) {
        return false;
    }
    
    return RectangleInteract::draw(args);
}

bool
SeExprInteract::penMotion(const OFX::PenArgs &args)
{
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    if (boundingBox != eRegionOfDefinitionOptionSize) {
        return false;
    }
    
    return RectangleInteract::penMotion(args);
}

bool
SeExprInteract::penDown(const OFX::PenArgs &args)
{
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    if (boundingBox != eRegionOfDefinitionOptionSize) {
        return false;
    }
    
    return RectangleInteract::penDown(args);
}

bool
SeExprInteract::penUp(const OFX::PenArgs &args)
{
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    if (boundingBox != eRegionOfDefinitionOptionSize) {
        return false;
    }
    
    return RectangleInteract::penUp(args);
}

void
SeExprInteract::loseFocus(const OFX::FocusArgs &args)
{
    return RectangleInteract::loseFocus(args);
}



bool
SeExprInteract::keyDown(const OFX::KeyArgs &args)
{
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    if (boundingBox != eRegionOfDefinitionOptionSize) {
        return false;
    }
    
    return RectangleInteract::keyDown(args);
}

bool
SeExprInteract::keyUp(const OFX::KeyArgs & args)
{
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
    if (boundingBox != eRegionOfDefinitionOptionSize) {
        return false;
    }
    
    return RectangleInteract::keyUp(args);
}



class SeExprOverlayDescriptor
: public DefaultEffectOverlayDescriptor<SeExprOverlayDescriptor, SeExprInteract>
{
};

mDeclarePluginFactory(SeExprPluginFactory, {}, {});

void SeExprPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);
    //desc.addSupportedBitDepth(eBitDepthCustom);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(true);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    
#ifdef OFX_EXTENSIONS_NATRON
    if (OFX::getImageEffectHostDescription()->isNatron) {
        gHostIsNatron = true;
    } else {
        gHostIsNatron = false;
    }
#else 
    gHostIsNatron = false;
#endif
    
#if defined(OFX_EXTENSIONS_NATRON) && defined(OFX_EXTENSIONS_NUKE)
    // TODO @MrKepzie: can we support multiplanar even if host is not Natron?
    if (OFX::getImageEffectHostDescription()->isMultiPlanar &&
        OFX::getImageEffectHostDescription()->supportsDynamicChoices) {
        gHostIsMultiPlanar = true;
        desc.setIsMultiPlanar(true);
        desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelPassThroughNonRenderedPlanes);
    } else {
        gHostIsMultiPlanar = false;
    }
#else 
    gHostIsMultiPlanar = false;
#endif
    
    desc.setOverlayInteractDescriptor(new SeExprOverlayDescriptor);
}

void SeExprPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

    for (ImageEffectHostDescription::PixelComponentArray::const_iterator it = getImageEffectHostDescription()->_supportedComponents.begin();
         it != getImageEffectHostDescription()->_supportedComponents.end();
         ++it) {
        switch (*it) {
            case ePixelComponentRGBA:
                gHostSupportsRGBA  = true;
                break;
            case ePixelComponentRGB:
                gHostSupportsRGB = true;
                break;
            case ePixelComponentAlpha:
                gHostSupportsAlpha = true;
                break;
            default:
                // other components are not supported by this plugin
                break;
        }
    }

    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kSourceClipCount; ++i) {
        
        std::string clipName;
        {
            char name[256];
            snprintf(name, sizeof(name), "%d", i+1);
            clipName.append(name);
        }
        ClipDescriptor *srcClip;
        if (i == 0 && context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName); // mandatory clip for the filter context
        } else {
            srcClip = desc.defineClip(clipName);
        }
        if (gHostSupportsRGBA) {
            srcClip->addSupportedComponent(ePixelComponentRGBA);
        }
        if (gHostSupportsRGB) {
            srcClip->addSupportedComponent(ePixelComponentRGB);
        }
        if (gHostSupportsAlpha) {
            srcClip->addSupportedComponent(ePixelComponentAlpha);
        }
        //srcClip->addSupportedComponent(ePixelComponentCustom);
        srcClip->setTemporalClipAccess(true);
        srcClip->setSupportsTiles(true);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
    }

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    if (gHostSupportsRGBA) {
        dstClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (gHostSupportsRGB) {
        dstClip->addSupportedComponent(ePixelComponentRGB);
    }
    if (gHostSupportsAlpha) {
        dstClip->addSupportedComponent(ePixelComponentAlpha);
    }
    //dstClip->addSupportedComponent(ePixelComponentCustom);
    dstClip->setSupportsTiles(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamRegionOfDefinition);
        param->setLabel(kParamRegionOfDefinitionLabel);
        param->setHint(kParamRegionOfDefinitionHint);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);

        assert(param->getNOptions() == eRegionOfDefinitionOptionUnion);
        param->appendOption(kParamRegionOfDefinitionOptionUnion, kParamRegionOfDefinitionOptionUnionHelp);
        assert(param->getNOptions() == eRegionOfDefinitionOptionIntersection);
        param->appendOption(kParamRegionOfDefinitionOptionIntersection, kParamRegionOfDefinitionOptionIntersectionHelp);
        assert(param->getNOptions() == eRegionOfDefinitionOptionSize);
        param->appendOption(kParamRegionOfDefinitionOptionSize, kParamRegionOfDefinitionOptionSizeHelp);
        assert(param->getNOptions() == eRegionOfDefinitionOptionFormat);
        param->appendOption(kParamRegionOfDefinitionOptionFormat, kParamRegionOfDefinitionOptionFormatHelp);
        assert(param->getNOptions() == eRegionOfDefinitionOptionProject);
        param->appendOption(kParamRegionOfDefinitionOptionProject, kParamRegionOfDefinitionOptionProjectHelp);

        assert(param->getNOptions() == eRegionOfDefinitionOptionCustom);
        char name[256], help[256];
        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), kParamRegionOfDefinitionOptionCustomInput "%d", i+1);
            snprintf(help, sizeof(help), kParamRegionOfDefinitionOptionCustomInputHelp "%d", i+1);
            param->appendOption(name, help);
        }
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputComponents);
        param->setLabel(kParamOutputComponentsLabel);
        param->setHint(kParamOutputComponentsHint);
        int i = 0;

        if (gHostSupportsRGBA) {
            gOutputComponentsMap[i] = ePixelComponentRGBA;
            ++i;
            // coverity[check_return]
            assert(param->getNOptions() >= 0 && gOutputComponentsMap[param->getNOptions()] == ePixelComponentRGBA);
            param->appendOption(kParamOutputComponentsOptionRGBA);
        }
        if (gHostSupportsRGB) {
            gOutputComponentsMap[i] = ePixelComponentRGB;
            ++i;
            // coverity[check_return]
            assert(param->getNOptions() >= 0 && gOutputComponentsMap[param->getNOptions()] == ePixelComponentRGB);
            param->appendOption(kParamOutputComponentsOptionRGB);
        }
        if (gHostSupportsAlpha) {
            gOutputComponentsMap[i] = ePixelComponentAlpha;
            ++i;
            // coverity[check_return]
            assert(param->getNOptions() >= 0 && gOutputComponentsMap[param->getNOptions()] == ePixelComponentAlpha);
            param->appendOption(kParamOutputComponentsOptionAlpha);
        }
        gOutputComponentsMap[i] = ePixelComponentNone;

        param->setDefault(0); // default to the first one available, i.e. the most chromatic
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamGeneratorFormat);
        param->setLabel(kParamGeneratorFormatLabel);
        assert(param->getNOptions() == eParamFormatPCVideo);
        param->appendOption(kParamFormatPCVideoLabel);
        assert(param->getNOptions() == eParamFormatNTSC);
        param->appendOption(kParamFormatNTSCLabel);
        assert(param->getNOptions() == eParamFormatPAL);
        param->appendOption(kParamFormatPALLabel);
        assert(param->getNOptions() == eParamFormatHD);
        param->appendOption(kParamFormatHDLabel);
        assert(param->getNOptions() == eParamFormatNTSC169);
        param->appendOption(kParamFormatNTSC169Label);
        assert(param->getNOptions() == eParamFormatPAL169);
        param->appendOption(kParamFormatPAL169Label);
        assert(param->getNOptions() == eParamFormat1kSuper35);
        param->appendOption(kParamFormat1kSuper35Label);
        assert(param->getNOptions() == eParamFormat1kCinemascope);
        param->appendOption(kParamFormat1kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat2kSuper35);
        param->appendOption(kParamFormat2kSuper35Label);
        assert(param->getNOptions() == eParamFormat2kCinemascope);
        param->appendOption(kParamFormat2kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat4kSuper35);
        param->appendOption(kParamFormat4kSuper35Label);
        assert(param->getNOptions() == eParamFormat4kCinemascope);
        param->appendOption(kParamFormat4kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormatSquare256);
        param->appendOption(kParamFormatSquare256Label);
        assert(param->getNOptions() == eParamFormatSquare512);
        param->appendOption(kParamFormatSquare512Label);
        assert(param->getNOptions() == eParamFormatSquare1k);
        param->appendOption(kParamFormatSquare1kLabel);
        assert(param->getNOptions() == eParamFormatSquare2k);
        param->appendOption(kParamFormatSquare2kLabel);
        param->setDefault(0);
        param->setHint(kParamGeneratorFormatHint);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    
    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabel(kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setIncrement(1.);
        param->setHint("Coordinates of the bottom left corner of the size rectangle.");
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }
    
    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabel(kParamRectangleInteractSizeLabel);
        param->setDoubleType(OFX::eDoubleTypeXY);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(1., 1.);
        param->setRange(0, 0, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0, 0, 10000., 10000.);
        param->setIncrement(1.);
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim1);
        param->setHint("Width and height of the size rectangle.");
        param->setIncrement(1.);
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRectangleInteractInteractive);
        param->setLabel(kParamRectangleInteractInteractiveLabel);
        param->setHint(kParamRectangleInteractInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    char name[1024], label[1024], hint[1024];

    if (gHostIsMultiPlanar) {
        GroupParamDescriptor *group = desc.defineGroupParam("Input layers");
        group->setLabel("Input layers");
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }
        for (int i = 0; i < kSourceClipCount; ++i) {
            {
                snprintf(name, sizeof(name), kParamLayerInput "%d", i+1);
                snprintf(label, sizeof(label), kParamLayerInputLabel "%d", i+1);
                snprintf(hint, sizeof(hint), kParamLayerInputHint "%d", i+1);
                ChoiceParamDescriptor *param = desc.defineChoiceParam(name);
                param->setLabel(label);
                param->setHint(hint);
                param->setAnimates(false);
                param->appendOption(kSeExprColorPlaneName);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                param->setEvaluateOnChange(false);
                param->setIsPersistant(false);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                snprintf(name, sizeof(name), kParamLayerInputChoice "%d", i+1);
                snprintf(label, sizeof(label), kParamLayerInputLabel "Choice %d", i+1);
                StringParamDescriptor *param = desc.defineStringParam(name);
                param->setLabel(label);
                param->setIsSecret(true);
                param->setParent(*group);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam("Scalar Parameters");
        group->setLabel("Scalar Parameters");
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }

        {
            IntParamDescriptor* param = desc.defineIntParam(kParamDoubleParamNumber);
            param->setLabel(kParamDoubleParamNumberLabel);
            param->setHint(kParamDoubleParamNumberHint);
            param->setRange(0, kParamsCount);
            param->setDisplayRange(0, kParamsCount);
            param->setDefault(0);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), kParamDouble "%d", i+1);
            snprintf(label, sizeof(label), kParamDoubleLabel "%d", i+1);
            snprintf(hint, sizeof(hint), kParamDoubleHint "%d", i+1);
            DoubleParamDescriptor *param = desc.defineDoubleParam(name);
            param->setLabel(label);
            param->setHint(hint);
            param->setAnimates(true);
            //param->setIsSecret(true); // done in the plugin constructor
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(-1000.,1000.);
            param->setDoubleType(OFX::eDoubleTypePlain);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam("Position Parameters");
        group->setLabel("Position Parameters");
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }

        {
            IntParamDescriptor* param = desc.defineIntParam(kParamDouble2DParamNumber);
            param->setLabel(kParamDouble2DParamNumberLabel);
            param->setHint(kParamDouble2DParamNumberHint);
            param->setRange(0, kParamsCount);
            param->setDisplayRange(0, kParamsCount);
            param->setDefault(0);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }

        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), kParamDouble2D "%d", i+1);
            snprintf(label, sizeof(label), kParamDouble2DLabel "%d", i+1);
            snprintf(hint, sizeof(hint), kParamDouble2DHint "%d", i+1);
            Double2DParamDescriptor *param = desc.defineDouble2DParam(name);
            param->setLabel(label);
            param->setHint(hint);
            param->setAnimates(true);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
            param->setDisplayRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
            //param->setIsSecret(true); // done in the plugin constructor
            param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
            bool hostHasNativeOverlayForPosition = param->getHostHasNativeOverlayHandle();
            if (hostHasNativeOverlayForPosition) {
                param->setUseHostNativeOverlayHandle(true);
            }
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor *group = desc.defineGroupParam("Color Parameters");
        group->setLabel("Color Parameters");
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }
        {
            IntParamDescriptor* param = desc.defineIntParam(kParamColorNumber);
            param->setLabel(kParamColorNumberLabel);
            param->setHint(kParamColorNumberHint);
            param->setRange(0, kParamsCount);
            param->setDisplayRange(0, kParamsCount);
            param->setDefault(0);
            param->setAnimates(false);
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
            }
        }
        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), kParamColor "%d", i+1);
            snprintf(label, sizeof(label), kParamColorLabel "%d", i+1);
            snprintf(hint, sizeof(hint), kParamColorHint "%d", i+1);
            RGBParamDescriptor *param = desc.defineRGBParam(name);
            param->setLabel(label);
            param->setHint(hint);
            param->setAnimates(true);
            param->setParent(*group);
            //param->setIsSecret(true); // done in the plugin constructor
            if (page) {
                page->addChild(*param);
            }
        }
    }
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setDefault(kParamFrameRangeDefault);
        param->setHint(kParamFrameRangeHint);
        param->setLabel(kParamFrameRangeLabel);
        param->setDimensionLabels("min", "max");
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFrameRangeAbsolute);
        param->setDefault(kParamFrameRangeAbsoluteDefault);
        param->setHint(kParamFrameRangeAbsoluteHint);
        param->setLabel(kParamFrameRangeAbsoluteLabel);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor *param = desc.defineStringParam(kParamScript);
        param->setLabel(kParamScriptLabel);
        param->setHint(kParamScriptHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault(kSeExprDefaultRGBScript);
        if (page) {
            page->addChild(*param);
        }
    }
    if (!gHostIsNatron) {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamShowScript);
        param->setLabel(kParamShowScriptLabel);
        param->setHint(kParamShowScriptHint);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor *param = desc.defineStringParam(kParamAlphaScript);
        param->setLabel(kParamAlphaScriptLabel);
        param->setHint(kParamAlphaScriptHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault(kSeExprDefaultAlphaScript);
        if (page) {
            page->addChild(*param);
        }
    }
    if (!gHostIsNatron) {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamShowAlphaScript);
        param->setLabel(kParamShowAlphaScriptLabel);
        param->setHint(kParamShowAlphaScriptHint);
        if (page) {
            page->addChild(*param);
        }
    }


    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabel(kParamValidateLabel);
        param->setHint(kParamValidateHint);
        param->setEvaluateOnChange(true);
        if (gHostIsNatron) {
            param->setIsSecret(true);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* SeExprPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new SeExprPlugin(handle);
}

static SeExprPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
