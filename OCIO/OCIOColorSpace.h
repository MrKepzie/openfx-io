/*
 OCIOColorSpace plugin.
 Convert from one colorspace to another.

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
#ifndef __Io__OCIOColorSpace__
#define __Io__OCIOColorSpace__

#include <ofxsImageEffect.h>
#include "IOUtility.h"

class GenericOCIO;
namespace OFX {
    class PixelProcessorFilterBase;
}

class OCIOColorSpacePlugin : public OFX::ImageEffect 
{
public:
    
    OCIOColorSpacePlugin(OfxImageEffectHandle handle);
    
    virtual ~OCIOColorSpacePlugin();
    
  /* Override the render */
  virtual void render(const OFX::RenderArguments &args);

  /* override is identity */
  virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime);

  /** @brief the effect is about to be actively edited by a user, called when the first user interface is opened on an instance */
  virtual void beginEdit(void);

  /* override changedParam */
  virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

  /* override changed clip */
  //virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName);

  // override the rod call
  //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);

  // override the roi call
  //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);

private :
    void copyPixelData(const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       OFX::Image* dstImg)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        copyPixelData(renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
                       OFX::BitDepthEnum srcBitDepth,
                       int srcRowBytes,
                       OFX::Image* dstImg)
    {
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        copyPixelData(renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       OFX::BitDepthEnum dstBitDepth,
                       int dstRowBytes)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        copyPixelData(renderWindow,
                      srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                      dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
                       OFX::BitDepthEnum srcPixelDepth,
                       int srcRowBytes,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       OFX::BitDepthEnum dstBitDepth,
                       int dstRowBytes);

  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;

  GenericOCIO* _ocio;
};

mDeclarePluginFactory(OCIOColorSpacePluginFactory, {}, {});

#endif /* defined(__Io__OCIOColorSpace__) */
