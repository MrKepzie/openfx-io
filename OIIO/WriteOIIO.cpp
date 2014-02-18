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
#include "GenericOCIO.h"

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/filesystem.h>

OIIO_NAMESPACE_USING

#define kParamPremultiplied "premultiplied"
#define kParamPremultipliedLabel "Premultiplied"

#define kTuttlePluginBitDepth        "bitDepth"
#define kTuttlePluginBitDepthLabel   "Bit depth"

#define kTuttlePluginBitDepthAuto     "auto"
#define kTuttlePluginBitDepthAutoLabel "Guess from the output format"
//#define kTuttlePluginBitDepthNone     "none"
#define kTuttlePluginBitDepth8      "8i"
#define kTuttlePluginBitDepth8Label   "8  bits integer"
#define kTuttlePluginBitDepth10     "10i"
#define kTuttlePluginBitDepth10Label  "10 bits integer"
#define kTuttlePluginBitDepth12     "12i"
#define kTuttlePluginBitDepth12Label  "12 bits integer"
#define kTuttlePluginBitDepth16     "16i"
#define kTuttlePluginBitDepth16Label  "16 bits integer"
#define kTuttlePluginBitDepth16f    "16f"
#define kTuttlePluginBitDepth16fLabel "16 bits floating point"
#define kTuttlePluginBitDepth32     "32i"
#define kTuttlePluginBitDepth32Label  "32 bits integer"
#define kTuttlePluginBitDepth32f    "32f"
#define kTuttlePluginBitDepth32fLabel "32 bits floating point"
#define kTuttlePluginBitDepth64     "64i"
#define kTuttlePluginBitDepth64Label  "64 bits integer"
#define kTuttlePluginBitDepth64f    "64f"
#define kTuttlePluginBitDepth64fLabel "64 bits floating point"

enum ETuttlePluginBitDepth
{
	eTuttlePluginBitDepthAuto = 0,
	eTuttlePluginBitDepth8,
	eTuttlePluginBitDepth10,
	eTuttlePluginBitDepth12,
	eTuttlePluginBitDepth16,
	eTuttlePluginBitDepth16f,
	eTuttlePluginBitDepth32,
	eTuttlePluginBitDepth32f,
	eTuttlePluginBitDepth64,
	eTuttlePluginBitDepth64f
};

enum ETuttlePluginComponents
{
	eTuttlePluginComponentsAuto = 0,
	eTuttlePluginComponentsGray,
	eTuttlePluginComponentsRGB,
	eTuttlePluginComponentsRGBA
};

#define kParamOutputQuality        "quality"
#define kParamOutputQualityLabel   "Quality"

#define kParamOutputOrientation        "orientation"
#define kParamOutputOrientationLabel   "Orientation"

/*
 TIFF defines these values:

 1 = The 0th row represents the visual top of the image, and the 0th column represents the visual left-hand side.
 2 = The 0th row represents the visual top of the image, and the 0th column represents the visual right-hand side.
 3 = The 0th row represents the visual bottom of the image, and the 0th column represents the visual right-hand side.
 4 = The 0th row represents the visual bottom of the image, and the 0th column represents the visual left-hand side.
 5 = The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual top.
 6 = The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual top.
 7 = The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual bottom.
 8 = The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual bottom. 
 */
#define kParamOutputOrientationNormal                "normal"
#define kParamOutputOrientationFlop                  "flop"
#define kParamOutputOrientationR180                  "180"
#define kParamOutputOrientationFlip                  "flip"
#define kParamOutputOrientationTransposed            "transposed"
#define kParamOutputOrientationR90Clockwise          "90clockwise"
#define kParamOutputOrientationTransverse            "transverse"
#define kParamOutputOrientationR90CounterClockwise   "90counter-clockwise"


#define kParamOutputCompression        "compression"
#define kParamOutputCompressionLabel   "Compression"

