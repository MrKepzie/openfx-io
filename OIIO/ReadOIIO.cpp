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
 * OFX oiioReader plugin.
 * Reads an image using the OpenImageIO library.
 */

#include <iostream>
#include <set>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstddef>
#include <climits>
#include <algorithm>

#ifdef _WIN32
#include <IlmThreadPool.h>
#endif

#include "ofxsMacros.h"

#include "OIIOGlobal.h"
GCC_DIAG_OFF(unused-parameter)
#include <OpenImageIO/imagecache.h>
GCC_DIAG_ON(unused-parameter)

#include <ofxNatron.h>

#include "GenericOCIO.h"
#include "GenericReader.h"
#include "IOUtility.h"

#include <ofxsCoords.h>
#include <ofxsMultiPlane.h>


#define OFX_READ_OIIO_USES_CACHE
#define OFX_READ_OIIO_SUPPORTS_SUBIMAGES

#ifdef OFX_READ_OIIO_USES_CACHE
#define OFX_READ_OIIO_SHARED_CACHE
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ReadOIIO"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription \
"Read images using OpenImageIO.\n\n" \
"Ouput is always Premultiplied (alpha is associated).\n\n" \
"The \"Image Premult\" parameter controls the file premultiplication state, " \
"and can be used to fix wrong file metadata (see the help for that parameter).\n"
#define kPluginIdentifier "fr.inria.openfx.ReadOIIO"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 91

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#ifdef OFX_READ_OIIO_USES_CACHE
#define kSupportsTiles true
#else
// It is more efficient to read full frames if no cache is used.
#define kSupportsTiles false
#endif
#define kIsMultiPlanar true



#define kParamShowMetadata "showMetadata"
#define kParamShowMetadataLabel "Image Info..."
#define kParamShowMetadataHint "Shows information and metadata from the image at current time."

// number of channels for hosts that don't support modifying choice menus (e.g. Nuke)
#define kDefaultChannelCount 16


#define kParamRChannel "rChannel"
#define kParamRChannelLabel "R Channel"
#define kParamRChannelHint "Channel from the input file corresponding to the red component.\nSee \"Image Info...\" for a list of image channels."

#define kParamGChannel "gChannel"
#define kParamGChannelLabel "G Channel"
#define kParamGChannelHint "Channel from the input file corresponding to the green component.\nSee \"Image Info...\" for a list of image channels."

#define kParamBChannel "bChannel"
#define kParamBChannelLabel "B Channel"
#define kParamBChannelHint "Channel from the input file corresponding to the blue component.\nSee \"Image Info...\" for a list of image channels."

#define kParamAChannel "aChannel"
#define kParamAChannelLabel "A Channel"
#define kParamAChannelHint "Channel from the input file corresponding to the alpha component.\nSee \"Image Info...\" for a list of image channels."

#define kParamRChannelName "rChannelIndex"
#define kParamGChannelName "gChannelIndex"
#define kParamBChannelName "bChannelIndex"
#define kParamAChannelName "aChannelIndex"

#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
#define kParamUseDisplayWindowAsOrigin "originAtDisplayWindow"
#define kParamUseDisplayWindowAsOriginLabel "Use Display Window As Origin"
#define kParamUseDisplayWindowAsOriginHint "When checked, the bottom left corner (0,0) will shifted to the bottom left corner of the display window."
#endif


// Channels 0 and 1 are reserved for 0 and 1 constants
#define kXChannelFirst 2


#define kParamChannelOutputLayer "outputLayer"
#define kParamChannelOutputLayerLabel "Output Layer"
#define kParamChannelOutputLayerHint "This is the layer that will be set to the the color plane. This is relevant only for image formats that can have multiple layers: " \
"exr, tiff, psd, etc... Note that in Natron you can access other layers with a Shuffle node downstream of this node."

//The string param behind the dynamic choice menu
#define kParamChannelOutputLayerChoice kParamChannelOutputLayer "Choice"

#define kParamAvailableViews "availableViews"
#define kParamAvailableViewsLabel "Available Views"
#define kParamAvailableViewsHint "Comma separated list of available views"

#define kReadOIIOColorLayer "Color"
#define kReadOIIOXYZLayer "XYZ"
#define kReadOIIODepthLayer "depth"

static bool gHostSupportsDynamicChoices   = false;
static bool gHostSupportsMultiPlane = false;

struct LayerChannelIndexes
{
    //The index of the subimage in the file
    int subImageIdx;
    
    //The channel indexes in the subimage
    //WARNING: We do NOT allow layers with more than 4 channels
    std::vector<int> channelIndexes;
    
    //A vector with the same size as channelIndexes
    std::vector<std::string> channelNames;

};

struct LayerUnionData
{
    //The data related to the layer
    LayerChannelIndexes layer;
    
    //the option as it appears in the choice menu
    std::string choiceOption;
    
    
    //A list of the views that contain this layer
    std::vector<std::string> views;
    
};

//This is a vector to remain the ordering imposed by the file <layer name, layer info>
typedef std::vector<std::pair<std::string, LayerChannelIndexes> > LayersMap;

//For each view name, the layer availables. Note that they are ordered because the first one is the "Main" view
typedef std::vector< std::pair<std::string,LayersMap> > ViewsLayersMap;

// <layer name, extended layer info>
typedef std::vector<std::pair<std::string, LayerUnionData> > LayersUnionVect;

class ReadOIIOPlugin : public GenericReaderPlugin {

public:

    ReadOIIOPlugin(bool useRGBAChoices,OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~ReadOIIOPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    
    virtual void getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
    
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    virtual void clearAnyCache() OVERRIDE FINAL;
private:

    
    virtual void onInputFileChanged(const std::string& filename, bool setColorSpace, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components, int *componentCount) OVERRIDE FINAL;

    virtual bool isVideoStream(const std::string& /*filename*/) OVERRIDE FINAL { return false; }
    
    virtual void decode(const std::string& filename, OfxTime time, int view, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,
                             OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes) OVERRIDE FINAL
    {
        std::string rawComps;
        switch (pixelComponents) {
            case OFX::ePixelComponentAlpha:
                rawComps = kOfxImageComponentAlpha;
                break;
            case OFX::ePixelComponentRGB:
                rawComps = kOfxImageComponentRGB;
                break;
            case OFX::ePixelComponentRGBA:
                rawComps = kOfxImageComponentRGBA;
                break;
            default:
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
        }
        decodePlane(filename, time, view, isPlayback, renderWindow, pixelData, bounds, pixelComponents, pixelComponentCount, rawComps, rowBytes);
    }
    
    virtual void decodePlane(const std::string& filename, OfxTime time, int view, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,
                             OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, const std::string& rawComponents, int rowBytes) OVERRIDE FINAL;
    
    void getOIIOChannelIndexesFromLayerName(const std::string& filename, int view, const std::string& layerName, OFX::PixelComponentEnum pixelComponents, const std::vector<ImageSpec>& subimages, std::vector<int>& channels, int& numChannels, int& subImageIndex);
    
    void openFile(const std::string& filename, bool useCache, ImageInput** img, std::vector<ImageSpec>* subimages);

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, double *par, std::string *error,  int* tile_width, int* tile_height) OVERRIDE FINAL;

    virtual void onOutputComponentsParamChanged(OFX::PixelComponentEnum components) OVERRIDE FINAL;
    
    virtual void restoreState(const std::string& filename) OVERRIDE FINAL;
    
    std::string metadata(const std::string& filename);

    void getSpecsFromImageInput(ImageInput* img, std::vector<ImageSpec>& subimages) const;
    
    void getSpecsFromCache(const std::string& filename, std::vector<ImageSpec>& subimages) const;
    
    void updateSpec(const std::string &filename);
    
    void setOCIOColorspacesFromSpec(const std::string& filename);

    void updateChannelMenusVisibility(OFX::PixelComponentEnum outputComponents);

    void buildChannelMenus();
    
    ///This may warn the user if some views do not exist in the project
    
    static void layersMapFromSubImages(const std::vector<ImageSpec>& subimages, ViewsLayersMap* layersMap, LayersUnionVect* layersUnion);
    
    void buildLayersMenu();

    void setDefaultChannels(OFX::PixelComponentEnum *components);
    
    void restoreChannelMenusFromStringParams();

    void setDefaultChannelsFromRed(int rChannelIdx, bool mustSetChannelNames, OFX::PixelComponentEnum *components);
    
    bool _useRGBAChoices;

#ifdef OFX_READ_OIIO_USES_CACHE
    //// OIIO image cache
    ImageCache* _cache;
#endif

    ///V1 params
    OFX::ChoiceParam *_rChannel;
    OFX::ChoiceParam *_gChannel;
    OFX::ChoiceParam *_bChannel;
    OFX::ChoiceParam *_aChannel;
    OFX::StringParam *_rChannelName;
    OFX::StringParam *_gChannelName;
    OFX::StringParam *_bChannelName;
    OFX::StringParam *_aChannelName;
    
    ///V2 params
    OFX::ChoiceParam* _outputLayer;
    OFX::StringParam* _outputLayerString;
    OFX::StringParam* _availableViews;

#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    OFX::BooleanParam* _useDisplayWindowAsOrigin;
#endif
    
    //Only accessed on the main-thread
    std::vector<ImageSpec> _subImagesSpec;
    bool _specValid; //!< does _spec contain anything valid?
    
    //We keep the name of the last file read when not in playback so that
    //if it changes we can invalidate the last file read from the OIIO cache since it is no longer useful.
    //The host cache will back it up on most case. The only useful case for the OIIO cache is when there are
    //multiple threads trying to read the same image.
    OFX::MultiThread::Mutex _lastFileReadNoPlaybackMutex;
    std::string _lastFileReadNoPlayback;
    
    
    OFX::MultiThread::Mutex _layersMapMutex;

    
    ///Union all layers across views to build the layers choice.
    ///This is because we cannot provide a choice with different entries across views, so if there are some disparities,
    ///let the render action just return a black image if the layer requested cannot be found for the given view.
    LayersUnionVect _layersUnion;

};

ReadOIIOPlugin::ReadOIIOPlugin(bool useRGBAChoices,
                               OfxImageEffectHandle handle,
                               const std::vector<std::string>& extensions)
: GenericReaderPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles,
#ifdef OFX_EXTENSIONS_NUKE
                      (OFX::getImageEffectHostDescription() && OFX::getImageEffectHostDescription()->isMultiPlanar) ? kIsMultiPlanar : false
#else
                      false
#endif
                      )
