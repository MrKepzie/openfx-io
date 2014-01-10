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

#if 0 //remove to use occio
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

#include "Lut.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "IOExtensions.h"
#endif


#define kReaderFileParamName "file"
#define kReaderMissingFrameParamName "onMissingFrame"
#define kReaderStartTimeParamName "startingTime"
#define kReaderInputColorSpaceParamName "inputColorSpace"
#define kReaderFirstFrameParamName "firstFrame"
#define kReaderLastFrameParamName "lastFrame"
#define kReaderBeforeParamName "before"
#define kReaderAfterParamName "after"



#ifdef OFX_EXTENSIONS_NATRON
static bool gHostIsNatron = true;
#endif

// if a hole in the sequence is larger than 2000 frames inside the sequence's time domain, this will output black frames.
#define MAX_SEARCH_RANGE 1000

GenericReaderPlugin::GenericReaderPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _missingFrameParam(0)
, _lut(0)
, _outputClip(0)
, _fileParam(0)
, _firstFrame(0)
, _beforeFirst(0)
, _lastFrame(0)
, _afterLast(0)
, _startTime(0)
#if 0 //remove to use occio
, _inputColorSpace(0)
#endif
, _dstImg(0)
{
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kReaderFileParamName);
    _missingFrameParam = fetchChoiceParam(kReaderMissingFrameParamName);
    _firstFrame = fetchIntParam(kReaderFirstFrameParamName);
    _beforeFirst = fetchChoiceParam(kReaderBeforeParamName);
    _lastFrame = fetchIntParam(kReaderLastFrameParamName);
    _afterLast = fetchChoiceParam(kReaderAfterParamName);
    _startTime = fetchIntParam(kReaderStartTimeParamName);
    
#if 0 //remove to use occio
    _inputColorSpace = fetchChoiceParam(kReaderInputColorSpaceParamName);
#endif
    
}

GenericReaderPlugin::~GenericReaderPlugin(){
    
}

bool GenericReaderPlugin::getTimeDomain(OfxRangeD &range){
    
    std::string filename;
    _fileParam->getValueAtTime(0,filename);
    
    ///call the plugin specific getTimeDomain (if it is a video-stream , it is responsible to
    ///find-out the time domain
    if(!getSequenceTimeDomain(filename,range)){
        ///the plugin wants to have the default behaviour
        
        ///They're 3 cases:
        /// 1) - the frame range is lesser than 0, e.g: [-10,-5]
        /// 2) - the frame range contains 0, e.g: [-5,5]
        /// 3) - the frame range is greater than 0, e.g: [5,10]
        
        
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
        
        
    }
    
    ///these are the value held by the "First frame" and "Last frame" param
    int frameRangeFirst;
    _firstFrame->getValue(frameRangeFirst);
    int frameRangeLast;
    _lastFrame->getValue(frameRangeLast);
    
    bool areFrameRangeValuesValid = frameRangeFirst >= range.min && frameRangeFirst <= range.max
    && frameRangeLast >= range.min && frameRangeLast <= range.max;
    
    int startingTime;
    _startTime->getValue(startingTime);
    
    range.min = startingTime; //< the first frame is always the starting time
    
    int frameRange = areFrameRangeValuesValid ? frameRangeLast - frameRangeFirst : range.max - range.min;
    range.max = startingTime + frameRange;
    
    return true;
    
}


