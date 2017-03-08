/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2013-2017 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#include "PixelFormat.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace OFX {
namespace FFmpeg {

bool
pixelFormatIsYUV(AVPixelFormat pix_fmt)
{
    // from swscale_internal.h
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    return desc && !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 2;
}

bool
pixelFormatAlpha(AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);

    return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

int
pixelFormatBPP(const AVPixelFormat pixelFormat)
{
#if 1
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pixelFormat);

    return av_get_bits_per_pixel(desc);
#else
    switch (pixelFormat) {
        case AV_PIX_FMT_NONE:

            return 0;
            break;

        case AV_PIX_FMT_YUV420P:   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
            return 12;
            break;
        case AV_PIX_FMT_YUYV422:   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
            return 16;
            break;
        case AV_PIX_FMT_RGB24:     ///< packed RGB 8:8:8, 24bpp, RGBRGB...
            return 24;
            break;
        case AV_PIX_FMT_BGR24:     ///< packed RGB 8:8:8, 24bpp, BGRBGR...
            return 24;
            break;
        case AV_PIX_FMT_YUV422P:   ///< planar YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples)
            return 16;
            break;
        case AV_PIX_FMT_YUV444P:   ///< planar YUV 4:4:4, 24bpp, (1 Cr & Cb sample per 1x1 Y samples)
            return 24;
            break;
        case AV_PIX_FMT_YUV410P:   ///< planar YUV 4:1:0,  9bpp, (1 Cr & Cb sample per 4x4 Y samples)
            return 9;
            break;
        case AV_PIX_FMT_YUV411P:   ///< planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples)
            return 12;
            break;
        case AV_PIX_FMT_GRAY8:     ///<        Y        ,  8bpp
            return 8;
            break;
        case AV_PIX_FMT_MONOWHITE: ///<        Y        ,  1bpp, 0 is white, 1 is black, in each byte pixels are ordered from the msb to the lsb
            return 1;
            break;
        case AV_PIX_FMT_MONOBLACK: ///<        Y        ,  1bpp, 0 is black, 1 is white, in each byte pixels are ordered from the msb to the lsb
            return 1;
            break;
        case AV_PIX_FMT_PAL8:      ///< 8 bits with AV_PIX_FMT_RGB32 palette
            return 8;
            break;
        case AV_PIX_FMT_YUVJ420P:  ///< planar YUV 4:2:0, 12bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV420P and setting color_range
            return 12;
            break;
        case AV_PIX_FMT_YUVJ422P:  ///< planar YUV 4:2:2, 16bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV422P and setting color_range
            return 16;
            break;
        case AV_PIX_FMT_YUVJ444P:  ///< planar YUV 4:4:4, 24bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV444P and setting color_range
            return 24;
            break;
        case AV_PIX_FMT_UYVY422:   ///< packed YUV 4:2:2, 16bpp, Cb Y0 Cr Y1
            return 16;
            break;
        case AV_PIX_FMT_UYYVYY411: ///< packed YUV 4:1:1, 12bpp, Cb Y0 Y1 Cr Y2 Y3
            return 12;
            break;
        case AV_PIX_FMT_BGR8:      ///< packed RGB 3:3:2,  8bpp, (msb)2B 3G 3R(lsb)
            return 8;
            break;
        case AV_PIX_FMT_BGR4:      ///< packed RGB 1:2:1 bitstream,  4bpp, (msb)1B 2G 1R(lsb), a byte contains two pixels, the first pixel in the byte is the one composed by the 4 msb bits
            return 4;
            break;
        case AV_PIX_FMT_BGR4_BYTE: ///< packed RGB 1:2:1,  8bpp, (msb)1B 2G 1R(lsb)
            return 8;
            break;
        case AV_PIX_FMT_RGB8:      ///< packed RGB 3:3:2,  8bpp, (msb)2R 3G 3B(lsb)
            return 8;
            break;
        case AV_PIX_FMT_RGB4:      ///< packed RGB 1:2:1 bitstream,  4bpp, (msb)1R 2G 1B(lsb), a byte contains two pixels, the first pixel in the byte is the one composed by the 4 msb bits
            return 4;
            break;
        case AV_PIX_FMT_RGB4_BYTE: ///< packed RGB 1:2:1,  8bpp, (msb)1R 2G 1B(lsb)
            return 8;
            break;
        case AV_PIX_FMT_NV12:      ///< planar YUV 4:2:0, 12bpp, 1 plane for Y and 1 plane for the UV components, which are interleaved (first byte U and the following byte V)
            return 12;
            break;
        case AV_PIX_FMT_NV21:      ///< as above, but U and V bytes are swapped
            return 12;
            break;

        case AV_PIX_FMT_ARGB:      ///< packed ARGB 8:8:8:8, 32bpp, ARGBARGB...
            return 32;
            break;
        case AV_PIX_FMT_RGBA:      ///< packed RGBA 8:8:8:8, 32bpp, RGBARGBA...
            return 32;
            break;
        case AV_PIX_FMT_ABGR:      ///< packed ABGR 8:8:8:8, 32bpp, ABGRABGR...
            return 32;
            break;
        case AV_PIX_FMT_BGRA:      ///< packed BGRA 8:8:8:8, 32bpp, BGRABGRA...
            return 32;
            break;

        case AV_PIX_FMT_GRAY16BE:  ///<        Y        , 16bpp, big-endian
            return 16;
            break;
        case AV_PIX_FMT_GRAY16LE:  ///<        Y        , 16bpp, little-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV440P:   ///< planar YUV 4:4:0 (1 Cr & Cb sample per 1x2 Y samples)
            return 16;
            break;
        case AV_PIX_FMT_YUVJ440P:  ///< planar YUV 4:4:0 full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV440P and setting color_range
            return 16;
            break;
        case AV_PIX_FMT_YUVA420P:  ///< planar YUV 4:2:0, 20bpp, (1 Cr & Cb sample per 2x2 Y & A samples)
            return 20;
            break;
        case AV_PIX_FMT_RGB48BE:   ///< packed RGB 16:16:16, 48bpp, 16R, 16G, 16B, the 2-byte value for each R/G/B component is stored as big-endian
            return 48;
            break;
        case AV_PIX_FMT_RGB48LE:   ///< packed RGB 16:16:16, 48bpp, 16R, 16G, 16B, the 2-byte value for each R/G/B component is stored as little-endian
            return 48;
            break;

        case AV_PIX_FMT_RGB565BE:  ///< packed RGB 5:6:5, 16bpp, (msb)   5R 6G 5B(lsb), big-endian
            return 16;
            break;
        case AV_PIX_FMT_RGB565LE:  ///< packed RGB 5:6:5, 16bpp, (msb)   5R 6G 5B(lsb), little-endian
            return 16;
            break;
        case AV_PIX_FMT_RGB555BE:  ///< packed RGB 5:5:5, 16bpp, (msb)1X 5R 5G 5B(lsb), big-endian   , X=unused/undefined
            return 16;
            break;
        case AV_PIX_FMT_RGB555LE:  ///< packed RGB 5:5:5, 16bpp, (msb)1X 5R 5G 5B(lsb), little-endian, X=unused/undefined
            return 16;
            break;

        case AV_PIX_FMT_BGR565BE:  ///< packed BGR 5:6:5, 16bpp, (msb)   5B 6G 5R(lsb), big-endian
            return 16;
            break;
        case AV_PIX_FMT_BGR565LE:  ///< packed BGR 5:6:5, 16bpp, (msb)   5B 6G 5R(lsb), little-endian
            return 16;
            break;
        case AV_PIX_FMT_BGR555BE:  ///< packed BGR 5:5:5, 16bpp, (msb)1X 5B 5G 5R(lsb), big-endian   , X=unused/undefined
            return 16;
            break;
        case AV_PIX_FMT_BGR555LE:  ///< packed BGR 5:5:5, 16bpp, (msb)1X 5B 5G 5R(lsb), little-endian, X=unused/undefined
            return 16;
            break;

        case AV_PIX_FMT_YUV420P16LE:  ///< planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 24;
            break;
        case AV_PIX_FMT_YUV420P16BE:  ///< planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 24;
            break;
        case AV_PIX_FMT_YUV422P16LE:  ///< planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 32;
            break;
        case AV_PIX_FMT_YUV422P16BE:  ///< planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 32;
            break;
        case AV_PIX_FMT_YUV444P16LE:  ///< planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 48;
            break;
        case AV_PIX_FMT_YUV444P16BE:  ///< planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 48;
            break;
            
        case AV_PIX_FMT_RGB444LE:  ///< packed RGB 4:4:4, 16bpp, (msb)4X 4R 4G 4B(lsb), little-endian, X=unused/undefined
            return 16;
            break;
        case AV_PIX_FMT_RGB444BE:  ///< packed RGB 4:4:4, 16bpp, (msb)4X 4R 4G 4B(lsb), big-endian,    X=unused/undefined
            return 16;
            break;
        case AV_PIX_FMT_BGR444LE:  ///< packed BGR 4:4:4, 16bpp, (msb)4X 4B 4G 4R(lsb), little-endian, X=unused/undefined
            return 16;
            break;
        case AV_PIX_FMT_BGR444BE:  ///< packed BGR 4:4:4, 16bpp, (msb)4X 4B 4G 4R(lsb), big-endian,    X=unused/undefined
            return 16;
            break;
        case AV_PIX_FMT_YA8:       ///< 8 bits gray, 8 bits alpha
            return 16;
            break;

        case AV_PIX_FMT_BGR48BE:   ///< packed RGB 16:16:16, 48bpp, 16B, 16G, 16R, the 2-byte value for each R/G/B component is stored as big-endian
            return 48;
            break;
        case AV_PIX_FMT_BGR48LE:   ///< packed RGB 16:16:16, 48bpp, 16B, 16G, 16R, the 2-byte value for each R/G/B component is stored as little-endian
            return 48;
            break;

            /**
             * The following 12 formats have the disadvantage of needing 1 format for each bit depth.
             * Notice that each 9/10 bits sample is stored in 16 bits with extra padding.
             * If you want to support multiple bit depths, then using AV_PIX_FMT_YUV420P16* with the bpp stored separately is better.
             */
        case AV_PIX_FMT_YUV420P9BE: ///< planar YUV 4:2:0, 13.5bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return (int)13.5;
            break;
        case AV_PIX_FMT_YUV420P9LE: ///< planar YUV 4:2:0, 13.5bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return (int)13.5;
            break;
        case AV_PIX_FMT_YUV420P10BE:///< planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 15;
            break;
        case AV_PIX_FMT_YUV420P10LE:///< planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 15;
            break;
        case AV_PIX_FMT_YUV422P10BE:///< planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 20;
            break;
        case AV_PIX_FMT_YUV422P10LE:///< planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 20;
            break;
        case AV_PIX_FMT_YUV444P9BE: ///< planar YUV 4:4:4, 27bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 27;
            break;
        case AV_PIX_FMT_YUV444P9LE: ///< planar YUV 4:4:4, 27bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 27;
            break;
        case AV_PIX_FMT_YUV444P10BE:///< planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 30;
            break;
        case AV_PIX_FMT_YUV444P10LE:///< planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 30;
            break;
        case AV_PIX_FMT_YUV422P9BE: ///< planar YUV 4:2:2, 18bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 18;
            break;
        case AV_PIX_FMT_YUV422P9LE: ///< planar YUV 4:2:2, 18bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 18;
            break;
        case AV_PIX_FMT_GBRP:      ///< planar GBR 4:4:4 24bpp
            return 24;
            break;
        case AV_PIX_FMT_GBRP9BE:   ///< planar GBR 4:4:4 27bpp, big-endian
            return 27;
            break;
        case AV_PIX_FMT_GBRP9LE:   ///< planar GBR 4:4:4 27bpp, little-endian
            return 27;
            break;
        case AV_PIX_FMT_GBRP10BE:  ///< planar GBR 4:4:4 30bpp, big-endian
            return 30;
            break;
        case AV_PIX_FMT_GBRP10LE:  ///< planar GBR 4:4:4 30bpp, little-endian
            return 30;
            break;
        case AV_PIX_FMT_GBRP16BE:  ///< planar GBR 4:4:4 48bpp, big-endian
            return 48;
            break;
        case AV_PIX_FMT_GBRP16LE:  ///< planar GBR 4:4:4 48bpp, little-endian
            return 48;
            break;
        case AV_PIX_FMT_YUVA422P:  ///< planar YUV 4:2:2 24bpp, (1 Cr & Cb sample per 2x1 Y & A samples)
            return 24;
            break;
        case AV_PIX_FMT_YUVA444P:  ///< planar YUV 4:4:4 32bpp, (1 Cr & Cb sample per 1x1 Y & A samples)
            return 32;
            break;
        case AV_PIX_FMT_YUVA420P9BE:  ///< planar YUV 4:2:0 22.5bpp, (1 Cr & Cb sample per 2x2 Y & A samples), big-endian
            return (int)22.5;
            break;
        case AV_PIX_FMT_YUVA420P9LE:  ///< planar YUV 4:2:0 22.5bpp, (1 Cr & Cb sample per 2x2 Y & A samples), little-endian
            return (int)22.5;
            break;
        case AV_PIX_FMT_YUVA422P9BE:  ///< planar YUV 4:2:2 27bpp, (1 Cr & Cb sample per 2x1 Y & A samples), big-endian
            return 27;
            break;
        case AV_PIX_FMT_YUVA422P9LE:  ///< planar YUV 4:2:2 27bpp, (1 Cr & Cb sample per 2x1 Y & A samples), little-endian
            return 27;
            break;
        case AV_PIX_FMT_YUVA444P9BE:  ///< planar YUV 4:4:4 36bpp, (1 Cr & Cb sample per 1x1 Y & A samples), big-endian
            return 36;
            break;
        case AV_PIX_FMT_YUVA444P9LE:  ///< planar YUV 4:4:4 36bpp, (1 Cr & Cb sample per 1x1 Y & A samples), little-endian
            return 36;
            break;
        case AV_PIX_FMT_YUVA420P10BE: ///< planar YUV 4:2:0 25bpp, (1 Cr & Cb sample per 2x2 Y & A samples, big-endian)
            return 25;
            break;
        case AV_PIX_FMT_YUVA420P10LE: ///< planar YUV 4:2:0 25bpp, (1 Cr & Cb sample per 2x2 Y & A samples, little-endian)
            return 25;
            break;
        case AV_PIX_FMT_YUVA422P10BE: ///< planar YUV 4:2:2 30bpp, (1 Cr & Cb sample per 2x1 Y & A samples, big-endian)
            return 30;
            break;
        case AV_PIX_FMT_YUVA422P10LE: ///< planar YUV 4:2:2 30bpp, (1 Cr & Cb sample per 2x1 Y & A samples, little-endian)
            return 30;
            break;
        case AV_PIX_FMT_YUVA444P10BE: ///< planar YUV 4:4:4 40bpp, (1 Cr & Cb sample per 1x1 Y & A samples, big-endian)
            return 40;
            break;
        case AV_PIX_FMT_YUVA444P10LE: ///< planar YUV 4:4:4 40bpp, (1 Cr & Cb sample per 1x1 Y & A samples, little-endian)
            return 40;
            break;
        case AV_PIX_FMT_YUVA420P16BE: ///< planar YUV 4:2:0 40bpp, (1 Cr & Cb sample per 2x2 Y & A samples, big-endian)
            return 40;
            break;
        case AV_PIX_FMT_YUVA420P16LE: ///< planar YUV 4:2:0 40bpp, (1 Cr & Cb sample per 2x2 Y & A samples, little-endian)
            return 40;
            break;
        case AV_PIX_FMT_YUVA422P16BE: ///< planar YUV 4:2:2 48bpp, (1 Cr & Cb sample per 2x1 Y & A samples, big-endian)
            return 48;
            break;
        case AV_PIX_FMT_YUVA422P16LE: ///< planar YUV 4:2:2 48bpp, (1 Cr & Cb sample per 2x1 Y & A samples, little-endian)
            return 48;
            break;
        case AV_PIX_FMT_YUVA444P16BE: ///< planar YUV 4:4:4 64bpp, (1 Cr & Cb sample per 1x1 Y & A samples, big-endian)
            return 64;
            break;
        case AV_PIX_FMT_YUVA444P16LE: ///< planar YUV 4:4:4 64bpp, (1 Cr & Cb sample per 1x1 Y & A samples, little-endian)
            return 64;
            break;

        case AV_PIX_FMT_XYZ12LE:      ///< packed XYZ 4:4:4, 36 bpp, (msb) 12X, 12Y, 12Z (lsb), the 2-byte value for each X/Y/Z is stored as little-endian, the 4 lower bits are set to 0
            return 36;
            break;
        case AV_PIX_FMT_XYZ12BE:      ///< packed XYZ 4:4:4, 36 bpp, (msb) 12X, 12Y, 12Z (lsb), the 2-byte value for each X/Y/Z is stored as big-endian, the 4 lower bits are set to 0
            return 36;
            break;
        case AV_PIX_FMT_NV16:         ///< interleaved chroma YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples)
            return 16;
            break;
        case AV_PIX_FMT_NV20LE:       ///< interleaved chroma YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 20;
            break;
        case AV_PIX_FMT_NV20BE:       ///< interleaved chroma YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 20;
            break;

        case AV_PIX_FMT_RGBA64BE:     ///< packed RGBA 16:16:16:16, 64bpp, 16R, 16G, 16B, 16A, the 2-byte value for each R/G/B/A component is stored as big-endian
            return 64;
            break;
        case AV_PIX_FMT_RGBA64LE:     ///< packed RGBA 16:16:16:16, 64bpp, 16R, 16G, 16B, 16A, the 2-byte value for each R/G/B/A component is stored as little-endian
            return 64;
            break;
        case AV_PIX_FMT_BGRA64BE:     ///< packed RGBA 16:16:16:16, 64bpp, 16B, 16G, 16R, 16A, the 2-byte value for each R/G/B/A component is stored as big-endian
            return 64;
            break;
        case AV_PIX_FMT_BGRA64LE:     ///< packed RGBA 16:16:16:16, 64bpp, 16B, 16G, 16R, 16A, the 2-byte value for each R/G/B/A component is stored as little-endian
            return 64;
            break;

        case AV_PIX_FMT_YVYU422:   ///< packed YUV 4:2:2, 16bpp, Y0 Cr Y1 Cb
            return 16;
            break;

        case AV_PIX_FMT_YA16BE:       ///< 16 bits gray, 16 bits alpha (big-endian)
            return 32;
            break;
        case AV_PIX_FMT_YA16LE:       ///< 16 bits gray, 16 bits alpha (little-endian)
            return 32;
            break;

        case AV_PIX_FMT_GBRAP:        ///< planar GBRA 4:4:4:4 32bpp
            return 32;
            break;
        case AV_PIX_FMT_GBRAP16BE:    ///< planar GBRA 4:4:4:4 64bpp, big-endian
            return 64;
            break;
        case AV_PIX_FMT_GBRAP16LE:    ///< planar GBRA 4:4:4:4 64bpp, little-endian
            return 64;
            break;

        case AV_PIX_FMT_0RGB:///< packed RGB 8:8:8, 32bpp, XRGBXRGB...   X=unused/undefined
            return 32;
            break;
        case AV_PIX_FMT_RGB0:        ///< packed RGB 8:8:8, 32bpp, RGBXRGBX...   X=unused/undefined
            return 32;
            break;
        case AV_PIX_FMT_0BGR:        ///< packed BGR 8:8:8, 32bpp, XBGRXBGR...   X=unused/undefined
            return 32;
            break;
        case AV_PIX_FMT_BGR0:        ///< packed BGR 8:8:8, 32bpp, BGRXBGRX...   X=unused/undefined
            return 32;
            break;

        case AV_PIX_FMT_YUV420P12BE: ///< planar YUV 4:2:0,18bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 18;
            break;
        case AV_PIX_FMT_YUV420P12LE: ///< planar YUV 4:2:0,18bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 18;
            break;
        case AV_PIX_FMT_YUV420P14BE: ///< planar YUV 4:2:0,21bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 21;
            break;
        case AV_PIX_FMT_YUV420P14LE: ///< planar YUV 4:2:0,21bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 21;
            break;
        case AV_PIX_FMT_YUV422P12BE: ///< planar YUV 4:2:2,24bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 24;
            break;
        case AV_PIX_FMT_YUV422P12LE: ///< planar YUV 4:2:2,24bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 24;
            break;
        case AV_PIX_FMT_YUV422P14BE: ///< planar YUV 4:2:2,28bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 28;
            break;
        case AV_PIX_FMT_YUV422P14LE: ///< planar YUV 4:2:2,28bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 28;
            break;
        case AV_PIX_FMT_YUV444P12BE: ///< planar YUV 4:4:4,36bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 36;
            break;
        case AV_PIX_FMT_YUV444P12LE: ///< planar YUV 4:4:4,36bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 36;
            break;
        case AV_PIX_FMT_YUV444P14BE: ///< planar YUV 4:4:4,42bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 42;
            break;
        case AV_PIX_FMT_YUV444P14LE: ///< planar YUV 4:4:4,42bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 42;
            break;
        case AV_PIX_FMT_GBRP12BE:    ///< planar GBR 4:4:4 36bpp, big-endian
            return 36;
            break;
        case AV_PIX_FMT_GBRP12LE:    ///< planar GBR 4:4:4 36bpp, little-endian
            return 36;
            break;
        case AV_PIX_FMT_GBRP14BE:    ///< planar GBR 4:4:4 42bpp, big-endian
            return 42;
            break;
        case AV_PIX_FMT_GBRP14LE:    ///< planar GBR 4:4:4 42bpp, little-endian
            return 42;
            break;
        case AV_PIX_FMT_YUVJ411P:    ///< planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples) full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV411P and setting color_range
            return 12;
            break;

        case AV_PIX_FMT_BAYER_BGGR8:    ///< bayer, BGBG..(odd line), GRGR..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_RGGB8:    ///< bayer, RGRG..(odd line), GBGB..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_GBRG8:    ///< bayer, GBGB..(odd line), RGRG..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_GRBG8:    ///< bayer, GRGR..(odd line), BGBG..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_BGGR16LE: ///< bayer, BGBG..(odd line), GRGR..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_BGGR16BE: ///< bayer, BGBG..(odd line), GRGR..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_RGGB16LE: ///< bayer, RGRG..(odd line), GBGB..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_RGGB16BE: ///< bayer, RGRG..(odd line), GBGB..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GBRG16LE: ///< bayer, GBGB..(odd line), RGRG..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GBRG16BE: ///< bayer, GBGB..(odd line), RGRG..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GRBG16LE: ///< bayer, GRGR..(odd line), BGBG..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GRBG16BE: ///< bayer, GRGR..(odd line), BGBG..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_YUV440P10LE: ///< planar YUV 4:4:0,20bpp, (1 Cr & Cb sample per 1x2 Y samples), little-endian
            return 20;
            break;
        case AV_PIX_FMT_YUV440P10BE: ///< planar YUV 4:4:0,20bpp, (1 Cr & Cb sample per 1x2 Y samples), big-endian
            return 20;
            break;
        case AV_PIX_FMT_YUV440P12LE: ///< planar YUV 4:4:0,24bpp, (1 Cr & Cb sample per 1x2 Y samples), little-endian
            return 24;
            break;
        case AV_PIX_FMT_YUV440P12BE: ///< planar YUV 4:4:0,24bpp, (1 Cr & Cb sample per 1x2 Y samples), big-endian
            return 24;
            break;
        case AV_PIX_FMT_AYUV64LE:    ///< packed AYUV 4:4:4,64bpp (1 Cr & Cb sample per 1x1 Y & A samples), little-endian
            return 64;
            break;
        case AV_PIX_FMT_AYUV64BE:    ///< packed AYUV 4:4:4,64bpp (1 Cr & Cb sample per 1x1 Y & A samples), big-endian
            return 64;
            break;
            
        case AV_PIX_FMT_P010LE: ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, little-endian
            return 10;
            break;
        case AV_PIX_FMT_P010BE: ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, big-endian
            return 10;
            break;
            
        case AV_PIX_FMT_GBRAP12BE:  ///< planar GBR 4:4:4:4 48bpp, big-endian
            return 48;
            break;
        case AV_PIX_FMT_GBRAP12LE:  ///< planar GBR 4:4:4:4 48bpp, little-endian
            return 48;
            break;
            
        case AV_PIX_FMT_GBRAP10BE:  ///< planar GBR 4:4:4:4 40bpp, big-endian
            return 40;
            break;
        case AV_PIX_FMT_GBRAP10LE:  ///< planar GBR 4:4:4:4 40bpp, little-endian
            return 40;
            break;

        default:
            return 0;
            break;
    } // switch
#endif
} // WriteFFmpegPlugin::pixelFormatBPP


