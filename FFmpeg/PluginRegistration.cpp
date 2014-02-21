#include "ofxsImageEffect.h"
#include "ReadFFmpeg.h"
#include "WriteFFmpeg.h"

namespace OFX 
{
  namespace Plugin 
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static ReadFFmpegPluginFactory p1("fr.inria.openfx:ReadFFmpeg", 1, 0);
      ids.push_back(&p1);
      static WriteFFmpegPluginFactory p2("fr.inria.openfx:WriteFFmpeg", 1, 0);
      ids.push_back(&p2);
    }
  }
}
