PLUGINOBJECTS = oiioReader.o GenericReader.o Lut.o
PLUGINNAME = oiioReader
VPATH += ../IOSupport

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenImageIO`
LINKFLAGS += `pkg-config --libs OpenImageIO`
