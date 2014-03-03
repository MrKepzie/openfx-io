#include "RunScript.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static RunScriptPluginFactory p("fr.inria.openfx:RunScript", 1, 0);
            ids.push_back(&p);
        }
    }
}
