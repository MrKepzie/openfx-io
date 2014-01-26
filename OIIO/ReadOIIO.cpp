/*
 OFX oiioReader plugin.
 Reads an image using the OpenImageIO library.
 
 Copyright (C) 2013 INRIA
 Author Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
 
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


#include "ReadOIIO.h"

#include <iostream>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagecache.h>

OIIO_NAMESPACE_USING


////global OIIO image cache
static ImageCache* cache = 0;

ReadOIIOPlugin::ReadOIIOPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, "texture_paint", "reference")
{
    
}

ReadOIIOPlugin::~ReadOIIOPlugin() {
    
}

void ReadOIIOPlugin::clearAnyCache() {
    ///flush the OIIO cache
    cache->invalidate_all();
}

void ReadOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericReaderPlugin::changedParam(args, paramName);
}

void ReadOIIOPlugin::onInputFileChanged(const std::string &filename) {
    ///uncomment to use OCIO meta-data as a hint to set the correct color-space for the file.
    
#if 0
    ImageSpec spec;

    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        return;
    }

    ///find-out the image color-space
    ParamValue* colorSpaceValue = spec.find_attribute("occio:ColorSpace",TypeDesc::STRING);

    //we found a color-space hint, use it to do the color-space conversion
    const char* colorSpaceStr;
    if (colorSpaceValue) {
        colorSpaceStr = *(const char**)colorSpaceValue->data();
        if (!strcmp(colorSpaceStr, "GammaCorrected")) {

        } else if(!strcmp(colorSpaceStr, "sRGB")) {

        } else if(!strcmp(colorSpaceStr, "AdobeRGB")) {

        } else if(!strcmp(colorSpaceStr, "Rec709")) {

        } else if(!strcmp(colorSpaceStr, "KodakLog")) {

        } else {
            //unknown color-space or Linear, don't do anything
        }
    }
#endif //0
}

void ReadOIIOPlugin::decode(const std::string& filename,OfxTime time,OFX::Image* dstImg) {
    ImageSpec spec;
    
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        return;
    }
    int channelsCount = 4; //< rgba
    if(!cache->get_pixels(ustring(filename),
                      0, //subimage
                      0, //miplevel
                      spec.x, //x begin
                      spec.x + spec.width, //x end
                      spec.y , //y begin
                      spec.y + spec.height, //y end
                      0, //z begin
                      1, //z end
                      0, //chan begin
                      channelsCount, // chan end
                      TypeDesc::FLOAT, // data type
                      dstImg->getPixelAddress(spec.x, spec.y + spec.height - 1),// output buffer
                      AutoStride, //x stride
                      -(spec.width * channelsCount * sizeof(float)), //y stride < make it invert Y
                      AutoStride //z stride
                          )) {
        
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        return;
    }
    

}

void ReadOIIOPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod) {
    ImageSpec spec;
    
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        return;
    }
    rod.x1 = spec.x;
    rod.x2 = spec.x + spec.width;
    rod.y1 = spec.y;
    rod.y2 = spec.y + spec.height;
}



using namespace OFX;

void ReadOIIOPluginFactory::load() {
    if (!cache) {
        cache = ImageCache::create();
    }
}

void ReadOIIOPluginFactory::unload() {
    if (cache) {
        ImageCache::destroy(cache);
    }
}

#if 0
namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ReadOIIOPluginFactory p("fr.inria.openfx:ReadOIIO", 1, 0);
            ids.push_back(&p);
        }
    };
};
#endif

/** @brief The basic describe function, passed a plugin descriptor */
void ReadOIIOPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc);
    ///set OIIO to use as many threads as there are cores on the CPU
    if(!attribute("threads", 0)){
        std::cerr << "Failed to set the number of threads for OIIO" << std::endl;
    }
    
    // basic labels
    desc.setLabels("ReadOIIOOFX", "ReadOIIOOFX", "ReadOIIOOFX");
    desc.setPluginDescription("Read images using OpenImageIO.");


#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "bmp", "cin", "dpx", "fits", "hdr", "ico", "iff", "jpeg", "jpg", "jpe", "jfif", "jfi", "jp2", "j2k", "exr", "png", "pbm", "pgm", "ppm", "psd", "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin());

    GenericReaderDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ReadOIIOPlugin(handle);
}
