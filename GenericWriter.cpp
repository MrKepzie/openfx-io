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

#include "ofxsProcessing.H"
#include "ofxsLog.h"

#ifdef OFX_EXTENSIONS_NATRON
#include "IOExtensions.h"
#endif

#define kWriterFileParamName "file"
#define kWriterRenderParamName "render"
#define kWriterFrameRangeChoiceParamName "frameRange"
#define kWriterFirstFrameParamName "firstFrame"
#define kWriterLastFrameParamName "lastFrame"

#ifdef IO_USING_OCIO
#define kWriterOCCIOConfigFileParamName "WriterOCCIOConfigFileParamName"
#define kWriterOutputColorSpaceParamName "outputColorSpace"
#endif

#ifdef OFX_EXTENSIONS_NATRON
static bool gHostIsNatron = true;
#endif

// Base class for the RGBA and the Alpha processor
class CopierBase : public OFX::ImageProcessor {
    protected :
    OFX::Image *_srcImg;
    public :
    /** @brief no arg ctor */
    CopierBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    {
    }
    
    /** @brief set the src image */
    void setSrcImg(OFX::Image *v) {_srcImg = v;}
};

// template to do the RGBA processing
template <class PIX, int nComponents>
class ImageCopier : public CopierBase {
    public :
    // ctor
    ImageCopier(OFX::ImageEffect &instance)
    : CopierBase(instance)
    {}
    
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for(int y = procWindow.y1; y < procWindow.y2; y++) {
            if(_effect.abort()) break;
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            for(int x = procWindow.x1; x < procWindow.x2; x++) {
                
                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                
                // do we have a source image to scale up
                if(srcPix) {
                    for(int c = 0; c < nComponents; c++) {
                        dstPix[c] = srcPix[c];
                    }
                }
                else {
                    // no src pixel here, be black and transparent
                    for(int c = 0; c < nComponents; c++) {
                        dstPix[c] = 0;
                    }
                }
                
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

GenericWriterPlugin::GenericWriterPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _inputClip(0)
, _outputClip(0)
, _fileParam(0)
, _frameRange(0)
, _firstFrame(0)
, _lastFrame(0)
#ifdef IO_USING_OCIO
, _occioConfigFile(0)
, _outputColorSpace(0)
#endif
{
    _inputClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kWriterFileParamName);
    _frameRange = fetchChoiceParam(kWriterFrameRangeChoiceParamName);
    _firstFrame = fetchIntParam(kWriterFirstFrameParamName);
    _lastFrame = fetchIntParam(kWriterLastFrameParamName);
    
#ifdef IO_USING_OCIO
    _occioConfigFile = fetchStringParam(kWriterOCCIOConfigFileParamName);
    _outputColorSpace = fetchChoiceParam(kWriterOutputColorSpaceParamName);
#endif
}

GenericWriterPlugin::~GenericWriterPlugin(){
    
}

static std::string filenameFromPattern(const std::string& pattern,int frameIndex) {
    std::string ret = pattern;
    int lastDot = pattern.find_last_of('.');
    if(lastDot == std::string::npos){
        ///the filename has not extension, return an empty str
        return "";
    }
    
    std::stringstream fStr;
    fStr << frameIndex;
    std::string frameIndexStr = fStr.str();
    int lastPos = pattern.find_last_of('#');
    
    if (lastPos == std::string::npos) {
        ///the filename has no #, just put the digits between etxension and path
        ret.insert(lastDot, frameIndexStr);
        return pattern;
    }
    
    int nSharpChar = 0;
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



void GenericWriterPlugin::render(const OFX::RenderArguments &args){

    
    std::string filename;
    _fileParam->getValue(filename);
    filename = filenameFromPattern(filename, args.time);
    
    ///find out whether we support this extension...
    size_t sepPos = filename.find_last_of('.');
    if(sepPos == std::string::npos){ //we reached the start of the file, return false because we can't determine from the extension
        setPersistentMessage(OFX::Message::eMessageError, "", "Invalid file name");
        return;
    }
    size_t i = sepPos;
    ++i;//< bypass the '.' character
    std::string ext;
	std::locale loc;
    while(i < filename.size()){
        ext.append(1,std::tolower(filename.at(i),loc));
        ++i;
    }

    
    
    bool found = false;
    std::vector<std::string> supportedExtensions;
    supportedFileFormats(&supportedExtensions);
    for (unsigned int i = 0; i < supportedExtensions.size(); ++i) {
        if (supportedExtensions[i] == ext) {
            found = true;
            break;
        }
    }
    if(!found){
        std::string err("Unsupported file extension: ");
        err.append(ext);
        setPersistentMessage(OFX::Message::eMessageError, "", ext);
    }
    
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
    
    OFX::Image* srcImg = _inputClip->fetchImage(args.time);
    ////copy the image if the output clip is connected!
    if (_outputClip->isConnected()) {
        // instantiate the render code based on the pixel depth of the dst clip

        std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImage(args.time));
        
        OFX::BitDepthEnum       dstBitDepth    = _outputClip->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = _outputClip->getPixelComponents();
        
        // do the rendering
        if(dstComponents == OFX::ePixelComponentRGBA) {
            switch(dstBitDepth) {
                case OFX::eBitDepthUByte : {
                    ImageCopier<unsigned char, 4> fred(*this);
                    setupAndProcess(fred, args,srcImg,dstImg.get());
                }
                    break;
                    
                case OFX::eBitDepthUShort : {
                    ImageCopier<unsigned short, 4> fred(*this);
                    setupAndProcess(fred, args,srcImg,dstImg.get());
                }
                    break;
                    
                case OFX::eBitDepthFloat : {
                    ImageCopier<float, 4> fred(*this);
                    setupAndProcess(fred, args,srcImg,dstImg.get());
                }
                    break;
                default :
                    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
            }
        }
        else {
            switch(dstBitDepth) {
                case OFX::eBitDepthUByte : {
                    ImageCopier<unsigned char, 1> fred(*this);
                    setupAndProcess(fred, args,srcImg,dstImg.get());
                }
                    break;
                    
                case OFX::eBitDepthUShort : {
                    ImageCopier<unsigned short, 1> fred(*this);
                    setupAndProcess(fred, args,srcImg,dstImg.get());
                }
                    break;
                    
                case OFX::eBitDepthFloat : {
                    ImageCopier<float, 1> fred(*this);
                    setupAndProcess(fred, args,srcImg,dstImg.get());
                }                          
                    break;
                default :
                    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
            }
        } // switch
    }
    

    ///do the color-space conversion
#ifdef IO_USING_OCIO
    try
    {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
        int colorSpaceIndex;
        _outputColorSpace->getValue(colorSpaceIndex);
        const char* inputName = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName();
        const char * outputName = config->getColorSpaceNameByIndex(colorSpaceIndex);
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        OCIO::ConstProcessorRcPtr proc = config->getProcessor(context, inputName, outputName);
        
        OfxRectI rod = srcImg->getRegionOfDefinition();
        OCIO::PackedImageDesc img((float*)srcImg->getPixelAddress(0, 0),rod.x2 - rod.x1,rod.y2 - rod.y1,4);
        proc->apply(img);
    }
    catch(OCIO::Exception &e)
    {
        setPersistentMessage(OFX::Message::eMessageError, "", e.what());
    }
#endif
    
    ///and call the plug-in specific encode function.
    encode(filename, args.time, srcImg);
    
    delete srcImg;

}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// make sure components are sane
static void
checkComponents(const OFX::Image &src,
                OFX::BitDepthEnum dstBitDepth,
                OFX::PixelComponentEnum dstComponents)
{
    OFX::BitDepthEnum      srcBitDepth     = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();
    
    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
        throw int(1); // HACK!! need to throw an sensible exception here!
}

void GenericWriterPlugin::setupAndProcess(CopierBase & processor, const OFX::RenderArguments &args,OFX::Image* srcImg,OFX::Image* dstImg){
    

    OFX::BitDepthEnum          dstBitDepth    = dstImg->getPixelDepth();
    OFX::PixelComponentEnum    dstComponents  = dstImg->getPixelComponents();
    
    
    // make sure bit depths are sane
    if(srcImg) checkComponents(*srcImg, dstBitDepth, dstComponents);
    
    // set the images
    processor.setDstImg(dstImg);
    processor.setSrcImg(srcImg);
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();

}

bool GenericWriterPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod){
    
    ///get the RoD of the output clip
    rod = _outputClip->getRegionOfDefinition(args.time);
    return true;
}


bool GenericWriterPlugin::getTimeDomain(OfxRangeD &range){
    int choice;
    _frameRange->getValue(choice);
    if(choice == 0){
        ///let the default be applied
        return false;
    }else if(choice == 1){
        timeLineGetBounds(range.min, range.max);
        return true;
    }else{
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
    }
#ifdef IO_USING_OCIO
    else if ( paramName == kWriterOCCIOConfigFileParamName ) {
        std::string filename;
        _occioConfigFile->getValue(filename);
        
        if (filename.empty()) {
            return;
        }
        
        std::vector<std::string> colorSpaces;
        int defaultIndex;
        OCIO_OFX::openOCIOConfigFile(&colorSpaces, &defaultIndex,filename.c_str());
        
        _outputColorSpace->resetOptions();
        for (unsigned int i = 0; i < colorSpaces.size(); ++i) {
            _outputColorSpace->appendOption(colorSpaces[i]);
        }
        if (defaultIndex < (int)colorSpaces.size()) {
            _outputColorSpace->setValue(defaultIndex);
        }
    }
#endif
    

}

#ifdef IO_USING_OCIO
namespace OCIO_OFX {
    void openOCIOConfigFile(std::vector<std::string>* colorSpaces,int* defaultColorSpaceIndex,const char* filename,std::string occioRoleHint)
    {
        *defaultColorSpaceIndex = 0;
        if (occioRoleHint.empty()) {
            occioRoleHint = std::string(OCIO::ROLE_SCENE_LINEAR);
        }
        try
        {
            OCIO::ConstConfigRcPtr config;
            if (filename) {
                config = OCIO::Config::CreateFromFile(filename);
            } else {
                config = OCIO::Config::CreateFromEnv();
            }
            OCIO::SetCurrentConfig(config);
            
            OCIO::ConstColorSpaceRcPtr defaultcs = config->getColorSpace(occioRoleHint.c_str());
            if(!defaultcs){
                throw std::runtime_error("ROLE_COMPOSITING_LOG not defined.");
            }
            std::string defaultColorSpaceName = defaultcs->getName();
            
            for(int i = 0; i < config->getNumColorSpaces(); ++i)
            {
                std::string csname = config->getColorSpaceNameByIndex(i);
                colorSpaces->push_back(csname);
                
                if(csname == defaultColorSpaceName)
                {
                    *defaultColorSpaceIndex = i;
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
        
        
    }
}
#endif

using namespace OFX;

#ifdef IO_USING_OCIO
void GenericWriterPluginFactory::getOutputColorSpace(std::string& ocioRole) const { ocioRole = std::string(OCIO::ROLE_SCENE_LINEAR); }
#endif

/**
 * @brief Override this to describe the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describe(desc);
 **/
void GenericWriterPluginFactory::describe(OFX::ImageEffectDescriptor &desc){
    desc.setPluginGrouping("Image/WriteOFX");
    
    desc.addSupportedContext(OFX::eContextFilter);
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
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(false); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
    
#ifdef OFX_EXTENSIONS_NATRON
    // to check if the host is Natron-compatible, we could rely on gHostDescription.hostName,
    // but we prefer checking if the host has the right properties, in case another host implements
    // these extensions
    try {
        std::vector<std::string> fileFormats;
        supportedFileFormats(&fileFormats);
        for (unsigned int i = 0; i < fileFormats.size(); ++i) {
            desc.getPropertySet().propSetString(kNatronImageEffectPropFormats, fileFormats[i], i,true);
        }
    } catch (const OFX::Exception::PropertyUnknownToHost &e) {
        // the host is does not implement Natron extensions
        gHostIsNatron = false;
    }
    OFX::Log::warning(!gHostIsNatron, "WriteOFX: Host does not implement Natron extensions.");
#endif
    
    describeWriter(desc);
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
void GenericWriterPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setSupportsTiles(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
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
    fileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*fileParam);
#ifdef OFX_EXTENSIONS_NATRON
    if (gHostIsNatron) {
        fileParam->setFilePathIsImage(true);
        fileParam->setFilePathIsOutput(true);
    }
#endif
    page->addChild(*fileParam);

    ///////////Frame range choosal
    OFX::ChoiceParamDescriptor* frameRangeChoiceParam = desc.defineChoiceParam(kWriterFrameRangeChoiceParamName);
    frameRangeChoiceParam->setLabels("Frame range", "Frame range", "Frame range");
    frameRangeChoiceParam->appendOption("Inputs union","The union of all inputs frame ranges will be rendered.");
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
    page->addChild(*firstFrameParam);

    ////////////Last frame
    OFX::IntParamDescriptor* lastFrameParam = desc.defineIntParam(kWriterLastFrameParamName);
    lastFrameParam->setLabels("Last frame", "Last frame", "Last frame");
    lastFrameParam->setIsSecret(true);
    page->addChild(*lastFrameParam);

    ///////////Render button
    OFX::PushButtonParamDescriptor* renderParam = desc.definePushButtonParam(kWriterRenderParamName);
    renderParam->setLabels("Render","Render","Render");
    renderParam->setHint("Starts rendering all the frame range.");
#ifdef OFX_EXTENSIONS_NATRON
    if (gHostIsNatron) {
        renderParam->setAsRenderButton();
    }
#endif
    page->addChild(*renderParam);
    
#ifdef IO_USING_OCIO
    ////////// OCIO config file
    OFX::StringParamDescriptor* occioConfigFileParam = desc.defineStringParam(kWriterOCCIOConfigFileParamName);
    occioConfigFileParam->setLabels("OCIO config file", "OCIO config file", "OCIO config file");
    occioConfigFileParam->setStringType(OFX::eStringTypeFilePath);
    occioConfigFileParam->setHint("The file to read the OpenColorIO config from.");
    occioConfigFileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*occioConfigFileParam);
    
    ///////////Output Color-space
    OFX::ChoiceParamDescriptor* outputColorSpaceParam = desc.defineChoiceParam(kWriterOutputColorSpaceParamName);
    outputColorSpaceParam->setLabels("Output color-space", "Output color-space", "Output color-space");
    outputColorSpaceParam->setHint("Output data will be in this color-space.");
    outputColorSpaceParam->setAnimates(false);
    page->addChild(*outputColorSpaceParam);
    
    ///read the default config pointed to by the env var OCIO
    std::vector<std::string> colorSpaces;
    int defaultIndex;
    std::string defaultOcioRole;
    getOutputColorSpace(defaultOcioRole);
    OCIO_OFX::openOCIOConfigFile(&colorSpaces, &defaultIndex,NULL,defaultOcioRole);
    
    for (unsigned int i = 0; i < colorSpaces.size(); ++i) {
        outputColorSpaceParam->appendOption(colorSpaces[i]);
    }
    if (defaultIndex < (int)colorSpaces.size()) {
        outputColorSpaceParam->setDefault(defaultIndex);
    }
    
#endif
    
    describeWriterInContext(desc, context, page);
}
