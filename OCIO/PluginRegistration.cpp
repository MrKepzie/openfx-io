#include <stdio.h>
#include "ofxsImageEffect.h"
#include "OCIOColorSpace.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static OCIOColorSpacePluginFactory p1("fr.inria.openfx:OCIOColorSpace", 1, 0);
      ids.push_back(&p1);
    }
  }
}
