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

#include <cfloat> // DBL_MAX
#include <vector>
#include <algorithm>
#include <limits>
#include <set>

//#include <stdio.h> // for snprintf & _snprintf
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

#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsMultiThread.h"
#include "ofxsFormatResolution.h"
#include "ofxsRectangleInteract.h"
#include "ofxsFilter.h"

GCC_DIAG_OFF(deprecated)
#include <SeExpression.h>
#include <SeExprFunc.h>
#include <SeExprNode.h>
#include <SeExprBuiltins.h>
#include <SeMutex.h>
GCC_DIAG_ON(deprecated)

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

using namespace OFX;

using std::string;
using std::vector;
using std::map;
using std::pair;
using std::make_pair;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "SeExpr"
#define kPluginNameSimple "SeExprSimple"
#define kPluginGrouping "Merge"
#define kPluginDescriptionHead \
    "Use the SeExpr expression language (by Walt Disney Animation Studios) to process images.\n" \
    "\n" \
    "### What is SeExpr?\n" \
    "\n" \
    "SeExpr is a very simple mathematical expression language used in graphics software (RenderMan, Maya, Mudbox, Yeti).\n" \
    "\n" \
    "See the [SeExpr Home Page](http://www.disneyanimation.com/technology/seexpr.html) and " \
    "[SeExpr Language Documentation](http://wdas.github.io/SeExpr/doxygen/userdoc.html) " \
    "for more information.\n" \
    "\n" \
    "SeExpr is licensed under the Apache License, Version 2.0, and is Copyright Disney Enterprises, Inc.\n" \
    "\n" \
    "### SeExpr vs. SeExprSimple\n" \
    "\n" \
    "The SeExpr plugin comes in two versions:\n" \
    "\n" \
    "- *SeExpr* has a single vector expression for the color channels, and a scalar expression for the alpha channel. The source color is accessed through the `Cs`vector, and alpha through the `As` scalar, as specified in the original SeExpr language.\n" \
    "- *SeExprSimple* has one scalar expression per channel, and the source channels may also be accessed through scalars (`r`, `g`, `b`, `a`).\n" \
    "\n" \
    "### SeExpr extensions\n" \
    "\n" \
    "A few pre-defined variables and functions were added to the language for filtering and blending several input images.\n" \
    "\n" \
    "The following pre-defined variables can be used in the script:\n" \
    "\n" \
    "- `x`: X coordinate (in pixel units) of the pixel to render.\n" \
    "- `y`: Y coordinate (in pixel units) of the pixel to render.\n" \
    "- `u`: X coordinate (normalized in the [0,1] range) of the output pixel to render.\n" \
    "- `v`: Y coordinate (normalized in the [0,1] range) of the output pixel to render.\n" \
    "- `sx`, `sy`: Scale at which the image is being rendered. Depending on the zoom level " \
    "of the viewer, the image might be rendered at a lower scale than usual. " \
    "This parameter is useful when producing spatial effects that need to be invariant " \
    "to the pixel scale, especially when using X and Y coordinates. (0.5,0.5) means that the " \
    "image is being rendered at half of its original size.\n" \
    "- `par`: The pixel aspect ratio.\n" \
    "- `cx`, `cy`: Shortcuts for `(x + 0.5)/par/sx` and `(y + 0.5)/sy`, i.e. the canonical " \
    "coordinates of the current pixel.\n" \
    "- `frame`: Current frame being rendered\n"
#define kPluginDescriptionMid ""
#define kPluginDescriptionMidSimple \
    "- *SeExprSimple only:* `r`, `g`, `b`, `a`: RGBA channels (scalar) of the image from input 1.\n" \
    "- *SeExprSimple only:* `rN`, `gN`, `bN`, `aN`: RGBA channels (scalar) of the image from input N, " \
    "e.g. `r2` and `a2` are red and alpha channels from input 2.\n"
#define kPluginDescriptionFoot \
    "- `Cs`, `As`: Color (RGB vector) and alpha (scalar) of the image from input 1.\n" \
    "- `CsN`, `AsN`: Color (RGB vector) and alpha (scalar) of the image from input N, " \
    "e.g. `Cs2` and `As2` for input 2.\n" \
    "- `output_width`, `output_height`: Dimensions of the output image being rendered.\n" \
    "- `input_width`, `input_height`: Dimensions of image from input 1, in pixels.\n" \
    "- `input_widthN`, `input_heightN`: Dimensions of image from input N, e.g. `input_width2` and " \
    "`input_height2` for input 2.\n" \
    "\n" \
    "The following additional functions are available:\n" \
    "\n" \
    "- `color cpixel(int i, int f, float x, float y, int interp = 0)`: interpolates the " \
    "color from input i at the pixel position (x,y) in the image, at frame f.\n" \
    "- `float apixel(int i, int f, float x, float y, int interp = 0)`: interpolates the " \
    "alpha from input i at the pixel position (x,y) in the image, at frame f.\n" \
    "\n" \
    "The pixel position of the center of the bottom-left pixel is (0., 0.).\n" \
    "\n" \
    "The first input has index i=1.\n" \
    "\n" \
    "`interp` controls the interpolation filter, and can take one of the following values:\n" \
    "\n" \
    "- 0: impulse - (nearest neighbor / box) Use original values\n" \
    "- 1: bilinear - (tent / triangle) Bilinear interpolation between original values\n" \
    "- 2: cubic - (cubic spline) Some smoothing\n" \
    "- 3: Keys - (Catmull-Rom / Hermite spline) Some smoothing, plus minor sharpening (*)\n" \
    "- 4: Simon - Some smoothing, plus medium sharpening (*)\n" \
    "- 5: Rifman - Some smoothing, plus significant sharpening (*)\n" \
    "- 6: Mitchell - Some smoothing, plus blurring to hide pixelation (*+)\n" \
    "- 7: Parzen - (cubic B-spline) Greatest smoothing of all filters (+)\n" \
    "- 8: notch - Flat smoothing (which tends to hide moire' patterns) (+)\n" \
    "\n" \
    "Some filters may produce values outside of the initial range (*) or modify the values even at integer positions (+).\n" \
    "\n" \
    "### Sample scripts\n" \
    "\n" \
    "#### Add green channel to red, keep green, and apply a 50% gain on blue\n" \
    "\n" \
    "*SeExprSimple:*\n" \
    "\n" \
    "    r+g\n" \
    "    g\n" \
    "    0.5*b\n" \
    "\n" \
    "*SeExpr:*\n" \
    "\n" \
    "    [Cs[0]+Cs[1], Cs[1], 0.5*Cs[2]]\n" \
    "\n" \
    "#### \"Multiply\" merge operator on inputs 1 and 2\n" \
    "\n" \
    "*SeExprSimple:*\n" \
    "\n" \
    "    r*r2\n" \
    "    g*g2" \
    "    b*b2\n" \
    "    a+a2-a*a2\n" \
    "\n" \
    "*SeExpr:*\n" \
    "\n" \
    "    Cs * Cs2\n" \
    "    As + As2 - As * As2\n" \
    "\n" \
    "#### \"Over\" merge operator on inputs 1 and 2\n" \
    "\n" \
    "*SeExprSimple:*\n" \
    "\n" \
    "    r+r2*(1-a)\n" \
    "    g+g2*(1-a)\n" \
    "    b+b2*(1-a)\n" \
    "    a+a2-a*a2\n" \
    "\n" \
    "*SeExpr:*\n" \
    "\n" \
    "    Cs + Cs2 * (1 -  As)\n" \
    "    As + As2 - As * As2\n" \
    "\n" \
    "#### Generating a time-varying colored Perlin noise with size x1\n" \
    "\n" \
    "    cnoise([cx/x1,cy/x1,frame])\n" \
    "\n" \
    "#### Average pixels over the previous, current and next frame\n" \
    "\n" \
    "*SeExpr:*\n" \
    "\n" \
    "    prev = cpixel(1,frame - 1,x,y);\n" \
    "    cur = Cs;\n" \
    "    next = cpixel(1,frame + 1,x,y);\n" \
    "    (prev + cur + next) / 3;\n" \
    "\n" \
    "### Custom parameters\n" \
    "\n" \
    "To use custom variables that are pre-defined in the plug-in (scalars, positions and colors) you must reference them " \
    "using their script-name in the expression. For example, the parameter x1 can be referenced using x1 in the script:\n" \
    "\n" \
    "    Cs + x1\n" \
    "\n" \
    "### Multi-instruction expressions\n" \
    "\n" \
    "If an expression spans multiple instructions (usually written one per line), " \
    "each instruction must end with a semicolumn (';'). The last instruction " \
    "of the expression is considered as the final value of the pixel (a RGB vector or an Alpha scalar, depending " \
    "on the script), and must not be terminated by a semicolumn.\n" \
    "More documentation is available on the [SeExpr website](http://www.disneyanimation.com/technology/seexpr.html).\n" \
    "\n" \
    "### Accessing pixel values from other frames\n" \
    "\n" \
    "The input frame range used to render a given output frame is computed automatically if the following conditions hold:\n" \
    "- The `frame` parameter to cpixel/apixel must not depend on the color or alpha of a pixel, nor on the result of another call to cpixel/apixel\n" \
    "- A call to cpixel/apixel must not depend on the color or alpha of a pixel, as in the following:\n" \
    "\n" \
    "    if (As > 0.1) {\n" \
    "        src = cpixel(1,frame,x,y);\n" \
    "    } else {\n" \
    "        src = [0,0,0];\n" \
    "    }\n" \
    "If one of these conditions does not hold, all frames from the specified input frame range are asked for.\n"
#define kPluginDescription kPluginDescriptionHead kPluginDescriptionMid kPluginDescriptionFoot
#define kPluginDescriptionSimple kPluginDescriptionHead kPluginDescriptionMidSimple kPluginDescriptionFoot

