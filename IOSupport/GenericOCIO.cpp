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
static bool global_hostIsNatron;
#endif

/* define to True to show the NATRON infinite loop bug in such cases as:
 void
 Plugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
 {
   if ( paramName == "blabla" && args.reason == OFX::eChangeUserEdit) {
     int blabla;
     _blabla->getValue(blabla);
     _blabla->setValue(blabla);
   }
 }
 
 normally, the recursion should stop at second level, since the reason should be eChangePluginEdit
 */
//#define NATRON_INFINITE_LOOP_BUG true
#ifndef NATRON_INFINITE_LOOP_BUG
#define NATRON_INFINITE_LOOP_BUG false
#endif

GenericOCIO::GenericOCIO(OFX::ImageEffect* parent)
: _parent(parent)
#ifdef OFX_IO_USING_OCIO
, _ocioConfigFileName()
, _ocioConfigFile(0)
, _inputSpace()
, _outputSpace()
#ifdef OFX_OCIO_CHOICE
, _choiceIsOk(true)
, _choiceFileName()
, _inputSpaceChoice(0)
, _outputSpaceChoice(0)
#endif
, _config()
#endif
{
#ifdef OFX_IO_USING_OCIO
    _ocioConfigFile = _parent->fetchStringParam(kOCCIOParamConfigFilename);
    _inputSpace = _parent->fetchStringParam(kOCIOParamInputSpace);
    _outputSpace = _parent->fetchStringParam(kOCIOParamOutputSpace);
#ifdef OFX_OCIO_CHOICE
    _ocioConfigFile->getDefault(_choiceFileName);
    _inputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamInputSpaceChoice);
    _outputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamOutputSpaceChoice);
#endif
#endif
}

void
GenericOCIO::loadConfig()
{
    std::string filename;
    _ocioConfigFile->getValue(filename);

    if (filename == _ocioConfigFileName) {
        return;
    }

    _config.reset();
    try {
        _ocioConfigFileName = filename;
        _config = OCIO::Config::CreateFromFile(_ocioConfigFileName.c_str());
    } catch (OCIO::Exception &e) {
        _ocioConfigFileName.clear();
        _inputSpace->setEnabled(false);
        _outputSpace->setEnabled(false);
#ifdef OFX_OCIO_CHOICE
        _inputSpaceChoice->setEnabled(false);
        _outputSpaceChoice->setEnabled(false);
#endif
    }
#ifdef OFX_OCIO_CHOICE
    if (_config) {
        if (global_hostIsNatron) {
            // the choice menu can only be modified in Natron
            // Natron supports changing the entries in a choiceparam
            _inputSpaceChoice->resetOptions();
            _outputSpaceChoice->resetOptions();
            for (int i = 0; i < _config->getNumColorSpaces(); ++i) {
                std::string csname = _config->getColorSpaceNameByIndex(i);
                _inputSpaceChoice->appendOption(csname);
                _outputSpaceChoice->appendOption(csname);
            }
            _choiceFileName = _ocioConfigFileName;
        }
        _choiceIsOk = (_ocioConfigFileName == _choiceFileName);
        inputCheck();
        outputCheck();
    }
#endif
}