, _useRGBAChoices(useRGBAChoices)
#ifdef OFX_READ_OIIO_USES_CACHE
#  ifdef OFX_READ_OIIO_SHARED_CACHE
, _cache(ImageCache::create(true)) // shared cache
#  else
, _cache(ImageCache::create(false)) // non-shared cache
#  endif
#endif
, _rChannel(0)
, _gChannel(0)
, _bChannel(0)
, _aChannel(0)
, _rChannelName(0)
, _gChannelName(0)
, _bChannelName(0)
, _aChannelName(0)
, _outputLayer(0)
, _outputLayerString(0)
, _availableViews(0)
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
, _useDisplayWindowAsOrigin(0)
#endif
, _subImagesSpec()
, _specValid(false)
, _lastFileReadNoPlaybackMutex()
, _lastFileReadNoPlayback()
, _layersMapMutex()
, _layersUnion()
{
#ifdef OFX_READ_OIIO_USES_CACHE
    // Always keep unassociated alpha.
    // Don't let OIIO premultiply, because if the image is 8bits,
    // it multiplies in 8bits (see TIFFInput::unassalpha_to_assocalpha()),
    // which causes a lot of precision loss.
    // see also https://github.com/OpenImageIO/oiio/issues/960
    _cache->attribute("unassociatedalpha", 1);
#endif
    
    if (_useRGBAChoices) {
        _rChannel = fetchChoiceParam(kParamRChannel);
        _gChannel = fetchChoiceParam(kParamGChannel);
        _bChannel = fetchChoiceParam(kParamBChannel);
        _aChannel = fetchChoiceParam(kParamAChannel);
        _rChannelName = fetchStringParam(kParamRChannelName);
        _gChannelName = fetchStringParam(kParamGChannelName);
        _bChannelName = fetchStringParam(kParamBChannelName);
        _aChannelName = fetchStringParam(kParamAChannelName);
        assert(_outputComponents && _rChannel && _gChannel && _bChannel && _aChannel &&
               _rChannelName && _bChannelName && _gChannelName && _aChannelName);
        updateChannelMenusVisibility(getOutputComponents());
    } else {
        if (gHostSupportsDynamicChoices && gHostSupportsMultiPlane) {
            _outputLayer = fetchChoiceParam(kParamChannelOutputLayer);
            _outputLayerString = fetchStringParam(kParamChannelOutputLayerChoice);
            _availableViews = fetchStringParam(kParamAvailableViews);
            assert(_outputLayer && _outputLayerString);
        }
    }
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    _useDisplayWindowAsOrigin = fetchBooleanParam(kParamUseDisplayWindowAsOrigin);
    assert(_useDisplayWindowAsOrigin);
#endif
    
    //Don't try to restore any state in here, do so in restoreState instead which is called
    //right away after the constructor.
    
    initOIIOThreads();

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
    _cache->invalidate_all(true);
#endif
}

void ReadOIIOPlugin::updateChannelMenusVisibility(OFX::PixelComponentEnum outputComponents)
{
    assert(_useRGBAChoices);
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

void ReadOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamShowMetadata) {
        std::string filename;
        OfxStatus st = getFilenameAtTime(args.time, &filename);
        std::stringstream ss;
        if (st == kOfxStatOK) {
            ss << metadata(filename);
        } else {
            ss << "Impossible to read image info:\nCould not get filename at time " << args.time << '.';
        }
        sendMessage(OFX::Message::eMessageMessage, "", ss.str());
    } else if (paramName == kParamRChannel && args.reason == OFX::eChangeUserEdit) {
        int rChannelIdx;
        _rChannel->getValue(rChannelIdx);
        if (rChannelIdx >= kXChannelFirst) {
            setDefaultChannelsFromRed(rChannelIdx - kXChannelFirst, true, NULL);
        }
            if (_rChannelName) {
            std::string optionName;
            _rChannel->getOption(rChannelIdx, optionName);
            _rChannelName->setValue(optionName);
        }
    } else if (_gChannelName && paramName == kParamGChannel && args.reason == OFX::eChangeUserEdit) {
        int gChannelIdx;
        _gChannel->getValue(gChannelIdx);
        std::string optionName;
        _gChannel->getOption(gChannelIdx, optionName);
        _gChannelName->setValue(optionName);
    } else if (_bChannelName && paramName == kParamBChannel && args.reason == OFX::eChangeUserEdit) {
        int bChannelIdx;
        _bChannel->getValue(bChannelIdx);
        std::string optionName;
        _bChannel->getOption(bChannelIdx, optionName);
        _bChannelName->setValue(optionName);
    } else if (_aChannelName && paramName == kParamAChannel && args.reason == OFX::eChangeUserEdit) {
        int aChannelIdx;
        _aChannel->getValue(aChannelIdx);
        std::string optionName;
        _aChannel->getOption(aChannelIdx, optionName);
        _aChannelName->setValue(optionName);
    } else if (_outputLayerString && paramName == kParamChannelOutputLayer) {
        int index;
        _outputLayer->getValue(index);
        std::string optionName;
        _outputLayer->getOption(index, optionName);
        if (args.reason == OFX::eChangeUserEdit) {
            _outputLayerString->setValue(optionName);
        }
        
        for (LayersUnionVect::iterator it = _layersUnion.begin(); it!=_layersUnion.end(); ++it) {
            if (it->second.choiceOption == optionName) {
                OFX::PixelComponentEnum comps;
                switch (it->second.layer.channelNames.size()) {
                    case 1:
                        comps = OFX::ePixelComponentAlpha;
                        break;
                    case 3:
                        comps = OFX::ePixelComponentRGB;
                        break;
                    case 4:
                    default:
                        comps = OFX::ePixelComponentRGBA;
                        break;
                }
                setOutputComponents(comps);
                break;
            }
        }
    } else {
        GenericReaderPlugin::changedParam(args,paramName);
    }
}

void
ReadOIIOPlugin::onOutputComponentsParamChanged(OFX::PixelComponentEnum components)
{
    if (_useRGBAChoices) {
        updateChannelMenusVisibility(components);
    }
}

void
ReadOIIOPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    GenericReaderPlugin::getClipPreferences(clipPreferences);
    
}

void
ReadOIIOPlugin::getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents)
{
    //Should only be called if multi-planar
    assert(isMultiPlanar());
    
    clipComponents.addClipComponents(*_outputClip, getOutputComponents());
    clipComponents.setPassThroughClip(NULL, args.time, args.view);
    
    if (_specValid) {
        if (_useRGBAChoices) {
            std::map<std::string,std::vector<std::string> > layers;
            for (std::size_t s = 0; s < _subImagesSpec.size(); ++s) {
                for (int i = 0; i < _subImagesSpec[s].nchannels; ++i) {
                    if (i < (int)_subImagesSpec[s].channelnames.size()) {
                        const std::string& chan = _subImagesSpec[s].channelnames[i];
                        std::size_t foundLastDot = chan.find_last_of('.');
                        
                        //Consider everything before the last dot as being a layer
                        if (foundLastDot != std::string::npos) {
                            std::string layer = chan.substr(0,foundLastDot);
                            std::string channel = chan.substr(foundLastDot + 1,std::string::npos);
                            
                            std::map<std::string,std::vector<std::string> >::iterator foundLayer = layers.find(layer);
                            if (foundLayer == layers.end()) {
                                ///Add a new vector of channels for the layer
                                std::vector<std::string> chanVec;
                                chanVec.push_back(channel);
                                layers.insert(std::make_pair(layer, chanVec));
                                
                            } else {
                                ///Complete the vector
                                foundLayer->second.push_back(channel);
                            }
                            
                        } else {
                            //The channel does not have a layer prefix, it is either R,G,B,A or a custom single channel component
                            //If RGBA, don't consider it as it is already considered with the output components.
                            if (chan != "R" && chan != "r" && chan != "red" &&
                                chan != "G" && chan != "g" && chan != "green" &&
                                chan != "B" && chan != "b" && chan != "blue" &&
                                chan != "A" && chan != "a" && chan != "alpha") {
                                
                                std::vector<std::string> chanVec;
                                chanVec.push_back(chan);
                                layers.insert(std::make_pair(chan, chanVec));
                            }
                        }
                    }
                }
            }
            for (std::map<std::string,std::vector<std::string> >::iterator it = layers.begin(); it!=layers.end(); ++it) {
                std::string component = OFX::MultiPlane::Utils::makeNatronCustomChannel(it->first, it->second);
                clipComponents.addClipComponents(*_outputClip, component);
            }
        } else { // !_useRGBAChoices
            OFX::MultiThread::AutoMutex lock(_layersMapMutex);
            for (LayersUnionVect::iterator it = _layersUnion.begin(); it != _layersUnion.end(); ++it) {
                std::string component;
                if (it->first == kReadOIIOColorLayer) {
                    continue;
                    /*switch (it->second.layer.channelNames.size()) {
                        case 1:
                            component = kOfxImageComponentAlpha;
                            break;
                        case 3:
                            component = kOfxImageComponentRGB;
                            break;
                        case 4:
                        default:
                            component = kOfxImageComponentRGBA;
                            break;
                    };*/
                } else {
                    component = OFX::MultiPlane::Utils::makeNatronCustomChannel(it->first, it->second.layer.channelNames);
                }
                clipComponents.addClipComponents(*_outputClip, component);
            }
        }
    }
}

namespace  {
/*static bool startsWith(const std::string& str,
                       const std::string& prefix)
{
    return str.substr(0,prefix.size()) == prefix;
}*/

static bool endsWith(const std::string &str, const std::string &suffix)
{
    return ((str.size() >= suffix.size()) &&
            (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0));
}

/*
 * @brief Remap channel, to a known channel name, that is a single upper case letter
 */
static std::string remapToKnownChannelName(const std::string& channel)
{

        
    if (channel == "r" || channel == "red" || channel == "RED" || channel == "Red") {
        return "R";
    }
    
    if (channel == "g" || channel == "green" || channel == "GREEN" || channel == "Green") {
        return "G";
    }
        
    if (channel == "b" || channel == "blue" || channel == "BLUE" || channel == "Blue") {
        return "B";
    }
 
    if (channel == "a" || channel == "alpha" || channel == "ALPHA" || channel == "Alpha") {
        return "A";
    }
        
    if (channel == "z" || channel == "depth" || channel == "DEPTH" || channel == "Depth") {
        return "Z";
    }
        
    return channel;
}

///Returns true if one is found
static bool hasDuplicate(const LayersMap& layers, const std::string& layer, const std::string& channel)
{
    //Try to find an existing layer, or a duplicate
    for (std::size_t c = 0; c < layers.size(); ++c) {
        if (layers[c].first == layer) {
            for (std::size_t i = 0; i < layers[c].second.channelNames.size(); ++i) {
                if (layers[c].second.channelNames[i] == channel) {
                    return true;
                }
            }
            break;
        }
    }
    return false;
}


///encodedLayerName is in the format view.layer.channel
static void extractLayerName(const std::string& encodedLayerName, std::string* viewName, std::string* layerName, std::string* channelName)
{
    ///if there is a layer/view prefix, this will be non empty
    std::string layerDotPrefix;
    
    size_t lastdot = encodedLayerName.find_last_of(".");
    if (lastdot != std::string::npos) {
        layerDotPrefix = encodedLayerName.substr(0, lastdot);
        *channelName = encodedLayerName.substr(lastdot + 1);
        *channelName = remapToKnownChannelName(*channelName);
    } else {
        *channelName = encodedLayerName;
        *channelName = remapToKnownChannelName(*channelName);
        return;
    }
    size_t firstDot = layerDotPrefix.find_first_of(".");
    if (firstDot != std::string::npos) {
        *viewName = layerDotPrefix.substr(0, firstDot);
        *layerName = layerDotPrefix.substr(firstDot + 1);
    } else {
        *layerName = layerDotPrefix;
    }
    
}



//e.g: find "X" in view.layer.z
static bool hasChannelName(const std::string& viewName,
                            const std::string& layerName,
                            const std::string& mappedChannelName,
                            const std::vector<std::string>& originalUnMappedNames)
{
    for (std::size_t i = 0; i < originalUnMappedNames.size(); ++i) {
        std::string view,layer,channel;
        extractLayerName(originalUnMappedNames[i], &view, &layer, &channel);
        if (viewName != view || layerName != layer) {
            continue;
        }
        if (channel == mappedChannelName) {
            return true;
        }
    }
    return false;
}

static std::string toLowerString(const std::string& str)
{
    std::string ret;
    std::locale loc;
    for (std::size_t i = 0; i < str.size(); ++i) {
        ret.push_back(std::tolower(str[i],loc));
    }
    return ret;
}

static bool caseInsensitiveCompare(const std::string& lhs, const std::string& rhs)
{
    std::string lowerLhs = toLowerString(lhs);
    std::string lowerRhs = toLowerString(rhs);
    return lowerLhs == lowerRhs;
}

} // anon namespace

