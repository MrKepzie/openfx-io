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

#include "RunScript.h"

#include <string>

#ifdef _WINDOWS
#include <windows.h>
#endif


#define kRunScriptPluginSourceClipCount 10
#define kRunScriptPluginArgumentsCount 10

#define kRunScriptPluginParamTypeNone          "None"
#define kRunScriptPluginParamTypeNoneLabel     "Empty parameter"
#define kRunScriptPluginParamTypeFilename      "Output filename"
#define kRunScriptPluginParamTypeFilenameLabel "Parameter is the "

enum ERunScriptPluginParamType
{
	eRunScriptPluginParamTypeNone = 0,
    eRunScriptPluginParamTypeFilename,
	eRunScriptPluginParamTypeString,
    eRunScriptPluginParamTypeDouble,
	eRunScriptPluginParamTypeInteger
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RunScriptPlugin : public OFX::ImageEffect {
    protected :
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *srcClip_[kRunScriptPluginSourceClipCount];

    OFX::ChoiceParam *type_[kRunScriptPluginArgumentsCount];
    OFX::StringParam *type_[kRunScriptPluginArgumentsCount];

    public :
    /** @brief ctor */
    RunScriptPlugin(OfxImageEffectHandle handle);

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);

    /* override is identity */
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime);

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName);
};

RunScriptPlugin::RunScriptPlugin(OfxImageEffectHandle handle)
: ImageEffect(handle)
, which_(0)
{
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        std::stringstream s;
        s << i;
        srcClip_[i] = fetchClip(s.str());
    }
#warning TODO
    //which_  = fetchIntParam(kRunScriptPluginParamWhich);
}

void
RunScriptPlugin::render(const OFX::RenderArguments &args)
{
    // execute the script
#warning TODO
}

using namespace OFX;

void RunScriptPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("RunScriptOFX", "RunScriptOFX", "RunScriptOFX");
    desc.setPluginGrouping("Image");
    desc.setPluginDescription("Run a script with the given arguments.\n"
                              "Each argument may be:\n"
                              "- An input filename (connect an input to an upstream Writer, and link the parameter to the output filename of this writer)\n"
                              "- An output filename (which can be linked to the input of a downstream Reader)\n"
                              "- A double (which can be linked to any plugin)\n"
                              "- An integer\n"
                              "- A string\n"
                              "Under Unix, the script should begin with a traditional shebang line, e.g. '#!/bin/sh' or '#!/usr/bin/env python'\n"
                              "The arguments can be accessed as usual from the script (in a Unix shell-script, argument 1 would be accessed as \"$1\" - use double quotes to avoid problems with spaces)\n");

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.addSupportedBitDepth(eBitDepthCustom);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(true);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void RunScriptPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    for (int i = 0; i < kRunScriptPluginSourceClipCount; ++i) {
        std::stringstream s;
        s << i;
        ClipDescriptor *srcClip = desc.defineClip(s.str());
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->addSupportedComponent(ePixelComponentCustom);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(false);
        srcClip->setIsMask(false);
        if (i >= 2) {
            srcClip->setOptional(true);
        }
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->addSupportedComponent(ePixelComponentCustom);
    dstClip->setSupportsTiles(false);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

#warning TODO
    //IntParamDescriptor *which = desc.defineIntParam(kRunScriptPluginParamWhich);
    //which->setLabels("which", "which", "which");
    //which->setScriptName("which");
    //which->setHint("The input to display. Each input is displayed at the value corresponding to the number of the input. For example, setting which to 4 displays the image from input 4.");
    //which->setDefault(0);
    //which->setRange(0, kRunScriptPluginSourceClipCount);
    //which->setDisplayRange(0, 1);
    //which->setAnimates(true);
    
    //page->addChild(*which);
}

OFX::ImageEffect* RunScriptPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new RunScriptPlugin(handle);
}

