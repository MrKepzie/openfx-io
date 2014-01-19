PLUGINOBJECTS = oiioReader.o ../GenericReader.o ../Lut.o
PLUGINNAME = oiioReader

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenImageIO`
LINKFLAGS += `pkg-config --libs OpenImageIO`
