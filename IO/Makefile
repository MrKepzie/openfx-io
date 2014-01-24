PLUGINOBJECTS = ReadEXR.o WriteEXR.o ReadFFmpeg.o FFmpegHandler.o WriteFFmpeg.o ReadOIIO.o WriteOIIO.o GenericReader.o GenericWriter.o GenericOCIO.o PluginRegistrationCombined.o SequenceParser.o
PLUGINNAME = IO
VPATH += ../IOSupport ../EXR ../FFmpeg ../OIIO

include ../Makefile.master

CXXFLAGS += -I../EXR -I../FFmpeg -I../OIIO
CXXFLAGS +=`pkg-config --cflags OpenEXR` `pkg-config --cflags libavformat libavcodec libswscale libavutil`
LINKFLAGS += `pkg-config --libs OpenEXR` `pkg-config --libs libavformat libavcodec libswscale libavutil` -lOpenImageIO
