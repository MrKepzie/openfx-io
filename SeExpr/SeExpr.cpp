/*
 OFX SeExpr plugin.
 Execute a SeExpr script.

 Copyright (C) 2015 INRIA
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France


 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England

 */
#include "SeExpr.h"

#include <vector>
#include <algorithm>
#include <limits>
#include <stdio.h> // for snprintf & _snprintf
#ifdef _WINDOWS
#include <windows.h>
#define snprintf _snprintf
#endif

#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsMerging.h"
#include "ofxsMultiThread.h"
#include "ofxsFormatResolution.h"
#include "ofxsGenerator.h"
#include "ofxsRectangleInteract.h"

#include <SeExpression.h>
#include <SeExprFunc.h>
#include <SeExprNode.h>
#include <SeMutex.h>

#define kPluginName "SeExpr"
#define kPluginGrouping "Merge"
#define kPluginDescription \
"Use the Walt Disney Animation Studio SeExpr expresion language to process pixels of the input image.\n" \
"SeExpr is licensed under the Apache License v2 and is copyright of Disney Enterprises, Inc.\n\n" \
"Some extensions to the language have been developped in order to use it in the purpose of filtering and blending input images. " \
"The following pre-defined variables can be used in the script:\n\n" \
"- x: This is the pixel X coordinate of the pixel to render (this is not normalized in the [0,1] range)\n\n" \
"- y: This is the pixel Y coordinate of the pixel to render (this is not normalized in the [0,1] range)\n\n" \
"- u: This is the normalized (to the output image size) X coordinate of the output pixel to render\n\n" \
"- v: This is the normalized (to the output image size) Y coordinate of the output pixel to render\n\n" \
"- scale: A 2-Dimensional vector (X,Y) indicating the scale at which the image is being rendered. Depending on the zoom level " \
"of the viewer, the image might be rendered at a lower scale than usual. This parameter is useful when producing spatial " \
"effects that need to be invariant to the pixel scale, especially when using X and Y coordinates. (0.5,0.5) means that the " \
"image is being rendered at half of its original size.\n\n " \
"- frame: This is the current frame being rendered\n\n" \
"- Each input has 2 variables named Cs<index> and As<index> which respectively references the color (RGB vector) " \
"and the alpha (scalar) of the image originated from the input at the given index. For the first input, you do not need to add " \
"the index after Cs and As. See usage example below.\n\n" \
"- output_width: This is the width of the output image being rendered. This is useful to normalize x coordinates into the range [0,1]\n\n" \
"- output_height: This is the height of the output image being rendered. This is useful to normalize y coordinates into the range [0,1]\n\n" \
"- Each input has a variable named input_width<index> and input_height<index> indicating respectively the width and height of the input. " \
"For the first input you do not need to add the index after input_width and input_height." \
"For example, the input 2 will have the variables input_width2 and input_height2.\n\n" \
"To fetch an arbitraty input pixel, you must use the getPixel(inputNumber,frame,x,y) function that will for " \
"a given input fetch the pixel at the (x,y) position in the image at the given frame. " \
"Note that inputNumber starts from 1 and that x,y are PIXEL COORDINATES and not normalized coordinates.\n\n" \
"Usage example (Application of the Multiply Merge operator on the input 1 and 2):\n\n" \
"Cs * Cs2\n\n" \
"Another merge operator example (over):\n\n" \
"Cs + Cs2 * (1 -  As)\n\n" \
"A more complex example used to average pixels over the previous, current and next frame:\n\n" \
"prev = getPixel(1,frame - 1,x,y);\n" \
"cur = Cs;\n" \
"next = getPixel(1,frame + 1,x,y);\n" \
"(prev + cur + next) / 3;\n\n" \
"To use custom variables that are pre-defined in the plug-in (scalars, positions and colors) you must reference them " \
"using their script-name in the expression. For example, the parameter x1 can be referenced using x1 in the script:\n\n" \
"Cs + x1\n\n" \
"Note that for expressions that span multiple lines, you must end each instruction by ; as you would do in C/C++. The last line " \
"of your expression will always be considered as the final value of the pixel.\n" \
"More documentation is available on the website of the SeExpr project: http://www.disneyanimation.com/technology/seexpr.html\n\n" \
"Limitations:\n\n" \
"In order to be efficient getPixel(inputNumber,frame,x,y) works only under certain circumstances:\n" \
"- the inputNumber must be in the correct range\n" \
"- frame must not depend on the color or alpha of a pixel, nor on the result of another call to getPixel\n" \
"- A call to getPixel must not depend on the color or alpha of a pixel, e.g this is not correct:\n\n" \
"if (As > 0.1) {\n" \
"    src = getPixel(1,frame,x,y);\n" \
"} else {\n" \
"    src = [0,0,0];\n" \
"}\n"


#define kPluginIdentifier "fr.inria.openfx.SeExpr"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kSourceClipCount 10
#define kParamsCount 10

#define kSeExprGetPixelFuncName "getPixel"
#define kSeExprCurrentTimeVarName "frame"
#define kSeExprXCoordVarName "x"
#define kSeExprYCoordVarName "y"
#define kSeExprUCoordVarName "u"
#define kSeExprVCoordVarName "v"
#define kSeExprInputWidthVarName "input_width"
#define kSeExprInputHeightVarName "input_height"
#define kSeExprOutputWidthVarName "output_width"
#define kSeExprOutputHeightVarName "output_height"
#define kSeExprColorVarName "Cs"
#define kSeExprAlphaVarName "As"
#define kSeExprRenderScaleVarName "scale"

#define kSeExprDefaultScript "#Just copy the source image\nCs"

#define kParamRegionOfDefinition "rod"
#define kParamRegionOfDefinitionLabel "Region of Definition"
#define kParamRegionOfDefinitionHint "The region of definition of the output."

#define kParamRegionOfDefinitionOptionFormat "Format"
#define kParamRegionOfDefinitionOptionFormatHelp "The output region will be of the specified format."
#define kParamRegionOfDefinitionOptionProject "Project"
#define kParamRegionOfDefinitionOptionProjectHelp "The output region will be of the size of the project."
#define kParamRegionOfDefinitionOptionSize "Size"
#define kParamRegionOfDefinitionOptionSizeHelp "The output region will be of the size of the rectangle overlay."
#define kParamRegionOfDefinitionOptionUnion "Union"
#define kParamRegionOfDefinitionOptionUnionHelp "The output region will be the union of the regions of definition of all connected inputs."
#define kParamRegionOfDefinitionOptionIntersection "Intersection"
#define kParamRegionOfDefinitionOptionIntersectionHelp "The output region will be the intersection the regions of definition of all connected inputs."
#define kParamRegionOfDefinitionOptionCustomInput "Input%d"
#define kParamRegionOfDefinitionOptionCustomInputHelp "The output region will be the regions of definition of input %d."

