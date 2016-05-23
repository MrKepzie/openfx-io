PLUGINOBJECTS = \
	ReadFFmpeg.o FFmpegFile.o WriteFFmpeg.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o ofxsMultiPlane.o
PLUGINNAME = FFmpeg

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

RESOURCES = \
fr.inria.openfx.ReadFFmpeg.png \
fr.inria.openfx.ReadFFmpeg.svg \
fr.inria.openfx.WriteFFmpeg.png \
fr.inria.openfx.WriteFFmpeg.svg 

CXXFLAGS += $(FFMPEG_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(FFMPEG_LINKFLAGS) $(OCIO_LINKFLAGS)
