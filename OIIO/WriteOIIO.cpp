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
: GenericWriterPlugin(handle, "reference", "reference")
{
    
}


WriteOIIOPlugin::~WriteOIIOPlugin() {
    
}

void WriteOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericWriterPlugin::changedParam(args, paramName);
}

void WriteOIIOPlugin::encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OIIO: can only write RGBA, RGB or Alpha components images");
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

    std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
    if (!output.get()) {
        setPersistentMessage(OFX::Message::eMessageError, "", output->geterror());
        return;
    }
    
    ImageSpec spec (bounds.x2 - bounds.x1, bounds.y2 - bounds.y1, numChannels, TypeDesc::FLOAT);
    // by default, the channel names are R, G, B, A, which is OK except for Alpha images
    if (pixelComponents == OFX::ePixelComponentAlpha) {
        spec.channelnames.clear();
        spec.channelnames.push_back ("A");
        spec.alpha_channel = 0;
    }
    bool supportsRectangles = output->supports("rectangles");
    
    if (supportsRectangles) {
        spec.x = bounds.x1;
        spec.y = bounds.y1;
        spec.full_x = bounds.x1;
        spec.full_y = bounds.y1;
    }
    
    if (!output->open(filename, spec)) {
        setPersistentMessage(OFX::Message::eMessageError, "", output->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    if (supportsRectangles) {
        output->write_rectangle(spec.x, //xmin
                                spec.x + spec.width, //xmax
                                spec.y, //ymin
                                spec.y + spec.height, //ymax
                                0, //zmin
                                1, //zmax
                                spec.format, //datatype
                                (char*)pixelData + (spec.height - 1) * rowBytes, //invert y
                                AutoStride, //xstride
                                -rowBytes, //ystride
                                AutoStride //zstride
                                );
    } else {
        output->write_image(spec.format,
                            (char*)pixelData + (spec.height - 1) * rowBytes, //invert y
                            AutoStride, //xstride
                            -rowBytes, //ystride
                            AutoStride //zstride
                            );
    }
    
    output->close();
}

bool WriteOIIOPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}


using namespace OFX;

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

static std::string oiio_versions()
{
    std::ostringstream oss;
    int ver = openimageio_version();
    oss << "OIIO versions:" << std::endl;
    oss << "compiled with " << OIIO_VERSION_STRING << std::endl;
    oss << "running with " << ver/10000 << '.' << (ver%10000)/100 << '.' << (ver%100) << std::endl;
    return oss.str();
}

/** @brief The basic describe function, passed a plugin descriptor */
void WriteOIIOPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc);
    // basic labels
    desc.setLabels("WriteOIIOOFX", "WriteOIIOOFX", "WriteOIIOOFX");
    desc.setPluginDescription("Write images file using the OpenImageIO library.\n\n"
                              "OpenImageIO supports writing the following file formats:\n"
                              "BMP (*.bmp)\n"
                              "Cineon (*.cin)\n"
                              //"Direct Draw Surface (*.dds)\n"
                              "DPX (*.dpx)\n"
                              //"Field3D (*.f3d)\n"
                              "FITS (*.fits)\n"
                              "HDR/RGBE (*.hdr)\n"
                              "Icon (*.ico)\n"
                              "IFF (*.iff)\n"
                              "JPEG (*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi)\n"
                              "JPEG-2000 (*.jp2 *.j2k)\n"
                              "OpenEXR (*.exr)\n"
                              "Portable Network Graphics (*.png)\n"
                              "PNM / Netpbm (*.pbm *.pgm *.ppm)\n"
                              "PSD (*.psd *.pdd *.psb)\n"
                              //"Ptex (*.ptex)\n"
                              "RLA (*.rla)\n"
                              "SGI (*.sgi *.rgb *.rgba *.bw *.int *.inta)\n"
                              "Softimage PIC (*.pic)\n"
                              "Targa (*.tga *.tpic)\n"
                              "TIFF (*.tif *.tiff *.tx *.env *.sm *.vsm)\n"
                              "Zfile (*.zfile)\n\n"
                              + oiio_versions());

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "bmp", "cin", /*"dds",*/ "dpx", /*"f3d",*/ "fits", "hdr", "ico", "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png", "pbm", "pgm", "ppm", "psd", "pdd", "psb", /*"ptex",*/ "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WriteOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(), true, false, false);

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new WriteOIIOPlugin(handle);
}
