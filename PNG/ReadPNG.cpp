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
 * OFX PNG reader plugin.
 * Reads an image in the PNG format
 */

#include <cstdio> // fopen, fread...
#include <iomanip>
#include <locale>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <png.h> // includes setjmp.h
#include <zlib.h>

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#endif

#include "GenericReader.h"
#include "GenericOCIO.h"
#include "ofxsMacros.h"
#include "ofxsFileOpen.h"

using namespace OFX;
using namespace OFX::IO;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ReadPNG"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read PNG files."
#define kPluginIdentifier "fr.inria.openfx.ReadPNG"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 92 // better than ReadOIIO

#define kParamShowMetadata "showMetadata"
#define kParamShowMetadataLabel "Image Info..."
#define kParamShowMetadataHint "Shows information and metadata from the image at current time."


// All PNG images represent RGBA.
// Single-channel images are Y
// Two-channel images are Y+A
// Three-channel images are RGB
// Four-channel images are RGBA
// RGB may be compacted to Y and A may be removed when writing a PNG image.
// User can still use a Shuffle plugin to select just the Alpha channel.
//
// References:
// https://www.w3.org/TR/PNG/#4Concepts.RGBMerging
// https://www.w3.org/TR/PNG/#4Concepts.Alpha-indexing
// Issue:
// https://github.com/MrKepzie/openfx-io/issues/24

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsXY false
#define kSupportsAlpha false
#define kSupportsTiles false

#define OFX_IO_LIBPNG_VERSION (PNG_LIBPNG_VER_MAJOR*10000 + PNG_LIBPNG_VER_MINOR*100 + PNG_LIBPNG_VER_RELEASE)

// Try to deduce endianness
#if (defined(_WIN32) || defined(__i386__) || defined(__x86_64__))
#  ifndef __LITTLE_ENDIAN__
#    define __LITTLE_ENDIAN__ 1
#    undef __BIG_ENDIAN__
#  endif
#endif

inline bool littleendian (void)
{
#if defined(__BIG_ENDIAN__)
    return false;
#elif defined(__LITTLE_ENDIAN__)
    return true;
#else
    // Otherwise, do something quick to compute it
    int i = 1;
    return *((char *) &i);
#endif
}


/// Helper function - reads background colour.
///
inline bool
get_background (png_structp& sp,
                png_infop& ip,
                BitDepthEnum bitdepth,
                int real_bit_depth,
                int nChannels,
                float *red, float *green, float *blue)
{
    if (setjmp (png_jmpbuf (sp)))
        return false;
    if (! png_get_valid (sp, ip, PNG_INFO_bKGD))
        return false;

    png_color_16p bg;
    png_get_bKGD (sp, ip, &bg);
    if (bitdepth == eBitDepthUShort) {
        *red   = bg->red   / 65535.0;
        *green = bg->green / 65535.0;
        *blue  = bg->blue  / 65535.0;
    } else if (nChannels < 3 && real_bit_depth < 8) {
        if (real_bit_depth == 1)
            *red = *green = *blue = (bg->gray ? 1 : 0);
        else if (real_bit_depth == 2)
            *red = *green = *blue = bg->gray / 3.0;
        else // 4 bits
            *red = *green = *blue = bg->gray / 15.0;
    } else {
        *red   = bg->red   / 255.0;
        *green = bg->green / 255.0;
        *blue  = bg->blue  / 255.0;
    }
    return true;
}

enum PNGColorSpaceEnum
{
    ePNGColorSpaceLinear,
    ePNGColorSpacesRGB,
    ePNGColorSpaceRec709,
    ePNGColorSpaceGammaCorrected
};

