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
 * OFX PNG writer plugin.
 * Writes an image in the PNG format
 */


#include <cstdio> // fopen, fwrite...
#include <vector>
#include <algorithm>

#include <png.h>
#include <zlib.h>

#include "GenericOCIO.h"

#include "GenericWriter.h"
#include "ofxsMacros.h"
#include "ofxsFileOpen.h"
using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "WritePNG"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write PNG files."
#define kPluginIdentifier "fr.inria.openfx.WritePNG"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 40 // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#define kSupportsXY false

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

/// Writes a scanline.
///
inline bool
write_row (png_structp& sp, png_byte *data)
{
    if (setjmp (png_jmpbuf(sp))) {
        //error ("PNG library error");
        return false;
    }
    png_write_row (sp, data);
    return true;
}


/// Initializes a PNG write struct.
/// \return empty string on success, C-string error message on failure.
///
inline void
create_write_struct (png_structp& sp,
                     png_infop& ip,
                     int nChannels,
                     int* color_type)
{

    switch (nChannels) {
        case 1 : *color_type = PNG_COLOR_TYPE_GRAY; break;
        case 2 : *color_type = PNG_COLOR_TYPE_GRAY_ALPHA; break;
        case 3 : *color_type = PNG_COLOR_TYPE_RGB; break;
        case 4 : *color_type = PNG_COLOR_TYPE_RGB_ALPHA; break;
        default:
            throw std::runtime_error("PNG only supports 1-4 channels");
    }

    sp = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (! sp)
        throw std::runtime_error("Could not create PNG write structure");

    ip = png_create_info_struct (sp);
    if (! ip)
        throw std::runtime_error("Could not create PNG info structure");

    // Must call this setjmp in every function that does PNG writes
    if (setjmp (png_jmpbuf(sp)))
        throw std::runtime_error("PNG library error");
}

/// Helper function - finalizes writing the image.
///
inline void
finish_image (png_structp& sp)
{
    // Must call this setjmp in every function that does PNG writes
    if (setjmp (png_jmpbuf(sp))) {
        //error ("PNG library error");
        return;
    }
    png_write_end (sp, NULL);
}


/// Destroys a PNG write struct.
///
inline void
destroy_write_struct (png_structp& sp, png_infop& ip)
{
    if (sp && ip) {
        finish_image (sp);
        png_destroy_write_struct (&sp, &ip);
        sp = NULL;
        ip = NULL;
    }
}

/// Helper function - writes a single parameter.
///
/*inline bool
put_parameter (png_structp& sp, png_infop& ip, const std::string &_name,
               TypeDesc type, const void *data, std::vector<png_text>& text)
{
    std::string name = _name;

    // Things to skip
   if (Strutil::iequals(name, "planarconfig"))  // No choice for PNG files
        return false;
    if (Strutil::iequals(name, "compression"))
        return false;
    if (Strutil::iequals(name, "ResolutionUnit") ||
        Strutil::iequals(name, "XResolution") || Strutil::iequals(name, "YResolution"))
        return false;

    // Remap some names to PNG conventions
    if (Strutil::iequals(name, "Artist") && type == TypeDesc::STRING)
        name = "Author";
    if ((Strutil::iequals(name, "name") || Strutil::iequals(name, "DocumentName")) &&
        type == TypeDesc::STRING)
        name = "Title";
    if ((Strutil::iequals(name, "description") || Strutil::iequals(name, "ImageDescription")) &&
        type == TypeDesc::STRING)
        name = "Description";

    if (Strutil::iequals(name, "DateTime") && type == TypeDesc::STRING) {
        png_time mod_time;
        int year, month, day, hour, minute, second;
        if (sscanf (*(const char **)data, "%4d:%02d:%02d %2d:%02d:%02d",
                    &year, &month, &day, &hour, &minute, &second) == 6) {
            mod_time.year = year;
            mod_time.month = month;
            mod_time.day = day;
            mod_time.hour = hour;
            mod_time.minute = minute;
            mod_time.second = second;
            png_set_tIME (sp, ip, &mod_time);
            return true;
        } else {
            return false;
        }
    }

    if (type == TypeDesc::STRING) {
        png_text t;
        t.compression = PNG_TEXT_COMPRESSION_NONE;
        t.key = (char *)ustring(name).c_str();
        t.text = *(char **)data;   // Already uniquified
        text.push_back (t);
    }

    return false;
}*/


