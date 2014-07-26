/*
 OFX oiioReader plugin.
 Reads an image using the OpenImageIO library.
 
 Copyright (C) 2013 INRIA
 Author Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
 
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


#include "ReadOIIO.h"
#include "GenericOCIO.h"
#include "GenericReader.h"

#include <iostream>
#include <sstream>
#include <cmath>
#include <cstddef>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagecache.h>

#include <ofxNatron.h>

#include "IOUtility.h"

OIIO_NAMESPACE_USING

#define OFX_READ_OIIO_USES_CACHE
#define OFX_READ_OIIO_NEWMENU

#ifdef OFX_READ_OIIO_USES_CACHE
#define OFX_READ_OIIO_SHARED_CACHE
#endif

#define kPluginName "ReadOIIOOFX"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription "Read images using OpenImageIO."
#define kPluginIdentifier "fr.inria.openfx:ReadOIIO"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kMetadataButtonName "showMetadata"
#define kMetadataButtonLabel "Image Info..."
#define kMetadataButtonHint "Shows information and metadata from the image at current time."

#ifndef OFX_READ_OIIO_SHARED_CACHE
// unassociatedAlpha is a cache parameter, so it can't be set separately for each instance
#define kParamUnassociatedAlphaName "unassociatedAlpha"
#define kParamUnassociatedAlphaLabel "Keep Unassoc. Alpha"
#define kParamUnassociatedAlphaHint "When checked, don't associate alpha (i.e. don't premultiply) if alpha is marked as unassociated in the metadata.\nImages which have associated alpha (i.e. are already premultiplied) are unaffected."
#endif

#define kOutputComponentsParamName "outputComponents"
#define kOutputComponentsParamLabel "Output Components"
#define kOutputComponentsParamHint "Components in the output"
#define kOutputComponentsRGBAOption "RGBA"
#define kOutputComponentsRGBOption "RGB"
#define kOutputComponentsAlphaOption "Alpha"

#ifndef OFX_READ_OIIO_NEWMENU
#define kFirstChannelParamName "firstChannel"
#define kFirstChannelParamLabel "First Channel"
#define kFirstChannelParamHint "Channel from the input file corresponding to the first component.\nSee \"Image Info...\" for a list of image channels."
#endif

#ifdef OFX_READ_OIIO_NEWMENU

#define kRChannelParamName "rChannel"
#define kRChannelParamLabel "R Channel"
#define kRChannelParamHint "Channel from the input file corresponding to the red component.\nSee \"Image Info...\" for a list of image channels."

#define kGChannelParamName "gChannel"
#define kGChannelParamLabel "G Channel"
#define kGChannelParamHint "Channel from the input file corresponding to the green component.\nSee \"Image Info...\" for a list of image channels."

#define kBChannelParamName "bChannel"
#define kBChannelParamLabel "B Channel"
#define kBChannelParamHint "Channel from the input file corresponding to the blue component.\nSee \"Image Info...\" for a list of image channels."

#define kAChannelParamName "aChannel"
#define kAChannelParamLabel "A Channel"
#define kAChannelParamHint "Channel from the input file corresponding to the alpha component.\nSee \"Image Info...\" for a list of image channels."

// number of channels for hosts that don't support modifying choice menus (e.g. Nuke)
#define kDefaultChannelCount 16

// Channels 0 and 1 are reserved for 0 and 1 constants
#define kXChannelFirst 2

#endif // OFX_READ_OIIO_NEWMENU

#ifdef OFX_READ_OIIO_USES_CACHE
static const bool kSupportsTiles = true;
#else
static const bool kSupportsTiles = false;
#endif

static bool gSupportsRGBA   = false;
static bool gSupportsRGB    = false;
static bool gSupportsAlpha  = false;
static bool gHostIsNatron   = false;

static OFX::PixelComponentEnum gOutputComponentsMap[4];

class ReadOIIOPlugin : public GenericReaderPlugin {

public:

    ReadOIIOPlugin(OfxImageEffectHandle handle);

    virtual ~ReadOIIOPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

    virtual void clearAnyCache();
private:

    virtual void onInputFileChanged(const std::string& filename);

    virtual bool isVideoStream(const std::string& /*filename*/) { return false; }

    virtual void decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes);

    virtual bool getFrameRegionOfDefinition(const std::string& /*filename*/,OfxTime time,OfxRectD& rod,std::string& error);

    std::string metadata(const std::string& filename);

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) /* OVERRIDE FINAL */;

    void updateSpec(const std::string &filename);

#ifdef OFX_READ_OIIO_NEWMENU
    void updateComponents(OFX::PixelComponentEnum outputComponents);

    void buildChannelMenus();

    void setDefaultChannels();

    void setDefaultChannelsFromRed(int rChannelIdx);
#endif

#ifdef OFX_READ_OIIO_USES_CACHE
    //// OIIO image cache
    ImageCache* _cache;
#endif
#ifndef OFX_READ_OIIO_SHARED_CACHE
    OFX::BooleanParam* _unassociatedAlpha;
#endif
    OFX::ChoiceParam *_outputComponents;
#ifdef OFX_READ_OIIO_NEWMENU
    OFX::ChoiceParam *_rChannel;
    OFX::ChoiceParam *_gChannel;
    OFX::ChoiceParam *_bChannel;
    OFX::ChoiceParam *_aChannel;
#else
    OFX::IntParam *_firstChannel;
#endif
    ImageSpec _spec;
    bool _specValid; //!< does _spec contain anything valid?
};