// av_get_bits_per_sample knows about surprisingly few codecs.
// We have to do this manually.
/*static*/
int
pixelFormatBitDepth(const AVPixelFormat pixelFormat)
{
    switch (pixelFormat) {
        case AV_PIX_FMT_NONE:

            return 0;
            break;

        case AV_PIX_FMT_YUV420P:   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_YUYV422:   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
            return 8;
            break;
        case AV_PIX_FMT_RGB24:     ///< packed RGB 8:8:8, 24bpp, RGBRGB...
            return 8;
            break;
        case AV_PIX_FMT_BGR24:     ///< packed RGB 8:8:8, 24bpp, BGRBGR...
            return 8;
            break;
        case AV_PIX_FMT_YUV422P:   ///< planar YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_YUV444P:   ///< planar YUV 4:4:4, 24bpp, (1 Cr & Cb sample per 1x1 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_YUV410P:   ///< planar YUV 4:1:0,  9bpp, (1 Cr & Cb sample per 4x4 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_YUV411P:   ///< planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_GRAY8:     ///<        Y        ,  8bpp
            return 8;
            break;
        case AV_PIX_FMT_MONOWHITE: ///<        Y        ,  1bpp, 0 is white, 1 is black, in each byte pixels are ordered from the msb to the lsb
            return 1;
            break;
        case AV_PIX_FMT_MONOBLACK: ///<        Y        ,  1bpp, 0 is black, 1 is white, in each byte pixels are ordered from the msb to the lsb
            return 1;
            break;
        case AV_PIX_FMT_PAL8:      ///< 8 bits with AV_PIX_FMT_RGB32 palette
            return 8;
            break;
        case AV_PIX_FMT_YUVJ420P:  ///< planar YUV 4:2:0, 12bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV420P and setting color_range
            return 8;
            break;
        case AV_PIX_FMT_YUVJ422P:  ///< planar YUV 4:2:2, 16bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV422P and setting color_range
            return 8;
            break;
        case AV_PIX_FMT_YUVJ444P:  ///< planar YUV 4:4:4, 24bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV444P and setting color_range
            return 8;
            break;
        case AV_PIX_FMT_UYVY422:   ///< packed YUV 4:2:2, 16bpp, Cb Y0 Cr Y1
            return 8;
            break;
        case AV_PIX_FMT_UYYVYY411: ///< packed YUV 4:1:1, 12bpp, Cb Y0 Y1 Cr Y2 Y3
            return 8;
            break;
        case AV_PIX_FMT_BGR8:      ///< packed RGB 3:3:2,  8bpp, (msb)2B 3G 3R(lsb)
            return 2;
            break;
        case AV_PIX_FMT_BGR4:      ///< packed RGB 1:2:1 bitstream,  4bpp, (msb)1B 2G 1R(lsb), a byte contains two pixels, the first pixel in the byte is the one composed by the 4 msb bits
            return 1;
            break;
        case AV_PIX_FMT_BGR4_BYTE: ///< packed RGB 1:2:1,  8bpp, (msb)1B 2G 1R(lsb)
            return 1;
            break;
        case AV_PIX_FMT_RGB8:      ///< packed RGB 3:3:2,  8bpp, (msb)2R 3G 3B(lsb)
            return 2;
            break;
        case AV_PIX_FMT_RGB4:      ///< packed RGB 1:2:1 bitstream,  4bpp, (msb)1R 2G 1B(lsb), a byte contains two pixels, the first pixel in the byte is the one composed by the 4 msb bits
            return 1;
            break;
        case AV_PIX_FMT_RGB4_BYTE: ///< packed RGB 1:2:1,  8bpp, (msb)1R 2G 1B(lsb)
            return 1;
            break;
        case AV_PIX_FMT_NV12:      ///< planar YUV 4:2:0, 12bpp, 1 plane for Y and 1 plane for the UV components, which are interleaved (first byte U and the following byte V)
            return 8;
            break;
        case AV_PIX_FMT_NV21:      ///< as above, but U and V bytes are swapped
            return 8;
            break;

        case AV_PIX_FMT_ARGB:      ///< packed ARGB 8:8:8:8, 32bpp, ARGBARGB...
            return 8;
            break;
        case AV_PIX_FMT_RGBA:      ///< packed RGBA 8:8:8:8, 32bpp, RGBARGBA...
            return 8;
            break;
        case AV_PIX_FMT_ABGR:      ///< packed ABGR 8:8:8:8, 32bpp, ABGRABGR...
            return 8;
            break;
        case AV_PIX_FMT_BGRA:      ///< packed BGRA 8:8:8:8, 32bpp, BGRABGRA...
            return 8;
            break;

        case AV_PIX_FMT_GRAY16BE:  ///<        Y        , 16bpp, big-endian
            return 16;
            break;
        case AV_PIX_FMT_GRAY16LE:  ///<        Y        , 16bpp, little-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV440P:   ///< planar YUV 4:4:0 (1 Cr & Cb sample per 1x2 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_YUVJ440P:  ///< planar YUV 4:4:0 full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV440P and setting color_range
            return 8;
            break;
        case AV_PIX_FMT_YUVA420P:  ///< planar YUV 4:2:0, 20bpp, (1 Cr & Cb sample per 2x2 Y & A samples)
            return 8;
            break;
        case AV_PIX_FMT_RGB48BE:   ///< packed RGB 16:16:16, 48bpp, 16R, 16G, 16B, the 2-byte value for each R/G/B component is stored as big-endian
            return 16;
            break;
        case AV_PIX_FMT_RGB48LE:   ///< packed RGB 16:16:16, 48bpp, 16R, 16G, 16B, the 2-byte value for each R/G/B component is stored as little-endian
            return 16;
            break;

        case AV_PIX_FMT_RGB565BE:  ///< packed RGB 5:6:5, 16bpp, (msb)   5R 6G 5B(lsb), big-endian
            return 5;
            break;
        case AV_PIX_FMT_RGB565LE:  ///< packed RGB 5:6:5, 16bpp, (msb)   5R 6G 5B(lsb), little-endian
            return 5;
            break;
        case AV_PIX_FMT_RGB555BE:  ///< packed RGB 5:5:5, 16bpp, (msb)1X 5R 5G 5B(lsb), big-endian   , X=unused/undefined
            return 5;
            break;
        case AV_PIX_FMT_RGB555LE:  ///< packed RGB 5:5:5, 16bpp, (msb)1X 5R 5G 5B(lsb), little-endian, X=unused/undefined
            return 5;
            break;

        case AV_PIX_FMT_BGR565BE:  ///< packed BGR 5:6:5, 16bpp, (msb)   5B 6G 5R(lsb), big-endian
            return 5;
            break;
        case AV_PIX_FMT_BGR565LE:  ///< packed BGR 5:6:5, 16bpp, (msb)   5B 6G 5R(lsb), little-endian
            return 5;
            break;
        case AV_PIX_FMT_BGR555BE:  ///< packed BGR 5:5:5, 16bpp, (msb)1X 5B 5G 5R(lsb), big-endian   , X=unused/undefined
            return 5;
            break;
        case AV_PIX_FMT_BGR555LE:  ///< packed BGR 5:5:5, 16bpp, (msb)1X 5B 5G 5R(lsb), little-endian, X=unused/undefined
            return 5;
            break;

        case AV_PIX_FMT_YUV420P16LE:  ///< planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV420P16BE:  ///< planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV422P16LE:  ///< planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV422P16BE:  ///< planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV444P16LE:  ///< planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 16;
            break;
        case AV_PIX_FMT_YUV444P16BE:  ///< planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 16;
            break;
            
        case AV_PIX_FMT_RGB444LE:  ///< packed RGB 4:4:4, 16bpp, (msb)4X 4R 4G 4B(lsb), little-endian, X=unused/undefined
            return 4;
            break;
        case AV_PIX_FMT_RGB444BE:  ///< packed RGB 4:4:4, 16bpp, (msb)4X 4R 4G 4B(lsb), big-endian,    X=unused/undefined
            return 4;
            break;
        case AV_PIX_FMT_BGR444LE:  ///< packed BGR 4:4:4, 16bpp, (msb)4X 4B 4G 4R(lsb), little-endian, X=unused/undefined
            return 4;
            break;
        case AV_PIX_FMT_BGR444BE:  ///< packed BGR 4:4:4, 16bpp, (msb)4X 4B 4G 4R(lsb), big-endian,    X=unused/undefined
            return 4;
            break;
        case AV_PIX_FMT_YA8:       ///< 8 bits gray, 8 bits alpha
            return 8;
            break;

        case AV_PIX_FMT_BGR48BE:   ///< packed RGB 16:16:16, 48bpp, 16B, 16G, 16R, the 2-byte value for each R/G/B component is stored as big-endian
            return 16;
            break;
        case AV_PIX_FMT_BGR48LE:   ///< packed RGB 16:16:16, 48bpp, 16B, 16G, 16R, the 2-byte value for each R/G/B component is stored as little-endian
            return 16;
            break;

            /**
             * The following 12 formats have the disadvantage of needing 1 format for each bit depth.
             * Notice that each 9/10 bits sample is stored in 16 bits with extra padding.
             * If you want to support multiple bit depths, then using AV_PIX_FMT_YUV420P16* with the bpp stored separately is better.
             */
        case AV_PIX_FMT_YUV420P9BE: ///< planar YUV 4:2:0, 13.5bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 9;
            break;
        case AV_PIX_FMT_YUV420P9LE: ///< planar YUV 4:2:0, 13.5bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 9;
            break;
        case AV_PIX_FMT_YUV420P10BE:///< planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV420P10LE:///< planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV422P10BE:///< planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV422P10LE:///< planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV444P9BE: ///< planar YUV 4:4:4, 27bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 9;
            break;
        case AV_PIX_FMT_YUV444P9LE: ///< planar YUV 4:4:4, 27bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 9;
            break;
        case AV_PIX_FMT_YUV444P10BE:///< planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV444P10LE:///< planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV422P9BE: ///< planar YUV 4:2:2, 18bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 9;
            break;
        case AV_PIX_FMT_YUV422P9LE: ///< planar YUV 4:2:2, 18bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 9;
            break;
        case AV_PIX_FMT_GBRP:      ///< planar GBR 4:4:4 24bpp
            return 8;
            break;
        case AV_PIX_FMT_GBRP9BE:   ///< planar GBR 4:4:4 27bpp, big-endian
            return 9;
            break;
        case AV_PIX_FMT_GBRP9LE:   ///< planar GBR 4:4:4 27bpp, little-endian
            return 9;
            break;
        case AV_PIX_FMT_GBRP10BE:  ///< planar GBR 4:4:4 30bpp, big-endian
            return 10;
            break;
        case AV_PIX_FMT_GBRP10LE:  ///< planar GBR 4:4:4 30bpp, little-endian
            return 10;
            break;
        case AV_PIX_FMT_GBRP16BE:  ///< planar GBR 4:4:4 48bpp, big-endian
            return 16;
            break;
        case AV_PIX_FMT_GBRP16LE:  ///< planar GBR 4:4:4 48bpp, little-endian
            return 16;
            break;
        case AV_PIX_FMT_YUVA422P:  ///< planar YUV 4:2:2 24bpp, (1 Cr & Cb sample per 2x1 Y & A samples)
            return 8;
            break;
        case AV_PIX_FMT_YUVA444P:  ///< planar YUV 4:4:4 32bpp, (1 Cr & Cb sample per 1x1 Y & A samples)
            return 8;
            break;
        case AV_PIX_FMT_YUVA420P9BE:  ///< planar YUV 4:2:0 22.5bpp, (1 Cr & Cb sample per 2x2 Y & A samples), big-endian
            return 9;
            break;
        case AV_PIX_FMT_YUVA420P9LE:  ///< planar YUV 4:2:0 22.5bpp, (1 Cr & Cb sample per 2x2 Y & A samples), little-endian
            return 9;
            break;
        case AV_PIX_FMT_YUVA422P9BE:  ///< planar YUV 4:2:2 27bpp, (1 Cr & Cb sample per 2x1 Y & A samples), big-endian
            return 9;
            break;
        case AV_PIX_FMT_YUVA422P9LE:  ///< planar YUV 4:2:2 27bpp, (1 Cr & Cb sample per 2x1 Y & A samples), little-endian
            return 9;
            break;
        case AV_PIX_FMT_YUVA444P9BE:  ///< planar YUV 4:4:4 36bpp, (1 Cr & Cb sample per 1x1 Y & A samples), big-endian
            return 9;
            break;
        case AV_PIX_FMT_YUVA444P9LE:  ///< planar YUV 4:4:4 36bpp, (1 Cr & Cb sample per 1x1 Y & A samples), little-endian
            return 9;
            break;
        case AV_PIX_FMT_YUVA420P10BE: ///< planar YUV 4:2:0 25bpp, (1 Cr & Cb sample per 2x2 Y & A samples, big-endian)
            return 10;
            break;
        case AV_PIX_FMT_YUVA420P10LE: ///< planar YUV 4:2:0 25bpp, (1 Cr & Cb sample per 2x2 Y & A samples, little-endian)
            return 10;
            break;
        case AV_PIX_FMT_YUVA422P10BE: ///< planar YUV 4:2:2 30bpp, (1 Cr & Cb sample per 2x1 Y & A samples, big-endian)
            return 10;
            break;
        case AV_PIX_FMT_YUVA422P10LE: ///< planar YUV 4:2:2 30bpp, (1 Cr & Cb sample per 2x1 Y & A samples, little-endian)
            return 10;
            break;
        case AV_PIX_FMT_YUVA444P10BE: ///< planar YUV 4:4:4 40bpp, (1 Cr & Cb sample per 1x1 Y & A samples, big-endian)
            return 10;
            break;
        case AV_PIX_FMT_YUVA444P10LE: ///< planar YUV 4:4:4 40bpp, (1 Cr & Cb sample per 1x1 Y & A samples, little-endian)
            return 10;
            break;
        case AV_PIX_FMT_YUVA420P16BE: ///< planar YUV 4:2:0 40bpp, (1 Cr & Cb sample per 2x2 Y & A samples, big-endian)
            return 16;
            break;
        case AV_PIX_FMT_YUVA420P16LE: ///< planar YUV 4:2:0 40bpp, (1 Cr & Cb sample per 2x2 Y & A samples, little-endian)
            return 16;
            break;
        case AV_PIX_FMT_YUVA422P16BE: ///< planar YUV 4:2:2 48bpp, (1 Cr & Cb sample per 2x1 Y & A samples, big-endian)
            return 16;
            break;
        case AV_PIX_FMT_YUVA422P16LE: ///< planar YUV 4:2:2 48bpp, (1 Cr & Cb sample per 2x1 Y & A samples, little-endian)
            return 16;
            break;
        case AV_PIX_FMT_YUVA444P16BE: ///< planar YUV 4:4:4 64bpp, (1 Cr & Cb sample per 1x1 Y & A samples, big-endian)
            return 16;
            break;
        case AV_PIX_FMT_YUVA444P16LE: ///< planar YUV 4:4:4 64bpp, (1 Cr & Cb sample per 1x1 Y & A samples, little-endian)
            return 16;
            break;

        case AV_PIX_FMT_XYZ12LE:      ///< packed XYZ 4:4:4, 36 bpp, (msb) 12X, 12Y, 12Z (lsb), the 2-byte value for each X/Y/Z is stored as little-endian, the 4 lower bits are set to 0
            return 12;
            break;
        case AV_PIX_FMT_XYZ12BE:      ///< packed XYZ 4:4:4, 36 bpp, (msb) 12X, 12Y, 12Z (lsb), the 2-byte value for each X/Y/Z is stored as big-endian, the 4 lower bits are set to 0
            return 12;
            break;
        case AV_PIX_FMT_NV16:         ///< interleaved chroma YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples)
            return 8;
            break;
        case AV_PIX_FMT_NV20LE:       ///< interleaved chroma YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 10;
            break;
        case AV_PIX_FMT_NV20BE:       ///< interleaved chroma YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 10;
            break;

        case AV_PIX_FMT_RGBA64BE:     ///< packed RGBA 16:16:16:16, 64bpp, 16R, 16G, 16B, 16A, the 2-byte value for each R/G/B/A component is stored as big-endian
            return 16;
            break;
        case AV_PIX_FMT_RGBA64LE:     ///< packed RGBA 16:16:16:16, 64bpp, 16R, 16G, 16B, 16A, the 2-byte value for each R/G/B/A component is stored as little-endian
            return 16;
            break;
        case AV_PIX_FMT_BGRA64BE:     ///< packed RGBA 16:16:16:16, 64bpp, 16B, 16G, 16R, 16A, the 2-byte value for each R/G/B/A component is stored as big-endian
            return 16;
            break;
        case AV_PIX_FMT_BGRA64LE:     ///< packed RGBA 16:16:16:16, 64bpp, 16B, 16G, 16R, 16A, the 2-byte value for each R/G/B/A component is stored as little-endian
            return 16;
            break;

        case AV_PIX_FMT_YVYU422:   ///< packed YUV 4:2:2, 16bpp, Y0 Cr Y1 Cb
            return 8;
            break;

        case AV_PIX_FMT_YA16BE:       ///< 16 bits gray, 16 bits alpha (big-endian)
            return 16;
            break;
        case AV_PIX_FMT_YA16LE:       ///< 16 bits gray, 16 bits alpha (little-endian)
            return 16;
            break;

        case AV_PIX_FMT_GBRAP:        ///< planar GBRA 4:4:4:4 32bpp
            return 8;
            break;
        case AV_PIX_FMT_GBRAP16BE:    ///< planar GBRA 4:4:4:4 64bpp, big-endian
            return 16;
            break;
        case AV_PIX_FMT_GBRAP16LE:    ///< planar GBRA 4:4:4:4 64bpp, little-endian
            return 16;
            break;

        case AV_PIX_FMT_0RGB:///< packed RGB 8:8:8, 32bpp, XRGBXRGB...   X=unused/undefined
            return 8;
            break;
        case AV_PIX_FMT_RGB0:        ///< packed RGB 8:8:8, 32bpp, RGBXRGBX...   X=unused/undefined
            return 8;
            break;
        case AV_PIX_FMT_0BGR:        ///< packed BGR 8:8:8, 32bpp, XBGRXBGR...   X=unused/undefined
            return 8;
            break;
        case AV_PIX_FMT_BGR0:        ///< packed BGR 8:8:8, 32bpp, BGRXBGRX...   X=unused/undefined
            return 8;
            break;

        case AV_PIX_FMT_YUV420P12BE: ///< planar YUV 4:2:0,18bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV420P12LE: ///< planar YUV 4:2:0,18bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV420P14BE: ///< planar YUV 4:2:0,21bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return 14;
            break;
        case AV_PIX_FMT_YUV420P14LE: ///< planar YUV 4:2:0,21bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return 14;
            break;
        case AV_PIX_FMT_YUV422P12BE: ///< planar YUV 4:2:2,24bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV422P12LE: ///< planar YUV 4:2:2,24bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV422P14BE: ///< planar YUV 4:2:2,28bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return 14;
            break;
        case AV_PIX_FMT_YUV422P14LE: ///< planar YUV 4:2:2,28bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return 14;
            break;
        case AV_PIX_FMT_YUV444P12BE: ///< planar YUV 4:4:4,36bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV444P12LE: ///< planar YUV 4:4:4,36bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV444P14BE: ///< planar YUV 4:4:4,42bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return 14;
            break;
        case AV_PIX_FMT_YUV444P14LE: ///< planar YUV 4:4:4,42bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return 14;
            break;
        case AV_PIX_FMT_GBRP12BE:    ///< planar GBR 4:4:4 36bpp, big-endian
            return 12;
            break;
        case AV_PIX_FMT_GBRP12LE:    ///< planar GBR 4:4:4 36bpp, little-endian
            return 12;
            break;
        case AV_PIX_FMT_GBRP14BE:    ///< planar GBR 4:4:4 42bpp, big-endian
            return 14;
            break;
        case AV_PIX_FMT_GBRP14LE:    ///< planar GBR 4:4:4 42bpp, little-endian
            return 14;
            break;
        case AV_PIX_FMT_YUVJ411P:    ///< planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples) full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV411P and setting color_range
            return 8;
            break;

        case AV_PIX_FMT_BAYER_BGGR8:    ///< bayer, BGBG..(odd line), GRGR..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_RGGB8:    ///< bayer, RGRG..(odd line), GBGB..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_GBRG8:    ///< bayer, GBGB..(odd line), RGRG..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_GRBG8:    ///< bayer, GRGR..(odd line), BGBG..(even line), 8-bit samples */
            return 8;
            break;
        case AV_PIX_FMT_BAYER_BGGR16LE: ///< bayer, BGBG..(odd line), GRGR..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_BGGR16BE: ///< bayer, BGBG..(odd line), GRGR..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_RGGB16LE: ///< bayer, RGRG..(odd line), GBGB..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_RGGB16BE: ///< bayer, RGRG..(odd line), GBGB..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GBRG16LE: ///< bayer, GBGB..(odd line), RGRG..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GBRG16BE: ///< bayer, GBGB..(odd line), RGRG..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GRBG16LE: ///< bayer, GRGR..(odd line), BGBG..(even line), 16-bit samples, little-endian */
            return 16;
            break;
        case AV_PIX_FMT_BAYER_GRBG16BE: ///< bayer, GRGR..(odd line), BGBG..(even line), 16-bit samples, big-endian */
            return 16;
            break;
        case AV_PIX_FMT_YUV440P10LE: ///< planar YUV 4:4:0,20bpp, (1 Cr & Cb sample per 1x2 Y samples), little-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV440P10BE: ///< planar YUV 4:4:0,20bpp, (1 Cr & Cb sample per 1x2 Y samples), big-endian
            return 10;
            break;
        case AV_PIX_FMT_YUV440P12LE: ///< planar YUV 4:4:0,24bpp, (1 Cr & Cb sample per 1x2 Y samples), little-endian
            return 12;
            break;
        case AV_PIX_FMT_YUV440P12BE: ///< planar YUV 4:4:0,24bpp, (1 Cr & Cb sample per 1x2 Y samples), big-endian
            return 12;
            break;
        case AV_PIX_FMT_AYUV64LE:    ///< packed AYUV 4:4:4,64bpp (1 Cr & Cb sample per 1x1 Y & A samples), little-endian
            return 16;
            break;
        case AV_PIX_FMT_AYUV64BE:    ///< packed AYUV 4:4:4,64bpp (1 Cr & Cb sample per 1x1 Y & A samples), big-endian
            return 16;
            break;
            
        case AV_PIX_FMT_P010LE: ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, little-endian
            return 10;
            break;
        case AV_PIX_FMT_P010BE: ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, big-endian
            return 10;
            break;
            
        case AV_PIX_FMT_GBRAP12BE:  ///< planar GBR 4:4:4:4 48bpp, big-endian
            return 12;
            break;
        case AV_PIX_FMT_GBRAP12LE:  ///< planar GBR 4:4:4:4 48bpp, little-endian
            return 12;
            break;
            
        case AV_PIX_FMT_GBRAP10BE:  ///< planar GBR 4:4:4:4 40bpp, big-endian
            return 10;
            break;
        case AV_PIX_FMT_GBRAP10LE:  ///< planar GBR 4:4:4:4 40bpp, little-endian
            return 10;
            break;

        default:
#if OFX_FFMPEG_PRINT_CODECS
            std::cout << "** Format " << av_get_pix_fmt_name(pixelFormat) << "not handled" << std::endl;
#endif

            return 0;
    } // switch
    
} // WriteFFmpegPlugin::pixelFormatBitDepth