bool
GenericOCIO::isIdentity()
{
#ifdef OFX_IO_USING_OCIO
    loadConfig();
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
GenericOCIO::inputCheck()
{
#ifdef OFX_OCIO_CHOICE
    if (!_config) {
        return;
    }
    if (!_choiceIsOk) {
        // choice menu is dirty, only use the text entry
        _inputSpace->setEnabled(true);
        _inputSpace->setIsSecret(false);
        _inputSpaceChoice->setEnabled(false);
        _inputSpaceChoice->setIsSecret(true);
        return;
    }
    std::string inputSpaceName;
    _inputSpace->getValue(inputSpaceName);
    int inputSpaceIndex = _config->getIndexForColorSpace(inputSpaceName.c_str());
    if (inputSpaceIndex >= 0) {
        int inputSpaceIndexOld;
        _inputSpaceChoice->getValue(inputSpaceIndexOld);
        if (NATRON_INFINITE_LOOP_BUG || inputSpaceIndexOld != inputSpaceIndex) {
            _inputSpaceChoice->setValue(inputSpaceIndex);
        }
        _inputSpace->setEnabled(false);
        _inputSpace->setIsSecret(true);
        _inputSpaceChoice->setEnabled(true);
        _inputSpaceChoice->setIsSecret(false);
    }
#endif
}

// returns true if the choice menu item and the string must be synchronized
void
GenericOCIO::outputCheck()
{
#ifdef OFX_OCIO_CHOICE
    if (!_config) {
        return;
    }
    if (!_choiceIsOk) {
        // choice menu is dirty, only use the text entry
        _outputSpace->setEnabled(true);
        _outputSpace->setIsSecret(false);
        _outputSpaceChoice->setEnabled(false);
        _outputSpaceChoice->setIsSecret(true);
        return;
    }
    std::string outputSpaceName;
    _outputSpace->getValue(outputSpaceName);
    int outputSpaceIndex = _config->getIndexForColorSpace(outputSpaceName.c_str());
    if (outputSpaceIndex >= 0) {
        int outputSpaceIndexOld;
        _outputSpaceChoice->getValue(outputSpaceIndexOld);
        if (NATRON_INFINITE_LOOP_BUG || outputSpaceIndexOld != outputSpaceIndex) {
            _outputSpaceChoice->setValue(outputSpaceIndex);
        }
        _outputSpace->setEnabled(false);
        _outputSpace->setIsSecret(true);
        _outputSpaceChoice->setEnabled(true);
        _outputSpaceChoice->setIsSecret(false);
    }
#endif
}

void
GenericOCIO::apply(const OfxRectI& renderWindow, OFX::Image* img)
{
#ifdef OFX_IO_USING_OCIO
    loadConfig();
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
    loadConfig();
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
        if (proc) {
            OCIO::PackedImageDesc img(pix,renderWindow.x2 - renderWindow.x1,renderWindow.y2 - renderWindow.y1, numChannels, sizeof(float), pixelBytes, rowBytes);
            proc->apply(img);
        }
    } catch (OCIO::Exception &e) {
        _parent->setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        throw std::runtime_error(std::string("OpenColorIO error: ")+e.what());
    }
#endif
}

void
GenericOCIO::beginEdit()
{
    loadConfig();
    // trigger changedParam() for inputSpace and outputSpace
    std::string inputSpace;
    _inputSpace->getValue(inputSpace);
    _inputSpace->setValue(inputSpace);
    std::string outputSpace;
    _outputSpace->getValue(inputSpace);
    _outputSpace->setValue(inputSpace);
}

void
GenericOCIO::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
#ifdef OFX_IO_USING_OCIO
    if ( paramName == kOCCIOParamConfigFilename ) {
        beginEdit();
        if (!_config && args.reason == OFX::eChangeUserEdit) {
            _parent->sendMessage(OFX::Message::eMessageError, "", std::string("Cannot load OCIO config file '") + _ocioConfigFileName);
        }
    }

    if (paramName == kOCIOHelpButton) {
        std::string msg = "OpenColorIO Help\n"
            "The OCIO configuration file can be set using the \"OCIO\" environment variable, which should contain the full path to the .ocio file.\n";
        if (_config) {
            const char* configdesc = _config->getDescription();
            int configdesclen = std::strlen(configdesc);
            if ( configdesclen > 0 ) {
                msg += "\nThis OCIO configuration is ";
                msg += configdesc;
                if (configdesc[configdesclen-1] != '\n') {
                    msg += '\n';
                }
            }
            msg += '\n';
            msg += "Available colorspaces in this OCIO Configuration:\n";
            int defaultcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT);
            int referencecs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_REFERENCE);
            int datacs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_DATA);
            int colorpickingcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COLOR_PICKING);
            int scenelinearcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
            int compositinglogcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COMPOSITING_LOG);
            int colortimingcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COLOR_TIMING);
            int texturepaintcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_TEXTURE_PAINT);
            int mattepaintcs = _config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_MATTE_PAINT);

            for (int i = 0; i < _config->getNumColorSpaces(); ++i) {
                const char* csname = _config->getColorSpaceNameByIndex(i);;
                OCIO_NAMESPACE::ConstColorSpaceRcPtr cs = _config->getColorSpace(csname);
                msg += "- ";
                msg += csname;
                if (i == defaultcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_DEFAULT;
                    msg += ')';
                }
                if (i == referencecs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_REFERENCE;
                    msg += ')';
                }
                if (i == datacs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_DATA;
                    msg += ')';
                }
                if (i == colorpickingcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_COLOR_PICKING;
                    msg += ')';
                }
                if (i == scenelinearcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_SCENE_LINEAR;
                    msg += ')';
                }
                if (i == compositinglogcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_COMPOSITING_LOG;
                    msg += ')';
                }
                if (i == colortimingcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_COLOR_TIMING;
                    msg += ')';
                }
                if (i == texturepaintcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_TEXTURE_PAINT;
                    msg += ')';
                }
                if (i == mattepaintcs) {
                    msg += " (";
                    msg += OCIO_NAMESPACE::ROLE_MATTE_PAINT;
                    msg += ')';
                }
                const char *csdesc = cs->getDescription();
                int csdesclen = std::strlen(csdesc);
                if ( csdesclen > 0 ) {
                    msg += ": ";
                    msg += csdesc;
                    if (csdesc[csdesclen-1] != '\n') {
                        msg += '\n';
                    }
                } else {
                    msg += '\n';
                }
            }
        }
        _parent->sendMessage(OFX::Message::eMessageMessage, "", msg);
    }

    // the other parameters assume there is a valid config
    if (!_config) {
        return;
    }

    if (paramName == kOCIOParamInputSpace) {
        std::string inputSpace;
        _inputSpace->getValue(inputSpace);
        int inputSpaceIndex = _config->getIndexForColorSpace(inputSpace.c_str());
        if (inputSpaceIndex < 0) {
            if (args.reason == OFX::eChangeUserEdit) {
                _parent->sendMessage(OFX::Message::eMessageWarning, "", std::string("Unknown OCIO colorspace \"")+inputSpace+"\"");
            }
            inputSpace = _config->getColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT)->getName();
            _inputSpace->setValue(inputSpace);
        }
        inputCheck();
    }
