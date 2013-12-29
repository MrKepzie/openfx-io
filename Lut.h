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

#ifndef Support_ofxsColorSpace_h
#define Support_ofxsColorSpace_h

#include <cmath>
#include <ofxsImageEffect.h>
#include <ofxsMultiThread.h>


/*
 Color-space conversion utilities.
 It comes along with the openfx-io repository.
 
 This namespace is relevant to decode colors to linear and encode colors from linear
 colors. It aims to provide a clean and optimised way for a plugin (mainly readers/writers).
 We propose this namespace in order to avoid all readers to be appended with a color-space conversion effect and all writers
 to be prepended with another color-space conversion effect, which would require multiple
 useless image copies back and forth.
 The way it works is quite simple: The plugin states which color-space it want to convert
 convert and whether it should convert "from" the indicated color-space or "to" the
 indicated color-space.
 
 We define a color-space as 3 components:
 - a name (e.g: sRGB, Rec709, etc...)
 - a "from" function, taking in argument a float ranging in [0.,1.] and returning a float
 ranging in [0.,1.]. This function should convert from the color-space to linear.
 - a "to" function, which has the exact same signature as the "from" function but
 converts from linear to the indicated color-space.
 
 
 See http://mysite.verizon.net/spitzak/conversion/algorithm.html for implementation details.
 */



namespace OFX {
    namespace Color {
        
        /// @enum An enum describing supported pixels packing formats
        enum PixelPacking {
            PACKING_RGBA = 0,
            PACKING_BGRA,
            PACKING_RGB,
            PACKING_BGR,
            PACKING_PLANAR
        };
        
        
        
        /* @brief Converts a float ranging in [0 - 1.f] in the desired color-space to linear color-space also ranging in [0 - 1.f]*/
        typedef float (*OfxFromColorSpaceFunctionV1)(float v);
        
        /* @brief Converts a float ranging in [0 - 1.f] in  linear color-space to the desired color-space to also ranging in [0 - 1.f]*/
        typedef float (*OfxToColorSpaceFunctionV1)(float v);
        
        
        // a Singleton that holds precomputed LUTs for the whole application.
        // The m_instance member is static and is thus built before the first call to Instance().
        // WARNING : This class is not thread-safe and calling getLut must not be done in a function called
        // by multiple thread at once!
        class Lut;
        class LutManager
        {
        public:
            static LutManager &Instance() {
                return m_instance;
            };
            
            /**
             * @brief Returns a pointer to a lut with the given name and the given from and to functions.
             * If a lut with the same name didn't already exist, then it will create one.
             * WARNING : NOT THREAD-SAFE
             **/
            static const Lut *getLut(const std::string& name,OfxFromColorSpaceFunctionV1 fromFunc,OfxToColorSpaceFunctionV1 toFunc);
            
            /**
             * @brief Decrement the reference count of the lut of the given name if it was found.
             * When the ref count of a lut reaches 0 it will delete it.
             * WARNING : NOT THREAD-SAFE
             * WARNING: For any call to getLut() their must be a matching call to release() otherwise
             * the program will crash in the LutManager detructor.
             * @see ~LutManager()
             **/
            static void release(const std::string& name);
            
            ///buit-ins color-spaces
            static const Lut* sRGBLut();

            static const Lut* Rec709Lut();
            
            static const Lut* CineonLut();
            
            static const Lut* Gamma1_8Lut();
            
            static const Lut* Gamma2_2Lut();
            
            static const Lut* PanaLogLut();
            
            static const Lut* ViperLogLut();
            
            static const Lut* RedLogLut();
            
            static const Lut* AlexaV3LogCLut();
            
        private:
            LutManager &operator= (const LutManager &) {
                return *this;
            }
            LutManager(const LutManager &) {}
            
            static LutManager m_instance;
            LutManager();
            
            ////the luts must all have been released before!
            ////This is because the Lut holds a OFX::MultiThread::Mutex and it can't be deleted
            //// by this singleton because it makes their destruction time uncertain regarding to
            ///the host multi-thread suite.
            ~LutManager();
            
            //each lut with a ref count mapped against their name
            typedef std::map<std::string, std::pair< const Lut *, int > > LutsMap;
            LutsMap luts;
        };
    

    
        /**
         * @brief A Lut (look-up table) used to speed-up color-spaces conversions.
         * If you plan on doing linear conversion, you should just use the Linear class instead.
         **/
        class Lut {
            
