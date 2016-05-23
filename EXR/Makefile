PLUGINOBJECTS = \
	ReadEXR.o WriteEXR.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o ofxsMultiPlane.o
PLUGINNAME = EXR
RESOURCES = fr.inria.openfx.WriteEXR.png \
fr.inria.openfx.WriteEXR.svg \
fr.inria.openfx.ReadEXR.png \
fr.inria.openfx.ReadEXR.svg 

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(OPENEXR_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(OPENEXR_LINKFLAGS) $(OCIO_LINKFLAGS)
