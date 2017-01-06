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
 * OFX RunScript plugin.
 * Run a shell script.
 */

#if !( defined(_WIN32) || defined(__WIN32__) || defined(WIN32 ) ) // Sorry, MS Windows users, this plugin won't work for you

#include "RunScript.h"
#include "ofxsMacros.h"

#include <cfloat> // DBL_MAX
#undef DEBUG
#ifdef DEBUG
#include <iostream>
#define DBG(x) x
#else
#define DBG(x) (void)0
#endif
#include <string>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h> // for snprintf & _snprintf
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include "ofxsCopier.h"

#include "pstream.h"

using namespace OFX;

using std::string;
using std::vector;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "RunScriptOFX"
#define kPluginGrouping "Image"
#define kPluginDescription \
    "Run a script with the given arguments.\n" \
    "This is mostly useful to execute an external program on a set of input images files, which outputs image files.\n" \
    "Writers should be connected to each input, so that the image files are written before running the script, and the output of this node should be fed into one or more Readers, which read the images written by the script.\n" \
    "Sample node graph:\n" \
    "... +- WriteOIIO(scriptinput#####.png) +- RunScript(processes scriptinput#####.png, output is scriptoutput#####.png) +- ReadOIIO(scriptoutput#####.png) +- ...\n" \
    "Keep in mind that the input and output files are never removed in the above graph.\n" \
    "The output of RunScript is a copy of its first input, so that it can be used to execute a script at some point, e.g. to cleanup temporary files, as in:\n" \
    "... +- WriteOIIO(scriptinput#####.png) +- RunScript(processes scriptinput#####.png, output is scriptoutput#####.png) +- ReadOIIO(scriptoutput#####.png) +- RunScript(deletes temporary files scriptinput#####.png and scriptoutput#####.png, optional) +- ...\n" \
    "Each argument may be:\n" \
    "- A filename (connect an input to an upstream Writer, and link the parameter to the output filename of this writer, or link to the input filename of a downstream Reader)\n" \
    "- A floating-point value (which can be linked to any plugin)\n" \
    "- An integer\n" \
    "- A string\n" \
    "Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n" \
    "The arguments can be accessed as usual from the script (in a Unix shell-script, argument 1 would be accessed as \"$1\" - use double quotes to avoid problems with spaces).\n" \
    "This plugin uses pstream (http://pstreams.sourceforge.net), which is distributed under the GNU LGPLv3.\n"

#define kPluginIdentifier "fr.inria.openfx.RunScript"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 0
#define kRenderThreadSafety eRenderInstanceSafe

#define kRunScriptPluginSourceClipCount 10
#define kRunScriptPluginArgumentsCount 10

#define kGroupRunScriptPlugin                   "scriptParameters"
#define kGroupRunScriptPluginLabel              "Script Parameters"
#define kGroupRunScriptPluginHint               "The list of command-line parameters passed to the script."

#define kParamCount                   "paramCount"
#define kParamCountLabel              "Number of Parameters"

#define kParamType                    "type"
#define kParamTypeLabel               "Type of Parameter "

#define kParamTypeFilenameName  "filename"
#define kParamTypeFilenameLabel "File Name"
#define kParamTypeFilenameHint  "A constant or animated string containing a filename.\nIf the string contains hashes (like ####) or a printf token (like %04d), they will be replaced by the frame number, and if it contains %v or %V, it will be replaced by the view ID (\"l\" or \"r\" for %v, \"left\" or \"right\" for %V).\nThis is usually linked to the output filename of an upstream Writer node, or to the input filename of a downstream Reader node."
#define kParamTypeStringName          "string"
#define kParamTypeStringLabel         "String"
#define kParamTypeStringHint          "A string (or sequence of characters)."
#define kParamTypeDoubleName          "double"
#define kParamTypeDoubleLabel         "Floating Point"
#define kParamTypeDoubleHint          "A floating point numerical value."
#define kParamTypeIntName             "integer"
#define kParamTypeIntLabel            "Integer"
#define kParamTypeIntHint             "An integer numerical value."

#define kNukeWarnTcl "On Nuke, the characters '$', '[' ']' must be preceded with a backslash (as '\\$', '\\[', '\\]') to avoid TCL variable and expression substitution."

