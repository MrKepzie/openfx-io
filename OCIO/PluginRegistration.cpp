#include "ofxsImageEffect.h"
#include "OCIOCDLTransform.h"
#include "OCIOColorSpace.h"
#include "OCIOFileTransform.h"
#include "OCIOLogConvert.h"
#include "OCIOLookTransform.h"

namespace OFX 
{
    namespace Plugin 
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
#ifdef OFX_IO_USING_OCIO
            getOCIOCDLTransformPluginID(ids);
            getOCIOColorSpacePluginID(ids);
            getOCIOFileTransformPluginID(ids);
            getOCIOLogConvertPluginID(ids);
            getOCIOLookTransformPluginID(ids);
#endif
        }
    }
}
