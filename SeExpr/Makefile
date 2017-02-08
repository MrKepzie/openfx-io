PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
ofxsGenerator.o ofxsRectangleInteract.o ofxsRamp.o ofxsTransformInteract.o \
ofxsOGLTextRenderer.o ofxsOGLFontData.o \
SeExpr.o SeNoise.o SeGrain.o \

PLUGINNAME = SeExpr
RESOURCES = fr.inria.openfx.SeExpr.png fr.inria.openfx.SeExpr.svg fr.inria.openfx.SeExprSimple.png fr.inria.openfx.SeExprSimple.svg

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CXXFLAGS += $(SEEXPR_CXXFLAGS)
LINKFLAGS += $(SEEXPR_LINKFLAGS)
