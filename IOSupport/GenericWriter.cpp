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

#include "GenericOCIO.h"

// in the Writer context, the script name must be "filename", @see kOfxImageEffectContextWriter
#define kWriterFileParamName "filename"
#define kWriterFrameRangeChoiceParamName "frameRange"
#define kWriterFirstFrameParamName "firstFrame"
#define kWriterLastFrameParamName "lastFrame"

GenericWriterPlugin::GenericWriterPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _inputClip(0)
, _outputClip(0)
, _fileParam(0)
, _frameRange(0)
, _firstFrame(0)
, _lastFrame(0)
, _ocio(new GenericOCIO(this))
{
    _inputClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kWriterFileParamName);
    _frameRange = fetchChoiceParam(kWriterFrameRangeChoiceParamName);
    _firstFrame = fetchIntParam(kWriterFirstFrameParamName);
    _lastFrame = fetchIntParam(kWriterLastFrameParamName);
}

GenericWriterPlugin::~GenericWriterPlugin()
{
    delete _ocio;
}

static std::string filenameFromPattern(const std::string& pattern,int frameIndex) {
    std::string ret = pattern;
    size_t lastDot = pattern.find_last_of('.');
    if(lastDot == std::string::npos){
        ///the filename has not extension, return an empty str
        return "";
    }
    
    std::stringstream fStr;
    fStr << frameIndex;
    std::string frameIndexStr = fStr.str();
    size_t lastPos = pattern.find_last_of('#');
    
    if (lastPos == std::string::npos) {
        ///the filename has no #, just put the digits between etxension and path
        ret.insert(lastDot, frameIndexStr);
        return pattern;
    }
    
    size_t nSharpChar = 0;
    int i = lastDot;
    --i; //< char before '.'
    while (i >= 0 && pattern[i] == '#') {
        --i;
        ++nSharpChar;
    }
    
    int prepending0s = nSharpChar > frameIndexStr.size() ? nSharpChar - frameIndexStr.size() : 0;
    
    //remove all ocurrences of the # char
    ret.erase(std::remove(ret.begin(), ret.end(), '#'),ret.end());
    
    //insert prepending zeroes
    std::string zeroesStr;
    for (int j = 0; j < prepending0s; ++j) {
        zeroesStr.push_back('0');
    }
    frameIndexStr.insert(0,zeroesStr);

    //refresh the last '.' position
    lastDot = ret.find_last_of('.');
    
    ret.insert(lastDot, frameIndexStr);
    return ret;
}



