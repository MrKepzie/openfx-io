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

#include <cstring>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <ofxsParam.h>
#include <ofxsImageEffect.h>
#include <ofxsLog.h>
#include <ofxNatron.h>

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
static bool gWasOCIOEnvVarFound = false;
static bool gHostIsNatron   = false;
#endif

GenericOCIO::GenericOCIO(OFX::ImageEffect* parent)
: _parent(parent)
, _created(false)
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
    _ocioConfigFile = _parent->fetchStringParam(kOCIOParamConfigFileName);
    _inputSpace = _parent->fetchStringParam(kOCIOParamInputSpaceName);
    _outputSpace = _parent->fetchStringParam(kOCIOParamOutputSpaceName);
#ifdef OFX_OCIO_CHOICE
    _ocioConfigFile->getDefault(_choiceFileName);
    _inputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamInputSpaceChoiceName);
    _outputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamOutputSpaceChoiceName);
#endif
    loadConfig(0.);
#endif
    // setup the GUI
    // setValue() may be called from createInstance, according to
    // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#SettingParams
    inputCheck(0.);
    outputCheck(0.);
    _created = true;
}

#ifdef OFX_IO_USING_OCIO
#ifdef OFX_OCIO_CHOICE

// ChoiceParamType may be OFX::ChoiceParamDescriptor or OFX::ChoiceParam
template <typename ChoiceParamType>
static void
buildChoiceMenu(OCIO::ConstConfigRcPtr config,
                ChoiceParamType* choice,
                const std::string& name = "")
{
    choice->resetOptions();
    if (!config) {
        return;
    }
    int defaultcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT);
    int referencecs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_REFERENCE);
    int datacs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_DATA);
    int colorpickingcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COLOR_PICKING);
    int scenelinearcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
    int compositinglogcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COMPOSITING_LOG);
    int colortimingcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COLOR_TIMING);
    int texturepaintcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_TEXTURE_PAINT);
    int mattepaintcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_MATTE_PAINT);
    for (int i = 0; i < config->getNumColorSpaces(); ++i) {
        std::string csname = config->getColorSpaceNameByIndex(i);
        std::string msg;
        OCIO_NAMESPACE::ConstColorSpaceRcPtr cs = config->getColorSpace(csname.c_str());
        std::string csdesc = cs->getDescription();
        csdesc.erase(csdesc.find_last_not_of(" \n\r\t")+1);
        int csdesclen = csdesc.size();
        if ( csdesclen > 0 ) {
            msg += csdesc;
        }
        bool first = true;
        int roles = 0;
        if (i == defaultcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_DEFAULT;
            first = false;
            ++roles;
        }
        if (i == referencecs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_REFERENCE;
            first = false;
            ++roles;
        }
        if (i == datacs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_DATA;
            first = false;
            ++roles;
        }
        if (i == colorpickingcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_COLOR_PICKING;
            first = false;
            ++roles;
        }
        if (i == scenelinearcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_SCENE_LINEAR;
            first = false;
            ++roles;
        }
        if (i == compositinglogcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_COMPOSITING_LOG;
            first = false;
            ++roles;
        }
        if (i == colortimingcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_COLOR_TIMING;
            first = false;
            ++roles;
        }
        if (i == texturepaintcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_TEXTURE_PAINT;
            first = false;
            ++roles;
        }
        if (i == mattepaintcs) {
            msg += first ? " (" : ", ";
            msg += OCIO_NAMESPACE::ROLE_MATTE_PAINT;
            first = false;
            ++roles;
        }
        if (roles > 0) {
            msg += ')';
        }
        choice->appendOption(csname, msg);
        // set the default value, in case the GUI uses it
        if (!name.empty() && csname == name) {
            choice->setDefault(i);
        }
    }
}
#endif
#endif

void
GenericOCIO::loadConfig(double time)
{
#ifdef OFX_IO_USING_OCIO
    std::string filename;
    _ocioConfigFile->getValueAtTime(time, filename);

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
        if (gHostIsNatron) {
            // the choice menu can only be modified in Natron
            // Natron supports changing the entries in a choiceparam
            // Nuke (at least up to 8.0v3) does not
            buildChoiceMenu(_config, _inputSpaceChoice);
            buildChoiceMenu(_config, _outputSpaceChoice);
            _choiceFileName = _ocioConfigFileName;
        }
        _choiceIsOk = (_ocioConfigFileName == _choiceFileName);
        // do not set values during CreateInstance!!
        ////inputCheck(); // may set values
        ////outputCheck(); // may set values
    }