ReadOIIOPlugin::ReadOIIOPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, kSupportsTiles)
#ifdef OFX_READ_OIIO_USES_CACHE
#  ifdef OFX_READ_OIIO_SHARED_CACHE
, _cache(ImageCache::create(true)) // shared cache
#  else
, _cache(ImageCache::create(false)) // non-shared cache
#  endif
#endif
#ifndef OFX_READ_OIIO_SHARED_CACHE
, _unassociatedAlpha(0)
#endif
, _outputComponents(0)
#ifdef OFX_READ_OIIO_NEWMENU
, _rChannel(0)
, _gChannel(0)
, _bChannel(0)
, _aChannel(0)
#else
, _firstChannel(0)
#endif
, _spec()
, _specValid(false)
{
#ifndef OFX_READ_OIIO_SHARED_CACHE
    _unassociatedAlpha = fetchBooleanParam(kParamUnassociatedAlphaName);
# ifdef OFX_READ_OIIO_USES_CACHE
    bool unassociatedAlpha;
    _unassociatedAlpha->getValue(unassociatedAlpha);
    _cache->attribute("unassociatedalpha", (int)unassociatedAlpha);
# endif
#endif
    _outputComponents = fetchChoiceParam(kOutputComponentsParamName);
#ifdef OFX_READ_OIIO_NEWMENU
    _rChannel = fetchChoiceParam(kRChannelParamName);
    _gChannel = fetchChoiceParam(kGChannelParamName);
    _bChannel = fetchChoiceParam(kBChannelParamName);
    _aChannel = fetchChoiceParam(kAChannelParamName);
    assert(_outputComponents && _rChannel && _gChannel && _bChannel && _aChannel);
#else
    _firstChannel = fetchIntParam(kFirstChannelParamName);
    assert(_outputComponents && _firstChannel);
#endif

#ifdef OFX_READ_OIIO_NEWMENU
    std::string filename;
    int first,last;
    _originalFrameRange->getValue(first, last);
    
    ///Make sure to get a valid file name
    _fileParam->getValueAtTime(first,filename);
    if (!filename.empty()) {
        updateSpec(filename);
        buildChannelMenus();
        setDefaultChannels();
        // the channel values may be out of the menu range, but we don't care (we can't setValue() here)
        ///Edit: Sure you can set Value : http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#SettingParams
        ///The Create instance action is in the list of actions where you can set param values
    }
#endif
}

ReadOIIOPlugin::~ReadOIIOPlugin()
{
#ifdef OFX_READ_OIIO_USES_CACHE
#  ifdef OFX_READ_OIIO_SHARED_CACHE
    ImageCache::destroy(_cache); // don't teardown if it's a shared cache
#  else
    ImageCache::destroy(_cache, true); // teardown non-shared cache
#  endif
#endif
}

void ReadOIIOPlugin::clearAnyCache()
{
#ifdef OFX_READ_OIIO_USES_CACHE
    ///flush the OIIO cache
    _cache->invalidate_all();
#endif
}

#ifdef OFX_READ_OIIO_NEWMENU
void ReadOIIOPlugin::updateComponents(OFX::PixelComponentEnum outputComponents)
{
    switch (outputComponents) {
        case OFX::ePixelComponentRGBA: {
            _rChannel->setIsSecret(false);
            _bChannel->setIsSecret(false);
            _gChannel->setIsSecret(false);
            _aChannel->setIsSecret(false);
        }   break;
        case OFX::ePixelComponentRGB: {
            _rChannel->setIsSecret(false);
            _bChannel->setIsSecret(false);
            _gChannel->setIsSecret(false);
            _aChannel->setIsSecret(true);
        }   break;
        case OFX::ePixelComponentAlpha: {
            _rChannel->setIsSecret(true);
            _bChannel->setIsSecret(true);
            _gChannel->setIsSecret(true);
            _aChannel->setIsSecret(false);
        }   break;
        default:
            // unsupported components
            assert(false);
            break;
    }
}
#endif

void ReadOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kMetadataButtonName) {
        std::string filename;
        getCurrentFileName(filename);
        sendMessage(OFX::Message::eMessageMessage, "", metadata(filename));
    }
#if defined(OFX_READ_OIIO_USES_CACHE) && !defined(OFX_READ_OIIO_SHARED_CACHE)
    ///This cannot be done elsewhere as the Cache::attribute function is not thread safe!
    else if (paramName == kParamUnassociatedAlphaName) {
        bool unassociatedAlpha;
        _unassociatedAlpha->getValue(unassociatedAlpha); // non-animatable
        _cache->attribute("unassociatedalpha", (int)unassociatedAlpha);
    }
#endif
    else if (paramName == kOutputComponentsParamName) {
        int outputComponents_i;
        _outputComponents->getValue(outputComponents_i);
        OFX::PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
#ifdef OFX_READ_OIIO_NEWMENU
        updateComponents(outputComponents);
#else
        // set the first channel to the alpha channel if output is alpha
        if (outputComponents == OFX::ePixelComponentAlpha) {
            std::string filename;
            _fileParam->getValueAtTime(args.time, filename);
            onInputFileChanged(filename);
        }
#endif
    } else if (paramName == kRChannelParamName) {
#ifdef OFX_READ_OIIO_NEWMENU
        int rChannelIdx;
        _rChannel->getValue(rChannelIdx);
        if (rChannelIdx >= kXChannelFirst) {
            setDefaultChannelsFromRed(rChannelIdx - kXChannelFirst);
        }
#endif
    } else {
        GenericReaderPlugin::changedParam(args,paramName);
    }
}

#ifdef OFX_READ_OIIO_NEWMENU
void
ReadOIIOPlugin::buildChannelMenus()
{
    if (gHostIsNatron) {
        assert(_specValid);
        // the choice menu can only be modified in Natron
        // Natron supports changing the entries in a choiceparam
        // Nuke (at least up to 8.0v3) does not
        _rChannel->resetOptions();
        _gChannel->resetOptions();
        _bChannel->resetOptions();
        _aChannel->resetOptions();
        _rChannel->appendOption("0");
        _rChannel->appendOption("1");
        _gChannel->appendOption("0");
        _gChannel->appendOption("1");
        _bChannel->appendOption("0");
        _bChannel->appendOption("1");
        _aChannel->appendOption("0");
        _aChannel->appendOption("1");
        assert(_rChannel->getNOptions() == kXChannelFirst);
        assert(_gChannel->getNOptions() == kXChannelFirst);
        assert(_bChannel->getNOptions() == kXChannelFirst);
        assert(_aChannel->getNOptions() == kXChannelFirst);
        for (std::size_t i = 0; i < _spec.nchannels; ++i) {
            if (i < _spec.channelnames.size()) {
                _rChannel->appendOption(_spec.channelnames[i]);
                _bChannel->appendOption(_spec.channelnames[i]);
                _gChannel->appendOption(_spec.channelnames[i]);
                _aChannel->appendOption(_spec.channelnames[i]);
            } else {
                std::ostringstream oss;
                oss << "channel " << i;
                _rChannel->appendOption(oss.str());
                _gChannel->appendOption(oss.str());
                _bChannel->appendOption(oss.str());
                _aChannel->appendOption(oss.str());
            }
        }
    }
}

