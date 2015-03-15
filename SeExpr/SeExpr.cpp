/*
 OFX SeExpr plugin.
 Run a shell script.

 Copyright (C) 2014 INRIA
 
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

#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsMerging.h"
#include "ofxsMultiThread.h"

#include <SeExpression.h>
#include <SeExprFunc.h>
#include <SeExprNode.h>
#include <SeMutex.h>

#define kPluginName "SeExpr"
#define kPluginGrouping "Merge"
#define kPluginDescription \
"Use the Walt Disney Animation Studio SeExpr library to write an expression to process pixels of the input image.\n" \
"SeExpr is licensed under the Apache License v2 and is copyright of Disney Enterprises, Inc.\n\n" \
"Some extensions to the language have been developped in order to use it in the purpose of filtering and blending input images. " \
"The following pre-defined variables can be used in the script:\n\n" \
"- $x: This is the canonical X coordinate of the pixel to render (this is not normalized in the [0,1] range)\n" \
"- $y: This is the canonical Y coordinate of the pixel to render (this is not normalized in the [0,1] range)\n" \
"- $frame: This is the current frame being rendered\n" \
"- $output_width: This is the width of the output image being rendered. This is useful to normalize x coordinates into the range [0,1]\n" \
"- $output_height: This is the height of the output image being rendered. This is useful to normalize y coordinates into the range [0,1]\n" \
"- Each input has a variable named $input_width<index> and $input_height<index> indicating respectively the width and height of the input. " \
"For example, the input 1 will have the variables $input_width1 and $input_height1.\n\n" \
"To fetch an input pixel, you must use the getPixel(inputNumber,frame,x,y) function that will for a given input fetch the pixel at the " \
"(x,y) position in the image at the given frame. Note that inputNumber starts from 1.\n\n" \
"Usage example (Application of the Multiply Merge operator on the input 1 and 2):\n\n" \
"$a = getPixel(1,frame,$x,$y);\n" \
"$b = getPixel(2,frame,$x,$y);\n" \
"$color = $a * $b;\n" \
"$color\n" \
"\n\n" \
"Limitations:\n\n" \
"Some optimizations have been made so that the plug-in is efficient even though using an expression language." \
"In order to correctly work with input images, your expression should always call the getPixel(...) function in the " \
"global scope of your expression, e.g:\n" \
"if ($a > 5) {\n" \
"   $b = getPixel(1,frame,$x,$y);\n" \
"} else {\n" \
"   $b = [0,0,0]\n" \
"}\n" \
"This will not produce an efficient program and the plug-in will most likely be very slow to render.\n" \
"A well formed and efficient expression should use the getPixel function in the global scope like this:\n" \
"$c = getPixel(1,frame,$x,$y);\n"\
"if ($a > 5) {\n"\
"   $b = $c\n"\
"} else {\n"\
"   $b = [0,0,0]\n"\
"}"

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
#define kSeExprInputWidthVarName "input_width"
#define kSeExprInputHeightVarName "input_height"
#define kSeExprOutputWidthVarName "output_width"
#define kSeExprOutputHeightVarName "output_height"

#define kSeExprDefaultScript "$color = " kSeExprGetPixelFuncName "(1, frame, x, y);\n#Return the output color\n$color"

#define kBoundingBoxParamName "boundingBox"
#define kBoundingBoxParamLabel "Bounding box"
#define kBoundingBoxParamHint "The region to output"

#define kBoundingBoxChoiceUnion "Union"
#define kBoundingBoxChoiceUnionHelp "The output region will be the union of all connected inputs bounding box"
#define kBoundingBoxChoiceIntersection "Intersection"
#define kBoundingBoxChoiceIntersectionHelp "The output region will be the intersection of all connected inputs bounding box"
#define kBoundingBoxChoiceCustomInput "Input"
#define kBoundingBoxChoiceCustomInputHelp "The output region will be the bounding box of the input"

#define kLayerInputParamName "layerInput"
#define kLayerInputParamLabel "Input Layer"
#define kLayerInputParamHint "Select which layer from the input to use when calling " kSeExprGetPixelFuncName " on the given input."

#define kDoubleParamNumberParamName "noDoubleParams"
#define kDoubleParamNumberParamLabel "Number of scalar parameters"
#define kDoubleParamNumberParamHint "Use this to control how many scalar parameters should be exposed to the SeExpr expression."

#define kDoubleParamName "x"
#define kDoubleParamLabel "X"
#define kDoubleParamHint "A custom 1-dimensional variable that can be referenced in the expression by its script-name, e.g: $x1"

#define kDouble2DParamNumberParamName "noDouble2DParams"
#define kDouble2DParamNumberParamLabel "Number of positional parameters"
#define kDouble2DParamNumberParamHint "Use this to control how many positional parameters should be exposed to the SeExpr expression."

#define kDouble2DParamName "pos"
#define kDouble2DParamLabel "Pos"
#define kDouble2DParamHint "A custom 2-dimensional variable that can be referenced in the expression by its script-name, e.g: $pos1"

#define kColorParamNumberParamName "noColorParams"
#define kColorParamNumberParamLabel "Number of color parameters"
#define kColorParamNumberParamHint "Use this to control how many color parameters should be exposed to the SeExpr expression."

#define kColorParamName "color"
#define kColorParamLabel "Color"
#define kColorParamHint "A custom RGB variable that can be referenced in the expression by its script-name, e.g: $color1"

#define kScriptParamName "script"
#define kScriptParamLabel "Script"
#define kScriptParamHint "Contents of the SeExpr expression. See the description of the plug-in and " \
"http://www.disneyanimation.com/technology/seexpr.html for documentation."

#define kParamValidate                  "validate"
#define kParamValidateLabel             "Validate"
#define kParamValidateHint              "Validate the script contents and execute it on next render. This locks the script and all its parameters."

#define kSeExprColorPlaneName "Color"
#define kSeExprBackwardMotionPlaneName "Backward"
#define kSeExprForwardMotionPlaneName "Forward"
#define kSeExprDisparityLeftPlaneName "DisparityLeft"
#define kSeExprDisparityRightPlaneName "DisparityRight"

static bool gHostIsMultiPlanar ;

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
};

class OFXSeExpression;

int getNComponents(OFX::PixelComponentEnum pixelComps, const std::string& rawComponents) {
    switch (pixelComps) {
        case OFX::ePixelComponentRGBA:
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
    const OFX::Image* _src0CurTime;
    OFX::Image* _dstImg;
    bool _maskInvert;
    const OFX::Image* _maskImg;
    bool _doMasking;
    double _mix;
    
    struct ImageData
    {
        OFX::Image* img;
        int nComponents;
        
        ImageData() : img(0), nComponents(0) {}
    };
    
    
    // <clipIndex, <time, image> >
    typedef std::map<int, std::map<OfxTime, ImageData> > FetchedImagesMap;
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
    

    
    void setValues(OfxTime time, int view, double mix, const std::string& expression, std::string* layers, OfxPointI* inputSizes, OfxPointI outputSize);
    
    bool isExprOk(std::string* error);
    
    OFX::Image* getOrFetchImage(int inputIndex, OfxTime time, int* nComponents) {
        FetchedImagesMap::iterator foundInput = _images.find(inputIndex);
        if (foundInput == _images.end()) {
            OFX::Clip* clip = _plugin->getClip(inputIndex);
            assert(clip);
            if (!clip->isConnected()) {
                return 0;
            }
            ImageData data;
            if (gHostIsMultiPlanar) {
                if (_layersToFetch[inputIndex].empty()) {
                    data.img = 0;
                } else {
                    data.img = clip->fetchImagePlane(time, _renderView,  _layersToFetch[inputIndex].c_str());
                }
            } else {
                data.img = clip->fetchImage(time);
            }
            if (!data.img) {
                return 0;
            }
            data.nComponents = getNComponents(data.img->getPixelComponents(), data.img->getPixelComponentsProperty());
            *nComponents = data.nComponents;
            std::map<OfxTime, ImageData> imgMap;
            imgMap.insert(std::make_pair(time, data));
            std::pair<FetchedImagesMap::iterator,bool> ret = _images.insert(std::make_pair(inputIndex, imgMap));
            assert(ret.second);
            return data.img;
        } else {
            std::map<OfxTime, ImageData>::iterator foundImage = foundInput->second.find(time);
            if (foundImage == foundInput->second.end()) {
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
                std::pair<std::map<OfxTime, ImageData>::iterator, bool> ret = foundInput->second.insert(std::make_pair(time, data));
                assert(ret.second);
                return data.img;
            } else {
                *nComponents = foundImage->second.nComponents;
                return foundImage->second.img;
            }
        }
    }
    
    virtual void process(OfxRectI procWindow) = 0;
};


template <typename PIX, int maxComps>
void getPixInternal(int nComps, void* data, SeVec3d& result) {
    PIX* pix = (PIX*)data;
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
                std::stringstream ss;
                ss << "Argument " << i + 1 << " should be an integer.";
                node->addError(ss.str());
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
        OFX::Image* img = _processor->getOrFetchImage(inputIndex[0] - 1, frame[0], &nComponents);
        if (!img || nComponents == 0) {
            result[0] = result[1] = result[2] = 0.;
        } else {
            void* data = img->getPixelAddress(xCoord[0], yCoord[0]);
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

class GetPixelFunc : public SeExprFunc {
    
public:
    
    GetPixelFunc(SeExprFuncX& f, int minargs, int maxargs)
    : SeExprFunc(f, GetPixelFuncX::numArgs() , GetPixelFuncX::numArgs()) {
        
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
    virtual void eval(const SeExprVarNode* node, SeVec3d& result)
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
    virtual void eval(const SeExprVarNode* node, SeVec3d& result)
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
    virtual void eval(const SeExprVarNode* node, SeVec3d& result)
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
   
    virtual void eval(const SeExprVarNode* node, SeVec3d& result)
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
    
    virtual void eval(const SeExprVarNode* node, SeVec3d& result)
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

/**
 * @brief Used to determine what are the frames needed and RoIs of the expression
 **/