void
ReadOIIOPlugin::layersMapFromSubImages(const std::vector<ImageSpec>& subimages, ViewsLayersMap* layersMap, LayersUnionVect* layersUnion)
{
    assert(!subimages.empty());
    
    std::vector<std::string> views;
    
    
    /*
     First off, detect views.
     */
    std::vector<std::string> partsViewAttribute;
    if (subimages.size() == 1) {
        //Check the "multiView" property
        //We have to pass TypeDesc::UNKNOWN because if we pass TypeDesc::String OIIO will also check the type length which is encoded in the type
        //but we do not know it yet
        //See https://github.com/OpenImageIO/oiio/issues/1247
        const ParamValue* multiviewValue = subimages[0].find_attribute("multiView", TypeDesc::UNKNOWN);
        if (multiviewValue) {
            
            ///This is the only way to retrieve the array size currently, see issue above
            int nValues = multiviewValue->type().arraylen;
            const ustring* dataPtr = (const ustring*)multiviewValue->data();
            for (int i = 0; i < nValues ; ++i) {
                std::string view(dataPtr[i].data());
                if (!view.empty()) {
                    if (std::find(views.begin(), views.end(), view) == views.end()) {
                        views.push_back(view);
                    }
                }
            }
        }
    } else {
        //Check for each subimage the "view" property
        partsViewAttribute.resize(subimages.size());
        for (std::size_t i = 0; i < subimages.size(); ++i) {
            const ParamValue* viewValue = subimages[i].find_attribute("view", TypeDesc::STRING);
            bool viewPartAdded = false;
            if (viewValue) {
                const char* dataPtr = *(const char**)viewValue->data();
                std::string view = std::string(dataPtr);
                if (!view.empty()) {
                    if (std::find(views.begin(), views.end(), view) == views.end()) {
                        views.push_back(view);
                    }
                    viewPartAdded = true;
                    partsViewAttribute[i] = view;
                }
            }
            if (!viewPartAdded) {
                partsViewAttribute[i] = std::string();
            }
        }
    }
    
    std::string viewsEncoded;
    for (std::size_t i = 0; i < views.size(); ++i) {
        
        viewsEncoded.append(views[i]);
        if (i < views.size() - 1) {
            viewsEncoded.push_back(',');
        }
        layersMap->push_back(std::make_pair(views[i], LayersMap()));
    }
    
    
    if (views.empty()) {
        layersMap->push_back(std::make_pair("Main", LayersMap()));
    }
    
    
    
    ///Layers are considered to be named as view.layer.channels. If no view prefix then it is considered to be part of the "main" view
    ///that is, the first view declared.
    
    for (std::size_t i = 0; i < subimages.size(); ++i) {
        for (int j = 0; j < subimages[i].nchannels; ++j) {
            std::string layerChanName;
            if (j >= (int)subimages[i].channelnames.size()) {
                //give it a generic name since it's not in the channelnames
                std::stringstream ss;
                ss << "channel " << i;
                layerChanName = ss.str();
            } else {
                layerChanName = subimages[i].channelnames[j];
            }
            
            //Extract the view layer and channel to our format so we can compare strings
            std::string originalView,originalLayer,channel;
            extractLayerName(layerChanName, &originalView, &originalLayer, &channel);
            std::string view = originalView;
            std::string layer = originalLayer;
            
            if (view.empty() && !partsViewAttribute.empty() && i < partsViewAttribute.size() && !partsViewAttribute[i].empty()) {
                view = partsViewAttribute[i];
            }
            if (view.empty() && !layer.empty()) {
                ///Check if the layer we parsed is in fact not a view name
                for (std::size_t v = 0; v < views.size(); ++v) {
                    if (caseInsensitiveCompare(views[v],layer)) {
                        view = layer;
                        layer.clear();
                        break;
                    }
                }
            }
            
            ViewsLayersMap::iterator foundView = layersMap->end();
            if (view.empty()) {
                ///Set to main view (view 0)
                foundView = layersMap->begin();
            } else {
                for (ViewsLayersMap::iterator it = layersMap->begin(); it!=layersMap->end(); ++it) {
                    if (it->first == view) {
                        foundView = it;
                        break;
                    }
                }
            }
            if (foundView == layersMap->end()) {
                //The view does not exist in the metadata, this is probably a channel named aaa.bbb.c, just concatenate aaa.bbb as a single layer name
                //and put it in the "Main" view
                layer = view + "." + layer;
                view.clear();
                foundView = layersMap->begin();
            }
            
            assert(foundView != layersMap->end());
            
            //If the layer name is empty, try to map it to something known
            if (layer.empty()) {
                //channel  has already been remapped to our formatting of channels, i.e: 1 upper-case letter
                if (channel == "R" || channel == "G" || channel == "B" || channel == "A" || channel == "I") {
                    layer = kReadOIIOColorLayer;
                } else if (channel == "X") {
                    //try to put XYZ together, unless Z is alone
                    bool hasY = hasChannelName(originalView, originalLayer, "Y", subimages[i].channelnames);
                    bool hasZ = hasChannelName(originalView, originalLayer, "Z", subimages[i].channelnames);
                    if (hasY && hasZ) {
                        layer = kReadOIIOXYZLayer;
                    }
                } else if (channel == "Y") {
                    //try to put XYZ together, unless Z is alone
                    bool hasX = hasChannelName(originalView, originalLayer, "X", subimages[i].channelnames);
                    bool hasZ = hasChannelName(originalView, originalLayer, "Z", subimages[i].channelnames);
                    if (hasX && hasZ) {
                        layer = kReadOIIOXYZLayer;
                    }
                } else if (channel == "Z") {
                    //try to put XYZ together, unless Z is alone
                    bool hasX = hasChannelName(originalView, originalLayer, "X", subimages[i].channelnames);
                    bool hasY = hasChannelName(originalView, originalLayer, "Y", subimages[i].channelnames);
                    if (hasX && hasY) {
                        layer = kReadOIIOXYZLayer;
                    } else {
                        layer = kReadOIIODepthLayer;
                    }
                }
            }
            
            //The layer is still empty, put the channel alone in a new layer
            if (layer.empty()) {
                layer = channel;
            }
            
            //There may be duplicates, e.g: 2 parts of a EXR file with same RGBA layer, we have no choice but to prepend the part index
            {
                int attempts = 1;
                std::string baseLayerName = layer;
                while (hasDuplicate(foundView->second, layer, channel)) {
                    std::stringstream ss;
                    
                    ss << "Part" << attempts;
                    
                    ss << '.' << baseLayerName;
                    layer = ss.str();
                    ++attempts;
                }
            }
            
            assert(!layer.empty());
            
            int layerIndex = -1;
            for (std::size_t c = 0; c < foundView->second.size(); ++c) {
                if (foundView->second[c].first == layer) {
                    layerIndex = (int)c;
                    break;
                }
            }
            if (layerIndex == -1) {
                foundView->second.push_back(std::make_pair(layer,LayerChannelIndexes()));
                layerIndex = (int)foundView->second.size() - 1;
            }
            //Now we are sure there are no duplicates
            foundView->second[layerIndex].second.subImageIdx = i;
            foundView->second[layerIndex].second.channelIndexes.push_back(j);
            foundView->second[layerIndex].second.channelNames.push_back(channel);
        } // for (int j = 0; j < _subImagesSpec[i].nchannels; ++j) {
    } // for (std::size_t i = 0; i < _subImagesSpec.size(); ++i) {
    
    
    ///Union all layers across views
    if (layersUnion) {
        for (ViewsLayersMap::iterator it = layersMap->begin(); it!=layersMap->end(); ++it) {
            for (LayersMap::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
                
                LayersUnionVect::iterator found = layersUnion->end();
                for (LayersUnionVect::iterator it3 = layersUnion->begin(); it3 != layersUnion->end(); ++it3) {
                    if (it3->first == it2->first) {
                        found = it3;
                        break;
                    }
                }
                
                
                if (found == layersUnion->end()) {
                    // We did not find a view in the layersUnion with this name
                    LayerUnionData d;
                    d.layer = it2->second;
                    d.views.push_back(it->first);
                    layersUnion->push_back(std::make_pair(it2->first, d));
                    
                } else {
                    // We already found a view in the layersUnion with this name
                    if (views.size() > 1) {
                        //register views that have this layer
                        found->second.views.push_back(it->first);
                    }
                }
            }
        }
    }
}

void
ReadOIIOPlugin::buildLayersMenu()
{
    assert(gHostSupportsMultiPlane && gHostSupportsDynamicChoices);
    assert(!_useRGBAChoices);
    assert(_specValid);
    if (!_specValid) {
        return;
    }
    assert(!_subImagesSpec.empty());
    
    std::vector<std::string> options,optionsLabel;

    //Protect the map
    {
        OFX::MultiThread::AutoMutex lock(_layersMapMutex);
        _layersUnion.clear();
        
        ViewsLayersMap layersMap;
        layersMapFromSubImages(_subImagesSpec, &layersMap, &_layersUnion);
        
        std::string viewsEncoded;
        for (std::size_t i = 0; i < layersMap.size(); ++i) {
        
            viewsEncoded.append(layersMap[i].first);
            if (i < layersMap.size() - 1) {
                viewsEncoded.push_back(',');
            }
        }
        
        _availableViews->setValue(viewsEncoded);

        

        ///Now build the choice options
        for (std::size_t i = 0; i < _layersUnion.size(); ++i) {
            const std::string& layerName = _layersUnion[i].first;
            std::string choice;
            if (layerName == kReadOIIOColorLayer) {
                switch (_layersUnion[i].second.layer.channelNames.size()) {
                    case 1:
                        choice = kReadOIIOColorLayer ".Alpha";
                        break;
                    default: {
                        choice.append(kReadOIIOColorLayer ".");
                        for (std::size_t j = 0; j < _layersUnion[i].second.layer.channelNames.size(); ++j) {
                            choice.append(_layersUnion[i].second.layer.channelNames[j]);
                        }
                    }   break;
                }
            } else if (_layersUnion[i].second.layer.channelNames.size() == 1 && layerName == _layersUnion[i].second.layer.channelNames[0]) {
                //Depth.Depth for instance
                for (std::size_t j = 0; j < _layersUnion[i].second.layer.channelNames.size(); ++j) {
                    choice.append(_layersUnion[i].second.layer.channelNames[j]);
                }

            }   else {
                choice.append(layerName);
                choice.push_back('.');
                for (std::size_t j = 0; j < _layersUnion[i].second.layer.channelNames.size(); ++j) {
                    choice.append(_layersUnion[i].second.layer.channelNames[j]);
                }
            }
            
            
            std::string optionLabel;
            if (layersMap.size() > 1) {
                std::stringstream ss;
                ss << "Present in views: ";
                for (std::size_t j = 0; j < _layersUnion[i].second.views.size(); ++j) {
                    ss << _layersUnion[i].second.views[j];
                    if (j < _layersUnion[i].second.views.size() - 1) {
                        ss << ", ";
                    }
                }
                optionLabel = ss.str();
            }
            options.push_back(choice);
            optionsLabel.push_back(optionLabel);
            _layersUnion[i].second.choiceOption = choice;
        }
        
        assert(options.size() == _layersUnion.size());
        
    } // OFX::MultiThread::AutoMutex lock(_layersMapMutex);
    
    
    
    ///Actually build the menu
    _outputLayer->resetOptions(options, optionsLabel);

    
   ///synchronize with the value stored in the string param
    std::string valueStr;
    _outputLayerString->getValue(valueStr);
    if (valueStr.empty()) {
        int cur_i;
        _outputLayer->getValue(cur_i);
        if (cur_i >= 0 && cur_i < (int)options.size()) {
            valueStr = options[cur_i];
        } else if (!options.empty()) {
            //No choice but to change the chocie value
            valueStr = options[0];
            _outputLayer->setValue(0);
        }
        _outputLayerString->setValue(valueStr);
    } else {
        int foundOption = -1;
        for (int i = 0; i < (int)options.size(); ++i) {
            if (options[i] == valueStr) {
                foundOption = i;
                break;
            }
        }
        if (foundOption != -1) {
            _outputLayer->setValue(foundOption);
        } else {
            _outputLayer->setValue(0);
            _outputLayerString->setValue(options[0]);
        }
    }

} // buildLayersMenu