#define kPluginIdentifier "fr.inria.openfx.SeExpr"
#define kPluginIdentifierSimple "fr.inria.openfx.SeExprSimple"
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
#define kSeExprRVarName "r"
#define kSeExprGVarName "g"
#define kSeExprBVarName "b"
#define kSeExprAVarName "a"
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
enum RegionOfDefinitionEnum
{
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
#define kParamLayerInputLabel "Input Layer "
#define kParamLayerInputChoice kParamLayerInput "Choice"
#define kParamLayerInputChoiceLabel kParamLayerInputLabel "Choice "
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
#define kParamFrameRangeDefault 0, 0

#define kParamFrameRangeAbsolute "frameRangeAbsolute"
#define kParamFrameRangeAbsoluteLabel "Absolute Frame Range"
#define kParamFrameRangeAbsoluteHint "If checked, the frame range is given as absolute frame numbers, else it is relative to the current frame."
#define kParamFrameRangeAbsoluteDefault false

#define kParamRExpr "rExpr"
#define kParamRExprLabel "R="
#define kParamRExprHint "Expression to compute the output red channel. If empty, the channel is left unchanged."
#define kParamGExpr "gExpr"
#define kParamGExprLabel "G="
#define kParamGExprHint "Expression to compute the output green channel. If empty, the channel is left unchanged."
#define kParamBExpr "bExpr"
#define kParamBExprLabel "B="
#define kParamBExprHint "Expression to compute the output blue channel. If empty, the channel is left unchanged."
#define kParamAExpr "aExpr"
#define kParamAExprLabel "A="
#define kParamAExprHint "Expression to compute the output alpha channel. If empty, the channel is left unchanged."

#define kNukeWarnTcl "On Nuke, the characters '$', '[' ']' must be preceded with a backslash (as '\\$', '\\[', '\\]') to avoid TCL variable and expression substitution."

#define kParamScript "script"
#define kParamScriptLabel "RGB Script"
#define kParamScriptHint "Contents of the SeExpr expression. This expression should output the RGB components as a SeExpr vector. See the description of the plug-in and " \
    "http://www.disneyanimation.com/technology/seexpr.html for documentation."

#define kParamShowExprs "showExprs"
#define kParamShowExprsLabel "Show Exprs"
#define kParamShowExprsHint "Show the contents of the expressions as seen by SeExpr in a dialog window. It may be different from the expressions visible in the GUI, because the host may perform variable or expression substitution on the expressions."

#define kParamShowScript "showScript"
#define kParamShowScriptLabel "Show RGB Script"
#define kParamShowScriptHint "Show the contents of the RGB script as seen by SeExpr in a dialog window. It may be different from the script visible in the GUI, because the host may perform variable or expression substitution on the RGB script parameter."

#define kParamAlphaScript "alphaScript"
#define kParamAlphaScriptLabel "Alpha Script"
#define kParamAlphaScriptHint "Contents of the SeExpr expression. This expression should output the alpha component only as a scalar. See the description of the plug-in and " \
    "http://www.disneyanimation.com/technology/seexpr.html for documentation."

#define kParamShowAlphaScript "showAlphaScript"
#define kParamShowAlphaScriptLabel "Show Alpha Script"
#define kParamShowAlphaScriptHint "Show the contents of the Alpha script as seen by SeExpr in a dialog window. It may be different from the script visible in the GUI, because the host may perform variable or expression substitution on the Alpha script parameter."

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
static PixelComponentEnum gOutputComponentsMap[4];
static
string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb = (char)( '0' + (j % 10) ) + nb;
    }

    return nb;
}

// Check if s consists only of whitespaces
static
bool
isSpaces(const string& s)
{
    return s.find_first_not_of(" \t\n\v\f\r") == string::npos;
}

template<typename T>
static inline void
unused(const T&) {}

class SeExprProcessorBase;


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SeExprPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    SeExprPlugin(OfxImageEffectHandle handle, bool simple);

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    // override isIdentity
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;
    virtual void changedClip(const InstanceChangedArgs &args, const string &clipName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    virtual void getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
    Clip* getClip(int index) const
    {
        assert(index >= 0 && index < kSourceClipCount);

        return _srcClip[index];
    }

    DoubleParam**  getDoubleParams()  { return _doubleParams; }

    Double2DParam**  getDouble2DParams()  { return _double2DParams; }

    RGBParam**  getRGBParams()  { return _colorParams; }

private:

    void buildChannelMenus();

    void setupAndProcess(SeExprProcessorBase & processor, const RenderArguments &args);

    PixelComponentEnum getOutputComponents() const;

    string getOfxComponentsForClip(int inputNumber) const;

    string getOfxPlaneForClip(int inputNumber) const;

private:
    const bool _simple;
    Clip *_srcClip[kSourceClipCount];
    Clip* _maskClip;
    Clip *_dstClip;

    vector<vector<string> > _clipLayerOptions;
    ChoiceParam *_clipLayerToFetch[kSourceClipCount];
    StringParam *_clipLayerToFetchString[kSourceClipCount];
    IntParam *_doubleParamCount;
    DoubleParam* _doubleParams[kParamsCount];
    IntParam *_double2DParamCount;
    Double2DParam* _double2DParams[kParamsCount];
    IntParam *_colorParamCount;
    RGBParam* _colorParams[kParamsCount];
    Int2DParam *_frameRange;
    BooleanParam *_frameRangeAbsolute;
    StringParam *_rExpr;
    StringParam *_gExpr;
    StringParam *_bExpr;
    StringParam *_aExpr;
    StringParam *_rgbScript;
    StringParam *_alphaScript;
    BooleanParam *_validate;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    ChoiceParam* _boundingBox;
    ChoiceParam* _format;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    BooleanParam* _interactive;
    ChoiceParam* _outputComponents;
};

PixelComponentEnum
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
    string _layersToFetch[kSourceClipCount];
    OFXSeExpression* _rExpr;
    OFXSeExpression* _gExpr;
    OFXSeExpression* _bExpr;
    OFXSeExpression* _rgbExpr;
    OFXSeExpression* _alphaExpr;
    const Image* _srcCurTime[kSourceClipCount];
    int _nSrcComponents[kSourceClipCount];
    Image* _dstImg;
    bool _maskInvert;
    const Image* _maskImg;
    bool _doMasking;
    double _mix;

    // <clipIndex, <time, image> >
    typedef map<OfxTime, const Image*> FetchedImagesForClipMap;
    typedef map<int, FetchedImagesForClipMap> FetchedImagesMap;
    FetchedImagesMap _images;

public:

    SeExprProcessorBase(SeExprPlugin* instance);

    virtual ~SeExprProcessorBase();

    SeExprPlugin* getPlugin() const
    {
        return _plugin;
    }

    void setDstImg(Image* dstImg)
    {
        _dstImg = dstImg;
    }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }


    void setValues(OfxTime time,
                   int view,
                   double mix,
                   const string& rgbExpr,
                   const string& alphaExpr,
                   string* layers,
                   const OfxRectI& dstPixelRod,
                   OfxPointI* inputSizes,
                   const OfxPointI& outputSize,
                   const OfxPointD& renderScale,
                   double par);

    void setValuesSimple(OfxTime time,
                         int view,
                         double mix,
                         const string& rExpr,
                         const string& gExpr,
                         const string& bExpr,
                         const string& aExpr,
                         string* layers,
                         const OfxRectI& dstPixelRod,
                         OfxPointI* inputSizes,
                         const OfxPointI& outputSize,
                         const OfxPointD& renderScale,
                         double par);

    bool isExprOk(string* error);

    void prefetchImage(int inputIndex,
                       OfxTime time)
    {
        // find or create input
        FetchedImagesForClipMap& foundInput = _images[inputIndex];

        FetchedImagesForClipMap::iterator foundImage = foundInput.find(time);
        if ( foundImage != foundInput.end() ) {
            // image already fetched
            return;
        }

        Clip* clip = _plugin->getClip(inputIndex);
        assert(clip);

        if ( !clip->isConnected() ) {
            // clip is not connected, image is NULL
            return;
        }

        Image *img;
        if (gHostIsMultiPlanar) {
            img = clip->fetchImagePlane( time, _renderView,  _layersToFetch[inputIndex].c_str() );
        } else {
            img = clip->fetchImage(time);
        }
        if (!img) {
            return;
        }
        pair<FetchedImagesForClipMap::iterator, bool> ret = foundInput.insert( make_pair(time, img) );
        assert(ret.second);
    }

    const Image* getImage(int inputIndex,
                          OfxTime time)
    {
        // find or create input
        FetchedImagesForClipMap& foundInput = _images[inputIndex];

        FetchedImagesForClipMap::iterator foundImage = foundInput.find(time);
        if ( foundImage != foundInput.end() ) {
            return foundImage->second;
        }

        return NULL;
    }

    virtual void process(OfxRectI procWindow) = 0;

private:
    void setExprs(OfxTime time,
                  const string& rgbExpr,
                  const string& alphaExpr,
                  const OfxRectI& dstPixelRod,
                  const OfxPointD& renderScale,
                  double par);

    void setExprsSimple(OfxTime time,
                        const string& rExpr,
                        const string& gExpr,
                        const string& bExpr,
                        const string& aExpr,
                        const OfxRectI& dstPixelRod,
                        const OfxPointD& renderScale,
                        double par);

    void setValuesOther(OfxTime time,
                        int view,
                        double mix,
                        string* layers,
                        OfxPointI* inputSizes,
                        const OfxPointI& outputSize);
};