class StubSeExpression : public SeExpression
{
    
    
    mutable SimpleScalar _stubScalar;
    mutable StubGetPixelFuncX _getPix;
    mutable GetPixelFunc _getPixFunction;
    mutable SimpleScalar _currentTime;
    mutable SimpleScalar _xCoord,_yCoord;

    typedef std::map<int,std::vector<OfxTime> > FramesNeeded;
    mutable FramesNeeded _images;
    
    typedef std::map<int, OfxRectI> RoIsMap;
    mutable RoIsMap _rois;
    
public:
    
    StubSeExpression(const std::string& expr, OfxTime time);
    
    virtual ~StubSeExpression();
    
    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const std::string& name) const OVERRIDE FINAL;
    
    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const std::string& name) const OVERRIDE FINAL;
    
    /** NOT MT-SAFE, this object is to be used PER-THREAD*/
    void setXY(int x, int y) {
        _xCoord._value = x;
        _yCoord._value = y;
    }
    
    void onGetPixelCalled(int inputIndex, OfxTime time, int x, int y) {
        
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
        {
            //Register RoI
            RoIsMap::iterator foundInput = _rois.find(inputIndex);
            if (foundInput == _rois.end()) {
                OfxRectI roiPixel;
                roiPixel.x1 = x;
                roiPixel.x2 = x + 1;
                roiPixel.y1 = y;
                roiPixel.y2 = y + 1;
                _rois.insert(std::make_pair(inputIndex, roiPixel));
            } else {
                foundInput->second.x1 = std::min(foundInput->second.x1, x);
                foundInput->second.x2 = std::max(foundInput->second.x2, x + 1);
                foundInput->second.y1 = std::min(foundInput->second.y1, x);
                foundInput->second.y2 = std::max(foundInput->second.y2, x + 1);
            }
            
        }
    }
    
    const std::map<int,std::vector<OfxTime> >& getFramesNeeded() const
    {
        return _images;
    }
    
    const std::map<int, OfxRectI> & getRoIs() const
    {
        return _rois;
    }
};

