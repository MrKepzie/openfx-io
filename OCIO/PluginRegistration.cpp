#include "ofxsImageEffect.h"
#include "OCIOColorSpace.h"
#include "OCIOFileTransform.h"

namespace OFX 
{
    namespace Plugin 
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
          getOCIOColorSpacePluginId(ids);
          //getOCIOFileTransformPluginId(ids);
        }
    }
}