#ifdef OFX_OCIO_CHOICE
    if ( paramName == kOCIOParamInputSpaceChoice && args.reason == OFX::eChangeUserEdit) {
        int inputSpaceIndex;
        _inputSpaceChoice->getValue(inputSpaceIndex);
        std::string inputSpace = _config->getColorSpaceNameByIndex(inputSpaceIndex);
        _inputSpace->setValue(inputSpace);
    }
#endif

    if (paramName == kOCIOParamOutputSpace) {
        std::string outputSpace;
        _outputSpace->getValue(outputSpace);
        int outputSpaceIndex = _config->getIndexForColorSpace(outputSpace.c_str());
        if (outputSpaceIndex < 0) {
            if (args.reason == OFX::eChangeUserEdit) {
                _parent->sendMessage(OFX::Message::eMessageWarning, "", std::string("Unknown OCIO colorspace \"")+outputSpace+"\"");
            }
            outputSpace = _config->getColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT)->getName();
            _outputSpace->setValue(outputSpace);
            outputSpaceIndex = _config->getIndexForColorSpace(outputSpace.c_str());
        }
        outputCheck();
    }
#ifdef OFX_OCIO_CHOICE
    if ( paramName == kOCIOParamOutputSpaceChoice && args.reason == OFX::eChangeUserEdit) {
        int outputSpaceIndex;
        _outputSpaceChoice->getValue(outputSpaceIndex);
        std::string outputSpace = _config->getColorSpaceNameByIndex(outputSpaceIndex);
        _outputSpace->setValue(outputSpace);
    }
#endif // OFX_OCIO_CHOICE


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
GenericOCIO::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor *page, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault)
{
#ifdef OFX_IO_USING_OCIO
    global_hostIsNatron = (OFX::getImageEffectHostDescription()->hostName == "NatronHost");
    ////////// OCIO config file
    OFX::StringParamDescriptor* ocioConfigFileParam = desc.defineStringParam(kOCCIOParamConfigFilename);
    ocioConfigFileParam->setLabels("OCIO config file", "OCIO config file", "OCIO config file");
    ocioConfigFileParam->setHint("OpenColorIO configuration file");
    ocioConfigFileParam->setStringType(OFX::eStringTypeFilePath);
    ocioConfigFileParam->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*ocioConfigFileParam);
    // the OCIO config can only be set in a portable fashion using the environment variable.
    // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
    // disable it, and set the default from the env variable.
    assert(OFX::getImageEffectHostDescription());
    ocioConfigFileParam->setEnabled(true);
    ocioConfigFileParam->setStringType(OFX::eStringTypeFilePath);

    ///////////Input Color-space
    OFX::StringParamDescriptor* inputSpace = desc.defineStringParam(kOCIOParamInputSpace);
    inputSpace->setLabels("Input colorspace", "Input colorspace", "Input colorspace");
    inputSpace->setEnabled(true); // enabled only if host is not Natron and OCIO Config file is changed
    inputSpace->setIsSecret(false); // visible only if host is not Natron and OCIO Config file is changed
    page->addChild(*inputSpace);