#define kParamGeneratorFormat "format"
#define kParamGeneratorFormatLabel "Format"
#define kParamGeneratorFormatHint "The output format"

#define kParamLayerInput "layerInput%d"
#define kParamLayerInputLabel "Input Layer %d"
#define kParamLayerInputHint "Select which layer from the input to use when calling " kSeExprGetPixelFuncName " on input %d."

#define kParamDoubleParamNumber "doubleParamsNb"
#define kParamDoubleParamNumberLabel "No. of Scalar Params"
#define kParamDoubleParamNumberHint "Use this to control how many scalar parameters should be exposed to the SeExpr expression."

#define kParamDouble "x%d"
#define kParamDoubleLabel "x%d"
#define kParamDoubleHint "A custom 1-dimensional variable that can be referenced in the expression by its script-name, x%d"

#define kParamDouble2DParamNumber "double2DParamsNb"
#define kParamDouble2DParamNumberLabel "No. of 2D Params"
#define kParamDouble2DParamNumberHint "Use this to control how many 2D (position) parameters should be exposed to the SeExpr expression."

#define kParamDouble2D "pos%d"
#define kParamDouble2DLabel "pos%d"
#define kParamDouble2DHint "A custom 2-dimensional variable that can be referenced in the expression by its script-name, pos%d"

#define kParamColorNumber "colorParamsNb"
#define kParamColorNumberLabel "No. of Color Params"
#define kParamColorNumberHint "Use this to control how many color parameters should be exposed to the SeExpr expression."

#define kParamColor "color%d"
#define kParamColorLabel "color%d"
#define kParamColorHint "A custom RGB variable that can be referenced in the expression by its script-name, color%d"

#define kParamScript "script"
#define kParamScriptLabel "Script"
#define kParamScriptHint "Contents of the SeExpr expression. See the description of the plug-in and " \
"http://www.disneyanimation.com/technology/seexpr.html for documentation."

#define kParamValidate                  "validate"
#define kParamValidateLabel             "Validate"
#define kParamValidateHint              "Validate the script contents and execute it on next render. This locks the script and all its parameters."

#define kSeExprColorPlaneName "Color"
#define kSeExprBackwardMotionPlaneName "Backward"
#define kSeExprForwardMotionPlaneName "Forward"
#define kSeExprDisparityLeftPlaneName "DisparityLeft"
#define kSeExprDisparityRightPlaneName "DisparityRight"

static bool gHostIsMultiPlanar;
static bool gHostIsNatron;

class SeExprProcessorBase;



////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SeExprPlugin : public OFX::ImageEffect {
public:
    /** @brief ctor */
    SeExprPlugin(OfxImageEffectHandle handle);

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

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
    
    std::string getOfxComponentsForClip(int inputNumber) const;
    
    std::string getOfxPlaneForClip(int inputNumber) const;
    
    OFX::Clip *_srcClip[kSourceClipCount];
    OFX::Clip* _maskClip;
    OFX::Clip *_dstClip;
    
    OFX::ChoiceParam *_clipLayerToFetch[kSourceClipCount];
    
    OFX::IntParam *_doubleParamCount;
    OFX::DoubleParam* _doubleParams[kParamsCount];
    
    OFX::IntParam *_double2DParamCount;
    OFX::Double2DParam* _double2DParams[kParamsCount];
    
    OFX::IntParam *_colorParamCount;
    OFX::RGBParam* _colorParams[kParamsCount];
    
    OFX::StringParam *_script;
    OFX::BooleanParam *_validate;
    
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
    
    OFX::ChoiceParam* _boundingBox;
    
    OFX::ChoiceParam* _format;
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OFX::BooleanParam* _interactive;

};

class OFXSeExpression;

int getNComponents(OFX::PixelComponentEnum pixelComps, const std::string& rawComponents) {
    switch (pixelComps) {
        case OFX::ePixelComponentRGBA:
            return 4;
        case OFX::ePixelComponentRGB:
            return 3;
        case OFX::ePixelComponentStereoDisparity:
        case OFX::ePixelComponentMotionVectors:
            return 2;
        case OFX::ePixelComponentAlpha:
            return 1;
        case OFX::ePixelComponentCustom:
        {
            std::string layer;
            std::vector<std::string> channelNames;
            if (!OFX::ImageBase::ofxCustomCompToNatronComp(rawComponents, &layer, &channelNames)) {
                return 0;
            }
            return (int)std::max((double)channelNames.size() , 3.);
        }   break;
        default:
            return 0;
    }
}

// Base class for processor, note that we do not use the multi-thread suite.
class SeExprProcessorBase
{
    
protected:
    
    OfxTime _renderTime;
    int _renderView;
    SeExprPlugin* _plugin;
    std::string _layersToFetch[kSourceClipCount];
    OFXSeExpression* _expression;
    const OFX::Image* _srcCurTime[kSourceClipCount];
    int _nSrcComponents[kSourceClipCount];
    OFX::Image* _dstImg;
    bool _maskInvert;
    const OFX::Image* _maskImg;
    bool _doMasking;
    double _mix;
    
    struct ImageData
    {
        const OFX::Image* img;
        int nComponents;
        
        ImageData() : img(0), nComponents(0) {}
    };
    
    
    // <clipIndex, <time, image> >
    typedef std::map<OfxTime, const ImageData> FetchedImagesForClipMap;
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
    

    
    void setValues(OfxTime time, int view, double mix, const std::string& expression, std::string* layers,
                   const OfxRectI& dstPixelRod, OfxPointI* inputSizes, const OfxPointI& outputSize, const OfxPointD& renderScale);
    
    bool isExprOk(std::string* error);

    const OFX::Image* getOrFetchImage(int inputIndex,
                                      OfxTime time,
                                      int* nComponents)
    {
        // find or create input
        FetchedImagesForClipMap& foundInput = _images[inputIndex];

        FetchedImagesForClipMap::iterator foundImage = foundInput.find(time);
        if (foundImage != foundInput.end()) {
            *nComponents = foundImage->second.nComponents;
            return foundImage->second.img;
        }

        OFX::Clip* clip = _plugin->getClip(inputIndex);
        assert(clip);

        if (!clip->isConnected()) {
            return 0;
        }

        ImageData data;
        if (gHostIsMultiPlanar) {
            data.img = clip->fetchImagePlane(time, _renderView,  _layersToFetch[inputIndex].c_str());
        } else {
            data.img = clip->fetchImage(time);
        }
        if (!data.img) {
            return 0;
        }
        data.nComponents = getNComponents(data.img->getPixelComponents(), data.img->getPixelComponentsProperty());
        *nComponents = data.nComponents;
        std::pair<FetchedImagesForClipMap::iterator, bool> ret = foundInput.insert(std::make_pair(time, data));
        assert(ret.second);
        return data.img;
    }