// implementation of the "apixel" function
template <typename PIX, int nComps, FilterEnum interp, bool alpha>
static void
pixelForDepthCompsFilter(const Image* img,
                         double x,
                         double y,
                         SeVec3d& result)
{
    result.setValue(0., 0., 0.);
    if ( ( alpha && (nComps != 1) && (nComps != 4) ) ||
         ( !alpha && ( nComps <= 1) ) ) {
        // no value
        return;
    }
    float pix[4];
    // In OFX pixel coordinates, the center of pixel (0,0) has coordinates (0.5,0.5)
    ofxsFilterInterpolate2D<PIX, nComps, interp, /*clamp=*/ true>(x + 0.5, y + 0.5, img, /*blackOutside=*/ false, pix);
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
static void
pixelForDepthComps(const Image* img,
                   FilterEnum interp,
                   double x,
                   double y,
                   SeVec3d& result)
{
    switch (interp) {
    case eFilterImpulse:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterImpulse, alpha>(img, x, y, result);
    case eFilterBilinear:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterBilinear, alpha>(img, x, y, result);
    case eFilterCubic:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterCubic, alpha>(img, x, y, result);
    case eFilterKeys:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterKeys, alpha>(img, x, y, result);
    case eFilterSimon:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterSimon, alpha>(img, x, y, result);
    case eFilterRifman:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterRifman, alpha>(img, x, y, result);
    case eFilterMitchell:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterMitchell, alpha>(img, x, y, result);
    case eFilterParzen:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterParzen, alpha>(img, x, y, result);
    case eFilterNotch:

        return pixelForDepthCompsFilter<PIX, nComps, eFilterNotch, alpha>(img, x, y, result);
    default:
        result.setValue(0., 0., 0.);
    }
}

template <typename PIX, bool alpha>
static void
pixelForDepth(const Image* img,
              FilterEnum interp,
              double x,
              double y,
              SeVec3d& result)
{
    int nComponents = img->getPixelComponentCount();

    switch (nComponents) {
    case 1:

        return pixelForDepthComps<PIX, 1, alpha>(img, interp, x, y, result);
    case 2:

        return pixelForDepthComps<PIX, 2, alpha>(img, interp, x, y, result);
    case 3:

        return pixelForDepthComps<PIX, 3, alpha>(img, interp, x, y, result);
    case 4:

        return pixelForDepthComps<PIX, 4, alpha>(img, interp, x, y, result);
    default:
        result.setValue(0., 0., 0.);
    }
}

template<bool alpha>
class PixelFuncX
    : public SeExprFuncX
{
    SeExprProcessorBase* _processor;

public:


    PixelFuncX(SeExprProcessorBase* processor)
        : SeExprFuncX(true) // Thread Safe
        , _processor(processor)
    {}

    virtual ~PixelFuncX() {}

private:

    virtual bool prep(SeExprFuncNode* node,
                      bool /*wantVec*/)
    {
        // check number of arguments
        int nargs = node->nargs();

        if ( (nargs < 4) || (5 < nargs) ) {
            node->addError("Wrong number of arguments, should be 4 or 5");

            return false;
        }

        for (int i = 0; i < nargs; ++i) {
            if ( node->child(i)->isVec() ) {
                node->addError("Wrong arguments, should be all scalars");

                return false;
            }
            if ( !node->child(i)->prep(false) ) {
                return false;
            }
        }

        SeVec3d v;
        node->child(0)->eval(v);
        int inputIndex = (int)SeExpr::round(v[0]) - 1;
        if ( (inputIndex < 0) || (inputIndex >= kSourceClipCount) ) {
            node->addError("Invalid input index");

            return false;
        }

        return true;
    }

    virtual void eval(const SeExprFuncNode* node,
                      SeVec3d& result) const
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
        FilterEnum interp = eFilterImpulse;
        if (node->nargs() == 5) {
            node->child(4)->eval(v);
            int interp_i = SeExpr::round(v[0]);
            if (interp_i < 0) {
                interp_i = 0;
            } else if (interp_i > (int)eFilterNotch) {
                interp_i = (int)eFilterNotch;
            }
            interp = (FilterEnum)interp_i;
        }
        if ( (frame != frame) || (x != x) || (y != y) ) {
            // one of the parameters is NaN
            result.setValue(0., 0., 0.);

            return;
        }
        _processor->prefetchImage(inputIndex, frame);
        const Image* img = _processor->getImage(inputIndex, frame);
        if (!img) {
            // be black and transparent
            result.setValue(0., 0., 0.);
        } else {
            BitDepthEnum depth = img->getPixelDepth();
            switch (depth) {
            case eBitDepthFloat:
                pixelForDepth<float, alpha>(img, interp, x, y, result);
                break;
            case eBitDepthUByte:
                pixelForDepth<unsigned char, 255>(img, interp, x, y, result);
                break;
            case eBitDepthUShort:
                pixelForDepth<unsigned short, 65535>(img, interp, x, y, result);
                break;
            default:
                result.setValue(0., 0., 0.);
                break;
            }
        }
    } // eval
};


class DoubleParamVarRef
    : public SeExprVarRef
{
    //Used to call getValue only once per expression evaluation and not once per pixel
    //Using SeExpr lock is faster than calling the multi-thread suite to get a mutex
    SeExprInternal::Mutex _lock;
    bool _varSet;
    double _value;
    DoubleParam* _param;

public:

    DoubleParamVarRef(DoubleParam* param)
        : SeExprVarRef()
        , _lock()
        , _varSet(false)
        , _value(0)
        , _param(param)
    {
    }

    virtual ~DoubleParamVarRef() {}

    //! returns true for a vector type, false for a scalar type
    virtual bool isVec() { return false; }

    //! returns this variable's value by setting result, node refers to
    //! where in the parse tree the evaluation is occurring
    virtual void eval(const SeExprVarNode* /*node*/,
                      SeVec3d& result)
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

class Double2DParamVarRef
    : public SeExprVarRef
{
    //Used to call getValue only once per expression evaluation and not once per pixel
    //Using SeExpr lock is faster than calling the multi-thread suite to get a mutex
    SeExprInternal::Mutex _lock;
    bool _varSet;
    double _value[2];
    Double2DParam* _param;

public:

    Double2DParamVarRef(Double2DParam* param)
        : SeExprVarRef()
        , _lock()
        , _varSet(false)
        , _value()
        , _param(param)
    {
    }

    virtual ~Double2DParamVarRef() {}

    //! returns true for a vector type, false for a scalar type
    virtual bool isVec() { return true; }

    //! returns this variable's value by setting result, node refers to
    //! where in the parse tree the evaluation is occurring
    virtual void eval(const SeExprVarNode* /*node*/,
                      SeVec3d& result)
    {
        SeExprInternal::AutoLock<SeExprInternal::Mutex> locker(_lock);
        if (!_varSet) {
            _param->getValue(_value[0], _value[1]);
            _varSet = true;
        } else {
            result[0] = _value[0];
            result[1] = _value[1];
        }
    }
};

class ColorParamVarRef
    : public SeExprVarRef
{
    //Used to call getValue only once per expression evaluation and not once per pixel
    //Using SeExpr lock is faster than calling the multi-thread suite to get a mutex
    SeExprInternal::Mutex _lock;
    bool _varSet;
    double _value[3];
    RGBParam* _param;

public:

    ColorParamVarRef(RGBParam* param)
        : SeExprVarRef()
        , _lock()
        , _varSet(false)
        , _value()
        , _param(param)
    {
    }

    virtual ~ColorParamVarRef() {}

    //! returns true for a vector type, false for a scalar type
    virtual bool isVec() { return true; }

    //! returns this variable's value by setting result, node refers to
    //! where in the parse tree the evaluation is occurring
    virtual void eval(const SeExprVarNode* /*node*/,
                      SeVec3d& result)
    {
        SeExprInternal::AutoLock<SeExprInternal::Mutex> locker(_lock);
        if (!_varSet) {
            _param->getValue(_value[0], _value[1], _value[2]);
            _varSet = true;
        } else {
            result[0] = _value[0];
            result[1] = _value[1];
            result[2] = _value[2];
        }
    }
};

class SimpleScalar
    : public SeExprVarRef
{
public:
    double _value;

    SimpleScalar() : SeExprVarRef(), _value(0) {}

    virtual ~SimpleScalar() {}

    virtual bool isVec() { return false; }

    virtual void eval(const SeExprVarNode* /*node*/,
                      SeVec3d& result)
    {
        result[0] = _value;
    }
};

class SimpleVec
    : public SeExprVarRef
{
public:
    double _value[3];

    SimpleVec() : SeExprVarRef(), _value() { _value[0] = _value[1] = _value[2] = 0.; }

    virtual ~SimpleVec() {}

    virtual bool isVec() { return true; }

    virtual void eval(const SeExprVarNode* /*node*/,
                      SeVec3d& result)
    {
        result[0] = _value[0];
        result[1] = _value[1];
        result[2] = _value[2];
    }
};

class StubSeExpression;

class StubPixelFuncX
    : public SeExprFuncX
{
    StubSeExpression* _expr;

public:

    StubPixelFuncX(StubSeExpression* expr)
        : SeExprFuncX(true) // Thread Safe
        , _expr(expr)
    {}

    virtual ~StubPixelFuncX() {}

private:


    virtual bool prep(SeExprFuncNode* node, bool /*wantVec*/);
    virtual void eval(const SeExprFuncNode* node, SeVec3d& result) const;
};

typedef map<int, vector<OfxTime> > FramesNeeded;

/**
 * @brief Used to determine what are the frames needed and RoIs of the expression
 **/
class StubSeExpression
    : public SeExpression
{
    mutable SimpleScalar _nanScalar, _zeroScalar;
    mutable StubPixelFuncX _pixel;
    mutable SeExprFunc _pixelFunction;
    mutable SimpleScalar _currentTime;
    mutable FramesNeeded _images;

public:

    StubSeExpression(const string& expr,
                     bool wantVec,
                     OfxTime time);

    virtual ~StubSeExpression();

    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const string& name) const OVERRIDE FINAL;

    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const string& name) const OVERRIDE FINAL;

    void onPixelCalled(int inputIndex,
                       OfxTime time)
    {
        //Register image needed
        FramesNeeded::iterator foundInput = _images.find(inputIndex);
        if ( foundInput == _images.end() ) {
            vector<OfxTime> times;
            times.push_back(time);
            _images.insert( make_pair(inputIndex, times) );
        } else {
            if ( std::find(foundInput->second.begin(), foundInput->second.end(), time) == foundInput->second.end() ) {
                foundInput->second.push_back(time);
            }
        }
    }

    const FramesNeeded& getFramesNeeded() const
    {
        return _images;
    }
};

class OFXSeExpression
    : public SeExpression
{
    const bool _simple;
    mutable PixelFuncX<false> _cpixel;
    mutable SeExprFunc _cpixelFunction;
    mutable PixelFuncX<true> _apixel;
    mutable SeExprFunc _apixelFunction;
    OfxRectI _dstPixelRod;
    typedef map<string, SeExprVarRef*> VariablesMap;
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
    SimpleScalar _inputR[kSourceClipCount];
    SimpleScalar _inputG[kSourceClipCount];
    SimpleScalar _inputB[kSourceClipCount];
    SimpleVec _inputColors[kSourceClipCount];
    SimpleScalar _inputAlphas[kSourceClipCount];
    DoubleParamVarRef* _doubleRef[kParamsCount];
    Double2DParamVarRef* _double2DRef[kParamsCount];
    ColorParamVarRef* _colorRef[kParamsCount];

public:


    OFXSeExpression(SeExprProcessorBase* processor,
                    const string& expr,
                    bool wantVec,
                    bool simple,
                    OfxTime time,
                    const OfxPointD& renderScale,
                    double par,
                    const OfxRectI& outputRod);

    virtual ~OFXSeExpression();

    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const string& name) const OVERRIDE FINAL;

    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const string& name) const OVERRIDE FINAL;

    /** NOT MT-SAFE, this object is to be used PER-THREAD*/
    void setXY(int x,
               int y)
    {
        _xCoord._value = x;
        _yCoord._value = y;
        assert(_dstPixelRod.x2 - _dstPixelRod.x1);
        assert(_dstPixelRod.y2 - _dstPixelRod.y1);
        _uCoord._value = (x + 0.5 - _dstPixelRod.x1) / (_dstPixelRod.x2 - _dstPixelRod.x1);
        _vCoord._value = (y + 0.5 - _dstPixelRod.y1) / (_dstPixelRod.y2 - _dstPixelRod.y1);
        _xCanCoord._value = (x + 0.5) * _par._value / _scalex._value;
        _yCanCoord._value = (y + 0.5) / _scaley._value;
    }

    void setRGBA(int inputIndex,
                 float r,
                 float g,
                 float b,
                 float a)
    {
        if (_simple) {
            _inputR[inputIndex]._value = r;
            _inputG[inputIndex]._value = g;
            _inputB[inputIndex]._value = b;
        }
        _inputColors[inputIndex]._value[0] = r;
        _inputColors[inputIndex]._value[1] = g;
        _inputColors[inputIndex]._value[2] = b;
        _inputAlphas[inputIndex]._value = a;
    }

    void setSize(int inputNumber,
                 int w,
                 int h)
    {
        if (inputNumber == -1) {
            _outputWidth._value = w;
            _outputHeight._value = h;
        } else {
            _inputWidths[inputNumber]._value = w;
            _inputHeights[inputNumber]._value = h;
        }
    }
};

