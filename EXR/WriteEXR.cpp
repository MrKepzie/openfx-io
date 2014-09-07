/*
 OFX exrWriter plugin.
 Writes a an output image using the OpenEXR library.
 
 Copyright (C) 2013 INRIA
 Author Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
 
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
#include "WriteEXR.h"

#include <memory>
#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfOutputFile.h>
#include <half.h>


#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfOutputFile.h>
#include <half.h>

#include "GenericWriter.h"

#define kPluginName "WriteEXR"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write images using OpenEXR."
#define kPluginIdentifier "fr.inria.openfx.WriteEXR"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kWriteEXRCompressionParamName "compression"
#define kWriteEXRDataTypeParamName "dataType"

#ifndef OPENEXR_IMF_NAMESPACE
#define OPENEXR_IMF_NAMESPACE Imf
#endif

namespace Imf_ = OPENEXR_IMF_NAMESPACE;


namespace Exr {
    
    static std::string const compressionNames[6]={
        "No compression",
        "Zip (1 scanline)",
        "Zip (16 scanlines)",
        "PIZ Wavelet (32 scanlines)",
        "RLE",
        "B44"
    };
    
    static Imf_::Compression stringToCompression(const std::string& str){
        if(str == compressionNames[0]){
            return Imf_::NO_COMPRESSION;
        }else if(str == compressionNames[1]){
            return Imf_::ZIPS_COMPRESSION;
        }else if(str == compressionNames[2]){
            return Imf_::ZIP_COMPRESSION;
        }else if(str == compressionNames[3]){
            return Imf_::PIZ_COMPRESSION;
        }else if(str == compressionNames[4]){
            return Imf_::RLE_COMPRESSION;
        }else{
            return Imf_::B44_COMPRESSION;
        }
    }
    
    static  std::string const depthNames[2] = {
        "16 bit half", "32 bit float"
    };
    
    static int depthNameToInt(const std::string& name){
        if(name == depthNames[0]){
            return 16;
        }else{
            return 32;
        }
    }
    
    
}

class WriteEXRPlugin : public GenericWriterPlugin
{
public:

    WriteEXRPlugin(OfxImageEffectHandle handle);


    virtual ~WriteEXRPlugin();

    //virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

private:

    virtual void encode(const std::string& filename, OfxTime time, const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes) OVERRIDE FINAL;

    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;

    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const { return OFX::eImagePreMultiplied; }

    OFX::ChoiceParam* _compression;
    OFX::ChoiceParam* _bitDepth;
    
};

WriteEXRPlugin::WriteEXRPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
, _compression(0)
, _bitDepth(0)
{
    _compression = fetchChoiceParam(kWriteEXRCompressionParamName);
    _bitDepth = fetchChoiceParam(kWriteEXRDataTypeParamName);
}

WriteEXRPlugin::~WriteEXRPlugin(){
    
}

//void WriteEXRPlugin::changedParam(const OFX::InstanceChangedArgs &/*args*/, const std::string &paramName)
//{
//}


void WriteEXRPlugin::encode(const std::string& filename,
                            OfxTime /*time*/,
                            const float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "EXR: can only write RGBA, RGB, or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    int numChannels = 0;
    switch(pixelComponents)
    {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    assert(numChannels);
    try {
        int compressionIndex;
        _compression->getValue(compressionIndex);
        
        Imf_::Compression compression(Exr::stringToCompression(Exr::compressionNames[compressionIndex]));
        
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

        Imf_::Header exrheader(exrDispW, exrDataW, 1.,
                               Imath::V2f(0, 0), 1, Imf_::INCREASING_Y, compression);
        
        Imf_::PixelType pixelType;
        if (depth == 32) {
            pixelType = Imf_::FLOAT;
        } else {
            assert(depth == 16);
            pixelType = Imf_::HALF;
        }

        const char* chanNames[4] = { "R" , "G" , "B" , "A" };
        if (pixelComponents == OFX::ePixelComponentAlpha) {
            chanNames[0] = chanNames[3];
        }
        for (int chan = 0; chan < numChannels; ++chan) {
            exrheader.channels().insert(chanNames[chan],Imf_::Channel(pixelType));
        }

        Imf_::OutputFile outputFile(filename.c_str(),exrheader);
        
        for (int y = bounds.y1; y < bounds.y2; ++y) {
            /*First we create a row that will serve as the output buffer.
             We copy the scan-line (with y inverted) in the inputImage to the row.*/
            int exrY = bounds.y2 - y - 1;
            
            float* src_pixels = (float*)((char*)pixelData + (exrY - bounds.y1)*rowBytes);
            
            /*we create the frame buffer*/
            Imf_::FrameBuffer fbuf;
            if (depth == 32) {
                for (int chan = 0; chan < numChannels; ++chan) {
                    fbuf.insert(chanNames[chan],Imf_::Slice(Imf_::FLOAT, (char*)src_pixels + chan, sizeof(float) * numChannels, 0));
                }
            } else {
                Imf_::Array2D<half> halfwriterow(numChannels ,bounds.x2 - bounds.x1);
                
                for (int chan = 0; chan < numChannels; ++chan) {
                    fbuf.insert(chanNames[chan],
                                Imf_::Slice(Imf_::HALF,
                                            (char*)(&halfwriterow[chan][0] - exrDataW.min.x),
                                            sizeof(halfwriterow[chan][0]), 0));
                    const float* from = src_pixels + chan;
                    for (int i = exrDataW.min.x,f = exrDataW.min.x; i < exrDataW.max.x ; ++i, f += numChannels) {
                        halfwriterow[chan][i - exrDataW.min.x] = from[f];
                    }
                }
            }
            outputFile.setFrameBuffer(fbuf);
            outputFile.writePixels(1);
        }

        
        
    } catch (const std::exception& e) {
        setPersistentMessage(OFX::Message::eMessageError, "",std::string("OpenEXR error") + ": " + e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
}

bool WriteEXRPlugin::isImageFile(const std::string& /*fileExtension*/) const{
    return true;
}


using namespace OFX;

mDeclareWriterPluginFactory(WriteEXRPluginFactory, {}, {}, false);


/** @brief The basic describe function, passed a plugin descriptor */
void WriteEXRPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc);
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginDescription(kPluginDescription);

#ifdef OFX_EXTENSIONS_TUTTLE
    const char* extensions[] = { "exr", NULL };
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(10);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WriteEXRPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,isVideoStreamPlugin(), /*supportsRGBA =*/true, /*supportsRGB=*/true, /*supportsAlpha=*/true, "reference", "reference");

    /////////Compression
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kWriteEXRCompressionParamName);
        param->setAnimates(true);
        for (int i =0; i < 6; ++i) {
            param->appendOption(Exr::compressionNames[i]);
        }
        param->setDefault(3);
        page->addChild(*param);
    }

    ////////Data type
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kWriteEXRDataTypeParamName);
        param->setAnimates(true);
        for(int i = 0 ; i < 2 ; ++i) {
            param->appendOption(Exr::depthNames[i]);
        }
        param->setDefault(1);
        page->addChild(*param);
    }

    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteEXRPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WriteEXRPlugin(handle);
}

void getWriteEXRPluginID(OFX::PluginFactoryArray &ids)
{
    static WriteEXRPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
