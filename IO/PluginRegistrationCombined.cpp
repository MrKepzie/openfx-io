#include <stdio.h>
#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"
#include "ReadFFmpeg.h"
#include "WriteFFmpeg.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"
#include "OCIOColorSpace.h"

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
      static ReadFFmpegPluginFactory p3("fr.inria.openfx:ReadFFmpeg", 1, 0);
      ids.push_back(&p3);
      static WriteFFmpegPluginFactory p4("fr.inria.openfx:WriteFFmpeg", 1, 0);
      ids.push_back(&p4);
      static ReadOIIOPluginFactory p5("fr.inria.openfx:ReadOIIO", 1, 0);
      ids.push_back(&p5);
      static WriteOIIOPluginFactory p6("fr.inria.openfx:WriteOIIO", 1, 0);
      ids.push_back(&p6);
      static OCIOColorSpacePluginFactory p7("fr.inria.openfx:OCIOColorSpace", 1, 0);
      ids.push_back(&p7);
    }
  }
}
