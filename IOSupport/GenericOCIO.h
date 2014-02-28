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
#define kOCIOParamConfigFilename "ocio config file"
#define kOCIOParamInputSpace "ocio input space"
#define kOCIOParamOutputSpace "ocio output space"
#ifdef OFX_OCIO_CHOICE
#define kOCIOParamInputSpaceChoice "ocio input space index"
#define kOCIOParamOutputSpaceChoice "ocio output space index"
#endif
#define kOCIOHelpButton "ocio help"
#endif

class GenericOCIO
{
    friend class OCIOProcessor;
public:
    GenericOCIO(OFX::ImageEffect* parent);
    bool isIdentity();
    void apply(const OfxRectI& renderWindow, OFX::Image* dstImg);
    void apply(const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes);
    void beginEdit();
    void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    void purgeCaches();
    std::string getInputColorspace() const;
    std::string getOutputColorspace() const;
    bool hasColorspace(const char* name) const;
    void setInputColorspace(const char* name);
    void setOutputColorspace(const char* name);

    static void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault);

private:
    void loadConfig();
    void inputCheck();
    void outputCheck();
    void applyInternal(const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes);

    OFX::ImageEffect* _parent;
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
    OCIO_NAMESPACE::ConstConfigRcPtr _config;
#endif
};

#endif
