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
#include <iostream>

#include "ofxsLog.h"

#ifdef OFX_EXTENSIONS_TUTTLE
#include <tuttle/ofxReadWrite.h>
#endif
#ifdef OFX_EXTENSIONS_NATRON
#include <natron/IOExtensions.h>
#endif

#define kReaderFileParamName "file"
#define kReaderMissingFrameParamName "onMissingFrame"
#define kReaderFrameModeParamName "frameMode"
#define kReaderTimeOffsetParamName "timeOffset"
#define kReaderStartingFrameParamName "startingFrame"
#define kReaderOriginalFrameRangeParamName "ReaderOriginalFrameRangeParamName"

#ifdef IO_USING_OCIO
#define kReaderOCCIOConfigFileParamName "ReaderOCCIOConfigFileParamName"
#define kReaderInputColorSpaceParamName "inputColorSpace"
#endif

#define kReaderFirstFrameParamName "firstFrame"
#define kReaderLastFrameParamName "lastFrame"
#define kReaderBeforeParamName "before"
#define kReaderAfterParamName "after"

// if a hole in the sequence is larger than 2000 frames inside the sequence's time domain, this will output black frames.
#define MAX_SEARCH_RANGE 400000

GenericReaderPlugin::GenericReaderPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _missingFrameParam(0)
, _outputClip(0)
, _fileParam(0)
, _firstFrame(0)
, _beforeFirst(0)
, _lastFrame(0)
, _afterLast(0)
, _frameMode(0)
, _timeOffset(0)
, _startingFrame(0)
, _originalFrameRange(0)
#ifdef IO_USING_OCIO
, _occioConfigFile(0)
, _inputColorSpace(0)
#endif
, _settingFrameRange(false)
{
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kReaderFileParamName);
    _missingFrameParam = fetchChoiceParam(kReaderMissingFrameParamName);
    _firstFrame = fetchIntParam(kReaderFirstFrameParamName);
    _beforeFirst = fetchChoiceParam(kReaderBeforeParamName);
    _lastFrame = fetchIntParam(kReaderLastFrameParamName);
    _afterLast = fetchChoiceParam(kReaderAfterParamName);
    _frameMode = fetchChoiceParam(kReaderFrameModeParamName);
    _timeOffset = fetchIntParam(kReaderTimeOffsetParamName);
    _startingFrame = fetchIntParam(kReaderStartingFrameParamName);
    _originalFrameRange = fetchInt2DParam(kReaderOriginalFrameRangeParamName);
    
#ifdef IO_USING_OCIO
    _occioConfigFile = fetchStringParam(kReaderOCCIOConfigFileParamName);
    _inputColorSpace = fetchChoiceParam(kReaderInputColorSpaceParamName);
#endif
    
}

GenericReaderPlugin::~GenericReaderPlugin(){
    
}



bool GenericReaderPlugin::getTimeDomain(OfxRangeD &range){
    bool ret = getSequenceTimeDomainInternal(range);
    if (ret) {
        timeDomainFromSequenceTimeDomain(range, false);
    }
    return ret;
}

