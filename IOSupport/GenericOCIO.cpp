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

#include "GenericOCIO.h"

/*
   http://opencolorio.org/userguide/config_syntax.html#roles

   A description of all roles. Note that applications may interpret or use these differently.

   color_picking - Colors in a color-selection UI can be displayed in this space, while selecting colors in a different working space (e.g scene_linear or texture_paint)
   color_timing - colorspace used for applying color corrections, e.g user-specified grade within an image viewer (if the application uses the DisplayTransform::setDisplayCC API method)
   compositing_log - a log colorspace used for certain processing operations (plate resizing, pulling keys, degrain, etc). Used by the OCIOLogConvert Nuke node.
   data - used when writing data outputs such as normals, depth data, and other “non color” data. The colorspace in this role should typically have data: true specified, so no color transforms are applied.
   default - when strictparsing: false, this colorspace is used as a fallback. If not defined, the scene_linear role is used
   matte_paint - Colorspace which matte-paintings are created in (for more information, see the guide on baking ICC profiles for Photoshop, and spi-vfx)
   reference - Colorspace used for reference imagery (e.g sRGB images from the internet)
   scene_linear - The scene-referred linear-to-light colorspace, typically used as reference space (see Terminology)
   texture_paint - Similar to matte_paint but for painting textures for 3D objects (see the description of texture painting in SPI’s pipeline)
 */

#include <cstring>
#include <cstdlib>
#ifdef DEBUG
#include <cstdio>
#define DBG(x) x
#else
#define DBG(x) (void)0
#endif
#include <string>
#include <stdexcept>
#include <ofxsParam.h>
#include <ofxsImageEffect.h>
#include <ofxsLog.h>
#include <ofxNatron.h>
#include "ofxsMacros.h"

#ifdef OFX_IO_USING_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

using std::string;

NAMESPACE_OFX_ENTER
    NAMESPACE_OFX_IO_ENTER

#ifdef OFX_IO_USING_OCIO
static bool gWasOCIOEnvVarFound = false;
static bool gHostIsNatron   = false;
#endif


// define to disable hiding parameters (useful for debugging)
//#define OFX_OCIO_NOSECRET

static string
trim(string const & str)
{
    const string whitespace = " \t\f\v\n\r";

    std::size_t first = str.find_first_not_of(whitespace);

    // If there is no non-whitespace character, both first and last will be string::npos (-1)
    // There is no point in checking both, since if either doesn't work, the
    // other won't work, either.
    if (first == string::npos) {
        return "";
    }

    std::size_t last  = str.find_last_not_of(whitespace);

    return str.substr(first, last - first + 1);
}

static string
whitespacify(string str)
{
    std::replace( str.begin(), str.end(), '\t', ' ');
    std::replace( str.begin(), str.end(), '\f', ' ');
    std::replace( str.begin(), str.end(), '\v', ' ');
    std::replace( str.begin(), str.end(), '\n', ' ');
    std::replace( str.begin(), str.end(), '\r', ' ');

    return str;
}

#ifdef OFX_IO_USING_OCIO
static const char*
colorSpaceName(OCIO::ConstConfigRcPtr config,
               const char* colorSpaceNameDefault)
{
    OpenColorIO::ConstColorSpaceRcPtr cs;
    if ( !strcmp(colorSpaceNameDefault, "sRGB") || !strcmp(colorSpaceNameDefault, "srgb") ) {
        if ( ( cs = config->getColorSpace("sRGB") ) ) {
            // nuke-default, blender, natron
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("sRGB D65") ) ) {
            // blender-cycles
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("sRGB (D60 sim.)") ) ) {
            // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("out_srgbd60sim") ) ) {
            // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("rrt_Gamma2.2") ) ) {
            // rrt_Gamma2.2 in aces 0.7.1
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("rrt_srgb") ) ) {
            // rrt_srgb in aces 0.1.1
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("srgb8") ) ) {
            // srgb8 in spi-vfx
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("vd16") ) ) {
            // vd16 in spi-anim
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("VD16") ) ) {
            // VD16 in blender
            return cs->getName();
        }
    } else if ( !strcmp(colorSpaceNameDefault, "AdobeRGB") || !strcmp(colorSpaceNameDefault, "adobergb") ) {
        if ( ( cs = config->getColorSpace("AdobeRGB") ) ) {
            // natron
            return cs->getName();
        }
    } else if ( !strcmp(colorSpaceNameDefault, "Rec709") || !strcmp(colorSpaceNameDefault, "rec709") ) {
        if ( ( cs = config->getColorSpace("Rec709") ) ) {
            // nuke-default
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("nuke_rec709") ) ) {
            // blender
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("Rec.709 - Full") ) ) {
            // out_rec709full or "Rec.709 - Full" in aces 1.0.0
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("out_rec709full") ) ) {
            // out_rec709full or "Rec.709 - Full" in aces 1.0.0
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("rrt_rec709_full_100nits") ) ) {
            // rrt_rec709_full_100nits in aces 0.7.1
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("rrt_rec709") ) ) {
            // rrt_rec709 in aces 0.1.1
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("hd10") ) ) {
            // hd10 in spi-anim and spi-vfx
            return cs->getName();
        }
    } else if ( !strcmp(colorSpaceNameDefault, "KodakLog") || !strcmp(colorSpaceNameDefault, "kodaklog") ) {
        if ( ( cs = config->getColorSpace("Cineon") ) ) {
            // Cineon in nuke-default
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("REDlogFilm") ) ) {
            // REDlogFilm in aces 1.0.0
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("cineon") ) ) {
            // cineon in aces 0.7.1
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("adx10") ) ) {
            // adx10 in aces 0.1.1
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("lg10") ) ) {
            // lg10 in spi-vfx
            return cs->getName();
        } else if ( ( cs = config->getColorSpace("lm10") ) ) {
            // lm10 in spi-anim
            return cs->getName();
        } else {
            return OCIO::ROLE_COMPOSITING_LOG; // reasonable default
        }
    } else if ( !strcmp(colorSpaceNameDefault, "Linear") || !strcmp(colorSpaceNameDefault, "linear") ) {
        return OCIO::ROLE_SCENE_LINEAR;
        // lnf in spi-vfx
    } else if ( ( cs = config->getColorSpace(colorSpaceNameDefault) ) ) {
        // maybe we're lucky
        return cs->getName();
    }

    // unlucky
    return colorSpaceNameDefault;
} // colorSpaceName

