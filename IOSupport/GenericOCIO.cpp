/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2015 INRIA
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

#include "GenericOCIO.h"

#include <cstring>
#include <cstdlib>
#ifdef DEBUG
#include <cstdio>
#endif
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

// define to disable hiding parameters (useful for debugging)
//#define OFX_OCIO_NOSECRET

static std::string
trim(std::string const & str)
{
    const std::string whitespace = " \t\f\v\n\r";
    std::size_t first = str.find_first_not_of(whitespace);

    // If there is no non-whitespace character, both first and last will be std::string::npos (-1)
    // There is no point in checking both, since if either doesn't work, the
    // other won't work, either.
    if (first == std::string::npos) {
        return "";
    }

    std::size_t last  = str.find_last_not_of(whitespace);

    return str.substr(first, last - first + 1);
}

static std::string
whitespacify(std::string str)
{
    std::replace( str.begin(), str.end(), '\t', ' ');
    std::replace( str.begin(), str.end(), '\f', ' ');
    std::replace( str.begin(), str.end(), '\v', ' ');
    std::replace( str.begin(), str.end(), '\n', ' ');
    std::replace( str.begin(), str.end(), '\r', ' ');
    return str;
}

#ifdef OFX_IO_USING_OCIO
static const char* colorSpaceName(OCIO_NAMESPACE::ConstConfigRcPtr config, const char* colorSpaceNameDefault)
{
    OpenColorIO::ConstColorSpaceRcPtr cs;
    if (!strcmp(colorSpaceNameDefault, "sRGB") || !strcmp(colorSpaceNameDefault, "srgb")) {
        if ((cs = config->getColorSpace("sRGB"))) {
            // nuke-default and blender
            return cs->getName();
        } else if ((cs = config->getColorSpace("sRGB D65"))) {
            // blender-cycles
            return cs->getName();
        } else if ((cs = config->getColorSpace("sRGB (D60 sim.)"))) {
            // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
            return cs->getName();
        } else if ((cs = config->getColorSpace("out_srgbd60sim"))) {
            // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
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
        } else if ((cs = config->getColorSpace("Rec.709 - Full"))) {
            // out_rec709full or "Rec.709 - Full" in aces 1.0.0
            return cs->getName();
        } else if ((cs = config->getColorSpace("out_rec709full"))) {
            // out_rec709full or "Rec.709 - Full" in aces 1.0.0
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
        } else if ((cs = config->getColorSpace("REDlogFilm"))) {
            // REDlogFilm in aces 1.0.0
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
            return OCIO_NAMESPACE::ROLE_COMPOSITING_LOG; // reasonable default
        }
    } else if (!strcmp(colorSpaceNameDefault, "Linear") || !strcmp(colorSpaceNameDefault, "linear")) {
        return OCIO_NAMESPACE::ROLE_SCENE_LINEAR;
        // lnf in spi-vfx
    } else if ((cs = config->getColorSpace(colorSpaceNameDefault))) {
        // maybe we're lucky
        return cs->getName();
    }
    // unlucky
    return colorSpaceNameDefault;
}

static std::string
canonicalizeColorSpace(OCIO_NAMESPACE::ConstConfigRcPtr config, const std::string &csname)
{
    if (!config) {
        return csname;
    }
    const int defaultcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_DEFAULT);
    const int referencecs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_REFERENCE);
    const int datacs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_DATA);
    const int colorpickingcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COLOR_PICKING);
    const int scenelinearcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
    const int compositinglogcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COMPOSITING_LOG);
    const int colortimingcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_COLOR_TIMING);
    const int texturepaintcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_TEXTURE_PAINT);
    const int mattepaintcs = config->getIndexForColorSpace(OCIO_NAMESPACE::ROLE_MATTE_PAINT);

    int inputSpaceIndex = config->getIndexForColorSpace(csname.c_str());
    if (inputSpaceIndex == scenelinearcs) {
        return OCIO_NAMESPACE::ROLE_SCENE_LINEAR;
    } else if (inputSpaceIndex == defaultcs) {
        return OCIO_NAMESPACE::ROLE_DEFAULT;
    } else if (inputSpaceIndex == referencecs) {
        return OCIO_NAMESPACE::ROLE_REFERENCE;
    } else if (inputSpaceIndex == datacs) {
        return OCIO_NAMESPACE::ROLE_DATA;
    } else if (inputSpaceIndex == colorpickingcs) {
        return OCIO_NAMESPACE::ROLE_COLOR_PICKING;
    } else if (inputSpaceIndex == compositinglogcs) {
        return OCIO_NAMESPACE::ROLE_COMPOSITING_LOG;
    } else if (inputSpaceIndex == colortimingcs) {
        return OCIO_NAMESPACE::ROLE_COLOR_TIMING;
    } else if (inputSpaceIndex == texturepaintcs) {
        return OCIO_NAMESPACE::ROLE_TEXTURE_PAINT;
    } else if (inputSpaceIndex == mattepaintcs) {
        return OCIO_NAMESPACE::ROLE_MATTE_PAINT;
    }
    return csname;
}
#endif

