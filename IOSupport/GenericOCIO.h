/*
 OFX GenericOCIO plugin add-on.
 Adds OpenColorIO functionality to any plugin.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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

#ifndef IO_GenericOCIO_h
#define IO_GenericOCIO_h

#include <string>

#include <ofxsImageEffect.h>
#include "ofxsPixelProcessor.h"

// define OFX_OCIO_CHOICE to enable the colorspace choice popup menu
#define OFX_OCIO_CHOICE

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#define kOCIOParamConfigFile "ocioConfigFile"
#define kOCIOParamConfigFileLabel "OCIO Config File"
#define kOCIOParamConfigFileHint "OpenColorIO configuration file"
#define kOCIOParamInputSpace "ocioInputSpace"
#define kOCIOParamInputSpaceLabel "Input Colorspace"
#define kOCIOParamInputSpaceHint "Input data is taken to be in this colorspace."
#define kOCIOParamOutputSpace "ocioOutputSpace"
#define kOCIOParamOutputSpaceLabel "Output Colorspace"
#define kOCIOParamOutputSpaceHint "Output data is taken to be in this colorspace."
#ifdef OFX_OCIO_CHOICE
#define kOCIOParamInputSpaceChoice "ocioInputSpaceIndex"
#define kOCIOParamOutputSpaceChoice "ocioOutputSpaceIndex"
#endif
#define kOCIOHelpButton "ocioHelp"
#define kOCIOHelpLooksButton "ocioHelpLooks"
#define kOCIOHelpDisplaysButton "ocioHelpDisplays"
#define kOCIOHelpButtonLabel "OCIO config help..."
#define kOCIOHelpButtonHint "Help about the OpenColorIO configuration."
#else
#define kOCIOParamInputSpaceLabel ""
#define kOCIOParamOutputSpaceLabel ""
#endif

#define kOCIOParamContext "Context"
#define kOCIOParamContextHint \
"OCIO Contexts allow you to apply specific LUTs or grades to different shots.\n" \
"Here you can specify the context name (key) and its corresponding value.\n" \
"Full details of how to set up contexts and add them to your config can be found in the OpenColorIO documentation:\n" \
"http://opencolorio.org/userguide/contexts.html"

#define kOCIOParamContextKey1 "key1"
#define kOCIOParamContextValue1 "value1"
#define kOCIOParamContextKey2 "key2"
#define kOCIOParamContextValue2 "value2"
#define kOCIOParamContextKey3 "key3"
#define kOCIOParamContextValue3 "value3"
#define kOCIOParamContextKey4 "key4"
#define kOCIOParamContextValue4 "value4"

class GenericOCIO
{
    friend class OCIOProcessor;
public:
    GenericOCIO(OFX::ImageEffect* parent);
    bool isIdentity(double time);
    void apply(double time, const OfxRectI& renderWindow, OFX::Image* dstImg);
    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);
    void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    void purgeCaches();
    void getInputColorspace(std::string &v);
    void getInputColorspaceAtTime(double time, std::string &v);
    void getOutputColorspace(std::string &v);
    void getOutputColorspaceAtTime(double time, std::string &v);
    bool hasColorspace(const char* name) const;
    void setInputColorspace(const char* name);
    void setOutputColorspace(const char* name);
    OCIO_NAMESPACE::ConstContextRcPtr getLocalContext(double time);
    OCIO_NAMESPACE::ConstConfigRcPtr getConfig() { return _config; };
    bool configIsDefault();

    // Each of the following functions re-reads the OCIO config: Not optimal but more clear.
    static void describeInContextInput(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page, const char* inputSpaceNameDefault, const char* inputSpaceLabel = kOCIOParamInputSpaceLabel);
    static void describeInContextOutput(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page, const char* outputSpaceNameDefault, const char* outputSpaceLabel = kOCIOParamOutputSpaceLabel);
    static void describeInContextContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page);

private:
    void loadConfig();
    void inputCheck(double time);
    void outputCheck(double time);

    OFX::ImageEffect* _parent;
    bool _created;
#ifdef OFX_IO_USING_OCIO
    std::string _ocioConfigFileName;
    OFX::StringParam *_ocioConfigFile; //< filepath of the OCIO config file
    OFX::StringParam* _inputSpace;
    OFX::StringParam* _outputSpace;
#ifdef OFX_OCIO_CHOICE
    bool _choiceIsOk; //< true if the choice menu contains the right entries
    std::string _choiceFileName; //< the name of the OCIO config file that was used for the choice menu
    OFX::ChoiceParam* _inputSpaceChoice; //< the input colorspace we're converting from
    OFX::ChoiceParam* _outputSpaceChoice; //< the output colorspace we're converting to
#endif
    OFX::StringParam* _contextKey1;
    OFX::StringParam* _contextValue1;
    OFX::StringParam* _contextKey2;
    OFX::StringParam* _contextValue2;
    OFX::StringParam* _contextKey3;
    OFX::StringParam* _contextValue3;
    OFX::StringParam* _contextKey4;
    OFX::StringParam* _contextValue4;

    OCIO_NAMESPACE::ConstConfigRcPtr _config;
#endif
};

#ifdef OFX_IO_USING_OCIO
class OCIOProcessor : public OFX::PixelProcessor {
public:
    // ctor
    OCIOProcessor(OFX::ImageEffect &instance)
    : OFX::PixelProcessor(instance)
    , _proc()
    , _instance(&instance)
    {}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow);

    void setValues(const OCIO_NAMESPACE::ConstConfigRcPtr& config, const std::string& inputSpace, const std::string& outputSpace);
    void setValues(const OCIO_NAMESPACE::ConstConfigRcPtr& config, const OCIO_NAMESPACE::ConstContextRcPtr &context, const std::string& inputSpace, const std::string& outputSpace);
    void setValues(const OCIO_NAMESPACE::ConstConfigRcPtr& config, const OCIO_NAMESPACE::ConstTransformRcPtr& transform);
    void setValues(const OCIO_NAMESPACE::ConstConfigRcPtr& config, const OCIO_NAMESPACE::ConstTransformRcPtr& transform, OCIO_NAMESPACE::TransformDirection direction);
    void setValues(const OCIO_NAMESPACE::ConstConfigRcPtr& config, const OCIO_NAMESPACE::ConstContextRcPtr &context, const OCIO_NAMESPACE::ConstTransformRcPtr& transform, OCIO_NAMESPACE::TransformDirection direction);

private:
    OCIO_NAMESPACE::ConstProcessorRcPtr _proc;
    OFX::ImageEffect* _instance;
};
#endif

#endif
