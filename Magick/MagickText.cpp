/*
 MagickText

 Write text using ImageMagick.

 Written by Ole-André Rodlie <olear@fxarena.net> <https://github.com/olear>

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

#include "MagickText.h"

#include "ofxsPositionInteract.h"
#include "ofxsMacros.h"

#include <sstream>
#include <iostream>
#include <stdint.h>

#include <Magick++.h>
#include <magick/MagickCore.h>

#define CLAMP(value, min, max) (((value) >(max)) ? (max) : (((value) <(min)) ? (min) : (value)))

#define kPluginName "Text"
#define kPluginGrouping "Image"
#define kPluginDescription  "Write text"

#define kPluginIdentifier "fr.inria.openfx.MagickText"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderInstanceSafe

#define kParamPosition "position"
#define kParamPositionLabel "Position"
#define kParamPositionHint "The position of the first character on the first line."

#define kParamInteractive "interactive"
#define kParamInteractiveLabel "Interactive"
#define kParamInteractiveHint "When checked the image will be rendered whenever moving the overlay interact instead of when releasing the mouse button."

#define kParamText "text"
#define kParamTextLabel "Text"
#define kParamTextHint "The text that will be drawn"

#define kParamFontSize "fontSize"
#define kParamFontSizeLabel "Size"
#define kParamFontSizeHint "The height of the characters to render in pixels"

#define kParamFontName "fontName"
#define kParamFontNameLabel "Font"
#define kParamFontNameHint "The name of the font to be used. If empty then you need to update your font cache."

#define kParamFontDecor "fontDecor"
#define kParamFontDecorLabel "Decoration"
#define kParamFontDecorHint "Font decoration."

#define kParamTextColor "textColor"
#define kParamTextColorLabel "Fill Color"
#define kParamTextColorHint "The fill color of the text to render"

#define kParamStrokeCheck "strokeCheck"
#define kParamStrokeCheckLabel "Stroke"
#define kParamStrokeCheckHint "Enable or disable outline"
#define kParamStrokeCheckDefault false

#define kParamStrokeColor "strokeColor"
#define kParamStrokeColorLabel "Stroke Color"
#define kParamStrokeColorHint "The stroke color of the text to render"

#define kParamStroke "stroke"
#define kParamStrokeLabel "Stroke Width"
#define kParamStrokeHint "Adjust stroke width for outline"
#define kParamStrokeDefault 1

using namespace OFX;

class MagickTextPlugin : public OFX::ImageEffect
{
public:
    MagickTextPlugin(OfxImageEffectHandle handle);
    virtual ~MagickTextPlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

private:


private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Double2DParam *position_;
    OFX::StringParam *text_;
    OFX::IntParam *fontSize_;
    OFX::ChoiceParam *fontName_;
    OFX::ChoiceParam *fontDecor_;
    OFX::RGBAParam *textColor_;
    OFX::RGBAParam *strokeColor_;
    OFX::BooleanParam *strokeEnabled_;
    OFX::DoubleParam *strokeWidth_;
};

MagickTextPlugin::MagickTextPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
{
    Magick::InitializeMagick("");
    MagickCore::MagickCoreGenesis( NULL, MagickCore::MagickTrue );

    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentRGB));

    position_ = fetchDouble2DParam(kParamPosition);
    text_ = fetchStringParam(kParamText);
    fontSize_ = fetchIntParam(kParamFontSize);
    fontName_ = fetchChoiceParam(kParamFontName);
    fontDecor_ = fetchChoiceParam(kParamFontDecor);
    textColor_ = fetchRGBAParam(kParamTextColor);
    strokeColor_ = fetchRGBAParam(kParamStrokeColor);
    strokeEnabled_ = fetchBooleanParam(kParamStrokeCheck);
    strokeWidth_ = fetchDoubleParam(kParamStroke);
    assert(position_ && text_ && fontSize_ && fontName_ && textColor_ && fontDecor_ && strokeColor_ && strokeEnabled_ && strokeWidth_);
}

MagickTextPlugin::~MagickTextPlugin()
{
}

/* Override the render */
void MagickTextPlugin::render(const OFX::RenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
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

    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ((dstComponents != OFX::ePixelComponentRGBA && dstComponents != OFX::ePixelComponentRGB && dstComponents != OFX::ePixelComponentAlpha)) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        return;
        //throw std::runtime_error("render window outside of image bounds");
    }

    // Get params
    double x, y;
    position_->getValueAtTime(args.time, x, y);
    std::string text;
    text_->getValueAtTime(args.time, text);
    int fontSize;
    fontSize_->getValueAtTime(args.time, fontSize);
    int fontName;
    fontName_->getValueAtTime(args.time, fontName);
    int fontDecor;
    fontDecor_->getValueAtTime(args.time, fontDecor);
    double r, g, b, a;
    textColor_->getValueAtTime(args.time, r, g, b, a);
    float textColor[4];
    textColor[0] = (float)r;
    textColor[1] = (float)g;
    textColor[2] = (float)b;
    textColor[3] = (float)a;
    double r_s, g_s, b_s, a_s;
    strokeColor_->getValueAtTime(args.time, r_s, g_s, b_s, a_s);
    float strokeColor[4];
    strokeColor[0] = (float)r_s;
    strokeColor[1] = (float)g_s;
    strokeColor[2] = (float)b_s;
    strokeColor[3] = (float)a_s;
    bool use_stroke = false;
    strokeEnabled_->getValueAtTime(args.time, use_stroke);
    double strokeWidth;
    strokeWidth_->getValueAtTime(args.time, strokeWidth);

    // Get font
    std::string fontFile;
    char **fonts;
    std::size_t fontList;
    fonts=MagickCore::MagickQueryFonts("*",&fontList);
    fontFile = fonts[fontName];

    for (size_t i = 0; i < fontList; i++)
        free(fonts[i]);

    // setup
    int magickWidth = args.renderWindow.x2 - args.renderWindow.x1;
    int magickHeight = args.renderWindow.y2 - args.renderWindow.y1;
    int magickWidthStep = magickWidth*4;
    int magickSize = magickWidth*magickHeight*4;
    float* magickBlock;
    magickBlock = new float[magickSize];

    // Generate empty image
    Magick::Image magickImage(Magick::Geometry(magickWidth,magickHeight),Magick::Color("rgba(0,0,0,0)"));

    // Set font size
    magickImage.fontPointsize(fontSize);

    // Set stroke width
    magickImage.strokeWidth(strokeWidth);

    // Convert colors to int
    int rI = ((uint8_t)(255.0f *CLAMP(r, 0.0, 1.0)));
    int gI = ((uint8_t)(255.0f *CLAMP(g, 0.0, 1.0)));
    int bI = ((uint8_t)(255.0f *CLAMP(b, 0.0, 1.0)));
    int r_sI = ((uint8_t)(255.0f *CLAMP(r_s, 0.0, 1.0)));
    int g_sI = ((uint8_t)(255.0f *CLAMP(g_s, 0.0, 1.0)));
    int b_sI = ((uint8_t)(255.0f *CLAMP(b_s, 0.0, 1.0)));

    std::ostringstream rgba;
    rgba << "rgba(" << rI <<"," << gI << "," << bI << "," << a << ")";
    std::string textRGBA = rgba.str();
    std::ostringstream rgba_s;
    rgba_s << "rgba(" << r_sI <<"," << g_sI << "," << b_sI << "," << a_s << ")";
    std::string strokeRGBA = rgba_s.str();

    // Flip image
    magickImage.flip();

    // Position x y
    OfxRectI rod,bounds;
    rod = dstImg->getRegionOfDefinition();
    bounds = dstImg->getBounds();
    int ytext = y*args.renderScale.y;
    int xtext = x*args.renderScale.x;
    int tmp_y = rod.y2 - bounds.y2;
    int tmp_height = bounds.y2 - bounds.y1;
    ytext = tmp_y + ((tmp_y+tmp_height-1) - ytext);

    // Setup draw
    std::list<Magick::Drawable> text_draw_list;
    text_draw_list.push_back(Magick::DrawableFont(fontFile));
    text_draw_list.push_back(Magick::DrawableText(xtext, ytext, text));
    text_draw_list.push_back(Magick::DrawableFillColor(textRGBA));
    if (use_stroke)
        text_draw_list.push_back(Magick::DrawableStrokeColor(strokeRGBA));

    // Text decoration
    // TODO: adjust size on decor
    if (fontDecor>0) {
        switch(fontDecor) {
        case 1:
            text_draw_list.push_back(Magick::DrawableTextDecoration(Magick::UnderlineDecoration));
            break;
        case 2:
            text_draw_list.push_back(Magick::DrawableTextDecoration(Magick::OverlineDecoration));
            break;
        case 3:
            text_draw_list.push_back(Magick::DrawableTextDecoration(Magick::LineThroughDecoration));
            break;
        default:
            text_draw_list.push_back(Magick::DrawableTextDecoration(Magick::NoDecoration));
            break;
        }
    }

    // Draw
    magickImage.draw(text_draw_list);

    // Flip image
    magickImage.flip();

    // Return
    magickImage.write(0,0,magickWidth,magickHeight,"RGBA",Magick::FloatPixel,magickBlock);
    for(int y = args.renderWindow.y1; y < (args.renderWindow.y1 + magickHeight); y++) {
        OfxRGBAColourF *dstPix = (OfxRGBAColourF *)dstImg->getPixelAddress(args.renderWindow.x1, y);
        float *srcPix = (float*)(magickBlock + y * magickWidthStep + args.renderWindow.x1);
        for(int x = args.renderWindow.x1; x < (args.renderWindow.x1 + magickWidth); x++) {
            dstPix->r = srcPix[0];
            dstPix->g = srcPix[1];
            dstPix->b = srcPix[2];
            dstPix->a = srcPix[3];
            dstPix++;
            srcPix+=4;
        }
    }
    free(magickBlock);
}

void MagickTextPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &/*paramName*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    clearPersistentMessage();
}

bool MagickTextPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }

    rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
    rod.x2 = rod.y2 = kOfxFlagInfiniteMax;

    return true;
}

mDeclarePluginFactory(MagickTextPluginFactory, {}, {});

namespace {
struct PositionInteractParam {
    static const char *name() { return kParamPosition; }
    static const char *interactiveName() { return kParamInteractive; }
};
}

/** @brief The basic describe function, passed a plugin descriptor */
void MagickTextPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths TODO: add support for less bits
    /*desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);*/
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setOverlayInteractDescriptor(new PositionOverlayDescriptor<PositionInteractParam>);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void MagickTextPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
{   
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
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
        param->setAnimates(true);
        hostHasNativeOverlayForPosition = param->getHostHasNativeOverlayHandle();
        if (hostHasNativeOverlayForPosition) {
            param->setUseHostOverlayHandle(true);
        }
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInteractive);
        param->setLabel(kParamInteractiveLabel);
        param->setHint(kParamInteractiveHint);
        param->setAnimates(false);
        page->addChild(*param);
        
        //Do not show this parameter if the host handles the interact
        if (hostHasNativeOverlayForPosition) {
            param->setIsSecret(true);
        }
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamText);
        param->setLabel(kParamTextLabel);
        param->setHint(kParamTextHint);
        param->setStringType(eStringTypeMultiLine);
        param->setAnimates(true);
        param->setDefault("Enter text");
        page->addChild(*param);
    }
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamFontSize);
        param->setLabel(kParamFontSizeLabel);
        param->setHint(kParamFontSizeHint);
        param->setDefault(32);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFontName);
        param->setLabel(kParamFontNameLabel);
        param->setHint(kParamFontNameHint);

        // Get all fonts
        char **fonts;
        std::size_t fontList;
        fonts=MagickCore::MagickQueryFonts("*",&fontList);
        for (size_t i=0;i<fontList;i++)
          param->appendOption(fonts[i]);

        for (size_t i = 0; i < fontList; i++)
            free(fonts[i]);

        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFontDecor);
        param->setLabel(kParamFontDecorLabel);
        param->setHint(kParamFontDecorHint);
        param->appendOption("None");
        param->appendOption("Underline");
        param->appendOption("Overline");
        param->appendOption("Strike-through");
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamTextColor);
        param->setLabel(kParamTextColorLabel);
        param->setHint(kParamTextColorHint);
        param->setDefault(1., 1., 1., 1.);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamStrokeCheck);
        param->setLabel(kParamStrokeCheckLabel);
        param->setHint(kParamStrokeCheckHint);
        param->setEvaluateOnChange(true);
        param->setDefault(kParamStrokeCheckDefault);
        page->addChild(*param);
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStrokeColor);
        param->setLabel(kParamStrokeColorLabel);
        param->setHint(kParamStrokeColorHint);
        param->setDefault(1., 1., 1., 1.);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamStroke);
        param->setLabel(kParamStrokeLabel);
        param->setHint(kParamStrokeHint);
        param->setRange(0, 20);
        param->setDisplayRange(0, 20);
        param->setDefault(kParamStrokeDefault);
        page->addChild(*param);
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* MagickTextPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new MagickTextPlugin(handle);
}

void getMagickTextPluginID(OFX::PluginFactoryArray &ids)
{
    static MagickTextPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