#endif
#endif
}

bool
GenericOCIO::isIdentity(double time)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return true;
    }
    std::string inputSpace;
    _inputSpace->getValueAtTime(time, inputSpace);
    std::string outputSpace;
    _outputSpace->getValueAtTime(time, outputSpace);
    if (inputSpace == outputSpace) {
        return true;
    }
    try {
        // maybe the names are not the same, but it's still a no-op (e.g. "scene_linear" and "linear")
        OCIO::ConstContextRcPtr context = _config->getCurrentContext();
        OCIO_NAMESPACE::ConstProcessorRcPtr proc = _config->getProcessor(context, inputSpace.c_str(), outputSpace.c_str());
        return proc->isNoOp();
    } catch (const std::exception& e) {
        _parent->setPersistentMessage(OFX::Message::eMessageError, "", e.what());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }
#else
    return true;
#endif
}


// sets the correct choice menu item from the inputSpace string value
void
GenericOCIO::inputCheck(double time)
{
#ifdef OFX_IO_USING_OCIO
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
    _inputSpace->getValueAtTime(time, inputSpaceName);
    int inputSpaceIndex = _config->getIndexForColorSpace(inputSpaceName.c_str());
    if (inputSpaceIndex >= 0) {
        int inputSpaceIndexOld;
        _inputSpaceChoice->getValueAtTime(time, inputSpaceIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (inputSpaceIndexOld != inputSpaceIndex) {
            _inputSpaceChoice->setValue(inputSpaceIndex);
        }
        _inputSpace->setEnabled(false);
        _inputSpace->setIsSecret(true);
        _inputSpaceChoice->setEnabled(true);
        _inputSpaceChoice->setIsSecret(false);
    } else {
        // the input space name is not valid
        _inputSpace->setEnabled(true);
        _inputSpace->setIsSecret(false);
        _inputSpaceChoice->setEnabled(false);
        _inputSpaceChoice->setIsSecret(true);
    }
#endif
#endif
}

// sets the correct choice menu item from the outputSpace string value
void
GenericOCIO::outputCheck(double time)
{
#ifdef OFX_IO_USING_OCIO
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
    _outputSpace->getValueAtTime(time, outputSpaceName);
    int outputSpaceIndex = _config->getIndexForColorSpace(outputSpaceName.c_str());
    if (outputSpaceIndex >= 0) {
        int outputSpaceIndexOld;
        _outputSpaceChoice->getValueAtTime(time, outputSpaceIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (outputSpaceIndexOld != outputSpaceIndex) {
            _outputSpaceChoice->setValue(outputSpaceIndex);
        }
        _outputSpace->setEnabled(false);
        _outputSpace->setIsSecret(true);
        _outputSpaceChoice->setEnabled(true);
        _outputSpaceChoice->setIsSecret(false);
    } else {
        // the output space name is not valid
        _outputSpace->setEnabled(true);
        _outputSpace->setIsSecret(false);
        _outputSpaceChoice->setEnabled(false);
        _outputSpaceChoice->setIsSecret(true);
    }
#endif
#endif
}

void
GenericOCIO::apply(double time, const OfxRectI& renderWindow, OFX::Image* img)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    OFX::BitDepthEnum bitDepth = img->getPixelDepth();
    if (bitDepth != OFX::eBitDepthFloat) {
        throw std::runtime_error("OCIO: invalid pixel depth (only float is supported)");
    }

    apply(time, renderWindow, (float*)img->getPixelData(), img->getBounds(), img->getPixelComponents(), img->getRowBytes());
#endif
}


void
OCIOProcessor::setValues(const OCIO_NAMESPACE::ConstConfigRcPtr &config, const std::string& inputSpace, const std::string& outputSpace)
{
    OCIO::ConstContextRcPtr context = config->getCurrentContext();
    _proc = config->getProcessor(context, inputSpace.c_str(), outputSpace.c_str());
}

void
OCIOProcessor::setValues(const OCIO_NAMESPACE::ConstConfigRcPtr &config, const OCIO_NAMESPACE::ConstTransformRcPtr& transform, OCIO_NAMESPACE::TransformDirection direction)
{
    _proc = config->getProcessor(transform, direction);
}

void
OCIOProcessor::setValues(const OCIO_NAMESPACE::ConstConfigRcPtr &config, const OCIO_NAMESPACE::ConstTransformRcPtr& transform)
{
    _proc = config->getProcessor(transform);
}

void
OCIOProcessor::multiThreadProcessImages(OfxRectI renderWindow)
{
    assert(_dstBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= _dstBounds.x2);
    assert(_dstBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= _dstBounds.y2);
#ifdef OFX_IO_USING_OCIO
    if (!_proc) {
        throw std::logic_error("OCIO configuration not loaded");
    }
    int numChannels;
    int pixelBytes;
    switch(_dstPixelComponents)
    {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            break;
            //case OFX::ePixelComponentAlpha: pixelBytes = 1; break;
        default:
            numChannels = 0;
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    pixelBytes = numChannels * sizeof(float);
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - _dstBounds.y1) * _dstRowBytes + (size_t)(renderWindow.x1 - _dstBounds.x1) * pixelBytes;
    float *pix = (float *) (((char *) _dstPixelData) + pixelDataOffset); // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
    try {
        if (_proc) {
            OCIO::PackedImageDesc img(pix,renderWindow.x2 - renderWindow.x1,renderWindow.y2 - renderWindow.y1, numChannels, sizeof(float), pixelBytes, _dstRowBytes);
            _proc->apply(img);
        }
    } catch (OCIO::Exception &e) {
        _instance->setPersistentMessage(OFX::Message::eMessageError, "", std::string("OpenColorIO error: ") + e.what());
        throw std::runtime_error(std::string("OpenColorIO error: ") + e.what());
    }
    _instance->clearPersistentMessage();
#endif
}


void
GenericOCIO::apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return;
    }
    if (isIdentity(time)) {
        return;
    }
    // are we in the image bounds
    if(renderWindow.x1 < bounds.x1 || renderWindow.x1 >= bounds.x2 || renderWindow.y1 < bounds.y1 || renderWindow.y1 >= bounds.y2 ||
       renderWindow.x2 <= bounds.x1 || renderWindow.x2 > bounds.x2 || renderWindow.y2 <= bounds.y1 || renderWindow.y2 > bounds.y2) {
        throw std::runtime_error("OCIO: render window outside of image bounds");
    }
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB) {
        throw std::runtime_error("OCIO: invalid components (only RGB and RGBA are supported)");
    }

    OCIOProcessor processor(*_parent);
    // set the images
    processor.setDstImg(pixelData, bounds, pixelComponents, OFX::eBitDepthFloat, rowBytes);

    std::string inputSpace;
    _inputSpace->getValueAtTime(time, inputSpace);
    std::string outputSpace;
    _outputSpace->getValueAtTime(time, outputSpace);
    processor.setValues(_config, inputSpace, outputSpace);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