#define kParamScript                  "script"
#define kParamScriptLabel             "Script"
#define kParamScriptHint \
    "Contents of the script. Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n" \
    "The arguments can be accessed as usual from the script (in a Unix shell-script, argument 1 would be accessed as \"$1\" - use double quotes to avoid problems with spaces)."

#define kParamValidate                  "validate"
#define kParamValidateLabel             "Validate"
#define kParamValidateHint              "Validate the script contents and execute it on next render. This locks the script and all its parameters."

enum ERunScriptPluginParamType
{
    eRunScriptPluginParamTypeFilename = 0,
    eRunScriptPluginParamTypeString,
    eRunScriptPluginParamTypeDouble,
    eRunScriptPluginParamTypeInteger
};

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

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RunScriptPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    RunScriptPlugin(OfxImageEffectHandle handle);

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            Clip * & /*identityClip*/,
                            double & /*identityTime*/) OVERRIDE FINAL
    {
        // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
        clearPersistentMessage();

        return false;
    }

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
    void updateVisibility(void);

private:
    Clip *_srcClip[kRunScriptPluginSourceClipCount];
    Clip *_dstClip;
    IntParam *_param_count;
    ChoiceParam *_type[kRunScriptPluginArgumentsCount];
    StringParam *_filename[kRunScriptPluginArgumentsCount];
    StringParam *_string[kRunScriptPluginArgumentsCount];
    DoubleParam *_double[kRunScriptPluginArgumentsCount];
    IntParam *_int[kRunScriptPluginArgumentsCount];
    StringParam *_script;
    BooleanParam *_validate;
};

RunScriptPlugin::RunScriptPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
{
    if (getContext() != eContextGenerator) {
        for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
            if ( (i == 0) && (getContext() == eContextFilter) ) {
                _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
            } else {
                const string istr = unsignedToString(i + 1);
                _srcClip[i] = fetchClip(istr);
            }
            assert(_srcClip[i]);
        }
    }
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip);

    _param_count = fetchIntParam(kParamCount);

    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        const string istr = unsignedToString(i + 1);
        _type[i] = fetchChoiceParam(kParamType + istr);
        _filename[i] = fetchStringParam(kParamTypeFilenameName + istr);
        _string[i] = fetchStringParam(kParamTypeStringName + istr);
        _double[i] = fetchDoubleParam(kParamTypeDoubleName + istr);
        _int[i] = fetchIntParam(kParamTypeIntName + istr);
        assert(_type[i] && _filename[i] && _string[i] && _double[i] && _int[i]);
    }
    _script = fetchStringParam(kParamScript);
    _validate = fetchBooleanParam(kParamValidate);
    assert(_script && _validate);

    updateVisibility();
}

