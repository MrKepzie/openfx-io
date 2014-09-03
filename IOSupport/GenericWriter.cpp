/*
 OFX GenericWriter plugin.
 A base class for all OpenFX-based encoders.
 
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

#include "GenericWriter.h"

#include <locale>
#include <sstream>
#include <cstring>
#include <algorithm>

#include "ofxsLog.h"
#include "ofxsCopier.h"

#ifdef OFX_EXTENSIONS_TUTTLE
#include <tuttle/ofxReadWrite.h>
#endif
#include "../SupportExt/ofxsFormatResolution.h"
#include "GenericOCIO.h"

#define kWriterGrouping "Image/Writers"

// in the Writer context, the script name must be "filename", @see kOfxImageEffectContextWriter
#define kWriterFileParamName "filename"
#define kWriterFileParamLabel "File"
#define kWriterFileParamHint \
"The output image sequence/video stream file(s). " \
"The string must match the following format: " \
"path/sequenceName###.ext where the number of " \
"# (hashes) will define the number of digits to append to each " \
"file. For example path/mySequence###.jpg will be translated to " \
"path/mySequence000.jpg, path/mySequence001.jpg, etc. " \
"By default the plugin will append as many digits as necessary (if you have 11 frames, " \
"there will be at least 2 digits). The file name may not contain any # (hash)."

#define kSupportsTiles 0
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 0 // Writers do not support render scale: all images must be rendered/written at full resolution
#define kRenderThreadSafety eRenderInstanceSafe

#define kWriterOutputFormatParamName "outputFormat"
#define kWriterOutputFormatParamLabel "Format"
#define kWriterOutputFormatHint "The output format to render"

#define kWriterOutputTypeParamName "formatType"
#define kWriterOutputTypeParamLabel "Format type"
#define kWriterOutputTypeParamHint "Whether to choose the input stream's format as output format or one from the drop-down menu"

#define kWriterFrameRangeChoiceParamName "frameRange"
#define kWriterFrameRangeChoiceParamLabel "Frame range"
#define kWriterFrameRangeChoiceParamHint "What frame range should be rendered."
#define kWriterFrameRangeChoiceParamOptionUnion "Union of input ranges"
#define kWriterFrameRangeChoiceParamOptionUnionHint "The union of all inputs frame ranges will be rendered."
#define kWriterFrameRangeChoiceParamOptionBounds "Timeline bounds"
#define kWriterFrameRangeChoiceParamOptionBoundsHint "The frame range delimited by the timeline bounds will be rendered."
#define kWriterFrameRangeChoiceParamOptionManual "Manual"
#define kWriterFrameRangeChoiceParamOptionManualHint "The frame range will be the one defined by the first frame and last frame parameters."

#define kWriterFirstFrameParamName "firstFrame"
#define kWriterFirstFrameParamLabel "First frame"

#define kWriterLastFrameParamName "lastFrame"
#define kWriterLastFrameParamLabel "Last frame"

GenericWriterPlugin::GenericWriterPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _inputClip(0)
, _outputClip(0)
, _fileParam(0)
, _frameRange(0)
, _firstFrame(0)
, _lastFrame(0)
, _outputFormatType(0)
, _outputFormat(0)
, _ocio(new GenericOCIO(this))
{
    _inputClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kWriterFileParamName);
    _frameRange = fetchChoiceParam(kWriterFrameRangeChoiceParamName);
    _firstFrame = fetchIntParam(kWriterFirstFrameParamName);
    _lastFrame = fetchIntParam(kWriterLastFrameParamName);
    
    _outputFormatType = fetchChoiceParam(kWriterOutputTypeParamName);
    _outputFormat = fetchChoiceParam(kWriterOutputFormatParamName);
    
    int frameRangeChoice;
    _frameRange->getValue(frameRangeChoice);
    double first,last;
    timeLineGetBounds(first,last);
    if (frameRangeChoice == 2) {
        _firstFrame->setIsSecret(false);
        _lastFrame->setIsSecret(false);
    } else {
        _firstFrame->setIsSecret(true);
        _lastFrame->setIsSecret(true);
    }
}

GenericWriterPlugin::~GenericWriterPlugin()
{
}


void
GenericWriterPlugin::getOutputFileNameAndExtension(OfxTime time, std::string& filename)
{
    _fileParam->getValueAtTime(time,filename);
    // filename = filenameFromPattern(filename, time);
    
    ///find out whether we support this extension...
    size_t sepPos = filename.find_last_of('.');
    if (sepPos == std::string::npos){ //we reached the start of the file, return false because we can't determine from the extension
        setPersistentMessage(OFX::Message::eMessageError, "", "Invalid file name");
        return;
    }
    
    std::string ext;
    size_t i = sepPos;
    ++i;//< bypass the '.' character
	std::locale loc;
    while(i < filename.size()){
        ext.append(1,std::tolower(filename.at(i),loc));
        ++i;
    }
    
#ifdef OFX_EXTENSIONS_TUTTLE
    try {
        bool found = false;
        int nExtensions = getPropertySet().propGetDimension(kTuttleOfxImageEffectPropSupportedExtensions);
        for (int i = 0; i < nExtensions; ++i) {
            std::string exti = getPropertySet().propGetString(kTuttleOfxImageEffectPropSupportedExtensions, i);
            if (exti == ext) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::string err("Unsupported file extension: ");
            err.append(ext);
            setPersistentMessage(OFX::Message::eMessageError, "", ext);
        }
    } catch (OFX::Exception::PropertyUnknownToHost &e) {
        // ignore exception
    }
#endif
    
    ////if the file extension corresponds to a video file, remove file digits that were
    ////added to the file path in order to write into the same file.
    if (!isImageFile(ext)) {
        ///find the position of the first digit
        size_t firstDigitPos = sepPos;
        --firstDigitPos;
		std::locale loc;
        while (firstDigitPos &&  std::isdigit(filename.at(firstDigitPos),loc)) {
            --firstDigitPos;
        }
        ++firstDigitPos;
        filename.erase(firstDigitPos, sepPos - firstDigitPos); //< erase the digits
    }

}

bool
GenericWriterPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                OFX::Clip * &/*identityClip*/,
                                double &/*identityTime*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    return false;
}

void
GenericWriterPlugin::render(const OFX::RenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if (!_inputClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    std::string filename;
    getOutputFileNameAndExtension(args.time, filename);
    
    std::auto_ptr<const OFX::Image> srcImg(_inputClip->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (srcImg->getRenderScale().x != args.renderScale.x ||
        srcImg->getRenderScale().y != args.renderScale.y ||
        srcImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }


    const void* srcPixelData = NULL;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    getImageData(srcImg.get(), &srcPixelData, &bounds, &pixelComponents, &bitDepth, &srcRowBytes);
    if (bitDepth != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    
    // The following (commented out) code is not fully-safe, because the same instance may be have
    // two threads running on the same area of the same frame, and the apply()
    // calls both read and write dstImg.
    // This results in colorspace conversion being applied several times.
    //
    //if (dstImg.get()) {
    //// do the color-space conversion on dstImg
    //getImageData(dstImg.get(), &pixelData, &bounds, &pixelComponents, &rowBytes);
    //_ocio->apply(args.time, args.renderWindow, pixelData, bounds, pixelComponents, rowBytes);
    //encode(filename, args.time, pixelData, bounds, pixelComponents, rowBytes);
    //}
    //
    // The only viable solution (below) is to do the conversion in a temporary space,
    // and finally copy the result.
    //
    // allocate
    int pixelBytes = getPixelBytes(pixelComponents, bitDepth);
    int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
    size_t memSize = (args.renderWindow.y2 - args.renderWindow.y1) * tmpRowBytes;
    OFX::ImageMemory mem(memSize,this);
    float *tmpPixelData = (float*)mem.lock();
    
    ///Set to black and transparant so that outside the portion defined by the image there's nothing
    std::memset(tmpPixelData, 0, memSize);
    
    ///Clip the render window to the bounds of the source image!
    OfxRectI renderWindowClipped;
    intersect(args.renderWindow, bounds, &renderWindowClipped);
    
    // copy the whole src image
    copyPixelData(renderWindowClipped, srcPixelData, bounds, pixelComponents, bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, bitDepth, tmpRowBytes);
    
    if (!_ocio->isIdentity(args.time)) {
        
        // do the color-space conversion
        _ocio->apply(args.time, renderWindowClipped, tmpPixelData, bounds, pixelComponents, tmpRowBytes);
        
    }
    // write theimage file
    encode(filename, args.time, tmpPixelData, args.renderWindow, pixelComponents, tmpRowBytes);
    // copy to dstImg if necessary
    if (_outputClip && _outputClip->isConnected()) {
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
        
        
        copyPixelData(args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, bitDepth, tmpRowBytes, dstImg.get());
    }
    
    // unlock (the OFX::ImageMemory destructor frees the memory)
    mem.unlock();
    
    clearPersistentMessage();
}


void
GenericWriterPlugin::beginSequenceRender(const OFX::BeginSequenceRenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    std::string filename;
    getOutputFileNameAndExtension(args.frameRange.min, filename);
    
    OfxRectD rod;
    getRegionOfDefinitionInternal(args.frameRange.min, rod);
    
    ////Since the generic writer doesn't support tiles and multi-resolution, the RoD is necesserarily the
    ////output image size.
    OfxRectI rodI;
    rodI.x1 = std::floor(rod.x1);
    rodI.y1 = std::floor(rod.y1);
    rodI.x2 = std::ceil(rod.x2);
    rodI.y2 = std::ceil(rod.y2);
    
    beginEncode(filename, rodI, args);
}


void
GenericWriterPlugin::endSequenceRender(const OFX::EndSequenceRenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    endEncode(args);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

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

    // make sure bit depths are sane
    if (srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

void
GenericWriterPlugin::copyPixelData(const OfxRectI& renderWindow,
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
        OFX::PixelCopier<float, 4, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        OFX::PixelCopier<float, 3, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        OFX::PixelCopier<float, 1, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } // switch
}

void
GenericWriterPlugin::getRegionOfDefinitionInternal(OfxTime time,OfxRectD& rod)
{
    
    int formatType;
    _outputFormatType->getValue(formatType);
    
    if (formatType == 0) {
        // use the default RoD
        rod = _inputClip->getRegionOfDefinition(time);
    } else {
        int formatIndex;
        _outputFormat->getValueAtTime(time, formatIndex);
        std::size_t w,h;
        double par;
        getFormatResolution((OFX::EParamFormat)formatIndex, &w, &h, &par);
        rod.x1 = rod.y1 = 0.;
        rod.x2 = w;
        rod.y2 = h;
    }
}

bool
GenericWriterPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    getRegionOfDefinitionInternal(args.time, rod);
    return true;
}

// override the roi call
void
GenericWriterPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                       OFX::RegionOfInterestSetter &rois)
{
    if (!kSupportsTiles) {
        // The effect requires full images to render any region
        OfxRectD srcRoI;

        if (_inputClip && _inputClip->isConnected()) {
            srcRoI = _inputClip->getRegionOfDefinition(args.time);
            rois.setRegionOfInterest(*_inputClip, srcRoI);
        }
    }
}


bool
GenericWriterPlugin::getTimeDomain(OfxRangeD &range)
{
    int choice;
    _frameRange->getValue(choice);
    if (choice == 0) {
        ///let the default be applied
        return false;
    } else if (choice == 1) {
        timeLineGetBounds(range.min, range.max);
        return true;
    } else {
        int first;
        _firstFrame->getValue(first);
        range.min = first;
        
        int last;
        _lastFrame->getValue(last);
        range.max = last;
        return true;
    }
}

void
GenericWriterPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kWriterFrameRangeChoiceParamName) {
        int choice;
        double first,last;
        timeLineGetBounds(first,last);
        _frameRange->getValue(choice);
        if (choice == 2) {
            _firstFrame->setIsSecret(false);
            _firstFrame->setValue(first);
            _lastFrame->setIsSecret(false);
            _lastFrame->setValue(last);
        } else {
            _firstFrame->setIsSecret(true);
            _lastFrame->setIsSecret(true);
        }
    } else if (paramName == kWriterFileParamName) {
        std::string filename;
        _fileParam->getValue(filename);

        ///let the derive class a chance to initialize any data structure it may need
        onOutputFileChanged(filename);
    } else if (paramName == kWriterOutputTypeParamName) {
        int type;
        _outputFormatType->getValue(type);
        if (type == 0) {
            _outputFormat->setIsSecret(true);
        } else {
            _outputFormat->setIsSecret(false);
        }
    }

    _ocio->changedParam(args, paramName);
}


void
GenericWriterPlugin::purgeCaches()
{
    clearAnyCache();
    _ocio->purgeCaches();
}



using namespace OFX;

/**
 * @brief Override this to describe the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describe(desc);
 **/
