#include <stdio.h>
#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static ExrReaderPluginFactory p1("fr.inria.openfx:ReadEXR", 1, 0);
      ids.push_back(&p1);
      static ExrWriterPluginFactory p2("fr.inria.openfx:WriteEXR", 1, 0);
      ids.push_back(&p2);
    }
  }
}