void
ReadOIIOPlugin::buildChannelMenus()
{
    
    assert(_useRGBAChoices);
    
    if (gHostSupportsDynamicChoices) {
        // the choice menu can only be modified in Natron
        // Natron supports changing the entries in a choiceparam
        // Nuke (at least up to 9.0v1) does not
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
        if (_specValid) {
            for (int i = 0; i < _subImagesSpec[0].nchannels; ++i) {
                if (i < (int)_subImagesSpec[0].channelnames.size()) {
                    _rChannel->appendOption(_subImagesSpec[0].channelnames[i]);
                    _bChannel->appendOption(_subImagesSpec[0].channelnames[i]);
                    _gChannel->appendOption(_subImagesSpec[0].channelnames[i]);
                    _aChannel->appendOption(_subImagesSpec[0].channelnames[i]);
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
}

// called when the red channel is set
// from the red channel name, infer the corresponding G,B,A channel values
void
ReadOIIOPlugin::setDefaultChannelsFromRed(int rChannelIdx, bool mustSetChannelNames, OFX::PixelComponentEnum *components)
{
    assert(_useRGBAChoices);
    
    assert(rChannelIdx >= 0);
    if (!_specValid) {
        return;
    }
    int channelsVisitedSoFar = 0;
    std::string rFullName;
    int specIndex = -1;
    for (std::size_t s = 0; s < _subImagesSpec.size(); ++s) {
        if (rChannelIdx < channelsVisitedSoFar + _subImagesSpec[s].nchannels && rChannelIdx >= channelsVisitedSoFar) {
            rFullName = _subImagesSpec[s].channelnames[rChannelIdx - channelsVisitedSoFar];
            specIndex = (int)s;
            break;
        }
        channelsVisitedSoFar += _subImagesSpec[s].nchannels;
    }
    if (rFullName.empty()) {
        // no name, can't do anything
        return;
    }
    assert(specIndex != -1);
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
    int layerViewChannels = 0;
    for (size_t i = 0; i < _subImagesSpec[specIndex].channelnames.size(); ++i) {
        // check if the channel is within the layer/view, and count number of channels
        size_t channelDot = _subImagesSpec[specIndex].channelnames[i].find_last_of(".");
        if (lastdot == std::string::npos ||
            _subImagesSpec[specIndex].channelnames[i].compare(0, layerDotViewDot.length(), layerDotViewDot) == 0) {
            if ((lastdot == std::string::npos && channelDot == std::string::npos) ||
                (lastdot != std::string::npos && channelDot != std::string::npos)) {
                ++layerViewChannels;
            }
            if (_subImagesSpec[specIndex].channelnames[i] == gFullName) {
                _gChannel->setValue(kXChannelFirst + i);
                if (mustSetChannelNames) {
                    std::string optionName;
                    _gChannel->getOption(kXChannelFirst + i, optionName);
                    _gChannelName->setValue(optionName);
                }
                gSet = true;
            }
            if (_subImagesSpec[specIndex].channelnames[i] == bFullName) {
                _bChannel->setValue(kXChannelFirst + i);
                if (mustSetChannelNames) {
                    std::string optionName;
                    _bChannel->getOption(kXChannelFirst + i, optionName);
                    _bChannelName->setValue(optionName);
                }
                bSet = true;
            }
            if (_subImagesSpec[specIndex].channelnames[i] == aFullName) {
                _aChannel->setValue(kXChannelFirst + i);
                if (mustSetChannelNames) {
                    std::string optionName;
                    _aChannel->getOption(kXChannelFirst + i, optionName);
                    _aChannelName->setValue(optionName);
                }
                aSet = true;
            }
        }
    }
    if (!gSet) {
        _gChannel->setValue(0);
        if (mustSetChannelNames) {
            std::string optionName;
            _gChannel->getOption(0, optionName);
            _gChannelName->setValue(optionName);
        }
    }
    if (!bSet) {
        _bChannel->setValue(0);
        if (mustSetChannelNames) {
            std::string optionName;
            _bChannel->getOption(0, optionName);
            _bChannelName->setValue(optionName);
        }
    }
    if (!aSet) {
        if (_subImagesSpec[specIndex].alpha_channel >= 0) {
            _aChannel->setValue(kXChannelFirst + _subImagesSpec[specIndex].alpha_channel);
            if (mustSetChannelNames) {
                std::string optionName;
                _aChannel->getOption(kXChannelFirst + _subImagesSpec[specIndex].alpha_channel, optionName);
                _aChannelName->setValue(optionName);
            }
        } else if (layerViewChannels != 4) {
            // Output is Opaque with alpha=0 by default,
            // but premultiplication is set to opaque.
            // That way, chaining with a Roto node works correctly.
            // Alpha is set to 0 and premult is set to Opaque.
            // That way, the Roto node can be conveniently used to draw a mask. This shouldn't
            // disturb anything else in the process, since Opaque premult means that alpha should
            // be considered as being 1 everywhere, whatever the actual alpha value is.
            // see GenericWriterPlugin::render, if (userPremult == OFX::eImageOpaque...
            if (components && *components == OFX::ePixelComponentRGBA) {
                *components = OFX::ePixelComponentRGB;
            }
            _aChannel->setValue(0);
            if (mustSetChannelNames) {
                std::string optionName;
                _aChannel->getOption(0, optionName);
                _aChannelName->setValue(optionName);
            }
        } else {
            // if there are exactly 4 channels in this layer/view, then the
            // remaining one should be Alpha
            for (size_t i = 0; i < _subImagesSpec[specIndex].channelnames.size(); ++i) {
                // check if the channel is within the layer/view
                if (lastdot == std::string::npos ||
                    _subImagesSpec[specIndex].channelnames[i].compare(0, layerDotViewDot.length(), layerDotViewDot) == 0) {
                    if (_subImagesSpec[specIndex].channelnames[i] != rFullName &&
                        _subImagesSpec[specIndex].channelnames[i] != gFullName &&
                        _subImagesSpec[specIndex].channelnames[i] != bFullName) {
                        _aChannel->setValue(kXChannelFirst + i);
                        if (mustSetChannelNames) {
                            std::string optionName;
                            _aChannel->getOption(kXChannelFirst + i, optionName);
                            _aChannelName->setValue(optionName);
                        }
                    }
                }
            }
        }
    }
}



// called after changing the filename, set all channels
void
ReadOIIOPlugin::setDefaultChannels(OFX::PixelComponentEnum *components)
{
    assert(_useRGBAChoices);
    if (!_specValid) {
        return;
    }
    {
        int rChannelIdx = -1;

        // first, look for the main red channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "R" ||
                _subImagesSpec[0].channelnames[i] == "r" ||
                _subImagesSpec[0].channelnames[i] == "red") {
                rChannelIdx = i;
                break; // found!
            }
        }

        if (rChannelIdx < 0) {
            // find a name which ends with ".R", ".r" or ".red"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (endsWith(_subImagesSpec[0].channelnames[i], ".R") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".r") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".red")) {
                    rChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (rChannelIdx >= 0) {
            // red was found
            _rChannel->setValue(kXChannelFirst + rChannelIdx);
            setDefaultChannelsFromRed(rChannelIdx, false, components);
            return;
        } else if (_subImagesSpec[0].nchannels >= 3) {
            _rChannel->setValue(kXChannelFirst + 0);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _rChannel->setValue(kXChannelFirst);
        } else {
            _rChannel->setValue(0);
        }
    }
    // could not find red. look for green, blue, alpha.
    {
        int gChannelIdx = -1;

        // first, look for the main green channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "G" ||
                _subImagesSpec[0].channelnames[i] == "g" ||
                _subImagesSpec[0].channelnames[i] == "green") {
                gChannelIdx = i;
                break; // found!
            }
        }

        if (gChannelIdx < 0) {
            // find a name which ends with ".G", ".g" or ".green"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (endsWith(_subImagesSpec[0].channelnames[i], ".G") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".g") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".green")) {
                    gChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (gChannelIdx >= 0) {
            // green was found
            _gChannel->setValue(kXChannelFirst + gChannelIdx);
        } else if (_subImagesSpec[0].nchannels >= 3) {
            _gChannel->setValue(kXChannelFirst + 1);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _gChannel->setValue(kXChannelFirst);
        } else {
            _gChannel->setValue(0);
        }
    }
    {
        int bChannelIdx = -1;

        // first, look for the main blue channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "B" ||
                _subImagesSpec[0].channelnames[i] == "b" ||
                _subImagesSpec[0].channelnames[i] == "blue") {
                bChannelIdx = i;
                break; // found!
            }
        }

        if (bChannelIdx < 0) {
            // find a name which ends with ".B", ".b" or ".blue"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (endsWith(_subImagesSpec[0].channelnames[i], ".B") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".b") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".blue")) {
                    bChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (bChannelIdx >= 0) {
            // blue was found
            _bChannel->setValue(kXChannelFirst + bChannelIdx);
        } else if (_subImagesSpec[0].nchannels >= 3) {
            _bChannel->setValue(kXChannelFirst + 2);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _bChannel->setValue(kXChannelFirst);
        } else {
            _bChannel->setValue(0);
        }
    }
    {
        int aChannelIdx = -1;

        // first, look for the main alpha channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "A" ||
                _subImagesSpec[0].channelnames[i] == "a" ||
                _subImagesSpec[0].channelnames[i] == "alpha") {
                aChannelIdx = i;
                break; // found!
            }
        }

        if (aChannelIdx < 0) {
            // find a name which ends with ".A", ".a" or ".alpha"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (endsWith(_subImagesSpec[0].channelnames[i], ".A") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".a") ||
                    endsWith(_subImagesSpec[0].channelnames[i], ".alpha")) {
                    aChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (aChannelIdx >= 0) {
            // alpha was found
            _aChannel->setValue(kXChannelFirst + aChannelIdx);
        } else if (_subImagesSpec[0].nchannels >= 4) {
            _aChannel->setValue(kXChannelFirst + 3);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _aChannel->setValue(kXChannelFirst);
        } else {
            if (components && *components == OFX::ePixelComponentRGBA) {
                *components = OFX::ePixelComponentRGB; // so that premult is set to opaque
            }
            _aChannel->setValue(0);
        }
    }
}

void
ReadOIIOPlugin::restoreChannelMenusFromStringParams()
{
    assert(_useRGBAChoices);
    
    OFX::ChoiceParam* channelParams[4] = { _rChannel, _gChannel, _bChannel, _aChannel };
    OFX::StringParam* stringParams[4] = { _rChannelName, _gChannelName, _bChannelName, _aChannelName };

    for (int c = 0; c < 4; ++c) {
        
        std::string channelString;
        stringParams[c]->getValue(channelString);

        bool channelSet = false;

        if ( !channelString.empty() ) {
            // Restore the index from the serialized string
            for (int i = 0; i < channelParams[c]->getNOptions(); ++i) {
                std::string option;
                channelParams[c]->getOption(i, option);
                if (option == channelString) {
                    channelParams[c]->setValue(i);
                    channelSet = true;
                    break;
                }
            }
        }
        if (!channelSet) {
            // We are in the case where the strings were not serialized (or didn't exist in that project),
            // or the named channel doesnt exist,
            // so we blindly trust the values in the channels
            
            //Edit: don't do this if the channel menus are empty (i.e: containing only 2 entries (0 and 1)) otherwise
            //we will always set the strings to "0" when building new instances of the plug-in and when
            //inputFileChanged() will be called further on, it will set the channel index to 0 since the string won't be empty!
            int nChoices = channelParams[c]->getNOptions();
            if (nChoices > 2) {
                int idx;
                channelParams[c]->getValue(idx);
                std::string option;
                channelParams[c]->getOption(idx, option);
                assert(option != channelString); // if they were equal, it should have triggered channelSet = true above
                stringParams[c]->setValue(option);
            }
        }
    }

}

