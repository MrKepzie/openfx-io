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
    const void *_srcPixelData;
    OfxRectI _srcBounds;
    OFX::PixelComponentEnum _srcPixelComponents;
    OFX::BitDepthEnum _srcBitDepth;
    int _srcPixelBytes;
    int _srcRowBytes;

public :
    /** @brief no arg ctor */
    CopierBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcPixelData(0)
    , _srcBounds()
    , _srcPixelComponents(OFX::ePixelComponentNone)
    , _srcBitDepth(OFX::eBitDepthNone)
    , _srcPixelBytes(0)
    , _srcRowBytes(0)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v)
    {
        _srcPixelData = v->getPixelData();
        _srcBounds = v->getBounds();
        _srcPixelComponents = v->getPixelComponents();
        _srcBitDepth = v->getPixelDepth();
        _srcPixelBytes = pixelBytes(_srcPixelComponents, _srcBitDepth);
        _srcRowBytes = v->getRowBytes();
     }

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
        _srcPixelBytes = pixelBytes(_srcPixelComponents, _srcBitDepth);
        _srcRowBytes = srcRowBytes;
    }

protected:
    void* getSrcPixelAddress(int x, int y) const
    {
        // are we in the image bounds
        if(x < _srcBounds.x1 || x >= _srcBounds.x2 || y < _srcBounds.y1 || y >= _srcBounds.y2 || _srcPixelBytes == 0)
            return 0;

        char *pix = (char *) (((char *) _srcPixelData) + (y - _srcBounds.y1) * _srcRowBytes);
        pix += (x - _srcBounds.x1) * _srcPixelBytes;
        return (void *) pix;
    }

private:
    static int pixelBytes(OFX::PixelComponentEnum pixelComponents,
                             OFX::BitDepthEnum bitDepth)
    {
        // compute bytes per pixel
        int pixelBytes = 0;
        switch (pixelComponents) {
            case OFX::ePixelComponentNone : pixelBytes = 0; break;
            case OFX::ePixelComponentRGBA  : pixelBytes = 4; break;
            case OFX::ePixelComponentRGB  : pixelBytes = 3; break;
            case OFX::ePixelComponentAlpha : pixelBytes = 1; break;
            case OFX::ePixelComponentCustom : pixelBytes = 0; break;
        }
        switch (bitDepth) {
            case OFX::eBitDepthNone   : pixelBytes *= 0; break;
            case OFX::eBitDepthUByte  : pixelBytes *= 1; break;
            case OFX::eBitDepthUShort : pixelBytes *= 2; break;
            case OFX::eBitDepthFloat  : pixelBytes *= 4; break;
#ifdef OFX_EXTENSIONS_VEGAS
            case OFX::eBitDepthUByteBGRA  : pixelBytes *= 1; break;
            case OFX::eBitDepthUShortBGRA : pixelBytes *= 2; break;
            case OFX::eBitDepthFloatBGRA  : pixelBytes *= 4; break;
#endif
            case OFX::eBitDepthCustom : pixelBytes *= 0; break;
        }
    }

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

                PIX *srcPix = (PIX *)  (_srcPixelData ? getSrcPixelAddress(x, y) : 0);

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