#endif
}


void
GenericOCIO::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    if (paramName == kOCIOParamConfigFileName) {
        loadConfig(args.time); // re-load the new OCIO config
        inputCheck(args.time);
        outputCheck(args.time);
        if (!_config && args.reason == OFX::eChangeUserEdit) {
            std::string filename;
            _ocioConfigFile->getValueAtTime(args.time, filename);
            _parent->sendMessage(OFX::Message::eMessageError, "", std::string("Cannot load OCIO config file \"") + filename + '"');
        }
    } else if (paramName == kOCIOHelpButtonName) {
        std::string msg = "OpenColorIO Help\n"
            "The OCIO configuration file can be set using the \"OCIO\" environment variable, which should contain the full path to the .ocio file.\n"
            "OpenColorIO version (compiled with / running with): " OCIO_VERSION "/";
        msg += OCIO_NAMESPACE::GetVersion();
        msg += '\n';
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
                bool first = true;
                //int roles = 0;
                if (i == defaultcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_DEFAULT;
                    first = false;
                    //++roles;
                }
                if (i == referencecs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_REFERENCE;
                    first = false;
                    //++roles;
                }
                if (i == datacs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_DATA;
                    first = false;
                    //++roles;
                }
                if (i == colorpickingcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_COLOR_PICKING;
                    first = false;
                    //++roles;
                }
                if (i == scenelinearcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_SCENE_LINEAR;
                    first = false;
                    //++roles;
                }
                if (i == compositinglogcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_COMPOSITING_LOG;
                    first = false;
                    //++roles;
                }
                if (i == colortimingcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_COLOR_TIMING;
                    first = false;
                    //++roles;
                }
                if (i == texturepaintcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_TEXTURE_PAINT;
                    first = false;
                    //++roles;
                }
                if (i == mattepaintcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO_NAMESPACE::ROLE_MATTE_PAINT;
                    first = false;
                    //++roles;
                }
                if (!first /*&& roles > 0*/) {
                    msg += ')';
                }
                std::string csdesc = cs->getDescription();
                csdesc.erase(csdesc.find_last_not_of(" \n\r\t")+1);
                int csdesclen = csdesc.size();
                if ( csdesclen > 0 ) {
                    msg += ": ";
                    msg += csdesc;
                    msg += '\n';
                } else {
                    msg += '\n';
                }
            }
        }
        _parent->sendMessage(OFX::Message::eMessageMessage, "", msg);
    } else if (!_config) {
        // the other parameters assume there is a valid config
        return;
    } else if (paramName == kOCIOParamInputSpaceName) {
        if (args.reason == OFX::eChangeUserEdit) {
            // if the inputspace doesn't correspond to a valid one, reset to default
            std::string inputSpace;
            _inputSpace->getValueAtTime(args.time, inputSpace);
            int inputSpaceIndex = _config->getIndexForColorSpace(inputSpace.c_str());
            if (inputSpaceIndex < 0) {
                if (args.reason == OFX::eChangeUserEdit) {
                    _parent->sendMessage(OFX::Message::eMessageWarning, "", std::string("Unknown OCIO colorspace \"")+inputSpace+"\"");
                }
                OCIO::ConstColorSpaceRcPtr colorspace = _config->getColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT);
                if (colorspace) {
                    inputSpace = colorspace->getName();
                    _inputSpace->setValue(inputSpace);
                } 
            }
        }
        inputCheck(args.time);
    }
