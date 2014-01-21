#include "OCIO.h"

#include <stdexcept>

#ifdef IO_USING_OCIO
namespace OCIO_OFX {
void openOCIOConfigFile(std::vector<std::string>* colorSpaces,int* defaultColorSpaceIndex,const char* filename,std::string occioRoleHint)
{
    *defaultColorSpaceIndex = 0;
    if (occioRoleHint.empty()) {
        occioRoleHint = std::string(OCIO::ROLE_SCENE_LINEAR);
    }
    try
    {
        OCIO::ConstConfigRcPtr config;
        if (filename) {
            config = OCIO::Config::CreateFromFile(filename);
        } else {
            config = OCIO::Config::CreateFromEnv();
        }
        OCIO::SetCurrentConfig(config);
        
        OCIO::ConstColorSpaceRcPtr defaultcs = config->getColorSpace(occioRoleHint.c_str());
        if(!defaultcs){
            throw std::runtime_error("ROLE_COMPOSITING_LOG not defined.");
        }
        std::string defaultColorSpaceName = defaultcs->getName();
        
        for(int i = 0; i < config->getNumColorSpaces(); ++i)
        {
            std::string csname = config->getColorSpaceNameByIndex(i);
            colorSpaces->push_back(csname);
            
            if(csname == defaultColorSpaceName)
            {
                *defaultColorSpaceIndex = i;
            }
        }
    }
    catch (OCIO::Exception& e)
    {
        std::cerr << "OCIOColorSpace: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "OCIOColorSpace: Unknown exception during OCIO setup." << std::endl;
    }
    

}
}
#endif
