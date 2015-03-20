PLUGINOBJECTS = \
	ReadEXR.o WriteEXR.o \
	ReadFFmpeg.o FFmpegFile.o WriteFFmpeg.o \
	ReadOIIO.o WriteOIIO.o \
	OIIOText.o \
	OIIOResize.o \
	ReadPFM.o WritePFM.o \
	OCIOColorSpace.o \
	OCIOCDLTransform.o \
	OCIOFileTransform.o \
	OCIOLogConvert.o \
	OCIOLookTransform.o \
	RunScript.o \
	ofxsOGLTextRenderer.o \
	ofxsOGLFontData.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o \
        SeExpr.o \
	ofxsGenerator.o \
	ofxsRectangleInteract.o \
	PluginRegistrationCombined.o

PLUGINNAME = IO

# ls */*png */*svg|sed -e 's!^!../!' -e 's!$! \\!'|sort
RESOURCES = \
../EXR/fr.inria.openfx.ReadEXR.png \
../EXR/fr.inria.openfx.ReadEXR.svg \
../EXR/fr.inria.openfx.WriteEXR.png \
../EXR/fr.inria.openfx.WriteEXR.svg \
../FFmpeg/fr.inria.openfx.ReadFFmpeg.png \
../FFmpeg/fr.inria.openfx.ReadFFmpeg.svg \
../FFmpeg/fr.inria.openfx.WriteFFmpeg.png \
../FFmpeg/fr.inria.openfx.WriteFFmpeg.svg \
../OIIO/fr.inria.openfx.OIIOText.png \
../OIIO/fr.inria.openfx.OIIOText.svg \
../OIIO/fr.inria.openfx.ReadOIIO.png \
../OIIO/fr.inria.openfx.ReadOIIO.svg \
../OIIO/fr.inria.openfx.WriteOIIO.png \
../OIIO/fr.inria.openfx.WriteOIIO.svg \
../PFM/fr.inria.openfx.ReadPFM.png \
../PFM/fr.inria.openfx.ReadPFM.svg \
../PFM/fr.inria.openfx.WritePFM.png \
../PFM/fr.inria.openfx.WritePFM.svg \
../RunScript/fr.inria.openfx.RunScript.png \
../RunScript/fr.inria.openfx.RunScript.svg \

VPATH += \
../EXR \
../FFmpeg \
../OCIO \
../OIIO \
../PFM \
../RunScript \
../SeExpr \
../IOSupport/SequenceParsing

include ../Makefile.master

CXXFLAGS += -I../EXR -I../FFmpeg -I../OIIO -I../PFM -I../OCIO -I../RunScript -I../SeExpr

CXXFLAGS  += $(OPENEXR_CXXFLAGS)  $(FFMPEG_CXXFLAGS)  $(OIIO_CXXFLAGS)  $(SEEXPR_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(OPENEXR_LINKFLAGS) $(FFMPEG_LINKFLAGS) $(OIIO_LINKFLAGS)  $(SEEXPR_LINKFLAGS) $(OCIO_LINKFLAGS)