static string
canonicalizeColorSpace(OCIO::ConstConfigRcPtr config,
                       const string &csname)
{
    if (!config) {
        return csname;
    }
    const int defaultcs = config->getIndexForColorSpace(OCIO::ROLE_DEFAULT);
    const int referencecs = config->getIndexForColorSpace(OCIO::ROLE_REFERENCE);
    const int datacs = config->getIndexForColorSpace(OCIO::ROLE_DATA);
    const int colorpickingcs = config->getIndexForColorSpace(OCIO::ROLE_COLOR_PICKING);
    const int scenelinearcs = config->getIndexForColorSpace(OCIO::ROLE_SCENE_LINEAR);
    const int compositinglogcs = config->getIndexForColorSpace(OCIO::ROLE_COMPOSITING_LOG);
    const int colortimingcs = config->getIndexForColorSpace(OCIO::ROLE_COLOR_TIMING);
    const int texturepaintcs = config->getIndexForColorSpace(OCIO::ROLE_TEXTURE_PAINT);
    const int mattepaintcs = config->getIndexForColorSpace(OCIO::ROLE_MATTE_PAINT);
    int inputSpaceIndex = config->getIndexForColorSpace( csname.c_str() );
    if (inputSpaceIndex == scenelinearcs) {
        return OCIO::ROLE_SCENE_LINEAR;
    } else if (inputSpaceIndex == defaultcs) {
        return OCIO::ROLE_DEFAULT;
    } else if (inputSpaceIndex == referencecs) {
        return OCIO::ROLE_REFERENCE;
    } else if (inputSpaceIndex == datacs) {
        return OCIO::ROLE_DATA;
    } else if (inputSpaceIndex == colorpickingcs) {
        return OCIO::ROLE_COLOR_PICKING;
    } else if (inputSpaceIndex == compositinglogcs) {
        return OCIO::ROLE_COMPOSITING_LOG;
    } else if (inputSpaceIndex == colortimingcs) {
        return OCIO::ROLE_COLOR_TIMING;
    } else if (inputSpaceIndex == texturepaintcs) {
        return OCIO::ROLE_TEXTURE_PAINT;
    } else if (inputSpaceIndex == mattepaintcs) {
        return OCIO::ROLE_MATTE_PAINT;
    }

    return csname;
}

#endif // ifdef OFX_IO_USING_OCIO

GenericOCIO::GenericOCIO(ImageEffect* parent)
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
    if ( _parent->paramExists(kOCIOParamInputSpace) ) {
        _inputSpace = _parent->fetchStringParam(kOCIOParamInputSpace);
    }
    if ( _parent->paramExists(kOCIOParamOutputSpace) ) {
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
            _inputSpaceChoice->setIsSecretAndDisabled(true);
        }
        if (_outputSpace) {
            _outputSpaceChoice->setIsSecretAndDisabled(true);
        }
#     endif
    }
