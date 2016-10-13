/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX CTL plugin.
 */

#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
//#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsMacros.h"

GCC_DIAG_OFF(unused-parameter)
#include <CtlRcPtr.h>
#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>
GCC_DIAG_ON(unused-parameter)

#include "Iex.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "IOUtility.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "CTLTransform"
#define kPluginGrouping "Color"
#define kPluginDescription \
"Apply a color transform written using the Color Transformation Language (CTL)\n" \
"\n" \
"The Color Transformation Language, or CTL, is a small programming language that has been designed to serve as a building block for digital color management systems. CTL allows users to describe color transforms in a concise and unambiguous way by expressing them as programs.\n" \
"\n" \
"Color transforms can be shared by distributing CTL programs. Two parties with the same CTL program can apply the same transform to an image. In addition to the original image, a CTL program can have input parameters whose settings affect how the input image will be transformed. For example, a transform may have an 'exposure' parameter, such that changing the exposure makes the image brighter or darker. In order to guarantee identical results, parties that have agreed to use a particular transform must also agree on the settings for the transform's parameters.\n" \
"\n" \
"A domain-specific programming language such as CTL can be designed to allow only the kinds of operations that are needed to describe color transforms. This improves the portability of programs, protects users against application software crashes and malicious code, and permits efficient interpreter implementations."

#define kPluginIdentifier "fr.inria.CTLTransform"

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

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamChooseInput "input"
#define kParamChooseInputLabel "Input"
#define kParamChooseInputOptionCode "Code"
#define kParamChooseInputOptionFile "File"
enum ChooseInputEnum
{
    eChooseInputCode = 0,
    eChooseInputFile,
};

#define kParamValidate                  "validate"
#define kParamValidateLabel             "Validate"
#define kParamValidateHint              "Validate the script contents and execute it on next render. This locks the script and all its parameters."

#define kNukeWarnTcl "On Nuke, the characters '$', '[' ']' must be preceded with a backslash (as '\\$', '\\[', '\\]') to avoid TCL variable and expression substitution."

#define kParamCTLCode "code"
#define kParamCTLCodeLabel "CTL Code"
#define kParamCTLCodeHint "Your CTL code."

#define kParamShowScript "showScript"
#define kParamShowScriptLabel "Show CTL Code"
#define kParamShowScriptHint "Show the contents of the CTL code as seen by the CTL interpreter in a dialog window. It may be different from the script visible in the GUI, because the host may perform variable or expression substitution on the RGB script parameter."

#define kParamFilename "filename"
#define kParamFilenameLabel "CTL File Name"
#define kParamFilenameHint "CTL source code file."

using std::string;
using std::vector;
using std::istringstream;
using std::ostringstream;
using std::exception;

using namespace Iex;
using namespace Ctl;

static bool gHostIsNatron = false;

// These two are from http://stackoverflow.com/questions/236129/how-to-split-a-string-in-c
vector<string>&
split(const string &s, char delim, vector<string> &elems)
{
    istringstream ss(s);
    string item;
    while (getline(ss, item, delim) && !item.empty()) {
        elems.push_back(item);
    }
    return elems;
}

vector<string>
split(const string &s, char delim)
{
    vector<string> elems;
    split(s, delim, elems);
    return elems;
}

static std::string
trim(std::string const & str)
{
    const std::string whitespace = " \t\f\v\n\r";
    std::size_t first = str.find_first_not_of(whitespace);

    // If there is no non-whitespace character, both first and last will be std::string::npos (-1)
    // There is no point in checking both, since if either doesn't work, the
    // other won't work, either.
    if (first == std::string::npos) {
        return "";
    }

    std::size_t last  = str.find_last_not_of(whitespace);

    return str.substr(first, last - first + 1);
}

static std::string
removeExtension(const std::string& filename)
{
    std::string::const_reverse_iterator pivot = std::find( filename.rbegin(), filename.rend(), '.' );
    if (pivot == filename.rend()) {
        return "";
    }
    return string( filename.begin(), pivot.base() );
}

