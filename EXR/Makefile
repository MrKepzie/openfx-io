PLUGINOBJECTS = \
	ReadEXR.o WriteEXR.o \
	PluginRegistration.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o
PLUGINNAME = EXR

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenEXR`
LINKFLAGS += `pkg-config --libs OpenEXR`
