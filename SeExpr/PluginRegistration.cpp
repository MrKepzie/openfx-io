#include "SeExpr.h"
#include "SeGrain.h"
#include "SeNoisePlugin.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getSeExprPluginID(ids);
            getSeGrainPluginID(ids);
            getSeNoisePluginID(ids);
        }
    }
}
