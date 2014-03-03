#include "ofxsImageEffect.h"
#include "ReadPFM.h"
#include "WritePFM.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static ReadPFMPluginFactory p1("fr.inria.openfx:ReadPFM", 1, 0);
      ids.push_back(&p1);
      static WritePFMPluginFactory p2("fr.inria.openfx:WritePFM", 1, 0);
      ids.push_back(&p2);
    }
  }
}