// called when the red channel is set
// from the red channel name, infer the corresponding G,B,A channel values
void
ReadOIIOPlugin::setDefaultChannelsFromRed(int rChannelIdx)
{
    assert(rChannelIdx >= 0);
    if (!_specValid) {
        return;
    }
    if (rChannelIdx >= (int)_spec.channelnames.size()) {
        // no name, can't do anything
        return;
    }
    const std::string rFullName = _spec.channelnames[rChannelIdx];
    std::string rChannelName;
    std::string layerDotViewDot;
    // the EXR channel naming convention is layer.view.channel
    // ref: http://www.openexr.com/MultiViewOpenEXR.pdf

    // separate layer.view. from channel
    size_t lastdot = rFullName.find_last_of(".");
    if (lastdot == std::string::npos) {
        // no dot, channel name is the full name
        rChannelName = rFullName;
    } else {
        layerDotViewDot = rFullName.substr(0, lastdot + 1);
        rChannelName = rFullName.substr(lastdot + 1);
    }
    // now check if the channel name looks like red (normally, it should be "R")
    if (rChannelName != "R" &&
        rChannelName != "r" &&
        rChannelName != "red") {
        // not red, can't do anything
        return;
    }
    std::string gFullName;
    std::string bFullName;
    std::string aFullName;

    if (rChannelName == "R") {
        gFullName = layerDotViewDot + "G";
        bFullName = layerDotViewDot + "B";
        aFullName = layerDotViewDot + "A";
    } else if (rChannelName == "r") {
        gFullName = layerDotViewDot + "g";
        bFullName = layerDotViewDot + "b";
        aFullName = layerDotViewDot + "a";
    } else if (rChannelName == "red") {
        gFullName = layerDotViewDot + "green";
        bFullName = layerDotViewDot + "blue";
        aFullName = layerDotViewDot + "alpha";
    }
    bool gSet = false;
    bool bSet = false;
    bool aSet = false;
    for (size_t i = 0; i < _spec.channelnames.size(); ++i) {
        if (_spec.channelnames[i] == gFullName) {
            _gChannel->setValue(kXChannelFirst + i);
            gSet = true;
        }
        if (_spec.channelnames[i] == bFullName) {
            _bChannel->setValue(kXChannelFirst + i);
            bSet = true;
        }
        if (_spec.channelnames[i] == aFullName) {
            _aChannel->setValue(kXChannelFirst + i);
            aSet = true;
        }
    }
    if (!gSet) {
        _gChannel->setValue(0);
    }
    if (!bSet) {
        _bChannel->setValue(0);
    }
    if (!aSet) {
        if (_spec.alpha_channel >= 0) {
            _aChannel->setValue(kXChannelFirst + _spec.alpha_channel);
        } else {
            _aChannel->setValue(1); // opaque by default
        }
    }
}

static bool has_suffix(const std::string &str, const std::string &suffix)
{
    return (str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
}

// called after changing the filename, set all channels
void
ReadOIIOPlugin::setDefaultChannels()
{
    if (!_specValid) {
        return;
    }
    int rChannelIdx = -1;

    // first, look for the main red channel
    for (std::size_t i = 0; i < _spec.channelnames.size(); ++i) {
        if (_spec.channelnames[i] == "R" ||
            _spec.channelnames[i] == "r" ||
            _spec.channelnames[i] == "red") {
            rChannelIdx = i;
            break; // found!
        }
    }

    if (rChannelIdx < 0) {
        // find a name which ends with ".R", ".r" or ".red"
        for (std::size_t i = 0; i < _spec.channelnames.size(); ++i) {
            if (has_suffix(_spec.channelnames[i], ".R") ||
                has_suffix(_spec.channelnames[i], ".r") ||
                has_suffix(_spec.channelnames[i], ".red")) {
                rChannelIdx = i;
                break; // found!
            }
        }
    }

    if (rChannelIdx >= 0) {
        // red was found
        _rChannel->setValue(kXChannelFirst + rChannelIdx);
        setDefaultChannelsFromRed(rChannelIdx);
    } else {
        _rChannel->setValue(0);
        _gChannel->setValue(0);
        _bChannel->setValue(0);
        _aChannel->setValue(1);
    }
}
#endif // OFX_READ_OIIO_NEWMENU

void
ReadOIIOPlugin::updateSpec(const std::string &filename)
{
# ifdef OFX_READ_OIIO_USES_CACHE
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if (!_cache->get_imagespec(ustring(filename), _spec)) {
        return;
    }
# else
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        return;
    }
    _spec = img->spec();
# endif
    _specValid = true;
}

