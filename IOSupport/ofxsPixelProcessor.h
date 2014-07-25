#ifndef _ofxsPixelProcessor_h_
#define _ofxsPixelProcessor_h_

/*
 ofxsPixelProcessor: generic multithreaded OFX pixel processor
 
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

#include <cassert>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "IOUtility.h"

/** @file This file contains a useful base class that can be used to process images 

The code below is not so much a skin on the base OFX classes, but code used in implementing 
specific image processing algorithms. As such it does not sit in the support include lib, but in
its own include directory.
*/

namespace OFX {

    ////////////////////////////////////////////////////////////////////////////////
    // base class to process images with
    class PixelProcessor : public OFX::MultiThread::Processor {
    protected:
        OFX::ImageEffect &_effect;      /**< @brief effect to render with */
        //OFX::Image       *_dstImg;        /**< @brief image to process into */
        void* _dstPixelData;
        OfxRectI _dstBounds;
        OFX::PixelComponentEnum _dstPixelComponents;
        OFX::BitDepthEnum _dstBitDepth;
        int _dstPixelBytes;
        int _dstRowBytes;
        OfxRectI          _renderWindow;  /**< @brief render window to use */

    public:
        /** @brief ctor */
        PixelProcessor(OFX::ImageEffect &effect)
          : _effect(effect)
          , _dstPixelData(0)
          , _dstBounds()
          , _dstPixelComponents(OFX::ePixelComponentNone)
          , _dstBitDepth(OFX::eBitDepthNone)
          , _dstPixelBytes(0)
          , _dstRowBytes(0)
        {
            _renderWindow.x1 = _renderWindow.y1 = _renderWindow.x2 = _renderWindow.y2 = 0;
        }  
        
        /** @brief set the destination image */
        void setDstImg(OFX::Image *v)
        {
            _dstPixelData = v->getPixelData();
            _dstBounds = v->getBounds();
            _dstPixelComponents = v->getPixelComponents();
            _dstBitDepth = v->getPixelDepth();
            _dstPixelBytes = getPixelBytes(_dstPixelComponents, _dstBitDepth);
            _dstRowBytes = v->getRowBytes();
        }

        /** @brief set the destination image */
        void setDstImg(void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       OFX::BitDepthEnum dstPixelDepth,
                       int dstRowBytes)
        {
            _dstPixelData = dstPixelData;
            _dstBounds = dstBounds;
            _dstPixelComponents = dstPixelComponents;
            _dstBitDepth = dstPixelDepth;
            _dstPixelBytes = getPixelBytes(_dstPixelComponents, _dstBitDepth);
            _dstRowBytes = dstRowBytes;
        }

        /** @brief reset the render window */
        void setRenderWindow(OfxRectI rect) {_renderWindow = rect;}

        /** @brief overridden from OFX::MultiThread::Processor. This function is called once on each SMP thread by the base class */
        void multiThreadFunction(unsigned int threadId, unsigned int nThreads)
        {
            // slice the y range into the number of threads it has
            unsigned int dy = _renderWindow.y2 - _renderWindow.y1;
            // the following is equivalent to std::ceil(dy/(double)nThreads);
            unsigned int h = (dy+nThreads-1)/nThreads;
            if (h == 0) {
                // there are more threads than lines to process
                h = 1;
            }
            if (threadId * h >= dy) {
                // empty render subwindow
                return;
            }
            unsigned int y1 = _renderWindow.y1 + threadId * h;
            
            unsigned int step = (threadId + 1) * h;
            unsigned int y2 = _renderWindow.y1 + (step < dy ? step : dy);
            
            OfxRectI win = _renderWindow;
            win.y1 = y1; win.y2 = y2;
            
            // and render that thread on each
            multiThreadProcessImages(win);
        }
        
        /** @brief called before any MP is done */
        virtual void preProcess(void) {}

        /** @brief this is called by multiThreadFunction to actually process images, override in derived classes */
        virtual void multiThreadProcessImages(OfxRectI window) = 0;