OFXSeExpression::OFXSeExpression(SeExprProcessorBase* processor,
                                 const string& expr,
                                 bool wantVec,
                                 bool simple,
                                 OfxTime time,
                                 const OfxPointD& renderScale,
                                 double par,
                                 const OfxRectI& outputRod)
    : SeExpression(expr, wantVec)
    , _simple(simple)
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

    for (int i = 0; i < kSourceClipCount; ++i) {
        const string istr = unsignedToString(i + 1);
        _variables[kSeExprInputWidthVarName + istr] = &_inputWidths[i];
        _variables[kSeExprInputHeightVarName + istr] = &_inputHeights[i];
        if (_simple) {
            _variables[kSeExprRVarName + istr] = &_inputR[i];
            _variables[kSeExprGVarName + istr] = &_inputG[i];
            _variables[kSeExprBVarName + istr] = &_inputB[i];
            _variables[kSeExprAVarName + istr] = &_inputAlphas[i];
        }
        _variables[kSeExprColorVarName + istr] = &_inputColors[i];
        _variables[kSeExprAlphaVarName + istr] = &_inputAlphas[i];
        if (i == 0) {
            // default names for the first input
            _variables[kSeExprInputWidthVarName] = &_inputWidths[i];
            _variables[kSeExprInputHeightVarName] = &_inputHeights[i];
            if (_simple) {
                _variables[kSeExprRVarName] = &_inputR[i];
                _variables[kSeExprGVarName] = &_inputG[i];
                _variables[kSeExprBVarName] = &_inputB[i];
                _variables[kSeExprAVarName] = &_inputAlphas[i];
            }
            _variables[kSeExprColorVarName] = &_inputColors[i];
            _variables[kSeExprAlphaVarName] = &_inputAlphas[i];
        }
    }

    assert(processor);
    SeExprPlugin* plugin = processor->getPlugin();
    DoubleParam** doubleParams = plugin->getDoubleParams();
    Double2DParam** double2DParams = plugin->getDouble2DParams();
    RGBParam** colorParams = plugin->getRGBParams();

    for (int i = 0; i < kParamsCount; ++i) {
        _doubleRef[i] = new DoubleParamVarRef(doubleParams[i]);
        _double2DRef[i]  = new Double2DParamVarRef(double2DParams[i]);
        _colorRef[i]  = new ColorParamVarRef(colorParams[i]);
        const string istr = unsignedToString(i + 1);
        _variables[kParamDouble + istr] = _doubleRef[i];
        _variables[kParamDouble2D + istr] = _double2DRef[i];
        _variables[kParamColor + istr] = _colorRef[i];
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
OFXSeExpression::resolveVar(const string& varName) const
{
    VariablesMap::const_iterator found = _variables.find(varName);
    if ( found == _variables.end() ) {
        return 0;
    }

    return found->second;
}

SeExprFunc*
OFXSeExpression::resolveFunc(const string& funcName) const
{
    // check if it is builtin so we get proper behavior
    if ( SeExprFunc::lookup(funcName) ) {
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
StubPixelFuncX::prep(SeExprFuncNode* node,
                     bool /*wantVec*/)
{
    // check number of arguments
    int nargs = node->nargs();

    if ( (nargs < 4) || (5 < nargs) ) {
        node->addError("Wrong number of arguments, should be 4 or 5");

        return false;
    }

    for (int i = 0; i < nargs; ++i) {
        if ( node->child(i)->isVec() ) {
            node->addError("Wrong arguments, should be all scalars");

            return false;
        }
        if ( !node->child(i)->prep(false) ) {
            return false;
        }
    }

    SeVec3d v;
    node->child(0)->eval(v);
    int inputIndex = (int)SeExpr::round(v[0]) - 1;
    if ( (inputIndex < 0) || (inputIndex >= kSourceClipCount) ) {
        node->addError("Invalid input index");

        return false;
    }

    return true;
}

void
StubPixelFuncX::eval(const SeExprFuncNode* node,
                     SeVec3d& result) const
{
    SeVec3d v;

    node->child(0)->eval(v);
    int inputIndex = (int)SeExpr::round(v[0]) - 1;
    node->child(1)->eval(v);
    OfxTime frame = SeExpr::round(v[0]);


    _expr->onPixelCalled(inputIndex, frame);
    result[0] = result[1] = result[2] = std::numeric_limits<double>::quiet_NaN();
}

StubSeExpression::StubSeExpression(const string& expr,
                                   bool wantVec,
                                   OfxTime time)
    : SeExpression(expr, wantVec)
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
StubSeExpression::resolveVar(const string& varName) const
{
    if (varName == kSeExprCurrentTimeVarName) {
        return &_currentTime;
    }

    return &_nanScalar;
}

/** override resolveFunc to add external functions */
SeExprFunc*
StubSeExpression::resolveFunc(const string& funcName) const
{
    // check if it is builtin so we get proper behavior
    if ( SeExprFunc::lookup(funcName) ) {
        return 0;
    }
    if ( (funcName == kSeExprCPixelFuncName) || (funcName == kSeExprAPixelFuncName) ) {
        return &_pixelFunction;
    }

    return 0;
}

SeExprProcessorBase::SeExprProcessorBase(SeExprPlugin* instance)
    : _renderTime(0)
    , _renderView(0)
    , _plugin(instance)
    , _rExpr(0)
    , _gExpr(0)
    , _bExpr(0)
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
    delete _rExpr;
    delete _gExpr;
    delete _bExpr;
    delete _rgbExpr;
    delete _alphaExpr;
    for (FetchedImagesMap::iterator it = _images.begin(); it != _images.end(); ++it) {
        for (FetchedImagesForClipMap::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
            delete it2->second;
        }
    }
}

void
SeExprProcessorBase::setValues(OfxTime time,
                               int view,
                               double mix,
                               const string& rgbExpr,
                               const string& alphaExpr,
                               string* layers,
                               const OfxRectI& dstPixelRod,
                               OfxPointI* inputSizes,
                               const OfxPointI& outputSize,
                               const OfxPointD& renderScale,
                               double par)
{
    setExprs(time, rgbExpr, alphaExpr, dstPixelRod, renderScale, par);
    setValuesOther(time, view, mix, layers, inputSizes, outputSize);
}

void
SeExprProcessorBase::setValuesSimple(OfxTime time,
                                     int view,
                                     double mix,
                                     const string& rExpr,
                                     const string& gExpr,
                                     const string& bExpr,
                                     const string& aExpr,
                                     string* layers,
                                     const OfxRectI& dstPixelRod,
                                     OfxPointI* inputSizes,
                                     const OfxPointI& outputSize,
                                     const OfxPointD& renderScale,
                                     double par)
{
    setExprsSimple(time, rExpr, gExpr, bExpr, aExpr, dstPixelRod, renderScale, par);
    setValuesOther(time, view, mix, layers, inputSizes, outputSize);
}

void
SeExprProcessorBase::setExprs(OfxTime time,
                              const string& rgbExpr,
                              const string& alphaExpr,
                              const OfxRectI& dstPixelRod,
                              const OfxPointD& renderScale,
                              double par)
{
    if ( !isSpaces(rgbExpr) ) {
        _rgbExpr = new OFXSeExpression(this, rgbExpr, /*wantVec=*/ true, /*simple=*/ false, time, renderScale, par, dstPixelRod);
    }
    if ( !isSpaces(alphaExpr) ) {
        _alphaExpr = new OFXSeExpression(this, alphaExpr, /*wantVec=*/ false, /*simple=*/ false, time, renderScale, par, dstPixelRod);
    }
}

void
SeExprProcessorBase::setExprsSimple(OfxTime time,
                                    const string& rExpr,
                                    const string& gExpr,
                                    const string& bExpr,
                                    const string& aExpr,
                                    const OfxRectI& dstPixelRod,
                                    const OfxPointD& renderScale,
                                    double par)
{
    if ( !isSpaces(rExpr) ) {
        _rExpr = new OFXSeExpression(this, rExpr, /*wantVec=*/ false, /*simple=*/ true, time, renderScale, par, dstPixelRod);
    }
    if ( !isSpaces(gExpr) ) {
        _gExpr = new OFXSeExpression(this, gExpr, /*wantVec=*/ false, /*simple=*/ true, time, renderScale, par, dstPixelRod);
    }
    if ( !isSpaces(bExpr) ) {
        _bExpr = new OFXSeExpression(this, bExpr, /*wantVec=*/ false, /*simple=*/ true, time, renderScale, par, dstPixelRod);
    }
    if ( !isSpaces(aExpr) ) {
        _alphaExpr = new OFXSeExpression(this, aExpr, /*wantVec=*/ false, /*simple=*/ true, time, renderScale, par, dstPixelRod);
    }
}

void
SeExprProcessorBase::setValuesOther(OfxTime time,
                                    int view,
                                    double mix,
                                    string* layers,
                                    OfxPointI* inputSizes,
                                    const OfxPointI& outputSize)
{
    _renderTime = time;
    _renderView = view;
    if (gHostIsMultiPlanar) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            _layersToFetch[i] = layers[i];
        }
    }
    for (int i = 0; i < kSourceClipCount; ++i) {
        if (_rExpr) {
            _rExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
        if (_gExpr) {
            _gExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
        if (_bExpr) {
            _bExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
        if (_rgbExpr) {
            _rgbExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
        if (_alphaExpr) {
            _alphaExpr->setSize(i, inputSizes[i].x, inputSizes[i].y);
        }
    }
    if (_rExpr) {
        _rExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    if (_gExpr) {
        _gExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    if (_bExpr) {
        _bExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    if (_rgbExpr) {
        _rgbExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    if (_alphaExpr) {
        _alphaExpr->setSize(-1, outputSize.x, outputSize.y);
    }
    //assert(_alphaExpr || _rgbExpr); // both may be empty!
    _mix = mix;
}

bool
SeExprProcessorBase::isExprOk(string* error)
{
    if ( _rExpr && !_rExpr->isValid() ) {
        *error = _rExpr->parseError();

        return false;
    }
    if ( _gExpr && !_gExpr->isValid() ) {
        *error = _gExpr->parseError();

        return false;
    }
    if ( _bExpr && !_bExpr->isValid() ) {
        *error = _bExpr->parseError();

        return false;
    }
    if ( _rgbExpr && !_rgbExpr->isValid() ) {
        *error = _rgbExpr->parseError();

        return false;
    }
    if ( _alphaExpr && !_alphaExpr->isValid() ) {
        *error = _alphaExpr->parseError();

        return false;
    }

    //Run the expression once to initialize all the images fields before multi-threading
    if (_rExpr) {
        (void)_rExpr->evaluate();
    }
    if (_gExpr) {
        (void)_gExpr->evaluate();
    }
    if (_bExpr) {
        (void)_bExpr->evaluate();
    }
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
} // SeExprProcessorBase::isExprOk

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue>
class SeExprProcessor
    : public SeExprProcessorBase
{
public:
    // ctor
    SeExprProcessor(SeExprPlugin* instance)
        : SeExprProcessorBase(instance)
    {
        assert(maxValue);
    }

private:
    // and do some processing
    virtual void process(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert( (nComponents == 4 /*&& _rgbExpr && _alphaExpr*/) ||
                (nComponents == 3 /*&& _rgbExpr && !_alphaExpr*/) ||
                (nComponents == 1 /*&& !_rgbExpr && _alphaExpr*/) );


        float tmpPix[4];
        PIX srcPixels[kSourceClipCount][4];

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _plugin->abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                for (int i = kSourceClipCount - 1; i  >= 0; --i) {
                    const PIX* src_pixels  = _srcCurTime[i] ? (const PIX*) _srcCurTime[i]->getPixelAddress(x, y) : 0;
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
                    float a = srcPixels[i][_nSrcComponents[i] == 4 ? 3 : 0] / (float)maxValue;
                    if (_rExpr) {
                        _rExpr->setRGBA(i, r, g, b, a);
                    }
                    if (_gExpr) {
                        _gExpr->setRGBA(i, r, g, b, a);
                    }
                    if (_bExpr) {
                        _bExpr->setRGBA(i, r, g, b, a);
                    }
                    if (_rgbExpr) {
                        _rgbExpr->setRGBA(i, r, g, b, a);
                    }
                    if (_alphaExpr) {
                        _alphaExpr->setRGBA(i, r, g, b, a);
                    }
                }

                // initialize with values from first input (some expressions may be empty)
                if (nComponents == 1) {
                    tmpPix[0] = srcPixels[0][3];
                }
                if (nComponents >= 3) {
                    tmpPix[0] = srcPixels[0][0];
                    tmpPix[1] = srcPixels[0][1];
                    tmpPix[2] = srcPixels[0][2];
                }
                if (nComponents == 4) {
                    tmpPix[3] = srcPixels[0][3];
                }

                // execute the valid expressions
                if (_rExpr) {
                    _rExpr->setXY(x, y);
                    SeVec3d result = _rExpr->evaluate();
                    if (nComponents >= 3) {
                        tmpPix[0] = result[0] * maxValue;
                    }
                }
                if (_gExpr) {
                    _gExpr->setXY(x, y);
                    SeVec3d result = _gExpr->evaluate();
                    if (nComponents >= 3) {
                        tmpPix[1] = result[0] * maxValue;
                    }
                }
                if (_bExpr) {
                    _bExpr->setXY(x, y);
                    SeVec3d result = _bExpr->evaluate();
                    if (nComponents >= 3) {
                        tmpPix[2] = result[0] * maxValue;
                    }
                }
                if (_rgbExpr) {
                    _rgbExpr->setXY(x, y);
                    SeVec3d result = _rgbExpr->evaluate();
                    if (nComponents >= 3) {
                        tmpPix[0] = result[0] * maxValue;
                        tmpPix[1] = result[1] * maxValue;
                        tmpPix[2] = result[2] * maxValue;
                    }
                }
                if (_alphaExpr) {
                    _alphaExpr->setXY(x, y);
                    SeVec3d result = _alphaExpr->evaluate();
                    if (nComponents == 4) {
                        tmpPix[3] = result[0] * maxValue;
                    } else if (nComponents == 1) {
                        tmpPix[0] = result[0] * maxValue;
                    }
                }

                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixels[0], _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};

SeExprPlugin::SeExprPlugin(OfxImageEffectHandle handle,
                           bool simple)
    : ImageEffect(handle)
    , _simple(simple)
{
    if (getContext() != eContextGenerator) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            if ( (i == 0) && (getContext() == eContextFilter) ) {
                _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
            } else {
                const string istr = unsignedToString(i + 1);
                _srcClip[i] = fetchClip(istr);
            }
        }
    }
    _clipLayerOptions.resize(kSourceClipCount);

    _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
    assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);

    _doubleParamCount = fetchIntParam(kParamDoubleParamNumber);
    assert(_doubleParamCount);
    _double2DParamCount = fetchIntParam(kParamDouble2DParamNumber);
    assert(_double2DParamCount);
    _colorParamCount = fetchIntParam(kParamColorNumber);
    assert(_colorParamCount);

    for (int i = 0; i < kParamsCount; ++i) {
        const string istr = unsignedToString(i + 1);
        if (gHostIsMultiPlanar) {
            _clipLayerToFetch[i] = fetchChoiceParam(kParamLayerInput + istr);
            _clipLayerToFetchString[i] = fetchStringParam(kParamLayerInputChoice + istr);
        } else {
            _clipLayerToFetch[i] = 0;
            _clipLayerToFetchString[i] = 0;
        }

        _doubleParams[i] = fetchDoubleParam(kParamDouble + istr);
        _double2DParams[i] = fetchDouble2DParam(kParamDouble2D + istr);
        _colorParams[i] = fetchRGBParam(kParamColor + istr);
    }

    _frameRange = fetchInt2DParam(kParamFrameRange);
    _frameRangeAbsolute = fetchBooleanParam(kParamFrameRangeAbsolute);
    assert(_frameRange && _frameRangeAbsolute);

    if (_simple) {
        _rExpr = fetchStringParam(kParamRExpr);
        _gExpr = fetchStringParam(kParamGExpr);
        _bExpr = fetchStringParam(kParamBExpr);
        _aExpr = fetchStringParam(kParamAExpr);
        assert(_rExpr && _gExpr && _bExpr && _aExpr);
        _rgbScript = _alphaScript = NULL;
    } else {
        _rExpr = _gExpr = _bExpr = _aExpr = NULL;
        _rgbScript = fetchStringParam(kParamScript);
        assert(_rgbScript);
        _alphaScript = fetchStringParam(kParamAlphaScript);
        assert(_alphaScript);
    }
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
    InstanceChangedArgs args = {
        eChangeUserEdit, 0, {1, 1}
    };
    changedParam(args, kParamDoubleParamNumber);
    changedParam(args, kParamDouble2DParamNumber);
    changedParam(args, kParamColorNumber);
    changedParam(args, kParamValidate);
    changedParam(args, kParamRegionOfDefinition);
    changedParam(args, kParamOutputComponents);
}

string
SeExprPlugin::getOfxComponentsForClip(int inputNumber) const
{
    assert(inputNumber >= 0 && inputNumber < kSourceClipCount);
    int opt_i;
    _clipLayerToFetch[inputNumber]->getValue(opt_i);
    string opt;
    _clipLayerToFetch[inputNumber]->getOption(opt_i, opt);

    if (opt == kSeExprColorPlaneName) {
        return _srcClip[inputNumber]->getPixelComponentsProperty();
    } else if ( (opt == kSeExprForwardMotionPlaneName) || (opt == kSeExprBackwardMotionPlaneName) ) {
        return kFnOfxImageComponentMotionVectors;
    } else if ( (opt == kSeExprDisparityLeftPlaneName) || (opt == kSeExprDisparityRightPlaneName) ) {
        return kFnOfxImageComponentStereoDisparity;
    } else {
        vector<string> components;
        _srcClip[inputNumber]->getComponentsPresent(&components);
        for (vector<string>::iterator it = components.begin(); it != components.end(); ++it) {
            vector<string> layerChannels = mapPixelComponentCustomToLayerChannels(*it);
            if ( layerChannels.empty() ) {
                continue;
            }
            // first element is layer name
            if (layerChannels[0] == opt) {
                return *it;
            }
        }
    }

    return string();
}

string
SeExprPlugin::getOfxPlaneForClip(int inputNumber) const
{
    assert(inputNumber >= 0 && inputNumber < kSourceClipCount);
    int opt_i;
    _clipLayerToFetch[inputNumber]->getValue(opt_i);
    string opt;
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
        vector<string> components;
        _srcClip[inputNumber]->getComponentsPresent(&components);
        for (vector<string>::iterator it = components.begin(); it != components.end(); ++it) {
            vector<string> layerChannels = mapPixelComponentCustomToLayerChannels(*it);
            if ( layerChannels.empty() ) {
                continue;
            }
            // first element is layer name
            if (layerChannels[0] == opt) {
                return *it;
            }
        }
    }

    return string();
}

void
SeExprPlugin::setupAndProcess(SeExprProcessorBase & processor,
                              const RenderArguments &args)
{
    const double time = args.time;

    std::auto_ptr<Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    string rExpr, gExpr, bExpr, aExpr;
    string rgbScript, alphaScript;
    if ( (dstComponents == ePixelComponentRGB) || (dstComponents == ePixelComponentRGBA) ) {
        if (_simple) {
            _rExpr->getValue(rExpr);
            _gExpr->getValue(gExpr);
            _bExpr->getValue(bExpr);
            unused(rgbScript);
        } else {
            unused(rExpr);
            unused(gExpr);
            unused(bExpr);
            _rgbScript->getValue(rgbScript);
        }
    }
    if ( (dstComponents == ePixelComponentRGBA) || (dstComponents == ePixelComponentAlpha) ) {
        if (_simple) {
            _aExpr->getValue(aExpr);
            unused(alphaScript);
        } else {
            unused(_aExpr);
            _alphaScript->getValue(alphaScript);
        }
    }


    string inputLayers[kSourceClipCount];
    if (gHostIsMultiPlanar) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            inputLayers[i] = getOfxPlaneForClip(i);
        }
    }

    double mix;
    _mix->getValue(mix);

    processor.setDstImg( dst.get() );

    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    // do we do masking
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    OfxPointI inputSizes[kSourceClipCount];
    for (int i = 0; i < kSourceClipCount; ++i) {
        if ( _srcClip[i]->isConnected() ) {
            OfxRectD rod = _srcClip[i]->getRegionOfDefinition(time);
            double par = _srcClip[i]->getPixelAspectRatio();
            OfxRectI pixelRod;
            Coords::toPixelEnclosing(rod, args.renderScale, par, &pixelRod);
            inputSizes[i].x = pixelRod.x2 - pixelRod.x1;
            inputSizes[i].y = pixelRod.y2 - pixelRod.y1;
        } else {
            inputSizes[i].x = inputSizes[i].y = 0.;
        }
    }

    RegionOfDefinitionArguments rodArgs;
    rodArgs.time = time;
    rodArgs.view = args.viewsToRender;
    rodArgs.renderScale = args.renderScale;
    OfxRectD outputRod;
    getRegionOfDefinition(rodArgs, outputRod);
    OfxRectI outputPixelRod;
    double par = dst->getPixelAspectRatio();

    Coords::toPixelEnclosing(outputRod, args.renderScale, par, &outputPixelRod);
    OfxPointI outputSize;
    outputSize.x = outputPixelRod.x2 - outputPixelRod.x1;
    outputSize.y = outputPixelRod.y2 - outputPixelRod.y1;

    if (_simple) {
        processor.setValuesSimple(time, args.renderView, mix, rExpr, gExpr, bExpr, aExpr, inputLayers, outputPixelRod, inputSizes, outputSize, args.renderScale, par);
    } else {
        processor.setValues(time, args.renderView, mix, rgbScript, alphaScript, inputLayers, outputPixelRod, inputSizes, outputSize, args.renderScale, par);
    }

    string error;
    if ( !processor.isExprOk(&error) ) {
        setPersistentMessage(Message::eMessageError, "", error);
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }


    processor.process(args.renderWindow);
} // SeExprPlugin::setupAndProcess

void
SeExprPlugin::render(const RenderArguments &args)
{
    clearPersistentMessage();
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(Message::eMessageError, "", "Validate the script before rendering/running.");
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
    }

    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentAlpha);

    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    if (dstComponents != outputComponents) {
        setPersistentMessage(Message::eMessageError, "", "SeExpr: OFX Host did not take into account output components");
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            SeExprProcessor<unsigned char, 4, 255> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            SeExprProcessor<unsigned short, 4, 65535> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            SeExprProcessor<float, 4, 1> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);

            return;
        }
    } else if (dstComponents == ePixelComponentRGB) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            SeExprProcessor<unsigned char, 3, 255> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            SeExprProcessor<unsigned short, 3, 65535> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            SeExprProcessor<float, 3, 1> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);

            return;
        }
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            SeExprProcessor<unsigned char, 1, 255> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            SeExprProcessor<unsigned short, 1, 65535> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            SeExprProcessor<float, 1, 1> fred(this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);

            return;
        }
    }
} // SeExprPlugin::render

