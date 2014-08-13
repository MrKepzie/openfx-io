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
"Each argument may be:\n" \
"- A filename (connect an input to an upstream Writer, and link the parameter to the output filename of this writer, or link to the input filename of a downstream Reader)\n" \
"- A floating-point value (which can be linked to any plugin)\n" \
"- An integer\n" \
"- A string\n" \
"Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n" \
"The arguments can be accessed as usual from the script (in a Unix shell-script, argument 1 would be accessed as \"$1\" - use double quotes to avoid problems with spaces).\n" \
"This plugin uses pstream <http://pstreams.sourceforge.net>, which is distributed under the GNU LGPLv3.\n"

#define kPluginIdentifier "fr.inria.openfx:RunScript"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 0
#define kRenderThreadSafety eRenderInstanceSafe

#define kRunScriptPluginSourceClipCount 10
#define kRunScriptPluginArgumentsCount 10

#define kRunScriptPluginParamGroupName               "scriptParameters"
#define kRunScriptPluginParamGroupLabel              "Script parameters"
#define kRunScriptPluginParamGroupHint               "The list of command-line parameters passed to the script."

#define kRunScriptPluginParamCountName               "paramCount"
#define kRunScriptPluginParamCountLabel              "Number of parameters"

#define kRunScriptPluginParamTypeName                "type"
#define kRunScriptPluginParamTypeLabel               "Type of parameter"

#define kRunScriptPluginParamTypeFilenameName  "filename"
#define kRunScriptPluginParamTypeFilenameLabel "File name"
#define kRunScriptPluginParamTypeFilenameHint  "A constant or animated string containing a filename.\nIf the string contains hashes (like ####) or a printf token (like %04d), they will be replaced by the frame number, and if it contains %v or %V, it will be replaced by the view ID (\"l\" or \"r\" for %v, \"left\" or \"right\" for %V).\nThis is usually linked to the output filename of an upstream Writer node, or to the input filename of a downstream Reader node."
#define kRunScriptPluginParamTypeStringName          "string"
#define kRunScriptPluginParamTypeStringLabel         "String"
#define kRunScriptPluginParamTypeStringHint          "A string (or sequence of characters)."
#define kRunScriptPluginParamTypeDoubleName          "double"
#define kRunScriptPluginParamTypeDoubleLabel         "Floating point"
#define kRunScriptPluginParamTypeDoubleHint          "A floating point numerical value."
#define kRunScriptPluginParamTypeIntName             "integer"
#define kRunScriptPluginParamTypeIntLabel            "Integer"
#define kRunScriptPluginParamTypeIntHint             "An integer numerical value."


#define kRunScriptPluginParamScriptName              "script"
#define kRunScriptPluginParamScriptLabel             "Script"
#define kRunScriptPluginParamScriptHint              "Contents of the script. Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n" \
                                                     "The arguments can be accessed as usual from the script (in a Unix shell-script, argument 1 would be accessed as \"$1\" - use double quotes to avoid problems with spaces)."

