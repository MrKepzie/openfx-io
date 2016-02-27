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
 * OFX PFM reader plugin.
 * Reads an image in the Portable Float Map (PFM) format.
 */

#include <cstdio> // fopen, fread...
#include <algorithm>

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#endif

#include "GenericReader.h"
#include "GenericOCIO.h"
#include "ofxsMacros.h"

#define kPluginName "ReadPFM"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read PFM (Portable Float Map) files."
#define kPluginIdentifier "fr.inria.openfx.ReadPFM"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 92 // better than ReadOIIO

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#define kSupportsTiles false

class ReadPFMPlugin : public GenericReaderPlugin
{
public:

    ReadPFMPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~ReadPFMPlugin();

private:

    virtual bool isVideoStream(const std::string& /*filename*/) OVERRIDE FINAL { return false; }

    virtual void decode(const std::string& filename, OfxTime time, int view, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes) OVERRIDE FINAL;

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, double *par, std::string *error, int* tile_width, int* tile_height) OVERRIDE FINAL;

    virtual void onInputFileChanged(const std::string& newFile, bool setColorSpace, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components, int *componentCount) OVERRIDE FINAL;

};


/**
 \return \c false for "Little Endian", \c true for "Big Endian".
 **/
static inline bool endianness()
{
    const int x = 1;
    return ((unsigned char *)&x)[0] ? false : true;
}

//! Invert endianness of a memory buffer.
template<typename T>
static void invert_endianness(T *const buffer, const unsigned int size)
{
    if (size) {
        switch (sizeof(T)) {
            case 1:
                break;
            case 2:
                for (unsigned short *ptr = (unsigned short *)buffer + size; ptr > (unsigned short *)buffer;) {
                    const unsigned short val = *(--ptr);
                    *ptr = (unsigned short)((val >> 8) | ((val << 8)));
                }
                break;
            case 4:
                for (unsigned int *ptr = (unsigned int *)buffer + size; ptr > (unsigned int *)buffer;) {
                    const unsigned int val = *(--ptr);
                    *ptr = (val >> 24) | ((val >> 8) & 0xff00) | ((val << 8) & 0xff0000) | (val << 24);
                }
                break;
            default:
                for (T *ptr = buffer + size; ptr > buffer;) {
                    unsigned char *pb = (unsigned char *)(--ptr), *pe = pb + sizeof(T);
                    for (int i = 0; i < (int)sizeof(T) / 2; ++i) {
                        std::swap(*(pb++), *(--pe));
                    }
                }
        }
    }
}

ReadPFMPlugin::ReadPFMPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions)
: GenericReaderPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, false)
{
}

ReadPFMPlugin::~ReadPFMPlugin()
{
}

template <class PIX, int srcC, int dstC>
static void copyLine(PIX *image, int x1, int x2, int C, PIX *dstPix)
{
    assert(srcC == C);

    const PIX *srcPix = image + x1 * C;
    dstPix += x1 * dstC;

    for(int x = x1; x < x2; ++x) {
        if(srcC == 1) {
            // alpha/grayscale image
            for (int c = 0; c < std::min(dstC,3); ++c) {
                dstPix[c] = srcPix[0];
            }
        } else {
            // color image (if dstC == 1, only the red channel is extracted)
            for (int c = 0; c < std::min(dstC,3); ++c) {
                dstPix[c] = srcPix[c];
            }
        }
        if (dstC == 4) {
            // Alpha is 0 on RGBA images to allow adding alpha using a Roto node.
            // Alpha is set to 0 and premult is set to Opaque.
            // That way, the Roto node can be conveniently used to draw a mask. This shouldn't
            // disturb anything else in the process, since Opaque premult means that alpha should
            // be considered as being 1 everywhere, whatever the actual alpha value is.
            // see GenericWriterPlugin::render, if (userPremult == OFX::eImageOpaque...
            dstPix[3] = 0.f; // alpha
        }

        srcPix += srcC;
        dstPix += dstC;
    }
}

