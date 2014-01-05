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
#include "exrWriter.h"

#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfOutputFile.h>
#include <half.h>

#include <ofxsMultiThread.h>


#define kExrWriterCompressionParamName "compression"
#define kExrWriterDataTypeParamName "dataType"

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

ExrWriterPlugin::ExrWriterPlugin(OfxImageEffectHandle handle)
: GenericWriterPlugin(handle)
, _compression(0)
, _bitDepth(0)
{
    _compression = fetchChoiceParam(kExrWriterCompressionParamName);
    _bitDepth = fetchChoiceParam(kExrWriterDataTypeParamName);
}

ExrWriterPlugin::~ExrWriterPlugin(){
    
}

static void supportedFileFormats_static(std::vector<std::string>* formats) {
    formats->push_back("exr");
}

void ExrWriterPlugin::supportedFileFormats(std::vector<std::string>* formats) const{
    supportedFileFormats_static(formats);
}

void ExrWriterPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName){
    
}


void ExrWriterPlugin::initializeLut(){
    //exr files are stored linear
    _lut = 0;
}

void ExrWriterPlugin::encode(const std::string& filename,OfxTime time,const OFX::Image* srcImg){
    try {
        int compressionIndex;
        _compression->getValue(compressionIndex);
        
        Imf_::Compression compression(Exr::stringToCompression(Exr::compressionNames[compressionIndex]));
        
        int depthIndex;
        _bitDepth->getValue(depthIndex);
        
        int depth = Exr::depthNameToInt(Exr::depthNames[depthIndex]);
        Imath::Box2i exrDataW;
        
        OfxRectI rod = srcImg->getBounds();
        
        exrDataW.min.x = rod.x1;
        exrDataW.min.y = rod.y1;
        exrDataW.max.x = rod.x2 - 1;
        exrDataW.max.y = rod.y2 - 1;
        
        Imath::Box2i exrDispW;
        exrDispW.min.x = 0;
        exrDispW.min.y = 0;
        exrDispW.max.x = (rod.x2 - rod.x1);
        exrDispW.max.y = (rod.y2 - rod.y1);

        Imf_::Header exrheader(exrDispW, exrDataW, 1.,
                               Imath::V2f(0, 0), 1, Imf_::INCREASING_Y, compression);
        
        Imf_::PixelType pixelType;
        if(depth == 32){
            pixelType = Imf_::FLOAT;
        }else{
            assert(depth == 16);
            pixelType = Imf_::HALF;
        }
        
        const char* chanNames[4] = { "R" , "G" , "B" , "A" };
        
        for (int chan = 0; chan < 4; ++chan) {
            exrheader.channels().insert(chanNames[chan],Imf_::Channel(pixelType));
        }
        
        Imf_::OutputFile outputFile(filename.c_str(),exrheader);
        
        for (int y = rod.y1; y < rod.y2; ++y) {
            /*First we create a row that will serve as the output buffer.
             We copy the scan-line (with y inverted) in the inputImage to the row.*/
            int exrY = rod.y2 - y - 1;
            
            float* src_pixels = (float*)srcImg->getPixelAddress(rod.x1, exrY);
            
            /*we create the frame buffer*/
            Imf_::FrameBuffer fbuf;
            Imf_::Array2D<half>* halfwriterow = 0 ;
            if (depth == 32) {
                for (int chan = 0; chan < 4; ++chan) {
                    fbuf.insert(chanNames[chan],Imf_::Slice(Imf_::FLOAT, (char*)src_pixels + chan,sizeof(float) * 4, 0));
                }
            } else {
                halfwriterow = new Imf_::Array2D<half>(4 ,rod.x2 - rod.x1);
                
                for(int chan = 0; chan < 4 ; ++chan){
                    fbuf.insert(chanNames[chan],
                                Imf_::Slice(Imf_::HALF,
                                            (char*)(&(*halfwriterow)[chan][0] - exrDataW.min.x),
                                            sizeof((*halfwriterow)[chan][0]), 0));
                    const float* from = src_pixels + chan;
                    for (int i = exrDataW.min.x,f = exrDataW.min.x; i < exrDataW.max.x ; ++i, f += 4) {
                        (*halfwriterow)[chan][i - exrDataW.min.x] = from[f];
                    }
                }
                delete halfwriterow;
            }
            outputFile.setFrameBuffer(fbuf);
            outputFile.writePixels(1);
        }

        
        
    }catch (const std::exception& e) {
        setPersistentMessage(OFX::Message::eMessageError, "",std::string("OpenEXR error") + ": " + e.what());
        return;
    }
}

bool ExrWriterPlugin::isImageFile(const std::string& /*fileExtension*/) const{
    return true;
}


using namespace OFX;
mDeclareWriterPluginFactory(ExrWriterPluginFactory, {}, {});




void ExrWriterPluginFactory::supportedFileFormats(std::vector<std::string>* formats) const{
    supportedFileFormats_static(formats);
}

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ExrWriterPluginFactory p("fr.inria.openfx:exrWriter", 1, 0);
            ids.push_back(&p);
        }
    };
};


/** @brief The basic describe function, passed a plugin descriptor */
void ExrWriterPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("ExrWriterOFX", "ExrWriterOFX", "ExrWriterOFX");
    desc.setPluginDescription("Writes image file using the OpenEXR library.");
    
    
    
    GenericWriterPluginFactory::describe(desc);
    
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ExrWriterPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    
    ////base class params
    GenericWriterPluginFactory::describeInContext(desc, context);
    

    /////////Compression
    OFX::ChoiceParamDescriptor* compressionParam = desc.defineChoiceParam(kExrWriterCompressionParamName);
    compressionParam->setAnimates(false);
    for (int i =0; i < 6; ++i) {
        compressionParam->appendOption(Exr::compressionNames[i]);
    }
    compressionParam->setDefault(3);
    
    ////////Data type
    OFX::ChoiceParamDescriptor* dataTypeParam = desc.defineChoiceParam(kExrWriterDataTypeParamName);
    dataTypeParam->setAnimates(false);
    for(int i = 0 ; i < 2 ; ++i) {
        dataTypeParam->appendOption(Exr::depthNames[i]);
    }
    compressionParam->setDefault(1);
    
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ExrWriterPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ExrWriterPlugin(handle);
}