double GenericReaderPlugin::getSequenceTime(double t)
{
    int startingTime;
    _startTime->getValue(startingTime);
    
    ///the return value
    int sequenceTime =  t;
    
    
    ///get the time domain (which will be offset to the starting time)
    OfxRangeD sequenceTimeDomain;
    getTimeDomain(sequenceTimeDomain);
    
    ///get the offset from the starting time of the sequence in case we bounce or loop
    int timeOffsetFromStart = t -  sequenceTimeDomain.min;
    
    ///if the time given is before the sequence
    if( t < sequenceTimeDomain.min) {
        /////if we're before the first frame
        int beforeChoice;
        _beforeFirst->getValue(beforeChoice);
        switch (beforeChoice) {
            case 0: //hold
                sequenceTime = sequenceTimeDomain.min;
                break;
            case 1: //loop
                    //call this function recursively with the appropriate offset in the time range
                timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                sequenceTime = sequenceTimeDomain.max - std::abs(timeOffsetFromStart);
                break;
            case 2: //bounce
                    //call this function recursively with the appropriate offset in the time range
            {
                int sequenceIntervalsCount = timeOffsetFromStart / (sequenceTimeDomain.max - sequenceTimeDomain.min);
                ///if the sequenceIntervalsCount is odd then do exactly like loop, otherwise do the load the opposite frame
                if (sequenceIntervalsCount % 2 == 0) {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.min + std::abs(timeOffsetFromStart);
                } else {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.max - std::abs(timeOffsetFromStart);
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
        
    } else if( t > sequenceTimeDomain.max) { ///the time given is after the sequence
                                             /////if we're after the last frame
        int afterChoice;
        _afterLast->getValue(afterChoice);
        
        switch (afterChoice) {
            case 0: //hold
                sequenceTime = sequenceTimeDomain.max;
                break;
            case 1: //loop
                    //call this function recursively with the appropriate offset in the time range
                timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                sequenceTime = sequenceTimeDomain.min + std::abs(timeOffsetFromStart);
                break;
            case 2: //bounce
                    //call this function recursively with the appropriate offset in the time range
            {
                int sequenceIntervalsCount = timeOffsetFromStart / (sequenceTimeDomain.max - sequenceTimeDomain.min);
                ///if the sequenceIntervalsCount is odd then do exactly like loop, otherwise do the load the opposite frame
                if (sequenceIntervalsCount % 2 == 0) {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.min + std::abs(timeOffsetFromStart);
                } else {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    sequenceTime = sequenceTimeDomain.max - std::abs(timeOffsetFromStart);
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
    
    ///get the sequence time by removing the startingTime from it
    sequenceTime -= startingTime;
    
    ///also offset properly the frame to the frame range
    ///get the "real" first frame.
    int realFirst = sequenceTimeDomain.min - startingTime;
    
    /// value held by the "First frame" param
    int frameRangeFirst;
    _firstFrame->getValue(frameRangeFirst);
    
    ///the offset is the difference of the frameRangeFirst with the realFirst
    assert(frameRangeFirst >= realFirst);
    
    sequenceTime += (frameRangeFirst - realFirst);
    return sequenceTime;
}

void GenericReaderPlugin::getFilenameAtSequenceTime(double time, std::string &filename)
{
    //////find the nearest frame, we have to do it anyway because in the case of a video stream file
    ///// there's a single file and we don't know at what time the host has set it.
    int offset = 0;
    int maxOffset = MAX_SEARCH_RANGE;
    
    int startingTime;
    _startTime->getValue(startingTime);
    
    ///get the rawTime by removing the startingTime that was removed from it
    int rawTime = time + startingTime;
    
    OfxRangeD sequenceTimeDomain;
    bool hasTimeDomain = getTimeDomain(sequenceTimeDomain);
    if (hasTimeDomain) {
        maxOffset = std::max(sequenceTimeDomain.max - rawTime, rawTime - sequenceTimeDomain.min);
    }
    
    
    while (filename.empty() && offset <= maxOffset) {
        _fileParam->getValueAtTime(time + offset, filename);
        if (!filename.empty()) {
            break;
        }
        _fileParam->getValueAtTime(time - offset, filename);
        ++offset;
    }
    
    ///if the frame is missing, do smthing according to the missing frame param
    int missingChoice;
    _missingFrameParam->getValue(missingChoice);
    switch (missingChoice) {
        case 0: // Load nearest
                ///the nearest frame search went out of range and couldn't find a frame.
            if(filename.empty()){
                setPersistentMessage(OFX::Message::eMessageError, "", "Nearest frame search went out of range");
            }
            break;
        case 1: // Error
                /// For images sequences, if the offset is not 0, that means no frame were found at the  originally given
                /// time, we can safely say this is  a missing frame. For video-streams we do not know and the derived class
                // will have to handle the case itself.
            if (offset != 0 && !isVideoStream(filename)) {
                filename.clear();
                setPersistentMessage(OFX::Message::eMessageError, "", "Missing frame");
            }
        case 2: // Black image
                /// For images sequences, if the offset is not 0, that means no frame were found at the  originally given
                /// time, we can safely say this is  a missing frame. For video-streams we do not know and the derived class
                // will have to handle the case itself.
            if (offset != 0 && !isVideoStream(filename)) {
                filename.clear();
            }
            break;
    }
    
    
    
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
    
    ///we want to cache away the rod and the image read from file
    if (!areHeaderAndDataTied(filename, sequenceTime)) {
        getFrameRegionOfDefinition(filename, sequenceTime, rod);
        
    } else {
        _dstImg = _outputClip->fetchImage(args.time);
        
        ///initialize the color-space if it wasn't
        if(!_lut){
            initializeLut();
        }
        
        decode(filename, sequenceTime, _dstImg);
        imgRoI = _dstImg->getRegionOfDefinition();
        rod.x1 = imgRoI.x1;
        rod.x2 = imgRoI.x2;
        rod.y1 = imgRoI.y1;
        rod.y2 = imgRoI.y2;
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
    
    double sequenceTime;
    try {
        sequenceTime =  getSequenceTime(args.time);
    } catch (const std::exception& e) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::string filename;
    getFilenameAtSequenceTime(sequenceTime, filename);
    if (!filename.empty()) {
        decode(filename, sequenceTime, _dstImg);
    }
    
#if 0 //remove to use occio
    try
    {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        int colorSpaceIndex;
        _inputColorSpace->getValue(colorSpaceIndex);
        const char * inputName = config->getColorSpaceNameByIndex(colorSpaceIndex);
        const char* outputName = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName();
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        OCIO::ConstProcessorRcPtr proc = config->getProcessor(context, inputName, outputName);
        
        OfxRectI rod = _dstImg->getRegionOfDefinition();
        OCIO::PackedImageDesc img((float*)_dstImg->getPixelAddress(0, 0),rod.x2 - rod.x1,rod.y2 - rod.y1,4);
        proc->apply(img);
    }
    catch(OCIO::Exception &e)
    {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
    }
#endif
    /// flush the cached image
    delete _dstImg;
    _dstImg = 0;
}

void GenericReaderPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    if(paramName == kReaderFileParamName){
        std::string filename;
        _fileParam->getValueAtTime(args.time,filename);
        
        ///we don't pass the _frameRange range as we don't want to store the time domain too
        OfxRangeD tmp;
        getTimeDomain(tmp);
        onInputFileChanged(filename);
        
        ///adjust the first frame / last frame params
        _firstFrame->setValue(tmp.min);
        _firstFrame->setRange(tmp.min, tmp.max);
        
        _lastFrame->setValue(tmp.max);
        _lastFrame->setRange(tmp.min, tmp.max);
        
        _startTime->setValue(tmp.min);
    } else if( paramName == kReaderFirstFrameParamName) {
        int first;
        int last;
        _firstFrame->getValue(first);
        _lastFrame->getValue(last);
        _startTime->setValue(first);
        _lastFrame->setRange(first, last);
    } else if( paramName == kReaderLastFrameParamName) {
        int first;
        int last;
        _firstFrame->getValue(first);
        _lastFrame->getValue(last);
        _firstFrame->setRange(first, last);
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
    
    ///////////Starting frame
    OFX::IntParamDescriptor* startingFrameParam = desc.defineIntParam(kReaderStartTimeParamName);
    startingFrameParam->setLabels("Starting time", "Starting time", "Starting time");
    startingFrameParam->setHint("At what time (on the timeline) should this sequence/video start.");
    startingFrameParam->setDefault(0);
    startingFrameParam->setAnimates(false);
    page->addChild(*startingFrameParam);
    
    
#if 0 //remove to use occio
      ///////////Input Color-space
    OFX::ChoiceParamDescriptor* inputColorSpaceParam = desc.defineChoiceParam(kReaderInputColorSpaceParamName);
    inputColorSpaceParam->setLabels("Input color-space", "Input color-space", "Input color-space");
    inputColorSpaceParam->setHint("Input data is taken to be in this color-space.");
    inputColorSpaceParam->setAnimates(false);
    page->addChild(*inputColorSpaceParam);
    
    // Query the color space names from the current config
    try
    {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        
        OCIO::ConstColorSpaceRcPtr defaultcs = config->getColorSpace(OCIO::ROLE_COMPOSITING_LOG);
        if(!defaultcs){
            throw std::runtime_error("ROLE_COMPOSITING_LOG not defined.");
        }
        std::string defaultColorSpaceName = defaultcs->getName();
        
        for(int i = 0; i < config->getNumColorSpaces(); i++)
        {
            std::string csname = config->getColorSpaceNameByIndex(i);
            inputColorSpaceParam->appendOption(csname);
            
            if(csname == defaultColorSpaceName)
            {
                inputColorSpaceParam->setDefault(i);
            }
        }
    }
    catch (OCIO::Exception& e)
    {
        std::cerr << "OCIOColorSpace: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "OCIOColorSpace: Unknown exception during OCIO setup." << std::endl;
    }
#endif
}