void ReadOIIOPlugin::onInputFileChanged(const std::string &filename)
{
    updateSpec(filename);
    if (!_specValid) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# ifdef OFX_IO_USING_OCIO
    ///find-out the image color-space
    const ParamValue* colorSpaceValue = _spec.find_attribute("oiio:ColorSpace",TypeDesc::STRING);

    //we found a color-space hint, use it to do the color-space conversion
    const char* colorSpaceStr = NULL;
    if (colorSpaceValue) {
        colorSpaceStr = *(const char**)colorSpaceValue->data();
    } else {
        // no colorspace... we'll probably have to try something else, then.
        // we set the following defaults:
        // sRGB for 8-bit images
        // Rec709 for 10-bits, 12-bits or 16-bits integer images
        // Linear for anything else
        switch (_spec.format.basetype) {
            case TypeDesc::UCHAR:
            case TypeDesc::CHAR:
                colorSpaceStr = "sRGB";
                break;
            case TypeDesc::USHORT:
            case TypeDesc::SHORT:
                colorSpaceStr = "Rec709";
                break;
            default:
                colorSpaceStr = "Linear";
                break;
        }
    }
    if (colorSpaceStr) {
        if (!strcmp(colorSpaceStr, "GammaCorrected")) {
            float gamma = _spec.get_float_attribute("oiio:Gamma");
            if (std::fabs(gamma-1.8) < 0.01) {
                if (_ocio->hasColorspace("Gamma1.8")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma1.8");
                }
            } else if (std::fabs(gamma-2.2) < 0.01) {
                if (_ocio->hasColorspace("Gamma2.2")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma2.2");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim and spi-vfx
                    _ocio->setInputColorspace("vd16");
                } else if (_ocio->hasColorspace("sRGB")) {
                    // nuke-default
                    _ocio->setInputColorspace("sRGB");
                } else if (_ocio->hasColorspace("rrt_srgb")) {
                    // rrt_srgb in aces
                    _ocio->setInputColorspace("rrt_srgb");
                } else if (_ocio->hasColorspace("srgb8")) {
                    // srgb8 in spi-vfx
                    _ocio->setInputColorspace("srgb8");
                }
            }
        } else if(!strcmp(colorSpaceStr, "sRGB")) {
            if (_ocio->hasColorspace("sRGB")) {
                // nuke-default
                _ocio->setInputColorspace("sRGB");
            } else if (_ocio->hasColorspace("rrt_srgb")) {
                // rrt_srgb in aces
                _ocio->setInputColorspace("rrt_srgb");
            } else if (_ocio->hasColorspace("srgb8")) {
                // srgb8 in spi-vfx
                _ocio->setInputColorspace("srgb8");
            } else if (_ocio->hasColorspace("Gamma2.2")) {
                // nuke-default
                _ocio->setInputColorspace("Gamma2.2");
            } else if (_ocio->hasColorspace("vd16")) {
                // vd16 in spi-anim and spi-vfx
                _ocio->setInputColorspace("vd16");
            }
        } else if(!strcmp(colorSpaceStr, "AdobeRGB")) {
            // ???
        } else if(!strcmp(colorSpaceStr, "Rec709")) {
            if (_ocio->hasColorspace("Rec709")) {
                // nuke-default
                _ocio->setInputColorspace("Rec709");
            } else if (_ocio->hasColorspace("rrt_rec709")) {
                // rrt_rec709 in aces
                _ocio->setInputColorspace("rrt_rec709");
            } else if (_ocio->hasColorspace("hd10")) {
                // hd10 in spi-anim and spi-vfx
                _ocio->setInputColorspace("hd10");
            }
        } else if(!strcmp(colorSpaceStr, "KodakLog")) {
            if (_ocio->hasColorspace("Cineon")) {
                // Cineon in nuke-default
                _ocio->setInputColorspace("Cineon");
            } else if (_ocio->hasColorspace("lg10")) {
                // lg10 in spi-vfx
                _ocio->setInputColorspace("lg10");
            }
        } else if(!strcmp(colorSpaceStr, "Linear")) {
            _ocio->setInputColorspace("scene_linear");
            // lnf in spi-vfx
        } else if (_ocio->hasColorspace(colorSpaceStr)) {
            // maybe we're lucky
            _ocio->setInputColorspace(colorSpaceStr);
        } else {
            // unknown color-space or Linear, don't do anything
        }
    }
#  ifdef OFX_READ_OIIO_USES_CACHE
#  else
    img->close();
#  endif
# endif // OFX_IO_USING_OCIO

#ifdef OFX_READ_OIIO_NEWMENU
    // rebuild the channel choices
    buildChannelMenus();
    // set the default values for R, G, B, A channels
    setDefaultChannels();
#else
    _firstChannel->setDisplayRange(0, _spec.nchannels);

    // set the first channel to the alpha channel if output is alpha
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    OFX::PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    if (_spec.alpha_channel != -1 && outputComponents == OFX::ePixelComponentAlpha) {
        _firstChannel->setValue(_spec.alpha_channel);
    }
#endif
}

