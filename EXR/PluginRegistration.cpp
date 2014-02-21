#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static ReadEXRPluginFactory p1("fr.inria.openfx:ReadEXR", 1, 0);
      ids.push_back(&p1);
      static WriteEXRPluginFactory p2("fr.inria.openfx:WriteEXR", 1, 0);
      ids.push_back(&p2);
    }
  }
}