class OFXSeExpression : public SeExpression
{
    mutable GetPixelFuncX _getPix;
    mutable GetPixelFunc _getPixFunction;
    
    DoubleParamVarRef* _doubleRefs[kParamsCount];
    Double2DParamVarRef* _double2DRefs[kParamsCount];
    ColorParamVarRef* _colorRefs[kParamsCount];
    
    mutable SimpleScalar _currentTime;
    mutable SimpleScalar _xCoord,_yCoord;
    
    mutable SimpleScalar _inputWidths[kSourceClipCount];
    mutable SimpleScalar _inputHeights[kSourceClipCount];
    
    mutable SimpleScalar _outputWidth,_outputHeight;
    
public:
    
    
    
    
    OFXSeExpression(SeExprProcessorBase* processor,const std::string& expr, OfxTime time);
    
    virtual ~OFXSeExpression();
    
    /** override resolveVar to add external variables */
    virtual SeExprVarRef* resolveVar(const std::string& name) const OVERRIDE FINAL;
    
    /** override resolveFunc to add external functions */
    virtual SeExprFunc* resolveFunc(const std::string& name) const OVERRIDE FINAL;
    
    /** NOT MT-SAFE, this object is to be used PER-THREAD*/
    void setXY(int x, int y) {
        _xCoord._value = x;
        _yCoord._value = y;
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

OFXSeExpression::OFXSeExpression( SeExprProcessorBase* processor, const std::string& expr, OfxTime time)
: SeExpression(expr)
, _getPix(processor)
, _getPixFunction(_getPix, 4, 4)
, _doubleRefs()
, _double2DRefs()
, _colorRefs()
, _currentTime()
, _xCoord()
, _yCoord()
{
    _currentTime._value = time;
    
    assert(processor);
    SeExprPlugin* plugin = processor->getPlugin();
    
    OFX::DoubleParam** doubleParams = plugin->getDoubleParams();
    OFX::Double2DParam** double2DParams = plugin->getDouble2DParams();
    OFX::RGBParam** colorParams = plugin->getRGBParams();
    
    for (int i = 0; i < kParamsCount; ++i) {
        _doubleRefs[i] = new DoubleParamVarRef(doubleParams[i]);
        _double2DRefs[i] = new Double2DParamVarRef(double2DParams[i]);
        _colorRefs[i] = new ColorParamVarRef(colorParams[i]);
    }
    
}

OFXSeExpression::~OFXSeExpression()
{
    for (int i = 0; i < kParamsCount; ++i) {
        delete _doubleRefs[i];
        delete _double2DRefs[i];
        delete _colorRefs[i];
    }
}

SeExprVarRef*
OFXSeExpression::resolveVar(const std::string& name) const
{
    if (name == kSeExprCurrentTimeVarName) {
        return &_currentTime;
    } else if (name == kSeExprXCoordVarName) {
        return &_xCoord;
    } else if (name == kSeExprYCoordVarName) {
        return &_yCoord;
    } else if (name == kSeExprOutputWidthVarName) {
        return &_outputWidth;
    } else if (name == kSeExprOutputHeightVarName) {
        return &_outputHeight;
    }
    
    for (int i = 0; i < kParamsCount; ++i) {
        {
            std::stringstream ss;
            ss << kDoubleParamName << i+1;
            if (ss.str() == name) {
                return _doubleRefs[i];
            }
        }
        {
            std::stringstream ss;
            ss << kDouble2DParamName << i+1;
            if (ss.str() == name) {
                return _double2DRefs[i];
            }
        }
        {
            std::stringstream ss;
            ss << kColorParamName << i+1;
            if (ss.str() == name) {
                return _colorRefs[i];
            }
        }
        {
            std::stringstream ss;
            ss << kSeExprInputWidthVarName << i+1;
            if (ss.str() == name) {
                return &_inputWidths[i];
            }
        }
        {
            std::stringstream ss;
            ss << kSeExprInputHeightVarName << i+1;
            if (ss.str() == name) {
                return &_inputHeights[i];
            }
        }
        
        
    }
    return 0;
}


SeExprFunc* OFXSeExpression::resolveFunc(const std::string& name) const
{
    // check if it is builtin so we get proper behavior
    if (SeExprFunc::lookup(name)) {
        return 0;
    }
    
    if (name == kSeExprGetPixelFuncName) {
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
            std::stringstream ss;
            ss << "Argument " << i + 1 << " should be an integer.";
            node->addError(ss.str());
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
    
    SeVec3d xCoord;
    node->child(2)->eval(xCoord);
    
    SeVec3d yCoord;
    node->child(3)->eval(yCoord);
    
    _expr->onGetPixelCalled(inputIndex[0] - 1, frame[0], xCoord[0], yCoord[0]);
    
}

StubSeExpression::StubSeExpression(const std::string& expr, OfxTime time)
: SeExpression(expr)
, _stubScalar()
, _getPix(this)
, _getPixFunction(_getPix, 4, 4)
, _currentTime()
, _xCoord()
, _yCoord()
{
    
}

StubSeExpression::~StubSeExpression()
{
    
}

/** override resolveVar to add external variables */
SeExprVarRef*
StubSeExpression::resolveVar(const std::string& name) const
{
    if (name == kSeExprCurrentTimeVarName) {
        return &_currentTime;
    } else if (name == kSeExprXCoordVarName) {
        return &_xCoord;
    } else if (name == kSeExprYCoordVarName) {
        return &_yCoord;
    }
    return &_stubScalar;
}

/** override resolveFunc to add external functions */
SeExprFunc*
StubSeExpression::resolveFunc(const std::string& name) const
{
    // check if it is builtin so we get proper behavior
    if (SeExprFunc::lookup(name)) {
        return 0;
    }
    if (name == kSeExprGetPixelFuncName) {
        return &_getPixFunction;
    }
    return 0;
}

SeExprProcessorBase::SeExprProcessorBase(SeExprPlugin* instance)
: _renderTime(0)
, _renderView(0)
, _plugin(instance)
, _expression(0)
, _src0CurTime(0)
, _dstImg(0)
, _maskInvert(false)
, _maskImg(0)
, _doMasking(false)
, _mix(0.)
, _images()
{
}

SeExprProcessorBase::~SeExprProcessorBase()
{
    delete _expression;
    for (FetchedImagesMap::iterator it = _images.begin(); it!=_images.end(); ++it) {
        for (std::map<OfxTime, ImageData> ::iterator it2 = it->second.begin(); it2!= it->second.end(); ++it2) {
            delete it2->second.img;
        }
    }
}

void
SeExprProcessorBase::setValues(OfxTime time, int view, double mix, const std::string& expression, std::string* layers,OfxPointI* inputSizes, OfxPointI outputSize)
{
    _renderTime = time;
    _renderView = view;
    _expression = new OFXSeExpression(this, expression, time);
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
    int nComps;
    _src0CurTime = getOrFetchImage(0, _renderTime, &nComps);
    
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
        bool fetchBgImage = _src0CurTime && (_doMasking || _mix != 1.);
        
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_plugin->abort()) {
                break;
            }
        
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                const PIX* srcPix0 = fetchBgImage ? (const PIX*) _src0CurTime->getPixelAddress(x, y) : 0;
                
                _expression->setXY(x, y);
                SeVec3d result = _expression->evaluate();
                
                for (int k = 0; k < nComponents; ++k) {
                    if (k < 3) {
                        tmpPix[k] = result[k];
                    } else {
                        tmpPix[k] = 0.;
                    }
                }
                
                OFX::ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix0, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

SeExprPlugin::SeExprPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
{
    for (int i = 0; i < kSourceClipCount; ++i) {
        if (i == 0 && getContext() == OFX::eContextFilter) {
            _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            std::stringstream s;
            s << i+1;
            _srcClip[i] = fetchClip(s.str());
        }
    }
    
    _maskClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
    assert(!_maskClip || _maskClip->getPixelComponents() == OFX::ePixelComponentAlpha);
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);

    _doubleParamCount = fetchIntParam(kDoubleParamNumberParamName);
    assert(_doubleParamCount);
    _double2DParamCount = fetchIntParam(kDouble2DParamNumberParamName);
    assert(_double2DParamCount);
    _colorParamCount = fetchIntParam(kColorParamNumberParamName);
    assert(_colorParamCount);

    for (int i = 0; i < kParamsCount; ++i) {
        if (gHostIsMultiPlanar) {
            std::stringstream ss;
            ss << kLayerInputParamName << i+1;
            _clipLayerToFetch[i] = fetchChoiceParam(ss.str());
        } else {
            _clipLayerToFetch[i] = 0;
        }
        {
            std::stringstream ss;
            ss << kDoubleParamName << i+1;
            _doubleParams[i] = fetchDoubleParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kDouble2DParamName << i+1;
            _double2DParams[i] = fetchDouble2DParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kColorParamName << i+1;
            _colorParams[i] = fetchRGBParam(ss.str());
        }
    }
    _script = fetchStringParam(kScriptParamName);
    assert(_script);
    _validate = fetchBooleanParam(kParamValidate);
    assert(_validate);
    
    _mix = fetchDoubleParam(kParamMix);
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);

    _boundingBox = fetchChoiceParam(kBoundingBoxParamName);
    assert(_boundingBox);

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
    
    processor.setValues(args.time, args.viewsToRender, mix, script, inputLayers, inputSizes, outputSize);
    
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
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    bool validated;
    _validate->getValue(validated);
    if (!validated) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
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

    if (paramName == kDoubleParamNumberParamName) {
        int numVisible;
        _doubleParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _doubleParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kDouble2DParamNumberParamName) {
        int numVisible;
        _double2DParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _double2DParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kColorParamNumberParamName) {
        int numVisible;
        _colorParamCount->getValue(numVisible);
        assert(numVisible <= kParamsCount && numVisible >=0);
        for (int i = 0; i < kParamsCount; ++i) {
            bool visible = i < numVisible;
            _colorParams[i]->setIsSecret(!visible);
        }
    } else if (paramName == kParamValidate) {
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

}

void
SeExprPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (!gHostIsMultiPlanar) {
        return;
    }
    for (int i = 0; i < kSourceClipCount; ++i) {
        std::stringstream ss;
        ss << i+1;
        if (ss.str() == clipName) {
            assert(_srcClip[i]);
            _clipLayerToFetch[i]->setIsSecret(!_srcClip[i]->isConnected());
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
    const std::map<int, std::vector<OfxTime > >& framesNeeded = expr.getFramesNeeded();
    for (std::map<int, std::vector<OfxTime > >::const_iterator it = framesNeeded.begin(); it != framesNeeded.end(); ++it) {
        
        assert(it->first >= 0  && it->first < kSourceClipCount);
        OFX::Clip* clip = getClip(it->first);
        assert(clip);
        
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            OfxRangeD range;
            range.min = range.max = it->second[i];
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
        
        double par = _srcClip[0]->getPixelAspectRatio();
        
        OfxRectI originalRoIPixel;
        OFX::MergeImages2D::toPixelEnclosing(args.regionOfInterest, args.renderScale, par, &originalRoIPixel);
        
        expr.setXY(originalRoIPixel.x1, originalRoIPixel.y1);
        (void)expr.evaluate();
        
        expr.setXY(originalRoIPixel.x1, originalRoIPixel.y2 - 1);
        (void)expr.evaluate();

        expr.setXY(originalRoIPixel.x2 - 1, originalRoIPixel.y2 - 1);
        (void)expr.evaluate();

        expr.setXY(originalRoIPixel.x2 - 1, originalRoIPixel.y1);
        (void)expr.evaluate();
        
        
        bool clipSet[kSourceClipCount];
        for (int i = 0; i < kSourceClipCount; ++i) {
            clipSet[i] = false;
        }

        const std::map<int,OfxRectI>& roisMap = expr.getRoIs();
        for (std::map<int,OfxRectI>::const_iterator it = roisMap.begin(); it != roisMap.end(); ++it) {
            assert(it->first >= 0 && it->first < kSourceClipCount);
            OFX::Clip* clip = getClip(it->first);
            assert(clip);
            
            clipSet[it->first] = true;
            
            OfxRectD roi;
            OFX::MergeImages2D::toCanonical(it->second, args.renderScale, par, &roi);
            rois.setRegionOfInterest(*clip, roi);
        }
        
        bool isMasked = _maskClip ? _maskClip->isConnected() : false;
        
        for (int i = 0; i < kSourceClipCount; ++i) {
            
            if (i == 0 && isMasked) {
                //Make sure the first input is fetched on all the render window if we are using a mask
                rois.setRegionOfInterest(*_srcClip[0], args.regionOfInterest);
            }
            
            if (!clipSet) {
                
                OFX::Clip* clip = getClip(i);
                assert(clip);
                //Set an empty roi if the user didn't specify any interest for this input.
                OfxRectD emptyRoI;
                emptyRoI.x1 = emptyRoI.y1 = emptyRoI.x2 = emptyRoI.y2 = 0.;
                rois.setRegionOfInterest(*clip, emptyRoI);
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
                    rodSet = true;
                    rod = srcRod;
                }
                rodSet = true;
            }
        }
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
}

void SeExprPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kSourceClipCount; ++i) {
        std::stringstream s;
        s << i+1;
        ClipDescriptor *srcClip;
        if (i == 0 && context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName); // mandatory clip for the filter context
        } else {
            srcClip = desc.defineClip(s.str());
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
    dstClip->setSupportsTiles(false);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kBoundingBoxParamName);
        param->setLabel(kBoundingBoxParamLabel);
        param->setHint(kBoundingBoxParamHint);
        param->appendOption(kBoundingBoxChoiceUnion, kBoundingBoxChoiceUnionHelp);
        param->appendOption(kBoundingBoxChoiceIntersection, kBoundingBoxChoiceIntersectionHelp);
        for (int i = 0; i < kSourceClipCount; ++i) {
            
            std::stringstream opt;
            opt << kBoundingBoxChoiceCustomInput << i+1;
            std::stringstream help;
            help << kBoundingBoxChoiceCustomInputHelp << i+1;
            param->appendOption(opt.str(), help.str());
        }
        param->setAnimates(false);
        page->addChild(*param);
    }

