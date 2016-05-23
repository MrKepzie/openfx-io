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
 * OFX GenericWriter plugin.
 * A base class for all OpenFX-based encoders.
 */

#include "GenericWriter.h"

#include <locale>
#include <sstream>
#include <cstring>
#include <algorithm>

#include "ofxsLog.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"

#include "ofxsMultiPlane.h"

#ifdef OFX_EXTENSIONS_TUTTLE
#include <tuttle/ofxReadWrite.h>
#endif
#include "../SupportExt/ofxsFormatResolution.h"
#include "GenericOCIO.h"

#define kPluginGrouping "Image/Writers"

#define kSupportsTiles 0
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 0 // Writers do not support render scale: all images must be rendered/written at full resolution

// in the Writer context, the script name must be kOfxImageEffectFileParamName, @see kOfxImageEffectContextWriter
#define kParamFilename kOfxImageEffectFileParamName
#define kParamFilenameLabel "File"
#define kParamFilenameHint \
"The output image sequence/video stream file(s). " \
"The string must match the following format: " \
"path/sequenceName###.ext where the number of " \
"# (hashes) will define the number of digits to append to each " \
"file. For example path/mySequence###.jpg will be translated to " \
"path/mySequence000.jpg, path/mySequence001.jpg, etc. " \
"%d printf-like notation can also be used instead of the hashes, for example path/sequenceName%03d.ext will achieve the same than the example aforementionned. " \
"there will be at least 2 digits). The file name may not contain any # (hash) in which case it  will be overriden everytimes. " \
"Views can be specified using the \"long\" view notation %V or the \"short\" notation using %v."

#define kParamOutputFormat kNatronParamFormatChoice
#define kParamOutputFormatLabel "Format"
#define kParamOutputFormatHint \
"The output format to render"

#define kParamFormatType "formatType"
#define kParamFormatTypeLabel "Format Type"
#define kParamFormatTypeHint \
"Whether to choose the input stream's format as output format or one from the drop-down menu"

#define kParamFormatSize kNatronParamFormatSize

#define kParamFormatPar kNatronParamFormatPar

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint \
"What frame range should be rendered."
#define kParamFrameRangeOptionUnion "Union of input ranges"
#define kParamFrameRangeOptionUnionHint "The union of all inputs frame ranges will be rendered."
#define kParamFrameRangeOptionBounds "Project frame range"
#define kParamFrameRangeOptionBoundsHint "The frame range delimited by the frame range of the project will be rendered."
#define kParamFrameRangeOptionManual "Manual"
#define kParamFrameRangeOptionManualHint "The frame range will be the one defined by the first frame and last frame parameters."

#define kParamFirstFrame "firstFrame"
#define kParamFirstFrameLabel "First Frame"

#define kParamLastFrame "lastFrame"
#define kParamLastFrameLabel "Last Frame"

#define kParamInputPremult "inputPremult"
#define kParamInputPremultLabel "Input Premult"
#define kParamInputPremultHint \
"Input is considered to have this premultiplication state.\n"\
"If it is Premultiplied, red, green and blue channels are divided by the alpha channel "\
"before applying the colorspace conversion.\n"\
"This is set automatically from the input stream information, but can be adjusted if this information is wrong."
#define kParamInputPremultOptionOpaqueHint "The image is opaque and so has no premultiplication state, as if the alpha component in all pixels were set to the white point."
#define kParamInputPremultOptionPreMultipliedHint "The image is premultiplied by its alpha (also called \"associated alpha\")."
#define kParamInputPremultOptionUnPreMultipliedHint "The image is unpremultiplied (also called \"unassociated alpha\")."

#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

#define kParamOutputSpaceLabel "File Colorspace"

#define kParamClipToProject "clipToProject"
#define kParamClipToProjectLabel "Clip To Project"
#define kParamClipToProjectHint "When checked, the portion of the image written will be the size of the image in input and not the format of the project. " \
"For the EXR file format, this will distinguish the data window (size of the image in input) from the display window (size of the project)."

#define kParamProcessHint  "When checked, this channel of the layer will be written to the file otherwise it will be skipped. Most file formats will " \
"pack the channels into the first N channels of the file. If for some reason it's not possible, the channel will be filled with 0."

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output Components"
#define kParamOutputComponentsHint "Map the input layer to this type of components before writing it to the output file."

static bool gHostIsNatron   = false;
static bool gHostIsMultiPlanar = false;
static bool gHostIsMultiView = false;

static std::vector<OFX::PixelComponentEnum> gPluginOutputComponents;


GenericWriterPlugin::GenericWriterPlugin(OfxImageEffectHandle handle,
                                         const std::vector<std::string>& extensions,
                                         bool supportsRGBA, bool supportsRGB, bool supportsAlpha, bool supportsXY)
: OFX::MultiPlane::MultiPlaneEffect(handle)
, _inputClip(0)
, _outputClip(0)
, _fileParam(0)
, _frameRange(0)
, _firstFrame(0)
, _lastFrame(0)
, _outputFormatType(0)
, _outputFormat(0)
, _outputFormatSize(0)
, _outputFormatPar(0)
, _premult(0)
, _clipToProject(0)
, _sublabel(0)
, _processChannels()
, _outputComponents(0)
, _ocio(new GenericOCIO(this))
, _extensions(extensions)
, _supportsAlpha(supportsAlpha)
, _supportsXY(supportsXY)
, _supportsRGB(supportsRGB)
, _supportsRGBA(supportsRGBA)
{
    _inputClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kParamFilename);
    _frameRange = fetchChoiceParam(kParamFrameRange);
    _firstFrame = fetchIntParam(kParamFirstFrame);
    _lastFrame = fetchIntParam(kParamLastFrame);
    
    _outputFormatType = fetchChoiceParam(kParamFormatType);
    _outputFormat = fetchChoiceParam(kParamOutputFormat);
    _outputFormatSize = fetchInt2DParam(kParamFormatSize);
    _outputFormatPar = fetchDoubleParam(kParamFormatPar);
    
    _premult = fetchChoiceParam(kParamInputPremult);


    ///Param does not necessarily exist for all IO plugins
    if (paramExists(kParamClipToProject)) {
        _clipToProject = fetchBooleanParam(kParamClipToProject);
    }

    if (gHostIsNatron) {
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_sublabel);
    }

    _processChannels[0] = fetchBooleanParam(kNatronOfxParamProcessR);
    _processChannels[1] = fetchBooleanParam(kNatronOfxParamProcessG);
    _processChannels[2] = fetchBooleanParam(kNatronOfxParamProcessB);
    _processChannels[3] = fetchBooleanParam(kNatronOfxParamProcessA);
    _outputComponents = fetchChoiceParam(kParamOutputComponents);
    assert(_processChannels[0] && _processChannels[1] && _processChannels[2] && _processChannels[3] && _outputComponents);
    
    int frameRangeChoice;
    _frameRange->getValue(frameRangeChoice);
    double first,last;
    timeLineGetBounds(first,last);
    if (frameRangeChoice == 2) {
        _firstFrame->setIsSecret(false);
        _lastFrame->setIsSecret(false);
    } else {
        _firstFrame->setIsSecret(true);
        _lastFrame->setIsSecret(true);
    }
    int outputFormat_i;
    _outputFormatType->getValue(outputFormat_i);
    if (_clipToProject) {
        std::string filename;
        _fileParam->getValue(filename);
        _clipToProject->setIsSecret(outputFormat_i != 1 || !displayWindowSupportedByFormat(filename));
    }
    if (outputFormat_i == 0 || outputFormat_i == 1) {
        _outputFormat->setIsSecret(true);
    } else {
        _outputFormat->setIsSecret(false);
    }
}

GenericWriterPlugin::~GenericWriterPlugin()
{
}


bool
GenericWriterPlugin::checkExtension(const std::string& ext)
{
    if (ext.empty()) {
        // no extension
        return false;
    }
    return std::find(_extensions.begin(), _extensions.end(), ext) != _extensions.end();
}

