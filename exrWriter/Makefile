PLUGINOBJECTS = exrWriter.o ../GenericWriter.o ../Lut.o
PLUGINNAME = exrWriter

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenEXR`
LINKFLAGS += `pkg-config --libs OpenEXR`