void ReadOIIOPlugin::decode(const std::string& filename, OfxTime time, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
#ifdef OFX_READ_OIIO_USES_CACHE
    ImageSpec spec;
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!_cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#else
#ifndef OFX_READ_OIIO_SHARED_CACHE
    bool unassociatedAlpha;
    _unassociatedAlpha->getValueAtTime(time, unassociatedAlpha);
    ImageSpec config;
    if (unassociatedAlpha) {
        config.attribute("oiio:UnassociatedAlpha",1);
    }
#endif

    std::auto_ptr<ImageInput> img(ImageInput::open(filename, &config));
    if (!img.get()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const ImageSpec &spec = img->spec();
#endif
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    OFX::PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    if (pixelComponents != outputComponents) {
        setPersistentMessage(OFX::Message::eMessageError, "", "ReadOIIO: OFX Host dit not take into account output components");
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    // we only support RGBA, RGB or Alpha output clip
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OIIO: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == spec.width && renderWindow.y1 == 0 && renderWindow.y2 == spec.height));
    assert((renderWindow.x2 - renderWindow.x1) <= spec.width && (renderWindow.y2 - renderWindow.y1) <= spec.height);
    assert(bounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= bounds.x2);
    assert(bounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= bounds.y2);

#ifdef OFX_READ_OIIO_NEWMENU
    int rChannel, gChannel, bChannel, aChannel;
    _rChannel->getValueAtTime(time, rChannel);
    _gChannel->getValueAtTime(time, gChannel);
    _bChannel->getValueAtTime(time, bChannel);
    _aChannel->getValueAtTime(time, aChannel);
    // test if channels are valid
    if (rChannel > spec.nchannels + kXChannelFirst) {
        rChannel = 0;
    }
    if (gChannel > spec.nchannels + kXChannelFirst) {
        gChannel = 0;
    }
    if (bChannel > spec.nchannels + kXChannelFirst) {
        bChannel = 0;
    }
    if (aChannel > spec.nchannels + kXChannelFirst) {
        aChannel = 1; // opaque by default
    }
    int numChannels = 0;
    int pixelBytes = getPixelBytes(pixelComponents, OFX::eBitDepthFloat);
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;

    std::vector<int> channels;
    switch (pixelComponents) {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            channels.resize(numChannels);
            channels[0] = rChannel;
            channels[1] = gChannel;
            channels[2] = bChannel;
            channels[3] = aChannel;
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            channels.resize(numChannels);
            channels[0] = rChannel;
            channels[1] = gChannel;
            channels[2] = bChannel;
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            channels.resize(numChannels);
            channels[0] = aChannel;
            break;
        default:
            assert(false);
            break;
    }
    std::size_t incr; // number of channels processed
    for (std::size_t i = 0; i < channels.size(); i+=incr) {
        incr = 1;
        if (channels[i] < kXChannelFirst) {
            // fill channel with constant value
            char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
            for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
                float *cur = (float*)lineStart;
                for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                    cur[i] = float(channels[i]);
                }
            }
        } else {
            // read as many contiguous channels as we can
            while ((i+incr) < channels.size() &&
                   channels[i+incr] == channels[i+incr-1]+1) {
                ++incr;
            }
            const int outputChannelBegin = i;
            const int chbegin = channels[i] - kXChannelFirst; // start channel for reading
            const int chend = chbegin + incr; // last channel + 1
            size_t pixelDataOffset2 = (size_t)(renderWindow.y2 - 1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes; // offset for line y2-1
#ifdef OFX_READ_OIIO_USES_CACHE
            if (!_cache->get_pixels(ustring(filename),
                                    0, //subimage
                                    0, //miplevel
                                    renderWindow.x1, //x begin
                                    renderWindow.x2, //x end
                                    spec.height - renderWindow.y2, //y begin
                                    spec.height - renderWindow.y1, //y end
                                    0, //z begin
                                    1, //z end
                                    chbegin, //chan begin
                                    chend, // chan end
                                    TypeDesc::FLOAT, // data type
                                    //(float*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y2 - 1) + outputChannelBegin,// output buffer
                                    (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,// output buffer
                                    numChannels * sizeof(float), //x stride
                                    -rowBytes, //y stride < make it invert Y
                                    AutoStride //z stride
                                    )) {
                setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
                return;
            }
#else
            assert(!kSupportsTiles && renderWindow.x1 == 0 && renderWindow.x2 == spec.width && renderWindow.y1 == 0 && renderWindow.y2 == spec.height);
            if (spec.tile_width == 0) {
                ///read by scanlines
                img->read_scanlines(spec.height - renderWindow.y2, //y begin
                                    spec.height - renderWindow.y1, //y end
                                    0, // z
                                    chbegin, // chan begin
                                    chend, // chan end
                                    TypeDesc::FLOAT, // data type
                                    (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                    numChannels * sizeof(float), //x stride
                                    -rowBytes); //y stride < make it invert Y;
            } else {
                img->read_tiles(renderWindow.x1, //x begin
                                renderWindow.x2,//x end
                                spec.height - renderWindow.y2,//y begin
                                spec.height - renderWindow.y1,//y end
                                0, // z begin
                                1, // z end
                                chbegin, // chan begin
                                chend, // chan end
                                TypeDesc::FLOAT,  // data type
                                (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                numChannels * sizeof(float), //x stride
                                -rowBytes, //y stride < make it invert Y
                                AutoStride); //z stride
            }
#endif
        }
#ifdef OFX_READ_OIIO_USES_CACHE
#else
        img->close();
#endif
    }
    // read

#else // !OFX_READ_OIIO_NEWMENU
    int firstChannel;
    _firstChannel->getValueAtTime(time, firstChannel);

    int chcount = spec.nchannels - firstChannel; // number of available channels
    if (chcount <= 0) {
        std::ostringstream oss;
        oss << "ReadOIIO: Cannot read, first channel is " << firstChannel << ", but image has only " << spec.nchannels << " channels";
        setPersistentMessage(OFX::Message::eMessageError, "", oss.str());
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    int numChannels = 0;
    int outputChannelBegin = 0;
    int chbegin; // start channel for reading
    int chend; // last channel + 1
    bool fillRGB = false;
    bool fillAlpha = false;
    bool moveAlpha = false;
    bool copyRtoGB = false;
    switch (pixelComponents) {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            if (chcount == 1) {
                // only one channel to read from input
                chbegin = firstChannel;
                chend = firstChannel + 1;
                if (spec.alpha_channel == -1 || spec.alpha_channel != firstChannel) {
                    // Most probably a luminance image.
                    // fill alpha with 0, duplicate the single channel to r,g,b
                    fillAlpha = true;
                    copyRtoGB = true;
                } else {
                    // An alpha image.
                    fillRGB = true;
                    fillAlpha = false;
                    outputChannelBegin = 3;
                }
            } else {
                chbegin = firstChannel;
                chend = std::min(spec.nchannels, firstChannel + numChannels);
                // After reading, if spec.alpha_channel != 3 and -1,
                // move the channel spec.alpha_channel to channel 3 and fill it
                // with zeroes
                moveAlpha = (firstChannel <= spec.alpha_channel && spec.alpha_channel < firstChannel+3);
                fillAlpha = (chcount < 4); //(spec.alpha_channel == -1);

                fillRGB = (chcount < 3); // need to fill B with black
            }
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            fillRGB = (spec.nchannels == 1) || (spec.nchannels == 2);
            if (chcount == 1) {
                chbegin = chend = -1;
            } else {
                chbegin = firstChannel;
                chend = std::min(spec.nchannels, firstChannel + numChannels);
            }
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            chbegin = firstChannel;
            chend = chbegin + numChannels;
            break;
        default:
#ifndef OFX_READ_OIIO_USES_CACHE
            img->close();
#endif
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    assert(numChannels);
    int pixelBytes = getPixelBytes(pixelComponents, OFX::eBitDepthFloat);
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;

    if (fillRGB) {
        // fill RGB values with black
        assert(pixelComponents != OFX::ePixelComponentAlpha);
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[0] = 0.;
                cur[1] = 0.;
                cur[2] = 0.;
            }
        }
    }
    if (fillAlpha) {
        // fill Alpha values with opaque
        assert(pixelComponents != OFX::ePixelComponentRGB);
        int outputChannelAlpha = (pixelComponents == OFX::ePixelComponentAlpha) ? 0 : 3;
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart + outputChannelAlpha;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[0] = 1.;
            }
        }
    }

    if (chbegin != -1 && chend != -1) {
        assert(0 <= chbegin && chbegin < spec.nchannels && chbegin < chend && 0 < chend && chend <= spec.nchannels);
        size_t pixelDataOffset2 = (size_t)(renderWindow.y2 - 1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;
#ifdef OFX_READ_OIIO_USES_CACHE
        // offset for line y2-1
        if (!_cache->get_pixels(ustring(filename),
                               0, //subimage
                               0, //miplevel
                               renderWindow.x1, //x begin
                               renderWindow.x2, //x end
                               spec.height - renderWindow.y2, //y begin
                               spec.height - renderWindow.y1, //y end
                               0, //z begin
                               1, //z end
                               chbegin, //chan begin
                               chend, // chan end
                               TypeDesc::FLOAT, // data type
                               //(float*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y2 - 1) + outputChannelBegin,// output buffer
                               (float*)((char*)pixelData + pixelDataOffset2)
                               + outputChannelBegin,// output buffer
                               numChannels * sizeof(float), //x stride
                               -rowBytes, //y stride < make it invert Y
                               AutoStride //z stride
                               )) {
            setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
            return;
        }
#else
        assert(!kSupportsTiles && renderWindow.x1 == 0 && renderWindow.x2 == spec.width && renderWindow.y1 == 0 && renderWindow.y2 == spec.height);
        if (spec.tile_width == 0) {
           ///read by scanlines
            img->read_scanlines(spec.height - renderWindow.y2, //y begin
                                spec.height - renderWindow.y1, //y end
                                0, // z
                                chbegin, // chan begin
                                chend, // chan end
                                TypeDesc::FLOAT, // data type
                                (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                numChannels * sizeof(float), //x stride
                                -rowBytes); //y stride < make it invert Y;
        } else {
            img->read_tiles(renderWindow.x1, //x begin
                            renderWindow.x2,//x end
                            spec.height - renderWindow.y2,//y begin
                            spec.height - renderWindow.y1,//y end
                            0, // z begin
                            1, // z end
                            chbegin, // chan begin
                            chend, // chan end
                            TypeDesc::FLOAT,  // data type
                            (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                            numChannels * sizeof(float), //x stride
                            -rowBytes, //y stride < make it invert Y
                            AutoStride); //z stride
        }
        img->close();
#endif
    }
    if (moveAlpha) {
        // move alpha channel to the right place
        assert(pixelComponents == OFX::ePixelComponentRGB && spec.alpha_channel < 3 && spec.alpha_channel != -1);
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[3] = cur[spec.alpha_channel];
                cur[spec.alpha_channel] = 0.;
            }
        }
    }
    if (copyRtoGB) {
        // copy red to green and blue RGB values with black
        assert(pixelComponents != OFX::ePixelComponentAlpha);
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[1] = cur[2] = cur[0];
            }
        }
    }