void
SeExprPlugin::changedParam(const InstanceChangedArgs &args,
                           const string &paramName)
{
    const double time = args.time;

    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if ( (paramName == kParamDoubleParamNumber) && (args.reason == eChangeUserEdit) ) {
        int numVisible;
        _doubleParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >= 0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _doubleParams[i]->setIsSecret(!visible);
        }
    } else if ( (paramName == kParamDouble2DParamNumber) && (args.reason == eChangeUserEdit) ) {
        int numVisible;
        _double2DParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >= 0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _double2DParams[i]->setIsSecret(!visible);
        }
    } else if ( (paramName == kParamColorNumber) && (args.reason == eChangeUserEdit) ) {
        int numVisible;
        _colorParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >= 0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _colorParams[i]->setIsSecret(!visible);
        }
    } else if ( (paramName == kParamValidate) && (args.reason == eChangeUserEdit) ) {
        if (!gHostIsNatron) {
            bool validated;
            _validate->getValue(validated);

            _doubleParamCount->setEnabled(!validated);
            _double2DParamCount->setEnabled(!validated);
            _colorParamCount->setEnabled(!validated);
            _doubleParamCount->setEvaluateOnChange(validated);
            _double2DParamCount->setEvaluateOnChange(validated);
            _colorParamCount->setEvaluateOnChange(validated);
            if (_simple) {
                _rExpr->setEnabled(!validated);
                _rExpr->setEvaluateOnChange(validated);
                _gExpr->setEnabled(!validated);
                _gExpr->setEvaluateOnChange(validated);
                _bExpr->setEnabled(!validated);
                _bExpr->setEvaluateOnChange(validated);
                _aExpr->setEnabled(!validated);
                _aExpr->setEvaluateOnChange(validated);
            } else {
                _rgbScript->setEnabled(!validated);
                _rgbScript->setEvaluateOnChange(validated);
                _alphaScript->setEnabled(!validated);
                _alphaScript->setEvaluateOnChange(validated);
            }
            clearPersistentMessage();
        }
    } else if ( (paramName == kParamRegionOfDefinition) && (args.reason == eChangeUserEdit) ) {
        int boundingBox_i;
        _boundingBox->getValue(boundingBox_i);
        RegionOfDefinitionEnum boundingBox = (RegionOfDefinitionEnum)boundingBox_i;
        bool hasFormat = (boundingBox == eRegionOfDefinitionOptionFormat);
        bool hasSize = (boundingBox == eRegionOfDefinitionOptionSize);

        _format->setIsSecretAndDisabled(!hasFormat);
        _size->setIsSecretAndDisabled(!hasSize);
        _btmLeft->setIsSecretAndDisabled(!hasSize);
        _interactive->setIsSecretAndDisabled(!hasSize);
    } else if ( (paramName == kParamOutputComponents) && (args.reason == eChangeUserEdit) ) {
        PixelComponentEnum outputComponents = getOutputComponents();
        const bool hasRGB = (outputComponents == ePixelComponentRGB || outputComponents == ePixelComponentRGBA); // RGB || RGBA
        if (_simple) {
            _rExpr->setIsSecretAndDisabled(!hasRGB);
            _gExpr->setIsSecretAndDisabled(!hasRGB);
            _bExpr->setIsSecretAndDisabled(!hasRGB);
        } else {
            _rgbScript->setIsSecretAndDisabled(!hasRGB);
        }
        const bool hasAlpha = (outputComponents == ePixelComponentRGBA || outputComponents == ePixelComponentAlpha); // RGBA || alpha
        if (_simple) {
            _aExpr->setIsSecretAndDisabled(!hasAlpha);
        } else {
            _alphaScript->setIsSecretAndDisabled(!hasAlpha);
        }
    } else if ( (paramName == kParamShowExprs) && (args.reason == eChangeUserEdit) ) {
        string rExpr, gExpr, bExpr, aExpr;
        if (_rExpr) {
            _rExpr->getValueAtTime(time, rExpr);
        }
        if (_gExpr) {
            _gExpr->getValueAtTime(time, gExpr);
        }
        if (_bExpr) {
            _bExpr->getValueAtTime(time, bExpr);
        }
        if (_aExpr) {
            _aExpr->getValueAtTime(time, aExpr);
        }
        sendMessage(Message::eMessageMessage, "", "R Expr:\n" + rExpr + "\n\nG Expr:\n" + gExpr + "\n\nB Expr:\n" + bExpr + "\n\nA Expr:\n" + aExpr);
    } else if ( (paramName == kParamShowScript) && (args.reason == eChangeUserEdit) ) {
        string script;
        if (_rgbScript) {
            _rgbScript->getValueAtTime(time, script);
        }
        sendMessage(Message::eMessageMessage, "", "RGB Script:\n" + script);
    } else if ( (paramName == kParamShowAlphaScript) && (args.reason == eChangeUserEdit) ) {
        string script;
        if (_alphaScript) {
            _alphaScript->getValueAtTime(time, script);
        }
        sendMessage(Message::eMessageMessage, "", "Alpha Script:\n" + script);
    } else {
        for (int i = 0; i < kSourceClipCount; ++i) {
            const string istr = unsignedToString(i + 1);
            if ( ( paramName == (kParamLayerInput + istr) ) && (args.reason == eChangeUserEdit) ) {
                int cur_i;
                _clipLayerToFetch[i]->getValue(cur_i);
                string opt;
                _clipLayerToFetch[i]->getOption(cur_i, opt);
                _clipLayerToFetchString[i]->setValue(opt);
                break;
            }
        }
    }
} // SeExprPlugin::changedParam

