PLUGINOBJECTS = \
	ReadEXR.o WriteEXR.o \
	ReadFFmpeg.o FFmpegHandler.o WriteFFmpeg.o \
	OCIOColorSpace.o \
	OCIOCDLTransform.o \
	OCIOFileTransform.o \
	OCIOLogConvert.o \
	ReadOIIO.o WriteOIIO.o \
    OIIOText.o \
    OIIOResize.o \
	ReadPFM.o WritePFM.o \
	RunScript.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o \
	PluginRegistrationCombined.o

PLUGINNAME = IO
VPATH += ../EXR ../FFmpeg ../OIIO ../PFM ../OCIO ../RunScript \
../IOSupport/SequenceParsing

include ../Makefile.master

CXXFLAGS += -I../EXR -I../FFmpeg -I../OIIO -I../PFM -I../OCIO -I../RunScript

# OpenEXR
CXXFLAGS +=`pkg-config --cflags OpenEXR`
LINKFLAGS += `pkg-config --libs OpenEXR`

# FFmpeg
CXXFLAGS +=`pkg-config --cflags libavformat libavcodec libswscale libavutil`
LINKFLAGS += `pkg-config --libs libavformat libavcodec libswscale libavutil` 

# OpenImageIO
OIIO_HOME ?= /usr
CXXFLAGS += -I$(OIIO_HOME)/include
LINKFLAGS += -L$(OIIO_HOME)/lib -lOpenImageIO
ifeq ($(OS),Linux)
LINKFLAGS += -Wl,-rpath,$(OIIO_HOME)/lib
endif
