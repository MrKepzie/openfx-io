/*
 OIIOResize plugin.
 Resize images using OIIO.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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


#include "OIIOResize.h"


#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "../SupportExt/ofxsFormatResolution.h"
#include <OpenImageIO/imageio.h>


#define kPluginName "OIIOResizeOFX"
#define kPluginGrouping "Transform/OIIOResize"
#define kPluginDescription  "Use OpenImageIO to resize images."

#define kPluginIdentifier "fr.inria.openfx:OIIOResize"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kTypeParamName "type"
#define kTypeParamLabel "Type"
#define kTypeParamHint "Format: Converts between formats, the image is resized to fit in the target format. "
"Size: Scales to fit into a box of a given width and height. "
"Scale: Scales the image."

#define kFormatParamName "format"
#define kFormatParamLabel "Format"
#define kFormatParamHint "The output format"

#define kSizeParamName "size"
#define kSizeParamLabel "Size"
#define kSizeParamHint "The output size"

#define kPreserveParParamName "keepPAR"
#define kPreserveParParamLabel "Preserve aspect ratio"
#define kPreserveParParamHint "When checked, one direction is either clipped or padded."

#define kScaleParamName "scale"
#define kScaleParamLabel "Scale"
#define kScaleParamHint "The scale factor to apply to the image."

#define kFilterParamName "filter"
#define kFilterParamLabel "Filter"
#define kFilterParamHint "The filter used to resize."

#define kFilterImpulse "impulse"
#define kFilterBox "box"
#define kFilterGaussian "gaussian"
#define kFilterCubic "cubic"
#define kFilterKeys "keys"
#define kFilterSimon "simon"
#define kFilterRifman "rifman"
#define kFilterMitchell "mitchell"
#define kFilterLanczos3 "lanczos3"
#define kFilterBlackmanHarris "blackman-harris"

using namespace OFX;
class OIIOResizePlugin : public OFX::ImageEffect
{
public:

    OIIOResizePlugin(OfxImageEffectHandle handle);

    virtual ~OIIOResizePlugin();

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

    //OFX::ChoiceParam *interpolation_;
};

OIIOResizePlugin::OIIOResizePlugin(OfxImageEffectHandle handle)
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

OIIOResizePlugin::~OIIOResizePlugin()
{
}
/* Override the render */
void
OIIOResizePlugin::render(const OFX::RenderArguments &args)
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
    }

}

bool
OIIOResizePlugin::isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{

    return false;
}


void
OIIOResizePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
 

}

using namespace OFX;

mDeclarePluginFactory(OIIOResizePluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void OIIOResizePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(OFX::eBitDepthUByte);
    desc.addSupportedBitDepth(OFX::eBitDepthUShort);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSupportsTiles(true);
    desc.setRenderThreadSafety(eRenderFullySafe);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OIIOResizePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
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
    PageParamDescriptor *page = desc.definePageParam("Controls");

    ChoiceParamDescriptor* type = desc.defineChoiceParam(kTypeParamName);
    type->setLabels(kTypeParamLabel, kTypeParamLabel, kTypeParamLabel);
    type->setHint(kTypeParamHint);
    type->appendOption("Format");
    type->appendOption("Size");
    type->appendOption("Scale");
    type->setAnimates(false);
    type->setDefault(0);
    page->addChild(*type);
    
    ChoiceParamDescriptor* format = desc.defineChoiceParam("Format");
    format->setLabels(kFormatParamLabel, kFormatParamLabel, kFormatParamLabel);
    format->setAnimates(false);
    format->appendOption(kParamFormatPCVideoLabel);
    format->appendOption(kParamFormatNTSCLabel);
    format->appendOption(kParamFormatPALLabel);
    format->appendOption(kParamFormatHDLabel);
    format->appendOption(kParamFormatNTSC169Label);
    format->appendOption(kParamFormatPAL169Label);
    format->appendOption(kParamFormat1kSuper35Label);
    format->appendOption(kParamFormat1kCinemascopeLal);
    format->appendOption(kParamFormat2kSuper35Label);
    format->appendOption(kParamFormat2kCinemascopeLal);
    format->appendOption(kParamFormat4kSuper35Label);
    format->appendOption(kParamFormat4kCinemascopeLal);
    format->appendOption(kParamFormatSquare256Label);
    format->appendOption(kParamFormatSquare512Label);
    format->appendOption(kParamFormatSquare1kLabel);
    format->appendOption(kParamFormatSquare2kLabel);
    format->setDefault(0);
    format->setHint(kFormatParamHint);
    page->addChild(*format);
    
    Int2DParamDescriptor* size = desc.defineInt2DParam(kSizeParamName);
    size->setLabels(kSizeParamLabel, kSizeParamLabel, kSizeParamLabel);
    size->setHint(kSizeParamHint);
    size->setDefault(200, 200);
    size->setAnimates(false);
    size->setIsSecret(true);
    size->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*size);
    
    BooleanParamDescriptor* preservePAR = desc.defineBooleanParam(kPreserveParParamName);
    preservePAR->setLabels(kPreserveParParamLabel, kPreserveParParamLabel, kPreserveParParamLabel);
    preservePAR->setHint(kPreserveParParamHint);
    preservePAR->setAnimates(false);
    preservePAR->setDefault(false);
    preservePAR->setIsSecret(true);
    page->addChild(*preservePAR);
    
    DoubleParamDescriptor* scale = desc.defineDoubleParam(kScaleParamName);
    scale->setHint(kScaleParamHint);
    scale->setLabels(kScaleParamLabel, kScaleParamLabel, kScaleParamLabel);
    scale->setAnimates(true);
    scale->setIsSecret(true);
    page->addChild(*scale);
    
    ChoiceParamDescriptor *filter = desc.defineChoiceParam(kFilterParamName);
    filter->setLabels(kFilterParamLabel, kFilterParamLabel, kFilterParamLabel);
    filter->setHint(kFilterParamHint);
    filter->setAnimates(false);
    filter->appendOption(kFilterImpulse);
    filter->appendOption(kFilterBox);
    filter->appendOption(kFilterGaussian);
    filter->appendOption(kFilterCubic);
    filter->appendOption(kFilterKeys);
    filter->appendOption(kFilterSimon);
    filter->appendOption(kFilterRifman);
    filter->appendOption(kFilterMitchell);
    filter->appendOption(kFilterLanczos3);
    filter->appendOption(kFilterBlackmanHarris);
    filter->setDefault(3);
    page->addChild(*filter);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OIIOResizePluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OIIOResizePlugin(handle);
}


void getOIIOResizePluginID(OFX::PluginFactoryArray &ids)
{
    static OIIOResizePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