bool
GenericWriterPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                OFX::Clip * &/*identityClip*/,
                                double &/*identityTime*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();
    return false;
}


    
GenericWriterPlugin::InputImagesHolder::InputImagesHolder()
{
    
}

void
GenericWriterPlugin::InputImagesHolder::addImage(const OFX::Image* img)
{
    _imgs.push_back(img);
}

void
GenericWriterPlugin::InputImagesHolder::addMemory(OFX::ImageMemory* mem)
{
    _mems.push_back(mem);
}

GenericWriterPlugin::InputImagesHolder::~InputImagesHolder()
{
    for (std::list<const OFX::Image*>::iterator it = _imgs.begin(); it != _imgs.end(); ++it) {
        delete *it;
    }
    for (std::list<OFX::ImageMemory*>::iterator it = _mems.begin(); it!= _mems.end(); ++it) {
        delete *it;
    }
}

static int getPixelsComponentsCount(const std::string& rawComponents,
                                    OFX::PixelComponentEnum* mappedComponents)
{
    
    std::string layer,pairedLayer;
    std::vector<std::string> channels;
    OFX::MultiPlane::Utils::extractChannelsFromComponentString(rawComponents, &layer, &pairedLayer, &channels);
    switch (channels.size()) {
        case 0:
            *mappedComponents = OFX::ePixelComponentNone;
            break;
        case 1:
            *mappedComponents = OFX::ePixelComponentAlpha;
            break;
        case 2:
            *mappedComponents = OFX::ePixelComponentXY;
            break;
        case 3:
            *mappedComponents = OFX::ePixelComponentRGB;
            break;
        case 4:
            *mappedComponents = OFX::ePixelComponentRGBA;
            break;
        default:
            assert(false);
            break;
    }
    return (int)channels.size();
}

void
GenericWriterPlugin::fetchPlaneConvertAndCopy(const std::string& plane,
                                              bool failIfNoSrcImg,
                                              int view,
                                              int renderRequestedView,
                                              double time,
                                              const OfxRectI& renderWindow,
                                              const OfxPointD& renderScale,
                                              OFX::FieldEnum fieldToRender,
                                              OFX::PreMultiplicationEnum pluginExpectedPremult,
                                              OFX::PreMultiplicationEnum userPremult,
                                              const bool isOCIOIdentity,
                                              const bool doAnyPacking,
                                              const bool packingContiguous,
                                              const std::vector<int>& packingMapping,
                                              InputImagesHolder* srcImgsHolder,
                                              OfxRectI* bounds,
                                              OFX::ImageMemory** tmpMem,
                                              const OFX::Image** inputImage,
                                              float** tmpMemPtr,
                                              int* rowBytes,
                                              OFX::PixelComponentEnum* mappedComponents,
                                              int* mappedComponentsCount)
{
    *inputImage = 0;
    *tmpMem = 0;
    *tmpMemPtr = 0;
    *mappedComponentsCount = 0;
    
    const void* srcPixelData = 0;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    
    const OFX::Image* srcImg = _inputClip->fetchImagePlane(time, view, plane.c_str());
    *inputImage = srcImg;
    if (!srcImg) {
        if (failIfNoSrcImg) {
            std::stringstream ss;
            ss << "Input layer ";
            std::string layerName,pairedLayer;
            std::vector<std::string> channels;
            OFX::MultiPlane::Utils::extractChannelsFromComponentString(plane, &layerName, &pairedLayer, &channels);
            ss << layerName;
            ss << " could not be fetched";
            
            setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        return;
    } else {
        ///Add it to the holder so we are sure it gets released if an exception occurs below
        srcImgsHolder->addImage(srcImg);
    }
    
    if (srcImg->getRenderScale().x != renderScale.x ||
        srcImg->getRenderScale().y != renderScale.y ||
        srcImg->getField() != fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    getImageData(srcImg, &srcPixelData, bounds, &pixelComponents, &bitDepth, &srcRowBytes);
    
    if (bitDepth != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    
    
    // premultiplication/unpremultiplication is only useful for RGBA data
    bool noPremult = (pixelComponents != OFX::ePixelComponentRGBA) || (userPremult == OFX::eImageOpaque);
    
    *mappedComponentsCount = getPixelsComponentsCount(srcImg->getPixelComponentsProperty(),mappedComponents);
    assert(*mappedComponentsCount != 0 && *mappedComponents != OFX::ePixelComponentNone);

    bool renderWindowIsBounds = renderWindow.x1 == bounds->x1 &&
    renderWindow.y1 == bounds->y1 &&
    renderWindow.x2 == bounds->x2 &&
    renderWindow.y2 == bounds->y2;
    
    
    if (renderWindowIsBounds &&
        isOCIOIdentity &&
        (noPremult || userPremult == pluginExpectedPremult)) {
        // Render window is of the same size as the input image and we don't need to apply colorspace conversion
        // or premultiplication operations.
        
        *tmpMemPtr = (float*)srcPixelData;
        *rowBytes = srcRowBytes;
        
        // copy to dstImg if necessary
        if (renderRequestedView == view && _outputClip && _outputClip->isConnected()) {
            std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImagePlane(time,renderRequestedView,plane.c_str()));
            if (!dstImg.get()) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Output image could not be fetched");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            if (dstImg->getRenderScale().x != renderScale.x ||
                dstImg->getRenderScale().y != renderScale.y ||
                dstImg->getField() != fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            
            // copy the source image (the writer is a no-op)
            copyPixelData(renderWindow, srcPixelData, renderWindow, pixelComponents, *mappedComponentsCount, bitDepth, srcRowBytes, dstImg.get());
            
        }
    } else {
        // generic case: some conversions are needed.
        
        // allocate
        int pixelBytes = *mappedComponentsCount * getComponentBytes(bitDepth);
        int tmpRowBytes = (renderWindow.x2 - renderWindow.x1) * pixelBytes;
        *rowBytes = tmpRowBytes;
        size_t memSize = (renderWindow.y2 - renderWindow.y1) * tmpRowBytes;
        *tmpMem = new OFX::ImageMemory(memSize,this);
        srcImgsHolder->addMemory(*tmpMem);
        *tmpMemPtr = (float*)(*tmpMem)->lock();
        if (!*tmpMemPtr) {
            OFX::throwSuiteStatusException(kOfxStatErrMemory);
            return;
        }
        
        float* tmpPixelData = *tmpMemPtr;
        
        // Set to black and transparant so that outside the portion defined by the image there's nothing.
        if (!renderWindowIsBounds) {
            std::memset(tmpPixelData, 0, memSize);
        }
        
        // Clip the render window to the bounds of the source image.
        OfxRectI renderWindowClipped;
        if (!intersect(renderWindow, *bounds, &renderWindowClipped)) {
            std::stringstream ss;
            ss << "Output format does not intersect the input image bounds: Render Window : (x1=" << renderWindow.x1 <<
            ",y1="<<renderWindow.y1 <<",x2="<<renderWindow.x2<<",y2="<<renderWindow.y2<< ") vs. Input bounds : (x1=" << bounds->x1 <<
            ",y1="<<bounds->y1<<",x2="<<bounds->x2<<",y2="<<bounds->y2<<")";
            setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        
        if (isOCIOIdentity) {
            // bypass OCIO
            
            if (noPremult || userPremult == pluginExpectedPremult) {
                if (userPremult == OFX::eImageOpaque && (*mappedComponents == OFX::ePixelComponentRGBA ||
                                                         *mappedComponents == OFX::ePixelComponentAlpha)) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                     srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount, bitDepth, srcRowBytes,
                                     tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount, bitDepth, srcRowBytes,
                               tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
                }
            } else if (userPremult == OFX::eImagePreMultiplied) {
                assert(pluginExpectedPremult == OFX::eImageUnPreMultiplied);
                unPremultPixelData(renderWindow, srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount
                                   , bitDepth, srcRowBytes, tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
            } else {
                assert(userPremult == OFX::eImageUnPreMultiplied);
                assert(pluginExpectedPremult == OFX::eImagePreMultiplied);
                premultPixelData(renderWindow, srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount
                                 , bitDepth, srcRowBytes, tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
            }
        } else {
            assert(!isOCIOIdentity);
            // OCIO expects unpremultiplied input
            if (noPremult || userPremult == OFX::eImageUnPreMultiplied) {
                if (userPremult == OFX::eImageOpaque && (*mappedComponents == OFX::ePixelComponentRGBA ||
                                                         *mappedComponents == OFX::ePixelComponentAlpha)) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                     srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount, bitDepth, srcRowBytes,
                                     tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount, bitDepth, srcRowBytes,
                               tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
                }
            } else {
                assert(userPremult == OFX::eImagePreMultiplied);
                unPremultPixelData(renderWindow, srcPixelData, *bounds, *mappedComponents, *mappedComponentsCount
                                   , bitDepth, srcRowBytes, tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
            }
            // do the color-space conversion
            if (*mappedComponents == OFX::ePixelComponentRGB || *mappedComponents == OFX::ePixelComponentRGBA) {
                _ocio->apply(time, renderWindowClipped, tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, tmpRowBytes);
            }
            
            ///If needed, re-premult the image for the plugin to work correctly
            if (pluginExpectedPremult == OFX::eImagePreMultiplied && *mappedComponents == OFX::ePixelComponentRGBA) {
                
                premultPixelData(renderWindow, tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount
                                 , bitDepth, tmpRowBytes, tmpPixelData, renderWindow, *mappedComponents, *mappedComponentsCount, bitDepth, tmpRowBytes);
            }
        } // if (isOCIOIdentity) {
 
        // copy to dstImg if necessary
        if (renderRequestedView == view && _outputClip && _outputClip->isConnected()) {
            std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImagePlane(time,renderRequestedView,plane.c_str()));
            if (!dstImg.get()) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Output image could not be fetched");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            if (dstImg->getRenderScale().x != renderScale.x ||
                dstImg->getRenderScale().y != renderScale.y ||
                dstImg->getField() != fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            
            // copy the source image (the writer is a no-op)
            copyPixelData(renderWindow, srcPixelData, *bounds, pixelComponents, *mappedComponentsCount, bitDepth, srcRowBytes, dstImg.get());
            
        }
        *bounds = renderWindow;

    } // if (renderWindowIsBounds && isOCIOIdentity && (noPremult || userPremult == pluginExpectedPremult))

    
    if (doAnyPacking && (!packingContiguous || (int)packingMapping.size() != *mappedComponentsCount)) {
        int pixelBytes = packingMapping.size() * getComponentBytes(bitDepth);
        int tmpRowBytes = (renderWindow.x2 - renderWindow.x1) * pixelBytes;
        size_t memSize = (renderWindow.y2 - renderWindow.y1) * tmpRowBytes;
        OFX::ImageMemory *packingBufferMem = new OFX::ImageMemory(memSize,this);
        srcImgsHolder->addMemory(packingBufferMem);
        float* packingBufferData = (float*)packingBufferMem->lock();
        if (!packingBufferData) {
            OFX::throwSuiteStatusException(kOfxStatErrMemory);
            return;
        }
        
        packPixelBuffer(renderWindow, *tmpMemPtr, *bounds, bitDepth, *rowBytes, *mappedComponents, packingMapping, tmpRowBytes, packingBufferData);
        
        *tmpMemPtr = packingBufferData;
        *rowBytes = tmpRowBytes;
        *bounds = renderWindow;
        *mappedComponentsCount = packingMapping.size();
        switch (packingMapping.size()) {
            case 1:
                *mappedComponents = OFX::ePixelComponentAlpha;
                break;
            case 2:
                *mappedComponents = OFX::ePixelComponentXY;
                break;
            case 3:
                *mappedComponents = OFX::ePixelComponentRGB;
                break;
            case 4:
                *mappedComponents = OFX::ePixelComponentRGBA;
                break;
            default:
                assert(false);
                break;
        }
    }
    
  
}

struct ImageData
{
    float* srcPixelData;
    int rowBytes;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    int pixelComponentsCount;
};

void
GenericWriterPlugin::getPackingOptions(bool *allCheckboxHidden, std::vector<int>* packingMapping) const
{
    bool processChannels[4] = {true, true, true, true};
    bool processCheckboxSecret[4];
    *allCheckboxHidden = true;
    for (int i = 0; i < 4; ++i) {
        processCheckboxSecret[i] = _processChannels[i]->getIsSecret();
        if (!processCheckboxSecret[i]) {
            *allCheckboxHidden = false;
        }
    }
    if (!*allCheckboxHidden) {
        for (int i = 0; i < 4; ++i) {
            if (!processCheckboxSecret[i]) {
                _processChannels[i]->getValue(processChannels[i]);
            } else {
                processChannels[i] = false;
            }
            if (processChannels[i]) {
                packingMapping->push_back(i);
            }
        }
    }
}

void
GenericWriterPlugin::render(const OFX::RenderArguments &args)
{
    const double time = args.time;
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (!_inputClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    if (args.planes.empty()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Host did not requested any layer to render");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    std::string filename;
    _fileParam->getValueAtTime(time, filename);
    // filename = filenameFromPattern(filename, time);
    {
        std::string ext = extension(filename);
        if (!checkExtension(ext)) {
            setPersistentMessage(OFX::Message::eMessageError, "", std::string("Unsupported file extension: ") + ext);
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    float pixelAspectRatio = _inputClip->getPixelAspectRatio();
    
    ///This is automatically the same generally as inputClip premultiplication but can differ is the user changed it.
    int userPremult_i;
    _premult->getValueAtTime(time, userPremult_i);
    OFX::PreMultiplicationEnum userPremult = (OFX::PreMultiplicationEnum)userPremult_i;

    ///This is what the plug-in expects to be passed to the encode function.
    OFX::PreMultiplicationEnum pluginExpectedPremult = getExpectedInputPremultiplication();

    
    
    ///This is the mapping of destination channels onto source channels if packing happens
    std::vector<int> packingMapping;
    bool allCheckboxHidden;
    getPackingOptions(&allCheckboxHidden, &packingMapping);

    const bool doAnyPacking = args.planes.size() == 1 && !allCheckboxHidden;
    
    //Packing is required if channels are not contiguous, e.g: the user unchecked G but left R,B,A checked
    bool packingContiguous = false;

    if (doAnyPacking) {
        
        OFX::PixelComponentEnum clipComps = _inputClip->getPixelComponents();
      
        if (packingMapping.empty()) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Nothing to render: At least 1 channel checkbox must be checked");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (clipComps == OFX::ePixelComponentAlpha && packingMapping.size() != 1) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Output Components selected is Alpha: select only one single channel checkbox");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        
        if (packingMapping.size() == 1 && !_supportsAlpha) {
            if (_supportsXY) {
                packingMapping.push_back(-1);
            } else if (_supportsRGB) {
                for (int i = 0; i < 2; ++i) {
                    packingMapping.push_back(-1);
                }
            } else if (_supportsRGBA) {
                for (int i = 0; i < 3; ++i) {
                    packingMapping.push_back(-1);
                }
            } else {
                setPersistentMessage(OFX::Message::eMessageError, "", "Plug-in does not know how to render single-channel images");
                OFX::throwSuiteStatusException(kOfxStatFailed);

            }
        } else if (packingMapping.size() == 2 && !_supportsXY) {
            if (_supportsRGB) {
                packingMapping.push_back(-1);
            } else if (_supportsRGBA) {
                for (int i = 0; i < 2; ++i) {
                    packingMapping.push_back(-1);
                }
            } else {
                setPersistentMessage(OFX::Message::eMessageError, "", "Plug-in does not know how to render 2-channel images");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                
            }
        } else if (packingMapping.size() == 3 && !_supportsRGB) {
            if (_supportsRGBA) {
                packingMapping.push_back(-1);
            } else {
                setPersistentMessage(OFX::Message::eMessageError, "", "Plug-in does not know how to render 3-channel images");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                
            }
        } else if (packingMapping.size() == 4 && !_supportsRGBA) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Plug-in does not know how to render 4-channel images");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        int prevChannel = -1;
        for (std::size_t i = 0; i < packingMapping.size(); ++i) {
            if (i > 0) {
                if (packingMapping[i] != prevChannel + 1) {
                    packingContiguous = true;
                    break;
                }
            }
            prevChannel = packingMapping[i];
        }
    }
    
    
    
    // The following (commented out) code is not fully-safe, because the same instance may be have
    // two threads running on the same area of the same frame, and the apply()
    // calls both read and write dstImg.
    // This results in colorspace conversion being applied several times.
    //
    //if (dstImg.get()) {
    //// do the color-space conversion on dstImg
    //getImageData(dstImg.get(), &pixelData, &bounds, &pixelComponents, &rowBytes);
    //_ocio->apply(time, args.renderWindow, pixelData, bounds, pixelComponents, rowBytes);
    //encode(filename, time, pixelData, bounds, pixelComponents, rowBytes);
    //}
    //
    // The only viable solution (below) is to do the conversion in a temporary space,
    // and finally copy the result.
    //
    
   
    bool isOCIOIdentity = _ocio->isIdentity(time);

    //The host required that we render all views into 1 file. This is for now only supported by EXR.
    
    bool doDefaultView = false;
    std::map<int,std::string> viewNames;
    int viewToRender = getViewToRender();

    if (viewToRender == kGenericWriterViewAll) {
        if (args.renderView != 0) {
            return; // nothing to do, except for the main view
        }
        int nViews = getViewCount();
        for (int v = 0; v < nViews; ++v) {
            std::string view = getViewName(v);
            viewNames[v] = view;
        }
    } else {
        int viewToRender = getViewToRender();
        if (viewToRender >= 0 && viewToRender != args.renderView) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Inconsistent view to render requested");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        
        if (viewToRender == kGenericWriterViewAll) {
            /*
             We might be in this situation if the user requested %V or %v in the filename, so the host didn't request -1 as render view.
             We might also be here if the host never requests -1 as render view
             Just fallback to the default view
             */
            doDefaultView = true;
        } else if (viewToRender == kGenericWriterViewDefault) {
            doDefaultView = true;
        } else {
            std::string view;
            view = getViewName(viewToRender);
            viewNames[viewToRender] = view;
        }
    }
    
    if (viewNames.empty() || doDefaultView) {
        std::string view;
        if (gHostIsMultiView) {
            view = getViewName(args.renderView);
        } else {
            view = "Main";
        }
        viewNames[args.renderView] = view;
    }
    assert(!viewNames.empty());
    
    //This controls how we split into parts
    LayerViewsPartsEnum partsSplit = getPartsSplittingPreference();

    if (viewNames.size() == 1 && args.planes.size() == 1) {
        //Regular case, just do a simple part
        int viewIndex = viewNames.begin()->first;
        InputImagesHolder dataHolder;
        
        const OFX::Image* srcImg;
        OFX::ImageMemory *tmpMem;
        ImageData data;
        fetchPlaneConvertAndCopy(args.planes.front(), true, viewIndex, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents,&data.pixelComponentsCount);
        
        int dstNComps = doAnyPacking ? packingMapping.size() : data.pixelComponentsCount;
        int dstNCompsStartIndex = doAnyPacking ? packingMapping[0] : 0;
        
        encode(filename, time, viewNames[0], data.srcPixelData, args.renderWindow, pixelAspectRatio, data.pixelComponentsCount, dstNCompsStartIndex, dstNComps, data.rowBytes);
    
    } else {
        /*
         Use the beginEncodeParts/encodePart/endEncodeParts API when there are multiple views/planes to render
         Note that the number of times that we call encodePart depends on the LayerViewsPartsEnum value
         */
        assert(gHostIsMultiPlanar);
        EncodePlanesLocalData_RAII encodeData(this);
        InputImagesHolder dataHolder;
        
        
        if (partsSplit == eLayerViewsSplitViews &&
            args.planes.size() == 1) {
            /*
             Splitting views but only a single layer per view is equivalent to split views/layers: code path is shorter in later case
             */
            partsSplit = eLayerViewsSplitViewsLayers;
        }
        
        switch (partsSplit) {
            case eLayerViewsSinglePart: {
              /*
               We have to aggregate all views/layers into a single buffer and write it all at once.
               */
                int nChannels = 0;
                InputImagesHolder dataHolder;
                std::list<ImageData> planesData;
                
                // The list of actual planes that could be fetched
                std::list<std::string> actualPlanes;
                
                for (std::map<int,std::string>::const_iterator view = viewNames.begin(); view!=viewNames.end(); ++view) {
                    
                    
                    // The first view determines the planes that could be fetched. Other views just attempt to fetch the exact same planes.
                    const std::list<std::string> *planesToFetch = 0;
                    if (view == viewNames.begin()) {
                        planesToFetch = &args.planes;
                    } else {
                        planesToFetch = &actualPlanes;
                    }
                    
                    for (std::list<std::string>::const_iterator plane = planesToFetch->begin(); plane != planesToFetch->end(); ++plane) {
                        OFX::ImageMemory *tmpMem;
                        const OFX::Image* srcImg;
                        
                        ImageData data;
                        fetchPlaneConvertAndCopy(*plane, false, view->first, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);
                        if (!data.srcPixelData) {
                            continue;
                        }
                        
                        if (view == viewNames.begin()) {
                            actualPlanes.push_back(*plane);
                        }
                        
                        assert(data.pixelComponentsCount != 0 && data.pixelComponents != OFX::ePixelComponentNone);
                        
                        planesData.push_back(data);
                        int dstNComps = doAnyPacking ? packingMapping.size() : data.pixelComponentsCount;
                        nChannels += dstNComps;
                    }// for each plane
                } // for each view
                if (nChannels == 0) {
                    setPersistentMessage(OFX::Message::eMessageError, "", "Failed to fetch input layers");
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                    return;
                }
                int pixelBytes = nChannels * getComponentBytes(OFX::eBitDepthFloat);
                int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
                size_t memSize = (args.renderWindow.y2 - args.renderWindow.y1) * tmpRowBytes;
                OFX::ImageMemory interleavedMem(memSize, this);
                float* tmpMemPtr = (float*)interleavedMem.lock();
                if (!tmpMemPtr) {
                    OFX::throwSuiteStatusException(kOfxStatErrMemory);
                    return;
                }
                
                ///Set to 0 everywhere since the render window might be bigger than the src img bounds
                memset(tmpMemPtr, 0, memSize);
                
                int interleaveIndex = 0;
                for (std::list<ImageData>::iterator it = planesData.begin(); it!=planesData.end(); ++it) {
                    assert(interleaveIndex < nChannels);
                    
                    int dstNComps = doAnyPacking ? packingMapping.size() : it->pixelComponentsCount;
                    int dstNCompsStartIndex = doAnyPacking ? packingMapping[0] : 0;

                    OfxRectI intersection;
                    if (OFX::Coords::rectIntersection(args.renderWindow, it->bounds, &intersection)) {
                        interleavePixelBuffers(intersection, it->srcPixelData, it->bounds, it->pixelComponents, it->pixelComponentsCount, dstNCompsStartIndex,dstNComps, OFX::eBitDepthFloat, it->rowBytes, args.renderWindow, interleaveIndex, nChannels, tmpRowBytes, tmpMemPtr);
                    }
                    interleaveIndex += dstNComps;
                }
                
                beginEncodeParts(encodeData.getData(), filename, time, pixelAspectRatio, partsSplit, viewNames, actualPlanes, doAnyPacking && !packingContiguous,packingMapping, args.renderWindow);
                encodePart(encodeData.getData(), filename, tmpMemPtr, nChannels, 0, tmpRowBytes);
                
            } break;
            case eLayerViewsSplitViews: {
              /*
               Write each view into a single part but aggregate all layers for each view
               */
                // The list of actual planes that could be fetched
                std::list<std::string> actualPlanes;
                
                
                int partIndex = 0;
                for (std::map<int,std::string>::const_iterator view = viewNames.begin(); view!=viewNames.end(); ++view) {
                    
                    
                    // The first view determines the planes that could be fetched. Other views just attempt to fetch the exact same planes.
                    const std::list<std::string> *planesToFetch = 0;
                    if (view == viewNames.begin()) {
                        planesToFetch = &args.planes;
                    } else {
                        planesToFetch = &actualPlanes;
                    }
                    
                    int nChannels = 0;
                    InputImagesHolder dataHolder;
                    
                    std::list<ImageData> planesData;
                    for (std::list<std::string>::const_iterator plane = planesToFetch->begin(); plane != planesToFetch->end(); ++plane) {
                        
                        OFX::ImageMemory *tmpMem;
                        const OFX::Image* srcImg;
                        
                        ImageData data;
                        fetchPlaneConvertAndCopy(*plane, false, view->first, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);
                        if (!data.srcPixelData) {
                            continue;
                        }
                       
                        if (view == viewNames.begin()) {
                            actualPlanes.push_back(*plane);
                        }
                        
                        assert(data.pixelComponentsCount != 0 && data.pixelComponents != OFX::ePixelComponentNone);
                        
                        planesData.push_back(data);
                        
                        int dstNComps = doAnyPacking ? packingMapping.size() : data.pixelComponentsCount;
                        nChannels += dstNComps;

                    }
                    if (nChannels == 0) {
                        setPersistentMessage(OFX::Message::eMessageError, "", "Failed to fetch input layers");
                        OFX::throwSuiteStatusException(kOfxStatFailed);
                        return;
                    }
                    int pixelBytes = nChannels * getComponentBytes(OFX::eBitDepthFloat);
                    int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
                    size_t memSize = (args.renderWindow.y2 - args.renderWindow.y1) * tmpRowBytes;
                    OFX::ImageMemory interleavedMem(memSize, this);
                    float* tmpMemPtr = (float*)interleavedMem.lock();
                    if (!tmpMemPtr) {
                        OFX::throwSuiteStatusException(kOfxStatErrMemory);
                        return;
                    }
                    
                    ///Set to 0 everywhere since the render window might be bigger than the src img bounds
                    memset(tmpMemPtr, 0, memSize);
                    
                    int interleaveIndex = 0;
                    for (std::list<ImageData>::iterator it = planesData.begin(); it!=planesData.end(); ++it) {
                        assert(interleaveIndex < nChannels);
                        
                        OfxRectI intersection;
                        int dstNComps = doAnyPacking ? packingMapping.size() : it->pixelComponentsCount;
                        int dstNCompsStartIndex = doAnyPacking ? packingMapping[0] : 0;
                        if (OFX::Coords::rectIntersection(args.renderWindow, it->bounds, &intersection)) {
                            
                            
                            
                            interleavePixelBuffers(intersection, it->srcPixelData, it->bounds,
                                                   it->pixelComponents, it->pixelComponentsCount, dstNCompsStartIndex, dstNComps,
                                                   OFX::eBitDepthFloat, it->rowBytes, args.renderWindow,
                                                   interleaveIndex, nChannels, tmpRowBytes, tmpMemPtr);
                        }
                        interleaveIndex += dstNComps;
                    }
                    
                    if (view == viewNames.begin()) {
                        beginEncodeParts(encodeData.getData(), filename, time, pixelAspectRatio, partsSplit, viewNames, actualPlanes, doAnyPacking && !packingContiguous,packingMapping, args.renderWindow);
                    }
                    
                    encodePart(encodeData.getData(), filename, tmpMemPtr, nChannels, partIndex, tmpRowBytes);
                    
                    ++partIndex;
                } // for each view
            } break;
            case eLayerViewsSplitViewsLayers: {
              /*
               Write each layer of each view in an independent part
               */
                // The list of actual planes that could be fetched
                std::list<std::string> actualPlanes;

                int partIndex = 0;
                for (std::map<int,std::string>::const_iterator view = viewNames.begin(); view!=viewNames.end(); ++view) {
                    InputImagesHolder dataHolder;
                    std::vector<ImageData> datas;
                    
                    // The first view determines the planes that could be fetched. Other views just attempt to fetch the exact same planes.
                    const std::list<std::string> *planesToFetch = 0;
                    if (view == viewNames.begin()) {
                        planesToFetch = &args.planes;
                    } else {
                        planesToFetch = &actualPlanes;
                    }
                    
                    for (std::list<std::string>::const_iterator plane = planesToFetch->begin(); plane != planesToFetch->end(); ++plane) {
                    
                        
                        OFX::ImageMemory *tmpMem;
                        const OFX::Image* srcImg;
                        ImageData data;
                        fetchPlaneConvertAndCopy(*plane, false, view->first, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);
                        if (!data.srcPixelData) {
                            continue;
                        }
                        datas.push_back(data);
                        if (view == viewNames.begin()) {
                            actualPlanes.push_back(*plane);
                        }

                    } // for each plane
                    
                    if (view == viewNames.begin()) {
                        beginEncodeParts(encodeData.getData(), filename, time, pixelAspectRatio, partsSplit, viewNames, actualPlanes, doAnyPacking && !packingContiguous,packingMapping, args.renderWindow);
                    }
                    for (std::vector<ImageData>::iterator it = datas.begin(); it!=datas.end(); ++it) {
                        encodePart(encodeData.getData(), filename, it->srcPixelData, it->pixelComponentsCount, partIndex, it->rowBytes);
                        ++partIndex;
                    }
                } // for each view
            } break;
        };
        
        endEncodeParts(encodeData.getData());
    }
    
    clearPersistentMessage();
}


