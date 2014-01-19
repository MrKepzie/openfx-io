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


#include "oiioReader.h"
#include <iostream>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagecache.h>
OIIO_NAMESPACE_USING


////global OIIO image cache
static ImageCache* cache = 0;

OiioReaderPlugin::OiioReaderPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle)
{
    
}

OiioReaderPlugin::~OiioReaderPlugin() {
    
}

void OiioReaderPlugin::clearAnyCache() {
    ///flush the OIIO cache
    cache->invalidate_all();
}

void OiioReaderPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericReaderPlugin::changedParam(args, paramName);
}

void OiioReaderPlugin::onInputFileChanged(const std::string &filename) {
    ///uncomment to use OCIO meta-data as a hint to set the correct color-space for the file.
    
//    ImageSpec spec;
//    
//    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
//    if(!cache->get_imagespec(ustring(filename), spec)){
//        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
//        return;
//    }
//    
//    ///find-out the image color-space
//    ParamValue* colorSpaceValue = spec.find_attribute("occio:ColorSpace",TypeDesc::STRING);
//    
//    //we found a color-space hint, use it to do the color-space conversion
//    const char* colorSpaceStr;
//    if (colorSpaceValue) {
//        colorSpaceStr = *(const char**)colorSpaceValue->data();
//        if (!strcmp(colorSpaceStr, "GammaCorrected")) {
//            
//        } else if(!strcmp(colorSpaceStr, "sRGB")) {
//            
//        } else if(!strcmp(colorSpaceStr, "AdobeRGB")) {
//            
//        } else if(!strcmp(colorSpaceStr, "Rec709")) {
//            
//        } else if(!strcmp(colorSpaceStr, "KodakLog")) {
//            
//        } else {
//            //unknown color-space or Linear, don't do anything
//        }
//    }
    
}

static void supportedFileFormats_static(std::vector<std::string>* formats) {
    formats->push_back("bmp");
    formats->push_back("cin");
    formats->push_back("dpx");
    formats->push_back("fits");
    formats->push_back("hdr");
    formats->push_back("ico");
    formats->push_back("iff");
    formats->push_back("jpeg");
    formats->push_back("jpg");
    formats->push_back("jpe");
    formats->push_back("jfif");
    formats->push_back("jfi");
    formats->push_back("jp2");
    formats->push_back("j2k");
    formats->push_back("exr");
    formats->push_back("png");
    formats->push_back("pbm");
    formats->push_back("pgm");
    formats->push_back("ppm");
    formats->push_back("psd");
    formats->push_back("rla");
    formats->push_back("sgi");
    formats->push_back("rgb");
    formats->push_back("rgba");
    formats->push_back("bw");
    formats->push_back("int");
    formats->push_back("inta");
    formats->push_back("pic");
    formats->push_back("tga");
    formats->push_back("tpic");
    formats->push_back("tif");
    formats->push_back("tiff");
    formats->push_back("tx");
    formats->push_back("env");
    formats->push_back("sm");
    formats->push_back("vsm");
    formats->push_back("zfile");
}


void OiioReaderPlugin::supportedFileFormats(std::vector<std::string>* formats) const {
    supportedFileFormats_static(formats);
}

void OiioReaderPlugin::decode(const std::string& filename,OfxTime time,OFX::Image* dstImg) {
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

void OiioReaderPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod) {
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
mDeclareReaderPluginFactory(OiioReaderPluginFactory, ;, ;,false,OCIO::ROLE_SCENE_LINEAR);

void OiioReaderPluginFactory::load() {
    if (!cache) {
        cache = ImageCache::create();
    }
}

void OiioReaderPluginFactory::unload() {
    if (cache) {
        ImageCache::destroy(cache);
    }
}

void OiioReaderPluginFactory::supportedFileFormats(std::vector<std::string>* formats) const{
    supportedFileFormats_static(formats);
}

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static OiioReaderPluginFactory p("fr.inria.openfx:ReadOIIO", 1, 0);
            ids.push_back(&p);
        }
    };
};

/** @brief The basic describe function, passed a plugin descriptor */
void OiioReaderPluginFactory::describeReader(OFX::ImageEffectDescriptor &desc)
{
    
    ///set OIIO to use as many threads as there are cores on the CPU
    if(!attribute("threads", 0)){
        std::cerr << "Failed to set the number of threads for OIIO" << std::endl;
    }
    
    // basic labels
    desc.setLabels("ReadOIIOOFX", "ReadOIIOOFX", "ReadOIIOOFX");
    desc.setPluginDescription("Read images using OpenImageIO.");
    
    
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OiioReaderPluginFactory::describeReaderInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context,OFX::PageParamDescriptor* page)
{
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OiioReaderPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new OiioReaderPlugin(handle);
}
