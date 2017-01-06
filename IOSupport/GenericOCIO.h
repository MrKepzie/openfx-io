/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2013-2017 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX GenericOCIO plugin add-on.
 * Adds OpenColorIO functionality to any plugin.
 */

#ifndef IO_GenericOCIO_h
#define IO_GenericOCIO_h

#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsPixelProcessor.h"
#include "ofxsMultiThread.h"
#ifndef OFX_USE_MULTITHREAD_MUTEX
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
#endif

// define OFX_OCIO_CHOICE to enable the colorspace choice popup menu
#define OFX_OCIO_CHOICE

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#endif

#include "IOUtility.h"

NAMESPACE_OFX_ENTER
    NAMESPACE_OFX_IO_ENTER


#ifdef OFX_IO_USING_OCIO
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
#define kOCIOParamContextLabel "OCIO Context"
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

class OCIOOpenGLContextData
{
public:
    std::vector<float> procLut3D;
    std::string procShaderCacheID;
    std::string procLut3DCacheID;
    unsigned int procLut3DID;
    unsigned int procShaderProgramID;
    unsigned int procFragmentShaderID;

public:
    OCIOOpenGLContextData();

    ~OCIOOpenGLContextData();
};


class GenericOCIO
{
    friend class OCIOProcessor;

public:
    GenericOCIO(OFX::ImageEffect* parent);
    bool isIdentity(double time) const;

    /**
     * @brief Applies the given OCIO processor using GLSL with the given source texture onto
     * the currently bound framebuffer.
     * @param lut3DParam[in,out] If non NULL, you may pass a storage for the LUT3D so that the allocation
     * of the LUT only occurs once.
     *
     * @param lut3DTexIDParam[in,out] If non NULL, you may pass the ID of the texture 3D that will contain the LUT3D so
     * that its allocation occurs only once, and subsequent calls only have to call glTexSubImage3D
     *
     * @param shaderProgramIDParam[in,out] If non NULL, you may pass the ID of the shader program that will be used to do the processing
     * so that it is only compiled once. Note that to cache the shaderProgramID, you also need to set the shaderTextCacheIDParam parameter.
     *
     * @param fragShaderIDParam[in,out] If nont NULL, you may pass the ID of the fragment shader program that will be used by the OCIO shader program
     *
     * @param lut3DCacheIDParam[in,out] If non NULL, you may pass a string that will be used as a key to cache the LUT3D so that internally
     * the function may determine if computing the LUT again is required. If the cache ID did not change, no call to glTexSubImage3D will be made.
     *
     * @param shaderTextCacheIDParam[in,out] if non NULL, you may pass a string that will be used as a key to cache the shader so that internally
     * the function may determine if generating and compiling the shader again is required. If the shader cache ID did not change, the shader passed
     * by shaderProgramIDParam will be used as-is.
     *
     * Note: All lut3DParam, lut3DTexIDParam, shaderProgramIDParam, lut3DCacheIDParam, shaderTextCacheIDParam must be either set to a value different
     * than NULL, or all set to NULL.
     *
     **/
#if defined(OFX_IO_USING_OCIO)
    static void applyGL(const OFX::Texture* srcImg,
                        const OCIO_NAMESPACE::ConstProcessorRcPtr& processor,
                        std::vector<float>* lut3DParam,
                        unsigned int *lut3DTexIDParam,
                        unsigned int *shaderProgramIDParam,
                        unsigned int *fragShaderIDParam,
                        std::string* lut3DCacheIDParam,
                        std::string* shaderTextCacheIDParam);
#endif

    void apply(double time, const OfxRectI& renderWindow, OFX::Image* dstImg);
    void apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes);
    void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    void purgeCaches();
    void getInputColorspaceDefault(std::string &v) const;
    void getInputColorspace(std::string &v) const;
    void getInputColorspaceAtTime(double time, std::string &v) const;
    void getOutputColorspaceDefault(std::string &v) const;
    void getOutputColorspace(std::string &v) const;
    void getOutputColorspaceAtTime(double time, std::string &v) const;
    bool hasColorspace(const char* name) const;
    void setInputColorspace(const char* name);
    void setOutputColorspace(const char* name);
#ifdef OFX_IO_USING_OCIO
    OCIO_NAMESPACE::ConstContextRcPtr getLocalContext(double time) const;
    OCIO_NAMESPACE::ConstConfigRcPtr getConfig() const { return _config; };
    OCIO_NAMESPACE::ConstProcessorRcPtr getProcessor() const;
    OCIO_NAMESPACE::ConstProcessorRcPtr getOrCreateProcessor(double time);

#endif
    bool configIsDefault() const;

    // Each of the following functions re-reads the OCIO config: Not optimal but more clear.
    static void describeInContextInput(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page, const char* inputSpaceNameDefault, const char* inputSpaceLabel = kOCIOParamInputSpaceLabel);
    static void describeInContextOutput(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page, const char* outputSpaceNameDefault, const char* outputSpaceLabel = kOCIOParamOutputSpaceLabel);
    static void describeInContextContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page);

#ifdef OFX_IO_USING_OCIO
    void setValues(const std::string& inputSpace, const std::string& outputSpace);
    void setValues(const OCIO_NAMESPACE::ConstContextRcPtr &context, const std::string& inputSpace, const std::string& outputSpace);
#endif

    // Calls inputCheck and outputCheck
    void refreshInputAndOutputState(double time)
    {
        inputCheck(time);
        outputCheck(time);
    }

public:
#ifdef OFX_USE_MULTITHREAD_MUTEX
    typedef OFX::MultiThread::Mutex Mutex;
    typedef OFX::MultiThread::AutoMutex AutoMutex;
#else
    typedef tthread::fast_mutex Mutex;
    typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
#endif

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

    mutable Mutex _procMutex;
    OCIO_NAMESPACE::ConstProcessorRcPtr _proc;
    OCIO_NAMESPACE::ConstContextRcPtr _procContext;
    std::string _procInputSpace;
    std::string _procOutputSpace;
    //OCIO_NAMESPACE::ConstTransformRcPtr _procTransform;
#endif
};

#ifdef OFX_IO_USING_OCIO
class OCIOProcessor
    : public OFX::PixelProcessor
{
public:
    // ctor
    OCIOProcessor(OFX::ImageEffect &instance)
        : OFX::PixelProcessor(instance)
        , _proc()
        , _instance(&instance)
    {}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow);

    void setProcessor(const OCIO_NAMESPACE::ConstProcessorRcPtr& proc)
    {
        _proc = proc;
    }

private:
    OCIO_NAMESPACE::ConstProcessorRcPtr _proc;
    OFX::ImageEffect* _instance;
};

#endif

NAMESPACE_OFX_IO_EXIT
    NAMESPACE_OFX_EXIT

#endif // ifndef IO_GenericOCIO_h
