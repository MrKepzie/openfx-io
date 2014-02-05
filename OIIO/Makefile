PLUGINOBJECTS = \
	ReadOIIO.o WriteOIIO.o \
	PluginRegistration.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParser.o

PLUGINNAME = OIIO
VPATH += ../IOSupport

include ../Makefile.master

OIIO_HOME ?= /usr
CXXFLAGS += -I$(OIIO_HOME)/include
LINKFLAGS += -L$(OIIO_HOME)/lib -lOpenImageIO