GenericOCIO::GenericOCIO(OFX::ImageEffect* parent)
: _parent(parent)
, _created(false)
#ifdef OFX_IO_USING_OCIO
, _ocioConfigFileName()
, _ocioConfigFile(0)
, _inputSpace(0)
, _outputSpace(0)
#ifdef OFX_OCIO_CHOICE
, _choiceIsOk(true)
, _choiceFileName()
, _inputSpaceChoice(0)
, _outputSpaceChoice(0)
#endif
, _contextKey1(0)
, _contextValue1(0)
, _contextKey2(0)
, _contextValue2(0)
, _contextKey3(0)
, _contextValue3(0)
, _contextKey4(0)
, _contextValue4(0)
, _config()
#endif
{
#ifdef OFX_IO_USING_OCIO
    _ocioConfigFile = _parent->fetchStringParam(kOCIOParamConfigFile);
    if (_parent->paramExists(kOCIOParamInputSpace)) {
        _inputSpace = _parent->fetchStringParam(kOCIOParamInputSpace);
    }
    if (_parent->paramExists(kOCIOParamOutputSpace)) {
        _outputSpace = _parent->fetchStringParam(kOCIOParamOutputSpace);
    }
#ifdef OFX_OCIO_CHOICE
    _ocioConfigFile->getDefault(_choiceFileName);
    if (_inputSpace) {
        _inputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamInputSpaceChoice);
    }
    if (_outputSpace) {
        _outputSpaceChoice = _parent->fetchChoiceParam(kOCIOParamOutputSpaceChoice);
    }
#endif
    loadConfig();
#ifdef OFX_OCIO_CHOICE
    if (!_config) {
#     ifndef OFX_OCIO_NOSECRET
        if (_inputSpace) {
            _inputSpaceChoice->setIsSecret(true);
        }
        if (_outputSpace) {
            _outputSpaceChoice->setIsSecret(true);
        }
#     endif
    }
#endif
    if (_parent->paramExists(kOCIOParamContextKey1)) {
        _contextKey1 = _parent->fetchStringParam(kOCIOParamContextKey1);
        _contextValue1 = _parent->fetchStringParam(kOCIOParamContextValue1);
        _contextKey2 = _parent->fetchStringParam(kOCIOParamContextKey2);
        _contextValue2 = _parent->fetchStringParam(kOCIOParamContextValue2);
        _contextKey3 = _parent->fetchStringParam(kOCIOParamContextKey3);
        _contextValue3 = _parent->fetchStringParam(kOCIOParamContextValue3);
        _contextKey4 = _parent->fetchStringParam(kOCIOParamContextKey4);
        _contextValue4 = _parent->fetchStringParam(kOCIOParamContextValue4);
        assert(_contextKey1 && _contextKey2 && _contextKey3 && _contextKey4);
        assert(_contextValue1 && _contextValue2 && _contextValue3 && _contextValue4);
    }
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
                bool cascading,
                const std::string& name = "")
{
#ifdef DEBUG
    //printf("%p->resetOptions\n", (void*)choice);
#endif
    choice->resetOptions();
    assert(choice->getNOptions() == 0);
    if (!config) {
        return;
    }
    int def = -1;
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
        // set the default value, in case the GUI uses it
        if (!name.empty() && csname == name) {
            def = i;
        }
        OCIO_NAMESPACE::ConstColorSpaceRcPtr cs = config->getColorSpace(csname.c_str());
        if (cascading) {
            std::string family = config->getColorSpace(csname.c_str())->getFamily();
            if (!family.empty()) {
                csname = family + "/" + csname;
            }
        }
        std::string csdesc = cs ? cs->getDescription() : "(no colorspace)";
        csdesc = whitespacify(trim(csdesc));
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
#ifdef DEBUG
        //printf("%p->appendOption(\"%s\",\"%s\") (%d->%d options)\n", (void*)choice, csname.c_str(), msg.c_str(), i, i+1);
#endif
        assert(choice->getNOptions() == i);
        choice->appendOption(csname, msg);
        assert(choice->getNOptions() == i+1);
    }
    if (def != -1) {
        choice->setDefault(def);
    }
}
#endif
#endif

