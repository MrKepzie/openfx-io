TOP_SRCDIR = ..

PLUGINOBJECTS = \
	ReadEXR.o WriteEXR.o \
	ReadFFmpeg.o FFmpegFile.o WriteFFmpeg.o \
	ReadOIIO.o WriteOIIO.o \
	OIIOText.o \
	OIIOResize.o \
	ReadPFM.o WritePFM.o \
	OCIOColorSpace.o \
	OCIODisplay.o \
	OCIOCDLTransform.o \
	OCIOFileTransform.o \
	OCIOLogConvert.o \
	OCIOLookTransform.o \
	ofxsOGLTextRenderer.o \
	ofxsOGLFontData.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o \
	SeExpr.o \
	SeGrain.o \
	SeNoise.o \
	ofxsRamp.o \
	ofxsGenerator.o \
	ofxsTransformInteract.o \
	ofxsRectangleInteract.o

ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
    PLUGINOBJECTS += RunScript.o
endif

PLUGINNAME = IO

# ls */*png */*svg|sed -e 's!^!../!' -e 's!$! \\!'|sort
RESOURCES = \
$(TOP_SRCDIR)/EXR/fr.inria.openfx.ReadEXR.png \
$(TOP_SRCDIR)/EXR/fr.inria.openfx.ReadEXR.svg \
$(TOP_SRCDIR)/EXR/fr.inria.openfx.WriteEXR.png \
$(TOP_SRCDIR)/EXR/fr.inria.openfx.WriteEXR.svg \
$(TOP_SRCDIR)/FFmpeg/fr.inria.openfx.ReadFFmpeg.png \
$(TOP_SRCDIR)/FFmpeg/fr.inria.openfx.ReadFFmpeg.svg \
$(TOP_SRCDIR)/FFmpeg/fr.inria.openfx.WriteFFmpeg.png \
$(TOP_SRCDIR)/FFmpeg/fr.inria.openfx.WriteFFmpeg.svg \
$(TOP_SRCDIR)/OCIO/fr.inria.openfx.OCIOCDLTransform.png \
$(TOP_SRCDIR)/OCIO/fr.inria.openfx.OCIOColorSpace.png \
$(TOP_SRCDIR)/OCIO/fr.inria.openfx.OCIODisplay.png \
$(TOP_SRCDIR)/OCIO/fr.inria.openfx.OCIOFileTransform.png \
$(TOP_SRCDIR)/OCIO/fr.inria.openfx.OCIOLogConvert.png \
$(TOP_SRCDIR)/OCIO/fr.inria.openfx.OCIOLookTransform.png \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.OIIOText.png \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.OIIOText.svg \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.ReadOIIO.png \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.ReadOIIO.svg \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.OIIOResize.png \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.OIIOResize.svg \
$(TOP_SRCDIR)/SeExpr/fr.inria.openfx.SeExpr.png \
$(TOP_SRCDIR)/SeExpr/fr.inria.openfx.SeExpr.svg \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.WriteOIIO.png \
$(TOP_SRCDIR)/OIIO/fr.inria.openfx.WriteOIIO.svg \
$(TOP_SRCDIR)/PFM/fr.inria.openfx.ReadPFM.png \
$(TOP_SRCDIR)/PFM/fr.inria.openfx.ReadPFM.svg \
$(TOP_SRCDIR)/PFM/fr.inria.openfx.WritePFM.png \
$(TOP_SRCDIR)/PFM/fr.inria.openfx.WritePFM.svg

ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
RESOURCES += ../RunScript/fr.inria.openfx.RunScript.png \
             ../RunScript/fr.inria.openfx.RunScript.svg 
endif

VPATH += \
$(TOP_SRCDIR)/EXR \
$(TOP_SRCDIR)/FFmpeg \
$(TOP_SRCDIR)/OCIO \
$(TOP_SRCDIR)/OIIO \
$(TOP_SRCDIR)/PFM \
$(TOP_SRCDIR)/SeExpr \
$(TOP_SRCDIR)/IOSupport/SequenceParsing

ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
VPATH += $(TOP_SRCDIR)/RunScript
endif

include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += -I$(TOP_SRCDIR)/EXR -I$(TOP_SRCDIR)/FFmpeg -I$(TOP_SRCDIR)/OIIO -I$(TOP_SRCDIR)/PFM -I$(TOP_SRCDIR)/OCIO -I$(TOP_SRCDIR)/SeExpr

ifeq ($(OS),$(filter $(OS),Linux FreeBSD Darwin))
CXXFLAGS += -I$(TOP_SRCDIR)/RunScript
endif

CXXFLAGS  += $(OPENEXR_CXXFLAGS)  $(FFMPEG_CXXFLAGS)  $(OIIO_CXXFLAGS)  $(SEEXPR_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(OPENEXR_LINKFLAGS) $(FFMPEG_LINKFLAGS) $(OIIO_LINKFLAGS)  $(SEEXPR_LINKFLAGS) $(OCIO_LINKFLAGS)
