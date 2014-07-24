#include "RunScript.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
#ifdef OFX_IO_USING_OCIO
            getRunScriptPluginID(ids);
#endif
        }
    }
}
