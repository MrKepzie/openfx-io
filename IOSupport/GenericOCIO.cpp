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

#include "GenericOCIO.h"

#include <stdexcept>
#include <ofxsParam.h>
#include <ofxsImageEffect.h>
#include <ofxsLog.h>

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
static bool global_wasOCIOVarFund;
#endif

GenericOCIO::GenericOCIO(OFX::ImageEffect* parent, const char* inputName, const char* outputName)
: _parent(parent)
#ifdef OFX_IO_USING_OCIO
, _occioConfigFile(0)
, _inputSpace(0)
, _outputSpace(0)
, _inputSpaceNameDefault(inputName)
, _outputSpaceNameDefault(outputName)
, _config()
#endif
{
#ifdef OFX_IO_USING_OCIO
    _occioConfigFile = _parent->fetchStringParam(kOCCIOParamConfigFilename);
    _inputSpace = _parent->fetchChoiceParam(kOCIOParamInputSpace);
    _outputSpace = _parent->fetchChoiceParam(kOCIOParamOutputSpace);
    std::string filename;
    _occioConfigFile->getValue(filename);
    
    _config = OCIO::Config::CreateFromFile(filename.c_str());
#endif
    setDefault();
}

bool
GenericOCIO::isIdentity()
{
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return true;
    }
    int inputSpaceIndex;
    _inputSpace->getValue(inputSpaceIndex);
    int outputSpaceIndex;
    _outputSpace->getValue(outputSpaceIndex);
    return inputSpaceIndex == outputSpaceIndex;
#else
    return true;
#endif
}

void
GenericOCIO::apply(const OfxRectI& renderWindow, OFX::Image* img)
{
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return;
    }
    if (isIdentity()) {
        return;
    }
    OFX::BitDepthEnum bitDepth = img->getPixelDepth();
    if (bitDepth != OFX::eBitDepthFloat) {
        throw std::runtime_error("invalid pixel depth (only float is supported)");
    }

    apply(renderWindow, (float*)img->getPixelData(), img->getBounds(), img->getPixelComponents(), img->getRowBytes());
#endif
}

void
GenericOCIO::apply(const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return;
    }
    if (isIdentity()) {
        return;
    }
    // are we in the image bounds
    if(renderWindow.x1 < bounds.x1 || renderWindow.x1 >= bounds.x2 || renderWindow.y1 < bounds.y1 || renderWindow.y1 >= bounds.y2 ||
       renderWindow.x2 <= bounds.x1 || renderWindow.x2 > bounds.x2 || renderWindow.y2 <= bounds.y1 || renderWindow.y2 > bounds.y2) {
        throw std::runtime_error("render window outside of image bounds");
    }

    int numChannels;
    int pixelBytes;
    switch(pixelComponents)
    {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            break;
        //case OFX::ePixelComponentAlpha: pixelBytes = 1; break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    pixelBytes = numChannels * sizeof(float);
    float *pix = (float *) (((char *) pixelData) + (renderWindow.y1 - bounds.y1) * rowBytes + (renderWindow.x1 - bounds.x1) * pixelBytes);
    try {
        const char * inputSpaceName;
        {
            int inputSpaceIndex;
            _inputSpace->getValue(inputSpaceIndex);
            inputSpaceName = _config->getColorSpaceNameByIndex(inputSpaceIndex);
        }
        const char * outputSpaceName;
        {
            int outputSpaceIndex;
            _outputSpace->getValue(outputSpaceIndex);
            outputSpaceName = _config->getColorSpaceNameByIndex(outputSpaceIndex);
        }
        OCIO::ConstContextRcPtr context = _config->getCurrentContext();
        OCIO::ConstProcessorRcPtr proc = _config->getProcessor(context, inputSpaceName, outputSpaceName);

        OCIO::PackedImageDesc img(pix,renderWindow.x2 - renderWindow.x1,renderWindow.y2 - renderWindow.y1, numChannels, sizeof(float), pixelBytes, rowBytes);
        proc->apply(img);
    } catch(OCIO::Exception &e) {
        _parent->setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        throw std::runtime_error(std::string("OpenColorIO error: ")+e.what());
    }
#endif
}

void
GenericOCIO::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
#ifdef OFX_IO_USING_OCIO
    if ( paramName == kOCCIOParamConfigFilename ) {
        // this happens only if the parameter is enabled, i.e. on Natron.
        // Nuke, for example, doesn't support changing the options of a ChoiceParam.
        std::string filename;
        _occioConfigFile->getValue(filename);

        _config = OCIO::Config::CreateFromFile(filename.c_str());
        _inputSpace->resetOptions();
        _outputSpace->resetOptions();
        for (int i = 0; i < _config->getNumColorSpaces(); ++i) {
            std::string csname = _config->getColorSpaceNameByIndex(i);
            _inputSpace->appendOption(csname);
            _outputSpace->appendOption(csname);
        }
        setDefault();
    }
#endif
}

static void
printRole(OCIO::ConstConfigRcPtr config, const char* role)
{
    OFX::Log::print("OCIO %s->%s", role, config->getColorSpace(role)->getName());
}

