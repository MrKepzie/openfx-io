cmake_minimum_required(VERSION 2.8) 

project(openfx-io)

# Check for OpenFX
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/include)
    message(FATAL_ERROR "\n OpenFX is missing, please update your repository.\n")
endif()

# Add extra cmake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# OfxSupport
add_subdirectory("${PROJECT_SOURCE_DIR}/openfx")
include_directories("${CMAKE_SOURCE_DIR}/openfx/Support/Plugins/include")
target_compile_definitions(OfxSupport PUBLIC OFX_EXTENSIONS_VEGAS OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_TUTTLE OFX_EXTENSIONS_NATRON OFX_SUPPORTS_OPENGLRENDER)

# SupportExt
include_directories("${CMAKE_SOURCE_DIR}/SupportExt")

# IOSupport
add_definitions(-DOFX_IO_USING_OCIO)
include_directories("${CMAKE_SOURCE_DIR}/IOSupport" "${CMAKE_SOURCE_DIR}/SupportExt/glad")

# Find packages
find_package(FFmpeg)
find_package(OpenColorIO)
find_package(OpenEXR)
find_package(OpenImageIO)
find_package(SeExpr)

# IO.ofx
create_ofx_plugin(IO 
    "EXR;FFmpeg;OCIO;OIIO;PFM;PNG;SeExpr" 
    "${OCIO_LIBRARIES};${FFMPEG_LIBRARIES};${OPENEXR_HALF_LIBRARY};${OPENEXR_IEX_LIBRARY};${OPENEXR_IMATH_LIBRARY};${OPENEXR_ILMIMF_LIBRARY};${OPENEXR_ILMTHREAD_LIBRARY};${OpenImageIO_LIBRARY};${SEEXPR_LIBRARIES};${PNG_LIBRARIES}" 
    "EXR/fr.inria.openfx.ReadEXR.png;EXR/fr.inria.openfx.ReadEXR.svg;EXR/fr.inria.openfx.WriteEXR.png;EXR/fr.inria.openfx.WriteEXR.svg;FFmpeg/fr.inria.openfx.ReadFFmpeg.png;FFmpeg/fr.inria.openfx.ReadFFmpeg.svg;FFmpeg/fr.inria.openfx.WriteFFmpeg.png;FFmpeg/fr.inria.openfx.WriteFFmpeg.svg;OCIO/fr.inria.openfx.OCIOCDLTransform.png;OCIO/fr.inria.openfx.OCIOColorSpace.png;OCIO/fr.inria.openfx.OCIODisplay.png;OCIO/fr.inria.openfx.OCIOFileTransform.png;OCIO/fr.inria.openfx.OCIOLogConvert.png;OCIO/fr.inria.openfx.OCIOLookTransform.png;OIIO/fr.inria.openfx.OIIOText.png;OIIO/fr.inria.openfx.OIIOText.svg;OIIO/fr.inria.openfx.ReadOIIO.png;OIIO/fr.inria.openfx.ReadOIIO.svg;OIIO/fr.inria.openfx.OIIOResize.png;OIIO/fr.inria.openfx.OIIOResize.svg;SeExpr/fr.inria.openfx.SeExpr.png;SeExpr/fr.inria.openfx.SeExpr.svg;OIIO/fr.inria.openfx.WriteOIIO.png;OIIO/fr.inria.openfx.WriteOIIO.svg;PFM/fr.inria.openfx.ReadPFM.png;PFM/fr.inria.openfx.ReadPFM.svg;PFM/fr.inria.openfx.WritePFM.png;PFM/fr.inria.openfx.WritePFM.svg;PNG/fr.inria.openfx.ReadPNG.png;PNG/fr.inria.openfx.ReadPNG.svg;PNG/fr.inria.openfx.WritePNG.png;PNG/fr.inria.openfx.WritePNG.svg" 
    "${OCIO_INCLUDES};${FFMPEG_INCLUDE_DIRS};${OPENEXR_INCLUDE_DIR};;${OpenImageIO_INCLUDE_DIR};${SEEXPR_INCLUDE_DIRS};${PNG_INCLUDE_DIRS}"
    "${CMAKE_SOURCE_DIR}/IOSupport/GenericReader.cpp;${CMAKE_SOURCE_DIR}/IOSupport/GenericWriter.cpp;${CMAKE_SOURCE_DIR}/IOSupport/GenericOCIO.cpp;${CMAKE_SOURCE_DIR}/IOSupport/GenericOCIOOpenGL.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsFileOpen.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsOGLTextRenderer.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsOGLFontData.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsRamp.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsGenerator.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsTransformInteract.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsMultiPlane.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsRectangleInteract.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsLut.cpp;${CMAKE_SOURCE_DIR}/SupportExt/ofxsOGLUtilities.cpp")

