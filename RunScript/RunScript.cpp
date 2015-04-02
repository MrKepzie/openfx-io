/*
 OFX RunScript plugin.
 Run a shell script.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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

#ifndef _WINDOWS // Sorry, MS Windows users, this plugin won't work for you

#include "RunScript.h"
#include "ofxsMacros.h"

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
#ifdef _WINDOWS
#include <windows.h>
#define snprintf _snprintf
#endif

#include "ofxsCopier.h"

#include "pstream.h"

#define kPluginName "RunScriptOFX"
#define kPluginGrouping "Image"
#define kPluginDescription \
"Run a script with the given arguments.\n" \
"This is mostly useful to execute an external program on a set of input images files, which outputs image files.\n" \
"Writers should be connected to each input, so that the image files are written before running the script, and the output of this node should be fed into one or more Readers, which read the images written by the script.\n" \
"Sample node graph:\n" \
"... <- WriteOIIO(scriptinput#####.png) <- RunScript(processes scriptinput#####.png, output is scriptoutput#####.png) <- ReadOIIO(scriptoutput#####.png) <- ...\n" \
"Keep in mind that the input and output files are never removed in the above graph.\n" \
"The output of RunScript is a copy of its first input, so that it can be used to execute a script at some point, e.g. to cleanup temporary files, as in:\n" \
"... <- WriteOIIO(scriptinput#####.png) <- RunScript(processes scriptinput#####.png, output is scriptoutput#####.png) <- ReadOIIO(scriptoutput#####.png) <- RunScript(deletes temporary files scriptinput#####.png and scriptoutput#####.png, optional) <- ...\n" \
"Each argument may be:\n" \
"- A filename (connect an input to an upstream Writer, and link the parameter to the output filename of this writer, or link to the input filename of a downstream Reader)\n" \
"- A floating-point value (which can be linked to any plugin)\n" \
"- An integer\n" \
"- A string\n" \
"Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n" \
"The arguments can be accessed as usual from the script (in a Unix shell-script, argument 1 would be accessed as \"$1\" - use double quotes to avoid problems with spaces).\n" \
"This plugin uses pstream <http://pstreams.sourceforge.net>, which is distributed under the GNU LGPLv3.\n"

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

#define kParamType                    "type%d"
#define kParamTypeLabel               "Type of Parameter %d"

#define kParamTypeFilenameName  "filename%d"
#define kParamTypeFilenameLabel "File Name %d"
#define kParamTypeFilenameHint  "A constant or animated string containing a filename.\nIf the string contains hashes (like ####) or a printf token (like %04d), they will be replaced by the frame number, and if it contains %v or %V, it will be replaced by the view ID (\"l\" or \"r\" for %v, \"left\" or \"right\" for %V).\nThis is usually linked to the output filename of an upstream Writer node, or to the input filename of a downstream Reader node."
#define kParamTypeStringName          "string%d"
#define kParamTypeStringLabel         "String %d"
#define kParamTypeStringHint          "A string (or sequence of characters)."
#define kParamTypeDoubleName          "double%d"
#define kParamTypeDoubleLabel         "Floating Point %d"
#define kParamTypeDoubleHint          "A floating point numerical value."
#define kParamTypeIntName             "integer%d"
#define kParamTypeIntLabel            "Integer %d"
#define kParamTypeIntHint             "An integer numerical value."


#define kParamScript                  "script"
#define kParamScriptLabel             "Script"
#define kParamScriptHint              "Contents of the script. Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n" \
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


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RunScriptPlugin : public OFX::ImageEffect {
public:
    /** @brief ctor */
    RunScriptPlugin(OfxImageEffectHandle handle);

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:
    void updateVisibility(void);

private:
    OFX::Clip *srcClip_[kRunScriptPluginSourceClipCount];
    OFX::Clip *dstClip_;

    OFX::IntParam *param_count_;
    OFX::ChoiceParam *type_[kRunScriptPluginArgumentsCount];
    OFX::StringParam *filename_[kRunScriptPluginArgumentsCount];
    OFX::StringParam *string_[kRunScriptPluginArgumentsCount];
    OFX::DoubleParam *double_[kRunScriptPluginArgumentsCount];
    OFX::IntParam *int_[kRunScriptPluginArgumentsCount];
    OFX::StringParam *script_;
    OFX::BooleanParam *validate_;
};

