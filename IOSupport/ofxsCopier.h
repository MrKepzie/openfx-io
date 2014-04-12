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
        for(int y = procWindow.y1; y < procWindow.y2; y++) {
            if(_effect.abort()) break;

            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, y);
            const PIX *srcPix = (const PIX *) getSrcPixelAddress(procWindow.x1, y);
            std::memcpy(dstPix, srcPix, rowBytes);
        }
    }
};


template <class PIX, int nComponents>
class PixelScaler : public OFX::PixelScalerProcessorFilterBase {
    public :
    // ctor
    PixelScaler(OFX::ImageEffect &instance)
    : OFX::PixelScalerProcessorFilterBase(instance)
    {}
    
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(procWindow.x1 == _dstBounds.x1 && procWindow.x2 == _dstBounds.x2);
        for(int y = procWindow.y1; y < procWindow.y2; ++y) {
            double srcY = (double)y / _scale.y;
            int fy = std::floor(srcY);
            int cy = std::ceil(srcY);
            double dy = std::max(0., std::min(srcY - fy, 1.));
            
            const PIX *srcPixFloor = (const PIX *) getSrcPixelAddress(_srcBounds.x1, fy);
            const PIX *srcPixCeil = (const PIX *) getSrcPixelAddress(_srcBounds.x1, cy);
            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double srcX = (double)x / _scale.x;
                int fx = std::floor(srcX);
                int cx = std::ceil(srcX);
                double dx = std::max(0., std::min(srcX - fx, 1.));
                
                for (int i = 0; i < nComponents ;++i) {
                    
                    const double Icc = (!srcPixFloor || fx < _srcBounds.x1) ? 0. : srcPixFloor[fx * nComponents + i];
                    const double Inc = (!srcPixFloor || cx >= _srcBounds.x2) ? 0. : srcPixFloor[cx * nComponents + i];
                    const double Icn = (!srcPixCeil || fx < _srcBounds.x1) ? 0. : srcPixCeil[fx * nComponents + i];
                    const double Inn = (!srcPixCeil || cx >= _srcBounds.x2) ? 0. : srcPixCeil[cx * nComponents + i];
                    *dstPix++ = Icc + dx*(Inc-Icc + dy*(Icc+Inn-Icn-Inc)) + dy*(Icn-Icc);
                }

            }
        }
    }
};


#endif