#define kRunScriptPluginParamValidateName              "validate"
#define kRunScriptPluginParamValidateLabel             "Validate"
#define kRunScriptPluginParamValidateHint              "Validate the script contents and execute it on next render. This locks the script and all its parameters."

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
    virtual void render(const OFX::RenderArguments &args);

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);


    /** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
    virtual void beginEdit(void);

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);

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

    param_count_ = fetchIntParam(kRunScriptPluginParamCountName);

    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeName << i+1;
            type_[i] = fetchChoiceParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeFilenameName << i+1;
            filename_[i] = fetchStringParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeStringName << i+1;
            string_[i] = fetchStringParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeDoubleName << i+1;
            double_[i] = fetchDoubleParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeIntName << i+1;
            int_[i] = fetchIntParam(ss.str());
        }
    }
    script_ = fetchStringParam(kRunScriptPluginParamScriptName);
    validate_ = fetchBooleanParam(kRunScriptPluginParamValidateName);
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
            std::auto_ptr<OFX::Image> srcImg(srcClip_[i]->fetchImage(args.time));
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

    if (paramName == kRunScriptPluginParamCountName) {
        // update the parameters visibility
        beginEdit();
    } else if (paramName == kRunScriptPluginParamValidateName) {
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
                                       OfxRectD &rod)
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

    GroupParamDescriptor *script_parameters_ = desc.defineGroupParam(kRunScriptPluginParamGroupName);
    script_parameters_->setHint(kRunScriptPluginParamGroupHint);
    script_parameters_->setLabels(kRunScriptPluginParamGroupLabel, kRunScriptPluginParamGroupLabel, kRunScriptPluginParamGroupLabel);
    page->addChild(*script_parameters_);

    IntParamDescriptor *param_count_ = desc.defineIntParam(kRunScriptPluginParamCountName);
    param_count_->setLabels(kRunScriptPluginParamCountLabel, kRunScriptPluginParamCountLabel, kRunScriptPluginParamCountLabel);
    param_count_->setAnimates(true);
    param_count_->setRange(0, kRunScriptPluginArgumentsCount);
    param_count_->setDisplayRange(0, kRunScriptPluginArgumentsCount);
    param_count_->setParent(*script_parameters_);
    page->addChild(*param_count_);

    // Note: if we use setIsSecret() here, the parameters cannot be shown again in Nuke.
    // We thus hide them in beginEdit(), which is called after instance creation
    for (int i = 0; i < kRunScriptPluginArgumentsCount; ++i) {
        ChoiceParamDescriptor *type_;
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeName << i+1;
            type_ = desc.defineChoiceParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeLabel << ' ' << i+1;
            type_->setLabels(ss.str(), ss.str(), ss.str());
        }
        type_->setAnimates(true);
        type_->appendOption(kRunScriptPluginParamTypeFilenameLabel, kRunScriptPluginParamTypeFilenameHint);
        type_->appendOption(kRunScriptPluginParamTypeStringLabel,   kRunScriptPluginParamTypeStringHint);
        type_->appendOption(kRunScriptPluginParamTypeDoubleLabel,   kRunScriptPluginParamTypeDoubleHint);
        type_->appendOption(kRunScriptPluginParamTypeIntLabel,      kRunScriptPluginParamTypeIntHint);
        //type_->setIsSecret(true);
        type_->setParent(*script_parameters_);
        page->addChild(*type_);

        StringParamDescriptor *filename_;
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeFilenameName << i+1;
            filename_ = desc.defineStringParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeFilenameLabel << ' ' << i+1;
            filename_->setLabels(ss.str(), ss.str(), ss.str());
        }
        filename_->setHint(kRunScriptPluginParamTypeFilenameHint);
        filename_->setStringType(eStringTypeFilePath);
        filename_->setFilePathExists(false); // the file may or may not exist
        filename_->setAnimates(true); // the file name may change with time
        //filename_->setIsSecret(true);
        filename_->setParent(*script_parameters_);
        page->addChild(*filename_);

        StringParamDescriptor *string_;
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeStringName << i+1;
            string_ = desc.defineStringParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeStringLabel << ' ' << i+1;
            string_->setLabels(ss.str(), ss.str(), ss.str());
        }
        string_->setHint(kRunScriptPluginParamTypeStringHint);
        string_->setAnimates(true);
        //string_->setIsSecret(true);
        string_->setParent(*script_parameters_);
        page->addChild(*string_);

        DoubleParamDescriptor *double_;
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeDoubleName << i+1;
            double_ = desc.defineDoubleParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeDoubleLabel << ' ' << i+1;
            double_->setLabels(ss.str(), ss.str(), ss.str());
        }
        double_->setHint(kRunScriptPluginParamTypeDoubleHint);
        double_->setAnimates(true);
        //double_->setIsSecret(true);
        double_->setParent(*script_parameters_);
        page->addChild(*double_);

        IntParamDescriptor *int_;
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeIntName << i+1;
            int_ = desc.defineIntParam(ss.str());
        }
        {
            std::stringstream ss;
            ss << kRunScriptPluginParamTypeIntLabel << ' ' << i+1;
            int_->setLabels(ss.str(), ss.str(), ss.str());
        }
        int_->setHint(kRunScriptPluginParamTypeIntHint);
        int_->setAnimates(true);
        //int_->setIsSecret(true);
        int_->setParent(*script_parameters_);
        page->addChild(*int_);
    }

    StringParamDescriptor *script_ = desc.defineStringParam(kRunScriptPluginParamScriptName);
    script_->setLabels(kRunScriptPluginParamScriptLabel, kRunScriptPluginParamScriptLabel, kRunScriptPluginParamScriptLabel);
    script_->setHint(kRunScriptPluginParamScriptHint);
    script_->setStringType(eStringTypeMultiLine);
    script_->setAnimates(true);
    script_->setDefault("#!/bin/sh\n");
    page->addChild(*script_);

    BooleanParamDescriptor *validate_ = desc.defineBooleanParam(kRunScriptPluginParamValidateName);
    validate_->setLabels(kRunScriptPluginParamValidateLabel, kRunScriptPluginParamValidateLabel, kRunScriptPluginParamValidateLabel);
    validate_->setHint(kRunScriptPluginParamValidateHint);
    validate_->setEvaluateOnChange(true);
    page->addChild(*validate_);
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