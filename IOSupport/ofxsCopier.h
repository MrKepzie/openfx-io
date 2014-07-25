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
#include "ofxsMaskMix.h"

// Base class for the RGBA and the Alpha processor

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue, bool masked>
class PixelCopier : public OFX::PixelProcessorFilterBase {
public:
    // ctor
    PixelCopier(OFX::ImageEffect &instance)
    : OFX::PixelProcessorFilterBase(instance)
    {}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        int rowBytes = sizeof(PIX) * nComponents * (procWindow.x2 - procWindow.x1);
        float tmpPix[nComponents];
        for(int y = procWindow.y1; y < procWindow.y2; ++y) {
            if(_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, y);
            assert(dstPix);

            if (!masked) {
                const PIX *srcPix = (const PIX *) getSrcPixelAddress(procWindow.x1, y);
                assert(srcPix);
                std::memcpy(dstPix, srcPix, rowBytes);
            } else {
                for (int x = procWindow.x1; x < procWindow.x2; x++) {
                    const PIX *origPix = (const PIX *)  (_origImg ? _origImg->getPixelAddress(x, y) : 0);
                    const PIX *srcPix = (const PIX *) getSrcPixelAddress(x, y);
                    if (srcPix) {
                        std::copy(srcPix, srcPix + nComponents, tmpPix);
                    } else {
                        std::fill(tmpPix, tmpPix + nComponents, 0.); // no src pixel here, be black and transparent
                    }
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, origPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                    // increment the dst pixel
                    dstPix += nComponents;
                }
            }
        }
    }
};

template <class PIX, int nComponents>
class BlackFiller : public OFX::PixelProcessorFilterBase {
public:
    // ctor
    BlackFiller(OFX::ImageEffect &instance)
    : OFX::PixelProcessorFilterBase(instance)
    {}
    
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        int rowSize =  nComponents * (procWindow.x2 - procWindow.x1);
        for(int y = procWindow.y1; y < procWindow.y2; ++y) {
            if(_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, y);
            assert(dstPix);
            std::fill(dstPix, dstPix + rowSize,0);
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
    assert(srcBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= srcBounds.y2);
    assert(srcBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= srcBounds.x2);
    const PIX* srcPixels = srcPixelData + (size_t)(renderWindow.y1 - srcBounds.y1) * srcRowElements + (renderWindow.x1 - srcBounds.x1) * nComponents;
    
    int dstRowElements = dstRowBytes / sizeof(PIX);
    
    PIX* dstPixels = dstPixelData + (size_t)(renderWindow.y1 - dstBounds.y1) * dstRowElements + (renderWindow.x1 - dstBounds.x1) * nComponents;
    
    int rowBytes = sizeof(PIX) * nComponents * (renderWindow.x2 - renderWindow.x1);
    
    for (int y = renderWindow.y1; y < renderWindow.y2; ++y,srcPixels += srcRowElements, dstPixels += dstRowElements) {
        std::memcpy(dstPixels, srcPixels, rowBytes);
    }
}

#endif
