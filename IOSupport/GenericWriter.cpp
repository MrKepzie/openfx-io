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

#define kParamOutputFormat "outputFormat"
#define kParamOutputFormatLabel "Format"
#define kParamOutputFormatHint \
"The output format to render"

#define kParamFormatType "formatType"
#define kParamFormatTypeLabel "Format Type"
#define kParamFormatTypeHint \
"Whether to choose the input stream's format as output format or one from the drop-down menu"

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

static bool gHostIsNatron   = false;
static bool gHostIsMultiPlanar = false;
static bool gHostIsMultiView = false;

GenericWriterPlugin::GenericWriterPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, _inputClip(0)
, _outputClip(0)
, _fileParam(0)
, _frameRange(0)
, _firstFrame(0)
, _lastFrame(0)
, _outputFormatType(0)
, _outputFormat(0)
, _premult(0)
, _clipToProject(0)
, _sublabel(0)
, _ocio(new GenericOCIO(this))
{
    _inputClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    _outputClip = fetchClip(kOfxImageEffectOutputClipName);
    
    _fileParam = fetchStringParam(kParamFilename);
    _frameRange = fetchChoiceParam(kParamFrameRange);
    _firstFrame = fetchIntParam(kParamFirstFrame);
    _lastFrame = fetchIntParam(kParamLastFrame);
    
    _outputFormatType = fetchChoiceParam(kParamFormatType);
    _outputFormat = fetchChoiceParam(kParamOutputFormat);
    
    _premult = fetchChoiceParam(kParamInputPremult);


    ///Param does not necessarily exist for all IO plugins
    if (paramExists(kParamClipToProject)) {
        _clipToProject = fetchBooleanParam(kParamClipToProject);
    }

    if (gHostIsNatron) {
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_sublabel);
    }

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