void
ReadPFMPlugin::decode(const std::string& filename,
                      OfxTime /*time*/,
                      int /*view*/,
                      bool /*isPlayback*/,
                      const OfxRectI& renderWindow,
                      float *pixelData,
                      const OfxRectI& bounds,
                      OFX::PixelComponentEnum pixelComponents,
                      int pixelComponentCount,
                      int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // read PFM header
    std::FILE *const nfile = std::fopen(filename.c_str(), "rb");

    char pfm_type, item[1024] = { 0 };
    int W = 0;
    int H = 0;
    int C = 0;
    int err = 0;
    double scale = 0.0;
    while ((err = std::fscanf(nfile, "%1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        int c = std::fgetc(nfile);
        (void)c;
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("PFM header not found in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        int c = std::fgetc(nfile);
        (void)c;
    }
    if (std::sscanf(item, " %d %d", &W, &H) != 2) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("WIDTH and HEIGHT fields are undefined in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (W <= 0 || H <= 0 || 0xffff < W || 0xffff < H) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("invalid WIDTH or HEIGHT fields in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    clearPersistentMessage();
    while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        int c = std::fgetc(nfile);
        (void)c;
    }
    if (std::sscanf(item, "%lf", &scale) != 1) {
        setPersistentMessage(OFX::Message::eMessageWarning, "", std::string("SCALE field is undefined in file \"") + filename + "\".");
    }

    {
        int c = std::fgetc(nfile);
        (void)c;
    }

    const bool is_inverted = (scale > 0) != endianness();
    if (pfm_type == 'F') {
        C = 3;
    } else {
        C = 1;
    }

    std::size_t numpixels = W * C;
    std::vector<float> image(numpixels);

    assert(0 <= renderWindow.x1 && renderWindow.x2 <= W &&
           0 <= renderWindow.y1 && renderWindow.y2 <= H);
    const int x1 = renderWindow.x1;
    const int x2 = renderWindow.x2;

    for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
        std::size_t numread = std::fread(&image.front(), 4, numpixels, nfile);
        if (numread < numpixels) {
            std::fclose(nfile);
            setPersistentMessage(OFX::Message::eMessageError, "", "could not read all the image samples needed");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }

        if (is_inverted) {
            invert_endianness(&image.front(), numpixels);
        }

        // now copy to the dstImg
        float* dstPix = (float*)((char*)pixelData + (y - bounds.y1)*rowBytes);
        if (C == 1) {
            switch (pixelComponentCount) {
                case 1:
                    copyLine<float,1,1>(&image.front(), x1, x2, C, dstPix);
                    break;
                case 2:
                    copyLine<float,1,2>(&image.front(), x1, x2, C, dstPix);
                    break;
                case 3:
                    copyLine<float,1,3>(&image.front(), x1, x2, C, dstPix);
                    break;
                case 4:
                    copyLine<float,1,4>(&image.front(), x1, x2, C, dstPix);
                    break;
                default:
                    break;
            }
        } else if (C == 3) {
            switch (pixelComponentCount) {
                case 1:
                    copyLine<float,3,1>(&image.front(), x1, x2, C, dstPix);
                    break;
                case 2:
                    copyLine<float,3,2>(&image.front(), x1, x2, C, dstPix);
                    break;
                case 3:
                    copyLine<float,3,3>(&image.front(), x1, x2, C, dstPix);
                    break;
                case 4:
                    copyLine<float,3,4>(&image.front(), x1, x2, C, dstPix);
                    break;
                default:
                    break;
            }
        }
    }
    std::fclose(nfile);
}

bool
ReadPFMPlugin::getFrameBounds(const std::string& filename,
                              OfxTime /*time*/,
                              OfxRectI *bounds,
                              double *par,
                              std::string *error,
                              int* tile_width,
                              int* tile_height)
{
    assert(bounds && par);
    // read PFM header
    std::FILE *const nfile = std::fopen(filename.c_str(), "rb");

    char pfm_type, item[1024] = { 0 };
    int W = 0;
    int H = 0;
    int err = 0;
    double scale = 0.0;
    while ((err = std::fscanf(nfile, "%1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        int c = std::fgetc(nfile);
        (void)c;
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        if (error) {
            *error = std::string("PFM header not found in file \"") + filename + "\".";
        }
        return false;
    }
    while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        int c = std::fgetc(nfile);
        (void)c;
    }
    if ((err = std::sscanf(item, " %d %d", &W, &H)) < 2) {
        std::fclose(nfile);
        if (error) {
            *error =  std::string("WIDTH and HEIGHT fields are undefined in file \"") + filename + "\".";
        }
        return false;
    }
    if (err == 2) {
        clearPersistentMessage();
        while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
            int c = std::fgetc(nfile);
            (void)c;
        }
        if (std::sscanf(item, "%lf", &scale) != 1) {
            setPersistentMessage(OFX::Message::eMessageWarning, "", std::string("SCALE field is undefined in file \"") + filename + "\".");
        }
    }
    std::fclose(nfile);

    bounds->x1 = 0;
    bounds->x2 = W;
    bounds->y1 = 0;
    bounds->y2 = H;
    *par = 1.;
    *tile_width = *tile_height = 0;
    return true;
}

void
ReadPFMPlugin::onInputFileChanged(const std::string& /*newFile*/,
                                  bool setColorSpace,
                                  OFX::PreMultiplicationEnum *premult,
                                  OFX::PixelComponentEnum *components,
                                  int *componentCount)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, pfm files are assumed to be linear.
        _ocio->setInputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
#     endif
    }
    assert(premult && components);
    int startingTime = getStartingTime();
    std::string filename;
    OfxStatus st = getFilenameAtTime(startingTime, &filename);
    if (st != kOfxStatOK) {
        return;
    }
    std::stringstream ss;
    
    // read PFM header
    std::FILE *const nfile = std::fopen(filename.c_str(), "rb");
    
    char pfm_type = 0;
    char item[1024] = { 0 };
    int err = 0;
    while ((err = std::fscanf(nfile, "%1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        int c = std::fgetc(nfile);
        (void)c;
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageWarning, "", std::string("PFM header not found in file \"") + filename + "\".");
        return;
    }
    std::fclose(nfile);

    // set the components of _outputClip
    *components = OFX::ePixelComponentNone;
    *componentCount = 0;
    if (pfm_type == 'F') {
        *components = OFX::ePixelComponentRGB;
        *componentCount = 3;
    } else if (pfm_type == 'f') {
        *components = OFX::ePixelComponentAlpha;
        *componentCount = 1;
    } else {
        *premult = OFX::eImageOpaque;
        return;
    }
    if (*components != OFX::ePixelComponentRGBA && *components != OFX::ePixelComponentAlpha) {
        *premult = OFX::eImageOpaque;
    } else {
        // output is always premultiplied
        *premult = OFX::eImagePreMultiplied;
    }
}

using namespace OFX;

mDeclareReaderPluginFactory(ReadPFMPluginFactory, {}, false);

void
ReadPFMPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("pfm");
}

/** @brief The basic describe function, passed a plugin descriptor */
void
ReadPFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, _extensions, kPluginEvaluation, kSupportsTiles, false);
    
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadPFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, true);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadPFMPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    ReadPFMPlugin* ret =  new ReadPFMPlugin(handle, _extensions);
    ret->restoreStateFromParameters();
    return ret;
}


static ReadPFMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
