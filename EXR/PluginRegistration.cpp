#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getReadEXRPluginID(ids);
            getWriteEXRPluginID(ids);
        }
    }
}