void
GenericWriterPlugin::getOutputFileNameAndExtension(OfxTime time, std::string& filename)
{
    _fileParam->getValueAtTime(time,filename);
    // filename = filenameFromPattern(filename, time);
    
    ///find out whether we support this extension...
    size_t sepPos = filename.find_last_of('.');
    if (sepPos == std::string::npos){ //we reached the start of the file, return false because we can't determine from the extension
        setPersistentMessage(OFX::Message::eMessageError, "", "Invalid file name");
        return;
    }
    
    std::string ext;
    size_t i = sepPos;
    ++i;//< bypass the '.' character
	std::locale loc;
    while(i < filename.size()){
        ext.append(1,std::tolower(filename.at(i),loc));
        ++i;
    }
    
#ifdef OFX_EXTENSIONS_TUTTLE
    try {
        bool found = false;
        int nExtensions = getPropertySet().propGetDimension(kTuttleOfxImageEffectPropSupportedExtensions);
        for (int i = 0; i < nExtensions; ++i) {
            std::string exti = getPropertySet().propGetString(kTuttleOfxImageEffectPropSupportedExtensions, i);
            if (exti == ext) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::string err("Unsupported file extension: ");
            err.append(ext);
            setPersistentMessage(OFX::Message::eMessageError, "", ext);
        }
    } catch (OFX::Exception::PropertyUnknownToHost &e) {
        // ignore exception
    }
#endif
    
#if 0
    // [FD] disabled 5/04/2015 because it modifies the filename when writing to a file that ends with digits.
    // For example sequence_01.mov, will be changed to sequence_.mov, which is very dangerous.

    ////if the file extension corresponds to a video file, remove file digits that were
    ////added to the file path in order to write into the same file.
    if (!isImageFile(ext)) {
        ///find the position of the first digit
        size_t firstDigitPos = sepPos;
        --firstDigitPos;
		std::locale loc;
        while (firstDigitPos &&  std::isdigit(filename.at(firstDigitPos),loc)) {
            --firstDigitPos;
        }
        ++firstDigitPos;
        filename.erase(firstDigitPos, sepPos - firstDigitPos); //< erase the digits
    }
#endif
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

static int getPixelsComponentsCount(const OFX::Image* img,
                                    OFX::PixelComponentEnum* mappedComponents,
                                    bool* isColorPlane)
{
    std::string rawComponents = img->getPixelComponentsProperty();
    *mappedComponents = OFX::ePixelComponentNone;
    OFX::PixelComponentEnum pixelComponents = img->getPixelComponents();
    int pixelComponentsCount = 0;
    
    if (pixelComponents != OFX::ePixelComponentCustom) {
        *isColorPlane = true;
        *mappedComponents = pixelComponents;
        switch (pixelComponents) {
            case OFX::ePixelComponentAlpha:
                pixelComponentsCount = 1;
                break;
            case OFX::ePixelComponentXY:
                pixelComponentsCount = 2;
                break;
            case OFX::ePixelComponentRGB:
                pixelComponentsCount = 3;
                break;
            case OFX::ePixelComponentRGBA:
                pixelComponentsCount = 4;
                break;
            default:
                OFX::throwSuiteStatusException(kOfxStatErrFormat);
                return pixelComponentsCount;
                break;
        }
    } else {
        
        *isColorPlane = false;
        std::vector<std::string> channelNames = OFX::mapPixelComponentCustomToLayerChannels(rawComponents);
        pixelComponentsCount = (int)channelNames.size() - 1;
        
        //Remap pixelComponents to something known by encode() and copyPixelData/unpremult/premult
        switch (pixelComponentsCount) {
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
                OFX::throwSuiteStatusException(kOfxStatErrFormat);
                return pixelComponentsCount;
                break;
        }
    }
    return pixelComponentsCount;
}

void
GenericWriterPlugin::fetchPlaneConvertAndCopy(const std::string& plane,
                                          int view,
                                          int renderRequestedView,
                                          double time,
                                          const OfxRectI& renderWindow,
                                          const OfxPointD& renderScale,
                                          OFX::FieldEnum fieldToRender,
                                          OFX::PreMultiplicationEnum pluginExpectedPremult,
                                          OFX::PreMultiplicationEnum userPremult,
                                          bool isOCIOIdentity,
                                          InputImagesHolder* srcImgsHolder,
                                          OfxRectI* bounds,
                                          OFX::ImageMemory** tmpMem,
                                          const OFX::Image** inputImage,
                                          float** tmpMemPtr,
                                          int* rowBytes,
                                          OFX::PixelComponentEnum* mappedComponents)
{
    *inputImage = 0;
    *tmpMem = 0;
    *tmpMemPtr = 0;
    
    const void* srcPixelData = 0;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    
    const OFX::Image* srcImg = _inputClip->fetchImagePlane(time, view, plane.c_str());
    *inputImage = srcImg;
    if (!srcImg) {
        setPersistentMessage(OFX::Message::eMessageError, "", "Input image could not be fetched");
        OFX::throwSuiteStatusException(kOfxStatFailed);
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
    
    bool isColorPlane;
    int pixelComponentsCount = getPixelsComponentsCount(srcImg,mappedComponents,&isColorPlane);
    assert(pixelComponentsCount != 0 && *mappedComponents != OFX::ePixelComponentNone);

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
            copyPixelData(renderWindow, srcPixelData, renderWindow, pixelComponents, pixelComponentsCount, bitDepth, srcRowBytes, dstImg.get());
            
        }
    } else {
        // generic case: some conversions are needed.
        
        // allocate
        int pixelBytes = pixelComponentsCount * getComponentBytes(bitDepth);
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
        intersect(renderWindow, *bounds, &renderWindowClipped);
        
        if (isOCIOIdentity) {
            // bypass OCIO
            
            if (noPremult || userPremult == pluginExpectedPremult) {
                if (userPremult == OFX::eImageOpaque && (*mappedComponents == OFX::ePixelComponentRGBA ||
                                                         *mappedComponents == OFX::ePixelComponentAlpha)) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                     srcPixelData, *bounds, *mappedComponents, pixelComponentsCount, bitDepth, srcRowBytes,
                                     tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, *bounds, *mappedComponents, pixelComponentsCount, bitDepth, srcRowBytes,
                               tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
                }
            } else if (userPremult == OFX::eImagePreMultiplied) {
                assert(pluginExpectedPremult == OFX::eImageUnPreMultiplied);
                unPremultPixelData(renderWindow, srcPixelData, *bounds, *mappedComponents, pixelComponentsCount
                                   , bitDepth, srcRowBytes, tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
            } else {
                assert(userPremult == OFX::eImageUnPreMultiplied);
                assert(pluginExpectedPremult == OFX::eImagePreMultiplied);
                premultPixelData(renderWindow, srcPixelData, *bounds, *mappedComponents, pixelComponentsCount
                                 , bitDepth, srcRowBytes, tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
            }
        } else {
            assert(!isOCIOIdentity);
            // OCIO expects unpremultiplied input
            if (noPremult || userPremult == OFX::eImageUnPreMultiplied) {
                if (userPremult == OFX::eImageOpaque && (*mappedComponents == OFX::ePixelComponentRGBA ||
                                                         *mappedComponents == OFX::ePixelComponentAlpha)) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                     srcPixelData, *bounds, *mappedComponents, pixelComponentsCount, bitDepth, srcRowBytes,
                                     tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, *bounds, *mappedComponents, pixelComponentsCount, bitDepth, srcRowBytes,
                               tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
                }
            } else {
                assert(userPremult == OFX::eImagePreMultiplied);
                unPremultPixelData(renderWindow, srcPixelData, *bounds, *mappedComponents, pixelComponentsCount
                                   , bitDepth, srcRowBytes, tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
            }
            // do the color-space conversion
            if (*mappedComponents == OFX::ePixelComponentRGB || *mappedComponents == OFX::ePixelComponentRGBA) {
                _ocio->apply(time, renderWindowClipped, tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, tmpRowBytes);
            }
            
            ///If needed, re-premult the image for the plugin to work correctly
            if (pluginExpectedPremult == OFX::eImagePreMultiplied && *mappedComponents == OFX::ePixelComponentRGBA) {
                
                premultPixelData(renderWindow, tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount
                                 , bitDepth, tmpRowBytes, tmpPixelData, renderWindow, *mappedComponents, pixelComponentsCount, bitDepth, tmpRowBytes);
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
            copyPixelData(renderWindow, srcPixelData, *bounds, pixelComponents, pixelComponentsCount, bitDepth, srcRowBytes, dstImg.get());
            
        }
        *bounds = renderWindow;

    } // if (renderWindowIsBounds && isOCIOIdentity && (noPremult || userPremult == pluginExpectedPremult))
    
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
GenericWriterPlugin::render(const OFX::RenderArguments &args)
{
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
    getOutputFileNameAndExtension(args.time, filename);
    
    float pixelAspectRatio = _inputClip->getPixelAspectRatio();
    
    ///This is automatically the same generally as inputClip premultiplication but can differ is the user changed it.
    int userPremult_i;
    _premult->getValueAtTime(args.time, userPremult_i);
    OFX::PreMultiplicationEnum userPremult = (OFX::PreMultiplicationEnum)userPremult_i;

    ///This is what the plug-in expects to be passed to the encode function.
    OFX::PreMultiplicationEnum pluginExpectedPremult = getExpectedInputPremultiplication();

    
    
    // The following (commented out) code is not fully-safe, because the same instance may be have
    // two threads running on the same area of the same frame, and the apply()
    // calls both read and write dstImg.
    // This results in colorspace conversion being applied several times.
    //
    //if (dstImg.get()) {
    //// do the color-space conversion on dstImg
    //getImageData(dstImg.get(), &pixelData, &bounds, &pixelComponents, &rowBytes);
    //_ocio->apply(args.time, args.renderWindow, pixelData, bounds, pixelComponents, rowBytes);
    //encode(filename, args.time, pixelData, bounds, pixelComponents, rowBytes);
    //}
    //
    // The only viable solution (below) is to do the conversion in a temporary space,
    // and finally copy the result.
    //
    
   
    bool isOCIOIdentity = _ocio->isIdentity(args.time);

    //The host required that we render all views into 1 file. This is for now only supported by EXR.
    
    bool doDefaultView = false;
    std::map<int,std::string> viewNames;
    
    if (args.renderView == -1) {
        int viewToRender = getViewToRender();
        if (viewToRender != -1) {
            setPersistentMessage(OFX::Message::eMessageError, "", "Inconsistent view to render requested");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
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
        
        if (viewToRender == -1) {
            /*
             We might be in this situation if the user requested %V or %v in the filename, so the host didn't request -1 as render view.
             We might also be here if the host never requests -1 as render view
             Just fallback to the default view
             */
            doDefaultView = true;
        } else if (viewToRender == -2) {
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
        fetchPlaneConvertAndCopy(args.planes.front(), viewIndex, args.renderView, args.time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents);
        
        encode(filename, args.time, viewNames[0], data.srcPixelData, args.renderWindow, pixelAspectRatio, data.pixelComponents, data.rowBytes);
    } else {
        /*
         Use the beginEncodeParts/encodePart/endEncodeParts API when there are multiple views/planes to render
         Note that the number of times that we call encodePart depends on the LayerViewsPartsEnum value
         */
        assert(gHostIsMultiPlanar);
        EncodePlanesLocalData_RAII encodeData(this);
        InputImagesHolder dataHolder;
        
        beginEncodeParts(encodeData.getData(), filename, args.time, pixelAspectRatio, partsSplit, viewNames, args.planes, args.renderWindow);
        
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
                for (std::map<int,std::string>::const_iterator view = viewNames.begin(); view!=viewNames.end(); ++view) {
                    for (std::list<std::string>::const_iterator plane = args.planes.begin(); plane != args.planes.end(); ++plane) {
                        OFX::ImageMemory *tmpMem;
                        const OFX::Image* srcImg;
                        
                        ImageData data;
                        fetchPlaneConvertAndCopy(*plane, view->first, args.renderView, args.time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents);
                        
                        bool isColorPlane;
                        data.pixelComponentsCount = getPixelsComponentsCount(srcImg,&data.pixelComponents,&isColorPlane);
                        assert(data.pixelComponentsCount != 0 && data.pixelComponents != OFX::ePixelComponentNone);
                        
                        planesData.push_back(data);
                        
                        nChannels += data.pixelComponentsCount;
                    }// for each plane
                } // for each view
                
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
                    if (OFX::Coords::rectIntersection(args.renderWindow, it->bounds, &intersection)) {
                        interleavePixelBuffers(intersection, it->srcPixelData, it->bounds, it->pixelComponents, it->pixelComponentsCount, OFX::eBitDepthFloat, it->rowBytes, args.renderWindow, interleaveIndex, nChannels, tmpRowBytes, tmpMemPtr);
                    }
                    interleaveIndex += it->pixelComponentsCount;
                }
                
                encodePart(encodeData.getData(), filename, tmpMemPtr, 0, tmpRowBytes);
                
            } break;
            case eLayerViewsSplitViews: {
              /*
               Write each view into a single part but aggregate all layers for each view
               */
                int partIndex = 0;
                for (std::map<int,std::string>::const_iterator view = viewNames.begin(); view!=viewNames.end(); ++view) {
                    int nChannels = 0;
                    InputImagesHolder dataHolder;
                    
                    std::list<ImageData> planesData;
                    for (std::list<std::string>::const_iterator plane = args.planes.begin(); plane != args.planes.end(); ++plane) {
                        
                        OFX::ImageMemory *tmpMem;
                        const OFX::Image* srcImg;
                        
                        ImageData data;
                        fetchPlaneConvertAndCopy(*plane, view->first, args.renderView, args.time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents);
                        
                        bool isColorPlane;
                        data.pixelComponentsCount = getPixelsComponentsCount(srcImg,&data.pixelComponents,&isColorPlane);
                        assert(data.pixelComponentsCount != 0 && data.pixelComponents != OFX::ePixelComponentNone);
                        
                        planesData.push_back(data);
                        
                        nChannels += data.pixelComponentsCount;

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
                        if (OFX::Coords::rectIntersection(args.renderWindow, it->bounds, &intersection)) {
                            interleavePixelBuffers(intersection, it->srcPixelData, it->bounds,
                                                   it->pixelComponents, it->pixelComponentsCount,
                                                   OFX::eBitDepthFloat, it->rowBytes, args.renderWindow,
                                                   interleaveIndex, nChannels, tmpRowBytes, tmpMemPtr);
                        }
                        interleaveIndex += it->pixelComponentsCount;
                    }
                    
                    encodePart(encodeData.getData(), filename, tmpMemPtr, partIndex, tmpRowBytes);
                    
                    ++partIndex;
                } // for each view
            } break;
            case eLayerViewsSplitViewsLayers: {
              /*
               Write each layer of each view in an independent part
               */
                int partIndex = 0;
                for (std::map<int,std::string>::const_iterator view = viewNames.begin(); view!=viewNames.end(); ++view) {
                    for (std::list<std::string>::const_iterator plane = args.planes.begin(); plane != args.planes.end(); ++plane) {
                        
                        InputImagesHolder dataHolder;
                        OFX::ImageMemory *tmpMem;
                        const OFX::Image* srcImg;
                        ImageData data;
                        fetchPlaneConvertAndCopy(*plane, view->first, args.renderView, args.time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents);
                        encodePart(encodeData.getData(), filename, data.srcPixelData, partIndex, data.rowBytes);
                        
                        ++partIndex;
                    } // for each plane
                } // for each view
            } break;
        };
        
        endEncodeParts(encodeData.getData());
    }
    
    clearPersistentMessage();
}

class InterleaveProcessorBase: public OFX::PixelProcessorFilterBase
{
protected:
    
    int _dstStartIndex;

public:
    InterleaveProcessorBase(OFX::ImageEffect& instance)
    : OFX::PixelProcessorFilterBase(instance)
    , _dstStartIndex(-1)
    {
    }
    
    void setDstPixelComponentStartIndex(int dstStartIndex)
    {
        _dstStartIndex = dstStartIndex;
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
                memcpy(dstPix, srcPix, _srcPixelBytes);
                
            }
        }

    }

};

