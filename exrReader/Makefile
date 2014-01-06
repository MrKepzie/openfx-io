PLUGINOBJECTS = exrReader.o ../GenericReader.o ../Lut.o
PLUGINNAME = exrReader

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenEXR`
LINKFLAGS += `pkg-config --libs OpenEXR`
