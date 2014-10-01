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

#include <cstdio> // fopen, fread...

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#endif

#include "GenericReader.h"
#include "GenericOCIO.h"
#include "ofxsMacros.h"

#define kPluginName "ReadPFMOFX"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read PFM (Portable Float Map) files."
#define kPluginIdentifier "fr.inria.openfx.ReadPFM"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#define kSupportsTiles false

class ReadPFMPlugin : public GenericReaderPlugin
{
public:

    ReadPFMPlugin(OfxImageEffectHandle handle);

    virtual ~ReadPFMPlugin();

private:

    virtual bool isVideoStream(const std::string& filename) OVERRIDE FINAL { return false; }

    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, double *par, std::string *error) OVERRIDE FINAL;

    virtual void onInputFileChanged(const std::string& newFile, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components) OVERRIDE FINAL;

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

ReadPFMPlugin::ReadPFMPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles)
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
            // Alpha is 0 on RGBA images to allow adding alpha using a Roto node
            dstPix[3] = 0.; // alpha
        }

        srcPix += srcC;
        dstPix += dstC;
    }
}

void
ReadPFMPlugin::decode(const std::string& filename,
                      OfxTime /*time*/,
                      const OfxRectI& renderWindow,
                      float *pixelData,
                      const OfxRectI& bounds,
                      OFX::PixelComponentEnum pixelComponents,
                      int rowBytes)
{
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

    std::fgetc(nfile);

    const bool is_inverted = (scale > 0) != endianness();
    if (pfm_type == 'F') {
        C = 3;
    } else {
        C = 1;
    }

    int numpixels = W * C;
    std::vector<float> image(numpixels);

    assert(0 <= renderWindow.x1 && renderWindow.x2 <= W &&
           0 <= renderWindow.y1 && renderWindow.y2 <= H);
    const int x1 = renderWindow.x1;
    const int x2 = renderWindow.x2;

    for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
        int numread = std::fread(image.data(), 4, numpixels, nfile);
        if (numread < numpixels) {
            setPersistentMessage(OFX::Message::eMessageError, "", "could not read all the image samples needed");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }

        if (is_inverted) {
            invert_endianness(image.data(), numpixels);
        }

        // now copy to the dstImg
        float* dstPix = (float*)((char*)pixelData + (y - bounds.y1)*rowBytes);
        if (C == 1) {
            switch (pixelComponents) {
                case OFX::ePixelComponentAlpha:
                    copyLine<float,1,1>(image.data(), x1, x2, C, dstPix);
                    break;
                case OFX::ePixelComponentRGB:
                    copyLine<float,1,3>(image.data(), x1, x2, C, dstPix);
                    break;
                case OFX::ePixelComponentRGBA:
                    copyLine<float,1,4>(image.data(), x1, x2, C, dstPix);
                    break;
                default:
                    break;
            }
        } else if (C == 3) {
            switch (pixelComponents) {
                case OFX::ePixelComponentAlpha:
                    copyLine<float,3,1>(image.data(), x1, x2, C, dstPix);
                    break;
                case OFX::ePixelComponentRGB:
                    copyLine<float,3,3>(image.data(), x1, x2, C, dstPix);
                    break;
                case OFX::ePixelComponentRGBA:
                    copyLine<float,3,4>(image.data(), x1, x2, C, dstPix);
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
                              std::string *error)
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
        std::fgetc(nfile);
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        if (error) {
            *error = std::string("PFM header not found in file \"") + filename + "\".";
        }
        return false;
    }
    while ((err = std::fscanf(nfile, " %1023[^\n]", item)) != EOF && (*item == '#' || !err)) {
        std::fgetc(nfile);
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
            std::fgetc(nfile);
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
    return true;
}

void
ReadPFMPlugin::onInputFileChanged(const std::string& newFile,
                                  OFX::PreMultiplicationEnum *premult,
                                  OFX::PixelComponentEnum *components)
{
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
        std::fgetc(nfile);
    }
    if (std::sscanf(item, " P%c", &pfm_type) != 1) {
        std::fclose(nfile);
        setPersistentMessage(OFX::Message::eMessageWarning, "", std::string("PFM header not found in file \"") + filename + "\".");
    }
    
    // set the components of _outputClip
    *components = OFX::ePixelComponentNone;
    if (pfm_type == 'F') {
        *components = OFX::ePixelComponentRGB;
    } else if (pfm_type == 'f') {
        *components = OFX::ePixelComponentAlpha;
    } else {
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

mDeclareReaderPluginFactory(ReadPFMPluginFactory, {}, {}, false);

/** @brief The basic describe function, passed a plugin descriptor */
void ReadPFMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginDescription(kPluginDescription);


#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "pfm", NULL };
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(60); // better than ReadOIIO
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadPFMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadPFMPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    ReadPFMPlugin* ret =  new ReadPFMPlugin(handle);
    ret->restoreStateFromParameters();
    return ret;
}


void getReadPFMPluginID(OFX::PluginFactoryArray &ids)
{
    static ReadPFMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