class PackPixelsProcessorBase: public OFX::PixelProcessorFilterBase
{
protected:
    
    std::vector<int> _mapping;
    
public:
    PackPixelsProcessorBase(OFX::ImageEffect& instance)
    : OFX::PixelProcessorFilterBase(instance)
    , _mapping()
    {
    }
    
    void setMapping(const std::vector<int>& mapping)
    {
        _mapping = mapping;
    }
};


template <typename PIX, int maxValue, int srcNComps>
class PackPixelsProcessor : public PackPixelsProcessorBase
{
public:
    
    PackPixelsProcessor(OFX::ImageEffect& instance)
    : PackPixelsProcessorBase(instance)
    {
    }
    
    virtual void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert(_srcBounds.x1 < _srcBounds.x2 && _srcBounds.y1 < _srcBounds.y2);
        assert((int)_mapping.size() == _dstPixelComponentCount);
        
        PIX *dstPix = (PIX *)getDstPixelAddress(procWindow.x1, procWindow.y1);
        assert(dstPix);
        
        const PIX *srcPix = (const PIX *) getSrcPixelAddress(procWindow.x1, procWindow.y1);
        assert(srcPix);
        
        const int srcRowElements = _srcRowBytes / sizeof(PIX);
        const int dstRowElements = _dstRowBytes / sizeof(PIX);
        const int procWidth = procWindow.x2 - procWindow.x1;
        
