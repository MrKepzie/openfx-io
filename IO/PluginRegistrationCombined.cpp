#include "ofxsImageEffect.h"
#ifdef OFX_IO_USE_DEPRECATED_EXR
#include "ReadEXR.h"
#include "WriteEXR.h"
#endif
#include "ReadFFmpeg.h"
#include "WriteFFmpeg.h"
#include "ReadPFM.h"
#include "WritePFM.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"
#include "OIIOText.h"
#include "OIIOResize.h"
#ifdef OFX_IO_USING_OCIO
#include "OCIOCDLTransform.h"
#include "OCIOColorSpace.h"
#include "OCIOFileTransform.h"
#include "OCIOLogConvert.h"
#endif
#ifndef _WINDOWS
#include "RunScript.h"
#endif

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
#ifdef OFX_IO_USE_DEPRECATED_EXR
            // EXR plugins are deprecated and should be revised/rewritten
            {
                static ReadEXRPluginFactory p("fr.inria.openfx:ReadEXR", 1, 1);
                ids.push_back(&p);
            }
            {
                static WriteEXRPluginFactory p("fr.inria.openfx:WriteEXR", 1, 0);
                ids.push_back(&p);
            }
#endif
            {
                static ReadFFmpegPluginFactory p("fr.inria.openfx:ReadFFmpeg", 1, 1);
                ids.push_back(&p);
            }
            {
                static WriteFFmpegPluginFactory p("fr.inria.openfx:WriteFFmpeg", 1, 0);
                ids.push_back(&p);
            }
            {
                static ReadPFMPluginFactory p("fr.inria.openfx:ReadPFM", 1, 1);
                ids.push_back(&p);
            }
            {
                static WritePFMPluginFactory p("fr.inria.openfx:WritePFM", 1, 0);
                ids.push_back(&p);
            }
            getReadOIIOPluginID(ids);
            getWriteOIIOPluginID(ids);
            getOIIOTextPluginID(ids);
            getOIIOResizePluginID(ids);
#ifdef OFX_IO_USING_OCIO
            getOCIOCDLTransformPluginID(ids);
            getOCIOColorSpacePluginID(ids);
            getOCIOFileTransformPluginID(ids);
            getOCIOLogConvertPluginID(ids);
#endif
#ifndef _WINDOWS
            getRunScriptPluginID(ids);
#endif
        }
    }
}
