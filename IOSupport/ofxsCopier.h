//
//  ofxsCopier.h
//  IO
//
//  Created by Frédéric Devernay on 03/02/2014.
//
//

#ifndef IO_ofxsCopier_h
#define IO_ofxsCopier_h


// Base class for the RGBA and the Alpha processor
class CopierBase : public OFX::ImageProcessor {
    protected :
    OFX::Image *_srcImg;
    public :
    /** @brief no arg ctor */
    CopierBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    {
    }

    /** @brief set the src image */
    void setSrcImg(OFX::Image *v) {_srcImg = v;}
};

// template to do the RGBA processing
template <class PIX, int nComponents>
class ImageCopier : public CopierBase {
    public :
    // ctor
    ImageCopier(OFX::ImageEffect &instance)
    : CopierBase(instance)
    {}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for(int y = procWindow.y1; y < procWindow.y2; y++) {
            if(_effect.abort()) break;

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for(int x = procWindow.x1; x < procWindow.x2; x++) {

                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                if(srcPix) {
                    for(int c = 0; c < nComponents; c++) {
                        dstPix[c] = srcPix[c];
                    }
                }
                else {
                    // no src pixel here, be black and transparent
                    for(int c = 0; c < nComponents; c++) {
                        dstPix[c] = 0;
                    }
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


#endif
