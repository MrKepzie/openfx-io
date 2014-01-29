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

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
#define kOCCIOParamConfigFilename "ocio config file"
#define kOCIOParamInputSpace "ocio input space"
#define kOCIOParamOutputSpace "ocio output space"
#endif

class GenericOCIO
{

public:
    GenericOCIO(OFX::ImageEffect* parent, const char* inputName, const char* outputName);
    bool isIdentity();
    void apply(const OfxRectI& renderWindow, OFX::Image* dstImg);
    void apply(const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes);
    void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    void purgeCaches();
    void setDefault();
    std::string getInputColorspace();
    std::string getOutputColorspace();
    void setInputColorspace(const char* name);
    void setOutputColorspace(const char* name);

    static void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page);
    
private:
    OFX::ImageEffect* _parent;
#ifdef OFX_IO_USING_OCIO
    OFX::StringParam *_occioConfigFile; //< filepath of the OCCIO config file
    OFX::ChoiceParam* _inputSpace; //< the input colorspace we're converting from
    OFX::ChoiceParam* _outputSpace; //< the output colorspace we're converting to
    std::string _inputSpaceNameDefault;
    std::string _outputSpaceNameDefault;
    OCIO_NAMESPACE::ConstConfigRcPtr _config;
#endif
};

#endif
