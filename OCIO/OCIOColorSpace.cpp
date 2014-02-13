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


#include "OCIOColorSpace.h"

//#include <iostream>

#include <GenericOCIO.h>


#include "ofxsProcessing.H"
#include "ofxsCopier.h"

OCIOColorSpacePlugin::OCIOColorSpacePlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, _ocio(new GenericOCIO(this))
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
}

OCIOColorSpacePlugin::~OCIOColorSpacePlugin()
{
    
}

// make sure components are sane
static void
checkComponents(const OFX::Image &src,
                OFX::BitDepthEnum dstBitDepth,
                OFX::PixelComponentEnum dstComponents)
{
    OFX::BitDepthEnum      srcBitDepth     = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
}

void
OCIOColorSpacePlugin::setupAndProcess(CopierBase & processor, const OFX::RenderArguments &args, OFX::Image* srcImg, OFX::Image* dstImg)
{
    // set the images
    processor.setDstImg(dstImg);
    processor.setSrcImg(srcImg);

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

/* Override the render */
void
OCIOColorSpacePlugin::render(const OFX::RenderArguments &args)
{
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<OFX::Image> srcImg(srcClip_->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcImg->getPixelComponents();

    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
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
    OfxRectI bounds = dstImg->getBounds();
    if(args.renderWindow.x1 < bounds.x1 || args.renderWindow.x1 >= bounds.x2 || args.renderWindow.y1 < bounds.y1 || args.renderWindow.y1 >= bounds.y2 ||
       args.renderWindow.x2 <= bounds.x1 || args.renderWindow.x2 > bounds.x2 || args.renderWindow.y2 <= bounds.y1 || args.renderWindow.y2 > bounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        //throw std::runtime_error("render window outside of image bounds");
    }

    if(dstComponents == OFX::ePixelComponentRGBA) {
        ImageCopier<float, 4> fred(*this);
        setupAndProcess(fred, args,srcImg.get(),dstImg.get());
    } else if(dstComponents == OFX::ePixelComponentRGB) {
        ImageCopier<float, 3> fred(*this);
        setupAndProcess(fred, args,srcImg.get(),dstImg.get());
    }  else if(dstComponents == OFX::ePixelComponentAlpha) {
        ImageCopier<float, 1> fred(*this);
        setupAndProcess(fred, args,srcImg.get(),dstImg.get());
    } // switch

    ///do the color-space conversion
    _ocio->apply(args.renderWindow, dstImg.get());
}

bool
OCIOColorSpacePlugin::isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    return _ocio->isIdentity();
}

/** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
void
OCIOColorSpacePlugin::beginEdit() {
    return _ocio->beginEdit();
}

void
OCIOColorSpacePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    return _ocio->changedParam(args, paramName);
}

using namespace OFX;


/** @brief The basic describe function, passed a plugin descriptor */
void OCIOColorSpacePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("OCIOColorSpaceOFX", "OCIOColorSpaceOFX", "OCIOColorSpaceOFX");
    desc.setPluginGrouping("Color");
    desc.setPluginDescription("ColorSpace transformation using OpenColorIO configuration file.");

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSupportsTiles(true);
    desc.setRenderThreadSafety(eRenderFullySafe);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OCIOColorSpacePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
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
    // insert OCIO parameters
    GenericOCIO::describeInContext(desc, context, page, OCIO_NAMESPACE::ROLE_REFERENCE, OCIO_NAMESPACE::ROLE_REFERENCE);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OCIOColorSpacePluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new OCIOColorSpacePlugin(handle);
}
