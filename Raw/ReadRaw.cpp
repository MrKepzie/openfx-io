/*
 OFX Raw reader plugin.
 Reads a raw image using Libraw.
 
 Copyright (C) 2014 INRIA
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


#include "ReadRaw.h"

#include <cstdio> // fopen, fread...
#include <cerrno>
#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#endif

#include <libraw/libraw.h>

#include "GenericReader.h"
#include "GenericOCIO.h"
#include "ofxsMacros.h"

#define kPluginName "ReadRaw"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read Raw files using LibRaw."
#define kPluginIdentifier "fr.inria.openfx.ReadPFM"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha false
#define kSupportsTiles false

static const std::string openErr = std::string("Error while opening file");
static const std::string decodeErr = std::string("Error while decoding informations from file");

static std::string libRawErrStr(LibRaw_errors err)
{
    return libraw_strerror(err);
    /*switch (err) {
        case LIBRAW_SUCCESS:
            return "";
        case LIBRAW_BAD_CROP:
            return "Incorrect cropping coordinates:  the left-top corner of cropping rectangle is outside the image";
        case LIBRAW_CANCELLED_BY_CALLBACK:
            return "Processing cancelled due to calling application demand";
        case LIBRAW_DATA_ERROR:
            return "A fatal error emerged during data unpacking";
        case LIBRAW_FILE_UNSUPPORTED:
            return "Unsupported file format";
        case LIBRAW_INPUT_CLOSED:
            return "Input stream closed";
        case LIBRAW_IO_ERROR:
            return "A fatal error emerged during file reading (premature end-of-file encountered or file is corrupt)";
        case LIBRAW_NO_THUMBNAIL:
            return "Returned upon an attempt to retrieve a thumbnail from a file containing no preview";
        case LIBRAW_OUT_OF_ORDER_CALL:
            return "API functions have been called in wrong order";
        case LIBRAW_REQUEST_FOR_NONEXISTENT_IMAGE:
            return "Attempt to retrieve a RAW image with a number absent in the data file";
        case LIBRAW_UNSPECIFIED_ERROR:
            return "An unknown error has been encountered";
        case LIBRAW_UNSUFFICIENT_MEMORY:
            return "Attempt to get memory from the system has failed";
        case LIBRAW_UNSUPPORTED_THUMBNAIL:
            return "RAW file contains a preview of unsupported format";
    }*/
}

class ReadRawPlugin : public GenericReaderPlugin
{
public:

    ReadRawPlugin(OfxImageEffectHandle handle);

    virtual ~ReadRawPlugin();

private:

    virtual bool isVideoStream(const std::string& /*filename*/) OVERRIDE FINAL { return false; }

    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, double *par, std::string *error) OVERRIDE FINAL;

    virtual void onInputFileChanged(const std::string& newFile, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components) OVERRIDE FINAL;
    
    bool makeErrorString(int err,const std::string& error, const std::string& filename, std::string* errorString);
};


ReadRawPlugin::ReadRawPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, false)
{
}

ReadRawPlugin::~ReadRawPlugin()
{
}



void
ReadRawPlugin::decode(const std::string& filename,
                      OfxTime /*time*/,
                      const OfxRectI& renderWindow,
                      float *pixelData,
                      const OfxRectI& bounds,
                      OFX::PixelComponentEnum pixelComponents,
                      int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Raw: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    
    int numComps = getNComponents(pixelComponents);

    LibRaw rawObj;
    int err = LIBRAW_SUCCESS;
    std::string error;
    
    // Let us open the file
    err = rawObj.open_file(filename.c_str());
    if (err != LIBRAW_SUCCESS) {
        bool fatal = makeErrorString(err, openErr, filename, &error);
        setPersistentMessage(fatal ? OFX::Message::eMessageError : OFX::Message::eMessageWarning, "", error);
        if (fatal) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }
    
    // Let us unpack the image
    err = rawObj.unpack();
    if (err != LIBRAW_SUCCESS) {
        bool fatal = makeErrorString(err, decodeErr, filename, &error);
        setPersistentMessage(fatal ? OFX::Message::eMessageError : OFX::Message::eMessageWarning, "", error);
        if (fatal) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }


    err = rawObj.dcraw_process();
    if (err != LIBRAW_SUCCESS) {
        bool fatal = makeErrorString(err, decodeErr, filename, &error);
        setPersistentMessage(fatal ? OFX::Message::eMessageError : OFX::Message::eMessageWarning, "", error);
        if (fatal) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }
    
    int dstRowSize = (bounds.x2 - bounds.x1) * numComps;
    
    assert(bounds.x1 >= 0 && bounds.y1 >= 0 && bounds.x2 <= rawObj.imgdata.sizes.width && bounds.y2 <= rawObj.imgdata.sizes.height);
    assert(rawObj.imgdata.idata.colors == numComps);
    
    for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
        for (int c = 0; c < numComps; ++c) {
            
            float* dst_pixels = pixelData + (y - bounds.y1) * dstRowSize + (renderWindow.x1 - bounds.x1) * numComps;            
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x/*, ++src_pixels*/, dst_pixels += numComps) {
                ushort pix = rawObj.imgdata.image[(rawObj.imgdata.sizes.height - 1 - y) * rawObj.imgdata.sizes.width + x][c];
                dst_pixels[c] = float(pix / 65535.);
            }
        }
    }
    
}

