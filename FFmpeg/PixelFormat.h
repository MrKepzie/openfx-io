/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2013-2018 INRIA
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

#ifndef PixelFormat_h
#define PixelFormat_h

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace OFX {
namespace FFmpeg {

// these are ordered
enum PixelCodingEnum
{
    ePixelCodingNone = 0,
    ePixelCodingGray,   // Grayscale
    ePixelCodingPalette, // Palette (color-capable)
    ePixelCodingBayer,  // Bayer mosaic
    ePixelCodingYUV410, // 1 Cr & Cb sample per 4x4 Y samples
    ePixelCodingYUV411, // 1 Cr & Cb sample per 4x1 Y samples
    ePixelCodingYUV420, // 1 Cr & Cb sample per 2x2 Y samples
    ePixelCodingYUV422, // 1 Cr & Cb sample per 2x1 Y samples
    ePixelCodingYUV440 = ePixelCodingYUV422, // 1 Cr & Cb sample per 1x2 Y samples
    ePixelCodingYUV444, // 1 Cr & Cb sample per 1x1 Y samples
    ePixelCodingRGB,    // RGB
    ePixelCodingXYZ, // XYZ
};

bool pixelFormatIsYUV(AVPixelFormat pixelFormat);
int pixelFormatBitDepth(AVPixelFormat pixelFormat);
int pixelFormatBPP(AVPixelFormat pixelFormat);
PixelCodingEnum pixelFormatCoding(AVPixelFormat pixelFormat);
bool pixelFormatAlpha(AVPixelFormat pixelFormat);

int pixelFormatBPPFromSpec(PixelCodingEnum coding, int bitdepth, bool alpha);

}
}

#endif /* defined(PixelFormat_h) */