        for (int y = procWindow.y1; y < procWindow.y2; ++y,
             srcPix += (srcRowElements - procWidth * srcNComps), // Move to next row and substract what was done on last iteration
             dstPix += (dstRowElements - procWidth * _dstPixelComponentCount)
             ) {
            
            if ((y % 100 == 0) && _effect.abort()) {
                //check for abort only every 100 lines
                break;
            }
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x,
                 srcPix += srcNComps,
                 dstPix += _dstPixelComponentCount
                 ) {
                
                assert(srcPix == ((const PIX*)getSrcPixelAddress(x, y)));
                for (int c = 0; c < _dstPixelComponentCount; ++ c) {
                    int srcCol = _mapping[c];
                    if (srcCol == -1) {
                        dstPix[c] = c != 3 ? 0 : maxValue;
                    } else {
                        if (srcCol < srcNComps) {
                            dstPix[c] = srcPix[srcCol];
                        } else {
                            if (srcNComps == 1) {
                                dstPix[c] = *srcPix;
                            } else {
                                dstPix[c] = c != 3 ? 0 : maxValue;
                            }
                        }
                    }
                    
                }
                
            }
        }
        
    }
    
};



template <typename PIX,int maxValue>
void packPixelBufferForDepth(OFX::ImageEffect* instance,
                             const OfxRectI& renderWindow,
                             const void *srcPixelData,
                             const OfxRectI& bounds,
                             OFX::BitDepthEnum bitDepth,
                             int srcRowBytes,
                             OFX::PixelComponentEnum srcPixelComponents,
                             const std::vector<int>& channelsMapping,
                             int dstRowBytes,
                             void* dstPixelData)
{
    assert(channelsMapping.size() <= 4);
    std::auto_ptr<PackPixelsProcessorBase> p;
    int srcNComps = 0;
    switch (srcPixelComponents) {
        case OFX::ePixelComponentAlpha:
            p.reset(new PackPixelsProcessor<PIX,maxValue,1>(*instance));
            srcNComps = 1;
            break;
        case OFX::ePixelComponentXY:
            p.reset(new PackPixelsProcessor<PIX,maxValue,2>(*instance));
            srcNComps = 2;
            break;
        case OFX::ePixelComponentRGB:
            p.reset(new PackPixelsProcessor<PIX,maxValue,3>(*instance));
            srcNComps = 3;
            break;
        case OFX::ePixelComponentRGBA:
            p.reset(new PackPixelsProcessor<PIX,maxValue,4>(*instance));
            srcNComps = 4;
            break;
        default:
            //Unsupported components
            OFX::throwSuiteStatusException(kOfxStatFailed);
            break;
    };
    
    p->setSrcImg(srcPixelData, bounds, srcPixelComponents, srcNComps, bitDepth, srcRowBytes, 0);
    p->setDstImg(dstPixelData, bounds, srcPixelComponents /*this argument is meaningless*/, channelsMapping.size(), bitDepth, dstRowBytes);
    p->setRenderWindow(renderWindow);
    
    p->setMapping(channelsMapping);
    
    p->process();
}