    virtual void process(OfxRectI procWindow) = 0;
};


template <typename PIX, int maxComps>
void getPixInternal(int nComps, const void* data, SeVec3d& result)
{
    const PIX* pix = (const PIX*)data;
    for (int i = 0; i < nComps; ++i) {
        result[i] = pix[i] / maxComps;
    }
}

class GetPixelFuncX : public SeExprFuncX
{
    SeExprProcessorBase* _processor;
    
public:
    
    
    GetPixelFuncX(SeExprProcessorBase* processor)
    : SeExprFuncX(true)  // Thread Safe
    , _processor(processor)
    {}
    
    virtual ~GetPixelFuncX() {}
    
    static int numArgs() { return 4; }
private:
    
    
    virtual bool prep(SeExprFuncNode* node, bool /*wantVec*/)
    {
        // check number of arguments
        int nargs = node->nargs();
        if (nargs != numArgs()) {
            node->addError("Wrong number of arguments, should be " kSeExprGetPixelFuncName "(inputIndex, frame, x, y)");
            return false;
        }
        
        for (int i = 0; i < numArgs(); ++i) {
            
            if (node->child(i)->isVec()) {
                node->addError("Wrong arguments, should be " kSeExprGetPixelFuncName "(inputIndex, frame, x, y)");
                return false;
            }
            if (!node->child(i)->prep(false)) {
                return false;
            }
            
            SeVec3d val;
            node->child(i)->eval(val);
            if ((val[0] - std::floor(val[0] + 0.5)) != 0.) {
                char name[256];
                snprintf(name, sizeof(name), "Argument %d should be an integer.", i+1);
                node->addError(name);
                return false;
            }

        }
        
        SeVec3d inputIndex;
        node->child(0)->eval(inputIndex);
        if (inputIndex[0] < 0 || inputIndex[0] >= kSourceClipCount) {
            node->addError("Invalid input index");
            return false;
        }
        
//        GetPixelFuncData* data = new GetPixelFuncData;
//        data->index = inputIndex[0];
//        data->frame = frame[0];
//        data->x = xCoord[0];
//        data->y = yCoord[0];
//        
//        node->setData((SeExprFuncNode::Data*)(data));
        return true;
    }
    

    
    virtual void eval(const SeExprFuncNode* node, SeVec3d& result) const
    {
    
        SeVec3d inputIndex;
        node->child(0)->eval(inputIndex);
        
        SeVec3d frame;
        node->child(1)->eval(frame);
        
        SeVec3d xCoord;
        node->child(2)->eval(xCoord);

        SeVec3d yCoord;
        node->child(3)->eval(yCoord);
        
        int nComponents;
        const OFX::Image* img = _processor->getOrFetchImage(inputIndex[0] - 1, frame[0], &nComponents);
        if (!img || nComponents == 0) {
            result[0] = result[1] = result[2] = 0.;
        } else {
            const void* data = img->getPixelAddress(xCoord[0], yCoord[0]);
            if (!data) {
                result[0] = result[1] = result[2] = 0.;
                return;
            }
            OFX::BitDepthEnum depth = img->getPixelDepth();
            switch (depth) {
                case OFX::eBitDepthFloat:
                    getPixInternal<float, 1>(nComponents, data, result);
                    break;
                case OFX::eBitDepthUByte:
                    getPixInternal<unsigned char, 255>(nComponents, data, result);
                    break;
                case OFX::eBitDepthUShort:
                    getPixInternal<unsigned short, 65535>(nComponents, data, result);
                    break;
                default:
                    result[0] = result[1] = result[2] = 0.;
                    break;
            }
        }
        //GetPixelFuncData *data = (GetPixelFuncData*) node->getData();
    
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
class StubGetPixelFuncX : public SeExprFuncX
{
    StubSeExpression* _expr;
    
public:
    
    StubGetPixelFuncX(StubSeExpression* expr)
    : SeExprFuncX(true)  // Thread Safe
    , _expr(expr)
    {}
    
    virtual ~StubGetPixelFuncX() {}
    
    static int numArgs() { return 4; }
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
    mutable StubGetPixelFuncX _getPix;
    mutable SeExprFunc _getPixFunction;
    mutable SimpleScalar _currentTime;
    mutable SimpleScalar _xCoord,_yCoord;

    mutable FramesNeeded _images;
    
public:
    
    StubSeExpression(const std::string& expr, OfxTime time);
    
    virtual ~StubSeExpression();
    
    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const std::string& name) const OVERRIDE FINAL;
    
    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const std::string& name) const OVERRIDE FINAL;
    
    void onGetPixelCalled(int inputIndex, OfxTime time) {
        
        {
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
        
    }
    
    const FramesNeeded& getFramesNeeded() const
    {
        return _images;
    }
    

};

class OFXSeExpression : public SeExpression
{
    mutable GetPixelFuncX _getPix;
    mutable SeExprFunc _getPixFunction;
    OfxRectI _dstPixelRod;
    typedef std::map<std::string,SeExprVarRef*> VariablesMap;
    VariablesMap _variables;
    
    SimpleVec* _scale;
    
    SimpleScalar* _curTime;
    SimpleScalar* _xCoord;
    SimpleScalar* _yCoord;
    SimpleScalar* _uCoord;
    SimpleScalar* _vCoord;
    
    SimpleScalar* _outputWidth;
    SimpleScalar* _outputHeight;
    SimpleScalar* _inputWidths[kSourceClipCount];
    SimpleScalar* _inputHeights[kSourceClipCount];
    
    SimpleVec* _inputColors[kSourceClipCount];
    SimpleScalar* _inputAlphas[kSourceClipCount];
    
    DoubleParamVarRef* _doubleRef[kParamsCount];
    Double2DParamVarRef* _double2DRef[kParamsCount];
    ColorParamVarRef* _colorRef[kParamsCount];
public:
    
    
    
    
    OFXSeExpression(SeExprProcessorBase* processor,const std::string& expr, OfxTime time,
                    const OfxPointD& renderScale, const OfxRectI& outputRod);
    
