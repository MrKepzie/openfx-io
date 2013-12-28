/*
 OFX ffmpegReader plugin.
 Reads a video input file using the libav library.
 
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
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */
#include <cmath>

#include "GenericReader.h"

#include "FfmpegHandler.h"
#include <ofxsColorSpace.h>

class FfmpegReaderPlugin : public GenericReaderPlugin {
    
    FfmpegFile* _ffmpegFile; //< a ptr to the ffmpeg file, don't delete it the FfmpegFileManager handles their allocation/deallocation
    
    unsigned char* _buffer;
    int _bufferWidth;
    int _bufferHeight;
    
public:
    
    FfmpegReaderPlugin(OfxImageEffectHandle handle)
    : GenericReaderPlugin(handle)
    , _ffmpegFile(0)
    , _buffer(0)
    , _bufferWidth(0)
    , _bufferHeight(0)
    {
        ///initialize the manager if it isn't
        FFmpegFileManager::s_readerManager.initialize();
    }
    
    virtual ~FfmpegReaderPlugin() {
        FFmpegFileManager::s_readerManager.release(_ffmpegFile->filename());
        
        if(_buffer){
            delete [] _buffer;
        }
    }
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
private:
    
    virtual void supportedFileFormats(std::vector<std::string>* formats) const {
        formats->push_back("avi");
        formats->push_back("flv");
        formats->push_back("mov");
        formats->push_back("mp4");
        formats->push_back("mkv");
        formats->push_back("r3d");
        formats->push_back("bmp");
        formats->push_back("pix");
        formats->push_back("dpx");
        formats->push_back("exr");
        formats->push_back("jpeg");
        formats->push_back("jpg");
        formats->push_back("png");
        formats->push_back("ppm");
        formats->push_back("ptx");
        formats->push_back("tiff");
        formats->push_back("tga");
    }
    
    virtual void decode(const std::string& filename,OfxTime time,OFX::Image* dstImg);
    
    virtual void initializeLut();
    
    virtual bool getTimeDomainForVideoStream(const std::string& filename,OfxRangeD &range);
    
    virtual bool areHeaderAndDataTied(const std::string& filename,OfxTime time) const;
    
    virtual void getFrameRegionOfDefinition(const std::string& /*filename*/,OfxTime time,OfxRectD& rod);
};


void FfmpegReaderPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    
    if(paramName == kReaderFileParamName) {
        std::string filename;
        _fileParam->getValue(filename);
        _ffmpegFile = FFmpegFileManager::s_readerManager.get(filename);
        
        if(_ffmpegFile->invalid()) {
            setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
            return;
        }
    }
}

void FfmpegReaderPlugin::decode(const std::string& filename,OfxTime time,OFX::Image* dstImg){
    
    _ffmpegFile = FFmpegFileManager::s_readerManager.get(filename);
    
    if(_ffmpegFile->invalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        return;
    }
    
    int width,height,frames;
    double ap;
    
    _ffmpegFile->info(width, height, ap, frames);
    
    OfxRectI imgBounds = dstImg->getBounds();
    
    if((imgBounds.x2 - imgBounds.x1) != width ||
       (imgBounds.y2 - imgBounds.y1) != height){
        setPersistentMessage(OFX::Message::eMessageFatal, "", "The host provided an image of wrong size, can't decode.");
    }
    
    ///set the pixel aspect ratio
    dstImg->getPropertySet().propSetDouble(kOfxImagePropPixelAspectRatio, ap, 0);
    
    if(_bufferWidth != width || _bufferHeight != height){
        delete [] _buffer;
        _buffer = 0;
    }
    
    if(!_buffer){
        _buffer = new unsigned char[width * height * 3];
        _bufferHeight = height;
        _bufferWidth = width;
    }

    
    if (!_ffmpegFile->decode(_buffer, std::floor(time+0.5))) { //< round the time to an int to get a frame number ? Not sure about this
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
    }
    
    ///we (aka the GenericReader) only support float bit depth
    /// and RGBA output clip
    OFX::BitDepthEnum e = dstImg->getPixelDepth();
    if(e != OFX::eBitDepthFloat){
        return;
    }
    
    ///do pixel transfer to the ofx image and color-space conversion if the host hopefully implements the color-space conversion suite.
    _lut->from_byte_packed((float*)dstImg->getPixelAddress(0, 0), _buffer, imgBounds, imgBounds, imgBounds, OFX::Color::Lut::PACKING_RGB, OFX::Color::Lut::PACKING_RGBA, true, false);
    
}

void FfmpegReaderPlugin::initializeLut() {
    
    ///we must check if the host has the color-space suite, otherwise we just use the handy
    ///functions from the OFX::Color::Linear class to do the pixels transfer
    if(hasColorSpaceSuite()){
        _lut = new OFX::Color::sRGBLut();
    }else{
        _lut = new OFX::Color::Linear();
    }
}

bool FfmpegReaderPlugin::getTimeDomainForVideoStream(const std::string& filename,OfxRangeD &range) {
    _ffmpegFile = FFmpegFileManager::s_readerManager.get(filename);
    
    if(_ffmpegFile->invalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        return false;
    }
    
    int width,height,frames;
    double ap;
    _ffmpegFile->info(width, height, ap, frames);
    range.min = 0;
    range.max = frames - 1;
    return true;
    
}

bool FfmpegReaderPlugin::areHeaderAndDataTied(const std::string& filename,OfxTime time) const {
    ///not sure about this
    return false;
}

void FfmpegReaderPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime time,OfxRectD& rod) {
    
    _ffmpegFile = FFmpegFileManager::s_readerManager.get(filename);
    
    if(_ffmpegFile->invalid()) {
        setPersistentMessage(OFX::Message::eMessageError, "", _ffmpegFile->error());
        return;
    }

    
    int width,height,frames;
    double ap;
    _ffmpegFile->info(width, height, ap, frames);
    rod.x1 = 0;
    rod.x2 = width;
    rod.y1 = 0;
    rod.y2 = height;
    
}

using namespace OFX;
mDeclarePluginFactory(FfmpegReaderPluginFactory, {}, {});

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static FfmpegReaderPluginFactory p("net.sf.openfx:ffmpegReader", 1, 0);
            ids.push_back(&p);
        }
    };
};

/** @brief The basic describe function, passed a plugin descriptor */
void FfmpegReaderPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("FfmpegReaderOFX", "FfmpegReaderOFX", "FfmpegReaderOFX");
    desc.setPluginDescription("Reads image or video file using the libav");
    
    OFX::Plugin::describeGenericReader(desc);
    
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void FfmpegReaderPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    OFX::Plugin::defineGenericReaderParamsInContext(desc, context);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* FfmpegReaderPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new FfmpegReaderPlugin(handle);
}