            std::string _name; ///< name of the lut
            OfxFromColorSpaceFunctionV1 _fromFunc;
            OfxToColorSpaceFunctionV1 _toFunc;
            
            /// the fast lookup tables are mutable, because they are automatically initialized post-construction,
            /// and never change afterwards
            mutable unsigned short to_byte_table[0x10000]; /// contains  2^16 = 65536 values between 0-255
            mutable float from_byte_table[256]; /// values between 0-1.f
            mutable bool init_; ///< false if the tables are not yet initialized
            mutable OFX::MultiThread::Mutex _lock; ///< protects init_

            friend class LutManager;
            ///private constructor, used by LutManager
            Lut(const std::string& name,OfxFromColorSpaceFunctionV1 fromFunc,OfxToColorSpaceFunctionV1 toFunc)
            : _name(name)
            , _fromFunc(fromFunc)
            , _toFunc(toFunc)
            , init_(false)
            , _lock()
            {
                
            }

            ///init luts
            ///it uses fromFloat(float) and toFloat(float)
            ///Called by validate()
            void fillTables() const;

            
            //Called by all public members
            void validate() const {
                OFX::MultiThread::AutoMutex g(_lock);
                if (init_) {
                    return;
                }
                fillTables();
                init_=true;
            }
            
            /* @brief Converts a float ranging in [0 - 1.f] in the desired color-space to linear color-space also ranging in [0 - 1.f]
             * This function is not fast!
             * @see fromFloatFast(float)
             */
            float fromFloat(float v) const { return _fromFunc(v); }
            
            /* @brief Converts a float ranging in [0 - 1.f] in  linear color-space to the desired color-space to also ranging in [0 - 1.f]
             * This function is not fast!
             * @see toFloatFast(float)
             */
            float toFloat(float v) const { return _toFunc(v); }
            
        public:
            
            const std::string& getName() const { return _name; }
            
            /* @brief Converts a float ranging in [0 - 1.f] in linear color-space using the look-up tables.
             * @return A float in [0 - 1.f] in the destination color-space.
             */
            float toFloatFast(float v) const;
            
            /* @brief Converts a float ranging in [0 - 1.f] in the destination color-space using the look-up tables.
             * @return A float in [0 - 1.f] in linear color-space.
             */
            float fromFloatFast(float v) const;
        
            
            /////@TODO the following functions expects a float input buffer, one could extend it to cover all bitdepths.
            
            /**
             * @brief Convert an array of linear floating point pixel values to an
             * array of destination lut values, with error diffusion to avoid posterizing
             * artifacts.
             *
             * \a W is the number of pixels to convert.
             * \a delta is the distance between the output bytes
             * (useful for interlacing them into a packed-pixels buffer).
             * \a alpha is a pointer to an extra alpha planar buffer if you want to premultiply by alpha the from channel.
             * The input and output buffers must not overlap in memory.
             **/
            void to_byte_planar(unsigned char* to, const float* from,int W,const float* alpha = NULL,int delta = -1) const;
            void to_short_planar(unsigned short* to, const float* from,int W,const float* alpha = NULL,int delta = -1) const;
            void to_float_planar(float* to, const float* from,int W,const float* alpha = NULL,int delta = -1) const;
            
            
            /**
             * @brief These functions work exactly like the to_X_planar functions but expect 2 buffers
             * pointing at (0,0) in an image and convert a rectangle of the image. It also supports
             * several pixel packing commonly used by openfx images.
             * Note that the conversionRect will be clipped to srcRoD and dstRoD to prevent bad memory access
             
             \arg 	- from - A pointer to the input buffer, pointing at (0,0) in the image.
             \arg 	- srcRoD - The region of definition of the input buffer.
             \arg 	- inputPacking - The pixel packing of the input buffer.
             
             \arg 	- conversionRect - The region we want to convert. This will be clipped
             agains srcRoD and dstRoD.
             
             \arg 	- to - A pointer to the output buffer, pointing at (0,0) in the image.
             \arg 	- dstRoD - The region of definition of the output buffer.
             \arg 	- outputPacking - The pixel packing of the output buffer.
             
             \arg premult - If true, it indicates we want to premultiply by the alpha channel
             the R,G and B channels if they exist.
             
             \arg invertY - If true then the output scan-line y of the output buffer
             should be converted with the scan-line (srcRoD.y2 - y - 1) of the
             input buffer.

             **/
            void to_byte_packed(unsigned char* to, const float* from,const OfxRectI& conversionRect,
                                        const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                        PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult) const;
            void to_short_packed(unsigned short* to, const float* from,const OfxRectI& conversionRect,
                                         const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                         PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult) const;
            void to_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                         const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                         PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult) const;
            
            
            
