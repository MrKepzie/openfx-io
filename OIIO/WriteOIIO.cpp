/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2015 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX oiio Writer plugin.
 * Writes an image using the OpenImageIO library.
 */

#include "WriteOIIO.h"

#include "ofxsMacros.h"

GCC_DIAG_OFF(unused-parameter)
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/filesystem.h>
GCC_DIAG_ON(unused-parameter)

#include "GenericOCIO.h"
#include "GenericWriter.h"

OIIO_NAMESPACE_USING

#define kPluginName "WriteOIIOOFX"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write images using OpenImageIO."
#define kPluginIdentifier "fr.inria.openfx.WriteOIIO"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamBitDepth    "bitDepth"
#define kParamBitDepthLabel   "Bit Depth"
#define kParamBitDepthHint \
"Number of bits per sample in the file [TIFF,DPX,TGA,DDS,ICO,IFF,PNM,PIC]."

#define kParamBitDepthOptionAuto     "auto"
#define kParamBitDepthOptionAutoHint "Guess from the output format"
//#define kParamBitDepthNone     "none"
#define kParamBitDepthOption8      "8i"
#define kParamBitDepthOption8Hint   "8  bits integer"
#define kParamBitDepthOption10     "10i"
#define kParamBitDepthOption10Hint  "10 bits integer"
#define kParamBitDepthOption12     "12i"
#define kParamBitDepthOption12Hint  "12 bits integer"
#define kParamBitDepthOption16     "16i"
#define kParamBitDepthOption16Hint  "16 bits integer"
#define kParamBitDepthOption16f    "16f"
#define kParamBitDepthOption16fHint "16 bits floating point"
#define kParamBitDepthOption32     "32i"
#define kParamBitDepthOption32Hint  "32 bits integer"
#define kParamBitDepthOption32f    "32f"
#define kParamBitDepthOption32fHint "32 bits floating point"
#define kParamBitDepthOption64     "64i"
#define kParamBitDepthOption64Hint  "64 bits integer"
#define kParamBitDepthOption64f    "64f"
#define kParamBitDepthOption64fHint "64 bits floating point"

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

#define kParamOutputQualityName    "quality"
#define kParamOutputQualityLabel   "Quality"
#define kParamOutputQualityHint \
"Indicates the quality of compression to use (0–100), for those plugins and compression methods that allow a variable amount of compression, with higher numbers indicating higher image fidelity."

#define kParamOutputOrientationName    "orientation"
#define kParamOutputOrientationLabel   "Orientation"
#define kParamOutputOrientationHint \
"The orientation of the image data [DPX,TIFF,JPEG,HDR,FITS].\n" \
"By default, image pixels are ordered from the top of the display to the bottom, " \
"and within each scanline, from left to right (i.e., the same ordering as English " \
"text and scan progression on a CRT). But the \"Orientation\" parameter can " \
"suggest that it should be displayed with a different orientation, according to " \
"the TIFF/EXIF conventions."
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
#define kParamOutputOrientationNormalHint              "normal (top to bottom, left to right)"
#define kParamOutputOrientationFlop                  "flop"
#define kParamOutputOrientationFlopHint                "flipped horizontally (top to bottom, right to left)"
#define kParamOutputOrientationR180                  "180"
#define kParamOutputOrientationR180Hint                "rotate 180deg (bottom to top, right to left)"
#define kParamOutputOrientationFlip                  "flip"
#define kParamOutputOrientationFlipHint                "flipped vertically (bottom to top, left to right)"
#define kParamOutputOrientationTransposed            "transposed"
#define kParamOutputOrientationTransposedHint          "transposed (left to right, top to bottom)"
#define kParamOutputOrientationR90Clockwise          "90clockwise"
#define kParamOutputOrientationR90ClockwiseHint        "rotated 90deg clockwise (right to left, top to bottom)"
#define kParamOutputOrientationTransverse            "transverse"
#define kParamOutputOrientationTransverseHint          "transverse (right to left, bottom to top)"
#define kParamOutputOrientationR90CounterClockwise   "90counter-clockwise"
#define kParamOutputOrientationR90CounterClockwiseHint "rotated 90deg counter-clockwise (left to right, bottom to top)"
enum EOutputOrientation
{
    eOutputOrientationNormal = 0,
    eOutputOrientationFlop,
    eOutputOrientationR180,
    eOutputOrientationFlip,
    eOutputOrientationTransposed,
    eOutputOrientationR90Clockwise,
    eOutputOrientationTransverse,
    eOutputOrientationR90CounterClockwise,
};