void
GenericWriterDescribe(OFX::ImageEffectDescriptor &desc)
{
    desc.setPluginGrouping(kWriterGrouping);
    
#ifdef OFX_EXTENSIONS_TUTTLE
    desc.addSupportedContext(OFX::eContextWriter);
#endif
    desc.addSupportedContext(OFX::eContextGeneral);

    // OCIO is only supported for float images.
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
PageParamDescriptor*
GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault)
{
    // create the mandated source clip
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
    srcClip->setSupportsTiles(kSupportsTiles);

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
    dstClip->setSupportsTiles(kSupportsTiles);//< we don't support tiles in output!

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    //////////Output file
    OFX::StringParamDescriptor* fileParam = desc.defineStringParam(kWriterFileParamName);
    fileParam->setLabels(kWriterFileParamLabel, kWriterFileParamLabel, kWriterFileParamLabel);
    fileParam->setStringType(OFX::eStringTypeFilePath);
    fileParam->setFilePathExists(false);
    fileParam->setHint(kWriterFileParamHint);
    // in the Writer context, the script name should be "filename", for consistency with the reader nodes @see kOfxImageEffectContextReader
    fileParam->setScriptName(kWriterFileParamName);
    fileParam->setAnimates(!isVideoStreamPlugin);
    desc.addClipPreferencesSlaveParam(*fileParam);

    page->addChild(*fileParam);

    //////////// Output type
    OFX::ChoiceParamDescriptor* outputType = desc.defineChoiceParam(kWriterOutputTypeParamName);
    outputType->setLabels(kWriterOutputTypeParamLabel, kWriterOutputTypeParamLabel, kWriterOutputTypeParamLabel);
    outputType->appendOption("Input stream format","Renders using for format the input stream's format.");
    outputType->appendOption("Fixed format","Renders using for format the format indicated by the " kWriterOutputFormatParamLabel " parameter.");
    outputType->setDefault(1);
    outputType->setAnimates(false);
    outputType->setHint(kWriterOutputTypeParamHint);
    outputType->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*outputType);
    
    //////////// Output format
    OFX::ChoiceParamDescriptor* outputFormat = desc.defineChoiceParam(kWriterOutputFormatParamName);
    outputFormat->setLabels(kWriterOutputFormatParamLabel, kWriterOutputFormatParamLabel, kWriterOutputFormatParamLabel);
    outputFormat->setAnimates(true);
    outputFormat->setHint(kWriterOutputFormatHint);
    outputFormat->appendOption(kParamFormatPCVideoLabel);
    outputFormat->appendOption(kParamFormatNTSCLabel);
    outputFormat->appendOption(kParamFormatPALLabel);
    outputFormat->appendOption(kParamFormatHDLabel);
    outputFormat->appendOption(kParamFormatNTSC169Label);
    outputFormat->appendOption(kParamFormatPAL169Label);
    outputFormat->appendOption(kParamFormat1kSuper35Label);
    outputFormat->appendOption(kParamFormat1kCinemascopeLabel);
    outputFormat->appendOption(kParamFormat2kSuper35Label);
    outputFormat->appendOption(kParamFormat2kCinemascopeLabel);
    outputFormat->appendOption(kParamFormat4kSuper35Label);
    outputFormat->appendOption(kParamFormat4kCinemascopeLabel);
    outputFormat->appendOption(kParamFormatSquare256Label);
    outputFormat->appendOption(kParamFormatSquare512Label);
    outputFormat->appendOption(kParamFormatSquare1kLabel);
    outputFormat->appendOption(kParamFormatSquare2kLabel);
    outputFormat->setDefault(3);
    page->addChild(*outputFormat);
    
    // insert OCIO parameters
    GenericOCIO::describeInContext(desc, context, page, inputSpaceNameDefault, outputSpaceNameDefault);

    ///////////Frame range choosal
    OFX::ChoiceParamDescriptor* frameRangeChoiceParam = desc.defineChoiceParam(kWriterFrameRangeChoiceParamName);
    frameRangeChoiceParam->setLabels(kWriterFrameRangeChoiceParamLabel, kWriterFrameRangeChoiceParamLabel, kWriterFrameRangeChoiceParamLabel);
    frameRangeChoiceParam->setHint(kWriterFrameRangeChoiceParamHint);
    frameRangeChoiceParam->appendOption(kWriterFrameRangeChoiceParamOptionUnion, kWriterFrameRangeChoiceParamOptionUnionHint);
    frameRangeChoiceParam->appendOption(kWriterFrameRangeChoiceParamOptionBounds, kWriterFrameRangeChoiceParamOptionBoundsHint);
    frameRangeChoiceParam->appendOption(kWriterFrameRangeChoiceParamOptionManual, kWriterFrameRangeChoiceParamOptionManualHint);
    frameRangeChoiceParam->setAnimates(true);
    frameRangeChoiceParam->setDefault(0);
    page->addChild(*frameRangeChoiceParam);
    
    /////////////First frame
    OFX::IntParamDescriptor* firstFrameParam = desc.defineIntParam(kWriterFirstFrameParamName);
    firstFrameParam->setLabels(kWriterFirstFrameParamLabel, kWriterFirstFrameParamLabel, kWriterFirstFrameParamLabel);
    firstFrameParam->setIsSecret(true);
    firstFrameParam->setAnimates(true);
    page->addChild(*firstFrameParam);

    ////////////Last frame
    OFX::IntParamDescriptor* lastFrameParam = desc.defineIntParam(kWriterLastFrameParamName);
    lastFrameParam->setLabels(kWriterLastFrameParamLabel, kWriterLastFrameParamLabel, kWriterLastFrameParamLabel);
    lastFrameParam->setIsSecret(true);
    lastFrameParam->setAnimates(true);
    page->addChild(*lastFrameParam);


    return page;
}

void
GenericWriterDescribeInContextEnd(OFX::ImageEffectDescriptor &/*desc*/, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor* /*page*/)
{
}

