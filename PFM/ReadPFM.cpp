/*
 OFX PFM reader plugin.
 Reads an image in the Portable Float Map (PFM) format.
 
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


#include "ReadPFM.h"
#include "GenericOCIO.h"

#include <iostream>
#include <sstream>

static const bool kSupportsTiles = false;

ReadPFMPlugin::ReadPFMPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle)
{
    
}

ReadPFMPlugin::~ReadPFMPlugin() {
    
}

void ReadPFMPlugin::decode(const std::string& filename, OfxTime /*time*/, const OfxRectI& renderWindow, OFX::Image* dstImg)
{
    OFX::PixelComponentEnum pixelComponents  = dstImg->getPixelComponents();

    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    // read PFM header

#warning TODO


    assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == spec.width && renderWindow.y1 == 0 && renderWindow.y2 == spec.height));

#warning TODO
    // read pixel data
}

void ReadPFMPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod)
{
    // read PFM header

#warning TODO
    /*
    rod.x1 = spec.x;
    rod.x2 = spec.x + spec.width;
    rod.y1 = spec.y;
    rod.y2 = spec.y + spec.height;
*/
}


using namespace OFX;


/** @brief The basic describe function, passed a plugin descriptor */
void ReadPFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    
    // basic labels
    desc.setLabels("ReadPFMOFX", "ReadPFMOFX", "ReadPFMOFX");
    desc.setPluginDescription("Read PFM (Portable Float Map) files.");


#ifdef OFX_EXTENSIONS_TUTTLE

    const char* extensions[] = { "pfm", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadPFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), /*supportsRGBA =*/ true, /*supportsRGB =*/ true, /*supportsAlpha =*/ true, /*supportsTiles =*/ kSupportsTiles);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadPFMPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ReadPFMPlugin(handle);
}