void
GenericOCIO::loadConfig()
{
#ifdef OFX_IO_USING_OCIO
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
        if (_inputSpace) {
            _inputSpace->setEnabled(false);
#         ifdef OFX_OCIO_CHOICE
            _inputSpaceChoice->setEnabled(false);
#         endif
        }
        if (_outputSpace) {
            _outputSpace->setEnabled(false);
#         ifdef OFX_OCIO_CHOICE
            _outputSpaceChoice->setEnabled(false);
#         endif
        }
    }
#ifdef OFX_OCIO_CHOICE
    if (_config) {
        if (gHostIsNatron) {
            // the choice menu can only be modified in Natron
            // Natron supports changing the entries in a choiceparam
            // Nuke (at least up to 8.0v3) does not
            if (_inputSpace) {
                buildChoiceMenu(_config, _inputSpaceChoice, _inputSpaceChoice->getIsCascading());
            }
            if (_outputSpace) {
                buildChoiceMenu(_config, _outputSpaceChoice, _outputSpaceChoice->getIsCascading());
            }
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
GenericOCIO::configIsDefault()
{
#ifdef OFX_IO_USING_OCIO
    std::string filename;
    _ocioConfigFile->getValue(filename);
    std::string defaultFilename;
    _ocioConfigFile->getDefault(defaultFilename);
    return (filename == defaultFilename);
#else
    return true;
#endif
}

#ifdef OFX_IO_USING_OCIO
OCIO::ConstContextRcPtr
GenericOCIO::getLocalContext(double time)
{
    OCIO::ConstContextRcPtr context = _config->getCurrentContext();
    OCIO::ContextRcPtr mutableContext;

    if (_contextKey1) {
        std::string contextKey1;
        _contextKey1->getValueAtTime(time, contextKey1);
        if (!contextKey1.empty()) {
            std::string contextValue1;
            _contextValue1->getValueAtTime(time, contextValue1);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar(contextKey1.c_str(), contextValue1.c_str());
        }
    }
    if (_contextKey2) {
        std::string contextKey2;
        _contextKey2->getValueAtTime(time, contextKey2);
        if (!contextKey2.empty()) {
            std::string contextValue2;
            _contextValue2->getValueAtTime(time, contextValue2);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar(contextKey2.c_str(), contextValue2.c_str());
        }
    }
    if (_contextKey3) {
        std::string contextKey3;
        _contextKey3->getValueAtTime(time, contextKey3);
        if (!contextKey3.empty()) {
            std::string contextValue3;
            _contextValue3->getValueAtTime(time, contextValue3);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar(contextKey3.c_str(), contextValue3.c_str());
        }
    }
    if (_contextKey4) {
        std::string contextKey4;
        _contextKey4->getValueAtTime(time, contextKey4);
        if (!contextKey4.empty()) {
            std::string contextValue4;
            _contextValue4->getValueAtTime(time, contextValue4);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar(contextKey4.c_str(), contextValue4.c_str());
        }
    }

    if (mutableContext) {
        context = mutableContext;
    }
    return context;
}
#endif

bool
GenericOCIO::isIdentity(double time)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        return true;
    }
    std::string inputSpace;
    getInputColorspaceAtTime(time, inputSpace);
    std::string outputSpace;
    getOutputColorspaceAtTime(time, outputSpace);
    if (inputSpace == outputSpace) {
        return true;
    }
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    _parent->clearPersistentMessage();
    try {
        // maybe the names are not the same, but it's still a no-op (e.g. "scene_linear" and "linear")
        OCIO::ConstContextRcPtr context = getLocalContext(time);//_config->getCurrentContext();
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
    if (!_config || !_inputSpace) {
        return;
    }
    if (!_choiceIsOk) {
        // choice menu is dirty, only use the text entry
        _inputSpace->setEnabled(true);
        _inputSpaceChoice->setEnabled(false);
#ifndef OFX_OCIO_NOSECRET
        _inputSpace->setIsSecret(false);
        _inputSpaceChoice->setIsSecret(true);
#endif
        return;
    }
    std::string inputSpaceName;
    getInputColorspaceAtTime(time, inputSpaceName);
    int inputSpaceIndex = _config->getIndexForColorSpace(inputSpaceName.c_str());
    if (inputSpaceIndex >= 0) {
        int inputSpaceIndexOld;
        _inputSpaceChoice->getValueAtTime(time, inputSpaceIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (inputSpaceIndexOld != inputSpaceIndex) {
            _inputSpaceChoice->setValue(inputSpaceIndex);
        }
        _inputSpace->setEnabled(false);
        _inputSpaceChoice->setEnabled(true);
#ifndef OFX_OCIO_NOSECRET
        _inputSpace->setIsSecret(true);
        _inputSpaceChoice->setIsSecret(false);
#endif
    } else {
        // the input space name is not valid
        _inputSpace->setEnabled(true);
        _inputSpaceChoice->setEnabled(false);
#ifndef OFX_OCIO_NOSECRET
        _inputSpace->setIsSecret(false);
        _inputSpaceChoice->setIsSecret(true);
#endif
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
    if (!_config || !_outputSpace) {
        return;
    }
    if (!_choiceIsOk) {
        // choice menu is dirty, only use the text entry
        _outputSpace->setEnabled(true);
        _outputSpaceChoice->setEnabled(false);
#ifndef OFX_OCIO_NOSECRET
        _outputSpace->setIsSecret(false);
        _outputSpaceChoice->setIsSecret(true);
#endif
        return;
    }
    std::string outputSpaceName;
    getOutputColorspaceAtTime(time, outputSpaceName);
    int outputSpaceIndex = _config->getIndexForColorSpace(outputSpaceName.c_str());
    if (outputSpaceIndex >= 0) {
        int outputSpaceIndexOld;
        _outputSpaceChoice->getValueAtTime(time, outputSpaceIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (outputSpaceIndexOld != outputSpaceIndex) {
            _outputSpaceChoice->setValue(outputSpaceIndex);
        }
        _outputSpace->setEnabled(false);
        _outputSpaceChoice->setEnabled(true);
#ifndef OFX_OCIO_NOSECRET
        _outputSpace->setIsSecret(true);
        _outputSpaceChoice->setIsSecret(false);
#endif
    } else {
        // the output space name is not valid
        _outputSpace->setEnabled(true);
        _outputSpaceChoice->setEnabled(false);
#ifndef OFX_OCIO_NOSECRET
        _outputSpace->setIsSecret(false);
        _outputSpaceChoice->setIsSecret(true);
#endif
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

    apply(time, renderWindow, (float*)img->getPixelData(), img->getBounds(), img->getPixelComponents(), img->getPixelComponentCount(), img->getRowBytes());
#endif
}


#ifdef OFX_IO_USING_OCIO
OCIO_NAMESPACE::ConstProcessorRcPtr
GenericOCIO::getProcessor()
{
    OFX::MultiThread::AutoMutex guard(_procMutex);
    return _proc;
};

void
GenericOCIO::setValues(const std::string& inputSpace, const std::string& outputSpace)
{
    return setValues(_config->getCurrentContext(), inputSpace.c_str(), outputSpace.c_str());
}

void
GenericOCIO::setValues(const OCIO_NAMESPACE::ConstContextRcPtr &context, const std::string& inputSpace, const std::string& outputSpace)
{
    OFX::MultiThread::AutoMutex guard(_procMutex);
    if (!_proc ||
        context != _procContext ||
        inputSpace != _procInputSpace ||
        outputSpace != _procOutputSpace) {
        _procContext = context;
        _procInputSpace = inputSpace;
        _procOutputSpace = outputSpace;
        _proc = _config->getProcessor(context, inputSpace.c_str(), outputSpace.c_str());
    }
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
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
            return;
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
#endif
}
#endif // OFX_IO_USING_OCIO


void
GenericOCIO::apply(double time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes)
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
        _parent->setPersistentMessage(OFX::Message::eMessageError, "","OCIO: render window outside of image bounds");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB) {
        _parent->setPersistentMessage(OFX::Message::eMessageError, "","OCIO: invalid components (only RGB and RGBA are supported)");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OCIOProcessor processor(*_parent);
    // set the images
    processor.setDstImg(pixelData, bounds, pixelComponents, pixelComponentCount, OFX::eBitDepthFloat, rowBytes);

    std::string inputSpace;
    getInputColorspaceAtTime(time, inputSpace);
    std::string outputSpace;
    getOutputColorspaceAtTime(time, outputSpace);
    OCIO::ConstContextRcPtr context = getLocalContext(time);//_config->getCurrentContext();
    setValues(context, inputSpace, outputSpace);
    processor.setProcessor(getProcessor());

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
    if (paramName == kOCIOParamConfigFile && args.reason != OFX::eChangeTime) {
        // compute canonical inputSpace and outputSpace before changing the config,
        // if different from inputSpace and outputSpace they must be set to the canonical value after changing ocio config
        std::string inputSpace;
        getInputColorspaceAtTime(args.time, inputSpace);
        std::string inputSpaceCanonical = canonicalizeColorSpace(_config, inputSpace);
        if (inputSpaceCanonical != inputSpace) {
            _inputSpace->setValue(inputSpaceCanonical);
        }
        if (_outputSpace) {
            std::string outputSpace;
            getOutputColorspaceAtTime(args.time, outputSpace);
            std::string outputSpaceCanonical = canonicalizeColorSpace(_config, outputSpace);
            if (outputSpaceCanonical != outputSpace) {
                _outputSpace->setValue(outputSpaceCanonical);
            }
        }
        
        loadConfig(); // re-load the new OCIO config
        //if inputspace or outputspace are not valid in the new config, reset them to "default"
        if (_config) {
            std::string inputSpaceName;
            getInputColorspaceAtTime(args.time, inputSpaceName);
            int inputSpaceIndex = _config->getIndexForColorSpace(inputSpaceName.c_str());
            if (inputSpaceIndex < 0) {
                _inputSpace->setValue(OCIO_NAMESPACE::ROLE_DEFAULT);
            }
        }
        inputCheck(args.time);
        if (_config && _outputSpace) {
            std::string outputSpaceName;
            getOutputColorspaceAtTime(args.time, outputSpaceName);
            int outputSpaceIndex = _config->getIndexForColorSpace(outputSpaceName.c_str());
            if (outputSpaceIndex < 0) {
                _outputSpace->setValue(OCIO_NAMESPACE::ROLE_DEFAULT);
            }
        }
        outputCheck(args.time);

        if (!_config && args.reason == OFX::eChangeUserEdit) {
            std::string filename;
            _ocioConfigFile->getValue(filename);
            _parent->sendMessage(OFX::Message::eMessageError, "", std::string("Cannot load OCIO config file \"") + filename + '"');
        }
    } else if (paramName == kOCIOHelpButton || paramName == kOCIOHelpLooksButton || paramName == kOCIOHelpDisplaysButton) {
        std::string msg = "OpenColorIO Help\n"
            "The OCIO configuration file can be set using the \"OCIO\" environment variable, which should contain the full path to the .ocio file.\n"
            "OpenColorIO version (compiled with / running with): " OCIO_VERSION "/";
        msg += OCIO_NAMESPACE::GetVersion();
        msg += '\n';
        if (_config) {
            std::string configdesc = _config->getDescription();
            configdesc = whitespacify(trim(configdesc));
            if ( configdesc.size() > 0 ) {
                msg += "\nThis OCIO configuration is ";
                msg += configdesc;
                msg += '\n';
            }
            msg += '\n';
            if (paramName == kOCIOHelpLooksButton) {
                msg += (_config->getNumLooks() <= 0 ? "No look available in this OCIO configuration.\n" : "Available looks in this OCIO Configuration (applied in the given colorspace):\n");
                for (int i = 0; i < _config->getNumLooks(); ++i) {
                    const char* lkname = _config->getLookNameByIndex(i);
                    OCIO_NAMESPACE::ConstLookRcPtr lk = _config->getLook(lkname);
                    msg += "- ";
                    msg += lkname;
                    std::string lkspace = lk->getProcessSpace();
                    msg += " (" + lkspace + ")\n";
                }
                msg += '\n';
            }
            if (paramName == kOCIOHelpDisplaysButton) {
                if (_config->getNumDisplays() <= 0) {
                    msg += "No display available in this OCIO configuration.\n";
                } else {
                    msg += "Available displays and views in this OCIO Configuration:\n";
                    std::string defaultdisplay = _config->getDefaultDisplay();
                    for (int i = 0; i < _config->getNumDisplays(); ++i) {
                        const char* display = _config->getDisplay(i);
                        msg += "- ";
                        msg += display;
                        if (display == defaultdisplay) {
                            msg += " (default)";
                        }
                        int numViews = _config->getNumViews(display);
                        if (numViews <= 0) {
                            msg += ", no view available.\n";
                        } else {
                            msg += ", views: ";
                            std::string defaultview = _config->getDefaultView(display);
                            for (int j = 0; j < numViews; ++j) {
                                const char* view = _config->getView(display, j);
                                msg += view;
                                if (view == defaultview) {
                                    msg += " (default)";
                                }
                                if (j < numViews-1) {
                                    msg += ", ";
                                }
                            }
                            msg += '\n';
                        }
                    }
                }
                msg += '\n';
            }
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
                std::string csdesc = cs ? cs->getDescription() : "(no colorspace)";
                csdesc = whitespacify(trim(csdesc));
                if ( !csdesc.empty() ) {
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
    } else if (paramName == kOCIOParamInputSpace) {
        assert(_inputSpace);
        if (args.reason == OFX::eChangeUserEdit) {
            // if the inputspace doesn't correspond to a valid one, reset to default.
            // first, canonicalize.
            std::string inputSpace;
            getInputColorspaceAtTime(args.time, inputSpace);
            std::string inputSpaceCanonical = canonicalizeColorSpace(_config, inputSpace);
            if (inputSpaceCanonical != inputSpace) {
                _inputSpace->setValue(inputSpaceCanonical);
                inputSpace = inputSpaceCanonical;
            }
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
    else if ( paramName == kOCIOParamInputSpaceChoice && args.reason == OFX::eChangeUserEdit) {
        assert(_inputSpace);
        int inputSpaceIndex;
        _inputSpaceChoice->getValueAtTime(args.time, inputSpaceIndex);
        std::string inputSpaceOld;
        getInputColorspaceAtTime(args.time, inputSpaceOld);
        std::string inputSpace = canonicalizeColorSpace(_config, _config->getColorSpaceNameByIndex(inputSpaceIndex));
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (inputSpace != inputSpaceOld) {
            _inputSpace->setValue(inputSpace);
        }
    }
#endif
    else if (paramName == kOCIOParamOutputSpace) {
        assert(_outputSpace);
        if (args.reason == OFX::eChangeUserEdit) {
            // if the outputspace doesn't correspond to a valid one, reset to default.
            // first, canonicalize.
            std::string outputSpace;
            getOutputColorspaceAtTime(args.time, outputSpace);
            std::string outputSpaceCanonical = canonicalizeColorSpace(_config, outputSpace);
            if (outputSpaceCanonical != outputSpace) {
                _outputSpace->setValue(outputSpaceCanonical);
                outputSpace = outputSpaceCanonical;
            }
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
    else if ( paramName == kOCIOParamOutputSpaceChoice && args.reason == OFX::eChangeUserEdit) {
        assert(_outputSpace);
        int outputSpaceIndex;
        _outputSpaceChoice->getValueAtTime(args.time, outputSpaceIndex);
        std::string outputSpaceOld;
        getOutputColorspaceAtTime(args.time, outputSpaceOld);
        std::string outputSpace = canonicalizeColorSpace(_config, _config->getColorSpaceNameByIndex(outputSpaceIndex));
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (outputSpace != outputSpaceOld) {
            _outputSpace->setValue(outputSpace);
        }
    }
#endif // OFX_OCIO_CHOICE


#endif
}

#ifdef OFX_IO_USING_OCIO
void
GenericOCIO::getInputColorspace(std::string &v)
{
    assert(_inputSpace);
    _inputSpace->getValue(v);
}

void
GenericOCIO::getInputColorspaceAtTime(double time, std::string &v)
{
    assert(_inputSpace);
    _inputSpace->getValueAtTime(time, v);
}

void
GenericOCIO::getOutputColorspace(std::string &v)
{
    assert(_outputSpace);
    _outputSpace->getValue(v);
}

void
GenericOCIO::getOutputColorspaceAtTime(double time, std::string &v)
{
    assert(_outputSpace);
    _outputSpace->getValueAtTime(time, v);
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
    assert(_inputSpace);
    _inputSpace->setValue(name);
#endif
}

void
GenericOCIO::setOutputColorspace(const char* name)
{
#ifdef OFX_IO_USING_OCIO
    assert(_outputSpace);
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
GenericOCIO::describeInContextInput(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor *page, const char* inputSpaceNameDefault, const char* inputSpaceLabel)
{
#ifdef OFX_IO_USING_OCIO
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

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
        inputSpaceName = canonicalizeColorSpace(config, colorSpaceName(config, inputSpaceNameDefault));
    }

    ////////// OCIO config file
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamConfigFile);
        param->setLabel(kOCIOParamConfigFileLabel);
        param->setHint(kOCIOParamConfigFileHint);
        param->setStringType(OFX::eStringTypeFilePath);
        param->setFilePathExists(true);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        // the OCIO config can only be set in a portable fashion using the environment variable.
        // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
        // disable it, and set the default from the env variable.
        assert(OFX::getImageEffectHostDescription());
        param->setEnabled(true);
        if (file == NULL) {
            param->setDefault("WARNING: Open an OCIO config file, or set the OCIO environnement variable");
        } else if (config) {
            param->setDefault(file);
        } else {
            std::string s("ERROR: Invalid OCIO configuration '");
            s += file;
            s += '\'';
            param->setDefault(s);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    ///////////Input Color-space
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamInputSpace);
        param->setLabel(inputSpaceLabel);
        param->setHint(kOCIOParamInputSpaceHint);
        param->setAnimates(true);
        if (config) {
            param->setDefault(inputSpaceName);
        } else {
            param->setEnabled(false);
        }
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_OCIO_CHOICE
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kOCIOParamInputSpaceChoice);
        param->setLabel(inputSpaceLabel);
        param->setHint(kOCIOParamInputSpaceHint);
        param->setCascading(OFX::getImageEffectHostDescription()->supportsCascadingChoices);
        if (config) {
            buildChoiceMenu(config, param, OFX::getImageEffectHostDescription()->supportsCascadingChoices, inputSpaceName);
        } else {
            param->setEnabled(false);
            //param->setIsSecret(true); // done in the plugin constructor
        }
        param->setAnimates(true);
        param->setEvaluateOnChange(false); // evaluate only when the StringParam is changed
        param->setIsPersistant(false); // don't save/serialize
        if (page) {
            page->addChild(*param);
        }
    }
#endif
#endif
}

void
GenericOCIO::describeInContextOutput(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor *page, const char* outputSpaceNameDefault, const char* outputSpaceLabel)
{
#ifdef OFX_IO_USING_OCIO
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

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
    std::string outputSpaceName;
    if (config) {
        outputSpaceName = canonicalizeColorSpace(config, colorSpaceName(config, outputSpaceNameDefault));
    }

    ///////////Output Color-space
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamOutputSpace);
        param->setLabel(outputSpaceLabel);
        param->setHint(kOCIOParamOutputSpaceHint);
        param->setAnimates(true);
        if (config) {
            param->setDefault(outputSpaceName);
        } else {
            param->setEnabled(false);
        }
        if (page) {
            page->addChild(*param);
        }
    }
#ifdef OFX_OCIO_CHOICE
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kOCIOParamOutputSpaceChoice);
        param->setLabel(outputSpaceLabel);
        param->setHint(kOCIOParamOutputSpaceHint);
        param->setCascading(OFX::getImageEffectHostDescription()->supportsCascadingChoices);
        if (config) {
            buildChoiceMenu(config, param, OFX::getImageEffectHostDescription()->supportsCascadingChoices, outputSpaceName);
        } else {
            param->setEnabled(false);
            //param->setIsSecret(true); // done in the plugin constructor
        }
        param->setAnimates(true);
        param->setEvaluateOnChange(false); // evaluate only when the StringParam is changed
        param->setIsPersistant(false); // don't save/serialize
        if (page) {
            page->addChild(*param);
        }
    }
#endif
#endif
}

void
GenericOCIO::describeInContextContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor *page)
{
#ifdef OFX_IO_USING_OCIO
    OFX::GroupParamDescriptor* group = desc.defineGroupParam(kOCIOParamContext);
    group->setHint(kOCIOParamContextHint);
    group->setOpen(false);

    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey1);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue1);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey2);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue2);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey3);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue3);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey4);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue4);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    if (page) {
        page->addChild(*group);
    }
#endif
}

