PLUGINOBJECTS = ReadEXR.o WriteEXR.o PluginRegistration.o GenericReader.o GenericWriter.o Lut.o OCIO.o
PLUGINNAME = EXR
VPATH += ../IOSupport

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenEXR`
LINKFLAGS += `pkg-config --libs OpenEXR`