bool
SeExprPlugin::isIdentity(const IsIdentityArguments &args,
                         Clip * &identityClip,
                         double & /*identityTime*/)
{
    const double time = args.time;

    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();


    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip[0];

                return true;
            }
        }
    }

    // check if all expressions are empty
    PixelComponentEnum outputComponents = getOutputComponents();
    string script;
    if ( (outputComponents == ePixelComponentRGB) || (outputComponents == ePixelComponentRGBA) ) { // RGB || RGBA
        if (_simple) {
            assert(_rExpr && _gExpr && _bExpr);
            _rExpr->getValueAtTime(time, script);
            if ( !isSpaces(script) ) {
                return false;
            }
            _gExpr->getValueAtTime(time, script);
            if ( !isSpaces(script) ) {
                return false;
            }
            _bExpr->getValueAtTime(time, script);
            if ( !isSpaces(script) ) {
                return false;
            }
        } else {
            assert(_rgbScript);
            _rgbScript->getValueAtTime(time, script);
            if ( !isSpaces(script) ) {
                return false;
            }
        }
    }
    if ( (outputComponents == ePixelComponentRGBA) || (outputComponents == ePixelComponentAlpha) ) { // RGBA || alpha
        if (_simple) {
            assert(_aExpr);
            _aExpr->getValueAtTime(time, script);
        } else {
            assert(_alphaScript);
            _alphaScript->getValueAtTime(time, script);
        }
        if ( !isSpaces(script) ) {
            return false;
        }
    }

    identityClip = _srcClip[0];

    return true;
} // SeExprPlugin::isIdentity

