PLUGINOBJECTS = \
	ReadEXR.o WriteEXR.o \
	PluginRegistration.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o
PLUGINNAME = EXR
RESOURCES = fr.inria.openfx.WriteEXR.png \
fr.inria.openfx.WriteEXR.svg \
fr.inria.openfx.ReadEXR.png \
fr.inria.openfx.ReadEXR.svg 

include ../Makefile.master

CXXFLAGS += `pkg-config --cflags OpenEXR`
LINKFLAGS += `pkg-config --libs OpenEXR`
