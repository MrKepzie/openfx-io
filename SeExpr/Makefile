PLUGINOBJECTS = \
	SeExpr.o SeNoise.o SeGrain.o \
	ofxsGenerator.o ofxsRectangleInteract.o ofxsRamp.o ofxsTransformInteract.o \
	ofxsOGLTextRenderer.o ofxsOGLFontData.o

PLUGINNAME = SeExpr
RESOURCES = fr.inria.openfx.SeExpr.png fr.inria.openfx.SeExpr.svg

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(SEEXPR_CXXFLAGS)
LINKFLAGS += $(SEEXPR_LINKFLAGS)
