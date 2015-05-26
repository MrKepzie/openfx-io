#include "ofxsImageEffect.h"
#include "MagickText.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
        getMagickTextPluginID(ids);
    }
  }
}
