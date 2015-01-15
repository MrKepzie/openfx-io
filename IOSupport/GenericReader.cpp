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

#include <memory>
#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#ifdef DEBUG
#include <cstdio>
#define DBG(x) (void)0//x
#else
#define DBG(x) (void)0
#endif

#include "ofxsLog.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"

#ifdef OFX_EXTENSIONS_TUTTLE
#include <tuttle/ofxReadWrite.h>
#endif
#include <ofxNatron.h>

#include "SequenceParsing/SequenceParsing.h"
#include "GenericOCIO.h"
#include "IOUtility.h"

#define kPluginGrouping "Image/Readers"

// in the Reader context, the script name must be kOfxImageEffectFileParamName, @see kOfxImageEffectContextReader
#define kParamFilename kOfxImageEffectFileParamName
#define kParamFilenameLabel "File"
#define kParamFilenameHint "The input image sequence/video stream file(s)."

#define kParamProxy kOfxImageEffectProxyParamName
#define kParamProxyLabel "Proxy File"
#define kParamProxyHint \
"Filename of the proxy images. They will be used instead of the images read from the File parameter " \
"when the proxy mode (downscaling of the images) is activated."

#define kParamProxyThreshold "proxyThreshold"
#define kParamProxyThresholdLabel "Proxy threshold"
#define kParamProxyThresholdHint \
"The scale of the proxy images. By default it will be automatically computed out of the " \
"images headers when you set the proxy file(s) path. When the render scale (proxy) is set to " \
"a scale lower or equal to this value then the proxy image files will be used instead of the " \
"original images. You can change this parameter by checking the \"Custom scale\" checkbox " \
"so that you can change the scale at which the proxy images should be used instead of the original images."

#define kParamOriginalProxyScale "originalProxyScale"
#define kParamOriginalProxyScaleLabel "Original Proxy Scale"
#define kParamOriginalProxyScaleHint \
"The original scale of the proxy image."

#define kParamCustomProxyScale "customProxyScale"
#define kParamCustomProxyScaleLabel "Custom Proxy Scale"
#define kParamCustomProxyScaleHint \
"Check to enable the Proxy scale edition."

#define kParamOnMissingFrame "onMissingFrame"
#define kParamOnMissingFrameLabel "On Missing Frame"
#define kParamOnMissingFrameHint \
"What to do when a frame is missing from the sequence/stream."

#define kParamFrameMode "frameMode"
#define kParamFrameModeLabel "Frame Mode"
enum FrameModeEnum
{
    eFrameModeStartingTime,
    eFrameModeTimeOffset,
};
#define kParamFrameModeOptionStartingTime "Starting Time"
#define kParamFrameModeOptionTimeOffset "Time Offset"

#define kParamTimeOffset "timeOffset"
#define kParamTimeOffsetLabel "Time Offset"
#define kParamTimeOffsetHint \
"Offset applied to the sequence in time units (i.e. frames)."

#define kParamStartingTime "startingTime"
#define kParamStartingTimeLabel "Starting Time"
#define kParamStartingTimeHint \
"At what time (on the timeline) should this sequence/video start."

#define kParamOriginalFrameRange "originalFrameRange"
#define kParamOriginalFrameRangeLabel "Original Range"

#define kParamFirstFrame "firstFrame"
#define kParamFirstFrameLabel "First Frame"
#define kParamFirstFrameHint \
"The first frame this sequence/video should start at. This cannot be less " \
" than the first frame of the sequence and cannot be greater than the last" \
" frame of the sequence."

#define kParamLastFrame "lastFrame"
#define kParamLastFrameLabel "Last Frame"
#define kParamLastFrameHint \
"The frame this sequence/video should end at. This cannot be lesser " \
"than the first frame of the sequence and cannot be greater than the last " \
"frame of the sequence."

#define kParamBefore "before"
#define kParamBeforeLabel "Before"
#define kParamBeforeHint \
"What to do before the first frame of the sequence."

#define kParamAfter "after"
#define kParamAfterLabel "After"
#define kParamAfterHint \
"What to do after the last frame of the sequence."

#define kParamTimeDomainUserEdited "timeDomainUserEdited"

enum BeforeAfterEnum
{
    eBeforeAfterHold,
    eBeforeAfterLoop,
    eBeforeAfterBounce,
    eBeforeAfterBlack,
    eBeforeAfterError,
};

enum MissingEnum
{
    eMissingNearest,
    eMissingError,
    eMissingBlack,
};

#define kReaderOptionHold "Hold"
#define kReaderOptionHoldHint "While before the sequence, load the first frame."
#define kReaderOptionLoop "Loop"
#define kReaderOptionLoopHint "Repeat the sequence before the first frame"
#define kReaderOptionBounce "Bounce"
#define kReaderOptionBounceHint "Repeat the sequence in reverse before the first frame"
#define kReaderOptionBlack "Black"
#define kReaderOptionBlackHint "Render a black image"
#define kReaderOptionError "Error"
#define kReaderOptionErrorHint "Report an error"
#define kReaderOptionNearest "Load nearest"
#define kReaderOptionNearestHint "Try to load the nearest frame in the sequence/stream, if any."

#define kParamFilePremult "filePremult"
#define kParamFilePremultLabel "File Premult"
#define kParamFilePremultHint \
"The image file being read is considered to have this premultiplication state.\n"\
"On output, RGB images are always Opaque, Alpha and RGBA images are always Premultiplied (also called \"associated alpha\").\n"\
"To get UnPremultiplied (or \"unassociated alpha\") images, use the \"Unpremult\" plugin after this plugin.\n\n"\
"- Opaque means that the alpha channel is considered to be 1 (one), and it is not taken into account in colorspace conversion.\n" \
"- Premultiplied, red, green and blue channels are divided by the alpha channel "\
"before applying the colorspace conversion, and re-multiplied by alpha after colorspace conversion.\n"\
"- UnPremultiplied, means that red, green and blue channels are not modified "\
"before applying the colorspace conversion, and are multiplied by alpha after colorspace conversion.\n"\
"This is set automatically from the image file and the plugin, but can be adjusted if this information is wrong in the file metadata.\n"\
"RGB images can only be Opaque, and Alpha images can only be Premultiplied (the value of this parameter doesn't matter).\n"
#define kParamFilePremultOptionOpaqueHint \
"The image is opaque and so has no premultiplication state, as if the alpha component in all pixels were set to the white point."
#define kParamFilePremultOptionPreMultipliedHint \
"The image is premultiplied by its alpha (also called \"associated alpha\")."
#define kParamFilePremultOptionUnPreMultipliedHint \
"The image is unpremultiplied (also called \"unassociated alpha\")."

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output Components"
#define kParamOutputComponentsHint "Components present in the output. The default value is set from the first frame in the sequence."
#define kParamOutputComponentsOptionRGBA "RGBA"
#define kParamOutputComponentsOptionRGB "RGB"
#define kParamOutputComponentsOptionAlpha "Alpha"

#define kParamInputSpaceLabel "File Colorspace"

#define kParamFrameRate "frameRate"
#define kParamFrameRateLabel "Frame rate"
#define kParamFrameRateHint "By default this value is guessed from the file. You can override it by checking the Custom fps parameter. " \
"The value of this parameter is what will be visible by the effects down-stream."

#define kParamCustomFps "customFps"
#define kParamCustomFpsLabel "Custom FPS"
#define kParamCustomFpsHint "If checked, you can freely force the value of the frame rate parameter."

#define MISSING_FRAME_NEAREST_RANGE 100

#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1 // GenericReader supports render scale: it scales images and uses proxy image when applicable

#define GENERIC_READER_USE_MULTI_THREAD

static bool gHostIsNatron   = false;
static bool gHostSupportsRGBA   = false;
static bool gHostSupportsRGB    = false;
static bool gHostSupportsAlpha  = false;
static OFX::PixelComponentEnum gOutputComponentsMap[4];



static const char*
premultString(OFX::PreMultiplicationEnum e)
{
    switch (e) {
        case OFX::eImageOpaque:
            return "Opaque";
        case OFX::eImagePreMultiplied:
            return "PreMultiplied";
        case OFX::eImageUnPreMultiplied:
            return "UnPreMultiplied";
    }
    return "Unknown";
}

GenericReaderPlugin::GenericReaderPlugin(OfxImageEffectHandle handle,
                                         bool supportsRGBA,
                                         bool supportsRGB,
                                         bool supportsAlpha,
                                         bool supportsTiles)
: OFX::ImageEffect(handle)
, _missingFrameParam(0)
, _outputClip(0)
, _fileParam(0)
, _proxyFileParam(0)
, _proxyThreshold(0)
, _originalProxyScale(0)
, _enableCustomScale(0)
, _firstFrame(0)
, _beforeFirst(0)
, _lastFrame(0)
, _afterLast(0)
, _frameMode(0)
, _timeOffset(0)
, _startingTime(0)
, _originalFrameRange(0)
, _outputComponents(0)
, _premult(0)
, _timeDomainUserSet(0)
, _customFPS(0)
, _fps(0)
, _ocio(new GenericOCIO(this))
, _sequenceFromFiles()
, _supportsTiles(supportsTiles)
{
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kParamFilename);
    _proxyFileParam = fetchStringParam(kParamProxy);
    _proxyThreshold = fetchDouble2DParam(kParamProxyThreshold);
    _originalProxyScale = fetchDouble2DParam(kParamOriginalProxyScale);
    _enableCustomScale = fetchBooleanParam(kParamCustomProxyScale);
    _missingFrameParam = fetchChoiceParam(kParamOnMissingFrame);
    _firstFrame = fetchIntParam(kParamFirstFrame);
    _beforeFirst = fetchChoiceParam(kParamBefore);
    _lastFrame = fetchIntParam(kParamLastFrame);
    _afterLast = fetchChoiceParam(kParamAfter);
    _frameMode = fetchChoiceParam(kParamFrameMode);
    _timeOffset = fetchIntParam(kParamTimeOffset);
    _startingTime = fetchIntParam(kParamStartingTime);
    _originalFrameRange = fetchInt2DParam(kParamOriginalFrameRange);
    _timeDomainUserSet = fetchBooleanParam(kParamTimeDomainUserEdited);
    _outputComponents = fetchChoiceParam(kParamOutputComponents);
    _premult = fetchChoiceParam(kParamFilePremult);
    _customFPS = fetchBooleanParam(kParamCustomFps);
    _fps = fetchDoubleParam(kParamFrameRate);

    _proxyThreshold->setIsSecret(true);
    _enableCustomScale->setIsSecret(true);
    _timeOffset->setIsSecret(true);
}

