PLUGINOBJECTS = \
	ReadOIIO.o WriteOIIO.o \
	OIIOText.o OIIOResize.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o \
	ofxsOGLTextRenderer.o ofxsOGLFontData.o ofxsMultiPlane.o

PLUGINNAME = OIIO

RESOURCES = \
fr.inria.openfx.OIIOText.png \
fr.inria.openfx.OIIOText.svg \
fr.inria.openfx.ReadOIIO.png \
fr.inria.openfx.ReadOIIO.svg \
fr.inria.openfx.OIIOResize.png \
fr.inria.openfx.OIIOResize.svg \
fr.inria.openfx.WriteOIIO.png \
fr.inria.openfx.WriteOIIO.svg 

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(OPENEXR_CXXFLAGS) $(OIIO_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(OPENEXR_LINKFLAGS) $(OIIO_LINKFLAGS) $(OCIO_LINKFLAGS)
