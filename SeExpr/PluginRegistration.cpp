#include "SeExpr.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getSeExprPluginID(ids);
        }
    }
}
