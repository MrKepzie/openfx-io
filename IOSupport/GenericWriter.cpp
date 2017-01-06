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
 * OFX GenericWriter plugin.
 * A base class for all OpenFX-based encoders.
 */

#include "GenericWriter.h"

#include <cfloat> // DBL_MAX
#include <cstring>
#include <locale>
#include <sstream>
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

using std::string;
using std::stringstream;
using std::vector;
using std::map;

NAMESPACE_OFX_ENTER
NAMESPACE_OFX_IO_ENTER

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
    "Determines which rectangle of pixels will be written in output."
#define kParamFormatTypeOptionInput "Input Format"
#define kParamFormatTypeOptionInputHint "Renders the pixels included in the input format"
#define kParamFormatTypeOptionProject "Project Format"
#define kParamFormatTypeOptionProjectHint "Renders the pixels included in the project format"
#define kParamFormatTypeOptionFixed "Fixed Format"
#define kParamFormatTypeOptionFixedHint "Renders the pixels included in the format indicated by the " kParamOutputFormatLabel " parameter."
enum FormatTypeEnum
{
    eFormatTypeInput = 0,
    eFormatTypeProject,
    eFormatTypeFixed,
};

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
    "Input is considered to have this premultiplication state.\n" \
    "If it is Premultiplied, red, green and blue channels are divided by the alpha channel " \
    "before applying the colorspace conversion.\n" \
    "This is set automatically from the input stream information, but can be adjusted if this information is wrong."
#define kParamInputPremultOptionOpaqueHint "The image is opaque and so has no premultiplication state, as if the alpha component in all pixels were set to the white point."
#define kParamInputPremultOptionPreMultipliedHint "The image is premultiplied by its alpha (also called \"associated alpha\")."
#define kParamInputPremultOptionUnPreMultipliedHint "The image is unpremultiplied (also called \"unassociated alpha\")."

#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

#define kParamOutputSpaceLabel "File Colorspace"

#define kParamClipToRoD "clipToRoD"
#define kParamClipToRoDLabel "Clip To RoD"
#define kParamClipToRoDHint "When checked, the portion of the image written will be the region of definition of the image in input and not the format " \
    "selected by the Output Format parameter.\n" \
    "For the EXR file format, this will distinguish the data window (size of the image in input) from the display window (the format specified by Output Format)."

#define kParamProcessHint  "When checked, this channel of the layer will be written to the file otherwise it will be skipped. Most file formats will " \
    "pack the channels into the first N channels of the file. If for some reason it's not possible, the channel will be filled with 0."

#define kParamOutputComponents "outputComponents"
#define kParamOutputComponentsLabel "Output Components"
#define kParamOutputComponentsHint "Map the input layer to this type of components before writing it to the output file."

#define kParamGuessedParams "ParamExistingInstance" // was guessParamsFromFilename already successfully called once on this instance

#ifdef OFX_IO_USING_OCIO
#define kParamOutputSpaceSet "ocioOutputSpaceSet" // was the output colorspace set by user?
#endif

static bool gHostIsNatron   = false;
static bool gHostIsMultiPlanar = false;
static bool gHostIsMultiView = false;


GenericWriterPlugin::GenericWriterPlugin(OfxImageEffectHandle handle,
                                         const vector<string>& extensions,
                                         bool supportsRGBA,
                                         bool supportsRGB,
                                         bool supportsXY,
                                         bool supportsAlpha)
    : MultiPlane::MultiPlaneEffect(handle)
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
    , _clipToRoD(0)
    , _sublabel(0)
    , _processChannels()
    , _outputComponents(0)
    , _guessedParams(0)
#ifdef OFX_IO_USING_OCIO
    , _outputSpaceSet(NULL)
    , _ocio( new GenericOCIO(this) )
#endif
    , _extensions(extensions)
    , _supportsRGBA(supportsRGBA)
    , _supportsRGB(supportsRGB)
    , _supportsXY(supportsXY)
    , _supportsAlpha(supportsAlpha)
    , _outputComponentsTable()
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
    if ( paramExists(kParamClipToRoD) ) {
        _clipToRoD = fetchBooleanParam(kParamClipToRoD);
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

    _guessedParams = fetchBooleanParam(kParamGuessedParams);

#ifdef OFX_IO_USING_OCIO
    _outputSpaceSet = fetchBooleanParam(kParamOutputSpaceSet);
#endif

    int frameRangeChoice;
    _frameRange->getValue(frameRangeChoice);
    double first, last;
    timeLineGetBounds(first, last);
    if (frameRangeChoice == 2) {
        _firstFrame->setIsSecretAndDisabled(false);
        _lastFrame->setIsSecretAndDisabled(false);
    } else {
        _firstFrame->setIsSecretAndDisabled(true);
        _lastFrame->setIsSecretAndDisabled(true);
    }
    FormatTypeEnum outputFormat = (FormatTypeEnum)_outputFormatType->getValue();
    if (_clipToRoD) {
        string filename;
        _fileParam->getValue(filename);
        _clipToRoD->setIsSecretAndDisabled( outputFormat != eFormatTypeProject || !displayWindowSupportedByFormat(filename) );
    }
    if ( (outputFormat == eFormatTypeInput) || (outputFormat == eFormatTypeProject) ) {
        _outputFormat->setIsSecretAndDisabled(true);
    } else {
        _outputFormat->setIsSecretAndDisabled(false);
    }


    // must be in sync with GenericWriterDescribeInContextBegin
    if (supportsAlpha) {
        _outputComponentsTable.push_back(ePixelComponentAlpha);
    }
    if (supportsRGB) {
        _outputComponentsTable.push_back(ePixelComponentRGB);
    }
    if (supportsRGBA) {
        _outputComponentsTable.push_back(ePixelComponentRGBA);
    }
}

GenericWriterPlugin::~GenericWriterPlugin()
{
}

/**
 * @brief Restore any state from the parameters set
 * Called from createInstance() and changedParam() (via outputFileChanged()), must restore the
 * state of the Reader, such as Choice param options, data members and non-persistent param values.
 * We don't do this in the ctor of the plug-in since we can't call virtuals yet.
 * Any derived implementation must call GenericWriterPlugin::restoreStateFromParams() first
 **/
void
GenericWriterPlugin::restoreStateFromParams()
{
    // Natron explicitly set the value of filename before instanciating a Writer.
    // We need to know if all parameters were setup already and we are just loading a project
    // or if we are creating a new Writer from scratch and need toa djust parameters.
    bool writerExisted = _guessedParams->getValue();

    outputFileChanged(eChangePluginEdit, writerExisted, false);
    if (!writerExisted) {
        _guessedParams->setValue(true);
    }
}

bool
GenericWriterPlugin::checkExtension(const string& ext)
{
    if ( ext.empty() ) {
        // no extension
        return false;
    }

    return std::find(_extensions.begin(), _extensions.end(), ext) != _extensions.end();
}

