PLUGINOBJECTS = \
	ReadOIIO.o WriteOIIO.o \
	OIIOText.o OIIOResize.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o \
	ofxsOGLTextRenderer.o ofxsOGLFontData.o

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

include ../Makefile.master

CXXFLAGS += $(OIIO_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(OIIO_LINKFLAGS) $(OCIO_LINKFLAGS)
