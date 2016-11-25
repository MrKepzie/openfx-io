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

#include <cstdio> // fopen, fwrite, fprintf...
#include <vector>
#include <algorithm>

#include "GenericOCIO.h"

#include "GenericWriter.h"
#include "ofxsMacros.h"
#include "ofxsFileOpen.h"

using namespace OFX;
using namespace IO;
#ifdef OFX_IO_USING_OCIO
namespace OCIO = OCIO_NAMESPACE;
#endif

using std::string;
using std::vector;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "WritePFM"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write PFM (Portable Float Map) files."
#define kPluginIdentifier "fr.inria.openfx.WritePFM"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 40 // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsXY false
#define kSupportsAlpha true

/**
   \return \c false for "Little Endian", \c true for "Big Endian".
 **/
static inline bool
endianness()
{
    const int x = 1;

    return ( (unsigned char *)&x )[0] ? false : true;
}

class WritePFMPlugin
    : public GenericWriterPlugin
{
public:

    WritePFMPlugin(OfxImageEffectHandle handle, const vector<string>& extensions);

    virtual ~WritePFMPlugin();

private:

    virtual void encode(const string& filename,
                        const OfxTime time,
                        const string& viewName,
                        const float *pixelData,
                        const OfxRectI& bounds,
                        const float pixelAspectRatio,
                        const int pixelDataNComps,
                        const int dstNCompsStartIndex,
                        const int dstNComps,
                        const int rowBytes) OVERRIDE FINAL;
    virtual bool isImageFile(const string& fileExtension) const OVERRIDE FINAL;
    virtual PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return eImageUnPreMultiplied; }

    virtual void onOutputFileChanged(const string& newFile, bool setColorSpace) OVERRIDE FINAL;
};

WritePFMPlugin::WritePFMPlugin(OfxImageEffectHandle handle,
                               const vector<string>& extensions)
    : GenericWriterPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsXY, kSupportsAlpha)
{
}

WritePFMPlugin::~WritePFMPlugin()
{
}

template <class PIX, int srcC, int dstC>
static void
copyLine(const PIX* pixelData,
         int rowbytes,
         int W,
         int /*H*/,
         int dstNCompsStartIndex,
         int C,
         int y,
         PIX *image)
{
    assert(dstC == 3 || dstC == 1);

    const PIX *srcPix = (const PIX*)( (char*)pixelData + y * rowbytes );
    PIX *dstPix = image;

    for (int x = 0; x < W; ++x) {
        if (srcC == 1) {
            // alpha/grayscale image
            for (int c = 0; c < std::min(dstC, 3); ++c) {
                dstPix[c] = srcPix[dstNCompsStartIndex];
            }
        } else {
            // color image (if dstC == 1, only the red channel is extracted)
            for (int c = 0; c < std::min(dstC, 3); ++c) {
                dstPix[c] = srcPix[dstNCompsStartIndex + c];
            }
        }

        srcPix += C;
        dstPix += dstC;
    }
}

void
WritePFMPlugin::encode(const string& filename,
                       const OfxTime /*time*/,
                       const string& /*viewName*/,
                       const float *pixelData,
                       const OfxRectI& bounds,
                       const float /*pixelAspectRatio*/,
                       const int pixelDataNComps,
                       const int dstNCompsStartIndex,
                       const int dstNComps,
                       const int rowBytes)
{
    if ( (dstNComps != 4) && (dstNComps != 3) && (dstNComps != 1) ) {
        setPersistentMessage(Message::eMessageError, "", "PFM: can only write RGBA, RGB or Alpha components images");
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    std::FILE *const nfile = fopen_utf8(filename.c_str(), "wb");
    if (!nfile) {
        setPersistentMessage(Message::eMessageError, "", "Cannot open file \"" + filename + "\"");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    int width = (bounds.x2 - bounds.x1);
    int height = (bounds.y2 - bounds.y1);
    const int depth = (dstNComps == 1 ? 1 : 3);
    const unsigned int buf_size = width * depth;
    vector<float> buffer(buf_size);
    std::fill(buffer.begin(), buffer.end(), 0.);

    std::fprintf(nfile, "P%c\n%u %u\n%d.0\n", (dstNComps == 1 ? 'f' : 'F'), width, height, endianness() ? 1 : -1);

    for (int y = 0; y < height; ++y) {
        // now copy to the dstImg
        if (depth == 1) {
            assert(dstNComps == 1);
            copyLine<float, 1, 1>( pixelData, rowBytes, width, height,  dstNCompsStartIndex, pixelDataNComps, y, &buffer.front() );
        } else if (depth == 3) {
            assert(dstNComps == 3 || dstNComps == 4);
            if (dstNComps == 3) {
                copyLine<float, 3, 3>( pixelData, rowBytes, width, height, dstNCompsStartIndex, pixelDataNComps, y, &buffer.front() );
            } else if (dstNComps == 4) {
                copyLine<float, 4, 3>( pixelData, rowBytes, width, height, dstNCompsStartIndex, pixelDataNComps, y, &buffer.front() );
            }
        }

        std::fwrite(&buffer.front(), sizeof(float), buf_size, nfile);
    }
    std::fclose(nfile);
}

bool
WritePFMPlugin::isImageFile(const string& /*fileExtension*/) const
{
    return true;
}

void
WritePFMPlugin::onOutputFileChanged(const string & /*filename*/,
                                    bool setColorSpace)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, pfm files are assumed to be linear.
        _ocio->setOutputColorspace(OCIO::ROLE_SCENE_LINEAR);
#     endif
    }
}

mDeclareWriterPluginFactory(WritePFMPluginFactory, {}, false);
void
WritePFMPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("pfm");
}

/** @brief The basic describe function, passed a plugin descriptor */
void
WritePFMPluginFactory::describe(ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc, eRenderFullySafe, _extensions, kPluginEvaluation, false, false);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
WritePFMPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,
                                                                    kSupportsRGBA,
                                                                    kSupportsRGB,
                                                                    kSupportsXY,
                                                                    kSupportsAlpha,
                                                                    "scene_linear", "scene_linear", false);

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
WritePFMPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    WritePFMPlugin* ret = new WritePFMPlugin(handle, _extensions);

    ret->restoreStateFromParams();

    return ret;
}

static WritePFMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