#define kParamOutputCompressionName    "compression"
#define kParamOutputCompressionLabel   "Compression"
#define kParamOutputCompressionHint \
"Compression type [TIFF,EXR,DDS,IFF,SGI,TGA]\n" \
"Indicates the type of compression the file uses. Supported compression modes will vary from format to format. " \
"As an example, the TIFF format supports \"none\", \"lzw\", \"ccittrle\", \"zip\" (the default), \"packbits\", "\
"and the EXR format supports \"none\", \"rle\", \"zip\" (the default), \"piz\", \"pxr24\", \"b44\", or \"b44a\"."

#define kParamOutputCompressionOptionAuto        "default"
#define kParamOutputCompressionOptionAutoHint     "Guess from the output format"
#define kParamOutputCompressionOptionNone        "none"
#define kParamOutputCompressionOptionNoneHint     "No compression [EXR, TIFF, IFF]"
#define kParamOutputCompressionOptionZip         "zip"
#define kParamOutputCompressionOptionZipHint      "Zlib/Deflate compression (lossless) [EXR, TIFF, Zfile]"
#define kParamOutputCompressionOptionZips        "zips"
#define kParamOutputCompressionOptionZipsHint     "Zlib compression (lossless), one scan line at a time [EXR]"
#define kParamOutputCompressionOptionRle         "rle"
#define kParamOutputCompressionOptionRleHint      "Run Length Encoding (lossless) [DPX, IFF, EXR, TGA, RLA]"
#define kParamOutputCompressionOptionPiz         "piz"
#define kParamOutputCompressionOptionPizHint      "Piz-based wavelet compression [EXR]"
#define kParamOutputCompressionOptionPxr24       "pxr24"
#define kParamOutputCompressionOptionPxr24Hint    "Lossy 24bit float compression [EXR]"
#define kParamOutputCompressionOptionB44         "b44"
#define kParamOutputCompressionOptionB44Hint      "Lossy 4-by-4 pixel block compression, fixed compression rate [EXR]"
#define kParamOutputCompressionOptionB44a        "b44a"
#define kParamOutputCompressionOptionB44aHint     "Lossy 4-by-4 pixel block compression, flat fields are compressed more [EXR]"
#define kParamOutputCompressionOptionLZW         "lzw"
#define kParamOutputCompressionOptionLZWHint      "Lempel-Ziv Welsch compression (lossless) [TIFF]"
#define kParamOutputCompressionOptionCCITTRLE    "ccittrle"
#define kParamOutputCompressionOptionCCITTRLEHint "CCITT modified Huffman RLE (lossless) [TIFF]"
#define kParamOutputCompressionOptionPACKBITS    "packbits"
#define kParamOutputCompressionOptionPACKBITSHint "Macintosh RLE (lossless) [TIFF]"

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

#define kParamTileSize "tileSize"
#define kParamTileSizeLabel "Tile Size"
#define kParamTileSizeHint "Size of a tile in the output file for formats that support tiles. If Untiled, the whole image will have a single tile."

enum EParamTileSize
{
    eParamTileSizeUntiled = 0,
    eParamTileSize64,
    eParamTileSize128,
    eParamTileSize256,
    eParamTileSize512
};

class WriteOIIOPlugin : public GenericWriterPlugin
{
public:
    WriteOIIOPlugin(OfxImageEffectHandle handle);

    virtual ~WriteOIIOPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    virtual void onOutputFileChanged(const std::string& filename) OVERRIDE FINAL;

    virtual void encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, float pixelAspectRatio, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;
    
    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return OFX::eImagePreMultiplied; }

    virtual bool displayWindowSupportedByFormat(const std::string& filename) const OVERRIDE FINAL;

    
private:
    OFX::ChoiceParam* _bitDepth;
    OFX::IntParam* _quality;
    OFX::ChoiceParam* _orientation;
    OFX::ChoiceParam* _compression;
    OFX::ChoiceParam* _tileSize;
};

