/*
 OFX PFM reader plugin.
 Reads an image in the Portable Float Map (PFM) format.
 
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


#include "ReadPFM.h"
#include "GenericOCIO.h"

#include <cstdio>

static const bool kSupportsTiles = false;

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

ReadPFMPlugin::ReadPFMPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle)
{
}

ReadPFMPlugin::~ReadPFMPlugin()
{
}

template <class PIX, int srcC, int dstC>
static void copyLine(PIX *image, int W, int H, int C, int y, OFX::Image* dstImg)
{
    assert(srcC == C);

    PIX *srcPix = image;
    PIX *dstPix = (PIX *) dstImg->getPixelAddress(0, H-1-y); // inverse y

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

#if 0
template <class PIX, int srcC, int dstC>
static void copyImage(PIX *image, int W, int H, int C, OFX::Image* dstImg)
{
    assert(srcC == C);
    for(int y = 0; y < H; ++y) {
        copyLine<PIX,srcC,dstC>(image + srcC * y, W, H, C, dstImg);
    }

}
#endif


void ReadPFMPlugin::decode(const std::string& filename, OfxTime /*time*/, const OfxRectI& renderWindow, OFX::Image* dstImg)
{
    OFX::PixelComponentEnum pixelComponents  = dstImg->getPixelComponents();

    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "PFM: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
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
        std::fgetc(nfile);
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("PFM header not found in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        std::fgetc(nfile);
    }
    if ((err = std::sscanf(item, " %d %d", &W, &H)) < 2) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("WIDTH and HEIGHT fields are undefined in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (err == 2) {
        clearPersistentMessage();
        while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
            std::fgetc(nfile);
        }
        if (std::sscanf(item, "%lf", &scale) != 1) {
            setPersistentMessage(OFX::Message::eMessageWarning, "", std::string("SCALE field is undefined in file \"") + filename + "\".");
        }
    }

    assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == W && renderWindow.y1 == 0 && renderWindow.y2 == H));

    std::fgetc(nfile);

    const bool is_inverted = (scale > 0) != endianness();
    if (pfm_type == 'F') {
        C = 3;
    } else {
        C = 1;
    }

    int numpixels = W * C;
    float *image = new float[numpixels];

    for (int y = 0; y < H; ++y) {
        int numread = std::fread(image, 4, numpixels, nfile);
        if (numread < numpixels) {
            setPersistentMessage(OFX::Message::eMessageError, "", "could not read all the image samples needed");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }

        if (is_inverted) {
            invert_endianness(image, numpixels);
        }
        std::fclose(nfile);

        // now copy to the dstImg
        if (C == 1) {
            switch (pixelComponents) {
                case OFX::ePixelComponentAlpha:
                    copyLine<float,1,1>(image, W, H, C, y, dstImg);
                    break;
                case OFX::ePixelComponentRGB:
                    copyLine<float,1,3>(image, W, H, C, y, dstImg);
                    break;
                case OFX::ePixelComponentRGBA:
                    copyLine<float,1,4>(image, W, H, C, y, dstImg);
                    break;
                default:
                    break;
            }
        } else if (C == 3) {
            switch (pixelComponents) {
                case OFX::ePixelComponentAlpha:
                    copyLine<float,3,1>(image, W, H, C, y, dstImg);
                    break;
                case OFX::ePixelComponentRGB:
                    copyLine<float,3,3>(image, W, H, C, y, dstImg);
                    break;
                case OFX::ePixelComponentRGBA:
                    copyLine<float,3,4>(image, W, H, C, y, dstImg);
                    break;
                default:
                    break;
            }
        }
    }
    delete [] image;
}

void ReadPFMPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod)
{
    // read PFM header
    std::FILE *const nfile = std::fopen(filename.c_str(), "rb");

    char pfm_type, item[1024] = { 0 };
    int W = 0;
    int H = 0;
    int err = 0;
    double scale = 0.0;
    while ((err = std::fscanf(nfile, "%1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        std::fgetc(nfile);
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("PFM header not found in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        std::fgetc(nfile);
    }
    if ((err = std::sscanf(item, " %d %d", &W, &H)) < 2) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("WIDTH and HEIGHT fields are undefined in file \"") + filename + "\".");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (err == 2) {
        clearPersistentMessage();
        while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
            std::fgetc(nfile);
        }
        if (std::sscanf(item, "%lf", &scale) != 1) {
            setPersistentMessage(OFX::Message::eMessageWarning, "", std::string("SCALE field is undefined in file \"") + filename + "\".");
        }
    }
    std::fclose(nfile);

    rod.x1 = 0;
    rod.x2 = W;
    rod.y1 = 0;
    rod.y2 = H;
}


using namespace OFX;


/** @brief The basic describe function, passed a plugin descriptor */
void ReadPFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    
    // basic labels
    desc.setLabels("ReadPFMOFX", "ReadPFMOFX", "ReadPFMOFX");
    desc.setPluginDescription("Read PFM (Portable Float Map) files.");


#ifdef OFX_EXTENSIONS_TUTTLE

    const char* extensions[] = { "pfm", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadPFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), /*supportsRGBA =*/ true, /*supportsRGB =*/ true, /*supportsAlpha =*/ true, /*supportsTiles =*/ kSupportsTiles);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadPFMPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ReadPFMPlugin(handle);
}