template <typename PIX,int maxValue>
void interleavePixelBuffersForDepth(OFX::ImageEffect* instance,
                                    const OfxRectI& renderWindow,
                                    const PIX *srcPixelData,
                                    const OfxRectI& bounds,
                                    OFX::PixelComponentEnum srcPixelComponents,
                                    int srcPixelComponentCount,
                                    OFX::BitDepthEnum bitDepth,
                                    int srcRowBytes,
                                    const OfxRectI& dstBounds,
                                    int dstPixelComponentStartIndex,
                                    int dstPixelComponentCount,
                                    int dstRowBytes,
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
    p->setDstPixelComponentStartIndex(dstPixelComponentStartIndex);
    
    p->process();
}


void
GenericWriterPlugin::interleavePixelBuffers(const OfxRectI& renderWindow,
                                            const void *srcPixelData,
                                            const OfxRectI& bounds,
                                            OFX::PixelComponentEnum srcPixelComponents,
                                            int srcPixelComponentCount,
                                            OFX::BitDepthEnum bitDepth,
                                            int srcRowBytes,
                                            const OfxRectI& dstBounds,
                                            int dstPixelComponentStartIndex,
                                            int dstPixelComponentCount,
                                            int dstRowBytes,
                                            void* dstPixelData)
{
    assert(renderWindow.x1 >= bounds.x1 && renderWindow.x2 <= bounds.x2 &&
           renderWindow.y1 >= bounds.y1 && renderWindow.y2 <= bounds.y2);
    assert(renderWindow.x1 >= dstBounds.x1 && renderWindow.x2 <= dstBounds.x2 &&
           renderWindow.y1 >= dstBounds.y1 && renderWindow.y2 <= dstBounds.y2);
    switch (bitDepth) {
        case OFX::eBitDepthFloat:
            interleavePixelBuffersForDepth<float, 1>(this, renderWindow, (const float*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, bitDepth,srcRowBytes, dstBounds, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (float*)dstPixelData);
            break;
        case OFX::eBitDepthUByte:
            interleavePixelBuffersForDepth<unsigned char, 255>(this, renderWindow, (const unsigned char*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, bitDepth, srcRowBytes, dstBounds, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (unsigned char*)dstPixelData);
            break;
        case OFX::eBitDepthUShort:
            interleavePixelBuffersForDepth<unsigned short, 65535>(this, renderWindow, (const unsigned short*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, bitDepth, srcRowBytes, dstBounds, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (unsigned short*)dstPixelData);
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
    getOutputFileNameAndExtension(args.frameRange.min, filename);
    
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
        int formatIndex;
        _outputFormat->getValueAtTime(time, formatIndex);
        std::size_t w,h;
        double par;
        getFormatResolution((OFX::EParamFormat)formatIndex, &w, &h, &par);
        rod.x1 = rod.y1 = 0.;
        rod.x2 = w;
        rod.y2 = h;
    }
    if (doDefaultBehaviour) {
        // union RoD across all views
        int viewsToRender = getViewToRender();
        if (viewsToRender == -2 || !gHostIsMultiView) {
            rod = _inputClip->getRegionOfDefinition(time);
        } else {
            if (viewsToRender == -1) {
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

// override the roi call
/*void
GenericWriterPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                       OFX::RegionOfInterestSetter &rois)
{
    if (!kSupportsTiles) {
        // The effect requires full images to render any region
        OfxRectD srcRoI;

        if (_inputClip && _inputClip->isConnected()) {
            srcRoI = _inputClip->getRegionOfDefinition(args.time);
            rois.setRegionOfInterest(*_inputClip, srcRoI);
        }
    }
}
*/

void
GenericWriterPlugin::encode(const std::string& /*filename*/,
                            OfxTime /*time*/,
                            const std::string& /*viewName*/,
                            const float */*pixelData*/,
                            const OfxRectI& /*bounds*/,
                            float /*pixelAspectRatio*/,
                            OFX::PixelComponentEnum /*pixelComponents*/,
                            int /*rowBytes*/)
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
                                       const OfxRectI& /*bounds*/)
{
     /// Does nothing
}


void
GenericWriterPlugin::encodePart(void* /*user_data*/, const std::string& /*filename*/, const float */*pixelData*/, int /*planeIndex*/, int /*rowBytes*/)
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
        if (_sublabel && args.reason != OFX::eChangePluginEdit) {
            _sublabel->setValue(basename(filename));
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
                   (components == OFX::ePixelComponentCustom && gHostIsMultiPlanar));
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
   
    clipPreferences.setOutputPremultiplication(getExpectedInputPremultiplication());
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
        if (viewsToRender == -1) {
            int nViews = getViewCount();
            for (int i = 0; i < nViews; ++i) {
                frameViews.addFrameViewsNeeded(*_inputClip, r, i);
            }
        } else {
            if (viewsToRender == -2) {
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
GenericWriterDescribe(OFX::ImageEffectDescriptor &desc,OFX::RenderSafetyEnum safety,bool isMultiPlanar, bool isMultiView)
{
    desc.setPluginGrouping(kPluginGrouping);
    
#ifdef OFX_EXTENSIONS_TUTTLE
    desc.addSupportedContext(OFX::eContextWriter);
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
    if (isMultiView && OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2)) {
        gHostIsMultiView = true;
        desc.setIsViewAware(true);
        desc.setIsViewInvariant(OFX::eViewInvarianceAllViewsVariant);
    }
#endif
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
PageParamDescriptor*
GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault, bool supportsDisplayWindow)
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
    dstClip->setSupportsTiles(kSupportsTiles);//< we don't support tiles in output!

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    //////////Output file
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamFilename);
        param->setLabel(kParamFilenameLabel);
        param->setStringType(OFX::eStringTypeFilePath);
        param->setFilePathExists(false);
        param->setHint(kParamFilenameHint);
        // in the Writer context, the script name should be kOfxImageEffectFileParamName, for consistency with the reader nodes @see kOfxImageEffectContextReader
        param->setScriptName(kParamFilename);
        param->setAnimates(!isVideoStreamPlugin);
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
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
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
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
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
        param->setDefault(0);
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