#endif // !OFX_READ_OIIO_NEWMENU

#ifndef OFX_READ_OIIO_USES_CACHE
    img->close();
#endif
}

bool ReadOIIOPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod,std::string& error)
{
#ifdef OFX_READ_OIIO_USES_CACHE
    ImageSpec spec;
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!_cache->get_imagespec(ustring(filename), spec)) {
        error = _cache->geterror();
        return false;
    }
#else 
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        return false;
    }
    const ImageSpec &spec = img->spec();
#endif
    rod.x1 = spec.x;
    rod.x2 = spec.x + spec.width;
    rod.y1 = spec.y;
    rod.y2 = spec.y + spec.height;
#ifdef OFX_READ_OIIO_USES_CACHE
#else
    img->close();
#endif
    return true;
}

std::string ReadOIIOPlugin::metadata(const std::string& filename)
{
    std::stringstream ss;

#ifdef OFX_READ_OIIO_USES_CACHE
    ImageSpec spec;
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!_cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#else 
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const ImageSpec& spec = img->spec();
#endif
    ss << "file: " << filename << std::endl;
    ss << "    channel list: ";
    for (std::size_t i = 0;  i < spec.nchannels;  ++i) {
        ss << i << ":";
        if (i < spec.channelnames.size()) {
            ss << spec.channelnames[i];
        } else {
            ss << "unknown";
        }
        if (i < spec.channelformats.size()) {
            ss << " (" << spec.channelformats[i].c_str() << ")";
        }
        if (i < spec.nchannels-1) {
            ss << ", ";
        }
    }
    ss << std::endl;

    if (spec.x || spec.y || spec.z) {
        ss << "    pixel data origin: x=" << spec.x << ", y=" << spec.y;
        if (spec.depth > 1) {
                ss << ", z=" << spec.z;
        }
        ss << std::endl;
    }
    if (spec.full_x || spec.full_y || spec.full_z ||
        (spec.full_width != spec.width && spec.full_width != 0) ||
        (spec.full_height != spec.height && spec.full_height != 0) ||
        (spec.full_depth != spec.depth && spec.full_depth != 0)) {
        ss << "    full/display size: " << spec.full_width << " x " << spec.full_height;
        if (spec.depth > 1) {
            ss << " x " << spec.full_depth;
        }
        ss << std::endl;
        ss << "    full/display origin: " << spec.full_x << ", " << spec.full_y;
        if (spec.depth > 1) {
            ss << ", " << spec.full_z;
        }
        ss << std::endl;
    }
    if (spec.tile_width) {
        ss << "    tile size: " << spec.tile_width << " x " << spec.tile_height;
        if (spec.depth > 1) {
            ss << " x " << spec.tile_depth;
        }
        ss << std::endl;
    }

    for (ImageIOParameterList::const_iterator p = spec.extra_attribs.begin(); p != spec.extra_attribs.end(); ++p) {
        std::string s = spec.metadata_val (*p, true);
        ss << "    " << p->name() << ": ";
        if (s == "1.#INF") {
            ss << "inf";
        } else {
            ss << s;
        }
        ss << std::endl;
    }
#ifdef OFX_READ_OIIO_USES_CACHE
#else
    img->close();
#endif

    return ss.str();
}