            /////@TODO the following functions expects a float output buffer, one could extend it to cover all bitdepths.
            
            /**
             * @brief Convert from a buffer in the input color-space to the output color-space.
             *
             * \a W is the number of pixels to convert.
             * \a delta is the distance between the output bytes
             * (useful for interlacing them).
             * \a alpha is a pointer to an extra planar buffer being the alpha channel of the image.
             * If the image was stored with premultiplication on, it will then unpremultiply by alpha
             * before doing the color-space conversion, and the multiply back by alpha.
             * The input and output buffers must not overlap in memory.
             **/
            void from_byte_planar(float* to,const unsigned char* from,
                                          int W,const unsigned char* alpha = NULL,int delta = -1) const;
            void from_short_planar(float* to,const unsigned short* from,
                                           int W,const unsigned short* alpha = NULL,int delta = -1) const;
            void from_float_planar(float* to,const float* from,
                                           int W,const float* alpha = NULL,int delta = -1) const;
            
            
            /**
             * @brief These functions work exactly like the to_X_planar functions but expect 2 buffers
             * pointing at (0,0) in an image and convert a rectangle of the image. It also supports
             * several pixel packing commonly used by openfx images.
             * Note that the conversionRect will be clipped to srcRoD and dstRoD to prevent bad memory access
             
             \arg 	- from - A pointer to the input buffer, pointing at (0,0) in the image.
             \arg 	- srcRoD - The region of definition of the input buffer.
             \arg 	- inputPacking - The pixel packing of the input buffer.
             
             \arg 	- conversionRect - The region we want to convert. This will be clipped
             agains srcRoD and dstRoD.
             
             \arg 	- to - A pointer to the output buffer, pointing at (0,0) in the image.
             \arg 	- dstRoD - The region of definition of the output buffer.
             \arg 	- outputPacking - The pixel packing of the output buffer.
             
             \arg premult - If true, it indicates we want to unpremultiply the R,G,B channels (if they exist) by the alpha channel
             before doing the color-space conversion, and multiply back by alpha.
             
             \arg invertY - If true then the output scan-line y of the output buffer
             should be converted with the scan-line (srcRoD.y2 - y - 1) of the
             input buffer.
             
             **/
            void from_byte_packed(float* to, const unsigned char* from,const OfxRectI& conversionRect,
                                          const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                          PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult) const;
            
            void from_short_packed(float* to, const unsigned short* from,const OfxRectI& conversionRect,
                                           const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                           PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult) const;
            
