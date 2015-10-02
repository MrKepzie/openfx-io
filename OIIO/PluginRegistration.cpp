#include "ofxsImageEffect.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"
#include "OIIOText.h"
#include "OIIOResize.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
        getReadOIIOPluginID(ids);
        getWriteOIIOPluginID(ids);
        getOIIOTextPluginID(ids);
        getOIIOResizePluginID(ids);
    }
  }
}