bool GenericReaderPlugin::getSequenceTimeDomainInternal(OfxRangeD& range) {
    
    ////first-off check if the original frame range param has valid values, in which
    ///case we don't bother calculating the frame range
    int originalMin,originalMax;
    _originalFrameRange->getValue(originalMin, originalMax);
    if (originalMin != INT_MIN && originalMax != INT_MAX) {
        range.min = originalMin;
        range.max = originalMax;
        return true;
    }
    
    ///otherwise compute the frame-range
    
    std::string filename;
    _fileParam->getValue(filename);
    ///call the plugin specific getTimeDomain (if it is a video-stream , it is responsible to
    ///find-out the time domain. If this function return false, it means this is an image sequence
    if(getSequenceTimeDomain(filename,range)){
        _originalFrameRange->setValue(range.min, range.max);
        return true;
    }
    
    ///check whether the host implements kNatronImageSequenceRange, if so return this value
    ///This is a speed-up for host that implements this property.
    int firstFrameProp = INT_MIN;
    int lastFrameProp = INT_MAX;
#ifdef OFX_EXTENSIONS_NATRON
    _fileParam->getImageSequenceRange(firstFrameProp, lastFrameProp);
#endif
    
    if (firstFrameProp != INT_MIN && firstFrameProp != INT_MAX) {
        range.min = firstFrameProp;
        range.max = lastFrameProp;
        _originalFrameRange->setValue(range.min, range.max);
        return true;
    }
    
    ///The host doesn't support kNatronImageSequenceRange and we have an image sequence
    ///the plugin wants to have the default behaviour: compute the frame range by scanning the timeline
    
    ///They're 3 cases:
    /// 1) - the frame range is lesser than 0, e.g: [-10,-5]
    /// 2) - the frame range contains 0, e.g: [-5,5]
    /// 3) - the frame range is greater than 0, e.g: [5,10]
    _fileParam->getValueAtTime(0, filename);
    
    if(filename.empty()) {
        /// If the filename is empty for the frame 0, that means we're in cases 1 or 3
        /// If we're in case 1, we must find a frame below 0
        int firstValidFrame = 0;
        while (filename.empty() && firstValidFrame != -MAX_SEARCH_RANGE) {
            --firstValidFrame;
            _fileParam->getValueAtTime(firstValidFrame,filename);
        }
        if (filename.empty()) {
            ///the only solution left is the 3rd case
            firstValidFrame = 0;
            while (filename.empty() && firstValidFrame != MAX_SEARCH_RANGE) {
                ++firstValidFrame;
                _fileParam->getValueAtTime(firstValidFrame,filename);
            }
            if (filename.empty()) {
                ///hmmm...we're not in cases 1,2 or 3, just return false...let the host deal with it.
                return false;
            } else {
                ///we're in the 3rd case, find the right bound
                int rightBound = firstValidFrame + 1;
                _fileParam->getValueAtTime(rightBound,filename);
                while (!filename.empty() && rightBound != MAX_SEARCH_RANGE) {
                    ++rightBound;
                    _fileParam->getValueAtTime(rightBound,filename);
                }
                --rightBound;
                range.min = firstValidFrame;
                range.max = rightBound;
            }
        } else {
            /// we're in the 1st case, firstValidFrame is the right bound, we need to find the left bound now
            int leftBound = firstValidFrame - 1;
            while (!filename.empty() && leftBound != -MAX_SEARCH_RANGE) {
                --leftBound;
                _fileParam->getValueAtTime(leftBound,filename);
            }
            ++leftBound;
            range.min = leftBound;
            range.max = firstValidFrame;
        }
    } else {
        ///we're in the 2nd, find out the left bound and right bound
        int leftBound = 0;
        while (!filename.empty() && leftBound != -MAX_SEARCH_RANGE) {
            --leftBound;
            _fileParam->getValueAtTime(leftBound,filename);
        }
        ++leftBound;
        
        int rightBound = 0;
        _fileParam->getValueAtTime(0, filename);
        while (!filename.empty() && rightBound != MAX_SEARCH_RANGE) {
            ++rightBound;
            _fileParam->getValueAtTime(rightBound,filename);
        }
        
        --rightBound;
        range.min = leftBound;
        range.max = rightBound;
    }
    _originalFrameRange->setValue(range.min, range.max);
    return true;
}


void GenericReaderPlugin::timeDomainFromSequenceTimeDomain(OfxRangeD& range,bool mustSetFrameRange)
{
    ///the values held by GUI parameters
    int frameRangeFirst,frameRangeLast;
    int startingFrame;
    if (mustSetFrameRange) {
        frameRangeFirst = range.min;
        frameRangeLast = range.max;
        startingFrame = frameRangeFirst;
        _settingFrameRange = true;
        _firstFrame->setDisplayRange(range.min, range.max);
        _lastFrame->setDisplayRange(range.min, range.max);
        
        _firstFrame->setValue(range.min);
        _lastFrame->setValue(range.max);
        
        _originalFrameRange->setValue(range.min, range.max);
        
        _settingFrameRange = false;
    } else {
        ///these are the value held by the "First frame" and "Last frame" param
        _firstFrame->getValue(frameRangeFirst);
        _lastFrame->getValue(frameRangeLast);
        _startingFrame->getValue(startingFrame);
    }
    
    range.min = startingFrame;
    range.max = startingFrame + frameRangeLast - frameRangeFirst;
    
}