void
ReadOIIOPlugin::getSpecsFromImageInput(ImageInput* img, std::vector<ImageSpec>& subimages) const
{
    int subImageIndex = 0;
    ImageSpec spec;
    while (img->seek_subimage(subImageIndex, 0, spec)) {
        subimages.push_back(spec);
        ++subImageIndex;
#ifndef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
        break;
#endif
    }
}

void
ReadOIIOPlugin::getSpecsFromCache(const std::string& filename, std::vector<ImageSpec>& subimages) const
{
    ImageSpec spec;
    int subImageIndex = 0;
    while (_cache->get_imagespec(ustring(filename), spec, subImageIndex)) {
        subimages.push_back(spec);
        ++subImageIndex;
#ifndef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
        break;
#endif
    }
}

void
ReadOIIOPlugin::updateSpec(const std::string &filename)
{
    _specValid = false;
    _subImagesSpec.clear();
# ifdef OFX_READ_OIIO_USES_CACHE
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    getSpecsFromCache(filename, _subImagesSpec);

# else
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        return;
    }
    getSpecsFromImageInput(img, _subImagesSpec);
# endif
    if (_subImagesSpec.empty()) {
        return;
    }
    _specValid = true;
    
    for (std::size_t i = 0; i < _subImagesSpec.size(); ++i) {
        if (_subImagesSpec[i].deep) {
            if (_subImagesSpec[0].deep) {
                sendMessage(OFX::Message::eMessageError, "", "Cannot read deep images yet.");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                _subImagesSpec.clear();
                _specValid = false;
                _layersUnion.clear();
                return;
            }
        }
    }
    
#ifdef OFX_READ_OIIO_USES_CACHE
    //Only support tiles if tile size is set
    int fullHeight = _subImagesSpec[0].full_height == 0 ? _subImagesSpec[0].height : _subImagesSpec[0].full_height;
    int fullWidth = _subImagesSpec[0].full_width == 0 ? _subImagesSpec[0].width : _subImagesSpec[0].full_width;
    
    setSupportsTiles(_subImagesSpec[0].tile_width != 0 && _subImagesSpec[0].tile_width != fullWidth && _subImagesSpec[0].tile_height != 0 && _subImagesSpec[0].tile_height != fullHeight);
#endif
}

void
ReadOIIOPlugin::restoreState(const std::string& filename)
{
    
    if (_useRGBAChoices) {
        //Update OIIO spec
        updateSpec(filename);

        //Update RGBA parameters visibility according to the output components
        updateChannelMenusVisibility(getOutputComponents());
        
        //Build available channels from OIIO spec
        buildChannelMenus();
        // set the default values for R, G, B, A channels
        setDefaultChannels(NULL);
        //Restore channels from the channel strings serialized
        restoreChannelMenusFromStringParams();
    } else {
        OFX::PreMultiplicationEnum premult;
        OFX::PixelComponentEnum comps;
        int compsNum;
        try {
            onInputFileChanged(filename, true, &premult, &comps, &compsNum);
        } catch (...) {
            
        }
    }
    
    ///http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#SettingParams
    ///The Create instance action is in the list of actions where you can set param values
    
    
    
}

void
ReadOIIOPlugin::setOCIOColorspacesFromSpec(const std::string& filename) {
    
    
#     ifdef OFX_IO_USING_OCIO
    ///find-out the image color-space
    const ParamValue* colorSpaceValue = _subImagesSpec[0].find_attribute("oiio:ColorSpace", TypeDesc::STRING);
    const ParamValue* photoshopICCProfileValue = _subImagesSpec[0].find_attribute("photoshop:ICCProfile", TypeDesc::STRING);
    //photoshop:ICCProfile: "HDTV (Rec. 709)"
    
    //we found a color-space hint, use it to do the color-space conversion
    const char* colorSpaceStr = NULL;
    if (colorSpaceValue) {
        colorSpaceStr = *(const char**)colorSpaceValue->data();
    } else if (photoshopICCProfileValue) {
        const char* ICCProfileStr = *(const char**)photoshopICCProfileValue->data();
        if (!strcmp(ICCProfileStr, "HDTV (Rec. 709)") ||
            !strcmp(ICCProfileStr, "SDTV NTSC") ||
            !strcmp(ICCProfileStr, "SDTV PAL") ||
            !strcmp(ICCProfileStr, "HDTV (Rec. 709) 16-235") ||
            !strcmp(ICCProfileStr, "SDTV NTSC 16-235") ||
            !strcmp(ICCProfileStr, "SDTV PAL 16-235") ||
            !strcmp(ICCProfileStr, "SDTV NTSC 16-235")) {
            colorSpaceStr = "Rec709";
        } else if (!strcmp(ICCProfileStr, "sRGB IEC61966-2.1")) {
            colorSpaceStr = "sRGB";
        } else if (!strcmp(ICCProfileStr, "Universal Camera Film Printing Density)")) {
            colorSpaceStr = "KodakLog";
        }
    }
    if (!colorSpaceStr) {
        // no colorspace... we'll probably have to try something else, then.
        // we set the following defaults:
        // sRGB for 8-bit images
        // Rec709 for 10-bits, 12-bits or 16-bits integer images
        // Linear for anything else
        switch (_subImagesSpec[0].format.basetype) {
            case TypeDesc::UCHAR:
            case TypeDesc::CHAR:
                colorSpaceStr = "sRGB";
                break;
            case TypeDesc::USHORT:
            case TypeDesc::SHORT:
                if (endsWith(filename, ".cin") || endsWith(filename, ".dpx") ||
                    endsWith(filename, ".CIN") || endsWith(filename, ".DPX")) {
                    // Cineon or DPX file
                    colorSpaceStr = "KodakLog";
                } else {
                    colorSpaceStr = "Rec709";
                }
                break;
            default:
                colorSpaceStr = "Linear";
                break;
        }
    }
    if (colorSpaceStr) {
        if (!strcmp(colorSpaceStr, "GammaCorrected")) {
            float gamma = _subImagesSpec[0].get_float_attribute("oiio:Gamma");
            if (std::fabs(gamma-1.8) < 0.01) {
                if (_ocio->hasColorspace("Gamma1.8")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma1.8");
                }
            } else if (std::fabs(gamma-2.2) < 0.01) {
                if (_ocio->hasColorspace("Gamma2.2")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma2.2");
                } else if (_ocio->hasColorspace("VD16")) {
                    // VD16 in blender
                    _ocio->setInputColorspace("VD16");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim and spi-vfx
                    _ocio->setInputColorspace("vd16");
                } else if (_ocio->hasColorspace("sRGB")) {
                    // nuke-default and blender
                    _ocio->setInputColorspace("sRGB");
                } else if (_ocio->hasColorspace("sRGB D65")) {
                    // blender-cycles
                    _ocio->setInputColorspace("sRGB D65");
                } else if (_ocio->hasColorspace("sRGB (D60 sim.)")) {
                    // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                    _ocio->setInputColorspace("sRGB (D60 sim.)");
                } else if (_ocio->hasColorspace("out_srgbd60sim")) {
                    // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                    _ocio->setInputColorspace("out_srgbd60sim");
                } else if (_ocio->hasColorspace("rrt_Gamma2.2")) {
                    // rrt_Gamma2.2 in aces 0.7.1
                    _ocio->setInputColorspace("rrt_Gamma2.2");
                } else if (_ocio->hasColorspace("rrt_srgb")) {
                    // rrt_srgb in aces 0.1.1
                    _ocio->setInputColorspace("rrt_srgb");
                } else if (_ocio->hasColorspace("srgb8")) {
                    // srgb8 in spi-vfx
                    _ocio->setInputColorspace("srgb8");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim
                    _ocio->setInputColorspace("vd16");
                }
            }
        } else if(!strcmp(colorSpaceStr, "sRGB")) {
            if (_ocio->hasColorspace("sRGB")) {
                // nuke-default and blender
                _ocio->setInputColorspace("sRGB");
            } else if (_ocio->hasColorspace("sRGB D65")) {
                // blender-cycles
                _ocio->setInputColorspace("sRGB D65");
            } else if (_ocio->hasColorspace("sRGB (D60 sim.)")) {
                // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                _ocio->setInputColorspace("sRGB (D60 sim.)");
            } else if (_ocio->hasColorspace("out_srgbd60sim")) {
                // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                _ocio->setInputColorspace("out_srgbd60sim");
            } else if (_ocio->hasColorspace("rrt_Gamma2.2")) {
                // rrt_Gamma2.2 in aces 0.7.1
                _ocio->setInputColorspace("rrt_Gamma2.2");
            } else if (_ocio->hasColorspace("rrt_srgb")) {
                // rrt_srgb in aces 0.1.1
                _ocio->setInputColorspace("rrt_srgb");
            } else if (_ocio->hasColorspace("srgb8")) {
                // srgb8 in spi-vfx
                _ocio->setInputColorspace("srgb8");
            } else if (_ocio->hasColorspace("Gamma2.2")) {
                // nuke-default
                _ocio->setInputColorspace("Gamma2.2");
            } else if (_ocio->hasColorspace("srgb8")) {
                // srgb8 in spi-vfx
                _ocio->setInputColorspace("srgb8");
            } else if (_ocio->hasColorspace("vd16")) {
                // vd16 in spi-anim
                _ocio->setInputColorspace("vd16");
            }
        } else if(!strcmp(colorSpaceStr, "AdobeRGB")) {
            // ???
        } else if(!strcmp(colorSpaceStr, "Rec709")) {
            if (_ocio->hasColorspace("Rec709")) {
                // nuke-default
                _ocio->setInputColorspace("Rec709");
            } else if (_ocio->hasColorspace("nuke_rec709")) {
                // blender
                _ocio->setInputColorspace("nuke_rec709");
            } else if (_ocio->hasColorspace("Rec.709 - Full")) {
                // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                _ocio->setInputColorspace("Rec.709 - Full");
            } else if (_ocio->hasColorspace("out_rec709full")) {
                // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                _ocio->setInputColorspace("out_rec709full");
            } else if (_ocio->hasColorspace("rrt_rec709_full_100nits")) {
                // rrt_rec709_full_100nits in aces 0.7.1
                _ocio->setInputColorspace("rrt_rec709_full_100nits");
            } else if (_ocio->hasColorspace("rrt_rec709")) {
                // rrt_rec709 in aces 0.1.1
                _ocio->setInputColorspace("rrt_rec709");
            } else if (_ocio->hasColorspace("hd10")) {
                // hd10 in spi-anim and spi-vfx
                _ocio->setInputColorspace("hd10");
            }
        } else if(!strcmp(colorSpaceStr, "KodakLog")) {
            if (_ocio->hasColorspace("Cineon")) {
                // Cineon in nuke-default
                _ocio->setInputColorspace("Cineon");
            } else if (_ocio->hasColorspace("REDlogFilm")) {
                // REDlogFilm in aces 1.0.0
                _ocio->setInputColorspace("REDlogFilm");
            } else if (_ocio->hasColorspace("cineon")) {
                // cineon in aces 0.7.1
                _ocio->setInputColorspace("cineon");
            } else if (_ocio->hasColorspace("adx10")) {
                // adx10 in aces 0.1.1
                _ocio->setInputColorspace("adx10");
            } else if (_ocio->hasColorspace("lg10")) {
                // lg10 in spi-vfx
                _ocio->setInputColorspace("lg10");
            } else if (_ocio->hasColorspace("lm10")) {
                // lm10 in spi-anim
                _ocio->setInputColorspace("lm10");
            } else {
                _ocio->setInputColorspace(OCIO_NAMESPACE::ROLE_COMPOSITING_LOG);
            }
        } else if(!strcmp(colorSpaceStr, "Linear")) {
            _ocio->setInputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
            // lnf in spi-vfx
        } else if (_ocio->hasColorspace(colorSpaceStr)) {
            // maybe we're lucky
            _ocio->setInputColorspace(colorSpaceStr);
        } else {
            // unknown color-space or Linear, don't do anything
        }
    }

#     endif // OFX_IO_USING_OCIO
}

