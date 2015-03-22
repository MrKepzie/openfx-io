PLUGINOBJECTS = \
	ReadRaw.o \
	PluginRegistration.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o

PLUGINNAME = Raw

#RESOURCES = fr.inria.openfx.ReadRaw.png fr.inria.openfx.ReadRaw.svg  

include ../Makefile.master

CXXFLAGS += $(OCIO_CXXFLAGS)
LINKFLAGS += $(OCIO_LINKFLAGS)
