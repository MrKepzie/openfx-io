/*
 OFX ffmpegWriter plugin.
 Writes a video output file using the libav library.
 
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

#ifndef Io_ffmpegWriter_h
#define Io_ffmpegWriter_h

#include "GenericWriter.h"

struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
class FfmpegWriterPlugin : public GenericWriterPlugin {
    
public:
    
    FfmpegWriterPlugin(OfxImageEffectHandle handle);
    
    virtual ~FfmpegWriterPlugin();
    
    virtual void supportedFileFormats(std::vector<std::string>* formats) const;
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
private:
    
        
    virtual void encode(const std::string& filename,OfxTime time,const OFX::Image* srcImg);
    
    virtual bool isImageFile(const std::string& fileExtension) const;
    
    void freeFormat();
    
    AVCodecContext*   _codecContext;
    AVFormatContext*  _formatContext;
    AVStream* _stream;
    
    OFX::ChoiceParam* _format;
    OFX::DoubleParam* _fps;
    
    OFX::ChoiceParam* _codec;
    OFX::IntParam* _bitRate;
    OFX::IntParam* _bitRateTolerance;
    OFX::IntParam* _gopSize;
    OFX::IntParam* _bFrames;
    OFX::ChoiceParam* _macroBlockDecision;
    
};


#endif
