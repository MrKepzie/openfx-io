/*
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

#include "ofxsSupportPrivate.h"


namespace OFX {
    namespace Color {
        
        // compile-time endianness checking found on:
        // http://stackoverflow.com/questions/2100331/c-macro-definition-to-determine-big-endian-or-little-endian-machine
        // if(O32_HOST_ORDER == O32_BIG_ENDIAN) will always be optimized by gcc -O2
        enum {
            O32_LITTLE_ENDIAN = 0x03020100ul,
            O32_BIG_ENDIAN = 0x00010203ul,
            O32_PDP_ENDIAN = 0x01000302ul
        };
        static const union {
            uint8_t bytes[4];
            uint32_t value;
        } o32_host_order = { { 0, 1, 2, 3 } };
#define O32_HOST_ORDER (o32_host_order.value)
        
        static unsigned short hipart(const float f)
        {
            union {
                float f;
                unsigned short us[2];
            } tmp;
            tmp.us[0] = tmp.us[1] = 0;
            tmp.f = f;
            
            if (O32_HOST_ORDER == O32_BIG_ENDIAN) {
                return tmp.us[0];
            } else if (O32_HOST_ORDER == O32_LITTLE_ENDIAN) {
                return tmp.us[1];
            } else {
                assert((O32_HOST_ORDER == O32_LITTLE_ENDIAN) || (O32_HOST_ORDER == O32_BIG_ENDIAN));
                return 0;
            }
        }
        
        static float index_to_float(const unsigned short i)
        {
            union {
                float f;
                unsigned short us[2];
            } tmp;
            /* positive and negative zeros, and all gradual underflow, turn into zero: */
            if (i < 0x80 || (i >= 0x8000 && i < 0x8080)) {
                return 0;
            }
            /* All NaN's and infinity turn into the largest possible legal float: */
            if (i >= 0x7f80 && i < 0x8000) {
                return std::numeric_limits<float>::max();
            }
            if (i >= 0xff80) {
                return -std::numeric_limits<float>::max();
            }
            if (O32_HOST_ORDER == O32_BIG_ENDIAN) {
                tmp.us[0] = i;
                tmp.us[1] = 0x8000;
            } else if (O32_HOST_ORDER == O32_LITTLE_ENDIAN) {
                tmp.us[0] = 0x8000;
                tmp.us[1] = i;
            } else {
                assert((O32_HOST_ORDER == O32_LITTLE_ENDIAN) || (O32_HOST_ORDER == O32_BIG_ENDIAN));
            }
            
            return tmp.f;
        }

        
        const Lut *LutManager::getLut(const std::string& name,OfxFromColorSpaceFunctionV1 fromFunc,OfxToColorSpaceFunctionV1 toFunc) {
            std::map<std::string, const Lut *>::const_iterator found = luts.find(name);
            if (found != luts.end()) {
                return found->second;
            }else{
                std::pair<std::map<std::string, const Lut *>::iterator,bool> ret = luts.insert(std::make_pair(name,new Lut(fromFunc,toFunc)));
                assert(ret.second);
                return ret.first->second;
            }
            return NULL;
        }
        
        LutManager::~LutManager()
        {
            for (std::map<std::string, const Lut *>::iterator it = luts.begin(); it != luts.end(); ++it) {
                if (it->second) {
                    delete it->second;
                    it->second = 0;
                }
            }
            luts.clear();
        }

   
        void clip(OfxRectI* what,const OfxRectI& to){
            if(what->x1 < to.x1){
                what->x1 = to.x1;
            }
            if(what->x2 > to.x2){
                what->x2 = to.x2;
            }
            if(what->y1 < to.y1){
                what->y1 = to.y1;
            }
            if(what->y2 > to.y2){
                what->y2 = to.y2;
            }
        }
        
        bool intersects(const OfxRectI& what,const OfxRectI& other){
            return (what.x2 >= other.x1 && what.x1 < other.x1 ) ||
            ( what.x1 < other.x2 && what.x2 >= other.x2) ||
            ( what.y2 >= other.y1 && what.y1 < other.y1) ||
            ( what.y1 < other.y2 && what.y2 >= other.y2);
        }
        
        float Lut::fromFloatFast(float v) const
        {
            return from_byte_table[(int)(v * 255)];
        }
        
        float Lut::toFloatFast(float v) const
        {
            return (float)to_byte_table[hipart(v)] / 255.f;
        }
        
        
        
        
        void Lut::fillTables() const
        {
            if (init_) {
                return;
            }
            for (int i = 0; i < 0x10000; ++i) {
                float inp = index_to_float((unsigned short)i);
                float f = _toFunc(inp);
                if (f <= 0) {
                    to_byte_table[i] = 0;
                } else if (f < 255) {
                    to_byte_table[i] = (unsigned short)(f * 0x100 + .5);
                } else {
                    to_byte_table[i] = 0xff00;
                }
            }
            
            for (int b = 0; b <= 255; ++b) {
                float f = _fromFunc((float)b / 255.f);
                from_byte_table[b] = f;
                int i = hipart(f);
                to_byte_table[i] = (unsigned short)(b * 0x100);
            }
            
        }
        
        void Lut::to_byte_planar(unsigned char* to, const float* from,int W,const float* alpha,int delta){
            validate();
            unsigned char *end = to + W * delta;
            int start = rand() % W;
            const float *q;
            unsigned char *p;
            unsigned error;
            if(!alpha){
                /* go fowards from starting point to end of line: */
                error = 0x80;
                for (p = to + start * delta, q = from + start; p < end; p += delta) {
                    error = (error & 0xff) + to_byte_table[hipart(*q)];
                    ++q;
                    *p = (unsigned char)(error >> 8);
                }
                /* go backwards from starting point to start of line: */
                error = 0x80;
                for (p = to + (start - 1) * delta, q = from + start; p >= to; p -= delta) {
                    --q;
                    error = (error & 0xff) + to_byte_table[hipart(*q)];
                    *p = (unsigned char)(error >> 8);
                }
            }else{
                const float *a = alpha;
                /* go fowards from starting point to end of line: */
                error = 0x80;
                for (p = to + start * delta, q = from + start, a += start; p < end; p += delta) {
                    const float v = *q * *a;
                    error = (error & 0xff) + to_byte_table[hipart(v)];
                    ++q;
                    ++a;
                    *p = (unsigned char)(error >> 8);
                }
                /* go backwards from starting point to start of line: */
                error = 0x80;
                for (p = to + (start - 1) * delta, q = from + start , a = alpha + start; p >= to; p -= delta) {
                    const float v = *q * *a;
                    --q;
                    --a;
                    error = (error & 0xff) + to_byte_table[hipart(v)];
                    *p = (unsigned char)(error >> 8);
                }

            }

        }
        
        void Lut::to_short_planar(unsigned short* to, const float* from,int W,const float* alpha ,int delta){
            
            throw std::runtime_error("Lut::to_short_planar not implemented yet.");
        }
        
        void Lut::to_float_planar(float* to, const float* from,int W,const float* alpha ,int delta){
            
            validate();
            if(!alpha){
                for (int i = 0; i < W; i += delta) {
                    to[i] = toFloatFast(from[i]);
                }
            }else{
                for (int i = 0; i < W; i += delta) {
                    to[i] = toFloatFast(from[i] * alpha[i]);
                }
            }
        }
        
        void Lut::to_byte_packed(unsigned char* to, const float* from,const OfxRectI& conversionRect,
                                 const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                 PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult){
            
            bool inputHasAlpha = inputFormat == Lut::PIXELS_PACKING_BGRA || inputFormat == Lut::PIXELS_PACKING_RGBA;
            bool outputHasAlpha = outputFormat == Lut::PIXELS_PACKING_BGRA || outputFormat == Lut::PIXELS_PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputFormat, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputFormat, &outROffset, &outGOffset, &outBOffset, &outAOffset);
            
            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            validate();
            
            for (int y = rect.y1; y < rect.y2; ++y) {
                int start = rand() % (rect.x2 - rect.x1) + rect.x1;
                unsigned error_r, error_g, error_b;
                error_r = error_g = error_b = 0x80;
                int srcY = y;
                if (!invertY) {
                    srcY = srcRod.y2 - y - 1;
                }
                
                /// if not in the srcRoD, continue to next line
                if(srcY < srcRod.y1 || srcY >= srcRod.y2){
                    continue;
                }
                
                int dstY = dstRod.y2 - y - 1;
                
                const float *src_pixels = from + (srcY * (srcRod.x2 - srcRod.x1) * inPackingSize);
                unsigned char *dst_pixels = to + (dstY * (dstRod.x2 - dstRod.x1) * outPackingSize);
                /* go fowards from starting point to end of line: */
                for (int x = start; x < rect.x2; ++x) {
                    /// if not in the srcRoD, continue to next line
                    if(x < srcRod.x1 || x >= srcRod.x2){
                        continue;
                    }
                    
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;
                    error_r = (error_r & 0xff) + to_byte_table[hipart(src_pixels[inCol + inROffset] * a)];
                    error_g = (error_g & 0xff) + to_byte_table[hipart(src_pixels[inCol + inGOffset] * a)];
                    error_b = (error_b & 0xff) + to_byte_table[hipart(src_pixels[inCol + inBOffset] * a)];
                    dst_pixels[outCol + outROffset] = (unsigned char)(error_r >> 8);
                    dst_pixels[outCol + outGOffset] = (unsigned char)(error_g >> 8);
                    dst_pixels[outCol + outBOffset] = (unsigned char)(error_b >> 8);
                    if(outputHasAlpha){
                        dst_pixels[outCol + outAOffset] = (unsigned char)(std::min(a * 256.f, 255.f));
                    }
                }
                /* go backwards from starting point to start of line: */
                error_r = error_g = error_b = 0x80;
                for (int x = start - 1; x >= rect.x1; --x) {
                    /// if not in the srcRoD, continue to next line
                    if(x < srcRod.x1 || x >= srcRod.x2){
                        continue;
                    }
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;
                    error_r = (error_r & 0xff) + to_byte_table[hipart(src_pixels[inCol + inROffset] * a)];
                    error_g = (error_g & 0xff) + to_byte_table[hipart(src_pixels[inCol + inGOffset] * a)];
                    error_b = (error_b & 0xff) + to_byte_table[hipart(src_pixels[inCol + inBOffset] * a)];
                    dst_pixels[outCol + outROffset] = (unsigned char)(error_r >> 8);
                    dst_pixels[outCol + outGOffset] = (unsigned char)(error_g >> 8);
                    dst_pixels[outCol + outBOffset] = (unsigned char)(error_b >> 8);
                    if(outputHasAlpha){
                        dst_pixels[outCol + outAOffset] = (unsigned char)(std::min(a * 256.f, 255.f));
                    }
                }
            }

            
        }
        
        void Lut::to_short_packed(unsigned short* to, const float* from,const OfxRectI& conversionRect,
                                  const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                  PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult){
            
            throw std::runtime_error("Lut::to_short_packed not implemented yet.");

        }
        
        void Lut::to_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                  const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                  PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult){
            
            bool inputHasAlpha = inputFormat == Lut::PIXELS_PACKING_BGRA || inputFormat == Lut::PIXELS_PACKING_RGBA;
            bool outputHasAlpha = outputFormat == Lut::PIXELS_PACKING_BGRA || outputFormat == Lut::PIXELS_PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputFormat, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputFormat, &outROffset, &outGOffset, &outBOffset, &outAOffset);
            
            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            validate();
            
            for (int y = rect.y1; y < rect.y2; ++y) {
                int srcY = y;
                if (invertY) {
                    srcY = srcRod.y2 - y - 1;
                }
                /// if not in the srcRoD, continue to next line
                if(srcY < srcRod.y1 || srcY >= srcRod.y2){
                    continue;
                }
                int dstY = dstRod.y2 - y - 1;
                const float *src_pixels = from + (srcY * (srcRod.x2 - srcRod.x1) * inPackingSize);
                float *dst_pixels = to + (dstY * (dstRod.x2 - dstRod.x1) * outPackingSize);
                /* go fowards from starting point to end of line: */
                for (int x = rect.x1; x < rect.x2; ++x) {
                    /// if not in the srcRoD, continue to next line
                    if(x < srcRod.x1 || x >= srcRod.x2){
                        continue;
                    }
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;;
                    dst_pixels[outCol + outROffset] = toFloatFast(src_pixels[inCol + inROffset] * a);
                    dst_pixels[outCol + outGOffset] = toFloatFast(src_pixels[inCol + inGOffset] * a);
                    dst_pixels[outCol + outBOffset] = toFloatFast(src_pixels[inCol + inBOffset] * a);
                    if(outputHasAlpha) {
                        dst_pixels[outCol + outAOffset] = a;
                    }
                }
            }

        }
        
      
        
        void Lut::from_byte_planar(float* to,const unsigned char* from,int W,const unsigned char* alpha ,int delta){
            
            validate();
            if(!alpha){
                for (int i = 0 ; i < W ; i += delta) {
                    to[i] = from_byte_table[(int)from[i]];
                }
            }else{
                for (int i = 0 ; i < W ; i += delta) {
                    to[i] = from_byte_table[(from[i]*255 + 128) / alpha[i]] * alpha[i] / 255.;
                }
            }
            
        }
        
        void Lut::from_short_planar(float* to,const unsigned short* from,int W,const unsigned short* alpha ,int delta){
            
            throw std::runtime_error("Lut::from_short_planar not implemented yet.");
        }
        
        void Lut::from_float_planar(float* to,const float* from,int W,const float* alpha ,int delta){
            
            validate();
            if(!alpha){
                for (int i = 0; i < W; i += delta) {
                    to[i] = fromFloatFast(from[i]);
                }
            }else{
                for (int i = 0; i < W; i += delta) {
                    float a = alpha[i];
                    to[i] = fromFloatFast(from[i] / a) * a;
                }
            }
        }
        
        void Lut::from_byte_packed(float* to, const unsigned char* from,const OfxRectI& conversionRect,
                                   const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                   PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult){
            
            if(inputFormat == Lut::PIXELS_PACKING_PLANAR || outputFormat == Lut::PIXELS_PACKING_PLANAR){
                throw std::runtime_error("Invalid pixel format.");
            }
            
            bool inputHasAlpha = inputFormat == Lut::PIXELS_PACKING_BGRA || inputFormat == Lut::PIXELS_PACKING_RGBA;
            bool outputHasAlpha = outputFormat == Lut::PIXELS_PACKING_BGRA || outputFormat == Lut::PIXELS_PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputFormat, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputFormat, &outROffset, &outGOffset, &outBOffset, &outAOffset);
            
            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            validate();
            for (int y = rect.y1; y < rect.y2; ++y) {
                int srcY = y;
                if (invertY) {
                    srcY = rod.y2 - y - 1;
                }
                const unsigned char *src_pixels = from + (srcY * (rod.x2 - rod.x1) * inPackingSize);
                float *dst_pixels = to + (y * (rod.x2 - rod.x1) * outPackingSize);
                for (int x = rect.x1; x < rect.x2; ++x) {
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] / 255.f : 1.f;
                    dst_pixels[outCol + outROffset] = from_byte_table[(int)(((src_pixels[inCol + inROffset] / 255.f) / a) * 255)] * a;
                    dst_pixels[outCol + outGOffset] = from_byte_table[(int)(((src_pixels[inCol + inGOffset] / 255.f) / a) * 255)] * a;
                    dst_pixels[outCol + outBOffset] = from_byte_table[(int)(((src_pixels[inCol + inBOffset] / 255.f) / a) * 255)] * a;
                    if (outputHasAlpha) {
                        dst_pixels[outCol + outAOffset] = a;
                    }
                }
            }

        }
        
        void Lut::from_short_packed(float* to, const unsigned short* from,const OfxRectI& conversionRect,
                                    const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                    PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult){
            
           throw std::runtime_error("Lut::from_short_packed not implemented yet.");
        }
        
        void Lut::from_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                    const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                    PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult){
            
            bool inputHasAlpha = inputFormat == Lut::PIXELS_PACKING_BGRA || inputFormat == Lut::PIXELS_PACKING_RGBA;
            bool outputHasAlpha = outputFormat == Lut::PIXELS_PACKING_BGRA || outputFormat == Lut::PIXELS_PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputFormat, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputFormat, &outROffset, &outGOffset, &outBOffset, &outAOffset);
            
            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            validate();
            
            for (int y = rect.y1; y < rect.y2; ++y) {
                int srcY = y;
                if (invertY) {
                    srcY = rod.y2 - y - 1;
                }
                const float *src_pixels = from + (srcY * (rod.x2 - rod.x1) * inPackingSize);
                float *dst_pixels = to + (y * (rod.x2 - rod.x1) * outPackingSize);
                for (int x = rect.x1; x < rect.x2; ++x) {
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;;
                    dst_pixels[outCol + outROffset] = fromFloatFast((src_pixels[inCol + inROffset] / a) * 255.f) * a;
                    dst_pixels[outCol + outGOffset] = fromFloatFast((src_pixels[inCol + inGOffset] / a) * 255.f) * a;
                    dst_pixels[outCol + outBOffset] = fromFloatFast((src_pixels[inCol + inBOffset] /a) * 255.f) * a;
                    if(outputHasAlpha) {
                        dst_pixels[outCol + outAOffset] = a;
                    }
                    
                }
            }
        }
        
        
        /////////////////////////////////////////// LINEAR //////////////////////////////////////////////
        
        
        
        void Linear::from_byte_planar(float *to, const unsigned char *from, int W,const unsigned char* /*alpha*/, int delta)
        {
            ///disregard premultiplication flag as we are just copying data
            from += (W - 1) * delta;
            to += W;
            for (; --W >= 0; from -= delta) {
                *--to = Linear::toFloat(*from);
            }
        }
        
        void Linear::from_short_planar(float *to, const unsigned short *from, int W, const unsigned short* /*alpha*/, int delta)
        {
            ///disregard premultiplication flag as we are just copying data
            for (int i = 0; i < W; i += delta) {
                to[i] = Linear::toFloat(from[i]);
            }
            
        }
        
        void Linear::from_float_planar(float *to, const float *from, int W, const float* /*alpha*/,int delta)
        {
            ///disregard premultiplication flag as we are just copying data
            if(delta == 1){
                memcpy(to, from, W*sizeof(float));
            }else{
                for (int i = 0; i < W; i += delta) {
                    to[i] = from[i];
                }
            }
        }
        
        void getOffsetsForPacking(Lut::PixelPacking format, int *r, int *g, int *b, int *a)
        {
            if (format == Lut::PACKING_BGRA) {
                *b = 0;
                *g = 1;
                *r = 2;
                *a = 3;
            }else if(format == Lut::PACKING_RGBA){
                *r = 0;
                *g = 1;
                *b = 2;
                *a = 3;
            }else if(format == Lut::PACKING_RGB){
                *r = 0;
                *g = 1;
                *b = 2;
                *a = -1;
            }else if(format == Lut::PACKING_BGR){
                *r = 0;
                *g = 1;
                *b = 2;
                *a = -1;
            }else if(format == Lut::PACKING_PLANAR){
                *r = 0;
                *g = 1;
                *b = 2;
                *a = -1;
            }else{
                *r = -1;
                *g = -1;
                *b = -1;
                *a = -1;
                throw std::runtime_error("Unsupported pixel packing format");
            }
        }
        
        void Linear::from_byte_packed(float *to, const unsigned char *from,
                                      const OfxRectI &rect,const OfxRectI &srcRod, const OfxRectI &rod,
                                      Lut::PixelPacking inputPacking,Lut::PixelPacking outputPacking,
                                      bool invertY ,bool /*premult*/)
        
        {
            
            if(inputPacking == PACKING_PLANAR || outputPacking == PACKING_PLANAR){
                throw std::runtime_error("This function is not meant for planar buffers.");
            }
                
            if(srcRod.x1 != rod.x1 ||
               srcRod.x2 != rod.x2 ||
               srcRod.y1 != rod.y1 ||
               srcRod.y2 != rod.y2){
                throw std::runtime_error("Different input and output RoD is unsupported.");
            }
            
            bool inputHasAlpha = inputPacking == Lut::PACKING_BGRA || inputPacking == Lut::PACKING_RGBA;
            bool outputHasAlpha = outputPacking == Lut::PACKING_BGRA || outputPacking == Lut::PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputPacking, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputPacking, &outROffset, &outGOffset, &outBOffset, &outAOffset);

            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            
            
            for (int y = rect.y1; y < rect.y2; ++y) {
                int srcY = y;
                if (invertY) {
                    srcY = rod.y2 - y - 1;
                }
                const unsigned char *src_pixels = from + (srcY * (rod.x2 - rod.x1) * inPackingSize);
                float *dst_pixels = to + (y * (rod.x2 - rod.x1) * outPackingSize);
                for (int x = rect.x1; x < rect.x2; ++x) {
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    unsigned char a = inputHasAlpha ? src_pixels[inCol + inAOffset] : 255;
                    dst_pixels[outCol + outROffset] = src_pixels[inCol + inROffset] / 255.f;
                    dst_pixels[outCol + outGOffset] = src_pixels[inCol + inGOffset] / 255.f;
                    dst_pixels[outCol + outBOffset] = src_pixels[inCol + inBOffset] / 255.f;
                    if(outputHasAlpha) {
                        dst_pixels[outCol + outAOffset] = a / 255.f;
                    }
                }
            }
            
        }
        
        void Linear::from_short_packed(float *to, const unsigned short *from,
                                       const OfxRectI &rect,const OfxRectI &/*srcRod*/, const OfxRectI &rod,
                                       Lut::PixelPacking inputFormat,Lut::PixelPacking format,
                                       bool invertY ,bool premult)
        {
            throw std::runtime_error("Linear::from_short_packed not yet implemented.");
            
        }
        
        void Linear::from_float_packed(float *to, const float *from,
                                       const OfxRectI &rect,const OfxRectI &srcRod, const OfxRectI &rod,
                                       Lut::PixelPacking inputPacking,Lut::PixelPacking outputPacking,
                                       bool invertY ,bool /*premult*/)
        {
            if(inputPacking == PACKING_PLANAR || outputPacking == PACKING_PLANAR){
                throw std::runtime_error("This function is not meant for planar buffers.");
            }

            
            if(srcRod.x1 != rod.x1 ||
               srcRod.x2 != rod.x2 ||
               srcRod.y1 != rod.y1 ||
               srcRod.y2 != rod.y2){
                throw std::runtime_error("Different input and output RoD is unsupported.");
            }
            
            bool inputHasAlpha = inputPacking == Lut::PACKING_BGRA || inputPacking == Lut::PACKING_RGBA;
            bool outputHasAlpha = outputPacking == Lut::PACKING_BGRA || outputPacking == Lut::PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputPacking, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputPacking, &outROffset, &outGOffset, &outBOffset, &outAOffset);

            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            for (int y = rect.y1; y < rect.y2; ++y) {
                int srcY = y;
                if (invertY) {
                    srcY = rod.y2 - y - 1;
                }
                const float *src_pixels = from + (srcY * (rod.x2 - rod.x1) * inPackingSize);
                float *dst_pixels = to + (y * (rod.x2 - rod.x1) * outPackingSize);
                if (inputPacking == outputPacking) {
                    memcpy(dst_pixels, src_pixels, (rod.x2 - rod.x1)*sizeof(float));
                } else {
                    for (int x = rect.x1; x < rect.x2; ++x) {
                        int inCol = x * inPackingSize;
                        int outCol = x * outPackingSize;
                        float a = inputHasAlpha ? src_pixels[inCol + inAOffset] : 1.f;
                        dst_pixels[outCol + outROffset] = src_pixels[inCol + inROffset];
                        dst_pixels[outCol + outGOffset] = src_pixels[inCol + inGOffset];
                        dst_pixels[outCol + outBOffset] = src_pixels[inCol + inBOffset];
                        if(outputHasAlpha) {
                            dst_pixels[outCol + outAOffset] = a;
                        }
                    }
                }
            }
        }
        
        void Linear::to_byte_planar(unsigned char *to, const float *from, int W,const float* alpha, int delta)
        {
            if(!alpha){
                unsigned char *end = to + W * delta;
                int start = rand() % W;
                const float *q;
                unsigned char *p;
                /* go fowards from starting point to end of line: */
                float error = .5;
                for (p = to + start * delta, q = from + start; p < end; p += delta) {
                    float G = error + *q++ * 255.0f;
                    if (G <= 0) {
                        *p = 0;
                    } else if (G < 255) {
                        int i = (int)G;
                        *p = (unsigned char)i;
                        error = G - i;
                    } else {
                        *p = 255;
                    }
                }
                /* go backwards from starting point to start of line: */
                error = .5;
                for (p = to + (start - 1) * delta, q = from + start; p >= to; p -= delta) {
                    float G = error + *--q * 255.0f;
                    if (G <= 0) {
                        *p = 0;
                    } else if (G < 255) {
                        int i = (int)G;
                        *p = (unsigned char)i;
                        error = G - i;
                    } else {
                        *p = 255;
                    }
                }
            }else{
                unsigned char *end = to + W * delta;
                int start = rand() % W;
                const float *q;
                const float *a = alpha;
                unsigned char *p;
                /* go fowards from starting point to end of line: */
                float error = .5;
                for (p = to + start * delta, q = from + start, a += start; p < end; p += delta) {
                    float v = *q * *a;
                    float G = error + v * 255.0f;
                    ++q;
                    ++a;
                    if (G <= 0) {
                        *p = 0;
                    } else if (G < 255) {
                        int i = (int)G;
                        *p = (unsigned char)i;
                        error = G - i;
                    } else {
                        *p = 255;
                    }
                }
                /* go backwards from starting point to start of line: */
                error = .5;
                for (p = to + (start - 1) * delta, q = from + start, a = alpha + start; p >= to; p -= delta) {
                    const float v = *q * *a;
                    float G = error + v * 255.0f;
                    --q;
                    --a;
                    if (G <= 0) {
                        *p = 0;
                    } else if (G < 255) {
                        int i = (int)G;
                        *p = (unsigned char)i;
                        error = G - i;
                    } else {
                        *p = 255;
                    }
                }
                
            }
        }
        
        
        
        void Linear::to_short_planar(unsigned short *to, const float *from, int W, const float* alpha ,int delta)
        {
            (void)to;
            (void)from;
            (void)W;
            (void)alpha;
            (void)delta;
            throw std::runtime_error("Linear::to_short_planar not yet implemented.");
        }
        
        
        void Linear::to_float_planar(float *to, const float *from, int W,const float* alpha ,int delta)
        {
            if(!alpha){
                (void)delta;
                if (delta == 1) {
                    memcpy(to, from, W * sizeof(float));
                } else {
                    for (int i = 0; i < W; i += delta) {
                        to[i] = from[i];
                    }
                }
            }else{
                for (int i = 0; i < W; i += delta) {
                    to[i] = from[i] * alpha[i];
                }
            }
        }
        
        void Linear::to_byte_packed(unsigned char* to, const float* from,const OfxRectI& conversionRect,
                                    const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                    PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult)
        {
            
            if(inputPacking == PACKING_PLANAR || outputPacking == PACKING_PLANAR){
                throw std::runtime_error("This function is not meant for planar buffers.");
            }
            
            bool inputHasAlpha = inputPacking == Lut::PACKING_BGRA || inputPacking == Lut::PACKING_RGBA;
            bool outputHasAlpha = outputPacking == Lut::PACKING_BGRA || outputPacking == Lut::PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputPacking, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputPacking, &outROffset, &outGOffset, &outBOffset, &outAOffset);

            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            for (int y = conversionRect.y1; y < conversionRect.y2; ++y) {
                int start = rand() % (conversionRect.x2 - conversionRect.x1) + conversionRect.x1;
                unsigned error_r, error_g, error_b;
                error_r = error_g = error_b = 0x80;
                int srcY = y;
                if (invertY) {
                    srcY = srcRoD.y2 - y - 1;
                }
                
                /// if not in the srcRoD, continue to next line
                if(srcY < srcRoD.y1 || srcY >= srcRoD.y2){
                    continue;
                }
                
                const float *src_pixels = from + (srcY * (srcRoD.x2 - srcRoD.x1) * inPackingSize);
                unsigned char *dst_pixels = to + (y * (dstRoD.x2 - dstRoD.x1) * outPackingSize);
                /* go fowards from starting point to end of line: */
                for (int x = start; x < conversionRect.x2; ++x) {
                    
                    /// if not in the srcRoD, continue to next line
                    if(x < srcRoD.x1 || x >= srcRoD.x2){
                        continue;
                    }
                    
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;
                    
                    error_r = (error_r & 0xff) + src_pixels[inCol + inROffset] * a * 255.f;
                    error_g = (error_g & 0xff) + src_pixels[inCol + inGOffset] * a * 255.f;
                    error_b = (error_b & 0xff) + src_pixels[inCol + inBOffset] * a * 255.f;
                    dst_pixels[outCol + outROffset] = (unsigned char)(error_r >> 8);
                    dst_pixels[outCol + outGOffset] = (unsigned char)(error_g >> 8);
                    dst_pixels[outCol + outBOffset] = (unsigned char)(error_b >> 8);
                    if(outputHasAlpha) {
                        dst_pixels[outCol + outAOffset] = (unsigned char)(std::min(a * 256.f, 255.f));
                    }

                }
                /* go backwards from starting point to start of line: */
                error_r = error_g = error_b = 0x80;
                for (int x = start - 1; x >= conversionRect.x1; --x) {
                    
                    /// if not in the srcRoD, continue to next line
                    if(x < srcRoD.x1 || x >= srcRoD.x2){
                        continue;
                    }
                    
                    int inCol = x * inPackingSize;
                    int outCol = x * outPackingSize;
                    
                    float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;
                    
                    error_r = (error_r & 0xff) + src_pixels[inCol + inROffset] * a * 255.f;
                    error_g = (error_g & 0xff) + src_pixels[inCol + inGOffset] * a * 255.f;
                    error_b = (error_b & 0xff) + src_pixels[inCol + inBOffset] * a * 255.f;
                    dst_pixels[outCol + outROffset] = (unsigned char)(error_r >> 8);
                    dst_pixels[outCol + outGOffset] = (unsigned char)(error_g >> 8);
                    dst_pixels[outCol + outBOffset] = (unsigned char)(error_b >> 8);
                    if(outputHasAlpha) {
                        dst_pixels[outCol + outAOffset] = (unsigned char)(std::min(a * 256.f, 255.f));
                    }
                }
            }
        }
        
        
        
        void Linear::to_short_packed(unsigned short* to, const float* from,const OfxRectI& conversionRect,
                                     const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                     PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult)
        {
            (void)to;
            (void)from;
            (void)conversionRect;
            (void)srcRoD;
            (void)dstRoD;
            (void)invertY;
            (void)premult;
            (void)inputPacking;
            (void)outputPacking;
            throw std::runtime_error("Linear::to_short_packed not yet implemented.");
        }
        
        void Linear::to_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                     const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                     PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult)
        {
            
            if(inputPacking == PACKING_PLANAR || outputPacking == PACKING_PLANAR){
                throw std::runtime_error("This function is not meant for planar buffers.");
            }
            
            bool inputHasAlpha = inputPacking == Lut::PACKING_BGRA || inputPacking == Lut::PACKING_RGBA;
            bool outputHasAlpha = outputPacking == Lut::PACKING_BGRA || outputPacking == Lut::PACKING_RGBA;
            
            int inROffset, inGOffset, inBOffset, inAOffset;
            int outROffset, outGOffset, outBOffset, outAOffset;
            getOffsetsForPacking(inputPacking, &inROffset, &inGOffset, &inBOffset, &inAOffset);
            getOffsetsForPacking(outputPacking, &outROffset, &outGOffset, &outBOffset, &outAOffset);
            
            
            int inPackingSize,outPackingSize;
            inPackingSize = inputHasAlpha ? 4 : 3;
            outPackingSize = outputHasAlpha ? 4 : 3;
            
            for (int y = conversionRect.y1; y < conversionRect.y2; ++y) {
                int srcY = y;
                if (invertY) {
                    srcY = srcRoD.y2 - y - 1;
                }
                /// if not in the srcRoD, continue to next line
                if(srcY < srcRoD.y1 || srcY >= srcRoD.y2){
                    continue;
                }
                
                const float *src_pixels = from + (srcY * (srcRoD.x2 - srcRoD.x1) * inPackingSize);
                float *dst_pixels = to + (y * (dstRoD.x2 - dstRoD.x1) * outPackingSize);
                if (inputPacking == outputPacking && !premult) {
                    memcpy(dst_pixels, src_pixels, (conversionRect.x2 - conversionRect.x1) *sizeof(float));
                } else {
                    for (int x = conversionRect.x1; x < conversionRect.x2; ++x) {
                        
                        /// if not in the srcRoD, continue to next line
                        if(x < srcRoD.x1 || x >= srcRoD.x2){
                            continue;
                        }
                        
                        int inCol = x * inPackingSize;
                        int outCol = x * outPackingSize;

                        float a = (inputHasAlpha && premult) ? src_pixels[inCol + inAOffset] : 1.f;

                        dst_pixels[outCol + outROffset] = src_pixels[inCol + inROffset] * a;
                        dst_pixels[outCol + outGOffset] = src_pixels[inCol + inGOffset] * a;
                        dst_pixels[outCol + outBOffset] = src_pixels[inCol + inBOffset] * a;
                        if(outputHasAlpha){
                            dst_pixels[outCol + outAOffset] = a;
                        }
                    }
                }
            }
        }
        
        float from_func_srgb(float v){
            if (v < 0.04045f)
                return (v < 0.0f) ? 0.0f : v * (1.0f / 12.92f);
            else
                return std::pow((v + 0.055f) * (1.0f / 1.055f), 2.4f);
        }
        
        float to_func_srgb(float v){
            if (v < 0.0031308f)
                return (v < 0.0f) ? 0.0f : v * 12.92f;
            else
                return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        }
        
        const Lut* LutManager::sRGBLut(){
            return getLut("sRGB",from_func_srgb,to_func_srgb);
        }
        
        float from_func_Rec709(float v){
            if (v < 0.081f)
                return (v < 0.0f) ? 0.0f : v * (1.0f / 4.5f);
            else
                return std::pow((v + 0.099f) * (1.0f / 1.099f), (1.0f / 0.45f));
        }
        
        float to_func_Rec709(float v){
            if (v < 0.018f)
                return (v < 0.0f) ? 0.0f : v * 4.5f;
            else
                return 1.099f * std::pow(v, 0.45f) - 0.099f;
        }
        
        const Lut* LutManager::Rec709Lut(){
            return getLut("Rec709",from_func_Rec709,to_func_Rec709);
        }
        
        
        /*
         Following the formula:
         offset = pow(10,(blackpoint - whitepoint) * 0.002 / gammaSensito)
         gain = 1/(1-offset)
         linear = gain * pow(10,(1023*v - whitepoint)*0.002/gammaSensito)
         cineon = (log10((v + offset) /gain)/ (0.002 / gammaSensito) + whitepoint)/1023
         Here we're using: blackpoint = 95.0
         whitepoint = 685.0
         gammasensito = 0.6
         */
        float from_func_Cineon(float v){
            return (1.f / (1.f - std::pow(10.f,1.97f))) * std::pow(10.f,((1023.f * v) - 685.f) * 0.002f / 0.6f);
        }
        
        float to_func_Cineon(float v){
            float offset = std::pow(10.f,1.97f);
            return (std::log10((v + offset) / (1.f / (1.f - offset))) / 0.0033f + 685.0f) / 1023.f;
        }

        const Lut* LutManager::CineonLut(){
            return getLut("Cineon",from_func_Cineon,to_func_Cineon);
        }
        
        float from_func_Gamma1_8(float v){
            return std::pow(v, 0.55f);
        }
        
        float to_func_Gamma1_8(float v){
            return std::pow(v, 1.8f);
        }
        
        const Lut* LutManager::Gamma1_8Lut(){
            return getLut("Gamma1_8",from_func_Gamma1_8,to_func_Gamma1_8);
        }
        
        float from_func_Gamma2_2(float v){
            return std::pow(v, 0.45f);
        }
        
        float to_func_Gamma2_2(float v){
            return std::pow(v, 2.2f);
        }
        
        const Lut* LutManager::Gamma2_2Lut(){
            return getLut("Gamma2_2",from_func_Gamma2_2,to_func_Gamma2_2);
        }
        
        float from_func_Panalog(float v){
            return (std::pow(10.f,(1023.f * v - 681.f) / 444.f) - 0.0408) / 0.96f;
        }
        
        float to_func_Panalog(float v){
            return (444.f * std::log10(0.0408 + 0.96f * v) + 681.f) / 1023.f;
        }
        
        const Lut* LutManager::PanaLogLut(){
            return getLut("PanaLog",from_func_Panalog,to_func_Panalog);
        }
        
        float from_func_ViperLog(float v){
            return std::pow(10.f,(1023.f * v - 1023.f) / 500.f);
        }
        
        float to_func_ViperLog(float v){
            return (500.f * std::log10(v) + 1023.f) / 1023.f;
        }
        
        const Lut* LutManager::ViperLogLut(){
            return getLut("ViperLog",from_func_ViperLog,to_func_ViperLog);
        }
        
        float from_func_RedLog(float v){
            return (std::pow(10.f,( 1023.f * v - 1023.f ) / 511.f) - 0.01f) / 0.99f;
        }
        
        float to_func_RedLog(float v){
            return (511.f * std::log10(0.01f + 0.99f * v) + 1023.f) / 1023.f;
        }
        
        const Lut* LutManager::RedLogLut(){
            return getLut("RedLog",from_func_RedLog,to_func_RedLog);
        }
        
        float from_func_AlexaV3LogC(float v){
            return v > 0.1496582f ? std::pow(10.f,(v - 0.385537f) / 0.2471896f) * 0.18f - 0.00937677f
            : ( v / 0.9661776f - 0.04378604) * 0.18f - 0.00937677f;
        }
        
        float to_func_AlexaV3LogC(float v){
            return v > 0.010591f ?  0.247190f * std::log10(5.555556f * v + 0.052272f) + 0.385537f
            : v * 5.367655f + 0.092809f;
        }
        
        const Lut* LutManager::AlexaV3LogCLut(){
            return getLut("AlexaV3LogC",from_func_AlexaV3LogC,to_func_AlexaV3LogC);
        }
        

}