    if (gHostIsMultiPlanar) {
        GroupParamDescriptor *group = desc.defineGroupParam("Input layers");
        group->setLayoutHint(OFX::eLayoutHintDivider);
        group->setLabel("Input layers");
        group->setOpen(false);
        page->addChild(*group);

        for (int i = 0; i < kSourceClipCount; ++i) {
            
            std::stringstream s;
            s << kLayerInputParamName << i+1;
            ChoiceParamDescriptor *param = desc.defineChoiceParam(s.str());
            
            std::stringstream ss;
            ss << kLayerInputParamLabel << i+1;
            param->setLabel(ss.str());
            param->setAnimates(false);
            param->appendOption(kSeExprColorPlaneName);
            param->setIsSecret(true);
            param->setParent(*group);
            page->addChild(*param);
        }
    }
    
    {
        GroupParamDescriptor *group = desc.defineGroupParam("Scalar variables");
        group->setLabel("Scalar variables");
        group->setLayoutHint(OFX::eLayoutHintDivider);
        group->setOpen(false);
        page->addChild(*group);
        
        IntParamDescriptor* numParam = desc.defineIntParam(kDoubleParamNumberParamName);
        numParam->setLabel(kDoubleParamNumberParamLabel);
        numParam->setHint(kDoubleParamNumberParamHint);
        numParam->setRange(0, kParamsCount);
        numParam->setDefault(0);
        numParam->setAnimates(false);
        numParam->setParent(*group);
        page->addChild(*numParam);
        
        for (int i = 0; i < kSourceClipCount; ++i) {
            
            std::stringstream s;
            s << kDoubleParamName << i+1;
            DoubleParamDescriptor *param = desc.defineDoubleParam(s.str());
            
            std::stringstream ss;
            ss << kDoubleParamLabel << i+1;
            
            param->setLabel(ss.str());
            param->setHint(kDoubleParamHint);
            param->setAnimates(true);
            param->setIsSecret(true);
            param->setDoubleType(OFX::eDoubleTypePlain);
            param->setParent(*group);
            page->addChild(*param);
        }
    }
    
