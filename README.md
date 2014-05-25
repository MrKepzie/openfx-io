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

Download and install the MacPorts installer that corresponds to your Mac OS X version.
Download the local portfiles archive on the bottom of [that](http://devernay.free.fr/hacks/openfx/)
 page, named TuttleOFX-OSX-macports.tar.gz .
Uncompress the portfiles in a directory of your choice. In the following, we suppose you
uncompressed it in /Users/USER_NAME/Development/dports-dev, but it can be anywhere
except in the Documents directory, which has special permissions.

Give read/execute permissions to the local repository:

	 "chmod 755 /Users/USER_NAME/Development/dports-dev"

Check that the user "nobody" can read this directory by typing the following command in a Terminal:

	 "sudo -u nobody ls /Users/USER_NAME/Development/dports-dev"
	 
 If it fails, then try again after having given execution permissions on your home directory
using the following command:

	 "chmod o+x /Users/USER_NAME"
	 
 If this still fails, then something is really wrong.

Edit the sources.conf file for MacPorts, for example using the nano editor: 

	sudo nano /opt/local/etc/macports/sources.conf
	
insert at the beginning of the file the configuration for a local repository 
(read the comments in the file), by inserting the line 

	file:///Users/USER_NAME/Development/dports-dev
	
Save and exit (if you're using nano, this means typing ctrl-X, Y and return).
 
Update MacPorts: 

	sudo port selfupdate

Recreate the index in the local repository: 

	cd /Users/USER_NAME/Development/dports-dev; portindex
	
(no need to be root for this)

Add the following line to /opt/local/etc/macports/variants.conf:

	-x11 +no_x11 +bash_completion +no_gnome +quartz
	
(add +universal on OSX 10.5 and 10.6)

* special portfiles:
- graphics/openimageio
- graphics/opencolorio

* external libraries

	sudo port -v install \
	openexr \
	ffmpeg \
	opencolorio \
	openimageio
	
Then to compile...

	make BITS=64 OCIO_HOME=/opt/local OIIO_HOME=/opt/local
	
where /opt/local is where the macports tree stores the includes and libs.

Compiling on Windows
--------------------

We provide pre-compiled static binaries for dependencies here:

https://www.dropbox.com/s/s5yuh9k3kum99jp/3rdparty_windows_32_and_64bits_msvc2010.zip

On the other hand if you want to compile them yourself, you can do so :

### *OpenColorIO*
Clone the repository from github:

	git clone https://github.com/imageworks/OpenColorIO
	
See (this page) [https://github.com/MrKepzie/Natron/wiki/Compiling-OpenColorIO-static-on-Windows]

### *OpenEXR*

Clone the repository from github:

	git clone https://github.com/openexr/openexr
	
And follow the install instruction in IlmBase/README.win32

### *OpenImageIO*
Clone the repository from github:

	git clone https://github.com/OpenImageIO/oiio
	
Follow the instructions in the INSTALL file of the source distribution.
When compiling to use the pre-compiled binaries of OpenEXR provided as the version is too
old to successfully build OpenImageIO. Instead use the version you just compiled before
and change the include/lib paths in the visual studio project.


### *FFmpeg*

I can only advice you download the pre-built binaries from [Zeranoe](http://ffmpeg.zeranoe.com/builds/).
You will need to download the Shared to have the dlls and the Dev to have the .lib files allowing you to link with the dlls.
Note that on this website you can only get shared versions of the ffmpeg libraries.

### *Boost*

You can downlad pre-built binaries for your visual studio version here:

http://boost.teeks99.com/


### *Note about the external libraries*

Please note that all libraries should be compiled as static libraries for the simplicity of the IO.ofx deployment.
You should have all libraries as static EXCEPT ffmpeg. We will discuss at the end of the README how to deploy so the 
library loader can find the shared libraries at run time.

### *Compile IO.sln*

There's a solution file in the repository whose purpose is to help building openfx-io on Windows.
Note that you might have to make a few changes to the Configuration properties of the project so it can compile on your machine.
You will need to change all the hardcoded path in the Additional Include directories to the ones on your machine.
You will have to do the same with Additional Dependencies in the linker options.

Also, some preprocessor definitions are mandatory, most notably:

OFX_IO_USING_OCIO
OFX_EXTENSIONS_VEGAS
OFX_EXTENSIONS_TUTTLE
OFX_EXTENSIONS_NUKE
OFX_IO_MT_FFMPEG
OpenColorIO_STATIC
_WINDOWS
WIN64 (if you're doing a x64 build)
NOMINMAX

Also note that you'll have to set the runtime library (in the Code Generation tab) to Multi-threaded (/MT).


Once you compile successfully and you have your bundle, locate the IO.ofx file.
We assume from now on that the only shared libraries you linked with was ffmpeg (i.e: avcodec.lib, avformat.lib avutil.lib and swscale.lib).
Make a new file called INRIA.IO.manifest right next to IO.ofx in the bundle (i.e: at this location: IO.ofx.bundle/Content/Win64/INRIA.IO.manifest).
In this file copy the following lines:

	<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
	<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">

	<assemblyIdentity name="INRIA.IO" version="1.0.0.0" type="win32" processorArchitecture="amd64"/>

	<file name="avcodec-55.dll">
	</file>

	<file name="avformat-55.dll">
	</file>

	<file name="avutil-52.dll">
	</file>

	<file name="swscale-2.dll">
	</file>

	</assembly>

Now open the command line tool and navigate to IO.ofx.bundle/Content/Win64/
Execute the following command:

	mt -manifest INRIA.IO.manifest -outputresource:IO.ofx;2
	
This will embed the manifest into the .ofx file so it can now find at runtime the shared dependencies (i.e: the ffmpeg Dlls).