double GenericReaderPlugin::getSequenceTime(double t)
{
    int timeOffset;
    _timeOffset->getValue(timeOffset);
    
    ///the return value
    int sequenceTime =  t - timeOffset;
    
    ///get the time domain (which will be offset to the starting time)
    OfxRangeD sequenceTimeDomain;
    getSequenceTimeDomainInternal(sequenceTimeDomain);
    
    ///get the offset from the starting time of the sequence in case we bounce or loop
    int timeOffsetFromStart = t -  sequenceTimeDomain.min;
    
    ///if the time given is before the sequence
    if( sequenceTime < sequenceTimeDomain.min) {
        /////if we're before the first frame
        int beforeChoice;
        _beforeFirst->getValue(beforeChoice);
        switch (beforeChoice) {
            case 0: //hold
                sequenceTime = sequenceTimeDomain.min;
                break;
            case 1: //loop
                timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                sequenceTime = sequenceTimeDomain.max + timeOffsetFromStart;
                break;
            case 2: //bounce
            {
                int sequenceIntervalsCount = timeOffsetFromStart / (sequenceTimeDomain.max - sequenceTimeDomain.min);
                ///if the sequenceIntervalsCount is odd then do exactly like loop, otherwise do the load the opposite frame
                if (sequenceIntervalsCount % 2 == 0) {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.min - timeOffsetFromStart;
                } else {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.max + timeOffsetFromStart;
                }
            }
                break;
            case 3: //black
                throw std::invalid_argument("Out of frame range.");
                break;
            case 4: //error
                setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
                throw std::invalid_argument("Out of frame range.");
                break;
            default:
                break;
        }
        
    } else if( sequenceTime > sequenceTimeDomain.max) { ///the time given is after the sequence
                                             /////if we're after the last frame
        int afterChoice;
        _afterLast->getValue(afterChoice);
        
        switch (afterChoice) {
            case 0: //hold
                sequenceTime = sequenceTimeDomain.max;
                break;
            case 1: //loop
                timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                sequenceTime = sequenceTimeDomain.min + timeOffsetFromStart;
                break;
            case 2: //bounce
            {
                int sequenceIntervalsCount = timeOffsetFromStart / (sequenceTimeDomain.max - sequenceTimeDomain.min);
                ///if the sequenceIntervalsCount is odd then do exactly like loop, otherwise do the load the opposite frame
                if (sequenceIntervalsCount % 2 == 0) {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.min + timeOffsetFromStart;
                } else {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.max - timeOffsetFromStart;
                }
            }
                
                break;
            case 3: //black
                throw std::invalid_argument("Out of frame range.");
                break;
            case 4: //error
                setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
                throw std::invalid_argument("Out of frame range.");
                break;
            default:
                break;
        }
        
    }
    
    assert(sequenceTime >= sequenceTimeDomain.min && sequenceTime <= sequenceTimeDomain.max);
    return sequenceTime;
}

void GenericReaderPlugin::getFilenameAtSequenceTime(double sequenceTime, std::string &filename)
{

    
    OfxRangeD sequenceTimeDomain;
    getSequenceTimeDomainInternal(sequenceTimeDomain);
    
    _fileParam->getValueAtTime(sequenceTime,filename);
    
    ///if the frame is missing, do smthing according to the missing frame param
    if (filename.empty()) {
        int missingChoice;
        _missingFrameParam->getValue(missingChoice);
        switch (missingChoice) {
            case 0: // Load nearest
            {
                int offset = -1;
                int maxOffset = MAX_SEARCH_RANGE;
                while (filename.empty() && offset <= maxOffset) {
                    _fileParam->getValueAtTime(sequenceTime + offset, filename);
                    if (offset < 0) {
                        offset = -offset;
                    } else {
                        ++offset;
                    }
                }
                if(filename.empty()){
                    setPersistentMessage(OFX::Message::eMessageError, "", "Nearest frame search went out of range");
                    // return a black image
                }
            }
                break;
            case 1: // Error
                    /// For images sequences, we can safely say this is  a missing frame. For video-streams we do not know and the derived class
                    // will have to handle the case itself.
                setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
                break;
            case 2: // Black image
                    /// For images sequences, we can safely say this is  a missing frame. For video-streams we do not know and the derived class
                    // will have to handle the case itself.
                break;
        }

    }
    
    
    
}

