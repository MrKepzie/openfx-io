PLUGINOBJECTS = \
	ReadPNG.o WritePNG.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o ofxsMultiPlane.o ofxsFileOpen.o ofxsLut.o

PLUGINNAME = PNG

RESOURCES = \
fr.inria.openfx.ReadPNG.png \
fr.inria.openfx.ReadPNG.svg \
fr.inria.openfx.WritePNG.png \
fr.inria.openfx.WritePNG.svg

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(PNG_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(PNG_CXXFLAGS) $(OCIO_LINKFLAGS)
