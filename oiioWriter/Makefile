PLUGINOBJECTS = oiioWriter.o ../GenericWriter.o ../Lut.o
PLUGINNAME = oiioWriter

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenImageIO`
LINKFLAGS += `pkg-config --libs OpenImageIO`