RunScriptPlugin::RunScriptPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
{
    char name[256];
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        if (i == 0 && getContext() == OFX::eContextFilter) {
            srcClip_[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            snprintf(name, sizeof(name), "%d", i+1);
            srcClip_[i] = fetchClip(name);
        }
    }
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);

    param_count_ = fetchIntParam(kParamCount);

    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        snprintf(name, sizeof(name), kParamType, i+1);
        type_[i] = fetchChoiceParam(name);
        snprintf(name, sizeof(name), kParamTypeFilenameName, i+1);
        filename_[i] = fetchStringParam(name);
        snprintf(name, sizeof(name), kParamTypeStringName, i+1);
        string_[i] = fetchStringParam(name);
        snprintf(name, sizeof(name), kParamTypeDoubleName, i+1);
        double_[i] = fetchDoubleParam(name);
        snprintf(name, sizeof(name), kParamTypeIntName, i+1);
        int_[i] = fetchIntParam(name);
    }
    script_ = fetchStringParam(kParamScript);
    validate_ = fetchBooleanParam(kParamValidate);

    updateVisibility();
}

void
RunScriptPlugin::render(const OFX::RenderArguments &args)
{
    DBG(std::cout << "rendering time " << args.time << " scale " << args.renderScale.x << ',' << args.renderScale.y << " window " << args.renderWindow.x1 << ',' << args.renderWindow.y1 << " - " << args.renderWindow.x2 << ',' << args.renderWindow.y2 << " field " << (int)args.fieldToRender << " view " << args.renderView << std::endl);

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    bool validated;
    validate_->getValue(validated);
    if (!validated) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // fetch images corresponding to all connected inputs,
    // since it may trigger render actions upstream
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        if (srcClip_[i]->isConnected()) {
            std::auto_ptr<const OFX::Image> srcImg(srcClip_[i]->fetchImage(args.time));
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
        }
    }

    // We must at least fetch the output image, even if we don't touch it,
    // or the host may think we couldn't render.
    // Nuke executes hundreds of render() if we don't.
    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
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

    // create the script
    char scriptname[] = "/tmp/runscriptXXXXXX";
    // Coverity suggests to call umask here for compatibility with POSIX<2008 systems,
    // but umask affects the whole process. We prefer to ignore this.
    // coverity[secure_temp]
    int fd = mkstemp(scriptname); // modifies template
    if (fd < 0) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    std::string script;
    script_->getValue(script);
    ssize_t s = write(fd, script.c_str(), script.size());
    close(fd);
    if (s < 0) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // make the script executable
    int stat = chmod(scriptname, S_IRWXU);
    if (stat != 0) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // build the command-line
    std::vector<std::string> argv;
    argv.push_back(scriptname);

    int param_count;
    param_count_->getValue(param_count);

    char name[256];
    for (int i = 0; i < param_count; ++i) {
        int t_int;
        type_[i]->getValue(t_int);
        ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
        OFX::ValueParam *p = NULL;
        switch (t) {
            case eRunScriptPluginParamTypeFilename: {
                std::string s;
                filename_[i]->getValue(s);
                p = filename_[i];
                DBG(std::cout << p->getName() << "=" << s);
                argv.push_back(s);
                break;
            }
            case eRunScriptPluginParamTypeString: {
                std::string s;
                string_[i]->getValue(s);
                p = string_[i];
                DBG(std::cout << p->getName() << "=" << s);
                argv.push_back(s);
                break;
            }
            case eRunScriptPluginParamTypeDouble: {
                double v;
                double_[i]->getValue(v);
                p = double_[i];
                DBG(std::cout << p->getName() << "=" << v);
                snprintf(name, sizeof(name), "%g", v);
                argv.push_back(name);
                break;
            }
            case eRunScriptPluginParamTypeInteger: {
                int v;
                int_[i]->getValue(v);
                p = int_[i];
                DBG(std::cout << p->getName() << "=" << v);
                snprintf(name, sizeof(name), "%d", v);
                argv.push_back(name);
                break;
            }
        }
        if (p) {
            DBG(std::cout << "; IsAnimating=" << (p->getIsAnimating() ? "true" : "false"));
            DBG(std::cout << "; IsAutoKeying=" << (p->getIsAutoKeying() ? "true" : "false"));
            DBG(std::cout << "; NumKeys=" << p->getNumKeys());
        }
        DBG(std::cout << std::endl);
    }

    // execute the script
    std::vector<std::string> errors;
    redi::ipstream in(scriptname, argv, redi::pstreambuf::pstderr|redi::pstreambuf::pstderr);
    std::string errmsg;
    while (std::getline(in, errmsg)) {
        errors.push_back(errmsg);
        DBG(std::cout << "output: " << errmsg << std::endl);
    }

    // remove the script
    (void)unlink(scriptname);

    // now copy the first input to output

    if (dstClip_->isConnected()) {
        std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
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

        std::auto_ptr<const OFX::Image> srcImg(srcClip_[0]->fetchImage(args.time));

        if (!srcImg.get()) {
            // fill output with black
            fillBlack(*this, args.renderWindow, dstImg.get());
        } else {
            if (srcImg->getRenderScale().x != args.renderScale.x ||
                srcImg->getRenderScale().y != args.renderScale.y ||
                srcImg->getField() != args.fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }

            // copy the source image (the writer is a no-op)
            copyPixels(*this, args.renderWindow, srcImg.get(), dstImg.get());
        }
    }

}

