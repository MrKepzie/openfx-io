ne/*
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
static bool global_hostIsNatron;
#endif

GenericOCIO::GenericOCIO(OFX::ImageEffect* parent, const char* inputName, const char* outputName)
: _parent(parent)
#ifdef OFX_IO_USING_OCIO
, _occioConfigFileName()
, _occioConfigFile(0)
, _inputSpace()
, _inputSpaceChoice(0)
, _outputSpace()
, _outputSpaceChoice(0)
, _inputSpaceNameDefault(inputName)
, _outputSpaceNameDefault(outputName)
, _config()
#endif
{
#ifdef OFX_IO_USING_OCIO
    _occioConfigFile = _parent->fetchStringParam(kOCCIOParamConfigFilename);
    _inputSpace = _parent->fetchStringParam(kOCIOParamInputSpace);
    _inputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamInputSpaceChoice);
    _outputSpace = _parent->fetchStringParam(kOCIOParamOutputSpace);
    _outputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamOutputSpaceChoice);

    _occioConfigFile->getDefault(_occioConfigFileName);
    try {
        _config = OCIO::Config::CreateFromFile(_occioConfigFileName.c_str());
    } catch (OCIO::Exception &e) {
        _parent->setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        //throw std::runtime_error(std::string("OpenColorIO error: ")+e.what());
        _inputSpaceChoice->setEnabled(false);
        _outputSpace->setEnabled(false);
    }
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
    std::string inputSpace;
    _inputSpace->getValue(inputSpace);
    std::string outputSpace;
    _outputSpace->getValue(outputSpace);
    return inputSpace == outputSpace;
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
        std::string inputSpace;
        _inputSpace->getValue(inputSpace);
        std::string outputSpace;
        _outputSpace->getValue(outputSpace);
        OCIO::ConstContextRcPtr context = _config->getCurrentContext();
        OCIO::ConstProcessorRcPtr proc = _config->getProcessor(context, inputSpace.c_str(), outputSpace.c_str());

        OCIO::PackedImageDesc img(pix,renderWindow.x2 - renderWindow.x1,renderWindow.y2 - renderWindow.y1, numChannels, sizeof(float), pixelBytes, rowBytes);
        proc->apply(img);
    } catch (OCIO::Exception &e) {
        _parent->setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        throw std::runtime_error(std::string("OpenColorIO error: ")+e.what());
    }
#endif
}

void
GenericOCIO::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
#ifdef OFX_IO_USING_OCIO
    if (paramName == kOCIOParamInputSpace) {
        std::string inputSpace;
        _inputSpace->getValue(inputSpace);
        int inputSpaceIndex = _config ? _config->getIndexForColorSpace(inputSpace.c_str()) : -1;
        if (global_hostIsNatron && inputSpaceIndex >= 0) {
            _inputSpace->setIsSecret(true);
            // setIsSecret(true) to avoid triggering the changedParam() callback (see below)
            _inputSpaceChoice->setIsSecret(true);
            _inputSpaceChoice->setValue(inputSpaceIndex);
            _inputSpaceChoice->setIsSecret(false);
            _inputSpaceChoice->setEnabled(true);
        } else {
            // inputSpace can be modified by hand
            _inputSpace->setEnabled(true);
            _inputSpace->setIsSecret(false);
            _inputSpaceChoice->setIsSecret(true);
            _inputSpaceChoice->setEnabled(false);
        }
    }
    if ( paramName == kOCIOParamInputSpaceChoice && !_inputSpaceChoice->getIsSecret()) {
        assert(_config);
        if (_config) {
            int inputSpaceIndex;
            _inputSpaceChoice->getValue(inputSpaceIndex);
            std::string inputSpace = _config->getColorSpaceNameByIndex(inputSpaceIndex);
            _inputSpace->setValue(inputSpace);
        }
    }

    if (paramName == kOCIOParamOutputSpace) {
        std::string outputSpace;
        _outputSpace->getValue(outputSpace);
        int outputSpaceIndex = _config ? _config->getIndexForColorSpace(outputSpace.c_str()) : -1;
        if (global_hostIsNatron && outputSpaceIndex >= 0) {
            _outputSpace->setIsSecret(true);
            // setIsSecret(true) to avoid triggering the changedParam() callback (see below)
            _outputSpaceChoice->setIsSecret(true);
            _outputSpaceChoice->setValue(outputSpaceIndex);
            _outputSpaceChoice->setIsSecret(false);
            _outputSpaceChoice->setEnabled(true);
        } else {
            // outputSpace can be modified by hand
            _outputSpace->setEnabled(true);
            _outputSpace->setIsSecret(false);
            _outputSpaceChoice->setIsSecret(true);
            _outputSpaceChoice->setEnabled(false);
        }
    }
    if ( paramName == kOCIOParamOutputSpaceChoice && !_outputSpaceChoice->getIsSecret()) {
        assert(_config);
        if (_config) {
            int outputSpaceIndex;
            _outputSpaceChoice->getValue(outputSpaceIndex);
            std::string outputSpace = _config->getColorSpaceNameByIndex(outputSpaceIndex);
            _outputSpace->setValue(outputSpace);
        }
    }

    if ( paramName == kOCCIOParamConfigFilename ) {
        std::string filename;
        _occioConfigFile->getValue(filename);
        if (filename != _occioConfigFileName) {
            _config.reset();
            try {
                _occioConfigFileName = filename;
                _config = OCIO::Config::CreateFromFile(_occioConfigFileName.c_str());
            } catch (OCIO::Exception &e) {
                _parent->setPersistentMessage(OFX::Message::eMessageError, "", e.what());
                _occioConfigFileName.clear();
            }
            if (_config) {
                if (global_hostIsNatron) {
                    // Natron supports changing the entries in a choiceparam
                    _inputSpaceChoice->resetOptions();
                    _outputSpaceChoice->resetOptions();
                    for (int i = 0; i < _config->getNumColorSpaces(); ++i) {
                        std::string csname = _config->getColorSpaceNameByIndex(i);
                        _inputSpaceChoice->appendOption(csname);
                        _outputSpaceChoice->appendOption(csname);
                    }
                }
            }
            // trigger changedParam() for inputSpace and outputSpace
            std::string inputSpace;
            _inputSpace->getValue(inputSpace);
            _inputSpace->setValue(inputSpace);
            std::string outputSpace;
            _outputSpace->getValue(inputSpace);
            _outputSpace->setValue(inputSpace);
        }
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
    std::string inputSpace = _config->getColorSpace(_inputSpaceNameDefault.c_str())->getName();
    _inputSpace->setDefault(inputSpace);
    std::string outputSpace = _config->getColorSpace(_outputSpaceNameDefault.c_str())->getName();
    _outputSpace->setDefault(outputSpace);
#endif
}

#ifdef OFX_IO_USING_OCIO
std::string
GenericOCIO::getInputColorspace()
{
    std::string space;
    _inputSpace->getValue(space);
    return space;
}

std::string
GenericOCIO::getOutputColorspace()
{
    std::string space;
    _outputSpace->getValue(space);
    return space;
}
#endif

void
GenericOCIO::setInputColorspace(const char* name)
{
#ifdef OFX_IO_USING_OCIO
    _inputSpace->setValue(name);
#endif
}

void
GenericOCIO::setOutputColorspace(const char* name)
{
#ifdef OFX_IO_USING_OCIO
    _outputSpace->setValue(name);
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
    global_hostIsNatron = (OFX::getImageEffectHostDescription()->hostName == "NatronHost");
    ////////// OCIO config file
    OFX::StringParamDescriptor* occioConfigFileParam = desc.defineStringParam(kOCCIOParamConfigFilename);
    occioConfigFileParam->setLabels("OCIO config file", "OCIO config file", "OCIO config file");
    occioConfigFileParam->setHint("OpenColorIO configuration file");
    occioConfigFileParam->setStringType(OFX::eStringTypeFilePath);
    occioConfigFileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*occioConfigFileParam);
    // the OCIO config can only be set in a portable fashion using the environment variable.
    // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
    // disable it, and set the default from the env variable.
    assert(OFX::getImageEffectHostDescription());
    occioConfigFileParam->setEnabled(true);
    occioConfigFileParam->setStringType(OFX::eStringTypeFilePath);

    ///////////Input Color-space
    {
        OFX::StringParamDescriptor* inputSpace = desc.defineStringParam(kOCIOParamInputSpace);
        inputSpace->setEnabled(false); // enabled only if host is not Natron and OCIO Config file is changed
        inputSpace->setIsSecret(true); // visible only if host is not Natron and OCIO Config file is changed
        page->addChild(*inputSpace);
    }

    OFX::ChoiceParamDescriptor* inputSpaceChoice = desc.defineChoiceParam(kOCIOParamInputSpaceChoice);
    inputSpaceChoice->setLabels("Input colorspace", "Input colorspace", "Input colorspace");
    inputSpaceChoice->setHint("Input data is taken to be in this colorspace.");
    inputSpaceChoice->setAnimates(false);
    inputSpaceChoice->setIsPersistant(false);
    page->addChild(*inputSpaceChoice);

    ///////////Output Color-space
    {
        OFX::StringParamDescriptor* outputSpace = desc.defineStringParam(kOCIOParamOutputSpace);
        outputSpace->setEnabled(false); // enabled only if host is not Natron and OCIO Config file is changed
        outputSpace->setIsSecret(true); // visible only if host is not Natron and OCIO Config file is changed
        page->addChild(*outputSpace);
    }

    OFX::ChoiceParamDescriptor* outputSpaceChoice = desc.defineChoiceParam(kOCIOParamOutputSpaceChoice);
    outputSpaceChoice->setLabels("Output colorspace", "Output colorspace", "Output colorspace");
    outputSpaceChoice->setHint("Output data is taken to be in this colorspace.");
    outputSpaceChoice->setAnimates(false);
    outputSpaceChoice->setIsPersistant(false);
    page->addChild(*outputSpaceChoice);

    char* file = std::getenv("OCIO");
    OCIO::ConstConfigRcPtr config;
    if (file != NULL) {
        global_wasOCIOVarFund = true;
        occioConfigFileParam->setDefault(file);
        //Add choices
        try {
            config = OCIO::Config::CreateFromFile(file);
        } catch (OCIO::Exception &e) {
        }
    }
    if (config) {
        //std::string inputSpaceNameDefault = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName(); // FIXME: default should depend
        //std::string outputSpaceNameDefault = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName(); // FIXME: default sshould depend
        for (int i = 0; i < config->getNumColorSpaces(); ++i) {
            std::string csname = config->getColorSpaceNameByIndex(i);
            inputSpaceChoice->appendOption(csname);
            //if (csname == inputSpaceNameDefault) {
            //    inputSpace->setDefault(i);
            //}
            outputSpaceChoice->appendOption(csname);
            //if (csname == outputSpaceNameDefault) {
            //    outputSpace->setDefault(i);
            //}
        }
    } else {
        if (file == NULL) {
            occioConfigFileParam->setDefault("WARNING: You should set an OCIO environnement variable");
        } else {
            std::string s("ERROR: Invalid OCIO configuration '");
            s += file;
            s += '\'';
            occioConfigFileParam->setDefault(s);
        }
        inputSpaceChoice->setEnabled(false);
        inputSpaceChoice->setIsSecret(true);
        outputSpaceChoice->setEnabled(false);
        outputSpaceChoice->setIsSecret(true);
        global_wasOCIOVarFund = false;
    }
#endif
}