void
ReadOIIOPlugin::onInputFileChanged(const std::string &filename,
                                   bool setColorSpace,
                                   OFX::PreMultiplicationEnum *premult,
                                   OFX::PixelComponentEnum *components,
                                   int *componentCount)
{
    updateSpec(filename);
    if (!_specValid) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    if (setColorSpace) {
        setOCIOColorspacesFromSpec(filename);
    }
    
    if (_useRGBAChoices) {
        
        switch (_subImagesSpec[0].nchannels) {
            case 0:
                *components = OFX::ePixelComponentNone;
                *componentCount = 0;
                break;
            case 1:
                *components = OFX::ePixelComponentAlpha;
                *componentCount = 1;
                break;
            case 3:
                *components = OFX::ePixelComponentRGB;
                *componentCount = 3;
                break;
            case 4:
                *components = OFX::ePixelComponentRGBA;
                *componentCount = 4;
                break;
            default:
                *components = OFX::ePixelComponentRGBA;
                *componentCount = 4;
                break;
        }
        *componentCount = _subImagesSpec[0].nchannels;
        
        // rebuild the channel choices
        buildChannelMenus();
        // set the default values for R, G, B, A channels
        setDefaultChannels(components);
        
        restoreChannelMenusFromStringParams();
    } else {
        buildLayersMenu();
        if (!_layersUnion.empty()) {
            const std::vector<std::string>& channels = _layersUnion[0].second.layer.channelNames;
            switch (channels.size()) {
                case 0:
                    *components = OFX::ePixelComponentNone;
                    *componentCount = 0;
                    break;
                case 1:
                    *components = OFX::ePixelComponentAlpha;
                    *componentCount = 1;
                    break;
                case 3:
                    *components = OFX::ePixelComponentRGB;
                    *componentCount = 3;
                    break;
                case 4:
                    *components = OFX::ePixelComponentRGBA;
                    *componentCount = 4;
                    break;
                case 2: {
                    //in OIIO, PNG with alpha are stored with as a 2-channel image
                    bool hasI = false;
                    bool hasA = false;
                    for (std::size_t i = 0; i < channels.size(); ++i) {
                        if (channels[i] == "I" || channels[i] == "i") {
                            hasI = true;
                        }
                        if (channels[i] == "A" || channels[i] == "a") {
                            hasA = true;
                        }
                    }
                    if (hasI && hasA) {
                        *components = OFX::ePixelComponentRGBA;
                        *componentCount = 4;
                    } else {
                        *components = OFX::ePixelComponentXY;
                        *componentCount = 2;
                    }
                }   break;
                default:
                    *components = OFX::ePixelComponentRGBA;
                    *componentCount = 4;
                    break;
            }
            *componentCount = _subImagesSpec[0].nchannels;
        }
    }

    if (*components != OFX::ePixelComponentRGBA && *components != OFX::ePixelComponentAlpha) {
        *premult = OFX::eImageOpaque;
    } else {
        bool unassociatedAlpha = _subImagesSpec[0].get_int_attribute("oiio:UnassociatedAlpha", 0);
        if (unassociatedAlpha) {
            *premult = OFX::eImageUnPreMultiplied;
        } else {
            *premult = OFX::eImagePreMultiplied;
        }
    }
    
}