/// Writes PNG header according to the ImageSpec.
///
inline void
write_info (png_structp& sp,
            png_infop& ip,
            int color_type,
            int x1, int y1,
            int width,
            int height,
            double par,
            const std::string& outputColorspace,
            BitDepthEnum bitdepth)
{
    int pixelBytes = bitdepth == eBitDepthUByte ? sizeof(unsigned char) : sizeof(unsigned short);
    png_set_IHDR (sp, ip, width, height, pixelBytes*8, color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_oFFs (sp, ip, x1, y1, PNG_OFFSET_PIXEL);

    // @TODO: set colorspace meta-data from OCIO colorspace string
    /*if (Strutil::iequals (colorspace, "Linear")) {
        png_set_gAMA (sp, ip, 1.0);
    }
    else if (Strutil::iequals (colorspace, "GammaCorrected")) {
        png_set_gAMA (sp, ip, 1.0f/gamma);
    }
    else if (Strutil::iequals (colorspace, "sRGB")) {
        png_set_sRGB_gAMA_and_cHRM (sp, ip, PNG_sRGB_INTENT_ABSOLUTE);
    }*/

    // Write ICC profile, if we have anything
    /*const ImageIOParameter* icc_profile_parameter = spec.find_attribute(ICC_PROFILE_ATTR);
    if (icc_profile_parameter != NULL) {
        unsigned int length = icc_profile_parameter->type().size();
#if OIIO_LIBPNG_VERSION > 10500 // PNG function signatures changed
        unsigned char *icc_profile = (unsigned char*)icc_profile_parameter->data();
        if (icc_profile && length)
            png_set_iCCP (sp, ip, "Embedded Profile", 0, icc_profile, length);
#else
        char *icc_profile = (char*)icc_profile_parameter->data();
        if (icc_profile && length)
            png_set_iCCP (sp, ip, (png_charp)"Embedded Profile", 0, icc_profile, length);
#endif
    }*/

    /*if (false && ! spec.find_attribute("DateTime")) {
        time_t now;
        time (&now);
        struct tm mytm;
        Sysutil::get_local_time (&now, &mytm);
        std::string date = Strutil::format ("%4d:%02d:%02d %2d:%02d:%02d",
                                            mytm.tm_year+1900, mytm.tm_mon+1, mytm.tm_mday,
                                            mytm.tm_hour, mytm.tm_min, mytm.tm_sec);
        spec.attribute ("DateTime", date);
    }*/

    /*string_view unitname = spec.get_string_attribute ("ResolutionUnit");
    float xres = spec.get_float_attribute ("XResolution");
    float yres = spec.get_float_attribute ("YResolution");*/
    int unittype = PNG_RESOLUTION_METER;
    float scale = 100.0/2.54;
    float xres = 100.0f;
    float yres = xres * (par ? par : 1.0f);
    png_set_pHYs (sp, ip, (png_uint_32)(xres*scale),
                  (png_uint_32)(yres*scale), unittype);


    // Deal with all other params
    /*for (size_t p = 0;  p < spec.extra_attribs.size();  ++p)
        put_parameter (sp, ip,
                       spec.extra_attribs[p].name().string(),
                       spec.extra_attribs[p].type(),
                       spec.extra_attribs[p].data(),
                       text);*/

    /*if (text.size())
        png_set_text (sp, ip, &text[0], text.size());*/

    png_write_info (sp, ip);
    png_set_packing (sp);   // Pack 1, 2, 4 bit into bytes
}


class WritePNGPlugin : public GenericWriterPlugin
{
public:

    WritePNGPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~WritePNGPlugin();

private:

    virtual void encode(const std::string& filename,
                        const OfxTime time,
                        const std::string& viewName,
                        const float *pixelData,
                        const OfxRectI& bounds,
                        const float pixelAspectRatio,
                        const int pixelDataNComps,
                        const int dstNCompsStartIndex,
                        const int dstNComps,
                        const int rowBytes) OVERRIDE FINAL;

    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;

    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return OFX::eImageUnPreMultiplied; }

    virtual void onOutputFileChanged(const std::string& newFile, bool setColorSpace) OVERRIDE FINAL;

    void openFile(const std::string& filename,
                  int nChannels,
                  png_structp* png,
                  png_infop* info,
                  FILE** file,
                  int *color_type);
};

WritePNGPlugin::WritePNGPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions)
: GenericWriterPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsXY)
{
}


WritePNGPlugin::~WritePNGPlugin()
{
}

void
WritePNGPlugin::openFile(const std::string& filename,
                         int nChannels,
                         png_structp* png,
                         png_infop* info,
                         FILE** file,
                         int *color_type)
{
    *file = OFX::open_file(filename, "wb");
    if (!*file) {
        throw std::runtime_error("Couldn't not open file");
    }

    try {
        create_write_struct (*png, *info, nChannels, color_type);
    } catch (const std::exception& e) {
        destroy_write_struct(*png, *info);
        fclose(*file);
        throw e;
    }

    png_init_io (*png, *file);
    png_set_sig_bytes (*png, 8);  // already read 8 bytes
}

/// Bitwise circular rotation left by k bits (for 32 bit unsigned integers)
inline unsigned int rotl32 (unsigned int x, int k) {
    return (x<<k) | (x>>(32-k));
}

