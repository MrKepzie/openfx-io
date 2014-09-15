PLUGINOBJECTS = \
	ReadOIIO.o WriteOIIO.o \
	PluginRegistration.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o

PLUGINNAME = OIIO

RESOURCES = fr.inria.openfx.OIIOText.png

include ../Makefile.master

OIIO_HOME ?= /usr
CXXFLAGS += -I$(OIIO_HOME)/include
LINKFLAGS += -L$(OIIO_HOME)/lib -lOpenImageIO
