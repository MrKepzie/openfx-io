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
 
 */
#ifndef Io_ffmpegReader_h
#define Io_ffmpegReader_h

#include "GenericReader.h"

namespace FFmpeg {
    class File;
}

class FfmpegReaderPlugin : public GenericReaderPlugin {
    
    FFmpeg::File* _ffmpegFile; //< a ptr to the ffmpeg file, don't delete it the FfmpegFileManager handles their allocation/deallocation
    
    unsigned char* _buffer;
    int _bufferWidth;
    int _bufferHeight;
    
public:
    
    FfmpegReaderPlugin(OfxImageEffectHandle handle);
    
    virtual ~FfmpegReaderPlugin();
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
    bool loadNearestFrame() const;
    
private:
    
    FFmpeg::File* getFile(const std::string& filename) const;
    
    virtual bool isVideoStream(const std::string& filename);
    
    virtual void onInputFileChanged(const std::string& filename);
    
    virtual void supportedFileFormats(std::vector<std::string>* formats) const;
    
    virtual void decode(const std::string& filename,OfxTime time,OFX::Image* dstImg);
    
    virtual void initializeLut();
    
    virtual bool getTimeDomain(const std::string& filename,OfxRangeD &range);
    
    virtual bool areHeaderAndDataTied(const std::string& filename,OfxTime time) const;
    
    virtual void getFrameRegionOfDefinition(const std::string& /*filename*/,OfxTime time,OfxRectD& rod);
};


#endif