PLUGINOBJECTS = \
	ReadPFM.o WritePFM.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o ofxsMultiPlane.o

PLUGINNAME = PFM

RESOURCES = \
fr.inria.openfx.ReadPFM.png \
fr.inria.openfx.ReadPFM.svg \
fr.inria.openfx.WritePFM.png \
fr.inria.openfx.WritePFM.svg  

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(OCIO_CXXFLAGS)
LINKFLAGS += $(OCIO_LINKFLAGS)
