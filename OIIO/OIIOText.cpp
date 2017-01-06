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
 * OIIOText plugin.
 * Write text on images using OIIO.
 */

#include <cfloat> // DBL_MAX

#include "ofxsMacros.h"

GCC_DIAG_OFF(unused-parameter)
#include <OpenImageIO/imageio.h>
/*
   unfortunately, OpenImageIO/imagebuf.h includes OpenImageIO/thread.h,
   which includes boost/thread.hpp,
   which includes boost/system/error_code.hpp,
   which requires the library boost_system to get the symbol boost::system::system_category().

   the following define prevents including error_code.hpp, which is not used anyway.
 */
#define OPENIMAGEIO_THREAD_H
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
GCC_DIAG_ON(unused-parameter)
#include "OIIOGlobal.h"

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsPositionInteract.h"

#include "IOUtility.h"
#include "ofxNatron.h"


using namespace OFX;
using std::string;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TextOIIO"
#define kPluginGrouping "Draw"
#define kPluginDescription  "Use OpenImageIO to write text on images."

#define kPluginIdentifier "fr.inria.openfx.OIIOText"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#ifdef DEBUG
#define kSupportsTiles 1
#pragma message WARN("TextOIIO: tiles support is buggy - enabled in DEBUG mode")
#else
#define kSupportsTiles 0
#pragma message WARN("TextOIIO: tiles support is buggy - disabled in  RELEASE mode")
#endif
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe


#define kParamPosition "position"
#define kParamPositionLabel "Position"
#define kParamPositionHint \
    "The position where starts the baseline of the first character."

#define kParamInteractive "interactive"
#define kParamInteractiveLabel "Interactive"
#define kParamInteractiveHint \
    "When checked the image will be rendered whenever moving the overlay interact instead of when releasing the mouse button."


#define kParamText "text"
#define kParamTextLabel "Text"
#define kParamTextHint \
    "The text that will be drawn on the image"

#define kParamFontSize "fontSize"
#define kParamFontSizeLabel "Size"
#define kParamFontSizeHint \
    "The height of the characters to render in pixels"

#define kParamFontName "fontName"
#define kParamFontNameLabel "Font"
#define kParamFontNameHint \
    "The name of the font to be used. Defaults to some reasonable system font."

#define kParamTextColor "textColor"
#define kParamTextColorLabel "Color"
#define kParamTextColorHint \
    "The color of the text to render"


class OIIOTextPlugin
    : public ImageEffect
{
public:

    OIIOTextPlugin(OfxImageEffectHandle handle);

    virtual ~OIIOTextPlugin();

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const string &paramName) OVERRIDE FINAL;

    /* override changed clip */
    //virtual void changedClip(const InstanceChangedArgs &args, const string &clipName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    //virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

private:

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Double2DParam *_position;
    StringParam *_text;
    IntParam *_fontSize;
    StringParam *_fontName;
    RGBAParam *_textColor;
};

OIIOTextPlugin::OIIOTextPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                         _dstClip->getPixelComponents() == ePixelComponentRGB) );
    _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert( (!_srcClip && getContext() == eContextGenerator) ||
            ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                           _srcClip->getPixelComponents() == ePixelComponentRGB) ) );

    _position = fetchDouble2DParam(kParamPosition);
    _text = fetchStringParam(kParamText);
    _fontSize = fetchIntParam(kParamFontSize);
    _fontName = fetchStringParam(kParamFontName);
    _textColor = fetchRGBAParam(kParamTextColor);
    assert(_position && _text && _fontSize && _fontName && _textColor);

    initOIIOThreads();
}

OIIOTextPlugin::~OIIOTextPlugin()
{
}