/* Override the clip preferences */
void
ReadOIIOPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
#ifdef OFX_READ_OIIO_SHARED_CACHE
    // output is always premultiplied
    clipPreferences.setOutputPremultiplication(OFX::eImagePreMultiplied);
#else
    // set the premultiplication of _outputClip
    // OIIO always outputs premultiplied images, except if it's told not to do so
    bool unassociatedAlpha = false;

    // We assume that if "unassociatedAlpha" is checked, output is UnPremultiplied,
    // but its only true if the image had originally unassociated alpha
    // (OIIO metadata "oiio:UnassociatedAlpha").
    // However, it is not possible to check here if the alpha in the
    // images is associated or not. If the user checked the option, it's
    // probably because it was not associated/premultiplied.
    _unassociatedAlpha->getValue(unassociatedAlpha);
    clipPreferences.setOutputPremultiplication(unassociatedAlpha ? OFX::eImageUnPreMultiplied : OFX::eImagePreMultiplied);
#endif
    // set the components of _outputClip
    int outputComponents_i;
    _outputComponents->getValue(outputComponents_i);
    OFX::PixelComponentEnum outputComponents = gOutputComponentsMap[outputComponents_i];
    clipPreferences.setClipComponents(*_outputClip, outputComponents);
}

using namespace OFX;

mDeclareReaderPluginFactory(ReadOIIOPluginFactory, ;, ;,false);

void ReadOIIOPluginFactory::load() {
}

void ReadOIIOPluginFactory::unload()
{
#  ifdef OFX_READ_OIIO_SHARED_CACHE
    // get the shared image cache (may be shared with other plugins using OIIO)
    ImageCache* sharedcache = ImageCache::create(true);
    // purge it
    // teardown is dangerous if there are other users
    ImageCache::destroy(sharedcache);
#  endif
}

static std::string oiio_versions()
{
    std::ostringstream oss;
    int ver = openimageio_version();
    oss << "OIIO versions:" << std::endl;
    oss << "compiled with " << OIIO_VERSION_STRING << std::endl;
    oss << "running with " << ver/10000 << '.' << (ver%10000)/100 << '.' << (ver%100) << std::endl;
    return oss.str();
}

/** @brief The basic describe function, passed a plugin descriptor */
void ReadOIIOPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    ///set OIIO to use as many threads as there are cores on the CPU
    if(!attribute("threads", 0)){
        std::cerr << "Failed to set the number of threads for OIIO" << std::endl;
    }
    
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginDescription("Read images using OpenImageIO.\n\n"
                              "OpenImageIO supports reading/writing the following file formats:\n"
                              "BMP (*.bmp)\n"
                              "Cineon (*.cin)\n"
                              "Direct Draw Surface (*.dds)\n"
                              "DPX (*.dpx)\n"
                              "Field3D (*.f3d)\n"
                              "FITS (*.fits)\n"
                              "HDR/RGBE (*.hdr)\n"
                              "Icon (*.ico)\n"
                              "IFF (*.iff)\n"
                              "JPEG (*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi)\n"
                              "JPEG-2000 (*.jp2 *.j2k)\n"
                              "OpenEXR (*.exr)\n"
                              "Portable Network Graphics (*.png)\n"
#                           if OIIO_VERSION >= 10400
                              "PNM / Netpbm (*.pbm *.pgm *.ppm *.pfm)\n"
#                           else
                              "PNM / Netpbm (*.pbm *.pgm *.ppm)\n"
#                           endif
                              "PSD (*.psd *.pdd *.psb)\n"
                              "Ptex (*.ptex)\n"
                              "RLA (*.rla)\n"
                              "SGI (*.sgi *.rgb *.rgba *.bw *.int *.inta)\n"
                              "Softimage PIC (*.pic)\n"
                              "Targa (*.tga *.tpic)\n"
                              "TIFF (*.tif *.tiff *.tx *.env *.sm *.vsm)\n"
                              "Zfile (*.zfile)\n\n"
#if defined(OFX_READ_OIIO_USES_CACHE) && !defined(OFX_READ_OIIO_SHARED_CACHE)
                              "Note that the output is always premultiplied. "
                              "If the file was wrongly tagged as unpremultiplied, "
                              "this will result in darker colors than expected. "
                              "To fix this, use the Unpremult plugin.\n\n"
#endif
                              + oiio_versions());