class RcSimdInterpreter : public Ctl::SimdInterpreter, public Ctl::RcObject
{
};

typedef Ctl::RcPtr<RcSimdInterpreter> SimdInterpreterPtr;

class Transform {
    friend class TransformFriend;
public:
    Transform(const std::string &modulePath,
              const std::string &transformPath);

    //void execute(const DD::Image::Row &in, int l, int r, DD::Image::Row &out);
private:
    static const std::vector<std::string> parseModulePath(const std::string &modulePath);

    static void verifyModuleName(const std::string &moduleName);

    static bool matchesCTLCannotFindFunctionExceptionText(const std::exception& e, const std::string &functionName);

    static bool matchesCTLCannotFindModuleExceptionText(const std::exception& e);

    static const std::string missingModuleFromException(const std::exception& e);

    const std::string topLevelFunctionNameInTransform();

    void loadArgMap();

    std::vector<std::string>  modulePathComponents_;

    // Keeping this in the heap makes it possible to share it between the Transform
    // and TransformFriend (which gets used in unit testing).
    SimdInterpreterPtr        interpreter_;

    // On the other hand, you do NOT want to try anything comparable for sharing
    // functional calls with Ctl::FunctionCallPtr. That sort of thing will work in
    // unit testing, which is single-threaded, but Nuke has multiple threads sharing
    // from a single Nuke Op.

    // transformPath_ is not used at runtime but is handy for forensics
    std::string               transformPath_;
    std::string               functionName_;

    // Make this private, and don't implement them - that way we know that the compiler
    // isn't doing anything odd.
    Transform(const Transform &transform);

    // Make this private, and don't implement them - that way we know that the compiler
    // isn't doing anything odd.
    Transform&
    operator=(const Transform &rhs);

};

const vector<string>
Transform::parseModulePath(const string &modulePath)
{
    vector<string> modulePathComponents;
    try {
        modulePathComponents = split(modulePath,':');
    } catch (const ArgExc &e) {
        THROW(ArgExc, "Cannot parse module path '" << modulePath << "': " << e.what());
    }
    return modulePathComponents;
}

void
Transform::verifyModuleName(const string &moduleName)
{
    // components of path need not exist, they just need to be syntactically legal. At least, that's as far as Ctl::Interpreter goes.
    // path should be legal, as far as the interpreter allows; that means no /, :. ; or \ characters.
    if (moduleName.find_first_of("/:;\\") != string::npos)
    {
        THROW(ArgExc, "Module path component `" << moduleName << "' contains invalid characters (one of /, :, ; or \\");
    }
}

bool
Transform::matchesCTLCannotFindFunctionExceptionText(const exception& e, const string &functionName)
{
    ostringstream s;
    s << "Cannot find CTL function " << functionName << ".";
    string pattern(s.str());
    return pattern == e.what();
}

bool
Transform::matchesCTLCannotFindModuleExceptionText(const exception& e)
{
    string exceptionText(e.what());
    string::size_type i = exceptionText.find_first_of("Cannot find CTL module \"");
    string::size_type j = exceptionText.find_last_of("\"");
    return j != string::npos && i < j;
}

const string
Transform::missingModuleFromException(const exception& e)
{
    if (! matchesCTLCannotFindModuleExceptionText(e)) {
        THROW(LogicExc, "Attempt to extract missing module name from an exception not concerned with missing modules");
    }
    string exceptionText(e.what());
    string::size_type i = exceptionText.find_first_not_of("Cannot find CTL module \"");
    string::size_type j = exceptionText.find("\".");
    return exceptionText.substr(i, j-i);
}

