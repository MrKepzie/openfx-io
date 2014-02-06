openfx-io [![Build Status](https://api.travis-ci.org/MrKepzie/openfx-io.png?branch=master)](https://travis-ci.org/MrKepzie/openfx-io)

=========

A set of Readers/Writers plugins written using the OpenFX standard.

Compiling on Ubuntu 12.04 LTS:
-------------------------------

### *OpenColorIO*
    sudo apt-get install cmake libtinyxml-dev liblcms2-dev libyaml-cpp-dev libboost-dev
    git clone -b "v1.0.8" https://github.com/imageworks/OpenColorIO.git ocio
    cd ocio
    mkdir _build
    cd _build
    cmake .. -DCMAKE_INSTALL_PREFIX=/opt/ocio -DCMAKE_BUILD_TYPE=Release -DOCIO_BUILD_JNIGLUE=OFF -DOCIO_BUILD_NUKE=OFF -DOCIO_BUILD_SHARED=ON -DOCIO_BUILD_STATIC=OFF -DOCIO_STATIC_JNIGLUE=OFF -DOCIO_BUILD_TRUELIGHT=OFF -DUSE_EXTERNAL_LCMS=ON -DUSE_EXTERNAL_TINYXML=ON -DUSE_EXTERNAL_YAML=ON -DOCIO_BUILD_APPS=OFF -DOCIO_USE_BOOST_PTR=ON -DOCIO_BUILD_TESTS=OFF -DOCIO_BUILD_PYGLUE=OFF
    make && sudo make install
    cd ../..
    
### *OpenEXR*

    sudo apt-get install libopenexr-dev libilmbase-dev
    
### *OpenImageIO* (for some reason, freetype is not recognized)

    sudo apt-get install libopenjpeg-dev libtiff-dev libjpeg-dev libpng-dev libboost-filesystem-dev libboost-regex-dev libboost-thread-dev libboost-system-dev libwebp-dev libfreetype6-dev libssl-dev
    git clone -b "RB-1.2" git://github.com/OpenImageIO/oiio.git oiio
    cd oiio
    make USE_QT=0 USE_TBB=0 USE_PYTHON=0 USE_FIELD3D=0 USE_OPENJPEG=1 USE_OCIO=1 OIIO_BUILD_TESTS=0 OIIO_BUILD_TOOLS=0 OCIO_HOME=/opt/ocio INSTALLDIR=/opt/oiio dist_dir=. cmake
    sudo make dist_dir=.
    cd ..
### *FFmpeg*

    sudo apt-get install libavcodec-dev libavformat-dev libswscale-dev libavutil-dev
    
### *Finally*

    make BITS=64 OIIO_HOME=/opt/oiio
    
Compiling on OSX with macports
------------------------------

TODO

Compiling on Windows
--------------------

TODO
