PLUGINOBJECTS = ReadFFmpeg.o FFmpegHandler.o WriteFFmpeg.o PluginRegistration.o GenericReader.o GenericWriter.o OCIO.o Lut.o
PLUGINNAME = FFmpeg
VPATH += ../IOSupport

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags libavformat libavcodec libswscale libavutil`
LINKFLAGS += `pkg-config --libs libavformat libavcodec libswscale libavutil`