#ifdef OFX_OCIO_CHOICE
    else if ( paramName == kOCIOParamInputSpaceChoiceName && args.reason == OFX::eChangeUserEdit) {
        int inputSpaceIndex;
        _inputSpaceChoice->getValueAtTime(args.time, inputSpaceIndex);
        std::string inputSpaceOld;
        _inputSpace->getValueAtTime(args.time, inputSpaceOld);
        std::string inputSpace = _config->getColorSpaceNameByIndex(inputSpaceIndex);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (inputSpace != inputSpaceOld) {
            _inputSpace->setValue(inputSpace);
        }
    }
#endif
    else if (paramName == kOCIOParamOutputSpaceName) {
        if (args.reason == OFX::eChangeUserEdit) {
            // if the outputspace doesn't correspond to a valid one, reset to default
            std::string outputSpace;
            _outputSpace->getValueAtTime(args.time, outputSpace);
            int outputSpaceIndex = _config->getIndexForColorSpace(outputSpace.c_str());
            if (outputSpaceIndex < 0) {
                if (args.reason == OFX::eChangeUserEdit) {
                    _parent->sendMessage(OFX::Message::eMessageWarning, "", std::string("Unknown OCIO colorspace \"")+outputSpace+"\"");
                }
                outputSpace = _config->getColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT)->getName();
                _outputSpace->setValue(outputSpace);
                outputSpaceIndex = _config->getIndexForColorSpace(outputSpace.c_str());
                assert(outputSpaceIndex >= 0);
            }
        }
        outputCheck(args.time);
    }
#ifdef OFX_OCIO_CHOICE
    else if ( paramName == kOCIOParamOutputSpaceChoiceName && args.reason == OFX::eChangeUserEdit) {
        int outputSpaceIndex;
        _outputSpaceChoice->getValueAtTime(args.time, outputSpaceIndex);
        std::string outputSpaceOld;
        _outputSpace->getValueAtTime(args.time, outputSpaceOld);
        std::string outputSpace = _config->getColorSpaceNameByIndex(outputSpaceIndex);
        _outputSpace->setValue(outputSpace);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (outputSpace != outputSpaceOld) {
            _outputSpace->setValue(outputSpace);
        }
    }
