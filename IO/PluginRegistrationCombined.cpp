#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"
#include "ReadFFmpeg.h"
#include "WriteFFmpeg.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"
#include "OCIOCDLTransform.h"
#include "OCIOColorSpace.h"
#include "OCIOFileTransform.h"
#include "OCIOLogConvert.h"
#include "OIIOText.h"
#include "OIIOResize.h"
#include "ReadPFM.h"
#include "WritePFM.h"
#ifndef _WIN32
#include "RunScript.h"
#endif

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            //      static ReadEXRPluginFactory p1("fr.inria.openfx:ReadEXR", 1, 1);
            //      ids.push_back(&p1);
            //      static WriteEXRPluginFactory p2("fr.inria.openfx:WriteEXR", 1, 0);
            //      ids.push_back(&p2);
            static ReadFFmpegPluginFactory p3("fr.inria.openfx:ReadFFmpeg", 1, 1);
            ids.push_back(&p3);
            static WriteFFmpegPluginFactory p4("fr.inria.openfx:WriteFFmpeg", 1, 0);
            ids.push_back(&p4);
            getReadOIIOPluginID(ids);
            getWriteOIIOPluginID(ids);
#ifdef DEBUG
            getOIIOTextPluginID(ids); //< not ready
            getOIIOResizePluginID(ids);
#endif
            static ReadPFMPluginFactory p7("fr.inria.openfx:ReadPFM", 1, 1);
            ids.push_back(&p7);
            static WritePFMPluginFactory p8("fr.inria.openfx:WritePFM", 1, 0);
            ids.push_back(&p8);
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