void
RunScriptPlugin::render(const RenderArguments &args)
{
    DBG(std::cout << "rendering time " << args.time << " scale " << args.renderScale.x << ',' << args.renderScale.y << " window " << args.renderWindow.x1 << ',' << args.renderWindow.y1 << " - " << args.renderWindow.x2 << ',' << args.renderWindow.y2 << " field " << (int)args.fieldToRender << " view " << args.renderView << std::endl);

    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    bool validated;
    _validate->getValue(validated);
    if (!validated) {
        setPersistentMessage(Message::eMessageError, "", "Validate the script before rendering/running.");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    // fetch images corresponding to all connected inputs,
    // since it may trigger render actions upstream
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        if ( _srcClip[i]->isConnected() ) {
            std::auto_ptr<const Image> srcImg( _srcClip[i]->fetchImage(args.time) );
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
        }
    }

    // We must at least fetch the output image, even if we don't touch it,
    // or the host may think we couldn't render.
    // Nuke executes hundreds of render() if we don't.
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

    // create the script
    char scriptname[] = "/tmp/runscriptXXXXXX";
    // Coverity suggests to call umask here for compatibility with POSIX<2008 systems,
    // but umask affects the whole process. We prefer to ignore this.
    // coverity[secure_temp]
    int fd = mkstemp(scriptname); // modifies template
    if (fd < 0) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    string script;
    _script->getValue(script);
    ssize_t s = write( fd, script.c_str(), script.size() );
    close(fd);
    if (s < 0) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    // make the script executable
    int stat = chmod(scriptname, S_IRWXU);
    if (stat != 0) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    // build the command-line
    vector<string> argv;
    argv.push_back(scriptname);

    int param_count;
    _param_count->getValue(param_count);

    char name[256];
    for (int i = 0; i < param_count; ++i) {
        int t_int;
        _type[i]->getValue(t_int);
        ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
        ValueParam *p = NULL;
        switch (t) {
        case eRunScriptPluginParamTypeFilename: {
            string s;
            _filename[i]->getValue(s);
            p = _filename[i];
            DBG(std::cout << p->getName() << "=" << s);
            argv.push_back(s);
            break;
        }
        case eRunScriptPluginParamTypeString: {
            string s;
            _string[i]->getValue(s);
            p = _string[i];
            DBG(std::cout << p->getName() << "=" << s);
            argv.push_back(s);
            break;
        }
        case eRunScriptPluginParamTypeDouble: {
            double v;
            _double[i]->getValue(v);
            p = _double[i];
            DBG(std::cout << p->getName() << "=" << v);
            snprintf(name, sizeof(name), "%g", v);
            argv.push_back(name);
            break;
        }
        case eRunScriptPluginParamTypeInteger: {
            int v;
            _int[i]->getValue(v);
            p = _int[i];
            DBG(std::cout << p->getName() << "=" << v);
            snprintf(name, sizeof(name), "%d", v);
            argv.push_back(name);
            break;
        }
        }
        if (p) {
            DBG( std::cout << "; IsAnimating=" << (p->getIsAnimating() ? "true" : "false") );
            DBG( std::cout << "; IsAutoKeying=" << (p->getIsAutoKeying() ? "true" : "false") );
            DBG( std::cout << "; NumKeys=" << p->getNumKeys() );
        }
        DBG(std::cout << std::endl);
    }

    // execute the script
    vector<string> errors;
    redi::ipstream in(scriptname, argv, redi::pstreambuf::pstderr | redi::pstreambuf::pstderr);
    string errmsg;
    while ( std::getline(in, errmsg) ) {
        errors.push_back(errmsg);
        DBG(std::cout << "output: " << errmsg << std::endl);
    }

    // remove the script
    (void)unlink(scriptname);

    // now copy the first input to output

    if ( _dstClip->isConnected() ) {
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

        std::auto_ptr<const Image> srcImg( _srcClip[0]->fetchImage(args.time) );

        if ( !srcImg.get() ) {
            // fill output with black
            fillBlack( *this, args.renderWindow, dstImg.get() );
        } else {
            if ( (srcImg->getRenderScale().x != args.renderScale.x) ||
                 ( srcImg->getRenderScale().y != args.renderScale.y) ||
                 ( srcImg->getField() != args.fieldToRender) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }

            // copy the source image (the writer is a no-op)
            copyPixels( *this, args.renderWindow, srcImg.get(), dstImg.get() );
        }
    }
} // RunScriptPlugin::render