bool GenericReaderPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod){
    
    double sequenceTime;
    try {
        sequenceTime =  getSequenceTime(args.time);
    } catch (const std::exception& e) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::string filename;
    
    getFilenameAtSequenceTime(sequenceTime, filename);
    
    if (filename.empty()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    getFrameRegionOfDefinition(filename, sequenceTime, rod);
    return true;
}

void GenericReaderPlugin::render(const OFX::RenderArguments &args) {
    
    
    
    OFX::Image* dstImg = _outputClip->fetchImage(args.time);
    
    
    double sequenceTime;
    try {
        sequenceTime =  getSequenceTime(args.time);
    } catch (const std::exception& e) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::string filename;
    getFilenameAtSequenceTime(sequenceTime, filename);
    if (!filename.empty()) {
        decode(filename, sequenceTime, dstImg);
    }
    
#ifdef IO_USING_OCIO
    try
    {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        int colorSpaceIndex;
        _inputColorSpace->getValue(colorSpaceIndex);
        const char * inputName = config->getColorSpaceNameByIndex(colorSpaceIndex);
        const char* outputName = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName();
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        OCIO::ConstProcessorRcPtr proc = config->getProcessor(context, inputName, outputName);
        
        OfxRectI rod = dstImg->getRegionOfDefinition();
        OCIO::PackedImageDesc img((float*)dstImg->getPixelAddress(rod.x1, rod.y1),rod.x2 - rod.x1,rod.y2 - rod.y1,3,sizeof(float),
                                  4*sizeof(float),(rod.x2 - rod.x1)*4*sizeof(float));
        proc->apply(img);
    }
    catch(OCIO::Exception &e)
    {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
    }
#endif

    delete dstImg;
}

void GenericReaderPlugin::purgeCaches() {
    OCIO::ClearAllCaches();
    clearAnyCache();
}

void GenericReaderPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    if(paramName == kReaderFileParamName){
        std::string filename;
        _fileParam->getValueAtTime(args.time,filename);
        
        //reset the original range param
        _originalFrameRange->setValue(INT_MIN, INT_MAX);
        
        ///we don't pass the _frameRange range as we don't want to store the time domain too
        OfxRangeD tmp;
        getSequenceTimeDomainInternal(tmp);
        timeDomainFromSequenceTimeDomain(tmp, true);
        _startingFrame->setValue(tmp.min);
        onInputFileChanged(filename);
        
        
    } else if( paramName == kReaderFirstFrameParamName && !_settingFrameRange) {
        int first;
        int last;
        _firstFrame->getValue(first);
        _lastFrame->getValue(last);
        _lastFrame->setDisplayRange(first, last);
        
        int offset;
        _timeOffset->getValue(offset);
        _settingFrameRange = true,
        _startingFrame->setValue(first + offset);
        _settingFrameRange = false;
    } else if( paramName == kReaderLastFrameParamName && !_settingFrameRange) {
        int first;
        int last;
        _firstFrame->getValue(first);
        _lastFrame->getValue(last);
        _firstFrame->setDisplayRange(first, last);
    } else if( paramName == kReaderFrameModeParamName ) {
        int mode;
        _frameMode->getValue(mode);
        switch (mode) {
            case 0: //starting frame
                _startingFrame->setIsSecret(false);
                _timeOffset->setIsSecret(true);
                break;
            case 1: //time offset
                _startingFrame->setIsSecret(true);
                _timeOffset->setIsSecret(false);
                break;
            default:
                //no such case
                assert(false);
                break;
        }
    } else if( paramName == kReaderStartingFrameParamName && !_settingFrameRange) {
        //also update the time offset
        int startingFrame;
        _startingFrame->getValue(startingFrame);
        OfxRangeD sequenceTimeDomain;
        getSequenceTimeDomainInternal(sequenceTimeDomain);
        
        ///prevent recursive calls of setValue(...)
        _settingFrameRange = true;
        _timeOffset->setValue(startingFrame - sequenceTimeDomain.min);
        _settingFrameRange = false;
        
    } else if( paramName == kReaderTimeOffsetParamName && !_settingFrameRange) {
        //also update the starting frame
        int offset;
        _timeOffset->getValue(offset);
        int first;
        _firstFrame->getValue(first);
        
        ///prevent recursive calls of setValue(...)
        _settingFrameRange = true;
        _startingFrame->setValue(offset + first);
        _settingFrameRange = false;

    }
#ifdef IO_USING_OCIO
    else if ( paramName == kReaderOCCIOConfigFileParamName ) {
        std::string filename;
        _occioConfigFile->getValue(filename);
        std::vector<std::string> colorSpaces;
        int defaultIndex;
        OCIO_OFX::openOCIOConfigFile(&colorSpaces, &defaultIndex,filename.c_str());
        
        _inputColorSpace->resetOptions();
        for (unsigned int i = 0; i < colorSpaces.size(); ++i) {
            _inputColorSpace->appendOption(colorSpaces[i]);
        }
        if (defaultIndex < (int)colorSpaces.size()) {
            _inputColorSpace->setValue(defaultIndex);
        }
    }
#endif
    
    
}