        /** @brief called before any MP is done */
        virtual void postProcess(void) {}

        /** @brief called to process everything */
        virtual void process(void)
        {
            // is it OK ?
            if(!_dstPixelData || (_renderWindow.x2 - _renderWindow.x1 == 0 && _renderWindow.y2 - _renderWindow.y1))
                return;

            // call the pre MP pass
            preProcess();

            // call the base multi threading code, should put a pre & post thread calls in too
            multiThread();

            // call the post MP pass
            postProcess();
        }
       
  protected:
        void* getDstPixelAddress(int x, int y) const
        {
            // are we in the image bounds
            if(x < _dstBounds.x1 || x >= _dstBounds.x2 || y < _dstBounds.y1 || y >= _dstBounds.y2 || _dstPixelBytes == 0)
                return 0;

            char *pix = (char *) (((char *) _dstPixelData) + (size_t)(y - _dstBounds.y1) * _dstRowBytes);
            pix += (x - _dstBounds.x1) * _dstPixelBytes;
            return (void *) pix;
        }

    };


    
    // base class for a processor with a single source image
    class PixelProcessorFilterBase : public OFX::PixelProcessor {
    protected:
        const void *_srcPixelData;
        OfxRectI _srcBounds;
        OFX::PixelComponentEnum _srcPixelComponents;
        OFX::BitDepthEnum _srcBitDepth;
        int _srcPixelBytes;
        int _srcRowBytes;
        const OFX::Image *_origImg;
        const OFX::Image *_maskImg;
        bool   _doMasking;
        double _mix;
        bool _maskInvert;

    public:
        /** @brief no arg ctor */
        PixelProcessorFilterBase(OFX::ImageEffect &instance)
        : OFX::PixelProcessor(instance)
        , _srcPixelData(0)
        , _srcBounds()
        , _srcPixelComponents(OFX::ePixelComponentNone)
        , _srcBitDepth(OFX::eBitDepthNone)
        , _srcPixelBytes(0)
        , _srcRowBytes(0)
        , _maskImg(0)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        {
        }

        /** @brief set the src image */
        void setSrcImg(const OFX::Image *v)
        {
            _srcPixelData = v->getPixelData();
            _srcBounds = v->getBounds();
            _srcPixelComponents = v->getPixelComponents();
            _srcBitDepth = v->getPixelDepth();
            _srcPixelBytes = getPixelBytes(_srcPixelComponents, _srcBitDepth);
            _srcRowBytes = v->getRowBytes();
        }

        /** @brief set the src image */
        void setSrcImg(const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
                       OFX::BitDepthEnum srcPixelDepth,
                       int srcRowBytes)
        {
            _srcPixelData = srcPixelData;
            _srcBounds = srcBounds;
            _srcPixelComponents = srcPixelComponents;
            _srcBitDepth = srcPixelDepth;
            _srcPixelBytes = getPixelBytes(_srcPixelComponents, _srcBitDepth);
            _srcRowBytes = srcRowBytes;
        }

        void setOrigImg(const OFX::Image *v)
        {
            _origImg = v;
        }

        void setMaskImg(const OFX::Image *v)
        {
            _maskImg = v;
        }

        void doMasking(bool v) {
            _doMasking = v;
        }

        void setMaskMix(double mix,
                        bool maskInvert)
        {
             _mix = mix;
            _maskInvert = maskInvert;
        }

    protected:
        const void* getSrcPixelAddress(int x, int y) const
        {
            // are we in the image bounds
            if(x < _srcBounds.x1 || x >= _srcBounds.x2 || y < _srcBounds.y1 || y >= _srcBounds.y2 || _srcPixelBytes == 0)
                return 0;

            char *pix = (char *) (((char *) _srcPixelData) + (size_t)(y - _srcBounds.y1) * _srcRowBytes);
            pix += (x - _srcBounds.x1) * _srcPixelBytes;
            return (void *) pix;
        }
    };


};
#endif