#endif
    if ( _parent->paramExists(kOCIOParamContextKey1) ) {
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

// ChoiceParamType may be ChoiceParamDescriptor or ChoiceParam
template <typename ChoiceParamType>
static void
buildChoiceMenu(OCIO::ConstConfigRcPtr config,
                ChoiceParamType* choice,
                bool cascading,
                const string& name = "")
{
    //DBG(std::printf("%p->resetOptions\n", (void*)choice));
    choice->resetOptions();
    assert(choice->getNOptions() == 0);
    if (!config) {
        return;
    }
    int def = -1;
    int defaultcs = config->getIndexForColorSpace(OCIO::ROLE_DEFAULT);
    int referencecs = config->getIndexForColorSpace(OCIO::ROLE_REFERENCE);
    int datacs = config->getIndexForColorSpace(OCIO::ROLE_DATA);
    int colorpickingcs = config->getIndexForColorSpace(OCIO::ROLE_COLOR_PICKING);
    int scenelinearcs = config->getIndexForColorSpace(OCIO::ROLE_SCENE_LINEAR);
    int compositinglogcs = config->getIndexForColorSpace(OCIO::ROLE_COMPOSITING_LOG);
    int colortimingcs = config->getIndexForColorSpace(OCIO::ROLE_COLOR_TIMING);
    int texturepaintcs = config->getIndexForColorSpace(OCIO::ROLE_TEXTURE_PAINT);
    int mattepaintcs = config->getIndexForColorSpace(OCIO::ROLE_MATTE_PAINT);
    for (int i = 0; i < config->getNumColorSpaces(); ++i) {
        string csname = config->getColorSpaceNameByIndex(i);
        string msg;
        // set the default value, in case the GUI uses it
        if ( !name.empty() && (csname == name) ) {
            def = i;
        }
        OCIO::ConstColorSpaceRcPtr cs = config->getColorSpace( csname.c_str() );
        if (cascading) {
            string family = config->getColorSpace( csname.c_str() )->getFamily();
            if ( !family.empty() ) {
                csname = family + "/" + csname;
            }
        }
        string csdesc = cs ? cs->getDescription() : "(no colorspace)";
        csdesc = whitespacify( trim(csdesc) );
        int csdesclen = csdesc.size();
        if (csdesclen > 0) {
            msg += csdesc;
        }
        bool first = true;
        int roles = 0;
        if (i == defaultcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_DEFAULT;
            first = false;
            ++roles;
        }
        if (i == referencecs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_REFERENCE;
            first = false;
            ++roles;
        }
        if (i == datacs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_DATA;
            first = false;
            ++roles;
        }
        if (i == colorpickingcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_COLOR_PICKING;
            first = false;
            ++roles;
        }
        if (i == scenelinearcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_SCENE_LINEAR;
            first = false;
            ++roles;
        }
        if (i == compositinglogcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_COMPOSITING_LOG;
            first = false;
            ++roles;
        }
        if (i == colortimingcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_COLOR_TIMING;
            first = false;
            ++roles;
        }
        if (i == texturepaintcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_TEXTURE_PAINT;
            first = false;
            ++roles;
        }
        if (i == mattepaintcs) {
            msg += first ? " (" : ", ";
            msg += OCIO::ROLE_MATTE_PAINT;
            first = false;
            ++roles;
        }
        if (roles > 0) {
            msg += ')';
        }
        //DBG(printf("%p->appendOption(\"%s\",\"%s\") (%d->%d options)\n", (void*)choice, csname.c_str(), msg.c_str(), i, i+1));
        assert(choice->getNOptions() == i);
        choice->appendOption(csname, msg);
        assert(choice->getNOptions() == i + 1);
    }
    if (def != -1) {
        choice->setDefault(def);
    }
} // buildChoiceMenu

#endif // ifdef OFX_OCIO_CHOICE
#endif // ifdef OFX_IO_USING_OCIO

void
GenericOCIO::loadConfig()
{
#ifdef OFX_IO_USING_OCIO
    string filename;
    _ocioConfigFile->getValue(filename);

    if (filename == _ocioConfigFileName) {
        return;
    }
    _config.reset();
    try {
        _ocioConfigFileName = filename;
        _config = OCIO::Config::CreateFromFile( _ocioConfigFileName.c_str() );
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
                buildChoiceMenu( _config, _inputSpaceChoice, _inputSpaceChoice->getIsCascading() );
            }
            if (_outputSpace) {
                buildChoiceMenu( _config, _outputSpaceChoice, _outputSpaceChoice->getIsCascading() );
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
} // GenericOCIO::loadConfig

bool
GenericOCIO::configIsDefault() const
{
#ifdef OFX_IO_USING_OCIO
    string filename;
    _ocioConfigFile->getValue(filename);
    string defaultFilename;
    _ocioConfigFile->getDefault(defaultFilename);

    return (filename == defaultFilename);
#else

    return true;
#endif
}

#ifdef OFX_IO_USING_OCIO
OCIO::ConstContextRcPtr
GenericOCIO::getLocalContext(double time) const
{
    OCIO::ConstContextRcPtr context = _config->getCurrentContext();
    OCIO::ContextRcPtr mutableContext;

    if (_contextKey1) {
        string contextKey1;
        _contextKey1->getValueAtTime(time, contextKey1);
        if ( !contextKey1.empty() ) {
            string contextValue1;
            _contextValue1->getValueAtTime(time, contextValue1);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar( contextKey1.c_str(), contextValue1.c_str() );
        }
    }
    if (_contextKey2) {
        string contextKey2;
        _contextKey2->getValueAtTime(time, contextKey2);
        if ( !contextKey2.empty() ) {
            string contextValue2;
            _contextValue2->getValueAtTime(time, contextValue2);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar( contextKey2.c_str(), contextValue2.c_str() );
        }
    }
    if (_contextKey3) {
        string contextKey3;
        _contextKey3->getValueAtTime(time, contextKey3);
        if ( !contextKey3.empty() ) {
            string contextValue3;
            _contextValue3->getValueAtTime(time, contextValue3);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar( contextKey3.c_str(), contextValue3.c_str() );
        }
    }
    if (_contextKey4) {
        string contextKey4;
        _contextKey4->getValueAtTime(time, contextKey4);
        if ( !contextKey4.empty() ) {
            string contextValue4;
            _contextValue4->getValueAtTime(time, contextValue4);

            if (!mutableContext) {
                mutableContext = context->createEditableCopy();
            }
            mutableContext->setStringVar( contextKey4.c_str(), contextValue4.c_str() );
        }
    }

    if (mutableContext) {
        context = mutableContext;
    }

    return context;
} // GenericOCIO::getLocalContext

#endif // ifdef OFX_IO_USING_OCIO

bool
GenericOCIO::isIdentity(double time) const
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    if (!_config) {
        string filename;
        _ocioConfigFile->getValue(filename);
        _parent->setPersistentMessage( Message::eMessageError, "", "Invalid OCIO config. file \"" + filename + "\"" );
        throwSuiteStatusException(kOfxStatFailed);

        return false;
    }
    string inputSpace;
    getInputColorspaceAtTime(time, inputSpace);
    string outputSpace;
    getOutputColorspaceAtTime(time, outputSpace);
    if (inputSpace == outputSpace) {
        return true;
    }
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    _parent->clearPersistentMessage();
    try {
        // maybe the names are not the same, but it's still a no-op (e.g. "scene_linear" and "linear")
        OCIO::ConstContextRcPtr context = getLocalContext(time);//_config->getCurrentContext();
        OCIO::ConstProcessorRcPtr proc = _config->getProcessor( context, inputSpace.c_str(), outputSpace.c_str() );

        return proc->isNoOp();
    } catch (const std::exception& e) {
        _parent->setPersistentMessage( Message::eMessageError, "", e.what() );
        throwSuiteStatusException(kOfxStatFailed);

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
#ifdef OFX_OCIO_NOSECRET
        _inputSpace->setEnabled(true);
        _inputSpaceChoice->setEnabled(false);
#else
        _inputSpace->setIsSecretAndDisabled(false);
        _inputSpaceChoice->setIsSecretAndDisabled(true);
#endif

        return;
    }
    string inputSpaceName;
    getInputColorspaceAtTime(time, inputSpaceName);
    int inputSpaceIndex = _config->getIndexForColorSpace( inputSpaceName.c_str() );
    if (inputSpaceIndex >= 0) {
        int inputSpaceIndexOld;
        _inputSpaceChoice->getValueAtTime(time, inputSpaceIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (inputSpaceIndexOld != inputSpaceIndex) {
            _inputSpaceChoice->setValue(inputSpaceIndex);
        }
#ifdef OFX_OCIO_NOSECRET
        _inputSpace->setEnabled(false);
        _inputSpaceChoice->setEnabled(true);
#else
        _inputSpace->setIsSecretAndDisabled(true);
        _inputSpaceChoice->setIsSecretAndDisabled(false);
#endif
    } else {
        // the input space name is not valid
#ifdef OFX_OCIO_NOSECRET
        _inputSpace->setEnabled(true);
        _inputSpaceChoice->setEnabled(false);
#else
        _inputSpace->setIsSecretAndDisabled(false);
        _inputSpaceChoice->setIsSecretAndDisabled(true);
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
#ifdef OFX_OCIO_NOSECRET
        _outputSpace->setEnabled(true);
        _outputSpaceChoice->setEnabled(false);
#else
        _outputSpace->setIsSecretAndDisabled(false);
        _outputSpaceChoice->setIsSecretAndDisabled(true);
#endif

        return;
    }
    string outputSpaceName;
    getOutputColorspaceAtTime(time, outputSpaceName);
    int outputSpaceIndex = _config->getIndexForColorSpace( outputSpaceName.c_str() );
    if (outputSpaceIndex >= 0) {
        int outputSpaceIndexOld;
        _outputSpaceChoice->getValueAtTime(time, outputSpaceIndexOld);
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (outputSpaceIndexOld != outputSpaceIndex) {
            _outputSpaceChoice->setValue(outputSpaceIndex);
        }
#ifdef OFX_OCIO_NOSECRET
        _outputSpace->setEnabled(false);
        _outputSpaceChoice->setEnabled(true);
#else
        _outputSpace->setIsSecretAndDisabled(true);
        _outputSpaceChoice->setIsSecretAndDisabled(false);
#endif
    } else {
        // the output space name is not valid
#ifdef OFX_OCIO_NOSECRET
        _outputSpace->setEnabled(true);
        _outputSpaceChoice->setEnabled(false);
#else
        _outputSpace->setIsSecretAndDisabled(false);
        _outputSpaceChoice->setIsSecretAndDisabled(true);
#endif
    }
#endif
#endif
}

void
GenericOCIO::apply(double time,
                   const OfxRectI& renderWindow,
                   Image* img)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    BitDepthEnum bitDepth = img->getPixelDepth();
    if (bitDepth != eBitDepthFloat) {
        throw std::runtime_error("OCIO: invalid pixel depth (only float is supported)");
    }

    apply( time, renderWindow, (float*)img->getPixelData(), img->getBounds(), img->getPixelComponents(), img->getPixelComponentCount(), img->getRowBytes() );
#endif
}

#ifdef OFX_IO_USING_OCIO
OCIO::ConstProcessorRcPtr
GenericOCIO::getProcessor() const
{
    AutoMutex guard(_procMutex);

    return _proc;
};
void
GenericOCIO::setValues(const string& inputSpace,
                       const string& outputSpace)
{
    return setValues( _config->getCurrentContext(), inputSpace.c_str(), outputSpace.c_str() );
}

void
GenericOCIO::setValues(const OCIO::ConstContextRcPtr &context,
                       const string& inputSpace,
                       const string& outputSpace)
{
    AutoMutex guard(_procMutex);

    if ( !_proc ||
         ( context != _procContext) ||
         ( inputSpace != _procInputSpace) ||
         ( outputSpace != _procOutputSpace) ) {
        _procContext = context;
        _procInputSpace = inputSpace;
        _procOutputSpace = outputSpace;
        _proc = _config->getProcessor( context, inputSpace.c_str(), outputSpace.c_str() );
    }
}

void
OCIOProcessor::multiThreadProcessImages(OfxRectI renderWindow)
{
    assert(_dstBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= _dstBounds.x2);
    assert(_dstBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= _dstBounds.y2);
    // Ensure there are pixels to render otherwise OCIO::PackedImageDesc will throw an exception.
    if ( (renderWindow.y2 <= renderWindow.y1) || (renderWindow.x2 <= renderWindow.x1) ) {
        return;
    }
#ifdef OFX_IO_USING_OCIO
    if (!_proc) {
        throw std::logic_error("OCIO configuration not loaded");
    }
    int numChannels;
    int pixelBytes;
    switch (_dstPixelComponents) {
    case ePixelComponentRGBA:
        numChannels = 4;
        break;
    case ePixelComponentRGB:
        numChannels = 3;
        break;
    //case ePixelComponentAlpha: pixelBytes = 1; break;
    default:
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    pixelBytes = numChannels * sizeof(float);
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - _dstBounds.y1) * _dstRowBytes + (size_t)(renderWindow.x1 - _dstBounds.x1) * pixelBytes;
    float *pix = (float *) ( ( (char *) _dstPixelData ) + pixelDataOffset ); // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
    try {
        if (_proc) {
            OCIO::PackedImageDesc img(pix, renderWindow.x2 - renderWindow.x1, renderWindow.y2 - renderWindow.y1, numChannels, sizeof(float), pixelBytes, _dstRowBytes);
            _proc->apply(img);
        }
    } catch (OCIO::Exception &e) {
        _instance->setPersistentMessage( Message::eMessageError, "", string("OpenColorIO error: ") + e.what() );
        throw std::runtime_error( string("OpenColorIO error: ") + e.what() );
    }
#endif
}

#endif // OFX_IO_USING_OCIO

#ifdef OFX_IO_USING_OCIO
OCIO::ConstProcessorRcPtr
GenericOCIO::getOrCreateProcessor(double time)
{
    if (!_config) {
        return OCIO::ConstProcessorRcPtr();
    }
    string inputSpace;
    getInputColorspaceAtTime(time, inputSpace);
    string outputSpace;
    getOutputColorspaceAtTime(time, outputSpace);
    OCIO::ConstContextRcPtr context = getLocalContext(time);//_config->getCurrentContext();
    setValues(context, inputSpace, outputSpace);

    return getProcessor();
}

#endif // OFX_IO_USING_OCIO

void
GenericOCIO::apply(double time,
                   const OfxRectI& renderWindow,
                   float *pixelData,
                   const OfxRectI& bounds,
                   PixelComponentEnum pixelComponents,
                   int pixelComponentCount,
                   int rowBytes)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO

    if (!_created) {
        return;
    }

    if ( isIdentity(time) ) {
        return;
    }
    // are we in the image bounds
    if ( (renderWindow.x1 < bounds.x1) || (renderWindow.x1 >= bounds.x2) || (renderWindow.y1 < bounds.y1) || (renderWindow.y1 >= bounds.y2) ||
         ( renderWindow.x2 <= bounds.x1) || ( renderWindow.x2 > bounds.x2) || ( renderWindow.y2 <= bounds.y1) || ( renderWindow.y2 > bounds.y2) ) {
        _parent->setPersistentMessage(Message::eMessageError, "", "OCIO: render window outside of image bounds");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (pixelComponents != ePixelComponentRGBA) && (pixelComponents != ePixelComponentRGB) ) {
        _parent->setPersistentMessage(Message::eMessageError, "", "OCIO: invalid components (only RGB and RGBA are supported)");
        throwSuiteStatusException(kOfxStatFailed);
    }

    OCIO::ConstProcessorRcPtr proc = getOrCreateProcessor(time);
    if (!proc) {
        _parent->setPersistentMessage( Message::eMessageError, "", "Cannot create OCIO processor" );
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    OCIOProcessor processor(*_parent);
    // set the images
    processor.setDstImg(pixelData, bounds, pixelComponents, pixelComponentCount, eBitDepthFloat, rowBytes);

    processor.setProcessor(proc);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
#endif
}

void
GenericOCIO::changedParam(const InstanceChangedArgs &args,
                          const string &paramName)
{
    assert(_created);
#ifdef OFX_IO_USING_OCIO
    if ( (paramName == kOCIOParamConfigFile) && (args.reason != eChangeTime) ) {
        // compute canonical inputSpace and outputSpace before changing the config,
        // if different from inputSpace and outputSpace they must be set to the canonical value after changing ocio config
        string inputSpace;
        getInputColorspaceAtTime(args.time, inputSpace);
        string inputSpaceCanonical = canonicalizeColorSpace(_config, inputSpace);
        if (inputSpaceCanonical != inputSpace) {
            _inputSpace->setValue(inputSpaceCanonical);
        }
        if (_outputSpace) {
            string outputSpace;
            getOutputColorspaceAtTime(args.time, outputSpace);
            string outputSpaceCanonical = canonicalizeColorSpace(_config, outputSpace);
            if (outputSpaceCanonical != outputSpace) {
                _outputSpace->setValue(outputSpaceCanonical);
            }
        }

        loadConfig(); // re-load the new OCIO config
        //if inputspace or outputspace are not valid in the new config, reset them to "default"
        if (_config) {
            string inputSpaceName;
            getInputColorspaceAtTime(args.time, inputSpaceName);
            int inputSpaceIndex = _config->getIndexForColorSpace( inputSpaceName.c_str() );
            if (inputSpaceIndex < 0) {
                OCIO::ConstColorSpaceRcPtr cs;
                if (!cs) {
                    cs = _config->getColorSpace(OCIO::ROLE_DEFAULT);
                }
                if (!cs) {
                    // no default colorspace, fallback to the first one
                    cs = _config->getColorSpace( _config->getColorSpaceNameByIndex(0) );
                }
                inputSpaceName = cs ? cs->getName() : OCIO::ROLE_DEFAULT;
                _inputSpace->setValue(inputSpaceName);
            }
        }
        inputCheck(args.time);
        if (_config && _outputSpace) {
            string outputSpaceName;
            getOutputColorspaceAtTime(args.time, outputSpaceName);
            int outputSpaceIndex = _config->getIndexForColorSpace( outputSpaceName.c_str() );
            if (outputSpaceIndex < 0) {
                OCIO::ConstColorSpaceRcPtr cs;
                if (!cs) {
                    cs = _config->getColorSpace(OCIO::ROLE_DEFAULT);
                }
                if (!cs) {
                    // no default colorspace, fallback to the first one
                    cs = _config->getColorSpace( _config->getColorSpaceNameByIndex(0) );
                }
                outputSpaceName = cs ? cs->getName() : OCIO::ROLE_DEFAULT;
                _outputSpace->setValue(OCIO::ROLE_DEFAULT);
            }
        }
        outputCheck(args.time);

        if ( !_config && (args.reason == eChangeUserEdit) ) {
            string filename;
            _ocioConfigFile->getValue(filename);
            _parent->sendMessage(Message::eMessageError, "", string("Cannot load OCIO config file \"") + filename + '"');
        }
    } else if ( (paramName == kOCIOHelpButton) || (paramName == kOCIOHelpLooksButton) || (paramName == kOCIOHelpDisplaysButton) ) {
        string msg = "OpenColorIO Help\n"
                     "The OCIO configuration file can be set using the \"OCIO\" environment variable, which should contain the full path to the .ocio file.\n"
                     "OpenColorIO version (compiled with / running with): " OCIO_VERSION "/";
        msg += OCIO::GetVersion();
        msg += '\n';
        if (_config) {
            string configdesc = _config->getDescription();
            configdesc = whitespacify( trim(configdesc) );
            if (configdesc.size() > 0) {
                msg += "\nThis OCIO configuration is ";
                msg += configdesc;
                msg += '\n';
            }
            msg += '\n';
            if (paramName == kOCIOHelpLooksButton) {
                msg += (_config->getNumLooks() <= 0 ? "No look available in this OCIO configuration.\n" : "Available looks in this OCIO Configuration (applied in the given colorspace):\n");
                for (int i = 0; i < _config->getNumLooks(); ++i) {
                    const char* lkname = _config->getLookNameByIndex(i);
                    OCIO::ConstLookRcPtr lk = _config->getLook(lkname);
                    msg += "- ";
                    msg += lkname;
                    string lkspace = lk->getProcessSpace();
                    msg += " (" + lkspace + ")\n";
                }
                msg += '\n';
            }
            if (paramName == kOCIOHelpDisplaysButton) {
                if (_config->getNumDisplays() <= 0) {
                    msg += "No display available in this OCIO configuration.\n";
                } else {
                    msg += "Available displays and views in this OCIO Configuration:\n";
                    string defaultdisplay = _config->getDefaultDisplay();
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
                            string defaultview = _config->getDefaultView(display);
                            for (int j = 0; j < numViews; ++j) {
                                const char* view = _config->getView(display, j);
                                msg += view;
                                if (view == defaultview) {
                                    msg += " (default)";
                                }
                                if (j < numViews - 1) {
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
            int defaultcs = _config->getIndexForColorSpace(OCIO::ROLE_DEFAULT);
            int referencecs = _config->getIndexForColorSpace(OCIO::ROLE_REFERENCE);
            int datacs = _config->getIndexForColorSpace(OCIO::ROLE_DATA);
            int colorpickingcs = _config->getIndexForColorSpace(OCIO::ROLE_COLOR_PICKING);
            int scenelinearcs = _config->getIndexForColorSpace(OCIO::ROLE_SCENE_LINEAR);
            int compositinglogcs = _config->getIndexForColorSpace(OCIO::ROLE_COMPOSITING_LOG);
            int colortimingcs = _config->getIndexForColorSpace(OCIO::ROLE_COLOR_TIMING);
            int texturepaintcs = _config->getIndexForColorSpace(OCIO::ROLE_TEXTURE_PAINT);
            int mattepaintcs = _config->getIndexForColorSpace(OCIO::ROLE_MATTE_PAINT);

            for (int i = 0; i < _config->getNumColorSpaces(); ++i) {
                const char* csname = _config->getColorSpaceNameByIndex(i);;
                OCIO::ConstColorSpaceRcPtr cs = _config->getColorSpace(csname);
                msg += "- ";
                msg += csname;
                bool first = true;
                //int roles = 0;
                if (i == defaultcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_DEFAULT;
                    first = false;
                    //++roles;
                }
                if (i == referencecs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_REFERENCE;
                    first = false;
                    //++roles;
                }
                if (i == datacs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_DATA;
                    first = false;
                    //++roles;
                }
                if (i == colorpickingcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_COLOR_PICKING;
                    first = false;
                    //++roles;
                }
                if (i == scenelinearcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_SCENE_LINEAR;
                    first = false;
                    //++roles;
                }
                if (i == compositinglogcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_COMPOSITING_LOG;
                    first = false;
                    //++roles;
                }
                if (i == colortimingcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_COLOR_TIMING;
                    first = false;
                    //++roles;
                }
                if (i == texturepaintcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_TEXTURE_PAINT;
                    first = false;
                    //++roles;
                }
                if (i == mattepaintcs) {
                    msg += first ? " (" : ", ";
                    msg += OCIO::ROLE_MATTE_PAINT;
                    first = false;
                    //++roles;
                }
                if (!first /*&& roles > 0*/) {
                    msg += ')';
                }
                string csdesc = cs ? cs->getDescription() : "(no colorspace)";
                csdesc = whitespacify( trim(csdesc) );
                if ( !csdesc.empty() ) {
                    msg += ": ";
                    msg += csdesc;
                    msg += '\n';
                } else {
                    msg += '\n';
                }
            }
        }
        _parent->sendMessage(Message::eMessageMessage, "", msg);
    } else if (!_config) {
        // the other parameters assume there is a valid config
        return;
    } else if (paramName == kOCIOParamInputSpace) {
        assert(_inputSpace);
        if (args.reason == eChangeUserEdit) {
            // if the inputspace doesn't correspond to a valid one, reset to default.
            // first, canonicalize.
            string inputSpace;
            getInputColorspaceAtTime(args.time, inputSpace);
            string inputSpaceCanonical = canonicalizeColorSpace(_config, inputSpace);
            if (inputSpaceCanonical != inputSpace) {
                _inputSpace->setValue(inputSpaceCanonical);
                inputSpace = inputSpaceCanonical;
            }
            int inputSpaceIndex = _config->getIndexForColorSpace( inputSpace.c_str() );
            if (inputSpaceIndex < 0) {
                if (args.reason == eChangeUserEdit) {
                    _parent->sendMessage(Message::eMessageWarning, "", string("Unknown OCIO colorspace \"") + inputSpace + "\"");
                }
                OCIO::ConstColorSpaceRcPtr cs;
                if (!cs) {
                    cs = _config->getColorSpace(OCIO::ROLE_DEFAULT);
                }
                if (!cs) {
                    // no default colorspace, fallback to the first one
                    cs = _config->getColorSpace( _config->getColorSpaceNameByIndex(0) );
                }
                inputSpace = cs ? cs->getName() : OCIO::ROLE_DEFAULT;
                _inputSpace->setValue(inputSpace);
                inputSpaceIndex = _config->getIndexForColorSpace( inputSpace.c_str() );
                assert(inputSpaceIndex >= 0);
            }
        }
        inputCheck(args.time);
    }
#ifdef OFX_OCIO_CHOICE
    else if ( (paramName == kOCIOParamInputSpaceChoice) && (args.reason == eChangeUserEdit) ) {
        assert(_inputSpace);
        int inputSpaceIndex;
        _inputSpaceChoice->getValueAtTime(args.time, inputSpaceIndex);
        string inputSpaceOld;
        getInputColorspaceAtTime(args.time, inputSpaceOld);
        string inputSpace = canonicalizeColorSpace( _config, _config->getColorSpaceNameByIndex(inputSpaceIndex) );
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (inputSpace != inputSpaceOld) {
            _inputSpace->setValue(inputSpace);
        }
    }
#endif
    else if (paramName == kOCIOParamOutputSpace) {
        assert(_outputSpace);
        if (args.reason == eChangeUserEdit) {
            // if the outputspace doesn't correspond to a valid one, reset to default.
            // first, canonicalize.
            string outputSpace;
            getOutputColorspaceAtTime(args.time, outputSpace);
            string outputSpaceCanonical = canonicalizeColorSpace(_config, outputSpace);
            if (outputSpaceCanonical != outputSpace) {
                _outputSpace->setValue(outputSpaceCanonical);
                outputSpace = outputSpaceCanonical;
            }
            int outputSpaceIndex = _config->getIndexForColorSpace( outputSpace.c_str() );
            if (outputSpaceIndex < 0) {
                if (args.reason == eChangeUserEdit) {
                    _parent->sendMessage(Message::eMessageWarning, "", string("Unknown OCIO colorspace \"") + outputSpace + "\"");
                }
                OCIO::ConstColorSpaceRcPtr cs;
                if (!cs) {
                    cs = _config->getColorSpace(OCIO::ROLE_DEFAULT);
                }
                if (!cs) {
                    // no default colorspace, fallback to the first one
                    cs = _config->getColorSpace( _config->getColorSpaceNameByIndex(0) );
                }
                outputSpace = cs ? cs->getName() : OCIO::ROLE_DEFAULT;
                _outputSpace->setValue(outputSpace);
                outputSpaceIndex = _config->getIndexForColorSpace( outputSpace.c_str() );
                assert(outputSpaceIndex >= 0);
            }
        }
        outputCheck(args.time);
    }
#ifdef OFX_OCIO_CHOICE
    else if ( (paramName == kOCIOParamOutputSpaceChoice) && (args.reason == eChangeUserEdit) ) {
        assert(_outputSpace);
        int outputSpaceIndex;
        _outputSpaceChoice->getValueAtTime(args.time, outputSpaceIndex);
        string outputSpaceOld;
        getOutputColorspaceAtTime(args.time, outputSpaceOld);
        string outputSpace = canonicalizeColorSpace( _config, _config->getColorSpaceNameByIndex(outputSpaceIndex) );
        // avoid an infinite loop on bad hosts (for examples those which don't set args.reason correctly)
        if (outputSpace != outputSpaceOld) {
            _outputSpace->setValue(outputSpace);
        }
    }
#endif // OFX_OCIO_CHOICE


#endif // ifdef OFX_IO_USING_OCIO
} // GenericOCIO::changedParam

#ifdef OFX_IO_USING_OCIO
void
GenericOCIO::getInputColorspaceDefault(string &v) const
{
    assert(_inputSpace);
    _inputSpace->getDefault(v);
}

void
GenericOCIO::getInputColorspace(string &v) const
{
    assert(_inputSpace);
    _inputSpace->getValue(v);
}

void
GenericOCIO::getInputColorspaceAtTime(double time,
                                      string &v) const
{
    assert(_inputSpace);
    _inputSpace->getValueAtTime(time, v);
}

void
GenericOCIO::getOutputColorspaceDefault(string &v) const
{
    assert(_outputSpace);
    _outputSpace->getDefault(v);
}

void
GenericOCIO::getOutputColorspace(string &v) const
{
    assert(_outputSpace);
    _outputSpace->getValue(v);
}

void
GenericOCIO::getOutputColorspaceAtTime(double time,
                                       string &v) const
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
GenericOCIO::describeInContextInput(ImageEffectDescriptor &desc,
                                    ContextEnum /*context*/,
                                    PageParamDescriptor *page,
                                    const char* inputSpaceNameDefault,
                                    const char* inputSpaceLabel)
{
#ifdef OFX_IO_USING_OCIO
    gHostIsNatron = (getImageEffectHostDescription()->isNatron);

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
    string inputSpaceName, outputSpaceName;
    if (config) {
        inputSpaceName = canonicalizeColorSpace( config, colorSpaceName(config, inputSpaceNameDefault) );
    }

    ////////// OCIO config file
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamConfigFile);
        param->setLabel(kOCIOParamConfigFileLabel);
        param->setHint(kOCIOParamConfigFileHint);
        param->setStringType(eStringTypeFilePath);
        param->setFilePathExists(true);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        // the OCIO config can only be set in a portable fashion using the environment variable.
        // Nuke, for example, doesn't support changing the entries in a ChoiceParam outside of describeInContext.
        // disable it, and set the default from the env variable.
        assert( getImageEffectHostDescription() );
        if (file == NULL) {
            param->setDefault("WARNING: Open an OCIO config file, or set the OCIO environnement variable");
        } else if (config) {
            param->setDefault(file);
        } else {
            string s("ERROR: Invalid OCIO configuration '");
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
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamInputSpace);
        param->setLabel(inputSpaceLabel);
        param->setHint(kOCIOParamInputSpaceHint);
        param->setAnimates(true);
        if (config) {
            param->setDefault(inputSpaceName);
        } else {
            //param->setEnabled(false); // done in constructor
        }
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_OCIO_CHOICE
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kOCIOParamInputSpaceChoice);
        param->setLabel(inputSpaceLabel);
        param->setHint(kOCIOParamInputSpaceHint);
        param->setCascading(getImageEffectHostDescription()->supportsCascadingChoices);
        if (config) {
            buildChoiceMenu(config, param, getImageEffectHostDescription()->supportsCascadingChoices, inputSpaceName);
        } else {
            //param->setEnabled(false); // done in the plugin constructor
            //param->setIsSecret(true); // done in the plugin constructor
        }
        param->setAnimates(true);
        param->setEvaluateOnChange(false); // evaluate only when the StringParam is changed
        param->setIsPersistent(false); // don't save/serialize
        if (page) {
            page->addChild(*param);
        }
    }
#endif
#endif // ifdef OFX_IO_USING_OCIO
} // GenericOCIO::describeInContextInput

void
GenericOCIO::describeInContextOutput(ImageEffectDescriptor &desc,
                                     ContextEnum /*context*/,
                                     PageParamDescriptor *page,
                                     const char* outputSpaceNameDefault,
                                     const char* outputSpaceLabel)
{
#ifdef OFX_IO_USING_OCIO
    gHostIsNatron = (getImageEffectHostDescription()->isNatron);

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
    string outputSpaceName;
    if (config) {
        outputSpaceName = canonicalizeColorSpace( config, colorSpaceName(config, outputSpaceNameDefault) );
    }

    ///////////Output Color-space
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamOutputSpace);
        param->setLabel(outputSpaceLabel);
        param->setHint(kOCIOParamOutputSpaceHint);
        param->setAnimates(true);
        if (config) {
            param->setDefault(outputSpaceName);
        } else {
            //param->setEnabled(false); // done in constructor
        }
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_OCIO_CHOICE
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kOCIOParamOutputSpaceChoice);
        param->setLabel(outputSpaceLabel);
        param->setHint(kOCIOParamOutputSpaceHint);
        param->setCascading(getImageEffectHostDescription()->supportsCascadingChoices);
        if (config) {
            buildChoiceMenu(config, param, getImageEffectHostDescription()->supportsCascadingChoices, outputSpaceName);
        } else {
            //param->setEnabled(false); // done in the plugin constructor
            //param->setIsSecret(true); // done in the plugin constructor
        }
        param->setAnimates(true);
        param->setEvaluateOnChange(false); // evaluate only when the StringParam is changed
        param->setIsPersistent(false); // don't save/serialize
        if (page) {
            page->addChild(*param);
        }
    }
#endif
#endif // ifdef OFX_IO_USING_OCIO
} // GenericOCIO::describeInContextOutput

void
GenericOCIO::describeInContextContext(ImageEffectDescriptor &desc,
                                      ContextEnum /*context*/,
                                      PageParamDescriptor *page)
{
#ifdef OFX_IO_USING_OCIO
    GroupParamDescriptor* group = desc.defineGroupParam(kOCIOParamContext);
    group->setLabel(kOCIOParamContextLabel);
    group->setHint(kOCIOParamContextHint);
    group->setOpen(false);

    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey1);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue1);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey2);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue2);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey3);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue3);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextKey4);
        param->setHint(kOCIOParamContextHint);
        param->setAnimates(true);
        param->setParent(*group);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kOCIOParamContextValue4);
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
#endif // ifdef OFX_IO_USING_OCIO
} // GenericOCIO::describeInContextContext

NAMESPACE_OFX_IO_EXIT
NAMESPACE_OFX_EXIT