#define kParamOutputCompressionAuto        "default"
#define kParamOutputCompressionAutoLabel     "Guess from the output format"
#define kParamOutputCompressionNone        "none"
#define kParamOutputCompressionNoneLabel     "No compression [EXR, TIFF, IFF]"
#define kParamOutputCompressionZip         "zip"
#define kParamOutputCompressionZipLabel      "Zlib/Deflate compression (lossless) [EXR, TIFF, Zfile]"
#define kParamOutputCompressionZips        "zips"
#define kParamOutputCompressionZipsLabel     "Zlib compression (lossless), one scan line at a time [EXR]"
#define kParamOutputCompressionRle         "rle"
#define kParamOutputCompressionRleLabel      "Run Length Encoding (lossless) [DPX, IFF, EXR, TGA, RLA]"
#define kParamOutputCompressionPiz         "piz"
#define kParamOutputCompressionPizLabel      "Piz-based wavelet compression [EXR]"
#define kParamOutputCompressionPxr24       "pxr24"
#define kParamOutputCompressionPxr24Label    "Lossy 24bit float compression [EXR]"
#define kParamOutputCompressionB44         "b44"
#define kParamOutputCompressionB44Label      "Lossy 4-by-4 pixel block compression, fixed compression rate [EXR]"
#define kParamOutputCompressionB44a        "b44a"
#define kParamOutputCompressionB44aLabel     "Lossy 4-by-4 pixel block compression, flat fields are compressed more [EXR]"
#define kParamOutputCompressionLZW         "lzw"
#define kParamOutputCompressionLZWLabel      "Lempel-Ziv Welsch compression (lossless) [TIFF]"
#define kParamOutputCompressionCCITTRLE    "ccittrle"
#define kParamOutputCompressionCCITTRLELabel "CCITT modified Huffman RLE (lossless) [TIFF]"
#define kParamOutputCompressionPACKBITS    "packbits"
#define kParamOutputCompressionPACKBITSLabel "Macintosh RLE (lossless) [TIFF]"

enum EParamCompression
{
	eParamCompressionAuto = 0,
    eParamCompressionNone,
	eParamCompressionZip,
	eParamCompressionZips,
	eParamCompressionRle,
	eParamCompressionPiz,
	eParamCompressionPxr24,
	eParamCompressionB44,
	eParamCompressionB44a,
    eParamCompressionLZW,
    eParamCompressionCCITTRLE,
    eParamCompressionPACKBITS
};

WriteOIIOPlugin::WriteOIIOPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
{
  _bitDepth = fetchChoiceParam(kTuttlePluginBitDepth);
  _premult = fetchBooleanParam(kParamPremultiplied);
  _quality     = fetchIntParam(kParamOutputQuality);   
  _orientation = fetchChoiceParam(kParamOutputOrientation);
  _compression = fetchChoiceParam(kParamOutputCompression);
}


WriteOIIOPlugin::~WriteOIIOPlugin() {
    
}

void WriteOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    GenericWriterPlugin::changedParam(args, paramName);
}

/**
 * Deduce the best bitdepth when it hasn't been set by the user
 */
static
ETuttlePluginBitDepth getDefaultBitDepth(const std::string& filepath, ETuttlePluginBitDepth bitDepth)
{
	if (bitDepth != eTuttlePluginBitDepthAuto) {
        return bitDepth;
    }
    std::string format = Filesystem::extension (filepath);
    if (format.find("exr") != std::string::npos || format.find("hdr") != std::string::npos || format.find("rgbe") != std::string::npos) {
        return eTuttlePluginBitDepth32f;
    } else if(format.find("jpg") != std::string::npos || format.find("jpeg") != std::string::npos ||
              format.find("bmp") != std::string::npos || format.find("dds") != std::string::npos  ||
              format.find("ico") != std::string::npos || format.find("jfi") != std::string::npos  ||
              format.find("pgm") != std::string::npos || format.find("pnm") != std::string::npos  ||
              format.find("ppm") != std::string::npos || format.find("pbm") != std::string::npos  ||
              format.find("pic") != std::string::npos) {
        return eTuttlePluginBitDepth8;
    } else {
        //bmp, cin, dpx, fits, j2k, j2c, jp2, jpe, png, sgi, tga, tif, tiff, tpic, webp
        return eTuttlePluginBitDepth16;
    }

    return bitDepth;
}