WriteOIIOPlugin::WriteOIIOPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
{
    _bitDepth = fetchChoiceParam(kParamBitDepth);
    _quality     = fetchIntParam(kParamOutputQualityName);
    _orientation = fetchChoiceParam(kParamOutputOrientationName);
    _compression = fetchChoiceParam(kParamOutputCompressionName);
    _tileSize = fetchChoiceParam(kParamTileSize);
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

bool
WriteOIIOPlugin::displayWindowSupportedByFormat(const std::string& filename) const
{
    std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
    if (output.get()) {
        return output->supports("displaywindow");
    } else {
        return false;
    }
}

void WriteOIIOPlugin::onOutputFileChanged(const std::string &filename) {
    ///uncomment to use OCIO meta-data as a hint to set the correct color-space for the file.

#ifdef OFX_IO_USING_OCIO
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
    
    std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
    if (output.get()) {
        _tileSize->setIsSecret(!output->supports("tiles"));
    } else {
        _tileSize->setIsSecret(false);
    }
}

void WriteOIIOPlugin::encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, float pixelAspectRatio, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OIIO: can only write RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    int numChannels = 0;
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
            return;
    }

    std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
    if (!output.get()) {
        // output is NULL
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("Cannot create output file ")+filename);
        return;
    }
    
	OpenImageIO::TypeDesc oiioBitDepth;
	//size_t sizeOfChannel = 0;
	int    bitsPerSample  = 0;

	int finalBitDepth_i;
    _bitDepth->getValue(finalBitDepth_i);
    ETuttlePluginBitDepth finalBitDepth = getDefaultBitDepth(filename,(ETuttlePluginBitDepth)finalBitDepth_i);

	switch (finalBitDepth) {
		case eTuttlePluginBitDepthAuto:
            OFX::throwSuiteStatusException(kOfxStatErrUnknown);
            return;
		case eTuttlePluginBitDepth8:
			oiioBitDepth = TypeDesc::UINT8;
			bitsPerSample = 8;
			//sizeOfChannel = 1;
			break;
		case eTuttlePluginBitDepth10:
			oiioBitDepth = TypeDesc::UINT16;
			bitsPerSample = 10;
			//sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth12:
			oiioBitDepth = TypeDesc::UINT16;
			bitsPerSample = 12;
			//sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth16:
			oiioBitDepth = TypeDesc::UINT16;
			bitsPerSample = 16;
			//sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth16f:
			oiioBitDepth = TypeDesc::HALF;
			bitsPerSample = 16;
			//sizeOfChannel = 2;
			break;
		case eTuttlePluginBitDepth32:
			oiioBitDepth = TypeDesc::UINT32;
			bitsPerSample = 32;
			//sizeOfChannel = 4;
			break;
		case eTuttlePluginBitDepth32f:
			oiioBitDepth = TypeDesc::FLOAT;
			bitsPerSample = 32;
			//sizeOfChannel = 4;
			break;
		case eTuttlePluginBitDepth64:
			oiioBitDepth = TypeDesc::UINT64;
			bitsPerSample = 64;
			//sizeOfChannel = 8;
			break;
		case eTuttlePluginBitDepth64f:
			oiioBitDepth = TypeDesc::DOUBLE;
			bitsPerSample = 64;
			//sizeOfChannel = 8;
			break;
	}
    ImageSpec spec (bounds.x2 - bounds.x1, bounds.y2 - bounds.y1, numChannels, oiioBitDepth);


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
            break;
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
    // oiio:UnassociatedAlpha should be set if the data buffer in unassociated/unpremultiplied.
    // However, WriteOIIO::getExpectedInputPremultiplication() stated that input to the encode()
    // function should always be premultiplied/associated
	//spec.attribute("oiio:UnassociatedAlpha", premultiply);