#endif // OFX_OCIO_CHOICE


#endif
}

#ifdef OFX_IO_USING_OCIO
std::string
GenericOCIO::getInputColorspace(double time) const
{
    std::string space;
    _inputSpace->getValueAtTime(time, space);
    return space;
}

std::string
GenericOCIO::getOutputColorspace(double time) const
{
    std::string space;
    _outputSpace->getValueAtTime(time, space);
    return space;
}
#endif


bool
GenericOCIO::hasColorspace(const char* name) const
{
#ifdef OFX_IO_USING_OCIO
    return _config && (bool)_config->getColorSpace(name);
#else
    return false;
#endif
}

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

#ifdef OFX_IO_USING_OCIO
static const char* colorSpaceName(OCIO_NAMESPACE::ConstConfigRcPtr config, const char* colorSpaceNameDefault)
{
    OpenColorIO::ConstColorSpaceRcPtr cs;
    if (!strcmp(colorSpaceNameDefault, "sRGB") || !strcmp(colorSpaceNameDefault, "srgb")) {
        if ((cs = config->getColorSpace("sRGB"))) {
            // nuke-default and blender
            return cs->getName();
        } else if ((cs = config->getColorSpace("rrt_Gamma2.2"))) {
            // rrt_Gamma2.2 in aces 0.7.1
            return cs->getName();
        } else if ((cs = config->getColorSpace("rrt_srgb"))) {
            // rrt_srgb in aces 0.1.1
            return cs->getName();
        } else if ((cs = config->getColorSpace("srgb8"))) {
            // srgb8 in spi-vfx
            return cs->getName();
        } else if ((cs = config->getColorSpace("vd16"))) {
            // vd16 in spi-anim
            return cs->getName();
        } else if ((cs = config->getColorSpace("VD16"))) {
            // VD16 in blender
            return cs->getName();
        }
    //} else if(!strcmp(inputSpaceNameDefault, "AdobeRGB") || !strcmp(inputSpaceNameDefault, "adobergb")) {
        // ???
    } else if (!strcmp(colorSpaceNameDefault, "Rec709") || !strcmp(colorSpaceNameDefault, "rec709")) {
        if ((cs = config->getColorSpace("Rec709"))) {
            // nuke-default
            return cs->getName();
        } else if ((cs = config->getColorSpace("nuke_rec709"))) {
            // blender
            return cs->getName();
        } else if ((cs = config->getColorSpace("rrt_rec709_full_100nits"))) {
            // rrt_rec709_full_100nits in aces 0.7.1
            return cs->getName();
        } else if ((cs = config->getColorSpace("rrt_rec709"))) {
            // rrt_rec709 in aces 0.1.1
            return cs->getName();
        } else if ((cs = config->getColorSpace("hd10"))) {
            // hd10 in spi-anim and spi-vfx
            return cs->getName();
        }
    } else if (!strcmp(colorSpaceNameDefault, "KodakLog") || !strcmp(colorSpaceNameDefault, "kodaklog")) {
        if ((cs = config->getColorSpace("Cineon"))) {
            // Cineon in nuke-default
            return cs->getName();
        } else if ((cs = config->getColorSpace("cineon"))) {
            // cineon in aces 0.7.1
            return cs->getName();
        } else if ((cs = config->getColorSpace("adx10"))) {
            // adx10 in aces 0.1.1
            return cs->getName();
        } else if ((cs = config->getColorSpace("lg10"))) {
            // lg10 in spi-vfx
            return cs->getName();
        } else if ((cs = config->getColorSpace("lm10"))) {
            // lm10 in spi-anim
            return cs->getName();
        } else {
            return "compositing_log"; // reasonable default
        }
    } else if (!strcmp(colorSpaceNameDefault, "Linear") || !strcmp(colorSpaceNameDefault, "linear")) {
        return "scene_linear";
        // lnf in spi-vfx
    } else if ((cs = config->getColorSpace(colorSpaceNameDefault))) {
        // maybe we're lucky
        return cs->getName();
    }
    // unlucky
    return colorSpaceNameDefault;
}
#endif