void
SeExprPlugin::changedClip(const InstanceChangedArgs &args,
                          const string &clipName)
{
    if (!gHostIsMultiPlanar) {
        return;
    }
    if (args.reason == eChangeUserEdit) {
        string strName;
        for (int i = 0; i < kSourceClipCount; ++i) {
            const string istr = unsignedToString(i + 1);
            if (istr == clipName) {
                assert(_srcClip[i]);
                _clipLayerToFetch[i]->setIsSecretAndDisabled( !_srcClip[i]->isConnected() );
            }
        }
    }
}

namespace {
static bool
hasListChanged(const vector<string>& oldList,
               const vector<string>& newList)
{
    if ( oldList.size() != newList.size() ) {
        return true;
    }

    vector<string>::const_iterator itNew = newList.begin();
    for (vector<string>::const_iterator it = oldList.begin(); it != oldList.end(); ++it, ++itNew) {
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
        vector<string> components;
        _srcClip[i]->getComponentsPresent(&components);
        if ( !hasListChanged(_clipLayerOptions[i], components) ) {
            continue;
        }
        _clipLayerToFetch[i]->resetOptions();

        _clipLayerOptions[i] = components;

        vector<string> options;
        options.push_back(kSeExprColorPlaneName);

        for (vector<string> ::iterator it = components.begin(); it != components.end(); ++it) {
            const string& comp = *it;
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
                vector<string> layerChannels = mapPixelComponentCustomToLayerChannels(*it);
                if ( layerChannels.empty() ) {
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
        string valueStr;
        _clipLayerToFetchString[i]->getValue(valueStr);
        if ( valueStr.empty() ) {
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
} // SeExprPlugin::buildChannelMenus

void
SeExprPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
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
        getFormatResolution( (EParamFormat)index, &w, &h, &par );
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

    PixelComponentEnum outputComponents = getOutputComponents();
    if (outputComponents == ePixelComponentRGB) {
        clipPreferences.setOutputPremultiplication(eImageOpaque);
    }
    clipPreferences.setClipComponents(*_dstClip, outputComponents);
} // SeExprPlugin::getClipPreferences

void
SeExprPlugin::getClipComponents(const ClipComponentsArguments& args,
                                ClipComponentsSetter& clipComponents)
{
    const double time = args.time;

    for (int i = 0; i < kSourceClipCount; ++i) {
        if ( !_srcClip[i]->isConnected() ) {
            continue;
        }

        string ofxComp = getOfxComponentsForClip(i);
        if ( !ofxComp.empty() ) {
            clipComponents.addClipComponents(*_srcClip[i], ofxComp);
        }
    }

    PixelComponentEnum outputComps = _dstClip->getPixelComponents();
    clipComponents.addClipComponents(*_dstClip, outputComps);
    clipComponents.setPassThroughClip(_srcClip[0], time, args.view);
}

void
SeExprPlugin::getFramesNeeded(const FramesNeededArguments &args,
                              FramesNeededSetter &framesNeededSetter)
{
    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(Message::eMessageError, "", "Validate the script before rendering/running.");
            throwSuiteStatusException(kOfxStatFailed);

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
    PixelComponentEnum outputComponents = getOutputComponents();
    if ( (outputComponents == ePixelComponentRGB) || (outputComponents == ePixelComponentRGBA) ) {// RGB || RGBA
        for (int e = 0; e < (_simple ? 3 : 1); ++e) {
            StringParam* param = !_simple ? _rgbScript : ( e == 0 ? _rExpr : (e == 1 ? _gExpr : _bExpr) );
            string script;
            if (param) {
                param->getValue(script);
            }

            if ( isSpaces(script) ) {
                framesNeeded[0].push_back(time);
            } else {
                StubSeExpression expr(script, /*wantVec=*/ !_simple, time);
                if ( !expr.isValid() ) {
                    setPersistentMessage( Message::eMessageError, "", expr.parseError() );
                    throwSuiteStatusException(kOfxStatFailed);

                    return;
                }

                (void)expr.evaluate();
                const FramesNeeded& rgbNeeded = expr.getFramesNeeded();
                for (FramesNeeded::const_iterator it = rgbNeeded.begin(); it != rgbNeeded.end(); ++it) {
                    vector<OfxTime>& frames = framesNeeded[it->first];
                    for (std::size_t j = 0; j < it->second.size(); ++j) {
                        bool found = false;
                        for (std::size_t i = 0; i < frames.size(); ++i) {
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
        }
    }
    if ( (outputComponents == ePixelComponentRGBA) || (outputComponents == ePixelComponentAlpha) ) { // RGBA || alpha
        StringParam* param = !_simple ? _alphaScript : _aExpr;
        string script;
        if (param) {
            param->getValue(script);
        }

        if ( isSpaces(script) ) {
            framesNeeded[0].push_back(time);
        } else {
            StubSeExpression expr(script, false, time);
            if ( !expr.isValid() ) {
                setPersistentMessage( Message::eMessageError, "", expr.parseError() );
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }

            (void)expr.evaluate();
            const FramesNeeded& alphaNeeded = expr.getFramesNeeded();
            for (FramesNeeded::const_iterator it = alphaNeeded.begin(); it != alphaNeeded.end(); ++it) {
                vector<OfxTime>& frames = framesNeeded[it->first];
                for (std::size_t j = 0; j < it->second.size(); ++j) {
                    bool found = false;
                    for (std::size_t i = 0; i < frames.size(); ++i) {
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
        Clip* clip = getClip(it->first);
        assert(clip);

        bool hasFetchedCurrentTime = false;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            assert (it->second[i] == it->second[i]);
            OfxRangeD range;
            if (it->second[i] == time) {
                hasFetchedCurrentTime = true;
            }
            range.min = range.max = it->second[i];
            framesNeededSetter.setFramesNeeded(*clip, range);
        }
        if (!hasFetchedCurrentTime) {
            OfxRangeD range;
            range.min = range.max = time;
            framesNeededSetter.setFramesNeeded(*clip, range);
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
            Clip* clip = getClip(i);
            assert(clip);
            framesNeededSetter.setFramesNeeded(*clip, range);
        }
    }
} // SeExprPlugin::getFramesNeeded

// override the roi call
void
SeExprPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                   RegionOfInterestSetter &rois)
{
    const double time = args.time;

    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(Message::eMessageError, "", "Validate the script before rendering/running.");
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
    }

    if (!kSupportsTiles) {
        // The effect requires full images to render any region
        for (int i = 0; i < kSourceClipCount; ++i) {
            OfxRectD srcRoI;

            if ( _srcClip[i] && _srcClip[i]->isConnected() ) {
                srcRoI = _srcClip[i]->getRegionOfDefinition(time);
                rois.setRegionOfInterest(*_srcClip[i], srcRoI);
            }
        }
    } else {
        //Notify that we will need the RoI for all connected input clips at the current time
        for (int i = 0; i < kSourceClipCount; ++i) {
            Clip* clip = getClip(i);
            assert(clip);
            if ( clip->isConnected() ) {
                rois.setRegionOfInterest(*clip, args.regionOfInterest);
            }
        }

        //To determine the ROIs of the expression, we just execute the expression at the 4 corners of the render window
        //and record what are the calls made to getPixel in order to figure out the Roi.

        std::set<Clip*> processedClips;

        PixelComponentEnum outputComponents = getOutputComponents();

        for (int e = 0; e < 6; ++e) {
            string script;
            bool wantVec = false;
            switch (e) {
            case 0:     // rExpr
                if ( _rExpr && _simple &&
                     ( ( outputComponents == ePixelComponentRGB) || ( outputComponents == ePixelComponentRGBA) ) ) {
                    _rExpr->getValue(script);
                }
                break;

            case 1:     // gExpr
                if ( _gExpr && _simple &&
                     ( ( outputComponents == ePixelComponentRGB) || ( outputComponents == ePixelComponentRGBA) ) ) {
                    _gExpr->getValue(script);
                }
                break;

            case 2:     // bExpr
                if ( _bExpr && _simple &&
                     ( ( outputComponents == ePixelComponentRGB) || ( outputComponents == ePixelComponentRGBA) ) ) {
                    _bExpr->getValue(script);
                }
                break;

            case 3:     // aExpr
                if ( _aExpr && _simple &&
                     ( ( outputComponents == ePixelComponentRGBA) || ( outputComponents == ePixelComponentAlpha) ) ) {
                    _aExpr->getValue(script);
                }
                break;

            case 4:     // rgbScript
                if ( _rgbScript && !_simple &&
                     ( ( outputComponents == ePixelComponentRGB) || ( outputComponents == ePixelComponentRGBA) ) ) {
                    _rgbScript->getValue(script);
                    wantVec = true;
                }
                break;

            case 5:     // alphaScript
                if ( _alphaScript && !_simple &&
                     ( ( outputComponents == ePixelComponentRGBA) || ( outputComponents == ePixelComponentAlpha) ) ) {
                    _alphaScript->getValue(script);
                }
                break;
            }
            if ( isSpaces(script) ) {
                continue;
            }

            StubSeExpression expr(script, wantVec, time);
            if ( !expr.isValid() ) {
                setPersistentMessage( Message::eMessageError, "", expr.parseError() );
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            //Now evaluate the expression once and determine whether the user will call getPixel.
            //If he/she does, then we have no choice but to ask for the entire input image because we do not know
            //what the user may need (typically when applying UVMaps and stuff)

            (void)expr.evaluate();
            const FramesNeeded& framesNeeded = expr.getFramesNeeded();

            for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
                Clip* clip = getClip(it->first);
                assert(clip);
                pair<std::set<Clip*>::iterator, bool> ret = processedClips.insert(clip);
                if (ret.second) {
                    if ( clip->isConnected() ) {
                        rois.setRegionOfInterest( *clip, clip->getRegionOfDefinition(time) );
                    }
                }
            }
        }
    }
} // SeExprPlugin::getRegionsOfInterest

bool
SeExprPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    const double time = args.time;

    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

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
            if ( _srcClip[i]->isConnected() ) {
                OfxRectD srcRod = _srcClip[i]->getRegionOfDefinition(time);
                if (rodSet) {
                    Coords::rectBoundingBox(srcRod, rod, &rod);
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
            if ( _srcClip[i]->isConnected() ) {
                OfxRectD srcRod = _srcClip[i]->getRegionOfDefinition(time);
                if (rodSet) {
                    Coords::rectIntersection<OfxRectD>(srcRod, rod, &rod);
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
        getFormatResolution( (EParamFormat)format_i, &w, &h, &par );
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
        rodSet = true;
        break;
    }
    default: {
        int inputIndex = boundingBox_i - (int)eRegionOfDefinitionOptionCustom;
        assert(inputIndex >= 0 && inputIndex < kSourceClipCount);
        rod = _srcClip[inputIndex]->getRegionOfDefinition(time);
        rodSet = true;
        break;
    }
    } // switch
    if (!rodSet) {
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        rod.x1 = offset.x;
        rod.y1 = offset.y;
        rod.x2 = offset.x + size.x;
        rod.y2 = offset.y + size.y;
    }

    return true;
} // SeExprPlugin::getRegionOfDefinition

class SeExprInteract
    : public RectangleInteract
{
public:

    SeExprInteract(OfxInteractHandle handle,
                   ImageEffect* effect);

    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL;
    virtual bool penMotion(const PenArgs &args) OVERRIDE FINAL;
    virtual bool penDown(const PenArgs &args) OVERRIDE FINAL;
    virtual bool penUp(const PenArgs &args) OVERRIDE FINAL;
    virtual bool keyDown(const KeyArgs &args) OVERRIDE FINAL;
    virtual bool keyUp(const KeyArgs & args) OVERRIDE FINAL;
    virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;

private:

    virtual void aboutToCheckInteractivity(OfxTime time) OVERRIDE FINAL;
    virtual bool allowTopLeftInteraction() const OVERRIDE FINAL;
    virtual bool allowBtmRightInteraction() const OVERRIDE FINAL;
    virtual bool allowBtmLeftInteraction() const OVERRIDE FINAL;
    virtual bool allowBtmMidInteraction() const OVERRIDE FINAL;
    virtual bool allowMidLeftInteraction() const OVERRIDE FINAL;
    virtual bool allowCenterInteraction() const OVERRIDE FINAL;
    ChoiceParam* _boundingBox;
    RegionOfDefinitionEnum _bboxType;
};

SeExprInteract::SeExprInteract(OfxInteractHandle handle,
                               ImageEffect* effect)
    : RectangleInteract(handle, effect)
    , _boundingBox(0)
    , _bboxType(eRegionOfDefinitionOptionUnion)
{
    _boundingBox = effect->fetchChoiceParam(kParamRegionOfDefinition);
    assert(_boundingBox);
}

void
SeExprInteract::aboutToCheckInteractivity(OfxTime /*time*/)
{
    int type_i;

    _boundingBox->getValue(type_i);
    _bboxType = (RegionOfDefinitionEnum)type_i;
}

bool
SeExprInteract::allowTopLeftInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::allowBtmRightInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::allowBtmLeftInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::allowBtmMidInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::allowMidLeftInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::allowCenterInteraction() const
{
    return _bboxType == eRegionOfDefinitionOptionSize;
}

bool
SeExprInteract::draw(const DrawArgs &args)
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
SeExprInteract::penMotion(const PenArgs &args)
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
SeExprInteract::penDown(const PenArgs &args)
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
SeExprInteract::penUp(const PenArgs &args)
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
SeExprInteract::loseFocus(const FocusArgs &args)
{
    return RectangleInteract::loseFocus(args);
}

bool
SeExprInteract::keyDown(const KeyArgs &args)
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
SeExprInteract::keyUp(const KeyArgs & args)
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

//mDeclarePluginFactory(SeExprPluginFactory, {}, {});

template<bool simple>
class SeExprPluginFactory
    : public PluginFactoryHelper<SeExprPluginFactory<simple> >
{
public:
    SeExprPluginFactory(const string& id,
                        unsigned int verMaj,
                        unsigned int verMin) : PluginFactoryHelper<SeExprPluginFactory>(id, verMaj, verMin) {}

    virtual void describe(ImageEffectDescriptor &desc);
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context);
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context);
};

template<bool simple>
void
SeExprPluginFactory<simple>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(simple ? kPluginNameSimple : kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    if ( desc.getPropertySet().propGetDimension(kNatronOfxPropDescriptionIsMarkdown, false) ) {
        desc.setPluginDescription(simple ?
                                  kPluginDescriptionSimple /*Markdown*/ :
                                  kPluginDescription /*Markdown*/, false);
        desc.getPropertySet().propSetInt(kNatronOfxPropDescriptionIsMarkdown, 1);
    } else {
        desc.setPluginDescription(simple ?
                                  kPluginDescriptionSimple :
                                  kPluginDescription);
    }

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
    if (getImageEffectHostDescription()->isNatron) {
        gHostIsNatron = true;
    } else {
        gHostIsNatron = false;
    }
#else
    gHostIsNatron = false;
#endif

#if defined(OFX_EXTENSIONS_NATRON) && defined(OFX_EXTENSIONS_NUKE)
    // TODO @MrKepzie: can we support multiplanar even if host is not Natron?
    if (getImageEffectHostDescription()->isMultiPlanar &&
        getImageEffectHostDescription()->supportsDynamicChoices) {
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
} // >::describe

template<bool simple>
void
SeExprPluginFactory<simple>::describeInContext(ImageEffectDescriptor &desc,
                                               ContextEnum context)
{
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();

    gHostIsNatron = gHostDescription.isNatron;
    bool hostIsNuke = (gHostDescription.hostName.find("nuke") != string::npos ||
                       gHostDescription.hostName.find("Nuke") != string::npos);

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
        ClipDescriptor *srcClip;
        if ( (i == 0) && (context == eContextFilter) ) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName); // mandatory clip for the filter context
        } else {
            const string istr = unsignedToString(i + 1);
            srcClip = desc.defineClip(istr);
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
        param->setLayoutHint(eLayoutHintNoNewLine, 1);

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
        for (int i = 0; i < kSourceClipCount; ++i) {
            const string istr = unsignedToString(i + 1);
            param->appendOption(kParamRegionOfDefinitionOptionCustomInput + istr, kParamRegionOfDefinitionOptionCustomInputHelp + istr);
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
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
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
        param->setDoubleType(eDoubleTypeXY);
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
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

    if (gHostIsMultiPlanar) {
        GroupParamDescriptor *group = desc.defineGroupParam("Input layers");
        group->setLabel("Input layers");
        group->setOpen(false);
        if (page) {
            page->addChild(*group);
        }
        for (int i = 0; i < kSourceClipCount; ++i) {
            const string istr = unsignedToString(i + 1);
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamLayerInput + istr);
                param->setLabel(kParamLayerInputLabel + istr);
                param->setHint(kParamLayerInputHint + istr);
                param->setAnimates(false);
                param->appendOption(kSeExprColorPlaneName);
                //param->setIsSecretAndDisabled(true); // done in the plugin constructor
                param->setParent(*group);
                param->setEvaluateOnChange(false);
                param->setIsPersistent(false);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                StringParamDescriptor *param = desc.defineStringParam(kParamLayerInputChoice + istr);
                param->setLabel(kParamLayerInputChoiceLabel + istr);
                param->setIsSecretAndDisabled(true); // always secret
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
            const string istr = unsignedToString(i + 1);
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDouble + istr);
            param->setLabel(kParamDoubleLabel + istr);
            param->setHint(kParamDoubleHint + istr);
            param->setAnimates(true);
            //param->setIsSecretAndDisabled(true); // done in the plugin constructor
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(-1000., 1000.);
            param->setDoubleType(eDoubleTypePlain);
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
            const string istr = unsignedToString(i + 1);
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamDouble2D + istr);
            param->setLabel(kParamDouble2DLabel + istr);
            param->setHint(kParamDouble2DHint + istr);
            param->setAnimates(true);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
            param->setDisplayRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
            //param->setIsSecretAndDisabled(true); // done in the plugin constructor
            param->setDoubleType(eDoubleTypeXYAbsolute);
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
            const string istr = unsignedToString(i + 1);
            RGBParamDescriptor *param = desc.defineRGBParam(kParamColor + istr);
            param->setLabel(kParamColorLabel + istr);
            param->setHint(kParamColorHint + istr);
            param->setAnimates(true);
            param->setParent(*group);
            //param->setIsSecretAndDisabled(true); // done in the plugin constructor
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

    if (simple) {
        {
            StringParamDescriptor *param = desc.defineStringParam(kParamRExpr);
            param->setLabel(kParamRExprLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamRExprHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamRExprHint);
            }
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            StringParamDescriptor *param = desc.defineStringParam(kParamGExpr);
            param->setLabel(kParamGExprLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamGExprHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamGExprHint);
            }
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            StringParamDescriptor *param = desc.defineStringParam(kParamBExpr);
            param->setLabel(kParamBExprLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamBExprHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamBExprHint);
            }
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            StringParamDescriptor *param = desc.defineStringParam(kParamAExpr);
            param->setLabel(kParamAExprLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamAExprHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamAExprHint);
            }
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
        }

        if (!gHostIsNatron) {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamShowExprs);
            param->setLabel(kParamShowExprsLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamShowExprsHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamShowExprsHint);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    } else {
        {
            StringParamDescriptor *param = desc.defineStringParam(kParamScript);
            param->setLabel(kParamScriptLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamScriptHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamScriptHint);
            }
            param->setStringType(eStringTypeMultiLine);
            param->setAnimates(true);
            //param->setDefault(kSeExprDefaultRGBScript);
            if (page) {
                page->addChild(*param);
            }
        }
        if (!gHostIsNatron) {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamShowScript);
            param->setLabel(kParamShowScriptLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamShowScriptHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamShowScriptHint);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            StringParamDescriptor *param = desc.defineStringParam(kParamAlphaScript);
            param->setLabel(kParamAlphaScriptLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamAlphaScriptHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamAlphaScriptHint);
            }
            param->setStringType(eStringTypeMultiLine);
            param->setAnimates(true);
            //param->setDefault(kSeExprDefaultAlphaScript);
            if (page) {
                page->addChild(*param);
            }
        }
        if (!gHostIsNatron) {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamShowAlphaScript);
            param->setLabel(kParamShowAlphaScriptLabel);
            if (hostIsNuke) {
                param->setHint(string(kParamShowAlphaScriptHint) + " " kNukeWarnTcl);
            } else {
                param->setHint(kParamShowAlphaScriptHint);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }


    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabel(kParamValidateLabel);
        param->setHint(kParamValidateHint);
        param->setEvaluateOnChange(true);
        if (gHostIsNatron) {
            param->setIsSecretAndDisabled(true);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // >::describeInContext

template<bool simple>
ImageEffect*
SeExprPluginFactory<simple>::createInstance(OfxImageEffectHandle handle,
                                            ContextEnum /*context*/)
{
    return new SeExprPlugin(handle, simple);
}

static SeExprPluginFactory<false> p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static SeExprPluginFactory<true> p2(kPluginIdentifierSimple, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