// Bob Jenkins "lookup3" hashes:  http://burtleburtle.net/bob/c/lookup3.c
// It's in the public domain.

// Mix up the bits of a, b, and c (changing their values in place).
inline void bjmix (unsigned int &a, unsigned int &b, unsigned int &c)
{
    a -= c;  a ^= rotl32(c, 4);  c += b;
    b -= a;  b ^= rotl32(a, 6);  a += c;
    c -= b;  c ^= rotl32(b, 8);  b += a;
    a -= c;  a ^= rotl32(c,16);  c += b;
    b -= a;  b ^= rotl32(a,19);  a += c;
    c -= b;  c ^= rotl32(b, 4);  b += a;
}

static void add_dither (int nchannels, int width, int height,
                        float *data, std::size_t xstride, std::size_t ystride,
                        float ditheramplitude,
                        int alpha_channel, unsigned int ditherseed,
                        int chorigin, int xorigin, int yorigin)
{
    
    assert(sizeof(unsigned int) == 4);
    
    char *scanline = (char*)data;
    for (int y = 0;  y < height;  ++y, scanline += ystride) {
        char *pixel = (char*)data;
        unsigned int ba = yorigin + y;
        unsigned int bb = ditherseed + (chorigin << 24);
        unsigned int bc = xorigin;
        for (int x = 0;  x < width;  ++x, pixel += xstride) {
            float *val = (float *)pixel;
            for (int c = 0;  c < nchannels;  ++c, ++val, ++bc) {
                bjmix (ba, bb, bc);
                int channel = c+chorigin;
                if (channel == alpha_channel)
                    continue;
                float dither = bc / float(std::numeric_limits<uint32_t>::max());
                *val += ditheramplitude * (dither - 0.5f);
            }
        }
    }
}


void WritePNGPlugin::encode(const std::string& filename,
                            const OfxTime /*time*/,
                            const std::string& /*viewName*/,
                            const float *pixelData,
                            const OfxRectI& bounds,
                            const float pixelAspectRatio,
                            const int pixelDataNComps,
                            const int dstNCompsStartIndex,
                            const int dstNComps,
                            const int rowBytes)
{
    if (dstNComps != 4 && dstNComps != 3 && dstNComps != 1) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only write RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    png_structp png;
    png_infop info;
    FILE* file;
    int color_type;
    try {
        openFile(filename, pixelDataNComps, &png, &info, &file, &color_type);
    } catch (const std::exception& e) {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        throwSuiteStatusException(kOfxStatFailed);
    }


    png_init_io (png, file);

    // TODO: should be a parameter
    int compressionLevel = std::max(std::min(6/* medium speed vs size tradeoff */, Z_BEST_COMPRESSION), Z_NO_COMPRESSION);
    png_set_compression_level(png, compressionLevel);


    // Todo should be a parameter;
    std::string compression;
    if (compression.empty ()) {
        png_set_compression_strategy(png, Z_DEFAULT_STRATEGY);
    }
    /*else if (Strutil::iequals (compression, "default")) {
        png_set_compression_strategy(png, Z_DEFAULT_STRATEGY);
    }
    else if (Strutil::iequals (compression, "filtered")) {
        png_set_compression_strategy(png, Z_FILTERED);
    }
    else if (Strutil::iequals (compression, "huffman")) {
        png_set_compression_strategy(png, Z_HUFFMAN_ONLY);
    }
    else if (Strutil::iequals (compression, "rle")) {
        png_set_compression_strategy(png, Z_RLE);
    }
    else if (Strutil::iequals (compression, "fixed")) {
        png_set_compression_strategy(png, Z_FIXED);
    }
    else {
        png_set_compression_strategy(png, Z_DEFAULT_STRATEGY);
    }*/

    // TOdo should be a parameter
    BitDepthEnum pngDetph = eBitDepthUByte;
    write_info(png, info, color_type, bounds.x1, bounds.y1, bounds.x2 - bounds.x1, bounds.y2 - bounds.y1, pixelAspectRatio, std::string() /*colorSpace*/, pngDetph);


    // Todo should be a parameter
    bool dither = pngDetph == eBitDepthUByte;




}

bool WritePNGPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}

void
WritePNGPlugin::onOutputFileChanged(const std::string &/*filename*/,
                                    bool setColorSpace)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, pfm files are assumed to be linear.
        _ocio->setOutputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
#     endif
    }
}


mDeclareWriterPluginFactory(WritePNGPluginFactory, {}, false);

void WritePNGPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("png");
}

/** @brief The basic describe function, passed a plugin descriptor */
void WritePNGPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc,OFX::eRenderFullySafe, _extensions, kPluginEvaluation, false, false);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WritePNGPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha,kSupportsXY,
                                                                    "reference", "reference", false);

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WritePNGPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    WritePNGPlugin* ret = new WritePNGPlugin(handle, _extensions);
    ret->restoreState();
    return ret;
}


static WritePNGPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
