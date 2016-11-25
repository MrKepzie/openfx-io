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
 * OFX exr Writer plugin.
 * Writes a an output image using the OpenEXR library.
 */

#include <memory>
#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfOutputFile.h>
#include <half.h>


#include <ImfChannelList.h>
#include <IlmThreadPool.h>
#include <ImfArray.h>
#include <ImfOutputFile.h>
#include <half.h>

#include "GenericOCIO.h"
#include "GenericWriter.h"

using namespace OFX;
using namespace OFX::IO;
#ifdef OFX_IO_USING_OCIO
namespace OCIO = OCIO_NAMESPACE;
#endif

using std::string;
using std::vector;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "WriteEXR"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write images using OpenEXR."
#define kPluginIdentifier "fr.inria.openfx.WriteEXR"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 10 // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated

#define kParamWriteEXRCompression "compression"
#define kParamWriteEXRDataType "dataType"

#ifndef OPENEXR_IMF_NAMESPACE
#define OPENEXR_IMF_NAMESPACE Imf
#endif

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#define kSupportsXY false

namespace Imf_ = OPENEXR_IMF_NAMESPACE;


namespace Exr {
static const char* compressionNames[6] = {
    "No compression",
    "Zip (1 scanline)",
    "Zip (16 scanlines)",
    "PIZ Wavelet (32 scanlines)",
    "RLE",
    "B44"
};
static Imf_::Compression
stringToCompression(const string& str)
{
    if (str == compressionNames[0]) {
        return Imf_::NO_COMPRESSION;
    } else if (str == compressionNames[1]) {
        return Imf_::ZIPS_COMPRESSION;
    } else if (str == compressionNames[2]) {
        return Imf_::ZIP_COMPRESSION;
    } else if (str == compressionNames[3]) {
        return Imf_::PIZ_COMPRESSION;
    } else if (str == compressionNames[4]) {
        return Imf_::RLE_COMPRESSION;
    } else {
        return Imf_::B44_COMPRESSION;
    }
}

static const char* depthNames[2] = {
    "16 bit half", "32 bit float"
};
static int
depthNameToInt(const string& name)
{
    if (name == depthNames[0]) {
        return 16;
    } else {
        return 32;
    }
}
}

class WriteEXRPlugin
    : public GenericWriterPlugin
{
public:

    WriteEXRPlugin(OfxImageEffectHandle handle, const vector<string>& extensions);


    virtual ~WriteEXRPlugin();

    //virtual void changedParam(const InstanceChangedArgs &args, const string &paramName);

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
    virtual PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return eImagePreMultiplied; }

    virtual void onOutputFileChanged(const string& newFile, bool setColorSpace) OVERRIDE FINAL;
    ChoiceParam* _compression;
    ChoiceParam* _bitDepth;
};

WriteEXRPlugin::WriteEXRPlugin(OfxImageEffectHandle handle,
                               const vector<string>& extensions)
    : GenericWriterPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsXY, kSupportsAlpha)
    , _compression(0)
    , _bitDepth(0)
{
    _compression = fetchChoiceParam(kParamWriteEXRCompression);
    _bitDepth = fetchChoiceParam(kParamWriteEXRDataType);
}

WriteEXRPlugin::~WriteEXRPlugin()
{
}

//void WriteEXRPlugin::changedParam(const InstanceChangedArgs &/*args*/, const string &paramName)
//{
//}