void
RunScriptPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    DBG(std::cout << "changed param " << paramName << " at time " << args.time << " reason = " << (int)args.reason <<  std::endl);

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    int param_count;
    param_count_->getValue(param_count);

    if (paramName == kParamCount) {
        // update the parameters visibility
        updateVisibility();
    } else if (paramName == kParamValidate) {
        bool validated;
        validate_->getValue(validated);
        param_count_->setEnabled(!validated);
        param_count_->setEvaluateOnChange(validated);
        for (int i = 0; i < param_count; ++i) {
            type_[i]->setEnabled(!validated);
            type_[i]->setEvaluateOnChange(validated);
            filename_[i]->setEnabled(!validated);
            filename_[i]->setEvaluateOnChange(validated);
            string_[i]->setEnabled(!validated);
            string_[i]->setEvaluateOnChange(validated);
            double_[i]->setEnabled(!validated);
            double_[i]->setEvaluateOnChange(validated);
            int_[i]->setEnabled(!validated);
            int_[i]->setEvaluateOnChange(validated);
        }
        script_->setEnabled(!validated);
        script_->setEvaluateOnChange(validated);
        clearPersistentMessage();
    } else {
        for (int i = 0; i < param_count; ++i) {
            if (paramName == type_[i]->getName() && args.reason == OFX::eChangeUserEdit) {
                int t_int;
                type_[i]->getValue(t_int);
                ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
                filename_[i]->setIsSecret(t != eRunScriptPluginParamTypeFilename);
                string_[i]->setIsSecret(t != eRunScriptPluginParamTypeString);
                double_[i]->setIsSecret(t != eRunScriptPluginParamTypeDouble);
                int_[i]->setIsSecret(t != eRunScriptPluginParamTypeInteger);
            }
        }
    }

    for (int i = 0; i < param_count; ++i) {
        int t_int;
        type_[i]->getValue(t_int);
        ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
        OFX::ValueParam *p = NULL;
        switch (t) {
            case eRunScriptPluginParamTypeFilename:
            {
                std::string s;
                filename_[i]->getValue(s);
                p = filename_[i];
                DBG(std::cout << p->getName() << "=" << s);
            }
                break;
            case eRunScriptPluginParamTypeString:
            {
                std::string s;
                string_[i]->getValue(s);
                p = string_[i];
                DBG(std::cout << p->getName() << "=" << s);
            }
                break;
            case eRunScriptPluginParamTypeDouble:
            {
                double v;
                double_[i]->getValue(v);
                p = double_[i];
                DBG(std::cout << p->getName() << "=" << v);
            }
                break;
            case eRunScriptPluginParamTypeInteger:
            {
                int v;
                int_[i]->getValue(v);
                p = int_[i];
                DBG(std::cout << p->getName() << "=" << v);
            }
                break;
        }
        if (p) {
            DBG(std::cout << "; IsAnimating=" << (p->getIsAnimating() ? "true" : "false"));
            DBG(std::cout << "; IsAutoKeying=" << (p->getIsAutoKeying() ? "true" : "false"));
            DBG(std::cout << "; NumKeys=" << p->getNumKeys());
        }
        DBG(std::cout << std::endl);
    }

}

void
RunScriptPlugin::updateVisibility(void)
{
    // Due to a bug in Nuke, all visibility changes have to be done after instance creation.
    // It is not possible in Nuke to show a parameter that was set as secret/hidden in describeInContext()

    int param_count;
    param_count_->getValue(param_count);

    bool validated;
    validate_->getValue(validated);

    param_count_->setEnabled(!validated);
    param_count_->setEvaluateOnChange(validated);
    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        if (i >= param_count) {
            type_[i]->setIsSecret(true);
            filename_[i]->setIsSecret(true);
            string_[i]->setIsSecret(true);
            double_[i]->setIsSecret(true);
            int_[i]->setIsSecret(true);
        } else {
            type_[i]->setIsSecret(false);
            int t_int;
            type_[i]->getValue(t_int);
            ERunScriptPluginParamType t = (ERunScriptPluginParamType)t_int;
            filename_[i]->setIsSecret(t != eRunScriptPluginParamTypeFilename);
            string_[i]->setIsSecret(t != eRunScriptPluginParamTypeString);
            double_[i]->setIsSecret(t != eRunScriptPluginParamTypeDouble);
            int_[i]->setIsSecret(t != eRunScriptPluginParamTypeInteger);
        }
        type_[i]->setEnabled(!validated);
        type_[i]->setEvaluateOnChange(validated);
        filename_[i]->setEnabled(!validated);
        filename_[i]->setEvaluateOnChange(validated);
        string_[i]->setEnabled(!validated);
        string_[i]->setEvaluateOnChange(validated);
        double_[i]->setEnabled(!validated);
        double_[i]->setEvaluateOnChange(validated);
        int_[i]->setEnabled(!validated);
        int_[i]->setEvaluateOnChange(validated);
    }
    script_->setEnabled(!validated);
    script_->setEvaluateOnChange(validated);
}