void
GenericWriterPlugin::packPixelBuffer(const OfxRectI& renderWindow,
                                     const void *srcPixelData,
                                     const OfxRectI& bounds,
                                     OFX::BitDepthEnum bitDepth,
                                     int srcRowBytes,
                                     OFX::PixelComponentEnum srcPixelComponents,
                                     const std::vector<int>& channelsMapping, //maps dst channels to input channels
                                     int dstRowBytes,
                                     void* dstPixelData)
{
    assert(renderWindow.x1 >= bounds.x1 && renderWindow.x2 <= bounds.x2 &&
           renderWindow.y1 >= bounds.y1 && renderWindow.y2 <= bounds.y2);
    switch (bitDepth) {
        case OFX::eBitDepthFloat:
            packPixelBufferForDepth<float, 1>(this, renderWindow, (const float*)srcPixelData, bounds, bitDepth, srcRowBytes, srcPixelComponents, channelsMapping, dstRowBytes, (float*)dstPixelData);
            break;
        case OFX::eBitDepthUByte:
            packPixelBufferForDepth<unsigned char, 255>(this, renderWindow, (const unsigned char*)srcPixelData, bounds, bitDepth, srcRowBytes, srcPixelComponents, channelsMapping, dstRowBytes, (unsigned char*)dstPixelData);
            break;
        case OFX::eBitDepthUShort:
            packPixelBufferForDepth<unsigned short, 65535>(this, renderWindow, (const unsigned short*)srcPixelData, bounds, bitDepth, srcRowBytes, srcPixelComponents, channelsMapping, dstRowBytes, (unsigned short*)dstPixelData);
            break;
        default:
            //unknown pixel depth
            OFX::throwSuiteStatusException(kOfxStatFailed);
            break;
    }
    
}

class InterleaveProcessorBase: public OFX::PixelProcessorFilterBase
{
protected:
    
    int _dstStartIndex;
    int _desiredSrcNComps;
    int _srcNCompsStartIndex;
public:
    InterleaveProcessorBase(OFX::ImageEffect& instance)
    : OFX::PixelProcessorFilterBase(instance)
    , _dstStartIndex(-1)
    , _desiredSrcNComps(-1)
    , _srcNCompsStartIndex(0)
    {
    }
    
    void setValues(int dstStartIndex,
                   int desiredSrcNComps,
                   int srcNCompsStartIndex)
    {
        _dstStartIndex = dstStartIndex;
        _desiredSrcNComps = desiredSrcNComps;
        _srcNCompsStartIndex = srcNCompsStartIndex;
    }
};

template <typename PIX, int maxValue, int srcNComps>
class InterleaveProcessor : public InterleaveProcessorBase
{
public:
    
    InterleaveProcessor(OFX::ImageEffect& instance)
    : InterleaveProcessorBase(instance)
    {
    }
    
    virtual void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert(_srcBounds.x1 < _srcBounds.x2 && _srcBounds.y1 < _srcBounds.y2);
        assert(_dstStartIndex >= 0);
        PIX *dstPix = (PIX *)getDstPixelAddress(procWindow.x1, procWindow.y1);
        assert(dstPix);
        dstPix += _dstStartIndex;
        
        const PIX *srcPix = (const PIX *) getSrcPixelAddress(procWindow.x1, procWindow.y1);
        assert(srcPix);
        
        const int srcRowElements = _srcRowBytes / sizeof(PIX);
        const int dstRowElements = _dstRowBytes / sizeof(PIX);
        const int procWidth = procWindow.x2 - procWindow.x1;
        
        for (int y = procWindow.y1; y < procWindow.y2; ++y,
             srcPix += (srcRowElements - procWidth * srcNComps), // Move to next row and substract what was done on last iteration
             dstPix += (dstRowElements - procWidth * _dstPixelComponentCount)
             ) {
            
            if ((y % 10 == 0) && _effect.abort()) {
                //check for abort only every 10 lines
                break;
            }
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x,
                 srcPix += srcNComps,
                 dstPix += _dstPixelComponentCount
                 ) {
                assert(dstPix == ((PIX*)getDstPixelAddress(x, y)) + _dstStartIndex);
                assert(srcPix == ((const PIX*)getSrcPixelAddress(x, y)));
                
                for (int c = 0; c < _desiredSrcNComps; ++c) {
                    dstPix[c] = srcPix[c + _srcNCompsStartIndex];
                }
                
            }
        }

    }

};

template <typename PIX,int maxValue>
void interleavePixelBuffersForDepth(OFX::ImageEffect* instance,
                                    const OfxRectI& renderWindow,
                                    const PIX *srcPixelData,
                                    const OfxRectI& bounds,
                                    const OFX::PixelComponentEnum srcPixelComponents,
                                    const int srcPixelComponentCount,
                                    const int srcNCompsStartIndex,
                                    const int desiredSrcNComps,
                                    const OFX::BitDepthEnum bitDepth,
                                    const int srcRowBytes,
                                    const OfxRectI& dstBounds,
                                    const int dstPixelComponentStartIndex,
                                    const int dstPixelComponentCount,
                                    const int dstRowBytes,
                                    PIX* dstPixelData)
{
    std::auto_ptr<InterleaveProcessorBase> p;
    switch (srcPixelComponentCount) {
        case 1:
            p.reset(new InterleaveProcessor<PIX,maxValue,1>(*instance));
            break;
        case 2:
            p.reset(new InterleaveProcessor<PIX,maxValue,2>(*instance));
            break;
        case 3:
            p.reset(new InterleaveProcessor<PIX,maxValue,3>(*instance));
            break;
        case 4:
            p.reset(new InterleaveProcessor<PIX,maxValue,4>(*instance));
            break;
        default:
            //Unsupported components
            OFX::throwSuiteStatusException(kOfxStatFailed);
            break;
    };
    p->setSrcImg(srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, bitDepth, srcRowBytes, 0);
    p->setDstImg(dstPixelData, dstBounds, srcPixelComponents /*this argument is meaningless*/, dstPixelComponentCount, bitDepth, dstRowBytes);
    p->setRenderWindow(renderWindow);
    p->setValues(dstPixelComponentStartIndex, desiredSrcNComps, srcNCompsStartIndex);
    
    p->process();
}



