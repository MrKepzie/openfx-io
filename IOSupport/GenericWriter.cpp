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
    _outputFormat->setIsSecret(true);
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

    std::string filename;
    getOutputFileNameAndExtension(args.time, filename);
    
    std::auto_ptr<const OFX::Image> srcImg(_inputClip->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (srcImg->getRenderScale().x != args.renderScale.x ||
        srcImg->getRenderScale().y != args.renderScale.y ||
        srcImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }


    const void* srcPixelData = NULL;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    getImageData(srcImg.get(), &srcPixelData, &bounds, &pixelComponents, &bitDepth, &srcRowBytes);
    int pixelComponentCount = srcImg->getPixelComponentCount();
    if (bitDepth != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    float pixelAspectRatio = srcImg->getPixelAspectRatio();
    
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
    
    bool renderWindowIsBounds = args.renderWindow.x1 == bounds.x1 &&
    args.renderWindow.y1 == bounds.y1 &&
    args.renderWindow.x2 == bounds.x2 &&
    args.renderWindow.y2 == bounds.y2;
    
    bool isOCIOIdentity = _ocio->isIdentity(args.time);

    // premultiplication/unpremultiplication is only useful for RGBA data
    bool noPremult = (pixelComponents != OFX::ePixelComponentRGBA) || (userPremult == OFX::eImageOpaque);

    if (renderWindowIsBounds &&
        isOCIOIdentity &&
        (noPremult || userPremult == pluginExpectedPremult)) {
        // Render window is of the same size as the input image and we don't need to apply colorspace conversion
        // or premultiplication operations.

        encode(filename, args.time, (const float*)srcPixelData, args.renderWindow, pixelAspectRatio, pixelComponents, srcRowBytes);
        // copy to dstImg if necessary
        if (_outputClip && _outputClip->isConnected()) {
            std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImage(args.time));
            if (!dstImg.get()) {
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            if (dstImg->getRenderScale().x != args.renderScale.x ||
                dstImg->getRenderScale().y != args.renderScale.y ||
                dstImg->getField() != args.fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            
            // copy the source image (the writer is a no-op)
            copyPixelData(args.renderWindow, srcPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes, dstImg.get());
        }
    } else {
        // generic case: some conversions are needed.

        // allocate
        int pixelBytes = pixelComponentCount * getComponentBytes(bitDepth);
        int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
        size_t memSize = (args.renderWindow.y2 - args.renderWindow.y1) * tmpRowBytes;
        OFX::ImageMemory mem(memSize,this);
        float *tmpPixelData = (float*)mem.lock();
        
        // Set to black and transparant so that outside the portion defined by the image there's nothing.
        if (!renderWindowIsBounds) {
            std::memset(tmpPixelData, 0, memSize);
        }
        
        // Clip the render window to the bounds of the source image.
        OfxRectI renderWindowClipped;
        intersect(args.renderWindow, bounds, &renderWindowClipped);
        
        if (isOCIOIdentity) {
            // bypass OCIO

            if (noPremult || userPremult == pluginExpectedPremult) {
                if (userPremult == OFX::eImageOpaque && (pixelComponents == OFX::ePixelComponentRGBA ||
                                                         pixelComponents == OFX::ePixelComponentAlpha)) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                             srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes,
                                             tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes,
                               tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
                }
            } else if (userPremult == OFX::eImagePreMultiplied) {
                assert(pluginExpectedPremult == OFX::eImageUnPreMultiplied);
                unPremultPixelData(args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount
                                   , bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
            } else {
                assert(userPremult == OFX::eImageUnPreMultiplied);
                assert(pluginExpectedPremult == OFX::eImagePreMultiplied);
                premultPixelData(args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount
                                 , bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
            }
        } else {
            assert(!isOCIOIdentity);
            // OCIO expects unpremultiplied input
            if (noPremult || userPremult == OFX::eImageUnPreMultiplied) {
                if (userPremult == OFX::eImageOpaque && (pixelComponents == OFX::ePixelComponentRGBA ||
                                                         pixelComponents == OFX::ePixelComponentAlpha)) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                               srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes,
                               tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes,
                               tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
                }
            } else {
                assert(userPremult == OFX::eImagePreMultiplied);
                unPremultPixelData(args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount
                                   , bitDepth, srcRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
            }
            // do the color-space conversion
            _ocio->apply(args.time, renderWindowClipped, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, tmpRowBytes);

            ///If needed, re-premult the image for the plugin to work correctly
            if (pluginExpectedPremult == OFX::eImagePreMultiplied && pixelComponents == OFX::ePixelComponentRGBA) {
                
                premultPixelData(args.renderWindow, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount
                                 , bitDepth, tmpRowBytes, tmpPixelData, args.renderWindow, pixelComponents, pixelComponentCount, bitDepth, tmpRowBytes);
            }
        }
        // write the image file
        encode(filename, args.time, tmpPixelData, args.renderWindow, pixelAspectRatio, pixelComponents, tmpRowBytes);
        // copy to dstImg if necessary
        if (_outputClip && _outputClip->isConnected()) {
            std::auto_ptr<OFX::Image> dstImg(_outputClip->fetchImage(args.time));
            if (!dstImg.get()) {
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }
            if (dstImg->getRenderScale().x != args.renderScale.x ||
                dstImg->getRenderScale().y != args.renderScale.y ||
                dstImg->getField() != args.fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
            }

            // copy the source image (the writer is a no-op)
            copyPixelData(args.renderWindow, srcPixelData, bounds, pixelComponents, pixelComponentCount, bitDepth, srcRowBytes, dstImg.get());
        }
        
        // mem is freed at destruction
    }
    
    clearPersistentMessage();
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
    getRegionOfDefinitionInternal(args.frameRange.min, rod);
    
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
GenericWriterPlugin::getRegionOfDefinitionInternal(OfxTime time,OfxRectD& rod)
{
    
    int formatType;
    _outputFormatType->getValue(formatType);
    
    if (formatType == 0) {
        // use the default RoD
        rod = _inputClip->getRegionOfDefinition(time);
    } else if (formatType == 1) {
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        rod.x1 = offset.x;
        rod.y1 = offset.y;
        rod.x2 = offset.x + size.x;
        rod.y2 = offset.y + size.y;
    } else {
        int formatIndex;
        _outputFormat->getValueAtTime(time, formatIndex);
        std::size_t w,h;
        double par;
        getFormatResolution((OFX::EParamFormat)formatIndex, &w, &h, &par);
        rod.x1 = rod.y1 = 0.;
        rod.x2 = w;
        rod.y2 = h;
    }
}

bool
GenericWriterPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }
    getRegionOfDefinitionInternal(args.time, rod);
    return true;
}

// override the roi call
void
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

        ///let the derive class a chance to initialize any data structure it may need
        onOutputFileChanged(filename);
    } else if (paramName == kParamFormatType) {
        int type;
        _outputFormatType->getValue(type);
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
        OFX::PixelComponentEnum components = _inputClip->getPixelComponents();
        assert((components == OFX::ePixelComponentAlpha && premult != OFX::eImageOpaque) ||
               (components == OFX::ePixelComponentRGB && premult == OFX::eImageOpaque) ||
               (components == OFX::ePixelComponentRGBA));
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
GenericWriterDescribe(OFX::ImageEffectDescriptor &desc,OFX::RenderSafetyEnum safety)
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
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
PageParamDescriptor*
GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault)
{
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
        
        page->addChild(*param);
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
        page->addChild(*param);
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
        page->addChild(*param);
    }

    // insert OCIO parameters
    GenericOCIO::describeInContextInput(desc, context, page, inputSpaceNameDefault);
    GenericOCIO::describeInContextOutput(desc, context, page, outputSpaceNameDefault, kParamOutputSpaceLabel);
    {
        OFX::PushButtonParamDescriptor* pb = desc.definePushButtonParam(kOCIOHelpButton);
        pb->setLabel(kOCIOHelpButtonLabel);
        pb->setHint(kOCIOHelpButtonHint);
        page->addChild(*pb);
    }

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
        page->addChild(*param);
    }

    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
        param->setLabel(kParamClipInfoLabel);
        param->setHint(kParamClipInfoHint);
        page->addChild(*param);
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
        page->addChild(*param);
    }

    /////////////First frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setAnimates(true);
        page->addChild(*param);
    }

    ////////////Last frame
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamLastFrame);
        param->setLabel(kParamLastFrameLabel);
        //param->setIsSecret(true); // done in the plugin constructor
        param->setAnimates(true);
        page->addChild(*param);
    }
    
    return page;
}

void
GenericWriterDescribeInContextEnd(OFX::ImageEffectDescriptor &/*desc*/, OFX::ContextEnum /*context*/, OFX::PageParamDescriptor* /*page*/)
{
}

