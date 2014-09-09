/*
 OFX PFM writer plugin.
 Writes an image in the Portable Float Map (PFM) format.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay frederic.devernay@inria.fr

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

#include "WritePFM.h"

#include <cstdio>
#include <vector>

#include "GenericOCIO.h"

#include "GenericWriter.h"
#include "ofxsMacros.h"

#define kPluginName "WritePFMOFX"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write PFM (Portable Float Map) files."
#define kPluginIdentifier "fr.inria.openfx.WritePFM"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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

    WritePFMPlugin(OfxImageEffectHandle handle);

    virtual ~WritePFMPlugin();

private:

    virtual void encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;

    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const { return OFX::eImageUnPreMultiplied; }
};

WritePFMPlugin::WritePFMPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
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
            dstPix[3] = 1.; // alpha
        }

        srcPix += srcC;
        dstPix += dstC;
    }
    
}


void WritePFMPlugin::encode(const std::string& filename, OfxTime /*time*/, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only write RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    int spectrum;
    switch(pixelComponents)
    {
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
            spectrum = 0;
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    std::FILE *const nfile = std::fopen(filename.c_str(), "wb");
    if (!nfile) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Cannot open file \"" + filename + "\"");
        OFX::throwSuiteStatusException(kOfxStatFailed);
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
            switch (pixelComponents) {
                case OFX::ePixelComponentAlpha:
                    copyLine<float,1,1>(pixelData, rowBytes, width, height, spectrum, y, buffer.data());
                    break;
                case OFX::ePixelComponentRGB:
                    copyLine<float,3,1>(pixelData, rowBytes, width, height, spectrum, y, buffer.data());
                    break;
                case OFX::ePixelComponentRGBA:
                    copyLine<float,4,1>(pixelData, rowBytes, width, height, spectrum, y, buffer.data());
                    break;
                default:
                    break;
            }
        } else if (depth == 3) {
            switch (pixelComponents) {
                case OFX::ePixelComponentAlpha:
                    copyLine<float,1,3>(pixelData, rowBytes, width, height, spectrum, y, buffer.data());
                    break;
                case OFX::ePixelComponentRGB:
                    copyLine<float,3,3>(pixelData, rowBytes, width, height, spectrum, y, buffer.data());
                    break;
                case OFX::ePixelComponentRGBA:
                    copyLine<float,4,3>(pixelData, rowBytes, width, height, spectrum, y, buffer.data());
                    break;
                default:
                    break;
            }
        }

        std::fwrite(buffer.data(), sizeof(float), buf_size, nfile);
    }
    std::fclose(nfile);
}

bool WritePFMPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}


using namespace OFX;

mDeclareWriterPluginFactory(WritePFMPluginFactory, {}, {}, false);

/** @brief The basic describe function, passed a plugin descriptor */
void WritePFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc);
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginDescription(kPluginDescription);

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "pfm", NULL };
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(40);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WritePFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{    
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha,
                                                                    "reference", "reference");

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WritePFMPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WritePFMPlugin(handle);
}


void getWritePFMPluginID(OFX::PluginFactoryArray &ids)
{
    static WritePFMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
