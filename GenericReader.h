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

#ifndef Io_GenericReader_h
#define Io_GenericReader_h

#include <ofxsImageEffect.h>
#include <ofxsMultiThread.h>
#include "../include/ofxsProcessing.H"






class GenericReaderPlugin : public OFX::ImageEffect {
    
public:
    
    GenericReaderPlugin(OfxImageEffectHandle handle);
    
    /* Override the render */
    void render(const OFX::RenderArguments &/*args*/){}
    
    /* override the time domain action, only for the general context */
    bool getTimeDomain(OfxRangeD &range);
    
    bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
    /* Override to decode the image*/
    virtual void decode(const std::string& filename,OfxTime time,OFX::Image* dstImg,const OfxRectI* renderWindow = NULL) = 0;
    
protected:
    
    /* Override to indicate whether the file targeted by the filename is a video file*/
    virtual bool isVideoStream(const std::string& filename) const = 0;
    
    /* Override to indicate the time domain of a video stream */
    virtual bool getTimeDomainForVideoStream(OfxRangeD &/*range*/){return false;}
    
    /* Override to indicate whether a frame needs to be decoded entirely to extract only its
     meta-data (i.e: bitdepth & image bounds) */
    virtual bool areHeaderAndDataTied() const = 0;
    
    virtual void getFrameRegionOfDefinition(const std::string& /*filename*/,OfxRectD& rod){}
    
    
    OFX::Clip *_outputClip; //< Mandated output clip
    OFX::StringParam  *_fileParam; //< The input file
    OFX::IntParam *_timeOffsetParam;//< the time offset to apply
    OFX::Int2DParam* _frameRangeParam; //< the frame range to restrain this param too
    
private:
    
    /**
     * @brief A map of pointers to images read.
     * This is used when a single call to getRegionOfDefinition() would need to
     * decode entirely a frame to avoid decoding the frame multiple times.
     * If areHeaderAndDataTied() returns true the the first call to
     * getRegionOfDefinition() will set the pointer, and the last call to
     * render() will flush it.
     **/
    typedef std::map<OfxTime,OFX::Image*> FrameCache;
    FrameCache _frameCache;

};

//class ReaderProcessorBase : public OFX::ImageProcessor {
//public:
//    
//    ReaderProcessor(GenericReaderPlugin& instance,const std::string& filename,OfxTime time)
//    : OFX::ImageProcessor(instance)
//    
//}

template <class PIX, int nComponents>
class ReaderProcessor : public OFX::ImageProcessor {
    
    GenericReaderPlugin& _instance;
    std::string _filename;
    OfxTime _time;
    
public :
    /** @brief no arg ctor */
    ReaderProcessor(GenericReaderPlugin& instance,const std::string& filename,OfxTime time)
    : OFX::ImageProcessor(instance)
    , _instance(instance)
    , _filename(filename)
    , _time(time)
    {
    }
    
    virtual void multiThreadProcessImages(OfxRectI window){
        ///call the plugin's decoding function
        _instance.decode(_filename,_time, _dstImg, &window);
    }
    
    
};


namespace OFX
{
    namespace Plugin
    {
        /* Call this in all describeInContext() functions of the readers */
        void defineGenericReaderParamsInContext(OFX::ImageEffectDescriptor& desc,OFX::ContextEnum context);
    };
};

#endif