/// Read information from a PNG file and fill the ImageSpec accordingly.
///
inline void
getPNGInfo(png_structp& sp,
           png_infop& ip,
           int* x1_p,
           int* y1_p,
           int* width_p,
           int* height_p,
           double* par_p,
           int* nChannels_p,
           BitDepthEnum* bit_depth_p,
           int* real_bit_depth_p,
           int* color_type_p,
           PNGColorSpaceEnum* colorspace_p, // optional
           double* gamma_p, // optional
           int* interlace_type_p, // optional
           OfxRGBColourF* bg_p, // optional
           unsigned char** iccprofile_data_p, // optional
           unsigned int* iccprofile_length_p, // optional
           bool* isResolutionInches_p, // optional
           double* xResolution_p, // optional
           double* yResolution_p, // optional
           std::string* date_p, // optional
           std::map<std::string, std::string>* additionalComments_p) // optional
{

    png_read_info (sp, ip);

    // Auto-convert 1-, 2-, and 4- bit images to 8 bits, palette to RGB,
    // and transparency to alpha.
    png_set_expand (sp);

    /* Expand the grayscale to 24-bit RGB if necessary. */
    png_set_gray_to_rgb(sp);

    // PNG files are naturally big-endian
    if (littleendian()) {
        png_set_swap (sp);
    }
    png_set_interlace_handling(sp);

    png_read_update_info (sp, ip);


    png_uint_32 width, height;
    png_get_IHDR (sp, ip, &width, &height,
                  real_bit_depth_p, color_type_p, NULL, NULL, NULL);
    *width_p = width;
    *height_p = height;
    *bit_depth_p = *real_bit_depth_p == 16 ? eBitDepthUShort : eBitDepthUByte;
    png_byte channels = png_get_channels (sp, ip);
#ifndef NDEBUG
    png_byte color_type = png_get_color_type(sp, ip);
    assert((channels == 1 && (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)) ||
           (channels == 2 && (color_type == PNG_COLOR_TYPE_GRAY_ALPHA)) ||
           (channels == 3 && (color_type == PNG_COLOR_TYPE_RGB)) ||
           (channels == 4 && (color_type == PNG_COLOR_TYPE_RGB_ALPHA || color_type == PNG_COLOR_TYPE_RGB)));
#endif
    *nChannels_p = channels;

    *x1_p = png_get_x_offset_pixels (sp, ip);
    *y1_p = png_get_y_offset_pixels (sp, ip);

    float aspect = (float)png_get_pixel_aspect_ratio (sp, ip);
    *par_p = 1.;
    if (aspect != 0 && aspect != 1) {
        *par_p = aspect;
    }

    if (colorspace_p) {
        *colorspace_p = ePNGColorSpaceLinear;

        // is there a srgb intent ?
        int srgb_intent;
        if (png_get_sRGB (sp, ip, &srgb_intent) == PNG_INFO_sRGB) {
            *colorspace_p = ePNGColorSpacesRGB;
        }

        // if not is there a gamma ?
        assert(gamma_p);
        bool gotGamma = false;
        if (*colorspace_p == ePNGColorSpaceLinear) {
            if (!png_get_gAMA (sp, ip, gamma_p)) {
                *gamma_p = 1.0;
            } else {
                // guard against division by zero
                *gamma_p = *gamma_p ? (1. / *gamma_p) : 1.;
                gotGamma = true;
                if (*gamma_p > 1.) {
                    *colorspace_p = ePNGColorSpaceGammaCorrected;
                }
            }
        }

        // if not, deduce from the bitdepth
        if (!gotGamma && *colorspace_p == ePNGColorSpaceLinear) {
            *colorspace_p = *bit_depth_p == eBitDepthUByte ? ePNGColorSpacesRGB : ePNGColorSpaceRec709;
        }
    }

    if (iccprofile_data_p && png_get_valid(sp, ip, PNG_INFO_iCCP)) {
        png_charp profile_name = NULL;
#if OFX_IO_LIBPNG_VERSION > 10500   /* PNG function signatures changed */
        png_bytep profile_data = NULL;
#else
        png_charp profile_data = NULL;
#endif
        png_uint_32 profile_length = 0;
        int compression_type;
        png_get_iCCP (sp, ip, &profile_name, &compression_type,
                      &profile_data, &profile_length);
        if (profile_length && profile_data) {
#if OFX_IO_LIBPNG_VERSION > 10500
            *iccprofile_data_p = profile_data;
#else   
            *iccprofile_data_p = reinterpret_cast<unsigned char*>(profile_data);
#endif
            *iccprofile_length_p = profile_length;
        }
    }

    png_timep mod_time;
    if (date_p && png_get_tIME (sp, ip, &mod_time)) {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(4) << mod_time->year << ':' << std::setw(2) << mod_time->month << ':' << mod_time->day << ' ';
        ss << mod_time->hour << ':' << mod_time->minute << ':' << mod_time->second;
        *date_p = ss.str();
    }

    if (additionalComments_p) {
        png_textp text_ptr;
        int num_comments = png_get_text (sp, ip, &text_ptr, NULL);
        if (num_comments) {
            std::string comments;
            for (int i = 0;  i < num_comments;  ++i) {
                (*additionalComments_p)[std::string(text_ptr[i].key)] = std::string(text_ptr[i].text);
            }
        }
    }

    if (xResolution_p && yResolution_p) {
        int unit;
        png_uint_32 resx, resy;
        if (png_get_pHYs (sp, ip, &resx, &resy, &unit)) {
            float scale = 1;
            if (unit == PNG_RESOLUTION_METER) {
                // Convert to inches, to match most other formats
                scale = 2.54 / 100.0;
                *isResolutionInches_p = true;
            } else {
                *isResolutionInches_p = false;
            }
            *xResolution_p = (double)resx*scale;
            *yResolution_p = (double)resy*scale;
        }
    }

    if (bg_p) {
        get_background (sp, ip, *bit_depth_p, *real_bit_depth_p, *nChannels_p, &bg_p->r, &bg_p->g, &bg_p->b);
    }
    if (interlace_type_p) {
        *interlace_type_p = png_get_interlace_type (sp, ip);
    }
}