    virtual ~OFXSeExpression();
    
    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const std::string& name) const OVERRIDE FINAL;
    
    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const std::string& name) const OVERRIDE FINAL;
    
    /** NOT MT-SAFE, this object is to be used PER-THREAD*/
    void setXY(int x, int y) {
        _xCoord->_value = x;
        _yCoord->_value = y;
        _uCoord->_value = (x + 0.5 - _dstPixelRod.x1) / (_dstPixelRod.x2 - _dstPixelRod.x1);
        _vCoord->_value = (y + 0.5 - _dstPixelRod.y1) / (_dstPixelRod.y2 - _dstPixelRod.y1);
    }
    
    void setRGBA(int inputIndex, float r, float g, float b, float a) {
        _inputColors[inputIndex]->_value[0] = r;
        _inputColors[inputIndex]->_value[1] = g;
        _inputColors[inputIndex]->_value[2] = b;
        _inputAlphas[inputIndex]->_value = a;
    }
    
    void setSize(int inputNumber, int w, int h) {
        if (inputNumber == -1) {
            _outputWidth->_value = w;
            _outputHeight->_value = h;
        } else {
            _inputWidths[inputNumber]->_value = w;
            _inputHeights[inputNumber]->_value = h;
        }
    }

    
};

OFXSeExpression::OFXSeExpression( SeExprProcessorBase* processor, const std::string& expr, OfxTime time,
                                 const OfxPointD& renderScale, const OfxRectI& outputRod)
: SeExpression(expr)
, _getPix(processor)
, _getPixFunction(_getPix, _getPix.numArgs(), _getPix.numArgs())
, _dstPixelRod(outputRod)
, _variables()
, _scale(0)
, _curTime(0)
, _xCoord(0)
, _yCoord(0)
, _uCoord(0)
, _vCoord(0)
, _outputWidth(0)
, _outputHeight(0)
, _inputWidths()
, _inputHeights()
, _inputColors()
, _inputAlphas()
, _doubleRef()
, _double2DRef()
, _colorRef()
{
    _dstPixelRod = outputRod;
    
    _scale = new SimpleVec;
    _scale->_value[0] = renderScale.x;
    _scale->_value[1] = renderScale.y;
    _scale->_value[2] = 1.;
    _variables.insert(std::make_pair(kSeExprRenderScaleVarName, _scale));
    
    assert(processor);
    SeExprPlugin* plugin = processor->getPlugin();
    
    char name[256];
    
    _curTime = new SimpleScalar;
    _curTime->_value = time;
    _variables.insert(std::make_pair(std::string(kSeExprCurrentTimeVarName), _curTime));
    
    _xCoord = new SimpleScalar;
    _variables.insert(std::make_pair(std::string(kSeExprXCoordVarName), _xCoord));
    
    _yCoord = new SimpleScalar;
    _variables.insert(std::make_pair(std::string(kSeExprYCoordVarName), _yCoord));
    
    _uCoord = new SimpleScalar;
    _variables.insert(std::make_pair(std::string(kSeExprUCoordVarName), _uCoord));
    
    _vCoord = new SimpleScalar;
    _variables.insert(std::make_pair(std::string(kSeExprVCoordVarName), _vCoord));
    
    _outputWidth = new SimpleScalar;
    _variables.insert(std::make_pair(std::string(kSeExprOutputWidthVarName), _outputWidth));
    
    _outputHeight = new SimpleScalar;
    _variables.insert(std::make_pair(std::string(kSeExprOutputHeightVarName), _outputHeight));
    
    for (int i = 0; i < kSourceClipCount; ++i) {
        snprintf(name, sizeof(name), kSeExprInputWidthVarName "%d", i+1);
        _inputWidths[i] = new SimpleScalar;
        _variables.insert(std::make_pair(std::string(name), _inputWidths[i]));
        if (i == 0) {
            _variables.insert(std::make_pair(std::string(kSeExprInputWidthVarName), _inputWidths[i]));
        }
        
        snprintf(name, sizeof(name), kSeExprInputHeightVarName "%d", i+1);
        _inputHeights[i] = new SimpleScalar;
        _variables.insert(std::make_pair(std::string(name), _inputHeights[i]));
        
        if (i == 0) {
            _variables.insert(std::make_pair(std::string(kSeExprInputHeightVarName), _inputHeights[i]));
        }
        
        snprintf(name, sizeof(name), kSeExprColorVarName "%d", i+1);
        _inputColors[i] = new SimpleVec;
        _variables.insert(std::make_pair(std::string(name), _inputColors[i]));
        
        if (i == 0) {
            _variables.insert(std::make_pair(std::string(kSeExprColorVarName), _inputColors[i]));
        }
        
        snprintf(name, sizeof(name), kSeExprAlphaVarName "%d", i+1);
        _inputAlphas[i] = new SimpleScalar;
        _variables.insert(std::make_pair(std::string(name), _inputAlphas[i]));
        
        if (i == 0) {
            _variables.insert(std::make_pair(std::string(kSeExprAlphaVarName), _inputAlphas[i]));
        }
    }
    
    
    OFX::DoubleParam** doubleParams = plugin->getDoubleParams();
    OFX::Double2DParam** double2DParams = plugin->getDouble2DParams();
    OFX::RGBParam** colorParams = plugin->getRGBParams();
    
    for (int i = 0; i < kParamsCount; ++i) {
        _doubleRef[i] = new DoubleParamVarRef(doubleParams[i]);
        _double2DRef[i]  = new Double2DParamVarRef(double2DParams[i]);
        _colorRef[i]  = new ColorParamVarRef(colorParams[i]);
        snprintf(name, sizeof(name), kParamDouble, i+1);
        _variables.insert(std::make_pair(std::string(name), _doubleRef[i]));
        snprintf(name, sizeof(name), kParamDouble2D, i+1);
        _variables.insert(std::make_pair(std::string(name), _double2DRef[i]));
        snprintf(name, sizeof(name), kParamColor, i+1);
        _variables.insert(std::make_pair(std::string(name), _colorRef[i]));
    }
    
    
    
}