#ifdef OFX_IO_USING_OCIO
    std::string ocioColorspace;
    _ocio->getOutputColorspaceAtTime(time, ocioColorspace);
    float gamma = 0.f;
    std::string colorSpaceStr;
    if (ocioColorspace == "Gamma1.8") {
        // Gamma1.8 in nuke-default
        colorSpaceStr = "GammaCorrected";
        gamma = 1.8f;
    } else if (ocioColorspace == "Gamma2.2" || ocioColorspace == "vd8" || ocioColorspace == "vd10" || ocioColorspace == "vd16" || ocioColorspace == "VD16") {
        // Gamma2.2 in nuke-default
        // vd8, vd10, vd16 in spi-anim and spi-vfx
        // VD16 in blender
        colorSpaceStr = "GammaCorrected";
        gamma = 2.2f;
    } else if (ocioColorspace == "sRGB" || ocioColorspace == "sRGB (D60 sim.)" || ocioColorspace == "out_srgbd60sim" || ocioColorspace == "rrt_srgb" || ocioColorspace == "srgb8") {
        // sRGB in nuke-default and blender
        // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
        // rrt_srgb in aces
        // srgb8 in spi-vfx
        colorSpaceStr = "sRGB";
    } else if (ocioColorspace == "Rec709" || ocioColorspace == "nuke_rec709" || ocioColorspace == "Rec.709 - Full" || ocioColorspace == "out_rec709full" || ocioColorspace == "rrt_rec709" || ocioColorspace == "hd10") {
        // Rec709 in nuke-default
        // nuke_rec709 in blender
        // out_rec709full or "Rec.709 - Full" in aces 1.0.0
        // rrt_rec709 in aces
        // hd10 in spi-anim and spi-vfx
        colorSpaceStr = "Rec709";
    } else if (ocioColorspace == "KodakLog" || ocioColorspace == "Cineon" || ocioColorspace == "REDlogFilm" || ocioColorspace == "lg10") {
        // Cineon in nuke-default
        // REDlogFilm in aces 1.0.0
        // lg10 in spi-vfx and blender
        colorSpaceStr = "KodakLog";
    } else if (ocioColorspace == "Linear" || ocioColorspace == "linear" || ocioColorspace == "ACES2065-1" || ocioColorspace == "aces" || ocioColorspace == "lnf" || ocioColorspace == "ln16") {
        // linear in nuke-default
        // ACES2065-1 in aces 1.0.0
        // aces in aces
        // lnf, ln16 in spi-anim and spi-vfx
        colorSpaceStr = "Linear";
    } else if (ocioColorspace == "raw" || ocioColorspace == "Raw" || ocioColorspace == "ncf") {
        // raw in nuke-default
        // raw in aces
        // Raw in blender
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
    if (pixelAspectRatio != 1.) {
        spec.attribute("PixelAspectRatio", pixelAspectRatio);
    }

    // by default, the channel names are R, G, B, A, which is OK except for Alpha images
    if (pixelComponents == OFX::ePixelComponentAlpha) {
        spec.channelnames.clear();
        spec.channelnames.push_back ("A");
        spec.alpha_channel = 0;
    }
    
    if (output->supports("tiles")) {
        spec.x = bounds.x1;
        spec.y = bounds.y1;
        spec.full_x = bounds.x1;
        spec.full_y = bounds.y1;
        
        bool clipToProject = true;
        if (_clipToProject && !_clipToProject->getIsSecret()) {
            _clipToProject->getValue(clipToProject);
        }
        if (!clipToProject) {
            //Spec has already been set to bounds which are the input RoD, so post-fix by setting display window to project size
            OfxPointD size = getProjectSize();
            OfxPointD offset = getProjectOffset();
            spec.full_x = offset.x;
            spec.full_y = offset.y;
            spec.full_width = size.x;
            spec.full_height = size.y;
        }
        
        int tileSize_i;
        _tileSize->getValue(tileSize_i);
        EParamTileSize tileSizeE = (EParamTileSize)tileSize_i;
        switch (tileSizeE) {
            case eParamTileSize64:
                spec.tile_width = std::min(64,spec.full_width);
                spec.tile_height = std::min(64,spec.full_height);
                break;
            case eParamTileSize128:
                spec.tile_width = std::min(128,spec.full_width);
                spec.tile_height = std::min(128,spec.full_height);
                break;
            case eParamTileSize256:
                spec.tile_width = std::min(256,spec.full_width);
                spec.tile_height = std::min(256,spec.full_height);
                break;
            case eParamTileSize512:
                spec.tile_width = std::min(512,spec.full_width);
                spec.tile_height = std::min(512,spec.full_height);
                break;
            case eParamTileSizeUntiled:
            default:
                break;
        }
    }
    
    if (!output->open(filename, spec)) {
        setPersistentMessage(OFX::Message::eMessageError, "", output->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    output->write_image(TypeDesc::FLOAT,
                        (char*)pixelData + (spec.height - 1) * rowBytes, //invert y
                        AutoStride, //xstride
                        -rowBytes, //ystride
                        AutoStride //zstride
                        );
    
    
    output->close();
}

bool WriteOIIOPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}


using namespace OFX;

static std::string oiio_versions()
{
    std::ostringstream oss;
    int ver = openimageio_version();
    oss << "OIIO versions:" << std::endl;
    oss << "compiled with " << OIIO_VERSION_STRING << std::endl;
    oss << "running with " << ver/10000 << '.' << (ver%10000)/100 << '.' << (ver%100) << std::endl;
    return oss.str();
}

mDeclareWriterPluginFactory(WriteOIIOPluginFactory, {}, {}, false);

/** @brief The basic describe function, passed a plugin descriptor */
void WriteOIIOPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc,OFX::eRenderFullySafe);
    
    if (!attribute("threads", 1)) {
#     ifdef DEBUG
        std::cerr << "Failed to set the number of threads for OIIO" << std::endl;
#     endif
    }

    std::string extensions_list;
    getattribute("extension_list", extensions_list);

    std::string extensions_pretty;
    {
        std::stringstream formatss(extensions_list);
        std::string format;
        std::vector<std::string> extensions;
        while (std::getline(formatss, format, ';')) {
            std::stringstream extensionss(format);
            std::string extension;
            std::getline(extensionss, extension, ':'); // extract the format
            extensions_pretty += extension;
            extensions_pretty += ": ";
            bool first = true;
            while (std::getline(extensionss, extension, ',')) {
                if (!first) {
                    extensions_pretty += ", ";
                }
                first = false;
                extensions_pretty += extension;
            }
            extensions_pretty += "; ";
        }
    }

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription("Write images using OpenImageIO.\n\n"
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
                              "All supported formats and extensions: " + extensions_pretty + "\n\n"
                              + oiio_versions());