class ReadPNGPlugin : public GenericReaderPlugin
{
public:

    ReadPNGPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~ReadPNGPlugin();

private:

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    virtual bool isVideoStream(const std::string& /*filename*/) OVERRIDE FINAL { return false; }

    virtual void decode(const std::string& filename, OfxTime time, int view, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes) OVERRIDE FINAL;

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, OfxRectI *format, double *par, std::string *error, int* tile_width, int* tile_height) OVERRIDE FINAL;

    virtual void onInputFileChanged(const std::string& newFile, bool throwErrors,bool setColorSpace, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components, int *componentCount) OVERRIDE FINAL;

    void openFile(const std::string& filename,
                  png_structp* png,
                  png_infop* info,
                  std::FILE** file);

    std::string metadata(const std::string& filename);
};



ReadPNGPlugin::ReadPNGPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions)
: GenericReaderPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsXY, kSupportsAlpha, kSupportsTiles, false)
{
}

ReadPNGPlugin::~ReadPNGPlugin()
{
}

#define PNG_BYTES_TO_CHECK 8

std::string
ReadPNGPlugin::metadata(const std::string& filename)
{
    // Open the file
    std::FILE *image = OFX::fopen_utf8(filename.c_str(), "rb");
    if (image == NULL) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadPNG: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);

        return std::string();
    }

    std::stringstream ss;

    ss << "file: " << filename << std::endl;

    {
        unsigned char sig[8];
        // Check that it really is a PNG file
        /* Read in some of the signature bytes */
        if (std::fread(sig, 1, PNG_BYTES_TO_CHECK, image) != PNG_BYTES_TO_CHECK ||
            png_sig_cmp(sig, 0, PNG_BYTES_TO_CHECK) ) {
            ss << "  This file is not a valid PNG file\n";
            std::fclose (image);

            return ss.str();
        }
    }

    // Start decompressing
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
                                  NULL, NULL);
    if (png == NULL) {
        ss << "Could not create a PNG read structure (out of memory?)\n";
        std::fclose(image);

        return ss.str();
    }
    png_infop info = png_create_info_struct(png);
    if (info == NULL) {
        ss << "Could not create PNG info structure (out of memory?)\n";
        png_destroy_read_struct(&png, NULL, NULL);
        std::fclose(image);

        return ss.str();
    }

    if ( setjmp( png_jmpbuf(png) ) ) {
        png_destroy_read_struct(&png, &info, NULL);
        ss << "Could not set PNG jump value";
        std::fclose(image);

        return ss.str();
    }
    // Get ready for IO and tell the API we have already read the image signature
    png_init_io (png, image);
    png_set_sig_bytes (png, PNG_BYTES_TO_CHECK);
    png_read_info (png, info);
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    png_get_IHDR (png, info, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

    ///////////////////////////////////////////////////////////////////////////
    // Start displaying information
    ///////////////////////////////////////////////////////////////////////////

    ss << "  Image Width: " << width << " Image Length: " << height << std::endl;
    int pixel_depth = bit_depth * png_get_channels(png, info);
    ss << "  Bitdepth (Bits/Sample): " << bit_depth << std::endl;
    ss << "  Channels (Samples/Pixel): " << (int)png_get_channels(png, info) << std::endl;
    ss << "  Pixel depth (Pixel Depth): " << pixel_depth << std::endl;	// Does this add value?

    // Photometric interp packs a lot of information
    ss << "  Colour Type (Photometric Interpretation): ";

    switch (color_type) {
        case PNG_COLOR_TYPE_GRAY: {
            ss << "GRAYSCALE ";
            png_bytep trans = NULL;
            int num_trans = 0;
            if (png_get_tRNS(png, info, &trans, &num_trans, NULL) == PNG_INFO_tRNS) {
                ss << "(" << num_trans << " transparent) ";
            }
            break;
        }
        case PNG_COLOR_TYPE_PALETTE: {
            ss << "PALETTED COLOUR ";
            int num_palette = 0;
            png_color *palette = NULL;
            png_get_PLTE(png, info, &palette, &num_palette);
            int num_trans = 0;
            png_bytep trans = NULL;
            ;
            ss << "(" << num_palette << " colours";
            if (png_get_tRNS(png, info, &trans, &num_trans, NULL) == PNG_INFO_tRNS) {
                ss << ", " << num_trans << " transparent";
            }
            ss << ")";
            break;
        }
        case PNG_COLOR_TYPE_RGB: {
            ss << "RGB ";
            break;
        }
        case PNG_COLOR_TYPE_RGB_ALPHA: {
            ss << "RGB with alpha channel ";
            break;
        }
        case PNG_COLOR_TYPE_GRAY_ALPHA: {
            ss << "GRAYSCALE with alpha channel ";
            break;
        }
        default: {
            ss << "Unknown photometric interpretation!";
            break;
        }
    }
    ss << std::endl;

    ss << "  Image filter: ";
    switch (png_get_filter_type(png, info))
    {
        case PNG_FILTER_TYPE_BASE:
            ss << "Single row per byte filter ";
            break;

        case PNG_INTRAPIXEL_DIFFERENCING:
            ss << "Intrapixel differencing (MNG only) ";
            break;

        default:
            ss << "Unknown filter! ";
            break;
    }
    ss << std::endl;

#if defined(PNG_READ_INTERLACING_SUPPORTED)
    ss << "  Interlacing: ";
    switch (png_get_interlace_type(png, info))
    {
        case PNG_INTERLACE_NONE:
            ss << "No interlacing ";
            break;

        case PNG_INTERLACE_ADAM7:
            ss << "Adam7 interlacing ";
            break;

        default:
            ss << "Unknown interlacing ";
            break;
    }
    ss << std::endl;
#endif

    ss << "  Compression Scheme: ";
    switch (png_get_compression_type(png, info))
    {
        case PNG_COMPRESSION_TYPE_BASE:
            ss << "Deflate method 8, 32k window";
            break;

        default:
            ss << "Unknown compression scheme!";
            break;
    }
    ss << std::endl;

#ifdef PNG_pHYs_SUPPORTED
    {
        png_uint_32 res_x, res_y;
        int unit_type;

        if (png_get_pHYs(png, info, &res_x, &res_y,
                         &unit_type) == PNG_INFO_pHYs) {
            ss << "  Resolution: " << res_x << ", " << res_y << " ";
            switch (unit_type) {
                case PNG_RESOLUTION_UNKNOWN:
                    ss << "(unit unknown)";
                    break;

                case PNG_RESOLUTION_METER:
                    ss << "(pixels per meter)";
                    break;

                default:
                    ss << "(Unknown value for unit stored)";
                    break;
            }
            ss << std::endl;
        }
    }
#endif

    // FillOrder is always msb-to-lsb, big endian
    //ss << "  FillOrder: msb-to-lsb\n  Byte Order: Network (Big Endian)\n";

#ifdef PNG_cHRM_SUPPORTED
    {
        double white_x, white_y, red_x, red_y, green_x, green_y, blue_x,
        blue_y;

        if (png_get_cHRM(png, info, &white_x, &white_y, &red_x,
                         &red_y, &green_x, &green_y, &blue_x, &blue_y) == PNG_INFO_cHRM) {
            ss << "  CIE white point: " << white_x  << ", " << white_y  << std::endl;
            ss << "  CIE chromaticities: ";
            ss << "red = "  << red_x    << ", " << red_y    << "; ";
            ss << "green =" << green_x  << ", " << green_y  << "; ";
            ss << "blue ="  << blue_x   << ", " << blue_y   << std::endl;
        }
    }
#endif
#ifdef PNG_gAMA_SUPPORTED
    {
        double gamma;

        if (png_get_gAMA(png, info, &gamma) == PNG_INFO_gAMA)
            ss << "  Gamma: " << gamma  << std::endl;
    }
#endif
#ifdef PNG_iCCP_SUPPORTED
    {
        png_charp name;
#if PNG_LIBPNG_VER_SONUM >=15
        png_bytep profile;
#else
        png_charp profile;
#endif
        png_uint_32 proflen;
        int compression_type;

        if (png_get_iCCP(png, info, &name, &compression_type,
                         &profile, &proflen) == PNG_INFO_iCCP) {
            ss << "  ICC profile: " << name;
        }
    }
#endif
#ifdef PNG_sRGB_SUPPORTED
    {
        int intent;

        if (png_get_sRGB(png, info, &intent) == PNG_INFO_sRGB) {
            ss << "  sRGB intent: ";
            switch (intent) {
                case PNG_sRGB_INTENT_PERCEPTUAL:
                    ss << "perceptual";
                    break;
                case PNG_sRGB_INTENT_RELATIVE:
                    ss << "relative";
                    break;
                case PNG_sRGB_INTENT_SATURATION:
                    ss << "saturation";
                    break;
                case PNG_sRGB_INTENT_ABSOLUTE:
                    ss << "absolute";
                    break;
            }
            ss << std::endl;
        }
    }
#endif
#ifdef PNG_bKGD_SUPPORTED
    {
        png_color_16p background;

        if (png_get_bKGD(png, info, &background) == PNG_INFO_bKGD) {
            ss << "  Background color present\n";
        }
    }
#endif
#ifdef PNG_hIST_SUPPORTED
    {
        png_uint_16p hist;

        if (png_get_hIST(png, info, &hist) == PNG_INFO_hIST) {
            ss << "  Histogram present\n";
        }
    }
#endif
#ifdef PNG_oFFs_SUPPORTED
    {
        png_int_32 offset_x, offset_y;
        int unit_type;

        if (png_get_oFFs(png, info, &offset_x, &offset_y, &unit_type) == PNG_INFO_oFFs) {
            // see http://www.libpng.org/pub/png/spec/register/pngext-1.4.0-pdg.html#C.oFFs
            const char* unit = (unit_type == PNG_OFFSET_PIXEL) ? "px" : "um";
            ss << "  Offset: " << offset_x << unit << ", " << offset_y << unit << std::endl;
        }
    }
#endif
#ifdef PNG_pCAL_SUPPORTED
    {
        png_charp purpose, units;
        png_charpp params;
        png_int_32 X0, X1;
        int type, nparams;

        if (png_get_pCAL(png, info, &purpose, &X0, &X1, &type,
                         &nparams, &units, &params) == PNG_INFO_pCAL) {
            // see http://www.libpng.org/pub/png/spec/register/pngext-1.4.0-pdg.html#C.pCAL
            ss << "  Physical calibration chunk present\n";
        }
    }
#endif
#ifdef PNG_sBIT_SUPPORTED
    {
        png_color_8p sig_bit;

        if (png_get_sBIT(png, info, &sig_bit) == PNG_INFO_sBIT) {
            ss << "  Significant bits per channel: " << sig_bit << std::endl;
        }
    }
#endif
#ifdef PNG_sCAL_SUPPORTED
    {
        int unit_type;
        double scal_width, scal_height;

        if (png_get_sCAL(png, info, &unit_type, &scal_width,
                         &scal_height) == PNG_INFO_sCAL) {
            // see http://www.libpng.org/pub/png/spec/register/pngext-1.4.0-pdg.html#C.sCAL
            const char* unit = unit_type == PNG_SCALE_UNKNOWN ? "" : (unit_type == PNG_SCALE_METER ? "m" : "rad");
            ss << "  Scale: " << scal_width << unit << " x " << scal_height << unit << std::endl;
        }
    }
#endif

#if defined(PNG_TEXT_SUPPORTED)
    {
        png_textp text;
        int num_text = 0;

        if (png_get_text(png, info, &text, &num_text) > 0) {
            // Text comments
            ss << "  Number of text strings: " << num_text << std::endl;

            for (int i = 0; i < num_text; ++i) {
                ss << "   " << text[i].key << " ";

                switch (text[1].compression)
                {
                    case -1:
                        ss << "(tEXt uncompressed)";
                        break;

                    case 0:
                        ss << "(xTXt deflate compressed)";
                        break;

                    case 1:
                        ss << "(iTXt uncompressed)";
                        break;

                    case 2:
                        ss << "(iTXt deflate compressed)";
                        break;

                    default:
                        ss << "(unknown compression)";
                        break;
                }

                ss << ": ";
                int j = 0;
                while (text[i].text[j] != '\0') {
                    if (text[i].text[j] == '\n') {
                        ss << std::endl;
                    } else {
                        ss << text[i].text[j];
                    }
                    j++;
                }
                
                ss << std::endl;
            }
        }
    }
#endif

    // This cleans things up for us in the PNG library
    std::fclose(image);
    png_destroy_read_struct (&png, &info, NULL);

    return ss.str();
}