PixelCodingEnum
pixelFormatCoding(const AVPixelFormat pixelFormat)
{
    switch (pixelFormat) {
        case AV_PIX_FMT_NONE:

            return ePixelCodingNone;
            break;

        case AV_PIX_FMT_YUV420P:   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUYV422:   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_RGB24:     ///< packed RGB 8:8:8, 24bpp, RGBRGB...
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR24:     ///< packed RGB 8:8:8, 24bpp, BGRBGR...
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_YUV422P:   ///< planar YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV444P:   ///< planar YUV 4:4:4, 24bpp, (1 Cr & Cb sample per 1x1 Y samples)
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV410P:   ///< planar YUV 4:1:0,  9bpp, (1 Cr & Cb sample per 4x4 Y samples)
            return ePixelCodingYUV410;
            break;
        case AV_PIX_FMT_YUV411P:   ///< planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples)
            return ePixelCodingYUV411;
            break;
        case AV_PIX_FMT_GRAY8:     ///<        Y        ,  8bpp
            return ePixelCodingGray;
            break;
        case AV_PIX_FMT_MONOWHITE: ///<        Y        ,  1bpp, 0 is white, 1 is black, in each byte pixels are ordered from the msb to the lsb
            return ePixelCodingGray;
            break;
        case AV_PIX_FMT_MONOBLACK: ///<        Y        ,  1bpp, 0 is black, 1 is white, in each byte pixels are ordered from the msb to the lsb
            return ePixelCodingGray;
            break;
        case AV_PIX_FMT_PAL8:      ///< 8 bits with AV_PIX_FMT_RGB32 palette
            return ePixelCodingPalette;
            break;
        case AV_PIX_FMT_YUVJ420P:  ///< planar YUV 4:2:0, 12bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV420P and setting color_range
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVJ422P:  ///< planar YUV 4:2:2, 16bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV422P and setting color_range
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVJ444P:  ///< planar YUV 4:4:4, 24bpp, full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV444P and setting color_range
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_UYVY422:   ///< packed YUV 4:2:2, 16bpp, Cb Y0 Cr Y1
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_UYYVYY411: ///< packed YUV 4:1:1, 12bpp, Cb Y0 Y1 Cr Y2 Y3
            return ePixelCodingYUV411;
            break;
        case AV_PIX_FMT_BGR8:      ///< packed RGB 3:3:2,  8bpp, (msb)2B 3G 3R(lsb)
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR4:      ///< packed RGB 1:2:1 bitstream,  4bpp, (msb)1B 2G 1R(lsb), a byte contains two pixels, the first pixel in the byte is the one composed by the 4 msb bits
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR4_BYTE: ///< packed RGB 1:2:1,  8bpp, (msb)1B 2G 1R(lsb)
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB8:      ///< packed RGB 3:3:2,  8bpp, (msb)2R 3G 3B(lsb)
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB4:      ///< packed RGB 1:2:1 bitstream,  4bpp, (msb)1R 2G 1B(lsb), a byte contains two pixels, the first pixel in the byte is the one composed by the 4 msb bits
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB4_BYTE: ///< packed RGB 1:2:1,  8bpp, (msb)1R 2G 1B(lsb)
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_NV12:      ///< planar YUV 4:2:0, 12bpp, 1 plane for Y and 1 plane for the UV components, which are interleaved (first byte U and the following byte V)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_NV21:      ///< as above, but U and V bytes are swapped
            return ePixelCodingYUV420;
            break;

        case AV_PIX_FMT_ARGB:      ///< packed ARGB 8:8:8:8, 32bpp, ARGBARGB...
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGBA:      ///< packed RGBA 8:8:8:8, 32bpp, RGBARGBA...
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_ABGR:      ///< packed ABGR 8:8:8:8, 32bpp, ABGRABGR...
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGRA:      ///< packed BGRA 8:8:8:8, 32bpp, BGRABGRA...
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_GRAY16BE:  ///<        Y        , 16bpp, big-endian
            return ePixelCodingGray;
            break;
        case AV_PIX_FMT_GRAY16LE:  ///<        Y        , 16bpp, little-endian
            return ePixelCodingGray;
            break;
        case AV_PIX_FMT_YUV440P:   ///< planar YUV 4:4:0 (1 Cr & Cb sample per 1x2 Y samples)
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_YUVJ440P:  ///< planar YUV 4:4:0 full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV440P and setting color_range
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_YUVA420P:  ///< planar YUV 4:2:0, 20bpp, (1 Cr & Cb sample per 2x2 Y & A samples)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_RGB48BE:   ///< packed RGB 16:16:16, 48bpp, 16R, 16G, 16B, the 2-byte value for each R/G/B component is stored as big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB48LE:   ///< packed RGB 16:16:16, 48bpp, 16R, 16G, 16B, the 2-byte value for each R/G/B component is stored as little-endian
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_RGB565BE:  ///< packed RGB 5:6:5, 16bpp, (msb)   5R 6G 5B(lsb), big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB565LE:  ///< packed RGB 5:6:5, 16bpp, (msb)   5R 6G 5B(lsb), little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB555BE:  ///< packed RGB 5:5:5, 16bpp, (msb)1X 5R 5G 5B(lsb), big-endian   , X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB555LE:  ///< packed RGB 5:5:5, 16bpp, (msb)1X 5R 5G 5B(lsb), little-endian, X=unused/undefined
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_BGR565BE:  ///< packed BGR 5:6:5, 16bpp, (msb)   5B 6G 5R(lsb), big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR565LE:  ///< packed BGR 5:6:5, 16bpp, (msb)   5B 6G 5R(lsb), little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR555BE:  ///< packed BGR 5:5:5, 16bpp, (msb)1X 5B 5G 5R(lsb), big-endian   , X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR555LE:  ///< packed BGR 5:5:5, 16bpp, (msb)1X 5B 5G 5R(lsb), little-endian, X=unused/undefined
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_YUV420P16LE:  ///< planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P16BE:  ///< planar YUV 4:2:0, 24bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV422P16LE:  ///< planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV422P16BE:  ///< planar YUV 4:2:2, 32bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV444P16LE:  ///< planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P16BE:  ///< planar YUV 4:4:4, 48bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return ePixelCodingYUV444;
            break;

        case AV_PIX_FMT_RGB444LE:  ///< packed RGB 4:4:4, 16bpp, (msb)4X 4R 4G 4B(lsb), little-endian, X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB444BE:  ///< packed RGB 4:4:4, 16bpp, (msb)4X 4R 4G 4B(lsb), big-endian,    X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR444LE:  ///< packed BGR 4:4:4, 16bpp, (msb)4X 4B 4G 4R(lsb), little-endian, X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR444BE:  ///< packed BGR 4:4:4, 16bpp, (msb)4X 4B 4G 4R(lsb), big-endian,    X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_YA8:       ///< 8 bits gray, 8 bits alpha
            return ePixelCodingGray;
            break;

        case AV_PIX_FMT_BGR48BE:   ///< packed RGB 16:16:16, 48bpp, 16B, 16G, 16R, the 2-byte value for each R/G/B component is stored as big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR48LE:   ///< packed RGB 16:16:16, 48bpp, 16B, 16G, 16R, the 2-byte value for each R/G/B component is stored as little-endian
            return ePixelCodingRGB;
            break;

            /**
             * The following 12 formats have the disadvantage of needing 1 format for each bit depth.
             * Notice that each 9/10 bits sample is stored in 16 bits with extra padding.
             * If you want to support multiple bit depths, then using AV_PIX_FMT_YUV420P16* with the bpp stored separately is better.
             */
        case AV_PIX_FMT_YUV420P9BE: ///< planar YUV 4:2:0, 13.5bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P9LE: ///< planar YUV 4:2:0, 13.5bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P10BE:///< planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P10LE:///< planar YUV 4:2:0, 15bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV422P10BE:///< planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV422P10LE:///< planar YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV444P9BE: ///< planar YUV 4:4:4, 27bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P9LE: ///< planar YUV 4:4:4, 27bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P10BE:///< planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P10LE:///< planar YUV 4:4:4, 30bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV422P9BE: ///< planar YUV 4:2:2, 18bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV422P9LE: ///< planar YUV 4:2:2, 18bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_GBRP:      ///< planar GBR 4:4:4 24bpp
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP9BE:   ///< planar GBR 4:4:4 27bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP9LE:   ///< planar GBR 4:4:4 27bpp, little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP10BE:  ///< planar GBR 4:4:4 30bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP10LE:  ///< planar GBR 4:4:4 30bpp, little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP16BE:  ///< planar GBR 4:4:4 48bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP16LE:  ///< planar GBR 4:4:4 48bpp, little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_YUVA422P:  ///< planar YUV 4:2:2 24bpp, (1 Cr & Cb sample per 2x1 Y & A samples)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA444P:  ///< planar YUV 4:4:4 32bpp, (1 Cr & Cb sample per 1x1 Y & A samples)
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUVA420P9BE:  ///< planar YUV 4:2:0 22.5bpp, (1 Cr & Cb sample per 2x2 Y & A samples), big-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVA420P9LE:  ///< planar YUV 4:2:0 22.5bpp, (1 Cr & Cb sample per 2x2 Y & A samples), little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVA422P9BE:  ///< planar YUV 4:2:2 27bpp, (1 Cr & Cb sample per 2x1 Y & A samples), big-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA422P9LE:  ///< planar YUV 4:2:2 27bpp, (1 Cr & Cb sample per 2x1 Y & A samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA444P9BE:  ///< planar YUV 4:4:4 36bpp, (1 Cr & Cb sample per 1x1 Y & A samples), big-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUVA444P9LE:  ///< planar YUV 4:4:4 36bpp, (1 Cr & Cb sample per 1x1 Y & A samples), little-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUVA420P10BE: ///< planar YUV 4:2:0 25bpp, (1 Cr & Cb sample per 2x2 Y & A samples, big-endian)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVA420P10LE: ///< planar YUV 4:2:0 25bpp, (1 Cr & Cb sample per 2x2 Y & A samples, little-endian)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVA422P10BE: ///< planar YUV 4:2:2 30bpp, (1 Cr & Cb sample per 2x1 Y & A samples, big-endian)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA422P10LE: ///< planar YUV 4:2:2 30bpp, (1 Cr & Cb sample per 2x1 Y & A samples, little-endian)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA444P10BE: ///< planar YUV 4:4:4 40bpp, (1 Cr & Cb sample per 1x1 Y & A samples, big-endian)
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUVA444P10LE: ///< planar YUV 4:4:4 40bpp, (1 Cr & Cb sample per 1x1 Y & A samples, little-endian)
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUVA420P16BE: ///< planar YUV 4:2:0 40bpp, (1 Cr & Cb sample per 2x2 Y & A samples, big-endian)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVA420P16LE: ///< planar YUV 4:2:0 40bpp, (1 Cr & Cb sample per 2x2 Y & A samples, little-endian)
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUVA422P16BE: ///< planar YUV 4:2:2 48bpp, (1 Cr & Cb sample per 2x1 Y & A samples, big-endian)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA422P16LE: ///< planar YUV 4:2:2 48bpp, (1 Cr & Cb sample per 2x1 Y & A samples, little-endian)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUVA444P16BE: ///< planar YUV 4:4:4 64bpp, (1 Cr & Cb sample per 1x1 Y & A samples, big-endian)
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUVA444P16LE: ///< planar YUV 4:4:4 64bpp, (1 Cr & Cb sample per 1x1 Y & A samples, little-endian)
            return ePixelCodingYUV444;
            break;

        case AV_PIX_FMT_XYZ12LE:      ///< packed XYZ 4:4:4, 36 bpp, (msb) 12X, 12Y, 12Z (lsb), the 2-byte value for each X/Y/Z is stored as little-endian, the 4 lower bits are set to 0
            return ePixelCodingXYZ;
            break;
        case AV_PIX_FMT_XYZ12BE:      ///< packed XYZ 4:4:4, 36 bpp, (msb) 12X, 12Y, 12Z (lsb), the 2-byte value for each X/Y/Z is stored as big-endian, the 4 lower bits are set to 0
            return ePixelCodingXYZ;
            break;
        case AV_PIX_FMT_NV16:         ///< interleaved chroma YUV 4:2:2, 16bpp, (1 Cr & Cb sample per 2x1 Y samples)
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_NV20LE:       ///< interleaved chroma YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_NV20BE:       ///< interleaved chroma YUV 4:2:2, 20bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return ePixelCodingYUV422;
            break;

        case AV_PIX_FMT_RGBA64BE:     ///< packed RGBA 16:16:16:16, 64bpp, 16R, 16G, 16B, 16A, the 2-byte value for each R/G/B/A component is stored as big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGBA64LE:     ///< packed RGBA 16:16:16:16, 64bpp, 16R, 16G, 16B, 16A, the 2-byte value for each R/G/B/A component is stored as little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGRA64BE:     ///< packed RGBA 16:16:16:16, 64bpp, 16B, 16G, 16R, 16A, the 2-byte value for each R/G/B/A component is stored as big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGRA64LE:     ///< packed RGBA 16:16:16:16, 64bpp, 16B, 16G, 16R, 16A, the 2-byte value for each R/G/B/A component is stored as little-endian
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_YVYU422:   ///< packed YUV 4:2:2, 16bpp, Y0 Cr Y1 Cb
            return ePixelCodingYUV422;
            break;

        case AV_PIX_FMT_YA16BE:       ///< 16 bits gray, 16 bits alpha (big-endian)
            return ePixelCodingGray;
            break;
        case AV_PIX_FMT_YA16LE:       ///< 16 bits gray, 16 bits alpha (little-endian)
            return ePixelCodingGray;
            break;

        case AV_PIX_FMT_GBRAP:        ///< planar GBRA 4:4:4:4 32bpp
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRAP16BE:    ///< planar GBRA 4:4:4:4 64bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRAP16LE:    ///< planar GBRA 4:4:4:4 64bpp, little-endian
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_0RGB:///< packed RGB 8:8:8, 32bpp, XRGBXRGB...   X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_RGB0:        ///< packed RGB 8:8:8, 32bpp, RGBXRGBX...   X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_0BGR:        ///< packed BGR 8:8:8, 32bpp, XBGRXBGR...   X=unused/undefined
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_BGR0:        ///< packed BGR 8:8:8, 32bpp, BGRXBGRX...   X=unused/undefined
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_YUV420P12BE: ///< planar YUV 4:2:0,18bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P12LE: ///< planar YUV 4:2:0,18bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P14BE: ///< planar YUV 4:2:0,21bpp, (1 Cr & Cb sample per 2x2 Y samples), big-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV420P14LE: ///< planar YUV 4:2:0,21bpp, (1 Cr & Cb sample per 2x2 Y samples), little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_YUV422P12BE: ///< planar YUV 4:2:2,24bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV422P12LE: ///< planar YUV 4:2:2,24bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV422P14BE: ///< planar YUV 4:2:2,28bpp, (1 Cr & Cb sample per 2x1 Y samples), big-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV422P14LE: ///< planar YUV 4:2:2,28bpp, (1 Cr & Cb sample per 2x1 Y samples), little-endian
            return ePixelCodingYUV422;
            break;
        case AV_PIX_FMT_YUV444P12BE: ///< planar YUV 4:4:4,36bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P12LE: ///< planar YUV 4:4:4,36bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P14BE: ///< planar YUV 4:4:4,42bpp, (1 Cr & Cb sample per 1x1 Y samples), big-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_YUV444P14LE: ///< planar YUV 4:4:4,42bpp, (1 Cr & Cb sample per 1x1 Y samples), little-endian
            return ePixelCodingYUV444;
            break;
        case AV_PIX_FMT_GBRP12BE:    ///< planar GBR 4:4:4 36bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP12LE:    ///< planar GBR 4:4:4 36bpp, little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP14BE:    ///< planar GBR 4:4:4 42bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRP14LE:    ///< planar GBR 4:4:4 42bpp, little-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_YUVJ411P:    ///< planar YUV 4:1:1, 12bpp, (1 Cr & Cb sample per 4x1 Y samples) full scale (JPEG), deprecated in favor of AV_PIX_FMT_YUV411P and setting color_range
            return ePixelCodingYUV411;
            break;

        case AV_PIX_FMT_BAYER_BGGR8:    ///< bayer, BGBG..(odd line), GRGR..(even line), 8-bit samples */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_RGGB8:    ///< bayer, RGRG..(odd line), GBGB..(even line), 8-bit samples */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_GBRG8:    ///< bayer, GBGB..(odd line), RGRG..(even line), 8-bit samples */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_GRBG8:    ///< bayer, GRGR..(odd line), BGBG..(even line), 8-bit samples */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_BGGR16LE: ///< bayer, BGBG..(odd line), GRGR..(even line), 16-bit samples, little-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_BGGR16BE: ///< bayer, BGBG..(odd line), GRGR..(even line), 16-bit samples, big-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_RGGB16LE: ///< bayer, RGRG..(odd line), GBGB..(even line), 16-bit samples, little-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_RGGB16BE: ///< bayer, RGRG..(odd line), GBGB..(even line), 16-bit samples, big-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_GBRG16LE: ///< bayer, GBGB..(odd line), RGRG..(even line), 16-bit samples, little-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_GBRG16BE: ///< bayer, GBGB..(odd line), RGRG..(even line), 16-bit samples, big-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_GRBG16LE: ///< bayer, GRGR..(odd line), BGBG..(even line), 16-bit samples, little-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_BAYER_GRBG16BE: ///< bayer, GRGR..(odd line), BGBG..(even line), 16-bit samples, big-endian */
            return ePixelCodingBayer;
            break;
        case AV_PIX_FMT_YUV440P10LE: ///< planar YUV 4:4:0,20bpp, (1 Cr & Cb sample per 1x2 Y samples), little-endian
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_YUV440P10BE: ///< planar YUV 4:4:0,20bpp, (1 Cr & Cb sample per 1x2 Y samples), big-endian
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_YUV440P12LE: ///< planar YUV 4:4:0,24bpp, (1 Cr & Cb sample per 1x2 Y samples), little-endian
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_YUV440P12BE: ///< planar YUV 4:4:0,24bpp, (1 Cr & Cb sample per 1x2 Y samples), big-endian
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_AYUV64LE:    ///< packed AYUV 4:4:4,64bpp (1 Cr & Cb sample per 1x1 Y & A samples), little-endian
            return ePixelCodingYUV440;
            break;
        case AV_PIX_FMT_AYUV64BE:    ///< packed AYUV 4:4:4,64bpp (1 Cr & Cb sample per 1x1 Y & A samples), big-endian
            return ePixelCodingYUV440;
            break;

        case AV_PIX_FMT_P010LE: ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, little-endian
            return ePixelCodingYUV420;
            break;
        case AV_PIX_FMT_P010BE: ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, big-endian
            return ePixelCodingYUV420;
            break;

        case AV_PIX_FMT_GBRAP12BE:  ///< planar GBR 4:4:4:4 48bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRAP12LE:  ///< planar GBR 4:4:4:4 48bpp, little-endian
            return ePixelCodingRGB;
            break;

        case AV_PIX_FMT_GBRAP10BE:  ///< planar GBR 4:4:4:4 40bpp, big-endian
            return ePixelCodingRGB;
            break;
        case AV_PIX_FMT_GBRAP10LE:  ///< planar GBR 4:4:4:4 40bpp, little-endian
            return ePixelCodingRGB;
            break;

        default:
#if OFX_FFMPEG_PRINT_CODECS
            std::cout << "** Format " << av_get_pix_fmt_name(pixelFormat) << "not handled" << std::endl;
#endif
            
            return ePixelCodingNone;
    } // switch
    
} // pixelFormatCoding