            void from_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                           const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                           PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult) const;
            
            
        };
        
        
        namespace Linear {
        
            ///utility functions
            inline float toFloat(unsigned char v) { return (float)v / 255.f; }
            inline float toFloat(unsigned short v) { return (float)v / 65535.f; }
            inline float toFloat(float v) { return v; }
            
            inline unsigned char fromFloatB(float v) { return (unsigned char)(v * 255.f); }
            inline unsigned short fromFloatS(float v) { return (unsigned short)(v * 65535.f); }
            inline float fromFloatF(float v) { return v; }
            
            /////the following functions expects a float input buffer, one could extend it to cover all bitdepths.
            
            /**
             * @brief Convert an array of linear floating point pixel values to an
             * array of destination lut values, with error diffusion to avoid posterizing
             * artifacts.
             *
             * \a W is the number of pixels to convert.
             * \a delta is the distance between the output bytes
             * (useful for interlacing them).
             * \a alpha is a pointer to an extra alpha planar buffer if you want to premultiply by alpha the from channel.
             * The input and output buffers must not overlap in memory.
             **/
            void to_byte_planar(unsigned char* to, const float* from,int W,const float* alpha = NULL,int delta = -1);
            void to_short_planar(unsigned short* to, const float* from,int W,const float* alpha = NULL,int delta = -1);
            void to_float_planar(float* to, const float* from,int W,const float* alpha = NULL,int delta = -1);
            
            
            /**
             * @brief These functions work exactly like the to_X_planar functions but expect 2 buffers
             * pointing at (0,0) in the image and convert a rectangle of the image. It also supports
             * several pixel packing commonly used by openfx images.
             * Note that the conversionRect will be clipped to srcRoD and dstRoD to prevent bad memory access
             \arg 	- from - A pointer to the input buffer, pointing at (0,0) in the image.
             \arg 	- srcRoD - The region of definition of the input buffer.
             \arg 	- inputPacking - The pixel packing of the input buffer.
             
             \arg 	- conversionRect - The region we want to convert. This will be clipped
             agains srcRoD and dstRoD.
             
             \arg 	- to - A pointer to the output buffer, pointing at (0,0) in the image.
             \arg 	- dstRoD - The region of definition of the output buffer.
             \arg 	- outputPacking - The pixel packing of the output buffer.
             
             \arg premult - If true, it indicates we want to premultiply by the alpha channel
             the R,G and B channels if they exist.
             
             \arg invertY - If true then the output scan-line y of the output buffer
             should be converted with the scan-line (srcRoD.y2 - y - 1) of the
             input buffer.
             **/
            void to_byte_packed(unsigned char* to, const float* from,const OfxRectI& conversionRect,
                                        const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                        PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult);
            void to_short_packed(unsigned short* to, const float* from,const OfxRectI& conversionRect,
                                         const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                         PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult);
            void to_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                         const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                         PixelPacking inputPacking,PixelPacking outputPacking,bool invertY,bool premult);
            
            
            
            /////the following functions expects a float output buffer, one could extend it to cover all bitdepths.
            
            /**
             * @brief Convert from a buffer in the input color-space to the output color-space.
             *
             * \a W is the number of pixels to convert.  \a delta is the distance
             * between the output bytes (useful for interlacing them).
             * The input and output buffers must not overlap in memory.
             **/
            
            void from_byte_planar(float* to,const unsigned char* from, int W,int delta = -1);
            void from_short_planar(float* to,const unsigned short* from,int W,int delta = -1);
            void from_float_planar(float* to,const float* from,int W,int delta = -1);
            
            
            /**
             * @brief These functions work exactly like the from_X_planar functions but expect 2 buffers
             * pointing at (0,0) in the image and convert a rectangle of the image. It also supports
             * several pixel packing commonly used by openfx images.
             * Note that the conversionRect will be clipped to srcRoD and dstRoD to prevent bad memory access
                
             \arg 	- from - A pointer to the input buffer, pointing at (0,0) in the image.
             \arg 	- srcRoD - The region of definition of the input buffer.
             \arg 	- inputPacking - The pixel packing of the input buffer.
             
             \arg 	- conversionRect - The region we want to convert. This will be clipped
             agains srcRoD and dstRoD.
             
             \arg 	- to - A pointer to the output buffer, pointing at (0,0) in the image.
             \arg 	- dstRoD - The region of definition of the output buffer.
             \arg 	- outputPacking - The pixel packing of the output buffer.

             \arg invertY - If true then the output scan-line y of the output buffer
             should be converted with the scan-line (srcRoD.y2 - y - 1) of the
             input buffer.
             **/
            void from_byte_packed(float* to, const unsigned char* from,const OfxRectI& conversionRect,
                                          const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                          PixelPacking inputPacking,PixelPacking outputPacking,bool invertY);
            
            void from_short_packed(float* to, const unsigned short* from,const OfxRectI& conversionRect,
                                           const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                           PixelPacking inputPacking,PixelPacking outputPacking,bool invertY);
            
            void from_float_packed(float* to, const float* from,const OfxRectI& conversionRect,
                                           const OfxRectI& srcRoD,const OfxRectI& dstRoD,
                                           PixelPacking inputPacking,PixelPacking outputPacking,bool invertY);
            
        }//namespace Linear
        
    } //namespace Color
} //namespace OFX



#endif
