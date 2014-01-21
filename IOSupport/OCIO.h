#ifndef _openfx_io_ocio_h_
#define _openfx_io_ocio_h_

#ifdef IO_USING_OCIO
#include <vector>
#include <string>

#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;

namespace OCIO_OFX {
/**
 * @brief Reads the OpenColorIO config file pointed to by filename and extract from it a list of the color spaces names
 * available in that config as-well as the default color-space index.
 * If filename is NULL, then it will read the environment variable OCIO which should point to a OpenColorIO config file.
 **/
void openOCIOConfigFile(std::vector<std::string>* colorSpaces,int* defaultColorSpaceIndex,const char* filename = NULL,
                         std::string occioRoleHint = std::string());
}
#endif

#endif