void
RunScriptPlugin::changedParam(const InstanceChangedArgs &args,
                              const string &paramName)
{
    DBG(std::cout << "changed param " << paramName << " at time " << args.time << " reason = " << (int)args.reason <<  std::endl);

    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    int param_count;
    _param_count->getValue(param_count);

    if (paramName == kParamCount) {
        // update the parameters visibility
        updateVisibility();
    } else if (paramName == kParamValidate) {
        bool validated;
        _validate->getValue(validated);
        _param_count->setEnabled(!validated);
        _param_count->setEvaluateOnChange(validated);
        for (int i = 0; i < param_count; ++i) {
            _type[i]->setEnabled(!validated);
            _type[i]->setEvaluateOnChange(validated);
            _filename[i]->setEnabled(!validated);
            _filename[i]->setEvaluateOnChange(validated);
            _string[i]->setEnabled(!validated);
            _string[i]->setEvaluateOnChange(validated);
            _double[i]->setEnabled(!validated);
            _double[i]->setEvaluateOnChange(validated);
            _int[i]->setEnabled(!validated);
            _int[i]->setEvaluateOnChange(validated);
        }
        _script->setEnabled(!validated);
        _script->setEvaluateOnChange(validated);
        clearPersistentMessage();
    } else {
        for (int i = 0; i < param_count; ++i) {
            if ( ( paramName == _type[i]->getName() ) && (args.reason == eChangeUserEdit) ) {
                int t_int;
                _type[i]->getValue(t_int);
                ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
                _filename[i]->setIsSecretAndDisabled(t != eRunScriptPluginParamTypeFilename);
                _string[i]->setIsSecretAndDisabled(t != eRunScriptPluginParamTypeString);
                _double[i]->setIsSecretAndDisabled(t != eRunScriptPluginParamTypeDouble);
                _int[i]->setIsSecretAndDisabled(t != eRunScriptPluginParamTypeInteger);
            }
        }
    }

    for (int i = 0; i < param_count; ++i) {
        int t_int;
        _type[i]->getValue(t_int);
        ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
        ValueParam *p = NULL;
        switch (t) {
        case eRunScriptPluginParamTypeFilename: {
            string s;
            _filename[i]->getValue(s);
            p = _filename[i];
            DBG(std::cout << p->getName() << "=" << s);
            break;
        }
        case eRunScriptPluginParamTypeString: {
            string s;
            _string[i]->getValue(s);
            p = _string[i];
            DBG(std::cout << p->getName() << "=" << s);
            break;
        }
        case eRunScriptPluginParamTypeDouble: {
            double v;
            _double[i]->getValue(v);
            p = _double[i];
            DBG(std::cout << p->getName() << "=" << v);
            break;
        }
        case eRunScriptPluginParamTypeInteger: {
            int v;
            _int[i]->getValue(v);
            p = _int[i];
            DBG(std::cout << p->getName() << "=" << v);
            break;
        }
        }
        if (p) {
            DBG( std::cout << "; IsAnimating=" << (p->getIsAnimating() ? "true" : "false") );
            DBG( std::cout << "; IsAutoKeying=" << (p->getIsAutoKeying() ? "true" : "false") );
            DBG( std::cout << "; NumKeys=" << p->getNumKeys() );
        }
        DBG(std::cout << std::endl);
    }
} // RunScriptPlugin::changedParam

void
RunScriptPlugin::updateVisibility(void)
{
    // Due to a bug in Nuke, all visibility changes have to be done after instance creation.
    // It is not possible in Nuke to show a parameter that was set as secret/hidden in describeInContext()

    int param_count;

    _param_count->getValue(param_count);

    bool validated;
    _validate->getValue(validated);

    _param_count->setEnabled(!validated);
    _param_count->setEvaluateOnChange(validated);
    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        if (i >= param_count) {
            _type[i]->setIsSecret(true);
            _filename[i]->setIsSecret(true);
            _string[i]->setIsSecret(true);
            _double[i]->setIsSecret(true);
            _int[i]->setIsSecret(true);
        } else {
            _type[i]->setIsSecret(false);
            int t_int;
            _type[i]->getValue(t_int);
            ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
            _filename[i]->setIsSecret(t != eRunScriptPluginParamTypeFilename);
            _string[i]->setIsSecret(t != eRunScriptPluginParamTypeString);
            _double[i]->setIsSecret(t != eRunScriptPluginParamTypeDouble);
            _int[i]->setIsSecret(t != eRunScriptPluginParamTypeInteger);
        }
        _type[i]->setEnabled(!validated);
        _type[i]->setEvaluateOnChange(validated);
        _filename[i]->setEnabled(!validated);
        _filename[i]->setEvaluateOnChange(validated);
        _string[i]->setEnabled(!validated);
        _string[i]->setEvaluateOnChange(validated);
        _double[i]->setEnabled(!validated);
        _double[i]->setEvaluateOnChange(validated);
        _int[i]->setEnabled(!validated);
        _int[i]->setEvaluateOnChange(validated);
    }
    _script->setEnabled(!validated);
    _script->setEvaluateOnChange(validated);
}

// override the roi call
void
RunScriptPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                      RegionOfInterestSetter &rois)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if (!kSupportsTiles) {
        // The effect requires full images to render any region
        for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
            OfxRectD srcRoI;

            if ( _srcClip[i] && _srcClip[i]->isConnected() ) {
                srcRoI = _srcClip[i]->getRegionOfDefinition(args.time);
                rois.setRegionOfInterest(*_srcClip[i], srcRoI);
            }
        }
    }
}