// override the roi call
void
RunScriptPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                      OFX::RegionOfInterestSetter &rois)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (!kSupportsTiles) {
        // The effect requires full images to render any region
        for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
            OfxRectD srcRoI;

            if (srcClip_[i] && srcClip_[i]->isConnected()) {
                srcRoI = srcClip_[i]->getRegionOfDefinition(args.time);
                rois.setRegionOfInterest(*srcClip_[i], srcRoI);
            }
        }
    }
}

bool
RunScriptPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                       OfxRectD &/*rod*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // use the default RoD
    return false;
}

using namespace OFX;

mDeclarePluginFactory(RunScriptPluginFactory, {}, {});

void RunScriptPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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

void RunScriptPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    char name[256];
    DBG(std::cout << "describing in context " << (int)context << std::endl);
    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        ClipDescriptor *srcClip;
        if (i == 0 && context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName); // mandatory clip for the filter context
        } else {
            snprintf(name, sizeof(name), "%d", i+1);
            srcClip = desc.defineClip(name);
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
        group->setHint(kGroupRunScriptPluginHint);
        group->setLabel(kGroupRunScriptPluginLabel);
        page->addChild(*group);

        {
            IntParamDescriptor *param = desc.defineIntParam(kParamCount);
            param->setLabel(kParamCountLabel);
            param->setAnimates(true);
            param->setRange(0, kRunScriptPluginArgumentsCount);
            param->setDisplayRange(0, kRunScriptPluginArgumentsCount);
            param->setParent(*group);
            page->addChild(*param);
        }

        // Note: if we use setIsSecret() here, the parameters cannot be shown again in Nuke.
        // We thus hide them in updateVisibility(), which is called after instance creation
        for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
            {
                snprintf(name, sizeof(name), kParamType, i+1);
                ChoiceParamDescriptor* param = desc.defineChoiceParam(name);
                snprintf(name, sizeof(name), kParamTypeLabel, i+1);
                param->setLabel(name);
                param->setAnimates(true);
                param->appendOption(kParamTypeFilenameLabel, kParamTypeFilenameHint);
                param->appendOption(kParamTypeStringLabel,   kParamTypeStringHint);
                param->appendOption(kParamTypeDoubleLabel,   kParamTypeDoubleHint);
                param->appendOption(kParamTypeIntLabel,      kParamTypeIntHint);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                snprintf(name, sizeof(name), kParamTypeFilenameName, i+1);
                StringParamDescriptor* param = desc.defineStringParam(name);
                snprintf(name, sizeof(name), kParamTypeFilenameLabel, i+1);
                param->setLabel(name);
                param->setHint(kParamTypeFilenameHint);
                param->setStringType(eStringTypeFilePath);
                param->setFilePathExists(false); // the file may or may not exist
                param->setAnimates(true); // the file name may change with time
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                snprintf(name, sizeof(name), kParamTypeStringName, i+1);
                StringParamDescriptor* param = desc.defineStringParam(name);
                snprintf(name, sizeof(name), kParamTypeStringLabel, i+1);
                param->setLabel(name);
                param->setHint(kParamTypeStringHint);
                param->setAnimates(true);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                snprintf(name, sizeof(name), kParamTypeDoubleName, i+1);
                DoubleParamDescriptor* param = desc.defineDoubleParam(name);
                snprintf(name, sizeof(name), kParamTypeDoubleLabel, i+1);
                param->setLabel(name);
                param->setHint(kParamTypeDoubleHint);
                param->setAnimates(true);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                snprintf(name, sizeof(name), kParamTypeIntName, i+1);
                IntParamDescriptor* param = desc.defineIntParam(name);
                snprintf(name, sizeof(name), kParamTypeIntLabel, i+1);
                param->setLabel(name);
                param->setHint(kParamTypeIntHint);
                param->setAnimates(true);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
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
        param->setDefault("#!/bin/sh\n");
        page->addChild(*param);
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabel(kParamValidateLabel);
        param->setHint(kParamValidateHint);
        param->setEvaluateOnChange(true);
        page->addChild(*param);
    }
}

OFX::ImageEffect* RunScriptPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new RunScriptPlugin(handle);
}



void getRunScriptPluginID(OFX::PluginFactoryArray &ids)
{
    static RunScriptPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

#endif // _WINDOWS