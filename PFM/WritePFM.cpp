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
 * OFX PFM writer plugin.
 * Writes an image in the Portable Float Map (PFM) format.
 */

#include <cstdio> // fopen, fwrite...
#include <vector>
#include <algorithm>

#include "GenericOCIO.h"

#include "GenericWriter.h"
#include "ofxsMacros.h"

#define kPluginName "WritePFM"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write PFM (Portable Float Map) files."
#define kPluginIdentifier "fr.inria.openfx.WritePFM"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 40 // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

/**
 \return \c false for "Little Endian", \c true for "Big Endian".
 **/
static inline bool endianness()
{
    const int x = 1;
    return ((unsigned char *)&x)[0] ? false : true;
}

class WritePFMPlugin : public GenericWriterPlugin
{
public:

    WritePFMPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~WritePFMPlugin();

private:

    virtual void encode(const std::string& filename, OfxTime time, const std::string& viewName, const float *pixelData, const OfxRectI& bounds, float pixelAspectRatio, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;

    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return OFX::eImageUnPreMultiplied; }

    virtual void onOutputFileChanged(const std::string& newFile, bool setColorSpace) OVERRIDE FINAL;
};

WritePFMPlugin::WritePFMPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions)
: GenericWriterPlugin(handle, extensions)
{
}


WritePFMPlugin::~WritePFMPlugin()
{
}

template <class PIX, int srcC, int dstC>
static void copyLine(const PIX* pixelData, int rowbytes, int W, int /*H*/, int C, int y, PIX *image)
{
    assert(srcC == C);

    const PIX *srcPix = (const PIX*)((char*)pixelData + y*rowbytes);
    PIX *dstPix = image;

    for(int x = 0; x < W; ++x) {
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
            dstPix[3] = 1.f; // alpha
        }

        srcPix += srcC;
        dstPix += dstC;
    }
    
}


void WritePFMPlugin::encode(const std::string& filename, OfxTime /*time*/, const std::string& /*viewName*/, const float *pixelData, const OfxRectI& bounds, float /*pixelAspectRatio*/, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only write RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    int spectrum;
    switch(pixelComponents) {
        case OFX::ePixelComponentRGBA:
            spectrum = 4;
            break;
        case OFX::ePixelComponentRGB:
            spectrum = 3;
            break;
        case OFX::ePixelComponentAlpha:
            spectrum = 1;
            break;
        default:
            //spectrum = 0;
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
            return;
    }

    std::FILE *const nfile = std::fopen(filename.c_str(), "wb");
    if (!nfile) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Cannot open file \"" + filename + "\"");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    int width = (bounds.x2 - bounds.x1);
    int height = (bounds.y2 - bounds.y1);

    const int depth = (spectrum == 1 ? 1 : 3);
    const unsigned int buf_size = width * depth;
    std::vector<float> buffer(buf_size);
    std::fill(buffer.begin(), buffer.end(), 0.);

    std::fprintf(nfile, "P%c\n%u %u\n%d.0\n", (spectrum == 1 ? 'f' : 'F'), width, height, endianness() ? 1 : -1);

    for (int y = height-1; y >= 0; --y) {

        // now copy to the dstImg
        if (depth == 1) {
            assert(pixelComponents == OFX::ePixelComponentAlpha);
            copyLine<float,1,1>(pixelData, rowBytes, width, height, spectrum, y, &buffer.front());
        } else if (depth == 3) {
            assert(pixelComponents == OFX::ePixelComponentRGB || pixelComponents == OFX::ePixelComponentRGBA);
            if (pixelComponents == OFX::ePixelComponentRGB) {
                copyLine<float,3,3>(pixelData, rowBytes, width, height, spectrum, y, &buffer.front());
            } else if (pixelComponents == OFX::ePixelComponentRGBA) {
                copyLine<float,4,3>(pixelData, rowBytes, width, height, spectrum, y, &buffer.front());
            }
        }

        std::fwrite(&buffer.front(), sizeof(float), buf_size, nfile);
    }
    std::fclose(nfile);
}

bool WritePFMPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}

void
WritePFMPlugin::onOutputFileChanged(const std::string &/*filename*/,
                                    bool setColorSpace)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, pfm files are assumed to be linear.
        _ocio->setOutputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
#     endif
    }
}

using namespace OFX;

mDeclareWriterPluginFactory(WritePFMPluginFactory, {}, false);

void WritePFMPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("pfm");
}

/** @brief The basic describe function, passed a plugin descriptor */
void WritePFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc,OFX::eRenderFullySafe, _extensions, kPluginEvaluation, false, false);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WritePFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha,
                                                                    "reference", "reference", false);

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WritePFMPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WritePFMPlugin(handle, _extensions);
}


static WritePFMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
