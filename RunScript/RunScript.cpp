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

#include "pstream.h"

#define kPluginName "RunScriptOFX"
#define kPluginGrouping "Image"
#define kPluginDescription \
"Run a script with the given arguments.\n" \
"This is mostly useful to execute an external program on a set of input images files, which outputs image files.\n" \
"Writers should be connected to each input, so that the image files are written before running the script, and the output of this node should be fed into one or more Readers, which read the images written by the script.\n" \
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

#define kParamType                    "type"
#define kParamTypeLabel               "Type of Parameter"

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


    /** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
    virtual void beginEdit(void) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

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
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        std::stringstream s;
        s << i+1;
        srcClip_[i] = fetchClip(s.str());
    }
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);

    param_count_ = fetchIntParam(kParamCount);

    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        {
            std::stringstream ss;
            ss << kParamType << i+1;
            type_[i] = fetchChoiceParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kParamTypeFilenameName << i+1;
            filename_[i] = fetchStringParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kParamTypeStringName << i+1;
            string_[i] = fetchStringParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kParamTypeDoubleName << i+1;
            double_[i] = fetchDoubleParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kParamTypeIntName << i+1;
            int_[i] = fetchIntParam(ss.str());
        }
    }
    script_ = fetchStringParam(kParamScript);
    validate_ = fetchBooleanParam(kParamValidate);
}

void
RunScriptPlugin::render(const OFX::RenderArguments &args)
{
    DBG(std::cout << "rendering time " << args.time << " scale " << args.renderScale.x << ',' << args.renderScale.y << " window " << args.renderWindow.x1 << ',' << args.renderWindow.y1 << " - " << args.renderWindow.x2 << ',' << args.renderWindow.y2 << " field " << (int)args.fieldToRender << " view " << args.renderView << std::endl);

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    bool validated;
    validate_->getValue(validated);
    if (!validated) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Validate the script before rendering/running.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch images corresponding to all connected inputs,
    // since it may trigger render actions upstream
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        if (srcClip_[i]->isConnected()) {
            std::auto_ptr<const OFX::Image> srcImg(srcClip_[i]->fetchImage(args.time));
            if (!srcImg.get()) {
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
            if (srcImg->getRenderScale().x != args.renderScale.x ||
                srcImg->getRenderScale().y != args.renderScale.y ||
                srcImg->getField() != args.fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
    }

    // We must at least fetch the output image, even if we don't touch it,
    // or the host may think we couldn't render.
    // Nuke executes hundreds of render() if we don't.
    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // create the script
    char scriptname[] = "/tmp/runscriptXXXXXX";
    int fd = mkstemp(scriptname); // modifies template
    if (fd < 0) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::string script;
    script_->getValue(script);
    ssize_t s = write(fd, script.c_str(), script.size());
    close(fd);
    if (s < 0) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // make the script executable
    int stat = chmod(scriptname, S_IRWXU);
    if (stat != 0) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // build the command-line
    std::vector<std::string> argv;
    argv.push_back(scriptname);

    int param_count;
    param_count_->getValue(param_count);

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
                argv.push_back(s);
            }
                break;
            case eRunScriptPluginParamTypeString:
            {
                std::string s;
                string_[i]->getValue(s);
                p = string_[i];
                DBG(std::cout << p->getName() << "=" << s);
                argv.push_back(s);
            }
                break;
            case eRunScriptPluginParamTypeDouble:
            {
                double v;
                double_[i]->getValue(v);
                p = double_[i];
                DBG(std::cout << p->getName() << "=" << v);
                std::ostringstream ss;
                ss << v;
                argv.push_back(ss.str());
            }
                break;
            case eRunScriptPluginParamTypeInteger:
            {
                int v;
                int_[i]->getValue(v);
                p = int_[i];
                DBG(std::cout << p->getName() << "=" << v);
                std::ostringstream ss;
                ss << v;
                argv.push_back(ss.str());
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
}

void
RunScriptPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    DBG(std::cout << "changed param " << paramName << " at time " << args.time << " reason = " << (int)args.reason <<  std::endl);

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    int param_count;
    param_count_->getValue(param_count);

    if (paramName == kParamCount) {
        // update the parameters visibility
        beginEdit();
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
RunScriptPlugin::beginEdit(void)
{
    DBG(std::cout << "beginEdit" << std::endl);
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
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
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
    DBG(std::cout << "describing in context " << (int)context << std::endl);
    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
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
        group->setLabels(kGroupRunScriptPluginLabel, kGroupRunScriptPluginLabel, kGroupRunScriptPluginLabel);
        page->addChild(*group);

        {
            IntParamDescriptor *param = desc.defineIntParam(kParamCount);
            param->setLabels(kParamCountLabel, kParamCountLabel, kParamCountLabel);
            param->setAnimates(true);
            param->setRange(0, kRunScriptPluginArgumentsCount);
            param->setDisplayRange(0, kRunScriptPluginArgumentsCount);
            param->setParent(*group);
            page->addChild(*param);
        }

        // Note: if we use setIsSecret() here, the parameters cannot be shown again in Nuke.
        // We thus hide them in beginEdit(), which is called after instance creation
        std::stringstream ss;
        for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
            {
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamType << i+1;
                ChoiceParamDescriptor* param = desc.defineChoiceParam(ss.str());
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeLabel << ' ' << i+1;
                param->setLabels(ss.str(), ss.str(), ss.str());
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
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeFilenameName << i+1;
                StringParamDescriptor* param = desc.defineStringParam(ss.str());
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeFilenameLabel << ' ' << i+1;
                param->setLabels(ss.str(), ss.str(), ss.str());
                param->setHint(kParamTypeFilenameHint);
                param->setStringType(eStringTypeFilePath);
                param->setFilePathExists(false); // the file may or may not exist
                param->setAnimates(true); // the file name may change with time
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeStringName << i+1;
                StringParamDescriptor* param = desc.defineStringParam(ss.str());
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeStringLabel << ' ' << i+1;
                param->setLabels(ss.str(), ss.str(), ss.str());
                param->setHint(kParamTypeStringHint);
                param->setAnimates(true);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeDoubleName << i+1;
                DoubleParamDescriptor* param = desc.defineDoubleParam(ss.str());
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeDoubleLabel << ' ' << i+1;
                param->setLabels(ss.str(), ss.str(), ss.str());
                param->setHint(kParamTypeDoubleHint);
                param->setAnimates(true);
                //param->setIsSecret(true); // done in the plugin constructor
                param->setParent(*group);
                page->addChild(*param);
            }

            {
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeIntName << i+1;
                IntParamDescriptor* param = desc.defineIntParam(ss.str());
                // https://stackoverflow.com/questions/2848087/how-to-clear-stringstream
                ss.str(std::string());
                ss.clear();
                ss << kParamTypeIntLabel << ' ' << i+1;
                param->setLabels(ss.str(), ss.str(), ss.str());
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
        param->setLabels(kParamScriptLabel, kParamScriptLabel, kParamScriptLabel);
        param->setHint(kParamScriptHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault("#!/bin/sh\n");
        page->addChild(*param);
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamValidate);
        param->setLabels(kParamValidateLabel, kParamValidateLabel, kParamValidateLabel);
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