GenericReaderPlugin::~GenericReaderPlugin()
{
}



void
GenericReaderPlugin::restoreStateFromParameters()
{
    std::string filename;
    
    if (!gHostIsNatron) {
        
        _fileParam->getValue(filename);
        
        if (!filename.empty()) {
            setSequenceFromFile(filename);
        }
        
        //reset the original range param only if the host is not Natron
        _originalFrameRange->setValue(kOfxFlagInfiniteMin, kOfxFlagInfiniteMax);
    }
    
    OfxRangeD tmp;
    if (getSequenceTimeDomainInternal(tmp,true)) {
        
        bool timeDomainUserEdited;
        _timeDomainUserSet->getValue(timeDomainUserEdited);
        
        timeDomainFromSequenceTimeDomain(tmp, true, !timeDomainUserEdited);
    }
    ///We call restoreState with the first frame of the sequence so we're almost sure it will work
    ///unless the user did a mistake. We are also safe to assume that images specs are the same for
    ///all the sequence
    _fileParam->getValueAtTime(tmp.min, filename);
    
    restoreState(filename);
    
    bool customFps;
    _customFPS->getValue(customFps);
    if (!customFps) {
        double fps;
        bool gotFps = getFrameRate(filename, &fps);
        if (gotFps) {
            _fps->setValue(fps);
        }
    }
}

bool
GenericReaderPlugin::getTimeDomain(OfxRangeD &range)
{
    bool ret = getSequenceTimeDomainInternal(range,false);
    if (ret) {
        timeDomainFromSequenceTimeDomain(range, false);
    }
    return ret;
}

bool
GenericReaderPlugin::getSequenceTimeDomainInternal(OfxRangeD& range,bool canSetOriginalFrameRange)
{
    
    ///compute the frame-range
    
    std::string filename;
    _fileParam->getValue(filename);
    ///call the plugin specific getTimeDomain (if it is a video-stream , it is responsible to
    ///find-out the time domain. If this function return false, it means this is an image sequence
    ///in which case our sequence parser will give us the sequence range
    if (!getSequenceTimeDomain(filename, range)) {
        ////first-off check if the original frame range param has valid values, in which
        ///case we don't bother calculating the frame range
        int originalMin,originalMax;
        _originalFrameRange->getValue(originalMin, originalMax);
        if (originalMin != kOfxFlagInfiniteMin && originalMax != kOfxFlagInfiniteMax) {
            range.min = originalMin;
            range.max = originalMax;
            return true;
        }

        if (_sequenceFromFiles.size() == 1) {
            range.min = range.max = 1;
        } else if (_sequenceFromFiles.size() > 1) {
            range.min = _sequenceFromFiles.begin()->first;
            range.max = _sequenceFromFiles.rbegin()->first;
        } else {
            range.min = range.max = 1.;
        }
    }
    

    //// From http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#SettingParams
//    Plugins are free to set parameters in limited set of circumstances, typically relating to user interaction. You can only set parameters in the following actions passed to the plug-in's main entry function...
//    
//    The Create Instance Action
//    The The Begin Instance Changed Action
//    The The Instance Changed Action
//    The The End Instance Changed Action
//    The The Sync Private Data Action
    if (!filename.empty() && canSetOriginalFrameRange) {
        _originalFrameRange->setValue(range.min, range.max);
    }
    return true;
}


void
GenericReaderPlugin::timeDomainFromSequenceTimeDomain(OfxRangeD& range,bool mustSetFrameRange, bool setFirstLastFrame)
{
    ///the values held by GUI parameters
    int frameRangeFirst,frameRangeLast;
    int startingTime;
    if (mustSetFrameRange) {
        frameRangeFirst = range.min;
        frameRangeLast = range.max;
        startingTime = frameRangeFirst;
        
        _firstFrame->setRange(range.min, range.max);
        _lastFrame->setRange(range.min, range.max);
        
        if (setFirstLastFrame) {
            _firstFrame->setValue(range.min);
            _lastFrame->setValue(range.max);
        }
        
        _originalFrameRange->setValue(range.min, range.max);
    } else {
        ///these are the value held by the "First frame" and "Last frame" param
        _firstFrame->getValue(frameRangeFirst);
        _lastFrame->getValue(frameRangeLast);
        _startingTime->getValue(startingTime);
    }
    
    range.min = startingTime;
    range.max = startingTime + frameRangeLast - frameRangeFirst;
}


static bool
fileExists(const std::string& filename)
{
    std::ifstream f(filename.c_str());
    bool ret = f.good();
    f.close();
    return ret;
}

GenericReaderPlugin::GetSequenceTimeRetEnum
GenericReaderPlugin::getSequenceTime(double t, bool canSetOriginalFrameRange, double *sequenceTime)
{
    int timeOffset;
    _timeOffset->getValue(timeOffset);
    
    
    ///get the time sequence domain
    OfxRangeI sequenceTimeDomain;
    _firstFrame->getValue(sequenceTimeDomain.min);
    _lastFrame->getValue(sequenceTimeDomain.max);

    
    ///the return value
    *sequenceTime = t - timeOffset ;

    
    ///get the offset from the starting time of the sequence in case we bounce or loop
    int timeOffsetFromStart = t -  sequenceTimeDomain.min;
    
    ///if the time given is before the sequence
    if (sequenceTimeDomain.min <= *sequenceTime && *sequenceTime <= sequenceTimeDomain.max) {
        return eGetSequenceTimeWithinSequence;
    } else if (*sequenceTime < sequenceTimeDomain.min) {
        /////if we're before the first frame
        int beforeChoice_i;
        _beforeFirst->getValue(beforeChoice_i);
        BeforeAfterEnum beforeChoice = BeforeAfterEnum(beforeChoice_i);
        switch (beforeChoice) {
            case eBeforeAfterHold: //hold
                *sequenceTime = sequenceTimeDomain.min;
                return eGetSequenceTimeBeforeSequence;

            case eBeforeAfterLoop: //loop
                timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                *sequenceTime = sequenceTimeDomain.max + timeOffsetFromStart;
                return eGetSequenceTimeBeforeSequence;

            case eBeforeAfterBounce: { //bounce
                int sequenceIntervalsCount = timeOffsetFromStart / (sequenceTimeDomain.max - sequenceTimeDomain.min);
                ///if the sequenceIntervalsCount is odd then do exactly like loop, otherwise do the load the opposite frame
                if (sequenceIntervalsCount % 2 == 0) {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    *sequenceTime = sequenceTimeDomain.min - timeOffsetFromStart;
                } else {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    *sequenceTime = sequenceTimeDomain.max + timeOffsetFromStart;
                }
                return eGetSequenceTimeBeforeSequence;
            }
            case eBeforeAfterBlack: //black
                return eGetSequenceTimeBlack;

            case eBeforeAfterError: //error
                setPersistentMessage(OFX::Message::eMessageError, "", "Out of frame range");
                return eGetSequenceTimeError;
        }

    } else {
        assert(*sequenceTime > sequenceTimeDomain.max); ///the time given is after the sequence
        /////if we're after the last frame
        int afterChoice_i;
        _afterLast->getValue(afterChoice_i);
        BeforeAfterEnum afterChoice = BeforeAfterEnum(afterChoice_i);

        switch (afterChoice) {
            case eBeforeAfterHold: //hold
                *sequenceTime = sequenceTimeDomain.max;
                return eGetSequenceTimeAfterSequence;

            case eBeforeAfterLoop: //loop
                timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                *sequenceTime = sequenceTimeDomain.min + timeOffsetFromStart;
                return eGetSequenceTimeAfterSequence;

            case eBeforeAfterBounce: { //bounce
                int sequenceIntervalsCount = timeOffsetFromStart / (sequenceTimeDomain.max - sequenceTimeDomain.min);
                ///if the sequenceIntervalsCount is odd then do exactly like loop, otherwise do the load the opposite frame
                if (sequenceIntervalsCount % 2 == 0) {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    *sequenceTime = sequenceTimeDomain.min + timeOffsetFromStart;
                } else {
                    timeOffsetFromStart %= (int)(sequenceTimeDomain.max - sequenceTimeDomain.min + 1);
                    *sequenceTime = sequenceTimeDomain.max - timeOffsetFromStart;
                }
                return eGetSequenceTimeAfterSequence;
            }
            case eBeforeAfterBlack: //black
                return eGetSequenceTimeBlack;
                break;

            case eBeforeAfterError: //error
                setPersistentMessage(OFX::Message::eMessageError, "", "Out of frame range");
                return eGetSequenceTimeError;
        }
    }
    return eGetSequenceTimeError;
}

