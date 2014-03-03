/*
 OFX I/O utility functions.
 Adds OpenColorIO functionality to any plugin.

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

#ifndef IO_Utility_h
#define IO_Utility_h


// compiler_warning.h
#define STRINGISE_IMPL(x) #x
#define STRINGISE(x) STRINGISE_IMPL(x)

// Use: #pragma message WARN("My message")
#if _MSC_VER
#   define FILE_LINE_LINK __FILE__ "(" STRINGISE(__LINE__) ") : "
#   define WARN(exp) (FILE_LINE_LINK "WARNING: " exp)
#else//__GNUC__ - may need other defines for different compilers
#   define WARN(exp) ("WARNING: " exp)
#endif

#include "ofxsImageEffect.h"

/// numvals should be 256 for byte, 65536 for 16-bits, etc.
template<int numvals>
float intToFloat(int value)
{
    return value / (float)(numvals-1);
}

template<int numvals>
int floatToInt(float value)
{
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return numvals - 1;
    }
    return value * (numvals-1) + 0.5;
}

inline void
getImageData(OFX::Image* img, void** pixelData, OfxRectI* bounds, OFX::PixelComponentEnum* pixelComponents, OFX::BitDepthEnum* bitDepth, int* rowBytes)
{
    *pixelData = img->getPixelData();
    *bounds = img->getBounds();
    *pixelComponents = img->getPixelComponents();
    *bitDepth = img->getPixelDepth();
    *rowBytes = img->getRowBytes();
}

inline void
getImageData(const OFX::Image* img, const void** pixelData, OfxRectI* bounds, OFX::PixelComponentEnum* pixelComponents, OFX::BitDepthEnum* bitDepth, int* rowBytes)
{
    *pixelData = img->getPixelData();
    *bounds = img->getBounds();
    *pixelComponents = img->getPixelComponents();
    *bitDepth = img->getPixelDepth();
    *rowBytes = img->getRowBytes();
}

inline
int getPixelBytes(OFX::PixelComponentEnum pixelComponents,
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
    return pixelBytes;
}

#endif
