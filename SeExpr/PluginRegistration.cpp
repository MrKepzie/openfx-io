#include "SeExpr.h"
#include "SeNoisePlugin.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getSeExprPluginID(ids);
            getSeNoisePluginID(ids);
        }
    }
}