void
WriteEXRPlugin::encode(const string& filename,
                       const OfxTime /*time*/,
                       const string& /*viewName*/,
                       const float *pixelData,
                       const OfxRectI& bounds,
                       const float pixelAspectRatio,
                       const int pixelDataNComps,
                       const int /*dstNCompsStartIndex*/,
                       const int /*dstNComps*/,
                       const int rowBytes)
{
    ///FIXME: WriteEXR should not disregard dstNComps

    if ( (pixelDataNComps != 4) && (pixelDataNComps != 3) && (pixelDataNComps != 1) ) {
        setPersistentMessage(Message::eMessageError, "", "EXR: can only write RGBA, RGB, or Alpha components images");
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    assert(pixelDataNComps);
    try {
        int compressionIndex;
        _compression->getValue(compressionIndex);

        Imf_::Compression compression( Exr::stringToCompression(Exr::compressionNames[compressionIndex]) );

        int depthIndex;
        _bitDepth->getValue(depthIndex);

        int depth = Exr::depthNameToInt(Exr::depthNames[depthIndex]);
        Imath::Box2i exrDataW;

        exrDataW.min.x = bounds.x1;
        exrDataW.min.y = bounds.y1;
        exrDataW.max.x = bounds.x2 - 1;
        exrDataW.max.y = bounds.y2 - 1;

        Imath::Box2i exrDispW;
        exrDispW.min.x = 0;
        exrDispW.min.y = 0;
        exrDispW.max.x = (bounds.x2 - bounds.x1);
        exrDispW.max.y = (bounds.y2 - bounds.y1);

        Imf_::Header exrheader(exrDispW, exrDataW, pixelAspectRatio,
                               Imath::V2f(0, 0), 1, Imf_::INCREASING_Y, compression);

        Imf_::PixelType pixelType;
        if (depth == 32) {
            pixelType = Imf_::FLOAT;
        } else {
            assert(depth == 16);
            pixelType = Imf_::HALF;
        }

        const char* chanNames[4] = { "R", "G", "B", "A" };
        if (pixelDataNComps == 1) {
            chanNames[0] = chanNames[3];
        }
        for (int chan = 0; chan < pixelDataNComps; ++chan) {
            exrheader.channels().insert( chanNames[chan], Imf_::Channel(pixelType) );
        }

        Imf_::OutputFile outputFile(filename.c_str(), exrheader);

        for (int y = bounds.y1; y < bounds.y2; ++y) {
            /*First we create a row that will serve as the output buffer.
               We copy the scan-line (with y inverted) in the inputImage to the row.*/
            int exrY = bounds.y2 - y - 1;
            float* src_pixels = (float*)( (char*)pixelData + (exrY - bounds.y1) * rowBytes );

            /*we create the frame buffer*/
            Imf_::FrameBuffer fbuf;
            if (depth == 32) {
                for (int chan = 0; chan < pixelDataNComps; ++chan) {
                    fbuf.insert( chanNames[chan], Imf_::Slice(Imf_::FLOAT, (char*)src_pixels + chan, sizeof(float) * pixelDataNComps, 0) );
                }
            } else {
                Imf_::Array2D<half> halfwriterow(pixelDataNComps, bounds.x2 - bounds.x1);

                for (int chan = 0; chan < pixelDataNComps; ++chan) {
                    fbuf.insert( chanNames[chan],
                                 Imf_::Slice(Imf_::HALF,
                                             (char*)(&halfwriterow[chan][0] - exrDataW.min.x),
                                             sizeof(halfwriterow[chan][0]), 0) );
                    const float* from = src_pixels + chan;
                    for (int i = exrDataW.min.x, f = exrDataW.min.x; i < exrDataW.max.x; ++i, f += pixelDataNComps) {
                        halfwriterow[chan][i - exrDataW.min.x] = from[f];
                    }
                }
            }
            outputFile.setFrameBuffer(fbuf);
            outputFile.writePixels(1);
        }
    } catch (const std::exception& e) {
        setPersistentMessage( Message::eMessageError, "", string("OpenEXR error") + ": " + e.what() );
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
} // WriteEXRPlugin::encode

bool
WriteEXRPlugin::isImageFile(const string& /*fileExtension*/) const
{
    return true;
}

void
WriteEXRPlugin::onOutputFileChanged(const string & /*filename*/,
                                    bool setColorSpace)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        // Unless otherwise specified, exr files are assumed to be linear.
        _ocio->setOutputColorspace(OCIO::ROLE_SCENE_LINEAR);
#     endif
    }
}

mDeclareWriterPluginFactory(WriteEXRPluginFactory,; , false);
void
WriteEXRPluginFactory::load()
{
    _extensions.clear();
    _extensions.push_back("exr");
}

void
WriteEXRPluginFactory::unload()
{
    //Kill all threads
    IlmThread::ThreadPool::globalThreadPool().setNumThreads(0);
}

/** @brief The basic describe function, passed a plugin descriptor */
void
WriteEXRPluginFactory::describe(ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc, eRenderFullySafe, _extensions, kPluginEvaluation, false, false);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);

    desc.setIsDeprecated(true); // This plugin was superseeded by WriteOIIO
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
WriteEXRPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsXY, kSupportsAlpha,
                                                                    "scene_linear", "scene_linear", false);

    /////////Compression
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamWriteEXRCompression);
        param->setAnimates(true);
        for (int i = 0; i < 6; ++i) {
            param->appendOption(Exr::compressionNames[i]);
        }
        param->setDefault(3);
        if (page) {
            page->addChild(*param);
        }
    }

    ////////Data type
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamWriteEXRDataType);
        param->setAnimates(true);
        for (int i = 0; i < 2; ++i) {
            param->appendOption(Exr::depthNames[i]);
        }
        param->setDefault(1);
        if (page) {
            page->addChild(*param);
        }
    }

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
WriteEXRPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    WriteEXRPlugin* ret =  new WriteEXRPlugin(handle, _extensions);

    ret->restoreStateFromParams();

    return ret;
}

static WriteEXRPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