void
GenericWriterPlugin::interleavePixelBuffers(const OfxRectI& renderWindow,
                                            const void *srcPixelData,
                                            const OfxRectI& bounds,
                                            const OFX::PixelComponentEnum srcPixelComponents,
                                            const int srcPixelComponentCount,
                                            const int srcNCompsStartIndex,
                                            const int desiredSrcNComps,
                                            const OFX::BitDepthEnum bitDepth,
                                            const int srcRowBytes,
                                            const OfxRectI& dstBounds,
                                            const int dstPixelComponentStartIndex,
                                            const int dstPixelComponentCount,
                                            const int dstRowBytes,
                                            void* dstPixelData)
{
    assert(renderWindow.x1 >= bounds.x1 && renderWindow.x2 <= bounds.x2 &&
           renderWindow.y1 >= bounds.y1 && renderWindow.y2 <= bounds.y2);
    assert(renderWindow.x1 >= dstBounds.x1 && renderWindow.x2 <= dstBounds.x2 &&
           renderWindow.y1 >= dstBounds.y1 && renderWindow.y2 <= dstBounds.y2);
    switch (bitDepth) {
        case OFX::eBitDepthFloat:
            interleavePixelBuffersForDepth<float, 1>(this, renderWindow, (const float*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, srcNCompsStartIndex, desiredSrcNComps, bitDepth,srcRowBytes, dstBounds, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (float*)dstPixelData);
            break;
        case OFX::eBitDepthUByte:
            interleavePixelBuffersForDepth<unsigned char, 255>(this, renderWindow, (const unsigned char*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, srcNCompsStartIndex, desiredSrcNComps, bitDepth, srcRowBytes, dstBounds, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (unsigned char*)dstPixelData);
            break;
        case OFX::eBitDepthUShort:
            interleavePixelBuffersForDepth<unsigned short, 65535>(this, renderWindow, (const unsigned short*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, srcNCompsStartIndex, desiredSrcNComps, bitDepth, srcRowBytes, dstBounds, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (unsigned short*)dstPixelData);
            break;
        default:
            //unknown pixel depth
            OFX::throwSuiteStatusException(kOfxStatFailed);
            break;
    }
}

void
GenericWriterPlugin::beginSequenceRender(const OFX::BeginSequenceRenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    std::string filename;
    _fileParam->getValue(filename);
    {
        std::string ext = extension(filename);
        if (!checkExtension(ext)) {
            setPersistentMessage(OFX::Message::eMessageError, "", std::string("Unsupported file extension: ") + ext);
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    OfxRectD rod;
    getOutputFormat(args.frameRange.min, rod);
    
    ////Since the generic writer doesn't support tiles and multi-resolution, the RoD is necesserarily the
    ////output image size.
    OfxRectI rodPixel;
    float pixelAspectRatio = _inputClip->getPixelAspectRatio();
    OFX::Coords::toPixelEnclosing(rod, args.renderScale, pixelAspectRatio, &rodPixel);

    beginEncode(filename, rodPixel, pixelAspectRatio, args);
}


void
GenericWriterPlugin::endSequenceRender(const OFX::EndSequenceRenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    endEncode(args);
}


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

static void
setupAndProcess(OFX::PixelProcessorFilterBase & processor,
                int premultChannel,
                const OfxRectI &renderWindow,
                const void *srcPixelData,
                const OfxRectI& srcBounds,
                OFX::PixelComponentEnum srcPixelComponents,
                int srcPixelComponentCount,
                OFX::BitDepthEnum srcPixelDepth,
                int srcRowBytes,
                void *dstPixelData,
                const OfxRectI& dstBounds,
                OFX::PixelComponentEnum dstPixelComponents,
                int dstPixelComponentCount,
                OFX::BitDepthEnum dstPixelDepth,
                int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // make sure bit depths are sane
    if (srcPixelDepth != dstPixelDepth || srcPixelComponents != dstPixelComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, 0);
    
    // set the render window
    processor.setRenderWindow(renderWindow);
    
    processor.setPremultMaskMix(true, premultChannel, 1.);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


void
GenericWriterPlugin::unPremultPixelData(const OfxRectI &renderWindow,
                                        const void *srcPixelData,
                                        const OfxRectI& srcBounds,
                                        OFX::PixelComponentEnum srcPixelComponents,
                                        int srcPixelComponentCount,
                                        OFX::BitDepthEnum srcPixelDepth,
                                        int srcRowBytes,
                                        void *dstPixelData,
                                        const OfxRectI& dstBounds,
                                        OFX::PixelComponentEnum dstPixelComponents,
                                        int dstPixelComponentCount,
                                        OFX::BitDepthEnum dstBitDepth,
                                        int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
        setupAndProcess(fred, 3, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else {
        ///other pixel components means you want to copy only...
        assert(false);
    }
}

void
GenericWriterPlugin::premultPixelData(const OfxRectI &renderWindow,
                                      const void *srcPixelData,
                                      const OfxRectI& srcBounds,
                                      OFX::PixelComponentEnum srcPixelComponents,
                                      int srcPixelComponentCount,
                                      OFX::BitDepthEnum srcPixelDepth,
                                      int srcRowBytes,
                                      void *dstPixelData,
                                      const OfxRectI& dstBounds,
                                      OFX::PixelComponentEnum dstPixelComponents,
                                      int dstPixelComponentCount,
                                      OFX::BitDepthEnum dstBitDepth,
                                      int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);
    
    // do the rendering
    if (dstBitDepth != OFX::eBitDepthFloat || (dstPixelComponents != OFX::ePixelComponentRGBA && dstPixelComponents != OFX::ePixelComponentRGB && dstPixelComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopierPremult<float, 4, 1, float, 4, 1> fred(*this);
        setupAndProcess(fred, 3, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);

    } else {
        ///other pixel components means you want to copy only...
        assert(false);
    }
}

void
GenericWriterPlugin::getOutputFormat(OfxTime time,OfxRectD& rod)
{
    
    int formatType;
    _outputFormatType->getValue(formatType);
    
    bool doDefaultBehaviour = false;
    if (formatType == 0) {
        doDefaultBehaviour = true;
    } else if (formatType == 1) {
        
        
        bool clipToProject = true;
        if (_clipToProject && !_clipToProject->getIsSecret()) {
            _clipToProject->getValue(clipToProject);
        }
        if (!clipToProject) {
            doDefaultBehaviour = true;
        } else {
            OfxPointD size = getProjectSize();
            OfxPointD offset = getProjectOffset();
            rod.x1 = offset.x;
            rod.y1 = offset.y;
            rod.x2 = offset.x + size.x;
            rod.y2 = offset.y + size.y;
        }
    } else if (formatType == 2) {

        int w,h;
        double par;
        _outputFormatSize->getValue(w,h);
        _outputFormatPar->getValue(par);
        
        OfxRectI rodPixel;
        rodPixel.x1 = rodPixel.y1 = 0;
        rodPixel.x2 = w;
        rodPixel.y2 = h;
        OfxPointD renderScale = {1., 1.};
        OFX::Coords::toCanonical(rodPixel, renderScale, par, &rod);
    }
    if (doDefaultBehaviour) {
        // union RoD across all views
        int viewsToRender = getViewToRender();
        if (viewsToRender == kGenericWriterViewDefault || !gHostIsMultiView) {
            rod = _inputClip->getRegionOfDefinition(time);
        } else {
            if (viewsToRender == kGenericWriterViewAll) {
                //Union all views
                bool rodSet = false;
                for (int i = 0; i < getViewCount(); ++i) {
                    OfxRectD subRod = _inputClip->getRegionOfDefinition(time, i);
                    if (!rodSet) {
                        rodSet = true;
                        rod = subRod;
                    } else {
                        OFX::Coords::rectBoundingBox(rod, subRod, &rod);
                    }
                }
            } else {
                rod = _inputClip->getRegionOfDefinition(viewsToRender);
            }
        }
    }
}

bool
GenericWriterPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }
    getOutputFormat(args.time, rod);
    return true;
}

void
GenericWriterPlugin::encode(const std::string& /*filename*/,
                            const OfxTime /*time*/,
                            const std::string& /*viewName*/,
                            const float */*pixelData*/,
                            const OfxRectI& /*bounds*/,
                            const float /*pixelAspectRatio*/,
                            const int /*pixelDataNComps*/,
                            const int /*dstNCompsStartIndex*/,
                            const int /*dstNComps*/,
                            const int /*rowBytes*/)
{
    /// Does nothing
}


void
GenericWriterPlugin::beginEncodeParts(void* /*user_data*/,
                                      const std::string& /*filename*/,
                                      OfxTime /*time*/,
                                      float /*pixelAspectRatio*/,
                                      LayerViewsPartsEnum /*partsSplitting*/,
                                      const std::map<int,std::string>& /*viewsToRender*/,
                                      const std::list<std::string>& /*planes*/,
                                      const bool /*packingRequired*/,
                                      const std::vector<int>& /*packingMapping*/,
                                      const OfxRectI& /*bounds*/)
{
    /// Does nothing
}


void
GenericWriterPlugin::encodePart(void* /*user_data*/, const std::string& /*filename*/, const float */*pixelData*/, int /*pixelDataNComps*/, int /*planeIndex*/, int /*rowBytes*/)
{
    /// Does nothing
}

bool
GenericWriterPlugin::getTimeDomain(OfxRangeD &range)
{
    int choice;
    _frameRange->getValue(choice);
    if (choice == 0) {
        ///let the default be applied
        return false;
    } else if (choice == 1) {
        timeLineGetBounds(range.min, range.max);
        return true;
    } else {
        int first;
        _firstFrame->getValue(first);
        range.min = first;
        
        int last;
        _lastFrame->getValue(last);
        range.max = last;
        return true;
    }
}

static std::string
imageFormatString(OFX::PixelComponentEnum components, OFX::BitDepthEnum bitDepth)
{
    std::string s;
    switch (components) {
        case OFX::ePixelComponentRGBA:
            s += "RGBA";
            break;
        case OFX::ePixelComponentRGB:
            s += "RGB";
            break;
        case OFX::ePixelComponentAlpha:
            s += "Alpha";
            break;
        case OFX::ePixelComponentCustom:
            s += "Custom";
            break;
        case OFX::ePixelComponentNone:
            s += "None";
            break;
        default:
            s += "[unknown components]";
            break;
    }
    switch (bitDepth) {
        case OFX::eBitDepthUByte:
            s += "8u";
            break;
        case OFX::eBitDepthUShort:
            s += "16u";
            break;
        case OFX::eBitDepthFloat:
            s += "32f";
            break;
        case OFX::eBitDepthCustom:
            s += "x";
            break;
        case OFX::eBitDepthNone:
            s += "0";
            break;
        default:
            s += "[unknown bit depth]";
            break;
    }
    return s;
}

static std::string
premultString(OFX::PreMultiplicationEnum e)
{
    switch (e) {
        case OFX::eImageOpaque:
            return "Opaque";
        case OFX::eImagePreMultiplied:
            return "PreMultiplied";
        case OFX::eImageUnPreMultiplied:
            return "UnPreMultiplied";
    }
    return "Unknown";
}

void
GenericWriterPlugin::setOutputComponentsParam(OFX::PixelComponentEnum components)
{
    assert(components == OFX::ePixelComponentRGB || components == OFX::ePixelComponentRGBA || components == OFX::ePixelComponentAlpha);

}

void
GenericWriterPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamFrameRange) {
        int choice;
        double first,last;
        timeLineGetBounds(first,last);
        _frameRange->getValue(choice);
        if (choice == 2) {
            _firstFrame->setIsSecret(false);
            _firstFrame->setValue((int)first);
            _lastFrame->setIsSecret(false);
            _lastFrame->setValue((int)last);
        } else {
            _firstFrame->setIsSecret(true);
            _lastFrame->setIsSecret(true);
        }
    } else if (paramName == kParamFilename) {
        std::string filename;
        _fileParam->getValue(filename);
        // filename = filenameFromPattern(filename, time);
        {
            std::string ext = extension(filename);
            if (!checkExtension(ext)) {
                if (args.reason == OFX::eChangeUserEdit) {
                    sendMessage(OFX::Message::eMessageError, "", std::string("Unsupported file extension: ") + ext);
                } else {
                    setPersistentMessage(OFX::Message::eMessageError, "", std::string("Unsupported file extension: ") + ext);
                }
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }


        bool setColorSpace = true;
# ifdef OFX_IO_USING_OCIO
        // Always try to parse from string first,
        // following recommendations from http://opencolorio.org/configurations/spi_pipeline.html
        if (_ocio->getConfig()) {
            const char* colorSpaceStr = _ocio->getConfig()->parseColorSpaceFromString(filename.c_str());
            if (colorSpaceStr && std::strlen(colorSpaceStr) == 0) {
                colorSpaceStr = NULL;
            }
            if (colorSpaceStr && _ocio->hasColorspace(colorSpaceStr)) {
                // we're lucky
                _ocio->setOutputColorspace(colorSpaceStr);
                setColorSpace = false;
            }
        }
# endif
        
        ///let the derive class a chance to initialize any data structure it may need
        onOutputFileChanged(filename, setColorSpace);
        
        if (_clipToProject) {
            int type;
            _outputFormatType->getValue(type);
            _clipToProject->setIsSecret(type != 1 || !displayWindowSupportedByFormat(filename));
        }
        
    } else if (paramName == kParamFormatType) {
        int type;
        _outputFormatType->getValue(type);
        if (_clipToProject) {
            std::string filename;
            _fileParam->getValue(filename);
            _clipToProject->setIsSecret(type != 1 || !displayWindowSupportedByFormat(filename));
        }
        if (type == 0 || type == 1) {
            _outputFormat->setIsSecret(true);
        } else {
            _outputFormat->setIsSecret(false);
        }
        
        
        
    } else if (paramName == kParamOutputFormat) {
        //the host does not handle the format itself, do it ourselves
        int format_i;
        _outputFormat->getValue(format_i);
        int w = 0, h = 0;
        double par = -1;
        getFormatResolution((OFX::EParamFormat)format_i, &w, &h, &par);
        assert(par != -1);
        _outputFormatPar->setValue(par);
        _outputFormatSize->setValue(w, h);
    } else if (paramName == kParamClipInfo && args.reason == OFX::eChangeUserEdit) {
        std::string msg;
        msg += "Input: ";
        if (!_inputClip) {
            msg += "N/A";
        } else {
            msg += imageFormatString(_inputClip->getPixelComponents(), _inputClip->getPixelDepth());
            msg += " ";
            msg += premultString(_inputClip->getPreMultiplication());
        }
        msg += "\n";
        msg += "Output: ";
        if (!_outputClip) {
            msg += "N/A";
        } else {
            msg += imageFormatString(_outputClip->getPixelComponents(), _outputClip->getPixelDepth());
            msg += " ";
            msg += premultString(_outputClip->getPreMultiplication());

        }
        msg += "\n";
        sendMessage(OFX::Message::eMessageMessage, "", msg);
    }


    _ocio->changedParam(args, paramName);
}

void
GenericWriterPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _inputClip && args.reason == OFX::eChangeUserEdit) {
        OFX::PreMultiplicationEnum premult = _inputClip->getPreMultiplication();
#     ifdef DEBUG
        if (_inputClip->isConnected()) {
            OFX::PixelComponentEnum components = _inputClip->getPixelComponents();
            assert((components == OFX::ePixelComponentAlpha && premult != OFX::eImageOpaque) ||
                   (components == OFX::ePixelComponentRGB && premult == OFX::eImageOpaque) ||
                   (components == OFX::ePixelComponentRGBA) ||
                   ((components == OFX::ePixelComponentCustom ||
                     components == OFX::ePixelComponentMotionVectors ||
                     components == OFX::ePixelComponentStereoDisparity) && gHostIsMultiPlanar));
            
            
            int index = -1;
            for (std::size_t i = 0; i < gPluginOutputComponents.size(); ++i) {
                if (gPluginOutputComponents[i] == components) {
                    index = i;
                    break;
                }
            }
            assert(index != -1);
            if (index != -1) {
                _outputComponents->setValue(index);
            }
        }
#      endif
        _premult->setValue(premult);
        
        double fps = _inputClip->getFrameRate();
        setOutputFrameRate(fps);
 
        
    }
}


void
GenericWriterPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    
    if (!_outputComponents->getIsSecret()) {
        int index;
        _outputComponents->getValue(index);
        assert(index >= 0 && index < (int)gPluginOutputComponents.size());
        OFX::PixelComponentEnum comps = gPluginOutputComponents[index];
        
        
        std::vector<std::string> checkboxesLabels;
        if (comps == OFX::ePixelComponentAlpha) {
            checkboxesLabels.push_back("A");
        } else if (comps == OFX::ePixelComponentRGB) {
            checkboxesLabels.push_back("R");
            checkboxesLabels.push_back("G");
            checkboxesLabels.push_back("B");
        } else if (comps == OFX::ePixelComponentRGBA) {
            checkboxesLabels.push_back("R");
            checkboxesLabels.push_back("G");
            checkboxesLabels.push_back("B");
            checkboxesLabels.push_back("A");
        }
        
        if (checkboxesLabels.size() == 1) {
            for (int i = 0; i < 3; ++i) {
                _processChannels[i]->setIsSecret(true);
            }
            _processChannels[3]->setIsSecret(false);
            _processChannels[3]->setLabel(checkboxesLabels[0]);
        } else {
            for (int i = 0; i < 4; ++i) {
                if (i < (int)checkboxesLabels.size()) {
                    _processChannels[i]->setIsSecret(false);
                    _processChannels[i]->setLabel(checkboxesLabels[i]);
                } else {
                    _processChannels[i]->setIsSecret(true);
                }
            }
        }
        //Set output pixel components to match what will be output if the choice is not All
        
        
        clipPreferences.setClipComponents(*_inputClip, comps);
        clipPreferences.setClipComponents(*_outputClip, comps);
        OFX::PreMultiplicationEnum premult = _inputClip->getPreMultiplication();
        switch (comps) {
            case OFX::ePixelComponentAlpha:
                premult = OFX::eImageUnPreMultiplied;
                break;
            case OFX::ePixelComponentXY:
                premult = OFX::eImageOpaque;
                break;
            case OFX::ePixelComponentRGB:
                premult = OFX::eImageOpaque;
                break;
            default:
                break;
        }
        
        clipPreferences.setOutputPremultiplication(premult);

    }
    
    
}

void
GenericWriterPlugin::getFrameViewsNeeded(const OFX::FrameViewsNeededArguments& args, OFX::FrameViewsNeededSetter& frameViews)
{
    OfxRangeD r;
    r.min = r.max = args.time;
    
    if (!gHostIsMultiView) {
        //As whats requested
        frameViews.addFrameViewsNeeded(*_inputClip, r, args.view);
    } else {
        int viewsToRender = getViewToRender();
        if (viewsToRender == kGenericWriterViewAll) {
            if (args.view != 0) {
                // any view other than view 0 does nothing and requires no input
                return;
            }
            // rendering view 0 requires all views, and writes them to file
            int nViews = getViewCount();
            for (int i = 0; i < nViews; ++i) {
                frameViews.addFrameViewsNeeded(*_inputClip, r, i);
            }
        } else {
            // default behavior
            if (viewsToRender == kGenericWriterViewDefault) {
                viewsToRender = args.view;
            }
            frameViews.addFrameViewsNeeded(*_inputClip, r, viewsToRender);
        }
    }
}

void
GenericWriterPlugin::purgeCaches()
{
    clearAnyCache();
    _ocio->purgeCaches();
}



using namespace OFX;

/**
 * @brief Override this to describe the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describe(desc);
 **/
void
GenericWriterDescribe(OFX::ImageEffectDescriptor &desc,
                      OFX::RenderSafetyEnum safety,
                      const std::vector<std::string>& extensions, // list of supported extensions
                      int evaluation, // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated
                      bool isMultiPlanar,
                      bool isMultiView)
{
    desc.setPluginGrouping(kPluginGrouping);
    
#ifdef OFX_EXTENSIONS_TUTTLE
    desc.addSupportedContext(OFX::eContextWriter);
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(evaluation);
#endif
    desc.addSupportedContext(OFX::eContextGeneral);

    // OCIO is only supported for float images.
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(safety);
    
    
#ifdef OFX_EXTENSIONS_NUKE
    if (OFX::getImageEffectHostDescription()
        && OFX::getImageEffectHostDescription()->isMultiPlanar) {
        desc.setIsMultiPlanar(isMultiPlanar);
        if (isMultiPlanar) {
            gHostIsMultiPlanar = true;
            //We let all un-rendered planes pass-through so that they can be retrieved below by a shuffle node
            desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelPassThroughNonRenderedPlanes);
        }
    }
    if (isMultiView && OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true)) {
        gHostIsMultiView = true;
        desc.setIsViewAware(true);
        desc.setIsViewInvariant(OFX::eViewInvarianceAllViewsVariant);
    }
