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
 
 */
#ifndef Io_GenericWriter_h
#define Io_GenericWriter_h

#include <ofxsImageEffect.h>

#include "OCIO.h"


class CopierBase;

/**
 * @brief A generic writer plugin, derive this to create a new writer for a specific file format.
 * This class propose to handle the common stuff among writers:
 * - common params
 * - a way to inform the host about the colour-space of the data.
 **/
class GenericWriterPlugin : public OFX::ImageEffect {
    
public:
    
    GenericWriterPlugin(OfxImageEffectHandle handle);
    
    virtual ~GenericWriterPlugin();
    
    
    /**
     * @brief Don't override this function, the GenericWriterPlugin class already does the rendering. The "encoding" of the frame
     * must be done by the pure virtual function encode(...) instead.
     * The render function also copies the image from the input clip to the output clip (only if the effect is connected downstream)
     * in order to be able to branch this effect in the middle of an effect tree.
     **/
    void render(const OFX::RenderArguments &args);
    
    /**
     * @brief Don't override this. It returns the projects region of definition.
     **/
    bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    /**
     * @brief Don't override this. It returns the frame range to render.
     **/
    bool getTimeDomain(OfxRangeD &range);
    
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
     * @brief Should append in formats the list of the format this plug-in can encode.
     * For example "png" , "jpg" , etc...
     * This function should return the same as the named-alike function in the factory.
     **/
    virtual void supportedFileFormats(std::vector<std::string>* formats) const = 0;
    
protected:
    
    /**
     * @brief Override this function to actually encode the image in the file pointed to by filename.
     * If the file is a video-stream then you should encode the frame at the time given in parameters.
     * You must write the decoded image into dstImg. This function should convert the  pixels from srcImg
     * into the color-space and bitdepths of the newly created images's file.
     * You can inform the host of the bitdepth you support in input in the describe() function.
     * Note that many hosts work with linear colors and we intend that this function transfer to the
     * image file's color-space from linear. To help you do this you can use the color-space conversion
     * class (Lut) written for this purpose.
     * You can always skip the color-space conversion, but for all linear hosts it would produce either
     * false colors or sub-par performances in the case the end-user has to prepend a color-space conversion
     * effect her/himself.
     *
     * @pre The filename has been validated against the file extensions returned in supportedFileFormats(...)
     * You don't need to check this yourself.
     **/
    virtual void encode(const std::string& filename,OfxTime time,const OFX::Image* srcImg) = 0;

    /**
     * @brief Overload to return false if the given file extension is a video file extension or
     * true if this is an image file extension.
     **/
    virtual bool isImageFile(const std::string& fileExtension) const = 0;

    
    OFX::Clip* _inputClip; //< Mantated input clip
    OFX::Clip *_outputClip; //< Mandated output clip
    OFX::StringParam  *_fileParam; //< The output file
    OFX::ChoiceParam *_frameRange; //<The frame range type
    OFX::IntParam* _firstFrame; //< the first frame if the frame range type is "Manual"
    OFX::IntParam* _lastFrame; //< the last frame if the frame range type is "Manual"

#ifdef IO_USING_OCIO
    OFX::StringParam *_occioConfigFile; //< filepath of the OCCIO config file
    OFX::ChoiceParam* _outputColorSpace; //< the output color-space we're converting to
#endif
    
private:
    
    /* set up and run a copy processor */
    void setupAndProcess(CopierBase &, const OFX::RenderArguments &args,OFX::Image* srcImg,OFX::Image* dstImg);
};

class GenericWriterPluginFactory : public OFX::PluginFactoryHelper<GenericWriterPluginFactory>
{
public:
    
    GenericWriterPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin)
    :OFX::PluginFactoryHelper<GenericWriterPluginFactory>(id, verMaj, verMin)
    {}
    
    /**
     * @brief Override to do something when your plugin is loaded (kOfxActionLoad).
     * Base-class doesn't do anything.
     **/
    virtual void load(){}
    
    /**
     * @brief Override to do something when your plugin is unloaded (kOfxActionUnload).
     * Base-class doesn't do anything.
     **/
    virtual void unload(){}
    
    /**
     * @brief Overriden to add the default description common for all writers.
     * DON T OVERRIDE this, instead override describeWriter(...) which is called by describe.
     * WARNING: This function is called after that the base class has set some flags, make sure
     * you override them correctly.
     **/
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    
    /**
     * @brief Overriden to add the default params common for all writers.
     * DON T OVERRIDE this, instead override describeWriterInContext(...) which is called by describeInContext.
     **/
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    
    /**
     * @brief Override to create the instance of your writer.
     **/
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context){ return NULL; }
    
    /**
     * @brief Should append in formats the list of the format this plug-in can encode.
     * For example "png" , "jpg" , etc...
     **/
    virtual void supportedFileFormats(std::vector<std::string>* formats) const = 0;
    
    virtual bool isVideoStreamPlugin() const { return false; }
    
#ifdef IO_USING_OCIO
    /**
     * @brief Override to return in ocioRole the default OpenColorIO role the input color-space is.
     * This is used as a hint by the describeInContext() function to determine what color-space is should use
     * by-default to convert from the input color-space. The base-class version set ocioRole to OCIO::ROLE_SCENE_LINEAR.
     **/
    virtual void getOutputColorSpace(std::string& ocioRole) const;
#endif

protected:
    /**
     * @brief Override to describe your writer as you would do in the describe function.
     **/
    virtual void describeWriter(OFX::ImageEffectDescriptor &desc) = 0;
    
    
    /**
     * @brief Override to describe your writer in context as you would in the describeInContext function.
     **/
    virtual void describeWriterInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context,OFX::PageParamDescriptor* defaultPage) = 0;


};

#define mDeclareWriterPluginFactory(CLASS, LOADFUNCDEF, UNLOADFUNCDEF,ISVIDEOSTREAM,OCIOROLE) \
class CLASS : public GenericWriterPluginFactory \
{ \
public: \
CLASS(const std::string& id, unsigned int verMaj, unsigned int verMin):GenericWriterPluginFactory(id, verMaj, verMin){} \
virtual void load() LOADFUNCDEF ;\
virtual void unload() UNLOADFUNCDEF ;\
virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context); \
virtual void supportedFileFormats(std::vector<std::string>* formats) const; \
virtual bool isVideoStreamPlugin() const { return ISVIDEOSTREAM; } \
virtual void getOutputColorSpace(std::string& ocioRole) const { ocioRole = std::string(OCIOROLE); } \
virtual void describeWriter(OFX::ImageEffectDescriptor &desc); \
virtual void describeWriterInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context,OFX::PageParamDescriptor* defaultPage); \
};


#endif
