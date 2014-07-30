/*
 OIIOText plugin.
 Write text on images using OIIO.

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


#include "OIIOText.h"


#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "IOUtility.h"
#include "ofxNatron.h"
#include <OpenImageIO/imageio.h>

#define kPluginName "TextOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription  "Use OpenImageIO to write text on images."

#define kPluginIdentifier "fr.inria.openfx:OIIOText"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kOutputComponentsParamName "outputComponents"
#define kOutputComponentsParamLabel "Output Components"
#define kOutputComponentsHint "The output components where the text will be written to"

#define kEnableClippingParamName "enableClipping"
#define kEnableClippingParamLabel "Enable clipping"
#define kEnableClippingHint "When enabled the text will be clipped to the source image if there's any"

#define kOpacityParamName "opacity"
#define kOpacityParamLabel "Opacity"
#define kOpacityHint "Controls the opacity of the text, works only if output components is set to Alpha or RGBA"

#define kTextParamName "text"
#define kTextParamLabel "Text"
#define kTextHint "The text that will be drawn on the image"

#define kTextPositionParamName "position"
#define kTextPositionParamLabel "Position"
#define kTextPositionHint "The bottom left corner position of the bounding box"

#define kTextRectangleSizeParamName "bboxSize"
#define kTextRectangleSizeParamLabel "Bounding box size"
#define kTextRectangleSizeHint "The width and height of the bounding box"

#define kFontParamName "font"
#define kFontParamLabel "Font"
#define kFontHint "The font used to render the text"

#define kFontSizeParamName "fontSize"
#define kFontSizeParamLabel "Font size"
#define kFontSizeHint "The height of the characters to render in pixels"

#define kFontColorParamName "fontColor"
#define kFontColorParamLabel "Color"
#define kFontColorHint "The color of the text to render"

#define kBoldParamName "bold"
#define kBoldParamLabel "Bold"
#define kBoldHint "Enable bold ?"

#define kItalicParamName "italic"
#define kItalicParamLabel "Italic"
#define kItalicHint "Enable italic ?"

#define kVerticalAlignParamName "vAlign"
#define kVerticalAlignParamLabel "Vertical align"
#define kVericalAlignHint "Controls where the text will be rendered in the bounding box vertically"

#define kHorizontalAlignParamName "hAlign"
#define kHorizontalAlignParamLabel "Horizontal align"
#define kHorizontalAlignHint "Controls where the text will be rendered in the bounding box horizontally"

using namespace OFX;

class OIIOTextPlugin : public OFX::ImageEffect
{
public:

    OIIOTextPlugin(OfxImageEffectHandle handle);

    virtual ~OIIOTextPlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);

    /* override is identity */
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime);

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

    /* override changed clip */
    //virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName);

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);

private:


private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

};

OIIOTextPlugin::OIIOTextPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && (srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA || srcClip_->getPixelComponents() == OFX::ePixelComponentRGB));

    //interpolation_ = fetchChoiceParam(kInterpolationParamName);
    //assert(interpolation_);
}

OIIOTextPlugin::~OIIOTextPlugin()
{
}
/* Override the render */
void
OIIOTextPlugin::render(const OFX::RenderArguments &args)
{
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(srcClip_);
    std::auto_ptr<OFX::Image> srcImg(srcClip_->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (srcImg->getRenderScale().x != args.renderScale.x ||
        srcImg->getRenderScale().y != args.renderScale.y ||
        srcImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OFX::BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcImg->getPixelComponents();

    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat || dstBitDepth != srcBitDepth) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ((dstComponents != OFX::ePixelComponentRGBA && dstComponents != OFX::ePixelComponentRGB && dstComponents != OFX::ePixelComponentAlpha) ||
        dstComponents != srcComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        //throw std::runtime_error("render window outside of image bounds");
    }

    const void* srcPixelData = NULL;
    OfxRectI bounds;
    OFX::PixelComponentEnum pixelComponents;
    OFX::BitDepthEnum bitDepth;
    int srcRowBytes;
    getImageData(srcImg.get(), &srcPixelData, &bounds, &pixelComponents, &bitDepth, &srcRowBytes);

 

   
}

bool
OIIOTextPlugin::isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    
}

void
OIIOTextPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
   
}


mDeclarePluginFactory(OIIOTextPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OIIOTextPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(true);
    desc.setSupportsMultiResolution(true);
    desc.setRenderThreadSafety(eRenderFullySafe);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OIIOTextPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    //gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);
    
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Text");

    ChoiceParamDescriptor* outputComps = desc.defineChoiceParam(kOutputComponentsParamName);
    outputComps->setLabels(kOutputComponentsParamLabel, kOutputComponentsParamLabel, kOutputComponentsParamLabel);
    outputComps->setHint(kOutputComponentsHint);
    outputComps->appendOption("RGBA");
    outputComps->appendOption("RGB");
    outputComps->appendOption("Alpha");
    page->addChild(*outputComps);
    
    BooleanParamDescriptor* clipping = desc.defineBooleanParam(kEnableClippingParamName);
    clipping->setLabels(kEnableClippingParamLabel, kEnableClippingParamLabel, kEnableClippingParamLabel);
    clipping->setDefault(false);
    clipping->setHint(kEnableClippingHint);
    page->addChild(*clipping);
    
    DoubleParamDescriptor* opacity = desc.defineDoubleParam(kOpacityParamName);
    opacity->setLabels(kOpacityParamLabel, kOpacityParamLabel, kOpacityParamLabel);
    opacity->setHint(kOpacityHint);
    opacity->setAnimates(true);
    opacity->setDefault(1.);
    opacity->setRange(0., 1.);
    opacity->setDisplayRange(0., 1.);
    page->addChild(*opacity);
    
    StringParamDescriptor* text = desc.defineStringParam(kTextParamName);
    text->setLabels(kTextParamLabel, kTextParamLabel, kTextParamLabel);
    text->setHint(kTextHint);
    text->setStringType(eStringTypeMultiLine);
    text->setAnimates(true);
    page->addChild(*text);
    
    Double2DParamDescriptor* position = desc.defineDouble2DParam(kTextPositionParamName);
    position->setLabels(kTextPositionParamLabel,kTextPositionParamLabel,kTextPositionParamLabel);
    position->setHint(kTextPositionHint);
    position->setAnimates(true);
    position->setDimensionLabels("x", "y");
    position->setDoubleType(eDoubleTypeXYAbsolute);
    page->addChild(*position);
    
    Double2DParamDescriptor* size = desc.defineDouble2DParam(kTextRectangleSizeParamName);
    size->setHint(kTextRectangleSizeHint);
    size->setLabels(kTextRectangleSizeParamLabel, kTextRectangleSizeParamLabel, kTextRectangleSizeParamLabel);
    size->setAnimates(true);
    size->setDimensionLabels("width", "height");
    size->setDoubleType(eDoubleTypeXYAbsolute);
    page->addChild(*size);
    
    ChoiceParamDescriptor* font = desc.defineChoiceParam(kFontParamName);
    font->setLabels(kFontParamLabel, kFontParamLabel, kFontParamLabel);
    font->setHint(kFontHint);
    
    
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OIIOTextPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OIIOTextPlugin(handle);
}


void getOIIOTextPluginID(OFX::PluginFactoryArray &ids)
{
    static OIIOTextPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
