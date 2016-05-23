/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2015 INRIA
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

/*
 * OFX oiio Writer plugin.
 * Writes an image using the OpenImageIO library.
 */

#include <cfloat>

#include "ofxsMacros.h"

#include "OIIOGlobal.h"
GCC_DIAG_OFF(unused-parameter)
#include <OpenImageIO/filesystem.h>
GCC_DIAG_ON(unused-parameter)

#include "GenericOCIO.h"
#include "GenericWriter.h"

#include <ofxsMultiPlane.h>

#ifdef _WIN32
#include <IlmThreadPool.h>
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "WriteOIIO"
#define kPluginGrouping "Image/Writers"
#define kPluginDescription "Write images using OpenImageIO."
#define kPluginIdentifier "fr.inria.openfx.WriteOIIO"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginEvaluation 91 // plugin quality from 0 (bad) to 100 (perfect) or -1 if not evaluated

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#define kSupportsXY true

#define kParamBitDepth    "bitDepth"
#define kParamBitDepthLabel   "Bit Depth"
#define kParamBitDepthHint \
"Number of bits per sample in the file [TIFF,DPX,TGA,DDS,ICO,IFF,PNM,PIC]."

#define kParamBitDepthOptionAuto     "auto"
#define kParamBitDepthOptionAutoHint "Guess from the output format"
//#define kParamBitDepthNone     "none"
#define kParamBitDepthOption8      "8i"
#define kParamBitDepthOption8Hint   "8  bits integer"
#define kParamBitDepthOption10     "10i"
#define kParamBitDepthOption10Hint  "10 bits integer"
#define kParamBitDepthOption12     "12i"
#define kParamBitDepthOption12Hint  "12 bits integer"
#define kParamBitDepthOption16     "16i"
#define kParamBitDepthOption16Hint  "16 bits integer"
#define kParamBitDepthOption16f    "16f"
#define kParamBitDepthOption16fHint "16 bits floating point"
#define kParamBitDepthOption32     "32i"
#define kParamBitDepthOption32Hint  "32 bits integer"
#define kParamBitDepthOption32f    "32f"
#define kParamBitDepthOption32fHint "32 bits floating point"
#define kParamBitDepthOption64     "64i"
#define kParamBitDepthOption64Hint  "64 bits integer"
#define kParamBitDepthOption64f    "64f"
#define kParamBitDepthOption64fHint "64 bits floating point"


enum ETuttlePluginBitDepth
{
	eTuttlePluginBitDepthAuto = 0,
	eTuttlePluginBitDepth8,
	eTuttlePluginBitDepth10,
	eTuttlePluginBitDepth12,
	eTuttlePluginBitDepth16,
	eTuttlePluginBitDepth16f,
	eTuttlePluginBitDepth32,
	eTuttlePluginBitDepth32f,
	eTuttlePluginBitDepth64,
	eTuttlePluginBitDepth64f
};

enum ETuttlePluginComponents
{
	eTuttlePluginComponentsAuto = 0,
	eTuttlePluginComponentsGray,
	eTuttlePluginComponentsRGB,
	eTuttlePluginComponentsRGBA
};

#define kParamOutputQuality        "quality"
#define kParamOutputQualityLabel   "Quality"
#define kParamOutputQualityHint \
"Indicates the quality of compression to use (0–100), for those plugins and compression methods that allow a variable amount of compression, with higher numbers indicating higher image fidelity. [JPEG, TIFF w/ JPEG comp., WEBP]"
#define kParamOutputQualityDefault 100

#define kParamOutputDWACompressionLevel        "dwaCompressionLevel"
#define kParamOutputDWACompressionLevelLabel   "DWA Compression Level"
#define kParamOutputDWACompressionLevelHint \
"Amount of compression when using Dreamworks DWAA or DWAB compression options. These lossy formats are variable in quality and can minimize the compression artifacts. Higher values will result in greater compression and likewise smaller file size, but increases the chance for artifacts. Values from 45 to 150 are usually correct for production shots, whereas HDR vacation photos could use up to 500. Values below 45 should give no visible imprrovement on photographs. [EXR w/ DWAa or DWAb comp.]"
#define kParamOutputDWACompressionLevelDefault 45

#define kParamOutputOrientation        "orientation"
#define kParamOutputOrientationLabel   "Orientation"
#define kParamOutputOrientationHint \
"The orientation of the image data [DPX,TIFF,JPEG,HDR,FITS].\n" \
"By default, image pixels are ordered from the top of the display to the bottom, " \
"and within each scanline, from left to right (i.e., the same ordering as English " \
"text and scan progression on a CRT). But the \"Orientation\" parameter can " \
"suggest that it should be displayed with a different orientation, according to " \
"the TIFF/EXIF conventions."
/*
 TIFF defines these values:

 1 = The 0th row represents the visual top of the image, and the 0th column represents the visual left-hand side.
 2 = The 0th row represents the visual top of the image, and the 0th column represents the visual right-hand side.
 3 = The 0th row represents the visual bottom of the image, and the 0th column represents the visual right-hand side.
 4 = The 0th row represents the visual bottom of the image, and the 0th column represents the visual left-hand side.
 5 = The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual top.
 6 = The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual top.
 7 = The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual bottom.
 8 = The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual bottom. 
 */
#define kParamOutputOrientationNormal                "normal"
#define kParamOutputOrientationNormalHint              "normal (top to bottom, left to right)"
#define kParamOutputOrientationFlop                  "flop"
#define kParamOutputOrientationFlopHint                "flipped horizontally (top to bottom, right to left)"
#define kParamOutputOrientationR180                  "180"
#define kParamOutputOrientationR180Hint                "rotate 180deg (bottom to top, right to left)"
#define kParamOutputOrientationFlip                  "flip"
#define kParamOutputOrientationFlipHint                "flipped vertically (bottom to top, left to right)"
#define kParamOutputOrientationTransposed            "transposed"
#define kParamOutputOrientationTransposedHint          "transposed (left to right, top to bottom)"
#define kParamOutputOrientationR90Clockwise          "90clockwise"
#define kParamOutputOrientationR90ClockwiseHint        "rotated 90deg clockwise (right to left, top to bottom)"
#define kParamOutputOrientationTransverse            "transverse"
#define kParamOutputOrientationTransverseHint          "transverse (right to left, bottom to top)"
#define kParamOutputOrientationR90CounterClockwise   "90counter-clockwise"
#define kParamOutputOrientationR90CounterClockwiseHint "rotated 90deg counter-clockwise (left to right, bottom to top)"
enum EOutputOrientation
{
    eOutputOrientationNormal = 0,
    eOutputOrientationFlop,
    eOutputOrientationR180,
    eOutputOrientationFlip,
    eOutputOrientationTransposed,
    eOutputOrientationR90Clockwise,
    eOutputOrientationTransverse,
    eOutputOrientationR90CounterClockwise,
};

#define kParamOutputCompression        "compression"
#define kParamOutputCompressionLabel   "Compression"
#define kParamOutputCompressionHint \
"Compression type [TIFF,EXR,DDS,IFF,SGI,TGA]\n" \
"Indicates the type of compression the file uses. Supported compression modes will vary from format to format. " \
"As an example, the TIFF format supports \"none\", \"lzw\", \"ccittrle\", \"zip\" (the default), \"jpeg\", \"packbits\", "\
"and the EXR format supports \"none\", \"rle\", \"zip\" (the default), \"piz\", \"pxr24\", \"b44\", \"b44a\", "\
"\"dwaa\" or \"dwab\"."