void GenericWriterPlugin::render(const OFX::RenderArguments &args)
{
    if (!_inputClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    std::string filename;
    _fileParam->getValue(filename);
    filename = filenameFromPattern(filename, args.time);
    
    ///find out whether we support this extension...
    size_t sepPos = filename.find_last_of('.');
    if(sepPos == std::string::npos){ //we reached the start of the file, return false because we can't determine from the extension
        setPersistentMessage(OFX::Message::eMessageError, "", "Invalid file name");
        return;
    }
    clearPersistentMessage();
    size_t i = sepPos;
    ++i;//< bypass the '.' character
    std::string ext;
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
    if(!isImageFile(ext)){
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
    
    std::auto_ptr<const OFX::Image> srcImg(_inputClip->fetchImage(args.time));
    if (!srcImg.get()) {
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
    const float* srcPixelDataF = (const float*)srcPixelData;
    if (_ocio->isIdentity()) {
        // no colorspace conversion, just encode the source image
        // we ealways encode the whole input image, regardless of args.renderWindow
        encode(filename, args.time, srcPixelDataF, bounds, pixelComponents, srcRowBytes);
        // copy to dstImg if necessary
        if (_outputClip && _outputClip->isConnected()) {
            std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImage(args.time));
            assert(dstImg.get());
            copyPixelData(args.renderWindow, srcPixelData, bounds, pixelComponents, bitDepth, srcRowBytes, dstImg.get());
        }
    } else {
        // The following (commented out) code is not fully-safe, because the same instance may be have
        // two threads running on the same area of the same frame, and the apply()
        // calls both read and write dstImg.
        // This results in colorspace conversion being applied several times.
        //
        //if (dstImg.get()) {
        //// do the color-space conversion on dstImg
        //getImageData(dstImg.get(), &pixelData, &bounds, &pixelComponents, &rowBytes);
        //_ocio->apply(args.renderWindow, pixelData, bounds, pixelComponents, rowBytes);
        //encode(filename, args.time, pixelData, bounds, pixelComponents, rowBytes);
        //}
        //
        // The only viable solution (below) is to do the conversion in a temporary space,
        // and finally copy the result.
        //
        // allocate
        int pixelBytes = getPixelBytes(pixelComponents, bitDepth);
        int tmpRowBytes = (bounds.x2-bounds.x1) * pixelBytes;
        size_t memSize = (bounds.y2-bounds.y1) * tmpRowBytes;
        OFX::ImageMemory mem(memSize,this);
        float *tmpPixelData = (float*)mem.lock();
        // copy the whole image
        copyPixelData(bounds, srcPixelData, bounds, pixelComponents, bitDepth, srcRowBytes, tmpPixelData, bounds, pixelComponents, bitDepth, tmpRowBytes);

        // do the color-space conversion
        _ocio->apply(args.renderWindow, tmpPixelData, bounds, pixelComponents, tmpRowBytes);
        // write theimage file
        encode(filename, args.time, tmpPixelData, bounds, pixelComponents, tmpRowBytes);
        // copy to dstImg if necessary
        if (_outputClip && _outputClip->isConnected()) {
            std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImage(args.time));
            assert(dstImg.get());
            copyPixelData(args.renderWindow, tmpPixelData, bounds, pixelComponents, bitDepth, tmpRowBytes, dstImg.get());
        }

        // unlock (the OFX::ImageMemory destructor frees the memory)
        mem.unlock();
    }
    clearPersistentMessage();
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a copy processor */
static void setupAndCopy(OFX::PixelProcessorFilterBase & processor,
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
    if(srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
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

void GenericWriterPlugin::copyPixelData(const OfxRectI& renderWindow,
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
    if(dstPixelComponents == OFX::ePixelComponentRGBA) {
        PixelCopier<float, 4> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } else if(dstPixelComponents == OFX::ePixelComponentRGB) {
        PixelCopier<float, 3> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }  else if(dstPixelComponents == OFX::ePixelComponentAlpha) {
        PixelCopier<float, 1> fred(*this);
        setupAndCopy(fred, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    } // switch
}

bool GenericWriterPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod){
    
    ///get the RoD of the input clip
    rod = _inputClip->getRegionOfDefinition(args.time);
    return true;
}


bool GenericWriterPlugin::getTimeDomain(OfxRangeD &range){
    int choice;
    _frameRange->getValue(choice);
    if (choice == 0) {
        ///let the default be applied
        return false;
    } else if(choice == 1) {
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

void GenericWriterPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName){
    if(paramName == kWriterFrameRangeChoiceParamName){
        int choice;
        double first,last;
        timeLineGetBounds(first,last);
        _frameRange->getValue(choice);
        if(choice == 2){
            _firstFrame->setIsSecret(false);
            _firstFrame->setValue(first);
            _lastFrame->setIsSecret(false);
            _lastFrame->setValue(last);
        }else{
            _firstFrame->setIsSecret(true);
            _lastFrame->setIsSecret(true);
        }
    } else if (paramName == kWriterFileParamName) {
        std::string filename;
        _fileParam->getValue(filename);

        ///let the derive class a chance to initialize any data structure it may need
        onOutputFileChanged(filename);
    }

    _ocio->changedParam(args, paramName);
}


void GenericWriterPlugin::purgeCaches() {
    clearAnyCache();
    _ocio->purgeCaches();
}



using namespace OFX;

/**
 * @brief Override this to describe the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describe(desc);
 **/
void GenericWriterDescribe(OFX::ImageEffectDescriptor &desc){
    desc.setPluginGrouping("Image/WriteOFX");
    
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
    desc.setSupportsMultiResolution(false);
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(false); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
PageParamDescriptor* GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault)
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
    srcClip->setSupportsTiles(false);

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
    dstClip->setSupportsTiles(false);//< we don't support tiles in output!

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    //////////Output file
    OFX::StringParamDescriptor* fileParam = desc.defineStringParam(kWriterFileParamName);
    fileParam->setLabels("File", "File", "File");
    fileParam->setStringType(OFX::eStringTypeFilePath);
    fileParam->setHint("The output image sequence/video stream file(s)."
                       "The string must match the following format: "
                       "path/sequenceName###.ext where the number of"
                       " # characters will define the number of digits to append to each"
                       " file. For example path/mySequence###.jpg will be translated to"
                       " path/mySequence000.jpg, path/mySequence001.jpg, etc..."
                       " By default the plugin will append digits on demand (i.e: if you have 11 frames"
                       " there will be 2 digits). You don't even need to provide the # character.");
    // in the Writer context, the script name should be "filename", for consistency with the reader nodes @see kOfxImageEffectContextReader
    fileParam->setScriptName(kWriterFileParamName);
    fileParam->setAnimates(!isVideoStreamPlugin);
    fileParam->setFilePathExists(false);
    desc.addClipPreferencesSlaveParam(*fileParam);

    page->addChild(*fileParam);

    // insert OCIO parameters
    GenericOCIO::describeInContext(desc, context, page, inputSpaceNameDefault, outputSpaceNameDefault);

    ///////////Frame range choosal
    OFX::ChoiceParamDescriptor* frameRangeChoiceParam = desc.defineChoiceParam(kWriterFrameRangeChoiceParamName);
    frameRangeChoiceParam->setLabels("Frame range", "Frame range", "Frame range");
    frameRangeChoiceParam->appendOption("Union of input ranges","The union of all inputs frame ranges will be rendered.");
    frameRangeChoiceParam->appendOption("Timeline bounds","The frame range delimited by the timeline bounds will be rendered.");
    frameRangeChoiceParam->appendOption("Manual","The frame range will be the one defined by the first frame and last frame parameters.");
    frameRangeChoiceParam->setAnimates(false);
    frameRangeChoiceParam->setHint("What frame range should be rendered.");
    frameRangeChoiceParam->setDefault(0);
    page->addChild(*frameRangeChoiceParam);
    
    /////////////First frame
    OFX::IntParamDescriptor* firstFrameParam = desc.defineIntParam(kWriterFirstFrameParamName);
    firstFrameParam->setLabels("First frame", "First frame", "First frame");
    firstFrameParam->setIsSecret(true);
    firstFrameParam->setAnimates(false);
    page->addChild(*firstFrameParam);

    ////////////Last frame
    OFX::IntParamDescriptor* lastFrameParam = desc.defineIntParam(kWriterLastFrameParamName);
    lastFrameParam->setLabels("Last frame", "Last frame", "Last frame");
    lastFrameParam->setIsSecret(true);
    lastFrameParam->setAnimates(false);
    page->addChild(*lastFrameParam);


    return page;
}

void GenericWriterDescribeInContextEnd(OFX::ImageEffectDescriptor &/*desc*/, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor* /*page*/)
{
}

