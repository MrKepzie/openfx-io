#include "ofxsImageEffect.h"
#include "ReadFFmpeg.h"
#include "WriteFFmpeg.h"

namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            getReadFFmpegPluginID(ids);
            getWriteFFmpegPluginID(ids);
        }
    }
}