bool
GenericWriterPlugin::isIdentity(const IsIdentityArguments &args,
                                Clip * & /*identityClip*/,
                                double & /*identityTime*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();

    return false;
}

GenericWriterPlugin::InputImagesHolder::InputImagesHolder()
{
}

void
GenericWriterPlugin::InputImagesHolder::addImage(const Image* img)
{
    _imgs.push_back(img);
}

void
GenericWriterPlugin::InputImagesHolder::addMemory(ImageMemory* mem)
{
    _mems.push_back(mem);
}

GenericWriterPlugin::InputImagesHolder::~InputImagesHolder()
{
    for (std::list<const Image*>::iterator it = _imgs.begin(); it != _imgs.end(); ++it) {
        delete *it;
    }
    for (std::list<ImageMemory*>::iterator it = _mems.begin(); it != _mems.end(); ++it) {
        delete *it;
    }
}

static int
getPixelsComponentsCount(const string& rawComponents,
                         PixelComponentEnum* mappedComponents)
{
    string layer, pairedLayer;

    vector<string> channels;
    MultiPlane::Utils::extractChannelsFromComponentString(rawComponents, &layer, &pairedLayer, &channels);
    switch ( channels.size() ) {
    case 0:
        *mappedComponents = ePixelComponentNone;
        break;
    case 1:
        *mappedComponents = ePixelComponentAlpha;
        break;
    case 2:
        *mappedComponents = ePixelComponentXY;
        break;
    case 3:
        *mappedComponents = ePixelComponentRGB;
        break;
    case 4:
        *mappedComponents = ePixelComponentRGBA;
        break;
    default:
        assert(false);
        break;
    }

    return (int)channels.size();
}

void
GenericWriterPlugin::fetchPlaneConvertAndCopy(const string& plane,
                                              bool failIfNoSrcImg,
                                              int view,
                                              int renderRequestedView,
                                              double time,
                                              const OfxRectI& renderWindow,
                                              const OfxPointD& renderScale,
                                              FieldEnum fieldToRender,
                                              PreMultiplicationEnum pluginExpectedPremult,
                                              PreMultiplicationEnum userPremult,
                                              const bool isOCIOIdentity,
                                              const bool doAnyPacking,
                                              const bool packingContiguous,
                                              const vector<int>& packingMapping,
                                              InputImagesHolder* srcImgsHolder, // must be deleted by caller
                                              OfxRectI* bounds,
                                              ImageMemory** tmpMem, // owned by srcImgsHolder
                                              const Image** inputImage, // owned by srcImgsHolder
                                              float** tmpMemPtr, // owned by srcImgsHolder
                                              int* rowBytes,
                                              PixelComponentEnum* mappedComponents,
                                              int* mappedComponentsCount)
{
    *inputImage = 0;
    *tmpMem = 0;
    *tmpMemPtr = 0;
    *mappedComponentsCount = 0;

    const void* srcPixelData = 0;
    PixelComponentEnum pixelComponents;
    BitDepthEnum bitDepth;
    int srcRowBytes;
    const Image* srcImg = _inputClip->fetchImagePlane( time, view, plane.c_str() );
    *inputImage = srcImg;
    if (!srcImg) {
        if (failIfNoSrcImg) {
            stringstream ss;
            ss << "Input layer ";
            string layerName, pairedLayer;
            vector<string> channels;
            MultiPlane::Utils::extractChannelsFromComponentString(plane, &layerName, &pairedLayer, &channels);
            ss << layerName;
            ss << " could not be fetched";

            setPersistentMessage( Message::eMessageError, "", ss.str() );
            throwSuiteStatusException(kOfxStatFailed);
        }

        return;
    } else {
        ///Add it to the holder so we are sure it gets released if an exception occurs below
        srcImgsHolder->addImage(srcImg);
    }

    if ( (srcImg->getRenderScale().x != renderScale.x) ||
         ( srcImg->getRenderScale().y != renderScale.y) ||
         ( srcImg->getField() != fieldToRender) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    getImageData(srcImg, &srcPixelData, bounds, &pixelComponents, &bitDepth, &srcRowBytes);

    if (bitDepth != eBitDepthFloat) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }


    // premultiplication/unpremultiplication is only useful for RGBA data
    bool noPremult = (pixelComponents != ePixelComponentRGBA) || (userPremult == eImageOpaque);
    PixelComponentEnum srcMappedComponents;
    const int srcMappedComponentsCount = getPixelsComponentsCount(srcImg->getPixelComponentsProperty(), &srcMappedComponents);

    *mappedComponents = srcMappedComponents;
    *mappedComponentsCount = srcMappedComponentsCount;
    assert(srcMappedComponentsCount != 0 && srcMappedComponents != ePixelComponentNone);

    bool renderWindowIsBounds = renderWindow.x1 == bounds->x1 &&
                                renderWindow.y1 == bounds->y1 &&
                                renderWindow.x2 == bounds->x2 &&
                                renderWindow.y2 == bounds->y2;


    if ( renderWindowIsBounds &&
         isOCIOIdentity &&
         ( noPremult || ( userPremult == pluginExpectedPremult) ) ) {
        // Render window is of the same size as the input image and we don't need to apply colorspace conversion
        // or premultiplication operations.

        *tmpMemPtr = (float*)srcPixelData;
        *rowBytes = srcRowBytes;

        // copy to dstImg if necessary
        if ( (renderRequestedView == view) && _outputClip && _outputClip->isConnected() ) {
            std::auto_ptr<Image> dstImg( _outputClip->fetchImagePlane( time, renderRequestedView, plane.c_str() ) );
            if ( !dstImg.get() ) {
                setPersistentMessage(Message::eMessageError, "", "Output image could not be fetched");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            if ( (dstImg->getRenderScale().x != renderScale.x) ||
                 ( dstImg->getRenderScale().y != renderScale.y) ||
                 ( dstImg->getField() != fieldToRender) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            if (dstImg->getPixelComponents() != pixelComponents) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong components");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }

            // copy the source image (the writer is a no-op)
            copyPixelData( renderWindow,
                           srcPixelData,
                           renderWindow,
                           pixelComponents /* could also be srcMappedComponents */,
                           srcMappedComponentsCount,
                           bitDepth,
                           srcRowBytes,
                           dstImg.get() );
        }
    } else {
        // generic case: some conversions are needed.

        // allocate
        int pixelBytes = srcMappedComponentsCount * getComponentBytes(bitDepth);
        int tmpRowBytes = (renderWindow.x2 - renderWindow.x1) * pixelBytes;
        *rowBytes = tmpRowBytes;
        size_t memSize = (size_t)(renderWindow.y2 - renderWindow.y1) * (size_t)tmpRowBytes;
        *tmpMem = new ImageMemory(memSize, this);
        srcImgsHolder->addMemory(*tmpMem);
        *tmpMemPtr = (float*)(*tmpMem)->lock();
        if (!*tmpMemPtr) {
            throwSuiteStatusException(kOfxStatErrMemory);

            return;
        }

        float* tmpPixelData = *tmpMemPtr;

        // Set to black and transparant so that outside the portion defined by the image there's nothing.
        if (!renderWindowIsBounds) {
            std::memset(tmpPixelData, 0, memSize);
        }

        // Clip the render window to the bounds of the source image.
        OfxRectI renderWindowClipped;
        if ( !intersect(renderWindow, *bounds, &renderWindowClipped) ) {
            // Nothing to do, exit
            return;
        }

        if (isOCIOIdentity) {
            // bypass OCIO

            if ( noPremult || (userPremult == pluginExpectedPremult) ) {
                if ( (userPremult == eImageOpaque) && ( (srcMappedComponents == ePixelComponentRGBA) ||
                                                        ( srcMappedComponents == ePixelComponentAlpha) ) ) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                     srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount, bitDepth, srcRowBytes,
                                     tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount, bitDepth, srcRowBytes,
                               tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
                }
            } else if (userPremult == eImagePreMultiplied) {
                assert(pluginExpectedPremult == eImageUnPreMultiplied);
                unPremultPixelData(renderWindow, srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount
                                   , bitDepth, srcRowBytes, tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
            } else {
                assert(userPremult == eImageUnPreMultiplied);
                assert(pluginExpectedPremult == eImagePreMultiplied);
                premultPixelData(renderWindow, srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount
                                 , bitDepth, srcRowBytes, tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
            }
        } else {
            assert(!isOCIOIdentity);
            // OCIO expects unpremultiplied input
            if ( noPremult || (userPremult == eImageUnPreMultiplied) ) {
                if ( (userPremult == eImageOpaque) && ( (srcMappedComponents == ePixelComponentRGBA) ||
                                                        ( srcMappedComponents == ePixelComponentAlpha) ) ) {
                    // Opaque: force the alpha channel to 1
                    copyPixelsOpaque(*this, renderWindowClipped,
                                     srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount, bitDepth, srcRowBytes,
                                     tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
                } else {
                    // copy the whole raw src image
                    copyPixels(*this, renderWindowClipped,
                               srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount, bitDepth, srcRowBytes,
                               tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
                }
            } else {
                assert(userPremult == eImagePreMultiplied);
                unPremultPixelData(renderWindow, srcPixelData, *bounds, srcMappedComponents, srcMappedComponentsCount
                                   , bitDepth, srcRowBytes, tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
            }
#         ifdef OFX_IO_USING_OCIO
            // do the color-space conversion
            if ( (srcMappedComponents == ePixelComponentRGB) || (srcMappedComponents == ePixelComponentRGBA) ) {
                _ocio->apply(time, renderWindowClipped, tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, tmpRowBytes);
            }
#         endif

            ///If needed, re-premult the image for the plugin to work correctly
            if ( (pluginExpectedPremult == eImagePreMultiplied) && (srcMappedComponents == ePixelComponentRGBA) ) {
                premultPixelData(renderWindow, tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount
                                 , bitDepth, tmpRowBytes, tmpPixelData, renderWindow, srcMappedComponents, srcMappedComponentsCount, bitDepth, tmpRowBytes);
            }
        } // if (isOCIOIdentity) {

        // copy to dstImg if necessary
        if ( (renderRequestedView == view) && _outputClip && _outputClip->isConnected() ) {
            std::auto_ptr<Image> dstImg( _outputClip->fetchImagePlane( time, renderRequestedView, plane.c_str() ) );
            if ( !dstImg.get() ) {
                setPersistentMessage(Message::eMessageError, "", "Output image could not be fetched");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            if ( (dstImg->getRenderScale().x != renderScale.x) ||
                 ( dstImg->getRenderScale().y != renderScale.y) ||
                 ( dstImg->getField() != fieldToRender) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }

            PixelComponentEnum dstMappedComponents;
            const int dstMappedComponentsCount = getPixelsComponentsCount(dstImg->getPixelComponentsProperty(), &dstMappedComponents);

            // copy the source image (the writer is a no-op)
            if (srcMappedComponentsCount == dstMappedComponentsCount) {
                copyPixelData( renderWindow, srcPixelData, *bounds, pixelComponents, srcMappedComponentsCount, bitDepth, srcRowBytes, dstImg.get() );
            } else {
                void* dstPixelData;
                OfxRectI dstBounds;
                PixelComponentEnum dstPixelComponents;
                BitDepthEnum dstBitDetph;
                int dstRowBytes;

                getImageData(dstImg.get(), &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDetph, &dstRowBytes);
                // Be careful: src may have more components than dst (eg dst is RGB, src is RGBA).
                // In this case, only copy the first components (thus the std::min)

                assert( ( /*dstPixelComponentStartIndex=*/ 0 + /*desiredSrcNComps=*/ std::min(srcMappedComponentsCount, dstMappedComponentsCount) ) <= /*dstPixelComponentCount=*/ dstMappedComponentsCount );
                interleavePixelBuffers(renderWindow,
                                       srcPixelData,
                                       *bounds,
                                       pixelComponents,
                                       srcMappedComponentsCount,
                                       0, // srcNCompsStartIndex
                                       std::min(srcMappedComponentsCount, dstMappedComponentsCount), // desiredSrcNComps
                                       bitDepth,
                                       srcRowBytes,
                                       dstBounds,
                                       dstPixelComponents,
                                       0, // dstPixelComponentStartIndex
                                       dstMappedComponentsCount,
                                       dstRowBytes,
                                       dstPixelData);
            }
        }
        *bounds = renderWindow;
    } // if (renderWindowIsBounds && isOCIOIdentity && (noPremult || userPremult == pluginExpectedPremult))


    if ( doAnyPacking && ( !packingContiguous || ( (int)packingMapping.size() != srcMappedComponentsCount ) ) ) {
        int pixelBytes = packingMapping.size() * getComponentBytes(bitDepth);
        int tmpRowBytes = (renderWindow.x2 - renderWindow.x1) * pixelBytes;
        size_t memSize = (size_t)(renderWindow.y2 - renderWindow.y1) * (size_t)tmpRowBytes;
        ImageMemory *packingBufferMem = new ImageMemory(memSize, this);
        srcImgsHolder->addMemory(packingBufferMem);
        float* packingBufferData = (float*)packingBufferMem->lock();
        if (!packingBufferData) {
            throwSuiteStatusException(kOfxStatErrMemory);

            return;
        }

        packPixelBuffer(renderWindow, *tmpMemPtr, *bounds, bitDepth, *rowBytes, srcMappedComponents, packingMapping, tmpRowBytes, packingBufferData);

        *tmpMemPtr = packingBufferData;
        *rowBytes = tmpRowBytes;
        *bounds = renderWindow;
        *mappedComponentsCount = packingMapping.size();
        switch ( packingMapping.size() ) {
        case 1:
            *mappedComponents = ePixelComponentAlpha;
            break;
        case 2:
            *mappedComponents = ePixelComponentXY;
            break;
        case 3:
            *mappedComponents = ePixelComponentRGB;
            break;
        case 4:
            *mappedComponents = ePixelComponentRGBA;
            break;
        default:
            assert(false);
            break;
        }
    }
} // GenericWriterPlugin::fetchPlaneConvertAndCopy

struct ImageData
{
    float* srcPixelData;
    int rowBytes;
    OfxRectI bounds;
    PixelComponentEnum pixelComponents;
    int pixelComponentsCount;
};

void
GenericWriterPlugin::getPackingOptions(bool *allCheckboxHidden,
                                       vector<int>* packingMapping) const
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
GenericWriterPlugin::render(const RenderArguments &args)
{
    const double time = args.time;

    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if (!_inputClip) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    if ( args.planes.empty() ) {
        setPersistentMessage(Message::eMessageError, "", "Host did not requested any layer to render");
        throwSuiteStatusException(kOfxStatFailed);
    }

    string filename;
    _fileParam->getValueAtTime(time, filename);
    // filename = filenameFromPattern(filename, time);
    {
        string ext = extension(filename);
        if ( !checkExtension(ext) ) {
            setPersistentMessage(Message::eMessageError, "", string("Unsupported file extension: ") + ext);
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    double pixelAspectRatio;
    getOutputRoD(time, args.renderView, 0, &pixelAspectRatio);

    ///This is automatically the same generally as inputClip premultiplication but can differ is the user changed it.
    int userPremult_i;
    _premult->getValueAtTime(time, userPremult_i);
    PreMultiplicationEnum userPremult = (PreMultiplicationEnum)userPremult_i;

    ///This is what the plug-in expects to be passed to the encode function.
    PreMultiplicationEnum pluginExpectedPremult = getExpectedInputPremultiplication();


    ///This is the mapping of destination channels onto source channels if packing happens
    vector<int> packingMapping;
    bool allCheckboxHidden;
    getPackingOptions(&allCheckboxHidden, &packingMapping);

    const bool doAnyPacking = args.planes.size() == 1 && !allCheckboxHidden;

    //Packing is required if channels are not contiguous, e.g: the user unchecked G but left R,B,A checked
    bool packingContiguous = true;

    if (doAnyPacking) {
        PixelComponentEnum clipComps = _inputClip->getPixelComponents();

        if ( packingMapping.empty() ) {
            setPersistentMessage(Message::eMessageError, "", "Nothing to render: At least 1 channel checkbox must be checked");
            throwSuiteStatusException(kOfxStatFailed);
        }
        if ( (clipComps == ePixelComponentAlpha) && (packingMapping.size() != 1) ) {
            setPersistentMessage(Message::eMessageError, "", "Output Components selected is Alpha: select only one single channel checkbox");
            throwSuiteStatusException(kOfxStatFailed);
        }

        if ( (packingMapping.size() == 1) && !_supportsAlpha ) {
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
                setPersistentMessage(Message::eMessageError, "", "Plug-in does not know how to render single-channel images");
                throwSuiteStatusException(kOfxStatFailed);
            }
        } else if ( (packingMapping.size() == 2) && !_supportsXY ) {
            if (_supportsRGB) {
                packingMapping.push_back(-1);
            } else if (_supportsRGBA) {
                for (int i = 0; i < 2; ++i) {
                    packingMapping.push_back(-1);
                }
            } else {
                setPersistentMessage(Message::eMessageError, "", "Plug-in does not know how to render 2-channel images");
                throwSuiteStatusException(kOfxStatFailed);
            }
        } else if ( (packingMapping.size() == 3) && !_supportsRGB ) {
            if (_supportsRGBA) {
                packingMapping.push_back(-1);
            } else {
                setPersistentMessage(Message::eMessageError, "", "Plug-in does not know how to render 3-channel images");
                throwSuiteStatusException(kOfxStatFailed);
            }
        } else if ( (packingMapping.size() == 4) && !_supportsRGBA ) {
            setPersistentMessage(Message::eMessageError, "", "Plug-in does not know how to render 4-channel images");
            throwSuiteStatusException(kOfxStatFailed);
        }
        int prevChannel = -1;
        for (std::size_t i = 0; i < packingMapping.size(); ++i) {
            if (i > 0) {
                if (packingMapping[i] != prevChannel + 1) {
                    packingContiguous = false;
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


#ifdef OFX_IO_USING_OCIO
    bool isOCIOIdentity = _ocio->isIdentity(time);
#else
    bool isOCIOIdentity = true;
#endif

    //The host required that we render all views into 1 file. This is for now only supported by EXR.

    bool doDefaultView = false;
    map<int, string> viewNames;
    int viewToRender = getViewToRender();

    if (viewToRender == kGenericWriterViewAll) {
        if (args.renderView != 0) {
            return; // nothing to do, except for the main view
        }
        int nViews = getViewCount();
        for (int v = 0; v < nViews; ++v) {
            string view = getViewName(v);
            viewNames[v] = view;
        }
    } else {
        int viewToRender = getViewToRender();
        if ( (viewToRender >= 0) && (viewToRender != args.renderView) ) {
            setPersistentMessage(Message::eMessageError, "", "Inconsistent view to render requested");
            throwSuiteStatusException(kOfxStatFailed);

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
            string view;
            view = getViewName(viewToRender);
            viewNames[viewToRender] = view;
        }
    }

    if (viewNames.empty() || doDefaultView) {
        string view;
        if (gHostIsMultiView) {
            view = getViewName(args.renderView);
        } else {
            view = "Main";
        }
        viewNames[args.renderView] = view;
    }
    assert( !viewNames.empty() );

    //This controls how we split into parts
    LayerViewsPartsEnum partsSplit = getPartsSplittingPreference();

    if ( (viewNames.size() == 1) && (args.planes.size() == 1) ) {
        //Regular case, just do a simple part
        int viewIndex = viewNames.begin()->first;
        InputImagesHolder dataHolder; // owns srcImg and tmpMem
        const Image* srcImg; // owned by dataHolder, no need to delete
        ImageMemory *tmpMem; // owned by dataHolder, no need to delete
        ImageData data;
        fetchPlaneConvertAndCopy(args.planes.front(), true, viewIndex, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);

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

        if ( (partsSplit == eLayerViewsSplitViews) &&
             ( args.planes.size() == 1) ) {
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
            InputImagesHolder dataHolder;     // owns all tmpMem and srcImg
            std::list<ImageData> planesData;

            // The list of actual planes that could be fetched
            std::list<string> actualPlanes;

            for (map<int, string>::const_iterator view = viewNames.begin(); view != viewNames.end(); ++view) {
                // The first view determines the planes that could be fetched. Other views just attempt to fetch the exact same planes.
                const std::list<string> *planesToFetch = 0;
                if ( view == viewNames.begin() ) {
                    planesToFetch = &args.planes;
                } else {
                    planesToFetch = &actualPlanes;
                }

                for (std::list<string>::const_iterator plane = planesToFetch->begin(); plane != planesToFetch->end(); ++plane) {
                    ImageMemory *tmpMem;     // owned by dataHolder, no need to delete
                    const Image* srcImg;     // owned by dataHolder, no need to delete
                    ImageData data;
                    fetchPlaneConvertAndCopy(*plane, false, view->first, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);
                    if (!data.srcPixelData) {
                        continue;
                    }

                    if ( view == viewNames.begin() ) {
                        actualPlanes.push_back(*plane);
                    }

                    assert(data.pixelComponentsCount != 0 && data.pixelComponents != ePixelComponentNone);

                    planesData.push_back(data);
                    int dstNComps = doAnyPacking ? packingMapping.size() : data.pixelComponentsCount;
                    nChannels += dstNComps;
                }    // for each plane
            }     // for each view
            if (nChannels == 0) {
                setPersistentMessage(Message::eMessageError, "", "Failed to fetch input layers");
                throwSuiteStatusException(kOfxStatFailed);

                return;
            }
            int pixelBytes = nChannels * getComponentBytes(eBitDepthFloat);
            int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
            size_t memSize = (size_t)(args.renderWindow.y2 - args.renderWindow.y1) * (size_t)tmpRowBytes;
            ImageMemory interleavedMem(memSize, this);
            float* tmpMemPtr = (float*)interleavedMem.lock();
            if (!tmpMemPtr) {
                throwSuiteStatusException(kOfxStatErrMemory);

                return;
            }

            ///Set to 0 everywhere since the render window might be bigger than the src img bounds
            memset(tmpMemPtr, 0, memSize);

            int interleaveIndex = 0;
            for (std::list<ImageData>::iterator it = planesData.begin(); it != planesData.end(); ++it) {
                assert(interleaveIndex < nChannels);

                int dstNComps = doAnyPacking ? packingMapping.size() : it->pixelComponentsCount;
                int dstNCompsStartIndex = doAnyPacking ? packingMapping[0] : 0;
                OfxRectI intersection;
                if ( Coords::rectIntersection(args.renderWindow, it->bounds, &intersection) ) {
                    assert( (/*dstPixelComponentStartIndex=*/ interleaveIndex + /*desiredSrcNComps=*/ dstNComps) <= /*dstPixelComponentCount=*/ nChannels );
                    interleavePixelBuffers(intersection,
                                           it->srcPixelData,
                                           it->bounds,
                                           it->pixelComponents,
                                           it->pixelComponentsCount,
                                           dstNCompsStartIndex,     // srcNCompsStartIndex
                                           dstNComps,     // desiredSrcNComps
                                           eBitDepthFloat,
                                           it->rowBytes,
                                           args.renderWindow,     // dstBounds
                                           ePixelComponentNone,     // dstPixelComponents
                                           interleaveIndex,     // dstPixelComponentStartIndex
                                           nChannels,     // dstPixelComponentCount
                                           tmpRowBytes,     // dstRowBytes
                                           tmpMemPtr);     // dstPixelData
                }
                interleaveIndex += dstNComps;
            }

            beginEncodeParts(encodeData.getData(), filename, time, pixelAspectRatio, partsSplit, viewNames, actualPlanes, doAnyPacking && !packingContiguous, packingMapping, args.renderWindow);
            encodePart(encodeData.getData(), filename, tmpMemPtr, nChannels, 0, tmpRowBytes);

            break;
        }
        case eLayerViewsSplitViews: {
            /*
               Write each view into a single part but aggregate all layers for each view
             */
            // The list of actual planes that could be fetched
            std::list<string> actualPlanes;


            int partIndex = 0;
            for (map<int, string>::const_iterator view = viewNames.begin(); view != viewNames.end(); ++view) {
                // The first view determines the planes that could be fetched. Other views just attempt to fetch the exact same planes.
                const std::list<string> *planesToFetch = 0;
                if ( view == viewNames.begin() ) {
                    planesToFetch = &args.planes;
                } else {
                    planesToFetch = &actualPlanes;
                }

                int nChannels = 0;
                InputImagesHolder dataHolder;     // owns all tmpMem and srcImg

                std::list<ImageData> planesData;
                for (std::list<string>::const_iterator plane = planesToFetch->begin(); plane != planesToFetch->end(); ++plane) {
                    ImageMemory *tmpMem;     // owned by dataHolder, no need to delete
                    const Image* srcImg;     // owned by dataHolder, no need to delete
                    ImageData data;
                    fetchPlaneConvertAndCopy(*plane, false, view->first, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);
                    if (!data.srcPixelData) {
                        continue;
                    }

                    if ( view == viewNames.begin() ) {
                        actualPlanes.push_back(*plane);
                    }

                    assert(data.pixelComponentsCount != 0 && data.pixelComponents != ePixelComponentNone);

                    planesData.push_back(data);

                    int dstNComps = doAnyPacking ? packingMapping.size() : data.pixelComponentsCount;
                    nChannels += dstNComps;
                }
                if (nChannels == 0) {
                    setPersistentMessage(Message::eMessageError, "", "Failed to fetch input layers");
                    throwSuiteStatusException(kOfxStatFailed);

                    return;
                }
                int pixelBytes = nChannels * getComponentBytes(eBitDepthFloat);
                int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
                size_t memSize = (size_t)(args.renderWindow.y2 - args.renderWindow.y1) * (size_t)tmpRowBytes;
                ImageMemory interleavedMem(memSize, this);
                float* tmpMemPtr = (float*)interleavedMem.lock();
                if (!tmpMemPtr) {
                    throwSuiteStatusException(kOfxStatErrMemory);

                    return;
                }

                ///Set to 0 everywhere since the render window might be bigger than the src img bounds
                memset(tmpMemPtr, 0, memSize);

                int interleaveIndex = 0;
                for (std::list<ImageData>::iterator it = planesData.begin(); it != planesData.end(); ++it) {
                    assert(interleaveIndex < nChannels);

                    OfxRectI intersection;
                    int dstNComps = doAnyPacking ? packingMapping.size() : it->pixelComponentsCount;
                    int dstNCompsStartIndex = doAnyPacking ? packingMapping[0] : 0;
                    if ( Coords::rectIntersection(args.renderWindow, it->bounds, &intersection) ) {
                        assert( (/*dstPixelComponentStartIndex=*/ interleaveIndex + /*desiredSrcNComps=*/ dstNComps) <= /*dstPixelComponentCount=*/ nChannels );
                        interleavePixelBuffers(intersection,
                                               it->srcPixelData,
                                               it->bounds,
                                               it->pixelComponents,
                                               it->pixelComponentsCount,
                                               dstNCompsStartIndex,     // srcNCompsStartIndex
                                               dstNComps,     // desiredSrcNComps
                                               eBitDepthFloat,
                                               it->rowBytes,
                                               args.renderWindow,
                                               ePixelComponentNone,     // dstPixelComponents
                                               interleaveIndex,     // dstPixelComponentStartIndex
                                               nChannels,     // dstPixelComponentCount
                                               tmpRowBytes,
                                               tmpMemPtr);
                    }
                    interleaveIndex += dstNComps;
                }

                if ( view == viewNames.begin() ) {
                    beginEncodeParts(encodeData.getData(), filename, time, pixelAspectRatio, partsSplit, viewNames, actualPlanes, doAnyPacking && !packingContiguous, packingMapping, args.renderWindow);
                }

                encodePart(encodeData.getData(), filename, tmpMemPtr, nChannels, partIndex, tmpRowBytes);

                ++partIndex;
            }     // for each view

            break;
        }
        case eLayerViewsSplitViewsLayers: {
            /*
               Write each layer of each view in an independent part
             */
            // The list of actual planes that could be fetched
            std::list<string> actualPlanes;

            int partIndex = 0;
            for (map<int, string>::const_iterator view = viewNames.begin(); view != viewNames.end(); ++view) {
                InputImagesHolder dataHolder;     // owns all tmpMem and srcImg
                vector<ImageData> datas;

                // The first view determines the planes that could be fetched. Other views just attempt to fetch the exact same planes.
                const std::list<string> *planesToFetch = 0;
                if ( view == viewNames.begin() ) {
                    planesToFetch = &args.planes;
                } else {
                    planesToFetch = &actualPlanes;
                }

                for (std::list<string>::const_iterator plane = planesToFetch->begin(); plane != planesToFetch->end(); ++plane) {
                    ImageMemory *tmpMem;     // owned by dataHolder, no need to delete
                    const Image* srcImg;     // owned by dataHolder, no need to delete
                    ImageData data;
                    fetchPlaneConvertAndCopy(*plane, false, view->first, args.renderView, time, args.renderWindow, args.renderScale, args.fieldToRender, pluginExpectedPremult, userPremult, isOCIOIdentity, doAnyPacking, packingContiguous, packingMapping, &dataHolder, &data.bounds, &tmpMem, &srcImg, &data.srcPixelData, &data.rowBytes, &data.pixelComponents, &data.pixelComponentsCount);
                    if (!data.srcPixelData) {
                        continue;
                    }
                    datas.push_back(data);
                    if ( view == viewNames.begin() ) {
                        actualPlanes.push_back(*plane);
                    }
                }     // for each plane

                if ( view == viewNames.begin() ) {
                    beginEncodeParts(encodeData.getData(), filename, time, pixelAspectRatio, partsSplit, viewNames, actualPlanes, doAnyPacking && !packingContiguous, packingMapping, args.renderWindow);
                }
                for (vector<ImageData>::iterator it = datas.begin(); it != datas.end(); ++it) {
                    encodePart(encodeData.getData(), filename, it->srcPixelData, it->pixelComponentsCount, partIndex, it->rowBytes);
                    ++partIndex;
                }
            }     // for each view

            break;
        }
        } // switch
        ;

        endEncodeParts( encodeData.getData() );
    }

    clearPersistentMessage();
} // GenericWriterPlugin::render

class PackPixelsProcessorBase
    : public PixelProcessorFilterBase
{
protected:

    vector<int> _mapping;

public:
    PackPixelsProcessorBase(ImageEffect& instance)
        : PixelProcessorFilterBase(instance)
        , _mapping()
    {
    }

    void setMapping(const vector<int>& mapping)
    {
        _mapping = mapping;
    }
};


template <typename PIX, int maxValue, int srcNComps>
class PackPixelsProcessor
    : public PackPixelsProcessorBase
{
public:

    PackPixelsProcessor(ImageEffect& instance)
        : PackPixelsProcessorBase(instance)
    {
    }

    virtual void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert(_srcBounds.x1 < _srcBounds.x2 && _srcBounds.y1 < _srcBounds.y2);
        assert( (int)_mapping.size() == _dstPixelComponentCount );

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
            if ( (y % 100 == 0) && _effect.abort() ) {
                //check for abort only every 100 lines
                break;
            }

            for (int x = procWindow.x1; x < procWindow.x2; ++x,
                 srcPix += srcNComps,
                 dstPix += _dstPixelComponentCount
                 ) {
                assert( srcPix == ( (const PIX*)getSrcPixelAddress(x, y) ) );
                for (int c = 0; c < _dstPixelComponentCount; ++c) {
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


template <typename PIX, int maxValue>
void
packPixelBufferForDepth(ImageEffect* instance,
                        const OfxRectI& renderWindow,
                        const void *srcPixelData,
                        const OfxRectI& bounds,
                        BitDepthEnum bitDepth,
                        int srcRowBytes,
                        PixelComponentEnum srcPixelComponents,
                        const vector<int>& channelsMapping,
                        int dstRowBytes,
                        void* dstPixelData)
{
    assert(channelsMapping.size() <= 4);
    std::auto_ptr<PackPixelsProcessorBase> p;
    int srcNComps = 0;
    switch (srcPixelComponents) {
    case ePixelComponentAlpha:
        p.reset( new PackPixelsProcessor<PIX, maxValue, 1>(*instance) );
        srcNComps = 1;
        break;
    case ePixelComponentXY:
        p.reset( new PackPixelsProcessor<PIX, maxValue, 2>(*instance) );
        srcNComps = 2;
        break;
    case ePixelComponentRGB:
        p.reset( new PackPixelsProcessor<PIX, maxValue, 3>(*instance) );
        srcNComps = 3;
        break;
    case ePixelComponentRGBA:
        p.reset( new PackPixelsProcessor<PIX, maxValue, 4>(*instance) );
        srcNComps = 4;
        break;
    default:
        //Unsupported components
        throwSuiteStatusException(kOfxStatFailed);
        break;
    }
    ;

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
                                     BitDepthEnum bitDepth,
                                     int srcRowBytes,
                                     PixelComponentEnum srcPixelComponents,
                                     const vector<int>& channelsMapping, //maps dst channels to input channels
                                     int dstRowBytes,
                                     void* dstPixelData)
{
    assert(renderWindow.x1 >= bounds.x1 && renderWindow.x2 <= bounds.x2 &&
           renderWindow.y1 >= bounds.y1 && renderWindow.y2 <= bounds.y2);
    switch (bitDepth) {
    case eBitDepthFloat:
        packPixelBufferForDepth<float, 1>(this, renderWindow, (const float*)srcPixelData, bounds, bitDepth, srcRowBytes, srcPixelComponents, channelsMapping, dstRowBytes, (float*)dstPixelData);
        break;
    case eBitDepthUByte:
        packPixelBufferForDepth<unsigned char, 255>(this, renderWindow, (const unsigned char*)srcPixelData, bounds, bitDepth, srcRowBytes, srcPixelComponents, channelsMapping, dstRowBytes, (unsigned char*)dstPixelData);
        break;
    case eBitDepthUShort:
        packPixelBufferForDepth<unsigned short, 65535>(this, renderWindow, (const unsigned short*)srcPixelData, bounds, bitDepth, srcRowBytes, srcPixelComponents, channelsMapping, dstRowBytes, (unsigned short*)dstPixelData);
        break;
    default:
        //unknown pixel depth
        throwSuiteStatusException(kOfxStatFailed);
        break;
    }
}

class InterleaveProcessorBase
    : public PixelProcessorFilterBase
{
protected:

    int _dstStartIndex;
    int _desiredSrcNComps;
    int _srcNCompsStartIndex;

public:
    InterleaveProcessorBase(ImageEffect& instance)
        : PixelProcessorFilterBase(instance)
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
class InterleaveProcessor
    : public InterleaveProcessorBase
{
public:

    InterleaveProcessor(ImageEffect& instance)
        : InterleaveProcessorBase(instance)
    {
    }

    virtual void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        assert(_srcBounds.x1 < _srcBounds.x2 && _srcBounds.y1 < _srcBounds.y2);
        assert(_dstStartIndex >= 0);
        assert(_dstStartIndex + _desiredSrcNComps <= _dstPixelComponentCount); // inner loop must not overrun dstPix
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
            if ( (y % 10 == 0) && _effect.abort() ) {
                //check for abort only every 10 lines
                break;
            }

            for (int x = procWindow.x1; x < procWindow.x2; ++x,
                 srcPix += srcNComps,
                 dstPix += _dstPixelComponentCount
                 ) {
                assert(dstPix == ( (PIX*)getDstPixelAddress(x, y) ) + _dstStartIndex);
                assert( srcPix == ( (const PIX*)getSrcPixelAddress(x, y) ) );

                for (int c = 0; c < _desiredSrcNComps; ++c) {
                    dstPix[c] = srcPix[c + _srcNCompsStartIndex];
                }
            }
        }
    }
};

template <typename PIX, int maxValue>
void
interleavePixelBuffersForDepth(ImageEffect* instance,
                               const OfxRectI& renderWindow,
                               const PIX *srcPixelData,
                               const OfxRectI& bounds,
                               const PixelComponentEnum srcPixelComponents,
                               const int srcPixelComponentCount,
                               const int srcNCompsStartIndex,
                               const int desiredSrcNComps,
                               const BitDepthEnum bitDepth,
                               const int srcRowBytes,
                               const OfxRectI& dstBounds,
                               const PixelComponentEnum dstPixelComponents,      // ignored, may be ePixelComponentNone
                               const int dstPixelComponentStartIndex,
                               const int dstPixelComponentCount,
                               const int dstRowBytes,
                               PIX* dstPixelData)
{
    assert( (dstPixelComponentStartIndex + desiredSrcNComps) <= dstPixelComponentCount );
    std::auto_ptr<InterleaveProcessorBase> p;
    switch (srcPixelComponentCount) {
    case 1:
        p.reset( new InterleaveProcessor<PIX, maxValue, 1>(*instance) );
        break;
    case 2:
        p.reset( new InterleaveProcessor<PIX, maxValue, 2>(*instance) );
        break;
    case 3:
        p.reset( new InterleaveProcessor<PIX, maxValue, 3>(*instance) );
        break;
    case 4:
        p.reset( new InterleaveProcessor<PIX, maxValue, 4>(*instance) );
        break;
    default:
        //Unsupported components
        throwSuiteStatusException(kOfxStatFailed);
        break;
    }
    ;
    p->setSrcImg(srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, bitDepth, srcRowBytes, 0);
    p->setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, bitDepth, dstRowBytes);
    p->setRenderWindow(renderWindow);
    p->setValues(dstPixelComponentStartIndex, desiredSrcNComps, srcNCompsStartIndex);

    p->process();
}

void
GenericWriterPlugin::interleavePixelBuffers(const OfxRectI& renderWindow,
                                            const void *srcPixelData,
                                            const OfxRectI& bounds,
                                            const PixelComponentEnum srcPixelComponents,
                                            const int srcPixelComponentCount,
                                            const int srcNCompsStartIndex,
                                            const int desiredSrcNComps,
                                            const BitDepthEnum bitDepth,
                                            const int srcRowBytes,
                                            const OfxRectI& dstBounds,
                                            const PixelComponentEnum dstPixelComponents, // ignored, may be ePixelComponentNone
                                            const int dstPixelComponentStartIndex,
                                            const int dstPixelComponentCount,
                                            const int dstRowBytes,
                                            void* dstPixelData)
{
    assert( (dstPixelComponentStartIndex + desiredSrcNComps) <= dstPixelComponentCount );
    assert(renderWindow.x1 >= bounds.x1 && renderWindow.x2 <= bounds.x2 &&
           renderWindow.y1 >= bounds.y1 && renderWindow.y2 <= bounds.y2);
    assert(renderWindow.x1 >= dstBounds.x1 && renderWindow.x2 <= dstBounds.x2 &&
           renderWindow.y1 >= dstBounds.y1 && renderWindow.y2 <= dstBounds.y2);
    switch (bitDepth) {
    case eBitDepthFloat:
        interleavePixelBuffersForDepth<float, 1>(this, renderWindow, (const float*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, srcNCompsStartIndex, desiredSrcNComps, bitDepth, srcRowBytes, dstBounds, dstPixelComponents, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (float*)dstPixelData);
        break;
    case eBitDepthUByte:
        interleavePixelBuffersForDepth<unsigned char, 255>(this, renderWindow, (const unsigned char*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, srcNCompsStartIndex, desiredSrcNComps, bitDepth, srcRowBytes, dstBounds, dstPixelComponents, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (unsigned char*)dstPixelData);
        break;
    case eBitDepthUShort:
        interleavePixelBuffersForDepth<unsigned short, 65535>(this, renderWindow, (const unsigned short*)srcPixelData, bounds, srcPixelComponents, srcPixelComponentCount, srcNCompsStartIndex, desiredSrcNComps, bitDepth, srcRowBytes, dstBounds, dstPixelComponents, dstPixelComponentStartIndex, dstPixelComponentCount, dstRowBytes, (unsigned short*)dstPixelData);
        break;
    default:
        //unknown pixel depth
        throwSuiteStatusException(kOfxStatFailed);
        break;
    }
}

void
GenericWriterPlugin::beginSequenceRender(const BeginSequenceRenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    string filename;
    _fileParam->getValue(filename);
    {
        string ext = extension(filename);
        if ( !checkExtension(ext) ) {
            setPersistentMessage(Message::eMessageError, "", string("Unsupported file extension: ") + ext);
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    OfxRectD rod;
    double par;
    getOutputRoD(args.frameRange.min, args.view, &rod, &par);

    ////Since the generic writer doesn't support tiles and multi-resolution, the RoD is necesserarily the
    ////output image size.
    OfxRectI rodPixel;
    Coords::toPixelEnclosing(rod, args.renderScale, par, &rodPixel);

    beginEncode(filename, rodPixel, par, args);
}

void
GenericWriterPlugin::endSequenceRender(const EndSequenceRenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    endEncode(args);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

static void
setupAndProcess(PixelProcessorFilterBase & processor,
                int premultChannel,
                const OfxRectI &renderWindow,
                const void *srcPixelData,
                const OfxRectI& srcBounds,
                PixelComponentEnum srcPixelComponents,
                int srcPixelComponentCount,
                BitDepthEnum srcPixelDepth,
                int srcRowBytes,
                void *dstPixelData,
                const OfxRectI& dstBounds,
                PixelComponentEnum dstPixelComponents,
                int dstPixelComponentCount,
                BitDepthEnum dstPixelDepth,
                int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);

    // make sure bit depths are sane
    if ( (srcPixelDepth != dstPixelDepth) || (srcPixelComponents != dstPixelComponents) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

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
                                        PixelComponentEnum srcPixelComponents,
                                        int srcPixelComponentCount,
                                        BitDepthEnum srcPixelDepth,
                                        int srcRowBytes,
                                        void *dstPixelData,
                                        const OfxRectI& dstBounds,
                                        PixelComponentEnum dstPixelComponents,
                                        int dstPixelComponentCount,
                                        BitDepthEnum dstBitDepth,
                                        int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);

    // do the rendering
    if ( (dstBitDepth != eBitDepthFloat) || ( (dstPixelComponents != ePixelComponentRGBA) && (dstPixelComponents != ePixelComponentRGB) && (dstPixelComponents != ePixelComponentAlpha) ) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }
    if (dstPixelComponents == ePixelComponentRGBA) {
        PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
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
                                      PixelComponentEnum srcPixelComponents,
                                      int srcPixelComponentCount,
                                      BitDepthEnum srcPixelDepth,
                                      int srcRowBytes,
                                      void *dstPixelData,
                                      const OfxRectI& dstBounds,
                                      PixelComponentEnum dstPixelComponents,
                                      int dstPixelComponentCount,
                                      BitDepthEnum dstBitDepth,
                                      int dstRowBytes)
{
    assert(srcPixelData && dstPixelData);

    // do the rendering
    if ( (dstBitDepth != eBitDepthFloat) || ( (dstPixelComponents != ePixelComponentRGBA) && (dstPixelComponents != ePixelComponentRGB) && (dstPixelComponents != ePixelComponentAlpha) ) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    if (dstPixelComponents == ePixelComponentRGBA) {
        PixelCopierPremult<float, 4, 1, float, 4, 1> fred(*this);
        setupAndProcess(fred, 3, renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcPixelDepth, srcRowBytes, dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    } else {
        ///other pixel components means you want to copy only...
        assert(false);
    }
}

void
GenericWriterPlugin::getSelectedOutputFormat(OfxRectI* format,
                                             double* par)
{
    FormatTypeEnum formatType = (FormatTypeEnum)_outputFormatType->getValue();

    switch (formatType) {
    case eFormatTypeInput: {
        // union RoD across all views
        OfxRectI inputFormat;
        _inputClip->getFormat(inputFormat);
        double inputPar = _inputClip->getPixelAspectRatio();
        format->x1 = inputFormat.x1 * inputPar;
        format->x2 = inputFormat.x2 * inputPar;
        format->y1 = inputFormat.y1;
        format->y2 = inputFormat.y2;
        *par = inputPar;
        break;
    }
    case eFormatTypeProject: {
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        double projectPar = getProjectPixelAspectRatio();
        *par = projectPar;
        format->x1 = offset.x * projectPar;
        format->y1 = offset.y;
        format->x2 = (offset.x + size.x) * projectPar;
        format->y2 = offset.y + size.y;
        break;
    }
    case eFormatTypeFixed: {
        int w, h;
        _outputFormatSize->getValue(w, h);
        _outputFormatPar->getValue(*par);
        format->x1 = format->y1 = 0;
        format->x2 = w;
        format->y2 = h;
        break;
    }
    }
} // getSelectedOutputFormat

void
GenericWriterPlugin::getOutputRoD(OfxTime time,
                                  int view,
                                  OfxRectD* rod,
                                  double* par)
{
    assert(rod || par);

    bool clipToRoD = false;
    if ( _clipToRoD && !_clipToRoD->getIsSecret() ) {
        _clipToRoD->getValue(clipToRoD);
    }
    // user wants RoD written, don't care parameters
    if (clipToRoD) {
        if (rod) {
            *rod = _inputClip->getRegionOfDefinition(time, view);
        }
        if (par) {
            *par = _inputClip->getPixelAspectRatio();
        }
    } else {
        OfxRectI format;
        double formatPar;
        getSelectedOutputFormat(&format, &formatPar);
        if (rod) {
            OfxPointD renderScale = {1., 1.};
            Coords::toCanonical(format, renderScale, formatPar, rod);
        }
        if (par) {
            *par = formatPar;
        }
    }
}

bool
GenericWriterPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                           OfxRectD &rod)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return false;
    }
    getOutputRoD(args.time, args.view, &rod, 0);

    return true;
}

void
GenericWriterPlugin::encode(const string& /*filename*/,
                            const OfxTime /*time*/,
                            const string& /*viewName*/,
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
                                      const string& /*filename*/,
                                      OfxTime /*time*/,
                                      float /*pixelAspectRatio*/,
                                      LayerViewsPartsEnum /*partsSplitting*/,
                                      const map<int, string>& /*viewsToRender*/,
                                      const std::list<string>& /*planes*/,
                                      const bool /*packingRequired*/,
                                      const vector<int>& /*packingMapping*/,
                                      const OfxRectI& /*bounds*/)
{
    /// Does nothing
}

void
GenericWriterPlugin::encodePart(void* /*user_data*/,
                                const string& /*filename*/,
                                const float */*pixelData*/,
                                int /*pixelDataNComps*/,
                                int /*planeIndex*/,
                                int /*rowBytes*/)
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

static string
imageFormatString(PixelComponentEnum components,
                  BitDepthEnum bitDepth)
{
    string s;

    switch (components) {
    case ePixelComponentRGBA:
        s += "RGBA";
        break;
    case ePixelComponentRGB:
        s += "RGB";
        break;
    case ePixelComponentAlpha:
        s += "Alpha";
        break;
    case ePixelComponentCustom:
        s += "Custom";
        break;
    case ePixelComponentNone:
        s += "None";
        break;
    default:
        s += "[unknown components]";
        break;
    }
    switch (bitDepth) {
    case eBitDepthUByte:
        s += "8u";
        break;
    case eBitDepthUShort:
        s += "16u";
        break;
    case eBitDepthFloat:
        s += "32f";
        break;
    case eBitDepthCustom:
        s += "x";
        break;
    case eBitDepthNone:
        s += "0";
        break;
    default:
        s += "[unknown bit depth]";
        break;
    }

    return s;
}

static string
premultString(PreMultiplicationEnum e)
{
    switch (e) {
    case eImageOpaque:

        return "Opaque";
    case eImagePreMultiplied:

        return "PreMultiplied";
    case eImageUnPreMultiplied:

        return "UnPreMultiplied";
    }

    return "Unknown";
}

void
GenericWriterPlugin::setOutputComponentsParam(PixelComponentEnum components)
{
    assert(components == ePixelComponentRGB || components == ePixelComponentRGBA || components == ePixelComponentAlpha);
}

void
GenericWriterPlugin::outputFileChanged(InstanceChangeReason reason,
                                       bool restoreExistingWriter,
                                       bool throwErrors)
{
    string filename;

    _fileParam->getValue(filename);

    if ( filename.empty() ) {
        // if the file name is set to an empty string,
        // reset so that values are automatically set on next call to outputFileChanged()
        _guessedParams->resetToDefault();

        return;
    }

    // only set perstistent params if not restoring
    bool setPersistentValues = !restoreExistingWriter && (reason == eChangeUserEdit);

    {
        string ext = extension(filename);
        if ( !checkExtension(ext) ) {
            if (throwErrors) {
                if (reason == eChangeUserEdit) {
                    sendMessage(Message::eMessageError, "", string("Unsupported file extension: ") + ext);
                } else {
                    setPersistentMessage(Message::eMessageError, "", string("Unsupported file extension: ") + ext);
                }
                throwSuiteStatusException(kOfxStatErrImageFormat);
            } else {
                return;
            }
        }
    }
    bool setColorSpace = true;
#     ifdef OFX_IO_USING_OCIO
    // if outputSpaceSet == true (output space was manually set by user) then setColorSpace = false
    if ( _outputSpaceSet->getValue() ) {
        setColorSpace = false;
    }
    // Always try to parse from string first,
    // following recommendations from http://opencolorio.org/configurations/spi_pipeline.html
    if ( setColorSpace && _ocio->getConfig() ) {
        const char* colorSpaceStr = _ocio->getConfig()->parseColorSpaceFromString( filename.c_str() );
        if ( colorSpaceStr && (std::strlen(colorSpaceStr) == 0) ) {
            colorSpaceStr = NULL;
        }
        if ( colorSpaceStr && _ocio->hasColorspace(colorSpaceStr) ) {
            // we're lucky
            _ocio->setOutputColorspace(colorSpaceStr);
            setColorSpace = false;
        }
    }
#     endif

    // give the derived class a chance to initialize any data structure it may need
    onOutputFileChanged(filename, setColorSpace);
#     ifdef OFX_IO_USING_OCIO
    _ocio->refreshInputAndOutputState(0);
#     endif

    if (_clipToRoD) {
        _clipToRoD->setIsSecretAndDisabled( !displayWindowSupportedByFormat(filename) );
    }


    if (reason == eChangeUserEdit) {
        // mark that most parameters should not be set automagically if the filename is changed
        // (the user must keep control over what happens)
        _guessedParams->setValue(true);
    }
} // GenericWriterPlugin::outputFileChanged

void
GenericWriterPlugin::changedParam(const InstanceChangedArgs &args,
                                  const string &paramName)
{
    if (paramName == kParamFrameRange) {
        int choice;
        double first, last;
        timeLineGetBounds(first, last);
        _frameRange->getValue(choice);
        if (choice == 2) {
            _firstFrame->setIsSecretAndDisabled(false);
            int curFirstFrame;
            _firstFrame->getValue(curFirstFrame);
            // if first-frame has never been set by the user, set it
            if ( (first != curFirstFrame) && (curFirstFrame == 0) ) {
                _firstFrame->setValue( (int)first );
            }
            _lastFrame->setIsSecretAndDisabled(false);
            int curLastFrame;
            _lastFrame->getValue(curLastFrame);
            // if last-frame has never been set by the user, set it
            if ( (last != curLastFrame) && (curLastFrame == 0) ) {
                _lastFrame->setValue( (int)last );
            }
        } else {
            _firstFrame->setIsSecretAndDisabled(true);
            _lastFrame->setIsSecretAndDisabled(true);
        }
    } else if (paramName == kParamFilename) {
        outputFileChanged(args.reason, _guessedParams->getValue(), true);
    } else if (paramName == kParamFormatType) {
        FormatTypeEnum type = (FormatTypeEnum)_outputFormatType->getValue();
        if (_clipToRoD) {
            string filename;
            _fileParam->getValue(filename);
            _clipToRoD->setIsSecretAndDisabled( !displayWindowSupportedByFormat(filename) );
        }
        if ( (type == eFormatTypeInput) || (type == eFormatTypeProject) ) {
            _outputFormat->setIsSecretAndDisabled(true);
        } else {
            _outputFormat->setIsSecretAndDisabled(false);
        }
    } else if (paramName == kParamOutputFormat) {
        //the host does not handle the format itself, do it ourselves
        int format_i;
        _outputFormat->getValue(format_i);
        int w = 0, h = 0;
        double par = -1;
        getFormatResolution( (EParamFormat)format_i, &w, &h, &par );
        assert(par != -1);
        _outputFormatPar->setValue(par);
        _outputFormatSize->setValue(w, h);
    } else if ( (paramName == kParamClipInfo) && (args.reason == eChangeUserEdit) ) {
        string msg;
        msg += "Input: ";
        if (!_inputClip) {
            msg += "N/A";
        } else {
            msg += imageFormatString( _inputClip->getPixelComponents(), _inputClip->getPixelDepth() );
            msg += " ";
            msg += premultString( _inputClip->getPreMultiplication() );
        }
        msg += "\n";
        msg += "Output: ";
        if (!_outputClip) {
            msg += "N/A";
        } else {
            msg += imageFormatString( _outputClip->getPixelComponents(), _outputClip->getPixelDepth() );
            msg += " ";
            msg += premultString( _outputClip->getPreMultiplication() );
        }
        msg += "\n";
        sendMessage(Message::eMessageMessage, "", msg);
#ifdef OFX_IO_USING_OCIO
    } else if ( ( (paramName == kOCIOParamOutputSpace) || (paramName == kOCIOParamOutputSpaceChoice) ) &&
                ( args.reason == eChangeUserEdit) ) {
        // set the outputSpaceSet param to true https://github.com/MrKepzie/Natron/issues/1492
        _outputSpaceSet->setValue(true);
#endif
    }

#ifdef OFX_IO_USING_OCIO
    _ocio->changedParam(args, paramName);
#endif
} // GenericWriterPlugin::changedParam

void
GenericWriterPlugin::changedClip(const InstanceChangedArgs &args,
                                 const string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) && _inputClip && (args.reason == eChangeUserEdit) ) {
        PreMultiplicationEnum premult = _inputClip->getPreMultiplication();
#     ifdef DEBUG
        if ( _inputClip->isConnected() ) {
            PixelComponentEnum components = _inputClip->getPixelComponents();
            assert( (components == ePixelComponentAlpha && premult != eImageOpaque) ||
                    (components == ePixelComponentRGB && premult == eImageOpaque) ||
                    (components == ePixelComponentRGBA) ||
                    ( (components == ePixelComponentCustom ||
                       components == ePixelComponentMotionVectors ||
                       components == ePixelComponentStereoDisparity) && gHostIsMultiPlanar ) );


            int index = -1;
            for (std::size_t i = 0; i < _outputComponentsTable.size(); ++i) {
                if (_outputComponentsTable[i] == components) {
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
GenericWriterPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if ( !_outputComponents->getIsSecret() ) {
        int index;
        _outputComponents->getValue(index);
        assert( index >= 0 && index < (int)_outputComponentsTable.size() );
        PixelComponentEnum comps = _outputComponentsTable[index];


        vector<string> checkboxesLabels;
        if (comps == ePixelComponentAlpha) {
            checkboxesLabels.push_back("A");
        } else if (comps == ePixelComponentRGB) {
            checkboxesLabels.push_back("R");
            checkboxesLabels.push_back("G");
            checkboxesLabels.push_back("B");
        } else if (comps == ePixelComponentRGBA) {
            checkboxesLabels.push_back("R");
            checkboxesLabels.push_back("G");
            checkboxesLabels.push_back("B");
            checkboxesLabels.push_back("A");
        }

        if (checkboxesLabels.size() == 1) {
            for (int i = 0; i < 3; ++i) {
                _processChannels[i]->setIsSecretAndDisabled(true);
            }
            _processChannels[3]->setIsSecretAndDisabled(false);
            _processChannels[3]->setLabel(checkboxesLabels[0]);
        } else {
            for (int i = 0; i < 4; ++i) {
                if ( i < (int)checkboxesLabels.size() ) {
                    _processChannels[i]->setIsSecretAndDisabled(false);
                    _processChannels[i]->setLabel(checkboxesLabels[i]);
                } else {
                    _processChannels[i]->setIsSecretAndDisabled(true);
                }
            }
        }
        //Set output pixel components to match what will be output if the choice is not All


        clipPreferences.setClipComponents(*_inputClip, comps);
        clipPreferences.setClipComponents(*_outputClip, comps);
        PreMultiplicationEnum premult = _inputClip->getPreMultiplication();
        switch (comps) {
        case ePixelComponentAlpha:
            premult = eImageUnPreMultiplied;
            break;
        case ePixelComponentXY:
            premult = eImageOpaque;
            break;
        case ePixelComponentRGB:
            premult = eImageOpaque;
            break;
        default:
            break;
        }

        clipPreferences.setOutputPremultiplication(premult);
    }
} // GenericWriterPlugin::getClipPreferences

void
GenericWriterPlugin::getFrameViewsNeeded(const FrameViewsNeededArguments& args,
                                         FrameViewsNeededSetter& frameViews)
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
#ifdef OFX_IO_USING_OCIO
    _ocio->purgeCaches();
#endif
}

using namespace OFX;

/**
 * @brief Override this to describe the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describe(desc);
 **/
void
GenericWriterDescribe(ImageEffectDescriptor &desc,
                      RenderSafetyEnum safety,
                      const vector<string>& extensions, // list of supported extensions
                      int evaluation, // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated
                      bool isMultiPlanar,
                      bool isMultiView)
{
    desc.setPluginGrouping(kPluginGrouping);

#ifdef OFX_EXTENSIONS_TUTTLE
    desc.addSupportedContext(eContextWriter);
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(evaluation);
#endif
    desc.addSupportedContext(eContextGeneral);

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
    if (getImageEffectHostDescription()
        && getImageEffectHostDescription()->isMultiPlanar) {
        desc.setIsMultiPlanar(isMultiPlanar);
        if (isMultiPlanar) {
            gHostIsMultiPlanar = true;
            //We let all un-rendered planes pass-through so that they can be retrieved below by a shuffle node
            desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelPassThroughNonRenderedPlanes);
        }
    }
    if ( isMultiView && fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
        gHostIsMultiView = true;
        desc.setIsViewAware(true);
        desc.setIsViewInvariant(eViewInvarianceAllViewsVariant);
    }
#endif

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

/**
 * @brief Override this to describe in context the writer.
 * You should call the base-class version at the end like this:
 * GenericWriterPluginFactory<YOUR_FACTORY>::describeInContext(desc,context);
 **/
PageParamDescriptor*
GenericWriterDescribeInContextBegin(ImageEffectDescriptor &desc,
                                    ContextEnum context,
                                    bool supportsRGBA,
                                    bool supportsRGB,
                                    bool supportsXY,
                                    bool supportsAlpha,
                                    const char* inputSpaceNameDefault,
                                    const char* outputSpaceNameDefault,
                                    bool supportsDisplayWindow)
{
    gHostIsNatron = (getImageEffectHostDescription()->isNatron);


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
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputComponents);
        param->setLabel(kParamOutputComponentsLabel);
        param->setHint(kParamOutputComponentsHint);
        // must be in sync with the end of the plugin construction
        if (supportsAlpha) {
            param->appendOption("Alpha");
        }
        if (supportsRGB) {
            param->appendOption("RGB");
        }
        if (supportsRGBA) {
            param->appendOption("RGBA");
        }
        param->setLayoutHint(eLayoutHintNoNewLine);
        param->setDefault(param->getNOptions() - 1);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessR);
        param->setLabel(kNatronOfxParamProcessRLabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessG);
        param->setLabel(kNatronOfxParamProcessGLabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessB);
        param->setLabel(kNatronOfxParamProcessBLabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kParamProcessHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    //////////Output file
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamFilename);
        param->setLabel(kParamFilenameLabel);
        param->setStringType(eStringTypeFilePath);
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
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormatType);
        param->setLabel(kParamFormatTypeLabel);
        assert(param->getNOptions() == (int)eFormatTypeInput);
        param->appendOption(kParamFormatTypeOptionInput, kParamFormatTypeOptionInputHint);
        assert(param->getNOptions() == (int)eFormatTypeProject);
        param->appendOption(kParamFormatTypeOptionProject, kParamFormatTypeOptionProjectHint);
        assert(param->getNOptions() == (int)eFormatTypeFixed);
        param->appendOption(kParamFormatTypeOptionFixed, kParamFormatTypeOptionFixedHint);
        param->setDefault( (int)eFormatTypeProject );
        param->setAnimates(false);
        param->setHint(kParamFormatTypeHint);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }


    //////////// Output format
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputFormat);
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
        // secret parameters to handle custom formats
        int w = 0, h = 0;
        double par = -1;
        getFormatResolution(eParamFormatHD, &w, &h, &par);
        assert(par != -1);
        {
            Int2DParamDescriptor* param = desc.defineInt2DParam(kParamFormatSize);
            param->setIsSecretAndDisabled(true);
            param->setDefault(w, h);
            if (page) {
                page->addChild(*param);
            }
        }

        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFormatPar);
            param->setIsSecretAndDisabled(true);
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
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamClipToRoD);
        param->setLabel(kParamClipToRoDLabel);
        param->setHint(kParamClipToRoDHint);
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
        BooleanParamDescriptor* param  = desc.defineBooleanParam(kParamOutputSpaceSet);
        param->setEvaluateOnChange(false);
        param->setAnimates(false);
        param->setIsSecretAndDisabled(true);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
    GenericOCIO::describeInContextContext(desc, context, page);
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kOCIOHelpButton);
        param->setLabel(kOCIOHelpButtonLabel);
        param->setHint(kOCIOHelpButtonHint);
        if (page) {
            page->addChild(*param);
        }
    }
#endif

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamInputPremult);
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
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
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
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFrameRange);
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
        IntParamDescriptor* param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        //param->setIsSecretAndDisabled(true); // done in the plugin constructor
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    ////////////Last frame
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamLastFrame);
        param->setLabel(kParamLastFrameLabel);
        //param->setIsSecretAndDisabled(true); // done in the plugin constructor
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // sublabel
    if (gHostIsNatron) {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecretAndDisabled(true); // always secret
        param->setIsPersistent(true);
        param->setEvaluateOnChange(false);
        //param->setDefault();
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param  = desc.defineBooleanParam(kParamGuessedParams);
        param->setEvaluateOnChange(false);
        param->setAnimates(false);
        param->setIsSecretAndDisabled(true);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }


    return page;
} // GenericWriterDescribeInContextBegin

void
GenericWriterDescribeInContextEnd(ImageEffectDescriptor & /*desc*/,
                                  ContextEnum /*context*/,
                                  PageParamDescriptor* /*page*/)
{
}

NAMESPACE_OFX_IO_EXIT
NAMESPACE_OFX_EXIT
