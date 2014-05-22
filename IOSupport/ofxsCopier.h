//
//  ofxsCopier.h
//  IO
//
//  Created by Frédéric Devernay on 03/02/2014.
//
//

#ifndef IO_ofxsCopier_h
#define IO_ofxsCopier_h

#include <cstring>
#include "ofxsPixelProcessor.h"

// Base class for the RGBA and the Alpha processor

// template to do the RGBA processing
template <class PIX, int nComponents>
class PixelCopier : public OFX::PixelProcessorFilterBase {
    public :
    // ctor
    PixelCopier(OFX::ImageEffect &instance)
    : OFX::PixelProcessorFilterBase(instance)
    {}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        int rowBytes = sizeof(PIX) * nComponents * (procWindow.x2 - procWindow.x1);
        for(int y = procWindow.y1; y < procWindow.y2; ++y) {
            if(_effect.abort()) break;

            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, y);
            const PIX *srcPix = (const PIX *) getSrcPixelAddress(procWindow.x1, y);
            assert(srcPix && dstPix);
            std::memcpy(dstPix, srcPix, rowBytes);
        }
    }
};

template<class PIX,int nComponents>
void copyPixels(const OfxRectI& renderWindow,
                const PIX *srcPixelData,
                const OfxRectI& srcBounds,
                OFX::PixelComponentEnum srcPixelComponents,
                OFX::BitDepthEnum srcPixelDepth,
                int srcRowBytes,
                PIX *dstPixelData,
                const OfxRectI& dstBounds,
                OFX::PixelComponentEnum dstPixelComponents,
                OFX::BitDepthEnum dstBitDepth,
                int dstRowBytes)
{
    int srcRowElements = srcRowBytes / sizeof(PIX);
    
    const PIX* srcPixels = srcPixelData + (renderWindow.y1 - srcBounds.y1) * srcRowElements + (renderWindow.x1 - srcBounds.x1) * nComponents;
    
    int dstRowElements = dstRowBytes / sizeof(PIX);
    
    PIX* dstPixels = dstPixelData + (renderWindow.y1 - dstBounds.y1) * dstRowElements + (renderWindow.x1 - dstBounds.x1) * nComponents;
    
    int rowBytes = sizeof(PIX) * nComponents * (renderWindow.x2 - renderWindow.x1);
    
    for (int y = renderWindow.y1; y < renderWindow.y2; ++y,srcPixels += srcRowElements, dstPixels += dstRowElements) {
        std::memcpy(dstPixels, srcPixels, rowBytes);
    }
}

#endif