static OIIO::ImageSpec
imageSpecFromOFXImage(const OfxRectI &rod,
                      const OfxRectI &bounds,
                      PixelComponentEnum pixelComponents,
                      BitDepthEnum bitDepth)
{
    OIIO::TypeDesc format;
    switch (bitDepth) {
    case eBitDepthUByte:
        format = OIIO::TypeDesc::UINT8;
        break;
    case eBitDepthUShort:
        format = OIIO::TypeDesc::UINT16;
        break;
    case eBitDepthHalf:
        format = OIIO::TypeDesc::HALF;
        break;
    case eBitDepthFloat:
        format = OIIO::TypeDesc::FLOAT;
        break;
    default:
        throwSuiteStatusException(kOfxStatErrFormat);
        break;
    }
    int nchannels = 0, alpha_channel = -1;
    switch (pixelComponents) {
    case ePixelComponentAlpha:
        nchannels = 1;
        alpha_channel = 0;
        break;
    case ePixelComponentRGB:
        nchannels = 3;
        break;
    case ePixelComponentRGBA:
        nchannels = 4;
        alpha_channel = 3;
        break;
    default:
        throwSuiteStatusException(kOfxStatErrFormat);
        break;
    }
    OIIO::ImageSpec spec (format);
    spec.x = bounds.x1;
    spec.y = rod.y2 - bounds.y2;
    spec.width = bounds.x2 - bounds.x1;
    spec.height = bounds.y2 - bounds.y1;
    spec.full_x = rod.x1;
    spec.full_y = 0;
    spec.full_width = rod.x2 - rod.x1;
    spec.full_height = rod.y2 - rod.y1;
    spec.nchannels = nchannels;
    spec.alpha_channel = alpha_channel;

    return spec;
} // imageSpecFromOFXImage