OFXSeExpression::~OFXSeExpression()
{
    delete _scale;
    delete _curTime;
    delete _xCoord;
    delete _yCoord;
    delete _uCoord;
    delete _vCoord;
    delete _outputWidth;
    delete _outputHeight;
    
    for (int i = 0; i < kParamsCount; ++i) {
        delete _doubleRef[i];
        delete _double2DRef[i];
        delete _colorRef[i];
    }
    
    for (int i = 0; i < kSourceClipCount; ++i) {
        delete _inputWidths[i];
        delete _inputHeights[i];
        delete _inputColors[i];
        delete _inputAlphas[i];
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
    
    if (funcName == kSeExprGetPixelFuncName) {
        return &_getPixFunction;
    }
    return 0;
}


bool
StubGetPixelFuncX::prep(SeExprFuncNode* node, bool /*wantVec*/)
{
    // check number of arguments
    int nargs = node->nargs();
    if (nargs != numArgs()) {
        node->addError("Wrong number of arguments, should be " kSeExprGetPixelFuncName "(inputIndex, frame, x, y)");
        return false;
    }
    
    for (int i = 0; i < numArgs(); ++i) {
        
        if (node->child(i)->isVec()) {
            node->addError("Wrong arguments, should be " kSeExprGetPixelFuncName "(inputIndex, frame, x, y)");
            return false;
        }
        if (!node->child(i)->prep(false)) {
            return false;
        }
        
        SeVec3d val;
        node->child(i)->eval(val);
        if ((val[0] - std::floor(val[0] + 0.5)) != 0.) {
            char name[256];
            snprintf(name, sizeof(name), "Argument %d should be an integer.", i+1);
            node->addError(name);
            return false;
        }
        
    }
    
    SeVec3d inputIndex;
    node->child(0)->eval(inputIndex);
    if (inputIndex[0] < 0 || inputIndex[0] >= kSourceClipCount) {
        node->addError("Invalid input index");
        return false;
    }
    return true;
}



void
StubGetPixelFuncX::eval(const SeExprFuncNode* node, SeVec3d& result) const
{
    
    SeVec3d inputIndex;
    node->child(0)->eval(inputIndex);
    
    SeVec3d frame;
    node->child(1)->eval(frame);
    
    
    _expr->onGetPixelCalled(inputIndex[0] - 1, frame[0]);
    result[0] = result[1] = result[2] = std::numeric_limits<double>::quiet_NaN();
}

StubSeExpression::StubSeExpression(const std::string& expr, OfxTime time)
: SeExpression(expr)
, _nanScalar()
, _zeroScalar()
, _getPix(this)
, _getPixFunction(_getPix, 4, 4)
, _currentTime()
, _xCoord()
, _yCoord()
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
        return &_xCoord;
    } else if (varName == kSeExprYCoordVarName) {
        return &_yCoord;
    } else if (varName == kSeExprUCoordVarName) {
        return &_zeroScalar;
    } else if (varName == kSeExprUCoordVarName) {
        return &_zeroScalar;
    } else if (varName == kSeExprOutputWidthVarName) {
        return &_zeroScalar;
    } else if (varName == kSeExprOutputHeightVarName) {
        return &_zeroScalar;
    } else if (varName == kSeExprColorVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprAlphaVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprInputWidthVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprInputHeightVarName) {
        return &_nanScalar;
    } else if (varName == kSeExprRenderScaleVarName) {
        return &_zeroScalar;
    }

    return &_zeroScalar;
}

/** override resolveFunc to add external functions */
SeExprFunc*
StubSeExpression::resolveFunc(const std::string& funcName) const
{
    // check if it is builtin so we get proper behavior
    if (SeExprFunc::lookup(funcName)) {
        return 0;
    }
    if (funcName == kSeExprGetPixelFuncName) {
        return &_getPixFunction;
    }
    return 0;
}

SeExprProcessorBase::SeExprProcessorBase(SeExprPlugin* instance)
: _renderTime(0)
, _renderView(0)
, _plugin(instance)
, _expression(0)
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
    delete _expression;
    for (FetchedImagesMap::iterator it = _images.begin(); it!=_images.end(); ++it) {
        for (FetchedImagesForClipMap::iterator it2 = it->second.begin(); it2!= it->second.end(); ++it2) {
            delete it2->second.img;
        }
    }
}

void
SeExprProcessorBase::setValues(OfxTime time, int view, double mix, const std::string& expression, std::string* layers, const OfxRectI& dstPixelRod, OfxPointI* inputSizes, const OfxPointI& outputSize, const OfxPointD& renderScale)
{
    _renderTime = time;
    _renderView = view;
    _expression = new OFXSeExpression(this, expression, time, renderScale, dstPixelRod);
    if (gHostIsMultiPlanar) {
        for (int i = 0; i < kSourceClipCount; ++i) {
            _layersToFetch[i] = layers[i];
        }
    }
    for (int i = 0; i < kSourceClipCount; ++i) {
        _expression->setSize(i, inputSizes[i].x, inputSizes[i].y);
    }
    _expression->setSize(-1, outputSize.x, outputSize.y);
    _mix = mix;
}

