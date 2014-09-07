#include "ofxsImageEffect.h"
#include "ReadPFM.h"
#include "WritePFM.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getReadPFMPluginID(ids);
            getWritePFMPluginID(ids);
        }
    }
}