#ifdef OFX_EXTENSIONS_TUTTLE

    const char* extensions[] = { "bmp", "cin", "dds", "dpx", "f3d", "fits", "hdr", "ico",
        "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png",
        "pbm", "pgm", "ppm",
#     if OIIO_VERSION >= 10400
        "pfm",
#     endif
        "psd", "pdd", "psb", "ptex", "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(50);
#endif

    for (ImageEffectHostDescription::PixelComponentArray::const_iterator it = getImageEffectHostDescription()->_supportedComponents.begin();
         it != getImageEffectHostDescription()->_supportedComponents.end();
         ++it) {
        switch (*it) {
            case ePixelComponentRGBA:
                gSupportsRGBA  = true;
                break;
            case ePixelComponentRGB:
                gSupportsRGB = true;
                break;
            case ePixelComponentAlpha:
                gSupportsAlpha = true;
                break;
            default:
                // other components are not supported by this plugin
                break;
        }
    }
    {
        int i = 0;
        if (gSupportsRGBA) {
            gOutputComponentsMap[i] = ePixelComponentRGBA;
            ++i;
        }
        if (gSupportsRGB) {
            gOutputComponentsMap[i] = ePixelComponentRGB;
            ++i;
        }
        if (gSupportsAlpha) {
            gOutputComponentsMap[i] = ePixelComponentAlpha;
            ++i;
        }
        gOutputComponentsMap[i] = ePixelComponentNone;
    }
}

static void
appendDefaultChannelList(ChoiceParamDescriptor *channel)
{
    channel->appendOption("0");
    channel->appendOption("1");
    for (int i = 0; i < kDefaultChannelCount; ++i) {
        std::ostringstream oss;
        oss << "channel " << i;
        channel->appendOption(oss.str());
    }
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);

    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), /*supportsRGBA =*/ true, /*supportsRGB =*/ true, /*supportsAlpha =*/ true, /*supportsTiles =*/ kSupportsTiles);

#ifndef OFX_READ_OIIO_SHARED_CACHE
    OFX::BooleanParamDescriptor* unassociatedAlpha = desc.defineBooleanParam(kParamUnassociatedAlphaName);
    unassociatedAlpha->setLabels(kParamUnassociatedAlphaLabel, kParamUnassociatedAlphaLabel, kParamUnassociatedAlphaLabel);
    unassociatedAlpha->setHint(kParamUnassociatedAlphaHint);
#ifdef OFX_READ_OIIO_USES_CACHE
    unassociatedAlpha->setAnimates(false); // cannot be animated, because relies on changedParam()
#endif
    page->addChild(*unassociatedAlpha);
    desc.addClipPreferencesSlaveParam(*unassociatedAlpha);
#endif

    OFX::PushButtonParamDescriptor* pb = desc.definePushButtonParam(kMetadataButtonName);
    pb->setLabels(kMetadataButtonLabel, kMetadataButtonLabel, kMetadataButtonLabel);
    pb->setHint(kMetadataButtonHint);
    page->addChild(*pb);

#ifndef OFX_READ_OIIO_NEWMENU
    IntParamDescriptor *firstChannel = desc.defineIntParam(kFirstChannelParamName);
    firstChannel->setLabels(kFirstChannelParamLabel, kFirstChannelParamLabel, kFirstChannelParamLabel);
    firstChannel->setHint(kFirstChannelParamHint);
    page->addChild(*firstChannel);
#endif

    ChoiceParamDescriptor *outputComponents = desc.defineChoiceParam(kOutputComponentsParamName);
    outputComponents->setLabels(kOutputComponentsParamLabel, kOutputComponentsParamLabel, kOutputComponentsParamLabel);
    outputComponents->setHint(kOutputComponentsParamHint);
    // the following must be in the same order as in describe(), so that the map works
    if (gSupportsRGBA) {
        assert(gOutputComponentsMap[outputComponents->getNOptions()] == ePixelComponentRGBA);
        outputComponents->appendOption(kOutputComponentsRGBAOption);
    }
    if (gSupportsRGB) {
        assert(gOutputComponentsMap[outputComponents->getNOptions()] == ePixelComponentRGB);
        outputComponents->appendOption(kOutputComponentsRGBOption);
    }
    if (gSupportsAlpha) {
        assert(gOutputComponentsMap[outputComponents->getNOptions()] == ePixelComponentAlpha);
        outputComponents->appendOption(kOutputComponentsAlphaOption);
    }
    outputComponents->setDefault(0);
    outputComponents->setAnimates(false);
    page->addChild(*outputComponents);
    desc.addClipPreferencesSlaveParam(*outputComponents);

#ifdef OFX_READ_OIIO_NEWMENU
    ChoiceParamDescriptor *rChannel = desc.defineChoiceParam(kRChannelParamName);
    rChannel->setLabels(kRChannelParamLabel, kRChannelParamLabel, kRChannelParamLabel);
    rChannel->setHint(kRChannelParamHint);
    appendDefaultChannelList(rChannel);
    rChannel->setAnimates(true);
    page->addChild(*rChannel);

    ChoiceParamDescriptor *gChannel = desc.defineChoiceParam(kGChannelParamName);
    gChannel->setLabels(kGChannelParamLabel, kGChannelParamLabel, kGChannelParamLabel);
    gChannel->setHint(kGChannelParamHint);
    appendDefaultChannelList(gChannel);
    gChannel->setAnimates(true);
    page->addChild(*gChannel);

    ChoiceParamDescriptor *bChannel = desc.defineChoiceParam(kBChannelParamName);
    bChannel->setLabels(kBChannelParamLabel, kBChannelParamLabel, kBChannelParamLabel);
    bChannel->setHint(kBChannelParamHint);
    appendDefaultChannelList(bChannel);
    bChannel->setAnimates(true);
    page->addChild(*bChannel);

    ChoiceParamDescriptor *aChannel = desc.defineChoiceParam(kAChannelParamName);
    aChannel->setLabels(kAChannelParamLabel, kAChannelParamLabel, kAChannelParamLabel);
    aChannel->setHint(kBChannelParamHint);
    appendDefaultChannelList(aChannel);
    aChannel->setAnimates(true);
    aChannel->setDefault(1); // opaque by default
    page->addChild(*aChannel);
#endif

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new ReadOIIOPlugin(handle);
}

void getReadOIIOPluginID(OFX::PluginFactoryArray &ids)
{
    static ReadOIIOPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