#endif
    
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
PageParamDescriptor*
GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, bool supportsXY, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault, bool supportsDisplayWindow)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

    
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    if (supportsRGBA) {
        srcClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        srcClip->addSupportedComponent(ePixelComponentRGB);
    }
    if (supportsAlpha) {
        srcClip->addSupportedComponent(ePixelComponentAlpha);
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (supportsXY && gHostIsNatron) {
        srcClip->addSupportedComponent(ePixelComponentXY);
    }
#endif
    srcClip->setSupportsTiles(kSupportsTiles);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    if (supportsRGBA) {
        dstClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        dstClip->addSupportedComponent(ePixelComponentRGB);
    }
    if (supportsAlpha) {
        dstClip->addSupportedComponent(ePixelComponentAlpha);
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (supportsXY && gHostIsNatron) {
        dstClip->addSupportedComponent(ePixelComponentXY);
    }
#endif
    dstClip->setSupportsTiles(kSupportsTiles);//< we don't support tiles in output!


    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputComponents);
        param->setLabel(kParamOutputComponentsLabel);
        param->setHint(kParamOutputComponentsHint);
        if (supportsAlpha) {
            param->appendOption("Alpha");
            gPluginOutputComponents.push_back(OFX::ePixelComponentAlpha);
        }
        if (supportsRGB) {
            param->appendOption("RGB");
            gPluginOutputComponents.push_back(OFX::ePixelComponentRGB);
        }
        if (supportsRGBA) {
            param->appendOption("RGBA");
            gPluginOutputComponents.push_back(OFX::ePixelComponentRGBA);
        }
        param->setLayoutHint(eLayoutHintNoNewLine);
        param->setDefault(gPluginOutputComponents.size() - 1);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessR);
        param->setLabel(kNatronOfxParamProcessRLabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessG);
        param->setLabel(kNatronOfxParamProcessGLabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessB);
        param->setLabel(kNatronOfxParamProcessBLabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    //////////Output file
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamFilename);
        param->setLabel(kParamFilenameLabel);
        param->setStringType(OFX::eStringTypeFilePath);
        param->setFilePathExists(false);
        param->setHint(kParamFilenameHint);
        // in the Writer context, the script name should be kOfxImageEffectFileParamName, for consistency with the reader nodes @see kOfxImageEffectContextReader
        param->setScriptName(kParamFilename);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    //////////// Output type
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormatType);
        param->setLabel(kParamFormatTypeLabel);
        param->appendOption("Input stream format","Renders using for format the input stream's format.");
        param->appendOption("Project format","Renders using the format of the current project");
        param->appendOption("Fixed format","Renders using for format the format indicated by the " kParamOutputFormatLabel " parameter.");
        param->setDefault(1);
        param->setAnimates(false);
        param->setHint(kParamFormatTypeHint);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    
    
    //////////// Output format
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputFormat);
        param->setLabel(kParamOutputFormatLabel);
        param->setAnimates(true);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setHint(kParamOutputFormatHint);
        assert(param->getNOptions() == eParamFormatPCVideo);
        param->appendOption(kParamFormatPCVideoLabel);
        assert(param->getNOptions() == eParamFormatNTSC);
        param->appendOption(kParamFormatNTSCLabel);
        assert(param->getNOptions() == eParamFormatPAL);
        param->appendOption(kParamFormatPALLabel);
        assert(param->getNOptions() == eParamFormatHD);
        param->appendOption(kParamFormatHDLabel);
        assert(param->getNOptions() == eParamFormatNTSC169);
        param->appendOption(kParamFormatNTSC169Label);
        assert(param->getNOptions() == eParamFormatPAL169);
        param->appendOption(kParamFormatPAL169Label);
        assert(param->getNOptions() == eParamFormat1kSuper35);
        param->appendOption(kParamFormat1kSuper35Label);
        assert(param->getNOptions() == eParamFormat1kCinemascope);
        param->appendOption(kParamFormat1kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat2kSuper35);
        param->appendOption(kParamFormat2kSuper35Label);
        assert(param->getNOptions() == eParamFormat2kCinemascope);
        param->appendOption(kParamFormat2kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat4kSuper35);
        param->appendOption(kParamFormat4kSuper35Label);
        assert(param->getNOptions() == eParamFormat4kCinemascope);
        param->appendOption(kParamFormat4kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormatSquare256);
        param->appendOption(kParamFormatSquare256Label);
        assert(param->getNOptions() == eParamFormatSquare512);
        param->appendOption(kParamFormatSquare512Label);
        assert(param->getNOptions() == eParamFormatSquare1k);
        param->appendOption(kParamFormatSquare1kLabel);
        assert(param->getNOptions() == eParamFormatSquare2k);
        param->appendOption(kParamFormatSquare2kLabel);
        param->setDefault(eParamFormatHD);
        if (page) {
            page->addChild(*param);
        }
    }
    
    {
        int w = 0, h = 0;
        double par = -1;
        getFormatResolution(eParamFormatHD, &w, &h, &par);
        assert(par != -1);
        {
            Int2DParamDescriptor* param = desc.defineInt2DParam(kParamFormatSize);
            param->setIsSecret(true);
            param->setDefault(w, h);
            if (page) {
                page->addChild(*param);
            }
        }
        
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFormatPar);
            param->setIsSecret(true);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0.5, 2.);
            param->setDefault(par);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    
    
    /////////// Clip to project
    if (supportsDisplayWindow) {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamClipToProject);
        param->setLabel(kParamClipToProjectLabel);
        param->setHint(kParamClipToProjectHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_IO_USING_OCIO
    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, inputSpaceNameDefault);
    GenericOCIO::describeInContextOutput(desc, context, page, outputSpaceNameDefault, kParamOutputSpaceLabel);
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kOCIOHelpButton);
        param->setLabel(kOCIOHelpButtonLabel);
        param->setHint(kOCIOHelpButtonHint);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
    
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamInputPremult);
        param->setLabel(kParamInputPremultLabel);
        param->setAnimates(true);
        param->setHint(kParamInputPremultHint);
        assert(param->getNOptions() == eImageOpaque);
        param->appendOption(premultString(eImageOpaque), kParamInputPremultOptionOpaqueHint);
        assert(param->getNOptions() == eImagePreMultiplied);
        param->appendOption(premultString(eImagePreMultiplied), kParamInputPremultOptionPreMultipliedHint);
        assert(param->getNOptions() == eImageUnPreMultiplied);
        param->appendOption(premultString(eImageUnPreMultiplied), kParamInputPremultOptionUnPreMultipliedHint);
        param->setDefault(eImagePreMultiplied); // images should be premultiplied in a compositing context
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
        param->setLabel(kParamClipInfoLabel);
        param->setHint(kParamClipInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ///////////Frame range choosal
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFrameRange);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->appendOption(kParamFrameRangeOptionUnion, kParamFrameRangeOptionUnionHint);
        param->appendOption(kParamFrameRangeOptionBounds, kParamFrameRangeOptionBoundsHint);
        param->appendOption(kParamFrameRangeOptionManual, kParamFrameRangeOptionManualHint);
        param->setAnimates(true);
        param->setDefault(1);
        if (page) {
            page->addChild(*param);
        }
    }

    /////////////First frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    ////////////Last frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamLastFrame);
        param->setLabel(kParamLastFrameLabel);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // sublabel
    if (gHostIsNatron) {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecret(true); // always secret
        param->setEnabled(false);
        param->setIsPersistant(true);
        param->setEvaluateOnChange(false);
        //param->setDefault();
        if (page) {
            page->addChild(*param);
        }
    }
    
    return page;
}

void
GenericWriterDescribeInContextEnd(OFX::ImageEffectDescriptor &/*desc*/, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor* /*page*/)
{
}

