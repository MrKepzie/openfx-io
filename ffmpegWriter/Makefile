PLUGINOBJECTS = ffmpegWriter.o ../GenericWriter.o ../Lut.o
PLUGINNAME = ffmpegWriter

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags libavformat libavcodec libswscale libavutil`
LINKFLAGS += `pkg-config --libs libavformat libavcodec libswscale libavutil`