#ifdef OFX_EXTENSIONS_TUTTLE
#if 0
    // hard-coded extensions list
    const char* extensions[] = { "bmp", "cin", /*"dds",*/ "dpx", /*"f3d",*/ "fits", "hdr", "ico", "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png", "pbm", "pgm", "ppm", "psd", "pdd", "psb", /*"ptex",*/ "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
#else
    // get extensions from OIIO (but there is no distinctions between readers and writers)
    std::vector<std::string> extensions;
    {
        std::stringstream formatss(extensions_list);
        std::string format;
        while (std::getline(formatss, format, ';')) {
            std::stringstream extensionss(format);
            std::string extension;
            std::getline(extensionss, extension, ':'); // extract the format
            while (std::getline(extensionss, extension, ',')) {
                extensions.push_back(extension);
            }
        }
    }

#endif
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(91);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WriteOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha,
                                                                    "reference", "reference", true);
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamTileSize);
        param->setLabel(kParamTileSizeLabel);
        param->setHint(kParamTileSizeHint);
        assert(param->getNOptions() == eParamTileSizeUntiled);
        param->appendOption("Untiled");
        assert(param->getNOptions() == eParamTileSize64);
        param->appendOption("64");
        assert(param->getNOptions() == eParamTileSize128);
        param->appendOption("128");
        assert(param->getNOptions() == eParamTileSize256);
        param->appendOption("256");
        assert(param->getNOptions() == eParamTileSize512);
        param->appendOption("512");
        param->setDefault(eParamTileSize256);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBitDepth);
        param->setLabel(kParamBitDepthLabel);
        param->setHint(kParamBitDepthHint);
        assert(param->getNOptions() == eTuttlePluginBitDepthAuto);
        param->appendOption(kParamBitDepthOptionAuto, kParamBitDepthOptionAutoHint);
        assert(param->getNOptions() == eTuttlePluginBitDepth8);
        param->appendOption(kParamBitDepthOption8, kParamBitDepthOption8Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth10);
        param->appendOption(kParamBitDepthOption10, kParamBitDepthOption10Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth12);
        param->appendOption(kParamBitDepthOption12, kParamBitDepthOption12Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth16);
        param->appendOption(kParamBitDepthOption16, kParamBitDepthOption16Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth16f);
        param->appendOption(kParamBitDepthOption16f, kParamBitDepthOption16fHint);
        assert(param->getNOptions() == eTuttlePluginBitDepth32);
        param->appendOption(kParamBitDepthOption32, kParamBitDepthOption32Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth32f);
        param->appendOption(kParamBitDepthOption32f, kParamBitDepthOption32fHint);
        assert(param->getNOptions() == eTuttlePluginBitDepth64);
        param->appendOption(kParamBitDepthOption64, kParamBitDepthOption64Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth64f);
        param->appendOption(kParamBitDepthOption64f, kParamBitDepthOption64fHint);
        param->setDefault(eTuttlePluginBitDepthAuto);
        page->addChild(*param);
    }
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamOutputQualityName);
        param->setLabel(kParamOutputQualityLabel);
        param->setHint(kParamOutputQualityHint);
        param->setRange(0, 100);
        param->setDisplayRange(0, 100);
        param->setDefault(80);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputOrientationName);
        param->setLabel(kParamOutputOrientationLabel);
        param->setHint(kParamOutputOrientationHint);
        assert(param->getNOptions() == eOutputOrientationNormal);
        param->appendOption(kParamOutputOrientationNormal, kParamOutputOrientationNormalHint);
        assert(param->getNOptions() == eOutputOrientationFlop);
        param->appendOption(kParamOutputOrientationFlop, kParamOutputOrientationFlopHint);
        assert(param->getNOptions() == eOutputOrientationR180);
        param->appendOption(kParamOutputOrientationR180, kParamOutputOrientationR180Hint);
        assert(param->getNOptions() == eOutputOrientationFlip);
        param->appendOption(kParamOutputOrientationFlip, kParamOutputOrientationFlipHint);
        assert(param->getNOptions() == eOutputOrientationTransposed);
        param->appendOption(kParamOutputOrientationTransposed, kParamOutputOrientationTransposedHint);
        assert(param->getNOptions() == eOutputOrientationR90Clockwise);
        param->appendOption(kParamOutputOrientationR90Clockwise, kParamOutputOrientationR90ClockwiseHint);
        assert(param->getNOptions() == eOutputOrientationTransverse);
        param->appendOption(kParamOutputOrientationTransverse, kParamOutputOrientationTransverseHint);
        assert(param->getNOptions() == eOutputOrientationR90CounterClockwise);
        param->appendOption(kParamOutputOrientationR90CounterClockwise, kParamOutputOrientationR90CounterClockwiseHint);
        param->setDefault(0);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputCompressionName);
        param->setLabel(kParamOutputCompressionLabel);
        param->setHint(kParamOutputCompressionHint);
        assert(param->getNOptions() == eParamCompressionAuto);
        param->appendOption(kParamOutputCompressionOptionAuto, kParamOutputCompressionOptionAutoHint);
        assert(param->getNOptions() == eParamCompressionNone);
        param->appendOption(kParamOutputCompressionOptionNone, kParamOutputCompressionOptionNoneHint);
        assert(param->getNOptions() == eParamCompressionZip);
        param->appendOption(kParamOutputCompressionOptionZip, kParamOutputCompressionOptionZipHint);
        assert(param->getNOptions() == eParamCompressionZips);
        param->appendOption(kParamOutputCompressionOptionZips, kParamOutputCompressionOptionZipsHint);
        assert(param->getNOptions() == eParamCompressionRle);
        param->appendOption(kParamOutputCompressionOptionRle, kParamOutputCompressionOptionRleHint);
        assert(param->getNOptions() == eParamCompressionPiz);
        param->appendOption(kParamOutputCompressionOptionPiz, kParamOutputCompressionOptionPizHint);
        assert(param->getNOptions() == eParamCompressionPxr24);
        param->appendOption(kParamOutputCompressionOptionPxr24, kParamOutputCompressionOptionPxr24Hint);
        assert(param->getNOptions() == eParamCompressionB44);
        param->appendOption(kParamOutputCompressionOptionB44, kParamOutputCompressionOptionB44Hint);
        assert(param->getNOptions() == eParamCompressionB44a);
        param->appendOption(kParamOutputCompressionOptionB44a, kParamOutputCompressionOptionB44aHint);
        assert(param->getNOptions() == eParamCompressionLZW);
        param->appendOption(kParamOutputCompressionOptionLZW, kParamOutputCompressionOptionLZWHint);
        assert(param->getNOptions() == eParamCompressionCCITTRLE);
        param->appendOption(kParamOutputCompressionOptionCCITTRLE, kParamOutputCompressionOptionCCITTRLEHint);
        assert(param->getNOptions() == eParamCompressionPACKBITS);
        param->appendOption(kParamOutputCompressionOptionPACKBITS, kParamOutputCompressionOptionPACKBITSHint);
        param->setDefault(eParamCompressionAuto);
        page->addChild(*param);
    }
    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WriteOIIOPlugin(handle);
}


void getWriteOIIOPluginID(OFX::PluginFactoryArray &ids)
{
    static WriteOIIOPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
