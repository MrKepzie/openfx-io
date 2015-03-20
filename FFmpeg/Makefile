PLUGINOBJECTS = \
	ReadFFmpeg.o FFmpegFile.o WriteFFmpeg.o \
	PluginRegistration.o \
	GenericReader.o GenericWriter.o GenericOCIO.o SequenceParsing.o
PLUGINNAME = FFmpeg

include ../Makefile.master
RESOURCES = \
fr.inria.openfx.ReadFFmpeg.png \
fr.inria.openfx.ReadFFmpeg.svg \
fr.inria.openfx.WriteFFmpeg.png \
fr.inria.openfx.WriteFFmpeg.svg 

CXXFLAGS += $(FFMPEG_CXXFLAGS) $(OCIO_CXXFLAGS)
LINKFLAGS += $(FFMPEG_LINKFLAGS) $(OCIO_LINKFLAGS)
