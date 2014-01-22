/*
 OFX oiioWriter plugin.
 Writs an image using the OpenImageIO library.
 
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

#include "WriteOIIO.h"

#include <OpenImageIO/imageio.h>

OIIO_NAMESPACE_USING


WriteOIIOPlugin::WriteOIIOPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
{
    
}


WriteOIIOPlugin::~WriteOIIOPlugin() {
    
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

void WriteOIIOPlugin::supportedFileFormats(std::vector<std::string>* formats) const {
    supportedFileFormats_static(formats);
}

void WriteOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericWriterPlugin::changedParam(args, paramName);
}

void WriteOIIOPlugin::encode(const std::string& filename,OfxTime time,const OFX::Image* srcImg) {
    ImageOutput *output = ImageOutput::create(filename);
    if (!output) {
        setPersistentMessage(OFX::Message::eMessageError, "", output->geterror());
        return;
    }
    
    OfxRectI rod = srcImg->getRegionOfDefinition();
    ImageSpec spec (rod.x2 - rod.x1, rod.y2 - rod.y1, 4, TypeDesc::FLOAT);
    
    bool supportsRectangles = output->supports("rectangles");
    
    if (supportsRectangles) {
        spec.x = rod.x1;
        spec.y = rod.y1;
        spec.full_x = rod.x1;
        spec.full_y = rod.y1;
    }
    
    if (!output->open(filename, spec)) {
        setPersistentMessage(OFX::Message::eMessageError, "", output->geterror());
        return;
    }
    
    if (supportsRectangles) {
        output->write_rectangle(spec.x, //xmin
                                spec.x + spec.width, //xmax
                                spec.y, //ymin
                                spec.y + spec.height, //ymax
                                0, //zmin
                                1, //zmax
                                spec.format, //datatype
                                srcImg->getPixelAddress(spec.x, spec.y + spec.height - 1), //invert y
                                AutoStride, //xstride
                                -(spec.width * 4 * sizeof(float)), //ystride
                                AutoStride //zstride
                                );
    } else {
        output->write_image(spec.format,
                            srcImg->getPixelAddress(spec.x, spec.y + spec.height - 1), //invert y
                            AutoStride, //xstride
                            -(spec.width * 4 * sizeof(float)), //ystride
                            AutoStride //zstride
                            );
    }
    
    output->close();
    delete output;
}

bool WriteOIIOPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}


using namespace OFX;




void WriteOIIOPluginFactory::supportedFileFormats(std::vector<std::string>* formats) const{
    supportedFileFormats_static(formats);
}

#if 0
namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static WriteOIIOPluginFactory p("fr.inria.openfx:WriteOIIO", 1, 0);
            ids.push_back(&p);
        }
    };
};
#endif

/** @brief The basic describe function, passed a plugin descriptor */
void WriteOIIOPluginFactory::describeWriter(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("WriteOIIOOFX", "WriteOIIOOFX", "WriteOIIOOFX");
    desc.setPluginDescription("Write images file using the OpenImageIO library.");
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WriteOIIOPluginFactory::describeWriterInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context,OFX::PageParamDescriptor* page)
{    
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new WriteOIIOPlugin(handle);
}
