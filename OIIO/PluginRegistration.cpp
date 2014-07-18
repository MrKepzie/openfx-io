#include "ofxsImageEffect.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
        getReadOIIOPluginID(OFX::PluginFactoryArray &ids);
        static WriteOIIOPluginFactory p2("fr.inria.openfx:WriteOIIO", 1, 0);
      ids.push_back(&p2);
    }
  }
}