void
GenericOCIO::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor *page, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault)
{
#ifdef OFX_IO_USING_OCIO
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);

    char* file = std::getenv("OCIO");
    OCIO::ConstConfigRcPtr config;
    if (file != NULL) {
        //Add choices
        try {
            config = OCIO::Config::CreateFromFile(file);
            gWasOCIOEnvVarFound = true;
        } catch (OCIO::Exception &e) {
        }
    }
    std::string inputSpaceName, outputSpaceName;
    if (config) {
        inputSpaceName = colorSpaceName(config, inputSpaceNameDefault);
        outputSpaceName = colorSpaceName(config, outputSpaceNameDefault);
    }

    ////////// OCIO config file
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamConfigFileName);
        param->setLabels(kOCIOParamConfigFileLabel, kOCIOParamConfigFileLabel, kOCIOParamConfigFileLabel);
        param->setHint(kOCIOParamConfigFileHint);
        param->setStringType(OFX::eStringTypeFilePath);
        param->setFilePathExists(true);
        param->setAnimates(true);
        desc.addClipPreferencesSlaveParam(*param);
        // the OCIO config can only be set in a portable fashion using the environment variable.
        // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
        // disable it, and set the default from the env variable.
        assert(OFX::getImageEffectHostDescription());
        param->setEnabled(true);
        if (file == NULL) {
            param->setDefault("WARNING: Open an OCIO config file, or set an OCIO environnement variable");
        } else if (config) {
            param->setDefault(file);
        } else {
            std::string s("ERROR: Invalid OCIO configuration '");
            s += file;
            s += '\'';
            param->setDefault(s);
        }
        page->addChild(*param);
    }

    ///////////Input Color-space
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamInputSpaceName);
        param->setLabels(kOCIOParamInputSpaceLabel, kOCIOParamInputSpaceLabel, kOCIOParamInputSpaceLabel);
        param->setHint(kOCIOParamInputSpaceHint);
        param->setAnimates(true);
        if (config) {
            param->setDefault(inputSpaceName);
        } else {
            param->setEnabled(false);
        }
        page->addChild(*param);
    }

#ifdef OFX_OCIO_CHOICE
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kOCIOParamInputSpaceChoiceName);
        param->setLabels(kOCIOParamInputSpaceLabel, kOCIOParamInputSpaceLabel, kOCIOParamInputSpaceLabel);
        param->setHint(kOCIOParamInputSpaceHint);
        if (config) {
            buildChoiceMenu(config, param, inputSpaceName);
        } else {
            param->setEnabled(false);
            param->setIsSecret(true);
        }
        param->setAnimates(true);
        param->setEvaluateOnChange(false); // evaluate only when the StringParam is changed
        param->setIsPersistant(false); // don't save/serialize
        page->addChild(*param);
    }
#endif

    ///////////Output Color-space
    {OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamOutputSpaceName);
        param->setLabels(kOCIOParamOutputSpaceLabel, kOCIOParamOutputSpaceLabel, kOCIOParamOutputSpaceLabel);
        param->setHint(kOCIOParamOutputSpaceHint);
        param->setAnimates(true);
        if (config) {
            param->setDefault(outputSpaceName);
        } else {
            param->setEnabled(false);
        }
        page->addChild(*param);
    }
#ifdef OFX_OCIO_CHOICE
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kOCIOParamOutputSpaceChoiceName);
        param->setLabels(kOCIOParamOutputSpaceLabel, kOCIOParamOutputSpaceLabel, kOCIOParamOutputSpaceLabel);
        param->setHint(kOCIOParamOutputSpaceHint);
        if (config) {
            buildChoiceMenu(config, param, outputSpaceName);
        } else {
            param->setEnabled(false);
            param->setIsSecret(true);
        }
        param->setAnimates(true);
        param->setEvaluateOnChange(false); // evaluate only when the StringParam is changed
        param->setIsPersistant(false); // don't save/serialize
        page->addChild(*param);
    }
#endif

    OFX::PushButtonParamDescriptor* pb = desc.definePushButtonParam(kOCIOHelpButtonName);
    pb->setLabels(kOCIOHelpButtonLabel, kOCIOHelpButtonLabel, kOCIOHelpButtonLabel);
    pb->setHint(kOCIOHelpButtonHint);
    page->addChild(*pb);

#endif
}
