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

#ifndef Io_GenericReader_h
#define Io_GenericReader_h

#include <ofxsImageEffect.h>

class SequenceParser;
class GenericOCIO;

/**
 * @brief A generic reader plugin, derive this to create a new reader for a specific file format.
 * This class propose to handle the common stuff among readers: 
 * - common params
 * - a tiny cache to speed-up the successive getRegionOfDefinition() calls
 * - a way to inform the host about the colour-space of the data.
 **/
class GenericReaderPlugin : public OFX::ImageEffect {
    
public:
    
    GenericReaderPlugin(OfxImageEffectHandle handle, const char* inputName, const char* outputName);
    
    virtual ~GenericReaderPlugin();
    
    /**
     * @brief Don't override this function, the GenericReaderPlugin class already does the rendering. The "decoding" of the frame
     * must be done by the pure virtual function decode(...) instead.
     **/
    void render(const OFX::RenderArguments &args);
    
    /**
     * @brief Don't override this. Basically this function will call getTimeDomainForVideoStream(...),
     * which your reader should implement to read from a video-stream the time range. 
     * If the file is not a video stream, the function getTimeDomainForVideoStream() should return false, indicating that
     * we're reading a sequence of images and that the host should get the time domain for us.
     **/
    bool getTimeDomain(OfxRangeD &range);
    
    /**
     * @brief Don't override this. If the pure virtual function areHeaderAndDataTied() returns true, this
     * function will call decode() to read the region of definition of the image and cache away the decoded image
     * into the _dstImg member.
     * If areHeaderAndDataTied() returns false instead, this function will call the virtual function
     * getFrameRegionOfDefinition() which should read the header of the image to only extract the region of
     * definition of the image.
     **/
    bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    
    /**
     * @brief You can override this to take actions in response to a param change. 
     * Make sure you call the base-class version of this function at the end: i.e:
     * 
     * void MyReader::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
     *      if (.....) {
     *      
     *      } else if(.....) {
     *
     *      } else {
     *          GenericReaderPlugin::changedParam(args,paramName);
     *      }
     * }
     **/
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
    /**
     * @brief Overriden to clear any OCIO cache. 
     * This function calls clearAnyCache() if you have any cache to clear.
     **/
    void purgeCaches(void);
    
protected:
    OFX::ChoiceParam* _missingFrameParam; //< what to do on missing frame

    void getCurrentFileName(std::string& filename);
    
private:
    
    /**
     * @brief Override if you want to do something when the input image/video file changed.
     * You shouldn't do any strong processing as this is called on the main thread and
     * the getRegionOfDefinition() and  decode() should open the file in a separate thread.
     **/
    virtual void onInputFileChanged(const std::string& /*newFile*/) {}
    
    /**
     * @brief Override to clear any cache you may have.
     **/
    virtual void clearAnyCache() {}
    
    
    /**
     * @brief Overload this function to exctract the region of definition out of the header
     * of the image targeted by the filename.
     **/
    virtual void getFrameRegionOfDefinition(const std::string& filename,OfxTime time,OfxRectD& rod) = 0;
    
    /**
     * @brief Override this function to actually decode the image contained in the file pointed to by filename.
     * If the file is a video-stream then you should decode the frame at the time given in parameters.
     * You must write the decoded image into dstImg. This function should convert the read pixels into the
     * bitdepth of the dstImg. You can inform the host of the bitdepth you support in the describe() function.
     * Note that many hosts work with linear colors and we intend that this function transfer from the
     * image file's color-space to linear. To help you do this you can use the color-space conversion
     * class (Lut) written for this purpose.
     * You can always skip the color-space conversion, but for all linear hosts it would produce either
     * false colors or sub-par performances in the case the end-user has to append a color-space conversion
     * effect her/himself.
     **/
    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, OFX::Image* dstImg) = 0;
    
    
    /**
     * @brief Override to indicate the time domain. Return false if you know that the
     * file isn't a video-stream, true when you can find-out the frame range.
     **/
    virtual bool getSequenceTimeDomain(const std::string& /*filename*/,OfxRangeD &/*range*/){ return false; }
    
    /**
     * @brief Called internally by getTimeDomain(...)
     **/
    bool getSequenceTimeDomainInternal(OfxRangeD& range);
    
    /**
     * @brief Used internally by the GenericReader.
     **/
    void timeDomainFromSequenceTimeDomain(OfxRangeD& range,bool mustSetFrameRange);
    
    /**
     * @brief Should return true if the file indicated by filename is a video-stream and not 
     * a single image file.
     **/
    virtual bool isVideoStream(const std::string& filename) = 0;
    
    /**
     * @brief compute the sequence/file time from time
     */
    double getSequenceTime(double t);

    /**
     * @brief Returns the filename of the image at the sequence time t.
     **/
    void getFilenameAtSequenceTime(double t, std::string &filename);
    
    

    OFX::Clip *_outputClip; //< Mandated output clip
    OFX::StringParam  *_fileParam; //< The input file
    
    OFX::IntParam* _firstFrame; //< the first frame in the sequence (clamped to the time domain)
    OFX::ChoiceParam* _beforeFirst;//< what to do before the first frame
    OFX::IntParam* _lastFrame; //< the last frame in the sequence (clamped to the time domain)
    OFX::ChoiceParam* _afterLast; //< what to do after the last frame
    
    OFX::ChoiceParam* _frameMode;//< do we use a time offset or an absolute starting frame
    OFX::IntParam* _timeOffset; //< the time offset applied to the sequence
    OFX::IntParam* _startingFrame; //< the starting frame of the sequence
    
    OFX::Int2DParam* _originalFrameRange; //< the original frame range computed the first time by getSequenceTimeDomainInternal
    
    GenericOCIO* _ocio;
    
    bool _settingFrameRange; //< true when getTimeDomainInternal is called with mustSetFrameRange = true
    
    SequenceParser* _sequenceParser; //< parser to extract the time domain

};


void GenericReaderDescribe(OFX::ImageEffectDescriptor &desc);
OFX::PageParamDescriptor* GenericReaderDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha);
void GenericReaderDescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor* page);

#define mDeclareReaderPluginFactory(CLASS, LOADFUNCDEF, UNLOADFUNCDEF,ISVIDEOSTREAM) \
  class CLASS : public OFX::PluginFactoryHelper<CLASS>                       \
  {                                                                     \
  public:                                                                \
    CLASS(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<CLASS>(id, verMaj, verMin){} \
    virtual void load() LOADFUNCDEF ;                                   \
    virtual void unload() UNLOADFUNCDEF ;                               \
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context); \
    bool isVideoStreamPlugin() const { return ISVIDEOSTREAM; }  \
    virtual void describe(OFX::ImageEffectDescriptor &desc);      \
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context); \
  }; 

#endif