#ifdef OFX_OCIO_CHOICE
    OFX::ChoiceParamDescriptor* inputSpaceChoice = desc.defineChoiceParam(kOCIOParamInputSpaceChoice);
    inputSpaceChoice->setLabels("Input colorspace", "Input colorspace", "Input colorspace");
    inputSpaceChoice->setHint("Input data is taken to be in this colorspace.");
    inputSpaceChoice->setAnimates(false);
    inputSpaceChoice->setIsPersistant(false);
    page->addChild(*inputSpaceChoice);
#endif

    ///////////Output Color-space
    OFX::StringParamDescriptor* outputSpace = desc.defineStringParam(kOCIOParamOutputSpace);
    outputSpace->setLabels("Output colorspace", "Output colorspace", "Output colorspace");
    outputSpace->setEnabled(true); // enabled only if host is not Natron and OCIO Config file is changed
    outputSpace->setIsSecret(false); // visible only if host is not Natron and OCIO Config file is changed
    page->addChild(*outputSpace);

#ifdef OFX_OCIO_CHOICE
    OFX::ChoiceParamDescriptor* outputSpaceChoice = desc.defineChoiceParam(kOCIOParamOutputSpaceChoice);
    outputSpaceChoice->setLabels("Output colorspace", "Output colorspace", "Output colorspace");
    outputSpaceChoice->setHint("Output data is taken to be in this colorspace.");
    outputSpaceChoice->setAnimates(false);
    outputSpaceChoice->setIsPersistant(false);
    page->addChild(*outputSpaceChoice);
#endif

    OFX::PushButtonParamDescriptor* pb = desc.definePushButtonParam(kOCIOHelpButton);
    pb->setLabels("Colorspace help", "Colorspace help", "Colorspace help");

    char* file = std::getenv("OCIO");
    OCIO::ConstConfigRcPtr config;
    if (file != NULL) {
        global_wasOCIOVarFund = true;
        ocioConfigFileParam->setDefault(file);
        //Add choices
        try {
            config = OCIO::Config::CreateFromFile(file);
        } catch (OCIO::Exception &e) {
        }
    }
    if (config) {
        std::string inputSpaceName = config->getColorSpace(inputSpaceNameDefault)->getName();
        inputSpace->setDefault(inputSpaceName);
        std::string outputSpaceName = config->getColorSpace(outputSpaceNameDefault)->getName();
        outputSpace->setDefault(outputSpaceName);
#ifdef OFX_OCIO_CHOICE
        //std::string inputSpaceNameDefault = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName(); // FIXME: default should depend
        //std::string outputSpaceNameDefault = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR)->getName(); // FIXME: default sshould depend
        for (int i = 0; i < config->getNumColorSpaces(); ++i) {
            std::string csname = config->getColorSpaceNameByIndex(i);
            inputSpaceChoice->appendOption(csname);
            if (csname == inputSpaceName) {
                inputSpaceChoice->setDefault(i);
            }
            outputSpaceChoice->appendOption(csname);
            if (csname == outputSpaceName) {
                outputSpaceChoice->setDefault(i);
            }
        }
#endif
    } else {
        if (file == NULL) {
            ocioConfigFileParam->setDefault("WARNING: Open an OCIO config file, or set an OCIO environnement variable");
        } else {
            std::string s("ERROR: Invalid OCIO configuration '");
            s += file;
            s += '\'';
            ocioConfigFileParam->setDefault(s);
        }
        inputSpace->setEnabled(false);
        outputSpace->setEnabled(false);
#ifdef OFX_OCIO_CHOICE
        inputSpaceChoice->setEnabled(false);
        inputSpaceChoice->setIsSecret(true);
        outputSpaceChoice->setEnabled(false);
        outputSpaceChoice->setIsSecret(true);
#endif
        global_wasOCIOVarFund = false;
    }
#endif
}