void WriteOIIOPlugin::onOutputFileChanged(const std::string &filename) {
    ///uncomment to use OCIO meta-data as a hint to set the correct color-space for the file.

#ifdef OFX_IO_USING_OCIO
	size_t sizeOfChannel = 0;
	int    bitsPerSample  = 0;

	int finalBitDepth_i;
    _bitDepth->getValue(finalBitDepth_i);
    ETuttlePluginBitDepth finalBitDepth = getDefaultBitDepth(filename, (ETuttlePluginBitDepth)finalBitDepth_i);

    if (finalBitDepth == eTuttlePluginBitDepth64f || finalBitDepth == eTuttlePluginBitDepth32f || finalBitDepth == eTuttlePluginBitDepth16f) {
        _ocio->setOutputColorspace("scene_linear");
    } else {
        if (_ocio->hasColorspace("sRGB")) {
            // nuke-default
            _ocio->setOutputColorspace("sRGB");
        } else if (_ocio->hasColorspace("rrt_srgb")) {
            // rrt_srgb in aces
            _ocio->setOutputColorspace("rrt_srgb");
        } else if (_ocio->hasColorspace("srgb8")) {
            // srgb8 in spi-vfx
            _ocio->setOutputColorspace("srgb8");
        }
    }
#endif
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
    
	OpenImageIO::TypeDesc oiioBitDepth;
	size_t sizeOfChannel = 0;
	int    bitsPerSample  = 0;

	int finalBitDepth_i;
    _bitDepth->getValue(finalBitDepth_i);
    ETuttlePluginBitDepth finalBitDepth = getDefaultBitDepth(filename,(ETuttlePluginBitDepth)finalBitDepth_i);

	switch (finalBitDepth) {
		case eTuttlePluginBitDepthAuto:
            OFX::throwSuiteStatusException(kOfxStatErrUnknown);
		case eTuttlePluginBitDepth8:
			oiioBitDepth = TypeDesc::UINT8;
			bitsPerSample = 8;
			sizeOfChannel = 1;
			break;
		case eTuttlePluginBitDepth10:
			oiioBitDepth = TypeDesc::UINT16;
			bitsPerSample = 10;
			sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth12:
			oiioBitDepth = TypeDesc::UINT16;
			bitsPerSample = 12;
			sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth16:
			oiioBitDepth = TypeDesc::UINT16;
			bitsPerSample = 16;
			sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth16f:
			oiioBitDepth = TypeDesc::HALF;
			bitsPerSample = 16;
			sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth32:
			oiioBitDepth = TypeDesc::UINT32;
			bitsPerSample = 32;
			sizeOfChannel = 4;
			break;
		case eTuttlePluginBitDepth32f:
			oiioBitDepth = TypeDesc::FLOAT;
			bitsPerSample = 32;
			sizeOfChannel = 4;
			break;
		case eTuttlePluginBitDepth64:
			oiioBitDepth = TypeDesc::UINT64;
			bitsPerSample = 64;
			sizeOfChannel = 8;
			break;
		case eTuttlePluginBitDepth64f:
			oiioBitDepth = TypeDesc::DOUBLE;
			bitsPerSample = 64;
			sizeOfChannel = 8;
			break;
	}
    ImageSpec spec (bounds.x2 - bounds.x1, bounds.y2 - bounds.y1, numChannels, oiioBitDepth);


    bool premultiply;
    _premult->getValue(premultiply);
    int quality;
    _quality->getValue(quality);
    int orientation;
    _orientation->getValue(orientation);
    int compression_i;
    _compression->getValue(compression_i);
    std::string compression;
    
    switch ((EParamCompression)compression_i) {
        case eParamCompressionAuto:
            break;
        case eParamCompressionNone: // EXR, TIFF, IFF
            compression = "none";
            break;
        case eParamCompressionZip: // EXR, TIFF, Zfile
            compression = "zip";
            break;
        case eParamCompressionZips: // EXR
            compression = "zips";
            break;
        case eParamCompressionRle: // DPX, IFF, EXR, TGA, RLA
            compression = "rle";
        case eParamCompressionPiz: // EXR
            compression = "piz";
            break;
        case eParamCompressionPxr24: // EXR
            compression = "pxr24";
            break;
        case eParamCompressionB44: // EXR
            compression = "b44";
            break;
        case eParamCompressionB44a: // EXR
            compression = "b44a";
            break;
        case eParamCompressionLZW: // TIFF
            compression = "lzw";
            break;
        case eParamCompressionCCITTRLE: // TIFF
            compression = "ccittrle";
            break;
        case eParamCompressionPACKBITS: // TIFF
            compression = "packbits";
            break;
    }

	spec.attribute("oiio:BitsPerSample", bitsPerSample);
	spec.attribute("oiio:UnassociatedAlpha", premultiply);
#ifdef OFX_IO_USING_OCIO
    std::string ocioColorspace = _ocio->getOutputColorspace();
    float gamma = 0.;
    std::string colorSpaceStr;
    if (ocioColorspace == "Gamma1.8") {
        // Gamma1.8 in nuke-default
       colorSpaceStr = "GammaCorrected";
        gamma = 1.8;
    } else if (ocioColorspace == "Gamma2.2" || ocioColorspace == "vd8" || ocioColorspace == "vd10" || ocioColorspace == "vd16") {
        // Gamma2.2 in nuke-default
        // vd8, vd10, vd16 in spi-anim and spi-vfx
        colorSpaceStr = "GammaCorrected";
        gamma = 2.2;
    } else if (ocioColorspace == "sRGB" || ocioColorspace == "rrt_srgb" || ocioColorspace == "srgb8") {
        // sRGB in nuke-default
        // rrt_srgb in aces
        // srgb8 in spi-vfx
        colorSpaceStr = "sRGB";
    } else if (ocioColorspace == "Rec709" || ocioColorspace == "rrt_rec709" || ocioColorspace == "hd10") {
        // Rec709 in nuke-default
        // rrt_rec709 in aces
        // hd10 in spi-anim and spi-vfx
        colorSpaceStr = "Rec709";
    } else if(ocioColorspace == "lg10") {
        // lg10 in spi-vfx
        colorSpaceStr = "KodakLog";
    } else if(ocioColorspace == "linear" || ocioColorspace == "aces" || ocioColorspace == "lnf" || ocioColorspace == "ln16") {
        // linear in nuke-default
        // aces in aces
        // lnf, ln16 in spi-anim and spi-vfx
        colorSpaceStr = "Linear";
    } else if(ocioColorspace == "raw" || ocioColorspace == "ncf") {
        // raw in nuke-default
        // raw in aces
        // ncf in spi-anim and spi-vfx
        // leave empty
    } else {
        //unknown color-space, don't do anything
    }
    if (!colorSpaceStr.empty()) {
        spec.attribute("oiio:ColorSpace", colorSpaceStr);
    }
    if (gamma != 0.) {
        spec.attribute("oiio:Gamma", gamma);
    }
#endif
	spec.attribute("CompressionQuality", quality);
	spec.attribute("Orientation", orientation + 1);
    if (!compression.empty()) { // some formats have a good value for the default compression
        spec.attribute("compression", compression);
    }

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
                                TypeDesc::FLOAT, //datatype
                                (char*)pixelData + (spec.height - 1) * rowBytes, //invert y
                                AutoStride, //xstride
                                -rowBytes, //ystride
                                AutoStride //zstride
                                );
    } else {
        output->write_image(TypeDesc::FLOAT,
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
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(), true, false, false, "reference", "reference");

    OFX::ChoiceParamDescriptor* bitDepth = desc.defineChoiceParam(kTuttlePluginBitDepth);
    bitDepth->setLabels(kTuttlePluginBitDepthLabel, kTuttlePluginBitDepthLabel, kTuttlePluginBitDepthLabel);
    bitDepth->appendOption(kTuttlePluginBitDepthAuto, kTuttlePluginBitDepthAutoLabel);
    bitDepth->appendOption(kTuttlePluginBitDepth8, kTuttlePluginBitDepth8Label);
    bitDepth->appendOption(kTuttlePluginBitDepth10, kTuttlePluginBitDepth10Label);
    bitDepth->appendOption(kTuttlePluginBitDepth12, kTuttlePluginBitDepth12Label);
    bitDepth->appendOption(kTuttlePluginBitDepth16, kTuttlePluginBitDepth16Label);
    bitDepth->appendOption(kTuttlePluginBitDepth16f, kTuttlePluginBitDepth16fLabel);
    bitDepth->appendOption(kTuttlePluginBitDepth32, kTuttlePluginBitDepth32Label);
    bitDepth->appendOption(kTuttlePluginBitDepth32f, kTuttlePluginBitDepth32fLabel);
    bitDepth->appendOption(kTuttlePluginBitDepth64, kTuttlePluginBitDepth64Label);
    bitDepth->appendOption(kTuttlePluginBitDepth64f, kTuttlePluginBitDepth64fLabel);
    bitDepth->setDefault(eTuttlePluginBitDepthAuto);

    OFX::BooleanParamDescriptor* premult = desc.defineBooleanParam(kParamPremultiplied);
    premult->setLabels(kParamPremultipliedLabel, kParamPremultipliedLabel, kParamPremultipliedLabel);
    premult->setDefault(false);

    OFX::IntParamDescriptor* quality = desc.defineIntParam(kParamOutputQuality);
    quality->setLabels(kParamOutputQualityLabel, kParamOutputQualityLabel, kParamOutputQualityLabel);
    quality->setRange(0, 100);
    quality->setDisplayRange(0, 100);
    quality->setDefault(80);

    OFX::ChoiceParamDescriptor* orientation = desc.defineChoiceParam(kParamOutputOrientation);
    orientation->setLabels(kParamOutputOrientationLabel, kParamOutputOrientationLabel, kParamOutputOrientationLabel);
    orientation->appendOption(kParamOutputOrientationNormal);
    orientation->appendOption(kParamOutputOrientationFlop);
    orientation->appendOption(kParamOutputOrientationR180);
    orientation->appendOption(kParamOutputOrientationFlip);
    orientation->appendOption(kParamOutputOrientationTransposed);
    orientation->appendOption(kParamOutputOrientationR90Clockwise);
    orientation->appendOption(kParamOutputOrientationTransverse);
    orientation->appendOption(kParamOutputOrientationR90CounterClockwise);
    orientation->setDefault(0);

    OFX::ChoiceParamDescriptor* compression = desc.defineChoiceParam(kParamOutputCompression);
    compression->setLabels(kParamOutputCompressionLabel, kParamOutputCompressionLabel, kParamOutputCompressionLabel);
    compression->setHint("Compression quality [JPEG, WEBP]");
    compression->appendOption(kParamOutputCompressionAuto, kParamOutputCompressionAutoLabel);
    compression->appendOption(kParamOutputCompressionNone, kParamOutputCompressionNoneLabel);
    compression->appendOption(kParamOutputCompressionZip, kParamOutputCompressionZipLabel);
    compression->appendOption(kParamOutputCompressionZips, kParamOutputCompressionZipsLabel);
    compression->appendOption(kParamOutputCompressionRle, kParamOutputCompressionRleLabel);
    compression->appendOption(kParamOutputCompressionPiz, kParamOutputCompressionPizLabel);
    compression->appendOption(kParamOutputCompressionPxr24, kParamOutputCompressionPxr24Label);
    compression->appendOption(kParamOutputCompressionB44, kParamOutputCompressionB44Label);
    compression->appendOption(kParamOutputCompressionB44a, kParamOutputCompressionB44aLabel);
    compression->appendOption(kParamOutputCompressionLZW, kParamOutputCompressionLZWLabel);
    compression->appendOption(kParamOutputCompressionCCITTRLE, kParamOutputCompressionCCITTRLELabel);
    compression->appendOption(kParamOutputCompressionPACKBITS, kParamOutputCompressionPACKBITSLabel);
    compression->setDefault(eParamCompressionAuto);

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new WriteOIIOPlugin(handle);
}
