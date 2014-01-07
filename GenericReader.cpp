/*
 OFX GenericReader plugin.
 A base class for all OpenFX-based decoders.
 
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

#include <sstream>

#include "ofxsLog.h"

#include "Lut.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "IOExtensions.h"
#endif


#define kReaderFileParamName "file"
#define kReaderMissingFrameParamName "onMissingFrame"
#define kReaderTimeOffsetParamName "timeOffset"

#ifdef OFX_EXTENSIONS_NATRON
static bool gHostIsNatron = true;
#endif

static int nearestFrameSearchLimit = 50000;


GenericReaderPlugin::GenericReaderPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _outputClip(0)
, _fileParam(0)
, _missingFrameParam(0)
, _timeOffset(0)
, _lut(0)
, _dstImg(0)
, _frameRangeValid(false)
, _frameRange()
{
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kReaderFileParamName);
    _missingFrameParam = fetchChoiceParam(kReaderMissingFrameParamName);
    _timeOffset = fetchIntParam(kReaderTimeOffsetParamName);
    
    
}

GenericReaderPlugin::~GenericReaderPlugin(){
    
}

bool GenericReaderPlugin::getTimeDomain(OfxRangeD &range){
    
    if(!_frameRangeValid) {
        std::string filename;
        _fileParam->getValueAtTime(0,filename);
        
        ///call the plugin specific getTimeDomain (if it is a video-stream , it is responsible to
        ///find-out the time domain
        if(!getTimeDomain(filename,range)){
            ///the plugin wants to have the default behaviour
            
            ///They're 3 cases:
            /// 1) - the frame range is lesser than 0, e.g: [-10,-5]
            /// 2) - the frame range contains 0, e.g: [-5,5]
            /// 3) - the frame range is greater than 0, e.g: [5,10]
            
            
            if(filename.empty()) {
                /// If the filename is empty for the frame 0, that means we're in cases 1 or 3
                /// If we're in case 1, we must find a frame below 0
                int firstValidFrame = 0;
                while (filename.empty() && firstValidFrame != -nearestFrameSearchLimit) {
                    --firstValidFrame;
                    _fileParam->getValueAtTime(firstValidFrame,filename);
                }
                if (filename.empty()) {
                    ///the only solution left is the 3rd case
                    firstValidFrame = 0;
                    while (filename.empty() && firstValidFrame != nearestFrameSearchLimit) {
                        ++firstValidFrame;
                        _fileParam->getValueAtTime(firstValidFrame,filename);
                    }
                    if (filename.empty()) {
                        ///hmmm...we're not in cases 1,2 or 3, just return false...let the host deal with it.
                        return false;
                    } else {
                        ///we're in the 3rd case, find the right bound
                        int rightBound = firstValidFrame;
                        while (!filename.empty() && rightBound != nearestFrameSearchLimit) {
                            ++rightBound;
                            _fileParam->getValueAtTime(rightBound,filename);
                        }
                        range.min = firstValidFrame;
                        range.max = rightBound;
                    }
                } else {
                    /// we're in the 1st case, firstValidFrame is the right bound, we need to find the left bound now
                    int leftBound = firstValidFrame;
                    while (!filename.empty() && leftBound != -nearestFrameSearchLimit) {
                        --leftBound;
                        _fileParam->getValueAtTime(leftBound,filename);
                    }
                    range.min = leftBound;
                    range.max = firstValidFrame;
                }
            } else {
                ///we're in the 2nd, find out the left bound and right bound
                int leftBound = 0;
                while (!filename.empty() && leftBound != -nearestFrameSearchLimit) {
                    --leftBound;
                    _fileParam->getValueAtTime(leftBound,filename);
                }
                
                int rightBound = 0;
                while (!filename.empty() && rightBound != nearestFrameSearchLimit) {
                    ++rightBound;
                    _fileParam->getValueAtTime(rightBound,filename);
                }
                
                range.min = leftBound;
                range.max = rightBound;
            }
            
            
        }
        _frameRangeValid = true;
    }else {
        range = _frameRange;
    }
    int timeOffset;
    _timeOffset->getValue(timeOffset);
    
    range.min += timeOffset;
    range.max += timeOffset;
    return true;
    
}

bool GenericReaderPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod){
    
    OfxRectI imgRoI;
    if(_dstImg){
        imgRoI = _dstImg->getRegionOfDefinition();
        rod.x1 = imgRoI.x1;
        rod.x2 = imgRoI.x2;
        rod.y1 = imgRoI.y1;
        rod.y2 = imgRoI.y2;
        return true;
    }
    
    int timeOffset;
    _timeOffset->getValue(timeOffset);
    
    int choice;
    _missingFrameParam->getValue(choice);
    
    std::string filename;
    int nearestIndex = 0;
    _fileParam->getValueAtTime(args.time - timeOffset, filename);
    
    ///we want to load the nearest frame
    while (filename.empty() && nearestIndex < nearestFrameSearchLimit) {
        ++nearestIndex;
        _fileParam->getValueAtTime(args.time - timeOffset + nearestIndex, filename);
        if (!filename.empty()) {
            break;
        }
        _fileParam->getValueAtTime(args.time - timeOffset - nearestIndex, filename);
    }
    if(filename.empty()){
        setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
        return true;
    } else {
        if (nearestIndex != 0) {
            bool video = isVideoStream(filename);
            if(choice == 1 && !video) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
                return true;
            }else if(choice == 2 && !video) {
                return true;
            }
        }
    }
    
    ///we want to cache away the rod and the image read from file
    if(areHeaderAndDataTied(filename,args.time - timeOffset)){
        
        _dstImg = _outputClip->fetchImage(args.time);
        
        ///initialize the color-space if it wasn't
        if(!_lut){
            initializeLut();
        }
        
        decode(filename, args.time - timeOffset, _dstImg);
        imgRoI = _dstImg->getRegionOfDefinition();
        rod.x1 = imgRoI.x1;
        rod.x2 = imgRoI.x2;
        rod.y1 = imgRoI.y1;
        rod.y2 = imgRoI.y2;
        
    }else{
        
        getFrameRegionOfDefinition(filename,args.time - timeOffset,rod);
        
    }
    return true;
}

void GenericReaderPlugin::render(const OFX::RenderArguments &args) {
    
    if (_dstImg) {
        return;
    }
    
    _dstImg = _outputClip->fetchImage(args.time);
    
    ///initialize the color-space if it wasn't
    if(!_lut){
        initializeLut();
    }
    
    int timeOffset;
    _timeOffset->getValue(timeOffset);
    
    int choice;
    _missingFrameParam->getValue(choice);
    
    std::string filename;
    _fileParam->getValueAtTime(args.time - timeOffset, filename);
    
    ///we want to load the nearest frame
    int nearestIndex = 0;
    while (filename.empty() && nearestIndex < nearestFrameSearchLimit) {
        ++nearestIndex;
        _fileParam->getValueAtTime(args.time - timeOffset + nearestIndex, filename);
        if (!filename.empty()) {
            break;
        }
        _fileParam->getValueAtTime(args.time - timeOffset - nearestIndex, filename);
    }
    if(filename.empty()){
        setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
        return;
    } else {
        if (nearestIndex != 0) {
            bool video = isVideoStream(filename);
            if(choice == 1 && !video) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
                return;
            }else if(choice == 2 && !video) {
                return;
            }
        }
    }
    
    
    decode(filename, args.time - timeOffset, _dstImg);
    
    /// flush the cached image
    delete _dstImg;
    _dstImg = 0;
}

void GenericReaderPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    if(paramName == kReaderFileParamName){
        std::string filename;
        _fileParam->getValueAtTime(args.time,filename);
        _frameRangeValid = false;
        
        ///we don't pass the _frameRange range as we don't want to store the time domain too
        OfxRangeD tmp;
        getTimeDomain(tmp);
        onInputFileChanged(filename);
    }
}

using namespace OFX;

void GenericReaderPluginFactory::describe(OFX::ImageEffectDescriptor &desc){
    desc.setPluginGrouping("Image/ReadOFX");
    
    desc.addSupportedContext(OFX::eContextGenerator);
    desc.addSupportedContext(OFX::eContextGeneral);
    
    ///Say we support only reading to float images.
    ///One would need to extend the ofxsColorSpace suite functions
    ///in order to support other bitdepths. I have no time for it
    ///at the moment and float is generally widely used among hosts.
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    
    
#ifdef OFX_EXTENSIONS_NATRON
    // to check if the host is Natron-compatible, we could rely on gHostDescription.hostName,
    // but we prefer checking if the host has the right properties, in care another host implements
    // these extensions
    try {
        std::vector<std::string> fileFormats;
        supportedFileFormats(&fileFormats);
        for (unsigned int i = 0; i < fileFormats.size(); ++i) {
            desc.getPropertySet().propSetString(kNatronImageEffectPropFormats, fileFormats[i], i,true);
        }
        desc.getPropertySet().propSetInt(kNatronImageEffectPropFormatsCount, (int)fileFormats.size(), 0);
    } catch (const OFX::Exception::PropertyUnknownToHost &e) {
        // the host is does not implement Natron extensions
        gHostIsNatron = false;
    }
    OFX::Log::warning(!gHostIsNatron, "ReadOFX: Host does not implement Natron extensions.");
#endif
}

void GenericReaderPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context){
    //////////Input file
    OFX::StringParamDescriptor* fileParam = desc.defineStringParam(kReaderFileParamName);
    fileParam->setLabels("File", "File", "File");
    fileParam->setStringType(OFX::eStringTypeFilePath);
    fileParam->setHint("The input image sequence/video stream file(s).");
    fileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*fileParam);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(true);
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
#ifdef OFX_EXTENSIONS_NATRON
    if (gHostIsNatron) {
        fileParam->setFilePathIsImage(true);
    }
#endif
    
    ///////////Missing frame choice
    OFX::ChoiceParamDescriptor* missingFrameParam = desc.defineChoiceParam(kReaderMissingFrameParamName);
    missingFrameParam->setLabels("On Missing Frame", "On Missing Frame", "On Missing Frame");
    missingFrameParam->setHint("What to do when a frame is missing from the sequence/stream.");
    missingFrameParam->appendOption("Load nearest","Tries to load the nearest frame in the sequence/stream if any.");
    missingFrameParam->appendOption("Error","An error is reported.");
    missingFrameParam->appendOption("Black image","A black image is rendered.");
    missingFrameParam->setAnimates(false);
    missingFrameParam->setDefault(0); //< default to nearest frame.
    page->addChild(*missingFrameParam);
    
    ///////////Time offset
    OFX::IntParamDescriptor* timeOffsetParam = desc.defineIntParam(kReaderTimeOffsetParamName);
    timeOffsetParam->setLabels("Time Offset", "Time Offset", "Time Offset");
    timeOffsetParam->setHint("Offset in frames (frame f of the input will be at f + offset).");
    timeOffsetParam->setDefault(0);
    timeOffsetParam->setAnimates(false);
    page->addChild(*timeOffsetParam);
}

