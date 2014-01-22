#include <stdio.h>
#include "ofxsImageEffect.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static ReadOIIOPluginFactory p1("fr.inria.openfx:ReadOIIO", 1, 0);
      ids.push_back(&p1);
      static WriteOIIOPluginFactory p2("fr.inria.openfx:WriteOIIO", 1, 0);
      ids.push_back(&p2);
    }
  }
}