bool
SeExprProcessorBase::isExprOk(std::string* error)
{
    if (!_expression->isValid()) {
        *error = _expression->parseError();
        return false;
    }
    
    //Run the expression once to initialize all the images fields before multi-threading
    (void)_expression->evaluate();
    
    //Ensure the image of the input 0 at the current time exists for the mix

    for (int i = 0; i < kSourceClipCount; ++i) {
        int nComps = 0;
        _srcCurTime[i] = getOrFetchImage(i, _renderTime, &nComps);
        _nSrcComponents[i] = nComps;
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
        float tmpPix[nComponents];
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
                    _expression->setRGBA(i, srcPixels[i][0] / maxValue, srcPixels[i][1] / maxValue, srcPixels[i][2] / maxValue, srcPixels[i][3] / maxValue);
                }
                
                _expression->setXY(x, y);
                SeVec3d result = _expression->evaluate();
                
                for (int k = 0; k < nComponents; ++k) {
                    if (k < 3) {
                        tmpPix[k] = result[k];
                    } else {
                        tmpPix[k] = 0.;
                    }
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
    for (int i = 0; i < kSourceClipCount; ++i) {
        if (i == 0 && getContext() == OFX::eContextFilter) {
            _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            snprintf(name, sizeof(name), "%d", i+1);
            _srcClip[i] = fetchClip(name);
        }
    }
    
    _maskClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
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
            snprintf(name, sizeof(name), kParamLayerInput, i+1 );
            _clipLayerToFetch[i] = fetchChoiceParam(name);
        } else {
            _clipLayerToFetch[i] = 0;
        }
        snprintf(name, sizeof(name), kParamDouble, i+1 );
        _doubleParams[i] = fetchDoubleParam(name);
        snprintf(name, sizeof(name), kParamDouble2D, i+1 );
        _double2DParams[i] = fetchDouble2DParam(name);
        snprintf(name, sizeof(name), kParamColor, i+1 );
        _colorParams[i] = fetchRGBParam(name);
    }
    _script = fetchStringParam(kParamScript);
    assert(_script);
    _validate = fetchBooleanParam(kParamValidate);
    assert(_validate);
    
    _mix = fetchDoubleParam(kParamMix);
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);

    _boundingBox = fetchChoiceParam(kParamRegionOfDefinition);
    assert(_boundingBox);

    
    _format = fetchChoiceParam(kParamGeneratorFormat);
    _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
    _size = fetchDouble2DParam(kParamRectangleInteractSize);
    _interactive = fetchBooleanParam(kParamRectangleInteractInteractive);
    assert(_format && _btmLeft && _size && _interactive);
    
    // update visibility
    int numVisible;
    _doubleParamCount->getValue(numVisible);
    assert(numVisible <= kParamsCount && numVisible >=0);
    for (int i = 0; i < kParamsCount; ++i) {
        bool visible = i < numVisible;
        _doubleParams[i]->setIsSecret(!visible);
    }
    _double2DParamCount->getValue(numVisible);
    assert(numVisible <= kParamsCount && numVisible >=0);
    for (int i = 0; i < kParamsCount; ++i) {
        bool visible = i < numVisible;
        _double2DParams[i]->setIsSecret(!visible);
    }
    _colorParamCount->getValue(numVisible);
    assert(numVisible <= kParamsCount && numVisible >=0);
    for (int i = 0; i < kParamsCount; ++i) {
        bool visible = i < numVisible;
        _colorParams[i]->setIsSecret(!visible);
    }

    int boundingbox_i;
    _boundingBox->getValue(boundingbox_i);
    bool hasFormat = (boundingbox_i == 3);
    bool hasSize = (boundingbox_i == 2);
    
    _format->setEnabled(hasFormat);
    _format->setIsSecret(!hasFormat);
    _size->setEnabled(hasSize);
    _size->setIsSecret(!hasSize);
    _btmLeft->setEnabled(hasSize);
    _btmLeft->setIsSecret(!hasSize);
    _interactive->setEnabled(hasSize);
    _interactive->setIsSecret(!hasSize);
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
        for (std::list<std::string>::iterator it = components.begin(); it!=components.end(); ++it) {
            std::string layer;
            std::vector<std::string> channels;
            if (!OFX::ImageBase::ofxCustomCompToNatronComp(*it, &layer, &channels)) {
                continue;
            }
            if (layer == opt) {
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
            std::string layer;
            std::vector<std::string> channels;
            if (!OFX::ImageBase::ofxCustomCompToNatronComp(*it, &layer, &channels)) {
                continue;
            }
            if (layer == opt) {
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

    std::string script;
    _script->getValue(script);
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
    std::auto_ptr<const OFX::Image> mask((getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) ?
                                         _maskClip->fetchImage(args.time) : 0);
    
    // do we do masking
    if (getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) {
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
            OFX::MergeImages2D::toPixelEnclosing(rod, args.renderScale, par, &pixelRod);
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
    
    OFX::MergeImages2D::toPixelEnclosing(outputRod, args.renderScale, par, &outputPixelRod);
    OfxPointI outputSize;
    outputSize.x = outputPixelRod.x2 - outputPixelRod.x1;
    outputSize.y = outputPixelRod.y2 - outputPixelRod.y1;
    
    processor.setValues(args.time, args.renderView, mix, script, inputLayers, outputPixelRod, inputSizes, outputSize, args.renderScale);
    
    std::string error;
    if (!processor.isExprOk(&error)) {
        setPersistentMessage(OFX::Message::eMessageError, "", error);
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    
    
    processor.process(args.renderWindow);
}

void
SeExprPlugin::render(const OFX::RenderArguments &args)
{
    clearPersistentMessage();
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    if (!gHostIsNatron) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);
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
        }
    }

}

void
SeExprPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if (paramName == kParamDoubleParamNumber) {
        int numVisible;
        _doubleParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _doubleParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamDouble2DParamNumber) {
        int numVisible;
        _double2DParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _double2DParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamColorNumber) {
        int numVisible;
        _colorParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _colorParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamValidate) {
        if (!gHostIsNatron) {
            bool validated;
            _validate->getValue(validated);
            
            _doubleParamCount->setEnabled(!validated);
            _double2DParamCount->setEnabled(!validated);
            _colorParamCount->setEnabled(!validated);
            _doubleParamCount->setEvaluateOnChange(validated);
            _double2DParamCount->setEvaluateOnChange(validated);
            _colorParamCount->setEvaluateOnChange(validated);
            _script->setEnabled(!validated);
            _script->setEvaluateOnChange(validated);
            if (validated) {
                clearPersistentMessage();
            }
        }
    } else if (paramName == kParamRegionOfDefinition && args.reason == OFX::eChangeUserEdit) {
        int boundingbox_i;
        _boundingBox->getValue(boundingbox_i);
        bool hasFormat = (boundingbox_i == 3);
        bool hasSize = (boundingbox_i == 2);
        
        _format->setEnabled(hasFormat);
        _format->setIsSecret(!hasFormat);
        _size->setEnabled(hasSize);
        _size->setIsSecret(!hasSize);
        _btmLeft->setEnabled(hasSize);
        _btmLeft->setIsSecret(!hasSize);
        _interactive->setEnabled(hasSize);
        _interactive->setIsSecret(!hasSize);
    }

}

