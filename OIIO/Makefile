PLUGINOBJECTS = ReadOIIO.o WriteOIIO.o GenericReader.o GenericWriter.o GenericOCIO.o Lut.o
PLUGINNAME = OIIO
VPATH += ../IOSupport

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenImageIO`
LINKFLAGS += `pkg-config --libs OpenImageIO`