GenericReaderPlugin::GetFilenameRetCodeEnum
GenericReaderPlugin::getFilenameAtSequenceTime(double sequenceTime,
                                               bool proxyFiles,
                                               std::string *filename)
{
    GetFilenameRetCodeEnum ret;
    OfxRangeD sequenceTimeDomain;
    getSequenceTimeDomainInternal(sequenceTimeDomain,false);

    int missingFrame_i;
    _missingFrameParam->getValue(missingFrame_i);
    const MissingEnum missingFrame = MissingEnum(missingFrame_i);

    std::string filename0;

    bool filenameGood = true;
    int offset = 0;

    do {
        _fileParam->getValueAtTime(sequenceTime + offset, *filename);
        if (offset == 0) {
            filename0 = *filename; // for error reporting
        }
        if (filename->empty()) {
            filenameGood = false;
        }
//        else {
//            std::ifstream fs(filename->c_str());
//            filenameGood = fs.is_open() && fs.good();
//        }
        if (filenameGood) {
            ret = eGetFileNameReturnedFullRes;
            // now, try the proxy file
            if (proxyFiles) {
                std::string proxyFileName;
                bool proxyGood;
                _proxyFileParam->getValueAtTime(sequenceTime + offset, proxyFileName);
                if (proxyFileName.empty()) {
                    proxyGood = false;
                } else {
                    std::ifstream fs(proxyFileName.c_str());
                    proxyGood = fs.is_open() && fs.good();
                }
                if (proxyGood) {
                    // proxy file exists, replace the filename with the proxy name
                    *filename = proxyFileName;
                    ret = eGetFileNameReturnedProxy;
                }
            }
        }
        if (offset <= 0) {
            offset = -offset + 1;
        } else {
            offset = -offset;
        }
    } while (missingFrame == eMissingNearest &&      // only loop if eMissingNearest
             !filenameGood &&                        // and no valid file was found
             offset <= MISSING_FRAME_NEAREST_RANGE); // and we are within range

    if (filenameGood) {
        // ret is already set (see above)
    } else {
        *filename = filename0; // reset to the original frame name;
        switch (missingFrame) {
            case eMissingNearest: // Load nearest
            case eMissingError:   // Error
                /// For images sequences, we can safely say this is  a missing frame. For video-streams we do not know and the derived class
                // will have to handle the case itself.
                ret = eGetFileNameFailed;
                // return a black image
                break;
            case eMissingBlack:  // Black image
                /// For images sequences, we can safely say this is  a missing frame. For video-streams we do not know and the derived class
                // will have to handle the case itself.
                ret = eGetFileNameBlack;
                break;
        }
    }
    return ret;
}

OfxStatus
GenericReaderPlugin::getFilenameAtTime(double t, std::string *filename)
{
    double sequenceTime;
    GetSequenceTimeRetEnum getSequenceTimeRet = getSequenceTime(t, false, &sequenceTime);
    switch (getSequenceTimeRet) {
        case eGetSequenceTimeBlack:
            return kOfxStatReplyDefault;

        case eGetSequenceTimeError:
            return kOfxStatFailed;

        case eGetSequenceTimeBeforeSequence:
        case eGetSequenceTimeWithinSequence:
        case eGetSequenceTimeAfterSequence:
            break;
    }
    GetFilenameRetCodeEnum getFilenameAtSequenceTimeRet = getFilenameAtSequenceTime(sequenceTime, false, filename);
    switch (getFilenameAtSequenceTimeRet) {
        case eGetFileNameFailed:
            // do not setPersistentMessage()!
            return kOfxStatFailed;

        case eGetFileNameBlack:
            return kOfxStatReplyDefault;

        case eGetFileNameReturnedFullRes:
        case eGetFileNameReturnedProxy:
            break;
    }
    return kOfxStatOK;
}

int
GenericReaderPlugin::getStartingTime()
{
    int startingTime;
    _startingTime->getValue(startingTime);
    return startingTime;
}