bool
RunScriptPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                       OfxRectD & /*rod*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    // use the default RoD
    return false;
}

mDeclarePluginFactory(RunScriptPluginFactory, {}, {});
void
RunScriptPluginFactory::describe(ImageEffectDescriptor &desc)
{
    DBG(std::cout << "describing!\n");
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
    desc.setHostFrameThreading(false);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void
RunScriptPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum context)
{
    DBG(std::cout << "describing in context " << (int)context << std::endl);

    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
    bool hostIsNuke = (gHostDescription.hostName.find("nuke") != string::npos ||
                       gHostDescription.hostName.find("Nuke") != string::npos);

    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        ClipDescriptor *srcClip;
        if ( (i == 0) && (context == eContextFilter) ) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName); // mandatory clip for the filter context
        } else {
            const string istr = unsignedToString(i + 1);
            srcClip = desc.defineClip(istr);
        }
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->addSupportedComponent(ePixelComponentCustom);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(false);
        srcClip->setIsMask(false);
        srcClip->setOptional(true);
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
        GroupParamDescriptor *group = desc.defineGroupParam(kGroupRunScriptPlugin);
        if (group) {
            group->setHint(kGroupRunScriptPluginHint);
            group->setLabel(kGroupRunScriptPluginLabel);
            if (page) {
                page->addChild(*group);
            }
        }
        {
            IntParamDescriptor *param = desc.defineIntParam(kParamCount);
            param->setLabel(kParamCountLabel);
            param->setAnimates(true);
            param->setRange(0, kRunScriptPluginArgumentsCount);
            param->setDisplayRange(0, kRunScriptPluginArgumentsCount);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // Note: if we use setIsSecret() here, the parameters cannot be shown again in Nuke.
        // We thus hide them in updateVisibility(), which is called after instance creation
        for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
            const string istr = unsignedToString(i + 1);
            {
                ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamType + istr);
                param->setLabel(kParamTypeLabel + istr);
                param->setAnimates(true);
                param->appendOption(kParamTypeFilenameLabel, kParamTypeFilenameHint);
                param->appendOption(kParamTypeStringLabel,   kParamTypeStringHint);
                param->appendOption(kParamTypeDoubleLabel,   kParamTypeDoubleHint);
                param->appendOption(kParamTypeIntLabel,      kParamTypeIntHint);
                //param->setIsSecretAndDisabled(true); // done in the plugin constructor
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                StringParamDescriptor* param = desc.defineStringParam(kParamTypeFilenameName + istr);
                param->setLabel(kParamTypeFilenameLabel + istr);
                param->setHint(kParamTypeFilenameHint);
                param->setStringType(eStringTypeFilePath);
                param->setFilePathExists(false); // the file may or may not exist
                param->setAnimates(true); // the file name may change with time
                //param->setIsSecretAndDisabled(true); // done in the plugin constructor
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                StringParamDescriptor* param = desc.defineStringParam(kParamTypeStringName + istr);
                param->setLabel(kParamTypeStringLabel + istr);
                param->setHint(kParamTypeStringHint);
                param->setAnimates(true);
                //param->setIsSecretAndDisabled(true); // done in the plugin constructor
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTypeDoubleName + istr);
                param->setLabel(kParamTypeDoubleLabel + istr);
                param->setHint(kParamTypeDoubleHint);
                param->setAnimates(true);
                //param->setIsSecretAndDisabled(true); // done in the plugin constructor
                param->setRange(-DBL_MAX, DBL_MAX);
                param->setDisplayRange(-1000., 1000.);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                IntParamDescriptor* param = desc.defineIntParam(kParamTypeIntName + istr);
                param->setLabel(kParamTypeIntLabel + istr);
                param->setHint(kParamTypeIntHint);
                param->setAnimates(true);
                //param->setIsSecretAndDisabled(true); // done in the plugin constructor
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

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
        param->setDefault("#!/bin/sh\n");
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabel(kParamValidateLabel);
        param->setHint(kParamValidateHint);
        param->setEvaluateOnChange(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // RunScriptPluginFactory::describeInContext

ImageEffect*
RunScriptPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new RunScriptPlugin(handle);
}

static RunScriptPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