int
pixelFormatBPPFromSpec(PixelCodingEnum coding, int bitdepth, bool alpha)
{
    int bits;
    switch (coding) {
        case ePixelCodingNone:
            bits = 0;
            break;
        case ePixelCodingGray:
        case ePixelCodingPalette:
        case ePixelCodingBayer:
            bits = bitdepth;
            break;
        case ePixelCodingYUV410:
            bits = (bitdepth * (16 + 2)) / 16;
            break;
        case ePixelCodingYUV411: // 1 Cr & Cb sample per 4x1 Y samples
        case ePixelCodingYUV420: // 1 Cr & Cb sample per 2x2 Y samples
            bits = (bitdepth * (4 + 2)) / 4;
            break;
        case ePixelCodingYUV422: // 1 Cr & Cb sample per 2x1 Y samples
        //case ePixelCodingYUV440: // 1 Cr & Cb sample per 1x2 Y samples
            bits = (bitdepth * (2 + 2)) / 2;
            break;
        case ePixelCodingYUV444: // 1 Cr & Cb sample per 1x1 Y samples
        case ePixelCodingRGB:    // RGB
        //case ePixelCodingXYZ = ePixelCodingRGB, // XYZ
            bits = bitdepth * 3;
    }
    return alpha ? (bits + bitdepth) : bits;
}



}
}
