/*
 OFX PFM writer plugin.
 Writes an image in the Portable Float Map (PFM) format.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay frederic.devernay@inria.fr

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
 
 */

#include "WritePFM.h"
#include "GenericOCIO.h"


WritePFMPlugin::WritePFMPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
{
}


WritePFMPlugin::~WritePFMPlugin()
{
}

void WritePFMPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericWriterPlugin::changedParam(args, paramName);
}

void WritePFMPlugin::encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only write RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    int numChannels;
    switch(pixelComponents)
    {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

#warning TODO
}

bool WritePFMPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}


using namespace OFX;

/** @brief The basic describe function, passed a plugin descriptor */
void WritePFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc);
    // basic labels
    desc.setLabels("WritePFMOFX", "WritePFMOFX", "WritePFMOFX");
    desc.setPluginDescription("Write PFM (Portable Float Map) files.");

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "pfm", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WritePFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(), true, false, false, "reference", "reference");

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WritePFMPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new WritePFMPlugin(handle);
}