void
GenericOCIO::setDefault()
{
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return;
    }
    printRole(_config, OCIO_NAMESPACE::ROLE_DEFAULT);
    printRole(_config, OCIO_NAMESPACE::ROLE_REFERENCE);
    printRole(_config, OCIO_NAMESPACE::ROLE_DATA);
    printRole(_config, OCIO_NAMESPACE::ROLE_COLOR_PICKING);
    printRole(_config, OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
    printRole(_config, OCIO_NAMESPACE::ROLE_COMPOSITING_LOG);
    printRole(_config, OCIO_NAMESPACE::ROLE_COLOR_TIMING);
    printRole(_config, OCIO_NAMESPACE::ROLE_TEXTURE_PAINT);
    printRole(_config, OCIO_NAMESPACE::ROLE_MATTE_PAINT);
    _inputSpace->setDefault(_config->getIndexForColorSpace(_inputSpaceNameDefault.c_str()));
    _outputSpace->setDefault(_config->getIndexForColorSpace(_outputSpaceNameDefault.c_str()));
#if 0
    std::string inputSpaceNameDefault = _config->getColorSpace(_inputSpaceNameDefault.c_str())->getName();
    std::string outputSpaceNameDefault = _config->getColorSpace(_outputSpaceNameDefault.c_str())->getName();
    for (int i = 0; i < _config->getNumColorSpaces(); ++i) {
        std::string csname = _config->getColorSpaceNameByIndex(i);
        if (csname == inputSpaceNameDefault) {
            _inputSpace->setDefault(i);
        }
        if (csname == outputSpaceNameDefault) {
            _outputSpace->setDefault(i);
        }
    }
#endif
#endif
}

#ifdef OFX_IO_USING_OCIO
std::string
GenericOCIO::getInputColorspace()
{
    if (!_config) {
        return "";
    }
    int index;
    _inputSpace->getValue(index);
    return _config->getColorSpaceNameByIndex(index);
}

std::string
GenericOCIO::getOutputColorspace()
{
    if (!_config) {
        return "";
    }
    assert(_config);
    int index;
    _outputSpace->getValue(index);
    return _config->getColorSpaceNameByIndex(index);
}
#endif

void
GenericOCIO::setInputColorspace(const char* name)
{
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return;
    }
    _inputSpace->setValue(_config->getIndexForColorSpace(name));
#endif
}

void
GenericOCIO::setOutputColorspace(const char* name)
{
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return;
    }
    _outputSpace->setValue(_config->getIndexForColorSpace(name));
#endif
}

void
GenericOCIO::purgeCaches()
{
#ifdef OFX_IO_USING_OCIO
    OCIO::ClearAllCaches();
#endif
}

void
GenericOCIO::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page)
{
#ifdef OFX_IO_USING_OCIO
    ////////// OCIO config file
    OFX::StringParamDescriptor* occioConfigFileParam = desc.defineStringParam(kOCCIOParamConfigFilename);
    occioConfigFileParam->setLabels("OCIO config file", "OCIO config file", "OCIO config file");
    occioConfigFileParam->setHint("OpenColorIO configuration file");
    occioConfigFileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*occioConfigFileParam);
    // the OCIO config can only be set in a portable fashion using the environment variable.
    // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
    // disable it, and set the default from the env variable.
    assert(OFX::getImageEffectHostDescription());
    if (OFX::getImageEffectHostDescription()->hostName == "NatronHost") {
        // enable it on Natron host only
        occioConfigFileParam->setEnabled(true);
        occioConfigFileParam->setStringType(OFX::eStringTypeFilePath);
    } else {
        occioConfigFileParam->setEnabled(false);
        //occioConfigFileParam->setStringType(OFX::eStringTypeFilePath);
    }
    char* file = std::getenv("OCIO");

    ///////////Input Color-space
    OFX::ChoiceParamDescriptor* inputSpace = desc.defineChoiceParam(kOCIOParamInputSpace);
    inputSpace->setLabels("Input colorspace", "Input colorspace", "Input colorspace");
    inputSpace->setHint("Input data is taken to be in this colorspace.");
    inputSpace->setAnimates(false);
    page->addChild(*inputSpace);

    ///////////Input Color-space
    OFX::ChoiceParamDescriptor* outputSpace = desc.defineChoiceParam(kOCIOParamOutputSpace);
    outputSpace->setLabels("Output colorspace", "Output colorspace", "Output colorspace");
    outputSpace->setHint("Output data is taken to be in this colorspace.");
    outputSpace->setAnimates(false);
    page->addChild(*outputSpace);

    if (file == NULL) {
        if (OFX::getImageEffectHostDescription()->hostName == "NatronHost") {
            occioConfigFileParam->setDefault("WARNING: You should set an OCIO environnement variable");
        } else {
            occioConfigFileParam->setDefault("WARNING: You must set an OCIO environnement variable");
        }
        inputSpace->setEnabled(false);
        outputSpace->setEnabled(false);
        global_wasOCIOVarFund = false;
    } else {
        global_wasOCIOVarFund = true;
        occioConfigFileParam->setDefault(file);
        //Add choices
        OCIO::ConstConfigRcPtr config = OCIO::Config::CreateFromFile(file);
        //std::string inputSpaceNameDefault = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName(); // FIXME: default should depend
        //std::string outputSpaceNameDefault = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName(); // FIXME: default sshould depend
        for (int i = 0; i < config->getNumColorSpaces(); ++i) {
            std::string csname = config->getColorSpaceNameByIndex(i);
            inputSpace->appendOption(csname);
            //if (csname == inputSpaceNameDefault) {
            //    inputSpace->setDefault(i);
            //}
            outputSpace->appendOption(csname);
            //if (csname == outputSpaceNameDefault) {
            //    outputSpace->setDefault(i);
            //}
        }
    }
#endif
}