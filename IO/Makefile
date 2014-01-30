PLUGINOBJECTS = ReadEXR.o WriteEXR.o ReadFFmpeg.o FFmpegHandler.o WriteFFmpeg.o ReadOIIO.o WriteOIIO.o GenericReader.o GenericWriter.o GenericOCIO.o PluginRegistrationCombined.o SequenceParser.o
PLUGINNAME = IO
VPATH += ../IOSupport ../EXR ../FFmpeg ../OIIO

include ../Makefile.master

CXXFLAGS += -I../EXR -I../FFmpeg -I../OIIO

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