using namespace OFX;

#ifdef IO_USING_OCIO
void GenericReaderPluginFactory::getInputColorSpace(std::string& ocioRole) const { ocioRole = std::string(OCIO::ROLE_SCENE_LINEAR); }
#endif

void GenericReaderPluginFactory::describe(OFX::ImageEffectDescriptor &desc){
    desc.setPluginGrouping("Image/ReadOFX");
    
#ifdef OFX_EXTENSIONS_TUTTLE
    desc.addSupportedContext(OFX::eContextReader);
#endif
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
    
    
#ifdef OFX_EXTENSIONS_TUTTLE
    std::vector<std::string> fileFormats;
    supportedFileFormats(&fileFormats);
    desc.addSupportedExtensions(fileFormats);
#endif

    describeReader(desc);
}

void GenericReaderPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context){
  
    
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(true);
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    //////////Input file
    OFX::StringParamDescriptor* fileParam = desc.defineStringParam(kReaderFileParamName);
    fileParam->setLabels("File", "File", "File");
    fileParam->setStringType(OFX::eStringTypeFilePath);
    fileParam->setHint("The input image sequence/video stream file(s).");
    fileParam->setAnimates(false);
    // in the Reader context, the script name must be "filename", @see kOfxImageEffectContextReader
    fileParam->setScriptName("filename");
    desc.addClipPreferencesSlaveParam(*fileParam);
    page->addChild(*fileParam);
    
    if (!isVideoStreamPlugin()) {
#ifdef OFX_EXTENSIONS_NATRON
        fileParam->setFilePathIsImage(true);
#endif
    }
    
    //////////First-frame
    OFX::IntParamDescriptor* firstFrameParam = desc.defineIntParam(kReaderFirstFrameParamName);
    firstFrameParam->setLabels("First frame", "First frame", "First frame");
    firstFrameParam->setHint("The first frame this sequence/video should start at. This cannot be lesser "
                             " than the first frame of the sequence and cannot be greater than the last"
                             " frame of the sequence.");
    firstFrameParam->setDefault(0);
    firstFrameParam->setAnimates(false);
    page->addChild(*firstFrameParam);
    
    ///////////Before first
    OFX::ChoiceParamDescriptor* beforeFirstParam = desc.defineChoiceParam(kReaderBeforeParamName);
    beforeFirstParam->setLabels("Before", "Before", "Before");
    beforeFirstParam->setHint("What to do before the first frame of the sequence.");
    beforeFirstParam->appendOption("hold","While before the sequence, load the first frame.");
    beforeFirstParam->appendOption("loop","Repeat the sequence before the first frame");
    beforeFirstParam->appendOption("bounce","Repeat the sequence in reverse before the first frame");
    beforeFirstParam->appendOption("black","Render a black image");
    beforeFirstParam->appendOption("error","Report an error");
    beforeFirstParam->setAnimates(false);
    beforeFirstParam->setDefault(0);
    page->addChild(*beforeFirstParam);
    
    //////////Last-frame
    OFX::IntParamDescriptor* lastFrameParam = desc.defineIntParam(kReaderLastFrameParamName);
    lastFrameParam->setLabels("Last frame", "Last frame", "Last frame");
    lastFrameParam->setHint("The frame this sequence/video should end at. This cannot be lesser "
                            " than the first frame of the sequence and cannot be greater than the last"
                            " frame of the sequence.");
    lastFrameParam->setDefault(0);
    lastFrameParam->setAnimates(false);
    page->addChild(*lastFrameParam);
    
    ///////////After first
    OFX::ChoiceParamDescriptor* afterLastParam = desc.defineChoiceParam(kReaderAfterParamName);
    afterLastParam->setLabels("After", "After", "After");
    afterLastParam->setHint("What to do after the last frame of the sequence.");
    afterLastParam->appendOption("hold","While after the sequence, load the last frame.");
    afterLastParam->appendOption("loop","Repeat the sequence after the last frame");
    afterLastParam->appendOption("bounce","Repeat the sequence in reverse after the last frame");
    afterLastParam->appendOption("black","Render a black image");
    afterLastParam->appendOption("error","Report an error");
    afterLastParam->setAnimates(false);
    afterLastParam->setDefault(0);
    page->addChild(*afterLastParam);
    
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
    
    
    ///////////Frame-mode
    OFX::ChoiceParamDescriptor* frameModeParam = desc.defineChoiceParam(kReaderFrameModeParamName);
    frameModeParam->appendOption("Starting frame");
    frameModeParam->appendOption("Time offset");
    frameModeParam->setAnimates(false);
    frameModeParam->setDefault(0);
    
    
    ///////////Starting frame
    OFX::IntParamDescriptor* startingFrameParam = desc.defineIntParam(kReaderStartingFrameParamName);
    startingFrameParam->setLabels("Starting time", "Starting time", "Starting time");
    startingFrameParam->setHint("At what time (on the timeline) should this sequence/video start.");
    startingFrameParam->setDefault(0);
    startingFrameParam->setAnimates(false);
    page->addChild(*startingFrameParam);
    
    ///////////Time offset
    OFX::IntParamDescriptor* timeOffsetParam = desc.defineIntParam(kReaderTimeOffsetParamName);
    timeOffsetParam->setLabels("Time offset", "Time offset", "Time offset");
    timeOffsetParam->setHint("Offset applied to the sequence in frames.");
    timeOffsetParam->setDefault(0);
    timeOffsetParam->setAnimates(false);
    timeOffsetParam->setIsSecret(true);
    page->addChild(*timeOffsetParam);
    
    ///////////Original frame range
    OFX::Int2DParamDescriptor* originalFrameRangeParam = desc.defineInt2DParam(kReaderOriginalFrameRangeParamName);
    originalFrameRangeParam->setLabels("Original range", "Original range", "Original range");
    originalFrameRangeParam->setDefault(INT_MIN, INT_MAX);
    originalFrameRangeParam->setAnimates(false);
    originalFrameRangeParam->setIsSecret(true);
    originalFrameRangeParam->setIsPersistant(false);
    page->addChild(*originalFrameRangeParam);
    
    
#ifdef IO_USING_OCIO
    ////////// OCIO config file
    OFX::StringParamDescriptor* occioConfigFileParam = desc.defineStringParam(kReaderOCCIOConfigFileParamName);
    occioConfigFileParam->setLabels("OCIO config file", "OCIO config file", "OCIO config file");
    occioConfigFileParam->setStringType(OFX::eStringTypeFilePath);
    occioConfigFileParam->setHint("The file to read the OpenColorIO config from.");
    occioConfigFileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*occioConfigFileParam);
    
    ///////////Input Color-space
    OFX::ChoiceParamDescriptor* inputColorSpaceParam = desc.defineChoiceParam(kReaderInputColorSpaceParamName);
    inputColorSpaceParam->setLabels("Input color-space", "Input color-space", "Input color-space");
    inputColorSpaceParam->setHint("Input data is taken to be in this color-space.");
    inputColorSpaceParam->setAnimates(false);
    page->addChild(*inputColorSpaceParam);
    
    ///read the default config pointed to by the env var OCIO
    std::vector<std::string> colorSpaces;
    int defaultIndex;
    std::string defaultOcioRole;
    getInputColorSpace(defaultOcioRole);
    OCIO_OFX::openOCIOConfigFile(&colorSpaces, &defaultIndex,NULL,defaultOcioRole);
    
    for (unsigned int i = 0; i < colorSpaces.size(); ++i) {
        inputColorSpaceParam->appendOption(colorSpaces[i]);
    }
    if (defaultIndex < (int)colorSpaces.size()) {
        inputColorSpaceParam->setDefault(defaultIndex);
    }

#endif
    
    describeReaderInContext(desc, context, page);
}