void
ReadPNGPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                            const std::string &paramName)
{
    if (paramName == kParamShowMetadata) {
        std::string filename;
        OfxStatus st = getFilenameAtTime(args.time, &filename);
        std::stringstream ss;
        if (st == kOfxStatOK) {
            ss << metadata(filename);
        } else {
            ss << "Impossible to read image info:\nCould not get filename at time " << args.time << '.';
        }
        sendMessage(OFX::Message::eMessageMessage, "", ss.str());
    } else {
        GenericReaderPlugin::changedParam(args, paramName);
    }
}

void
ReadPNGPlugin::openFile(const std::string& filename,
                        png_structp* png,
                        png_infop* info,
                        std::FILE** file)
{
    *png = NULL;
    *info = NULL;
    *file = OFX::fopen_utf8(filename.c_str(), "rb");
    if (!*file) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    unsigned char sig[8];
    if (std::fread (sig, 1, sizeof(sig), *file) != sizeof(sig)) {
        throw std::runtime_error("Not a PNG file");
    }

    if (png_sig_cmp (sig, 0, 7)) {
        throw std::runtime_error("Not a PNG file");
    }

    *png = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!*png) {
        std::fclose(*file);
        *file = NULL;

        throw std::runtime_error("Could not create PNG read structure");
    }

    *info = png_create_info_struct (*png);
    if (!*info) {
        png_destroy_read_struct (png, NULL, NULL);
        std::fclose(*file);
        *file = NULL;

        throw std::runtime_error("Could not create PNG info structure");
    }
    // Must call this setjmp in every function that does PNG reads
    if (setjmp (png_jmpbuf(*png))) {
        png_destroy_read_struct (png, info, NULL);
        std::fclose(*file);
        *file = NULL;

        throw std::runtime_error("PNG library error");
    }

    png_init_io (*png, *file);
    png_set_sig_bytes (*png, 8);  // already read 8 bytes
}