#define kParamOutputCompressionOptionAuto        "default"
#define kParamOutputCompressionOptionAutoHint     "Guess from the output format"
#define kParamOutputCompressionOptionNone        "none"
#define kParamOutputCompressionOptionNoneHint     "No compression [EXR, TIFF, IFF]"
#define kParamOutputCompressionOptionZip         "zip"
#define kParamOutputCompressionOptionZipHint      "Zlib/Deflate compression (lossless) [EXR, TIFF, Zfile]"
#define kParamOutputCompressionOptionZips        "zips"
#define kParamOutputCompressionOptionZipsHint     "Zlib compression (lossless), one scan line at a time [EXR]"
#define kParamOutputCompressionOptionRle         "rle"
#define kParamOutputCompressionOptionRleHint      "Run Length Encoding (lossless) [DPX, IFF, EXR, TGA, RLA]"
#define kParamOutputCompressionOptionPiz         "piz"
#define kParamOutputCompressionOptionPizHint      "Piz-based wavelet compression [EXR]"
#define kParamOutputCompressionOptionPxr24       "pxr24"
#define kParamOutputCompressionOptionPxr24Hint    "Lossy 24bit float compression [EXR]"
#define kParamOutputCompressionOptionB44         "b44"
#define kParamOutputCompressionOptionB44Hint      "Lossy 4-by-4 pixel block compression, fixed compression rate [EXR]"
#define kParamOutputCompressionOptionB44a        "b44a"
#define kParamOutputCompressionOptionB44aHint     "Lossy 4-by-4 pixel block compression, flat fields are compressed more [EXR]"
#define kParamOutputCompressionOptionDWAa        "dwaa"
#define kParamOutputCompressionOptionDWAaHint     "lossy DCT based compression, in blocks of 32 scanlines. More efficient for partial buffer access. [EXR]"
#define kParamOutputCompressionOptionDWAb        "dwab"
#define kParamOutputCompressionOptionDWAbHint     "lossy DCT based compression, in blocks of 256 scanlines. More efficient space wise and faster to decode full frames than DWAA. [EXR]"
#define kParamOutputCompressionOptionLZW         "lzw"
#define kParamOutputCompressionOptionLZWHint      "Lempel-Ziv Welsch compression (lossless) [TIFF]"
#define kParamOutputCompressionOptionCCITTRLE    "ccittrle"
#define kParamOutputCompressionOptionCCITTRLEHint "CCITT modified Huffman RLE (lossless) [TIFF]"
#define kParamOutputCompressionOptionJPEG        "jpeg"
#define kParamOutputCompressionOptionJPEGHint     "JPEG [TIFF]"
#define kParamOutputCompressionOptionPACKBITS    "packbits"
#define kParamOutputCompressionOptionPACKBITSHint "Macintosh RLE (lossless) [TIFF]"

enum EParamCompression
{
	eParamCompressionAuto = 0,
    eParamCompressionNone,
	eParamCompressionZip,
	eParamCompressionZips,
	eParamCompressionRle,
	eParamCompressionPiz,
	eParamCompressionPxr24,
	eParamCompressionB44,
    eParamCompressionB44a,
    eParamCompressionDWAa,
    eParamCompressionDWAb,
    eParamCompressionLZW,
    eParamCompressionCCITTRLE,
    eParamCompressionJPEG,
    eParamCompressionPACKBITS
};

#define kParamTileSize "tileSize"
#define kParamTileSizeLabel "Tile Size"
#define kParamTileSizeHint "Size of a tile in the output file for formats that support tiles. If Untiled, the whole image will have a single tile."

enum EParamTileSize
{
    eParamTileSizeUntiled = 0,
    eParamTileSize64,
    eParamTileSize128,
    eParamTileSize256,
    eParamTileSize512
};

#define kParamOutputChannels kNatronOfxParamOutputChannels
#define kParamOutputChannelsChoice kParamOutputChannels "Choice"
#define kParamOutputChannelsLabel "Layer(s)"
#define kParamOutputChannelsHint "Select which layer to write to the file. This is either All or a single layer. " \
"This is not yet possible to append a layer to an existing file."

#define kParamPartsSplitting "partSplitting"
#define kParamPartsSplittingLabel "Parts"
#define kParamPartsSplittingHint "Defines whether to separate views/layers in different EXR parts or not. " \
"Note that multi-part files are only supported by OpenEXR >= 2"

#define kParamPartsSinglePart "Single Part"
#define kParamPartsSinglePartHint "All views and layers will be in the same part, ensuring compatibility with OpenEXR 1.x"

#define kParamPartsSlitViews "Split Views"
#define kParamPartsSlitViewsHint "All views will have its own part, and each part will contain all layers. This will produce an EXR optimized in size that " \
"can be opened only with applications supporting OpenEXR 2"

#define kParamPartsSplitViewsLayers "Split Views,Layers"
#define kParamPartsSplitViewsLayersHint "Each layer of each view will have its own part. This will produce an EXR optimized for decoding speed that " \
"can be opened only with applications supporting OpenEXR 2"


#define kParamViewsSelector "viewsSelector"
#define kParamViewsSelectorLabel "Views"
#define kParamViewsSelectorHint "Select the views to render. When choosing All, make sure the output filename does not have a %v or %V view " \
"pattern in which case each view would be written to a separate file."

class WriteOIIOPlugin : public GenericWriterPlugin
{
public:
    WriteOIIOPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions);

    virtual ~WriteOIIOPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
    virtual void getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
    
    
private:
    
    virtual LayerViewsPartsEnum getPartsSplittingPreference() const OVERRIDE FINAL;
    
    virtual int getViewToRender() const OVERRIDE FINAL;
    
    virtual void onOutputFileChanged(const std::string& filename, bool setColorSpace) OVERRIDE FINAL;

    virtual void encode(const std::string& filename,
                        const OfxTime time,
                        const std::string& viewName,
                        const float *pixelData,
                        const OfxRectI& bounds,
                        const float pixelAspectRatio,
                        const int pixelDataNComps,
                        const int pixelDataNCompsStartIndex,
                        const int dstNComps,
                        const int rowBytes) OVERRIDE FINAL
    {
        std::string rawComps(kFnOfxImagePlaneColour);
        switch (dstNComps) {
            case 1:
                rawComps = kOfxImageComponentAlpha;
                break;
            case 3:
                rawComps = kOfxImageComponentRGB;
                break;
            case 4:
                rawComps = kOfxImageComponentRGBA;
                break;
            case 2:
                rawComps = kFnOfxImageComponentMotionVectors;
                break;
            default:
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
        }
        
        std::list<std::string> comps;
        comps.push_back(rawComps);
        EncodePlanesLocalData_RAII data(this);
        std::map<int,std::string> viewsToRender;
        viewsToRender[0] = viewName;
        
        std::vector<int> packingMapping(dstNComps);
        for (int i = 0; i < dstNComps; ++i) {
            packingMapping[i] = pixelDataNCompsStartIndex + i;
        }
        
        beginEncodeParts(data.getData(), filename, time, pixelAspectRatio, eLayerViewsSinglePart, viewsToRender, comps, false,packingMapping, bounds);
        encodePart(data.getData(), filename, pixelData, pixelDataNComps, 0, rowBytes);
        endEncodeParts(data.getData());
    }
    
    virtual void encodePart(void* user_data, const std::string& filename, const float *pixelData, int pixelDataNComps, int planeIndex, int rowBytes) OVERRIDE FINAL;
    
    virtual void beginEncodeParts(void* user_data,
                                  const std::string& filename,
                                  OfxTime time,
                                  float pixelAspectRatio,
                                  LayerViewsPartsEnum partsSplitting,
                                  const std::map<int,std::string>& viewsToRender,
                                  const std::list<std::string>& planes,
                                  const bool packingRequired,
                                  const std::vector<int>& packingMapping,
                                  const OfxRectI& bounds) OVERRIDE FINAL;
    
    void endEncodeParts(void* user_data) OVERRIDE FINAL;
    
    virtual void* allocateEncodePlanesUserData() OVERRIDE FINAL;
    virtual void destroyEncodePlanesUserData(void* data) OVERRIDE FINAL;

    virtual bool isImageFile(const std::string& fileExtension) const OVERRIDE FINAL;
    
    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const OVERRIDE FINAL { return OFX::eImagePreMultiplied; }

    virtual bool displayWindowSupportedByFormat(const std::string& filename) const OVERRIDE FINAL;
    
    void refreshParamsVisibility(const std::string& filename);
    
    void refreshCheckboxesLabels();
    
