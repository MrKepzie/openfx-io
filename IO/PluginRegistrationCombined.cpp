#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"
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
#if !defined(_WIN32) && !defined(__MINGW__)
#include "RunScript.h"
#endif
#include "SeExpr.h"
#include "SeNoisePlugin.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getReadEXRPluginID(ids);// EXR plugins are deprecated and should be revised/rewritten
            getWriteEXRPluginID(ids);// EXR plugins are deprecated and should be revised/rewritten
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
#if !defined(_WIN32) && !defined(__MINGW__)
            getRunScriptPluginID(ids);
#endif
			getSeExprPluginID(ids);
            getSeNoisePluginID(ids);
        }
    }
}