/* Override the render */
void
OIIOTextPlugin::render(const RenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    std::auto_ptr<const Image> srcImg(_srcClip ? _srcClip->fetchImage(args.time) : 0);
    if ( srcImg.get() ) {
        if ( (srcImg->getRenderScale().x != args.renderScale.x) ||
             ( srcImg->getRenderScale().y != args.renderScale.y) ||
             ( srcImg->getField() != args.fieldToRender) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);

            return;
        }
    }

    if (!_dstClip) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    assert(_dstClip);
    std::auto_ptr<Image> dstImg( _dstClip->fetchImage(args.time) );
    if ( !dstImg.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    if ( (dstImg->getRenderScale().x != args.renderScale.x) ||
         ( dstImg->getRenderScale().y != args.renderScale.y) ||
         ( dstImg->getField() != args.fieldToRender) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if ( (dstBitDepth != eBitDepthFloat) || ( srcImg.get() && ( dstBitDepth != srcImg->getPixelDepth() ) ) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ( ( (dstComponents != ePixelComponentRGBA) && (dstComponents != ePixelComponentRGB) && (dstComponents != ePixelComponentAlpha) ) ||
         ( srcImg.get() && ( dstComponents != srcImg->getPixelComponents() ) ) ) {
        throwSuiteStatusException(kOfxStatErrFormat);

        return;
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if ( (args.renderWindow.x1 < dstBounds.x1) || (args.renderWindow.x1 >= dstBounds.x2) || (args.renderWindow.y1 < dstBounds.y1) || (args.renderWindow.y1 >= dstBounds.y2) ||
         ( args.renderWindow.x2 <= dstBounds.x1) || ( args.renderWindow.x2 > dstBounds.x2) || ( args.renderWindow.y2 <= dstBounds.y1) || ( args.renderWindow.y2 > dstBounds.y2) ) {
        throwSuiteStatusException(kOfxStatErrValue);

        return;
        //throw std::runtime_error("render window outside of image bounds");
    }

    OfxRectI srcRod;
    OfxRectI srcBounds;
    PixelComponentEnum pixelComponents = ePixelComponentNone;
    int pixelComponentCount = 0;
    BitDepthEnum bitDepth = eBitDepthNone;
    OIIO::ImageSpec srcSpec;
    std::auto_ptr<const OIIO::ImageBuf> srcBuf;
    OfxRectI dstRod = dstImg->getRegionOfDefinition();
    if ( !srcImg.get() ) {
        setPersistentMessage(Message::eMessageError, "", "Source needs to be connected");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    } else {
        srcRod = srcImg->getRegionOfDefinition();
        srcBounds = srcImg->getBounds();
        pixelComponents = srcImg->getPixelComponents();
        pixelComponentCount = srcImg->getPixelComponentCount();
        bitDepth = srcImg->getPixelDepth();
        srcSpec = imageSpecFromOFXImage(srcRod, srcBounds, pixelComponents, bitDepth);
        srcBuf.reset( new OIIO::ImageBuf( "src", srcSpec, const_cast<void*>( srcImg->getPixelData() ) ) );

        if (!kSupportsTiles) {
            // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
            //  If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one.
            assert(srcRod.x1 == srcBounds.x1);
            assert(srcRod.x2 == srcBounds.x2);
            assert(srcRod.y1 == srcBounds.y1);
            assert(srcRod.y2 == srcBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
            assert(dstRod.x1 == dstBounds.x1);
            assert(dstRod.x2 == dstBounds.x2);
            assert(dstRod.y1 == dstBounds.y1);
            assert(dstRod.y2 == dstBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        }
        if (!kSupportsMultiResolution) {
            // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
            //   Multiple resolution images mean...
            //    input and output images can be of any size
            //    input and output images can be offset from the origin
            assert(srcRod.x1 == 0);
            assert(srcRod.y1 == 0);
            assert(srcRod.x1 == dstRod.x1);
            assert(srcRod.x2 == dstRod.x2);
            assert(srcRod.y1 == dstRod.y1);
            assert(srcRod.y2 == dstRod.y2); // crashes on Natron if kSupportsMultiResolution=0
        }
    }

    double x, y;
    _position->getValueAtTime(args.time, x, y);
    string text;
    _text->getValueAtTime(args.time, text);
    int fontSize;
    _fontSize->getValueAtTime(args.time, fontSize);
    string fontName;
    _fontName->getValueAtTime(args.time, fontName);
    double r, g, b, a;
    _textColor->getValueAtTime(args.time, r, g, b, a);
    float textColor[4];
    textColor[0] = (float)r;
    textColor[1] = (float)g;
    textColor[2] = (float)b;
    textColor[3] = (float)a;

    // allocate temporary image
    int pixelBytes = pixelComponentCount * getComponentBytes(bitDepth);
    int tmpRowBytes = (args.renderWindow.x2 - args.renderWindow.x1) * pixelBytes;
    size_t memSize = (args.renderWindow.y2 - args.renderWindow.y1) * tmpRowBytes;
    ImageMemory mem(memSize, this);
    float *tmpPixelData = (float*)mem.lock();
    const bool flipit = true;
    OIIO::ImageSpec tmpSpec = imageSpecFromOFXImage(srcImg.get() ? srcRod : args.renderWindow, args.renderWindow, pixelComponents, bitDepth);
    assert(tmpSpec.width == args.renderWindow.x2 - args.renderWindow.x1);
    assert(tmpSpec.width == args.renderWindow.x2 - args.renderWindow.x1);
    assert(tmpSpec.height == args.renderWindow.y2 - args.renderWindow.y1);
    OIIO::ROI srcRoi(tmpSpec.x, tmpSpec.x + tmpSpec.width, tmpSpec.y, tmpSpec.y + tmpSpec.height);
    int ytext = int(y * args.renderScale.y);
    if (flipit) {
        // from the OIIO 1.5 release notes:
        // Fixes, minor enhancements, and performance improvements:
        // * ImageBufAlgo:
        //   * flip(), flop(), flipflop() have been rewritten to work more
        //     sensibly for cropped images. In particular, the transformation now
        //     happens with respect to the display (full) window, rather than
        //     simply flipping or flopping within the data window. (1.5.2)
#if OIIO_VERSION >= 10502
        // the transformation happens with respect to the display (full) window
        tmpSpec.y = ( (tmpSpec.full_y + tmpSpec.full_height - 1) - tmpSpec.y ) - (tmpSpec.height - 1);
        tmpSpec.full_y = 0;
        ytext = (tmpSpec.full_y + tmpSpec.full_height - 1) - ytext;
#else
        // only the data window is flipped
        ytext = tmpSpec.y + ( (tmpSpec.y + tmpSpec.height - 1) - ytext );
#endif
    }
    assert(tmpSpec.width == args.renderWindow.x2 - args.renderWindow.x1);
    assert(tmpSpec.height == args.renderWindow.y2 - args.renderWindow.y1);
    OIIO::ImageBuf tmpBuf("tmp", tmpSpec, tmpPixelData);

    // do we have to flip the image?
    if ( !srcImg.get() ) {
        OIIO::ImageBufAlgo::zero(tmpBuf);
    } else {
        if (flipit) {
            bool ok = OIIO::ImageBufAlgo::flip(tmpBuf, *srcBuf, srcRoi);
            if (!ok) {
                setPersistentMessage( Message::eMessageError, "", tmpBuf.geterror().c_str() );
                throwSuiteStatusException(kOfxStatFailed);
            }
        } else {
            // copy the renderWindow from src to a temp buffer
            tmpBuf.copy_pixels(*srcBuf);
        }
    }

    // render text in the temp buffer
    {
        bool ok = OIIO::ImageBufAlgo::render_text(tmpBuf, int(x * args.renderScale.x), ytext, text, int(fontSize * args.renderScale.y), fontName, textColor);
        if (!ok) {
            setPersistentMessage( Message::eMessageError, "", tmpBuf.geterror().c_str() );
            //throwSuiteStatusException(kOfxStatFailed);
        }
    }
    OIIO::ImageSpec dstSpec = imageSpecFromOFXImage(dstRod, dstBounds, pixelComponents, bitDepth);
    OIIO::ImageBuf dstBuf( "dst", dstSpec, dstImg->getPixelData() );

    OIIO::ROI tmpRoi(tmpSpec.x, tmpSpec.x + tmpSpec.width, tmpSpec.y, tmpSpec.y + tmpSpec.height);
    // do we have to flip the image?
    if (flipit) {
        bool ok = OIIO::ImageBufAlgo::flip(dstBuf, tmpBuf, tmpRoi);
        if (!ok) {
            setPersistentMessage( Message::eMessageError, "", tmpBuf.geterror().c_str() );
            throwSuiteStatusException(kOfxStatFailed);
        }
    } else {
        // copy the temp buffer to dstImg
        //dstBuf.copy_pixels(tmpBuf); // erases everything out of tmpBuf!
        bool ok = OIIO::ImageBufAlgo::paste(dstBuf, args.renderWindow.x1, srcRod.y2 - args.renderWindow.y2, 0, 0, tmpBuf, tmpRoi);
        if (!ok) {
            setPersistentMessage( Message::eMessageError, "", tmpBuf.geterror().c_str() );
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // TODO: answer questions:
    // - can we support tiling?
    // - can we support multiresolution by just scaling the coordinates and the font size?
} // OIIOTextPlugin::render

bool
OIIOTextPlugin::isIdentity(const IsIdentityArguments &args,
                           Clip * &identityClip,
                           double & /*identityTime*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return false;
    }

    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();

    string text;
    _text->getValueAtTime(args.time, text);
    if ( text.empty() ) {
        identityClip = _srcClip;

        return true;
    }

    double r, g, b, a;
    _textColor->getValueAtTime(args.time, r, g, b, a);
    if (a == 0.) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

void
OIIOTextPlugin::changedParam(const InstanceChangedArgs &args,
                             const string & /*paramName*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }

    clearPersistentMessage();
}

bool
OIIOTextPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);

        return false;
    }
    if ( _srcClip && _srcClip->isConnected() ) {
        rod = _srcClip->getRegionOfDefinition(args.time);
    } else {
        rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
        rod.x2 = rod.y2 = kOfxFlagInfiniteMax;
    }

    return true;
}

mDeclarePluginFactory(OIIOTextPluginFactory, {}, {});

namespace {
struct PositionInteractParam
{
    static const char * name() { return kParamPosition; }

    static const char * interactiveName() { return kParamInteractive; }
};
}

/** @brief The basic describe function, passed a plugin descriptor */
void
OIIOTextPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles); // may be switched to true later?
    desc.setSupportsMultiResolution(kSupportsMultiResolution); // may be switch to true later? don't forget to reduce font size too
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setOverlayInteractDescriptor(new PositionOverlayDescriptor<PositionInteractParam>);

    desc.setIsDeprecated(true); // this effect was superseeded by the text plugin in openfx-arena
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void
OIIOTextPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum /*context*/)
{
    //gHostIsNatron = (getImageEffectHostDescription()->isNatron);

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Text");
    bool hostHasNativeOverlayForPosition;
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamPosition);
        param->setLabel(kParamPositionLabel);
        param->setHint(kParamPositionHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
        param->setDefault(0.5, 0.5);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setAnimates(true);
        hostHasNativeOverlayForPosition = param->getHostHasNativeOverlayHandle();
        if (hostHasNativeOverlayForPosition) {
            param->setUseHostNativeOverlayHandle(true);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInteractive);
        param->setLabel(kParamInteractiveLabel);
        param->setHint(kParamInteractiveHint);
        param->setAnimates(false);
        //Do not show this parameter if the host handles the interact
        if (hostHasNativeOverlayForPosition) {
            param->setIsSecretAndDisabled(true);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamText);
        param->setLabel(kParamTextLabel);
        param->setHint(kParamTextHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault("Enter text");
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamFontSize);
        param->setLabel(kParamFontSizeLabel);
        param->setHint(kParamFontSizeHint);
        param->setDefault(16);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamFontName);
        param->setLabel(kParamFontNameLabel);
        param->setHint(kParamFontNameHint);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamTextColor);
        param->setLabel(kParamTextColorLabel);
        param->setHint(kParamTextColorHint);
        param->setDefault(1., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // OIIOTextPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
OIIOTextPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new OIIOTextPlugin(handle);
}

static OIIOTextPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
