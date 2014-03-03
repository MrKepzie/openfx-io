#include "ofxsImageEffect.h"
#include "ReadEXR.h"
#include "WriteEXR.h"
#include "ReadFFmpeg.h"
#include "WriteFFmpeg.h"
#include "ReadOIIO.h"
#include "WriteOIIO.h"
#include "OCIOColorSpace.h"
#include "ReadPFM.h"
#include "WritePFM.h"
#ifndef _WIN32
#include "RunScript.h"
#endif

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
      static ReadPFMPluginFactory p7("fr.inria.openfx:ReadPFM", 1, 0);
      ids.push_back(&p7);
      static WritePFMPluginFactory p8("fr.inria.openfx:WritePFM", 1, 0);
      ids.push_back(&p8);
      static OCIOColorSpacePluginFactory p9("fr.inria.openfx:OCIOColorSpace", 1, 0);
      ids.push_back(&p9);
#ifndef _WIN32
      static RunScriptPluginFactory p10("fr.inria.openfx:RunScript", 1, 0);
      ids.push_back(&p10);
#endif
    }
  }
}