bool
ReadRawPlugin::makeErrorString(int err,const std::string& error, const std::string& filename, std::string* errorString)
{
    assert(err != LIBRAW_SUCCESS);
    if (err > 0) {
        const char* error = strerror(errno);
        *errorString = error + std::string(" ") + filename + ": " + std::string(error);
    } else if (err < 0) {
        *errorString = error + " " + filename + ": " + std::string(libRawErrStr((LibRaw_errors)err));
    }
    return LIBRAW_FATAL_ERROR(err);
}


bool
ReadRawPlugin::getFrameBounds(const std::string& filename,
                              OfxTime /*time*/,
                              OfxRectI *bounds,
                              double *par,
                              std::string *error)
{
    assert(bounds && par);
    LibRaw rawObj;
    int err = LIBRAW_SUCCESS;
    err = rawObj.open_file(filename.c_str());
    
    if (err != LIBRAW_SUCCESS) {
        if (makeErrorString(err, openErr, filename, error)) {
            return false;
        }
    }

    err = rawObj.adjust_sizes_info_only();
    if (err != LIBRAW_SUCCESS) {
        if (makeErrorString(err, decodeErr, filename, error)) {
            return false;
        }
    }
    
    bounds->x1 = 0;
    bounds->x2 = rawObj.imgdata.sizes.width;
    bounds->y1 = 0;
    bounds->y2 = rawObj.imgdata.sizes.height;
    *par = rawObj.imgdata.sizes.pixel_aspect;
    return true;
}

void
ReadRawPlugin::onInputFileChanged(const std::string& /*newFile*/,
                                  OFX::PreMultiplicationEnum *premult,
                                  OFX::PixelComponentEnum *components)
{
    assert(premult && components);
    *components = OFX::ePixelComponentRGB;
    *premult = OFX::eImageOpaque;
}

using namespace OFX;

mDeclareReaderPluginFactory(ReadRawPluginFactory, {}, {}, false);

/** @brief The basic describe function, passed a plugin descriptor */
void ReadRawPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles, false);
    
    // basic labels
    desc.setLabel(kPluginName);
    
    const char* extensions[] = { "3fr", "ari", "arw", "bay", "crw", "cr2", "cap",
        "dcs", "dcr", "dng", "drf", "eip", "erf", "fff", "iiq",
        "k25", "kdc", "mef", "mos", "mrw", "nef", "nrw", "obm",
        "orf", "pef", "ptx", "pxn", "r3d", "rad", "raf", "rw2",
        "raw", "rwl", "rwz", "srf", "sr2", "srw", "x3f", NULL };
#ifdef OFX_EXTENSIONS_TUTTLE
    
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(80);
#endif
    
    
    std::string description(kPluginDescription "\n");
    description.append("The following file extensions are supported:\n");
    const char** extIt = extensions;
    while (*extIt) {
        description.append("- ");
        description.append(*extIt);
        description.append("\n");
        ++extIt;
    }
    description.append("\n");
    description.append("LibRaw version: ");
    description.append(LibRaw::version());
    desc.setPluginDescription(description);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
ReadRawPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(),
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles);

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect*
ReadRawPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    ReadRawPlugin* ret =  new ReadRawPlugin(handle);
    ret->restoreStateFromParameters();
    return ret;
}


void getReadRawPluginID(OFX::PluginFactoryArray &ids)
{
    static ReadRawPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