const string
Transform::topLevelFunctionNameInTransform()
{
    FunctionCallPtr functionCall;
    try {
        functionCall = interpreter_->newFunctionCall("main");
    } catch (const ArgExc &e) {
        // There is no CTL exception specific to this problem, so we use secret knowledge (i.e. we peek at the source)
        // to see exactly what the CTL interpreter would do if the module cannot be found. And what it does is throw
        // ArgExc with the what() string having the form "Cannot find CTL function <foo>."
        if (matchesCTLCannotFindFunctionExceptionText(e, "main")) {
            string moduleName(removeExtension(OFX::IO::basename(transformPath_)));
            try {
                functionCall = interpreter_->newFunctionCall(moduleName);
                return moduleName;
            } catch (const exception &e) {
                if (matchesCTLCannotFindFunctionExceptionText(e, moduleName)) {
                    THROW(ArgExc, "CTL file at '" << transformPath_ << "' has neither a main function nor one named '" << moduleName << "'");
                } else if (matchesCTLCannotFindModuleExceptionText(e)) {
                    string missingModule(missingModuleFromException(e));
                    THROW(ArgExc, "Module '" << missingModule << "' not in the module path; referenced by " << moduleName << " function in CTL file '" << transformPath_ << "'");
                } else {
                    THROW(ArgExc, "Error searching for function 'main' and function '" << moduleName << "' in CTL file '" << transformPath_ << "': " << e.what());
                }
            }
        } else if (matchesCTLCannotFindModuleExceptionText(e)) {
            string missingModule(missingModuleFromException(e));
            THROW(ArgExc, "Module '" << missingModule << "' not in the module path; referenced by main function in CTL file '" << transformPath_ << "'");
        } else {
            THROW(ArgExc, "Error searching for function 'main' in CTL file '" << transformPath_ << "': " << e.what());
        }
    }
    return "main";
}


Transform::Transform(const string &modulePath,
                     const string &transformPath)
: modulePathComponents_(parseModulePath(modulePath)),
interpreter_(RcPtr<RcSimdInterpreter>(new RcSimdInterpreter)),
transformPath_(transformPath)
{
    // be diligent about not having bad parameters or state crash all of Nuke.
    try {
        interpreter_->setUserModulePath(modulePathComponents_, modulePathComponents_.size() > 0);
    } catch (const BaseExc& e) {
        THROW(ArgExc, "error setting CTL module path `" << modulePath << "': " << e.what());
    }
    try {
        interpreter_->loadFile(transformPath_);
        //interpreter_->loadModule("", "", _params._code);
        //interpreter_->loadFile(_params._filename, _params._module);
        try {
            functionName_ = topLevelFunctionNameInTransform();
        } catch (const BaseExc& e) {
            THROW(ArgExc, "error finding top-level function name in transform at path 1" << transformPath_ << "': " << e.what());
        }
    } catch (const BaseExc& e) {
        THROW(ArgExc, "error loading CTL transform from path `" << transformPath_ << "': " << e.what());
    }
}

//ChooseInputEnum inputType,
//const std::vector<std::string> &paths,
//const std::string &filename,
//const std::string &module,
//const std::string &code,
/*
void
Transform::execute(const Row &in, int l, int r, Row &out)
{
    // Although it doubtless looks tempting to create the function call and argument map
    // just once, at transform ctor time, and avoid the expense on each call to this
    // execute() function...you can't. As per page 17 of the CTL manual (24/07/2007 edition)
    // function call objects are not thread-safe. Interpreters (or at least the reference
    // SIMD interpreter) ARE thread-safe, so it's cool to stash an interpreter as CtlTransform
    // member variable and share it...but stay away from FunctionCallPtr member variables in
    // CtlTransform objects, and since they point into such objects, from ArgMap member
    // variables as well.

    try {
        FunctionCallPtr fn(interpreter_->newFunctionCall(functionName_));
        if (fn->returnValue()->type().cast<Ctl::VoidType>().refcount() == 0)
        {
            THROW(ArgExc, "top-level function of CTL file at '" << transformPath_
                  << "' returns other than void type");
        }
        try {
            ChanArgMap argMap(fn);
            for (int x = l; x < r;)
            {
                int maxSamples = static_cast<int>(interpreter_->maxSamples());
                int chunkSize = min(r - x, maxSamples);
                argMap.copyInputRowToArgData(in, x, x + chunkSize);
                fn->callFunction(chunkSize);
                argMap.copyArgDataToOutputRow(x, x + chunkSize, out);
                x += chunkSize;
            }
        }
        catch (const BaseExc& e)
        {
            THROW(LogicExc, "could not construct argument map for CTL transform loaded from path `" << transformPath_ << ": " << e.what());
        }
    }
    catch (const BaseExc& e)
    {
        THROW(ArgExc, "error finding top-level function in CTL transform loaded from path `" << transformPath_ << ": " << e.what());
    }
}
*/
using namespace OFX;

class CTLProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    ChooseInputEnum _inputType;
    std::vector<std::string> _paths;
    std::string _filename;
    std::string _module;
    std::string _code;

    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;


public:
    CTLProcessorBase(OFX::ImageEffect &instance,
                                const OFX::RenderArguments & /*args*/)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        // TODO: initialize plugin parameter values
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v; }

    void setMaskImg(const OFX::Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(ChooseInputEnum inputType,
                   const std::vector<std::string> &paths,
                   const std::string &filename,
                   const std::string &module,
                   const std::string &code,
                   bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA
                   // TODO: add plugin parameters
                   )
    {
        _inputType = inputType;
        _paths = paths;
        _filename = filename;
        _module = module;
        _code = code;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        // TODO: set plugin parameter values
    }
};


template <class PIX, int nComponents, int maxValue>
class CTLProcessor
    : public CTLProcessorBase
{
public:
    CTLProcessor(OFX::ImageEffect &instance,
                            const OFX::RenderArguments &args)
        : CTLProcessorBase(instance, args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        const bool a = _processA && (nComponents == 1 || nComponents == 4);
        if (r) {
            if (g) {
                if (b) {
                    if (a) {
                        return process<true, true, true, true >(procWindow); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:


    template<bool processR, bool processG, bool processB, bool processA>
    void process(OfxRectI procWindow)
    {
        // Although it doubtless looks tempting to create the function call and argument map
        // just once, at transform ctor time, and avoid the expense on each call to this
        // execute() function...you can't. As per page 17 of the CTL manual (24/07/2007 edition)
        // function call objects are not thread-safe. Interpreters (or at least the reference
        // SIMD interpreter) ARE thread-safe, so it's cool to stash an interpreter as CtlTransform
        // member variable and share it...but stay away from FunctionCallPtr member variables in
        // CtlTransform objects, and since they point into such objects, from ArgMap member
        // variables as well.

        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                double t_r = unpPix[0];
                double t_g = unpPix[1];
                double t_b = unpPix[2];
                double t_a = unpPix[3];

                // TODO: process the pixel (the actual computation goes here)
                t_r = 1. - t_r;
                t_g = 1. - t_g;
                t_b = 1. - t_b;

                tmpPix[0] = (float)t_r;
                tmpPix[1] = (float)t_g;
                tmpPix[2] = (float)t_b;
                tmpPix[3] = (float)t_a;
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!processA) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if ( (nComponents == 3) || (nComponents == 4) ) {
                    if (!processR) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                    if (!processG) {
                        dstPix[1] = srcPix ? srcPix[1] : PIX();
                    }
                    if (!processB) {
                        dstPix[2] = srcPix ? srcPix[2] : PIX();
                    }
                    if ( !processA && (nComponents == 4) ) {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CTLPlugin
    : public OFX::ImageEffect
{
public:

    /** @brief ctor */
    CTLPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _input(0)
        , _code(0)
        , _showScript(0)
        , _validate(0)
        , _premult(0)
        , _premultChannel(0)
        , _mix(0)
        , _maskApply(0)
        , _maskInvert(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (/*_dstClip->getPixelComponents() == ePixelComponentRGB ||*/
                             _dstClip->getPixelComponents() == ePixelComponentRGBA/* ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha*/) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (/*_srcClip->getPixelComponents() == ePixelComponentRGB ||*/
                               _srcClip->getPixelComponents() == ePixelComponentRGBA/* ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha*/) ) );
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // fetch plugin parameters
        _input = fetchChoiceParam(kParamChooseInput);
        _code = fetchStringParam(kParamCTLCode);
        _file = fetchStringParam(kParamFilename);
        assert(_input && _code && _file);
        if ( paramExists(kParamShowScript) ) {
            _showScript = fetchPushButtonParam(kParamShowScript);
            assert(_showScript);
        }
        if ( paramExists(kParamValidate) ) {
            _validate = fetchBooleanParam(kParamValidate);
            assert(_validate);
        }

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);

        updateVisibility();
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(CTLProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    void updateVisibility()
    {
        ChooseInputEnum input = (ChooseInputEnum)_input->getValue();
        switch(input) {
            case eChooseInputCode: {
                _code->setIsSecretAndDisabled(false);
                if (_showScript) {
                    _showScript->setIsSecretAndDisabled(false);
                }
                if (_validate) {
                    _validate->setIsSecretAndDisabled(false);
                }
                _file->setIsSecretAndDisabled(true);
                OFX::InstanceChangedArgs args = { OFX::eChangeUserEdit, 0, {1,1} };
                changedParam(args, kParamValidate);
                break;
            }
            case eChooseInputFile: {
                _code->setIsSecretAndDisabled(true);
                if (_showScript) {
                    _showScript->setIsSecretAndDisabled(true);
                }
                if (_validate) {
                    _validate->setIsSecretAndDisabled(true);
                }
                _file->setIsSecretAndDisabled(false);
                break;
            }
        }
    }
    
private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;

    ChoiceParam* _input;
    StringParam* _code;
    PushButtonParam* _showScript;
    BooleanParam* _validate;
    StringParam* _file;

    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CTLPlugin::setupAndProcess(CTLProcessorBase &processor,
                                      const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    ChooseInputEnum inputType = (ChooseInputEnum)_input->getValueAtTime(time);
    std::vector<std::string> paths;
    std::string filename;
    std::string module;
    std::string code;
    switch (inputType) {
        case eChooseInputCode: {
            module = "inputCode";
            _code->getValueAtTime(time, code);
            break;
        }
        case eChooseInputFile:
        {
            //			std::ifstream f( _paramFile->getValue().c_str(), std::ios::in );
            //			std::getline( f, params._code, '\0' );
            //			split( params._paths, paths, is_any_of(":;"), token_compress_on );
            _file->getValueAtTime(time, filename);
            filename = trim(filename);
            module = OFX::IO::basename(filename);
            paths.push_back( OFX::IO::dirname(filename) );
            break;
        }
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    // TODO: fetch noise parameter values

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    processor.setValues(inputType, paths, filename, module, code,
                        premult, premultChannel, mix,
                        processR, processG, processB, processA);
    processor.process();
} // CTLPlugin::setupAndProcess

// the overridden render function
void
CTLPlugin::render(const OFX::RenderArguments &args)
{
    //std::cout << "render!\n";

    clearPersistentMessage();
    if (_validate) {
        bool validated;
        _validate->getValue(validated);
        if (!validated) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Validate the CTL code before rendering/running.");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA/* || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha*/);
    // do the rendering
    switch (dstComponents) {
    case OFX::ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    /*
    case OFX::ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
    case OFX::ePixelComponentXY:
        renderForComponents<2>(args);
        break;
    case OFX::ePixelComponentAlpha:
        renderForComponents<1>(args);
        break;
     */
    default:
        //std::cout << "components usupported\n";
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
CTLPlugin::renderForComponents(const OFX::RenderArguments &args)
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
CTLPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    CTLProcessor<PIX, nComponents, maxValue> fred(*this, args);
    setupAndProcess(fred, args);
}

bool
CTLPlugin::isIdentity(const IsIdentityArguments &args,
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

    // which plugin parameter values give identity?
    ChooseInputEnum inputType = (ChooseInputEnum)_input->getValueAtTime(time);
    std::string filename;
    std::string code;
    switch (inputType) {
        case eChooseInputCode: {
            _code->getValueAtTime(time, code);
            code = trim(code);
            break;
        }
        case eChooseInputFile:
        {
            //			std::ifstream f( _paramFile->getValue().c_str(), std::ios::in );
            //			std::getline( f, params._code, '\0' );
            //			split( params._paths, paths, is_any_of(":;"), token_compress_on );
            _file->getValueAtTime(time, filename);
            filename = trim(filename);
            break;
        }
    }
    if (filename.empty() && code.empty()) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // CTLPlugin::isIdentity

void
CTLPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    const double time = args.time;

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (paramName == kParamChooseInput) {
        updateVisibility();
    } else if (paramName == kParamValidate && args.reason == OFX::eChangeUserEdit) {
        if (_validate) {
            bool validated;
            _validate->getValue(validated);

            _code->setEnabled(!validated);
            clearPersistentMessage();
        }
    } else if (paramName == kParamShowScript && args.reason == OFX::eChangeUserEdit) {
        std::string script;
        if (_code) {
            _code->getValueAtTime(time, script);
        }
        sendMessage(OFX::Message::eMessageMessage, "", "CTL Code:\n" + script);
    }
}

void
CTLPlugin::changedClip(const InstanceChangedArgs &args,
                                  const std::string &clipName)
{
    //std::cout << "changedClip!\n";
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) && _srcClip && (args.reason == OFX::eChangeUserEdit) ) {
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
    //std::cout << "changedClip OK!\n";
}

mDeclarePluginFactory(CTLPluginFactory, {}, {});
void
CTLPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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

#ifdef OFX_EXTENSIONS_NATRON
    if (OFX::getImageEffectHostDescription()->isNatron) {
        gHostIsNatron = true;
    } else {
        gHostIsNatron = false;
    }
#else
    gHostIsNatron = false;
#endif

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}

void
CTLPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                               OFX::ContextEnum context)
{
    //std::cout << "describeInContext!\n";

    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    gHostIsNatron = gHostDescription.isNatron;
    bool hostIsNuke = (gHostDescription.hostName.find("nuke") != std::string::npos ||
                       gHostDescription.hostName.find("Nuke") != std::string::npos);

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentXY);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // describe plugin params
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamChooseInput);
        param->setLabel(kParamChooseInputLabel);
        assert(param->getNOptions() == eChooseInputCode);
        param->appendOption(kParamChooseInputOptionCode);
        assert(param->getNOptions() == eChooseInputFile);
        param->appendOption(kParamChooseInputOptionFile);
        param->setDefault(eChooseInputCode);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamCTLCode);
        param->setLabel(kParamCTLCodeLabel);
        param->setHint(kParamCTLCodeHint);
        param->setStringType(eStringTypeMultiLine);
        param->setDefault("void main(\n"
                          "                input varying float rIn,\n"
                          "                input varying float gIn,\n"
                          "                input varying float bIn,\n"
                          "                input varying float aIn,\n"
                          "                output varying float rOut,\n"
                          "                output varying float gOut,\n"
                          "                output varying float bOut,\n"
                          "                output varying float aOut\n"
                          "        )\n"
                          "{\n"
                          "        rOut = rIn;\n"
                          "        gOut = gIn;\n"
                          "        bOut = bIn;\n"
                          "        aOut = aIn;\n"
                          "}\n");
        if (page) {
            page->addChild(*param);
        }
    }
    if (!gHostIsNatron) {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamShowScript);
        param->setLabel(kParamShowScriptLabel);
        if (hostIsNuke) {
            param->setHint(std::string(kParamShowScriptHint) + " " kNukeWarnTcl);
        } else {
            param->setHint(kParamShowScriptHint);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    if (!gHostIsNatron) {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabel(kParamValidateLabel);
        param->setHint(kParamValidateHint);
        param->setEvaluateOnChange(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamFilename);
        param->setLabel(kParamFilenameLabel);
        param->setHint(kParamFilenameHint);
        param->setStringType(eStringTypeFilePath);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
    //std::cout << "describeInContext! OK\n";
} // CTLPluginFactory::describeInContext

OFX::ImageEffect*
CTLPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum /*context*/)
{
    return new CTLPlugin(handle);
}

static CTLPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
