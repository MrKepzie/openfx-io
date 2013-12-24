/*
 OFX GenericReader plugin.
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
#include "GenericReader.h"

static const std::string kReaderFileParamName = "file";
static const std::string kReaderTimeOffsetParamName = "timeOffset";
static const std::string kReaderFrameRangeParamName = "frameRange";

GenericReaderPlugin::GenericReaderPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _outputClip(0)
, _fileParam(0)
, _timeOffsetParam(0)
, _frameRangeParam(0)
{
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kReaderFileParamName);
    _timeOffsetParam = fetchIntParam(kReaderTimeOffsetParamName);
    _frameRangeParam = fetchInt2DParam(kReaderFrameRangeParamName);
}

bool GenericReaderPlugin::getTimeDomain(OfxRangeD &range){
    
    std::string filename;
    _fileParam->getValue(filename);
    
    ///if the file is a video stream, let the plugin determine the frame range
    if(isVideoStream(filename)){
        return getTimeDomainForVideoStream(range);
    }
    
    ///let the host handle the time domain for the image sequence.
    return false;
    
}

bool GenericReaderPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod){
    
    ///we want to cache away the rod and the image read from file
    if(areHeaderAndDataTied()){
        
        FrameCache::iterator foundImg = _frameCache.find(args.time);
        OfxRectI imgRoI;
        
        if(foundImg != _frameCache.end()){
            
            imgRoI = foundImg->second->getRegionOfDefinition();
            
        }else{
            
            OFX::Image* img = _outputClip->fetchImage(args.time);
            std::string filename;
            _fileParam->getValueAtTime(args.time, filename);
            decode(filename, args.time, img);
            std::pair<FrameCache::iterator,bool> insertRet = _frameCache.insert(std::make_pair(args.time, img));
            assert(insertRet.second);
            imgRoI = img->getRegionOfDefinition();
        }
        
        rod.x1 = imgRoI.x1;
        rod.x2 = imgRoI.x2;
        rod.y1 = imgRoI.y1;
        rod.y2 = imgRoI.y2;

    }else{
        std::string filename;
        _fileParam->getValueAtTime(args.time, filename);
        getFrameRegionOfDefinition(filename,rod);
    }
    return true;
}


namespace OFX {
    namespace Plugin {
        
        void defineGenericReaderParamsInContext(OFX::ImageEffectDescriptor& desc,OFX::ContextEnum context) {
            OFX::StringParamDescriptor* fileParam = desc.defineStringParam(kReaderFileParamName);
            fileParam->setLabels("File", "File", "File");
            fileParam->setStringType(OFX::eStringTypeFilePath);
            fileParam->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*fileParam);
            
            OFX::IntParamDescriptor* timeOffset = desc.defineIntParam(kReaderTimeOffsetParamName);
            timeOffset->setLabels("Time offset", "Time offset", "Time offset");
            timeOffset->setAnimates(false);
            timeOffset->setDefault(0);
            
            OFX::Int2DParamDescriptor* frameRange = desc.defineInt2DParam(kReaderFrameRangeParamName);
            frameRange->setAnimates(false);
            frameRange->setLabels("Frame range", "Frame range", "Frame range");
            
        }
    }
}