private:
    OFX::ChoiceParam* _bitDepth;
    OFX::IntParam* _quality;
    OFX::DoubleParam* _dwaCompressionLevel;
    OFX::ChoiceParam* _orientation;
    OFX::ChoiceParam* _compression;
    OFX::ChoiceParam* _tileSize;
    OFX::ChoiceParam* _outputLayers;
    OFX::ChoiceParam* _parts;
    OFX::ChoiceParam* _views;
    std::list<std::string> _currentInputComponents;
    std::list<std::string> _availableViews;
};

WriteOIIOPlugin::WriteOIIOPlugin(OfxImageEffectHandle handle, const std::vector<std::string>& extensions)
: GenericWriterPlugin(handle, extensions, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsXY)
, _bitDepth(0)
, _quality(0)
, _dwaCompressionLevel(0)
, _orientation(0)
, _compression(0)
, _tileSize(0)
, _outputLayers(0)
, _parts(0)
, _views(0)
, _currentInputComponents()
, _availableViews()
{
# if OFX_EXTENSIONS_NATRON && OFX_EXTENSIONS_NUKE
    const bool enableMultiPlaneFeature = (OFX::getImageEffectHostDescription()->supportsDynamicChoices &&
                                          OFX::getImageEffectHostDescription()->isMultiPlanar &&
                                          OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true));
# else
    const bool enableMultiPlaneFeature = false;
# endif
    _bitDepth = fetchChoiceParam(kParamBitDepth);
    _quality     = fetchIntParam(kParamOutputQuality);
    _dwaCompressionLevel = fetchDoubleParam(kParamOutputDWACompressionLevel);
    _orientation = fetchChoiceParam(kParamOutputOrientation);
    _compression = fetchChoiceParam(kParamOutputCompression);
    _tileSize = fetchChoiceParam(kParamTileSize);
    if (enableMultiPlaneFeature) {
        _outputLayers = fetchChoiceParam(kParamOutputChannels);
        fetchDynamicMultiplaneChoiceParameter(kParamOutputChannels, _outputClip);
        _parts = fetchChoiceParam(kParamPartsSplitting);
        _views = fetchChoiceParam(kParamViewsSelector);
    }
    
    std::string filename;
    _fileParam->getValue(filename);
    refreshParamsVisibility(filename);
    initOIIOThreads();
}


WriteOIIOPlugin::~WriteOIIOPlugin() {
    
}


namespace  {
    
    static bool hasListChanged(const std::list<std::string>& oldList, const std::list<std::string>& newList)
    {
        if (oldList.size() != newList.size()) {
            return true;
        }
        
        std::list<std::string>::const_iterator itNew = newList.begin();
        for (std::list<std::string>::const_iterator it = oldList.begin(); it!=oldList.end(); ++it,++itNew) {
            if (*it != *itNew) {
                return true;
            }
        }
        return false;
    }
    
}

void WriteOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    if (paramName == kParamOutputCompression && args.reason == OFX::eChangeUserEdit) {
        std::string filename;
        _fileParam->getValue(filename);
        refreshParamsVisibility(filename);
    } else if (paramName == kParamOutputChannels) {
        
    }
    if (handleChangedParamForAllDynamicChoices(paramName, args.reason)) {
        return;
    }
    GenericWriterPlugin::changedParam(args, paramName);
}

void
WriteOIIOPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    
    if (_outputLayers && !_outputLayers->getIsSecret()) {

        std::string filename;
        _fileParam->getValue(filename);
        std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
        bool supportsNChannels = false;
        if (output.get()) {
            supportsNChannels = output->supports("nchannels");
        }
        
        buildChannelMenus(std::string(), true /*mergeMenus*/, supportsNChannels);
        
        std::string ofxPlane,ofxComponents;
        getPlaneNeededInOutput(&ofxPlane, &ofxComponents);
            
        
        if (ofxComponents == kPlaneLabelAll) {
            _outputComponents->setIsSecret(true);
            for (int i = 0; i < 4; ++i) {
                _processChannels[i]->setIsSecret(true);
            }
        } else {
            _outputComponents->setIsSecret(false);
        }
        
    }
    
    if (_views) {
        //Now build the views choice
        std::list<std::string> views;
        int nViews = getViewCount();
        for (int i = 0; i < nViews; ++i) {
            std::string view = getViewName(i);
            views.push_back(view);
        }
        if (hasListChanged(_availableViews, views)) {
            _availableViews = views;
            if (_views) {
                _views->resetOptions();
                _views->appendOption("All");
                for (std::list<std::string>::iterator it = views.begin(); it!=views.end();++it) {
                    _views->appendOption(*it);
                }
            }
        }
    }
    GenericWriterPlugin::getClipPreferences(clipPreferences);

}




void
WriteOIIOPlugin::getClipComponents(const OFX::ClipComponentsArguments& /*args*/, OFX::ClipComponentsSetter& clipComponents)
{
    
    if (_outputLayers && !_outputLayers->getIsSecret()) {
        
        std::string ofxPlane,ofxComp;
        getPlaneNeededInOutput(&ofxPlane, &ofxComp);
        
        if (ofxPlane == kPlaneLabelAll) {
            
            const std::vector<std::string>& components = getCachedComponentsPresent(_outputClip);
            for (std::vector<std::string>::const_iterator it = components.begin(); it!=components.end(); ++it) {
                clipComponents.addClipComponents(*_inputClip, *it);
                clipComponents.addClipComponents(*_outputClip, *it);
            }
            
        } else {
            clipComponents.addClipComponents(*_inputClip, ofxComp);
            clipComponents.addClipComponents(*_outputClip, ofxComp);
        }
    } else {
        OFX::PixelComponentEnum inputComponents = _inputClip->getPixelComponents();
        clipComponents.addClipComponents(*_inputClip, inputComponents);
        OFX::PixelComponentEnum outputComponents = _outputClip->getPixelComponents();
        clipComponents.addClipComponents(*_outputClip, outputComponents);
    }
    
    
}

int
WriteOIIOPlugin::getViewToRender() const
{
    
    if (!_views || _views->getIsSecret()) {
        return -2;
    } else {
        int view_i;
        _views->getValue(view_i);
        return view_i - 1;
    }
}

LayerViewsPartsEnum
WriteOIIOPlugin::getPartsSplittingPreference() const
{
    if (!_parts || _parts->getIsSecret()) {
        return eLayerViewsSinglePart;
    }
    int index;
    _parts->getValue(index);
    std::string option;
    _parts->getOption(index, option);
    if (option == kParamPartsSinglePart) {
        return eLayerViewsSinglePart;
    } else if (option == kParamPartsSlitViews) {
        return eLayerViewsSplitViews;
    } else if (option == kParamPartsSplitViewsLayers) {
        return eLayerViewsSplitViewsLayers;
    }
    return eLayerViewsSinglePart;
}

/**
 * Deduce the best bitdepth when it hasn't been set by the user
 */
static
ETuttlePluginBitDepth getDefaultBitDepth(const std::string& filepath, ETuttlePluginBitDepth bitDepth)
{
	if (bitDepth != eTuttlePluginBitDepthAuto) {
        return bitDepth;
    }
    std::string format = Filesystem::extension (filepath);
    if (format.find("exr") != std::string::npos || format.find("hdr") != std::string::npos || format.find("rgbe") != std::string::npos) {
        return eTuttlePluginBitDepth32f;
    } else if(format.find("jpg") != std::string::npos || format.find("jpeg") != std::string::npos ||
              format.find("bmp") != std::string::npos || format.find("dds") != std::string::npos  ||
              format.find("ico") != std::string::npos || format.find("jfi") != std::string::npos  ||
              format.find("pgm") != std::string::npos || format.find("pnm") != std::string::npos  ||
              format.find("ppm") != std::string::npos || format.find("pbm") != std::string::npos  ||
              format.find("pic") != std::string::npos || format.find("tga") != std::string::npos  ||
              format.find("png") != std::string::npos) {
        return eTuttlePluginBitDepth8;
    } else {
        //cin, dpx, fits, j2k, j2c, jp2, jpe, sgi, tif, tiff, tpic, webp
        return eTuttlePluginBitDepth16;
    }

    return bitDepth;
}