/* set up and run a copy processor */
static void
setupAndCopy(OFX::PixelProcessorFilterBase & processor,
             const OfxRectI &renderWindow,
             const void *srcPixelData,
             const OfxRectI& srcBounds,
             OFX::PixelComponentEnum srcPixelComponents,
             OFX::BitDepthEnum srcPixelDepth,
             int srcRowBytes,
             void *dstPixelData,
             const OfxRectI& dstBounds,
             OFX::PixelComponentEnum dstPixelComponents,
             OFX::BitDepthEnum dstPixelDepth,
             int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    assert(srcBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= srcBounds.y2);
    assert(srcBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= srcBounds.x2);

    // make sure bit depths are sane
    if (srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, 0);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

void
GenericReaderPlugin::copyPixelData(const OfxRectI& renderWindow,
                                   const void *srcPixelData,
                                   const OfxRectI& srcBounds,
                                   OFX::PixelComponentEnum srcPixelComponents,
                                   OFX::BitDepthEnum srcPixelDepth,
                                   int srcRowBytes,
                                   void *dstPixelData,
                                   const OfxRectI& dstBounds,
                                   OFX::PixelComponentEnum dstPixelComponents,
                                   OFX::BitDepthEnum dstBitDepth,
                                   int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    assert(srcBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= srcBounds.y2);
    assert(srcBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= srcBounds.x2);

#ifdef GENERIC_READER_USE_MULTI_THREAD

    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopier<float, 4, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        OFX::PixelCopier<float, 3, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        OFX::PixelCopier<float, 1, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } // switch
#else
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        copyPixels<float,4>(renderWindow, (const float*)srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, (float*)dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        copyPixels<float,3>(renderWindow, (const float*)srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, (float*)dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        copyPixels<float,1>(renderWindow, (const float*)srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, (float*)dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } // switch
#endif
}

// update the window of dst defined by nextRenderWindow by halving the corresponding area in src.
// proofread and fixed by F. Devernay on 3/10/2014
template <typename PIX,int nComponents>
static void
halveWindow(const OfxRectI& nextRenderWindow,
            const PIX* srcPixels,
            const OfxRectI& srcBounds,
            int srcRowBytes,
            PIX* dstPixels,
            const OfxRectI& dstBounds,
            int dstRowBytes)
{
    int srcRowSize = srcRowBytes / sizeof(PIX);
    int dstRowSize = dstRowBytes / sizeof(PIX);
    
    const PIX* srcData =  srcPixels - (srcBounds.x1 * nComponents + srcRowSize * srcBounds.y1);
    PIX* dstData = dstPixels - (dstBounds.x1 * nComponents + dstRowSize * dstBounds.y1);
    
    assert(nextRenderWindow.x1 * 2 >= (srcBounds.x1 - 1) && (nextRenderWindow.x2-1) * 2 < srcBounds.x2 &&
           nextRenderWindow.y1 * 2 >= (srcBounds.y1 - 1) && (nextRenderWindow.y2-1) * 2 < srcBounds.y2);
    for (int y = nextRenderWindow.y1; y < nextRenderWindow.y2;++y) {
        
        const PIX* srcLineStart = srcData + y * 2 * srcRowSize;
        PIX* dstLineStart = dstData + y * dstRowSize;

        bool pickNextRow = (y * 2) < (srcBounds.y2 - 1);
        bool pickThisRow = (y * 2) >= (srcBounds.y1);
        int sumH = (int)pickNextRow + (int)pickThisRow;
        assert(sumH == 1 || sumH == 2);
        for (int x = nextRenderWindow.x1; x < nextRenderWindow.x2;++x) {
            
            bool pickNextCol = (x * 2) < (srcBounds.x2 - 1);
            bool pickThisCol = (x * 2) >= (srcBounds.x1);
            int sumW = (int)pickThisCol + (int)pickNextCol;
            assert(sumW == 1 || sumW == 2);
            for (int k = 0; k < nComponents; ++k) {
                ///a b
                ///c d
                
                PIX a = (pickThisCol && pickThisRow) ? srcLineStart[x * 2 * nComponents + k] : 0;
                PIX b = (pickNextCol && pickThisRow) ? srcLineStart[(x * 2 + 1) * nComponents + k] : 0;
                PIX c = (pickThisCol && pickNextRow) ? srcLineStart[(x * 2 * nComponents) + srcRowSize + k]: 0;
                PIX d = (pickNextCol && pickNextRow) ? srcLineStart[(x * 2 + 1) * nComponents + srcRowSize + k] : 0;

                assert(sumW == 2 || (sumW == 1 && ((a == 0 && c == 0) || (b == 0 && d == 0))));
                assert(sumH == 2 || (sumH == 1 && ((a == 0 && b == 0) || (c == 0 && d == 0))));
                dstLineStart[x * nComponents + k] = (a + b + c + d) / (sumH * sumW);
            }
        }
    }
}

// update the window of dst defined by originalRenderWindow by mipmapping the windows of src defined by renderWindowFullRes
// proofread and fixed by F. Devernay on 3/10/2014
template <typename PIX,int nComponents>
static void
buildMipMapLevel(OFX::ImageEffect* instance,
                 const OfxRectI& originalRenderWindow,
                 const OfxRectI& renderWindowFullRes,
                 unsigned int level,
                 const PIX* srcPixels,
                 const OfxRectI& srcBounds,
                 int srcRowBytes,
                 PIX* dstPixels,
                 const OfxRectI& dstBounds,
                 int dstRowBytes)
{
    assert(level > 0);

    std::auto_ptr<OFX::ImageMemory> mem;
    size_t memSize = 0;
    std::auto_ptr<OFX::ImageMemory> tmpMem;
    size_t tmpMemSize = 0;
    PIX* nextImg = NULL;

    const PIX* previousImg = srcPixels;
    OfxRectI previousBounds = srcBounds;
    int previousRowBytes = srcRowBytes;

    OfxRectI nextRenderWindow = renderWindowFullRes;

    ///Build all the mipmap levels until we reach the one we are interested in
    for (unsigned int i = 1; i < level; ++i) {
        // loop invariant:
        // - previousImg, previousBounds, previousRowBytes describe the data ate the level before i
        // - nextRenderWindow contains the renderWindow at the level before i
        //
        ///Halve the smallest enclosing po2 rect as we need to render a minimum of the renderWindow
        nextRenderWindow = downscalePowerOfTwoSmallestEnclosing(nextRenderWindow, 1);
#     ifdef DEBUG
        {
            // check that doing i times 1 level is the same as doing i levels
            OfxRectI nrw = downscalePowerOfTwoSmallestEnclosing(renderWindowFullRes, i);
            assert(nrw.x1 == nextRenderWindow.x1 && nrw.x2 == nextRenderWindow.x2 && nrw.y1 == nextRenderWindow.y1 && nrw.y2 == nextRenderWindow.y2);
        }
#     endif
        ///Allocate a temporary image if necessary, or reuse the previously allocated buffer
        int nextRowBytes =  (nextRenderWindow.x2 - nextRenderWindow.x1)  * nComponents * sizeof(PIX);
        size_t newMemSize =  (nextRenderWindow.y2 - nextRenderWindow.y1) * nextRowBytes;
        if (tmpMem.get()) {
            // there should be enough memory: no need to reallocate
            assert(tmpMemSize >= memSize);
        } else {
            tmpMem.reset(new OFX::ImageMemory(newMemSize, instance));
            tmpMemSize = newMemSize;
        }
        nextImg = (float*)tmpMem->lock();

        halveWindow<PIX, nComponents>(nextRenderWindow, previousImg, previousBounds, previousRowBytes, nextImg, nextRenderWindow, nextRowBytes);

        ///Switch for next pass
        previousBounds = nextRenderWindow;
        previousRowBytes = nextRowBytes;
        previousImg = nextImg;
        mem = tmpMem;
        memSize = tmpMemSize;
    }
    // here:
    // - previousImg, previousBounds, previousRowBytes describe the data ate the level before 'level'
    // - nextRenderWindow contains the renderWindow at the level before 'level'

    ///On the last iteration halve directly into the dstPixels
    ///The nextRenderWindow should be equal to the original render window.
    nextRenderWindow = downscalePowerOfTwoSmallestEnclosing(nextRenderWindow, 1);
    assert(originalRenderWindow.x1 == nextRenderWindow.x1 && originalRenderWindow.x2 == nextRenderWindow.x2 &&
           originalRenderWindow.y1 == nextRenderWindow.y1 && originalRenderWindow.y2 == nextRenderWindow.y2);

    halveWindow<PIX, nComponents>(nextRenderWindow, previousImg, previousBounds, previousRowBytes, dstPixels, dstBounds, dstRowBytes);
    // mem and tmpMem are freed at destruction
}



void
GenericReaderPlugin::scalePixelData(const OfxRectI& originalRenderWindow,
                                    const OfxRectI& renderWindow,
                                    unsigned int levels,
                                    const void* srcPixelData,
                                    OFX::PixelComponentEnum srcPixelComponents,
                                    OFX::BitDepthEnum srcPixelDepth,
                                    const OfxRectI& srcBounds,
                                    int srcRowBytes,
                                    void* dstPixelData,
                                    OFX::PixelComponentEnum dstPixelComponents,
                                    OFX::BitDepthEnum dstPixelDepth,
                                    const OfxRectI& dstBounds,
                                    int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // do the rendering
    if (dstPixelDepth != OFX::eBitDepthFloat ||
        (dstPixelComponents != OFX::ePixelComponentRGBA &&
         dstPixelComponents != OFX::ePixelComponentRGB &&
         dstPixelComponents != OFX::ePixelComponentAlpha) ||
        dstPixelDepth != srcPixelDepth ||
        dstPixelComponents != srcPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        buildMipMapLevel<float, 4>(this, originalRenderWindow, renderWindow, levels, (const float*)srcPixelData,
                                   srcBounds, srcRowBytes, (float*)dstPixelData, dstBounds, dstRowBytes);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        buildMipMapLevel<float, 3>(this, originalRenderWindow, renderWindow, levels, (const float*)srcPixelData,
                                   srcBounds, srcRowBytes, (float*)dstPixelData, dstBounds, dstRowBytes);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        buildMipMapLevel<float, 1>(this, originalRenderWindow, renderWindow,levels, (const float*)srcPixelData,
                                   srcBounds, srcRowBytes, (float*)dstPixelData, dstBounds, dstRowBytes);
    } // switch
}

/* set up and run a copy processor */
static void
setupAndFillWithBlack(OFX::PixelProcessorFilterBase & processor,
                      const OfxRectI &renderWindow,
                      void *dstPixelData,
                      const OfxRectI& dstBounds,
                      OFX::PixelComponentEnum dstPixelComponents,
                      OFX::BitDepthEnum dstPixelDepth,
                      int dstRowBytes)
{
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    
    // set the render window
    processor.setRenderWindow(renderWindow);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


void
GenericReaderPlugin::fillWithBlack(const OfxRectI &renderWindow,
                                   void *dstPixelData,
                                   const OfxRectI& dstBounds,
                                   OFX::PixelComponentEnum dstPixelComponents,
                                   OFX::BitDepthEnum dstBitDepth,
                                   int dstRowBytes)
{
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::BlackFiller<float, 4> fred(*this);
        setupAndFillWithBlack(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        OFX::BlackFiller<float, 3> fred(*this);
        setupAndFillWithBlack(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        OFX::BlackFiller<float, 1> fred(*this);
        setupAndFillWithBlack(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } // switch

}


static void
setupAndProcess(OFX::PixelProcessorFilterBase & processor,
                int premultChannel,
                const OfxRectI &renderWindow,
                const void *srcPixelData,
                const OfxRectI& srcBounds,
                OFX::PixelComponentEnum srcPixelComponents,
                OFX::BitDepthEnum srcPixelDepth,
                int srcRowBytes,
                void *dstPixelData,
                const OfxRectI& dstBounds,
                OFX::PixelComponentEnum dstPixelComponents,
                OFX::BitDepthEnum dstPixelDepth,
                int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // make sure bit depths are sane
    if (srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, 0);
    
    // set the render window
    processor.setRenderWindow(renderWindow);
    
    processor.setPremultMaskMix(true, premultChannel, 1.);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


void
GenericReaderPlugin::unPremultPixelData(const OfxRectI &renderWindow,
                                        const void *srcPixelData,
                                        const OfxRectI& srcBounds,
                                        OFX::PixelComponentEnum srcPixelComponents,
                                        OFX::BitDepthEnum srcPixelDepth,
                                        int srcRowBytes,
                                        void *dstPixelData,
                                        const OfxRectI& dstBounds,
                                        OFX::PixelComponentEnum dstPixelComponents,
                                        OFX::BitDepthEnum dstBitDepth,
                                        int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
        setupAndProcess(fred, 3, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } else {
        ///other pixel components means you want to copy only...
        assert(false);
    }
}

void
GenericReaderPlugin::premultPixelData(const OfxRectI &renderWindow,
                                      const void *srcPixelData,
                                      const OfxRectI& srcBounds,
                                      OFX::PixelComponentEnum srcPixelComponents,
                                      OFX::BitDepthEnum srcPixelDepth,
                                      int srcRowBytes,
                                      void *dstPixelData,
                                      const OfxRectI& dstBounds,
                                      OFX::PixelComponentEnum dstPixelComponents,
                                      OFX::BitDepthEnum dstBitDepth,
                                      int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopierPremult<float, 4, 1, float, 4, 1> fred(*this);
        setupAndProcess(fred, 3, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        
    } else {
        ///other pixel components means you want to copy only...
        assert(false);
    }
}


bool
GenericReaderPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                           OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    double sequenceTime;
    GetSequenceTimeRetEnum getSequenceTimeRet = getSequenceTime(args.time, false, &sequenceTime);
    switch (getSequenceTimeRet) {
        case eGetSequenceTimeBlack:
            return false;

        case eGetSequenceTimeError:
            OFX::throwSuiteStatusException(kOfxStatFailed);

        case eGetSequenceTimeBeforeSequence:
        case eGetSequenceTimeWithinSequence:
        case eGetSequenceTimeAfterSequence:
            break;
    }

    /*We don't want to use the proxy image for the region of definition*/
    std::string filename;
    GetFilenameRetCodeEnum getFilenameAtSequenceTimeRet = getFilenameAtSequenceTime(sequenceTime, false, &filename);
    switch (getFilenameAtSequenceTimeRet) {
        case eGetFileNameFailed:
            setPersistentMessage(OFX::Message::eMessageError, "", filename + ": Cannot load frame");
            OFX::throwSuiteStatusException(kOfxStatFailed);

        case eGetFileNameBlack:
            clearPersistentMessage();
            return false;

        case eGetFileNameReturnedFullRes:
        case eGetFileNameReturnedProxy:
            clearPersistentMessage();
            break;
    }

    std::string error;
    OfxRectI bounds;
    double par = 1.;
    bool success = getFrameBounds(filename, sequenceTime, &bounds, &par, &error);
    if (!success) {
        setPersistentMessage(OFX::Message::eMessageError, "", error);
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    // get the PAR from the clip preferences, since it should be constant over time
    par = _outputClip->getPixelAspectRatio();
    rod.x1 = bounds.x1 * par;
    rod.x2 = bounds.x2 * par;
    rod.y1 = bounds.y1;
    rod.y2 = bounds.y2;

//    if (getFilenameAtSequenceTimeRet == eGetFileNameReturnedProxy) {
//        ///upscale the proxy RoD to be in canonical coords.
//        unsigned int mipmapLvl = getLevelFromScale(args.renderScale.x);
//        rod = upscalePowerOfTwo(rod, (double)mipmapLvl);
//    }
    
    return true;
}

void
GenericReaderPlugin::render(const OFX::RenderArguments &args)
{
    if (!_outputClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    assert(kSupportsRenderScale || (args.renderScale.x == 1. && args.renderScale.y == 1.));
    ///The image will have the appropriate size since we support the render scale (multi-resolution)
    std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    void* dstPixelData = NULL;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int dstRowBytes;
    getImageData(dstImg.get(), &dstPixelData, &bounds, &pixelComponents, &bitDepth, &dstRowBytes);
    if (bitDepth != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    float* dstPixelDataF = (float*)dstPixelData;

    if (!dstPixelDataF) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host provided an invalid image buffer");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    OFX::PixelComponentEnum outputComponents = getOutputComponents();
    if (pixelComponents != outputComponents) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host dit not take into account output components");
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    // are we in the image bounds
    if (args.renderWindow.x1 < bounds.x1 || args.renderWindow.x1 >= bounds.x2 || args.renderWindow.y1 < bounds.y1 || args.renderWindow.y1 >= bounds.y2 ||
        args.renderWindow.x2 <= bounds.x1 || args.renderWindow.x2 > bounds.x2 || args.renderWindow.y2 <= bounds.y1 || args.renderWindow.y2 > bounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        //throw std::runtime_error("render window outside of image bounds");
    }

    double sequenceTime;
    GetSequenceTimeRetEnum getSequenceTimeRet = getSequenceTime(args.time, false, &sequenceTime);
    switch (getSequenceTimeRet) {
        case eGetSequenceTimeBlack:
            fillWithBlack(args.renderWindow, dstPixelDataF, bounds,pixelComponents, dstImg->getPixelDepth(), dstRowBytes);
            return;

        case eGetSequenceTimeError:
            OFX::throwSuiteStatusException(kOfxStatFailed);

        case eGetSequenceTimeBeforeSequence:
        case eGetSequenceTimeWithinSequence:
        case eGetSequenceTimeAfterSequence:
            break;
    }

    bool useProxy = false;
    OfxPointD proxyScaleThreshold;
    _proxyThreshold->getValue(proxyScaleThreshold.x, proxyScaleThreshold.y);

    OfxPointD proxyOriginalScale;
    _originalProxyScale->getValue(proxyOriginalScale.x, proxyOriginalScale.y);
    
    ///We only support downscaling at a power of two.
    unsigned int renderMipmapLevel = getLevelFromScale(std::min(args.renderScale.x,args.renderScale.y));
    unsigned int proxyMipMapThresholdLevel = getLevelFromScale(std::min(proxyScaleThreshold.x, proxyScaleThreshold.y));
    unsigned int originalProxyMipMapLevel = getLevelFromScale(std::min(proxyOriginalScale.x, proxyOriginalScale.y));
    
    if (kSupportsRenderScale && (renderMipmapLevel >= proxyMipMapThresholdLevel)) {
        useProxy = true;
    }

    std::string filename;
    GetFilenameRetCodeEnum getFilenameAtSequenceTimeRet = getFilenameAtSequenceTime(sequenceTime, false, &filename);
    switch (getFilenameAtSequenceTimeRet) {
        case eGetFileNameFailed:
            setPersistentMessage(OFX::Message::eMessageError, "", filename + ": Cannot load frame");
            OFX::throwSuiteStatusException(kOfxStatFailed);

        case eGetFileNameBlack:
            clearPersistentMessage();
            fillWithBlack(args.renderWindow, dstPixelDataF, bounds, pixelComponents, dstImg->getPixelDepth(), dstRowBytes);
            return;

        case eGetFileNameReturnedFullRes:
            clearPersistentMessage();
            break;

        case eGetFileNameReturnedProxy:
            // we didn't ask for proxy!
            assert(false);
            OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    std::string proxyFile;
    if (useProxy) {
        ///Use the proxy only if getFilenameAtSequenceTime returned a valid proxy filename different from the original file
        GetFilenameRetCodeEnum getFilenameAtSequenceTimeRetPx = getFilenameAtSequenceTime(sequenceTime, true, &proxyFile);
        switch (getFilenameAtSequenceTimeRetPx) {
            case eGetFileNameFailed:
                // should never happen: it should return at least the full res frame
                assert(false);
                OFX::throwSuiteStatusException(kOfxStatFailed);

            case eGetFileNameBlack:
                // should never happen: it should return at least the full res frame
                assert(false);
                fillWithBlack(args.renderWindow, dstPixelDataF, bounds,pixelComponents, dstImg->getPixelDepth(), dstRowBytes);
                return;

            case eGetFileNameReturnedFullRes:
                assert(proxyFile == filename);
                useProxy = false;
                break;

            case eGetFileNameReturnedProxy:
                assert(!proxyFile.empty());
                useProxy = (proxyFile != filename);
                break;
        }
    }


    // The args.renderWindow is already in pixels coordinate (render scale is already taken into account).
    // If the filename IS NOT a proxy file we have to make sure the renderWindow is
    // upscaled to a scale of (1,1). On the other hand if the filename IS a proxy we have to determine the actual RoD
    // of the proxy file and adjust the scale so it fits the given scale.
    // When in proxy mode renderWindowFullRes is the render window at the proxy mip map level
    int downscaleLevels = renderMipmapLevel; // the number of mipmap levels from the actual file (proxy or not) to the renderWindow

    if (_supportsTiles && useProxy && !proxyFile.empty()) {
        filename = proxyFile;
        downscaleLevels -= originalProxyMipMapLevel;
    }
    assert(downscaleLevels >= 0);
    
    if (filename.empty() || !fileExists(filename)) {
        fillWithBlack(args.renderWindow, dstPixelDataF, bounds, pixelComponents, dstImg->getPixelDepth(), dstRowBytes);
        return;
    }

    OfxRectI renderWindowFullRes;
    OfxRectI frameBounds;
    double par = 1.;
    std::string error;

    ///if the plug-in doesn't support tiles, just render the full rod
    bool success = getFrameBounds(filename, sequenceTime, &frameBounds, &par, &error);
    ///We shouldve checked above for any failure, now this is too late.
    if (!success) {
        setPersistentMessage(OFX::Message::eMessageError, "", error);
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    renderWindowFullRes = upscalePowerOfTwo(args.renderWindow, downscaleLevels); // works even if downscaleLevels == 0

    // Intersect the full res renderwindow to the real rod,
    // because we may have gone a few pixels too far (but never more than 2^downscaleLevels-1 pixels)
    assert(renderWindowFullRes.x1 >= frameBounds.x1 - std::pow(2.,(double)downscaleLevels) + 1 &&
           renderWindowFullRes.x2 <= frameBounds.x2 + std::pow(2.,(double)downscaleLevels) - 1 &&
           renderWindowFullRes.y1 >= frameBounds.y1 - std::pow(2.,(double)downscaleLevels) + 1 &&
           renderWindowFullRes.y2 <= frameBounds.y2 + std::pow(2.,(double)downscaleLevels) - 1);
    intersect(renderWindowFullRes, frameBounds, &renderWindowFullRes);

    // Read into a temporary image, apply colorspace conversion, then copy.

    int premult_i;
    _premult->getValue(premult_i);
    OFX::PreMultiplicationEnum premult = (OFX::PreMultiplicationEnum)premult_i;
    // force premult for non-RGBA pixelComponents
    if (pixelComponents == OFX::ePixelComponentRGB) {
        premult = OFX::eImageOpaque;
    } else if (pixelComponents == OFX::ePixelComponentAlpha) {
        premult = OFX::eImagePreMultiplied;
    }

    // we have to do the final premultiplication if:
    // - pixelComponents is RGBA
    //  AND
    //   - buffer is PreMultiplied AND OCIO is not identity (OCIO works only on unpremultiplied data)
    //   OR
    //   - premult is unpremultiplied
    bool mustPremult = ((pixelComponents == OFX::ePixelComponentRGBA) &&
                        ((premult == OFX::eImagePreMultiplied && !_ocio->isIdentity(args.time)) ||
                         premult == OFX::eImageUnPreMultiplied));

    if (!mustPremult && _ocio->isIdentity(args.time) && (!kSupportsRenderScale || renderMipmapLevel == 0)) {
        // no colorspace conversion, no premultiplication, no proxy, just read file
        DBG(std::printf("decode (to dst)\n"));
        decode(filename, sequenceTime, args.renderWindow, dstPixelDataF, bounds, pixelComponents, dstRowBytes);

    } else {
        int pixelBytes = getPixelBytes(pixelComponents, bitDepth);
        int tmpRowBytes = (renderWindowFullRes.x2-renderWindowFullRes.x1) * pixelBytes;
        size_t memSize = (size_t)(renderWindowFullRes.y2-renderWindowFullRes.y1) * tmpRowBytes;
        OFX::ImageMemory mem(memSize, this);
        float *tmpPixelData = (float*)mem.lock();

        // read file
        DBG(std::printf("decode (to tmp)\n"));
        decode(filename, sequenceTime, renderWindowFullRes, tmpPixelData, renderWindowFullRes, pixelComponents, tmpRowBytes);

        ///do the color-space conversion
        if (!_ocio->isIdentity(args.time) && pixelComponents != OFX::ePixelComponentAlpha) {
            if (premult == OFX::eImagePreMultiplied) {
                assert(pixelComponents == OFX::ePixelComponentRGBA);
                DBG(std::printf("unpremult (tmp in-place)\n"));
                //tmpPixelData[0] = tmpPixelData[1] = tmpPixelData[2] = tmpPixelData[3] = 0.5;
                unPremultPixelData(renderWindowFullRes, tmpPixelData, renderWindowFullRes, pixelComponents, bitDepth, tmpRowBytes, tmpPixelData, renderWindowFullRes, pixelComponents, bitDepth, tmpRowBytes);
                //assert(tmpPixelData[0] == 1. && tmpPixelData[1] == 1. && tmpPixelData[2] == 1. && tmpPixelData[3] == 0.5);
            }
            DBG(std::printf("OCIO (tmp in-place)\n"));
            _ocio->apply(args.time, renderWindowFullRes, tmpPixelData, renderWindowFullRes, pixelComponents, tmpRowBytes);
        }
        
        if (kSupportsRenderScale && downscaleLevels > 0) {
            if (!mustPremult) {
                // we can write directly to dstPixelData
                /// adjust the scale to match the given output image
                DBG(std::printf("scale (no premult, tmp to dst)\n"));
                scalePixelData(args.renderWindow,renderWindowFullRes,(unsigned int)downscaleLevels, tmpPixelData, pixelComponents,
                               bitDepth, renderWindowFullRes, tmpRowBytes, dstPixelData,
                               pixelComponents, bitDepth, bounds, dstRowBytes);
            } else {
                // allocate a temporary image (we must avoid reading from dstPixelData, in case several threads are rendering the same area)
                int mem2RowBytes = (bounds.x2 - bounds.x1) * pixelBytes;
                size_t mem2Size = (bounds.y2 - bounds.y1) * mem2RowBytes;
                OFX::ImageMemory mem2(mem2Size, this);
                float *scaledPixelData = (float*)mem2.lock();

                /// adjust the scale to match the given output image
                DBG(std::printf("scale (tmp to scaled)\n"));
                scalePixelData(args.renderWindow,renderWindowFullRes,(unsigned int)downscaleLevels, tmpPixelData, pixelComponents,
                               bitDepth, renderWindowFullRes, tmpRowBytes, scaledPixelData,
                               pixelComponents, bitDepth, bounds, mem2RowBytes);
                // apply premult
                DBG(std::printf("premult (scaled to dst)\n"));
                //scaledPixelData[0] = scaledPixelData[1] = scaledPixelData[2] = 1.; scaledPixelData[3] = 0.5;
                premultPixelData(args.renderWindow, scaledPixelData, bounds, pixelComponents, bitDepth, mem2RowBytes, dstPixelData, bounds, pixelComponents, bitDepth, dstRowBytes);
                //assert(dstPixelDataF[0] == 0.5 && dstPixelDataF[1] == 0.5 && dstPixelDataF[2] == 0.5 && dstPixelDataF[3] == 0.5);
            }
        } else {
            // copy
            if (mustPremult) {
                DBG(std::printf("premult (no scale, tmp to dst)\n"));
                //tmpPixelData[0] = tmpPixelData[1] = tmpPixelData[2] = 1.; tmpPixelData[3] = 0.5;
                premultPixelData(args.renderWindow, tmpPixelData, renderWindowFullRes, pixelComponents, bitDepth, tmpRowBytes, dstPixelData, bounds, pixelComponents, bitDepth, dstRowBytes);
                //assert(dstPixelDataF[0] == 0.5 && dstPixelDataF[1] == 0.5 && dstPixelDataF[2] == 0.5 && dstPixelDataF[3] == 0.5);
            } else {
                DBG(std::printf("copy (no premult no scale, tmp to dst)\n"));
                copyPixelData(args.renderWindow, tmpPixelData, renderWindowFullRes, pixelComponents, bitDepth, tmpRowBytes, dstPixelData, bounds, pixelComponents, bitDepth, dstRowBytes);
            }
        }
        mem.unlock();
    }
}

void
GenericReaderPlugin::setSequenceFromFile(const std::string& filename)
{
    ///Do this only when the host is not Natron because we can't rely on the host to compute the frame range for us.
    ///Natron will set automatically the originalRange parameter when the user selects a file
    if (gHostIsNatron) {
        return;
    }
    
    SequenceParsing::FileNameContent content(filename);
    std::string pattern;
    ///We try to match all the files in the same directory that match the pattern with the frame number
    ///assumed to be in the last part of the filename. This is a harsh assumption but we can't just verify
    ///everything as it would take too much time.
    std::string noStr;
    int nbFrameNumbers = content.getPotentialFrameNumbersCount();
    content.getNumberByIndex(nbFrameNumbers - 1, &noStr);
    
    int numHashes = content.getNumPrependingZeroes();
    
    std::string noStrWithoutZeroes;
    for (std::size_t i = 0; i < noStr.size(); ++i) {
        if (noStr[i] == '0' && noStrWithoutZeroes.empty()) {
            continue;
        }
        noStrWithoutZeroes.push_back(noStr[i]);
    }
    
    if ((int)noStr.size() > numHashes) {
        numHashes += noStrWithoutZeroes.size();
    } else {
        numHashes = 1;
    }
    content.generatePatternWithFrameNumberAtIndex(nbFrameNumbers - 1,
                                                  numHashes,
                                                  &pattern);
    
    
    _sequenceFromFiles.clear();
    SequenceParsing::filesListFromPattern(pattern, &_sequenceFromFiles);

}


void
GenericReaderPlugin::inputFileChanged()
{
    std::string filename;
    
    if (!gHostIsNatron) {
        
        _fileParam->getValue(filename);
        
        setSequenceFromFile(filename);
        
        clearPersistentMessage();
        
        //reset the original range param only if not Natron
        _originalFrameRange->setValue(kOfxFlagInfiniteMin, kOfxFlagInfiniteMax);
    }
    
    
    OfxRangeD tmp;
    if (getSequenceTimeDomainInternal(tmp,true)) {
        timeDomainFromSequenceTimeDomain(tmp, true);
        _startingTime->setValue(tmp.min);
        
        ///We call onInputFileChanged with the first frame of the sequence so we're almost sure it will work
        ///unless the user did a mistake. We are also safe to assume that images specs are the same for
        ///all the sequence
        _fileParam->getValueAtTime(tmp.min, filename);
        ///let the derive class a chance to initialize any data structure it may need
        
        OFX::PixelComponentEnum components;
        OFX::PreMultiplicationEnum premult;
        onInputFileChanged(filename, &premult, &components);
        // RGB is always Opaque, Alpha is always PreMultiplied
        if (components == OFX::ePixelComponentRGB) {
            premult = OFX::eImageOpaque;
        } else if (components == OFX::ePixelComponentAlpha) {
            premult = OFX::eImagePreMultiplied;
        }
        setOutputComponents(components);
        _premult->setValue((int)premult);
        
        bool customFps;
        _customFPS->getValue(customFps);
        if (!customFps) {
            double fps;
            bool gotFps = getFrameRate(filename, &fps);
            if (gotFps) {
                _fps->setValue(fps);
            }
        }
    }
}

void
GenericReaderPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                  const std::string &paramName)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // please check the reason for each parameter when it makes sense!

    if (paramName == kParamFilename) {
        if (args.reason != OFX::eChangeTime) {
            inputFileChanged();
        }

    } else if (paramName == kParamProxy) {
        ///Detect the scale of the proxy.
        std::string proxyFile,originalFileName;
        double sequenceTime;
        GetSequenceTimeRetEnum getSequenceTimeRet = getSequenceTime(args.time, false, &sequenceTime);
        switch (getSequenceTimeRet) {
            case eGetSequenceTimeBlack:
            case eGetSequenceTimeError:
                break;

            case eGetSequenceTimeBeforeSequence:
            case eGetSequenceTimeWithinSequence:
            case eGetSequenceTimeAfterSequence: {
                GetFilenameRetCodeEnum getFilenameAtSequenceTimeRet   = getFilenameAtSequenceTime(sequenceTime, false, &originalFileName);
                GetFilenameRetCodeEnum getFilenameAtSequenceTimeRetPx = getFilenameAtSequenceTime(sequenceTime, true, &proxyFile);

                if (getFilenameAtSequenceTimeRet == eGetFileNameReturnedFullRes &&
                    getFilenameAtSequenceTimeRetPx ==  eGetFileNameReturnedProxy &&
                    proxyFile != originalFileName) {
                    assert(!proxyFile.empty());
                    ///show the scale param
                    _proxyThreshold->setIsSecret(false);
                    _enableCustomScale->setIsSecret(false);

                    OfxPointD scale = detectProxyScale(originalFileName,proxyFile,args.time);
                    _proxyThreshold->setValue(scale.x, scale.y);
                    _originalProxyScale->setValue(scale.x, scale.y);
                } else {
                    _proxyThreshold->setIsSecret(true);
                    _enableCustomScale->setIsSecret(true);
                }
            }   break;
        }

    } else if (paramName == kParamCustomProxyScale) {
        bool enabled;
        _enableCustomScale->getValue(enabled);
        _proxyThreshold->setEnabled(enabled);

    } else if (paramName == kParamFirstFrame &&  args.reason == OFX::eChangeUserEdit) {

        int first;
        int last;
        _firstFrame->getValue(first);
        _lastFrame->getValue(last);
        _lastFrame->setRange(first, last);

        int offset;
        _timeOffset->getValue(offset);
        _startingTime->setValue(first + offset); // will be called with reason == eChangePluginEdit
        
        _timeDomainUserSet->setValue(true);

    } else if (paramName == kParamLastFrame && args.reason == OFX::eChangeUserEdit) {
        int first;
        int last;
        _firstFrame->getValue(first);
        _lastFrame->getValue(last);
        _firstFrame->setRange(first, last);
        
        _timeDomainUserSet->setValue(true);

    } else if (paramName == kParamFrameMode && args.reason == OFX::eChangeUserEdit) {
        int frameMode_i;
        _frameMode->getValue(frameMode_i);
        FrameModeEnum frameMode = FrameModeEnum(frameMode_i);
        switch (frameMode) {
            case eFrameModeStartingTime: //starting frame
                _startingTime->setIsSecret(false);
                _timeOffset->setIsSecret(true);
                break;
            case eFrameModeTimeOffset: //time offset
                _startingTime->setIsSecret(true);
                _timeOffset->setIsSecret(false);
                break;
        }

    } else if (paramName == kParamStartingTime && args.reason == OFX::eChangeUserEdit) {
        ///recompute the timedomain
        OfxRangeD sequenceTimeDomain;
        getSequenceTimeDomainInternal(sequenceTimeDomain,true);
        
        //also update the time offset
        int startingTime;
        _startingTime->getValue(startingTime);
        
        int firstFrame;
        _firstFrame->getValue(firstFrame);
        
        _timeOffset->setValue(startingTime - firstFrame);
        
    } else if (paramName == kParamTimeOffset && args.reason == OFX::eChangeUserEdit) {

        //also update the starting frame
        int offset;
        _timeOffset->getValue(offset);
        int first;
        _firstFrame->getValue(first);
        
        _startingTime->setValue(offset + first);

    } else if (paramName == kParamOutputComponents) {
        OFX::PixelComponentEnum comps = getOutputComponents();
    
        if (args.reason == OFX::eChangeUserEdit) {
            int premult_i;
            _premult->getValue(premult_i);
            OFX::PreMultiplicationEnum premult = (OFX::PreMultiplicationEnum)premult_i;
            if (comps == OFX::ePixelComponentRGB && premult != OFX::eImageOpaque) {
                // RGB is always opaque
                _premult->setValue(OFX::eImageOpaque);
            } else if (comps == OFX::ePixelComponentAlpha && premult != OFX::eImagePreMultiplied) {
                // Alpha is always premultiplied
                _premult->setValue(OFX::eImagePreMultiplied);
            }
        }
        
        // Even when reason == pluginEdit notify the plug-in that components changed
        onOutputComponentsParamChanged(comps);
    } else if (paramName == kParamFilePremult && args.reason == OFX::eChangeUserEdit) {
        int premult_i;
        _premult->getValue(premult_i);
        OFX::PreMultiplicationEnum premult = (OFX::PreMultiplicationEnum)premult_i;
        OFX::PixelComponentEnum comps = getOutputComponents();
        // reset to authorized values if necessary
        if (comps == OFX::ePixelComponentRGB && premult != OFX::eImageOpaque) {
            // RGB is always opaque
            _premult->setValue((int)OFX::eImageOpaque);
        } else if (comps == OFX::ePixelComponentAlpha && premult != OFX::eImagePreMultiplied) {
            // Alpha is always premultiplied
            _premult->setValue((int)OFX::eImagePreMultiplied);
        }
    } else if (paramName == kParamCustomFps) {
      
        bool customFps;
        _customFPS->getValue(customFps);
        _fps->setEnabled(customFps);
        
        if (!customFps) {
            OfxRangeD tmp;
            if (getSequenceTimeDomainInternal(tmp,false)) {
                timeDomainFromSequenceTimeDomain(tmp, false);
                _startingTime->setValue(tmp.min);
                
                ///We call onInputFileChanged with the first frame of the sequence so we're almost sure it will work
                ///unless the user did a mistake. We are also safe to assume that images specs are the same for
                ///all the sequence
                std::string filename;
                _fileParam->getValueAtTime(tmp.min, filename);
                
                double fps;
                bool gotFps = getFrameRate(filename, &fps);
                if  (gotFps) {
                    _fps->setValue(fps);
                }

            }
            
        }
        
    } else {
        _ocio->changedParam(args, paramName);
    }
}

OFX::PixelComponentEnum
GenericReaderPlugin::getOutputComponents() const
{
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    return gOutputComponentsMap[outputComponents_i];
}

void
GenericReaderPlugin::setOutputComponents(OFX::PixelComponentEnum comps)
{
    int i;
    for (i = 0; i < 4 && gOutputComponentsMap[i] != comps; ++i) {
    }
    if (i >= 4) {
        // not found, set the first supported component
        i = 0;
    }
    _outputComponents->setValue(i);
}

/* Override the clip preferences */
void
GenericReaderPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // if there is only one frame and before/after behaviour is hold, then
    // the output is not framevarying
    bool frameVarying = true;
    OfxRangeD sequenceTimeDomain;
    getSequenceTimeDomainInternal(sequenceTimeDomain, false);
    if (sequenceTimeDomain.min == sequenceTimeDomain.max) {
        int beforeChoice_i;
        _beforeFirst->getValue(beforeChoice_i);
        BeforeAfterEnum beforeChoice = BeforeAfterEnum(beforeChoice_i);
        int afterChoice_i;
        _afterLast->getValue(afterChoice_i);
        BeforeAfterEnum afterChoice = BeforeAfterEnum(afterChoice_i);
        if (beforeChoice == eBeforeAfterHold && afterChoice == eBeforeAfterHold) {
            frameVarying = false;
        }
    }
    clipPreferences.setOutputFrameVarying(frameVarying); // true for readers and frame-varying generators/effects @see kOfxImageEffectFrameVarying

    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    OFX::PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    clipPreferences.setClipComponents(*_outputClip, outputComponents);

    // the output of the GenericReader plugin is *always* premultiplied (as long as only float is supported)
    int premult_i;
    _premult->getValue(premult_i);
    OFX::PreMultiplicationEnum premult = (OFX::PreMultiplicationEnum)premult_i;
    switch (outputComponents) {
        case OFX::ePixelComponentRGBA:
            // may be Opaque or PreMultiplied (never UnPremultiplied)
            if (premult == OFX::eImageUnPreMultiplied) {
                premult = OFX::eImagePreMultiplied;
            }
            assert(premult == OFX::eImagePreMultiplied || premult == OFX::eImageOpaque);
            break;

        case OFX::ePixelComponentAlpha:
            // alpha is always premultiplied
            premult = OFX::eImagePreMultiplied;
            break;

        default:
            // RGB is always Opaque
            premult = OFX::eImageOpaque;
            break;
    }
    clipPreferences.setOutputPremultiplication(premult);

    // get the pixel aspect ratio from the first frame
    OfxRangeD tmp;
    if (getSequenceTimeDomainInternal(tmp, false)) {
        timeDomainFromSequenceTimeDomain(tmp, false);
        std::string filename;
        GetFilenameRetCodeEnum e = getFilenameAtSequenceTime(tmp.min, false, &filename);
        if (e == eGetFileNameReturnedFullRes) {
            OfxRectI bounds;
            double par = 1.;
            std::string error;
            bool success = getFrameBounds(filename, tmp.min, &bounds, &par, &error);
            if (success) {
                clipPreferences.setPixelAspectRatio(*_outputClip, par);
            }
            
            bool customFPS;
            _customFPS->getValue(customFPS);
            
            double fps;
            if (customFPS) {
                _fps->getValue(fps);
                clipPreferences.setOutputFrameRate(fps);
            } else {
                success = getFrameRate(filename, &fps);
                if (success) {
                    clipPreferences.setOutputFrameRate(fps);
                }
            }
        }
    }
}

void
GenericReaderPlugin::purgeCaches()
{
    clearAnyCache();
    _ocio->purgeCaches();
}

bool
GenericReaderPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                OFX::Clip * &identityClip,
                                double &identityTime)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if (!gHostIsNatron) {
        // only Natron supports setting the identityClip to the output clip
        return false;
    }

    double sequenceTime;
    GetSequenceTimeRetEnum getSequenceTimeRet = getSequenceTime(args.time, false, &sequenceTime);
    switch (getSequenceTimeRet) {
        case eGetSequenceTimeBlack:
            return false;

        case eGetSequenceTimeError:
            OFX::throwSuiteStatusException(kOfxStatFailed);

        case eGetSequenceTimeBeforeSequence:
        case eGetSequenceTimeAfterSequence:
            ///Transform the sequence time to "real" time
            int timeOffset;
            _timeOffset->getValue(timeOffset);
            identityTime = sequenceTime + timeOffset;
            identityClip = _outputClip;
            return true;

        case eGetSequenceTimeWithinSequence:
            return false;
    }
    return false;
}

OfxPointD
GenericReaderPlugin::detectProxyScale(const std::string& originalFileName,
                                      const std::string& proxyFileName,
                                      OfxTime time)
{
    OfxRectI originalBounds, proxyBounds;
    std::string error;
    double originalPAR = 1., proxyPAR = 1.;
    bool success = getFrameBounds(originalFileName, time, &originalBounds, &originalPAR, &error);
    proxyBounds.x1 = proxyBounds.x2 = proxyBounds.y1 = proxyBounds.y2 = 0.;
    success = success && getFrameBounds(proxyFileName, time, &proxyBounds, &proxyPAR, &error);
    OfxPointD ret;
    if (!success ||
        (originalBounds.x1 == originalBounds.x2) ||
        (originalBounds.y1 == originalBounds.y2) ||
        (proxyBounds.x1 == proxyBounds.x2) ||
        (proxyBounds.y1 == proxyBounds.y2)) {
        ret.x = 1.;
        ret.y = 1.;
        setPersistentMessage(OFX::Message::eMessageError, "", "Cannot read the proxy file.");
        return ret;
    }
    ret.x = ((proxyBounds.x2 - proxyBounds.x1)  * proxyPAR) / ((originalBounds.x2 - originalBounds.x1) * originalPAR);
    ret.y = (proxyBounds.y2 - proxyBounds.y1) / (double)(originalBounds.y2 - originalBounds.y1);
    return ret;
}

using namespace OFX;

void
GenericReaderDescribe(OFX::ImageEffectDescriptor &desc,
                      bool supportsTiles)
{
    desc.setPluginGrouping(kPluginGrouping);
    
#ifdef OFX_EXTENSIONS_TUTTLE
    desc.addSupportedContext(OFX::eContextReader);
#endif
    desc.addSupportedContext(OFX::eContextGenerator);
    desc.addSupportedContext(OFX::eContextGeneral);
    
    // add supported pixel depths
    //desc.addSupportedBitDepth( eBitDepthUByte );
    //desc.addSupportedBitDepth( eBitDepthUShort );
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    
    desc.setSupportsTiles(supportsTiles);
    desc.setTemporalClipAccess(false); // say we will not be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(true); // plugin may setPixelAspectRatio on output clip
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);
}

OFX::PageParamDescriptor *
GenericReaderDescribeInContextBegin(OFX::ImageEffectDescriptor &desc,
                                    OFX::ContextEnum /*context*/,
                                    bool /*isVideoStreamPlugin*/,
                                    bool supportsRGBA,
                                    bool supportsRGB,
                                    bool supportsAlpha,
                                    bool supportsTiles)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);

    for (ImageEffectHostDescription::PixelComponentArray::const_iterator it = getImageEffectHostDescription()->_supportedComponents.begin();
         it != getImageEffectHostDescription()->_supportedComponents.end();
         ++it) {
        switch (*it) {
            case ePixelComponentRGBA:
                gHostSupportsRGBA  = true;
                break;
            case ePixelComponentRGB:
                gHostSupportsRGB = true;
                break;
            case ePixelComponentAlpha:
                gHostSupportsAlpha = true;
                break;
            default:
                // other components are not supported by this plugin
                break;
        }
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // create the optional source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    if (supportsRGBA) {
        srcClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        srcClip->addSupportedComponent(ePixelComponentRGB);
    }
    if (supportsAlpha) {
        srcClip->addSupportedComponent(ePixelComponentAlpha);
    }
    srcClip->setSupportsTiles(supportsTiles);
    srcClip->setOptional(true);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    if (supportsRGBA) {
        dstClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        dstClip->addSupportedComponent(ePixelComponentRGB);
    }
    if (supportsAlpha) {
        dstClip->addSupportedComponent(ePixelComponentAlpha);
    }
    dstClip->setSupportsTiles(supportsTiles);
    

    //////////Input file
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamFilename);
        param->setLabels(kParamFilenameLabel, kParamFilenameLabel, kParamFilenameLabel);
        param->setStringType(OFX::eStringTypeFilePath);
        param->setFilePathExists(true);
        param->setHint(kParamFilenameHint);
        param->setAnimates(false);
        // in the Reader context, the script name must be kOfxImageEffectFileParamName, @see kOfxImageEffectContextReader
        param->setScriptName(kParamFilename);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }
    
    //////////First-frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamFirstFrame);
        param->setLabels(kParamFirstFrameLabel, kParamFirstFrameLabel, kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        param->setDefault(0);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    
    ///////////Before first
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBefore);
        param->setLabels(kParamBeforeLabel, kParamBeforeLabel, kParamBeforeLabel);
        param->setHint(kParamBeforeHint);
        assert(param->getNOptions() == eBeforeAfterHold);
        param->appendOption(kReaderOptionHold,   kReaderOptionHoldHint);
        assert(param->getNOptions() == eBeforeAfterLoop);
        param->appendOption(kReaderOptionLoop,   kReaderOptionLoopHint);
        assert(param->getNOptions() == eBeforeAfterBounce);
        param->appendOption(kReaderOptionBounce, kReaderOptionBounceHint);
        assert(param->getNOptions() == eBeforeAfterBlack);
        param->appendOption(kReaderOptionBlack,  kReaderOptionBlackHint);
        assert(param->getNOptions() == eBeforeAfterError);
        param->appendOption(kReaderOptionError,  kReaderOptionErrorHint);
        param->setAnimates(true);
        param->setDefault(eBeforeAfterHold);
        page->addChild(*param);
    }

    //////////Last-frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamLastFrame);
        param->setLabels(kParamLastFrameLabel, kParamLastFrameLabel, kParamLastFrameLabel);
        param->setHint(kParamLastFrameHint);
        param->setDefault(0);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    ///////////After first
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamAfter);
        param->setLabels(kParamAfterLabel, kParamAfterLabel, kParamAfterLabel);
        param->setHint(kParamAfterHint);
        assert(param->getNOptions() == eBeforeAfterHold);
        param->appendOption(kReaderOptionHold,   kReaderOptionHoldHint);
        assert(param->getNOptions() == eBeforeAfterLoop);
        param->appendOption(kReaderOptionLoop,   kReaderOptionLoopHint);
        assert(param->getNOptions() == eBeforeAfterBounce);
        param->appendOption(kReaderOptionBounce, kReaderOptionBounceHint);
        assert(param->getNOptions() == eBeforeAfterBlack);
        param->appendOption(kReaderOptionBlack,  kReaderOptionBlackHint);
        assert(param->getNOptions() == eBeforeAfterError);
        param->appendOption(kReaderOptionError,  kReaderOptionErrorHint);
        param->setAnimates(true);
        param->setDefault(eBeforeAfterHold);
        page->addChild(*param);
    }

    ///////////Missing frame choice
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOnMissingFrame);
        param->setLabels(kParamOnMissingFrameLabel, kParamOnMissingFrameLabel, kParamOnMissingFrameLabel);
        param->setHint(kParamOnMissingFrameHint);
        assert(param->getNOptions() == eMissingNearest);
        param->appendOption(kReaderOptionNearest,  kReaderOptionNearestHint);
        assert(param->getNOptions() == eMissingError);
        param->appendOption(kReaderOptionError,  kReaderOptionErrorHint);
        assert(param->getNOptions() == eMissingBlack);
        param->appendOption(kReaderOptionBlack,  kReaderOptionBlackHint);
        param->setAnimates(true);
        param->setDefault(eMissingNearest);
        page->addChild(*param);
    }

    ///////////Frame-mode
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFrameMode);
        param->setLabels(kParamFrameModeLabel, kParamFrameModeLabel, kParamFrameModeLabel);
        assert(param->getNOptions() == eFrameModeStartingTime);
        param->appendOption(kParamFrameModeOptionStartingTime);
        assert(param->getNOptions() == eFrameModeTimeOffset);
        param->appendOption(kParamFrameModeOptionTimeOffset);
        param->setAnimates(true);
        param->setDefault(eFrameModeStartingTime);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    ///////////Starting frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamStartingTime);
        param->setLabels(kParamStartingTimeLabel, kParamStartingTimeLabel, kParamStartingTimeLabel);
        param->setHint(kParamStartingTimeHint);
        param->setDefault(0);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    ///////////Time offset
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamTimeOffset);
        param->setLabels(kParamTimeOffsetLabel, kParamTimeOffsetLabel, kParamTimeOffsetLabel);
        param->setHint(kParamTimeOffsetHint);
        param->setDefault(0);
        param->setAnimates(true);
        //param->setIsSecret(true); // done in the plugin constructor
        page->addChild(*param);
    }
    
    /////////// Secret param set to true if the time domain was edited by the user
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTimeDomainUserEdited);
        param->setLabels(kParamTimeDomainUserEdited, kParamTimeDomainUserEdited, kParamTimeDomainUserEdited);
        param->setIsSecret(true); // always secret
        param->setDefault(false);
        param->setAnimates(false);
        page->addChild(*param);
    }

    ///////////Original frame range
    {
        OFX::Int2DParamDescriptor* param = desc.defineInt2DParam(kParamOriginalFrameRange);
        param->setLabels(kParamOriginalFrameRangeLabel, kParamOriginalFrameRangeLabel, kParamOriginalFrameRangeLabel);
        param->setDefault(kOfxFlagInfiniteMin, kOfxFlagInfiniteMax);
        param->setAnimates(true);
        param->setIsSecret(true); // always secret
        param->setIsPersistant(false);
        page->addChild(*param);
    }

    //////////Input proxy file
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamProxy);
        param->setLabels(kParamProxyLabel, kParamProxyLabel, kParamProxyLabel);
        param->setStringType(OFX::eStringTypeFilePath);
        param->setFilePathExists(true);
        param->setHint(kParamProxyHint);
        param->setAnimates(false);
        // in the Reader context, the script name must be kOfxImageEffectFileParamName, @see kOfxImageEffectContextReader
        param->setScriptName(kParamProxy);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    ////Proxy original scale
    {
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamOriginalProxyScale);
        param->setLabels(kParamOriginalProxyScaleLabel,
                         kParamOriginalProxyScaleLabel, kParamOriginalProxyScaleLabel);
        param->setDefault(1., 1.);
        param->setIsSecret(true); // always secret
        param->setEnabled(false);
        param->setHint(kParamOriginalProxyScaleHint);
        // param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        param->setAnimates(true);
        page->addChild(*param);
    }

    ////Proxy  scale threshold
    {
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamProxyThreshold);
        param->setLabels(kParamProxyThresholdLabel,
                         kParamProxyThresholdLabel, kParamProxyThresholdLabel);
        param->setDefault(1., 1.);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setEnabled(false);
        param->setHint(kParamOriginalProxyScaleHint);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        param->setAnimates(true);
        page->addChild(*param);
    }

    ///Enable custom proxy scale
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCustomProxyScale);
        param->setLabels(kParamCustomProxyScaleLabel, kParamCustomProxyScaleLabel, kParamCustomProxyScaleLabel);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setDefault(false);
        param->setHint(kParamCustomProxyScaleHint);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
    }

    //// File premult
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFilePremult);
        param->setLabels(kParamFilePremultLabel, kParamFilePremultLabel, kParamFilePremultLabel);
        param->setAnimates(true);
        param->setHint(kParamFilePremultHint);
        assert(param->getNOptions() == eImageOpaque);
        param->appendOption(premultString(eImageOpaque), kParamFilePremultOptionOpaqueHint);
        if (gHostSupportsRGBA && supportsRGBA) {
            assert(param->getNOptions() == eImagePreMultiplied);
            param->appendOption(premultString(eImagePreMultiplied), kParamFilePremultOptionPreMultipliedHint);
            assert(param->getNOptions() == eImageUnPreMultiplied);
            param->appendOption(premultString(eImageUnPreMultiplied), kParamFilePremultOptionUnPreMultipliedHint);
            param->setDefault(eImagePreMultiplied); // images should be premultiplied in a compositing context
        }
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    //// Output components
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputComponents);
        param->setLabels(kParamOutputComponentsLabel, kParamOutputComponentsLabel, kParamOutputComponentsLabel);
        param->setHint(kParamOutputComponentsHint);
        int i = 0;

        if (gHostSupportsRGBA && supportsRGBA) {
            gOutputComponentsMap[i] = ePixelComponentRGBA;
            ++i;
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentRGBA);
            param->appendOption(kParamOutputComponentsOptionRGBA);
        }
        if (gHostSupportsRGB && supportsRGB) {
            gOutputComponentsMap[i] = ePixelComponentRGB;
            ++i;
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentRGB);
            param->appendOption(kParamOutputComponentsOptionRGB);
        }
        if (gHostSupportsAlpha && supportsAlpha) {
            gOutputComponentsMap[i] = ePixelComponentAlpha;
            ++i;
            assert(gOutputComponentsMap[param->getNOptions()] == ePixelComponentAlpha);
            param->appendOption(kParamOutputComponentsOptionAlpha);
        }
        gOutputComponentsMap[i] = ePixelComponentNone;

        param->setDefault(0); // default to the first one available, i.e. the most chromatic
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }
    
    ///Frame rate
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFrameRate);
        param->setLabels(kParamFrameRateLabel, kParamFrameRateLabel, kParamFrameRateLabel);
        param->setHint(kParamFrameRateHint);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        desc.addClipPreferencesSlaveParam(*param);
        param->setEnabled(false);
        param->setDefault(24.);
        page->addChild(*param);
    }
    
    ///Custom FPS
    {
        BooleanParamDescriptor* param  = desc.defineBooleanParam(kParamCustomFps);
        param->setLabels(kParamCustomFpsLabel, kParamCustomFpsLabel, kParamCustomFpsLabel);
        param->setHint(kParamCustomFpsHint);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    return page;
}


void
GenericReaderDescribeInContextEnd(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  OFX::PageParamDescriptor* page,
                                  const char* inputSpaceNameDefault,
                                  const char* outputSpaceNameDefault)
{
    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, inputSpaceNameDefault, kParamInputSpaceLabel);
    GenericOCIO::describeInContextOutput(desc, context, page, outputSpaceNameDefault);
}


