#include "ofxsImageEffect.h"
#include "OIIOText.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getOIIOTextPluginID(ids);
        }
    }
}