bool
WriteOIIOPlugin::displayWindowSupportedByFormat(const std::string& filename) const
{
    std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
    if (output.get()) {
        return output->supports("displaywindow");
    } else {
        return false;
    }
}


static bool has_suffix(const std::string &str, const std::string &suffix)
{
    return (str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
}


void
WriteOIIOPlugin::onOutputFileChanged(const std::string &filename,
                                     bool setColorSpace)
{
    if (setColorSpace) {
#     ifdef OFX_IO_USING_OCIO
        int finalBitDepth_i;
        _bitDepth->getValue(finalBitDepth_i);
        ETuttlePluginBitDepth finalBitDepth = getDefaultBitDepth(filename, (ETuttlePluginBitDepth)finalBitDepth_i);

        // no colorspace... we'll probably have to try something else, then.
        // we set the following defaults:
        // sRGB for 8-bit images
        // Rec709 for 10-bits, 12-bits or 16-bits integer images
        // Linear for anything else
        switch (finalBitDepth) {
            case eTuttlePluginBitDepth8: {
                if (_ocio->hasColorspace("sRGB")) {
                    // nuke-default
                    _ocio->setOutputColorspace("sRGB");
                } else if (_ocio->hasColorspace("sRGB D65")) {
                    // blender-cycles
                    _ocio->setOutputColorspace("sRGB D65");
                } else if (_ocio->hasColorspace("rrt_srgb")) {
                    // rrt_srgb in aces
                    _ocio->setOutputColorspace("rrt_srgb");
                } else if (_ocio->hasColorspace("srgb8")) {
                    // srgb8 in spi-vfx
                    _ocio->setOutputColorspace("srgb8");
                }
                break;
            }
            case eTuttlePluginBitDepth10:
            case eTuttlePluginBitDepth12:
            case eTuttlePluginBitDepth16: {
                if (has_suffix(filename, ".cin") || has_suffix(filename, ".dpx") ||
                    has_suffix(filename, ".CIN") || has_suffix(filename, ".DPX")) {
                    // Cineon or DPX file
                    if (_ocio->hasColorspace("Cineon")) {
                        // Cineon in nuke-default
                        _ocio->setOutputColorspace("Cineon");
                    } else if (_ocio->hasColorspace("REDlogFilm")) {
                        // REDlogFilm in aces 1.0.0
                        _ocio->setOutputColorspace("REDlogFilm");
                    } else if (_ocio->hasColorspace("cineon")) {
                        // cineon in aces 0.7.1
                        _ocio->setOutputColorspace("cineon");
                    } else if (_ocio->hasColorspace("adx10")) {
                        // adx10 in aces 0.1.1
                        _ocio->setOutputColorspace("adx10");
                    } else if (_ocio->hasColorspace("lg10")) {
                        // lg10 in spi-vfx
                        _ocio->setOutputColorspace("lg10");
                    } else if (_ocio->hasColorspace("lm10")) {
                        // lm10 in spi-anim
                        _ocio->setOutputColorspace("lm10");
                    } else {
                        _ocio->setOutputColorspace(OCIO_NAMESPACE::ROLE_COMPOSITING_LOG);
                    }
                } else {
                    if (_ocio->hasColorspace("Rec709")) {
                        // nuke-default
                        _ocio->setOutputColorspace("Rec709");
                    } else if (_ocio->hasColorspace("nuke_rec709")) {
                        // blender
                        _ocio->setOutputColorspace("nuke_rec709");
                    } else if (_ocio->hasColorspace("Rec.709 - Full")) {
                        // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                        _ocio->setOutputColorspace("Rec.709 - Full");
                    } else if (_ocio->hasColorspace("out_rec709full")) {
                        // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                        _ocio->setOutputColorspace("out_rec709full");
                    } else if (_ocio->hasColorspace("rrt_rec709_full_100nits")) {
                        // rrt_rec709_full_100nits in aces 0.7.1
                        _ocio->setOutputColorspace("rrt_rec709_full_100nits");
                    } else if (_ocio->hasColorspace("rrt_rec709")) {
                        // rrt_rec709 in aces 0.1.1
                        _ocio->setOutputColorspace("rrt_rec709");
                    } else if (_ocio->hasColorspace("hd10")) {
                        // hd10 in spi-anim and spi-vfx
                        _ocio->setOutputColorspace("hd10");
                    }
                }
                break;
            }
            default:
                _ocio->setOutputColorspace(OCIO_NAMESPACE::ROLE_SCENE_LINEAR);
        }
#     endif
    }

    refreshParamsVisibility(filename);
}


void
WriteOIIOPlugin::refreshParamsVisibility(const std::string& filename)
{
    std::auto_ptr<ImageOutput> output(ImageOutput::create(filename));
    if (output.get()) {
        _tileSize->setIsSecret(!output->supports("tiles"));
        //_outputLayers->setIsSecret(!output->supports("nchannels"));
        bool hasQuality = (strcmp(output->format_name(), "jpeg") == 0 ||
                           strcmp(output->format_name(), "webp") == 0);
        if (!hasQuality && (strcmp(output->format_name(), "tiff") == 0)) {
            int compression_i;
            _compression->getValue(compression_i);
            hasQuality = ((EParamCompression)compression_i == eParamCompressionJPEG);
        }
        _quality->setIsSecret(!hasQuality);
        bool isEXR = strcmp(output->format_name(), "openexr") == 0;
        bool hasDWA = isEXR;
        if (hasDWA) {
            int compression_i;
            _compression->getValue(compression_i);
            EParamCompression compression = (EParamCompression)compression_i;
            hasDWA = (compression == eParamCompressionDWAa) || (compression == eParamCompressionDWAb);
        }
        _dwaCompressionLevel->setIsSecret(!hasDWA);
        if (_views) {
            _views->setIsSecret(!isEXR);
        }
        if (_parts) {
            _parts->setIsSecret(!output->supports("multiimage"));
        }
    } else {
        _tileSize->setIsSecret(true);
        //_outputLayers->setIsSecret(true);
        _quality->setIsSecret(true);
        _dwaCompressionLevel->setIsSecret(true);
        if (_views) {
            _views->setIsSecret(true);
        }
        if (_parts) {
            _parts->setIsSecret(true);
        }
    }

}

struct WriteOIIOEncodePlanesData
{
    std::auto_ptr<ImageOutput> output;
    std::vector<ImageSpec> specs;
};

void*
WriteOIIOPlugin::allocateEncodePlanesUserData()
{
    WriteOIIOEncodePlanesData* data = new WriteOIIOEncodePlanesData;
    return data;
}

void
WriteOIIOPlugin::destroyEncodePlanesUserData(void* data)
{
    assert(data);
    WriteOIIOEncodePlanesData* d = (WriteOIIOEncodePlanesData*)data;
    delete d;
}

void
WriteOIIOPlugin::beginEncodeParts(void* user_data,
                                  const std::string& filename,
                                  OfxTime time,
                                  float pixelAspectRatio,
                                  LayerViewsPartsEnum partsSplitting,
                                  const std::map<int,std::string>& viewsToRender,
                                  const std::list<std::string>& planes,
                                  const bool packingRequired,
                                  const std::vector<int>& packingMapping,
                                  const OfxRectI& bounds)
{
    assert((packingRequired && planes.size() == 1) || !packingRequired);
    
    assert(!viewsToRender.empty());
    assert(user_data);
    WriteOIIOEncodePlanesData* data = (WriteOIIOEncodePlanesData*)user_data;
    data->output.reset(ImageOutput::create(filename));
    if (!data->output.get()) {
        // output is NULL
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("Cannot create output file ")+filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    if (!data->output->supports("multiimage") && partsSplitting != eLayerViewsSinglePart) {
        std::stringstream ss;
        ss << data->output->format_name() << " does not support writing multiple views/layers into a single file.";
        setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    bool isEXR = strcmp(data->output->format_name(),"openexr") == 0;
    if (!isEXR && viewsToRender.size() > 1) {
        std::stringstream ss;
        ss << data->output->format_name() << " format cannot render multiple views in a single file, use %v or %V in filename to render separate files per view";
        setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
    
    OpenImageIO::TypeDesc oiioBitDepth;
    //size_t sizeOfChannel = 0;
    int    bitsPerSample  = 0;
    
    int finalBitDepth_i;
    _bitDepth->getValue(finalBitDepth_i);
    ETuttlePluginBitDepth finalBitDepth = getDefaultBitDepth(filename,(ETuttlePluginBitDepth)finalBitDepth_i);
    
    switch (finalBitDepth) {
        case eTuttlePluginBitDepthAuto:
            OFX::throwSuiteStatusException(kOfxStatErrUnknown);
            return;
        case eTuttlePluginBitDepth8:
            oiioBitDepth = TypeDesc::UINT8;
            bitsPerSample = 8;
            //sizeOfChannel = 1;
            break;
        case eTuttlePluginBitDepth10:
            oiioBitDepth = TypeDesc::UINT16;
            bitsPerSample = 10;
            //sizeOfChannel = 2;
            break;
        case eTuttlePluginBitDepth12:
            oiioBitDepth = TypeDesc::UINT16;
            bitsPerSample = 12;
            //sizeOfChannel = 2;
            break;
        case eTuttlePluginBitDepth16:
            oiioBitDepth = TypeDesc::UINT16;
            bitsPerSample = 16;
            //sizeOfChannel = 2;
            break;
        case eTuttlePluginBitDepth16f:
            oiioBitDepth = TypeDesc::HALF;
            bitsPerSample = 16;
            //sizeOfChannel = 2;
            break;
        case eTuttlePluginBitDepth32:
            oiioBitDepth = TypeDesc::UINT32;
            bitsPerSample = 32;
            //sizeOfChannel = 4;
            break;
        case eTuttlePluginBitDepth32f:
            oiioBitDepth = TypeDesc::FLOAT;
            bitsPerSample = 32;
            //sizeOfChannel = 4;
            break;
        case eTuttlePluginBitDepth64:
            oiioBitDepth = TypeDesc::UINT64;
            bitsPerSample = 64;
            //sizeOfChannel = 8;
            break;
        case eTuttlePluginBitDepth64f:
            oiioBitDepth = TypeDesc::DOUBLE;
            bitsPerSample = 64;
            //sizeOfChannel = 8;
            break;
    }
    
    //Base spec with a stub nChannels
    ImageSpec spec (bounds.x2 - bounds.x1, bounds.y2 - bounds.y1, 4, oiioBitDepth);

    
    int quality = 100;
    if (!_quality->getIsSecret()) {
        _quality->getValue(quality);
    }
    double dwaCompressionLevel = 45.;
    if (!_dwaCompressionLevel->getIsSecret()) {
        _dwaCompressionLevel->getValue(dwaCompressionLevel);
    }
    int orientation;
    _orientation->getValue(orientation);
    int compression_i;
    _compression->getValue(compression_i);
    std::string compression;
    
    switch ((EParamCompression)compression_i) {
        case eParamCompressionAuto:
            break;
        case eParamCompressionNone: // EXR, TIFF, IFF
            compression = "none";
            break;
        case eParamCompressionZip: // EXR, TIFF, Zfile
            compression = "zip";
            break;
        case eParamCompressionZips: // EXR
            compression = "zips";
            break;
        case eParamCompressionRle: // DPX, IFF, EXR, TGA, RLA
            compression = "rle";
            break;
        case eParamCompressionPiz: // EXR
            compression = "piz";
            break;
        case eParamCompressionPxr24: // EXR
            compression = "pxr24";
            break;
        case eParamCompressionB44: // EXR
            compression = "b44";
            break;
        case eParamCompressionB44a: // EXR
            compression = "b44a";
            break;
        case eParamCompressionDWAa: // EXR
            compression = "dwaa";
            break;
        case eParamCompressionDWAb: // EXR
            compression = "dwab";
            break;
        case eParamCompressionLZW: // TIFF
            compression = "lzw";
            break;
        case eParamCompressionCCITTRLE: // TIFF
            compression = "ccittrle";
            break;
        case eParamCompressionJPEG: // TIFF
            compression = "jpeg";
            break;
        case eParamCompressionPACKBITS: // TIFF
            compression = "packbits";
            break;
    }
    
    spec.attribute("oiio:BitsPerSample", bitsPerSample);
    // oiio:UnassociatedAlpha should be set if the data buffer in unassociated/unpremultiplied.
    // However, WriteOIIO::getExpectedInputPremultiplication() stated that input to the encode()
    // function should always be premultiplied/associated
    //spec.attribute("oiio:UnassociatedAlpha", premultiply);
#ifdef OFX_IO_USING_OCIO
    std::string ocioColorspace;
    _ocio->getOutputColorspaceAtTime(time, ocioColorspace);
    float gamma = 0.f;
    std::string colorSpaceStr;
    if (ocioColorspace == "Gamma1.8") {
        // Gamma1.8 in nuke-default
        colorSpaceStr = "GammaCorrected";
        gamma = 1.8f;
    } else if (ocioColorspace == "Gamma2.2" || ocioColorspace == "vd8" || ocioColorspace == "vd10" || ocioColorspace == "vd16" || ocioColorspace == "VD16") {
        // Gamma2.2 in nuke-default
        // vd8, vd10, vd16 in spi-anim and spi-vfx
        // VD16 in blender
        colorSpaceStr = "GammaCorrected";
        gamma = 2.2f;
    } else if (ocioColorspace == "sRGB" || ocioColorspace == "sRGB D65" || ocioColorspace == "sRGB (D60 sim.)" || ocioColorspace == "out_srgbd60sim" || ocioColorspace == "rrt_srgb" || ocioColorspace == "srgb8") {
        // sRGB in nuke-default and blender
        // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
        // rrt_srgb in aces
        // srgb8 in spi-vfx
        colorSpaceStr = "sRGB";
    } else if (ocioColorspace == "Rec709" || ocioColorspace == "nuke_rec709" || ocioColorspace == "Rec.709 - Full" || ocioColorspace == "out_rec709full" || ocioColorspace == "rrt_rec709" || ocioColorspace == "hd10") {
        // Rec709 in nuke-default
        // nuke_rec709 in blender
        // out_rec709full or "Rec.709 - Full" in aces 1.0.0
        // rrt_rec709 in aces
        // hd10 in spi-anim and spi-vfx
        colorSpaceStr = "Rec709";
    } else if (ocioColorspace == "KodakLog" || ocioColorspace == "Cineon" || ocioColorspace == "REDlogFilm" || ocioColorspace == "lg10") {
        // Cineon in nuke-default
        // REDlogFilm in aces 1.0.0
        // lg10 in spi-vfx and blender
        colorSpaceStr = "KodakLog";
    } else if (ocioColorspace == "Linear" || ocioColorspace == "linear" || ocioColorspace == "ACES2065-1" || ocioColorspace == "aces" || ocioColorspace == "lnf" || ocioColorspace == "ln16") {
        // linear in nuke-default
        // ACES2065-1 in aces 1.0.0
        // aces in aces
        // lnf, ln16 in spi-anim and spi-vfx
        colorSpaceStr = "Linear";
    } else if (ocioColorspace == "raw" || ocioColorspace == "Raw" || ocioColorspace == "ncf") {
        // raw in nuke-default
        // raw in aces
        // Raw in blender
        // ncf in spi-anim and spi-vfx
        // leave empty
    } else {
        //unknown color-space, don't do anything
    }
    if (!colorSpaceStr.empty()) {
        spec.attribute("oiio:ColorSpace", colorSpaceStr);
    }
    if (gamma != 0.) {
        spec.attribute("oiio:Gamma", gamma);
    }
#endif
    if (!_quality->getIsSecret()) {
        spec.attribute("CompressionQuality", quality);
    }
    if (!_dwaCompressionLevel->getIsSecret()) {
        spec.attribute("openexr:dwaCompressionLevel", (float)dwaCompressionLevel);
    }
    spec.attribute("Orientation", orientation + 1);
    if (!compression.empty()) { // some formats have a good value for the default compression
        spec.attribute("compression", compression);
    }
    if (pixelAspectRatio != 1.) {
        spec.attribute("PixelAspectRatio", pixelAspectRatio);
    }
    
    if (data->output->supports("tiles")) {
        spec.x = bounds.x1;
        spec.y = bounds.y1;
        spec.full_x = bounds.x1;
        spec.full_y = bounds.y1;
        
        bool clipToProject = true;
        if (_clipToProject && !_clipToProject->getIsSecret()) {
            _clipToProject->getValue(clipToProject);
        }
        if (!clipToProject) {
            //Spec has already been set to bounds which are the input RoD, so post-fix by setting display window to project size
            OfxPointD size = getProjectSize();
            OfxPointD offset = getProjectOffset();
            spec.full_x = offset.x;
            spec.full_y = offset.y;
            spec.full_width = size.x;
            spec.full_height = size.y;
        }
        
        int tileSize_i;
        _tileSize->getValue(tileSize_i);
        EParamTileSize tileSizeE = (EParamTileSize)tileSize_i;
        switch (tileSizeE) {
            case eParamTileSize64:
                spec.tile_width = std::min(64,spec.full_width);
                spec.tile_height = std::min(64,spec.full_height);
                break;
            case eParamTileSize128:
                spec.tile_width = std::min(128,spec.full_width);
                spec.tile_height = std::min(128,spec.full_height);
                break;
            case eParamTileSize256:
                spec.tile_width = std::min(256,spec.full_width);
                spec.tile_height = std::min(256,spec.full_height);
                break;
            case eParamTileSize512:
                spec.tile_width = std::min(512,spec.full_width);
                spec.tile_height = std::min(512,spec.full_height);
                break;
            case eParamTileSizeUntiled:
            default:
                break;
        }
    }
    
    
    
    assert(!planes.empty());
    switch (partsSplitting) {
        case eLayerViewsSinglePart: {
            data->specs.resize(1);
            
            ImageSpec partSpec = spec;
            TypeDesc tv(TypeDesc::STRING, viewsToRender.size());
            
            std::vector<ustring> ustrvec (viewsToRender.size());
            {
                int i = 0;
                for (std::map<int,std::string>::const_iterator it = viewsToRender.begin(); it!=viewsToRender.end();++it,++i) {
                    ustrvec[i] = it->second;
                }
            }

            partSpec.attribute("multiView", tv, &ustrvec[0]);
            std::vector<std::string>  channels;

            for (std::map<int,std::string>::const_iterator view = viewsToRender.begin(); view!=viewsToRender.end();++view) {
                for (std::list<std::string>::const_iterator it = planes.begin(); it!=planes.end(); ++it) {
                    std::string layer,pairedLayer;
                    std::vector<std::string> planeChannels;
                    
                    std::string rawComponents;
                    if (*it == kFnOfxImagePlaneColour) {
                        rawComponents = _inputClip->getPixelComponentsProperty();
                    } else {
                        rawComponents = *it;
                    }
                    OFX::MultiPlane::Utils::extractChannelsFromComponentString(rawComponents, &layer, &pairedLayer, &planeChannels);
                    
                    if (!layer.empty()) {
                        for (std::size_t i = 0; i < planeChannels.size(); ++i) {
                            planeChannels[i] = layer + "." + planeChannels[i];
                        }
                    }
                    if (viewsToRender.size() > 1 && view != viewsToRender.begin()) {
                        ///Prefix the view name for all views except the main
                        for (std::size_t i = 0; i < planeChannels.size(); ++i) {
                            planeChannels[i] = view->second + "." + planeChannels[i];
                        }
                    }
                    
                    if (!packingRequired) {
                        channels.insert(channels.end(), planeChannels.begin(), planeChannels.end());
                    } else {
                        assert(planeChannels.size() >= packingMapping.size());
                        for (std::size_t i = 0; i < packingMapping.size(); ++i) {
                            channels.push_back(planeChannels[packingMapping[i]]);
                        }
                    }
                    
                    
                    
                }
            } //  for (std::size_t v = 0; v < viewsToRender.size(); ++v) {
            if (channels.size() == 4) {
                partSpec.alpha_channel = 3;
            } else {
                if (channels.size() == 1) {
                    ///Alpha component only
                    partSpec.alpha_channel = 0;
                } else {
                    ///no alpha
                    partSpec.alpha_channel = -1;
                }
            }
            
            partSpec.nchannels = channels.size();
            partSpec.channelnames = channels;
            data->specs[0] = partSpec;
            
        }   break;
        case eLayerViewsSplitViews: {
            data->specs.resize(viewsToRender.size());
            
            int specIndex = 0;
            for (std::map<int,std::string>::const_iterator view = viewsToRender.begin(); view!=viewsToRender.end();++view) {
                
                ImageSpec partSpec = spec;
                partSpec.attribute("view",view->second);
                std::vector<std::string>  channels;
                
                for (std::list<std::string>::const_iterator it = planes.begin(); it!=planes.end(); ++it) {
                    std::string layer,pairedLayer;
                    std::vector<std::string> planeChannels;
                    
                    std::string rawComponents;
                    if (*it == kFnOfxImagePlaneColour) {
                        rawComponents = _inputClip->getPixelComponentsProperty();
                    } else {
                        rawComponents = *it;
                    }
                    OFX::MultiPlane::Utils::extractChannelsFromComponentString(rawComponents, &layer, &pairedLayer, &planeChannels);
                    
                    if (!layer.empty()) {
                        for (std::size_t i = 0; i < planeChannels.size(); ++i) {
                            planeChannels[i] = layer + "." + planeChannels[i];
                        }
                    }
                 
                    if (!packingRequired) {
                        channels.insert(channels.end(), planeChannels.begin(), planeChannels.end());
                    } else {
                        assert(planeChannels.size() >= packingMapping.size());
                        for (std::size_t i = 0; i < packingMapping.size(); ++i) {
                            channels.push_back(planeChannels[packingMapping[i]]);
                        }
                    }
                    
                }
                if (channels.size() == 4) {
                    partSpec.alpha_channel = 3;
                } else {
                    if (channels.size() == 1) {
                        ///Alpha component only
                        partSpec.alpha_channel = 0;
                    } else {
                        ///no alpha
                        partSpec.alpha_channel = -1;
                    }
                }
                
                partSpec.nchannels = channels.size();
                partSpec.channelnames = channels;
                
                data->specs[specIndex] = partSpec;
                ++specIndex;

            } //  for (std::size_t v = 0; v < viewsToRender.size(); ++v) {
        }   break;
        case eLayerViewsSplitViewsLayers: {
            data->specs.resize(viewsToRender.size() * planes.size());
            
            int specIndex = 0;
            for (std::map<int,std::string>::const_iterator view = viewsToRender.begin(); view!=viewsToRender.end();++view) {
                
                for (std::list<std::string>::const_iterator it = planes.begin(); it!=planes.end(); ++it) {
                    std::string layer,pairedLayer;
                    std::vector<std::string> planeChannels;
                    
                    std::string rawComponents;
                    if (*it == kFnOfxImagePlaneColour) {
                        rawComponents = _inputClip->getPixelComponentsProperty();
                    } else {
                        rawComponents = *it;
                    }
                    OFX::MultiPlane::Utils::extractChannelsFromComponentString(rawComponents, &layer, &pairedLayer, &planeChannels);
                    ImageSpec partSpec = spec;
                    
                    if (!layer.empty()) {
                        for (std::size_t i = 0; i < planeChannels.size(); ++i) {
                            planeChannels[i] = layer + "." + planeChannels[i];
                        }
                    }
                    
                    std::vector<std::string> channels;
                    if (!packingRequired) {
                        channels.insert(channels.end(), planeChannels.begin(), planeChannels.end());
                    } else {
                        assert(planeChannels.size() >= packingMapping.size());
                        for (std::size_t i = 0; i < packingMapping.size(); ++i) {
                            channels.push_back(planeChannels[packingMapping[i]]);
                        }
                    }
                    
                    if (channels.size() == 4) {
                        partSpec.alpha_channel = 3;
                    } else {
                        if (layer.empty() && channels.size() == 1) {
                            ///Alpha component only
                            partSpec.alpha_channel = 0;
                        } else {
                            ///no alpha
                            partSpec.alpha_channel = -1;
                        }
                    }
                    
                    partSpec.nchannels = channels.size();
                    partSpec.channelnames = channels;
                    partSpec.attribute("view",view->second);
                    data->specs[specIndex] = partSpec;
                    
                    ++specIndex;
                }
            } //  for (std::size_t v = 0; v < viewsToRender.size(); ++v) {
            

        }   break;
    }

    
    if (!data->output->open(filename, data->specs.size(), &data->specs.front())) {
        setPersistentMessage(OFX::Message::eMessageError, "", data->output->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    
}

void WriteOIIOPlugin::encodePart(void* user_data, const std::string& filename, const float *pixelData, int pixelDataNComps, int planeIndex, int rowBytes)
{
   
    assert(user_data);
    WriteOIIOEncodePlanesData* data = (WriteOIIOEncodePlanesData*)user_data;
    if (planeIndex != 0) {
        if (!data->output->open(filename, data->specs[planeIndex], ImageOutput::AppendSubimage)) {
            setPersistentMessage(OFX::Message::eMessageError, "", data->output->geterror());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    }
  
    TypeDesc format = TypeDesc::FLOAT;
    
    //do not use auto-stride as the buffer may have more components that what we want to write
    std::size_t xStride = format.size() * pixelDataNComps;
    data->output->write_image(format,
                        (char*)pixelData + (data->specs[planeIndex].height - 1) * rowBytes, //invert y
                        xStride, //xstride
                        -rowBytes, //ystride
                        AutoStride //zstride
                        );
    
    
}

void
WriteOIIOPlugin::endEncodeParts(void* user_data)
{
    assert(user_data);
    WriteOIIOEncodePlanesData* data = (WriteOIIOEncodePlanesData*)user_data;
    data->output->close();
}

bool WriteOIIOPlugin::isImageFile(const std::string& /*fileExtension*/) const {
    return true;
}


static std::string oiio_versions()
{
    std::ostringstream oss;
    int ver = openimageio_version();
    oss << "OIIO versions:" << std::endl;
    oss << "compiled with " << OIIO_VERSION_STRING << std::endl;
    oss << "running with " << ver/10000 << '.' << (ver%10000)/100 << '.' << (ver%100) << std::endl;
    return oss.str();
}

mDeclareWriterPluginFactory(WriteOIIOPluginFactory, ;, false);

void WriteOIIOPluginFactory::unload()
{
#ifdef _WIN32
    //Kill all threads otherwise when the static global thread pool joins it threads there is a deadlock on Mingw
    IlmThread::ThreadPool::globalThreadPool().setNumThreads(0);
#endif
}

void WriteOIIOPluginFactory::load()
{
    _extensions.clear();
#if 0
    // hard-coded extensions list
    const char* extensionsl[] = { "bmp", "cin", /*"dds",*/ "dpx", /*"f3d",*/ "fits", "hdr", "ico",
        "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png",
        "pbm", "pgm", "ppm",
#     if OIIO_VERSION >= 10605
        "pfm", // PFM was flipped before 1.6.5
#     endif
        "psd", "pdd", "psb", /*"ptex",*/ "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
    for (const char** ext = extensionsl; *ext != NULL; ++ext) {
        _extensions.push_back(*ext);
    }
#else
    // get extensions from OIIO (but there is no distinctions between readers and writers)
    std::string extensions_list;
    getattribute("extension_list", extensions_list);
    std::stringstream formatss(extensions_list);
    std::string format;
    std::list<std::string> extensionsl;
    while (std::getline(formatss, format, ';')) {
        std::stringstream extensionss(format);
        std::string extension;
        std::getline(extensionss, extension, ':'); // extract the format
        while (std::getline(extensionss, extension, ',')) {
            extensionsl.push_back(extension);
        }
    }
    const char* extensions_blacklist[] = {
#     if OIIO_VERSION < 10605
        "pfm", // PFM was flipped before 1.6.5
#     endif
        "avi", "mov", "qt", "mp4", "m4a", "3gp", "3g2", "mj2", "m4v", "mpg", // FFmpeg extensions - better supported by WriteFFmpeg
        NULL
    };
    for (const char*const* e = extensions_blacklist; *e != NULL; ++e) {
        extensionsl.remove(*e);
    }
    _extensions.assign(extensionsl.begin(), extensionsl.end());
#endif
}

/** @brief The basic describe function, passed a plugin descriptor */
void WriteOIIOPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericWriterDescribe(desc,OFX::eRenderFullySafe, _extensions, kPluginEvaluation, true, true);
    

    std::string extensions_list;
    getattribute("extension_list", extensions_list);

    std::string extensions_pretty;
    {
        std::stringstream formatss(extensions_list);
        std::string format;
        std::vector<std::string> extensions;
        while (std::getline(formatss, format, ';')) {
            std::stringstream extensionss(format);
            std::string extension;
            std::getline(extensionss, extension, ':'); // extract the format
            extensions_pretty += extension;
            extensions_pretty += ": ";
            bool first = true;
            while (std::getline(extensionss, extension, ',')) {
                if (!first) {
                    extensions_pretty += ", ";
                }
                first = false;
                extensions_pretty += extension;
            }
            extensions_pretty += "; ";
        }
    }


    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription("Write images using OpenImageIO.\n\n"
                              "OpenImageIO supports writing the following file formats:\n"
                              "BMP (*.bmp)\n"
                              "Cineon (*.cin)\n"
                              //"Direct Draw Surface (*.dds)\n"
                              "DPX (*.dpx)\n"
                              //"Field3D (*.f3d)\n"
                              "FITS (*.fits)\n"
                              "HDR/RGBE (*.hdr)\n"
                              "Icon (*.ico)\n"
                              "IFF (*.iff)\n"
                              "JPEG (*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi)\n"
                              "JPEG-2000 (*.jp2 *.j2k)\n"
                              "OpenEXR (*.exr)\n"
                              "Portable Network Graphics (*.png)\n"
                              "PNM / Netpbm (*.pbm *.pgm *.ppm)\n"
                              "PSD (*.psd *.pdd *.psb)\n"
                              //"Ptex (*.ptex)\n"
                              "RLA (*.rla)\n"
                              "SGI (*.sgi *.rgb *.rgba *.bw *.int *.inta)\n"
                              "Softimage PIC (*.pic)\n"
                              "Targa (*.tga *.tpic)\n"
                              "TIFF (*.tif *.tiff *.tx *.env *.sm *.vsm)\n"
                              "Zfile (*.zfile)\n\n"
                              "All supported formats and extensions: " + extensions_pretty + "\n\n"
                              + oiio_versions());
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void WriteOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericWriterDescribeInContextBegin(desc, context,
                                                                    kSupportsRGBA, kSupportsRGB, kSupportsAlpha,kSupportsXY,
                                                                    "reference", "reference", true);
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamTileSize);
        param->setLabel(kParamTileSizeLabel);
        param->setHint(kParamTileSizeHint);
        assert(param->getNOptions() == eParamTileSizeUntiled);
        param->appendOption("Untiled");
        assert(param->getNOptions() == eParamTileSize64);
        param->appendOption("64");
        assert(param->getNOptions() == eParamTileSize128);
        param->appendOption("128");
        assert(param->getNOptions() == eParamTileSize256);
        param->appendOption("256");
        assert(param->getNOptions() == eParamTileSize512);
        param->appendOption("512");
        param->setDefault(eParamTileSize256);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBitDepth);
        param->setLabel(kParamBitDepthLabel);
        param->setHint(kParamBitDepthHint);
        assert(param->getNOptions() == eTuttlePluginBitDepthAuto);
        param->appendOption(kParamBitDepthOptionAuto, kParamBitDepthOptionAutoHint);
        assert(param->getNOptions() == eTuttlePluginBitDepth8);
        param->appendOption(kParamBitDepthOption8, kParamBitDepthOption8Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth10);
        param->appendOption(kParamBitDepthOption10, kParamBitDepthOption10Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth12);
        param->appendOption(kParamBitDepthOption12, kParamBitDepthOption12Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth16);
        param->appendOption(kParamBitDepthOption16, kParamBitDepthOption16Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth16f);
        param->appendOption(kParamBitDepthOption16f, kParamBitDepthOption16fHint);
        assert(param->getNOptions() == eTuttlePluginBitDepth32);
        param->appendOption(kParamBitDepthOption32, kParamBitDepthOption32Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth32f);
        param->appendOption(kParamBitDepthOption32f, kParamBitDepthOption32fHint);
        assert(param->getNOptions() == eTuttlePluginBitDepth64);
        param->appendOption(kParamBitDepthOption64, kParamBitDepthOption64Hint);
        assert(param->getNOptions() == eTuttlePluginBitDepth64f);
        param->appendOption(kParamBitDepthOption64f, kParamBitDepthOption64fHint);
        param->setDefault(eTuttlePluginBitDepthAuto);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamOutputQuality);
        param->setLabel(kParamOutputQualityLabel);
        param->setHint(kParamOutputQualityHint);
        param->setRange(0, 100);
        param->setDisplayRange(0, 100);
        param->setDefault(kParamOutputQualityDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamOutputDWACompressionLevel);
        param->setLabel(kParamOutputDWACompressionLevelLabel);
        param->setHint(kParamOutputDWACompressionLevelHint);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(45, 200);
        param->setDefault(kParamOutputDWACompressionLevelDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputOrientation);
        param->setLabel(kParamOutputOrientationLabel);
        param->setHint(kParamOutputOrientationHint);
        assert(param->getNOptions() == eOutputOrientationNormal);
        param->appendOption(kParamOutputOrientationNormal, kParamOutputOrientationNormalHint);
        assert(param->getNOptions() == eOutputOrientationFlop);
        param->appendOption(kParamOutputOrientationFlop, kParamOutputOrientationFlopHint);
        assert(param->getNOptions() == eOutputOrientationR180);
        param->appendOption(kParamOutputOrientationR180, kParamOutputOrientationR180Hint);
        assert(param->getNOptions() == eOutputOrientationFlip);
        param->appendOption(kParamOutputOrientationFlip, kParamOutputOrientationFlipHint);
        assert(param->getNOptions() == eOutputOrientationTransposed);
        param->appendOption(kParamOutputOrientationTransposed, kParamOutputOrientationTransposedHint);
        assert(param->getNOptions() == eOutputOrientationR90Clockwise);
        param->appendOption(kParamOutputOrientationR90Clockwise, kParamOutputOrientationR90ClockwiseHint);
        assert(param->getNOptions() == eOutputOrientationTransverse);
        param->appendOption(kParamOutputOrientationTransverse, kParamOutputOrientationTransverseHint);
        assert(param->getNOptions() == eOutputOrientationR90CounterClockwise);
        param->appendOption(kParamOutputOrientationR90CounterClockwise, kParamOutputOrientationR90CounterClockwiseHint);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputCompression);
        param->setLabel(kParamOutputCompressionLabel);
        param->setHint(kParamOutputCompressionHint);
        assert(param->getNOptions() == eParamCompressionAuto);
        param->appendOption(kParamOutputCompressionOptionAuto, kParamOutputCompressionOptionAutoHint);
        assert(param->getNOptions() == eParamCompressionNone);
        param->appendOption(kParamOutputCompressionOptionNone, kParamOutputCompressionOptionNoneHint);
        assert(param->getNOptions() == eParamCompressionZip);
        param->appendOption(kParamOutputCompressionOptionZip, kParamOutputCompressionOptionZipHint);
        assert(param->getNOptions() == eParamCompressionZips);
        param->appendOption(kParamOutputCompressionOptionZips, kParamOutputCompressionOptionZipsHint);
        assert(param->getNOptions() == eParamCompressionRle);
        param->appendOption(kParamOutputCompressionOptionRle, kParamOutputCompressionOptionRleHint);
        assert(param->getNOptions() == eParamCompressionPiz);
        param->appendOption(kParamOutputCompressionOptionPiz, kParamOutputCompressionOptionPizHint);
        assert(param->getNOptions() == eParamCompressionPxr24);
        param->appendOption(kParamOutputCompressionOptionPxr24, kParamOutputCompressionOptionPxr24Hint);
        assert(param->getNOptions() == eParamCompressionB44);
        param->appendOption(kParamOutputCompressionOptionB44, kParamOutputCompressionOptionB44Hint);
        assert(param->getNOptions() == eParamCompressionB44a);
        param->appendOption(kParamOutputCompressionOptionB44a, kParamOutputCompressionOptionB44aHint);
        assert(param->getNOptions() == eParamCompressionDWAa);
        param->appendOption(kParamOutputCompressionOptionDWAa, kParamOutputCompressionOptionDWAaHint);
        assert(param->getNOptions() == eParamCompressionDWAb);
        param->appendOption(kParamOutputCompressionOptionDWAb, kParamOutputCompressionOptionDWAbHint);
        assert(param->getNOptions() == eParamCompressionLZW);
        param->appendOption(kParamOutputCompressionOptionLZW, kParamOutputCompressionOptionLZWHint);
        assert(param->getNOptions() == eParamCompressionCCITTRLE);
        param->appendOption(kParamOutputCompressionOptionCCITTRLE, kParamOutputCompressionOptionCCITTRLEHint);
        assert(param->getNOptions() == eParamCompressionJPEG);
        param->appendOption(kParamOutputCompressionOptionJPEG, kParamOutputCompressionOptionJPEGHint);
        assert(param->getNOptions() == eParamCompressionPACKBITS);
        param->appendOption(kParamOutputCompressionOptionPACKBITS, kParamOutputCompressionOptionPACKBITSHint);
        param->setDefault(eParamCompressionAuto);
        if (page) {
            page->addChild(*param);
        }
    }

# if OFX_EXTENSIONS_NATRON && OFX_EXTENSIONS_NUKE
    const bool enableMultiPlaneFeature = (OFX::getImageEffectHostDescription()->supportsDynamicChoices &&
                                          OFX::getImageEffectHostDescription()->isMultiPlanar &&
                                          OFX::fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true));
# else
    const bool enableMultiPlaneFeature = false;
# endif

    if (enableMultiPlaneFeature) {
        
        {
            OFX::ChoiceParamDescriptor* param = OFX::MultiPlane::Factory::describeInContextAddOutputLayerChoice(true, desc, page);
            param->setLabel(kParamOutputChannelsLabel);
            param->setHint(kParamOutputChannelsHint);
        }
        {
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamPartsSplitting);
            param->setLabel(kParamPartsSplittingLabel);
            param->setHint(kParamPartsSplittingHint);
            param->appendOption(kParamPartsSinglePart,kParamPartsSinglePartHint);
            param->appendOption(kParamPartsSlitViews,kParamPartsSlitViewsHint);
            param->appendOption(kParamPartsSplitViewsLayers,kParamPartsSplitViewsLayersHint);
            param->setDefault(2);
            param->setAnimates(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamViewsSelector);
            param->setLabel(kParamViewsSelectorLabel);
            param->setHint(kParamViewsSelectorHint);
            param->appendOption("All");
            param->setAnimates(false);
            param->setDefault(0);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    GenericWriterDescribeInContextEnd(desc, context, page);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* WriteOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new WriteOIIOPlugin(handle, _extensions);
}


static WriteOIIOPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
