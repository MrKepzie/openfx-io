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
#include "OCIODisplay.h"
#include "OCIOFileTransform.h"
#include "OCIOLogConvert.h"
#include "OCIOLookTransform.h"
#endif
//#include "ReadRaw.h"
#ifndef _WINDOWS
#include "RunScript.h"
#endif
#include "SeExpr.h"
#include "MagickText.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
#ifdef OFX_IO_USE_DEPRECATED_EXR
            // EXR plugins are deprecated and should be revised/rewritten
            getReadEXRPluginID(ids);
            getWriteEXRPluginID(ids);
#endif
            getReadFFmpegPluginID(ids);
            getWriteFFmpegPluginID(ids);
            getReadPFMPluginID(ids);
            getWritePFMPluginID(ids);
            getReadOIIOPluginID(ids);
            getWriteOIIOPluginID(ids);
            getOIIOTextPluginID(ids);
            getOIIOResizePluginID(ids);
#ifdef OFX_IO_USING_OCIO
            getOCIOCDLTransformPluginID(ids);
            getOCIOColorSpacePluginID(ids);
            getOCIODisplayPluginID(ids);
            getOCIOFileTransformPluginID(ids);
            getOCIOLogConvertPluginID(ids);
            getOCIOLookTransformPluginID(ids);
#endif
#ifndef _WINDOWS
            getRunScriptPluginID(ids);
#endif
			getSeExprPluginID(ids);
#ifdef DEBUG
            //getReadRawPluginID(ids);
#endif
            getMagickTextPluginID(ids);
        }
    }
}