    {
        GroupParamDescriptor *group = desc.defineGroupParam("Position variables");
        group->setLabel("Position variables");
        group->setLayoutHint(OFX::eLayoutHintDivider);
        group->setOpen(false);
        page->addChild(*group);
        
        IntParamDescriptor* numParam = desc.defineIntParam(kDouble2DParamNumberParamName);
        numParam->setLabel(kDouble2DParamNumberParamLabel);
        numParam->setHint(kDouble2DParamNumberParamHint);
        numParam->setRange(0, kParamsCount);
        numParam->setDefault(0);
        numParam->setAnimates(false);
        numParam->setParent(*group);
        page->addChild(*numParam);
        
        for (int i = 0; i < kSourceClipCount; ++i) {
            
            std::stringstream s;
            s << kDouble2DParamName << i+1;
            Double2DParamDescriptor *param = desc.defineDouble2DParam(s.str());
            
            std::stringstream ss;
            ss << kDouble2DParamLabel << i+1;
            
            param->setLabel(ss.str());
            param->setHint(kDouble2DParamHint);
            param->setAnimates(true);
            param->setIsSecret(true);
            param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
            param->setParent(*group);
            page->addChild(*param);
        }
    }
    
    {
        GroupParamDescriptor *group = desc.defineGroupParam("Color variables");
        group->setLabel("Color variables");
        group->setLayoutHint(OFX::eLayoutHintDivider);
        group->setOpen(false);
        page->addChild(*group);
        
        IntParamDescriptor* numParam = desc.defineIntParam(kColorParamNumberParamName);
        numParam->setLabel(kColorParamNumberParamLabel);
        numParam->setHint(kColorParamNumberParamHint);
        numParam->setRange(0, kParamsCount);
        numParam->setDefault(0);
        numParam->setAnimates(false);
        numParam->setParent(*group);
        page->addChild(*numParam);
        
        for (int i = 0; i < kSourceClipCount; ++i) {
            
            std::stringstream s;
            s << kColorParamName << i+1;
            RGBParamDescriptor *param = desc.defineRGBParam(s.str());
            
            std::stringstream ss;
            ss << kColorParamLabel << i+1;
            
            param->setLabel(ss.str());
            param->setHint(kColorParamHint);
            param->setAnimates(true);
            param->setParent(*group);
            param->setIsSecret(true);
            page->addChild(*param);
        }
    }
    
    {
        StringParamDescriptor *param = desc.defineStringParam(kScriptParamName);
        param->setLabel(kScriptParamLabel);
        param->setHint(kScriptParamHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault(kSeExprDefaultScript);
        page->addChild(*param);
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabel(kParamValidateLabel);
        param->setHint(kParamValidateHint);
        param->setEvaluateOnChange(true);
        page->addChild(*param);
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
