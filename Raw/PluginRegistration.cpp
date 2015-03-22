#include "ofxsImageEffect.h"
#include "ReadRaw.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getReadRawPluginID(ids);
        }
    }
}
