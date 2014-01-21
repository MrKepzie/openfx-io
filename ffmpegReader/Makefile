PLUGINOBJECTS = ffmpegReader.o FfmpegHandler.o GenericReader.o Lut.o
PLUGINNAME = ffmpegReader
VPATH += ../IOSupport

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags libavformat libavcodec libswscale libavutil`
LINKFLAGS += `pkg-config --libs libavformat libavcodec libswscale libavutil`
