PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o OCIOCDLTransform.o OCIOColorSpace.o OCIODisplay.o OCIOFileTransform.o OCIOLogConvert.o OCIOLookTransform.o GenericOCIO.o $(OCIO_OPENGL_OBJS)
OCIO_OPENGL_OBJS = GenericOCIOOpenGL.o glad.o ofxsOGLUtilities.o
PLUGINNAME = OCIO

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(OCIO_CXXFLAGS)
LINKFLAGS += $(OCIO_LINKFLAGS)