void
ReadPNGPlugin::decode(const std::string& filename,
                      OfxTime /*time*/,
                      int /*view*/,
                      bool /*isPlayback*/,
                      const OfxRectI& renderWindow,
                      float *pixelData,
                      const OfxRectI& bounds,
                      OFX::PixelComponentEnum pixelComponents,
                      int /*pixelComponentCount*/,
                      int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentXY && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PNG: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    png_structp png;
    png_infop info;
    FILE* file;

    try {
        openFile(filename, &png, &info, &file);
    } catch (const std::exception& e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        throwSuiteStatusException(kOfxStatFailed);
    }

    int x1,y1,width,height;
    int nChannels;
    BitDepthEnum bitdepth;
    int realbitdepth;
    int colorType;
    double par;
    getPNGInfo(png, info, &x1, &y1, &width, &height, &par, &nChannels, &bitdepth, &realbitdepth, &colorType, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    assert(renderWindow.x1 >= x1 && renderWindow.y1 >= y1 && renderWindow.x2 <= x1 + width && renderWindow.y2 <= y1 + height);

    std::size_t pngRowBytes = nChannels * width;
    if (bitdepth == eBitDepthUShort) {
        pngRowBytes *= sizeof(unsigned short);
    }

    RamBuffer scratchBuffer(pngRowBytes * height);
    unsigned char* tmpData = scratchBuffer.getData();


    //if (interlace_type != 0) {
    std::vector<unsigned char *> row_pointers(height);
    for (int i = 0;  i < height;  ++i) {
        row_pointers[i] = tmpData + i * pngRowBytes;
    }

    // Must call this setjmp in every function that does PNG reads
    if (setjmp (png_jmpbuf (png))) {
        png_destroy_read_struct(&png, &info, NULL);
        std::fclose(file);
        setPersistentMessage(OFX::Message::eMessageError, "", "PNG library error");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }
    png_read_image(png, &row_pointers[0]);
    png_read_end(png, NULL);
    /*
    } else {
        for (int y = 0; y < height; ++y) {
            // Must call this setjmp in every function that does PNG reads
            if (setjmp (png_jmpbuf (png))) {
                png_destroy_read_struct(&png, &info, NULL);
                std::fclose(file);
                setPersistentMessage(OFX::Message::eMessageError, "", "PNG library error");
                OFX::throwSuiteStatusException(kOfxStatErrFormat);

                return;
            }
            png_read_row (png, (png_bytep)tmpData + y * pngRowBytes, NULL);
        }
    }
     */

    png_destroy_read_struct(&png, &info, NULL);
    std::fclose(file);
    file = NULL;

    OfxRectI srcBounds;
    srcBounds.x1 = x1;
    srcBounds.y1 = y1;
    srcBounds.x2 = x1 + width;
    srcBounds.y2 = y1 + height;

    PixelComponentEnum srcComponents;
    switch (nChannels) {
        case 1:
            srcComponents = ePixelComponentAlpha;
            break;
        case 2:
            srcComponents = ePixelComponentXY;
            break;
        case 3:
            srcComponents = ePixelComponentRGB;
            break;
        case 4:
            srcComponents = ePixelComponentRGBA;
            break;
        default:
            setPersistentMessage(OFX::Message::eMessageError, "", "This plug-in only supports images with 1 to 4 channels");
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
            return;
    }

    convertDepthAndComponents(tmpData, renderWindow, srcBounds, srcComponents, bitdepth, pngRowBytes, pixelData, bounds, pixelComponents, rowBytes);
}

bool
ReadPNGPlugin::getFrameBounds(const std::string& filename,
                              OfxTime /*time*/,
                              OfxRectI *bounds,
                              OfxRectI *format,
                              double *par,
                              std::string *error,
                              int* tile_width,
                              int* tile_height)
{
    assert(bounds && par);
    png_structp png;
    png_infop info;
    FILE* file;

    try {
        openFile(filename, &png, &info, &file);
    } catch (const std::exception& e) {
        *error = e.what();
        return false;
    }

    int x1,y1,width,height;
    int nChannels;
    BitDepthEnum bitdepth;
    int realbitdepth;
    int colorType;

    getPNGInfo(png, info, &x1, &y1, &width, &height, par, &nChannels, &bitdepth, &realbitdepth, &colorType, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    png_destroy_read_struct(&png, &info, NULL);
    std::fclose(file);
    file = NULL;

    bounds->x1 = x1;
    bounds->y1 = y1;
    bounds->x2 = x1 + width;
    bounds->y2 = y1 + height;
    *format = *bounds;
    *tile_height = *tile_width = 0;

    return true;
}

void
ReadPNGPlugin::onInputFileChanged(const std::string& filename,
                                  bool throwErrors,
                                  bool setColorSpace,
                                  OFX::PreMultiplicationEnum *premult,
                                  OFX::PixelComponentEnum *components,
                                  int *componentCount)
{

    assert(premult && components);
    png_structp png;
    png_infop info;
    FILE* file;
    try {
        openFile(filename, &png, &info, &file);
    } catch (const std::exception& e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        if (throwErrors) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    int x1,y1,width,height;
    double par;
    int nChannels;
    BitDepthEnum bitdepth;
    int realbitdepth;
    int colorType;
    double gamma;
    PNGColorSpaceEnum pngColorspace;
    getPNGInfo(png, info, &x1, &y1, &width, &height, &par, &nChannels, &bitdepth, &realbitdepth, &colorType, &pngColorspace, &gamma, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    png_destroy_read_struct(&png, &info, NULL);
    std::fclose(file);
    file = NULL;

    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        switch (pngColorspace) {
            case ePNGColorSpaceGammaCorrected:
                if (std::fabs(gamma - 1.8) < 0.05) {
                    if (_ocio->hasColorspace("Gamma1.8")) {
                        // nuke-default
                        _ocio->setInputColorspace("Gamma1.8");
                    }
                } else if (std::fabs(gamma - 2.2) < 0.05) {
                    if (_ocio->hasColorspace("Gamma2.2")) {
                        // nuke-default
                        _ocio->setInputColorspace("Gamma2.2");
                    } else if (_ocio->hasColorspace("VD16")) {
                        // VD16 in blender
                        _ocio->setInputColorspace("VD16");
                    } else if (_ocio->hasColorspace("vd16")) {
                        // vd16 in spi-anim and spi-vfx
                        _ocio->setInputColorspace("vd16");
                    } else if (_ocio->hasColorspace("sRGB")) {
                        // nuke-default and blender
                        _ocio->setInputColorspace("sRGB");
                    } else if (_ocio->hasColorspace("sRGB D65")) {
                        // blender-cycles
                        _ocio->setInputColorspace("sRGB D65");
                    } else if (_ocio->hasColorspace("sRGB (D60 sim.)")) {
                        // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                        _ocio->setInputColorspace("sRGB (D60 sim.)");
                    } else if (_ocio->hasColorspace("out_srgbd60sim")) {
                        // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                        _ocio->setInputColorspace("out_srgbd60sim");
                    } else if (_ocio->hasColorspace("rrt_Gamma2.2")) {
                        // rrt_Gamma2.2 in aces 0.7.1
                        _ocio->setInputColorspace("rrt_Gamma2.2");
                    } else if (_ocio->hasColorspace("rrt_srgb")) {
                        // rrt_srgb in aces 0.1.1
                        _ocio->setInputColorspace("rrt_srgb");
                    } else if (_ocio->hasColorspace("srgb8")) {
                        // srgb8 in spi-vfx
                        _ocio->setInputColorspace("srgb8");
                    } else if (_ocio->hasColorspace("vd16")) {
                        // vd16 in spi-anim
                        _ocio->setInputColorspace("vd16");
                    }

                }
                break;
            case ePNGColorSpacesRGB:
                if (_ocio->hasColorspace("sRGB")) {
                    // nuke-default and blender
                    _ocio->setInputColorspace("sRGB");
                } else if (_ocio->hasColorspace("sRGB D65")) {
                    // blender-cycles
                    _ocio->setInputColorspace("sRGB D65");
                } else if (_ocio->hasColorspace("sRGB (D60 sim.)")) {
                    // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                    _ocio->setInputColorspace("sRGB (D60 sim.)");
                } else if (_ocio->hasColorspace("out_srgbd60sim")) {
                    // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                    _ocio->setInputColorspace("out_srgbd60sim");
                } else if (_ocio->hasColorspace("rrt_Gamma2.2")) {
                    // rrt_Gamma2.2 in aces 0.7.1
                    _ocio->setInputColorspace("rrt_Gamma2.2");
                } else if (_ocio->hasColorspace("rrt_srgb")) {
                    // rrt_srgb in aces 0.1.1
                    _ocio->setInputColorspace("rrt_srgb");
                } else if (_ocio->hasColorspace("srgb8")) {
                    // srgb8 in spi-vfx
                    _ocio->setInputColorspace("srgb8");
                } else if (_ocio->hasColorspace("Gamma2.2")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma2.2");
                } else if (_ocio->hasColorspace("srgb8")) {
                    // srgb8 in spi-vfx
                    _ocio->setInputColorspace("srgb8");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim
                    _ocio->setInputColorspace("vd16");
                }

                break;
            case ePNGColorSpaceRec709:
                if (_ocio->hasColorspace("Rec709")) {
                    // nuke-default
                    _ocio->setInputColorspace("Rec709");
                } else if (_ocio->hasColorspace("nuke_rec709")) {
                    // blender
                    _ocio->setInputColorspace("nuke_rec709");
                } else if (_ocio->hasColorspace("Rec.709 - Full")) {
                    // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                    _ocio->setInputColorspace("Rec.709 - Full");
                } else if (_ocio->hasColorspace("out_rec709full")) {
                    // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                    _ocio->setInputColorspace("out_rec709full");
                } else if (_ocio->hasColorspace("rrt_rec709_full_100nits")) {
                    // rrt_rec709_full_100nits in aces 0.7.1
                    _ocio->setInputColorspace("rrt_rec709_full_100nits");
                } else if (_ocio->hasColorspace("rrt_rec709")) {
                    // rrt_rec709 in aces 0.1.1
                    _ocio->setInputColorspace("rrt_rec709");
                } else if (_ocio->hasColorspace("hd10")) {
                    // hd10 in spi-anim and spi-vfx
                    _ocio->setInputColorspace("hd10");
                }
                break;
            case ePNGColorSpaceLinear:
                _ocio->setInputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
                break;
        }
#     endif
    }

    switch (nChannels) {
        case 1:
            assert(false);
            *components = OFX::ePixelComponentAlpha;
            break;
        case 2:
            assert(false);
            *components = OFX::ePixelComponentRGBA;
            break;
        case 3:
            *components = OFX::ePixelComponentRGB;
            break;
        case 4:
            *components = OFX::ePixelComponentRGBA;
            break;
        default:
            break;
    }

    *componentCount = nChannels;

    if (*components != OFX::ePixelComponentRGBA && *components != OFX::ePixelComponentAlpha) {
        *premult = OFX::eImageOpaque;
    } else {
        // output is always unpremultiplied
        *premult = OFX::eImageUnPreMultiplied;
    }
}


mDeclareReaderPluginFactory(ReadPNGPluginFactory, {}, false);

void
ReadPNGPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("png");
}

/** @brief The basic describe function, passed a plugin descriptor */
void
ReadPNGPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, _extensions, kPluginEvaluation, kSupportsTiles, false);
    
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadPNGPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsXY, kSupportsAlpha, kSupportsTiles, true);

    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamShowMetadata);
        param->setLabel(kParamShowMetadataLabel);
        param->setHint(kParamShowMetadataHint);
        if (page) {
            page->addChild(*param);
        }
    }

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadPNGPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    ReadPNGPlugin* ret =  new ReadPNGPlugin(handle, _extensions);
    ret->restoreStateFromParameters();
    return ret;
}


static ReadPNGPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