void
SeExprPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (!gHostIsMultiPlanar) {
        return;
    }
    if (args.reason == OFX::eChangeUserEdit) {
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

void
SeExprPlugin::buildChannelMenus()
{
    
    for (int i = 0; i < kSourceClipCount; ++i) {
        
        _clipLayerToFetch[i]->resetOptions();
        _clipLayerToFetch[i]->appendOption(kSeExprColorPlaneName);

        std::list<std::string> components = _srcClip[i]->getComponentsPresent();
        for (std::list<std::string> ::iterator it = components.begin(); it!=components.end(); ++it) {
            const std::string& comp = *it;
            if (comp == kOfxImageComponentAlpha) {
                continue;
            } else if (comp == kOfxImageComponentRGB) {
                continue;
            } else if (comp == kOfxImageComponentRGBA) {
                continue;
            } else if (comp == kFnOfxImageComponentMotionVectors) {
                _clipLayerToFetch[i]->appendOption(kSeExprBackwardMotionPlaneName);
                _clipLayerToFetch[i]->appendOption(kSeExprForwardMotionPlaneName);
            } else if (comp == kFnOfxImageComponentStereoDisparity) {
                _clipLayerToFetch[i]->appendOption(kSeExprDisparityLeftPlaneName);
                _clipLayerToFetch[i]->appendOption(kSeExprDisparityRightPlaneName);
#ifdef OFX_EXTENSIONS_NATRON
            } else {
                std::string layer;
                std::vector<std::string> channels;
                if (OFX::ImageBase::ofxCustomCompToNatronComp(comp, &layer, &channels)) {
                    _clipLayerToFetch[i]->appendOption(layer);
                }
#endif
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

    
    if (boundingBox_i == 2) {
        //size
    } else if (boundingBox_i == 3) {
        //format
        int index;
        _format->getValue(index);
        size_t w,h;
        getFormatResolution( (OFX::EParamFormat)index, &w, &h, &par );
    } else if (boundingBox_i == 4) {
        //project format
        
        /// this should be the defalut value given by the host, no need to set it.
        /// @see Instance::setupClipPreferencesArgs() in HostSupport, it should have
        /// the line:
        /// double inputPar = getProjectPixelAspectRatio();
        
        //par = getProjectPixelAspectRatio();
    }
    
    if (par != 0.) {
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
    }
    
    //We're frame varying since we don't know what the user may output at any frame
    clipPreferences.setOutputFrameVarying(true);
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
    
    //To determine the frames needed of the expression, we just execute the expression for 1 pixel
    //and record what are the calls made to getPixel in order to figure out the Roi.
    
    std::string script;
    _script->getValue(script);
    
    StubSeExpression expr(script,args.time);
    if (!expr.isValid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", expr.parseError());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    //We trust that only evaluating the expression for 1 pixel will make all the calls to getPixel
    //In other words, we do not support scripts that do not fetch all images needed for all pixels, e.g:
    /*
        if(x > 0) {
            srcCol = getPixel(0,frame,5,5)
        } else {
            srcCol = [0,0,0]
        }
     */
    (void)expr.evaluate();
    const FramesNeeded& framesNeeded = expr.getFramesNeeded();
    for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
        
        assert(it->first >= 0  && it->first < kSourceClipCount);
        OFX::Clip* clip = getClip(it->first);
        assert(clip);
        
        bool hasFetchedCurrentTime = false;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            
            if (it->second[i] != it->second[i]) {
                //This number is NaN! The user probably used something dependant on a pixel value as a time for the getPixel function
                setPersistentMessage(OFX::Message::eMessageError, "", "Invalid frame for getPixel, see the Limitations in the description.");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
            
            OfxRangeD range;
            if (it->second[i] == args.time) {
                hasFetchedCurrentTime = true;
            }
            range.min = range.max = it->second[i];
            frames.setFramesNeeded(*clip, range);
        }
        if (!hasFetchedCurrentTime) {
            OfxRangeD range;
            range.min = range.max = args.time;
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
        //To determine the ROIs of the expression, we just execute the expression at the 4 corners of the render window
        //and record what are the calls made to getPixel in order to figure out the Roi.
        
        std::string script;
        _script->getValue(script);
        
        StubSeExpression expr(script,args.time);
        if (!expr.isValid()) {
            setPersistentMessage(OFX::Message::eMessageError, "", expr.parseError());
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        
        //Notify that we will need the RoI for all connected input clips at the current time
        for (int i = 0; i < kSourceClipCount; ++i) {
            OFX::Clip* clip = getClip(i);
            assert(clip);
            if (clip->isConnected()) {
                rois.setRegionOfInterest(*clip, args.regionOfInterest);
            }
        }
        
        //Now evaluate the expression once and determine whether the user will call getPixel.
        //If he/she does, then we have no choice but to ask for the entire input image because we do not know
        //what the user may need (typically when applying UVMaps and stuff)
        
        double par = _srcClip[0]->getPixelAspectRatio();
        
        OfxRectI originalRoIPixel;
        OFX::MergeImages2D::toPixelEnclosing(args.regionOfInterest, args.renderScale, par, &originalRoIPixel);
        
        (void)expr.evaluate();
        const FramesNeeded& framesNeeded = expr.getFramesNeeded();
        
        for (FramesNeeded::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
            OFX::Clip* clip = getClip(it->first);
            assert(clip);
            if (clip->isConnected()) {
                rois.setRegionOfInterest(*clip, clip->getRegionOfDefinition(args.time));
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
    }
    
    bool rodSet = false;
    
    int boundingBox_i;
    _boundingBox->getValue(boundingBox_i);
    
    if (boundingBox_i == 0) {
        //union of inputs
        for (int i = 0; i < kSourceClipCount; ++i) {
            if (_srcClip[i]->isConnected()) {
                OfxRectD srcRod = _srcClip[i]->getRegionOfDefinition(args.time);
                OFX::MergeImages2D::rectBoundingBox(srcRod, rod, &rod);
                rodSet = true;
            }
        }
        
    } else if (boundingBox_i == 1) {
        //intersection of inputs
        bool rodSet = false;
        for (int i = 0; i < kSourceClipCount; ++i) {
            if (_srcClip[i]->isConnected()) {
                OfxRectD srcRod = _srcClip[i]->getRegionOfDefinition(args.time);
                if (rodSet) {
                    OFX::MergeImages2D::rectIntersection<OfxRectD>(srcRod, rod, &rod);
                } else {
                    rod = srcRod;
                }
                rodSet = true;
            }
        }
    } else if (boundingBox_i == 2) {
      // custom size
        _size->getValue(rod.x2, rod.y2);
        _btmLeft->getValue(rod.x1, rod.y1);
        rod.x2 += rod.x1;
        rod.y2 += rod.y1;
        
        rodSet = true;
    } else if (boundingBox_i == 3) {
      // format
        
        int format_i;
        _format->getValue(format_i);
        double par;
        size_t w,h;
        getFormatResolution( (OFX::EParamFormat)format_i, &w, &h, &par );
        rod.x1 = rod.y1 = 0;
        rod.x2 = w;
        rod.y2 = h;
        
        rodSet = true;
    } else if (boundingBox_i == 4) {
      // project
        OfxPointD extent = getProjectExtent();
        OfxPointD offset = getProjectOffset();
        rod.x1 = offset.x;
        rod.y1 = offset.y;
        rod.x2 = extent.x;
        rod.y2 = extent.y;
        rodSet =true;
    } else {
        int inputIndex = boundingBox_i - 2;
        assert(inputIndex >= 0 && inputIndex < kSourceClipCount);
        rod = _srcClip[inputIndex]->getRegionOfDefinition(args.time);
        rodSet = true;
    }
    
    if (!rodSet) {
        OfxPointD extent = getProjectExtent();
        OfxPointD offset = getProjectOffset();
        rod.x1 = offset.x;
        rod.y1 = offset.y;
        rod.x2 = extent.x;
        rod.y2 = extent.y;
    }
    return true;
}

using namespace OFX;

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
    int _bboxType;
};

SeExprInteract::SeExprInteract(OfxInteractHandle handle,
                                     OFX::ImageEffect* effect)
: RectangleInteract(handle,effect)
, _boundingBox(0)
, _bboxType(0)
{
    _boundingBox = effect->fetchChoiceParam(kParamRegionOfDefinition);
    assert(_boundingBox);
}

void SeExprInteract::aboutToCheckInteractivity(OfxTime /*time*/)
{
    int type_i;
    _boundingBox->getValue(type_i);
    _bboxType = type_i;
}

bool SeExprInteract::allowTopLeftInteraction() const
{
    return _bboxType == 2;
}

bool SeExprInteract::allowBtmRightInteraction() const
{
    return _bboxType == 2;
}

bool SeExprInteract::allowBtmLeftInteraction() const
{
    return _bboxType == 2;
}

bool SeExprInteract::allowBtmMidInteraction() const
{
    return _bboxType == 2;
}

bool SeExprInteract::allowMidLeftInteraction() const
{
    return _bboxType == 2;
}

bool SeExprInteract::allowCenterInteraction() const
{
    return _bboxType == 2;
}

bool
SeExprInteract::draw(const OFX::DrawArgs &args)
{
    int type_i;
    _boundingBox->getValue(type_i);
    
    if (type_i != 2) {
        return false;
    }
    
    return RectangleInteract::draw(args);
}

bool
SeExprInteract::penMotion(const OFX::PenArgs &args)
{
    int type_i;
    _boundingBox->getValue(type_i);
    
    if (type_i != 2) {
        return false;
    }
    
    return RectangleInteract::penMotion(args);
}

bool
SeExprInteract::penDown(const OFX::PenArgs &args)
{
    int type_i;
    _boundingBox->getValue(type_i);
    
    if (type_i != 2) {
        return false;
    }
    
    return RectangleInteract::penDown(args);
}

bool
SeExprInteract::penUp(const OFX::PenArgs &args)
{
    int type_i;
    _boundingBox->getValue(type_i);
    
    if (type_i != 2) {
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
    int type_i;
    _boundingBox->getValue(type_i);
    
    if (type_i != 2) {
        return false;
    }
    
    return RectangleInteract::keyDown(args);
}

bool
SeExprInteract::keyUp(const OFX::KeyArgs & args)
{
    int type_i;
    _boundingBox->getValue(type_i);
    
    if (type_i != 2) {
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
    desc.addSupportedBitDepth(eBitDepthCustom);

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
        desc.setIsPassThroughForNotProcessedPlanes(true);
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
    char name[256];
    char help[256];
    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kSourceClipCount; ++i) {
        snprintf(name, sizeof(name), "%d", i+1);
        ClipDescriptor *srcClip;
        if (i == 0 && context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName); // mandatory clip for the filter context
        } else {
            srcClip = desc.defineClip(name);
        }
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->addSupportedComponent(ePixelComponentCustom);
        srcClip->setTemporalClipAccess(true);
        srcClip->setSupportsTiles(true);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
    }

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->addSupportedComponent(ePixelComponentCustom);
    dstClip->setSupportsTiles(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamRegionOfDefinition);
        param->setLabel(kParamRegionOfDefinitionLabel);
        param->setHint(kParamRegionOfDefinitionHint);
        
        param->appendOption(kParamRegionOfDefinitionOptionUnion, kParamRegionOfDefinitionOptionUnionHelp);
        param->appendOption(kParamRegionOfDefinitionOptionIntersection, kParamRegionOfDefinitionOptionIntersectionHelp);
        param->appendOption(kParamRegionOfDefinitionOptionSize, kParamRegionOfDefinitionOptionSizeHelp);
        param->appendOption(kParamRegionOfDefinitionOptionFormat, kParamRegionOfDefinitionOptionFormatHelp);
        param->appendOption(kParamRegionOfDefinitionOptionProject, kParamRegionOfDefinitionOptionProjectHelp);

        for (int i = 0; i < kSourceClipCount; ++i) {
            snprintf(name, sizeof(name), kParamRegionOfDefinitionOptionCustomInput, i+1);
            snprintf(help, sizeof(help), kParamRegionOfDefinitionOptionCustomInputHelp, i+1);
            param->appendOption(name, help);
        }
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamGeneratorFormat);
        param->setLabel(kParamGeneratorFormatLabel);
        param->setAnimates(false);
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
            snprintf(name, sizeof(name), kParamLayerInput, i+1);
            ChoiceParamDescriptor *param = desc.defineChoiceParam(name);
            snprintf(name, sizeof(name), kParamLayerInputLabel, i+1);
            param->setLabel(name);
            snprintf(name, sizeof(name), kParamLayerInputHint, i+1);
            param->setHint(name);
            param->setAnimates(false);
            param->appendOption(kSeExprColorPlaneName);
            //param->setIsSecret(true); // done in the plugin constructor
            param->setParent(*group);
            if (page) {
                page->addChild(*param);
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
            snprintf(name, sizeof(name), kParamDouble, i+1);
            DoubleParamDescriptor *param = desc.defineDoubleParam(name);
            snprintf(name, sizeof(name), kParamDoubleLabel, i+1);
            param->setLabel(name);
            snprintf(name, sizeof(name), kParamDoubleHint, i+1);
            param->setHint(name);
            param->setAnimates(true);
            //param->setIsSecret(true); // done in the plugin constructor
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
            snprintf(name, sizeof(name), kParamDouble2D, i+1);
            Double2DParamDescriptor *param = desc.defineDouble2DParam(name);
            snprintf(name, sizeof(name), kParamDouble2DLabel, i+1);
            param->setLabel(name);
            snprintf(name, sizeof(name), kParamDouble2DHint, i+1);
            param->setHint(name);
            param->setAnimates(true);
            //param->setIsSecret(true); // done in the plugin constructor
            param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
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
            snprintf(name, sizeof(name), kParamColor, i+1);
            RGBParamDescriptor *param = desc.defineRGBParam(name);
            snprintf(name, sizeof(name), kParamColorLabel, i+1);
            param->setLabel(name);
            snprintf(name, sizeof(name), kParamColorHint, i+1);
            param->setHint(name);
            param->setAnimates(true);
            param->setParent(*group);
            //param->setIsSecret(true); // done in the plugin constructor
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        StringParamDescriptor *param = desc.defineStringParam(kParamScript);
        param->setLabel(kParamScriptLabel);
        param->setHint(kParamScriptHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault(kSeExprDefaultScript);
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



void getSeExprPluginID(OFX::PluginFactoryArray &ids)
{
    static SeExprPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