void
ReadOIIOPlugin::openFile(const std::string& filename, bool useCache, ImageInput** img, std::vector<ImageSpec>* subimages)
{
    if (useCache) {
        getSpecsFromCache(filename, *subimages);
    } else {
        
        // Always keep unassociated alpha.
        // Don't let OIIO premultiply, because if the image is 8bits,
        // it multiplies in 8bits (see TIFFInput::unassalpha_to_assocalpha()),
        // which causes a lot of precision loss.
        // see also https://github.com/OpenImageIO/oiio/issues/960
        ImageSpec config;
        config.attribute("oiio:UnassociatedAlpha", 1);
        
        *img = ImageInput::open(filename, &config);
        if (!(*img)) {
            setPersistentMessage(OFX::Message::eMessageError, "", std::string("Cannot open file ") + filename);
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        getSpecsFromImageInput(*img, *subimages);
    }
}


void
ReadOIIOPlugin::getOIIOChannelIndexesFromLayerName(const std::string& filename,
                                                   int view,
                                                   const std::string& layerName,
                                                   OFX::PixelComponentEnum pixelComponents,
                                                   const std::vector<ImageSpec>& subimages,
                                                   std::vector<int>& channels, int& numChannels, int& subImageIndex)
{
    ViewsLayersMap layersMap;
    layersMapFromSubImages(subimages, &layersMap, 0);
    
    ///Find the view
    std::string viewName = getViewName(view);
    ViewsLayersMap::iterator foundView = layersMap.end();
    for (ViewsLayersMap::iterator it = layersMap.begin(); it!=layersMap.end(); ++it) {
        if (caseInsensitiveCompare(it->first, viewName)) {
            foundView = it;
            break;
        }
    }
    if (foundView == layersMap.end()) {
        /*
         We did not find the view by name. To offer some sort of compatibility and not fail, just load the view corresponding to the given
         index, even though the names do not match. 
         If the index is out of range, just load the main view (index 0)
         */
        
        foundView = layersMap.begin();
        if (view >= 0 && view < (int)layersMap.size()) {
            std::advance(foundView, view);
        }
        
        
    }
    
    int foundLayer = -1;
    for (std::size_t i = 0; i < foundView->second.size(); ++i) {
        if (foundView->second[i].first == layerName) {
            foundLayer = (int)i;
            break;
        }
    }
    if (foundLayer == -1) {
        std::stringstream ss;
        ss << "Could not find layer " << layerName << " in view " << viewName << " in " << filename;
        setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    const std::vector<int>& layerChannels = foundView->second[foundLayer].second.channelIndexes;
    subImageIndex = foundView->second[foundLayer].second.subImageIdx;
    
    // Some pngs are 2-channel intensity + alpha
    bool isIA = layerChannels.size() == 2 && foundView->second[foundLayer].second.channelNames[0] == "I" && foundView->second[foundLayer].second.channelNames[1] == "A";
                 
    switch (pixelComponents) {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            channels.resize(numChannels);
            if (isIA) {
                channels[0] = layerChannels[0] + kXChannelFirst;
                channels[1] = layerChannels[0] + kXChannelFirst;
                channels[2] = layerChannels[0] + kXChannelFirst;
                channels[3] = layerChannels[1] + kXChannelFirst;
            } else {
                if (layerChannels.size() == 1) {
                    channels[0] = layerChannels[0] + kXChannelFirst;
                    channels[1] = layerChannels[0] + kXChannelFirst;
                    channels[2] = layerChannels[0] + kXChannelFirst;
                    channels[3] = layerChannels[0] + kXChannelFirst;
                } else if (layerChannels.size() == 2) {
                    channels[0] = layerChannels[0] + kXChannelFirst;
                    channels[1] = layerChannels[1] + kXChannelFirst;
                    channels[2] = 0;
                    channels[3] = 1;
                } else if (layerChannels.size() == 3) {
                    channels[0] = layerChannels[0] + kXChannelFirst;
                    channels[1] = layerChannels[1] + kXChannelFirst;
                    channels[2] = layerChannels[2] + kXChannelFirst;
                    channels[3] = 1;
                } else {
                    channels[0] = layerChannels[0] + kXChannelFirst;
                    channels[1] = layerChannels[1] + kXChannelFirst;
                    channels[2] = layerChannels[2] + kXChannelFirst;
                    channels[3] = layerChannels[3] + kXChannelFirst;
                }
            }
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            channels.resize(numChannels);
            if (layerChannels.size() == 1) {
                channels[0] = layerChannels[0] + kXChannelFirst;
                channels[1] = layerChannels[0] + kXChannelFirst;
                channels[2] = layerChannels[0] + kXChannelFirst;
            } else if (layerChannels.size() == 2) {
                channels[0] = layerChannels[0] + kXChannelFirst;
                channels[1] = layerChannels[1] + kXChannelFirst;
                channels[2] = 0;
            } else if (layerChannels.size() >= 3) {
                channels[0] = layerChannels[0] + kXChannelFirst;
                channels[1] = layerChannels[1] + kXChannelFirst;
                channels[2] = layerChannels[2] + kXChannelFirst;
            }
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            channels.resize(numChannels);
            if (layerChannels.size() == 1) {
                channels[0] = layerChannels[0] + kXChannelFirst;
            } else if (layerChannels.size() == 2) {
                if (isIA) {
                    channels[0] = layerChannels[1] + kXChannelFirst;
                } else {
                    channels[0] = layerChannels[0] + kXChannelFirst;
                }
            } else if (layerChannels.size() == 3) {
                channels[0] = 1;
            } else if (layerChannels.size() == 4) {
                channels[0] = layerChannels[3] + kXChannelFirst;
            }
            break;
        case OFX::ePixelComponentCustom:
            //numChannels has been already set
            assert(numChannels != 0);
            channels.resize(numChannels);
            for (int i = 0; i < numChannels; ++i) {
                int defIndex = i == 3 ? 1 : 0;
                channels[i] = i < (int)layerChannels.size() ? layerChannels[i] + kXChannelFirst : defIndex;
            }
            break;
        default:
            assert(false);
            break;
    }
    
    
}

void ReadOIIOPlugin::decodePlane(const std::string& filename, OfxTime time, int view, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds, OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, const std::string& rawComponents, int rowBytes)
{
#if defined(OFX_READ_OIIO_USES_CACHE) && OIIO_VERSION >= 10605
    //Do not use cache in OIIO 1.5.x because it does not support channel ranges correctly
    bool useCache = !isPlayback;
#else
    bool useCache = false;
#endif
    
    
    //assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == spec.full_width && renderWindow.y1 == 0 && renderWindow.y2 == spec.full_height));
    //assert((renderWindow.x2 - renderWindow.x1) <= spec.width && (renderWindow.y2 - renderWindow.y1) <= spec.height);
    assert(bounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= bounds.x2);
    assert(bounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= bounds.y2);
    
    // we only support RGBA, RGB or Alpha output clip on the color plane
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha
        && pixelComponents != OFX::ePixelComponentCustom) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OIIO: can only read RGBA, RGB, Alpha or custom components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    
    std::vector<int> channels;
    int numChannels = 0;
    int pixelBytes = 0;
    std::auto_ptr<ImageInput> img;
    std::vector<ImageSpec> subimages;
    
    ImageInput* rawImg = 0;
    openFile(filename, useCache, &rawImg, &subimages);
    if (rawImg) {
        img.reset(rawImg);
    }
    
    if (subimages.empty()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("Cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    int subImageIndex = 0;
    if (pixelComponents != OFX::ePixelComponentCustom) {
        assert(rawComponents == kOfxImageComponentAlpha || rawComponents == kOfxImageComponentRGB || rawComponents == kOfxImageComponentRGBA);
        
        if (_useRGBAChoices) {
            
            
            int rChannel, gChannel, bChannel, aChannel;
            _rChannel->getValueAtTime(time, rChannel);
            _gChannel->getValueAtTime(time, gChannel);
            _bChannel->getValueAtTime(time, bChannel);
            _aChannel->getValueAtTime(time, aChannel);
            // test if channels are valid
            if (rChannel > subimages[0].nchannels + kXChannelFirst) {
                rChannel = 0;
            }
            if (gChannel > subimages[0].nchannels + kXChannelFirst) {
                gChannel = 0;
            }
            if (bChannel > subimages[0].nchannels + kXChannelFirst) {
                bChannel = 0;
            }
            if (aChannel > subimages[0].nchannels + kXChannelFirst) {
                aChannel = 1; // opaque by default
            }
            
            pixelBytes = pixelComponentCount * getComponentBytes(OFX::eBitDepthFloat);
            
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
        } else { // !_useRGBAChoices
            
            if (!_outputLayer) { // host is not multilayer nor anything, just use basic indexes
                pixelBytes = pixelComponentCount * getComponentBytes(OFX::eBitDepthFloat);
                
                switch (pixelComponents) {
                    case OFX::ePixelComponentRGBA:
                        numChannels = 4;
                        channels.resize(numChannels);
                        channels[0] = 0;
                        channels[1] = 1;
                        channels[2] = 2;
                        channels[3] = 3;
                        break;
                    case OFX::ePixelComponentRGB:
                        numChannels = 3;
                        channels.resize(numChannels);
                        channels[0] = 0;
                        channels[1] = 1;
                        channels[2] = 2;
                        break;
                    case OFX::ePixelComponentAlpha:
                        numChannels = 1;
                        channels.resize(numChannels);
                        channels[0] = 0;
                        break;
                    default:
                        assert(false);
                        break;
                }
            } else {
                int layer_i;
                _outputLayer->getValue(layer_i);
                
                OFX::MultiThread::AutoMutex lock(_layersMapMutex);
                if (layer_i < (int)_layersUnion.size() && layer_i >= 0) {
                    const std::string& layerName = _layersUnion[layer_i].first;
                    getOIIOChannelIndexesFromLayerName(filename, view, layerName, pixelComponents, subimages, channels, numChannels, subImageIndex);
                    
                } else {
                    setPersistentMessage(OFX::Message::eMessageError, "", "Failure to find requested layer in file");
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                    return;
                }
            }
        }
    } // if (pixelComponents != OFX::ePixelComponentCustom) {
#ifdef OFX_EXTENSIONS_NATRON
    else {
        std::vector<std::string> layerChannels = OFX::mapPixelComponentCustomToLayerChannels(rawComponents);
        if (!layerChannels.empty()) {
            numChannels = (int)layerChannels.size() - 1;
            channels.resize(numChannels);
            std::string layer = layerChannels[0];
            
            pixelBytes = numChannels * sizeof(float);
            
            if (!_useRGBAChoices && _outputLayer) {
                getOIIOChannelIndexesFromLayerName(filename, view, layer, pixelComponents, subimages, channels, numChannels, subImageIndex);
            } else {
                
                if (numChannels == 1 && layerChannels[1] == layer) {
                    layer.clear();
                }
                
                
                for (int i = 0; i < numChannels; ++i) {
                    bool found = false;
                    for (std::size_t j = 0; j < subimages[0].channelnames.size(); ++j) {
                        std::string realChan;
                        if (!layer.empty()) {
                            realChan.append(layer);
                            realChan.push_back('.');
                        }
                        realChan.append(layerChannels[i+1]);
                        if (subimages[0].channelnames[j] == realChan) {
                            channels[i] = j + kXChannelFirst;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        setPersistentMessage(OFX::Message::eMessageError, "", "Could not find channel named " + layerChannels[i+1]);
                        OFX::throwSuiteStatusException(kOfxStatFailed);
                        return;
                    }
                }
            }
        }
    }
#endif
    
    
    if (img.get() && !img->seek_subimage(subImageIndex, 0, subimages[0])) {
        std::stringstream ss;
        ss << "Cannot seek subimage " << subImageIndex << " in " << filename;
        setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
        
    }
    
    
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;
    
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    bool useDisplayWindowOrigin;
    _useDisplayWindowAsOrigin->getValue(useDisplayWindowOrigin);
#else
    const bool useDisplayWindowOrigin = true;
#endif
    
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

#         ifdef OFX_READ_OIIO_USES_CACHE
            if (useCache) {
                
                int fullHeight = subimages[subImageIndex].full_height == 0 ? subimages[subImageIndex].height : subimages[subImageIndex].full_height;
                
                if (!_cache->get_pixels(ustring(filename),
                                        subImageIndex, //subimage
                                        0, //miplevel
                                        useDisplayWindowOrigin ? subimages[subImageIndex].full_x + renderWindow.x1 : renderWindow.x1, //x begin
                                        useDisplayWindowOrigin ? subimages[subImageIndex].full_x + renderWindow.x2 : renderWindow.x2, //x end
                                        useDisplayWindowOrigin ? subimages[subImageIndex].full_y + fullHeight - renderWindow.y2 : renderWindow.y2, //y begin
                                        useDisplayWindowOrigin ? subimages[subImageIndex].full_y + fullHeight - renderWindow.y1 : renderWindow.y1, //y end
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
#                                     if OIIO_VERSION >= 10605
                                        ,
                                        chbegin, // only cache these channels
                                        chend
#                                     endif
                                        )) {
                    setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                    return;
                }
            } else // warning: '{' must follow #endif
#         endif
            { // !useCache
                assert(kSupportsTiles || (!kSupportsTiles && (renderWindow.x2 - renderWindow.x1) == subimages[subImageIndex].width && (renderWindow.y2 - renderWindow.y1) == subimages[subImageIndex].height));
                
                if (subimages[subImageIndex].tile_width == 0 ||
                    !subimages[subImageIndex].valid_tile_range(renderWindow.x1, renderWindow.x2, subimages[subImageIndex].height - renderWindow.y2, subimages[subImageIndex].height - renderWindow.y1, 0, 1)) {
                    ///read by scanlines
                    if (!img->read_scanlines(subimages[subImageIndex].height - renderWindow.y2, //y begin
                                             subimages[subImageIndex].height - renderWindow.y1, //y end
                                             0, // z
                                             chbegin, // chan begin
                                             chend, // chan end
                                             TypeDesc::FLOAT, // data type
                                             (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                             numChannels * sizeof(float), //x stride
                                             -rowBytes)) //y stride < make it invert Y;
                    {
                        setPersistentMessage(OFX::Message::eMessageError, "", img->geterror());
                        OFX::throwSuiteStatusException(kOfxStatFailed);
                        return;
                    }
                } else {
                    /*
                     This call will be expensive for multi-layred EXRs because the function internally
                     calls ImageInput::read_tile without chbegin,chend
                     */
                    if (!img->read_tiles(renderWindow.x1, //x begin
                                         renderWindow.x2,//x end
                                         subimages[subImageIndex].height - renderWindow.y2,//y begin
                                         subimages[subImageIndex].height - renderWindow.y1,//y end
                                         0, // z begin
                                         1, // z end
                                         chbegin, // chan begin
                                         chend, // chan end
                                         TypeDesc::FLOAT,  // data type
                                         (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                         numChannels * sizeof(float), //x stride
                                         -rowBytes, //y stride < make it invert Y
                                         AutoStride)) //z stride
                    {
                        setPersistentMessage(OFX::Message::eMessageError, "", img->geterror());
                        OFX::throwSuiteStatusException(kOfxStatFailed);
                        return;
                    }
                }
            } // !useCache
        } // if (channels[i] < kXChannelFirst) {
        
    } // for (std::size_t i = 0; i < channels.size(); i+=incr) {
    
    if (!useCache) {
        img->close();
    }
}

bool
ReadOIIOPlugin::getFrameBounds(const std::string& filename,
                               OfxTime /*time*/,
                               OfxRectI *bounds,
                               double *par,
                               std::string *error,
                               int* tile_width,
                               int* tile_height)
{
    assert(bounds && par);
    std::vector<ImageSpec> specs;
# ifdef OFX_READ_OIIO_USES_CACHE
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    {
        ImageSpec spec;
        int subImageIndex = 0;
        while (_cache->get_imagespec(ustring(filename), spec, subImageIndex)) {
            specs.push_back(spec);
            ++subImageIndex;
#ifndef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
            break;
#endif
        }
    }
    if (specs.empty()) {
        if (error) {
            *error = _cache->geterror();
        }
        return false;
    }
# else
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        if (error) {
            *error = std::string("ReadOIIO: cannot open file ") + filename;
        }
        return;
    }
    {
        int subImageIndex = 0;
        ImageSpec spec;
        while (img->seek_subimage(subImageIndex, 0, spec)) {
            specs.push_back(spec);
            ++subImageIndex;
#ifndef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
            break;
#endif
        }
    }
    if (specs.empty()) {
        return;
    }
# endif
    
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    bool originAtDisplayWindow;
    _useDisplayWindowAsOrigin->getValue(originAtDisplayWindow);
#endif
    
    /*
     Union bounds across all specs
     */
    
    OfxRectD mergeBounds = {0., 0., 0., 0.}; // start with empty bounds - rectBoundingBox grows them
    for (std::size_t i = 0; i < specs.size(); ++i) {
        OfxRectD specBounds;
        
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
        if (originAtDisplayWindow)
#endif
        {
            // the image coordinates are expressed in the "full/display" image.
            // The RoD are the coordinates of the data window with respect to that full window
            
            specBounds.x1 = (specs[i].x - specs[i].full_x);
            specBounds.x2 = (specs[i].x + specs[i].width - specs[i].full_x);
            
            int fullHeight = specs[i].full_height == 0 ? specs[i].height : specs[i].full_height;
            
            specBounds.y1 = specs[i].full_y + fullHeight - (specs[i].y + specs[i].height);
            specBounds.y2 = fullHeight + (specs[i].full_y - specs[i].y);
        }
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
        else {
            specBounds.x1 = specs[i].x;
            specBounds.x2 = specs[i].x + specs[i].width;
            specBounds.y1 =  specs[i].y;
            specBounds.y2 =  specs[i].y + specs[i].height;
        }
#endif

        OFX::Coords::rectBoundingBox(specBounds, mergeBounds, &mergeBounds);
    }
    
    bounds->x1 = mergeBounds.x1;
    bounds->x2 = mergeBounds.x2;
    bounds->y1 = mergeBounds.y1;
    bounds->y2 = mergeBounds.y2;
    *tile_width = specs[0].tile_width;
    *tile_height = specs[0].tile_height;
    
    *par = specs[0].get_float_attribute("PixelAspectRatio", 1);
#ifdef OFX_READ_OIIO_USES_CACHE
#else
    img->close();
#endif
    return true;
}

std::string
ReadOIIOPlugin::metadata(const std::string& filename)
{
    std::stringstream ss;


#ifndef OFX_READ_OIIO_USES_CACHE
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return std::string();
    }
#endif
    std::vector<ImageSpec> subImages;
    {
        int subImageIndex = 0;
        ImageSpec spec;
#ifdef OFX_READ_OIIO_USES_CACHE
        while (_cache->get_imagespec(ustring(filename), spec,subImageIndex))
#else
        while (img->seek_subimage(subImageIndex, 0, spec))
#endif
        {
            subImages.push_back(spec);
            ++subImageIndex;
        }
    }
    if (subImages.empty()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("No information found in") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return std::string();
    }
    
    ss << "file: " << filename << std::endl;

    for (std::size_t sIt = 0; sIt < subImages.size(); ++sIt) {
        
        if (subImages.size() > 1) {
            ss << "Part " << sIt << ":" << std::endl;
        }
        
        ss << "Channels list: " << std::endl;
        for (int i = 0;  i < subImages[sIt].nchannels;  ++i) {
            if (i < (int)subImages[sIt].channelnames.size()) {
                ss << subImages[sIt].channelnames[i];
                if (i == subImages[sIt].alpha_channel) {
                    ss << " - alpha channel";
                }
            } else {
                ss << "unknown";
            }
            if (i < (int)subImages[sIt].channelformats.size()) {
                ss << " (" << subImages[sIt].channelformats[i].c_str() << ")";
            }
            if (i < subImages[sIt].nchannels-1) {
                if (subImages[sIt].nchannels <= 4) {
                    ss << ", ";
                } else {
                    ss << std::endl;
                }
            }
        }
        ss << std::endl;
        
        if (subImages[sIt].x || subImages[sIt].y || subImages[sIt].z) {
            ss << "    pixel data origin: x=" << subImages[sIt].x << ", y=" << subImages[sIt].y;
            if (subImages[sIt].depth > 1) {
                ss << ", z=" << subImages[sIt].z;
            }
            ss << std::endl;
        }
        if (subImages[sIt].full_x || subImages[sIt].full_y || subImages[sIt].full_z ||
            (subImages[sIt].full_width != subImages[sIt].width && subImages[sIt].full_width != 0) ||
            (subImages[sIt].full_height != subImages[sIt].height && subImages[sIt].full_height != 0) ||
            (subImages[sIt].full_depth != subImages[sIt].depth && subImages[sIt].full_depth != 0)) {
            ss << "    full/display size: " << subImages[sIt].full_width << " x " << subImages[sIt].full_height;
            if (subImages[sIt].depth > 1) {
                ss << " x " << subImages[sIt].full_depth;
            }
            ss << std::endl;
            ss << "    full/display origin: " << subImages[sIt].full_x << ", " << subImages[sIt].full_y;
            if (subImages[sIt].depth > 1) {
                ss << ", " << subImages[sIt].full_z;
            }
            ss << std::endl;
        }
        if (subImages[sIt].tile_width) {
            ss << "    tile size: " << subImages[sIt].tile_width << " x " << subImages[sIt].tile_height;
            if (subImages[sIt].depth > 1) {
                ss << " x " << subImages[sIt].tile_depth;
            }
            ss << std::endl;
        }
        
        for (ImageIOParameterList::const_iterator p = subImages[sIt].extra_attribs.begin(); p != subImages[sIt].extra_attribs.end(); ++p) {
            std::string s = subImages[sIt].metadata_val (*p, true);
            ss << "    " << p->name() << ": ";
            if (s == "1.#INF") {
                ss << "inf";
            } else {
                ss << s;
            }
            ss << std::endl;
        }
        
        if (subImages.size() > 1 && sIt < subImages.size() -1) {
            ss << std::endl;
        }
    }
#ifdef OFX_READ_OIIO_USES_CACHE
#else
    img->close();
#endif

    return ss.str();
}


template<bool useRGBAChoices>
class ReadOIIOPluginFactory : public OFX::PluginFactoryHelper<ReadOIIOPluginFactory<useRGBAChoices> >
{
public:
    ReadOIIOPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<ReadOIIOPluginFactory>(id, verMaj, verMin) {}
    virtual void describe(OFX::ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context) OVERRIDE FINAL;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context) OVERRIDE FINAL;
    virtual void load() OVERRIDE FINAL;
    virtual void unload() OVERRIDE FINAL;
    bool isVideoStreamPlugin() const { return false; }
    std::vector<std::string> _extensions;
};

template <bool useRGBAChoices>
void
ReadOIIOPluginFactory<useRGBAChoices>::load()
{
    _extensions.clear();
#if 0
    // hard-coded extensions list
    const char* extensionsl[] = { "bmp", "cin", "dds", "dpx", "f3d", "fits", "hdr", "ico",
        "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png",
        "pbm", "pgm", "ppm",
#     if OIIO_VERSION >= 10605
        "pfm", // PFM was flipped before 1.6.5
#     endif
        "psd", "pdd", "psb", "ptex", "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
    for (const char** ext = extensionsl; *ext != NULL; ++ext) {
        _extensions.push_back(*ext);
    }
#else
    // get extensions from OIIO (but there is no distinctions between readers and writers)
    std::string extensions_list;
    getattribute("extension_list", extensions_list);
    std::stringstream formatss(extensions_list);
    std::string format;
    std::list<std::string> extensionsl;
    while (std::getline(formatss, format, ';')) {
        std::stringstream extensionss(format);
        std::string extension;
        std::getline(extensionss, extension, ':'); // extract the format
        while (std::getline(extensionss, extension, ',')) {
            extensionsl.push_back(extension);
        }
    }
    const char* extensions_blacklist[] = {
#     if OIIO_VERSION < 10605
        "pfm", // PFM was flipped before 1.6.5
#     endif
        "avi", "mov", "qt", "mp4", "m4a", "3gp", "3g2", "mj2", "m4v", "mpg", // FFmpeg extensions - better supported by ReadFFmpeg
        NULL
    };
    for (const char*const* e = extensions_blacklist; *e != NULL; ++e) {
        extensionsl.remove(*e);
    }
    _extensions.assign(extensionsl.begin(), extensionsl.end());
#endif
}

template <bool useRGBAChoices>
void
ReadOIIOPluginFactory<useRGBAChoices>::unload()
{
#  ifdef OFX_READ_OIIO_SHARED_CACHE
    // get the shared image cache (may be shared with other plugins using OIIO)
    ImageCache* sharedcache = ImageCache::create(true);
    // purge it
    // teardown is dangerous if there are other users
    ImageCache::destroy(sharedcache);
#  endif
    
#ifdef _WIN32
	//Kill all threads otherwise when the static global thread pool joins it threads there is a deadlock on Mingw
    IlmThread::ThreadPool::globalThreadPool().setNumThreads(0);
#endif
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
template <bool useRGBAChoices>
void
ReadOIIOPluginFactory<useRGBAChoices>::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, _extensions, kPluginEvaluation, kSupportsTiles, kIsMultiPlanar);

    if (useRGBAChoices) {
        //Keep the old plug-in with choice menus but set it deprecated so the user cannot create it anymore
        desc.setIsDeprecated(true);
    }
    std::string extensions_list;
    getattribute("extension_list", extensions_list);

    std::string extensions_pretty;
    {
        std::stringstream formatss(extensions_list);
        std::string format;
        std::vector<std::string> extensions;
        while (std::getline(formatss, format, ';')) {
            std::stringstream extensionss(format);
            std::string extension;
            std::getline(extensionss, extension, ':'); // extract the format
            extensions_pretty += extension;
            extensions_pretty += ": ";
            bool first = true;
            while (std::getline(extensionss, extension, ',')) {
                if (!first) {
                    extensions_pretty += ", ";
                }
                first = false;
                extensions_pretty += extension;
            }
            extensions_pretty += "; ";
        }
    }

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription
                              "\n\n"
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
                              "All supported formats and extensions: " + extensions_pretty + "\n\n"
                              + oiio_versions());
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
template <bool useRGBAChoices>
void ReadOIIOPluginFactory<useRGBAChoices>::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostSupportsDynamicChoices = (OFX::getImageEffectHostDescription()->supportsDynamicChoices);
    gHostSupportsMultiPlane = (OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true)) != 0;
    
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles, false);

    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamShowMetadata);
        param->setLabel(kParamShowMetadataLabel);
        param->setHint(kParamShowMetadataHint);
        if (page) {
            page->addChild(*param);
        }
    }


    if (useRGBAChoices) {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRChannel);
            param->setLabel(kParamRChannelLabel);
            param->setHint(kParamRChannelHint);
            appendDefaultChannelList(param);
            param->setAnimates(true);
            param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamGChannel);
            param->setLabel(kParamGChannelLabel);
            param->setHint(kParamGChannelHint);
            appendDefaultChannelList(param);
            param->setAnimates(true);
            param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBChannel);
            param->setLabel(kParamBChannelLabel);
            param->setHint(kParamBChannelHint);
            appendDefaultChannelList(param);
            param->setAnimates(true);
            param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamAChannel);
            param->setLabel(kParamAChannelLabel);
            param->setHint(kParamAChannelHint);
            appendDefaultChannelList(param);
            param->setAnimates(true);
            param->setDefault(1); // opaque by default
            param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
            if (page) {
                page->addChild(*param);
            }
        }
        
        {
            StringParamDescriptor* param = desc.defineStringParam(kParamRChannelName);
            param->setLabel(kParamRChannelLabel);
            param->setHint(kParamRChannelHint);
            param->setAnimates(false);
            param->setIsSecret(true); // never meant to be visible
            if (page) {
                page->addChild(*param);
            }
        }
        {
            StringParamDescriptor* param = desc.defineStringParam(kParamGChannelName);
            param->setLabel(kParamGChannelLabel);
            param->setHint(kParamGChannelHint);
            param->setAnimates(false);
            param->setIsSecret(true); // never meant to be visible
            if (page) {
                page->addChild(*param);
            }
        }
        
        {
            StringParamDescriptor* param = desc.defineStringParam(kParamBChannelName);
            param->setLabel(kParamBChannelLabel);
            param->setHint(kParamBChannelHint);
            param->setAnimates(false);
            param->setIsSecret(true); // never meant to be visible
            if (page) {
                page->addChild(*param);
            }
        }
        
        {
            StringParamDescriptor* param = desc.defineStringParam(kParamAChannelName);
            param->setLabel(kParamAChannelLabel);
            param->setHint(kParamAChannelHint);
            param->setAnimates(false);
            param->setIsSecret(true); // never meant to be visible
            if (page) {
                page->addChild(*param);
            }
        }
    } else { // if (useRGBAChoices) {
        
        if (gHostSupportsMultiPlane && gHostSupportsDynamicChoices) {
            {
                ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamChannelOutputLayer);
                param->setLabel(kParamChannelOutputLayerLabel);
                param->setHint(kParamChannelOutputLayerHint);
                param->setEvaluateOnChange(false);
                param->setIsPersistant(false);
                param->setAnimates(false);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                StringParamDescriptor* param = desc.defineStringParam(kParamChannelOutputLayerChoice);
                param->setLabel(kParamChannelOutputLayerChoice);
                param->setIsSecret(true);
                param->setAnimates(false);
                desc.addClipPreferencesSlaveParam(*param);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                StringParamDescriptor* param = desc.defineStringParam(kParamAvailableViews);
                param->setLabel(kParamAvailableViewsLabel);
                param->setHint(kParamAvailableViewsHint);
                param->setAnimates(false);
                param->setIsSecret(true);
                param->setEvaluateOnChange(false);
                param->setIsPersistant(false);
                param->setLayoutHint(OFX::eLayoutHintDivider);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseDisplayWindowAsOrigin);
        param->setLabel(kParamUseDisplayWindowAsOriginLabel);
        param->setHint(kParamUseDisplayWindowAsOriginHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
template <bool useRGBAChoices>
ImageEffect* ReadOIIOPluginFactory<useRGBAChoices>::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    ReadOIIOPlugin* ret =  new ReadOIIOPlugin(useRGBAChoices, handle, _extensions);
    ret->restoreStateFromParameters();
    return ret;
}


static ReadOIIOPluginFactory<false> p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static ReadOIIOPluginFactory<true> p2(kPluginIdentifier, 1, 0);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
